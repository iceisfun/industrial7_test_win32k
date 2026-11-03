/* gdi_pens_brushes_palettes helpers.
 *
 * Handle layout (win32kbase ENTRYOBJ::hSetup, see gdi_handle_table/gh.h): bits 0..15 table index, bits 16..22
 * object type, bit 23 stock flag, bits 24..31 reuse counter. Type7 values used here come from the exact
 * constants the kernel/gdi32full compare against:
 *   brush 0x10 (GetObjectW: `(h & 0x7f0000) == 0x100000`), old-style pen 0x30 (`| 0x300000` when creating an old-style pen
 *   extended pen 0x50 (`| 0x500000`), palette 0x08 (GetObject palette branch).
 * Cell (24 bytes): +0 einfo, +8 owner (pid|lock), +0xc upper handle bits, +0xe Objt, +0xf flags, +0x10 pUser.
 * For brush-type objects (brushes, pens) pUser is a BRUSHATTR: DWORD flags @0, COLORREF @4 (gdi32full!CreatePen
 * writes `attr[1] = color; attr[0] |= 4`; the kernel later syncs the brush color).
 */
#ifndef PB_H
#define PB_H
#include "t.h"
#include "k.h"
#include <wingdi.h>
#include <stdlib.h>

struct pb_cell {
    unsigned long long q0;
    unsigned int       owner;
    unsigned short     upper;
    unsigned char      objt;
    unsigned char      flags;
    unsigned long long puser;
};
typedef char pb_cell_size_must_be_24[(sizeof(struct pb_cell) == 24) ? 1 : -1];

#define PB_IDX(h)   ((unsigned)((ULONG_PTR)(h) & 0xffff))
#define PB_T7(h)    ((unsigned)(((ULONG_PTR)(h) >> 16) & 0x7f))
#define PB_STOCKED(h) (((ULONG_PTR)(h) & 0x00800000u) != 0)
#define PB_PID()    ((unsigned)GetCurrentProcessId())

#define T7_BRUSH  0x10u
#define T7_PEN    0x30u
#define T7_EXTPEN 0x50u
#define T7_PAL    0x08u

/* syscall signatures (derived from the gdi32full callers and win32kfull analysis) */
UDECL(HANDLE, NtGdiCreatePen, (LONG iStyle, LONG iWidth, ULONG cr, HANDLE hbr));
UDECL(HANDLE, NtGdiExtCreatePen, (ULONG iPenStyle, ULONG cWidth, ULONG iBrushStyle, ULONG ulColor,
                                  ULONG_PTR lBrushClient, ULONG_PTR lBrushHatch, ULONG cStyle, ULONG *pulStyle,
                                  ULONG cjDIB, BOOL bOldStylePen, HANDLE hbr));
UDECL(HANDLE, NtGdiCreateSolidBrush, (ULONG cr, HANDLE hbr));
UDECL(HANDLE, NtGdiCreateHatchBrushInternal, (ULONG iHatch, ULONG cr, BOOL bPen));
UDECL(HANDLE, NtGdiCreatePatternBrushInternal, (HANDLE hbm, BOOL b8x8, BOOL bPen));
UDECL(HANDLE, NtGdiCreateDIBBrush, (const void *pbmi, ULONG iUsage, ULONG cj, BOOL b8x8, BOOL bPen, const void *pClient));
UDECL(HANDLE, NtGdiCreatePaletteInternal, (const void *plp, ULONG cEntries));
UDECL(ULONG,  NtGdiDoPalette, (HANDLE h, ULONG iStart, ULONG cEntries, void *pe, ULONG iFunc, BOOL bInbound));
UDECL(BOOL,   NtGdiResizePalette, (HANDLE h, ULONG n));
UDECL(ULONG,  NtGdiGetNearestPaletteIndex, (HANDLE h, ULONG cr));
UDECL(ULONG,  NtGdiGetNearestColor, (HDC hdc, ULONG cr));
UDECL(LONG,   NtGdiExtGetObjectW, (HANDLE h, LONG cj, void *pv));
UDECL(BOOL,   NtGdiSetBrushOrg, (HDC hdc, LONG x, LONG y, POINT *pOld));
UDECL(BOOL,   NtGdiUnrealizeObject, (HANDLE h));
UDECL(ULONG,  NtGdiGetSystemPaletteUse, (HDC hdc));
UDECL(ULONG,  NtGdiSetSystemPaletteUse, (HDC hdc, ULONG use));
UDECL(BOOL,   NtGdiSetColorAdjustment, (HDC hdc, const void *pca));
UDECL(BOOL,   NtGdiGetColorAdjustment, (HDC hdc, void *pca));
UDECL(BOOL,   NtGdiDeleteObjectApp, (HANDLE h));

/* iFunc selectors of NtGdiDoPalette: gdi32full!AnimatePalette passes 0 (inbound), SetPaletteEntries 1 (inbound),
 * GetSystemPaletteEntries 3 (outbound); GetPaletteEntries is the remaining outbound slot, 2. */
enum { PAL_ANIMATE = 0, PAL_SET = 1, PAL_GET = 2, PAL_GETSYS = 3 };

static void *pb_base(void) {
    void *p = NULL;
    if (!k_try_read_ptr((char *)k_peb() + PEB_GdiSharedHandleTable, &p)) return NULL;
    return p;
}
static int pb_cell_at(unsigned idx, struct pb_cell *c) {
    void *b = pb_base();
    if (!b || idx >= 0x10000) return 0;
    return k_try_read((char *)b + (ULONG_PTR)idx * GDI_CELL_SIZE, c, sizeof *c);
}
static int pb_cell_of(HGDIOBJ h, struct pb_cell *c) { return pb_cell_at(PB_IDX(h), c); }
/* Connect to win32k (loads gdi32/user32 lazily) and see whether the shared table is readable. */
static int pb_connect(void) {
    struct pb_cell c;
    if (!U(GetStockObject) || !U(GetObjectW)) return 0;
    U(GetStockObject)(BLACK_PEN);
    return pb_base() != NULL && pb_cell_at(0, &c);
}
#define PB_TABLE() do { if (!pb_connect()) { t_skip("PEB->GdiSharedHandleTable unavailable"); return; } } while (0)
#define PB_GDI()   do { if (!U(GetStockObject) || !U(GetObjectW) || !U(DeleteObject) || !U(CreateCompatibleDC)) { t_skip("gdi32 unavailable"); return; } } while (0)

static int pb_getobj(HGDIOBJ h, int cb, void *buf) { return U(GetObjectW)(h, cb, buf); }
static BOOL pb_del(HGDIOBJ h) { return h ? U(DeleteObject)(h) : FALSE; }

/* Brush-type pUser (BRUSHATTR) pointer of an object; NULL if not readable. */
static void *pb_attr(HGDIOBJ h) {
    struct pb_cell c;
    if (!pb_cell_of(h, &c)) return NULL;
    return (void *)(ULONG_PTR)c.puser;
}

/* RASTERCAPS of the screen DC (RC_PALETTE set only on palette-managed 8bpp devices). */
static int pb_screen_has_palette(void) {
    HDC d = U(CreateDCW) ? U(CreateDCW)(L"DISPLAY", NULL, NULL, NULL) : NULL;
    int r = 0;
    if (d) { r = (U(GetDeviceCaps)(d, RASTERCAPS) & RC_PALETTE) != 0; U(DeleteDC)(d); }
    return r;
}

typedef struct { WORD ver; WORD n; PALETTEENTRY e[1]; } PB_LOGPAL_HDR;
/* build a LOGPALETTE with n entries in a malloc'd block */
static LOGPALETTE *pb_mkpal(unsigned n, WORD ver) {
    LOGPALETTE *lp = (LOGPALETTE *)calloc(1, sizeof(LOGPALETTE) + (n ? n - 1 : 0) * sizeof(PALETTEENTRY) + 8);
    if (!lp) return NULL;
    lp->palVersion = ver; lp->palNumEntries = (WORD)n;
    for (unsigned i = 0; i < n; i++) {
        lp->palPalEntry[i].peRed = (BYTE)(i * 7 + 1); lp->palPalEntry[i].peGreen = (BYTE)(i * 13 + 2);
        lp->palPalEntry[i].peBlue = (BYTE)(i * 29 + 3); lp->palPalEntry[i].peFlags = 0;
    }
    return lp;
}
#endif
