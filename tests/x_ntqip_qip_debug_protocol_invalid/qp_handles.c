/* The handle matrix of NtQueryInformationProcess, one TEST per handle kind, each swept over every class of the
 * table. An invalid handle gives STATUS_INVALID_HANDLE, the thread pseudo handle and handles of other object
 * types STATUS_OBJECT_TYPE_MISMATCH, a process handle without the needed right STATUS_ACCESS_DENIED. Whether the
 * length or the handle is judged first differs per class (table columns): a good length with a bad handle gives
 * the handle error, a bad length gives STATUS_INFO_LENGTH_MISMATCH for most classes and the handle error for the
 * others. The remote kinds use an idle helper process opened with the right named by the kind. */
#include "qp.h"

static void sweep_bad(int kind, HANDLE ph) {
    if (!QIP) { t_skip("missing"); return; }
    for (int i = 0; i < qp_nclasses; i++) {
        const qp_cls *c = &qp_classes[i];
        ULONG lens[3] = { c->oklen, 3, 0 };
        for (int j = 0; j < 3; j++) {
            if (j == 1 && lens[1] == c->oklen) continue;
            HANDLE h = qp_hmake(kind, ph);
            if (!h && kind != H_NULL && kind != H_THR) { t_skip("cannot make a %s handle", qp_hname[kind]); return; }
            ULONG rl; int w;
            NTSTATUS st = qp_q(h, c->id, qp_buf(), lens[j], &rl, &w);
            NTSTATUS want = qp_exp_handle(c, kind, lens[j], h);
            t_check(qp_match(want, st), __FILE__, __LINE__, "handle status", "%s class %lu len 0x%lx got 0x%lx want 0x%lx", qp_hname[kind], c->id, lens[j], (unsigned long)st, (unsigned long)want);
            qp_cleanup_out(c, st, qp_buf());
            qp_hrelease(kind, h);
        }
    }
}
static void sweep_good(int kind, HANDLE ph) {
    if (!QIP) { t_skip("missing"); return; }
    for (int i = 0; i < qp_nclasses; i++) {
        const qp_cls *c = &qp_classes[i];
        HANDLE h = qp_hmake(kind, ph);
        if (!h) { t_skip("cannot make a %s handle", qp_hname[kind]); return; }
        unsigned char *b = qp_buf();
        ULONG rl0 = 0;
        NTSTATUS st = QIP(h, c->id, b, c->oklen, &rl0);
        NTSTATUS want = qp_exp_handle(c, kind, c->oklen, h);
        t_check(qp_match(want, st), __FILE__, __LINE__, "access status", "%s class %lu got 0x%lx want 0x%lx", qp_hname[kind], c->id, (unsigned long)st, (unsigned long)want);
        qp_cleanup_out(c, st, b);
        qp_hrelease(kind, h);
    }
}
#define BADT(name, kind) T5(handle_##name) { sweep_bad(kind, NULL); }
BADT(null, H_NULL) BADT(thread_pseudo_handle, H_THR) BADT(minus3, H_M3) BADT(closed, H_CLOSED) BADT(event_handle, H_EVENT)
BADT(thread_handle, H_THREAD) BADT(far_invalid_value, H_FAR) BADT(self_sync_only, H_SYNC) BADT(self_zero_rights, H_ZERO)
#define GOODT(name, kind) T5(handle_##name) { sweep_good(kind, NULL); }
GOODT(self_limited_right, H_LIM) GOODT(self_query_right, H_QRY)

static void remote(int kind, int bad) {
    PROCESS_INFORMATION pi;
    HANDLE ph = qp_spawn('n', &pi);
    if (!ph) { t_skip("cannot start helper process"); return; }
    if (bad) sweep_bad(kind, ph); else sweep_good(kind, ph);
    qp_kill(ph, &pi);
}
T5(handle_child_limited_right) { remote(H_CH_LIM, 0); }
T5(handle_child_query_right) { remote(H_CH_QRY, 0); }
T5(handle_child_sync_only) { remote(H_CH_SYNC, 1); }
T5(handle_child_zero_rights) { remote(H_CH_ZERO, 1); }

/* the pseudo handle and a real handle to the caller are interchangeable except where the table says otherwise */
T5(handle_current_equals_real_self_handle_for_limited_classes) {
    if (!QIP) { t_skip("missing"); return; }
    HANDLE h = qp_hmake(H_QRY, NULL);
    for (int i = 0; i < qp_nclasses; i++) {
        const qp_cls *c = &qp_classes[i];
        if (c->need != 'L' && c->need != 'Q') continue;
        if (c->kind == K_VAR) continue;
        NTSTATUS a = QIP(QP_PSEUDO_SELF, c->id, qp_buf(), c->oklen, NULL);
        qp_cleanup_out(c, a, qp_buf());
        NTSTATUS b = QIP(h, c->id, qp_buf(), c->oklen, NULL);
        qp_cleanup_out(c, b, qp_buf());
        CHECKF(a == b, "class %lu pseudo 0x%lx real 0x%lx", c->id, (unsigned long)a, (unsigned long)b);
    }
    qp_hrelease(H_QRY, h);
}
/* classes whose pseudo-handle-only rule makes a real handle to oneself fail */
T5(handle_pseudo_only_classes_refuse_real_self_handle) {
    if (!QIP) { t_skip("missing"); return; }
    HANDLE h = qp_hmake(H_QRY, NULL);
    ULONG rl; int w;
    NTSTATUS st = qp_q(h, 89, qp_buf(), 8, &rl, &w);
    EXPECT_EQ(st, QS_ACCESS_DENIED);
    st = qp_q(h, 97, qp_buf(), 8, &rl, &w);
    EXPECT_EQ(st, QS_BAD_PARAM);
    qp_hrelease(H_QRY, h);
    st = qp_q(QP_PSEUDO_SELF, 89, qp_buf(), 8, &rl, &w);
    EXPECT_EQ(st, QS_OK);
    qp_cleanup_out(qp_find(89), st, qp_buf());
    st = qp_q(QP_PSEUDO_SELF, 97, qp_buf(), 8, &rl, &w);
    EXPECT_EQ(st, QS_OK);
}
T5(handle_class_89_returns_a_section_handle) {
    if (!QIP) { t_skip("missing"); return; }
    HANDLE out = (HANDLE)0x1234;
    ULONG rl = QP_SENT;
    NTSTATUS st = QIP(QP_PSEUDO_SELF, 89, &out, 8, &rl);
    EXPECT_EQ(st, QS_OK);
    EXPECT_EQ(rl, 8);
    if (st == QS_OK && out) { CHECK(qp_type_is(out, L"Section")); CloseHandle(out); } else CHECK(0);
}
T5(handle_class_28_ignores_the_handle) {
    if (!QIP) { t_skip("missing"); return; }
    for (int k = H_NULL; k <= H_ZERO; k++) {
        HANDLE h = qp_hmake(k, NULL);
        ULONG v = 0xFF, rl;
        int w;
        NTSTATUS st = qp_q(h, 28, &v, 4, &rl, &w);
        t_check(st == QS_OK && v == 1 && w && rl == 4, __FILE__, __LINE__, "class 28", "%s st 0x%lx v %lu", qp_hname[k], (unsigned long)st, v);
        qp_hrelease(k, h);
    }
}
T5(handle_class_81_refuses_every_caller_and_handle) {
    if (!QIP) { t_skip("missing"); return; }
    for (ULONG len = 0; len < 0x40; len += 4) {
        NTSTATUS st = QIP(QP_PSEUDO_SELF, 81, qp_buf(), len, NULL);
        t_check(st == QS_ACCESS_DENIED, __FILE__, __LINE__, "class 81", "len %lu st 0x%lx", len, (unsigned long)st);
    }
}
T5(handle_length_checked_before_handle_classes) {
    if (!QIP) { t_skip("missing"); return; }
    int n = 0;
    for (int i = 0; i < qp_nclasses; i++) {
        const qp_cls *c = &qp_classes[i];
        if (c->n_3 != QS_LEN_MISMATCH || c->n_ok != QS_BAD_HANDLE) continue;
        n++;
        NTSTATUS st = QIP(NULL, c->id, qp_buf(), c->oklen, NULL);
        CHECKF(st == QS_BAD_HANDLE, "class %lu good length, null handle 0x%lx", c->id, (unsigned long)st);
        st = QIP(NULL, c->id, qp_buf(), 3, NULL);
        CHECKF(st == QS_LEN_MISMATCH, "class %lu bad length, null handle 0x%lx", c->id, (unsigned long)st);
    }
    CHECK(n > 40);
}
T5(handle_handle_checked_before_length_classes) {
    if (!QIP) { t_skip("missing"); return; }
    static const ULONG ids[] = { 10, 15, 27, 43, 50, 60, 61, 64, 72, 76, 85, 88 };
    for (int i = 0; i < 12; i++) {
        NTSTATUS st = QIP(NULL, ids[i], qp_buf(), 3, NULL);
        CHECKF(st == QS_BAD_HANDLE, "class %lu bad length, null handle 0x%lx", ids[i], (unsigned long)st);
    }
}
