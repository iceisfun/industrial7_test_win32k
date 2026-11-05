/* NtQueryInformationProcess class 52 (mitigation policy) - the front door only.
 *
 * Measures the call protocol of the class, not the policy contents (those are covered by the mitigation and
 * lifecycle areas): the buffer is exactly 8 bytes, the first dword carries the policy id as INPUT, ids 0, 5 and
 * everything above 16 are refused with STATUS_NOT_SUPPORTED, the second dword is the OUTPUT, and on any error the
 * output dword is left exactly as the caller set it. The pseudo handle and a real handle with
 * PROCESS_QUERY_LIMITED_INFORMATION behave the same. Length is checked before the handle, the handle before the
 * policy id; ReturnLength is never written by this class.
 */
#include "qh.h"

static ULONG q52(HANDLE h, ULONG id, ULONG sentinel, ULONG *out4, ULONG *rl) {
    unsigned char b[8 + 8] __attribute__((aligned(8)));
    memset(b, 0xEE, sizeof b);
    memcpy(b, &id, 4); memcpy(b + 4, &sentinel, 4);
    ULONG st = qh(h, 52, b, 8, rl);
    if (out4) memcpy(out4, b + 4, 4);
    CHECKF(b[8] == 0xEE && b[15] == 0xEE, "bytes past the 8-byte buffer touched");
    ULONG d0; memcpy(&d0, b, 4);
    CHECKF(d0 == id, "input dword changed: id %lu -> %lu", id, d0);
    return st;
}

TEST(m52_len_mismatch_table) {
    static const ULONG lens[] = {0, 1, 2, 3, 4, 5, 7, 9, 12, 16, 0x20, 0x1b0, 0x1000};
    unsigned char in[8] = {2, 0, 0, 0, 0x55, 0x55, 0x55, 0x55};
    HANDLE lim = qh_open(PROCESS_QUERY_LIMITED_INFORMATION, 0);
    for (int i = 0; i < sizeof lens / sizeof *lens; i++) {
        qh_err("self", SELF, 52, lens[i], ST_LEN, in, 8, 8);
        if (lim) qh_err("lim", lim, 52, lens[i], ST_LEN, in, 8, 8);
    }
    if (lim) CloseHandle(lim);
}

TEST(m52_unsupported_ids) {
    static const ULONG ids[] = {0, 5, 17, 18, 19, 20, 100, 0xFF, 0x100, 0xFFFF, 0x10000, 0x7FFFFFFF, 0x80000000, 0x80000001, 0xFFFFFFFF};
    HANDLE lim = qh_open(PROCESS_QUERY_LIMITED_INFORMATION, 0);
    for (int i = 0; i < sizeof ids / sizeof *ids; i++) {
        ULONG o, rl;
        ULONG st = q52(SELF, ids[i], 0x55555555, &o, &rl);
        CHECKF(st == ST_NOT_SUPPORTED, "pseudo id 0x%lx status 0x%lx", ids[i], st);
        CHECKF(o == 0x55555555, "id 0x%lx output dword changed to 0x%lx", ids[i], o);
        CHECKF(rl == RL_UNSET, "id 0x%lx ReturnLength written", ids[i]);
        if (lim) { st = q52(lim, ids[i], 0x55555555, &o, &rl); CHECKF(st == ST_NOT_SUPPORTED, "real id 0x%lx status 0x%lx", ids[i], st); CHECKF(o == 0x55555555, "real id 0x%lx out changed", ids[i]); }
    }
    if (lim) CloseHandle(lim);
}

TEST(m52_supported_ids_succeed) {
    for (ULONG id = 1; id <= 16; id++) {
        if (id == 5) continue;
        ULONG o, rl;
        ULONG st = q52(SELF, id, 0x55555555, &o, &rl);
        CHECKF(st == ST_OK, "id %lu status 0x%lx", id, st);
        CHECKF(o != 0x55555555, "id %lu output dword not written", id);
        CHECKF(rl == RL_UNSET, "id %lu ReturnLength written (0x%lx)", id, rl);
    }
}

TEST(m52_pseudo_vs_real_lim_same_answer) {
    HANDLE lim = qh_open(PROCESS_QUERY_LIMITED_INFORMATION, 0);
    REQUIRE(lim);
    for (ULONG id = 0; id <= 20; id++) {
        ULONG o1, o2, s1 = q52(SELF, id, 0x12345678, &o1, NULL), s2 = q52(lim, id, 0x12345678, &o2, NULL);
        CHECKF(s1 == s2, "id %lu: pseudo 0x%lx real 0x%lx", id, s1, s2);
        CHECKF(o1 == o2, "id %lu: pseudo out 0x%lx real out 0x%lx", id, o1, o2);
    }
    CloseHandle(lim);
}

TEST(m52_handle_errors) {
    HANDLE closed = NULL, ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    HANDLE sync = qh_open(SYNCHRONIZE, 0), vmr = qh_open(PROCESS_VM_READ, 0), term = qh_open(PROCESS_TERMINATE, 0);
    closed = qh_closed_handle();   /* last: nothing may be opened after it, or the value would be reused */
    unsigned char in[8] = {2, 0, 0, 0, 0x55, 0x55, 0x55, 0x55};
    qh_err("null", NULL, 52, 8, ST_INV_HANDLE, in, 8, 8);
    if (closed) qh_err("closed", closed, 52, 8, ST_INV_HANDLE, in, 8, 8);
    qh_err("thread pseudo", THREAD_PSEUDO, 52, 8, ST_TYPE_MISMATCH, in, 8, 8);
    qh_err("event", ev, 52, 8, ST_TYPE_MISMATCH, in, 8, 8);
    if (sync) qh_err("sync only", sync, 52, 8, ST_DENIED, in, 8, 8);
    if (vmr) qh_err("vm read only", vmr, 52, 8, ST_DENIED, in, 8, 8);
    if (term) qh_err("terminate only", term, 52, 8, ST_DENIED, in, 8, 8);
    CloseHandle(ev); if (sync) CloseHandle(sync); if (vmr) CloseHandle(vmr); if (term) CloseHandle(term);
}

TEST(m52_length_checked_before_handle) {
    unsigned char in[8] = {2, 0, 0, 0, 0x55, 0x55, 0x55, 0x55};
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    qh_err("null+len7", NULL, 52, 7, ST_LEN, in, 8, 8);
    qh_err("thread+len9", THREAD_PSEUDO, 52, 9, ST_LEN, in, 8, 8);
    qh_err("event+len0", ev, 52, 0, ST_LEN, in, 8, 8);
    CloseHandle(ev);
}

TEST(m52_id_checked_after_handle) {
    /* an unsupported id with an invalid handle reports the handle problem (observed order) */
    unsigned char in[8] = {0, 0, 0, 0, 0x55, 0x55, 0x55, 0x55};
    ULONG st0 = qh(NULL, 52, in, 8, NULL);
    CHECKF(st0 == ST_INV_HANDLE, "id 0 with null handle: status 0x%lx", st0);
    unsigned char big[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0x55, 0x55, 0x55, 0x55};
    CHECKF(qh(THREAD_PSEUDO, 52, big, 8, NULL) == ST_TYPE_MISMATCH, "id 0xFFFFFFFF with a thread handle");
}

TEST(m52_misaligned_buffer) {
    static unsigned char raw[64] __attribute__((aligned(16)));
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    for (int off = 1; off < 4; off++) {
        memset(raw, 0xEE, sizeof raw); raw[16 + off] = 2;
        ULONG rl;
        CHECKF(qh(SELF, 52, raw + 16 + off, 8, &rl) == ST_MISALIGN, "off %d", off);
        CHECKF(rl == RL_UNSET, "off %d rl", off);
        CHECKF(qh(NULL, 52, raw + 16 + off, 8, NULL) == ST_MISALIGN, "null handle off %d: alignment is judged first", off);
        CHECKF(qh(ev, 52, raw + 16 + off, 8, NULL) == ST_MISALIGN, "event off %d", off);
        CHECKF(qh(SELF, 52, raw + 16 + off, 7, NULL) == ST_MISALIGN, "wrong len off %d: alignment first", off);
    }
    memset(raw, 0xEE, sizeof raw); raw[20] = 2; raw[21] = raw[22] = raw[23] = raw[24] = 0;
    CHECKF(qh(SELF, 52, raw + 20, 8, NULL) == ST_OK, "4-aligned (not 8-aligned) buffer is accepted");
    CloseHandle(ev);
}

TEST(m52_null_return_length_ok) {
    unsigned char in[8] = {2, 0, 0, 0, 0x55, 0x55, 0x55, 0x55};
    CHECKF(qh(SELF, 52, in, 8, NULL) == ST_OK, "NULL ReturnLength");
    CHECKF(qh(SELF, 52, NULL, 0, NULL) == ST_LEN, "NULL buffer len 0");
}

TEST(m52_foreign_child_rights) {
    struct qh_child c;
    if (!qh_child_start(&c, QC_PLAIN, NULL)) { t_skip("cannot start child"); return; }
    static const struct { DWORD rights; ULONG want; const char *n; } T[] = {
        {PROCESS_QUERY_LIMITED_INFORMATION, ST_OK, "lim"}, {PROCESS_QUERY_INFORMATION, ST_OK, "qry"},
        {PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, ST_OK, "qry+vmr"},
        {PROCESS_VM_READ, ST_DENIED, "vmr"}, {PROCESS_TERMINATE, ST_DENIED, "terminate"}, {PROCESS_VM_WRITE, ST_DENIED, "vmw"},
        {SYNCHRONIZE, ST_DENIED, "sync"},
    };
    for (int i = 0; i < sizeof T / sizeof *T; i++) {
        HANDLE h = qh_open(T[i].rights, c.pid);
        if (!h) { t_note("open %s refused", T[i].n); continue; }
        ULONG o, st = q52(h, 2, 0x55555555, &o, NULL);
        CHECKF(st == T[i].want, "%s: 0x%lx want 0x%lx", T[i].n, st, T[i].want);
        if (st != ST_OK) CHECKF(o == 0x55555555, "%s: out changed on error", T[i].n);
        CloseHandle(h);
    }
    qh_child_stop(&c);
}

TEST(m52_child_process_restriction_visible_from_parent) {
    struct qh_child c;
    if (!qh_child_start(&c, QC_CHILD_RESTRICTED, NULL)) { t_skip("cannot start restricted child"); return; }
    HANDLE h = qh_open(PROCESS_QUERY_LIMITED_INFORMATION, c.pid);
    if (h) {
        ULONG o = 0, st = q52(h, 13, 0, &o, NULL);
        CHECKF(st == ST_OK, "status 0x%lx", st);
        CHECKF(o == 1, "child-process policy flags 0x%lx want 1 (no child creation)", o);
        ULONG o2; st = q52(SELF, 13, 0, &o2, NULL);
        CHECKF(st == ST_OK && o2 == 0, "own process: status 0x%lx flags 0x%lx", st, o2);
        CloseHandle(h);
    }
    qh_child_stop(&c);
}

TEST(m52_terminated_child_still_answers) {
    struct qh_child c;
    if (!qh_child_start(&c, QC_PLAIN, NULL)) { t_skip("cannot start child"); return; }
    HANDLE h = qh_open(PROCESS_QUERY_LIMITED_INFORMATION, c.pid);
    REQUIRE(h);
    TerminateProcess(c.h, 0); WaitForSingleObject(c.h, 5000);
    ULONG o, st = q52(h, 2, 0x55555555, &o, NULL);
    CHECKF(st == ST_OK, "status 0x%lx", st);
    CloseHandle(h);
    qh_child_stop(&c);
}
