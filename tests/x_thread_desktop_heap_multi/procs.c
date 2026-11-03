/* x_thread_desktop_heap_multi / procs: process-level checks asked from different threads and processes.
 *
 * NtUserCheckProcessSession is for the compositor process: from every thread of every other process it answers FALSE with
 * ERROR_ACCESS_DENIED whatever the process id. NtUserNotifyProcessCreate with a non-zero flags word is for the session manager
 * of the subsystem only: STATUS_ACCESS_DENIED from everyone else. NtUserCheckAccessForIntegrityLevel(a, b, &out) accepts a
 * first process only when it is the caller's own (or the caller is the subsystem process), needs both to be processes of this
 * session that use win32k, and stores 1 when the first may access the second (equal integrity and ui-access words give 1).
 * NtUserDisableProcessWindowFiltering and NtUserClearForeground depend on the process, not on the thread or the call before.
 * Process-wide switches are only flipped in child processes.
 */
#include "x.h"

struct pa { DWORD v[16]; DWORD pid; };

/* ---- CheckProcessSession ---- */
static void job_cps(struct xt_wk *w, void *p) {
    struct pa *a = p;
    static const DWORD args[] = { 0, 4, 0x1234, 0xFFFFFFFFu };
    for (int i = 0; i < 4; i++) { SET_ERR(SENT); a->v[i] = (DWORD)R_D(UN(NtUserCheckProcessSession)(args[i])); a->v[8 + i] = GetLastError(); }
    SET_ERR(SENT); a->v[4] = R_D(UN(NtUserCheckProcessSession)(a->pid)); a->v[12] = GetLastError();
}
XT(check_process_session_is_denied_on_a_worker_for_any_pid) {
    NEED(NtUserCheckProcessSession);
    struct xt_wk w; struct pa a; a.pid = GetCurrentProcessId();
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_cps, &a));
    for (int i = 0; i < 5; i++) { EXPECT_EQ(a.v[i], 0); EXPECT_EQ(a.v[8 + i], E_ACCESS_DENIED); }
    xt_wk_stop(&w);
}
static void fresh_cps(void *p) {
    DWORD *r = p;
    SET_ERR(SENT); r[0] = R_D(UN(NtUserCheckProcessSession)(GetCurrentProcessId())); r[1] = GetLastError();
}
XT(check_process_session_is_denied_on_a_fresh_thread_as_its_first_call) {
    NEED(NtUserCheckProcessSession);
    DWORD r[2] = { 9, 9 };
    CHECK(xt_on_thread(fresh_cps, r));
    EXPECT_EQ(r[0], 0); EXPECT_EQ(r[1], E_ACCESS_DENIED);
}
XC(cps) {
    DWORD pids[] = { 0, GetCurrentProcessId(), (DWORD)s->v[0], 4 };
    for (int i = 0; i < 4; i++) {
        SET_ERR(SENT);
        CK_EQ(R_D(UN(NtUserCheckProcessSession)(pids[i])), 0);
        CK_EQ(GetLastError(), E_ACCESS_DENIED);
    }
    return 0;
}
XT(check_process_session_is_denied_in_a_child_process_for_parent_self_and_system) {
    NEED(NtUserCheckProcessSession);
    HANDLE hm; const char *tag = xt_tag(); struct xt_shm *s = xt_shm_create(tag, &hm); CHECK(s);
    s->v[0] = GetCurrentProcessId();
    PROCESS_INFORMATION pi; HANDLE pr = xt_spawn("cps", tag, NULL, &pi); CHECK(pr);
    EXPECT_EQ(pr ? xt_reap(pr, &pi, 60000) : 1, 0);
    UnmapViewOfFile((void *)s); CloseHandle(hm);
}
XT(check_process_session_denial_does_not_change_between_calls) {
    NEED(NtUserCheckProcessSession);
    int bad = 0;
    for (int i = 0; i < 50; i++) { SET_ERR(SENT); if (R_D(UN(NtUserCheckProcessSession)(i)) != 0 || GetLastError() != E_ACCESS_DENIED) bad++; }
    EXPECT_EQ(bad, 0);
}

/* ---- CheckAccessForIntegrityLevel ---- */
static void job_cafil(struct xt_wk *w, void *p) {
    struct pa *a = p; DWORD out = 0xCCCCCCCC;
    a->v[0] = (DWORD)UN(NtUserCheckAccessForIntegrityLevel)(a->pid, a->pid, &out);
    a->v[1] = out;
}
XT(integrity_check_of_the_own_process_against_itself_is_allowed_from_a_worker) {
    NEED(NtUserCheckAccessForIntegrityLevel);
    struct xt_wk w; struct pa a; a.pid = GetCurrentProcessId();
    HWND h = xt_window(); CHECK(h);                        /* the process uses win32k */
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_cafil, &a));
    EXPECT_EQ(a.v[0], 0); EXPECT_EQ(a.v[1], 1);
    xt_wk_stop(&w);
    xt_destroy(h);
}
XT(integrity_check_answers_alike_on_the_main_thread_and_a_worker) {
    NEED(NtUserCheckAccessForIntegrityLevel);
    struct xt_wk w; struct pa a; a.pid = GetCurrentProcessId();
    DWORD out = 0xCCCCCCCC;
    HWND h = xt_window(); CHECK(h);
    LONG st = UN(NtUserCheckAccessForIntegrityLevel)(a.pid, a.pid, &out);
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_cafil, &a));
    EXPECT_EQ((DWORD)st, a.v[0]); EXPECT_EQ(out, a.v[1]);
    xt_wk_stop(&w);
    xt_destroy(h);
}
static void fresh_cafil(void *p) {
    DWORD *r = p, out = 0xCCCCCCCC;
    r[0] = (DWORD)UN(NtUserCheckAccessForIntegrityLevel)(GetCurrentProcessId(), GetCurrentProcessId(), &out);
    r[1] = out;
}
XT(integrity_check_works_as_the_first_call_of_a_fresh_thread) {
    NEED(NtUserCheckAccessForIntegrityLevel);
    DWORD r[2] = { 9, 9 };
    HWND h = xt_window(); CHECK(h);
    CHECK(xt_on_thread(fresh_cafil, r));
    EXPECT_EQ(r[0], 0); EXPECT_EQ(r[1], 1);
    xt_destroy(h);
}
XC(cafil_child) {
    DWORD parent = (DWORD)s->v[0], self = GetCurrentProcessId(), out;
    HWND own = xt_window(); CK(own != NULL);               /* this process now uses win32k */
    out = 0xCCCCCCCC; LONG a = UN(NtUserCheckAccessForIntegrityLevel)(self, parent, &out);
    s->v[1] = (DWORD)a; s->v[2] = out;
    out = 0xCCCCCCCC; LONG b = UN(NtUserCheckAccessForIntegrityLevel)(parent, self, &out);
    s->v[3] = (DWORD)b; s->v[4] = out;
    out = 0xCCCCCCCC; LONG c = UN(NtUserCheckAccessForIntegrityLevel)(self, self, &out);
    s->v[5] = (DWORD)c; s->v[6] = out;
    xt_destroy(own);
    s->done = 1;
    return 0;
}
XT(integrity_check_in_a_child_allows_child_against_parent_and_denies_parent_against_child) {
    NEED(NtUserCheckAccessForIntegrityLevel);
    HWND h = xt_window(); CHECK(h);
    HANDLE hm; const char *tag = xt_tag(); struct xt_shm *s = xt_shm_create(tag, &hm); CHECK(s);
    s->v[0] = GetCurrentProcessId();
    PROCESS_INFORMATION pi; HANDLE pr = xt_spawn("cafil_child", tag, NULL, &pi); CHECK(pr);
    EXPECT_EQ(pr ? xt_reap(pr, &pi, 60000) : 1, 0);
    EXPECT_EQ((DWORD)s->v[1], 0);                      /* child -> parent: first is the caller's own process */
    EXPECT_EQ((DWORD)s->v[2], 1);                      /* same integrity and ui-access word: allowed */
    EXPECT_EQ((DWORD)s->v[3], ST_ACCESS_DENIED);       /* parent -> child: the first process is not the caller's */
    EXPECT_EQ((DWORD)s->v[4], 0xCCCCCCCCu);
    EXPECT_EQ((DWORD)s->v[5], 0); EXPECT_EQ((DWORD)s->v[6], 1);
    UnmapViewOfFile((void *)s); CloseHandle(hm);
    xt_destroy(h);
}
XT(integrity_check_of_a_foreign_first_process_leaves_out_untouched_from_a_worker) {
    NEED(NtUserCheckAccessForIntegrityLevel);
    struct xt_wk w; struct pa a;
    HWND h = xt_window(); CHECK(h);
    HANDLE hm; const char *tag = xt_tag(); struct xt_shm *s = xt_shm_create(tag, &hm); CHECK(s);
    s->v[0] = GetCurrentProcessId();
    PROCESS_INFORMATION pi; HANDLE pr = xt_spawn("cafil_child", tag, NULL, &pi); CHECK(pr);
    EXPECT_EQ(pr ? xt_reap(pr, &pi, 60000) : 1, 0);
    UnmapViewOfFile((void *)s); CloseHandle(hm);
    CHECK(xt_wk_start(&w, 1));
    a.pid = 0x7FFFFFF0;
    CHECK(xt_wk_run(&w, job_cafil, &a));
    EXPECT_EQ(a.v[0], ST_INVALID_PARAMETER);
    EXPECT_EQ(a.v[1], 0xCCCCCCCCu);
    xt_wk_stop(&w);
    xt_destroy(h);
}
XC(idle) {
    xt_wait(&s->go, 60000);
    return 0;
}
XT(integrity_check_against_a_child_that_never_used_win32k_is_invalid_parameter) {
    NEED(NtUserCheckAccessForIntegrityLevel);
    HWND h = xt_window(); CHECK(h);
    HANDLE hm; const char *tag = xt_tag(); struct xt_shm *s = xt_shm_create(tag, &hm); CHECK(s);
    PROCESS_INFORMATION pi; HANDLE pr = xt_spawn("idle", tag, NULL, &pi); CHECK(pr);
    DWORD out = 0xCCCCCCCC;
    LONG st = pr ? UN(NtUserCheckAccessForIntegrityLevel)(GetCurrentProcessId(), pi.dwProcessId, &out) : 0;
    EXPECT_EQ((DWORD)st, ST_INVALID_PARAMETER);        /* the process has no win32k process structure yet */
    EXPECT_EQ(out, 0xCCCCCCCCu);
    s->go = 1;
    if (pr) xt_reap(pr, &pi, 60000);
    UnmapViewOfFile((void *)s); CloseHandle(hm);
    xt_destroy(h);
}
XT(integrity_check_against_a_finished_child_is_invalid_parameter) {
    NEED(NtUserCheckAccessForIntegrityLevel);
    HWND h = xt_window(); CHECK(h);
    HANDLE hm; const char *tag = xt_tag(); struct xt_shm *s = xt_shm_create(tag, &hm); CHECK(s);
    s->v[0] = GetCurrentProcessId();
    PROCESS_INFORMATION pi; HANDLE pr = xt_spawn("cafil_child", tag, NULL, &pi); CHECK(pr);
    DWORD cpid = pr ? pi.dwProcessId : 0;
    EXPECT_EQ(pr ? xt_reap(pr, &pi, 60000) : 1, 0);
    DWORD out = 0xCCCCCCCC;
    LONG st = UN(NtUserCheckAccessForIntegrityLevel)(GetCurrentProcessId(), cpid, &out);
    EXPECT_EQ((DWORD)st, ST_INVALID_PARAMETER);
    EXPECT_EQ(out, 0xCCCCCCCCu);
    UnmapViewOfFile((void *)s); CloseHandle(hm);
    xt_destroy(h);
}

/* ---- NotifyProcessCreate ---- */
static void job_npc(struct xt_wk *w, void *p) {
    struct pa *a = p;
    static const ULONG_PTR fl[] = { 1, 2, 0x10, 0x80000000u, 0xFFFFFFFFull, 0xFFFFFFFF00000001ull, 0x100000000ull };
    for (int i = 0; i < 7; i++) a->v[i] = R_D(UN(NtUserNotifyProcessCreate)(a->pid, 0, 0, fl[i], 0));
}
XT(notify_process_create_with_flags_is_denied_on_a_worker) {
    NEED(NtUserNotifyProcessCreate);
    struct xt_wk w; struct pa a; a.pid = GetCurrentProcessId();
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_npc, &a));
    for (int i = 0; i < 6; i++) EXPECT_EQ(a.v[i], ST_ACCESS_DENIED);
    xt_wk_stop(&w);
}
static void job_npc_hi(struct xt_wk *w, void *p) {
    struct pa *a = p;
    a->v[0] = R_D(UN(NtUserNotifyProcessCreate)(0x7FFFFFF0, 0, 0, 0, 0));
    a->v[1] = R_D(UN(NtUserNotifyProcessCreate)(0x7FFFFFF0, 0, 0, 0x100000000ull, 0));
    a->v[2] = R_D(UN(NtUserNotifyProcessCreate)(0x7FFFFFF0, 0, 0, 0x8000000000000000ull, 0));
}
XT(notify_process_create_looks_at_the_low_32_bits_of_the_flags_only) {
    NEED(NtUserNotifyProcessCreate);
    struct xt_wk w; struct pa a;
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_npc_hi, &a));
    EXPECT_EQ(a.v[1], a.v[0]);                          /* flags that are zero in the low half are no flags */
    EXPECT_EQ(a.v[2], a.v[0]);
    CHECK(a.v[0] != ST_ACCESS_DENIED);
    xt_wk_stop(&w);
}
XT(notify_process_create_with_flags_is_denied_on_the_main_thread) {
    NEED(NtUserNotifyProcessCreate);
    static const ULONG_PTR fl[] = { 1, 2, 0x10, 0x80000000u, 0xFFFFFFFFull };
    for (int i = 0; i < 5; i++) EXPECT_EQ(R_D(UN(NtUserNotifyProcessCreate)(GetCurrentProcessId(), 0, 0, fl[i], 0)), ST_ACCESS_DENIED);
}
XT(notify_process_create_denial_does_not_depend_on_the_target_process) {
    NEED(NtUserNotifyProcessCreate);
    static const ULONG_PTR pid[] = { 0, 4, 0x1234, 0x7FFFFFF0, 0xFFFFFFFFull };
    for (int i = 0; i < 5; i++) EXPECT_EQ(R_D(UN(NtUserNotifyProcessCreate)(pid[i], 0, 0, 1, 0)), ST_ACCESS_DENIED);
}
XT(notify_process_create_denial_does_not_depend_on_the_other_arguments) {
    NEED(NtUserNotifyProcessCreate);
    static const ULONG_PTR x[] = { 0, 1, 0x1234, 0xFFFFFFFFull };
    for (int i = 0; i < 4; i++) EXPECT_EQ(R_D(UN(NtUserNotifyProcessCreate)(GetCurrentProcessId(), x[i], x[3 - i], 1, x[i])), ST_ACCESS_DENIED);
}
static void fresh_npc(void *p) { DWORD *r = p; r[0] = R_D(UN(NtUserNotifyProcessCreate)(GetCurrentProcessId(), 0, 0, 1, 0)); }
XT(notify_process_create_is_denied_on_a_fresh_thread) {
    NEED(NtUserNotifyProcessCreate);
    DWORD r[1] = { 9 };
    CHECK(xt_on_thread(fresh_npc, r));
    EXPECT_EQ(r[0], ST_ACCESS_DENIED);
}
XC(npc) {
    s->v[0] = R_D(UN(NtUserNotifyProcessCreate)(GetCurrentProcessId(), 0, 0, 1, 0));
    s->v[1] = R_D(UN(NtUserNotifyProcessCreate)((ULONG_PTR)s->v[1], 0, 0, 2, 0));
    s->done = 1;
    return 0;
}
XT(notify_process_create_is_denied_in_a_child_process) {
    NEED(NtUserNotifyProcessCreate);
    HANDLE hm; const char *tag = xt_tag(); struct xt_shm *s = xt_shm_create(tag, &hm); CHECK(s);
    s->v[1] = GetCurrentProcessId();
    PROCESS_INFORMATION pi; HANDLE pr = xt_spawn("npc", tag, NULL, &pi); CHECK(pr);
    EXPECT_EQ(pr ? xt_reap(pr, &pi, 60000) : 1, 0);
    EXPECT_EQ((DWORD)s->v[0], ST_ACCESS_DENIED);
    EXPECT_EQ((DWORD)s->v[1], ST_ACCESS_DENIED);
    UnmapViewOfFile((void *)s); CloseHandle(hm);
}
XC(npc_zero) {
    s->v[0] = R_D(UN(NtUserNotifyProcessCreate)(0x7FFFFFF0, 0, 0, 0, 0));
    s->done = 1;
    return 0;
}
XT(notify_process_create_without_flags_for_an_unknown_process_in_a_child) {
    NEED(NtUserNotifyProcessCreate);
    HANDLE hm; const char *tag = xt_tag(); struct xt_shm *s = xt_shm_create(tag, &hm); CHECK(s);
    s->v[0] = 0xAAAAAAAA;
    PROCESS_INFORMATION pi; HANDLE pr = xt_spawn("npc_zero", tag, NULL, &pi); CHECK(pr);
    EXPECT_EQ(pr ? xt_reap(pr, &pi, 60000) : 1, 0);
    EXPECT_EQ((DWORD)s->v[0], 0);                       /* no flags: nothing to deny, nothing to do for an unknown process */
    UnmapViewOfFile((void *)s); CloseHandle(hm);
}

/* ---- DisableProcessWindowFiltering ---- */
XC(dpwf) {
    HWND w = xt_window(); CK(w != NULL);
    SET_ERR(SENT);
    CK_EQ(R_D(UN(NtUserDisableProcessWindowFiltering)()), 1);
    CK_EQ(GetLastError(), SENT);
    CK_EQ(R_D(UN(NtUserDisableProcessWindowFiltering)()), 1);
    CK(U(IsWindow)(w));
    CK(U(PostMessageW)(w, WM_APP + 1, 0, 0));
    MSG m; CK(U(PeekMessageW)(&m, w, WM_APP + 1, WM_APP + 1, PM_REMOVE));
    xt_destroy(w);
    return 0;
}
XT(disable_process_window_filtering_returns_one_and_keeps_last_error_in_a_child) {
    NEED(NtUserDisableProcessWindowFiltering);
    EXPECT_EQ(xt_child("dpwf", NULL), 0);
}
static void thr_dpwf(void *p) { DWORD *r = p; SET_ERR(SENT); r[0] = R_D(UN(NtUserDisableProcessWindowFiltering)()); r[1] = GetLastError(); }
XC(dpwf_threads) {
    DWORD r[2] = { 9, 9 };
    HWND w = xt_window(); CK(w != NULL);
    CK_EQ(R_D(UN(NtUserDisableProcessWindowFiltering)()), 1);
    CK(xt_on_thread(thr_dpwf, r));                      /* the switch is the process's: another thread may call it again */
    CK_EQ(r[0], 1); CK_EQ(r[1], SENT);
    xt_destroy(w);
    return 0;
}
XT(disable_process_window_filtering_is_answered_alike_by_a_second_thread_of_the_process) {
    NEED(NtUserDisableProcessWindowFiltering);
    EXPECT_EQ(xt_child("dpwf_threads", NULL), 0);
}
XC(dpwf_first) {
    DWORD r[2] = { 9, 9 };
    CK(xt_on_thread(thr_dpwf, r));                      /* the very first win32k call of the process, on a fresh thread */
    CK_EQ(r[0], 1); CK_EQ(r[1], SENT);
    return 0;
}
XT(disable_process_window_filtering_works_as_the_first_win32k_call_of_a_process) {
    NEED(NtUserDisableProcessWindowFiltering);
    EXPECT_EQ(xt_child("dpwf_first", NULL), 0);
}
XC(dpwf_other) {
    /* the parent's switch is not this process's: this process's own windows keep their normal message path */
    HWND w = xt_window(); CK(w != NULL);
    CK(U(PostMessageW)(w, WM_APP + 2, 7, 0));
    MSG m; CK(U(PeekMessageW)(&m, w, WM_APP + 2, WM_APP + 2, PM_REMOVE));
    CK_EQ(m.wParam, 7);
    xt_destroy(w);
    return 0;
}
XT(parent_switch_does_not_stop_a_child_from_using_its_own_windows) {
    NEED(NtUserDisableProcessWindowFiltering);
    EXPECT_EQ(R_D(UN(NtUserDisableProcessWindowFiltering)()), 1);
    EXPECT_EQ(xt_child("dpwf_other", NULL), 0);
}

/* ---- ClearForeground ---- */
static void job_cfg(struct xt_wk *w, void *p) {
    struct pa *a = p;
    SET_ERR(SENT); a->v[0] = (DWORD)UN(NtUserClearForeground)() & 0xFF; a->v[1] = GetLastError();
}
XC(cfg) {
    struct xt_wk w; struct pa a;
    SET_ERR(SENT); LONG r1 = UN(NtUserClearForeground)() & 0xFF; DWORD e1 = GetLastError();
    SET_ERR(SENT); LONG r2 = UN(NtUserClearForeground)() & 0xFF; DWORD e2 = GetLastError();
    CK(xt_wk_start(&w, 1));
    CK(xt_wk_run(&w, job_cfg, &a));
    xt_wk_stop(&w);
    s->v[0] = r1; s->v[1] = e1; s->v[2] = r2; s->v[3] = e2; s->v[4] = a.v[0]; s->v[5] = a.v[1];
    s->done = 1;
    return 0;
}
static void run_cfg(struct xt_shm **ps, HANDLE *hm) {
    const char *tag = xt_tag(); struct xt_shm *s = xt_shm_create(tag, hm); CHECK(s);
    PROCESS_INFORMATION pi; HANDLE pr = xt_spawn("cfg", tag, NULL, &pi); CHECK(pr);
    EXPECT_EQ(pr ? xt_reap(pr, &pi, 60000) : 1, 0);
    *ps = s;
}
XT(clear_foreground_answers_true_or_false_with_access_denied) {
    NEED(NtUserClearForeground);
    struct xt_shm *s; HANDLE hm; run_cfg(&s, &hm);
    DWORD r = (DWORD)s->v[0], e = (DWORD)s->v[1];
    CHECKF(r == 1 || (r == 0 && e == E_ACCESS_DENIED), "ret=%lu gle=%lu", r, e);
    UnmapViewOfFile((void *)s); CloseHandle(hm);
}
XT(clear_foreground_second_call_in_a_process_answers_like_the_first) {
    NEED(NtUserClearForeground);
    struct xt_shm *s; HANDLE hm; run_cfg(&s, &hm);
    EXPECT_EQ((DWORD)s->v[0], (DWORD)s->v[2]);
    UnmapViewOfFile((void *)s); CloseHandle(hm);
}
XT(clear_foreground_on_a_second_thread_answers_like_the_first_thread) {
    NEED(NtUserClearForeground);
    struct xt_shm *s; HANDLE hm; run_cfg(&s, &hm);
    EXPECT_EQ((DWORD)s->v[0], (DWORD)s->v[4]);
    UnmapViewOfFile((void *)s); CloseHandle(hm);
}
XT(clear_foreground_last_error_follows_the_result_on_every_thread) {
    NEED(NtUserClearForeground);
    struct xt_shm *s; HANDLE hm; run_cfg(&s, &hm);
    if ((DWORD)s->v[0] == 1) { EXPECT_EQ((DWORD)s->v[1], SENT); EXPECT_EQ((DWORD)s->v[5], SENT); }
    else { EXPECT_EQ((DWORD)s->v[1], E_ACCESS_DENIED); EXPECT_EQ((DWORD)s->v[5], E_ACCESS_DENIED); }
    UnmapViewOfFile((void *)s); CloseHandle(hm);
}
