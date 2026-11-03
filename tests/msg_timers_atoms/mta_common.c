#include "mta.h"
#include <tlhelp32.h>

struct mta_ev mta_log[MTA_LOG_MAX];
volatile LONG mta_nlog;
struct mta_pev mta_plog[MTA_LOG_MAX];
volatile LONG mta_nplog;

void mta_reset(void) { mta_nlog = 0; mta_nplog = 0; }
int mta_count(UINT msg, WPARAM wp) {
    int n = 0, c = mta_nlog; if (c > MTA_LOG_MAX) c = MTA_LOG_MAX;
    for (int i = 0; i < c; i++) if (mta_log[i].msg == msg && (wp == (WPARAM)-1 || mta_log[i].wp == wp)) n++;
    return n;
}
int mta_pcount(int which) {
    int n = 0, c = mta_nplog; if (c > MTA_LOG_MAX) c = MTA_LOG_MAX;
    for (int i = 0; i < c; i++) if (which < 0 || mta_plog[i].which == which) n++;
    return n;
}

static LRESULT CALLBACK mta_wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_TIMER || m == MTA_WM_SYSTIMER || (m >= WM_APP && m < WM_APP + 0x100)) {
        LONG i = InterlockedIncrement(&mta_nlog) - 1;
        if (i < MTA_LOG_MAX) {
            mta_log[i].msg = m; mta_log[i].hwnd = h; mta_log[i].wp = w; mta_log[i].lp = l;
            mta_log[i].tid = GetCurrentThreadId(); mta_log[i].tick = GetTickCount();
        }
        return 0;   /* WM_SYSTIMER is deliberately not forwarded to DefWindowProc */
    }
    return U(DefWindowProcW)(h, m, w, l);
}

static void plog(int which, HWND h, UINT m, UINT_PTR id, DWORD tick) {
    LONG i = InterlockedIncrement(&mta_nplog) - 1;
    if (i < MTA_LOG_MAX) {
        mta_plog[i].which = which; mta_plog[i].hwnd = h; mta_plog[i].msg = m; mta_plog[i].id = id;
        mta_plog[i].tick = tick; mta_plog[i].tid = GetCurrentThreadId();
    }
}
VOID CALLBACK mta_proc_a(HWND h, UINT m, UINT_PTR id, DWORD t) { plog(1, h, m, id, t); }
VOID CALLBACK mta_proc_b(HWND h, UINT m, UINT_PTR id, DWORD t) { plog(2, h, m, id, t + 0); }

int mta_ready(void) {
    static int ready = -1;
    if (ready >= 0) return ready;
    ready = 0;
    if (!U(SetTimer) || !U(KillTimer) || !U(CreateWindowExW) || !U(RegisterClassExW) || !U(PeekMessageW) ||
        !U(DispatchMessageW) || !U(DefWindowProcW) || !U(GetDesktopWindow) || !U(RegisterWindowMessageW)) return 0;
    U(GetDesktopWindow)();      /* first win32k syscall converts this thread to a GUI thread */
    ready = 1;
    return 1;
}

void mta_name(WCHAR *out, size_t cch, const char *tag) {
    WCHAR wtag[96]; int i = 0;
    for (; tag[i] && i < 95; i++) wtag[i] = (WCHAR)(unsigned char)tag[i];
    wtag[i] = 0;
    snwprintf(out, cch, L"win32kt_mta_%lu_%ls", GetCurrentProcessId(), wtag);
}

const WCHAR *mta_cls(void) {
    static WCHAR name[64];
    if (!name[0]) {
        WNDCLASSEXW wc; memset(&wc, 0, sizeof wc);
        mta_name(name, 64, "wnd");
        wc.cbSize = sizeof wc; wc.lpfnWndProc = mta_wndproc;
        wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = name;
        if (!U(RegisterClassExW)(&wc)) name[0] = 0;
    }
    return name[0] ? name : NULL;
}
HWND mta_win(void) {
    const WCHAR *c = mta_cls();
    if (!c) return NULL;
    return U(CreateWindowExW)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, c, L"mta", WS_POPUP, -32000, -32000, 1, 1,
                              NULL, NULL, GetModuleHandleW(NULL), NULL);
}
void mta_destroy(HWND w) { if (w && U(IsWindow)(w)) U(DestroyWindow)(w); }

void mta_pump_ms(unsigned ms) {
    DWORD end = GetTickCount() + ms;
    MSG m;
    do {
        while (U(PeekMessageW)(&m, NULL, 0, 0, PM_REMOVE)) U(DispatchMessageW)(&m);
        Sleep(2);
    } while ((LONG)(end - GetTickCount()) > 0);
}
int mta_pump_until(int kind, UINT msg, int n, unsigned ms) {
    DWORD end = GetTickCount() + ms;
    MSG m; int c = 0;
    for (;;) {
        while (U(PeekMessageW)(&m, NULL, 0, 0, PM_REMOVE)) U(DispatchMessageW)(&m);
        c = kind == 0 ? mta_count(msg, (WPARAM)-1) : mta_pcount(kind);
        if (c >= n || (LONG)(end - GetTickCount()) <= 0) break;
        Sleep(2);
    }
    return c;
}
void mta_drain_timers(void) {
    MSG m; int guard = 0;
    while (guard++ < 10000 && U(PeekMessageW)(&m, NULL, WM_TIMER, WM_TIMER, PM_REMOVE)) {}
    while (guard++ < 20000 && U(PeekMessageW)(&m, NULL, MTA_WM_SYSTIMER, MTA_WM_SYSTIMER, PM_REMOVE)) {}
}

/* ---- USER handle table ------------------------------------------------------------------------ */
static struct k_user_shared_info s_si;
int mta_table_ok(void) {
    if (!mta_ready()) return 0;
    if (!k_shared_info(&s_si) || !s_si.psi || !s_si.aheList) return 0;
    if (s_si.heEntrySize != 0x20) return 0;   /* win32kbase!HMInitHandleTable sets the client stride to 0x20 */
    return mta_entry_count() != 0;
}
unsigned mta_entry_count(void) {
    unsigned long long n = 0;
    if (!s_si.psi && !k_shared_info(&s_si)) return 0;
    if (!k_try_read((char *)s_si.psi + 8, &n, 8) || n > 0xfffe) return 0;
    return (unsigned)n;
}
bool mta_read_entry(unsigned idx, struct mta_hent *e) {
    unsigned char raw[0x20];
    if (!s_si.aheList || idx >= mta_entry_count()) return false;
    if (!k_try_read((char *)s_si.aheList + (size_t)idx * 0x20, raw, sizeof raw)) return false;
    memcpy(&e->delta, raw + 0, 8); memcpy(&e->owner, raw + 8, 8); memcpy(&e->f10, raw + 0x10, 8);
    e->type = raw[0x18]; e->flags = raw[0x19]; memcpy(&e->uniq, raw + 0x1a, 2);
    return true;
}
static int my_ids(DWORD *ids, int max) {
    int n = 0;
    ids[n++] = GetCurrentProcessId();
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (s == INVALID_HANDLE_VALUE) { ids[n++] = GetCurrentThreadId(); return n; }
    THREADENTRY32 te; te.dwSize = sizeof te;
    if (Thread32First(s, &te)) do {
        if (te.th32OwnerProcessID == GetCurrentProcessId() && n < max) ids[n++] = te.th32ThreadID;
    } while (Thread32Next(s, &te));
    CloseHandle(s);
    return n;
}
unsigned mta_timer_indexes(unsigned *idx, unsigned max) {
    unsigned n = mta_entry_count(), cnt = 0;
    DWORD ids[512]; int ni = my_ids(ids, 512);
    unsigned char *buf = n ? malloc((size_t)n * 0x20) : NULL;
    if (!buf) return 0;
    for (unsigned i = 0; i < n; i += 1024) {
        unsigned c = n - i < 1024 ? n - i : 1024;
        if (!k_try_read((char *)s_si.aheList + (size_t)i * 0x20, buf + (size_t)i * 0x20, (size_t)c * 0x20)) { free(buf); return 0; }
    }
    for (unsigned i = 0; i < n; i++) {
        unsigned char *e = buf + (size_t)i * 0x20;
        unsigned long long owner; memcpy(&owner, e + 8, 8);
        if (e[0x18] != MTA_TYPE_TIMER) continue;
        for (int k = 0; k < ni; k++) if (owner == ids[k]) {
            if (idx && cnt < max) idx[cnt] = i;
            cnt++; break;
        }
    }
    free(buf);
    return cnt;
}
unsigned mta_timers(void) { return mta_timer_indexes(NULL, 0); }
DWORD mta_guires(void) { return U(GetGuiResources)(GetCurrentProcess(), GR_USEROBJECTS); }

/* ---- owner thread ----------------------------------------------------------------------------- */
static DWORD WINAPI owner_main(LPVOID p) {
    struct mta_owner *o = p;
    MSG m;
    U(GetDesktopWindow)();                 /* GUI-thread conversion */
    if (o->make_window) o->hwnd = mta_win();
    SetEvent(o->ready);
    for (;;) {
        if (o->pump) while (U(PeekMessageW)(&m, NULL, 0, 0, PM_REMOVE)) U(DispatchMessageW)(&m);
        if (WaitForSingleObject(o->stop, 5) == WAIT_OBJECT_0) break;
    }
    if (o->hwnd && !o->keep_window) mta_destroy(o->hwnd);
    return 0;
}
bool mta_owner_start(struct mta_owner *o, int make_window, int pump, int keep_window) {
    memset(o, 0, sizeof *o);
    o->make_window = make_window; o->pump = pump; o->keep_window = keep_window;
    o->ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    o->stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    o->th = CreateThread(NULL, 0, owner_main, o, 0, &o->tid);
    if (!o->th || WaitForSingleObject(o->ready, 5000) != WAIT_OBJECT_0 || (make_window && !o->hwnd)) {
        mta_owner_stop(o);
        return false;
    }
    return true;
}
void mta_owner_stop(struct mta_owner *o) {
    if (o->stop) SetEvent(o->stop);
    if (o->th) { if (WaitForSingleObject(o->th, 5000) != WAIT_OBJECT_0) TerminateThread(o->th, 0); CloseHandle(o->th); }
    if (o->ready) CloseHandle(o->ready);
    if (o->stop) CloseHandle(o->stop);
    memset(o, 0, sizeof *o);
}

UINT mta_register_utf16(const WCHAR *s) { return U(RegisterWindowMessageW)(s); }
