/* BeginDeferWindowPos / DeferWindowPos / EndDeferWindowPos and MoveWindow.
 *
 * BeginDeferWindowPos(n) is NtUserCallOneParam(n, 0x2c) -> win32kfull!_BeginDeferWindowPos, which
 * allocates a type-4 (TYPE_SETWINDOWPOS) object in the USER handle table; n == 0 means 8
 * slots. user32 itself rejects n < 0 with ERROR_INVALID_PARAMETER.
 * DeferWindowPos is NtUserDeferWindowPosAndBand(hdwp, hwnd, after, x, y, cx, cy, flags, 0, 0):
 *   - (flags & 0xfffc9800) -> ERROR_INVALID_FLAGS (1004) before anything else;
 *   - HMValidateHandle(hdwp, 4) -> ERROR_INVALID_DWP_HANDLE (0x57d = 1405) for a bad handle;
 *   - _DeferWindowPosAndBand additionally rejects flags & 0xfffefc00 with ERROR_INVALID_PARAMETER.
 * EndDeferWindowPos is NtUserEndDeferWindowPosEx(hdwp, 0): same type-4 validation, the object is
 * destroyed when the batch has been applied.
 * MoveWindow is NtUserMoveWindow: x,y clamped to [-0x8000,0x7fff], cx,cy to [0,0x7fff].
 */
#include "zo.h"

static int user_entry_type(HANDLE h, int *valid) {
    struct k_user_entry e;
    *valid = 0;
    if (!k_user_entry(h, &e)) return -1;
    *valid = 1;
    return e.type;
}

TEST(win_position_zorder_defer_begin_negative_count_rejected) {
    NEED_GUI();
    REQUIRE(U(BeginDeferWindowPos));
    int v[] = { -1, -2, -100, (int)0x80000000 };
    for (unsigned i = 0; i < 4; i++) {
        SetLastError(0);
        CHECKF(U(BeginDeferWindowPos)(v[i]) == NULL, "count %d", v[i]);
        EXPECT_ERR(87);     /* checked in user32!BeginDeferWindowPos before any syscall */
    }
}

TEST(win_position_zorder_defer_begin_zero_gets_default_capacity) {
    NEED_GUI();
    REQUIRE(U(BeginDeferWindowPos) && U(EndDeferWindowPos));
    HDWP h = U(BeginDeferWindowPos)(0);     /* _BeginDeferWindowPos substitutes 8 for 0 */
    CHECK(h != NULL);
    if (h) U(EndDeferWindowPos)(h);
}

TEST(win_position_zorder_defer_handle_is_type4_user_object) {
    NEED_GUI();
    REQUIRE(U(BeginDeferWindowPos) && U(EndDeferWindowPos));
    if (t_is_wine()) { t_skip("no USER handle table under Wine"); return; }
    HDWP h = U(BeginDeferWindowPos)(2);
    REQUIRE(h);
    int valid; int ty = user_entry_type(h, &valid);
    if (!valid) { U(EndDeferWindowPos)(h); t_skip("USER handle entry not readable"); return; }
    EXPECT_EQ(ty, 4);       /* HMValidateHandle(hdwp, 4) in NtUserDeferWindowPosAndBand / NtUserEndDeferWindowPosEx */
    U(EndDeferWindowPos)(h);
    ty = user_entry_type(h, &valid);
    CHECKF(ty == 0, "entry type after End is %d, want 0 (free)", ty);
}

TEST(win_position_zorder_defer_handles_are_distinct) {
    NEED_GUI();
    REQUIRE(U(BeginDeferWindowPos) && U(EndDeferWindowPos));
    HDWP h[16];
    for (int i = 0; i < 16; i++) { h[i] = U(BeginDeferWindowPos)(1); CHECK(h[i] != NULL); }
    for (int i = 0; i < 16; i++) for (int j = i + 1; j < 16; j++) CHECK(h[i] != h[j]);
    for (int i = 0; i < 16; i++) if (h[i]) U(EndDeferWindowPos)(h[i]);
}

TEST(win_position_zorder_defer_window_handle_is_not_an_hdwp) {
    NEED_GUI(); NEED_NT(NtUserDeferWindowPosAndBand);
    HWND w = zo_top("notdwp", ZO_OFFX, ZO_OFFY, 50, 50, 1);
    REQUIRE(w);
    SetLastError(0);
    /* HMValidateHandle(type 4) on a type-1 handle fails: ERROR_INVALID_DWP_HANDLE */
    CHECK(UN(NtUserDeferWindowPosAndBand)((HANDLE)w, w, NULL, 0, 0, 0, 0, ZO_SWP_NOTHING, 0, 0) == NULL);
    EXPECT_ERR(1405);
    SetLastError(0);
    CHECK(!UN(NtUserEndDeferWindowPosEx)((HANDLE)w, 0));
    EXPECT_ERR(1405);
    CHECK(U(IsWindow)(w));      /* and the window is untouched */
    zo_reset();
}

TEST(win_position_zorder_defer_invalid_hdwp_table) {
    NEED_GUI(); NEED_NT(NtUserDeferWindowPosAndBand);
    HWND w = zo_top("badhdwp", ZO_OFFX, ZO_OFFY, 50, 50, 1);
    REQUIRE(w);
    HANDLE bad[] = { NULL, (HANDLE)1, (HANDLE)0x10, (HANDLE)0xFFFF, (HANDLE)(ULONG_PTR)-1, (HANDLE)0x7fffffff };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(0);
        CHECKF(UN(NtUserDeferWindowPosAndBand)(bad[i], w, NULL, 0, 0, 0, 0, ZO_SWP_NOTHING, 0, 0) == NULL, "hdwp %p", bad[i]);
        CHECKF(GetLastError() == 1405, "hdwp %p error %lu want 1405", bad[i], GetLastError());
        SetLastError(0);
        CHECKF(!U(EndDeferWindowPos)(bad[i]), "End hdwp %p", bad[i]);
        CHECKF(GetLastError() == 1405, "End hdwp %p error %lu want 1405", bad[i], GetLastError());
    }
    zo_reset();
}

TEST(win_position_zorder_defer_end_twice_fails) {
    NEED_GUI();
    REQUIRE(U(BeginDeferWindowPos) && U(DeferWindowPos) && U(EndDeferWindowPos));
    HWND w = zo_top("end2", ZO_OFFX, ZO_OFFY, 50, 50, 1);
    REQUIRE(w);
    HDWP h = U(BeginDeferWindowPos)(1);
    REQUIRE(h);
    h = U(DeferWindowPos)(h, w, NULL, ZO_OFFX + 5, ZO_OFFY + 5, 50, 50, SWP_NOZORDER | SWP_NOACTIVATE);
    REQUIRE(h);
    CHECK(U(EndDeferWindowPos)(h));
    SetLastError(0);
    CHECK(!U(EndDeferWindowPos)(h));        /* the object was destroyed by the first End */
    EXPECT_ERR(1405);
    SetLastError(0);
    CHECK(U(DeferWindowPos)(h, w, NULL, 0, 0, 0, 0, ZO_SWP_NOTHING) == NULL);
    EXPECT_ERR(1405);
    zo_reset();
}

TEST(win_position_zorder_defer_applies_only_at_end) {
    NEED_GUI();
    REQUIRE(U(BeginDeferWindowPos) && U(DeferWindowPos) && U(EndDeferWindowPos));
    HWND w = zo_top("late", ZO_OFFX, ZO_OFFY, 50, 50, 1);
    REQUIRE(w);
    RECT before = zo_wr(w);
    HDWP h = U(BeginDeferWindowPos)(1);
    REQUIRE(h);
    h = U(DeferWindowPos)(h, w, NULL, ZO_OFFX + 40, ZO_OFFY + 41, 70, 71, SWP_NOZORDER | SWP_NOACTIVATE);
    REQUIRE(h);
    RECT mid = zo_wr(w);
    CHECK(zo_same_rect(&before, &mid));     /* nothing has moved yet */
    CHECK(U(EndDeferWindowPos)(h));
    RECT after = zo_wr(w);
    EXPECT_EQ(after.left, ZO_OFFX + 40); EXPECT_EQ(after.top, ZO_OFFY + 41);
    EXPECT_EQ(after.right - after.left, 70); EXPECT_EQ(after.bottom - after.top, 71);
    zo_reset();
}

TEST(win_position_zorder_defer_batch_of_children_moves_all) {
    NEED_GUI();
    REQUIRE(U(BeginDeferWindowPos) && U(DeferWindowPos) && U(EndDeferWindowPos));
    HWND p = zo_top("dp", ZO_OFFX, ZO_OFFY, 300, 200, 1);
    REQUIRE(p);
    HWND c[4];
    for (int i = 0; i < 4; i++) { c[i] = zo_child(p, "dc", 0, 0, 20, 20, WS_VISIBLE, 0); REQUIRE(c[i]); }
    HDWP h = U(BeginDeferWindowPos)(4);
    REQUIRE(h);
    for (int i = 0; i < 4 && h; i++)
        h = U(DeferWindowPos)(h, c[i], NULL, 10 * (i + 1), 5 * (i + 1), 30 + i, 40 + i, SWP_NOZORDER | SWP_NOACTIVATE);
    REQUIRE(h);
    CHECK(U(EndDeferWindowPos)(h));
    RECT pc = zo_cr_screen(p);
    for (int i = 0; i < 4; i++) {
        RECT r = zo_wr(c[i]);
        CHECKF(r.left == pc.left + 10 * (i + 1) && r.top == pc.top + 5 * (i + 1) && r.right - r.left == 30 + i && r.bottom - r.top == 40 + i, "child %d", i);
    }
    zo_reset();
}

TEST(win_position_zorder_defer_more_windows_than_capacity_grows) {
    NEED_GUI();
    REQUIRE(U(BeginDeferWindowPos) && U(DeferWindowPos) && U(EndDeferWindowPos));
    HWND p = zo_top("dgrow", ZO_OFFX, ZO_OFFY, 400, 300, 1);
    REQUIRE(p);
    enum { N = 12 };
    HWND c[N];
    for (int i = 0; i < N; i++) { c[i] = zo_child(p, "dg", 0, 0, 10, 10, WS_VISIBLE, 0); REQUIRE(c[i]); }
    HDWP h = U(BeginDeferWindowPos)(1);     /* one slot requested, twelve used */
    REQUIRE(h);
    for (int i = 0; i < N && h; i++) h = U(DeferWindowPos)(h, c[i], NULL, i * 3, i * 2, 11, 12, SWP_NOZORDER | SWP_NOACTIVATE);
    CHECK(h != NULL);
    if (h) CHECK(U(EndDeferWindowPos)(h));
    for (int i = 0; i < N; i++) { RECT r = zo_wr(c[i]), pc = zo_cr_screen(p); CHECK(r.left == pc.left + i * 3 && r.right - r.left == 11); }
    zo_reset();
}

TEST(win_position_zorder_defer_invalid_flags_fail_but_keep_batch_alive) {
    NEED_GUI(); NEED_NT(NtUserDeferWindowPosAndBand);
    REQUIRE(U(BeginDeferWindowPos) && U(DeferWindowPos) && U(EndDeferWindowPos));
    HWND w = zo_top("dflags", ZO_OFFX, ZO_OFFY, 50, 50, 1);
    REQUIRE(w);
    HDWP h = U(BeginDeferWindowPos)(2);
    REQUIRE(h);
    static const unsigned bad[] = { 0x800, 0x1000, 0x8000, 0x40000, 0x80000, 0x80000000u };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(0);
        CHECKF(UN(NtUserDeferWindowPosAndBand)(h, w, NULL, 0, 0, 0, 0, ZO_SWP_NOTHING | bad[i], 0, 0) == NULL, "flag 0x%x", bad[i]);
        CHECKF(GetLastError() == 1004, "flag 0x%x error %lu want 1004", bad[i], GetLastError());
    }
    /* the handle survived the failures and can still be used */
    HDWP h2 = U(DeferWindowPos)(h, w, NULL, ZO_OFFX + 3, ZO_OFFY + 3, 50, 50, SWP_NOZORDER | SWP_NOACTIVATE);
    CHECK(h2 != NULL);
    CHECK(U(EndDeferWindowPos)(h2 ? h2 : h));
    RECT r = zo_wr(w);
    EXPECT_EQ(r.left, ZO_OFFX + 3);
    zo_reset();
}

TEST(win_position_zorder_defer_flags_narrower_than_setwindowpos) {
    NEED_GUI(); NEED_NT(NtUserDeferWindowPosAndBand);
    REQUIRE(U(BeginDeferWindowPos) && U(EndDeferWindowPos));
    HWND w = zo_top("dnarrow", ZO_OFFX, ZO_OFFY, 50, 50, 1);
    REQUIRE(w);
    HDWP h = U(BeginDeferWindowPos)(2);
    REQUIRE(h);
    /* SWP_DEFERERASE (0x2000), SWP_ASYNCWINDOWPOS (0x4000) and bits >= 0x20000 pass the 0xfffc9800 test but are
     * refused by _DeferWindowPosAndBand's second mask (0xfffefc00) with ERROR_INVALID_PARAMETER. */
    static const unsigned narrow[] = { 0x2000, 0x4000, 0x20000 };
    for (unsigned i = 0; i < 3; i++) {
        SetLastError(0);
        CHECKF(UN(NtUserDeferWindowPosAndBand)(h, w, NULL, 0, 0, 0, 0, ZO_SWP_NOTHING | narrow[i], 0, 0) == NULL, "flag 0x%x", narrow[i]);
        CHECKF(GetLastError() == 87, "flag 0x%x error %lu want 87", narrow[i], GetLastError());
    }
    SetLastError(0);
    HANDLE r = UN(NtUserDeferWindowPosAndBand)(h, w, NULL, 0, 0, 0, 0, ZO_SWP_NOTHING | SWP_NOSENDCHANGING, 0, 0);
    t_note("SWP_NOSENDCHANGING in DeferWindowPos -> %p, error %lu", r, GetLastError());
    U(EndDeferWindowPos)(r ? (HDWP)r : h);
    zo_reset();
}

TEST(win_position_zorder_defer_valid_flags_accepted) {
    NEED_GUI(); NEED_NT(NtUserDeferWindowPosAndBand);
    REQUIRE(U(BeginDeferWindowPos) && U(EndDeferWindowPos));
    HWND w = zo_top("dgood", ZO_OFFX, ZO_OFFY, 50, 50, 1);
    REQUIRE(w);
    static const unsigned good[] = { SWP_NOSIZE, SWP_NOMOVE, SWP_NOZORDER, SWP_NOREDRAW, SWP_NOACTIVATE, SWP_FRAMECHANGED, SWP_NOCOPYBITS, SWP_NOOWNERZORDER };
    HDWP h = U(BeginDeferWindowPos)(2);
    REQUIRE(h);
    for (unsigned i = 0; i < sizeof good / sizeof good[0]; i++) {
        HANDLE r = UN(NtUserDeferWindowPosAndBand)(h, w, NULL, ZO_OFFX, ZO_OFFY, 50, 50, ZO_SWP_NOTHING | good[i], 0, 0);
        CHECKF(r != NULL, "flag 0x%x refused (error %lu)", good[i], GetLastError());
        if (r) h = (HDWP)r;
    }
    CHECK(U(EndDeferWindowPos)(h));
    zo_reset();
}

TEST(win_position_zorder_defer_zorder_is_applied_at_end) {
    NEED_GUI();
    REQUIRE(U(BeginDeferWindowPos) && U(DeferWindowPos) && U(EndDeferWindowPos));
    HWND w[3];
    for (int i = 0; i < 3; i++) { w[i] = zo_top("dz", ZO_OFFX + i, ZO_OFFY, 40, 40, 1); REQUIRE(w[i]); }
    HWND in[3] = { w[0], w[1], w[2] }, got[3];
    zo_arrange(in, 3);
    HDWP h = U(BeginDeferWindowPos)(3);
    REQUIRE(h);
    h = U(DeferWindowPos)(h, w[0], HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    REQUIRE(h);
    CHECK(zo_rank(in, 3, got) == 3 && zo_same(got, in, 3));        /* not yet */
    CHECK(U(EndDeferWindowPos)(h));
    HWND want[3] = { w[1], w[2], w[0] };
    CHECK(zo_rank(in, 3, got) == 3 && zo_same(got, want, 3));
    zo_reset();
}

TEST(win_position_zorder_defer_show_and_hide_flags_apply) {
    NEED_GUI();
    REQUIRE(U(BeginDeferWindowPos) && U(DeferWindowPos) && U(EndDeferWindowPos));
    HWND a = zo_top("dsa", ZO_OFFX, ZO_OFFY, 40, 40, 0), b = zo_top("dsb", ZO_OFFX, ZO_OFFY, 40, 40, 1);
    REQUIRE(a && b);
    HDWP h = U(BeginDeferWindowPos)(2);
    REQUIRE(h);
    h = U(DeferWindowPos)(h, a, NULL, 0, 0, 0, 0, ZO_SWP_NOTHING | SWP_SHOWWINDOW);
    REQUIRE(h);
    h = U(DeferWindowPos)(h, b, NULL, 0, 0, 0, 0, ZO_SWP_NOTHING | SWP_HIDEWINDOW);
    REQUIRE(h);
    CHECK(!U(IsWindowVisible)(a)); CHECK(U(IsWindowVisible)(b));
    CHECK(U(EndDeferWindowPos)(h));
    CHECK(U(IsWindowVisible)(a)); CHECK(!U(IsWindowVisible)(b));
    zo_reset();
}

TEST(win_position_zorder_defer_invalid_window_fails_batch_survives) {
    NEED_GUI(); NEED_NT(NtUserDeferWindowPosAndBand);
    REQUIRE(U(BeginDeferWindowPos) && U(EndDeferWindowPos));
    HWND dead = zo_top("ddead", ZO_OFFX, ZO_OFFY, 40, 40, 1);
    REQUIRE(dead);
    U(DestroyWindow)(dead);
    HDWP h = U(BeginDeferWindowPos)(2);
    REQUIRE(h);
    SetLastError(0);
    HANDLE r = UN(NtUserDeferWindowPosAndBand)(h, dead, NULL, 0, 0, 0, 0, ZO_SWP_NOTHING, 0, 0);
    CHECK(r == NULL);
    t_note("DeferWindowPos(destroyed window) error %lu", GetLastError());
    /* the HDWP itself remains valid after a refused entry (ValidateHWNDND failed before the object was touched) */
    SetLastError(0);
    CHECK(U(EndDeferWindowPos)(h));
    zo_reset();
}

TEST(win_position_zorder_defer_invalid_insert_after_fails) {
    NEED_GUI(); NEED_NT(NtUserDeferWindowPosAndBand);
    REQUIRE(U(BeginDeferWindowPos) && U(EndDeferWindowPos));
    HWND w = zo_top("dia", ZO_OFFX, ZO_OFFY, 40, 40, 1);
    REQUIRE(w);
    HDWP h = U(BeginDeferWindowPos)(2);
    REQUIRE(h);
    SetLastError(0);
    HANDLE r = UN(NtUserDeferWindowPosAndBand)(h, w, (HWND)0x1234, 0, 0, 0, 0, ZO_SWP_NOTHING & ~SWP_NOZORDER, 0, 0);
    CHECK(r == NULL);
    t_note("DeferWindowPos(bogus insert-after) error %lu", GetLastError());
    U(EndDeferWindowPos)(h);
    zo_reset();
}

TEST(win_position_zorder_defer_same_window_twice_note) {
    NEED_GUI();
    REQUIRE(U(BeginDeferWindowPos) && U(DeferWindowPos) && U(EndDeferWindowPos));
    HWND w = zo_top("d2x", ZO_OFFX, ZO_OFFY, 40, 40, 1);
    REQUIRE(w);
    HDWP h = U(BeginDeferWindowPos)(2);
    REQUIRE(h);
    h = U(DeferWindowPos)(h, w, NULL, ZO_OFFX + 10, ZO_OFFY + 10, 41, 42, SWP_NOZORDER | SWP_NOACTIVATE);
    REQUIRE(h);
    h = U(DeferWindowPos)(h, w, NULL, ZO_OFFX + 20, ZO_OFFY + 20, 43, 44, SWP_NOZORDER | SWP_NOACTIVATE);
    REQUIRE(h);
    CHECK(U(EndDeferWindowPos)(h));
    RECT r = zo_wr(w);
    t_note("after two entries for one window: (%ld,%ld) %ldx%ld", r.left - ZO_OFFX, r.top - ZO_OFFY, r.right - r.left, r.bottom - r.top);
    /* the batch is a set of entries; whichever way duplicates are merged, the window ends in one of the two requested states */
    CHECK((r.left == ZO_OFFX + 10 && r.right - r.left == 41) || (r.left == ZO_OFFX + 20 && r.right - r.left == 43));
    zo_reset();
}

TEST(win_position_zorder_defer_windows_with_different_parents_note) {
    NEED_GUI();
    REQUIRE(U(BeginDeferWindowPos) && U(DeferWindowPos) && U(EndDeferWindowPos));
    HWND p1 = zo_top("dpa", ZO_OFFX, ZO_OFFY, 100, 100, 1), p2 = zo_top("dpb", ZO_OFFX + 200, ZO_OFFY, 100, 100, 1);
    REQUIRE(p1 && p2);
    HWND c1 = zo_child(p1, "c1", 0, 0, 10, 10, WS_VISIBLE, 0), c2 = zo_child(p2, "c2", 0, 0, 10, 10, WS_VISIBLE, 0);
    REQUIRE(c1 && c2);
    HDWP h = U(BeginDeferWindowPos)(2);
    REQUIRE(h);
    HDWP h1 = U(DeferWindowPos)(h, c1, NULL, 5, 5, 10, 10, SWP_NOZORDER | SWP_NOACTIVATE);
    CHECK(h1 != NULL);
    SetLastError(0);
    HDWP h2 = U(DeferWindowPos)(h1 ? h1 : h, c2, NULL, 6, 6, 10, 10, SWP_NOZORDER | SWP_NOACTIVATE);
    t_note("second window with a different parent: %p error %lu", h2, GetLastError());
    BOOL e = U(EndDeferWindowPos)(h2 ? h2 : (h1 ? h1 : h));
    t_note("End returned %d", e);
    CHECK(U(IsWindow)(c1) && U(IsWindow)(c2));
    zo_reset();
}

TEST(win_position_zorder_defer_end_empty_batch_note) {
    NEED_GUI();
    REQUIRE(U(BeginDeferWindowPos) && U(EndDeferWindowPos));
    HDWP h = U(BeginDeferWindowPos)(3);
    REQUIRE(h);
    SetLastError(0);
    BOOL r = U(EndDeferWindowPos)(h);
    t_note("End of an empty batch -> %d, error %lu", r, GetLastError());
    if (!t_is_wine()) {
        int valid; int ty = user_entry_type(h, &valid);
        if (valid) t_note("entry type after empty End: %d", ty);
    }
}

TEST(win_position_zorder_defer_destroyed_window_before_end_note) {
    NEED_GUI();
    REQUIRE(U(BeginDeferWindowPos) && U(DeferWindowPos) && U(EndDeferWindowPos));
    HWND a = zo_top("ddA", ZO_OFFX, ZO_OFFY, 40, 40, 1), b = zo_top("ddB", ZO_OFFX, ZO_OFFY, 40, 40, 1);
    REQUIRE(a && b);
    HDWP h = U(BeginDeferWindowPos)(2);
    REQUIRE(h);
    h = U(DeferWindowPos)(h, a, NULL, ZO_OFFX + 5, ZO_OFFY + 5, 40, 40, SWP_NOZORDER | SWP_NOACTIVATE);
    REQUIRE(h);
    h = U(DeferWindowPos)(h, b, NULL, ZO_OFFX + 6, ZO_OFFY + 6, 40, 40, SWP_NOZORDER | SWP_NOACTIVATE);
    REQUIRE(h);
    U(DestroyWindow)(a);
    BOOL r = U(EndDeferWindowPos)(h);
    t_note("End with a destroyed member -> %d", r);
    RECT rb = zo_wr(b);
    CHECK(rb.left == ZO_OFFX + 6 || rb.left == ZO_OFFX);       /* survivor either moved or the batch was dropped */
    zo_reset();
}

TEST(win_position_zorder_defer_end_async_applies_after_pump) {
    NEED_GUI(); NEED_NT(NtUserEndDeferWindowPosEx);
    REQUIRE(U(BeginDeferWindowPos) && U(DeferWindowPos));
    HWND w = zo_top("dasync", ZO_OFFX, ZO_OFFY, 40, 40, 1);
    REQUIRE(w);
    HDWP h = U(BeginDeferWindowPos)(1);
    REQUIRE(h);
    h = U(DeferWindowPos)(h, w, NULL, ZO_OFFX + 33, ZO_OFFY + 34, 45, 46, SWP_NOZORDER | SWP_NOACTIVATE);
    REQUIRE(h);
    BOOL r = UN(NtUserEndDeferWindowPosEx)(h, TRUE);     /* AsyncWindowPos(): queued to the window's thread */
    t_note("async End returned %d", r);
    for (int i = 0; i < 100; i++) { k_pump(10); RECT q = zo_wr(w); if (q.left == ZO_OFFX + 33) break; }
    RECT q = zo_wr(w);
    EXPECT_EQ(q.left, ZO_OFFX + 33); EXPECT_EQ(q.top, ZO_OFFY + 34);
    EXPECT_EQ(q.right - q.left, 45);
    zo_reset();
}

TEST(win_position_zorder_defer_direct_stub_matches_user32) {
    NEED_GUI(); NEED_NT(NtUserDeferWindowPosAndBand); NEED_NT(NtUserEndDeferWindowPosEx);
    REQUIRE(U(BeginDeferWindowPos));
    HWND w = zo_top("dstub", ZO_OFFX, ZO_OFFY, 40, 40, 1);
    REQUIRE(w);
    HDWP h = U(BeginDeferWindowPos)(1);
    REQUIRE(h);
    HANDLE r = UN(NtUserDeferWindowPosAndBand)(h, w, NULL, ZO_OFFX + 15, ZO_OFFY + 16, 47, 48, SWP_NOZORDER | SWP_NOACTIVATE, 0, 0);
    CHECK(r != NULL);
    CHECK(UN(NtUserEndDeferWindowPosEx)(r ? r : h, 0));
    RECT q = zo_wr(w);
    EXPECT_EQ(q.left, ZO_OFFX + 15); EXPECT_EQ(q.right - q.left, 47);
    zo_reset();
}

/* ---- MoveWindow ---- */

TEST(win_position_zorder_move_basic) {
    NEED_GUI();
    HWND w = zo_top("mv", ZO_OFFX, ZO_OFFY, 100, 80, 1);
    REQUIRE(w);
    CHECK(U(MoveWindow)(w, ZO_OFFX + 11, ZO_OFFY + 22, 133, 144, TRUE));
    RECT r = zo_wr(w);
    EXPECT_EQ(r.left, ZO_OFFX + 11); EXPECT_EQ(r.top, ZO_OFFY + 22);
    EXPECT_EQ(r.right - r.left, 133); EXPECT_EQ(r.bottom - r.top, 144);
    zo_reset();
}

TEST(win_position_zorder_move_position_clamped) {
    NEED_GUI(); NEED_NT(NtUserMoveWindow);
    HWND w = zo_top("mvclamp", ZO_OFFX, ZO_OFFY, 20, 20, 1);
    REQUIRE(w);
    struct { int in, want; } v[] = { { 0x8000, 0x7fff }, { 0x12345, 0x7fff }, { -0x8001, -0x8000 }, { -0x12345, -0x8000 }, { 0x7fff, 0x7fff }, { -0x8000, -0x8000 } };
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) {
        CHECK(UN(NtUserMoveWindow)(w, v[i].in, v[i].in, 20, 20, FALSE));
        RECT r = zo_wr(w);
        CHECKF(r.left == v[i].want && r.top == v[i].want, "in=%d got (%ld,%ld)", v[i].in, r.left, r.top);
    }
    zo_reset();
}

TEST(win_position_zorder_move_size_clamped) {
    NEED_GUI(); NEED_NT(NtUserMoveWindow);
    HWND w = zo_top("mvsize", ZO_OFFX, ZO_OFFY, 20, 20, 1);
    REQUIRE(w);
    struct { int in, want; } v[] = { { -1, 0 }, { -100000, 0 }, { 0x8000, 0x7fff }, { 0x7fffffff, 0x7fff }, { 0x7fff, 0x7fff }, { 0, 0 } };
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) {
        CHECK(UN(NtUserMoveWindow)(w, ZO_OFFX, ZO_OFFY, v[i].in, v[i].in, FALSE));
        RECT r = zo_wr(w);
        CHECKF(r.right - r.left == v[i].want && r.bottom - r.top == v[i].want, "in=%d got %ldx%ld want %d", v[i].in, r.right - r.left, r.bottom - r.top, v[i].want);
    }
    UN(NtUserMoveWindow)(w, ZO_OFFX, ZO_OFFY, 20, 20, FALSE);
    zo_reset();
}

TEST(win_position_zorder_move_invalid_hwnd) {
    NEED_GUI(); NEED_NT(NtUserMoveWindow);
    HWND bad[] = { NULL, (HWND)1, (HWND)0xFFFF, (HWND)(ULONG_PTR)-1 };
    for (unsigned i = 0; i < 4; i++) {
        SetLastError(0);
        CHECKF(!UN(NtUserMoveWindow)(bad[i], 0, 0, 10, 10, TRUE), "hwnd %p", bad[i]);
        CHECKF(GetLastError() == 1400 || GetLastError() == 87, "hwnd %p error %lu", bad[i], GetLastError());
    }
    SetLastError(0);
    CHECK(!UN(NtUserMoveWindow)(NULL, 0, 0, 10, 10, TRUE));
    EXPECT_ERR(1400);
}

TEST(win_position_zorder_move_destroyed_window) {
    NEED_GUI();
    HWND w = zo_top("mvgone", ZO_OFFX, ZO_OFFY, 20, 20, 1);
    REQUIRE(w);
    U(DestroyWindow)(w);
    SetLastError(0);
    CHECK(!U(MoveWindow)(w, 0, 0, 10, 10, TRUE));
    EXPECT_ERR(1400);
    zo_reset();
}

TEST(win_position_zorder_move_desktop_refused) {
    NEED_GUI(); NEED_NT(NtUserMoveWindow);
    HWND d = zo_desktop();
    REQUIRE(d);
    RECT before; U(GetWindowRect)(d, &before);
    CHECK(!UN(NtUserMoveWindow)(d, before.left, before.top, before.right - before.left, before.bottom - before.top, FALSE));
    RECT after; U(GetWindowRect)(d, &after);
    CHECK(zo_same_rect(&before, &after));
}

TEST(win_position_zorder_move_sends_nozorder_noactivate) {
    NEED_GUI();
    HWND w = zo_top("mvflags", ZO_OFFX, ZO_OFFY, 100, 80, 1);
    REQUIRE(w);
    zo_log_clear(); zo_log_on = 1;
    CHECK(U(MoveWindow)(w, ZO_OFFX + 10, ZO_OFFY + 10, 90, 70, TRUE));
    int i = zo_log_find(w, WM_WINDOWPOSCHANGING, 0);
    REQUIRE(i >= 0);
    CHECK((zo_log[i].wp.flags & (SWP_NOZORDER | SWP_NOACTIVATE)) == (SWP_NOZORDER | SWP_NOACTIVATE));
    CHECK(!(zo_log[i].wp.flags & SWP_NOREDRAW));
    zo_log_clear();
    CHECK(U(MoveWindow)(w, ZO_OFFX + 20, ZO_OFFY + 10, 90, 70, FALSE));
    i = zo_log_find(w, WM_WINDOWPOSCHANGING, 0);
    REQUIRE(i >= 0);
    CHECK(zo_log[i].wp.flags & SWP_NOREDRAW);      /* bRepaint == FALSE */
    zo_reset();
}

TEST(win_position_zorder_move_keeps_zorder) {
    NEED_GUI();
    HWND w[3];
    for (int i = 0; i < 3; i++) { w[i] = zo_top("mvz", ZO_OFFX + i, ZO_OFFY, 40, 40, 1); REQUIRE(w[i]); }
    HWND in[3] = { w[0], w[1], w[2] }, got[3];
    zo_arrange(in, 3);
    CHECK(U(MoveWindow)(w[2], ZO_OFFX + 100, ZO_OFFY, 40, 40, TRUE));
    CHECK(zo_rank(in, 3, got) == 3 && zo_same(got, in, 3));
    zo_reset();
}

TEST(win_position_zorder_move_child_is_parent_relative) {
    NEED_GUI();
    HWND p = zo_top("mvp", ZO_OFFX + 50, ZO_OFFY + 60, 200, 150, 1);
    REQUIRE(p);
    HWND c = zo_child(p, "mvc", 1, 2, 30, 40, WS_VISIBLE, 0);
    REQUIRE(c);
    CHECK(U(MoveWindow)(c, 7, 9, 31, 41, TRUE));
    RECT r = zo_wr(c), pc = zo_cr_screen(p);
    EXPECT_EQ(r.left, pc.left + 7); EXPECT_EQ(r.top, pc.top + 9);
    EXPECT_EQ(r.right - r.left, 31);
    POINT pt = { r.left, r.top };
    U(ScreenToClient)(p, &pt);
    EXPECT_EQ(pt.x, 7); EXPECT_EQ(pt.y, 9);
    zo_reset();
}

static ULONG_PTR mvt_fn(void *p) { HWND h = (HWND)p; return (ULONG_PTR)U(MoveWindow)(h, ZO_OFFX + 70, ZO_OFFY + 71, 51, 52, TRUE); }
TEST(win_position_zorder_move_cross_thread_window) {
    NEED_GUI();
    struct zo_thread *t = zo_thread_start();
    if (!t) { t_skip("could not start GUI thread"); return; }
    HWND w = zo_thread_window(t, "mvx", ZO_OFFX, ZO_OFFY, 40, 40, 1);
    if (!w) { zo_thread_stop(t); t_skip("window on worker thread failed"); return; }
    CHECK(U(MoveWindow)(w, ZO_OFFX + 5, ZO_OFFY + 6, 41, 42, TRUE));
    RECT r = zo_wr(w);
    EXPECT_EQ(r.left, ZO_OFFX + 5); EXPECT_EQ(r.right - r.left, 41);
    CHECK(zo_thread_call(t, mvt_fn, w) == 1);
    r = zo_wr(w);
    EXPECT_EQ(r.left, ZO_OFFX + 70); EXPECT_EQ(r.bottom - r.top, 52);
    zo_thread_stop(t);
}

TEST(win_position_zorder_move_stub_and_user32_agree) {
    NEED_GUI(); NEED_NT(NtUserMoveWindow);
    HWND w = zo_top("mvag", ZO_OFFX, ZO_OFFY, 40, 40, 1);
    REQUIRE(w);
    U(MoveWindow)(w, ZO_OFFX + 1, ZO_OFFY + 2, 33, 34, TRUE);
    RECT a = zo_wr(w);
    UN(NtUserMoveWindow)(w, ZO_OFFX + 1, ZO_OFFY + 2, 33, 34, TRUE);
    RECT b = zo_wr(w);
    CHECK(zo_same_rect(&a, &b));
    FARPROC p = t_proc_in("win32u.dll", "NtUserMoveWindow");
    if (!t_is_wine()) EXPECT_EQ(k_stub_number(p), 4189);
    zo_reset();
}

TEST(win_position_zorder_move_heap_rect_matches) {
    NEED_GUI();
    if (t_is_wine() || !zo_heap_ok()) { t_skip("desktop heap windows not readable"); return; }
    HWND w = zo_top("mvh", ZO_OFFX, ZO_OFFY, 40, 40, 1);
    REQUIRE(w);
    U(MoveWindow)(w, ZO_OFFX + 9, ZO_OFFY + 8, 77, 66, TRUE);
    RECT hw; CHECK(zo_rect_heap(w, ZO_WND_RcWnd, &hw));
    EXPECT_EQ(hw.left, ZO_OFFX + 9); EXPECT_EQ(hw.top, ZO_OFFY + 8);
    EXPECT_EQ(hw.right - hw.left, 77); EXPECT_EQ(hw.bottom - hw.top, 66);
    zo_reset();
}
