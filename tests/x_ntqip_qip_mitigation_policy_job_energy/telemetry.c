/* NtQueryInformationProcess classes 85 (telemetry coverage), 64 (telemetry id) and 65 (commit release information).
 *
 *  65  exactly 0x20 bytes, 4-byte aligned. The first dword is INPUT and must be 3 (else STATUS_REVISION_MISMATCH);
 *      the second dword is an input flag word below 8 (else STATUS_INVALID_PARAMETER) which is returned with its
 *      lowest bit cleared; the next 8 bytes must be zero on input (else STATUS_INVALID_PARAMETER). ReturnLength
 *      is not written. Version is judged before the flags, the length before everything.
 *  85  a large blob (about 16 KB) of which a caller with a full-power token can read all; a short buffer gets
 *      STATUS_BUFFER_OVERFLOW with the needed size in ReturnLength; a token that is refused gets
 *      STATUS_ACCESS_DENIED. Everything that depends on the token or the machine is conditional.
 *  64  a variable size telemetry record of a few hundred bytes; the size is machine and run dependent, so only
 *      relations are checked (ReturnLength is the number of bytes needed and the number written).
 */
#include "qh.h"

/* ------------------------------------------------------------------ 65 */
static ULONG q65(HANDLE h, ULONG ver, ULONG flags, const unsigned char *tail /*24 bytes or NULL*/, unsigned char *out32, ULONG *rl) {
    static unsigned char b[0x30] __attribute__((aligned(8)));
    memset(b, 0xEE, sizeof b);
    memcpy(b, &ver, 4); memcpy(b + 4, &flags, 4);
    if (tail) memcpy(b + 8, tail, 24); else memset(b + 8, 0, 24);
    ULONG st = qh(h, 65, b, 0x20, rl);
    CHECKF(b[0x20] == 0xEE, "byte past the buffer touched");
    if (out32) memcpy(out32, b, 0x20);
    return st;
}

TEST(c65_length_table) {
    static const ULONG lens[] = {0, 1, 4, 8, 0x10, 0x18, 0x1f, 0x21, 0x28, 0x30, 0x40, 0x100, 0x1000};
    unsigned char in[0x20] = {3};
    for (int i = 0; i < sizeof lens / sizeof *lens; i++) {
        qh_err("pseudo", SELF, 65, lens[i], ST_LEN, in, 0, 0);
        qh_err("null handle", NULL, 65, lens[i], ST_LEN, in, 0, 0);
    }
}
TEST(c65_version_must_be_3) {
    static const ULONG vers[] = {0, 1, 2, 4, 5, 6, 0x100, 0x30000, 0x80000003, 0xFFFFFFFF, 0xCCCCCCCC};
    for (int i = 0; i < sizeof vers / sizeof *vers; i++) {
        unsigned char o[0x20]; ULONG rl;
        ULONG st = q65(SELF, vers[i], 0, NULL, o, &rl);
        CHECKF(st == ST_REV_MISMATCH, "version 0x%lx: status 0x%lx", vers[i], st);
        CHECKF(rl == RL_UNSET, "version 0x%lx: ReturnLength written", vers[i]);
    }
    unsigned char o[0x20]; ULONG rl;
    CHECKF(q65(SELF, 3, 0, NULL, o, &rl) == ST_OK, "version 3");
}
TEST(c65_version_judged_before_flags) {
    unsigned char o[0x20];
    CHECKF(q65(SELF, 2, 0x10, NULL, o, NULL) == ST_REV_MISMATCH, "bad version and bad flags");
    CHECKF(q65(SELF, 0, 0xFFFFFFFF, NULL, o, NULL) == ST_REV_MISMATCH, "version 0, flags all ones");
}
TEST(c65_flags_below_8_accepted_and_bit0_cleared) {
    for (ULONG f = 0; f < 8; f++) {
        unsigned char o[0x20]; ULONG rl;
        ULONG st = q65(SELF, 3, f, NULL, o, &rl);
        CHECKF(st == ST_OK, "flags %lu: status 0x%lx", f, st);
        ULONG d0, d1; memcpy(&d0, o, 4); memcpy(&d1, o + 4, 4);
        CHECKF(d0 == 3, "flags %lu: version dword became 0x%lx", f, d0);
        CHECKF(d1 == (f & ~1UL), "flags %lu: returned 0x%lx want 0x%lx", f, d1, f & ~1UL);
        CHECKF(rl == RL_UNSET, "flags %lu: ReturnLength written", f);
        for (int k = 8; k < 0x20; k++) CHECKF(o[k] == 0, "flags %lu: tail byte %d = 0x%x", f, k, o[k]);
    }
}
TEST(c65_flags_8_and_above_rejected) {
    static const ULONG fl[] = {8, 9, 0xA, 0xF, 0x10, 0x100, 0x80000000, 0xFFFFFFFF};
    for (int i = 0; i < sizeof fl / sizeof *fl; i++) {
        unsigned char o[0x20]; ULONG rl;
        ULONG st = q65(SELF, 3, fl[i], NULL, o, &rl);
        CHECKF(st == ST_INV_PARAM, "flags 0x%lx: status 0x%lx", fl[i], st);
        CHECKF(rl == RL_UNSET, "flags 0x%lx: ReturnLength written", fl[i]);
    }
}
TEST(c65_reserved_input_bytes_must_be_zero) {
    for (int k = 0; k < 8; k++) {
        unsigned char tail[24] = {0}, o[0x20]; tail[k] = 1;
        ULONG st = q65(SELF, 3, 0, tail, o, NULL);
        CHECKF(st == ST_INV_PARAM, "nonzero input byte at offset %d: status 0x%lx", 8 + k, st);
    }
    unsigned char tail[24], o[0x20];
    memset(tail, 0, sizeof tail); tail[8] = 1;   /* offset 16 is output space, ignored on input */
    CHECKF(q65(SELF, 3, 0, tail, o, NULL) == ST_OK, "input byte at offset 16 is output space and ignored");
}
TEST(c65_zero_input_succeeds_with_all_handle_kinds) {
    unsigned char o[0x20];
    HANDLE lim = qh_open(PROCESS_QUERY_LIMITED_INFORMATION, 0), qry = qh_open(PROCESS_QUERY_INFORMATION, 0);
    CHECKF(q65(SELF, 3, 0, NULL, o, NULL) == ST_OK, "pseudo");
    if (lim) { CHECKF(q65(lim, 3, 0, NULL, o, NULL) == ST_OK, "lim"); CloseHandle(lim); }
    if (qry) { CHECKF(q65(qry, 3, 0, NULL, o, NULL) == ST_OK, "qry"); CloseHandle(qry); }
    struct qh_child c;
    if (qh_child_start(&c, QC_PLAIN, NULL)) {
        HANDLE h = qh_open(PROCESS_QUERY_LIMITED_INFORMATION, c.pid);
        if (h) { CHECKF(q65(h, 3, 0, NULL, o, NULL) == ST_OK, "child lim"); CloseHandle(h); }
        qh_child_stop(&c);
    }
}
TEST(c65_handle_errors) {
    HANDLE closed = NULL, ev = CreateEventA(NULL, TRUE, FALSE, NULL), sync = qh_open(SYNCHRONIZE, 0), vmr = qh_open(PROCESS_VM_READ, 0);
    closed = qh_closed_handle();   /* last: nothing may be opened after it, or the value would be reused */
    unsigned char in[0x20] = {3};
    qh_err("null", NULL, 65, 0x20, ST_INV_HANDLE, in, 0x20, 0x20);
    if (closed) qh_err("closed", closed, 65, 0x20, ST_INV_HANDLE, in, 0x20, 0x20);
    qh_err("thread pseudo", THREAD_PSEUDO, 65, 0x20, ST_TYPE_MISMATCH, in, 0x20, 0x20);
    qh_err("event", ev, 65, 0x20, ST_TYPE_MISMATCH, in, 0x20, 0x20);
    if (sync) qh_err("sync", sync, 65, 0x20, ST_DENIED, in, 0x20, 0x20);
    if (vmr) qh_err("vmr", vmr, 65, 0x20, ST_DENIED, in, 0x20, 0x20);
    CloseHandle(ev); if (sync) CloseHandle(sync); if (vmr) CloseHandle(vmr);
}
TEST(c65_misaligned_buffer) {
    static unsigned char raw[0x60] __attribute__((aligned(16)));
    memset(raw, 0, sizeof raw);
    for (int off = 1; off < 4; off++) {
        CHECKF(qh(SELF, 65, raw + 16 + off, 0x20, NULL) == ST_MISALIGN, "off %d", off);
        CHECKF(qh(NULL, 65, raw + 16 + off, 0x20, NULL) == ST_MISALIGN, "off %d null handle", off);
    }
    raw[20] = 3;
    ULONG st = qh(SELF, 65, raw + 20, 0x20, NULL);
    CHECKF(st == ST_MISALIGN, "class 65 wants an 8-byte aligned buffer: a 4-aligned one gave 0x%lx", st);
}

/* ------------------------------------------------------------------ 85 */
static struct qh_res r85(HANDLE h, ULONG len) { return qh_probe(h, 85, len, NULL, 0, 0); }

TEST(c85_blob_or_refusal_by_token) {
    struct qh_res r = r85(SELF, 0x8000);
    if (r.st == ST_DENIED) { t_skip("telemetry coverage refused for this token"); return; }
    CHECKF(r.st == ST_OK, "status 0x%lx", r.st);
    CHECKF(r.rl > 0x1000 && r.rl < 0x8000, "ReturnLength 0x%lx", r.rl);
    CHECKF(r.touched_end == r.rl, "wrote up to %lu, ReturnLength %lu", r.touched_end, r.rl);
    CHECKF(r.touched_cnt >= r.rl / 2, "only %lu of %lu bytes differ from the fill", r.touched_cnt, r.rl);
    struct qh_res s = r85(SELF, 0x8000);
    CHECKF(s.rl == r.rl, "size changed between reads: 0x%lx 0x%lx", s.rl, r.rl);
}
TEST(c85_exact_and_short_buffers) {
    struct qh_res r = r85(SELF, 0x8000);
    if (r.st != ST_OK) { t_skip("telemetry coverage not readable here (0x%lx)", r.st); return; }
    ULONG need = r.rl;
    struct qh_res e = r85(SELF, need);
    CHECKF(e.st == ST_OK && e.rl == need, "exact: 0x%lx rl 0x%lx", e.st, e.rl);
    struct qh_res s = r85(SELF, need - 1);
    CHECKF(s.st == ST_BUF_OVERFLOW, "one short: 0x%lx", s.st);
    CHECKF(s.rl == need, "one short: ReturnLength 0x%lx want 0x%lx", s.rl, need);
    CHECKF(s.touched_end <= need - 1, "one short: wrote up to %lu", s.touched_end);
    static const ULONG small[] = {0, 1, 4, 0x10, 0x100, 0x1000};
    for (int i = 0; i < sizeof small / sizeof *small; i++) {
        struct qh_res t = r85(SELF, small[i]);
        CHECKF(t.st == ST_BUF_OVERFLOW && t.rl == need, "len %lu: 0x%lx rl 0x%lx", small[i], t.st, t.rl);
        CHECKF(t.touched_end <= small[i], "len %lu: wrote up to %lu", small[i], t.touched_end);
    }
}
TEST(c85_foreign_process_not_supported) {
    struct qh_res r = r85(SELF, 0x8000);
    if (r.st != ST_OK) { t_skip("telemetry coverage not readable here (0x%lx)", r.st); return; }
    struct qh_child c;
    if (!qh_child_start(&c, QC_PLAIN, NULL)) { t_skip("cannot start child"); return; }
    HANDLE h = qh_open(PROCESS_QUERY_LIMITED_INFORMATION, c.pid), q = qh_open(PROCESS_QUERY_INFORMATION, c.pid);
    if (h) { qh_err("child lim", h, 85, 0x8000, ST_NOT_SUPPORTED, NULL, 0, 0); CloseHandle(h); }
    if (q) { qh_err("child qry", q, 85, 0x8000, ST_NOT_SUPPORTED, NULL, 0, 0); CloseHandle(q); }
    HANDLE self = qh_open(PROCESS_QUERY_LIMITED_INFORMATION, 0);
    if (self) { struct qh_res s = r85(self, 0x8000); CHECKF(s.st == ST_OK, "own real handle: 0x%lx", s.st); CloseHandle(self); }
    qh_child_stop(&c);
}
TEST(c85_handle_errors) {
    HANDLE closed = NULL, ev = CreateEventA(NULL, TRUE, FALSE, NULL), sync = qh_open(SYNCHRONIZE, 0);
    closed = qh_closed_handle();   /* last: nothing may be opened after it, or the value would be reused */
    qh_err("null", NULL, 85, 0x8000, ST_INV_HANDLE, NULL, 0, 0);
    if (closed) qh_err("closed", closed, 85, 0x8000, ST_INV_HANDLE, NULL, 0, 0);
    qh_err("thread pseudo", THREAD_PSEUDO, 85, 0x8000, ST_TYPE_MISMATCH, NULL, 0, 0);
    qh_err("event", ev, 85, 0x8000, ST_TYPE_MISMATCH, NULL, 0, 0);
    if (sync) qh_err("sync", sync, 85, 0x8000, ST_DENIED, NULL, 0, 0);
    CloseHandle(ev); if (sync) CloseHandle(sync);
}
TEST(c85_misaligned_buffer) {
    static unsigned char raw[64] __attribute__((aligned(16)));
    for (int off = 1; off < 4; off++) CHECKF(qh(SELF, 85, raw + 16 + off, 16, NULL) == ST_MISALIGN, "off %d", off);
}

/* ------------------------------------------------------------------ 64 */
TEST(c64_record_size_relations) {
    struct qh_res big = qh_probe(SELF, 64, 0x1000, NULL, 0, 0);
    CHECKF(big.st == ST_OK, "status 0x%lx", big.st);
    if (big.st != ST_OK) return;
    CHECKF(big.rl >= 0x40 && big.rl <= 0x800, "ReturnLength 0x%lx", big.rl);
    CHECKF(big.touched_end == big.rl, "wrote up to %lu, ReturnLength %lu", big.touched_end, big.rl);
    ULONG first; memcpy(&first, big.out, 4);
    t_info("64: size %lu first dword 0x%lx", big.rl, first);
    HANDLE lim = qh_open(PROCESS_QUERY_LIMITED_INFORMATION, 0);
    if (lim) { struct qh_res b = qh_probe(lim, 64, 0x1000, NULL, 0, 0); CHECKF(b.st == ST_OK, "real lim 0x%lx", b.st); CloseHandle(lim); }
}
TEST(c64_short_buffer_reports_needed_size) {
    struct qh_res big = qh_probe(SELF, 64, 0x1000, NULL, 0, 0);
    if (big.st != ST_OK) { t_skip("no telemetry record (0x%lx)", big.st); return; }
    static const ULONG small[] = {0, 1, 4, 0x10, 0x20, 0x40};
    for (int i = 0; i < sizeof small / sizeof *small; i++) {
        struct qh_res r = qh_probe(SELF, 64, small[i], NULL, 0, 0);
        CHECKF(r.st == ST_LEN, "len %lu: status 0x%lx", small[i], r.st);
        CHECKF(r.rl != RL_UNSET && r.rl >= 0x40 && r.rl <= 0x800, "len %lu: ReturnLength 0x%lx", small[i], r.rl);
        CHECKF(r.touched_end <= small[i], "len %lu: wrote up to %lu", small[i], r.touched_end);
    }
    struct qh_res need = qh_probe(SELF, 64, big.rl, NULL, 0, 0);
    CHECKF(need.st == ST_OK || need.st == ST_LEN, "exact-size buffer: 0x%lx", need.st);   /* size may move between calls */
}
TEST(c64_handle_errors) {
    HANDLE closed = NULL, ev = CreateEventA(NULL, TRUE, FALSE, NULL), sync = qh_open(SYNCHRONIZE, 0), vmr = qh_open(PROCESS_VM_READ, 0);
    closed = qh_closed_handle();   /* last: nothing may be opened after it, or the value would be reused */
    qh_err("null", NULL, 64, 0x1000, ST_INV_HANDLE, NULL, 0, 0);
    if (closed) qh_err("closed", closed, 64, 0x1000, ST_INV_HANDLE, NULL, 0, 0);
    qh_err("thread pseudo", THREAD_PSEUDO, 64, 0x1000, ST_TYPE_MISMATCH, NULL, 0, 0);
    qh_err("event", ev, 64, 0x1000, ST_TYPE_MISMATCH, NULL, 0, 0);
    if (sync) qh_err("sync", sync, 64, 0x1000, ST_DENIED, NULL, 0, 0);
    if (vmr) qh_err("vmr", vmr, 64, 0x1000, ST_DENIED, NULL, 0, 0);
    CloseHandle(ev); if (sync) CloseHandle(sync); if (vmr) CloseHandle(vmr);
}
TEST(c64_foreign_child_answers) {
    struct qh_child c;
    if (!qh_child_start(&c, QC_PLAIN, NULL)) { t_skip("cannot start child"); return; }
    HANDLE h = qh_open(PROCESS_QUERY_LIMITED_INFORMATION, c.pid);
    if (h) { struct qh_res r = qh_probe(h, 64, 0x1000, NULL, 0, 0); CHECKF(r.st == ST_OK, "child lim: 0x%lx", r.st); if (r.st == ST_OK) CHECKF(r.rl >= 0x40 && r.rl <= 0x800 && r.touched_end == r.rl, "rl 0x%lx end %lu", r.rl, r.touched_end); CloseHandle(h); }
    qh_child_stop(&c);
}
TEST(c64_misaligned_buffer) {
    static unsigned char raw[64] __attribute__((aligned(16)));
    for (int off = 1; off < 4; off++) CHECKF(qh(SELF, 64, raw + 16 + off, 16, NULL) == ST_MISALIGN, "off %d", off);
}
