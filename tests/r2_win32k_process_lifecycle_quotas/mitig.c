/* mitig: ProcessSystemCallDisablePolicy (mitigation policy 4) seen against the process-lifecycle syscalls.
 * A locked process (EPROCESS flag 0x1000) cannot be converted to a GUI thread: KiSystemCall64 answers every win32k
 * service number with STATUS_INVALID_SYSTEM_SERVICE (0xC000001C) before win32kfull runs, so the csrss-only guards of
 * guards.c are never reached. Setting the policy after the thread became a GUI thread fails (STATUS_ALREADY_WIN32).
 * Policy readback: kernelbase!GetProcessMitigationPolicy -> NtQueryInformationProcess(ProcessMitigationPolicy=0x34,
 * {Policy, Flags}); policy 4 reports Disallow in bit 0.
 */
#include "r2.h"

typedef BOOL (WINAPI *setmit_t)(int, PVOID, SIZE_T);
typedef BOOL (WINAPI *getmit_t)(HANDLE, int, PVOID, SIZE_T);
typedef LONG (NTAPI *ntq_t)(HANDLE, ULONG, PVOID, ULONG, PULONG);

static BOOL set_scd(DWORD fl) { setmit_t f = (setmit_t)GetProcAddress(GetModuleHandleA("kernel32.dll"), "SetProcessMitigationPolicy"); return f ? f(4, &fl, 4) : FALSE; }
static BOOL get_scd(HANDLE h, DWORD *fl);
/* the child of a locked scenario was created locked: it only has to confirm that */
static BOOL self_locked(void) { DWORD fl = 0; return get_scd(GetCurrentProcess(), &fl) && (fl & 1); }
static BOOL get_scd(HANDLE h, DWORD *fl) {
    getmit_t f = (getmit_t)GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetProcessMitigationPolicy");
    DWORD v = 0xAAAAAAAA; BOOL ok = f ? f(h, 4, &v, 4) : FALSE; *fl = v; return ok;
}
static LONG nt_get(HANDLE h, DWORD pol, DWORD *fl) {
    ntq_t f = (ntq_t)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationProcess");
    DWORD b[2] = { pol, 0xAAAAAAAA }; LONG st = f ? f(h, 0x34, b, 8, NULL) : (LONG)0xC0000002; *fl = b[1]; return st;
}
static int have_policy(void) {
    if (t_is_wine()) return 0;
    DWORD fl; return get_scd(GetCurrentProcess(), &fl);
}

R2_SCN(mit_default_process_is_unlocked) {
    if (!have_policy()) { printf("  SKIP no mitigation policy support\n"); return; }
    DWORD fl = 0xFF;
    RC(get_scd(GetCurrentProcess(), &fl)); REQ(fl, 0);
    DWORD fl2 = 0xFF; RST(nt_get(GetCurrentProcess(), 4, &fl2), 0); REQ(fl2, 0);
    /* an unlocked process converts its thread when user32 loads (a bare win32k call before that is fatal on Windows 10) */
    void *pti = (void *)1; k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &pti); RC(pti == NULL);
    RC(LoadLibraryA("user32.dll") != NULL);
    (void)SYS0("NtUserGetThreadState");
    pti = NULL; k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &pti);
    RC(pti != NULL);   /* Win10 22H2: a small non-pointer value (the THREADINFO is not addressable from user mode), set once the thread is a GUI thread */
}
R2_SCN_LOCKED(mit_locked_lifecycle_syscalls_invalid_service) {
    if (!have_policy()) { printf("  SKIP no mitigation policy support\n"); return; }
    RC(self_locked());
    DWORD fl = 0; RC(get_scd(GetCurrentProcess(), &fl)); REQ(fl & 1, 1);
    /* Four of the services are refused with STATUS_INVALID_SYSTEM_SERVICE; the four that only set per-process or
     * per-thread state are filtered down to a zero result on Windows 10 22H2 (no state is set, no error status). */
    struct { const char *n; ULONG_PTR a1, a2, a3, a4; ULONG want; } t[] = {
        { "NtUserQueryInformationThread", (ULONG_PTR)-2, 0, 0, 0, 0xC000001C },
        { "NtUserNotifyProcessCreate", GetCurrentProcessId(), 0, 0, 1, 0 },
        { "NtUserRemoteConnect", 0, 0, 0, 0, 0xC000001C },
        { "NtUserInitTask", 0, 0, 0, 0, 0xC000001C },
        { "NtUserSetThreadState", 0, 0, 0, 0, 0 },
        { "NtUserSetProcessInteractionFlags", 0, 0, 0, 0, 0 },
        { "NtUserSetProcessMousewheelRoutingMode", 1, 0, 0, 0, 0 },
        { "NtUserProcessConnect", (ULONG_PTR)-1, 0, 0, 0, 0xC000001C },
    };
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
        r2_sys12 f = (r2_sys12)r2_w32u(t[i].n);
        if (!f) { printf("  SKIP missing %s\n", t[i].n); continue; }
        ULONG st = (ULONG)f(t[i].a1, t[i].a2, t[i].a3, t[i].a4, 0, 0, 0, 0, 0, 0, 0, 0);
        RCF(st == t[i].want, "%s -> %s", t[i].n, k_status_name((K_NTSTATUS)(LONG)st));
    }
    /* none of the refused calls converted the thread or connected the process */
    void *pti = (void *)1; k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &pti); RC(pti == NULL);
    void *gt = (void *)1; k_try_read_ptr((char *)k_peb() + PEB_GdiSharedHandleTable, &gt); RC(gt == NULL);
}
R2_SCN_LOCKED(mit_locked_repeat_is_stable) {
    if (!have_policy()) { printf("  SKIP no mitigation policy support\n"); return; }
    RC(self_locked());
    for (int i = 0; i < 5; i++) {
        ULONG st = (ULONG)SYS0("NtUserInitTask");
        RCF(st == 0xC000001C, "round %d -> %s", i, k_status_name((K_NTSTATUS)(LONG)st));
    }
    DWORD fl = 0; RC(get_scd(GetCurrentProcess(), &fl)); REQ(fl & 1, 1);
}
static DWORD WINAPI mit_worker(LPVOID p) {
    ULONG st = (ULONG)SYS("NtUserInitTask", 0, 0, 0,0,0,0,0,0,0,0,0,0);
    return st == 0xC000001C ? 0 : 1;
}
R2_SCN_LOCKED(mit_locked_worker_thread_also_refused) {
    if (!have_policy()) { printf("  SKIP no mitigation policy support\n"); return; }
    RC(self_locked());
    HANDLE h = CreateThread(NULL, 0, mit_worker, NULL, 0, NULL); RC(h != NULL); if (!h) return;
    REQ(r2_wait_thread(h, 5000), 0); CloseHandle(h);
}
R2_SCN(mit_after_conversion_policy_cannot_be_set) {
    if (!have_policy()) { printf("  SKIP no mitigation policy support\n"); return; }
    RC(LoadLibraryA("user32.dll") != NULL);              /* converts the thread (a bare win32k call would be fatal here) */
    ULONG st = (ULONG)SYS("NtUserProcessConnect", (ULONG_PTR)-1, 0, 0, 0, 0,0,0,0,0,0,0,0);
    RST(st, 0xC0000001);                                 /* refused by the csrss check */
    void *pti = NULL; k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &pti);
    RC(pti != NULL);   /* Win10 22H2: a small non-pointer value (the THREADINFO is not addressable from user mode), set once the thread is a GUI thread */
    SetLastError(0);
    RC(set_scd(1) == FALSE);                             /* too late: the thread already is a GUI thread */
    DWORD fl = 0xFF; RC(get_scd(GetCurrentProcess(), &fl)); REQ(fl, 0);
    RST(SYS0("NtUserInitTask"), 0xC0000001);              /* and the process keeps its normal behaviour */
}
R2_SCN_LOCKED(mit_locked_parked) {
    if (!have_policy()) { printf("  SKIP no mitigation policy support\n"); r2_park(); return; }
    RC(self_locked());
    r2_park();
}
R2_SCN(mit_unlocked_parked) {
    if (!have_policy()) { printf("  SKIP no mitigation policy support\n"); r2_park(); return; }
    RC(r2_connect());
    r2_park();
}
TEST(r2_win32k_process_lifecycle_quotas_mit_locked_child_policy_read_by_parent) {
    if (t_is_wine()) { t_skip("Wine has no win32k mitigation"); return; }
    struct r2_proc p;
    if (!r2_start_locked(&p, "mit_locked_parked")) { t_skip("cannot start child"); return; }
    if (!r2_wait_ready(&p, 20000)) { CHECK(0); r2_finish(&p, 100); return; }
    HANDLE hp = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, p.pid); REQUIRE(hp);
    DWORD fl = 0xFF; CHECK(get_scd(hp, &fl)); EXPECT_EQ(fl, 1);
    DWORD fl2 = 0xFF; LONG st = nt_get(hp, 4, &fl2); RST(st, 0); EXPECT_EQ(fl2, 1);
    DWORD mine = 0xFF; CHECK(get_scd(GetCurrentProcess(), &mine)); EXPECT_EQ(mine, 0);
    CloseHandle(hp);
    r2_go(&p);
    EXPECT_EQ(r2_finish(&p, 20000), 0);
}
TEST(r2_win32k_process_lifecycle_quotas_mit_locked_child_has_no_gui_resources) {
    if (t_is_wine()) { t_skip("Wine has no win32k mitigation"); return; }
    R2_NEED_WIN();
    struct r2_proc p;
    if (!r2_start_locked(&p, "mit_locked_parked")) { t_skip("cannot start child"); return; }
    if (!r2_wait_ready(&p, 20000)) { CHECK(0); r2_finish(&p, 100); return; }
    HANDLE hp = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, p.pid); REQUIRE(hp);
    SetLastError(0x7777);
    /* a process that never got a W32PROCESS reports zero for every counter (NtUserGetGuiResources reads +0x3c..+0x48 only if present) */
    for (DWORD f = 0; f <= 4; f++) if (f != 3) CHECKF(U(GetGuiResources)(hp, f) == 0, "flag %lu", f);
    CloseHandle(hp);
    r2_go(&p);
    EXPECT_EQ(r2_finish(&p, 20000), 0);
}
TEST(r2_win32k_process_lifecycle_quotas_mit_unlocked_child_policy_clear_and_counts_nonzero) {
    if (t_is_wine()) { t_skip("Wine has no win32k mitigation"); return; }
    R2_NEED_WIN();
    struct r2_proc p;
    if (!r2_start(&p, "mit_unlocked_parked")) { t_skip("cannot start child"); return; }
    if (!r2_wait_ready(&p, 20000)) { CHECK(0); r2_finish(&p, 100); return; }
    HANDLE hp = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, p.pid); REQUIRE(hp);
    DWORD fl = 0xFF; CHECK(get_scd(hp, &fl)); EXPECT_EQ(fl, 0);
    CHECK(U(GetGuiResources)(hp, 4) >= U(GetGuiResources)(hp, 1));
    CloseHandle(hp);
    r2_go(&p);
    EXPECT_EQ(r2_finish(&p, 20000), 0);
}
