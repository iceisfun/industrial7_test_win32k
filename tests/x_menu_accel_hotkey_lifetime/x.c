#include "x.h"

__thread jmp_buf xm_jb; __thread volatile int xm_armed; __thread volatile unsigned long xm_exc; static int veh_on;
static LONG CALLBACK xm_veh(PEXCEPTION_POINTERS ep) {
    DWORD c = ep->ExceptionRecord->ExceptionCode;
    if (xm_armed && ((c & 0xC0000000) == 0xC0000000 || c == 0x80000002 || c == 0x80000100) && c != 0xC0000194 && c != 0xC000013A) {
        xm_armed = 0; xm_exc = c; longjmp(xm_jb, 1);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
/* Runs f(arg) with exceptions caught on this thread; returns the exception code or 0. */
unsigned long xm_try(void (*f)(void *), void *arg) {
    if (!veh_on) { AddVectoredExceptionHandler(1, xm_veh); veh_on = 1; }
    jmp_buf save; memcpy(&save, &xm_jb, sizeof save); int was = xm_armed;
    unsigned long code = 0;
    if (!setjmp(xm_jb)) { xm_armed = 1; f(arg); xm_armed = was; }
    else code = xm_exc;
    memcpy(&xm_jb, &save, sizeof save); xm_armed = was;
    return code;
}
void xm_run_guarded(void (*f)(void)) {
    /* a win32k service issued before user32 is loaded ends the process: the thread cannot become a GUI thread */
    t_module("user32.dll"); t_module("gdi32.dll");
    if (!veh_on) { AddVectoredExceptionHandler(1, xm_veh); veh_on = 1; }
    if (!setjmp(xm_jb)) { xm_armed = 1; f(); xm_armed = 0; }
    else if (t_is_wine()) t_skip("emulator fault 0x%lx", xm_exc);
    else t_check(0, __FILE__, __LINE__, "no exception", "exception 0x%lx escaped a syscall", xm_exc);
}

void xm_fail(LONGLONG ret, DWORD gle, DWORD want, const char *file, int line, const char *what) {
    t_check((ret & 0xFFFFFFFF) == 0, file, line, what, "call unexpectedly returned 0x%llx", (unsigned long long)ret);
    t_check(gle == want, file, line, what, "last error %lu want %lu", gle, want);
}

void xm_acc_fill(xm_acc6 *a, int n) {
    for (int i = 0; i < n; i++) { a[i].fVirt = FVIRTKEY; a[i].pad = 0; a[i].key = (WORD)('A' + i % 26); a[i].cmd = (WORD)(1000 + i); }
}
HANDLE xm_acc_make(int n) {
    xm_acc6 *a = VirtualAlloc(NULL, (size_t)n * 6 + 16, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!a) return NULL;
    xm_acc_fill(a, n);
    HANDLE h = UN(NtUserCreateAcceleratorTable)(a, n);
    VirtualFree(a, 0, MEM_RELEASE);
    return h;
}

static DWORD WINAPI thr_main(LPVOID p) {
    struct xm_thr *t = p;
    t->hwnd = k_make_window("xm_thr");
    t->tid = GetCurrentThreadId();
    SetEvent(t->ready);
    while (WaitForSingleObject(t->stop, 0) == WAIT_TIMEOUT) k_pump(10);
    if (t->hwnd) U(DestroyWindow)(t->hwnd);
    return 0;
}
int xm_thr_start(struct xm_thr *t) {
    memset(t, 0, sizeof *t);
    t->ready = CreateEventA(NULL, TRUE, FALSE, NULL); t->stop = CreateEventA(NULL, TRUE, FALSE, NULL);
    t->thread = CreateThread(NULL, 0, thr_main, t, 0, NULL);
    if (!t->thread || WaitForSingleObject(t->ready, 10000) != WAIT_OBJECT_0 || !t->hwnd) { xm_thr_stop(t); return 0; }
    return 1;
}
void xm_thr_stop(struct xm_thr *t) {
    if (t->stop) SetEvent(t->stop);
    if (t->thread) { WaitForSingleObject(t->thread, 10000); CloseHandle(t->thread); }
    if (t->ready) CloseHandle(t->ready);
    if (t->stop) CloseHandle(t->stop);
    memset(t, 0, sizeof *t);
}

struct xm_key xm_key_next(void) {
    static int n;
    struct xm_key k;
    int i = n++ % 84;
    k.vk = 0x7C + (i % 12);                       /* VK_F13 .. VK_F24 */
    k.mods = (UINT)(1 + (i / 12) % 7);            /* every non-empty combination of alt, ctrl, shift (the Windows key combinations are partly taken by the shell) */
    return k;
}

const char *xm_tag(void) {
    static char b[8][48]; static int n;
    char *s = b[n++ % 8];
    snprintf(s, 48, "win32kt_xm_%lu_%d", GetCurrentProcessId(), n);
    return s;
}
struct xm_shm *xm_shm_create(const char *tag, HANDLE *h) {
    *h = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 4096, tag);
    return *h ? MapViewOfFile(*h, FILE_MAP_ALL_ACCESS, 0, 0, 4096) : NULL;
}
struct xm_shm *xm_shm_open(const char *tag, HANDLE *h) {
    *h = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, tag);
    return *h ? MapViewOfFile(*h, FILE_MAP_ALL_ACCESS, 0, 0, 4096) : NULL;
}
int xm_wait(volatile LONG *flag, DWORD ms) {
    DWORD t0 = GetTickCount();
    while (!*flag) { if (GetTickCount() - t0 > ms) return 0; Sleep(5); }
    return 1;
}
HANDLE xm_spawn(const char *child, const char *tag, const char *extra, PROCESS_INFORMATION *pi) {
    char a[256]; snprintf(a, sizeof a, "%s %s", tag, extra ? extra : "");
    return t_spawn_child(child, a, 0, NULL, NULL, NULL, FALSE, NULL, pi);
}
DWORD xm_reap(HANDLE p, PROCESS_INFORMATION *pi, DWORD ms) {
    DWORD ec = 0xDEAD0001;
    if (WaitForSingleObject(p, ms) == WAIT_OBJECT_0) GetExitCodeProcess(p, &ec);
    else { TerminateProcess(p, 1); WaitForSingleObject(p, 5000); }
    CloseHandle(p); if (pi && pi->hThread) CloseHandle(pi->hThread);
    return ec;
}
int xm_key_free_within(struct xm_key k, DWORD ms) {
    DWORD t0 = GetTickCount();
    for (;;) {
        if (UN(NtUserRegisterHotKey)(NULL, 0x7A11, k.mods, k.vk)) { UN(NtUserUnregisterHotKey)(NULL, 0x7A11); return 1; }
        if (GetTickCount() - t0 > ms) return 0;
        Sleep(20);
    }
}

/* ---- menu thread ---- */
static __thread struct xm_menu *cur_menu;
static VOID CALLBACK menu_timer(HWND h, UINT m, UINT_PTR id, DWORD t) {
    struct xm_menu *mm = cur_menu;
    if (!mm) return;
    LONG n = InterlockedIncrement(&mm->ticks);
    if (mm->on_tick) mm->on_tick(mm, (int)n);
    if (WaitForSingleObject(mm->rel, 0) == WAIT_OBJECT_0 && !mm->endcalled) {
        mm->endcalled = 1;
        mm->endret = U(EndMenu)();
    }
}
static DWORD WINAPI menu_main(LPVOID p) {
    struct xm_menu *mm = p;
    cur_menu = mm;
    mm->w = k_make_window("xm_menu");
    mm->tid = GetCurrentThreadId();
    mm->menu = U(CreatePopupMenu)();
    U(AppendMenuA)(mm->menu, MF_STRING, 7, "one"); U(AppendMenuA)(mm->menu, MF_STRING, 8, "two");
    if (mm->mnstyle) { MENUINFO mi = { sizeof mi, MIM_STYLE, mm->mnstyle, 0 }; U(SetMenuInfo)(mm->menu, &mi); }
    UINT_PTR t = U(SetTimer)(NULL, 0, 50, menu_timer);
    mm->state = 1;
    mm->ret = U(TrackPopupMenu)(mm->menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN, 200, 200, 0, mm->w, NULL);
    mm->state = 2;
    U(KillTimer)(NULL, t);
    k_pump(10);
    U(DestroyMenu)(mm->menu); U(DestroyWindow)(mm->w);
    return 0;
}
DWORD xm_gui_flags(DWORD tid) {
    GUITHREADINFO gi; memset(&gi, 0, sizeof gi); gi.cbSize = sizeof gi;
    if (!U(GetGUIThreadInfo)(tid, &gi)) return 0;
    return gi.flags;
}
int xm_menu_start(struct xm_menu *m, DWORD mnstyle, void (*on_tick)(struct xm_menu *, int), void *arg) {
    memset(m, 0, sizeof *m);
    m->rel = CreateEventA(NULL, TRUE, FALSE, NULL);
    m->mnstyle = mnstyle; m->on_tick = on_tick; m->arg = arg;
    m->thread = CreateThread(NULL, 0, menu_main, m, 0, NULL);
    if (!m->thread) return 0;
    DWORD t0 = GetTickCount();
    while (GetTickCount() - t0 < 5000) {
        if (m->state == 1 && m->tid && (xm_gui_flags(m->tid) & GUI_INMENUMODE)) return 1;
        if (m->state == 2) return 0;
        Sleep(10);
    }
    return 0;
}
int xm_menu_alive(struct xm_menu *m) { return m->state == 1 && (xm_gui_flags(m->tid) & GUI_INMENUMODE); }
LONG xm_menu_end(struct xm_menu *m) {
    LONG r = -1;
    SetEvent(m->rel);
    if (m->thread) {
        if (WaitForSingleObject(m->thread, 10000) == WAIT_OBJECT_0) r = m->ret;
        CloseHandle(m->thread);
    }
    CloseHandle(m->rel);
    return r;
}

/* ---- command thread ---- */
static DWORD WINAPI ct_main(LPVOID p) {
    struct xm_ct *t = p;
    t_module("user32.dll");
    if (t->use_wnd) t->w = k_make_window("xm_ct");
    t->tid = GetCurrentThreadId();
    SetEvent(t->ready);
    for (;;) {
        k_pump(5);
        LONG cmd = t->cmd;
        if (!cmd) continue;
        HWND h = t->use_wnd ? t->w : NULL;
        SET_ERR(XE_SENT);
        switch (cmd) {
        case CT_REG:   t->ret = UN(NtUserRegisterHotKey)(h, t->id, t->mods, t->vk); break;
        case CT_UNREG: t->ret = UN(NtUserUnregisterHotKey)(h, t->id); break;
        case CT_DESTROYWND: t->ret = U(DestroyWindow)(t->w); t->w = NULL; break;
        case CT_QUIT: t->gle = 0; t->cmd = 0; t->busy = 0; return 0;
        }
        t->gle = GetLastError();
        t->cmd = 0; t->busy = 0;
    }
}
int xm_ct_start(struct xm_ct *t, int with_window) {
    memset(t, 0, sizeof *t);
    t->ready = CreateEventA(NULL, TRUE, FALSE, NULL);
    t->use_wnd = with_window;
    t->thread = CreateThread(NULL, 0, ct_main, t, 0, NULL);
    if (!t->thread || WaitForSingleObject(t->ready, 10000) != WAIT_OBJECT_0) return 0;
    t->use_wnd = 0;
    return 1;
}
LONG64 xm_ct_do(struct xm_ct *t, int cmd, int use_wnd, int id, UINT mods, UINT vk) {
    t->use_wnd = use_wnd; t->id = id; t->mods = mods; t->vk = vk; t->ret = -99; t->gle = 0;
    t->busy = 1; t->cmd = cmd;
    DWORD t0 = GetTickCount();
    while (t->busy && GetTickCount() - t0 < 10000) Sleep(1);
    return t->ret;
}
void xm_ct_stop(struct xm_ct *t) {
    if (t->thread) {
        t->busy = 1; t->cmd = CT_QUIT;
        WaitForSingleObject(t->thread, 10000);
        CloseHandle(t->thread);
    }
    if (t->ready) CloseHandle(t->ready);
    memset(t, 0, sizeof *t);
}
