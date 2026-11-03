/* guards: win32k process/thread lifecycle syscalls that only csrss (or the session manager side) may use.
 * From an ordinary process each one must fail at its first test, before probing any argument.
 *   NtUserQueryInformationThread  win32kfull 0x7cc00: process != gpepCSRSS -> STATUS_ACCESS_DENIED
 *   NtUserNotifyProcessCreate     win32kfull 0x3ab60: argument 4 != 0 and caller != csrss -> 0xC0000022
 *   NtUserRemoteConnect           win32kfull 0x128000: caller != csrss -> 0xC0000022
 *   NtUserInitTask                bound to the stub returning STATUS_UNSUCCESSFUL
 *   NtUserSetThreadState(flags,mask) win32kfull 0x122350: flags & ~0x4000 -> LastError 0xC000000D, FALSE
 * Direct stub calls give the exact kernel status.
 */
#include "r2.h"

TEST(r2_win32k_process_lifecycle_quotas_guard_queryinfothread_access_denied_table) {
    R2_NEED_WIN();
    REQUIRE(r2_w32u("NtUserQueryInformationThread"));
    /* (thread handle, class, buffer, length): the csrss test precedes ObReferenceObjectByHandle and the probe */
    static const struct { ULONG_PTR h; ULONG cls; ULONG len; } t[] = {
        { 0, 0, 0 }, { (ULONG_PTR)-2, 0, 0 }, { (ULONG_PTR)-2, 0xb, 0 }, { (ULONG_PTR)-2, 1, 0 }, { 0x1234, 0xFFFF, 0 },
        { (ULONG_PTR)-2, 2, 0x10 }
    };
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
        char buf[0x20] = {0};
        ULONG_PTR b = t[i].len ? (ULONG_PTR)buf : 0;
        ULONG r = (ULONG)SYS("NtUserQueryInformationThread", t[i].h, t[i].cls, b, t[i].len, 0,0,0,0,0,0,0,0);
        CHECKF(r == 0xC0000022, "case %u: %s", i, k_status_name((K_NTSTATUS)(LONG)r));
    }
}
TEST(r2_win32k_process_lifecycle_quotas_guard_queryinfothread_buffer_untouched) {
    R2_NEED_WIN();
    REQUIRE(r2_w32u("NtUserQueryInformationThread"));
    unsigned char buf[0x40]; memset(buf, 0x5A, sizeof buf);
    ULONG r = (ULONG)SYS("NtUserQueryInformationThread", (ULONG_PTR)-2, 0, (ULONG_PTR)buf, 0x20, 0,0,0,0,0,0,0,0);
    RST(r, 0xC0000022);
    int same = 1; for (unsigned i = 0; i < sizeof buf; i++) same &= buf[i] == 0x5A;
    CHECK(same);                      /* the output copy only happens on the csrss path */
}
TEST(r2_win32k_process_lifecycle_quotas_guard_queryinfothread_repeatable) {
    R2_NEED_WIN();
    REQUIRE(r2_w32u("NtUserQueryInformationThread"));
    for (int i = 0; i < 4; i++) { ULONG r = (ULONG)SYS("NtUserQueryInformationThread", (ULONG_PTR)-2, 0, 0, 0, 0,0,0,0,0,0,0,0); RST(r, 0xC0000022); }
}
TEST(r2_win32k_process_lifecycle_quotas_guard_notifyprocesscreate_nonzero_flags_denied) {
    R2_NEED_WIN();
    REQUIRE(r2_w32u("NtUserNotifyProcessCreate"));
    static const ULONG flags[] = { 1, 2, 4, 0x10, 0x80000000, 0xFFFFFFFF };
    for (unsigned i = 0; i < sizeof flags / sizeof flags[0]; i++) {
        /* (pid, pid2, ULONG64, flags): the 4th argument selects the csrss-only path */
        ULONG r = (ULONG)SYS("NtUserNotifyProcessCreate", GetCurrentProcessId(), 0, 0, flags[i], 0,0,0,0,0,0,0,0);
        CHECKF(r == 0xC0000022, "flags 0x%lx: %s", (unsigned long)flags[i], k_status_name((K_NTSTATUS)(LONG)r));
    }
}
TEST(r2_win32k_process_lifecycle_quotas_guard_notifyprocesscreate_flags_taken_from_32_bits) {
    R2_NEED_WIN();
    REQUIRE(r2_w32u("NtUserNotifyProcessCreate"));
 /* argument 4 is an ulong: a set bit above bit 31 with low dword 1 is still non-zero, so the denied path is taken */
    ULONG r = (ULONG)SYS("NtUserNotifyProcessCreate", GetCurrentProcessId(), 0, 0, 0xFFFFFFFF00000001ULL, 0,0,0,0,0,0,0,0);
    RST(r, 0xC0000022);
}
TEST(r2_win32k_process_lifecycle_quotas_guard_remoteconnect_denied) {
    R2_NEED_WIN();
    REQUIRE(r2_w32u("NtUserRemoteConnect"));
    ULONG_PTR args[][3] = { { 0, 0, 0 }, { 0, 9, 0 }, { 0x1000, 0xFFFFFFFF, 0x1000 }, { 0xFFFF800000000000ULL, 1, 0xFFFF800000000000ULL } };
    for (unsigned i = 0; i < sizeof args / sizeof args[0]; i++) {
        /* the process check precedes the USERCONNECT copy and the 9-character name probe */
        ULONG r = (ULONG)SYS("NtUserRemoteConnect", args[i][0], args[i][1], args[i][2], 0,0,0,0,0,0,0,0,0);
        CHECKF(r == 0xC0000022, "case %u: %s", i, k_status_name((K_NTSTATUS)(LONG)r));
    }
}
TEST(r2_win32k_process_lifecycle_quotas_guard_inittask_unsuccessful) {
    R2_NEED_WIN();
    REQUIRE(r2_w32u("NtUserInitTask"));
    for (int i = 0; i < 3; i++) { ULONG r = (ULONG)SYS0("NtUserInitTask"); RST(r, 0xC0000001); }
}
TEST(r2_win32k_process_lifecycle_quotas_guard_inittask_ignores_arguments) {
    R2_NEED_WIN();
    REQUIRE(r2_w32u("NtUserInitTask"));
    ULONG r = (ULONG)SYS("NtUserInitTask", 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12);
    RST(r, 0xC0000001);
    r = (ULONG)SYS("NtUserInitTask", ~0ULL, ~0ULL, 0xFFFF800000000000ULL, 0xFFFF800000000000ULL, ~0ULL, ~0ULL, ~0ULL, ~0ULL, ~0ULL, ~0ULL, ~0ULL, ~0ULL);
    RST(r, 0xC0000001);
}
TEST(r2_win32k_process_lifecycle_quotas_guard_inittask_does_not_disturb_thread) {
    R2_NEED_WIN();
    REQUIRE(r2_w32u("NtUserInitTask"));
    void *pti0 = NULL, *pti1 = NULL; k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &pti0);
    (void)SYS0("NtUserInitTask");
    k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &pti1);
    CHECK(pti0 && pti0 == pti1);
    HWND w = r2_window(); CHECK(w != NULL); if (w) { CHECK(U(IsWindow)(w)); U(DestroyWindow)(w); }
}
TEST(r2_win32k_process_lifecycle_quotas_guard_setthreadstate_invalid_flag_bits) {
    R2_NEED_WIN();
    REQUIRE(r2_w32u("NtUserSetThreadState"));
    static const ULONG bad[] = { 1, 2, 0x4, 0x8, 0x100, 0x2000, 0x8000, 0x10000, 0x80000000, 0xFFFFFFFF, 0xFFFFBFFF, 0x4001, 0xC000 };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(0x7777);
        ULONG_PTR r = SYS("NtUserSetThreadState", bad[i], 0xFFFFFFFF, 0,0,0,0,0,0,0,0,0,0);
        /* any bit outside 0x4000 is rejected with the raw status as the last error; the result is a C++ bool (al) */
        CHECKF((r & 0xFF) == 0, "flags 0x%lx returned 0x%llx", (unsigned long)bad[i], (unsigned long long)r);
        CHECKF(GetLastError() == 0xC000000D, "flags 0x%lx last error 0x%lx", (unsigned long)bad[i], GetLastError());
    }
}
TEST(r2_win32k_process_lifecycle_quotas_guard_setthreadstate_valid_flags_succeed) {
    R2_NEED_WIN();
    REQUIRE(r2_w32u("NtUserSetThreadState"));
    static const ULONG ok[] = { 0, 0x4000 };
    for (unsigned i = 0; i < 2; i++) {
        SetLastError(0x7777);
        /* a zero mask makes (old ^ flags) & mask ^ old == old: the call can succeed without changing the state */
        ULONG_PTR r = SYS("NtUserSetThreadState", ok[i], 0, 0,0,0,0,0,0,0,0,0,0);
        CHECKF((r & 0xFF) == 1, "flags 0x%lx returned 0x%llx", (unsigned long)ok[i], (unsigned long long)r);
        CHECKF(GetLastError() == 0x7777, "flags 0x%lx last error 0x%lx", (unsigned long)ok[i], GetLastError());
    }
}
TEST(r2_win32k_process_lifecycle_quotas_guard_setthreadstate_bad_flags_ignore_mask) {
    R2_NEED_WIN();
    REQUIRE(r2_w32u("NtUserSetThreadState"));
    static const ULONG masks[] = { 0, 0x4000, 0xFFFFFFFF };
    for (unsigned i = 0; i < 3; i++) {
        SetLastError(0);
        ULONG_PTR r = SYS("NtUserSetThreadState", 0x20, masks[i], 0,0,0,0,0,0,0,0,0,0);
        CHECK((r & 0xFF) == 0); CHECK(GetLastError() == 0xC000000D);
    }
}
TEST(r2_win32k_process_lifecycle_quotas_guard_setthreadstate_upper_bits_of_flags_count) {
    R2_NEED_WIN();
    REQUIRE(r2_w32u("NtUserSetThreadState"));
    /* the parameter is a 32-bit uint: bit 32 of the register is discarded, the low dword 0x4000 is valid */
    SetLastError(0x7777);
    ULONG_PTR r = SYS("NtUserSetThreadState", 0xABCD000000004000ULL, 0, 0,0,0,0,0,0,0,0,0,0);
    CHECK((r & 0xFF) == 1);
    CHECKF(GetLastError() == 0x7777, "last error 0x%lx", GetLastError());
}
TEST(r2_win32k_process_lifecycle_quotas_guard_lifecycle_syscalls_do_not_change_counts) {
    R2_NEED_WIN();
    DWORD g = r2_gur(0), u = r2_gur(1);
    (void)SYS("NtUserQueryInformationThread", (ULONG_PTR)-2, 0, 0, 0, 0,0,0,0,0,0,0,0);
    (void)SYS("NtUserNotifyProcessCreate", GetCurrentProcessId(), 0, 0, 1, 0,0,0,0,0,0,0,0);
    (void)SYS("NtUserRemoteConnect", 0, 0, 0, 0,0,0,0,0,0,0,0,0);
    (void)SYS0("NtUserInitTask");
    (void)SYS("NtUserSetThreadState", 1, 0, 0,0,0,0,0,0,0,0,0,0);
    EXPECT_EQ(r2_gur(0), g); EXPECT_EQ(r2_gur(1), u);
}
