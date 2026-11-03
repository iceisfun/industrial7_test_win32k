/* Process-wide DPI awareness. Everything here changes irreversible per-process state (SetProcessDpiAwarenessContext locks the
 * process after the first successful call), so each scenario runs in its own child process. A second group of tests keeps a child
 * alive in a chosen awareness state and queries it from this process through NtUserGetProcessDpiAwarenessContext /
 * NtUserGetSystemDpiForProcess, which only read another process's win32 process info (ppi+0x118 / +0x11c).
 *
 * user32!SetProcessDpiAwarenessContext: invalid context -> ERROR_INVALID_PARAMETER; once gfDpiAwarenessInitialized is set (by a
 * successful call, a manifest or SetProcessDPIAware) every further call fails with ERROR_ACCESS_DENIED (5) - except the legacy
 * SetProcessDPIAware, which returns TRUE when already initialized.
 */
#include "dm.h"

#define CC(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fail++; } } while (0)
#define CCF(c, ...) do { if (!(c)) { printf("FAIL %s:%d: %s (", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf(")\n"); fail++; } } while (0)
#define CDPI(c) ((DPI_AWARENESS_CONTEXT)(ULONG_PTR)(c))

static int proc_awareness(void) { int a = -9; return UN(GetProcessDpiAwarenessInternal)(GetCurrentProcess(), &a) ? a : -9; }
/* a win32u syscall made before user32 is loaded has no kernel callback table to call back into: load user32 first */
static unsigned proc_dpi(void) { if (t_is_wine()) return 96; t_module("user32.dll"); return UN(NtUserGetDpiForCurrentProcess)() & 0xffff; }

/* ---- scenarios (child side) --------------------------------------------------------------------- */

static int sc_default_unaware(void) {
    int fail = 0;
    CC(U(GetThreadDpiAwarenessContext) != NULL);
    unsigned long v = 1;
    CC(dm_ci(DM_CI_CTX, &v) && v == 0);                                     /* no explicit thread context in a fresh thread */
    CC(dm_ci(DM_CI_HOST, &v) && v == 0);
    unsigned cur = dm_cur_ctx();
    CCF((cur & 0xf) == 0, "default context 0x%x of a manifest-less process", cur);
    CC(!U(IsProcessDPIAware)());
    CC(proc_awareness() == 0);
    CC(U(GetAwarenessFromDpiAwarenessContext)(U(GetThreadDpiAwarenessContext)()) == DPI_AWARENESS_UNAWARE);
    HWND w = k_make_window("dm_child_unaware");
    CC(w != NULL);
    if (w) {
        CC(U(GetDpiForWindow)(w) == 96);
        CC((int)U(GetAwarenessFromDpiAwarenessContext)(U(GetWindowDpiAwarenessContext)(w)) == DPI_AWARENESS_UNAWARE);
        U(DestroyWindow)(w);
    }
    HDC dc = U(GetDC)(NULL);
    CC(dc != NULL);
    if (dc) { CC(U(GetDeviceCaps)(dc, LOGPIXELSX) == 96); CC(U(GetDeviceCaps)(dc, LOGPIXELSY) == 96); U(ReleaseDC)(NULL, dc); }
    if (dm_composited() == 1) CC(U(GetDpiForSystem)() == 96);
    return fail;
}

static int sc_sp_first_ok_second_denied(void) {
    int fail = 0;
    SetLastError(0);
    CC(U(SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) == TRUE);
    CC(proc_awareness() == 2);
    CC(U(IsProcessDPIAware)());
    CCF(dm_cur_ctx() == DM_RAW_PMONITOR2, "thread default after the call: 0x%x", dm_cur_ctx());      /* gDefaultDpiContext = validated context */
    DPI_AWARENESS_CONTEXT again[] = { DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE, DPI_AWARENESS_CONTEXT_UNAWARE, DPI_AWARENESS_CONTEXT_SYSTEM_AWARE, DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED };
    for (unsigned i = 0; i < 5; i++) {
        SetLastError(0);
        CCF(U(SetProcessDpiAwarenessContext)(again[i]) == FALSE, "attempt %u succeeded", i);
        CCF(GetLastError() == ERROR_ACCESS_DENIED, "attempt %u error %lu", i, GetLastError());
    }
    CC(proc_awareness() == 2);
    return fail;
}

static int sc_sp_invalid_then_valid(void) {
    int fail = 0;
    ULONG_PTR bad[] = { 0, (ULONG_PTR)-6, (ULONG_PTR)-100, 0x1234, 0x16010, 0x80000000, 0xffffffff };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        SetLastError(0);
        CCF(U(SetProcessDpiAwarenessContext)((DPI_AWARENESS_CONTEXT)bad[i]) == FALSE, "0x%llx accepted", (unsigned long long)bad[i]);
        CCF(GetLastError() == ERROR_INVALID_PARAMETER, "0x%llx error %lu", (unsigned long long)bad[i], GetLastError());
    }
    CC(!U(IsProcessDPIAware)());                                           /* nothing was initialized by the failed calls */
    CC(proc_awareness() == 0);
    CC(U(SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE) == TRUE);
    CC(proc_awareness() == 1);
    CC(U(IsProcessDPIAware)());
    SetLastError(0);
    CC(U(SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE) == FALSE);
    CC(GetLastError() == ERROR_ACCESS_DENIED);
    return fail;
}

static int sc_sp_unaware_locks(void) {
    int fail = 0;
    CC(U(SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT_UNAWARE) == TRUE);        /* an explicit "unaware" is also a first-and-only choice */
    CC(proc_awareness() == 0);
    CC(!U(IsProcessDPIAware)());
    SetLastError(0);
    CC(U(SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE) == FALSE);
    CC(GetLastError() == ERROR_ACCESS_DENIED);
    CC(proc_awareness() == 0);
    CC(U(SetProcessDPIAware)() == TRUE);                                   /* legacy call on an initialized process is a no-op that returns TRUE */
    CC(!U(IsProcessDPIAware)());
    return fail;
}

static int sc_sp_system_aware(void) {
    int fail = 0;
    CC(U(SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE) == TRUE);
    unsigned dpi = proc_dpi();
    CC(proc_awareness() == 1);
    CCF(dm_cur_ctx() == DM_RAW_SYSTEM(dpi), "thread default 0x%x, expected 0x%x", dm_cur_ctx(), DM_RAW_SYSTEM(dpi));
    CC(U(GetDpiForSystem)() == dpi);
    HWND w = k_make_window("dm_child_system");
    CC(w != NULL);
    if (w) { CC(U(GetDpiForWindow)(w) == dpi); CC((int)U(GetAwarenessFromDpiAwarenessContext)(U(GetWindowDpiAwarenessContext)(w)) == DPI_AWARENESS_SYSTEM_AWARE); U(DestroyWindow)(w); }
    HDC dc = U(GetDC)(NULL);
    if (dc) { CC((unsigned)U(GetDeviceCaps)(dc, LOGPIXELSX) == dpi); U(ReleaseDC)(NULL, dc); }
    if (!t_is_wine()) {
        unsigned k = UN(NtUserGetProcessDpiAwarenessContext)(NULL);
        CCF(k == DM_RAW_SYSTEM(dpi), "kernel context 0x%x", k);
    }
    return fail;
}

static int sc_sp_per_monitor(void) {
    int fail = 0;
    CC(U(SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE) == TRUE);
    CC(proc_awareness() == 2);
    CCF(dm_cur_ctx() == DM_RAW_PMONITOR, "thread default 0x%x", dm_cur_ctx());
    HWND w = k_make_window("dm_child_pm");
    CC(w != NULL);
    if (w) { CC(U(AreDpiAwarenessContextsEqual)(U(GetWindowDpiAwarenessContext)(w), DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE)); CC(U(GetDpiForWindow)(w) >= 96); U(DestroyWindow)(w); }
    return fail;
}

static int sc_sp_gdiscaled(void) {
    int fail = 0;
    BOOL r = U(SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED);
    printf("NOTE SetProcessDpiAwarenessContext(GDISCALED) = %d (error %lu)\n", r, GetLastError());
    if (r) { CC(proc_awareness() == 0); CC((dm_cur_ctx() & 0x40000000u) != 0); SetLastError(0); CC(U(SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT_UNAWARE) == FALSE); CC(GetLastError() == ERROR_ACCESS_DENIED); }
    return fail;
}

static int sc_sp_legacy_api(void) {
    int fail = 0;
    CC(U(SetProcessDPIAware)() == TRUE);
    CC(U(IsProcessDPIAware)());
    CC(proc_awareness() == 1);
    CC(U(SetProcessDPIAware)() == TRUE);                                    /* already initialized and not forced: returns 1 without touching anything */
    SetLastError(0);
    CC(U(SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) == FALSE);
    CC(GetLastError() == ERROR_ACCESS_DENIED);
    CC(proc_awareness() == 1);
    return fail;
}

static int sc_sp_shcore(void) {
    int fail = 0;
    shcore_SetProcessDpiAwareness_pfn setp = DM_SHCORE(shcore_SetProcessDpiAwareness);
    shcore_GetProcessDpiAwareness_pfn getp = DM_SHCORE(shcore_GetProcessDpiAwareness);
    CC(setp != NULL && getp != NULL);
    if (!setp || !getp) return fail;
    CC((HRESULT)setp(5) == E_INVALIDARG);                                   /* SetProcessDpiAwarenessInternal: unknown value -> ERROR_INVALID_PARAMETER */
    CC((HRESULT)setp(-1) == E_INVALIDARG);
    int a = -9;
    CC((HRESULT)getp(NULL, &a) == S_OK && a == 0);
    CC((HRESULT)setp(2) == S_OK);
    CC((HRESULT)getp(NULL, &a) == S_OK && a == 2);
    CC((HRESULT)setp(1) == E_ACCESSDENIED);                                 /* second call: ERROR_ACCESS_DENIED -> HRESULT_FROM_WIN32(5) */
    CC((HRESULT)setp(2) == E_ACCESSDENIED);
    CC((HRESULT)setp(0) == E_ACCESSDENIED);
    CC((HRESULT)getp(GetCurrentProcess(), &a) == S_OK && a == 2);
    CC(proc_awareness() == 2);
    return fail;
}

static int sc_sp_shcore_system_value(void) {
    int fail = 0;
    shcore_SetProcessDpiAwareness_pfn setp = DM_SHCORE(shcore_SetProcessDpiAwareness);
    shcore_GetProcessDpiAwareness_pfn getp = DM_SHCORE(shcore_GetProcessDpiAwareness);
    CC(setp != NULL && getp != NULL);
    if (!setp || !getp) return fail;
    CC((HRESULT)setp(1) == S_OK);                                           /* PROCESS_SYSTEM_DPI_AWARE */
    int a = -9;
    CC((HRESULT)getp(NULL, &a) == S_OK && a == 1);
    CC(U(IsProcessDPIAware)());
    return fail;
}

static int sc_thread_override_precedence(void) {
    int fail = 0;
    CC(!U(IsProcessDPIAware)());
    CC(proc_awareness() == 0);
    CC(U(SetThreadDpiAwarenessContext)(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != NULL);
    CC(U(IsProcessDPIAware)());                                             /* IsProcessDPIAware reads the thread word first */
    CC(proc_awareness() == 2);                                              /* the kernel answers for the calling thread's context */
    HWND w = k_make_window("dm_child_ovr");
    if (w) { CC((int)U(GetAwarenessFromDpiAwarenessContext)(U(GetWindowDpiAwarenessContext)(w)) == DPI_AWARENESS_PER_MONITOR_AWARE); U(DestroyWindow)(w); }
    CC(U(SetThreadDpiAwarenessContext)(DPI_AWARENESS_CONTEXT_UNAWARE) != NULL);
    CC(!U(IsProcessDPIAware)());
    CC(proc_awareness() == 0);
    return fail;
}

static int sc_thread_override_survives_process_set(void) {
    int fail = 0;
    CC(U(SetThreadDpiAwarenessContext)(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE) != NULL);
    CC(U(SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE) == TRUE);
    CCF(dm_cur_ctx() == DM_RAW_PMONITOR, "explicit thread context was replaced: 0x%x", dm_cur_ctx());
    CC(proc_awareness() == 2);                                              /* still the thread's */
    unsigned long v = 0;
    CC(dm_ci(DM_CI_CTX, &v) && v == DM_RAW_PMONITOR);
    return fail;
}

struct ntf { unsigned cur; int awareness; };
static DWORD WINAPI ntf_thread(LPVOID p) { struct ntf *n = (struct ntf *)p; n->cur = dm_cur_ctx(); n->awareness = proc_awareness(); return 0; }
static int sc_new_thread_follows_process_default(void) {
    int fail = 0;
    CC(U(SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) == TRUE);
    CC(U(SetThreadDpiAwarenessContext)(DPI_AWARENESS_CONTEXT_UNAWARE) != NULL);     /* main thread overrides, process stays PMv2 */
    struct ntf n = { 0, -9 };
    HANDLE t = CreateThread(NULL, 0, ntf_thread, &n, 0, NULL);
    CC(t != NULL);
    if (t) { WaitForSingleObject(t, 10000); CloseHandle(t); }
    CCF(n.cur == DM_RAW_PMONITOR2, "new thread context 0x%x", n.cur);
    CC(n.awareness == 2);
    return fail;
}

struct vm { int w, h; };
static int sc_virtualized_metrics(void) {
    int fail = 0;
    unsigned dpi = proc_dpi();
    HWND w = U(CreateWindowExA)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, "STATIC", "dm_virt", WS_POPUP, 100, 100, 200, 100, NULL, NULL, GetModuleHandleA(NULL), NULL);
    CC(w != NULL);
    if (!w) return fail;
    RECT r = { 0 };
    CC(U(GetWindowRect)(w, &r));
    CCF(r.right - r.left == 200 && r.bottom - r.top == 100, "unaware GetWindowRect %ldx%ld", r.right - r.left, r.bottom - r.top);   /* logical size is what the app asked for */
    RECT c = { 0 };
    CC(U(GetClientRect)(w, &c));
    CC(c.right == 200 && c.bottom == 100);
    int uw = U(GetSystemMetrics)(SM_CXSCREEN), uh = U(GetSystemMetrics)(SM_CYSCREEN);
    CC(U(SetThreadDpiAwarenessContext)(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != NULL);
    RECT pr = { 0 };
    CC(U(GetWindowRect)(w, &pr));
    int pw = U(GetSystemMetrics)(SM_CXSCREEN), ph = U(GetSystemMetrics)(SM_CYSCREEN);
    int ew = MulDiv(200, dpi, 96), eh = MulDiv(100, dpi, 96);
    CCF(abs((pr.right - pr.left) - ew) <= 2 && abs((pr.bottom - pr.top) - eh) <= 2, "physical window %ldx%ld, expected about %dx%d at %u dpi", pr.right - pr.left, pr.bottom - pr.top, ew, eh, dpi);
    if (dpi == 96) CC(uw == pw && uh == ph);
    else if (dm_composited() == 1) CCF(abs(uw - MulDiv(pw, 96, dpi)) <= 1 && abs(uh - MulDiv(ph, 96, dpi)) <= 1, "unaware screen %dx%d physical %dx%d at %u dpi", uw, uh, pw, ph, dpi);
    printf("NOTE process dpi %u, unaware screen %dx%d, physical %dx%d, window physical %ldx%ld\n", dpi, uw, uh, pw, ph, pr.right - pr.left, pr.bottom - pr.top);
    U(DestroyWindow)(w);
    return fail;
}

static int sc_pmv2_not_virtualized(void) {
    int fail = 0;
    CC(U(SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) == TRUE);
    HWND w = U(CreateWindowExA)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, "STATIC", "dm_pmv2", WS_POPUP, 100, 100, 200, 100, NULL, NULL, GetModuleHandleA(NULL), NULL);
    CC(w != NULL);
    if (!w) return fail;
    RECT r = { 0 };
    CC(U(GetWindowRect)(w, &r));
    CCF(r.right - r.left == 200 && r.bottom - r.top == 100, "PMv2 window %ldx%ld: pixel sizes are never rescaled", r.right - r.left, r.bottom - r.top);
    MONITORINFO mi = { sizeof mi };
    CC(U(GetMonitorInfoW)(U(MonitorFromPoint)((POINT){ 0, 0 }, MONITOR_DEFAULTTOPRIMARY), &mi));
    CC(U(GetSystemMetrics)(SM_CXSCREEN) == mi.rcMonitor.right - mi.rcMonitor.left);
    U(DestroyWindow)(w);
    return fail;
}

static int sc_hold(const char *mode, unsigned long ppid);

static const struct { const char *name; int (*fn)(void); } scs[] = {
    { "default_unaware", sc_default_unaware }, { "sp_first_ok_second_denied", sc_sp_first_ok_second_denied }, { "sp_invalid_then_valid", sc_sp_invalid_then_valid },
    { "sp_unaware_locks", sc_sp_unaware_locks }, { "sp_system_aware", sc_sp_system_aware }, { "sp_per_monitor", sc_sp_per_monitor }, { "sp_gdiscaled", sc_sp_gdiscaled },
    { "sp_legacy_api", sc_sp_legacy_api }, { "sp_shcore", sc_sp_shcore }, { "sp_shcore_system_value", sc_sp_shcore_system_value },
    { "thread_override_precedence", sc_thread_override_precedence }, { "thread_override_survives_process_set", sc_thread_override_survives_process_set },
    { "new_thread_follows_process_default", sc_new_thread_follows_process_default }, { "virtualized_metrics", sc_virtualized_metrics }, { "pmv2_not_virtualized", sc_pmv2_not_virtualized },
};

CHILD(display_monitors_dpi_child) {
    const char *sel = argc > 1 ? argv[1] : "";
    if (!strncmp(sel, "hold:", 5)) return sc_hold(sel + 5, argc > 2 ? strtoul(argv[2], NULL, 10) : 0);
    for (unsigned i = 0; i < sizeof scs / sizeof *scs; i++) if (!strcmp(sel, scs[i].name)) return scs[i].fn();
    printf("FAIL unknown scenario '%s'\n", sel);
    return 1;
}

static int sc_hold(const char *mode, unsigned long ppid) {
    int fail = 0;
    char n1[96], n2[96];
    snprintf(n1, sizeof n1, "win32kt_dm_%lu_ready", ppid); snprintf(n2, sizeof n2, "win32kt_dm_%lu_go", ppid);
    HANDLE ready = OpenEventA(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, n1), go = OpenEventA(SYNCHRONIZE, FALSE, n2);
    CC(ready != NULL && go != NULL);
    if (!ready || !go) return fail + 1;
    if (strcmp(mode, "none")) {
        U(GetDpiForSystem)();                                               /* first user32 call: connects the process to win32k and converts the thread */
        if (!strcmp(mode, "pmv2")) CC(U(SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
        else if (!strcmp(mode, "pm")) CC(U(SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE));
        else if (!strcmp(mode, "system")) CC(U(SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE));
        else if (!strcmp(mode, "unaware_explicit")) CC(U(SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT_UNAWARE));
        else if (!strcmp(mode, "threadovr")) CC(U(SetThreadDpiAwarenessContext)(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != NULL);
        else if (!strcmp(mode, "unaware")) { /* leave defaults */ }
        /* a kernel round trip so the thread context is captured too */
        if (!t_is_wine()) UN(NtUserGetProcessDpiAwarenessContext)(NULL);
    }
    SetEvent(ready);
    WaitForSingleObject(go, 20000);
    return fail;
}

/* ---- parent side --------------------------------------------------------------------------------- */

static void run_sc(const char *name) {
    if (dm_wine_no_child()) return;
    struct k_child_result r = k_child_run("display_monitors_dpi_child", name, 60000);
    CHECKF(r.exit_code == 0, "child scenario %s exit code 0x%lx (0 = clean, else its failure count / crash code)", name, (unsigned long)r.exit_code);
}
#define SCENARIO_TEST(tname, scn) TEST(display_monitors_dpi_proc_##tname) { NEED_GUI(); run_sc(scn); }

SCENARIO_TEST(default_process_is_unaware_with_no_thread_override, "default_unaware")
SCENARIO_TEST(set_context_first_call_succeeds_every_later_call_is_denied, "sp_first_ok_second_denied")
SCENARIO_TEST(set_context_invalid_values_do_not_initialize_awareness, "sp_invalid_then_valid")
SCENARIO_TEST(set_context_unaware_is_a_locking_choice, "sp_unaware_locks")
SCENARIO_TEST(set_context_system_aware_reports_system_dpi_everywhere, "sp_system_aware")
SCENARIO_TEST(set_context_per_monitor_v1, "sp_per_monitor")
SCENARIO_TEST(set_context_gdi_scaled_is_accepted_or_rejected_cleanly, "sp_gdiscaled")
SCENARIO_TEST(legacy_set_process_dpi_aware_locks_system_awareness, "sp_legacy_api")
SCENARIO_TEST(shcore_set_process_dpi_awareness_hresults, "sp_shcore")
SCENARIO_TEST(shcore_system_dpi_value, "sp_shcore_system_value")
SCENARIO_TEST(thread_override_takes_precedence_over_unaware_process, "thread_override_precedence")
SCENARIO_TEST(thread_override_survives_a_later_process_call, "thread_override_survives_process_set")
SCENARIO_TEST(new_thread_gets_process_default_not_creators_override, "new_thread_follows_process_default")
SCENARIO_TEST(unaware_process_sees_virtualized_window_and_screen_sizes, "virtualized_metrics")
SCENARIO_TEST(pmv2_process_sees_pixel_values, "pmv2_not_virtualized")

/* parent: keep a child in a state and query it */
struct hold { PROCESS_INFORMATION pi; HANDLE ready, go; int ok; };
static int hold_start(struct hold *h, const char *mode) {
    char n1[96], n2[96], args[128];
    memset(h, 0, sizeof *h);
    snprintf(n1, sizeof n1, "win32kt_dm_%lu_ready", GetCurrentProcessId()); snprintf(n2, sizeof n2, "win32kt_dm_%lu_go", GetCurrentProcessId());
    h->ready = CreateEventA(NULL, TRUE, FALSE, n1); h->go = CreateEventA(NULL, TRUE, FALSE, n2);
    snprintf(args, sizeof args, "hold:%s %lu", mode, GetCurrentProcessId());
    if (!h->ready || !h->go || !t_spawn_child("display_monitors_dpi_child", args, 0, NULL, NULL, NULL, FALSE, NULL, &h->pi)) return 0;
    if (WaitForSingleObject(h->ready, 15000) != WAIT_OBJECT_0) return 0;
    h->ok = 1;
    return 1;
}
static void hold_stop(struct hold *h) {
    if (h->go) SetEvent(h->go);
    if (h->pi.hProcess) {
        DWORD code = 0;
        if (WaitForSingleObject(h->pi.hProcess, 10000) != WAIT_OBJECT_0) TerminateProcess(h->pi.hProcess, 1);
        else { GetExitCodeProcess(h->pi.hProcess, &code); CHECKF(code == 0, "hold child exit code %lu", code); }
        CloseHandle(h->pi.hProcess); CloseHandle(h->pi.hThread);
    }
    if (h->ready) CloseHandle(h->ready);
    if (h->go) CloseHandle(h->go);
}
#define NEED_HOLD(mode) struct hold H; do { NEED_GUI(); NEED_NT(NtUserGetProcessDpiAwarenessContext); NEED_NT(NtUserGetSystemDpiForProcess); \
    if (t_is_wine()) { t_skip("Wine lacks the process dpi syscalls"); return; } if (dm_wine_no_child()) return; \
    if (!hold_start(&H, mode)) { hold_stop(&H); t_skip("could not start the %s helper", mode); return; } } while (0)

TEST(display_monitors_dpi_xproc_child_without_win32k_has_no_dpi_info) {
    NEED_HOLD("none");
    SetLastError(0);
    DWORD ctx = UN(NtUserGetProcessDpiAwarenessContext)(H.pi.hProcess);
    /* Windows 10: a process that never loaded user32 reports the default unaware context (0x6010), like a connected unaware one */
    CHECKF(ctx == DM_RAW_UNAWARE, "context 0x%lx for a process that never touched win32k", ctx);
    SetLastError(0);
    DWORD dpi = UN(NtUserGetSystemDpiForProcess)(H.pi.hProcess) & 0xffff;
    CHECK(dpi == 0);
    EXPECT_ERR(0);                                                                            /* no error is raised for the missing win32 process */
    hold_stop(&H);
}

TEST(display_monitors_dpi_xproc_connected_unaware_child_reports_6010) {
    NEED_HOLD("unaware");
    DWORD ctx = UN(NtUserGetProcessDpiAwarenessContext)(H.pi.hProcess);
    CHECKF(ctx == DM_RAW_UNAWARE || (ctx & 0xf) == 0, "context 0x%lx", ctx);                  /* ppi+0x118 == 0 is reported as 0x6010 */
    CHECK((ctx & 0xf) == 0);
    hold_stop(&H);
}

TEST(display_monitors_dpi_xproc_child_system_dpi_equals_ours) {
    NEED_HOLD("unaware");
    NEED_NT(NtUserGetDpiForCurrentProcess);
    DWORD theirs = UN(NtUserGetSystemDpiForProcess)(H.pi.hProcess) & 0xffff;
    CHECKF(theirs == (UN(NtUserGetDpiForCurrentProcess)() & 0xffff), "child %lu, us %u", theirs, UN(NtUserGetDpiForCurrentProcess)() & 0xffff);
    hold_stop(&H);
}

TEST(display_monitors_dpi_xproc_per_monitor_v2_child) {
    NEED_HOLD("pmv2");
    DWORD ctx = UN(NtUserGetProcessDpiAwarenessContext)(H.pi.hProcess);
    CHECKF((ctx & 0xdfffffff) == DM_RAW_PMONITOR2, "context 0x%lx", ctx);
    hold_stop(&H);
}

TEST(display_monitors_dpi_xproc_per_monitor_v1_child) {
    NEED_HOLD("pm");
    DWORD ctx = UN(NtUserGetProcessDpiAwarenessContext)(H.pi.hProcess);
    CHECKF((ctx & 0xdfffffff) == DM_RAW_PMONITOR, "context 0x%lx", ctx);
    hold_stop(&H);
}

TEST(display_monitors_dpi_xproc_system_aware_child_carries_its_dpi) {
    NEED_HOLD("system");
    DWORD ctx = UN(NtUserGetProcessDpiAwarenessContext)(H.pi.hProcess);
    DWORD dpi = UN(NtUserGetSystemDpiForProcess)(H.pi.hProcess) & 0xffff;
    CHECKF(ctx == DM_RAW_SYSTEM(dpi), "context 0x%lx, child system dpi %lu", ctx, dpi);
    hold_stop(&H);
}

TEST(display_monitors_dpi_xproc_explicit_unaware_child) {
    NEED_HOLD("unaware_explicit");
    DWORD ctx = UN(NtUserGetProcessDpiAwarenessContext)(H.pi.hProcess);
    CHECKF((ctx & 0xdfffffff) == DM_RAW_UNAWARE, "context 0x%lx", ctx);
    hold_stop(&H);
}

TEST(display_monitors_dpi_xproc_thread_override_in_child_is_invisible_from_outside) {
    NEED_HOLD("threadovr");
    DWORD ctx = UN(NtUserGetProcessDpiAwarenessContext)(H.pi.hProcess);
    CHECKF((ctx & 0xf) == 0, "context 0x%lx: only the process-level word (ppi+0x118) is visible to other processes", ctx);
    hold_stop(&H);
}

TEST(display_monitors_dpi_xproc_user32_exports_match_the_syscalls) {
    NEED_HOLD("pmv2");
    DWORD a = UN(NtUserGetProcessDpiAwarenessContext)(H.pi.hProcess);
    DWORD b = UN(GetDpiAwarenessContextForProcess) ? DM_LO(UN(GetDpiAwarenessContextForProcess)(H.pi.hProcess)) : a;
    DWORD c = UN(NtUserGetSystemDpiForProcess)(H.pi.hProcess) & 0xffff;
    DWORD d = U(GetSystemDpiForProcess) ? U(GetSystemDpiForProcess)(H.pi.hProcess) & 0xffff : c;
    CHECK(a == b);
    CHECK(c == d);
    hold_stop(&H);
}

TEST(display_monitors_dpi_xproc_limited_query_right_is_enough_and_synchronize_is_not) {
    NEED_HOLD("pmv2");
    HANDLE lim = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, H.pi.dwProcessId);
    HANDLE syn = OpenProcess(SYNCHRONIZE, FALSE, H.pi.dwProcessId);
    if (lim) CHECK((UN(NtUserGetProcessDpiAwarenessContext)(lim) & 0xdfffffff) == DM_RAW_PMONITOR2);
    if (syn) {
        SetLastError(0);
        CHECK(UN(NtUserGetProcessDpiAwarenessContext)(syn) == 0);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
        SetLastError(0);
        CHECK((UN(NtUserGetSystemDpiForProcess)(syn) & 0xffff) == 0);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    } else t_note("OpenProcess(SYNCHRONIZE) failed: %lu", GetLastError());
    if (lim) CloseHandle(lim);
    if (syn) CloseHandle(syn);
    hold_stop(&H);
}

TEST(display_monitors_dpi_xproc_exited_child_handle_has_no_win32_process) {
    NEED_HOLD("pmv2");
    HANDLE keep = NULL;
    DuplicateHandle(GetCurrentProcess(), H.pi.hProcess, GetCurrentProcess(), &keep, 0, FALSE, DUPLICATE_SAME_ACCESS);
    REQUIRE(keep);
    SetEvent(H.go);
    WaitForSingleObject(keep, 10000);
    SetLastError(0);
    DWORD ctx = UN(NtUserGetProcessDpiAwarenessContext)(keep);
    DWORD dpi = UN(NtUserGetSystemDpiForProcess)(keep) & 0xffff;
    t_note("exited child: ctx 0x%lx dpi %lu error %lu", ctx, dpi, GetLastError());
    CHECK(ctx == 0 || (ctx & 0xf) == 2);              /* either the win32 process is already gone (0) or still tearing down (its last value) */
    CloseHandle(keep);
    hold_stop(&H);
}
