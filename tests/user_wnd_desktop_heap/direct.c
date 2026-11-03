/* user_wnd_desktop_heap: direct win32u calls whose effect is visible in the client view of the
 * window (NtUserMoveWindow / NtUserSetWindowPos / NtUserShowWindow / NtUserGetThreadState /
 * NtUserQueryWindow queue words). */
#define WDH_TAG L"d"
#include "wdh.h"

/* signatures: user32 MoveWindow/SetWindowPos/ShowWindow are bare jumps to these, and the
 * win32kfull analysis reads (hwnd,x,y,cx,cy,repaint), (hwnd,after,x,y,cx,cy,flags), (hwnd,cmd) */
UDECL(DWORD, NtUserMoveWindow, (HWND, int, int, int, int, int));
UDECL(DWORD, NtUserSetWindowPos, (HWND, HWND, int, int, int, int, UINT));
UDECL(DWORD, NtUserShowWindow, (HWND, UINT));
UDECL(ULONG_PTR, NtUserGetThreadState, (int));

static bool rc_is(const struct wv *w, int l, int t, int r, int b) {
    RECT x = WRC(w, W_RCWINDOW);
    return x.left == l && x.top == t && x.right == r && x.bottom == b;
}

TEST(user_wnd_desktop_heap_direct_movewindow_updates_rect) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    REQUIRE(UN(NtUserMoveWindow));
    if (!dpi96(h)) { t_skip("DPI scaling active"); return; }
    /* MoveWindow stores the requested rectangle as the window rect */
    CHECK(UN(NtUserMoveWindow)(h, -29000, -28000, 123, 77, 0) != 0);
    CHECK(wv_get(h, &w));
    CHECK(rc_is(&w, -29000, -28000, -29000 + 123, -28000 + 77));
    END();
}
TEST(user_wnd_desktop_heap_direct_movewindow_invalid_handle) {
    T_BEGIN();
    REQUIRE(UN(NtUserMoveWindow));
    /* ValidateHwnd fails -> the routine returns FALSE */
    HWND bad[] = { NULL, (HWND)(ULONG_PTR)0xFFFFFFFF, (HWND)(ULONG_PTR)0x12345678 };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++)
        CHECKF(UN(NtUserMoveWindow)(bad[i], 0, 0, 1, 1, 0) == 0, "handle %p", bad[i]);
    END();
}
TEST(user_wnd_desktop_heap_direct_setwindowpos_updates_rect) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    REQUIRE(UN(NtUserSetWindowPos));
    if (!dpi96(h)) { t_skip("DPI scaling active"); return; }
    CHECK(UN(NtUserSetWindowPos)(h, NULL, -28000, -27000, 210, 90, SWP_NOZORDER | SWP_NOACTIVATE) != 0);
    CHECK(wv_get(h, &w));
    CHECK(rc_is(&w, -28000, -27000, -28000 + 210, -27000 + 90));
    RECT api;
    CHECK(U(GetWindowRect)(h, &api));
    CHECK(api.left == -28000 && api.right == -28000 + 210);
    END();
}
TEST(user_wnd_desktop_heap_direct_setwindowpos_nomove_nosize_keeps_rect) {
    T_BEGIN();
    HWND h = mk_top(plain_class(), WS_POPUP, 0, -30000, -29000, 100, 80, NULL);
    NEED_WND(h, w);
    REQUIRE(UN(NtUserSetWindowPos));
    if (!dpi96(h)) { t_skip("DPI scaling active"); return; }
    RECT before = WRC(&w, W_RCWINDOW);
    /* with NOMOVE|NOSIZE the x/y/cx/cy arguments are ignored */
    CHECK(UN(NtUserSetWindowPos)(h, NULL, 1, 2, 3, 4, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE) != 0);
    CHECK(wv_get(h, &w));
    RECT after = WRC(&w, W_RCWINDOW);
    CHECK(memcmp(&before, &after, sizeof before) == 0);
    END();
}
TEST(user_wnd_desktop_heap_direct_setwindowpos_invalid_handle) {
    T_BEGIN();
    REQUIRE(UN(NtUserSetWindowPos));
    HWND bad[] = { NULL, (HWND)(ULONG_PTR)0xFFFFFFFF, (HWND)(ULONG_PTR)0x12345678 };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++)
        CHECKF(UN(NtUserSetWindowPos)(bad[i], NULL, 0, 0, 1, 1, SWP_NOZORDER | SWP_NOACTIVATE) == 0, "handle %p", bad[i]);
    END();
}
TEST(user_wnd_desktop_heap_direct_setwindowpos_zorder_relinks) {
    T_BEGIN();
    HWND a = mk_plain();
    HWND b = mk_plain();
    NEED_WND(a, wa);
    struct wv wb;
    CHECK(wv_get(b, &wb));
    REQUIRE(UN(NtUserSetWindowPos));
    /* b was created later and is above a; HWND_BOTTOM (1) moves it to the end of the desktop child list */
    CHECK(UN(NtUserSetWindowPos)(b, (HWND)(ULONG_PTR)1, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != 0);
    CHECK(wv_get(a, &wa));
    CHECK(wv_get(b, &wb));
    /* HWND_BOTTOM puts b below every window of this process; only shell windows of the desktop band stay below it */
    HWND n = U(GetWindow)(b, GW_HWNDNEXT);
    int mine_below = 0;
    for (int i = 0; n && i < 4096; i++, n = U(GetWindow)(n, GW_HWNDNEXT)) {
        DWORD pid = 0; U(GetWindowThreadProcessId)(n, &pid);
        if (pid == GetCurrentProcessId()) mine_below++;
    }
    EXPECT_EQ(mine_below, 0);
    int a_above = 0;
    for (HWND c = U(GetWindow)(a, GW_HWNDNEXT); c; c = U(GetWindow)(c, GW_HWNDNEXT)) if (c == b) { a_above = 1; break; }
    CHECK(a_above);
    CHECK(U(GetWindow)(b, GW_HWNDLAST) != b);
    CHECK(wv_link(&wb, W_PREV) != NULL);
    END();
}
TEST(user_wnd_desktop_heap_direct_showwindow_toggles_visible) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    REQUIRE(UN(NtUserShowWindow));
    CHECK(!(wv_style(&w) & WS_VISIBLE));
    /* the return value is the previous visibility; SW_SHOWNA (8) does not activate */
    CHECK(UN(NtUserShowWindow)(h, SW_SHOWNA) == 0);
    CHECK(wv_get(h, &w));
    CHECK(wv_style(&w) & WS_VISIBLE);
    CHECK(U(IsWindowVisible)(h));
    CHECK(UN(NtUserShowWindow)(h, SW_HIDE) != 0);
    CHECK(wv_get(h, &w));
    CHECK(!(wv_style(&w) & WS_VISIBLE));
    CHECK(!U(IsWindowVisible)(h));
    END();
}
TEST(user_wnd_desktop_heap_direct_showwindow_bad_command) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    REQUIRE(UN(NtUserShowWindow));
    uint32_t before = wv_style(&w);
    /* NtUserShowWindow rejects commands >= 12 with ERROR_INVALID_PARAMETER and does not touch the window */
    UINT bad[] = { 12, 13, 0x20, 0xFFFF, 0x80000000u, 0xFFFFFFFFu };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        SET_ERR(0);
        CHECKF(UN(NtUserShowWindow)(h, bad[i]) == 0, "cmd 0x%x", bad[i]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
    CHECK(wv_get(h, &w));
    EXPECT_EQ(wv_style(&w), before);
    END();
}
TEST(user_wnd_desktop_heap_direct_showwindow_invalid_handle) {
    T_BEGIN();
    REQUIRE(UN(NtUserShowWindow));
    HWND bad[] = { NULL, (HWND)(ULONG_PTR)0xFFFFFFFF, (HWND)(ULONG_PTR)0x12345678 };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++)
        CHECKF(UN(NtUserShowWindow)(bad[i], SW_SHOWNA) == 0, "handle %p", bad[i]);
    END();
}
TEST(user_wnd_desktop_heap_direct_showwindow_desktop_window_untouched) {
    T_BEGIN();
    HWND d = U(GetDesktopWindow)();
    NEED_WND(d, w);
    REQUIRE(UN(NtUserShowWindow));
    uint32_t before = wv_style(&w);
    /* fnids 0x29d (desktop) and 0x29f are filtered at the top of NtUserShowWindow: result 0, no change.
     * SW_SHOWNA is harmless even if the filter did not apply */
    CHECK(UN(NtUserShowWindow)(d, SW_SHOWNA) == 0);
    CHECK(wv_get(d, &w));
    EXPECT_EQ(wv_style(&w), before);
    END();
}

/* ---- queue words ------------------------------------------------------------------------------ */

TEST(user_wnd_desktop_heap_threadstate_focus_active_match_api) {
    T_BEGIN();
    REQUIRE(UN(NtUserGetThreadState));
    /* user32 GetFocus = NtUserGetThreadState(0), GetActiveWindow = NtUserGetThreadState(1) */
    EXPECT_EQ(UN(NtUserGetThreadState)(0), (ULONG_PTR)U(GetFocus)());
    EXPECT_EQ(UN(NtUserGetThreadState)(1), (ULONG_PTR)U(GetActiveWindow)());
    END();
}
TEST(user_wnd_desktop_heap_querywindow_active_focus_of_own_thread) {
    T_BEGIN();
    HWND h = mk_plain();
    if (!h) { t_skip("window creation failed"); return; }
    REQUIRE(UN(NtUserQueryWindow));
    /* cases 3 and 4 read the active (queue+0x78) and focus (queue+0x70) window of the owning thread's
     * queue, the same words GetThreadState(1) and (0) return; this thread owns h */
    EXPECT_EQ(UN(NtUserQueryWindow)(h, 3), (ULONG_PTR)U(GetActiveWindow)());
    EXPECT_EQ(UN(NtUserQueryWindow)(h, 4), (ULONG_PTR)U(GetFocus)());
    END();
}
TEST(user_wnd_desktop_heap_querywindow_foreground_implied_by_api) {
    T_BEGIN();
    HWND h = mk_plain();
    if (!h) { t_skip("window creation failed"); return; }
    REQUIRE(UN(NtUserQueryWindow));
    HWND fg = U(GetForegroundWindow)();
    ULONG_PTR r = UN(NtUserQueryWindow)(h, 7);
    CHECK(r <= 1);
    /* case 7 compares the owner's queue with gpqForeground: if the foreground window belongs to this
     * thread the answer is yes */
    if (fg && U(GetWindowThreadProcessId)(fg, NULL) == GetCurrentThreadId()) CHECK(r == 1);
    END();
}
TEST(user_wnd_desktop_heap_querywindow_hung_is_zero_for_responsive_own_window) {
    T_BEGIN();
    HWND h = mk_plain();
    if (!h) { t_skip("window creation failed"); return; }
    REQUIRE(UN(NtUserQueryWindow));
    /* the calling thread is running, so IsHungWindow on its own window is false; IsHungAppWindow agrees.
     * A thread counts as hung when it has not read its queue for several seconds, and the earlier tests of the
     * area can keep it busy that long on a slow run: read the queue first so the check does not depend on that. */
    k_pump(5);
    EXPECT_EQ(UN(NtUserQueryWindow)(h, 5), 0);
    if (U(IsHungAppWindow)) CHECK(!U(IsHungAppWindow)(h));
    END();
}
TEST(user_wnd_desktop_heap_querywindow_desktop_window_owner_note) {
    T_BEGIN();
    HWND d = U(GetDesktopWindow)();
    if (!d) { t_skip("no desktop window"); return; }
    REQUIRE(UN(NtUserQueryWindow));
    ULONG_PTR pid = UN(NtUserQueryWindow)(d, 0), tid = UN(NtUserQueryWindow)(d, 2);
    DWORD p2 = 0, t2 = U(GetWindowThreadProcessId)(d, &p2);
    /* the desktop window belongs to a system thread: only agreement with the API is fixed */
    EXPECT_EQ(pid, p2);
    EXPECT_EQ(tid, t2);
    t_info("desktop window owner pid=%llu tid=%llu", (unsigned long long)pid, (unsigned long long)tid);
}
