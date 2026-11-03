/* gap_02: win32u stubs with no earlier direct coverage. Shared declarations.
 * Signatures are derived from the win32kfull.sys / win32kbase.sys analysis of each Nt* body (parameter
 * use, probes, copy-in/out) and the user32 / gdi32full call sites; see the comment above each group. */
#ifndef GAP2_H
#define GAP2_H
#include "t.h"
#include "k.h"

/* user-window syscalls: all return a 64-bit 0/1 (longlong / ulonglong in the analysis) */
UDECL(INT_PTR, NtUserIsTopLevelWindow, (HWND));
UDECL(ULONG_PTR, NtUserIsChildWindowDpiMessageEnabled, (HWND));
UDECL(INT_PTR, NtUserIsWindowBroadcastingDpiToChildren, (HWND));
UDECL(INT_PTR, NtUserEnableChildWindowDpiMessage, (HWND, DWORD));
UDECL(INT_PTR, NtUserIsNonClientDpiScalingEnabled, (HWND));
UDECL(DWORD, NtUserGetWindowBand, (HWND, DWORD *));
/* (process id, milliseconds, flag): user32!WaitForInputIdle passes PROCESS_BASIC_INFORMATION.UniqueProcessId and 0 */
UDECL(DWORD, NtUserWaitForInputIdle, (ULONG_PTR, DWORD, DWORD));
UDECL(DWORD, NtUserGetUniformSpaceMapping, (HMONITOR, UINT *));
UDECL(DWORD, NtUserGetOemBitmapSize, (UINT, int *));
UDECL(INT_PTR, NtUserBitBltSysBmp, (HDC, int, int, UINT, DWORD));
UDECL(INT_PTR, NtUserTrackPopupMenuEx, (HMENU, UINT, int, int, HWND, void *));
UDECL(INT_PTR, NtUserSetImeInfoEx, (void *));
UDECL(DWORD, NtUserEvent, (void *));
UDECL(ULONG_PTR, NtUserModifyUserStartupInfoFlags, (DWORD, DWORD));

/* GDI */
UDECL(BOOL, NtGdiUpdateTransform, (HDC));
UDECL(ULONG, NtGdiQueryFonts, (void *, ULONG, LONGLONG *));
UDECL(BOOL, NtGdiGetCharWidthW, (HDC, UINT, UINT, const WCHAR *, ULONG, void *));
UDECL(BOOL, NtGdiGetCharABCWidthsW, (HDC, UINT, UINT, const WCHAR *, ULONG, void *));
UDECL(ULONG_PTR, NtGdiStartDoc, (HDC, void *, int *, int));
UDECL(BOOL, NtGdiResetDC, (HDC, void *, int *, void *, void *));
UDECL(DWORD, NtGdiDrawStream, (HDC, ULONG, void *));

/* dxgkrnl (reached through win32k thunks): single pointer to a D3DKMT_* argument block */
UDECL(LONG, NtGdiDdDDIOpenAdapterFromLuid, (void *));
UDECL(LONG, NtGdiDdDDICloseAdapter, (void *));
UDECL(LONG, NtGdiDdDDICacheHybridQueryValue, (void *));
UDECL(LONG, NtGdiDdDDIGetCachedHybridQueryValue, (void *));
UDECL(LONG, NtGdiDdDDIGetMultiPlaneOverlayCaps, (void *));

/* Wine's win32u aborts the process on Nt* stubs it does not implement (nearly all of this area), so under
 * Wine the resolver yields NULL and REQUIRE() skips the test; on Windows this is UN(). */
#define GN(name) (t_is_wine() ? (name##_pfn)NULL : UN(name))

#define GAP2_BAD_HWND ((HWND)(ULONG_PTR)0x7ffe1234)

/* offscreen hidden toolwindow of a system class; never shown, never activated */
static inline HWND gap2_win(DWORD style, DWORD ex, HWND parent) {
    __typeof__(&CreateWindowExA) cw = U(CreateWindowExA);
    if (!cw) return NULL;
    return cw(ex | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, "STATIC", "win32kt_gap2", style, -32000, -32000, 8, 8,
              parent, NULL, GetModuleHandleA(NULL), NULL);
}
static inline HWND gap2_top(void) { return gap2_win(WS_POPUP, 0, NULL); }
static inline HWND gap2_child(HWND parent) { return gap2_win(WS_CHILD, 0, parent); }
static inline void gap2_destroy(HWND h) { if (h && U(DestroyWindow)) U(DestroyWindow)(h); }

#endif
