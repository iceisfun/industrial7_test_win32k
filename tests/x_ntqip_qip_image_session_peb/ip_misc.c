/* x_ntqip_qip_image_session_peb: output layouts and relations of the non-string classes
 *   19 wow64-x86 flag       21 affinity (mask, optional group)   23 device map      24 session id
 *   26 wow64 pointer        28 per-LUID device maps flag          37 image information (section image info)
 *   44 image file mapping check (input: a file handle)           47 group list      49 console host
 *   66 / 67 default and allowed cpu sets                          89 image section handle (self only)
 *   97 leap second information (self only, pseudo handle only)
 * Values that depend on the machine (drive letters, cpu counts, process ids) are only compared with the Win32 API
 * that reports the same fact. */
#include "ip.h"
#include <tlhelp32.h>

typedef LONG (NTAPI *NtQuerySection_t)(HANDLE, ULONG, PVOID, SIZE_T, PSIZE_T);
typedef LONG (NTAPI *NtQueryObject_t)(HANDLE, ULONG, PVOID, ULONG, PULONG);

/* ---- 24 session -------------------------------------------------------------------------------------------- */
TEST(ip_session_equals_peb_and_api) {
    if (!ip_init()) return;
    ULONG v = 0xAAAAAAAA, rl; unsigned long peb = 0xFFFFFFFF;
    EXPECT_EQ(ip_q((HANDLE)-1, C_SESSION, &v, 4, &rl), ST_OK);
    CHECK(rl == 4);
    REQUIRE(k_try_read_u32((char *)k_peb() + PEB_SessionId, &peb));
    CHECKF(v == peb, "class %lu peb %lu", v, peb);
    DWORD api = 0xFFFFFFFF; CHECK(ProcessIdToSessionId(GetCurrentProcessId(), &api) && api == v);
}
TEST(ip_session_of_another_process) {
    if (!ip_init()) return;
    struct kid k; if (!kid_start(&k, "", 0, NULL, 0, NULL)) { t_skip("no child"); return; }
    ULONG v = 0xAAAAAAAA, mine = 0, rl; DWORD api = 0xFFFFFFFF;
    HANDLE h = kid_open(&k, PROC_LIM);
    EXPECT_EQ(ip_q(h, C_SESSION, &v, 4, &rl), ST_OK);
    EXPECT_EQ(ip_q((HANDLE)-1, C_SESSION, &mine, 4, &rl), ST_OK);
    CHECK(v == mine);
    CHECK(ProcessIdToSessionId(k.pid, &api) && api == v);
    CloseHandle(h); kid_stop(&k);
}
TEST(ip_session_of_system_process) {
    if (!ip_init()) return;
    HANDLE h = OpenProcess(PROC_LIM, FALSE, 4);
    if (!h) { t_skip("cannot open the System process (restricted token)"); return; }
    ULONG v = 0xAAAAAAAA, rl; EXPECT_EQ(ip_q(h, C_SESSION, &v, 4, &rl), ST_OK);
    CHECK(v == 0);
    CloseHandle(h);
}

/* ---- 26 wow64 pointer, 19 wow64-x86 flag ------------------------------------------------------------------- */
TEST(ip_wow64_pointer_is_zero_on_native_x64) {
    if (!ip_init()) return;
    unsigned char *b = ip_buf(); ULONG rl; ip_fill(b, 16);
    EXPECT_EQ(ip_q((HANDLE)-1, C_WOW64, b, 8, &rl), ST_OK);
    CHECK(*(unsigned long long *)b == 0);          /* all 8 bytes written */
    CHECK(ip_all_filler(b + 8, 8));
    BOOL w = TRUE; CHECK(IsWow64Process(GetCurrentProcess(), &w) && !w);
}
TEST(ip_wow64_pointer_of_child_is_zero) {
    if (!ip_init()) return;
    struct kid k; if (!kid_start(&k, "", 0, NULL, 0, NULL)) { t_skip("no child"); return; }
    unsigned long long v = 5; ULONG rl; HANDLE h = kid_open(&k, PROC_LIM);
    EXPECT_EQ(ip_q(h, C_WOW64, &v, 8, &rl), ST_OK); CHECK(v == 0);
    ULONG f = 5; HANDLE q = kid_open(&k, PROC_QRY);
    EXPECT_EQ(ip_q(q, C_WOW86FLAG, &f, 4, &rl), ST_OK); CHECK(f == 0);
    CloseHandle(h); CloseHandle(q); kid_stop(&k);
}
TEST(ip_wow86_flag_is_zero_and_written) {
    if (!ip_init()) return;
    unsigned char *b = ip_buf(); ULONG rl; ip_fill(b, 8);
    EXPECT_EQ(ip_q((HANDLE)-1, C_WOW86FLAG, b, 4, &rl), ST_OK);
    CHECK(*(ULONG *)b == 0 && ip_all_filler(b + 4, 4));
}

/* ---- 28 per-LUID device maps ------------------------------------------------------------------------------- */
TEST(ip_luid_device_maps_flag_ignores_the_handle) {
    if (!ip_init()) return;
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    struct kid k; int have = kid_start(&k, "", 0, NULL, 0, NULL);
    HANDLE sy = have ? kid_open(&k, SYNCHRONIZE) : NULL;
    HANDLE hs[] = { NULL, (HANDLE)-1, (HANDLE)(LONG_PTR)-2, (HANDLE)(LONG_PTR)-5, (HANDLE)(ULONG_PTR)0x7ffc0000, ev, sy };
    for (int i = 0; i < 7; i++) {
        if (i == 6 && !sy) continue;
        ULONG v = 0xAAAAAAAA, rl;
        LONG st = ip_q(hs[i], C_LUIDMAPS, &v, 4, &rl);
        CHECKF(st == ST_OK && v == 1 && rl == 4, "handle #%d: 0x%lx value %lu rl %lu", i, (unsigned long)st, v, rl);
    }
    CloseHandle(ev); if (sy) CloseHandle(sy);
    if (have) kid_stop(&k);
}
TEST(ip_luid_device_maps_flag_length_still_checked_with_bad_handle) {
    if (!ip_init()) return;
    ULONG v, rl;
    EXPECT_EQ(ip_q(NULL, C_LUIDMAPS, &v, 3, &rl), ST_INFO_LEN);
    EXPECT_EQ(ip_q(NULL, C_LUIDMAPS, &v, 8, &rl), ST_INFO_LEN);
    CHECK(rl == RL_SENT);
}

/* ---- 21 affinity ------------------------------------------------------------------------------------------- */
TEST(ip_affinity_mask_matches_win32) {
    if (!ip_init()) return;
    unsigned char *b = ip_buf(); ULONG rl; DWORD_PTR pm, sm;
    CHECK(GetProcessAffinityMask(GetCurrentProcess(), &pm, &sm));
    ip_fill(b, 0x20);
    EXPECT_EQ(ip_q((HANDLE)-1, C_AFFINITY, b, 8, &rl), ST_OK);
    CHECK(rl == 8 && *(unsigned long long *)b == pm);
    CHECKF(ip_all_filler(b + 8, 0x18), "the 8-byte form wrote the group field");
    ip_fill(b, 0x20);
    EXPECT_EQ(ip_q((HANDLE)-1, C_AFFINITY, b, 0x10, &rl), ST_OK);
    CHECK(rl == 0x10 && *(unsigned long long *)b == pm);
    CHECKF(*(USHORT *)(b + 8) == 0, "group %u", *(USHORT *)(b + 8));
    CHECKF(ip_all_filler(b + 10, 6), "bytes after the group were written (they are padding)");
}
TEST(ip_affinity_follows_set_affinity) {
    if (!ip_init()) return;
    DWORD_PTR pm, sm; unsigned long long v = 0; ULONG rl;
    CHECK(GetProcessAffinityMask(GetCurrentProcess(), &pm, &sm));
    DWORD_PTR one = pm & (~pm + 1);                       /* lowest allowed cpu */
    if (!SetProcessAffinityMask(GetCurrentProcess(), one)) { t_skip("SetProcessAffinityMask refused"); return; }
    EXPECT_EQ(ip_q((HANDLE)-1, C_AFFINITY, &v, 8, &rl), ST_OK);
    CHECK(v == one);
    CHECK(SetProcessAffinityMask(GetCurrentProcess(), pm));
    EXPECT_EQ(ip_q((HANDLE)-1, C_AFFINITY, &v, 8, &rl), ST_OK);
    CHECK(v == pm);
}
TEST(ip_affinity_of_child_is_the_system_mask) {
    if (!ip_init()) return;
    struct kid k; if (!kid_start(&k, "", 0, NULL, 0, NULL)) { t_skip("no child"); return; }
    kid_wait_ready(&k);
    unsigned long long v = 0; ULONG rl; DWORD_PTR pm, sm; HANDLE h = kid_open(&k, PROC_LIM);
    CHECK(GetProcessAffinityMask(h, &pm, &sm));
    EXPECT_EQ(ip_q(h, C_AFFINITY, &v, 8, &rl), ST_OK);
    CHECK(v == pm && pm == sm);
    CloseHandle(h); kid_stop(&k);
}
TEST(ip_affinity_equals_basic_information_field) {
    if (!ip_init()) return;
    unsigned long long pbi[8] = { 0 }, v = 0; ULONG rl;
    EXPECT_EQ(ip_q((HANDLE)-1, 0, pbi, 0x30, &rl), ST_OK);
    EXPECT_EQ(ip_q((HANDLE)-1, C_AFFINITY, &v, 8, &rl), ST_OK);
    CHECK(pbi[2] == v);
}

/* ---- 47 group list ------------------------------------------------------------------------------------------ */
TEST(ip_groups_buffer_too_small) {
    if (!ip_init()) return;
    unsigned char *b = ip_buf(); ULONG rl; ip_fill(b, 0x20);
    EXPECT_EQ(ip_q((HANDLE)-1, C_GROUPS, b, 0, &rl), ST_BUF_TOO_SMALL);
    CHECK(rl == 2 && ip_all_filler(b, 0x20));
    EXPECT_EQ(ip_q((HANDLE)-1, C_GROUPS, NULL, 0, &rl), ST_BUF_TOO_SMALL);
    CHECK(rl == 2);
}
TEST(ip_groups_one_group_zero) {
    if (!ip_init()) return;
    unsigned char *b = ip_buf(); ULONG rl; ip_fill(b, 0x20);
    EXPECT_EQ(ip_q((HANDLE)-1, C_GROUPS, b, 2, &rl), ST_OK);
    CHECK(rl == 2 && *(USHORT *)b == 0 && ip_all_filler(b + 2, 0x1e));
    ip_fill(b, 0x2000);
    EXPECT_EQ(ip_q((HANDLE)-1, C_GROUPS, b, 0x1000, &rl), ST_OK);
    CHECK(rl == 2 && *(USHORT *)b == 0 && ip_all_filler(b + 2, 0x1ffe));
}
TEST(ip_groups_return_length_pointer_is_mandatory) {
    if (!ip_init()) return;
    unsigned char *b = ip_buf(); ip_fill(b, 0x20);
    EXPECT_EQ(ip_q_nullrl((HANDLE)-1, C_GROUPS, b, 2), ST_INVALID_PARAM);
    EXPECT_EQ(ip_q_nullrl((HANDLE)-1, C_GROUPS, b, 0), ST_INVALID_PARAM);
    EXPECT_EQ(ip_q_nullrl((HANDLE)-1, C_GROUPS, b, 0x10), ST_INVALID_PARAM);
    CHECK(ip_all_filler(b, 0x20));
}
TEST(ip_groups_odd_length_is_invalid_parameter) {
    if (!ip_init()) return;
    unsigned char *b = ip_buf(); ULONG rl; ULONG lens[] = { 1, 3, 5, 7, 0x1001 };
    for (int i = 0; i < 5; i++) {
        ip_fill(b, 0x20);
        EXPECT_EQ(ip_q((HANDLE)-1, C_GROUPS, b, lens[i], &rl), ST_INVALID_PARAM);
        CHECK(rl == RL_SENT && ip_all_filler(b, 0x20));
        EXPECT_EQ(ip_q(NULL, C_GROUPS, b, lens[i], &rl), ST_INVALID_PARAM);   /* before the handle */
    }
}
TEST(ip_groups_handle_checks) {
    if (!ip_init()) return;
    unsigned char *b = ip_buf(); ULONG rl; HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    ULONG lens[] = { 0, 2, 8 };
    for (int i = 0; i < 3; i++) {
        EXPECT_EQ(ip_q(NULL, C_GROUPS, b, lens[i], &rl), ST_INVALID_HANDLE);
        EXPECT_EQ(ip_q((HANDLE)(LONG_PTR)-2, C_GROUPS, b, lens[i], &rl), ST_TYPE_MISMATCH);
        EXPECT_EQ(ip_q(ev, C_GROUPS, b, lens[i], &rl), ST_TYPE_MISMATCH);
    }
    CloseHandle(ev);
    struct kid k; if (!kid_start(&k, "", 0, NULL, 0, NULL)) return;
    HANDLE sy = kid_open(&k, SYNCHRONIZE), li = kid_open(&k, PROC_LIM);
    EXPECT_EQ(ip_q(sy, C_GROUPS, b, 2, &rl), ST_ACCESS_DENIED);
    EXPECT_EQ(ip_q(sy, C_GROUPS, b, 0, &rl), ST_ACCESS_DENIED);
    EXPECT_EQ(ip_q(li, C_GROUPS, b, 2, &rl), ST_OK);
    EXPECT_EQ(ip_q(li, C_GROUPS, b, 0, &rl), ST_BUF_TOO_SMALL);
    CHECK(rl == 2);
    CloseHandle(sy); CloseHandle(li); kid_stop(&k);
}

/* ---- 66 / 67 cpu sets ---------------------------------------------------------------------------------------- */
static void cpuset_lengths(ULONG cls) {
    if (!ip_init()) return;
    unsigned char *b = ip_buf(); ULONG rl;
    ULONG good[] = { 0, 8, 0x10, 0x20, 0x98, 0xa0 };
    for (int i = 0; i < 6; i++) {
        ip_fill(b, 0x200);
        LONG st = ip_q((HANDLE)-1, cls, b, good[i], &rl);
        CHECKF(st == ST_OK && rl == 8, "class %lu len 0x%lx: 0x%lx rl %lu", cls, good[i], (unsigned long)st, rl);
        CHECKF(ip_all_filler(b + good[i], 0x200 - good[i]), "class %lu len 0x%lx: wrote past the buffer", cls, good[i]);
    }
    ULONG bad[] = { 1, 2, 4, 7, 9, 0xa1, 0xa8, 0xb0, 0x1000 };
    for (int i = 0; i < 9; i++) {
        ip_fill(b, 0x200);
        LONG st = ip_q((HANDLE)-1, cls, b, bad[i], &rl);
        CHECKF(st == ST_INFO_LEN && rl == RL_SENT, "class %lu len 0x%lx: 0x%lx rl %lu", cls, bad[i], (unsigned long)st, rl);
    }
}
TEST(ip_default_cpu_sets_length_rule) { cpuset_lengths(C_DEFCPUSETS); }
TEST(ip_allowed_cpu_sets_length_rule) { cpuset_lengths(C_ALLOWCPUSETS); }
static void cpuset_handle_order(ULONG cls) {
    if (!ip_init()) return;
    unsigned char *b = ip_buf(); ULONG rl;
    /* the length rule precedes the handle; an acceptable length (even 0) then reaches the handle */
    EXPECT_EQ(ip_q(NULL, cls, b, 4, &rl), ST_INFO_LEN);
    EXPECT_EQ(ip_q(NULL, cls, b, 0xa8, &rl), ST_INFO_LEN);
    EXPECT_EQ(ip_q(NULL, cls, b, 0, &rl), ST_INVALID_HANDLE);
    EXPECT_EQ(ip_q(NULL, cls, b, 8, &rl), ST_INVALID_HANDLE);
    EXPECT_EQ(ip_q((HANDLE)(LONG_PTR)-2, cls, b, 8, &rl), ST_TYPE_MISMATCH);
    struct kid k; if (!kid_start(&k, "", 0, NULL, 0, NULL)) return;
    HANDLE sy = kid_open(&k, SYNCHRONIZE), li = kid_open(&k, PROC_LIM);
    EXPECT_EQ(ip_q(sy, cls, b, 8, &rl), ST_ACCESS_DENIED);
    EXPECT_EQ(ip_q(li, cls, b, 8, &rl), ST_OK);
    CHECK(rl == 8);
    CloseHandle(sy); CloseHandle(li); kid_stop(&k);
}
TEST(ip_default_cpu_sets_handle_order) { cpuset_handle_order(C_DEFCPUSETS); }
TEST(ip_allowed_cpu_sets_handle_order) { cpuset_handle_order(C_ALLOWCPUSETS); }
TEST(ip_default_cpu_sets_null_buffer_and_return_length) {
    if (!ip_init()) return;
    ULONG rl;
    EXPECT_EQ(ip_q((HANDLE)-1, C_DEFCPUSETS, NULL, 0, &rl), ST_OK);
    CHECK(rl == 8);
    EXPECT_EQ(ip_q_nullrl((HANDLE)-1, C_DEFCPUSETS, ip_buf(), 8), ST_OK);
    EXPECT_EQ(ip_q((HANDLE)-1, C_ALLOWCPUSETS, NULL, 0, &rl), ST_OK);
    CHECK(rl == 8);
}
TEST(ip_default_cpu_sets_value_is_empty_without_assignment) {
    if (!ip_init()) return;
    typedef BOOL (WINAPI *GDCS)(HANDLE, PULONG, ULONG, PULONG);
    GDCS f = (GDCS)t_proc_in("kernel32.dll", "GetProcessDefaultCpuSets");
    if (!f) { t_skip("GetProcessDefaultCpuSets not exported"); return; }
    ULONG req = 99; f(GetCurrentProcess(), NULL, 0, &req);
    if (req != 0) { t_skip("the process has an explicit default cpu set"); return; }
    unsigned long long v = 0x1111; ULONG rl;
    EXPECT_EQ(ip_q((HANDLE)-1, C_DEFCPUSETS, &v, 8, &rl), ST_OK);
    CHECK(v == 0);
}

/* ---- 97 leap second information ------------------------------------------------------------------------------ */
TEST(ip_leap_second_pseudo_handle) {
    if (!ip_init()) return;
    ULONG v[2] = { 0xAAAAAAAA, 0xAAAAAAAA }, rl;
    EXPECT_EQ(ip_q((HANDLE)-1, C_LEAP, v, 8, &rl), ST_OK);
    CHECK(rl == 8);
    CHECKF((v[0] & ~1u) == 0 && v[1] == 0, "flags 0x%lx reserved 0x%lx", v[0], v[1]);
}
TEST(ip_leap_second_length_mismatch_reports_8) {
    if (!ip_init()) return;
    ULONG v[8]; ULONG rl; ULONG lens[] = { 0, 4, 12, 16, 0x20 };
    for (int i = 0; i < 5; i++) {
        ip_fill(v, sizeof v);
        EXPECT_EQ(ip_q((HANDLE)-1, C_LEAP, v, lens[i], &rl), ST_INFO_LEN);
        CHECKF(rl == 8, "len %lu: return length %lu", lens[i], rl);          /* written although the call failed */
        CHECK(ip_all_filler(v, sizeof v));
    }
    EXPECT_EQ(ip_q(NULL, C_LEAP, v, 12, &rl), ST_INFO_LEN);                  /* the length is checked first */
    CHECK(rl == 8);
    EXPECT_EQ(ip_q_nullrl((HANDLE)-1, C_LEAP, v, 4), ST_INFO_LEN);
}
TEST(ip_leap_second_only_the_pseudo_handle_is_accepted) {
    if (!ip_init()) return;
    ULONG v[2], rl; HANDLE self = ip_dup_self(); HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    struct kid k; int have = kid_start(&k, "", 0, NULL, 0, NULL);
    HANDLE hs[] = { self, NULL, (HANDLE)(ULONG_PTR)0x7ffc0000, (HANDLE)(LONG_PTR)-2, ev, have ? k.h : NULL };
    for (int i = 0; i < 6; i++) {
        if (!hs[i] && i != 1) continue;
        ip_fill(v, sizeof v);
        LONG st = ip_q(hs[i], C_LEAP, v, 8, &rl);
        CHECKF(st == ST_INVALID_PARAM && rl == RL_SENT && ip_all_filler(v, sizeof v), "handle #%d: 0x%lx", i, (unsigned long)st);
    }
    ULONG one = 0xAAAAAAAA; ip_fill(v, 8);
    EXPECT_EQ(ip_q_nullrl((HANDLE)-1, C_LEAP, v, 8), ST_OK);
    if (self) { CloseHandle(self); }
    CloseHandle(ev);
    if (have) { kid_stop(&k); }
}

/* ---- 89 image section handle --------------------------------------------------------------------------------- */
TEST(ip_section_handle_is_a_new_image_section) {
    if (!ip_init()) return;
    HANDLE s = (HANDLE)(ULONG_PTR)0xAAAAAAAAAAAAAAAAull; ULONG rl;
    EXPECT_EQ(ip_q((HANDLE)-1, C_SECTION, &s, 8, &rl), ST_OK);
    CHECK(rl == 8);
    CHECK(s != NULL && (ULONG_PTR)s != 0xAAAAAAAAAAAAAAAAull);
    DWORD fl = 0xFF; CHECK(GetHandleInformation(s, &fl)); CHECKF(fl == 0, "handle flags 0x%lx", fl);
    NtQueryObject_t qo = (NtQueryObject_t)t_proc_in("ntdll.dll", "NtQueryObject");
    if (qo) {
        static unsigned char tb[512]; ULONG r2;
        LONG st = qo(s, 2, tb, sizeof tb, &r2);                    /* object type information */
        CHECK(st == 0 && *(USHORT *)tb == 14 && !memcmp(*(WCHAR **)(tb + 8), L"Section", 14));
    }
    NtQuerySection_t qs = (NtQuerySection_t)t_proc_in("ntdll.dll", "NtQuerySection");
    if (qs) {
        unsigned long long sec[8] = { 0 }; SIZE_T r2 = 0; unsigned long long ii[8] = { 0 };
        LONG st = qs(s, 1, sec, 0x40, &r2);                         /* section image information */
        CHECKF(st == 0, "NtQuerySection image information 0x%lx", (unsigned long)st);
        EXPECT_EQ(ip_q((HANDLE)-1, C_IMAGEINFO, ii, 0x40, &rl), ST_OK);
        CHECK(sec[0] == ii[0]);                                     /* same entry point */
    }
    CHECK(CloseHandle(s));
}
TEST(ip_section_handle_each_call_gives_a_new_handle) {
    if (!ip_init()) return;
    HANDLE a = NULL, b = NULL; ULONG rl;
    EXPECT_EQ(ip_q((HANDLE)-1, C_SECTION, &a, 8, &rl), ST_OK);
    EXPECT_EQ(ip_q((HANDLE)-1, C_SECTION, &b, 8, &rl), ST_OK);
    CHECK(a && b && a != b);
    CHECK(CloseHandle(a)); CHECK(CloseHandle(b));
    SetLastError(0); CHECK(!CloseHandle(a));
}
TEST(ip_section_handle_refused_for_everything_but_the_pseudo_handle) {
    if (!ip_init()) return;
    HANDLE self = ip_dup_self(), ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    struct kid k; int have = kid_start(&k, "", 0, NULL, 0, NULL);
    HANDLE hs[] = { self, NULL, (HANDLE)(ULONG_PTR)0x7ffc0000, (HANDLE)(LONG_PTR)-2, (HANDLE)(LONG_PTR)-3, ev, have ? k.h : NULL };
    for (int i = 0; i < 7; i++) {
        if (!hs[i] && i != 1) continue;
        HANDLE out = (HANDLE)(ULONG_PTR)0xAAAAAAAAAAAAAAAAull; ULONG rl;
        LONG st = ip_q(hs[i], C_SECTION, &out, 8, &rl);
        CHECKF(st == ST_ACCESS_DENIED && (ULONG_PTR)out == 0xAAAAAAAAAAAAAAAAull, "handle #%d: 0x%lx", i, (unsigned long)st);
    }
    if (self) { CloseHandle(self); }
    CloseHandle(ev);
    if (have) { kid_stop(&k); }
}
TEST(ip_section_handle_length_is_checked_before_the_handle) {
    if (!ip_init()) return;
    HANDLE out[2]; ULONG rl;
    EXPECT_EQ(ip_q(NULL, C_SECTION, out, 4, &rl), ST_INFO_LEN);
    EXPECT_EQ(ip_q(NULL, C_SECTION, out, 16, &rl), ST_INFO_LEN);
    EXPECT_EQ(ip_q((HANDLE)-1, C_SECTION, out, 16, &rl), ST_INFO_LEN);
}

/* ---- 37 image information ----------------------------------------------------------------------------------- */
TEST(ip_image_information_matches_the_pe_headers) {
    if (!ip_init()) return;
    unsigned char *base = (unsigned char *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    unsigned char *b = ip_buf(); ULONG rl; ip_fill(b, 0x60);
    EXPECT_EQ(ip_q((HANDLE)-1, C_IMAGEINFO, b, 0x40, &rl), ST_OK);
    CHECK(rl == 0x40 && ip_all_filler(b + 0x40, 0x20));
    unsigned long long *q = (unsigned long long *)b; ULONG *d = (ULONG *)b;
    CHECK(q[0] == (unsigned long long)(ULONG_PTR)(base + nt->OptionalHeader.AddressOfEntryPoint));  /* entry point */
    CHECK(q[1] == 0);                                                                              /* zero bits */
    CHECK(q[2] == nt->OptionalHeader.SizeOfStackReserve);
    CHECK(q[3] == nt->OptionalHeader.SizeOfStackCommit);
    CHECK(d[8] == nt->OptionalHeader.Subsystem);
    CHECK(*(USHORT *)(b + 0x24) == nt->OptionalHeader.MinorSubsystemVersion);
    CHECK(*(USHORT *)(b + 0x26) == nt->OptionalHeader.MajorSubsystemVersion);
    CHECK(d[10] == (ULONG)(nt->OptionalHeader.MajorOperatingSystemVersion | (nt->OptionalHeader.MinorOperatingSystemVersion << 16)));
    CHECK(*(USHORT *)(b + 0x2c) == nt->FileHeader.Characteristics);
    CHECK(*(USHORT *)(b + 0x2e) == nt->OptionalHeader.DllCharacteristics);
    CHECK(*(USHORT *)(b + 0x30) == IMAGE_FILE_MACHINE_AMD64);
    CHECK(b[0x32] == 1);                                                                           /* contains code */
    CHECK(d[13] == 0);                                                                             /* loader flags */
    CHECK(d[15] == nt->OptionalHeader.CheckSum);
    HANDLE f = CreateFileA(t_self_path(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) { CHECKF(d[14] == GetFileSize(f, NULL), "file size field 0x%lx", d[14]); CloseHandle(f); }
}
TEST(ip_image_information_of_a_child_of_the_same_image) {
    if (!ip_init()) return;
    struct kid k; if (!kid_start(&k, "", 0, NULL, 0, NULL)) { t_skip("no child"); return; }
    kid_wait_ready(&k);
    unsigned char a[0x40], c[0x40]; ULONG rl; HANDLE h = kid_open(&k, PROC_LIM);
    EXPECT_EQ(ip_q((HANDLE)-1, C_IMAGEINFO, a, 0x40, &rl), ST_OK);
    EXPECT_EQ(ip_q(h, C_IMAGEINFO, c, 0x40, &rl), ST_OK);
    CHECK(rl == 0x40);
    CHECK(!memcmp(a + 8, c + 8, 0x25));          /* everything but the entry point (relocated image) */
    CHECK(!memcmp(a + 0x34, c + 0x34, 0xc));
    CloseHandle(h); kid_stop(&k);
}

/* ---- 44 image file mapping ------------------------------------------------------------------------------------ */
static const char *sys_ntdll(void) {
    static char p[MAX_PATH + 16];
    if (!p[0]) { UINT n = GetSystemDirectoryA(p, MAX_PATH); if (!n || n >= MAX_PATH) { p[0] = 0; return "ntdll.dll"; } strcat(p, "\\ntdll.dll"); }
    return p;
}
static HANDLE open_exec(const char *path, DWORD access) {
    return CreateFileA(path, access, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
}
static LONG q44(HANDLE proc, HANDLE file, ULONG *rl) { unsigned long long v = (unsigned long long)(ULONG_PTR)file; return ip_q(proc, C_FILEMAP, &v, 8, rl); }
TEST(ip_file_mapping_matches_the_process_image) {
    if (!ip_init()) return;
    DWORD acc[] = { FILE_EXECUTE, FILE_EXECUTE | SYNCHRONIZE, FILE_EXECUTE | FILE_READ_DATA | SYNCHRONIZE, GENERIC_READ | GENERIC_EXECUTE };
    for (int i = 0; i < 4; i++) {
        HANDLE f = open_exec(t_self_path(), acc[i]); ULONG rl;
        if (f == INVALID_HANDLE_VALUE) { t_skip("open 0x%lx refused", acc[i]); continue; }
        LONG st = q44((HANDLE)-1, f, &rl);
        CHECKF(st == ST_OK, "access 0x%lx: 0x%lx", acc[i], (unsigned long)st);
        t_info("class 44 success return length %lu", rl);
        CloseHandle(f);
    }
}
TEST(ip_file_mapping_other_files_do_not_match) {
    if (!ip_init()) return;
    char tf[MAX_PATH], cp[MAX_PATH]; t_tmpfile(tf, sizeof tf, "ipdata.bin"); t_tmpfile(cp, sizeof cp, "ipcopy2.exe");
    HANDLE w = CreateFileA(tf, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL); DWORD n; WriteFile(w, "abc", 3, &n, NULL); CloseHandle(w);
    CopyFileA(t_self_path(), cp, FALSE);
    const char *paths[] = { sys_ntdll(), tf, cp, t_tmpdir() };
    for (int i = 0; i < 4; i++) {
        HANDLE f = open_exec(paths[i], GENERIC_READ | GENERIC_EXECUTE | SYNCHRONIZE); ULONG rl;
        if (f == INVALID_HANDLE_VALUE) { t_skip("open #%d refused", i); continue; }
        LONG st = q44((HANDLE)-1, f, &rl);
        CHECKF(st == ST_UNSUCCESSFUL, "file #%d: 0x%lx", i, (unsigned long)st);
        CloseHandle(f);
    }
    DeleteFileA(tf); DeleteFileA(cp);
}
TEST(ip_file_mapping_needs_execute_access_on_the_file) {
    if (!ip_init()) return;
    DWORD acc[] = { FILE_READ_DATA | SYNCHRONIZE, FILE_READ_ATTRIBUTES, FILE_READ_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE };
    for (int i = 0; i < 3; i++) {
        HANDLE f = open_exec(t_self_path(), acc[i]); ULONG rl;
        if (f == INVALID_HANDLE_VALUE) { t_skip("open 0x%lx refused", acc[i]); continue; }
        EXPECT_EQ(q44((HANDLE)-1, f, &rl), ST_ACCESS_DENIED);
        CloseHandle(f);
    }
}
TEST(ip_file_mapping_bad_file_handles) {
    if (!ip_init()) return;
    ULONG rl; HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    EXPECT_EQ(q44((HANDLE)-1, NULL, &rl), ST_INVALID_HANDLE);
    EXPECT_EQ(q44((HANDLE)-1, (HANDLE)(ULONG_PTR)0x7ffc0000, &rl), ST_INVALID_HANDLE);
    EXPECT_EQ(q44((HANDLE)-1, ev, &rl), ST_TYPE_MISMATCH);
    EXPECT_EQ(q44((HANDLE)-1, (HANDLE)(LONG_PTR)-1, &rl), ST_TYPE_MISMATCH);   /* a process pseudo handle is not a file */
    CloseHandle(ev);
}
TEST(ip_file_mapping_process_access_and_other_process) {
    if (!ip_init()) return;
    struct kid k; if (!kid_start(&k, "", 0, NULL, 0, NULL)) { t_skip("no child"); return; }
    kid_wait_ready(&k);
    HANDLE f = open_exec(t_self_path(), GENERIC_READ | GENERIC_EXECUTE | SYNCHRONIZE); ULONG rl;
    if (f == INVALID_HANDLE_VALUE) { kid_stop(&k); t_skip("open refused"); return; }
    DWORD deny[] = { PROC_LIM, PROCESS_VM_READ, PROCESS_VM_WRITE, SYNCHRONIZE };
    for (int i = 0; i < 4; i++) { HANDLE p = kid_open(&k, deny[i]); if (!p) continue; EXPECT_EQ(q44(p, f, &rl), ST_ACCESS_DENIED); CloseHandle(p); }
    HANDLE q = kid_open(&k, PROC_QRY);
    EXPECT_EQ(q44(q, f, &rl), ST_OK);                          /* the child runs the same file */
    CloseHandle(q);
    HANDLE n = open_exec(sys_ntdll(), GENERIC_READ | GENERIC_EXECUTE | SYNCHRONIZE);
    if (n != INVALID_HANDLE_VALUE) { q = kid_open(&k, PROC_QRY); EXPECT_EQ(q44(q, n, &rl), ST_UNSUCCESSFUL); CloseHandle(q); CloseHandle(n); }
    CloseHandle(f); kid_stop(&k);
}
TEST(ip_file_mapping_handle_errors_on_the_process_side) {
    if (!ip_init()) return;
    HANDLE f = open_exec(t_self_path(), GENERIC_READ | GENERIC_EXECUTE | SYNCHRONIZE); ULONG rl; HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (f == INVALID_HANDLE_VALUE) { t_skip("open refused"); return; }
    EXPECT_EQ(q44(NULL, f, &rl), ST_INVALID_HANDLE);
    EXPECT_EQ(q44((HANDLE)(ULONG_PTR)0x7ffc0000, f, &rl), ST_INVALID_HANDLE);
    EXPECT_EQ(q44((HANDLE)(LONG_PTR)-2, f, &rl), ST_TYPE_MISMATCH);
    EXPECT_EQ(q44(ev, f, &rl), ST_TYPE_MISMATCH);
    CloseHandle(f); CloseHandle(ev);
}
TEST(ip_file_mapping_input_handle_is_left_alone) {
    if (!ip_init()) return;
    HANDLE f = open_exec(t_self_path(), GENERIC_READ | GENERIC_EXECUTE | SYNCHRONIZE); ULONG rl;
    if (f == INVALID_HANDLE_VALUE) { t_skip("open refused"); return; }
    unsigned long long v = (unsigned long long)(ULONG_PTR)f;
    EXPECT_EQ(ip_q((HANDLE)-1, C_FILEMAP, &v, 8, &rl), ST_OK);
    CHECK(v == (unsigned long long)(ULONG_PTR)f);
    DWORD fl; CHECK(GetHandleInformation(f, &fl));
    CloseHandle(f);
}

/* ---- 23 device map -------------------------------------------------------------------------------------------- */
TEST(ip_device_map_matches_logical_drives) {
    if (!ip_init()) return;
    unsigned char *b = ip_buf(); ULONG rl; ip_fill(b, 0x60);
    EXPECT_EQ(ip_q((HANDLE)-1, C_DEVMAP, b, 0x24, &rl), ST_OK);
    CHECK(rl == 0x24 && ip_all_filler(b + 0x24, 0x3c));
    ULONG map = *(ULONG *)b; DWORD drives = GetLogicalDrives();
    CHECKF(map == drives, "map 0x%lx drives 0x%lx", map, drives);
    for (int i = 0; i < 26; i++) if (map & (1u << i)) {
        char root[4] = { (char)('A' + i), ':', '\\', 0 };
        CHECKF(b[4 + i] == GetDriveTypeA(root), "drive %c: type byte %u api %u", 'A' + i, b[4 + i], GetDriveTypeA(root));
    }
}
TEST(ip_device_map_long_form_with_flags) {
    if (!ip_init()) return;
    unsigned char *b = ip_buf(); ULONG rl;
    memset(b, 0, 0x60); ip_fill(b, 0x30); *(ULONG *)(b + 0x28) = 0;
    EXPECT_EQ(ip_q((HANDLE)-1, C_DEVMAP, b, 0x30, &rl), ST_OK);
    CHECK(rl == 0x30);
    CHECK(*(ULONG *)b == GetLogicalDrives());
    CHECK(*(ULONG *)(b + 0x28) == 0);
    memset(b, 0, 0x60); *(ULONG *)(b + 0x28) = 1;
    EXPECT_EQ(ip_q((HANDLE)-1, C_DEVMAP, b, 0x30, &rl), ST_OK);
    CHECK(rl == 0x30);
    t_info("class 23 flags=1 map=0x%lx", *(ULONG *)b);
}
TEST(ip_device_map_flags_other_than_bit0_are_invalid) {
    if (!ip_init()) return;
    unsigned char *b = ip_buf(); ULONG rl;
    ULONG flags[] = { 2, 3, 4, 0x10, 0x80000000, 0x80000001, 0xFFFFFFFF };
    for (int i = 0; i < 7; i++) {
        ip_fill(b, 0x60); *(ULONG *)(b + 0x28) = flags[i];
        unsigned char before[0x30]; memcpy(before, b, 0x30);
        LONG st = ip_q((HANDLE)-1, C_DEVMAP, b, 0x30, &rl);
        CHECKF(st == ST_INVALID_PARAM && rl == RL_SENT && !memcmp(before, b, 0x30), "flags 0x%lx: 0x%lx", flags[i], (unsigned long)st);
    }
}
TEST(ip_device_map_other_bytes_of_the_long_form_are_not_inputs) {
    if (!ip_init()) return;
    unsigned char *b = ip_buf(); ULONG rl;
    for (int off = 0x24; off < 0x30; off += 4) {
        if (off == 0x28) continue;
        memset(b, 0, 0x60); *(ULONG *)(b + off) = 0xFFFFFFFF;
        CHECKF(ip_q((HANDLE)-1, C_DEVMAP, b, 0x30, &rl) == ST_OK, "garbage at +0x%x", off);
    }
}
TEST(ip_device_map_short_form_ignores_trailing_bytes) {
    if (!ip_init()) return;
    unsigned char *b = ip_buf(); ULONG rl; ip_fill(b, 0x60);
    EXPECT_EQ(ip_q((HANDLE)-1, C_DEVMAP, b, 0x24, &rl), ST_OK);     /* trailing filler = flags 0xAAAAAAAA is not read */
}
TEST(ip_device_map_of_another_process_is_invalid_parameter) {
    if (!ip_init()) return;
    struct kid k; if (!kid_start(&k, "", 0, NULL, 0, NULL)) { t_skip("no child"); return; }
    kid_wait_ready(&k);
    unsigned char *b = ip_buf(); ULONG rl; HANDLE h = kid_open(&k, PROC_QRY);
    memset(b, 0, 0x60);
    EXPECT_EQ(ip_q(h, C_DEVMAP, b, 0x24, &rl), ST_INVALID_PARAM);
    EXPECT_EQ(ip_q(h, C_DEVMAP, b, 0x30, &rl), ST_INVALID_PARAM);
    CloseHandle(h); kid_stop(&k);
}

/* ---- 49 console host ------------------------------------------------------------------------------------------- */
TEST(ip_console_host_value_names_a_console_host_process) {
    if (!ip_init()) return;
    unsigned long long v = 0; ULONG rl;
    EXPECT_EQ(ip_q((HANDLE)-1, C_CONHOST, &v, 8, &rl), ST_OK);
    if (v == 0) { t_skip("the test process has no console"); return; }
    CHECKF((v & 1) == 1, "low bit clear: %llx", v);
    DWORD pid = (DWORD)(v & ~1ull);
    HANDLE sn = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0); PROCESSENTRY32 pe; pe.dwSize = sizeof pe; int found = 0;
    if (sn == INVALID_HANDLE_VALUE) { t_skip("no process snapshot"); return; }
    for (BOOL ok = Process32First(sn, &pe); ok; ok = Process32Next(sn, &pe)) if (pe.th32ProcessID == pid && !_stricmp(pe.szExeFile, "conhost.exe")) found = 1;
    CloseHandle(sn);
    CHECKF(found, "value minus its low bit is not the pid of a conhost process");
}
TEST(ip_console_host_is_inherited_by_a_child) {
    if (!ip_init()) return;
    unsigned long long mine = 0, theirs = 0xAA; ULONG rl;
    EXPECT_EQ(ip_q((HANDLE)-1, C_CONHOST, &mine, 8, &rl), ST_OK);
    if (!mine) { t_skip("the test process has no console"); return; }
    struct kid k; if (!kid_start(&k, "", 0, NULL, 0, NULL)) { t_skip("no child"); return; }
    kid_wait_ready(&k);
    HANDLE h = kid_open(&k, PROC_LIM);
    EXPECT_EQ(ip_q(h, C_CONHOST, &theirs, 8, &rl), ST_OK);
    CHECK(theirs == mine);
    CloseHandle(h); kid_stop(&k);
}
TEST(ip_console_host_detached_child_has_only_the_flag) {
    if (!ip_init()) return;
    char out[4096]; DWORD code = 99;
    if (!ip_run_child("selfq", "", DETACHED_PROCESS, 0, NULL, out, sizeof out, &code)) { t_skip("cannot start child"); return; }
    CHECKF(code == 0, "child reported %lu: %s", code, out);
    const char *p = strstr(out, "CONHOST st=0 value=");
    CHECKF(p != NULL, "no console host line: %s", out);
    if (p) { unsigned long long v = strtoull(p + 19, NULL, 16); CHECKF(v == 1, "value 0x%llx", v); }
}
TEST(ip_console_host_value_of_system_process) {
    if (!ip_init()) return;
    HANDLE h = OpenProcess(PROC_LIM, FALSE, 4);
    if (!h) { t_skip("cannot open the System process (restricted token)"); return; }
    unsigned long long v = 0xAA; ULONG rl; LONG st = ip_q(h, C_CONHOST, &v, 8, &rl);
    t_info("system console host: 0x%lx value 0x%llx", (unsigned long)st, v);
    CHECK(st == ST_OK);
    CloseHandle(h);
}
