/* win32k_mitigation / policy: ProcessSystemCallDisablePolicy set/get protocol.
 *
 * Kernel facts (Windows 10 19041 analysis):
 *  - kernelbase!SetProcessMitigationPolicy(4, &flags, 4) -> NtSetInformationProcess(-1, 0x34, {4, flags}, 8).
 *    Sizes other than 4 and unknown policy numbers fail inside kernelbase with STATUS_INVALID_PARAMETER;
 *    policy 0 (DEP) fails there with STATUS_NOT_SUPPORTED. BaseSetLastNTError maps to 87 / 50.
 *  - ntoskrnl!NtSetInformationProcess case 0x34: length != 8 -> STATUS_INFO_LENGTH_MISMATCH; a handle other
 *    than -1 is only allowed for policy 2 (else STATUS_INVALID_PARAMETER); policy numbers the switch does not
 *    handle (0, 5, >0x11 ...) -> STATUS_NOT_SUPPORTED. Policy 4: (qword & 0xfffffffc00000000) -> STATUS_INVALID_PARAMETER;
 *    flags bit0 and bit1 together drop the audit bit; clearing an already set disallow (EPROCESS+0x9d0 & 0x1000)
 *    or audit (0x2000) bit -> STATUS_ACCESS_DENIED; enabling either on a process whose calling thread is already
 *    a GUI thread (PsIsGuiThread) -> STATUS_ALREADY_WIN32; disallow sets 0x3000, audit only 0x2000.
 *  - NtQueryInformationProcess case 0x34 policy 4 reports bit0 if 0x1000 else bit1 if 0x2000; a non -1 handle
 *    needs PROCESS_QUERY_LIMITED_INFORMATION (0x1000) on a process object. */
#include "mit.h"

/* ================================================================================================ */
/* Scenarios (children)                                                                             */
/* ================================================================================================ */

SCN(readback_flags) {
    DWORD fl = ~0UL; mit_scd s;
    MC(mit_get_scd(GetCurrentProcess(), &fl));
    MEQ(fl, 1);                      /* disallow reported alone: bit1 only shows when 0x1000 is clear */
    s.Flags = fl;
    MC(s.Disallow == 1); MC(s.Audit == 0); MC(s.Reserved == 0);
}
SCN(readback_real_handle) {
    DWORD fl = ~0UL;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentProcessId());
    MC(h != NULL);
    if (!h) return;
    MC(mit_get_scd(h, &fl));         /* handle path of NtQueryInformationProcess 0x34 (access 0x1000) */
    MEQ(fl, 1);
    CloseHandle(h);
}
SCN(readback_nt_raw) {
    DWORD fl = ~0UL;
    MST(mit_nt_get((HANDLE)-1, MIT_POLICY_SCD, &fl), ST_SUCCESS);
    MEQ(fl, 1);
    HANDLE h = mit_open_self(PROCESS_QUERY_LIMITED_INFORMATION);
    fl = ~0UL;
    MST(mit_nt_get(h, MIT_POLICY_SCD, &fl), ST_SUCCESS);
    MEQ(fl, 1);
    if (h) CloseHandle(h);
}
SCN(readback_len_mismatch) {
    static const DWORD lens[] = { 0, 4, 7, 9, 12, 16 };
    for (unsigned i = 0; i < sizeof lens / sizeof *lens; i++)
        MCF(mit_nt_get_len((HANDLE)-1, MIT_POLICY_SCD, lens[i]) == (LONG)ST_INFO_LENGTH_MISMATCH, "len %lu", lens[i]);
}
SCN(readback_other_policies) {
    typedef BOOL (WINAPI *getmit_fn)(HANDLE, int, PVOID, SIZE_T);
    getmit_fn g = (getmit_fn)mit_k32("GetProcessMitigationPolicy");
    if (!g) { printf("  SKIP no export\n"); return; }
    DWORD b[2];
    static const int pol[] = { 1, 2, 3, 6, 7, 8 };   /* ASLR, DynamicCode, StrictHandle, ExtensionPoint, CFG, Signature: size 4 */
    for (unsigned i = 0; i < sizeof pol / sizeof *pol; i++) { b[0] = 0; MCF(g(GetCurrentProcess(), pol[i], b, 4), "policy %d err %lu", pol[i], GetLastError()); }
    MC(g(GetCurrentProcess(), 0, b, 8));                        /* DEP: 8 bytes */
    SetLastError(0); MC(!g(GetCurrentProcess(), 99, b, 4)); MERR(ERROR_INVALID_PARAMETER);
    SetLastError(0); MC(!g(GetCurrentProcess(), MIT_POLICY_SCD, b, 8)); MERR(ERROR_INVALID_PARAMETER);   /* kernelbase wants size 4 */
    SetLastError(0); MC(!g(GetCurrentProcess(), MIT_POLICY_SCD, b, 0)); MERR(ERROR_INVALID_PARAMETER);
}
SCN(cannot_clear_disallow) {
    DWORD fl = ~0UL;
    SetLastError(0); MC(!mit_set_scd(0)); MERR(ERROR_ACCESS_DENIED);    /* 0x1000 set, new disallow bit 0 -> STATUS_ACCESS_DENIED */
    MST(mit_nt_set((HANDLE)-1, MIT_POLICY_SCD, 0), ST_ACCESS_DENIED);
    MC(mit_get_scd(GetCurrentProcess(), &fl)); MEQ(fl, 1);
}
SCN(cannot_downgrade_to_audit) {
    DWORD fl = ~0UL;
    MST(mit_nt_set((HANDLE)-1, MIT_POLICY_SCD, MIT_SCD_AUDIT), ST_ACCESS_DENIED);   /* disallow bit 0 with 0x1000 set is rejected first */
    SetLastError(0); MC(!mit_set_scd(MIT_SCD_AUDIT)); MERR(ERROR_ACCESS_DENIED);
    MC(mit_get_scd(GetCurrentProcess(), &fl)); MEQ(fl, 1);
}
SCN(set_idempotent) {
    DWORD fl = ~0UL;
    MC(mit_set_scd(MIT_SCD_DISALLOW));
    MST(mit_nt_set((HANDLE)-1, MIT_POLICY_SCD, MIT_SCD_DISALLOW), ST_SUCCESS);
    MC(mit_get_scd(GetCurrentProcess(), &fl)); MEQ(fl, 1);
}
SCN(set_both_bits) {
    DWORD fl = ~0UL;
    MC(mit_set_scd(MIT_SCD_DISALLOW | MIT_SCD_AUDIT));   /* audit bit is dropped when disallow is requested too */
    MC(mit_get_scd(GetCurrentProcess(), &fl)); MEQ(fl, 1);
}
SCN(set_reserved_bits) {
    static const DWORD bad[] = { 4, 8, 0x10, 0x100, 0x10000, 0x80000000, 0xFFFFFFFC, 0xFFFFFFFF, 5, 7 };
    DWORD fl = ~0UL;
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        MCF(mit_nt_set((HANDLE)-1, MIT_POLICY_SCD, bad[i]) == (LONG)ST_INVALID_PARAMETER, "flags 0x%lx", bad[i]);
        SetLastError(0); MCF(!mit_set_scd(bad[i]), "flags 0x%lx", bad[i]); MERR(ERROR_INVALID_PARAMETER);
    }
    MC(mit_get_scd(GetCurrentProcess(), &fl)); MEQ(fl, 1);
}
SCN(set_remote_handle) {
    HANDLE h = mit_open_self(PROCESS_ALL_ACCESS);
    MC(h != NULL);
    if (!h) return;
    MST(mit_nt_set(h, MIT_POLICY_SCD, MIT_SCD_DISALLOW), ST_INVALID_PARAMETER);   /* a handle != -1 only works for policy 2 */
    MST(mit_nt_set(h, MIT_POLICY_SCD, 0), ST_INVALID_PARAMETER);
    CloseHandle(h);
}
SCN(set_bad_calls_kernelbase) {
    typedef BOOL (WINAPI *setmit_fn)(int, PVOID, SIZE_T);
    setmit_fn s = (setmit_fn)mit_k32("SetProcessMitigationPolicy");
    DWORD f = 1;
    if (!s) { printf("  SKIP no export\n"); return; }
    SetLastError(0); MC(!s(MIT_POLICY_SCD, &f, 8)); MERR(ERROR_INVALID_PARAMETER);   /* size must be exactly 4 */
    SetLastError(0); MC(!s(MIT_POLICY_SCD, &f, 0)); MERR(ERROR_INVALID_PARAMETER);
    SetLastError(0); MC(!s(99, &f, 4));  MERR(ERROR_INVALID_PARAMETER);              /* unknown policy */
    SetLastError(0); MC(!s(0, &f, 4));   MERR(ERROR_NOT_SUPPORTED);                  /* DEP: STATUS_NOT_SUPPORTED */
}
SCN(win32k_filter_policy) {
    typedef BOOL (WINAPI *getmit_fn)(HANDLE, int, PVOID, SIZE_T);
    getmit_fn g = (getmit_fn)mit_k32("GetProcessMitigationPolicy");
    DWORD v = ~0UL;
    if (!g) { printf("  SKIP no export\n"); return; }
    MCF(g(GetCurrentProcess(), 11 /* ProcessSystemCallFilterPolicy */, &v, 4), "err %lu", GetLastError());
    t_note("ProcessSystemCallFilterPolicy readback in a Win32k-locked process: 0x%lx", v);
    MC((v & ~0xFUL) == 0);   /* NtQueryInformationProcess case 0xb stores only PsGetWin32KFilterSet() & 0xf */
}
SCN(policy_struct_set) {
    mit_scd s; s.Flags = 0; s.Disallow = 1;
    MEQ(s.Flags, 1);
    MC(mit_set_scd(s.Flags));                    /* bitfield-built value behaves like the raw one */
    s.Flags = 0xFFFFFFFF; MEQ(s.Disallow, 1); MEQ(s.Audit, 1); MEQ(s.Reserved, 0x3FFFFFFF);
}

MIT_LOCKED2(policy_readback_flags, "readback_flags", "")
MIT_LOCKED2(policy_readback_real_handle, "readback_real_handle", "")
MIT_LOCKED2(policy_readback_nt_raw, "readback_nt_raw", "")
MIT_LOCKED2(policy_readback_len_mismatch, "readback_len_mismatch", "")
MIT_LOCKED2(policy_readback_other_policies, "readback_other_policies", "")
MIT_LOCKED2(policy_cannot_clear_disallow, "cannot_clear_disallow", "")
MIT_LOCKED2(policy_cannot_downgrade_to_audit, "cannot_downgrade_to_audit", "")
MIT_LOCKED2(policy_set_idempotent, "set_idempotent", "")
MIT_LOCKED2(policy_set_both_bits, "set_both_bits", "")
MIT_LOCKED2(policy_set_reserved_bits, "set_reserved_bits", "")
MIT_LOCKED2(policy_set_remote_handle, "set_remote_handle", "")
MIT_LOCKED2(policy_set_bad_calls_kernelbase, "set_bad_calls_kernelbase", "")
MIT_LOCKED2(policy_win32k_filter_policy, "win32k_filter_policy", "")
MIT_LOCKED2(policy_struct_set, "policy_struct_set", "")

/* ================================================================================================ */
/* In-process tests (only calls that cannot change this process: failures and no-ops)                */
/* ================================================================================================ */

TEST(win32k_mitigation_struct_layout) {
    mit_scd s;
    CHECK(sizeof(mit_scd) == 4);
    s.Flags = 0; s.Disallow = 1; EXPECT_EQ(s.Flags, 1);       /* DisallowWin32kSystemCalls is bit 0 (winnt.h) */
    s.Flags = 0; s.Audit = 1;    EXPECT_EQ(s.Flags, 2);       /* AuditDisallowWin32kSystemCalls is bit 1 (NtQueryInformationProcess case 4) */
    s.Flags = 0; s.Reserved = 1; EXPECT_EQ(s.Flags, 4);
    PROCESS_MITIGATION_SYSTEM_CALL_DISABLE_POLICY sdk; memset(&sdk, 0, sizeof sdk);
    EXPECT_EQ(sizeof sdk, 4);
    sdk.DisallowWin32kSystemCalls = 1; EXPECT_EQ(sdk.Flags, 1);
}
TEST(win32k_mitigation_creation_constants) {
    EXPECT_EQ(PROCESS_CREATION_MITIGATION_POLICY_WIN32K_SYSTEM_CALL_DISABLE_ALWAYS_ON, MIT_CREATE_ON);
    EXPECT_EQ(PROCESS_CREATION_MITIGATION_POLICY_WIN32K_SYSTEM_CALL_DISABLE_ALWAYS_OFF, MIT_CREATE_OFF);
    EXPECT_EQ(PROCESS_CREATION_MITIGATION_POLICY_WIN32K_SYSTEM_CALL_DISABLE_RESERVED, MIT_CREATE_RESERVED);
    EXPECT_EQ(PROCESS_CREATION_MITIGATION_POLICY_WIN32K_SYSTEM_CALL_DISABLE_DEFER, 0);
    EXPECT_EQ(PROCESS_CREATION_MITIGATION_POLICY_WIN32K_SYSTEM_CALL_DISABLE_MASK, 0x30000000);
}
TEST(win32k_mitigation_plain_readback_zero) {
    MIT_WINE_SKIP();
    DWORD fl = ~0UL;
    REQUIRE(mit_k32("GetProcessMitigationPolicy"));
    CHECK(mit_get_scd(GetCurrentProcess(), &fl));
    EXPECT_EQ(fl, 0);                                /* this test exe is not locked down */
}
TEST(win32k_mitigation_plain_readback_nt_raw) {
    MIT_WINE_SKIP();
    DWORD fl = ~0UL;
    REQUIRE(mit_nt("NtQueryInformationProcess"));
    EXPECT_EQ((ULONG)mit_nt_get((HANDLE)-1, MIT_POLICY_SCD, &fl), ST_SUCCESS);
    EXPECT_EQ(fl, 0);
}
TEST(win32k_mitigation_plain_readback_is_gui_neutral) {
    MIT_WINE_SKIP();
    /* Reading the policy is not a win32k call and must not convert the thread. */
    unsigned long long before = mit_teb64(TEB_Win32ThreadInfo);
    DWORD fl; mit_get_scd(GetCurrentProcess(), &fl);
    EXPECT_EQ(mit_teb64(TEB_Win32ThreadInfo), before);
}
TEST(win32k_mitigation_plain_noop_clear_succeeds) {
    MIT_WINE_SKIP();
    /* disallow=0, audit=0 on a process with neither bit: case 4 falls through to STATUS_SUCCESS without any
     * PsIsGuiThread test, so this is a harmless no-op even from a GUI thread. */
    REQUIRE(mit_k32("SetProcessMitigationPolicy"));
    CHECK(mit_set_scd(0));
    DWORD fl = ~0UL; mit_get_scd(GetCurrentProcess(), &fl);
    EXPECT_EQ(fl, 0);
}
TEST(win32k_mitigation_plain_noop_clear_nt_status) {
    MIT_WINE_SKIP();
    REQUIRE(mit_nt("NtSetInformationProcess"));
    EXPECT_EQ((ULONG)mit_nt_set((HANDLE)-1, MIT_POLICY_SCD, 0), ST_SUCCESS);
}
TEST(win32k_mitigation_plain_set_wrong_length) {
    MIT_WINE_SKIP();
    REQUIRE(mit_nt("NtSetInformationProcess"));
    typedef LONG (WINAPI *ntset_fn)(HANDLE, ULONG, PVOID, ULONG);
    ntset_fn f = (ntset_fn)mit_nt("NtSetInformationProcess");
    DWORD buf[4] = { MIT_POLICY_SCD, 0, 0, 0 };
    static const ULONG lens[] = { 0, 4, 7, 9, 12, 16 };
    for (unsigned i = 0; i < sizeof lens / sizeof *lens; i++)
        CHECKF((ULONG)f((HANDLE)-1, 0x34, buf, lens[i]) == ST_INFO_LENGTH_MISMATCH, "len %lu", lens[i]);
}
TEST(win32k_mitigation_plain_query_wrong_length) {
    MIT_WINE_SKIP();
    REQUIRE(mit_nt("NtQueryInformationProcess"));
    static const ULONG lens[] = { 0, 4, 7, 9, 16 };
    for (unsigned i = 0; i < sizeof lens / sizeof *lens; i++)
        CHECKF((ULONG)mit_nt_get_len((HANDLE)-1, MIT_POLICY_SCD, lens[i]) == ST_INFO_LENGTH_MISMATCH, "len %lu", lens[i]);
}
#define RESERVED_FLAGS_TEST(tag, val) \
    TEST(win32k_mitigation_plain_set_reserved_##tag) { \
        MIT_WINE_SKIP(); \
        REQUIRE(mit_nt("NtSetInformationProcess")); \
        EXPECT_EQ((ULONG)mit_nt_set((HANDLE)-1, MIT_POLICY_SCD, val), ST_INVALID_PARAMETER); /* (qword & 0xfffffffc00000000) != 0 */ \
        DWORD fl = ~0UL; mit_get_scd(GetCurrentProcess(), &fl); EXPECT_EQ(fl, 0); \
    }
RESERVED_FLAGS_TEST(bit2, 0x4)
RESERVED_FLAGS_TEST(bit3, 0x8)
RESERVED_FLAGS_TEST(bit4, 0x10)
RESERVED_FLAGS_TEST(bit8, 0x100)
RESERVED_FLAGS_TEST(bit16, 0x10000)
RESERVED_FLAGS_TEST(bit31, 0x80000000)
RESERVED_FLAGS_TEST(disallow_plus_bit2, 0x5)
RESERVED_FLAGS_TEST(audit_plus_bit2, 0x6)
RESERVED_FLAGS_TEST(all_but_low2, 0xFFFFFFFC)
RESERVED_FLAGS_TEST(all_ones, 0xFFFFFFFF)

TEST(win32k_mitigation_plain_set_reserved_kernelbase) {
    MIT_WINE_SKIP();
    REQUIRE(mit_k32("SetProcessMitigationPolicy"));
    SET_ERR(0); CHECK(!mit_set_scd(0x4)); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SET_ERR(0); CHECK(!mit_set_scd(0xFFFFFFFF)); EXPECT_ERR(ERROR_INVALID_PARAMETER);
}
TEST(win32k_mitigation_plain_set_unsupported_policies) {
    MIT_WINE_SKIP();
    REQUIRE(mit_nt("NtSetInformationProcess"));
    /* The case 0x34 switch has no arm for these numbers: STATUS_NOT_SUPPORTED. */
    static const DWORD pol[] = { 0, 5, 99, 0x1000 };
    for (unsigned i = 0; i < sizeof pol / sizeof *pol; i++)
        CHECKF((ULONG)mit_nt_set((HANDLE)-1, pol[i], 0) == ST_NOT_SUPPORTED, "policy %lu", pol[i]);
}
TEST(win32k_mitigation_plain_set_remote_handle) {
    MIT_WINE_SKIP();
    HANDLE h = mit_open_self(PROCESS_ALL_ACCESS);
    REQUIRE(h);
    REQUIRE(mit_nt("NtSetInformationProcess"));
    EXPECT_EQ((ULONG)mit_nt_set(h, MIT_POLICY_SCD, MIT_SCD_DISALLOW), ST_INVALID_PARAMETER);   /* never reaches the lockdown arm */
    EXPECT_EQ((ULONG)mit_nt_set(h, MIT_POLICY_SCD, MIT_SCD_AUDIT), ST_INVALID_PARAMETER);
    EXPECT_EQ((ULONG)mit_nt_set(h, MIT_POLICY_SCD, 0), ST_INVALID_PARAMETER);
    DWORD fl = ~0UL; mit_get_scd(GetCurrentProcess(), &fl); EXPECT_EQ(fl, 0);
    CloseHandle(h);
}
TEST(win32k_mitigation_plain_set_kernelbase_bad_size) {
    MIT_WINE_SKIP();
    REQUIRE(mit_k32("SetProcessMitigationPolicy"));
    typedef BOOL (WINAPI *setmit_fn)(int, PVOID, SIZE_T);
    setmit_fn s = (setmit_fn)mit_k32("SetProcessMitigationPolicy");
    DWORD f = 0;
    SET_ERR(0); CHECK(!s(MIT_POLICY_SCD, &f, 8)); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SET_ERR(0); CHECK(!s(MIT_POLICY_SCD, &f, 0)); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SET_ERR(0); CHECK(!s(MIT_POLICY_SCD, &f, 2)); EXPECT_ERR(ERROR_INVALID_PARAMETER);
}
TEST(win32k_mitigation_plain_set_kernelbase_bad_policy) {
    MIT_WINE_SKIP();
    REQUIRE(mit_k32("SetProcessMitigationPolicy"));
    typedef BOOL (WINAPI *setmit_fn)(int, PVOID, SIZE_T);
    setmit_fn s = (setmit_fn)mit_k32("SetProcessMitigationPolicy");
    DWORD f = 0;
    SET_ERR(0); CHECK(!s(99, &f, 4)); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SET_ERR(0); CHECK(!s(-1, &f, 4)); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SET_ERR(0); CHECK(!s(0, &f, 4)); EXPECT_ERR(ERROR_NOT_SUPPORTED);          /* ProcessDEPPolicy has no setter */
}
TEST(win32k_mitigation_plain_get_kernelbase_bad_args) {
    MIT_WINE_SKIP();
    REQUIRE(mit_k32("GetProcessMitigationPolicy"));
    typedef BOOL (WINAPI *getmit_fn)(HANDLE, int, PVOID, SIZE_T);
    getmit_fn g = (getmit_fn)mit_k32("GetProcessMitigationPolicy");
    DWORD v[2];
    SET_ERR(0); CHECK(!g(GetCurrentProcess(), MIT_POLICY_SCD, v, 8)); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SET_ERR(0); CHECK(!g(GetCurrentProcess(), MIT_POLICY_SCD, v, 3)); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SET_ERR(0); CHECK(!g(GetCurrentProcess(), 99, v, 4)); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SET_ERR(0); CHECK(!g(GetCurrentProcess(), -3, v, 4)); EXPECT_ERR(ERROR_INVALID_PARAMETER);
}
TEST(win32k_mitigation_plain_get_invalid_handle) {
    MIT_WINE_SKIP();
    REQUIRE(mit_k32("GetProcessMitigationPolicy"));
    DWORD fl;
    SET_ERR(0); CHECK(!mit_get_scd((HANDLE)(ULONG_PTR)0x7FFFFFF0, &fl));
    EXPECT_ERR(ERROR_INVALID_HANDLE);                    /* ObReferenceObjectByHandleWithTag -> STATUS_INVALID_HANDLE */
    EXPECT_EQ((ULONG)mit_nt_get((HANDLE)(ULONG_PTR)0x7FFFFFF0, MIT_POLICY_SCD, &fl), ST_INVALID_HANDLE);
}
TEST(win32k_mitigation_plain_get_wrong_object_type) {
    MIT_WINE_SKIP();
    REQUIRE(mit_k32("GetProcessMitigationPolicy"));
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    DWORD fl;
    REQUIRE(ev);
    SET_ERR(0); CHECK(!mit_get_scd(ev, &fl));
    EXPECT_ERR(ERROR_INVALID_HANDLE);                    /* PsProcessType mismatch -> STATUS_OBJECT_TYPE_MISMATCH -> 6 */
    EXPECT_EQ((ULONG)mit_nt_get(ev, MIT_POLICY_SCD, &fl), 0xC0000024UL);
    CloseHandle(ev);
}
TEST(win32k_mitigation_plain_get_needs_query_limited) {
    MIT_WINE_SKIP();
    REQUIRE(mit_k32("GetProcessMitigationPolicy"));
    HANDLE h = mit_open_self(SYNCHRONIZE);
    DWORD fl;
    REQUIRE(h);
    SET_ERR(0); CHECK(!mit_get_scd(h, &fl));
    EXPECT_ERR(ERROR_ACCESS_DENIED);                     /* access 0x1000 missing -> STATUS_ACCESS_DENIED */
    EXPECT_EQ((ULONG)mit_nt_get(h, MIT_POLICY_SCD, &fl), ST_ACCESS_DENIED);
    CloseHandle(h);
    h = mit_open_self(PROCESS_QUERY_LIMITED_INFORMATION);
    REQUIRE(h);
    fl = ~0UL; CHECK(mit_get_scd(h, &fl)); EXPECT_EQ(fl, 0);
    CloseHandle(h);
}
TEST(win32k_mitigation_plain_get_pseudo_handles_agree) {
    MIT_WINE_SKIP();
    REQUIRE(mit_k32("GetProcessMitigationPolicy"));
    HANDLE h = mit_open_self(PROCESS_QUERY_LIMITED_INFORMATION);
    DWORD a = 1, b = 2, c = 3;
    REQUIRE(h);
    CHECK(mit_get_scd(GetCurrentProcess(), &a));
    CHECK(mit_get_scd(h, &b));
    CHECK(mit_get_scd((HANDLE)(LONG_PTR)-1, &c));
    EXPECT_EQ(a, b); EXPECT_EQ(b, c);
    CloseHandle(h);
}
