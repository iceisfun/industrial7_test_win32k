/* dx_sysparams_colors_metrics: SPI SET / SetSysColors / SetDoubleClickTime / SetCaretBlinkTime round trips.
 * Every test reads the current value first, never uses SPIF_UPDATEINIFILE (so nothing reaches the registry), and puts the
 * original back before it ends (plus an atexit backstop for the colors and timing values). Runs only with /destructive.
 *
 * Kernel facts used (analysis, 19041):
 *   SystemParametersInfo: with fWinIni bit0 clear the profile write is skipped and the in-memory
 *     value is stored directly; bit1 (SPIF_SENDCHANGE) only broadcasts when a
 *     profile write happened (a profile write), so SENDCHANGE alone broadcasts nothing.
 *   0x1c SETMENUDROPALIGNMENT stores (uiParam != 0) into the system metric SM_MENUDROPALIGNMENT; 0x1b GETs it.
 *   0x1d/0x1e SETDOUBLECLKWIDTH/HEIGHT store uiParam into the system metrics SM_CXDOUBLECLK/SM_CYDOUBLECLK.
 *   0x0b SETKEYBOARDSPEED stores min(uiParam, 31) in the low 5 bits of gnKeyboardSpeed; 0x0a GETs it.
 *   0x69/0x68 SETWHEELSCROLLLINES store/load the wheel scroll line count.
 *   0x1a SETICONTITLEWRAP sets/clears bit 0x200000 of gdwPUDFlags, which 0x2d GETICONMETRICS returns as iTitleWrap.
 *   _SetDoubleClickTime: 0 -> 500, > 5000 -> 5000, stores the time and derives the double-click distance times from it.
 *   SetCaretBlinkTime: stores the value (rejected when CheckDesktopPolicy says the value is policy-controlled).
 *   SetSysColor: described in spcm_colors.c.
 */
#include "../sysparams_colors_metrics/spcm.h"
#include "../sysparams_colors_metrics/spcm_util.c"

/* ---- atexit backstop ---------------------------------------------------------------------------------------------- */
static struct { int armed; int idx[SPCM_NCOLORS]; COLORREF unm[SPCM_NCOLORS]; int n; UINT dbl, caret; int have_t; } g_save;
static void backstop(void) {
    if (UN(NtUserSetSysColors) && g_save.n) UN(NtUserSetSysColors)(g_save.n, g_save.idx, g_save.unm, 0);
    if (g_save.have_t && UN(NtUserCallOneParam)) {
        UN(NtUserCallOneParam)(g_save.dbl, 0x44);
        UN(NtUserCallOneParam)(g_save.caret, 0x43);
    }
}
static void arm(void) { if (!g_save.armed) { g_save.armed = 1; atexit(backstop); } }

/* ---- generic uiParam-valued setting round trip -------------------------------------------------------------------------- */
static int get_u(UINT act, UINT *v) { *v = 0xCCCCCCCC; return spi(act, 0, v, 0); }
static void rt_uiparam(UINT get, UINT set, UINT (*pick)(UINT)) {
    UINT v0 = 0, v1 = 0, v2 = 0;
    CHECK(get_u(get, &v0) == 1);
    /* same value first: always accepted, never changes anything */
    CHECK(spi(set, v0, NULL, 0) == 1);
    CHECK(get_u(get, &v1) == 1); EXPECT_EQ(v1, v0);
    UINT nv = pick(v0);
    int r = spi(set, nv, NULL, 0);
    CHECKF(r == 1, "set 0x%x(%u)", set, nv);
    int g = get_u(get, &v2);
    CHECK(g == 1);
    if (r == 1) EXPECT_EQ(v2, nv);
    spi(set, v0, NULL, 0);     /* restore */
    CHECK(get_u(get, &v1) == 1); EXPECT_EQ(v1, v0);
}
static UINT pk_flip(UINT v) { return v ? 0 : 1; }
static UINT pk_inc(UINT v) { return v + 1; }
static UINT pk_inc7(UINT v) { return v + 7; }
static UINT pk_delay(UINT v) { return (v + 1) & 3; }
static UINT pk_speed(UINT v) { return v ^ 1; }

TEST(dx_sysparams_beep_roundtrip)            { NEED_SPI(); arm(); rt_uiparam(0x01, 0x02, pk_flip); }
TEST(dx_sysparams_keyboarddelay_roundtrip)   { NEED_SPI(); arm(); rt_uiparam(0x16, 0x17, pk_delay); }
TEST(dx_sysparams_keyboardspeed_roundtrip)   { NEED_SPI(); arm(); rt_uiparam(0x0a, 0x0b, pk_speed); }
TEST(dx_sysparams_keyboardspeed_clamped_to_31) {
    NEED_SPI(); UINT v0, v;
    CHECK(get_u(0x0a, &v0) == 1);
    CHECK(spi(0x0b, 100, NULL, 0) == 1);
    CHECK(get_u(0x0a, &v) == 1); EXPECT_EQ(v, 31);
    CHECK(spi(0x0b, v0, NULL, 0) == 1);
    CHECK(get_u(0x0a, &v) == 1); EXPECT_EQ(v, v0);
}
TEST(dx_sysparams_mousehovertime_roundtrip)  { NEED_SPI(); rt_uiparam(0x66, 0x67, pk_inc7); }
TEST(dx_sysparams_mousehoverwidth_roundtrip) { NEED_SPI(); rt_uiparam(0x62, 0x63, pk_inc); }
TEST(dx_sysparams_mousehoverheight_roundtrip){ NEED_SPI(); rt_uiparam(0x64, 0x65, pk_inc); }
TEST(dx_sysparams_menushowdelay_roundtrip)   { NEED_SPI(); rt_uiparam(0x6a, 0x6b, pk_inc7); }
TEST(dx_sysparams_wheelscrollchars_roundtrip){ NEED_SPI(); rt_uiparam(0x6c, 0x6d, pk_inc); }
TEST(dx_sysparams_snaptodefbutton_roundtrip) { NEED_SPI(); rt_uiparam(0x5f, 0x60, pk_flip); }
TEST(dx_sysparams_dragfullwindows_roundtrip) { NEED_SPI(); rt_uiparam(0x26, 0x25, pk_flip); }

TEST(dx_sysparams_wheelscrolllines_roundtrip_and_page) {
    NEED_SPI(); char *psi = spcm_page(); if (!psi) return;
    UINT v0, v;
    CHECK(get_u(0x68, &v0) == 1);
    CHECK(spi(0x69, 9, NULL, 0) == 1);
    CHECK(get_u(0x68, &v) == 1); EXPECT_EQ(v, 9);
    EXPECT_EQ(pg_u32(psi, PG_Wheel), 9);
    UINT w = 0; if (U(SystemParametersInfoW)) { CHECK(U(SystemParametersInfoW)(SPI_GETWHEELSCROLLLINES, 0, &w, 0)); EXPECT_EQ(w, 9); }
    CHECK(spi(0x69, v0, NULL, 0) == 1);
    EXPECT_EQ(pg_u32(psi, PG_Wheel), v0);
}
TEST(dx_sysparams_menudropalignment_roundtrip_page_and_metric) {
    NEED_SPI(); char *psi = spcm_page(); if (!psi) return;
    UINT v0, v;
    CHECK(get_u(0x1b, &v0) == 1);
    UINT nv = v0 ? 0 : 1;
    CHECK(spi(0x1c, nv, NULL, 0) == 1);
    CHECK(get_u(0x1b, &v) == 1); EXPECT_EQ(v, nv);
    EXPECT_EQ(pg_u32(psi, 0x808), nv);
    EXPECT_EQ(U(GetSystemMetrics)(SM_MENUDROPALIGNMENT), (int)nv);
    CHECK(spi(0x1c, 5, NULL, 0) == 1);   /* any non-zero uiParam is stored as 1 */
    EXPECT_EQ(pg_u32(psi, 0x808), 1);
    CHECK(spi(0x1c, v0, NULL, 0) == 1);
    EXPECT_EQ(pg_u32(psi, 0x808), v0 ? 1u : 0u);
}
TEST(dx_sysparams_sendchange_without_updateinifile_broadcasts_nothing) {
    NEED_SPI();
    if (!spcm_obs_create()) { t_skip("cannot create observer window"); return; }
    UINT v0; CHECK(get_u(0x1b, &v0) == 1);
    spcm_obs_reset(); k_pump(30); spcm_obs_reset();
    CHECK(spi(0x1c, v0, NULL, SPIF_SENDCHANGE) == 1);
    k_pump(200);
    EXPECT_EQ(g_obs.setting, 0);
    spcm_obs_destroy();
}
TEST(dx_sysparams_icontitlewrap_roundtrip_and_iconmetrics) {
    NEED_SPI(); UINT v0, v;
    CHECK(get_u(0x19, &v0) == 1);
    UINT nv = v0 ? 0 : 1;
    CHECK(spi(0x1a, nv, NULL, 0) == 1);
    CHECK(get_u(0x19, &v) == 1); EXPECT_EQ(v != 0, nv != 0);
    unsigned char im[ICM_SIZE]; memset(im, 0, sizeof im); unsigned c = ICM_SIZE; memcpy(im, &c, 4);
    CHECK(spi(0x2d, 0, im, 0) == 1);
    unsigned tw; memcpy(&tw, im + 12, 4); EXPECT_EQ(tw, nv);
    CHECK(spi(0x1a, v0, NULL, 0) == 1);
    CHECK(get_u(0x19, &v) == 1); EXPECT_EQ(v != 0, v0 != 0);
}
TEST(dx_sysparams_caretwidth_roundtrip) {
    NEED_SPI(); UINT v0, v;
    CHECK(get_u(0x2006, &v0) == 1);
    CHECK(spi(0x2007, 0, (PVOID)(ULONG_PTR)(v0 + 1), 0) == 1);
    CHECK(get_u(0x2006, &v) == 1); EXPECT_EQ(v, v0 + 1);
    CHECK(spi(0x2007, 0, (PVOID)(ULONG_PTR)v0, 0) == 1);
    CHECK(get_u(0x2006, &v) == 1); EXPECT_EQ(v, v0);
}
TEST(dx_sysparams_fontsmoothingtype_same_value_and_range) {
    NEED_SPI(); UINT v0, v;
    CHECK(get_u(0x200a, &v0) == 1);
    CHECK(v0 <= 3);
    CHECK(spi(0x200b, 0, (PVOID)(ULONG_PTR)v0, 0) == 1);
    CHECK(get_u(0x200a, &v) == 1); EXPECT_EQ(v, v0);
    CHECK(spi(0x200b, 0, (PVOID)(ULONG_PTR)4, 0) == 0); EXPECT_ERR(87);
    CHECK(get_u(0x200a, &v) == 1); EXPECT_EQ(v, v0);    /* the rejected value changed nothing */
}
TEST(dx_sysparams_fontsmoothing_same_value) {
    NEED_SPI(); UINT v0, v;
    CHECK(get_u(0x4a, &v0) == 1);
    CHECK(spi(0x4b, v0 ? 1 : 0, NULL, 0) == 1);
    CHECK(get_u(0x4a, &v) == 1); EXPECT_EQ(v != 0, v0 != 0);
}

/* ---- BOOL-in-pvParam flag actions: set the value just read ---------------------------------------------------------------- */
static void flag_same(UINT get) {
    UINT v0 = 0, v1 = 0;
    CHECK(get_u(get, &v0) == 1);
    int r = spi(get + 1, 0, (PVOID)(ULONG_PTR)(v0 ? 1 : 0), 0);
    CHECKF(r == 1, "set 0x%x", get + 1);
    CHECK(get_u(get, &v1) == 1);
    EXPECT_EQ(v1 != 0, v0 != 0);
}
#define FLAG_TEST(tag, a) TEST(dx_sysparams_flag_same_##tag) { NEED_SPI(); flag_same(a); }
FLAG_TEST(activewindowtracking_1000, 0x1000)
FLAG_TEST(menuanimation_1002, 0x1002)
FLAG_TEST(comboboxanimation_1004, 0x1004)
FLAG_TEST(listboxsmoothscrolling_1006, 0x1006)
FLAG_TEST(gradientcaptions_1008, 0x1008)
FLAG_TEST(keyboardcues_100a, 0x100a)
FLAG_TEST(activewndtrkzorder_100c, 0x100c)
FLAG_TEST(hottracking_100e, 0x100e)
FLAG_TEST(menufade_1012, 0x1012)
FLAG_TEST(selectionfade_1014, 0x1014)
FLAG_TEST(tooltipanimation_1016, 0x1016)
FLAG_TEST(tooltipfade_1018, 0x1018)
FLAG_TEST(cursorshadow_101a, 0x101a)

/* ---- work area (same rectangle) --------------------------------------------------------------------------------------------- */
TEST(dx_sysparams_workarea_set_same_rect) {
    NEED_SPI(); RECT r0, r1;
    CHECK(spi(0x30, 0, &r0, 0) == 1);
    int r = spi(0x2f, 0, &r0, 0);       /* SPI_SETWORKAREA with the rectangle just read */
    CHECK(r == 1);
    CHECK(spi(0x30, 0, &r1, 0) == 1);
    CHECK(!memcmp(&r0, &r1, sizeof r0));
}

/* ---- double-click / caret timing ---------------------------------------------------------------------------------------------- */
static void arm_time(void) {
    arm();
    if (!g_save.have_t && UN(NtUserGetDoubleClickTime) && UN(NtUserGetCaretBlinkTime)) {
        g_save.dbl = UN(NtUserGetDoubleClickTime)(); g_save.caret = UN(NtUserGetCaretBlinkTime)(); g_save.have_t = 1;
    }
}
TEST(dx_sysparams_doubleclicktime_set_clamps_and_page) {
    REQUIRE(U(SetDoubleClickTime)); REQUIRE(UN(NtUserGetDoubleClickTime)); REQUIRE(UN(NtUserCallOneParam));
    char *psi = spcm_page(); if (!psi) return;
    arm_time();
    UINT t0 = UN(NtUserGetDoubleClickTime)();
    struct { UINT in, want; } v[] = { { 0, 500 }, { 6000, 5000 }, { 5001, 5000 }, { 5000, 5000 }, { 1, 1 }, { 333, 333 }, { 0xffffffffu, 5000 } };
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) {
        BOOL ok = U(SetDoubleClickTime)(v[i].in);
        CHECKF(ok, "in %u", v[i].in);
        EXPECT_EQ(UN(NtUserGetDoubleClickTime)(), v[i].want);
        EXPECT_EQ(U(GetDoubleClickTime)(), v[i].want);
        EXPECT_EQ(pg_u32(psi, PG_Dbl4), v[i].want * 4ul);
        EXPECT_EQ(pg_u32(psi, 0x1370), (v[i].want * 4ul) / 5);
    }
    CHECK(U(SetDoubleClickTime)(t0));
    EXPECT_EQ(UN(NtUserGetDoubleClickTime)(), t0);
}
TEST(dx_sysparams_doubleclicktime_via_callone_param_0x44) {
    REQUIRE(UN(NtUserCallOneParam)); REQUIRE(UN(NtUserGetDoubleClickTime)); arm_time();
    UINT t0 = UN(NtUserGetDoubleClickTime)();
    CHECK(UN(NtUserCallOneParam)(250, 0x44) == 1);
    EXPECT_EQ(UN(NtUserGetDoubleClickTime)(), 250);
    CHECK(UN(NtUserCallOneParam)(t0, 0x44) == 1);
    EXPECT_EQ(UN(NtUserGetDoubleClickTime)(), t0);
}
TEST(dx_sysparams_setdoubleclicktime_spi_0x20_matches_helper) {
    NEED_SPI(); REQUIRE(UN(NtUserGetDoubleClickTime)); arm_time();
    UINT t0 = UN(NtUserGetDoubleClickTime)();
    CHECK(spi(0x20, 410, NULL, 0) == 1);
    EXPECT_EQ(UN(NtUserGetDoubleClickTime)(), 410);
    CHECK(spi(0x20, 0, NULL, 0) == 1);
    EXPECT_EQ(UN(NtUserGetDoubleClickTime)(), 500);
    CHECK(spi(0x20, t0, NULL, 0) == 1);
    EXPECT_EQ(UN(NtUserGetDoubleClickTime)(), t0);
}
TEST(dx_sysparams_caretblinktime_set_and_get) {
    REQUIRE(U(SetCaretBlinkTime)); REQUIRE(UN(NtUserGetCaretBlinkTime)); REQUIRE(UN(NtUserCallOneParam));
    char *psi = spcm_page(); if (!psi) return;
    arm_time();
    UINT c0 = UN(NtUserGetCaretBlinkTime)();
    BOOL ok = U(SetCaretBlinkTime)(777);
    if (!ok) { t_note("SetCaretBlinkTime refused (desktop policy controls the caret blink time)"); return; }
    EXPECT_EQ(UN(NtUserGetCaretBlinkTime)(), 777);
    EXPECT_EQ(U(GetCaretBlinkTime)(), 777);
    EXPECT_EQ(pg_u32(psi, PG_Caret), 777);
    CHECK(UN(NtUserCallOneParam)(c0, 0x43) == 1);
    EXPECT_EQ(UN(NtUserGetCaretBlinkTime)(), c0);
    EXPECT_EQ(pg_u32(psi, PG_Caret), c0);
}
TEST(dx_sysparams_doubleclick_size_metrics_roundtrip) {
    NEED_SPI(); char *psi = spcm_page(); if (!psi) return;
    int w0 = U(GetSystemMetrics)(SM_CXDOUBLECLK), h0 = U(GetSystemMetrics)(SM_CYDOUBLECLK);
    CHECK(spi(0x1d, (UINT)w0 + 3, NULL, 0) == 1);
    CHECK(spi(0x1e, (UINT)h0 + 5, NULL, 0) == 1);
    EXPECT_EQ(pg_sm(psi, SM_CXDOUBLECLK), w0 + 3);
    EXPECT_EQ(pg_sm(psi, SM_CYDOUBLECLK), h0 + 5);
    EXPECT_EQ(U(GetSystemMetrics)(SM_CXDOUBLECLK), w0 + 3);
    EXPECT_EQ(U(GetSystemMetrics)(SM_CYDOUBLECLK), h0 + 5);
    CHECK(spi(0x1d, (UINT)w0, NULL, 0) == 1);
    CHECK(spi(0x1e, (UINT)h0, NULL, 0) == 1);
    EXPECT_EQ(U(GetSystemMetrics)(SM_CXDOUBLECLK), w0);
    EXPECT_EQ(U(GetSystemMetrics)(SM_CYDOUBLECLK), h0);
}

/* ---- system colors: modify, observe the page and the brush, restore ---------------------------------------------------------- */
static void color_roundtrip(int idx, COLORREF nv, unsigned flags, int expect_broadcast) {
    char *psi = spcm_page(); if (!psi) return;
    REQUIRE(U(GetSysColorBrush)); REQUIRE(U(GetObjectW)); NEED_SYSCOL(); arm();
    /* restore the EFFECTIVE color: the unmatched slot is not guaranteed to hold the same value */
    COLORREF sys0 = (COLORREF)pg_u32(psi, PG_argbSys + 4 * (size_t)idx);
    g_save.idx[g_save.n] = idx; g_save.unm[g_save.n] = sys0; g_save.n++;
    HBRUSH hb0 = U(GetSysColorBrush)(idx);
    int obs = spcm_obs_create();
    spcm_obs_reset(); k_pump(30); spcm_obs_reset();
    INT ii = idx; COLORREF c = nv;
    BOOL r = UN(NtUserSetSysColors)(1, &ii, &c, flags);
    k_pump(expect_broadcast ? 400 : 100);
    CHECK(r == 1);
    EXPECT_EQ(pg_u32(psi, PG_argbUnm + 4 * (size_t)idx), nv);          /* SetSysColor: the shared color table entry equals the requested color */
    if (spcm_truecolor()) {
        EXPECT_EQ(pg_u32(psi, PG_argbSys + 4 * (size_t)idx) & 0xffffff, nv & 0xffffff);
        EXPECT_EQ(U(GetSysColor)(idx) & 0xffffff, nv & 0xffffff);
    }
    HBRUSH hb1 = U(GetSysColorBrush)(idx);
    if (hb0) EXPECT_EQ(hb1, hb0);                                        /* the system brush is updated in place */
    LOGBRUSH lb; memset(&lb, 0, sizeof lb);
    if (hb1 && U(GetObjectW)(hb1, sizeof lb, &lb) == sizeof lb && spcm_truecolor()) CHECK((lb.lbColor & 0xffffff) == (nv & 0xffffff));
    if (obs) { if (expect_broadcast) CHECK(g_obs.syscolor >= 1); else EXPECT_EQ(g_obs.syscolor, 0); }
    /* restore */
    c = sys0; UN(NtUserSetSysColors)(1, &ii, &c, flags);
    k_pump(expect_broadcast ? 400 : 100);
    EXPECT_EQ(pg_u32(psi, PG_argbUnm + 4 * (size_t)idx), sys0);
    if (spcm_truecolor()) EXPECT_EQ(pg_u32(psi, PG_argbSys + 4 * (size_t)idx) & 0xffffff, sys0 & 0xffffff);
    if (hb0) EXPECT_EQ(U(GetSysColorBrush)(idx), hb0);
    if (obs) spcm_obs_destroy();
    g_save.n--;
}
TEST(dx_syscolors_appworkspace_silent_change) { color_roundtrip(COLOR_APPWORKSPACE, 0x00123456, 0, 0); }
TEST(dx_syscolors_infobk_silent_change)       { color_roundtrip(COLOR_INFOBK, 0x00654321, 0, 0); }
TEST(dx_syscolors_infotext_silent_change)     { color_roundtrip(COLOR_INFOTEXT, 0x00010203, 0, 0); }
TEST(dx_syscolors_appworkspace_flag7_broadcast) { color_roundtrip(COLOR_APPWORKSPACE, 0x00334455, 7, 1); }
TEST(dx_syscolors_appworkspace_flag1_broadcast) { color_roundtrip(COLOR_APPWORKSPACE, 0x00445566, 1, 1); }

TEST(dx_syscolors_batch_last_duplicate_wins) {
    NEED_SYSCOL(); char *psi = spcm_page(); if (!psi) return; arm();
    COLORREF u12 = (COLORREF)pg_u32(psi, PG_argbSys + 4 * 12), u24 = (COLORREF)pg_u32(psi, PG_argbSys + 4 * 24);
    g_save.idx[0] = 12; g_save.unm[0] = u12; g_save.idx[1] = 24; g_save.unm[1] = u24; g_save.n = 2;
    INT idx[3] = { COLOR_APPWORKSPACE, COLOR_INFOBK, COLOR_APPWORKSPACE };
    COLORREF c[3] = { 0x00111111, 0x00222222, 0x00333333 };
    CHECK(UN(NtUserSetSysColors)(3, idx, c, 0) == 1);
    EXPECT_EQ(pg_u32(psi, PG_argbUnm + 4 * 12), 0x00333333);
    EXPECT_EQ(pg_u32(psi, PG_argbUnm + 4 * 24), 0x00222222);
    INT ri[2] = { 12, 24 }; COLORREF rc[2] = { u12, u24 };
    CHECK(UN(NtUserSetSysColors)(2, ri, rc, 0) == 1);
    g_save.n = 0;
    EXPECT_EQ(pg_u32(psi, PG_argbUnm + 4 * 12), u12);
    EXPECT_EQ(pg_u32(psi, PG_argbUnm + 4 * 24), u24);
}
TEST(dx_syscolors_high_byte_of_colorref_is_kept_in_unmatched_page) {
    NEED_SYSCOL(); char *psi = spcm_page(); if (!psi) return; arm();
    COLORREF u = (COLORREF)pg_u32(psi, PG_argbSys + 4 * 12);
    g_save.idx[0] = 12; g_save.unm[0] = u; g_save.n = 1;
    INT i = 12; COLORREF c = 0x7F0A0B0C;
    CHECK(UN(NtUserSetSysColors)(1, &i, &c, 0) == 1);
 /* SetSysColor stores the requested value verbatim  */
    EXPECT_EQ(pg_u32(psi, PG_argbUnm + 4 * 12), 0x7F0A0B0C);
    c = u; CHECK(UN(NtUserSetSysColors)(1, &i, &c, 0) == 1);
    g_save.n = 0;
    EXPECT_EQ(pg_u32(psi, PG_argbUnm + 4 * 12), u);
}
TEST(dx_syscolors_wrapper_changes_and_restores) {
    REQUIRE(U(SetSysColors)); REQUIRE(U(GetSysColor)); char *psi = spcm_page(); if (!psi) return; arm();
    COLORREF u = (COLORREF)pg_u32(psi, PG_argbSys + 4 * 12);
    g_save.idx[0] = 12; g_save.unm[0] = u; g_save.n = 1;
    INT i = COLOR_APPWORKSPACE; COLORREF c = 0x00778899;
    CHECK(U(SetSysColors)(1, &i, &c));
    if (spcm_truecolor()) EXPECT_EQ(U(GetSysColor)(12) & 0xffffff, 0x778899);
    EXPECT_EQ(pg_u32(psi, PG_argbUnm + 4 * 12), 0x00778899);
    c = u; CHECK(U(SetSysColors)(1, &i, &c));
    g_save.n = 0;
    EXPECT_EQ(pg_u32(psi, PG_argbUnm + 4 * 12), u);
}

/* WM_SYSCOLORCHANGE broadcast checks: moved here from the read-only area because they repaint every window. */
TEST(dx_syscolors_wrapper_broadcasts_syscolorchange) {
    REQUIRE(U(SetSysColors)); REQUIRE(U(GetSysColor));
    if (!spcm_obs_create()) { t_skip("cannot create observer window"); return; }
    spcm_obs_reset(); k_pump(30); spcm_obs_reset();
    INT idx = COLOR_BTNFACE; COLORREF c = U(GetSysColor)(COLOR_BTNFACE);
    COLORREF b4 = c;
    BOOL ok = U(SetSysColors)(1, &idx, &c);   /* identical value; flags 7 -> broadcast + repaint */
    CHECK(ok);
    k_pump(500);
    CHECK(g_obs.syscolor >= 1);       /* documented: SetSysColors sends WM_SYSCOLORCHANGE to all top-level windows */
    EXPECT_EQ(U(GetSysColor)(COLOR_BTNFACE), b4);
    spcm_obs_destroy();
}
TEST(dx_syscolors_wrapper_zero_elements_still_broadcasts) {
    REQUIRE(U(SetSysColors));
    if (!spcm_obs_create()) { t_skip("cannot create observer window"); return; }
    spcm_obs_reset(); k_pump(30); spcm_obs_reset();
    BOOL ok = U(SetSysColors)(0, NULL, NULL);
    CHECK(ok);
    k_pump(500);
    /* SetSysColors: (flags & 1) broadcast block runs regardless of cElements */
    CHECK(g_obs.syscolor >= 1);
    spcm_obs_destroy();
}
TEST(dx_syscolors_direct_flag1_broadcasts) {
    NEED_SYSCOL();
    if (!spcm_obs_create()) { t_skip("cannot create observer window"); return; }
    spcm_obs_reset(); k_pump(30); spcm_obs_reset();
    CHECK(UN(NtUserSetSysColors)(0, NULL, NULL, 1) == 1);
    k_pump(500);
    CHECK(g_obs.syscolor >= 1);
    spcm_obs_destroy();
}
