/* The classes whose output size depends on the process: image path (NT and DOS form), command line, window
 * information, telemetry id, handle snapshot, and the one whose answer depends on the account (class 85).
 * Protocol checked: a zero-length probe fails with STATUS_INFO_LENGTH_MISMATCH and reports the size needed in
 * ReturnLength; that exact size succeeds and reports the same value; one byte less fails; a larger buffer
 * succeeds and reports the used size. Contents are only checked through relations. */
#include "qp.h"

static void check_unicode_header(const qp_cls *c, unsigned char *b, ULONG rl) {
    UNICODE_STRING *u = (UNICODE_STRING *)b;
    CHECKF(u->Length % 2 == 0, "class %lu odd length", c->id);
    CHECKF(u->Length + sizeof(UNICODE_STRING) <= rl, "class %lu string longer than return length", c->id);
    CHECKF(u->MaximumLength >= u->Length, "class %lu maximum < length", c->id);
    CHECKF((unsigned char *)u->Buffer >= b + sizeof(UNICODE_STRING) && (unsigned char *)u->Buffer <= b + rl,
           "class %lu string buffer does not point inside the caller buffer", c->id);
}
static void generic_var(const qp_cls *c, ULONG min_need) {
    unsigned char *b = qp_buf();
    ULONG rl = 0; int w = 0;
    NTSTATUS st = qp_q(QP_PSEUDO_SELF, c->id, b, 0, &rl, &w);
    t_check(st == QS_LEN_MISMATCH && w, __FILE__, __LINE__, "len 0 reports size", "class %lu st 0x%lx w=%d", c->id, (unsigned long)st, w);
    ULONG need = rl;
    t_check(need >= min_need && need < 0x1000, __FILE__, __LINE__, "needed size plausible", "class %lu need 0x%lx", c->id, need);
    for (ULONG l = 1; l < need && l < 0x20; l++) {
        b = qp_buf();
        st = qp_q(QP_PSEUDO_SELF, c->id, b, l, &rl, &w);
        t_check((st == QS_LEN_MISMATCH || st == QS_OVERFLOW) && w && rl == need, __FILE__, __LINE__, "short buffer", "class %lu len %lu st 0x%lx w=%d rl 0x%lx need 0x%lx", c->id, l, (unsigned long)st, w, rl, need);
    }
    b = qp_buf();
    st = qp_q(QP_PSEUDO_SELF, c->id, b, need - 1, &rl, &w);
    t_check((st == QS_LEN_MISMATCH || st == QS_OVERFLOW) && w && rl == need, __FILE__, __LINE__, "need-1", "class %lu st 0x%lx rl 0x%lx need 0x%lx", c->id, (unsigned long)st, rl, need);
    b = qp_buf();
    st = qp_q(QP_PSEUDO_SELF, c->id, b, need, &rl, &w);
    t_check(st == QS_OK && w && rl == need, __FILE__, __LINE__, "exact size", "class %lu st 0x%lx rl 0x%lx need 0x%lx", c->id, (unsigned long)st, rl, need);
    if (c->id == 27 || c->id == 43 || c->id == 60) check_unicode_header(c, b, rl);
    b = qp_buf();
    st = qp_q(QP_PSEUDO_SELF, c->id, b, need + 0x100, &rl, &w);
    t_check(st == QS_OK && w && rl == need, __FILE__, __LINE__, "bigger buffer", "class %lu st 0x%lx rl 0x%lx need 0x%lx", c->id, (unsigned long)st, rl, need);
    b = qp_buf();
    st = QIP(QP_PSEUDO_SELF, c->id, b, need, NULL);
    t_check(st == QS_OK, __FILE__, __LINE__, "NULL RL accepted", "class %lu st 0x%lx", c->id, (unsigned long)st);
    if (c->id == 27) { UNICODE_STRING *u = (UNICODE_STRING *)b; CHECK(u->Length >= 8 && !wcsncmp(u->Buffer, L"\\Device\\", 8)); }
    if (c->id == 43) { UNICODE_STRING *u = (UNICODE_STRING *)b; CHECK(u->Length >= 6 && u->Buffer[1] == L':' && u->Buffer[2] == L'\\'); }
    if (c->id == 60) { UNICODE_STRING *u = (UNICODE_STRING *)b; CHECK(u->Length > 0); }
    if (c->id == 50) {
        USHORT tl = *(USHORT *)(b + 4);
        CHECKF(need == 6u + tl, "title length %u need 0x%lx", (unsigned)tl, need);
    }
}
static void handle_snapshot(const qp_cls *c) {
    unsigned char *b = qp_buf();
    ULONG rl = 0; int w = 0;
    for (ULONG l = 0; l < 0x10; l++) {
        NTSTATUS st = qp_q(QP_PSEUDO_SELF, 51, b, l, &rl, &w);
        t_check(st == QS_LEN_MISMATCH && w && rl == 0x10, __FILE__, __LINE__, "short header", "len %lu st 0x%lx w=%d rl 0x%lx", l, (unsigned long)st, w, rl);
    }
    NTSTATUS st = qp_q(QP_PSEUDO_SELF, 51, b, 0x10, &rl, &w);
    t_check(st == QS_LEN_MISMATCH && w && rl > 0x10, __FILE__, __LINE__, "header only is too small", "st 0x%lx rl 0x%lx", (unsigned long)st, rl);
    ULONG need = rl;
    st = qp_q(QP_PSEUDO_SELF, 51, b, need + 0x400, &rl, &w);
    t_check(st == QS_OK && w && rl <= need + 0x400 && rl >= 0x10, __FILE__, __LINE__, "large buffer", "st 0x%lx rl 0x%lx", (unsigned long)st, rl);
    if (st == QS_OK) {
        ULONG_PTR n = *(ULONG_PTR *)b;
        CHECKF((rl - 0x10) % 0x28 == 0 && n == (rl - 0x10) / 0x28, "count %llu rl 0x%lx", (unsigned long long)n, rl);
        CHECK(n >= 3);
    }
}
void qp_run_var(const qp_cls *c) {
    switch (c->id) {
    case 27: case 43: generic_var(c, 0x10); break;
    case 60: generic_var(c, 0x10); break;
    case 50: generic_var(c, 6); break;
    case 51: handle_snapshot(c); break;
    case 64: {
        unsigned char *b = qp_buf(); ULONG rl; int w;
        NTSTATUS st = qp_q(QP_PSEUDO_SELF, 64, b, 0, &rl, &w);
        t_check(st == QS_LEN_MISMATCH && w && rl > 0x60, __FILE__, __LINE__, "len 0", "st 0x%lx rl 0x%lx", (unsigned long)st, rl);
        ULONG need = rl;
        st = qp_q(QP_PSEUDO_SELF, 64, b, need, &rl, &w);
        t_check(st == QS_OK && rl == need, __FILE__, __LINE__, "exact", "st 0x%lx rl 0x%lx", (unsigned long)st, rl);
        st = qp_q(QP_PSEUDO_SELF, 64, b, need - 1, &rl, &w);
        t_check((st == QS_OVERFLOW || st == QS_LEN_MISMATCH) && rl == need, __FILE__, __LINE__, "need-1", "st 0x%lx", (unsigned long)st);
        break; }
    case 85: {
        unsigned char *b = qp_buf(); ULONG rl; int w;
        NTSTATUS st = qp_q(QP_PSEUDO_SELF, 85, b, 0x100, &rl, &w);
        t_note("class 85 answers in a way that depends on the account (status class: %s)", st == QS_ACCESS_DENIED ? "refused" : "answered");
        CHECKF(st == QS_ACCESS_DENIED || st == QS_OVERFLOW || st == QS_OK, "unexpected status 0x%lx", (unsigned long)st);
        /* the handle is looked at before the length for this class */
        st = qp_q(NULL, 85, b, 0x100, &rl, &w);
        CHECKF(st == QS_BAD_HANDLE, "null handle 0x%lx", (unsigned long)st);
        break; }
    }
}
