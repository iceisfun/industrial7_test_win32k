/* x_ntqip_qip_image_session_peb: processes that have exited, the System process, handles with only a synchronize
 * right, and the default CPU set mask (Windows 10 22H2 x64).
 *
 * Measured here (all on a real handle to another process, not the pseudo handle):
 *   - what a process that has exited still reports: session, affinity mask, wow64 pointer, luid-device-maps flag,
 *     group information, console host pid, cpu set masks and the NT image path survive; the wow64-x86 flag needs
 *     the full query right, the device map is refused with an invalid-parameter status, the leap second class and
 *     the image section class are refused for every handle but the pseudo handle;
 *   - the System process answers some classes with an empty or constant result and others with a distinct error;
 *   - a handle that only carries SYNCHRONIZE is refused by every class that looks at the process;
 *   - the default CPU set class reports the assigned sets as a processor mask (one bit per assigned set), the
 *     allowed CPU set class is not changed by that assignment, and the assignment is visible through a handle
 *     that only has the limited query right. */
#include "ip.h"
#include <wchar.h>

typedef BOOL (WINAPI *get_cpuset_info_t)(PVOID, ULONG, PULONG, HANDLE, ULONG);
typedef BOOL (WINAPI *set_default_cpusets_t)(HANDLE, const ULONG *, ULONG);
typedef BOOL (WINAPI *get_default_cpusets_t)(HANDLE, PULONG, ULONG, PULONG);

static unsigned long long popcnt(unsigned long long v) { unsigned long long n = 0; while (v) { n += v & 1; v >>= 1; } return n; }

/* ---- processes that have exited ---------------------------------------------------------------------------- */
struct snap { ULONG sess, luid, conhost_lo; unsigned long long aff, wow, defset, allowed; USHORT grp; WCHAR nt[MAX_PATH + 8]; };

static int take_snap(HANDLE h, struct snap *s) {
    ULONG rl; unsigned long long v8 = 0xAAAA; ULONG v4 = 0xAAAA; unsigned char *b = ip_buf(); int ok = 1; LONG st;
#define SNAPQ(cls, buf, len) do { st = ip_q(h, cls, buf, len, &rl); if (st != ST_OK) { t_note("snap class %d status 0x%lx", (int)(cls), (unsigned long)st); ok = 0; } } while (0)
    SNAPQ(C_SESSION, &v4, 4); s->sess = v4;
    SNAPQ(C_LUIDMAPS, &v4, 4); s->luid = v4;
    SNAPQ(C_AFFINITY, &v8, 8); s->aff = v8;
    SNAPQ(C_WOW64, &v8, 8); s->wow = v8;
    SNAPQ(C_CONHOST, &v8, 8); s->conhost_lo = (ULONG)v8;
    SNAPQ(C_DEFCPUSETS, &v8, 8); s->defset = v8;
    SNAPQ(C_ALLOWCPUSETS, &v8, 8); s->allowed = v8;
    ULONG g = 0xAAAAAAAA; SNAPQ(C_GROUPS, &g, 2); s->grp = (USHORT)g;
    SNAPQ(C_NTPATH, b, 0x1000);
    if (ok) { const IPUS *u = (const IPUS *)b; wcsncpy(s->nt, u->Buffer, MAX_PATH); s->nt[MAX_PATH] = 0; }
    return ok;
}

TEST(ip_exited_process_keeps_session_affinity_and_flags) {
    if (!ip_init()) return;
    struct kid k; if (!kid_start(&k, "", 0, NULL, 0, NULL)) { t_skip("cannot start a child"); return; }
    kid_wait_ready(&k);
    HANDLE all = kid_open(&k, PROCESS_ALL_ACCESS), lim = kid_open(&k, PROC_LIM);
    if (!all || !lim) { t_skip("cannot open the child"); kid_stop(&k); return; }
    struct snap a, b;
    CHECK(take_snap(lim, &a));
    TerminateProcess(k.h, 3); WaitForSingleObject(k.h, 10000);
    CHECK(take_snap(lim, &b));
    CHECK(a.sess == b.sess);
    CHECK(a.luid == b.luid && b.luid == 1);
    CHECK(a.aff == b.aff && b.aff != 0);
    CHECK(a.wow == 0 && b.wow == 0);
    CHECK(a.conhost_lo == b.conhost_lo);
    CHECK(a.defset == b.defset);
    CHECK(a.allowed == b.allowed);
    CHECK(a.grp == b.grp);
    CHECK(!_wcsicmp(a.nt, b.nt) && b.nt[0] != 0);
    CHECK(take_snap(all, &b));
    CHECK(a.sess == b.sess && a.aff == b.aff);
    CloseHandle(all); CloseHandle(lim); kid_stop(&k);
}

TEST(ip_exited_process_wow64_x86_flag_needs_the_full_query_right) {
    if (!ip_init()) return;
    struct kid k; if (!kid_start(&k, "", 0, NULL, 0, NULL)) { t_skip("cannot start a child"); return; }
    kid_wait_ready(&k);
    HANDLE qry = kid_open(&k, PROC_QRY), lim = kid_open(&k, PROC_LIM);
    if (!qry || !lim) { t_skip("cannot open the child"); kid_stop(&k); return; }
    TerminateProcess(k.h, 3); WaitForSingleObject(k.h, 10000);
    ULONG v = 0xAAAA, rl;
    EXPECT_EQ(ip_q(lim, C_WOW86FLAG, &v, 4, &rl), ST_ACCESS_DENIED);
    CHECK(v == 0xAAAA && rl == RL_SENT);
    EXPECT_EQ(ip_q(qry, C_WOW86FLAG, &v, 4, &rl), ST_OK);
    CHECK(v == 0 && rl == 4);
    CloseHandle(qry); CloseHandle(lim); kid_stop(&k);
}

TEST(ip_exited_process_device_map_leap_second_and_section) {
    if (!ip_init()) return;
    struct kid k; if (!kid_start(&k, "", 0, NULL, 0, NULL)) { t_skip("cannot start a child"); return; }
    kid_wait_ready(&k);
    HANDLE all = kid_open(&k, PROCESS_ALL_ACCESS), lim = kid_open(&k, PROC_LIM);
    if (!all || !lim) { t_skip("cannot open the child"); kid_stop(&k); return; }
    TerminateProcess(k.h, 3); WaitForSingleObject(k.h, 10000);
    unsigned char *b = ip_buf(); ULONG rl; ip_fill(b, 0x100);
    EXPECT_EQ(ip_q(all, C_DEVMAP, b, 0x24, &rl), ST_INVALID_PARAM);
    EXPECT_EQ(ip_q(lim, C_DEVMAP, b, 0x24, &rl), ST_ACCESS_DENIED);
    EXPECT_EQ(ip_q(all, C_LEAP, b, 8, &rl), ST_INVALID_PARAM);
    EXPECT_EQ(ip_q(lim, C_LEAP, b, 8, &rl), ST_INVALID_PARAM);
    EXPECT_EQ(ip_q(all, C_SECTION, b, 8, &rl), ST_ACCESS_DENIED);
    EXPECT_EQ(ip_q(lim, C_SECTION, b, 8, &rl), ST_ACCESS_DENIED);
    CHECK(rl == RL_SENT && ip_all_filler(b, 0x100));
    CloseHandle(all); CloseHandle(lim); kid_stop(&k);
}

TEST(ip_exited_process_string_and_image_classes_full_matrix) {
    if (!ip_init()) return;
    struct kid k; if (!kid_start(&k, "", 0, NULL, 0, NULL)) { t_skip("cannot start a child"); return; }
    kid_wait_ready(&k);
    HANDLE all = kid_open(&k, PROCESS_ALL_ACCESS), lim = kid_open(&k, PROC_LIM);
    if (!all || !lim) { t_skip("cannot open the child"); kid_stop(&k); return; }
    TerminateProcess(k.h, 3); WaitForSingleObject(k.h, 10000);
    static unsigned char b[0x1000]; ULONG rl;
    HANDLE hs[2] = { lim, all };
    for (int i = 0; i < 2; i++) {
        EXPECT_EQ(ip_q(hs[i], C_NTPATH, b, sizeof b, &rl), ST_OK);
        EXPECT_EQ(ip_q(hs[i], C_DOSPATH, b, sizeof b, &rl), ST_UNSUCCESSFUL);
        EXPECT_EQ(ip_q(hs[i], C_CMDLINE, b, sizeof b, &rl), ST_TERMINATING);
        EXPECT_EQ(ip_q(hs[i], C_WININFO, b, sizeof b, &rl), ST_TERMINATING);
        EXPECT_EQ(ip_q(hs[i], C_IMAGEINFO, b, 0x40, &rl), ST_TERMINATING);
    }
    CloseHandle(all); CloseHandle(lim); kid_stop(&k);
}

TEST(ip_exited_process_length_rules_still_apply) {
    if (!ip_init()) return;
    struct kid k; if (!kid_start(&k, "", 0, NULL, 0, NULL)) { t_skip("cannot start a child"); return; }
    kid_wait_ready(&k);
    HANDLE lim = kid_open(&k, PROC_LIM);
    if (!lim) { t_skip("cannot open the child"); kid_stop(&k); return; }
    TerminateProcess(k.h, 3); WaitForSingleObject(k.h, 10000);
    unsigned char *b = ip_buf(); ULONG rl;
    ip_fill(b, 0x100);
    EXPECT_EQ(ip_q(lim, C_SESSION, b, 3, &rl), ST_INFO_LEN);
    EXPECT_EQ(ip_q(lim, C_SESSION, b, 8, &rl), ST_INFO_LEN);
    EXPECT_EQ(ip_q(lim, C_AFFINITY, b, 4, &rl), ST_INFO_LEN);
    EXPECT_EQ(ip_q(lim, C_WOW64, b, 4, &rl), ST_INFO_LEN);
    EXPECT_EQ(ip_q(lim, C_IMAGEINFO, b, 0x3f, &rl), ST_INFO_LEN);
    CHECK(rl == RL_SENT && ip_all_filler(b, 0x100));
    ULONG need = 0;
    EXPECT_EQ(ip_q(lim, C_NTPATH, b, 4, &need), ST_INFO_LEN);
    CHECK(need > 16 && need != RL_SENT);
    EXPECT_EQ(ip_q(lim, C_NTPATH, b, need, &rl), ST_OK);
    CHECK(rl == need);
    CloseHandle(lim); kid_stop(&k);
}

/* ---- the System process ------------------------------------------------------------------------------------ */
static HANDLE open_system(void) { return OpenProcess(PROC_LIM, FALSE, 4); }

TEST(ip_system_process_has_no_image_information) {
    HANDLE h = open_system();
    if (!h) { t_skip("cannot open the System process (restricted token)"); return; }
    if (!ip_init()) { CloseHandle(h); return; }
    unsigned char *b = ip_buf(); ULONG rl; ip_fill(b, 0x100);
    EXPECT_EQ(ip_q(h, C_IMAGEINFO, b, 0x40, &rl), ST_INVALID_PARAM);
    EXPECT_EQ(ip_q(h, C_DOSPATH, b, 0x100, &rl), ST_UNSUCCESSFUL);
    EXPECT_EQ(ip_q(h, C_CMDLINE, b, 0x100, &rl), ST_NOT_FOUND);
    EXPECT_EQ(ip_q(h, C_WININFO, b, 0x100, &rl), (LONG)0xC0000005);
    CHECK(rl == RL_SENT && ip_all_filler(b, 0x100));
    CloseHandle(h);
}

TEST(ip_system_process_small_classes) {
    HANDLE h = open_system();
    if (!h) { t_skip("cannot open the System process (restricted token)"); return; }
    if (!ip_init()) { CloseHandle(h); return; }
    ULONG rl, v4; unsigned long long v8;
    v8 = 0x1111; EXPECT_EQ(ip_q(h, C_WOW64, &v8, 8, &rl), ST_OK); CHECK(v8 == 0 && rl == 8);
    v4 = 0x1111; EXPECT_EQ(ip_q(h, C_LUIDMAPS, &v4, 4, &rl), ST_OK); CHECK(v4 == 1 && rl == 4);
    ULONG g = 0x11111111; EXPECT_EQ(ip_q(h, C_GROUPS, &g, 2, &rl), ST_OK); CHECK((g & 0xFFFF) == 0 && (g >> 16) == 0x1111 && rl == 2);
    v8 = 0; EXPECT_EQ(ip_q(h, C_AFFINITY, &v8, 8, &rl), ST_OK);
    DWORD_PTR pm = 0, sm = 0;
    CHECK(GetProcessAffinityMask(GetCurrentProcess(), &pm, &sm));
    CHECKF(v8 != 0 && (v8 & ~(unsigned long long)sm) == 0, "System affinity is not a non-empty subset of the system mask");
    v8 = 0x1111; EXPECT_EQ(ip_q(h, C_DEFCPUSETS, &v8, 8, &rl), ST_OK); CHECK(v8 == 0);
    v8 = 0; EXPECT_EQ(ip_q(h, C_ALLOWCPUSETS, &v8, 8, &rl), ST_OK);
    CHECKF(v8 != 0 && (v8 & ~(unsigned long long)sm) == 0, "System allowed cpu set mask is not a non-empty subset of the system mask");
    t_info("system allowed mask 0x%llx", v8);
    ULONG v = 0xAAAA; EXPECT_EQ(ip_q(h, C_WOW86FLAG, &v, 4, &rl), ST_ACCESS_DENIED);
    CloseHandle(h);
}

/* ---- handles that carry only SYNCHRONIZE -------------------------------------------------------------------- */
TEST(ip_synchronize_only_handle_is_refused_by_every_looking_class) {
    if (!ip_init()) return;
    struct kid k; if (!kid_start(&k, "", 0, NULL, 0, NULL)) { t_skip("cannot start a child"); return; }
    kid_wait_ready(&k);
    HANDLE sy = kid_open(&k, SYNCHRONIZE);
    if (!sy) { t_skip("cannot open the child"); kid_stop(&k); return; }
    static const struct { ULONG id; ULONG len; } T[] = {
        { C_WOW86FLAG, 4 }, { C_AFFINITY, 8 }, { C_DEVMAP, 0x24 }, { C_SESSION, 4 }, { C_WOW64, 8 }, { C_NTPATH, 0x400 },
        { C_IMAGEINFO, 0x40 }, { C_DOSPATH, 0x400 }, { C_GROUPS, 2 }, { C_CONHOST, 8 }, { C_WININFO, 0x400 },
        { C_CMDLINE, 0x400 }, { C_DEFCPUSETS, 8 }, { C_ALLOWCPUSETS, 8 },
    };
    static unsigned char b[0x400];
    for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) {
        ULONG rl; memset(b, FILLB, sizeof b);
        LONG st = ip_q(sy, T[i].id, b, T[i].len, &rl);
        CHECKF(st == ST_ACCESS_DENIED, "class %lu: status 0x%lx", T[i].id, (unsigned long)st);
        CHECKF(rl == RL_SENT && ip_all_filler(b, sizeof b), "class %lu: output written", T[i].id);
    }
    ULONG v = 0xAAAA, rl;     /* no handle lookup at all */
    EXPECT_EQ(ip_q(sy, C_LUIDMAPS, &v, 4, &rl), ST_OK); CHECK(v == 1);
    CloseHandle(sy); kid_stop(&k);
}

TEST(ip_real_handle_to_self_with_limited_right_follows_the_right_not_the_identity) {
    if (!ip_init()) return;
    HANDLE lim = OpenProcess(PROC_LIM, FALSE, GetCurrentProcessId());
    if (!lim) { t_skip("cannot open self"); return; }
    unsigned char *b = ip_buf(); ULONG rl; ULONG v4 = 0; unsigned long long v8 = 0;
    EXPECT_EQ(ip_q(lim, C_WOW86FLAG, &v4, 4, &rl), ST_ACCESS_DENIED);       /* the pseudo handle is accepted */
    EXPECT_EQ(ip_q((HANDLE)-1, C_WOW86FLAG, &v4, 4, &rl), ST_OK);
    EXPECT_EQ(ip_q(lim, C_DEVMAP, b, 0x24, &rl), ST_ACCESS_DENIED);
    EXPECT_EQ(ip_q((HANDLE)-1, C_DEVMAP, b, 0x24, &rl), ST_OK);
    EXPECT_EQ(ip_q(lim, C_SESSION, &v4, 4, &rl), ST_OK);
    EXPECT_EQ(ip_q(lim, C_AFFINITY, &v8, 8, &rl), ST_OK);
    EXPECT_EQ(ip_q(lim, C_IMAGEINFO, b, 0x40, &rl), ST_OK);
    EXPECT_EQ(ip_q(lim, C_CMDLINE, b, 0x1000, &rl), ST_OK);
    EXPECT_EQ(ip_q(lim, C_WININFO, b, 0x1000, &rl), ST_OK);
    EXPECT_EQ(ip_q(lim, C_SECTION, &v8, 8, &rl), ST_ACCESS_DENIED);         /* only the pseudo handle gives a section */
    EXPECT_EQ(ip_q(lim, C_LEAP, &v8, 8, &rl), ST_INVALID_PARAM);
    CloseHandle(lim);
}

/* ---- default and allowed CPU set masks ---------------------------------------------------------------------- */
static int cpuset_ids(ULONG *ids, int cap) {
    get_cpuset_info_t gi = (get_cpuset_info_t)t_proc_in("kernel32.dll", "GetSystemCpuSetInformation");
    if (!gi) return 0;
    ULONG need = 0; gi(NULL, 0, &need, GetCurrentProcess(), 0);
    if (!need) return 0;
    unsigned char *buf = (unsigned char *)malloc(need); ULONG got = 0; int n = 0;
    if (!gi(buf, need, &got, GetCurrentProcess(), 0)) { free(buf); return 0; }
    for (ULONG o = 0; o + 12 <= got && n < cap; ) {
        ULONG sz = *(ULONG *)(buf + o); if (!sz) break;
        /* only records of the first processor group: the masks are group-relative */
        if (*(ULONG *)(buf + o + 4) == 0 && *(USHORT *)(buf + o + 12) == 0) ids[n++] = *(ULONG *)(buf + o + 8);
        o += sz;
    }
    free(buf);
    return n;
}

TEST(ip_default_cpu_sets_mask_counts_the_assigned_sets) {
    if (!ip_init()) return;
    set_default_cpusets_t sd = (set_default_cpusets_t)t_proc_in("kernel32.dll", "SetProcessDefaultCpuSets");
    get_default_cpusets_t gd = (get_default_cpusets_t)t_proc_in("kernel32.dll", "GetProcessDefaultCpuSets");
    ULONG ids[8]; int n = sd && gd ? cpuset_ids(ids, 8) : 0;
    if (n < 2) { t_skip("fewer than two cpu sets in group 0 or no cpu set api"); return; }
    ULONG req = 99; gd(GetCurrentProcess(), NULL, 0, &req);
    if (req != 0) { t_skip("the process already has an explicit default cpu set"); return; }
    unsigned long long m = 0x7777, allowed0 = 0, allowed1 = 0; ULONG rl;
    EXPECT_EQ(ip_q((HANDLE)-1, C_ALLOWCPUSETS, &allowed0, 8, &rl), ST_OK);
    for (int k = 1; k <= 2; k++) {
        BOOL r = sd(GetCurrentProcess(), ids, (ULONG)k);
        CHECKF(r, "SetProcessDefaultCpuSets(%d) failed: %lu", k, GetLastError());
        if (!r) break;
        m = 0x7777; rl = 0;
        EXPECT_EQ(ip_q((HANDLE)-1, C_DEFCPUSETS, &m, 8, &rl), ST_OK);
        CHECKF(rl == 8 && popcnt(m) == (unsigned long long)k, "%d sets assigned, mask has %llu bits", k, popcnt(m));
        gd(GetCurrentProcess(), NULL, 0, &req);
        CHECK(req == (ULONG)k);
        EXPECT_EQ(ip_q((HANDLE)-1, C_ALLOWCPUSETS, &allowed1, 8, &rl), ST_OK);
        CHECK(allowed1 == allowed0);
        /* a longer buffer does not report more than the 8 byte mask */
        unsigned char *b = ip_buf(); ip_fill(b, 0x100);
        EXPECT_EQ(ip_q((HANDLE)-1, C_DEFCPUSETS, b, 0xa0, &rl), ST_OK);
        CHECK(rl == 8 && ip_all_filler(b + 8, 0x100 - 8));
    }
    CHECK(sd(GetCurrentProcess(), NULL, 0));
    m = 0x7777; EXPECT_EQ(ip_q((HANDLE)-1, C_DEFCPUSETS, &m, 8, &rl), ST_OK);
    CHECK(m == 0);
}

TEST(ip_default_cpu_sets_assignment_is_visible_through_a_limited_handle_to_a_child) {
    if (!ip_init()) return;
    set_default_cpusets_t sd = (set_default_cpusets_t)t_proc_in("kernel32.dll", "SetProcessDefaultCpuSets");
    ULONG ids[8]; int n = sd ? cpuset_ids(ids, 8) : 0;
    if (n < 1) { t_skip("no cpu set api or no cpu set in group 0"); return; }
    struct kid k; if (!kid_start(&k, "", 0, NULL, 0, NULL)) { t_skip("cannot start a child"); return; }
    kid_wait_ready(&k);
    HANDLE set = kid_open(&k, 0x2000 | PROC_LIM);       /* PROCESS_SET_LIMITED_INFORMATION */
    HANDLE lim = kid_open(&k, PROC_LIM);
    if (!set || !lim) { t_skip("cannot open the child"); if (set) CloseHandle(set); if (lim) CloseHandle(lim); kid_stop(&k); return; }
    unsigned long long m = 0x7777; ULONG rl;
    EXPECT_EQ(ip_q(lim, C_DEFCPUSETS, &m, 8, &rl), ST_OK); CHECK(m == 0);
    BOOL r = sd(set, ids, 1);
    CHECKF(r, "SetProcessDefaultCpuSets on the child failed: %lu", GetLastError());
    if (r) {
        m = 0x7777; EXPECT_EQ(ip_q(lim, C_DEFCPUSETS, &m, 8, &rl), ST_OK);
        CHECK(popcnt(m) == 1);
        unsigned long long mine = 0x7777; EXPECT_EQ(ip_q((HANDLE)-1, C_DEFCPUSETS, &mine, 8, &rl), ST_OK);
        CHECK(mine == 0);                              /* the parent is unaffected */
        CHECK(sd(set, NULL, 0));
        m = 0x7777; EXPECT_EQ(ip_q(lim, C_DEFCPUSETS, &m, 8, &rl), ST_OK); CHECK(m == 0);
    }
    CloseHandle(set); CloseHandle(lim); kid_stop(&k);
}
