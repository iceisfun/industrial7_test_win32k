/* Information class ids that do not exist for a query on Windows 10 22H2 x64: the set-only ids, the removed ones,
 * the future range 100..127 and out-of-range values. They all answer STATUS_INVALID_INFO_CLASS, and they do so
 * before the handle or the buffer length is looked at, so no ReturnLength is written either. The only checks that
 * run before the class is examined are the buffer alignment one (STATUS_DATATYPE_MISALIGNMENT) and probing of
 * the ReturnLength pointer. Only valid pointers are used. */
#include "qp.h"

static void gap_one(ULONG id) {
    static const ULONG lens[] = { 0, 1, 2, 4, 8, 0x30, 0x1000 };
    if (!QIP) { t_skip("missing"); return; }
    for (int i = 0; i < (int)(sizeof lens / sizeof lens[0]); i++) {
        ULONG rl; int w;
        NTSTATUS st = qp_q(QP_PSEUDO_SELF, id, qp_buf(), lens[i], &rl, &w);
        t_check(st == QS_BAD_CLASS && !w, __FILE__, __LINE__, "invalid id", "id 0x%lx len %lu st 0x%lx written=%d", id, lens[i], (unsigned long)st, w);
    }
    for (int k = H_NULL; k <= H_CUR; k++) {
        HANDLE h = qp_hmake(k, NULL);
        ULONG rl; int w;
        NTSTATUS st = qp_q(h, id, qp_buf(), 4, &rl, &w);
        t_check(st == QS_BAD_CLASS && !w, __FILE__, __LINE__, "invalid id, any handle", "id 0x%lx handle %s st 0x%lx", id, qp_hname[k], (unsigned long)st);
        qp_hrelease(k, h);
    }
    NTSTATUS st = QIP(QP_PSEUDO_SELF, id, qp_buf(), 4, NULL);
    CHECKF(st == QS_BAD_CLASS, "id 0x%lx null rl st 0x%lx", id, (unsigned long)st);
}
#define GAP(id) T5(gap_##id) { gap_one(id); }
GAP(5) GAP(6) GAP(8) GAP(9) GAP(11) GAP(13) GAP(16) GAP(17) GAP(25) GAP(35) GAP(40) GAP(41) GAP(48) GAP(53) GAP(56)
GAP(57) GAP(62) GAP(63) GAP(68) GAP(78) GAP(80) GAP(83) GAP(86) GAP(90) GAP(91) GAP(93) GAP(95) GAP(98) GAP(99)

T5(gap_future_range_100_127) { for (ULONG id = 100; id <= 127; id++) gap_one(id); }
T5(gap_large_values) {
    static const ULONG big[] = { 0x100, 0x101, 0x1000, 0xFFFF, 0x10000, 0x7FFFFFFF, 0x80000000, 0x80000001, 0xFFFFFFFE, 0xFFFFFFFF };
    for (int i = 0; i < 10; i++) gap_one(big[i]);
}
T5(gap_table_driven_all_ids) { for (int i = 0; i < qp_ngap; i++) gap_one(qp_gap_ids[i]); }
T5(gap_misaligned_buffer_reports_misalignment_first) {
    if (!QIP) { t_skip("missing"); return; }
    for (int i = 0; i < qp_ngap; i++) for (int off = 1; off <= 3; off++) {
        ULONG rl; int w;
        NTSTATUS st = qp_q(QP_PSEUDO_SELF, qp_gap_ids[i], qp_buf() + off, 4, &rl, &w);
        t_check(st == QS_MISALIGNED, __FILE__, __LINE__, "misaligned, invalid id", "id 0x%lx off %d st 0x%lx", qp_gap_ids[i], off, (unsigned long)st);
    }
}
T5(gap_misaligned_buffer_zero_length_is_not_checked) {
    if (!QIP) { t_skip("missing"); return; }
    for (int i = 0; i < qp_ngap; i++) {
        NTSTATUS st = QIP(QP_PSEUDO_SELF, qp_gap_ids[i], qp_buf() + 1, 0, NULL);
        t_check(st == QS_BAD_CLASS, __FILE__, __LINE__, "len 0 misaligned", "id 0x%lx st 0x%lx", qp_gap_ids[i], (unsigned long)st);
    }
}
T5(gap_null_buffer_zero_length) {
    if (!QIP) { t_skip("missing"); return; }
    for (int i = 0; i < qp_ngap; i++) {
        ULONG rl; int w;
        NTSTATUS st = qp_q(QP_PSEUDO_SELF, qp_gap_ids[i], NULL, 0, &rl, &w);
        t_check(st == QS_BAD_CLASS && !w, __FILE__, __LINE__, "NULL buffer", "id 0x%lx st 0x%lx", qp_gap_ids[i], (unsigned long)st);
    }
}
T5(gap_misaligned_return_length_pointer_is_accepted) {
    if (!QIP) { t_skip("missing"); return; }
    ULONG *prl = (ULONG *)(qp_buf() + 0x101);
    NTSTATUS st = QIP(QP_PSEUDO_SELF, 0xFFFF, qp_buf() + 0x200, 4, prl);
    EXPECT_EQ(st, QS_BAD_CLASS);
    st = QIP(QP_PSEUDO_SELF, 24, qp_buf() + 0x200, 4, prl);
    EXPECT_EQ(st, QS_OK);
}
T5(gap_remote_child_handles) {
    if (!QIP) { t_skip("missing"); return; }
    PROCESS_INFORMATION pi;
    HANDLE ph = qp_spawn('n', &pi);
    if (!ph) { t_skip("cannot start helper process"); return; }
    for (int k = H_CH_LIM; k <= H_CH_ZERO; k++) {
        HANDLE h = qp_hmake(k, ph);
        for (int i = 0; i < qp_ngap && h; i++) {
            NTSTATUS st = QIP(h, qp_gap_ids[i], qp_buf(), 4, NULL);
            t_check(st == QS_BAD_CLASS, __FILE__, __LINE__, "child handle, invalid id", "%s id 0x%lx st 0x%lx", qp_hname[k], qp_gap_ids[i], (unsigned long)st);
        }
        qp_hrelease(k, h);
    }
    qp_kill(ph, &pi);
}
/* the thread-information id space is separate: an id that is valid there is not special here */
T5(gap_ids_valid_only_for_other_calls_are_still_invalid) {
    if (!QIP) { t_skip("missing"); return; }
    /* 5 and 9 are accepted by the set call of the same family; the query refuses them */
    EXPECT_EQ(QIP(QP_PSEUDO_SELF, 9, qp_buf(), 8, NULL), QS_BAD_CLASS);
    EXPECT_EQ(QIP(QP_PSEUDO_SELF, 5, qp_buf(), 4, NULL), QS_BAD_CLASS);
    EXPECT_EQ(QIP(QP_PSEUDO_SELF, 8, qp_buf(), 8, NULL), QS_BAD_CLASS);
}
