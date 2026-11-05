/* The debugging classes of NtQueryInformationProcess on Windows 10 22H2 x64: the debug port (class 7), the debug
 * object handle (30), the debug flags (31) and the handle tracing database (32), plus the restricted-caller
 * rule that refuses class 32 (and two working-set watch classes) to low-integrity callers. A helper process is
 * attached with DebugActiveProcess in a controlled way, queried, detached and terminated; every handle that
 * class 30 hands out is closed. Statuses, output cells and ReturnLength are checked; handle values are not. */
#include "qp.h"

static ULONG_PTR port_of(HANDLE h, NTSTATUS *st) {
    ULONG_PTR v = 0x1234;
    ULONG rl = QP_SENT;
    *st = QIP(h, 7, &v, 8, &rl);
    return v;
}
static int attach(DWORD pid) {
    if (!DebugActiveProcess(pid)) { t_skip("DebugActiveProcess failed with %lu", GetLastError()); return 0; }
    return 1;
}
static void detach(DWORD pid) {
    DEBUG_EVENT ev;
    int n = 0;
    DebugSetProcessKillOnExit(FALSE);
    while (n < 200 && WaitForDebugEvent(&ev, 100)) {
        n++;
        ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, DBG_CONTINUE);
    }
    DebugActiveProcessStop(pid);
}
#define NEED_Q() do { if (!QIP) { t_skip("NtQueryInformationProcess missing"); return; } } while (0)
#define SPAWN(mode) PROCESS_INFORMATION pi; HANDLE ph = qp_spawn(mode, &pi); if (!ph) { t_skip("cannot start helper process"); return; }

/* ---- class 7 ---------------------------------------------------------------------------------------- */
T5(dbg7_self_port_is_zero) {
    NEED_Q();
    NTSTATUS st; ULONG_PTR v = port_of(QP_PSEUDO_SELF, &st);
    EXPECT_EQ(st, QS_OK); EXPECT_EQ(v, 0);
}
T5(dbg7_self_return_length_is_eight) {
    NEED_Q();
    ULONG_PTR v = 0x55; ULONG rl = QP_SENT;
    EXPECT_EQ(QIP(QP_PSEUDO_SELF, 7, &v, 8, &rl), QS_OK);
    EXPECT_EQ(rl, 8);
}
T5(dbg7_writes_exactly_eight_bytes) {
    NEED_Q();
    ULONG_PTR cell[3] = { 0xA5A5A5A5A5A5A5A5ull, 0xA5A5A5A5A5A5A5A5ull, 0x5A5A5A5A5A5A5A5Aull };
    EXPECT_EQ(QIP(QP_PSEUDO_SELF, 7, &cell[0], 8, NULL), QS_OK);
    EXPECT_EQ(cell[0], 0);
    EXPECT_EQ(cell[1], 0xA5A5A5A5A5A5A5A5ull);
    EXPECT_EQ(cell[2], 0x5A5A5A5A5A5A5A5Aull);
}
T5(dbg7_wrong_lengths_do_not_write) {
    NEED_Q();
    static const ULONG lens[] = { 0, 1, 4, 7, 9, 16, 0x100 };
    for (int i = 0; i < 7; i++) {
        ULONG_PTR cell[34]; memset(cell, 0xCC, sizeof cell);
        ULONG rl = QP_SENT;
        NTSTATUS st = QIP(QP_PSEUDO_SELF, 7, cell, lens[i], &rl);
        t_check(st == QS_LEN_MISMATCH && rl == QP_SENT && cell[0] == 0xCCCCCCCCCCCCCCCCull, __FILE__, __LINE__, "len mismatch", "len %lu st 0x%lx", lens[i], (unsigned long)st);
    }
}
T5(dbg7_matches_check_remote_debugger_present_for_self) {
    NEED_Q();
    BOOL rem = 7;
    NTSTATUS st; ULONG_PTR v = port_of(QP_PSEUDO_SELF, &st);
    CHECK(CheckRemoteDebuggerPresent(GetCurrentProcess(), &rem));
    CHECKF((rem != FALSE) == (v != 0), "rem %d port nonzero %d", (int)rem, v != 0);
    CHECK(!IsDebuggerPresent() == (rem == FALSE));
}
T5(dbg7_real_self_handle_with_query_right) {
    NEED_Q();
    HANDLE h = qp_hmake(H_QRY, NULL);
    NTSTATUS st; ULONG_PTR v = port_of(h, &st);
    EXPECT_EQ(st, QS_OK); EXPECT_EQ(v, 0x1234 * 0);
    qp_hrelease(H_QRY, h);
}
T5(dbg7_limited_right_is_not_enough) {
    NEED_Q();
    HANDLE h = qp_hmake(H_LIM, NULL);
    NTSTATUS st; port_of(h, &st);
    EXPECT_EQ(st, QS_ACCESS_DENIED);
    qp_hrelease(H_LIM, h);
}
T5(dbg7_child_not_debugged) {
    NEED_Q(); SPAWN('n');
    HANDLE q = qp_hmake(H_CH_QRY, ph);
    NTSTATUS st; ULONG_PTR v = port_of(q, &st);
    EXPECT_EQ(st, QS_OK); EXPECT_EQ(v, 0);
    BOOL rem = 9;
    CHECK(CheckRemoteDebuggerPresent(ph, &rem));
    CHECK(rem == FALSE);
    qp_hrelease(H_CH_QRY, q); qp_kill(ph, &pi);
}
T5(dbg7_child_rights) {
    NEED_Q(); SPAWN('n');
    static const int kinds[] = { H_CH_LIM, H_CH_SYNC, H_CH_ZERO, H_CH_QRY };
    static const NTSTATUS want[] = { QS_ACCESS_DENIED, QS_ACCESS_DENIED, QS_ACCESS_DENIED, QS_OK };
    for (int i = 0; i < 4; i++) {
        HANDLE h = qp_hmake(kinds[i], ph); NTSTATUS st; port_of(h, &st);
        t_check(st == want[i], __FILE__, __LINE__, "rights", "%s st 0x%lx", qp_hname[kinds[i]], (unsigned long)st);
        qp_hrelease(kinds[i], h);
    }
    qp_kill(ph, &pi);
}
T5(dbg7_attached_child_reports_all_ones) {
    NEED_Q(); SPAWN('n');
    if (attach(pi.dwProcessId)) {
        HANDLE q = qp_hmake(H_CH_QRY, ph);
        NTSTATUS st; ULONG_PTR v = port_of(q, &st);
        EXPECT_EQ(st, QS_OK); EXPECT_EQ(v, ~(ULONG_PTR)0);
        BOOL rem = 0;
        CHECK(CheckRemoteDebuggerPresent(ph, &rem) && rem);
        qp_hrelease(H_CH_QRY, q);
        detach(pi.dwProcessId);
    }
    qp_kill(ph, &pi);
}
T5(dbg7_attach_does_not_change_the_debuggers_own_port) {
    NEED_Q(); SPAWN('n');
    if (attach(pi.dwProcessId)) {
        NTSTATUS st; ULONG_PTR v = port_of(QP_PSEUDO_SELF, &st);
        EXPECT_EQ(st, QS_OK); EXPECT_EQ(v, 0);
        detach(pi.dwProcessId);
    }
    qp_kill(ph, &pi);
}
T5(dbg7_detach_restores_zero) {
    NEED_Q(); SPAWN('n');
    if (attach(pi.dwProcessId)) {
        detach(pi.dwProcessId);
        HANDLE q = qp_hmake(H_CH_QRY, ph);
        NTSTATUS st; ULONG_PTR v = port_of(q, &st);
        EXPECT_EQ(st, QS_OK); EXPECT_EQ(v, 0);
        BOOL rem = 9;
        CHECK(CheckRemoteDebuggerPresent(ph, &rem) && !rem);
        qp_hrelease(H_CH_QRY, q);
    }
    qp_kill(ph, &pi);
}

/* ---- class 30 --------------------------------------------------------------------------------------- */
T5(dbg30_self_port_not_set) {
    NEED_Q();
    HANDLE out = (HANDLE)(ULONG_PTR)0x1234; ULONG rl = QP_SENT;
    EXPECT_EQ(QIP(QP_PSEUDO_SELF, 30, &out, 8, &rl), QS_PORT_NOT_SET);
    EXPECT_EQ(rl, 8);
    EXPECT_EQ(out, 0);
}
T5(dbg30_status_is_the_documented_value) {
    NEED_Q();
    HANDLE out = NULL;
    NTSTATUS st = QIP(QP_PSEUDO_SELF, 30, &out, 8, NULL);
    EXPECT_EQ((ULONG)st, 0xC0000353u);
    CHECK(st < 0);
}
T5(dbg30_zeroes_only_eight_bytes) {
    NEED_Q();
    ULONG_PTR cell[2] = { 0x1111111111111111ull, 0x2222222222222222ull };
    EXPECT_EQ(QIP(QP_PSEUDO_SELF, 30, cell, 8, NULL), QS_PORT_NOT_SET);
    EXPECT_EQ(cell[0], 0); EXPECT_EQ(cell[1], 0x2222222222222222ull);
}
T5(dbg30_wrong_lengths) {
    NEED_Q();
    static const ULONG lens[] = { 0, 1, 4, 7, 9, 16 };
    for (int i = 0; i < 6; i++) {
        ULONG_PTR cell[4]; memset(cell, 0xCC, sizeof cell); ULONG rl = QP_SENT;
        NTSTATUS st = QIP(QP_PSEUDO_SELF, 30, cell, lens[i], &rl);
        t_check(st == QS_LEN_MISMATCH && rl == QP_SENT && cell[0] == 0xCCCCCCCCCCCCCCCCull, __FILE__, __LINE__, "len mismatch", "len %lu st 0x%lx", lens[i], (unsigned long)st);
    }
}
T5(dbg30_child_not_debugged) {
    NEED_Q(); SPAWN('n');
    HANDLE q = qp_hmake(H_CH_QRY, ph); HANDLE out = (HANDLE)0x77; ULONG rl = QP_SENT;
    EXPECT_EQ(QIP(q, 30, &out, 8, &rl), QS_PORT_NOT_SET);
    EXPECT_EQ(out, 0); EXPECT_EQ(rl, 8);
    qp_hrelease(H_CH_QRY, q); qp_kill(ph, &pi);
}
T5(dbg30_child_rights) {
    NEED_Q(); SPAWN('n');
    static const int kinds[] = { H_CH_LIM, H_CH_SYNC, H_CH_ZERO, H_CH_QRY };
    static const NTSTATUS want[] = { QS_ACCESS_DENIED, QS_ACCESS_DENIED, QS_ACCESS_DENIED, QS_PORT_NOT_SET };
    for (int i = 0; i < 4; i++) {
        HANDLE h = qp_hmake(kinds[i], ph); HANDLE out = (HANDLE)0x77;
        NTSTATUS st = QIP(h, 30, &out, 8, NULL);
        t_check(st == want[i], __FILE__, __LINE__, "rights", "%s st 0x%lx", qp_hname[kinds[i]], (unsigned long)st);
        qp_hrelease(kinds[i], h);
    }
    qp_kill(ph, &pi);
}
T5(dbg30_attached_child_returns_a_debug_object_handle) {
    NEED_Q(); SPAWN('n');
    if (attach(pi.dwProcessId)) {
        HANDLE q = qp_hmake(H_CH_QRY, ph); HANDLE out = NULL; ULONG rl = QP_SENT;
        NTSTATUS st = QIP(q, 30, &out, 8, &rl);
        EXPECT_EQ(st, QS_OK); EXPECT_EQ(rl, 8);
        CHECK(out != NULL);
        if (out) {
            DWORD fl = 0xFF;
            CHECK(GetHandleInformation(out, &fl));
            CHECK(qp_type_is(out, L"DebugObject"));
            CloseHandle(out);
        }
        qp_hrelease(H_CH_QRY, q);
        detach(pi.dwProcessId);
    }
    qp_kill(ph, &pi);
}
T5(dbg30_each_query_makes_a_new_handle) {
    NEED_Q(); SPAWN('n');
    if (attach(pi.dwProcessId)) {
        HANDLE q = qp_hmake(H_CH_QRY, ph); HANDLE a = NULL, b = NULL;
        EXPECT_EQ(QIP(q, 30, &a, 8, NULL), QS_OK);
        EXPECT_EQ(QIP(q, 30, &b, 8, NULL), QS_OK);
        CHECK(a && b && a != b);
        if (a) CloseHandle(a);
        if (b) CloseHandle(b);
        qp_hrelease(H_CH_QRY, q);
        detach(pi.dwProcessId);
    }
    qp_kill(ph, &pi);
}
T5(dbg30_wrong_length_creates_no_handle_when_attached) {
    NEED_Q(); SPAWN('n');
    if (attach(pi.dwProcessId)) {
        HANDLE q = qp_hmake(H_CH_QRY, ph);
        DWORD before = 0, after = 0;
        GetProcessHandleCount(GetCurrentProcess(), &before);
        HANDLE cell[2] = { 0, 0 };
        EXPECT_EQ(QIP(q, 30, cell, 4, NULL), QS_LEN_MISMATCH);
        EXPECT_EQ(QIP(q, 30, cell, 16, NULL), QS_LEN_MISMATCH);
        GetProcessHandleCount(GetCurrentProcess(), &after);
        CHECK(before == after);
        CHECK(cell[0] == 0);
        qp_hrelease(H_CH_QRY, q);
        detach(pi.dwProcessId);
    }
    qp_kill(ph, &pi);
}
T5(dbg30_detached_child_is_not_set_again) {
    NEED_Q(); SPAWN('n');
    if (attach(pi.dwProcessId)) {
        detach(pi.dwProcessId);
        HANDLE q = qp_hmake(H_CH_QRY, ph); HANDLE out = (HANDLE)0x77;
        EXPECT_EQ(QIP(q, 30, &out, 8, NULL), QS_PORT_NOT_SET);
        EXPECT_EQ(out, 0);
        qp_hrelease(H_CH_QRY, q);
    }
    qp_kill(ph, &pi);
}
T5(dbg30_handle_checked_after_length) {
    NEED_Q();
    HANDLE out = NULL;
    EXPECT_EQ(QIP(NULL, 30, &out, 8, NULL), QS_BAD_HANDLE);
    EXPECT_EQ(QIP(NULL, 30, &out, 4, NULL), QS_LEN_MISMATCH);
    EXPECT_EQ(QIP(QP_PSEUDO_THREAD, 30, &out, 8, NULL), QS_TYPE_MISMATCH);
}

/* ---- class 31 --------------------------------------------------------------------------------------- */
T5(dbg31_self_default_inherits) {
    NEED_Q();
    ULONG v = 0x77, rl = QP_SENT;
    EXPECT_EQ(QIP(QP_PSEUDO_SELF, 31, &v, 4, &rl), QS_OK);
    EXPECT_EQ(v, 1); EXPECT_EQ(rl, 4);
}
T5(dbg31_writes_exactly_four_bytes) {
    NEED_Q();
    ULONG cell[3] = { 0x77777777, 0x88888888, 0x99999999 };
    EXPECT_EQ(QIP(QP_PSEUDO_SELF, 31, cell, 4, NULL), QS_OK);
    EXPECT_EQ(cell[0], 1); EXPECT_EQ(cell[1], 0x88888888); EXPECT_EQ(cell[2], 0x99999999);
}
T5(dbg31_child_default_inherits) {
    NEED_Q(); SPAWN('n');
    HANDLE q = qp_hmake(H_CH_QRY, ph); ULONG v = 0x77;
    EXPECT_EQ(QIP(q, 31, &v, 4, NULL), QS_OK);
    EXPECT_EQ(v, 1);
    qp_hrelease(H_CH_QRY, q); qp_kill(ph, &pi);
}
T5(dbg31_child_that_set_no_debug_inherit_reads_zero) {
    NEED_Q(); SPAWN('f');
    HANDLE q = qp_hmake(H_CH_QRY, ph); ULONG v = 0x77;
    EXPECT_EQ(QIP(q, 31, &v, 4, NULL), QS_OK);
    EXPECT_EQ(v, 0);
    qp_hrelease(H_CH_QRY, q); qp_kill(ph, &pi);
}
T5(dbg31_child_rights) {
    NEED_Q(); SPAWN('n');
    static const int kinds[] = { H_CH_LIM, H_CH_SYNC, H_CH_ZERO, H_CH_QRY };
    static const NTSTATUS want[] = { QS_ACCESS_DENIED, QS_ACCESS_DENIED, QS_ACCESS_DENIED, QS_OK };
    for (int i = 0; i < 4; i++) {
        HANDLE h = qp_hmake(kinds[i], ph); ULONG v = 0x77;
        NTSTATUS st = QIP(h, 31, &v, 4, NULL);
        t_check(st == want[i], __FILE__, __LINE__, "rights", "%s st 0x%lx", qp_hname[kinds[i]], (unsigned long)st);
        qp_hrelease(kinds[i], h);
    }
    qp_kill(ph, &pi);
}
T5(dbg31_wrong_lengths) {
    NEED_Q();
    static const ULONG lens[] = { 0, 1, 2, 3, 5, 8, 0x10 };
    for (int i = 0; i < 7; i++) {
        ULONG cell[8]; memset(cell, 0xCC, sizeof cell); ULONG rl = QP_SENT;
        NTSTATUS st = QIP(QP_PSEUDO_SELF, 31, cell, lens[i], &rl);
        t_check(st == QS_LEN_MISMATCH && rl == QP_SENT && cell[0] == 0xCCCCCCCC, __FILE__, __LINE__, "len mismatch", "len %lu st 0x%lx", lens[i], (unsigned long)st);
    }
}
T5(dbg31_attached_child_reads_zero) {
    NEED_Q(); SPAWN('n');
    if (attach(pi.dwProcessId)) {
        HANDLE q = qp_hmake(H_CH_QRY, ph); ULONG v = 0x77;
        EXPECT_EQ(QIP(q, 31, &v, 4, NULL), QS_OK);
        EXPECT_EQ(v, 0);
        qp_hrelease(H_CH_QRY, q);
        detach(pi.dwProcessId);
    }
    qp_kill(ph, &pi);
}

/* ---- class 32 --------------------------------------------------------------------------------------- */
T5(dbg32_below_sixteen_bytes_is_a_length_mismatch) {
    NEED_Q();
    for (ULONG l = 0; l < 0x10; l++) {
        unsigned char *b = qp_buf(); memset(b, 0xCC, 0x40); ULONG rl = QP_SENT;
        NTSTATUS st = QIP(QP_PSEUDO_SELF, 32, b, l, &rl);
        t_check(st == QS_LEN_MISMATCH && rl == QP_SENT && b[0] == 0xCC, __FILE__, __LINE__, "short", "len %lu st 0x%lx", l, (unsigned long)st);
    }
}
T5(dbg32_tracing_not_enabled_is_an_invalid_parameter) {
    NEED_Q();
    static const ULONG lens[] = { 0x10, 0x14, 0x18, 0x20, 0x21, 0x40, 0x100, 0x1000 };
    for (int i = 0; i < 8; i++) {
        unsigned char *b = qp_buf(); memset(b, 0xCC, 0x40); ULONG rl = QP_SENT;
        NTSTATUS st = QIP(QP_PSEUDO_SELF, 32, b, lens[i], &rl);
        t_check(st == QS_BAD_PARAM && rl == QP_SENT && b[0] == 0xCC, __FILE__, __LINE__, "not enabled", "len 0x%lx st 0x%lx", lens[i], (unsigned long)st);
    }
}
T5(dbg32_child_not_enabled) {
    NEED_Q(); SPAWN('n');
    HANDLE q = qp_hmake(H_CH_QRY, ph);
    EXPECT_EQ(QIP(q, 32, qp_buf(), 0x10, NULL), QS_BAD_PARAM);
    EXPECT_EQ(QIP(q, 32, qp_buf(), 0xF, NULL), QS_LEN_MISMATCH);
    qp_hrelease(H_CH_QRY, q); qp_kill(ph, &pi);
}
T5(dbg32_child_rights) {
    NEED_Q(); SPAWN('n');
    static const int kinds[] = { H_CH_LIM, H_CH_SYNC, H_CH_ZERO, H_CH_QRY };
    static const NTSTATUS want[] = { QS_ACCESS_DENIED, QS_ACCESS_DENIED, QS_ACCESS_DENIED, QS_BAD_PARAM };
    for (int i = 0; i < 4; i++) {
        HANDLE h = qp_hmake(kinds[i], ph);
        NTSTATUS st = QIP(h, 32, qp_buf(), 0x10, NULL);
        t_check(st == want[i], __FILE__, __LINE__, "rights", "%s st 0x%lx", qp_hname[kinds[i]], (unsigned long)st);
        qp_hrelease(kinds[i], h);
    }
    qp_kill(ph, &pi);
}
T5(dbg32_handle_errors_precede_the_tracing_check) {
    NEED_Q();
    EXPECT_EQ(QIP(NULL, 32, qp_buf(), 0x10, NULL), QS_BAD_HANDLE);
    EXPECT_EQ(QIP(QP_PSEUDO_THREAD, 32, qp_buf(), 0x10, NULL), QS_TYPE_MISMATCH);
    EXPECT_EQ(QIP(NULL, 32, qp_buf(), 0x8, NULL), QS_LEN_MISMATCH);
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    EXPECT_EQ(QIP(ev, 32, qp_buf(), 0x10, NULL), QS_TYPE_MISMATCH);
    CloseHandle(ev);
}
T5(dbg32_pseudo_handle_and_real_query_handle_agree) {
    NEED_Q();
    HANDLE h = qp_hmake(H_QRY, NULL);
    EXPECT_EQ(QIP(h, 32, qp_buf(), 0x20, NULL), QIP(QP_PSEUDO_SELF, 32, qp_buf(), 0x20, NULL));
    qp_hrelease(H_QRY, h);
}

/* ---- restricted callers ------------------------------------------------------------------------------ */
static int have_il_child(void) {
    NTSTATUS st;
    if (!qp_run_il(0x2000, 24, 4, &st)) { t_skip("cannot start a reduced-integrity helper process"); return 0; }
    return 1;
}
T5(restricted_low_integrity_caller_is_refused_class_32) {
    NEED_Q();
    if (!have_il_child()) return;
    NTSTATUS st = 0;
    CHECK(qp_run_il(0x1000, 32, 0x10, &st));
    EXPECT_EQ(st, QS_ACCESS_DENIED);
}
T5(restricted_low_integrity_caller_is_refused_the_watch_classes) {
    NEED_Q();
    if (!have_il_child()) return;
    NTSTATUS st = 0;
    CHECK(qp_run_il(0x1000, 15, 0x20, &st)); EXPECT_EQ(st, QS_ACCESS_DENIED);
    CHECK(qp_run_il(0x1000, 42, 0x20, &st)); EXPECT_EQ(st, QS_ACCESS_DENIED);
}
T5(restricted_medium_integrity_caller_is_not_refused) {
    NEED_Q();
    if (!have_il_child()) return;
    NTSTATUS st = 0;
    CHECK(qp_run_il(0x2000, 32, 0x10, &st)); EXPECT_EQ(st, QS_BAD_PARAM);
    CHECK(qp_run_il(0x2000, 15, 0x20, &st)); EXPECT_EQ(st, QS_UNSUCCESSFUL);
    CHECK(qp_run_il(0x2000, 42, 0x20, &st)); EXPECT_EQ(st, QS_UNSUCCESSFUL);
}
T5(restricted_low_integrity_caller_still_answers_ordinary_classes) {
    NEED_Q();
    if (!have_il_child()) return;
    static const struct { ULONG id, len; NTSTATUS want; } t[] = {
        { 0, 0x30, QS_OK }, { 7, 8, QS_OK }, { 24, 4, QS_OK }, { 28, 4, QS_OK }, { 30, 8, QS_PORT_NOT_SET },
        { 31, 4, QS_OK }, { 27, 0x400, QS_OK }, { 12, 4, QS_OK }, { 81, 0x30, QS_ACCESS_DENIED }, { 0xFFFF, 4, QS_BAD_CLASS },
    };
    for (int i = 0; i < 10; i++) {
        NTSTATUS st = 0x55;
        int ok = qp_run_il(0x1000, t[i].id, t[i].len, &st);
        t_check(ok && st == t[i].want, __FILE__, __LINE__, "low integrity", "class %lu ok=%d st 0x%lx", t[i].id, ok, (unsigned long)st);
    }
}
T5(restricted_length_and_class_rules_are_unchanged_at_low_integrity) {
    NEED_Q();
    if (!have_il_child()) return;
    NTSTATUS st = 0;
    CHECK(qp_run_il(0x1000, 24, 5, &st)); EXPECT_EQ(st, QS_LEN_MISMATCH);
    CHECK(qp_run_il(0x1000, 32, 4, &st)); EXPECT_EQ(st, QS_LEN_MISMATCH);
    CHECK(qp_run_il(0x1000, 0x7FFFFFFF, 4, &st)); EXPECT_EQ(st, QS_BAD_CLASS);
}
T5(caller_integrity_is_known) {
    DWORD rid = 0;
    if (!qp_integrity(&rid)) { t_skip("token integrity unreadable"); return; }
    t_note("integrity rid 0x%lx", rid);
    CHECK(rid >= 0x1000);
}
