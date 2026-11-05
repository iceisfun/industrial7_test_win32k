/* x_ntqip_qip_basic_quota_handles / t_fixed: the NtQueryInformationProcess front-door protocol of the
 * accounting classes that have a fixed set of valid lengths, one table row per class:
 *   0 basic information (0x30, or the extended 0x40 form), 1 quota limits (0x30 / 0x58), 2 io counters (0x30),
 *   3 vm counters (0x58 / 0x60 / 0x70), 4 times (0x20), 14 pooled usage and limits (0x48), 20 handle count (4 / 8),
 *   38 cycle time (0x10), 54 handle checking mode (4), 55 keep-alive counts (8), 71 invalid-handle-close mode (4).
 * Measured on Windows 10 22H2 x64 for every row:
 *   - length: only the listed lengths succeed; every other length from 0 up to 0x1000 (also 0) is
 *     STATUS_INFO_LENGTH_MISMATCH, the buffer and ReturnLength are left untouched, and the length check comes
 *     before the handle lookup (a bad length with an invalid handle is still the length error).
 *   - ReturnLength: classes 0-4, 14 and 38 report the written size; class 20 reports 4 for both its forms;
 *     classes 54, 55 and 71 leave it untouched. A NULL ReturnLength is accepted. Bytes behind the written size
 *     are never touched.
 *   - handles: pseudo handle -1 and a handle to self need no extra right; a real handle needs
 *     PROCESS_QUERY_LIMITED_INFORMATION (PROCESS_QUERY_INFORMATION also satisfies it) for every class here except
 *     54 and 55, which need PROCESS_QUERY_INFORMATION itself. Class 71 only needs the limited right.
 *     A missing right is STATUS_ACCESS_DENIED, zero / closed / never-opened values STATUS_INVALID_HANDLE, the
 *     thread pseudo handle or an event handle STATUS_OBJECT_TYPE_MISMATCH. On those errors nothing is written
 *     except the leading size qword of the extended 0x40 form of class 0.
 * Values of the counters are machine-varying and are covered by the per-class files.
 */
#include "qb.h"

struct row {
    ULONG cls;
    const char *name;
    int nvalid;
    ULONG valid[3];
    ULONG right;     /* R_LIM or R_QRY */
    int rlmode;      /* 1: RL = written len, 2: RL = 4, 0: untouched */
};
static const struct row g_rows[] = {
    {  0, "c0",  2, { 0x30, 0x40, 0 },       R_LIM, 1 },
    {  1, "c1",  2, { 0x30, 0x58, 0 },       R_LIM, 1 },
    {  2, "c2",  1, { 0x30, 0, 0 },          R_LIM, 1 },
    {  3, "c3",  3, { 0x58, 0x60, 0x70 },    R_LIM, 1 },
    {  4, "c4",  1, { 0x20, 0, 0 },          R_LIM, 1 },
    { 14, "c14", 1, { 0x48, 0, 0 },          R_LIM, 1 },
    { 20, "c20", 2, { 4, 8, 0 },             R_LIM, 2 },
    { 38, "c38", 1, { 0x10, 0, 0 },          R_LIM, 1 },
    { 54, "c54", 1, { 4, 0, 0 },             R_QRY, 0 },
    { 55, "c55", 1, { 8, 0, 0 },             R_QRY, 0 },
    { 71, "c71", 1, { 4, 0, 0 },             R_LIM, 0 },
};
#define NROWS (sizeof g_rows / sizeof *g_rows)

static int is_valid(const struct row *r, ULONG len) {
    for (int i = 0; i < r->nvalid; i++) if (r->valid[i] == len) return 1;
    return 0;
}
static ULONG expect_rl(const struct row *r, ULONG len) {
    return r->rlmode == 1 ? len : r->rlmode == 2 ? 4 : QB_SENT;
}
static int is_dead_ok(ULONG cls) { return cls <= 4 || cls == 14 || cls == 20 || cls == 38 || cls == 55; }

static void sweep(const struct row *r) {
    REQUIRE(qb_have());
    for (ULONG len = 0; len <= 0x90; len++) {
        ULONG rl;
        LONG s = qb_call(PSEUDO_SELF, r->cls, 0, len, 0, 0, &rl);
        if (is_valid(r, len)) {
            CHECKF(s == ST_OK, "cls %lu len 0x%lx status 0x%lx", r->cls, len, (ULONG)s);
            CHECKF(rl == expect_rl(r, len), "cls %lu len 0x%lx rl 0x%lx want 0x%lx", r->cls, len, rl, expect_rl(r, len));
            CHECKF(qb_untouched(len, len + 16), "cls %lu len 0x%lx wrote past the buffer", r->cls, len);
        } else {
            CHECKF(s == ST_LENMIS, "cls %lu len 0x%lx status 0x%lx", r->cls, len, (ULONG)s);
            CHECKF(rl == QB_SENT, "cls %lu len 0x%lx rl 0x%lx touched", r->cls, len, rl);
            CHECKF(qb_untouched(0, 0x100), "cls %lu len 0x%lx buffer touched on error", r->cls, len);
        }
    }
    static const ULONG big[] = { 0x98, 0xa0, 0xc0, 0x100, 0x200, 0x440, 0x800, 0x1000 };
    for (unsigned i = 0; i < sizeof big / sizeof *big; i++) {
        ULONG rl;
        LONG s = qb_call(PSEUDO_SELF, r->cls, 0, big[i], 0, 0, &rl);
        CHECKF(s == ST_LENMIS, "cls %lu len 0x%lx status 0x%lx", r->cls, big[i], (ULONG)s);
        CHECKF(rl == QB_SENT && qb_untouched(0, 0x100), "cls %lu len 0x%lx touched on error", r->cls, big[i]);
    }
    /* the length check precedes the handle lookup */
    static const ULONG bad[] = { 0, 1, 3, 0x11, 0x2f, 0x31, 0x100 };
    HANDLE hs[3] = { NULL, (HANDLE)(ULONG_PTR)0x7777770, PSEUDO_THREAD };
    for (int k = 0; k < 3; k++) for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        if (is_valid(r, bad[i])) continue;
        ULONG rl;
        LONG s = qb_call(hs[k], r->cls, 0, bad[i], 0, 0, &rl);
        CHECKF(s == ST_LENMIS, "cls %lu bad len 0x%lx with handle %d status 0x%lx", r->cls, bad[i], k, (ULONG)s);
    }
}

static void handles(const struct row *r) {
    REQUIRE(qb_have());
    HANDLE self = OpenProcess(PROCESS_ALL_ACCESS, FALSE, GetCurrentProcessId());
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    HANDLE closed;
    struct { const char *n; HANDLE h; DWORD mask; } hs[] = {
        { "pseudo", PSEUDO_SELF, 0 }, { "selfall", self, 0 },
        { "lim", qb_child_open(R_LIM), 0 }, { "qry", qb_child_open(R_QRY), 0 },
        { "qry+dup", qb_child_open(R_QRY | R_DUP), 0 }, { "all", qb_child_open(PROCESS_ALL_ACCESS), 0 },
        { "sync", qb_child_open(R_SYNC), 0 }, { "vmr", qb_child_open(R_VMR), 0 }, { "dup", qb_child_open(R_DUP), 0 },
        { "zero", NULL, 0 }, { "closed", NULL, 0 }, { "never", (HANDLE)(ULONG_PTR)0x7777770, 0 },
        { "thread", PSEUDO_THREAD, 0 }, { "event", ev, 0 },
    };
    closed = qb_closed_handle();   /* last, so that no later open reuses the slot */
    hs[10].h = closed;
    for (unsigned i = 0; i < sizeof hs / sizeof *hs; i++) {
        const char *n = hs[i].n;
        LONG want;
        if (!strcmp(n, "pseudo") || !strcmp(n, "selfall") || !strcmp(n, "all") || !strcmp(n, "qry") || !strcmp(n, "qry+dup")) want = ST_OK;
        else if (!strcmp(n, "lim")) want = r->right == R_LIM ? ST_OK : ST_ACCESS;
        else if (!strcmp(n, "sync") || !strcmp(n, "vmr") || !strcmp(n, "dup")) want = ST_ACCESS;
        else if (!strcmp(n, "thread") || !strcmp(n, "event")) want = ST_TYPEMIS;
        else want = ST_BADHANDLE;
        if (!hs[i].h && strcmp(n, "zero")) { t_note("%s: could not open handle %s", r->name, n); continue; }
        for (int v = 0; v < r->nvalid; v++) {
            ULONG len = r->valid[v], rl;
            LONG s = qb_call(hs[i].h, r->cls, 0, len, 0, 0, &rl);
            CHECKF(s == want, "cls %lu handle %s len 0x%lx status 0x%lx want 0x%lx", r->cls, n, len, (ULONG)s, (ULONG)want);
            if (want == ST_OK) {
                CHECKF(rl == expect_rl(r, len), "cls %lu handle %s rl 0x%lx", r->cls, n, rl);
            } else {
                if (r->cls == 0 && len == 0x40) {   /* the extended form stores its size field before the handle is looked at */
                    CHECKF(rl == QB_SENT && qb_u64(0) == 0x40 && qb_untouched(8, 0x100), "cls 0 ext handle %s touched on error", n);
                } else {
                    CHECKF(rl == QB_SENT && qb_untouched(0, 0x100), "cls %lu handle %s touched on error", r->cls, n);
                }
            }
        }
    }
    for (unsigned i = 0; i < sizeof hs / sizeof *hs; i++)
        if (hs[i].h && hs[i].h != PSEUDO_SELF && hs[i].h != PSEUDO_THREAD && hs[i].h != closed && hs[i].h != (HANDLE)(ULONG_PTR)0x7777770) CloseHandle(hs[i].h);
}

static void nullargs(const struct row *r) {
    REQUIRE(qb_have());
    ULONG rl;
    /* NULL ReturnLength with a valid length succeeds and writes the data */
    for (int v = 0; v < r->nvalid; v++) {
        LONG s = qb_call(PSEUDO_SELF, r->cls, 0, r->valid[v], 0, 1, &rl);
        CHECKF(s == ST_OK, "cls %lu NULL rl len 0x%lx status 0x%lx", r->cls, r->valid[v], (ULONG)s);
    }
    /* NULL buffer with length 0 (and with or without ReturnLength) is the plain length error */
    LONG s = qb_call(PSEUDO_SELF, r->cls, 0, 0, 1, 0, &rl);
    CHECKF(s == ST_LENMIS && rl == QB_SENT, "cls %lu NULL buffer len 0 status 0x%lx rl 0x%lx", r->cls, (ULONG)s, rl);
    s = qb_call(PSEUDO_SELF, r->cls, 0, 0, 1, 1, &rl);
    CHECKF(s == ST_LENMIS, "cls %lu NULL buffer NULL rl len 0 status 0x%lx", r->cls, (ULONG)s);
    /* a real, writable buffer of length 0 */
    s = qb_call(PSEUDO_SELF, r->cls, 0, 0, 0, 1, &rl);
    CHECKF(s == ST_LENMIS, "cls %lu len 0 NULL rl status 0x%lx", r->cls, (ULONG)s);
    /* a real handle to self and the pseudo handle answer the same status for the valid length */
    HANDLE self = OpenProcess(PROCESS_ALL_ACCESS, FALSE, GetCurrentProcessId());
    if (self) {
        LONG a = qb_call(PSEUDO_SELF, r->cls, 0, r->valid[0], 0, 0, &rl);
        LONG b = qb_call(self, r->cls, 0, r->valid[0], 0, 0, &rl);
        CHECKF(a == b && a == ST_OK, "cls %lu pseudo 0x%lx real 0x%lx", r->cls, (ULONG)a, (ULONG)b);
        CloseHandle(self);
    }
}

static void dead(const struct row *r) {
    HANDLE d = qb_dead();
    REQUIRE(d);
    for (int v = 0; v < r->nvalid; v++) {
        ULONG rl;
        LONG s = qb_call(d, r->cls, 0, r->valid[v], 0, 0, &rl);
        if (is_dead_ok(r->cls)) {
            CHECKF(s == ST_OK, "cls %lu exited process len 0x%lx status 0x%lx", r->cls, r->valid[v], (ULONG)s);
            CHECKF(rl == expect_rl(r, r->valid[v]), "cls %lu exited process rl 0x%lx", r->cls, rl);
        } else {
            CHECKF(s == ST_TERMING, "cls %lu exited process len 0x%lx status 0x%lx", r->cls, r->valid[v], (ULONG)s);
            CHECKF(rl == QB_SENT && qb_untouched(0, 0x100), "cls %lu exited process touched on error", r->cls);
        }
    }
}

#define ROWTESTS(IDX, N) \
    TEST(sweep_##N) { sweep(&g_rows[IDX]); } \
    TEST(handles_##N) { handles(&g_rows[IDX]); } \
    TEST(nullargs_##N) { nullargs(&g_rows[IDX]); } \
    TEST(exited_##N) { dead(&g_rows[IDX]); }
ROWTESTS(0, c0)
ROWTESTS(1, c1)
ROWTESTS(2, c2)
ROWTESTS(3, c3)
ROWTESTS(4, c4)
ROWTESTS(5, c14)
ROWTESTS(6, c20)
ROWTESTS(7, c38)
ROWTESTS(8, c54)
ROWTESTS(9, c55)
ROWTESTS(10, c71)
