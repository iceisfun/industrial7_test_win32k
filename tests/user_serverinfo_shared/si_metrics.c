/* GetSystemMetrics for every index versus the SERVERINFO page (aiSysMet / DPI cache) it is served from.
 *
 * user32!RealGetSystemMetrics (19041): nIndex 0..0x60 are handled as
 *   - explicit cases computed from monitor objects / flags / registry (si_metric_special),
 *   - DPI-dependent metrics read from the per-DPI cache from the shared server-info page (slot chosen by the system DPI),
 *   - everything else read straight from aiSysMet[nIndex] from the shared server-info page.
 * nIndex > 0x60 (except 0x1000, 0x1800, 0x2000..0x2006) sets ERROR_INVALID_PARAMETER and returns 0.
 */
#include "si.h"

static void si_check_special(char *psi, int m, int gm, unsigned pd) {
    unsigned long flags = si_u32(psi, SI_dwSRVIFlags);
    bool modern = si_expver_modern();
    struct si_mon_count c = { 0 };
    char *prim = si_primary_monitor_obj();
    RECT rc = {0}, wk = {0};
    bool have = prim && k_try_read(prim + SI_MON_rcMonitor, &rc, sizeof rc) && k_try_read(prim + SI_MON_rcWork, &wk, sizeof wk);
    switch (m) {
    case SM_CXSCREEN: case SM_CYSCREEN:
        /* cases 0/1: width/height of the primary monitor object's rcMonitor (GetPrimaryMonitorRect). */
        CHECK(have);
        if (!have) break;
        if (pd == 96) EXPECT_EQ(gm, m == SM_CXSCREEN ? rc.right - rc.left : rc.bottom - rc.top);
        else t_note("system DPI %u: metric %d = %d, raw monitor %ldx%ld", pd, m, gm, rc.right - rc.left, rc.bottom - rc.top);
        CHECK(gm > 0);
        break;
    case SM_CXFULLSCREEN: case SM_CYFULLSCREEN:
        /* cases 0x10/0x11: primary work area size; the height also loses the caption height (GetDpiDependentMetric(2, dpi)). */
        CHECK(have);
        if (!have) break;
        CHECK(gm > 0);
        if (pd == 96 && modern) {
            if (m == SM_CXFULLSCREEN) EXPECT_EQ(gm, wk.right - wk.left);
            else EXPECT_EQ(gm, wk.bottom - wk.top - U(GetSystemMetrics)(SM_CYCAPTION));
        } else t_note("DPI %u modern=%d metric %d = %d", pd, modern, m, gm);
        break;
    case SM_DBCSENABLED:
        EXPECT_EQ(gm, (flags & 2) ? 1 : 0); break;
    case SM_IMMENABLED:
        EXPECT_EQ(gm, (flags >> 2) & 1); break;
    case SM_MIDEASTENABLED:
        if (!(flags & 8)) EXPECT_EQ(gm, 0); else CHECK(gm == 0 || gm == 1);
        break;
    case SM_XVIRTUALSCREEN: case SM_YVIRTUALSCREEN: case SM_CXVIRTUALSCREEN: case SM_CYVIRTUALSCREEN:
        /* cases 0x4c..0x4f: the desktop (union of all monitors) rectangle. */
        REQUIRE(U(EnumDisplayMonitors));
        U(EnumDisplayMonitors)(NULL, NULL, si_enum_cb, (LPARAM)&c);
        CHECK(c.n >= 1);
        if (pd == 96) {
            int want = m == SM_XVIRTUALSCREEN ? c.u.left : m == SM_YVIRTUALSCREEN ? c.u.top :
                       m == SM_CXVIRTUALSCREEN ? c.u.right - c.u.left : c.u.bottom - c.u.top;
            EXPECT_EQ(gm, want);
        } else t_note("system DPI %u: metric %d = %d", pd, m, gm);
        if (m == SM_CXVIRTUALSCREEN || m == SM_CYVIRTUALSCREEN) CHECK(gm > 0);
        break;
    case SM_CXFRAME: case SM_CYFRAME: case SM_CXPADDEDBORDER:
        /* window-frame metrics: GetWindowFrameMetricForDpiWithCompatFlags2; non-negative, and the sizing frame is never thinner than the fixed frame. */
        CHECK(gm >= 0);
        if (m != SM_CXPADDEDBORDER) CHECK(gm >= U(GetSystemMetrics)(m == SM_CXFRAME ? SM_CXEDGE : SM_CYEDGE));
        break;
    case SM_CXMAXTRACK: case SM_CYMAXTRACK: case SM_CXMAXIMIZED: case SM_CYMAXIMIZED:
        /* GetMaxTrackSize / maximized size = work area + 2 * resizable border; always larger than a minimum window. */
        CHECK(gm > 0);
        if (have && pd == 96) CHECK(gm >= (m == SM_CXMAXIMIZED || m == SM_CXMAXTRACK ? wk.right - wk.left : wk.bottom - wk.top));
        break;
    case 0x56: /* SM_TABLETPC: registry (Tablet PC\IsTabletPC) derived */
        CHECK(gm >= 0); t_note("SM_TABLETPC = %d", gm); break;
    case 0x57: /* SM_MEDIACENTER: registry derived, boolean */
        CHECK(gm == 0 || gm == 1); break;
    case 0x5e: /* SM_DIGITIZER: registry value & 0xcf */
        CHECKF((gm & ~0xcf) == 0, "SM_DIGITIZER=0x%x", gm); break;
    case 0x5f: /* SM_MAXIMUMTOUCHES: (registry value >> 8) & 0xffff */
        CHECKF(gm >= 0 && gm <= 0xffff, "SM_MAXIMUMTOUCHES=%d", gm); break;
    }
}

static void si_check_metric(int m) {
    char *psi = si_get(); if (!psi) return;
    int gm = U(GetSystemMetrics)(m);
    unsigned pd = si_u16(psi, SI_SystemDpi);
    if (si_metric_special(m)) { si_check_special(psi, m, gm, pd); return; }
    bool modern = si_expver_modern();
    int idx = si_cache_idx(m);
    if (idx >= 0) {
        UINT dpi = si_sysdpi_user();
        int slot = si_cache_slot(dpi, pd);
        if (slot < 0) { t_note("metric %d: process DPI %u has no cache slot (page DPI %u)", m, dpi, pd); CHECK(gm >= 0); return; }
        long cached = si_i32(psi, SI_DpiCache + (size_t)(slot * SI_DpiCacheMetrics + idx) * 4);
        if (cached == -1) { t_note("metric %d: cache entry still -1 after the call", m); return; }
        if (!modern) { t_note("metric %d: legacy expected-version adjustment path", m); return; }
        /* GetSystemMetrics returns the cached value for DPI-dependent metrics. */
        /* SM_CXSIZE (0x1e) is widened by user32 above the cached caption height on Windows 10; only the relation holds */
        if (m == SM_CXSIZE) { CHECKF(gm >= cached && cached > 0, "metric %d: GetSystemMetrics=%d cache[slot %d][%d]=%ld", m, gm, slot, idx, cached); return; }
        CHECKF(gm == cached, "metric %d: GetSystemMetrics=%d cache[slot %d][%d]=%ld", m, gm, slot, idx, cached);
        return;
    }
    long pv = si_i32(psi, SI_aiSysMet + (size_t)m * 4);
    if (!modern && (m == SM_CXDLGFRAME || m == SM_CYDLGFRAME)) pv -= 1; /* legacy apps see the dialog frame one pixel thinner */
    CHECKF(gm == pv, "metric %d: GetSystemMetrics=%d aiSysMet=%ld", m, gm, pv);
    /* a second call returns the same datum */
    CHECK(U(GetSystemMetrics)(m) == gm);
}

/* one test per valid index 0..0x60 */
TEST(user_serverinfo_shared_metric_00) { si_check_metric(0); }
TEST(user_serverinfo_shared_metric_01) { si_check_metric(1); }
TEST(user_serverinfo_shared_metric_02) { si_check_metric(2); }
TEST(user_serverinfo_shared_metric_03) { si_check_metric(3); }
TEST(user_serverinfo_shared_metric_04) { si_check_metric(4); }
TEST(user_serverinfo_shared_metric_05) { si_check_metric(5); }
TEST(user_serverinfo_shared_metric_06) { si_check_metric(6); }
TEST(user_serverinfo_shared_metric_07) { si_check_metric(7); }
TEST(user_serverinfo_shared_metric_08) { si_check_metric(8); }
TEST(user_serverinfo_shared_metric_09) { si_check_metric(9); }
TEST(user_serverinfo_shared_metric_10) { si_check_metric(10); }
TEST(user_serverinfo_shared_metric_11) { si_check_metric(11); }
TEST(user_serverinfo_shared_metric_12) { si_check_metric(12); }
TEST(user_serverinfo_shared_metric_13) { si_check_metric(13); }
TEST(user_serverinfo_shared_metric_14) { si_check_metric(14); }
TEST(user_serverinfo_shared_metric_15) { si_check_metric(15); }
TEST(user_serverinfo_shared_metric_16) { si_check_metric(16); }
TEST(user_serverinfo_shared_metric_17) { si_check_metric(17); }
TEST(user_serverinfo_shared_metric_18) { si_check_metric(18); }
TEST(user_serverinfo_shared_metric_19) { si_check_metric(19); }
TEST(user_serverinfo_shared_metric_20) { si_check_metric(20); }
TEST(user_serverinfo_shared_metric_21) { si_check_metric(21); }
TEST(user_serverinfo_shared_metric_22) { si_check_metric(22); }
TEST(user_serverinfo_shared_metric_23) { si_check_metric(23); }
TEST(user_serverinfo_shared_metric_24) { si_check_metric(24); }
TEST(user_serverinfo_shared_metric_25) { si_check_metric(25); }
TEST(user_serverinfo_shared_metric_26) { si_check_metric(26); }
TEST(user_serverinfo_shared_metric_27) { si_check_metric(27); }
TEST(user_serverinfo_shared_metric_28) { si_check_metric(28); }
TEST(user_serverinfo_shared_metric_29) { si_check_metric(29); }
TEST(user_serverinfo_shared_metric_30) { si_check_metric(30); }
TEST(user_serverinfo_shared_metric_31) { si_check_metric(31); }
TEST(user_serverinfo_shared_metric_32) { si_check_metric(32); }
TEST(user_serverinfo_shared_metric_33) { si_check_metric(33); }
TEST(user_serverinfo_shared_metric_34) { si_check_metric(34); }
TEST(user_serverinfo_shared_metric_35) { si_check_metric(35); }
TEST(user_serverinfo_shared_metric_36) { si_check_metric(36); }
TEST(user_serverinfo_shared_metric_37) { si_check_metric(37); }
TEST(user_serverinfo_shared_metric_38) { si_check_metric(38); }
TEST(user_serverinfo_shared_metric_39) { si_check_metric(39); }
TEST(user_serverinfo_shared_metric_40) { si_check_metric(40); }
TEST(user_serverinfo_shared_metric_41) { si_check_metric(41); }
TEST(user_serverinfo_shared_metric_42) { si_check_metric(42); }
TEST(user_serverinfo_shared_metric_43) { si_check_metric(43); }
TEST(user_serverinfo_shared_metric_44) { si_check_metric(44); }
TEST(user_serverinfo_shared_metric_45) { si_check_metric(45); }
TEST(user_serverinfo_shared_metric_46) { si_check_metric(46); }
TEST(user_serverinfo_shared_metric_47) { si_check_metric(47); }
TEST(user_serverinfo_shared_metric_48) { si_check_metric(48); }
TEST(user_serverinfo_shared_metric_49) { si_check_metric(49); }
TEST(user_serverinfo_shared_metric_50) { si_check_metric(50); }
TEST(user_serverinfo_shared_metric_51) { si_check_metric(51); }
TEST(user_serverinfo_shared_metric_52) { si_check_metric(52); }
TEST(user_serverinfo_shared_metric_53) { si_check_metric(53); }
TEST(user_serverinfo_shared_metric_54) { si_check_metric(54); }
TEST(user_serverinfo_shared_metric_55) { si_check_metric(55); }
TEST(user_serverinfo_shared_metric_56) { si_check_metric(56); }
TEST(user_serverinfo_shared_metric_57) { si_check_metric(57); }
TEST(user_serverinfo_shared_metric_58) { si_check_metric(58); }
TEST(user_serverinfo_shared_metric_59) { si_check_metric(59); }
TEST(user_serverinfo_shared_metric_60) { si_check_metric(60); }
TEST(user_serverinfo_shared_metric_61) { si_check_metric(61); }
TEST(user_serverinfo_shared_metric_62) { si_check_metric(62); }
TEST(user_serverinfo_shared_metric_63) { si_check_metric(63); }
TEST(user_serverinfo_shared_metric_64) { si_check_metric(64); }
TEST(user_serverinfo_shared_metric_65) { si_check_metric(65); }
TEST(user_serverinfo_shared_metric_66) { si_check_metric(66); }
TEST(user_serverinfo_shared_metric_67) { si_check_metric(67); }
TEST(user_serverinfo_shared_metric_68) { si_check_metric(68); }
TEST(user_serverinfo_shared_metric_69) { si_check_metric(69); }
TEST(user_serverinfo_shared_metric_70) { si_check_metric(70); }
TEST(user_serverinfo_shared_metric_71) { si_check_metric(71); }
TEST(user_serverinfo_shared_metric_72) { si_check_metric(72); }
TEST(user_serverinfo_shared_metric_73) { si_check_metric(73); }
TEST(user_serverinfo_shared_metric_74) { si_check_metric(74); }
TEST(user_serverinfo_shared_metric_75) { si_check_metric(75); }
TEST(user_serverinfo_shared_metric_76) { si_check_metric(76); }
TEST(user_serverinfo_shared_metric_77) { si_check_metric(77); }
TEST(user_serverinfo_shared_metric_78) { si_check_metric(78); }
TEST(user_serverinfo_shared_metric_79) { si_check_metric(79); }
TEST(user_serverinfo_shared_metric_80) { si_check_metric(80); }
TEST(user_serverinfo_shared_metric_81) { si_check_metric(81); }
TEST(user_serverinfo_shared_metric_82) { si_check_metric(82); }
TEST(user_serverinfo_shared_metric_83) { si_check_metric(83); }
TEST(user_serverinfo_shared_metric_84) { si_check_metric(84); }
TEST(user_serverinfo_shared_metric_85) { si_check_metric(85); }
TEST(user_serverinfo_shared_metric_86) { si_check_metric(86); }
TEST(user_serverinfo_shared_metric_87) { si_check_metric(87); }
TEST(user_serverinfo_shared_metric_88) { si_check_metric(88); }
TEST(user_serverinfo_shared_metric_89) { si_check_metric(89); }
TEST(user_serverinfo_shared_metric_90) { si_check_metric(90); }
TEST(user_serverinfo_shared_metric_91) { si_check_metric(91); }
TEST(user_serverinfo_shared_metric_92) { si_check_metric(92); }
TEST(user_serverinfo_shared_metric_93) { si_check_metric(93); }
TEST(user_serverinfo_shared_metric_94) { si_check_metric(94); }
TEST(user_serverinfo_shared_metric_95) { si_check_metric(95); }
TEST(user_serverinfo_shared_metric_96) { si_check_metric(96); }

/* ---------------- invalid and special indices ---------------- */
static void si_bad_index(int idx) {
    REQUIRE(U(GetSystemMetrics));
    U(GetSystemMetrics)(SM_CXSCREEN);
    SET_ERR(0);
    int r = U(GetSystemMetrics)(idx);
    /* RealGetSystemMetrics: (uint)nIndex > 0x60 after the special ranges -> ERROR_INVALID_PARAMETER, 0 */
    EXPECT_EQ(r, 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}
#define BADIDX(name, v) TEST(user_serverinfo_shared_metric_invalid_##name) { si_bad_index(v); }
BADIDX(0x61, 0x61)
BADIDX(0x62, 0x62)
BADIDX(0x7f, 0x7f)
BADIDX(0x100, 0x100)
BADIDX(0x0fff, 0x0fff)
BADIDX(0x1001, 0x1001)
BADIDX(0x17ff, 0x17ff)
BADIDX(0x1801, 0x1801)
BADIDX(0x1fff, 0x1fff)
BADIDX(0x2007, 0x2007)
BADIDX(0x2008, 0x2008)
BADIDX(0x7fff, 0x7fff)
BADIDX(neg1, -1)
BADIDX(neg100, -100)
BADIDX(intmax, 0x7fffffff)
BADIDX(intmin, (int)0x80000000)
BADIDX(0xffff, 0xffff)
BADIDX(0x10000, 0x10000)

TEST(user_serverinfo_shared_metric_cmetrics_constant_is_invalid) {
    /* SM_CMETRICS (97 on this build) is one past the last metric. */
    si_bad_index(SM_CMETRICS > 0x60 ? SM_CMETRICS : 0x61);
}
TEST(user_serverinfo_shared_metric_index_0x5a_is_served) {
    REQUIRE(U(GetSystemMetrics));
    /* GetSystemMetrics routes 0x5a (and 0x2000) straight to RealGetSystemMetrics without the API-hook dispatch; still a valid index. */
    SET_ERR(0);
    int v = U(GetSystemMetrics)(0x5a);
    CHECK(v >= 0);
    EXPECT_ERR(0);
}
TEST(user_serverinfo_shared_metric_valid_leaves_last_error_alone) {
    REQUIRE(U(GetSystemMetrics));
    U(GetSystemMetrics)(0);
    SET_ERR(0xdead);
    /* generic page-served indices run purely client-side in RealGetSystemMetrics: no error is written on success */
    for (int i = 0; i <= 0x60; i++) if (!si_metric_special(i) && si_cache_idx(i) < 0) U(GetSystemMetrics)(i);
    EXPECT_ERR(0xdead);
}
TEST(user_serverinfo_shared_metric_values_non_negative) {
    REQUIRE(U(GetSystemMetrics));
    U(GetSystemMetrics)(0);
    for (int i = 0; i <= 0x60; i++) {
        if (i == SM_XVIRTUALSCREEN || i == SM_YVIRTUALSCREEN) continue; /* may legitimately be negative with a monitor left of/above the primary */
        int v = U(GetSystemMetrics)(i);
        CHECKF(v >= 0, "metric %d = %d", i, v);
    }
}
TEST(user_serverinfo_shared_metric_boolean_metrics_are_0_or_1) {
    REQUIRE(U(GetSystemMetrics));
    static const int b[] = { SM_MOUSEPRESENT, SM_DEBUG, SM_SWAPBUTTON, SM_PENWINDOWS, SM_DBCSENABLED, SM_MENUDROPALIGNMENT,
                             SM_SHOWSOUNDS, SM_SLOWMACHINE, SM_MIDEASTENABLED, SM_MOUSEWHEELPRESENT, SM_SAMEDISPLAYFORMAT,
                             SM_IMMENABLED, SM_MOUSEHORIZONTALWHEELPRESENT };
    U(GetSystemMetrics)(0);
    for (unsigned i = 0; i < sizeof b / sizeof *b; i++) {
        int v = U(GetSystemMetrics)(b[i]);
        CHECKF(v == 0 || v == 1, "metric %d = %d", b[i], v);
    }
}
TEST(user_serverinfo_shared_metric_sm_secure_is_zero) {
    REQUIRE(U(GetSystemMetrics));
    CHECK(U(GetSystemMetrics)(SM_SECURE) == 0); /* documented: always 0 */
}
TEST(user_serverinfo_shared_metric_cleanboot_range) {
    REQUIRE(U(GetSystemMetrics));
    int v = U(GetSystemMetrics)(SM_CLEANBOOT);
    CHECKF(v >= 0 && v <= 2, "SM_CLEANBOOT=%d", v); /* 0 normal, 1 fail-safe, 2 fail-safe with network */
}
TEST(user_serverinfo_shared_metric_cmousebuttons_matches_mousepresent) {
    char *psi = si_get(); if (!psi) return;
    int btn = U(GetSystemMetrics)(SM_CMOUSEBUTTONS), present = U(GetSystemMetrics)(SM_MOUSEPRESENT);
    if (present) CHECK(btn >= 2 || btn == 0); /* with a mouse attached the count is at least 2 on any real or VM pointer */
    t_note("SM_MOUSEPRESENT=%d SM_CMOUSEBUTTONS=%d", present, btn);
}
TEST(user_serverinfo_shared_metric_frame_aliases) {
    REQUIRE(U(GetSystemMetrics));
    /* SM_CXFRAME/SM_CXSIZEFRAME and SM_CXFIXEDFRAME/SM_CXDLGFRAME share an index. */
    CHECK(SM_CXFRAME == SM_CXSIZEFRAME && SM_CYFRAME == SM_CYSIZEFRAME);
    CHECK(SM_CXFIXEDFRAME == SM_CXDLGFRAME && SM_CYFIXEDFRAME == SM_CYDLGFRAME);
    CHECK(U(GetSystemMetrics)(SM_CXFIXEDFRAME) == U(GetSystemMetrics)(SM_CXDLGFRAME));
    CHECK(U(GetSystemMetrics)(SM_CXMINTRACK) == U(GetSystemMetrics)(34));
}
TEST(user_serverinfo_shared_metric_min_and_mintrack_share_cache_slot) {
    char *psi = si_get(); if (!psi) return;
    /* GetDpiDepSysMetCacheMetricSlot maps both SM_CXMIN(0x1c) and SM_CXMINTRACK(0x22) to cache index 0x10, SM_CYMIN/CYMINTRACK to 0x11. */
    EXPECT_EQ(si_cache_idx(0x1c), si_cache_idx(0x22));
    EXPECT_EQ(si_cache_idx(0x1d), si_cache_idx(0x23));
    if (si_expver_modern()) {
        CHECK(U(GetSystemMetrics)(0x1c) == U(GetSystemMetrics)(0x22));
        CHECK(U(GetSystemMetrics)(0x1d) == U(GetSystemMetrics)(0x23));
    }
}
TEST(user_serverinfo_shared_metric_page_array_not_all_zero) {
    char *psi = si_get(); if (!psi) return;
    int nz = 0;
    for (int i = 0; i <= 0x60; i++) if (si_i32(psi, SI_aiSysMet + i * 4)) nz++;
    CHECKF(nz > 20, "only %d non-zero aiSysMet entries", nz);
    /* DPI-dependent metrics (screen size, scrollbars) live in the per-DPI cache, not here; these four are plain entries */
    static const int must[] = { SM_CXBORDER, SM_CYBORDER, SM_MOUSEPRESENT, SM_CMOUSEBUTTONS };
    for (unsigned i = 0; i < sizeof must / sizeof *must; i++)
        CHECKF(si_i32(psi, SI_aiSysMet + must[i] * 4) != 0, "aiSysMet[%d] is zero", must[i]);
}
TEST(user_serverinfo_shared_metric_mouse_wheel_present_vs_horizontal) {
    char *psi = si_get(); if (!psi) return;
    t_note("SM_MOUSEWHEELPRESENT=%d SM_MOUSEHORIZONTALWHEELPRESENT=%d", U(GetSystemMetrics)(SM_MOUSEWHEELPRESENT), U(GetSystemMetrics)(SM_MOUSEHORIZONTALWHEELPRESENT));
    CHECK(si_i32(psi, SI_aiSysMet + 75 * 4) == U(GetSystemMetrics)(SM_MOUSEWHEELPRESENT));
}

/* ---------------- GetSystemMetricsForDpi and the per-DPI cache ---------------- */
static const int si_fordpi_metrics[SI_DpiCacheMetrics] = {
    2, 3, 4, 9, 10, 11, 12, 13, 14, 15, 0x14, 0x15, 0x1e, 0x1f, 0x20, 0x21, 0x1c, 0x1d,
    0x26, 0x27, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x47, 0x48, 0x5c };

static void si_fordpi_vs_cache(UINT dpi) {
    char *psi = si_get(); if (!psi) return;
    REQUIRE(UN(GetSystemMetricsForDpi));
    unsigned pd = si_u16(psi, SI_SystemDpi);
    int slot = si_cache_slot(dpi, pd);
    if (slot < 0) { t_skip("dpi %u has no cache slot", dpi); return; }
    for (int i = 0; i < SI_DpiCacheMetrics; i++) {
        int m = si_fordpi_metrics[i];
        int v = UN(GetSystemMetricsForDpi)(m, dpi);
        long c = si_i32(psi, SI_DpiCache + (size_t)(slot * SI_DpiCacheMetrics + i) * 4);
        /* RealGetSystemMetricsForDpi -> GetDpiDependentMetric fills the slot through NtUserCallOneParam(dpi, 0x50) when it is -1. */
        if (c == -1) { t_note("dpi %u metric %d: slot entry still -1 (value %d)", dpi, m, v); continue; }
        if (m == SM_CXSIZE) CHECKF(v >= c && c > 0, "dpi %u slot %d metric 0x%x: API=%d page=%ld", dpi, slot, m, v, c); /* widened by user32 above the cache */
        else CHECKF(v == c, "dpi %u slot %d metric 0x%x: API=%d page=%ld", dpi, slot, m, v, c);
        CHECK(v >= 0);
    }
}
#define FORDPI(d) TEST(user_serverinfo_shared_fordpi_cache_##d) { si_fordpi_vs_cache(d); }
FORDPI(96)
FORDPI(120)
FORDPI(144)
FORDPI(168)
FORDPI(192)
FORDPI(216)
FORDPI(240)
FORDPI(288)
FORDPI(384)
FORDPI(480)

TEST(user_serverinfo_shared_fordpi_cache_system_dpi) {
    char *psi = si_get(); if (!psi) return;
    si_fordpi_vs_cache(si_u16(psi, SI_SystemDpi));
}
TEST(user_serverinfo_shared_fordpi_slot_zero_entries_are_filled) {
    char *psi = si_get(); if (!psi) return;
    REQUIRE(UN(GetSystemMetricsForDpi));
    unsigned pd = si_u16(psi, SI_SystemDpi);
    for (int i = 0; i < SI_DpiCacheMetrics; i++) UN(GetSystemMetricsForDpi)(si_fordpi_metrics[i], pd);
    for (int i = 0; i < SI_DpiCacheMetrics; i++) {
        long c = si_i32(psi, SI_DpiCache + (size_t)i * 4);
        CHECKF(c != -1 && c >= 0, "system-DPI slot entry %d = %ld", i, c);
    }
}
TEST(user_serverinfo_shared_fordpi_invalid_dpi_zero) {
    REQUIRE(UN(GetSystemMetricsForDpi));
    U(GetSystemMetrics)(0);
    SET_ERR(0);
    /* RealGetSystemMetricsForDpi: (0 < dpi && -1 < nIndex) else SetLastError(ERROR_INVALID_PARAMETER), 0 */
    EXPECT_EQ(UN(GetSystemMetricsForDpi)(SM_CXVSCROLL, 0), 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}
TEST(user_serverinfo_shared_fordpi_invalid_dpi_negative) {
    REQUIRE(UN(GetSystemMetricsForDpi));
    U(GetSystemMetrics)(0);
    SET_ERR(0);
    EXPECT_EQ(UN(GetSystemMetricsForDpi)(SM_CXVSCROLL, 0xffffffffu), 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}
TEST(user_serverinfo_shared_fordpi_invalid_metric_negative) {
    REQUIRE(UN(GetSystemMetricsForDpi));
    U(GetSystemMetrics)(0);
    SET_ERR(0);
    EXPECT_EQ(UN(GetSystemMetricsForDpi)(-1, 96), 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}
TEST(user_serverinfo_shared_fordpi_metric_above_range) {
    REQUIRE(UN(GetSystemMetricsForDpi));
    U(GetSystemMetrics)(0);
    SET_ERR(0);
    /* non-cached indices fall through to RealGetSystemMetrics, which rejects > 0x60 */
    EXPECT_EQ(UN(GetSystemMetricsForDpi)(0x61, 96), 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}
TEST(user_serverinfo_shared_fordpi_uncached_metric_ignores_dpi) {
    char *psi = si_get(); if (!psi) return;
    REQUIRE(UN(GetSystemMetricsForDpi));
    static const int m[] = { SM_CXSCREEN, SM_CYSCREEN, SM_CMOUSEBUTTONS, SM_CXBORDER, SM_CYBORDER, SM_CMONITORS, SM_MOUSEPRESENT, SM_SWAPBUTTON, SM_CXDOUBLECLK };
    for (unsigned i = 0; i < sizeof m / sizeof *m; i++) {
        int a = UN(GetSystemMetricsForDpi)(m[i], 96), b = UN(GetSystemMetricsForDpi)(m[i], 192), c = U(GetSystemMetrics)(m[i]);
        /* the default case of RealGetSystemMetricsForDpi calls RealGetSystemMetrics(nIndex), ignoring the DPI */
        CHECKF(a == c && b == c, "metric %d: dpi96=%d dpi192=%d plain=%d", m[i], a, b, c);
    }
}
TEST(user_serverinfo_shared_fordpi_non_cached_dpi_positive_scroll) {
    REQUIRE(UN(GetSystemMetricsForDpi));
    U(GetSystemMetrics)(0);
    static const UINT d[] = { 97, 100, 110, 130, 150, 200, 333 };
    for (unsigned i = 0; i < sizeof d / sizeof *d; i++) {
        /* DPIs without a cache slot go to NtUserCallTwoParam(index, dpi, 0x8d) and are computed by the kernel. */
        int v = UN(GetSystemMetricsForDpi)(SM_CXVSCROLL, d[i]);
        CHECKF(v > 0, "dpi %u: SM_CXVSCROLL=%d", d[i], v);
    }
}
TEST(user_serverinfo_shared_fordpi_scales_monotonically) {
    REQUIRE(UN(GetSystemMetricsForDpi));
    U(GetSystemMetrics)(0);
    static const int m[] = { SM_CXVSCROLL, SM_CYHSCROLL, SM_CYCAPTION, SM_CXICON, SM_CYICON, SM_CXSMICON, SM_CYSMICON, SM_CXSIZE, SM_CYMENU };
    static const UINT d[] = { 96, 120, 144, 168, 192, 240 };
    for (unsigned i = 0; i < sizeof m / sizeof *m; i++) {
        int prev = 0;
        for (unsigned j = 0; j < sizeof d / sizeof *d; j++) {
            int v = UN(GetSystemMetricsForDpi)(m[i], d[j]);
            CHECKF(v >= prev, "metric %d dpi %u: %d < previous %d", m[i], d[j], v, prev);
            prev = v;
        }
    }
}
TEST(user_serverinfo_shared_fordpi_at_96_matches_plain_for_unaware) {
    char *psi = si_get(); if (!psi) return;
    REQUIRE(UN(GetSystemMetricsForDpi)); REQUIRE(UN(IsProcessDPIAware));
    if (UN(IsProcessDPIAware)()) { t_skip("aware process: plain metrics are at the system DPI"); return; }
    /* An unaware thread looks up cache slot GetDpiCacheSlot(96): the ForDpi(96) value is the very same cache entry. */
    for (int i = 0; i < SI_DpiCacheMetrics; i++) {
        int m = si_fordpi_metrics[i];
        if (m == 0x20 || m == 0x21 || m == 0x5c) continue; /* frame metrics take the compat-flags path in the plain API */
        CHECKF(UN(GetSystemMetricsForDpi)(m, 96) == U(GetSystemMetrics)(m), "metric 0x%x", m);
    }
}
TEST(user_serverinfo_shared_dpi_cache_unused_slots_are_minus_one_or_filled) {
    char *psi = si_get(); if (!psi) return;
    /* every cache cell is either the -1 "not computed yet" marker or a plausible metric value */
    for (int s = 0; s < SI_DpiCacheSlots; s++)
        for (int i = 0; i < SI_DpiCacheMetrics; i++) {
            long c = si_i32(psi, SI_DpiCache + (size_t)(s * SI_DpiCacheMetrics + i) * 4);
            CHECKF(c == -1 || (c >= 0 && c < 4096), "slot %d idx %d = %ld", s, i, c);
        }
}
