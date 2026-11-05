/* NtQueryInformationProcess classes 70 (in-private flag, 1 byte), 73 (child process policy, 3 bytes),
 * 75 (subsystem information, dword) and 79 (win32k syscall filter, 8 bytes).
 *
 * All four answer only to their exact length (checked before the handle). Class 70 is the only class of this set
 * that accepts a byte-aligned buffer; the others require 4-byte alignment and report a misaligned buffer as
 * STATUS_DATATYPE_MISALIGNMENT before looking at the class or the handle. 70 and 75 need
 * PROCESS_QUERY_LIMITED_INFORMATION, 73 and 79 need PROCESS_QUERY_INFORMATION for a real handle (the pseudo
 * handle always passes). An ordinary process reports all zeros; a child created with the child-process
 * restriction attribute reports it in the first byte of class 73.
 */
#include "qh.h"

static const struct fl { ULONG cls, len; DWORD need; } F[] = {
    {70, 1, PROCESS_QUERY_LIMITED_INFORMATION}, {73, 3, PROCESS_QUERY_INFORMATION},
    {75, 4, PROCESS_QUERY_LIMITED_INFORMATION}, {79, 8, PROCESS_QUERY_INFORMATION},
};
#define NF (int)(sizeof F / sizeof *F)

static void len_table(int i) {
    static const ULONG lens[] = {0, 1, 2, 3, 4, 5, 7, 8, 9, 12, 16, 0x20, 0x28, 0x38, 0x1b0, 0x1000};
    HANDLE lim = qh_open(PROCESS_QUERY_INFORMATION, 0);
    for (int k = 0; k < sizeof lens / sizeof *lens; k++) {
        if (lens[k] == F[i].len) continue;
        qh_err("pseudo", SELF, F[i].cls, lens[k], ST_LEN, NULL, 0, 0);
        if (lim) qh_err("real", lim, F[i].cls, lens[k], ST_LEN, NULL, 0, 0);
        qh_err("null handle", NULL, F[i].cls, lens[k], ST_LEN, NULL, 0, 0);
        qh_err("thread pseudo", THREAD_PSEUDO, F[i].cls, lens[k], ST_LEN, NULL, 0, 0);
    }
    if (lim) CloseHandle(lim);
}
TEST(c70_length_table) { len_table(0); }
TEST(c73_length_table) { len_table(1); }
TEST(c75_length_table) { len_table(2); }
TEST(c79_length_table) { len_table(3); }

static void normal_value(int i) {
    struct qh_res r = qh_probe(SELF, F[i].cls, F[i].len, NULL, 0, 0);
    CHECKF(r.st == ST_OK, "cls %lu status 0x%lx", F[i].cls, r.st);
    CHECKF(r.touched_end == F[i].len && r.touched_cnt == F[i].len, "cls %lu wrote %lu of %lu bytes", F[i].cls, r.touched_cnt, F[i].len);
    for (ULONG k = 0; k < F[i].len; k++) CHECKF(r.out[k] == 0, "cls %lu byte %lu = 0x%x in an ordinary process", F[i].cls, k, r.out[k]);
    /* ReturnLength: not written by 70, equal to the length for the others */
    CHECKF(r.rl == (F[i].cls == 70 ? RL_UNSET : F[i].len), "cls %lu ReturnLength 0x%lx", F[i].cls, r.rl);
    HANDLE h = qh_open(F[i].need, 0);
    if (h) {
        struct qh_res r2 = qh_probe(h, F[i].cls, F[i].len, NULL, 0, 0);
        CHECKF(r2.st == ST_OK && !memcmp(r.out, r2.out, F[i].len), "real handle (rights 0x%lx): status 0x%lx", (ULONG)F[i].need, r2.st);
        CHECKF(r2.rl == r.rl, "real handle ReturnLength 0x%lx vs 0x%lx", r2.rl, r.rl);
        CloseHandle(h);
    }
}
TEST(c70_ordinary_process_value) { normal_value(0); }
TEST(c73_ordinary_process_value) { normal_value(1); }
TEST(c75_ordinary_process_value) { normal_value(2); }
TEST(c79_ordinary_process_value) { normal_value(3); }

TEST(flags_null_return_length_and_null_buffer) {
    unsigned char b[16] __attribute__((aligned(8)));
    for (int i = 0; i < NF; i++) {
        CHECKF(qh(SELF, F[i].cls, b, F[i].len, NULL) == ST_OK, "cls %lu NULL ReturnLength", F[i].cls);
        CHECKF(qh(SELF, F[i].cls, NULL, 0, NULL) == ST_LEN, "cls %lu NULL buffer len 0", F[i].cls);
    }
}

TEST(c70_byte_aligned_buffer_accepted) {
    static unsigned char raw[32] __attribute__((aligned(16)));
    for (int off = 0; off < 4; off++) {
        memset(raw, 0xEE, sizeof raw);
        ULONG st = qh(SELF, 70, raw + 8 + off, 1, NULL);
        CHECKF(st == ST_OK, "off %d status 0x%lx", off, st);
        CHECKF(raw[8 + off] == 0, "off %d value 0x%x", off, raw[8 + off]);
        CHECKF(raw[7 + off] == 0xEE && raw[9 + off] == 0xEE, "off %d neighbours touched", off);
    }
}
TEST(flags_misaligned_buffer) {
    static unsigned char raw[64] __attribute__((aligned(16)));
    for (int i = 1; i < NF; i++) for (int off = 1; off < 4; off++) {
        ULONG rl;
        CHECKF(qh(SELF, F[i].cls, raw + 16 + off, F[i].len, &rl) == ST_MISALIGN, "cls %lu off %d", F[i].cls, off);
        CHECKF(rl == RL_UNSET, "cls %lu off %d ReturnLength", F[i].cls, off);
        CHECKF(qh(NULL, F[i].cls, raw + 16 + off, F[i].len, NULL) == ST_MISALIGN, "cls %lu off %d null handle", F[i].cls, off);
        CHECKF(qh(SELF, F[i].cls, raw + 16 + off, F[i].len + 1, NULL) == ST_MISALIGN, "cls %lu off %d wrong len", F[i].cls, off);
    }
}

static void handle_errors(int i) {
    HANDLE closed = NULL, ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    HANDLE sync = qh_open(SYNCHRONIZE, 0), vmr = qh_open(PROCESS_VM_READ, 0), vmw = qh_open(PROCESS_VM_WRITE, 0);
    closed = qh_closed_handle();   /* last: nothing may be opened after it, or the value would be reused */
    ULONG c = F[i].cls, n = F[i].len;
    qh_err("null", NULL, c, n, ST_INV_HANDLE, NULL, 0, 0);
    if (closed) qh_err("closed", closed, c, n, ST_INV_HANDLE, NULL, 0, 0);
    qh_err("thread pseudo", THREAD_PSEUDO, c, n, ST_TYPE_MISMATCH, NULL, 0, 0);
    qh_err("event", ev, c, n, ST_TYPE_MISMATCH, NULL, 0, 0);
    if (sync) qh_err("sync only", sync, c, n, ST_DENIED, NULL, 0, 0);
    if (vmr) qh_err("vm read only", vmr, c, n, ST_DENIED, NULL, 0, 0);
    if (vmw) qh_err("vm write only", vmw, c, n, ST_DENIED, NULL, 0, 0);
    CloseHandle(ev); if (sync) CloseHandle(sync); if (vmr) CloseHandle(vmr); if (vmw) CloseHandle(vmw);
}
TEST(c70_handle_errors) { handle_errors(0); }
TEST(c73_handle_errors) { handle_errors(1); }
TEST(c75_handle_errors) { handle_errors(2); }
TEST(c79_handle_errors) { handle_errors(3); }

/* the right a real handle needs differs: LIM is enough for 70/75, 73/79 need QUERY_INFORMATION (also for own process) */
static void rights_split(int i) {
    struct qh_child c;
    if (!qh_child_start(&c, QC_PLAIN, NULL)) { t_skip("cannot start child"); return; }
    HANDLE lim = qh_open(PROCESS_QUERY_LIMITED_INFORMATION, c.pid), qry = qh_open(PROCESS_QUERY_INFORMATION, c.pid);
    HANDLE slim = qh_open(PROCESS_QUERY_LIMITED_INFORMATION, 0), sqry = qh_open(PROCESS_QUERY_INFORMATION, 0);
    int needs_qry = F[i].need == PROCESS_QUERY_INFORMATION;
    struct { HANDLE h; const char *n; } H[] = {{lim, "child lim"}, {slim, "self lim"}};
    for (int k = 0; k < 2; k++) {
        if (!H[k].h) continue;
        if (needs_qry) qh_err(H[k].n, H[k].h, F[i].cls, F[i].len, ST_DENIED, NULL, 0, 0);
        else { struct qh_res r = qh_probe(H[k].h, F[i].cls, F[i].len, NULL, 0, 0); CHECKF(r.st == ST_OK, "%s: 0x%lx", H[k].n, r.st); }
    }
    struct { HANDLE h; const char *n; } Q[] = {{qry, "child qry"}, {sqry, "self qry"}};
    for (int k = 0; k < 2; k++) { if (!Q[k].h) continue; struct qh_res r = qh_probe(Q[k].h, F[i].cls, F[i].len, NULL, 0, 0); CHECKF(r.st == ST_OK, "%s: 0x%lx", Q[k].n, r.st); }
    struct qh_res rp = qh_probe(SELF, F[i].cls, F[i].len, NULL, 0, 0);
    CHECKF(rp.st == ST_OK, "pseudo handle 0x%lx", rp.st);
    if (lim) CloseHandle(lim);
    if (qry) CloseHandle(qry);
    if (slim) CloseHandle(slim);
    if (sqry) CloseHandle(sqry);
    qh_child_stop(&c);
}
TEST(c70_rights_foreign_and_self) { rights_split(0); }
TEST(c73_rights_foreign_and_self) { rights_split(1); }
TEST(c75_rights_foreign_and_self) { rights_split(2); }
TEST(c79_rights_foreign_and_self) { rights_split(3); }

TEST(c73_child_process_restriction_attribute) {
    struct qh_child c;
    if (!qh_child_start(&c, QC_CHILD_RESTRICTED, NULL)) { t_skip("cannot start restricted child"); return; }
    HANDLE h = qh_open(PROCESS_QUERY_INFORMATION, c.pid);
    REQUIRE(h);
    struct qh_res r = qh_probe(h, 73, 3, NULL, 0, 0);
    CHECKF(r.st == ST_OK, "status 0x%lx", r.st);
    CHECKF(r.out[0] == 1 && r.out[1] == 0 && r.out[2] == 0, "bytes %02x %02x %02x want 01 00 00", r.out[0], r.out[1], r.out[2]);
    CHECKF(r.rl == 3, "ReturnLength 0x%lx", r.rl);
    CloseHandle(h); qh_child_stop(&c);
}

TEST(c79_ordinary_child_value_and_win32k_disable_child) {
    struct qh_child c;
    if (qh_child_start(&c, QC_PLAIN, NULL)) {
        HANDLE h = qh_open(PROCESS_QUERY_INFORMATION, c.pid);
        if (h) { struct qh_res r = qh_probe(h, 79, 8, NULL, 0, 0); CHECKF(r.st == ST_OK && !r.out[0] && !r.out[4] && !r.out[1], "plain child: 0x%lx", r.st); CloseHandle(h); }
        qh_child_stop(&c);
    }
    if (qh_child_start(&c, QC_WIN32K_DISABLE, NULL)) {
        HANDLE h = qh_open(PROCESS_QUERY_INFORMATION, c.pid);
        if (h) {
            struct qh_res r = qh_probe(h, 79, 8, NULL, 0, 0);
            CHECKF(r.st == ST_OK, "status 0x%lx", r.st);
            /* the system-call-disable mitigation is a different mechanism from the filter reported by this class */
            t_note("win32k-disable child: filter state %u set %u", r.out[0], r.out[4]);
            CHECKF(r.out[0] == 0 && r.out[4] == 0, "filter dwords 0x%x 0x%x", r.out[0], r.out[4]);
            unsigned char in[8] = {4, 0, 0, 0, 0x55, 0x55, 0x55, 0x55};
            struct qh_res m = qh_probe(h, 52, 8, in, 8, 8);
            CHECKF(m.st == ST_OK && m.out[4] == 1, "system call disable policy (id 4) flags 0x%x status 0x%lx", m.out[4], m.st);
            CloseHandle(h);
        }
        qh_child_stop(&c);
    } else t_note("win32k-disable attribute child could not be started");
}

TEST(c70_terminated_child_still_answers) {
    struct qh_child c;
    if (!qh_child_start(&c, QC_PLAIN, NULL)) { t_skip("cannot start child"); return; }
    HANDLE h = qh_open(PROCESS_QUERY_LIMITED_INFORMATION, c.pid);
    REQUIRE(h);
    TerminateProcess(c.h, 0); WaitForSingleObject(c.h, 5000);
    struct qh_res r = qh_probe(h, 70, 1, NULL, 0, 0);
    CHECKF(r.st == ST_OK && r.out[0] == 0, "status 0x%lx value %u", r.st, r.out[0]);
    r = qh_probe(h, 75, 4, NULL, 0, 0);
    CHECKF(r.st == ST_OK && r.out[0] == 0, "75: status 0x%lx value %u", r.st, r.out[0]);
    CloseHandle(h); qh_child_stop(&c);
}
