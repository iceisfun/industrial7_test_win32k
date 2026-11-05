/* NtQueryInformationProcess classes 76 (energy values), 82 (energy tracking state) and 88 (uptime statistics).
 *
 * These are the "any length" classes: the kernel copies min(length, size) bytes of a fixed structure and reports
 * the copied count as ReturnLength, with no length mismatch error above a small floor.
 *   76  size 0x1b0, any length including 0; ReturnLength is always 0x1b0 (the full size, not the copied count).
 *   82  size 0x90, length must be at least 8 (shorter is STATUS_INFO_LENGTH_MISMATCH); ReturnLength = bytes copied.
 *   88  size 0x38, any length including 0; ReturnLength = bytes copied.
 * The copied extent is measured with two different fill patterns so zero-valued data cannot hide a write. All three
 * need PROCESS_QUERY_LIMITED_INFORMATION and judge alignment (4 bytes) before everything else.
 */
#include "qh.h"

struct en { ULONG cls, size, floor; int rl_is_full; DWORD need; } E[] = {
    {76, 0x1b0, 0, 1, PROCESS_QUERY_LIMITED_INFORMATION},
    {82, 0x90, 8, 0, PROCESS_QUERY_LIMITED_INFORMATION},
    {88, 0x38, 0, 0, PROCESS_QUERY_LIMITED_INFORMATION},
};

static void copy_min(int i, HANDLE h, const char *tag) {
    static const ULONG lens[] = {0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 0x1f, 0x20, 0x37, 0x38, 0x39, 0x40, 0x8f, 0x90, 0x91, 0x100, 0x1af, 0x1b0, 0x1b1, 0x400, 0x1000, 0x8000};
    for (int k = 0; k < sizeof lens / sizeof *lens; k++) {
        ULONG len = lens[k];
        if (len < E[i].floor) continue;
        ULONG want = len < E[i].size ? len : E[i].size;
        struct qh_res r = qh_probe(h, E[i].cls, len, NULL, 0, 0);
        CHECKF(r.st == ST_OK, "%s cls %lu len 0x%lx: status 0x%lx", tag, E[i].cls, len, r.st);
        CHECKF(r.touched_end <= want, "%s cls %lu len 0x%lx: wrote up to byte %lu, more than min(len,size)=%lu", tag, E[i].cls, len, r.touched_end, want);
        CHECKF(r.rl == (E[i].rl_is_full ? E[i].size : want), "%s cls %lu len 0x%lx: ReturnLength 0x%lx", tag, E[i].cls, len, r.rl);
    }
}
TEST(c76_copy_min_with_pseudo_handle) { copy_min(0, SELF, "pseudo"); }
TEST(c82_copy_min_with_pseudo_handle) { copy_min(1, SELF, "pseudo"); }
TEST(c88_copy_min_with_pseudo_handle) { copy_min(2, SELF, "pseudo"); }

static void copy_min_real(int i) {
    HANDLE lim = qh_open(PROCESS_QUERY_LIMITED_INFORMATION, 0), qry = qh_open(PROCESS_QUERY_INFORMATION, 0);
    if (lim) { copy_min(i, lim, "self lim"); CloseHandle(lim); }
    if (qry) { copy_min(i, qry, "self qry"); CloseHandle(qry); }
    struct qh_child c;
    if (qh_child_start(&c, QC_PLAIN, NULL)) {
        HANDLE h = qh_open(PROCESS_QUERY_LIMITED_INFORMATION, c.pid);
        if (h) { copy_min(i, h, "child lim"); CloseHandle(h); }
        qh_child_stop(&c);
    }
}
TEST(c76_copy_min_real_handles) { copy_min_real(0); }
TEST(c82_copy_min_real_handles) { copy_min_real(1); }
TEST(c88_copy_min_real_handles) { copy_min_real(2); }

static void full_write(int i) {
    struct qh_res r = qh_probe(SELF, E[i].cls, E[i].size, NULL, 0, 0);
    CHECKF(r.st == ST_OK, "status 0x%lx", r.st);
    if (r.touched_cnt != E[i].size) t_note("cls %lu: structure has %lu unwritten or fill-equal bytes", E[i].cls, E[i].size - r.touched_cnt);
    CHECKF(r.touched_end == E[i].size, "cls %lu: last byte written is %lu, want %lu", E[i].cls, r.touched_end, E[i].size);
}
TEST(c76_full_structure_written) { full_write(0); }
TEST(c82_full_structure_written) { full_write(1); }
TEST(c88_full_structure_written) { full_write(2); }

static void null_args(int i) {
    unsigned char b[0x200] __attribute__((aligned(8)));
    ULONG st = qh(SELF, E[i].cls, b, E[i].size, NULL);
    CHECKF(st == ST_OK, "cls %lu NULL ReturnLength: 0x%lx", E[i].cls, st);
    ULONG rl;
    st = qh(SELF, E[i].cls, NULL, 0, &rl);
    if (E[i].floor == 0) { CHECKF(st == ST_OK, "cls %lu NULL buffer len 0: 0x%lx", E[i].cls, st); CHECKF(rl == (E[i].rl_is_full ? E[i].size : 0), "cls %lu NULL buffer ReturnLength 0x%lx", E[i].cls, rl); }
    else { CHECKF(st == ST_LEN, "cls %lu NULL buffer len 0: 0x%lx", E[i].cls, st); CHECKF(rl == RL_UNSET, "cls %lu ReturnLength written", E[i].cls); }
    st = qh(SELF, E[i].cls, NULL, 0, NULL);
    CHECKF(st == (E[i].floor == 0 ? ST_OK : ST_LEN), "cls %lu NULL buffer and NULL ReturnLength: 0x%lx", E[i].cls, st);
}
TEST(c76_null_pointers) { null_args(0); }
TEST(c82_null_pointers) { null_args(1); }
TEST(c88_null_pointers) { null_args(2); }

TEST(c82_below_floor_is_length_mismatch) {
    HANDLE lim = qh_open(PROCESS_QUERY_LIMITED_INFORMATION, 0);
    for (ULONG len = 0; len < 8; len++) {
        qh_err("pseudo", SELF, 82, len, ST_LEN, NULL, 0, 0);
        if (lim) qh_err("real", lim, 82, len, ST_LEN, NULL, 0, 0);
    }
    if (lim) CloseHandle(lim);
}

static void handles(int i) {
    HANDLE closed = NULL, ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    HANDLE sync = qh_open(SYNCHRONIZE, 0), vmr = qh_open(PROCESS_VM_READ, 0), vmw = qh_open(PROCESS_VM_WRITE, 0);
    closed = qh_closed_handle();   /* last: nothing may be opened after it, or the value would be reused */
    ULONG c = E[i].cls, n = E[i].size;
    qh_err("null", NULL, c, n, ST_INV_HANDLE, NULL, 0, 0);
    if (closed) qh_err("closed", closed, c, n, ST_INV_HANDLE, NULL, 0, 0);
    qh_err("thread pseudo", THREAD_PSEUDO, c, n, ST_TYPE_MISMATCH, NULL, 0, 0);
    qh_err("event", ev, c, n, ST_TYPE_MISMATCH, NULL, 0, 0);
    if (sync) qh_err("sync only", sync, c, n, ST_DENIED, NULL, 0, 0);
    if (vmr) qh_err("vm read only", vmr, c, n, ST_DENIED, NULL, 0, 0);
    if (vmw) qh_err("vm write only", vmw, c, n, ST_DENIED, NULL, 0, 0);
    /* with a small length the handle is still judged (no length error can mask it) */
    if (E[i].floor == 0) { qh_err("null, len 0", NULL, c, 0, ST_INV_HANDLE, NULL, 0, 0); qh_err("thread, len 1", THREAD_PSEUDO, c, 1, ST_TYPE_MISMATCH, NULL, 0, 0); }
    CloseHandle(ev); if (sync) CloseHandle(sync); if (vmr) CloseHandle(vmr); if (vmw) CloseHandle(vmw);
}
TEST(c76_handle_errors) { handles(0); }
TEST(c82_handle_errors) { handles(1); }
TEST(c88_handle_errors) { handles(2); }

static void misaligned(int i) {
    static unsigned char raw[0x200] __attribute__((aligned(16)));
    for (int off = 1; off < 4; off++) {
        ULONG rl;
        CHECKF(qh(SELF, E[i].cls, raw + 16 + off, E[i].size, &rl) == ST_MISALIGN, "cls %lu off %d", E[i].cls, off);
        CHECKF(rl == RL_UNSET, "cls %lu off %d ReturnLength written", E[i].cls, off);
        CHECKF(qh(SELF, E[i].cls, raw + 16 + off, 1, NULL) == ST_MISALIGN, "cls %lu off %d short len", E[i].cls, off);
        CHECKF(qh(NULL, E[i].cls, raw + 16 + off, E[i].size, NULL) == ST_MISALIGN, "cls %lu off %d null handle", E[i].cls, off);
    }
    CHECKF(qh(SELF, E[i].cls, raw + 20, E[i].size, NULL) == ST_OK, "cls %lu 4-aligned buffer", E[i].cls);
}
TEST(c76_misaligned_buffer) { misaligned(0); }
TEST(c82_misaligned_buffer) { misaligned(1); }
TEST(c88_misaligned_buffer) { misaligned(2); }

TEST(c76_energy_values_are_zero_or_monotone_across_two_reads) {
    /* the structure carries counters: two consecutive reads must not go backwards in any 64-bit field */
    static ULONGLONG a[0x1b0 / 8], b[0x1b0 / 8];
    CHECKF(qh(SELF, 76, a, 0x1b0, NULL) == ST_OK, "first");
    CHECKF(qh(SELF, 76, b, 0x1b0, NULL) == ST_OK, "second");
    int back = 0;
    for (int k = 0; k < 0x1b0 / 8; k++) if (b[k] < a[k]) back++;
    CHECKF(back == 0, "%d quadwords went backwards between two reads", back);
}

TEST(c88_leading_quadwords_agree) {
    /* the first two quadwords of the uptime structure are equal in every sample taken on Windows 10 */
    static ULONGLONG q[0x38 / 8];
    CHECKF(qh(SELF, 88, q, 0x38, NULL) == ST_OK, "read");
    CHECKF(q[0] == q[1], "q0 0x%llx q1 0x%llx", q[0], q[1]);
}

TEST(c88_partial_read_is_prefix_of_full_read) {
    static unsigned char full1[0x38] __attribute__((aligned(8))), full2[0x38] __attribute__((aligned(8))), part[0x38] __attribute__((aligned(8)));
    CHECKF(qh(SELF, 88, full1, 0x38, NULL) == ST_OK, "full 1");
    CHECKF(qh(SELF, 88, full2, 0x38, NULL) == ST_OK, "full 2");
    if (memcmp(full1, full2, 0x38)) { t_note("uptime structure moved between two reads; prefix comparison skipped"); return; }
    for (ULONG len = 1; len < 0x38; len += 7) {
        memset(part, 0xEE, sizeof part);
        CHECKF(qh(SELF, 88, part, len, NULL) == ST_OK, "len %lu", len);
        CHECKF(!memcmp(part, full1, len), "len %lu prefix differs", len);
        CHECKF(part[len] == 0xEE, "len %lu wrote past the length", len);
    }
}

TEST(c88_terminated_child_still_answers) {
    struct qh_child c;
    if (!qh_child_start(&c, QC_PLAIN, NULL)) { t_skip("cannot start child"); return; }
    HANDLE h = qh_open(PROCESS_QUERY_LIMITED_INFORMATION, c.pid);
    REQUIRE(h);
    TerminateProcess(c.h, 0); WaitForSingleObject(c.h, 5000);
    struct qh_res r = qh_probe(h, 88, 0x38, NULL, 0, 0);
    CHECKF(r.st == ST_OK && r.rl == 0x38, "88: status 0x%lx rl 0x%lx", r.st, r.rl);
    r = qh_probe(h, 76, 0x1b0, NULL, 0, 0);
    CHECKF(r.st == ST_OK && r.rl == 0x1b0, "76: status 0x%lx rl 0x%lx", r.st, r.rl);
    CloseHandle(h); qh_child_stop(&c);
}

TEST(c88_first_quadword_tracks_system_uptime) {
    /* observed relation: the leading quadword counts 100 ns units and follows the system tick count; it never goes
     * backwards and advances by at least the slept time (lower bounds only, no upper timing assumption) */
    static ULONGLONG a[0x38 / 8], b[0x38 / 8];
    CHECKF(qh(SELF, 88, a, 0x38, NULL) == ST_OK, "first read");
    ULONGLONG tick = GetTickCount64() * 10000ULL;
    CHECKF(a[0] + 5000000ULL >= tick, "uptime 0x%llx is more than half a second behind the tick count 0x%llx", a[0], tick);
    CHECKF(a[0] <= tick + 5000000ULL, "uptime 0x%llx is more than half a second ahead of the tick count 0x%llx", a[0], tick);
    Sleep(300);
    CHECKF(qh(SELF, 88, b, 0x38, NULL) == ST_OK, "second read");
    CHECKF(b[0] >= a[0] + 2000000ULL, "advanced by only 0x%llx after a 300 ms sleep", b[0] - a[0]);
    CHECKF(b[3] >= a[3], "fourth quadword went backwards");
}
