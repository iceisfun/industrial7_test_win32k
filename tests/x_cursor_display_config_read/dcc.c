/* Display configuration (CCD) services: NtUserGetDisplayConfigBufferSizes, NtUserDisplayConfigSetDeviceInfo,
 * NtUserSetDisplayConfig, with user32's GetDisplayConfigBufferSizes / QueryDisplayConfig /
 * DisplayConfigSetDeviceInfo / SetDisplayConfig in front.
 *
 * Measured on Windows 10 22H2: the raw buffer-size query accepts a flag word whose three low bits select exactly one
 * of ALL_PATHS (1), ONLY_ACTIVE_PATHS (2) or DATABASE_CURRENT (4), with any of the higher bits alongside, and the
 * all-ones word; everything else is STATUS_INVALID_PARAMETER and zeroes the output. Unwritable output pointers are
 * the same status with last error 998. DisplayConfigSetDeviceInfo refuses a header under 20 bytes with 122 and
 * every header here (undefined type, or a defined setter with a header-only buffer) with 87. SetDisplayConfig is
 * only ever called in forms that cannot apply a topology: no flags, SDC_VALIDATE with a missing or contradictory
 * argument set, and SDC_VALIDATE with a topology that is checked, not set. No test applies anything; each one that
 * reaches the validator checks afterwards that the monitor list did not change.
 */
#include "x.h"

#define NEEDCCD() do { NEED(NtUserGetDisplayConfigBufferSizes); if (!xc_ccd(NULL)) { t_skip("no display configuration on this system"); return; } } while (0)

struct mon_snapshot { int n; RECT rc[XC_MAXMON]; HMONITOR m[XC_MAXMON]; UINT32 paths; int cm; };
static void snap(struct mon_snapshot *s) {
    struct xc_enum e; memset(&e, 0, sizeof e);
    U(EnumDisplayMonitors)(NULL, NULL, xc_enum_cb, (LPARAM)&e);
    s->n = e.n; memcpy(s->rc, e.rc, sizeof s->rc); memcpy(s->m, e.m, sizeof s->m);
    s->cm = U(GetSystemMetrics)(SM_CMONITORS);
    s->paths = 0; xc_ccd(&s->paths);
}
static int snap_same(const struct mon_snapshot *a, const struct mon_snapshot *b) {
    return a->n == b->n && a->cm == b->cm && a->paths == b->paths && !memcmp(a->rc, b->rc, sizeof a->rc) && !memcmp(a->m, b->m, sizeof a->m);
}

/* ---- raw buffer sizes -------------------------------------------------------------------------------------- */
static LONG raw_sizes(UINT32 flags, UINT32 *n) { return UN(NtUserGetDisplayConfigBufferSizes)(flags, n); }

XT(ccd_sizes_all_paths_succeeds_with_a_path) {
    NEEDCCD();
    UINT32 n = 0xDEAD;
    EXPECT_EQ(raw_sizes(QDC_ALL_PATHS, &n), 0);
    CHECK(n >= 1 && n != 0xDEAD);
}
XT(ccd_sizes_active_paths_succeeds_with_a_path) {
    NEEDCCD();
    UINT32 n = 0xDEAD;
    EXPECT_EQ(raw_sizes(QDC_ONLY_ACTIVE_PATHS, &n), 0);
    CHECK(n >= 1 && n != 0xDEAD);
}
XT(ccd_sizes_database_current_succeeds) {
    NEEDCCD();
    UINT32 n = 0xDEAD;
    EXPECT_EQ(raw_sizes(QDC_DATABASE_CURRENT, &n), 0);
    CHECK(n != 0xDEAD);
}
XT(ccd_sizes_active_is_not_more_than_all) {
    NEEDCCD();
    UINT32 a = 0, b = 0;
    EXPECT_EQ(raw_sizes(QDC_ONLY_ACTIVE_PATHS, &a), 0);
    EXPECT_EQ(raw_sizes(QDC_ALL_PATHS, &b), 0);
    CHECKF(a <= b, "active %u > all %u", a, b);
}
XT(ccd_sizes_raw_agrees_with_user32_path_count) {
    NEEDCCD();
    UINT32 n = 0, p = 0, m = 0;
    EXPECT_EQ(raw_sizes(QDC_ONLY_ACTIVE_PATHS, &n), 0);
    EXPECT_EQ(U(GetDisplayConfigBufferSizes)(QDC_ONLY_ACTIVE_PATHS, &p, &m), ERROR_SUCCESS);
    EXPECT_EQ(n, p);
}
XT(ccd_sizes_all_paths_raw_agrees_with_user32) {
    NEEDCCD();
    UINT32 n = 0, p = 0, m = 0;
    EXPECT_EQ(raw_sizes(QDC_ALL_PATHS, &n), 0);
    EXPECT_EQ(U(GetDisplayConfigBufferSizes)(QDC_ALL_PATHS, &p, &m), ERROR_SUCCESS);
    EXPECT_EQ(n, p);
}
XT(ccd_sizes_stable_between_calls) {
    NEEDCCD();
    UINT32 a = 0, b = 0;
    raw_sizes(QDC_ONLY_ACTIVE_PATHS, &a); raw_sizes(QDC_ONLY_ACTIVE_PATHS, &b);
    EXPECT_EQ(a, b);
}
XT(ccd_sizes_active_paths_cover_the_monitors) {
    NEEDCCD();
    UINT32 n = 0;
    raw_sizes(QDC_ONLY_ACTIVE_PATHS, &n);
    int cm = U(GetSystemMetrics)(SM_CMONITORS);
    CHECKF((int)n >= cm, "%u active paths for %d monitors", n, cm);
}
XT(ccd_sizes_does_not_set_last_error_on_success) {
    NEEDCCD();
    UINT32 n = 0;
    SET_ERR(XSENT);
    EXPECT_EQ(raw_sizes(QDC_ONLY_ACTIVE_PATHS, &n), 0);
    EXPECT_EQ(GetLastError(), XSENT);
}
XT(ccd_sizes_flag_with_extra_high_bits_is_accepted) {
    NEEDCCD();
    static const UINT32 extra[] = { 0x8, 0x10, 0x20, 0x40, 0x80, 0x100, 0x1000, 0x10000, 0x80000000u };
    for (unsigned i = 0; i < sizeof extra / sizeof extra[0]; i++) {
        UINT32 n = 0xDEAD;
        LONG s = raw_sizes(QDC_ONLY_ACTIVE_PATHS | extra[i], &n);
        CHECKF(s == 0 && n >= 1 && n != 0xDEAD, "flags 0x%x: status 0x%lx n %u", (unsigned)(2 | extra[i]), (unsigned long)s, n);
    }
}
XT(ccd_sizes_all_ones_flag_is_accepted) {
    NEEDCCD();
    UINT32 n = 0xDEAD;
    EXPECT_EQ(raw_sizes(0xFFFFFFFFu, &n), 0);
    CHECK(n >= 1 && n != 0xDEAD);
}
static void sizes_bad(UINT32 flags) {
    UINT32 n = 0xDEAD;
    LONG s = raw_sizes(flags, &n);
    CHECKF(s == ST_INVALID_PARAMETER, "flags 0x%x: status 0x%lx want 0xC000000D", flags, (unsigned long)s);
    CHECKF(n == 0, "flags 0x%x: output 0x%x not zeroed", flags, n);
}
XT(ccd_sizes_zero_flags_rejected) { NEEDCCD(); sizes_bad(0); }
XT(ccd_sizes_two_selectors_rejected) { NEEDCCD(); sizes_bad(3); sizes_bad(5); sizes_bad(6); }
XT(ccd_sizes_three_selectors_rejected) { NEEDCCD(); sizes_bad(7); sizes_bad(7 | 0x10); sizes_bad(7 | 0x8); }
XT(ccd_sizes_no_selector_with_other_bits_rejected) { NEEDCCD(); sizes_bad(8); sizes_bad(0x10); sizes_bad(0x18); sizes_bad(0x100); sizes_bad(0x10000); sizes_bad(0x80000000u); }
XT(ccd_sizes_selector_matrix_over_the_low_six_bits) {
    NEEDCCD();
    for (UINT32 f = 0; f < 0x40; f++) {
        UINT32 sel = f & 7, n = 0xDEAD;
        LONG s = raw_sizes(f, &n);
        int valid = sel == 1 || sel == 2 || sel == 4;
        CHECKF(valid ? (s == 0 && n >= 1 && n != 0xDEAD) : (s == ST_INVALID_PARAMETER && n == 0), "flags 0x%x: status 0x%lx n %u", f, (unsigned long)s, n);
    }
}
XT(ccd_sizes_null_output_is_unsuccessful_with_no_access) {
    NEEDCCD();
    SET_ERR(XSENT);
    LONG s = raw_sizes(QDC_ONLY_ACTIVE_PATHS, NULL);
    CHECKF(s == ST_INVALID_PARAMETER, "status 0x%lx", (unsigned long)s);
    EXPECT_ERR(XE_NOACCESS);
}
XT(ccd_sizes_unwritable_outputs_fail_with_no_access) {
    NEEDCCD();
    UINT32 *bad[] = { (UINT32 *)(ULONG_PTR)1, (UINT32 *)(ULONG_PTR)0x1000, (UINT32 *)0xFFFF800000000000ULL };
    for (unsigned i = 0; i < 3; i++) {
        SET_ERR(XSENT);
        LONG s = raw_sizes(QDC_ONLY_ACTIVE_PATHS, bad[i]);
        CHECKF(s == ST_INVALID_PARAMETER, "pointer %u: status 0x%lx", i, (unsigned long)s);
        CHECKF(GetLastError() == XE_NOACCESS, "pointer %u: last error %lu", i, GetLastError());
    }
}
XT(ccd_sizes_bad_flags_with_bad_pointer_fails_the_same_way) {
    NEEDCCD();
    SET_ERR(XSENT);
    LONG s = raw_sizes(0, (UINT32 *)(ULONG_PTR)1);
    CHECKF(s == ST_INVALID_PARAMETER, "status 0x%lx", (unsigned long)s);
}
XT(ccd_sizes_invalid_flags_do_not_set_last_error) {
    NEEDCCD();
    UINT32 n;
    SET_ERR(XSENT);
    raw_sizes(3, &n);
    EXPECT_EQ(GetLastError(), XSENT);
}
XT(ccd_sizes_output_on_a_guard_page_edge) {
    NEEDCCD();
    char *pg = VirtualAlloc(NULL, 8192, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    CHECK(pg != NULL);
    if (!pg) return;
    DWORD old; VirtualProtect(pg + 4096, 4096, PAGE_NOACCESS, &old);
    UINT32 *edge = (UINT32 *)(pg + 4096 - 4);
    EXPECT_EQ(raw_sizes(QDC_ONLY_ACTIVE_PATHS, edge), 0);
    CHECK(*edge >= 1);
    UINT32 *over = (UINT32 *)(pg + 4096 - 2);
    SET_ERR(XSENT);
    LONG s = raw_sizes(QDC_ONLY_ACTIVE_PATHS, over);
    CHECKF(s == ST_INVALID_PARAMETER, "straddling pointer: status 0x%lx", (unsigned long)s);
    VirtualFree(pg, 0, MEM_RELEASE);
}

/* ---- user32 GetDisplayConfigBufferSizes / QueryDisplayConfig ------------------------------------------------ */
XT(user32_sizes_modes_are_twice_the_paths) {
    NEEDCCD();
    UINT32 p = 0, m = 0;
    EXPECT_EQ(U(GetDisplayConfigBufferSizes)(QDC_ONLY_ACTIVE_PATHS, &p, &m), ERROR_SUCCESS);
    EXPECT_EQ(m, p * 2);
}
XT(user32_sizes_virtual_mode_aware_modes_are_three_per_path) {
    NEEDCCD();
    UINT32 p = 0, m = 0;
    EXPECT_EQ(U(GetDisplayConfigBufferSizes)(QDC_ONLY_ACTIVE_PATHS | 0x10, &p, &m), ERROR_SUCCESS);
    EXPECT_EQ(m, p * 3);
}
XT(user32_sizes_invalid_selector_is_invalid_parameter) {
    NEEDCCD();
    UINT32 p, m;
    EXPECT_EQ(U(GetDisplayConfigBufferSizes)(3, &p, &m), ERROR_INVALID_PARAMETER);
    EXPECT_EQ(U(GetDisplayConfigBufferSizes)(5, &p, &m), ERROR_INVALID_PARAMETER);
    EXPECT_EQ(U(GetDisplayConfigBufferSizes)(0, &p, &m), ERROR_INVALID_PARAMETER);
}
XT(query_display_config_fills_the_active_paths) {
    NEEDCCD(); REQUIRE(U(QueryDisplayConfig));
    UINT32 p = 0, m = 0;
    U(GetDisplayConfigBufferSizes)(QDC_ONLY_ACTIVE_PATHS, &p, &m);
    DISPLAYCONFIG_PATH_INFO *pi = calloc(p, sizeof *pi); DISPLAYCONFIG_MODE_INFO *mi = calloc(m, sizeof *mi);
    UINT32 np = p, nm = m;
    EXPECT_EQ(U(QueryDisplayConfig)(QDC_ONLY_ACTIVE_PATHS, &np, pi, &nm, mi, NULL), ERROR_SUCCESS);
    CHECK(np >= 1 && np <= p);
    CHECK(nm <= m);
    for (UINT32 i = 0; i < np; i++) CHECKF(pi[i].flags & DISPLAYCONFIG_PATH_ACTIVE, "path %u not active", i);
    free(pi); free(mi);
}
XT(query_display_config_path_count_matches_the_size_query) {
    NEEDCCD(); REQUIRE(U(QueryDisplayConfig));
    UINT32 p = 0, m = 0;
    U(GetDisplayConfigBufferSizes)(QDC_ONLY_ACTIVE_PATHS, &p, &m);
    DISPLAYCONFIG_PATH_INFO *pi = calloc(p, sizeof *pi); DISPLAYCONFIG_MODE_INFO *mi = calloc(m, sizeof *mi);
    UINT32 np = p, nm = m;
    U(QueryDisplayConfig)(QDC_ONLY_ACTIVE_PATHS, &np, pi, &nm, mi, NULL);
    EXPECT_EQ(np, p);
    free(pi); free(mi);
}
XT(query_display_config_mode_indices_are_inside_the_mode_array) {
    NEEDCCD(); REQUIRE(U(QueryDisplayConfig));
    UINT32 p = 0, m = 0;
    U(GetDisplayConfigBufferSizes)(QDC_ONLY_ACTIVE_PATHS, &p, &m);
    DISPLAYCONFIG_PATH_INFO *pi = calloc(p, sizeof *pi); DISPLAYCONFIG_MODE_INFO *mi = calloc(m, sizeof *mi);
    UINT32 np = p, nm = m;
    U(QueryDisplayConfig)(QDC_ONLY_ACTIVE_PATHS, &np, pi, &nm, mi, NULL);
    for (UINT32 i = 0; i < np; i++) {
        UINT32 si = pi[i].sourceInfo.modeInfoIdx, ti = pi[i].targetInfo.modeInfoIdx;
        CHECKF(si == DISPLAYCONFIG_PATH_MODE_IDX_INVALID || si < nm, "path %u source index %u of %u", i, si, nm);
        CHECKF(ti == DISPLAYCONFIG_PATH_MODE_IDX_INVALID || ti < nm, "path %u target index %u of %u", i, ti, nm);
    }
    free(pi); free(mi);
}
XT(query_display_config_zero_capacity_is_invalid_parameter) {
    NEEDCCD(); REQUIRE(U(QueryDisplayConfig));
    DISPLAYCONFIG_PATH_INFO pi[2]; DISPLAYCONFIG_MODE_INFO mi[4];
    UINT32 np = 0, nm = 0;
    EXPECT_EQ(U(QueryDisplayConfig)(QDC_ONLY_ACTIVE_PATHS, &np, pi, &nm, mi, NULL), ERROR_INVALID_PARAMETER);
}

/* ---- DisplayConfigSetDeviceInfo ---------------------------------------------------------------------------- */
static void hdr_fill(DWORD *h, int type, DWORD size) { memset(h, 0, 64); h[0] = (DWORD)type; h[1] = size; }
#define NEEDSDI() NEED(NtUserDisplayConfigSetDeviceInfo)

XT(setdevinfo_header_under_20_bytes_is_insufficient_buffer) {
    NEEDSDI(); REQUIRE(U(DisplayConfigSetDeviceInfo));
    DWORD h[16];
    for (DWORD sz = 0; sz < 20; sz++) {
        hdr_fill(h, 99, sz);
        LONG r = U(DisplayConfigSetDeviceInfo)((DISPLAYCONFIG_DEVICE_INFO_HEADER *)h);
        CHECKF(r == XE_INSUFFICIENT_BUFFER, "size %lu: result %ld", sz, r);
    }
}
XT(setdevinfo_header_under_20_bytes_is_buffer_too_small_status) {
    NEEDSDI();
    DWORD h[16];
    for (DWORD sz = 0; sz < 20; sz += 4) {
        hdr_fill(h, 99, sz);
        LONG s = UN(NtUserDisplayConfigSetDeviceInfo)(h);
        CHECKF(s == (LONG)0xC0000023, "size %lu: status 0x%lx", sz, (unsigned long)s);
    }
}
XT(setdevinfo_undefined_type_with_a_20_byte_header_is_invalid_parameter) {
    NEEDSDI(); REQUIRE(U(DisplayConfigSetDeviceInfo));
    static const int types[] = { 0, 99, 100, 0x7FFFFFFF, -100, -1 };
    DWORD h[16];
    for (unsigned i = 0; i < sizeof types / sizeof types[0]; i++) {
        hdr_fill(h, types[i], 20);
        LONG r = U(DisplayConfigSetDeviceInfo)((DISPLAYCONFIG_DEVICE_INFO_HEADER *)h);
        CHECKF(r == ERROR_INVALID_PARAMETER, "type %d: result %ld", types[i], r);
    }
}
XT(setdevinfo_undefined_type_raw_status_is_invalid_parameter) {
    NEEDSDI();
    DWORD h[16];
    hdr_fill(h, 99, 20);
    EXPECT_EQ(UN(NtUserDisplayConfigSetDeviceInfo)(h), ST_INVALID_PARAMETER);
}
XT(setdevinfo_header_only_setters_are_refused) {
    NEEDSDI(); REQUIRE(U(DisplayConfigSetDeviceInfo));
    DWORD h[16];
    static const int setters[] = { DISPLAYCONFIG_DEVICE_INFO_SET_TARGET_PERSISTENCE, DISPLAYCONFIG_DEVICE_INFO_SET_SUPPORT_VIRTUAL_RESOLUTION };
    for (unsigned i = 0; i < 2; i++) {
        hdr_fill(h, setters[i], 20);
        LONG r = U(DisplayConfigSetDeviceInfo)((DISPLAYCONFIG_DEVICE_INFO_HEADER *)h);
        CHECKF(r != ERROR_SUCCESS, "setter %d accepted a header-only buffer", setters[i]);
    }
}
XT(setdevinfo_large_size_with_bad_type_is_unsuccessful_raw) {
    NEEDSDI();
    DWORD h[16];
    hdr_fill(h, 99, 0x100000);
    SET_ERR(XSENT);
    LONG s = UN(NtUserDisplayConfigSetDeviceInfo)(h);
    CHECKF(s == ST_UNSUCCESSFUL, "status 0x%lx", (unsigned long)s);
    EXPECT_ERR(XE_NOACCESS);
}
XT(setdevinfo_null_header_raw_is_unsuccessful_with_no_access) {
    NEEDSDI();
    SET_ERR(XSENT);
    LONG s = UN(NtUserDisplayConfigSetDeviceInfo)(NULL);
    CHECKF(s == ST_UNSUCCESSFUL, "status 0x%lx", (unsigned long)s);
    EXPECT_ERR(XE_NOACCESS);
}
XT(setdevinfo_unreadable_headers_raw_are_unsuccessful) {
    NEEDSDI();
    void *bad[] = { (void *)(ULONG_PTR)1, (void *)(ULONG_PTR)0x1000, (void *)0xFFFF800000000000ULL };
    for (unsigned i = 0; i < 3; i++) {
        SET_ERR(XSENT);
        LONG s = UN(NtUserDisplayConfigSetDeviceInfo)(bad[i]);
        CHECKF(s == ST_UNSUCCESSFUL && GetLastError() == XE_NOACCESS, "pointer %u: status 0x%lx error %lu", i, (unsigned long)s, GetLastError());
    }
}
XT(setdevinfo_user32_null_header_is_gen_failure) {
    NEEDSDI(); REQUIRE(U(DisplayConfigSetDeviceInfo));
    EXPECT_EQ(U(DisplayConfigSetDeviceInfo)(NULL), XE_GEN_FAILURE);
    EXPECT_EQ(U(DisplayConfigSetDeviceInfo)((DISPLAYCONFIG_DEVICE_INFO_HEADER *)(ULONG_PTR)1), XE_GEN_FAILURE);
}
XT(setdevinfo_header_ending_at_a_guard_page) {
    NEEDSDI(); REQUIRE(U(DisplayConfigSetDeviceInfo));
    char *pg = VirtualAlloc(NULL, 8192, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    CHECK(pg != NULL);
    if (!pg) return;
    DWORD old; VirtualProtect(pg + 4096, 4096, PAGE_NOACCESS, &old);
    DWORD *h = (DWORD *)(pg + 4096 - 20);
    h[0] = 99; h[1] = 20; h[2] = h[3] = h[4] = 0;
    EXPECT_EQ(U(DisplayConfigSetDeviceInfo)((DISPLAYCONFIG_DEVICE_INFO_HEADER *)h), ERROR_INVALID_PARAMETER);
    VirtualFree(pg, 0, MEM_RELEASE);
}
XT(setdevinfo_rejections_leave_the_monitor_list_alone) {
    NEEDSDI(); REQUIRE(U(DisplayConfigSetDeviceInfo));
    struct mon_snapshot a, b; snap(&a);
    DWORD h[16];
    for (int t = -3; t < 20; t++) { hdr_fill(h, t, 20); U(DisplayConfigSetDeviceInfo)((DISPLAYCONFIG_DEVICE_INFO_HEADER *)h); }
    snap(&b);
    CHECK(snap_same(&a, &b));
}

/* ---- SetDisplayConfig: forms that cannot apply -------------------------------------------------------------- */
#define NEEDSDC() do { NEED(NtUserSetDisplayConfig); REQUIRE(U(SetDisplayConfig)); } while (0)
static void sdc_bad(UINT np, UINT nm, UINT flags, const char *what) {
    struct mon_snapshot a, b; snap(&a);
    LONG r = U(SetDisplayConfig)(np, NULL, nm, NULL, flags);
    CHECKF(r == ERROR_INVALID_PARAMETER, "%s: result %ld", what, r);
    snap(&b);
    CHECKF(snap_same(&a, &b), "%s: the monitor list changed", what);
}
XT(sdc_no_flags_rejected) { NEEDSDC(); sdc_bad(0, 0, 0, "no flags"); }
XT(sdc_validate_alone_rejected) { NEEDSDC(); sdc_bad(0, 0, SDC_VALIDATE, "validate alone"); }
XT(sdc_validate_with_apply_rejected) { NEEDSDC(); sdc_bad(0, 0, SDC_VALIDATE | SDC_APPLY | SDC_TOPOLOGY_INTERNAL, "validate|apply"); }
XT(sdc_unknown_high_bit_rejected) { NEEDSDC(); sdc_bad(0, 0, SDC_VALIDATE | SDC_TOPOLOGY_INTERNAL | 0x80000000u, "high bit"); }
XT(sdc_unassigned_low_bit_rejected) { NEEDSDC(); sdc_bad(0, 0, SDC_VALIDATE | 0x1000, "0x1000"); }
XT(sdc_supplied_config_without_paths_rejected) { NEEDSDC(); sdc_bad(0, 0, SDC_VALIDATE | SDC_USE_SUPPLIED_DISPLAY_CONFIG, "supplied, no paths"); }
XT(sdc_supplied_config_with_null_arrays_rejected) { NEEDSDC(); sdc_bad(1, 0, SDC_VALIDATE | SDC_USE_SUPPLIED_DISPLAY_CONFIG, "supplied, 1 path NULL"); }
XT(sdc_supplied_config_with_modes_only_rejected) { NEEDSDC(); sdc_bad(0, 1, SDC_VALIDATE | SDC_USE_SUPPLIED_DISPLAY_CONFIG, "supplied, modes only"); }
XT(sdc_supplied_config_save_to_database_rejected) { NEEDSDC(); sdc_bad(0, 0, SDC_VALIDATE | SDC_USE_SUPPLIED_DISPLAY_CONFIG | SDC_SAVE_TO_DATABASE, "supplied|save"); }
XT(sdc_topology_with_a_path_count_but_no_array_rejected) { NEEDSDC(); sdc_bad(1, 0, SDC_VALIDATE | SDC_TOPOLOGY_INTERNAL, "topology, 1 path NULL"); }
XT(sdc_topology_with_allow_changes_rejected) { NEEDSDC(); sdc_bad(0, 0, SDC_VALIDATE | SDC_TOPOLOGY_INTERNAL | SDC_ALLOW_CHANGES, "topology|allow changes"); }
XT(sdc_database_with_allow_changes_rejected) { NEEDSDC(); sdc_bad(0, 0, SDC_VALIDATE | SDC_USE_DATABASE_CURRENT | SDC_ALLOW_CHANGES, "database|allow changes"); }
XT(sdc_topology_supplied_without_paths_rejected) { NEEDSDC(); sdc_bad(0, 0, SDC_VALIDATE | SDC_TOPOLOGY_SUPPLIED, "topology supplied"); }
XT(sdc_validate_internal_topology_is_checked_not_applied) {
    NEEDSDC();
    struct mon_snapshot a, b; snap(&a);
    LONG r = U(SetDisplayConfig)(0, NULL, 0, NULL, SDC_VALIDATE | SDC_TOPOLOGY_INTERNAL);
    CHECKF(r == ERROR_SUCCESS || r == XE_GEN_FAILURE, "result %ld", r);
    snap(&b);
    CHECK(snap_same(&a, &b));
}
XT(sdc_validate_database_current_is_checked_not_applied) {
    NEEDSDC();
    struct mon_snapshot a, b; snap(&a);
    LONG r = U(SetDisplayConfig)(0, NULL, 0, NULL, SDC_VALIDATE | SDC_USE_DATABASE_CURRENT);
    CHECKF(r == ERROR_SUCCESS || r == XE_GEN_FAILURE, "result %ld", r);
    snap(&b);
    CHECK(snap_same(&a, &b));
}
XT(sdc_validate_each_topology_preset_is_checked_not_applied) {
    NEEDSDC();
    static const UINT presets[] = { SDC_TOPOLOGY_INTERNAL, SDC_TOPOLOGY_CLONE, SDC_TOPOLOGY_EXTEND, SDC_TOPOLOGY_EXTERNAL };
    struct mon_snapshot a, b; snap(&a);
    for (unsigned i = 0; i < 4; i++) {
        LONG r = U(SetDisplayConfig)(0, NULL, 0, NULL, SDC_VALIDATE | presets[i]);
        CHECKF(r == ERROR_SUCCESS || r == XE_GEN_FAILURE, "preset 0x%x: result %ld", presets[i], r);
    }
    snap(&b);
    CHECK(snap_same(&a, &b));
}
XT(sdc_validate_results_are_repeatable) {
    NEEDSDC();
    LONG a = U(SetDisplayConfig)(0, NULL, 0, NULL, SDC_VALIDATE | SDC_TOPOLOGY_INTERNAL);
    LONG b = U(SetDisplayConfig)(0, NULL, 0, NULL, SDC_VALIDATE | SDC_TOPOLOGY_INTERNAL);
    EXPECT_EQ(a, b);
}
XT(sdc_raw_path_count_over_1024_is_invalid_parameter) {
    NEEDSDC();
    static const UINT counts[] = { 0x401, 0x1000, 0xFFFF, 0x7FFFFFFF, 0xFFFFFFFF };
    for (unsigned i = 0; i < sizeof counts / sizeof counts[0]; i++) {
        LONG s = UN(NtUserSetDisplayConfig)(counts[i], NULL, 0, NULL, SDC_VALIDATE);
        CHECKF(s == ST_INVALID_PARAMETER, "count 0x%x: status 0x%lx", counts[i], (unsigned long)s);
    }
}
XT(sdc_raw_rejections_leave_the_monitor_list_alone) {
    NEEDSDC();
    struct mon_snapshot a, b; snap(&a);
    UN(NtUserSetDisplayConfig)(0x401, NULL, 0, NULL, SDC_VALIDATE);
    UN(NtUserSetDisplayConfig)(0, NULL, 0, NULL, 0);
    UN(NtUserSetDisplayConfig)(0xFFFFFFFF, NULL, 0xFFFFFFFF, NULL, SDC_VALIDATE);
    snap(&b);
    CHECK(snap_same(&a, &b));
}
