/* gap_07: direct pinpoint tests for syscalls that other areas only reach through their user32/gdi32 route.
 * Every Nt* stub is resolved from win32u.dll with GetProcAddress; signatures are the ones the win32kfull/win32kbase
 * analysis and the user32/gdi32full call sites use (user32 thunks forward their register arguments unchanged,
 * so the public API shape applies unless a comment says otherwise). */
#ifndef G7_H
#define G7_H
#include "t.h"
#include "k.h"

/* t.h's EXPECT_EQ evaluates its arguments twice (once for the test, once for the message); several checks here call
 * state-changing functions, so single-evaluation versions are used throughout this area. */
#undef EXPECT_EQ
#define EXPECT_EQ(a, b) do { ULONG_PTR a_ = (ULONG_PTR)(a), b_ = (ULONG_PTR)(b); \
    t_check(a_ == b_, __FILE__, __LINE__, #a " == " #b, "got 0x%llx want 0x%llx", (unsigned long long)a_, (unsigned long long)b_); } while (0)
#undef EXPECT_NE
#define EXPECT_NE(a, b) do { ULONG_PTR a_ = (ULONG_PTR)(a), b_ = (ULONG_PTR)(b); \
    t_check(a_ != b_, __FILE__, __LINE__, #a " != " #b, "got 0x%llx", (unsigned long long)a_); } while (0)

UDECL(BOOL,  NtUserGetCIMSSM, (void *pSource));
UDECL(LONG,  NtUserQueryDisplayConfig, (UINT flags, UINT *pNumPaths, void *paths, void *pTopology, void *reserved));
UDECL(BOOL,  NtUserSetProcessDpiAwarenessContext, (UINT kernelCtx, int allowUpgrade));
UDECL(UINT,  NtUserGetProcessDpiAwarenessContext, (HANDLE process));
UDECL(BOOL,  NtUserUpdateLayeredWindow, (HWND, HDC, POINT *, SIZE *, HDC, POINT *, COLORREF, BLENDFUNCTION *, DWORD, RECT *));
UDECL(int,   NtUserCopyAcceleratorTable, (HACCEL, void *buf, int cEntries));
UDECL(BOOL,  NtUserRegisterPointerInputTarget, (HWND, ULONG_PTR reserved, DWORD pointerType, DWORD flag));
UDECL(BOOL,  NtUserDiscardPointerFrameMessages, (UINT pointerId));
UDECL(BOOL,  NtUserGetMenuItemRect, (HWND, HMENU, UINT pos, RECT *));
UDECL(int,   NtUserMenuItemFromPoint, (HWND, HMENU, ULONGLONG packedPoint));
UDECL(BOOL,  NtUserGetPointerCursorId, (UINT pointerId, UINT *cursorId));
UDECL(BOOL,  NtUserGetPointerDevice, (HANDLE device, void *info));
UDECL(BOOL,  NtUserGetPointerDeviceCursors, (HANDLE device, UINT *count, void *cursors));
UDECL(BOOL,  NtUserGetPointerDeviceProperties, (HANDLE device, UINT *count, void *props));
UDECL(BOOL,  NtUserGetPointerDeviceRects, (HANDLE device, RECT *deviceRect, RECT *displayRect));
UDECL(BOOL,  NtUserGetPointerDevices, (UINT *count, void *devices));
UDECL(BOOL,  NtUserGetRawPointerDeviceData, (UINT pointerId, UINT history, UINT propCount, void *props, LONG *values));
UDECL(BOOL,  NtUserRegisterTouchHitTestingWindow, (HWND, ULONG value));
UDECL(BOOL,  NtUserDrawIconEx, (HDC, int x, int y, HICON, int cx, int cy, UINT step, HBRUSH, UINT flags, BOOL metaHdc, void *drawIconExData));
UDECL(void,  NtUserTransformPoint, (POINT *pt, UINT toCtx, UINT fromCtx, HMONITOR mon));
UDECL(void,  NtUserTransformRect, (RECT *rc, UINT toCtx, UINT fromCtx, HMONITOR mon));
UDECL(HPALETTE, NtUserSelectPalette, (HDC, HPALETTE, BOOL bForceBkgd));
UDECL(BOOL,  NtGdiExtTextOutW, (HDC, int x, int y, UINT opts, const RECT *rc, const WCHAR *str, UINT cwc, const INT *dx, DWORD cp));
UDECL(int,   NtGdiDrawEscape, (HDC, int esc, int cjIn, const void *in));
UDECL(BOOL,  NtGdiAngleArc, (HDC, int x, int y, DWORD r, DWORD startBits, DWORD sweepBits));
UDECL(ULONG, NtGdiColorCorrectPalette, (HDC, HPALETTE, ULONG first, ULONG n, PALETTEENTRY *, int set));
UDECL(BOOL,  NtGdiCombineTransform, (XFORM *out, const XFORM *a, const XFORM *b));
UDECL(HPALETTE, NtGdiCreateHalftonePalette, (HDC));
UDECL(BOOL,  NtGdiGetColorAdjustment, (HDC, void *adj));
UDECL(int,   NtGdiPtVisible, (HDC, int x, int y));
UDECL(int,   NtGdiRectVisible, (HDC, const RECT *));
UDECL(UINT,  NtGdiSetSystemPaletteUse, (HDC, UINT usage));
UDECL(int,   NtGdiUpdateColors, (HDC));
UDECL(BOOL,  NtGdiDeleteColorSpace, (HANDLE));
UDECL(BOOL,  NtGdiMirrorWindowOrg, (HDC));
UDECL(ULONG_PTR, NtGdiEnsureDpiDepDefaultGuiFontForPlateau, (void));

/* Wine's win32u exports these names as stubs that abort the whole process when called; they are hidden under Wine so
 * REQUIRE() skips the test. On Windows every one of them exists and is called. */
static inline int g7_wine_missing(const char *n)
{
    static const char *const miss[] = {
        "NtGdiColorCorrectPalette", "NtGdiCombineTransform", "NtGdiDeleteColorSpace", "NtGdiDrawEscape",
        "NtGdiEnsureDpiDepDefaultGuiFontForPlateau", "NtGdiMirrorWindowOrg", "NtUserDiscardPointerFrameMessages",
        "NtUserGetCIMSSM", "NtUserGetPointerCursorId", "NtUserGetPointerDevice", "NtUserGetPointerDeviceCursors",
        "NtUserGetPointerDeviceProperties", "NtUserGetPointerDeviceRects", "NtUserGetPointerDevices",
        "NtUserGetRawPointerDeviceData", "NtUserTransformPoint", "NtUserTransformRect",
        "NtUserRegisterPointerInputTarget", "NtUserRegisterTouchHitTestingWindow", NULL };
    for (int i = 0; miss[i]; i++) if (!strcmp(miss[i], n)) return 1;
    return 0;
}
#define G7N(name) ((t_is_wine() && g7_wine_missing(#name)) ? (name##_pfn)NULL : UN(name))

/* kernel DPI awareness contexts valid for NtUserTransformPoint/Rect (IsValidKernelDpiAwarenessContext) */
#define G7_DPI_SYSTEM  0x12u
#define G7_DPI_PMV2    0x22u
#define G7_DPI_UNAWARE 0x6010u

/* A hidden, off-screen, non-activating window of a builtin class (never shown). */
static inline HWND g7_window(DWORD style, DWORD ex)
{
    return U(CreateWindowExW)(ex | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"win32kt", style,
                              -32000, -32000, 64, 64, NULL, NULL, GetModuleHandleW(NULL), NULL);
}
/* 32bpp top-down DIB section selected into a fresh memory DC; sets *bits. */
struct g7_dib { HDC dc; HBITMAP bm, old; DWORD *bits; int w, h; };
static inline BOOL g7_dib_open(struct g7_dib *d, int w, int h)
{
    BITMAPINFO bi; memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    memset(d, 0, sizeof *d); d->w = w; d->h = h;
    d->dc = U(CreateCompatibleDC)(NULL);
    if (!d->dc) return FALSE;
    d->bm = U(CreateDIBSection)(d->dc, &bi, DIB_RGB_COLORS, (void **)&d->bits, NULL, 0);
    if (!d->bm) { U(DeleteDC)(d->dc); d->dc = NULL; return FALSE; }
    d->old = (HBITMAP)U(SelectObject)(d->dc, d->bm);
    return TRUE;
}
static inline void g7_dib_close(struct g7_dib *d)
{
    if (d->dc) { U(SelectObject)(d->dc, d->old); U(DeleteDC)(d->dc); }
    if (d->bm) U(DeleteObject)(d->bm);
    memset(d, 0, sizeof *d);
}
static inline DWORD g7_f2d(float f) { union { float f; DWORD d; } u; u.f = f; return u.d; }
#endif
