/* dx_desktop_switch: worker threads that live on a chosen desktop, and a process launcher that
 * places a child on a named desktop through STARTUPINFO.lpDesktop. */
#include "dxd.h"

static LRESULT CALLBACK thr_wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_NCCREATE) {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)l;
        U(SetWindowLongPtrW)(h, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
    } else if (m == DXD_WM_COUNT) {
        struct dxd_thr *t = (struct dxd_thr *)U(GetWindowLongPtrW)(h, GWLP_USERDATA);
        if (t) InterlockedIncrement(&t->hits);
        return 77;
    }
    return U(DefWindowProcW)(h, m, w, l);
}

static DWORD WINAPI thr_main(void *p) {
    struct dxd_thr *t = (struct dxd_thr *)p;
    HINSTANCE hi = GetModuleHandleW(NULL);
    t->tid = GetCurrentThreadId();
    if (t->desk) { SetLastError(0); t->set_ok = U(SetThreadDesktop)(t->desk); t->set_err = GetLastError(); }
    t->got = U(GetThreadDesktop)(t->tid);
    if (t->want_window) {
        WNDCLASSW wc; memset(&wc, 0, sizeof wc);
        wc.lpfnWndProc = thr_wndproc; wc.hInstance = hi; wc.lpszClassName = t->cls;
        U(RegisterClassW)(&wc);
        t->hw = U(CreateWindowExW)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, t->cls, L"dxd", WS_POPUP,
                                   -32000, -32000, 8, 8, NULL, NULL, hi, t);
        if (t->hw && t->visible) U(ShowWindow)(t->hw, SW_SHOWNA);
    }
    if (t->action) t->action(t);
    SetEvent(t->ready);
    DWORD t0 = GetTickCount();
    while (WaitForSingleObject(t->done, 15) == WAIT_TIMEOUT && GetTickCount() - t0 < 20000) {
        MSG m;
        while (U(PeekMessageW)(&m, NULL, 0, 0, PM_REMOVE)) { U(TranslateMessage)(&m); U(DispatchMessageW)(&m); }
    }
    if (t->hw) U(DestroyWindow)(t->hw);
    if (t->want_window) U(UnregisterClassW)(t->cls, hi);
    return 0;
}

int dxd_thr_start(struct dxd_thr *t, const char *tag) {
    t->ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    t->done = CreateEventW(NULL, TRUE, FALSE, NULL);
    swprintf(t->cls, 96, L"win32kt%lu_%S", GetCurrentProcessId(), tag);
    t->th = CreateThread(NULL, 0, thr_main, t, 0, NULL);
    return t->th && WaitForSingleObject(t->ready, 5000) == WAIT_OBJECT_0;
}

void dxd_thr_stop(struct dxd_thr *t) {
    SetEvent(t->done);
    if (t->th) { WaitForSingleObject(t->th, 5000); CloseHandle(t->th); t->th = NULL; }
    CloseHandle(t->ready); CloseHandle(t->done);
}

HANDLE dxd_spawn_on_desktop(const char *child, const char *extra, const char *winsta_desk, PROCESS_INFORMATION *pi_out) {
    char cmd[4096]; STARTUPINFOA si; PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si); si.cb = sizeof si;
    si.lpDesktop = (char *)winsta_desk;
    HANDLE so = GetStdHandle(STD_OUTPUT_HANDLE); DWORD oldf = 0; int restore = 0;
    BOOL inherit = FALSE;
    if (so && so != INVALID_HANDLE_VALUE && GetHandleInformation(so, &oldf)) {
        if (!(oldf & HANDLE_FLAG_INHERIT)) { SetHandleInformation(so, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT); restore = 1; }
        si.dwFlags |= STARTF_USESTDHANDLES; si.hStdOutput = so; si.hStdError = so; si.hStdInput = NULL;
        inherit = TRUE;
    }
    snprintf(cmd, sizeof cmd, "\"%s\" --child %s %s", t_self_path(), child, extra ? extra : "");
    memset(&pi, 0, sizeof pi);
    BOOL ok = CreateProcessA(NULL, cmd, NULL, NULL, inherit, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    if (restore) SetHandleInformation(so, HANDLE_FLAG_INHERIT, 0);
    if (!ok) return NULL;
    if (pi_out) *pi_out = pi; else CloseHandle(pi.hThread);
    return pi.hProcess;
}

int dxd_have_window(struct dxd_thr *t) {
    if (t->hw) return 1;
    if (t_is_wine()) { t_skip("worker window creation unsupported here"); return 1; }
    return 0;
}
