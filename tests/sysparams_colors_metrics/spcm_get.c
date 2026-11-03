/* SPI GET paths: what the kernel writes into the caller's structure, cross-checked against the process-visible
 * SERVERINFO page (aiSysMet etc.), against GetSystemMetrics, and against the user32 wrapper's answer.
 * Read-only: no SPI SET is issued here (those live in dx_sysparams_colors_metrics). */
#include "spcm.h"

static int ncm_get(unsigned cb, void *out) {   /* direct kernel GET, cbSize pre-seeded the way user32 does */
    memset(out, 0, cb); memcpy(out, &cb, 4);
    return spi(0x29, cb, out, 0);
}
static unsigned u32at(const void *p, size_t off) { unsigned v; memcpy(&v, (const char *)p + off, 4); return v; }

TEST(sysparams_ncm_wrapper_equals_direct_bytes) {
    NEED_SPI(); REQUIRE(U(SystemParametersInfoW));
    NONCLIENTMETRICSW a, b;
    CHECK(ncm_get(NCM_SIZE, &a) == 1);
    memset(&b, 0, sizeof b); b.cbSize = NCM_SIZE;
    BOOL ok = U(SystemParametersInfoW)(SPI_GETNONCLIENTMETRICS, NCM_SIZE, &b, 0);
    CHECK(ok);
    /* user32 passes uiParam = cbSize and the same pointer straight to the kernel, then replaces iCaptionWidth
     * with SM_CXSIZE (the caption button width); every other byte is the kernel's answer. */
    CHECK(sizeof(NONCLIENTMETRICSW) == NCM_SIZE);
    unsigned char *pa = (unsigned char *)&a, *pb = (unsigned char *)&b;
    CHECK(!memcmp(pa, pb, NCM_iCaptionWidth));
    CHECK(!memcmp(pa + NCM_iCaptionWidth + 4, pb + NCM_iCaptionWidth + 4, NCM_SIZE - NCM_iCaptionWidth - 4));
    EXPECT_EQ(u32at(pb, NCM_iCaptionWidth), (unsigned)U(GetSystemMetrics)(SM_CXSIZE));
}
TEST(sysparams_ncm_wrapper_rejects_bad_cbsize_without_calling_kernel) {
    REQUIRE(U(SystemParametersInfoW));
    NONCLIENTMETRICSW n; memset(&n, 0, sizeof n);
    static const unsigned bad[] = { 0, 4, 499, 503, 505, 1000 };
    for (unsigned i = 0; i < 6; i++) {
        n.cbSize = bad[i];
        /* RealSystemParametersInfoW: (cbSize-500) & ~4 != 0 -> return 0 */
        CHECK(!U(SystemParametersInfoW)(SPI_GETNONCLIENTMETRICS, 0, &n, 0));
    }
}
TEST(sysparams_ncm_wrapper_accepts_500) {
    REQUIRE(U(SystemParametersInfoW));
    unsigned char b[504]; memset(b, 0, sizeof b); unsigned cb = NCM_SIZE_OLD; memcpy(b, &cb, 4);
    CHECK(U(SystemParametersInfoW)(SPI_GETNONCLIENTMETRICS, 0, b, 0));
    CHECK(u32at(b, NCM_iCaptionHeight) > 0);
}
TEST(sysparams_ncm_500_and_504_agree_on_common_fields) {
    NEED_SPI(); unsigned char a[NCM_SIZE], b[NCM_SIZE];
    CHECK(ncm_get(NCM_SIZE, a) == 1);
    CHECK(ncm_get(NCM_SIZE_OLD, b) == 1);
    /* both copies run the same GetWindowNCMetrics; only the cbSize word and the 4-byte tail differ */
    CHECK(!memcmp(a + 4, b + 4, NCM_SIZE_OLD - 4));
}
TEST(sysparams_ncm_fields_positive) {
    NEED_SPI(); unsigned char a[NCM_SIZE]; CHECK(ncm_get(NCM_SIZE, a) == 1);
    static const struct { const char *n; size_t off; } f[] = {
        { "iBorderWidth", NCM_iBorderWidth }, { "iScrollWidth", NCM_iScrollWidth }, { "iScrollHeight", NCM_iScrollHeight },
        { "iCaptionWidth", NCM_iCaptionWidth }, { "iCaptionHeight", NCM_iCaptionHeight }, { "iSmCaptionWidth", NCM_iSmCaptionWidth },
        { "iSmCaptionHeight", NCM_iSmCaptionHeight }, { "iMenuWidth", NCM_iMenuWidth }, { "iMenuHeight", NCM_iMenuHeight },
    };
    for (unsigned i = 0; i < sizeof f / sizeof f[0]; i++) CHECKF((int)u32at(a, f[i].off) > 0, "%s = %d", f[i].n, (int)u32at(a, f[i].off));
    CHECK((int)u32at(a, NCM_iPaddedBorderWidth) >= 0);
}
TEST(sysparams_ncm_fonts_are_populated) {
    NEED_SPI(); unsigned char a[NCM_SIZE]; CHECK(ncm_get(NCM_SIZE, a) == 1);
    static const size_t off[] = { NCM_lfCaptionFont, NCM_lfSmCaptionFont, NCM_lfMenuFont, NCM_lfStatusFont, NCM_lfMessageFont };
    for (unsigned i = 0; i < 5; i++) {
        LOGFONTW lf; memcpy(&lf, a + off[i], sizeof lf);
        /* GetObject(hfont, 0x5c, ...) / GetScaledLogFontForDpi fill complete LOGFONTWs */
        CHECKF(lf.lfHeight != 0, "font %u lfHeight", i);
        CHECKF(lf.lfFaceName[0] != 0, "font %u face", i);
        CHECKF(lf.lfWeight >= 100 && lf.lfWeight <= 900, "font %u weight %ld", i, lf.lfWeight);
        int n = -1; for (int k = 0; k < LF_FACESIZE; k++) if (!lf.lfFaceName[k]) { n = k; break; }
        CHECKF(n >= 0, "font %u face is NUL-terminated", i);
    }
}
TEST(sysparams_ncm_caption_font_is_not_oversized_for_caption) {
    NEED_SPI(); unsigned char a[NCM_SIZE]; CHECK(ncm_get(NCM_SIZE, a) == 1);
    LOGFONTW lf; memcpy(&lf, a + NCM_lfCaptionFont, sizeof lf);
    int h = lf.lfHeight < 0 ? -lf.lfHeight : lf.lfHeight;
    CHECK(h > 0);
    /* a caption font taller than its caption bar would be clipped by every non-client painter */
    CHECK(h <= (int)u32at(a, NCM_iCaptionHeight) + 4);
}
TEST(sysparams_ncm_matches_system_metrics_at_96dpi) {
    NEED_SPI(); char *psi = spcm_page(); if (!psi) return;
    unsigned char a[NCM_SIZE]; CHECK(ncm_get(NCM_SIZE, a) == 1);
    int d = (int)spcm_sysdpi(psi);
    static const struct { int sm; size_t off; const char *n; } m[] = {
        { SM_CXVSCROLL, NCM_iScrollWidth, "scroll width" }, { SM_CYHSCROLL, NCM_iScrollHeight, "scroll height" },
        { SM_CXSMSIZE, NCM_iSmCaptionWidth, "sm caption width" }, { SM_CYSMSIZE, NCM_iSmCaptionHeight, "sm caption height" },
        { SM_CXMENUSIZE, NCM_iMenuWidth, "menu width" }, { SM_CYMENUSIZE, NCM_iMenuHeight, "menu height" },
        { SM_CXSIZE, NCM_iCaptionWidth, "caption width" }, { SM_CYSIZE, NCM_iCaptionHeight, "caption height" },
    };
    for (unsigned i = 0; i < sizeof m / sizeof m[0]; i++) {
        int sm = U(GetSystemMetrics)(m[i].sm), sp = (int)u32at(a, m[i].off);
        /* The kernel's caption width is the plain caption-bar metric; SM_CXSIZE is the (wider) caption button, so
         * only the ordering holds. */
        if (m[i].sm == SM_CXSIZE) { CHECKF(sm >= sp && sp > 0, "%s: SM %d SPI %d", m[i].n, sm, sp); continue; }
        if (d == 96) CHECKF(sm == sp, "%s: SM %d SPI %d", m[i].n, sm, sp);
        else if (sm != sp) t_note("system DPI %d: %s SM %d vs SPI %d", d, m[i].n, sm, sp);
    }
}
TEST(sysparams_ncm_system_equals_fordpi_at_system_dpi) {
    NEED_SPI(); NEED_SPIDPI(); char *psi = spcm_page(); if (!psi) return;
    unsigned d = spcm_sysdpi(psi);
    unsigned char a[NCM_SIZE], b[NCM_SIZE];
    CHECK(ncm_get(NCM_SIZE, a) == 1);
    memset(b, 0, sizeof b); unsigned cb = NCM_SIZE; memcpy(b, &cb, 4);
    CHECK(spi_dpi(0x29, NCM_SIZE, b, d) == 1);
    /* both GetWindowNCMetrics and GetWindowNCMetricsForDpi read GetDpiDependentMetric(id, dpi) for these ids */
    static const size_t same[] = { NCM_iCaptionHeight, NCM_iMenuHeight, NCM_iScrollWidth, NCM_iScrollHeight, NCM_iCaptionWidth,
                                   NCM_iSmCaptionWidth, NCM_iSmCaptionHeight, NCM_iMenuWidth };
    for (unsigned i = 0; i < sizeof same / sizeof same[0]; i++)
        CHECKF(u32at(a, same[i]) == u32at(b, same[i]), "offset 0x%zx: system %u fordpi(%u) %u", same[i], u32at(a, same[i]), d, u32at(b, same[i]));
    if (u32at(a, NCM_iBorderWidth) != u32at(b, NCM_iBorderWidth)) t_note("iBorderWidth system %u fordpi %u", u32at(a, NCM_iBorderWidth), u32at(b, NCM_iBorderWidth));
    if (u32at(a, NCM_iPaddedBorderWidth) != u32at(b, NCM_iPaddedBorderWidth)) t_note("iPaddedBorderWidth system %u fordpi %u", u32at(a, NCM_iPaddedBorderWidth), u32at(b, NCM_iPaddedBorderWidth));
    if (memcmp(a + NCM_lfCaptionFont, b + NCM_lfCaptionFont, LOGFONTW_SIZE)) t_note("lfCaptionFont differs between system and ForDpi(%u)", d);
}

/* ---- ICONMETRICS ----------------------------------------------------------------------------------------- */
static int icm_get(unsigned char *p) { memset(p, 0, ICM_SIZE); unsigned c = ICM_SIZE; memcpy(p, &c, 4); return spi(0x2d, 0, p, 0); }
TEST(sysparams_iconmetrics_fields_sane) {
    NEED_SPI(); unsigned char a[ICM_SIZE]; CHECK(icm_get(a) == 1);
    CHECK(u32at(a, 0) == ICM_SIZE);
    CHECK((int)u32at(a, 4) > 0); CHECK((int)u32at(a, 8) > 0);
    CHECK(u32at(a, 12) <= 1);     /* (gdwPUDFlags >> 0x15) & 1 */
    LOGFONTW lf; memcpy(&lf, a + 0x10, sizeof lf);
    CHECK(lf.lfFaceName[0] != 0);
}
TEST(sysparams_iconmetrics_font_equals_icontitlelogfont) {
    NEED_SPI(); unsigned char a[ICM_SIZE]; CHECK(icm_get(a) == 1);
    unsigned char f[LOGFONTW_SIZE]; memset(f, 0, sizeof f);
    CHECK(spi(0x1f, LOGFONTW_SIZE, f, 0) == 1);
    /* worker 0x2d and 0x1f both GetObject(GetDPIMetrics()->hIconFont, 0x5c, ...) */
    CHECK(!memcmp(a + 0x10, f, LOGFONTW_SIZE));
}
TEST(sysparams_iconmetrics_wrapper_equals_direct) {
    NEED_SPI(); REQUIRE(U(SystemParametersInfoW));
    unsigned char a[ICM_SIZE]; CHECK(icm_get(a) == 1);
    ICONMETRICSW im; memset(&im, 0, sizeof im); im.cbSize = sizeof im;
    CHECK(U(SystemParametersInfoW)(SPI_GETICONMETRICS, sizeof im, &im, 0));
    CHECK(sizeof im == ICM_SIZE);
    CHECK(!memcmp(&im, a, ICM_SIZE));
}
TEST(sysparams_iconmetrics_spacing_vs_system_metrics) {
    NEED_SPI(); unsigned char a[ICM_SIZE]; CHECK(icm_get(a) == 1);
    int h = (int)u32at(a, 4), v = (int)u32at(a, 8);
    int sh = U(GetSystemMetrics)(SM_CXICONSPACING), sv = U(GetSystemMetrics)(SM_CYICONSPACING);
    if (h != sh || v != sv) t_note("ICONMETRICS spacing %dx%d vs SM_CX/CYICONSPACING %dx%d", h, v, sh, sv);
    CHECK(sh > 0 && sv > 0);
}
TEST(sysparams_iconmetrics_titlewrap_vs_get_icontitlewrap) {
    NEED_SPI(); unsigned char a[ICM_SIZE]; CHECK(icm_get(a) == 1);
    spcm_buf g; gb_init(&g); CHECK(spi(0x19, 0, g.b, 0) == 1);
    if ((gb_u32(&g, 0) != 0) != (u32at(a, 12) != 0)) t_note("SPI_GETICONTITLEWRAP %u vs ICONMETRICS.iTitleWrap %u", gb_u32(&g, 0), u32at(a, 12));
    CHECK(u32at(a, 12) <= 1);
}

/* ---- MINIMIZEDMETRICS: derived from SERVERINFO exactly ----------------------------------------------------- */
TEST(sysparams_minimizedmetrics_derived_from_serverinfo) {
    NEED_SPI(); char *psi = spcm_page(); if (!psi) return;
    spcm_buf g; gb_init(&g);
    CHECK(spi(0x2b, 0, g.b, 0) == 1);
    /* worker 0x2b: left=0x14, top=aiSysMet[57]-6, right=aiSysMet[47]-aiSysMet[57], bottom=aiSysMet[48]-aiSysMet[58], [1].left=aiSysMet[56] */
    int cxmin = pg_sm(psi, SM_CXMINIMIZED), cymin = pg_sm(psi, SM_CYMINIMIZED);
    int cxsp = pg_sm(psi, SM_CXMINSPACING), cysp = pg_sm(psi, SM_CYMINSPACING);
    EXPECT_EQ(gb_u32(&g, 0), 0x14);
    EXPECT_EQ((int)gb_u32(&g, 4), cxmin - 6);
    EXPECT_EQ((int)gb_u32(&g, 8), cxsp - cxmin);
    EXPECT_EQ((int)gb_u32(&g, 12), cysp - cymin);
    EXPECT_EQ((int)gb_u32(&g, 16), pg_sm(psi, SM_ARRANGE));
}
TEST(sysparams_minimizedmetrics_wrapper_equals_direct) {
    NEED_SPI(); REQUIRE(U(SystemParametersInfoW));
    MINIMIZEDMETRICS mm; memset(&mm, 0, sizeof mm); mm.cbSize = sizeof mm;
    CHECK(U(SystemParametersInfoW)(SPI_GETMINIMIZEDMETRICS, sizeof mm, &mm, 0));
    spcm_buf g; gb_init(&g); CHECK(spi(0x2b, 0, g.b, 0) == 1);
    CHECK(sizeof mm == 0x14);
    CHECK(!memcmp(&mm, g.b, 0x14));
}
TEST(sysparams_minimizedmetrics_wrapper_rejects_cbsize) {
    REQUIRE(U(SystemParametersInfoW));
    MINIMIZEDMETRICS mm; memset(&mm, 0, sizeof mm);
    mm.cbSize = 0;  CHECK(!U(SystemParametersInfoW)(SPI_GETMINIMIZEDMETRICS, 0, &mm, 0));
    mm.cbSize = 16; CHECK(!U(SystemParametersInfoW)(SPI_GETMINIMIZEDMETRICS, 0, &mm, 0));
}
TEST(sysparams_minimizedmetrics_vs_getsystemmetrics) {
    NEED_SPI(); spcm_buf g; gb_init(&g); CHECK(spi(0x2b, 0, g.b, 0) == 1);
    int d = (int)gb_u32(&g, 8) + (int)gb_u32(&g, 4) + 6;
    int want = U(GetSystemMetrics)(SM_CXMINSPACING);
    if (d != want) t_note("MINIMIZEDMETRICS-derived CXMINSPACING %d vs GetSystemMetrics %d", d, want);
    CHECK((int)gb_u32(&g, 16) >= 0);
}

/* ---- work area ---------------------------------------------------------------------------------------------- */
TEST(sysparams_workarea_get_writes_exactly_a_rect) {
    NEED_SPI(); spcm_buf g; gb_init(&g);
    int r = spi(0x30, 0, g.b, 0);
    CHECK(r == 1);
    CHECK(gb_pristine(&g, 16, 1008)); CHECK(gb_edges_ok(&g));
    RECT rc; memcpy(&rc, g.b, sizeof rc);
    CHECK(rc.right > rc.left); CHECK(rc.bottom > rc.top);
}
TEST(sysparams_workarea_inside_primary_monitor_rect) {
    NEED_SPI(); REQUIRE(U(MonitorFromPoint)); REQUIRE(U(GetMonitorInfoW));
    char *psi = spcm_page(); if (!psi) return;
    spcm_buf g; gb_init(&g); CHECK(spi(0x30, 0, g.b, 0) == 1);
    RECT w; memcpy(&w, g.b, sizeof w);
    POINT pt = { 0, 0 };
    HMONITOR hm = U(MonitorFromPoint)(pt, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi; memset(&mi, 0, sizeof mi); mi.cbSize = sizeof mi;
    CHECK(hm != NULL && U(GetMonitorInfoW)(hm, &mi));
    if (spcm_sysdpi(psi) == 96) {
        /* GetMonitorWorkRect(primary monitor) */
        CHECK(w.left == mi.rcWork.left && w.top == mi.rcWork.top && w.right == mi.rcWork.right && w.bottom == mi.rcWork.bottom);
    } else if (memcmp(&w, &mi.rcWork, sizeof w)) t_note("work area %ld,%ld,%ld,%ld vs monitor rcWork (virtualized for this DPI-awareness) %ld,%ld,%ld,%ld",
                w.left, w.top, w.right, w.bottom, mi.rcWork.left, mi.rcWork.top, mi.rcWork.right, mi.rcWork.bottom);
    CHECK(mi.rcWork.left >= mi.rcMonitor.left && mi.rcWork.right <= mi.rcMonitor.right);
}
TEST(sysparams_workarea_wrapper_equals_direct) {
    NEED_SPI(); REQUIRE(U(SystemParametersInfoW));
    RECT a, b; memset(&a, 0, sizeof a); memset(&b, 1, sizeof b);
    CHECK(spi(0x30, 0, &a, 0) == 1);
    CHECK(U(SystemParametersInfoW)(SPI_GETWORKAREA, 0, &b, 0));
    CHECK(!memcmp(&a, &b, sizeof a));
}

/* ---- individual SERVERINFO-backed values ---------------------------------------------------------------------- */
TEST(sysparams_wheelscrolllines_get_equals_page) {
    NEED_SPI(); char *psi = spcm_page(); if (!psi) return;
    spcm_buf g; gb_init(&g); CHECK(spi(0x68, 0, g.b, 0) == 1);
    /* SystemParametersInfo: the value is read straight from the shared server info */
    EXPECT_EQ(gb_u32(&g, 0), pg_u32(psi, PG_Wheel));
}
TEST(sysparams_wheelscrolllines_wrapper_equals_direct) {
    NEED_SPI(); REQUIRE(U(SystemParametersInfoW));
    UINT a = 0xAAAAAAAA, b = 0xBBBBBBBB;
    CHECK(spi(0x68, 0, &a, 0) == 1); CHECK(U(SystemParametersInfoW)(SPI_GETWHEELSCROLLLINES, 0, &b, 0));
    EXPECT_EQ(a, b);
}
TEST(sysparams_menudropalignment_get_equals_page_and_metric) {
    NEED_SPI(); char *psi = spcm_page(); if (!psi) return;
    spcm_buf g; gb_init(&g); CHECK(spi(0x1b, 0, g.b, 0) == 1);
    /* the worker reads the shared metric == aiSysMet[SM_MENUDROPALIGNMENT=40] */
    EXPECT_EQ(gb_u32(&g, 0), pg_u32(psi, 0x808));
    EXPECT_EQ(pg_u32(psi, 0x808), (unsigned long)pg_sm(psi, SM_MENUDROPALIGNMENT));
    EXPECT_EQ(U(GetSystemMetrics)(SM_MENUDROPALIGNMENT), pg_sm(psi, SM_MENUDROPALIGNMENT));
}
TEST(sysparams_doubleclick_metrics_equal_page) {
    char *psi = spcm_page(); if (!psi) return;
    /* SPI_SETDOUBLECLKWIDTH/HEIGHT store the same value as aiSysMet[SM_CXDOUBLECLK=36 / SM_CYDOUBLECLK=37] */
    EXPECT_EQ(U(GetSystemMetrics)(SM_CXDOUBLECLK), pg_sm(psi, SM_CXDOUBLECLK));
    EXPECT_EQ(U(GetSystemMetrics)(SM_CYDOUBLECLK), pg_sm(psi, SM_CYDOUBLECLK));
    EXPECT_EQ(PG_aiSysMet + 4 * SM_CXDOUBLECLK, 0x7f8);
    EXPECT_EQ(PG_aiSysMet + 4 * SM_CYDOUBLECLK, 0x7fc);
}
TEST(sysparams_doubleclicktime_direct_equals_wrapper) {
    REQUIRE(U(GetDoubleClickTime)); REQUIRE(UN(NtUserGetDoubleClickTime));
    UINT a = UN(NtUserGetDoubleClickTime)(), b = U(GetDoubleClickTime)();
    EXPECT_EQ(a, b);
    CHECK(a > 0);
    if (a > 5000) t_note("double-click time %u exceeds the 5000 clamp of _SetDoubleClickTime", a);
}
TEST(sysparams_doubleclicktime_page_shadow) {
    REQUIRE(UN(NtUserGetDoubleClickTime)); char *psi = spcm_page(); if (!psi) return;
    UINT t = UN(NtUserGetDoubleClickTime)();
    unsigned long p = pg_u32(psi, PG_Dbl4);
    /* SetDoubleClickTime keeps a scaled copy (time*4) in the shared server info; a registry-initialised value may differ */
    if (p != t * 4ul) t_note("shared double-click value = %lu, time*4 = %lu", p, t * 4ul);
    CHECK(p > 0);
}
TEST(sysparams_caretblinktime_direct_equals_wrapper_and_page) {
    REQUIRE(U(GetCaretBlinkTime)); REQUIRE(UN(NtUserGetCaretBlinkTime));
    UINT a = UN(NtUserGetCaretBlinkTime)(), b = U(GetCaretBlinkTime)();
    EXPECT_EQ(a, b);
    char *psi = spcm_page(); if (!psi) return;
    EXPECT_EQ(a, pg_u32(psi, PG_Caret));
}
TEST(sysparams_swapbutton_metric_vs_page) {
    char *psi = spcm_page(); if (!psi) return;
    EXPECT_EQ(U(GetSystemMetrics)(SM_SWAPBUTTON), pg_sm(psi, SM_SWAPBUTTON));
    CHECK(pg_sm(psi, SM_SWAPBUTTON) == 0 || pg_sm(psi, SM_SWAPBUTTON) == 1);
}

/* ---- HIGHCONTRAST ----------------------------------------------------------------------------------------------- */
TEST(sysparams_highcontrast_get_direct) {
    NEED_SPI();
    struct { unsigned cbSize; unsigned dwFlags; WCHAR *lpsz; } hc; WCHAR scheme[256]; memset(scheme, 0xAA, sizeof scheme);
    memset(&hc, 0, sizeof hc); hc.cbSize = 16; hc.lpsz = scheme;
    int r = spi(0x42, 16, &hc, 0);
    CHECK(r == 1);
    EXPECT_EQ(hc.cbSize, 16);
    CHECK(hc.lpsz == scheme);
}
TEST(sysparams_highcontrast_wrapper_matches_direct_flags) {
    NEED_SPI(); REQUIRE(U(SystemParametersInfoW));
    WCHAR s1[256], s2[256]; HIGHCONTRASTW a, b; memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
    a.cbSize = b.cbSize = sizeof a; a.lpszDefaultScheme = s1; b.lpszDefaultScheme = s2;
    CHECK(spi(0x42, 0, &a, 0) == 1);
    CHECK(U(SystemParametersInfoW)(SPI_GETHIGHCONTRAST, sizeof b, &b, 0));
    EXPECT_EQ(a.dwFlags, b.dwFlags);
}
TEST(sysparams_highcontrast_wrapper_rejects_bad_cbsize) {
    REQUIRE(U(SystemParametersInfoW));
    WCHAR s[256]; HIGHCONTRASTW h; memset(&h, 0, sizeof h); h.lpszDefaultScheme = s;
    h.cbSize = 8; CHECK(!U(SystemParametersInfoW)(SPI_GETHIGHCONTRAST, 0, &h, 0));
    h.cbSize = 24; CHECK(!U(SystemParametersInfoW)(SPI_GETHIGHCONTRAST, 0, &h, 0));
}

/* ---- accessibility GETs: flags and sizes ------------------------------------------------------------------------- */
TEST(sysparams_filterkeys_get_wrapper_equals_direct) {
    NEED_SPI(); REQUIRE(U(SystemParametersInfoW));
    FILTERKEYS a, b; memset(&a, 0, sizeof a); memset(&b, 0, sizeof b); a.cbSize = b.cbSize = sizeof a;
    CHECK(spi(0x32, sizeof a, &a, 0) == 1);
    CHECK(U(SystemParametersInfoW)(SPI_GETFILTERKEYS, sizeof b, &b, 0));
    CHECK(!memcmp(&a, &b, sizeof a));
}
TEST(sysparams_stickykeys_get_wrapper_equals_direct_modulo_latch) {
    NEED_SPI(); REQUIRE(U(SystemParametersInfoW));
    STICKYKEYS a, b; memset(&a, 0, sizeof a); memset(&b, 0, sizeof b); a.cbSize = b.cbSize = sizeof a;
    CHECK(spi(0x3a, sizeof a, &a, 0) == 1);
    CHECK(U(SystemParametersInfoW)(SPI_GETSTICKYKEYS, sizeof b, &b, 0));
    /* SystemParametersInfo ORs the live latch/lock bits (gLatchBits<<24, gLockBits<<16) into dwFlags; compare the stable low word */
    EXPECT_EQ(a.dwFlags & 0xffff, b.dwFlags & 0xffff);
}
TEST(sysparams_accesstimeout_get_wrapper_equals_direct) {
    NEED_SPI(); REQUIRE(U(SystemParametersInfoW));
    ACCESSTIMEOUT a, b; memset(&a, 0, sizeof a); memset(&b, 0, sizeof b); a.cbSize = b.cbSize = sizeof a;
    CHECK(spi(0x3c, sizeof a, &a, 0) == 1);
    CHECK(U(SystemParametersInfoW)(SPI_GETACCESSTIMEOUT, sizeof b, &b, 0));
    CHECK(!memcmp(&a, &b, sizeof a));
}
TEST(sysparams_togglekeys_get_wrapper_equals_direct) {
    NEED_SPI(); REQUIRE(U(SystemParametersInfoW));
    TOGGLEKEYS a, b; memset(&a, 0, sizeof a); memset(&b, 0, sizeof b); a.cbSize = b.cbSize = sizeof a;
    CHECK(spi(0x34, sizeof a, &a, 0) == 1);
    CHECK(U(SystemParametersInfoW)(SPI_GETTOGGLEKEYS, sizeof b, &b, 0));
    CHECK(!memcmp(&a, &b, sizeof a));
}
TEST(sysparams_mousekeys_get_wrapper_equals_direct) {
    NEED_SPI(); REQUIRE(U(SystemParametersInfoW));
    MOUSEKEYS a, b; memset(&a, 0, sizeof a); memset(&b, 0, sizeof b); a.cbSize = b.cbSize = sizeof a;
    CHECK(spi(0x36, sizeof a, &a, 0) == 1);
    CHECK(U(SystemParametersInfoW)(SPI_GETMOUSEKEYS, sizeof b, &b, 0));
    CHECK(!memcmp(&a, &b, sizeof a));
}

/* ---- wrapper vs direct parity for plain DWORD GETs ------------------------------------------------------------------ */
TEST(sysparams_get4_wrapper_parity_table) {
    NEED_SPI(); REQUIRE(U(SystemParametersInfoW));
    static const UINT a[] = { SPI_GETBEEP, SPI_GETKEYBOARDDELAY, SPI_GETKEYBOARDSPEED, SPI_GETMOUSEHOVERTIME, SPI_GETMOUSEHOVERWIDTH,
        SPI_GETMOUSEHOVERHEIGHT, SPI_GETWHEELSCROLLLINES, SPI_GETWHEELSCROLLCHARS, SPI_GETMENUSHOWDELAY, SPI_GETICONTITLEWRAP,
        SPI_GETMENUDROPALIGNMENT, SPI_GETDRAGFULLWINDOWS, SPI_GETFONTSMOOTHING, SPI_GETSNAPTODEFBUTTON, SPI_GETGRIDGRANULARITY,
        SPI_GETKEYBOARDPREF, SPI_GETSCREENREADER, SPI_GETACTIVEWINDOWTRACKING, SPI_GETMENUANIMATION, SPI_GETCARETWIDTH,
        SPI_GETFOCUSBORDERWIDTH, SPI_GETFOCUSBORDERHEIGHT, SPI_GETFONTSMOOTHINGTYPE, SPI_GETFONTSMOOTHINGCONTRAST,
        SPI_GETFONTSMOOTHINGORIENTATION, SPI_GETFOREGROUNDLOCKTIMEOUT, SPI_GETMOUSECLICKLOCKTIME, SPI_GETACTIVEWNDTRKTIMEOUT };
    for (unsigned i = 0; i < sizeof a / sizeof a[0]; i++) {
        UINT x = 0xAAAAAAAA, y = 0xBBBBBBBB;
        int r1 = spi(a[i], 0, &x, 0); BOOL r2 = U(SystemParametersInfoW)(a[i], 0, &y, 0);
        CHECKF((r1 != 0) == (r2 != 0), "action 0x%x: direct %d wrapper %d", a[i], r1, r2);
        if (r1 && r2) CHECKF(x == y, "action 0x%x: direct %u wrapper %u", a[i], x, y);
    }
}
TEST(sysparams_mouse_get_three_ints) {
    NEED_SPI(); spcm_buf g; gb_init(&g);
    int r = spi(0x03, 0, g.b, 0);   /* SPI_GETMOUSE: int[3] (threshold1, threshold2, acceleration) copied 0xc bytes */
    CHECK(r == 1);
    CHECK(gb_pristine(&g, 12, 1012));
    CHECK(gb_edges_ok(&g));
    CHECK(gb_u32(&g, 8) <= 2);
}
TEST(sysparams_mousetrails_get) {
    NEED_SPI(); spcm_buf g; gb_init(&g);
    int r = spi(0x5e, 0, g.b, 0);
    CHECK(r == 1);
    CHECK(gb_pristine(&g, 4, 1020));
}
TEST(sysparams_uieffects_get_is_bool) {
    NEED_SPI(); spcm_buf g; gb_init(&g);
    CHECK(spi(0x103e, 0, g.b, 0) == 1);
    CHECK(gb_u32(&g, 0) <= 1);
}
