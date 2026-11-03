/* NtGdiMonoBitmap: reports whether a bitmap surface uses the monochrome palette. Matrix of bitmap kinds (DDB and DIB
 * section of every depth, DIB sections with custom 1-bit palettes, the default bitmap of a memory DC, a bitmap
 * selected into a DC) and of wrong handle types; the result is 1 only for a monochrome surface and 0 for every
 * failure. */
#include "xb.h"

static HBITMAP dib(int bpp, int w, int h, DWORD c0, DWORD c1) {
    BYTE bi[40 + 1024]; BITMAPINFO *b = (BITMAPINFO *)bi; memset(bi, 0, sizeof bi);
    b->bmiHeader.biSize = 40; b->bmiHeader.biWidth = w; b->bmiHeader.biHeight = h; b->bmiHeader.biPlanes = 1; b->bmiHeader.biBitCount = (WORD)bpp;
    if (bpp == 1) { RGBQUAD *q = b->bmiColors; q[0].rgbRed = GetRValue(c0); q[0].rgbGreen = GetGValue(c0); q[0].rgbBlue = GetBValue(c0);
                    q[1].rgbRed = GetRValue(c1); q[1].rgbGreen = GetGValue(c1); q[1].rgbBlue = GetBValue(c1); }
    HDC dc = U(CreateCompatibleDC)(NULL); void *p; HBITMAP bm = U(CreateDIBSection)(dc, b, DIB_RGB_COLORS, &p, NULL, 0); U(DeleteDC)(dc); return bm;
}
#define MROWS(X) \
 X(ddb_mono_8x8,   U(CreateBitmap)(8, 8, 1, 1, NULL), 1) \
 X(ddb_mono_1x1,   U(CreateBitmap)(1, 1, 1, 1, NULL), 1) \
 X(ddb_mono_100x3, U(CreateBitmap)(100, 3, 1, 1, NULL), 1) \
 X(ddb_32,         U(CreateBitmap)(8, 8, 1, 32, NULL), 0) \
 X(ddb_24,         U(CreateBitmap)(8, 8, 1, 24, NULL), 0) \
 X(ddb_16,         U(CreateBitmap)(8, 8, 1, 16, NULL), 0) \
 X(ddb_8,          U(CreateBitmap)(8, 8, 1, 8, NULL), 0) \
 X(ddb_4,          U(CreateBitmap)(8, 8, 1, 4, NULL), 0) \
 X(dib_1_default,  dib(1, 8, 8, 0x000000, 0xFFFFFF), 0) \
 X(dib_1_red_blue, dib(1, 8, 8, RGB(255,0,0), RGB(0,0,255)), 0) \
 X(dib_1_white_black, dib(1, 8, 8, 0xFFFFFF, 0x000000), 0) \
 X(dib_1_gray,     dib(1, 8, 8, RGB(64,64,64), RGB(192,192,192)), 0) \
 X(dib_1_same,     dib(1, 8, 8, RGB(10,10,10), RGB(10,10,10)), 0) \
 X(dib_4,          dib(4, 8, 8, 0, 0), 0) \
 X(dib_8,          dib(8, 8, 8, 0, 0), 0) \
 X(dib_24,         dib(24, 8, 8, 0, 0), 0) \
 X(dib_32,         dib(32, 8, 8, 0, 0), 0) \
 X(dib_1_topdown,  dib(1, 8, -8, 0x000000, 0xFFFFFF), 0)

static void mono_row(const char *nm, HBITMAP bm, int exp) {
    REQUIRE(UN(NtGdiMonoBitmap)); REQUIRE(bm);
    SetLastError(0); int r = UN(NtGdiMonoBitmap)(bm);
    CHECKF(r == exp, "%s: returned %d want %d", nm, r, exp);
    CHECKF(GetLastError() == 0, "%s: error %lu", nm, (unsigned long)GetLastError());
    int again = UN(NtGdiMonoBitmap)(bm);
    CHECKF(again == r, "%s: second query %d, first %d", nm, again, r);
    HDC dc = U(CreateCompatibleDC)(NULL); HGDIOBJ old = U(SelectObject)(dc, bm);
    CHECKF(UN(NtGdiMonoBitmap)(bm) == r, "%s: result changed while selected into a DC", nm);
    U(SelectObject)(dc, old); U(DeleteDC)(dc);
    CHECKF(UN(NtGdiMonoBitmap)(bm) == r, "%s: result changed after deselecting", nm);
    U(DeleteObject)(bm);
    CHECKF(UN(NtGdiMonoBitmap)(bm) == 0, "%s: deleted bitmap answered non-zero", nm);
}
#define MT_(nm, expr, e) XT(mono_##nm) { mono_row(#nm, expr, e); }
MROWS(MT_)

XT(mono_wrong_handle_types) {
    REQUIRE(UN(NtGdiMonoBitmap)); XHS(x);
    for (int k = 0; k < XH_COUNT; k++) {
        if (k == XH_BITMAP) continue;
        SetLastError(0); int r = UN(NtGdiMonoBitmap)((HBITMAP)x.h[k]);
        CHECKF(r == 0, "%s: returned %d", xh_nm[k], r);
        CHECKF(GetLastError() == 0, "%s: error %lu", xh_nm[k], (unsigned long)GetLastError());
    }
    xhs_close(&x);
}
XT(mono_handle_bitmap_is_mono) { REQUIRE(UN(NtGdiMonoBitmap)); XHS(x); CHECK(UN(NtGdiMonoBitmap)(x.bm) == 1); xhs_close(&x); }
XT(mono_memdc_default_bitmap) {
    REQUIRE(UN(NtGdiMonoBitmap)); HDC dc = U(CreateCompatibleDC)(NULL); REQUIRE(dc);
    HBITMAP def = (HBITMAP)U(GetCurrentObject)(dc, OBJ_BITMAP);
    CHECK(def && UN(NtGdiMonoBitmap)(def) == 1);
    U(DeleteDC)(dc);
}
XT(mono_compatible_with_mono_dc) {
    REQUIRE(UN(NtGdiMonoBitmap)); HDC dc = U(CreateCompatibleDC)(NULL); REQUIRE(dc);
    HBITMAP b = U(CreateCompatibleBitmap)(dc, 8, 8);            /* the memory DC holds a 1bpp bitmap, so this is monochrome */
    CHECK(b && UN(NtGdiMonoBitmap)(b) == 1);
    if (b) U(DeleteObject)(b);
    U(DeleteDC)(dc);
}
XT(mono_compatible_with_screen) {
    REQUIRE(UN(NtGdiMonoBitmap)); HDC s = U(GetDC)(NULL); REQUIRE(s);
    HBITMAP b = U(CreateCompatibleBitmap)(s, 8, 8);
    int bpp = U(GetDeviceCaps)(s, BITSPIXEL) * U(GetDeviceCaps)(s, PLANES);
    if (bpp > 1) CHECK(b && UN(NtGdiMonoBitmap)(b) == 0); else t_skip("display is 1 bit per pixel");
    if (b) U(DeleteObject)(b);
    U(ReleaseDC)(NULL, s);
}
XT(mono_stock_bitmap_handle_types) {
    REQUIRE(UN(NtGdiMonoBitmap));
    /* a stock font and stock brush handle are not surfaces */
    CHECK(UN(NtGdiMonoBitmap)((HBITMAP)U(GetStockObject)(SYSTEM_FONT)) == 0);
    CHECK(UN(NtGdiMonoBitmap)((HBITMAP)U(GetStockObject)(WHITE_BRUSH)) == 0);
    CHECK(UN(NtGdiMonoBitmap)((HBITMAP)U(GetStockObject)(DEFAULT_PALETTE)) == 0);
}
XT(mono_handle_with_wrong_upper_bits) {
    REQUIRE(UN(NtGdiMonoBitmap)); HBITMAP b = U(CreateBitmap)(8, 8, 1, 1, NULL); REQUIRE(b);
    CHECK(UN(NtGdiMonoBitmap)(b) == 1);
    /* the reuse counter in the top byte must match: a handle with another counter value is rejected */
    HBITMAP w = (HBITMAP)(((ULONG_PTR)b) ^ 0x7F000000u);
    CHECK(UN(NtGdiMonoBitmap)(w) == 0);
    /* the type field must be a surface */
    HBITMAP t = (HBITMAP)(((ULONG_PTR)b) ^ 0x00010000u);
    CHECK(UN(NtGdiMonoBitmap)(t) == 0);
    U(DeleteObject)(b);
}
XT(mono_repeated_stress) {
    REQUIRE(UN(NtGdiMonoBitmap)); HBITMAP m = U(CreateBitmap)(8, 8, 1, 1, NULL), c = U(CreateBitmap)(8, 8, 1, 32, NULL); REQUIRE(m && c);
    int bad = 0; for (int i = 0; i < 200; i++) { if (UN(NtGdiMonoBitmap)(m) != 1) bad++; if (UN(NtGdiMonoBitmap)(c) != 0) bad++; }
    CHECKF(bad == 0, "%d inconsistent answers", bad);
    U(DeleteObject)(m); U(DeleteObject)(c);
}
