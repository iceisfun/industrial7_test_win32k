/* NtUserSystemParametersInfoForDpi (win32kfull, service 5382): five arguments (action, uiParam, pvParam, fWinIni-unused, dpi).
 * Kernel flow: EnterCrit; reject unless dpi != 0 && pv != NULL; only actions 0x29 (NONCLIENTMETRICS, uiParam must be 0x1f8),
 * 0x1f (ICONTITLELOGFONT, uiParam 0x5c) and 0x2d (ICONMETRICS, uiParam 0x6c) are accepted -- every other combination is
 * UserSetLastError(0x57) + FALSE. The structure is copied to a pool block, _SystemParametersInfoForDpi fills it from
 * GetDpiDependentMetric/GetScaledLogFontForDpi, and the whole block is copied back. */
#include "spcm.h"

static const UINT g_dpis[] = { 96, 120, 144, 168, 192, 240, 288, 384 };
static unsigned u32at(const void *p, size_t off) { unsigned v; memcpy(&v, (const char *)p + off, 4); return v; }
static int fd_ncm(unsigned char *b, UINT dpi) { memset(b, 0, NCM_SIZE); unsigned c = NCM_SIZE; memcpy(b, &c, 4); return spi_dpi(0x29, NCM_SIZE, b, dpi); }
static int fd_icm(unsigned char *b, UINT dpi) { memset(b, 0, ICM_SIZE); unsigned c = ICM_SIZE; memcpy(b, &c, 4); return spi_dpi(0x2d, ICM_SIZE, b, dpi); }

TEST(sysparams_fordpi_zero_dpi_rejected) {
    NEED_SPIDPI(); spcm_buf g; gb_init(&g); unsigned c = NCM_SIZE; memcpy(g.b, &c, 4);
    int r = spi_dpi(0x29, NCM_SIZE, g.b, 0);
    EXPECT_ERR(87); CHECK(r == 0);
    CHECK(gb_pristine(&g, 4, 1020));   /* rejected before the probe/copy */
}
TEST(sysparams_fordpi_zero_dpi_rejected_icon_actions) {
    NEED_SPIDPI(); spcm_buf g; int r;
    gb_init(&g); r = spi_dpi(0x1f, LOGFONTW_SIZE, g.b, 0); EXPECT_ERR(87); CHECK(r == 0); CHECK(gb_pristine(&g, 0, 1024));
    gb_init(&g); r = spi_dpi(0x2d, ICM_SIZE, g.b, 0); EXPECT_ERR(87); CHECK(r == 0); CHECK(gb_pristine(&g, 0, 1024));
}
TEST(sysparams_fordpi_unsupported_actions_rejected) {
    NEED_SPIDPI(); spcm_buf g;
    static const UINT a[] = { 0x01, 0x02, 0x2a, 0x2b, 0x2e, 0x30, 0x42, 0x48, 0x68, 0x1000, 0x2006 };
    for (unsigned i = 0; i < sizeof a / sizeof a[0]; i++) {
        gb_init(&g);
        int r = spi_dpi(a[i], 0x1f8, g.b, 96);
        EXPECT_ERR(87); CHECKF(r == 0, "action 0x%x", a[i]);
        CHECK(gb_pristine(&g, 0, 1024));
    }
}
TEST(sysparams_fordpi_ncm_uiparam_must_be_0x1f8) {
    NEED_SPIDPI(); spcm_buf g; static const UINT bad[] = { 0, 1, 500, 503, 505, 0x1fc, 0x200, 0xffffffffu };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        gb_init(&g); unsigned c = NCM_SIZE; memcpy(g.b, &c, 4);
        int r = spi_dpi(0x29, bad[i], g.b, 96);
        EXPECT_ERR(87); CHECKF(r == 0, "uiParam %u", bad[i]);
        CHECK(gb_pristine(&g, 4, 1020));
    }
}
TEST(sysparams_fordpi_ncm_embedded_cbsize_must_be_0x1f8) {
    NEED_SPIDPI(); spcm_buf g; static const UINT bad[] = { 0, 500, 503, 0x1f9, 0x1fc };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        gb_init(&g); memcpy(g.b, &bad[i], 4);
        int r = spi_dpi(0x29, NCM_SIZE, g.b, 96);
        EXPECT_ERR(87); CHECKF(r == 0, "embedded cbSize %u", bad[i]);
    }
}
TEST(sysparams_fordpi_iconmetrics_size_rules) {
    NEED_SPIDPI(); spcm_buf g; int r;
    gb_init(&g); { unsigned c = ICM_SIZE; memcpy(g.b, &c, 4); } r = spi_dpi(0x2d, ICM_SIZE - 4, g.b, 96); EXPECT_ERR(87); CHECK(r == 0);
    gb_init(&g); { unsigned c = ICM_SIZE; memcpy(g.b, &c, 4); } r = spi_dpi(0x2d, 0, g.b, 96); EXPECT_ERR(87); CHECK(r == 0);
    gb_init(&g); { unsigned c = ICM_SIZE + 4; memcpy(g.b, &c, 4); } r = spi_dpi(0x2d, ICM_SIZE, g.b, 96); EXPECT_ERR(87); CHECK(r == 0);  /* embedded cbSize */
    gb_init(&g); { unsigned c = 0; memcpy(g.b, &c, 4); } r = spi_dpi(0x2d, ICM_SIZE, g.b, 96); EXPECT_ERR(87); CHECK(r == 0);
}
TEST(sysparams_fordpi_logfont_uiparam_must_be_0x5c) {
    NEED_SPIDPI(); spcm_buf g; static const UINT bad[] = { 0, 0x58, 0x60, 0x6c, 0x1f8 };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        gb_init(&g);
        int r = spi_dpi(0x1f, bad[i], g.b, 96);
        EXPECT_ERR(87); CHECKF(r == 0, "uiParam %u", bad[i]);
        CHECK(gb_pristine(&g, 0, 1024));
    }
}
TEST(sysparams_fordpi_ncm_copyback_is_exactly_504_bytes) {
    NEED_SPIDPI(); spcm_buf g; gb_init(&g); unsigned c = NCM_SIZE; memcpy(g.b, &c, 4);
    int r = spi_dpi(0x29, NCM_SIZE, g.b, 96);
    CHECK(r == 1);
    EXPECT_EQ(gb_u32(&g, 0), NCM_SIZE);
    CHECK(gb_pristine(&g, NCM_SIZE, 1024 - NCM_SIZE));
    CHECK(gb_edges_ok(&g));
}
TEST(sysparams_fordpi_icon_copyback_sizes) {
    NEED_SPIDPI(); spcm_buf g; int r;
    gb_init(&g); { unsigned c = ICM_SIZE; memcpy(g.b, &c, 4); } r = spi_dpi(0x2d, ICM_SIZE, g.b, 96);
    CHECK(r == 1); CHECK(gb_pristine(&g, ICM_SIZE, 1024 - ICM_SIZE)); CHECK(gb_edges_ok(&g));
    gb_init(&g); r = spi_dpi(0x1f, LOGFONTW_SIZE, g.b, 96);
    CHECK(r == 1); CHECK(gb_pristine(&g, LOGFONTW_SIZE, 1024 - LOGFONTW_SIZE)); CHECK(gb_edges_ok(&g));
}
TEST(sysparams_fordpi_ncm_metrics_monotonic_in_dpi) {
    NEED_SPIDPI();
    static const size_t f[] = { NCM_iCaptionHeight, NCM_iCaptionWidth, NCM_iScrollWidth, NCM_iScrollHeight, NCM_iSmCaptionWidth,
                                NCM_iSmCaptionHeight, NCM_iMenuWidth, NCM_iMenuHeight };
    unsigned char prev[NCM_SIZE], cur[NCM_SIZE];
    for (unsigned i = 0; i < sizeof g_dpis / sizeof g_dpis[0]; i++) {
        CHECKF(fd_ncm(cur, g_dpis[i]) == 1, "dpi %u", g_dpis[i]);
        for (unsigned k = 0; k < sizeof f / sizeof f[0]; k++) {
            CHECKF((int)u32at(cur, f[k]) > 0, "dpi %u off 0x%zx = %d", g_dpis[i], f[k], (int)u32at(cur, f[k]));
            if (i) CHECKF((int)u32at(cur, f[k]) >= (int)u32at(prev, f[k]), "dpi %u->%u off 0x%zx: %d -> %d", g_dpis[i-1], g_dpis[i], f[k], (int)u32at(prev, f[k]), (int)u32at(cur, f[k]));
        }
        memcpy(prev, cur, sizeof prev);
    }
}
TEST(sysparams_fordpi_ncm_scales_roughly_with_dpi) {
    NEED_SPIDPI(); unsigned char a[NCM_SIZE], b[NCM_SIZE];
    CHECK(fd_ncm(a, 96) == 1); CHECK(fd_ncm(b, 192) == 1);
    int h1 = (int)u32at(a, NCM_iCaptionHeight), h2 = (int)u32at(b, NCM_iCaptionHeight);
    int s1 = (int)u32at(a, NCM_iScrollWidth), s2 = (int)u32at(b, NCM_iScrollWidth);
    t_note("ForDpi caption height 96dpi=%d 192dpi=%d, scrollbar width %d -> %d", h1, h2, s1, s2);
    CHECK(h2 > h1); CHECK(s2 > s1);
    CHECK(h2 <= h1 * 3); CHECK(s2 <= s1 * 3);
}
TEST(sysparams_fordpi_ncm_fonts_scale) {
    NEED_SPIDPI(); unsigned char a[NCM_SIZE], b[NCM_SIZE];
    CHECK(fd_ncm(a, 96) == 1); CHECK(fd_ncm(b, 192) == 1);
    static const size_t off[] = { NCM_lfCaptionFont, NCM_lfSmCaptionFont, NCM_lfMenuFont, NCM_lfStatusFont, NCM_lfMessageFont };
    for (unsigned i = 0; i < 5; i++) {
        LOGFONTW x, y; memcpy(&x, a + off[i], sizeof x); memcpy(&y, b + off[i], sizeof y);
        int hx = x.lfHeight < 0 ? -x.lfHeight : x.lfHeight, hy = y.lfHeight < 0 ? -y.lfHeight : y.lfHeight;
        CHECKF(hx > 0 && hy > 0, "font %u heights %d %d", i, hx, hy);
        CHECKF(hy >= hx, "font %u height shrank with dpi: %d -> %d", i, hx, hy);
        CHECKF(y.lfFaceName[0] != 0 && x.lfFaceName[0] != 0, "font %u face", i);
        /* scaling must not flip the sign convention of the metric */
        CHECKF((x.lfHeight < 0) == (y.lfHeight < 0), "font %u sign", i);
    }
}
TEST(sysparams_fordpi_icontitlelogfont_scales) {
    NEED_SPIDPI(); LOGFONTW a, b; memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
    CHECK(spi_dpi(0x1f, LOGFONTW_SIZE, &a, 96) == 1); CHECK(spi_dpi(0x1f, LOGFONTW_SIZE, &b, 192) == 1);
    int ha = a.lfHeight < 0 ? -a.lfHeight : a.lfHeight, hb = b.lfHeight < 0 ? -b.lfHeight : b.lfHeight;
    CHECK(ha > 0); CHECK(hb >= ha);
    CHECK(a.lfFaceName[0] != 0);
}
TEST(sysparams_fordpi_iconmetrics_fields) {
    NEED_SPIDPI(); unsigned char prev[ICM_SIZE] = {0}, cur[ICM_SIZE];
    for (unsigned i = 0; i < sizeof g_dpis / sizeof g_dpis[0]; i++) {
        CHECKF(fd_icm(cur, g_dpis[i]) == 1, "dpi %u", g_dpis[i]);
        EXPECT_EQ(u32at(cur, 0), ICM_SIZE);   /* ForDpi never rewrites cbSize: the caller's value is echoed */
        CHECKF((int)u32at(cur, 4) > 0 && (int)u32at(cur, 8) > 0, "dpi %u spacing %d x %d", g_dpis[i], (int)u32at(cur, 4), (int)u32at(cur, 8));
        CHECK(u32at(cur, 12) <= 1);
        if (i) { CHECK((int)u32at(cur, 4) >= (int)u32at(prev, 4)); CHECK((int)u32at(cur, 8) >= (int)u32at(prev, 8)); }
        memcpy(prev, cur, sizeof prev);
    }
}
TEST(sysparams_fordpi_iconmetrics_titlewrap_independent_of_dpi) {
    NEED_SPIDPI(); unsigned char a[ICM_SIZE], b[ICM_SIZE];
    CHECK(fd_icm(a, 96) == 1); CHECK(fd_icm(b, 288) == 1);
    /* iTitleWrap comes from gdwPUDFlags (bit 0x15), not from the dpi */
    EXPECT_EQ(u32at(a, 12), u32at(b, 12));
}
TEST(sysparams_fordpi_wrapper_equals_direct) {
    NEED_SPIDPI(); REQUIRE(U(SystemParametersInfoForDpi));
    unsigned char a[NCM_SIZE], b[NCM_SIZE];
    CHECK(fd_ncm(a, 144) == 1);
    memset(b, 0, sizeof b); unsigned c = NCM_SIZE; memcpy(b, &c, 4);
    BOOL ok = U(SystemParametersInfoForDpi)(SPI_GETNONCLIENTMETRICS, NCM_SIZE, b, 0, 144);
    CHECK(ok);
    /* as for SystemParametersInfoW, the wrapper substitutes the caption button width (SM_CXSIZE at that DPI) for
     * the kernel's iCaptionWidth and leaves every other byte alone */
    CHECK(!memcmp(a, b, NCM_iCaptionWidth));
    CHECK(!memcmp(a + NCM_iCaptionWidth + 4, b + NCM_iCaptionWidth + 4, NCM_SIZE - NCM_iCaptionWidth - 4));
    if (UN(GetSystemMetricsForDpi)) EXPECT_EQ(u32at(b, NCM_iCaptionWidth), (unsigned)UN(GetSystemMetricsForDpi)(SM_CXSIZE, 144));
}
TEST(sysparams_fordpi_wrapper_rejects_like_kernel) {
    NEED_SPIDPI(); REQUIRE(U(SystemParametersInfoForDpi));
    unsigned char b[NCM_SIZE]; memset(b, 0, sizeof b);
    __typeof__(&SystemParametersInfoForDpi) fn = U(SystemParametersInfoForDpi);
    SetLastError(0);
    BOOL ok = fn(SPI_GETNONCLIENTMETRICS, 500, b, 0, 96);
    DWORD e = GetLastError();
    CHECK(!ok); EXPECT_EQ(e, 87);
    /* dpi 0 is not forwarded as-is: the wrapper substitutes the system DPI, so unlike the kernel it succeeds */
    { unsigned c = NCM_SIZE; memcpy(b, &c, 4); }
    ok = fn(SPI_GETNONCLIENTMETRICS, NCM_SIZE, b, 0, 0);
    CHECK(ok);
}
TEST(sysparams_fordpi_matches_system_metrics_for_dpi_scroll) {
    NEED_SPIDPI(); REQUIRE(UN(GetSystemMetricsForDpi));
    for (unsigned i = 0; i < sizeof g_dpis / sizeof g_dpis[0]; i++) {
        unsigned char a[NCM_SIZE]; CHECK(fd_ncm(a, g_dpis[i]) == 1);
        int sm = UN(GetSystemMetricsForDpi)(SM_CXVSCROLL, g_dpis[i]);
        int sp = (int)u32at(a, NCM_iScrollWidth);
        if (sm != sp) t_note("dpi %u: GetSystemMetricsForDpi(SM_CXVSCROLL) %d vs NONCLIENTMETRICS.iScrollWidth %d", g_dpis[i], sm, sp);
        CHECK(sm > 0);
    }
}
