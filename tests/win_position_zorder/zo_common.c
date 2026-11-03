#include "zo.h"

struct zo_msg zo_log[ZO_LOG_MAX];
int zo_log_n;
int zo_log_on;
unsigned zo_beh;
int zo_ghost = 1;
int zo_beh_cx, zo_beh_cy, zo_beh_dx, zo_beh_dy;
MINMAXINFO zo_mm;

static HWND g_reg[1024];
static DWORD g_regtid[1024];
static volatile LONG g_nreg;
static int g_cls_done;
static ATOM g_atom1, g_atom2;
static char g_cls1[64], g_cls2[64];

HINSTANCE zo_hinst(void) { return GetModuleHandleA(NULL); }

void zo_log_clear(void) { zo_log_n = 0; }
int zo_log_count(HWND h, UINT msg) {
    int c = 0;
    for (int i = 0; i < zo_log_n; i++) if (zo_log[i].msg == msg && (!h || zo_log[i].h == h)) c++;
    return c;
}
int zo_log_find(HWND h, UINT msg, int from) {
    for (int i = from < 0 ? 0 : from; i < zo_log_n; i++) if (zo_log[i].msg == msg && (!h || zo_log[i].h == h)) return i;
    return -1;
}
const struct zo_msg *zo_log_get(int i) { return (i >= 0 && i < zo_log_n) ? &zo_log[i] : NULL; }

static void zo_record(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (!zo_log_on || zo_log_n >= ZO_LOG_MAX) return;
    struct zo_msg *e = &zo_log[zo_log_n];
    memset(e, 0, sizeof *e);
    e->h = h; e->msg = m; e->w = w; e->l = l; e->seq = zo_log_n;
    if (m == WM_WINDOWPOSCHANGING || m == WM_WINDOWPOSCHANGED) e->wp = *(WINDOWPOS *)l;
    zo_log_n++;
}

static LRESULT CALLBACK zo_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)l;
        zo_record(h, m, w, l);
        if (zo_beh & ZO_B_MINMAX_USER) {
            *mm = zo_mm;
        } else {
            /* keep a maximized fixture window off-screen and small */
            mm->ptMaxPosition.x = ZO_OFFX; mm->ptMaxPosition.y = ZO_OFFY;
            mm->ptMaxSize.x = 300; mm->ptMaxSize.y = 200;
        }
        return 0;
    }
    case WM_WINDOWPOSCHANGING: {
        WINDOWPOS *wp = (WINDOWPOS *)l;
        zo_record(h, m, w, l);
        if ((zo_beh & ZO_B_CLAMP_SIZE) && !(wp->flags & SWP_NOSIZE)) { wp->cx = zo_beh_cx; wp->cy = zo_beh_cy; }
        if ((zo_beh & ZO_B_SHIFT_POS) && !(wp->flags & SWP_NOMOVE)) { wp->x += zo_beh_dx; wp->y += zo_beh_dy; }
        if (zo_beh & ZO_B_FORCE_NOMOVE) wp->flags |= SWP_NOMOVE;
        if (zo_beh & ZO_B_FORCE_NOZ) wp->flags |= SWP_NOZORDER;
        if (zo_beh & ZO_B_FORCE_NOSIZE) wp->flags |= SWP_NOSIZE;
        break;
    }
    case WM_NCHITTEST:
        if (GetPropA(h, "zoHTT")) return HTTRANSPARENT;
        break;
    case WM_WINDOWPOSCHANGED: case WM_SHOWWINDOW: case WM_SIZE: case WM_MOVE: case WM_NCCALCSIZE:
    case WM_ACTIVATE: case WM_NCACTIVATE: case WM_PARENTNOTIFY: case WM_STYLECHANGED: case WM_STYLECHANGING:
    case WM_ENABLE: case WM_MOVING: case WM_SIZING: case WM_CHILDACTIVATE: case WM_NCCREATE: case WM_CREATE:
    case WM_DESTROY: case WM_NCDESTROY:
        zo_record(h, m, w, l);
        break;
    }
    return DefWindowProcA(h, m, w, l);
}

static void zo_register(void) {
    if (g_cls_done) return;
    g_cls_done = 1;
    /* Windows creates a hidden default IME window owned by the first window of every GUI thread; an owner drags its
     * owned windows along in the z-order, which makes sibling adjacency and topmost checks depend on the input
     * method. Switch the input method off for this process before any window exists. */
    {
        HMODULE imm = t_module("imm32.dll");
        BOOL (WINAPI *disable_ime)(DWORD) = imm ? (BOOL (WINAPI *)(DWORD))GetProcAddress(imm, "ImmDisableIME") : NULL;
        if (disable_ime) disable_ime((DWORD)-1);
    }
    if (!U(RegisterClassExA)) return;
    WNDCLASSEXA wc;
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc; wc.lpfnWndProc = zo_proc; wc.hInstance = zo_hinst();
    snprintf(g_cls1, sizeof g_cls1, "win32ktZO_%lu", GetCurrentProcessId());
    wc.lpszClassName = g_cls1;
    g_atom1 = U(RegisterClassExA)(&wc);
    snprintf(g_cls2, sizeof g_cls2, "win32ktZO2_%lu", GetCurrentProcessId());
    wc.lpszClassName = g_cls2;
    g_atom2 = U(RegisterClassExA)(&wc);
}
const char *zo_class(void) { zo_register(); return g_cls1; }
const char *zo_class2(void) { zo_register(); return g_cls2; }
ATOM zo_atom(void) { zo_register(); return g_atom1; }

void zo_unique_name(char *out, size_t cap, const char *stem) {
    static volatile LONG ctr;
    snprintf(out, cap, "win32kt_%s_%lu_%ld", stem, GetCurrentProcessId(), (long)InterlockedIncrement(&ctr));
}

HWND zo_desktop(void) { return U(GetDesktopWindow) ? U(GetDesktopWindow)() : NULL; }

static void reg_add(HWND h) {
    LONG i = InterlockedIncrement(&g_nreg) - 1;
    if (i < 1024) { g_reg[i] = h; g_regtid[i] = GetCurrentThreadId(); }
}

int zo_have_gui(void) {
    if (!U(CreateWindowExA) || !U(DestroyWindow) || !U(SetWindowPos) || !U(GetWindow) || !U(IsWindow)) return 0;
    if (!zo_atom()) return 0;
    HWND h = U(CreateWindowExA)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, g_cls1, "probe", WS_POPUP, ZO_OFFX, ZO_OFFY, 10, 10, NULL, NULL, zo_hinst(), NULL);
    if (!h) return 0;
    U(DestroyWindow)(h);
    return 1;
}

void zo_reset(void) {
    DWORD me = GetCurrentThreadId();
    LONG n = g_nreg; if (n > 1024) n = 1024;
    if (U(IsWindow) && U(DestroyWindow)) {
        for (LONG i = n - 1; i >= 0; i--)
            if (g_reg[i] && g_regtid[i] == me) {
                if (U(IsWindow)(g_reg[i])) U(DestroyWindow)(g_reg[i]);
                g_reg[i] = NULL;
            }
    }
    zo_beh = 0; zo_log_on = 0; zo_log_n = 0; zo_ghost = 1;
    zo_beh_cx = zo_beh_cy = zo_beh_dx = zo_beh_dy = 0;
    memset(&zo_mm, 0, sizeof zo_mm);
    SetLastError(0);
}

HWND zo_top_ex(const char *title, int x, int y, int w, int h, DWORD style, DWORD ex, HWND owner, int show, const char *cls) {
    zo_register();
    if (!style) style = WS_POPUP;
    int layer = zo_ghost && owner != HWND_MESSAGE && !(ex & WS_EX_LAYERED);
    HWND hw = U(CreateWindowExA)(ex | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | (layer ? WS_EX_LAYERED : 0), cls ? cls : g_cls1, title ? title : "zo",
                                 style & ~WS_VISIBLE, x, y, w, h, owner, NULL, zo_hinst(), NULL);
    if (hw && (layer || (ex & WS_EX_LAYERED)) && U(SetLayeredWindowAttributes)) U(SetLayeredWindowAttributes)(hw, 0, 1, LWA_ALPHA);
    if (hw) { reg_add(hw); if (show) zo_show(hw); }
    return hw;
}
HWND zo_top(const char *title, int x, int y, int w, int h, int show) {
    return zo_top_ex(title, x, y, w, h, WS_POPUP, 0, NULL, show, NULL);
}
HWND zo_child(HWND parent, const char *title, int x, int y, int w, int h, DWORD style, DWORD ex) {
    zo_register();
    HWND hw = U(CreateWindowExA)(ex, g_cls1, title ? title : "zoc", style | WS_CHILD, x, y, w, h, parent, NULL, zo_hinst(), NULL);
    if (hw) reg_add(hw);
    return hw;
}
void zo_show(HWND h) { U(SetWindowPos)(h, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW); }
void zo_hide(HWND h) { U(SetWindowPos)(h, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_HIDEWINDOW); }

void zo_arrange(HWND *order, int n) {
    for (int i = n - 1; i >= 0; i--)
        U(SetWindowPos)(order[i], HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

/* Windows 10 keeps the shell's desktop window below everything that HWND_BOTTOM can reach. Learn what that floor is by
 * sending a probe window to the bottom once; a window is "at the bottom" when exactly the floor windows lie below it. */
int zo_at_bottom(HWND w) {
    static HWND floor_[16]; static int nfloor = -1;
    if (nfloor < 0) {
        nfloor = 0;
        HWND pr = zo_top("floor", ZO_OFFX, ZO_OFFY, 10, 10, 1);
        if (pr) {
            U(SetWindowPos)(pr, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            for (HWND c = U(GetWindow)(pr, GW_HWNDNEXT); c && nfloor < 16; c = U(GetWindow)(c, GW_HWNDNEXT)) floor_[nfloor++] = c;
            U(DestroyWindow)(pr);
        }
    }
    HWND c = U(GetWindow)(w, GW_HWNDNEXT);
    for (int i = 0; i < nfloor; i++, c = U(GetWindow)(c, GW_HWNDNEXT)) if (c != floor_[i]) return 0;
    return c == NULL;
}

int zo_idx(const HWND *a, int n, HWND h) { for (int i = 0; i < n; i++) if (a[i] == h) return i; return -1; }
int zo_same(const HWND *a, const HWND *b, int n) { for (int i = 0; i < n; i++) if (a[i] != b[i]) return 0; return 1; }
void zo_dump(const char *tag, const HWND *a, int n) {
    char buf[512]; int o = snprintf(buf, sizeof buf, "%s:", tag);
    for (int i = 0; i < n && o < 480; i++) o += snprintf(buf + o, sizeof buf - o, " %p", (void *)a[i]);
    t_note("%s", buf);
}

int zo_rank(const HWND *in, int n, HWND *out) {
    if (n <= 0) return 0;
    HWND cur = U(GetWindow)(in[0], GW_HWNDFIRST);
    int got = 0, guard = 0;
    while (cur && guard++ < 20000) {
        if (zo_idx(in, n, cur) >= 0) { if (got < n) out[got++] = cur; }
        if (got == n) break;
        cur = U(GetWindow)(cur, GW_HWNDNEXT);
    }
    return got == n ? n : -1;
}

/* ---- desktop heap ---- */
int zo_rd(const unsigned char *p, size_t off, void *out, size_t n) { return p && k_try_read(p + off, out, n); }

const unsigned char *zo_wnd(HWND h) {
    const unsigned char *p = (const unsigned char *)k_wnd_ptr(h);
    ULONG_PTR v = 0;
    if (p && zo_rd(p, 0, &v, 8) && v == (ULONG_PTR)h) return p;
    if (t_is_wine()) return NULL;
    if (!UN(NtUserCallOneParam)) return NULL;
    p = (const unsigned char *)UN(NtUserCallOneParam)((ULONG_PTR)h, 0x38);
    if (p && zo_rd(p, 0, &v, 8) && v == (ULONG_PTR)h) return p;
    return NULL;
}
int zo_heap_ok(void) {
    HWND h = zo_top("heapok", ZO_OFFX, ZO_OFFY, 10, 10, 0);
    int ok = h && zo_wnd(h) != NULL;
    if (h) U(DestroyWindow)(h);
    return ok;
}
unsigned long zo_style_heap(HWND h, int *ok) {
    const unsigned char *p = zo_wnd(h); DWORD v = 0;
    int r = p && zo_rd(p, ZO_WND_Style, &v, 4);
    if (ok) *ok = r;
    return v;
}
unsigned long zo_exstyle_heap(HWND h, int *ok) {
    const unsigned char *p = zo_wnd(h); DWORD v = 0;
    int r = p && zo_rd(p, ZO_WND_ExStyle, &v, 4);
    if (ok) *ok = r;
    return v;
}
static const unsigned char *zo_follow(const unsigned char *p, size_t off, int *valid) {
    ULONG_PTR k = 0, self = 0;
    *valid = 0;
    if (!p || !zo_rd(p, off, &k, 8) || !zo_rd(p, ZO_WND_Self, &self, 8)) return NULL;
    *valid = 1;
    if (!k) return NULL;
    return p + (k - self);
}
HWND zo_link(HWND h, size_t off) {
    int v; const unsigned char *q = zo_follow(zo_wnd(h), off, &v);
    ULONG_PTR hv = 0;
    if (!q || !zo_rd(q, 0, &hv, 8)) return NULL;
    return (HWND)hv;
}
int zo_link_valid(HWND h, size_t off) { int v; zo_follow(zo_wnd(h), off, &v); return v; }
int zo_rect_heap(HWND h, size_t off, RECT *r) {
    const unsigned char *p = zo_wnd(h);
    return p && zo_rd(p, off, r, sizeof *r);
}

int zo_rank_heap(HWND parent, const HWND *in, int n, HWND *out) {
    if (n <= 0) return 0;
    if (!zo_wnd(parent)) return -1;
    HWND cur = zo_link(parent, ZO_WND_Child);
    int got = 0, guard = 0;
    while (cur && guard++ < 20000) {
        if (zo_idx(in, n, cur) >= 0 && got < n) out[got++] = cur;
        if (got == n) break;
        cur = zo_link(cur, ZO_WND_Next);
    }
    return got == n ? n : -1;
}

int zo_same_rect(const RECT *a, const RECT *b) { return a->left == b->left && a->top == b->top && a->right == b->right && a->bottom == b->bottom; }
void zo_note_rect(const char *what, const RECT *r) { t_note("%s = (%ld,%ld)-(%ld,%ld) %ldx%ld", what, r->left, r->top, r->right, r->bottom, r->right - r->left, r->bottom - r->top); }
int zo_is_child_of(HWND child, HWND parent) { return U(IsChild)(parent, child); }
void zo_pump(unsigned ms) { k_pump(ms); }

/* ---- GUI thread ---- */
struct zo_call { zo_fn fn; void *arg; HANDLE done; ULONG_PTR ret; };
static DWORD WINAPI zo_thread_main(LPVOID p) {
    struct zo_thread *t = (struct zo_thread *)p;
    MSG m;
    U(PeekMessageA)(&m, NULL, 0, 0, PM_NOREMOVE);   /* create the queue / GUI thread state */
    SetEvent(t->ready);
    while (U(GetMessageA)(&m, NULL, 0, 0) > 0) {
        if (m.hwnd == NULL && m.message == ZO_TM_CALL) {
            struct zo_call *c = (struct zo_call *)m.lParam;
            c->ret = c->fn(c->arg);
            SetEvent(c->done);
            continue;
        }
        U(TranslateMessage)(&m);
        U(DispatchMessageA)(&m);
    }
    zo_reset();
    return 0;
}
struct zo_thread *zo_thread_start(void) {
    if (!U(GetMessageA) || !U(PeekMessageA) || !U(DispatchMessageA) || !U(TranslateMessage) || !U(PostThreadMessageA)) return NULL;
    zo_register();
    struct zo_thread *t = (struct zo_thread *)calloc(1, sizeof *t);
    t->ready = CreateEventA(NULL, TRUE, FALSE, NULL);
    t->th = CreateThread(NULL, 0, zo_thread_main, t, 0, &t->tid);
    if (!t->th || WaitForSingleObject(t->ready, 5000) != WAIT_OBJECT_0) { return NULL; }
    return t;
}
ULONG_PTR zo_thread_call(struct zo_thread *t, zo_fn fn, void *arg) {
    struct zo_call *c = (struct zo_call *)calloc(1, sizeof *c);
    c->fn = fn; c->arg = arg; c->done = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!U(PostThreadMessageA)(t->tid, ZO_TM_CALL, 0, (LPARAM)c)) { CloseHandle(c->done); free(c); return (ULONG_PTR)-1; }
    DWORD start = GetTickCount();
    ULONG_PTR r = (ULONG_PTR)-1;
    for (;;) {
        DWORD el = GetTickCount() - start;
        if (el >= 8000) break;
        DWORD w = MsgWaitForMultipleObjects(1, &c->done, FALSE, 8000 - el, QS_ALLINPUT);
        if (w == WAIT_OBJECT_0) { r = c->ret; CloseHandle(c->done); free(c); return r; }
        if (w == WAIT_OBJECT_0 + 1) { MSG m; while (U(PeekMessageA)(&m, NULL, 0, 0, PM_REMOVE)) { U(TranslateMessage)(&m); U(DispatchMessageA)(&m); } }
        else break;
    }
    return r;   /* c leaked on timeout on purpose: the thread may still write it */
}
void zo_thread_stop(struct zo_thread *t) {
    if (!t) return;
    U(PostThreadMessageA)(t->tid, WM_QUIT, 0, 0);
    if (WaitForSingleObject(t->th, 5000) != WAIT_OBJECT_0) TerminateThread(t->th, 0);
    CloseHandle(t->th); CloseHandle(t->ready);
    free(t);
}
struct zo_mkargs { const char *title; int x, y, w, h, show; };
static ULONG_PTR zo_mk_fn(void *a) { struct zo_mkargs *m = (struct zo_mkargs *)a; return (ULONG_PTR)zo_top(m->title, m->x, m->y, m->w, m->h, m->show); }
HWND zo_thread_window(struct zo_thread *t, const char *title, int x, int y, int w, int h, int show) {
    struct zo_mkargs a = { title, x, y, w, h, show };
    ULONG_PTR r = zo_thread_call(t, zo_mk_fn, &a);
    return r == (ULONG_PTR)-1 ? NULL : (HWND)r;
}
