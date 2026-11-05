/* x_ntqip_qip_basic_quota_handles / t_more: buffer placement, NULL buffer ordering, and information about another
 * process that the test itself changes (priority class, affinity, working set limits, duplicated handles, remote
 * commit, exit code, the settable handle checking and invalid-handle-close modes).
 * Measured on Windows 10 22H2 x64:
 *   - the output buffer must be 4 byte aligned for every class here: an address that is not (offset 1, 2, 3, 5, 6, 7)
 *     is STATUS_DATATYPE_MISALIGNMENT before the length, the handle or the access right is looked at, and nothing is
 *     written. Offsets 4 and 8 are fine. The ReturnLength pointer has no alignment requirement.
 *   - a NULL buffer with a non-zero length: classes 0-4, 14, 20, 38, 54, 55 and 71 look up the handle first (invalid handle is
 *     STATUS_INVALID_HANDLE, a bad length STATUS_INFO_LENGTH_MISMATCH, a good length with a good handle
 *     STATUS_ACCESS_VIOLATION); class 51 checks the access right (limited handle: STATUS_ACCESS_DENIED), class 58 touches the buffer
 *     first and gives STATUS_ACCESS_VIOLATION whatever the handle.
 *   - base priority follows SetPriorityClass: idle 4, below normal 6, normal 8, above normal 10, high 13, real time 24.
 *   - class 20, 58 and 51 of a process show a handle that was duplicated into it, and DUPLICATE_CLOSE_SOURCE removes it.
 *   - class 54 can be turned on for another process (needs PROCESS_SET_INFORMATION) and never off again, other values are
 *     STATUS_INVALID_PARAMETER, wrong lengths STATUS_INFO_LENGTH_MISMATCH; class 71 turns on for 1 and off for 0, any other value is accepted and changes nothing; class 55
 *     cannot be set (STATUS_INVALID_INFO_CLASS).
 */
#include "qb.h"

static LONG callx(HANDLE h, ULONG cls, ULONG off, ULONG len, int nobuf, ULONG rloff, int norl) {
    unsigned char *a = qb_arena();
    memset(a, QB_FILL, 0x3000);
    ULONG *rlp = norl ? NULL : (ULONG *)(a + 0x2000 + rloff);
    if (rlp) memcpy(rlp, &(ULONG){ QB_SENT }, 4);
    return qb_q(h, cls, nobuf ? NULL : a + off, len, rlp);
}
static ULONG rl_at(ULONG rloff) { ULONG v; memcpy(&v, qb_arena() + 0x2000 + rloff, 4); return v; }

struct cl { ULONG cls; ULONG len; int rlmode; };   /* rlmode 1: RL = len, 2: RL = 4, 0: untouched */
static const struct cl g_cl[] = {
    { 0, 0x30, 1 }, { 0, 0x40, 1 }, { 1, 0x30, 1 }, { 2, 0x30, 1 }, { 3, 0x58, 1 }, { 4, 0x20, 1 }, { 14, 0x48, 1 },
    { 20, 4, 2 }, { 38, 0x10, 1 }, { 54, 4, 0 }, { 55, 8, 0 }, { 71, 4, 0 },
};
#define NCL (sizeof g_cl / sizeof *g_cl)

TEST(misaligned_buffer_is_datatype_misalignment) {
    REQUIRE(qb_have());
    static const ULONG offs[] = { 1, 2, 3, 5, 6, 7 };
    for (unsigned i = 0; i < NCL; i++) for (unsigned k = 0; k < 6; k++) {
        LONG s = callx(PSEUDO_SELF, g_cl[i].cls, offs[k], g_cl[i].len, 0, 0, 0);
        CHECKF(s == (LONG)0x80000002, "cls %lu len 0x%lx offset %lu status 0x%lx", g_cl[i].cls, g_cl[i].len, offs[k], (ULONG)s);
        CHECKF(rl_at(0) == QB_SENT && qb_untouched(0, 0x2000), "cls %lu offset %lu wrote", g_cl[i].cls, offs[k]);
    }
    /* the big buffer classes */
    static const ULONG big[] = { 51, 58 };
    for (int i = 0; i < 2; i++) for (unsigned k = 0; k < 6; k++) {
        LONG s = callx(PSEUDO_SELF, big[i], offs[k], 0x400, 0, 0, 0);
        CHECKF(s == (LONG)0x80000002, "cls %lu offset %lu status 0x%lx", big[i], offs[k], (ULONG)s);
    }
}
TEST(misalignment_precedes_length_handle_and_right) {
    REQUIRE(qb_have());
    HANDLE lim = qb_child_open(R_LIM);
    HANDLE hs[3] = { PSEUDO_SELF, NULL, lim };
    for (unsigned i = 0; i < NCL; i++) for (int k = 0; k < 3; k++) {
        if (k == 2 && !lim) continue;
        LONG a = callx(hs[k], g_cl[i].cls, 1, g_cl[i].len, 0, 0, 0);
        LONG b = callx(hs[k], g_cl[i].cls, 3, g_cl[i].len + 1, 0, 0, 0);   /* also a wrong length */
        CHECKF(a == (LONG)0x80000002 && b == (LONG)0x80000002, "cls %lu handle %d: 0x%lx 0x%lx", g_cl[i].cls, k, (ULONG)a, (ULONG)b);
    }
    static const ULONG big[] = { 51, 58 };
    for (int i = 0; i < 2; i++) for (int k = 0; k < 3; k++) {
        if (k == 2 && !lim) continue;
        LONG a = callx(hs[k], big[i], 1, 0x400, 0, 0, 0);
        LONG b = callx(hs[k], big[i], 2, 3, 0, 0, 0);
        CHECKF(a == (LONG)0x80000002 && b == (LONG)0x80000002, "cls %lu handle %d: 0x%lx 0x%lx", big[i], k, (ULONG)a, (ULONG)b);
    }
    if (lim) CloseHandle(lim);
}
TEST(aligned_offsets_four_and_eight_work) {
    REQUIRE(qb_have());
    static const ULONG offs[] = { 4, 8, 0x10, 0x14 };
    for (unsigned i = 0; i < NCL; i++) for (unsigned k = 0; k < 4; k++) {
        LONG s = callx(PSEUDO_SELF, g_cl[i].cls, offs[k], g_cl[i].len, 0, 0, 0);
        CHECKF(s == 0, "cls %lu offset %lu status 0x%lx", g_cl[i].cls, offs[k], (ULONG)s);
        CHECKF(qb_untouched(0, offs[k]), "cls %lu offset %lu wrote before the buffer", g_cl[i].cls, offs[k]);
        CHECKF(qb_untouched(offs[k] + g_cl[i].len, 0x2000), "cls %lu offset %lu wrote behind the buffer", g_cl[i].cls, offs[k]);
    }
    /* snapshot and table at a 4 byte offset: the status is the normal one (the snapshot needs more room than 0x100) */
    EXPECT_EQ(callx(PSEUDO_SELF, 58, 4, 0x400, 0, 0, 0), 0);
    EXPECT_EQ(callx(PSEUDO_SELF, 51, 4, 0x10, 0, 0, 0), ST_LENMIS);
}
TEST(unaligned_return_length_pointer_is_accepted) {
    REQUIRE(qb_have());
    for (unsigned i = 0; i < NCL; i++) for (ULONG ro = 1; ro <= 3; ro++) {
        LONG s = callx(PSEUDO_SELF, g_cl[i].cls, 0, g_cl[i].len, 0, ro, 0);
        CHECKF(s == 0, "cls %lu rl offset %lu status 0x%lx", g_cl[i].cls, ro, (ULONG)s);
        ULONG want = g_cl[i].rlmode == 1 ? g_cl[i].len : g_cl[i].rlmode == 2 ? 4 : QB_SENT;
        CHECKF(rl_at(ro) == want || 0, "cls %lu rl offset %lu value 0x%lx want 0x%lx", g_cl[i].cls, ro, rl_at(ro), want);
        /* a wrong length with the same pointer is still the plain length error */
        s = callx(PSEUDO_SELF, g_cl[i].cls, 0, g_cl[i].len + 1, 0, ro, 0);
        CHECKF(s == ST_LENMIS, "cls %lu bad length, rl offset %lu status 0x%lx", g_cl[i].cls, ro, (ULONG)s);
    }
}

TEST(null_buffer_with_length_ordering) {
    REQUIRE(qb_have());
    HANDLE lim = qb_child_open(R_LIM);
    REQUIRE(lim);
    static const ULONG plain[] = { 0, 3, 4, 20, 38, 71 };   /* classes verified one by one */
    static const ULONG plen[]  = { 0x30, 0x58, 0x20, 4, 0x10, 4 };
    for (unsigned i = 0; i < sizeof plain / sizeof *plain; i++) {
        ULONG c = plain[i], l = plen[i];
        LONG a = callx(PSEUDO_SELF, c, 0, l, 1, 0, 0);
        LONG b = callx(NULL, c, 0, l, 1, 0, 0);
        LONG d = callx(PSEUDO_SELF, c, 0, l + 1, 1, 0, 0);
        LONG e = callx(lim, c, 0, l, 1, 0, 0);
        CHECKF(a == (LONG)0xC0000005, "cls %lu pseudo handle NULL buffer 0x%lx", c, (ULONG)a);
        CHECKF(b == ST_BADHANDLE, "cls %lu zero handle NULL buffer 0x%lx", c, (ULONG)b);
        CHECKF(d == ST_LENMIS, "cls %lu bad length NULL buffer 0x%lx", c, (ULONG)d);
        CHECKF(e == (LONG)0xC0000005, "cls %lu limited handle NULL buffer 0x%lx", c, (ULONG)e);
    }
    /* class 54 needs the query right, so the limited handle is refused before the buffer is touched */
    EXPECT_EQ(callx(PSEUDO_SELF, 54, 0, 4, 1, 0, 0), (LONG)0xC0000005);
    EXPECT_EQ(callx(NULL, 54, 0, 4, 1, 0, 0), ST_BADHANDLE);
    EXPECT_EQ(callx(PSEUDO_SELF, 54, 0, 5, 1, 0, 0), ST_LENMIS);
    EXPECT_EQ(callx(lim, 54, 0, 4, 1, 0, 0), ST_ACCESS);
    /* class 51: handle and right first, the snapshot size is only known after the lookup */
    EXPECT_EQ(callx(NULL, 51, 0, 0x11, 1, 0, 0), ST_BADHANDLE);
    EXPECT_EQ(callx(lim, 51, 0, 0x11, 1, 0, 0), ST_ACCESS);
    EXPECT_EQ(callx(PSEUDO_SELF, 51, 0, 0x11, 1, 0, 0), ST_LENMIS);      /* too small for the snapshot */
    EXPECT_EQ(callx(PSEUDO_SELF, 51, 0, 0x6000, 1, 0, 0), (LONG)0xC0000005);   /* room for it, nowhere to put it */
    EXPECT_EQ(callx(PSEUDO_SELF, 51, 0, 4, 1, 0, 0), ST_LENMIS);
    /* class 58 writes first: whatever the handle, a NULL buffer is an access violation */
    EXPECT_EQ(callx(PSEUDO_SELF, 58, 0, 0x40, 1, 0, 0), (LONG)0xC0000005);
    EXPECT_EQ(callx(NULL, 58, 0, 0x40, 1, 0, 0), (LONG)0xC0000005);
    EXPECT_EQ(callx(lim, 58, 0, 0x40, 1, 0, 0), (LONG)0xC0000005);
    EXPECT_EQ(callx(PSEUDO_SELF, 58, 0, 3, 1, 0, 0), ST_LENMIS);
    CloseHandle(lim);
}

/* ---- base priority and affinity of another process ---------------------- */
TEST(c0_base_priority_follows_priority_class) {
    REQUIRE(qb_have());
    DWORD pid;
    HANDLE c = qb_fresh_child(&pid);
    REQUIRE(c);
    static const struct { DWORD pc; LONG base; } t[] = {
        { IDLE_PRIORITY_CLASS, 4 }, { BELOW_NORMAL_PRIORITY_CLASS, 6 }, { NORMAL_PRIORITY_CLASS, 8 },
        { ABOVE_NORMAL_PRIORITY_CLASS, 10 }, { HIGH_PRIORITY_CLASS, 13 }, { NORMAL_PRIORITY_CLASS, 8 },
    };
    for (unsigned i = 0; i < sizeof t / sizeof *t; i++) {
        REQUIRE(SetPriorityClass(c, t[i].pc));
        ULONG rl;
        EXPECT_EQ(qb_call(c, 0, 0, 0x30, 0, 0, &rl), 0);
        CHECKF((LONG)qb_u32(0x18) == t[i].base, "priority class 0x%lx base %ld want %ld", t[i].pc, (long)qb_u32(0x18), (long)t[i].base);
        EXPECT_EQ(GetPriorityClass(c), t[i].pc);
        /* the extended form agrees */
        EXPECT_EQ(qb_call(c, 0, 0, 0x40, 0, 0, &rl), 0);
        CHECK((LONG)qb_u32(0x20) == t[i].base);
    }
    /* real time: the request may be lowered to high without the privilege; the base priority follows the class that took effect */
    if (SetPriorityClass(c, REALTIME_PRIORITY_CLASS)) {
        DWORD got = GetPriorityClass(c);
        ULONG rl;
        EXPECT_EQ(qb_call(c, 0, 0, 0x30, 0, 0, &rl), 0);
        if (got == REALTIME_PRIORITY_CLASS) EXPECT_EQ(qb_u32(0x18), 24);
        else if (got == HIGH_PRIORITY_CLASS) EXPECT_EQ(qb_u32(0x18), 13);
        else t_note("real time request left class 0x%lx", got);
    }
    TerminateProcess(c, 0);
    CloseHandle(c);
}
TEST(c0_affinity_follows_set_process_affinity_mask) {
    REQUIRE(qb_have());
    DWORD pid;
    HANDLE c = qb_fresh_child(&pid);
    REQUIRE(c);
    DWORD_PTR pm, sm;
    REQUIRE(GetProcessAffinityMask(c, &pm, &sm));
    ULONG rl;
    EXPECT_EQ(qb_call(c, 0, 0, 0x30, 0, 0, &rl), 0);
    EXPECT_EQ(qb_u64(0x10), pm);
    if (!SetProcessAffinityMask(c, 1)) { t_skip("SetProcessAffinityMask refused, error %lu", GetLastError()); TerminateProcess(c, 0); CloseHandle(c); return; }
    EXPECT_EQ(qb_call(c, 0, 0, 0x30, 0, 0, &rl), 0);
    EXPECT_EQ(qb_u64(0x10), 1);
    EXPECT_EQ(qb_call(c, 0, 0, 0x40, 0, 0, &rl), 0);
    EXPECT_EQ(qb_u64(0x18), 1);
    CHECK(SetProcessAffinityMask(c, pm));
    EXPECT_EQ(qb_call(c, 0, 0, 0x30, 0, 0, &rl), 0);
    EXPECT_EQ(qb_u64(0x10), pm);
    TerminateProcess(c, 0);
    CloseHandle(c);
}
TEST(c0_exit_status_is_the_exit_code) {
    REQUIRE(qb_have());
    static const DWORD codes[] = { 0x1234, 0xC0000142, 1, 0 };
    for (int i = 0; i < 4; i++) {
        DWORD pid;
        HANDLE c = qb_fresh_child(&pid);
        REQUIRE(c);
        ULONG rl;
        EXPECT_EQ(qb_call(c, 0, 0, 0x30, 0, 0, &rl), 0);
        EXPECT_EQ(qb_u32(0), 0x103);
        TerminateProcess(c, codes[i]);
        WaitForSingleObject(c, 10000);
        EXPECT_EQ(qb_call(c, 0, 0, 0x30, 0, 0, &rl), 0);
        CHECKF(qb_u32(0) == codes[i], "exit code 0x%lx read 0x%lx", codes[i], qb_u32(0));
        DWORD gx = 0xdead; GetExitCodeProcess(c, &gx);
        EXPECT_EQ(gx, codes[i]);
        EXPECT_EQ(qb_call(c, 0, 0, 0x40, 0, 0, &rl), 0);
        EXPECT_EQ(qb_u32(8), codes[i]);
        EXPECT_EQ(qb_u32(0x38), 4);
        CloseHandle(c);
    }
}

/* ---- class 1 after SetProcessWorkingSetSize ------------------------------ */
TEST(c1_follows_set_process_working_set_size) {
    REQUIRE(qb_have());
    DWORD pid;
    HANDLE c = qb_fresh_child(&pid);
    REQUIRE(c);
    ULONG rl;
    if (!SetProcessWorkingSetSize(c, 0x100000, 0x400000)) { t_skip("SetProcessWorkingSetSize refused, error %lu", GetLastError()); TerminateProcess(c, 0); CloseHandle(c); return; }
    EXPECT_EQ(qb_call(c, 1, 0, 0x30, 0, 0, &rl), 0);
    EXPECT_EQ(qb_u64(0x10), 0x100000); EXPECT_EQ(qb_u64(0x18), 0x400000);
    CHECK(SetProcessWorkingSetSize(c, 0x103000, 0x405000));
    EXPECT_EQ(qb_call(c, 1, 0, 0x58, 0, 0, &rl), 0);
    EXPECT_EQ(qb_u64(0x10), 0x103000); EXPECT_EQ(qb_u64(0x18), 0x405000);
    EXPECT_EQ(qb_u64(0x20), ~0ULL);
    /* (-1, -1) trims the working set and leaves the limits alone */
    CHECK(SetProcessWorkingSetSize(c, (SIZE_T)-1, (SIZE_T)-1));
    EXPECT_EQ(qb_call(c, 1, 0, 0x30, 0, 0, &rl), 0);
    EXPECT_EQ(qb_u64(0x10), 0x103000); EXPECT_EQ(qb_u64(0x18), 0x405000);
    /* the limits of the other process do not move ours */
    EXPECT_EQ(qb_call(PSEUDO_SELF, 1, 0, 0x30, 0, 0, &rl), 0);
    CHECK(qb_u64(0x10) != 0x103000 || qb_u64(0x18) != 0x405000);
    TerminateProcess(c, 0);
    CloseHandle(c);
}

/* ---- handles duplicated into another process ------------------------------ */
TEST(c20_c58_c51_show_a_handle_duplicated_into_the_child) {
    REQUIRE(qb_have());
    DWORD pid;
    HANDLE c = qb_fresh_child(&pid);
    REQUIRE(c);
    HANDLE full = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    REQUIRE(full);
    ULONG rl;
    EXPECT_EQ(qb_call(c, 20, 0, 4, 0, 0, &rl), 0);
    ULONG n0 = qb_u32(0);
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL), t = NULL;
    REQUIRE(DuplicateHandle(GetCurrentProcess(), ev, full, &t, EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, 0));
    EXPECT_EQ(qb_call(c, 20, 0, 8, 0, 0, &rl), 0);
    EXPECT_EQ(qb_u32(0), n0 + 1);
    EXPECT_EQ(qb_u32(4), n0 + 1);
    unsigned char *b = qb_big();
    memset(b, QB_FILL, 0x4000);
    EXPECT_EQ(qb_q(c, 58, b, 0x2000, &rl), 0);
    EXPECT_EQ(rl, 4 * (n0 + 1));
    int found = 0;
    for (ULONG i = 0; i < rl / 4; i++) { ULONG v; memcpy(&v, b + 4 * i, 4); if (v == (ULONG)(ULONG_PTR)t) found = 1; }
    CHECK(found);
    EXPECT_EQ(qb_q(c, 51, b, 0x6000, &rl), 0);
    ULONGLONG n; memcpy(&n, b, 8);
    EXPECT_EQ(n, n0 + 1);
    int f2 = 0;
    for (ULONGLONG i = 0; i < n; i++) {
        const unsigned char *e = b + 0x10 + i * 0x28;
        ULONGLONG hv; memcpy(&hv, e, 8);
        if (hv != (ULONGLONG)(ULONG_PTR)t) continue;
        ULONG acc; ULONGLONG hc; memcpy(&acc, e + 0x18, 4); memcpy(&hc, e + 8, 8);
        f2 = 1;
        EXPECT_EQ(acc, EVENT_MODIFY_STATE | SYNCHRONIZE);
        EXPECT_EQ(hc, 2);                                   /* ours and the duplicate */
    }
    CHECK(f2);
    /* close it again inside the child */
    CHECK(DuplicateHandle(full, t, NULL, NULL, 0, FALSE, DUPLICATE_CLOSE_SOURCE));
    EXPECT_EQ(qb_call(c, 20, 0, 4, 0, 0, &rl), 0);
    EXPECT_EQ(qb_u32(0), n0);
    EXPECT_EQ(qb_q(c, 58, b, 0x2000, &rl), 0);
    EXPECT_EQ(rl, 4 * n0);
    CloseHandle(ev); CloseHandle(full);
    TerminateProcess(c, 0);
    CloseHandle(c);
}

/* ---- class 3 of another process that commits and frees memory ------------- */
TEST(c3_child_remote_commit_growth) {
    REQUIRE(qb_have());
    DWORD pid;
    HANDLE c = qb_fresh_child(&pid);
    REQUIRE(c);
    HANDLE full = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    REQUIRE(full);
    ULONG rl;
    unsigned char a[0x70], b[0x70], d[0x70];
    EXPECT_EQ(qb_call(c, 3, 0, 0x70, 0, 0, &rl), 0); memcpy(a, qb_arena(), 0x70);
    SIZE_T sz = 0x400000;
    char *p = VirtualAllocEx(full, NULL, sz, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    REQUIRE(p);
    EXPECT_EQ(qb_call(c, 3, 0, 0x70, 0, 0, &rl), 0); memcpy(b, qb_arena(), 0x70);
    CHECK(VirtualFreeEx(full, p, 0, MEM_RELEASE));
    EXPECT_EQ(qb_call(c, 3, 0, 0x70, 0, 0, &rl), 0); memcpy(d, qb_arena(), 0x70);
    ULONGLONG *A = (ULONGLONG *)a, *B = (ULONGLONG *)b, *D = (ULONGLONG *)d;
    CHECKF(B[1] >= A[1] + sz, "VirtualSize %llx -> %llx", A[1], B[1]);
    CHECKF(B[9] >= A[9] + sz, "PagefileUsage %llx -> %llx", A[9], B[9]);
    CHECKF(B[11] >= A[11] + sz, "PrivateUsage %llx -> %llx", A[11], B[11]);
    CHECK(B[11] == B[9]);
    CHECKF(D[9] + sz <= B[9], "PagefileUsage after the free %llx, before %llx", D[9], B[9]);
    CHECKF(D[1] + sz <= B[1], "VirtualSize after the free %llx, before %llx", D[1], B[1]);
    CHECK(D[10] >= B[9]);                        /* PeakPagefileUsage keeps the high point */
    CHECK(D[0] >= B[1]);                         /* PeakVirtualSize too */
    CloseHandle(full);
    TerminateProcess(c, 0);
    CloseHandle(c);
}

TEST(c38_child_cycle_counters) {
    REQUIRE(qb_have());
    HANDLE c = qb_child();
    REQUIRE(c);
    ULONG rl;
    EXPECT_EQ(qb_call(c, 38, 0, 0x10, 0, 0, &rl), 0);
    CHECK(qb_u64(0) != 0);                       /* the process has run: its image was started */
    ULONGLONG a0 = qb_u64(0);
    EXPECT_EQ(qb_call(c, 38, 0, 0x10, 0, 0, &rl), 0);
    CHECK(qb_u64(0) >= a0);
}

/* ---- the settable classes 54, 55 and 71 on a throwaway child --------------- */
TEST(c54_set_on_child_is_one_way) {
    REQUIRE(qb_have());
    DWORD pid;
    HANDLE c = qb_fresh_child(&pid);
    REQUIRE(c);
    HANDLE sit = OpenProcess(PROCESS_SET_INFORMATION | R_QRY, FALSE, pid);
    REQUIRE(sit);
    ULONG rl, v = 1;
    /* wrong lengths and values change nothing */
    static const ULONG lens[] = { 0, 3, 5, 8 };
    for (int k = 0; k < 4; k++) {
        v = 1;
        LONG s = qb_set(sit, 54, &v, lens[k]);
        CHECKF(s == ST_LENMIS, "set 54 len %lu status 0x%lx", lens[k], (ULONG)s);
    }
    v = 2; LONG s = qb_set(sit, 54, &v, 4);
    CHECKF(s == (LONG)0xC000000D, "set 54 value 2 status 0x%lx", (ULONG)s);
    EXPECT_EQ(qb_call(sit, 54, 0, 4, 0, 0, &rl), 0);
    EXPECT_EQ(qb_u32(0), 0);
    /* a limited handle may not set */
    HANDLE lim = OpenProcess(R_LIM, FALSE, pid);
    if (lim) { v = 1; EXPECT_EQ(qb_set(lim, 54, &v, 4), ST_ACCESS); CloseHandle(lim); }
    EXPECT_EQ(qb_call(sit, 54, 0, 4, 0, 0, &rl), 0);
    EXPECT_EQ(qb_u32(0), 0);
    /* on, and it stays on */
    v = 1; EXPECT_EQ(qb_set(sit, 54, &v, 4), 0);
    EXPECT_EQ(qb_call(sit, 54, 0, 4, 0, 0, &rl), 0);
    EXPECT_EQ(qb_u32(0), 1);
    v = 0; EXPECT_EQ(qb_set(sit, 54, &v, 4), 0);
    EXPECT_EQ(qb_call(sit, 54, 0, 4, 0, 0, &rl), 0);
    EXPECT_EQ(qb_u32(0), 1);
    /* class 55 has no setter */
    ULONGLONG q = 5;
    s = qb_set(sit, 55, &q, 8);
    CHECKF(s == ST_BADCLASS, "set 55 status 0x%lx", (ULONG)s);
    EXPECT_EQ(qb_call(sit, 55, 0, 8, 0, 0, &rl), 0);
    EXPECT_EQ(qb_u64(0), 0);
    CloseHandle(sit);
    TerminateProcess(c, 0);
    CloseHandle(c);
}
TEST(c71_set_on_child_one_on_zero_off_others_ignored) {
    REQUIRE(qb_have());
    DWORD pid;
    HANDLE c = qb_fresh_child(&pid);
    REQUIRE(c);
    HANDLE sit = OpenProcess(PROCESS_SET_INFORMATION | R_QRY, FALSE, pid);
    REQUIRE(sit);
    ULONG rl, v;
    HANDLE lim = OpenProcess(R_LIM, FALSE, pid);
    if (lim) { v = 1; EXPECT_EQ(qb_set(lim, 71, &v, 4), ST_ACCESS); CloseHandle(lim); }
    static const struct { ULONG set; ULONG after; } t[] = { { 1, 1 }, { 0, 0 }, { 2, 0 }, { 1, 1 }, { 2, 1 }, { 0xffffffff, 1 }, { 0, 0 }, { 0xffffffff, 0 }, { 1, 1 } };
    for (unsigned i = 0; i < sizeof t / sizeof *t; i++) {
        v = t[i].set;
        LONG s = qb_set(sit, 71, &v, 4);
        CHECKF(s == 0, "set 71 value 0x%lx status 0x%lx", t[i].set, (ULONG)s);
        EXPECT_EQ(qb_call(sit, 71, 0, 4, 0, 0, &rl), 0);
        CHECKF(qb_u32(0) == t[i].after, "set 71 value 0x%lx read %lu want %lu", t[i].set, qb_u32(0), t[i].after);
    }
    CloseHandle(sit);
    TerminateProcess(c, 0);
    CloseHandle(c);
}
