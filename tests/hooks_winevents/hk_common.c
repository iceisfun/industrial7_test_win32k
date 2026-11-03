/* hooks_winevents: shared recorder, window, worker and peer-process helpers. */
#include "hk.h"
#include <limits.h>

/* Attribute table at win32kfull 0x2e9608, one byte per hook id (-1..14). Bit 0x02 = may be set per thread, bits
 * 0x24 = journal/low-level (no module needed globally). Those with 0x02 clear are global-only: ids 0,1,6,13,14. */
const int hk_thread_ids[11] = { -1, 2, 3, 4, 5, 7, 8, 9, 10, 11, 12 };
const int hk_global_only_ids[5] = { 0, 1, 6, 13, 14 };

/* ---- _CLIENTINFO readers ---- */
ULONG_PTR hk_ci_phkcurrent(void) { void *v = NULL; k_try_read_ptr(HK_CI(0x30), &v); return (ULONG_PTR)v; }
unsigned long hk_ci_fshooks(void) { unsigned long v = 0; k_try_read_u32(HK_CI(0x38), &v); return v; }
unsigned long hk_ci_hookcur(void) { unsigned long v = 0; k_try_read_u32(HK_CI(0x58), &v); return v; }
unsigned long hk_teb_fshooks(void *teb) { unsigned long v = 0; k_try_read_u32((char *)teb + TEB_Win32ClientInfo + 0x38, &v); return v; }
unsigned long hk_deskinfo_fshooks(unsigned long *ok) {
    void *di = NULL; unsigned long v = 0;
    if (ok) *ok = 0;
    if (!k_try_read_ptr(HK_CI(0x20), &di) || !di) return 0;
    if (!k_try_read_u32((char *)di + 0x10, &v)) return 0;
    if (ok) *ok = 1;
    return v;
}

/* ---- sequence log ---- */
static struct { int tag; UINT msg; LRESULT v; } g_lg[128];
static volatile LONG g_lgn;
static UINT g_ring[512];
static volatile LONG g_ringn;
volatile LONG hk_seq;
hk_slot hk_s[HK_NSLOT];

void hk_log_add(int tag, UINT msg, LRESULT v) {
    LONG i = InterlockedIncrement(&g_lgn) - 1;
    if (i < 128) { g_lg[i].tag = tag; g_lg[i].msg = msg; g_lg[i].v = v; }
}
int hk_log_n(void) { LONG n = g_lgn; return n > 128 ? 128 : (int)n; }
int hk_log_tag(int i) { return g_lg[i].tag; }
UINT hk_log_msg(int i) { return g_lg[i].msg; }
int hk_log_find(int tag, UINT msg) {
    for (int i = 0; i < hk_log_n(); i++) if (g_lg[i].tag == tag && g_lg[i].msg == msg) return i;
    return -1;
}
int hk_msg_seen(UINT msg) {
    int c = 0; LONG n = g_ringn; if (n > 512) n = 512;
    for (int i = 0; i < n; i++) if (g_ring[i] == msg) c++;
    return c;
}
static void ring_add(UINT m) { LONG i = InterlockedIncrement(&g_ringn) - 1; if (i < 512) g_ring[i] = m; }

/* The first window a thread creates also creates that thread's default IME window, which issues a second creation
 * (CBT, CREATESTRUCT callback, EVENT_OBJECT_CREATE) the tests would otherwise have to filter out. Keeping one window
 * alive per thread, made before the recorder is armed, leaves every later creation a single event. */
static __thread HWND g_warm;
void hk_reset(void) {
    hk_remove_all();
    if (!g_warm || !U(IsWindow)(g_warm)) g_warm = hk_window("warm");
    memset(hk_s, 0, sizeof hk_s);
    for (int i = 0; i < HK_NSLOT; i++) hk_s[i].only_code = INT_MIN;
    g_lgn = 0; g_ringn = 0; hk_seq = 0;
    memset(g_lg, 0, sizeof g_lg);
}

/* ---- the generic hook procedure: every slot runs this ---- */
static LRESULT hk_handle(int slot, int code, WPARAM wp, LPARAM lp) {
    hk_slot *s = &hk_s[slot];
    if (code < 0) return U(CallNextHookEx)(s->hh, code, wp, lp);
    InterlockedIncrement(&s->total);
    UINT msg = 0;
    if (s->kind == HK_K_MSG && lp) msg = ((MSG *)lp)->message;
    else if (s->kind == HK_K_CWP && lp) msg = ((CWPSTRUCT *)lp)->message;
    else if (s->kind == HK_K_CWPRET && lp) msg = ((CWPRETSTRUCT *)lp)->message;
    int match = 1;
    if (s->only_msg && msg != s->only_msg) match = 0;
    if (s->only_code != INT_MIN && code != s->only_code) match = 0;
    if (!match) return U(CallNextHookEx)(s->hh, code, wp, lp);
    s->code = code; s->wp = wp; s->lp = lp; s->tid = GetCurrentThreadId();
    int ord = InterlockedIncrement(&hk_seq);
    if (!s->first_order) s->first_order = ord;
    s->last_order = ord;
    s->ci_cur = hk_ci_phkcurrent(); s->ci_hookcur = hk_ci_hookcur(); s->ci_fshooks = hk_ci_fshooks();
    if (lp && s->kind == HK_K_MSG) k_try_read((void *)lp, &s->msg, sizeof s->msg);
    if (lp && s->kind == HK_K_CWP) {
        k_try_read((void *)lp, &s->cwp, sizeof s->cwp);
        if (s->cwp.message == WM_SETTEXT && s->cwp.lParam) k_try_read((void *)s->cwp.lParam, s->raw, 16);
        hk_log_add('C', s->cwp.message, 0);
    }
    if (lp && s->kind == HK_K_CWPRET) { k_try_read((void *)lp, &s->cwpr, sizeof s->cwpr); hk_log_add('R', s->cwpr.message, s->cwpr.lResult); }
    if (s->extra) s->extra(s, code, wp, lp);
    LRESULT r;
    if (s->callnext) { r = U(CallNextHookEx)(s->hh, code, wp, lp); s->next_result = r; r += s->add; }
    else r = s->ret;
    InterlockedIncrement(&s->calls);
    return r;
}
#define HKP(n) static LRESULT CALLBACK hk_proc##n(int c, WPARAM w, LPARAM l) { return hk_handle(n, c, w, l); }
HKP(0) HKP(1) HKP(2) HKP(3)
static HOOKPROC const g_procs[HK_NSLOT] = { hk_proc0, hk_proc1, hk_proc2, hk_proc3 };
LRESULT CALLBACK hk_nop_proc(int code, WPARAM wp, LPARAM lp) { return U(CallNextHookEx)(NULL, code, wp, lp); }

HHOOK hk_install_on(int slot, int id, int kind, BOOL ansi, DWORD tid) {
    hk_slot *s = &hk_s[slot];
    s->id = id; s->kind = kind;
    s->hh = ansi ? U(SetWindowsHookExA)(id, g_procs[slot], NULL, tid) : U(SetWindowsHookExW)(id, g_procs[slot], NULL, tid);
    return s->hh;
}
HHOOK hk_install(int slot, int id, int kind, BOOL ansi) { return hk_install_on(slot, id, kind, ansi, GetCurrentThreadId()); }
void hk_remove(int slot) {
    if (hk_s[slot].hh) { U(UnhookWindowsHookEx)(hk_s[slot].hh); hk_s[slot].hh = NULL; }
}
void hk_remove_all(void) { for (int i = 0; i < HK_NSLOT; i++) hk_remove(i); }

/* ---- windows ---- */
static LRESULT CALLBACK hk_wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    ring_add(m);
    if (m >= WM_APP && m < WM_APP + 0x100) { LRESULT r = (LRESULT)(w + l); hk_log_add('W', m, r); return r; }
    return U(DefWindowProcW)(h, m, w, l);
}
const WCHAR *hk_class(void) {
    static WCHAR name[48]; static int reg;
    if (!reg) {
        _snwprintf(name, 48, L"win32ktHk_%lu", GetCurrentProcessId());
        WNDCLASSW wc; memset(&wc, 0, sizeof wc);
        wc.lpfnWndProc = hk_wndproc; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = name;
        if (U(RegisterClassW)(&wc)) reg = 1;
    }
    return name;
}
HWND hk_window_ex(const char *tag, DWORD style, int x, int y, int cx, int cy) {
    WCHAR title[64]; _snwprintf(title, 64, L"hk_%S", tag ? tag : "w");
    return U(CreateWindowExW)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, hk_class(), title, style, x, y, cx, cy, NULL, NULL, GetModuleHandleW(NULL), NULL);
}
HWND hk_window(const char *tag) { return hk_window_ex(tag, WS_POPUP, -32000, -32000, 1, 1); }

int hk_user_obj_valid(HANDLE h, int type) {
    struct k_user_entry e;
    if (!k_user_entry(h, &e)) return 0;
    return e.type == type && !(e.flags & 1) && e.uniq == (unsigned short)((ULONG_PTR)h >> 16);
}
int hk_user_obj_dead(HANDLE h) {
    struct k_user_entry e;
    if (!k_user_entry(h, &e)) return 0;
    return e.type == 0 || (e.flags & 1);
}
unsigned long hk_gui_objects(void) { return U(GetGuiResources)(GetCurrentProcess(), GR_USEROBJECTS); }

/* ---- worker thread ---- */
static void hk_pump_once(void) {
    MSG m;
    while (U(PeekMessageW)(&m, NULL, 0, 0, PM_REMOVE)) { U(TranslateMessage)(&m); U(DispatchMessageW)(&m); }
}
static DWORD WINAPI hk_worker_main(LPVOID p) {
    hk_worker *w = (hk_worker *)p;
    w->tid = GetCurrentThreadId();
    w->hwnd = hk_window("worker");
    if (w->init) w->init(w);
    SetEvent(w->ready);
    for (int i = 0; i < 1000; i++) {
        hk_pump_once();
        if (WaitForSingleObject(w->stopev, 0) == WAIT_OBJECT_0) break;
        U(MsgWaitForMultipleObjects)(1, &w->stopev, FALSE, 10, QS_ALLINPUT);
    }
    if (w->hwnd) U(DestroyWindow)(w->hwnd);
    return 0;
}
int hk_worker_start(hk_worker *w, void (*init)(hk_worker *), void *arg) {
    memset(w, 0, sizeof *w);
    w->init = init; w->arg = arg;
    w->ready = CreateEventA(NULL, TRUE, FALSE, NULL);
    w->stopev = CreateEventA(NULL, TRUE, FALSE, NULL);
    w->th = CreateThread(NULL, 0, hk_worker_main, w, 0, NULL);
    if (!w->th || WaitForSingleObject(w->ready, 5000) != WAIT_OBJECT_0 || !w->hwnd) { hk_worker_stop(w); return 0; }
    return 1;
}
void hk_worker_stop(hk_worker *w) {
    if (w->stopev) SetEvent(w->stopev);
    if (w->th) { WaitForSingleObject(w->th, 8000); CloseHandle(w->th); }
    if (w->ready) CloseHandle(w->ready);
    if (w->stopev) CloseHandle(w->stopev);
    memset(w, 0, sizeof *w);
}

/* ---- peer process ---- */
CHILD(hooks_winevents_peer) {
    if (argc < 2) return 2;
    HANDLE ev = OpenEventA(SYNCHRONIZE, FALSE, argv[1]);
    if (!ev) return 3;
    HWND w = hk_window("peer");
    if (!w) { printf("FAIL peer window not created\n"); return 4; }
    printf("READY %lu %lu %llx\n", GetCurrentProcessId(), GetCurrentThreadId(), (unsigned long long)(ULONG_PTR)w);
    for (int i = 0; i < 2000; i++) {
        hk_pump_once();
        if (WaitForSingleObject(ev, 0) == WAIT_OBJECT_0) break;
        U(MsgWaitForMultipleObjects)(1, &ev, FALSE, 10, QS_ALLINPUT);
    }
    return 0;
}
int hk_peer_start(hk_peer *p) {
    memset(p, 0, sizeof *p);
    char evname[96];
    snprintf(evname, sizeof evname, "win32kt_hkpeer_%lu_%lu", GetCurrentProcessId(), GetTickCount());
    p->ev = CreateEventA(NULL, TRUE, FALSE, evname);
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    HANDLE rd = NULL, wr = NULL;
    if (!p->ev || !CreatePipe(&rd, &wr, &sa, 0)) return 0;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    PROCESS_INFORMATION pi;
    p->proc = t_spawn_child("hooks_winevents_peer", evname, 0, NULL, wr, wr, TRUE, NULL, &pi);
    CloseHandle(wr);
    if (!p->proc) { CloseHandle(rd); return 0; }
    CloseHandle(pi.hThread);
    p->rd = rd;
    char buf[512]; DWORD n = 0;
    memset(buf, 0, sizeof buf);
    for (int i = 0; i < 400; i++) {
        DWORD avail = 0, got = 0;
        if (!PeekNamedPipe(rd, NULL, 0, NULL, &avail, NULL)) break;
        if (avail) {
            if (avail > sizeof buf - 1 - n) avail = (DWORD)(sizeof buf - 1 - n);
            if (!avail || !ReadFile(rd, buf + n, avail, &got, NULL)) break;
            n += got; buf[n] = 0;
            char *r = strstr(buf, "READY");
            if (r && strchr(r, '\n')) break;
        } else {
            if (WaitForSingleObject(p->proc, 0) == WAIT_OBJECT_0) break;
            Sleep(25);
        }
    }
    char *r = strstr(buf, "READY");
    unsigned long long hw = 0; unsigned long pid = 0, tid = 0;
    if (!r || sscanf(r, "READY %lu %lu %llx", &pid, &tid, &hw) != 3) { hk_peer_stop(p); return 0; }
    p->pid = pid; p->tid = tid; p->hwnd = (HWND)(ULONG_PTR)hw;
    return 1;
}
void hk_peer_stop(hk_peer *p) {
    if (p->ev) SetEvent(p->ev);
    if (p->proc) {
        if (WaitForSingleObject(p->proc, 5000) != WAIT_OBJECT_0) { TerminateProcess(p->proc, 1); WaitForSingleObject(p->proc, 2000); }
        CloseHandle(p->proc);
    }
    if (p->rd) CloseHandle(p->rd);
    if (p->ev) CloseHandle(p->ev);
    memset(p, 0, sizeof *p);
}

/* ---- WinEvent recorder ---- */
struct we_rec we_r[WE_MAX];
volatile LONG we_n;
void CALLBACK we_proc(HWINEVENTHOOK h, DWORD ev, HWND hwnd, LONG obj, LONG child, DWORD tid, DWORD time) {
    LONG i = InterlockedIncrement(&we_n) - 1;
    if (i >= WE_MAX) return;
    we_r[i].h = h; we_r[i].ev = ev; we_r[i].hwnd = hwnd; we_r[i].obj = obj; we_r[i].child = child;
    we_r[i].tid = tid; we_r[i].time = time; we_r[i].cbtid = GetCurrentThreadId(); we_r[i].order = (int)i;
}
void we_reset(void) { we_n = 0; memset(we_r, 0, sizeof we_r); }
int we_count_event(DWORD ev) {
    int c = 0; LONG n = we_n; if (n > WE_MAX) n = WE_MAX;
    for (int i = 0; i < n; i++) if (we_r[i].ev == ev) c++;
    return c;
}
struct we_rec *we_find(DWORD ev, int nth) {
    LONG n = we_n; if (n > WE_MAX) n = WE_MAX;
    for (int i = 0; i < n; i++) if (we_r[i].ev == ev && nth-- == 0) return &we_r[i];
    return NULL;
}
