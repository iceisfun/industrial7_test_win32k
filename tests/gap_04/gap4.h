/* Shared fixtures for the gap_04 area: direct win32u stubs of syscalls that no other area calls,
 * with the argument lists derived from win32kfull/win32kbase analysis and the user32 thunks. */
#ifndef GAP4_H
#define GAP4_H
#include "t.h"
#include "k.h"

/* win32kfull!NtUserOpenDesktop(OBJECT_ATTRIBUTES*, dwFlags, access) -> HDESK; user32!CommonOpenDesktop
 * builds {0x30, hWinsta, name, attrs 0x40 (|2 when inheriting)} and passes it with (dwFlags, access). */
UDECL(HDESK, NtUserOpenDesktop, (PVOID oa, DWORD flags, ACCESS_MASK access));
UDECL(BOOL,  NtUserRemoveInjectionDevice, (HANDLE h));
UDECL(LONG,  NtUserSetDisplayConfig, (UINT count, PVOID paths, UINT flags, ULONG_PTR a4, ULONG_PTR a5));
UDECL(BOOL,  NtUserCalculatePopupWindowPosition, (const POINT *anchor, const SIZE *size, UINT flags, const RECT *excl, RECT *out));
UDECL(LONG,  NtUserDisplayConfigSetDeviceInfo, (PVOID header));
UDECL(BOOL,  NtUserDragDetect, (HWND h, POINT pt));
UDECL(BOOL,  NtUserDrawAnimatedRects, (HWND h, int idAni, const RECT *from, const RECT *to));
UDECL(BOOL,  NtUserDrawCaption, (HWND h, HDC dc, const RECT *rc, UINT flags));
UDECL(BOOL,  NtUserEnableNonClientDpiScaling, (HWND h));
UDECL(BOOL,  NtUserEndMenu, (void));
UDECL(BOOL,  NtUserGetAutoRotationState, (DWORD *state));
UDECL(BOOL,  NtUserGetDisplayAutoRotationPreferences, (DWORD *pref));
UDECL(BOOL,  NtUserSetDisplayAutoRotationPreferences, (DWORD pref));
UDECL(DWORD, NtUserGetListBoxInfo, (HWND h));
UDECL(int,   NtUserGetMouseMovePointsEx, (UINT cb, MOUSEMOVEPOINT *in, MOUSEMOVEPOINT *out, int n, DWORD res));
UDECL(BOOL,  NtUserGetWindowFeedbackSetting, (HWND h, int type, DWORD flags, UINT *size, PVOID cfg));
UDECL(BOOL,  NtUserSetWindowFeedbackSetting, (HWND h, int type, DWORD flags, UINT size, const PVOID cfg));
UDECL(BOOL,  NtUserLogicalToPhysicalPoint, (HWND h, POINT *pt));
UDECL(BOOL,  NtUserPhysicalToLogicalPoint, (HWND h, POINT *pt));
UDECL(BOOL,  NtUserPaintDesktop, (HDC dc));
UDECL(BOOL,  NtUserSetSystemCursor, (HCURSOR c, DWORD id));
UDECL(int,   NtUserShutdownBlockReasonCreate, (HWND h, LPCWSTR reason, UINT cch));
UDECL(BOOL,  NtUserShutdownBlockReasonQuery, (HWND h, LPWSTR buf, DWORD *pcch));
UDECL(int,   NtUserShutdownReasonDestroy, (HWND h));

#ifndef OCR_NORMAL
#define OCR_NORMAL 32512
#endif
#define G4_IDC_ARROW MAKEINTRESOURCEW(32512)
#define G4_IDC_CROSS MAKEINTRESOURCEW(32515)
#define G4_ERR_INVALID_WINDOW 1400   /* win32kbase!ValidateHwndEx failure path: UserSetLastError(0x578) */

/* A private class whose procedure is user32!DefWindowProcW; windows are hidden and tool windows. */
HWND g4_window(DWORD style, DWORD exstyle, int x, int y, int w, int h, HWND parent);
void g4_destroy(HWND h);
/* 32x32 top-down 32bpp DIB section selected into a fresh memory DC (NULL on failure). */
HDC  g4_memdc(HBITMAP *bmp_out, HBITMAP *old_out, void **bits_out);
void g4_memdc_free(HDC dc, HBITMAP bmp, HBITMAP old);
/* System DPI as the kernel sees it (physical): reads GetDpiForSystem with this thread temporarily per-monitor-v2
 * aware, so a DPI-unaware test exe still learns the real scaling; 96 when it cannot be determined. */
UINT g4_real_dpi(void);
/* Wine's win32u aborts the whole process on a syscall it does not implement; this lists those so Wine runs skip them. */
int g4_wine_skip(const char *name);
#define G4REQ(n) do { if (!UN(n)) { t_skip("missing export: " #n); return; } \
    if (g4_wine_skip(#n)) { t_skip("Wine does not implement " #n); return; } } while (0)
#endif
