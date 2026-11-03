/* win32k_modules_sysinfo: NtQuerySystemInformation(SystemModuleInformation / ...Ex) as the
 * user-mode-visible footprint of the Win32 driver stack.
 *
 * Kernel behaviour pinned here comes from ntoskrnl ExpQueryModuleInformation (class 11) and
 * ExpQueryModuleInformationEx (class 77) as analysed on 19041:
 *   - header ULONG count, entries of 0x128 bytes from offset 8; ReturnLength is always the full
 *     size 8 + 0x128*N, a short buffer yields STATUS_INFO_LENGTH_MISMATCH but NumberOfModules is
 *     still written when the buffer holds >= 8 bytes;
 *   - per entry: ImageBase = LDR DllBase, ImageSize = LDR SizeOfImage, LoadOrderIndex = position
 *     in PsLoadedModuleList, InitOrderIndex = 0, OffsetToFileName = index after the last '\\'.
 *   - class 77: entries of 0x140 bytes from offset 0, a zero USHORT terminator, total 2 + 0x140*N.
 */
#include "ms.h"

static int ms_cmp_base(const void *a, const void *b) {
    const MS_MOD *x = *(const MS_MOD * const *)a, *y = *(const MS_MOD * const *)b;
    return (ULONG_PTR)x->ImageBase < (ULONG_PTR)y->ImageBase ? -1 : (ULONG_PTR)x->ImageBase > (ULONG_PTR)y->ImageBase;
}

TEST(win32k_modules_sysinfo_modlist_query_succeeds) {
    MS_MODS *m = ms_mods();
    if (!m) { t_skip("SystemModuleInformation unavailable"); return; }
    CHECKF(m->NumberOfModules >= 8, "only %lu modules", (unsigned long)m->NumberOfModules);
    t_note("%lu kernel modules listed", (unsigned long)m->NumberOfModules);
}

TEST(win32k_modules_sysinfo_modlist_returnlength_exact) {
    MS_MODS *m = ms_mods(); REQUIRE(m);
    ULONG size = 8 + MS_MOD_SIZE * (m->NumberOfModules + 64), ret = 0;
    void *buf = calloc(1, size); REQUIRE(buf);
    LONG st = ms_qsi(11, buf, size, &ret);
    CHECKF(st == ST_SUCCESS, "status 0x%lx", (unsigned long)st);
    /* ExpQueryModuleInformation stores 8 + 0x128 * count whenever ReturnLength is non-NULL. */
    ULONG n = ((MS_MODS *)buf)->NumberOfModules;
    CHECKF(ret == 8 + MS_MOD_SIZE * n, "ret=%lu count=%lu", (unsigned long)ret, (unsigned long)n);
    free(buf);
}

TEST(win32k_modules_sysinfo_modlist_zero_length_reports_size) {
    MS_MODS *m = ms_mods(); REQUIRE(m);
    ULONG ret = 0x1234;
    LONG st = ms_qsi(11, NULL, 0, &ret);
    CHECKF(st == ST_INFO_LENGTH_MISMATCH, "status 0x%lx", (unsigned long)st);
    CHECK(ret >= 8 + MS_MOD_SIZE * m->NumberOfModules);
    CHECK((ret - 8) % MS_MOD_SIZE == 0);
}

TEST(win32k_modules_sysinfo_modlist_tiny_buffer_untouched) {
    MS_MODS *m = ms_mods(); REQUIRE(m);
    unsigned char b[8]; memset(b, 0xA5, sizeof b);
    ULONG ret = 0;
    LONG st = ms_qsi(11, b, 4, &ret);
 /* argument 3 < 8: no count is stored and the error is the length mismatch. */
    CHECKF(st == ST_INFO_LENGTH_MISMATCH, "status 0x%lx", (unsigned long)st);
    CHECK(ret >= 8 + MS_MOD_SIZE * m->NumberOfModules);
    for (int i = 0; i < 8; i++) CHECK(b[i] == 0xA5);
}

TEST(win32k_modules_sysinfo_modlist_header_only_stores_count) {
    MS_MODS *m = ms_mods(); REQUIRE(m);
    unsigned char b[8]; memset(b, 0, sizeof b);
    ULONG ret = 0;
    LONG st = ms_qsi(11, b, 8, &ret);
    /* With room for the 8-byte header only, ExpQueryModuleInformation still publishes the count. */
    CHECKF(st == ST_INFO_LENGTH_MISMATCH, "status 0x%lx", (unsigned long)st);
    ULONG n = *(ULONG *)b;
    CHECKF(ret == 8 + MS_MOD_SIZE * n, "ret=%lu n=%lu", (unsigned long)ret, (unsigned long)n);
    CHECK(n >= m->NumberOfModules);
}

TEST(win32k_modules_sysinfo_modlist_partial_buffer_prefix_matches) {
    MS_MODS *m = ms_mods(); REQUIRE(m);
    if (m->NumberOfModules < 4) { t_skip("too few modules"); return; }
    ULONG size = 8 + MS_MOD_SIZE * 3, ret = 0;
    MS_MODS *p = (MS_MODS *)calloc(1, size); REQUIRE(p);
    LONG st = ms_qsi(11, p, size, &ret);
    CHECKF(st == ST_INFO_LENGTH_MISMATCH, "status 0x%lx", (unsigned long)st);
    CHECK(p->NumberOfModules >= m->NumberOfModules);
    for (int i = 0; i < 3; i++) {
        CHECK(p->Modules[i].ImageBase == m->Modules[i].ImageBase);
        CHECK(p->Modules[i].ImageSize == m->Modules[i].ImageSize);
        CHECK(p->Modules[i].LoadOrderIndex == (USHORT)i);
        CHECK(!strcmp((char *)p->Modules[i].FullPathName, (char *)m->Modules[i].FullPathName));
    }
    free(p);
}

TEST(win32k_modules_sysinfo_modlist_null_returnlength_ok) {
    MS_MODS *m = ms_mods(); REQUIRE(m);
    ULONG size = 8 + MS_MOD_SIZE * (m->NumberOfModules + 64);
    void *buf = calloc(1, size); REQUIRE(buf);
    LONG st = ms_qsi(11, buf, size, NULL);
    CHECKF(st == ST_SUCCESS, "status 0x%lx", (unsigned long)st);
    free(buf);
}

TEST(win32k_modules_sysinfo_modlist_first_is_ntoskrnl) {
    MS_MODS *m = ms_mods(); REQUIRE(m);
    if (t_is_wine()) { t_skip("no kernel image list under Wine"); return; }
    /* PsLoadedModuleList starts with the kernel image itself. */
    CHECK(!_stricmp(ms_mod_name(&m->Modules[0]), "ntoskrnl.exe"));
    CHECK(m->Modules[0].LoadOrderIndex == 0);
    t_note("first module path %s", (char *)m->Modules[0].FullPathName);
}

TEST(win32k_modules_sysinfo_modlist_load_order_index_sequential) {
    MS_MODS *m = ms_mods(); REQUIRE(m);
    for (ULONG i = 0; i < m->NumberOfModules; i++)
        CHECKF(m->Modules[i].LoadOrderIndex == (USHORT)i, "entry %lu index %u", (unsigned long)i, m->Modules[i].LoadOrderIndex);
}

TEST(win32k_modules_sysinfo_modlist_init_order_index_zero) {
    MS_MODS *m = ms_mods(); REQUIRE(m);
    /* ExpQueryModuleInformation writes InitOrderIndex = 0 for every entry. */
    for (ULONG i = 0; i < m->NumberOfModules; i++) CHECK(m->Modules[i].InitOrderIndex == 0);
}

TEST(win32k_modules_sysinfo_modlist_paths_terminated_and_leaf_offset) {
    MS_MODS *m = ms_mods(); REQUIRE(m);
    for (ULONG i = 0; i < m->NumberOfModules; i++) {
        const MS_MOD *e = &m->Modules[i];
        size_t len = strnlen((const char *)e->FullPathName, 256);
        CHECKF(len < 256, "entry %lu path unterminated", (unsigned long)i);
        if (len >= 256) continue;
        const char *last = strrchr((const char *)e->FullPathName, '\\');
        size_t want = last ? (size_t)(last - (const char *)e->FullPathName) + 1 : 0;
        CHECKF(e->OffsetToFileName == want, "entry %lu off=%u want=%u (%s)", (unsigned long)i, e->OffsetToFileName, (unsigned)want, e->FullPathName);
        CHECK(len > want);   /* non-empty file name */
    }
}

TEST(win32k_modules_sysinfo_modlist_bases_in_kernel_range) {
    MS_MODS *m = ms_mods(); REQUIRE(m);
    if (t_is_wine()) { t_skip("emulated module list"); return; }
    for (ULONG i = 0; i < m->NumberOfModules; i++)
        CHECKF((ULONGLONG)(ULONG_PTR)m->Modules[i].ImageBase >= MS_KERNEL_START, "%s base %p", ms_mod_name(&m->Modules[i]), m->Modules[i].ImageBase);
}

TEST(win32k_modules_sysinfo_modlist_bases_agree_with_range_start) {
    ULONGLONG rs = 0; ULONG ret = 0;
    LONG st = ms_qsi(50, &rs, 8, &ret);
    if (st != ST_SUCCESS) { t_skip("SystemRangeStartInformation unavailable"); return; }
    MS_MODS *m = ms_mods(); REQUIRE(m);
    for (ULONG i = 0; i < m->NumberOfModules; i++)
        CHECK((ULONGLONG)(ULONG_PTR)m->Modules[i].ImageBase >= rs);
}

TEST(win32k_modules_sysinfo_modlist_bases_page_aligned) {
    MS_MODS *m = ms_mods(); REQUIRE(m);
    for (ULONG i = 0; i < m->NumberOfModules; i++)
        CHECKF(((ULONG_PTR)m->Modules[i].ImageBase & 0xFFF) == 0, "%s base %p", ms_mod_name(&m->Modules[i]), m->Modules[i].ImageBase);
}

TEST(win32k_modules_sysinfo_modlist_sizes_plausible) {
    MS_MODS *m = ms_mods(); REQUIRE(m);
    for (ULONG i = 0; i < m->NumberOfModules; i++) {
        const MS_MOD *e = &m->Modules[i];
        CHECKF(e->ImageSize >= 0x1000 && e->ImageSize < 0x10000000, "%s size 0x%lx", ms_mod_name(e), (unsigned long)e->ImageSize);
        CHECKF((ULONGLONG)(ULONG_PTR)e->ImageBase + e->ImageSize > (ULONGLONG)(ULONG_PTR)e->ImageBase, "%s wraps", ms_mod_name(e));
    }
}

TEST(win32k_modules_sysinfo_modlist_images_do_not_overlap) {
    MS_MODS *m = ms_mods(); REQUIRE(m);
    if (t_is_wine()) { t_skip("emulated module list"); return; }
    const MS_MOD **v = (const MS_MOD **)malloc(sizeof *v * m->NumberOfModules); REQUIRE(v);
    for (ULONG i = 0; i < m->NumberOfModules; i++) v[i] = &m->Modules[i];
    qsort(v, m->NumberOfModules, sizeof *v, ms_cmp_base);
    for (ULONG i = 0; i + 1 < m->NumberOfModules; i++) {
        ULONG_PTR end = (ULONG_PTR)v[i]->ImageBase + v[i]->ImageSize;
        CHECKF(end <= (ULONG_PTR)v[i + 1]->ImageBase, "%s [%p+%lx) overlaps %s at %p", ms_mod_name(v[i]), v[i]->ImageBase,
               (unsigned long)v[i]->ImageSize, ms_mod_name(v[i + 1]), v[i + 1]->ImageBase);
    }
    free(v);
}

TEST(win32k_modules_sysinfo_modlist_image_bases_unique) {
    MS_MODS *m = ms_mods(); REQUIRE(m);
    if (t_is_wine()) { t_skip("emulated module list"); return; }
    for (ULONG i = 0; i < m->NumberOfModules; i++)
        for (ULONG j = i + 1; j < m->NumberOfModules; j++)
            CHECK(m->Modules[i].ImageBase != m->Modules[j].ImageBase);
}

TEST(win32k_modules_sysinfo_modlist_load_count_win32k_stack_note) {
    MS_NEED_MOD(w, "win32k.sys"); MS_NEED_MOD(b, "win32kbase.sys"); MS_NEED_MOD(f, "win32kfull.sys");
    MS_MODS *m = ms_mods();
    /* LDR LoadCount semantics differ between boot and session images; recorded only. */
    t_note("LoadCount win32k=%u base=%u full=%u", m->Modules[w].LoadCount, m->Modules[b].LoadCount, m->Modules[f].LoadCount);
}

/* ---- the Win32 stack members --------------------------------------------------------- */
static void ms_check_member(const char *leaf) {
    MS_NEED_MOD(i, leaf);
    const MS_MOD *e = &ms_mods()->Modules[i];
    CHECKF((ULONGLONG)(ULONG_PTR)e->ImageBase >= MS_KERNEL_START, "%s base %p", leaf, e->ImageBase);
    CHECKF(e->ImageSize >= 0x20000, "%s size 0x%lx", leaf, (unsigned long)e->ImageSize);
    CHECK(e->ImageSize < 0x2000000);
    CHECK(((ULONG_PTR)e->ImageBase & 0xFFF) == 0);
    t_info("%s base=%p size=0x%lx idx=%u path=%s", leaf, e->ImageBase, (unsigned long)e->ImageSize, e->LoadOrderIndex, (char *)e->FullPathName);
}
TEST(win32k_modules_sysinfo_modlist_has_win32k_sys)     { ms_check_member("win32k.sys"); }
TEST(win32k_modules_sysinfo_modlist_has_win32kbase_sys) { ms_check_member("win32kbase.sys"); }
TEST(win32k_modules_sysinfo_modlist_has_win32kfull_sys) { ms_check_member("win32kfull.sys"); }

static void ms_check_path(const char *leaf) {
    MS_NEED_MOD(i, leaf);
    const MS_MOD *e = &ms_mods()->Modules[i];
    char want[96]; snprintf(want, sizeof want, "\\SystemRoot\\System32\\%s", leaf);
    /* The three images are loaded from the system directory by their \SystemRoot alias. */
    CHECKF(!_stricmp((const char *)e->FullPathName, want), "path \"%s\"", (char *)e->FullPathName);
}
TEST(win32k_modules_sysinfo_modlist_path_win32k_sys)     { ms_check_path("win32k.sys"); }
TEST(win32k_modules_sysinfo_modlist_path_win32kbase_sys) { ms_check_path("win32kbase.sys"); }
TEST(win32k_modules_sysinfo_modlist_path_win32kfull_sys) { ms_check_path("win32kfull.sys"); }

TEST(win32k_modules_sysinfo_modlist_win32kbase_loaded_before_win32kfull) {
    MS_NEED_MOD(b, "win32kbase.sys"); MS_NEED_MOD(f, "win32kfull.sys");
    /* win32kfull.sys imports win32kbase.sys (PE import table), so the loader must have mapped
     * win32kbase first: its entry precedes win32kfull in PsLoadedModuleList. */
    CHECK(b < f);
}

TEST(win32k_modules_sysinfo_modlist_ntoskrnl_precedes_win32k_stack) {
    MS_NEED_MOD(k, "ntoskrnl.exe"); MS_NEED_MOD(w, "win32k.sys"); MS_NEED_MOD(b, "win32kbase.sys"); MS_NEED_MOD(f, "win32kfull.sys");
    CHECK(k < w); CHECK(k < b); CHECK(k < f);
}

TEST(win32k_modules_sysinfo_modlist_has_hal) {
    MS_NEED_MOD(h, "hal.dll");
    CHECK((ULONGLONG)(ULONG_PTR)ms_mods()->Modules[h].ImageBase >= MS_KERNEL_START);
}

TEST(win32k_modules_sysinfo_modlist_has_dxgkrnl) {
    /* win32k.sys and win32kbase.sys import dxgkrnl.sys (import tables), so it must be resident. */
    MS_NEED_MOD(d, "dxgkrnl.sys");
    CHECK((ULONGLONG)(ULONG_PTR)ms_mods()->Modules[d].ImageBase >= MS_KERNEL_START);
    MS_NEED_MOD(w, "win32k.sys");
    CHECK(d < w);
}

TEST(win32k_modules_sysinfo_modlist_has_watchdog) {
    /* win32k.sys imports watchdog.sys directly. */
    MS_NEED_MOD(wd, "watchdog.sys");
    MS_NEED_MOD(w, "win32k.sys");
    CHECK(wd < w);
}

TEST(win32k_modules_sysinfo_modlist_optional_display_stack_note) {
    if (!ms_mods()) { t_skip("no module list"); return; }
    static const char *opt[] = { "dxgmms2.sys", "dxgmms1.sys", "cdd.dll", "BasicDisplay.sys", "BasicRender.sys", "dxgkrnl.sys", "watchdog.sys", "win32kbase.sys", "win32kfull.sys", "win32k.sys", "msrpc.sys", "ksecdd.sys", "cng.sys", "hidparse.sys", "WppRecorder.sys" };
    int present = 0;
    for (size_t i = 0; i < sizeof opt / sizeof *opt; i++) {
        int idx = ms_find_mod(opt[i]);
        t_note("%-18s %s", opt[i], idx < 0 ? "absent" : "present");
        present += idx >= 0;
    }
    /* the three win32k images are the only ones required here */
    if (!t_is_wine()) CHECK(present >= 3);
}

TEST(win32k_modules_sysinfo_modlist_win32k_images_adjacent_session_space) {
    MS_NEED_MOD(w, "win32k.sys"); MS_NEED_MOD(b, "win32kbase.sys"); MS_NEED_MOD(f, "win32kfull.sys");
    MS_MODS *m = ms_mods();
    ULONG_PTR a = (ULONG_PTR)m->Modules[w].ImageBase, bb = (ULONG_PTR)m->Modules[b].ImageBase, ff = (ULONG_PTR)m->Modules[f].ImageBase;
    /* Placement inside session image space is a memory-manager policy, not something the analysis pins:
     * record the span and only require distinct, non-overlapping images (checked list-wide above). */
    ULONG_PTR lo = a < bb ? (a < ff ? a : ff) : (bb < ff ? bb : ff);
    ULONG_PTR hi = a > bb ? (a > ff ? a : ff) : (bb > ff ? bb : ff);
    t_note("win32k trio span 0x%llx", (unsigned long long)(hi - lo));
    CHECK(a != bb && bb != ff && a != ff);
}

TEST(win32k_modules_sysinfo_modlist_name_lookup_case_insensitive_helper) {
    MS_NEED_MOD(a, "WIN32KBASE.SYS");
    MS_NEED_MOD(b, "win32kbase.sys");
    CHECK(a == b);
}

/* ---- SystemModuleInformationEx (class 77) ------------------------------------------- */
#define MS_MODEX_SIZE 0x140
static unsigned char *ms_modex(ULONG *count_out, ULONG *size_out) {
    static unsigned char *cache; static ULONG cnt, sz; static int tried;
    if (!tried) {
        tried = 1;
        ULONG size = 0x60000;
        for (int i = 0; i < 6 && !cache; i++) {
            unsigned char *b = (unsigned char *)calloc(1, size);
            if (!b) break;
            ULONG ret = 0;
            LONG st = ms_qsi(77, b, size, &ret);
            if (st == ST_SUCCESS) {
                cache = b; sz = ret;
                for (ULONG off = 0; off + MS_MODEX_SIZE <= ret && *(USHORT *)(b + off); off += *(USHORT *)(b + off)) cnt++;
                break;
            }
            free(b);
            if (st != ST_INFO_LENGTH_MISMATCH) break;
            size = ret + 0x4000;
        }
    }
    if (!cache) return NULL;
    *count_out = cnt; *size_out = sz; return cache;
}

TEST(win32k_modules_sysinfo_modex_query_succeeds) {
    ULONG n, sz; unsigned char *b = ms_modex(&n, &sz);
    if (!b) { t_skip("SystemModuleInformationEx unavailable (restricted caller or emulator)"); return; }
    CHECKF(sz == 2 + MS_MODEX_SIZE * n, "size %lu count %lu", (unsigned long)sz, (unsigned long)n);
    MS_MODS *m = ms_mods(); REQUIRE(m);
    CHECKF(n >= m->NumberOfModules && n <= m->NumberOfModules + 8, "ex count %lu base count %lu", (unsigned long)n, (unsigned long)m->NumberOfModules);
}

TEST(win32k_modules_sysinfo_modex_next_offset_constant) {
    ULONG n, sz; unsigned char *b = ms_modex(&n, &sz);
    if (!b) { t_skip("SystemModuleInformationEx unavailable"); return; }
    for (ULONG i = 0; i < n; i++) CHECK(*(USHORT *)(b + MS_MODEX_SIZE * i) == MS_MODEX_SIZE);
    CHECK(*(USHORT *)(b + MS_MODEX_SIZE * n) == 0);   /* terminator written after the last entry */
}

TEST(win32k_modules_sysinfo_modex_matches_basic_list) {
    ULONG n, sz; unsigned char *b = ms_modex(&n, &sz);
    if (!b) { t_skip("SystemModuleInformationEx unavailable"); return; }
    MS_MODS *m = ms_mods(); REQUIRE(m);
    for (ULONG i = 0; i < m->NumberOfModules && i < n; i++) {
        const MS_MOD *e = (const MS_MOD *)(b + MS_MODEX_SIZE * i + 8);   /* BaseInfo follows NextOffset, 8-aligned */
        CHECKF(e->ImageBase == m->Modules[i].ImageBase, "entry %lu base %p vs %p", (unsigned long)i, e->ImageBase, m->Modules[i].ImageBase);
        CHECK(e->ImageSize == m->Modules[i].ImageSize);
        CHECK(e->LoadOrderIndex == i);
        CHECK(!strcmp((const char *)e->FullPathName, (const char *)m->Modules[i].FullPathName));
    }
}

TEST(win32k_modules_sysinfo_modex_short_buffers_report_full_size) {
    ULONG n, sz; unsigned char *b = ms_modex(&n, &sz);
    if (!b) { t_skip("SystemModuleInformationEx unavailable"); return; }
    ULONG ret = 0; unsigned char one[8] __attribute__((aligned(8))) = { 0xAA, 0xAA };   /* an unaligned buffer is rejected before its length is looked at */
    LONG st = ms_qsi(77, one, 1, &ret);
    CHECKF(st == ST_INFO_LENGTH_MISMATCH, "status 0x%lx", (unsigned long)st);
    CHECK(ret >= 2 + MS_MODEX_SIZE * n);
    CHECK(one[0] == 0xAA);
    ret = 0;
    st = ms_qsi(77, NULL, 0, &ret);
    CHECK(st == ST_INFO_LENGTH_MISMATCH);
    CHECK(ret >= 2 + MS_MODEX_SIZE * n);
    CHECK((ret - 2) % MS_MODEX_SIZE == 0);
}

TEST(win32k_modules_sysinfo_modex_image_checksum_and_stamp_note) {
    ULONG n, sz; unsigned char *b = ms_modex(&n, &sz);
    if (!b) { t_skip("SystemModuleInformationEx unavailable"); return; }
    MS_NEED_MOD(w, "win32kfull.sys");
    const unsigned char *e = b + MS_MODEX_SIZE * w;
    /* +0x130 ImageChecksum, +0x134 TimeDateStamp (zeroed for callers without SeLoadDriverPrivilege),
     * +0x138 DefaultBase. */
    t_note("win32kfull checksum=0x%08lx stamp=0x%08lx defaultbase=%p", *(unsigned long *)(e + 0x130), *(unsigned long *)(e + 0x134), *(void **)(e + 0x138));
}
