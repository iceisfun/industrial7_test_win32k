#include "x.h"

int xt_cf;

void xt_init(void) {
    t_module("user32.dll"); t_module("gdi32.dll");
}

static LRESULT CALLBACK xt_wp(HWND h, UINT m, WPARAM w, LPARAM l) { return U(DefWindowProcW)(h, m, w, l); }
static const WCHAR *xt_class(void) {
    static WCHAR cls[64];
    if (!cls[0]) {
        WNDCLASSEXW wc; memset(&wc, 0, sizeof wc);
        _snwprintf(cls, 64, L"win32kt_xt_%lu", GetCurrentProcessId());
        wc.cbSize = sizeof wc; wc.lpfnWndProc = xt_wp; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = cls;
        if (!U(RegisterClassExW)(&wc)) cls[0] = 0;
    }
    return cls;
}
HWND xt_window(void) {
    const WCHAR *c = xt_class();
    if (!c[0]) return NULL;
    HWND h = U(CreateWindowExW)(WS_EX_TOOLWINDOW, c, L"win32kt_xt", WS_POPUP, -32000, -32000, 8, 8, NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (h) U(ShowWindow)(h, SW_SHOWNOACTIVATE);
    return h;
}
HWND xt_child_window(HWND parent) {
    const WCHAR *c = xt_class();
    if (!c[0]) return NULL;
    return U(CreateWindowExW)(0, c, L"win32kt_xt_c", WS_CHILD | WS_VISIBLE, 0, 0, 4, 4, parent, NULL, GetModuleHandleW(NULL), NULL);
}
void xt_destroy(HWND h) { if (h && U(IsWindow)(h)) U(DestroyWindow)(h); }
void xt_drain(void) {
    MSG m;
    while (U(PeekMessageW)(&m, NULL, 0, 0, PM_REMOVE)) { U(TranslateMessage)(&m); U(DispatchMessageW)(&m); }
    U(GetQueueStatus)(0x5DFF);
}
int xt_peek_remove(UINT lo, UINT hi, MSG *out) {
    MSG m;
    int r = U(PeekMessageW)(out ? out : &m, NULL, lo, hi, PM_REMOVE) ? 1 : 0;
    return r;
}

/* ---- worker threads ---- */
static DWORD WINAPI wk_main(LPVOID p) {
    struct xt_wk *w = p;
    t_module("user32.dll");
    for (int i = 0; i < w->nw; i++) w->w[i] = xt_window();
    w->tid = GetCurrentThreadId();
    SetEvent(w->evready);
    for (;;) {
        WaitForSingleObject(w->evcmd, INFINITE);
        if (w->quit) break;
        w->job(w, w->arg);
        SetEvent(w->evdone);
    }
    return 0;
}
int xt_wk_start(struct xt_wk *w, int nwin) {
    memset(w, 0, sizeof *w);
    w->nw = nwin;
    w->evcmd = CreateEventA(NULL, FALSE, FALSE, NULL); w->evdone = CreateEventA(NULL, TRUE, FALSE, NULL);
    w->evready = CreateEventA(NULL, TRUE, FALSE, NULL); w->sig = CreateEventA(NULL, FALSE, FALSE, NULL);
    w->th = CreateThread(NULL, 0, wk_main, w, 0, NULL);
    if (!w->th || WaitForSingleObject(w->evready, 15000) != WAIT_OBJECT_0) { xt_wk_stop(w); return 0; }
    for (int i = 0; i < nwin; i++) if (!w->w[i]) { xt_wk_stop(w); return 0; }
    return 1;
}
void xt_wk_go(struct xt_wk *w, xt_job job, void *arg) {
    w->job = job; w->arg = arg;
    ResetEvent(w->evdone);
    SetEvent(w->evcmd);
}
int xt_wk_wait(struct xt_wk *w, DWORD ms) {
    DWORD t0 = GetTickCount();
    for (;;) {
        DWORD el = GetTickCount() - t0;
        if (el >= ms) return WaitForSingleObject(w->evdone, 0) == WAIT_OBJECT_0;
        DWORD r = U(MsgWaitForMultipleObjectsEx)(1, &w->evdone, ms - el, QS_ALLINPUT, 0);
        if (r == WAIT_OBJECT_0) return 1;
        if (r == WAIT_OBJECT_0 + 1) xt_drain();
        else if (r == WAIT_TIMEOUT) return 0;
        else return 0;
    }
}
int xt_wk_run(struct xt_wk *w, xt_job job, void *arg) { xt_wk_go(w, job, arg); return xt_wk_wait(w, 20000); }
int xt_wk_sig(struct xt_wk *w, DWORD ms) { return WaitForSingleObject(w->sig, ms) == WAIT_OBJECT_0; }
void xt_wk_stop(struct xt_wk *w) {
    if (w->th) {
        w->quit = 1; SetEvent(w->evcmd);
        if (WaitForSingleObject(w->th, 15000) != WAIT_OBJECT_0) TerminateThread(w->th, 1);
        CloseHandle(w->th);
    }
    if (w->evcmd) CloseHandle(w->evcmd);
    if (w->evdone) CloseHandle(w->evdone);
    if (w->evready) CloseHandle(w->evready);
    if (w->sig) CloseHandle(w->sig);
    memset(w, 0, sizeof *w);
}
struct once { void (*fn)(void *); void *arg; };
static DWORD WINAPI once_main(LPVOID p) { struct once *o = p; t_module("user32.dll"); o->fn(o->arg); return 0; }
int xt_on_thread(void (*fn)(void *), void *arg) {
    struct once o = { fn, arg };
    HANDLE h = CreateThread(NULL, 0, once_main, &o, 0, NULL);
    if (!h) return 0;
    int ok = WaitForSingleObject(h, 30000) == WAIT_OBJECT_0;
    if (!ok) TerminateThread(h, 1);
    CloseHandle(h);
    return ok;
}

/* ---- child processes ---- */
struct xt_shm *xt_shm_create(const char *tag, HANDLE *h) {
    *h = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 4096, tag);
    return *h ? MapViewOfFile(*h, FILE_MAP_ALL_ACCESS, 0, 0, 4096) : NULL;
}
struct xt_shm *xt_shm_open(const char *tag, HANDLE *h) {
    *h = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, tag);
    return *h ? MapViewOfFile(*h, FILE_MAP_ALL_ACCESS, 0, 0, 4096) : NULL;
}
int xt_wait(volatile LONG *flag, DWORD ms) {
    DWORD t0 = GetTickCount();
    while (!*flag) { if (GetTickCount() - t0 > ms) return 0; Sleep(2); }
    return 1;
}
const char *xt_tag(void) {
    static char b[8][64]; static int n;
    char *s = b[n++ & 7];
    snprintf(s, 64, "win32kt_xt_%lu_%d", GetCurrentProcessId(), n);
    return s;
}
HANDLE xt_spawn(const char *mode, const char *tag, const char *extra, PROCESS_INFORMATION *pi) {
    char a[256]; snprintf(a, sizeof a, "%s %s %s", mode, tag, extra ? extra : "");
    return t_spawn_child("xt_child", a, 0, NULL, NULL, NULL, FALSE, NULL, pi);
}
DWORD xt_reap(HANDLE p, PROCESS_INFORMATION *pi, DWORD ms) {
    DWORD ec = 0xDEAD0001;
    if (WaitForSingleObject(p, ms) == WAIT_OBJECT_0) GetExitCodeProcess(p, &ec);
    else { TerminateProcess(p, 1); WaitForSingleObject(p, 5000); }
    CloseHandle(p); if (pi && pi->hThread) CloseHandle(pi->hThread);
    return ec;
}
DWORD xt_child(const char *mode, const char *extra) {
    char a[256]; snprintf(a, sizeof a, "%s %s %s", mode, "-", extra ? extra : "");
    struct k_child_result r = k_child_run("xt_child", a, 60000);
    return r.exit_code;
}

static struct { const char *n; xt_cmode f; } modes[64]; static int nmodes;
void xt_register_mode(const char *name, xt_cmode fn) { if (nmodes < 64) { modes[nmodes].n = name; modes[nmodes].f = fn; nmodes++; } }
CHILD(xt_child) {
    if (argc < 3) return 90;
    t_module("user32.dll"); t_module("gdi32.dll");
    HANDLE hm = NULL; struct xt_shm *s = NULL;
    if (strcmp(argv[2], "-")) { s = xt_shm_open(argv[2], &hm); if (!s) return 91; }
    for (int i = 0; i < nmodes; i++) if (!strcmp(modes[i].n, argv[1])) {
        int r = modes[i].f(s, argc - 3, argv + 3);
        return xt_cf ? xt_cf : r;
    }
    return 92;
}

void job_drain(struct xt_wk *w, void *p) { xt_drain(); }
