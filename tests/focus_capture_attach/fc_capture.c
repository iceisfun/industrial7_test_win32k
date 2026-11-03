/* WM_CAPTURECHANGED is only sent to windows flagged Win4.0-compatible (state bit tested in the capture path) and not
 * yet deleted (fnid bit 15 clear); a window created by this exe (subsystem version >= 4.0) qualifies.
 * NtUserSetCapture / GetCapture (NtUserGetThreadState(2)) / ReleaseCapture (NtUserCallNoParam 0x16):
 * SetCapture, and the WM_CAPTURECHANGED notification. */
#include "fc.h"

static int cap_took(HWND h) { return U(GetCapture)() == h; }

TEST(fca_capture_none_initially) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    EXPECT_EQ(U(GetCapture)(), NULL);
    GUITHREADINFO g;
    CHECK(fc_gti(GetCurrentThreadId(), &g));
    EXPECT_EQ(g.hwndCapture, NULL);
    fc_destroy(a);
}

TEST(fca_capture_set_get_release) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    HWND r = U(SetCapture)(a);
    EXPECT_EQ(r, NULL);                   /* no previous capture window */
    EXPECT_EQ(U(GetCapture)(), a);
    BOOL ok = U(ReleaseCapture)();
    CHECK(ok);
    EXPECT_EQ(U(GetCapture)(), NULL);
    fc_destroy(a);
}

TEST(fca_capture_invalid_handle) {
    FC_NEED(); REQUIRE(UN(NtUserSetCapture));
    HWND a = fc_top(), s = fc_top(); REQUIRE(a && s);
    fc_destroy(s);
    HWND bad[] = { (HWND)(ULONG_PTR)1, (HWND)(ULONG_PTR)0x12345678, (HWND)(ULONG_PTR)0xDEADBEEF, s,
                   (HWND)((ULONG_PTR)a ^ 0x00030000) };
    fc_quiesce();
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SET_ERR(0);
        HWND r = U(SetCapture)(bad[i]);
        CHECKF(r == NULL, "SetCapture(%p) -> %p", bad[i], r);
        EXPECT_ERR(1400);
        SET_ERR(0);
        r = UN(NtUserSetCapture)(bad[i]);
        CHECKF(r == NULL, "NtUserSetCapture(%p) -> %p", bad[i], r);
        EXPECT_ERR(1400);
        EXPECT_EQ(U(GetCapture)(), NULL);
    }
    fc_destroy(a);
}

TEST(fca_capture_invalid_keeps_existing_capture) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    U(SetCapture)(a);
    if (!cap_took(a)) { fc_destroy(a); FC_SKIP("capture not taken"); }
    SET_ERR(0);
    CHECK(U(SetCapture)((HWND)(ULONG_PTR)0x12345678) == NULL);
    EXPECT_ERR(1400);
    EXPECT_EQ(U(GetCapture)(), a);       /* validation fails before SetCapture */
    U(ReleaseCapture)();
    fc_destroy(a);
}

TEST(fca_capture_returns_previous) {
    FC_NEED();
    HWND a = fc_top(), b = fc_top(); REQUIRE(a && b);
    fc_quiesce();
    U(SetCapture)(a);
    if (!cap_took(a)) { fc_destroy(a); fc_destroy(b); FC_SKIP("capture not taken"); }
    HWND r = U(SetCapture)(b);
    EXPECT_EQ(r, a);
    EXPECT_EQ(U(GetCapture)(), b);
    r = U(SetCapture)(a);
    EXPECT_EQ(r, b);
    U(ReleaseCapture)();
    fc_destroy(a); fc_destroy(b);
}

TEST(fca_capture_direct_stub_returns_previous) {
    FC_NEED(); REQUIRE(UN(NtUserSetCapture));
    HWND a = fc_top(), b = fc_top(); REQUIRE(a && b);
    fc_quiesce();
    EXPECT_CALL_EQ(UN(NtUserSetCapture)(a), 0);
    if (!cap_took(a)) { fc_destroy(a); fc_destroy(b); FC_SKIP("capture not taken"); }
    EXPECT_CALL_EQ(UN(NtUserSetCapture)(b), a);
    EXPECT_EQ(U(GetCapture)(), b);
    EXPECT_CALL_EQ(UN(NtUserSetCapture)(NULL), b);   /* SetCapture(NULL) releases and returns the old capture */
    EXPECT_EQ(U(GetCapture)(), NULL);
    fc_destroy(a); fc_destroy(b);
}

TEST(fca_capture_null_releases) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    U(SetCapture)(a);
    if (!cap_took(a)) { fc_destroy(a); FC_SKIP("capture not taken"); }
    fc_reset();
    HWND r = U(SetCapture)(NULL);
    EXPECT_EQ(r, a);
    EXPECT_EQ(U(GetCapture)(), NULL);
    int c = fc_find(WM_CAPTURECHANGED, a, 0);
    CHECK(c >= 0);
    if (c >= 0) EXPECT_EQ(fc_log[c].lp, 0);
    fc_destroy(a);
}

TEST(fca_capture_release_without_capture) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    fc_reset();
    BOOL ok = U(ReleaseCapture)();
    CHECK(ok);                                          /* NtUserCallNoParam(0x16) returns 1 even when nothing is captured */
    EXPECT_EQ(fc_count(WM_CAPTURECHANGED, NULL), 0);
    EXPECT_EQ(U(GetCapture)(), NULL);
    fc_destroy(a);
}

TEST(fca_capture_release_direct_call) {
    FC_NEED(); if (t_is_wine()) { FC_SKIP("win32k call codes"); return; }
    REQUIRE(UN(NtUserCallNoParam));
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    U(SetCapture)(a);
    if (!cap_took(a)) { fc_destroy(a); FC_SKIP("capture not taken"); }
    EXPECT_CALL_EQ(UN(NtUserCallNoParam)(FC_CALL_RELEASECAPTURE), 1);
    EXPECT_EQ(U(GetCapture)(), NULL);
    fc_destroy(a);
}

TEST(fca_capture_changed_messages) {
    FC_NEED();
    HWND a = fc_top(), b = fc_top(); REQUIRE(a && b);
    fc_quiesce();
    U(SetCapture)(a);
    if (!cap_took(a)) { fc_destroy(a); fc_destroy(b); FC_SKIP("capture not taken"); }
    fc_reset();
    U(SetCapture)(b);
    int c = fc_find(WM_CAPTURECHANGED, a, 0);
    CHECK(c >= 0);
    if (c >= 0) {
        EXPECT_EQ(fc_log[c].lp, (LPARAM)b);   /* lParam = window gaining the capture */
        EXPECT_EQ(fc_log[c].tid, GetCurrentThreadId());
    }
    EXPECT_EQ(fc_count(WM_CAPTURECHANGED, b), 0);
    fc_reset();
    U(ReleaseCapture)();
    c = fc_find(WM_CAPTURECHANGED, b, 0);
    CHECK(c >= 0);
    if (c >= 0) EXPECT_EQ(fc_log[c].lp, 0);
    fc_destroy(a); fc_destroy(b);
}

TEST(fca_capture_changed_state_snapshot) {
    FC_NEED();
    HWND a = fc_top(), b = fc_top(); REQUIRE(a && b);
    fc_quiesce();
    U(SetCapture)(a);
    if (!cap_took(a)) { fc_destroy(a); fc_destroy(b); FC_SKIP("capture not taken"); }
    fc_reset();
    U(SetCapture)(b);
    int c = fc_find(WM_CAPTURECHANGED, a, 0);
    /* the capture path has already stored the new capture window when it notifies the old one. */
    if (c >= 0) EXPECT_EQ(fc_log[c].capture_now, b);
    fc_reset();
    U(ReleaseCapture)();
    c = fc_find(WM_CAPTURECHANGED, b, 0);
    if (c >= 0) EXPECT_EQ(fc_log[c].capture_now, NULL);
    fc_destroy(a); fc_destroy(b);
}

TEST(fca_capture_other_thread_window_refused) {
    FC_NEED(); REQUIRE(UN(NtUserSetCapture));
    struct fc_thr t; if (!fc_thr_start(&t)) { FC_SKIP("worker thread failed"); return; }
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    HWND r = U(SetCapture)(t.w[0]);   /* SetCapture: window owned by another queue -> returns NULL without capturing */
    EXPECT_EQ(r, NULL);
    EXPECT_EQ(U(GetCapture)(), NULL);
    r = UN(NtUserSetCapture)(t.w[1]);
    EXPECT_EQ(r, NULL);
    EXPECT_EQ(U(GetCapture)(), NULL);
    U(SetCapture)(a);
    if (cap_took(a)) {
        r = U(SetCapture)(t.w[0]);
        EXPECT_EQ(r, NULL);
        EXPECT_EQ(U(GetCapture)(), a);   /* the failed request leaves the existing capture alone */
        U(ReleaseCapture)();
    }
    fc_destroy(a);
    fc_thr_stop(&t);
}

static DWORD_PTR thr_setcap(void *p) { HWND *a = (HWND *)p; a[1] = U(SetCapture)(a[0]); a[2] = U(GetCapture)(); return 0; }
static DWORD_PTR thr_getcap(void *p) { *(HWND *)p = U(GetCapture)(); return 0; }
static DWORD_PTR thr_relcap(void *p) { *(BOOL *)p = U(ReleaseCapture)(); return 0; }

TEST(fca_capture_is_per_queue) {
    FC_NEED();
    struct fc_thr t; if (!fc_thr_start(&t)) { FC_SKIP("worker thread failed"); return; }
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    HWND args[3] = { t.w[0], 0, 0 };
    CHECK(fc_thr_call(&t, thr_setcap, args, NULL));
    if (args[2] != t.w[0]) { fc_destroy(a); fc_thr_stop(&t); FC_SKIP("worker capture not taken"); }
    EXPECT_EQ(args[1], NULL);
    EXPECT_NE(U(GetCapture)(), t.w[0]);
    GUITHREADINFO g;
    CHECK(fc_gti(t.tid, &g));
    EXPECT_EQ(g.hwndCapture, t.w[0]);
    U(SetCapture)(a);
    HWND wc = NULL;
    fc_thr_call(&t, thr_getcap, &wc, NULL);
    EXPECT_EQ(wc, t.w[0]);
    BOOL rel = FALSE;
    fc_thr_call(&t, thr_relcap, &rel, NULL);
    CHECK(rel);
    if (cap_took(a)) {}
    U(ReleaseCapture)();
    fc_destroy(a);
    fc_thr_stop(&t);
}

TEST(fca_capture_attached_threads_share_capture) {
    FC_NEED();
    struct fc_thr t; if (!fc_thr_start(&t)) { FC_SKIP("worker thread failed"); return; }
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    DWORD me = GetCurrentThreadId();
    if (!U(AttachThreadInput)(me, t.tid, TRUE)) { fc_destroy(a); fc_thr_stop(&t); FC_SKIP("attach failed"); }
    HWND r = U(SetCapture)(t.w[0]);
    if (cap_took(t.w[0])) {
        HWND wc = NULL;
        fc_thr_call(&t, thr_getcap, &wc, NULL);
        EXPECT_EQ(wc, t.w[0]);
        U(ReleaseCapture)();
        EXPECT_EQ(U(GetCapture)(), NULL);
    } else {
        t_note("attached cross-thread SetCapture: ret=%p err=%lu", r, GetLastError());
        CHECK(0);
    }
    fc_detach_all(me, t.tid);
    fc_destroy(a);
    fc_thr_stop(&t);
}

TEST(fca_capture_disabled_window_allowed) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    U(EnableWindow)(a, FALSE);
    HWND r = U(SetCapture)(a);   /* SetCapture has no WS_DISABLED test */
    EXPECT_EQ(r, NULL);
    EXPECT_EQ(U(GetCapture)(), a);
    U(ReleaseCapture)();
    U(EnableWindow)(a, TRUE);
    fc_destroy(a);
}

TEST(fca_capture_hidden_window_allowed) {
    FC_NEED();
    HWND a = fc_hidden(); REQUIRE(a);
    fc_quiesce();
    U(SetCapture)(a);
    EXPECT_EQ(U(GetCapture)(), a);   /* no visibility test either */
    U(ReleaseCapture)();
    fc_destroy(a);
}

TEST(fca_capture_child_window) {
    FC_NEED();
    HWND p = fc_top(); REQUIRE(p);
    HWND c = fc_child(p); REQUIRE(c);
    fc_quiesce();
    U(SetCapture)(c);
    EXPECT_EQ(U(GetCapture)(), c);
    U(ReleaseCapture)();
    fc_destroy(p);
}

TEST(fca_capture_independent_of_focus_and_active) {
    FC_NEED();
    HWND a = fc_top(), b = fc_top(); REQUIRE(a && b);
    fc_quiesce();
    U(SetFocus)(a);
    HWND f = U(GetFocus)(), act = U(GetActiveWindow)();
    U(SetCapture)(b);
    if (!cap_took(b)) { fc_destroy(a); fc_destroy(b); FC_SKIP("capture not taken"); }
    EXPECT_EQ(U(GetFocus)(), f);
    EXPECT_EQ(U(GetActiveWindow)(), act);
    U(ReleaseCapture)();
    EXPECT_EQ(U(GetFocus)(), f);
    fc_destroy(a); fc_destroy(b);
}

TEST(fca_capture_destroy_capture_window) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    U(SetCapture)(a);
    if (!cap_took(a)) { fc_destroy(a); FC_SKIP("capture not taken"); }
    fc_reset();
    U(DestroyWindow)(a);
    EXPECT_EQ(U(GetCapture)(), NULL);        /* destruction releases the capture */
    GUITHREADINFO g;
    CHECK(fc_gti(GetCurrentThreadId(), &g));
    EXPECT_EQ(g.hwndCapture, NULL);
    t_note("WM_CAPTURECHANGED count on destroy: %d", fc_count(WM_CAPTURECHANGED, NULL));
}

TEST(fca_capture_matches_guithreadinfo) {
    FC_NEED();
    HWND a = fc_top(), b = fc_top(); REQUIRE(a && b);
    fc_quiesce();
    GUITHREADINFO g;
    HWND seq[] = { a, b, NULL };
    for (int i = 0; i < 3; i++) {
        if (seq[i]) U(SetCapture)(seq[i]); else U(ReleaseCapture)();
        CHECK(fc_gti(GetCurrentThreadId(), &g));
        EXPECT_EQ(g.hwndCapture, U(GetCapture)());
    }
    fc_destroy(a); fc_destroy(b);
}

TEST(fca_capture_threadstate_class) {
    FC_NEED(); if (t_is_wine()) { FC_SKIP("syscall info classes are win32k-specific"); return; }
    REQUIRE(UN(NtUserGetThreadState));
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    EXPECT_CALL_EQ(UN(NtUserGetThreadState)(FC_TS_CAPTURE), 0);
    U(SetCapture)(a);
    if (cap_took(a)) EXPECT_CALL_EQ(UN(NtUserGetThreadState)(FC_TS_CAPTURE), a);
    U(ReleaseCapture)();
    EXPECT_CALL_EQ(UN(NtUserGetThreadState)(FC_TS_CAPTURE), 0);
    fc_destroy(a);
}

TEST(fca_capture_server_counter_nonzero_while_captured) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    unsigned long before = 0, during = 0;
    if (!fc_psi_dword(FC_PSI_CCAPTURES, &before)) { fc_destroy(a); FC_SKIP("SERVERINFO not readable"); }
    U(SetCapture)(a);
    if (!cap_took(a)) { fc_destroy(a); FC_SKIP("capture not taken"); }
    CHECK(fc_psi_dword(FC_PSI_CCAPTURES, &during));
    /* user32!GetCapture returns NULL early when the shared capture counter is zero, so it must be non-zero while a window holds the capture. */
    CHECKF(during != 0, "cCaptures=%lu (before %lu)", during, before);
    U(ReleaseCapture)();
    unsigned long after = 0; fc_psi_dword(FC_PSI_CCAPTURES, &after);
    t_note("shared capture counter: before=%lu during=%lu after=%lu", before, during, after);
    fc_destroy(a);
}

TEST(fca_capture_repeat_set_same_window) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    U(SetCapture)(a);
    if (!cap_took(a)) { fc_destroy(a); FC_SKIP("capture not taken"); }
    HWND r = U(SetCapture)(a);
    EXPECT_EQ(r, a);
    EXPECT_EQ(U(GetCapture)(), a);
    U(ReleaseCapture)();
    EXPECT_EQ(U(GetCapture)(), NULL);
    fc_destroy(a);
}
