/* gap9_lwa* / gap9_dpi* / gap9_spidpi*:
 *  NtUserGetLayeredWindowAttributes (win32kfull 1fa320): ValidateHwnd; if the window is composed on the DWM
 *    desktop and state bit 0x20 is set -> 0x57; else _GetLayeredWindowAttributes; each output pointer is
 *    optional and written only on success; *flags is masked with 3.
 *  NtUserGetCurrentDpiInfoForWindow (user32 ordinal 2636 calls it): copies exactly 0x60 bytes of the window's
 *    DPI context record to the caller after ValidateHwnd.
 *  NtUserPerMonitorDPIPhysicalToLogicalPoint: hwnd may be NULL; with a window the converted point must lie in
 *    the window rect or nothing is written and FALSE is returned.
 *  NtUserSystemParametersInfoForDpi: only SPI 0x29/0x1F/0x2D with the exact size and a non-zero dpi. */
#include "g9.h"

UDECL(BOOL, NtUserGetLayeredWindowAttributes, (HWND, COLORREF *, BYTE *, DWORD *));
UDECL(BOOL, NtUserGetCurrentDpiInfoForWindow, (HWND, void *));
UDECL(BOOL, NtUserPerMonitorDPIPhysicalToLogicalPoint, (HWND, POINT *));
UDECL(BOOL, NtUserSystemParametersInfoForDpi, (UINT action, UINT param, void *pv, UINT winini, UINT dpi));

/* ---- layered attributes ------------------------------------------------------------------------- */
static HWND layered_window(void) {
    HWND h = g9_window(WS_EX_LAYERED);
    return h;
}

TEST(gap9_lwa_invalid_handles) {
    REQUIRE(UN(NtUserGetLayeredWindowAttributes));
    HWND bad[] = { NULL, (HWND)(ULONG_PTR)0x777777, (HWND)(ULONG_PTR)-1 };
    for (int i = 0; i < G9_N(bad); i++) {
        COLORREF k = 0x11; BYTE a = 0x22; DWORD f = 0x33;
        SetLastError(0);
        CHECKF(!UN(NtUserGetLayeredWindowAttributes)(bad[i], &k, &a, &f), "hwnd %p", bad[i]);
        EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
        CHECK(k == 0x11 && a == 0x22 && f == 0x33);        /* outputs are written only after success */
    }
}

TEST(gap9_lwa_not_layered_fails) {
    REQUIRE(UN(NtUserGetLayeredWindowAttributes));
    HWND h = g9_window(0);
    if (!h) { t_skip("no window"); return; }
    COLORREF k = 0x11; BYTE a = 0x22; DWORD f = 0x33;
    SetLastError(0);
    CHECK(!UN(NtUserGetLayeredWindowAttributes)(h, &k, &a, &f));
    CHECK(k == 0x11 && a == 0x22 && f == 0x33);
    t_note("non-layered window: last error %lu", GetLastError());
    g9_destroy(h);
}

TEST(gap9_lwa_desktop_window_not_layered) {
    REQUIRE(UN(NtUserGetLayeredWindowAttributes)); REQUIRE(U(GetDesktopWindow));
    COLORREF k = 0; BYTE a = 0; DWORD f = 0;
    CHECK(!UN(NtUserGetLayeredWindowAttributes)(U(GetDesktopWindow)(), &k, &a, &f));
}

TEST(gap9_lwa_roundtrip_alpha_and_key) {
    REQUIRE(UN(NtUserGetLayeredWindowAttributes)); REQUIRE(U(SetLayeredWindowAttributes));
    HWND h = layered_window();
    if (!h) { t_skip("no window"); return; }
    static const BYTE alpha[] = { 0, 1, 127, 128, 254, 255 };
    static const COLORREF key[] = { 0x000000, 0xFFFFFF, 0x123456, 0xFF0000, 0x0000FF, 0x00FF00 };
    for (int i = 0; i < G9_N(alpha); i++) {
        CHECKF(U(SetLayeredWindowAttributes)(h, key[i], alpha[i], LWA_ALPHA | LWA_COLORKEY), "set %d", i);
        COLORREF k = 0xAAAAAAAA; BYTE a = 0xAB; DWORD f = 0xFFFFFFFF;
        CHECKF(UN(NtUserGetLayeredWindowAttributes)(h, &k, &a, &f), "get %d", i);
        EXPECT_EQ(k, key[i]);
        EXPECT_EQ(a, alpha[i]);
        EXPECT_EQ(f, LWA_ALPHA | LWA_COLORKEY);            /* masked with 3 */
    }
    g9_destroy(h);
}

TEST(gap9_lwa_flags_report_which_were_set) {
    REQUIRE(UN(NtUserGetLayeredWindowAttributes)); REQUIRE(U(SetLayeredWindowAttributes));
    HWND h = layered_window();
    if (!h) { t_skip("no window"); return; }
    COLORREF k; BYTE a; DWORD f;
    CHECK(U(SetLayeredWindowAttributes)(h, 0x00020406, 0x44, LWA_ALPHA));
    f = 0xFF; CHECK(UN(NtUserGetLayeredWindowAttributes)(h, NULL, &a, &f));
    EXPECT_EQ(f, LWA_ALPHA); EXPECT_EQ(a, 0x44);
    CHECK(U(SetLayeredWindowAttributes)(h, 0x00AABBCC, 0x44, LWA_COLORKEY));
    f = 0xFF; k = 0; CHECK(UN(NtUserGetLayeredWindowAttributes)(h, &k, NULL, &f));
    EXPECT_EQ(f, LWA_COLORKEY); EXPECT_EQ(k, 0x00AABBCC);
    CHECK(U(SetLayeredWindowAttributes)(h, 0x00AABBCC, 0x80, LWA_ALPHA | LWA_COLORKEY));
    f = 0; CHECK(UN(NtUserGetLayeredWindowAttributes)(h, NULL, NULL, &f));
    EXPECT_EQ(f, LWA_ALPHA | LWA_COLORKEY);
    g9_destroy(h);
}

TEST(gap9_lwa_each_output_is_optional) {
    REQUIRE(UN(NtUserGetLayeredWindowAttributes)); REQUIRE(U(SetLayeredWindowAttributes));
    HWND h = layered_window();
    if (!h) { t_skip("no window"); return; }
    CHECK(U(SetLayeredWindowAttributes)(h, 0x00010203, 0x66, LWA_ALPHA | LWA_COLORKEY));
    COLORREF k = 0x5A5A5A5A; BYTE a = 0x5B; DWORD f = 0x5C5C5C5C;
    CHECK(UN(NtUserGetLayeredWindowAttributes)(h, NULL, NULL, NULL));          /* all optional */
    CHECK(UN(NtUserGetLayeredWindowAttributes)(h, &k, NULL, NULL)); EXPECT_EQ(k, 0x00010203); EXPECT_EQ(a, 0x5B); EXPECT_EQ(f, 0x5C5C5C5C);
    k = 0x5A5A5A5A;
    CHECK(UN(NtUserGetLayeredWindowAttributes)(h, NULL, &a, NULL)); EXPECT_EQ(a, 0x66); EXPECT_EQ(k, 0x5A5A5A5A); EXPECT_EQ(f, 0x5C5C5C5C);
    a = 0x5B;
    CHECK(UN(NtUserGetLayeredWindowAttributes)(h, NULL, NULL, &f)); EXPECT_EQ(f, 3); EXPECT_EQ(a, 0x5B); EXPECT_EQ(k, 0x5A5A5A5A);
    g9_destroy(h);
}

TEST(gap9_lwa_alpha_is_a_single_byte_store) {
    REQUIRE(UN(NtUserGetLayeredWindowAttributes)); REQUIRE(U(SetLayeredWindowAttributes));
    HWND h = layered_window();
    if (!h) { t_skip("no window"); return; }
    CHECK(U(SetLayeredWindowAttributes)(h, 0, 0x99, LWA_ALPHA));
    BYTE buf[8]; memset(buf, 0xEE, sizeof buf);
    CHECK(UN(NtUserGetLayeredWindowAttributes)(h, NULL, buf + 3, NULL));
    EXPECT_EQ(buf[3], 0x99);
    CHECK(buf[0] == 0xEE && buf[1] == 0xEE && buf[2] == 0xEE && buf[4] == 0xEE && buf[7] == 0xEE);
    g9_destroy(h);
}

TEST(gap9_lwa_alpha_only_keeps_alpha_when_changing_to_colorkey) {
    REQUIRE(UN(NtUserGetLayeredWindowAttributes)); REQUIRE(U(SetLayeredWindowAttributes));
    HWND h = layered_window();
    if (!h) { t_skip("no window"); return; }
    CHECK(U(SetLayeredWindowAttributes)(h, 0, 0x30, LWA_ALPHA));
    CHECK(U(SetLayeredWindowAttributes)(h, 0x00112233, 0x77, LWA_COLORKEY));
    COLORREF k = 0; BYTE a = 0; DWORD f = 0;
    CHECK(UN(NtUserGetLayeredWindowAttributes)(h, &k, &a, &f));
    EXPECT_EQ(f, LWA_COLORKEY); EXPECT_EQ(k, 0x00112233);
    t_note("alpha after colorkey-only set: 0x%x (set value was 0x77, previous 0x30)", a);
    g9_destroy(h);
}

static DWORD thr_lwa(void *p) {
    COLORREF k = 0; BYTE a = 0; DWORD f = 0;
    if (!UN(NtUserGetLayeredWindowAttributes)((HWND)p, &k, &a, &f)) return 0;
    return (k == 0x00BEEF01 && a == 0xA5 && f == 3) ? 1 : 2;
}
TEST(gap9_lwa_read_from_other_thread) {
    REQUIRE(UN(NtUserGetLayeredWindowAttributes)); REQUIRE(U(SetLayeredWindowAttributes));
    HWND h = layered_window();
    if (!h) { t_skip("no window"); return; }
    CHECK(U(SetLayeredWindowAttributes)(h, 0x00BEEF01, 0xA5, LWA_ALPHA | LWA_COLORKEY));
    EXPECT_EQ(g9_on_thread(thr_lwa, h), 1);                /* the query has no thread or ownership check */
    g9_destroy(h);
}

TEST(gap9_lwa_after_clearing_layered_style) {
    REQUIRE(UN(NtUserGetLayeredWindowAttributes)); REQUIRE(U(SetLayeredWindowAttributes));
    REQUIRE(U(SetWindowLongPtrW)); REQUIRE(U(GetWindowLongPtrW));
    HWND h = layered_window();
    if (!h) { t_skip("no window"); return; }
    CHECK(U(SetLayeredWindowAttributes)(h, 0, 0x10, LWA_ALPHA));
    LONG_PTR ex = U(GetWindowLongPtrW)(h, GWL_EXSTYLE);
    U(SetWindowLongPtrW)(h, GWL_EXSTYLE, ex & ~WS_EX_LAYERED);
    BYTE a = 0;
    CHECK(!UN(NtUserGetLayeredWindowAttributes)(h, NULL, &a, NULL));
    g9_destroy(h);
}

TEST(gap9_lwa_user32_matches_syscall) {
    REQUIRE(U(GetLayeredWindowAttributes)); REQUIRE(UN(NtUserGetLayeredWindowAttributes)); REQUIRE(U(SetLayeredWindowAttributes));
    HWND h = layered_window();
    if (!h) { t_skip("no window"); return; }
    CHECK(U(SetLayeredWindowAttributes)(h, 0x00404040, 0x42, LWA_ALPHA | LWA_COLORKEY));
    COLORREF k1 = 0, k2 = 0; BYTE a1 = 0, a2 = 0; DWORD f1 = 0, f2 = 0;
    CHECK(U(GetLayeredWindowAttributes)(h, &k1, &a1, &f1));
    CHECK(UN(NtUserGetLayeredWindowAttributes)(h, &k2, &a2, &f2));
    CHECK(k1 == k2 && a1 == a2 && f1 == f2);
    g9_destroy(h);
}

/* ---- per-window DPI record -------------------------------------------------------------------- */
TEST(gap9_dpi_info_writes_exactly_0x60_bytes) {
    REQUIRE(UN(NtUserGetCurrentDpiInfoForWindow));
    HWND h = g9_window(0);
    if (!h) { t_skip("no window"); return; }
    BYTE buf[0x100];
    memset(buf, 0xA7, sizeof buf);
    BOOL r = UN(NtUserGetCurrentDpiInfoForWindow)(h, buf);
    CHECK(r);
    if (r) {
        int allsent = 1;
        for (int i = 0; i < 0x60; i++) if (buf[i] != 0xA7) { allsent = 0; break; }
        CHECK(!allsent);
        for (int i = 0x60; i < 0x100; i++) if (buf[i] != 0xA7) { CHECKF(0, "byte 0x%x past the record was written", i); break; }
    }
    g9_destroy(h);
}

TEST(gap9_dpi_info_invalid_handle_leaves_buffer) {
    REQUIRE(UN(NtUserGetCurrentDpiInfoForWindow));
    BYTE buf[0x80];
    memset(buf, 0x3C, sizeof buf);
    SetLastError(0);
    CHECK(!UN(NtUserGetCurrentDpiInfoForWindow)((HWND)(ULONG_PTR)0x4242, buf));
    EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
    for (int i = 0; i < 0x80; i++) if (buf[i] != 0x3C) { CHECKF(0, "buffer byte 0x%x changed", i); break; }
    SetLastError(0);
    CHECK(!UN(NtUserGetCurrentDpiInfoForWindow)(NULL, buf));
    EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
}

TEST(gap9_dpi_info_is_stable_and_per_window) {
    REQUIRE(UN(NtUserGetCurrentDpiInfoForWindow));
    HWND a = g9_window(0), b = g9_window(0);
    if (!a || !b) { t_skip("no window"); g9_destroy(a); g9_destroy(b); return; }
    BYTE x[0x60], y[0x60], z[0x60];
    CHECK(UN(NtUserGetCurrentDpiInfoForWindow)(a, x));
    CHECK(UN(NtUserGetCurrentDpiInfoForWindow)(a, y));
    CHECK(!memcmp(x, y, 0x60));
    CHECK(UN(NtUserGetCurrentDpiInfoForWindow)(b, z));
    /* two top-level windows of one thread on one monitor share the DPI record content */
    CHECK(!memcmp(x, z, 0x60));
    g9_destroy(a); g9_destroy(b);
}

TEST(gap9_dpi_info_contains_window_dpi) {
    REQUIRE(UN(NtUserGetCurrentDpiInfoForWindow)); REQUIRE(U(GetDpiForWindow));
    HWND h = g9_window(0);
    if (!h) { t_skip("no window"); return; }
    BYTE buf[0x60];
    CHECK(UN(NtUserGetCurrentDpiInfoForWindow)(h, buf));
    UINT dpi = U(GetDpiForWindow)(h);
    int found = -1;
    for (int i = 0; i + 4 <= 0x60; i += 4) { UINT v; memcpy(&v, buf + i, 4); if (v == dpi) { found = i; break; } }
    t_note("GetDpiForWindow=%u, first matching dword in record at offset %d", dpi, found);
    g9_destroy(h);
}

/* ---- physical -> logical point -------------------------------------------------------------- */
TEST(gap9_physpt_invalid_window) {
    REQUIRE(UN(NtUserPerMonitorDPIPhysicalToLogicalPoint));
    POINT p = { 123, 456 };
    SetLastError(0);
    CHECK(!UN(NtUserPerMonitorDPIPhysicalToLogicalPoint)((HWND)(ULONG_PTR)0x9999, &p));
    EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
    CHECK(p.x == 123 && p.y == 456);
}

TEST(gap9_physpt_null_window_always_converts) {
    REQUIRE(UN(NtUserPerMonitorDPIPhysicalToLogicalPoint));
    POINT pts[] = { {0, 0}, {10, 10}, {-5, -5}, {100000, 100000}, {-32000, 32000} };
    for (int i = 0; i < G9_N(pts); i++) {
        POINT p = pts[i];
        CHECKF(UN(NtUserPerMonitorDPIPhysicalToLogicalPoint)(NULL, &p), "pt %d,%d", pts[i].x, pts[i].y);  /* no rect test without a window */
    }
}

TEST(gap9_physpt_point_outside_window_is_rejected) {
    REQUIRE(UN(NtUserPerMonitorDPIPhysicalToLogicalPoint));
    HWND h = g9_window(0);
    if (!h) { t_skip("no window"); return; }
    POINT p = { -30000, -30000 };
    CHECK(!UN(NtUserPerMonitorDPIPhysicalToLogicalPoint)(h, &p));   /* DCEPtInRect fails -> *pt untouched */
    CHECK(p.x == -30000 && p.y == -30000);
    g9_destroy(h);
}

TEST(gap9_physpt_inside_window_at_100_percent) {
    REQUIRE(UN(NtUserPerMonitorDPIPhysicalToLogicalPoint)); REQUIRE(U(GetWindowRect)); REQUIRE(U(GetDpiForSystem)); REQUIRE(U(GetDpiForWindow));
    HWND h = g9_window(0);
    if (!h) { t_skip("no window"); return; }
    RECT r; CHECK(U(GetWindowRect)(h, &r));
    POINT p = { (r.left + r.right) / 2, (r.top + r.bottom) / 2 }, q = p;
    BOOL ok = UN(NtUserPerMonitorDPIPhysicalToLogicalPoint)(h, &q);
    if (U(GetDpiForSystem)() == 96 && U(GetDpiForWindow)(h) == 96) {
        CHECK(ok);                                         /* identity conversion keeps the point inside the rect */
        CHECK(q.x == p.x && q.y == p.y);
    } else {
        t_note("scaled desktop (system dpi %u): ok=%d (%ld,%ld)->(%ld,%ld)", U(GetDpiForSystem)(), ok, p.x, p.y, q.x, q.y);
    }
    g9_destroy(h);
}

TEST(gap9_physpt_user32_wrapper) {
    REQUIRE(U(PhysicalToLogicalPointForPerMonitorDPI));
    POINT p = { 5, 5 };
    SetLastError(0);
    CHECK(!U(PhysicalToLogicalPointForPerMonitorDPI)((HWND)(ULONG_PTR)0x9999, &p));
    EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
    CHECK(U(PhysicalToLogicalPointForPerMonitorDPI)(NULL, &p));
}

/* ---- SystemParametersInfoForDpi ----------------------------------------------------------------- */
#define SPIF_NCM 0x29
#define SPIF_ICONFONT 0x1F
#define SPIF_ICONMETRICS 0x2D

TEST(gap9_spidpi_rejects_bad_arguments) {
    REQUIRE(UN(NtUserSystemParametersInfoForDpi));
    NONCLIENTMETRICSW ncm;
    LOGFONTW lf;
    ICONMETRICSW im;
    struct { UINT a, p; void *pv; UINT dpi; const char *n; } t[] = {
        { SPIF_NCM, sizeof ncm, &ncm, 0, "dpi 0" },
        { SPIF_NCM, sizeof ncm, NULL, 96, "NULL pvParam" },
        { SPIF_NCM, sizeof ncm - 1, &ncm, 96, "size-1" },
        { SPIF_NCM, sizeof ncm + 1, &ncm, 96, "size+1" },
        { SPIF_NCM, 0, &ncm, 96, "size 0" },
        { SPIF_ICONFONT, sizeof lf + 4, &lf, 96, "iconfont size+4" },
        { SPIF_ICONFONT, 0, &lf, 96, "iconfont size 0" },
        { SPIF_ICONMETRICS, sizeof im - 4, &im, 96, "iconmetrics size-4" },
        { SPIF_ICONMETRICS, sizeof lf, &im, 96, "iconmetrics given LOGFONT size" },
        { SPIF_NCM, sizeof lf, &ncm, 96, "ncm given LOGFONT size" },
    };
    for (int i = 0; i < G9_N(t); i++) {
        memset(&ncm, 0x6D, sizeof ncm); memset(&lf, 0x6D, sizeof lf); memset(&im, 0x6D, sizeof im);
        SetLastError(0);
        CHECKF(!UN(NtUserSystemParametersInfoForDpi)(t[i].a, t[i].p, t[i].pv, 0, t[i].dpi), "%s", t[i].n);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
        if (t[i].pv == &ncm) { CHECKF(((BYTE *)&ncm)[0] == 0x6D && ((BYTE *)&ncm)[sizeof ncm - 1] == 0x6D, "%s: buffer touched", t[i].n); }
    }
}

TEST(gap9_spidpi_rejects_other_actions) {
    REQUIRE(UN(NtUserSystemParametersInfoForDpi));
    /* only the three read actions are accepted; setters and every other getter fail up front with 0x57 */
    static const UINT acts[] = { SPI_GETBEEP, SPI_SETBEEP, SPI_GETWORKAREA, SPI_SETNONCLIENTMETRICS, SPI_SETICONMETRICS,
                                 SPI_SETICONTITLELOGFONT, SPI_GETMOUSE, SPI_GETWHEELSCROLLLINES, 0, 0xFFFF };
    BYTE buf[512];
    for (int i = 0; i < G9_N(acts); i++) {
        memset(buf, 0, sizeof buf);
        SetLastError(0);
        CHECKF(!UN(NtUserSystemParametersInfoForDpi)(acts[i], sizeof(NONCLIENTMETRICSW), buf, 0, 96), "action 0x%x", acts[i]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
}

static int get_ncm(UINT dpi, NONCLIENTMETRICSW *n) {
    memset(n, 0, sizeof *n);
    n->cbSize = sizeof *n;
    return UN(NtUserSystemParametersInfoForDpi)(SPIF_NCM, sizeof *n, n, 0, dpi);
}

TEST(gap9_spidpi_ncm_at_several_dpis) {
    REQUIRE(UN(NtUserSystemParametersInfoForDpi));
    static const UINT dpis[] = { 96, 120, 144, 192, 288 };
    NONCLIENTMETRICSW prev; int have = 0;
    for (int i = 0; i < G9_N(dpis); i++) {
        NONCLIENTMETRICSW n;
        BOOL ok = get_ncm(dpis[i], &n);
        CHECKF(ok, "dpi %u err %lu", dpis[i], GetLastError());
        if (!ok) continue;
        EXPECT_EQ(n.cbSize, sizeof n);
        CHECK(n.iBorderWidth > 0 && n.iScrollWidth > 0 && n.iCaptionHeight > 0 && n.iMenuHeight > 0);
        if (have) {                                         /* metrics never shrink as the DPI grows */
            CHECKF(n.iScrollWidth >= prev.iScrollWidth, "scroll %d -> %d at dpi %u", prev.iScrollWidth, n.iScrollWidth, dpis[i]);
            CHECKF(n.iCaptionHeight >= prev.iCaptionHeight, "caption at dpi %u", dpis[i]);
            CHECKF(n.iMenuHeight >= prev.iMenuHeight, "menu at dpi %u", dpis[i]);
        }
        t_note("dpi %u: border %d scroll %d caption %d menu %d font %ld", dpis[i], n.iBorderWidth, n.iScrollWidth,
               n.iCaptionHeight, n.iMenuHeight, n.lfCaptionFont.lfHeight);
        prev = n; have = 1;
    }
}

TEST(gap9_spidpi_ncm_deterministic) {
    REQUIRE(UN(NtUserSystemParametersInfoForDpi));
    NONCLIENTMETRICSW a, b;
    CHECK(get_ncm(144, &a)); CHECK(get_ncm(144, &b));
    CHECK(!memcmp(&a, &b, sizeof a));
}

TEST(gap9_spidpi_ignores_winini_flags) {
    REQUIRE(UN(NtUserSystemParametersInfoForDpi));
    NONCLIENTMETRICSW a, b;
    CHECK(get_ncm(96, &a));
    memset(&b, 0, sizeof b); b.cbSize = sizeof b;
    CHECK(UN(NtUserSystemParametersInfoForDpi)(SPIF_NCM, sizeof b, &b, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE, 96));
    CHECK(!memcmp(&a, &b, sizeof a));
}

TEST(gap9_spidpi_iconmetrics_and_iconfont) {
    REQUIRE(UN(NtUserSystemParametersInfoForDpi));
    ICONMETRICSW lo, hi;
    memset(&lo, 0, sizeof lo); lo.cbSize = sizeof lo;
    memset(&hi, 0, sizeof hi); hi.cbSize = sizeof hi;
    CHECK(UN(NtUserSystemParametersInfoForDpi)(SPIF_ICONMETRICS, sizeof lo, &lo, 0, 96));
    CHECK(UN(NtUserSystemParametersInfoForDpi)(SPIF_ICONMETRICS, sizeof hi, &hi, 0, 192));
    EXPECT_EQ(lo.cbSize, sizeof lo);
    CHECK(hi.iHorzSpacing >= lo.iHorzSpacing && hi.iVertSpacing >= lo.iVertSpacing);
    t_note("icon spacing 96dpi %d/%d, 192dpi %d/%d", lo.iHorzSpacing, lo.iVertSpacing, hi.iHorzSpacing, hi.iVertSpacing);
    LOGFONTW f96, f192;
    memset(&f96, 0, sizeof f96); memset(&f192, 0, sizeof f192);
    CHECK(UN(NtUserSystemParametersInfoForDpi)(SPIF_ICONFONT, sizeof f96, &f96, 0, 96));
    CHECK(UN(NtUserSystemParametersInfoForDpi)(SPIF_ICONFONT, sizeof f192, &f192, 0, 192));
    CHECK(f96.lfFaceName[0] != 0);
    CHECK(!wcscmp(f96.lfFaceName, f192.lfFaceName));        /* only the size is DPI dependent */
    CHECK(labs(f192.lfHeight) >= labs(f96.lfHeight));
    t_note("icon font %ls height %ld -> %ld", f96.lfFaceName, f96.lfHeight, f192.lfHeight);
}

TEST(gap9_spidpi_user32_export_matches_syscall) {
    REQUIRE(U(SystemParametersInfoForDpi)); REQUIRE(UN(NtUserSystemParametersInfoForDpi));
    NONCLIENTMETRICSW a, b;
    CHECK(get_ncm(120, &a));
    memset(&b, 0, sizeof b); b.cbSize = sizeof b;
    CHECK(U(SystemParametersInfoForDpi)(SPIF_NCM, sizeof b, &b, 0, 120));
    /* Observed on Windows 10: the user32 export and the raw syscall agree on every field except iCaptionWidth (user32 returns its
     * own value there), so compare the structure with that field set aside */
    NONCLIENTMETRICSW a2 = a, b2 = b;
    a2.iCaptionWidth = b2.iCaptionWidth = 0;
    CHECK(!memcmp(&a2, &b2, sizeof a2));
    CHECK(b.iCaptionWidth > 0);
    t_note("caption width at 120 dpi: syscall %d, user32 %d", a.iCaptionWidth, b.iCaptionWidth);
}
