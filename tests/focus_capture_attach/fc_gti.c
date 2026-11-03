/* NtUserGetGUIThreadInfo (NtUserGetGUIThreadInfo). */
#include "fc.h"

static int no_0xcc(const void *p, size_t n) {
    const unsigned char *b = (const unsigned char *)p;
    for (size_t i = 0; i < n; i++) if (b[i] == 0xCC) return 0;
    return 1;
}

/* Fill the buffer with two different patterns: every byte the kernel writes is identical in both
 * results, so equality proves all 0x48 bytes were stored (a lone 0xCC could be genuine data). */
static int gti_fully_written(DWORD tid, BOOL *ret) {
    GUITHREADINFO a, b;
    memset(&a, 0xCC, sizeof a); a.cbSize = sizeof a;
    memset(&b, 0x33, sizeof b); b.cbSize = sizeof b;
    BOOL ra = U(GetGUIThreadInfo)(tid, &a), rb = U(GetGUIThreadInfo)(tid, &b);
    if (ret) *ret = ra && rb;
    return ra && rb && memcmp(&a, &b, sizeof a) == 0;
}

TEST(fca_gti_flag_constants) {
    /* GetGUIThreadInfo sets 1 (caret visible), 2 (in move/size), 4 (menu), 8 (system menu), 0x10 (popup menu). */
    EXPECT_EQ(GUI_CARETBLINKING, 1);
    EXPECT_EQ(GUI_INMOVESIZE, 2);
    EXPECT_EQ(GUI_INMENUMODE, 4);
    EXPECT_EQ(GUI_SYSTEMMENUMODE, 8);
    EXPECT_EQ(GUI_POPUPMENUMODE, 0x10);
    EXPECT_EQ(sizeof(GUITHREADINFO), 0x48);   /* the only cbSize the kernel accepts */
    EXPECT_EQ(offsetof(GUITHREADINFO, hwndCaret), 0x30);
    EXPECT_EQ(offsetof(GUITHREADINFO, rcCaret), 0x38);
}

TEST(fca_gti_cbsize_validation_user32) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    DWORD sizes[] = { 0, 1, 4, 0x24, 0x47, 0x49, 0x50, 0x100, 0x80000000, 0xFFFFFFFF };
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        unsigned char buf[0x120]; memset(buf, 0xCC, sizeof buf);
        GUITHREADINFO *g = (GUITHREADINFO *)buf;
        g->cbSize = sizes[i];
        SET_ERR(0);
        BOOL r = U(GetGUIThreadInfo)(GetCurrentThreadId(), g);
        CHECKF(!r, "cbSize %lx accepted", sizes[i]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
        CHECKF(g->cbSize == sizes[i] && no_0xcc(buf + 4, 0x44) == 0, "buffer modified on failure (cbSize %lx)", sizes[i]);
    }
    fc_destroy(a);
}

TEST(fca_gti_cbsize_validation_direct) {
    FC_NEED(); REQUIRE(UN(NtUserGetGUIThreadInfo));
    DWORD sizes[] = { 0, 0x47, 0x49, 0x90 };
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        unsigned char buf[0x120]; memset(buf, 0xCC, sizeof buf);
        GUITHREADINFO *g = (GUITHREADINFO *)buf;
        g->cbSize = sizes[i];
        SET_ERR(0);
        BOOL r = UN(NtUserGetGUIThreadInfo)(GetCurrentThreadId(), g);
        CHECKF(!r, "cbSize %lx accepted", sizes[i]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
}

TEST(fca_gti_bad_thread_ids) {
    FC_NEED(); REQUIRE(UN(NtUserGetGUIThreadInfo));
    DWORD ids[] = { 1, 2, 3, 5, 0x7FFFFFFC, 0xFFFFFFFC, 0xFFFFFFFF, 0x80000000 };
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        GUITHREADINFO g;
        SET_ERR(0);
        BOOL r = fc_gti(ids[i], &g);
        CHECKF(!r, "tid %lx accepted", ids[i]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);   /* PtiFromThreadId failed -> 0x57 */
        CHECK(no_0xcc(&g, 0x48) == 0);         /* untouched on failure */
        memset(&g, 0xCC, sizeof g); g.cbSize = sizeof g;
        SET_ERR(0);
        r = UN(NtUserGetGUIThreadInfo)(ids[i], &g);
        CHECKF(!r, "direct: tid %lx accepted", ids[i]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
}

static DWORD WINAPI exit_now(LPVOID p) { return 0; }

TEST(fca_gti_exited_thread_id) {
    FC_NEED();
    DWORD tid = 0;
    HANDLE h = CreateThread(NULL, 0, exit_now, NULL, 0, &tid);
    REQUIRE(h);
    WaitForSingleObject(h, 5000);
    CloseHandle(h);
    GUITHREADINFO g;
    SET_ERR(0);
    CHECK(!fc_gti(tid, &g));
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}

static DWORD WINAPI idle_thread(LPVOID p) { WaitForSingleObject((HANDLE)p, 5000); return 0; }

TEST(fca_gti_non_gui_thread_id) {
    FC_NEED();
    HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    DWORD tid = 0;
    HANDLE h = CreateThread(NULL, 0, idle_thread, ev, 0, &tid);
    REQUIRE(h);
    Sleep(50);
    GUITHREADINFO g;
    SET_ERR(0);
    BOOL r = fc_gti(tid, &g);
    if (r) t_note("thread that never made a win32k call already has a W32THREAD (user32 attach path converted it)");
    else EXPECT_ERR(ERROR_INVALID_PARAMETER);   /* PtiFromThreadId: no win32 thread -> NULL -> 0x57 */
    SetEvent(ev);
    WaitForSingleObject(h, 5000);
    CloseHandle(h); CloseHandle(ev);
}

TEST(fca_gti_all_fields_written) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    unsigned char buf[0x60]; memset(buf, 0xCC, sizeof buf);
    GUITHREADINFO *g = (GUITHREADINFO *)buf;
    g->cbSize = sizeof *g;
    BOOL r = U(GetGUIThreadInfo)(GetCurrentThreadId(), g);
    CHECK(r);
    EXPECT_EQ(g->cbSize, 0x48);
    BOOL both = FALSE;
    CHECK(gti_fully_written(GetCurrentThreadId(), &both));   /* flags, six HWNDs and rcCaret are all stored */
    CHECK(both);
    for (int i = 0x48; i < 0x60; i++) CHECK(buf[i] == 0xCC);   /* nothing past 0x48 */
    fc_destroy(a);
}

TEST(fca_gti_own_thread_idle) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    U(DestroyCaret)();
    GUITHREADINFO g;
    CHECK(fc_gti(GetCurrentThreadId(), &g));
    EXPECT_EQ(g.flags, 0);                   /* no caret, no move/size, no menu */
    EXPECT_EQ(g.hwndCapture, NULL);
    EXPECT_EQ(g.hwndFocus, NULL);
    EXPECT_EQ(g.hwndMenuOwner, NULL);
    EXPECT_EQ(g.hwndMoveSize, NULL);
    EXPECT_EQ(g.hwndCaret, NULL);
    EXPECT_EQ(g.rcCaret.left, 0); EXPECT_EQ(g.rcCaret.top, 0);
    EXPECT_EQ(g.rcCaret.right, 0); EXPECT_EQ(g.rcCaret.bottom, 0);
    fc_destroy(a);
}

TEST(fca_gti_direct_equals_user32) {
    FC_NEED(); REQUIRE(UN(NtUserGetGUIThreadInfo));
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    U(SetFocus)(a);
    GUITHREADINFO g1, g2;
    CHECK(fc_gti(GetCurrentThreadId(), &g1));
    memset(&g2, 0xCC, sizeof g2); g2.cbSize = sizeof g2;
    CHECK(UN(NtUserGetGUIThreadInfo)(GetCurrentThreadId(), &g2));
    CHECK(memcmp(&g1, &g2, sizeof g1) == 0);   /* GetGUIThreadInfo is a bare jump to the win32u stub */
    fc_destroy(a);
}

TEST(fca_gti_tracks_focus_active_capture) {
    FC_NEED();
    HWND a = fc_top(), b = fc_top(); REQUIRE(a && b);
    fc_quiesce();
    GUITHREADINFO g;
    for (int step = 0; step < 6; step++) {
        switch (step) {
        case 0: U(SetFocus)(a); break;
        case 1: U(SetFocus)(b); break;
        case 2: U(SetCapture)(a); break;
        case 3: U(SetCapture)(b); break;
        case 4: U(ReleaseCapture)(); break;
        case 5: U(SetFocus)(NULL); break;
        }
        CHECK(fc_gti(GetCurrentThreadId(), &g));
        EXPECT_EQ(g.hwndFocus, U(GetFocus)());
        EXPECT_EQ(g.hwndActive, U(GetActiveWindow)());
        EXPECT_EQ(g.hwndCapture, U(GetCapture)());
    }
    fc_destroy(a); fc_destroy(b);
}

TEST(fca_gti_fields_are_valid_windows) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    U(SetFocus)(a); U(SetCapture)(a);
    U(CreateCaret)(a, NULL, 2, 3);
    GUITHREADINFO g;
    CHECK(fc_gti(GetCurrentThreadId(), &g));
    HWND all[] = { g.hwndActive, g.hwndFocus, g.hwndCapture, g.hwndCaret };
    for (int i = 0; i < 4; i++) CHECKF(all[i] == NULL || U(IsWindow)(all[i]), "field %d = %p is not a window", i, all[i]);
    U(ReleaseCapture)(); U(DestroyCaret)();
    fc_destroy(a);
}

TEST(fca_gti_caret_flag_follows_show_hide) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    U(DestroyCaret)();
    GUITHREADINFO g;
    CHECK(U(CreateCaret)(a, NULL, 4, 8));
    CHECK(fc_gti(GetCurrentThreadId(), &g));
    EXPECT_EQ(g.hwndCaret, a);
    EXPECT_EQ(g.flags & GUI_CARETBLINKING, 0);   /* CreateCaret leaves the caret hidden (hide count 1) */
    CHECK(U(ShowCaret)(a));
    CHECK(fc_gti(GetCurrentThreadId(), &g));
    EXPECT_EQ(g.flags & GUI_CARETBLINKING, GUI_CARETBLINKING);
    CHECK(U(HideCaret)(a));
    CHECK(fc_gti(GetCurrentThreadId(), &g));
    EXPECT_EQ(g.flags & GUI_CARETBLINKING, 0);
    EXPECT_EQ(g.hwndCaret, a);                    /* hidden, but still the queue's caret window */
    CHECK(U(DestroyCaret)());
    CHECK(fc_gti(GetCurrentThreadId(), &g));
    EXPECT_EQ(g.hwndCaret, NULL);
    EXPECT_EQ(g.flags, 0);
    fc_destroy(a);
}

TEST(fca_gti_caret_rect) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    U(DestroyCaret)();
    struct { int w, h, x, y; } c[] = { {5, 7, 3, 4}, {1, 1, 0, 0}, {2, 20, 100, 50}, {30, 2, 7, 9}, {8, 8, -4, -6} };
    for (unsigned i = 0; i < sizeof c / sizeof c[0]; i++) {
        CHECK(U(CreateCaret)(a, NULL, c[i].w, c[i].h));
        CHECK(U(SetCaretPos)(c[i].x, c[i].y));
        GUITHREADINFO g;
        CHECK(fc_gti(GetCurrentThreadId(), &g));
        /* _GetGUIThreadInfo: left/top = queue caret position, right = left + width, bottom = top + height */
        CHECKF(g.rcCaret.left == c[i].x && g.rcCaret.top == c[i].y &&
               g.rcCaret.right == c[i].x + c[i].w && g.rcCaret.bottom == c[i].y + c[i].h,
               "case %u: rc=(%ld,%ld,%ld,%ld)", i, g.rcCaret.left, g.rcCaret.top, g.rcCaret.right, g.rcCaret.bottom);
        U(DestroyCaret)();
    }
    fc_destroy(a);
}

static DWORD_PTR thr_mkcaret(void *p) {
    HWND *a = (HWND *)p;
    a[1] = (HWND)(ULONG_PTR)U(SetFocus)(a[0]);
    a[2] = (HWND)(ULONG_PTR)U(CreateCaret)(a[0], NULL, 6, 9);
    U(SetCaretPos)(11, 13);
    U(ShowCaret)(a[0]);
    return 0;
}

TEST(fca_gti_other_thread_view) {
    FC_NEED();
    struct fc_thr t; if (!fc_thr_start(&t)) { FC_SKIP("worker thread failed"); return; }
    HWND mine = fc_top(); REQUIRE(mine);
    fc_quiesce();
    HWND args[3] = { t.w[0], 0, 0 };
    CHECK(fc_thr_call(&t, thr_mkcaret, args, NULL));
    CHECK(args[2] != NULL);
    GUITHREADINFO g;
    CHECK(fc_gti(t.tid, &g));
    EXPECT_EQ(g.hwndCaret, t.w[0]);
    EXPECT_EQ(g.flags & GUI_CARETBLINKING, GUI_CARETBLINKING);
    EXPECT_EQ(g.rcCaret.left, 11); EXPECT_EQ(g.rcCaret.top, 13);
    EXPECT_EQ(g.rcCaret.right, 17); EXPECT_EQ(g.rcCaret.bottom, 22);
    CHECK(g.hwndFocus == t.w[0] || g.hwndFocus == NULL);
    /* and the calling thread's own caret/focus are not the worker's */
    GUITHREADINFO me;
    CHECK(fc_gti(GetCurrentThreadId(), &me));
    EXPECT_EQ(me.hwndCaret, NULL);
    EXPECT_NE(me.hwndFocus, t.w[0]);
    fc_destroy(mine);
    fc_thr_stop(&t);
}

TEST(fca_gti_attached_threads_same_view) {
    FC_NEED();
    struct fc_thr t; if (!fc_thr_start(&t)) { FC_SKIP("worker thread failed"); return; }
    HWND mine = fc_top(); REQUIRE(mine);
    fc_quiesce();
    DWORD me = GetCurrentThreadId();
    if (!U(AttachThreadInput)(me, t.tid, TRUE)) { fc_destroy(mine); fc_thr_stop(&t); FC_SKIP("attach failed"); }
    U(SetFocus)(t.w[1]);
    GUITHREADINFO a, b;
    CHECK(fc_gti(me, &a));
    CHECK(fc_gti(t.tid, &b));
    EXPECT_EQ(a.hwndActive, b.hwndActive);   /* one queue: hwndActive/Focus/Capture come from the same tagQ */
    EXPECT_EQ(a.hwndFocus, b.hwndFocus);
    EXPECT_EQ(a.hwndCapture, b.hwndCapture);
    EXPECT_EQ(a.hwndCaret, b.hwndCaret);
    fc_detach_all(me, t.tid);
    fc_destroy(mine);
    fc_thr_stop(&t);
}

TEST(fca_gti_tid_zero_foreground) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    GUITHREADINFO g;
    SET_ERR(0);
    BOOL r = fc_gti(0, &g);
    HWND fg = U(GetForegroundWindow)();
    if (!r) {
        t_note("GetGUIThreadInfo(0): FALSE err=%lu (fg=%p)", GetLastError(), fg);
        CHECK(no_0xcc(&g, 0x48) == 0);
    } else {
        EXPECT_EQ(g.cbSize, 0x48);
        CHECK(gti_fully_written(0, NULL));
        if (fg) t_note("foreground queue active=%p fg=%p", g.hwndActive, fg);
        CHECK(g.hwndActive == NULL || U(IsWindow)(g.hwndActive));
    }
    fc_destroy(a);
}

TEST(fca_gti_move_size_and_menu_idle) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    GUITHREADINFO g;
    CHECK(fc_gti(GetCurrentThreadId(), &g));
    /* GUI_INMOVESIZE and the three menu-mode bits are driven by per-thread move-size and menu state, both unset when no modal loop runs. */
    EXPECT_EQ(g.flags & (GUI_INMOVESIZE | GUI_INMENUMODE | GUI_SYSTEMMENUMODE | GUI_POPUPMENUMODE), 0);
    EXPECT_EQ(g.hwndMoveSize, NULL);
    EXPECT_EQ(g.hwndMenuOwner, NULL);
    fc_destroy(a);
}
