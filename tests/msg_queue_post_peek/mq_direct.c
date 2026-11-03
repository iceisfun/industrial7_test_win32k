/* Direct win32u syscall checks that complement the user32-routed tests: argument protocols of
 * NtUserGetMessage / NtUserPeekMessage / NtUserPostThreadMessage / NtUserGetThreadState.
 */
#include "mq.h"

#define M1 (WM_USER + 0x301)
#define M2 (WM_USER + 0x302)

QTEST(msg_queue_post_peek_direct_getmessage_retrieves_posted) {
    /* NtUserGetMessage: a queued message is returned without
     * blocking and copied out as a MSG with the padding dwords (+12, +44) zeroed. */
    QNEED(UN(NtUserGetMessage));
    HWND h = mq_win(); QNEED(h);
    MSG m, pk;
    CHECK(mq_post(h, M1, 0x11, 0x22));
    CHECK(mq_peek(&pk, h, M1, M1, PM_NOREMOVE));      /* proves the call below cannot block */
    memset(&m, 0xAA, sizeof m);
    LONG r = UN(NtUserGetMessage)(&m, h, M1, M1);
    CHECKF(r != 0 && r != -1, "r=%ld", r);
    CHECK(m.hwnd == h);
    EQ(m.message, M1);
    EQ(m.wParam, 0x11);
    EQ(m.lParam, 0x22);
    EQ(((DWORD *)&m)[3], 0);
    EQ(((DWORD *)&m)[11], 0);
    CHECK(!mq_peek(&pk, h, M1, M1, PM_NOREMOVE));     /* GetMessage removes */
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_direct_getmessage_wm_quit_returns_zero) {
    QNEED(UN(NtUserGetMessage)); QNEED(UN(NtUserPostThreadMessage));
    MSG m, pk;
    EQ(UN(NtUserPostThreadMessage)(GetCurrentThreadId(), WM_QUIT, 7, 0), 1);
    if (!mq_peek(&pk, NULL, 0, 0, PM_NOREMOVE)) {      /* never block in GetMessage */
        t_note("posted WM_QUIT not visible to a peek; GetMessage not issued");
        mq_drain();
        return;
    }
    memset(&m, 0, sizeof m);
    EQ(UN(NtUserGetMessage)(&m, NULL, 0, 0), 0);       /* WM_QUIT -> GetMessage returns FALSE */
    EQ(m.message, WM_QUIT);
    EQ(m.wParam, 7);
}

QTEST(msg_queue_post_peek_direct_peek_empty_queue_leaves_error_and_zero_return) {
    QNEED(UN(NtUserPeekMessage));
    MSG m;
    SetLastError(0x1234);
    EQ(UN(NtUserPeekMessage)(&m, NULL, 0, 0, PM_REMOVE, 0), 0);
    EQ(GetLastError(), 0x1234);        /* an empty queue is not an error */
}

QTEST(msg_queue_post_peek_direct_peek_range_with_zero_max_is_open_ended) {
    /* NtUserPeekMessage forwards max=0 as 0xffffffff to the message retrieval. */
    QNEED(UN(NtUserPeekMessage));
    HWND h = mq_win(); QNEED(h);
    MSG m;
    CHECK(mq_post(h, M1, 1, 0)); CHECK(mq_post(h, M2, 2, 0));
    NE(UN(NtUserPeekMessage)(&m, h, M2, 0, PM_REMOVE, 0), 0);
    EQ(m.message, M2);
    EQ(UN(NtUserPeekMessage)(&m, h, M2, 0, PM_REMOVE, 0), 0);
    NE(UN(NtUserPeekMessage)(&m, h, 0, 0, PM_REMOVE, 0), 0);
    EQ(m.message, M1);
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_direct_postthreadmessage_success_keeps_error) {
    QNEED(UN(NtUserPostThreadMessage));
    MSG m;
    SetLastError(0xBEEF);
    EQ(UN(NtUserPostThreadMessage)(GetCurrentThreadId(), M1, 5, 6), 1);
    SOFT_EQ(GetLastError(), 0xBEEF, "NtUserPostThreadMessage success path touched last error");
    CHECK(mq_peek(&m, NULL, M1, M1, PM_REMOVE));
    CHECK(m.hwnd == NULL);
    EQ(m.wParam, 5);
    EQ(m.lParam, 6);
}

QTEST(msg_queue_post_peek_direct_postmessage_hwnd_sentinels) {
    /* NtUserPostMessage treats 0, 0xffff and -1 as sentinels (thread / broadcast) without
 * ValidateHwnd; any other value below 0x10000 is also not validated. */
    QNEED(UN(NtUserPostMessage));
    MSG m;
    EQ(UN(NtUserPostMessage)(NULL, M1, 1, 2), 1);
    CHECK(mq_peek(&m, NULL, M1, M1, PM_REMOVE));
    CHECK(m.hwnd == NULL);
    mq_drain();
}

QTEST(msg_queue_post_peek_thread_state_foreground_selector_fresh_thread) {
    /* selector 0xa: current queue == gpqForeground. A fresh background thread owns its own queue. */
    QNEED(UN(NtUserGetThreadState));
    ULONG_PTR v = UN(NtUserGetThreadState)(0xA);
    CHECKF(v == 0 || v == 1, "0x%llx", (unsigned long long)v);
    if (v) t_note("fresh thread queue is the foreground queue");
}

QTEST(msg_queue_post_peek_thread_state_msgtime_signed_extension) {
    /* selector 9 returns (longlong)*(int*)(pti+0x238): sign extended; a DWORD tick below 2^31 matches. */
    QNEED(UN(NtUserGetThreadState));
    HWND h = mq_win(); QNEED(h);
    MSG m;
    CHECK(mq_post(h, M1, 0, 0));
    CHECK(mq_peek(&m, h, M1, M1, PM_REMOVE));
    ULONG_PTR t = UN(NtUserGetThreadState)(TS_MSGTIME);
    EQ((DWORD)t, m.time);
    EQ(t, (ULONG_PTR)(LONG_PTR)(LONG)m.time);
    U(DestroyWindow)(h);
}
