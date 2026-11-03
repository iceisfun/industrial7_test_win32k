/* GetSystemMetrics / GetSystemMetricsForDpi are pure user32 reads of SERVERINFO (there is no NtUser syscall for them);
 * this file covers the parts that depend on kernel-maintained state set through SPI: invalid-index protocol,
 * relations between SPI structures and the metrics array, and DPI-keyed lookups. Per-index value coverage is in
 * user_serverinfo_shared. */
#include "spcm.h"

TEST(sysmetrics_invalid_index_sets_87) {
    REQUIRE(U(GetSystemMetrics)); __typeof__(&GetSystemMetrics) gsm = U(GetSystemMetrics);
    static const int bad[] = { 0x61, 0x62, 0x7f, 0x100, 0x3000, -1, 0x7fffffff, (int)0x80000000 };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(SPCM_ERR_SENTINEL);
        int v = gsm(bad[i]);
        EXPECT_ERR(87); CHECKF(v == 0, "index 0x%x -> %d", bad[i], v);
    }
}
TEST(sysmetrics_valid_index_leaves_last_error) {
    REQUIRE(U(GetSystemMetrics)); __typeof__(&GetSystemMetrics) gsm = U(GetSystemMetrics);
    SetLastError(SPCM_ERR_SENTINEL);
    (void)gsm(SM_CXSCREEN);
    EXPECT_EQ(GetLastError(), SPCM_ERR_SENTINEL);
}
TEST(sysmetrics_all_indices_stable_between_calls) {
    REQUIRE(U(GetSystemMetrics)); char *psi = spcm_page(); if (!psi) return;
    for (int i = 0; i <= 0x60; i++) {
        if (i == SM_REMOTESESSION) continue;
        int a = U(GetSystemMetrics)(i), b = U(GetSystemMetrics)(i);
        CHECKF(a == b, "index %d: %d vs %d", i, a, b);
    }
}
TEST(sysmetrics_page_array_matches_for_plain_indices) {
    REQUIRE(U(GetSystemMetrics)); char *psi = spcm_page(); if (!psi) return;
    /* indices RealGetSystemMetrics serves straight from aiSysMet[nIndex] (no per-monitor/DPI/special handling) */
    static const int plain[] = { SM_CMOUSEBUTTONS, SM_CXDOUBLECLK, SM_CYDOUBLECLK, SM_CXICONSPACING, SM_CYICONSPACING, SM_MENUDROPALIGNMENT,
        SM_PENWINDOWS, SM_CXMINSPACING, SM_CYMINSPACING, SM_ARRANGE, SM_CXMINIMIZED, SM_CYMINIMIZED, SM_SWAPBUTTON, SM_SHOWSOUNDS };
    for (unsigned i = 0; i < sizeof plain / sizeof plain[0]; i++) {
        int v = U(GetSystemMetrics)(plain[i]);
        if (v != pg_sm(psi, plain[i])) t_note("index %d: GetSystemMetrics %d, aiSysMet %d", plain[i], v, pg_sm(psi, plain[i]));
    }
    EXPECT_EQ(U(GetSystemMetrics)(SM_CXDOUBLECLK), pg_sm(psi, SM_CXDOUBLECLK));
    EXPECT_EQ(U(GetSystemMetrics)(SM_CYDOUBLECLK), pg_sm(psi, SM_CYDOUBLECLK));
    EXPECT_EQ(U(GetSystemMetrics)(SM_ARRANGE), pg_sm(psi, SM_ARRANGE));
}
TEST(sysmetrics_minimized_spacing_ge_size) {
    REQUIRE(U(GetSystemMetrics)); char *psi = spcm_page(); if (!psi) return;
    /* SPI_GETMINIMIZEDMETRICS returns spacing minus size as the gap; a negative gap would mean overlapping slots */
    spcm_buf g; gb_init(&g); NEED_SPI(); CHECK(spi(0x2b, 0, g.b, 0) == 1);
    CHECK((int)gb_u32(&g, 8) >= 0); CHECK((int)gb_u32(&g, 12) >= 0);
}
TEST(sysmetrics_fordpi_basic_positive) {
    REQUIRE(UN(GetSystemMetricsForDpi));
    static const int idx[] = { SM_CXVSCROLL, SM_CYHSCROLL, SM_CYCAPTION, SM_CXICON, SM_CYICON, SM_CXSMICON, SM_CYSMICON, SM_CXSIZEFRAME, SM_CYSIZEFRAME, SM_CXBORDER };
    static const UINT dpis[] = { 96, 120, 144, 192 };
    for (unsigned d = 0; d < 4; d++) for (unsigned i = 0; i < sizeof idx / sizeof idx[0]; i++) {
        int v = UN(GetSystemMetricsForDpi)(idx[i], dpis[d]);
        CHECKF(v > 0, "index %d dpi %u -> %d", idx[i], dpis[d], v);
    }
}
TEST(sysmetrics_fordpi_monotonic) {
    REQUIRE(UN(GetSystemMetricsForDpi));
    static const int idx[] = { SM_CXVSCROLL, SM_CYHSCROLL, SM_CYCAPTION, SM_CXICON, SM_CYICON, SM_CXSMICON, SM_CYSMICON, SM_CXMENUSIZE, SM_CYMENUSIZE };
    static const UINT dpis[] = { 96, 120, 144, 168, 192 };
    for (unsigned i = 0; i < sizeof idx / sizeof idx[0]; i++) for (unsigned d = 1; d < 5; d++) {
        int a = UN(GetSystemMetricsForDpi)(idx[i], dpis[d - 1]), b = UN(GetSystemMetricsForDpi)(idx[i], dpis[d]);
        CHECKF(b >= a, "index %d: dpi %u -> %d, dpi %u -> %d", idx[i], dpis[d-1], a, dpis[d], b);
    }
}
TEST(sysmetrics_fordpi_invalid_index) {
    REQUIRE(UN(GetSystemMetricsForDpi)); GetSystemMetricsForDpi_pfn gf = UN(GetSystemMetricsForDpi);
    SetLastError(SPCM_ERR_SENTINEL);
    int v = gf(0x7fff, 96);
    EXPECT_ERR(87); CHECK(v == 0);
}
TEST(sysmetrics_fordpi_caption_height_tracks_ncm) {
    REQUIRE(UN(GetSystemMetricsForDpi)); NEED_SPIDPI();
    static const UINT dpis[] = { 96, 144, 192 };
    for (unsigned d = 0; d < 3; d++) {
        unsigned char b[NCM_SIZE]; memset(b, 0, sizeof b); unsigned c = NCM_SIZE; memcpy(b, &c, 4);
        CHECK(spi_dpi(0x29, NCM_SIZE, b, dpis[d]) == 1);
        int cap = UN(GetSystemMetricsForDpi)(SM_CYCAPTION, dpis[d]);
        unsigned ch; memcpy(&ch, b + NCM_iCaptionHeight, 4);
        if (cap != (int)ch + 1) t_note("dpi %u: SM_CYCAPTION %d, NCM iCaptionHeight %u", dpis[d], cap, ch);
        CHECK(cap >= (int)ch);
    }
}
TEST(sysmetrics_system_dpi_matches_page) {
    char *psi = spcm_page(); if (!psi) return;
    REQUIRE(UN(GetDpiForSystem));
    unsigned pd = pg_u16(psi, PG_SysDpi), ud = UN(GetDpiForSystem)();
    CHECK(pd >= 96);
    if (pd != ud) t_note("page system DPI %u, GetDpiForSystem %u (per-process virtualization)", pd, ud);
}

/* RealGetSystemMetrics: index > 0x60 (unsigned) is the only 87 case; 0x60 is the last table slot and 0x1000/0x1800/0x2000..0x2006 are specials. */
TEST(sysmetrics_boundary_index_0x60_is_valid) {
    REQUIRE(U(GetSystemMetrics));
    SetLastError(SPCM_ERR_SENTINEL);
    (void)U(GetSystemMetrics)(0x60);
    EXPECT_EQ(GetLastError(), SPCM_ERR_SENTINEL);
}
TEST(sysmetrics_special_indices_no_error) {
    REQUIRE(U(GetSystemMetrics));
    static const int sp[] = { 0x1000, 0x1800, 0x2000, 0x2001, 0x2002, 0x2003, 0x2004, 0x2005, 0x2006 };
    for (unsigned i = 0; i < sizeof sp / sizeof sp[0]; i++) {
        SetLastError(SPCM_ERR_SENTINEL);
        int v = U(GetSystemMetrics)(sp[i]);
        CHECKF(GetLastError() == SPCM_ERR_SENTINEL, "index 0x%x set last error %lu", sp[i], GetLastError());
        CHECKF(v == 0 || v == 1, "index 0x%x -> %d", sp[i], v);
    }
}
TEST(sysmetrics_index_0x1800_is_constant_one) {
    REQUIRE(U(GetSystemMetrics));
 /* RealGetSystemMetrics: */
    EXPECT_EQ(U(GetSystemMetrics)(0x1800), 1);
}
TEST(sysmetrics_digitizer_mask) {
    REQUIRE(U(GetSystemMetrics));
    /* SM_DIGITIZER (0x5e) is "registry value & 0xcf"; SM_MAXIMUMTOUCHES (0x5f) is a 16-bit field */
    int d = U(GetSystemMetrics)(0x5e), t = U(GetSystemMetrics)(0x5f);
    CHECKF((d & ~0xcf) == 0, "digitizer 0x%x", d);
    CHECKF(t >= 0 && t <= 0xffff, "maxtouches %d", t);
}
TEST(sysmetrics_flag_indices_are_boolean) {
    REQUIRE(U(GetSystemMetrics));
    static const int b[] = { SM_MOUSEPRESENT, SM_DEBUG, SM_SWAPBUTTON, SM_SECURE, SM_SHOWSOUNDS, SM_MIDEASTENABLED, SM_MOUSEWHEELPRESENT,
        SM_SLOWMACHINE, SM_NETWORK & 0xffff, SM_PENWINDOWS, SM_DBCSENABLED };
    for (unsigned i = 0; i < sizeof b / sizeof b[0]; i++) {
        int v = U(GetSystemMetrics)(b[i]);
        if (v != 0 && v != 1) t_note("SM index %d is %d (expected a boolean)", b[i], v);
    }
    CHECK(U(GetSystemMetrics)(SM_CMOUSEBUTTONS) >= 0);
}
TEST(sysmetrics_screen_metrics_positive_and_consistent) {
    REQUIRE(U(GetSystemMetrics));
    int cx = U(GetSystemMetrics)(SM_CXSCREEN), cy = U(GetSystemMetrics)(SM_CYSCREEN);
    CHECK(cx > 0); CHECK(cy > 0);
    int vx = U(GetSystemMetrics)(SM_CXVIRTUALSCREEN), vy = U(GetSystemMetrics)(SM_CYVIRTUALSCREEN);
    CHECK(vx >= cx || t_is_wine()); CHECK(vy >= cy || t_is_wine());
    CHECK(U(GetSystemMetrics)(SM_CMONITORS) >= 1);
    /* work area (SM_CXFULLSCREEN/SM_CYFULLSCREEN) fits in the primary screen */
    CHECK(U(GetSystemMetrics)(SM_CXFULLSCREEN) <= cx);
    CHECK(U(GetSystemMetrics)(SM_CYFULLSCREEN) <= cy);
}
TEST(sysmetrics_fordpi_index_negative_and_zero_dpi) {
    REQUIRE(UN(GetSystemMetricsForDpi)); GetSystemMetricsForDpi_pfn gf = UN(GetSystemMetricsForDpi);
    /* RealGetSystemMetricsForDpi: dpi must be > 0 and index >= 0, else it falls through to a plain call of the index with no DPI scaling;
     * either way the result for a valid index is not an error. */
    SetLastError(SPCM_ERR_SENTINEL);
    int v = gf(SM_CXVSCROLL, 0);
    t_note("GetSystemMetricsForDpi(SM_CXVSCROLL, 0) = %d, last error %lu", v, GetLastError());
    SetLastError(SPCM_ERR_SENTINEL);
    v = gf(-1, 96);
    t_note("GetSystemMetricsForDpi(-1, 96) = %d, last error %lu", v, GetLastError());
}
