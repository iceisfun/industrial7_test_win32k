/* Carets: NtUserCreateCaret, NtUserShowCaret/HideCaret, NtUserGetCaretPos, SetCaretPos
 * (NtUserCallTwoParam 0x88), DestroyCaret (NtUserCallNoParam 6), GetCaretBlinkTime. State lives in
 * the thread's tagQ: spwndCaret, hide count (+0x134), position (+0x13c/+0x140), size (+0x148/+0x144). */
#include "fc.h"

static void caret_clear(void) { U(DestroyCaret)(); }

TEST(fca_caret_setpos_without_caret) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce(); caret_clear();
    SET_ERR(0);
    CHECK(!U(SetCaretPos)(5, 6));
    EXPECT_ERR(ERROR_ACCESS_DENIED);   /* UT_CaretSet: queue has no caret -> error 5 */
    fc_destroy(a);
}

TEST(fca_caret_show_hide_destroy_without_caret) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce(); caret_clear();
    SET_ERR(0); CHECK(!U(ShowCaret)(NULL)); EXPECT_ERR(ERROR_ACCESS_DENIED);
    SET_ERR(0); CHECK(!U(HideCaret)(NULL)); EXPECT_ERR(ERROR_ACCESS_DENIED);
    SET_ERR(0); CHECK(!U(ShowCaret)(a));    EXPECT_ERR(ERROR_ACCESS_DENIED);
    SET_ERR(0); CHECK(!U(HideCaret)(a));    EXPECT_ERR(ERROR_ACCESS_DENIED);
    SET_ERR(0); CHECK(!U(DestroyCaret)());  EXPECT_ERR(ERROR_ACCESS_DENIED);   /* zzzDestroyCaret -> UT_CaretSet(NULL) */
    fc_destroy(a);
}

TEST(fca_caret_show_hide_without_caret_direct) {
    FC_NEED(); REQUIRE(UN(NtUserShowCaret) && UN(NtUserHideCaret));
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce(); caret_clear();
    SET_ERR(0); CHECK(!UN(NtUserShowCaret)(NULL)); EXPECT_ERR(ERROR_ACCESS_DENIED);
    SET_ERR(0); CHECK(!UN(NtUserHideCaret)(a));    EXPECT_ERR(ERROR_ACCESS_DENIED);
    fc_destroy(a);
}

TEST(fca_caret_getpos_always_succeeds) {
    FC_NEED(); REQUIRE(UN(NtUserGetCaretPos));
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce(); caret_clear();
    POINT p = { 0x1234, 0x5678 };
    CHECK(U(GetCaretPos)(&p));          /* NtUserGetCaretPos just copies pq+0x13c/0x140 and returns 1 */
    POINT q = { 0x1234, 0x5678 };
    CHECK(UN(NtUserGetCaretPos)(&q));
    CHECK(p.x == q.x && p.y == q.y);
    fc_destroy(a);
}

TEST(fca_caret_create_invalid_hwnd) {
    FC_NEED(); REQUIRE(UN(NtUserCreateCaret));
    HWND a = fc_top(), s = fc_top(); REQUIRE(a && s);
    fc_destroy(s);
    fc_quiesce(); caret_clear();
    HWND bad[] = { NULL, (HWND)(ULONG_PTR)1, (HWND)(ULONG_PTR)0xDEADBEEF, s, (HWND)((ULONG_PTR)a ^ 0x00030000) };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SET_ERR(0);
        CHECKF(!U(CreateCaret)(bad[i], NULL, 4, 4), "CreateCaret(%p) succeeded", bad[i]);
        EXPECT_ERR(1400);
        SET_ERR(0);
        CHECKF(!UN(NtUserCreateCaret)(bad[i], NULL, 4, 4), "NtUserCreateCaret(%p) succeeded", bad[i]);
        EXPECT_ERR(1400);
    }
    GUITHREADINFO g; CHECK(fc_gti(GetCurrentThreadId(), &g)); EXPECT_EQ(g.hwndCaret, NULL);
    fc_destroy(a);
}

TEST(fca_caret_create_basic) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce(); caret_clear();
    CHECK(U(CreateCaret)(a, NULL, 4, 9));
    GUITHREADINFO g; CHECK(fc_gti(GetCurrentThreadId(), &g));
    EXPECT_EQ(g.hwndCaret, a);
    EXPECT_EQ(g.rcCaret.right - g.rcCaret.left, 4);
    EXPECT_EQ(g.rcCaret.bottom - g.rcCaret.top, 9);
    CHECK(U(DestroyCaret)());
    fc_destroy(a);
}

TEST(fca_caret_create_direct_stub) {
    FC_NEED(); REQUIRE(UN(NtUserCreateCaret));
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce(); caret_clear();
    CHECK(UN(NtUserCreateCaret)(a, NULL, 3, 5));
    GUITHREADINFO g; CHECK(fc_gti(GetCurrentThreadId(), &g));
    EXPECT_EQ(g.hwndCaret, a);
    EXPECT_EQ(g.rcCaret.right - g.rcCaret.left, 3);
    EXPECT_EQ(g.rcCaret.bottom - g.rcCaret.top, 5);
    CHECK(U(DestroyCaret)());
    fc_destroy(a);
}

TEST(fca_caret_size_table) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce(); caret_clear();
    int t[][4] = { {1,1,1,1}, {2,3,2,3}, {10,1,10,1}, {1,40,1,40}, {0,5,1,5}, {5,0,5,1}, {0,0,1,1}, {100,100,100,100} };
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
        CHECKF(U(CreateCaret)(a, NULL, t[i][0], t[i][1]), "case %u", i);
        GUITHREADINFO g; CHECK(fc_gti(GetCurrentThreadId(), &g));
        /* CreateCaret substitutes 1 for a zero width or height. */
        CHECKF(g.rcCaret.right - g.rcCaret.left == t[i][2] && g.rcCaret.bottom - g.rcCaret.top == t[i][3],
               "case %u: %dx%d -> %ldx%ld", i, t[i][0], t[i][1],
               g.rcCaret.right - g.rcCaret.left, g.rcCaret.bottom - g.rcCaret.top);
    }
    U(DestroyCaret)();
    fc_destroy(a);
}

TEST(fca_caret_bitmap_defines_size) {
    FC_NEED(); REQUIRE(U(CreateBitmap) && U(DeleteObject));
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce(); caret_clear();
    HBITMAP bm = U(CreateBitmap)(7, 11, 1, 1, NULL);
    REQUIRE(bm);
    CHECK(U(CreateCaret)(a, bm, 99, 99));
    GUITHREADINFO g; CHECK(fc_gti(GetCurrentThreadId(), &g));
    /* a bitmap handle (> 1) takes its dimensions from GetObject(BITMAP), ignoring nWidth/nHeight */
    EXPECT_EQ(g.rcCaret.right - g.rcCaret.left, 7);
    EXPECT_EQ(g.rcCaret.bottom - g.rcCaret.top, 11);
    U(DestroyCaret)();
    U(DeleteObject)(bm);
    fc_destroy(a);
}

TEST(fca_caret_bitmap_zero_and_one_use_arguments) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce(); caret_clear();
    HBITMAP special[] = { NULL, (HBITMAP)(ULONG_PTR)1 };   /* solid / gray */
    for (int i = 0; i < 2; i++) {
        CHECK(U(CreateCaret)(a, special[i], 6, 4));
        GUITHREADINFO g; CHECK(fc_gti(GetCurrentThreadId(), &g));
        EXPECT_EQ(g.rcCaret.right - g.rcCaret.left, 6);
        EXPECT_EQ(g.rcCaret.bottom - g.rcCaret.top, 4);
    }
    U(DestroyCaret)();
    fc_destroy(a);
}

TEST(fca_caret_show_hide_counting) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce(); caret_clear();
    CHECK(U(CreateCaret)(a, NULL, 2, 2));
    GUITHREADINFO g;
    #define VISIBLE() (fc_gti(GetCurrentThreadId(), &g) && (g.flags & GUI_CARETBLINKING))
    CHECK(!VISIBLE());                     /* created hidden: hide count 1 */
    CHECK(U(HideCaret)(a)); CHECK(!VISIBLE());   /* hide count 2 */
    CHECK(U(ShowCaret)(a)); CHECK(!VISIBLE());   /* back to 1: still hidden */
    CHECK(U(ShowCaret)(a)); CHECK(VISIBLE());    /* 0: visible */
    CHECK(U(ShowCaret)(a)); CHECK(VISIBLE());    /* show on a visible caret does not go negative */
    CHECK(U(HideCaret)(a)); CHECK(!VISIBLE());   /* a single hide hides it again */
    CHECK(U(ShowCaret)(a)); CHECK(VISIBLE());
    CHECK(U(ShowCaret)(NULL)); CHECK(VISIBLE()); /* NULL targets the queue's caret */
    CHECK(U(HideCaret)(NULL)); CHECK(!VISIBLE());
    #undef VISIBLE
    U(DestroyCaret)();
    fc_destroy(a);
}

TEST(fca_caret_show_hide_direct_stub) {
    FC_NEED(); REQUIRE(UN(NtUserShowCaret) && UN(NtUserHideCaret));
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce(); caret_clear();
    CHECK(U(CreateCaret)(a, NULL, 2, 2));
    GUITHREADINFO g;
    CHECK(UN(NtUserShowCaret)(a));
    CHECK(fc_gti(GetCurrentThreadId(), &g)); EXPECT_EQ(g.flags & GUI_CARETBLINKING, 1);
    CHECK(UN(NtUserHideCaret)(a));
    CHECK(fc_gti(GetCurrentThreadId(), &g)); EXPECT_EQ(g.flags & GUI_CARETBLINKING, 0);
    U(DestroyCaret)();
    fc_destroy(a);
}

TEST(fca_caret_other_window_cannot_show_hide) {
    FC_NEED();
    HWND a = fc_top(), b = fc_top(); REQUIRE(a && b);
    fc_quiesce(); caret_clear();
    CHECK(U(CreateCaret)(a, NULL, 2, 2));
    CHECK(U(ShowCaret)(a));
    CHECK(!U(ShowCaret)(b));    /* UT_CaretSet: hwnd is not the caret window -> returns 0 */
    CHECK(!U(HideCaret)(b));
    GUITHREADINFO g; CHECK(fc_gti(GetCurrentThreadId(), &g));
    EXPECT_EQ(g.hwndCaret, a);
    EXPECT_EQ(g.flags & GUI_CARETBLINKING, 1);   /* untouched by the refused HideCaret */
    U(DestroyCaret)();
    fc_destroy(a); fc_destroy(b);
}

TEST(fca_caret_replace_on_second_create) {
    FC_NEED();
    HWND a = fc_top(), b = fc_top(); REQUIRE(a && b);
    fc_quiesce(); caret_clear();
    CHECK(U(CreateCaret)(a, NULL, 2, 2));
    CHECK(U(ShowCaret)(a));
    CHECK(U(CreateCaret)(b, NULL, 3, 3));        /* one caret per queue: the old one is destroyed first */
    GUITHREADINFO g; CHECK(fc_gti(GetCurrentThreadId(), &g));
    EXPECT_EQ(g.hwndCaret, b);
    EXPECT_EQ(g.flags & GUI_CARETBLINKING, 0);   /* the new caret starts hidden again */
    CHECK(!U(ShowCaret)(a));
    CHECK(U(ShowCaret)(b));
    CHECK(U(DestroyCaret)());
    SET_ERR(0);
    CHECK(!U(DestroyCaret)());
    EXPECT_ERR(ERROR_ACCESS_DENIED);
    fc_destroy(a); fc_destroy(b);
}

TEST(fca_caret_recreate_same_window_resets_hide_count) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce(); caret_clear();
    CHECK(U(CreateCaret)(a, NULL, 2, 2));
    U(ShowCaret)(a); U(ShowCaret)(a);
    CHECK(U(CreateCaret)(a, NULL, 5, 6));
    GUITHREADINFO g; CHECK(fc_gti(GetCurrentThreadId(), &g));
    EXPECT_EQ(g.flags & GUI_CARETBLINKING, 0);
    EXPECT_EQ(g.rcCaret.right - g.rcCaret.left, 5);
    U(DestroyCaret)();
    fc_destroy(a);
}

TEST(fca_caret_destroy_clears_state) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce(); caret_clear();
    CHECK(U(CreateCaret)(a, NULL, 2, 2));
    CHECK(U(ShowCaret)(a));
    CHECK(U(DestroyCaret)());
    GUITHREADINFO g; CHECK(fc_gti(GetCurrentThreadId(), &g));
    EXPECT_EQ(g.hwndCaret, NULL);
    EXPECT_EQ(g.flags & GUI_CARETBLINKING, 0);
    SET_ERR(0); CHECK(!U(SetCaretPos)(1, 1)); EXPECT_ERR(ERROR_ACCESS_DENIED);
    SET_ERR(0); CHECK(!U(ShowCaret)(a));       EXPECT_ERR(ERROR_ACCESS_DENIED);
    fc_destroy(a);
}

TEST(fca_caret_destroy_direct_call) {
    FC_NEED(); if (t_is_wine()) { FC_SKIP("win32k call codes"); return; }
    REQUIRE(UN(NtUserCallNoParam));
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce(); caret_clear();
    CHECK(U(CreateCaret)(a, NULL, 2, 2));
    EXPECT_CALL_EQ(UN(NtUserCallNoParam)(FC_CALL_DESTROYCARET), 1);
    GUITHREADINFO g; CHECK(fc_gti(GetCurrentThreadId(), &g));
    EXPECT_EQ(g.hwndCaret, NULL);
    SET_ERR(0);
    EXPECT_CALL_EQ(UN(NtUserCallNoParam)(FC_CALL_DESTROYCARET), 0);
    EXPECT_ERR(ERROR_ACCESS_DENIED);
    fc_destroy(a);
}

TEST(fca_caret_window_destroy_removes_caret) {
    FC_NEED();
    HWND a = fc_top(), b = fc_top(); REQUIRE(a && b);
    fc_quiesce(); caret_clear();
    CHECK(U(CreateCaret)(a, NULL, 2, 2));
    U(DestroyWindow)(a);
    GUITHREADINFO g; CHECK(fc_gti(GetCurrentThreadId(), &g));
    EXPECT_EQ(g.hwndCaret, NULL);
    SET_ERR(0); CHECK(!U(SetCaretPos)(1, 1)); EXPECT_ERR(ERROR_ACCESS_DENIED);
    fc_destroy(b);
}

TEST(fca_caret_setpos_roundtrip_table) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce(); caret_clear();
    CHECK(U(CreateCaret)(a, NULL, 2, 2));
    int pts[][2] = { {0,0}, {1,1}, {17,29}, {-5,-7}, {1000,2000}, {0,100}, {100,0}, {32767,32767}, {-32768,-32768}, {123456,654321} };
    for (unsigned i = 0; i < sizeof pts / sizeof pts[0]; i++) {
        SET_ERR(0);
        CHECKF(U(SetCaretPos)(pts[i][0], pts[i][1]), "SetCaretPos(%d,%d) err %lu", pts[i][0], pts[i][1], GetLastError());
        POINT p = { -1, -1 };
        CHECK(U(GetCaretPos)(&p));
        CHECKF(p.x == pts[i][0] && p.y == pts[i][1], "(%d,%d) -> (%ld,%ld)", pts[i][0], pts[i][1], p.x, p.y);
        GUITHREADINFO g; CHECK(fc_gti(GetCurrentThreadId(), &g));
        CHECK(g.rcCaret.left == pts[i][0] && g.rcCaret.top == pts[i][1]);
    }
    U(DestroyCaret)();
    fc_destroy(a);
}

TEST(fca_caret_setpos_same_position_succeeds) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce(); caret_clear();
    CHECK(U(CreateCaret)(a, NULL, 2, 2));
    CHECK(U(SetCaretPos)(9, 9));
    CHECK(U(SetCaretPos)(9, 9));      /* zzzSetCaretPos returns 1 even when nothing moved */
    U(DestroyCaret)();
    fc_destroy(a);
}

TEST(fca_caret_setpos_while_hidden_and_shown) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce(); caret_clear();
    CHECK(U(CreateCaret)(a, NULL, 3, 3));
    CHECK(U(SetCaretPos)(4, 4));      /* hidden caret can still be positioned */
    CHECK(U(ShowCaret)(a));
    CHECK(U(SetCaretPos)(8, 8));
    POINT p; CHECK(U(GetCaretPos)(&p)); CHECK(p.x == 8 && p.y == 8);
    CHECK(U(HideCaret)(a));
    CHECK(U(GetCaretPos)(&p)); CHECK(p.x == 8 && p.y == 8);   /* position survives hide */
    U(DestroyCaret)();
    fc_destroy(a);
}

static DWORD_PTR thr_caret_only(void *p) {
    HWND *a = (HWND *)p;
    U(DestroyCaret)();
    a[1] = (HWND)(ULONG_PTR)U(CreateCaret)(a[0], NULL, 5, 5);
    U(SetCaretPos)(21, 22);
    return 0;
}
static DWORD_PTR thr_probe_caret(void *p) {
    HWND *a = (HWND *)p;
    SetLastError(0);
    a[1] = (HWND)(ULONG_PTR)U(SetCaretPos)(1, 1);
    a[2] = (HWND)(ULONG_PTR)GetLastError();
    return 0;
}
static DWORD_PTR thr_setpos(void *p) {
    int *a = (int *)p;
    SetLastError(0);
    a[0] = U(SetCaretPos)(a[2], a[3]);
    a[1] = (int)GetLastError();
    return 0;
}

TEST(fca_caret_is_per_queue) {
    FC_NEED();
    struct fc_thr t; if (!fc_thr_start(&t)) { FC_SKIP("worker thread failed"); return; }
    HWND mine = fc_top(); REQUIRE(mine);
    fc_quiesce(); caret_clear();
    HWND args[3] = { t.w[0], 0, 0 };
    CHECK(fc_thr_call(&t, thr_caret_only, args, NULL));
    CHECK(args[1] != NULL);
    /* the worker's caret is invisible to this thread's queue: every caret call here fails with error 5 */
    SET_ERR(0); CHECK(!U(SetCaretPos)(3, 3)); EXPECT_ERR(ERROR_ACCESS_DENIED);
    SET_ERR(0); CHECK(!U(HideCaret)(NULL));   EXPECT_ERR(ERROR_ACCESS_DENIED);
    SET_ERR(0); CHECK(!U(ShowCaret)(t.w[0])); EXPECT_ERR(ERROR_ACCESS_DENIED);
    SET_ERR(0); CHECK(!U(DestroyCaret)());    EXPECT_ERR(ERROR_ACCESS_DENIED);
    GUITHREADINFO g; CHECK(fc_gti(t.tid, &g));
    EXPECT_EQ(g.hwndCaret, t.w[0]);           /* still there */
    EXPECT_EQ(g.rcCaret.left, 21);
    CHECK(fc_gti(GetCurrentThreadId(), &g));
    EXPECT_EQ(g.hwndCaret, NULL);
    fc_destroy(mine);
    fc_thr_stop(&t);
}

TEST(fca_caret_create_on_foreign_window_refused) {
    FC_NEED();
    struct fc_thr t; if (!fc_thr_start(&t)) { FC_SKIP("worker thread failed"); return; }
    fc_quiesce(); caret_clear();
    CHECK(!U(CreateCaret)(t.w[0], NULL, 4, 4));   /* CreateCaret: window must belong to the calling thread's queue */
    GUITHREADINFO g; CHECK(fc_gti(GetCurrentThreadId(), &g));
    EXPECT_EQ(g.hwndCaret, NULL);
    CHECK(fc_gti(t.tid, &g));
    EXPECT_EQ(g.hwndCaret, NULL);
    fc_thr_stop(&t);
}

TEST(fca_caret_threads_each_own_one) {
    FC_NEED();
    struct fc_thr t; if (!fc_thr_start(&t)) { FC_SKIP("worker thread failed"); return; }
    HWND mine = fc_top(); REQUIRE(mine);
    fc_quiesce(); caret_clear();
    HWND args[3] = { t.w[0], 0, 0 };
    fc_thr_call(&t, thr_caret_only, args, NULL);
    CHECK(U(CreateCaret)(mine, NULL, 2, 2));
    GUITHREADINFO g1, g2;
    CHECK(fc_gti(GetCurrentThreadId(), &g1)); CHECK(fc_gti(t.tid, &g2));
    EXPECT_EQ(g1.hwndCaret, mine);
    EXPECT_EQ(g2.hwndCaret, t.w[0]);      /* creating a caret here did not replace the worker's */
    U(DestroyCaret)();
    CHECK(fc_gti(t.tid, &g2));
    EXPECT_EQ(g2.hwndCaret, t.w[0]);
    fc_destroy(mine);
    fc_thr_stop(&t);
}

TEST(fca_caret_attached_threads_share_one_caret) {
    FC_NEED();
    struct fc_thr t; if (!fc_thr_start(&t)) { FC_SKIP("worker thread failed"); return; }
    HWND mine = fc_top(); REQUIRE(mine);
    fc_quiesce(); caret_clear();
    DWORD me = GetCurrentThreadId();
    HWND args[3] = { t.w[0], 0, 0 };
    fc_thr_call(&t, thr_caret_only, args, NULL);    /* worker created the caret on its own window */
    if (!U(AttachThreadInput)(me, t.tid, TRUE)) { fc_destroy(mine); fc_thr_stop(&t); FC_SKIP("attach failed"); }
    GUITHREADINFO g; CHECK(fc_gti(me, &g));
    EXPECT_EQ(g.hwndCaret, t.w[0]);                  /* shared queue: our view shows the worker's caret */
    /* UT_CaretSet accepts a caret created by another thread of the same process on the shared queue. */
    SET_ERR(0);
    BOOL r = U(SetCaretPos)(31, 32);
    CHECKF(r, "SetCaretPos on attached thread's caret failed, err %lu", GetLastError());
    int a[4] = {0, 0, 40, 41};
    fc_thr_call(&t, thr_setpos, a, NULL);
    CHECK(a[0]);
    POINT p; CHECK(U(GetCaretPos)(&p)); CHECK(p.x == 40 && p.y == 41);
    CHECK(U(CreateCaret)(mine, NULL, 3, 3));        /* replaces the worker's caret in the shared queue */
    CHECK(fc_gti(t.tid, &g));
    EXPECT_EQ(g.hwndCaret, mine);
    fc_detach_all(me, t.tid);
    U(DestroyCaret)();
    fc_destroy(mine);
    fc_thr_stop(&t);
}

TEST(fca_caret_blink_time_matches_direct) {
    FC_NEED(); REQUIRE(UN(NtUserGetCaretBlinkTime));
    UINT u = U(GetCaretBlinkTime)();
    UINT n = UN(NtUserGetCaretBlinkTime)();
    EXPECT_EQ(u, n);
    unsigned long raw = 0;
    if (fc_psi_dword(FC_PSI_CARETBLINK, &raw)) EXPECT_EQ(raw, n);   /* NtUserGetCaretBlinkTime returns the shared-info blink time */
    t_note("caret blink time %u ms", u);
}

TEST(fca_caret_blink_time_set_and_restore) {
    FC_NEED(); REQUIRE(U(SetCaretBlinkTime) && U(GetCaretBlinkTime));
    UINT old = U(GetCaretBlinkTime)();
    UINT vals[] = { 300, 777, 0xFFFFFFFF };
    for (unsigned i = 0; i < sizeof vals / sizeof vals[0]; i++) {
        SET_ERR(0);
        BOOL ok = U(SetCaretBlinkTime)(vals[i]);
        if (!ok) { t_note("SetCaretBlinkTime(%u) refused, err %lu", vals[i], GetLastError()); continue; }
        EXPECT_EQ(U(GetCaretBlinkTime)(), vals[i]);   /* SetCaretBlinkTime stores the value verbatim in the shared info */
        unsigned long raw;
        if (fc_psi_dword(FC_PSI_CARETBLINK, &raw)) EXPECT_EQ(raw, vals[i]);
    }
    U(SetCaretBlinkTime)(old);
    EXPECT_EQ(U(GetCaretBlinkTime)(), old);
}

TEST(fca_caret_blink_time_with_live_caret) {
    FC_NEED(); REQUIRE(U(SetCaretBlinkTime));
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce(); caret_clear();
    UINT old = U(GetCaretBlinkTime)();
    CHECK(U(CreateCaret)(a, NULL, 2, 2));
    CHECK(U(ShowCaret)(a));
    if (U(SetCaretBlinkTime)(old == 400 ? 450 : 400)) {   /* re-arms the caret timer (RemoveCaretTimer/CreateCaretTimer) */
        GUITHREADINFO g; CHECK(fc_gti(GetCurrentThreadId(), &g));
        EXPECT_EQ(g.hwndCaret, a);
        EXPECT_EQ(g.flags & GUI_CARETBLINKING, 1);        /* still visible after the timer was re-armed */
    }
    U(SetCaretBlinkTime)(old);
    U(DestroyCaret)();
    fc_destroy(a);
}
