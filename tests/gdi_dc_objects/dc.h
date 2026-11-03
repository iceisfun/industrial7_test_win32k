/* Shared helpers for the gdi_dc_objects area (device contexts and the objects selected into them).
 *
 * Facts used here come from the Windows 10 19041 analysis:
 *  - gdi32full!GetObjectType / SelectObjectImpl / GetCurrentObject / SaveDCImpl / RestoreDCImpl / SetDCBrushColor /
 *    GetDeviceCaps (gdi32.dll) / InternalDeleteObject / InternalDeleteDC / CreateCompatibleDC;
 *  - the kernel DC save/restore, compatible-DC creation, bitmap selection and DC query paths.
 * Handle layout: bits 0..15 table index, 16..22 object type (DC = 0x01, region 0x04, bitmap 0x05, palette 0x08,
 * font 0x0a, brush 0x10, colour space 0x09, pen 0x30, extpen 0x50), bit 23 stock, bits 24..31 reuse counter.
 */
#ifndef DC_H
#define DC_H
#include "t.h"
#include "k.h"
#include <wingdi.h>
#include <stdint.h>

/* Single-evaluation EXPECT_EQ: many checks here call the function under test inside the macro argument. */
#undef EXPECT_EQ
#define EXPECT_EQ(a, b) do { unsigned long long a_ = (unsigned long long)(ULONG_PTR)(a), b_ = (unsigned long long)(ULONG_PTR)(b); \
    t_check(a_ == b_, __FILE__, __LINE__, #a " == " #b, "got 0x%llx want 0x%llx", a_, b_); } while (0)
#undef EXPECT_NE
#define EXPECT_NE(a, b) do { unsigned long long a_ = (unsigned long long)(ULONG_PTR)(a), b_ = (unsigned long long)(ULONG_PTR)(b); \
    t_check(a_ != b_, __FILE__, __LINE__, #a " != " #b, "got 0x%llx", a_); } while (0)

/* Object type bits of a GDI handle (HIWORD & 0x7f). */
#define H_TYPE7(h)   ((unsigned)(((ULONG_PTR)(h) >> 16) & 0x7f))
#define H_STOCKED(h) (((ULONG_PTR)(h) & 0x800000) != 0)
#define H_INDEX(h)   ((unsigned)((ULONG_PTR)(h) & 0xffff))
#define T7_DC     0x01
#define T7_REGION 0x04
#define T7_BITMAP 0x05
#define T7_PALETTE 0x08
#define T7_FONT   0x0a
#define T7_BRUSH  0x10
#define T7_PEN    0x30

/* OBJ_* used by GetCurrentObject / NtGdiGetDCObject: the kernel takes the shifted type constants
 * (type7 << 16): font 0xa0000, palette 0x80000, bitmap 0x50000, brush 0x100000, pen 0x300000. */
#define K_DCOBJ_FONT   0xa0000
#define K_DCOBJ_PAL    0x80000
#define K_DCOBJ_BITMAP 0x50000
#define K_DCOBJ_BRUSH  0x100000
#define K_DCOBJ_PEN    0x300000
#define K_DCOBJ_EXTPEN 0x500000

/* Process-wide handle table cell (24 bytes): einfo@0, owner@8, upper@0xc, objt@0xe, flags@0xf, pUser@0x10. */
struct dc_cell { unsigned long long q0; unsigned owner; unsigned short upper; unsigned char objt, flags; unsigned long long puser; };

static inline int dc_cell_of(HGDIOBJ h, struct dc_cell *c) {
    void *base = NULL;
    if (!k_try_read_ptr((char *)k_peb() + PEB_GdiSharedHandleTable, &base) || !base) return 0;
    return k_try_read((char *)base + (ULONG_PTR)H_INDEX(h) * GDI_CELL_SIZE, c, sizeof *c);
}
/* True when the cell for h is a live entry of type7 whose upper bits match the handle. */
static inline int dc_cell_live(HGDIOBJ h, unsigned type7) {
    struct dc_cell c;
    if (!dc_cell_of(h, &c)) return 0;
    return c.objt == (type7 & 0x1f) && c.upper == (unsigned short)(((ULONG_PTR)h >> 16) & 0xffff);
}

/* A handle whose object was deleted. GetObjectType is no help here: for a dead DC, bitmap, pen, font or palette handle
 * Windows 10 keeps answering with the type decoded from the handle bits (OBJ_DC, OBJ_BITMAP, ...), and it only returns 0
 * when the table slot has meanwhile been taken by another object. The shared-table cell is the authority: 1 = cell is
 * free, 0 = still live, -1 = table unreadable. */
static inline int h_cell_dead(HGDIOBJ h) {
    struct dc_cell c;
    if (!dc_cell_of(h, &c)) return -1;
    return c.objt == 0 || c.upper != (unsigned short)(((ULONG_PTR)h >> 16) & 0xffff);
}
#define EXPECT_DEAD(h) do { int d_ = h_cell_dead((HGDIOBJ)(h)); if (d_ >= 0) CHECKF(d_, "handle %p still has a live cell", (void *)(h)); } while (0)

/* DC_ATTR (cell.pUser of a DC). Offsets are the ones gdi32full dereferences. */
#define DA_FLAGS     0x000  /* bit0: DIB section selected, bit1: GetDeviceCaps may use the shared DEVCAPS table */
#define DA_LAYOUT    0x06c  /* layout flags (GetLayout returns it) */
#define DA_DIRTY     0x098  /* ulDirty_ */
#define DA_HBRUSH    0x0a0
#define DA_HPEN      0x0a8
#define DA_DCBRUSH_X 0x0c0  /* translated DC brush colour */
#define DA_DCBRUSH   0x0c4  /* what GetDCBrushColor returns */
#define DA_DCPEN_X   0x0c8
#define DA_DCPEN     0x0cc  /* what GetDCPenColor returns */
#define DA_GFXMODE   0x0d0
#define DA_BKCLR     0x0b4
#define DA_TXCLR     0x0bc
#define DA_HFONT     0x128
#define DA_MIN       0x168
#define DIRTY_BRUSH_SEL 0x1000
#define DIRTY_PEN_SEL   0x2000

static inline void *dc_attr(HDC dc) {
    struct dc_cell c;
    if (!dc_cell_of((HGDIOBJ)dc, &c)) return NULL;
    if (!c.puser || c.puser >= 0xFFFF800000000000ull) return NULL;
    return (void *)(ULONG_PTR)c.puser;
}
static inline uint32_t da_u32(void *a, unsigned off) { unsigned long v = 0xDEADBEEF; if (a) k_try_read_u32((char *)a + off, &v); return (uint32_t)v; }
static inline void *da_ptr(void *a, unsigned off) { void *v = NULL; if (a) k_try_read_ptr((char *)a + off, &v); return v; }

/* Syscalls with no SDK declaration (signatures derived in the area comments). */
UDECL(HDC,  NtGdiCreateCompatibleDC, (HDC));
UDECL(BOOL, NtGdiDeleteObjectApp, (HANDLE));
UDECL(int,  NtGdiSaveDC, (HDC));
UDECL(BOOL, NtGdiRestoreDC, (HDC, int));
UDECL(HANDLE, NtGdiGetDCObject, (HDC, int));
UDECL(HANDLE, NtGdiSelectBitmap, (HDC, HANDLE));
UDECL(HANDLE, NtGdiSelectBrush, (HDC, HANDLE));
UDECL(HANDLE, NtGdiSelectPen, (HDC, HANDLE));
UDECL(HANDLE, NtGdiSelectFont, (HDC, HANDLE));
UDECL(int,  NtGdiGetDCDword, (HDC, int, ULONG *));
UDECL(int,  NtGdiGetAndSetDCDword, (HDC, int, ULONG, ULONG *));
UDECL(int,  NtGdiGetDCPoint, (HDC, int, void *));
UDECL(int,  NtGdiGetDeviceCaps, (HDC, int));
UDECL(int,  NtGdiGetDeviceCapsAll, (HDC, void *));
UDECL(BOOL, NtGdiCancelDC, (HDC));
UDECL(DWORD, NtGdiSetLayout, (HDC, LONG, DWORD));
UDECL(void, NtGdiFlush, (void));
UDECL(HDC,  NtGdiOpenDCW, (void *, void *, void *, ULONG, ULONG, ULONG, void *, void *, void *));
UDECL(int,  NtGdiMakeInfoDC, (HDC, int));

/* The hdc used as pointer-sized handle in direct calls. */
#define DCWANT(...) do { if (!(__VA_ARGS__)) { t_skip("missing export: " #__VA_ARGS__); return; } } while (0)

/* A memory DC plus the default bitmap, released by dcf_close. */
struct dcf { HDC mem; HDC scr; };
static inline int dcf_open(struct dcf *f) {
    memset(f, 0, sizeof *f);
    if (!U(CreateCompatibleDC) || !U(GetDC)) return 0;
    f->scr = U(GetDC)(NULL);
    f->mem = U(CreateCompatibleDC)(f->scr);
    return f->mem != NULL;
}
static inline void dcf_close(struct dcf *f) {
    if (f->mem) U(DeleteDC)(f->mem);
    if (f->scr) U(ReleaseDC)(NULL, f->scr);
    f->mem = f->scr = NULL;
}
#define FX(f) struct dcf f; if (!dcf_open(&f)) { dcf_close(&f); t_skip("cannot create a memory DC"); return; }
#endif
