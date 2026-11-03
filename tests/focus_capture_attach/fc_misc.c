/* Extra per-queue scenarios: system windows (desktop thread's queue), message-only windows, long
 * ping-pong sequences, activation messages across attached threads, BlockInput observation. */
#include "fc.h"

TEST(fca_misc_desktop_window_is_foreign_queue) {
    FC_NEED(); REQUIRE(U(GetDesktopWindow));
    HWND d = U(GetDesktopWindow)();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    DWORD pid = 0;
    DWORD tid = U(GetWindowThreadProcessId)(d, &pid);
    CHECK(tid != GetCurrentThreadId());           /* owned by the desktop/system thread, never by a test thread */
    SET_ERR(0);
    CHECK(U(SetFocus)(d) == NULL);
    EXPECT_ERR(ERROR_ACCESS_DENIED);              /* SetFocus: not this queue */
    CHECK(U(SetActiveWindow)(d) == NULL);
    CHECK(U(SetCapture)(d) == NULL);
    EXPECT_EQ(U(GetCapture)(), NULL);
    CHECK(!U(CreateCaret)(d, NULL, 2, 2));
    EXPECT_EQ(U(GetFocus)(), NULL);
    fc_destroy(a);
}

TEST(fca_misc_message_only_window_observed) {
    FC_NEED();
    HWND m = fc_make(WS_POPUP, 0, HWND_MESSAGE, 0);
    if (!m) { t_note("message-only window creation failed err=%lu", GetLastError()); return; }
    fc_quiesce();
    SET_ERR(0);
    HWND r = U(SetFocus)(m);
    t_note("SetFocus(message-only): ret=%p err=%lu focus==m:%d", r, GetLastError(), U(GetFocus)() == m);
    r = U(SetActiveWindow)(m);
    t_note("SetActiveWindow(message-only): ret=%p active==m:%d", r, U(GetActiveWindow)() == m);
    r = U(SetCapture)(m);
    t_note("SetCapture(message-only): ret=%p capture==m:%d", r, U(GetCapture)() == m);
    U(ReleaseCapture)();
    /* NtUserCallHwndParamLockSafe(0x7d) skips fnid 0x29d/0x29f windows; whether a custom-class message-only
     * window carries such an fnid is not proven, so only observe it. */
    BOOL er = U(EnableWindow)(m, FALSE);
    t_note("EnableWindow(message-only): ret=%d enabled-after=%d", er, U(IsWindowEnabled)(m));
    U(EnableWindow)(m, TRUE);
    fc_destroy(m);
}

TEST(fca_misc_focus_ping_pong) {
    FC_NEED();
    /* two children of one top-level window: SetFocus between top-level windows activates the target first and
     * returns the target itself on Windows 10 (see fca_focus_returns_previous_window), so the "previous focus
     * window" result is only observable inside one active window */
    HWND p = fc_top(); REQUIRE(p);
    HWND a = fc_child(p), b = fc_child(p); REQUIRE(a && b);
    fc_quiesce();
    U(SetFocus)(a);
    if (U(GetFocus)() != a) { fc_destroy(p); FC_SKIP("focus not taken"); }
    fc_reset();
    HWND cur = a, other = b;
    for (int i = 0; i < 40; i++) {
        HWND r = U(SetFocus)(other);
        CHECKF(r == cur, "step %d: returned %p want %p", i, r, cur);
        CHECKF(U(GetFocus)() == other, "step %d: focus %p want %p", i, U(GetFocus)(), other);
        HWND t = cur; cur = other; other = t;
    }
    EXPECT_EQ(fc_count(WM_SETFOCUS, a), 20);
    EXPECT_EQ(fc_count(WM_SETFOCUS, b), 20);
    EXPECT_EQ(fc_count(WM_KILLFOCUS, a), 20);
    EXPECT_EQ(fc_count(WM_KILLFOCUS, b), 20);
    fc_destroy(p);
}

TEST(fca_misc_focus_cycle_children) {
    FC_NEED();
    HWND p = fc_top(); REQUIRE(p);
    HWND c[5];
    for (int i = 0; i < 5; i++) { c[i] = fc_child(p); REQUIRE(c[i]); }
    fc_quiesce();
    U(SetFocus)(c[0]);
    if (U(GetFocus)() != c[0]) { fc_destroy(p); FC_SKIP("child focus not taken"); }
    fc_reset();
    for (int i = 1; i < 5; i++) {
        HWND r = U(SetFocus)(c[i]);
        CHECKF(r == c[i - 1], "child %d: previous %p want %p", i, r, c[i - 1]);
    }
    EXPECT_EQ(U(GetActiveWindow)(), p);
    for (int i = 0; i < 4; i++) { CHECK(fc_count(WM_KILLFOCUS, c[i]) == 1); }
    for (int i = 1; i < 5; i++) { CHECK(fc_count(WM_SETFOCUS, c[i]) == 1); }
    fc_destroy(p);
}

TEST(fca_misc_capture_ping_pong) {
    FC_NEED();
    HWND a = fc_top(), b = fc_top(); REQUIRE(a && b);
    fc_quiesce();
    HWND cur = NULL, next = a, other = b;
    for (int i = 0; i < 30; i++) {
        HWND r = U(SetCapture)(next);
        CHECKF(r == cur, "step %d: returned %p want %p", i, r, cur);
        CHECKF(U(GetCapture)() == next, "step %d: capture %p", i, U(GetCapture)());
        cur = next; next = other; other = cur;
    }
    EXPECT_EQ(fc_count(WM_CAPTURECHANGED, NULL), 29);   /* one per switch; the first capture had no predecessor */
    EXPECT_EQ(U(ReleaseCapture)(), TRUE);
    EXPECT_EQ(fc_count(WM_CAPTURECHANGED, NULL), 30);
    fc_destroy(a); fc_destroy(b);
}

TEST(fca_misc_enable_toggle_loop) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    for (int i = 0; i < 25; i++) {
        BOOL r1 = U(EnableWindow)(a, FALSE), r2 = U(EnableWindow)(a, TRUE);
        CHECKF(r1 == FALSE && r2 == TRUE, "iteration %d: %d %d", i, r1, r2);
    }
    EXPECT_EQ(fc_count(WM_ENABLE, a), 50);
    EXPECT_EQ(fc_count(WM_CANCELMODE, a), 25);
    CHECK(U(IsWindowEnabled)(a));
    fc_destroy(a);
}

TEST(fca_misc_caret_create_destroy_loop) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    U(DestroyCaret)();
    for (int i = 1; i <= 25; i++) {
        CHECKF(U(CreateCaret)(a, NULL, i, i + 1), "create %d", i);
        GUITHREADINFO g;
        CHECK(fc_gti(GetCurrentThreadId(), &g));
        CHECK(g.rcCaret.right - g.rcCaret.left == i && g.rcCaret.bottom - g.rcCaret.top == i + 1);
        CHECKF(U(DestroyCaret)(), "destroy %d", i);
    }
    fc_destroy(a);
}

TEST(fca_misc_attach_refcount_loop) {
    FC_NEED();
    struct fc_thr t; if (!fc_thr_start(&t)) FC_SKIP("worker thread failed");
    HWND w = fc_top(); REQUIRE(w);
    DWORD me = GetCurrentThreadId();
    if (!U(AttachThreadInput)(me, t.tid, TRUE)) { fc_destroy(w); fc_thr_stop(&t); FC_SKIP("attach refused"); }
    for (int i = 0; i < 9; i++) CHECK(U(AttachThreadInput)(me, t.tid, TRUE));
    for (int i = 0; i < 10; i++) CHECKF(U(AttachThreadInput)(t.tid, me, FALSE), "detach %d", i);
    SET_ERR(0);
    CHECK(!U(AttachThreadInput)(me, t.tid, FALSE));
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    fc_destroy(w);
    fc_thr_stop(&t);
}

TEST(fca_misc_attached_activation_across_threads) {
    FC_NEED();
    struct fc_thr t; if (!fc_thr_start(&t)) FC_SKIP("worker thread failed");
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    U(SetActiveWindow)(a);
    if (U(GetActiveWindow)() != a) { fc_destroy(a); fc_thr_stop(&t); FC_SKIP("activation not taken"); }
    DWORD me = GetCurrentThreadId();
    if (!U(AttachThreadInput)(me, t.tid, TRUE)) { fc_destroy(a); fc_thr_stop(&t); FC_SKIP("attach refused"); }
    fc_reset();
    HWND r = U(SetActiveWindow)(t.w[0]);
    if (U(GetActiveWindow)() != t.w[0]) {
        t_note("attached cross-thread SetActiveWindow: ret=%p err=%lu", r, GetLastError());
        CHECK(0);
    } else {
        EXPECT_EQ(r, a);
        int ia = fc_find_lo(WM_ACTIVATE, a, WA_INACTIVE, 0), ib = fc_find(WM_ACTIVATE, t.w[0], 0);
        CHECK(ia >= 0 && ib >= 0);
        if (ia >= 0 && ib >= 0) {
            CHECK(ia < ib);
            EXPECT_EQ(fc_log[ia].tid, me);        /* each window is notified on its own thread */
            EXPECT_EQ(fc_log[ib].tid, t.tid);
            EXPECT_EQ(fc_log[ib].lp, (LPARAM)a);
        }
    }
    fc_detach_all(me, t.tid);
    fc_destroy(a);
    fc_thr_stop(&t);
}

TEST(fca_misc_blockinput_false_observed) {
    FC_NEED(); REQUIRE(U(BlockInput));
    SET_ERR(0);
    BOOL r = U(BlockInput)(FALSE);   /* only the unblock direction: never blocks the interactive session */
    t_note("BlockInput(FALSE) when not blocked: ret=%d err=%lu", r, GetLastError());
}

TEST(fca_misc_getgui_after_window_destroyed_thread_state) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    U(SetFocus)(a); U(SetCapture)(a); U(CreateCaret)(a, NULL, 2, 2);
    fc_destroy(a);
    GUITHREADINFO g;
    CHECK(fc_gti(GetCurrentThreadId(), &g));
    EXPECT_EQ(g.hwndFocus, NULL);          /* nothing may refer to the destroyed window */
    EXPECT_EQ(g.hwndCapture, NULL);
    EXPECT_EQ(g.hwndCaret, NULL);
    CHECK(g.hwndActive != a);
    EXPECT_EQ(g.flags & GUI_CARETBLINKING, 0);
}
