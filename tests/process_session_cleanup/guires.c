/* process_session_cleanup / guires: GetGuiResources(hProcess, flags) on OTHER processes.
 *
 * NtUserGetGuiResources : flags > 4 -> ERROR_INVALID_PARAMETER; (HANDLE)-2 -> session
 * totals (session-wide handle count and peak, flag 3 -> 0, no error);
 * (HANDLE)-1 -> current process; any other handle -> ObReferenceObjectByHandle(h, 0x1000, PsProcessType)
 * and PsGetProcessWin32Process of the target. Every failure (denied, wrong type, bad handle, dead
 * process, suspended process that never ran, other session) is ERROR_INVALID_PARAMETER and result 0; a running
 * process that made no win32k call is answered like any other (no error).
 * Counters: GDI objects, GDI peak, USER objects, USER peak.
 */
#include "psc.h"

#define NEED_UI() do { REQUIRE(U(GetGuiResources)); REQUIRE(U(CreateWindowExA)); \
    HWND probe_ = psc_mk(NULL, "probe"); if (!probe_) { t_skip("cannot create a window (no display driver)"); return; } U(DestroyWindow)(probe_); } while (0)
#define NEED_GR() do { REQUIRE(U(GetGuiResources)); REQUIRE(U(CreateSolidBrush)); } while (0)

/* ---- child scenarios -------------------------------------------------------------------------- */
static void report_counts(struct psc_cx *x, int base) {
    x->s->v[base + 0] = psc_gr(PSC_SELF, PSC_GDI);
    x->s->v[base + 1] = psc_gr(PSC_SELF, PSC_USER);
    x->s->v[base + 2] = psc_gr(PSC_SELF, PSC_GDI_PEAK);
    x->s->v[base + 3] = psc_gr(PSC_SELF, PSC_USER_PEAK);
}
static void child_warm(void) {
    HBRUSH b = U(CreateSolidBrush)(0);
    HWND w = psc_mk(NULL, "w");
    if (b) U(DeleteObject)(b);
    if (w) U(DestroyWindow)(w);
}

SCEN(idle) {
    child_warm();
    HBRUSH b[3]; HWND w[2];
    for (int i = 0; i < 3; i++) b[i] = U(CreateSolidBrush)(RGB(i, i, i));
    for (int i = 0; i < 2; i++) w[i] = psc_mk(NULL, "idle");
    report_counts(x, 0);
    x->s->v[8] = (LONGLONG)(ULONG_PTR)w[0];
    psc_sync(x);
}

/* extra = "<kind> <count>": create count objects, report, delete them, report; the parent samples at each sync */
SCEN(gdi_n) {
    int kind = 0, n = 0;
    sscanf(x->extra, "%d %d", &kind, &n);
    HGDIOBJ *h = (HGDIOBJ *)calloc((size_t)n, sizeof *h);
    struct psc_drain dr; memset(&dr, 0, sizeof dr);
    child_warm();
    if (psc_kind_cached(kind)) psc_drain_take(&dr);      /* later creates of the kind are fresh handles, not cache hits */
    report_counts(x, 0);
    if (!psc_sync(x)) return;
    for (int i = 0; i < n; i++) { h[i] = psc_mk_kind(kind); CC(h[i] != NULL); }
    report_counts(x, 4);
    if (!psc_sync(x)) return;
    for (int i = 0; i < n; i++) if (h[i]) CC(psc_del_kind(kind, h[i]));
    report_counts(x, 8);
    psc_sync(x);
    psc_drain_give(&dr);
    free(h);
}

/* extra = "<mode> <count>": 0 top-level popups, 1 children of one parent, 2 owned popups of one owner */
SCEN(user_n) {
    int mode = 0, n = 0;
    sscanf(x->extra, "%d %d", &mode, &n);
    HWND *h = (HWND *)calloc((size_t)n, sizeof *h);
    x->s->phase = 1;
    child_warm();
    x->s->phase = 2;
    /* the first top-level window of a thread brings a hidden helper window of its own (+1 USER object); keep one window
     * alive from the start so that the counts below only see the windows created by the loop */
    HWND top = psc_mk(NULL, "top");

    x->s->phase = 3;
    report_counts(x, 0);
    if (!psc_sync(x)) return;
    for (int i = 0; i < n; i++) {
        h[i] = mode == 1 ? psc_mk(top, "c") : mode == 2 ? psc_mk_owned(top, "o") : psc_mk(NULL, "t");
        CC(h[i] != NULL);
    }
    report_counts(x, 4);
    if (!psc_sync(x)) return;
    for (int i = n - 1; i >= 0; i--) if (h[i]) CC(U(DestroyWindow)(h[i]));
    report_counts(x, 8);
    psc_sync(x);
    if (top) U(DestroyWindow)(top);
    free(h);
}

SCEN(nogui) {            /* never makes a win32k call */
    psc_sync(x);
}

/* extra = "gdi" | "user" | "win" | "stock": the very first win32k-facing call a pristine process makes */
SCEN(first_call) {
    psc_sync(x);                                   /* S0: still pristine */
    if (!strcmp(x->extra, "gdi")) { HRGN r = U(CreateRectRgn)(0, 0, 1, 1); CC(r != NULL); }
    else if (!strcmp(x->extra, "user")) { CC(U(GetSystemMetrics)(SM_CXSCREEN) > 0); }
    else if (!strcmp(x->extra, "win")) { HWND w = psc_mk(NULL, "fc"); CC(w != NULL); }
    else if (!strcmp(x->extra, "stock")) { CC(U(GetStockObject)(BLACK_PEN) != NULL); }
    report_counts(x, 0);
    psc_sync(x);                                   /* S1: connected */
}

/* ---- parent helpers ----------------------------------------------------------------------------- */
static HANDLE open_with(DWORD pid, DWORD access) { return OpenProcess(access, FALSE, pid); }

/* ---- 1. counts of an idle child equal what the child sees for itself --------------------------- */
TEST(process_session_cleanup_gr_child_matches_self_view) {
    NEED_GR();
    struct psc_ch c;
    PSC_START(c, "idle", "");
    static const DWORD flags[4] = { PSC_GDI, PSC_USER, PSC_GDI_PEAK, PSC_USER_PEAK };
    for (int i = 0; i < 4; i++) {
        SetLastError(PSC_MAGIC);
        DWORD v = psc_gr(c.hp, flags[i]);
        /* the handle path reads the same per-process GUI counters as the (HANDLE)-1 path in the child */
        CHECKF(v == (DWORD)c.s->v[i], "flag %lu: parent sees %lu, child sees %llu", flags[i], v, (unsigned long long)c.s->v[i]);
        EXPECT_ERR(PSC_MAGIC);   /* success never touches the last error */
    }
    PSC_END(c); psc_free(&c);
}
TEST(process_session_cleanup_gr_child_counts_nonzero) {
    NEED_GR();
    struct psc_ch c;
    PSC_START(c, "idle", "");
    CHECK(psc_gr(c.hp, PSC_GDI) >= 3);          /* three brushes were created and kept */
    CHECK(psc_gr(c.hp, PSC_USER) >= 2);         /* two windows */
    PSC_END(c); psc_free(&c);
}
TEST(process_session_cleanup_gr_child_peak_ge_current) {
    NEED_GR();
    struct psc_ch c;
    PSC_START(c, "idle", "");
    /* handle allocation: peak = max(peak, count) */
    CHECK(psc_gr(c.hp, PSC_GDI_PEAK) >= psc_gr(c.hp, PSC_GDI));
    CHECK(psc_gr(c.hp, PSC_USER_PEAK) >= psc_gr(c.hp, PSC_USER));
    PSC_END(c); psc_free(&c);
}
TEST(process_session_cleanup_gr_nt_stub_matches_user32) {
    NEED_GR();
    REQUIRE(UN(NtUserGetGuiResources));
    if (t_is_wine()) { t_skip("Wine's win32u export is an abort stub"); return; }
    struct psc_ch c;
    PSC_START(c, "idle", "");
    static const DWORD flags[4] = { 0, 1, 2, 4 };
    for (int i = 0; i < 4; i++) CHECK(psc_gr_nt(c.hp, flags[i]) == psc_gr(c.hp, flags[i]));
    PSC_END(c); psc_free(&c);
}
TEST(process_session_cleanup_gr_win32u_stub_service_number) {
    FARPROC p = t_proc_in("win32u.dll", "NtUserGetGuiResources");
    REQUIRE(p);
    if (t_is_wine()) { t_skip("service numbers are Windows-specific"); return; }
    CHECK(k_stub_valid(p));
    EXPECT_EQ(k_stub_number(p), 0x140b);
}
TEST(process_session_cleanup_gr_same_via_second_handle) {
    NEED_GR();
    struct psc_ch c;
    PSC_START(c, "idle", "");
    HANDLE h2 = open_with(c.pid, PROCESS_QUERY_LIMITED_INFORMATION);
    REQUIRE(h2);
    for (DWORD f = 0; f <= 4; f += (f == 2 ? 2 : 1)) CHECK(psc_gr(h2, f) == psc_gr(c.hp, f));
    CloseHandle(h2);
    PSC_END(c); psc_free(&c);
}

/* ---- 2. access rights ------------------------------------------------------------------------- */
TEST(process_session_cleanup_gr_limited_query_access_suffices) {
    NEED_GR();
    struct psc_ch c;
    PSC_START(c, "idle", "");
    HANDLE h = open_with(c.pid, PROCESS_QUERY_LIMITED_INFORMATION);
    REQUIRE(h);
    SetLastError(PSC_MAGIC);
    CHECK(psc_gr(h, PSC_GDI) == (DWORD)c.s->v[0]);       /* ObReferenceObjectByHandle(h, 0x1000) */
    EXPECT_ERR(PSC_MAGIC);
    CloseHandle(h);
    PSC_END(c); psc_free(&c);
}
TEST(process_session_cleanup_gr_synchronize_only_is_denied_as_invalid_parameter) {
    NEED_GR();
    struct psc_ch c;
    PSC_START(c, "idle", "");
    HANDLE h = open_with(c.pid, SYNCHRONIZE);
    REQUIRE(h);
    SetLastError(PSC_MAGIC);
    DWORD v = psc_gr(h, PSC_GDI);
    DWORD e = GetLastError();
 /* the access failure is NOT mapped to ERROR_ACCESS_DENIED: the branch calls SetLastError(0x57) */
    CHECK(v == 0);
    CHECKF(e == ERROR_INVALID_PARAMETER, "error %lu", e);
    CloseHandle(h);
    PSC_END(c); psc_free(&c);
}
TEST(process_session_cleanup_gr_terminate_only_is_denied) {
    NEED_GR();
    struct psc_ch c;
    PSC_START(c, "idle", "");
    HANDLE h = open_with(c.pid, PROCESS_TERMINATE);
    REQUIRE(h);
    SetLastError(PSC_MAGIC);
    CHECK(psc_gr(h, PSC_GDI) == 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    CloseHandle(h);
    PSC_END(c); psc_free(&c);
}
TEST(process_session_cleanup_gr_query_information_without_limited_bit) {
    NEED_GR();
    struct psc_ch c;
    PSC_START(c, "idle", "");
    HANDLE h = open_with(c.pid, 0x0400 /* PROCESS_QUERY_INFORMATION only */);
    REQUIRE(h);
    SetLastError(PSC_MAGIC);
    DWORD v = psc_gr(h, PSC_GDI);
    DWORD e = GetLastError();
    /* the kernel requests exactly bit 0x1000; a handle granted only 0x400 does not carry it unless the OS added it to the mask */
    if (v == 0) CHECK(e == ERROR_INVALID_PARAMETER);
    else t_note("PROCESS_QUERY_INFORMATION handle also carried the limited bit (count %lu)", v);
    CloseHandle(h);
    PSC_END(c); psc_free(&c);
}
TEST(process_session_cleanup_gr_both_query_rights) {
    NEED_GR();
    struct psc_ch c;
    PSC_START(c, "idle", "");
    HANDLE h = open_with(c.pid, PROCESS_QUERY_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION);
    REQUIRE(h);
    CHECK(psc_gr(h, PSC_USER) == (DWORD)c.s->v[1]);
    CloseHandle(h);
    PSC_END(c); psc_free(&c);
}
TEST(process_session_cleanup_gr_duplicated_handle_with_reduced_access) {
    NEED_GR();
    struct psc_ch c;
    PSC_START(c, "idle", "");
    HANDLE d = NULL;
    REQUIRE(DuplicateHandle(GetCurrentProcess(), c.hp, GetCurrentProcess(), &d, SYNCHRONIZE, FALSE, 0));
    SetLastError(PSC_MAGIC);
    CHECK(psc_gr(d, PSC_GDI) == 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    CloseHandle(d);
    REQUIRE(DuplicateHandle(GetCurrentProcess(), c.hp, GetCurrentProcess(), &d, PROCESS_QUERY_LIMITED_INFORMATION, FALSE, 0));
    CHECK(psc_gr(d, PSC_GDI) == (DWORD)c.s->v[0]);
    CloseHandle(d);
    PSC_END(c); psc_free(&c);
}

/* ---- 3. flag validation with a real process handle ------------------------------------------ */
TEST(process_session_cleanup_gr_flag3_on_child_is_zero_without_error) {
    NEED_GR();
    struct psc_ch c;
    PSC_START(c, "idle", "");
    SetLastError(PSC_MAGIC);
    CHECK(psc_gr(c.hp, 3) == 0);       /* no counter matches 3: result 0 and the last error is left alone */
    EXPECT_ERR(PSC_MAGIC);
    PSC_END(c); psc_free(&c);
}
static void flag_invalid_child(DWORD flag) {
    NEED_GR();
    struct psc_ch c;
    PSC_START(c, "idle", "");
    SetLastError(PSC_MAGIC);
    DWORD v = psc_gr(c.hp, flag);
    CHECKF(v == 0, "flag 0x%lx -> %lu", flag, v);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    PSC_END(c); psc_free(&c);
}
#define FLAG_T(n, v) TEST(process_session_cleanup_gr_child_flag_##n##_invalid) { flag_invalid_child(v); }
FLAG_T(5, 5) FLAG_T(6, 6) FLAG_T(8, 8) FLAG_T(16, 16) FLAG_T(0x100, 0x100) FLAG_T(0x80000000, 0x80000000u) FLAG_T(0xffffffff, 0xffffffffu)

TEST(process_session_cleanup_gr_bad_flag_checked_before_handle) {
    NEED_GR();
    /* an invalid process handle AND an invalid flag: the flag test runs first, the result is the same 0x57 either way */
    SetLastError(PSC_MAGIC);
    CHECK(psc_gr((HANDLE)(ULONG_PTR)0x7654, 5) == 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}
TEST(process_session_cleanup_gr_bad_flag_with_session_handle) {
    NEED_GR();
    SetLastError(PSC_MAGIC);
    CHECK(psc_gr(PSC_SESSION, 5) == 0);   /* flag check precedes the -2 branch */
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}

/* ---- 4. handles that cannot name a Win32 process --------------------------------------------- */
TEST(process_session_cleanup_gr_null_handle) {
    NEED_GR();
    SetLastError(PSC_MAGIC);
    CHECK(psc_gr(NULL, PSC_GDI) == 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}
TEST(process_session_cleanup_gr_unopened_handle_value) {
    NEED_GR();
    static const ULONG_PTR vals[] = { 0x4, 0x7654, 0x1fffc, 0xfffc };
    for (size_t i = 0; i < sizeof vals / sizeof vals[0]; i++) {
        HANDLE probe = (HANDLE)vals[i];
        DWORD fl = 0;
        if (GetHandleInformation(probe, &fl)) continue;     /* skip values that happen to be open here */
        SetLastError(PSC_MAGIC);
        CHECKF(psc_gr(probe, PSC_GDI) == 0, "handle %p", probe);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
}
TEST(process_session_cleanup_gr_non_process_handles) {
    NEED_GR();
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    HANDLE mx = CreateMutexA(NULL, FALSE, NULL);
    HANDLE th = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentThreadId());
    HANDLE hs[3] = { ev, mx, th };
    static const char *const nm[3] = { "event", "mutex", "thread" };
    for (int i = 0; i < 3; i++) {
        if (!hs[i]) continue;
        SetLastError(PSC_MAGIC);
        /* ObReferenceObjectByHandle with PsProcessType fails with STATUS_OBJECT_TYPE_MISMATCH -> 0x57 */
        CHECKF(psc_gr(hs[i], PSC_GDI) == 0, "%s handle", nm[i]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
        CloseHandle(hs[i]);
    }
}
TEST(process_session_cleanup_gr_closed_handle) {
    NEED_GR();
    HANDLE h = CreateEventA(NULL, TRUE, FALSE, NULL);
    REQUIRE(h);
    CloseHandle(h);
    SetLastError(PSC_MAGIC);
    CHECK(psc_gr(h, PSC_GDI) == 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}
TEST(process_session_cleanup_gr_error_cleared_on_success_not_set) {
    NEED_GR();
    /* an error left by a previous failure is not cleared by a following success */
    psc_gr(NULL, PSC_GDI);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    DWORD v = psc_gr(PSC_SELF, PSC_GDI);
    (void)v;
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}

/* ---- 5. a process that has made no win32k call -------------------------------------------------- */
/* On Windows 10 a running process that never touched win32k already has a process block: GetGuiResources answers
 * for it (no error, no last-error change) instead of failing. A suspended child that never ran has none (below). */
TEST(process_session_cleanup_gr_process_without_gui_all_flags) {
    NEED_GR();
    struct psc_ch c;
    PSC_START(c, "nogui", "");
    SetLastError(PSC_MAGIC);
    DWORD g = psc_gr(c.hp, 0), u = psc_gr(c.hp, 1), gp = psc_gr(c.hp, 2), up = psc_gr(c.hp, 4);
    t_note("child without win32k activity: gdi %lu user %lu gdi peak %lu user peak %lu", g, u, gp, up);
    CHECK(g == 0);
    CHECK(gp == 0);
    CHECK(u <= 1 && up == u);        /* at most the one baseline USER object every process starts with */
    EXPECT_ERR(PSC_MAGIC);
    PSC_END(c); psc_free(&c);
}
TEST(process_session_cleanup_gr_process_without_gui_flag3) {
    NEED_GR();
    struct psc_ch c;
    PSC_START(c, "nogui", "");
    SetLastError(PSC_MAGIC);
    CHECK(psc_gr(c.hp, 3) == 0);
    EXPECT_ERR(PSC_MAGIC);          /* flag 3 is answered with 0 and no error, as for the pseudo handles */
    PSC_END(c); psc_free(&c);
}
TEST(process_session_cleanup_gr_suspended_child_has_no_win32_process) {
    NEED_GR();
    struct psc_ch c;
    if (!psc_spawn_ex(&c, "nogui", "", CREATE_SUSPENDED)) { t_skip("spawn"); psc_free(&c); return; }
    SetLastError(PSC_MAGIC);
    CHECK(psc_gr(c.hp, PSC_GDI) == 0);       /* the process object exists but has never run a win32k call */
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    TerminateProcess(c.hp, 0);
    WaitForSingleObject(c.hp, 5000);
    psc_free(&c);
}

/* the first win32k call makes the process visible to GetGuiResources */
static void first_call(const char *how) {
    NEED_GR();
    struct psc_ch c;
    PSC_START(c, "first_call", how);
    SetLastError(PSC_MAGIC);
    CHECK(psc_gr(c.hp, PSC_GDI) == 0);
    EXPECT_ERR(PSC_MAGIC);                      /* S0: pristine, but already a process block: no error */
    PSC_STEP(c);
    SetLastError(PSC_MAGIC);
    DWORD g = psc_gr(c.hp, PSC_GDI);
    EXPECT_ERR(PSC_MAGIC);                      /* S1: the process now has GUI process state */
    CHECKF(g == (DWORD)c.s->v[0], "parent %lu child %llu", g, (unsigned long long)c.s->v[0]);
    CHECK(psc_gr(c.hp, PSC_USER_PEAK) >= psc_gr(c.hp, PSC_USER));
    if (!strcmp(how, "gdi")) CHECK(g >= 1);
    PSC_END(c); psc_free(&c);
}
TEST(process_session_cleanup_gr_first_call_gdi) { first_call("gdi"); }
TEST(process_session_cleanup_gr_first_call_user) { first_call("user"); }
TEST(process_session_cleanup_gr_first_call_window) { first_call("win"); }
TEST(process_session_cleanup_gr_first_call_stock_object) { first_call("stock"); }

TEST(process_session_cleanup_gr_dead_process_handle) {
    NEED_GR();
    struct psc_ch c;
    PSC_START(c, "idle", "");
    CHECK(psc_gr(c.hp, PSC_GDI) > 0);
    PSC_END(c);
    SetLastError(PSC_MAGIC);
    /* after the process object's Win32Process was detached at exit: ERROR_INVALID_PARAMETER, count 0 */
    DWORD v = 1;
    int ok = PSC_POLL((v = psc_gr(c.hp, PSC_GDI)) == 0, 3000);
    CHECKF(ok, "count %lu after exit", v);
    SetLastError(PSC_MAGIC);
    CHECK(psc_gr(c.hp, PSC_USER) == 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    psc_free(&c);
}

/* ---- 6. session wide (HANDLE)-2 ---------------------------------------------------------------- */
TEST(process_session_cleanup_gr_session_totals_cover_self) {
    NEED_GR();
    psc_warm();
    DWORD own_gdi = psc_gr(PSC_SELF, PSC_GDI), own_user = psc_gr(PSC_SELF, PSC_USER);
    DWORD all_gdi = psc_gr(PSC_SESSION, PSC_GDI), all_user = psc_gr(PSC_SESSION, PSC_USER);
    SetLastError(PSC_MAGIC);
    /* the session handle count counts every live handle of the session, ours included */
    CHECKF(all_gdi >= own_gdi, "session %lu own %lu", all_gdi, own_gdi);
    CHECKF(all_user >= own_user, "session %lu own %lu", all_user, own_user);
    EXPECT_ERR(PSC_MAGIC);
}
TEST(process_session_cleanup_gr_session_peaks) {
    NEED_GR();
    DWORD c1 = psc_gr(PSC_SESSION, PSC_GDI);
    DWORD p1 = psc_gr(PSC_SESSION, PSC_GDI_PEAK);
    DWORD u1 = psc_gr(PSC_SESSION, PSC_USER);
    DWORD up1 = psc_gr(PSC_SESSION, PSC_USER_PEAK);
    /* the peaks are read later than the counts and never decrease */
    CHECK(p1 >= c1);
    CHECK(up1 >= u1);
}
TEST(process_session_cleanup_gr_session_flag3) {
    NEED_GR();
    SetLastError(PSC_MAGIC);
    CHECK(psc_gr(PSC_SESSION, 3) == 0);
    EXPECT_ERR(PSC_MAGIC);
}
TEST(process_session_cleanup_gr_session_peak_monotonic_across_child) {
    NEED_GR();
    DWORD before = psc_gr(PSC_SESSION, PSC_GDI_PEAK);
    struct psc_ch c;
    char ex[32]; sprintf(ex, "%d %d", PK_BRUSH, 40);
    PSC_START(c, "gdi_n", ex);
    PSC_STEP(c);
    PSC_STEP(c);
    PSC_END(c); psc_free(&c);
    CHECK(psc_gr(PSC_SESSION, PSC_GDI_PEAK) >= before);
}
TEST(process_session_cleanup_gr_session_total_grows_with_child_objects) {
    NEED_GR();
    struct psc_ch c;
    char ex[32]; sprintf(ex, "%d %d", PK_BRUSH, 300);
    PSC_START(c, "gdi_n", ex);
    DWORD during_base = psc_gr(PSC_SESSION, PSC_GDI_PEAK);
    PSC_STEP(c);                       /* 300 brushes alive in the child */
    DWORD child_now = psc_gr(c.hp, PSC_GDI);
    DWORD sess_peak = psc_gr(PSC_SESSION, PSC_GDI_PEAK);
    /* the session peak is >= any single process's count: the session peak counters are session-wide high-water marks */
    CHECKF(sess_peak >= child_now, "session peak %lu child %lu", sess_peak, child_now);
    CHECK(sess_peak >= during_base);
    PSC_STEP(c);
    PSC_END(c); psc_free(&c);
}
TEST(process_session_cleanup_gr_self_pseudo_vs_real_handle) {
    NEED_GR();
    psc_warm();
    HANDLE me = open_with(GetCurrentProcessId(), PROCESS_QUERY_LIMITED_INFORMATION);
    REQUIRE(me);
    static const DWORD flags[4] = { 0, 1, 2, 4 };
    for (int i = 0; i < 4; i++) CHECKF(psc_gr(me, flags[i]) == psc_gr(PSC_SELF, flags[i]), "flag %lu", flags[i]);
    CloseHandle(me);
}
TEST(process_session_cleanup_gr_current_process_pseudo_handle_is_minus_one) {
    NEED_GR();
    psc_warm();
    CHECK(GetCurrentProcess() == PSC_SELF);
    CHECK(psc_gr(GetCurrentProcess(), PSC_GDI) == psc_gr(PSC_SELF, PSC_GDI));
}
TEST(process_session_cleanup_gr_self_counts_track_own_objects) {
    NEED_GR();
    psc_warm();
    struct psc_drain dr; psc_drain_take(&dr);
    DWORD g0 = psc_gr(PSC_SELF, PSC_GDI), p0 = psc_gr(PSC_SELF, PSC_GDI_PEAK);
    HBRUSH b[10];
    for (int i = 0; i < 10; i++) b[i] = U(CreateSolidBrush)(RGB(i, 0, 0));
    CHECK(psc_gr(PSC_SELF, PSC_GDI) == g0 + 10);
    CHECK(psc_gr(PSC_SELF, PSC_GDI_PEAK) >= g0 + 10);
    for (int i = 0; i < 10; i++) U(DeleteObject)(b[i]);
    { DWORD g1 = psc_gr(PSC_SELF, PSC_GDI); CHECKF(g1 >= g0 && g1 - g0 <= PSC_CACHE_MAX, "after delete %lu, before %lu", g1, g0); }
    CHECK(psc_gr(PSC_SELF, PSC_GDI_PEAK) >= p0);
    psc_drain_give(&dr);
}

/* ---- 7. counts before / during / after object creation in the child ----------------------------- */
static void gr_gdi_n(int kind, int n) {
    NEED_GR();
    struct psc_ch c;
    char ex[32]; sprintf(ex, "%d %d", kind, n);
    PSC_START(c, "gdi_n", ex);
    DWORD b0 = psc_gr(c.hp, PSC_GDI), bp = psc_gr(c.hp, PSC_GDI_PEAK), bu = psc_gr(c.hp, PSC_USER);
    CHECK(b0 == (DWORD)c.s->v[0]);
    PSC_STEP(c);
    DWORD a0 = psc_gr(c.hp, PSC_GDI), ap = psc_gr(c.hp, PSC_GDI_PEAK), au = psc_gr(c.hp, PSC_USER);
    PSC_STEP(c);
    DWORD d0 = psc_gr(c.hp, PSC_GDI), dp = psc_gr(c.hp, PSC_GDI_PEAK), du = psc_gr(c.hp, PSC_USER);
    CHECKF(a0 == (DWORD)c.s->v[4], "parent %lu child %llu", a0, (unsigned long long)c.s->v[4]);
    CHECKF(d0 == (DWORD)c.s->v[8], "parent %lu child %llu", d0, (unsigned long long)c.s->v[8]);
    if (psc_kind_exact[kind]) {
        /* the count rises by one per object and falls by one per delete */
        CHECKF(a0 == b0 + (DWORD)n, "%s x%d: %lu -> %lu", psc_kind_name[kind], n, b0, a0);
        if (psc_kind_cached(kind)) CHECKF(d0 >= b0 && d0 - b0 <= PSC_CACHE_MAX, "after delete %lu, before %lu (the handle cache keeps a few)", d0, b0);
        else CHECKF(d0 == b0, "after delete %lu, before %lu", d0, b0);
    } else {
        CHECKF(a0 >= b0 + (DWORD)n, "%s x%d: %lu -> %lu", psc_kind_name[kind], n, b0, a0);
        CHECKF(d0 <= a0 - (DWORD)n, "after delete %lu, created %lu", d0, a0);
        t_note("%s x%d: GDI count delta %ld, after delete %ld", psc_kind_name[kind], n, (long)(a0 - b0), (long)(d0 - b0));
    }
    CHECK(ap >= a0);
    CHECK(ap >= bp);
    CHECKF(dp == ap, "deleting objects must not lower or raise the peak: %lu vs %lu", dp, ap);
    CHECK(dp >= d0);
    CHECKF((au == bu && du == bu) || kind == PK_DC || kind == PK_FONT, "USER count moved %lu -> %lu -> %lu while only GDI objects were created", bu, au, du);
    PSC_END(c); psc_free(&c);
}
#define GDI_T(nm, kind, n) TEST(process_session_cleanup_gr_gdi_##nm##_##n) { gr_gdi_n(kind, n); }
GDI_T(brush, PK_BRUSH, 1) GDI_T(brush, PK_BRUSH, 7) GDI_T(brush, PK_BRUSH, 64) GDI_T(brush, PK_BRUSH, 500)
GDI_T(pen, PK_PEN, 1) GDI_T(pen, PK_PEN, 32)
GDI_T(rgn, PK_RGN, 1) GDI_T(rgn, PK_RGN, 32) GDI_T(rgn, PK_RGN, 200)
GDI_T(bitmap, PK_BITMAP, 1) GDI_T(bitmap, PK_BITMAP, 32)
GDI_T(palette, PK_PALETTE, 1) GDI_T(palette, PK_PALETTE, 16)
GDI_T(dc, PK_DC, 1) GDI_T(dc, PK_DC, 16)
GDI_T(font, PK_FONT, 1) GDI_T(font, PK_FONT, 16)
GDI_T(dib, PK_DIB, 1) GDI_T(dib, PK_DIB, 16)

static void gr_user_n(int mode, int n) {
    NEED_UI();
    struct psc_ch c;
    char ex[32]; sprintf(ex, "%d %d", mode, n);
    PSC_START(c, "user_n", ex);
    DWORD b0 = psc_gr(c.hp, PSC_USER), bp = psc_gr(c.hp, PSC_USER_PEAK), bg = psc_gr(c.hp, PSC_GDI);
    PSC_STEP(c);
    DWORD a0 = psc_gr(c.hp, PSC_USER), ap = psc_gr(c.hp, PSC_USER_PEAK), ag = psc_gr(c.hp, PSC_GDI);
    PSC_STEP(c);
    DWORD d0 = psc_gr(c.hp, PSC_USER), dp = psc_gr(c.hp, PSC_USER_PEAK);
    CHECKF(a0 == (DWORD)c.s->v[5], "parent %lu child %llu", a0, (unsigned long long)c.s->v[5]);
    /* every window is one thread-owned USER object -> the process USER-object count rises by 1 */
    CHECKF(a0 == b0 + (DWORD)n, "mode %d x%d: %lu -> %lu", mode, n, b0, a0);
    /* each DestroyWindow gives its handle back */
    CHECKF(d0 == b0, "after destroy %lu, before %lu", d0, b0);
    CHECK(ap >= a0 && ap >= bp);
    CHECKF(dp == ap, "peak %lu -> %lu across destroy", ap, dp);
    if (ag != bg) t_note("mode %d x%d: GDI count moved %lu -> %lu while creating windows", mode, n, bg, ag);
    PSC_END(c); psc_free(&c);
}
#define USER_T(nm, mode, n) TEST(process_session_cleanup_gr_user_##nm##_##n) { gr_user_n(mode, n); }
USER_T(toplevel, 0, 1) USER_T(toplevel, 0, 10) USER_T(toplevel, 0, 100) USER_T(toplevel, 0, 400)
USER_T(children, 1, 1) USER_T(children, 1, 50)
USER_T(owned, 2, 1) USER_T(owned, 2, 20)
