/* Shared fixtures for the r2_input_pointer_rim_state area: RIM (raw input manager) syscalls, input
 * injection permission gates, foreground brokering, cursor helpers and the per-thread queue wake/
 * change bits (CLIENTTHREADINFO) behind GetQueueStatus / NtUserGetQueueStatusReadonly and the
 * RealWaitMessageEx / WaitAvailableMessageEx sleepers.
 *
 * Argument roles come from the win32kbase/win32kfull bodies of each NtRIM or NtUser function (see the
 * comment above each test), not from any header: none of these are declared in the SDK.
 */
#ifndef R2IP_H
#define R2IP_H
#include "t.h"
#include "k.h"

/* Wine's win32u aborts the process with EXCEPTION_WINE_STUB (0x80000100) on an unimplemented syscall: r2_veh_once()
 * turns that into an ordinary fault on the test thread (the harness reports one failed test) and ends a helper
 * thread; Windows never raises it. */
void r2_veh_once(void);
/* UN() for the syscalls of this area: under an emulator whose win32u only has an aborting placeholder for the
 * syscall (Wine), resolves to NULL so that REQUIRE() skips the test. On Windows it is exactly UN(). */
FARPROC r2_sysproc(const char *name);
#define UNW(name) ((name##_pfn)r2_sysproc(#name))
#define RT(n) \
    static void rt_##n(void); \
    TEST(r2_input_pointer_rim_state_##n) { r2_veh_once(); rt_##n(); } \
    static void rt_##n(void)

/* ---- NTSTATUS values used by the checks --------------------------------------------------- */
#define R2_ST_ACCESS_DENIED     ((LONG)0xC0000022)
#define R2_ST_INVALID_PARAMETER ((LONG)0xC000000D)
#define R2_ST_INVALID_HANDLE    ((LONG)0xC0000008)
#define R2_ST_TYPE_MISMATCH     ((LONG)0xC0000024)
#define R2_ST_NOT_FOUND         ((LONG)0xC0000225)

#ifndef ERROR_TIMEOUT
#define ERROR_TIMEOUT 1460
#endif
#define E_ACCESS 5
#define E_INVALID_PARAM 87
#define E_INVALID_FLAGS 1004
#define E_INVALID_HANDLE 6
#define E_INVALID_HWND 1400

#ifndef QS_KEY
#define QS_KEY 0x0001
#define QS_MOUSEMOVE 0x0002
#define QS_MOUSEBUTTON 0x0004
#define QS_POSTMESSAGE 0x0008
#define QS_TIMER 0x0010
#define QS_PAINT 0x0020
#define QS_SENDMESSAGE 0x0040
#define QS_HOTKEY 0x0080
#define QS_ALLPOSTMESSAGE 0x0100
#endif
#ifndef QS_RAWINPUT
#define QS_RAWINPUT 0x0400
#endif
#define R2_QS_ALL_VALID 0x5DFF  /* the mask NtUserGetQueueStatus applies (win32kfull!NtUserGetQueueStatus: param & 0x5dff) */

/* ---- win32u stubs without an SDK declaration ----------------------------------------------- */
UDECL(LONG,      NtRIMQueryDevicePath, (UNICODE_STRING *path, HANDLE *out));
UDECL(LONG,      NtRIMGetDevicePreparsedDataLockfree, (HANDLE dev, void *buf, UINT *pcb));
UDECL(LONG,      NtRIMGetDevicePropertiesLockfree, (HANDLE dev, void *props));
UDECL(LONG,      NtRIMSetExtendedDeviceProperty, (HANDLE dev, const void *prop, int cb));
UDECL(LONG,      NtRIMFreeInputBuffer, (HANDLE rim, void *p));
UDECL(LONG,      NtRIMObserveNextInput, (HANDLE obs));
UDECL(LONG,      NtRIMRemoveInputObserver, (HANDLE obs));
UDECL(LONG,      NtRIMUnregisterForInput, (HANDLE rim));
UDECL(LONG,      NtRIMOnPnpNotification, (HANDLE rim));
UDECL(LONG,      NtRIMOnTimerNotification, (HANDLE rim, UINT arg));
UDECL(LONG,      NtRIMSetTestModeStatus, (UINT on));
UDECL(LONG,      NtRIMEnableMonitorMappingForDevice, (HANDLE dev, void *a, void *b));

UDECL(LONG,      NtUserInjectKeyboardInput, (const void *in, UINT count));
UDECL(LONG,      NtUserInjectMouseInput, (const void *in, UINT count));
UDECL(LONG,      NtUserInjectDeviceInput, (HANDLE dev, const void *in, UINT count));
UDECL(LONG,      NtUserInjectGenericHidInput, (HANDLE dev, const void *data, UINT cb));
UDECL(LONG,      NtUserInitializeGenericHidInjection, (const void *desc, HANDLE *out));
UDECL(LONG,      NtUserInitializeInputDeviceInjection, (USHORT a, USHORT b, const void *items, UINT n, HANDLE shared, int mode, HANDLE *out));
UDECL(LONG,      NtUserInitializePointerDeviceInjectionEx, (int type, int contacts, HANDLE shared, int mode, DWORD flags, HANDLE *out));
UDECL(LONG,      NtUserInitializePointerDeviceInjection, (int type, int contacts, HANDLE shared, int mode, DWORD flags, HANDLE *out));
UDECL(LONG,      NtUserInitializeTouchInjection, (UINT contacts, DWORD mode));
UDECL(LONG,      NtUserRemoveInjectionDevice, (HANDLE dev));

UDECL(LONG,      NtUserClearForeground, (void));
UDECL(LONG,      NtUserSetFallbackForeground, (HWND h, int set));
UDECL(LONG,      NtUserSetBrokeredForeground, (HWND h));
UDECL(LONG,      NtUserCanBrokerForceForeground, (DWORD *out));
UDECL(LONG,      NtUserSetForegroundWindowForApplication, (HWND h));
UDECL(ULONG_PTR, NtUserNavigateFocus, (HWND h));
UDECL(LONG,      NtUserLockCursor, (const RECT *r));
UDECL(LONG,      NtUserShowSystemCursor, (int show));
UDECL(HANDLE,    NtUserGetCursorFrameInfo, (HCURSOR c, int frame, DWORD *rate, int *steps));
UDECL(HANDLE,    NtUserInternalGetWindowIcon, (HWND h, UINT type));
UDECL(LONG,      NtUserIsMouseInputEnabled, (void));
UDECL(LONG,      NtUserSetThreadInputBlocked, (int magic, int block));
UDECL(ULONG_PTR, NtUserMarkWindowForRawMouse, (HWND h, int on));
UDECL(LONG,      NtUserPostKeyboardInputMessage, (DWORD a, const void *kbd));
UDECL(LONG,      NtUserPromoteMouseInPointer, (void));

UDECL(DWORD,     NtUserGetQueueStatus, (UINT flags));
UDECL(DWORD,     NtUserGetQueueStatusReadonly, (UINT flags));
UDECL(LONG,      NtUserWaitAvailableMessageEx, (DWORD mask, DWORD timeout));
UDECL(LONG,      NtUserRealWaitMessageEx, (DWORD mask, DWORD timeout));
UDECL(ULONG_PTR, NtUserWaitForMsgAndEvent, (ULONG_PTR a, ULONG_PTR b, ULONG_PTR c, ULONG_PTR d));
UDECL(LONG,      NtUserRealInternalGetMessage, (MSG *msg, HWND h, UINT lo, UINT hi, UINT flags, UINT extra));
UDECL(ULONG_PTR, NtUserGetThreadState, (LONG));

/* ---- GUI-thread fixtures ------------------------------------------------------------------- */

/* Runs body() on a fresh thread whose win32k queue is empty (the thread is converted by its first
 * win32k call; GetQueueStatus(0x5DFF) also resets the change bits). The body must not call t_skip
 * (longjmp across threads): do REQUIRE()s in the TEST before calling r2_gui_thread(). */
typedef void (*r2_body)(void);
void r2_gui_thread(r2_body b);

/* Hidden, off-screen, non-activating popup owned by the calling thread (class registered on demand). */
HWND r2_window(DWORD style);
/* The window procedure counts WM_APP+i (i < 8) in r2_hit[i] and records the thread that ran it; it returns 0x5151. */
extern volatile LONG r2_hit[8];
extern volatile DWORD r2_hit_tid[8];
void r2_hits_reset(void);
void r2_pump_discard(void);   /* removes every pending message of the calling thread */

/* CLIENTTHREADINFO of the calling thread: _CLIENTINFO.pClientThreadInfo (TEB+0x860, user32!GetInputState)
 * -> { DWORD flags; WORD fsChangeBits(+4); WORD fsWakeBits(+6); WORD fsWakeBitsJournal(+8);
 *      WORD fsWakeMask(+0xa); DWORD timeLastRead(+0xc); }  - measured from user mode
 * (+4 changed, +6 wake, +8 journal, +0xa wake mask, +0xc last-read tick). */
struct r2_cti { unsigned short change, wake, journal, wakemask; unsigned long tick; int ok; };
void r2_cti_read(struct r2_cti *c);

/* A short-lived helper thread running fn(arg); returns after the thread ended (bounded wait). */
BOOL r2_run_thread(LPTHREAD_START_ROUTINE fn, void *arg);

/* Is the "inputInjection" capability granted to this process? Side-effect free probe:
 * NtUserInitializeInputDeviceInjection with a zero item count fails with 0x57 when the capability
 * check (RIMIDECheckCurrentProcessForInjectionPermissions) passes and with 5 when it does not. */
int r2_injection_permitted(void);
#endif
