/* Cross-thread timer ownership, process-wide timer matching, ValidateTimerCallback corner cases and
 * extra atom-name buffer rules. Timer matching in win32kfull!NtUserKillTimer / InternalSetTimer compares the
 * owning process (ppi), never the thread, for thread-queue (NULL hwnd) timers as well. */
#include "mta.h"

struct nt {                       /* a helper thread owning one NULL-hwnd timer until told to stop */
    HANDLE th, ready, stop;
    UINT_PTR id;
    TIMERPROC proc;
    DWORD tid;
};
static DWORD WINAPI nt_main(LPVOID p) {
    struct nt *n = p;
    U(GetDesktopWindow)();
    n->id = U(SetTimer)(NULL, 0, 3600000, n->proc);
    SetEvent(n->ready);
    WaitForSingleObject(n->stop, 10000);
    return 0;
}
static bool nt_start(struct nt *n, TIMERPROC proc) {
    memset(n, 0, sizeof *n);
    n->proc = proc;
    n->ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    n->stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    n->th = CreateThread(NULL, 0, nt_main, n, 0, &n->tid);
    if (!n->th || WaitForSingleObject(n->ready, 5000) != WAIT_OBJECT_0 || !n->id) {
        SetEvent(n->stop);
        if (n->th) { WaitForSingleObject(n->th, 5000); CloseHandle(n->th); }
        CloseHandle(n->ready); CloseHandle(n->stop);
        return false;
    }
    return true;
}
static void nt_stop(struct nt *n) {
    SetEvent(n->stop);
    WaitForSingleObject(n->th, 5000);
    CloseHandle(n->th); CloseHandle(n->ready); CloseHandle(n->stop);
}

TEST(msg_timers_atoms_x_null_hwnd_timer_killable_from_other_thread) {
    MTA_NEED();
    struct nt n;
    if (!nt_start(&n, NULL)) { t_skip("helper thread"); return; }
    bool tab = mta_table_ok();
    unsigned c0 = tab ? mta_timers() : 0;
    CHECK(n.id >= 0x100 && n.id <= 0x7fff);
    /* NtUserKillTimer: the match needs the same process (ppi), so another thread of the process may kill it */
    CHECK(U(KillTimer)(NULL, n.id));
    if (tab) MEQ(mta_timers(), c0 - 1);
    CHECK(!U(KillTimer)(NULL, n.id));
    nt_stop(&n);
}
TEST(msg_timers_atoms_x_null_hwnd_set_replaces_other_threads_timer) {
    MTA_NEED();
    struct nt n;
    if (!nt_start(&n, NULL)) { t_skip("helper thread"); return; }
    bool tab = mta_table_ok();
    unsigned c0 = tab ? mta_timers() : 0;
    /* InternalSetTimer: (NULL, id) matches the other thread's timer by process, so it is reset in place */
    MEQ(U(SetTimer)(NULL, n.id, 3500000, NULL), n.id);
    if (tab) MEQ(mta_timers(), c0);
    CHECK(U(KillTimer)(NULL, n.id));
    if (tab) MEQ(mta_timers(), c0 - 1);
    nt_stop(&n);
}
TEST(msg_timers_atoms_x_null_hwnd_ids_distinct_across_threads) {
    MTA_NEED();
    struct nt n;
    if (!nt_start(&n, NULL)) { t_skip("helper thread"); return; }
    UINT_PTR mine = U(SetTimer)(NULL, 0, 3600000, NULL);
    REQUIRE(mine);
    CHECK(mine != n.id);                    /* one process-wide id bitmap (gTimerId) backs every NULL-hwnd timer */
    CHECK(mine >= 0x100 && mine <= 0x7fff);
    U(KillTimer)(NULL, mine); U(KillTimer)(NULL, n.id);
    nt_stop(&n);
}
TEST(msg_timers_atoms_x_validate_callback_sees_other_threads_timer) {
    MTA_NEED();
    NtUserValidateTimerCallback_pfn v = UN(NtUserValidateTimerCallback);
    REQUIRE(v);
    if (t_is_wine()) { t_skip("Wine's win32u aborts on the unimplemented NtUserValidateTimerCallback"); return; }
    struct nt n;
    if (!nt_start(&n, mta_proc_b)) { t_skip("helper thread"); return; }
    MEQ(v((PVOID)mta_proc_b), 1);           /* NtUserValidateTimerCallback compares ppi only */
    CHECK(U(KillTimer)(NULL, n.id));
    MEQ(v((PVOID)mta_proc_b), 0);
    nt_stop(&n);
}
TEST(msg_timers_atoms_x_validate_callback_null_matches_procless_timer) {
    MTA_NEED();
    NtUserValidateTimerCallback_pfn v = UN(NtUserValidateTimerCallback);
    REQUIRE(v);
    if (t_is_wine()) { t_skip("Wine's win32u aborts on the unimplemented NtUserValidateTimerCallback"); return; }
    /* the scan compares tagTIMER.pfn with the argument, so NULL matches a timer created without a callback */
    UINT_PTR id = U(SetTimer)(NULL, 0, 3600000, NULL);
    REQUIRE(id);
    MEQ(v(NULL), 1);
    CHECK(U(KillTimer)(NULL, id));
}
TEST(msg_timers_atoms_x_validate_callback_ignores_system_timers) {
    MTA_WIN(w);
    NtUserValidateTimerCallback_pfn v = UN(NtUserValidateTimerCallback);
    NtUserSetSystemTimer_pfn st = UN(NtUserSetSystemTimer);
    REQUIRE(v); REQUIRE(st);
    if (t_is_wine()) { t_skip("Wine's win32u aborts on the unimplemented NtUserValidateTimerCallback"); mta_destroy(w); return; }
    /* system timers carry flags 2 and the scan skips flags & 6: a callback-less system timer (pfn NULL) must not
     * make NULL a valid callback, while a plain callback-less timer does */
    MEQ(st(w, 0x50, 3600000), 0x50);
    MEQ(v(NULL), 0);
    MEQ(U(SetTimer)(w, 0x51, 3600000, NULL), 0x51);
    MEQ(v(NULL), 1);
    CHECK(U(KillTimer)(w, 0x51));
    MEQ(v(NULL), 0);
    mta_destroy(w);
}
TEST(msg_timers_atoms_x_timer_user_objects_count_table) {
    MTA_NEED();
    NtUserSetTimer_pfn st = UN(NtUserSetTimer); NtUserKillTimer_pfn kt = UN(NtUserKillTimer);
    REQUIRE(st); REQUIRE(kt);
    DWORD g0 = mta_guires();
    if (!g0 && t_is_wine()) { t_skip("no GetGuiResources counts"); return; }
    UINT_PTR ids[24];
    for (int i = 0; i < 24; i++) { ids[i] = st(NULL, 0, 3600000, NULL, 0); CHECK(ids[i] != 0); }
    MEQ(mta_guires(), g0 + 24);             /* each timer is one USER handle charged to the process */
    for (int i = 0; i < 24; i++) CHECK(kt(NULL, ids[i]));
    MEQ(mta_guires(), g0);
}
TEST(msg_timers_atoms_x_replace_does_not_allocate_new_handle) {
    MTA_WIN(w);
    DWORD g0 = mta_guires();
    if (!g0 && t_is_wine()) { t_skip("no GetGuiResources counts"); mta_destroy(w); return; }
    MEQ(U(SetTimer)(w, 4, 3600000, NULL), 4);
    DWORD g1 = mta_guires();
    for (int i = 0; i < 20; i++) MEQ(U(SetTimer)(w, 4, 3600000 - i, mta_proc_a), 4);
    MEQ(mta_guires(), g1);                  /* the (hwnd,id) match reuses the object */
    CHECK(U(KillTimer)(w, 4));
    MEQ(mta_guires(), g1 - 1);
    mta_destroy(w);
}
TEST(msg_timers_atoms_x_replace_changes_proc_registration) {
    MTA_WIN(w);
    NtUserValidateTimerCallback_pfn v = UN(NtUserValidateTimerCallback);
    REQUIRE(v);
    if (t_is_wine()) { t_skip("Wine's win32u aborts on the unimplemented NtUserValidateTimerCallback"); mta_destroy(w); return; }
    MEQ(U(SetTimer)(w, 1, 3600000, mta_proc_a), 1);
    MEQ(v((PVOID)mta_proc_a), 1);
    MEQ(U(SetTimer)(w, 1, 3600000, mta_proc_b), 1);   /* InternalSetTimer overwrites tagTIMER.pfn on a match */
    MEQ(v((PVOID)mta_proc_a), 0);
    MEQ(v((PVOID)mta_proc_b), 1);
    CHECK(U(KillTimer)(w, 1));
    mta_destroy(w);
}
TEST(msg_timers_atoms_x_qs_timer_is_per_thread_queue) {
    MTA_WIN(w);
    mta_drain_timers();
    CHECK(!((U(GetQueueStatus)(QS_TIMER) >> 16) & QS_TIMER));
    struct mta_owner o;
    if (!mta_owner_start(&o, 1, 1, 0)) { t_skip("owner thread"); mta_destroy(w); return; }
    mta_reset();
    MEQ(U(SetTimer)(o.hwnd, 3, 20, NULL), 3);        /* due on the owner thread's queue */
    DWORD end = GetTickCount() + 3000; int n = 0;
    while ((LONG)(end - GetTickCount()) > 0 && !(n = mta_count(WM_TIMER, 3))) Sleep(5);
    CHECK(n >= 1);
    CHECK(!((U(GetQueueStatus)(QS_TIMER) >> 16) & QS_TIMER));   /* this thread's wake bits are untouched */
    U(KillTimer)(o.hwnd, 3);
    mta_owner_stop(&o);
    mta_destroy(w);
}
TEST(msg_timers_atoms_x_direct_killtimer_invalid_hwnd) {
    MTA_NEED();
    NtUserKillTimer_pfn kt = UN(NtUserKillTimer);
    REQUIRE(kt);
    SET_ERR(0);
    MEQ(kt((HWND)(ULONG_PTR)0x12345678, 1), 0);
    EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
    MEQ(kt(NULL, 0x7ff0), 0);                         /* NULL hwnd is not validated, just not found */
}
TEST(msg_timers_atoms_x_direct_settimer_max_elapse_tolerance_special_values) {
    MTA_WIN(w);
    NtUserSetTimer_pfn st = UN(NtUserSetTimer); NtUserKillTimer_pfn kt = UN(NtUserKillTimer);
    REQUIRE(st); REQUIRE(kt);
    /* 0x7ffffff5 means "tolerance = max - elapse" (any elapse), -1 means none; both skip the overflow test */
    MEQ(st(w, 1, 0x7fffffff, NULL, 0x7ffffff5), 1);
    MEQ(st(w, 2, 10, NULL, 0x7ffffff5), 2);
    MEQ(st(w, 3, 0xffffffffu, NULL, 0xffffffffu), 3);
    SET_ERR(0);
    MEQ(st(w, 4, 10, NULL, 0x7ffffff6), 0);          /* 10 + tolerance exceeds 0x7fffffff */
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    for (UINT_PTR i = 1; i <= 3; i++) CHECK(kt(w, i));
    CHECK(!kt(w, 4));
    mta_destroy(w);
}
TEST(msg_timers_atoms_x_failed_set_leaves_existing_timer_alone) {
    MTA_WIN(w);
    NtUserSetTimer_pfn st = UN(NtUserSetTimer);
    REQUIRE(st);
    bool tab = mta_table_ok();
    MEQ(st(w, 6, 3600000, NULL, 0), 6);
    unsigned c0 = tab ? mta_timers() : 0;
    MEQ(st(w, 6, 1000, NULL, 0x7fffffff), 0);        /* rejected before InternalSetTimer runs */
    if (tab) MEQ(mta_timers(), c0);
    CHECK(U(KillTimer)(w, 6));
    mta_destroy(w);
}
/* ---- atom-name buffers ------------------------------------------------------------------------------------ */
TEST(msg_timers_atoms_x_getatomname_odd_maximum_length_rounds_down) {
    MTA_NEED();
    NtUserGetAtomName_pfn g = UN(NtUserGetAtomName);
    REQUIRE(g);
    WCHAR n[300], b[300]; mta_name(n, 300, "x_odd");
    UINT id = U(RegisterWindowMessageW)(n); REQUIRE(id);
    unsigned len = (unsigned)wcslen(n);
    /* NtUserGetAtomName passes MaximumLength >> 1 characters, so an odd byte count loses the half character */
    memset(b, 0xAA, sizeof b);
    UNICODE_STRING u; u.Buffer = b; u.Length = 0; u.MaximumLength = (USHORT)(6 * 2 + 1);
    MEQ(g((ATOM)id, &u), 5);
    CHECK(b[5] == 0 && !wcsncmp(b, n, 5));
    CHECK(b[6] == 0xAAAA);
    (void)len;
}
TEST(msg_timers_atoms_x_getatomname_length_field_is_ignored) {
    MTA_NEED();
    NtUserGetAtomName_pfn g = UN(NtUserGetAtomName);
    REQUIRE(g);
    WCHAR n[300], b[300]; mta_name(n, 300, "x_lenign");
    UINT id = U(RegisterWindowMessageW)(n); REQUIRE(id);
    memset(b, 0, sizeof b);
    UNICODE_STRING u; u.Buffer = b; u.Length = 2; u.MaximumLength = sizeof b;   /* only MaximumLength sizes the buffer */
    MEQ(g((ATOM)id, &u), wcslen(n));
    CHECK(!wcscmp(b, n));
}
TEST(msg_timers_atoms_x_atom_names_are_stable_across_queries) {
    MTA_NEED();
    WCHAR n[300], a[300], b[300]; mta_name(n, 300, "x_stable");
    UINT id = U(RegisterWindowMessageW)(n); REQUIRE(id);
    for (int i = 0; i < 25; i++) {
        memset(a, 0, sizeof a); memset(b, 0, sizeof b);
        MEQ(U(GetClipboardFormatNameW)(id, a, 300), wcslen(n));
        MEQ(U(GetClipboardFormatNameW)(id, b, 300), wcslen(n));
        CHECK(!wcscmp(a, n) && !wcscmp(b, n));
    }
}
TEST(msg_timers_atoms_x_register_does_not_consume_integer_range) {
    MTA_NEED();
    /* string atoms occupy 0xC000..0xFFFF, so registering never yields an id of an integer (resource/class) atom */
    for (int i = 0; i < 20; i++) {
        WCHAR n[300]; char t[32]; snprintf(t, sizeof t, "x_range_%d", i); mta_name(n, 300, t);
        UINT id = U(RegisterWindowMessageW)(n);
        CHECKF(id >= 0xC000 && id <= 0xFFFF, "id 0x%x", id);
    }
}
TEST(msg_timers_atoms_x_rwm_integer_zero_and_leading_zero) {
    MTA_NEED();
    /* "#0" parses to the invalid integer atom 0; "#0012" parses like "#12" */
    SET_ERR(0);
    MEQ(U(RegisterWindowMessageW)(L"#0"), 0);
    MEQ(U(RegisterWindowMessageW)(L"#0012"), 12);
}
