/* x_ntqip_qip_basic_quota_handles / t_values: layouts and invariants of the accounting classes
 *   0 basic information, 1 quota limits, 2 io counters, 3 vm counters, 4 times, 14 pooled usage and limits,
 *   20 handle count, 38 cycle time, 54 / 55 handle checking and keep-alive counts, 71 invalid-handle-close mode.
 * Absolute values (addresses, sizes, times, counters) differ between machines and runs; the tests check
 * the field layout, equalities with the documented Win32 front doors (GetProcessTimes, GetProcessHandleCount,
 * QueryProcessCycleTime, GetProcessMemoryInfo, ...), monotonicity, peak >= current relations and the
 * behaviour of a process that was started, is running, and one that has exited.
 *   - class 0: ExitStatus (STATUS_PENDING 0x103 while running, the exit code afterwards), PEB pointer, affinity,
 *     base priority 8 for a normal priority class, process id and parent id; the extended 0x40 form starts with
 *     its size, holds the same body at +8 and a flags dword at +0x38; padding dwords are not written.
 *   - class 1: both forms start with the same six values; minimum working set <= maximum; no pagefile limit
 *     and no time limit (all ones).
 *   - class 3: PeakVirtualSize >= VirtualSize, PeakWorkingSet >= WorkingSet, peak >= current for pool and pagefile
 *     usage; the 0x60 form adds PrivateUsage which equals PagefileUsage; commit growth shows in PagefileUsage,
 *     PageFaultCount and VirtualSize and the peaks survive a free.
 */
#include "qb.h"
#include <psapi.h>

static ULONGLONG ft64(FILETIME f) { return ((ULONGLONG)f.dwHighDateTime << 32) | f.dwLowDateTime; }
static ULONGLONG rd64(const unsigned char *p, ULONG off) { ULONGLONG v; memcpy(&v, p + off, 8); return v; }
static ULONG rd32(const unsigned char *p, ULONG off) { ULONG v; memcpy(&v, p + off, 4); return v; }

/* ---- class 0 ----------------------------------------------------------- */
static void c0_check_body(const unsigned char *b, ULONG base, DWORD pid, DWORD ppid, ULONGLONG peb, LONG exitst, int normal) {
    CHECKF(rd32(b, base + 0) == (ULONG)exitst, "ExitStatus 0x%lx want 0x%lx", rd32(b, base), (ULONG)exitst);
    CHECKF(rd32(b, base + 4) == 0xcccccccc, "padding after ExitStatus written");
    if (peb) CHECKF(rd64(b, base + 8) == peb, "PebBaseAddress 0x%llx want 0x%llx", rd64(b, base + 8), peb);
    CHECK(rd64(b, base + 8) != 0);
    CHECK(rd64(b, base + 0x10) != 0);                                   /* affinity mask */
    if (normal) CHECKF(rd32(b, base + 0x18) == 8, "BasePriority %ld", (long)rd32(b, base + 0x18));
    CHECKF(rd32(b, base + 0x1c) == 0xcccccccc, "padding after BasePriority written");
    CHECKF(rd64(b, base + 0x20) == pid, "UniqueProcessId 0x%llx want 0x%lx", rd64(b, base + 0x20), pid);
    if (ppid) CHECKF(rd64(b, base + 0x28) == ppid, "InheritedFromUniqueProcessId 0x%llx want 0x%lx", rd64(b, base + 0x28), ppid);
    else CHECK(rd64(b, base + 0x28) != 0);
}
TEST(c0_self_basic_form) {
    REQUIRE(qb_have());
    ULONG rl;
    LONG s = qb_call(PSEUDO_SELF, 0, 0, 0x30, 0, 0, &rl);
    EXPECT_EQ(s, 0); EXPECT_EQ(rl, 0x30);
    ULONGLONG peb = (ULONGLONG)(ULONG_PTR)NtCurrentTeb()->ProcessEnvironmentBlock;
    int normal = GetPriorityClass(GetCurrentProcess()) == NORMAL_PRIORITY_CLASS;
    unsigned char *a = qb_arena();
    c0_check_body(a, 0, GetCurrentProcessId(), 0, peb, 0x103, normal);
    /* the parent id is a real, different process id */
    ULONGLONG ppid = rd64(a, 0x28);
    CHECK(ppid != 0 && ppid != GetCurrentProcessId());
    DWORD_PTR pm, sm;
    if (GetProcessAffinityMask(GetCurrentProcess(), &pm, &sm)) EXPECT_EQ(rd64(a, 0x10), pm);
}
TEST(c0_self_extended_form) {
    REQUIRE(qb_have());
    ULONG rl;
    LONG s = qb_call(PSEUDO_SELF, 0, 0, 0x40, 0, 0, &rl);
    EXPECT_EQ(s, 0); EXPECT_EQ(rl, 0x40);
    unsigned char *a = qb_arena();
    EXPECT_EQ(rd64(a, 0), 0x40);                               /* leading size field */
    ULONGLONG peb = (ULONGLONG)(ULONG_PTR)NtCurrentTeb()->ProcessEnvironmentBlock;
    c0_check_body(a, 8, GetCurrentProcessId(), 0, peb, 0x103, GetPriorityClass(GetCurrentProcess()) == NORMAL_PRIORITY_CLASS);
    EXPECT_EQ(rd32(a, 0x38), 0);                               /* no protected/wow64/deleting/frozen/... flag on an ordinary x64 process */
    EXPECT_EQ(rd32(a, 0x3c), 0xcccccccc);                      /* the dword after the flags is not written */
    EXPECT_EQ(qb_untouched(0x40, 0x50), 1);
    /* the basic form of the same process agrees field by field */
    unsigned char ext[0x40]; memcpy(ext, a, sizeof ext);
    s = qb_call(PSEUDO_SELF, 0, 0, 0x30, 0, 0, &rl);
    EXPECT_EQ(s, 0);
    CHECK(rd32(ext, 8) == rd32(a, 0) && !memcmp(ext + 16, a + 8, 8) && !memcmp(ext + 0x18, a + 0x10, 8));
    CHECK(rd64(ext, 0x30) == rd64(a, 0x28) && rd64(ext, 0x28) == rd64(a, 0x20));
}
TEST(c0_child_ids) {
    REQUIRE(qb_have());
    HANDLE c = qb_child();
    REQUIRE(c);
    ULONG rl;
    for (int form = 0; form < 2; form++) {
        LONG s = qb_call(c, 0, 0, form ? 0x40 : 0x30, 0, 0, &rl);
        EXPECT_EQ(s, 0);
        c0_check_body(qb_arena(), form ? 8 : 0, qb_child_pid(), GetCurrentProcessId(), 0, 0x103, 1);
    }
    /* the PEB of the child is not ours */
    qb_call(c, 0, 0, 0x30, 0, 0, &rl);
    CHECK(rd64(qb_arena(), 8) != (ULONGLONG)(ULONG_PTR)NtCurrentTeb()->ProcessEnvironmentBlock);
}
TEST(c0_exited_process) {
    REQUIRE(qb_have());
    HANDLE d = qb_dead();
    REQUIRE(d);
    ULONG rl;
    EXPECT_EQ(qb_call(d, 0, 0, 0x30, 0, 0, &rl), 0);
    EXPECT_EQ(qb_u32(0), 7);                                 /* the exit code replaces STATUS_PENDING */
    EXPECT_EQ(qb_u32(4), 0xcccccccc);
    CHECK(qb_u64(0x20) != 0 && qb_u64(0x28) == GetCurrentProcessId());
    EXPECT_EQ(qb_call(d, 0, 0, 0x40, 0, 0, &rl), 0);
    EXPECT_EQ(qb_u64(0), 0x40); EXPECT_EQ(qb_u32(8), 7);
    EXPECT_EQ(qb_u32(0x38), 4);                              /* the process-deleting flag */
}
TEST(c0_extended_size_written_before_checks) {
    REQUIRE(qb_have());
    /* the leading size field of the extended form is stored even when the call then fails on the handle */
    HANDLE bad[3] = { NULL, PSEUDO_THREAD, qb_closed_handle() };
    LONG want[3] = { ST_BADHANDLE, ST_TYPEMIS, ST_BADHANDLE };
    for (int i = 0; i < 3; i++) {
        ULONG rl;
        LONG s = qb_call(bad[i], 0, 0, 0x40, 0, 0, &rl);
        EXPECT_EQ(s, want[i]);
        EXPECT_EQ(rl, QB_SENT);
        EXPECT_EQ(qb_u64(0), 0x40);
        EXPECT_EQ(qb_untouched(8, 0x100), 1);
    }
    /* no such write for the basic form */
    ULONG rl;
    qb_call(NULL, 0, 0, 0x30, 0, 0, &rl);
    EXPECT_EQ(qb_untouched(0, 0x100), 1);
}
TEST(c0_priority_and_affinity_follow_win32) {
    REQUIRE(qb_have());
    HANDLE c = qb_child();
    REQUIRE(c);
    DWORD_PTR pm, sm;
    ULONG rl;
    CHECK(GetProcessAffinityMask(c, &pm, &sm));
    EXPECT_EQ(qb_call(c, 0, 0, 0x30, 0, 0, &rl), 0);
    EXPECT_EQ(qb_u64(0x10), pm);
    DWORD pc = GetPriorityClass(c);
    if (pc == NORMAL_PRIORITY_CLASS) EXPECT_EQ(qb_u32(0x18), 8);
    else t_note("child priority class 0x%lx base priority %ld", pc, (long)qb_u32(0x18));
}

/* ---- class 1 ----------------------------------------------------------- */
static void c1_check(HANDLE h) {
    ULONG rl;
    unsigned char b30[0x30], b58[0x58];
    EXPECT_EQ(qb_call(h, 1, 0, 0x30, 0, 0, &rl), 0);
    memcpy(b30, qb_arena(), 0x30);
    EXPECT_EQ(qb_call(h, 1, 0, 0x58, 0, 0, &rl), 0);
    memcpy(b58, qb_arena(), 0x58);
    CHECK(!memcmp(b30, b58, 0x30));
    ULONGLONG minws = rd64(b58, 0x10), maxws = rd64(b58, 0x18);
    CHECK(minws != 0 && minws <= maxws);
    CHECK((minws & 0xfff) == 0 && (maxws & 0xfff) == 0);
    EXPECT_EQ(rd64(b58, 0x20), ~0ULL);                         /* no pagefile limit */
    EXPECT_EQ(rd64(b58, 0x28), ~0ULL);                         /* no time limit */
    CHECK(rd64(b58, 0x00) != 0 && rd64(b58, 0x08) != 0);       /* paged and nonpaged pool quota */
    t_note("class 1 extended tail %llx %llx %llx %llx %llx", rd64(b58, 0x30), rd64(b58, 0x38), rd64(b58, 0x40), rd64(b58, 0x48), rd64(b58, 0x50));
}
TEST(c1_self_limits) { REQUIRE(qb_have()); c1_check(PSEUDO_SELF); }
TEST(c1_child_limits) { REQUIRE(qb_have()); HANDLE c = qb_child(); REQUIRE(c); c1_check(c); }
TEST(c1_exited_process_limits) { REQUIRE(qb_have()); HANDLE d = qb_dead(); REQUIRE(d); c1_check(d); }
TEST(c1_limits_match_pooled_class) {
    REQUIRE(qb_have());
    ULONG rl;
    unsigned char q[0x30], p[0x48];
    EXPECT_EQ(qb_call(PSEUDO_SELF, 1, 0, 0x30, 0, 0, &rl), 0); memcpy(q, qb_arena(), 0x30);
    EXPECT_EQ(qb_call(PSEUDO_SELF, 14, 0, 0x48, 0, 0, &rl), 0); memcpy(p, qb_arena(), 0x48);
    EXPECT_EQ(rd64(q, 0), rd64(p, 0x10));    /* paged pool limit */
    EXPECT_EQ(rd64(q, 8), rd64(p, 0x28));    /* nonpaged pool limit */
    EXPECT_EQ(rd64(q, 0x20), rd64(p, 0x40)); /* pagefile limit */
}

/* ---- class 14 ---------------------------------------------------------- */
static void c14_check(HANDLE h) {
    ULONG rl;
    EXPECT_EQ(qb_call(h, 14, 0, 0x48, 0, 0, &rl), 0);
    const unsigned char *b = qb_arena();
    /* three triples (peak, usage, limit) for paged pool, nonpaged pool and pagefile */
    CHECK(rd64(b, 0x08) <= rd64(b, 0x00) && rd64(b, 0x00) <= rd64(b, 0x10));
    CHECK(rd64(b, 0x20) <= rd64(b, 0x18) && rd64(b, 0x18) <= rd64(b, 0x28));
    CHECK(rd64(b, 0x38) <= rd64(b, 0x30) && rd64(b, 0x30) <= rd64(b, 0x40));
    CHECK(rd64(b, 0x10) != 0 && rd64(b, 0x28) != 0);
}
TEST(c14_self_relations) { REQUIRE(qb_have()); c14_check(PSEUDO_SELF); }
TEST(c14_child_relations) { REQUIRE(qb_have()); HANDLE c = qb_child(); REQUIRE(c); c14_check(c); }
TEST(c14_exited_relations) { REQUIRE(qb_have()); HANDLE d = qb_dead(); REQUIRE(d); c14_check(d); }

/* ---- class 2 ----------------------------------------------------------- */
TEST(c2_counters_follow_file_io) {
    REQUIRE(qb_have());
    ULONG rl;
    unsigned char a[0x30], b[0x30];
    char tf[300];
    t_tmpfile(tf, sizeof tf, "qb_io.bin");
    HANDLE f = CreateFileA(tf, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    REQUIRE(f != INVALID_HANDLE_VALUE);
    char buf[512] = { 0 };
    DWORD n;
    EXPECT_EQ(qb_call(PSEUDO_SELF, 2, 0, 0x30, 0, 0, &rl), 0); memcpy(a, qb_arena(), 0x30);
    for (int i = 0; i < 10; i++) WriteFile(f, buf, sizeof buf, &n, NULL);
    SetFilePointer(f, 0, NULL, FILE_BEGIN);
    for (int i = 0; i < 10; i++) ReadFile(f, buf, sizeof buf, &n, NULL);
    EXPECT_EQ(qb_call(PSEUDO_SELF, 2, 0, 0x30, 0, 0, &rl), 0); memcpy(b, qb_arena(), 0x30);
    CloseHandle(f);
    DeleteFileA(tf);
    /* ReadOperationCount, WriteOperationCount, OtherOperationCount, ReadTransferCount, WriteTransferCount, OtherTransferCount */
    CHECKF(rd64(b, 0x00) >= rd64(a, 0x00) + 10, "read ops %llu -> %llu", rd64(a, 0), rd64(b, 0));
    CHECKF(rd64(b, 0x08) >= rd64(a, 0x08) + 10, "write ops %llu -> %llu", rd64(a, 8), rd64(b, 8));
    CHECKF(rd64(b, 0x18) >= rd64(a, 0x18) + 5120, "read bytes %llu -> %llu", rd64(a, 0x18), rd64(b, 0x18));
    CHECKF(rd64(b, 0x20) >= rd64(a, 0x20) + 5120, "write bytes %llu -> %llu", rd64(a, 0x20), rd64(b, 0x20));
    for (int i = 0; i < 6; i++) CHECK(rd64(b, i * 8) >= rd64(a, i * 8));
    CHECK(rd64(b, 0x10) >= 1 && rd64(b, 0x28) >= 1);          /* other operations: the open and close at least */
}
TEST(c2_child_and_exited_counters) {
    REQUIRE(qb_have());
    HANDLE c = qb_child(), d = qb_dead();
    REQUIRE(c && d);
    ULONG rl;
    EXPECT_EQ(qb_call(c, 2, 0, 0x30, 0, 0, &rl), 0);
    CHECK(qb_u64(0x10) >= 1);                                  /* process start did some other io (image load) */
    EXPECT_EQ(qb_call(d, 2, 0, 0x30, 0, 0, &rl), 0);
    CHECK(qb_u64(0x10) >= 1);
}

/* ---- class 3 ----------------------------------------------------------- */
static void c3_rel(const unsigned char *b) {
    CHECK(rd64(b, 0x00) >= rd64(b, 0x08) && rd64(b, 0x08) != 0);          /* PeakVirtualSize >= VirtualSize */
    CHECK(rd64(b, 0x18) >= rd64(b, 0x20) && rd64(b, 0x20) != 0);          /* PeakWorkingSetSize >= WorkingSetSize */
    CHECK(rd64(b, 0x28) >= rd64(b, 0x30));                                /* QuotaPeakPagedPoolUsage >= QuotaPagedPoolUsage */
    CHECK(rd64(b, 0x38) >= rd64(b, 0x40));                                /* QuotaPeakNonPagedPoolUsage >= QuotaNonPagedPoolUsage */
    CHECK(rd64(b, 0x50) >= rd64(b, 0x48) && rd64(b, 0x48) != 0);          /* PeakPagefileUsage >= PagefileUsage */
    CHECK(rd32(b, 0x10) > 0);                                             /* PageFaultCount */
}
static void c3_check(HANDLE h) {
    ULONG rl;
    EXPECT_EQ(qb_call(h, 3, 0, 0x58, 0, 0, &rl), 0); c3_rel(qb_arena());
    EXPECT_EQ(qb_untouched(0x58, 0x70), 1);
    EXPECT_EQ(qb_call(h, 3, 0, 0x60, 0, 0, &rl), 0); c3_rel(qb_arena());
    EXPECT_EQ(qb_u64(0x58), qb_u64(0x48));                                /* PrivateUsage == PagefileUsage */
    EXPECT_EQ(qb_untouched(0x60, 0x70), 1);
    EXPECT_EQ(qb_call(h, 3, 0, 0x70, 0, 0, &rl), 0); c3_rel(qb_arena());
    EXPECT_EQ(qb_u64(0x58), qb_u64(0x48));
    CHECK(qb_u64(0x60) <= qb_u64(0x20));                                  /* private working set <= working set */
}
TEST(c3_self_relations) { REQUIRE(qb_have()); c3_check(PSEUDO_SELF); }
TEST(c3_child_relations) { REQUIRE(qb_have()); HANDLE c = qb_child(); REQUIRE(c); c3_check(c); }
TEST(c3_exited_relations) {
    REQUIRE(qb_have());
    HANDLE d = qb_dead();
    REQUIRE(d);
    ULONG rl;
    EXPECT_EQ(qb_call(d, 3, 0, 0x58, 0, 0, &rl), 0);
    const unsigned char *b = qb_arena();
    CHECK(rd64(b, 0x00) >= rd64(b, 0x08));
    CHECK(rd64(b, 0x50) >= rd64(b, 0x48));
    CHECK(rd64(b, 0x18) >= rd64(b, 0x20));
    EXPECT_EQ(rd64(b, 0x08), 0);                                         /* no address space any more */
    t_note("exited process: working set %s", rd64(b, 0x20) ? "kept" : "zero");
}
TEST(c3_matches_psapi) {
    REQUIRE(qb_have());
    PROCESS_MEMORY_COUNTERS_EX m;
    memset(&m, 0, sizeof m);
    CHECK(GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&m, sizeof m));
    ULONG rl;
    EXPECT_EQ(qb_call(PSEUDO_SELF, 3, 0, 0x60, 0, 0, &rl), 0);
    const unsigned char *b = qb_arena();
    /* the Win32 front door reports the same pagefile numbers (they move only with commit changes) */
    EXPECT_EQ(rd64(b, 0x48), m.PagefileUsage);
    EXPECT_EQ(rd64(b, 0x58), m.PrivateUsage);
    CHECK(rd64(b, 0x50) >= m.PagefileUsage);
}
TEST(c3_commit_growth_and_peaks) {
    REQUIRE(qb_have());
    ULONG rl;
    unsigned char a[0x70], b[0x70], c[0x70];
    EXPECT_EQ(qb_call(PSEUDO_SELF, 3, 0, 0x70, 0, 0, &rl), 0); memcpy(a, qb_arena(), 0x70);
    SIZE_T sz = 0x400000;
    char *p = VirtualAlloc(NULL, sz, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    REQUIRE(p);
    memset(p, 1, sz);
    EXPECT_EQ(qb_call(PSEUDO_SELF, 3, 0, 0x70, 0, 0, &rl), 0); memcpy(b, qb_arena(), 0x70);
    VirtualFree(p, 0, MEM_RELEASE);
    EXPECT_EQ(qb_call(PSEUDO_SELF, 3, 0, 0x70, 0, 0, &rl), 0); memcpy(c, qb_arena(), 0x70);
    CHECKF(rd64(b, 0x48) >= rd64(a, 0x48) + sz, "PagefileUsage %llx -> %llx", rd64(a, 0x48), rd64(b, 0x48));
    CHECKF(rd64(b, 0x08) >= rd64(a, 0x08) + sz, "VirtualSize grew less than the allocation");
    CHECKF(rd32(b, 0x10) >= rd32(a, 0x10) + 1000, "PageFaultCount %lu -> %lu", rd32(a, 0x10), rd32(b, 0x10));
    CHECK(rd64(b, 0x58) >= rd64(a, 0x58) + sz);
    CHECK(rd64(b, 0x00) >= rd64(a, 0x00));
    /* after the free the usage falls back below the high point, the peaks stay */
    CHECK(rd64(c, 0x48) < rd64(b, 0x48));
    CHECK(rd64(c, 0x50) >= rd64(b, 0x48));
    CHECK(rd64(c, 0x00) >= rd64(b, 0x08));
    CHECK(rd64(c, 0x08) < rd64(b, 0x08));
    CHECK(rd64(c, 0x18) >= rd64(c, 0x20));
    CHECK(rd32(c, 0x10) >= rd32(b, 0x10));
}

/* ---- class 4 ----------------------------------------------------------- */
TEST(c4_running_process_times) {
    REQUIRE(qb_have());
    ULONG rl;
    FILETIME c, e, k, u;
    EXPECT_EQ(qb_call(PSEUDO_SELF, 4, 0, 0x20, 0, 0, &rl), 0);
    ULONGLONG create = rd64(qb_arena(), 0), exitt = rd64(qb_arena(), 8), kern = rd64(qb_arena(), 0x10), user = rd64(qb_arena(), 0x18);
    CHECK(GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u));
    EXPECT_EQ(create, ft64(c));
    EXPECT_EQ(exitt, 0);
    CHECK(ft64(k) >= kern && ft64(u) >= user);
    FILETIME now; GetSystemTimeAsFileTime(&now);
    CHECK(create != 0 && create <= ft64(now));
}
TEST(c4_monotone_cpu_time) {
    REQUIRE(qb_have());
    ULONG rl;
    EXPECT_EQ(qb_call(PSEUDO_SELF, 4, 0, 0x20, 0, 0, &rl), 0);
    ULONGLONG k0 = qb_u64(0x10), u0 = qb_u64(0x18);
    volatile ULONG x = 0;
    for (ULONG i = 0; i < 3000000; i++) x += i;
    HANDLE f = CreateFileA("NUL", GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) { DWORD n; for (int i = 0; i < 2000; i++) WriteFile(f, "x", 1, &n, NULL); CloseHandle(f); }
    EXPECT_EQ(qb_call(PSEUDO_SELF, 4, 0, 0x20, 0, 0, &rl), 0);
    CHECK(qb_u64(0x10) >= k0 && qb_u64(0x18) >= u0);
}
TEST(c4_child_times_match_win32) {
    REQUIRE(qb_have());
    HANDLE ch = qb_child();
    REQUIRE(ch);
    ULONG rl;
    FILETIME c, e, k, u;
    EXPECT_EQ(qb_call(ch, 4, 0, 0x20, 0, 0, &rl), 0);
    CHECK(GetProcessTimes(ch, &c, &e, &k, &u));
    EXPECT_EQ(qb_u64(0), ft64(c));
    EXPECT_EQ(qb_u64(8), 0);
    CHECK(ft64(k) >= qb_u64(0x10) && ft64(u) >= qb_u64(0x18));
    /* the child was created after this process */
    ULONGLONG child_create = ft64(c);
    EXPECT_EQ(qb_call(PSEUDO_SELF, 4, 0, 0x20, 0, 0, &rl), 0);
    CHECK(child_create >= qb_u64(0));
}
TEST(c4_exited_process_times) {
    REQUIRE(qb_have());
    HANDLE d = qb_dead();
    REQUIRE(d);
    ULONG rl;
    FILETIME c, e, k, u;
    EXPECT_EQ(qb_call(d, 4, 0, 0x20, 0, 0, &rl), 0);
    ULONGLONG create = qb_u64(0), exitt = qb_u64(8);
    CHECK(GetProcessTimes(d, &c, &e, &k, &u));
    EXPECT_EQ(create, ft64(c));
    EXPECT_EQ(exitt, ft64(e));
    CHECK(exitt != 0 && exitt >= create);
}

/* ---- class 20 ---------------------------------------------------------- */
TEST(c20_count_matches_win32) {
    REQUIRE(qb_have());
    ULONG rl, n32 = 0;
    EXPECT_EQ(qb_call(PSEUDO_SELF, 20, 0, 4, 0, 0, &rl), 0);
    ULONG n = qb_u32(0);
    CHECK(GetProcessHandleCount(GetCurrentProcess(), &n32));
    EXPECT_EQ(n, n32);
    CHECK(n >= 3);                                                      /* at least the standard handles' worth of objects */
    EXPECT_EQ(qb_call(PSEUDO_SELF, 20, 0, 8, 0, 0, &rl), 0);
    EXPECT_EQ(rl, 4);
    EXPECT_EQ(qb_u32(0), n);
    CHECK(qb_u32(4) >= qb_u32(0));
    EXPECT_EQ(qb_u32(4), qb_u32(0));                                    /* second dword mirrors the count */
}
TEST(c20_count_follows_open_handles) {
    REQUIRE(qb_have());
    ULONG rl;
    HANDLE ev[5];
    EXPECT_EQ(qb_call(PSEUDO_SELF, 20, 0, 4, 0, 0, &rl), 0);
    ULONG n0 = qb_u32(0);
    for (int i = 0; i < 5; i++) ev[i] = CreateEventA(NULL, FALSE, FALSE, NULL);
    EXPECT_EQ(qb_call(PSEUDO_SELF, 20, 0, 8, 0, 0, &rl), 0);
    EXPECT_EQ(qb_u32(0), n0 + 5);
    CHECK(qb_u32(4) >= qb_u32(0));
    HANDLE dup = NULL;
    CHECK(DuplicateHandle(GetCurrentProcess(), ev[0], GetCurrentProcess(), &dup, 0, FALSE, DUPLICATE_SAME_ACCESS));
    EXPECT_EQ(qb_call(PSEUDO_SELF, 20, 0, 4, 0, 0, &rl), 0);
    EXPECT_EQ(qb_u32(0), n0 + 6);
    for (int i = 0; i < 5; i++) CloseHandle(ev[i]);
    CloseHandle(dup);
    EXPECT_EQ(qb_call(PSEUDO_SELF, 20, 0, 4, 0, 0, &rl), 0);
    EXPECT_EQ(qb_u32(0), n0);
}
TEST(c20_child_and_exited) {
    REQUIRE(qb_have());
    HANDLE c = qb_child(), d = qb_dead();
    REQUIRE(c && d);
    ULONG rl, n32 = 0;
    EXPECT_EQ(qb_call(c, 20, 0, 8, 0, 0, &rl), 0);
    CHECK(qb_u32(0) >= 1 && qb_u32(4) >= qb_u32(0));
    CHECK(GetProcessHandleCount(c, &n32));
    EXPECT_EQ(qb_u32(0), n32);
    EXPECT_EQ(qb_call(d, 20, 0, 8, 0, 0, &rl), 0);
    EXPECT_EQ(qb_u32(0), 0); EXPECT_EQ(qb_u32(4), 0); EXPECT_EQ(rl, 4);   /* the handle table is gone */
}

/* ---- class 38 ---------------------------------------------------------- */
TEST(c38_cycles_monotone) {
    REQUIRE(qb_have());
    ULONG rl;
    EXPECT_EQ(qb_call(PSEUDO_SELF, 38, 0, 0x10, 0, 0, &rl), 0);
    ULONGLONG a0 = qb_u64(0), c0 = qb_u64(8);
    CHECK(a0 != 0 && c0 != 0);
    volatile ULONG x = 0;
    for (ULONG i = 0; i < 1000000; i++) x += i;
    EXPECT_EQ(qb_call(PSEUDO_SELF, 38, 0, 0x10, 0, 0, &rl), 0);
    CHECKF(qb_u64(0) > a0, "accumulated cycles %llu -> %llu", a0, qb_u64(0));
    CHECK(qb_u64(8) > c0);
}
TEST(c38_matches_cycle_time_api) {
    REQUIRE(qb_have());
    ULONG rl;
    ULONG64 cyc = 0;
    EXPECT_EQ(qb_call(PSEUDO_SELF, 38, 0, 0x10, 0, 0, &rl), 0);
    ULONGLONG acc = qb_u64(0);
    CHECK(QueryProcessCycleTime(GetCurrentProcess(), &cyc));
    CHECK(cyc >= acc);
    HANDLE c = qb_child();
    REQUIRE(c);
    EXPECT_EQ(qb_call(c, 38, 0, 0x10, 0, 0, &rl), 0);
    ULONGLONG cur1 = qb_u64(8);
    EXPECT_EQ(qb_call(PSEUDO_SELF, 38, 0, 0x10, 0, 0, &rl), 0);
    CHECK(qb_u64(8) >= cur1);                                           /* the second qword is a machine wide counter */
}
TEST(c38_exited_process) {
    REQUIRE(qb_have());
    HANDLE d = qb_dead();
    REQUIRE(d);
    ULONG rl;
    ULONG64 cyc = 0;
    EXPECT_EQ(qb_call(d, 38, 0, 0x10, 0, 0, &rl), 0);
    ULONGLONG acc = qb_u64(0);
    CHECK(acc != 0);
    CHECK(QueryProcessCycleTime(d, &cyc));
    EXPECT_EQ(cyc, acc);                                                /* frozen after exit */
}

/* ---- classes 54, 55, 71 ------------------------------------------------ */
TEST(c54_c55_default_values) {
    REQUIRE(qb_have());
    HANDLE hs[2] = { PSEUDO_SELF, qb_child() };
    REQUIRE(hs[1]);
    for (int i = 0; i < 2; i++) {
        ULONG rl;
        EXPECT_EQ(qb_call(hs[i], 54, 0, 4, 0, 0, &rl), 0);
        EXPECT_EQ(qb_u32(0), 0);
        EXPECT_EQ(qb_call(hs[i], 55, 0, 8, 0, 0, &rl), 0);
        EXPECT_EQ(qb_u64(0), 0);
        EXPECT_EQ(rl, QB_SENT);
    }
}
TEST(c71_values_and_set_roundtrip) {
    REQUIRE(qb_have());
    ULONG rl;
    EXPECT_EQ(qb_call(PSEUDO_SELF, 71, 0, 4, 0, 0, &rl), 0);
    ULONG orig = qb_u32(0);
    CHECK(orig <= 1);
    t_note("class 71 self initial value %lu", orig);
    HANDLE c = qb_child();
    if (c) { EXPECT_EQ(qb_call(c, 71, 0, 4, 0, 0, &rl), 0); CHECK(qb_u32(0) <= 1); t_note("class 71 child value %lu", qb_u32(0)); }
    ULONG v = 1;
    LONG s = qb_set(PSEUDO_SELF, 71, &v, 4);
    if (s != 0) { t_skip("set of class 71 refused with 0x%lx", (ULONG)s); return; }
    EXPECT_EQ(qb_call(PSEUDO_SELF, 71, 0, 4, 0, 0, &rl), 0);
    EXPECT_EQ(qb_u32(0), 1);
    v = 0;
    EXPECT_EQ(qb_set(PSEUDO_SELF, 71, &v, 4), 0);
    EXPECT_EQ(qb_call(PSEUDO_SELF, 71, 0, 4, 0, 0, &rl), 0);
    EXPECT_EQ(qb_u32(0), 0);
    v = orig;
    qb_set(PSEUDO_SELF, 71, &v, 4);
}
