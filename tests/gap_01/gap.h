/* Shared declarations/fixtures for the gap_01 and dx_gap_01 areas (syscalls with no other direct coverage).
 *
 * Signatures below were derived from the win32kfull analysis of each Nt* routine (parameter use, probes,
 * copy-in/out) and from the gdi32full/user32 call sites; each test file says which branch backs an expectation.
 * Error numbers used (user-mode winerror): 0x57 ERROR_INVALID_PARAMETER, 0x578/0x579/0x57a invalid window/menu/cursor
 * handle (win32kbase!HMValidateHandle picks it from the expected object type 1/2/3), 0x585 ERROR_INVALID_INDEX.
 */
#ifndef GAP_H
#define GAP_H
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

/* Wine's win32u exports many of these Nt* names as abort-on-call stubs (the process dies with "unimplemented function"), so under Wine
 * UN() answers NULL for them and the test skips via REQUIRE(). On Windows every name resolves normally. */
static FARPROC gap_proc(const char *n) {
    static const char *const wine_stub[] = {
        "NtGdiMonoBitmap", "NtGdiSetMiterLimit", "NtGdiSetSizeDevice", "NtUserGetRequiredCursorSizes", "NtUserLinkDpiCursor",
        "NtUserSetMenuFlagRtoL", "NtUserMinMaximize", "NtUserChangeWindowMessageFilterEx", "NtUserGetCPD",
        "NtUserGetProcessUIContextInformation", "NtUserInjectGesture", "NtUserSetImeOwnerWindow", "NtGdiGetTextExtent",
        "NtGdiGetGlyphIndicesW", "NtGdiGetGlyphIndicesWInternal", "NtGdiGetWidthTable", "NtGdiGetUFI", "NtGdiGetEmbUFI",
        "NtGdiGetLinkedUFIs", "NtGdiChangeGhostFont", "GetFontRealizationInfo", "GetProcessUIContextInformation", NULL };
    if (t_is_wine()) for (int i = 0; wine_stub[i]; i++) if (!strcmp(wine_stub[i], n)) return NULL;
    return t_proc(n);
}
#undef UN
#define UN(name) ((name##_pfn)gap_proc(#name))

/* ---- syscall signatures ------------------------------------------------ */
UDECL(int,   NtGdiMonoBitmap, (HBITMAP));
UDECL(int,   NtGdiSetMiterLimit, (HDC, DWORD, DWORD *));
UDECL(int,   NtGdiSetSizeDevice, (HDC, int, int));
UDECL(int,   NtGdiStartPage, (HDC));
UDECL(int,   NtGdiStretchDIBitsInternal, (HDC, int, int, int, int, int, int, int, int, const void *, const BITMAPINFO *,
                                          DWORD, DWORD, UINT, UINT, HANDLE));
UDECL(BOOL,  NtUserGetRequiredCursorSizes, (HCURSOR, int *));
UDECL(BOOL,  NtUserLinkDpiCursor, (HCURSOR, HCURSOR, UINT));
UDECL(int,   NtGdiGetWidthTable, (HDC, ULONG, WCHAR *, UINT, USHORT *, void *, ULONG *));
UDECL(BOOL,  NtUserSetMenuFlagRtoL, (HMENU));
UDECL(BOOL,  NtGdiGetTextExtentExW, (HDC, const WCHAR *, ULONG, ULONG, ULONG *, ULONG *, SIZE *));
UDECL(int,   NtGdiGetGlyphIndicesW, (HDC, const WCHAR *, int, WORD *, DWORD));
UDECL(int,   NtGdiGetGlyphIndicesWInternal, (HDC, const WCHAR *, int, WORD *, DWORD, DWORD));
UDECL(BOOL,  NtGdiGetRealizationInfo, (HDC, DWORD *));
UDECL(BOOL,  NtUserGetProcessUIContextInformation, (HANDLE, ULONGLONG *));
UDECL(ULONG_PTR, NtUserMinMaximize, (HWND, int, BOOL));
UDECL(BOOL,  NtUserSetSystemMenu, (HWND, HMENU));
UDECL(BOOL,  NtUserChangeWindowMessageFilterEx, (HWND, UINT, DWORD, CHANGEFILTERSTRUCT *));
UDECL(ULONG_PTR, NtUserGetCPD, (HWND, DWORD, ULONG_PTR));
UDECL(BOOL,  NtGdiGetTextExtent, (HDC, const WCHAR *, int, SIZE *, DWORD));
UDECL(BOOL,  NtGdiChangeGhostFont, (void *, BOOL));
UDECL(BOOL,  NtGdiGetEmbUFI, (HDC, void *, void *, ULONG *, ULONG *, ULONG *, ULONGLONG *));
UDECL(BOOL,  NtGdiGetUFI, (HDC, void *, void *, ULONG *, ULONG *, ULONG *));
UDECL(int,   NtGdiGetLinkedUFIs, (HDC, void *, int));
UDECL(BOOL,  NtUserInjectGesture, (HWND, int, LONGLONG, void *, LONGLONG));
UDECL(ULONG_PTR, NtUserSetImeOwnerWindow, (HWND, HWND));

/* gdi32!GetFontRealizationInfo / user32!GetProcessUIContextInformation are exported without SDK declarations. */
UDECL(BOOL, GetFontRealizationInfo, (HDC, DWORD *));
UDECL(BOOL, GetProcessUIContextInformation, (HANDLE, ULONGLONG *));

typedef struct { DWORD CheckSum; DWORD Index; } gap_ufi;       /* UNIVERSAL_FONT_ID, 8 bytes (NtGdiGetUFI copies exactly one qword) */

#define GAP_SENT 0x5a5a5a5au

/* ---- fixtures ---------------------------------------------------------- */
/* Loads user32/gdi32 (connecting the process to win32k) and reports whether the basics resolve. */
static int gap_gui(void) {
    return U(GetDesktopWindow) && U(CreateCompatibleDC) && U(CreateWindowExA) && U(GetDesktopWindow)() != NULL;
}
#define GAP_BEGIN() do { if (!gap_gui()) { t_skip("user32/gdi32 unavailable"); return; } } while (0)

static DWORD gap_err(void) { return GetLastError(); }
#define GAP_POISON() SetLastError(0xBEEF)

/* A memory DC (default 1x1 mono bitmap), optionally with a named font selected. */
struct gap_dc { HDC dc; HFONT font, oldfont; WCHAR face[LF_FACESIZE]; };
static int gap_dc_open(struct gap_dc *d, const WCHAR *face, int height) {
    memset(d, 0, sizeof *d);
    d->dc = U(CreateCompatibleDC)(NULL);
    if (!d->dc) return 0;
    if (face) {
        d->font = U(CreateFontW)(height, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, ANTIALIASED_QUALITY, 0, face);
        if (d->font) d->oldfont = (HFONT)U(SelectObject)(d->dc, d->font);
    }
    U(GetTextFaceW)(d->dc, LF_FACESIZE, d->face);
    return 1;
}
static void gap_dc_close(struct gap_dc *d) {
    if (d->font) { U(SelectObject)(d->dc, d->oldfont); U(DeleteObject)(d->font); }
    if (d->dc) U(DeleteDC)(d->dc);
    memset(d, 0, sizeof *d);
}

/* Hidden, off-screen, non-activating windows of two private classes (ANSI and Unicode wndproc). */
static ATOM gap_atomA, gap_atomW;
static const char gap_clsA[] = "win32kt_gap1_A";
static const WCHAR gap_clsW[] = L"win32kt_gap1_W";
static int gap_classes(int cbCls) {
    if (!gap_atomA) {
        WNDCLASSEXA a; memset(&a, 0, sizeof a);
        a.cbSize = sizeof a; a.lpfnWndProc = (WNDPROC)t_proc_in("user32.dll", "DefWindowProcA");
        a.hInstance = GetModuleHandleA(NULL); a.lpszClassName = gap_clsA; a.cbClsExtra = 8;
        if (a.lpfnWndProc) gap_atomA = U(RegisterClassExA)(&a);
        /* gap.h is included by several source files, each with its own copy of these statics: a later file finds the class
         * already registered by an earlier one (ERROR_CLASS_ALREADY_EXISTS) and takes its atom from GetClassInfoExA. */
        if (!gap_atomA && GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
            WNDCLASSEXA q; memset(&q, 0, sizeof q);
            gap_atomA = (ATOM)(ULONG_PTR)U(GetClassInfoExA)(a.hInstance, gap_clsA, &q);
        }
    }
    if (!gap_atomW) {
        WNDCLASSEXW w; memset(&w, 0, sizeof w);
        w.cbSize = sizeof w; w.lpfnWndProc = (WNDPROC)t_proc_in("user32.dll", "DefWindowProcW");
        w.hInstance = GetModuleHandleA(NULL); w.lpszClassName = gap_clsW; w.cbClsExtra = 8;
        if (w.lpfnWndProc) gap_atomW = U(RegisterClassExW)(&w);
        if (!gap_atomW && GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
            WNDCLASSEXW q; memset(&q, 0, sizeof q);
            gap_atomW = (ATOM)(ULONG_PTR)U(GetClassInfoExW)(w.hInstance, gap_clsW, &q);
        }
    }
    return gap_atomA && gap_atomW;
}
static HWND gap_win(int wide, DWORD style) {
    if (!gap_classes(8)) return NULL;
    if (wide) return U(CreateWindowExW)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, gap_clsW, L"win32kt", style, -32000, -32000, 120, 80,
                                      NULL, NULL, GetModuleHandleA(NULL), NULL);
    return U(CreateWindowExA)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, gap_clsA, "win32kt", style, -32000, -32000, 120, 80,
                              NULL, NULL, GetModuleHandleA(NULL), NULL);
}
#define GAP_STYLE_SYS (WS_POPUP | WS_CAPTION | WS_SYSMENU)   /* never WS_VISIBLE: the window stays hidden */

static HCURSOR gap_cursor(void) {
    BYTE andp[128], xorp[128];
    memset(andp, 0xff, sizeof andp); memset(xorp, 0, sizeof xorp);
    return U(CreateCursor)(GetModuleHandleA(NULL), 0, 0, 32, 32, andp, xorp);
}

static DWORD gap_fbits(float f) { union { float f; DWORD d; } u; u.f = f; return u.d; }
static float gap_bitsf(DWORD d) { union { float f; DWORD d; } u; u.d = d; return u.f; }
#endif
