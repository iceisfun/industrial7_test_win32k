/* Shared helpers for the x_gdi_region_clip_neg area: return-code and error matrices for the thin
 * clip / region-creation / bounds / unrealize / window-extent / MoveTo syscalls. Every expectation is
 * what Windows 10 22H2 returns; machine-varying values (display sizes, handles) are never asserted.
 * Route (a): the win32u Nt* stub called directly. Route (b): the gdi32 export, with the batch drained
 * by GdiFlush before and after and compared against GdiSetBatchLimit(1), so batching cannot hide
 * the kernel entry. */
#ifndef XN_H
#define XN_H
#include "../gdi_regions_clip/rgn.h"

UDECL(UINT, NtGdiGetBoundsRect, (HDC, LPRECT, DWORD));
UDECL(BOOL, NtGdiUnrealizeObject, (HANDLE));
UDECL(BOOL, NtGdiScaleWindowExtEx, (HDC, int, int, int, int, LPSIZE));
UDECL(BOOL, NtGdiMoveTo, (HDC, int, int, LPPOINT));

#define XT(n) TEST(x_gdi_region_clip_neg_##n)

enum { ROUTE_NT = 0, ROUTE_GDI = 1, ROUTE_GDI_B1 = 2 };

/* Runs fn-less batch bracket: drain before, and after with limit restore. */
static inline DWORD xn_limit_set(int route) {
    DWORD old = 0;
    if (U(GdiFlush)) U(GdiFlush)();
    if (route == ROUTE_GDI_B1 && U(GdiSetBatchLimit)) old = U(GdiSetBatchLimit)(1);
    return old;
}
static inline void xn_limit_restore(int route, DWORD old) {
    if (U(GdiFlush)) U(GdiFlush)();
    if (route == ROUTE_GDI_B1 && U(GdiSetBatchLimit) && old) U(GdiSetBatchLimit)(old);
}

/* The handles used for the wrong-type matrices. */
struct xh { HDC wd, mdc; HBITMAP bm; HPEN pn; HBRUSH br; HRGN rg; HPALETTE pal; HFONT ft; HANDLE h[16]; };
enum { H_NULL, H_BITMAP, H_PEN, H_BRUSH, H_REGION, H_PALETTE, H_STOCKPEN, H_JUNK, H_MINUS1, H_HWND, H_FONT, H_STOCKFONT, H_HATCH, H_STOCKBRUSH, H_STOCKPAL, H_COUNT };
static const char *const xh_name[H_COUNT] = { "null", "bitmap", "pen", "brush", "region", "palette", "stockpen", "junk", "minus1", "hwnd", "font", "stockfont", "hatch", "stockbrush", "stockpal" };
static inline int xh_open(struct xh *x) {
    memset(x, 0, sizeof *x);
    if (!U(GetDC) || !U(CreateCompatibleDC) || !U(CreateCompatibleBitmap) || !U(CreatePen) || !U(CreateSolidBrush) || !U(CreateRectRgn) ||
        !U(CreatePalette) || !U(CreateFontW) || !U(GetStockObject) || !U(DeleteObject) || !U(GetDesktopWindow) || !U(CreateHatchBrush)) return 0;
    x->wd = U(GetDC)(NULL); x->mdc = U(CreateCompatibleDC)(NULL);
    x->bm = U(CreateCompatibleBitmap)(x->wd, 4, 4); x->pn = U(CreatePen)(PS_SOLID, 1, 0); x->br = U(CreateSolidBrush)(0x123456);
    x->rg = U(CreateRectRgn)(0, 0, 3, 3);
    LOGPALETTE lp = { 0x300, 1, { { 1, 2, 3, 0 } } }; x->pal = U(CreatePalette)(&lp);
    x->ft = U(CreateFontW)(10, 0, 0, 0, 400, 0, 0, 0, 0, 0, 0, 0, 0, L"Arial");
    x->h[H_NULL] = NULL; x->h[H_BITMAP] = x->bm; x->h[H_PEN] = x->pn; x->h[H_BRUSH] = x->br; x->h[H_REGION] = x->rg; x->h[H_PALETTE] = x->pal;
    x->h[H_STOCKPEN] = U(GetStockObject)(BLACK_PEN); x->h[H_JUNK] = (HANDLE)(ULONG_PTR)0x12345678; x->h[H_MINUS1] = (HANDLE)(LONG_PTR)-1;
    x->h[H_HWND] = (HANDLE)U(GetDesktopWindow)(); x->h[H_FONT] = x->ft; x->h[H_STOCKFONT] = U(GetStockObject)(SYSTEM_FONT);
    x->h[H_HATCH] = U(CreateHatchBrush)(HS_CROSS, 0); x->h[H_STOCKBRUSH] = U(GetStockObject)(WHITE_BRUSH); x->h[H_STOCKPAL] = U(GetStockObject)(DEFAULT_PALETTE);
    return x->wd && x->mdc && x->bm && x->pn && x->br && x->rg && x->pal && x->ft && x->h[H_HATCH];
}
static inline void xh_close(struct xh *x) {
    if (x->h[H_HATCH]) U(DeleteObject)(x->h[H_HATCH]);
    HGDIOBJ o[] = { x->bm, x->pn, x->br, x->rg, x->pal, x->ft };
    for (unsigned i = 0; i < 6; i++) { if (o[i]) U(DeleteObject)(o[i]); }
    if (x->mdc) { U(DeleteDC)(x->mdc); }
    if (x->wd) { U(ReleaseDC)(NULL, x->wd); }
}
#define XH(x) struct xh x; if (!xh_open(&x)) { xh_close(&x); t_skip("cannot create the handle set"); return; }
#endif
