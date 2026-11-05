/* x_ntqip_qip_sched_security / t_classes: the NtQueryInformationProcess front-door protocol of the
 * scheduling, priority, security and per-process flag classes, one table row per class (see qs.c).
 *
 * What is measured on Windows 10 22H2 x64, for each class in the table:
 *   - length: fixed-size classes accept exactly their size (STATUS_INFO_LENGTH_MISMATCH otherwise, also for
 *     length 0 with or without a buffer); classes 61 and 87 take any length >= 1 (they write one byte),
 *     class 96 any length >= 4 (it writes the first byte only). A failed call leaves the output buffer and
 *     ReturnLength untouched.
 *   - ReturnLength: success reports the written size for the fixed classes and for 61, and leaves the
 *     variable always-OK classes 59/74/87/96 untouched; NULL ReturnLength is accepted.
 *   - alignment: a non-zero length with a buffer that is not aligned to the class requirement (4 bytes,
 *     8 bytes for 92 and 94; none for 61, 74 and 87) returns STATUS_DATATYPE_MISALIGNMENT before the handle is
 *     looked at; length 0 skips the test.
 *   - handles: pseudo handle -1 needs no right; real handles need PROCESS_QUERY_LIMITED_INFORMATION (or
 *     PROCESS_QUERY_INFORMATION) for most classes, PROCESS_QUERY_INFORMATION for 34 and 59, PROCESS_VM_WRITE
 *     for 36. A zero or closed handle is STATUS_INVALID_HANDLE, the thread pseudo handle or a handle to
 *     another object type STATUS_OBJECT_TYPE_MISMATCH, a missing right STATUS_ACCESS_DENIED. The length check
 *     precedes the handle lookup, except for class 61 where the handle is looked at first.
 */
#include "qs.h"

static const ULONG g_lens[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 12, 16, 0x20, 0x40, 0x100 };
static const DWORD g_masks[] = { R_SYNC, R_LIM, R_QRY, R_VMR, R_VMW, R_VMO, 0x200, 0x40, 0x1000 | R_VMR, R_QRY | R_VMR,
                                 R_LIM | R_VMW, 0x1fffff };

static int untouched(ULONG upto) {
    unsigned char *a = qs_arena();
    for (ULONG i = 0; i < upto; i++) if (a[i] != QS_FILL) return 0;
    return 1;
}

static void t_len(ULONG id) {
    const struct qs_cls *c = qs_find(id);
    if (!qs_have()) { t_skip("NtQueryInformationProcess missing"); return; }
    for (unsigned i = 0; i < sizeof g_lens / sizeof *g_lens; i++) {
        ULONG len = g_lens[i], rl;
        LONG s = qs_call((HANDLE)-1, id, 0, len, 0, 0, &rl);
        if (qs_len_ok(c, len)) {
            CHECKF(s == QS_OK, "class %lu len %lu: status 0x%lx", id, len, (unsigned long)s);
            if (s == QS_OK) {
                CHECKF(rl == (c->rl_written ? c->rl_val : QS_SENT), "class %lu len %lu: rl 0x%lx", id, len, rl);
                { int clean = 1; for (ULONG k = c->nwritten; k < 0x200; k++) if (qs_arena()[k] != QS_FILL) clean = 0;
                  CHECKF(clean, "class %lu len %lu: wrote past %lu bytes", id, len, c->nwritten); }
            }
        } else {
            CHECKF(s == QS_LENMISM, "class %lu len %lu: status 0x%lx", id, len, (unsigned long)s);
            CHECKF(rl == QS_SENT, "class %lu len %lu: rl written 0x%lx", id, len, rl);
            CHECKF(untouched(0x100), "class %lu len %lu: buffer modified on failure", id, len);
        }
    }
}
static void t_nobuf(ULONG id) {
    ULONG rl;
    if (!qs_have()) { t_skip("export missing"); return; }
    /* length 0 with a NULL buffer: never succeeds for these classes, with or without ReturnLength */
    LONG s = qs_call((HANDLE)-1, id, 0, 0, 1, 0, &rl);
    EXPECT_EQ((ULONG)s, (ULONG)QS_LENMISM);
    CHECK(rl == QS_SENT);
    s = qs_call((HANDLE)-1, id, 0, 0, 1, 1, &rl);
    EXPECT_EQ((ULONG)s, (ULONG)QS_LENMISM);
    /* NULL ReturnLength with a good length is fine, with a wrong one it is still the length error */
    const struct qs_cls *c = qs_find(id);
    s = qs_call((HANDLE)-1, id, 0, qs_good_len(c), 0, 1, &rl);
    EXPECT_EQ((ULONG)s, (ULONG)QS_OK);
    s = qs_call((HANDLE)-1, id, 0, c->mode == M_EXACT ? c->len + 1 : 0, 0, 1, &rl);
    EXPECT_EQ((ULONG)s, (ULONG)QS_LENMISM);
}
static void t_rights(ULONG id) {
    const struct qs_cls *c = qs_find(id);
    HANDLE ch = qs_child();
    if (!qs_have()) { t_skip("export missing"); return; }
    if (!ch) { t_skip("no child process"); return; }
    for (int who = 0; who < 3; who++) {
        for (unsigned i = 0; i < sizeof g_masks / sizeof *g_masks; i++) {
            DWORD m = g_masks[i]; ULONG rl;
            HANDLE h = who == 0 ? qs_dup(ch, m) : who == 1 ? qs_self_open(m) : qs_dup(qs_dead(), m);
            if (!h) { t_note("class %lu who %d mask 0x%lx: handle not available (err %lu)", id, who, m, GetLastError()); continue; }
            LONG s = qs_call(h, id, 0, qs_good_len(c), 0, 0, &rl);
            LONG want = qs_mask_ok(c->right, m) ? QS_OK : QS_DENIED;
            CHECKF(s == want, "class %lu %s handle mask 0x%lx: status 0x%lx want 0x%lx", id,
                   who == 0 ? "child" : who == 1 ? "self" : "exited", m, (unsigned long)s, (unsigned long)want);
            if (want == QS_DENIED) CHECK(rl == QS_SENT && untouched(0x40));
            CloseHandle(h);
        }
    }
    /* the pseudo handle needs no right at all */
    ULONG rl;
    EXPECT_EQ((ULONG)qs_call((HANDLE)-1, id, 0, qs_good_len(c), 0, 0, &rl), (ULONG)QS_OK);
}
typedef struct { const char *n; int kind; } hk_t;
static void t_handles(ULONG id) {
    const struct qs_cls *c = qs_find(id);
    if (!qs_have()) { t_skip("export missing"); return; }
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    HANDLE th = OpenThread(THREAD_QUERY_INFORMATION, FALSE, GetCurrentThreadId());
    HANDLE nul = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    HANDLE cl = qs_dup(qs_child(), R_LIM | R_QRY | R_VMW); if (cl) CloseHandle(cl);
    struct { const char *n; HANDLE h; LONG want; } hs[] = {
        { "null", NULL, QS_BADHANDLE },
        { "closed", cl, QS_BADHANDLE },
        { "bogus", (HANDLE)0x12344, QS_BADHANDLE },
        { "thread pseudo", (HANDLE)(ULONG_PTR)-2, QS_TYPEMISM },
        { "event", ev, QS_TYPEMISM },
        { "thread", th, QS_TYPEMISM },
        { "file", nul, QS_TYPEMISM },
    };
    for (unsigned i = 0; i < sizeof hs / sizeof *hs; i++) {
        ULONG rl;
        if (!hs[i].h && strcmp(hs[i].n, "null")) continue;
        LONG s = qs_call(hs[i].h, id, 0, qs_good_len(c), 0, 0, &rl);
        CHECKF(s == hs[i].want, "class %lu %s handle: 0x%lx want 0x%lx", id, hs[i].n, (unsigned long)s, (unsigned long)hs[i].want);
        CHECK(rl == QS_SENT);
        /* wrong length: the length error wins except for class 61 (handle first) when the length is 0 */
        ULONG bad = c->mode == M_EXACT ? c->len + 1 : (c->mode == M_MIN4 ? 3 : 0);
        s = qs_call(hs[i].h, id, 0, bad, 0, 0, &rl);
        LONG w2 = (id == 61) ? hs[i].want : QS_LENMISM;
        CHECKF(s == w2, "class %lu %s handle len %lu: 0x%lx want 0x%lx", id, hs[i].n, bad, (unsigned long)s, (unsigned long)w2);
    }
    CloseHandle(ev); if (th) CloseHandle(th); if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
}
static void t_align(ULONG id) {
    const struct qs_cls *c = qs_find(id);
    if (!qs_have()) { t_skip("export missing"); return; }
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    HANDLE sy = qs_dup(qs_child(), R_SYNC);
    HANDLE hs[] = { (HANDLE)-1, NULL, (HANDLE)(ULONG_PTR)-2, ev, sy, (HANDLE)0x12344 };
    for (ULONG off = 1; off < 8; off++) {
        for (unsigned i = 0; i < sizeof hs / sizeof *hs; i++) {
            ULONG rl;
            if (!hs[i] && i != 1) continue;
            LONG s = qs_call(hs[i], id, off, qs_good_len(c), 0, 0, &rl);
            int mis = c->align > 1 && (off % c->align) != 0;
            if (mis) {
                CHECKF(s == QS_MISALIGN, "class %lu off %lu handle #%u: 0x%lx", id, off, i, (unsigned long)s);
                CHECK(rl == QS_SENT);
            } else if (i == 0) {
                CHECKF(s == QS_OK, "class %lu off %lu: 0x%lx", id, off, (unsigned long)s);
            }
        }
        /* length 0 skips the alignment test; the length error follows */
        ULONG rl;
        LONG s = qs_call((HANDLE)-1, id, off, 0, 0, 0, &rl);
        CHECKF(s == QS_LENMISM, "class %lu off %lu len 0: 0x%lx", id, off, (unsigned long)s);
    }
    /* a misaligned buffer is refused whatever the (invalid) length, 0 excepted */
    if (c->align > 1) {
        ULONG rl;
        for (unsigned i = 0; i < sizeof g_lens / sizeof *g_lens; i++) {
            if (!g_lens[i]) continue;
            LONG s = qs_call((HANDLE)-1, id, 1, g_lens[i], 0, 0, &rl);
            CHECKF(s == QS_MISALIGN, "class %lu off 1 len %lu: 0x%lx", id, g_lens[i], (unsigned long)s);
        }
        /* at the class alignment the call works; 8-aligned classes also work at 8, 4-aligned at 4 and 8 */
        EXPECT_EQ((ULONG)qs_call((HANDLE)-1, id, c->align, qs_good_len(c), 0, 0, &rl), (ULONG)QS_OK);
        EXPECT_EQ((ULONG)qs_call((HANDLE)-1, id, 8, qs_good_len(c), 0, 0, &rl), (ULONG)QS_OK);
    }
    CloseHandle(ev); if (sy) CloseHandle(sy);
}
static void t_rl(ULONG id) {
    const struct qs_cls *c = qs_find(id);
    ULONG rl;
    if (!qs_have()) { t_skip("export missing"); return; }
    /* pseudo handle and a real full-access handle of the caller agree on status and ReturnLength */
    HANDLE self = qs_self_open(PROCESS_QUERY_INFORMATION | R_VMW);
    for (int k = 0; k < 2; k++) {
        HANDLE h = k ? self : (HANDLE)-1;
        if (!h) { t_skip("OpenProcess self failed"); continue; }
        LONG s = qs_call(h, id, 0, qs_good_len(c), 0, 0, &rl);
        EXPECT_EQ((ULONG)s, (ULONG)QS_OK);
        CHECKF(rl == (c->rl_written ? c->rl_val : QS_SENT), "class %lu rl 0x%lx", id, rl);
    }
    /* a ReturnLength preloaded with a value is overwritten (written classes) or left alone (others) */
    ULONG v = 0x12345678;
    LONG s = qs_q((HANDLE)-1, id, qs_arena(), qs_good_len(c), &v);
    EXPECT_EQ((ULONG)s, (ULONG)QS_OK);
    CHECK(v == (c->rl_written ? c->rl_val : 0x12345678));
    if (self) CloseHandle(self);
}

#define PERCLASS(X) X(12) X(18) X(22) X(29) X(33) X(34) X(36) X(39) X(45) X(46) X(59) X(61) X(74) X(87) X(92) X(94) X(96)
#define GEN(id) \
    TEST(qip_ss_len_##id)     { t_len(id); } \
    TEST(qip_ss_nobuf_##id)   { t_nobuf(id); } \
    TEST(qip_ss_rights_##id)  { t_rights(id); } \
    TEST(qip_ss_handles_##id) { t_handles(id); } \
    TEST(qip_ss_align_##id)   { t_align(id); } \
    TEST(qip_ss_rl_##id)      { t_rl(id); }
PERCLASS(GEN)
