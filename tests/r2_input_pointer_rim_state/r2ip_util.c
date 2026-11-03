#include "r2ip.h"
#include <stdio.h>
#include <stdlib.h>

static const char *const wine_unimplemented[] = {
    "NtRIMEnableMonitorMappingForDevice",
    "NtRIMFreeInputBuffer",
    "NtRIMGetDevicePreparsedDataLockfree",
    "NtRIMGetDevicePropertiesLockfree",
    "NtRIMObserveNextInput",
    "NtRIMOnPnpNotification",
    "NtRIMOnTimerNotification",
    "NtRIMQueryDevicePath",
    "NtRIMRemoveInputObserver",
    "NtRIMSetExtendedDeviceProperty",
    "NtRIMSetTestModeStatus",
    "NtRIMUnregisterForInput",
    "NtUserCanBrokerForceForeground",
    "NtUserGetQueueStatusReadonly",
    "NtUserInitializeGenericHidInjection",
    "NtUserInitializeInputDeviceInjection",
    "NtUserInitializePointerDeviceInjection",
    "NtUserInitializePointerDeviceInjectionEx",
    "NtUserInitializeTouchInjection",
    "NtUserInjectDeviceInput",
    "NtUserInjectGenericHidInput",
    "NtUserInjectKeyboardInput",
    "NtUserInjectMouseInput",
    "NtUserIsMouseInputEnabled",
    "NtUserNavigateFocus",
    "NtUserPostKeyboardInputMessage",
    "NtUserPromoteMouseInPointer",
    "NtUserRealInternalGetMessage",
    "NtUserRealWaitMessageEx",
    "NtUserRemoveInjectionDevice",
    "NtUserSetBrokeredForeground",
    "NtUserSetFallbackForeground",
    "NtUserSetForegroundWindowForApplication",
    "NtUserSetThreadInputBlocked",
    "NtUserWaitAvailableMessageEx",
    "NtUserWaitForMsgAndEvent",
};
FARPROC r2_sysproc(const char *name) {
    if (t_is_wine()) {
        for (size_t i = 0; i < sizeof wine_unimplemented / sizeof *wine_unimplemented; i++)
            if (!strcmp(name, wine_unimplemented[i])) return NULL;
    }
    return t_proc(name);
}

static DWORD g_main_tid;
static LONG CALLBACK stub_veh(PEXCEPTION_POINTERS ep) {
    if (ep->ExceptionRecord->ExceptionCode == 0x80000100) {
        if (GetCurrentThreadId() == g_main_tid) ep->ExceptionRecord->ExceptionCode = 0xC0000005;
        else { printf("  NOTE unimplemented syscall stub hit on a helper thread (emulator)\n"); ExitThread(0xDEAD0002); }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
void r2_veh_once(void) {
    static int done;
    if (done) return;
    done = 1;
    /* The exe does not link user32. On Windows 10 a process without user32 has no PEB.KernelCallbackTable and its first win32k
     * syscall is fatal (access violation, then 0xC000041D), so load it as any GUI process would. */
    t_module("user32.dll");
    g_main_tid = GetCurrentThreadId();
    AddVectoredExceptionHandler(1, stub_veh);
}

static r2_body g_body;
static DWORD WINAPI gui_thunk(LPVOID p) {
    (void)p;
    if (U(GetQueueStatus)) U(GetQueueStatus)(R2_QS_ALL_VALID);   /* first win32k call: converts the thread, creates its queue */
    g_body();
    return 0;
}
void r2_gui_thread(r2_body b) {
    g_body = b;
    HANDLE h = CreateThread(NULL, 0, gui_thunk, NULL, 0, NULL);
    if (!h) { t_note("CreateThread failed"); return; }
    if (WaitForSingleObject(h, 60000) != WAIT_OBJECT_0) {
        CHECKF(0, "gui test thread did not finish within 60s");
        TerminateThread(h, 1);
    }
    CloseHandle(h);
}

static LRESULT (WINAPI *g_defproc)(HWND, UINT, WPARAM, LPARAM);
volatile LONG r2_hit[8];
volatile DWORD r2_hit_tid[8];
static LRESULT CALLBACK r2_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m >= WM_APP && m < WM_APP + 8) { r2_hit_tid[m - WM_APP] = GetCurrentThreadId(); InterlockedIncrement(&r2_hit[m - WM_APP]); return 0x5151; }
    return g_defproc(h, m, w, l);
}
void r2_hits_reset(void) { for (int i = 0; i < 8; i++) { r2_hit[i] = 0; r2_hit_tid[i] = 0; } }
static wchar_t g_cls[64];
HWND r2_window(DWORD style) {
    if (!U(RegisterClassExW) || !U(CreateWindowExW) || !U(DefWindowProcW)) return NULL;
    if (!g_cls[0]) {
        g_defproc = (LRESULT (WINAPI *)(HWND, UINT, WPARAM, LPARAM))U(DefWindowProcW);
        _snwprintf(g_cls, 64, L"win32kt_r2ip_%lu", GetCurrentProcessId());
        WNDCLASSEXW wc; memset(&wc, 0, sizeof wc);
        wc.cbSize = sizeof wc; wc.lpfnWndProc = r2_proc; wc.hInstance = GetModuleHandleW(NULL);
        wc.lpszClassName = g_cls;
        if (!U(RegisterClassExW)(&wc)) t_note("RegisterClassExW failed: %lu", GetLastError());
    }
    HWND h = U(CreateWindowExW)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, g_cls, L"r2ip", style ? style : WS_POPUP,
                                -32000, -32000, 40, 30, NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!h) t_note("CreateWindowExW failed: %lu", GetLastError());
    return h;
}
void r2_pump_discard(void) {
    MSG m; int n = 0;
    if (!U(PeekMessageW)) return;
    while (n++ < 2000 && U(PeekMessageW)(&m, NULL, 0, 0, PM_REMOVE)) { }
}

void r2_cti_read(struct r2_cti *c) {
    void *p = NULL; unsigned char b[16];
    memset(c, 0, sizeof *c);
    if (!k_try_read_ptr((char *)k_teb() + TEB_Win32ClientInfo + 0x60, &p) || !p) return;
    if (!k_try_read(p, b, sizeof b)) return;
    c->change = *(unsigned short *)(b + 4);
    c->wake = *(unsigned short *)(b + 6);
    c->journal = *(unsigned short *)(b + 8);
    c->wakemask = *(unsigned short *)(b + 10);
    c->tick = *(unsigned long *)(b + 12);
    c->ok = 1;
}

BOOL r2_run_thread(LPTHREAD_START_ROUTINE fn, void *arg) {
    HANDLE h = CreateThread(NULL, 0, fn, arg, 0, NULL);
    if (!h) return FALSE;
    BOOL ok = WaitForSingleObject(h, 10000) == WAIT_OBJECT_0;
    if (!ok) TerminateThread(h, 1);
    CloseHandle(h);
    return ok;
}

int r2_injection_permitted(void) {
    if (!UNW(NtUserInitializeInputDeviceInjection)) return -1;
    SET_ERR(0);
    LONG r = UNW(NtUserInitializeInputDeviceInjection)(0, 0, NULL, 0, NULL, 0, NULL);
    DWORD e = GetLastError();
    if (r != 0) return -1;
    return e != E_ACCESS;
}
