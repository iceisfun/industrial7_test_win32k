/* NtQueryInformationProcess classes 72 (challenge response), 77 (activity throttle state), 81 (wake information)
 * and 84 (live dump).
 *
 *  81  every user-mode caller gets STATUS_ACCESS_DENIED, for every length, every handle (valid or not) and
 *      without writing anything; only a misaligned buffer is reported earlier (alignment is judged first).
 *  84  exactly 4 bytes (length first); a valid process handle, even with every query/operation right, is refused
 *      with STATUS_ACCESS_DENIED because the process is not a secure one; invalid handles report their own error.
 *  72  handled by a secure-kernel helper: the answer depends on the machine, so the status itself is a note;
 *      what is fixed is that the handle is validated and rights-checked first, and that every length gives the
 *      same status and no output.
 *  77  exactly 12 bytes; whether the throttling extension answers is machine dependent, so the layout checks run
 *      only when it does.
 */
#include "qh.h"

/* ------------------------------------------------------------------ 81 */
TEST(c81_denied_for_every_length) {
    static const ULONG lens[] = {0, 1, 3, 4, 8, 0x2f, 0x30, 0x31, 0x40, 0x1b0, 0x1000};
    HANDLE lim = qh_open(PROCESS_QUERY_LIMITED_INFORMATION, 0);
    for (int i = 0; i < sizeof lens / sizeof *lens; i++) {
        qh_err("pseudo", SELF, 81, lens[i], ST_DENIED, NULL, 0, 0);
        if (lim) qh_err("real lim", lim, 81, lens[i], ST_DENIED, NULL, 0, 0);
    }
    if (lim) CloseHandle(lim);
}
TEST(c81_denied_before_the_handle_is_looked_at) {
    HANDLE closed = NULL, ev = CreateEventA(NULL, TRUE, FALSE, NULL), sync = qh_open(SYNCHRONIZE, 0);
    closed = qh_closed_handle();   /* last: nothing may be opened after it, or the value would be reused */
    qh_err("null", NULL, 81, 0x30, ST_DENIED, NULL, 0, 0);
    if (closed) qh_err("closed", closed, 81, 0x30, ST_DENIED, NULL, 0, 0);
    qh_err("thread pseudo", THREAD_PSEUDO, 81, 0x30, ST_DENIED, NULL, 0, 0);
    qh_err("event", ev, 81, 0x30, ST_DENIED, NULL, 0, 0);
    if (sync) qh_err("sync", sync, 81, 0x30, ST_DENIED, NULL, 0, 0);
    qh_err("null buffer", SELF, 81, 0, ST_DENIED, NULL, 0, 0);
    CHECKF(qh(SELF, 81, NULL, 0, NULL) == ST_DENIED, "NULL ReturnLength");
    CloseHandle(ev); if (sync) CloseHandle(sync);
}
TEST(c81_denied_for_foreign_process_with_all_rights) {
    struct qh_child c;
    if (!qh_child_start(&c, QC_PLAIN, NULL)) { t_skip("cannot start child"); return; }
    HANDLE h = qh_open(PROCESS_QUERY_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, c.pid);
    if (h) { qh_err("child", h, 81, 0x30, ST_DENIED, NULL, 0, 0); CloseHandle(h); }
    qh_child_stop(&c);
}
TEST(c81_misaligned_buffer_reported_first) {
    static unsigned char raw[0x80] __attribute__((aligned(16)));
    for (int off = 1; off < 4; off++) {
        CHECKF(qh(SELF, 81, raw + 16 + off, 0x30, NULL) == ST_MISALIGN, "off %d", off);
        CHECKF(qh(NULL, 81, raw + 16 + off, 0x30, NULL) == ST_MISALIGN, "off %d null handle", off);
    }
    CHECKF(qh(SELF, 81, raw + 20, 0x30, NULL) == ST_DENIED, "4-aligned buffer reaches the refusal");
}

/* ------------------------------------------------------------------ 84 */
TEST(c84_length_table) {
    static const ULONG lens[] = {0, 1, 2, 3, 5, 8, 0x10, 0x20, 0x418, 0x1000};
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    for (int i = 0; i < sizeof lens / sizeof *lens; i++) {
        qh_err("pseudo", SELF, 84, lens[i], ST_LEN, NULL, 0, 0);
        qh_err("null handle", NULL, 84, lens[i], ST_LEN, NULL, 0, 0);
        qh_err("event", ev, 84, lens[i], ST_LEN, NULL, 0, 0);
    }
    CloseHandle(ev);
}
TEST(c84_valid_handles_refused_non_secure_process) {
    static const DWORD rights[] = {PROCESS_QUERY_LIMITED_INFORMATION, PROCESS_QUERY_INFORMATION, PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION,
                                   PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_READ, PROCESS_VM_OPERATION, PROCESS_VM_READ, SYNCHRONIZE};
    qh_err("pseudo", SELF, 84, 4, ST_DENIED, NULL, 0, 0);
    for (int i = 0; i < sizeof rights / sizeof *rights; i++) {
        HANDLE h = qh_open(rights[i], 0);
        if (h) { qh_err("self", h, 84, 4, ST_DENIED, NULL, 0, 0); CloseHandle(h); }
    }
    struct qh_child c;
    if (qh_child_start(&c, QC_PLAIN, NULL)) {
        for (int i = 0; i < sizeof rights / sizeof *rights; i++) {
            HANDLE h = qh_open(rights[i], c.pid);
            if (h) { qh_err("child", h, 84, 4, ST_DENIED, NULL, 0, 0); CloseHandle(h); }
        }
        qh_child_stop(&c);
    }
}
TEST(c84_invalid_handles_report_their_own_error) {
    HANDLE closed = NULL, ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    closed = qh_closed_handle();   /* last: nothing may be opened after it, or the value would be reused */
    qh_err("null", NULL, 84, 4, ST_INV_HANDLE, NULL, 0, 0);
    if (closed) qh_err("closed", closed, 84, 4, ST_INV_HANDLE, NULL, 0, 0);
    qh_err("thread pseudo", THREAD_PSEUDO, 84, 4, ST_TYPE_MISMATCH, NULL, 0, 0);
    qh_err("event", ev, 84, 4, ST_TYPE_MISMATCH, NULL, 0, 0);
    CloseHandle(ev);
}
TEST(c84_misaligned_buffer) {
    static unsigned char raw[64] __attribute__((aligned(16)));
    for (int off = 1; off < 4; off++) { CHECKF(qh(SELF, 84, raw + 16 + off, 4, NULL) == ST_MISALIGN, "off %d", off); CHECKF(qh(SELF, 84, raw + 16 + off, 8, NULL) == ST_MISALIGN, "off %d wrong len", off); }
}

/* ------------------------------------------------------------------ 72 */
TEST(c72_status_is_independent_of_length) {
    static const ULONG lens[] = {0, 1, 4, 8, 16, 0x20, 0x100, 0x1000};
    struct qh_res first = qh_probe(SELF, 72, 16, NULL, 0, 0);
    t_note("challenge response: status class %s", first.st == ST_OK ? "success" : first.st == ST_DEV_NOT_CONNECTED ? "device not connected" : "other");
    for (int i = 0; i < sizeof lens / sizeof *lens; i++) {
        struct qh_res r = qh_probe(SELF, 72, lens[i], NULL, 0, 0);
        CHECKF(r.st == first.st, "len %lu: 0x%lx vs 0x%lx", lens[i], r.st, first.st);
        if (first.st != ST_OK) { CHECKF(r.touched_cnt == 0, "len %lu: output written on error", lens[i]); CHECKF(r.rl == RL_UNSET, "len %lu: ReturnLength written", lens[i]); }
    }
}
TEST(c72_handle_validated_first) {
    HANDLE closed = NULL, ev = CreateEventA(NULL, TRUE, FALSE, NULL), sync = qh_open(SYNCHRONIZE, 0), vmr = qh_open(PROCESS_VM_READ, 0);
    closed = qh_closed_handle();   /* last: nothing may be opened after it, or the value would be reused */
    qh_err("null", NULL, 72, 16, ST_INV_HANDLE, NULL, 0, 0);
    if (closed) qh_err("closed", closed, 72, 16, ST_INV_HANDLE, NULL, 0, 0);
    qh_err("thread pseudo", THREAD_PSEUDO, 72, 16, ST_TYPE_MISMATCH, NULL, 0, 0);
    qh_err("event", ev, 72, 16, ST_TYPE_MISMATCH, NULL, 0, 0);
    if (sync) qh_err("sync", sync, 72, 16, ST_DENIED, NULL, 0, 0);
    if (vmr) qh_err("vmr", vmr, 72, 16, ST_DENIED, NULL, 0, 0);
    qh_err("null len 0", NULL, 72, 0, ST_INV_HANDLE, NULL, 0, 0);
    CloseHandle(ev); if (sync) CloseHandle(sync); if (vmr) CloseHandle(vmr);
}
TEST(c72_same_status_for_real_handles_and_children) {
    struct qh_res a = qh_probe(SELF, 72, 16, NULL, 0, 0);
    HANDLE lim = qh_open(PROCESS_QUERY_LIMITED_INFORMATION, 0);
    if (lim) { struct qh_res b = qh_probe(lim, 72, 16, NULL, 0, 0); CHECKF(b.st == a.st, "real lim 0x%lx vs pseudo 0x%lx", b.st, a.st); CloseHandle(lim); }
    struct qh_child c;
    if (qh_child_start(&c, QC_PLAIN, NULL)) {
        HANDLE h = qh_open(PROCESS_QUERY_LIMITED_INFORMATION, c.pid);
        if (h) { struct qh_res b = qh_probe(h, 72, 16, NULL, 0, 0); CHECKF(b.st == a.st, "child 0x%lx vs self 0x%lx", b.st, a.st); CloseHandle(h); }
        qh_child_stop(&c);
    }
}
TEST(c72_misaligned_buffer) {
    static unsigned char raw[64] __attribute__((aligned(16)));
    for (int off = 1; off < 4; off++) CHECKF(qh(SELF, 72, raw + 16 + off, 16, NULL) == ST_MISALIGN, "off %d", off);
}

/* ------------------------------------------------------------------ 77 */
TEST(c77_length_table) {
    static const ULONG lens[] = {0, 1, 4, 8, 11, 13, 16, 0x20, 0x1b0};
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    for (int i = 0; i < sizeof lens / sizeof *lens; i++) {
        qh_err("pseudo", SELF, 77, lens[i], ST_LEN, NULL, 0, 0);
        qh_err("null handle", NULL, 77, lens[i], ST_LEN, NULL, 0, 0);
        qh_err("event", ev, 77, lens[i], ST_LEN, NULL, 0, 0);
    }
    CloseHandle(ev);
}
TEST(c77_exact_length_layout) {
    struct qh_res r = qh_probe(SELF, 77, 12, NULL, 0, 0);
    if (r.st != ST_OK) { t_skip("activity throttle state not provided here (status 0x%lx)", r.st); return; }
    CHECKF(r.touched_end == 12 && r.touched_cnt == 12, "wrote %lu of 12 bytes", r.touched_cnt);
    CHECKF(r.rl == 12, "ReturnLength 0x%lx", r.rl);
    HANDLE lim = qh_open(PROCESS_QUERY_LIMITED_INFORMATION, 0);
    if (lim) { struct qh_res b = qh_probe(lim, 77, 12, NULL, 0, 0); CHECKF(b.st == ST_OK && !memcmp(b.out, r.out, 12), "real lim handle answer differs"); CloseHandle(lim); }
    CHECKF(qh(SELF, 77, r.out, 12, NULL) == ST_OK, "NULL ReturnLength");
}
TEST(c77_handle_errors_and_rights) {
    struct qh_res r = qh_probe(SELF, 77, 12, NULL, 0, 0);
    HANDLE closed = NULL, ev = CreateEventA(NULL, TRUE, FALSE, NULL), sync = qh_open(SYNCHRONIZE, 0), vmw = qh_open(PROCESS_VM_WRITE, 0);
    closed = qh_closed_handle();   /* last: nothing may be opened after it, or the value would be reused */
    qh_err("null", NULL, 77, 12, ST_INV_HANDLE, NULL, 0, 0);
    if (closed) qh_err("closed", closed, 77, 12, ST_INV_HANDLE, NULL, 0, 0);
    qh_err("thread pseudo", THREAD_PSEUDO, 77, 12, ST_TYPE_MISMATCH, NULL, 0, 0);
    qh_err("event", ev, 77, 12, ST_TYPE_MISMATCH, NULL, 0, 0);
    if (sync) qh_err("sync", sync, 77, 12, ST_DENIED, NULL, 0, 0);
    if (vmw) qh_err("vmw", vmw, 77, 12, ST_DENIED, NULL, 0, 0);
    (void)r;
    CloseHandle(ev); if (sync) CloseHandle(sync); if (vmw) CloseHandle(vmw);
}
TEST(c77_misaligned_buffer) {
    static unsigned char raw[64] __attribute__((aligned(16)));
    for (int off = 1; off < 4; off++) CHECKF(qh(SELF, 77, raw + 16 + off, 12, NULL) == ST_MISALIGN, "off %d", off);
}
