/* Shared fixtures for the gdi_blit_raster area: 32bpp top-down DIB sections whose bits we read back
 * directly after GdiFlush, and typed pointers for the win32u NtGdi* raster syscalls (no SDK header
 * declares them; argument lists come from the gdi32full callers: BitBlt/StretchBlt/PatBlt/MaskBlt/
 * PlgBlt/GdiAlphaBlend/GdiTransparentBlt/GdiGradientFill/SetPixel/GetPixel/ExtFloodFill and the region fills). */
#ifndef BR_H
#define BR_H
#include "t.h"
#include "k.h"

typedef BOOL     (WINAPI *NtGdiBitBlt_pfn)(HDC, int, int, int, int, HDC, int, int, DWORD rop, DWORD crBack, DWORD fl);
typedef BOOL     (WINAPI *NtGdiStretchBlt_pfn)(HDC, int, int, int, int, HDC, int, int, int, int, DWORD rop, DWORD crBack);
typedef BOOL     (WINAPI *NtGdiPatBlt_pfn)(HDC, int, int, int, int, DWORD rop);
typedef BOOL     (WINAPI *NtGdiMaskBlt_pfn)(HDC, int, int, int, int, HDC, int, int, HBITMAP, int, int, DWORD rop, DWORD crBack);
typedef BOOL     (WINAPI *NtGdiPlgBlt_pfn)(HDC, const POINT *, HDC, int, int, int, int, HBITMAP, int, int, DWORD crBack);
typedef BOOL     (WINAPI *NtGdiAlphaBlend_pfn)(HDC, int, int, int, int, HDC, int, int, int, int, DWORD blend, DWORD fl);
typedef BOOL     (WINAPI *NtGdiTransparentBlt_pfn)(HDC, int, int, int, int, HDC, int, int, int, int, DWORD key);
typedef BOOL     (WINAPI *NtGdiGradientFill_pfn)(HDC, TRIVERTEX *, ULONG, PVOID, ULONG, ULONG);
typedef DWORD    (WINAPI *NtGdiSetPixel_pfn)(HDC, int, int, DWORD);
typedef DWORD    (WINAPI *NtGdiGetPixel_pfn)(HDC, int, int);
typedef BOOL     (WINAPI *NtGdiExtFloodFill_pfn)(HDC, int, int, DWORD, UINT);
typedef BOOL     (WINAPI *NtGdiFillRgn_pfn)(HDC, HRGN, HBRUSH);
typedef BOOL     (WINAPI *NtGdiFrameRgn_pfn)(HDC, HRGN, HBRUSH, int, int);
typedef BOOL     (WINAPI *NtGdiInvertRgn_pfn)(HDC, HRGN);
typedef BOOL     (WINAPI *NtGdiSetBrushOrg_pfn)(HDC, int, int, POINT *);
typedef BOOL     (WINAPI *GdiAlphaBlend_pfn)(HDC, int, int, int, int, HDC, int, int, int, int, BLENDFUNCTION);
typedef BOOL     (WINAPI *GdiTransparentBlt_pfn)(HDC, int, int, int, int, HDC, int, int, int, int, UINT);
typedef BOOL     (WINAPI *GdiGradientFill_pfn)(HDC, PTRIVERTEX, ULONG, PVOID, ULONG, ULONG);

/* Resolve a win32u stub into p_<name>, skipping the test when it is missing. */
#define NT(name) name##_pfn p_##name = (name##_pfn)t_proc_in("win32u.dll", #name); REQUIRE(p_##name)
#define GDIF(name) name##_pfn p_##name = (name##_pfn)t_proc(#name); REQUIRE(p_##name)

/* msimg32 exports (AlphaBlend/TransparentBlt/GradientFill) forward to gdi32's Gdi* functions; msimg32 is not in the module list. */
#define MS(name) ((__typeof__(&name))t_proc_in("msimg32.dll", #name))

#define CLR_BAD 0xFFFFFFFFu
#define RGBPX(r, g, b) ((((DWORD)(r)) << 16) | (((DWORD)(g)) << 8) | (DWORD)(b))   /* DIB pixel (0x00RRGGBB) */
#define PX2CR(p) RGB(((p) >> 16) & 0xff, ((p) >> 8) & 0xff, (p) & 0xff)             /* DIB pixel -> COLORREF */
#define M24 0x00FFFFFFu

typedef struct { HDC dc; HBITMAP bmp, old; DWORD *px; int w, h; } BR;

/* 32bpp BI_RGB top-down DIB section selected into a fresh memory DC; bits zeroed. */
static BR br_make(int w, int h) {
    BR d; BITMAPINFO bi; void *bits = NULL;
    memset(&d, 0, sizeof d); memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    d.dc = U(CreateCompatibleDC)(NULL);
    if (!d.dc) return d;
    d.bmp = U(CreateDIBSection)(d.dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!d.bmp || !bits) { U(DeleteDC)(d.dc); d.dc = NULL; return d; }
    d.old = (HBITMAP)U(SelectObject)(d.dc, d.bmp);
    d.px = (DWORD *)bits; d.w = w; d.h = h;
    return d;
}
static void br_free(BR *d) {
    if (d->dc) { U(SelectObject)(d->dc, d->old); U(DeleteDC)(d->dc); }
    if (d->bmp) U(DeleteObject)(d->bmp);
    memset(d, 0, sizeof *d);
}
static void br_fill(BR *d, DWORD v) { for (int i = 0; i < d->w * d->h; i++) d->px[i] = v; }
static void br_flush(void) { U(GdiFlush)(); }
/* Pixel with the unused top byte masked off (the alpha byte of a BI_RGB 32bpp surface is not a color). */
static DWORD br_at(const BR *d, int x, int y) { return d->px[y * d->w + x] & M24; }
static void br_set(BR *d, int x, int y, DWORD v) { d->px[y * d->w + x] = v; }
/* Count pixels in rect [x0,x1) x [y0,y1) equal to v (masked). */
static int br_count(const BR *d, int x0, int y0, int x1, int y1, DWORD v) {
    int n = 0;
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) if (br_at(d, x, y) == (v & M24)) n++;
    return n;
}
static int br_all(const BR *d, int x0, int y0, int x1, int y1, DWORD v) {
    return br_count(d, x0, y0, x1, y1, v) == (x1 - x0) * (y1 - y0);
}
/* Deterministic, all-distinct source pattern: pixel (x,y) of a w-wide image. */
static DWORD br_pat(int x, int y) { return RGBPX(0x10 + x * 7, 0x20 + y * 5, 0x40 + x * 3 + y * 11); }
static void br_fill_pat(BR *d) { for (int y = 0; y < d->h; y++) for (int x = 0; x < d->w; x++) br_set(d, x, y, br_pat(x, y)); }

/* Ternary raster op, one bit at a time: bit i of the code byte is the result for
 * index i = P*4 + S*2 + D (SRCCOPY 0xCC: result == S). */
static DWORD rop3_apply(unsigned code, DWORD p, DWORD s, DWORD d) {
    DWORD r = 0;
    for (int b = 0; b < 24; b++) {
        int i = (((p >> b) & 1) << 2) | (((s >> b) & 1) << 1) | ((d >> b) & 1);
        r |= (DWORD)((code >> i) & 1) << b;
    }
    return r;
}
#define ROPCODE(c) (((DWORD)(c)) << 16)
#endif
