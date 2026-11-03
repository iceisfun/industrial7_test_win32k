/* UnrealizeObject and NtGdiUnrealizeObject on every object kind. The kernel stub succeeds only for palettes (stock
 * palette included) and fails for every other handle without setting an error; the gdi32 export additionally reports
 * TRUE for brushes (it decides from the handle type in user mode, so even a deleted or selected brush) and
 * FALSE for everything else. */
#include "xn.h"

#define NEED_U() do { if (!U(UnrealizeObject) || !UN(NtGdiUnrealizeObject)) { t_skip("exports missing"); return; } } while (0)
static void unr(int k, int gdi_want, int nt_want) {
    XH(x); NEED_U();
    SetLastError(0);
    int g = U(UnrealizeObject)(x.h[k]) ? 1 : 0, n = UN(NtGdiUnrealizeObject)(x.h[k]) ? 1 : 0;
    CHECKF(g == gdi_want, "gdi32 route %d want %d", g, gdi_want);
    CHECKF(n == nt_want, "Nt route %d want %d", n, nt_want);
    xh_close(&x);
}
XT(unrealize_null)       { unr(H_NULL, 0, 0); }
XT(unrealize_bitmap)     { unr(H_BITMAP, 0, 0); }
XT(unrealize_pen)        { unr(H_PEN, 0, 0); }
XT(unrealize_solid_brush) { unr(H_BRUSH, 1, 0); }
XT(unrealize_hatch_brush) { unr(H_HATCH, 1, 0); }
XT(unrealize_stock_brush) { unr(H_STOCKBRUSH, 1, 0); }
XT(unrealize_region)     { unr(H_REGION, 0, 0); }
XT(unrealize_palette)    { unr(H_PALETTE, 1, 1); }
XT(unrealize_stock_palette) { unr(H_STOCKPAL, 1, 1); }
XT(unrealize_font)       { unr(H_FONT, 0, 0); }
XT(unrealize_stock_font) { unr(H_STOCKFONT, 0, 0); }
XT(unrealize_junk)       { unr(H_JUNK, 0, 0); }
XT(unrealize_window_handle) { unr(H_HWND, 0, 0); }
XT(unrealize_dc_handle) {
    NEED_U(); struct fx f; if (!fx_open(&f, 8, 8)) { t_skip("no DIB"); return; }
    CHECK(!U(UnrealizeObject)(f.dc)); CHECK(!UN(NtGdiUnrealizeObject)(f.dc));
    fx_close(&f);
}
XT(unrealize_selected_brush_still_true) {
    NEED_U(); struct fx f; if (!fx_open(&f, 8, 8)) { t_skip("no DIB"); return; }
    HBRUSH b = U(CreateSolidBrush)(0x123456); HGDIOBJ o = U(SelectObject)(f.dc, b);
    CHECK(U(UnrealizeObject)(b));
    U(SelectObject)(f.dc, o); U(DeleteObject)(b); fx_close(&f);
}
XT(unrealize_palette_selected_into_dc) {
    NEED_U(); struct fx f; if (!fx_open(&f, 8, 8)) { t_skip("no DIB"); return; }
    LOGPALETTE lp = { 0x300, 1, { { 9, 8, 7, 0 } } }; HPALETTE p = U(CreatePalette)(&lp);
    HGDIOBJ o = U(SelectPalette)(f.dc, p, FALSE);
    CHECK(U(UnrealizeObject)(p)); CHECK(UN(NtGdiUnrealizeObject)(p));
    U(SelectPalette)(f.dc, o, FALSE); U(DeleteObject)(p); fx_close(&f);
}
XT(unrealize_palette_twice_is_idempotent) {
    NEED_U(); LOGPALETTE lp = { 0x300, 1, { { 9, 8, 7, 0 } } }; HPALETTE p = U(CreatePalette)(&lp);
    CHECK(p != NULL); CHECK(UN(NtGdiUnrealizeObject)(p)); CHECK(UN(NtGdiUnrealizeObject)(p));
    U(DeleteObject)(p);
}
XT(unrealize_deleted_palette_fails_in_kernel) {
    NEED_U(); LOGPALETTE lp = { 0x300, 1, { { 9, 8, 7, 0 } } }; HPALETTE p = U(CreatePalette)(&lp);
    CHECK(U(DeleteObject)(p));
    CHECK(!UN(NtGdiUnrealizeObject)(p));
}
XT(unrealize_does_not_change_palette_contents) {
    NEED_U(); if (!U(GetPaletteEntries)) { t_skip("exports missing"); return; }
    LOGPALETTE lp = { 0x300, 1, { { 9, 8, 7, 0 } } }; HPALETTE p = U(CreatePalette)(&lp);
    PALETTEENTRY e = { 0 };
    UN(NtGdiUnrealizeObject)(p);
    EXPECT_EQ(U(GetPaletteEntries)(p, 0, 1, &e), 1);
    CHECK(e.peRed == 9 && e.peGreen == 8 && e.peBlue == 7);
    U(DeleteObject)(p);
}
XT(unrealize_region_leaves_region_usable) {
    NEED_U(); HRGN r = U(CreateRectRgn)(1, 2, 3, 4); RECT b;
    CHECK(!U(UnrealizeObject)(r));
    CHECK(kbox(r, &b) == RC_SIMPLE); CHECK(box_is(&b, 1, 2, 3, 4));
    U(DeleteObject)(r);
}
