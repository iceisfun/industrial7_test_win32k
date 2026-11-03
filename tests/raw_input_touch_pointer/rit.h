/* Shared fixtures for the raw_input_touch_pointer area.
 *
 * Everything here reaches win32kfull/win32kbase through win32u's NtUser* stubs (the SDK user32
 * exports for these APIs are bare jumps into the same stubs, see user32!GetRawInputDeviceList,
 * RegisterRawInputDevices, GetPointerType, ...). Signatures that are not in the SDK were derived
 * from the win32kfull/win32kbase bodies and from user32's callers.
 */
#ifndef RIT_H
#define RIT_H
#include "t.h"
#include "k.h"
#include <stdlib.h>

void rit_veh_once(void);   /* installs the Wine-stub handler after the harness's own (rit_misc.c) */
#define RT(n) \
    static void rt_##n(void); \
    TEST(raw_input_touch_pointer_##n) { rit_veh_once(); rt_##n(); } \
    static void rt_##n(void)

#ifndef ERROR_INVALID_FLAGS
#define ERROR_INVALID_FLAGS 1004
#endif
#define E_INSUFFICIENT 122      /* ERROR_INSUFFICIENT_BUFFER */
#define E_INVALID_PARAM 87      /* ERROR_INVALID_PARAMETER */
#define E_INVALID_HANDLE 6      /* ERROR_INVALID_HANDLE */
#define E_ACCESS_DENIED 5
#define E_INVALID_HWND 1400     /* ERROR_INVALID_WINDOW_HANDLE: HMValidateHandle(type 1) failure code */
#define RIT_RAWINPUT_HDR 24     /* sizeof(RAWINPUTHEADER) on x64: the cbSizeHeader every raw-input call demands (0x18) */
#define RIT_RID_INPUT  0x10000003
#define RIT_RID_HEADER 0x10000005
#define RIT_RIDI_PREPARSED 0x20000005
#define RIT_RIDI_NAME      0x20000007
#define RIT_RIDI_INFO      0x2000000b
#define RIT_RIDI_HEALTH    0x2000000c   /* undocumented: win32kfull!NtUserGetRawInputDeviceInfo, size 0x2c */
/* _CLIENTINFO.pClientThreadInfo: user32!GetRawInputBuffer reads *(TEB+0x860) = TEB.Win32ClientInfo + 0x60 */
#define RIT_CI_pClientThreadInfo 0x60

/* The GUI thread must exist (first win32k syscall converts it) before kernel entry points that
 * dereference the current-thread info are called through a raw stub. Any user32 call does that. */
static inline void rit_gui(void) {
    MSG m;
    if (U(PeekMessageW)) U(PeekMessageW)(&m, NULL, 0, 0, PM_NOREMOVE);
}

/* ---- raw input device registrations are per process: always restore ---------------------- */
static inline UINT rit_reg_count(void) {
    UINT n = 0xdeadbeef;
    if (!U(GetRegisteredRawInputDevices)) return 0;
    if (U(GetRegisteredRawInputDevices)(NULL, &n, sizeof(RAWINPUTDEVICE)) != 0) return 0xffffffffu;
    return n;
}
/* Fetches the registered list. Returns the number of entries (0 if none), caller frees *out. */
static inline int rit_reg_get(RAWINPUTDEVICE **out) {
    UINT n = rit_reg_count();
    *out = NULL;
    if (n == 0 || n > 4096) return 0;
    UINT cap = n + 4;
    RAWINPUTDEVICE *d = calloc(cap, sizeof *d);
    if (!d) return 0;
    int r = (int)U(GetRegisteredRawInputDevices)(d, &cap, sizeof(RAWINPUTDEVICE));
    if (r <= 0) { free(d); return 0; }
    *out = d;
    return r;
}
static inline int rit_reg_find(USHORT page, USHORT usage, RAWINPUTDEVICE *found) {
    RAWINPUTDEVICE *d; int n = rit_reg_get(&d), hit = 0;
    if (found) memset(found, 0, sizeof *found);
    for (int i = 0; i < n; i++)
        if (d[i].usUsagePage == page && d[i].usUsage == usage) { if (found) *found = d[i]; hit = 1; break; }
    free(d);
    return hit;
}
/* Removes every registration of this process (one RIDEV_REMOVE per entry; a page-only entry is
 * the only kind that needs RIDEV_PAGEONLY|RIDEV_REMOVE because its usage is 0, see
 * win32kfull!HidRequestValidityCheck). */
static inline void rit_unreg_all(void) {
    if (!U(RegisterRawInputDevices)) return;
    for (int pass = 0; pass < 3; pass++) {
        RAWINPUTDEVICE *d; int n = rit_reg_get(&d);
        if (!n) return;
        for (int i = 0; i < n; i++) {
            RAWINPUTDEVICE r = { d[i].usUsagePage, d[i].usUsage, RIDEV_REMOVE, NULL };
            if ((d[i].dwFlags & 0x30) == RIDEV_PAGEONLY) r.dwFlags |= RIDEV_PAGEONLY;
            U(RegisterRawInputDevices)(&r, 1, sizeof r);
        }
        free(d);
    }
}

typedef struct { HANDLE h; DWORD type; } rit_dev;
/* Snapshot of the raw input device list. Returns count (0 on none/failure). */
static inline int rit_devices(rit_dev *out, int cap) {
    UINT n = 0;
    if (!U(GetRawInputDeviceList)) return 0;
    if (U(GetRawInputDeviceList)(NULL, &n, sizeof(RAWINPUTDEVICELIST)) != 0 || n == 0 || n > 512) return 0;
    RAWINPUTDEVICELIST *l = calloc(n + 4, sizeof *l);
    UINT cnt = n + 4;
    int r = (int)U(GetRawInputDeviceList)(l, &cnt, sizeof *l);
    int k = 0;
    for (int i = 0; i < r && k < cap; i++) { out[k].h = l[i].hDevice; out[k].type = l[i].dwType; k++; }
    free(l);
    return k;
}

/* Windows owned by a helper GUI thread (for cross-thread ownership rules). */
typedef struct {
    HWND hwnd;
    HANDLE ready, quit, thread;
    DWORD tid;
} rit_owner;
static inline DWORD WINAPI rit_owner_main(LPVOID p) {
    rit_owner *o = p;
    o->hwnd = k_make_window("win32kt-rit-owner");
    o->tid = GetCurrentThreadId();
    SetEvent(o->ready);
    while (WaitForSingleObject(o->quit, 20) == WAIT_TIMEOUT) k_pump(1);
    if (o->hwnd) U(DestroyWindow)(o->hwnd);
    return 0;
}
static inline int rit_owner_start(rit_owner *o) {
    memset(o, 0, sizeof *o);
    o->ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    o->quit = CreateEventW(NULL, TRUE, FALSE, NULL);
    o->thread = CreateThread(NULL, 0, rit_owner_main, o, 0, NULL);
    if (!o->thread || WaitForSingleObject(o->ready, 5000) != WAIT_OBJECT_0 || !o->hwnd) return 0;
    return 1;
}
static inline void rit_owner_stop(rit_owner *o) {
    if (o->quit) SetEvent(o->quit);
    if (o->thread) { WaitForSingleObject(o->thread, 5000); CloseHandle(o->thread); }
    if (o->ready) CloseHandle(o->ready);
    if (o->quit) CloseHandle(o->quit);
    memset(o, 0, sizeof *o);
}

/* ---- raw stubs not in the SDK ----------------------------------------------------------- */
/* win32kfull!NtUserRegisterRawInputDevices(PCRAWINPUTDEVICE, UINT count, int cbSize) -> longlong */
UDECL(LONGLONG, NtUserRegisterRawInputDevices, (const void *, UINT, int));
UDECL(UINT, NtUserGetRegisteredRawInputDevices, (void *, UINT *, UINT));
UDECL(UINT, NtUserGetRawInputDeviceList, (void *, UINT *, UINT));
UDECL(UINT, NtUserGetRawInputDeviceInfo, (HANDLE, UINT, void *, UINT *));
UDECL(UINT, NtUserGetRawInputData, (HANDLE, UINT, void *, UINT *, UINT));
UDECL(INT,  NtUserGetRawInputBuffer, (void *, UINT *, UINT));
/* NtUserGetPointerInfoList(pointerId, kind, history, frame, cbInfo, *entries, *pointerCount, out):
 * user32!GetPointerInfo passes (id,1,0,0,0x60,&1,&1,out); GetPointerTouchInfo (id,2,..,0x90); Pen (id,3,..,0x78);
 * GetPointerFrameInfo (id,1,0,1,0x60,&1,count,out); GetPointerInfoHistory (id,1,1,0,0x60,entries,&1,out). */
UDECL(LONGLONG, NtUserGetPointerInfoList, (UINT, ULONG, int, int, UINT, UINT *, UINT *, void *));
UDECL(BOOL, NtUserGetPointerType, (UINT, ULONG *));
UDECL(LONGLONG, NtUserGetPointerFrameTimes, (UINT, UINT, void *));
UDECL(LONGLONG, NtUserGetPointerInputTransform, (UINT, UINT, void *));
UDECL(BOOL, NtUserGetPointerProprietaryId, (UINT, void *));
UDECL(LONGLONG, NtUserGetPointerCursorId, (UINT, ULONG *));
UDECL(BOOL, NtUserGetExtendedPointerDeviceProperty, (HANDLE, void *));
UDECL(BOOL, NtUserGetPointerDeviceOrientation, (HANDLE, UINT *));
UDECL(BOOL, NtUserGetPointerDeviceInputSpace, (HANDLE, ULONGLONG *));
UDECL(LONGLONG, NtUserModifyWindowTouchCapability, (HWND, ULONG, int));
UDECL(BOOL, NtUserIsTouchWindow, (HWND, ULONG *));
UDECL(BOOL, NtUserGetTouchInputInfo, (HANDLE, UINT, void *, int));
UDECL(BOOL, NtUserGetTouchValidationStatus, (HANDLE));
UDECL(BOOL, NtUserGetGestureInfo, (HANDLE, void *));
UDECL(BOOL, NtUserGetGestureExtArgs, (HANDLE, UINT, void *));
UDECL(LONGLONG, NtUserSetGestureConfig, (HWND, DWORD, UINT, void *, int));
UDECL(LONGLONG, NtUserGetGestureConfig, (HWND, DWORD, ULONG64, UINT *, void *, int));
UDECL(LONGLONG, NtUserRegisterTouchHitTestingWindow, (HWND, ULONG));
UDECL(LONGLONG, NtUserRegisterPointerDeviceNotifications, (HWND, BOOL));
UDECL(LONGLONG, NtUserRegisterPointerInputTarget, (HWND, ULONG64, ULONG, int));
UDECL(ULONGLONG, NtUserEnableMouseInPointer, (UINT));
UDECL(UINT, NtUserIsMouseInPointerEnabled, (void));
UDECL(ULONGLONG, NtUserEnableMouseInPointerForWindow, (HWND, UINT));
UDECL(BOOL, NtUserGetCurrentInputMessageSource, (void *));
UDECL(BOOL, NtUserGetCIMSSM, (void *));
UDECL(BOOL, NtUserGetInputLocaleInfo, (HKL, void *));

/* Wine's win32u aborts on syscalls it does not implement (see rit_veh_once); skip those tests there. */
#define WINE_UNIMPL(what) do { if (t_is_wine()) { t_skip("Wine does not implement %s", what); return; } } while (0)

/* child exit-code accounting: t_check() is global, children return their own failure count */
#define KC(c) do { int ok_ = !!(c); if (!ok_) nfail++; t_check(ok_, __FILE__, __LINE__, #c, NULL); } while (0)

#endif
