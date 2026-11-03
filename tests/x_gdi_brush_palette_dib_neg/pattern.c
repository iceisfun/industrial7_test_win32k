/* NtGdiCreatePatternBrushInternal and CreatePatternBrush: the bitmap handle matrix (every wrong handle type, a deleted
 * bitmap, bitmaps of several depths and sizes, a bitmap selected into a DC), the 8x8 and pen flags, the LOGBRUSH
 * readback of the result and the rule that the brush owns a copy of the pattern (deleting the source bitmap leaves the
 * brush working). */
#include "xb.h"

enum { K_MONO8, K_COLOR8, K_DIB24, K_DIB32, K_DIB1, K_DIB8, K_MONO16, K_COLOR16, K_DIB24_16, K_MONO1, K_MONO9, K_SELECTED,
       K_DELETED, K_BIG, K_DIB24_TD, K_MEMDC_DEFAULT, K_NULL, K_PEN, K_BRUSH, K_RGN, K_PAL, K_STOCKPEN, K_JUNK, K_MINUS1, K_HWND, K_FONT, K_DC, K_STOCKBRUSH };

static HBITMAP mkdib(int w, int h, int bpp, void **bits) {
    BYTE bi[40 + 256 * 4]; BITMAPINFO *b = (BITMAPINFO *)bi; memset(bi, 0, sizeof bi);
    b->bmiHeader.biSize = 40; b->bmiHeader.biWidth = w; b->bmiHeader.biHeight = h; b->bmiHeader.biPlanes = 1; b->bmiHeader.biBitCount = (WORD)bpp;
    if (bpp == 1) { b->bmiColors[1].rgbRed = b->bmiColors[1].rgbGreen = b->bmiColors[1].rgbBlue = 255; }
    HDC dc = U(CreateCompatibleDC)(NULL); void *p = NULL;
    HBITMAP bm = U(CreateDIBSection)(dc, b, DIB_RGB_COLORS, bits ? bits : &p, NULL, 0);
    U(DeleteDC)(dc);
    return bm;
}

/* Builds the bitmap for kind k; *own tells the caller to delete it; *dc is a DC the kind needs kept alive. */
static HANDLE mk(int k, int *own, HDC *dc) {
    *own = 1; *dc = NULL;
    HDC scr = U(GetDC)(NULL); HANDLE r = NULL;
    switch (k) {
    case K_MONO8: r = U(CreateBitmap)(8, 8, 1, 1, NULL); break;
    case K_COLOR8: r = U(CreateCompatibleBitmap)(scr, 8, 8); break;
    case K_DIB24: r = mkdib(8, 8, 24, NULL); break;
    case K_DIB32: r = mkdib(8, 8, 32, NULL); break;
    case K_DIB1: r = mkdib(8, 8, 1, NULL); break;
    case K_DIB8: r = mkdib(8, 8, 8, NULL); break;
    case K_MONO16: r = U(CreateBitmap)(16, 16, 1, 1, NULL); break;
    case K_COLOR16: r = U(CreateCompatibleBitmap)(scr, 16, 16); break;
    case K_DIB24_16: r = mkdib(16, 16, 24, NULL); break;
    case K_MONO1: r = U(CreateBitmap)(1, 1, 1, 1, NULL); break;
    case K_MONO9: r = U(CreateBitmap)(9, 9, 1, 1, NULL); break;
    case K_BIG: r = U(CreateBitmap)(200, 200, 1, 1, NULL); break;
    case K_DIB24_TD: r = mkdib(8, -8, 24, NULL); break;
    case K_SELECTED: r = U(CreateBitmap)(8, 8, 1, 1, NULL); *dc = U(CreateCompatibleDC)(NULL); if (r && *dc) U(SelectObject)(*dc, r); break;
    case K_DELETED: r = U(CreateBitmap)(8, 8, 1, 1, NULL); if (r) U(DeleteObject)(r); break;
    case K_MEMDC_DEFAULT: *own = 0; *dc = U(CreateCompatibleDC)(NULL); r = *dc ? U(GetCurrentObject)(*dc, OBJ_BITMAP) : NULL; break;
    case K_NULL: *own = 0; r = NULL; break;
    case K_PEN: r = U(CreatePen)(PS_SOLID, 1, 0); break;
    case K_BRUSH: r = U(CreateSolidBrush)(0x123456); break;
    case K_RGN: r = U(CreateRectRgn)(0, 0, 3, 3); break;
    case K_PAL: { LOGPALETTE lp = { 0x300, 1, { { 1, 2, 3, 0 } } }; r = U(CreatePalette)(&lp); break; }
    case K_STOCKPEN: *own = 0; r = U(GetStockObject)(BLACK_PEN); break;
    case K_STOCKBRUSH: *own = 0; r = U(GetStockObject)(WHITE_BRUSH); break;
    case K_JUNK: *own = 0; r = (HANDLE)(ULONG_PTR)0x12345678; break;
    case K_MINUS1: *own = 0; r = (HANDLE)(LONG_PTR)-1; break;
    case K_HWND: *own = 0; r = (HANDLE)U(GetDesktopWindow)(); break;
    case K_FONT: r = U(CreateFontW)(10, 0, 0, 0, 400, 0, 0, 0, 0, 0, 0, 0, 0, L"Arial"); break;
    case K_DC: *own = 0; *dc = U(CreateCompatibleDC)(NULL); r = *dc; break;
    }
    U(ReleaseDC)(NULL, scr);
    return r;
}

#define PATROWS(X) \
 X(mono8,        K_MONO8, 0, 0, 1,0) \
 X(mono8_8x8,    K_MONO8, 1, 0, 1,0) \
 X(mono8_pen,    K_MONO8, 0, 1, 1,0) \
 X(mono8_8x8_pen, K_MONO8, 1, 1, 1,0) \
 X(color8,       K_COLOR8, 0, 0, 1,0) \
 X(color8_8x8,   K_COLOR8, 1, 0, 1,0) \
 X(dib24,        K_DIB24, 0, 0, 1,0) \
 X(dib24_8x8,    K_DIB24, 1, 0, 1,0) \
 X(dib24_pen,    K_DIB24, 0, 1, 1,0) \
 X(dib32,        K_DIB32, 0, 0, 1,0) \
 X(dib32_8x8,    K_DIB32, 1, 0, 1,0) \
 X(dib1,         K_DIB1, 0, 0, 1,0) \
 X(dib8,         K_DIB8, 0, 0, 1,0) \
 X(dib8_8x8,     K_DIB8, 1, 0, 1,0) \
 X(mono16,       K_MONO16, 0, 0, 1,0) \
 X(mono16_8x8,   K_MONO16, 1, 0, 1,0) \
 X(color16,      K_COLOR16, 0, 0, 1,0) \
 X(color16_8x8,  K_COLOR16, 1, 0, 1,0) \
 X(dib24_16,     K_DIB24_16, 0, 0, 1,0) \
 X(dib24_16_8x8, K_DIB24_16, 1, 0, 1,0) \
 X(mono1,        K_MONO1, 0, 0, 1,0) \
 X(mono1_8x8,    K_MONO1, 1, 0, 1,0) \
 X(mono9,        K_MONO9, 0, 0, 1,0) \
 X(mono9_8x8,    K_MONO9, 1, 0, 1,0) \
 X(big200,       K_BIG, 0, 0, 1,0) \
 X(big200_8x8,   K_BIG, 1, 0, 1,0) \
 X(dib24_topdown, K_DIB24_TD, 0, 0, 1,0) \
 X(selected,     K_SELECTED, 0, 0, 1,0) \
 X(selected_8x8, K_SELECTED, 1, 0, 1,0) \
 X(memdc_default, K_MEMDC_DEFAULT, 0, 0, 1,0) \
 X(deleted,      K_DELETED, 0, 0, 0,6) \
 X(null,         K_NULL, 0, 0, 0,6) \
 X(null_8x8,     K_NULL, 1, 0, 0,6) \
 X(pen,          K_PEN, 0, 0, 0,6) \
 X(brush,        K_BRUSH, 0, 0, 0,6) \
 X(region,       K_RGN, 0, 0, 0,6) \
 X(palette,      K_PAL, 0, 0, 0,6) \
 X(stock_pen,    K_STOCKPEN, 0, 0, 0,6) \
 X(stock_brush,  K_STOCKBRUSH, 0, 0, 0,6) \
 X(junk,         K_JUNK, 0, 0, 0,6) \
 X(minus1,       K_MINUS1, 0, 0, 0,6) \
 X(hwnd,         K_HWND, 0, 0, 0,6) \
 X(font,         K_FONT, 0, 0, 0,6) \
 X(dc,           K_DC, 0, 0, 0,6)

struct patrow { const char *nm; int kind, b8, pen, nz, err; };
#define PS_(nm, k, b8, pn, enz, eerr) { #nm, k, b8, pn, enz, eerr },
static const struct patrow patrows[] = { PATROWS(PS_) };

static void pat_run(const struct patrow *r) {
    REQUIRE(UN(NtGdiCreatePatternBrushInternal)); REQUIRE(U(CreatePatternBrush)); REQUIRE(U(CreateCompatibleDC));
    int own; HDC dc; HANDLE bm = mk(r->kind, &own, &dc);
    SetLastError(0);
    HANDLE h = UN(NtGdiCreatePatternBrushInternal)(bm, r->b8, r->pen);
    DWORD e = GetLastError();
    xb_row(r->nm, h != NULL, e, r->nz, r->err);
    if (h) {
        CHECKF(U(GetObjectType)(h) == OBJ_BRUSH, "%s: result is not a brush", r->nm);
        CHECKF(U(DeleteObject)(h), "%s: delete", r->nm);
    }
    /* the gdi32 export takes the same handle, without the flags */
    char nm2[80]; wsprintfA(nm2, "%s/gdi32", r->nm);
    SetLastError(0);
    HBRUSH h2 = U(CreatePatternBrush)((HBITMAP)bm);
    DWORD e2 = GetLastError();
    xb_row(nm2, h2 != NULL, e2, r->nz, r->err);
    if (h2) U(DeleteObject)(h2);
    if (dc) U(DeleteDC)(dc);
    if (own && bm && r->kind != K_DELETED) U(DeleteObject)(bm);
}
XB_GROUP(pattern_ddb_sources, patrows, pat_run, "mono|color|big|selected|memdc")
XB_GROUP(pattern_dib_section_sources, patrows, pat_run, "dib")
XB_GROUP(pattern_unusable_bitmap, patrows, pat_run, "deleted|null")
XB_GROUP(pattern_wrong_handle_type, patrows, pat_run, "pen|brush|region|palette|stock_pen|stock_brush|junk|minus1|hwnd|font|dc")
