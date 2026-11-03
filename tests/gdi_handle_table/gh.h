/* gdi_handle_table helpers: decode HGDIOBJ values and the PEB->GdiSharedHandleTable cells.
 *
 * Layout of one cell (24 bytes, win32k.sys!_ENTRY from the PDB, 19041): einfo @0 (8), ObjectOwner @8
 * (4: bit0 = lock, bits1..31 = pid>>1, so pid = owner & ~1 as every gdi32full getter compares
 * `(owner & 0xfffffffe) == gW32PID`), UpperHandleBits @0xc (2), Objt @0xe (1), Flags @0xf (1), pUser @0x10 (8).
 * Handle value (win32kbase!ENTRYOBJ::hSetup): (int)(upper << 16) | index16, so bits 16..22 = type,
 * bit 23 = stock flag, bits 24..31 = reuse counter (the byte at cell+0xd).
 */
#ifndef GH_H
#define GH_H
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
GH_KIND(font_indirect,  0x0a, 10, OBJ_FONT,  1,  1, mk_font_indirect,  rm_obj)
GH_KIND(font_w,         0x0a, 10, OBJ_FONT,  1,  1, mk_font_w,         rm_obj)
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

/* Windows 10 keeps freed solid brushes and rectangular regions (the kinds with a client attribute block) in a small cache:
 * DeleteObject leaves the cell, its pUser block and the GDI handle count as they were and the next create of that kind takes
 * the cell over under a bumped reuse byte. The cache holds a few entries (9 brushes, 7 regions were observed); once it is
 * full a delete really frees. gh_cache_fill() leaves a kind's cache full; gh_cache_hold() takes the cached entries out. */
#define GH_CACHE_PROBE 24
static int gh_cached_kind(const struct gh_kind *k) { return !strcmp(k->name, "brush_solid") || !strcmp(k->name, "rgn_rect"); }
static void gh_cache_fill(const struct gh_kind *k) {
    HGDIOBJ t[GH_CACHE_PROBE]; int n = 0;
    for (; n < GH_CACHE_PROBE; n++) { t[n] = k->mk(); if (!t[n]) break; }
    for (int i = 0; i < n; i++) k->rm(t[i]);
}
/* create GH_CACHE_PROBE objects and keep them: afterwards the cache of the kind is empty and every create is a fresh cell */
static int gh_cache_hold(const struct gh_kind *k, HGDIOBJ *keep) {
    int n = 0;
    for (; n < GH_CACHE_PROBE; n++) { keep[n] = k->mk(); if (!keep[n]) break; }
    return n;
}
#define GH_CACHE_MAX 16     /* an upper bound for what the caches can hold back */

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

#endif
