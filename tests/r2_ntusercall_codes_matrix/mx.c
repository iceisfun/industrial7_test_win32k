/* helpers for r2_ntusercall_codes_matrix */
#include "mx.h"

void mx_init(void) { t_module("user32.dll"); }

static DWORD WINAPI mx_bg_proc(LPVOID p) {
    struct mx_bg *b = (struct mx_bg *)p;
    b->hwnd = k_make_window("r2ncm_bg");
    if (b->hwnd && b->helpid) {
        BOOL (WINAPI *set)(HWND, DWORD) = (BOOL (WINAPI *)(HWND, DWORD))t_proc_in("user32.dll", "SetWindowContextHelpId");
        if (set) set(b->hwnd, b->helpid);
    }
    SetEvent(b->ready);
    BOOL (WINAPI *pk)(LPMSG, HWND, UINT, UINT, UINT) = (BOOL (WINAPI *)(LPMSG, HWND, UINT, UINT, UINT))t_proc_in("user32.dll", "PeekMessageA");
    LRESULT (WINAPI *dm)(const MSG *) = (LRESULT (WINAPI *)(const MSG *))t_proc_in("user32.dll", "DispatchMessageA");
    DWORD start = GetTickCount();
    while (WaitForSingleObject(b->stop, 5) == WAIT_TIMEOUT && GetTickCount() - start < 20000) {
        MSG m;
        while (pk && pk(&m, NULL, 0, 0, PM_REMOVE)) { if (dm) dm(&m); }
    }
    BOOL (WINAPI *dw)(HWND) = (BOOL (WINAPI *)(HWND))t_proc_in("user32.dll", "DestroyWindow");
    if (b->hwnd && dw) dw(b->hwnd);
    return 0;
}

int mx_bg_start(struct mx_bg *b, DWORD helpid) {
    memset(b, 0, sizeof *b);
    b->helpid = helpid;
    b->ready = CreateEventA(NULL, TRUE, FALSE, NULL);
    b->stop = CreateEventA(NULL, TRUE, FALSE, NULL);
    b->th = CreateThread(NULL, 0, mx_bg_proc, b, 0, &b->tid);
    if (!b->ready || !b->stop || !b->th) return 0;
    if (WaitForSingleObject(b->ready, 5000) != WAIT_OBJECT_0) return 0;
    return b->hwnd != NULL;
}

void mx_bg_stop(struct mx_bg *b) {
    if (b->stop) SetEvent(b->stop);
    if (b->th) { WaitForSingleObject(b->th, 5000); CloseHandle(b->th); }
    if (b->ready) CloseHandle(b->ready);
    if (b->stop) CloseHandle(b->stop);
    memset(b, 0, sizeof *b);
}

void mx_snap_take(struct mx_snap *s) {
    memset(s, 0, sizeof *s);
    s->fg = U(GetForegroundWindow)();
    s->cap = U(GetCapture)();
    s->focus = U(GetFocus)();
    s->act = U(GetActiveWindow)();
    U(GetCursorPos)(&s->cur);
    s->blink = U(GetCaretBlinkTime)();
    s->dbl = U(GetDoubleClickTime)();
    k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &s->w32ti);
    s->extra = U(GetMessageExtraInfo)();
    s->swap = U(GetSystemMetrics)(SM_SWAPBUTTON);
    DWORD lay = 0;
    if (U(GetProcessDefaultLayout)) U(GetProcessDefaultLayout)(&lay);
    s->layout = lay;
}

int mx_snap_equal(const struct mx_snap *a, const struct mx_snap *b) {
    /* foreground window and cursor position belong to the user, not to this thread: not compared */
    return a->cap == b->cap && a->focus == b->focus && a->act == b->act &&
           a->blink == b->blink && a->dbl == b->dbl &&
           a->w32ti == b->w32ti && a->extra == b->extra && a->swap == b->swap && a->layout == b->layout;
}

HWND mx_visible_window(HWND owner, DWORD style_extra) {
    HWND probe = k_make_window("r2ncm_cls");   /* registers the private class */
    if (!probe) return NULL;
    U(DestroyWindow)(probe);
    HWND h = U(CreateWindowExA)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, "win32ktHelperWnd", "r2ncm_vis",
                                WS_POPUP | style_extra, -32000, -32000, 8, 8, owner, NULL, GetModuleHandleA(NULL), NULL);
    if (h) U(ShowWindow)(h, SW_SHOWNOACTIVATE);
    return h;
}
