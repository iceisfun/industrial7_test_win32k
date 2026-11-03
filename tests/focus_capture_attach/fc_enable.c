/* EnableWindow (NtUserCallHwndParamLockSafe 0x7d) and
 * IsWindowEnabled (client-side: WS_DISABLED bit of the window object in the desktop heap). */
#include "fc.h"

static BOOL dis(HWND h) { return !U(IsWindowEnabled)(h); }

TEST(fca_enable_invalid_handles) {
    FC_NEED(); REQUIRE(UN(NtUserCallHwndParamLockSafe));
    HWND a = fc_top(), s = fc_top(); REQUIRE(a && s);
    fc_destroy(s);
    HWND bad[] = { NULL, (HWND)(ULONG_PTR)1, (HWND)(ULONG_PTR)0xDEADBEEF, s, (HWND)((ULONG_PTR)a ^ 0x00030000) };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        for (int f = 0; f < 2; f++) {
            SET_ERR(0);
            BOOL r = U(EnableWindow)(bad[i], f);
            CHECKF(!r, "EnableWindow(%p,%d) -> %d", bad[i], f, r);
            EXPECT_ERR(1400);
        }
        SET_ERR(0);
        CHECK(!U(IsWindowEnabled)(bad[i]));
    }
    fc_destroy(a);
}

TEST(fca_enable_invalid_handles_direct) {
    FC_NEED(); if (t_is_wine()) { FC_SKIP("win32k call codes"); return; }
    REQUIRE(UN(NtUserCallHwndParamLockSafe));
    SET_ERR(0);
    EXPECT_CALL_EQ(UN(NtUserCallHwndParamLockSafe)((HWND)(ULONG_PTR)0x12345678, 0, FC_CALL_ENABLEWINDOW), 0);
    EXPECT_ERR(1400);
    SET_ERR(0);
    EXPECT_CALL_EQ(UN(NtUserCallHwndParamLockSafe)(NULL, 1, FC_CALL_ENABLEWINDOW), 0);
    EXPECT_ERR(1400);
}

TEST(fca_enable_direct_matches_user32) {
    FC_NEED(); if (t_is_wine()) { FC_SKIP("win32k call codes"); return; }
    REQUIRE(UN(NtUserCallHwndParamLockSafe));
    HWND a = fc_top(); REQUIRE(a);
    EXPECT_CALL_EQ(UN(NtUserCallHwndParamLockSafe)(a, 0, FC_CALL_ENABLEWINDOW), 0);   /* was enabled */
    CHECK(dis(a));
    EXPECT_CALL_EQ(UN(NtUserCallHwndParamLockSafe)(a, 0, FC_CALL_ENABLEWINDOW), 1);   /* already disabled */
    EXPECT_CALL_EQ(UN(NtUserCallHwndParamLockSafe)(a, 1, FC_CALL_ENABLEWINDOW), 1);
    CHECK(!dis(a));
    EXPECT_CALL_EQ(UN(NtUserCallHwndParamLockSafe)(a, 1, FC_CALL_ENABLEWINDOW), 0);
    fc_destroy(a);
}

TEST(fca_enable_return_values) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    CHECK(U(IsWindowEnabled)(a));
    BOOL r = U(EnableWindow)(a, FALSE);
    EXPECT_EQ(r, FALSE);          /* returns whether the window WAS disabled */
    CHECK(dis(a));
    r = U(EnableWindow)(a, FALSE);
    CHECK(r);                     /* it was disabled */
    CHECK(dis(a));
    r = U(EnableWindow)(a, TRUE);
    CHECK(r);                     /* was disabled */
    CHECK(!dis(a));
    r = U(EnableWindow)(a, TRUE);
    EXPECT_EQ(r, FALSE);          /* was enabled */
    fc_destroy(a);
}

TEST(fca_enable_nonzero_means_enable) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    int vals[] = { 2, 0x100, -1, 0x7FFFFFFF };
    for (unsigned i = 0; i < sizeof vals / sizeof vals[0]; i++) {
        U(EnableWindow)(a, FALSE);
        CHECK(dis(a));
        BOOL r = U(EnableWindow)(a, vals[i]);   /* the enable flag is normalised to 0/1 */
        CHECKF(r, "value %d", vals[i]);
        CHECKF(!dis(a), "value %d did not enable", vals[i]);
    }
    fc_destroy(a);
}

TEST(fca_enable_style_bit) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    CHECK(!(U(GetWindowLongW)(a, GWL_STYLE) & WS_DISABLED));
    U(EnableWindow)(a, FALSE);
    CHECK(U(GetWindowLongW)(a, GWL_STYLE) & WS_DISABLED);
    U(EnableWindow)(a, TRUE);
    CHECK(!(U(GetWindowLongW)(a, GWL_STYLE) & WS_DISABLED));
    fc_destroy(a);
}

TEST(fca_enable_created_disabled) {
    FC_NEED();
    HWND a = fc_make(WS_POPUP | WS_DISABLED, WS_EX_TOOLWINDOW, NULL, 1); REQUIRE(a);
    CHECK(dis(a));
    BOOL r = U(EnableWindow)(a, TRUE);
    CHECK(r);
    CHECK(!dis(a));
    fc_destroy(a);
}

TEST(fca_enable_client_object_bit) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    unsigned char b = 0;
    char *wnd = (char *)k_wnd_ptr(a);
    if (!wnd || !k_try_read_u8(wnd + 0x1f, &b)) { fc_destroy(a); FC_SKIP("client window object not reachable"); }
    /* user32!IsWindowEnabled reads bit 3 of the byte at +0x1f of the desktop-heap window object. */
    CHECK((b & 0x08) == 0);
    U(EnableWindow)(a, FALSE);
    CHECK(k_try_read_u8(wnd + 0x1f, &b)); CHECK((b & 0x08) != 0);
    CHECK(!U(IsWindowEnabled)(a));
    U(EnableWindow)(a, TRUE);
    CHECK(k_try_read_u8(wnd + 0x1f, &b)); CHECK((b & 0x08) == 0);
    CHECK(U(IsWindowEnabled)(a));
    fc_destroy(a);
}

TEST(fca_enable_wm_enable_only_on_change) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    U(EnableWindow)(a, FALSE);
    int e = fc_find(WM_ENABLE, a, 0);
    CHECK(e >= 0);
    if (e >= 0) { EXPECT_EQ(fc_log[e].wp, 0); CHECK(fc_log[e].style_now & WS_DISABLED); }   /* style already updated when delivered */
    fc_reset();
    U(EnableWindow)(a, FALSE);
    EXPECT_EQ(fc_count(WM_ENABLE, a), 0);
    U(EnableWindow)(a, TRUE);
    e = fc_find(WM_ENABLE, a, 0);
    CHECK(e >= 0);
    if (e >= 0) { EXPECT_EQ(fc_log[e].wp, 1); CHECK(!(fc_log[e].style_now & WS_DISABLED)); }
    fc_reset();
    U(EnableWindow)(a, TRUE);
    EXPECT_EQ(fc_count(WM_ENABLE, a), 0);
    fc_destroy(a);
}

TEST(fca_enable_cancelmode_before_disable) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    U(EnableWindow)(a, FALSE);
    int c = fc_find(WM_CANCELMODE, a, 0), e = fc_find(WM_ENABLE, a, 0);
    CHECK(c >= 0); CHECK(e >= 0);
    if (c >= 0) CHECK(!(fc_log[c].style_now & WS_DISABLED));   /* WM_CANCELMODE is sent before the bit is set */
    if (c >= 0 && e >= 0) CHECK(c < e);
    fc_reset();
    U(EnableWindow)(a, FALSE);                                  /* disabling again still sends WM_CANCELMODE */
    CHECK(fc_count(WM_CANCELMODE, a) == 1);
    fc_reset();
    U(EnableWindow)(a, TRUE);
    EXPECT_EQ(fc_count(WM_CANCELMODE, a), 0);                   /* enabling never does */
    fc_destroy(a);
}

TEST(fca_enable_disable_focus_window_removes_focus) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    U(SetFocus)(a);
    if (U(GetFocus)() != a) { fc_destroy(a); FC_SKIP("focus not taken"); }
    fc_reset();
    U(EnableWindow)(a, FALSE);
    EXPECT_EQ(U(GetFocus)(), NULL);   /* a disabled window that has focus loses focus */
    int c = fc_find(WM_CANCELMODE, a, 0), k = fc_find(WM_KILLFOCUS, a, 0), e = fc_find(WM_ENABLE, a, 0);
    CHECK(c >= 0); CHECK(k >= 0); CHECK(e >= 0);
    if (c >= 0 && k >= 0) CHECK(c < k);
    if (k >= 0 && e >= 0) CHECK(k < e);
    if (k >= 0) { EXPECT_EQ(fc_log[k].wp, 0); CHECK(!(fc_log[k].style_now & WS_DISABLED)); }
    U(EnableWindow)(a, TRUE);
    EXPECT_EQ(U(GetFocus)(), NULL);   /* enabling does not give the focus back */
    fc_destroy(a);
}

TEST(fca_enable_disable_other_window_keeps_focus) {
    FC_NEED();
    HWND a = fc_top(), b = fc_top(); REQUIRE(a && b);
    fc_quiesce();
    U(SetFocus)(a);
    if (U(GetFocus)() != a) { fc_destroy(a); fc_destroy(b); FC_SKIP("focus not taken"); }
    fc_reset();
    U(EnableWindow)(b, FALSE);
    EXPECT_EQ(U(GetFocus)(), a);
    EXPECT_EQ(fc_count(WM_KILLFOCUS, NULL), 0);
    U(EnableWindow)(b, TRUE);
    fc_destroy(a); fc_destroy(b);
}

TEST(fca_enable_disable_parent_of_focus_child) {
    FC_NEED();
    HWND p = fc_top(); REQUIRE(p);
    HWND c = fc_child(p); REQUIRE(c);
    fc_quiesce();
    U(SetFocus)(c);
    if (U(GetFocus)() != c) { fc_destroy(p); FC_SKIP("child focus not taken"); }
    fc_reset();
    U(EnableWindow)(p, FALSE);
    HWND f = U(GetFocus)();
    /* disabling a window only clears focus when the disabled window itself is the focus window. */
    t_note("focus after disabling the parent of the focus child: %s", f == c ? "child kept it" : f ? "other" : "cleared");
    EXPECT_EQ(fc_count(WM_ENABLE, c), 0);           /* children get no WM_ENABLE */
    CHECK(U(IsWindowEnabled)(c));
    EXPECT_EQ(fc_count(WM_ENABLE, p), 1);
    U(EnableWindow)(p, TRUE);
    fc_destroy(p);
}

TEST(fca_enable_disable_child_with_focus) {
    FC_NEED();
    HWND p = fc_top(); REQUIRE(p);
    HWND c = fc_child(p); REQUIRE(c);
    fc_quiesce();
    U(SetFocus)(c);
    if (U(GetFocus)() != c) { fc_destroy(p); FC_SKIP("child focus not taken"); }
    U(EnableWindow)(c, FALSE);
    EXPECT_EQ(U(GetFocus)(), NULL);
    CHECK(U(IsWindowEnabled)(p));
    fc_destroy(p);
}

TEST(fca_enable_desktop_window_untouched) {
    FC_NEED(); REQUIRE(U(GetDesktopWindow));
    HWND d = U(GetDesktopWindow)();
    BOOL was = U(IsWindowEnabled)(d);
    SET_ERR(0);
    BOOL r = U(EnableWindow)(d, FALSE);
    BOOL now = U(IsWindowEnabled)(d);
    if (!now) U(EnableWindow)(d, TRUE);   /* restore, whatever happened */
    CHECK(was);
    /* NtUserCallHwndParamLockSafe(0x7d) skips windows whose fnid is 0x29d (desktop) or 0x29f and returns 0. */
    EXPECT_EQ(r, FALSE);
    CHECK(now);
}

static DWORD_PTR thr_enable(void *p) { HWND *a = (HWND *)p; a[1] = (HWND)(ULONG_PTR)U(EnableWindow)(a[0], FALSE); return 0; }

TEST(fca_enable_cross_thread) {
    FC_NEED();
    struct fc_thr t; if (!fc_thr_start(&t)) { FC_SKIP("worker thread failed"); return; }
    fc_quiesce();
    BOOL r = U(EnableWindow)(t.w[0], FALSE);   /* no queue restriction: the WM_ENABLE is sent across threads */
    EXPECT_EQ(r, FALSE);
    CHECK(dis(t.w[0]));
    int e = fc_find(WM_ENABLE, t.w[0], 0);
    CHECK(e >= 0);
    if (e >= 0) { EXPECT_EQ(fc_log[e].tid, t.tid); EXPECT_EQ(fc_log[e].wp, 0); }
    int c = fc_find(WM_CANCELMODE, t.w[0], 0);
    CHECK(c >= 0);
    if (c >= 0) EXPECT_EQ(fc_log[c].tid, t.tid);
    r = U(EnableWindow)(t.w[0], TRUE);
    CHECK(r);
    CHECK(!dis(t.w[0]));
    HWND args[2] = { t.w[1], 0 };
    CHECK(fc_thr_call(&t, thr_enable, args, NULL));   /* and from the owning thread */
    CHECK(dis(t.w[1]));
    fc_thr_stop(&t);
}

TEST(fca_enable_disabled_window_still_destroyable_and_showable) {
    FC_NEED();
    HWND a = fc_hidden(); REQUIRE(a);
    U(EnableWindow)(a, FALSE);
    U(ShowWindow)(a, SW_SHOWNOACTIVATE);
    CHECK(dis(a));
    U(DestroyWindow)(a);
    CHECK(!U(IsWindow)(a));
}

TEST(fca_enable_disabled_window_messages_still_delivered) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    U(EnableWindow)(a, FALSE);
    fc_reset();
    LRESULT r = U(SendMessageW)(a, WM_ENABLE, 1, 0);   /* programmatic messages are not filtered by WS_DISABLED */
    (void)r;
    EXPECT_EQ(fc_count(WM_ENABLE, a), 1);
    CHECK(dis(a));                                       /* a bare message does not change the style bit */
    U(EnableWindow)(a, TRUE);
    fc_destroy(a);
}

TEST(fca_enable_disabled_window_activation_observed) {
    FC_NEED();
    HWND a = fc_top(), b = fc_top(); REQUIRE(a && b);
    fc_quiesce();
    U(SetActiveWindow)(a);
    U(EnableWindow)(b, FALSE);
    HWND r = U(SetActiveWindow)(b);
    t_note("SetActiveWindow(disabled b): ret=%p now-active-is-b=%d", r, U(GetActiveWindow)() == b);
    U(EnableWindow)(b, TRUE);
    fc_destroy(a); fc_destroy(b);
}
