/* The write side of the debug classes and their interplay with the queries, on Windows 10 22H2 x64:
 *  - the debug flags (class 31) can be set on the own process or on a helper through a handle with the
 *    set-information right: 0 and 1 are accepted, every other value is an invalid parameter, only a 4-byte buffer
 *    is accepted, and the set right is a different right from the query right (neither implies the other);
 *  - the handle tracing database (class 32): enabling takes an all-zero 4-byte or 8-byte flag block, a zero-length
 *    call disables, a query selects one handle through the first 8 bytes of its buffer, newest event first,
 *    header-then-entries layout of 0x10 + n * 0xA0 bytes;
 *  - queries on a helper that has already exited.
 * Only the test process and its helpers are touched; tracing is always switched off again and the debug flags are
 * restored to their default before a test returns. */
#include "qp.h"

typedef NTSTATUS (NTAPI *qp_sfn)(HANDLE, ULONG, PVOID, ULONG);
static qp_sfn SIP(void) {
    static qp_sfn f;
    if (!f) f = (qp_sfn)t_proc_in("ntdll.dll", "NtSetInformationProcess");
    return f;
}
#define NEED_QS() do { if (!QIP || !SIP()) { t_skip("NtQueryInformationProcess/NtSetInformationProcess missing"); return; } } while (0)
#define SPAWN(mode) PROCESS_INFORMATION pi; HANDLE ph = qp_spawn(mode, &pi); if (!ph) { t_skip("cannot start helper process"); return; }

static NTSTATUS set_flags(HANDLE h, ULONG v) { return SIP()(h, 31, &v, 4); }
static ULONG get_flags(HANDLE h, NTSTATUS *st) { ULONG v = 0x77; *st = QIP(h, 31, &v, 4, NULL); return v; }
static HANDLE open_self(DWORD acc) { return OpenProcess(acc, FALSE, GetCurrentProcessId()); }

/* ---- class 31, set side ----------------------------------------------------------------------------- */
T5(set31_zero_and_one_round_trip) {
    NEED_QS(); NTSTATUS st;
    EXPECT_EQ(set_flags(QP_PSEUDO_SELF, 0), QS_OK);
    EXPECT_EQ(get_flags(QP_PSEUDO_SELF, &st), 0); EXPECT_EQ(st, QS_OK);
    EXPECT_EQ(set_flags(QP_PSEUDO_SELF, 1), QS_OK);
    EXPECT_EQ(get_flags(QP_PSEUDO_SELF, &st), 1); EXPECT_EQ(st, QS_OK);
    EXPECT_EQ(set_flags(QP_PSEUDO_SELF, 1), QS_OK);
    EXPECT_EQ(get_flags(QP_PSEUDO_SELF, &st), 1);
}
T5(set31_other_values_are_invalid_parameters_and_change_nothing) {
    NEED_QS(); NTSTATUS st;
    static const ULONG bad[] = { 2, 3, 4, 0x10, 0x100, 0x80000000, 0x80000001, 0xFFFFFFFE, 0xFFFFFFFF };
    for (int s = 0; s < 2; s++) {
        set_flags(QP_PSEUDO_SELF, (ULONG)s);
        for (int i = 0; i < 9; i++) {
            NTSTATUS r = set_flags(QP_PSEUDO_SELF, bad[i]);
            t_check(r == QS_BAD_PARAM, __FILE__, __LINE__, "bad value", "state %d value 0x%lx st 0x%lx", s, bad[i], (unsigned long)r);
            ULONG v = get_flags(QP_PSEUDO_SELF, &st);
            t_check(st == QS_OK && v == (ULONG)s, __FILE__, __LINE__, "unchanged", "state %d value 0x%lx now %lu", s, bad[i], v);
        }
    }
    set_flags(QP_PSEUDO_SELF, 1);
}
T5(set31_only_a_four_byte_buffer_is_accepted) {
    NEED_QS();
    for (ULONG l = 0; l <= 0x10; l++) {
        ULONG cell[8] = { 1 };
        set_flags(QP_PSEUDO_SELF, 1);
        NTSTATUS st = SIP()(QP_PSEUDO_SELF, 31, cell, l);
        t_check(st == (l == 4 ? QS_OK : QS_LEN_MISMATCH), __FILE__, __LINE__, "set length", "len %lu st 0x%lx", l, (unsigned long)st);
    }
    set_flags(QP_PSEUDO_SELF, 1);
}
T5(set31_value_is_checked_after_the_length) {
    NEED_QS();
    ULONG cell[4] = { 2, 2, 2, 2 };
    EXPECT_EQ(SIP()(QP_PSEUDO_SELF, 31, cell, 8), QS_LEN_MISMATCH);
    EXPECT_EQ(SIP()(QP_PSEUDO_SELF, 31, cell, 4), QS_BAD_PARAM);
}
T5(set31_needs_the_set_information_right) {
    NEED_QS();
    static const DWORD acc[] = { PROCESS_QUERY_LIMITED_INFORMATION, PROCESS_QUERY_INFORMATION, SYNCHRONIZE, PROCESS_VM_READ,
        PROCESS_QUERY_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION, PROCESS_SET_INFORMATION, PROCESS_SET_INFORMATION | PROCESS_QUERY_INFORMATION };
    static const NTSTATUS want[] = { QS_ACCESS_DENIED, QS_ACCESS_DENIED, QS_ACCESS_DENIED, QS_ACCESS_DENIED, QS_ACCESS_DENIED, QS_OK, QS_OK };
    for (int i = 0; i < 7; i++) {
        HANDLE h = open_self(acc[i]);
        if (!h) { t_skip("OpenProcess 0x%lx failed with %lu", acc[i], GetLastError()); continue; }
        NTSTATUS st = set_flags(h, 1);
        t_check(st == want[i], __FILE__, __LINE__, "set right", "access 0x%lx st 0x%lx", acc[i], (unsigned long)st);
        CloseHandle(h);
    }
}
T5(set31_handle_checks) {
    NEED_QS();
    EXPECT_EQ(set_flags(NULL, 1), QS_BAD_HANDLE);
    EXPECT_EQ(set_flags(QP_PSEUDO_THREAD, 1), QS_TYPE_MISMATCH);
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    EXPECT_EQ(set_flags(ev, 1), QS_TYPE_MISMATCH);
    CloseHandle(ev);
    EXPECT_EQ(set_flags(qp_hmake(H_CLOSED, NULL), 1), QS_BAD_HANDLE);
}
T5(query_rights_do_not_include_the_set_right) {
    NEED_QS();
    HANDLE h = open_self(PROCESS_SET_INFORMATION);
    if (!h) { t_skip("OpenProcess failed with %lu", GetLastError()); return; }
    static const struct { ULONG id, len; } q[] = { { 7, 8 }, { 30, 8 }, { 31, 4 }, { 32, 0x10 }, { 0, 0x30 } };
    for (int i = 0; i < 5; i++) {
        unsigned char *b = qp_buf();
        NTSTATUS st = QIP(h, q[i].id, b, q[i].len, NULL);
        t_check(st == QS_ACCESS_DENIED, __FILE__, __LINE__, "set right only", "class %lu st 0x%lx", q[i].id, (unsigned long)st);
    }
    CloseHandle(h);
}
T5(set31_on_a_helper) {
    NEED_QS(); SPAWN('n');
    HANDLE sh = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pi.dwProcessId);
    HANDLE q = qp_hmake(H_CH_QRY, ph);
    NTSTATUS st;
    if (sh) {
        EXPECT_EQ(get_flags(q, &st), 1);
        EXPECT_EQ(set_flags(sh, 0), QS_OK);
        EXPECT_EQ(get_flags(q, &st), 0); EXPECT_EQ(st, QS_OK);
        EXPECT_EQ(get_flags(QP_PSEUDO_SELF, &st), 1);   /* the caller's own flag is not touched */
        EXPECT_EQ(set_flags(sh, 1), QS_OK);
        EXPECT_EQ(get_flags(q, &st), 1);
        EXPECT_EQ(set_flags(sh, 5), QS_BAD_PARAM);
        CloseHandle(sh);
    } else t_skip("OpenProcess set-information on the helper failed with %lu", GetLastError());
    HANDLE lim = qp_hmake(H_CH_LIM, ph);
    EXPECT_EQ(set_flags(lim, 0), QS_ACCESS_DENIED);
    EXPECT_EQ(set_flags(q, 0), QS_ACCESS_DENIED);
    qp_hrelease(H_CH_LIM, lim); qp_hrelease(H_CH_QRY, q); qp_kill(ph, &pi);
}
T5(set31_on_a_debugged_helper) {
    NEED_QS(); SPAWN('n');
    HANDLE sh = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pi.dwProcessId);
    HANDLE q = qp_hmake(H_CH_QRY, ph);
    if (sh && DebugActiveProcess(pi.dwProcessId)) {
        NTSTATUS st;
        EXPECT_EQ(get_flags(q, &st), 0);               /* attached: reads as NoDebugInherit */
        EXPECT_EQ(set_flags(sh, 1), QS_OK);
        EXPECT_EQ(get_flags(q, &st), 1);
        EXPECT_EQ(set_flags(sh, 0), QS_OK);
        EXPECT_EQ(get_flags(q, &st), 0);
        DEBUG_EVENT ev; int n = 0;
        DebugSetProcessKillOnExit(FALSE);
        while (n++ < 200 && WaitForDebugEvent(&ev, 100)) ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, DBG_CONTINUE);
        DebugActiveProcessStop(pi.dwProcessId);
    } else t_skip("cannot attach to the helper");
    if (sh) CloseHandle(sh);
    qp_hrelease(H_CH_QRY, q); qp_kill(ph, &pi);
}

/* ---- queries on a helper that has exited -------------------------------------------------------------- */
T5(exited_helper_debug_queries) {
    NEED_QS(); SPAWN('n');
    HANDLE q = qp_hmake(H_CH_QRY, ph);
    qp_kill(ph, &pi);
    ULONG_PTR port = 0x55; HANDLE obj = (HANDLE)0x55; ULONG fl = 0x55; NTSTATUS st;
    st = QIP(q, 7, &port, 8, NULL);   EXPECT_EQ(st, QS_OK); EXPECT_EQ(port, 0);
    st = QIP(q, 30, &obj, 8, NULL);   EXPECT_EQ(st, QS_PORT_NOT_SET); EXPECT_EQ(obj, 0);
    st = QIP(q, 31, &fl, 4, NULL);    EXPECT_EQ(st, QS_OK); EXPECT_EQ(fl, 1);
    unsigned char *b = qp_buf();
    st = QIP(q, 32, b, 0x10, NULL);   EXPECT_EQ(st, (NTSTATUS)0xC000010A);
    qp_hrelease(H_CH_QRY, q);
}
T5(exited_helper_length_errors_still_come_first) {
    NEED_QS(); SPAWN('n');
    HANDLE q = qp_hmake(H_CH_QRY, ph);
    qp_kill(ph, &pi);
    ULONG_PTR port = 0;
    EXPECT_EQ(QIP(q, 7, &port, 4, NULL), QS_LEN_MISMATCH);
    EXPECT_EQ(QIP(q, 32, qp_buf(), 4, NULL), QS_LEN_MISMATCH);
    qp_hrelease(H_CH_QRY, q);
}

/* ---- class 32, handle tracing ------------------------------------------------------------------------- */
static NTSTATUS trace_on(HANDLE h) { ULONG f = 0; return SIP()(h, 32, &f, 4); }
static NTSTATUS trace_off(HANDLE h) { return SIP()(h, 32, NULL, 0); }
#define TE_SIZE 0xA0
/* one query selecting handle `sel` (0 = every handle); buffer and return length as the caller supplies them */
static NTSTATUS tq(HANDLE h, ULONG_PTR sel, ULONG len, ULONG *rl, unsigned char **bp) {
    unsigned char *b = qp_buf();
    memset(b, 0, 0x2000);
    *(ULONG_PTR *)b = sel;
    *rl = QP_SENT;
    *bp = b;
    return QIP(h, 32, b, len, rl);
}
static ULONG ent_type(unsigned char *b, ULONG k) { return *(ULONG *)(b + 0x10 + k * TE_SIZE + 0x18); }
static ULONG_PTR ent_handle(unsigned char *b, ULONG k) { return *(ULONG_PTR *)(b + 0x10 + k * TE_SIZE); }

T5(trace_enable_takes_only_a_zero_flag_block) {
    NEED_QS();
    static const ULONG bad[] = { 1, 2, 3, 0x10, 0x80000000, 0xFFFFFFFF };
    for (int i = 0; i < 6; i++) {
        ULONG f = bad[i];
        NTSTATUS st = SIP()(QP_PSEUDO_SELF, 32, &f, 4);
        t_check(st == QS_BAD_PARAM, __FILE__, __LINE__, "flags", "flags 0x%lx st 0x%lx", f, (unsigned long)st);
        ULONG two[2] = { f, 4 };
        st = SIP()(QP_PSEUDO_SELF, 32, two, 8);
        t_check(st == QS_BAD_PARAM, __FILE__, __LINE__, "flags (8 bytes)", "flags 0x%lx st 0x%lx", f, (unsigned long)st);
    }
    unsigned char *b = qp_buf(); ULONG rl;
    EXPECT_EQ(tq(QP_PSEUDO_SELF, 0, 0x10, &rl, &b), QS_BAD_PARAM);   /* still switched off */
}
T5(trace_enable_and_disable_lengths) {
    NEED_QS();
    for (ULONG l = 0; l <= 16; l++) {
        ULONG cell[4] = { 0, 0, 0, 0 };
        NTSTATUS st = SIP()(QP_PSEUDO_SELF, 32, l ? cell : NULL, l);
        NTSTATUS want = (l == 0 || l == 4 || l == 8) ? QS_OK : QS_LEN_MISMATCH;
        t_check(st == want, __FILE__, __LINE__, "set length", "len %lu st 0x%lx", l, (unsigned long)st);
        trace_off(QP_PSEUDO_SELF);
    }
}
T5(trace_extended_block_slot_counts) {
    NEED_QS();
    static const ULONG slots[] = { 0, 1, 4, 0x100, 0x10000 };
    for (int i = 0; i < 5; i++) {
        ULONG two[2] = { 0, slots[i] };
        NTSTATUS st = SIP()(QP_PSEUDO_SELF, 32, two, 8);
        t_check(st == QS_OK, __FILE__, __LINE__, "slots", "slots 0x%lx st 0x%lx", slots[i], (unsigned long)st);
        trace_off(QP_PSEUDO_SELF);
    }
}
T5(trace_disable_is_idempotent_and_switches_queries_off) {
    NEED_QS(); unsigned char *b; ULONG rl;
    EXPECT_EQ(trace_off(QP_PSEUDO_SELF), QS_OK);
    EXPECT_EQ(trace_off(QP_PSEUDO_SELF), QS_OK);
    EXPECT_EQ(trace_on(QP_PSEUDO_SELF), QS_OK);
    EXPECT_EQ(tq(QP_PSEUDO_SELF, 0, 0x10, &rl, &b), QS_OK);
    EXPECT_EQ(trace_on(QP_PSEUDO_SELF), QS_OK);   /* enabling twice is fine */
    EXPECT_EQ(tq(QP_PSEUDO_SELF, 0, 0x10, &rl, &b), QS_OK);
    EXPECT_EQ(trace_off(QP_PSEUDO_SELF), QS_OK);
    EXPECT_EQ(tq(QP_PSEUDO_SELF, 0, 0x10, &rl, &b), QS_BAD_PARAM);
    EXPECT_EQ(rl, QP_SENT);
    EXPECT_EQ(trace_off(QP_PSEUDO_SELF), QS_OK);
}
T5(trace_set_needs_the_set_information_right) {
    NEED_QS();
    HANDLE q = open_self(PROCESS_QUERY_INFORMATION), l = open_self(PROCESS_QUERY_LIMITED_INFORMATION), s = open_self(PROCESS_SET_INFORMATION);
    if (q) EXPECT_EQ(trace_on(q), QS_ACCESS_DENIED);
    if (l) EXPECT_EQ(trace_on(l), QS_ACCESS_DENIED);
    if (q) EXPECT_EQ(trace_off(q), QS_ACCESS_DENIED);
    if (s) { EXPECT_EQ(trace_on(s), QS_OK); EXPECT_EQ(trace_off(s), QS_OK); }
    else t_skip("OpenProcess set-information failed with %lu", GetLastError());
    trace_off(QP_PSEUDO_SELF);
    if (q) CloseHandle(q);
    if (l) CloseHandle(l);
    if (s) CloseHandle(s);
}
T5(trace_set_length_is_checked_before_the_handle) {
    NEED_QS();
    ULONG f[4] = { 0 };
    EXPECT_EQ(SIP()(NULL, 32, f, 4), QS_BAD_HANDLE);
    EXPECT_EQ(SIP()(NULL, 32, f, 3), QS_LEN_MISMATCH);
    EXPECT_EQ(SIP()(QP_PSEUDO_THREAD, 32, f, 4), QS_TYPE_MISMATCH);
}
T5(trace_open_and_close_are_recorded_newest_first) {
    NEED_QS(); unsigned char *b; ULONG rl;
    EXPECT_EQ(trace_on(QP_PSEUDO_SELF), QS_OK);
    HANDLE e = CreateEventA(NULL, TRUE, FALSE, NULL);
    ULONG_PTR key = (ULONG_PTR)e;
    EXPECT_EQ(tq(QP_PSEUDO_SELF, key, 0x1000, &rl, &b), QS_OK);
    EXPECT_EQ(*(ULONG *)(b + 8), 1); EXPECT_EQ(rl, 0x10 + TE_SIZE);
    EXPECT_EQ(ent_handle(b, 0), key); EXPECT_EQ(ent_type(b, 0), 1);
    CloseHandle(e);
    EXPECT_EQ(tq(QP_PSEUDO_SELF, key, 0x1000, &rl, &b), QS_OK);
    EXPECT_EQ(*(ULONG *)(b + 8), 2); EXPECT_EQ(rl, 0x10 + 2 * TE_SIZE);
    EXPECT_EQ(ent_handle(b, 0), key); EXPECT_EQ(ent_type(b, 0), 2);
    EXPECT_EQ(ent_handle(b, 1), key); EXPECT_EQ(ent_type(b, 1), 1);
    EXPECT_EQ(trace_off(QP_PSEUDO_SELF), QS_OK);
}
T5(trace_entries_carry_the_caller_identity_and_a_stack) {
    NEED_QS(); unsigned char *b; ULONG rl;
    EXPECT_EQ(trace_on(QP_PSEUDO_SELF), QS_OK);
    HANDLE e = CreateEventA(NULL, TRUE, FALSE, NULL);
    EXPECT_EQ(tq(QP_PSEUDO_SELF, (ULONG_PTR)e, 0x1000, &rl, &b), QS_OK);
    EXPECT_EQ(*(ULONG_PTR *)(b + 0x10 + 8), GetCurrentProcessId());
    EXPECT_EQ(*(ULONG_PTR *)(b + 0x10 + 16), GetCurrentThreadId());
    CHECK(*(ULONG_PTR *)(b + 0x10 + 0x20) != 0);
    EXPECT_EQ(*(ULONG *)(b + 0x10 + 0x1C), 0);
    CloseHandle(e);
    trace_off(QP_PSEUDO_SELF);
}
T5(trace_header_handle_field_is_not_changed) {
    NEED_QS(); unsigned char *b; ULONG rl;
    trace_on(QP_PSEUDO_SELF);
    HANDLE e = CreateEventA(NULL, TRUE, FALSE, NULL);
    EXPECT_EQ(tq(QP_PSEUDO_SELF, (ULONG_PTR)e, 0x1000, &rl, &b), QS_OK);
    EXPECT_EQ(*(ULONG_PTR *)b, (ULONG_PTR)e);
    EXPECT_EQ(*(ULONG *)(b + 12), 0);
    CloseHandle(e);
    trace_off(QP_PSEUDO_SELF);
}
T5(trace_unknown_handle_selects_nothing) {
    NEED_QS(); unsigned char *b; ULONG rl;
    trace_on(QP_PSEUDO_SELF);
    HANDLE e = CreateEventA(NULL, TRUE, FALSE, NULL); CloseHandle(e);
    static const ULONG_PTR sel[] = { 0x7FFC, 0xCCCCCCCCCCCCCCCCull, 0xFFFFFFFFFFFFFFFFull };
    for (int i = 0; i < 3; i++) {
        NTSTATUS st = tq(QP_PSEUDO_SELF, sel[i], 0x1000, &rl, &b);
        t_check(st == QS_OK && rl == 0x10 && *(ULONG *)(b + 8) == 0 && ent_handle(b, 0) == 0, __FILE__, __LINE__, "unknown handle", "sel 0x%llx st 0x%lx rl 0x%lx", (unsigned long long)sel[i], (unsigned long)st, rl);
    }
    trace_off(QP_PSEUDO_SELF);
}
T5(trace_short_buffers_report_the_count_and_what_fits) {
    NEED_QS(); unsigned char *b; ULONG rl;
    trace_on(QP_PSEUDO_SELF);
    HANDLE e = CreateEventA(NULL, TRUE, FALSE, NULL); ULONG_PTR key = (ULONG_PTR)e; CloseHandle(e);
    /* two events for the handle: a buffer for fewer than two entries is a length mismatch that still reports */
    for (ULONG l = 0x10; l < 0x10 + 2 * TE_SIZE; l += (l == 0x10 ? 1 : 0x37)) {
        NTSTATUS st = tq(QP_PSEUDO_SELF, key, l, &rl, &b);
        ULONG fit = (l - 0x10) / TE_SIZE;
        t_check(st == QS_LEN_MISMATCH && *(ULONG *)(b + 8) == 2 && rl == 0x10 + fit * TE_SIZE, __FILE__, __LINE__, "short", "len 0x%lx st 0x%lx n %lu rl 0x%lx", l, (unsigned long)st, *(ULONG *)(b + 8), rl);
    }
    NTSTATUS st = tq(QP_PSEUDO_SELF, key, 0x10 + 2 * TE_SIZE - 1, &rl, &b);
    t_check(st == QS_LEN_MISMATCH && rl == 0x10 + TE_SIZE && ent_type(b, 0) == 2, __FILE__, __LINE__, "one byte short", "st 0x%lx rl 0x%lx", (unsigned long)st, rl);
    st = tq(QP_PSEUDO_SELF, key, 0x10 + 2 * TE_SIZE, &rl, &b);
    t_check(st == QS_OK && rl == 0x10 + 2 * TE_SIZE, __FILE__, __LINE__, "exact", "st 0x%lx rl 0x%lx", (unsigned long)st, rl);
    st = tq(QP_PSEUDO_SELF, key, 0x10 + 2 * TE_SIZE + 7, &rl, &b);
    t_check(st == QS_OK && rl == 0x10 + 2 * TE_SIZE, __FILE__, __LINE__, "longer", "st 0x%lx rl 0x%lx", (unsigned long)st, rl);
    trace_off(QP_PSEUDO_SELF);
}
T5(trace_below_the_header_size_is_a_length_mismatch_without_writes) {
    NEED_QS(); unsigned char *b; ULONG rl;
    trace_on(QP_PSEUDO_SELF);
    for (ULONG l = 0; l < 0x10; l++) {
        NTSTATUS st = tq(QP_PSEUDO_SELF, 0, l, &rl, &b);
        t_check(st == QS_LEN_MISMATCH && rl == QP_SENT && *(ULONG *)(b + 8) == 0, __FILE__, __LINE__, "short header", "len %lu st 0x%lx", l, (unsigned long)st);
    }
    trace_off(QP_PSEUDO_SELF);
}
T5(trace_query_through_a_real_handle_matches_the_pseudo_handle) {
    NEED_QS(); unsigned char *b; ULONG rl;
    trace_on(QP_PSEUDO_SELF);
    HANDLE e = CreateEventA(NULL, TRUE, FALSE, NULL); ULONG_PTR key = (ULONG_PTR)e; CloseHandle(e);
    HANDLE h = open_self(PROCESS_QUERY_INFORMATION);
    if (h) {
        EXPECT_EQ(tq(h, key, 0x1000, &rl, &b), QS_OK);
        EXPECT_EQ(*(ULONG *)(b + 8), 2); EXPECT_EQ(rl, 0x10 + 2 * TE_SIZE);
        CloseHandle(h);
    }
    trace_off(QP_PSEUDO_SELF);
}
T5(trace_enabling_again_starts_a_new_log) {
    NEED_QS(); unsigned char *b; ULONG rl;
    trace_on(QP_PSEUDO_SELF);
    HANDLE e = CreateEventA(NULL, TRUE, FALSE, NULL); ULONG_PTR key = (ULONG_PTR)e; CloseHandle(e);
    EXPECT_EQ(tq(QP_PSEUDO_SELF, key, 0x1000, &rl, &b), QS_OK); EXPECT_EQ(*(ULONG *)(b + 8), 2);
    trace_on(QP_PSEUDO_SELF);
    EXPECT_EQ(tq(QP_PSEUDO_SELF, key, 0x1000, &rl, &b), QS_OK); EXPECT_EQ(*(ULONG *)(b + 8), 0);
    trace_off(QP_PSEUDO_SELF);
}
T5(trace_disabling_discards_the_log) {
    NEED_QS(); unsigned char *b; ULONG rl;
    trace_on(QP_PSEUDO_SELF);
    HANDLE e = CreateEventA(NULL, TRUE, FALSE, NULL); ULONG_PTR key = (ULONG_PTR)e; CloseHandle(e);
    trace_off(QP_PSEUDO_SELF);
    trace_on(QP_PSEUDO_SELF);
    EXPECT_EQ(tq(QP_PSEUDO_SELF, key, 0x1000, &rl, &b), QS_OK); EXPECT_EQ(*(ULONG *)(b + 8), 0);
    trace_off(QP_PSEUDO_SELF);
}
T5(trace_unfiltered_query_counts_every_handle_event) {
    NEED_QS(); unsigned char *b; ULONG rl;
    trace_on(QP_PSEUDO_SELF);
    HANDLE e[3];
    for (int i = 0; i < 3; i++) e[i] = CreateEventA(NULL, TRUE, FALSE, NULL);
    for (int i = 0; i < 3; i++) CloseHandle(e[i]);
    EXPECT_EQ(tq(QP_PSEUDO_SELF, 0, 0x2000, &rl, &b), QS_OK);
    ULONG n = *(ULONG *)(b + 8);
    CHECKF(n >= 6, "only %lu events", n);
    EXPECT_EQ(rl, 0x10 + n * TE_SIZE);
    int found = 0;
    for (ULONG k = 0; k < n && 0x10 + (k + 1) * TE_SIZE <= 0x2000; k++) for (int i = 0; i < 3; i++) if (ent_handle(b, k) == (ULONG_PTR)e[i]) found++;
    CHECKF(found >= 6, "found %d entries for the three handles", found);
    trace_off(QP_PSEUDO_SELF);
}
T5(trace_on_a_helper_leaves_the_caller_alone) {
    NEED_QS(); SPAWN('n'); unsigned char *b; ULONG rl;
    HANDLE sh = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_INFORMATION, FALSE, pi.dwProcessId);
    if (sh) {
        EXPECT_EQ(tq(sh, 0, 0x10, &rl, &b), QS_BAD_PARAM);
        EXPECT_EQ(trace_on(sh), QS_OK);
        EXPECT_EQ(tq(sh, 0, 0x10, &rl, &b), QS_OK);
        EXPECT_EQ(*(ULONG *)(b + 8), 0); EXPECT_EQ(rl, 0x10);
        EXPECT_EQ(tq(QP_PSEUDO_SELF, 0, 0x10, &rl, &b), QS_BAD_PARAM);   /* the caller's own tracing stays off */
        EXPECT_EQ(trace_off(sh), QS_OK);
        EXPECT_EQ(tq(sh, 0, 0x10, &rl, &b), QS_BAD_PARAM);
        CloseHandle(sh);
    } else t_skip("OpenProcess on the helper failed with %lu", GetLastError());
    qp_kill(ph, &pi);
}
T5(exited_helper_trace_and_flag_set_calls) {
    NEED_QS(); SPAWN('n');
    HANDLE sh = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pi.dwProcessId);
    qp_kill(ph, &pi);
    if (sh) {
        EXPECT_EQ(trace_on(sh), (NTSTATUS)0xC000010A);
        EXPECT_EQ(trace_off(sh), (NTSTATUS)0xC000010A);
        EXPECT_EQ(set_flags(sh, 1), QS_OK);   /* the flag is plain process state, still settable */
        EXPECT_EQ(set_flags(sh, 7), QS_BAD_PARAM);
        CloseHandle(sh);
    }
}

/* ---- the debug object handle returned by class 30 ------------------------------------------------------- */
T5(dbg30_handle_properties) {
    NEED_QS(); SPAWN('n');
    if (DebugActiveProcess(pi.dwProcessId)) {
        HANDLE q = qp_hmake(H_CH_QRY, ph), out = NULL;
        EXPECT_EQ(QIP(q, 30, &out, 8, NULL), QS_OK);
        if (out) {
            DWORD fl = 0xFF;
            CHECK(GetHandleInformation(out, &fl));
            CHECK(!(fl & (HANDLE_FLAG_INHERIT | HANDLE_FLAG_PROTECT_FROM_CLOSE)));
            typedef NTSTATUS (NTAPI *qo_t)(HANDLE, ULONG, PVOID, ULONG, PULONG);
            qo_t qo = (qo_t)t_proc_in("ntdll.dll", "NtQueryObject");
            struct { ULONG Attributes; ACCESS_MASK Access; ULONG HandleCount, PointerCount, rest[10]; } o;
            ULONG n = 0; memset(&o, 0, sizeof o);
            NTSTATUS os = qo ? qo(out, 0, &o, sizeof o, &n) : -1;
            CHECKF(os == 0, "NtQueryObject 0x%lx", (unsigned long)os);
            if (os == 0) t_note("debug object handle access 0x%lx, inherit attribute %d", (unsigned long)o.Access, !!(o.Attributes & 2));
            CloseHandle(out);
        }
        qp_hrelease(H_CH_QRY, q);
        DEBUG_EVENT ev; int k = 0;
        DebugSetProcessKillOnExit(FALSE);
        while (k++ < 200 && WaitForDebugEvent(&ev, 100)) ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, DBG_CONTINUE);
        DebugActiveProcessStop(pi.dwProcessId);
    } else t_skip("cannot attach to the helper");
    qp_kill(ph, &pi);
}
