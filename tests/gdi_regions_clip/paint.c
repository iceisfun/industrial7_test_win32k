/* Region painting through the kernel: FillRgn / PaintRgn / InvertRgn / FrameRgn (NtGdiFillRgn,
 * NtGdiInvertRgn, NtGdiFrameRgn) validated by the pixels written into a DIB section, including the
 * interaction with the DC clip region. */
#include "rgn.h"

UDECL(BOOL, NtGdiFillRgn,   (HDC, HRGN, HBRUSH));
UDECL(BOOL, NtGdiInvertRgn, (HDC, HRGN));
UDECL(BOOL, NtGdiFrameRgn,  (HDC, HRGN, HBRUSH, int, int));

#define READY() do { if (!U(CreateRectRgn) || !U(DeleteObject) || !U(FillRgn) || !U(GetStockObject) || !U(SelectClipRgn) || !U(GdiFlush) || !U(IntersectClipRect)) { t_skip("exports missing"); return; } } while (0)

static void del(HRGN h) { if (h) U(DeleteObject)(h); }
static int nblack(struct fx *f) { U(GdiFlush)(); int n = 0; for (int i = 0; i < f->w * f->h; i++) if ((f->bits[i] & 0xffffff) == 0) n++; return n; }
static int nblack_in(struct fx *f, int l, int t, int r, int b) { int n = 0; for (int y = t; y < b; y++) for (int x = l; x < r; x++) if (fx_px_black(f, x, y)) n++; return n; }

TEST(gdi_regions_clip_paint_fillrgn_rect_pixels) {
    READY(); FX(f, 32, 32);
    HRGN r = U(CreateRectRgn)(2, 3, 8, 9);
    /* FillRgn paints exactly the region area: 6x6 pixels, none outside. */
    CHECK(U(FillRgn)(f.dc, r, (HBRUSH)U(GetStockObject)(BLACK_BRUSH)));
    EXPECT_EQ(nblack(&f), 36);
    EXPECT_EQ(nblack_in(&f, 2, 3, 8, 9), 36);
    del(r); fx_close(&f);
}

TEST(gdi_regions_clip_paint_fillrgn_syscall_matches_userland) {
    READY(); NEED(UN(NtGdiFillRgn)); FX(f, 32, 32);
    HRGN r = U(CreateRectRgn)(1, 1, 5, 5);
    CHECK(UN(NtGdiFillRgn)(f.dc, r, (HBRUSH)U(GetStockObject)(BLACK_BRUSH)));
    EXPECT_EQ(nblack(&f), 16);
    del(r); fx_close(&f);
}

TEST(gdi_regions_clip_paint_fillrgn_complex_region_skips_hole) {
    READY(); FX(f, 32, 32);
    HRGN a = U(CreateRectRgn)(0, 0, 10, 10), b = U(CreateRectRgn)(3, 3, 7, 7);
    EXPECT_EQ(UN(NtGdiCombineRgn)(a, a, b, RGN_DIFF), RC_COMPLEX);
    CHECK(U(FillRgn)(f.dc, a, (HBRUSH)U(GetStockObject)(BLACK_BRUSH)));
    EXPECT_EQ(nblack(&f), 100 - 16);
    CHECK(!fx_px_black(&f, 5, 5));
    CHECK(fx_px_black(&f, 1, 1));
    del(a); del(b); fx_close(&f);
}

TEST(gdi_regions_clip_paint_fillrgn_respects_dc_clip) {
    READY(); FX(f, 32, 32);
    HRGN r = U(CreateRectRgn)(2, 2, 10, 10);
    /* the fill is intersected with the DC clip (device coordinates): x in [0,4) leaves 2x8 pixels */
    CHECK(U(IntersectClipRect)(f.dc, 0, 0, 4, 32) > 0);
    CHECK(U(FillRgn)(f.dc, r, (HBRUSH)U(GetStockObject)(BLACK_BRUSH)));
    EXPECT_EQ(nblack(&f), 16);
    EXPECT_EQ(nblack_in(&f, 2, 2, 4, 10), 16);
    del(r); fx_close(&f);
}

TEST(gdi_regions_clip_paint_fillrgn_empty_clip_draws_nothing) {
    READY(); FX(f, 16, 16);
    HRGN r = U(CreateRectRgn)(0, 0, 16, 16), nul = U(CreateRectRgn)(0, 0, 0, 0);
    CHECK(U(SelectClipRgn)(f.dc, nul) == RC_NULL);
    U(FillRgn)(f.dc, r, (HBRUSH)U(GetStockObject)(BLACK_BRUSH));
    EXPECT_EQ(nblack(&f), 0);
    del(r); del(nul); fx_close(&f);
}

TEST(gdi_regions_clip_paint_fillrgn_clip_copy_survives_source_delete) {
    READY(); FX(f, 16, 16);
    HRGN r = U(CreateRectRgn)(0, 0, 4, 4);
    /* the region is copied when selected; deleting r afterwards must not change what is painted */
    HRGN c = U(CreateRectRgn)(0, 0, 8, 8);
    U(SelectClipRgn)(f.dc, c); del(c);
    HRGN fr = U(CreateRectRgn)(0, 0, 16, 16);
    U(FillRgn)(f.dc, fr, (HBRUSH)U(GetStockObject)(BLACK_BRUSH));
    EXPECT_EQ(nblack(&f), 64);
    del(r); del(fr); fx_close(&f);
}

TEST(gdi_regions_clip_paint_paintrgn_uses_dc_brush) {
    READY(); FX(f, 16, 16);
    HRGN r = U(CreateRectRgn)(4, 4, 8, 8);
    /* PaintRgn fills with the DC's current brush (GetDCObject(hdc, OBJ_BRUSH) in gdi32full) */
    HGDIOBJ ob = U(SelectObject)(f.dc, U(GetStockObject)(BLACK_BRUSH));
    CHECK(U(PaintRgn)(f.dc, r));
    EXPECT_EQ(nblack(&f), 16);
    U(SelectObject)(f.dc, ob);
    del(r); fx_close(&f);
}

TEST(gdi_regions_clip_paint_invertrgn_twice_restores) {
    READY(); NEED(UN(NtGdiInvertRgn)); FX(f, 16, 16);
    HRGN r = U(CreateRectRgn)(2, 2, 6, 6);
    /* NtGdiInvertRgn is a DSTINVERT over the region: white becomes black, a second call restores */
    CHECK(UN(NtGdiInvertRgn)(f.dc, r));
    EXPECT_EQ(nblack(&f), 16);
    EXPECT_EQ(nblack_in(&f, 2, 2, 6, 6), 16);
    CHECK(UN(NtGdiInvertRgn)(f.dc, r));
    EXPECT_EQ(nblack(&f), 0);
    del(r); fx_close(&f);
}

TEST(gdi_regions_clip_paint_invertrgn_userland_respects_clip) {
    READY(); FX(f, 16, 16);
    HRGN r = U(CreateRectRgn)(0, 0, 16, 16);
    CHECK(U(IntersectClipRect)(f.dc, 4, 4, 6, 6) > 0);
    CHECK(U(InvertRgn)(f.dc, r));
    EXPECT_EQ(nblack(&f), 4);
    EXPECT_EQ(nblack_in(&f, 4, 4, 6, 6), 4);
    del(r); fx_close(&f);
}

TEST(gdi_regions_clip_paint_framergn_leaves_interior_white) {
    READY(); NEED(UN(NtGdiFrameRgn)); FX(f, 32, 32);
    HRGN r = U(CreateRectRgn)(4, 4, 20, 20);
    CHECK(UN(NtGdiFrameRgn)(f.dc, r, (HBRUSH)U(GetStockObject)(BLACK_BRUSH), 2, 2));
    int n = nblack(&f);
    CHECK(n > 0);
    /* a frame never covers the middle of a 16x16 region and stays near the region boundary */
    CHECK(!fx_px_black(&f, 12, 12));
    EXPECT_EQ(nblack_in(&f, 7, 7, 17, 17), 0);
    EXPECT_EQ(nblack_in(&f, 2, 2, 22, 22), n);
    t_note("FrameRgn(2,2) of a 16x16 rect painted %d pixels", n);
    del(r); fx_close(&f);
}

TEST(gdi_regions_clip_paint_framergn_wider_frame_paints_more) {
    READY(); NEED(UN(NtGdiFrameRgn)); FX(f, 32, 32);
    HRGN r = U(CreateRectRgn)(4, 4, 24, 24);
    HBRUSH b = (HBRUSH)U(GetStockObject)(BLACK_BRUSH);
    CHECK(UN(NtGdiFrameRgn)(f.dc, r, b, 1, 1)); int n1 = nblack(&f);
    fx_white(&f);
    CHECK(UN(NtGdiFrameRgn)(f.dc, r, b, 3, 3)); int n3 = nblack(&f);
    CHECKF(n3 > n1 && n1 > 0, "n1=%d n3=%d", n1, n3);
    del(r); fx_close(&f);
}

TEST(gdi_regions_clip_paint_fillrgn_null_arguments) {
    READY();
    FX(f, 8, 8);
    HRGN r = U(CreateRectRgn)(0, 0, 4, 4);
    /* gdi32full!FillRgn/FrameRgn/InvertRgn reject NULL hrgn (and NULL brush) before any syscall */
    CHECK(!U(FillRgn)(f.dc, NULL, (HBRUSH)U(GetStockObject)(BLACK_BRUSH)));
    CHECK(!U(FillRgn)(f.dc, r, NULL));
    CHECK(!U(InvertRgn)(f.dc, NULL));
    EXPECT_EQ(nblack(&f), 0);
    del(r); fx_close(&f);
}

TEST(gdi_regions_clip_paint_syscalls_invalid_handles) {
    READY(); NEED(UN(NtGdiFillRgn)); NEED(UN(NtGdiInvertRgn)); NEED(UN(NtGdiFrameRgn));
    FX(f, 8, 8);
    HRGN r = U(CreateRectRgn)(0, 0, 4, 4);
    HBRUSH b = (HBRUSH)U(GetStockObject)(BLACK_BRUSH);
    /* handles that are not objects of the required type are rejected by the handle manager lookups */
    CHECK(!UN(NtGdiFillRgn)((HDC)(ULONG_PTR)0x7777, r, b));
    CHECK(!UN(NtGdiInvertRgn)((HDC)(ULONG_PTR)0x7777, r));
    CHECK(!UN(NtGdiFrameRgn)((HDC)(ULONG_PTR)0x7777, r, b, 1, 1));
    CHECK(!UN(NtGdiInvertRgn)(f.dc, (HRGN)(ULONG_PTR)0x7777));
    CHECK(!UN(NtGdiFillRgn)(f.dc, (HRGN)(ULONG_PTR)0x7777, b));
    /* a brush handle used as a region, and a region used as a DC */
    CHECK(!UN(NtGdiInvertRgn)(f.dc, (HRGN)b));
    CHECK(!UN(NtGdiInvertRgn)((HDC)r, r));
    EXPECT_EQ(nblack(&f), 0);
    del(r); fx_close(&f);
}
