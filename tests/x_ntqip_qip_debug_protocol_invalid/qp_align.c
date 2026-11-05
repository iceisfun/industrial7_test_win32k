/* Buffer alignment, NULL buffers and ReturnLength pointers. A non-empty buffer must be 4-byte aligned for nearly
 * every class (even those with 8-byte fields), 8-byte aligned for three classes, and byte aligned for four; a
 * misaligned buffer gives STATUS_DATATYPE_MISALIGNMENT as a status, before the class and the handle are
 * examined. A zero length is never checked for alignment. */
#include "qp.h"

static int align_need(const qp_cls *c) {
    switch (c->id) { case 61: case 70: case 74: case 87: return 1; case 65: case 92: case 94: return 8; }
    return 4;
}
static void sweep_off(int off) {
    if (!QIP) { t_skip("missing"); return; }
    for (int i = 0; i < qp_nclasses; i++) {
        const qp_cls *c = &qp_classes[i];
        if (c->id == 85) continue;
        int need = align_need(c);
        int bad = (off % need) != 0;
        unsigned char *b = qp_buf();
        ULONG rl0 = 0;
        NTSTATUS st = QIP(QP_PSEUDO_SELF, c->id, b + off, c->oklen, &rl0);
        NTSTATUS want = bad ? QS_MISALIGNED : qp_exp_handle(c, H_CUR, c->oklen, QP_PSEUDO_SELF);
        t_check(qp_match(want, st), __FILE__, __LINE__, "alignment", "class %lu off %d got 0x%lx want 0x%lx", c->id, off, (unsigned long)st, (unsigned long)want);
        if (!bad) qp_cleanup_out(c, st, b + off);
    }
}
T5(align_offset_1) { sweep_off(1); }
T5(align_offset_2) { sweep_off(2); }
T5(align_offset_3) { sweep_off(3); }
T5(align_offset_4) { sweep_off(4); }
T5(align_offset_5) { sweep_off(5); }
T5(align_offset_6) { sweep_off(6); }
T5(align_offset_7) { sweep_off(7); }
T5(align_offset_8) { sweep_off(8); }
T5(align_offset_16) { sweep_off(16); }

T5(align_checked_before_the_handle) {
    if (!QIP) { t_skip("missing"); return; }
    for (int i = 0; i < qp_nclasses; i++) {
        const qp_cls *c = &qp_classes[i];
        if (c->id == 85 || align_need(c) == 1) continue;
        NTSTATUS st = QIP(NULL, c->id, qp_buf() + 1, c->oklen, NULL);
        CHECKF(st == QS_MISALIGNED, "class %lu null handle misaligned 0x%lx", c->id, (unsigned long)st);
        HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
        st = QIP(ev, c->id, qp_buf() + 2, c->oklen, NULL);
        CHECKF(st == QS_MISALIGNED, "class %lu event handle misaligned 0x%lx", c->id, (unsigned long)st);
        CloseHandle(ev);
    }
}
T5(align_zero_length_not_checked) {
    if (!QIP) { t_skip("missing"); return; }
    for (int i = 0; i < qp_nclasses; i++) {
        const qp_cls *c = &qp_classes[i];
        if (c->id == 85) continue;
        NTSTATUS a = QIP(QP_PSEUDO_SELF, c->id, qp_buf() + 1, 0, NULL);
        NTSTATUS b = QIP(QP_PSEUDO_SELF, c->id, qp_buf(), 0, NULL);
        CHECKF(a == b && a != QS_MISALIGNED, "class %lu len 0: misaligned 0x%lx aligned 0x%lx", c->id, (unsigned long)a, (unsigned long)b);
        a = QIP(QP_PSEUDO_SELF, c->id, NULL, 0, NULL);
        CHECKF(a == b, "class %lu len 0 NULL buffer 0x%lx", c->id, (unsigned long)a);
    }
}
T5(align_eight_byte_classes_reject_a_four_byte_offset) {
    if (!QIP) { t_skip("missing"); return; }
    static const ULONG ids[] = { 65, 92, 94 };
    for (int i = 0; i < 3; i++) {
        const qp_cls *c = qp_find(ids[i]);
        EXPECT_EQ(QIP(QP_PSEUDO_SELF, ids[i], qp_buf() + 4, c->oklen, NULL), QS_MISALIGNED);
        NTSTATUS st = QIP(QP_PSEUDO_SELF, ids[i], qp_buf() + 8, c->oklen, NULL);
        EXPECT_EQ(st, c->okst);
    }
    /* the other 8-byte-wide classes only need 4-byte alignment */
    EXPECT_EQ(QIP(QP_PSEUDO_SELF, 7, qp_buf() + 4, 8, NULL), QS_OK);
    EXPECT_EQ(QIP(QP_PSEUDO_SELF, 26, qp_buf() + 4, 8, NULL), QS_OK);
}
T5(align_byte_classes_accept_any_offset) {
    if (!QIP) { t_skip("missing"); return; }
    static const ULONG ids[] = { 61, 70, 74, 87 };
    for (int i = 0; i < 4; i++) for (int off = 0; off < 8; off++) {
        NTSTATUS st = QIP(QP_PSEUDO_SELF, ids[i], qp_buf() + off, 1, NULL);
        t_check(st == QS_OK, __FILE__, __LINE__, "byte class", "class %lu off %d st 0x%lx", ids[i], off, (unsigned long)st);
    }
}
T5(return_length_null_is_accepted_for_fixed_classes) {
    if (!QIP) { t_skip("missing"); return; }
    for (int i = 0; i < qp_nclasses; i++) {
        const qp_cls *c = &qp_classes[i];
        if (c->kind == K_VAR || c->id == 47 || c->id == 85) continue;
        NTSTATUS st = QIP(QP_PSEUDO_SELF, c->id, qp_buf(), c->oklen, NULL);
        NTSTATUS want = qp_exp_len(c, c->oklen, QP_PSEUDO_SELF);
        t_check(st == want, __FILE__, __LINE__, "NULL RL", "class %lu st 0x%lx want 0x%lx", c->id, (unsigned long)st, (unsigned long)want);
        qp_cleanup_out(c, st, qp_buf());
    }
}
T5(return_length_group_class_requires_the_pointer) {
    if (!QIP) { t_skip("missing"); return; }
    EXPECT_EQ(QIP(QP_PSEUDO_SELF, 47, qp_buf(), 2, NULL), QS_BAD_PARAM);
    EXPECT_EQ(QIP(QP_PSEUDO_SELF, 47, qp_buf(), 0, NULL), QS_BAD_PARAM);
    ULONG rl = QP_SENT;
    EXPECT_EQ(QIP(QP_PSEUDO_SELF, 47, qp_buf(), 0, &rl), QS_BUF_TOO_SMALL);
    EXPECT_EQ(rl, 2);
    rl = QP_SENT;
    EXPECT_EQ(QIP(QP_PSEUDO_SELF, 47, qp_buf(), 3, &rl), QS_BAD_PARAM);
    EXPECT_EQ(rl, QP_SENT);
    rl = QP_SENT;
    EXPECT_EQ(QIP(QP_PSEUDO_SELF, 47, qp_buf(), 2, &rl), QS_OK);
    EXPECT_EQ(rl, 2);
    CHECK(*(USHORT *)qp_buf() == 0);
}
T5(return_length_pointer_may_be_misaligned) {
    if (!QIP) { t_skip("missing"); return; }
    unsigned char *b = qp_buf();
    ULONG *prl = (ULONG *)(b + 0x101);
    memcpy(prl, "\xAA\xAA\xAA\xAA", 4);
    EXPECT_EQ(QIP(QP_PSEUDO_SELF, 24, b + 0x200, 4, prl), QS_OK);
    ULONG v; memcpy(&v, prl, 4);
    EXPECT_EQ(v, 4);
}
T5(null_buffer_with_nonzero_length_is_not_a_status_exception) {
    if (!QIP) { t_skip("missing"); return; }
    /* a NULL buffer with a plausible length is probed and refused with a status, never an exception */
    NTSTATUS st = QIP(QP_PSEUDO_SELF, 24, NULL, 4, NULL);
    EXPECT_EQ(st, QS_ACCESS_VIOL);
}
T5(null_buffer_zero_length_classes_that_accept_zero) {
    if (!QIP) { t_skip("missing"); return; }
    static const ULONG ok_ids[] = { 66, 67, 76, 88 };
    for (int i = 0; i < 4; i++) {
        ULONG rl = QP_SENT;
        NTSTATUS st = QIP(QP_PSEUDO_SELF, ok_ids[i], NULL, 0, &rl);
        t_check(st == QS_OK && rl != QP_SENT, __FILE__, __LINE__, "NULL buffer len 0", "class %lu st 0x%lx", ok_ids[i], (unsigned long)st);
    }
    EXPECT_EQ(QIP(QP_PSEUDO_SELF, 42, NULL, 0, NULL), QS_UNSUCCESSFUL);
}
