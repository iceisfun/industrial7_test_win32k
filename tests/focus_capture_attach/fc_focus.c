/* NtUserSetFocus / GetFocus (NtUserGetThreadState(0)): win32kfull!NtUserSetFocus
 * and the focus notifications. */
#include "fc.h"

/* Focus only sticks when the window's root could be activated in this queue; a test that wants to
 * assert on the resulting state first makes sure the call worked. */
static int focus_took(HWND h) { return U(GetFocus)() == h; }

/* window handle validation failures set ERROR_INVALID_WINDOW_HANDLE (1400). */
static void bad_handles(HWND *out, int *n, HWND stale, HWND live) {
    int i = 0;
    out[i++] = (HWND)(ULONG_PTR)1;
    out[i++] = (HWND)(ULONG_PTR)0x2;
    out[i++] = (HWND)(ULONG_PTR)0x12345678;
    out[i++] = (HWND)(ULONG_PTR)0xDEADBEEF;
    out[i++] = (HWND)(ULONG_PTR)0x7FFF0001;
    out[i++] = stale;                                              /* destroyed window */
    out[i++] = (HWND)((ULONG_PTR)live ^ 0x00030000);               /* right index, wrong uniq */
    *n = i;
}

TEST(fca_focus_getfocus_matches_threadstate) {
    FC_NEED(); if (t_is_wine()) { FC_SKIP("syscall info classes are win32k-specific"); return; }
    UN(NtUserGetThreadState); REQUIRE(UN(NtUserGetThreadState));
    HWND w = fc_top(); REQUIRE(w);
    fc_quiesce();
    EXPECT_EQ(U(GetFocus)(), UN(NtUserGetThreadState)(FC_TS_FOCUS));
    U(SetFocus)(w);
    EXPECT_EQ(U(GetFocus)(), UN(NtUserGetThreadState)(FC_TS_FOCUS));
    fc_quiesce();
    EXPECT_EQ(UN(NtUserGetThreadState)(FC_TS_FOCUS), 0);
    fc_destroy(w);
}

TEST(fca_focus_invalid_handle_user32) {
    FC_NEED();
    HWND w = fc_top(), s = fc_top(); REQUIRE(w && s);
    fc_destroy(s);
    HWND bad[16]; int n; bad_handles(bad, &n, s, w);
    fc_quiesce();
    for (int i = 0; i < n; i++) {
        SET_ERR(0);
        HWND r = U(SetFocus)(bad[i]);
        CHECKF(r == NULL, "SetFocus(%p) returned %p", bad[i], r);
        EXPECT_ERR(1400);
        CHECKF(U(GetFocus)() == NULL, "focus changed by invalid SetFocus(%p)", bad[i]);
    }
    fc_destroy(w);
}

TEST(fca_focus_invalid_handle_direct) {
    FC_NEED(); UN(NtUserSetFocus); REQUIRE(UN(NtUserSetFocus));
    HWND w = fc_top(), s = fc_top(); REQUIRE(w && s);
    fc_destroy(s);
    HWND bad[16]; int n; bad_handles(bad, &n, s, w);
    fc_quiesce();
    for (int i = 0; i < n; i++) {
        SET_ERR(0);
        HWND r = UN(NtUserSetFocus)(bad[i]);
        CHECKF(r == NULL, "NtUserSetFocus(%p) returned %p", bad[i], r);
        EXPECT_ERR(1400);
    }
    fc_destroy(w);
}

TEST(fca_focus_invalid_does_not_disturb_existing_focus) {
    FC_NEED();
    HWND w = fc_top(); REQUIRE(w);
    fc_quiesce();
    U(SetFocus)(w);
    if (!focus_took(w)) { t_note("SetFocus(w) did not take focus here"); fc_destroy(w); FC_SKIP("no focus"); }
    SET_ERR(0);
    CHECK(U(SetFocus)((HWND)(ULONG_PTR)0x12345678) == NULL);
    EXPECT_ERR(1400);
    EXPECT_EQ(U(GetFocus)(), w);   /* validation fails before the focus change runs */
    fc_destroy(w);
}

TEST(fca_focus_null_clears_and_returns_previous) {
    FC_NEED();
    HWND w = fc_top(); REQUIRE(w);
    fc_quiesce();
    U(SetFocus)(w);
    if (!focus_took(w)) { fc_destroy(w); FC_SKIP("SetFocus did not take focus"); }
    fc_reset();
    HWND r = U(SetFocus)(NULL);
    EXPECT_EQ(r, w);                      /* previous focus window handle is returned */
    EXPECT_EQ(U(GetFocus)(), NULL);
    int k = fc_find(WM_KILLFOCUS, w, 0);
    CHECK(k >= 0);
    if (k >= 0) EXPECT_EQ(fc_log[k].wp, 0); /* the focus notification (thread, 0): WM_KILLFOCUS wParam = NULL */
    EXPECT_EQ(fc_count(WM_SETFOCUS, NULL), 0);
    fc_destroy(w);
}

TEST(fca_focus_null_when_no_focus) {
    FC_NEED();
    HWND w = fc_top(); REQUIRE(w);
    fc_quiesce();
    fc_reset();
    HWND r = U(SetFocus)(NULL);
    EXPECT_EQ(r, NULL);
    EXPECT_EQ(U(GetFocus)(), NULL);
    EXPECT_EQ(fc_count(WM_KILLFOCUS, NULL), 0); /* no old focus window: nothing to notify */
    fc_destroy(w);
}

TEST(fca_focus_set_get_roundtrip) {
    FC_NEED();
    HWND w = fc_top(); REQUIRE(w);
    fc_quiesce();
    HWND prev = U(SetFocus)(w);
    /* Windows 10: a window outside the active one is activated first and DefWindowProc(WM_ACTIVATE) hands it the
     * focus, so the call reports the window itself rather than "no previous focus" */
    EXPECT_EQ(prev, w);
    if (!focus_took(w)) { t_note("focus not taken: GetFocus=%p err=%lu", U(GetFocus)(), GetLastError()); fc_destroy(w); FC_SKIP("no focus"); }
    CHECK(fc_find(WM_SETFOCUS, w, 0) >= 0);
    fc_destroy(w);
}

TEST(fca_focus_returns_previous_window) {
    FC_NEED();
    /* inside one active top-level window SetFocus returns the previous focus window */
    HWND p = fc_top(); REQUIRE(p);
    HWND c1 = fc_child(p), c2 = fc_child(p); REQUIRE(c1 && c2);
    fc_quiesce();
    U(SetFocus)(p);
    if (!focus_took(p)) { fc_destroy(p); FC_SKIP("no focus"); }
    HWND r = U(SetFocus)(c1);
    EXPECT_EQ(r, p);
    EXPECT_EQ(U(GetFocus)(), c1);
    r = U(SetFocus)(c2);
    EXPECT_EQ(r, c1);
    EXPECT_EQ(U(GetFocus)(), c2);
    r = U(SetFocus)(c1);
    EXPECT_EQ(r, c2);
    EXPECT_EQ(U(GetFocus)(), c1);
    fc_destroy(p);
    /* between two top-level windows the target is activated first, DefWindowProc(WM_ACTIVATE) gives it the focus,
     * and the call then reports the target itself */
    HWND a = fc_top(), b = fc_top(); REQUIRE(a && b);
    fc_quiesce();
    U(SetFocus)(a);
    if (!focus_took(a)) { fc_destroy(a); fc_destroy(b); FC_SKIP("no focus"); }
    r = U(SetFocus)(b);
    EXPECT_EQ(r, b);
    EXPECT_EQ(U(GetFocus)(), b);
    r = U(SetFocus)(a);
    EXPECT_EQ(r, a);
    EXPECT_EQ(U(GetFocus)(), a);
    fc_destroy(a); fc_destroy(b);
}

TEST(fca_focus_direct_stub_returns_previous_window) {
    FC_NEED(); UN(NtUserSetFocus); REQUIRE(UN(NtUserSetFocus));
    HWND p = fc_top(); REQUIRE(p);
    HWND a = fc_child(p), b = fc_child(p); REQUIRE(a && b);
    fc_quiesce();
    UN(NtUserSetFocus)(p);
    if (!focus_took(p)) { fc_destroy(p); FC_SKIP("no focus"); }
    EXPECT_CALL_EQ(UN(NtUserSetFocus)(a), p);       /* inside the active window: the previous focus window */
    EXPECT_CALL_EQ(UN(NtUserSetFocus)(b), a);
    EXPECT_EQ(U(GetFocus)(), b);
    EXPECT_CALL_EQ(UN(NtUserSetFocus)(NULL), b);
    EXPECT_EQ(U(GetFocus)(), NULL);
    fc_destroy(p);
}

TEST(fca_focus_kill_before_set_with_wparams) {
    FC_NEED();
    HWND a = fc_top(), b = fc_top(); REQUIRE(a && b);
    fc_quiesce();
    U(SetFocus)(a);
    if (!focus_took(a)) { fc_destroy(a); fc_destroy(b); FC_SKIP("no focus"); }
    fc_reset();
    U(SetFocus)(b);
    int k = fc_find(WM_KILLFOCUS, a, 0), s = fc_find(WM_SETFOCUS, b, 0);
    CHECK(k >= 0); CHECK(s >= 0);
    if (k >= 0 && s >= 0) {
        CHECK(k < s);                           /* the focus notification path: KILLFOCUS to old, then SETFOCUS to new */
        EXPECT_EQ(fc_log[k].wp, (WPARAM)b);     /* KILLFOCUS wParam = window receiving focus */
        EXPECT_EQ(fc_log[s].wp, (WPARAM)a);     /* SETFOCUS wParam = window losing focus */
    }
    fc_destroy(a); fc_destroy(b);
}

TEST(fca_focus_first_setfocus_wparam_zero) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    fc_reset();
    U(SetFocus)(a);
    if (!focus_took(a)) { fc_destroy(a); FC_SKIP("no focus"); }
    int s = fc_find(WM_SETFOCUS, a, 0);
    CHECK(s >= 0);
    if (s >= 0) EXPECT_EQ(fc_log[s].wp, 0);   /* no previous focus window: wParam is 0 (the first notification, from the activation) */
    /* Windows 10 follows it with a WM_KILLFOCUS/WM_SETFOCUS pair for the window itself (wParam = the window); nobody else is notified */
    EXPECT_EQ(fc_count(WM_KILLFOCUS, NULL), fc_count(WM_KILLFOCUS, a));
    fc_destroy(a);
}

TEST(fca_focus_queue_state_during_notifications) {
    FC_NEED();
    HWND a = fc_top(), b = fc_top(); REQUIRE(a && b);
    fc_quiesce();
    U(SetFocus)(a);
    if (!focus_took(a)) { fc_destroy(a); fc_destroy(b); FC_SKIP("no focus"); }
    fc_reset();
    U(SetFocus)(b);
    int k = fc_find(WM_KILLFOCUS, a, 0), s = fc_find(WM_SETFOCUS, b, 0);
    CHECK(k >= 0); CHECK(s >= 0);
    /* the focus notification path assigns the queue focus window before it sends either message. */
    if (k >= 0) EXPECT_EQ(fc_log[k].focus_now, b);
    if (s >= 0) EXPECT_EQ(fc_log[s].focus_now, b);
    fc_reset();
    U(SetFocus)(NULL);
    k = fc_find(WM_KILLFOCUS, b, 0);
    CHECK(k >= 0);
    if (k >= 0) EXPECT_EQ(fc_log[k].focus_now, NULL);
    fc_destroy(a); fc_destroy(b);
}

TEST(fca_focus_same_window_sends_nothing) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    U(SetFocus)(a);
    if (!focus_took(a)) { fc_destroy(a); FC_SKIP("no focus"); }
    fc_reset();
    HWND r = U(SetFocus)(a);
    EXPECT_EQ(r, a);                          /* previous focus is the window itself */
    EXPECT_EQ(fc_count(WM_SETFOCUS, NULL), 0); /* the focus change only notifies when the queue focus window != target */
    EXPECT_EQ(fc_count(WM_KILLFOCUS, NULL), 0);
    EXPECT_EQ(U(GetFocus)(), a);
    fc_destroy(a);
}

TEST(fca_focus_disabled_window_fails_invalid_parameter) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    U(EnableWindow)(a, FALSE);
    fc_reset();
    SET_ERR(0);
    HWND r = U(SetFocus)(a);
    CHECK(r == NULL);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);  /* the focus change: WS_DISABLED in the non-child chain -> error 0x57 */
    EXPECT_EQ(U(GetFocus)(), NULL);
    EXPECT_EQ(fc_count(WM_SETFOCUS, NULL), 0);
    U(EnableWindow)(a, TRUE);
    U(SetFocus)(a);
    if (focus_took(a)) EXPECT_EQ(U(GetFocus)(), a);  /* re-enabled: accepted again */
    fc_destroy(a);
}

TEST(fca_focus_disabled_window_fails_direct_stub) {
    FC_NEED(); UN(NtUserSetFocus); REQUIRE(UN(NtUserSetFocus));
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    U(EnableWindow)(a, FALSE);
    SET_ERR(0);
    CHECK(UN(NtUserSetFocus)(a) == NULL);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    U(EnableWindow)(a, TRUE);
    fc_destroy(a);
}

TEST(fca_focus_child_of_disabled_parent_fails) {
    FC_NEED();
    HWND p = fc_top(); REQUIRE(p);
    HWND c = fc_child(p); REQUIRE(c);
    fc_quiesce();
    U(EnableWindow)(p, FALSE);
    SET_ERR(0);
    CHECK(U(SetFocus)(c) == NULL);   /* the loop re-checks every WS_CHILD ancestor up to the first non-child */
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    EXPECT_EQ(U(GetFocus)(), NULL);
    CHECK(U(IsWindowEnabled)(c));    /* IsWindowEnabled looks only at the window's own WS_DISABLED bit */
    U(EnableWindow)(p, TRUE);
    fc_destroy(p);
}

TEST(fca_focus_disabled_child_fails) {
    FC_NEED();
    HWND p = fc_top(); REQUIRE(p);
    HWND c = fc_child(p); REQUIRE(c);
    fc_quiesce();
    U(EnableWindow)(c, FALSE);
    SET_ERR(0);
    CHECK(U(SetFocus)(c) == NULL);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    CHECK(U(IsWindowEnabled)(p));
    U(EnableWindow)(c, TRUE);
    fc_destroy(p);
}

TEST(fca_focus_minimized_window_fails) {
    FC_NEED();
    HWND a = fc_make(WS_POPUP | WS_MINIMIZE, WS_EX_TOOLWINDOW, NULL, 0); REQUIRE(a);
    if (!(U(GetWindowLongW)(a, GWL_STYLE) & WS_MINIMIZE)) { fc_destroy(a); FC_SKIP("WS_MINIMIZE did not stick on a hidden window"); }
    fc_quiesce();
    SET_ERR(0);
    CHECK(U(SetFocus)(a) == NULL);   /* WS_MINIMIZE (byte 3 bit 0x20) takes the same 0x57 exit as WS_DISABLED */
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    EXPECT_EQ(U(GetFocus)(), NULL);
    fc_destroy(a);
}

TEST(fca_focus_hidden_window_observed) {
    FC_NEED();
    HWND a = fc_hidden(); REQUIRE(a);
    fc_quiesce();
    SET_ERR(0);
    HWND r = U(SetFocus)(a);
    DWORD e = GetLastError();
    t_note("SetFocus(hidden popup): ret=%p err=%lu GetFocus==w:%d", r, e, U(GetFocus)() == a);
    if (U(GetFocus)() == a) CHECK(fc_find(WM_SETFOCUS, a, 0) >= 0);
    fc_destroy(a);
}

TEST(fca_focus_child_gets_focus_root_active) {
    FC_NEED();
    HWND p = fc_top(); REQUIRE(p);
    HWND c = fc_child(p); REQUIRE(c);
    fc_quiesce();
    U(SetFocus)(c);
    if (!focus_took(c)) { fc_destroy(p); FC_SKIP("child focus not taken"); }
    EXPECT_EQ(U(GetFocus)(), c);
    EXPECT_EQ(U(GetActiveWindow)(), p);   /* the focus change activates the non-child ancestor */
    int s = -1, n = fc_total();
    for (int i = 0; i < n; i++) if (fc_log[i].msg == WM_SETFOCUS && fc_log[i].hwnd == c) s = i;
    CHECK(s >= 0);
    fc_destroy(p);
}

TEST(fca_focus_child_to_parent_and_back) {
    FC_NEED();
    HWND p = fc_top(); REQUIRE(p);
    HWND c = fc_child(p); REQUIRE(c);
    fc_quiesce();
    U(SetFocus)(c);
    if (!focus_took(c)) { fc_destroy(p); FC_SKIP("child focus not taken"); }
    fc_reset();
    EXPECT_CALL_EQ(U(SetFocus)(p), c);
    EXPECT_EQ(U(GetFocus)(), p);
    int k = fc_find(WM_KILLFOCUS, c, 0), s = fc_find(WM_SETFOCUS, p, 0);
    CHECK(k >= 0 && s >= 0 && k < s);
    if (k >= 0) EXPECT_EQ(fc_log[k].wp, (WPARAM)p);
    EXPECT_CALL_EQ(U(SetFocus)(c), p);
    EXPECT_EQ(U(GetFocus)(), c);
    fc_destroy(p);
}

/* DestroyWindow and the focus/activation notifications. Whether the destroyed focus window is sent WM_KILLFOCUS (and the
 * deactivation messages) depends on the thread's queue being the foreground queue: a process that has handed the
 * foreground back (the corpus does after every test that took it, see fc_fg_restore) is in the background state, where
 * Windows 10 sends no focus or activation messages on the destruction, only WM_DESTROY. Both states are asserted. */
TEST(fca_focus_destroy_focus_window_clears_focus) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    U(SetFocus)(a);
    if (!focus_took(a)) { fc_destroy(a); FC_SKIP("no focus"); }
    int fg = fc_own_foreground();
    fc_reset();
    U(DestroyWindow)(a);
    EXPECT_EQ(U(GetFocus)(), NULL);
    int k = fc_find(WM_KILLFOCUS, a, 0), d = fc_find(WM_DESTROY, a, 0);
    t_note("queue was %s when the focus window was destroyed", fg ? "foreground" : "background");
    CHECK(d >= 0);
    if (fg) {
        CHECK(k >= 0);
        if (k >= 0 && d >= 0) CHECK(k < d);   /* DestroyWindow takes the focus away before WM_DESTROY */
    } else {
        CHECK(k < 0);
    }
    CHECK(!U(IsWindow)(a));
}

TEST(fca_focus_destroy_focused_child) {
    FC_NEED();
    HWND p = fc_top(); REQUIRE(p);
    HWND c = fc_child(p); REQUIRE(c);
    fc_quiesce();
    U(SetFocus)(c);
    if (!focus_took(c)) { fc_destroy(p); FC_SKIP("child focus not taken"); }
    U(DestroyWindow)(c);
    HWND f = U(GetFocus)();
    CHECK(f != c);
    CHECK(f == NULL || U(IsWindow)(f));
    fc_destroy(p);
}

TEST(fca_focus_destroy_parent_with_focused_child) {
    FC_NEED();
    HWND p = fc_top(); REQUIRE(p);
    HWND c = fc_child(p); REQUIRE(c);
    fc_quiesce();
    U(SetFocus)(c);
    if (!focus_took(c)) { fc_destroy(p); FC_SKIP("child focus not taken"); }
    int fg = fc_own_foreground();
    fc_reset();
    U(DestroyWindow)(p);
    HWND f = U(GetFocus)();
    CHECK(f != c && f != p);
    int k = fc_find(WM_KILLFOCUS, c, 0), d = fc_find(WM_DESTROY, c, 0);
    t_note("queue was %s when the parent was destroyed", fg ? "foreground" : "background");
    CHECK(d >= 0);
    if (fg) {
        CHECK(k >= 0);
        if (k >= 0 && d >= 0) CHECK(k < d);
    } else {
        CHECK(k < 0);
    }
}

TEST(fca_focus_stale_handle_after_destroy) {
    FC_NEED();
    HWND a = fc_top(), b = fc_top(); REQUIRE(a && b);
    fc_quiesce();
    U(SetFocus)(a);
    fc_destroy(a);
    SET_ERR(0);
    CHECK(U(SetFocus)(a) == NULL);
    EXPECT_ERR(1400);
    fc_destroy(b);
}

TEST(fca_focus_stable_after_pump) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    U(SetFocus)(a);
    if (!focus_took(a)) { fc_destroy(a); FC_SKIP("no focus"); }
    k_pump(30);
    EXPECT_EQ(U(GetFocus)(), a);
    fc_destroy(a);
}

TEST(fca_focus_owned_popup) {
    FC_NEED();
    HWND o = fc_top(); REQUIRE(o);
    HWND p = fc_owned(o); REQUIRE(p);
    fc_quiesce();
    U(SetFocus)(p);
    if (!focus_took(p)) { fc_destroy(o); FC_SKIP("owned popup focus not taken"); }
    /* An owned popup is not WS_CHILD, so the activation target is the popup itself. */
    EXPECT_EQ(U(GetActiveWindow)(), p);
    EXPECT_CALL_EQ(U(SetFocus)(o), o);      /* o is another top-level window: it is activated first and reported itself */
    EXPECT_EQ(U(GetFocus)(), o);
    fc_destroy(o);
}

/* ---- per-thread queues ------------------------------------------------------------------- */

static DWORD_PTR thr_setfocus(void *a) { HWND *w = (HWND *)a; HWND r = U(SetFocus)(w[0]); w[1] = r; w[2] = U(GetFocus)(); return GetLastError(); }
static DWORD_PTR thr_getfocus(void *a) { *(HWND *)a = U(GetFocus)(); return 0; }

TEST(fca_focus_cross_thread_unattached_denied) {
    FC_NEED();
    struct fc_thr t; if (!fc_thr_start(&t)) { FC_SKIP("worker thread failed"); return; }
    HWND mine = fc_top(); REQUIRE(mine);
    fc_quiesce();
    U(SetFocus)(mine);
    HWND before = U(GetFocus)();
    SET_ERR(0);
    HWND r = U(SetFocus)(t.w[0]);
    DWORD e = GetLastError();
    CHECK(r == NULL);
    CHECKF(e == ERROR_ACCESS_DENIED, "err %lu", e);   /* the focus change: target in another queue -> error 5 */
    EXPECT_EQ(U(GetFocus)(), before);
    UN(NtUserSetFocus);
    if (UN(NtUserSetFocus)) {
        SET_ERR(0);
        CHECK(UN(NtUserSetFocus)(t.w[1]) == NULL);
        EXPECT_ERR(ERROR_ACCESS_DENIED);
    }
    fc_destroy(mine);
    fc_thr_stop(&t);
}

TEST(fca_focus_is_per_queue) {
    FC_NEED();
    struct fc_thr t; if (!fc_thr_start(&t)) { FC_SKIP("worker thread failed"); return; }
    HWND mine = fc_top(); REQUIRE(mine);
    fc_quiesce();
    HWND args[3] = { t.w[0], 0, 0 };
    DWORD_PTR e = 0;
    CHECK(fc_thr_call(&t, thr_setfocus, args, &e));
    if (args[2] != t.w[0]) { t_note("worker SetFocus did not take focus (err %lu)", (unsigned long)e); fc_destroy(mine); fc_thr_stop(&t); FC_SKIP("no focus"); }
    EXPECT_EQ(args[1], t.w[0]);         /* the worker's window is activated first and the call reports it (as in fca_focus_set_get_roundtrip) */
    EXPECT_NE(U(GetFocus)(), t.w[0]);   /* the worker's focus does not show in this thread's queue */
    U(SetFocus)(mine);
    HWND wf = NULL;
    CHECK(fc_thr_call(&t, thr_getfocus, &wf, NULL));
    EXPECT_EQ(wf, t.w[0]);              /* and this thread's SetFocus does not move the worker's */
    GUITHREADINFO g;
    CHECK(fc_gti(t.tid, &g));
    EXPECT_EQ(g.hwndFocus, t.w[0]);
    fc_destroy(mine);
    fc_thr_stop(&t);
}

TEST(fca_focus_attached_threads_share_focus) {
    FC_NEED();
    struct fc_thr t; if (!fc_thr_start(&t)) { FC_SKIP("worker thread failed"); return; }
    HWND mine = fc_top(); REQUIRE(mine);
    fc_quiesce();
    DWORD me = GetCurrentThreadId();
    if (!U(AttachThreadInput)(me, t.tid, TRUE)) {
        t_note("attach failed err=%lu", GetLastError()); fc_destroy(mine); fc_thr_stop(&t); FC_SKIP("attach failed"); }
    SET_ERR(0);
    HWND r = U(SetFocus)(t.w[0]);
    DWORD e = GetLastError();
    HWND mine_sees = U(GetFocus)(), wf = NULL;
    fc_thr_call(&t, thr_getfocus, &wf, NULL);
    if (mine_sees == t.w[0]) {
        EXPECT_EQ(wf, t.w[0]);   /* one shared queue: both threads read the same queue focus window */
        int s = fc_find(WM_SETFOCUS, t.w[0], 0);
        CHECK(s >= 0);
        if (s >= 0) EXPECT_EQ(fc_log[s].tid, t.tid);  /* delivered on the window's own thread */
    } else {
        t_note("cross-thread SetFocus while attached: ret=%p err=%lu", r, e);
        CHECK(0);
    }
    fc_detach_all(me, t.tid);
    fc_destroy(mine);
    fc_thr_stop(&t);
}

TEST(fca_focus_worker_exit_leaves_valid_focus) {
    FC_NEED();
    struct fc_thr t; if (!fc_thr_start(&t)) { FC_SKIP("worker thread failed"); return; }
    HWND mine = fc_top(); REQUIRE(mine);
    fc_quiesce();
    DWORD me = GetCurrentThreadId();
    if (!U(AttachThreadInput)(me, t.tid, TRUE)) { fc_destroy(mine); fc_thr_stop(&t); FC_SKIP("attach failed"); }
    HWND ww = t.w[0];
    U(SetFocus)(ww);
    fc_thr_stop(&t);   /* worker (and its windows) go away while attached */
    HWND f = U(GetFocus)();
    CHECKF(f == NULL || U(IsWindow)(f), "stale focus handle %p", f);
    CHECK(f != ww);    /* the window no longer exists, so it cannot still be the focus */
    fc_destroy(mine);
}
