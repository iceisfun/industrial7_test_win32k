/* Region algebra and queries: NtGdiCombineRgn (both the gdi32 attr fast path and the kernel), GetRgnBox,
 * PtInRegion, RectInRegion, EqualRgn, OffsetRgn, GetRegionData buffer protocol. */
#include "rgn.h"

#define READY() do { if (!U(CreateRectRgn) || !U(DeleteObject) || !U(CombineRgn) || !UN(NtGdiCombineRgn) || !UN(NtGdiGetRgnBox) || !UN(NtGdiGetRegionData) || !UN(NtGdiCreateRectRgn)) { t_skip("exports missing"); return; } } while (0)

static void del(HRGN h) { if (h) U(DeleteObject)(h); }

/* ---- CombineRgn: the five modes ---------------------------------------------------------------------- */

struct combo { int mode; int cplx; int n; RECT r[4]; };
/* A=(0,0,10,10) B=(5,5,15,15): band decomposition is top-to-bottom, left-to-right; consecutive bands are only
 * merged when their x-spans are identical. */
static const struct combo g_ab[] = {
    { RGN_AND,  RC_SIMPLE,  1, { { 5, 5, 10, 10 } } },
    { RGN_OR,   RC_COMPLEX, 3, { { 0, 0, 10, 5 }, { 0, 5, 15, 10 }, { 5, 10, 15, 15 } } },
    { RGN_XOR,  RC_COMPLEX, 4, { { 0, 0, 10, 5 }, { 0, 5, 5, 10 }, { 10, 5, 15, 10 }, { 5, 10, 15, 15 } } },
    { RGN_DIFF, RC_COMPLEX, 2, { { 0, 0, 10, 5 }, { 0, 5, 5, 10 } } },
    { RGN_COPY, RC_SIMPLE,  1, { { 0, 0, 10, 10 } } },
};
static const char *mname(int m) { static const char *n[] = { "?", "AND", "OR", "XOR", "DIFF", "COPY" }; return (m >= 1 && m <= 5) ? n[m] : "?"; }

static void check_combo(const struct combo *c, int ret, HRGN dst, const char *how) {
    CHECKF(ret == c->cplx, "%s %s: ret %d want %d", how, mname(c->mode), ret, c->cplx);
    RECT out[8]; int n = rgn_rects(dst, out, 8);
    CHECKF(n == c->n, "%s %s: rect count %d want %d", how, mname(c->mode), n, c->n);
    if (n == c->n) for (int i = 0; i < n; i++)
        CHECKF(box_is(&out[i], c->r[i].left, c->r[i].top, c->r[i].right, c->r[i].bottom), "%s %s: rect %d (%ld,%ld,%ld,%ld)", how, mname(c->mode), i, out[i].left, out[i].top, out[i].right, out[i].bottom);
}

TEST(gdi_regions_clip_combine_modes_userland) {
    READY();
    for (unsigned i = 0; i < sizeof g_ab / sizeof g_ab[0]; i++) {
        HRGN a = U(CreateRectRgn)(0, 0, 10, 10), b = U(CreateRectRgn)(5, 5, 15, 15), d = U(CreateRectRgn)(100, 100, 101, 101);
        int r = U(CombineRgn)(d, a, b, g_ab[i].mode);
        check_combo(&g_ab[i], r, d, "user");
        /* sources are untouched */
        CHECK(rgn_is_rect(a, 0, 0, 10, 10)); CHECK(rgn_is_rect(b, 5, 5, 15, 15));
        del(a); del(b); del(d);
    }
}

TEST(gdi_regions_clip_combine_modes_syscall) {
    READY();
    for (unsigned i = 0; i < sizeof g_ab / sizeof g_ab[0]; i++) {
        HRGN a = kcreate(0, 0, 10, 10), b = kcreate(5, 5, 15, 15), d = kcreate(100, 100, 101, 101);
        int r = UN(NtGdiCombineRgn)(d, a, b, g_ab[i].mode);
        check_combo(&g_ab[i], r, d, "nt");
        del(a); del(b); del(d);
    }
}

TEST(gdi_regions_clip_combine_dest_aliases_source) {
    READY();
    /* which: 0 dst==src1, 1 dst==src2, 2 dst==src1 with swapped sources (b op a).
 * CombineRgn special-cases argument 1 == argument 2 and argument 1 == argument 3 explicitly. */
    for (int which = 0; which < 3; which++) {
        for (unsigned i = 0; i < sizeof g_ab / sizeof g_ab[0]; i++) {
            int m = g_ab[i].mode;
            HRGN a = kcreate(0, 0, 10, 10), b = kcreate(5, 5, 15, 15);
            HRGN dst = (which == 1) ? b : a;
            HRGN s1 = (which == 2) ? b : a, s2 = (which == 2) ? a : b;
            int r = UN(NtGdiCombineRgn)(dst, s1, s2, m);
            if (which == 2 && (m == RGN_DIFF || m == RGN_COPY)) {
                CHECKF(r != RC_ERROR, "which %d %s", which, mname(m));
                if (m == RGN_DIFF) { RECT o[4]; CHECK(rgn_rects(dst, o, 4) == 2); }   /* b - a */
                else CHECK(rgn_is_rect(dst, 5, 5, 15, 15));
            } else check_combo(&g_ab[i], r, dst, "alias");
            del(a); del(b);
        }
    }
}

TEST(gdi_regions_clip_combine_same_region_everywhere) {
    READY();
 /* argument 1 == argument 2 == argument 3: AND/OR leave it unchanged, XOR/DIFF empty it (special case) */
    HRGN h = kcreate(2, 2, 12, 12);
    CHECK(UN(NtGdiCombineRgn)(h, h, h, RGN_AND) == RC_SIMPLE); CHECK(rgn_is_rect(h, 2, 2, 12, 12));
    CHECK(UN(NtGdiCombineRgn)(h, h, h, RGN_OR) == RC_SIMPLE);  CHECK(rgn_is_rect(h, 2, 2, 12, 12));
    CHECK(UN(NtGdiCombineRgn)(h, h, h, RGN_XOR) == RC_NULL);
    RECT bx; CHECK(kbox(h, &bx) == RC_NULL);
    UN(NtGdiSetRectRgn)(h, 2, 2, 12, 12);
    CHECK(UN(NtGdiCombineRgn)(h, h, h, RGN_DIFF) == RC_NULL);
    del(h);
}

static const struct { RECT a, b; } g_pairs[] = {
    { { 0, 0, 10, 10 }, { 5, 5, 15, 15 } },     /* partial overlap */
    { { 0, 0, 10, 10 }, { 2, 2, 8, 8 } },       /* b inside a */
    { { 2, 2, 8, 8 }, { 0, 0, 10, 10 } },       /* a inside b */
    { { 0, 0, 10, 10 }, { 20, 20, 30, 30 } },   /* disjoint */
    { { 0, 0, 10, 10 }, { 10, 0, 20, 10 } },    /* touching right edge */
    { { 0, 0, 10, 10 }, { 0, 10, 10, 20 } },    /* touching bottom edge */
    { { 0, 0, 10, 10 }, { 0, 0, 10, 10 } },     /* identical */
    { { 0, 0, 10, 10 }, { 0, 0, 10, 5 } },      /* shared corner, b is the upper half */
    { { 0, 0, 10, 10 }, { 3, -5, 7, 15 } },     /* cross */
    { { -10, -10, 0, 0 }, { -5, -5, 5, 5 } },   /* negative coordinates */
    { { 0, 0, 10, 10 }, { 4, 4, 4, 9 } },       /* b empty */
    { { 6, 6, 6, 6 }, { 6, 6, 6, 6 } },         /* both empty */
};

TEST(gdi_regions_clip_combine_userland_equals_kernel) {
    READY(); NEED(UN(NtGdiEqualRgn));
    static const int modes[] = { RGN_AND, RGN_OR, RGN_XOR, RGN_DIFF, RGN_COPY };
    for (unsigned p = 0; p < sizeof g_pairs / sizeof g_pairs[0]; p++) {
        for (unsigned mi = 0; mi < 5; mi++) {
            const RECT *a = &g_pairs[p].a, *b = &g_pairs[p].b;
            HRGN ua = U(CreateRectRgn)(a->left, a->top, a->right, a->bottom), ub = U(CreateRectRgn)(b->left, b->top, b->right, b->bottom);
            HRGN ka = kcreate(a->left, a->top, a->right, a->bottom), kb = kcreate(b->left, b->top, b->right, b->bottom);
            HRGN ud = U(CreateRectRgn)(0, 0, 1, 1), kd = kcreate(0, 0, 1, 1);
            int ru = U(CombineRgn)(ud, ua, ub, modes[mi]);
            int rk = UN(NtGdiCombineRgn)(kd, ka, kb, modes[mi]);
            CHECKF(ru == rk, "pair %u %s: user %d kernel %d", p, mname(modes[mi]), ru, rk);
            CHECKF(UN(NtGdiEqualRgn)(ud, kd), "pair %u %s: results differ", p, mname(modes[mi]));
            RECT bu, bk; int cu = kbox(ud, &bu), ck = kbox(kd, &bk);
            CHECKF(cu == ck && box_is(&bu, bk.left, bk.top, bk.right, bk.bottom), "pair %u %s: boxes", p, mname(modes[mi]));
            del(ua); del(ub); del(ka); del(kb); del(ud); del(kd);
        }
    }
}

TEST(gdi_regions_clip_combine_empty_results) {
    READY();
    HRGN a = U(CreateRectRgn)(0, 0, 10, 10), farr = U(CreateRectRgn)(50, 50, 60, 60), d = U(CreateRectRgn)(0, 0, 1, 1);
    CHECK(U(CombineRgn)(d, a, farr, RGN_AND) == RC_NULL);
    RECT bx; CHECK(kbox(d, &bx) == RC_NULL);
    CHECK(U(CombineRgn)(d, a, a, RGN_XOR) == RC_NULL);
    CHECK(U(CombineRgn)(d, a, a, RGN_DIFF) == RC_NULL);
    CHECK(U(CombineRgn)(d, a, farr, RGN_DIFF) == RC_SIMPLE);
    CHECK(rgn_is_rect(d, 0, 0, 10, 10));
    CHECK(U(CombineRgn)(d, a, a, RGN_OR) == RC_SIMPLE);
    CHECK(U(CombineRgn)(d, a, a, RGN_AND) == RC_SIMPLE);
    CHECK(U(CombineRgn)(d, a, farr, RGN_OR) == RC_COMPLEX);
    del(a); del(farr); del(d);
}

TEST(gdi_regions_clip_combine_copy_ignores_second_source) {
    READY();
    HRGN a = U(CreateRectRgn)(3, 3, 9, 9), d = U(CreateRectRgn)(0, 0, 1, 1);
 /* CombineRgn(mode 5) opens only argument 1 and argument 2 */
    CHECK(U(CombineRgn)(d, a, NULL, RGN_COPY) == RC_SIMPLE);
    CHECK(rgn_is_rect(d, 3, 3, 9, 9));
    CHECK(UN(NtGdiCombineRgn)(d, a, (HRGN)(ULONG_PTR)0x77777, RGN_COPY) == RC_SIMPLE);
    del(a); del(d);
}

TEST(gdi_regions_clip_combine_invalid_mode) {
    READY();
    static const int bad[] = { 0, 6, 7, -1, 0x100, 0x7fffffff, (int)0x80000000 };
    HRGN a = kcreate(0, 0, 10, 10), b = kcreate(5, 5, 15, 15), d = kcreate(0, 0, 3, 3);
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SET_ERR(0);
        /* CombineRgn: (mode - 1) > 4 -> EngSetLastError(0x57), 0 */
        CHECKF(UN(NtGdiCombineRgn)(d, a, b, bad[i]) == RC_ERROR, "mode %d", bad[i]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
        CHECKF(rgn_is_rect(d, 0, 0, 3, 3), "mode %d changed dst", bad[i]);
    }
    HRGN ua = U(CreateRectRgn)(0, 0, 10, 10), ub = U(CreateRectRgn)(5, 5, 15, 15), ud = U(CreateRectRgn)(0, 0, 3, 3);
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SET_ERR(0);
        CHECKF(U(CombineRgn)(ud, ua, ub, bad[i]) == RC_ERROR, "user mode %d", bad[i]);
    }
    del(a); del(b); del(d); del(ua); del(ub); del(ud);
}

TEST(gdi_regions_clip_combine_invalid_handles) {
    READY();
    HRGN a = kcreate(0, 0, 10, 10), d = kcreate(0, 0, 3, 3);
    HRGN bogus = (HRGN)(ULONG_PTR)0x00040777;
    HGDIOBJ brush = U(GetStockObject)(BLACK_BRUSH);
    HRGN badh[3] = { NULL, bogus, (HRGN)brush };
    for (int i = 0; i < 3; i++) {
        SET_ERR(0);
        CHECKF(UN(NtGdiCombineRgn)(d, badh[i], a, RGN_COPY) == RC_ERROR, "bad src1 %d", i);
        EXPECT_ERR(ERROR_INVALID_HANDLE);                       /* RGNOBJAPI null -> EngSetLastError(6) */
        SET_ERR(0);
        CHECKF(UN(NtGdiCombineRgn)(badh[i], a, a, RGN_COPY) == RC_ERROR, "bad dst %d", i);
        EXPECT_ERR(ERROR_INVALID_HANDLE);
        CHECKF(UN(NtGdiCombineRgn)(d, a, badh[i], RGN_OR) == RC_ERROR, "bad src2 %d", i);
        CHECKF(UN(NtGdiCombineRgn)(d, badh[i], a, RGN_AND) == RC_ERROR, "bad src1 and %d", i);
        CHECKF(UN(NtGdiCombineRgn)(badh[i], a, a, RGN_XOR) == RC_ERROR, "bad dst xor %d", i);
    }
    CHECK(rgn_is_rect(d, 0, 0, 3, 3));   /* failures never touched the destination */
    del(a); del(d);
}

TEST(gdi_regions_clip_combine_userland_invalid_handles) {
    READY();
    HRGN a = U(CreateRectRgn)(0, 0, 10, 10), d = U(CreateRectRgn)(0, 0, 3, 3);
    CHECK(U(CombineRgn)(d, NULL, a, RGN_OR) == RC_ERROR);
    CHECK(U(CombineRgn)(NULL, a, a, RGN_OR) == RC_ERROR);
    CHECK(U(CombineRgn)(d, a, NULL, RGN_OR) == RC_ERROR);
    CHECK(U(CombineRgn)(d, (HRGN)(ULONG_PTR)0xdead0004, a, RGN_AND) == RC_ERROR);
    CHECK(rgn_is_rect(d, 0, 0, 3, 3));
    del(a); del(d);
}

TEST(gdi_regions_clip_combine_complex_chain) {
    READY();
    /* (A or B) xor C diffed with D: exercises complex/complex kernel paths */
    HRGN a = kcreate(0, 0, 20, 20), b = kcreate(10, 10, 30, 30);
    HRGN c = kcreate(5, 5, 25, 25), dd = kcreate(0, 12, 40, 16), t = kcreate(0, 0, 1, 1);
    CHECK(UN(NtGdiCombineRgn)(t, a, b, RGN_OR) == RC_COMPLEX);
    CHECK(UN(NtGdiCombineRgn)(t, t, c, RGN_XOR) == RC_COMPLEX);
    CHECK(UN(NtGdiCombineRgn)(t, t, dd, RGN_DIFF) == RC_COMPLEX);
    RECT bx; kbox(t, &bx); CHECK_BOX(bx, 0, 0, 30, 30);
    /* sample points computed by hand */
    CHECK(UN(NtGdiPtInRegion)(t, 2, 2));       /* in A only */
    CHECK(!UN(NtGdiPtInRegion)(t, 7, 7));      /* in A and C: xor removes */
    CHECK(!UN(NtGdiPtInRegion)(t, 2, 13));     /* removed by D */
    CHECK(UN(NtGdiPtInRegion)(t, 28, 28));     /* B only */
    CHECK(!UN(NtGdiPtInRegion)(t, 35, 35));
    del(a); del(b); del(c); del(dd); del(t);
}

/* ---- GetRgnBox ----------------------------------------------------------------------------------------- */

TEST(gdi_regions_clip_getrgnbox_complex_and_null) {
    READY();
    HRGN a = kcreate(0, 0, 10, 10), b = kcreate(30, -5, 40, 5), u = kcreate(0, 0, 1, 1);
    UN(NtGdiCombineRgn)(u, a, b, RGN_OR);
    RECT bx; int c = kbox(u, &bx);
    CHECK(c == RC_COMPLEX); CHECK_BOX(bx, 0, -5, 40, 10);
    UN(NtGdiSetRectRgn)(u, 4, 4, 4, 4);
    memset(&bx, 0x7f, sizeof bx);
    c = UN(NtGdiGetRgnBox)(u, &bx);
    CHECK(c == RC_NULL); CHECK_BOX(bx, 0, 0, 0, 0);
    del(a); del(b); del(u);
}

TEST(gdi_regions_clip_getrgnbox_invalid_handles) {
    READY();
    RECT bx;
    memset(&bx, 0x33, sizeof bx);
    CHECK(UN(NtGdiGetRgnBox)(NULL, &bx) == RC_ERROR);
    CHECK(UN(NtGdiGetRgnBox)((HRGN)(ULONG_PTR)0x00040999, &bx) == RC_ERROR);
    CHECK(UN(NtGdiGetRgnBox)((HRGN)U(GetStockObject)(WHITE_BRUSH), &bx) == RC_ERROR);
    /* on error NtGdiGetRgnBox stores nothing */
    CHECKF(((unsigned char *)&bx)[0] == 0x33 && ((unsigned char *)&bx)[15] == 0x33, "buffer touched");
    CHECK(U(GetRgnBox)(NULL, &bx) == RC_ERROR);
}

/* ---- PtInRegion / RectInRegion / EqualRgn ------------------------------------------------------------------ */

TEST(gdi_regions_clip_ptinregion_half_open_edges) {
    READY(); NEED(UN(NtGdiPtInRegion));
    HRGN h = U(CreateRectRgn)(0, 0, 10, 10);
    static const struct { int x, y, in; } t[] = {
        { 0, 0, 1 }, { 9, 9, 1 }, { 5, 5, 1 }, { 9, 0, 1 }, { 0, 9, 1 },
        { 10, 10, 0 }, { 10, 5, 0 }, { 5, 10, 0 }, { -1, 5, 0 }, { 5, -1, 0 }, { -1, -1, 0 },
        { 0x7ffffff, 5, 0 }, { 5, (int)0x80000000, 0 }, { 0x7fffffff, 0x7fffffff, 0 },
    };
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
        CHECKF((UN(NtGdiPtInRegion)(h, t[i].x, t[i].y) != 0) == t[i].in, "(%d,%d)", t[i].x, t[i].y);
        if (U(PtInRegion)) CHECKF((U(PtInRegion)(h, t[i].x, t[i].y) != 0) == t[i].in, "user (%d,%d)", t[i].x, t[i].y);
    }
    del(h);
}

TEST(gdi_regions_clip_ptinregion_invalid_handle_last_error) {
    READY(); NEED(UN(NtGdiPtInRegion));
    SET_ERR(0);
    CHECK(!UN(NtGdiPtInRegion)(NULL, 0, 0));
    EXPECT_ERR(ERROR_INVALID_HANDLE);   /* PtInRegion: error 6 */
    SET_ERR(0);
    CHECK(!UN(NtGdiPtInRegion)((HRGN)(ULONG_PTR)0x00040555, 0, 0));
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    SET_ERR(0);
    CHECK(!UN(NtGdiPtInRegion)((HRGN)U(GetStockObject)(BLACK_PEN), 0, 0));
    EXPECT_ERR(ERROR_INVALID_HANDLE);
}

TEST(gdi_regions_clip_ptinregion_null_region_contains_nothing) {
    READY(); NEED(UN(NtGdiPtInRegion));
    HRGN h = U(CreateRectRgn)(3, 3, 3, 3);
    CHECK(!UN(NtGdiPtInRegion)(h, 3, 3));
    CHECK(!UN(NtGdiPtInRegion)(h, 0, 0));
    del(h);
}

TEST(gdi_regions_clip_rectinregion_overlap_table) {
    READY(); NEED(UN(NtGdiRectInRegion));
    HRGN a = kcreate(0, 0, 10, 10), b = kcreate(20, 0, 30, 10), u = kcreate(0, 0, 1, 1);
    UN(NtGdiCombineRgn)(u, a, b, RGN_OR);
    static const struct { RECT r; int in; } t[] = {
        { { 5, 5, 6, 6 }, 1 }, { { 9, 9, 20, 20 }, 1 }, { { -5, -5, 1, 1 }, 1 }, { { 0, 0, 10, 10 }, 1 }, { { -100, -100, 100, 100 }, 1 },
        { { 10, 0, 20, 10 }, 0 },    /* the gap between the two rectangles */
        { { 10, 10, 20, 20 }, 0 }, { { -5, -5, 0, 0 }, 0 }, { { 0, 10, 10, 20 }, 0 }, { { 30, 0, 40, 10 }, 0 },
        { { 25, 9, 26, 10 }, 1 }, { { 25, 10, 26, 11 }, 0 },
    };
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
        RECT r = t[i].r;
        CHECKF((UN(NtGdiRectInRegion)(u, &r) != 0) == t[i].in, "row %u", i);
        if (U(RectInRegion)) { r = t[i].r; CHECKF((U(RectInRegion)(u, &r) != 0) == t[i].in, "user row %u", i); }
    }
    del(a); del(b); del(u);
}

TEST(gdi_regions_clip_rectinregion_swapped_rect_is_ordered) {
    READY(); NEED(UN(NtGdiRectInRegion));
    HRGN h = kcreate(0, 0, 10, 10);
    RECT r = { 8, 8, 2, 2 };   /* NtGdiRectInRegion orders the corners before calling the kernel */
    BOOL in = UN(NtGdiRectInRegion)(h, &r);
    CHECK(in);
    t_note("swapped rect after call (%ld,%ld,%ld,%ld) (kernel writes the ordered rect back on a hit)", r.left, r.top, r.right, r.bottom);
    RECT farr = { 50, 50, 40, 40 };
    CHECK(!UN(NtGdiRectInRegion)(h, &farr));
    del(h);
}

TEST(gdi_regions_clip_rectinregion_null_rect_and_bad_handle) {
    READY(); NEED(UN(NtGdiRectInRegion));
    HRGN h = kcreate(0, 0, 10, 10);
    RECT r = { 0, 0, 5, 5 };
    CHECK(!UN(NtGdiRectInRegion)(NULL, &r));
    CHECK(!UN(NtGdiRectInRegion)((HRGN)(ULONG_PTR)0x40123, &r));
    del(h);
}

TEST(gdi_regions_clip_equalrgn_cases) {
    READY(); NEED(UN(NtGdiEqualRgn));
    HRGN a = U(CreateRectRgn)(0, 0, 10, 10), b = kcreate(0, 0, 10, 10), c = U(CreateRectRgn)(0, 0, 10, 11);
    CHECK(UN(NtGdiEqualRgn)(a, a));
    CHECK(UN(NtGdiEqualRgn)(a, b));   /* different creation paths, same region */
    CHECK(UN(NtGdiEqualRgn)(b, a));
    CHECK(!UN(NtGdiEqualRgn)(a, c));
    CHECK(!UN(NtGdiEqualRgn)(c, b));
    if (U(EqualRgn)) { CHECK(U(EqualRgn)(a, b)); CHECK(!U(EqualRgn)(a, c)); }
    HRGN n1 = U(CreateRectRgn)(1, 1, 1, 1), n2 = U(CreateRectRgn)(9, 9, 9, 40);
    CHECK(UN(NtGdiEqualRgn)(n1, n2));   /* every NULLREGION is the empty set */
    CHECK(!UN(NtGdiEqualRgn)(n1, a));
    /* complex equality is band-structure equality */
    HRGN p = kcreate(0, 0, 1, 1), q = kcreate(0, 0, 1, 1);
    HRGN r1 = kcreate(0, 0, 10, 10), r2 = kcreate(20, 0, 30, 10);
    UN(NtGdiCombineRgn)(p, r1, r2, RGN_OR); UN(NtGdiCombineRgn)(q, r2, r1, RGN_OR);
    CHECK(UN(NtGdiEqualRgn)(p, q));
    CHECK(!UN(NtGdiEqualRgn)(p, a));
    del(a); del(b); del(c); del(n1); del(n2); del(p); del(q); del(r1); del(r2);
}

TEST(gdi_regions_clip_equalrgn_invalid_handles) {
    READY(); NEED(UN(NtGdiEqualRgn));
    HRGN a = U(CreateRectRgn)(0, 0, 10, 10);
    CHECK(!UN(NtGdiEqualRgn)(NULL, a));
    CHECK(!UN(NtGdiEqualRgn)(a, NULL));
    CHECK(!UN(NtGdiEqualRgn)(NULL, NULL));
    CHECK(!UN(NtGdiEqualRgn)(a, (HRGN)(ULONG_PTR)0x40666));
    CHECK(!UN(NtGdiEqualRgn)((HRGN)U(GetStockObject)(BLACK_BRUSH), (HRGN)U(GetStockObject)(BLACK_BRUSH)));
    del(a);
}

/* ---- OffsetRgn ------------------------------------------------------------------------------------------- */

TEST(gdi_regions_clip_offsetrgn_complex_region_via_kernel) {
    READY(); NEED(UN(NtGdiOffsetRgn));
    HRGN a = kcreate(0, 0, 10, 10), b = kcreate(5, 5, 15, 15), u = kcreate(0, 0, 1, 1);
    UN(NtGdiCombineRgn)(u, a, b, RGN_OR);
    int r = UN(NtGdiOffsetRgn)(u, 100, -50);
    CHECK(r == RC_COMPLEX);
    RECT out[8]; int n = rgn_rects(u, out, 8);
    CHECK(n == 3);
    if (n == 3) { CHECK_BOX(out[0], 100, -50, 110, -45); CHECK_BOX(out[1], 100, -45, 115, -40); CHECK_BOX(out[2], 105, -40, 115, -35); }
    r = UN(NtGdiOffsetRgn)(u, -100, 50);
    CHECK(r == RC_COMPLEX);
    n = rgn_rects(u, out, 8);
    if (n == 3) CHECK_BOX(out[0], 0, 0, 10, 5);
    CHECK(UN(NtGdiOffsetRgn)(u, 0, 0) == RC_COMPLEX);
    del(a); del(b); del(u);
}

TEST(gdi_regions_clip_offsetrgn_simple_kernel_and_user_agree) {
    READY(); NEED(UN(NtGdiOffsetRgn));
    HRGN k = kcreate(1, 2, 11, 12), u = U(CreateRectRgn)(1, 2, 11, 12);
    CHECK(UN(NtGdiOffsetRgn)(k, -7, 3) == RC_SIMPLE);
    CHECK(U(OffsetRgn)(u, -7, 3) == RC_SIMPLE);
    CHECK(rgn_is_rect(k, -6, 5, 4, 15)); CHECK(rgn_is_rect(u, -6, 5, 4, 15));
    CHECK(UN(NtGdiEqualRgn)(k, u));
    del(k); del(u);
}

TEST(gdi_regions_clip_offsetrgn_invalid_handle_and_null_region) {
    READY(); NEED(UN(NtGdiOffsetRgn));
    CHECK(UN(NtGdiOffsetRgn)(NULL, 1, 1) == RC_ERROR);
    CHECK(UN(NtGdiOffsetRgn)((HRGN)(ULONG_PTR)0x40321, 1, 1) == RC_ERROR);
    CHECK(UN(NtGdiOffsetRgn)((HRGN)U(GetStockObject)(BLACK_BRUSH), 1, 1) == RC_ERROR);
    HRGN n = kcreate(2, 2, 2, 2);
    CHECK(UN(NtGdiOffsetRgn)(n, 5, 5) == RC_NULL);
    del(n);
}

/* ---- GetRegionData -------------------------------------------------------------------------------------------- */

TEST(gdi_regions_clip_getregiondata_header_of_rect) {
    READY();
    HRGN h = kcreate(3, 4, 13, 24);
    DWORD need = UN(NtGdiGetRegionData)(h, 0, NULL);
    CHECKF(need == 32 + 16, "need %lu", need);
    BYTE buf[256]; memset(buf, 0xcc, sizeof buf);
    DWORD got = UN(NtGdiGetRegionData)(h, sizeof buf, buf);
    CHECKF(got == need, "got %lu need %lu", got, need);
    RGNDATAHEADER *hd = (RGNDATAHEADER *)buf;
    CHECK(hd->dwSize == 32);
    CHECK(hd->iType == RDH_RECTANGLES);
    CHECK(hd->nCount == 1);
    CHECK(hd->nRgnSize == 16);
    CHECK_BOX(hd->rcBound, 3, 4, 13, 24);
    RECT *r = (RECT *)(buf + 32);
    CHECK_BOX(r[0], 3, 4, 13, 24);
    CHECK(buf[got] == 0xcc);   /* exactly `got` bytes were written */
    del(h);
}

TEST(gdi_regions_clip_getregiondata_user_wrapper_matches_syscall) {
    READY(); NEED(U(GetRegionData));
    HRGN a = kcreate(0, 0, 10, 10), b = kcreate(5, 5, 15, 15), u = kcreate(0, 0, 1, 1);
    UN(NtGdiCombineRgn)(u, a, b, RGN_XOR);
    DWORD n1 = U(GetRegionData)(u, 0, NULL), n2 = UN(NtGdiGetRegionData)(u, 0, NULL);
    CHECKF(n1 == n2 && n1 == 32 + 4 * 16, "user %lu kernel %lu", n1, n2);
    BYTE b1[512], b2[512]; memset(b1, 0, sizeof b1); memset(b2, 0, sizeof b2);
    CHECK(U(GetRegionData)(u, sizeof b1, (RGNDATA *)b1) == n1);
    CHECK(UN(NtGdiGetRegionData)(u, sizeof b2, b2) == n2);
    CHECK(!memcmp(b1, b2, n1));
    del(a); del(b); del(u);
}

TEST(gdi_regions_clip_getregiondata_buffer_protocol) {
    READY();
    HRGN a = kcreate(0, 0, 10, 10), b = kcreate(5, 5, 15, 15), u = kcreate(0, 0, 1, 1);
    UN(NtGdiCombineRgn)(u, a, b, RGN_OR);
    DWORD need = UN(NtGdiGetRegionData)(u, 0, NULL);
    CHECKF(need == 32 + 3 * 16, "need %lu", need);
    BYTE buf[512];
    /* a buffer shorter than the required size: GetRegionData fails with ERROR_INVALID_PARAMETER and 0 */
    for (DWORD cap = 0; cap < need; cap += (cap < 40 ? 7 : 1)) {
        memset(buf, 0xaa, sizeof buf);
        SET_ERR(0);
        DWORD r = UN(NtGdiGetRegionData)(u, cap, buf);
        CHECKF(r == 0, "cap %lu returned %lu", cap, r);
        if (r) break;
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
        CHECKF(buf[0] == 0xaa && buf[need - 1] == 0xaa, "cap %lu: buffer written on failure", cap);
    }
    memset(buf, 0xaa, sizeof buf);
    CHECK(UN(NtGdiGetRegionData)(u, need, buf) == need);
    CHECK(UN(NtGdiGetRegionData)(u, need + 1000 > sizeof buf ? (DWORD)sizeof buf : need + 1000, buf) == need);
    /* a size above 0x2710000 with a real buffer is refused before the buffer is touched */
    SET_ERR(0);
    CHECK(UN(NtGdiGetRegionData)(u, 0x2710001, buf) == 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    del(a); del(b); del(u);
}

TEST(gdi_regions_clip_getregiondata_invalid_handle) {
    READY();
    BYTE buf[128];
    SET_ERR(0);
    CHECK(UN(NtGdiGetRegionData)(NULL, sizeof buf, buf) == 0);
    EXPECT_ERR(ERROR_INVALID_HANDLE);   /* GetRegionData: invalid region -> 6 */
    SET_ERR(0);
    CHECK(UN(NtGdiGetRegionData)((HRGN)(ULONG_PTR)0x40888, 0, NULL) == 0);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    SET_ERR(0);
    CHECK(UN(NtGdiGetRegionData)((HRGN)U(GetStockObject)(BLACK_BRUSH), sizeof buf, buf) == 0);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
}

TEST(gdi_regions_clip_getregiondata_null_region_layout) {
    READY();
    HRGN h = kcreate(4, 4, 4, 9);
    DWORD need = UN(NtGdiGetRegionData)(h, 0, NULL);
    t_note("NULLREGION RGNDATA size %lu", need);
    BYTE buf[256]; memset(buf, 0xee, sizeof buf);
    DWORD got = UN(NtGdiGetRegionData)(h, sizeof buf, buf);
    CHECK(got == need);
    RGNDATAHEADER *hd = (RGNDATAHEADER *)buf;
    CHECK(hd->dwSize == 32 && hd->iType == RDH_RECTANGLES);
    CHECK(hd->nRgnSize == hd->nCount * 16);
    CHECK(got == hd->dwSize + hd->nRgnSize);
    if (hd->nCount == 0) CHECK_BOX(hd->rcBound, 0, 0, 0, 0);   /* nCount==0 -> bound zeroed */
    del(h);
}

TEST(gdi_regions_clip_getregiondata_rcbound_and_order_for_ellipse) {
    READY(); NEED(U(CreateEllipticRgn));
    HRGN h = U(CreateEllipticRgn)(0, 0, 30, 30);
    CHECK(h != NULL); if (!h) return;
    DWORD need = UN(NtGdiGetRegionData)(h, 0, NULL);
    BYTE *buf = (BYTE *)HeapAlloc(GetProcessHeap(), 0, need);
    CHECK(buf != NULL); if (!buf) { del(h); return; }
    CHECK(UN(NtGdiGetRegionData)(h, need, buf) == need);
    RGNDATAHEADER *hd = (RGNDATAHEADER *)buf;
    RECT bx; kbox(h, &bx);
    CHECK_BOX(hd->rcBound, bx.left, bx.top, bx.right, bx.bottom);
    RECT *r = (RECT *)(buf + 32);
    CHECK(hd->nCount > 8);
    BOOL ordered = TRUE, within = TRUE;
    for (DWORD i = 0; i < hd->nCount; i++) {
        if (r[i].left >= r[i].right || r[i].top >= r[i].bottom) ordered = FALSE;
        if (r[i].left < hd->rcBound.left || r[i].right > hd->rcBound.right || r[i].top < hd->rcBound.top || r[i].bottom > hd->rcBound.bottom) within = FALSE;
        if (i && (r[i].top < r[i - 1].top || (r[i].top == r[i - 1].top && r[i].left < r[i - 1].right))) ordered = FALSE;   /* bands top-to-bottom, rects left-to-right, no overlap */
    }
    CHECK(ordered); CHECK(within);
    /* round trip: ExtCreateRegion of the bytes rebuilds an equal region */
    if (U(ExtCreateRegion)) {
        HRGN back = U(ExtCreateRegion)(NULL, need, (RGNDATA *)buf);
        CHECK(back != NULL);
        if (back) {
            /* compare by sampling every pixel rather than by band structure */
            RECT b2; int c1 = kbox(h, &bx), c2 = kbox(back, &b2);
            CHECK(c1 == c2 && box_is(&b2, bx.left, bx.top, bx.right, bx.bottom));
            int diff = 0;
            for (int y = -1; y < 32; y++) for (int x = -1; x < 32; x++)
                if ((UN(NtGdiPtInRegion)(h, x, y) != 0) != (UN(NtGdiPtInRegion)(back, x, y) != 0)) diff++;
            CHECKF(diff == 0, "%d pixels differ after RGNDATA round trip", diff);
            del(back);
        }
    }
    HeapFree(GetProcessHeap(), 0, buf);
    del(h);
}

TEST(gdi_regions_clip_region_size_grows_with_rect_count) {
    READY();
    /* a staircase of isolated 1x1 rectangles never shares a band or a span: nCount == rectangles OR-ed in */
    HRGN acc = kcreate(0, 0, 0, 0), one = kcreate(0, 0, 1, 1);
    DWORD last = UN(NtGdiGetRegionData)(acc, 0, NULL);
    t_note("empty region RGNDATA size %lu", last);
    for (int i = 0; i < 512; i++) {
        UN(NtGdiSetRectRgn)(one, 2 * i, 2 * i, 2 * i + 1, 2 * i + 1);
        int r = UN(NtGdiCombineRgn)(acc, acc, one, RGN_OR);
        CHECKF(r == (i == 0 ? RC_SIMPLE : RC_COMPLEX), "step %d ret %d", i, r);
        int n = i + 1;
        if (n == 1 || n == 2 || n == 3 || n == 8 || n == 33 || n == 100 || n == 256 || n == 512) {
            DWORD sz = UN(NtGdiGetRegionData)(acc, 0, NULL);
            CHECKF(sz == 32 + 16 * (DWORD)n, "n=%d size %lu", n, sz);
            CHECKF(sz > last, "n=%d size did not grow", n);
            last = sz;
        }
    }
    RECT bx; CHECK(kbox(acc, &bx) == RC_COMPLEX); CHECK_BOX(bx, 0, 0, 1023, 1023);
    del(acc); del(one);
}

TEST(gdi_regions_clip_large_region_roundtrips_through_regiondata) {
    READY(); NEED(UN(NtGdiExtCreateRegion)); NEED(UN(NtGdiEqualRgn));
    enum { N = 1500 };
    HRGN acc = kcreate(0, 0, 0, 0), one = kcreate(0, 0, 1, 1);
    for (int i = 0; i < N; i++) {
        UN(NtGdiSetRectRgn)(one, 3 * i, 2 * i, 3 * i + 2, 2 * i + 1);
        UN(NtGdiCombineRgn)(acc, acc, one, RGN_OR);
    }
    DWORD sz = UN(NtGdiGetRegionData)(acc, 0, NULL);
    CHECKF(sz == 32 + 16 * N, "size %lu", sz);
    BYTE *buf = (BYTE *)HeapAlloc(GetProcessHeap(), 0, sz);
    CHECK(buf != NULL);
    if (buf) {
        CHECK(UN(NtGdiGetRegionData)(acc, sz, buf) == sz);
        HRGN back = UN(NtGdiExtCreateRegion)(NULL, sz, (RGNDATA *)buf);
        CHECK(back != NULL);
        if (back) { CHECK(UN(NtGdiEqualRgn)(acc, back)); del(back); }
        HeapFree(GetProcessHeap(), 0, buf);
    }
    del(acc); del(one);
}
