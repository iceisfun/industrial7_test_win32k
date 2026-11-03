/* NtUserShowWindow, NtUserShowWindowAsync, NtUserGetWindowPlacement, NtUserSetWindowPlacement.
 *
 * NtUserShowWindow: ValidateHwndEx(hwnd,1,1) (-> 1400), desktop/switch window fnid -> 0
 * silently, cmd must be < 12 and the window must not be being destroyed, else ERROR_INVALID_PARAMETER
 * (87); then the show-window worker which maps the cmd, sends WM_SHOWWINDOW only when the visibility
 * actually changes and returns the previous WS_VISIBLE state.
 * NtUserGetWindowPlacement: showCmd is derived from the WS_MINIMIZE/WS_MAXIMIZE bits only (a hidden
 * normal window reports SW_SHOWNORMAL); flags is WPF_RESTORETOMAXIMIZED when WS_MAXIMIZE is set or
 * the checkpoint remembers it; ptMin/ptMaxPosition are (-1,-1) unless a checkpoint holds one;
 * rcNormalPosition is in screen coordinates for WS_EX_TOOLWINDOW top-levels (workspace
 * coordinates otherwise) and parent-client coordinates for children; length is rewritten to 0x2c.
 * NtUserSetWindowPlacement: length must be 0x2c (87), the rect is applied unless the window is
 * minimized/maximized, then the show-window worker (showCmd).
 */
#include "zo.h"

static void park_min(HWND w) {
    /* remember an off-screen minimized position so the iconic window never lands on the user's screen */
    WINDOWPLACEMENT wp; memset(&wp, 0, sizeof wp); wp.length = sizeof wp;
    U(GetWindowPlacement)(w, &wp);
    wp.flags |= WPF_SETMINPOSITION;
    wp.ptMinPosition.x = ZO_OFFX; wp.ptMinPosition.y = ZO_OFFY;
    wp.showCmd = U(IsWindowVisible)(w) ? SW_SHOWNA : SW_HIDE;
    if (U(IsIconic)(w)) wp.showCmd = SW_SHOWMINNOACTIVE;
    if (U(IsZoomed)(w)) wp.showCmd = SW_SHOWMAXIMIZED;
    U(SetWindowPlacement)(w, &wp);
}


/* Wine's NtUserSetWindowPlacement takes an extra flags argument, so the raw stub is only called on Windows. */
static BOOL setpl(HWND h, const WINDOWPLACEMENT *wp) { return t_is_wine() ? U(SetWindowPlacement)(h, wp) : UN(NtUserSetWindowPlacement)(h, wp); }
static BOOL getpl(HWND h, WINDOWPLACEMENT *wp) { return t_is_wine() ? U(GetWindowPlacement)(h, wp) : UN(NtUserGetWindowPlacement)(h, wp); }

static int size_code_last(HWND w, int *any) {
    int last = -1; *any = 0;
    for (int i = 0; i < zo_log_n; i++) if (zo_log[i].h == w && zo_log[i].msg == WM_SIZE) { last = (int)zo_log[i].w; *any = 1; }
    return last;
}

/* ---- ShowWindow ---- */

TEST(win_position_zorder_show_invalid_cmd_table) {
    NEED_GUI(); NEED_NT(NtUserShowWindow);
    HWND w = zo_top("badcmd", ZO_OFFX, ZO_OFFY, 80, 60, 1);
    REQUIRE(w);
    int v[] = { 12, 13, 14, 15, 16, 31, 32, 100, 255, 256, 0x10000, 0x10001, 0x7fffffff, -1, (int)0x80000000 };
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) {
        SetLastError(0);
        BOOL r = UN(NtUserShowWindow)(w, v[i]);
        CHECKF(r == 0, "cmd %d returned %d", v[i], r);
        CHECKF(GetLastError() == 87, "cmd %d error %lu want 87", v[i], GetLastError());
        CHECKF(U(IsWindowVisible)(w), "window changed state on rejected cmd %d", v[i]);
    }
    zo_reset();
}

TEST(win_position_zorder_show_invalid_cmd_via_user32) {
    NEED_GUI();
    HWND w = zo_top("badcmd2", ZO_OFFX, ZO_OFFY, 80, 60, 1);
    REQUIRE(w);
    SetLastError(0);
    CHECK(!U(ShowWindow)(w, 12));
    EXPECT_ERR(87);
    zo_reset();
}

TEST(win_position_zorder_show_invalid_hwnd_table) {
    NEED_GUI(); NEED_NT(NtUserShowWindow);
    HWND bad[] = { NULL, (HWND)1, (HWND)0x10, (HWND)0xFFFF, (HWND)(ULONG_PTR)-1 };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(0);
        CHECKF(!UN(NtUserShowWindow)(bad[i], SW_SHOWNA), "hwnd %p", bad[i]);
        CHECKF(GetLastError() == 1400, "hwnd %p error %lu want 1400", bad[i], GetLastError());
    }
}

TEST(win_position_zorder_show_invalid_hwnd_checked_before_cmd) {
    NEED_GUI(); NEED_NT(NtUserShowWindow);
    SetLastError(0);
    CHECK(!UN(NtUserShowWindow)((HWND)1, 99));
    EXPECT_ERR(1400);       /* ValidateHwndEx runs before the cmd < 12 test */
}

TEST(win_position_zorder_show_desktop_window_is_refused) {
    NEED_GUI(); NEED_NT(NtUserShowWindow);
    HWND d = zo_desktop();
    REQUIRE(d);
    /* SW_SHOWNA on the (already visible) desktop is a no-op either way; the fnid 0x29d filter makes it return FALSE
     * where any other visible window returns TRUE (previously visible). */
    CHECK(!UN(NtUserShowWindow)(d, SW_SHOWNA));
    CHECK(U(IsWindowVisible)(d));
}

TEST(win_position_zorder_show_returns_previous_visibility) {
    NEED_GUI(); NEED_NT(NtUserShowWindow);
    HWND w = zo_top("prev", ZO_OFFX, ZO_OFFY, 80, 60, 0);
    REQUIRE(w);
    CHECK(UN(NtUserShowWindow)(w, SW_SHOWNA) == 0);     /* was hidden */
    CHECK(U(IsWindowVisible)(w));
    CHECK(UN(NtUserShowWindow)(w, SW_SHOWNA) != 0);     /* was visible */
    CHECK(UN(NtUserShowWindow)(w, SW_HIDE) != 0);
    CHECK(!U(IsWindowVisible)(w));
    CHECK(UN(NtUserShowWindow)(w, SW_HIDE) == 0);
    zo_reset();
}

TEST(win_position_zorder_show_visible_variants_table) {
    NEED_GUI(); NEED_NT(NtUserShowWindow);
    static const int cmds[] = { SW_SHOWNORMAL, SW_SHOWNOACTIVATE, SW_SHOW, SW_SHOWNA, SW_RESTORE };
    for (unsigned i = 0; i < sizeof cmds / sizeof cmds[0]; i++) {
        HWND w = zo_top("var", ZO_OFFX, ZO_OFFY, 80, 60, 0);
        REQUIRE(w);
        CHECKF(UN(NtUserShowWindow)(w, cmds[i]) == 0, "cmd %d on hidden window", cmds[i]);
        CHECKF(U(IsWindowVisible)(w), "cmd %d did not show", cmds[i]);
        CHECKF(!U(IsIconic)(w) && !U(IsZoomed)(w), "cmd %d changed min/max state", cmds[i]);
        U(DestroyWindow)(w);
    }
    zo_reset();
}

TEST(win_position_zorder_show_hide_makes_invisible_and_keeps_rect) {
    NEED_GUI(); NEED_NT(NtUserShowWindow);
    HWND w = zo_top("hidekeep", ZO_OFFX + 3, ZO_OFFY + 4, 80, 60, 1);
    REQUIRE(w);
    RECT a = zo_wr(w);
    CHECK(UN(NtUserShowWindow)(w, SW_HIDE));
    RECT b = zo_wr(w);
    CHECK(zo_same_rect(&a, &b));
    zo_reset();
}

TEST(win_position_zorder_show_showwindow_message_only_on_change) {
    NEED_GUI(); NEED_NT(NtUserShowWindow);
    HWND w = zo_top("msg", ZO_OFFX, ZO_OFFY, 80, 60, 0);
    REQUIRE(w);
    zo_log_clear(); zo_log_on = 1;
    UN(NtUserShowWindow)(w, SW_HIDE);                    /* already hidden: ShowWindow returns before any message */
    EXPECT_EQ(zo_log_count(w, WM_SHOWWINDOW), 0);
    EXPECT_EQ(zo_log_count(w, WM_WINDOWPOSCHANGING), 0);
    UN(NtUserShowWindow)(w, SW_SHOWNA);
    EXPECT_EQ(zo_log_count(w, WM_SHOWWINDOW), 1);
    UN(NtUserShowWindow)(w, SW_SHOW);                    /* already visible */
    EXPECT_EQ(zo_log_count(w, WM_SHOWWINDOW), 1);
    UN(NtUserShowWindow)(w, SW_HIDE);
    EXPECT_EQ(zo_log_count(w, WM_SHOWWINDOW), 2);
    zo_reset();
}

TEST(win_position_zorder_show_showna_on_visible_resends_showwindow) {
    NEED_GUI(); NEED_NT(NtUserShowWindow);
    HWND w = zo_top("showna", ZO_OFFX, ZO_OFFY, 80, 60, 0);
    REQUIRE(w);
    UN(NtUserShowWindow)(w, SW_SHOWNA);
    zo_log_clear(); zo_log_on = 1;
    UN(NtUserShowWindow)(w, SW_SHOWNA);                  /* unlike SW_SHOW, SW_SHOWNOACTIVATE and SW_SHOWNORMAL, SW_SHOWNA does not stop at an already visible window */
    EXPECT_EQ(zo_log_count(w, WM_SHOWWINDOW), 1);
    EXPECT_EQ(zo_log_count(w, WM_WINDOWPOSCHANGED), 0);  /* but nothing changes, so no position change is reported */
    int i = zo_log_find(w, WM_SHOWWINDOW, 0);
    if (i >= 0) { CHECK(zo_log_get(i)->w == 1); CHECK(zo_log_get(i)->l == 0); }
    zo_reset();
}

TEST(win_position_zorder_show_showwindow_message_params) {
    NEED_GUI(); NEED_NT(NtUserShowWindow);
    HWND w = zo_top("msgp", ZO_OFFX, ZO_OFFY, 80, 60, 0);
    REQUIRE(w);
    zo_log_clear(); zo_log_on = 1;
    UN(NtUserShowWindow)(w, SW_SHOWNA);
    UN(NtUserShowWindow)(w, SW_HIDE);
    int a = zo_log_find(w, WM_SHOWWINDOW, 0), b = zo_log_find(w, WM_SHOWWINDOW, a + 1);
    REQUIRE(a >= 0 && b >= 0);
    EXPECT_EQ(zo_log[a].w, TRUE); EXPECT_EQ(zo_log[a].l, 0);        /* SendMessage(hwnd, WM_SHOWWINDOW, fShow, 0) */
    EXPECT_EQ(zo_log[b].w, FALSE); EXPECT_EQ(zo_log[b].l, 0);
    zo_reset();
}

TEST(win_position_zorder_show_heap_visible_bit) {
    NEED_GUI(); NEED_NT(NtUserShowWindow);
    if (t_is_wine() || !zo_heap_ok()) { t_skip("desktop heap windows not readable"); return; }
    HWND w = zo_top("hv", ZO_OFFX, ZO_OFFY, 80, 60, 0);
    REQUIRE(w);
    int ok; unsigned long st;
    st = zo_style_heap(w, &ok); CHECK(ok && !(st & WS_VISIBLE));
    UN(NtUserShowWindow)(w, SW_SHOWNA);
    st = zo_style_heap(w, &ok); CHECK(ok && (st & WS_VISIBLE) && !(st & (WS_MINIMIZE | WS_MAXIMIZE)));
    UN(NtUserShowWindow)(w, SW_HIDE);
    st = zo_style_heap(w, &ok); CHECK(ok && !(st & WS_VISIBLE));
    zo_reset();
}

TEST(win_position_zorder_show_child_of_hidden_parent) {
    NEED_GUI(); NEED_NT(NtUserShowWindow);
    HWND p = zo_top("hp", ZO_OFFX, ZO_OFFY, 100, 100, 0);
    REQUIRE(p);
    HWND c = zo_child(p, "hc", 0, 0, 20, 20, 0, 0);
    REQUIRE(c);
    CHECK(UN(NtUserShowWindow)(c, SW_SHOWNA) == 0);
    CHECK(U(GetWindowLongA)(c, GWL_STYLE) & WS_VISIBLE);       /* WS_VISIBLE set ... */
    CHECK(!U(IsWindowVisible)(c));                              /* ... but an ancestor is hidden */
    zo_show(p);
    CHECK(U(IsWindowVisible)(c));
    zo_hide(p);
    CHECK(!U(IsWindowVisible)(c));
    CHECK(U(GetWindowLongA)(c, GWL_STYLE) & WS_VISIBLE);       /* hiding the parent leaves the child's own bit alone */
    zo_reset();
}

TEST(win_position_zorder_show_minimize_variants_table) {
    NEED_GUI(); NEED_NT(NtUserShowWindow);
    /* SW_FORCEMINIMIZE goes through a separate hung-window path and is only noted below */
    static const int cmds[] = { SW_SHOWMINIMIZED, SW_MINIMIZE, SW_SHOWMINNOACTIVE };
    for (unsigned i = 0; i < sizeof cmds / sizeof cmds[0]; i++) {
        HWND w = zo_top("min", ZO_OFFX, ZO_OFFY, 80, 60, 1);
        REQUIRE(w);
        park_min(w);
        BOOL prev = UN(NtUserShowWindow)(w, cmds[i]);
        CHECKF(prev != 0, "cmd %d: previous state was visible", cmds[i]);
        CHECKF(U(IsIconic)(w), "cmd %d did not minimize", cmds[i]);
        CHECKF(!U(IsZoomed)(w), "cmd %d left WS_MAXIMIZE", cmds[i]);
        CHECKF(U(IsWindowVisible)(w), "cmd %d minimized window not visible", cmds[i]);
        WINDOWPLACEMENT wp = { sizeof wp };
        CHECK(U(GetWindowPlacement)(w, &wp));
        CHECKF(wp.showCmd == SW_SHOWMINIMIZED, "cmd %d showCmd %u", cmds[i], wp.showCmd);
        U(DestroyWindow)(w);
    }
    zo_reset();
}

TEST(win_position_zorder_show_forceminimize_note) {
    NEED_GUI(); NEED_NT(NtUserShowWindow);
    HWND w = zo_top("force", ZO_OFFX, ZO_OFFY, 80, 60, 1);
    REQUIRE(w);
    park_min(w);
    BOOL prev = UN(NtUserShowWindow)(w, SW_FORCEMINIMIZE);       /* cmd 11 is accepted by the cmd < 12 test */
    t_note("SW_FORCEMINIMIZE returned %d, iconic=%d visible=%d", prev, U(IsIconic)(w), U(IsWindowVisible)(w));
    CHECK(GetLastError() != 87);
    zo_reset();
}

TEST(win_position_zorder_show_minimize_sends_size_minimized) {
    NEED_GUI(); NEED_NT(NtUserShowWindow);
    HWND w = zo_top("minsz", ZO_OFFX, ZO_OFFY, 80, 60, 1);
    REQUIRE(w);
    park_min(w);
    zo_log_clear(); zo_log_on = 1;
    UN(NtUserShowWindow)(w, SW_SHOWMINNOACTIVE);
    int any; int c = size_code_last(w, &any);
    CHECK(any); CHECK(c == SIZE_MINIMIZED);
    CHECK(zo_log_count(w, WM_WINDOWPOSCHANGED) >= 1);
    zo_reset();
}

TEST(win_position_zorder_show_minimized_hidden_window_becomes_visible) {
    NEED_GUI(); NEED_NT(NtUserShowWindow);
    HWND w = zo_top("minhid", ZO_OFFX, ZO_OFFY, 80, 60, 0);
    REQUIRE(w);
    park_min(w);
    CHECK(UN(NtUserShowWindow)(w, SW_SHOWMINNOACTIVE) == 0);
    CHECK(U(IsIconic)(w));
    CHECK(U(IsWindowVisible)(w));
    zo_reset();
}

TEST(win_position_zorder_show_maximize_uses_getminmaxinfo) {
    NEED_GUI(); NEED_NT(NtUserShowWindow);
    HWND w = zo_top("max", ZO_OFFX + 10, ZO_OFFY + 20, 120, 90, 1);
    REQUIRE(w);
    zo_log_clear(); zo_log_on = 1;
    CHECK(UN(NtUserShowWindow)(w, SW_SHOWMAXIMIZED) != 0);
    CHECK(U(IsZoomed)(w)); CHECK(!U(IsIconic)(w));
    CHECK(zo_log_count(w, WM_GETMINMAXINFO) >= 1);          /* the kernel asks the window */
    RECT r = zo_wr(w);
    /* the fixture's handler returns ptMaxPosition = (ZO_OFFX,ZO_OFFY), ptMaxSize = 300x200; the kernel may
     * validate/clamp the reply against the monitor, so the exact result is only noted */
    zo_note_rect("maximized rect (handler asked for 300x200 at the off-screen origin)", &r);
    CHECK(r.right - r.left > 0 && r.bottom - r.top > 0);
    int any; int c = size_code_last(w, &any);
    CHECK(any); CHECK(c == SIZE_MAXIMIZED);
    zo_reset();
}

TEST(win_position_zorder_show_maximize_follows_user_minmaxinfo) {
    NEED_GUI(); NEED_NT(NtUserShowWindow);
    HWND w = zo_top("maxu", ZO_OFFX, ZO_OFFY, 120, 90, 1);
    REQUIRE(w);
    zo_beh = ZO_B_MINMAX_USER;
    zo_mm.ptMaxPosition.x = ZO_OFFX + 111; zo_mm.ptMaxPosition.y = ZO_OFFY + 222;
    zo_mm.ptMaxSize.x = 321; zo_mm.ptMaxSize.y = 234;
    zo_mm.ptMaxTrackSize.x = 1000; zo_mm.ptMaxTrackSize.y = 1000;
    zo_mm.ptMinTrackSize.x = 10; zo_mm.ptMinTrackSize.y = 10;
    UN(NtUserShowWindow)(w, SW_MAXIMIZE);
    CHECK(U(IsZoomed)(w));
    RECT r = zo_wr(w);
    zo_note_rect("maximized rect (handler asked for 321x234 at +111,+222 from the off-screen origin)", &r);
    CHECK(r.right - r.left > 0 && r.bottom - r.top > 0);
    zo_reset();
}

TEST(win_position_zorder_show_maximize_heap_style_and_rect) {
    NEED_GUI(); NEED_NT(NtUserShowWindow);
    if (t_is_wine() || !zo_heap_ok()) { t_skip("desktop heap windows not readable"); return; }
    HWND w = zo_top("maxh", ZO_OFFX, ZO_OFFY, 120, 90, 1);
    REQUIRE(w);
    UN(NtUserShowWindow)(w, SW_SHOWMAXIMIZED);
    int ok; unsigned long st = zo_style_heap(w, &ok);
    CHECK(ok && (st & WS_MAXIMIZE) && !(st & WS_MINIMIZE));
    RECT hw; CHECK(zo_rect_heap(w, ZO_WND_RcWnd, &hw));
    RECT api = zo_wr(w); CHECK(zo_same_rect(&hw, &api));
    UN(NtUserShowWindow)(w, SW_RESTORE);
    st = zo_style_heap(w, &ok); CHECK(ok && !(st & (WS_MAXIMIZE | WS_MINIMIZE)));
    zo_reset();
}

TEST(win_position_zorder_show_restore_from_maximized) {
    NEED_GUI(); NEED_NT(NtUserShowWindow);
    HWND w = zo_top("rest", ZO_OFFX + 10, ZO_OFFY + 20, 120, 90, 1);
    REQUIRE(w);
    RECT orig = zo_wr(w);
    UN(NtUserShowWindow)(w, SW_SHOWMAXIMIZED);
    zo_log_clear(); zo_log_on = 1;
    CHECK(UN(NtUserShowWindow)(w, SW_RESTORE) != 0);
    CHECK(!U(IsZoomed)(w)); CHECK(!U(IsIconic)(w));
    RECT r = zo_wr(w);
    CHECK(zo_same_rect(&r, &orig));
    int any; int c = size_code_last(w, &any);
    CHECK(any); CHECK(c == SIZE_RESTORED);
    zo_reset();
}

TEST(win_position_zorder_show_restore_from_minimized) {
    NEED_GUI(); NEED_NT(NtUserShowWindow);
    HWND w = zo_top("restm", ZO_OFFX + 10, ZO_OFFY + 20, 120, 90, 1);
    REQUIRE(w);
    RECT orig = zo_wr(w);
    park_min(w);
    UN(NtUserShowWindow)(w, SW_SHOWMINNOACTIVE);
    CHECK(U(IsIconic)(w));
    RECT mn = zo_wr(w);
    t_note("minimized rect: (%ld,%ld) %ldx%ld", mn.left, mn.top, mn.right - mn.left, mn.bottom - mn.top);
    zo_log_clear(); zo_log_on = 1;
    UN(NtUserShowWindow)(w, SW_SHOWNOACTIVATE);         /* restores an iconic window without activating it */
    CHECK(!U(IsIconic)(w));
    RECT r = zo_wr(w);
    CHECK(zo_same_rect(&r, &orig));
    zo_reset();
}

TEST(win_position_zorder_show_restore_chain_max_min_restore) {
    NEED_GUI(); NEED_NT(NtUserShowWindow);
    HWND w = zo_top("chain", ZO_OFFX + 10, ZO_OFFY + 20, 120, 90, 1);
    REQUIRE(w);
    RECT orig = zo_wr(w);
    park_min(w);
    UN(NtUserShowWindow)(w, SW_SHOWMAXIMIZED);
    UN(NtUserShowWindow)(w, SW_SHOWMINNOACTIVE);
    CHECK(U(IsIconic)(w));
    WINDOWPLACEMENT wp = { sizeof wp };
    CHECK(U(GetWindowPlacement)(w, &wp));
    CHECK(wp.flags & WPF_RESTORETOMAXIMIZED);              /* will come back maximized */
    CHECK(wp.showCmd == SW_SHOWMINIMIZED);
    UN(NtUserShowWindow)(w, SW_RESTORE);
    CHECK(!U(IsIconic)(w)); CHECK(U(IsZoomed)(w));          /* restore from min returns to the maximized state */
    UN(NtUserShowWindow)(w, SW_RESTORE);
    CHECK(!U(IsIconic)(w)); CHECK(!U(IsZoomed)(w));
    RECT r = zo_wr(w);
    CHECK(zo_same_rect(&r, &orig));
    zo_reset();
}

TEST(win_position_zorder_show_restore_on_hidden_normal_window_shows_it) {
    NEED_GUI(); NEED_NT(NtUserShowWindow);
    HWND w = zo_top("resthid", ZO_OFFX, ZO_OFFY, 80, 60, 0);
    REQUIRE(w);
    CHECK(UN(NtUserShowWindow)(w, SW_RESTORE) == 0);
    CHECK(U(IsWindowVisible)(w));
    CHECK(UN(NtUserShowWindow)(w, SW_RESTORE) != 0);        /* second time: visible already, nothing else changes */
    CHECK(U(IsWindowVisible)(w));
    zo_reset();
}

TEST(win_position_zorder_show_maximize_twice_is_noop) {
    NEED_GUI(); NEED_NT(NtUserShowWindow);
    HWND w = zo_top("max2", ZO_OFFX, ZO_OFFY, 80, 60, 1);
    REQUIRE(w);
    UN(NtUserShowWindow)(w, SW_SHOWMAXIMIZED);
    RECT a = zo_wr(w);
    zo_log_clear(); zo_log_on = 1;
    CHECK(UN(NtUserShowWindow)(w, SW_SHOWMAXIMIZED) != 0);
    RECT b = zo_wr(w);
    CHECK(zo_same_rect(&a, &b));
    CHECK(U(IsZoomed)(w));
    zo_reset();
}

TEST(win_position_zorder_show_hide_a_maximized_window_keeps_maximized_style) {
    NEED_GUI(); NEED_NT(NtUserShowWindow);
    HWND w = zo_top("maxhide", ZO_OFFX, ZO_OFFY, 80, 60, 1);
    REQUIRE(w);
    UN(NtUserShowWindow)(w, SW_SHOWMAXIMIZED);
    CHECK(UN(NtUserShowWindow)(w, SW_HIDE) != 0);
    CHECK(!U(IsWindowVisible)(w));
    CHECK(U(IsZoomed)(w));
    UN(NtUserShowWindow)(w, SW_SHOWNA);
    CHECK(U(IsWindowVisible)(w)); CHECK(U(IsZoomed)(w));
    zo_reset();
}

TEST(win_position_zorder_show_child_maximize_and_minimize_flags) {
    NEED_GUI(); NEED_NT(NtUserShowWindow);
    HWND p = zo_top("cmp", ZO_OFFX, ZO_OFFY, 300, 200, 1);
    REQUIRE(p);
    HWND c = zo_child(p, "cmc", 10, 10, 50, 40, WS_VISIBLE, 0);
    REQUIRE(c);
    UN(NtUserShowWindow)(c, SW_SHOWMAXIMIZED);
    t_note("child after SW_SHOWMAXIMIZED: zoomed=%d iconic=%d rect=%ldx%ld", U(IsZoomed)(c), U(IsIconic)(c),
           zo_wr(c).right - zo_wr(c).left, zo_wr(c).bottom - zo_wr(c).top);
    UN(NtUserShowWindow)(c, SW_RESTORE);
    CHECK(!U(IsZoomed)(c));
    RECT cs = zo_cr_screen(p), r = zo_wr(c);
    EXPECT_EQ(r.left, cs.left + 10); EXPECT_EQ(r.top, cs.top + 10);
    EXPECT_EQ(r.right - r.left, 50);
    zo_reset();
}

TEST(win_position_zorder_show_user32_matches_stub) {
    NEED_GUI(); NEED_NT(NtUserShowWindow);
    HWND w = zo_top("shag", ZO_OFFX, ZO_OFFY, 80, 60, 0);
    REQUIRE(w);
    CHECK(U(ShowWindow)(w, SW_SHOWNA) == 0);
    CHECK(UN(NtUserShowWindow)(w, SW_SHOWNA) != 0);
    CHECK(U(ShowWindow)(w, SW_HIDE) != 0);
    FARPROC p = t_proc_in("win32u.dll", "NtUserShowWindow");
    if (!t_is_wine()) EXPECT_EQ(k_stub_number(p), 4183);
    zo_reset();
}

/* ---- ShowWindowAsync ---- */

TEST(win_position_zorder_showasync_invalid_cmd) {
    NEED_GUI(); NEED_NT(NtUserShowWindowAsync);
    HWND w = zo_top("async_bad", ZO_OFFX, ZO_OFFY, 80, 60, 1);
    REQUIRE(w);
    int v[] = { 12, 13, 100, 0x10000, -1 };
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) {
        SetLastError(0);
        CHECKF(UN(NtUserShowWindowAsync)(w, v[i]) == 0, "cmd %d", v[i]);
        CHECKF(GetLastError() == 87, "cmd %d error %lu want 87", v[i], GetLastError());
    }
    zo_reset();
}

TEST(win_position_zorder_showasync_invalid_hwnd) {
    NEED_GUI(); NEED_NT(NtUserShowWindowAsync);
    HWND bad[] = { NULL, (HWND)1, (HWND)0xFFFF, (HWND)(ULONG_PTR)-1 };
    for (unsigned i = 0; i < 4; i++) {
        SetLastError(0);
        CHECKF(!UN(NtUserShowWindowAsync)(bad[i], SW_HIDE), "hwnd %p", bad[i]);
        CHECKF(GetLastError() == 1400, "hwnd %p error %lu want 1400", bad[i], GetLastError());
    }
}

TEST(win_position_zorder_showasync_deferred_until_pumped) {
    NEED_GUI(); NEED_NT(NtUserShowWindowAsync);
    HWND w = zo_top("async_show", ZO_OFFX, ZO_OFFY, 80, 60, 0);
    REQUIRE(w);
    BOOL r = UN(NtUserShowWindowAsync)(w, SW_SHOWNA);
    t_note("ShowWindowAsync(hidden window) returned %d", r);
    CHECK(!U(IsWindowVisible)(w));     /* queued to the window's own thread; this thread has not pumped */
    for (int i = 0; i < 100 && !U(IsWindowVisible)(w); i++) k_pump(10);
    CHECK(U(IsWindowVisible)(w));
    UN(NtUserShowWindowAsync)(w, SW_HIDE);
    CHECK(U(IsWindowVisible)(w));
    for (int i = 0; i < 100 && U(IsWindowVisible)(w); i++) k_pump(10);
    CHECK(!U(IsWindowVisible)(w));
    zo_reset();
}

TEST(win_position_zorder_showasync_minimize_applies) {
    NEED_GUI(); NEED_NT(NtUserShowWindowAsync);
    HWND w = zo_top("async_min", ZO_OFFX, ZO_OFFY, 80, 60, 1);
    REQUIRE(w);
    park_min(w);
    CHECK(UN(NtUserShowWindowAsync)(w, SW_SHOWMINNOACTIVE) != 0);
    for (int i = 0; i < 100 && !U(IsIconic)(w); i++) k_pump(10);
    CHECK(U(IsIconic)(w));
    zo_reset();
}

static ULONG_PTR showasync_fn(void *p) { return (ULONG_PTR)U(ShowWindowAsync)((HWND)p, SW_SHOWNA); }
TEST(win_position_zorder_showasync_cross_thread_target) {
    NEED_GUI();
    struct zo_thread *t = zo_thread_start();
    if (!t) { t_skip("could not start GUI thread"); return; }
    HWND w = zo_thread_window(t, "asyncx", ZO_OFFX, ZO_OFFY, 80, 60, 0);
    if (!w) { zo_thread_stop(t); t_skip("window on worker thread failed"); return; }
    CHECK(U(ShowWindowAsync)(w, SW_SHOWNA));
    for (int i = 0; i < 200 && !U(IsWindowVisible)(w); i++) Sleep(10);      /* the worker is idle in GetMessage and handles the event itself */
    CHECK(U(IsWindowVisible)(w));
    U(ShowWindowAsync)(w, SW_HIDE);
    for (int i = 0; i < 200 && U(IsWindowVisible)(w); i++) Sleep(10);
    CHECK(!U(IsWindowVisible)(w));
    zo_thread_stop(t);
}

/* ---- GetWindowPlacement ---- */

TEST(win_position_zorder_placement_get_basic_fields) {
    NEED_GUI(); NEED_NT(NtUserGetWindowPlacement);
    HWND w = zo_top("gp", ZO_OFFX + 13, ZO_OFFY + 14, 111, 77, 1);
    REQUIRE(w);
    WINDOWPLACEMENT wp; memset(&wp, 0xAA, sizeof wp); wp.length = sizeof wp;
    CHECK(getpl(w, &wp));
    EXPECT_EQ(wp.length, 0x2c);
    EXPECT_EQ(wp.showCmd, SW_SHOWNORMAL);
    EXPECT_EQ(wp.flags, 0);
    EXPECT_EQ(wp.ptMinPosition.x, -1); EXPECT_EQ(wp.ptMinPosition.y, -1);
    EXPECT_EQ(wp.ptMaxPosition.x, -1); EXPECT_EQ(wp.ptMaxPosition.y, -1);
    /* WS_EX_TOOLWINDOW top-level: rcNormalPosition is in screen coordinates (no work-area adjustment) */
    EXPECT_EQ(wp.rcNormalPosition.left, ZO_OFFX + 13); EXPECT_EQ(wp.rcNormalPosition.top, ZO_OFFY + 14);
    EXPECT_EQ(wp.rcNormalPosition.right, ZO_OFFX + 13 + 111); EXPECT_EQ(wp.rcNormalPosition.bottom, ZO_OFFY + 14 + 77);
    zo_reset();
}

TEST(win_position_zorder_placement_get_invalid_hwnd) {
    NEED_GUI(); NEED_NT(NtUserGetWindowPlacement);
    WINDOWPLACEMENT wp = { sizeof wp };
    HWND bad[] = { NULL, (HWND)1, (HWND)0xFFFF, (HWND)(ULONG_PTR)-1 };
    for (unsigned i = 0; i < 4; i++) {
        SetLastError(0);
        CHECKF(!getpl(bad[i], &wp), "hwnd %p", bad[i]);
        CHECKF(GetLastError() == 1400, "hwnd %p error %lu want 1400", bad[i], GetLastError());
    }
}

TEST(win_position_zorder_placement_get_hidden_window_reports_shownormal) {
    NEED_GUI(); NEED_NT(NtUserGetWindowPlacement);
    HWND w = zo_top("gph", ZO_OFFX, ZO_OFFY, 60, 50, 0);
    REQUIRE(w);
    WINDOWPLACEMENT wp = { sizeof wp };
    CHECK(getpl(w, &wp));
    EXPECT_EQ(wp.showCmd, SW_SHOWNORMAL);   /* derived from WS_MINIMIZE/WS_MAXIMIZE only; never SW_HIDE */
    zo_reset();
}

TEST(win_position_zorder_placement_get_maximized) {
    NEED_GUI(); NEED_NT(NtUserGetWindowPlacement);
    HWND w = zo_top("gpm", ZO_OFFX + 5, ZO_OFFY + 6, 90, 70, 1);
    REQUIRE(w);
    UN(NtUserShowWindow)(w, SW_SHOWMAXIMIZED);
    WINDOWPLACEMENT wp = { sizeof wp };
    CHECK(getpl(w, &wp));
    EXPECT_EQ(wp.showCmd, SW_SHOWMAXIMIZED);
    CHECK(wp.flags & WPF_RESTORETOMAXIMIZED);
    /* the stored normal rectangle is the pre-maximize rectangle, not the current one */
    EXPECT_EQ(wp.rcNormalPosition.left, ZO_OFFX + 5); EXPECT_EQ(wp.rcNormalPosition.top, ZO_OFFY + 6);
    EXPECT_EQ(wp.rcNormalPosition.right - wp.rcNormalPosition.left, 90);
    RECT cur = zo_wr(w);
    CHECK(cur.right - cur.left != 88 || cur.bottom - cur.top != 66);   /* the window shows the maximized rect ... */
    zo_reset();
}

TEST(win_position_zorder_placement_get_minimized) {
    NEED_GUI(); NEED_NT(NtUserGetWindowPlacement);
    HWND w = zo_top("gpn", ZO_OFFX + 5, ZO_OFFY + 6, 90, 70, 1);
    REQUIRE(w);
    park_min(w);
    UN(NtUserShowWindow)(w, SW_SHOWMINNOACTIVE);
    WINDOWPLACEMENT wp = { sizeof wp };
    CHECK(getpl(w, &wp));
    EXPECT_EQ(wp.showCmd, SW_SHOWMINIMIZED);
    EXPECT_EQ(wp.rcNormalPosition.left, ZO_OFFX + 5); EXPECT_EQ(wp.rcNormalPosition.top, ZO_OFFY + 6);
    EXPECT_EQ(wp.rcNormalPosition.right - wp.rcNormalPosition.left, 90);
    /* the position remembered by park_min() */
    EXPECT_EQ(wp.ptMinPosition.x, ZO_OFFX); EXPECT_EQ(wp.ptMinPosition.y, ZO_OFFY);
    zo_reset();
}

TEST(win_position_zorder_placement_get_child_is_parent_relative) {
    NEED_GUI(); NEED_NT(NtUserGetWindowPlacement);
    HWND p = zo_top("gpc", ZO_OFFX + 100, ZO_OFFY + 100, 300, 200, 1);
    REQUIRE(p);
    HWND c = zo_child(p, "gpcc", 12, 34, 56, 78, WS_VISIBLE, 0);
    REQUIRE(c);
    WINDOWPLACEMENT wp = { sizeof wp };
    CHECK(getpl(c, &wp));
    EXPECT_EQ(wp.rcNormalPosition.left, 12); EXPECT_EQ(wp.rcNormalPosition.top, 34);
    EXPECT_EQ(wp.rcNormalPosition.right, 12 + 56); EXPECT_EQ(wp.rcNormalPosition.bottom, 34 + 78);
    zo_reset();
}

TEST(win_position_zorder_placement_get_length_is_rewritten) {
    NEED_GUI(); NEED_NT(NtUserGetWindowPlacement);
    HWND w = zo_top("gpl", ZO_OFFX, ZO_OFFY, 60, 50, 1);
    REQUIRE(w);
    unsigned lens[] = { 0, 1, sizeof(WINDOWPLACEMENT) };
    for (unsigned i = 0; i < 3; i++) {
        WINDOWPLACEMENT wp; memset(&wp, 0, sizeof wp); wp.length = lens[i];
        SetLastError(0);
        BOOL r = getpl(w, &wp);
        t_note("input length %u -> result %d, output length %u", lens[i], r, wp.length);
        if (r) EXPECT_EQ(wp.length, 0x2c);
    }
    zo_reset();
}

TEST(win_position_zorder_placement_get_tracks_window_moves) {
    NEED_GUI(); NEED_NT(NtUserGetWindowPlacement);
    HWND w = zo_top("gpt", ZO_OFFX, ZO_OFFY, 60, 50, 1);
    REQUIRE(w);
    for (int i = 1; i <= 3; i++) {
        U(MoveWindow)(w, ZO_OFFX + i * 10, ZO_OFFY + i * 11, 60 + i, 50 + i, TRUE);
        WINDOWPLACEMENT wp = { sizeof wp };
        CHECK(getpl(w, &wp));
        CHECKF(wp.rcNormalPosition.left == ZO_OFFX + i * 10 && wp.rcNormalPosition.top == ZO_OFFY + i * 11 &&
               wp.rcNormalPosition.right - wp.rcNormalPosition.left == 60 + i, "step %d", i);
    }
    zo_reset();
}

TEST(win_position_zorder_placement_get_stub_and_user32_agree) {
    NEED_GUI(); NEED_NT(NtUserGetWindowPlacement);
    HWND w = zo_top("gpa", ZO_OFFX + 1, ZO_OFFY + 2, 60, 50, 1);
    REQUIRE(w);
    WINDOWPLACEMENT a = { sizeof a }, b = { sizeof b };
    CHECK(U(GetWindowPlacement)(w, &a));
    CHECK(UN(NtUserGetWindowPlacement)(w, &b));
    CHECK(!memcmp(&a, &b, sizeof a));
    FARPROC p = t_proc_in("win32u.dll", "NtUserGetWindowPlacement");
    if (!t_is_wine()) EXPECT_EQ(k_stub_number(p), 4296);
    zo_reset();
}

/* ---- SetWindowPlacement ---- */

TEST(win_position_zorder_placement_set_bad_length_table) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPlacement);
    HWND w = zo_top("splen", ZO_OFFX, ZO_OFFY, 60, 50, 1);
    REQUIRE(w);
    unsigned lens[] = { 0, 1, 0x2b, 0x2d, 0x30, 0x58, 0x100, 0xffffffffu };
    for (unsigned i = 0; i < sizeof lens / sizeof lens[0]; i++) {
        WINDOWPLACEMENT wp; memset(&wp, 0, sizeof wp);
        wp.length = lens[i]; wp.showCmd = SW_SHOWNA;
        wp.rcNormalPosition.left = ZO_OFFX + 99; wp.rcNormalPosition.top = ZO_OFFY + 99;
        wp.rcNormalPosition.right = ZO_OFFX + 199; wp.rcNormalPosition.bottom = ZO_OFFY + 199;
        wp.ptMinPosition.x = wp.ptMinPosition.y = wp.ptMaxPosition.x = wp.ptMaxPosition.y = -1;
        SetLastError(0);
        CHECKF(!setpl(w, &wp), "length 0x%x accepted", lens[i]);
        CHECKF(GetLastError() == 87, "length 0x%x error %lu want 87", lens[i], GetLastError());
    }
    RECT r = zo_wr(w);
    EXPECT_EQ(r.left, ZO_OFFX);     /* nothing applied */
    zo_reset();
}

TEST(win_position_zorder_placement_set_invalid_hwnd) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPlacement);
    WINDOWPLACEMENT wp; memset(&wp, 0, sizeof wp); wp.length = sizeof wp; wp.showCmd = SW_SHOWNA;
    HWND bad[] = { NULL, (HWND)1, (HWND)0xFFFF, (HWND)(ULONG_PTR)-1 };
    for (unsigned i = 0; i < 4; i++) {
        SetLastError(0);
        CHECKF(!setpl(bad[i], &wp), "hwnd %p", bad[i]);
        CHECKF(GetLastError() == 1400, "hwnd %p error %lu want 1400", bad[i], GetLastError());
    }
}

TEST(win_position_zorder_placement_set_moves_normal_window) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPlacement);
    HWND w = zo_top("spm", ZO_OFFX, ZO_OFFY, 60, 50, 1);
    REQUIRE(w);
    WINDOWPLACEMENT wp; memset(&wp, 0, sizeof wp); wp.length = sizeof wp; wp.showCmd = SW_SHOWNA;
    wp.ptMinPosition.x = wp.ptMinPosition.y = wp.ptMaxPosition.x = wp.ptMaxPosition.y = -1;
    wp.rcNormalPosition.left = ZO_OFFX + 21; wp.rcNormalPosition.top = ZO_OFFY + 22;
    wp.rcNormalPosition.right = ZO_OFFX + 21 + 123; wp.rcNormalPosition.bottom = ZO_OFFY + 22 + 99;
    CHECK(setpl(w, &wp));
    RECT r = zo_wr(w);
    EXPECT_EQ(r.left, ZO_OFFX + 21); EXPECT_EQ(r.top, ZO_OFFY + 22);
    EXPECT_EQ(r.right - r.left, 123); EXPECT_EQ(r.bottom - r.top, 99);
    CHECK(U(IsWindowVisible)(w));
    zo_reset();
}

TEST(win_position_zorder_placement_set_showcmd_hide_and_show) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPlacement);
    HWND w = zo_top("spc", ZO_OFFX, ZO_OFFY, 60, 50, 1);
    REQUIRE(w);
    WINDOWPLACEMENT wp; memset(&wp, 0, sizeof wp); wp.length = sizeof wp;
    wp.ptMinPosition.x = wp.ptMinPosition.y = wp.ptMaxPosition.x = wp.ptMaxPosition.y = -1;
    wp.rcNormalPosition.left = ZO_OFFX; wp.rcNormalPosition.top = ZO_OFFY;
    wp.rcNormalPosition.right = ZO_OFFX + 60; wp.rcNormalPosition.bottom = ZO_OFFY + 50;
    wp.showCmd = SW_HIDE;
    CHECK(setpl(w, &wp));
    CHECK(!U(IsWindowVisible)(w));
    wp.showCmd = SW_SHOWNA;
    CHECK(setpl(w, &wp));
    CHECK(U(IsWindowVisible)(w));
    zo_reset();
}

TEST(win_position_zorder_placement_set_showmaximized_keeps_normal_rect) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPlacement);
    HWND w = zo_top("spmax", ZO_OFFX, ZO_OFFY, 60, 50, 1);
    REQUIRE(w);
    WINDOWPLACEMENT wp; memset(&wp, 0, sizeof wp); wp.length = sizeof wp;
    wp.ptMinPosition.x = wp.ptMinPosition.y = wp.ptMaxPosition.x = wp.ptMaxPosition.y = -1;
    wp.rcNormalPosition.left = ZO_OFFX + 40; wp.rcNormalPosition.top = ZO_OFFY + 41;
    wp.rcNormalPosition.right = ZO_OFFX + 40 + 88; wp.rcNormalPosition.bottom = ZO_OFFY + 41 + 66;
    wp.showCmd = SW_SHOWMAXIMIZED;
    CHECK(setpl(w, &wp));
    CHECK(U(IsZoomed)(w));
    RECT cur = zo_wr(w);
    CHECK(!(cur.right - cur.left == 88 && cur.bottom - cur.top == 66));   /* the window shows the maximized rect ... */
    WINDOWPLACEMENT g = { sizeof g };
    CHECK(U(GetWindowPlacement)(w, &g));
    EXPECT_EQ(g.rcNormalPosition.left, ZO_OFFX + 40);          /* ... and remembers the requested normal rect */
    EXPECT_EQ(g.rcNormalPosition.right - g.rcNormalPosition.left, 88);
    UN(NtUserShowWindow)(w, SW_RESTORE);
    RECT r = zo_wr(w);
    EXPECT_EQ(r.left, ZO_OFFX + 40); EXPECT_EQ(r.top, ZO_OFFY + 41);
    EXPECT_EQ(r.right - r.left, 88); EXPECT_EQ(r.bottom - r.top, 66);
    zo_reset();
}

TEST(win_position_zorder_placement_set_showminimized_sets_iconic_and_normal_rect) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPlacement);
    HWND w = zo_top("spmin", ZO_OFFX, ZO_OFFY, 60, 50, 1);
    REQUIRE(w);
    WINDOWPLACEMENT wp; memset(&wp, 0, sizeof wp); wp.length = sizeof wp;
    wp.flags = WPF_SETMINPOSITION;
    wp.ptMinPosition.x = ZO_OFFX + 5; wp.ptMinPosition.y = ZO_OFFY + 6;
    wp.ptMaxPosition.x = wp.ptMaxPosition.y = -1;
    wp.rcNormalPosition.left = ZO_OFFX + 30; wp.rcNormalPosition.top = ZO_OFFY + 31;
    wp.rcNormalPosition.right = ZO_OFFX + 30 + 70; wp.rcNormalPosition.bottom = ZO_OFFY + 31 + 60;
    wp.showCmd = SW_SHOWMINNOACTIVE;
    CHECK(setpl(w, &wp));
    CHECK(U(IsIconic)(w));
    WINDOWPLACEMENT g = { sizeof g };
    CHECK(U(GetWindowPlacement)(w, &g));
    EXPECT_EQ(g.showCmd, SW_SHOWMINIMIZED);
    EXPECT_EQ(g.rcNormalPosition.left, ZO_OFFX + 30);
    EXPECT_EQ(g.rcNormalPosition.right - g.rcNormalPosition.left, 70);
    EXPECT_EQ(g.ptMinPosition.x, ZO_OFFX + 5); EXPECT_EQ(g.ptMinPosition.y, ZO_OFFY + 6);
    UN(NtUserShowWindow)(w, SW_RESTORE);
    RECT r = zo_wr(w);
    EXPECT_EQ(r.left, ZO_OFFX + 30); EXPECT_EQ(r.right - r.left, 70);
    zo_reset();
}

TEST(win_position_zorder_placement_set_minmax_points_round_trip) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPlacement);
    HWND w = zo_top("sprt", ZO_OFFX, ZO_OFFY, 60, 50, 1);
    REQUIRE(w);
    WINDOWPLACEMENT wp; memset(&wp, 0, sizeof wp); wp.length = sizeof wp;
    wp.ptMinPosition.x = ZO_OFFX + 100; wp.ptMinPosition.y = ZO_OFFY + 101;
    wp.ptMaxPosition.x = ZO_OFFX + 200; wp.ptMaxPosition.y = ZO_OFFY + 201;
    wp.rcNormalPosition.left = ZO_OFFX; wp.rcNormalPosition.top = ZO_OFFY;
    wp.rcNormalPosition.right = ZO_OFFX + 60; wp.rcNormalPosition.bottom = ZO_OFFY + 50;
    wp.showCmd = SW_SHOWNA;
    CHECK(setpl(w, &wp));
    WINDOWPLACEMENT g = { sizeof g };
    CHECK(getpl(w, &g));
    /* SetWindowPlacement stores both points in the checkpoint whenever neither coordinate is -1; _GetWindowPlacement returns them */
    EXPECT_EQ(g.ptMinPosition.x, ZO_OFFX + 100); EXPECT_EQ(g.ptMinPosition.y, ZO_OFFY + 101);
    EXPECT_EQ(g.ptMaxPosition.x, ZO_OFFX + 200); EXPECT_EQ(g.ptMaxPosition.y, ZO_OFFY + 201);
    /* -1 in either coordinate clears the stored point */
    wp.ptMinPosition.x = -1; wp.ptMaxPosition.y = -1;
    CHECK(setpl(w, &wp));
    CHECK(getpl(w, &g));
    EXPECT_EQ(g.ptMinPosition.x, -1); EXPECT_EQ(g.ptMinPosition.y, -1);
    EXPECT_EQ(g.ptMaxPosition.x, -1); EXPECT_EQ(g.ptMaxPosition.y, -1);
    zo_reset();
}

TEST(win_position_zorder_placement_set_restoretomaximized_flag) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPlacement);
    HWND w = zo_top("sprm", ZO_OFFX, ZO_OFFY, 60, 50, 1);
    REQUIRE(w);
    WINDOWPLACEMENT wp; memset(&wp, 0, sizeof wp); wp.length = sizeof wp;
    wp.flags = WPF_RESTORETOMAXIMIZED | WPF_SETMINPOSITION;
    wp.ptMinPosition.x = ZO_OFFX; wp.ptMinPosition.y = ZO_OFFY; wp.ptMaxPosition.x = wp.ptMaxPosition.y = -1;
    wp.rcNormalPosition.left = ZO_OFFX; wp.rcNormalPosition.top = ZO_OFFY;
    wp.rcNormalPosition.right = ZO_OFFX + 60; wp.rcNormalPosition.bottom = ZO_OFFY + 50;
    wp.showCmd = SW_SHOWMINNOACTIVE;
    CHECK(setpl(w, &wp));
    CHECK(U(IsIconic)(w));
    WINDOWPLACEMENT g = { sizeof g };
    CHECK(getpl(w, &g));
    CHECK(g.flags & WPF_RESTORETOMAXIMIZED);
    UN(NtUserShowWindow)(w, SW_RESTORE);
    CHECK(U(IsZoomed)(w));          /* WPF_RESTORETOMAXIMIZED: restore goes to the maximized state */
    zo_reset();
}

TEST(win_position_zorder_placement_set_minimized_window_updates_rect_only) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPlacement);
    HWND w = zo_top("spmu", ZO_OFFX, ZO_OFFY, 60, 50, 1);
    REQUIRE(w);
    park_min(w);
    UN(NtUserShowWindow)(w, SW_SHOWMINNOACTIVE);
    CHECK(U(IsIconic)(w));
    WINDOWPLACEMENT wp = { sizeof wp };
    U(GetWindowPlacement)(w, &wp);
    wp.rcNormalPosition.left = ZO_OFFX + 70; wp.rcNormalPosition.top = ZO_OFFY + 71;
    wp.rcNormalPosition.right = ZO_OFFX + 70 + 55; wp.rcNormalPosition.bottom = ZO_OFFY + 71 + 45;
    wp.showCmd = SW_SHOWMINNOACTIVE;
    CHECK(setpl(w, &wp));
    CHECK(U(IsIconic)(w));                                      /* still minimized */
    UN(NtUserShowWindow)(w, SW_RESTORE);
    RECT r = zo_wr(w);
    EXPECT_EQ(r.left, ZO_OFFX + 70); EXPECT_EQ(r.right - r.left, 55);
    zo_reset();
}

TEST(win_position_zorder_placement_set_child_rect_is_parent_relative) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPlacement);
    HWND p = zo_top("spcp", ZO_OFFX + 100, ZO_OFFY + 100, 300, 200, 1);
    REQUIRE(p);
    HWND c = zo_child(p, "spcc", 1, 2, 30, 40, WS_VISIBLE, 0);
    REQUIRE(c);
    WINDOWPLACEMENT wp; memset(&wp, 0, sizeof wp); wp.length = sizeof wp; wp.showCmd = SW_SHOWNA;
    wp.ptMinPosition.x = wp.ptMinPosition.y = wp.ptMaxPosition.x = wp.ptMaxPosition.y = -1;
    wp.rcNormalPosition.left = 20; wp.rcNormalPosition.top = 30; wp.rcNormalPosition.right = 20 + 61; wp.rcNormalPosition.bottom = 30 + 62;
    CHECK(setpl(c, &wp));
    RECT r = zo_wr(c), pc = zo_cr_screen(p);
    EXPECT_EQ(r.left, pc.left + 20); EXPECT_EQ(r.top, pc.top + 30);
    EXPECT_EQ(r.right - r.left, 61);
    zo_reset();
}

TEST(win_position_zorder_placement_set_async_flag_same_thread) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPlacement);
    HWND w = zo_top("spas", ZO_OFFX, ZO_OFFY, 60, 50, 0);
    REQUIRE(w);
    WINDOWPLACEMENT wp; memset(&wp, 0, sizeof wp); wp.length = sizeof wp;
    wp.flags = WPF_ASYNCWINDOWPLACEMENT;
    wp.ptMinPosition.x = wp.ptMinPosition.y = wp.ptMaxPosition.x = wp.ptMaxPosition.y = -1;
    wp.rcNormalPosition.left = ZO_OFFX + 9; wp.rcNormalPosition.top = ZO_OFFY + 9;
    wp.rcNormalPosition.right = ZO_OFFX + 9 + 60; wp.rcNormalPosition.bottom = ZO_OFFY + 9 + 50;
    wp.showCmd = SW_SHOWNA;
    CHECK(setpl(w, &wp));
    /* owner thread == current thread: SetWindowPlacement skips the posted-event detour and shows synchronously */
    CHECK(U(IsWindowVisible)(w));
    zo_reset();
}

static ULONG_PTR setpl_fn(void *p) { (void)p; return 1; }
TEST(win_position_zorder_placement_set_async_flag_cross_thread) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPlacement);
    struct zo_thread *t = zo_thread_start();
    if (!t) { t_skip("could not start GUI thread"); return; }
    HWND w = zo_thread_window(t, "spasx", ZO_OFFX, ZO_OFFY, 60, 50, 0);
    if (!w) { zo_thread_stop(t); t_skip("window on worker thread failed"); return; }
    WINDOWPLACEMENT wp; memset(&wp, 0, sizeof wp); wp.length = sizeof wp;
    wp.flags = WPF_ASYNCWINDOWPLACEMENT;
    wp.ptMinPosition.x = wp.ptMinPosition.y = wp.ptMaxPosition.x = wp.ptMaxPosition.y = -1;
    wp.rcNormalPosition.left = ZO_OFFX + 9; wp.rcNormalPosition.top = ZO_OFFY + 9;
    wp.rcNormalPosition.right = ZO_OFFX + 9 + 60; wp.rcNormalPosition.bottom = ZO_OFFY + 9 + 50;
    wp.showCmd = SW_SHOWNA;
    CHECK(setpl(w, &wp));
    zo_thread_call(t, setpl_fn, NULL);
    for (int i = 0; i < 200 && !U(IsWindowVisible)(w); i++) Sleep(10);
    CHECK(U(IsWindowVisible)(w));
    zo_thread_stop(t);
}

TEST(win_position_zorder_placement_set_stub_and_user32_agree) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPlacement);
    HWND w = zo_top("spag", ZO_OFFX, ZO_OFFY, 60, 50, 1);
    REQUIRE(w);
    WINDOWPLACEMENT wp = { sizeof wp };
    U(GetWindowPlacement)(w, &wp);
    wp.rcNormalPosition.left += 7; wp.rcNormalPosition.right += 7;
    wp.showCmd = SW_SHOWNA;
    CHECK(U(SetWindowPlacement)(w, &wp));
    RECT a = zo_wr(w);
    wp.rcNormalPosition.left += 7; wp.rcNormalPosition.right += 7;
    CHECK(UN(NtUserSetWindowPlacement)(w, &wp));
    RECT b = zo_wr(w);
    EXPECT_EQ(b.left - a.left, 7);
    FARPROC p = t_proc_in("win32u.dll", "NtUserSetWindowPlacement");
    if (!t_is_wine()) EXPECT_EQ(k_stub_number(p), 4309);
    zo_reset();
}

/* ---- min/max track info ---- */

TEST(win_position_zorder_minmax_sent_for_thickframe_resize_note) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND w = zo_top_ex("track", ZO_OFFX, ZO_OFFY, 200, 150, WS_POPUP | WS_THICKFRAME, 0, NULL, 1, NULL);
    REQUIRE(w);
    zo_beh = ZO_B_MINMAX_USER;
    zo_mm.ptMinTrackSize.x = 160; zo_mm.ptMinTrackSize.y = 120;
    zo_mm.ptMaxTrackSize.x = 260; zo_mm.ptMaxTrackSize.y = 190;
    zo_mm.ptMaxSize.x = 260; zo_mm.ptMaxSize.y = 190;
    zo_mm.ptMaxPosition.x = ZO_OFFX; zo_mm.ptMaxPosition.y = ZO_OFFY;
    zo_log_clear(); zo_log_on = 1;
    UN(NtUserSetWindowPos)(w, NULL, ZO_OFFX, ZO_OFFY, 20, 20, SWP_NOZORDER | SWP_NOACTIVATE);
    RECT r1 = zo_wr(w);
    t_note("WM_GETMINMAXINFO count on shrink: %d; resulting size %ldx%ld (requested 20x20, min track 160x120)",
           zo_log_count(w, WM_GETMINMAXINFO), r1.right - r1.left, r1.bottom - r1.top);
    UN(NtUserSetWindowPos)(w, NULL, ZO_OFFX, ZO_OFFY, 900, 900, SWP_NOZORDER | SWP_NOACTIVATE);
    RECT r2 = zo_wr(w);
    t_note("size after requesting 900x900 (max track 260x190): %ldx%ld", r2.right - r2.left, r2.bottom - r2.top);
    CHECK(r1.right - r1.left > 0 && r2.right - r2.left > 0);
    zo_reset();
}

TEST(win_position_zorder_minmax_not_sent_for_plain_move) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND w = zo_top("trackmv", ZO_OFFX, ZO_OFFY, 100, 80, 1);
    REQUIRE(w);
    zo_log_clear(); zo_log_on = 1;
    UN(NtUserSetWindowPos)(w, NULL, ZO_OFFX + 5, ZO_OFFY + 5, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    t_note("WM_GETMINMAXINFO count on a pure move: %d", zo_log_count(w, WM_GETMINMAXINFO));
    CHECK(zo_log_count(w, WM_WINDOWPOSCHANGED) >= 1);
    zo_reset();
}

TEST(win_position_zorder_minmax_maxtrack_vs_maxsize_note) {
    NEED_GUI(); NEED_NT(NtUserShowWindow);
    HWND w = zo_top("trackmax", ZO_OFFX, ZO_OFFY, 100, 80, 1);
    REQUIRE(w);
    zo_beh = ZO_B_MINMAX_USER;
    zo_mm.ptMaxPosition.x = ZO_OFFX; zo_mm.ptMaxPosition.y = ZO_OFFY;
    zo_mm.ptMaxSize.x = 400; zo_mm.ptMaxSize.y = 300;
    zo_mm.ptMaxTrackSize.x = 250; zo_mm.ptMaxTrackSize.y = 180;
    zo_mm.ptMinTrackSize.x = 10; zo_mm.ptMinTrackSize.y = 10;
    UN(NtUserShowWindow)(w, SW_MAXIMIZE);
    RECT r = zo_wr(w);
    t_note("maximized with ptMaxSize 400x300 and ptMaxTrackSize 250x180 -> %ldx%ld", r.right - r.left, r.bottom - r.top);
    CHECK(U(IsZoomed)(w));
    CHECK(r.right - r.left <= 400 && r.bottom - r.top <= 300);
    zo_reset();
}

/* ---- startup info (child process) ---- */

CHILD(zo_startup_default) {
    /* a child started without STARTF_USESHOWWINDOW: SW_SHOWDEFAULT behaves as SW_SHOWNORMAL (ShowWindow maps cmd 10 -> 1) */
    if (!zo_have_gui()) { printf("NOTE no GUI available\n"); return 0; }
    int fails = 0;
    HWND w = zo_top_ex("def", ZO_OFFX, ZO_OFFY, 200, 100, WS_OVERLAPPEDWINDOW, 0, NULL, 0, NULL);
    if (!w) { printf("NOTE no window\n"); return 0; }
    U(ShowWindow)(w, SW_SHOWDEFAULT);
    if (!U(IsWindowVisible)(w)) { printf("FAIL SW_SHOWDEFAULT did not show\n"); fails++; }
    if (U(IsIconic)(w) || U(IsZoomed)(w)) { printf("FAIL SW_SHOWDEFAULT changed min/max\n"); fails++; }
    U(DestroyWindow)(w);
    return fails;
}

CHILD(zo_startup_minimized) {
    if (!zo_have_gui()) { printf("NOTE no GUI available\n"); return 0; }
    if (t_is_wine()) { printf("NOTE Wine does not implement win32k STARTUPINFO show handling\n"); return 0; }
    int fails = 0;
    STARTUPINFOA si; memset(&si, 0, sizeof si); si.cb = sizeof si;
    GetStartupInfoA(&si);
    if (!(si.dwFlags & STARTF_USESHOWWINDOW) || si.wShowWindow != SW_SHOWMINNOACTIVE) { printf("FAIL startup info not delivered (flags=%lx show=%u)\n", si.dwFlags, si.wShowWindow); return 1; }
    HWND w1 = zo_top_ex("sw1", ZO_OFFX, ZO_OFFY, 200, 100, WS_OVERLAPPEDWINDOW, 0, NULL, 0, NULL);
    HWND w2 = zo_top_ex("sw2", ZO_OFFX + 300, ZO_OFFY, 200, 100, WS_OVERLAPPEDWINDOW, 0, NULL, 0, NULL);
    if (!w1 || !w2) { printf("NOTE no window\n"); return 0; }
    /* ShowWindow: first SW_SHOWNORMAL/SW_SHOW of an unowned WS_CAPTION top-level takes the process's
     * STARTUPINFO nCmdShow (SW_SHOWMINNOACTIVE here) and then clears the flag in the process info. A
     * SetWindowPlacement call (park_min) counts as that first show too, so w1 is parked only afterwards. */
    U(ShowWindow)(w1, SW_SHOWNORMAL);
    if (!U(IsIconic)(w1)) { printf("FAIL first ShowWindow(SW_SHOWNORMAL) ignored STARTUPINFO wShowWindow\n"); fails++; }
    U(SetWindowPos)(w1, NULL, ZO_OFFX, ZO_OFFY, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);   /* keep the iconic window off screen */
    park_min(w2);
    U(ShowWindow)(w2, SW_SHOWNORMAL);
    if (U(IsIconic)(w2)) { printf("FAIL STARTUPINFO nCmdShow applied twice\n"); fails++; }
    if (!U(IsWindowVisible)(w2)) { printf("FAIL second window not shown\n"); fails++; }
    U(DestroyWindow)(w1); U(DestroyWindow)(w2);
    return fails;
}

CHILD(zo_startup_popup_unaffected) {
    if (!zo_have_gui()) { printf("NOTE no GUI available\n"); return 0; }
    if (t_is_wine()) { printf("NOTE Wine\n"); return 0; }
    int fails = 0;
    HWND w = zo_top("popup", ZO_OFFX, ZO_OFFY, 200, 100, 0);       /* WS_POPUP: no WS_CAPTION, so the startup nCmdShow is not consumed */
    if (!w) return 0;
    U(ShowWindow)(w, SW_SHOWNORMAL);
    if (U(IsIconic)(w)) { printf("FAIL caption-less popup took the startup show command\n"); fails++; }
    HWND w2 = zo_top_ex("owned", ZO_OFFX, ZO_OFFY, 200, 100, WS_OVERLAPPEDWINDOW, 0, w, 0, NULL);
    if (w2) {
        U(ShowWindow)(w2, SW_SHOWNORMAL);
        if (U(IsIconic)(w2)) { printf("FAIL owned window took the startup show command\n"); fails++; }
    }
    return fails;
}

TEST(win_position_zorder_startup_showdefault_without_startupinfo) {
    NEED_GUI();
    struct k_child_result r = k_child_run("zo_startup_default", "", 30000);
    EXPECT_EQ(r.exit_code, 0);
}

TEST(win_position_zorder_startup_nCmdShow_consumed_by_first_captioned_window) {
    NEED_GUI();
    STARTUPINFOEXA si; memset(&si, 0, sizeof si);
    si.StartupInfo.cb = sizeof si;
    si.StartupInfo.dwFlags = STARTF_USESHOWWINDOW;
    si.StartupInfo.wShowWindow = SW_SHOWMINNOACTIVE;
    struct k_child_result r = k_child_run_ex("zo_startup_minimized", "", 30000, &si);
    EXPECT_EQ(r.exit_code, 0);
}

TEST(win_position_zorder_startup_nCmdShow_ignored_for_popup_and_owned) {
    NEED_GUI();
    STARTUPINFOEXA si; memset(&si, 0, sizeof si);
    si.StartupInfo.cb = sizeof si;
    si.StartupInfo.dwFlags = STARTF_USESHOWWINDOW;
    si.StartupInfo.wShowWindow = SW_SHOWMINNOACTIVE;
    struct k_child_result r = k_child_run_ex("zo_startup_popup_unaffected", "", 30000, &si);
    EXPECT_EQ(r.exit_code, 0);
}
