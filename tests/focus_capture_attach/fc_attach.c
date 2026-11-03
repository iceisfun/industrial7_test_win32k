/* NtUserAttachThreadInput (win32kfull!NtUserAttachThreadInput -> zzzAttachThreadInput).
 * Error mapping: unknown/non-GUI thread ids -> 0x57; attaching a thread to itself or detaching a pair
 * that is not attached -> STATUS_INVALID_PARAMETER -> 87; different desktops / access refusal -> 5.
 * The kernel keeps a reference count per pair (gpai list): N attaches need N detaches. */
#include "fc.h"

static BOOL AT(DWORD a, DWORD b, BOOL f) { return U(AttachThreadInput)(a, b, f); }

TEST(fca_attach_invalid_ids_user32) {
    FC_NEED();
    DWORD me = GetCurrentThreadId();
    HWND w = fc_top(); REQUIRE(w);      /* make sure this thread is a GUI thread */
    DWORD ids[][2] = { {0,0}, {me,0}, {0,me}, {1,2}, {me,1}, {1,me}, {me,0xFFFFFFFC}, {0xFFFFFFFC,me},
                       {me,0xFFFFFFFF}, {0x7FFFFFFC,0x7FFFFFF8}, {me,3}, {me,0x80000000} };
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        for (int f = 0; f < 2; f++) {
            SET_ERR(0);
            BOOL r = AT(ids[i][0], ids[i][1], f);
            CHECKF(!r, "attach(%lx,%lx,%d) accepted", ids[i][0], ids[i][1], f);
            EXPECT_ERR(ERROR_INVALID_PARAMETER);
        }
    }
    fc_destroy(w);
}

TEST(fca_attach_invalid_ids_direct) {
    FC_NEED(); REQUIRE(UN(NtUserAttachThreadInput));
    DWORD me = GetCurrentThreadId();
    HWND w = fc_top(); REQUIRE(w);
    DWORD ids[][2] = { {0,0}, {me,0}, {0,me}, {1,2}, {me,0xFFFFFFFF} };
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        SET_ERR(0);
        BOOL r = UN(NtUserAttachThreadInput)(ids[i][0], ids[i][1], TRUE);
        CHECKF(!r, "direct attach(%lx,%lx) accepted", ids[i][0], ids[i][1]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
    fc_destroy(w);
}

TEST(fca_attach_to_self_fails) {
    FC_NEED();
    DWORD me = GetCurrentThreadId();
    HWND w = fc_top(); REQUIRE(w);
    SET_ERR(0); CHECK(!AT(me, me, TRUE));  EXPECT_ERR(ERROR_INVALID_PARAMETER);   /* zzzAttachThreadInput: pti1 == pti2 -> STATUS_INVALID_PARAMETER */
    SET_ERR(0); CHECK(!AT(me, me, FALSE)); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    fc_destroy(w);
}

TEST(fca_attach_worker_to_itself_fails) {
    FC_NEED();
    struct fc_thr t; if (!fc_thr_start(&t)) { FC_SKIP("worker thread failed"); return; }
    SET_ERR(0); CHECK(!AT(t.tid, t.tid, TRUE)); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    fc_thr_stop(&t);
}

TEST(fca_attach_detach_when_not_attached) {
    FC_NEED();
    struct fc_thr t; if (!fc_thr_start(&t)) { FC_SKIP("worker thread failed"); return; }
    HWND w = fc_top(); REQUIRE(w);
    DWORD me = GetCurrentThreadId();
    SET_ERR(0); CHECK(!AT(me, t.tid, FALSE)); EXPECT_ERR(ERROR_INVALID_PARAMETER);   /* no gpai entry for the pair */
    SET_ERR(0); CHECK(!AT(t.tid, me, FALSE)); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    fc_destroy(w);
    fc_thr_stop(&t);
}

TEST(fca_attach_basic_pair) {
    FC_NEED();
    struct fc_thr t; if (!fc_thr_start(&t)) { FC_SKIP("worker thread failed"); return; }
    HWND w = fc_top(); REQUIRE(w);
    DWORD me = GetCurrentThreadId();
    SET_ERR(0);
    BOOL a = AT(me, t.tid, TRUE);
    if (!a) { t_note("attach err=%lu", GetLastError()); fc_destroy(w); fc_thr_stop(&t); FC_SKIP("attach refused"); }
    CHECK(AT(me, t.tid, FALSE));
    SET_ERR(0); CHECK(!AT(me, t.tid, FALSE)); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    fc_destroy(w);
    fc_thr_stop(&t);
}

TEST(fca_attach_direct_stub_pair) {
    FC_NEED(); REQUIRE(UN(NtUserAttachThreadInput));
    struct fc_thr t; if (!fc_thr_start(&t)) { FC_SKIP("worker thread failed"); return; }
    HWND w = fc_top(); REQUIRE(w);
    DWORD me = GetCurrentThreadId();
    if (!UN(NtUserAttachThreadInput)(me, t.tid, TRUE)) { fc_destroy(w); fc_thr_stop(&t); FC_SKIP("attach refused"); }
    CHECK(UN(NtUserAttachThreadInput)(t.tid, me, FALSE));
    SET_ERR(0); CHECK(!UN(NtUserAttachThreadInput)(me, t.tid, FALSE)); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    fc_destroy(w);
    fc_thr_stop(&t);
}

TEST(fca_attach_reference_counted) {
    FC_NEED();
    struct fc_thr t; if (!fc_thr_start(&t)) { FC_SKIP("worker thread failed"); return; }
    HWND w = fc_top(); REQUIRE(w);
    DWORD me = GetCurrentThreadId();
    if (!AT(me, t.tid, TRUE)) { fc_destroy(w); fc_thr_stop(&t); FC_SKIP("attach refused"); }
    CHECK(AT(me, t.tid, TRUE));      /* second attach of the same pair only bumps the count */
    CHECK(AT(me, t.tid, TRUE));
    CHECK(AT(me, t.tid, FALSE));
    CHECK(AT(me, t.tid, FALSE));
    CHECK(AT(me, t.tid, FALSE));     /* count reaches zero here */
    SET_ERR(0); CHECK(!AT(me, t.tid, FALSE)); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    fc_destroy(w);
    fc_thr_stop(&t);
}

TEST(fca_attach_still_shared_after_partial_detach) {
    FC_NEED();
    struct fc_thr t; if (!fc_thr_start(&t)) { FC_SKIP("worker thread failed"); return; }
    HWND w = fc_top(); REQUIRE(w);
    DWORD me = GetCurrentThreadId();
    if (!AT(me, t.tid, TRUE)) { fc_destroy(w); fc_thr_stop(&t); FC_SKIP("attach refused"); }
    CHECK(AT(me, t.tid, TRUE));
    CHECK(AT(me, t.tid, FALSE));     /* one reference left: queues stay joined */
    SET_ERR(0);
    U(SetFocus)(t.w[0]);
    CHECKF(U(GetFocus)() == t.w[0], "cross-thread SetFocus after partial detach: err %lu", GetLastError());
    CHECK(AT(me, t.tid, FALSE));     /* last reference: queues split */
    SET_ERR(0);
    HWND r = U(SetFocus)(t.w[1]);
    CHECK(r == NULL);
    EXPECT_ERR(ERROR_ACCESS_DENIED);
    fc_destroy(w);
    fc_thr_stop(&t);
}

TEST(fca_attach_order_symmetric) {
    FC_NEED();
    struct fc_thr t; if (!fc_thr_start(&t)) { FC_SKIP("worker thread failed"); return; }
    HWND w = fc_top(); REQUIRE(w);
    DWORD me = GetCurrentThreadId();
    if (!AT(me, t.tid, TRUE)) { fc_destroy(w); fc_thr_stop(&t); FC_SKIP("attach refused"); }
    CHECK(AT(t.tid, me, FALSE));     /* zzzAttachThreadInput matches the pair in either order */
    SET_ERR(0); CHECK(!AT(me, t.tid, FALSE)); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    if (AT(t.tid, me, TRUE)) {
        CHECK(AT(me, t.tid, FALSE));
    }
    fc_destroy(w);
    fc_thr_stop(&t);
}

TEST(fca_attach_any_nonzero_fattach_attaches) {
    FC_NEED();
    struct fc_thr t; if (!fc_thr_start(&t)) { FC_SKIP("worker thread failed"); return; }
    HWND w = fc_top(); REQUIRE(w);
    DWORD me = GetCurrentThreadId();
    BOOL vals[] = { 2, 0x100, (BOOL)0x80000000, -1 };
    for (unsigned i = 0; i < sizeof vals / sizeof vals[0]; i++) {
        BOOL r = AT(me, t.tid, vals[i]);   /* NtUserAttachThreadInput tests fAttach != 0 */
        CHECKF(r, "fAttach=%d refused (err %lu)", vals[i], GetLastError());
        if (r) { CHECK(AT(me, t.tid, FALSE)); SET_ERR(0); CHECK(!AT(me, t.tid, FALSE)); EXPECT_ERR(ERROR_INVALID_PARAMETER); }
    }
    fc_destroy(w);
    fc_thr_stop(&t);
}

TEST(fca_attach_two_other_threads) {
    FC_NEED();
    struct fc_thr a, b;
    if (!fc_thr_start(&a)) { FC_SKIP("worker thread failed"); return; }
    if (!fc_thr_start(&b)) { fc_thr_stop(&a); FC_SKIP("worker thread failed"); return; }
    HWND w = fc_top(); REQUIRE(w);
    /* the caller need not be either thread; both targets are in the caller's process, so no UIPI check applies */
    BOOL r = AT(a.tid, b.tid, TRUE);
    if (!r) { t_note("attach between two other threads refused, err=%lu", GetLastError()); }
    else {
        GUITHREADINFO ga, gb;
        U(SetFocus)(NULL);
        CHECK(fc_gti(a.tid, &ga)); CHECK(fc_gti(b.tid, &gb));
        EXPECT_EQ(ga.hwndActive, gb.hwndActive);
        EXPECT_EQ(ga.hwndFocus, gb.hwndFocus);
        CHECK(AT(b.tid, a.tid, FALSE));
    }
    fc_destroy(w);
    fc_thr_stop(&a); fc_thr_stop(&b);
}

TEST(fca_attach_exited_thread_id) {
    FC_NEED();
    DWORD tid = 0;
    HANDLE h = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ExitThread, NULL, 0, &tid);
    REQUIRE(h);
    WaitForSingleObject(h, 5000);
    CloseHandle(h);
    HWND w = fc_top(); REQUIRE(w);
    SET_ERR(0); CHECK(!AT(GetCurrentThreadId(), tid, TRUE)); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SET_ERR(0); CHECK(!AT(tid, GetCurrentThreadId(), TRUE)); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    fc_destroy(w);
}

static DWORD WINAPI idle_thread(LPVOID p) { WaitForSingleObject((HANDLE)p, 5000); return 0; }

TEST(fca_attach_non_gui_thread) {
    FC_NEED();
    HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    DWORD tid = 0;
    HANDLE h = CreateThread(NULL, 0, idle_thread, ev, 0, &tid);
    REQUIRE(h);
    Sleep(50);
    HWND w = fc_top(); REQUIRE(w);
    GUITHREADINFO g;
    if (fc_gti(tid, &g)) {
        t_note("idle thread already has a W32THREAD; cannot exercise the non-GUI case");
    } else {
        SET_ERR(0); CHECK(!AT(GetCurrentThreadId(), tid, TRUE)); EXPECT_ERR(ERROR_INVALID_PARAMETER);   /* PtiFromThreadId -> NULL */
    }
    fc_destroy(w);
    SetEvent(ev);
    WaitForSingleObject(h, 5000);
    CloseHandle(h); CloseHandle(ev);
}

TEST(fca_attach_worker_exit_drops_attachment) {
    FC_NEED();
    struct fc_thr t; if (!fc_thr_start(&t)) { FC_SKIP("worker thread failed"); return; }
    HWND w = fc_top(); REQUIRE(w);
    DWORD me = GetCurrentThreadId(), wt = t.tid;
    if (!AT(me, wt, TRUE)) { fc_destroy(w); fc_thr_stop(&t); FC_SKIP("attach refused"); }
    fc_thr_stop(&t);                     /* attached thread terminates: the kernel detaches it during cleanup */
    SET_ERR(0);
    CHECK(!AT(me, wt, FALSE));
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    GUITHREADINFO g;
    CHECK(fc_gti(me, &g));               /* this thread's queue is still usable */
    CHECK(g.hwndFocus == NULL || U(IsWindow)(g.hwndFocus));
    fc_destroy(w);
}

TEST(fca_attach_shared_queue_state_equality) {
    FC_NEED();
    struct fc_thr t; if (!fc_thr_start(&t)) { FC_SKIP("worker thread failed"); return; }
    HWND w = fc_top(); REQUIRE(w);
    fc_quiesce();
    DWORD me = GetCurrentThreadId();
    if (!AT(me, t.tid, TRUE)) { fc_destroy(w); fc_thr_stop(&t); FC_SKIP("attach refused"); }
    U(SetCapture)(w);
    GUITHREADINFO a, b;
    CHECK(fc_gti(me, &a)); CHECK(fc_gti(t.tid, &b));
    EXPECT_EQ(a.hwndCapture, b.hwndCapture);
    EXPECT_EQ(a.hwndActive, b.hwndActive);
    EXPECT_EQ(a.hwndFocus, b.hwndFocus);
    U(ReleaseCapture)();
    fc_detach_all(me, t.tid);
    fc_destroy(w);
    fc_thr_stop(&t);
}

TEST(fca_attach_split_after_detach) {
    FC_NEED();
    struct fc_thr t; if (!fc_thr_start(&t)) { FC_SKIP("worker thread failed"); return; }
    HWND w = fc_top(); REQUIRE(w);
    fc_quiesce();
    DWORD me = GetCurrentThreadId();
    if (!AT(me, t.tid, TRUE)) { fc_destroy(w); fc_thr_stop(&t); FC_SKIP("attach refused"); }
    U(SetFocus)(t.w[0]);
    CHECK(AT(me, t.tid, FALSE));
    /* separate queues again: the foreign window is refused */
    SET_ERR(0);
    CHECK(U(SetFocus)(t.w[1]) == NULL);
    EXPECT_ERR(ERROR_ACCESS_DENIED);
    SET_ERR(0);
    CHECK(U(SetCapture)(t.w[1]) == NULL);
    EXPECT_EQ(U(GetCapture)(), NULL);
    fc_destroy(w);
    fc_thr_stop(&t);
}

TEST(fca_attach_does_not_change_thread_ids_or_windows) {
    FC_NEED();
    struct fc_thr t; if (!fc_thr_start(&t)) { FC_SKIP("worker thread failed"); return; }
    HWND w = fc_top(); REQUIRE(w);
    DWORD me = GetCurrentThreadId();
    if (!AT(me, t.tid, TRUE)) { fc_destroy(w); fc_thr_stop(&t); FC_SKIP("attach refused"); }
    DWORD pid = 0;
    EXPECT_CALL_EQ(U(GetWindowThreadProcessId)(t.w[0], &pid), t.tid);   /* window ownership is per thread, not per queue */
    EXPECT_CALL_EQ(U(GetWindowThreadProcessId)(w, &pid), me);
    CHECK(AT(me, t.tid, FALSE));
    fc_destroy(w);
    fc_thr_stop(&t);
}
