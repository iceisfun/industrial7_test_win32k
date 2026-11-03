/* Region construction: the userland path (gdi32 keeps rectangle regions in an RGN_ATTR block) and the
 * win32u syscalls directly, validated through the kernel (NtGdiGetRgnBox / NtGdiGetRegionData) and
 * through the GDI shared handle table cell. */
#include "rgn.h"

#define P(x) "gdi_regions_clip_" #x

/* Make sure user32/gdi32 are loaded and the GDI table exists before anything inspects process state. */
static int gdi_ready(void) {
    return U(CreateRectRgn) && U(DeleteObject) && U(GetRgnBox) && UN(NtGdiGetRgnBox) && UN(NtGdiGetRegionData);
}
#define READY() do { if (!gdi_ready()) { t_skip("gdi32/win32u exports missing"); return; } } while (0)

/* ---- CreateRectRgn: normalisation ----------------------------------------------------------------- */

struct rc4 { int l, t, r, b; int el, et, er, eb; int cplx; };
static const struct rc4 g_norm[] = {
    {  3,  4, 13, 24,   3,  4, 13, 24, RC_SIMPLE },
    { 13,  4,  3, 24,   3,  4, 13, 24, RC_SIMPLE },   /* x swapped: vOrder */
    {  3, 24, 13,  4,   3,  4, 13, 24, RC_SIMPLE },   /* y swapped */
    { 13, 24,  3,  4,   3,  4, 13, 24, RC_SIMPLE },   /* both */
    {  0,  0,  1,  1,   0,  0,  1,  1, RC_SIMPLE },
    { -5, -7,  5,  7,  -5, -7,  5,  7, RC_SIMPLE },   /* negative origin */
    { -9, -9, -3, -3,  -9, -9, -3, -3, RC_SIMPLE },
    {  0,  0, 100000, 100000, 0, 0, 100000, 100000, RC_SIMPLE },
    { COORD_MIN, COORD_MIN, COORD_MAX, COORD_MAX, COORD_MIN, COORD_MIN, COORD_MAX, COORD_MAX, RC_SIMPLE },
    { COORD_MAX, COORD_MAX, COORD_MIN, COORD_MIN, COORD_MIN, COORD_MIN, COORD_MAX, COORD_MAX, RC_SIMPLE },
};

TEST(gdi_regions_clip_createrect_normalises_via_userland) {
    READY();
    for (unsigned i = 0; i < sizeof g_norm / sizeof g_norm[0]; i++) {
        const struct rc4 *c = &g_norm[i];
        HRGN h = U(CreateRectRgn)(c->l, c->t, c->r, c->b);
        CHECKF(h != NULL, "row %u", i);
        if (!h) continue;
        RECT bx; int cx = kbox(h, &bx);
        CHECKF(cx == c->cplx, "row %u cplx %d", i, cx);
        CHECKF(box_is(&bx, c->el, c->et, c->er, c->eb), "row %u (%ld,%ld,%ld,%ld)", i, bx.left, bx.top, bx.right, bx.bottom);
        RECT ub; int cu = U(GetRgnBox)(h, &ub);   /* user-side read of the RGN_ATTR block */
        CHECKF(cu == c->cplx && box_is(&ub, c->el, c->et, c->er, c->eb), "row %u user GetRgnBox", i);
        U(DeleteObject)(h);
    }
}

TEST(gdi_regions_clip_createrect_normalises_via_syscall) {
    READY(); NEED(UN(NtGdiCreateRectRgn));
    for (unsigned i = 0; i < sizeof g_norm / sizeof g_norm[0]; i++) {
        const struct rc4 *c = &g_norm[i];
        /* The raw syscall returns a region handle for every in-range rectangle, corners in any order. The geometry is not
           readable through the kernel until the client block is filled in (see kcreate), so only the handle is checked. */
        HRGN h = UN(NtGdiCreateRectRgn)(c->l, c->t, c->r, c->b);
        CHECKF(h != NULL, "row %u", i);
        if (!h) continue;
        CHECKF(U(GetObjectType)(h) == OBJ_REGION, "row %u type", i);
        /* the client block is not initialised by the syscall (a recycled cell still carries the state of its previous region) */
        struct rcell rc;
        if (rgn_cell(h, &rc)) CHECKF(rc.user != NULL, "row %u client block", i);
        U(DeleteObject)(h);
    }
}

static const struct { int l, t, r, b; } g_empty[] = {
    { 5, 5, 5, 10 }, { 5, 5, 10, 5 }, { 7, 7, 7, 7 }, { 0, 0, 0, 0 }, { -3, -3, -3, 9 }, { 10, 10, 10, -10 },
};

TEST(gdi_regions_clip_createrect_empty_is_nullregion) {
    READY();
    for (unsigned i = 0; i < sizeof g_empty / sizeof g_empty[0]; i++) {
        HRGN h = U(CreateRectRgn)(g_empty[i].l, g_empty[i].t, g_empty[i].r, g_empty[i].b);
        CHECKF(h != NULL, "row %u: an empty rectangle still yields a region handle", i);
        if (!h) continue;
        RECT bx; int c = kbox(h, &bx);
        CHECKF(c == RC_NULL, "row %u kernel cplx %d", i, c);
        /* GetRgnBox zeroes the box of a NULLREGION. */
        CHECKF(box_is(&bx, 0, 0, 0, 0), "row %u box (%ld,%ld,%ld,%ld)", i, bx.left, bx.top, bx.right, bx.bottom);
        RECT ub; memset(&ub, 0x55, sizeof ub);
        int cu = U(GetRgnBox)(h, &ub);
        CHECKF(cu == RC_NULL && box_is(&ub, 0, 0, 0, 0), "row %u user GetRgnBox %d", i, cu);
        U(DeleteObject)(h);
    }
}

TEST(gdi_regions_clip_createrect_empty_via_syscall_is_nullregion) {
    READY(); NEED(UN(NtGdiCreateRectRgn));
    for (unsigned i = 0; i < sizeof g_empty / sizeof g_empty[0]; i++) {
        /* an empty rectangle still yields a region handle (the NULLREGION shape is decided when the client block is filled in) */
        HRGN h = UN(NtGdiCreateRectRgn)(g_empty[i].l, g_empty[i].t, g_empty[i].r, g_empty[i].b);
        CHECKF(h != NULL, "row %u", i);
        if (!h) continue;
        CHECKF(U(GetObjectType)(h) == OBJ_REGION, "row %u type", i);
        U(DeleteObject)(h);
        h = kcreate(g_empty[i].l, g_empty[i].t, g_empty[i].r, g_empty[i].b);
        RECT bx; int c = kbox(h, &bx);
        CHECKF(c == RC_NULL && box_is(&bx, 0, 0, 0, 0), "row %u cplx %d", i, c);
        U(DeleteObject)(h);
    }
}

/* ---- coordinate range ----------------------------------------------------------------------------- */

static const struct { int l, t, r, b; int ok; } g_range[] = {
    { COORD_MIN, 0, 10, 10, 1 },        { COORD_MIN - 1, 0, 10, 10, 0 },
    { 0, COORD_MIN, 10, 10, 1 },        { 0, COORD_MIN - 1, 10, 10, 0 },
    { 0, 0, COORD_MAX, 10, 1 },         { 0, 0, COORD_MAX + 1, 10, 0 },
    { 0, 0, 10, COORD_MAX, 1 },         { 0, 0, 10, COORD_MAX + 1, 0 },
    { 0x10000000, 0, 0x10000001, 10, 0 }, { 0, 0, 0x7fffffff, 10, 0 },
    { (int)0x80000000, 0, 10, 10, 0 },  { 0, (int)0x80000000, 10, 10, 0 },
    { 0, 0, 0x08000000, 0x08000000, 0 }, { -0x08000001, -0x08000001, 0, 0, 0 },
    { COORD_MAX, COORD_MAX, COORD_MAX, COORD_MAX, 1 },   /* empty but in range */
    { COORD_MIN, COORD_MIN, COORD_MIN, COORD_MIN, 1 },
};

TEST(gdi_regions_clip_createrect_range_limits_userland) {
    READY();
    for (unsigned i = 0; i < sizeof g_range / sizeof g_range[0]; i++) {
        SET_ERR(0);
        HRGN h = U(CreateRectRgn)(g_range[i].l, g_range[i].t, g_range[i].r, g_range[i].b);
        if (g_range[i].ok) { CHECKF(h != NULL, "row %u should succeed", i); }
        else {
            /* gdi32!CreateRectRgn rejects it before any syscall: last error 0x57 (ERROR_INVALID_PARAMETER). */
            CHECKF(h == NULL, "row %u should fail", i);
            EXPECT_ERR(ERROR_INVALID_PARAMETER);
        }
        if (h) U(DeleteObject)(h);
    }
}

TEST(gdi_regions_clip_createrect_range_limits_syscall) {
    READY(); NEED(UN(NtGdiCreateRectRgn));
    for (unsigned i = 0; i < sizeof g_range / sizeof g_range[0]; i++) {
        SET_ERR(0);
        HRGN h = UN(NtGdiCreateRectRgn)(g_range[i].l, g_range[i].t, g_range[i].r, g_range[i].b);
        if (g_range[i].ok) { CHECKF(h != NULL, "row %u should succeed", i); }
        else {
            /* NtGdiCreateRectRgn: failed range test -> EngSetLastError(0x57), NULL. */
            CHECKF(h == NULL, "row %u should fail", i);
            EXPECT_ERR(ERROR_INVALID_PARAMETER);
        }
        if (h) U(DeleteObject)(h);
    }
}

/* ---- handle encoding and GDI cell ------------------------------------------------------------------ */

TEST(gdi_regions_clip_createrect_handle_encoding) {
    READY(); NOTWINE("Windows GDI handle encoding");
    HRGN h = U(CreateRectRgn)(1, 2, 3, 4);
    CHECK(h != NULL); if (!h) return;
    /* object type 4 = region sits in bits 16..22 of the handle. */
    CHECKF((((ULONG_PTR)h >> 16) & 0x7f) == 4, "handle %p", h);
    CHECK(((ULONG_PTR)h & 0xffff) != 0);
    CHECKF(U(GetObjectType)(h) == OBJ_REGION, "type %lu", U(GetObjectType)(h));
    U(DeleteObject)(h);
}

TEST(gdi_regions_clip_createrect_cell_describes_region) {
    READY(); NOTWINE("Windows GDICELL layout");
    HRGN h = U(CreateRectRgn)(1, 2, 30, 40);
    CHECK(h != NULL); if (!h) return;
    struct rcell c;
    if (!rgn_cell(h, &c)) { t_skip("GdiSharedHandleTable not available"); U(DeleteObject)(h); return; }
    /* gdi32 (CombineRgn, GetRgnBox, OffsetRgn) accept a cell only if wType==4, wUpper==HIWORD(handle),
     * (owner & ~1)==gW32PID and pUser!=NULL. */
    CHECKF(c.type == 4, "type %u", c.type);
    CHECKF(c.upper == (unsigned)(((ULONG_PTR)h >> 16) & 0xffff), "upper 0x%x", c.upper);
    CHECKF((c.owner & ~1ul) == GetCurrentProcessId(), "owner 0x%lx pid %lu", c.owner, GetCurrentProcessId());
    CHECK(c.kern != NULL);
    /* NtGdiCreateRectRgn: the handle-table entry carries a user-mode attribute block (RGN_ATTR). */
    CHECK(c.user != NULL);
    U(DeleteObject)(h);
}

TEST(gdi_regions_clip_createrect_attr_block_tracks_rect) {
    READY(); NOTWINE("Windows RGN_ATTR layout");
    HRGN h = U(CreateRectRgn)(3, 4, 13, 24);
    CHECK(h != NULL); if (!h) return;
    struct rattr a;
    if (!rgn_attr(h, &a)) { t_skip("RGN_ATTR not readable"); U(DeleteObject)(h); return; }
    CHECKF(a.cplx == RC_SIMPLE, "cplx %lu", a.cplx);
    CHECK_BOX(a.r, 3, 4, 13, 24);
    U(DeleteObject)(h);
}

TEST(gdi_regions_clip_createrect_attr_block_swapped_and_empty) {
    READY(); NOTWINE("Windows RGN_ATTR layout");
    HRGN h = U(CreateRectRgn)(13, 24, 3, 4);
    HRGN e = U(CreateRectRgn)(5, 5, 5, 9);
    CHECK(h && e);
    struct rattr a;
    if (h && rgn_attr(h, &a)) { CHECKF(a.cplx == RC_SIMPLE, "cplx %lu", a.cplx); CHECK_BOX(a.r, 3, 4, 13, 24); }
    else t_skip("RGN_ATTR not readable");
    /* gdi32!CreateRectRgn stores complexity 1 (NULLREGION) with a zero rect for an empty rectangle. */
    if (e && rgn_attr(e, &a)) { CHECKF(a.cplx == RC_NULL, "cplx %lu", a.cplx); CHECK_BOX(a.r, 0, 0, 0, 0); }
    if (h) U(DeleteObject)(h);
    if (e) U(DeleteObject)(e);
}

TEST(gdi_regions_clip_syscall_created_rect_has_attr_block) {
    READY(); NEED(UN(NtGdiCreateRectRgn)); NOTWINE("Windows RGN_ATTR layout");
    HRGN h = UN(NtGdiCreateRectRgn)(2, 3, 12, 13);
    CHECK(h != NULL); if (!h) return;
    struct rcell c;
    if (rgn_cell(h, &c)) {
        CHECK(c.type == 4);
        CHECK(c.user != NULL);   /* attribute block stored by the syscall itself */
    } else t_skip("no GDI table");
    /* the block is allocated but not initialised by the syscall: gdi32's CreateRectRgn writes it afterwards (see kcreate) */
    U(DeleteObject)(h);
}

TEST(gdi_regions_clip_kernel_sees_userland_attr_state) {
    READY();
    /* gdi32!CreateRectRgn asks the kernel for a 0,0,1,1 region and then writes the real rectangle into the
     * RGN_ATTR block; every NtGdiXxx that opens the region (RGNOBJAPI) must observe the user-side rect. */
    HRGN h = U(CreateRectRgn)(10, 20, 110, 220);
    CHECK(h != NULL); if (!h) return;
    RECT bx; int cx = kbox(h, &bx);
    CHECK(cx == RC_SIMPLE); CHECK_BOX(bx, 10, 20, 110, 220);
    CHECK(UN(NtGdiPtInRegion)(h, 10, 20) != 0);
    CHECK(UN(NtGdiPtInRegion)(h, 109, 219) != 0);
    CHECK(UN(NtGdiPtInRegion)(h, 110, 220) == 0);
    DWORD sz = UN(NtGdiGetRegionData)(h, 0, NULL);
    CHECKF(sz >= 32 + 16, "size %lu", sz);
    U(DeleteObject)(h);
}

TEST(gdi_regions_clip_userland_offsetrgn_updates_attr_and_kernel) {
    READY(); NOTWINE("Windows RGN_ATTR layout");
    HRGN h = U(CreateRectRgn)(0, 0, 10, 10);
    CHECK(h != NULL); if (!h) return;
    int r = U(OffsetRgn)(h, 5, -3);
    CHECK(r == RC_SIMPLE);   /* gdi32!OffsetRgn returns 2 for a simple attr region */
    struct rattr a;
    if (rgn_attr(h, &a)) {
        CHECK_BOX(a.r, 5, -3, 15, 7);
        CHECKF((a.flags & 0x20) != 0, "flags 0x%lx", a.flags);   /* OffsetRgn: *attr |= 0x20 */
    } else t_skip("RGN_ATTR not readable");
    RECT bx; int cx = kbox(h, &bx);
    CHECK(cx == RC_SIMPLE); CHECK_BOX(bx, 5, -3, 15, 7);
    U(DeleteObject)(h);
}

TEST(gdi_regions_clip_userland_offsetrgn_overflow_fails_unchanged) {
    READY(); NOTWINE("Windows RGN_ATTR fast path");
    HRGN h = U(CreateRectRgn)(0, 0, 10, 10);
    CHECK(h != NULL); if (!h) return;
    /* the attr fast path range-checks the shifted rectangle and returns 0 without touching it */
    int r = U(OffsetRgn)(h, COORD_MAX, 0);
    CHECK(r == RC_ERROR);
    RECT bx; int cx = kbox(h, &bx);
    CHECK(cx == RC_SIMPLE); CHECK_BOX(bx, 0, 0, 10, 10);
    U(DeleteObject)(h);
}

TEST(gdi_regions_clip_userland_offsetrgn_null_region_unchanged) {
    READY();
    HRGN h = U(CreateRectRgn)(4, 4, 4, 4);
    CHECK(h != NULL); if (!h) return;
    int r = U(OffsetRgn)(h, 10, 10);
    CHECKF(r == RC_NULL, "offset of NULLREGION returned %d", r);
    RECT bx; CHECK(kbox(h, &bx) == RC_NULL);
    U(DeleteObject)(h);
}

TEST(gdi_regions_clip_regions_are_distinct_objects) {
    READY();
    HRGN a = U(CreateRectRgn)(0, 0, 4, 4), b = U(CreateRectRgn)(0, 0, 4, 4), c = U(CreateRectRgn)(0, 0, 4, 4);
    CHECK(a && b && c);
    CHECK(a != b && b != c && a != c);
    if (!t_is_wine()) {
        struct rcell ca, cb;
        if (rgn_cell(a, &ca) && rgn_cell(b, &cb)) {
            CHECK(ca.kern != cb.kern);
            CHECK(ca.user != cb.user);
        }
    }
    if (a) U(DeleteObject)(a);
    if (b) U(DeleteObject)(b);
    if (c) U(DeleteObject)(c);
}

/* ---- SetRectRgn ------------------------------------------------------------------------------------ */

TEST(gdi_regions_clip_setrect_replaces_region) {
    READY(); NEED(UN(NtGdiSetRectRgn));
    HRGN h = kcreate(0, 0, 3, 3);
    CHECK(h != NULL); if (!h) return;
    BOOL ok = UN(NtGdiSetRectRgn)(h, 20, 30, 40, 50);
    CHECK(ok);
    CHECK(rgn_is_rect(h, 20, 30, 40, 50));
    /* swapped corners are ordered before the region is set */
    CHECK(UN(NtGdiSetRectRgn)(h, 40, 50, 20, 30));
    CHECK(rgn_is_rect(h, 20, 30, 40, 50));
    /* an empty rectangle turns the region into a NULLREGION */
    CHECK(UN(NtGdiSetRectRgn)(h, 7, 7, 7, 20));
    RECT bx; CHECK(kbox(h, &bx) == RC_NULL);
    U(DeleteObject)(h);
}

TEST(gdi_regions_clip_setrect_userland_updates_kernel_view) {
    READY(); NEED(U(SetRectRgn));
    HRGN h = U(CreateRectRgn)(0, 0, 5, 5);
    CHECK(h != NULL); if (!h) return;
    CHECK(U(SetRectRgn)(h, 100, 200, 150, 260));
    CHECK(rgn_is_rect(h, 100, 200, 150, 260));
    CHECK(U(SetRectRgn)(h, 150, 260, 100, 200));
    CHECK(rgn_is_rect(h, 100, 200, 150, 260));
    U(DeleteObject)(h);
}

TEST(gdi_regions_clip_setrect_range_and_handle_failures) {
    READY(); NEED(UN(NtGdiSetRectRgn));
    HRGN h = kcreate(1, 1, 9, 9);
    CHECK(h != NULL); if (!h) return;
    static const int bad[][4] = { { COORD_MIN - 1, 0, 5, 5 }, { 0, COORD_MIN - 1, 5, 5 }, { 0, 0, COORD_MAX + 1, 5 },
                                  { 0, 0, 5, COORD_MAX + 1 }, { (int)0x80000000, 0, 5, 5 }, { 0, 0, 0x7fffffff, 0x7fffffff } };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        /* SetRectRgn returns 0 on a failed range test and leaves the region as it was. */
        CHECKF(!UN(NtGdiSetRectRgn)(h, bad[i][0], bad[i][1], bad[i][2], bad[i][3]), "row %u", i);
        CHECKF(rgn_is_rect(h, 1, 1, 9, 9), "row %u region changed", i);
    }
    CHECK(!UN(NtGdiSetRectRgn)(NULL, 0, 0, 1, 1));
    CHECK(!UN(NtGdiSetRectRgn)((HRGN)(ULONG_PTR)0x12345678, 0, 0, 1, 1));
    CHECK(!UN(NtGdiSetRectRgn)((HRGN)U(GetStockObject)(BLACK_BRUSH), 0, 0, 1, 1));   /* wrong object type */
    U(DeleteObject)(h);
}

/* ---- elliptic and round-rect regions ---------------------------------------------------------------- */

/* CreateEllipticRgn / CreateRoundRectRgn: the exact right/bottom extent (rect or rect-1) is a build detail
 * of the path flattener; assert the left/top origin and a 1-pixel tolerance, record the exact value. */
static int loose_box(const RECT *b, int l, int t, int r, int bt) {
    return b->left == l && b->top == t && (b->right == r || b->right == r - 1) && (b->bottom == bt || b->bottom == bt - 1);
}
static int count_in(HRGN h, int l, int t, int r, int b) {
    int n = 0;
    for (int y = t; y < b; y++) for (int x = l; x < r; x++) if (UN(NtGdiPtInRegion)(h, x, y)) n++;
    return n;
}

TEST(gdi_regions_clip_elliptic_box_and_shape) {
    READY(); NEED(U(CreateEllipticRgn));
    HRGN h = U(CreateEllipticRgn)(0, 0, 40, 20);
    CHECK(h != NULL); if (!h) return;
    RECT bx; int c = kbox(h, &bx);
    CHECKF(c == RC_COMPLEX, "cplx %d", c);   /* an ellipse needs many bands */
    CHECKF(loose_box(&bx, 0, 0, 40, 20), "box (%ld,%ld,%ld,%ld)", bx.left, bx.top, bx.right, bx.bottom);
    t_note("ellipse (0,0,40,20) box (%ld,%ld,%ld,%ld)", bx.left, bx.top, bx.right, bx.bottom);
    CHECK(UN(NtGdiPtInRegion)(h, 20, 10));
    CHECK(!UN(NtGdiPtInRegion)(h, 0, 0));
    CHECK(!UN(NtGdiPtInRegion)(h, 39, 0));
    CHECK(!UN(NtGdiPtInRegion)(h, 0, 19));
    CHECK(!UN(NtGdiPtInRegion)(h, 39, 19));
    CHECK(!UN(NtGdiPtInRegion)(h, 40, 10));
    CHECK(!UN(NtGdiPtInRegion)(h, -1, 10));
    int in = count_in(h, 0, 0, 40, 20);
    /* area of the ellipse is pi*20*10 ~ 628; the region spans (0,0)-(39,19), whose ellipse holds about 580 pixels, and 545 were counted */
    CHECKF(in > 520 && in < 580, "pixels inside %d", in);
    U(DeleteObject)(h);
}

TEST(gdi_regions_clip_elliptic_is_not_attr_backed_note) {
    READY(); NEED(U(CreateEllipticRgn)); NOTWINE("Windows GDICELL layout");
    HRGN h = U(CreateEllipticRgn)(0, 0, 8, 8);
    CHECK(h != NULL); if (!h) return;
    struct rcell c;
    if (rgn_cell(h, &c)) {
        CHECK(c.type == 4);
        CHECKF(c.upper == (unsigned)(((ULONG_PTR)h >> 16) & 0xffff), "upper");
        t_note("elliptic region cell pUser=%p (NtGdiCreateEllipticRgn does not allocate an attribute block)", c.user);
    }
    U(DeleteObject)(h);
}

TEST(gdi_regions_clip_elliptic_table) {
    READY(); NEED(U(CreateEllipticRgn));
    static const int t[][4] = { { 0, 0, 10, 10 }, { 5, 5, 105, 55 }, { -20, -10, 20, 10 }, { 0, 0, 1, 1 }, { 3, 3, 4, 20 }, { 0, 0, 2, 2 } };
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
        HRGN h = U(CreateEllipticRgn)(t[i][0], t[i][1], t[i][2], t[i][3]);
        CHECKF(h != NULL, "row %u", i);
        if (!h) continue;
        RECT bx; int c = kbox(h, &bx);
        t_note("row %u cplx %d box (%ld,%ld,%ld,%ld)", i, c, bx.left, bx.top, bx.right, bx.bottom);
        if (t[i][2] - t[i][0] >= 4 && t[i][3] - t[i][1] >= 4)
            CHECKF(loose_box(&bx, t[i][0], t[i][1], t[i][2], t[i][3]), "row %u box (%ld,%ld,%ld,%ld)", i, bx.left, bx.top, bx.right, bx.bottom);
        U(DeleteObject)(h);
    }
}

TEST(gdi_regions_clip_elliptic_empty_and_out_of_range) {
    READY(); NEED(U(CreateEllipticRgn)); NEED(UN(NtGdiCreateEllipticRgn));
    HRGN e = U(CreateEllipticRgn)(5, 5, 5, 20);
    t_note("empty-box CreateEllipticRgn returned %p", e);
    if (e) { RECT bx; int c = kbox(e, &bx); t_note("empty ellipse cplx %d", c); U(DeleteObject)(e); }
    static const int bad[][4] = { { COORD_MIN - 1, 0, 10, 10 }, { 0, 0, COORD_MAX + 1, 10 }, { 0, 0, 10, COORD_MAX + 1 },
                                  { 0, COORD_MIN - 1, 10, 10 }, { (int)0x80000000, 0, 10, 10 } };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SET_ERR(0);
        HRGN h = UN(NtGdiCreateEllipticRgn)(bad[i][0], bad[i][1], bad[i][2], bad[i][3]);
        CHECKF(h == NULL, "row %u", i);
        if (h) U(DeleteObject)(h);
        else t_note("row %u last error %lu", i, GetLastError());
    }
}

TEST(gdi_regions_clip_roundrect_box_corners_and_edges) {
    READY(); NEED(U(CreateRoundRectRgn));
    HRGN h = U(CreateRoundRectRgn)(0, 0, 40, 30, 12, 12);
    CHECK(h != NULL); if (!h) return;
    RECT bx; int c = kbox(h, &bx);
    CHECKF(c == RC_COMPLEX, "cplx %d", c);
    CHECKF(loose_box(&bx, 0, 0, 40, 30), "box (%ld,%ld,%ld,%ld)", bx.left, bx.top, bx.right, bx.bottom);
    CHECK(!UN(NtGdiPtInRegion)(h, 0, 0));        /* rounded corners cut the extreme pixels */
    CHECK(!UN(NtGdiPtInRegion)(h, 39, 0));
    CHECK(!UN(NtGdiPtInRegion)(h, 0, 29));
    CHECK(!UN(NtGdiPtInRegion)(h, 39, 29));
    CHECK(UN(NtGdiPtInRegion)(h, 20, 0));         /* straight top edge */
    CHECK(UN(NtGdiPtInRegion)(h, 0, 15));         /* straight left edge */
    CHECK(UN(NtGdiPtInRegion)(h, 20, 15));
    CHECK(UN(NtGdiPtInRegion)(h, 38, 15));
    CHECK(UN(NtGdiPtInRegion)(h, 20, 28));
    U(DeleteObject)(h);
}

TEST(gdi_regions_clip_roundrect_zero_radius_note) {
    READY(); NEED(U(CreateRoundRectRgn));
    HRGN h = U(CreateRoundRectRgn)(0, 0, 10, 10, 0, 0);
    CHECK(h != NULL); if (!h) return;
    RECT bx; int c = kbox(h, &bx);
    CHECK(loose_box(&bx, 0, 0, 10, 10));
    t_note("zero-radius round rect: cplx %d, corner (0,0) inside=%d", c, UN(NtGdiPtInRegion)(h, 0, 0));
    U(DeleteObject)(h);
}

TEST(gdi_regions_clip_roundrect_range_failures) {
    READY(); NEED(UN(NtGdiCreateRoundRectRgn));
    static const int bad[][4] = { { COORD_MIN - 1, 0, 10, 10 }, { 0, COORD_MIN - 1, 10, 10 }, { 0, 0, COORD_MAX + 1, 10 }, { 0, 0, 10, COORD_MAX + 1 } };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        HRGN h = UN(NtGdiCreateRoundRectRgn)(bad[i][0], bad[i][1], bad[i][2], bad[i][3], 4, 4);
        CHECKF(h == NULL, "row %u", i);
        if (h) U(DeleteObject)(h);
    }
    HRGN ok = UN(NtGdiCreateRoundRectRgn)(0, 0, 20, 20, 6, 6);
    CHECK(ok != NULL);
    if (ok) { RECT bx; kbox(ok, &bx); CHECK(loose_box(&bx, 0, 0, 20, 20)); U(DeleteObject)(ok); }
}

/* ---- ExtCreateRegion -------------------------------------------------------------------------------- */

struct rgnbuf { RGNDATAHEADER h; RECT r[8]; };
static void mk_rgndata(struct rgnbuf *b, const RECT *rc, int n) {
    memset(b, 0, sizeof *b);
    b->h.dwSize = sizeof(RGNDATAHEADER);   /* 32 */
    b->h.iType = RDH_RECTANGLES;
    b->h.nCount = n;
    b->h.nRgnSize = n * sizeof(RECT);
    if (n) { b->h.rcBound = rc[0]; memcpy(b->r, rc, n * sizeof(RECT)); }
    for (int i = 1; i < n; i++) UnionRect(&b->h.rcBound, &b->h.rcBound, &rc[i]);
}
#define RGNBUF_SIZE(n) (sizeof(RGNDATAHEADER) + (n) * sizeof(RECT))

TEST(gdi_regions_clip_extcreate_two_rects_roundtrip) {
    READY(); NEED(U(ExtCreateRegion));
    RECT rc[2] = { { 0, 0, 10, 5 }, { 3, 5, 20, 12 } };
    struct rgnbuf b; mk_rgndata(&b, rc, 2);
    HRGN h = U(ExtCreateRegion)(NULL, RGNBUF_SIZE(2), (RGNDATA *)&b);
    CHECK(h != NULL); if (!h) return;
    RECT bx; int c = kbox(h, &bx);
    CHECK(c == RC_COMPLEX); CHECK_BOX(bx, 0, 0, 20, 12);
    CHECK(UN(NtGdiPtInRegion)(h, 9, 4));
    CHECK(!UN(NtGdiPtInRegion)(h, 10, 4));
    CHECK(!UN(NtGdiPtInRegion)(h, 2, 5));
    CHECK(UN(NtGdiPtInRegion)(h, 3, 5));
    CHECK(UN(NtGdiPtInRegion)(h, 19, 11));
    RECT out[4]; int n = rgn_rects(h, out, 4);
    CHECKF(n == 2, "n=%d", n);
    if (n == 2) { CHECK_BOX(out[0], 0, 0, 10, 5); CHECK_BOX(out[1], 3, 5, 20, 12); }
    U(DeleteObject)(h);
}

TEST(gdi_regions_clip_extcreate_size_and_header_validation) {
    READY(); NEED(U(ExtCreateRegion)); NEED(UN(NtGdiExtCreateRegion));
    RECT rc[2] = { { 0, 0, 10, 5 }, { 3, 5, 20, 12 } };
    struct rgnbuf b; mk_rgndata(&b, rc, 2);
    /* gdi32!ExtCreateRegion: lpData==NULL or nCount < 0x20 -> last error 0x57, NULL (no syscall) */
    SET_ERR(0); CHECK(U(ExtCreateRegion)(NULL, RGNBUF_SIZE(2), NULL) == NULL); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SET_ERR(0); CHECK(U(ExtCreateRegion)(NULL, 0x1f, (RGNDATA *)&b) == NULL); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SET_ERR(0); CHECK(U(ExtCreateRegion)(NULL, 0, (RGNDATA *)&b) == NULL); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    /* ExtCreateRegion: dwSize must be exactly 0x20 */
    struct rgnbuf bad = b; bad.h.dwSize = 28;
    CHECK(UN(NtGdiExtCreateRegion)(NULL, RGNBUF_SIZE(2), (RGNDATA *)&bad) == NULL);
    bad = b; bad.h.dwSize = 36;
    CHECK(U(ExtCreateRegion)(NULL, RGNBUF_SIZE(2), (RGNDATA *)&bad) == NULL);
    bad = b; bad.h.dwSize = 0;
    CHECK(UN(NtGdiExtCreateRegion)(NULL, RGNBUF_SIZE(2), (RGNDATA *)&bad) == NULL);
    /* the supplied byte count must cover (nCount + 2) * 16 bytes */
    CHECK(UN(NtGdiExtCreateRegion)(NULL, RGNBUF_SIZE(2) - 1, (RGNDATA *)&b) == NULL);
    CHECK(UN(NtGdiExtCreateRegion)(NULL, RGNBUF_SIZE(2) - 16, (RGNDATA *)&b) == NULL);
    bad = b; bad.h.nCount = 3;                 /* header claims more rectangles than the byte count holds */
    CHECK(UN(NtGdiExtCreateRegion)(NULL, RGNBUF_SIZE(2), (RGNDATA *)&bad) == NULL);
    bad = b; bad.h.nCount = 0xffffffd + 1;
    CHECK(UN(NtGdiExtCreateRegion)(NULL, RGNBUF_SIZE(2), (RGNDATA *)&bad) == NULL);
    /* the 0x270ffe1 window on the byte count: sizes below 0x20 never reach the allocator */
    CHECK(UN(NtGdiExtCreateRegion)(NULL, 0x10, (RGNDATA *)&b) == NULL);
    CHECK(UN(NtGdiExtCreateRegion)(NULL, 0, (RGNDATA *)&b) == NULL);
}

TEST(gdi_regions_clip_extcreate_type_field_note) {
    READY(); NEED(U(ExtCreateRegion));
    RECT rc[2] = { { 0, 0, 10, 5 }, { 3, 5, 20, 12 } };
    struct rgnbuf b; mk_rgndata(&b, rc, 2);
    b.h.iType = 0;   /* ExtCreateRegion never reads iType; documented value is RDH_RECTANGLES (1) */
    HRGN h = U(ExtCreateRegion)(NULL, RGNBUF_SIZE(2), (RGNDATA *)&b);
    t_note("iType=0 accepted: %d", h != NULL);
    if (h) U(DeleteObject)(h);
}

TEST(gdi_regions_clip_extcreate_zero_rects) {
    READY(); NEED(U(ExtCreateRegion));
    struct rgnbuf b; mk_rgndata(&b, NULL, 0);
    SET_ERR(0);
    HRGN h = U(ExtCreateRegion)(NULL, sizeof(RGNDATAHEADER), (RGNDATA *)&b);
    t_note("nCount==0 region handle %p (err %lu)", h, GetLastError());
    if (h) { RECT bx; int c = kbox(h, &bx); CHECKF(c == RC_NULL, "cplx %d", c); U(DeleteObject)(h); }
}

TEST(gdi_regions_clip_extcreate_single_rect_matches_createrect) {
    READY(); NEED(U(ExtCreateRegion));
    RECT rc = { 11, 22, 33, 44 };
    struct rgnbuf b; mk_rgndata(&b, &rc, 1);
    /* nCount==1, no XFORM, >=0x30 bytes -> gdi32 forwards to CreateRectRgn(rect) */
    HRGN h = U(ExtCreateRegion)(NULL, RGNBUF_SIZE(1), (RGNDATA *)&b);
    CHECK(h != NULL); if (!h) return;
    CHECK(rgn_is_rect(h, 11, 22, 33, 44));
    U(DeleteObject)(h);
    /* the same buffer through the syscall directly */
    HRGN k = UN(NtGdiExtCreateRegion)(NULL, RGNBUF_SIZE(1), (RGNDATA *)&b);
    CHECK(k != NULL);
    if (k) { CHECK(rgn_is_rect(k, 11, 22, 33, 44)); U(DeleteObject)(k); }
}

TEST(gdi_regions_clip_extcreate_one_rect_short_buffer_fails) {
    READY(); NEED(U(ExtCreateRegion));
    RECT rc = { 1, 2, 3, 4 };
    struct rgnbuf b; mk_rgndata(&b, &rc, 1);
    /* nCount==1 without XFORM needs 0x30 bytes in gdi32!ExtCreateRegion; 0x2f falls through to 0x57 */
    SET_ERR(0);
    CHECK(U(ExtCreateRegion)(NULL, 0x2f, (RGNDATA *)&b) == NULL);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}

TEST(gdi_regions_clip_extcreate_identity_xform) {
    READY(); NEED(U(ExtCreateRegion));
    RECT rc[2] = { { 0, 0, 10, 5 }, { 3, 5, 20, 12 } };
    struct rgnbuf b; mk_rgndata(&b, rc, 2);
    XFORM x = { 1, 0, 0, 1, 0, 0 };
    HRGN h = U(ExtCreateRegion)(&x, RGNBUF_SIZE(2), (RGNDATA *)&b);
    CHECK(h != NULL); if (!h) return;
    HRGN ref = U(ExtCreateRegion)(NULL, RGNBUF_SIZE(2), (RGNDATA *)&b);
    CHECK(ref != NULL);
    if (ref) { CHECK(UN(NtGdiEqualRgn)(h, ref)); U(DeleteObject)(ref); }
    U(DeleteObject)(h);
}

TEST(gdi_regions_clip_extcreate_translate_xform) {
    READY(); NEED(U(ExtCreateRegion));
    RECT rc[2] = { { 0, 0, 10, 5 }, { 3, 5, 20, 12 } };
    struct rgnbuf b; mk_rgndata(&b, rc, 2);
    XFORM x = { 1, 0, 0, 1, 7, 9 };
    HRGN h = U(ExtCreateRegion)(&x, RGNBUF_SIZE(2), (RGNDATA *)&b);
    CHECK(h != NULL); if (!h) return;
    RECT bx; kbox(h, &bx);
    CHECKF(box_is(&bx, 7, 9, 27, 21), "translated box (%ld,%ld,%ld,%ld)", bx.left, bx.top, bx.right, bx.bottom);
    U(DeleteObject)(h);
}

TEST(gdi_regions_clip_extcreate_scale_xform_note) {
    READY(); NEED(U(ExtCreateRegion));
    RECT rc[2] = { { 0, 0, 10, 5 }, { 3, 5, 20, 12 } };
    struct rgnbuf b; mk_rgndata(&b, rc, 2);
    XFORM x = { 2, 0, 0, 3, 1, 1 };
    HRGN h = U(ExtCreateRegion)(&x, RGNBUF_SIZE(2), (RGNDATA *)&b);
    CHECK(h != NULL); if (!h) return;
    RECT bx; kbox(h, &bx);
    t_note("scaled box (%ld,%ld,%ld,%ld), expect (1,1,41,37) up to path rounding", bx.left, bx.top, bx.right, bx.bottom);
    CHECK(bx.left <= 2 && bx.top <= 2 && bx.right >= 40 && bx.bottom >= 36);
    U(DeleteObject)(h);
}

TEST(gdi_regions_clip_extcreate_range_overflow_rejected) {
    READY(); NEED(UN(NtGdiExtCreateRegion));
    RECT rc[2] = { { 0, 0, 10, 5 }, { 3, 5, COORD_MAX + 1, 12 } };
    struct rgnbuf b; mk_rgndata(&b, rc, 2);
    b.h.rcBound.right = COORD_MAX + 1;
    /* a rectangle past the coordinate range does not make the call fail (see below) */
    HRGN h = UN(NtGdiExtCreateRegion)(NULL, RGNBUF_SIZE(2), (RGNDATA *)&b);
    RECT bx; int c = h ? kbox(h, &bx) : -1;
    if (h) t_note("rectangle past the coordinate range accepted: cplx %d box (%ld,%ld,%ld,%ld)", c, bx.left, bx.top, bx.right, bx.bottom);
    /* Windows 10 accepts the data but drops the rectangle that lies past the coordinate range: only the first one remains */
    CHECK(h != NULL && c == RC_SIMPLE && box_is(&bx, 0, 0, 10, 5));
    if (h) U(DeleteObject)(h);
}

/* ---- polygon regions --------------------------------------------------------------------------------- */

TEST(gdi_regions_clip_polypolygon_fill_modes_overlap) {
    READY(); NEED(U(CreatePolyPolygonRgn));
    /* two overlapping squares wound the same way: even-odd leaves a hole in the overlap, nonzero fills it */
    POINT pt[8] = { { 0, 0 }, { 10, 0 }, { 10, 10 }, { 0, 10 }, { 5, 5 }, { 15, 5 }, { 15, 15 }, { 5, 15 } };
    INT cnt[2] = { 4, 4 };
    HRGN alt = U(CreatePolyPolygonRgn)(pt, cnt, 2, ALTERNATE);
    HRGN wnd = U(CreatePolyPolygonRgn)(pt, cnt, 2, WINDING);
    CHECK(alt != NULL); CHECK(wnd != NULL);
    if (alt && wnd) {
        RECT ba, bw; kbox(alt, &ba); kbox(wnd, &bw);
        CHECK_BOX(ba, 0, 0, 15, 15); CHECK_BOX(bw, 0, 0, 15, 15);
        CHECK(UN(NtGdiPtInRegion)(alt, 2, 2));   CHECK(UN(NtGdiPtInRegion)(wnd, 2, 2));
        CHECK(!UN(NtGdiPtInRegion)(alt, 7, 7));  CHECK(UN(NtGdiPtInRegion)(wnd, 7, 7));
        CHECK(UN(NtGdiPtInRegion)(alt, 12, 12)); CHECK(UN(NtGdiPtInRegion)(wnd, 12, 12));
        CHECK(!UN(NtGdiPtInRegion)(alt, 12, 2)); CHECK(!UN(NtGdiPtInRegion)(wnd, 12, 2));
        CHECK(!UN(NtGdiPtInRegion)(alt, 2, 12)); CHECK(!UN(NtGdiPtInRegion)(wnd, 2, 12));
        CHECK(!UN(NtGdiEqualRgn)(alt, wnd));
        RECT ra[8]; int n = rgn_rects(alt, ra, 8);
        /* even-odd rows: [0,5) x[0,10); [5,10) x[0,5)+[10,15); [10,15) x[5,15) */
        CHECKF(n == 4, "alternate rect count %d", n);
        if (n == 4) { CHECK_BOX(ra[0], 0, 0, 10, 5); CHECK_BOX(ra[1], 0, 5, 5, 10); CHECK_BOX(ra[2], 10, 5, 15, 10); CHECK_BOX(ra[3], 5, 10, 15, 15); }
        RECT rw[8]; n = rgn_rects(wnd, rw, 8);
        CHECKF(n == 3, "winding rect count %d", n);
        if (n == 3) { CHECK_BOX(rw[0], 0, 0, 10, 5); CHECK_BOX(rw[1], 0, 5, 15, 10); CHECK_BOX(rw[2], 5, 10, 15, 15); }
    }
    if (alt) U(DeleteObject)(alt);
    if (wnd) U(DeleteObject)(wnd);
}

TEST(gdi_regions_clip_polygon_rect_covers_exact_box) {
    READY(); NEED(U(CreatePolygonRgn));
    POINT sq[4] = { { 2, 3 }, { 12, 3 }, { 12, 9 }, { 2, 9 } };
    HRGN h = U(CreatePolygonRgn)(sq, 4, ALTERNATE);
    CHECK(h != NULL); if (!h) return;
    RECT bx; int c = kbox(h, &bx);
    CHECK(c == RC_SIMPLE); CHECK_BOX(bx, 2, 3, 12, 9);   /* right/bottom edge excluded */
    U(DeleteObject)(h);
}

TEST(gdi_regions_clip_polygon_triangle_shape) {
    READY(); NEED(U(CreatePolygonRgn));
    POINT tri[3] = { { 0, 0 }, { 20, 0 }, { 0, 20 } };
    HRGN h = U(CreatePolygonRgn)(tri, 3, WINDING);
    CHECK(h != NULL); if (!h) return;
    RECT bx; int c = kbox(h, &bx);
    CHECK(c == RC_COMPLEX);
    CHECK(bx.left == 0 && bx.top == 0 && bx.right <= 20 && bx.bottom <= 20 && bx.right >= 19 && bx.bottom >= 19);
    CHECK(UN(NtGdiPtInRegion)(h, 2, 2));
    CHECK(!UN(NtGdiPtInRegion)(h, 18, 18));
    CHECK(!UN(NtGdiPtInRegion)(h, 19, 19));
    int in = count_in(h, 0, 0, 20, 20);
    CHECKF(in > 170 && in < 230, "pixels %d", in);   /* area 200 */
    U(DeleteObject)(h);
}

TEST(gdi_regions_clip_polygon_invalid_arguments_note) {
    READY(); NEED(U(CreatePolygonRgn)); NEED(U(CreatePolyPolygonRgn));
    POINT sq[4] = { { 0, 0 }, { 5, 0 }, { 5, 5 }, { 0, 5 } };
    INT cnt[1] = { 4 };
    SET_ERR(0); HRGN h = U(CreatePolygonRgn)(sq, 4, 0);
    t_note("fill mode 0: %p err %lu", h, GetLastError()); if (h) U(DeleteObject)(h);
    SET_ERR(0); h = U(CreatePolygonRgn)(sq, 4, 3);
    t_note("fill mode 3: %p err %lu", h, GetLastError()); if (h) U(DeleteObject)(h);
    SET_ERR(0); h = U(CreatePolyPolygonRgn)(sq, cnt, 0, ALTERNATE);
    t_note("zero polygons: %p err %lu", h, GetLastError()); if (h) U(DeleteObject)(h);
}
