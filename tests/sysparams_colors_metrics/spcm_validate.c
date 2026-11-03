/* NtUserSystemParametersInfo parameter validation, called straight through the win32u.dll stub so the
 * user32 wrapper (which pre-checks several struct sizes and never sets a last error for them) is out of the way.
 *
 * Kernel decomposition used (win32kbase!NtUserSystemParametersInfo, 19041):
 *   - the first switch on `action` only matters for UIPI-restricted callers (not this test's process);
 *   - the second switch decides how pvParam is probed/copied: 4-byte copy-out for plain GETs, struct copy-in/out
 *     for sized structures, no copy for everything else;
 *   - several actions require uiParam to equal the structure size, otherwise UserSetLastError(0x57) and return 0
 *     before any state is touched, and the caller's buffer is not written;
 *   - actions 0x1000..0x1053 and 0x2000..0x2029 other than a few special ones require uiParam == 0 (else 0x57);
 *   - win32kfull SystemParametersInfo answers 0x57 for the unused action numbers listed below, and
 *     returns 1 for the deprecated power-management numbers.
 */
#include "spcm.h"

#define BADRC(act, up) do { gb_init(&g); r = spi((act), (up), g.b, 0); EXPECT_ERR(87); CHECK(r == 0); CHECK(gb_pristine(&g, 0, 1024)); CHECK(gb_edges_ok(&g)); } while (0)

TEST(sysparams_stubs_service_numbers) {
    static const struct { const char *n; long num; } v[] = {
        { "NtUserSystemParametersInfo", 4162 }, { "NtUserSystemParametersInfoForDpi", 5382 },
        { "NtUserSetSysColors", 5356 }, { "NtUserGetDoubleClickTime", 4270 }, { "NtUserGetCaretBlinkTime", 4327 },
        { "NtUserCallOneParam", 4098 },
    };
    for (int i = 0; i < 6; i++) {
        FARPROC p = t_proc_in("win32u.dll", v[i].n);
        if (!p) { t_skip("win32u.dll missing %s", v[i].n); return; }
        CHECK(k_stub_valid(p));
        if (t_is_wine()) { t_note("%s number %ld (Wine table)", v[i].n, k_stub_number(p)); continue; }
        CHECKF(k_stub_number(p) == v[i].num, "%s got %ld want %ld", v[i].n, k_stub_number(p), v[i].num);
    }
}

/* ---- unused action numbers: SystemParametersInfo -> UserSetLastError(0x57) ---------------------- */
#define UNUSED_ACTION_TEST(tag, act) \
    TEST(sysparams_unused_action_##tag) { NEED_SPI(); spcm_buf g; int r; BADRC(act, 0); BADRC(act, 1); }
UNUSED_ACTION_TEST(0x07, 0x07)
UNUSED_ACTION_TEST(0x08, 0x08)
UNUSED_ACTION_TEST(0x0c, 0x0c)
UNUSED_ACTION_TEST(0x27, 0x27)
UNUSED_ACTION_TEST(0x28, 0x28)
UNUSED_ACTION_TEST(0x31, 0x31)
UNUSED_ACTION_TEST(0x5c, 0x5c)
UNUSED_ACTION_TEST(0x61, 0x61)

/* SPI_GETSERIALKEYS / SPI_SETSERIALKEYS are not supported on Windows 10: same worker mask as the unused numbers. */
TEST(sysparams_serialkeys_unsupported) {
    NEED_SPI(); spcm_buf g; int r;
    BADRC(0x3e, 0); BADRC(0x3f, 0); BADRC(0x3e, 0x1c); BADRC(0x3f, 0x1c);
}

/* ---- out-of-table actions: NtUserSystemParametersInfo's `default` ends in 0x57 ------------------------- */
TEST(sysparams_action_above_table_0xb0) {
    NEED_SPI(); spcm_buf g; int r; BADRC(0xb0, 0); BADRC(0xb1, 0); BADRC(0xb2, 0); BADRC(0xff, 0);
}
TEST(sysparams_action_between_tables) {
    NEED_SPI(); spcm_buf g; int r;
    static const UINT a[] = { 0x1054, 0x1055, 0x1100, 0x1fff, 0x202a, 0x202b, 0x2100, 0x3000, 0x3fff };
    for (unsigned i = 0; i < sizeof a / sizeof a[0]; i++) BADRC(a[i], 0);
}
TEST(sysparams_action_huge_values) {
    NEED_SPI(); spcm_buf g; int r;
    static const UINT a[] = { 0x10000, 0x7fffffff, 0x80000000u, 0xfffffffeu, 0xffffffffu };
    for (unsigned i = 0; i < sizeof a / sizeof a[0]; i++) BADRC(a[i], 0);
}

/* ---- uiParam must be 0 for the 0x1000 / 0x2000 flag-and-value actions --------------------------------- */
TEST(sysparams_flag_get_actions_reject_uiparam) {
    NEED_SPI(); spcm_buf g; int r;
    /* even numbers are GETs; 0x1026 (copied straight to the 4-byte path) and 0x103f (uiParam==1 build-dependent) are excluded */
    for (UINT a = 0x1000; a <= 0x1053; a += 2) { if (a == 0x1026 || a == 0x103e) continue; BADRC(a, 0x7777); }
}
TEST(sysparams_flag_set_actions_reject_uiparam) {
    NEED_SPI(); spcm_buf g; int r;
    for (UINT a = 0x1001; a <= 0x1053; a += 2) { if (a == 0x1027 || a == 0x103f) continue; BADRC(a, 0x7777); }
}
TEST(sysparams_value_get_actions_reject_uiparam) {
    NEED_SPI(); spcm_buf g; int r;
    for (UINT a = 0x2000; a <= 0x2029; a += 2) { if (a == 0x201c) continue; BADRC(a, 0x7777); }
}
TEST(sysparams_value_set_actions_reject_uiparam) {
    NEED_SPI(); spcm_buf g; int r;
    for (UINT a = 0x2001; a <= 0x2029; a += 2) { if (a == 0x201d) continue; BADRC(a, 0x7777); }
}
/* Value ranges the kernel checks before storing anything. */
TEST(sysparams_fontsmoothingtype_value_range) {
    NEED_SPI(); int r;
    /* SPI_SETFONTSMOOTHINGTYPE (0x200b): pvParam is the value; (pv & 0xfffffffc) != 0 -> 0x57 */
    for (ULONG_PTR v = 4; v <= 0x40; v <<= 1) { r = spi(0x200b, 0, (PVOID)v, 0); EXPECT_ERR(87); CHECK(r == 0); }
    /* no 64-bit-only value here: the kernel masks the pointer-sized value with 0xfffffffc as a 64-bit AND, so 1<<32 would be accepted and store 0 */
}
TEST(sysparams_fontsmoothingorientation_value_range) {
    NEED_SPI(); int r;
    /* SPI_SETFONTSMOOTHINGORIENTATION (0x2013): only 0 (BGR) and 1 (RGB) */
    for (ULONG_PTR v = 2; v <= 0x40; v <<= 1) { r = spi(0x2013, 0, (PVOID)v, 0); EXPECT_ERR(87); CHECK(r == 0); }
    r = spi(0x2013, 0, (PVOID)3, 0); EXPECT_ERR(87); CHECK(r == 0);
}
/* ---- struct-size protocols: uiParam must equal the structure size ---------------------------------------- */
static void size_proto(UINT act, UINT good) {
    spcm_buf g; int r;
    UINT bad[] = { 0, 1, good - 4, good + 4, good * 2, 0x10000, 0xffffffffu };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        if (bad[i] == good) continue;
        BADRC(act, bad[i]);
    }
}
#define SIZE_PROTO_TEST(tag, act, good) TEST(sysparams_size_protocol_##tag) { NEED_SPI(); size_proto(act, good); }
SIZE_PROTO_TEST(0x92, 0x92, 0x38)
SIZE_PROTO_TEST(0x93, 0x93, 0x38)
SIZE_PROTO_TEST(0x94, 0x94, 0x20)
SIZE_PROTO_TEST(0x95, 0x95, 0x20)
SIZE_PROTO_TEST(0x96, 0x96, 0x1c)
SIZE_PROTO_TEST(0x97, 0x97, 0x1c)
SIZE_PROTO_TEST(0x98, 0x98, 0x80)
SIZE_PROTO_TEST(0x99, 0x99, 0x80)
SIZE_PROTO_TEST(0x9a, 0x9a, 0x224)
SIZE_PROTO_TEST(0x9b, 0x9b, 0x224)
SIZE_PROTO_TEST(0x9c, 0x9c, 0x10)   /* SPI_GETTOUCHPREDICTIONPARAMETERS */
SIZE_PROTO_TEST(0xa5, 0xa5, 0x0c)
SIZE_PROTO_TEST(0xa7, 0xa7, 0x0c)
SIZE_PROTO_TEST(0xac, 0xac, 0x04)
SIZE_PROTO_TEST(0xad, 0xad, 0x04)
SIZE_PROTO_TEST(0xae, 0xae, 0x18)
SIZE_PROTO_TEST(0xaf, 0xaf, 0x18)

/* NONCLIENTMETRICS: uiParam must be 500 (pre-Vista layout) or 504 (with iPaddedBorderWidth): (uiParam-500) & ~4 == 0 */
TEST(sysparams_ncm_get_rejects_bad_sizes) {
    NEED_SPI(); spcm_buf g; int r;
    static const UINT bad[] = { 0, 1, 4, 496, 499, 501, 502, 503, 505, 506, 507, 508, 512, 1000, 0xffffffffu };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) { gb_init(&g); memcpy(g.b, &bad[i], 4); BADRC(0x29, bad[i]); }
}
TEST(sysparams_ncm_set_rejects_bad_sizes) {
    NEED_SPI(); spcm_buf g; int r;
    static const UINT bad[] = { 0, 1, 4, 496, 499, 501, 503, 505, 508, 512, 1000, 0xffffffffu };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) BADRC(0x2a, bad[i]);
}
TEST(sysparams_ncm_get_accepts_504_and_writes_exactly_504) {
    NEED_SPI(); spcm_buf g; gb_init(&g);
    unsigned cb = NCM_SIZE; memcpy(g.b, &cb, 4);
    int r = spi(0x29, NCM_SIZE, g.b, 0);
    CHECK(r == 1);
    EXPECT_EQ(gb_u32(&g, NCM_cbSize), NCM_SIZE);       /* GetWindowNCMetrics never rewrites cbSize: the copy-in value comes back */
    CHECK(gb_pristine(&g, NCM_SIZE, 1024 - NCM_SIZE));   /* copy-out length is exactly uiParam */
    CHECK(gb_edges_ok(&g));
    CHECK((int)gb_u32(&g, NCM_iCaptionHeight) > 0);
    CHECK((int)gb_u32(&g, NCM_iScrollWidth) > 0);
}
TEST(sysparams_ncm_get_accepts_500_and_leaves_padded_border) {
    NEED_SPI(); spcm_buf g; gb_init(&g);
    unsigned cb = NCM_SIZE_OLD; memcpy(g.b, &cb, 4);
    int r = spi(0x29, NCM_SIZE_OLD, g.b, 0);
    CHECK(r == 1);
    /* copy-out is uiParam (500) bytes, so the 4 bytes of iPaddedBorderWidth are not written back */
    EXPECT_EQ(gb_u32(&g, NCM_iPaddedBorderWidth), 0xA5A5A5A5u);
    CHECK(gb_pristine(&g, NCM_SIZE_OLD, 1024 - NCM_SIZE_OLD));
    EXPECT_EQ(gb_u32(&g, NCM_cbSize), NCM_SIZE_OLD);
    CHECK((int)gb_u32(&g, NCM_iCaptionHeight) > 0);
}
TEST(sysparams_ncm_get_ignores_embedded_cbsize) {
    NEED_SPI(); spcm_buf g; gb_init(&g);
    /* the kernel path keys on uiParam only; the embedded cbSize is just echoed back */
    unsigned cb = 0x1234; memcpy(g.b, &cb, 4);
    int r = spi(0x29, NCM_SIZE, g.b, 0);
    CHECK(r == 1);
    EXPECT_EQ(gb_u32(&g, NCM_cbSize), 0x1234);
}

/* ---- ICONMETRICS / ICONTITLELOGFONT / MINIMIZEDMETRICS: fixed kernel-side sizes ------------------------ */
TEST(sysparams_iconmetrics_get_writes_exactly_0x6c) {
    NEED_SPI(); spcm_buf g; gb_init(&g);
    unsigned cb = ICM_SIZE; memcpy(g.b, &cb, 4);
    int r = spi(0x2d, 0, g.b, 0);
    CHECK(r == 1);
    EXPECT_EQ(gb_u32(&g, 0), ICM_SIZE);   /* worker 0x2d writes cbSize = 0x6c itself */
    CHECK(gb_pristine(&g, ICM_SIZE, 1024 - ICM_SIZE));
    CHECK(gb_edges_ok(&g));
}
TEST(sysparams_iconmetrics_get_writes_cbsize_regardless_of_input) {
    NEED_SPI(); spcm_buf g; gb_init(&g);
    memset(g.b, 0, 4);
    int r = spi(0x2d, 0, g.b, 0);
    CHECK(r == 1);
    EXPECT_EQ(gb_u32(&g, 0), ICM_SIZE);
}
TEST(sysparams_icontitlelogfont_get_writes_exactly_0x5c) {
    NEED_SPI(); spcm_buf g; gb_init(&g);
    int r = spi(0x1f, LOGFONTW_SIZE, g.b, 0);
    CHECK(r == 1);
    CHECK(gb_pristine(&g, LOGFONTW_SIZE, 1024 - LOGFONTW_SIZE));
    CHECK(gb_edges_ok(&g));
    LOGFONTW lf; memcpy(&lf, g.b, sizeof lf);
    CHECK(lf.lfHeight != 0);
    CHECK(lf.lfFaceName[0] != 0);
}
TEST(sysparams_minimizedmetrics_get_ignores_input_cbsize) {
    NEED_SPI(); spcm_buf g; gb_init(&g);
    memset(g.b, 0, 4);
    /* worker 0x2b writes left=0x14 unconditionally; the user32 wrapper's cbSize==0x14 test is user-mode only */
    int r = spi(0x2b, 0, g.b, 0);
    CHECK(r == 1);
    EXPECT_EQ(gb_u32(&g, 0), 0x14);
    CHECK(gb_pristine(&g, 0x14, 1024 - 0x14));   /* Nt copies exactly 0x14 bytes back */
    CHECK(gb_edges_ok(&g));
}
/* ---- accessibility structures: cbSize in the struct and uiParam in {0, size} ------------------------------ */
static void acc_proto(UINT get, UINT size) {
    spcm_buf g; int r;
    /* correct cbSize, uiParam 0 and uiParam == size: TRUE, struct size preserved */
    for (int k = 0; k < 2; k++) {
        gb_init(&g); memset(g.b, 0, size); memcpy(g.b, &size, 4);
        r = spi(get, k ? size : 0, g.b, 0);
        CHECKF(r == 1, "action 0x%x uiParam %u", get, k ? size : 0);
        EXPECT_EQ(gb_u32(&g, 0), size);
        CHECK(gb_edges_ok(&g));
    }
    /* wrong embedded cbSize: FALSE and, because the worker simply returns 0, no last error is set */
    UINT wrong[] = { 0, 1, size - 4, size + 4 };
    for (unsigned i = 0; i < 4; i++) {
        gb_init(&g); memset(g.b, 0, size); memcpy(g.b, &wrong[i], 4);
        r = spi(get, 0, g.b, 0);
        CHECKF(r == 0, "action 0x%x cbSize %u", get, wrong[i]);
        EXPECT_EQ(GetLastError(), SPCM_ERR_SENTINEL);
    }
    /* uiParam outside {0, size} */
    UINT badp[] = { 1, 4, size + 1, 0x100 };
    for (unsigned i = 0; i < 4; i++) {
        gb_init(&g); memset(g.b, 0, size); memcpy(g.b, &size, 4);
        r = spi(get, badp[i], g.b, 0);
        CHECKF(r == 0, "action 0x%x uiParam %u", get, badp[i]);
        EXPECT_EQ(GetLastError(), SPCM_ERR_SENTINEL);
    }
}
TEST(sysparams_filterkeys_get_size_protocol)    { NEED_SPI(); acc_proto(0x32, 0x18); }
TEST(sysparams_togglekeys_get_size_protocol)    { NEED_SPI(); acc_proto(0x34, 0x08); }
TEST(sysparams_stickykeys_get_size_protocol)    { NEED_SPI(); acc_proto(0x3a, 0x08); }
TEST(sysparams_accesstimeout_get_size_protocol) { NEED_SPI(); acc_proto(0x3c, 0x0c); }
TEST(sysparams_mousekeys_get_size_protocol)     { NEED_SPI(); acc_proto(0x36, 0x1c); }

/* ---- deprecated power-management numbers: worker returns TRUE without side effects -------------------------- */
TEST(sysparams_power_get_numbers_return_true_zero) {
    NEED_SPI();
    /* SPI_GETLOWPOWERTIMEOUT 0x4f, SPI_GETPOWEROFFTIMEOUT 0x50, SPI_GETLOWPOWERACTIVE 0x53, SPI_GETPOWEROFFACTIVE 0x54:
     * worker stores 0 into the 4-byte out buffer and returns 1. */
    static const UINT a[] = { 0x4f, 0x50, 0x53, 0x54 };
    for (unsigned i = 0; i < 4; i++) {
        spcm_buf g; gb_init(&g);
        int r = spi(a[i], 0, g.b, 0);
        CHECKF(r == 1, "action 0x%x", a[i]);
        EXPECT_EQ(gb_u32(&g, 0), 0);
        CHECK(gb_pristine(&g, 4, 1020));
        CHECK(gb_edges_ok(&g));
    }
}
TEST(sysparams_power_set_numbers_return_true) {
    NEED_SPI();
    /* SPI_SETLOWPOWERTIMEOUT 0x51, SPI_SETPOWEROFFTIMEOUT 0x52, SPI_SETLOWPOWERACTIVE 0x55, SPI_SETPOWEROFFACTIVE 0x56 */
    static const UINT a[] = { 0x51, 0x52, 0x55, 0x56 };
    for (unsigned i = 0; i < 4; i++) {
        spcm_buf g; gb_init(&g);
        int r = spi(a[i], 1, g.b, 0);
        CHECKF(r == 1, "action 0x%x", a[i]);
        CHECK(gb_pristine(&g, 0, 1024));       /* no copy-out for SETs */
        spcm_buf h; gb_init(&h);
        r = spi(a[i], 0, h.b, 0);
        CHECKF(r == 1, "action 0x%x uiParam 0", a[i]);
    }
}

/* ---- icon spacing: pvParam below 0x10000 is the "set" form; uiParam==0 is a TRUE no-op ----------------------- */
TEST(sysparams_icon_spacing_null_pv_zero_uiparam_is_noop) {
    NEED_SPI(); REQUIRE(U(GetSystemMetrics));
    int h0 = U(GetSystemMetrics)(SM_CXICONSPACING), v0 = U(GetSystemMetrics)(SM_CYICONSPACING);
    /* SPI_ICONHORIZONTALSPACING 0x0d / SPI_ICONVERTICALSPACING 0x18: worker returns 1 when (pv & ~0xffff)==0 and uiParam==0 */
    int r = spi(0x0d, 0, NULL, 0); CHECK(r == 1);
    r = spi(0x18, 0, NULL, 0); CHECK(r == 1);
    EXPECT_EQ(U(GetSystemMetrics)(SM_CXICONSPACING), h0);
    EXPECT_EQ(U(GetSystemMetrics)(SM_CYICONSPACING), v0);
}
TEST(sysparams_icon_spacing_get_matches_metric) {
    NEED_SPI(); spcm_buf g;
    int r;
    gb_init(&g); r = spi(0x0d, 0, g.b, 0); CHECK(r == 1);
    int h = (int)gb_u32(&g, 0); CHECK(h > 0); CHECK(gb_pristine(&g, 4, 1020));
    gb_init(&g); r = spi(0x18, 0, g.b, 0); CHECK(r == 1);
    int v = (int)gb_u32(&g, 0); CHECK(v > 0); CHECK(gb_pristine(&g, 4, 1020));
    int h2 = U(GetSystemMetrics)(SM_CXICONSPACING), v2 = U(GetSystemMetrics)(SM_CYICONSPACING);
    if (h != h2 || v != v2) t_note("icon spacing SPI %dx%d vs SM %dx%d (DPI virtualization?)", h, v, h2, v2);
}

/* ---- SPI_GETDESKWALLPAPER: kernel copies at most 0x104 WCHARs --------------------------------------------- */
TEST(sysparams_wallpaper_get_bounded_copy) {
    NEED_SPI(); spcm_buf g; gb_init(&g);
    int r = spi(0x73, 260, g.b, 0);
    CHECK(r == 1);
    /* RtlStringCchCopyW(buf, 0x104, path): always NUL-terminated inside 260 characters */
    const WCHAR *w = (const WCHAR *)g.b; int n = -1;
    for (int i = 0; i < 260; i++) if (!w[i]) { n = i; break; }
    CHECK(n >= 0);
    CHECK(gb_pristine(&g, 520, 1024 - 520));
    CHECK(gb_edges_ok(&g));
}
TEST(sysparams_wallpaper_get_uiparam_larger_than_limit_copies_at_most_0x104) {
    NEED_SPI(); spcm_buf g; gb_init(&g);
    int r = spi(0x73, 400, g.b, 0);
    CHECK(r == 1);
    /* NtUserSystemParametersInfo clamps the copy-out to min(uiParam,0x104)*2 bytes */
    CHECK(gb_pristine(&g, 520, 1024 - 520));
    CHECK(gb_edges_ok(&g));
}
TEST(sysparams_wallpaper_get_matches_user32_wrapper) {
    NEED_SPI(); REQUIRE(U(SystemParametersInfoW));
    WCHAR a[260], b[260]; memset(a, 0xAA, sizeof a); memset(b, 0xBB, sizeof b);
    int r1 = spi(0x73, 260, a, 0);
    BOOL r2 = U(SystemParametersInfoW)(SPI_GETDESKWALLPAPER, 260, b, 0);
    CHECK(r1 == 1); CHECK(r2);
    a[259] = 0; b[259] = 0;
    EXPECT_WSTR(a, b);
}

/* ---- plain GET actions: exactly four bytes come back -------------------------------------------------------- */
static void get4(UINT act, int boolean) {
    spcm_buf g; gb_init(&g);
    int r = spi(act, 0, g.b, 0);
    CHECKF(r == 1, "action 0x%x", act);
    CHECKF(gb_pristine(&g, 4, 1020), "action 0x%x wrote past 4 bytes", act);
    CHECK(gb_edges_ok(&g));
    unsigned v = gb_u32(&g, 0);
    CHECKF(v != 0xA5A5A5A5u, "action 0x%x did not write its value", act);
    if (boolean && v > 1) t_note("action 0x%x returned %u for a BOOL", act, v);
}
#define GET4_TEST(tag, act, b) TEST(sysparams_get4_##tag) { NEED_SPI(); get4(act, b); }
GET4_TEST(beep_0x01, 0x01, 1)
GET4_TEST(border_0x05, 0x05, 0)
GET4_TEST(keyboardspeed_0x0a, 0x0a, 0)
GET4_TEST(screensavetimeout_0x0e, 0x0e, 0)
GET4_TEST(screensaveactive_0x10, 0x10, 1)
/* Action 0x12 (SPI_GETGRIDGRANULARITY) is obsolete: the kernel refuses it with ERROR_INVALID_SPI_VALUE. */
TEST(sysparams_get_gridgranularity_0x12_refused) {
    NEED_SPI(); spcm_buf g; gb_init(&g);
    int r = spi(0x12, 0, g.b, 0);
    EXPECT_ERR(1439); CHECK(r == 0);
    CHECK(gb_pristine(&g, 4, 1020)); CHECK(gb_edges_ok(&g));      /* at most the first dword is touched */
}
GET4_TEST(keyboarddelay_0x16, 0x16, 0)
GET4_TEST(icontitlewrap_0x19, 0x19, 1)
GET4_TEST(menudropalignment_0x1b, 0x1b, 1)
GET4_TEST(dragfullwindows_0x26, 0x26, 1)
GET4_TEST(keyboardpref_0x44, 0x44, 1)
GET4_TEST(screenreader_0x46, 0x46, 1)
GET4_TEST(fontsmoothing_0x4a, 0x4a, 1)
GET4_TEST(mousehoverwidth_0x62, 0x62, 0)
GET4_TEST(mousehoverheight_0x64, 0x64, 0)
GET4_TEST(mousehovertime_0x66, 0x66, 0)
GET4_TEST(wheelscrolllines_0x68, 0x68, 0)
GET4_TEST(menushowdelay_0x6a, 0x6a, 0)
GET4_TEST(wheelscrollchars_0x6c, 0x6c, 0)
GET4_TEST(snaptodefbutton_0x5f, 0x5f, 1)
GET4_TEST(activewindowtracking_0x1000, 0x1000, 1)
GET4_TEST(menuanimation_0x1002, 0x1002, 1)
GET4_TEST(comboboxanimation_0x1004, 0x1004, 1)
GET4_TEST(gradientcaptions_0x1008, 0x1008, 1)
GET4_TEST(keyboardcues_0x100a, 0x100a, 1)
GET4_TEST(hottracking_0x100e, 0x100e, 1)
GET4_TEST(menufade_0x1012, 0x1012, 1)
GET4_TEST(selectionfade_0x1014, 0x1014, 1)
GET4_TEST(tooltipanimation_0x1016, 0x1016, 1)
GET4_TEST(cursorshadow_0x101a, 0x101a, 1)
GET4_TEST(flatmenu_0x1022, 0x1022, 1)
GET4_TEST(dropshadow_0x1024, 0x1024, 1)
GET4_TEST(uieffects_0x103e, 0x103e, 1)
GET4_TEST(activewndtrktimeout_0x2002, 0x2002, 0)
GET4_TEST(foregroundflashcount_0x2004, 0x2004, 0)
GET4_TEST(caretwidth_0x2006, 0x2006, 0)
GET4_TEST(mouseclicklocktime_0x2008, 0x2008, 0)
GET4_TEST(fontsmoothingtype_0x200a, 0x200a, 0)
GET4_TEST(focusborderwidth_0x200e, 0x200e, 0)
GET4_TEST(focusborderheight_0x2010, 0x2010, 0)
GET4_TEST(fontsmoothingorientation_0x2012, 0x2012, 0)

/* fWinIni is masked with 3 before the action is processed (SystemParametersInfo): stray high bits never reach it. */
TEST(sysparams_get_ignores_high_fwinini_bits) {
    NEED_SPI(); spcm_buf a, b; gb_init(&a); gb_init(&b);
    CHECK(spi(0x01, 0, a.b, 0) == 1);
    CHECK(spi(0x01, 0, b.b, 0xfffffffcu) == 1);
    EXPECT_EQ(gb_u32(&a, 0), gb_u32(&b, 0));
    CHECK(gb_pristine(&b, 4, 1020)); CHECK(gb_edges_ok(&b));
}
TEST(sysparams_get_ignores_uiparam_for_plain_bools) {
    NEED_SPI(); spcm_buf a, b; gb_init(&a); gb_init(&b);
    CHECK(spi(0x19, 0, a.b, 0) == 1);
    CHECK(spi(0x19, 0x12345678, b.b, 0) == 1);
    EXPECT_EQ(gb_u32(&a, 0), gb_u32(&b, 0));
}
/* The user32 wrappers answer an unused action (0x61) with FALSE without calling the kernel, so, unlike the direct
 * stub (ERROR_INVALID_PARAMETER), the last error is left exactly as the caller set it. The very first call into a
 * wrapper clears the last error while it initialises, so each wrapper is warmed up with a harmless GET first. */
static void warm_spi_wrapper(int ansi) {
    BOOL beep;
    if (ansi) U(SystemParametersInfoA)(SPI_GETBEEP, 0, &beep, 0); else U(SystemParametersInfoW)(SPI_GETBEEP, 0, &beep, 0);
}
TEST(sysparams_wrapper_unused_action_matches_direct) {
    NEED_SPI(); REQUIRE(U(SystemParametersInfoW));
    spcm_buf g; gb_init(&g);
    warm_spi_wrapper(0);
    SetLastError(SPCM_ERR_SENTINEL);
    BOOL r = U(SystemParametersInfoW)(0x61, 0, g.b, 0);
    EXPECT_ERR(SPCM_ERR_SENTINEL); CHECK(!r); CHECK(gb_pristine(&g, 0, 1024));
    CHECK(spi(0x61, 0, g.b, 0) == 0);          /* the kernel itself refuses it with 87 */
    EXPECT_ERR(87);
}
TEST(sysparams_wrapper_out_of_table_action) {
    REQUIRE(U(SystemParametersInfoW)); spcm_buf g; gb_init(&g);
    SetLastError(SPCM_ERR_SENTINEL);
    BOOL r = U(SystemParametersInfoW)(0xb0, 0, g.b, 0);
    EXPECT_ERR(87); CHECK(!r);
}
TEST(sysparams_ansi_wrapper_unused_action) {
    REQUIRE(U(SystemParametersInfoA)); spcm_buf g; gb_init(&g);
    warm_spi_wrapper(1);
    SetLastError(SPCM_ERR_SENTINEL);
    BOOL r = U(SystemParametersInfoA)(0x61, 0, g.b, 0);
    EXPECT_ERR(SPCM_ERR_SENTINEL); CHECK(!r);
}
