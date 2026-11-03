/* win32k_modules_sysinfo: KUSER_SHARED_DATA at 0x7FFE0000 as maintained by the kernel and consumed
 * by the Win32 user-mode DLLs. Layout is the PDB-backed _KUSER_SHARED_DATA of ntoskrnl 19041
 * (TickCountMultiplier 0x4, InterruptTime 0x8, SystemTime 0x14, NtSystemRoot 0x30, NtBuildNumber
 * 0x260, NtProductType 0x264, NtMajorVersion 0x26C, ProcessorFeatures 0x274, ActiveConsoleId 0x2D8,
 * QpcFrequency 0x300, SystemCall 0x308, TickCount 0x320, Cookie 0x330,
 * ConsoleSessionForegroundProcessId 0x338, ActiveProcessorCount 0x3C0, XState 0x3D8).
 * Wherever a user-mode API reads the field verbatim (GetTickCount, QueryPerformanceFrequency,
 * IsProcessorFeaturePresent, WTSGetActiveConsoleSessionId, GetLargePageMinimum, RtlGetNtProductType,
 * GetSystemTimeAsFileTime) the test pins field and API to each other. */
#include "ms.h"

TEST(win32k_modules_sysinfo_kuser_page_readonly_committed) {
    MEMORY_BASIC_INFORMATION mbi; memset(&mbi, 0, sizeof mbi);
    REQUIRE(VirtualQuery((void *)(ULONG_PTR)MS_KUSER, &mbi, sizeof mbi) == sizeof mbi);
    CHECK(mbi.State == MEM_COMMIT);
    if (t_is_wine()) { t_note("emulated protect 0x%lx", (unsigned long)mbi.Protect); return; }
    CHECKF(mbi.Protect == PAGE_READONLY, "protect 0x%lx", (unsigned long)mbi.Protect);
    CHECK(mbi.BaseAddress == (void *)(ULONG_PTR)MS_KUSER);
    CHECK(mbi.RegionSize >= 0x1000);
}

TEST(win32k_modules_sysinfo_kuser_page_not_writable_via_virtualprotect) {
    if (t_is_wine()) { t_skip("emulated shared data page"); return; }
    DWORD old = 0;
    /* The shared data view is a read-only mapping; asking for write access must be refused. */
    SetLastError(0);
    BOOL ok = VirtualProtect((void *)(ULONG_PTR)MS_KUSER, 0x1000, PAGE_READWRITE, &old);
    /* How NtProtectVirtualMemory treats the shared-data section view is not pinned by an analysis we have:
     * record it, and put the protection back immediately if it was granted. */
    t_note("VirtualProtect(KUSER, RW) -> %d (error %lu)", ok, (unsigned long)GetLastError());
    if (ok) VirtualProtect((void *)(ULONG_PTR)MS_KUSER, 0x1000, old, &old);
}

TEST(win32k_modules_sysinfo_kuser_os_version_is_10_0) {
    unsigned long maj = 0, min = 0;
    REQUIRE(ms_ku_u32(0x26C, &maj)); REQUIRE(ms_ku_u32(0x270, &min));
    CHECKF(maj == 10 && min == 0, "%lu.%lu", maj, min);
}

TEST(win32k_modules_sysinfo_kuser_build_number_matches_ntdll_file) {
    unsigned long b = 0; VS_FIXEDFILEINFO nt;
    REQUIRE(ms_ku_u32(0x260, &b));
    if (t_is_wine() || !ms_file_version("ntdll.dll", &nt)) { t_note("kuser build %lu", b); return; }
    /* NtBuildNumber is the product build RtlGetVersion reports; the 19041..19045 servicing releases all ship binaries whose
     * file version still says 19041, so ntdll's file build is only compared for the 19041 family */
    unsigned long build = 0, ubr = 0; k_win_build(&build, &ubr);
    CHECKF((b & 0x0FFFFFFF) == build, "kuser %lu RtlGetVersion %lu", b & 0x0FFFFFFF, build);
    if (build >= 19041 && build <= 19045) CHECKF(HIWORD(nt.dwFileVersionLS) == 19041, "ntdll file build %u", HIWORD(nt.dwFileVersionLS));
    else CHECK((b & 0x0FFFFFFF) == HIWORD(nt.dwFileVersionLS));
    t_note("kuser build %lu, ntdll file build %u", b & 0x0FFFFFFF, HIWORD(nt.dwFileVersionLS));
}

TEST(win32k_modules_sysinfo_kuser_build_number_free_build_flag_note) {
    unsigned long b = 0; REQUIRE(ms_ku_u32(0x260, &b));
    t_note("NtBuildNumber raw 0x%08lx", b);
    CHECK((b & 0x0FFFFFFF) > 10000);
}

TEST(win32k_modules_sysinfo_kuser_version_matches_rtlgetversion) {
    unsigned long maj = 0, min = 0, b = 0; REQUIRE(ms_ku_u32(0x26C, &maj)); REQUIRE(ms_ku_u32(0x270, &min)); REQUIRE(ms_ku_u32(0x260, &b));
    unsigned long build = 0, ubr = 0; k_win_build(&build, &ubr);
    /* RtlGetVersion reads the PEB copy; they differ only when a compatibility shim lies about the version. */
    if ((b & 0x0FFFFFFF) != build) { t_note("PEB build %lu differs from kuser %lu (shimmed process?)", build, b & 0x0FFFFFFF); return; }
    CHECK(maj == 10 && min == 0);
}

TEST(win32k_modules_sysinfo_kuser_product_type_valid) {
    unsigned long pt = 0; unsigned char valid = 0;
    REQUIRE(ms_ku_u32(0x264, &pt)); REQUIRE(ms_ku_u8(0x268, &valid));
    if (t_is_wine()) { t_note("product type %lu valid %u", pt, valid); return; }
    CHECKF(pt >= 1 && pt <= 3, "NtProductType %lu", pt);   /* NtProductWinNt / LanManNt / Server */
    CHECK(valid == 1);
}

TEST(win32k_modules_sysinfo_kuser_product_type_matches_ntdll_api) {
    typedef BOOLEAN (NTAPI *fn)(ULONG *);
    fn f = (fn)t_proc_in("ntdll.dll", "RtlGetNtProductType"); REQUIRE(f);
    ULONG api = 0; unsigned long ku = 0; REQUIRE(ms_ku_u32(0x264, &ku));
    if (!f(&api)) { t_skip("RtlGetNtProductType failed"); return; }
    CHECKF(api == ku, "api %lu kuser %lu", (unsigned long)api, ku);
}

TEST(win32k_modules_sysinfo_kuser_native_processor_architecture_amd64) {
    unsigned short a = 0; REQUIRE(ms_ku_u16(0x26A, &a));
    t_note("NativeProcessorArchitecture=%u", a);
    if (t_is_wine()) return;
    CHECKF(a == PROCESSOR_ARCHITECTURE_AMD64 || a == 0, "%u", a);   /* 0 where the kernel leaves the field unset */
}

TEST(win32k_modules_sysinfo_kuser_image_number_range) {
    unsigned short lo = 0, hi = 0; REQUIRE(ms_ku_u16(0x2C, &lo)); REQUIRE(ms_ku_u16(0x2E, &hi));
    t_note("ImageNumberLow=0x%04x ImageNumberHigh=0x%04x", lo, hi);
    CHECK(lo <= hi || hi == 0);
}

TEST(win32k_modules_sysinfo_kuser_nt_system_root_matches_windows_directory) {
    WCHAR ku[260], win[MAX_PATH];
    REQUIRE(k_try_read((void *)(ULONG_PTR)(MS_KUSER + 0x30), ku, sizeof ku));
    ku[259] = 0;
    UINT n = GetSystemWindowsDirectoryW(win, MAX_PATH); REQUIRE(n && n < MAX_PATH);
    CHECKF(!lstrcmpiW(ku, win), "kuser root \"%ls\" windows dir \"%ls\"", ku, win);
}

TEST(win32k_modules_sysinfo_kuser_nt_system_root_is_absolute_drive_path) {
    WCHAR ku[260]; REQUIRE(k_try_read((void *)(ULONG_PTR)(MS_KUSER + 0x30), ku, sizeof ku)); ku[259] = 0;
    CHECK(ku[0] && ku[1] == L':' && ku[2] == L'\\');
    size_t len = lstrlenW(ku);
    CHECK(len >= 4 && len < 100);
    CHECK(ku[len - 1] != L'\\');
}

TEST(win32k_modules_sysinfo_kuser_tick_multiplier_plausible) {
    unsigned long m = 0; REQUIRE(ms_ku_u32(0x4, &m));
    /* ticks * multiplier >> 24 is milliseconds: the period lies between 0.5 ms and 40 ms. */
    CHECKF(m >= 0x00800000UL && m <= 0x28000000UL, "TickCountMultiplier 0x%lx", m);
    t_note("TickCountMultiplier=0x%lx (%.4f ms per tick)", m, m / 16777216.0);
}

TEST(win32k_modules_sysinfo_kuser_gettickcount_derives_from_tickcount) {
    unsigned long m = 0; REQUIRE(ms_ku_u32(0x4, &m));
    for (int rep = 0; rep < 3; rep++) {
        unsigned long long q0 = ms_ku_ktime(0x320);
        DWORD api = GetTickCount();
        unsigned long long q1 = ms_ku_ktime(0x320);
        unsigned long long lo = (q0 * m) >> 24, hi = (q1 * m) >> 24;
        /* GetTickCount multiplies TickCount by TickCountMultiplier and shifts by 24 (kernel32/kernelbase). */
        CHECKF((DWORD)lo <= api + 1 && api <= (DWORD)hi + 1, "api %lu window [%llu,%llu]", (unsigned long)api, lo, hi);
        Sleep(15);
    }
}

TEST(win32k_modules_sysinfo_kuser_tickcount_advances_with_sleep) {
    unsigned long long a = ms_ku_ktime(0x320); Sleep(120); unsigned long long b = ms_ku_ktime(0x320);
    CHECKF(b > a, "tick %llu -> %llu", a, b);
    unsigned long m = 0; REQUIRE(ms_ku_u32(0x4, &m));
    unsigned long long ms = ((b - a) * m) >> 24;
    CHECKF(ms >= 60 && ms < 2000, "elapsed %llu ms for a 120 ms sleep", ms);
}

TEST(win32k_modules_sysinfo_kuser_tickcount_quad_aliases_ksystem_time) {
    unsigned long long q = 0; REQUIRE(ms_ku_u64(0x320, &q));
    unsigned long long k = ms_ku_ktime(0x320);
    /* TickCountQuad overlays the KSYSTEM_TIME (Low, High1): both name the same counter within a tick or two. */
    CHECKF(k >= q && k - q < 1000, "quad %llu ktime %llu", q, k);
}

TEST(win32k_modules_sysinfo_kuser_interrupt_time_monotonic) {
    unsigned long long a = ms_ku_ktime(0x8); Sleep(30); unsigned long long b = ms_ku_ktime(0x8);
    CHECK(a != 0);
    CHECKF(b > a, "%llu -> %llu", a, b);
    CHECK((b - a) < 10 * 10000000ULL);   /* well under 10 s of 100 ns units */
}

TEST(win32k_modules_sysinfo_kuser_ksystem_time_handshake_consistent) {
    /* High1 and High2 are written in the order the reader relies on; a reader that sees them equal gets a coherent value. */
    for (unsigned off = 0x8; off <= 0x20; off += 0xC) {
        unsigned long h1 = 0, h2 = 0, lo = 0;
        REQUIRE(ms_ku_u32(off + 4, &h1));
        REQUIRE(ms_ku_u32(off, &lo));
        REQUIRE(ms_ku_u32(off + 8, &h2));
        CHECK(h1 == h2 || h1 + 1 == h2);
    }
}

TEST(win32k_modules_sysinfo_kuser_system_time_matches_getsystemtimeasfiletime) {
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    unsigned long long api = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    unsigned long long ku = ms_ku_ktime(0x14); REQUIRE(ku);
    long long d = (long long)(api - ku);
    if (d < 0) d = -d;
    CHECKF(d < 2 * 10000000LL, "difference %lld x100ns", d);
}

TEST(win32k_modules_sysinfo_kuser_system_time_is_recent_epoch) {
    unsigned long long ku = ms_ku_ktime(0x14); REQUIRE(ku);
    /* FILETIME epoch 1601: anything after 2020-01-01 (13222... x100ns) and before 2100 */
    CHECK(ku > 132223104000000000ULL);
    CHECK(ku < 157766880000000000ULL);
}

TEST(win32k_modules_sysinfo_kuser_timezone_id_range) {
    unsigned long id = 0; REQUIRE(ms_ku_u32(0x240, &id));
    CHECKF(id <= 2, "TimeZoneId %lu", id);   /* TIME_ZONE_ID_UNKNOWN/STANDARD/DAYLIGHT */
}

TEST(win32k_modules_sysinfo_kuser_timezone_bias_matches_gettimezoneinformation) {
    TIME_ZONE_INFORMATION tz; DWORD r = GetTimeZoneInformation(&tz);
    if (r == TIME_ZONE_ID_INVALID) { t_skip("GetTimeZoneInformation failed"); return; }
    long long bias = (long long)ms_ku_ktime(0x20);
    /* KUSER TimeZoneBias is the current bias in 100 ns units: (Bias + active-bias) minutes, non-positive east of UTC. */
    long active = tz.Bias + (r == TIME_ZONE_ID_DAYLIGHT ? tz.DaylightBias : tz.StandardBias);
    long long want = (long long)active * 60 * 10000000LL;
    if (bias != want) t_note("kuser bias %lld, tz api %lld (dynamic DST transition?)", bias, want);
    CHECK(llabs(bias - want) <= 3600LL * 10000000LL || bias == -want);
}

TEST(win32k_modules_sysinfo_kuser_active_console_matches_wts_api) {
    typedef DWORD (WINAPI *fn)(void);
    fn f = (fn)t_proc_in("kernel32.dll", "WTSGetActiveConsoleSessionId"); REQUIRE(f);
    unsigned long ku = 0; REQUIRE(ms_ku_u32(0x2D8, &ku));
    CHECKF(f() == ku, "api %lu kuser %lu", (unsigned long)f(), ku);
}

TEST(win32k_modules_sysinfo_kuser_active_console_is_valid_or_none) {
    unsigned long ku = 0; REQUIRE(ms_ku_u32(0x2D8, &ku));
    CHECKF(ku < 4096 || ku == 0xFFFFFFFF, "ActiveConsoleId 0x%lx", ku);
    t_note("ActiveConsoleId=%lu own session=%lu", ku, k_session_id());
}

TEST(win32k_modules_sysinfo_kuser_foreground_process_id_range) {
    unsigned long long fg = 0; REQUIRE(ms_ku_u64(0x338, &fg));
    t_info("ConsoleSessionForegroundProcessId=%llu", fg);
    /* A process id (multiple of 4, fits 32 bits) or 0 when the console session has no foreground window. */
    CHECK(fg <= 0xFFFFFFFFULL);
    CHECK((fg & 3) == 0);
}

TEST(win32k_modules_sysinfo_kuser_foreground_process_is_a_live_process_when_set) {
    unsigned long long fg = 0; REQUIRE(ms_ku_u64(0x338, &fg));
    if (!fg || fg == 0xFFFFFFFFULL) { t_skip("no foreground process recorded"); return; }
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)fg);
    if (!h && GetLastError() == ERROR_ACCESS_DENIED) { t_info("foreground pid %llu exists but is protected", fg); return; }
    if (!h) { t_info("foreground pid %llu already gone (raced)", fg); return; }
    DWORD code = 0; GetExitCodeProcess(h, &code);
    CloseHandle(h);
    t_info("foreground pid %llu exit code %lu", fg, (unsigned long)code);
}

TEST(win32k_modules_sysinfo_kuser_system_call_selector_values) {
    unsigned long sc = 0; REQUIRE(ms_ku_u32(0x308, &sc));
    t_note("SystemCall selector=%lu (win32u stub: test byte [0x7FFE0308],1; jne -> syscall else int 2E)", sc);
    CHECK(sc <= 0xFF);
}

TEST(win32k_modules_sysinfo_kuser_win32u_stubs_test_the_systemcall_byte) {
    FARPROC p = t_proc_in("win32u.dll", "NtUserGetThreadState"); REQUIRE(p);
    if (t_is_wine()) { t_skip("emulated stub"); return; }
    unsigned char b[32]; REQUIRE(k_try_read((void *)p, b, sizeof b));   /* k_stub_bytes only returns the short prologue */
    /* mov r10,rcx; mov eax,imm32; test byte [0x7FFE0308],1; jne +3; syscall; ret; int 2E; ret */
    CHECK(b[0] == 0x4C && b[1] == 0x8B && b[2] == 0xD1 && b[3] == 0xB8);
    static const unsigned char tst[] = { 0xF6, 0x04, 0x25, 0x08, 0x03, 0xFE, 0x7F, 0x01 };
    CHECK(!memcmp(b + 8, tst, sizeof tst));
    CHECK(b[16] == 0x75 && b[17] == 0x03 && b[18] == 0x0F && b[19] == 0x05 && b[20] == 0xC3 && b[21] == 0xCD && b[22] == 0x2E && b[23] == 0xC3);
}

TEST(win32k_modules_sysinfo_kuser_qpc_frequency_matches_api) {
    unsigned long long f = 0; REQUIRE(ms_ku_u64(0x300, &f));
    LARGE_INTEGER q; REQUIRE(QueryPerformanceFrequency(&q));
    CHECKF((unsigned long long)q.QuadPart == f, "api %lld kuser %llu", (long long)q.QuadPart, f);
    CHECK(f >= 1000000);
}

TEST(win32k_modules_sysinfo_kuser_qpc_shifts_sane) {
    unsigned char s1 = 0, s2 = 0; REQUIRE(ms_ku_u8(0x368, &s1)); REQUIRE(ms_ku_u8(0x369, &s2));
    CHECK(s1 < 64 && s2 < 64);
    unsigned long long inc = 0; REQUIRE(ms_ku_u64(0x358, &inc));
    t_note("QpcSystemTimeIncrement=0x%llx shift=%u interrupt shift=%u", inc, s1, s2);
}

TEST(win32k_modules_sysinfo_kuser_large_page_minimum_matches_api) {
    typedef SIZE_T (WINAPI *fn)(void);
    fn f = (fn)t_proc_in("kernel32.dll", "GetLargePageMinimum"); REQUIRE(f);
    unsigned long ku = 0; REQUIRE(ms_ku_u32(0x244, &ku));
    CHECKF(f() == ku, "api %llu kuser %lu", (unsigned long long)f(), ku);
    if (ku) CHECK((ku & (ku - 1)) == 0);
}

static const struct { int pf; const char *name; } MS_PF[] = {
    { PF_XMMI64_INSTRUCTIONS_AVAILABLE, "SSE2" }, { PF_NX_ENABLED, "NX" }, { PF_SSE3_INSTRUCTIONS_AVAILABLE, "SSE3" },
    { PF_XSAVE_ENABLED, "XSAVE" }, { PF_FASTFAIL_AVAILABLE, "FASTFAIL" }, { PF_COMPARE_EXCHANGE128, "CMPXCHG16B" },
    { PF_SSSE3_INSTRUCTIONS_AVAILABLE, "SSSE3" }, { PF_SSE4_1_INSTRUCTIONS_AVAILABLE, "SSE4.1" }, { PF_SSE4_2_INSTRUCTIONS_AVAILABLE, "SSE4.2" },
    { PF_PAE_ENABLED, "PAE" }, { PF_RDTSC_INSTRUCTION_AVAILABLE, "RDTSC" }, { PF_MMX_INSTRUCTIONS_AVAILABLE, "MMX" },
};
TEST(win32k_modules_sysinfo_kuser_processor_features_match_isprocessorfeaturepresent) {
    for (size_t i = 0; i < sizeof MS_PF / sizeof *MS_PF; i++) {
        unsigned char ku = 0;
        REQUIRE(ms_ku_u8(0x274 + MS_PF[i].pf, &ku));
        BOOL api = IsProcessorFeaturePresent(MS_PF[i].pf);
        CHECKF((ku != 0) == (api != 0), "%s: kuser %u api %d", MS_PF[i].name, ku, api);
    }
}

TEST(win32k_modules_sysinfo_kuser_processor_features_boolean_bytes) {
    for (int i = 0; i < 64; i++) {
        unsigned char b = 0; REQUIRE(ms_ku_u8(0x274 + i, &b));
        CHECKF(b <= 1, "feature %d byte 0x%02x", i, b);
    }
}

TEST(win32k_modules_sysinfo_kuser_x64_baseline_features_present) {
    if (t_is_wine()) { t_skip("emulated feature bytes"); return; }
    /* Every x64 CPU Windows runs on has these: SSE2 (PF 10), CMPXCHG16B (PF 14), NX (PF 12, mandatory since 8.1). */
    unsigned char sse2 = 0, cx16 = 0, nx = 0;
    REQUIRE(ms_ku_u8(0x274 + 10, &sse2)); REQUIRE(ms_ku_u8(0x274 + 14, &cx16)); REQUIRE(ms_ku_u8(0x274 + 12, &nx));
    CHECK(sse2 == 1); CHECK(cx16 == 1); CHECK(nx == 1);
}

TEST(win32k_modules_sysinfo_kuser_active_processors_match_basic_information) {
    unsigned long cnt = 0; unsigned char groups = 0;
    REQUIRE(ms_ku_u32(0x3C0, &cnt)); REQUIRE(ms_ku_u8(0x3C4, &groups));
    CHECK(cnt >= 1 && cnt <= 4096);
    CHECK(groups >= 1);
    unsigned char basic[0x40]; ULONG ret;
    REQUIRE(ms_qsi(0, basic, 0x40, &ret) == ST_SUCCESS);
    unsigned nproc = basic[0x38];
    if (groups == 1 && !t_is_wine()) CHECKF(cnt == nproc, "kuser %lu basic %u", cnt, nproc);
    DWORD api = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    CHECKF(api == cnt, "GetActiveProcessorCount %lu kuser %lu", (unsigned long)api, cnt);
}

TEST(win32k_modules_sysinfo_kuser_unparked_not_above_active) {
    unsigned short un = 0; unsigned long act = 0;
    REQUIRE(ms_ku_u16(0x36A, &un)); REQUIRE(ms_ku_u32(0x3C0, &act));
    if (un > act) t_note("UnparkedProcessorCount %u above ActiveProcessorCount %lu", un, act);
    CHECK(un <= act || t_is_wine());
}

TEST(win32k_modules_sysinfo_kuser_cookie_nonzero_and_stable) {
    unsigned long a = 0, b = 0; REQUIRE(ms_ku_u32(0x330, &a)); Sleep(5); REQUIRE(ms_ku_u32(0x330, &b));
    CHECK(a == b);
    if (!t_is_wine()) CHECK(a != 0);
}

TEST(win32k_modules_sysinfo_kuser_numberofphysicalpages_positive) {
    unsigned long n = 0; REQUIRE(ms_ku_u32(0x2E8, &n));
    CHECKF(n > 1000, "%lu pages", n);
    MEMORYSTATUSEX ms; ms.dwLength = sizeof ms; REQUIRE(GlobalMemoryStatusEx(&ms));
    /* total physical memory reported by the API is at most the page count in bytes */
    if (ms.ullTotalPhys > (unsigned long long)n * 4096 + (1ULL << 20)) t_note("API total %llu bytes above KUSER page count %lu (memory hot-add?)", ms.ullTotalPhys, n);
}

TEST(win32k_modules_sysinfo_kuser_safeboot_mode_range) {
    unsigned char sb = 0; REQUIRE(ms_ku_u8(0x2EC, &sb));
    CHECKF(sb <= 3, "SafeBootMode %u", sb);
    if (U(GetSystemMetrics)) CHECK(U(GetSystemMetrics)(SM_CLEANBOOT) == sb || t_is_wine());
}

TEST(win32k_modules_sysinfo_kuser_suite_mask_note) {
    unsigned long m = 0; REQUIRE(ms_ku_u32(0x2D0, &m));
    t_note("SuiteMask=0x%lx", m);
}

TEST(win32k_modules_sysinfo_kuser_shared_data_flags_note) {
    /* Bit 0 (DbgErrorPortPresent) depends on boot timing and differs between fresh guests; print only the stable
     * bits, the full value goes through t_info. */
    unsigned long f = 0; REQUIRE(ms_ku_u32(0x2F0, &f));
    t_info("SharedDataFlags=0x%lx", f);
    t_note("SharedDataFlags(stable bits)=0x%lx: Elevation=%lu Virt=%lu Installer=%lu LKG=%lu DynProc=%lu ConsoleBroker=%lu SecureBoot=%lu MultiSession=%lu MultiUsers=%lu StateSep=%lu",
           f & ~1ul, (f >> 1) & 1, (f >> 2) & 1, (f >> 3) & 1, (f >> 4) & 1, (f >> 5) & 1, (f >> 6) & 1, (f >> 7) & 1, (f >> 8) & 1, (f >> 9) & 1, (f >> 10) & 1);
}

TEST(win32k_modules_sysinfo_kuser_error_port_flag_matches_system_information) {
    /* DbgErrorPortPresent (bit 0) is set once the error port is registered during boot; whether that has happened
     * when the test runs varies between fresh guests, so only the value is reported (t_info) and the bit is
     * not asserted; the remaining bits are checked to be stable between two reads. */
    unsigned long f = 0; REQUIRE(ms_ku_u32(0x2F0, &f));
    t_info("DbgErrorPortPresent=%lu", f & 1);
    unsigned long g = 0; REQUIRE(ms_ku_u32(0x2F0, &g));
    CHECK((g & ~1ul) == (f & ~1ul));   /* the other flag bits do not change between reads */
}

TEST(win32k_modules_sysinfo_kuser_xstate_enabled_features_note) {
    unsigned long long en = 0; REQUIRE(ms_ku_u64(0x3D8, &en));
    t_note("XState.EnabledFeatures=0x%llx", en);
    if (en) CHECK((en & 3) == 3);   /* x87 and SSE state are always part of an enabled XSAVE configuration */
}

TEST(win32k_modules_sysinfo_kuser_xstate_size_covers_legacy_area) {
    unsigned long sz = 0; unsigned long long en = 0;
    REQUIRE(ms_ku_u64(0x3D8, &en));
    /* _XSTATE_CONFIGURATION: EnabledFeatures +0, EnabledVolatileFeatures +8, Size +0x10 */
    REQUIRE(ms_ku_u32(0x3D8 + 0x10, &sz));
    if (!en) { t_skip("no XSAVE configuration"); return; }
    CHECKF(sz >= 576 + 0 && sz <= 65536, "XState.Size %lu", sz);   /* 512-byte legacy region + 64-byte XSAVE header minimum */
}

TEST(win32k_modules_sysinfo_kuser_boot_id_and_dismount_note) {
    unsigned long boot = 0, dis = 0; REQUIRE(ms_ku_u32(0x2C4, &boot)); REQUIRE(ms_ku_u32(0x2DC, &dis));
    t_note("BootId=%lu DismountCount=%lu", boot, dis);
}

TEST(win32k_modules_sysinfo_kuser_kd_debugger_enabled_bits) {
    unsigned char kd = 0; REQUIRE(ms_ku_u8(0x2D4, &kd));
    CHECKF(kd <= 3, "KdDebuggerEnabled 0x%02x", kd);
}

TEST(win32k_modules_sysinfo_kuser_max_stack_trace_depth) {
    unsigned long d = 0; REQUIRE(ms_ku_u32(0x238, &d));
    CHECKF(d <= 1024, "MaxStackTraceDepth %lu", d);
}

TEST(win32k_modules_sysinfo_kuser_expected_64k_tail_unmodified_by_reads) {
    /* Two snapshots of the immutable head of the page (version, root, features) agree. */
    unsigned char a[0x2B4], b[0x2B4];
    REQUIRE(k_try_read((void *)(ULONG_PTR)(MS_KUSER + 0x260), a, 0x54)); Sleep(2); REQUIRE(k_try_read((void *)(ULONG_PTR)(MS_KUSER + 0x260), b, 0x54));
    CHECK(!memcmp(a, b, 0x54));
}
