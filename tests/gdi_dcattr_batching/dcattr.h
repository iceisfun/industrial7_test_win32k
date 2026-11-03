/* Shared helpers for the gdi_dcattr_batching area.
 *
 * Everything here is derived from the Windows 10 19041 analysis of the user-mode setters/getters
 * (gdi32full!SetBkColor/SetTextColor/SetBkModeImpl/SetROP2/SetMapMode/SetTextAlignImpl/MoveToExImpl/
 * SetStretchBltModeImpl/SetPolyFillModeImpl/SetWindowOrgEx/SetViewportOrgEx/SetBrushOrgEx/SelectObjectImpl,
 * bBatchTextOut, ExtTextOutWImpl, PatBlt, GdiSetBatchLimit) and of the kernel side
 * (NtGdiFlushUserBatch, NtGdiGetDCDword,
 * ntoskrnl!KiSystemServiceStart). The DC_ATTR layout has no PDB type on this corpus, so every
 * offset below is the one the real gdi32full code dereferences off GDICELL.pUser.
 */
#ifndef DCATTR_H
#define DCATTR_H
#include "t.h"
#include "k.h"
#include <stdint.h>

/* The harness EXPECT_EQ evaluates its operands twice (check and message); many checks here call setters
 * whose return value is the thing under test, so evaluate each side exactly once. */
#undef EXPECT_EQ
#define EXPECT_EQ(a, b) do { unsigned long long a_ = (unsigned long long)(ULONG_PTR)(a), b_ = (unsigned long long)(ULONG_PTR)(b); \
    t_check(a_ == b_, __FILE__, __LINE__, #a " == " #b, "got 0x%llx want 0x%llx", a_, b_); } while (0)

/* ---- DC_ATTR (GDICELL.pUser of a DC) offsets, read from gdi32full ----------------------------- */
#define DCA_MAPMODE      0x068  /* int   iMapMode: GetMapMode returns it, SetMapMode's kernel half writes it */
#define DCA_LAYOUTFLAGS  0x06c  /* byte, bit0 = right-to-left layout (SetTextAlign/SetViewportOrgEx test it) */
#define DCA_DIRTY        0x098  /* ulDirty_ */
#define DCA_HBRUSH       0x0a0  /* HBRUSH hbrush (SelectObjectImpl, brush case) */
#define DCA_HPEN         0x0a8  /* HPEN   hpen   (SelectObjectImpl, pen case) */
#define DCA_BKCLR_XL     0x0b0  /* COLORREF crBackgroundClr (after ICM translation, flag bits masked) */
#define DCA_BKCLR        0x0b4  /* COLORREF ulBackgroundClr: what GetBkColor returns */
#define DCA_TXCLR_XL     0x0b8  /* COLORREF crForegroundClr (translated) */
#define DCA_TXCLR        0x0bc  /* COLORREF ulForegroundClr: what GetTextColor returns */
#define DCA_GFXMODE      0x0d0  /* int GetGraphicsMode */
#define DCA_ROP2         0x0d4  /* byte jROP2 */
#define DCA_JBKMODE      0x0d5  /* byte jBkMode  (1 = TRANSPARENT-ish, 2 = OPAQUE) */
#define DCA_JFILLMODE    0x0d6  /* byte jFillMode */
#define DCA_JSTRETCH     0x0d7  /* byte jStretchBltMode */
#define DCA_PTLCURRENT   0x0d8  /* POINT ptlCurrent (logical) */
#define DCA_BKMODE       0x0e0  /* int lBkMode */
#define DCA_FILLMODE     0x0e4  /* int lFillMode */
#define DCA_STRETCH      0x0e8  /* int lStretchBltMode */
#define DCA_ICMFLAGS     0x0f0  /* ulICMFlags-like word tested by SetBkColor/SetTextColor (bit0 = ICM on) */
#define DCA_TA_MASKED    0x110  /* lTextAlign & 0x1f (RTL-adjusted) */
#define DCA_TA           0x114  /* lTextAlign as the caller passed it */
#define DCA_CHAREXTRA    0x118  /* int  lTextExtra */
#define DCA_HFONT        0x128  /* HFONT hlfntNew */
#define DCA_WNDORG_X     0x130  /* int  x copy used by GetWindowOrgEx */
#define DCA_WNDORG       0x134  /* POINT ptlWindowOrg (x at +0x134, y at +0x138) */
#define DCA_VIEWORG      0x144  /* POINT ptlViewportOrg */
#define DCA_XFORMDIRTY   0x154  /* flXform dirty bits (0x2010 set when an origin changes) */
#define DCA_BRUSHORG     0x158  /* POINT ptlBrushOrigin */
#define DCA_MIN_READ     0x168  /* we only ever read bytes below this offset */

/* ulDirty_ bits as the gdi32full code sets/tests them (names describe the observed effect only) */
#define DIRTY_FILL       0x00000001
#define DIRTY_LINE       0x00000002
#define DIRTY_TEXT       0x00000004
#define DIRTY_BACKGROUND 0x00000008
#define DIRTY_PTLCURRENT 0x00000100  /* kernel moved the pen: ptfxCurrent (+0x08) is newer than ptlCurrent */
#define DIRTY_PTFX       0x00000200
#define DIRTY_STYLESTATE 0x00000400
#define DIRTY_BRUSH      0x00001000  /* SelectObject(brush) / batch replay */
#define DIRTY_PEN        0x00002000  /* SelectObject(pen) */
#define DIRTY_BATCH_PEND 0x00200000  /* a PatBlt/ExtTextOut(rect)/text record is queued in TEB.GdiTebBatch */
#define DIRTY_BATCH_TEXT 0x00400000  /* a text record is queued (bBatchTextOut sets 0x600000) */

/* ---- TEB batch ---------------------------------------------------------------------------------- */
#define TB_OFFSET_RAW  (TEB_GdiTebBatch + 0x0)   /* bits 0-30 Offset, bit 31 HasRenderingCommand */
#define TB_HDC         (TEB_GdiTebBatch + 0x8)
#define TB_BUFFER      (TEB_GdiTebBatch + 0x10)  /* 0x300 */
#define TB_BUFFER_SIZE 1224u                     /* UCHAR[1224] in the PDB, but the code accepts records up to 0x4d8 */
#define TB_FULL        0x4d9u                    /* gdi32full queues only while (Offset + len) < 0x4d9 */

enum { REC_PATBLT = 0, REC_POLYPATBLT = 1, REC_TEXTOUT = 2, REC_TEXTRECT = 3, REC_BRUSHORG = 4,
       REC_CLIPRGN = 5, REC_SELECTFONT = 6, REC_DELETE7 = 7, REC_DELETE8 = 8 };

static inline unsigned long rd32(const void *p) { unsigned long v = 0xDEADBEEFu; k_try_read_u32(p, &v); return v; }
static inline unsigned rd16(const void *p) { unsigned short v = 0xBEEF; k_try_read_u16(p, &v); return v; }
static inline unsigned rd8(const void *p) { unsigned char v = 0xEF; k_try_read_u8(p, &v); return v; }
static inline void *rdp(const void *p) { void *v = NULL; k_try_read_ptr(p, &v); return v; }
static inline int wr32(void *p, unsigned long v) { SIZE_T n = 0; return WriteProcessMemory(GetCurrentProcess(), p, &v, 4, &n) && n == 4; }
static inline int wr8(void *p, unsigned char v) { SIZE_T n = 0; return WriteProcessMemory(GetCurrentProcess(), p, &v, 1, &n) && n == 1; }
static inline int wrb(void *p, const void *src, size_t sz) { SIZE_T n = 0; return WriteProcessMemory(GetCurrentProcess(), p, src, sz, &n) && n == sz; }

static inline unsigned teb_count(void) { return (unsigned)rd32((char *)k_teb() + TEB_GdiBatchCount); }
static inline unsigned teb_offset(void) { return (unsigned)(rd32((char *)k_teb() + TB_OFFSET_RAW) & 0x7fffffffu); }
static inline unsigned teb_hrc(void) { return (unsigned)(rd32((char *)k_teb() + TB_OFFSET_RAW) >> 31); }
static inline HDC teb_hdc(void) { return (HDC)rdp((char *)k_teb() + TB_HDC); }
static inline char *teb_rec(unsigned off) { return (char *)k_teb() + TB_BUFFER + off; }
static inline unsigned rec_len(unsigned off) { return rd16(teb_rec(off)); }
static inline unsigned rec_type(unsigned off) { return rd16(teb_rec(off) + 2); }

/* Leaves the calling thread with an empty batch. GdiFlush -> NtGdiFlush is itself a win32k syscall, and
 * KiSystemServiceStart drains a non-empty batch on entry to every win32k service. The kernel flush
 * routine we can see (NtGdiFlushUserBatchInternal) clears TEB.HDC, but the entry hook variant may not,
 * so the (user-owned) TEB word is zeroed here to keep tests independent of each other. */
static unsigned g_hrc_after_flush;   /* HasRenderingCommand as the last flush left it */
static inline void batch_reset(void) {
    if (U(GdiFlush)) U(GdiFlush)();
    if (teb_count() == 0) {
        g_hrc_after_flush = teb_hrc();
        if (teb_hdc() != NULL) wrb((char *)k_teb() + TB_HDC, &(void *){ NULL }, sizeof(void *));
        if (rd32((char *)k_teb() + TB_OFFSET_RAW) != 0) wr32((char *)k_teb() + TB_OFFSET_RAW, 0);
    }
}

/* ---- DC_ATTR page ------------------------------------------------------------------------------ */
struct dca_cell { void *kern; unsigned long owner; unsigned upper; unsigned type; void *user; };

static inline int dca_cell(void *h, struct dca_cell *c) {
    char *base = (char *)rdp((char *)k_peb() + PEB_GdiSharedHandleTable);
    if (!base) return 0;
    char *cell = base + (ULONG_PTR)((ULONG_PTR)h & 0xffff) * GDI_CELL_SIZE;
    unsigned long o; unsigned short up; unsigned char ty; void *k, *u;
    if (!k_try_read_ptr(cell, &k) || !k_try_read_u32(cell + 8, &o) || !k_try_read_u16(cell + 0xc, &up) ||
        !k_try_read_u8(cell + 0xe, &ty) || !k_try_read_ptr(cell + 0x10, &u)) return 0;
    c->kern = k; c->owner = o; c->upper = up; c->type = ty; c->user = u;
    return 1;
}
/* GdiGetEntry-equivalent validation, then GDICELL.pUser; NULL when the layout is not that of Windows. */
static inline void *dca_of(HDC h) {
    struct dca_cell c;
    if (t_is_wine()) return NULL;   /* Wine's GDI tables have no Windows DC_ATTR pages */
    if (!dca_cell(h, &c)) return NULL;
    if (c.type != 1 || c.upper != (unsigned)(((ULONG_PTR)h >> 16) & 0xffff)) return NULL;
    if (c.user == NULL) return NULL;
    unsigned char probe[DCA_MIN_READ];
    if (!k_try_read(c.user, probe, sizeof probe)) return NULL;
    return c.user;
}
static inline unsigned dca_u32(void *a, unsigned off) { return (unsigned)rd32((char *)a + off); }
static inline int dca_i32(void *a, unsigned off) { return (int)rd32((char *)a + off); }
static inline unsigned dca_u8(void *a, unsigned off) { return rd8((char *)a + off); }
static inline void *dca_ptr(void *a, unsigned off) { return rdp((char *)a + off); }
static inline POINT dca_pt(void *a, unsigned off) { POINT p = { 0x7eadbeef, 0x7eadbeef }; k_try_read((char *)a + off, &p, sizeof p); return p; }
static inline int dca_poke32(void *a, unsigned off, unsigned v) { return wr32((char *)a + off, v); }
static inline int dca_poke8(void *a, unsigned off, unsigned v) { return wr8((char *)a + off, (unsigned char)v); }

/* ---- memory DC + 32bpp top-down DIB fixture ----------------------------------------------------- */
struct fx { HDC dc; HBITMAP bmp, old; DWORD *bits; int w, h; void *a; };

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
    f->bits = (DWORD *)bits; f->w = w; f->h = h;
    f->old = (HBITMAP)U(SelectObject)(f->dc, f->bmp);
    f->a = dca_of(f->dc);
    batch_reset();
    return 1;
}
static inline void fx_close(struct fx *f) {
    batch_reset();
    if (f->dc) { if (f->old) U(SelectObject)(f->dc, f->old); }
    if (f->bmp) U(DeleteObject)(f->bmp);
    if (f->dc) U(DeleteDC)(f->dc);
    batch_reset();
    memset(f, 0, sizeof *f);
}
static inline void fx_fill(struct fx *f, DWORD v) { for (int i = 0; i < f->w * f->h; i++) f->bits[i] = v; }
static inline DWORD fx_px(struct fx *f, int x, int y) { return f->bits[y * f->w + x] & 0x00ffffffu; }
/* DIB sections are written asynchronously through the batch/flush machinery: GdiFlush before reading. */
static inline void fx_sync(void) { if (U(GdiFlush)) U(GdiFlush)(); }
#define RGB2PX(r, g, b) ((DWORD)(((r) << 16) | ((g) << 8) | (b)))

#define NOTWINE(why) do { if (t_is_wine()) { t_skip("%s", why); return; } } while (0)
#define FXT(f, w, h) struct fx f; if (!fx_open(&f, (w), (h))) { t_skip("cannot create memory DC with DIB section"); return; }
#define NEEDA(f) do { if (!(f).a) { t_skip("DC_ATTR page not reachable through GdiSharedHandleTable (not the Windows layout)"); fx_close(&f); return; } } while (0)
/* Skip when the first batched call did not queue (e.g. emulator, or text batching disabled by a language pack). */
#define NEEDBATCH(f, why) do { if (!(f).a || teb_count() == 0) { t_note("batch not observed: %s", why); batch_reset(); fx_close(&f); return; } } while (0)

/* Expected length of a queued text record (bBatchTextOut): (((cb+7)&~7) + 0x58 + 7) & 0xfff8 for a string without a dx array. */
static inline unsigned text_rec_len(unsigned nchars) { unsigned cb = nchars * 2; unsigned a = (cb + 7) & ~7u; return (a + 0x58 + 7) & 0xfff8u; }

#endif
