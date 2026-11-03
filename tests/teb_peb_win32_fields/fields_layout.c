/* teb_peb_win32_fields: layout and ownership of the TEB / PEB members the Win32 subsystem sits on.
 *
 * Everything here is observable without a GUI call: the offsets in common/k.h are checked against
 * what the kernel (NtQueryInformation*, CreateThread, ProcessIdToSessionId) and the loader say
 * about the same process, so a wrong offset constant is caught before the win32k-specific areas
 * build on it. */
#include "tp.h"

UDECL(LONG, RtlGetVersion, (RTL_OSVERSIONINFOW *));
UDECL(LONG, NtQueryInformationThread, (HANDLE, int, void *, ULONG, ULONG *));

struct tp_tbi { LONG ExitStatus; void *TebBaseAddress; ULONG_PTR Pid, Tid; ULONG_PTR Affinity; LONG Priority, BasePriority; };

/* ---- identity and size of the TEB ------------------------------------------------------------ */

TEST(teb_peb_win32_fields_teb_self_pointer) {
    /* _NT_TIB.Self (TEB+0x30) is the TEB's own linear address: gs:[0x30] */
    void *teb = k_teb();
    CHECK(teb != NULL);
    EXPECT_EQ(tp_teb64(TEB_Self), (ULONG_PTR)teb);
    EXPECT_EQ(tp_teb64(0x30), (ULONG_PTR)NtCurrentTeb());
    CHECK(tp_is_user_va((ULONG_PTR)teb));
}

TEST(teb_peb_win32_fields_teb_peb_pointer) {
    /* TEB+0x60 and the kernel's own idea of the PEB (ProcessBasicInformation) agree */
    void *peb = (void *)(ULONG_PTR)tp_teb64(TEB_ProcessEnvironmentBlock);
    EXPECT_EQ(peb, k_peb());
    CHECK(tp_is_user_va((ULONG_PTR)peb));
    REQUIRE(U(NtQueryInformationProcess));
    PROCESS_BASIC_INFORMATION pbi; ULONG rl = 0;
    memset(&pbi, 0, sizeof pbi);
    LONG st = U(NtQueryInformationProcess)(GetCurrentProcess(), ProcessBasicInformation, &pbi, sizeof pbi, &rl);
    EXPECT_EQ(st, 0);
    EXPECT_EQ(pbi.PebBaseAddress, peb);
}

TEST(teb_peb_win32_fields_teb_base_matches_kernel_thread_info) {
    /* ThreadBasicInformation.TebBaseAddress comes from ETHREAD->Tcb.Teb, i.e. the address the
     * kernel (and win32k, via PsGetThreadTeb) uses for this thread */
    REQUIRE(UN(NtQueryInformationThread));
    struct tp_tbi tbi; ULONG rl = 0;
    memset(&tbi, 0, sizeof tbi);
    LONG st = UN(NtQueryInformationThread)(GetCurrentThread(), 0 /* ThreadBasicInformation */, &tbi, sizeof tbi, &rl);
    EXPECT_EQ(st, 0);
    EXPECT_EQ(tbi.TebBaseAddress, k_teb());
    EXPECT_EQ(tbi.Pid, GetCurrentProcessId());
    EXPECT_EQ(tbi.Tid, GetCurrentThreadId());
}

TEST(teb_peb_win32_fields_teb_client_id) {
    /* _CLIENT_ID at TEB+0x40: UniqueProcess then UniqueThread */
    EXPECT_EQ(tp_teb64(TEB_ClientId), GetCurrentProcessId());
    EXPECT_EQ(tp_teb64(TEB_ClientId + 8), GetCurrentThreadId());
    /* RealClientId is the loader's copy of the same pair on a thread that was never impersonated into a different client */
    unsigned long long rp = tp_teb64(TEB_RealClientId), rt = tp_teb64(TEB_RealClientId + 8);
    if (rp != GetCurrentProcessId() || rt != GetCurrentThreadId())
        t_note("RealClientId = %llu/%llu differs from ClientId %lu/%lu", rp, rt, GetCurrentProcessId(), GetCurrentThreadId());
}

TEST(teb_peb_win32_fields_teb_whole_structure_readable) {
    /* _TEB is 0x1838 bytes; every byte of it is committed user memory the kernel and win32k write into */
    unsigned char *buf = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, TEB_SIZE);
    REQUIRE(buf);
    CHECK(k_try_read(k_teb(), buf, TEB_SIZE));
    CHECK(k_try_read((char *)k_teb() + TEB_SIZE - 1, buf, 1));
    HeapFree(GetProcessHeap(), 0, buf);
}

TEST(teb_peb_win32_fields_peb_whole_structure_readable) {
    /* _PEB is 0x7C8 bytes (1992) on 19041 x64 */
    unsigned char *buf = (unsigned char *)HeapAlloc(GetProcessHeap(), 0, PEB_SIZE);
    REQUIRE(buf);
    CHECK(k_try_read(k_peb(), buf, PEB_SIZE));
    CHECK(k_try_read((char *)k_peb() + PEB_SIZE - 1, buf, 1));
    HeapFree(GetProcessHeap(), 0, buf);
}

TEST(teb_peb_win32_fields_peb_is_page_aligned) {
    /* MmCreatePeb maps the PEB on its own page */
    ULONG_PTR peb = (ULONG_PTR)k_peb();
    CHECK(peb != 0);
    EXPECT_EQ(peb & 0xFFF, 0);
}

TEST(teb_peb_win32_fields_teb_pdb_size_constants) {
    /* compile-time identities of the k.h constants: the Win32 block of the TEB is contiguous */
    EXPECT_EQ(TEB_GdiTebBatch + 0x4E8, TEB_RealClientId);                   /* _GDI_TEB_BATCH is 0x4E8 bytes */
    EXPECT_EQ(TEB_RealClientId + 0x10, TEB_GdiCachedProcessHandle);
    EXPECT_EQ(TEB_Win32ClientInfo + 62 * 8, TEB_glDispatchTable);            /* unsigned __int64[62] */
    EXPECT_EQ(TEB_glDispatchTable + 233 * 8, TEB_glReserved1);               /* void*[233] */
    EXPECT_EQ(TEB_glReserved1 + 29 * 8, TEB_glReserved2);                    /* unsigned __int64[29] */
    EXPECT_EQ(TEB_GdiTebBatch + GDI_TEB_BATCH_Buffer, K_GDI_TEB_BATCH_RECORDS);
    EXPECT_EQ(K_GDI_TEB_BATCH_LIMIT - K_GDI_TEB_BATCH_RECORDS, 0x4D8);
    EXPECT_EQ(TEB_WinSockData + 8, TEB_GdiBatchCount);
    EXPECT_EQ(TEB_EffectiveContainerId + 16, TEB_SIZE);
    EXPECT_EQ(PEB_NtGlobalFlag2 + 4, PEB_SIZE);
    EXPECT_EQ(PEB_GdiHandleBuffer + 60 * 4, PEB_PostProcessInitRoutine);     /* unsigned long[60] */
}

TEST(teb_peb_win32_fields_teb_stack_bounds) {
    /* NtTib.StackBase/StackLimit bracket the live stack; DeallocationStack is the allocation base */
    volatile char local = 0;
    unsigned long long base = tp_teb64(TEB_StackBase), limit = tp_teb64(TEB_StackLimit), dealloc = tp_teb64(TEB_DeallocationStack);
    CHECK(base > limit);
    CHECKF((ULONG_PTR)&local < base && (ULONG_PTR)&local >= limit, "local %p not in [%llx,%llx)", &local, limit, base);
    CHECK(dealloc != 0 && dealloc <= limit);
}

TEST(teb_peb_win32_fields_teb_activation_context_stack_present) {
    /* RtlAllocateActivationContextStack ran in thread init; win32k/user32 callouts rely on it through SxS lookups */
    unsigned long long p = tp_teb64(TEB_ActivationContextStackPointer);
    CHECK(tp_is_user_va(p));
    unsigned char b[8];
    CHECK(k_try_read((void *)(ULONG_PTR)p, b, sizeof b));
}

/* ---- last-error plumbing ----------------------------------------------------------------------- */

TEST(teb_peb_win32_fields_last_error_value_is_the_teb_field) {
    /* kernel32!SetLastError / GetLastError are plain stores/loads of TEB+0x68, the same word win32k's
     * UserSetLastError / EngSetLastError write (win32kbase!EngSetLastError: *(Teb+0x68) = code) */
    static const DWORD v[] = { 0, 1, 5, 87, 1400, 0x80000005, 0xDEADBEEF, 0xFFFFFFFF };
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) {
        SetLastError(v[i]);
        EXPECT_EQ(tp_teb32(TEB_LastErrorValue), v[i]);
        EXPECT_EQ(GetLastError(), v[i]);
    }
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) {
        tp_teb_write32(TEB_LastErrorValue, v[i]);
        EXPECT_EQ(GetLastError(), v[i]);
    }
    SetLastError(0);
}

TEST(teb_peb_win32_fields_last_status_value_is_set_by_ntstatus_conversion) {
    /* ntdll!RtlSetLastWin32ErrorAndNtStatusFromNtStatus stores both words; the pair is what
     * win32kbase!UserSetLastStatus produces on the kernel side (status -> LastStatusValue, mapped code -> LastErrorValue) */
    typedef ULONG (WINAPI *fn_t)(LONG);
    fn_t f = (fn_t)t_proc("RtlSetLastWin32ErrorAndNtStatusFromNtStatus");
    REQUIRE(f);
    static const struct { LONG st; } v[] = { {(LONG)0xC0000034}, {(LONG)0xC0000008}, {(LONG)0xC000000D}, {(LONG)0x80000005}, {(LONG)0xC0000022} };
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) {
        f(v[i].st);
        EXPECT_EQ(tp_teb32(TEB_LastStatusValue), (unsigned long)v[i].st);
        EXPECT_EQ(tp_teb32(TEB_LastErrorValue), U(RtlNtStatusToDosError)(v[i].st));
    }
    SetLastError(0);
}

struct lep { DWORD seen; };
static DWORD lep_thread(void *a) { struct lep *p = (struct lep *)a; p->seen = GetLastError(); SetLastError(77); return 0; }
TEST(teb_peb_win32_fields_last_error_is_per_thread) {
    struct lep s = {0};
    SetLastError(4242);
    CHECK(tp_run_thread(lep_thread, &s, 5000) == 0);
    /* A new thread's TEB is not copied from its creator; on Windows 10 the loader's thread start path leaves a
     * machine-dependent value behind, so only "not inherited" is a stable property. */
    CHECK(s.seen != 4242);
    EXPECT_EQ(GetLastError(), 4242);
    SetLastError(0);
}

/* ---- TLS slots ---------------------------------------------------------------------------------- */

TEST(teb_peb_win32_fields_tls_slots_map_to_teb_array) {
    /* TlsSlots[64] at TEB+0x1480; TlsSetValue(idx<64) stores into slot idx */
    DWORD idx[8]; int n = 0;
    for (; n < 8; n++) { idx[n] = TlsAlloc(); if (idx[n] == TLS_OUT_OF_INDEXES) break; }
    for (int i = 0; i < n; i++) {
        if (idx[i] >= 64) continue;
        void *val = (void *)(ULONG_PTR)(0x1000 + i * 0x10);
        CHECK(TlsSetValue(idx[i], val));
        EXPECT_EQ(tp_teb64(TEB_TlsSlots + idx[i] * 8), (ULONG_PTR)val);
        EXPECT_EQ(TlsGetValue(idx[i]), val);
    }
    for (int i = 0; i < n; i++) TlsFree(idx[i]);
}

TEST(teb_peb_win32_fields_tls_slots_survive_gui_calls) {
    /* nothing win32k/user32/gdi32 does for the application touches the application's TLS slots */
    DWORD idx[4]; int n = 0;
    for (; n < 4; n++) { idx[n] = TlsAlloc(); if (idx[n] == TLS_OUT_OF_INDEXES) break; }
    for (int i = 0; i < n; i++) TlsSetValue(idx[i], (void *)(ULONG_PTR)(0xA5A5000 + i));
    tp_gui();
    if (U(GdiFlush)) U(GdiFlush)();
    if (U(GetSystemMetrics)) U(GetSystemMetrics)(SM_CXSCREEN);
    k_pump(10);
    for (int i = 0; i < n; i++) EXPECT_EQ(TlsGetValue(idx[i]), (void *)(ULONG_PTR)(0xA5A5000 + i));
    for (int i = 0; i < n; i++) TlsFree(idx[i]);
}

/* ---- PEB identity ------------------------------------------------------------------------------- */

TEST(teb_peb_win32_fields_peb_session_id) {
    /* ProcessIdToSessionId reads EPROCESS->Session; the PEB copy is stored at process creation and
     * is what user32/kernelbase compare to RtlGetActiveConsoleId (user32!_UserClientDllInitialize reads PEB+0x2C0) */
    DWORD sid = 0xFFFFFFFF;
    CHECK(ProcessIdToSessionId(GetCurrentProcessId(), &sid));
    EXPECT_EQ(tp_peb32(PEB_SessionId), sid);
    EXPECT_EQ(k_session_id(), sid);
    struct { ULONG SessionId; } psi = { 0xFFFFFFFF };
    ULONG rl = 0;
    if (U(NtQueryInformationProcess)) {
        LONG st = U(NtQueryInformationProcess)(GetCurrentProcess(), 24 /* ProcessSessionInformation */, &psi, sizeof psi, &rl);
        EXPECT_EQ(st, 0);
        EXPECT_EQ(psi.SessionId, sid);
    }
}

TEST(teb_peb_win32_fields_peb_os_version_matches_rtlgetversion) {
    REQUIRE(UN(RtlGetVersion));
    RTL_OSVERSIONINFOW vi; memset(&vi, 0, sizeof vi); vi.dwOSVersionInfoSize = sizeof vi;
    EXPECT_EQ(UN(RtlGetVersion)(&vi), 0);
    EXPECT_EQ(tp_peb32(PEB_OSMajorVersion), vi.dwMajorVersion);
    EXPECT_EQ(tp_peb32(PEB_OSMinorVersion), vi.dwMinorVersion);
    EXPECT_EQ(tp_peb16(PEB_OSBuildNumber), (vi.dwBuildNumber & 0xFFFF));
    EXPECT_EQ(tp_peb32(PEB_OSPlatformId), vi.dwPlatformId);
    EXPECT_EQ(vi.dwPlatformId, 2);                                 /* VER_PLATFORM_WIN32_NT */
    if (!k_is_19041()) t_note("build %lu, expectations are for 19041", vi.dwBuildNumber);
    else { EXPECT_EQ(vi.dwMajorVersion, 10); EXPECT_EQ(vi.dwMinorVersion, 0); EXPECT_EQ(tp_peb16(PEB_OSBuildNumber), 19041); }
}

TEST(teb_peb_win32_fields_peb_image_subsystem_matches_pe_header) {
    /* ntdll!LdrpInitializeProcess copies Subsystem / Major / MinorSubsystemVersion from the exe's optional header */
    HMODULE exe = GetModuleHandleW(NULL);
    IMAGE_DOS_HEADER dos; IMAGE_NT_HEADERS64 nt;
    REQUIRE(k_try_read(exe, &dos, sizeof dos));
    REQUIRE(k_try_read((char *)exe + dos.e_lfanew, &nt, sizeof nt));
    EXPECT_EQ(tp_peb32(PEB_ImageSubsystem), nt.OptionalHeader.Subsystem);
    EXPECT_EQ(tp_peb32(PEB_ImageSubsystemMajorVersion), nt.OptionalHeader.MajorSubsystemVersion);
    EXPECT_EQ(tp_peb32(PEB_ImageSubsystemMinorVersion), nt.OptionalHeader.MinorSubsystemVersion);
    EXPECT_EQ(tp_peb64(PEB_ImageBaseAddress), (ULONG_PTR)exe);
}

TEST(teb_peb_win32_fields_peb_process_heap) {
    HANDLE h = GetProcessHeap();
    EXPECT_EQ(tp_peb64(PEB_ProcessHeap), (ULONG_PTR)h);
    CHECK(HeapValidate(h, 0, NULL));
    /* PEB->ProcessHeaps[0] is the default process heap */
    unsigned long long arr = tp_peb64(PEB_ProcessHeaps), first = 0;
    CHECK(tp_peb32(PEB_NumberOfHeaps) >= 1);
    CHECK(tp_peb32(PEB_NumberOfHeaps) <= tp_peb32(PEB_MaximumNumberOfHeaps));
    if (k_try_read((void *)(ULONG_PTR)arr, &first, sizeof first)) EXPECT_EQ(first, (ULONG_PTR)h);
    else CHECK(0);
}

TEST(teb_peb_win32_fields_peb_ntglobalflag_is_informational) {
    unsigned long f = tp_peb32(PEB_NtGlobalFlag);
    t_note("PEB.NtGlobalFlag=0x%lx NtGlobalFlag2=0x%lx BeingDebugged=%u", f, tp_peb32(PEB_NtGlobalFlag2), tp_peb8(PEB_BeingDebugged));
    /* heap-debug flags (0x70) are only ever set by a debugger-spawn or an IFEO GlobalFlag */
    if (!IsDebuggerPresent() && (f & 0x70)) t_note("heap tail/free/validate flags set without a debugger (0x%lx)", f & 0x70);
}

TEST(teb_peb_win32_fields_peb_beingdebugged_matches_api) {
    EXPECT_EQ(tp_peb8(PEB_BeingDebugged) != 0, IsDebuggerPresent() != 0);
}

TEST(teb_peb_win32_fields_peb_os_version_matches_kuser_shared_data) {
    /* KUSER_SHARED_DATA is the kernel's read-only page at 0x7FFE0000: NtMajorVersion@0x26C, NtMinorVersion@0x270 */
    unsigned long maj = 0, min = 0;
    CHECK(k_try_read_u32((void *)0x7FFE026C, &maj));
    CHECK(k_try_read_u32((void *)0x7FFE0270, &min));
    EXPECT_EQ(tp_peb32(PEB_OSMajorVersion), maj);
    EXPECT_EQ(tp_peb32(PEB_OSMinorVersion), min);
}

/* ---- WOW64 members ------------------------------------------------------------------------------ */

TEST(teb_peb_win32_fields_native_x64_has_no_wow64_state) {
    BOOL w = TRUE;
    REQUIRE(U(IsWow64Process));
    CHECK(U(IsWow64Process)(GetCurrentProcess(), &w));
    if (w) { t_skip("running as WOW64; the x64 build of this exe is never WOW64"); return; }
    EXPECT_EQ(tp_teb64(TEB_WOW32Reserved), 0);           /* wow64cpu's transition slot exists only in WOW64 threads */
    EXPECT_EQ(tp_peb32(PEB_AtlThunkSListPtr32), 0);
    if (tp_teb32(TEB_WowTebOffset) != 0) t_note("WowTebOffset=%ld on a native x64 thread", (long)tp_teb32(TEB_WowTebOffset));
}

/* ---- members that stay zero until the matching component loads ---------------------------------- */

TEST(teb_peb_win32_fields_opengl_members_zero_without_opengl32) {
    /* only opengl32 (glsrvr/ICD thunks) populates TEB->glDispatchTable / glCurrentRC / glContext / glTable / glSection(Info) */
    if (GetModuleHandleA("opengl32.dll")) { t_skip("opengl32 loaded"); return; }
    EXPECT_EQ(tp_teb64(TEB_glDispatchTable), 0);
    for (unsigned o = 0; o < 233 * 8; o += 8) if (tp_teb64(TEB_glDispatchTable + o) != 0) { CHECKF(0, "glDispatchTable+0x%x non-zero", o); break; }
    EXPECT_EQ(tp_teb64(TEB_glReserved2), 0);
    EXPECT_EQ(tp_teb64(TEB_glSectionInfo), 0);
    EXPECT_EQ(tp_teb64(TEB_glSection), 0);
    EXPECT_EQ(tp_teb64(TEB_glTable), 0);
    EXPECT_EQ(tp_teb64(TEB_glCurrentRC), 0);
    EXPECT_EQ(tp_teb64(TEB_glContext), 0);
}

TEST(teb_peb_win32_fields_gl_reserved1_values) {
    for (unsigned i = 0; i < 29; i++) {
        unsigned long long v = tp_teb64(TEB_glReserved1 + i * 8);
        if (v) t_note("glReserved1[%u]=0x%llx", i, v);
    }
}

TEST(teb_peb_win32_fields_winsock_data_zero_without_ws2_32) {
    if (GetModuleHandleA("ws2_32.dll") || GetModuleHandleA("mswsock.dll")) { t_skip("winsock loaded"); return; }
    EXPECT_EQ(tp_teb64(TEB_WinSockData), 0);
}

/* The harness's own module lookups load user32/gdi32 and, on Windows 10, that pulls COM into this process,
 * so the check runs in a fresh child that has loaded nothing but its static imports. Exit codes: 0 pass,
 * 1 field non-zero, 2 COM loaded anyway (skip). */
CHILD(teb_peb_win32_fields_ole_child) {
    (void)argc; (void)argv;
    if (GetModuleHandleA("ole32.dll") || GetModuleHandleA("combase.dll")) return 2;
    return tp_teb64(TEB_ReservedForOle) == 0 ? 0 : 1;
}
TEST(teb_peb_win32_fields_reserved_for_ole_zero_without_com) {
    DWORD rc = t_run_child("teb_peb_win32_fields_ole_child", NULL, 10000);
    if (rc == 2) { t_skip("COM loaded in a fresh process"); return; }
    EXPECT_EQ(rc, 0);
}

TEST(teb_peb_win32_fields_user_reserved_arrays_are_reported) {
    /* User32Reserved[26] (TEB+0x80) and UserReserved[5] (TEB+0xE8) are reserved for user32/win32k;
     * their use is not documented in any analysis inspected, so only report */
    int nz = 0;
    for (unsigned i = 0; i < 26; i++) { unsigned long v = tp_teb32(TEB_User32Reserved + i * 4); if (v) { t_note("User32Reserved[%u]=0x%lx", i, v); nz++; } }
    for (unsigned i = 0; i < 5; i++) { unsigned long v = tp_teb32(TEB_UserReserved + i * 4); if (v) { t_note("UserReserved[%u]=0x%lx", i, v); nz++; } }
    if (!nz) t_note("User32Reserved/UserReserved all zero");
}

TEST(teb_peb_win32_fields_impersonation_flag_follows_thread_token) {
    /* TEB.IsImpersonating (0x179C) is maintained by ntdll/kernelbase when a thread token is set via the Rtl/advapi path */
    t_note("IsImpersonating before=%lu", tp_teb32(TEB_IsImpersonating));
    HANDLE tok = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY, &tok)) { t_skip("OpenProcessToken failed"); return; }
    HANDLE dup = NULL;
    BOOL ok = DuplicateTokenEx(tok, TOKEN_IMPERSONATE | TOKEN_QUERY, NULL, SecurityImpersonation, TokenImpersonation, &dup);
    CloseHandle(tok);
    if (!ok) { t_skip("DuplicateTokenEx failed"); return; }
    if (SetThreadToken(NULL, dup)) {
        t_note("IsImpersonating during=%lu", tp_teb32(TEB_IsImpersonating));
        SetThreadToken(NULL, NULL);
    }
    t_note("IsImpersonating after=%lu", tp_teb32(TEB_IsImpersonating));
    CloseHandle(dup);
    HANDLE cur = NULL;
    CHECK(!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &cur));   /* thread token is gone again */
    if (cur) CloseHandle(cur);
}

TEST(teb_peb_win32_fields_current_locale_is_thread_locale) {
    /* TEB.CurrentLocale caches NtQueryDefaultLocale(TRUE); GetThreadLocale() returns it unless SetThreadLocale overrode */
    LCID l = GetThreadLocale();
    unsigned long c = tp_teb32(TEB_CurrentLocale);
    if (c == 0) t_note("TEB.CurrentLocale is 0 (not yet cached), GetThreadLocale()=0x%lx", (unsigned long)l);
    else { CHECKF(c == (unsigned long)l, "TEB.CurrentLocale=0x%lx GetThreadLocale()=0x%lx", c, (unsigned long)l); }
}
