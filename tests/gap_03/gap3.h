/* Shared declarations for the gap_03 area: direct win32u.dll syscall stubs that no other area calls.
 *
 * Signatures below are derived from the 19041 win32kfull/win32kbase analysis of each Nt* body
 * (probe sites, copy-in/out sizes, callee parameter use) and from the user32/gdi32full call sites;
 * each test file cites the analysis branch behind every hard CHECK.
 */
#ifndef GAP3_H
#define GAP3_H
#include "t.h"
#include "k.h"

/* ---- gdi ---- */
UDECL(BOOL,  NtGdiGetMiterLimit,        (HDC, DWORD *));            /* GetMiterLimit(hdc, float*) */
UDECL(BOOL,  NtGdiGetRasterizerCaps,    (void *, UINT));            /* (RASTERIZER_STATUS*, cb) */
UDECL(BOOL,  NtGdiScaleRgn,             (HDC, HRGN));               /* scale region(hdc, hrgn) */
UDECL(BOOL,  NtGdiScaleValues,          (HDC, LONG *, UINT));       /* scale values(hdc, long[], count) */
UDECL(BOOL,  NtGdiPolyDraw,             (HDC, const POINT *, const BYTE *, DWORD));
UDECL(BOOL,  NtGdiPolyTextOutW,         (HDC, const POLYTEXTW *, UINT));
UDECL(DWORD, NtGdiGetCharacterPlacementW, (HDC, LPCWSTR, int, int, GCP_RESULTSW *, DWORD));
UDECL(int,   NtGdiMakeFontDir,          (DWORD, void *, UINT, void *, UINT));
UDECL(ULONG_PTR, NtGdiGetDhpdev,        (HANDLE));
UDECL(int,   NtGdiCheckBitmapBits,      (HDC, void *, void *, int, UINT, int, UINT, BYTE *));

/* ---- user ---- */
UDECL(BOOL,  NtUserTrackMouseEvent,     (TRACKMOUSEEVENT *));
UDECL(BOOL,  NtUserTranslateAccelerator,(HWND, HACCEL, MSG *));
UDECL(BOOL,  NtUserSBGetParms,          (HWND, int, int *sbdata, SCROLLINFO *));
UDECL(BOOL,  NtUserFlashWindowEx,       (FLASHWINFO *));
UDECL(BOOL,  NtUserInheritWindowMonitor,(HWND, HWND));
UDECL(BOOL,  NtUserGetAltTabInfo,       (HWND, int, ALTTABINFO *, void *, UINT, BOOL));
UDECL(BOOL,  NtUserSetWindowCompositionTransition, (HWND, DWORD, void *, void *, void *, void *, void *));

/* WINCOMPATTRDATA: { DWORD attrib; PVOID pvData; SIZE_T cbData; } (kernel copies 0x18 bytes). */
typedef struct { DWORD attrib; PVOID pvData; SIZE_T cbData; } GAP3_WCAD;
UDECL(BOOL,  NtUserGetWindowCompositionAttribute, (HWND, GAP3_WCAD *));
UDECL(BOOL,  NtUserSetWindowCompositionAttribute, (HWND, GAP3_WCAD *));

/* ---- dcomp ---- */
UDECL(NTSTATUS, NtDCompositionGetFrameStatistics, (void *stats /*0x28 bytes*/, void *extra /*0x10 bytes, optional*/));

/* Evaluate-once EXPECT_EQ (the harness macro evaluates operands twice). */
#undef EXPECT_EQ
#define EXPECT_EQ(a, b) do { unsigned long long a_ = (unsigned long long)(ULONG_PTR)(a), b_ = (unsigned long long)(ULONG_PTR)(b); \
    t_check(a_ == b_, __FILE__, __LINE__, #a " == " #b, "got 0x%llx want 0x%llx", a_, b_); } while (0)

/* Wine 9 leaves many of these win32u exports as unimplemented stubs that abort the whole process when called.
 * UNX() is UN() except that on an emulator it yields NULL for those, so REQUIRE() turns the test into a skip. */
static inline FARPROC gap3_nt(const char *name)
{
    static const char *const wine_missing[] = {
        "NtGdiGetMiterLimit", "NtGdiGetDhpdev", "NtGdiScaleRgn", "NtGdiScaleValues", "NtGdiPolyTextOutW",
        "NtGdiGetCharacterPlacementW", "NtGdiMakeFontDir", "NtGdiCheckBitmapBits", "NtUserSBGetParms", "NtUserGetAltTabInfo",
        "NtUserInheritWindowMonitor", "NtUserSetWindowCompositionAttribute", "NtUserGetWindowCompositionAttribute",
        "NtUserSetWindowCompositionTransition", "NtDCompositionGetFrameStatistics", "NtUserTranslateAccelerator",
        "NtUserCreateDesktopEx", "NtUserCreateWindowStation", "NtUserInitializePointerDeviceInjection",
        "NtUserInjectPointerInput", "NtGdiDdDDISetProcessDeviceRemovalSupport", NULL };
    if (t_is_wine())
        for (int i = 0; wine_missing[i]; i++) if (!strcmp(wine_missing[i], name)) return NULL;
    return t_proc(name);
}
#define UNX(name) ((name##_pfn)gap3_nt(#name))

#define SENTINEL_ERR 0x31337
/* A direct-syscall failure leaves the thread error at whatever the kernel set. Plant a sentinel first. */
#define PLANT() SetLastError(SENTINEL_ERR)

#define NTSTATUS_INVALID_PARAMETER 0xC000000DUL
#define NTSTATUS_ACCESS_DENIED     0xC0000022UL

/* Bitmap + memory DC fixture: 32x32 top-down 32bpp white DIB selected into a compatible DC. */
typedef struct { HDC dc; HBITMAP bm, old; DWORD *px; int w, h; } gap3_dib;
static inline int gap3_dib_open(gap3_dib *d, int w, int h)
{
    memset(d, 0, sizeof *d);
    HDC scr = U(GetDC)(NULL);
    d->dc = U(CreateCompatibleDC)(scr);
    BITMAPINFO bi; memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    d->bm = U(CreateDIBSection)(scr, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    U(ReleaseDC)(NULL, scr);
    if (!d->dc || !d->bm || !bits) {
        if (d->bm) U(DeleteObject)(d->bm);
        if (d->dc) U(DeleteDC)(d->dc);
        memset(d, 0, sizeof *d);
        return 0;
    }
    d->old = (HBITMAP)U(SelectObject)(d->dc, d->bm);
    d->px = (DWORD *)bits; d->w = w; d->h = h;
    for (int i = 0; i < w * h; i++) d->px[i] = 0x00FFFFFF;
    return 1;
}
static inline void gap3_dib_close(gap3_dib *d)
{
    if (!d->dc) return;
    U(SelectObject)(d->dc, d->old);
    U(DeleteObject)(d->bm);
    U(DeleteDC)(d->dc);
    memset(d, 0, sizeof *d);
}
static inline int gap3_dib_nonwhite(const gap3_dib *d)
{
    int n = 0;
    for (int i = 0; i < d->w * d->h; i++) if ((d->px[i] & 0xFFFFFF) != 0xFFFFFF) n++;
    return n;
}
/* Open a fixture or skip the calling test. */
#define DIB_OR_SKIP(d, w, h) do { if (!gap3_dib_open(&(d), (w), (h))) { t_skip("cannot create memory DIB DC"); return; } } while (0)
#endif
