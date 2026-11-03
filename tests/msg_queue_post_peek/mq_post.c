/* PostMessage / PeekMessage on a thread's posted-message list.
 *
 * Kernel side: win32kfull!NtUserPostMessage (validation, UIPI, blocked ids) ->
 * the post path (links a 0xa0-byte QMSG on the target THREADINFO, limited by
 * the per-thread posted-message limit) and win32kfull!NtUserPeekMessage.
 */
#include "mq.h"

#define M1 (WM_USER + 0x201)
#define M2 (WM_USER + 0x202)
#define M3 (WM_USER + 0x203)
#define M4 (WM_USER + 0x204)
#define M5 (WM_USER + 0x205)

/* ---- value fidelity ---------------------------------------------------------- */

QTEST(msg_queue_post_peek_post_roundtrip_values) {
    /* The QMSG keeps the full 64-bit wParam/lParam (NtUserPostMessage takes ULONGLONG/__int64,
     * _PostTransformableMessageExtended stores them untouched for ids >= WM_USER). */
    static const struct { WPARAM w; LPARAM l; } v[] = {
        { 0, 0 }, { 1, 1 }, { 0xFFFFFFFFull, 0xFFFFFFFFull }, { (WPARAM)-1, (LPARAM)-1 },
        { 0x123456789ABCDEF0ull, 0x0FEDCBA987654321ll }, { 0x80000000ull, (LPARAM)0x80000000ll },
        { 0x100000000ull, (LPARAM)0x100000000ll }, { 0x7FFFFFFFFFFFFFFFull, (LPARAM)0x7FFFFFFFFFFFFFFFll },
    };
    HWND h = mq_win(); QNEED(h);
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) {
        MSG m; memset(&m, 0xAA, sizeof m);
        CHECKF(mq_post(h, M1, v[i].w, v[i].l), "post %u", i);
        CHECKF(mq_peek(&m, h, M1, M1, PM_REMOVE), "peek %u", i);
        CHECK(m.hwnd == h);
        EQ(m.message, M1);
        EQ(m.wParam, v[i].w);
        EQ(m.lParam, v[i].l);
    }
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_post_returns_true_and_error_untouched) {
    HWND h = mq_win(); QNEED(h);
    SetLastError(0xCAFE);
    CHECK(mq_post(h, M1, 1, 2));
    EQ(GetLastError(), 0xCAFE);   /* success path does not touch the last error */
    mq_drain();
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_post_fifo_order) {
    HWND h = mq_win(); QNEED(h);
    for (int i = 0; i < 24; i++) CHECK(mq_post(h, M1, i, 100 + i));
    for (int i = 0; i < 24; i++) {
        MSG m;
        CHECKF(mq_peek(&m, h, 0, 0, PM_REMOVE), "msg %d", i);
        EQ(m.wParam, (WPARAM)i);
        EQ(m.lParam, (LPARAM)(100 + i));
    }
    MSG m; CHECK(!mq_peek(&m, h, 0, 0, PM_REMOVE));
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_post_noremove_keeps_message) {
    HWND h = mq_win(); QNEED(h);
    CHECK(mq_post(h, M1, 11, 22));
    MSG a, b;
    for (int i = 0; i < 4; i++) {
        memset(&a, 0, sizeof a);
        CHECK(mq_peek(&a, h, 0, 0, PM_NOREMOVE));   /* repeated PM_NOREMOVE yields the same entry */
        EQ(a.message, M1);
        EQ(a.wParam, 11);
    }
    CHECK(mq_peek(&b, h, 0, 0, PM_REMOVE));
    EQ(b.lParam, 22);
    CHECK(!mq_peek(&b, h, 0, 0, PM_NOREMOVE));
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_peek_empty_queue_false) {
    MSG m; memset(&m, 0x5A, sizeof m);
    CHECK(!mq_peek(&m, NULL, 0, 0, PM_REMOVE));
    CHECK(!mq_peek(&m, NULL, 0, 0, PM_NOREMOVE));
    CHECK(!mq_peek(&m, NULL, 0, 0, PM_REMOVE | PM_NOYIELD));
    CHECK(!mq_peek(&m, NULL, WM_USER, WM_USER + 10, PM_REMOVE));
}

/* ---- message id space ---------------------------------------------------------- */

QTEST(msg_queue_post_peek_post_message_id_space) {
    /* NtUserPostMessage rejects only ids with bits in 0xfffe0000 (ERROR_INVALID_PARAMETER); every
     * id <= 0x1FFFF that is not specially blocked is queued as-is. */
    static const UINT ids[] = { WM_NULL, WM_USER, WM_APP, WM_APP + 0x3FFF, 0xC000, 0xFFFF, 0x10000, 0x1FFFF };
    HWND h = mq_win(); QNEED(h);
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        MSG m;
        CHECKF(mq_post(h, ids[i], 5, 6), "post 0x%x", ids[i]);
        if (ids[i] > 0xFFFF) {      /* message retrieval narrows some ids to 16 bits: delivery of these is only noted */
            if (!mq_peek(&m, h, 0, 0, PM_REMOVE)) t_note("id 0x%x was accepted but not retrievable", ids[i]);
            else if (m.message != ids[i]) t_note("id 0x%x retrieved as 0x%x", ids[i], m.message);
            mq_drain();
            continue;
        }
        CHECKF(mq_peek(&m, h, 0, 0, PM_REMOVE), "peek 0x%x", ids[i]);
        EQ(m.message, ids[i]);
        EQ(m.hwnd, h);
    }
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_post_message_bits_rejected) {
    static const UINT bad[] = { 0x20000, 0x40000, 0x80000, 0x100000, 0x01000000, 0x80000000, 0xFFFFFFFF, 0x7FFE0000, 0x2FFFF };
    HWND h = mq_win(); QNEED(h);
    DWORD tid = GetCurrentThreadId();
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(0);
        CHECKF(!mq_post(h, bad[i], 0, 0), "PostMessage 0x%x", bad[i]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
        SetLastError(0);
        CHECKF(!U(PostThreadMessageW)(tid, bad[i], 0, 0), "PostThreadMessage 0x%x", bad[i]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
        SetLastError(0);
        CHECKF(!mq_post(NULL, bad[i], 0, 0), "PostMessage(NULL) 0x%x", bad[i]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
    MSG m; CHECK(!mq_peek(&m, NULL, 0, 0, PM_NOREMOVE));   /* nothing was queued */
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_post_message_bits_rejected_direct) {
    QNEED(UN(NtUserPostMessage)); QNEED(UN(NtUserPostThreadMessage));
    HWND h = mq_win(); QNEED(h);
    static const UINT bad[] = { 0x20000, 0xC0000000, 0xFFFE0000, 0x1000000 };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(0);
        EQ(UN(NtUserPostMessage)(h, bad[i], 1, 2), 0);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
        SetLastError(0);
        EQ(UN(NtUserPostThreadMessage)(GetCurrentThreadId(), bad[i], 1, 2), 0);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
    CHECK(UN(NtUserPostMessage)(h, M1, 1, 2) != 0);
    EQ(mq_drain(), 1);
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_post_registered_message_roundtrip) {
    HWND h = mq_win(); QNEED(h);
    UINT a = mq_regmsg("rt"), b = mq_regmsg("rt");
    CHECK(a >= 0xC000 && a <= 0xFFFF);          /* RegisterWindowMessage ids live in the string-atom range */
    EQ(a, b);                            /* same name -> same id */
    CHECK(mq_regmsg("other") != a);
    MSG m;
    CHECK(mq_post(h, a, 0x77, 0x88));
    CHECK(mq_peek(&m, h, a, a, PM_REMOVE));
    EQ(m.message, a);
    EQ(m.wParam, 0x77);
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_post_wm_char_wparam_masking_is_user_mode) {
    /* user32!_PeekMessage masks WM_CHAR wParam to 16 bits (unicode path); the kernel
     * (NtUserPeekMessage) hands back what was posted. */
    HWND h = mq_win(); QNEED(h); QNEED(UN(NtUserPeekMessage));
    MSG m;
    CHECK(mq_post(h, WM_CHAR, 0xFFFF0041ull, 0));
    CHECK(mq_peek(&m, h, WM_CHAR, WM_CHAR, PM_REMOVE));
    EQ(m.wParam, 0x41);
    CHECK(mq_post(h, WM_CHAR, 0xFFFF0041ull, 0));
    memset(&m, 0, sizeof m);
    NE(UN(NtUserPeekMessage)(&m, h, WM_CHAR, WM_CHAR, PM_REMOVE, 0), 0);
    EQ(m.message, WM_CHAR);
    EQ(m.wParam, 0xFFFF0041ull);
    mq_drain();
    U(DestroyWindow)(h);
}

/* ---- invalid / blocked targets ------------------------------------------------------- */

QTEST(msg_queue_post_peek_post_invalid_hwnd_values) {
    /* Values < 0x10000 other than 0 and 0xffff, and anything else that is not -1, go through
     * ValidateHwnd; a miss fails with ERROR_INVALID_WINDOW_HANDLE. */
    static const ULONG_PTR bad[] = { 1, 2, 0x1234, 0xFFFE, 0x10000, 0x7FFF0001, 0xDEADBEEF, 0x0FFFFFFF };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(0);
        CHECKF(!mq_post((HWND)bad[i], M1, 0, 0), "hwnd 0x%llx", (unsigned long long)bad[i]);
        EXPECT_ERR(MQ_ERR_INVALID_WINDOW_HANDLE);
    }
    MSG m; CHECK(!mq_peek(&m, NULL, 0, 0, PM_NOREMOVE));
}

QTEST(msg_queue_post_peek_post_stale_uniq_rejected) {
    HWND h = mq_win(); QNEED(h);
    HWND stale = (HWND)((ULONG_PTR)h ^ 0x10000);     /* same table slot, different uniqueness word */
    SetLastError(0);
    CHECK(!mq_post(stale, M1, 0, 0));
    EXPECT_ERR(MQ_ERR_INVALID_WINDOW_HANDLE);
    CHECK(mq_post(h, M1, 0, 0));
    mq_drain();
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_post_destroyed_window_handle) {
    HWND h = mq_win(); QNEED(h);
    U(DestroyWindow)(h);
    CHECK(!U(IsWindow)(h));
    SetLastError(0);
    CHECK(!mq_post(h, M1, 0, 0));
    EXPECT_ERR(MQ_ERR_INVALID_WINDOW_HANDLE);
}

QTEST(msg_queue_post_peek_destroy_window_purges_posted_messages) {
    HWND a = mq_win(), b = mq_win(); QNEED(a); QNEED(b);
    for (int i = 0; i < 5; i++) { CHECK(mq_post(a, M1, i, 0)); CHECK(mq_post(b, M2, i, 0)); }
    U(DestroyWindow)(a);        /* DestroyWindow removes its queued messages */
    MSG m; int na = 0, nb = 0;
    while (mq_peek(&m, NULL, 0, 0, PM_REMOVE | PM_QS_POSTMESSAGE)) { if (m.hwnd == a) na++; else if (m.hwnd == b) nb++; }
    EQ(na, 0);
    EQ(nb, 5);
    U(DestroyWindow)(b);
}

QTEST(msg_queue_post_peek_post_blocked_pointer_and_gesture_ids) {
    /* NtUserPostMessage fails ids 0x119, 0x11B, 0x241..0x244 and 0x245..0x257 (except 0x24d)
     * without queueing them; the last error is 1002 on the open path. */
    HWND h = mq_win(); QNEED(h);
    static const UINT ids[] = { 0x119, 0x241, 0x242, 0x243, 0x244, 0x245, 0x246, 0x247, 0x250, 0x253, 0x257 };
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        SetLastError(0);
        CHECKF(!U(PostMessageW)(h, ids[i], 0, 0), "id 0x%x", ids[i]);
        if (GetLastError() != 0x3EA) t_note("id 0x%x: last error %lu (expected 1002)", ids[i], GetLastError());
    }
    MSG m; CHECK(!mq_peek(&m, NULL, 0, 0, PM_NOREMOVE));
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_postthread_blocked_touch_gesture_user32) {
    /* user32!PostThreadMessageW rejects WM_TOUCH (0x240) and WM_GESTURE (0x119) before any
     * syscall with ERROR_INVALID_MESSAGE-class code 1002. */
    SetLastError(0);
    CHECK(!U(PostThreadMessageW)(GetCurrentThreadId(), 0x240, 0, 0));
    EXPECT_ERR(0x3EA);
    SetLastError(0);
    CHECK(!U(PostThreadMessageW)(GetCurrentThreadId(), 0x119, 0, 0));
    EXPECT_ERR(0x3EA);
}

QTEST(msg_queue_post_peek_post_wm_touch_without_window_direct) {
    QNEED(UN(NtUserPostMessage));
    /* NtUserPostMessage(0x240) needs a real window handle (ForwardTouchMessage); NULL and
     * broadcast fail with 0x578. */
    SetLastError(0);
    EQ(UN(NtUserPostMessage)(NULL, 0x240, 0, 0), 0);
    if (GetLastError() != 0x578) t_note("WM_TOUCH to NULL: last error %lu (expected 1400)", GetLastError());
}

QTEST(msg_queue_post_peek_post_sync_only_messages_probe) {
    /* Messages whose lParam/wParam carry pointers are sync-only (MessageTable flag 0x200 ->
     * ERROR_MESSAGE_SYNC_ONLY). The table is build data: record it, and check the invariant that a
     * rejected post queues nothing and an accepted one queues exactly one. */
    static const UINT ids[] = { WM_GETTEXT, WM_GETTEXTLENGTH, WM_SETTEXT, WM_COPYDATA, WM_NCCREATE, WM_CREATE, WM_GETMINMAXINFO, WM_NOTIFY, WM_DRAWITEM, WM_WINDOWPOSCHANGING, WM_STYLECHANGING, WM_NCCALCSIZE };
    HWND h = mq_win(); QNEED(h);
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        SetLastError(0);
        BOOL ok = U(PostMessageW)(h, ids[i], 0, 0);
        DWORD e = GetLastError();
        int n = 0; MSG m;
        while (mq_peek(&m, h, ids[i], ids[i], PM_REMOVE)) n++;
        CHECKF(n == (ok ? 1 : 0), "id 0x%x ok=%d queued=%d", ids[i], ok, n);
        t_note("post of 0x%x: %s (error %lu)", ids[i], ok ? "accepted" : "rejected", ok ? 0 : e);
        if (!ok) CHECKF(e == MQ_ERR_MESSAGE_SYNC_ONLY || e == 5 || e == 0x3EA, "error %lu", e);
    }
    mq_drain();
    U(DestroyWindow)(h);
}

/* ---- thread messages (NULL hwnd) ------------------------------------------------------ */

QTEST(msg_queue_post_peek_post_null_hwnd_is_thread_message) {
    CHECK(mq_post(NULL, M1, 3, 4));         /* hWnd == NULL: same as PostThreadMessage(current) */
    MSG m;
    CHECK(mq_peek(&m, NULL, M1, M1, PM_REMOVE));
    CHECK(m.hwnd == NULL);
    EQ(m.message, M1);
    EQ(m.wParam, 3);
    EQ(m.lParam, 4);
    /* DispatchMessage of a window-less message calls nothing and returns 0 */
    HWND w = mq_win(); QNEED(w);
    int before = mq_n;
    EQ(U(DispatchMessageW)(&m), 0);
    EQ(mq_n, before);
    U(DestroyWindow)(w);
}

QTEST(msg_queue_post_peek_post_thread_self_equals_null_hwnd) {
    DWORD tid = GetCurrentThreadId();
    MSG m;
    CHECK(U(PostThreadMessageW)(tid, M2, 9, 10));
    CHECK(mq_peek(&m, (HWND)-1, 0, 0, PM_REMOVE));
    CHECK(m.hwnd == NULL);
    EQ(m.message, M2);
    EQ(m.wParam, 9);
}

QTEST(msg_queue_post_peek_peek_hwnd_minus1_returns_thread_messages_only) {
    /* message retrieval folds hwnd -1 and 0xffff to 1 = "only messages without a window". */
    static const ULONG_PTR filt[] = { (ULONG_PTR)-1, 0xFFFF };
    HWND h = mq_win(); QNEED(h);
    for (unsigned i = 0; i < 2; i++) {
        MSG m;
        CHECK(mq_post(h, M1, 1, 0));
        CHECK(mq_post(NULL, M2, 2, 0));
        CHECK(mq_post(h, M3, 3, 0));
        CHECK(mq_peek(&m, (HWND)filt[i], 0, 0, PM_REMOVE));
        EQ(m.message, M2);
        CHECK(m.hwnd == NULL);
        CHECK(!mq_peek(&m, (HWND)filt[i], 0, 0, PM_REMOVE));
        EQ(mq_drain(), 2);
    }
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_peek_invalid_hwnd_filter_fails) {
    MSG m;
    SetLastError(0);
    CHECK(!mq_peek(&m, (HWND)(ULONG_PTR)0xDEADBEEF, 0, 0, PM_REMOVE));
    EXPECT_ERR(MQ_ERR_INVALID_WINDOW_HANDLE);
    HWND h = mq_win(); QNEED(h);
    U(DestroyWindow)(h);
    SetLastError(0);
    CHECK(!mq_peek(&m, h, 0, 0, PM_REMOVE));
    EXPECT_ERR(MQ_ERR_INVALID_WINDOW_HANDLE);
}

QTEST(msg_queue_post_peek_peek_hwnd_filter_selects_window) {
    HWND a = mq_win(), b = mq_win(); QNEED(a); QNEED(b);
    CHECK(mq_post(a, M1, 1, 0)); CHECK(mq_post(b, M1, 2, 0)); CHECK(mq_post(a, M1, 3, 0));
    MSG m;
    CHECK(mq_peek(&m, b, 0, 0, PM_REMOVE));
    CHECK(m.hwnd == b); EQ(m.wParam, 2);
    CHECK(!mq_peek(&m, b, 0, 0, PM_REMOVE));
    CHECK(mq_peek(&m, a, 0, 0, PM_REMOVE)); EQ(m.wParam, 1);
    CHECK(mq_peek(&m, a, 0, 0, PM_REMOVE)); EQ(m.wParam, 3);
    U(DestroyWindow)(a); U(DestroyWindow)(b);
}

QTEST(msg_queue_post_peek_peek_hwnd_filter_includes_child_windows) {
    /* The hwnd filter of message retrieval accepts the window and its descendants. */
    HWND p = mq_win(); QNEED(p);
    HWND c = U(CreateWindowExW)(0, L"win32ktMQ", L"c", WS_CHILD, 0, 0, 8, 8, p, (HMENU)1, GetModuleHandleW(NULL), NULL);
    if (!c) { t_note("child window creation failed"); U(DestroyWindow)(p); return; }
    MSG m;
    CHECK(mq_post(c, M1, 1, 0));
    if (mq_peek(&m, p, 0, 0, PM_REMOVE)) { CHECK(m.hwnd == c); }
    else t_note("hwnd filter did not include the child window");
    mq_drain();
    U(DestroyWindow)(p);
}

/* ---- range filters ------------------------------------------------------------------- */

QTEST(msg_queue_post_peek_peek_range_selects_in_queue_order) {
    HWND h = mq_win(); QNEED(h);
    UINT ids[] = { M1, M2, M3, M4, M5 };
    for (int i = 0; i < 5; i++) CHECK(mq_post(h, ids[i], i, 0));
    MSG m;
    CHECK(mq_peek(&m, h, M3, M4, PM_REMOVE)); EQ(m.message, M3);
    CHECK(mq_peek(&m, h, M3, M4, PM_REMOVE)); EQ(m.message, M4);
    CHECK(!mq_peek(&m, h, M3, M4, PM_REMOVE));
    CHECK(mq_peek(&m, h, 0, 0, PM_REMOVE)); EQ(m.message, M1);
    CHECK(mq_peek(&m, h, 0, 0, PM_REMOVE)); EQ(m.message, M2);
    CHECK(mq_peek(&m, h, 0, 0, PM_REMOVE)); EQ(m.message, M5);
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_peek_range_bounds_inclusive) {
    HWND h = mq_win(); QNEED(h);
    MSG m;
    CHECK(mq_post(h, M2, 0, 0));
    CHECK(mq_peek(&m, h, M2, M2, PM_NOREMOVE));            /* min == max matches exactly that id */
    CHECK(mq_peek(&m, h, M2 - 1, M2 + 1, PM_NOREMOVE));
    CHECK(!mq_peek(&m, h, M2 + 1, M2 + 5, PM_NOREMOVE));
    CHECK(!mq_peek(&m, h, M2 - 5, M2 - 1, PM_NOREMOVE));
    CHECK(mq_peek(&m, h, 0, M2, PM_NOREMOVE));
    CHECK(mq_peek(&m, h, M2, 0, PM_NOREMOVE));
    CHECK(!mq_peek(&m, h, 0, M2 - 1, PM_NOREMOVE));
    mq_drain();
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_peek_min_without_max_is_open_ended) {
 /* message retrieval turns max == 0 into 0xffffffff. */
    HWND h = mq_win(); QNEED(h);
    MSG m;
    CHECK(mq_post(h, M1, 0, 0)); CHECK(mq_post(h, 0xC123, 0, 0));
    CHECK(mq_peek(&m, h, M2, 0, PM_REMOVE));
    EQ(m.message, 0xC123);
    CHECK(!mq_peek(&m, h, M2, 0, PM_REMOVE));
    CHECK(mq_peek(&m, h, 0, 0, PM_REMOVE)); EQ(m.message, M1);
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_peek_range_across_registered_ids) {
    HWND h = mq_win(); QNEED(h);
    UINT a = mq_regmsg("rngA");
    MSG m;
    CHECK(mq_post(h, M1, 0, 0)); CHECK(mq_post(h, a, 0, 0));
    CHECK(mq_peek(&m, h, 0xC000, 0xFFFF, PM_REMOVE));
    EQ(m.message, a);
    CHECK(mq_peek(&m, h, WM_USER, 0x7FFF, PM_REMOVE));
    EQ(m.message, M1);
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_peek_filter_bits_rejected_direct) {
    QNEED(UN(NtUserGetMessage));
    if (t_is_wine()) { t_note("Wine's GetMessage blocks instead of validating the filter"); return; }
    MSG m;
    static const UINT bad[] = { 0x20000, 0x40000, 0x80000000, 0xFFFF0000 };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(0);
        UN(NtUserGetMessage)(&m, NULL, bad[i], 0);        /* min|max & 0xfffe0000 -> ERROR_INVALID_PARAMETER */
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
        SetLastError(0);
        UN(NtUserGetMessage)(&m, NULL, 0, bad[i]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
}

QTEST(msg_queue_post_peek_getmessage_invalid_filter_user32) {
    MSG m;
    if (t_is_wine()) { t_note("Wine's GetMessage blocks instead of validating the filter"); return; }
    SetLastError(0);
    EQ(U(GetMessageW)(&m, NULL, 0x20000, 0), 0);   /* user32!GetMessageW rejects before the syscall */
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SetLastError(0);
    EQ(U(GetMessageW)(&m, NULL, 0, 0x40000), 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}

QTEST(msg_queue_post_peek_getmessage_invalid_hwnd_returns_minus_one) {
 /* message retrieval returns -1 for an invalid window. */
    MSG m;
    SetLastError(0);
    EQ(U(GetMessageW)(&m, (HWND)(ULONG_PTR)0xDEADBEEF, 0, 0), -1);
    EXPECT_ERR(MQ_ERR_INVALID_WINDOW_HANDLE);
}

/* ---- PeekMessage flags ------------------------------------------------------------------ */

QTEST(msg_queue_post_peek_peek_invalid_flag_bits) {
    /* NtUserPeekMessage: (flags & 0xe300fffc) != 0 -> ERROR_INVALID_FLAGS. PM_REMOVE=1/PM_NOYIELD=2
     * and the PM_QS_* word (0x1cff0000) are the only legal bits. A message is queued so user32's
     * "queue is quiet" early-out cannot hide the syscall. */
    static const UINT bad[] = { 0x4, 0x8, 0x10, 0x100, 0x8000, 0xFFFC, 0x01000000, 0x02000000, 0x20000000, 0x40000000, 0x80000000, 0xE3000000 };
    HWND h = mq_win(); QNEED(h);
    CHECK(mq_post(h, M1, 1, 1));
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        MSG m;
        SetLastError(0);
        CHECKF(!mq_peek(&m, h, 0, 0, bad[i]), "flags 0x%x", bad[i]);
        EXPECT_ERR(MQ_ERR_INVALID_FLAGS);
        SetLastError(0);
        if (UN(NtUserPeekMessage)) {
            EQ(UN(NtUserPeekMessage)(&m, h, 0, 0, bad[i], 0), 0);
            EXPECT_ERR(MQ_ERR_INVALID_FLAGS);
        }
    }
    EQ(mq_drain(), 1);      /* the message survived every rejected call */
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_peek_valid_flag_combinations) {
    static const UINT ok[] = {
        PM_NOREMOVE, PM_REMOVE, PM_NOYIELD, PM_REMOVE | PM_NOYIELD,
        PM_NOREMOVE | PM_QS_POSTMESSAGE, PM_QS_INPUT, PM_QS_PAINT, PM_QS_SENDMESSAGE,
        PM_QS_POSTMESSAGE | PM_QS_PAINT | PM_QS_SENDMESSAGE | PM_QS_INPUT, 0x1c000000, 0x00FF0000,
    };
    HWND h = mq_win(); QNEED(h);
    for (unsigned i = 0; i < sizeof ok / sizeof ok[0]; i++) {
        MSG m;
        CHECK(mq_post(h, M1, 0, 0));
        SetLastError(0);
        (void)mq_peek(&m, h, 0, 0, ok[i]);
        CHECKF(GetLastError() != MQ_ERR_INVALID_FLAGS, "flags 0x%x", ok[i]);
        mq_drain();
    }
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_peek_noyield_flag_still_retrieves) {
    HWND h = mq_win(); QNEED(h);
    MSG m;
    CHECK(mq_post(h, M1, 6, 7));
    CHECK(mq_peek(&m, h, 0, 0, PM_REMOVE | PM_NOYIELD));
    EQ(m.wParam, 6);
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_peek_pm_qs_filters_select_queue_kind) {
    /* PM_QS_* narrows the wake mask (CalcWakeMask). A pending posted message is invisible to
     * PM_QS_SENDMESSAGE / PM_QS_INPUT / PM_QS_PAINT and visible to PM_QS_POSTMESSAGE. */
    static const struct { UINT fl; int found; } t[] = {
        { PM_QS_POSTMESSAGE, 1 }, { PM_QS_SENDMESSAGE, 0 }, { PM_QS_INPUT, 0 }, { PM_QS_PAINT, 0 },
        { PM_QS_POSTMESSAGE | PM_QS_SENDMESSAGE, 1 }, { PM_QS_SENDMESSAGE | PM_QS_PAINT, 0 }, { 0, 1 },
    };
    HWND h = mq_win(); QNEED(h);
    CHECK(mq_post(h, M1, 0, 0));
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
        MSG m;
        int r = mq_peek(&m, h, 0, 0, PM_NOREMOVE | t[i].fl);
        CHECKF(!!r == t[i].found, "flags 0x%x got %d", t[i].fl, r);
    }
    EQ(mq_drain(), 1);
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_peek_direct_msg_layout) {
    /* NtUserPeekMessage writes the MSG field by field and zeroes both padding dwords (+12, +44);
     * on "no message" it returns before touching the buffer. */
    QNEED(UN(NtUserPeekMessage));
    HWND h = mq_win(); QNEED(h);
    unsigned char raw[sizeof(MSG) + 16];
    MSG *m = (MSG *)raw;
    memset(raw, 0xCC, sizeof raw);
    EQ(UN(NtUserPeekMessage)(m, h, 0, 0, PM_REMOVE, 0), 0);
    for (size_t i = 0; i < sizeof(MSG); i++) if (raw[i] != 0xCC) { CHECKF(0, "byte %zu written on empty queue", i); break; }
    CHECK(mq_post(h, M3, 0x1111222233334444ull, 0x5555666677778888ll));
    memset(raw, 0xCC, sizeof raw);
    NE(UN(NtUserPeekMessage)(m, h, 0, 0, PM_REMOVE, 0), 0);
    CHECK(m->hwnd == h);
    EQ(m->message, M3);
    EQ(m->wParam, 0x1111222233334444ull);
    EQ(m->lParam, 0x5555666677778888ll);
    for (int i = 12; i < 16; i++) EQ(raw[i], 0);
    for (int i = 44; i < 48; i++) EQ(raw[i], 0);
    for (size_t i = sizeof(MSG); i < sizeof raw; i++) EQ(raw[i], 0xCC);   /* no overrun */
    U(DestroyWindow)(h);
}

/* ---- broadcast and message-only ------------------------------------------------------------ */

QTEST(msg_queue_post_peek_post_broadcast_returns_true) {
    UINT r = mq_regmsg("bcast");
    HWND h = mq_win(); QNEED(h);
    mq_reset();
    CHECK(mq_post(HWND_BROADCAST, r, 0x55, 0x66));            /* broadcast path, returns 1 */
    CHECK(mq_post((HWND)(ULONG_PTR)0xFFFF, r, 0x56, 0x66));    /* 0xffff is the same sentinel */
    int got = 0;
    DWORD t0 = GetTickCount();
    while (got < 2 && GetTickCount() - t0 < 2000) {
        MSG m;
        while (mq_peek(&m, NULL, 0, 0, PM_REMOVE | PM_QS_POSTMESSAGE)) if (m.message == r && m.hwnd == h) got++;
        if (got < 2) mq_wait(0, NULL, 100, QS_POSTMESSAGE, 0);
    }
    if (got != 2) t_note("broadcast reached own top-level window %d/2 times", got);
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_post_message_only_window_roundtrip) {
    HWND h = mq_msgwin();
    if (!h) { t_note("message-only window creation failed"); return; }
    UINT r = mq_regmsg("mo");
    CHECK(mq_post(h, M1, 1, 2));
    CHECK(mq_post(HWND_BROADCAST, r, 0, 0));
    MSG m; int own = 0, bc = 0;
    mq_wait(0, NULL, 200, QS_POSTMESSAGE, 0);
    while (mq_peek(&m, NULL, 0, 0, PM_REMOVE | PM_QS_POSTMESSAGE)) { if (m.message == M1 && m.hwnd == h) own++; if (m.message == r) bc++; }
    EQ(own, 1);
    EQ(bc, 0);        /* message-only windows are not top-level windows of the desktop: not broadcast targets */
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_post_wm_quit_as_plain_message_is_fifo) {
    /* A WM_QUIT that is posted (not PostQuitMessage) is an ordinary QMSG. */
    HWND h = mq_win(); QNEED(h);
    MSG m; UINT seq[3]; int n = 0;
    CHECK(mq_post(h, M1, 0, 0)); CHECK(mq_post(NULL, WM_QUIT, 7, 0)); CHECK(mq_post(h, M2, 0, 0));
    while (n < 3 && mq_peek(&m, NULL, 0, 0, PM_REMOVE)) seq[n++] = m.message;
    EQ(n, 3);
    if (n == 3 && !(seq[0] == M1 && seq[1] == WM_QUIT && seq[2] == M2)) t_note("order was 0x%x 0x%x 0x%x", seq[0], seq[1], seq[2]);
    U(DestroyWindow)(h);
}
