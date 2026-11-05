/* Cross-class protocol table for the classes of this area: for each class, with its exact length, the order in
 * which problems are reported (alignment, length, handle type, access right), the ReturnLength protocol and the
 * equivalence of the pseudo handle with a real handle to the same process. Also prints, once, the integrity and
 * elevation state of the token the area ran with.
 */
#include "qh.h"

static const unsigned char IN52[8] = {2, 0, 0, 0, 0x55, 0x55, 0x55, 0x55};
static const unsigned char IN65[0x20] = {3};

static const void *input_of(ULONG cls, ULONG *n) {
    if (cls == 52) { *n = 8; return IN52; }
    if (cls == 65) { *n = 0x20; return IN65; }
    *n = 0; return NULL;
}

TEST(x_token_state_note) {
    unsigned long il = k_integrity_level();
    t_note("token integrity %s, elevated %d", il >= 0x3000 ? "high-or-above" : il >= 0x2000 ? "medium" : "low-or-below", k_is_elevated() ? 1 : 0);
}

TEST(x_handle_error_matrix) {
    HANDLE closed = NULL, ev = CreateEventA(NULL, TRUE, FALSE, NULL), sync = qh_open(SYNCHRONIZE, 0), vmr = qh_open(PROCESS_VM_READ, 0);
    closed = qh_closed_handle();   /* last: nothing may be opened after it, or the value would be reused */
    for (int i = 0; i < qh_nclasses; i++) {
        ULONG c = qh_classes[i].cls, n = qh_classes[i].len, il; const void *in = input_of(c, &il);
        ULONG skip = il;
        if (c == 81) {   /* refused for every handle */
            qh_err("null", NULL, c, n, ST_DENIED, in, il, skip); qh_err("event", ev, c, n, ST_DENIED, in, il, skip);
            continue;
        }
        if (c == 72) skip = 0;
        qh_err("null", NULL, c, n, ST_INV_HANDLE, in, il, skip);
        if (closed) qh_err("closed", closed, c, n, ST_INV_HANDLE, in, il, skip);
        qh_err("thread pseudo", THREAD_PSEUDO, c, n, ST_TYPE_MISMATCH, in, il, skip);
        qh_err("event", ev, c, n, ST_TYPE_MISMATCH, in, il, skip);
        if (sync) qh_err("sync only", sync, c, n, ST_DENIED, in, il, skip);
        if (vmr && c != 77) qh_err("vm read only", vmr, c, n, ST_DENIED, in, il, skip);
    }
    CloseHandle(ev); if (sync) CloseHandle(sync); if (vmr) CloseHandle(vmr);
}

TEST(x_wrong_length_beats_handle_for_exact_classes) {
    static const ULONG exact[] = {52, 69, 70, 73, 75, 79, 77, 84, 65};
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    for (int i = 0; i < sizeof exact / sizeof *exact; i++) {
        ULONG n = 0; for (int k = 0; k < qh_nclasses; k++) if (qh_classes[k].cls == exact[i]) n = qh_classes[k].len;
        qh_err("null +1", NULL, exact[i], n + 1, ST_LEN, NULL, 0, 0);
        qh_err("thread -1", THREAD_PSEUDO, exact[i], n - 1, ST_LEN, NULL, 0, 0);
        qh_err("event +4", ev, exact[i], n + 4, ST_LEN, NULL, 0, 0);
        qh_err("pseudo 0", SELF, exact[i], 0, ST_LEN, NULL, 0, 0);
    }
    CloseHandle(ev);
}

TEST(x_misaligned_beats_everything_except_class_70) {
    static unsigned char raw[0x200] __attribute__((aligned(16)));
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    for (int i = 0; i < qh_nclasses; i++) {
        ULONG c = qh_classes[i].cls, n = qh_classes[i].len;
        for (int off = 1; off < 4; off++) {
            unsigned char *p = raw + 16 + off;
            ULONG want = c == 70 ? ST_INV_HANDLE : ST_MISALIGN;
            ULONG s1 = qh(NULL, c, p, c == 70 ? n : n, NULL), s2 = qh(ev, c, p, n + 3, NULL), s3 = qh(SELF, c, p, 0 + (c == 70 ? n : 1), NULL);
            if (c == 81) want = ST_MISALIGN;
            if (c == 70) { CHECKF(s1 == ST_INV_HANDLE, "70 null handle off %d: 0x%lx", off, s1); continue; }
            CHECKF(s1 == want && s2 == want && s3 == want, "cls %lu off %d: 0x%lx 0x%lx 0x%lx", c, off, s1, s2, s3);
        }
    }
    CloseHandle(ev);
}

TEST(x_return_length_protocol_on_success) {
    /* which classes write ReturnLength on success, and what they write */
    static const struct { ULONG cls, len, rl; } T[] = {
        {52, 8, RL_UNSET}, {70, 1, RL_UNSET}, {65, 0x20, RL_UNSET}, {73, 3, 3}, {75, 4, 4}, {79, 8, 8}, {76, 0x1b0, 0x1b0}, {82, 0x90, 0x90}, {88, 0x38, 0x38},
    };
    for (int i = 0; i < sizeof T / sizeof *T; i++) {
        ULONG il; const void *in = input_of(T[i].cls, &il);
        struct qh_res r = qh_probe(SELF, T[i].cls, T[i].len, in, il, il);
        CHECKF(r.st == ST_OK, "cls %lu: 0x%lx", T[i].cls, r.st);
        CHECKF(r.rl == T[i].rl, "cls %lu: ReturnLength 0x%lx want 0x%lx", T[i].cls, r.rl, T[i].rl);
    }
}

TEST(x_null_return_length_accepted_everywhere) {
    static unsigned char buf[0x200] __attribute__((aligned(8)));
    for (int i = 0; i < qh_nclasses; i++) {
        ULONG c = qh_classes[i].cls, n = qh_classes[i].len, il; const void *in = input_of(c, &il);
        if (c == 81 || c == 84 || c == 72 || c == 77 || c == 69) continue;
        memset(buf, 0, sizeof buf); if (in) memcpy(buf, in, il);
        CHECKF(qh(SELF, c, buf, n, NULL) == ST_OK, "cls %lu NULL ReturnLength", c);
    }
}

TEST(x_pseudo_handle_equals_real_handle) {
    HANDLE qry = qh_open(PROCESS_QUERY_INFORMATION, 0), lim = qh_open(PROCESS_QUERY_LIMITED_INFORMATION, 0);
    REQUIRE(qry && lim);
    static const ULONG stable[] = {52, 70, 73, 75, 79, 82, 76, 65};
    for (int i = 0; i < sizeof stable / sizeof *stable; i++) {
        ULONG n = 0; for (int k = 0; k < qh_nclasses; k++) if (qh_classes[k].cls == stable[i]) n = qh_classes[k].len;
        ULONG il; const void *in = input_of(stable[i], &il);
        struct qh_res a = qh_probe(SELF, stable[i], n, in, il, il), b = qh_probe(qry, stable[i], n, in, il, il);
        CHECKF(a.st == b.st && a.rl == b.rl && !memcmp(a.out, b.out, 64), "cls %lu: pseudo/qry differ (0x%lx/0x%lx)", stable[i], a.st, b.st);
        if (stable[i] != 73 && stable[i] != 79) {
            struct qh_res c = qh_probe(lim, stable[i], n, in, il, il);
            CHECKF(a.st == c.st && a.rl == c.rl && !memcmp(a.out, c.out, 64), "cls %lu: pseudo/lim differ (0x%lx/0x%lx)", stable[i], a.st, c.st);
        }
    }
    CloseHandle(qry); CloseHandle(lim);
}

TEST(x_error_leaves_return_length_alone) {
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    for (int i = 0; i < qh_nclasses; i++) {
        ULONG c = qh_classes[i].cls, n = qh_classes[i].len, rl;
        static unsigned char buf[0x200] __attribute__((aligned(8)));
        memset(buf, 0, sizeof buf);
        ULONG st = qh(ev, c, buf, n, &rl);
        CHECKF(st != ST_OK, "cls %lu: event handle accepted", c);
        CHECKF(rl == RL_UNSET, "cls %lu: ReturnLength written on a handle error (0x%lx)", c, rl);
    }
    CloseHandle(ev);
}

TEST(x_closed_then_reopened_handle_works_again) {
    HANDLE h = qh_open(PROCESS_QUERY_LIMITED_INFORMATION, 0);
    REQUIRE(h);
    struct qh_res a = qh_probe(h, 75, 4, NULL, 0, 0);
    CHECKF(a.st == ST_OK, "open: 0x%lx", a.st);
    CloseHandle(h);
    struct qh_res b = qh_probe(h, 75, 4, NULL, 0, 0);
    CHECKF(b.st == ST_INV_HANDLE, "closed: 0x%lx", b.st);
    h = qh_open(PROCESS_QUERY_LIMITED_INFORMATION, 0);
    if (h) { struct qh_res c = qh_probe(h, 75, 4, NULL, 0, 0); CHECKF(c.st == ST_OK, "reopened: 0x%lx", c.st); CloseHandle(h); }
}

TEST(x_inherited_handle_value_in_child_context) {
    /* a handle opened for the parent's own process, queried from the parent after the child exited, still works */
    struct qh_child c;
    if (!qh_child_start(&c, QC_PLAIN, NULL)) { t_skip("cannot start child"); return; }
    HANDLE h = qh_open(PROCESS_QUERY_LIMITED_INFORMATION, c.pid);
    REQUIRE(h);
    struct qh_res a = qh_probe(h, 75, 4, NULL, 0, 0);
    CHECKF(a.st == ST_OK && a.out[0] == 0, "alive: 0x%lx", a.st);
    qh_child_stop(&c);
    struct qh_res b = qh_probe(h, 75, 4, NULL, 0, 0);
    CHECKF(b.st == ST_OK, "after exit: 0x%lx", b.st);
    CloseHandle(h);
}
