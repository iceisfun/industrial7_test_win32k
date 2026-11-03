/* gdi_handle_table helpers: decode HGDIOBJ values and the PEB->GdiSharedHandleTable cells.
 *
 * Layout of one cell (24 bytes, win32k.sys!_ENTRY from the PDB, 19041): einfo @0 (8), ObjectOwner @8
 * (4: bit0 = lock, bits1..31 = pid>>1, so pid = owner & ~1 as every gdi32full getter compares
 * `(owner & 0xfffffffe) == gW32PID`), UpperHandleBits @0xc (2), Objt @0xe (1), Flags @0xf (1), pUser @0x10 (8).
 * Handle value (win32kbase!ENTRYOBJ::hSetup): (int)(upper << 16) | index16, so bits 16..22 = type,
 * bit 23 = stock flag, bits 24..31 = reuse counter (the byte at cell+0xd).
 */
#ifndef R2_H
#define R2_H
#include "t.h"
#include "k.h"
#include <wingdi.h>

#define GH_CELLS   0x10000u
#define GH_STOCK   0x00800000u
#define GH_KERNEL_FLOOR 0xFFFF800000000000ull

struct gh_cell {
    unsigned long long q0;     /* +0x00 */
    unsigned int       owner;  /* +0x08 */
    unsigned short     upper;  /* +0x0c */
    unsigned char      objt;   /* +0x0e */
    unsigned char      flags;  /* +0x0f */
    unsigned long long puser;  /* +0x10 */
};
typedef char gh_cell_size_must_be_24[(sizeof(struct gh_cell) == 24) ? 1 : -1];

#define GH_IDX(h)     ((unsigned)((ULONG_PTR)(h) & 0xffff))
#define GH_TYPE7(h)   ((unsigned)(((ULONG_PTR)(h) >> 16) & 0x7f))
#define GH_UNIQ(h)    ((unsigned)(((ULONG_PTR)(h) >> 24) & 0xff))
#define GH_STOCKED(h) (((ULONG_PTR)(h) & GH_STOCK) != 0)
#define GH_HIWORD(h)  ((unsigned)(((ULONG_PTR)(h) >> 16) & 0xffff))
#define GH_PID()      ((unsigned)GetCurrentProcessId())

static void *gh_base(void) {
    void *p = NULL;
    if (!k_try_read_ptr((char *)k_peb() + PEB_GdiSharedHandleTable, &p)) return NULL;
    return p;
}
static int gh_read(unsigned idx, struct gh_cell *c) {
    void *b = gh_base();
    if (!b || idx >= GH_CELLS) return 0;
    return k_try_read((char *)b + (ULONG_PTR)idx * GDI_CELL_SIZE, c, sizeof *c);
}
/* Make sure gdi32 is loaded and this process is connected, then check the table is readable. */
static int gh_connect(void) {
    struct gh_cell c;
    if (!U(GetStockObject)) return 0;
    U(GetStockObject)(BLACK_PEN);
    return gh_base() != NULL && gh_read(0, &c);
}
#define GH_NEED() do { if (!gh_connect()) { t_skip("PEB->GdiSharedHandleTable unavailable"); return; } } while (0)

/* HANDLE_TO_INDEX (gdi32full): the 24-bit form only when the reuse byte does not match the cell. */
static int gh_cell_of(HGDIOBJ h, struct gh_cell *c) {
    unsigned lo = GH_IDX(h), ext = (unsigned)(((ULONG_PTR)h >> 8) & 0xff0000) | lo;
    struct gh_cell t;
    if (ext < GH_CELLS) return gh_read(lo, c);
    if (gh_read(lo, &t) && GH_UNIQ(h) == (unsigned)(t.upper >> 8)) return gh_read(lo, c);
    return 0;
}

/* Number of live cells whose owner is `pid` (objt != 0), scanning all 0x10000 cells.
 * Returns -1 if part of the table is unreadable. */
static int gh_count_owned(unsigned pid) {
    void *b = gh_base();
    static struct gh_cell buf[1024];
    int n = 0;
    if (!b) return -1;
    for (unsigned i = 0; i < GH_CELLS; i += 1024) {
        if (!k_try_read((char *)b + (ULONG_PTR)i * GDI_CELL_SIZE, buf, sizeof buf)) return -1;
        for (unsigned j = 0; j < 1024; j++)
            if (buf[j].objt != 0 && (buf[j].owner & ~1u) == pid) n++;
    }
    return n;
}

/* ---- creating objects through the normal gdi32 paths ----------------------------------- */
typedef HGDIOBJ (*gh_mk_fn)(void);
typedef void    (*gh_rm_fn)(HGDIOBJ);
struct gh_kind {
    const char *name;
    unsigned type7;    /* (handle >> 16) & 0x7f, from GetObjectType's switch in gdi32full */
    int      objt;     /* expected cell Objt: type7 & 0x1f (InternalDeleteObject), 6 for client objects; -1 = not asserted */
    DWORD    objtype;  /* GetObjectType() result */
    int      puser;    /* 1 = cell pUser must be non-NULL, 0 = must be NULL, -1 = only noted */
    int      nh;       /* GDI handles the object consumes (GR_GDIOBJECTS delta); -1 = not asserted */
    gh_mk_fn mk;
    gh_rm_fn rm;
};

static HDC gh_disp(void) { return U(CreateDCW) ? U(CreateDCW)(L"DISPLAY", NULL, NULL, NULL) : NULL; }

static HGDIOBJ mk_dc_memory(void) { return U(CreateCompatibleDC) ? (HGDIOBJ)U(CreateCompatibleDC)(NULL) : NULL; }
static HGDIOBJ mk_dc_display(void) { return gh_disp(); }
static HGDIOBJ mk_dc_info(void) { return U(CreateICW) ? (HGDIOBJ)U(CreateICW)(L"DISPLAY", NULL, NULL, NULL) : NULL; }
static HGDIOBJ mk_dc_compat_disp(void) {
    HDC d = gh_disp(), m;
    if (!d || !U(CreateCompatibleDC)) return NULL;
    m = U(CreateCompatibleDC)(d);
    U(DeleteDC)(d);
    return m;
}
static HGDIOBJ mk_dc_getdc(void) { return (U(GetDC) ? (HGDIOBJ)U(GetDC)(NULL) : NULL); }
static void rm_dc(HGDIOBJ h) { U(DeleteDC)((HDC)h); }
static void rm_getdc(HGDIOBJ h) { U(ReleaseDC)(NULL, (HDC)h); }
static void rm_obj(HGDIOBJ h) { U(DeleteObject)(h); }

static HGDIOBJ mk_rgn_rect(void) { return U(CreateRectRgn) ? (HGDIOBJ)U(CreateRectRgn)(0, 0, 10, 10) : NULL; }
static HGDIOBJ mk_rgn_ellip(void) { return U(CreateEllipticRgn) ? (HGDIOBJ)U(CreateEllipticRgn)(0, 0, 10, 10) : NULL; }
static HGDIOBJ mk_rgn_round(void) { return U(CreateRoundRectRgn) ? (HGDIOBJ)U(CreateRoundRectRgn)(0, 0, 20, 20, 4, 4) : NULL; }
static HGDIOBJ mk_rgn_poly(void) {
    POINT p[3] = { {0, 0}, {10, 0}, {5, 10} };
    return U(CreatePolygonRgn) ? (HGDIOBJ)U(CreatePolygonRgn)(p, 3, WINDING) : NULL;
}

static HGDIOBJ mk_bmp_mono(void) { return U(CreateBitmap) ? (HGDIOBJ)U(CreateBitmap)(1, 1, 1, 1, NULL) : NULL; }
static HGDIOBJ mk_bmp_color(void) { return U(CreateBitmap) ? (HGDIOBJ)U(CreateBitmap)(4, 4, 1, 32, NULL) : NULL; }
static HGDIOBJ mk_bmp_compat(void) {
    HDC d = gh_disp(); HBITMAP b;
    if (!d || !U(CreateCompatibleBitmap)) return NULL;
    b = U(CreateCompatibleBitmap)(d, 8, 8);
    U(DeleteDC)(d);
    return b;
}
static HGDIOBJ mk_dib(int w, int h, int bpp) {
    BITMAPINFO bi; void *bits = NULL;
    if (!U(CreateDIBSection)) return NULL;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = (WORD)bpp; bi.bmiHeader.biCompression = BI_RGB;
    return U(CreateDIBSection)(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
}
static HGDIOBJ mk_dib24(void) { return mk_dib(8, 8, 24); }
static HGDIOBJ mk_dib32td(void) { return mk_dib(8, -8, 32); }
static HGDIOBJ mk_bmp_dibitmap(void) {
    BITMAPINFOHEADER bh; HDC d; HBITMAP b;
    if (!U(CreateDIBitmap)) return NULL;
    d = gh_disp();
    if (!d) return NULL;
    memset(&bh, 0, sizeof bh);
    bh.biSize = sizeof bh; bh.biWidth = 4; bh.biHeight = 4; bh.biPlanes = 1; bh.biBitCount = 24;
    b = U(CreateDIBitmap)(d, &bh, 0, NULL, NULL, DIB_RGB_COLORS);
    U(DeleteDC)(d);
    return b;
}

static HGDIOBJ mk_pal_logical(void) {
    struct { WORD v, n; PALETTEENTRY e[2]; } lp = { 0x300, 2, { {255, 0, 0, 0}, {0, 255, 0, 0} } };
    return U(CreatePalette) ? (HGDIOBJ)U(CreatePalette)((LOGPALETTE *)&lp) : NULL;
}
static HGDIOBJ mk_pal_halftone(void) {
    HDC d = gh_disp(); HPALETTE p;
    if (!d || !U(CreateHalftonePalette)) return NULL;
    p = U(CreateHalftonePalette)(d);
    U(DeleteDC)(d);
    return p;
}

static HGDIOBJ mk_font_indirect(void) {
    LOGFONTW lf;
    if (!U(CreateFontIndirectW)) return NULL;
    memset(&lf, 0, sizeof lf);
    lf.lfHeight = -12; lf.lfWeight = FW_NORMAL; lstrcpyW(lf.lfFaceName, L"Arial");
    return U(CreateFontIndirectW)(&lf);
}
static HGDIOBJ mk_font_w(void) {
    return U(CreateFontW) ? (HGDIOBJ)U(CreateFontW)(-16, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Tahoma") : NULL;
}

static HGDIOBJ mk_brush_solid(void) { return U(CreateSolidBrush) ? (HGDIOBJ)U(CreateSolidBrush)(RGB(10, 20, 30)) : NULL; }
static HGDIOBJ mk_brush_hatch(void) { return U(CreateHatchBrush) ? (HGDIOBJ)U(CreateHatchBrush)(HS_CROSS, RGB(1, 2, 3)) : NULL; }
static HGDIOBJ mk_brush_pattern(void) {
    HBITMAP b; HBRUSH r;
    if (!U(CreatePatternBrush) || !U(CreateBitmap)) return NULL;
    b = U(CreateBitmap)(8, 8, 1, 1, NULL);
    if (!b) return NULL;
    r = U(CreatePatternBrush)(b);
    U(DeleteObject)(b);
    return r;
}
static HGDIOBJ mk_brush_dibpat(void) {
    struct { BITMAPINFOHEADER h; DWORD px[4]; } bi;
    if (!U(CreateDIBPatternBrushPt)) return NULL;
    memset(&bi, 0, sizeof bi);
    bi.h.biSize = sizeof bi.h; bi.h.biWidth = 2; bi.h.biHeight = 2; bi.h.biPlanes = 1; bi.h.biBitCount = 32;
    return U(CreateDIBPatternBrushPt)(&bi, DIB_RGB_COLORS);
}
static HGDIOBJ mk_pen_solid(void) { return U(CreatePen) ? (HGDIOBJ)U(CreatePen)(PS_SOLID, 1, RGB(9, 9, 9)) : NULL; }
static HGDIOBJ mk_pen_indirect(void) {
    LOGPEN lp = { PS_DASH, { 1, 0 }, RGB(1, 1, 1) };
    return U(CreatePenIndirect) ? (HGDIOBJ)U(CreatePenIndirect)(&lp) : NULL;
}
static HGDIOBJ mk_extpen_geom(void) {
    LOGBRUSH lb = { BS_SOLID, RGB(5, 6, 7), 0 };
    return U(ExtCreatePen) ? (HGDIOBJ)U(ExtCreatePen)(PS_GEOMETRIC | PS_SOLID, 3, &lb, 0, NULL) : NULL;
}
static HGDIOBJ mk_extpen_cosm(void) {
    LOGBRUSH lb = { BS_SOLID, RGB(5, 6, 7), 0 };
    return U(ExtCreatePen) ? (HGDIOBJ)U(ExtCreatePen)(PS_COSMETIC | PS_SOLID, 1, &lb, 0, NULL) : NULL;
}
static HGDIOBJ mk_colorspace(void) {
    LOGCOLORSPACEW lcs;
    if (!U(CreateColorSpaceW)) return NULL;
    memset(&lcs, 0, sizeof lcs);
    lcs.lcsSignature = 0x50534F43; /* 'PSOC' */ lcs.lcsVersion = 0x400; lcs.lcsSize = sizeof lcs;
    lcs.lcsCSType = LCS_CALIBRATED_RGB; lcs.lcsIntent = LCS_GM_IMAGES;
    return U(CreateColorSpaceW)(&lcs);
}
static void rm_colorspace(HGDIOBJ h) { U(DeleteColorSpace)((HCOLORSPACE)h); }

static HGDIOBJ mk_emf_dc(void) { return U(CreateEnhMetaFileW) ? (HGDIOBJ)U(CreateEnhMetaFileW)(NULL, NULL, NULL, NULL) : NULL; }
static void rm_emf_dc(HGDIOBJ h) { HENHMETAFILE e = U(CloseEnhMetaFile)((HDC)h); if (e) U(DeleteEnhMetaFile)(e); }
static HGDIOBJ mk_emf(void) {
    HDC d = U(CreateEnhMetaFileW) ? U(CreateEnhMetaFileW)(NULL, NULL, NULL, NULL) : NULL;
    return d ? (HGDIOBJ)U(CloseEnhMetaFile)(d) : NULL;
}
static void rm_emf(HGDIOBJ h) { U(DeleteEnhMetaFile)((HENHMETAFILE)h); }
static HGDIOBJ mk_wmf_dc(void) { return U(CreateMetaFileW) ? (HGDIOBJ)U(CreateMetaFileW)(NULL) : NULL; }
static void rm_wmf_dc(HGDIOBJ h) { HMETAFILE m = U(CloseMetaFile)((HDC)h); if (m) U(DeleteMetaFile)(m); }
static HGDIOBJ mk_wmf(void) {
    HDC d = U(CreateMetaFileW) ? U(CreateMetaFileW)(NULL) : NULL;
    return d ? (HGDIOBJ)U(CloseMetaFile)(d) : NULL;
}
static void rm_wmf(HGDIOBJ h) { U(DeleteMetaFile)((HMETAFILE)h); }

/* One definition per kind; the X-list drives the generated per-kind tests. */
#define GH_KIND(id, t7, objt, ot, pu, nh, mk, rm) \
    static const struct gh_kind gk_##id = { #id, t7, objt, ot, pu, nh, mk, rm };
GH_KIND(dc_memory,      0x01, 1, OBJ_MEMDC,  1,  1, mk_dc_memory,      rm_dc)
GH_KIND(dc_display,     0x01, 1, OBJ_DC,     1,  1, mk_dc_display,     rm_dc)
GH_KIND(dc_info,        0x01, 1, OBJ_DC,     1,  1, mk_dc_info,        rm_dc)
GH_KIND(dc_compat_disp, 0x01, 1, OBJ_MEMDC,  1,  1, mk_dc_compat_disp, rm_dc)
/* gk_dc_getdc is not in GH_KINDS: the DC belongs to USER's DC cache, so it must never be passed to DeleteObject/DeleteDC */
GH_KIND(dc_getdc,       0x01, 1, OBJ_DC,     1, -1, mk_dc_getdc,       rm_getdc)
GH_KIND(rgn_rect,       0x04, 4, OBJ_REGION, 1,  1, mk_rgn_rect,       rm_obj)
GH_KIND(rgn_ellip,      0x04, 4, OBJ_REGION, -1, 1, mk_rgn_ellip,      rm_obj)
GH_KIND(rgn_round,      0x04, 4, OBJ_REGION, -1, 1, mk_rgn_round,      rm_obj)
GH_KIND(rgn_poly,       0x04, 4, OBJ_REGION, -1, 1, mk_rgn_poly,       rm_obj)
GH_KIND(bmp_mono,       0x05, 5, OBJ_BITMAP, 0,  1, mk_bmp_mono,       rm_obj)
GH_KIND(bmp_color,      0x05, 5, OBJ_BITMAP, 0,  1, mk_bmp_color,      rm_obj)
GH_KIND(bmp_compat,     0x05, 5, OBJ_BITMAP, 0,  1, mk_bmp_compat,     rm_obj)
GH_KIND(bmp_dib24,      0x05, 5, OBJ_BITMAP, -1, 1, mk_dib24,          rm_obj)
GH_KIND(bmp_dib32td,    0x05, 5, OBJ_BITMAP, -1, 1, mk_dib32td,        rm_obj)
GH_KIND(bmp_dibitmap,   0x05, 5, OBJ_BITMAP, 0,  1, mk_bmp_dibitmap,   rm_obj)
GH_KIND(pal_logical,    0x08, 8, OBJ_PAL,    0,  1, mk_pal_logical,    rm_obj)
GH_KIND(pal_halftone,   0x08, 8, OBJ_PAL,    0,  1, mk_pal_halftone,   rm_obj)
GH_KIND(font_indirect,  0x0a, 10, OBJ_FONT,  0,  1, mk_font_indirect,  rm_obj)
GH_KIND(font_w,         0x0a, 10, OBJ_FONT,  0,  1, mk_font_w,         rm_obj)
GH_KIND(brush_solid,    0x10, 16, OBJ_BRUSH, -1, 1, mk_brush_solid,    rm_obj)
GH_KIND(brush_hatch,    0x10, 16, OBJ_BRUSH, -1, 1, mk_brush_hatch,    rm_obj)
/* the kernel keeps a private copy of the pattern bitmap; whether that copy consumes an extra handle is not asserted */
GH_KIND(brush_pattern,  0x10, 16, OBJ_BRUSH, -1, -1, mk_brush_pattern,  rm_obj)
GH_KIND(brush_dibpat,   0x10, 16, OBJ_BRUSH, -1, -1, mk_brush_dibpat,   rm_obj)
GH_KIND(pen_solid,      0x30, 16, OBJ_PEN,   -1, 1, mk_pen_solid,      rm_obj)
GH_KIND(pen_indirect,   0x30, 16, OBJ_PEN,   -1, 1, mk_pen_indirect,   rm_obj)
GH_KIND(extpen_geom,    0x50, 16, OBJ_EXTPEN, -1, 1, mk_extpen_geom,   rm_obj)
GH_KIND(extpen_cosm,    0x50, 16, OBJ_EXTPEN, -1, 1, mk_extpen_cosm,   rm_obj)
GH_KIND(colorspace,     0x09, 9, OBJ_COLORSPACE, -1, 1, mk_colorspace, rm_colorspace)
GH_KIND(emf_dc,         0x21, -1, OBJ_ENHMETADC, -1, -1, mk_emf_dc,    rm_emf_dc)
GH_KIND(emf,            0x46, 6, OBJ_ENHMETAFILE, -1, -1, mk_emf,      rm_emf)
GH_KIND(wmf_dc,         0x66, -1, OBJ_METADC, -1, -1, mk_wmf_dc,       rm_wmf_dc)
GH_KIND(wmf,            0x26, 6, OBJ_METAFILE, -1, -1, mk_wmf,         rm_wmf)

#define GH_KINDS(X) \
    X(dc_memory) X(dc_display) X(dc_info) X(dc_compat_disp) \
    X(rgn_rect) X(rgn_ellip) X(rgn_round) X(rgn_poly) \
    X(bmp_mono) X(bmp_color) X(bmp_compat) X(bmp_dib24) X(bmp_dib32td) X(bmp_dibitmap) \
    X(pal_logical) X(pal_halftone) X(font_indirect) X(font_w) \
    X(brush_solid) X(brush_hatch) X(brush_pattern) X(brush_dibpat) \
    X(pen_solid) X(pen_indirect) X(extpen_geom) X(extpen_cosm) X(colorspace) \
    X(emf_dc) X(emf) X(wmf_dc) X(wmf)
#define GH_KIND_PTR(id) &gk_##id,
static const struct gh_kind *const gh_all_kinds[] = { GH_KINDS(GH_KIND_PTR) };
#define GH_NKINDS (sizeof gh_all_kinds / sizeof gh_all_kinds[0])

/* Create one object of a kind or skip the calling test. */
#define GH_MAKE(var, k) \
    HGDIOBJ var = (k)->mk(); \
    if (!var) { t_skip("cannot create %s (err %lu)", (k)->name, GetLastError()); return; }

/* ---- syscall declarations (win32kbase/win32kfull analysis) -------------------------------- */
UDECL(LONG,   NtGdiGetEntry,       (ULONG index, void *out24));            /* NTSTATUS, copies the 24-byte cell */
UDECL(ULONG_PTR, NtGdiDeleteObjectApp, (HANDLE h));                          /* BOOL-like */
UDECL(HANDLE, NtGdiGetDCObject,    (HANDLE hdc, int type_bits));
UDECL(ULONG,  NtGdiGetDCDword,     (HANDLE hdc, int index, ULONG *out));
UDECL(HANDLE, NtGdiCreateClientObj, (ULONG type_bits));
UDECL(ULONG_PTR, NtGdiDeleteClientObj, (HANDLE h));
UDECL(HANDLE, NtGdiCreateRectRgn,  (int l, int t, int r, int b));
UDECL(ULONG,  NtUserGetGuiResources, (HANDLE hproc, ULONG flags));
UDECL(ULONG,  NtGdiMakeObjectXferable,   (HANDLE hdc, ULONG pid));
UDECL(ULONG,  NtGdiMakeObjectUnXferable, (HANDLE h));

/* client objects created straight through the syscall (type7 0x26 metafile, 0x46 enhanced metafile, 0x66 metafile DC) */
static HGDIOBJ mk_cobj_t7(unsigned t7) { return (UN(NtGdiCreateClientObj) && !t_is_wine()) ? (HGDIOBJ)UN(NtGdiCreateClientObj)(t7 << 16) : NULL; }
static HGDIOBJ mk_cobj_meta(void)   { return mk_cobj_t7(0x26); }
static HGDIOBJ mk_cobj_emf(void)    { return mk_cobj_t7(0x46); }
static HGDIOBJ mk_cobj_metadc(void) { return mk_cobj_t7(0x66); }
static void rm_cobj(HGDIOBJ h) { if (UN(NtGdiDeleteClientObj)) UN(NtGdiDeleteClientObj)((HANDLE)h); }
GH_KIND(cobj_meta,   0x26, 6, OBJ_METAFILE,    0, -1, mk_cobj_meta,   rm_cobj)
GH_KIND(cobj_emf,    0x46, 6, OBJ_ENHMETAFILE, 0, -1, mk_cobj_emf,    rm_cobj)
GH_KIND(cobj_metadc, 0x66, 6, OBJ_METADC,      0, -1, mk_cobj_metadc, rm_cobj)
#define R2_KINDS(X) GH_KINDS(X) X(cobj_meta) X(cobj_emf) X(cobj_metadc)

/* Wine's win32u marks these as unimplemented stubs that abort the whole process when called. */
static int gh_wine_missing(const char *n) {
    return t_is_wine() && (!strcmp(n, "NtGdiGetEntry") || !strcmp(n, "NtUserGetGuiResources"));
}
#define GHN(name) (gh_wine_missing(#name) ? (name##_pfn)NULL : UN(name))


/* ---- child-process support ------------------------------------------------------------ */
static int gh_child_fails;
#define CC(c) do { if (!(c)) { gh_child_fails++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CCF(c, ...) do { if (!(c)) { gh_child_fails++; printf("  FAIL %s:%d: %s (", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf(")\n"); } } while (0)

/* Block shared between a test and its child through a named file mapping. */
struct gh_shm {
    unsigned parent_pid, child_pid;
    volatile LONG ready;
    unsigned nph;
    unsigned long long ph[16];       /* parent handles */
    unsigned ph_type[16];            /* their (handle>>16)&0x7f */
    unsigned long long stock[24];    /* GetStockObject(i) in the parent */
    unsigned long long ch[64];       /* child handles */
    unsigned nch;
    unsigned child_gr;               /* GR_GDIOBJECTS the child saw for itself */
};
static void gh_shm_name(char *out, size_t n, unsigned ppid, const char *what) { snprintf(out, n, "Local\\win32kt_gh_%s_%u", what, ppid); }
static struct gh_shm *gh_shm_open(unsigned ppid, int create, HANDLE *hmap) {
    char nm[96]; struct gh_shm *p;
    gh_shm_name(nm, sizeof nm, ppid, "shm");
    *hmap = create ? CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(struct gh_shm), nm)
                   : OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, nm);
    if (!*hmap) return NULL;
    p = (struct gh_shm *)MapViewOfFile(*hmap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(struct gh_shm));
    return p;
}


/* ======================================================================================
 * r2_gdi_cell_per_type helpers
 *
 * PEB handle cache (win32kbase!bPEBCacheHandle writes it, gdi32!hGetPEBHandle / gdi32!CreateRectRgn read it):
 *   PEB+0x140  qword   cache lock (owner TEB while one side is working, 0 at rest)
 *   PEB+0x148  dword[3] number of cached handles per type (0 = solid brush, 1 = pen-like, 2 = region)
 *   PEB+0x158  qword[27] cached handle values: (cell.upper << 16) | index, zero-extended
 * A cached object keeps its handle-table cell alive (Objt != 0, owner == pid); only the reuse byte is bumped
 * and OBJECTATTR.AttrFlags bit 0 is set.
 * ====================================================================================== */
#define R2_PEB_CACHE   0x140
#define R2_CACHE_SLOTS 27
struct r2_cache {
    unsigned long long lock;
    unsigned           count[3];
    unsigned           pad;       /* PEB+0x154: count slot of a possible fourth type, 0 here */
    unsigned long long slot[R2_CACHE_SLOTS];
};
typedef char r2_cache_size_must_be_0xf0[(sizeof(struct r2_cache) == 0xf0) ? 1 : -1];
static int r2_cache_read(struct r2_cache *c) {
    return k_try_read((char *)k_peb() + R2_PEB_CACHE, c, sizeof *c);
}
static int r2_cache_find(const struct r2_cache *c, unsigned idx) {
    for (int i = 0; i < R2_CACHE_SLOTS; i++)
        if (c->slot[i] && (unsigned)(c->slot[i] & 0xffff) == idx) return i;
    return -1;
}
static int r2_cache_nonzero(const struct r2_cache *c) {
    int n = 0;
    for (int i = 0; i < R2_CACHE_SLOTS; i++) n += c->slot[i] != 0;
    return n;
}
#define R2_T_BRUSH  0
#define R2_T_REGION 2

/* OBJECTATTR dwords behind a cell's pUser */
static int r2_attr(const struct gh_cell *c, unsigned dword_index, unsigned *v) {
    return c->puser && k_try_read((void *)(ULONG_PTR)(c->puser + dword_index * 4ull), v, 4);
}

/* Live cells owned by `pid`, split by Objt&0x1f (cnt[] must hold 32). Returns total or -1. */
static int r2_scan(unsigned pid, int cnt[32]) {
    void *b = gh_base();
    static struct gh_cell buf[1024];
    int n = 0;
    if (!b) return -1;
    memset(cnt, 0, 32 * sizeof(int));
    for (unsigned i = 0; i < GH_CELLS; i += 1024) {
        if (!k_try_read((char *)b + (ULONG_PTR)i * GDI_CELL_SIZE, buf, sizeof buf)) return -1;
        for (unsigned j = 0; j < 1024; j++)
            if (buf[j].objt != 0 && (buf[j].owner & ~1u) == pid) { n++; cnt[buf[j].objt & 0x1f]++; }
    }
    return n;
}
static int r2_gr(int flags) { return U(GetGuiResources) ? (int)U(GetGuiResources)(GetCurrentProcess(), flags) : -1; }

/* Empty the PEB cache of one type by creating objects until it reports zero cached handles.
 * Returned objects are live; the caller deletes them (which refills the cache). */
static int r2_drain(int type, HGDIOBJ *held, int cap) {
    struct r2_cache c;
    int n = 0;
    while (n < cap && r2_cache_read(&c) && c.count[type] > 0) {
        HGDIOBJ h = type == R2_T_REGION ? (HGDIOBJ)U(CreateRectRgn)(0, 0, 1, 1) : (HGDIOBJ)U(CreateSolidBrush)(RGB(0, 0, 0));
        if (!h) break;
        held[n++] = h;
    }
    return n;
}
/* On Windows 10 DeleteObject of a solid brush or rectangular region is queued in the GDI batch: the kernel parks (or frees) the
 * cell, bumps its reuse byte and fills the PEB cache only when the batch is flushed (next unbatched GDI call, or GdiFlush).
 * Until then the cell still shows the old reuse byte and the attribute block carries the pending-delete bit (AttrFlags 8).
 * r2_del is DeleteObject followed by that flush. */
static BOOL r2_del(HGDIOBJ h) { BOOL r = U(DeleteObject)(h); U(GdiFlush)(); return r; }
static void r2_release(HGDIOBJ *held, int n) { for (int i = 0; i < n; i++) U(DeleteObject)(held[i]); U(GdiFlush)(); }
static unsigned r2_lo32(void *h) { return (unsigned)(ULONG_PTR)h; }

#endif
