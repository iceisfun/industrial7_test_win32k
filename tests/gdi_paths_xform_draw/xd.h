/* Shared helpers for the gdi_paths_xform_draw area (paths, transforms, mapping modes, vector drawing).
 *
 * Facts used here come from the Windows 10 19041 analysis:
 *  - win32kfull!NtGdiBeginPath / NtGdiEndPath / NtGdiAbortPath / NtGdiCloseFigure / NtGdiGetPath /
 *    NtGdiFillPath / NtGdiStrokePath / NtGdiStrokeAndFillPath / NtGdiWidenPath / NtGdiFlattenPath /
 *    NtGdiPathToRegion / NtGdiSelectClipPath (DC+0xf8 bit0 = "path under construction", DC+0xc8 = path);
 *  - win32kfull!NtGdiModifyWorldTransform / NtGdiGetTransform / NtGdiTransformPoints / NtGdiGetDCPoint,
 *    the kernel DC-origin query;
 *  - gdi32full!ModifyWorldTransform / SetGraphicsMode / SetMapMode / SetWindowExtEx / SetViewportExtEx /
 *    SetWindowOrgEx / OffsetViewportOrgEx / GetWindowExtEx / LPtoDP (user-mode DC_ATTR accessors).
 * DC_ATTR offsets (read from the gdi32full accessors above): +0x20 float matrix, +0x68 map mode, +0x6c flags
 * (bit0 = RTL layout), +0xd0 graphics mode, +0x130/+0x134/+0x138 window origin, +0x13c/+0x140 window extent,
 * +0x144/+0x148 viewport origin, +0x14c/+0x150 viewport extent, +0x154 dirty flags.
 */
#ifndef XD_H
#define XD_H
#include "t.h"
#include "k.h"
#include <wingdi.h>
#include <stdint.h>

#undef EXPECT_EQ
#define EXPECT_EQ(a, b) do { unsigned long long a_ = (unsigned long long)(ULONG_PTR)(a), b_ = (unsigned long long)(ULONG_PTR)(b); \
    t_check(a_ == b_, __FILE__, __LINE__, #a " == " #b, "got 0x%llx want 0x%llx", a_, b_); } while (0)
#undef EXPECT_NE
#define EXPECT_NE(a, b) do { unsigned long long a_ = (unsigned long long)(ULONG_PTR)(a), b_ = (unsigned long long)(ULONG_PTR)(b); \
    t_check(a_ != b_, __FILE__, __LINE__, #a " != " #b, "got 0x%llx", a_); } while (0)
/* Signed compare for values that can be negative (GetPath -1, GDI_ERROR-style ints). */
#define EXPECT_INT(a, b) do { long long a_ = (long long)(a), b_ = (long long)(b); \
    t_check(a_ == b_, __FILE__, __LINE__, #a " == " #b, "got %lld want %lld", a_, b_); } while (0)

/* Area-prefixed test names. */
#define XT(n) TEST(gdi_paths_xform_draw_##n)

#ifndef PT_CLOSEFIGURE
#define PT_CLOSEFIGURE 0x01
#define PT_LINETO      0x02
#define PT_BEZIERTO    0x04
#define PT_MOVETO      0x06
#endif
#ifndef MWT_SET
#define MWT_SET 4
#endif
#define ERR_INVALID_HANDLE 6
#define ERR_INVALID_PARAMETER 87
#define ERR_CAN_NOT_COMPLETE 1003

/* ---- 32bpp DIB fixture: a memory DC with a top-down 32bpp section selected. ------------------------------ */
typedef struct { HDC dc; HBITMAP bmp, old; uint32_t *bits; int w, h; } fx_t;

static inline void fx_sync(void) { U(GdiFlush)(); }

static inline void fx_clear(fx_t *f, uint32_t v) {
    int i;
    fx_sync();
    for (i = 0; i < f->w * f->h; i++) f->bits[i] = v;
}
static inline int fx_open(fx_t *f, int w, int h) {
    BITMAPINFO bi; void *bits = NULL;
    memset(f, 0, sizeof *f);
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    f->dc = U(CreateCompatibleDC)(NULL);
    if (!f->dc) return 0;
    f->bmp = U(CreateDIBSection)(f->dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!f->bmp || !bits) { U(DeleteDC)(f->dc); f->dc = NULL; return 0; }
    f->bits = (uint32_t *)bits; f->w = w; f->h = h;
    f->old = (HBITMAP)U(SelectObject)(f->dc, f->bmp);
    fx_clear(f, 0x00FFFFFF);
    return 1;
}
static inline void fx_close(fx_t *f) {
    if (f->dc) {
        if (f->old) U(SelectObject)(f->dc, f->old);
        U(DeleteDC)(f->dc);
    }
    if (f->bmp) U(DeleteObject)(f->bmp);
    memset(f, 0, sizeof *f);
}
#define FX(f, w, h) fx_t f; if (!fx_open(&f, w, h)) { t_skip("cannot create 32bpp DIB fixture"); return; }

static inline uint32_t fx_px(fx_t *f, int x, int y) {
    fx_sync();
    if (x < 0 || y < 0 || x >= f->w || y >= f->h) return 0xdeadbeef;
    return f->bits[y * f->w + x] & 0xffffff;
}
static inline int fx_count(fx_t *f, uint32_t color) {
    int i, n = 0;
    fx_sync();
    for (i = 0; i < f->w * f->h; i++) if ((f->bits[i] & 0xffffff) == color) n++;
    return n;
}
/* Counts pixels of `color` and returns their bounding box. */
static inline int fx_bbox(fx_t *f, uint32_t color, int *l, int *t, int *r, int *b) {
    int x, y, n = 0;
    *l = f->w; *t = f->h; *r = -1; *b = -1;
    fx_sync();
    for (y = 0; y < f->h; y++) for (x = 0; x < f->w; x++)
        if ((f->bits[y * f->w + x] & 0xffffff) == color) {
            n++;
            if (x < *l) { *l = x; }
            if (x > *r) { *r = x; }
            if (y < *t) { *t = y; }
            if (y > *b) { *b = y; }
        }
    return n;
}
/* Counts `color` pixels inside the inclusive box. */
static inline int fx_count_box(fx_t *f, int l, int t, int r, int b, uint32_t color) {
    int x, y, n = 0;
    fx_sync();
    for (y = t; y <= b; y++) for (x = l; x <= r; x++)
        if (x >= 0 && y >= 0 && x < f->w && y < f->h && (f->bits[y * f->w + x] & 0xffffff) == color) n++;
    return n;
}
/* True if any pixel within `rad` (Chebyshev) of (x,y) has `color`. */
static inline int fx_near(fx_t *f, int x, int y, int rad, uint32_t color) {
    return fx_count_box(f, x - rad, y - rad, x + rad, y + rad, color) > 0;
}
/* Number of pixels that differ between two fixtures of the same size. */
static inline int fx_diff(fx_t *a, fx_t *b) {
    int i, n = 0;
    fx_sync();
    if (a->w != b->w || a->h != b->h) return -1;
    for (i = 0; i < a->w * a->h; i++) if ((a->bits[i] & 0xffffff) != (b->bits[i] & 0xffffff)) n++;
    return n;
}

#define COL_RED   RGB(255, 0, 0)
#define COL_BLUE  RGB(0, 0, 255)
#define COL_GREEN RGB(0, 255, 0)
#define PX_RED    0xFF0000u
#define PX_BLUE   0x0000FFu
#define PX_GREEN  0x00FF00u
#define PX_WHITE  0xFFFFFFu
#define PX_BLACK  0x000000u
/* COLORREF (0x00BBGGRR) -> 32bpp DIB pixel (0x00RRGGBB). */
static inline uint32_t col2px(COLORREF c) { return ((c & 0xff) << 16) | (c & 0xff00) | ((c >> 16) & 0xff); }

/* Selects a solid pen / brush (or stock null objects) and returns the objects to restore & delete. */
typedef struct { HGDIOBJ oldpen, oldbrush; HGDIOBJ pen, brush; } gdi_sel_t;
static inline void sel_pen(HDC dc, gdi_sel_t *s, COLORREF c, int width) {
    s->pen = U(CreatePen)(PS_SOLID, width, c);
    s->oldpen = U(SelectObject)(dc, s->pen);
}
static inline void sel_brush(HDC dc, gdi_sel_t *s, COLORREF c) {
    s->brush = U(CreateSolidBrush)(c);
    s->oldbrush = U(SelectObject)(dc, s->brush);
}
static inline void sel_nullbrush(HDC dc, gdi_sel_t *s) {
    s->brush = NULL;
    s->oldbrush = U(SelectObject)(dc, U(GetStockObject)(NULL_BRUSH));
}
static inline void sel_nullpen(HDC dc, gdi_sel_t *s) {
    s->pen = NULL;
    s->oldpen = U(SelectObject)(dc, U(GetStockObject)(NULL_PEN));
}
static inline void sel_done(HDC dc, gdi_sel_t *s) {
    if (s->oldpen) U(SelectObject)(dc, s->oldpen);
    if (s->oldbrush) U(SelectObject)(dc, s->oldbrush);
    if (s->pen) U(DeleteObject)(s->pen);
    if (s->brush) U(DeleteObject)(s->brush);
    memset(s, 0, sizeof *s);
}

/* ---- path capture ---------------------------------------------------------------------------------------- */
#define PATH_MAX_PTS 512
struct pth { int n; POINT pt[PATH_MAX_PTS]; BYTE ty[PATH_MAX_PTS]; };
/* GetPath via the nSize==0 count protocol, then the data. Returns the count (or -1) and fills p. */
static inline int pth_get(HDC dc, struct pth *p) {
    int n, r;
    memset(p, 0, sizeof *p);
    n = U(GetPath)(dc, NULL, NULL, 0);
    p->n = n;
    if (n <= 0 || n > PATH_MAX_PTS) return n;
    r = U(GetPath)(dc, p->pt, p->ty, n);
    p->n = r;
    return r;
}
/* Begin a path, run nothing, return TRUE. */
#define PBEGIN(dc) CHECK(U(BeginPath)(dc))
#define PEND(dc)   CHECK(U(EndPath)(dc))

/* A path is structurally valid when it starts with MOVETO, every type is a known base type with optional
 * CLOSEFIGURE, and bezier runs come in triples. (Verified: NtGdiGetPath writes 6 for the figure start,
 * 2/4 per point with bit0 OR'ed onto the last point of a closed figure.) */
static inline int pth_valid(const struct pth *p) {
    int i;
    if (p->n <= 0) return 1;
    if ((p->ty[0] & ~PT_CLOSEFIGURE) != PT_MOVETO) return 0;
    for (i = 0; i < p->n; i++) {
        BYTE b = p->ty[i] & ~PT_CLOSEFIGURE;
        if (b != PT_MOVETO && b != PT_LINETO && b != PT_BEZIERTO) return 0;
        if (b == PT_MOVETO && (p->ty[i] & PT_CLOSEFIGURE)) return 0;
    }
    return 1;
}
static inline int pth_count_type(const struct pth *p, BYTE base) {
    int i, n = 0;
    for (i = 0; i < p->n; i++) if ((p->ty[i] & ~PT_CLOSEFIGURE) == base) n++;
    return n;
}

/* ---- DC_ATTR peeks (client-side mirror of kernel DC state) -------------------------------------------------- */
static inline void *xd_dcattr(HDC dc) {
    void *base = NULL, *pu = NULL;
    if (t_is_wine()) return NULL;
    if (!k_try_read_ptr((char *)k_peb() + PEB_GdiSharedHandleTable, &base) || !base) return NULL;
    if (!k_try_read_ptr((char *)base + (ULONG_PTR)((ULONG_PTR)dc & 0xffff) * GDI_CELL_SIZE + 0x10, &pu)) return NULL;
    return pu;
}
/* Reads a LONG from DC_ATTR+off; returns 0 (and leaves *v alone) when the attribute page is not reachable. */
static inline int xd_attr_i32(HDC dc, unsigned off, long *v) {
    void *a = xd_dcattr(dc); unsigned long u = 0;
    if (!a || !k_try_read_u32((char *)a + off, &u)) return 0;
    *v = (long)(int)u;
    return 1;
}
#define DCATTR_MapMode    0x68
#define DCATTR_Flags      0x6c
#define DCATTR_GfxMode    0xd0
#define DCATTR_WndOrgX    0x130
#define DCATTR_WndOrgY    0x138
#define DCATTR_WndExtX    0x13c
#define DCATTR_WndExtY    0x140
#define DCATTR_VpOrgX     0x144
#define DCATTR_VpOrgY     0x148
#define DCATTR_VpExtX     0x14c
#define DCATTR_VpExtY     0x150

/* XFORM helpers. Row-vector convention: p' = p * M, M = | m11 m12 0 ; m21 m22 0 ; dx dy 1 |. */
static inline XFORM xf_make(float m11, float m12, float m21, float m22, float dx, float dy) {
    XFORM x; x.eM11 = m11; x.eM12 = m12; x.eM21 = m21; x.eM22 = m22; x.eDx = dx; x.eDy = dy; return x;
}
/* r = a x b (apply a first, then b). */
static inline XFORM xf_mul(XFORM a, XFORM b) {
    XFORM r;
    r.eM11 = a.eM11 * b.eM11 + a.eM12 * b.eM21;
    r.eM12 = a.eM11 * b.eM12 + a.eM12 * b.eM22;
    r.eM21 = a.eM21 * b.eM11 + a.eM22 * b.eM21;
    r.eM22 = a.eM21 * b.eM12 + a.eM22 * b.eM22;
    r.eDx = a.eDx * b.eM11 + a.eDy * b.eM21 + b.eDx;
    r.eDy = a.eDx * b.eM12 + a.eDy * b.eM22 + b.eDy;
    return r;
}
static inline int xf_eq(const XFORM *a, const XFORM *b) {
    return a->eM11 == b->eM11 && a->eM12 == b->eM12 && a->eM21 == b->eM21 && a->eM22 == b->eM22 &&
           a->eDx == b->eDx && a->eDy == b->eDy;
}
#define CHECK_XF(got, want) do { XFORM g_ = (got), w_ = (want); \
    t_check(xf_eq(&g_, &w_), __FILE__, __LINE__, #got " == " #want, \
            "got {%g %g %g %g %g %g} want {%g %g %g %g %g %g}", g_.eM11, g_.eM12, g_.eM21, g_.eM22, g_.eDx, g_.eDy, \
            w_.eM11, w_.eM12, w_.eM21, w_.eM22, w_.eDx, w_.eDy); } while (0)

/* Wine's win32u exports a stub for every NtGdi/NtUser name but aborts the process when an unimplemented one is
 * called; those names are skipped there (Windows has them all). */
static inline int xd_wine_unimpl(const char *name) {
    static const char *const list[] = { "NtGdiPolyPatBlt", NULL };
    int i;
    if (!t_is_wine()) return 0;
    for (i = 0; list[i]; i++) if (!strcmp(list[i], name)) return 1;
    return 0;
}
#define REQUIRE_NT(name) do { if (!UN(name)) { t_skip("missing export: " #name); return; } \
    if (xd_wine_unimpl(#name)) { t_skip(#name " is unimplemented under Wine"); return; } } while (0)

/* Windows 10 win32u syscall stubs used directly (signatures from the win32kfull analysis). */
UDECL(BOOL,  NtGdiGetDCDword, (HDC, UINT, DWORD *));
UDECL(BOOL,  NtGdiBeginPath, (HDC));
UDECL(BOOL,  NtGdiEndPath, (HDC));
UDECL(BOOL,  NtGdiAbortPath, (HDC));
UDECL(BOOL,  NtGdiCloseFigure, (HDC));
UDECL(BOOL,  NtGdiFillPath, (HDC));
UDECL(BOOL,  NtGdiStrokePath, (HDC));
UDECL(BOOL,  NtGdiStrokeAndFillPath, (HDC));
UDECL(BOOL,  NtGdiWidenPath, (HDC));
UDECL(BOOL,  NtGdiFlattenPath, (HDC));
UDECL(HRGN,  NtGdiPathToRegion, (HDC));
UDECL(BOOL,  NtGdiSelectClipPath, (HDC, int));
UDECL(int,   NtGdiGetPath, (HDC, POINT *, BYTE *, int));
UDECL(BOOL,  NtGdiGetDCPoint, (HDC, UINT, POINT *));
UDECL(BOOL,  NtGdiModifyWorldTransform, (HDC, const XFORM *, DWORD));
UDECL(BOOL,  NtGdiGetTransform, (HDC, DWORD, XFORM *));
UDECL(BOOL,  NtGdiTransformPoints, (HDC, const POINT *, POINT *, int, int));
UDECL(BOOL,  NtGdiMoveTo, (HDC, int, int, POINT *));
UDECL(BOOL,  NtGdiLineTo, (HDC, int, int));
UDECL(BOOL,  NtGdiRectangle, (HDC, int, int, int, int));
UDECL(BOOL,  NtGdiEllipse, (HDC, int, int, int, int));
UDECL(BOOL,  NtGdiRoundRect, (HDC, int, int, int, int, int, int));
UDECL(BOOL,  NtGdiArcInternal, (int, HDC, int, int, int, int, int, int, int, int));
UDECL(BOOL,  NtGdiPolyPolyDraw, (HDC, const POINT *, const ULONG *, ULONG, int));
UDECL(UINT,  NtGdiSetBoundsRect, (HDC, const RECT *, UINT));
UDECL(UINT,  NtGdiGetBoundsRect, (HDC, RECT *, UINT));
UDECL(BOOL,  NtGdiPolyPatBlt, (HDC, DWORD, void *, DWORD, DWORD));

typedef struct { LONG x, y, cx, cy; HBRUSH hBrush; } POLYPATBLT_K;  /* 0x18 bytes: gdi32full!PolyPatBlt batches count*0x18 */
UDECL(BOOL, PolyPatBlt, (HDC, DWORD, void *, int, DWORD));
#endif
