/* Shared helpers for the x_gdi_brush_palette_dib_neg area: parameter-validation matrices and consistency checks for the
 * thin brush / palette / DIB syscalls NtGdiCreateDIBBrush, NtGdiCreatePatternBrushInternal, NtGdiGetNearestPaletteIndex,
 * NtGdiGetSystemPaletteUse, NtGdiColorCorrectPalette, NtGdiHT_Get8BPPFormatPalette, NtGdiHT_Get8BPPMaskPalette,
 * NtGdiStretchDIBitsInternal, NtGdiMonoBitmap and NtGdiGetGlyphIndicesWInternal.
 * Every expectation is what Windows 10 22H2 returns. Machine-varying values (handles, display depth, font data) are never
 * asserted, only relations over them. Route (a) is the win32u Nt* stub called directly, route (b) the gdi32 export.
 * A matrix row carries the expected "non-zero result" flag and the expected last error (a negative error expectation
 * means the error is not checked). */
#ifndef XB_H
#define XB_H
#include "t.h"
#include "k.h"
#include <wingdi.h>
#include <stdlib.h>

UDECL(HANDLE, NtGdiCreateDIBBrush, (const void *pbmi, ULONG iUsage, ULONG cj, BOOL b8x8, BOOL bPen, const void *pClient));
UDECL(HANDLE, NtGdiCreatePatternBrushInternal, (HANDLE hbm, BOOL b8x8, BOOL bPen));
UDECL(ULONG,  NtGdiGetNearestPaletteIndex, (HANDLE h, ULONG cr));
UDECL(ULONG,  NtGdiGetSystemPaletteUse, (HDC hdc));
UDECL(ULONG,  NtGdiColorCorrectPalette, (HDC, HPALETTE, ULONG first, ULONG n, PALETTEENTRY *, int set));
typedef ULONG_PTR (WINAPI *xb_fn14)(ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR,
                                   ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR);
UDECL(int,    NtGdiStretchDIBitsInternal, (HDC, int, int, int, int, int, int, int, int, const void *, const BITMAPINFO *,
                                           DWORD, DWORD, UINT, UINT, HANDLE));
UDECL(int,    NtGdiMonoBitmap, (HBITMAP));
UDECL(int,    NtGdiGetGlyphIndicesWInternal, (HDC, const WCHAR *, int, WORD *, DWORD, DWORD));
UDECL(HANDLE, NtGdiCreatePaletteInternal, (const void *plp, ULONG cEntries));

#define XT(n) TEST(x_gdi_brush_palette_dib_neg_##n)

/* Compare one matrix row. nz = the result was non-zero. */
static inline void xb_row(const char *name, int nz, DWORD err, int expnz, int experr) {
    CHECKF(nz == expnz, "%s: result nonzero=%d want %d", name, nz, expnz);
    if (experr >= 0) CHECKF(err == (DWORD)experr, "%s: error %lu want %d", name, (unsigned long)err, experr);
}

/* Compare one numeric result and the last error. */
static inline void xb_val(const char *name, unsigned long long v, DWORD err, unsigned long long exp, int experr) {
    CHECKF(v == exp, "%s: value %llu want %llu", name, v, exp);
    if (experr >= 0) CHECKF(err == (DWORD)experr, "%s: error %lu want %d", name, (unsigned long)err, experr);
}

/* Row selection for grouped matrix tests: pats is a '|' separated list of name prefixes; a pattern starting with '*' is a suffix. */
static inline int xb_match(const char *nm, const char *pats) {
    size_t nl = strlen(nm);
    while (*pats) {
        const char *e = strchr(pats, '|'); size_t pl = e ? (size_t)(e - pats) : strlen(pats);
        if (pats[0] == '*') { if (pl - 1 <= nl && !strncmp(nm + nl - (pl - 1), pats + 1, pl - 1)) return 1; }
        else if (pl <= nl && !strncmp(nm, pats, pl)) return 1;
        pats += pl; if (*pats == '|') pats++;
    }
    return 0;
}
#define XB_GROUP(testname, rows, runner, pats) \
    XT(testname) { int n_ = 0; for (unsigned i_ = 0; i_ < sizeof(rows) / sizeof((rows)[0]); i_++) if (xb_match((rows)[i_].nm, pats)) { runner(&(rows)[i_]); n_++; } \
                   CHECKF(n_ > 0, "no matrix rows in group " #testname); }

/* A buffer whose last byte is the last byte of a committed page, the next page being inaccessible: any read or write
 * past `n` bytes faults. */
struct xb_guard { BYTE *base; BYTE *p; size_t n; };
static inline int xb_guard_open(struct xb_guard *g, size_t n) {
    DWORD old;
    g->base = (BYTE *)VirtualAlloc(NULL, 0x2000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    g->n = n; g->p = NULL;
    if (!g->base || n > 0x1000) return 0;
    VirtualProtect(g->base + 0x1000, 0x1000, PAGE_NOACCESS, &old);
    g->p = g->base + 0x1000 - n;
    memset(g->p, 0, n);
    return 1;
}
static inline void xb_guard_close(struct xb_guard *g) { if (g->base) VirtualFree(g->base, 0, MEM_RELEASE); g->base = NULL; }

/* ---- packed DIB builder ---------------------------------------------------------------------------------------- */
struct xb_spec { DWORD size; int bpp; DWORD comp; DWORD clrused; LONG w, h; int planes; DWORD usage; };
static inline unsigned xb_stride(LONG w, int bpp) { unsigned long long b = (unsigned long long)(w < 0 ? -w : w) * (unsigned)bpp; return (unsigned)(((b + 31) / 32) * 4); }
/* Writes a header per spec, a palette and a bit area into buf (size cap), returns the used length. The bit area is the
 * size the header implies, capped to what fits in cap; a header that implies more than fits simply yields a short buffer. */
static inline size_t xb_build(BYTE *buf, size_t cap, const struct xb_spec *s) {
    memset(buf, 0, cap);
    DWORD hs = s->size;
    int core = (hs == 12);
    if (core) {
        WORD *w = (WORD *)buf; *(DWORD *)buf = 12; w[2] = (WORD)s->w; w[3] = (WORD)s->h; w[4] = (WORD)s->planes; w[5] = (WORD)s->bpp;
    } else {
        BITMAPINFOHEADER *h = (BITMAPINFOHEADER *)buf;
        h->biSize = hs; h->biWidth = s->w; h->biHeight = s->h; h->biPlanes = (WORD)s->planes; h->biBitCount = (WORD)s->bpp;
        h->biCompression = s->comp; h->biClrUsed = s->clrused;
    }
    size_t off = hs < 12 ? 12 : hs;
    if (!core && s->comp == BI_BITFIELDS && hs == 40) {
        DWORD *m = (DWORD *)(buf + off);
        if (s->bpp == 16) { m[0] = 0xF800; m[1] = 0x07E0; m[2] = 0x001F; } else { m[0] = 0xFF0000; m[1] = 0xFF00; m[2] = 0xFF; }
        off += 12;
    }
    unsigned nc = s->clrused ? s->clrused : (s->bpp > 0 && s->bpp <= 8 ? (1u << s->bpp) : 0);
    if (nc > 256) nc = 256;
    if (s->usage == DIB_PAL_COLORS) { WORD *ix = (WORD *)(buf + off); for (unsigned i = 0; i < nc; i++) ix[i] = (WORD)(i & 0xff); off += nc * 2; }
    else {
        unsigned es = core ? 3 : 4;
        for (unsigned i = 0; i < nc; i++) { BYTE *e = buf + off + i * es; e[0] = (BYTE)(i * 15 + 3); e[1] = (BYTE)(255 - i * 12); e[2] = (BYTE)(i * 7 + 100); }
        off += nc * es;
    }
    unsigned long long bits = (unsigned long long)xb_stride(s->w, s->bpp) * (unsigned long long)(s->h < 0 ? -s->h : s->h);
    size_t room = cap > off ? cap - off : 0;
    size_t nb = bits > room ? room : (size_t)bits;
    for (size_t i = 0; i < nb; i++) buf[off + i] = (BYTE)(i * 37 + 11);
    return off + nb;
}

/* The standard 8x8 test patterns: pixel (x, y) in top-down order, per depth. */
static inline void xb_pix(int bpp, int x, int y, BYTE *r, BYTE *g, BYTE *b) {
    if (bpp >= 24) { *b = (BYTE)(10 + x * 20); *g = (BYTE)(10 + y * 20); *r = (BYTE)(200 - x * 10 - y * 10); return; }
    unsigned nc = 1u << bpp; unsigned i = (unsigned)(x + 2 * y) % nc;
    *b = (BYTE)(i * 15 + 3); *g = (BYTE)(255 - i * 12); *r = (BYTE)(i * 7 + 100);
}
/* Packed bottom-up 8x8 DIB of the given depth (1, 4, 8, 24, 32) with the palette matching xb_pix. Returns the length. */
static inline size_t xb_dib8(BYTE *out, int bpp) {
    BITMAPINFOHEADER *h = (BITMAPINFOHEADER *)out;
    unsigned nc = bpp <= 8 ? (1u << bpp) : 0, st = xb_stride(8, bpp);
    memset(out, 0, 40 + nc * 4 + st * 8);
    h->biSize = 40; h->biWidth = 8; h->biHeight = 8; h->biPlanes = 1; h->biBitCount = (WORD)bpp; h->biCompression = BI_RGB; h->biClrUsed = nc;
    RGBQUAD *pal = (RGBQUAD *)(out + 40);
    for (unsigned i = 0; i < nc; i++) { pal[i].rgbBlue = (BYTE)(i * 15 + 3); pal[i].rgbGreen = (BYTE)(255 - i * 12); pal[i].rgbRed = (BYTE)(i * 7 + 100); }
    BYTE *bits = out + 40 + nc * 4;
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {
        BYTE *row = bits + (7 - y) * st; BYTE r, g, b; xb_pix(bpp, x, y, &r, &g, &b);
        if (bpp == 32) { row[x * 4] = b; row[x * 4 + 1] = g; row[x * 4 + 2] = r; }
        else if (bpp == 24) { row[x * 3] = b; row[x * 3 + 1] = g; row[x * 3 + 2] = r; }
        else { unsigned idx = (unsigned)(x + 2 * y) % nc;
               if (bpp == 8) row[x] = (BYTE)idx; else if (bpp == 4) row[x / 2] |= (BYTE)((x & 1) ? idx : idx << 4); else row[x / 8] |= (BYTE)(idx << (7 - (x & 7))); }
    }
    return 40 + nc * 4 + st * 8;
}

/* A 32bpp top-down DIB section selected into a memory DC; pixels readable after GdiFlush. */
struct xb_surf { HDC dc; HBITMAP bm, old; DWORD *px; int w, h; };
static inline int xb_surf_open(struct xb_surf *s, int w, int h) {
    BITMAPINFO bi; memset(&bi, 0, sizeof bi); memset(s, 0, sizeof *s);
    if (!U(CreateCompatibleDC) || !U(CreateDIBSection) || !U(SelectObject)) return 0;
    bi.bmiHeader.biSize = 40; bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    s->w = w; s->h = h;
    s->dc = U(CreateCompatibleDC)(NULL);
    s->bm = U(CreateDIBSection)(s->dc, &bi, DIB_RGB_COLORS, (void **)&s->px, NULL, 0);
    if (!s->dc || !s->bm || !s->px) return 0;
    s->old = (HBITMAP)U(SelectObject)(s->dc, s->bm);
    for (int i = 0; i < w * h; i++) s->px[i] = 0x00FFFFFFu;
    return 1;
}
static inline void xb_surf_close(struct xb_surf *s) {
    if (s->dc && s->old) U(SelectObject)(s->dc, s->old);
    if (s->bm) U(DeleteObject)(s->bm);
    if (s->dc) U(DeleteDC)(s->dc);
    memset(s, 0, sizeof *s);
}
static inline DWORD xb_px(const struct xb_surf *s, int x, int y) { U(GdiFlush)(); return s->px[y * s->w + x] & 0xFFFFFFu; }

/* The handle set for wrong-type matrices. */
enum { XH_NULL, XH_BITMAP, XH_PEN, XH_BRUSH, XH_REGION, XH_PALETTE, XH_STOCKPEN, XH_JUNK, XH_MINUS1, XH_HWND, XH_FONT, XH_DC, XH_COUNT };
static const char *const xh_nm[XH_COUNT] = { "null", "bitmap", "pen", "brush", "region", "palette", "stockpen", "junk", "minus1", "hwnd", "font", "dc" };
struct xhs { HBITMAP bm; HPEN pn; HBRUSH br; HRGN rg; HPALETTE pal; HFONT ft; HDC dc; HANDLE h[XH_COUNT]; };
static inline int xhs_open(struct xhs *x) {
    memset(x, 0, sizeof *x);
    if (!U(CreateBitmap) || !U(CreatePen) || !U(CreateSolidBrush) || !U(CreateRectRgn) || !U(CreatePalette) || !U(CreateFontW) ||
        !U(GetStockObject) || !U(GetDesktopWindow) || !U(CreateCompatibleDC)) return 0;
    x->bm = U(CreateBitmap)(4, 4, 1, 1, NULL); x->pn = U(CreatePen)(PS_SOLID, 1, 0); x->br = U(CreateSolidBrush)(0x123456);
    x->rg = U(CreateRectRgn)(0, 0, 3, 3);
    LOGPALETTE lp = { 0x300, 1, { { 1, 2, 3, 0 } } }; x->pal = U(CreatePalette)(&lp);
    x->ft = U(CreateFontW)(10, 0, 0, 0, 400, 0, 0, 0, 0, 0, 0, 0, 0, L"Arial");
    x->dc = U(CreateCompatibleDC)(NULL);
    x->h[XH_NULL] = NULL; x->h[XH_BITMAP] = x->bm; x->h[XH_PEN] = x->pn; x->h[XH_BRUSH] = x->br; x->h[XH_REGION] = x->rg; x->h[XH_PALETTE] = x->pal;
    x->h[XH_STOCKPEN] = U(GetStockObject)(BLACK_PEN); x->h[XH_JUNK] = (HANDLE)(ULONG_PTR)0x12345678; x->h[XH_MINUS1] = (HANDLE)(LONG_PTR)-1;
    x->h[XH_HWND] = (HANDLE)U(GetDesktopWindow)(); x->h[XH_FONT] = x->ft; x->h[XH_DC] = x->dc;
    return x->bm && x->pn && x->br && x->rg && x->pal && x->ft && x->dc;
}
static inline void xhs_close(struct xhs *x) {
    HGDIOBJ o[] = { x->bm, x->pn, x->br, x->rg, x->pal, x->ft };
    for (unsigned i = 0; i < 6; i++) if (o[i]) U(DeleteObject)(o[i]);
    if (x->dc) U(DeleteDC)(x->dc);
}
#define XHS(x) struct xhs x; if (!xhs_open(&x)) { xhs_close(&x); t_skip("cannot create the handle set"); return; }
#endif
