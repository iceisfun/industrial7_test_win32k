/* Shared helpers for the gdi_regions_clip area.
 *
 * Kernel facts used (win32kbase/win32kfull 19041 analysis):
 *  - NtGdiCreateRectRgn / SetRectRgn / ExtCreateRegion / Elliptic / RoundRect accept coordinates in
 *    [-0x8000000, 0x7ffffff] only (the "(v & 0xf8000000) + 0x8000000 & 0xf7ffffff" test); out of range
 *    fails with ERROR_INVALID_PARAMETER (0x57) and a NULL/FALSE result.
 *  - gdi32!CreateRectRgn keeps simple rectangle regions in an RGN_ATTR block (GDICELL.pUser):
 *    dword[0] flags (bit 0x20 = changed in user mode), dword[1] complexity (1 NULL, 2 SIMPLE),
 *    dword[2..5] the rectangle. gdi32!GetRgnBox / OffsetRgn / CombineRgn read and write it directly.
 *  - GDICELL: +8 owner pid (bit0 ignored), +0xc wUpper = HIWORD(handle), +0xe wType (4 = region).
 */
#ifndef RGN_H
#define RGN_H
#include "t.h"
#include "k.h"

/* ---- win32u syscalls (signatures derived from the Gre* callees and from the gdi32 call sites) --- */
UDECL(HRGN,  NtGdiCreateRectRgn,   (int, int, int, int));
UDECL(HRGN,  NtGdiCreateEllipticRgn, (int, int, int, int));
UDECL(HRGN,  NtGdiCreateRoundRectRgn, (int, int, int, int, int, int));
UDECL(HRGN,  NtGdiExtCreateRegion, (const XFORM *, DWORD, const RGNDATA *));
UDECL(int,   NtGdiCombineRgn,      (HRGN, HRGN, HRGN, int));
UDECL(BOOL,  NtGdiSetRectRgn,      (HRGN, int, int, int, int));
UDECL(int,   NtGdiGetRgnBox,       (HRGN, RECT *));
UDECL(DWORD, NtGdiGetRegionData,   (HRGN, DWORD, void *));
UDECL(BOOL,  NtGdiPtInRegion,      (HRGN, int, int));
UDECL(BOOL,  NtGdiRectInRegion,    (HRGN, RECT *));
UDECL(BOOL,  NtGdiEqualRgn,        (HRGN, HRGN));
UDECL(int,   NtGdiOffsetRgn,       (HRGN, int, int));
UDECL(HRGN,  NtGdiPathToRegion,    (HDC));
UDECL(BOOL,  NtGdiSelectClipPath,  (HDC, int));
UDECL(int,   NtGdiExtSelectClipRgn, (HDC, HRGN, int));
UDECL(int,   NtGdiIntersectClipRect, (HDC, int, int, int, int));
UDECL(int,   NtGdiExcludeClipRect, (HDC, int, int, int, int));
UDECL(int,   NtGdiOffsetClipRgn,   (HDC, int, int));
UDECL(int,   NtGdiSetMetaRgn,      (HDC));
UDECL(int,   NtGdiGetAppClipBox,   (HDC, RECT *));
UDECL(int,   NtGdiGetRandomRgn,    (HDC, HRGN, int));

#ifndef RGN_COPY
#define RGN_COPY 5
#endif
#define RC_ERROR   0
#define RC_NULL    1
#define RC_SIMPLE  2
#define RC_COMPLEX 3
#define GRR_CLIP 1
#define GRR_META 2
#define GRR_API  3
#define GRR_SYS  4
#define COORD_MIN (-0x8000000)
#define COORD_MAX 0x7ffffff

/* The harness EXPECT_EQ evaluates operands twice; evaluate once here. */
#undef EXPECT_EQ
#define EXPECT_EQ(a, b) do { unsigned long long a_ = (unsigned long long)(ULONG_PTR)(a), b_ = (unsigned long long)(ULONG_PTR)(b); \
    t_check(a_ == b_, __FILE__, __LINE__, #a " == " #b, "got 0x%llx want 0x%llx", a_, b_); } while (0)

#define NOTWINE(why) do { if (t_is_wine()) { t_skip("%s", why); return; } } while (0)

static inline int box_is(const RECT *r, int l, int t, int rr, int b) { return r->left == l && r->top == t && r->right == rr && r->bottom == b; }
#define CHECK_BOX(r, l, t, rr, b) CHECKF(box_is(&(r), l, t, rr, b), "box (%ld,%ld,%ld,%ld) want (%d,%d,%d,%d)", (r).left, (r).top, (r).right, (r).bottom, (int)(l), (int)(t), (int)(rr), (int)(b))

/* ---- GDI cell / RGN_ATTR ------------------------------------------------------------------------ */
struct rcell { void *kern; unsigned long owner; unsigned upper; unsigned type; void *user; };
static inline int rgn_cell(void *h, struct rcell *c) {
    char *base = NULL;
    if (!k_try_read_ptr((char *)k_peb() + PEB_GdiSharedHandleTable, (void **)&base) || !base) return 0;
    char *cell = base + (ULONG_PTR)((ULONG_PTR)h & 0xffff) * GDI_CELL_SIZE;
    unsigned long o; unsigned short up; unsigned char ty; void *k, *u;
    if (!k_try_read_ptr(cell, &k) || !k_try_read_u32(cell + 8, &o) || !k_try_read_u16(cell + 0xc, &up) ||
        !k_try_read_u8(cell + 0xe, &ty) || !k_try_read_ptr(cell + 0x10, &u)) return 0;
    c->kern = k; c->owner = o; c->upper = up; c->type = ty; c->user = u;
    return 1;
}
struct rattr { unsigned long flags, cplx; RECT r; };
static inline int rgn_attr(HRGN h, struct rattr *a) {
    struct rcell c;
    if (t_is_wine() || !rgn_cell(h, &c) || !c.user) return 0;
    unsigned long v[6];
    if (!k_try_read(c.user, v, sizeof v)) return 0;
    a->flags = v[0]; a->cplx = v[1];
    a->r.left = (LONG)v[2]; a->r.top = (LONG)v[3]; a->r.right = (LONG)v[4]; a->r.bottom = (LONG)v[5];
    return 1;
}
static inline int rgn_attr_poke_cplx(HRGN h, unsigned long v) {
    struct rcell c; SIZE_T n = 0;
    if (!rgn_cell(h, &c) || !c.user) return 0;
    return WriteProcessMemory(GetCurrentProcess(), (char *)c.user + 4, &v, 4, &n) && n == 4;
}

/* A rectangle region made by the raw NtGdiCreateRectRgn syscall comes back with an empty client block (flags 0, complexity 0)
 * and the kernel rejects it (GetRgnBox returns 0, SetRectRgn/CombineRgn fail) until that block is filled in; gdi32's
 * CreateRectRgn does exactly that right after the syscall (flags 0x10, complexity SIMPLE/NULL, the ordered rectangle).
 * kcreate() creates through the syscall and completes the block the same way, so the kernel entry points under test see a
 * region exactly as gdi32 would hand it over. */
static inline HRGN kcreate(int l, int t, int r, int b) {
    HRGN h = UN(NtGdiCreateRectRgn)(l, t, r, b);
    struct rcell c; SIZE_T n = 0;
    if (!h || t_is_wine() || !rgn_cell(h, &c) || !c.user) return h;
    int tmp;
    if (l > r) { tmp = l; l = r; r = tmp; }
    if (t > b) { tmp = t; t = b; b = tmp; }
    int empty = (l == r || t == b);
    unsigned long v[6] = { 0x10, empty ? 1u : 2u, (unsigned long)(empty ? 0 : l), (unsigned long)(empty ? 0 : t),
                           (unsigned long)(empty ? 0 : r), (unsigned long)(empty ? 0 : b) };
    WriteProcessMemory(GetCurrentProcess(), c.user, v, sizeof v, &n);
    return h;
}

/* ---- region utilities (all through win32u so the userland wrapper cannot short-circuit) ---------- */
/* box via the kernel */
static inline int kbox(HRGN h, RECT *r) { memset(r, 0x55, sizeof *r); return UN(NtGdiGetRgnBox) ? UN(NtGdiGetRgnBox)(h, r) : -1; }
/* rectangles of a region in canonical band order; returns nCount or -1 */
static inline int rgn_rects(HRGN h, RECT *out, int max) {
    BYTE buf[32 + 16 * 600];
    DWORD n = UN(NtGdiGetRegionData)(h, sizeof buf, buf);
    if (!n) return -1;
    RGNDATAHEADER *hd = (RGNDATAHEADER *)buf;
    if ((int)hd->nCount > max) return -1;
    memcpy(out, buf + 32, hd->nCount * sizeof(RECT));
    return (int)hd->nCount;
}
static inline int rgn_is_rect(HRGN h, int l, int t, int r, int b) {
    RECT bx; int c = kbox(h, &bx);
    return c == RC_SIMPLE && box_is(&bx, l, t, r, b);
}

/* ---- memory DC with a 32bpp top-down DIB section ------------------------------------------------- */
struct fx { HDC dc; HBITMAP bmp, old; DWORD *bits; int w, h; };
static inline int fx_open(struct fx *f, int w, int h) {
    memset(f, 0, sizeof *f);
    if (!U(CreateCompatibleDC) || !U(CreateDIBSection) || !U(SelectObject) || !U(DeleteDC) || !U(DeleteObject)) return 0;
    f->dc = U(CreateCompatibleDC)(NULL);
    if (!f->dc) return 0;
    BITMAPINFO bi; memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    f->bmp = U(CreateDIBSection)(f->dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!f->bmp || !bits) { U(DeleteDC)(f->dc); f->dc = NULL; return 0; }
    f->old = (HBITMAP)U(SelectObject)(f->dc, f->bmp);
    f->bits = (DWORD *)bits; f->w = w; f->h = h;
    memset(bits, 0xff, (size_t)w * h * 4);   /* white */
    return 1;
}
static inline void fx_close(struct fx *f) {
    if (!f->dc) return;
    U(SelectObject)(f->dc, f->old);
    U(DeleteObject)(f->bmp);
    U(DeleteDC)(f->dc);
    memset(f, 0, sizeof *f);
}
/* Fill the DIB memory directly: PatBlt would be limited by whatever clip the test installed. */
static inline void fx_white(struct fx *f) { if (U(GdiFlush)) U(GdiFlush)(); memset(f->bits, 0xff, (size_t)f->w * f->h * 4); }
/* blacken the whole DC area through the current clip, flush the TEB batch, count black pixels */
static inline int fx_blacken(struct fx *f) {
    U(PatBlt)(f->dc, 0, 0, f->w, f->h, BLACKNESS);
    if (U(GdiFlush)) U(GdiFlush)();
    int n = 0;
    for (int i = 0; i < f->w * f->h; i++) if ((f->bits[i] & 0xffffff) == 0) n++;
    return n;
}
static inline int fx_px_black(struct fx *f, int x, int y) { return (f->bits[y * f->w + x] & 0xffffff) == 0; }
#define FX(f, w, h) struct fx f; if (!fx_open(&f, w, h)) { t_skip("cannot create memory DC + DIB section"); return; }
#define NEED(p) do { if (!(p)) { t_skip("missing export: " #p); return; } } while (0)
#endif
