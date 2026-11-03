/* Device-context clipping: ExtSelectClipRgn / IntersectClipRect / ExcludeClipRect / OffsetClipRgn /
 * SetMetaRgn / GetRandomRgn / GetClipBox, validated by reading the clip regions back out of the kernel
 * (NtGdiGetRandomRgn) and by the pixels the kernel wrote into a DIB section. */
#include "rgn.h"

#define READY() do { if (!U(CreateRectRgn) || !U(DeleteObject) || !U(SelectClipRgn) || !U(ExtSelectClipRgn) || !U(IntersectClipRect) || !U(GetClipBox) || !UN(NtGdiGetRandomRgn) || !UN(NtGdiGetRgnBox) || !UN(NtGdiGetRegionData) || !U(PatBlt) || !U(GdiFlush)) { t_skip("exports missing"); return; } } while (0)

static void del(HRGN h) { if (h) U(DeleteObject)(h); }

/* Reads one of the DC's regions through the kernel. Returns NtGdiGetRandomRgn's result (1 copied, 0 none,
 * -1 failure) and fills bx and cplx with the region (zeroed when none). */
static int getrr(HDC dc, int code, RECT *bx, int *cplx) {
    HRGN t = U(CreateRectRgn)(-7, -7, -3, -3);   /* sentinel contents prove whether the kernel overwrote it */
    memset(bx, 0, sizeof *bx); if (cplx) *cplx = -1;
    int r = UN(NtGdiGetRandomRgn)(dc, t, code);
    if (r == 1) { int c = kbox(t, bx); if (cplx) *cplx = c; }
    del(t);
    return r;
}
/* number of rectangles in a DC region, -1 when absent */
static int getrr_rects(HDC dc, int code, RECT *out, int max) {
    HRGN t = U(CreateRectRgn)(0, 0, 1, 1);
    int n = -1;
    if (UN(NtGdiGetRandomRgn)(dc, t, code) == 1) n = rgn_rects(t, out, max);
    del(t);
    return n;
}
static int clip_is_rect(HDC dc, int l, int t, int r, int b) {
    RECT bx; int c; return getrr(dc, GRR_CLIP, &bx, &c) == 1 && c == RC_SIMPLE && box_is(&bx, l, t, r, b);
}

/* ---- GetRandomRgn ------------------------------------------------------------------------------------------ */

TEST(gdi_regions_clip_getrandomrgn_fresh_dc_has_no_clip_meta_api) {
    READY(); FX(f, 64, 64);
    RECT bx; int c;
    /* DC+0xa0 (clip) and DC+0xa8 (meta) are NULL on a new DC: the dispatch ends at "no region" == 0 */
    CHECK(getrr(f.dc, GRR_CLIP, &bx, &c) == 0);
    CHECK(getrr(f.dc, GRR_META, &bx, &c) == 0);
    CHECK(getrr(f.dc, GRR_API, &bx, &c) == 0);
    fx_close(&f);
}

TEST(gdi_regions_clip_getrandomrgn_sysrgn_of_memory_dc_is_bitmap_rect) {
    READY(); FX(f, 64, 48);
    RECT bx; int c;
    int r = getrr(f.dc, GRR_SYS, &bx, &c);
    CHECK(r == 1);
    CHECK(c == RC_SIMPLE); CHECK_BOX(bx, 0, 0, 64, 48);   /* prgnVis of a memory DC is the selected bitmap */
    fx_close(&f);
}

TEST(gdi_regions_clip_getrandomrgn_sysrgn_follows_bitmap_size) {
    READY();
    static const int sz[][2] = { { 1, 1 }, { 17, 5 }, { 100, 3 }, { 8, 90 } };
    for (unsigned i = 0; i < sizeof sz / sizeof sz[0]; i++) {
        struct fx f;
        if (!fx_open(&f, sz[i][0], sz[i][1])) { t_skip("no DIB"); return; }
        RECT bx; int c;
        CHECKF(getrr(f.dc, GRR_SYS, &bx, &c) == 1, "row %u", i);
        CHECKF(box_is(&bx, 0, 0, sz[i][0], sz[i][1]), "row %u (%ld,%ld,%ld,%ld)", i, bx.left, bx.top, bx.right, bx.bottom);
        fx_close(&f);
    }
}

TEST(gdi_regions_clip_getrandomrgn_bad_code_and_arguments) {
    READY(); FX(f, 32, 32);
    HRGN t = U(CreateRectRgn)(0, 0, 1, 1);
    /* code 5 returns 0 before the DC is even locked; codes outside 1..5 fall to the "no region" arm */
    CHECK(UN(NtGdiGetRandomRgn)(f.dc, t, 5) == 0);
    CHECK(UN(NtGdiGetRandomRgn)(NULL, t, 5) == 0);
    CHECK(UN(NtGdiGetRandomRgn)(f.dc, t, 0) == 0);
    CHECK(UN(NtGdiGetRandomRgn)(f.dc, t, 6) == 0);
    CHECK(UN(NtGdiGetRandomRgn)(f.dc, t, 100) == 0);
    CHECK(UN(NtGdiGetRandomRgn)(f.dc, t, -1) == 0);
    CHECK(rgn_is_rect(t, 0, 0, 1, 1));   /* t never touched */
    /* invalid DC: EngSetLastError(6) and the -1 initial result */
    SET_ERR(0);
    CHECK(UN(NtGdiGetRandomRgn)(NULL, t, GRR_SYS) == -1);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    SET_ERR(0);
    CHECK(UN(NtGdiGetRandomRgn)((HDC)(ULONG_PTR)0x00010123, t, GRR_CLIP) == -1);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    SET_ERR(0);
    CHECK(UN(NtGdiGetRandomRgn)((HDC)(ULONG_PTR)t, t, GRR_SYS) == -1);   /* a region handle is not a DC */
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    /* valid source region, invalid destination region: RGNOBJAPI null -> -1 */
    CHECK(UN(NtGdiGetRandomRgn)(f.dc, NULL, GRR_SYS) == -1);
    CHECK(UN(NtGdiGetRandomRgn)(f.dc, (HRGN)(ULONG_PTR)0x40999, GRR_SYS) == -1);
    CHECK(UN(NtGdiGetRandomRgn)(f.dc, (HRGN)U(GetStockObject)(BLACK_BRUSH), GRR_SYS) == -1);
    del(t);
    fx_close(&f);
}

TEST(gdi_regions_clip_getrandomrgn_overwrites_destination) {
    READY(); FX(f, 64, 64);
    HRGN clip = U(CreateRectRgn)(10, 10, 30, 30);
    CHECK(U(SelectClipRgn)(f.dc, clip) != RC_ERROR);
    HRGN dst = U(CreateRectRgn)(500, 500, 600, 600);
    CHECK(UN(NtGdiGetRandomRgn)(f.dc, dst, GRR_CLIP) == 1);
    CHECK(rgn_is_rect(dst, 10, 10, 30, 30));
    /* the destination is a private copy: changing it leaves the DC alone */
    UN(NtGdiSetRectRgn)(dst, 0, 0, 3, 3);
    CHECK(clip_is_rect(f.dc, 10, 10, 30, 30));
    del(dst); del(clip);
    fx_close(&f);
}

/* ---- SelectClipRgn / ExtSelectClipRgn ------------------------------------------------------------------------ */

TEST(gdi_regions_clip_selectclip_rect_roundtrip) {
    READY(); FX(f, 64, 64);
    HRGN r = U(CreateRectRgn)(10, 12, 30, 40);
    int ret = U(SelectClipRgn)(f.dc, r);
    CHECKF(ret == RC_SIMPLE, "SelectClipRgn returned %d", ret);   /* complexity of the effective region */
    CHECK(clip_is_rect(f.dc, 10, 12, 30, 40));
    RECT bx; int c = U(GetClipBox)(f.dc, &bx);
    CHECK(c == RC_SIMPLE); CHECK_BOX(bx, 10, 12, 30, 40);
    del(r);
    fx_close(&f);
}

TEST(gdi_regions_clip_selectclip_region_is_copied) {
    READY(); FX(f, 64, 64);
    HRGN r = kcreate(10, 10, 30, 30);
    CHECK(UN(NtGdiExtSelectClipRgn)(f.dc, r, RGN_COPY) == RC_SIMPLE);
    /* edit, then delete, the caller's region: DC::iSelect copied it into the DC */
    UN(NtGdiSetRectRgn)(r, 0, 0, 5, 5);
    CHECK(clip_is_rect(f.dc, 10, 10, 30, 30));
    del(r);
    CHECK(clip_is_rect(f.dc, 10, 10, 30, 30));
    fx_close(&f);
}

TEST(gdi_regions_clip_selectclip_clipped_pixels_after_region_deleted) {
    READY(); FX(f, 64, 64);
    fx_white(&f);
    HRGN r = U(CreateRectRgn)(10, 10, 30, 30);
    U(SelectClipRgn)(f.dc, r);
    del(r);
    int n = fx_blacken(&f);
    CHECKF(n == 400, "black pixels %d", n);
    CHECK(fx_px_black(&f, 10, 10)); CHECK(fx_px_black(&f, 29, 29));
    CHECK(!fx_px_black(&f, 9, 10)); CHECK(!fx_px_black(&f, 10, 9)); CHECK(!fx_px_black(&f, 30, 29)); CHECK(!fx_px_black(&f, 29, 30));
    fx_close(&f);
}

TEST(gdi_regions_clip_selectclip_null_region_copy_resets) {
    READY(); FX(f, 64, 64);
    HRGN r = U(CreateRectRgn)(10, 10, 30, 30);
    U(SelectClipRgn)(f.dc, r);
    RECT bx; int c;
    CHECK(getrr(f.dc, GRR_CLIP, &bx, &c) == 1);
    int ret = U(SelectClipRgn)(f.dc, NULL);
    t_note("SelectClipRgn(NULL) returned %d", ret);
    CHECK(ret != RC_ERROR);
    CHECK(getrr(f.dc, GRR_CLIP, &bx, &c) == 0);
    c = U(GetClipBox)(f.dc, &bx);
    CHECK(c == RC_SIMPLE); CHECK_BOX(bx, 0, 0, 64, 64);
    fx_white(&f);
    CHECK(fx_blacken(&f) == 64 * 64);
    del(r);
    fx_close(&f);
}

TEST(gdi_regions_clip_extselect_null_region_mode_semantics) {
    READY(); FX(f, 64, 64);
    HRGN r = U(CreateRectRgn)(10, 10, 30, 30);
    CHECK(UN(NtGdiExtSelectClipRgn)(f.dc, r, RGN_COPY) == RC_SIMPLE);
    /* NULL region: only RGN_COPY acts (it drops the clip and reports the visible region's complexity);
     * every other mode returns 0 and leaves the clip alone */
    for (int m = RGN_AND; m <= RGN_DIFF; m++) {
        CHECKF(UN(NtGdiExtSelectClipRgn)(f.dc, NULL, m) == 0, "mode %d", m);
        CHECKF(clip_is_rect(f.dc, 10, 10, 30, 30), "mode %d changed the clip", m);
    }
    CHECK(UN(NtGdiExtSelectClipRgn)(f.dc, NULL, RGN_COPY) == RC_SIMPLE);
    RECT bx; int c;
    CHECK(getrr(f.dc, GRR_CLIP, &bx, &c) == 0);
    del(r);
    fx_close(&f);
}

struct selcase { int mode; int cplx; int n; RECT r[4]; };
/* current clip A=(0,0,20,20), operand B=(10,10,30,30), device is 64x64 */
static const struct selcase g_sel[] = {
    { RGN_AND,  RC_SIMPLE,  1, { { 10, 10, 20, 20 } } },
    { RGN_OR,   RC_COMPLEX, 3, { { 0, 0, 20, 10 }, { 0, 10, 30, 20 }, { 10, 20, 30, 30 } } },
    { RGN_XOR,  RC_COMPLEX, 4, { { 0, 0, 20, 10 }, { 0, 10, 10, 20 }, { 20, 10, 30, 20 }, { 10, 20, 30, 30 } } },
    { RGN_DIFF, RC_COMPLEX, 2, { { 0, 0, 20, 10 }, { 0, 10, 10, 20 } } },
    { RGN_COPY, RC_SIMPLE,  1, { { 10, 10, 30, 30 } } },
};

TEST(gdi_regions_clip_extselect_combine_modes) {
    READY(); FX(f, 64, 64);
    for (unsigned i = 0; i < sizeof g_sel / sizeof g_sel[0]; i++) {
        const struct selcase *s = &g_sel[i];
        HRGN a = kcreate(0, 0, 20, 20), b = kcreate(10, 10, 30, 30);
        CHECK(UN(NtGdiExtSelectClipRgn)(f.dc, a, RGN_COPY) == RC_SIMPLE);
        int ret = UN(NtGdiExtSelectClipRgn)(f.dc, b, s->mode);
        CHECKF(ret == s->cplx, "mode %d ret %d want %d", s->mode, ret, s->cplx);
        RECT out[8]; int n = getrr_rects(f.dc, GRR_CLIP, out, 8);
        CHECKF(n == s->n, "mode %d rect count %d want %d", s->mode, n, s->n);
        if (n == s->n) for (int k = 0; k < n; k++)
            CHECKF(box_is(&out[k], s->r[k].left, s->r[k].top, s->r[k].right, s->r[k].bottom), "mode %d rect %d (%ld,%ld,%ld,%ld)", s->mode, k, out[k].left, out[k].top, out[k].right, out[k].bottom);
        del(a); del(b);
    }
    fx_close(&f);
}

TEST(gdi_regions_clip_extselect_combine_modes_userland) {
    READY(); FX(f, 64, 64);
    for (unsigned i = 0; i < sizeof g_sel / sizeof g_sel[0]; i++) {
        const struct selcase *s = &g_sel[i];
        HRGN a = U(CreateRectRgn)(0, 0, 20, 20), b = U(CreateRectRgn)(10, 10, 30, 30);
        U(SelectClipRgn)(f.dc, a);
        int ret = U(ExtSelectClipRgn)(f.dc, b, s->mode);
        CHECKF(ret == s->cplx, "mode %d ret %d want %d", s->mode, ret, s->cplx);
        RECT out[8]; int n = getrr_rects(f.dc, GRR_CLIP, out, 8);
        CHECKF(n == s->n, "mode %d rect count %d", s->mode, n);
        del(a); del(b);
    }
    fx_close(&f);
}

TEST(gdi_regions_clip_extselect_modes_clip_pixels) {
    READY(); FX(f, 64, 64);
    /* black pixel counts of the five modes (A=20x20 at 0,0; B=20x20 at 10,10; overlap 10x10) */
    static const int want[] = { 100, 700, 600, 300, 400 };   /* AND OR XOR DIFF COPY */
    for (unsigned i = 0; i < sizeof g_sel / sizeof g_sel[0]; i++) {
        HRGN a = kcreate(0, 0, 20, 20), b = kcreate(10, 10, 30, 30);
        UN(NtGdiExtSelectClipRgn)(f.dc, a, RGN_COPY);
        UN(NtGdiExtSelectClipRgn)(f.dc, b, g_sel[i].mode);
        fx_white(&f);
        int n = fx_blacken(&f);
        CHECKF(n == want[i], "mode %d black %d want %d", g_sel[i].mode, n, want[i]);
        del(a); del(b);
    }
    fx_close(&f);
}

TEST(gdi_regions_clip_extselect_invalid_arguments) {
    READY(); FX(f, 64, 64);
    HRGN r = kcreate(10, 10, 30, 30);
    static const int bad[] = { 0, 6, 7, -1, 100, 0x7fffffff };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SET_ERR(0);
        CHECKF(UN(NtGdiExtSelectClipRgn)(f.dc, r, bad[i]) == 0, "mode %d", bad[i]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);   /* (mode - 1) > 4 -> 0x57 before the DC is touched */
        CHECK(getrr(f.dc, GRR_CLIP, &(RECT){0}, &(int){0}) == 0);
    }
    SET_ERR(0);
    CHECK(UN(NtGdiExtSelectClipRgn)(NULL, r, RGN_COPY) == 0);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    SET_ERR(0);
    CHECK(UN(NtGdiExtSelectClipRgn)((HDC)(ULONG_PTR)0x00010777, r, RGN_COPY) == 0);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    /* a bad region handle on a good DC yields 0 and leaves no clip */
    CHECK(UN(NtGdiExtSelectClipRgn)(f.dc, (HRGN)(ULONG_PTR)0x40ABC, RGN_COPY) == 0);
    CHECK(UN(NtGdiExtSelectClipRgn)(f.dc, (HRGN)U(GetStockObject)(BLACK_BRUSH), RGN_COPY) == 0);
    RECT bx; int c;
    CHECK(getrr(f.dc, GRR_CLIP, &bx, &c) == 0);
    del(r);
    fx_close(&f);
}

TEST(gdi_regions_clip_selectclip_region_in_device_units_not_logical) {
    READY(); NEED(U(SetMapMode)); NEED(U(SetWindowExtEx)); NEED(U(SetViewportExtEx)); FX(f, 64, 64);
    U(SetMapMode)(f.dc, MM_ANISOTROPIC);
    U(SetWindowExtEx)(f.dc, 1, 1, NULL);
    U(SetViewportExtEx)(f.dc, 2, 2, NULL);
    HRGN r = U(CreateRectRgn)(0, 0, 10, 10);
    U(SelectClipRgn)(f.dc, r);
    /* the clip region is stored as given (device units); GetClipBox maps it back to logical units */
    CHECK(clip_is_rect(f.dc, 0, 0, 10, 10));
    RECT bx; U(GetClipBox)(f.dc, &bx);
    CHECK_BOX(bx, 0, 0, 5, 5);
    del(r);
    fx_close(&f);
}

/* ---- IntersectClipRect / ExcludeClipRect ----------------------------------------------------------------------- */

TEST(gdi_regions_clip_intersect_rect_builds_and_narrows_clip) {
    READY(); FX(f, 64, 64);
    int r1 = U(IntersectClipRect)(f.dc, 10, 10, 40, 40);
    t_note("IntersectClipRect first returned %d", r1);
    CHECK(r1 != RC_ERROR && r1 != RC_NULL);
    CHECK(clip_is_rect(f.dc, 10, 10, 40, 40));
    int r2 = U(IntersectClipRect)(f.dc, 20, 5, 60, 30);
    CHECK(r2 != RC_ERROR && r2 != RC_NULL);
    CHECK(clip_is_rect(f.dc, 20, 10, 40, 30));
    RECT bx; U(GetClipBox)(f.dc, &bx); CHECK_BOX(bx, 20, 10, 40, 30);
    fx_white(&f);
    CHECK(fx_blacken(&f) == 20 * 20);
    fx_close(&f);
}

TEST(gdi_regions_clip_intersect_disjoint_yields_nullregion) {
    READY(); FX(f, 64, 64);
    U(IntersectClipRect)(f.dc, 0, 0, 10, 10);
    int r = U(IntersectClipRect)(f.dc, 20, 20, 30, 30);
    CHECKF(r == RC_NULL || r == RC_COMPLEX, "returned %d", r);   /* the call may report COMPLEXREGION for the empty result; the clip itself is NULL */
    RECT bx; int c;
    int g = getrr(f.dc, GRR_CLIP, &bx, &c);
    CHECK(g == 1); CHECK(c == RC_NULL);
    c = U(GetClipBox)(f.dc, &bx);
    CHECK(c == RC_NULL);
    fx_white(&f);
    CHECK(fx_blacken(&f) == 0);   /* an empty clip draws nothing */
    /* ... and the empty clip survives until reset */
    U(SelectClipRgn)(f.dc, NULL);
    CHECK(fx_blacken(&f) == 64 * 64);
    fx_close(&f);
}

TEST(gdi_regions_clip_intersect_swapped_and_empty_rect_note) {
    READY(); FX(f, 64, 64);
    int r = U(IntersectClipRect)(f.dc, 30, 30, 10, 10);
    RECT bx; int c; int g = getrr(f.dc, GRR_CLIP, &bx, &c);
    t_note("swapped rect: ret %d, clip %d cplx %d (%ld,%ld,%ld,%ld)", r, g, c, bx.left, bx.top, bx.right, bx.bottom);
    U(SelectClipRgn)(f.dc, NULL);
    r = U(IntersectClipRect)(f.dc, 10, 10, 10, 30);
    t_note("empty rect: ret %d", r);
    fx_close(&f);
}

TEST(gdi_regions_clip_intersect_exclude_invalid_arguments) {
    READY(); NEED(UN(NtGdiIntersectClipRect)); NEED(UN(NtGdiExcludeClipRect)); FX(f, 64, 64);
    SET_ERR(0);
    CHECK(UN(NtGdiIntersectClipRect)(NULL, 0, 0, 10, 10) == 0);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    SET_ERR(0);
    CHECK(UN(NtGdiExcludeClipRect)(NULL, 0, 0, 10, 10) == 0);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    SET_ERR(0);
    CHECK(UN(NtGdiIntersectClipRect)((HDC)(ULONG_PTR)0x00010CCC, 0, 0, 10, 10) == 0);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    /* identity transform: the rectangle is range-tested, out of range -> 0x57 and no change */
    static const int bad[][4] = { { COORD_MIN - 1, 0, 10, 10 }, { 0, COORD_MIN - 1, 10, 10 }, { 0, 0, COORD_MAX + 1, 10 }, { 0, 0, 10, COORD_MAX + 1 } };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SET_ERR(0);
        CHECKF(UN(NtGdiIntersectClipRect)(f.dc, bad[i][0], bad[i][1], bad[i][2], bad[i][3]) == 0, "intersect row %u", i);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
    RECT bx; int c;
    CHECK(getrr(f.dc, GRR_CLIP, &bx, &c) == 0);   /* nothing was applied */
    /* a region handle is not a DC */
    HRGN r = U(CreateRectRgn)(0, 0, 1, 1);
    SET_ERR(0);
    CHECK(UN(NtGdiIntersectClipRect)((HDC)r, 0, 0, 10, 10) == 0);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    del(r);
    fx_close(&f);
}

TEST(gdi_regions_clip_exclude_rect_punches_hole) {
    READY(); NEED(U(ExcludeClipRect)); FX(f, 64, 64);
    int r = U(ExcludeClipRect)(f.dc, 10, 10, 20, 20);
    t_note("ExcludeClipRect returned %d", r);
    CHECK(r != RC_ERROR && r != RC_NULL);
    HRGN t = U(CreateRectRgn)(0, 0, 1, 1);
    CHECK(UN(NtGdiGetRandomRgn)(f.dc, t, GRR_CLIP) == 1);
    CHECK(!UN(NtGdiPtInRegion)(t, 15, 15));
    CHECK(!UN(NtGdiPtInRegion)(t, 10, 10));
    CHECK(!UN(NtGdiPtInRegion)(t, 19, 19));
    CHECK(UN(NtGdiPtInRegion)(t, 20, 20));
    CHECK(UN(NtGdiPtInRegion)(t, 9, 15));
    CHECK(UN(NtGdiPtInRegion)(t, 5, 5));
    CHECK(UN(NtGdiPtInRegion)(t, 40, 40));
    del(t);
    fx_white(&f);
    CHECK(fx_blacken(&f) == 64 * 64 - 100);
    CHECK(!fx_px_black(&f, 15, 15)); CHECK(fx_px_black(&f, 20, 20)); CHECK(fx_px_black(&f, 9, 15));
    fx_close(&f);
}

TEST(gdi_regions_clip_exclude_everything_and_nothing) {
    READY(); NEED(U(ExcludeClipRect)); FX(f, 64, 64);
    int r = U(ExcludeClipRect)(f.dc, -10, -10, 100, 100);
    CHECKF(r == RC_NULL, "excluding the whole bitmap returned %d", r);
    fx_white(&f);
    CHECK(fx_blacken(&f) == 0);
    U(SelectClipRgn)(f.dc, NULL);
    r = U(ExcludeClipRect)(f.dc, 100, 100, 120, 120);   /* disjoint from the visible area: no effective change */
    CHECK(r != RC_ERROR && r != RC_NULL);
    CHECK(fx_blacken(&f) == 64 * 64);
    fx_close(&f);
}

TEST(gdi_regions_clip_exclude_then_intersect_complex_clip) {
    READY(); NEED(U(ExcludeClipRect)); FX(f, 64, 64);
    U(IntersectClipRect)(f.dc, 0, 0, 40, 40);
    U(ExcludeClipRect)(f.dc, 10, 10, 30, 30);
    U(IntersectClipRect)(f.dc, 5, 5, 35, 35);
    fx_white(&f);
    int n = fx_blacken(&f);
    CHECKF(n == 30 * 30 - 20 * 20, "black %d", n);
    RECT bx; int c = U(GetClipBox)(f.dc, &bx);
    CHECK(c == RC_COMPLEX); CHECK_BOX(bx, 5, 5, 35, 35);
    fx_close(&f);
}

/* ---- OffsetClipRgn --------------------------------------------------------------------------------------------- */

TEST(gdi_regions_clip_offsetclip_moves_clip) {
    READY(); NEED(U(OffsetClipRgn)); FX(f, 64, 64);
    HRGN r = U(CreateRectRgn)(10, 10, 30, 30);
    U(SelectClipRgn)(f.dc, r);
    int ret = U(OffsetClipRgn)(f.dc, 5, -3);
    CHECKF(ret == RC_SIMPLE, "ret %d", ret);
    CHECK(clip_is_rect(f.dc, 15, 7, 35, 27));
    CHECK(rgn_is_rect(r, 10, 10, 30, 30));   /* the caller's region was never the clip */
    fx_white(&f);
    CHECK(fx_blacken(&f) == 400);
    CHECK(fx_px_black(&f, 15, 7)); CHECK(!fx_px_black(&f, 14, 7));
    del(r);
    fx_close(&f);
}

TEST(gdi_regions_clip_offsetclip_without_clip_is_simple_noop) {
    READY(); NEED(U(OffsetClipRgn)); FX(f, 64, 64);
    int ret = U(OffsetClipRgn)(f.dc, 7, 7);
    CHECKF(ret == RC_SIMPLE, "ret %d", ret);   /* NtGdiOffsetClipRgn: DC+0xa0 == NULL -> 2 */
    RECT bx; int c;
    CHECK(getrr(f.dc, GRR_CLIP, &bx, &c) == 0);
    fx_close(&f);
}

TEST(gdi_regions_clip_offsetclip_invalid_dc) {
    READY(); NEED(UN(NtGdiOffsetClipRgn));
    SET_ERR(0);
    CHECK(UN(NtGdiOffsetClipRgn)(NULL, 1, 1) == 0);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    SET_ERR(0);
    CHECK(UN(NtGdiOffsetClipRgn)((HDC)(ULONG_PTR)0x00010DDD, 1, 1) == 0);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
}

TEST(gdi_regions_clip_offsetclip_complex_region) {
    READY(); NEED(U(OffsetClipRgn)); FX(f, 64, 64);
    HRGN a = kcreate(0, 0, 10, 10), b = kcreate(5, 5, 15, 15), u = kcreate(0, 0, 1, 1);
    UN(NtGdiCombineRgn)(u, a, b, RGN_OR);
    UN(NtGdiExtSelectClipRgn)(f.dc, u, RGN_COPY);
    int ret = U(OffsetClipRgn)(f.dc, 20, 30);
    CHECKF(ret == RC_COMPLEX, "ret %d", ret);
    RECT out[8]; int n = getrr_rects(f.dc, GRR_CLIP, out, 8);
    CHECK(n == 3);
    if (n == 3) { CHECK_BOX(out[0], 20, 30, 30, 35); CHECK_BOX(out[1], 20, 35, 35, 40); CHECK_BOX(out[2], 25, 40, 35, 45); }
    del(a); del(b); del(u);
    fx_close(&f);
}

/* ---- clip is stored in device coordinates ---------------------------------------------------------------------- */

TEST(gdi_regions_clip_intersect_applies_viewport_origin) {
    READY(); NEED(U(SetViewportOrgEx)); FX(f, 64, 64);
    U(SetViewportOrgEx)(f.dc, 5, 7, NULL);
    U(IntersectClipRect)(f.dc, 0, 0, 10, 10);
    CHECK(clip_is_rect(f.dc, 5, 7, 15, 17));   /* stored in device units */
    RECT bx; int c = U(GetClipBox)(f.dc, &bx);
    CHECK(c == RC_SIMPLE); CHECK_BOX(bx, 0, 0, 10, 10);   /* reported back in logical units */
    fx_white(&f);
    CHECK(fx_blacken(&f) == 100);
    CHECK(fx_px_black(&f, 5, 7)); CHECK(fx_px_black(&f, 14, 16)); CHECK(!fx_px_black(&f, 4, 7)); CHECK(!fx_px_black(&f, 15, 17));
    fx_close(&f);
}

TEST(gdi_regions_clip_changing_origin_does_not_move_existing_clip) {
    READY(); NEED(U(SetViewportOrgEx)); FX(f, 64, 64);
    U(IntersectClipRect)(f.dc, 0, 0, 10, 10);
    U(SetViewportOrgEx)(f.dc, 20, 20, NULL);
    CHECK(clip_is_rect(f.dc, 0, 0, 10, 10));
    RECT bx; U(GetClipBox)(f.dc, &bx);
    CHECK_BOX(bx, -20, -20, -10, -10);
    fx_close(&f);
}

TEST(gdi_regions_clip_intersect_scales_with_extents) {
    READY(); NEED(U(SetMapMode)); NEED(U(SetWindowExtEx)); NEED(U(SetViewportExtEx)); FX(f, 64, 64);
    U(SetMapMode)(f.dc, MM_ANISOTROPIC);
    U(SetWindowExtEx)(f.dc, 1, 1, NULL);
    U(SetViewportExtEx)(f.dc, 2, 3, NULL);
    U(IntersectClipRect)(f.dc, 1, 1, 10, 10);
    CHECK(clip_is_rect(f.dc, 2, 3, 20, 30));
    RECT bx; U(GetClipBox)(f.dc, &bx);
    CHECK_BOX(bx, 1, 1, 10, 10);
    fx_close(&f);
}

TEST(gdi_regions_clip_offsetclip_scales_with_extents) {
    READY(); NEED(U(SetMapMode)); NEED(U(SetWindowExtEx)); NEED(U(SetViewportExtEx)); NEED(U(OffsetClipRgn)); FX(f, 64, 64);
    U(SetMapMode)(f.dc, MM_ANISOTROPIC);
    U(SetWindowExtEx)(f.dc, 1, 1, NULL);
    U(SetViewportExtEx)(f.dc, 2, 3, NULL);
    HRGN r = U(CreateRectRgn)(10, 10, 20, 20);
    U(SelectClipRgn)(f.dc, r);
    U(OffsetClipRgn)(f.dc, 3, 2);   /* NtGdiOffsetClipRgn transforms the vector (3,2) -> (6,6) */
    CHECK(clip_is_rect(f.dc, 16, 16, 26, 26));
    del(r);
    fx_close(&f);
}

TEST(gdi_regions_clip_intersect_through_world_transform) {
    READY(); NEED(U(SetGraphicsMode)); NEED(U(SetWorldTransform)); FX(f, 64, 64);
    if (!U(SetGraphicsMode)(f.dc, GM_ADVANCED)) { t_skip("GM_ADVANCED unavailable"); fx_close(&f); return; }
    XFORM x = { 2, 0, 0, 3, 1, 2 };
    CHECK(U(SetWorldTransform)(f.dc, &x));
    U(IntersectClipRect)(f.dc, 0, 0, 5, 5);
    CHECK(clip_is_rect(f.dc, 1, 2, 11, 17));   /* IntersectClipRect: the rectangle is transformed */
    RECT bx; U(GetClipBox)(f.dc, &bx);
    t_note("GetClipBox under world transform: (%ld,%ld,%ld,%ld)", bx.left, bx.top, bx.right, bx.bottom);
    fx_close(&f);
}

TEST(gdi_regions_clip_intersect_through_rotation_makes_bounding_clip) {
    READY(); NEED(U(SetGraphicsMode)); NEED(U(SetWorldTransform)); FX(f, 64, 64);
    U(SetGraphicsMode)(f.dc, GM_ADVANCED);
    XFORM x = { 0, 1, -1, 0, 40, 0 };   /* 90 degrees: (x,y) -> (40 - y, x) */
    if (!U(SetWorldTransform)(f.dc, &x)) { t_skip("no rotation"); fx_close(&f); return; }
    U(IntersectClipRect)(f.dc, 0, 0, 10, 20);
    RECT bx; int c;
    int g = getrr(f.dc, GRR_CLIP, &bx, &c);
    t_note("rotated intersect: get %d cplx %d (%ld,%ld,%ld,%ld)", g, c, bx.left, bx.top, bx.right, bx.bottom);
    CHECK(g == 1);
    CHECK(bx.left >= 19 && bx.right <= 41 && bx.top >= -1 && bx.bottom <= 11);
    fx_close(&f);
}

/* ---- GetClipBox / GetAppClipBox ---------------------------------------------------------------------------------- */

TEST(gdi_regions_clip_getclipbox_unclipped_memory_dc) {
    READY(); FX(f, 64, 48);
    RECT bx; int c = U(GetClipBox)(f.dc, &bx);
    CHECK(c != RC_ERROR); CHECK_BOX(bx, 0, 0, 64, 48);
    t_note("GetClipBox on unclipped memory DC returned %d", c);
    CHECK(UN(NtGdiGetAppClipBox)(f.dc, &bx) != 0);
    CHECK_BOX(bx, 0, 0, 64, 48);
    fx_close(&f);
}

TEST(gdi_regions_clip_getclipbox_clamps_to_surface) {
    READY(); FX(f, 64, 64);
    HRGN r = U(CreateRectRgn)(50, 50, 100, 100);
    U(SelectClipRgn)(f.dc, r);
    RECT bx; int c = U(GetClipBox)(f.dc, &bx);
    CHECK(c == RC_SIMPLE); CHECK_BOX(bx, 50, 50, 64, 64);     /* effective = clip AND visible */
    CHECK(clip_is_rect(f.dc, 50, 50, 100, 100));                 /* the stored clip keeps its full extent */
    HRGN far_ = U(CreateRectRgn)(200, 200, 300, 300);
    c = U(SelectClipRgn)(f.dc, far_);
    CHECKF(c == RC_NULL, "clip fully outside the surface: %d", c);
    c = U(GetClipBox)(f.dc, &bx);
    CHECK(c == RC_NULL);
    del(r); del(far_);
    fx_close(&f);
}

TEST(gdi_regions_clip_getappclipbox_invalid_dc) {
    READY(); NEED(UN(NtGdiGetAppClipBox));
    RECT bx;
    SET_ERR(0);
    CHECK(UN(NtGdiGetAppClipBox)(NULL, &bx) == 0);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    SET_ERR(0);
    CHECK(U(GetClipBox)(NULL, &bx) == 0);
    SET_ERR(0);
    CHECK(UN(NtGdiGetAppClipBox)((HDC)(ULONG_PTR)0x00010EEE, &bx) == 0);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
}

/* ---- SetMetaRgn ------------------------------------------------------------------------------------------------------ */

TEST(gdi_regions_clip_setmetargn_moves_clip_into_meta) {
    READY(); NEED(U(SetMetaRgn)); FX(f, 64, 64);
    HRGN r = U(CreateRectRgn)(10, 10, 40, 40);
    U(SelectClipRgn)(f.dc, r);
    int ret = U(SetMetaRgn)(f.dc);
    CHECKF(ret == RC_SIMPLE, "SetMetaRgn returned %d", ret);   /* DC::iSetMetaRgn: complexity of the old clip */
    RECT bx; int c;
    CHECK(getrr(f.dc, GRR_META, &bx, &c) == 1);
    CHECK(c == RC_SIMPLE); CHECK_BOX(bx, 10, 10, 40, 40);
    CHECK(getrr(f.dc, GRR_CLIP, &bx, &c) == 0);   /* clip region is now empty of state */
    c = U(GetClipBox)(f.dc, &bx);
    CHECK(c == RC_SIMPLE); CHECK_BOX(bx, 10, 10, 40, 40);   /* effective region is the meta region */
    del(r);
    fx_close(&f);
}

TEST(gdi_regions_clip_setmetargn_without_any_clip_returns_simple) {
    READY(); NEED(U(SetMetaRgn)); FX(f, 64, 64);
    int ret = U(SetMetaRgn)(f.dc);
    CHECKF(ret == RC_SIMPLE, "ret %d", ret);   /* meta==NULL and clip==NULL -> 2 */
    RECT bx; int c;
    CHECK(getrr(f.dc, GRR_META, &bx, &c) == 0);
    fx_close(&f);
}

TEST(gdi_regions_clip_setmetargn_intersects_with_existing_meta) {
    READY(); NEED(U(SetMetaRgn)); FX(f, 64, 64);
    U(IntersectClipRect)(f.dc, 0, 0, 40, 40);
    U(SetMetaRgn)(f.dc);
    U(IntersectClipRect)(f.dc, 20, 20, 60, 60);
    RECT bx; int c;
    /* APIRGN (3) is the clip ANDed with meta */
    CHECK(getrr(f.dc, GRR_API, &bx, &c) == 1);
    CHECK(c == RC_SIMPLE); CHECK_BOX(bx, 20, 20, 40, 40);
    int ret = U(SetMetaRgn)(f.dc);
    CHECKF(ret == RC_SIMPLE, "ret %d", ret);
    CHECK(getrr(f.dc, GRR_META, &bx, &c) == 1); CHECK_BOX(bx, 20, 20, 40, 40);
    CHECK(getrr(f.dc, GRR_CLIP, &bx, &c) == 0);
    fx_close(&f);
}

TEST(gdi_regions_clip_meta_limits_later_clips_and_pixels) {
    READY(); NEED(U(SetMetaRgn)); FX(f, 64, 64);
    U(IntersectClipRect)(f.dc, 8, 8, 24, 24);
    U(SetMetaRgn)(f.dc);
    HRGN big = U(CreateRectRgn)(0, 0, 64, 64);
    U(SelectClipRgn)(f.dc, big);
    fx_white(&f);
    CHECK(fx_blacken(&f) == 16 * 16);   /* the clip cannot escape the meta region */
    RECT bx; int c;
    CHECK(getrr(f.dc, GRR_CLIP, &bx, &c) == 1); CHECK_BOX(bx, 0, 0, 64, 64);
    CHECK(getrr(f.dc, GRR_API, &bx, &c) == 1); CHECK_BOX(bx, 8, 8, 24, 24);
    /* resetting the clip keeps the meta region */
    U(SelectClipRgn)(f.dc, NULL);
    CHECK(getrr(f.dc, GRR_CLIP, &bx, &c) == 0);
    CHECK(getrr(f.dc, GRR_META, &bx, &c) == 1);
    fx_white(&f);
    CHECK(fx_blacken(&f) == 16 * 16);
    del(big);
    fx_close(&f);
}

TEST(gdi_regions_clip_setmetargn_invalid_dc) {
    READY(); NEED(UN(NtGdiSetMetaRgn));
    SET_ERR(0);
    CHECK(UN(NtGdiSetMetaRgn)(NULL) == 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);   /* NtGdiSetMetaRgn reports 0x57, not 6 */
    SET_ERR(0);
    CHECK(UN(NtGdiSetMetaRgn)((HDC)(ULONG_PTR)0x00010FFF) == 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}

/* ---- clip effect ordering with the GDI batch ---------------------------------------------------------------------------- */

TEST(gdi_regions_clip_batched_drawing_keeps_clip_ordering) {
    READY(); FX(f, 64, 64);
    fx_white(&f);
    HRGN right = U(CreateRectRgn)(32, 0, 64, 64), left = U(CreateRectRgn)(0, 0, 32, 64);
    U(SelectClipRgn)(f.dc, right);
    U(PatBlt)(f.dc, 0, 0, 64, 64, DSTINVERT);   /* queued in the TEB batch */
    U(SelectClipRgn)(f.dc, left);
    U(PatBlt)(f.dc, 0, 0, 64, 64, DSTINVERT);
    U(SelectClipRgn)(f.dc, NULL);
    U(GdiFlush)();
    int bl = 0, br = 0;
    for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) if (fx_px_black(&f, x, y)) { if (x < 32) bl++; else br++; }
    CHECKF(bl == 32 * 64 && br == 32 * 64, "left %d right %d", bl, br);
    del(right); del(left);
    fx_close(&f);
}

TEST(gdi_regions_clip_clip_replaced_between_draws) {
    READY(); FX(f, 64, 64);
    fx_white(&f);
    HRGN a = U(CreateRectRgn)(0, 0, 10, 64), b = U(CreateRectRgn)(0, 0, 64, 10);
    U(SelectClipRgn)(f.dc, a);
    U(PatBlt)(f.dc, 0, 0, 64, 64, BLACKNESS);
    U(SelectClipRgn)(f.dc, b);
    U(PatBlt)(f.dc, 0, 0, 64, 64, BLACKNESS);
    U(GdiFlush)();
    int n = 0;
    for (int i = 0; i < 64 * 64; i++) if ((f.bits[i] & 0xffffff) == 0) n++;
    CHECKF(n == 10 * 64 + 64 * 10 - 100, "black %d", n);
    del(a); del(b);
    fx_close(&f);
}

/* ---- paths and clipping ------------------------------------------------------------------------------------------------------ */

static void path_rect(HDC dc, int l, int t, int r, int b) {
    U(BeginPath)(dc); U(Rectangle)(dc, l, t, r, b); U(EndPath)(dc);
}

TEST(gdi_regions_clip_selectclippath_rectangle) {
    READY(); NEED(U(BeginPath)); NEED(U(Rectangle)); NEED(U(EndPath)); NEED(U(SelectClipPath)); FX(f, 64, 64);
    path_rect(f.dc, 10, 10, 30, 30);
    BOOL ok = U(SelectClipPath)(f.dc, RGN_COPY);
    CHECK(ok);
    RECT bx; int c; int g = getrr(f.dc, GRR_CLIP, &bx, &c);
    CHECK(g == 1);
    t_note("Rectangle(10,10,30,30) path clip: cplx %d box (%ld,%ld,%ld,%ld)", c, bx.left, bx.top, bx.right, bx.bottom);
    CHECK(bx.left >= 9 && bx.left <= 10 && bx.top >= 9 && bx.top <= 10 && bx.right >= 29 && bx.right <= 31 && bx.bottom >= 29 && bx.bottom <= 31);
    HRGN t = U(CreateRectRgn)(0, 0, 1, 1);
    UN(NtGdiGetRandomRgn)(f.dc, t, GRR_CLIP);
    CHECK(UN(NtGdiPtInRegion)(t, 20, 20)); CHECK(!UN(NtGdiPtInRegion)(t, 5, 5)); CHECK(!UN(NtGdiPtInRegion)(t, 40, 40));
    del(t);
    /* the path was consumed by SelectClipPath */
    SET_ERR(0);
    CHECK(!U(SelectClipPath)(f.dc, RGN_COPY));
    fx_close(&f);
}

TEST(gdi_regions_clip_selectclippath_modes_combine_with_clip) {
    READY(); NEED(U(BeginPath)); NEED(U(Rectangle)); NEED(U(EndPath)); NEED(U(SelectClipPath)); FX(f, 64, 64);
    HRGN base = U(CreateRectRgn)(0, 0, 20, 20);
    U(SelectClipRgn)(f.dc, base);
    path_rect(f.dc, 10, 10, 40, 40);
    CHECK(U(SelectClipPath)(f.dc, RGN_AND));
    HRGN t = U(CreateRectRgn)(0, 0, 1, 1);
    UN(NtGdiGetRandomRgn)(f.dc, t, GRR_CLIP);
    CHECK(UN(NtGdiPtInRegion)(t, 15, 15)); CHECK(!UN(NtGdiPtInRegion)(t, 5, 5)); CHECK(!UN(NtGdiPtInRegion)(t, 30, 30));
    U(SelectClipRgn)(f.dc, base);
    path_rect(f.dc, 10, 10, 40, 40);
    CHECK(U(SelectClipPath)(f.dc, RGN_DIFF));
    UN(NtGdiGetRandomRgn)(f.dc, t, GRR_CLIP);
    CHECK(UN(NtGdiPtInRegion)(t, 5, 5)); CHECK(!UN(NtGdiPtInRegion)(t, 15, 15));
    del(t); del(base);
    fx_close(&f);
}

TEST(gdi_regions_clip_selectclippath_invalid_arguments) {
    READY(); NEED(UN(NtGdiSelectClipPath)); NEED(U(BeginPath)); NEED(U(Rectangle)); NEED(U(EndPath)); FX(f, 64, 64);
    path_rect(f.dc, 1, 1, 9, 9);
    static const int bad[] = { 0, 6, -1, 77 };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SET_ERR(0);
        CHECKF(!UN(NtGdiSelectClipPath)(f.dc, bad[i]), "mode %d", bad[i]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
    SET_ERR(0);
    CHECK(!UN(NtGdiSelectClipPath)(NULL, RGN_COPY));
    EXPECT_ERR(ERROR_INVALID_PARAMETER);   /* NtGdiSelectClipPath: DCOBJ null -> 0x57 */
    /* the rejected calls did not consume the closed path */
    CHECK(UN(NtGdiSelectClipPath)(f.dc, RGN_COPY));
    fx_close(&f);
}

TEST(gdi_regions_clip_selectclippath_open_bracket_fails) {
    READY(); NEED(UN(NtGdiSelectClipPath)); NEED(U(BeginPath)); NEED(U(Rectangle)); NEED(U(AbortPath)); FX(f, 64, 64);
    U(BeginPath)(f.dc);
    U(Rectangle)(f.dc, 1, 1, 9, 9);
    SET_ERR(0);
    CHECK(!UN(NtGdiSelectClipPath)(f.dc, RGN_COPY));
    EXPECT_ERR(1003);   /* DC::bInactive false: ERROR_CAN_NOT_COMPLETE */
    SET_ERR(0);
    CHECK(UN(NtGdiPathToRegion)(f.dc) == NULL);
    EXPECT_ERR(1003);
    U(AbortPath)(f.dc);
    SET_ERR(0);
    CHECK(!UN(NtGdiSelectClipPath)(f.dc, RGN_COPY));
    t_note("SelectClipPath with no path: error %lu", GetLastError());
    fx_close(&f);
}

TEST(gdi_regions_clip_pathtoregion_shape_and_consumption) {
    READY(); NEED(UN(NtGdiPathToRegion)); NEED(U(BeginPath)); NEED(U(Rectangle)); NEED(U(EndPath)); FX(f, 64, 64);
    path_rect(f.dc, 4, 6, 24, 16);
    HRGN h = UN(NtGdiPathToRegion)(f.dc);
    CHECK(h != NULL);
    if (h) {
        RECT bx; int c = kbox(h, &bx);
        t_note("PathToRegion Rectangle(4,6,24,16): cplx %d (%ld,%ld,%ld,%ld)", c, bx.left, bx.top, bx.right, bx.bottom);
        CHECK(bx.left >= 3 && bx.left <= 4 && bx.top >= 5 && bx.top <= 6 && bx.right >= 23 && bx.right <= 25 && bx.bottom >= 15 && bx.bottom <= 17);
        CHECK(UN(NtGdiPtInRegion)(h, 10, 10)); CHECK(!UN(NtGdiPtInRegion)(h, 2, 2)); CHECK(!UN(NtGdiPtInRegion)(h, 30, 10));
        del(h);
    }
    SET_ERR(0);
    CHECK(UN(NtGdiPathToRegion)(f.dc) == NULL);   /* path was consumed */
    fx_close(&f);
}

TEST(gdi_regions_clip_pathtoregion_invalid_dc) {
    READY(); NEED(UN(NtGdiPathToRegion));
    SET_ERR(0);
    CHECK(UN(NtGdiPathToRegion)(NULL) == NULL);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SET_ERR(0);
    CHECK(UN(NtGdiPathToRegion)((HDC)(ULONG_PTR)0x00010AAA) == NULL);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}

TEST(gdi_regions_clip_pathtoregion_honours_poly_fill_mode) {
    READY(); NEED(UN(NtGdiPathToRegion)); NEED(U(BeginPath)); NEED(U(PolyPolygon)); NEED(U(EndPath)); NEED(U(SetPolyFillMode)); FX(f, 64, 64);
    POINT pt[8] = { { 0, 0 }, { 10, 0 }, { 10, 10 }, { 0, 10 }, { 5, 5 }, { 15, 5 }, { 15, 15 }, { 5, 15 } };
    INT cnt[2] = { 4, 4 };
    for (int mode = ALTERNATE; mode <= WINDING; mode++) {
        U(SetPolyFillMode)(f.dc, mode);
        U(BeginPath)(f.dc);
        U(PolyPolygon)(f.dc, pt, cnt, 2);
        U(EndPath)(f.dc);
        HRGN h = UN(NtGdiPathToRegion)(f.dc);   /* RGNMEMOBJ::vCreate(path, fill mode from the DC) */
        CHECKF(h != NULL, "mode %d", mode);
        if (!h) continue;
        CHECK(UN(NtGdiPtInRegion)(h, 2, 2)); CHECK(UN(NtGdiPtInRegion)(h, 12, 12));
        CHECKF((UN(NtGdiPtInRegion)(h, 7, 7) != 0) == (mode == WINDING), "mode %d overlap pixel", mode);
        del(h);
    }
    fx_close(&f);
}

/* ---- window DCs -------------------------------------------------------------------------------------------------------------- */

TEST(gdi_regions_clip_window_dc_clip_and_sysrgn) {
    READY(); NEED(U(GetDC)); NEED(U(ReleaseDC)); NEED(U(DestroyWindow)); NEED(U(GetClientRect));
    HWND w = k_make_window("win32kt_rgn");
    if (!w) { t_skip("cannot create helper window"); return; }
    HDC dc = U(GetDC)(w);
    if (!dc) { t_skip("no window DC"); U(DestroyWindow)(w); return; }
    RECT bx; int c;
    /* a window DC starts without an application clip and without a meta region */
    CHECK(getrr(dc, GRR_CLIP, &bx, &c) == 0);
    CHECK(getrr(dc, GRR_META, &bx, &c) == 0);
    /* system region of the (hidden, 1x1) window DC: copies fine and cannot exceed the client area */
    int r = getrr(dc, GRR_SYS, &bx, &c);
    t_note("hidden window SYSRGN: ret %d cplx %d (%ld,%ld,%ld,%ld)", r, c, bx.left, bx.top, bx.right, bx.bottom);
    CHECK(r == 1);
    /* application clip in window-DC device units round-trips */
    HRGN rg = U(CreateRectRgn)(2, 3, 12, 13);
    U(SelectClipRgn)(dc, rg);
    CHECK(clip_is_rect(dc, 2, 3, 12, 13));
    U(SelectClipRgn)(dc, NULL);
    CHECK(getrr(dc, GRR_CLIP, &bx, &c) == 0);
    del(rg);
    U(ReleaseDC)(w, dc);
    U(DestroyWindow)(w);
}

TEST(gdi_regions_clip_window_dc_sysrgn_tracks_window_position) {
    READY(); NEED(U(GetDC)); NEED(U(ReleaseDC)); NEED(U(DestroyWindow)); NEED(U(SetWindowPos)); NEED(U(ShowWindow)); NEED(U(GetWindowRect));
    if (!k_has_interactive_desktop()) { t_skip("no interactive desktop"); return; }
    HWND w = k_make_window("win32kt_rgn_pos");
    if (!w) { t_skip("cannot create helper window"); return; }
    /* partially off the top-left corner so the sliver on screen is tiny; never activated */
    U(SetWindowPos)(w, NULL, -30, -20, 80, 60, SWP_NOACTIVATE | SWP_NOZORDER);
    U(ShowWindow)(w, SW_SHOWNOACTIVATE);
    HDC dc = U(GetDC)(w);
    if (!dc) { t_skip("no window DC"); U(DestroyWindow)(w); return; }
    RECT wr; U(GetWindowRect)(w, &wr);
    RECT bx; int c;
    int r = getrr(dc, GRR_SYS, &bx, &c);
    CHECK(r == 1);
    if (r == 1 && c != RC_NULL) {
        /* SYSRGN is in screen coordinates: it lies inside the window rectangle */
        CHECKF(bx.left >= wr.left && bx.top >= wr.top && bx.right <= wr.right && bx.bottom <= wr.bottom,
               "sysrgn (%ld,%ld,%ld,%ld) window (%ld,%ld,%ld,%ld)", bx.left, bx.top, bx.right, bx.bottom, wr.left, wr.top, wr.right, wr.bottom);
        RECT first = bx;
        U(SetWindowPos)(w, NULL, -10, -5, 80, 60, SWP_NOACTIVATE | SWP_NOZORDER);
        U(GetWindowRect)(w, &wr);
        r = getrr(dc, GRR_SYS, &bx, &c);
        CHECK(r == 1);
        if (r == 1 && c != RC_NULL) {
            CHECKF(bx.left >= wr.left && bx.top >= wr.top && bx.right <= wr.right && bx.bottom <= wr.bottom, "moved sysrgn (%ld,%ld,%ld,%ld)", bx.left, bx.top, bx.right, bx.bottom);
            t_note("sysrgn before (%ld,%ld,%ld,%ld) after move (%ld,%ld,%ld,%ld)", first.left, first.top, first.right, first.bottom, bx.left, bx.top, bx.right, bx.bottom);
            CHECKF(bx.right > first.right || bx.bottom > first.bottom, "visible area did not grow after moving the window onto the screen");
        }
    } else t_note("window not visible on any monitor (ret %d cplx %d)", r, c);
    U(ReleaseDC)(w, dc);
    U(ShowWindow)(w, SW_HIDE);
    U(DestroyWindow)(w);
}
