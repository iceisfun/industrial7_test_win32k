/* LastInput tick, SPI values that live on the SERVERINFO page, screen DC versus metrics, class/atom
 * tables, and the win32u Nt* stubs that serve the same data directly. */
#include "si.h"

/* ================= GetLastInputInfo / dwLastRITEventTickCount ================= */
static int si_tick_diff(DWORD a, DWORD b) { return (int)(a - b); } /* wrap-safe a - b */

TEST(user_serverinfo_shared_lastinput_equals_page_tick) {
    char *psi = si_get(); if (!psi) return;
    REQUIRE(U(GetLastInputInfo));
    for (int i = 0; i < 20; i++) {
        unsigned long pre = si_u32(psi, SI_LastRITTick);
        LASTINPUTINFO li = { sizeof li, 0xcccccccc };
        CHECK(U(GetLastInputInfo)(&li));
        unsigned long post = si_u32(psi, SI_LastRITTick);
        /* GetLastInputInfo copies the kernel's last-input tick; the tick only moves forward, so the copy lies between the two page reads. */
        CHECKF(si_tick_diff(li.dwTime, pre) >= 0 && si_tick_diff(post, li.dwTime) >= 0, "pre=%lu got=%lu post=%lu", pre, (unsigned long)li.dwTime, post);
    }
}
TEST(user_serverinfo_shared_lastinput_not_in_future) {
    char *psi = si_get(); if (!psi) return;
    REQUIRE(U(GetLastInputInfo));
    LASTINPUTINFO li = { sizeof li, 0 };
    REQUIRE(U(GetLastInputInfo)(&li));
    DWORD now = U(GetTickCount)();
    CHECKF(si_tick_diff(now, li.dwTime) >= 0, "now=%lu last=%lu", (unsigned long)now, (unsigned long)li.dwTime);
}
TEST(user_serverinfo_shared_lastinput_idle_time_is_bounded) {
    char *psi = si_get(); if (!psi) return;
    REQUIRE(U(GetLastInputInfo));
    LASTINPUTINFO li = { sizeof li, 0 };
    REQUIRE(U(GetLastInputInfo)(&li));
    DWORD idle = U(GetTickCount)() - li.dwTime;
    t_info("idle for %lu ms (dwTime=%lu)", (unsigned long)idle, (unsigned long)li.dwTime);
    CHECK(idle < 0x80000000u); /* within the 24.8 day wrap window */
}
TEST(user_serverinfo_shared_lastinput_monotonic_over_time) {
    char *psi = si_get(); if (!psi) return;
    unsigned long a = si_u32(psi, SI_LastRITTick);
    k_pump(100);
    unsigned long b = si_u32(psi, SI_LastRITTick);
    CHECKF(si_tick_diff(b, a) >= 0, "went backwards: %lu -> %lu", a, b);
}
static void si_lastinput_bad_cbsize(DWORD cb) {
    REQUIRE(U(GetLastInputInfo));
    if (t_is_wine()) { t_skip("Wine"); return; }
    U(GetSystemMetrics)(0);
    LASTINPUTINFO li; memset(&li, 0x5a, sizeof li);
    li.cbSize = cb;
    DWORD sentinel = li.dwTime;
    SET_ERR(0);
    /* GetLastInputInfo: cbSize != 8 -> ERROR_INVALID_PARAMETER, FALSE, buffer untouched */
    BOOL r = U(GetLastInputInfo)(&li);
    CHECK(!r);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    EXPECT_EQ(li.dwTime, sentinel);
}
#define LIBAD(n) TEST(user_serverinfo_shared_lastinput_cbsize_##n) { si_lastinput_bad_cbsize(n); }
LIBAD(0) LIBAD(1) LIBAD(4) LIBAD(7) LIBAD(9) LIBAD(12) LIBAD(16) LIBAD(0x100) LIBAD(0xffffffffu)

TEST(user_serverinfo_shared_lastinput_success_keeps_last_error) {
    char *psi = si_get(); if (!psi) return;
    LASTINPUTINFO li = { sizeof li, 0 };
    SET_ERR(0x4242);
    CHECK(U(GetLastInputInfo)(&li));
    EXPECT_ERR(0x4242);
}
TEST(user_serverinfo_shared_lastinput_struct_size_is_8) {
    CHECK(sizeof(LASTINPUTINFO) == 8);
}
UDECL(LONG_PTR, NtCompositorUpdateLastInputTime, (void));

/* ================= SPI values that are also on the page ================= */
static void si_spi_bool_vs_metric(UINT spi, int sm, size_t page_off) {
    char *psi = si_get(); if (!psi) return;
    REQUIRE(U(SystemParametersInfoW));
    BOOL v = 0xcccccccc;
    CHECK(U(SystemParametersInfoW)(spi, 0, &v, 0));
    int m = U(GetSystemMetrics)(sm);
    long p = si_i32(psi, page_off);
    CHECKF(m == p, "GetSystemMetrics(%d)=%d page=%ld", sm, m, p);
    CHECKF((v != 0) == (m != 0), "SPI 0x%x -> %d, metric %d", spi, v, m);
}
TEST(user_serverinfo_shared_spi_menudropalignment_matches_metric) {
    /* SPI GET (0x1b) returns the SM_MENUDROPALIGNMENT=40 metric. */
    si_spi_bool_vs_metric(SPI_GETMENUDROPALIGNMENT, SM_MENUDROPALIGNMENT, SI_aiSysMet + 40 * 4);
}
TEST(user_serverinfo_shared_spi_showsounds_matches_metric) {
    si_spi_bool_vs_metric(SPI_GETSHOWSOUNDS, SM_SHOWSOUNDS, SI_aiSysMet + 70 * 4);
}
static void si_spi_focus_border(UINT spi, int sm, size_t off) {
    char *psi = si_get(); if (!psi) return;
    UINT v = 0xcccccccc;
    CHECK(U(SystemParametersInfoW)(spi, 0, &v, 0));
    /* SPI_SETFOCUSBORDERWIDTH/HEIGHT mirror their value into the metrics table (SM 83/84); DrawFocusRect reads them. */
    EXPECT_EQ(si_i32(psi, off), U(GetSystemMetrics)(sm));
    CHECKF(v >= 1 && v < 100, "focus border = %u", v);
    if (v != (UINT)U(GetSystemMetrics)(sm)) t_note("SPI focus border %u != metric %d", v, U(GetSystemMetrics)(sm));
}
TEST(user_serverinfo_shared_spi_focusborder_width_matches_metric) { si_spi_focus_border(SPI_GETFOCUSBORDERWIDTH, SM_CXFOCUSBORDER, 0x8b4); }
TEST(user_serverinfo_shared_spi_focusborder_height_matches_metric) { si_spi_focus_border(SPI_GETFOCUSBORDERHEIGHT, SM_CYFOCUSBORDER, 0x8b8); }

TEST(user_serverinfo_shared_spi_minimized_metrics_match_page) {
    char *psi = si_get(); if (!psi) return;
    MINIMIZEDMETRICS mm = { sizeof mm };
    REQUIRE(U(SystemParametersInfoW)(SPI_GETMINIMIZEDMETRICS, sizeof mm, &mm, 0));
    /* SPI case 0x2b: iWidth = aiSysMet[57]-6, iHorzGap = [47]-[57], iVertGap = [48]-[58], iArrange = [56] */
    long cxmin = si_i32(psi, 0x84c), cymin = si_i32(psi, 0x850), cxsp = si_i32(psi, 0x824), cysp = si_i32(psi, 0x828), arr = si_i32(psi, 0x848);
    EXPECT_EQ(mm.cbSize, 0x14);
    EXPECT_EQ(mm.iWidth, cxmin - 6);
    EXPECT_EQ(mm.iHorzGap, cxsp - cxmin);
    EXPECT_EQ(mm.iVertGap, cysp - cymin);
    EXPECT_EQ(mm.iArrange, arr);
    /* and the same datums are the public metrics */
    EXPECT_EQ(cxmin, U(GetSystemMetrics)(SM_CXMINIMIZED));
    EXPECT_EQ(cymin, U(GetSystemMetrics)(SM_CYMINIMIZED));
    EXPECT_EQ(cxsp, U(GetSystemMetrics)(SM_CXMINSPACING));
    EXPECT_EQ(cysp, U(GetSystemMetrics)(SM_CYMINSPACING));
    EXPECT_EQ(arr, U(GetSystemMetrics)(SM_ARRANGE));
}
TEST(user_serverinfo_shared_spi_minimized_metrics_direct_stub_matches) {
    char *psi = si_get(); if (!psi) return;
    typedef LONG_PTR (WINAPI *spi_t)(UINT, UINT, PVOID, UINT);
    spi_t nt = (spi_t)t_proc_in("win32u.dll", "NtUserSystemParametersInfo"); REQUIRE(nt);
    MINIMIZEDMETRICS a = { sizeof a }, b = { sizeof b };
    REQUIRE(U(SystemParametersInfoW)(SPI_GETMINIMIZEDMETRICS, sizeof a, &a, 0));
    LONG_PTR r = nt(SPI_GETMINIMIZEDMETRICS, sizeof b, &b, 0);
    CHECK((int)r != 0);
    CHECK(memcmp(&a, &b, sizeof a) == 0);
}
TEST(user_serverinfo_shared_spi_minimized_metrics_wrapper_validation) {
    REQUIRE(U(SystemParametersInfoW));
    if (t_is_wine()) { t_skip("Wine"); return; }
    U(GetSystemMetrics)(0);
    MINIMIZEDMETRICS mm; memset(&mm, 0x77, sizeof mm);
    /* user32!RealSystemParametersInfoW rejects NULL and a cbSize other than 0x14 for action 0x2b/0x2c before any syscall */
    CHECK(!U(SystemParametersInfoW)(SPI_GETMINIMIZEDMETRICS, sizeof mm, NULL, 0));
    mm.cbSize = 8;
    CHECK(!U(SystemParametersInfoW)(SPI_GETMINIMIZEDMETRICS, 8, &mm, 0));
    EXPECT_EQ(mm.iWidth, 0x77777777);
    mm.cbSize = 0x18;
    CHECK(!U(SystemParametersInfoW)(SPI_GETMINIMIZEDMETRICS, 0x18, &mm, 0));
    EXPECT_EQ(mm.iWidth, 0x77777777);
}
TEST(user_serverinfo_shared_spi_iconmetrics_wrapper_validation) {
    REQUIRE(U(SystemParametersInfoW));
    if (t_is_wine()) { t_skip("Wine"); return; }
    U(GetSystemMetrics)(0);
    ICONMETRICSW im; memset(&im, 0x33, sizeof im);
    im.cbSize = 0x20;
    /* actions 0x2d/0x2e require *(int*)pvParam == 0x6c (sizeof(ICONMETRICSW)) */
    CHECK(!U(SystemParametersInfoW)(SPI_GETICONMETRICS, sizeof im, &im, 0));
    EXPECT_EQ(im.iHorzSpacing, 0x33333333);
    im.cbSize = sizeof im;
    EXPECT_EQ(sizeof im, 0x6c);
    CHECK(U(SystemParametersInfoW)(SPI_GETICONMETRICS, sizeof im, &im, 0));
    CHECK(im.iHorzSpacing > 0 && im.iVertSpacing > 0);
}
TEST(user_serverinfo_shared_spi_iconmetrics_spacing_matches_metrics) {
    char *psi = si_get(); if (!psi) return;
    ICONMETRICSW im; memset(&im, 0, sizeof im); im.cbSize = sizeof im;
    REQUIRE(U(SystemParametersInfoW)(SPI_GETICONMETRICS, sizeof im, &im, 0));
    /* SM_CXICONSPACING/SM_CYICONSPACING are what ICONMETRICS reports (page indexes 38/39) */
    t_note("ICONMETRICS spacing %dx%d, SM spacing %dx%d", im.iHorzSpacing, im.iVertSpacing, U(GetSystemMetrics)(SM_CXICONSPACING), U(GetSystemMetrics)(SM_CYICONSPACING));
    CHECK(im.iHorzSpacing > 0 && im.iVertSpacing > 0);
}

TEST(user_serverinfo_shared_spi_workarea_matches_primary_monitor) {
    char *psi = si_get(); if (!psi) return;
    REQUIRE(U(MonitorFromPoint)); REQUIRE(U(GetMonitorInfoW));
    RECT wa = {0};
    REQUIRE(U(SystemParametersInfoW)(SPI_GETWORKAREA, 0, &wa, 0));
    POINT pt = {0, 0};
    HMONITOR m = U(MonitorFromPoint)(pt, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = { sizeof mi };
    REQUIRE(U(GetMonitorInfoW)(m, &mi));
    /* SPI_GETWORKAREA is documented as the work area of the primary display monitor */
    CHECK(wa.left == mi.rcWork.left && wa.top == mi.rcWork.top && wa.right == mi.rcWork.right && wa.bottom == mi.rcWork.bottom);
    CHECK(wa.right > wa.left && wa.bottom > wa.top);
    char *obj = si_monitor_obj(m);
    RECT raw;
    if (obj && k_try_read(obj + SI_MON_rcWork, &raw, sizeof raw) && si_u16(psi, SI_SystemDpi) == 96)
        CHECK(raw.left == wa.left && raw.top == wa.top && raw.right == wa.right && raw.bottom == wa.bottom);
}
TEST(user_serverinfo_shared_spi_workarea_inside_screen) {
    char *psi = si_get(); if (!psi) return;
    RECT wa = {0};
    REQUIRE(U(SystemParametersInfoW)(SPI_GETWORKAREA, 0, &wa, 0));
    int cx = U(GetSystemMetrics)(SM_CXSCREEN), cy = U(GetSystemMetrics)(SM_CYSCREEN);
    CHECK(wa.right - wa.left <= cx && wa.bottom - wa.top <= cy);
    CHECK(wa.left >= 0 && wa.top >= 0);
}
TEST(user_serverinfo_shared_spi_cxfullscreen_is_workarea_width) {
    char *psi = si_get(); if (!psi) return;
    RECT wa = {0};
    REQUIRE(U(SystemParametersInfoW)(SPI_GETWORKAREA, 0, &wa, 0));
    if (si_u16(psi, SI_SystemDpi) != 96) { t_skip("system DPI != 96"); return; }
    EXPECT_EQ(U(GetSystemMetrics)(SM_CXFULLSCREEN), wa.right - wa.left);
}

TEST(user_serverinfo_shared_spi_nonclientmetrics_scrollbar_matches_metrics) {
    char *psi = si_get(); if (!psi) return;
    NONCLIENTMETRICSW nc; memset(&nc, 0, sizeof nc); nc.cbSize = sizeof nc;
    REQUIRE(U(SystemParametersInfoW)(SPI_GETNONCLIENTMETRICS, sizeof nc, &nc, 0));
    t_note("NONCLIENTMETRICS scroll %dx%d caption %dx%d menu %dx%d border %d padded %d; metrics %d %d %d %d %d %d",
           nc.iScrollWidth, nc.iScrollHeight, nc.iCaptionWidth, nc.iCaptionHeight, nc.iMenuWidth, nc.iMenuHeight, nc.iBorderWidth, nc.iPaddedBorderWidth,
           U(GetSystemMetrics)(SM_CXVSCROLL), U(GetSystemMetrics)(SM_CYHSCROLL), U(GetSystemMetrics)(SM_CXSIZE), U(GetSystemMetrics)(SM_CYSIZE),
           U(GetSystemMetrics)(SM_CXMENUSIZE), U(GetSystemMetrics)(SM_CYMENUSIZE));
    CHECK(nc.cbSize == sizeof nc);
    CHECK(nc.iScrollWidth > 0 && nc.iScrollHeight > 0 && nc.iCaptionWidth > 0 && nc.iCaptionHeight > 0 && nc.iMenuHeight > 0);
    CHECK(nc.lfCaptionFont.lfHeight != 0 && nc.lfMenuFont.lfHeight != 0 && nc.lfMessageFont.lfHeight != 0);
    CHECK(nc.lfCaptionFont.lfFaceName[0] != 0);
}
TEST(user_serverinfo_shared_spi_nonclientmetrics_fields_vs_metrics_dpi96) {
    char *psi = si_get(); if (!psi) return;
    if (si_u16(psi, SI_SystemDpi) != 96) { t_skip("system DPI != 96"); return; }
    NONCLIENTMETRICSW nc; memset(&nc, 0, sizeof nc); nc.cbSize = sizeof nc;
    REQUIRE(U(SystemParametersInfoW)(SPI_GETNONCLIENTMETRICS, sizeof nc, &nc, 0));
    /* NONCLIENTMETRICS documents these as the same sizes GetSystemMetrics reports */
    CHECK(nc.iScrollWidth == U(GetSystemMetrics)(SM_CXVSCROLL));
    CHECK(nc.iScrollHeight == U(GetSystemMetrics)(SM_CYHSCROLL));
    CHECK(nc.iCaptionWidth == U(GetSystemMetrics)(SM_CXSIZE));
    CHECK(nc.iSmCaptionWidth == U(GetSystemMetrics)(SM_CXSMSIZE));
    CHECK(nc.iSmCaptionHeight == U(GetSystemMetrics)(SM_CYSMSIZE));
    CHECK(nc.iMenuWidth == U(GetSystemMetrics)(SM_CXMENUSIZE));
    CHECK(nc.iMenuHeight == U(GetSystemMetrics)(SM_CYMENUSIZE));
}
/* NtUserSystemParametersInfo(SPI_GETNONCLIENTMETRICS) takes the structure size in uiParam; anything but 500/504 fails before touching the buffer. */
static void si_ncm_bad_size(UINT sz) {
    typedef LONG_PTR (WINAPI *spi_t)(UINT, UINT, PVOID, UINT);
    spi_t nt = (spi_t)t_proc_in("win32u.dll", "NtUserSystemParametersInfo");
    REQUIRE(nt);
    if (t_is_wine()) { t_skip("Wine"); return; }
    U(GetSystemMetrics)(0);
    NONCLIENTMETRICSW nc; memset(&nc, 0x66, sizeof nc);
    SET_ERR(0);
    LONG_PTR r = nt(SPI_GETNONCLIENTMETRICS, sz, &nc, 0);
    CHECK((int)r == 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    EXPECT_EQ(nc.iBorderWidth, 0x66666666);
}
#define NCMBAD(n) TEST(user_serverinfo_shared_ntuser_spi_getncm_size_##n) { si_ncm_bad_size(n); }
NCMBAD(0) NCMBAD(1) NCMBAD(100) NCMBAD(499) NCMBAD(501) NCMBAD(503) NCMBAD(505) NCMBAD(508) NCMBAD(1000)

static void si_ncm_good_size(UINT sz) {
    typedef LONG_PTR (WINAPI *spi_t)(UINT, UINT, PVOID, UINT);
    spi_t nt = (spi_t)t_proc_in("win32u.dll", "NtUserSystemParametersInfo");
    REQUIRE(nt);
    if (t_is_wine()) { t_skip("Wine"); return; }
    U(GetSystemMetrics)(0);
    NONCLIENTMETRICSW nc, ref;
    memset(&nc, 0, sizeof nc); nc.cbSize = sz;
    memset(&ref, 0, sizeof ref); ref.cbSize = sizeof ref;
    LONG_PTR r = nt(SPI_GETNONCLIENTMETRICS, sz, &nc, 0);
    CHECK((int)r != 0);
    REQUIRE(U(SystemParametersInfoW)(SPI_GETNONCLIENTMETRICS, sizeof ref, &ref, 0));
    /* the size-500 (Vista) layout is a prefix of the 504-byte layout */
    CHECK(nc.iScrollWidth == ref.iScrollWidth && nc.iCaptionHeight == ref.iCaptionHeight && nc.iMenuHeight == ref.iMenuHeight);
    CHECK(nc.lfCaptionFont.lfHeight == ref.lfCaptionFont.lfHeight);
}
TEST(user_serverinfo_shared_ntuser_spi_getncm_size_500) { si_ncm_good_size(500); }
TEST(user_serverinfo_shared_ntuser_spi_getncm_size_504) { si_ncm_good_size(504); }

TEST(user_serverinfo_shared_ntuser_spi_setncm_bad_size_rejected) {
    typedef LONG_PTR (WINAPI *spi_t)(UINT, UINT, PVOID, UINT);
    spi_t nt = (spi_t)t_proc_in("win32u.dll", "NtUserSystemParametersInfo"); REQUIRE(nt);
    if (t_is_wine()) { t_skip("Wine"); return; }
    U(GetSystemMetrics)(0);
    /* SPI_SETNONCLIENTMETRICS goes through the same size check (case 0x2a) and fails with 0x57 before changing anything. */
    NONCLIENTMETRICSW nc; memset(&nc, 0, sizeof nc);
    NONCLIENTMETRICSW before; memset(&before, 0, sizeof before); before.cbSize = sizeof before;
    REQUIRE(U(SystemParametersInfoW)(SPI_GETNONCLIENTMETRICS, sizeof before, &before, 0));
    SET_ERR(0);
    LONG_PTR r = nt(SPI_SETNONCLIENTMETRICS, 100, &nc, 0);
    CHECK((int)r == 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    NONCLIENTMETRICSW after; memset(&after, 0, sizeof after); after.cbSize = sizeof after;
    REQUIRE(U(SystemParametersInfoW)(SPI_GETNONCLIENTMETRICS, sizeof after, &after, 0));
    CHECK(memcmp(&before, &after, sizeof before) == 0);
}

/* ---------- direct NtUserSystemParametersInfo vs SystemParametersInfoW for GET actions ---------- */
static void si_spi_direct_vs_api(UINT spi, UINT param, size_t bytes, int pass_size_in_first_dword) {
    typedef LONG_PTR (WINAPI *spi_t)(UINT, UINT, PVOID, UINT);
    spi_t nt = (spi_t)t_proc_in("win32u.dll", "NtUserSystemParametersInfo");
    REQUIRE(nt); REQUIRE(U(SystemParametersInfoW));
    U(GetSystemMetrics)(0);
    unsigned char a[512], b[512];
    memset(a, 0xa5, sizeof a); memset(b, 0xa5, sizeof b);
    if (pass_size_in_first_dword) { *(DWORD *)a = (DWORD)bytes; *(DWORD *)b = (DWORD)bytes; }
    BOOL r1 = U(SystemParametersInfoW)(spi, param, a, 0);
    LONG_PTR r2 = nt(spi, param, b, 0);
    CHECKF((r1 != 0) == ((int)r2 != 0), "SPI 0x%x: API=%d NtUser=%ld", spi, r1, (long)r2);
    CHECKF(memcmp(a, b, bytes) == 0, "SPI 0x%x buffers differ", spi);
    /* bytes past the documented payload are not written */
    CHECK(a[bytes] == 0xa5 && b[bytes] == 0xa5);
}
#define SPIGET(name, spi, param, bytes, hdr) TEST(user_serverinfo_shared_ntuser_spi_##name) { si_spi_direct_vs_api(spi, param, bytes, hdr); }
SPIGET(getbeep,                 SPI_GETBEEP, 0, 4, 0)
SPIGET(getmouse,                SPI_GETMOUSE, 0, 12, 0)
SPIGET(getborder,               SPI_GETBORDER, 0, 4, 0)
SPIGET(getkeyboardspeed,        SPI_GETKEYBOARDSPEED, 0, 4, 0)
SPIGET(getscreensavetimeout,    SPI_GETSCREENSAVETIMEOUT, 0, 4, 0)
SPIGET(getscreensaveactive,     SPI_GETSCREENSAVEACTIVE, 0, 4, 0)
SPIGET(getgridgranularity,      SPI_GETGRIDGRANULARITY, 0, 4, 0)
SPIGET(getkeyboarddelay,        SPI_GETKEYBOARDDELAY, 0, 4, 0)
SPIGET(geticontitlewrap,        SPI_GETICONTITLEWRAP, 0, 4, 0)
SPIGET(getmenudropalignment,    SPI_GETMENUDROPALIGNMENT, 0, 4, 0)
SPIGET(getdragfullwindows,      SPI_GETDRAGFULLWINDOWS, 0, 4, 0)
SPIGET(getworkarea,             SPI_GETWORKAREA, 0, 16, 0)
SPIGET(getfontsmoothing,        SPI_GETFONTSMOOTHING, 0, 4, 0)
SPIGET(getlowpowertimeout,      SPI_GETLOWPOWERTIMEOUT, 0, 4, 0)
SPIGET(getpowerofftimeout,      SPI_GETPOWEROFFTIMEOUT, 0, 4, 0)
SPIGET(getshowsounds,           SPI_GETSHOWSOUNDS, 0, 4, 0)
SPIGET(getkeyboardpref,         SPI_GETKEYBOARDPREF, 0, 4, 0)
SPIGET(getscreenreader,         SPI_GETSCREENREADER, 0, 4, 0)
SPIGET(getmousehoverwidth,      SPI_GETMOUSEHOVERWIDTH, 0, 4, 0)
SPIGET(getmousehoverheight,     SPI_GETMOUSEHOVERHEIGHT, 0, 4, 0)
SPIGET(getmousehovertime,       SPI_GETMOUSEHOVERTIME, 0, 4, 0)
SPIGET(getwheelscrolllines,     SPI_GETWHEELSCROLLLINES, 0, 4, 0)
SPIGET(getmenushowdelay,        SPI_GETMENUSHOWDELAY, 0, 4, 0)
SPIGET(getmousespeed,           SPI_GETMOUSESPEED, 0, 4, 0)
SPIGET(getfocusborderwidth,     SPI_GETFOCUSBORDERWIDTH, 0, 4, 0)
SPIGET(getfocusborderheight,    SPI_GETFOCUSBORDERHEIGHT, 0, 4, 0)
SPIGET(getcaretwidth,           SPI_GETCARETWIDTH, 0, 4, 0)
SPIGET(getforegroundlocktimeout, SPI_GETFOREGROUNDLOCKTIMEOUT, 0, 4, 0)
SPIGET(getmenuanimation,        SPI_GETMENUANIMATION, 0, 4, 0)
SPIGET(getcombobokanimation,    SPI_GETCOMBOBOXANIMATION, 0, 4, 0)
SPIGET(getlistboxsmoothscrolling, SPI_GETLISTBOXSMOOTHSCROLLING, 0, 4, 0)
SPIGET(getgradientcaptions,     SPI_GETGRADIENTCAPTIONS, 0, 4, 0)
SPIGET(getflatmenu,             SPI_GETFLATMENU, 0, 4, 0)
SPIGET(getdesktopshadow,        SPI_GETDROPSHADOW, 0, 4, 0)
SPIGET(getminimizedmetrics,     SPI_GETMINIMIZEDMETRICS, 0x14, 0x14, 1)

TEST(user_serverinfo_shared_spi_keyboard_values_in_documented_ranges) {
    REQUIRE(U(SystemParametersInfoW));
    U(GetSystemMetrics)(0);
    DWORD delay = 99, speed = 99;
    CHECK(U(SystemParametersInfoW)(SPI_GETKEYBOARDDELAY, 0, &delay, 0));
    CHECK(U(SystemParametersInfoW)(SPI_GETKEYBOARDSPEED, 0, &speed, 0));
    /* documented: delay 0..3 (250ms..1s), speed 0..31 */
    CHECKF(delay <= 3, "delay=%lu", (unsigned long)delay);
    CHECKF(speed <= 31, "speed=%lu", (unsigned long)speed);
}
TEST(user_serverinfo_shared_spi_mouse_speed_in_documented_range) {
    REQUIRE(U(SystemParametersInfoW));
    U(GetSystemMetrics)(0);
    int sp = 0;
    CHECK(U(SystemParametersInfoW)(SPI_GETMOUSESPEED, 0, &sp, 0));
    CHECKF(sp >= 1 && sp <= 20, "mouse speed=%d", sp);
}
TEST(user_serverinfo_shared_spi_mouse_triple_sane) {
    REQUIRE(U(SystemParametersInfoW));
    U(GetSystemMetrics)(0);
    int m[3] = { -1, -1, -1 };
    CHECK(U(SystemParametersInfoW)(SPI_GETMOUSE, 0, m, 0));
    CHECK(m[0] >= 0 && m[1] >= 0 && (m[2] == 0 || m[2] == 1 || m[2] == 2));
}
TEST(user_serverinfo_shared_spi_wheel_scroll_lines_nonnegative) {
    REQUIRE(U(SystemParametersInfoW));
    U(GetSystemMetrics)(0);
    UINT l = 0xcccccccc;
    CHECK(U(SystemParametersInfoW)(SPI_GETWHEELSCROLLLINES, 0, &l, 0));
    CHECKF(l <= 100 || l == (UINT)-1, "lines=%u", l);
}

/* ================= double click / caret blink: syscall-served values ================= */
UDECL(UINT, NtUserGetDoubleClickTime, (void));
UDECL(UINT, NtUserGetCaretBlinkTime, (void));
TEST(user_serverinfo_shared_ntuser_doubleclicktime_matches_api) {
    char *psi = si_get(); if (!psi) return;
    REQUIRE(UN(NtUserGetDoubleClickTime)); REQUIRE(U(GetDoubleClickTime));
    UINT a = U(GetDoubleClickTime)();
    UINT b = UN(NtUserGetDoubleClickTime)();
    /* user32!GetDoubleClickTime is a bare tail-jump to the win32u stub */
    EXPECT_EQ(a, b);
    CHECKF(a >= 1 && a <= 5000, "double click time %u", a);
}
TEST(user_serverinfo_shared_ntuser_caretblinktime_matches_api) {
    char *psi = si_get(); if (!psi) return;
    REQUIRE(UN(NtUserGetCaretBlinkTime)); REQUIRE(U(GetCaretBlinkTime));
    UINT a = U(GetCaretBlinkTime)();
    UINT b = UN(NtUserGetCaretBlinkTime)();
    EXPECT_EQ(a, b);
    t_note("caret blink time %u (0xffffffff = no blink)", a);
}
TEST(user_serverinfo_shared_ntuser_caretblink_vs_caret_metric) {
    char *psi = si_get(); if (!psi) return;
    UINT t = U(GetCaretBlinkTime)();
    int en = U(GetSystemMetrics)(0x2002); /* SM_CARETBLINKINGENABLED */
    /* SM_CARETBLINKINGENABLED is a kernel flag bit; a blink time of INFINITE (-1) means blinking is off */
    EXPECT_EQ(en, (si_u32(psi, SI_SystemRemarks) >> 2) & 1);
    t_note("blink time %u, blinking enabled %d", t, en);
}
TEST(user_serverinfo_shared_double_click_size_metrics_positive) {
    char *psi = si_get(); if (!psi) return;
    EXPECT_EQ(si_i32(psi, 0x7f8), U(GetSystemMetrics)(SM_CXDOUBLECLK));
    EXPECT_EQ(si_i32(psi, 0x7fc), U(GetSystemMetrics)(SM_CYDOUBLECLK));
    CHECK(U(GetSystemMetrics)(SM_CXDOUBLECLK) > 0 && U(GetSystemMetrics)(SM_CYDOUBLECLK) > 0);
}
TEST(user_serverinfo_shared_drag_metrics_positive) {
    char *psi = si_get(); if (!psi) return;
    EXPECT_EQ(si_i32(psi, 0x878), U(GetSystemMetrics)(SM_CXDRAG));
    EXPECT_EQ(si_i32(psi, 0x87c), U(GetSystemMetrics)(SM_CYDRAG));
    CHECK(U(GetSystemMetrics)(SM_CXDRAG) > 0 && U(GetSystemMetrics)(SM_CYDRAG) > 0);
}

/* ================= screen DC versus the page ================= */
TEST(user_serverinfo_shared_screen_dc_resolution_vs_metrics) {
    char *psi = si_get(); if (!psi) return;
    HDC dc = U(GetDC)(NULL); REQUIRE(dc);
    int hr = U(GetDeviceCaps)(dc, HORZRES), vr = U(GetDeviceCaps)(dc, VERTRES);
    U(ReleaseDC)(NULL, dc);
    int cx = U(GetSystemMetrics)(SM_CXSCREEN), cy = U(GetSystemMetrics)(SM_CYSCREEN);
    if (si_u16(psi, SI_SystemDpi) == 96) { EXPECT_EQ(hr, cx); EXPECT_EQ(vr, cy); }
    else t_note("system DPI != 96: HORZRES %d VERTRES %d vs metrics %d %d", hr, vr, cx, cy);
    CHECK(hr > 0 && vr > 0);
}
TEST(user_serverinfo_shared_screen_dc_logpixels_unaware_is_96) {
    char *psi = si_get(); if (!psi) return;
    REQUIRE(UN(IsProcessDPIAware));
    HDC dc = U(GetDC)(NULL); REQUIRE(dc);
    int lx = U(GetDeviceCaps)(dc, LOGPIXELSX), ly = U(GetDeviceCaps)(dc, LOGPIXELSY);
    U(ReleaseDC)(NULL, dc);
    CHECK(lx == ly);
    if (!UN(IsProcessDPIAware)()) EXPECT_EQ(lx, 96); else EXPECT_EQ(lx, si_u16(psi, SI_SystemDpi));
}
TEST(user_serverinfo_shared_screen_dc_color_depth_plausible) {
    char *psi = si_get(); if (!psi) return;
    HDC dc = U(GetDC)(NULL); REQUIRE(dc);
    int bpp = U(GetDeviceCaps)(dc, BITSPIXEL), pl = U(GetDeviceCaps)(dc, PLANES);
    int rast = U(GetDeviceCaps)(dc, RASTERCAPS);
    U(ReleaseDC)(NULL, dc);
    CHECKF(bpp * pl >= 4, "bpp=%d planes=%d", bpp, pl);
    if (bpp * pl > 8) CHECK(!(rast & RC_PALETTE));
    t_note("screen DC: %d bpp x %d planes, raster caps 0x%x", bpp, pl, rast);
}
TEST(user_serverinfo_shared_screen_dc_matches_same_display_format) {
    char *psi = si_get(); if (!psi) return;
    int same = U(GetSystemMetrics)(SM_SAMEDISPLAYFORMAT), n = U(GetSystemMetrics)(SM_CMONITORS);
    if (n == 1) EXPECT_EQ(same, 1);
    else t_note("%d monitors, SM_SAMEDISPLAYFORMAT=%d", n, same);
}
TEST(user_serverinfo_shared_screen_dc_virtual_matches_desktop_caps) {
    char *psi = si_get(); if (!psi) return;
    HDC dc = U(GetDC)(NULL); REQUIRE(dc);
    int dh = U(GetDeviceCaps)(dc, DESKTOPHORZRES), dv = U(GetDeviceCaps)(dc, DESKTOPVERTRES);
    U(ReleaseDC)(NULL, dc);
    /* DESKTOPHORZRES is the physical primary-monitor resolution: never smaller than the (possibly DPI-virtualized) SM_CXSCREEN */
    CHECK(dh >= U(GetSystemMetrics)(SM_CXSCREEN) && dv >= U(GetSystemMetrics)(SM_CYSCREEN));
}
TEST(user_serverinfo_shared_screen_dc_stock_font_char_size_vs_dialog_units) {
    char *psi = si_get(); if (!psi) return;
    HDC dc = U(GetDC)(NULL); REQUIRE(dc);
    TEXTMETRICW tm;
    HGDIOBJ old = U(SelectObject)(dc, U(GetStockObject)(DEFAULT_GUI_FONT));
    BOOL ok = U(GetTextMetricsW)(dc, &tm);
    U(SelectObject)(dc, old);
    U(ReleaseDC)(NULL, dc);
    REQUIRE(ok);
    LONG u = U(GetDialogBaseUnits)();
    t_note("DEFAULT_GUI_FONT avg %ld height %ld; dialog base units %d,%d", tm.tmAveCharWidth, tm.tmHeight, LOWORD(u), HIWORD(u));
    CHECK(tm.tmAveCharWidth > 0 && tm.tmHeight > 0);
}

/* ================= class / atom tables ================= */
static LRESULT CALLBACK si_wndproc(HWND h, UINT m, WPARAM w, LPARAM l) { return U(DefWindowProcA)(h, m, w, l); }
static ATOM si_register(const char *name) {
    WNDCLASSEXA wc; memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc; wc.lpfnWndProc = si_wndproc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = name;
    return U(RegisterClassExA)(&wc);
}
static void si_cls_name(char *buf, size_t cap, const char *tag) { snprintf(buf, cap, "win32kt_si_%s_%lu", tag, GetCurrentProcessId()); }

TEST(user_serverinfo_shared_class_atom_in_dynamic_range) {
    REQUIRE(U(RegisterClassExA)); REQUIRE(U(UnregisterClassA));
    char n[64]; si_cls_name(n, sizeof n, "rng");
    ATOM a = si_register(n); REQUIRE(a);
    /* class atoms come from the window station atom table: 0xC000..0xFFFF */
    CHECKF(a >= 0xC000, "atom 0x%x", a);
    CHECK(U(UnregisterClassA)(n, GetModuleHandleA(NULL)));
}
TEST(user_serverinfo_shared_class_atom_gcw_atom_roundtrip) {
    REQUIRE(U(RegisterClassExA)); REQUIRE(U(CreateWindowExA));
    char n[64]; si_cls_name(n, sizeof n, "gcw");
    ATOM a = si_register(n); REQUIRE(a);
    HWND w = U(CreateWindowExA)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, n, "x", WS_POPUP, -32000, -32000, 1, 1, NULL, NULL, GetModuleHandleA(NULL), NULL);
    if (w) {
        EXPECT_EQ(U(GetClassLongA)(w, GCW_ATOM), a);
        char out[64] = {0};
        CHECK(U(GetClassNameA)(w, out, sizeof out) == (int)strlen(n));
        EXPECT_STR(out, n);
        U(DestroyWindow)(w);
    } else t_note("CreateWindowEx failed (err %lu), window-based checks skipped", GetLastError());
    CHECK(U(UnregisterClassA)(n, GetModuleHandleA(NULL)));
}
TEST(user_serverinfo_shared_class_atoms_are_distinct) {
    REQUIRE(U(RegisterClassExA));
    char n1[64], n2[64]; si_cls_name(n1, sizeof n1, "d1"); si_cls_name(n2, sizeof n2, "d2");
    ATOM a1 = si_register(n1), a2 = si_register(n2);
    CHECK(a1 && a2 && a1 != a2);
    U(UnregisterClassA)(n1, GetModuleHandleA(NULL)); U(UnregisterClassA)(n2, GetModuleHandleA(NULL));
}
TEST(user_serverinfo_shared_class_duplicate_registration_fails) {
    REQUIRE(U(RegisterClassExA));
    char n[64]; si_cls_name(n, sizeof n, "dup");
    ATOM a = si_register(n); REQUIRE(a);
    SET_ERR(0);
    ATOM b = si_register(n);
    CHECK(b == 0);
    EXPECT_ERR(ERROR_CLASS_ALREADY_EXISTS);
    U(UnregisterClassA)(n, GetModuleHandleA(NULL));
}
TEST(user_serverinfo_shared_class_unregister_with_live_window_fails) {
    REQUIRE(U(RegisterClassExA)); REQUIRE(U(CreateWindowExA));
    char n[64]; si_cls_name(n, sizeof n, "live");
    ATOM a = si_register(n); REQUIRE(a);
    HWND w = U(CreateWindowExA)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, n, "x", WS_POPUP, -32000, -32000, 1, 1, NULL, NULL, GetModuleHandleA(NULL), NULL);
    REQUIRE(w);
    SET_ERR(0);
    CHECK(!U(UnregisterClassA)(n, GetModuleHandleA(NULL)));
    EXPECT_ERR(ERROR_CLASS_HAS_WINDOWS);
    U(DestroyWindow)(w);
    CHECK(U(UnregisterClassA)(n, GetModuleHandleA(NULL)));
}
TEST(user_serverinfo_shared_class_unregister_unknown_fails) {
    REQUIRE(U(UnregisterClassA));
    SET_ERR(0);
    CHECK(!U(UnregisterClassA)("win32kt_si_never_registered", GetModuleHandleA(NULL)));
    EXPECT_ERR(ERROR_CLASS_DOES_NOT_EXIST);
}
TEST(user_serverinfo_shared_class_lookup_by_atom) {
    REQUIRE(U(RegisterClassExA)); REQUIRE(U(GetClassInfoExA));
    char n[64]; si_cls_name(n, sizeof n, "byatom");
    ATOM a = si_register(n); REQUIRE(a);
    WNDCLASSEXA wc; memset(&wc, 0, sizeof wc); wc.cbSize = sizeof wc;
    ATOM r = (ATOM)(ULONG_PTR)U(GetClassInfoExA)(GetModuleHandleA(NULL), MAKEINTATOM(a), &wc);
    CHECK(r != 0);
    CHECK(wc.lpfnWndProc == si_wndproc);
    U(UnregisterClassA)(n, GetModuleHandleA(NULL));
}
TEST(user_serverinfo_shared_class_atom_after_unregister_not_found) {
    REQUIRE(U(RegisterClassExA)); REQUIRE(U(GetClassInfoExA));
    char n[64]; si_cls_name(n, sizeof n, "gone");
    ATOM a = si_register(n); REQUIRE(a);
    CHECK(U(UnregisterClassA)(n, GetModuleHandleA(NULL)));
    WNDCLASSEXA wc; memset(&wc, 0, sizeof wc); wc.cbSize = sizeof wc;
    SET_ERR(0);
    CHECK(!U(GetClassInfoExA)(GetModuleHandleA(NULL), n, &wc));
    EXPECT_ERR(ERROR_CLASS_DOES_NOT_EXIST);
}
TEST(user_serverinfo_shared_class_global_atom_range) {
    REQUIRE(U(GlobalAddAtomA)); REQUIRE(U(GlobalDeleteAtom)); REQUIRE(U(GlobalGetAtomNameA));
    char n[64]; si_cls_name(n, sizeof n, "gatom");
    ATOM a = U(GlobalAddAtomA)(n); REQUIRE(a);
    /* string atoms live in 0xC000..0xFFFF */
    CHECKF(a >= 0xC000, "atom 0x%x", a);
    char out[64] = {0};
    UINT len = U(GlobalGetAtomNameA)(a, out, sizeof out);
    EXPECT_EQ(len, strlen(n));
    EXPECT_STR(out, n);
    CHECK(U(GlobalAddAtomA)(n) == a); /* same string -> same atom (refcounted) */
    U(GlobalDeleteAtom)(a); U(GlobalDeleteAtom)(a);
}
TEST(user_serverinfo_shared_class_global_atom_vs_class_atom_note) {
    REQUIRE(U(RegisterClassExA)); REQUIRE(U(GlobalFindAtomA));
    char n[64]; si_cls_name(n, sizeof n, "cmp");
    ATOM a = si_register(n); REQUIRE(a);
    ATOM g = U(GlobalFindAtomA)(n);
    t_info("class atom 0x%x, GlobalFindAtom of the class name = 0x%x", a, g);
    U(UnregisterClassA)(n, GetModuleHandleA(NULL));
}
TEST(user_serverinfo_shared_class_builtin_atoms_resolve) {
    REQUIRE(U(GetClassInfoExA));
    static const char *names[] = { "Button", "Edit", "Static", "ListBox", "ComboBox", "ScrollBar", "#32770", "#32769", "#32768", "#32771" };
    U(GetSystemMetrics)(0);
    for (unsigned i = 0; i < sizeof names / sizeof *names; i++) {
        WNDCLASSEXA wc; memset(&wc, 0, sizeof wc); wc.cbSize = sizeof wc;
        BOOL ok = U(GetClassInfoExA)(NULL, names[i], &wc) != 0;
        CHECKF(ok || i > 5, "class %s not found", names[i]);
    }
}

/* ================= syscall stubs of this area ================= */
static void si_stub_number(const char *name, long want) {
    FARPROC p = t_proc_in("win32u.dll", name);
    if (!p) { t_skip("win32u.dll missing %s", name); return; }
    CHECK(k_stub_valid(p));
    if (t_is_wine()) { t_note("%s -> %ld (Wine table)", name, k_stub_number(p)); return; }
    CHECKF(k_stub_number(p) == want, "%s service number %ld want %ld", name, k_stub_number(p), want);
}
#define STUBNUM(n, v) TEST(user_serverinfo_shared_stub_number_##n) { si_stub_number(#n, v); }
STUBNUM(NtUserSystemParametersInfo, 4162)
STUBNUM(NtUserSystemParametersInfoForDpi, 5382)
STUBNUM(NtUserGetDoubleClickTime, 4270)
STUBNUM(NtUserGetCaretBlinkTime, 4327)
STUBNUM(NtUserSetSysColors, 5356)
STUBNUM(NtUserGetDpiForCurrentProcess, 5125)
STUBNUM(NtUserEnumDisplayMonitors, 4170)
STUBNUM(NtUserGetThreadState, 4096)
STUBNUM(NtUserCallOneParam, 4098)
STUBNUM(NtUserCallTwoParam, 4138)
STUBNUM(NtUserCallNoParam, 4101)
STUBNUM(NtUserGetAtomName, 4258)

UDECL(ULONG_PTR, NtUserGetThreadState, (ULONG));
TEST(user_serverinfo_shared_ntuser_getthreadstate_0xe_is_one) {
    char *psi = si_get(); if (!psi) return;
    REQUIRE(UN(NtUserGetThreadState));
    /* win32kfull!NtUserGetThreadState: class 0xe returns 1 for any thread that reaches it (user32 uses it as a "GUI thread exists" probe). */
    EXPECT_EQ(UN(NtUserGetThreadState)(0xe), 1);
    void *ti = NULL;
    CHECK(k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &ti));
    CHECK(ti != NULL);
}
TEST(user_serverinfo_shared_ntuser_getthreadstate_foreground_is_boolean) {
    char *psi = si_get(); if (!psi) return;
    REQUIRE(UN(NtUserGetThreadState));
    ULONG_PTR v = UN(NtUserGetThreadState)(10); /* class 10: is this thread's queue the foreground queue */
    CHECK(v == 0 || v == 1);
}
TEST(user_serverinfo_shared_ntuser_getthreadstate_unknown_class_zero) {
    char *psi = si_get(); if (!psi) return;
    REQUIRE(UN(NtUserGetThreadState));
    /* classes past 0xf fall through every branch -> 0 */
    EXPECT_EQ(UN(NtUserGetThreadState)(0x10), 0);
    EXPECT_EQ(UN(NtUserGetThreadState)(0x7fffffff), 0);
}
TEST(user_serverinfo_shared_win32thread_info_present_after_gui_call) {
    char *psi = si_get(); if (!psi) return;
    void *ti = NULL;
    REQUIRE(k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &ti));
    CHECK(ti != NULL);
}

TEST(user_serverinfo_shared_ntuser_getthreadstate_negative_class_zero) {
    char *psi = si_get(); if (!psi) return;
    REQUIRE(UN(NtUserGetThreadState));
    /* param < 9 and not 0..8 falls through to 0 */
    EXPECT_EQ(UN(NtUserGetThreadState)((ULONG)-1), 0);
    EXPECT_EQ(UN(NtUserGetThreadState)((ULONG)0x80000000u), 0);
}
TEST(user_serverinfo_shared_ntuser_getthreadstate_boolean_classes) {
    char *psi = si_get(); if (!psi) return;
    REQUIRE(UN(NtUserGetThreadState));
    /* class 5 is ((GetQueueStatus & 5) != 0), class 0xf is a single bit of the THREADINFO flags: both strictly 0 or 1 */
    ULONG_PTR a = UN(NtUserGetThreadState)(5), b = UN(NtUserGetThreadState)(0xf);
    CHECK(a == 0 || a == 1);
    CHECK(b == 0 || b == 1);
}
TEST(user_serverinfo_shared_ntuser_getthreadstate_stable_across_calls) {
    char *psi = si_get(); if (!psi) return;
    REQUIRE(UN(NtUserGetThreadState));
    /* class 0xe is a constant 1 and class 9 reads a per-thread field that a read-only query does not change */
    ULONG_PTR a = UN(NtUserGetThreadState)(9);
    EXPECT_EQ(UN(NtUserGetThreadState)(0xe), 1);
    EXPECT_EQ(UN(NtUserGetThreadState)(9), a);
}
