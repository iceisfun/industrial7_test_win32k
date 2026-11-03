/* Direct win32u stub calls for NtUserShowScrollBar / NtUserEnableScrollBar and the
 * NtUserGetScrollBarInfo argument checks (win32kfull analysis: ValidateHwnd, bar < 4 test, fnid filter). */
#include "sib.h"

typedef BOOL (WINAPI *bar3_pfn)(HWND, int, BOOL);
typedef BOOL (WINAPI *en3_pfn)(HWND, UINT, UINT);

static void give_range(HWND h, int bar) {
    SCROLLINFO si = { sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS, 0, 100, 10, 5, 0 };
    U(SetScrollInfo)(h, bar, &si, FALSE);
}

TEST(sbd_show_stub_bad_bar) {
    FARPROC p = sib_stub("NtUserShowScrollBar");
    REQUIRE(p); REQUIRE(U(CreateWindowExW));
    HWND h = sib_mk(WS_OVERLAPPEDWINDOW, 0, 200, 200, 0); SIB_NEED_WND(h);
    static const int bars[] = { 4, 7, -1, 0x7fffffff };
    for (unsigned i = 0; i < sizeof bars / sizeof *bars; i++) {
        SET_ERR(0);
        /* NtUserShowScrollBar: nBar compared unsigned < 4, else ERROR_INVALID_PARAMETER and result 0. */
        EQ(((bar3_pfn)p)(h, bars[i], TRUE), 0);
        EXPECT_ERR(E_INVALID_PARAM);
    }
    U(DestroyWindow)(h);
}

TEST(sbd_show_stub_invalid_hwnd) {
    FARPROC p = sib_stub("NtUserShowScrollBar");
    REQUIRE(p); REQUIRE(U(CreateWindowExW));
    HWND gone = sib_mk(WS_OVERLAPPEDWINDOW, 0, 100, 100, 0); SIB_NEED_WND(gone);
    U(DestroyWindow)(gone);
    HWND bad[] = { NULL, (HWND)(ULONG_PTR)0x10, (HWND)(ULONG_PTR)-1, gone };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        SET_ERR(0);
        /* ValidateHwnd failure: result 0 with ERROR_INVALID_WINDOW_HANDLE. */
        EQ(((bar3_pfn)p)(bad[i], SB_VERT, TRUE), 0);
        EXPECT_ERR(E_INVALID_HWND);
    }
}

TEST(sbd_show_stub_desktop_ignored) {
    FARPROC p = sib_stub("NtUserShowScrollBar");
    REQUIRE(p); REQUIRE(U(GetDesktopWindow));
    SET_ERR(0);
    /* fnid FNID_DESKTOP is filtered before nBar is examined: 0, no error. */
    EQ(((bar3_pfn)p)(U(GetDesktopWindow)(), SB_VERT, TRUE), 0);
    EXPECT_ERR(0);
}

TEST(sbd_show_stub_sets_style) {
    FARPROC p = sib_stub("NtUserShowScrollBar");
    REQUIRE(p); REQUIRE(U(CreateWindowExW)); REQUIRE(U(GetWindowLongW));
    HWND h = sib_mk(WS_OVERLAPPED, 0, 200, 200, 0); SIB_NEED_WND(h);
    CHECK(!(U(GetWindowLongW)(h, GWL_STYLE) & (WS_HSCROLL | WS_VSCROLL)));
    /* ShowScrollBar toggles the WS_VSCROLL / WS_HSCROLL style bits in the shared tagWND. */
    ((bar3_pfn)p)(h, SB_VERT, TRUE);
    CHECK(U(GetWindowLongW)(h, GWL_STYLE) & WS_VSCROLL);
    CHECK(!(U(GetWindowLongW)(h, GWL_STYLE) & WS_HSCROLL));
    ((bar3_pfn)p)(h, SB_HORZ, TRUE);
    CHECK(U(GetWindowLongW)(h, GWL_STYLE) & WS_HSCROLL);
    ((bar3_pfn)p)(h, SB_BOTH, FALSE);
    CHECK(!(U(GetWindowLongW)(h, GWL_STYLE) & (WS_HSCROLL | WS_VSCROLL)));
    U(DestroyWindow)(h);
}

TEST(sbd_enable_stub_bad_bar) {
    FARPROC p = sib_stub("NtUserEnableScrollBar");
    REQUIRE(p); REQUIRE(U(CreateWindowExW));
    HWND h = sib_mk(WS_OVERLAPPEDWINDOW, 0, 200, 200, 0); SIB_NEED_WND(h);
    static const UINT bars[] = { 4, 9, 0xffffffffu };
    for (unsigned i = 0; i < sizeof bars / sizeof *bars; i++) {
        SET_ERR(0);
        /* NtUserEnableScrollBar: wSBflags >= 4 gives ERROR_INVALID_PARAMETER. */
        EQ(((en3_pfn)p)(h, bars[i], ESB_DISABLE_BOTH), 0);
        EXPECT_ERR(E_INVALID_PARAM);
    }
    U(DestroyWindow)(h);
}

TEST(sbd_enable_stub_ctl_needs_scrollbar_class) {
    FARPROC p = sib_stub("NtUserEnableScrollBar");
    REQUIRE(p); REQUIRE(U(CreateWindowExW));
    HWND h = sib_mk(WS_OVERLAPPEDWINDOW, 0, 200, 200, 0); SIB_NEED_WND(h);
    SET_ERR(0);
    /* SB_CTL (2) is only accepted when the window's fnid is FNID_SCROLLBAR (0x29a). */
    EQ(((en3_pfn)p)(h, SB_CTL, ESB_DISABLE_BOTH), 0);
    EXPECT_ERR(E_INVALID_PARAM);
    U(DestroyWindow)(h);
}

TEST(sbd_enable_stub_invalid_hwnd) {
    FARPROC p = sib_stub("NtUserEnableScrollBar");
    REQUIRE(p);
    HWND bad[] = { NULL, (HWND)(ULONG_PTR)0x10, (HWND)(ULONG_PTR)-1 };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        SET_ERR(0);
        EQ(((en3_pfn)p)(bad[i], SB_HORZ, ESB_DISABLE_BOTH), 0);
        EXPECT_ERR(E_INVALID_HWND);
    }
}

TEST(sbd_enable_stub_matches_state) {
    FARPROC p = sib_stub("NtUserEnableScrollBar");
    REQUIRE(p); REQUIRE(U(CreateWindowExW)); REQUIRE(U(GetScrollBarInfo));
    HWND h = sib_mk(WS_OVERLAPPEDWINDOW | WS_VSCROLL, 0, 200, 200, 0); SIB_NEED_WND(h);
    give_range(h, SB_VERT);
    ((en3_pfn)p)(h, SB_VERT, ESB_DISABLE_BOTH);
    SCROLLBARINFO sb; memset(&sb, 0, sizeof sb); sb.cbSize = sizeof sb;
    if (U(GetScrollBarInfo)(h, OBJID_VSCROLL, &sb)) {
        /* EnableScrollBar disables both arrow buttons; rgstate[1]/[5] are the up and down arrow buttons. */
        CHECK(sb.rgstate[1] & STATE_SYSTEM_UNAVAILABLE);
        CHECK(sb.rgstate[5] & STATE_SYSTEM_UNAVAILABLE);
    }
    ((en3_pfn)p)(h, SB_VERT, ESB_ENABLE_BOTH);
    memset(&sb, 0, sizeof sb); sb.cbSize = sizeof sb;
    if (U(GetScrollBarInfo)(h, OBJID_VSCROLL, &sb)) {
        CHECK(!(sb.rgstate[1] & STATE_SYSTEM_UNAVAILABLE));
        CHECK(!(sb.rgstate[5] & STATE_SYSTEM_UNAVAILABLE));
    }
    U(DestroyWindow)(h);
}
