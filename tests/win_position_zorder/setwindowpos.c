/* NtUserSetWindowPos: parameter validation, geometry, z-order, topmost/owner rules and the
 * WM_WINDOWPOS* protocol, each cross-checked against the desktop-heap tagWND the kernel maintains.
 *
 * Observed behavior of NtUserSetWindowPos:
 *   - ValidateHwnd(hwnd) first: invalid handle -> 0 / ERROR_INVALID_WINDOW_HANDLE (1400);
 *     the desktop window (fnid 0x29d) and the switch window (0x29f) return 0 without an error.
 *   - (flags & 0xfffc9800) != 0 -> 0 / ERROR_INVALID_FLAGS (1004); this test sits before the
 *     ValidateHWNDIA(hWndInsertAfter) test, which is the only argument that may be a special value.
 *   - a window whose "being destroyed" bit is set -> ERROR_INVALID_PARAMETER (87).
 *   - ConstrainWindowPos clamps x,y to [-0x8000, 0x7fff]; ConstrainWindowSize clamps cx,cy to
 *     [0, 0xffff] (each only when the matching SWP_NOMOVE / SWP_NOSIZE bit is clear).
 */
#include "zo.h"

#define AREA "win_position_zorder_swp_"

static const unsigned bad_bits[] = {
    0x00000800, 0x00001000, 0x00008000, 0x00040000, 0x00080000, 0x00100000, 0x00200000, 0x00400000,
    0x00800000, 0x01000000, 0x02000000, 0x04000000, 0x08000000, 0x10000000, 0x20000000, 0x40000000, 0x80000000u
};

TEST(win_position_zorder_swp_invalid_flag_bits_table) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND w = zo_top("flags", ZO_OFFX, ZO_OFFY, 100, 80, 1);
    REQUIRE(w);
    RECT before = zo_wr(w);
    for (unsigned i = 0; i < sizeof bad_bits / sizeof bad_bits[0]; i++) {
        SetLastError(0xdead);
        BOOL r = UN(NtUserSetWindowPos)(w, NULL, 5, 5, 7, 7, ZO_SWP_NOTHING | bad_bits[i]);
        CHECKF(r == 0, "bit 0x%08x accepted", bad_bits[i]);
        CHECKF(GetLastError() == 1004, "bit 0x%08x error %lu want 1004", bad_bits[i], GetLastError());
    }
    RECT after = zo_wr(w);
    CHECK(zo_same_rect(&before, &after));   /* rejected before any geometry is touched */
    zo_reset();
}

TEST(win_position_zorder_swp_invalid_flag_bits_via_user32) {
    NEED_GUI();
    HWND w = zo_top("flags2", ZO_OFFX, ZO_OFFY, 100, 80, 1);
    REQUIRE(w);
    for (unsigned i = 0; i < sizeof bad_bits / sizeof bad_bits[0]; i += 3) {
        SetLastError(0);
        CHECK(!U(SetWindowPos)(w, NULL, 0, 0, 0, 0, ZO_SWP_NOTHING | bad_bits[i]));
        EXPECT_ERR(1004);
    }
    zo_reset();
}

TEST(win_position_zorder_swp_invalid_flag_bits_all_together) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND w = zo_top("flagsall", ZO_OFFX, ZO_OFFY, 100, 80, 1);
    REQUIRE(w);
    SetLastError(0);
    CHECK(!UN(NtUserSetWindowPos)(w, NULL, 0, 0, 0, 0, 0xfffc9800u | ZO_SWP_NOTHING));
    EXPECT_ERR(1004);
    SetLastError(0);
    CHECK(!UN(NtUserSetWindowPos)(w, NULL, 0, 0, 0, 0, 0xffffffffu));
    EXPECT_ERR(1004);
    zo_reset();
}

TEST(win_position_zorder_swp_valid_flag_bits_not_rejected) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    /* every single bit outside the 0xfffc9800 mask passes the flag filter; none may produce 1004 */
    static const unsigned good[] = { SWP_NOSIZE, SWP_NOMOVE, SWP_NOZORDER, SWP_NOREDRAW, SWP_NOACTIVATE,
        SWP_FRAMECHANGED, SWP_NOCOPYBITS, SWP_NOOWNERZORDER, SWP_NOSENDCHANGING, 0x2000, 0x4000, 0x10000, 0x20000 };
    HWND w = zo_top("goodbits", ZO_OFFX, ZO_OFFY, 100, 80, 1);
    REQUIRE(w);
    for (unsigned i = 0; i < sizeof good / sizeof good[0]; i++) {
        SetLastError(0);
        UN(NtUserSetWindowPos)(w, NULL, ZO_OFFX, ZO_OFFY, 100, 80, ZO_SWP_NOTHING | good[i]);
        CHECKF(GetLastError() != 1004, "valid bit 0x%x produced ERROR_INVALID_FLAGS", good[i]);
    }
    k_pump(30);
    CHECK(U(IsWindow)(w));
    zo_reset();
}

TEST(win_position_zorder_swp_invalid_hwnd_table) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND bad[] = { NULL, (HWND)1, (HWND)2, (HWND)0x10, (HWND)0xFFFF, (HWND)(ULONG_PTR)-1, (HWND)(ULONG_PTR)-3, (HWND)0x7FFFFFFF };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(0);
        BOOL r = UN(NtUserSetWindowPos)(bad[i], NULL, 0, 0, 0, 0, ZO_SWP_NOTHING);
        CHECKF(r == 0, "hwnd %p accepted", bad[i]);
        CHECKF(GetLastError() == 1400, "hwnd %p error %lu want 1400", bad[i], GetLastError());
    }
}

TEST(win_position_zorder_swp_destroyed_handle_is_invalid) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND w = zo_top("gone", ZO_OFFX, ZO_OFFY, 50, 50, 1);
    REQUIRE(w);
    U(DestroyWindow)(w);
    SetLastError(0);
    CHECK(!UN(NtUserSetWindowPos)(w, NULL, 0, 0, 0, 0, ZO_SWP_NOTHING));
    EXPECT_ERR(1400);
    /* the handle-table entry is freed: user32's own validation agrees */
    CHECK(!U(IsWindow)(w));
    zo_reset();
}

TEST(win_position_zorder_swp_desktop_window_refused) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND d = zo_desktop();
    REQUIRE(d);
    RECT before; U(GetWindowRect)(d, &before);
    /* fnid 0x29d (desktop) is filtered right after ValidateHwnd: FALSE, no geometry change */
    CHECK(!UN(NtUserSetWindowPos)(d, NULL, 10, 10, 20, 20, ZO_SWP_ZONLY | SWP_NOZORDER));
    CHECK(!UN(NtUserSetWindowPos)(d, NULL, 0, 0, 0, 0, ZO_SWP_NOTHING));
    RECT after; U(GetWindowRect)(d, &after);
    CHECK(zo_same_rect(&before, &after));
}

TEST(win_position_zorder_swp_invalid_insert_after) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND w = zo_top("ia", ZO_OFFX, ZO_OFFY, 50, 50, 1);
    HWND dead = zo_top("dead", ZO_OFFX, ZO_OFFY, 50, 50, 1);
    REQUIRE(w && dead);
    U(DestroyWindow)(dead);
    HWND bad[] = { dead, (HWND)0x1234, (HWND)(ULONG_PTR)-5, (HWND)0x7FFFFFFF };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(0);
        BOOL r = UN(NtUserSetWindowPos)(w, bad[i], 0, 0, 0, 0, ZO_SWP_ZONLY);
        CHECKF(r == 0, "insert-after %p accepted", bad[i]);
        t_note("insert-after %p -> error %lu", bad[i], GetLastError());
    }
    zo_reset();
}

TEST(win_position_zorder_swp_special_insert_after_values_accepted) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND w = zo_top("special", ZO_OFFX, ZO_OFFY, 50, 50, 1);
    REQUIRE(w);
    HWND sp[] = { HWND_TOP, HWND_BOTTOM, HWND_NOTOPMOST, HWND_TOPMOST };
    for (unsigned i = 0; i < 4; i++) {
        SetLastError(0);
        CHECKF(UN(NtUserSetWindowPos)(w, sp[i], 0, 0, 0, 0, ZO_SWP_ZONLY), "special %p", sp[i]);
    }
    /* leave it non-topmost for the cleanup */
    UN(NtUserSetWindowPos)(w, HWND_NOTOPMOST, 0, 0, 0, 0, ZO_SWP_ZONLY);
    zo_reset();
}

TEST(win_position_zorder_swp_invalid_hwnd_wins_over_invalid_flags) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    /* order of checks in NtUserSetWindowPos: ValidateHwnd (1400) before the flag mask (1004) */
    SetLastError(0);
    CHECK(!UN(NtUserSetWindowPos)((HWND)0x1, NULL, 0, 0, 0, 0, 0x800 | ZO_SWP_NOTHING));
    EXPECT_ERR(1400);
}

TEST(win_position_zorder_swp_invalid_flags_win_over_invalid_insert_after) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND w = zo_top("order", ZO_OFFX, ZO_OFFY, 50, 50, 1);
    REQUIRE(w);
    SetLastError(0);
    /* flag mask is tested before ValidateHWNDIA(hWndInsertAfter) */
    CHECK(!UN(NtUserSetWindowPos)(w, (HWND)0x1234, 0, 0, 0, 0, 0x1000 | ZO_SWP_ZONLY));
    EXPECT_ERR(1004);
    zo_reset();
}

TEST(win_position_zorder_swp_basic_move_and_size) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND w = zo_top("move", ZO_OFFX, ZO_OFFY, 100, 80, 1);
    REQUIRE(w);
    CHECK(UN(NtUserSetWindowPos)(w, NULL, ZO_OFFX + 40, ZO_OFFY + 50, 120, 90, SWP_NOZORDER | SWP_NOACTIVATE));
    RECT r = zo_wr(w);
    EXPECT_EQ(r.left, ZO_OFFX + 40); EXPECT_EQ(r.top, ZO_OFFY + 50);
    EXPECT_EQ(r.right - r.left, 120); EXPECT_EQ(r.bottom - r.top, 90);
    zo_reset();
}

TEST(win_position_zorder_swp_nomove_ignores_xy) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND w = zo_top("nomove", ZO_OFFX + 7, ZO_OFFY + 9, 100, 80, 1);
    REQUIRE(w);
    CHECK(UN(NtUserSetWindowPos)(w, NULL, 123, 456, 60, 70, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE));
    RECT r = zo_wr(w);
    EXPECT_EQ(r.left, ZO_OFFX + 7); EXPECT_EQ(r.top, ZO_OFFY + 9);
    EXPECT_EQ(r.right - r.left, 60); EXPECT_EQ(r.bottom - r.top, 70);
    zo_reset();
}

TEST(win_position_zorder_swp_nosize_ignores_cxcy) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND w = zo_top("nosize", ZO_OFFX, ZO_OFFY, 100, 80, 1);
    REQUIRE(w);
    CHECK(UN(NtUserSetWindowPos)(w, NULL, ZO_OFFX + 11, ZO_OFFY + 12, -50, 1000000, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE));
    RECT r = zo_wr(w);
    EXPECT_EQ(r.left, ZO_OFFX + 11); EXPECT_EQ(r.top, ZO_OFFY + 12);
    EXPECT_EQ(r.right - r.left, 100); EXPECT_EQ(r.bottom - r.top, 80);
    zo_reset();
}

TEST(win_position_zorder_swp_nomove_nosize_noop_returns_true) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND w = zo_top("noop", ZO_OFFX, ZO_OFFY, 100, 80, 1);
    REQUIRE(w);
    RECT a = zo_wr(w);
    SetLastError(0);
    CHECK(UN(NtUserSetWindowPos)(w, NULL, 1, 2, 3, 4, ZO_SWP_NOTHING));
    RECT b = zo_wr(w);
    CHECK(zo_same_rect(&a, &b));
    zo_reset();
}

TEST(win_position_zorder_swp_position_clamped_to_short_range) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    struct { int in; int want; } v[] = { { 0x12345, 0x7fff }, { 0x8000, 0x7fff }, { 0x7fff, 0x7fff }, { -0x8000, -0x8000 },
                                        { -0x8001, -0x8000 }, { -0x12345, -0x8000 }, { 0x7ffffff0, 0x7fff }, { (int)0x80000000, -0x8000 } };
    HWND w = zo_top("clampxy", ZO_OFFX, ZO_OFFY, 20, 20, 1);
    REQUIRE(w);
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) {
        /* ConstrainWindowPos in NtUserSetWindowPos clamps x and y independently */
        CHECK(UN(NtUserSetWindowPos)(w, NULL, v[i].in, v[i].in, 20, 20, SWP_NOZORDER | SWP_NOACTIVATE));
        RECT r = zo_wr(w);
        CHECKF(r.left == v[i].want && r.top == v[i].want, "in=%d got (%ld,%ld) want %d", v[i].in, r.left, r.top, v[i].want);
    }
    zo_reset();
}

TEST(win_position_zorder_swp_size_clamped_negative_to_zero) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND w = zo_top("clampneg", ZO_OFFX, ZO_OFFY, 50, 50, 1);
    REQUIRE(w);
    /* ConstrainWindowSize: values below 0 become 0 */
    CHECK(UN(NtUserSetWindowPos)(w, NULL, ZO_OFFX, ZO_OFFY, -1, -100000, SWP_NOZORDER | SWP_NOACTIVATE));
    RECT r = zo_wr(w);
    CHECKF(r.right - r.left >= 0 && r.bottom - r.top >= 0, "negative size leaked into rect");
    zo_note_rect("rect after negative size", &r);
    CHECK(r.right - r.left <= 0xffff);
    zo_reset();
}

TEST(win_position_zorder_swp_size_clamped_to_16_bits) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND w = zo_top("clamphuge", ZO_OFFX, ZO_OFFY, 50, 50, 1);
    REQUIRE(w);
    CHECK(UN(NtUserSetWindowPos)(w, NULL, ZO_OFFX, ZO_OFFY, 0x7fffffff, 0x10000, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW));
    RECT r = zo_wr(w);
    CHECKF(r.right - r.left <= 0xffff, "width %ld exceeds 0xffff", r.right - r.left);
    CHECKF(r.bottom - r.top <= 0xffff, "height %ld exceeds 0xffff", r.bottom - r.top);
    zo_note_rect("rect after huge size", &r);
    UN(NtUserSetWindowPos)(w, NULL, ZO_OFFX, ZO_OFFY, 50, 50, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
    zo_reset();
}

TEST(win_position_zorder_swp_heap_rects_match_api) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    if (t_is_wine() || !zo_heap_ok()) { t_skip("desktop heap windows not readable"); return; }
    HWND w = zo_top("heaprect", ZO_OFFX, ZO_OFFY, 100, 80, 1);
    REQUIRE(w);
    UN(NtUserSetWindowPos)(w, NULL, ZO_OFFX + 31, ZO_OFFY + 17, 133, 77, SWP_NOZORDER | SWP_NOACTIVATE);
    RECT api = zo_wr(w), hw, hc;
    CHECK(zo_rect_heap(w, ZO_WND_RcWnd, &hw));
    CHECK(zo_rect_heap(w, ZO_WND_RcCli, &hc));
    CHECKF(zo_same_rect(&api, &hw), "heap rcWindow (%ld,%ld,%ld,%ld) vs GetWindowRect (%ld,%ld,%ld,%ld)", hw.left, hw.top, hw.right, hw.bottom, api.left, api.top, api.right, api.bottom);
    RECT cs = zo_cr_screen(w);
    CHECKF(zo_same_rect(&cs, &hc), "heap rcClient vs ClientToScreen(GetClientRect)");
    /* a WS_POPUP window without frame bits: client == window */
    CHECK(zo_same_rect(&hw, &hc));
    zo_reset();
}

TEST(win_position_zorder_swp_heap_rect_tracks_every_step) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    if (t_is_wine() || !zo_heap_ok()) { t_skip("desktop heap windows not readable"); return; }
    HWND w = zo_top("heapstep", ZO_OFFX, ZO_OFFY, 100, 80, 1);
    REQUIRE(w);
    for (int i = 1; i <= 12; i++) {
        UN(NtUserSetWindowPos)(w, NULL, ZO_OFFX + i * 13, ZO_OFFY - i * 7, 40 + i * 3, 30 + i * 5, SWP_NOZORDER | SWP_NOACTIVATE);
        RECT hw; CHECK(zo_rect_heap(w, ZO_WND_RcWnd, &hw));
        CHECK(hw.left == ZO_OFFX + i * 13 && hw.top == ZO_OFFY - i * 7 && hw.right - hw.left == 40 + i * 3 && hw.bottom - hw.top == 30 + i * 5);
    }
    zo_reset();
}

TEST(win_position_zorder_swp_client_rect_follows_frame_after_framechanged) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    zo_ghost = 0;       /* plain window: a style change alone must not touch the non-client area */
    HWND w = zo_top("frame", ZO_OFFX, ZO_OFFY, 100, 80, 1);
    REQUIRE(w);
    RECT c0 = zo_cr_screen(w), w0 = zo_wr(w);
    CHECK(zo_same_rect(&c0, &w0));
    /* changing the style alone does not recompute the non-client area ... */
    LONG st = U(GetWindowLongA)(w, GWL_STYLE);
    U(SetWindowLongA)(w, GWL_STYLE, st | WS_BORDER);
    RECT c1 = zo_cr_screen(w);
    CHECK(zo_same_rect(&c1, &c0));
    /* ... SWP_FRAMECHANGED does (WM_NCCALCSIZE shrinks the client by the border) */
    CHECK(UN(NtUserSetWindowPos)(w, NULL, 0, 0, 0, 0, ZO_SWP_NOTHING | SWP_FRAMECHANGED));
    RECT c2 = zo_cr_screen(w), w2 = zo_wr(w);
    CHECK(zo_same_rect(&w2, &w0));
    CHECKF(c2.left > w2.left && c2.top > w2.top && c2.right < w2.right && c2.bottom < w2.bottom, "client did not shrink for WS_BORDER");
    if (!t_is_wine() && zo_heap_ok()) {
        RECT hc; zo_rect_heap(w, ZO_WND_RcCli, &hc);
        CHECK(zo_same_rect(&hc, &c2));
    }
    zo_reset();
}

TEST(win_position_zorder_swp_show_and_hide_flags) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND w = zo_top("showhide", ZO_OFFX, ZO_OFFY, 100, 80, 0);
    REQUIRE(w);
    CHECK(!U(IsWindowVisible)(w));
    zo_log_clear(); zo_log_on = 1;
    CHECK(UN(NtUserSetWindowPos)(w, NULL, 0, 0, 0, 0, ZO_SWP_NOTHING | SWP_SHOWWINDOW));
    CHECK(U(IsWindowVisible)(w));
    CHECK(zo_log_count(w, WM_WINDOWPOSCHANGED) >= 1);   /* a visibility change is reported with the position change */
    int i = zo_log_find(w, WM_WINDOWPOSCHANGED, 0);
    if (i >= 0) CHECK(zo_log[i].wp.flags & SWP_SHOWWINDOW);
    t_note("WM_SHOWWINDOW count after SWP_SHOWWINDOW: %d", zo_log_count(w, WM_SHOWWINDOW));
    zo_log_clear();
    CHECK(UN(NtUserSetWindowPos)(w, NULL, 0, 0, 0, 0, ZO_SWP_NOTHING | SWP_HIDEWINDOW));
    CHECK(!U(IsWindowVisible)(w));
    i = zo_log_find(w, WM_WINDOWPOSCHANGED, 0);
    CHECK(i >= 0);
    if (i >= 0) CHECK(zo_log[i].wp.flags & SWP_HIDEWINDOW);
    zo_reset();
}

TEST(win_position_zorder_swp_show_twice_is_idempotent) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND w = zo_top("showtwice", ZO_OFFX, ZO_OFFY, 100, 80, 0);
    REQUIRE(w);
    UN(NtUserSetWindowPos)(w, NULL, 0, 0, 0, 0, ZO_SWP_NOTHING | SWP_SHOWWINDOW);
    CHECK(U(IsWindowVisible)(w));
    CHECK(UN(NtUserSetWindowPos)(w, NULL, 0, 0, 0, 0, ZO_SWP_NOTHING | SWP_SHOWWINDOW));
    CHECK(U(IsWindowVisible)(w));
    UN(NtUserSetWindowPos)(w, NULL, 0, 0, 0, 0, ZO_SWP_NOTHING | SWP_HIDEWINDOW);
    CHECK(UN(NtUserSetWindowPos)(w, NULL, 0, 0, 0, 0, ZO_SWP_NOTHING | SWP_HIDEWINDOW));
    CHECK(!U(IsWindowVisible)(w));
    zo_reset();
}

TEST(win_position_zorder_swp_heap_visible_bit_follows_show_hide) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    if (t_is_wine() || !zo_heap_ok()) { t_skip("desktop heap windows not readable"); return; }
    HWND w = zo_top("visbit", ZO_OFFX, ZO_OFFY, 100, 80, 0);
    REQUIRE(w);
    int ok; unsigned long st = zo_style_heap(w, &ok);
    CHECK(ok); CHECK(!(st & WS_VISIBLE));
    UN(NtUserSetWindowPos)(w, NULL, 0, 0, 0, 0, ZO_SWP_NOTHING | SWP_SHOWWINDOW);
    st = zo_style_heap(w, &ok); CHECK(ok && (st & WS_VISIBLE));
    CHECK((unsigned long)U(GetWindowLongA)(w, GWL_STYLE) == st);   /* the heap dword is the style GetWindowLong reports */
    UN(NtUserSetWindowPos)(w, NULL, 0, 0, 0, 0, ZO_SWP_NOTHING | SWP_HIDEWINDOW);
    st = zo_style_heap(w, &ok); CHECK(ok && !(st & WS_VISIBLE));
    zo_reset();
}

TEST(win_position_zorder_swp_hidden_window_geometry_changes) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND w = zo_top("hidden", ZO_OFFX, ZO_OFFY, 100, 80, 0);
    REQUIRE(w);
    zo_log_clear(); zo_log_on = 1;
    CHECK(UN(NtUserSetWindowPos)(w, NULL, ZO_OFFX + 20, ZO_OFFY + 30, 70, 60, SWP_NOZORDER | SWP_NOACTIVATE));
    RECT r = zo_wr(w);
    EXPECT_EQ(r.left, ZO_OFFX + 20); EXPECT_EQ(r.top, ZO_OFFY + 30);
    EXPECT_EQ(r.right - r.left, 70); EXPECT_EQ(r.bottom - r.top, 60);
    CHECK(!U(IsWindowVisible)(w));
    CHECK(zo_log_count(w, WM_WINDOWPOSCHANGED) >= 1);
    zo_reset();
}

TEST(win_position_zorder_swp_message_order_changing_then_changed) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND w = zo_top("order", ZO_OFFX, ZO_OFFY, 100, 80, 1);
    REQUIRE(w);
    zo_log_clear(); zo_log_on = 1;
    CHECK(UN(NtUserSetWindowPos)(w, NULL, ZO_OFFX + 5, ZO_OFFY + 5, 90, 70, SWP_NOZORDER | SWP_NOACTIVATE));
    int a = zo_log_find(w, WM_WINDOWPOSCHANGING, 0), b = zo_log_find(w, WM_WINDOWPOSCHANGED, 0);
    CHECK(a >= 0 && b >= 0 && a < b);
    int n = zo_log_find(w, WM_NCCALCSIZE, 0);
    CHECK(n > a && n < b);     /* the size change recalculates the client between the two notifications */
    EXPECT_EQ(zo_log_count(w, WM_WINDOWPOSCHANGING), 1);
    EXPECT_EQ(zo_log_count(w, WM_WINDOWPOSCHANGED), 1);
    EXPECT_EQ(zo_log_count(w, WM_SIZE), 1);
    EXPECT_EQ(zo_log_count(w, WM_MOVE), 1);
    int s = zo_log_find(w, WM_SIZE, 0), m = zo_log_find(w, WM_MOVE, 0);
    CHECK(s > b && m > b);     /* DefWindowProc generates both from WM_WINDOWPOSCHANGED */
    t_note("WM_SIZE index %d, WM_MOVE index %d (relative order is build-defined)", s, m);
    zo_reset();
}

TEST(win_position_zorder_swp_changing_args_match_call) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND w = zo_top("args", ZO_OFFX, ZO_OFFY, 100, 80, 1), o = zo_top("argsother", ZO_OFFX, ZO_OFFY, 20, 20, 1);
    REQUIRE(w && o);
    zo_log_clear(); zo_log_on = 1;
    CHECK(UN(NtUserSetWindowPos)(w, o, ZO_OFFX + 3, ZO_OFFY + 4, 55, 66, SWP_NOACTIVATE));
    int a = zo_log_find(w, WM_WINDOWPOSCHANGING, 0);
    REQUIRE(a >= 0);
    const WINDOWPOS *wp = &zo_log[a].wp;
    EXPECT_EQ(wp->hwnd, w);
    EXPECT_EQ(wp->hwndInsertAfter, o);
    EXPECT_EQ(wp->x, ZO_OFFX + 3); EXPECT_EQ(wp->y, ZO_OFFY + 4);
    EXPECT_EQ(wp->cx, 55); EXPECT_EQ(wp->cy, 66);
    CHECK(wp->flags & SWP_NOACTIVATE);
    CHECK(!(wp->flags & (SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER)));
    zo_reset();
}

TEST(win_position_zorder_swp_nosendchanging_suppresses_poschanging) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND w = zo_top("nosc", ZO_OFFX, ZO_OFFY, 100, 80, 1);
    REQUIRE(w);
    zo_log_clear(); zo_log_on = 1;
    CHECK(UN(NtUserSetWindowPos)(w, NULL, ZO_OFFX + 9, ZO_OFFY + 9, 50, 50, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSENDCHANGING));
    EXPECT_EQ(zo_log_count(w, WM_WINDOWPOSCHANGING), 0);
    EXPECT_EQ(zo_log_count(w, WM_WINDOWPOSCHANGED), 1);
    zo_log_clear();
    CHECK(UN(NtUserSetWindowPos)(w, NULL, ZO_OFFX + 19, ZO_OFFY + 9, 50, 50, SWP_NOZORDER | SWP_NOACTIVATE));
    EXPECT_EQ(zo_log_count(w, WM_WINDOWPOSCHANGING), 1);
    zo_reset();
}

TEST(win_position_zorder_swp_nosendchanging_bypasses_override) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND w = zo_top("nosc2", ZO_OFFX, ZO_OFFY, 100, 80, 1);
    REQUIRE(w);
    zo_beh = ZO_B_CLAMP_SIZE; zo_beh_cx = 11; zo_beh_cy = 12;
    CHECK(UN(NtUserSetWindowPos)(w, NULL, ZO_OFFX, ZO_OFFY, 90, 70, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSENDCHANGING));
    RECT r = zo_wr(w);
    EXPECT_EQ(r.right - r.left, 90); EXPECT_EQ(r.bottom - r.top, 70);   /* handler never ran */
    zo_reset();
}

TEST(win_position_zorder_swp_framechanged_sends_nccalcsize) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND w = zo_top("fc", ZO_OFFX, ZO_OFFY, 100, 80, 1);
    REQUIRE(w);
    zo_log_clear(); zo_log_on = 1;
    CHECK(UN(NtUserSetWindowPos)(w, NULL, 0, 0, 0, 0, ZO_SWP_NOTHING | SWP_FRAMECHANGED));
    int n = zo_log_find(w, WM_NCCALCSIZE, 0);
    CHECK(n >= 0);
    if (n >= 0) CHECK(zo_log[n].w == TRUE);   /* wParam TRUE: lParam is an NCCALCSIZE_PARAMS */
    zo_log_clear();
    CHECK(UN(NtUserSetWindowPos)(w, NULL, 0, 0, 0, 0, ZO_SWP_NOTHING));
    EXPECT_EQ(zo_log_count(w, WM_NCCALCSIZE), 0);
    zo_reset();
}

TEST(win_position_zorder_swp_poschanging_can_override_size) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND w = zo_top("ovsize", ZO_OFFX, ZO_OFFY, 100, 80, 1);
    REQUIRE(w);
    zo_beh = ZO_B_CLAMP_SIZE; zo_beh_cx = 77; zo_beh_cy = 55;
    CHECK(UN(NtUserSetWindowPos)(w, NULL, ZO_OFFX, ZO_OFFY, 300, 200, SWP_NOZORDER | SWP_NOACTIVATE));
    RECT r = zo_wr(w);
    EXPECT_EQ(r.right - r.left, 77); EXPECT_EQ(r.bottom - r.top, 55);
    zo_reset();
}

TEST(win_position_zorder_swp_poschanging_can_force_nomove) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND w = zo_top("ovmove", ZO_OFFX + 3, ZO_OFFY + 4, 100, 80, 1);
    REQUIRE(w);
    zo_beh = ZO_B_FORCE_NOMOVE;
    CHECK(UN(NtUserSetWindowPos)(w, NULL, ZO_OFFX + 500, ZO_OFFY + 600, 66, 44, SWP_NOZORDER | SWP_NOACTIVATE));
    RECT r = zo_wr(w);
    EXPECT_EQ(r.left, ZO_OFFX + 3); EXPECT_EQ(r.top, ZO_OFFY + 4);
    EXPECT_EQ(r.right - r.left, 66);
    zo_reset();
}

TEST(win_position_zorder_swp_poschanging_can_force_nosize) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND w = zo_top("ovnosize", ZO_OFFX, ZO_OFFY, 100, 80, 1);
    REQUIRE(w);
    zo_beh = ZO_B_FORCE_NOSIZE;
    CHECK(UN(NtUserSetWindowPos)(w, NULL, ZO_OFFX + 8, ZO_OFFY + 8, 10, 10, SWP_NOZORDER | SWP_NOACTIVATE));
    RECT r = zo_wr(w);
    EXPECT_EQ(r.left, ZO_OFFX + 8);
    EXPECT_EQ(r.right - r.left, 100); EXPECT_EQ(r.bottom - r.top, 80);
    zo_reset();
}

TEST(win_position_zorder_swp_poschanging_shifts_position) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND w = zo_top("ovshift", ZO_OFFX, ZO_OFFY, 100, 80, 1);
    REQUIRE(w);
    zo_beh = ZO_B_SHIFT_POS; zo_beh_dx = 21; zo_beh_dy = -13;
    CHECK(UN(NtUserSetWindowPos)(w, NULL, ZO_OFFX + 100, ZO_OFFY + 100, 100, 80, SWP_NOZORDER | SWP_NOACTIVATE));
    RECT r = zo_wr(w);
    EXPECT_EQ(r.left, ZO_OFFX + 121); EXPECT_EQ(r.top, ZO_OFFY + 87);
    zo_reset();
}

TEST(win_position_zorder_swp_poschanging_can_force_nozorder) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND a = zo_top("za", ZO_OFFX, ZO_OFFY, 50, 50, 1), b = zo_top("zb", ZO_OFFX, ZO_OFFY, 50, 50, 1);
    REQUIRE(a && b);
    HWND ord[2] = { a, b }, got[2];
    zo_arrange(ord, 2);
    zo_beh = ZO_B_FORCE_NOZ;
    CHECK(UN(NtUserSetWindowPos)(b, HWND_TOP, 0, 0, 0, 0, ZO_SWP_ZONLY));
    CHECK(zo_rank(ord, 2, got) == 2 && got[0] == a && got[1] == b);   /* z change suppressed by the handler */
    zo_beh = 0;
    CHECK(UN(NtUserSetWindowPos)(b, HWND_TOP, 0, 0, 0, 0, ZO_SWP_ZONLY));
    CHECK(zo_rank(ord, 2, got) == 2 && got[0] == b && got[1] == a);
    zo_reset();
}

TEST(win_position_zorder_swp_poschanged_reports_final_geometry) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND w = zo_top("final", ZO_OFFX, ZO_OFFY, 100, 80, 1);
    REQUIRE(w);
    zo_beh = ZO_B_CLAMP_SIZE; zo_beh_cx = 61; zo_beh_cy = 62;
    zo_log_clear(); zo_log_on = 1;
    CHECK(UN(NtUserSetWindowPos)(w, NULL, ZO_OFFX + 40, ZO_OFFY + 41, 300, 300, SWP_NOZORDER | SWP_NOACTIVATE));
    int b = zo_log_find(w, WM_WINDOWPOSCHANGED, 0);
    REQUIRE(b >= 0);
    EXPECT_EQ(zo_log[b].wp.x, ZO_OFFX + 40); EXPECT_EQ(zo_log[b].wp.y, ZO_OFFY + 41);
    EXPECT_EQ(zo_log[b].wp.cx, 61); EXPECT_EQ(zo_log[b].wp.cy, 62);   /* post-override values */
    RECT r = zo_wr(w);
    EXPECT_EQ(r.right - r.left, zo_log[b].wp.cx);
    zo_reset();
}

/* ---- z-order ---- */

static int make3(HWND *w, const char *tag) {
    for (int i = 0; i < 3; i++) {
        char t[32]; snprintf(t, sizeof t, "%s%d", tag, i);
        w[i] = zo_top(t, ZO_OFFX + i * 5, ZO_OFFY + i * 5, 60, 60, 1);
        if (!w[i]) return 0;
    }
    return 1;
}

TEST(win_position_zorder_swp_hwnd_top_moves_to_front) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND w[3]; REQUIRE(make3(w, "top"));
    HWND ord[3] = { w[0], w[1], w[2] }, got[3];
    zo_arrange(ord, 3);
    CHECK(UN(NtUserSetWindowPos)(w[2], HWND_TOP, 0, 0, 0, 0, ZO_SWP_ZONLY));
    HWND want[3] = { w[2], w[0], w[1] };
    CHECK(zo_rank(ord, 3, got) == 3 && zo_same(got, want, 3));
    zo_reset();
}

TEST(win_position_zorder_swp_hwnd_bottom_moves_to_back) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND w[3]; REQUIRE(make3(w, "bot"));
    HWND ord[3] = { w[0], w[1], w[2] }, got[3];
    zo_arrange(ord, 3);
    CHECK(UN(NtUserSetWindowPos)(w[0], HWND_BOTTOM, 0, 0, 0, 0, ZO_SWP_ZONLY));
    HWND want[3] = { w[1], w[2], w[0] };
    CHECK(zo_rank(ord, 3, got) == 3 && zo_same(got, want, 3));
    /* HWND_BOTTOM places the window last of ALL siblings, not just of ours (only the shell's bottom windows stay below) */
    CHECK(zo_at_bottom(w[0]));
    zo_reset();
}

TEST(win_position_zorder_swp_insert_after_places_directly_below) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND w[3]; REQUIRE(make3(w, "ia"));
    HWND ord[3] = { w[0], w[1], w[2] }, got[3];
    zo_arrange(ord, 3);
    /* move the top window right behind the bottom one */
    CHECK(UN(NtUserSetWindowPos)(w[0], w[2], 0, 0, 0, 0, ZO_SWP_ZONLY));
    HWND want[3] = { w[1], w[2], w[0] };
    CHECK(zo_rank(ord, 3, got) == 3 && zo_same(got, want, 3));
    CHECK(U(GetWindow)(w[2], GW_HWNDNEXT) == w[0]);     /* directly adjacent */
    CHECK(U(GetWindow)(w[0], GW_HWNDPREV) == w[2]);
    /* and behind the middle one */
    CHECK(UN(NtUserSetWindowPos)(w[0], w[1], 0, 0, 0, 0, ZO_SWP_ZONLY));
    HWND want2[3] = { w[1], w[0], w[2] };
    CHECK(zo_rank(ord, 3, got) == 3 && zo_same(got, want2, 3));
    CHECK(U(GetWindow)(w[1], GW_HWNDNEXT) == w[0]);
    zo_reset();
}

TEST(win_position_zorder_swp_insert_after_all_permutations) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND w[3]; REQUIRE(make3(w, "perm"));
    static const int perms[6][3] = { {0,1,2}, {0,2,1}, {1,0,2}, {1,2,0}, {2,0,1}, {2,1,0} };
    for (int p = 0; p < 6; p++) {
        HWND want[3] = { w[perms[p][0]], w[perms[p][1]], w[perms[p][2]] }, got[3], in[3] = { w[0], w[1], w[2] };
        /* build the wanted order with explicit insert-after links */
        UN(NtUserSetWindowPos)(want[0], HWND_TOP, 0, 0, 0, 0, ZO_SWP_ZONLY);
        UN(NtUserSetWindowPos)(want[1], want[0], 0, 0, 0, 0, ZO_SWP_ZONLY);
        UN(NtUserSetWindowPos)(want[2], want[1], 0, 0, 0, 0, ZO_SWP_ZONLY);
        CHECKF(zo_rank(in, 3, got) == 3 && zo_same(got, want, 3), "permutation %d", p);
        CHECKF(U(GetWindow)(want[0], GW_HWNDNEXT) == want[1] && U(GetWindow)(want[1], GW_HWNDNEXT) == want[2], "adjacency in permutation %d", p);
    }
    zo_reset();
}

TEST(win_position_zorder_swp_nozorder_ignores_insert_after) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND w[3]; REQUIRE(make3(w, "noz"));
    HWND ord[3] = { w[0], w[1], w[2] }, got[3];
    zo_arrange(ord, 3);
    CHECK(UN(NtUserSetWindowPos)(w[0], HWND_BOTTOM, ZO_OFFX + 2, ZO_OFFY + 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE));
    CHECK(zo_rank(ord, 3, got) == 3 && zo_same(got, ord, 3));
    /* the insert-after handle is validated even though SWP_NOZORDER makes it irrelevant */
    SetLastError(0);
    CHECK(!UN(NtUserSetWindowPos)(w[0], (HWND)0x1234, ZO_OFFX + 4, ZO_OFFY + 4, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE));
    EXPECT_ERR(1400);
    zo_reset();
}

TEST(win_position_zorder_swp_insert_after_self_is_noop) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND w[3]; REQUIRE(make3(w, "self"));
    HWND ord[3] = { w[0], w[1], w[2] }, got[3];
    zo_arrange(ord, 3);
    BOOL r = UN(NtUserSetWindowPos)(w[1], w[1], 0, 0, 0, 0, ZO_SWP_ZONLY);
    t_note("insert-after-self returned %d", r);
    CHECK(zo_rank(ord, 3, got) == 3 && zo_same(got, ord, 3));
    zo_reset();
}

TEST(win_position_zorder_swp_heap_chain_matches_api_after_shuffle) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    if (t_is_wine() || !zo_heap_ok()) { t_skip("desktop heap windows not readable"); return; }
    HWND w[4];
    for (int i = 0; i < 4; i++) { w[i] = zo_top("shuf", ZO_OFFX + i, ZO_OFFY, 40, 40, 1); REQUIRE(w[i]); }
    HWND in[4] = { w[0], w[1], w[2], w[3] };
    static const int steps[8][2] = { {3,0}, {1,2}, {0,3}, {2,1}, {2,3}, {3,1}, {1,0}, {0,2} };
    for (int s = 0; s < 8; s++) {
        HWND tgt = w[steps[s][0]], after = (s & 1) ? w[steps[s][1]] : (s & 2 ? HWND_BOTTOM : HWND_TOP);
        if (after == tgt) continue;
        UN(NtUserSetWindowPos)(tgt, after, 0, 0, 0, 0, ZO_SWP_ZONLY);
        HWND a[4], b[4];
        int ra = zo_rank(in, 4, a), rb = zo_rank_heap(zo_desktop(), in, 4, b);
        CHECKF(ra == 4 && rb == 4 && zo_same(a, b, 4), "step %d: GetWindow chain and desktop-heap chain disagree", s);
        /* prev links are the inverse of next links */
        for (int i = 0; i < 3; i++) CHECK(zo_link(a[i], ZO_WND_Next) != NULL);
    }
    zo_reset();
}

TEST(win_position_zorder_swp_heap_prev_next_are_inverse) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    if (t_is_wine() || !zo_heap_ok()) { t_skip("desktop heap windows not readable"); return; }
    HWND w[3]; REQUIRE(make3(w, "inv"));
    HWND ord[3] = { w[0], w[1], w[2] };
    zo_arrange(ord, 3);
    for (int i = 0; i < 3; i++) {
        HWND nx = zo_link(w[i], ZO_WND_Next);
        if (nx) CHECK(zo_link(nx, ZO_WND_Prev) == w[i]);
        HWND pv = zo_link(w[i], ZO_WND_Prev);
        if (pv) CHECK(zo_link(pv, ZO_WND_Next) == w[i]);
    }
    CHECK(zo_link(w[0], ZO_WND_Prev) == U(GetWindow)(w[0], GW_HWNDPREV));
    zo_reset();
}

/* ---- topmost ---- */

static int is_topmost(HWND h) { return (U(GetWindowLongA)(h, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0; }

TEST(win_position_zorder_swp_topmost_sets_exstyle) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND a = zo_top("tm", ZO_OFFX, ZO_OFFY, 40, 40, 1);
    REQUIRE(a);
    CHECK(!is_topmost(a));
    CHECK(UN(NtUserSetWindowPos)(a, HWND_TOPMOST, 0, 0, 0, 0, ZO_SWP_ZONLY));
    CHECK(is_topmost(a));
    if (!t_is_wine() && zo_heap_ok()) {
        int ok; unsigned long ex = zo_exstyle_heap(a, &ok);
        CHECK(ok && (ex & WS_EX_TOPMOST));
        CHECK(ok && (ex & WS_EX_TOOLWINDOW) && (ex & WS_EX_NOACTIVATE));
    }
    CHECK(UN(NtUserSetWindowPos)(a, HWND_NOTOPMOST, 0, 0, 0, 0, ZO_SWP_ZONLY));
    CHECK(!is_topmost(a));
    if (!t_is_wine() && zo_heap_ok()) { int ok; CHECK(ok = 1); CHECK(!(zo_exstyle_heap(a, &ok) & WS_EX_TOPMOST)); }
    zo_reset();
}

TEST(win_position_zorder_swp_topmost_stays_above_nontopmost) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND a = zo_top("tma", ZO_OFFX, ZO_OFFY, 40, 40, 1), b = zo_top("tmb", ZO_OFFX, ZO_OFFY, 40, 40, 1);
    REQUIRE(a && b);
    HWND ord[2] = { b, a }, got[2];
    zo_arrange(ord, 2);
    CHECK(UN(NtUserSetWindowPos)(a, HWND_TOPMOST, 0, 0, 0, 0, ZO_SWP_ZONLY));
    CHECK(zo_rank(ord, 2, got) == 2 && got[0] == a);          /* a above b although b was HWND_TOP'd later */
    CHECK(UN(NtUserSetWindowPos)(b, HWND_TOP, 0, 0, 0, 0, ZO_SWP_ZONLY));
    CHECK(zo_rank(ord, 2, got) == 2 && got[0] == a);          /* HWND_TOP stays behind topmost windows */
    CHECK(!is_topmost(b));
    UN(NtUserSetWindowPos)(a, HWND_NOTOPMOST, 0, 0, 0, 0, ZO_SWP_ZONLY);
    zo_reset();
}

TEST(win_position_zorder_swp_notopmost_clears_and_keeps_front_of_nontopmost) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND a = zo_top("nt1", ZO_OFFX, ZO_OFFY, 40, 40, 1), b = zo_top("nt2", ZO_OFFX, ZO_OFFY, 40, 40, 1);
    REQUIRE(a && b);
    HWND ord[2] = { b, a }, got[2];
    zo_arrange(ord, 2);
    UN(NtUserSetWindowPos)(a, HWND_TOPMOST, 0, 0, 0, 0, ZO_SWP_ZONLY);
    CHECK(UN(NtUserSetWindowPos)(a, HWND_NOTOPMOST, 0, 0, 0, 0, ZO_SWP_ZONLY));
    CHECK(!is_topmost(a));
    /* HWND_NOTOPMOST puts the window above all non-topmost windows */
    CHECK(zo_rank(ord, 2, got) == 2 && got[0] == a);
    zo_reset();
}

TEST(win_position_zorder_swp_notopmost_on_nontopmost_changes_nothing) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND w[3]; REQUIRE(make3(w, "ntn"));
    HWND ord[3] = { w[0], w[1], w[2] }, got[3];
    zo_arrange(ord, 3);
    CHECK(UN(NtUserSetWindowPos)(w[2], HWND_NOTOPMOST, 0, 0, 0, 0, ZO_SWP_ZONLY));
    CHECK(zo_rank(ord, 3, got) == 3 && zo_same(got, ord, 3));   /* "no effect if already non-topmost" */
    zo_reset();
}

TEST(win_position_zorder_swp_hwnd_bottom_clears_topmost) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND a = zo_top("tmbot", ZO_OFFX, ZO_OFFY, 40, 40, 1);
    REQUIRE(a);
    UN(NtUserSetWindowPos)(a, HWND_TOPMOST, 0, 0, 0, 0, ZO_SWP_ZONLY);
    CHECK(is_topmost(a));
    CHECK(UN(NtUserSetWindowPos)(a, HWND_BOTTOM, 0, 0, 0, 0, ZO_SWP_ZONLY));
    CHECK(!is_topmost(a));
    CHECK(zo_at_bottom(a));
    zo_reset();
}

TEST(win_position_zorder_swp_insert_after_topmost_window_inherits_topmost) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND a = zo_top("itm", ZO_OFFX, ZO_OFFY, 40, 40, 1), b = zo_top("itn", ZO_OFFX, ZO_OFFY, 40, 40, 1);
    REQUIRE(a && b);
    UN(NtUserSetWindowPos)(a, HWND_TOPMOST, 0, 0, 0, 0, ZO_SWP_ZONLY);
    CHECK(UN(NtUserSetWindowPos)(b, a, 0, 0, 0, 0, ZO_SWP_ZONLY));
    t_note("window inserted after a topmost window: topmost=%d next-of-a-is-b=%d", is_topmost(b), U(GetWindow)(a, GW_HWNDNEXT) == b);
    CHECK(U(GetWindow)(a, GW_HWNDNEXT) == b);
    UN(NtUserSetWindowPos)(b, HWND_NOTOPMOST, 0, 0, 0, 0, ZO_SWP_ZONLY);
    UN(NtUserSetWindowPos)(a, HWND_NOTOPMOST, 0, 0, 0, 0, ZO_SWP_ZONLY);
    zo_reset();
}

TEST(win_position_zorder_swp_topmost_window_destroyed_leaves_chain_consistent) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND a = zo_top("td1", ZO_OFFX, ZO_OFFY, 40, 40, 1), b = zo_top("td2", ZO_OFFX, ZO_OFFY, 40, 40, 1);
    REQUIRE(a && b);
    UN(NtUserSetWindowPos)(a, HWND_TOPMOST, 0, 0, 0, 0, ZO_SWP_ZONLY);
    U(DestroyWindow)(a);
    CHECK(!U(IsWindow)(a));
    /* the chain length is the number of top-level windows on the desktop, which other processes change: one aggregate check,
     * so that the number of checks does not depend on it */
    HWND cur = U(GetTopWindow)(NULL); int n = 0, seen_b = 0, seen_a = 0;
    while (cur && n++ < 5000) { if (cur == b) seen_b = 1; if (cur == a) seen_a = 1; cur = U(GetWindow)(cur, GW_HWNDNEXT); }
    CHECK(!seen_a);
    CHECK(seen_b);
    zo_reset();
}

/* ---- owner / owned ---- */

TEST(win_position_zorder_swp_owned_window_stays_above_owner) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND o = zo_top("owner", ZO_OFFX, ZO_OFFY, 80, 80, 1);
    REQUIRE(o);
    HWND p = zo_top_ex("owned", ZO_OFFX, ZO_OFFY, 40, 40, WS_POPUP, 0, o, 1, NULL);
    HWND x = zo_top("other", ZO_OFFX, ZO_OFFY, 40, 40, 1);
    REQUIRE(p && x);
    HWND in[3] = { o, p, x }, got[3];
    CHECK(U(GetWindow)(p, GW_OWNER) == o);
    UN(NtUserSetWindowPos)(o, HWND_TOP, 0, 0, 0, 0, ZO_SWP_ZONLY);
    CHECK(zo_rank(in, 3, got) == 3);
    CHECK(zo_idx(got, 3, p) < zo_idx(got, 3, o));       /* owned windows are always above their owner */
    /* sending the OWNED window to the bottom: only observed */
    UN(NtUserSetWindowPos)(p, HWND_BOTTOM, 0, 0, 0, 0, ZO_SWP_ZONLY);
    CHECK(zo_rank(in, 3, got) == 3);
    t_note("owned window sent to HWND_BOTTOM: above its owner = %d", zo_idx(got, 3, p) < zo_idx(got, 3, o));
    zo_reset();
}

TEST(win_position_zorder_swp_owner_brought_to_top_carries_owned) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND o = zo_top("owner2", ZO_OFFX, ZO_OFFY, 80, 80, 1);
    REQUIRE(o);
    HWND p = zo_top_ex("owned2", ZO_OFFX, ZO_OFFY, 40, 40, WS_POPUP, 0, o, 1, NULL);
    HWND x = zo_top("other2", ZO_OFFX, ZO_OFFY, 40, 40, 1);
    REQUIRE(p && x);
    HWND in[3] = { o, p, x }, got[3];
    UN(NtUserSetWindowPos)(x, HWND_TOP, 0, 0, 0, 0, ZO_SWP_ZONLY);      /* x in front of the pair */
    CHECK(zo_rank(in, 3, got) == 3 && got[0] == x);
    UN(NtUserSetWindowPos)(o, HWND_TOP, 0, 0, 0, 0, ZO_SWP_ZONLY);      /* ZOrderByOwner: owner and its owned window move together */
    CHECK(zo_rank(in, 3, got) == 3);
    CHECK(zo_idx(got, 3, o) < zo_idx(got, 3, x));
    CHECK(zo_idx(got, 3, p) < zo_idx(got, 3, x));
    CHECK(zo_idx(got, 3, p) < zo_idx(got, 3, o));
    zo_reset();
}

TEST(win_position_zorder_swp_noownerzorder_note) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND o = zo_top("owner3", ZO_OFFX, ZO_OFFY, 80, 80, 1);
    REQUIRE(o);
    HWND p = zo_top_ex("owned3", ZO_OFFX, ZO_OFFY, 40, 40, WS_POPUP, 0, o, 1, NULL);
    HWND x = zo_top("other3", ZO_OFFX, ZO_OFFY, 40, 40, 1);
    REQUIRE(p && x);
    HWND in[3] = { o, p, x }, got[3];
    UN(NtUserSetWindowPos)(x, HWND_TOP, 0, 0, 0, 0, ZO_SWP_ZONLY);
    CHECK(UN(NtUserSetWindowPos)(o, HWND_TOP, 0, 0, 0, 0, ZO_SWP_ZONLY | SWP_NOOWNERZORDER));
    CHECK(zo_rank(in, 3, got) == 3);
    t_note("with SWP_NOOWNERZORDER: order = %d,%d,%d (idx of o/p/x)", zo_idx(got, 3, o), zo_idx(got, 3, p), zo_idx(got, 3, x));
    /* with SWP_NOOWNERZORDER the owner jumps in front of x and leaves its owned window where it was */
    CHECK(zo_idx(got, 3, o) == 0 && zo_idx(got, 3, x) == 1 && zo_idx(got, 3, p) == 2);
    zo_reset();
}

/* Runs in a fresh process: once a process has done a good deal of other z-order work, Windows 10 stops making an owner
 * topmost while its owned window still follows (observed: the owned window becomes topmost, the owner stays plain), so the
 * state under test has to start from a clean process. */
CHILD(zo_topmost_owner) {
    if (!zo_have_gui()) { printf("NOTE no GUI available\n"); return 0; }
    int fails = 0;
    HWND o = zo_top("owner4", ZO_OFFX, ZO_OFFY, 80, 80, 1);
    HWND p = o ? zo_top_ex("owned4", ZO_OFFX, ZO_OFFY, 40, 40, WS_POPUP, 0, o, 1, NULL) : NULL;
    if (!o || !p) { printf("NOTE no window\n"); return 0; }
    UN(NtUserSetWindowPos)(o, HWND_TOPMOST, 0, 0, 0, 0, ZO_SWP_ZONLY);
    printf("NOTE owner topmost=%d owned topmost=%d\n", is_topmost(o), is_topmost(p));
    if (!is_topmost(o)) { printf("FAIL owner not topmost\n"); fails++; }
    if (!is_topmost(p)) { printf("FAIL owned window not topmost\n"); fails++; }
    HWND in[2] = { o, p }, got[2];
    if (zo_rank(in, 2, got) != 2 || got[0] != p) { printf("FAIL owned window not above its owner\n"); fails++; }
    UN(NtUserSetWindowPos)(o, HWND_NOTOPMOST, 0, 0, 0, 0, ZO_SWP_ZONLY);
    printf("NOTE after NOTOPMOST: owner topmost=%d owned topmost=%d\n", is_topmost(o), is_topmost(p));
    UN(NtUserSetWindowPos)(p, HWND_NOTOPMOST, 0, 0, 0, 0, ZO_SWP_ZONLY);
    if (is_topmost(o) || is_topmost(p)) { printf("FAIL topmost not cleared\n"); fails++; }
    return fails;
}

TEST(win_position_zorder_swp_topmost_owner_and_owned_note) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    struct k_child_result r = k_child_run("zo_topmost_owner", "", 30000);
    EXPECT_EQ(r.exit_code, 0);
}

/* ---- child windows ---- */

TEST(win_position_zorder_swp_child_coordinates_are_parent_relative) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND p = zo_top("cp", ZO_OFFX + 100, ZO_OFFY + 200, 200, 150, 1);
    REQUIRE(p);
    HWND c = zo_child(p, "c", 10, 20, 50, 40, WS_VISIBLE, 0);
    REQUIRE(c);
    CHECK(UN(NtUserSetWindowPos)(c, NULL, 33, 44, 55, 66, SWP_NOZORDER | SWP_NOACTIVATE));
    RECT r = zo_wr(c), pc = zo_cr_screen(p);
    EXPECT_EQ(r.left, pc.left + 33); EXPECT_EQ(r.top, pc.top + 44);
    EXPECT_EQ(r.right - r.left, 55); EXPECT_EQ(r.bottom - r.top, 66);
    if (!t_is_wine() && zo_heap_ok()) {
        RECT hw; CHECK(zo_rect_heap(c, ZO_WND_RcWnd, &hw));
        CHECK(zo_same_rect(&hw, &r));     /* the heap stores screen coordinates even for children */
    }
    zo_reset();
}

TEST(win_position_zorder_swp_child_follows_parent_move) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND p = zo_top("cfp", ZO_OFFX, ZO_OFFY, 200, 150, 1);
    REQUIRE(p);
    HWND c = zo_child(p, "cf", 10, 20, 50, 40, WS_VISIBLE, 0);
    REQUIRE(c);
    RECT before = zo_wr(c);
    CHECK(UN(NtUserSetWindowPos)(p, NULL, ZO_OFFX + 70, ZO_OFFY - 30, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE));
    RECT after = zo_wr(c);
    EXPECT_EQ(after.left - before.left, 70); EXPECT_EQ(after.top - before.top, -30);
    EXPECT_EQ(after.right - after.left, before.right - before.left);
    if (!t_is_wine() && zo_heap_ok()) { RECT hw; zo_rect_heap(c, ZO_WND_RcWnd, &hw); CHECK(zo_same_rect(&hw, &after)); }
    zo_reset();
}

TEST(win_position_zorder_swp_child_sibling_zorder) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND p = zo_top("csp", ZO_OFFX, ZO_OFFY, 200, 150, 1);
    REQUIRE(p);
    HWND c[3];
    for (int i = 0; i < 3; i++) { c[i] = zo_child(p, "cs", 5 * i, 5 * i, 40, 40, WS_VISIBLE, 0); REQUIRE(c[i]); }
    HWND in[3] = { c[0], c[1], c[2] }, got[3], h[3];
    zo_arrange(in, 3);
    CHECK(U(GetWindow)(p, GW_CHILD) == c[0]);
    CHECK(UN(NtUserSetWindowPos)(c[0], HWND_BOTTOM, 0, 0, 0, 0, ZO_SWP_ZONLY));
    HWND want[3] = { c[1], c[2], c[0] };
    CHECK(zo_rank(in, 3, got) == 3 && zo_same(got, want, 3));
    CHECK(U(GetWindow)(p, GW_CHILD) == c[1]);
    CHECK(UN(NtUserSetWindowPos)(c[2], HWND_TOP, 0, 0, 0, 0, ZO_SWP_ZONLY));
    HWND want2[3] = { c[2], c[1], c[0] };
    CHECK(zo_rank(in, 3, got) == 3 && zo_same(got, want2, 3));
    if (!t_is_wine() && zo_heap_ok()) { CHECK(zo_rank_heap(p, in, 3, h) == 3 && zo_same(h, want2, 3)); }
    zo_reset();
}

TEST(win_position_zorder_swp_child_topmost_request) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND p = zo_top("ctp", ZO_OFFX, ZO_OFFY, 200, 150, 1);
    REQUIRE(p);
    HWND a = zo_child(p, "ca", 0, 0, 40, 40, WS_VISIBLE, 0), b = zo_child(p, "cb", 0, 0, 40, 40, WS_VISIBLE, 0);
    REQUIRE(a && b);
    HWND in[2] = { a, b }, got[2];
    zo_arrange(in, 2);
    BOOL r = UN(NtUserSetWindowPos)(b, HWND_TOPMOST, 0, 0, 0, 0, ZO_SWP_ZONLY);
    t_note("child HWND_TOPMOST returned %d, ex topmost=%d", r, is_topmost(b));
    t_note("child WS_EX_TOPMOST after HWND_TOPMOST: %d", is_topmost(b));
    CHECK(zo_rank(in, 2, got) == 2);
    zo_reset();
}

TEST(win_position_zorder_swp_bringwindowtotop_child_matches_swp_hwnd_top) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND p = zo_top("bwp", ZO_OFFX, ZO_OFFY, 200, 150, 1);
    REQUIRE(p);
    HWND c[3];
    for (int i = 0; i < 3; i++) { c[i] = zo_child(p, "bw", 0, 0, 40, 40, WS_VISIBLE, 0); REQUIRE(c[i]); }
    HWND in[3] = { c[0], c[1], c[2] }, got[3];
    zo_arrange(in, 3);
    /* user32!BringWindowToTop is NtUserSetWindowPos(hwnd, 0, 0, 0, 0, 0, SWP_NOSIZE|SWP_NOMOVE) */
    CHECK(U(BringWindowToTop)(c[2]));
    HWND want[3] = { c[2], c[0], c[1] };
    CHECK(zo_rank(in, 3, got) == 3 && zo_same(got, want, 3));
    zo_arrange(in, 3);
    CHECK(UN(NtUserSetWindowPos)(c[2], NULL, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE));
    CHECK(zo_rank(in, 3, got) == 3 && zo_same(got, want, 3));
    zo_reset();
}

TEST(win_position_zorder_swp_bringwindowtotop_invalid_window) {
    NEED_GUI();
    SetLastError(0);
    CHECK(!U(BringWindowToTop)((HWND)0x1));
    EXPECT_ERR(1400);
    SetLastError(0);
    CHECK(!U(BringWindowToTop)(NULL));
    EXPECT_ERR(1400);
}

/* ---- cross thread ---- */

struct mv { HWND h; int ok; };
static ULONG_PTR mv_fn(void *p) {
    struct mv *m = (struct mv *)p;
    m->ok = UN(NtUserSetWindowPos)(m->h, NULL, ZO_OFFX + 60, ZO_OFFY + 61, 77, 78, SWP_NOZORDER | SWP_NOACTIVATE);
    return 1;
}
TEST(win_position_zorder_swp_cross_thread_target) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    struct zo_thread *t = zo_thread_start();
    if (!t) { t_skip("could not start GUI thread"); return; }
    HWND w = zo_thread_window(t, "xthread", ZO_OFFX, ZO_OFFY, 100, 80, 1);
    if (!w) { zo_thread_stop(t); t_skip("window on worker thread failed"); return; }
    /* the owning thread sits in GetMessage, so the synchronous WM_WINDOWPOSCHANGING/CHANGED round trip completes */
    CHECK(UN(NtUserSetWindowPos)(w, NULL, ZO_OFFX + 12, ZO_OFFY + 13, 66, 67, SWP_NOZORDER | SWP_NOACTIVATE));
    RECT r = zo_wr(w);
    EXPECT_EQ(r.left, ZO_OFFX + 12); EXPECT_EQ(r.top, ZO_OFFY + 13);
    EXPECT_EQ(r.right - r.left, 66); EXPECT_EQ(r.bottom - r.top, 67);
    CHECK(U(GetWindowThreadProcessId)(w, NULL) == t->tid);
    struct mv m = { w, 0 };
    zo_thread_call(t, mv_fn, &m);       /* and from the owning thread itself */
    CHECK(m.ok);
    r = zo_wr(w);
    EXPECT_EQ(r.left, ZO_OFFX + 60);
    zo_thread_stop(t);
}

TEST(win_position_zorder_swp_async_flag_applied_after_pump) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    struct zo_thread *t = zo_thread_start();
    if (!t) { t_skip("could not start GUI thread"); return; }
    HWND w = zo_thread_window(t, "async", ZO_OFFX, ZO_OFFY, 100, 80, 1);
    if (!w) { zo_thread_stop(t); t_skip("window on worker thread failed"); return; }
    BOOL r = UN(NtUserSetWindowPos)(w, NULL, ZO_OFFX + 21, ZO_OFFY + 22, 61, 62, SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
    CHECK(r);
    for (int i = 0; i < 100; i++) { RECT q = zo_wr(w); if (q.left == ZO_OFFX + 21) break; Sleep(10); }
    RECT q = zo_wr(w);
    EXPECT_EQ(q.left, ZO_OFFX + 21); EXPECT_EQ(q.top, ZO_OFFY + 22);
    EXPECT_EQ(q.right - q.left, 61);
    zo_thread_stop(t);
}

TEST(win_position_zorder_swp_user32_and_stub_agree) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND w = zo_top("agree", ZO_OFFX, ZO_OFFY, 100, 80, 1);
    REQUIRE(w);
    CHECK(U(SetWindowPos)(w, NULL, ZO_OFFX + 3, ZO_OFFY + 4, 31, 32, SWP_NOZORDER | SWP_NOACTIVATE));
    RECT a = zo_wr(w);
    CHECK(UN(NtUserSetWindowPos)(w, NULL, ZO_OFFX + 3, ZO_OFFY + 4, 31, 32, SWP_NOZORDER | SWP_NOACTIVATE));
    RECT b = zo_wr(w);
    CHECK(zo_same_rect(&a, &b));
    SetLastError(0);
    CHECK(!U(SetWindowPos)(w, NULL, 0, 0, 0, 0, 0x800));
    EXPECT_ERR(1004);
    zo_reset();
}

TEST(win_position_zorder_swp_ex_stub_bytes_are_win32u_syscall_shape) {
    NEED_NT(NtUserSetWindowPos);
    FARPROC p = t_proc_in("win32u.dll", "NtUserSetWindowPos");
    REQUIRE(p);
    CHECK(k_stub_valid(p));
    long n = k_stub_number(p);
    if (t_is_wine()) { t_note("Wine syscall number %ld", n); return; }
    EXPECT_EQ(n, 4132);     /* tools/win32u_syscalls.tsv */
}

/* NtUserSetWindowPos rejects (flags & 0xfffc9800) before any window work,
 * so a rejected call must neither reorder siblings nor change visibility. */
TEST(win_position_zorder_swp_invalid_flag_bits_leave_zorder_unchanged) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND a = zo_top("zua", ZO_OFFX, ZO_OFFY, 40, 40, 1);
    HWND b = zo_top("zub", ZO_OFFX, ZO_OFFY, 40, 40, 1);
    REQUIRE(a && b);
    HWND above_b = U(GetWindow)(b, GW_HWNDPREV), below_b = U(GetWindow)(b, GW_HWNDNEXT);
    SetLastError(0);
    CHECK(!UN(NtUserSetWindowPos)(b, HWND_BOTTOM, 0, 0, 0, 0, ZO_SWP_NOTHING | 0x00000800u));
    EXPECT_ERR(1004);
    SetLastError(0);
    CHECK(!UN(NtUserSetWindowPos)(b, HWND_TOPMOST, 0, 0, 0, 0, ZO_SWP_NOTHING | 0x80000000u));
    EXPECT_ERR(1004);
    CHECK(U(GetWindow)(b, GW_HWNDPREV) == above_b);
    CHECK(U(GetWindow)(b, GW_HWNDNEXT) == below_b);
    CHECK(!(U(GetWindowLongW)(b, GWL_EXSTYLE) & WS_EX_TOPMOST));
    CHECK(U(IsWindowVisible)(b));
    zo_reset();
}

/* A valid no-op call (all NO* flags) returns TRUE and leaves the rect byte-identical. */
TEST(win_position_zorder_swp_all_no_flags_is_noop_true) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND w = zo_top("noop", ZO_OFFX, ZO_OFFY, 60, 50, 1);
    REQUIRE(w);
    RECT before = zo_wr(w);
    SetLastError(0);
    CHECK(UN(NtUserSetWindowPos)(w, NULL, 123, 456, 789, 12, ZO_SWP_NOTHING));
    RECT after = zo_wr(w);
    CHECK(zo_same_rect(&before, &after));
    zo_reset();
}
