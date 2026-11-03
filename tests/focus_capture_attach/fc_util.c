#include "fc.h"

struct fc_msg fc_log[FC_LOG_MAX];
volatile LONG fc_nlog;

int fc_own_foreground(void) {
    HWND f = U(GetForegroundWindow)(); DWORD pid = 0;
    if (f) U(GetWindowThreadProcessId)(f, &pid);
    return f && pid == GetCurrentProcessId();
}
void fc_reset(void) { memset(fc_log, 0, sizeof fc_log); fc_nlog = 0; }
int fc_total(void) { LONG n = fc_nlog; return n > FC_LOG_MAX ? FC_LOG_MAX : (int)n; }
int fc_find(UINT msg, HWND h, int from) {
    int n = fc_total();
    for (int i = from < 0 ? 0 : from; i < n; i++)
        if (fc_log[i].msg == msg && (!h || fc_log[i].hwnd == h)) return i;
    return -1;
}
int fc_find_wp(UINT msg, HWND h, WPARAM wp, int from) {
    int n = fc_total();
    for (int i = from < 0 ? 0 : from; i < n; i++)
        if (fc_log[i].msg == msg && (!h || fc_log[i].hwnd == h) && fc_log[i].wp == wp) return i;
    return -1;
}
int fc_find_lo(UINT msg, HWND h, WORD lo, int from) {
    int n = fc_total();
    for (int i = from < 0 ? 0 : from; i < n; i++)
        if (fc_log[i].msg == msg && (!h || fc_log[i].hwnd == h) && LOWORD(fc_log[i].wp) == lo) return i;
    return -1;
}
int fc_count(UINT msg, HWND h) {
    int c = 0, n = fc_total();
    for (int i = 0; i < n; i++) if (fc_log[i].msg == msg && (!h || fc_log[i].hwnd == h)) c++;
    return c;
}

int fc_user(void) {
    return U(CreateWindowExW) && U(DestroyWindow) && U(IsWindow) && U(RegisterClassExW) &&
           U(DefWindowProcW) && U(ShowWindow) && U(GetWindowLongW) && U(SetFocus) && U(GetFocus) &&
           U(SetActiveWindow) && U(GetActiveWindow) && U(SetCapture) && U(GetCapture) &&
           U(ReleaseCapture) && U(EnableWindow) && U(IsWindowEnabled) && U(PeekMessageW) &&
           U(DispatchMessageW) && U(GetMessageW) && U(SendMessageTimeoutW) && U(GetForegroundWindow) &&
           U(GetGUIThreadInfo) && U(CreateCaret) && U(DestroyCaret) && U(ShowCaret) && U(HideCaret) &&
           U(SetCaretPos) && U(GetCaretPos) && U(AttachThreadInput) && U(PostThreadMessageW) &&
           U(GetWindowThreadProcessId) && U(SetForegroundWindow);
}

/* ---- logging window procedure -------------------------------------------------------------- */
static int logged(UINT m) {
    switch (m) {
    case WM_ACTIVATE: case WM_SETFOCUS: case WM_KILLFOCUS: case WM_ENABLE: case WM_DESTROY:
    case WM_NCDESTROY: case WM_CANCELMODE: case WM_ACTIVATEAPP: case WM_NCACTIVATE:
    case WM_CAPTURECHANGED: case WM_MOUSEACTIVATE: return 1;
    }
    return 0;
}
static LRESULT CALLBACK fc_wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == FC_WM_CALL) return (LRESULT)((DWORD_PTR (*)(void *))w)((void *)l);
    if (logged(m)) {
        LONG idx = InterlockedIncrement(&fc_nlog) - 1;
        if (idx < FC_LOG_MAX) {
            struct fc_msg *e = &fc_log[idx];
            memset(e, 0, sizeof *e);
            e->seq = idx; e->msg = m; e->hwnd = h; e->wp = w; e->lp = l; e->tid = GetCurrentThreadId();
            e->is_window = U(IsWindow)(h) ? 1 : 0;
            if (e->is_window) e->style_now = U(GetWindowLongW)(h, GWL_STYLE);
            e->focus_now = U(GetFocus)(); e->active_now = U(GetActiveWindow)(); e->capture_now = U(GetCapture)();
        }
    }
    return U(DefWindowProcW)(h, m, w, l);
}

static void fg_save(void);
static WCHAR g_cls[64];
const WCHAR *fc_class(void) {
    if (!g_cls[0]) {
        WNDCLASSEXW wc; memset(&wc, 0, sizeof wc);
        _snwprintf(g_cls, 64, L"win32kt_fc_%lu", GetCurrentProcessId());
        wc.cbSize = sizeof wc; wc.lpfnWndProc = fc_wndproc; wc.hInstance = GetModuleHandleW(NULL);
        wc.lpszClassName = g_cls;
        if (!U(RegisterClassExW)(&wc)) g_cls[0] = 0;
    }
    return g_cls;
}

HWND fc_make(DWORD style, DWORD ex, HWND parent, int visible) {
    const WCHAR *c = fc_class();
    if (!c[0]) return NULL;
    fg_save();
    int x = (style & WS_CHILD) ? 0 : -32000;
    HWND h = U(CreateWindowExW)(ex, c, L"win32kt_fc", style, x, x, (style & WS_CHILD) ? 20 : 10,
                                (style & WS_CHILD) ? 20 : 10, parent, NULL, GetModuleHandleW(NULL), NULL);
    if (h && visible) U(ShowWindow)(h, SW_SHOWNOACTIVATE);
    return h;
}
HWND fc_top(void) { return fc_make(WS_POPUP, WS_EX_TOOLWINDOW, NULL, 1); }
HWND fc_hidden(void) { return fc_make(WS_POPUP, WS_EX_TOOLWINDOW, NULL, 0); }
HWND fc_owned(HWND owner) { return fc_make(WS_POPUP, WS_EX_TOOLWINDOW, owner, 1); }
HWND fc_child(HWND parent) { return fc_make(WS_CHILD | WS_VISIBLE, 0, parent, 0); }
/* If one of the calls under test made an off-screen window of ours the foreground window, hand the
 * foreground back to the window that had it when the corpus first created a window. */
static HWND g_fg0;
static int g_fg0_saved;
static void fg_save(void) {
    if (g_fg0_saved) return;
    g_fg0_saved = 1;
    HWND f = U(GetForegroundWindow)(); DWORD pid = 0;
    if (f) { U(GetWindowThreadProcessId)(f, &pid); if (pid != GetCurrentProcessId()) g_fg0 = f; }
}
void fc_fg_restore(void) {
    HWND f = U(GetForegroundWindow)(); DWORD pid = 0;
    if (!f || !g_fg0 || !U(SetForegroundWindow)) return;
    U(GetWindowThreadProcessId)(f, &pid);
    if (pid == GetCurrentProcessId() && U(IsWindow)(g_fg0)) U(SetForegroundWindow)(g_fg0);
}
void fc_destroy(HWND h) { if (h && U(IsWindow)(h)) U(DestroyWindow)(h); fc_fg_restore(); }

void fc_quiesce(void) {
    U(ReleaseCapture)();
    U(SetFocus)(NULL);
    k_pump(0);
    fc_reset();
}

/* ---- worker threads ------------------------------------------------------------------------ */
static DWORD WINAPI thr_main(LPVOID p) {
    struct fc_thr *t = (struct fc_thr *)p;
    t->w[0] = fc_top(); t->w[1] = fc_top();
    t->ok = t->w[0] && t->w[1];
    SetEvent(t->ready);
    if (!t->ok) return 1;
    MSG m;
    while (U(GetMessageW)(&m, NULL, 0, 0) > 0) { U(TranslateMessage)(&m); U(DispatchMessageW)(&m); }
    return 0;
}
int fc_thr_start(struct fc_thr *t) {
    memset(t, 0, sizeof *t);
    if (!fc_user() || !U(TranslateMessage)) return 0;
    t->ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    t->th = CreateThread(NULL, 0, thr_main, t, 0, &t->tid);
    if (!t->th) return 0;
    if (WaitForSingleObject(t->ready, 5000) != WAIT_OBJECT_0 || !t->ok) { fc_thr_stop(t); return 0; }
    return 1;
}
int fc_thr_call(struct fc_thr *t, DWORD_PTR (*fn)(void *), void *arg, DWORD_PTR *res) {
    DWORD_PTR r = 0;
    LRESULT ok = U(SendMessageTimeoutW)(t->w[0], FC_WM_CALL, (WPARAM)fn, (LPARAM)arg, SMTO_NORMAL, 5000, &r);
    if (res) *res = r;
    return ok != 0;
}
void fc_thr_stop(struct fc_thr *t) {
    if (t->th) {
        if (t->tid && U(PostThreadMessageW)) U(PostThreadMessageW)(t->tid, WM_QUIT, 0, 0);
        if (WaitForSingleObject(t->th, 3000) != WAIT_OBJECT_0) TerminateThread(t->th, 0);
        CloseHandle(t->th);
    }
    if (t->ready) CloseHandle(t->ready);
    memset(t, 0, sizeof *t);
    fc_fg_restore();
}

void fc_detach_all(DWORD a, DWORD b) {
    if (!U(AttachThreadInput)) return;
    for (int i = 0; i < 6; i++) if (!U(AttachThreadInput)(a, b, FALSE)) break;
}

bool fc_psi_dword(unsigned off, unsigned long *out) {
    struct k_user_shared_info si;
    if (!k_shared_info(&si) || !si.psi) return false;
    return k_try_read_u32((const char *)si.psi + off, out);
}
unsigned long fc_ci_dword(unsigned off) {
    unsigned long v = 0;
    if (!k_try_read_u32((const char *)k_teb() + TEB_Win32ClientInfo + off, &v)) return 0;
    return v;
}
BOOL fc_gti(DWORD tid, GUITHREADINFO *g) {
    memset(g, 0xCC, sizeof *g);
    g->cbSize = sizeof *g;
    return U(GetGUIThreadInfo)(tid, g);
}
