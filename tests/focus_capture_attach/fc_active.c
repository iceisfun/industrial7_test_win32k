/* NtUserSetActiveWindow / GetActiveWindow (NtUserGetThreadState(1)): the kernel activation path (WM_NCACTIVATE / WM_ACTIVATE sequence). */
#include "fc.h"

static int active_took(HWND h) { return U(GetActiveWindow)() == h; }

TEST(fca_active_getactive_matches_threadstate) {
    FC_NEED(); if (t_is_wine()) { FC_SKIP("syscall info classes are win32k-specific"); return; }
    REQUIRE(UN(NtUserGetThreadState));
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    EXPECT_CALL_EQ(UN(NtUserGetThreadState)(FC_TS_ACTIVE), (ULONG_PTR)U(GetActiveWindow)());
    U(SetActiveWindow)(a);
    EXPECT_CALL_EQ(UN(NtUserGetThreadState)(FC_TS_ACTIVE), (ULONG_PTR)U(GetActiveWindow)());
    fc_destroy(a);
}

TEST(fca_active_invalid_handle) {
    FC_NEED(); REQUIRE(UN(NtUserSetActiveWindow));
    HWND a = fc_top(); REQUIRE(a);
    HWND stale = fc_top(); REQUIRE(stale); fc_destroy(stale);
    HWND bad[] = { (HWND)(ULONG_PTR)1, (HWND)(ULONG_PTR)0x12345678, (HWND)(ULONG_PTR)0xDEADBEEF, stale,
                   (HWND)((ULONG_PTR)a ^ 0x00030000) };
    fc_quiesce();
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SET_ERR(0);
        HWND r = U(SetActiveWindow)(bad[i]);
        CHECKF(r == NULL, "SetActiveWindow(%p) -> %p", bad[i], r);
        EXPECT_ERR(1400);
        SET_ERR(0);
        r = UN(NtUserSetActiveWindow)(bad[i]);
        CHECKF(r == NULL, "NtUserSetActiveWindow(%p) -> %p", bad[i], r);
        EXPECT_ERR(1400);
    }
    fc_destroy(a);
}

TEST(fca_active_null_is_noop) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    U(SetActiveWindow)(a);
    if (!active_took(a)) { fc_destroy(a); FC_SKIP("activation not taken"); }
    HWND r = U(SetActiveWindow)(NULL);     /* SetActiveWindow(NULL) returns NULL without touching the queue */
    EXPECT_EQ(r, NULL);
    EXPECT_EQ(U(GetActiveWindow)(), a);
    fc_destroy(a);
}

TEST(fca_active_set_returns_previous) {
    FC_NEED();
    HWND a = fc_top(), b = fc_top(); REQUIRE(a && b);
    fc_quiesce();
    U(SetActiveWindow)(a);
    if (!active_took(a)) { fc_destroy(a); fc_destroy(b); FC_SKIP("activation not taken"); }
    HWND r = U(SetActiveWindow)(b);
    EXPECT_EQ(r, a);
    EXPECT_EQ(U(GetActiveWindow)(), b);
    r = UN(NtUserSetActiveWindow) ? UN(NtUserSetActiveWindow)(a) : a;
    EXPECT_EQ(r, b);
    EXPECT_EQ(U(GetActiveWindow)(), a);
    fc_destroy(a); fc_destroy(b);
}

TEST(fca_active_same_window_returns_itself) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    U(SetActiveWindow)(a);
    if (!active_took(a)) { fc_destroy(a); FC_SKIP("activation not taken"); }
    fc_reset();
    HWND r = U(SetActiveWindow)(a);
    EXPECT_EQ(r, a);
    EXPECT_EQ(U(GetActiveWindow)(), a);
    EXPECT_EQ(fc_count(WM_ACTIVATE, NULL), 0);   /* activation: already the queue's active window -> no messages */
    fc_destroy(a);
}

TEST(fca_active_message_sequence) {
    FC_NEED();
    HWND a = fc_top(), b = fc_top(); REQUIRE(a && b);
    fc_quiesce();
    U(SetActiveWindow)(a);
    if (!active_took(a)) { fc_destroy(a); fc_destroy(b); FC_SKIP("activation not taken"); }
    fc_reset();
    U(SetActiveWindow)(b);
    int ia = fc_find_lo(WM_ACTIVATE, a, WA_INACTIVE, 0);
    int ib = fc_find(WM_ACTIVATE, b, 0);
    CHECK(ia >= 0); CHECK(ib >= 0);
    if (ia >= 0 && ib >= 0) {
        CHECK(ia < ib);                                   /* old window is deactivated first */
        EXPECT_EQ(fc_log[ia].lp, (LPARAM)b);              /* WM_ACTIVATE(inactive).lParam = window being activated */
        CHECK(LOWORD(fc_log[ib].wp) == WA_ACTIVE || LOWORD(fc_log[ib].wp) == WA_CLICKACTIVE);
        EXPECT_EQ(fc_log[ib].lp, (LPARAM)a);              /* WM_ACTIVATE(active).lParam = window being deactivated */
    }
    int na = fc_find(WM_NCACTIVATE, a, 0), nb = fc_find(WM_NCACTIVATE, b, 0);
    CHECK(na >= 0); CHECK(nb >= 0);
    if (na >= 0 && ia >= 0) CHECK(na < ia);               /* WM_NCACTIVATE precedes WM_ACTIVATE */
    if (nb >= 0 && ib >= 0) CHECK(nb < ib);
    if (na >= 0 && nb >= 0) CHECK(na < nb);
    if (na >= 0) EXPECT_EQ(fc_log[na].wp & 1, 0);          /* old window gets an inactive-caption notification */
    fc_destroy(a); fc_destroy(b);
}

TEST(fca_active_default_activate_moves_focus) {
    FC_NEED();
    HWND a = fc_top(), b = fc_top(); REQUIRE(a && b);
    fc_quiesce();
    U(SetFocus)(a);
    if (!active_took(a) || U(GetFocus)() != a) { fc_destroy(a); fc_destroy(b); FC_SKIP("setup did not take"); }
    fc_reset();
    U(SetActiveWindow)(b);
    if (!active_took(b)) { fc_destroy(a); fc_destroy(b); FC_SKIP("activation not taken"); }
    /* DefWindowProc(WM_ACTIVATE, active) gives the activated window the focus. */
    EXPECT_EQ(U(GetFocus)(), b);
    int ib = fc_find(WM_ACTIVATE, b, 0), k = fc_find(WM_KILLFOCUS, a, 0), s = fc_find(WM_SETFOCUS, b, 0);
    CHECK(k >= 0); CHECK(s >= 0);
    if (ib >= 0 && k >= 0) CHECK(ib < k);
    if (k >= 0 && s >= 0) CHECK(k < s);
    fc_destroy(a); fc_destroy(b);
}

TEST(fca_active_queue_state_during_activate) {
    FC_NEED();
    HWND a = fc_top(), b = fc_top(); REQUIRE(a && b);
    fc_quiesce();
    U(SetActiveWindow)(a);
    if (!active_took(a)) { fc_destroy(a); fc_destroy(b); FC_SKIP("activation not taken"); }
    fc_reset();
    U(SetActiveWindow)(b);
    int ia = fc_find_lo(WM_ACTIVATE, a, WA_INACTIVE, 0);
    int ib = fc_find(WM_ACTIVATE, b, 0);
    /* The queue's spwndActive is replaced before the new window's
     * WM_ACTIVATE is sent, while the old window's deactivation is sent before it changes. */
    if (ia >= 0) EXPECT_EQ(fc_log[ia].active_now, a);
    if (ib >= 0) EXPECT_EQ(fc_log[ib].active_now, b);
    fc_destroy(a); fc_destroy(b);
}

TEST(fca_active_child_is_not_activated) {
    FC_NEED();
    HWND p = fc_top(), q = fc_top(); REQUIRE(p && q);
    HWND c = fc_child(p); REQUIRE(c);
    fc_quiesce();
    U(SetActiveWindow)(q);
    if (!active_took(q)) { fc_destroy(p); fc_destroy(q); FC_SKIP("activation not taken"); }
    fc_reset();
    HWND r = U(SetActiveWindow)(c);   /* activation bails out for WS_CHILD windows */
    EXPECT_EQ(r, q);                  /* previous active window is still returned */
    EXPECT_EQ(U(GetActiveWindow)(), q);
    EXPECT_EQ(fc_count(WM_ACTIVATE, NULL), 0);
    fc_destroy(p); fc_destroy(q);
}

TEST(fca_active_other_thread_window_refused) {
    FC_NEED();
    struct fc_thr t; if (!fc_thr_start(&t)) { FC_SKIP("worker thread failed"); return; }
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    U(SetActiveWindow)(a);
    HWND before = U(GetActiveWindow)();
    HWND r = U(SetActiveWindow)(t.w[0]);   /* target belongs to a different queue -> NULL, no change */
    EXPECT_EQ(r, NULL);
    EXPECT_EQ(U(GetActiveWindow)(), before);
    if (UN(NtUserSetActiveWindow)) {
        r = UN(NtUserSetActiveWindow)(t.w[1]);
        EXPECT_EQ(r, NULL);
        EXPECT_EQ(U(GetActiveWindow)(), before);
    }
    fc_destroy(a);
    fc_thr_stop(&t);
}

static DWORD_PTR thr_activate(void *p) {
    HWND *a = (HWND *)p;
    a[1] = U(SetActiveWindow)(a[0]);
    a[2] = U(GetActiveWindow)();
    return 0;
}
static DWORD_PTR thr_getactive(void *p) { *(HWND *)p = U(GetActiveWindow)(); return 0; }

TEST(fca_active_is_per_queue) {
    FC_NEED();
    struct fc_thr t; if (!fc_thr_start(&t)) { FC_SKIP("worker thread failed"); return; }
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    HWND args[3] = { t.w[1], 0, 0 };
    CHECK(fc_thr_call(&t, thr_activate, args, NULL));
    if (args[2] != t.w[1]) { fc_destroy(a); fc_thr_stop(&t); FC_SKIP("worker activation not taken"); }
    U(SetActiveWindow)(a);
    HWND wa = NULL;
    CHECK(fc_thr_call(&t, thr_getactive, &wa, NULL));
    EXPECT_EQ(wa, t.w[1]);           /* this thread's activation does not change the worker queue's active window */
    EXPECT_NE(U(GetActiveWindow)(), t.w[1]);
    GUITHREADINFO g;
    CHECK(fc_gti(t.tid, &g));
    EXPECT_EQ(g.hwndActive, t.w[1]);
    fc_destroy(a);
    fc_thr_stop(&t);
}

TEST(fca_active_attached_threads_share_active) {
    FC_NEED();
    struct fc_thr t; if (!fc_thr_start(&t)) { FC_SKIP("worker thread failed"); return; }
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    DWORD me = GetCurrentThreadId();
    if (!U(AttachThreadInput)(me, t.tid, TRUE)) { fc_destroy(a); fc_thr_stop(&t); FC_SKIP("attach failed"); }
    HWND r = U(SetActiveWindow)(t.w[0]);
    HWND mine = U(GetActiveWindow)(), wa = NULL;
    fc_thr_call(&t, thr_getactive, &wa, NULL);
    if (mine == t.w[0]) {
        EXPECT_EQ(wa, t.w[0]);
    } else {
        t_note("attached cross-thread SetActiveWindow: ret=%p mine=%p err=%lu", r, mine, GetLastError());
        CHECK(mine == wa);   /* attached threads read one queue */
    }
    fc_detach_all(me, t.tid);
    fc_destroy(a);
    fc_thr_stop(&t);
}

TEST(fca_active_destroy_clears_active) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    U(SetActiveWindow)(a);
    if (!active_took(a)) { fc_destroy(a); FC_SKIP("activation not taken"); }
    U(DestroyWindow)(a);
    HWND now = U(GetActiveWindow)();
    CHECK(now != a);
    CHECK(now == NULL || U(IsWindow)(now));
    t_note("active window after destroying the active window: %p", now);
}

TEST(fca_active_destroy_inactive_leaves_active) {
    FC_NEED();
    HWND a = fc_top(), b = fc_top(); REQUIRE(a && b);
    fc_quiesce();
    U(SetActiveWindow)(a);
    if (!active_took(a)) { fc_destroy(a); fc_destroy(b); FC_SKIP("activation not taken"); }
    fc_destroy(b);
    EXPECT_EQ(U(GetActiveWindow)(), a);
    fc_destroy(a);
}

TEST(fca_active_destroy_sends_deactivate_before_destroy) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    U(SetActiveWindow)(a);
    if (!active_took(a)) { fc_destroy(a); FC_SKIP("activation not taken"); }
    fc_reset();
    U(DestroyWindow)(a);
    int d = fc_find(WM_DESTROY, a, 0);
    int ia = fc_find_lo(WM_ACTIVATE, a, WA_INACTIVE, 0);
    CHECK(d >= 0);
    if (ia >= 0 && d >= 0) CHECK(ia < d);
    else t_note("no WM_ACTIVATE(inactive) before WM_DESTROY for the destroyed active window");
}

TEST(fca_active_activateapp_before_ncactivate) {
    FC_NEED();
    struct fc_thr t; if (!fc_thr_start(&t)) { FC_SKIP("worker thread failed"); return; }
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    fc_reset();
    U(SetActiveWindow)(a);
    int aa = fc_find(WM_ACTIVATEAPP, a, 0), na = fc_find(WM_NCACTIVATE, a, 0);
    /* activation sends WM_ACTIVATEAPP to the newly activated window's thread before its
     * WM_NCACTIVATE / WM_ACTIVATE, but only when the thread is not already flagged as the active app. */
    if (aa >= 0) {
        CHECK(na < 0 || aa < na);
        EXPECT_EQ(fc_log[aa].wp, 1);
    } else t_note("no WM_ACTIVATEAPP for first activation in this thread");
    fc_destroy(a);
    fc_thr_stop(&t);
}

TEST(fca_active_disabled_window_observed) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    U(EnableWindow)(a, FALSE);
    SET_ERR(0);
    HWND r = U(SetActiveWindow)(a);
    t_note("SetActiveWindow(disabled): ret=%p err=%lu active==w:%d", r, GetLastError(), active_took(a));
    U(EnableWindow)(a, TRUE);
    fc_destroy(a);
}

TEST(fca_active_hidden_window_observed) {
    FC_NEED();
    HWND a = fc_hidden(); REQUIRE(a);
    fc_quiesce();
    HWND r = U(SetActiveWindow)(a);
    t_note("SetActiveWindow(hidden): ret=%p active==w:%d visible:%d", r, active_took(a), U(IsWindowVisible)(a));
    fc_destroy(a);
}

TEST(fca_active_noactivate_exstyle_observed) {
    FC_NEED();
    HWND a = fc_make(WS_POPUP, WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, NULL, 1); REQUIRE(a);
    fc_quiesce();
    HWND r = U(SetActiveWindow)(a);
    t_note("SetActiveWindow(WS_EX_NOACTIVATE): ret=%p active==w:%d", r, active_took(a));
    r = U(SetFocus)(a);
    t_note("SetFocus(WS_EX_NOACTIVATE): ret=%p focus==w:%d", r, U(GetFocus)() == a);
    fc_destroy(a);
}

TEST(fca_active_owner_chain) {
    FC_NEED();
    HWND o = fc_top(); REQUIRE(o);
    HWND p = fc_owned(o); REQUIRE(p);
    fc_quiesce();
    U(SetActiveWindow)(o);
    if (!active_took(o)) { fc_destroy(o); FC_SKIP("activation not taken"); }
    HWND r = U(SetActiveWindow)(p);
    EXPECT_EQ(r, o);
    EXPECT_EQ(U(GetActiveWindow)(), p);          /* owned popups are activatable on their own */
    EXPECT_EQ(U(GetWindow)(p, GW_OWNER), o);
    fc_destroy(p);                                /* destroying the active owned window */
    HWND now = U(GetActiveWindow)();
    CHECK(now != p);
    t_note("active after destroying active owned popup: owner=%d now=%p owner=%p", now == o, now, o);
    fc_destroy(o);
}

TEST(fca_active_destroy_owner_destroys_owned_active) {
    FC_NEED();
    HWND o = fc_top(); REQUIRE(o);
    HWND p = fc_owned(o); REQUIRE(p);
    fc_quiesce();
    U(SetActiveWindow)(p);
    if (!active_took(p)) { fc_destroy(o); FC_SKIP("activation not taken"); }
    U(DestroyWindow)(o);
    CHECK(!U(IsWindow)(p));   /* owned windows are destroyed with their owner */
    HWND now = U(GetActiveWindow)();
    CHECK(now != p && now != o);
}

TEST(fca_active_matches_guithreadinfo) {
    FC_NEED();
    HWND a = fc_top(), b = fc_top(); REQUIRE(a && b);
    fc_quiesce();
    GUITHREADINFO g;
    HWND seq[] = { a, b, NULL };
    for (int i = 0; i < 3; i++) {
        if (seq[i]) U(SetActiveWindow)(seq[i]);
        CHECK(fc_gti(GetCurrentThreadId(), &g));
        EXPECT_EQ(g.hwndActive, U(GetActiveWindow)());
        EXPECT_EQ(g.hwndFocus, U(GetFocus)());
    }
    fc_destroy(a); fc_destroy(b);
}

TEST(fca_active_foreground_relation) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    U(SetActiveWindow)(a);
    HWND fg = U(GetForegroundWindow)();
    DWORD pid = 0; if (fg) U(GetWindowThreadProcessId)(fg, &pid);
    if (fg == a) {
        t_note("process became foreground through SetActiveWindow");
        EXPECT_EQ(U(GetActiveWindow)(), a);   /* the foreground window is the foreground queue's active window */
    } else {
        t_info("not foreground (fg=%p pid=%lu): active-in-queue=%d", fg, pid, active_took(a));
    }
    fc_destroy(a);
}
