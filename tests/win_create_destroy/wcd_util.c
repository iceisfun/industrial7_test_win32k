#include "wcd.h"
#include <wchar.h>

struct wcd_msg wcd_log[WCD_LOG_MAX];
volatile LONG wcd_nlog;
LRESULT wcd_ncreate_ret = TRUE;
LRESULT wcd_create_ret = 0;
void (*wcd_msg_cb)(HWND, UINT, WPARAM, LPARAM);
UINT wcd_log_skip_mask;

void wcd_reset(void) {
    memset(wcd_log, 0, sizeof wcd_log);
    wcd_nlog = 0;
    wcd_ncreate_ret = TRUE;
    wcd_create_ret = 0;
    wcd_msg_cb = NULL;
}
int wcd_find(UINT msg, int from) {
    LONG n = wcd_nlog; if (n > WCD_LOG_MAX) n = WCD_LOG_MAX;
    for (int i = from < 0 ? 0 : from; i < n; i++) if (wcd_log[i].msg == msg) return i;
    return -1;
}
int wcd_find_hwnd(UINT msg, HWND h, int from) {
    LONG n = wcd_nlog; if (n > WCD_LOG_MAX) n = WCD_LOG_MAX;
    for (int i = from < 0 ? 0 : from; i < n; i++) if (wcd_log[i].msg == msg && wcd_log[i].hwnd == h) return i;
    return -1;
}
int wcd_count(UINT msg) {
    int c = 0; LONG n = wcd_nlog; if (n > WCD_LOG_MAX) n = WCD_LOG_MAX;
    for (int i = 0; i < n; i++) if (wcd_log[i].msg == msg) c++;
    return c;
}

BOOL wcd_user(void) {
    return U(CreateWindowExW) && U(DestroyWindow) && U(IsWindow) && U(RegisterClassExW) &&
           U(GetWindowLongW) && U(DefWindowProcW);
}
HINSTANCE wcd_hinst(void) { return GetModuleHandleW(NULL); }

/* copy a NUL-terminated wide string out of the (kernel-marshalled) CREATESTRUCT safely */
static void cap_wstr(const void *p, WCHAR *out, int cap) {
    int i = 0;
    for (; i < cap - 1; i++) {
        WCHAR c;
        if (!k_try_read((const WCHAR *)p + i, &c, 2)) break;
        out[i] = c;
        if (!c) return;
    }
    out[i] = 0;
}

LRESULT CALLBACK wcd_wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    LONG idx = InterlockedIncrement(&wcd_nlog) - 1;
    if (idx < WCD_LOG_MAX) {
        struct wcd_msg *e = &wcd_log[idx];
        memset(e, 0, sizeof *e);
        e->seq = (int)idx; e->msg = m; e->hwnd = h; e->wp = w; e->lp = l; e->tid = GetCurrentThreadId();
        e->is_window = U(IsWindow)(h) ? 1 : 0;
        if (e->is_window) {
            e->style_now = U(GetWindowLongW)(h, GWL_STYLE);
            e->exstyle_now = U(GetWindowLongW)(h, GWL_EXSTYLE);
        }
        if ((m == WM_NCCREATE || m == WM_CREATE) && l) {
            CREATESTRUCTW *cs = (CREATESTRUCTW *)l;
            if (k_try_read(cs, &e->cs, sizeof e->cs)) {
                e->has_cs = 1;
                if (e->cs.lpszName) cap_wstr(e->cs.lpszName, e->cs_name, 96);
                if (((ULONG_PTR)e->cs.lpszClass & ~(ULONG_PTR)0xffff) == 0) e->cs_cls_is_atom = 1;
                else cap_wstr(e->cs.lpszClass, e->cs_cls, 96);
            }
        } else if (m == WM_NCCALCSIZE && l && w) {
            NCCALCSIZE_PARAMS *p = (NCCALCSIZE_PARAMS *)l;
            if (k_try_read(&p->rgrc[0], &e->rc, sizeof e->rc)) e->has_rc = 1;
        } else if (m == WM_NCCALCSIZE && l && !w) {
            if (k_try_read((void *)l, &e->rc, sizeof e->rc)) e->has_rc = 1;
        } else if (m == WM_GETMINMAXINFO && l) {
            if (k_try_read((void *)l, &e->mmi, sizeof e->mmi)) e->has_mmi = 1;
        } else if ((m == WM_WINDOWPOSCHANGING || m == WM_WINDOWPOSCHANGED) && l) {
            if (k_try_read((void *)l, &e->wpos, sizeof e->wpos)) e->has_wpos = 1;
        }
    }
    if (wcd_msg_cb) wcd_msg_cb(h, m, w, l);
    /* DefWindowProc(WM_NCCREATE) is what stores the window text and the create-time system menu */
    if (m == WM_NCCREATE) return wcd_ncreate_ret == TRUE ? U(DefWindowProcW)(h, m, w, l) : wcd_ncreate_ret;
    if (m == WM_CREATE) return wcd_create_ret;
    return U(DefWindowProcW)(h, m, w, l);
}

static WCHAR g_cls[96];
static ATOM g_atom;
const WCHAR *wcd_cls(void) {
    if (!g_cls[0]) swprintf(g_cls, 96, L"win32kt%lu_wcd", GetCurrentProcessId());
    if (!g_atom && U(RegisterClassExW)) {
        WNDCLASSEXW wc; memset(&wc, 0, sizeof wc);
        wc.cbSize = sizeof wc; wc.lpfnWndProc = wcd_wndproc; wc.hInstance = wcd_hinst(); wc.lpszClassName = g_cls;
        g_atom = U(RegisterClassExW)(&wc);
    }
    return g_cls;
}
ATOM wcd_cls_atom(void) { wcd_cls(); return g_atom; }

static LONG g_uniq;
void wcd_uniq_name(WCHAR *out, size_t cch, const char *tag) {
    swprintf(out, cch, L"win32kt%lu_%hs_%ld", GetCurrentProcessId(), tag, InterlockedIncrement(&g_uniq));
}
ATOM wcd_register(const WCHAR *name, WNDPROC proc, UINT style, int cbCls, int cbWnd) {
    WNDCLASSEXW wc; memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc; wc.style = style; wc.lpfnWndProc = proc ? proc : wcd_wndproc;
    wc.cbClsExtra = cbCls; wc.cbWndExtra = cbWnd; wc.hInstance = wcd_hinst(); wc.lpszClassName = name;
    return U(RegisterClassExW)(&wc);
}
void wcd_unregister(const WCHAR *name) { if (U(UnregisterClassW)) U(UnregisterClassW)(name, wcd_hinst()); }

HWND wcd_make(DWORD ex, DWORD style, HWND parent, int x, int y, int w, int h) {
    return U(CreateWindowExW)(ex, wcd_cls(), L"wcd", style, x, y, w, h, parent, NULL, wcd_hinst(), NULL);
}
HWND wcd_make_top(void) {
    return wcd_make(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, WS_POPUP, NULL, -32000, -32000, 100, 80);
}

static void lstr_w(WCD_LSTR *s, const void *w) {
    if (!w) { memset(s, 0, sizeof *s); return; }
    size_t n = wcslen((const WCHAR *)w) * 2;
    s->Length = (ULONG)n; s->MaximumLength = (ULONG)n + 2; s->Ansi = 0; s->Buffer = (PVOID)w;
}
HWND wcd_nt_create_full(DWORD ex, const void *cls, const void *clsver, const WCHAR *name, DWORD style,
                        int x, int y, int w, int h, HWND parent, HMENU menu, HINSTANCE inst,
                        LPVOID param, DWORD zbid, DWORD flagsver, DWORD flags2, PVOID act) {
    WCD_LSTR c, v, n;
    WCD_LSTR *pc, *pv, *pn;
    if (((ULONG_PTR)cls & ~(ULONG_PTR)0xffff) == 0) pc = (WCD_LSTR *)cls; else { lstr_w(&c, cls); pc = &c; }
    if (((ULONG_PTR)clsver & ~(ULONG_PTR)0xffff) == 0) pv = (WCD_LSTR *)clsver; else { lstr_w(&v, clsver); pv = &v; }
    if (name) { lstr_w(&n, name); pn = &n; } else pn = NULL;
    NtUserCreateWindowEx_pfn f = UN(NtUserCreateWindowEx);
    if (!f) return NULL;
    return f(ex, pc, pv, pn, style, x, y, w, h, parent, menu, inst, param, zbid, flagsver, flags2, act);
}
HWND wcd_nt_create(DWORD ex, const void *cls, const WCHAR *name, DWORD style, int x, int y,
                   int w, int h, HWND parent, HMENU menu, HINSTANCE inst, LPVOID param) {
    return wcd_nt_create_full(ex, cls, cls, name, style, x, y, w, h, parent, menu, inst, param, 0, WCD_WINVER, 0, NULL);
}
void wcd_destroy(HWND h) { if (h && U(IsWindow)(h)) U(DestroyWindow)(h); }
int wcd_user_objects(void) { return U(GetGuiResources) ? (int)U(GetGuiResources)(GetCurrentProcess(), GR_USEROBJECTS) : -1; }

void *wcd_sw(HWND h) {
    void *p = k_wnd_ptr(h);
    if (!p) return NULL;
    unsigned short fnid;
    if (!k_try_read_u16((char *)p + SW_FNID, &fnid)) return NULL;
    return p;
}
bool wcd_entry_live(HWND h) {
    struct k_user_entry e;
    if (!k_user_entry(h, &e)) return false;
    return e.type == 1 && !(e.flags & 1) && (e.uniq == (unsigned short)((ULONG_PTR)h >> 16) || e.uniq == 0xffff);
}
unsigned long long wcd_handle_count(void) {
    struct k_user_shared_info si;
    if (!k_shared_info(&si) || !si.psi) return 0;
    unsigned long long v = 0;
    if (!k_try_read((char *)si.psi + 8, &v, 8)) return 0;
    return v;
}
bool wcd_dpi_plain(void) {
    if (!U(GetDC) || !U(GetDeviceCaps) || !U(ReleaseDC)) return false;
    HDC dc = U(GetDC)(NULL); if (!dc) return false;
    int a = U(GetDeviceCaps)(dc, HORZRES), b = U(GetDeviceCaps)(dc, DESKTOPHORZRES);
    U(ReleaseDC)(NULL, dc);
    return a == b;
}

static DWORD WINAPI gt_proc(LPVOID p) {
    struct wcd_gt *g = (struct wcd_gt *)p;
    if (g->make_top) g->top = wcd_make_top();
    if (g->extra) g->extra(g);
    SetEvent(g->ev_ready);
    for (;;) {
        DWORD r = MsgWaitForMultipleObjects(1, &g->ev_stop, FALSE, 100, QS_ALLINPUT);
        MSG m;
        while (U(PeekMessageW)(&m, NULL, 0, 0, PM_REMOVE)) { U(DispatchMessageW)(&m); InterlockedIncrement(&g->pumped); }
        if (r == WAIT_OBJECT_0) break;
    }
    return 0;
}
bool wcd_gt_start(struct wcd_gt *g, int make_top) {
    void (*ex)(struct wcd_gt *) = g->extra; void *arg = g->arg;
    memset(g, 0, sizeof *g);
    g->extra = ex; g->arg = arg;
    g->make_top = make_top;
    g->ev_ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    g->ev_stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    g->th = CreateThread(NULL, 0, gt_proc, g, 0, &g->tid);
    if (!g->th) return false;
    return WaitForSingleObject(g->ev_ready, 5000) == WAIT_OBJECT_0;
}
void wcd_gt_stop(struct wcd_gt *g) {
    if (g->ev_stop) SetEvent(g->ev_stop);
    if (g->th) { WaitForSingleObject(g->th, 5000); CloseHandle(g->th); }
    if (g->ev_stop) CloseHandle(g->ev_stop);
    if (g->ev_ready) CloseHandle(g->ev_ready);
    g->th = g->ev_stop = g->ev_ready = NULL;
}
