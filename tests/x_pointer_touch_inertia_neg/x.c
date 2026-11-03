#include "x.h"

jmp_buf xp_jb; volatile int xp_armed; volatile unsigned long xp_exc; static int veh_on;
static LONG CALLBACK xp_veh(PEXCEPTION_POINTERS ep) {
    DWORD c = ep->ExceptionRecord->ExceptionCode;
    if (xp_armed && ((c & 0xC0000000) == 0xC0000000 || c == 0x80000002 || c == 0x80000100) && c != 0xC0000194 && c != 0xC000013A) {
        xp_armed = 0; xp_exc = c; longjmp(xp_jb, 1);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
void xp_run_guarded(void (*f)(void)) {
    /* a win32k service issued before user32 is loaded ends the process: the thread cannot become a GUI thread */
    t_module("user32.dll"); t_module("gdi32.dll");
    if (!veh_on) { AddVectoredExceptionHandler(1, xp_veh); veh_on = 1; }
    if (!setjmp(xp_jb)) { xp_armed = 1; f(); xp_armed = 0; }
    else if (t_is_wine()) t_skip("emulator fault 0x%lx", xp_exc);
    else t_check(0, __FILE__, __LINE__, "no exception", "exception 0x%lx escaped a syscall", xp_exc);
}

void xp_fail(LONGLONG ret, DWORD gle, DWORD want, const char *file, int line, const char *what) {
    t_check((ret & 0xFFFFFFFF) == 0, file, line, what, "call unexpectedly returned 0x%llx", (unsigned long long)ret);
    t_check(gle == want, file, line, what, "last error %lu want %lu", gle, want);
}

static DWORD WINAPI other_main(LPVOID p) {
    struct xp_other *o = p;
    o->hwnd = k_make_window("xp_other");
    if (o->hwnd && o->touch_flags >= 0 && UN(NtUserModifyWindowTouchCapability))
        UN(NtUserModifyWindowTouchCapability)(o->hwnd, (ULONG)o->touch_flags, 1);
    o->tid = GetCurrentThreadId();
    SetEvent(o->ready);
    while (WaitForSingleObject(o->stop, 0) == WAIT_TIMEOUT) k_pump(10);
    if (o->hwnd) U(DestroyWindow)(o->hwnd);
    return 0;
}
int xp_other_start(struct xp_other *o, int touch_flags) {
    memset(o, 0, sizeof *o);
    o->touch_flags = touch_flags;
    o->ready = CreateEventA(NULL, TRUE, FALSE, NULL); o->stop = CreateEventA(NULL, TRUE, FALSE, NULL);
    o->thread = CreateThread(NULL, 0, other_main, o, 0, NULL);
    if (!o->thread || WaitForSingleObject(o->ready, 10000) != WAIT_OBJECT_0 || !o->hwnd) { xp_other_stop(o); return 0; }
    return 1;
}
void xp_other_stop(struct xp_other *o) {
    if (o->stop) SetEvent(o->stop);
    if (o->thread) { WaitForSingleObject(o->thread, 10000); CloseHandle(o->thread); }
    if (o->ready) CloseHandle(o->ready);
    if (o->stop) CloseHandle(o->stop);
    memset(o, 0, sizeof *o);
}

int xp_touch_flags(HWND h) {
    ULONG g = 0xA5A5A5A5;
    if (!UN(NtUserIsTouchWindow)(h, &g)) return -1;
    return (int)g;
}
