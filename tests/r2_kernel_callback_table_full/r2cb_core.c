/* r2_kernel_callback_table_full: scenario runner, table copy/hook machinery, GUI worker. */
#include "r2cb.h"
#include "r2cb_data.h"

int r2_kfail;

/* ---- environment ---------------------------------------------------------------------------- */
int r2_ref_revision(void) {
    unsigned long b = 0, u = 0; k_win_build(&b, &u);
    return !t_is_wine() && b == 19041 && u == 2965;
}
int r2_family(void) {
    unsigned long b = 0; k_win_build(&b, NULL);
    return !t_is_wine() && b >= 19041 && b <= 19045;
}
HMODULE r2_user32(void) { return GetModuleHandleA("user32.dll"); }
int r2_user32_text(ULONG_PTR *lo, ULONG_PTR *hi) {
    HMODULE m = r2_user32();
    IMAGE_DOS_HEADER dos; IMAGE_NT_HEADERS64 nt;
    if (!m || !k_try_read(m, &dos, sizeof dos) || dos.e_magic != IMAGE_DOS_SIGNATURE) return 0;
    if (!k_try_read((char *)m + dos.e_lfanew, &nt, sizeof nt) || nt.Signature != IMAGE_NT_SIGNATURE) return 0;
    char *sec0 = (char *)m + dos.e_lfanew + offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + nt.FileHeader.SizeOfOptionalHeader;
    for (unsigned i = 0; i < nt.FileHeader.NumberOfSections && i < 96; i++) {
        IMAGE_SECTION_HEADER sh;
        if (!k_try_read(sec0 + i * sizeof sh, &sh, sizeof sh)) return 0;
        if (sh.Characteristics & IMAGE_SCN_MEM_EXECUTE) {
            *lo = (ULONG_PTR)m + sh.VirtualAddress;
            *hi = *lo + (sh.Misc.VirtualSize > sh.SizeOfRawData ? sh.Misc.VirtualSize : sh.SizeOfRawData);
            return 1;
        }
    }
    return 0;
}
int r2_gui_connect(void) {
    if (!t_proc("IsWindow")) return 0;
    IsWindow((HWND)0);
    return k_cb_table() != NULL;
}
void r2_pump(unsigned ms) { k_pump(ms); }

/* ---- scenarios ------------------------------------------------------------------------------ */
static struct { const char *name; r2_scn_fn fn; } g_scn[256];
static int g_nscn;
void r2_register_scn(const char *name, r2_scn_fn fn) {
    if (g_nscn < 256) { g_scn[g_nscn].name = name; g_scn[g_nscn].fn = fn; g_nscn++; }
}
void r2_run_scn_test(const char *name) {
    struct k_child_result r = k_child_run("r2cb_scn", name, 70000);
    if (r.exit_code == R2_SKIP) { t_skip("scenario %s: prerequisite missing (see child output)", name); return; }
    CHECKF(r.exit_code == 0, "scenario %s: child exit=0x%lx (1 = check failed, 0xDEAD0001 = timeout)", name, (unsigned long)r.exit_code);
}

/* A hung scenario must not sit on the desktop: the child kills itself after 45 s. */
static DWORD WINAPI r2_watchdog(LPVOID p) { Sleep(45000); printf("  FAIL watchdog: scenario exceeded 45 s\n"); ExitProcess(0x7777); return 0; }

CHILD(r2cb_scn) {
    (void)argc;
    const char *name = argc > 1 ? argv[1] : "";
    HANDLE wd = CreateThread(NULL, 0, r2_watchdog, NULL, 0, NULL);
    if (wd) CloseHandle(wd);
    for (int i = 0; i < g_nscn; i++) {
        if (strcmp(g_scn[i].name, name)) continue;
        int rc = g_scn[i].fn();
        r2_uninstall();
        if (r2_kfail) return 1;
        return rc;
    }
    printf("  FAIL no such scenario %s\n", name);
    return 2;
}

/* ---- hooking: private table copy + recording stubs -------------------------------------------- */
struct r2_ev r2_ev_log[R2_NEV];
volatile LONG r2_ev_n;
volatile LONG r2_hit[R2_NSLOT];
unsigned r2_nlive;
r2_ovr_fn r2_ovr[R2_NSLOT];
void *r2_orig[R2_NSLOT];
static void **g_copy;
static void *g_stubs;
static void *g_saved;
static int g_installed;
static int g_atexit;

typedef long (NTAPI *NtCallbackReturn_pfn)(PVOID, ULONG, long);
void r2_cbreturn(ULONG_PTR retval, ULONG outlen_field, void *pout, ULONG ret_len, long status) {
    struct { ULONG_PTR ret; ULONG cb; ULONG pad; void *p; } res;
    res.ret = retval; res.cb = outlen_field; res.pad = 0; res.p = pout;
    NtCallbackReturn_pfn f = (NtCallbackReturn_pfn)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtCallbackReturn");
    f(&res, ret_len, status);
    ExitProcess(0x7778);          /* NtCallbackReturn returns only when no callback is active */
    for (;;) ;
}

static ULONG_PTR r2_common(void *a, ULONG l, ULONG_PTR r8, ULONG_PTR r9, ULONG idx) {
    LONG s = InterlockedIncrement(&r2_ev_n) - 1;
    if (s < R2_NEV) {
        struct r2_ev *e = &r2_ev_log[s];
        e->seq = s; e->tid = GetCurrentThreadId(); e->idx = idx; e->len = l; e->buf = a; e->r8 = r8; e->r9 = r9;
        ULONG n = l > sizeof e->data ? (ULONG)sizeof e->data : l;
        if (!a || !k_try_read(a, e->data, n)) n = 0;
        e->ncopy = n;
    }
    if (idx < R2_NSLOT) InterlockedIncrement(&r2_hit[idx]);
    if (idx >= R2_NSLOT || !r2_orig[idx]) r2_cbreturn(0, 0, NULL, 0x18, 0);   /* padding slot: harmless empty result */
    if (r2_ovr[idx]) r2_ovr[idx](a, l, idx);
    return ((ULONG_PTR (*)(void *, ULONG, ULONG_PTR, ULONG_PTR))r2_orig[idx])(a, l, r8, r9);
}

/* stub: sub rsp,0x38 ; mov dword [rsp+0x20], idx ; mov rax, r2_common ; call rax ; add rsp,0x38 ; ret
 * rcx/rdx/r8/r9 reach r2_common untouched, idx is its fifth argument. */
static void r2_emit_stub(unsigned char *p, unsigned idx) {
    static const unsigned char pre[] = { 0x48, 0x83, 0xEC, 0x38, 0xC7, 0x44, 0x24, 0x20 };
    memcpy(p, pre, sizeof pre); p += sizeof pre;
    memcpy(p, &idx, 4); p += 4;
    *p++ = 0x48; *p++ = 0xB8;
    ULONG_PTR f = (ULONG_PTR)r2_common; memcpy(p, &f, 8); p += 8;
    *p++ = 0xFF; *p++ = 0xD0;
    *p++ = 0x48; *p++ = 0x83; *p++ = 0xC4; *p++ = 0x38;
    *p++ = 0xC3;
}
#define R2_STUB_SZ 48

int r2_installed(void) { return g_installed; }
void *r2_active_table(void) { return g_copy; }
void *r2_saved_table(void) { return g_saved; }

/* The first window a GUI thread creates also makes the thread's default IME window (a second NCCREATE/CREATE, a second CBT
 * create hook call, and a second NCDESTROY when that first window goes away). One window made here and kept alive for the
 * life of the scenario owns the IME window, so the windows a scenario creates after r2_install() are the only ones it sees. */
static HWND g_warm;
int r2_install(void) {
    if (g_installed) return 1;
    void *tbl = k_cb_table();
    if (!tbl) return 0;
    if (!g_warm) { g_warm = k_make_window("r2cb-warm"); k_pump(20); }
    unsigned n = k_cb_count();
    if (!n) return 0;
    g_copy = (void **)VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_stubs = VirtualAlloc(NULL, R2_NSLOT * R2_STUB_SZ, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!g_copy || !g_stubs) return 0;
    r2_nlive = n < R2_NSLOT ? n : R2_NSLOT;
    for (unsigned i = 0; i < R2_NSLOT; i++) {
        void *o = NULL;
        if (i < r2_nlive) k_try_read_ptr((char *)tbl + (size_t)i * sizeof(void *), &o);
        r2_orig[i] = o;
        r2_emit_stub((unsigned char *)g_stubs + (size_t)i * R2_STUB_SZ, i);
        g_copy[i] = (unsigned char *)g_stubs + (size_t)i * R2_STUB_SZ;
    }
    FlushInstructionCache(GetCurrentProcess(), g_stubs, R2_NSLOT * R2_STUB_SZ);
    g_saved = tbl;
    r2_ev_n = 0;
    memset((void *)r2_hit, 0, sizeof r2_hit);
    void **slot = (void **)((char *)k_peb() + PEB_KernelCallbackTable);
    *slot = g_copy;
    g_installed = 1;
    if (!g_atexit) { atexit(r2_uninstall); g_atexit = 1; }
    return 1;
}
void r2_uninstall(void) {
    if (!g_installed) return;
    void **slot = (void **)((char *)k_peb() + PEB_KernelCallbackTable);
    *slot = g_saved;
    g_installed = 0;
    memset(r2_ovr, 0, sizeof r2_ovr);
}
LONG r2_mark(void) { return r2_ev_n; }
void r2_dump(const char *tag, const struct r2_ev *e, unsigned nbytes) {
    if (!t_info_on()) return;   /* addresses, thread ids and raw buffers vary per run */
    if (!e) { printf("  NOTE %s: no event\n", tag); return; }
    printf("  NOTE %s: slot %u len 0x%lx buf %p tid %lu r8 %llx ncopy %lu\n", tag, e->idx, (unsigned long)e->len, e->buf, (unsigned long)e->tid,
           (unsigned long long)e->r8, (unsigned long)e->ncopy);
    for (unsigned o = 0; o < nbytes && o < e->ncopy; o += 32) {
        printf("  NOTE   +%03x:", o);
        for (unsigned i = 0; i < 32 && o + i < e->ncopy && o + i < nbytes; i += 8) {
            unsigned long long q = 0; memcpy(&q, e->data + o + i, o + i + 8 <= e->ncopy ? 8 : e->ncopy - o - i);
            printf(" %016llx", q);
        }
        printf("\n");
    }
}
struct r2_ev *r2_find(unsigned idx, LONG from, int nth) {
    LONG n = r2_ev_n < R2_NEV ? r2_ev_n : R2_NEV;
    for (LONG i = from < 0 ? 0 : from; i < n; i++) {
        if (r2_ev_log[i].idx != idx) continue;
        if (nth-- == 0) return &r2_ev_log[i];
    }
    return NULL;
}
int r2_since(unsigned idx, LONG from) {
    int c = 0; LONG n = r2_ev_n < R2_NEV ? r2_ev_n : R2_NEV;
    for (LONG i = from < 0 ? 0 : from; i < n; i++) if (r2_ev_log[i].idx == idx) c++;
    return c;
}

/* ---- GUI worker ------------------------------------------------------------------------------- */
static struct r2_wk *g_wks[8];
static struct r2_wk *g_creating;
static struct r2_wk *wk_of(HWND h) {
    for (int i = 0; i < 8; i++) if (g_wks[i] && g_wks[i]->hwnd == h) return g_wks[i];
    return g_creating;
}
static LRESULT CALLBACK wk_wndproc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    struct r2_wk *w = wk_of(h);
    if (w) {
        int handled = 0; LRESULT r = 0;
        w->last_tid = GetCurrentThreadId();
        if (m >= WM_USER && m < 0x8000) {
            w->nuser++;
            w->last_msg = m; w->last_wp = wp; w->last_lp = lp;
            w->ci_hwnd = r2_teb_q(R2_CI_CallbackWnd_hwnd);
            w->ci_pwnd = r2_teb_q(R2_CI_CallbackWnd_pwnd);
            w->ci_actctx = r2_teb_q(R2_CI_CallbackWnd_actctx);
            w->ci_dde = r2_teb_d(R2_CI_cInDDEMLCallback);
        }
        if (w->proc) { r = w->proc(w, h, m, wp, lp, &handled); if (handled) return r; }
        if (m >= WM_USER && m < 0x8000) return R2_WK_RET(m);
    }
    return DefWindowProcW(h, m, wp, lp);
}
void *r2_wk_proc_addr(void) { return (void *)wk_wndproc; }
static DWORD WINAPI wk_main(LPVOID p) {
    struct r2_wk *w = (struct r2_wk *)p;
    w->tid = GetCurrentThreadId();
    HINSTANCE hi = GetModuleHandleW(NULL);
    WNDCLASSW wc; memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = wk_wndproc; wc.hInstance = hi; wc.lpszClassName = w->cls;
    RegisterClassW(&wc);
    g_creating = w;
    w->hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, w->cls, L"r2wk", WS_POPUP,
                              -32000, -32000, 1, 1, w->top_level ? NULL : HWND_MESSAGE, NULL, hi, NULL);
    g_creating = NULL;
    SetEvent(w->ready);
    if (!w->hwnd) return 1;
    DWORD t0 = GetTickCount();
    while (!w->stop && GetTickCount() - t0 < 25000) {
        MsgWaitForMultipleObjectsEx(0, NULL, 25, QS_ALLINPUT, MWMO_ALERTABLE | MWMO_INPUTAVAILABLE);
        MSG msg;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
    }
    DestroyWindow(w->hwnd);
    UnregisterClassW(w->cls, hi);
    return 0;
}
int r2_wk_start(struct r2_wk *w, const char *tag, int top_level) {
    { r2_wk_proc keep = w->proc; memset(w, 0, sizeof *w); w->proc = keep; }
    w->top_level = top_level;
    _snwprintf(w->cls, 63, L"win32ktR2_%hs_%lu_%p", tag, GetCurrentProcessId(), (void *)w);
    w->ready = CreateEventA(NULL, TRUE, FALSE, NULL);
    for (int i = 0; i < 8; i++) if (!g_wks[i]) { g_wks[i] = w; break; }
    w->thread = CreateThread(NULL, 0, wk_main, w, 0, NULL);
    if (!w->thread) return 0;
    WaitForSingleObject(w->ready, 5000);
    if (!w->hwnd) { r2_wk_stop(w); return 0; }
    return 1;
}
void r2_wk_stop(struct r2_wk *w) {
    for (int i = 0; i < 8; i++) if (g_wks[i] == w) g_wks[i] = NULL;
    if (w->thread) {
        InterlockedExchange(&w->stop, 1);
        WaitForSingleObject(w->thread, 5000);
        CloseHandle(w->thread);
        w->thread = NULL;
    }
    if (w->ready) { CloseHandle(w->ready); w->ready = NULL; }
}
int r2_send(HWND h, UINT m, WPARAM wp, LPARAM lp, LRESULT *res) {
    DWORD_PTR r = 0;
    LRESULT ok = SendMessageTimeoutW(h, m, wp, lp, SMTO_NORMAL, 5000, &r);
    if (res) *res = (LRESULT)r;
    return ok != 0;
}
