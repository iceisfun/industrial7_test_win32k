/* gdi_bitmaps_dib helpers: Nt* syscall declarations, bitmap/DIB builders and local-state readers.
 *
 * Syscall signatures (derived from win32kfull analysis and the gdi32full callers):
 *   NtGdiCreateBitmap(cx, cy, cPlanes, cBPP, pBits)                          win32kfull 0xfcd00
 *   NtGdiCreateCompatibleBitmap(hdc, cx, cy)                                 win32kfull 0x106fc0
 *   NtGdiCreateDIBSection(hdc, hSection, dwOffset, pbmi, usage, cjHeader,
 *                         fl, dwColorSpace, ppvBits)                         win32kfull 0x95c30
 *   NtGdiGetBitmapBits(hbm, cjBuffer, pBuffer) -> bytes                      win32kfull 0xf1ab0
 *   NtGdiSetBitmapBits(hbm, cjBuffer, pBuffer) -> bytes                      win32kfull 0xf1e10
 *   NtGdiExtGetObjectW(h, cb, pv)                                            win32kfull 0x99e80
 *   NtGdiSetBitmapDimension(hbm, cx, cy, pOldSize) / NtGdiGetBitmapDimension(hbm, pSize)
 *   NtGdiGetDIBitsInternal(hdc, hbm, start, cLines, pBits, pbmi, usage, cjMaxBits, cjMaxInfo)
 *   NtGdiSetDIBitsToDeviceInternal(hdc, xD, yD, cx, cy, xS, yS, start, cLines, pBits, pbmi, usage,
 *                                  cjMaxBits, cjMaxInfo, bTransform, hcm)    win32kfull 0x95a10
 *   NtGdiCreateDIBitmapInternal(hdc, cx, cy, fInit, pjInit, pbmi, usage, cjMaxInitInfo, cjMaxBits, f, hcm)
 *   NtGdiGetDCforBitmap(hbm) -> HDC the bitmap is selected into, or NULL
 * Bitmap format ids used by the kernel (galBitsPerPixel): 1=1bpp 2=4bpp 3=8bpp 4=16bpp 5=24bpp 6=32bpp. */
#ifndef BD_H
#define BD_H
#include "t.h"
#include "k.h"
#include <wingdi.h>

UDECL(HBITMAP, NtGdiCreateBitmap, (int cx, int cy, UINT planes, UINT bpp, const void *bits));
UDECL(HBITMAP, NtGdiCreateCompatibleBitmap, (HDC hdc, int cx, int cy));
UDECL(HBITMAP, NtGdiCreateDIBSection, (HDC hdc, HANDLE hSection, DWORD offset, const BITMAPINFO *bmi, DWORD usage,
                                       UINT cjHeader, DWORD fl, ULONG_PTR colorSpace, void **ppvBits));
UDECL(LONG, NtGdiGetBitmapBits, (HBITMAP hbm, ULONG cb, void *buf));
UDECL(LONG, NtGdiSetBitmapBits, (HBITMAP hbm, ULONG cb, const void *buf));
UDECL(int, NtGdiExtGetObjectW, (HANDLE h, int cb, void *pv));
UDECL(BOOL, NtGdiSetBitmapDimension, (HBITMAP hbm, int cx, int cy, SIZE *old));
UDECL(BOOL, NtGdiGetBitmapDimension, (HBITMAP hbm, SIZE *sz));
UDECL(int, NtGdiGetDIBitsInternal, (HDC hdc, HBITMAP hbm, UINT start, UINT lines, void *bits, BITMAPINFO *bmi, UINT usage,
                                    UINT cjMaxBits, UINT cjMaxInfo));
UDECL(int, NtGdiSetDIBitsToDeviceInternal, (HDC hdc, int xD, int yD, DWORD cx, DWORD cy, int xS, int yS, DWORD start,
                                            DWORD lines, const void *bits, const BITMAPINFO *bmi, UINT usage,
                                            UINT cjMaxBits, UINT cjMaxInfo, BOOL bTransform, HANDLE hcm));
UDECL(HBITMAP, NtGdiCreateDIBitmapInternal, (HDC hdc, int cx, int cy, DWORD fInit, const void *init, const BITMAPINFO *bmi,
                                             UINT usage, UINT cjMaxInfo, UINT cjMaxBits, DWORD f, HANDLE hcm));
UDECL(HDC, NtGdiGetDCforBitmap, (HBITMAP hbm));
UDECL(HGDIOBJ, NtGdiSelectBitmap, (HDC hdc, HBITMAP hbm));
UDECL(BOOL, NtGdiDeleteObjectApp, (HANDLE h));
UDECL(BOOL, NtGdiFlush, (void));

#define BD_ERR_INVALID_HANDLE 6
#define BD_ERR_INVALID_PARAM  87

/* ---- handle and cell decoding (PEB->GdiSharedHandleTable, 0x18-byte cells) -------------------------------------------- */
struct bd_cell {
    unsigned long long q0;
    unsigned int       owner;
    unsigned short     upper;
    unsigned char      objt;
    unsigned char      flags;
    unsigned long long puser;
};
#define BD_IDX(h)   ((unsigned)((ULONG_PTR)(h) & 0xffff))
#define BD_TYPE7(h) ((unsigned)(((ULONG_PTR)(h) >> 16) & 0x7f))
#define BD_STOCK(h) (((ULONG_PTR)(h) & 0x00800000u) != 0)

static int bd_cell_read(HGDIOBJ h, struct bd_cell *c) {
    void *base = NULL;
    if (!k_try_read_ptr((char *)k_peb() + PEB_GdiSharedHandleTable, &base) || !base) return 0;
    return k_try_read((char *)base + (ULONG_PTR)BD_IDX(h) * GDI_CELL_SIZE, c, sizeof *c);
}

/* ---- DIB construction -------------------------------------------------------------------------------------------------- */
struct bd_bmi {
    BITMAPINFOHEADER h;
    RGBQUAD c[256];
};
static void bd_bmi_init(struct bd_bmi *b, int w, int h, int bpp) {
    memset(b, 0, sizeof *b);
    b->h.biSize = sizeof b->h; b->h.biWidth = w; b->h.biHeight = h; b->h.biPlanes = 1; b->h.biBitCount = (WORD)bpp;
    b->h.biCompression = BI_RGB;
}
static void bd_bmi_masks(struct bd_bmi *b, DWORD r, DWORD g, DWORD bl) {
    b->h.biCompression = BI_BITFIELDS;
    ((DWORD *)b->c)[0] = r; ((DWORD *)b->c)[1] = g; ((DWORD *)b->c)[2] = bl;
}
static void bd_bmi_gray(struct bd_bmi *b, int n) {
    for (int i = 0; i < n && i < 256; i++) {
        int v = n > 1 ? i * 255 / (n - 1) : 0;
        b->c[i].rgbRed = b->c[i].rgbGreen = b->c[i].rgbBlue = (BYTE)v; b->c[i].rgbReserved = 0;
    }
}
/* Kernel scanline stride of a DIB: ((w * bpp + 31) >> 3) & ~3. */
static unsigned bd_stride(int w, int bpp) { return (unsigned)(((unsigned)w * bpp + 31) >> 3) & ~3u; }
/* Stride of a DDB (CreateBitmap family): WORD aligned, (w * bpp + 15) >> 3 & ~1. */
static unsigned bd_ddb_stride(int w, int bpp) { return (unsigned)(((unsigned)w * bpp + 15) >> 3) & ~1u; }
static int bd_abs(int v) { return v < 0 ? -v : v; }

/* the bitmap-info size check: the cjHeader the syscall demands (header + colour table / masks). */
static UINT bd_hdr_size(const BITMAPINFO *bmi, UINT usage) {
    const BITMAPINFOHEADER *h = &bmi->bmiHeader;
    unsigned n, used = h->biClrUsed, ent = 4, tot;
    if (h->biCompression == BI_BITFIELDS) {
        n = 3; used = (h->biSize > 0x28) ? 0 : 3; if (h->biSize > 0x28) n = 0;
        return (h->biSize + 3 + used * 4) & ~3u;
    }
    n = h->biBitCount == 1 ? 2 : h->biBitCount == 4 ? 16 : h->biBitCount == 8 ? 256 : 0;
    tot = (used != 0 && used < n) ? used : n;
    if (usage == 1) ent = 2; else if (usage == 2) ent = 0;
    return (h->biSize + 3 + tot * ent) & ~3u;
}

/* gdi32 path */
static HBITMAP bd_dib(int w, int h, int bpp, void **bits) {
    struct bd_bmi b; void *p = NULL;
    if (!U(CreateDIBSection)) return NULL;
    bd_bmi_init(&b, w, h, bpp);
    if (bpp <= 8) bd_bmi_gray(&b, 1 << bpp);
    HBITMAP hb = U(CreateDIBSection)(NULL, (BITMAPINFO *)&b, DIB_RGB_COLORS, &p, NULL, 0);
    if (bits) *bits = p;
    return hb;
}
/* Direct syscall path; hdr computed like the real caller (gdi32full!CreateDIBSectionInternal). */
static HBITMAP bd_nt_dib(HDC dc, HANDLE sec, DWORD off, const struct bd_bmi *b, UINT usage, void **bits) {
    void *p = NULL;
    if (!UN(NtGdiCreateDIBSection)) return NULL;
    HBITMAP hb = UN(NtGdiCreateDIBSection)(dc, sec, off, (const BITMAPINFO *)b, usage, bd_hdr_size((const BITMAPINFO *)b, usage), 0, 0, &p);
    if (bits) *bits = p;
    return hb;
}
static HDC bd_screen_dc(void) { return U(CreateDCW) ? U(CreateDCW)(L"DISPLAY", NULL, NULL, NULL) : NULL; }
static HDC bd_mem_dc(void) { return U(CreateCompatibleDC) ? U(CreateCompatibleDC)(NULL) : NULL; }

static int bd_getobj(HBITMAP hb, BITMAP *bm) { memset(bm, 0, sizeof *bm); return U(GetObjectW)(hb, sizeof *bm, bm); }
static int bd_getdib(HBITMAP hb, DIBSECTION *ds) { memset(ds, 0xcc, sizeof *ds); return U(GetObjectW)(hb, sizeof *ds, ds); }

/* Memory state of a user address (VirtualQuery never faults). */
static int bd_query(const void *p, MEMORY_BASIC_INFORMATION *mbi) {
    memset(mbi, 0, sizeof *mbi);
    return VirtualQuery(p, mbi, sizeof *mbi) != 0;
}

#define BD_NEED() do { if (!U(CreateBitmap) || !U(GetObjectW) || !U(DeleteObject) || !U(CreateDIBSection)) { t_skip("gdi32 entry points missing"); return; } } while (0)
/* Wine's win32u exports stubs for syscalls it never implemented; calling one aborts the process. */
static int bd_wine_unimpl(const char *name) {
    static const char *const bad[] = { "NtGdiGetDCforBitmap", NULL };
    if (!t_is_wine()) return 0;
    for (int i = 0; bad[i]; i++) if (!strcmp(bad[i], name)) return 1;
    return 0;
}
#define BD_NEED_NT(fn) do { if (!UN(fn)) { t_skip("missing export: " #fn); return; } \
    if (bd_wine_unimpl(#fn)) { t_skip(#fn " is not implemented by this emulator"); return; } } while (0)
/* Bitmap bits are plain user memory, but GDI may batch drawing; flush before reading. */
static void bd_flush(void) { if (U(GdiFlush)) U(GdiFlush)(); }

#endif
