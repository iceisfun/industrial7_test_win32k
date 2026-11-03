/* win32k_modules_sysinfo: NtQuerySystemInformation classes that describe the machine the Win32
 * stack runs on (basic/processor info, range start, kernel debugger, invalid/set-only classes)
 * plus the win32k service-index limit as seen from KiSystemServiceStart.
 *
 * Kernel facts (ntoskrnl NtQuerySystemInformation -> ExpQuerySystemInformation, 19041 analysis):
 *   - class > 0xE3 returns STATUS_INVALID_INFO_CLASS; classes with no handler (set-only ones such as
 *     SystemLoadGdiDriverInformation 0x1A/0x1B, SystemSessionCreate 0x2F, SystemSessionDetach 0x30,
 *     SystemSessionInformation 0x31, SystemLoadGdiDriverInSystemSpace 0x36) fall to the same status;
 *     0x6B/0x79/0xB4/0xD2/0xD3 are rejected by the NtQuerySystemInformation wrapper itself.
 *   - fixed-size classes check the length first: 0x00 needs exactly 0x40, 0x09 exactly 4, 0x32 exactly 8,
 *     0x01 at least 0xC, 0x2B at least 0x18, 0x2C at least 0xAC, 0x33 at least 0x90; on mismatch the
 *     required size goes to ReturnLength (when supplied) and the status is STATUS_INFO_LENGTH_MISMATCH.
 *   - class 0x32 (SystemRangeStartInformation) stores 0xFFFF800000000000.
 */
#include "ms.h"

static unsigned long long ms_range_start(void) {
    unsigned long long v = 0; ULONG ret = 0;
    return ms_qsi(50, &v, 8, &ret) == ST_SUCCESS ? v : 0;
}

/* ---- range start ------------------------------------------------------------------- */
TEST(win32k_modules_sysinfo_sysinfo_range_start_value) {
    unsigned long long v = 0x1111; ULONG ret = 0;
    LONG st = ms_qsi(50, &v, 8, &ret);
    CHECKF(st == ST_SUCCESS, "status 0x%lx", (unsigned long)st);
    if (t_is_wine()) { t_note("emulator range start 0x%llx", v); return; }
    CHECKF(v == 0xFFFF800000000000ULL, "0x%llx", v);
    CHECK(ret == 8);
}
TEST(win32k_modules_sysinfo_sysinfo_range_start_null_returnlength) {
    unsigned long long v = 0;
    CHECK(ms_qsi(50, &v, 8, NULL) == ST_SUCCESS);
}
TEST(win32k_modules_sysinfo_sysinfo_range_start_short_buffer) {
    unsigned long long v = 0x5555; ULONG ret = 0;
    LONG st = ms_qsi(50, &v, 4, &ret);
    CHECKF(st == ST_INFO_LENGTH_MISMATCH, "status 0x%lx", (unsigned long)st);
    CHECK(ret == 8);
    CHECK(v == 0x5555);
}
TEST(win32k_modules_sysinfo_sysinfo_range_start_long_buffer_rejected) {
    unsigned long long v[2] = { 0x77, 0x88 }; ULONG ret = 0;
    LONG st = ms_qsi(50, v, 16, &ret);
 /* The handler requires exactly 8 bytes (). */
    CHECKF(st == ST_INFO_LENGTH_MISMATCH, "status 0x%lx", (unsigned long)st);
    CHECK(ret == 8);
    CHECK(v[0] == 0x77);
}
TEST(win32k_modules_sysinfo_sysinfo_range_start_above_user_range) {
    unsigned long long rs = ms_range_start(); if (!rs) { t_skip("range start unavailable"); return; }
    SYSTEM_INFO si; GetNativeSystemInfo(&si);
    CHECK((unsigned long long)(ULONG_PTR)si.lpMaximumApplicationAddress < rs);
    /* The user/kernel split is canonical: every module sits at or above it. */
    MS_MODS *m = ms_mods();
    if (m && !t_is_wine()) for (ULONG i = 0; i < m->NumberOfModules; i++) CHECK((unsigned long long)(ULONG_PTR)m->Modules[i].ImageBase >= rs);
}
TEST(win32k_modules_sysinfo_sysinfo_range_start_vs_kuser_address) {
    /* KUSER_SHARED_DATA's user alias 0x7FFE0000 is a user address, its kernel alias 0xFFFFF78000000000 is >= range start. */
    unsigned long long rs = ms_range_start(); if (!rs || t_is_wine()) { t_skip("no range start"); return; }
    CHECK(0x7FFE0000ULL < rs);
    CHECK(0xFFFFF78000000000ULL >= rs);
}

/* ---- SystemBasicInformation ---------------------------------------------------------- */
struct ms_basic {
    ULONG Reserved, TimerResolution, PageSize, NumberOfPhysicalPages, LowestPhysicalPageNumber, HighestPhysicalPageNumber, AllocationGranularity, pad;
    ULONG_PTR MinimumUserModeAddress, MaximumUserModeAddress, ActiveProcessorsAffinityMask;
    CCHAR NumberOfProcessors;
};
static int ms_basic(struct ms_basic *b) { memset(b, 0, sizeof *b); ULONG r; return ms_qsi(0, b, 0x40, &r) == ST_SUCCESS; }

TEST(win32k_modules_sysinfo_sysinfo_basic_requires_exact_length) {
    unsigned char buf[0x80]; memset(buf, 0, sizeof buf);
    ULONG lens[] = { 0, 4, 0x20, 0x3F, 0x41, 0x80 };
    for (size_t i = 0; i < sizeof lens / sizeof *lens; i++) {
        ULONG ret = 0;
        LONG st = ms_qsi(0, buf, lens[i], &ret);
        CHECKF(st == ST_INFO_LENGTH_MISMATCH, "len %lu status 0x%lx", (unsigned long)lens[i], (unsigned long)st);
        CHECKF(ret == 0x40, "len %lu ret %lu", (unsigned long)lens[i], (unsigned long)ret);
    }
    ULONG ret = 0;
    CHECK(ms_qsi(0, buf, 0x40, &ret) == ST_SUCCESS);
    CHECK(ret == 0x40);
}
TEST(win32k_modules_sysinfo_sysinfo_basic_page_and_granularity) {
    struct ms_basic b; REQUIRE(ms_basic(&b));
    CHECK(b.PageSize == 4096);
    CHECK(b.AllocationGranularity == 65536);
    SYSTEM_INFO si; GetNativeSystemInfo(&si);
    CHECK(si.dwPageSize == b.PageSize);
    CHECK(si.dwAllocationGranularity == b.AllocationGranularity);
}
TEST(win32k_modules_sysinfo_sysinfo_basic_user_address_range) {
    struct ms_basic b; REQUIRE(ms_basic(&b));
    if (t_is_wine()) { t_note("min=%p max=%p", (void *)b.MinimumUserModeAddress, (void *)b.MaximumUserModeAddress); return; }
    CHECK(b.MinimumUserModeAddress == 0x10000);
    CHECK(b.MaximumUserModeAddress == 0x00007FFFFFFEFFFFULL);   /* top of the 47-bit user range minus the guard 64 KB */
    SYSTEM_INFO si; GetNativeSystemInfo(&si);
    CHECK((ULONG_PTR)si.lpMinimumApplicationAddress == b.MinimumUserModeAddress);
    CHECK((ULONG_PTR)si.lpMaximumApplicationAddress == b.MaximumUserModeAddress);
}
TEST(win32k_modules_sysinfo_sysinfo_basic_processor_count_matches_getsysteminfo) {
    struct ms_basic b; REQUIRE(ms_basic(&b));
    SYSTEM_INFO si; GetNativeSystemInfo(&si);
    CHECK(b.NumberOfProcessors >= 1);
    CHECKF((ULONG)si.dwNumberOfProcessors == (ULONG)(unsigned char)b.NumberOfProcessors || si.dwNumberOfProcessors > 64,
           "basic %d getsysteminfo %lu", b.NumberOfProcessors, (unsigned long)si.dwNumberOfProcessors);
}
TEST(win32k_modules_sysinfo_sysinfo_basic_affinity_mask_has_cpus) {
    struct ms_basic b; REQUIRE(ms_basic(&b));
    CHECK(b.ActiveProcessorsAffinityMask != 0);
    CHECK(__builtin_popcountll(b.ActiveProcessorsAffinityMask) >= 1);
    SYSTEM_INFO si; GetNativeSystemInfo(&si);
    if (b.NumberOfProcessors <= 64) CHECK((ULONG_PTR)si.dwActiveProcessorMask == b.ActiveProcessorsAffinityMask);
}
TEST(win32k_modules_sysinfo_sysinfo_basic_physical_pages_note) {
    struct ms_basic b; REQUIRE(ms_basic(&b));
    CHECK(b.NumberOfPhysicalPages > 1000);
    CHECK(b.LowestPhysicalPageNumber <= b.HighestPhysicalPageNumber);
    unsigned long ku = 0;
    if (ms_ku_u32(0x2E8, &ku)) t_note("KUSER NumberOfPhysicalPages=%lu SystemBasicInformation=%lu", ku, (unsigned long)b.NumberOfPhysicalPages);
}
TEST(win32k_modules_sysinfo_sysinfo_basic_timer_resolution_plausible) {
    struct ms_basic b; REQUIRE(ms_basic(&b));
    /* 100 ns units: between the 0.5 ms hardware floor and the 15.6 ms default tick (+ margin). */
    CHECKF(b.TimerResolution >= 1000 && b.TimerResolution <= 200000, "timer resolution %lu", (unsigned long)b.TimerResolution);
}

/* ---- SystemProcessorInformation ------------------------------------------------------ */
struct ms_cpu { USHORT Architecture, Level, Revision, MaximumProcessors; ULONG FeatureBits; };
TEST(win32k_modules_sysinfo_sysinfo_processor_length_protocol) {
    unsigned char buf[0x20] = {0};
    ULONG ret = 0;
    LONG st = ms_qsi(1, buf, 4, &ret);
    CHECK(st == ST_INFO_LENGTH_MISMATCH);
    CHECK(ret == 0xC);
    st = ms_qsi(1, buf, 0xC, &ret);
    CHECK(st == ST_SUCCESS);
    CHECK(ret == 0xC);
    st = ms_qsi(1, buf, 0x20, &ret);   /* larger buffers are accepted, still 12 bytes returned */
    CHECK(st == ST_SUCCESS);
    CHECK(ret == 0xC);
}
TEST(win32k_modules_sysinfo_sysinfo_processor_architecture_amd64) {
    struct ms_cpu c; ULONG ret;
    REQUIRE(ms_qsi(1, &c, sizeof c, &ret) == ST_SUCCESS);
    CHECKF(c.Architecture == PROCESSOR_ARCHITECTURE_AMD64, "architecture %u", c.Architecture);
    SYSTEM_INFO si; GetNativeSystemInfo(&si);
    CHECK(si.wProcessorArchitecture == c.Architecture);
}
TEST(win32k_modules_sysinfo_sysinfo_processor_level_revision_match_getsysteminfo) {
    struct ms_cpu c; ULONG ret;
    REQUIRE(ms_qsi(1, &c, sizeof c, &ret) == ST_SUCCESS);
    SYSTEM_INFO si; GetNativeSystemInfo(&si);
    CHECK(si.wProcessorLevel == c.Level);
    CHECK(si.wProcessorRevision == c.Revision);
    CHECK(c.MaximumProcessors >= 1);
}
TEST(win32k_modules_sysinfo_sysinfo_processor_architecture_matches_kuser) {
    struct ms_cpu c; ULONG ret; unsigned short ku;
    REQUIRE(ms_qsi(1, &c, sizeof c, &ret) == ST_SUCCESS);
    REQUIRE(ms_ku_u16(0x26A, &ku));
    if (t_is_wine()) { t_note("kuser NativeProcessorArchitecture %u", ku); return; }
    CHECKF(ku == c.Architecture, "kuser %u class1 %u", ku, c.Architecture);
}

/* ---- length-first protocol for other fixed classes ---------------------------------- */
static const struct { ULONG cls; ULONG need; int exact; const char *what; } MS_FIXED[] = {
    { 0x00, 0x40, 1, "SystemBasicInformation" },
    { 0x01, 0x0C, 0, "SystemProcessorInformation" },
    { 0x09, 0x04, 1, "SystemFlagsInformation" },
    { 0x2B, 0x18, 0, "SystemLegacyDriverInformation" },
    { 0x2C, 0xAC, 0, "SystemCurrentTimeZoneInformation" },
    { 0x32, 0x08, 1, "SystemRangeStartInformation" },
    { 0x33, 0x90, 0, "SystemVerifierInformation" },
};
TEST(win32k_modules_sysinfo_sysinfo_fixed_classes_report_required_size) {
    unsigned char buf[0x300]; memset(buf, 0xCC, sizeof buf);
    for (size_t i = 0; i < sizeof MS_FIXED / sizeof *MS_FIXED; i++) {
        ULONG ret = 0xDEAD;
        LONG st = ms_qsi(MS_FIXED[i].cls, buf, 1, &ret);
        CHECKF(st == ST_INFO_LENGTH_MISMATCH, "%s: status 0x%lx", MS_FIXED[i].what, (unsigned long)st);
        CHECKF(ret == MS_FIXED[i].need, "%s: ret %lu want %lu", MS_FIXED[i].what, (unsigned long)ret, (unsigned long)MS_FIXED[i].need);
        CHECK(buf[0] == 0xCC);
    }
}
TEST(win32k_modules_sysinfo_sysinfo_fixed_classes_zero_length) {
    unsigned char buf[16];
    for (size_t i = 0; i < sizeof MS_FIXED / sizeof *MS_FIXED; i++) {
        ULONG ret = 0xDEAD;
        LONG st = ms_qsi(MS_FIXED[i].cls, buf, 0, &ret);
        CHECKF(st == ST_INFO_LENGTH_MISMATCH, "%s: status 0x%lx", MS_FIXED[i].what, (unsigned long)st);
        CHECKF(ret == MS_FIXED[i].need, "%s: ret %lu", MS_FIXED[i].what, (unsigned long)ret);
    }
}
TEST(win32k_modules_sysinfo_sysinfo_fixed_classes_null_returnlength_still_fail) {
    unsigned char buf[16];
    for (size_t i = 0; i < sizeof MS_FIXED / sizeof *MS_FIXED; i++) {
        LONG st = ms_qsi(MS_FIXED[i].cls, buf, 1, NULL);
        CHECKF(st == ST_INFO_LENGTH_MISMATCH, "%s: status 0x%lx", MS_FIXED[i].what, (unsigned long)st);
    }
}
TEST(win32k_modules_sysinfo_sysinfo_exact_classes_reject_oversize) {
    unsigned char buf[0x100]; memset(buf, 0, sizeof buf);
    for (size_t i = 0; i < sizeof MS_FIXED / sizeof *MS_FIXED; i++) {
        if (!MS_FIXED[i].exact) continue;
        ULONG ret = 0;
        LONG st = ms_qsi(MS_FIXED[i].cls, buf, MS_FIXED[i].need + 8, &ret);
        CHECKF(st == ST_INFO_LENGTH_MISMATCH, "%s: status 0x%lx", MS_FIXED[i].what, (unsigned long)st);
        CHECK(ret == MS_FIXED[i].need);
    }
}
TEST(win32k_modules_sysinfo_sysinfo_flags_information_roundtrip) {
    ULONG f = 0xFFFFFFFF, ret = 0;
    LONG st = ms_qsi(9, &f, 4, &ret);
    CHECK(st == ST_SUCCESS);
    CHECK(ret == 4);
    CHECK(f != 0xFFFFFFFF);
    unsigned long peb_flag = 0;
    if (k_try_read_u32((char *)k_peb() + 0xBC, &peb_flag)) t_note("system NtGlobalFlag=0x%lx, PEB NtGlobalFlag=0x%lx", (unsigned long)f, peb_flag);
}
TEST(win32k_modules_sysinfo_sysinfo_legacy_driver_information_minimum) {
    unsigned char buf[0x40] = {0};
    ULONG ret = 0;
    LONG st = ms_qsi(0x2B, buf, 0x17, &ret);
    CHECK(st == ST_INFO_LENGTH_MISMATCH);
    CHECK(ret == 0x18);
    st = ms_qsi(0x2B, buf, 0x18, &ret);
    /* Content is a veto list; the call may be refused for restricted callers, never a length error. */
    CHECKF(st != ST_INFO_LENGTH_MISMATCH, "status 0x%lx", (unsigned long)st);
}
TEST(win32k_modules_sysinfo_sysinfo_timezone_information_layout) {
    unsigned char buf[0xAC]; memset(buf, 0, sizeof buf);
    ULONG ret = 0;
    LONG st = ms_qsi(0x2C, buf, 0xAC, &ret);
    CHECK(st == ST_SUCCESS);
    CHECK(ret == 0xAC);
    /* RTL_TIME_ZONE_INFORMATION: Bias(4) StandardName[32] StandardStart(16) StandardBias(4) ...; bias is a sane number of minutes. */
    LONG bias = *(LONG *)buf;
    CHECKF(bias > -24 * 60 && bias < 24 * 60, "bias %ld", (long)bias);
}
TEST(win32k_modules_sysinfo_sysinfo_verifier_information_min_size) {
    unsigned char buf[0x100] = {0};
    ULONG ret = 0;
    LONG st = ms_qsi(0x33, buf, 0x8F, &ret);
    CHECK(st == ST_INFO_LENGTH_MISMATCH);
    CHECK(ret == 0x90);
}

/* ---- kernel debugger ----------------------------------------------------------------- */
TEST(win32k_modules_sysinfo_sysinfo_kernel_debugger_matches_kuser) {
    unsigned char kd[2] = { 0xEE, 0xEE }; ULONG ret = 0; unsigned char ku;
    LONG st = ms_qsi(0x23, kd, 2, &ret);
    if (st != ST_SUCCESS) { t_skip("SystemKernelDebuggerInformation status 0x%lx", (unsigned long)st); return; }
    CHECK(ret == 2);
    CHECK(kd[0] <= 1 && kd[1] <= 1);
    REQUIRE(ms_ku_u8(0x2D4, &ku));
    if (t_is_wine()) return;
    CHECKF(kd[0] == (ku & 1), "KernelDebuggerEnabled=%u KUSER=0x%02x", kd[0], ku);
    t_note("debugger enabled=%u notpresent=%u kuser=0x%02x", kd[0], kd[1], ku);
}

/* ---- invalid / set-only classes ------------------------------------------------------- */
TEST(win32k_modules_sysinfo_sysinfo_class_above_range_invalid) {
    ULONG classes[] = { 0xE4, 0xE5, 0xFF, 0x100, 0x1000, 0x7FFF, 0x10000, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF };
    unsigned char buf[64] = {0};
    for (size_t i = 0; i < sizeof classes / sizeof *classes; i++) {
        ULONG ret = 0x1234;
        LONG st = ms_qsi(classes[i], buf, sizeof buf, &ret);
        CHECKF(st == ST_INVALID_INFO_CLASS, "class 0x%lx status 0x%lx", (unsigned long)classes[i], (unsigned long)st);
    }
}
TEST(win32k_modules_sysinfo_sysinfo_wrapper_rejected_classes) {
    /* NtQuerySystemInformation itself returns STATUS_INVALID_INFO_CLASS for these before dispatch. */
    ULONG classes[] = { 0x6B, 0x79, 0xB4, 0xD2, 0xD3 };
    unsigned char buf[64] = {0};
    for (size_t i = 0; i < sizeof classes / sizeof *classes; i++) {
        ULONG ret = 0;
        LONG st = ms_qsi(classes[i], buf, sizeof buf, &ret);
        CHECKF(st == ST_INVALID_INFO_CLASS, "class 0x%lx status 0x%lx", (unsigned long)classes[i], (unsigned long)st);
    }
}
TEST(win32k_modules_sysinfo_sysinfo_set_only_gdi_classes_not_queryable) {
    /* SystemLoadGdiDriverInformation (0x1A), SystemUnloadGdiDriverInformation (0x1B) and
     * SystemLoadGdiDriverInSystemSpace (0x36) only exist for NtSetSystemInformation. */
    ULONG classes[] = { 0x1A, 0x1B, 0x36 };
    unsigned char buf[128] = {0};
    for (size_t i = 0; i < sizeof classes / sizeof *classes; i++) {
        ULONG ret = 0;
        LONG st = ms_qsi(classes[i], buf, sizeof buf, &ret);
        CHECKF(st == ST_INVALID_INFO_CLASS, "class 0x%lx status 0x%lx", (unsigned long)classes[i], (unsigned long)st);
    }
}
TEST(win32k_modules_sysinfo_sysinfo_set_only_session_classes_not_queryable) {
    /* SystemSessionCreate (0x2F), SystemSessionDetach (0x30) and SystemSessionInformation (0x31) are
     * set-only; csrss drives them through NtSetSystemInformation. */
    ULONG classes[] = { 0x2F, 0x30, 0x31 };
    unsigned char buf[128] = {0};
    for (size_t i = 0; i < sizeof classes / sizeof *classes; i++) {
        ULONG ret = 0;
        LONG st = ms_qsi(classes[i], buf, sizeof buf, &ret);
        CHECKF(st == ST_INVALID_INFO_CLASS, "class 0x%lx status 0x%lx", (unsigned long)classes[i], (unsigned long)st);
    }
}
TEST(win32k_modules_sysinfo_sysinfo_invalid_class_does_not_touch_buffer) {
    unsigned char buf[32]; memset(buf, 0x5A, sizeof buf);
    ULONG ret = 0;
    CHECK(ms_qsi(0x100, buf, sizeof buf, &ret) == ST_INVALID_INFO_CLASS);
    for (size_t i = 0; i < sizeof buf; i++) CHECK(buf[i] == 0x5A);
}

/* ---- the win32k service index limit as enforced by KiSystemServiceStart ---------------- */
static unsigned long ms_service_limit(void) {
    /* The raw syscalls below reach the win32k table; on Windows 10 the first one from a process that has not loaded user32
     * dies in the thread conversion (no client callback table yet), so load it first. */
    LoadLibraryA("user32.dll");
    MS_PE *pe = ms_pe("win32k.sys"); if (!pe) return 0;
    DWORD rva = ms_pe_export(pe, "W32pServiceLimit", NULL); if (!rva) return 0;
    size_t o = ms_pe_off(pe, rva); if (o == (size_t)-1) return 0;
    DWORD v; memcpy(&v, pe->base + o, 4); return v;
}
TEST(win32k_modules_sysinfo_sysinfo_service_index_at_limit_is_invalid_service) {
    if (t_is_wine()) { t_skip("raw syscalls are not meaningful under the emulator"); return; }
    unsigned long lim = ms_service_limit(); if (!lim) { t_skip("W32pServiceLimit unreadable"); return; }
    /* KiSystemServiceStart: index = eax & 0xFFF is compared with the table limit; at or beyond
     * it the call completes with STATUS_INVALID_SYSTEM_SERVICE (user32 is loaded first so the thread is a GUI thread). */
    K_NTSTATUS st = k_raw_syscall(0x1000 + lim, 0, 0, 0, 0, 0, 0, 0, 0);
    CHECKF((LONG)st == ST_INVALID_SYSTEM_SERVICE, "status 0x%llx", (unsigned long long)st);
}
TEST(win32k_modules_sysinfo_sysinfo_service_index_beyond_limit_is_invalid_service) {
    if (t_is_wine()) { t_skip("raw syscalls are not meaningful under the emulator"); return; }
    unsigned long lim = ms_service_limit(); if (!lim) { t_skip("W32pServiceLimit unreadable"); return; }
    unsigned long idx[] = { lim + 1, lim + 17, 0xFFE, 0xFFF };
    for (size_t i = 0; i < sizeof idx / sizeof *idx; i++) {
        K_NTSTATUS st = k_raw_syscall(0x1000 + idx[i], 0, 0, 0, 0, 0, 0, 0, 0);
        CHECKF((LONG)st == ST_INVALID_SYSTEM_SERVICE, "index %lu status 0x%llx", idx[i], (unsigned long long)st);
    }
}
TEST(win32k_modules_sysinfo_sysinfo_service_index_high_bits_ignored_by_table_select) {
    if (t_is_wine()) { t_skip("raw syscalls are not meaningful under the emulator"); return; }
    unsigned long lim = ms_service_limit(); if (!lim) { t_skip("W32pServiceLimit unreadable"); return; }
    /* Only bit 12 selects the win32k table ((eax >> 7) & 0x20); bits 13+ do not, but the index
     * (eax & 0xFFF) is still limit-checked, so bit 12 set with an out-of-range index fails the same way. */
    K_NTSTATUS st = k_raw_syscall(0x3000 + lim, 0, 0, 0, 0, 0, 0, 0, 0);
    CHECKF((LONG)st == ST_INVALID_SYSTEM_SERVICE, "status 0x%llx", (unsigned long long)st);
}
TEST(win32k_modules_sysinfo_sysinfo_last_valid_service_number_matches_limit) {
    if (t_is_wine()) { t_skip("no genuine win32u"); return; }
    unsigned long lim = ms_service_limit(); if (!lim) { t_skip("W32pServiceLimit unreadable"); return; }
    long mx = 0;
    FILE *unused = NULL; (void)unused;
    HMODULE m = GetModuleHandleA("win32u.dll"); REQUIRE(m);
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)m;
    const IMAGE_NT_HEADERS64 *nt = (const IMAGE_NT_HEADERS64 *)((const char *)m + dos->e_lfanew);
    const IMAGE_EXPORT_DIRECTORY *ex = (const IMAGE_EXPORT_DIRECTORY *)((const char *)m + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
    const DWORD *funcs = (const DWORD *)((const char *)m + ex->AddressOfFunctions);
    for (DWORD i = 0; i < ex->NumberOfFunctions; i++) {
        if (!funcs[i]) continue;
        FARPROC p = (FARPROC)((const char *)m + funcs[i]);
        if (!k_stub_valid(p)) continue;
        long n = k_stub_number(p);
        if (n > mx) mx = n;
    }
    CHECKF((unsigned long)(mx - 0x1000 + 1) == lim, "highest stub %ld, limit %lu", mx, lim);
}
