/* EnableScrollBar / ShowScrollBar / GetScrollBarInfo.
 * Scroll bar enable/show/info behavior. */
#include "sib.h"

#define STATE_UNAVAILABLE 0x00000001
#define STATE_INVISIBLE   0x00008000
#define STATE_OFFSCREEN   0x00010000
#define STATE_PRESSED     0x00000008

static int dpi96(void) {
    UINT (WINAPI *f)(void) = (UINT (WINAPI *)(void))t_proc("GetDpiForSystem");
    return !f || f() == 96;
}
/* Pixel-metric equalities hold exactly only when the process metric cache and window DPI agree (96). */
#define CHECK_METRIC(c, ...) do { if (dpi96()) CHECKF(c, __VA_ARGS__); else if (!(c)) t_note("metric mismatch at non-96 system DPI: " #c); } while (0)

static DWORD style_of(HWND h) { return (DWORD)U(GetWindowLongW)(h, GWL_STYLE); }
static void setrange(HWND h, int bar, int mn, int mx, UINT pg, int pos) {
    SCROLLINFO si = { sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS, mn, mx, pg, pos, 0 };
    U(SetScrollInfo)(h, bar, &si, FALSE);
}
static int sbi(HWND h, LONG obj, SCROLLBARINFO *o) {
    memset(o, 0xCC, sizeof *o);
    o->cbSize = sizeof *o;
    return U(GetScrollBarInfo)(h, obj, o);
}
static int zero_rect(const RECT *r) { return !r->left && !r->top && !r->right && !r->bottom; }

/* ---- EnableScrollBar -------------------------------------------------------------------- */

TEST(sbe_bad_bar_flag) {
    REQUIRE(U(EnableScrollBar)); REQUIRE(U(CreateWindowExW));
    HWND h = sib_mk(WS_OVERLAPPEDWINDOW, 0, 200, 200, 0); SIB_NEED_WND(h);
    static const UINT bars[] = { 4, 5, 100, 0xffffffffu, 0x80000000u };
    for (unsigned i = 0; i < sizeof bars / sizeof *bars; i++) {
        SET_ERR(0);
        /* NtUserEnableScrollBar: wSBflags >= 4 => ERROR_INVALID_PARAMETER, return 0. */
        EQ(U(EnableScrollBar)(h, bars[i], ESB_DISABLE_BOTH), 0);
        EXPECT_ERR(E_INVALID_PARAM);
    }
    U(DestroyWindow)(h);
}

TEST(sbe_ctl_on_non_scrollbar_window) {
    REQUIRE(U(EnableScrollBar)); REQUIRE(U(CreateWindowExW));
    HWND p = sib_mk(WS_OVERLAPPEDWINDOW, 0, 200, 200, 0); SIB_NEED_WND(p);
    HWND st = sib_child(p, L"Static", 0, 0, 0, 40, 20);
    HWND wins[] = { p, st };
    for (unsigned i = 0; i < 2; i++) {
        if (!wins[i]) continue;
        SET_ERR(0);
        /* SB_CTL requires the target's fnid to be FNID_SCROLLBAR (0x29a). */
        EQ(U(EnableScrollBar)(wins[i], SB_CTL, ESB_DISABLE_BOTH), 0);
        EXPECT_ERR(E_INVALID_PARAM);
        CHECK(U(IsWindowEnabled)(wins[i]));
    }
    U(DestroyWindow)(p);
}

TEST(sbe_invalid_hwnd) {
    REQUIRE(U(EnableScrollBar)); REQUIRE(U(CreateWindowExW));
    HWND gone = sib_mk(WS_OVERLAPPEDWINDOW, 0, 100, 100, 0); SIB_NEED_WND(gone);
    U(DestroyWindow)(gone);
    HWND bad[] = { NULL, (HWND)(ULONG_PTR)0x44, (HWND)(ULONG_PTR)-1, gone };
    for (unsigned i = 0; i < 4; i++) {
        SET_ERR(0);
        EQ(U(EnableScrollBar)(bad[i], SB_HORZ, ESB_DISABLE_BOTH), 0);
        EXPECT_ERR(E_INVALID_HWND);
        SET_ERR(0);
        EQ(U(ShowScrollBar)(bad[i], SB_HORZ, TRUE), 0);
        EXPECT_ERR(E_INVALID_HWND);
    }
}

TEST(sbe_desktop_ignored) {
    REQUIRE(U(EnableScrollBar)); REQUIRE(U(ShowScrollBar)); REQUIRE(U(GetDesktopWindow));
    HWND d = U(GetDesktopWindow)();
    SET_ERR(0);
    /* FNID_DESKTOP is filtered by the same (fnid & 0xffff2fff) - 0x29d & ~2 test: 0, no last error. */
    EQ(U(EnableScrollBar)(d, SB_VERT, ESB_ENABLE_BOTH), 0);
    EXPECT_ERR(0);
    SET_ERR(0);
    EQ(U(ShowScrollBar)(d, SB_VERT, TRUE), 0);
    EXPECT_ERR(0);
}

TEST(sbe_arrow_flags_state) {
    REQUIRE(U(EnableScrollBar)); REQUIRE(U(CreateWindowExW));
    if (t_is_wine()) { t_skip("no tagSBINFO under Wine"); return; }
    HWND h = sib_mk(WS_OVERLAPPEDWINDOW, 0, 200, 200, 0); SIB_NEED_WND(h);
    static const struct { int bar; UINT arrows; unsigned flags; } t[] = {
        { SB_HORZ, ESB_DISABLE_LTUP, 0x1 }, { SB_HORZ, ESB_DISABLE_RTDN, 0x2 }, { SB_HORZ, ESB_DISABLE_BOTH, 0x3 }, { SB_HORZ, ESB_ENABLE_BOTH, 0x0 },
        { SB_VERT, ESB_DISABLE_LTUP, 0x4 }, { SB_VERT, ESB_DISABLE_RTDN, 0x8 }, { SB_VERT, ESB_DISABLE_BOTH, 0xC }, { SB_VERT, ESB_ENABLE_BOTH, 0x0 },
    };
    for (unsigned i = 0; i < sizeof t / sizeof *t; i++) {
        /* disabling is cumulative (a second EnableScrollBar adds to the disabled arrows), so start each row clean */
        U(EnableScrollBar)(h, t[i].bar, ESB_ENABLE_BOTH);
        U(EnableScrollBar)(h, t[i].bar, t[i].arrows);
        struct sb_info sb; int pres = 0;
        if (!sib_read_sbinfo(h, &sb, &pres)) { t_skip("tagWND client view unavailable"); break; }
        CHECK(pres);
        /* Horizontal arrow bits in WSBflags[1:0], vertical in [3:2]. */
        CHECKF((sb.flags & 0xF) == t[i].flags, "bar %d arrows %u -> WSBflags %#x want %#x", t[i].bar, t[i].arrows, sb.flags, t[i].flags);
    }
    U(DestroyWindow)(h);
}

TEST(sbe_horz_vert_independent) {
    REQUIRE(U(EnableScrollBar)); REQUIRE(U(CreateWindowExW));
    if (t_is_wine()) { t_skip("no tagSBINFO under Wine"); return; }
    HWND h = sib_mk(WS_OVERLAPPEDWINDOW, 0, 200, 200, 0); SIB_NEED_WND(h);
    U(EnableScrollBar)(h, SB_HORZ, ESB_DISABLE_LTUP);
    U(EnableScrollBar)(h, SB_VERT, ESB_DISABLE_RTDN);
    struct sb_info sb; int pres = 0;
    if (!sib_read_sbinfo(h, &sb, &pres)) { t_skip("tagWND client view unavailable"); U(DestroyWindow)(h); return; }
    CHECKF((sb.flags & 0xF) == 0x9, "WSBflags %#x", sb.flags);
    U(EnableScrollBar)(h, SB_HORZ, ESB_ENABLE_BOTH);
    CHECK(sib_read_sbinfo(h, &sb, &pres) && (sb.flags & 0xF) == 0x8);
    U(DestroyWindow)(h);
}

TEST(sbe_return_value_tracks_change) {
    REQUIRE(U(EnableScrollBar)); REQUIRE(U(CreateWindowExW));
    HWND h = sib_mk(WS_OVERLAPPEDWINDOW, 0, 200, 200, 0); SIB_NEED_WND(h);
    for (int bar = SB_HORZ; bar <= SB_VERT; bar++) {
        /* EnableScrollBar returns 1 only when the stored arrow flags changed. */
        EQ(U(EnableScrollBar)(h, bar, ESB_DISABLE_BOTH), 1);
        EQ(U(EnableScrollBar)(h, bar, ESB_DISABLE_BOTH), 0);
        EQ(U(EnableScrollBar)(h, bar, ESB_ENABLE_BOTH), 1);
        EQ(U(EnableScrollBar)(h, bar, ESB_ENABLE_BOTH), 0);
        EQ(U(EnableScrollBar)(h, bar, ESB_DISABLE_LTUP), 1);
        EQ(U(EnableScrollBar)(h, bar, ESB_DISABLE_RTDN), 1);
        EQ(U(EnableScrollBar)(h, bar, ESB_DISABLE_RTDN), 0);
        EQ(U(EnableScrollBar)(h, bar, ESB_ENABLE_BOTH), 1);
    }
    U(DestroyWindow)(h);
}

TEST(sbe_enable_without_sbinfo_is_noop) {
    REQUIRE(U(EnableScrollBar)); REQUIRE(U(CreateWindowExW));
    HWND h = sib_mk(WS_OVERLAPPED, 0, 200, 200, 0); SIB_NEED_WND(h);
    /* No SBINFO and nothing to disable: EnableScrollBar returns 0 before _InitPwSB. */
    EQ(U(EnableScrollBar)(h, SB_HORZ, ESB_ENABLE_BOTH), 0);
    EQ(U(EnableScrollBar)(h, SB_VERT, ESB_ENABLE_BOTH), 0);
    struct sb_info sb; int pres = 1;
    if (sib_read_sbinfo(h, &sb, &pres)) CHECK(!pres);
    /* A real disable allocates the SBINFO. */
    EQ(U(EnableScrollBar)(h, SB_HORZ, ESB_DISABLE_BOTH), 1);
    if (sib_read_sbinfo(h, &sb, &pres)) CHECK(pres);
    U(DestroyWindow)(h);
}

TEST(sbe_sb_both) {
    REQUIRE(U(EnableScrollBar)); REQUIRE(U(CreateWindowExW));
    if (t_is_wine()) { t_skip("no tagSBINFO under Wine"); return; }
    HWND h = sib_mk(WS_OVERLAPPEDWINDOW, 0, 200, 200, 0); SIB_NEED_WND(h);
    EQ(U(EnableScrollBar)(h, SB_BOTH, ESB_DISABLE_BOTH), 1);
    struct sb_info sb; int pres = 0;
    if (sib_read_sbinfo(h, &sb, &pres)) CHECKF((sb.flags & 0xF) == 0xF, "WSBflags %#x", sb.flags);
    EQ(U(EnableScrollBar)(h, SB_BOTH, ESB_DISABLE_BOTH), 0);
    EQ(U(EnableScrollBar)(h, SB_BOTH, ESB_ENABLE_BOTH), 1);
    if (sib_read_sbinfo(h, &sb, &pres)) CHECKF((sb.flags & 0xF) == 0, "WSBflags %#x", sb.flags);
    U(DestroyWindow)(h);
}

TEST(sbe_ctl_disable_enable) {
    REQUIRE(U(EnableScrollBar)); REQUIRE(U(CreateWindowExW)); REQUIRE(U(IsWindowEnabled));
    HWND p = sib_mk(WS_OVERLAPPEDWINDOW, 0, 300, 300, 0); SIB_NEED_WND(p);
    HWND c = sib_child(p, L"ScrollBar", SBS_VERT, 0, 0, 20, 100);
    if (!c) { t_skip("ScrollBar control"); U(DestroyWindow)(p); return; }
    CHECK(U(IsWindowEnabled)(c));
    /* EnableScrollBar(SB_CTL): ESB_DISABLE_BOTH disables the whole control (as if EnableWindow(FALSE)); changed => nonzero. */
    CHECK(U(EnableScrollBar)(c, SB_CTL, ESB_DISABLE_BOTH) != 0);
    CHECK(!U(IsWindowEnabled)(c));
    CHECK(U(GetWindowLongW)(c, GWL_STYLE) & WS_DISABLED);
    /* Same flags again: (flags & 3) == wArrows => 0. */
    EQ(U(EnableScrollBar)(c, SB_CTL, ESB_DISABLE_BOTH), 0);
    CHECK(U(EnableScrollBar)(c, SB_CTL, ESB_ENABLE_BOTH) != 0);
    CHECK(U(IsWindowEnabled)(c));
    CHECK(!(U(GetWindowLongW)(c, GWL_STYLE) & WS_DISABLED));
    U(DestroyWindow)(p);
}

/* ---- ShowScrollBar ----------------------------------------------------------------------- */

TEST(sbs_bad_bar) {
    REQUIRE(U(ShowScrollBar)); REQUIRE(U(CreateWindowExW));
    HWND h = sib_mk(WS_OVERLAPPEDWINDOW, 0, 200, 200, 0); SIB_NEED_WND(h);
    static const int bars[] = { 4, 7, 100, -1, (int)0x80000000 };
    for (unsigned i = 0; i < sizeof bars / sizeof *bars; i++) {
        SET_ERR(0);
        /* NtUserShowScrollBar: nBar unsigned >= 4 => ERROR_INVALID_PARAMETER, 0. */
        EQ(U(ShowScrollBar)(h, bars[i], TRUE), 0);
        EXPECT_ERR(E_INVALID_PARAM);
    }
    CHECK(!(style_of(h) & (WS_HSCROLL | WS_VSCROLL)));
    U(DestroyWindow)(h);
}

TEST(sbs_show_hide_style_bits) {
    REQUIRE(U(ShowScrollBar)); REQUIRE(U(GetWindowLongW)); REQUIRE(U(CreateWindowExW));
    static const struct { int bar; DWORD bits; } t[] = {
        { SB_HORZ, WS_HSCROLL }, { SB_VERT, WS_VSCROLL }, { SB_BOTH, WS_HSCROLL | WS_VSCROLL },
    };
    for (unsigned i = 0; i < sizeof t / sizeof *t; i++) {
        HWND h = sib_mk(WS_OVERLAPPED, 0, 200, 200, 0); SIB_NEED_WND(h);
        /* ShowScrollBar maps 0 -> WS_HSCROLL, 1 -> WS_VSCROLL, 3 -> both; always returns 1. */
        EQ(U(ShowScrollBar)(h, t[i].bar, TRUE), 1);
        CHECKF((style_of(h) & (WS_HSCROLL | WS_VSCROLL)) == t[i].bits, "bar %d show -> %#lx", t[i].bar, style_of(h) & (WS_HSCROLL | WS_VSCROLL));
        EQ(U(ShowScrollBar)(h, t[i].bar, TRUE), 1);     /* already shown: still 1, no change */
        CHECK((style_of(h) & (WS_HSCROLL | WS_VSCROLL)) == t[i].bits);
        EQ(U(ShowScrollBar)(h, t[i].bar, FALSE), 1);
        CHECK(!(style_of(h) & (WS_HSCROLL | WS_VSCROLL)));
        EQ(U(ShowScrollBar)(h, t[i].bar, FALSE), 1);    /* already hidden */
        U(DestroyWindow)(h);
    }
}

TEST(sbs_both_hide_clears_each) {
    REQUIRE(U(ShowScrollBar)); REQUIRE(U(GetWindowLongW)); REQUIRE(U(CreateWindowExW));
    HWND h = sib_mk(WS_OVERLAPPED, 0, 200, 200, 0); SIB_NEED_WND(h);
    U(ShowScrollBar)(h, SB_HORZ, TRUE);
    CHECK((style_of(h) & (WS_HSCROLL | WS_VSCROLL)) == WS_HSCROLL);
    U(ShowScrollBar)(h, SB_BOTH, TRUE);
    CHECK((style_of(h) & (WS_HSCROLL | WS_VSCROLL)) == (WS_HSCROLL | WS_VSCROLL));
    U(ShowScrollBar)(h, SB_VERT, FALSE);
    CHECK((style_of(h) & (WS_HSCROLL | WS_VSCROLL)) == WS_HSCROLL);
    U(ShowScrollBar)(h, SB_BOTH, FALSE);
    CHECK(!(style_of(h) & (WS_HSCROLL | WS_VSCROLL)));
    U(DestroyWindow)(h);
}

TEST(sbs_show_nonzero_is_true) {
    REQUIRE(U(ShowScrollBar)); REQUIRE(U(GetWindowLongW)); REQUIRE(U(CreateWindowExW));
    static const BOOL vals[] = { 1, 2, -1, 0x100, (BOOL)0x80000000 };
    for (unsigned i = 0; i < sizeof vals / sizeof *vals; i++) {
        HWND h = sib_mk(WS_OVERLAPPED, 0, 200, 200, 0); SIB_NEED_WND(h);
        U(ShowScrollBar)(h, SB_VERT, vals[i]);
        CHECKF(style_of(h) & WS_VSCROLL, "bShow %#x", vals[i]);
        U(DestroyWindow)(h);
    }
}

TEST(sbs_show_allocates_sbinfo) {
    REQUIRE(U(ShowScrollBar)); REQUIRE(U(GetScrollRange)); REQUIRE(U(GetScrollPos)); REQUIRE(U(CreateWindowExW));
    HWND h = sib_mk(WS_OVERLAPPED, 0, 200, 200, 0); SIB_NEED_WND(h);
    int mn = 5, mx = 5;
    SET_ERR(0);
    CHECK(U(GetScrollRange)(h, SB_VERT, &mn, &mx));
    EXPECT_ERR(E_NO_SCROLLBARS);
    U(ShowScrollBar)(h, SB_VERT, TRUE);          /* ShowScrollBar creates the scroll bar info when absent */
    SET_ERR(0);
    mn = mx = 5;
    CHECK(U(GetScrollRange)(h, SB_VERT, &mn, &mx));
    EXPECT_ERR(0);
    CHECK(mn == 0 && mx == 100);       /* a freshly allocated SBINFO starts with the default range 0..100 */
    SET_ERR(0);
    EQ(U(GetScrollPos)(h, SB_VERT), 0);
    EXPECT_ERR(0);
    struct sb_info sb; int pres = 0;
    if (sib_read_sbinfo(h, &sb, &pres)) CHECK(pres && sb.flags == 0);
    U(DestroyWindow)(h);
}

TEST(sbs_hide_keeps_values) {
    REQUIRE(U(ShowScrollBar)); REQUIRE(U(SetScrollInfo)); REQUIRE(U(GetScrollInfo)); REQUIRE(U(CreateWindowExW));
    HWND h = sib_mk(WS_OVERLAPPED, 0, 200, 200, 0); SIB_NEED_WND(h);
    setrange(h, SB_VERT, 0, 100, 10, 33);
    CHECK(style_of(h) & WS_VSCROLL);
    U(ShowScrollBar)(h, SB_VERT, FALSE);
    CHECK(!(style_of(h) & WS_VSCROLL));
    /* Hiding only clears the style bit; pSBInfo and its SBDATA survive. */
    SCROLLINFO g = { sizeof g, SIF_ALL };
    CHECK(U(GetScrollInfo)(h, SB_VERT, &g));
    CHECK(g.nMin == 0 && g.nMax == 100 && g.nPage == 10 && g.nPos == 33);
    U(DestroyWindow)(h);
}

TEST(sbs_ctl_shows_and_hides_window) {
    REQUIRE(U(ShowScrollBar)); REQUIRE(U(GetWindowLongW)); REQUIRE(U(CreateWindowExW));
    HWND p = sib_mk(WS_OVERLAPPEDWINDOW, 0, 300, 300, 1); SIB_NEED_WND(p);
    HWND c = sib_child(p, L"ScrollBar", SBS_VERT, 0, 0, 20, 100);
    if (!c) { t_skip("ScrollBar control"); U(DestroyWindow)(p); return; }
    EQ(U(ShowScrollBar)(c, SB_CTL, TRUE), 1);       /* ShowScrollBar(SB_CTL) shows the control window */
    CHECK(U(GetWindowLongW)(c, GWL_STYLE) & WS_VISIBLE);
    EQ(U(ShowScrollBar)(c, SB_CTL, FALSE), 1);
    CHECK(!(U(GetWindowLongW)(c, GWL_STYLE) & WS_VISIBLE));
    /* SB_CTL is not restricted to scroll bar controls for ShowScrollBar. */
    HWND s = sib_child(p, L"Static", 0, 30, 0, 20, 20);
    if (s) {
        EQ(U(ShowScrollBar)(s, SB_CTL, TRUE), 1);
        CHECK(U(GetWindowLongW)(s, GWL_STYLE) & WS_VISIBLE);
        EQ(U(ShowScrollBar)(s, SB_CTL, FALSE), 1);
        CHECK(!(U(GetWindowLongW)(s, GWL_STYLE) & WS_VISIBLE));
    }
    U(DestroyWindow)(p);
}

TEST(sbs_client_area_shrinks) {
    REQUIRE(U(ShowScrollBar)); REQUIRE(U(GetClientRect)); REQUIRE(U(CreateWindowExW));
    HWND h = sib_mk(WS_OVERLAPPED | WS_CAPTION, 0, 300, 300, 1); SIB_NEED_WND(h);
    RECT c0, c1, c2, c3;
    CHECK(U(GetClientRect)(h, &c0));
    U(ShowScrollBar)(h, SB_VERT, TRUE);
    CHECK(U(GetClientRect)(h, &c1));
    /* The frame change recalculates the non-client area: the vertical bar takes SM_CXVSCROLL from the client width, height unchanged. */
    CHECK_METRIC((c0.right - c0.left) - (c1.right - c1.left) == U(GetSystemMetrics)(SM_CXVSCROLL),
                 "dx %ld vs %d", (c0.right - c0.left) - (c1.right - c1.left), U(GetSystemMetrics)(SM_CXVSCROLL));
    CHECK(c0.bottom - c0.top == c1.bottom - c1.top);
    U(ShowScrollBar)(h, SB_HORZ, TRUE);
    CHECK(U(GetClientRect)(h, &c2));
    CHECK_METRIC((c1.bottom - c1.top) - (c2.bottom - c2.top) == U(GetSystemMetrics)(SM_CYHSCROLL), "dy");
    CHECK(c1.right - c1.left == c2.right - c2.left);
    U(ShowScrollBar)(h, SB_BOTH, FALSE);
    CHECK(U(GetClientRect)(h, &c3));
    CHECK(c3.right == c0.right && c3.bottom == c0.bottom && c3.left == 0 && c3.top == 0);
    U(DestroyWindow)(h);
}

TEST(sbs_client_area_matches_desktop_heap) {
    REQUIRE(U(ShowScrollBar)); REQUIRE(U(GetWindowInfo)); REQUIRE(U(GetClientRect)); REQUIRE(U(CreateWindowExW));
    HWND h = sib_mk(WS_OVERLAPPED | WS_CAPTION, 0, 300, 300, 1); SIB_NEED_WND(h);
    U(ShowScrollBar)(h, SB_BOTH, TRUE);
    WINDOWINFO wi = { sizeof wi };
    RECT cr;
    CHECK(U(GetWindowInfo)(h, &wi) && U(GetClientRect)(h, &cr));
    /* rcClient in WINDOWINFO (tagWND.rcClient, screen coords) has the same extent GetClientRect reports. */
    CHECK(wi.rcClient.right - wi.rcClient.left == cr.right - cr.left);
    CHECK(wi.rcClient.bottom - wi.rcClient.top == cr.bottom - cr.top);
    U(DestroyWindow)(h);
}

/* ---- GetScrollBarInfo --------------------------------------------------------------------- */

TEST(sbi_bad_cbsize) {
    REQUIRE(U(GetScrollBarInfo)); REQUIRE(U(CreateWindowExW));
    HWND h = sib_mk(WS_OVERLAPPEDWINDOW, 0, 200, 200, 1); SIB_NEED_WND(h);
    static const DWORD sz[] = { 0, 4, 0x38, 0x3b, 0x3d, 0x40, 0x78, 0xffffffffu };
    for (unsigned i = 0; i < sizeof sz / sizeof *sz; i++) {
        SCROLLBARINFO o; memset(&o, 0, sizeof o); o.cbSize = sz[i];
        SET_ERR(0);
        /* GetScrollBarInfo: *pbi.cbSize != 0x3c => ERROR_INVALID_PARAMETER and FALSE. */
        CHECK(!U(GetScrollBarInfo)(h, OBJID_VSCROLL, &o));
        EXPECT_ERR(E_INVALID_PARAM);
    }
    U(DestroyWindow)(h);
}

TEST(sbi_bad_object_id) {
    REQUIRE(U(GetScrollBarInfo)); REQUIRE(U(CreateWindowExW));
    HWND h = sib_mk(WS_OVERLAPPEDWINDOW, 0, 200, 200, 1); SIB_NEED_WND(h);
    static const LONG ids[] = { 0, 1, 2, -1, -2, -3, -7, -8, 100, (LONG)0x80000000 };
    for (unsigned i = 0; i < sizeof ids / sizeof *ids; i++) {
        SCROLLBARINFO o;
        SET_ERR(0);
        /* Only OBJID_HSCROLL (-6), OBJID_VSCROLL (-5) and OBJID_CLIENT (-4, scroll bar controls) are accepted. */
        CHECK(!sbi(h, ids[i], &o));
        EXPECT_ERR(E_INVALID_PARAM);
    }
    U(DestroyWindow)(h);
}

TEST(sbi_invalid_hwnd) {
    REQUIRE(U(GetScrollBarInfo)); REQUIRE(U(CreateWindowExW));
    HWND gone = sib_mk(WS_OVERLAPPEDWINDOW, 0, 100, 100, 0); SIB_NEED_WND(gone);
    U(DestroyWindow)(gone);
    HWND bad[] = { NULL, (HWND)(ULONG_PTR)0x1234, (HWND)(ULONG_PTR)-1, gone };
    for (unsigned i = 0; i < 4; i++) {
        SCROLLBARINFO o;
        SET_ERR(0);
        CHECK(!sbi(bad[i], OBJID_VSCROLL, &o));
        EXPECT_ERR(E_INVALID_HWND);
    }
}

TEST(sbi_absent_bar_is_invisible) {
    REQUIRE(U(GetScrollBarInfo)); REQUIRE(U(CreateWindowExW));
    HWND h = sib_mk(WS_OVERLAPPEDWINDOW, 0, 200, 200, 1); SIB_NEED_WND(h);
    static const LONG ids[] = { OBJID_HSCROLL, OBJID_VSCROLL };
    for (unsigned i = 0; i < 2; i++) {
        SCROLLBARINFO o;
        /* No WS_xSCROLL: state[0] gets STATE_SYSTEM_INVISIBLE, everything else stays zero (kernel copy is memset). */
        CHECK(sbi(h, ids[i], &o));
        CHECK(o.cbSize == sizeof o);
        CHECKF(o.rgstate[0] == STATE_INVISIBLE, "rgstate[0] %#lx", o.rgstate[0]);
        for (int k = 1; k < 6; k++) CHECKF(o.rgstate[k] == 0, "rgstate[%d] %#lx", k, o.rgstate[k]);
        CHECK(zero_rect(&o.rcScrollBar));
        CHECK(o.dxyLineButton == 0 && o.xyThumbTop == 0 && o.xyThumbBottom == 0);
    }
    U(DestroyWindow)(h);
}

TEST(sbi_stub_matches_user32) {
    FARPROC p = sib_stub("NtUserGetScrollBarInfo");
    REQUIRE(p); REQUIRE(U(GetScrollBarInfo)); REQUIRE(U(CreateWindowExW));
    NtUserGetScrollBarInfo_pfn f = (NtUserGetScrollBarInfo_pfn)p;
    HWND h = sib_mk(WS_OVERLAPPEDWINDOW, 0, 300, 300, 1); SIB_NEED_WND(h);
    setrange(h, SB_VERT, 0, 100, 10, 20);
    SCROLLBARINFO a, b;
    CHECK(sbi(h, OBJID_VSCROLL, &a));
    memset(&b, 0xCC, sizeof b); b.cbSize = sizeof b;
    CHECK(f(h, OBJID_VSCROLL, &b));
    CHECK(!memcmp(&a, &b, sizeof a));
 /* The reserved field (offset 0x20) is written back as 0 by the kernel copy. */
    CHECK(a.reserved == 0);
    U(DestroyWindow)(h);
}

TEST(sbi_vertical_geometry) {
    REQUIRE(U(GetScrollBarInfo)); REQUIRE(U(GetWindowInfo)); REQUIRE(U(CreateWindowExW));
    HWND h = sib_mk(WS_OVERLAPPED | WS_CAPTION, 0, 300, 300, 1); SIB_NEED_WND(h);
    setrange(h, SB_VERT, 0, 100, 10, 0);
    CHECK(style_of(h) & WS_VSCROLL);
    WINDOWINFO wi = { sizeof wi };
    CHECK(U(GetWindowInfo)(h, &wi));
    SCROLLBARINFO o;
    CHECK(sbi(h, OBJID_VSCROLL, &o));
    CHECKF(!(o.rgstate[0] & (STATE_INVISIBLE | STATE_OFFSCREEN | STATE_UNAVAILABLE)), "rgstate[0] %#lx", o.rgstate[0]);
    int len = o.rcScrollBar.bottom - o.rcScrollBar.top;
    /* Vertical bar sits at the right edge of the client area, full client height, SM_CXVSCROLL wide. */
    CHECK(o.rcScrollBar.left == wi.rcClient.right);
    CHECK(o.rcScrollBar.top == wi.rcClient.top);
    CHECK(o.rcScrollBar.bottom == wi.rcClient.bottom);
    CHECK_METRIC(o.rcScrollBar.right - o.rcScrollBar.left == U(GetSystemMetrics)(SM_CXVSCROLL), "width %ld", o.rcScrollBar.right - o.rcScrollBar.left);
    CHECK(o.rcScrollBar.right <= wi.rcWindow.right && o.rcScrollBar.bottom <= wi.rcWindow.bottom);
    t_note("VSCROLL dxyLineButton %d thumb %d..%d len %d", o.dxyLineButton, o.xyThumbTop, o.xyThumbBottom, len);
    /* Thumb offsets are relative to the bar start and lie inside it; their exact relation to the arrow buttons is theme/metric dependent. */
    CHECK(o.dxyLineButton > 0 && 2 * o.dxyLineButton < len);
    CHECK(o.xyThumbTop >= 0);
    CHECK(o.xyThumbBottom > o.xyThumbTop);
    CHECK(o.xyThumbBottom <= len);
    for (int k = 0; k < 6; k++) CHECKF(!(o.rgstate[k] & (STATE_UNAVAILABLE | STATE_INVISIBLE | STATE_OFFSCREEN)) || k == 3 || k == 2 || k == 4, "rgstate[%d] %#lx", k, o.rgstate[k]);
    U(DestroyWindow)(h);
}

TEST(sbi_horizontal_geometry) {
    REQUIRE(U(GetScrollBarInfo)); REQUIRE(U(GetWindowInfo)); REQUIRE(U(CreateWindowExW));
    HWND h = sib_mk(WS_OVERLAPPED | WS_CAPTION, 0, 300, 300, 1); SIB_NEED_WND(h);
    setrange(h, SB_HORZ, 0, 100, 10, 0);
    CHECK(style_of(h) & WS_HSCROLL);
    WINDOWINFO wi = { sizeof wi };
    CHECK(U(GetWindowInfo)(h, &wi));
    SCROLLBARINFO o;
    CHECK(sbi(h, OBJID_HSCROLL, &o));
    CHECKF(!(o.rgstate[0] & (STATE_INVISIBLE | STATE_OFFSCREEN | STATE_UNAVAILABLE)), "rgstate[0] %#lx", o.rgstate[0]);
    int len = o.rcScrollBar.right - o.rcScrollBar.left;
    CHECK(o.rcScrollBar.top == wi.rcClient.bottom);
    CHECK(o.rcScrollBar.left == wi.rcClient.left);
    CHECK(o.rcScrollBar.right == wi.rcClient.right);
    CHECK_METRIC(o.rcScrollBar.bottom - o.rcScrollBar.top == U(GetSystemMetrics)(SM_CYHSCROLL), "height %ld", o.rcScrollBar.bottom - o.rcScrollBar.top);
    CHECK(o.dxyLineButton > 0 && 2 * o.dxyLineButton < len);
    CHECK(o.xyThumbTop >= 0 && o.xyThumbBottom > o.xyThumbTop && o.xyThumbBottom <= len);
    /* The vertical bar is absent. */
    SCROLLBARINFO v;
    CHECK(sbi(h, OBJID_VSCROLL, &v));
    CHECK(v.rgstate[0] == STATE_INVISIBLE);
    U(DestroyWindow)(h);
}

TEST(sbi_thumb_tracks_position) {
    REQUIRE(U(GetScrollBarInfo)); REQUIRE(U(CreateWindowExW));
    HWND h = sib_mk(WS_OVERLAPPED | WS_CAPTION, 0, 300, 300, 1); SIB_NEED_WND(h);
    SCROLLBARINFO lo, mid, hi;
    setrange(h, SB_VERT, 0, 100, 10, 0);   CHECK(sbi(h, OBJID_VSCROLL, &lo));
    setrange(h, SB_VERT, 0, 100, 10, 45);  CHECK(sbi(h, OBJID_VSCROLL, &mid));
    setrange(h, SB_VERT, 0, 100, 10, 91);  CHECK(sbi(h, OBJID_VSCROLL, &hi));
    t_note("thumb top at pos 0/45/91: %d/%d/%d bottom %d/%d/%d", lo.xyThumbTop, mid.xyThumbTop, hi.xyThumbTop, lo.xyThumbBottom, mid.xyThumbBottom, hi.xyThumbBottom);
    CHECK(lo.xyThumbTop < mid.xyThumbTop && mid.xyThumbTop < hi.xyThumbTop);
    CHECK(lo.xyThumbBottom < mid.xyThumbBottom && mid.xyThumbBottom < hi.xyThumbBottom);
    /* Thumb at the start rests on the first arrow and at the end on the last (exact pixel equality only noted). */
    if (lo.xyThumbTop != lo.dxyLineButton) t_note("thumb at min starts at %d, line button %d", lo.xyThumbTop, lo.dxyLineButton);
    int len = hi.rcScrollBar.bottom - hi.rcScrollBar.top;
    if (hi.xyThumbBottom != len - hi.dxyLineButton) t_note("thumb at max ends at %d, len-btn %d", hi.xyThumbBottom, len - hi.dxyLineButton);
    CHECK(hi.xyThumbBottom <= len);
    /* Moving the thumb does not change its length by more than rounding. */
    int l0 = lo.xyThumbBottom - lo.xyThumbTop, l1 = hi.xyThumbBottom - hi.xyThumbTop;
    CHECKF(l0 - l1 <= 1 && l1 - l0 <= 1, "thumb length %d vs %d", l0, l1);
    U(DestroyWindow)(h);
}

TEST(sbi_thumb_length_grows_with_page) {
    REQUIRE(U(GetScrollBarInfo)); REQUIRE(U(CreateWindowExW));
    HWND h = sib_mk(WS_OVERLAPPED | WS_CAPTION, 0, 300, 300, 1); SIB_NEED_WND(h);
    int prev = 0;
    static const UINT pages[] = { 5, 10, 25, 50, 90 };
    for (unsigned i = 0; i < sizeof pages / sizeof *pages; i++) {
        setrange(h, SB_VERT, 0, 100, pages[i], 0);
        SCROLLBARINFO o; CHECK(sbi(h, OBJID_VSCROLL, &o));
        int l = o.xyThumbBottom - o.xyThumbTop;
        CHECKF(l >= prev, "page %u thumb %d < previous %d", pages[i], l, prev);
        prev = l;
    }
    U(DestroyWindow)(h);
}

TEST(sbi_arrow_disable_states) {
    REQUIRE(U(GetScrollBarInfo)); REQUIRE(U(EnableScrollBar)); REQUIRE(U(CreateWindowExW));
    HWND h = sib_mk(WS_OVERLAPPED | WS_CAPTION, 0, 300, 300, 1); SIB_NEED_WND(h);
    setrange(h, SB_VERT, 0, 100, 10, 50);
    setrange(h, SB_HORZ, 0, 100, 10, 50);
    SCROLLBARINFO o;
    U(EnableScrollBar)(h, SB_VERT, ESB_DISABLE_LTUP);
    CHECK(sbi(h, OBJID_VSCROLL, &o));
    /* GetScrollBarInfo: arrow bit0 => state[1] (up button) and state[2] (page up) unavailable; bit1 => state[5] and state[4]. */
    CHECK((o.rgstate[1] & STATE_UNAVAILABLE) && (o.rgstate[2] & STATE_UNAVAILABLE));
    CHECK(!(o.rgstate[5] & STATE_UNAVAILABLE) && !(o.rgstate[4] & STATE_UNAVAILABLE));
    CHECK(!(o.rgstate[0] & STATE_UNAVAILABLE));
    U(EnableScrollBar)(h, SB_VERT, ESB_ENABLE_BOTH);     /* disabling is cumulative: re-enable before the next case */
    U(EnableScrollBar)(h, SB_VERT, ESB_DISABLE_RTDN);
    CHECK(sbi(h, OBJID_VSCROLL, &o));
    CHECK(!(o.rgstate[1] & STATE_UNAVAILABLE) && !(o.rgstate[2] & STATE_UNAVAILABLE));
    CHECK((o.rgstate[5] & STATE_UNAVAILABLE) && (o.rgstate[4] & STATE_UNAVAILABLE));
    U(EnableScrollBar)(h, SB_VERT, ESB_ENABLE_BOTH);
    U(EnableScrollBar)(h, SB_VERT, ESB_DISABLE_BOTH);
    CHECK(sbi(h, OBJID_VSCROLL, &o));
    CHECK(o.rgstate[0] & STATE_UNAVAILABLE);          /* both arrows disabled => the bar itself is unavailable */
    CHECK((o.rgstate[1] & STATE_UNAVAILABLE) && (o.rgstate[5] & STATE_UNAVAILABLE));
    /* The horizontal bar is unaffected, and vice versa. */
    CHECK(sbi(h, OBJID_HSCROLL, &o));
    CHECK(!(o.rgstate[0] & STATE_UNAVAILABLE) && !(o.rgstate[1] & STATE_UNAVAILABLE));
    U(EnableScrollBar)(h, SB_HORZ, ESB_DISABLE_LTUP);
    CHECK(sbi(h, OBJID_HSCROLL, &o));
    CHECK((o.rgstate[1] & STATE_UNAVAILABLE) && !(o.rgstate[5] & STATE_UNAVAILABLE));
    U(EnableScrollBar)(h, SB_BOTH, ESB_ENABLE_BOTH);
    CHECK(sbi(h, OBJID_VSCROLL, &o));
    CHECK(!(o.rgstate[0] & STATE_UNAVAILABLE) && !(o.rgstate[1] & STATE_UNAVAILABLE) && !(o.rgstate[5] & STATE_UNAVAILABLE));
    U(DestroyWindow)(h);
}

TEST(sbi_client_on_non_scrollbar) {
    REQUIRE(U(GetScrollBarInfo)); REQUIRE(U(CreateWindowExW));
    HWND p = sib_mk(WS_OVERLAPPEDWINDOW, 0, 300, 300, 0); SIB_NEED_WND(p);
    HWND s = sib_child(p, L"Static", 0, 0, 0, 50, 20);
    if (!s) { t_skip("static"); U(DestroyWindow)(p); return; }
    SCROLLBARINFO o;
    /* OBJID_CLIENT on a window that is not a scroll bar control is forwarded as SBM_GETSCROLLBARINFO (0xeb), which DefWindowProc ignores. */
    CHECK(!sbi(s, OBJID_CLIENT, &o));
    CHECK(!sbi(p, OBJID_CLIENT, &o));
    U(DestroyWindow)(p);
}

TEST(sbi_ctl_control) {
    REQUIRE(U(GetScrollBarInfo)); REQUIRE(U(SetScrollInfo)); REQUIRE(U(EnableScrollBar)); REQUIRE(U(CreateWindowExW));
    HWND p = sib_mk(WS_OVERLAPPEDWINDOW, 0, 300, 300, 1); SIB_NEED_WND(p);
    HWND c = sib_child(p, L"ScrollBar", SBS_VERT | WS_VISIBLE, 10, 10, 20, 150);
    if (!c) { t_skip("ScrollBar control"); U(DestroyWindow)(p); return; }
    setrange(c, SB_CTL, 0, 100, 10, 30);
    SCROLLBARINFO o;
    CHECK(sbi(c, OBJID_CLIENT, &o));
    RECT wr; U(GetWindowRect)(c, &wr);
    t_note("ctl rcScrollBar %ld,%ld,%ld,%ld window %ld,%ld,%ld,%ld dxy %d thumb %d..%d", o.rcScrollBar.left, o.rcScrollBar.top, o.rcScrollBar.right, o.rcScrollBar.bottom,
           wr.left, wr.top, wr.right, wr.bottom, o.dxyLineButton, o.xyThumbTop, o.xyThumbBottom);
    CHECK(o.rcScrollBar.right > o.rcScrollBar.left && o.rcScrollBar.bottom > o.rcScrollBar.top);
    CHECK(o.rcScrollBar.left >= wr.left && o.rcScrollBar.right <= wr.right && o.rcScrollBar.top >= wr.top && o.rcScrollBar.bottom <= wr.bottom);
    int len = o.rcScrollBar.bottom - o.rcScrollBar.top;
    CHECK(o.dxyLineButton > 0 && o.xyThumbTop >= 0 && o.xyThumbBottom > o.xyThumbTop && o.xyThumbBottom <= len);
    CHECK(!(o.rgstate[0] & STATE_UNAVAILABLE));
    /* The control stores its arrow flags in its own SBWND (+0xc): disabling both makes the bar unavailable. */
    U(EnableScrollBar)(c, SB_CTL, ESB_DISABLE_BOTH);
    CHECK(sbi(c, OBJID_CLIENT, &o));
    CHECK(o.rgstate[0] & STATE_UNAVAILABLE);
    CHECK((o.rgstate[1] & STATE_UNAVAILABLE) && (o.rgstate[5] & STATE_UNAVAILABLE));
    U(DestroyWindow)(p);
}

TEST(sbi_ctl_horizontal_vs_vertical_swap) {
    REQUIRE(U(GetScrollBarInfo)); REQUIRE(U(CreateWindowExW));
    HWND p = sib_mk(WS_OVERLAPPEDWINDOW, 0, 400, 400, 1); SIB_NEED_WND(p);
    HWND hz = sib_child(p, L"ScrollBar", SBS_HORZ | WS_VISIBLE, 10, 10, 200, 20);
    HWND vt = sib_child(p, L"ScrollBar", SBS_VERT | WS_VISIBLE, 10, 50, 20, 200);
    if (!hz || !vt) { t_skip("ScrollBar control"); U(DestroyWindow)(p); return; }
    setrange(hz, SB_CTL, 0, 100, 10, 50);
    setrange(vt, SB_CTL, 0, 100, 10, 50);
    SCROLLBARINFO a, b;
    CHECK(sbi(hz, OBJID_CLIENT, &a) && sbi(vt, OBJID_CLIENT, &b));
    /* A horizontal bar measures along x, a vertical one along y; mid position puts the thumb away from both ends. */
    int la = a.rcScrollBar.right - a.rcScrollBar.left, lb = b.rcScrollBar.bottom - b.rcScrollBar.top;
    CHECK(la > a.rcScrollBar.bottom - a.rcScrollBar.top);
    CHECK(lb > b.rcScrollBar.right - b.rcScrollBar.left);
    CHECK(a.xyThumbTop > 0 && a.xyThumbBottom < la);
    CHECK(b.xyThumbTop > 0 && b.xyThumbBottom < lb);
    U(DestroyWindow)(p);
}
