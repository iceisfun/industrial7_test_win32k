/* One TEST per information class that exists on Windows 10 22H2 x64 (71 classes): the length protocol of a
 * query on the current-process pseudo handle. Every length from 0 to 0x200 plus 0x1000 is tried; a length the
 * class accepts gives the class's status, every other length gives STATUS_INFO_LENGTH_MISMATCH. The
 * ReturnLength cell is pre-filled with a sentinel and must be written (value per class) or left untouched
 * exactly as observed on Windows 10. A NULL ReturnLength is accepted by every class but the group-array one. */
#include "qp.h"

static void scan(const qp_cls *c) {
    static const ULONG extra[] = { 0x1000 };
    for (ULONG i = 0; i <= 0x200 + 1; i++) {
        ULONG len = i <= 0x200 ? i : extra[0];
        unsigned char *b = qp_buf();
        ULONG rl; int w;
        NTSTATUS st = qp_q(QP_PSEUDO_SELF, c->id, b, len, &rl, &w);
        NTSTATUS want = qp_exp_len(c, len, QP_PSEUDO_SELF);
        t_check(st == want, __FILE__, __LINE__, "status == expected", "class %lu len 0x%lx got 0x%lx want 0x%lx",
                c->id, len, (unsigned long)st, (unsigned long)want);
        if (st != want) { qp_cleanup_out(c, st, b); continue; }
        ULONG mod; long code = qp_exp_rl(c, len, st, &mod);
        if (code == -1) t_check(!w, __FILE__, __LINE__, "RL untouched", "class %lu len 0x%lx rl written 0x%lx", c->id, len, rl);
        else if (code == -3) t_check(w && (mod == 0 || rl % mod == 0), __FILE__, __LINE__, "RL written", "class %lu len 0x%lx w=%d rl 0x%lx", c->id, len, w, rl);
        else t_check(w && rl == (ULONG)code, __FILE__, __LINE__, "RL value", "class %lu len 0x%lx w=%d rl 0x%lx want 0x%lx", c->id, len, w, rl, (unsigned long)code);
        qp_cleanup_out(c, st, b);
    }
}
static void null_rl(const qp_cls *c) {
    ULONG lens[3] = { c->oklen, 0, 3 };
    for (int i = 0; i < 3; i++) {
        unsigned char *b = qp_buf();
        NTSTATUS st = QIP(QP_PSEUDO_SELF, c->id, b, lens[i], NULL);
        NTSTATUS want = c->id == 47 ? QS_BAD_PARAM : qp_exp_len(c, lens[i], QP_PSEUDO_SELF);
        t_check(st == want, __FILE__, __LINE__, "NULL RL status", "class %lu len 0x%lx got 0x%lx want 0x%lx", c->id, lens[i], (unsigned long)st, (unsigned long)want);
        qp_cleanup_out(c, st, b);
    }
}
static void run_class(ULONG id) {
    if (!QIP) { t_skip("NtQueryInformationProcess missing"); return; }
    const qp_cls *c = qp_find(id);
    if (!c) { CHECKF(0, "class %lu missing from table", id); return; }
    if (c->kind == K_VAR) { qp_run_var(c); return; }
    scan(c);
    null_rl(c);
}
#define CLT(id) T5(class_##id) { run_class(id); }
CLT(0) CLT(1) CLT(2) CLT(3) CLT(4) CLT(7) CLT(10) CLT(12) CLT(14) CLT(15) CLT(18) CLT(19) CLT(20) CLT(21) CLT(22)
CLT(23) CLT(24) CLT(26) CLT(27) CLT(28) CLT(29) CLT(30) CLT(31) CLT(32) CLT(33) CLT(34) CLT(36) CLT(37) CLT(38)
CLT(39) CLT(42) CLT(43) CLT(44) CLT(45) CLT(46) CLT(47) CLT(49) CLT(50) CLT(51) CLT(52) CLT(54) CLT(55) CLT(58)
CLT(59) CLT(60) CLT(61) CLT(64) CLT(65) CLT(66) CLT(67) CLT(69) CLT(70) CLT(71) CLT(72) CLT(73) CLT(74) CLT(75)
CLT(76) CLT(77) CLT(79) CLT(81) CLT(82) CLT(84) CLT(85) CLT(87) CLT(88) CLT(89) CLT(92) CLT(94) CLT(96) CLT(97)

/* the table covers exactly the 71 classes of the reference list and nothing from the gap list */
T5(table_has_71_classes) {
    EXPECT_EQ(qp_nclasses, 71);
    for (int i = 1; i < qp_nclasses; i++) CHECKF(qp_classes[i].id > qp_classes[i - 1].id, "table not sorted at %d", i);
}
T5(table_and_gap_partition_0_to_127) {
    for (ULONG id = 0; id < 128; id++) {
        int in_t = qp_find(id) != NULL, in_g = 0;
        for (int i = 0; i < qp_ngap; i++) if (qp_gap_ids[i] == id) in_g = 1;
        CHECKF(in_t != in_g, "id %lu valid=%d gap=%d", id, in_t, in_g);
    }
}
/* an output buffer is not required to be touched by a refused query: buffer bytes stay as the caller left them */
T5(length_mismatch_leaves_buffer_alone) {
    if (!QIP) { t_skip("missing"); return; }
    for (int i = 0; i < qp_nclasses; i++) {
        const qp_cls *c = &qp_classes[i];
        if (c->kind != K_SET) continue;
        unsigned char *b = qp_buf();
        memset(b, 0xCC, 0x100);
        ULONG len = c->oklen + 1;
        if (qp_exp_len(c, len, QP_PSEUDO_SELF) != QS_LEN_MISMATCH) continue;
        NTSTATUS st = QIP(QP_PSEUDO_SELF, c->id, b, len, NULL);
        CHECKF(st == QS_LEN_MISMATCH, "class %lu got 0x%lx", c->id, (unsigned long)st);
        int clean = 1;
        for (int k = 0; k < 0x100; k++) if (b[k] != 0xCC) clean = 0;
        CHECKF(clean, "class %lu wrote into the buffer on a length mismatch", c->id);
    }
}
