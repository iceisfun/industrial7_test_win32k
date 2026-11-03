/* x_pointer_touch_inertia_neg: shared declarations.
 *
 * Syscalls reached through win32u.dll: the pointer-info list query, proprietary-id query, frame discard, pointer
 * injection (rejections only), touch-window capability get/set, touch validation, the DWM-only manipulation and
 * inertia entry points, mouse-in-pointer promotion, touch pad registration, himetric scale factors and the two small
 * mouse queries. Nothing here injects input, changes a system setting or touches a window of another thread beyond
 * a refused call. Signatures come from the user32 callers and the kernel bodies; none is in an SDK header.
 */
#ifndef XP_H
#define XP_H
#include "t.h"
#include "k.h"
#include <setjmp.h>

UDECL(LONGLONG,  NtUserGetPointerInfoList, (UINT id, ULONG kind, int history, int frame, UINT cbEach, UINT *entries, UINT *pointers, void *out));
UDECL(BOOL,      NtUserGetPointerProprietaryId, (UINT id, void *guid));
UDECL(BOOL,      NtUserDiscardPointerFrameMessages, (UINT id));
UDECL(BOOL,      NtUserInjectPointerInput, (HANDLE device, const void *records, UINT count));
UDECL(BOOL,      NtUserIsTouchWindow, (HWND, ULONG *));
UDECL(LONGLONG,  NtUserModifyWindowTouchCapability, (HWND, ULONG flags, int add));
UDECL(ULONG,     NtUserGetTouchValidationStatus, (HANDLE));
UDECL(ULONG_PTR, NtUserRegisterManipulationThread, (void));
UDECL(LONGLONG,  NtUserSetManipulationInputTarget, (UINT, void *, UINT, void *));
UDECL(LONGLONG,  NtUserStopAndEndInertia, (ULONG_PTR, UINT, void *));
UDECL(LONGLONG,  NtUserReportInertia, (ULONG_PTR id, UINT flags, HWND hwnd, void *routing, void *info, void *region));
UDECL(ULONGLONG, NtUserAutoPromoteMouseInPointer, (void));
UDECL(LONG,      NtUserPromoteMouseInPointer, (void));
UDECL(LONGLONG,  NtUserEnableTouchPad, (int));
UDECL(ULONGLONG, NtUserRegisterTouchPadCapable, (int));
UDECL(BOOL,      NtUserGetHimetricScaleFactorFromPixelLocation, (HANDLE device, POINT pt, UINT *x, UINT *y));
UDECL(ULONG_PTR, NtUserMarkWindowForRawMouse, (HWND, int));
UDECL(LONG,      NtUserIsMouseInputEnabled, (void));

/* Exceptions out of a syscall are findings on Windows (the test ends); under an emulator they are a skip. */
extern jmp_buf xp_jb;
extern volatile int xp_armed;
extern volatile unsigned long xp_exc;
void xp_run_guarded(void (*f)(void));
#define XT(n) \
    static void body_##n(void); \
    static void test_##n(void) { xp_run_guarded(body_##n); } \
    static void __attribute__((constructor)) reg_##n(void) { t_register("x_pointer_touch_inertia_neg_" #n, test_##n); } \
    static void body_##n(void)
#define NEED(n) do { if (!UN(n)) { t_skip("missing export: " #n); return; } if (t_is_wine()) { t_skip("emulator: " #n " is not a win32k service"); return; } } while (0)

/* Last errors the checks name. */
#define XE_INVALID_PARAMETER 87
#define XE_ACCESS_DENIED      5
#define XE_INVALID_HANDLE     6
#define XE_NOACCESS         998
#define XE_INVALID_FLAGS   1004
#define XE_INVALID_HWND    1400
#define XE_NO_DATA          232
/* A pointer no process can write: the first page. */
#define XP_BADPTR ((void *)1)
#define XP_KERNELPTR ((void *)0xFFFF800000000000ULL)

/* The call must fail: return value 0 (low 32 bits for BOOL-returning services) and this last error. */
void xp_fail(LONGLONG ret, DWORD gle, DWORD want, const char *file, int line, const char *what);
#define XFAIL(ret_expr, want) do { SET_ERR(0x4D2); LONGLONG r__ = (LONGLONG)(ret_expr); xp_fail(r__, GetLastError(), (want), __FILE__, __LINE__, #ret_expr); } while (0)

/* A window of another thread: the thread creates a hidden tool window (optionally registers it as a touch window with
 * the given flags, -1 for no registration), then pumps until released. */
struct xp_other { HANDLE thread; HWND hwnd; HANDLE ready, stop; int touch_flags; DWORD tid; };
int  xp_other_start(struct xp_other *o, int touch_flags);
void xp_other_stop(struct xp_other *o);

/* Reads the touch flags of a window through the syscall: returns -1 when it is not a touch window. */
int  xp_touch_flags(HWND h);
#endif
