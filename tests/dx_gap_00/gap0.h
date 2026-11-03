/* Shared declarations and helpers for the gap_00 / dx_gap_00 areas (direct win32u syscalls with no other coverage).
 *
 * Every signature below is derived from the win32kfull/win32kbase analysis of the syscall body (parameter
 * use, probes, copy-in/out) and from the gdi32full callers. DC_ATTR and GDICELL have no PDB type on this corpus,
 * so the offsets used here are the ones the kernel bodies dereference (DC+0x3d0 = DC_ATTR) and the ones the
 * gdi32full readers (GetColorSpace, GetDCDpiScaleValue, SetICMModeImpl) use off GDICELL.pUser. */
#ifndef GAP0_H
#define GAP0_H
#include "t.h"
#include "k.h"
#include <stdint.h>

/* The harness EXPECT_EQ evaluates operands twice; evaluate each side exactly once here because many operands are calls. */
#undef EXPECT_EQ
#define EXPECT_EQ(a, b) do { unsigned long long a_ = (unsigned long long)(ULONG_PTR)(a), b_ = (unsigned long long)(ULONG_PTR)(b); \
    t_check(a_ == b_, __FILE__, __LINE__, #a " == " #b, "got 0x%llx want 0x%llx", a_, b_); } while (0)

/* Wine's win32u exports most NtGdi* names as stubs that abort the whole process when called ("unimplemented function"),
 * and its gdi32 does the same for a few of the user-mode exports used here. Under Wine only the entry points known to be
 * implemented there (checked by running them) are resolved; every other UN() is NULL, so the REQUIRE() in the test skips it.
 * On Windows UN() is the plain lookup. */
static inline FARPROC g0_resolve(const char *n) {
    if (t_is_wine()) {
        static const char *const ok[] = { "NtGdiSetVirtualResolution", "NtGdiIcmBrushInfo", "NtGdiExtEscape", "NtGdiEndDoc" };
        int allowed = 0;
        for (unsigned i = 0; i < sizeof ok / sizeof *ok; i++) if (!strcmp(n, ok[i])) allowed = 1;
        if (!allowed) return NULL;
    }
    return t_proc(n);
}
#undef UN
#define UN(name) ((name##_pfn)g0_resolve(#name))

/* ---- win32u syscall stubs (not in any SDK header) ---------------------------------------------------- */
UDECL(LONG,      NtGdiGetDCDpiScaleValue, (HDC));
UDECL(INT,       NtGdiGetDeviceWidth, (HDC));
UDECL(INT,       NtGdiSetVirtualResolution, (HDC, INT, INT, INT, INT));
UDECL(INT,       NtGdiSetIcmMode, (HDC, ULONG, ULONG));
UDECL(INT,       NtGdiMakeInfoDC, (HDC, INT));
UDECL(HANDLE,    NtGdiCreateColorSpace, (void *logcolorspaceexw));
UDECL(INT,       NtGdiSetColorSpace, (HDC, HANDLE));
UDECL(HANDLE,    NtGdiCreateColorTransform, (HDC, void *lcs, void *, ULONG, void *, ULONG, void *, ULONG));
UDECL(INT,       NtGdiDeleteColorTransform, (HDC, HANDLE));
UDECL(INT,       NtGdiIcmBrushInfo, (HDC, HBRUSH, void *pbmi, void *pbits, ULONG *pcjBits, ULONG *pulColors, INT *pbValid, INT bSet));
UDECL(ULONG_PTR, NtGdiGetObjectBitmapHandle, (HGDIOBJ, ULONG *));
UDECL(INT,       NtGdiEndDoc, (HDC));
UDECL(INT,       NtGdiAbortDoc, (HDC));
UDECL(INT,       NtGdiEndPage, (HDC));
UDECL(INT,       NtGdiDoBanding, (HDC, INT, POINT *, SIZE *));
UDECL(ULONG,     NtGdiGetPerBandInfo, (HDC, void *));
UDECL(ULONG_PTR, NtGdiExtEscape, (HDC, const WCHAR *, INT, INT, INT, const void *, INT, void *));
UDECL(ULONG,     NtGdiGetUFIPathname, (void *ufi, ULONG *, WCHAR *, ULONG *, ULONG, INT *, ULONG *, void *, ULONG *, ULONG *));
UDECL(HBITMAP,   NtGdiCreateSessionMappedDIBSection, (HDC, HANDLE, ULONG, void *, ULONG, ULONG, ULONG, ULONG_PTR));
UDECL(INT,       NtGdiMakeObjectXferable, (HGDIOBJ, ULONG));
UDECL(INT,       NtGdiMakeObjectUnXferable, (HGDIOBJ));
UDECL(INT,       NtGdiAnyLinkedFonts, (void));
UDECL(INT,       NtGdiGetETM, (HDC, void *));
UDECL(ULONG_PTR, NtGdiGetMonitorID, (HDC, ULONG, void *));
UDECL(ULONG_PTR, NtGdiSetUMPDSandboxState, (ULONG));

/* user-mode gdi32 exports reaching them (not SDK-declared) */
UDECL(ULONG_PTR, GetDCDpiScaleValue, (HDC));
UDECL(BOOL,      SetVirtualResolution, (HDC, INT, INT, INT, INT));
UDECL(BOOL,      AnyLinkedFonts, (void));
UDECL(INT,       GetETM, (HDC, void *));

/* ---- GDI cell / DC_ATTR ------------------------------------------------------------------------------- */
#define CELL_UNIQUE 0x0c   /* ushort, compared with HIWORD(handle) by the handle-transfer marking path / GetColorSpace */
#define CELL_TYPE   0x0e   /* byte: object type (1 = DC) */
#define CELL_FLAGS  0x0f   /* byte: bit 0x20 = xferable (set when the object is marked transferable, cleared when unmarked) */
#define CELL_PUSER  0x10

#define DCA_COLORSPACE 0x010  /* HCOLORSPACE: GetColorSpace returns it; NtGdiSetColorSpace stores it (DC+0x3d0 -> +0x10) */
#define DCA_DPISCALE   0x088  /* DC::GetCachedDpiScaleValue returns *(DC_ATTR+0x88); gdi32full!GetDCDpiScaleValue reads the same */
#define DCA_ICMFLAGS   0x0f0  /* NtGdiSetIcmMode stores (old & 0xf000) | mode bits here */
#define DCA_VIRT_PX_CX 0x198  /* NtGdiSetVirtualResolution stores its 4 ints at +0x198..+0x1a4 */
#define DCA_VIRT_PX_CY 0x19c
#define DCA_VIRT_MM_CX 0x1a0
#define DCA_VIRT_MM_CY 0x1a4
#define DCA_MIN_READ   0x1a8

static inline unsigned long g0_rd32(const void *p) { unsigned long v = 0xDEADBEEFu; k_try_read_u32(p, &v); return v; }
static inline void *g0_rdp(const void *p) { void *v = NULL; k_try_read_ptr(p, &v); return v; }

static inline char *g0_cell(void *h) {
    if (t_is_wine()) return NULL;   /* Wine's tables are not the Windows layout */
    char *base = (char *)g0_rdp((char *)k_peb() + PEB_GdiSharedHandleTable);
    return base ? base + (ULONG_PTR)((ULONG_PTR)h & 0xffff) * GDI_CELL_SIZE : NULL;
}
/* GDICELL byte readers; returns -1 when the table is unreachable */
static inline int g0_cell_u8(void *h, unsigned off) {
    char *c = g0_cell(h); unsigned char v;
    if (!c || !k_try_read_u8(c + off, &v)) return -1;
    return v;
}
static inline int g0_cell_u16(void *h, unsigned off) {
    char *c = g0_cell(h); unsigned short v;
    if (!c || !k_try_read_u16(c + off, &v)) return -1;
    return v;
}
/* DC_ATTR page of a DC (GDICELL.pUser), after the same unique/type validation gdi32full does; NULL if unreachable */
static inline void *g0_dca(HDC h) {
    char *c = g0_cell(h);
    if (!c) return NULL;
    if (g0_cell_u8(h, CELL_TYPE) != 1) return NULL;
    if (g0_cell_u16(h, CELL_UNIQUE) != (int)(((ULONG_PTR)h >> 16) & 0xffff)) return NULL;
    void *u = g0_rdp(c + CELL_PUSER);
    unsigned char probe[DCA_MIN_READ];
    if (!u || !k_try_read(u, probe, sizeof probe)) return NULL;
    return u;
}
static inline int g0_dca_i32(void *a, unsigned off) { return (int)g0_rd32((char *)a + off); }

/* ---- fixtures ------------------------------------------------------------------------------------------ */
static inline HBITMAP g0_dib(HDC dc, int w, int h, int bpp) {
    BITMAPINFO bi; memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = (WORD)bpp; bi.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    return U(CreateDIBSection)(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
}

/* A memory DC with a private w x h DIB selected; fx_close deletes everything. */
struct g0fx { HDC dc; HBITMAP bmp, old; int w, h; };
static inline int g0fx_open(struct g0fx *f, int w, int h) {
    memset(f, 0, sizeof *f);
    if (!U(CreateCompatibleDC) || !U(CreateDIBSection) || !U(SelectObject) || !U(DeleteDC) || !U(DeleteObject)) return 0;
    f->dc = U(CreateCompatibleDC)(NULL);
    if (!f->dc) return 0;
    f->bmp = g0_dib(f->dc, w, h, 32);
    if (!f->bmp) { U(DeleteDC)(f->dc); f->dc = NULL; return 0; }
    f->old = (HBITMAP)U(SelectObject)(f->dc, f->bmp);
    f->w = w; f->h = h;
    return 1;
}
static inline void g0fx_close(struct g0fx *f) {
    if (f->dc && f->old) U(SelectObject)(f->dc, f->old);
    if (f->bmp) U(DeleteObject)(f->bmp);
    if (f->dc) U(DeleteDC)(f->dc);
    memset(f, 0, sizeof *f);
}
#define G0FX(f, w, h) struct g0fx f; if (!g0fx_open(&f, (w), (h))) { t_skip("cannot create memory DC with a DIB section"); return; }

/* A deleted DC handle: a once-valid handle whose cell has been released. */
static inline HDC g0_stale_dc(void) {
    HDC d = U(CreateCompatibleDC)(NULL);
    if (d) U(DeleteDC)(d);
    return d;
}

/* Handles the kernel's handle-lock paths must reject as DCs (index/type/unique mismatch). */
static const ULONG_PTR g0_bad_handles[] = { 0, 1, 0x7fff, 0x12345678, 0xdeadbeef, 0xffffffff };
#define G0_NBAD (sizeof g0_bad_handles / sizeof *g0_bad_handles)

static inline int g0_all(const void *p, size_t n, unsigned char v) {
    const unsigned char *b = (const unsigned char *)p;
    for (size_t i = 0; i < n; i++) if (b[i] != v) return 0;
    return 1;
}
#define G0_ERR_CANNOT_COMPLETE 1003   /* 0x3eb: bEndDocInternal's EngSetLastError on every rejected DC */
#endif
