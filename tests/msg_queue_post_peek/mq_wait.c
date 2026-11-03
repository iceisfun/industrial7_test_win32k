/* MsgWaitForMultipleObjectsEx / WaitMessage.
 *
 * win32kfull!NtUserMsgWaitForMultipleObjectsEx: flags & ~0x1f or nCount > 0x3f -> returns
 * 0xffffffff with ERROR_INVALID_PARAMETER. Before blocking it compares the wake mask with the
 * queue's change bits (and, with MWMO_INPUTAVAILABLE, the wake bits too); a hit returns nCount at
 * once (WAIT_OBJECT_0 + nCount), even if a handle is already signaled - except with MWMO_WAITALL
 * and nCount > 0, which skips that early return.
 */
#include "mq.h"

#define M1 (WM_USER + 0x231)
#define M2 (WM_USER + 0x232)

static HANDLE mkevent(BOOL signaled) { return CreateEventW(NULL, TRUE, signaled, NULL); }

/* ---- parameter validation ------------------------------------------------------------------- */

QTEST(msg_queue_post_peek_msgwait_invalid_flags) {
    static const DWORD bad[] = { 0x20, 0x40, 0x80, 0x100, 0x8000, 0x80000000, 0xFFFFFFE0, 0xFFFFFFFF };
    HANDLE e = mkevent(FALSE);
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(0);
        EQ(mq_wait(1, &e, 0, QS_ALLINPUT, bad[i]), (int)WAIT_FAILED);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
        if (UN(NtUserMsgWaitForMultipleObjectsEx)) {
            SetLastError(0);
            EQ(UN(NtUserMsgWaitForMultipleObjectsEx)(1, &e, 0, QS_ALLINPUT, bad[i]), WAIT_FAILED);
            EXPECT_ERR(ERROR_INVALID_PARAMETER);
        }
    }
    CloseHandle(e);
}

QTEST(msg_queue_post_peek_msgwait_too_many_handles) {
    HANDLE hs[80];
    for (int i = 0; i < 80; i++) hs[i] = mkevent(FALSE);
    static const DWORD counts[] = { 64, 65, 80 };    /* the message event takes one of the 64 slots */
    for (unsigned i = 0; i < sizeof counts / sizeof counts[0]; i++) {
        SetLastError(0);
        EQ(mq_wait(counts[i], hs, 0, QS_ALLINPUT, 0), (int)WAIT_FAILED);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
        SetLastError(0);
        EQ(mq_wait(counts[i], hs, 0, QS_ALLINPUT, MWMO_WAITALL), (int)WAIT_FAILED);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
    int r = mq_wait(63, hs, 0, QS_ALLINPUT, 0);       /* 63 + queue event is the legal maximum */
    EQ(r, (int)WAIT_TIMEOUT);
    for (int i = 0; i < 80; i++) CloseHandle(hs[i]);
}

QTEST(msg_queue_post_peek_msgwait_valid_flag_bits_accepted) {
    HANDLE e = mkevent(FALSE);
    static const DWORD ok[] = { 0, MWMO_ALERTABLE, MWMO_INPUTAVAILABLE, MWMO_ALERTABLE | MWMO_INPUTAVAILABLE, 0x8, 0x10, 0x18, 0x1E };
    for (unsigned i = 0; i < sizeof ok / sizeof ok[0]; i++) {
        SetLastError(0);
        int r = mq_wait(1, &e, 0, QS_ALLINPUT, ok[i]);
        CHECKF(r == (int)WAIT_TIMEOUT && GetLastError() == 0, "flags 0x%lx -> 0x%x err %lu", ok[i], r, GetLastError());
    }
    CloseHandle(e);
}

/* ---- timeouts and handle results ------------------------------------------------------------- */

QTEST(msg_queue_post_peek_msgwait_empty_queue_times_out) {
    EQ(mq_wait(0, NULL, 0, QS_ALLINPUT, 0), (int)WAIT_TIMEOUT);
    DWORD t0 = GetTickCount();
    EQ(mq_wait(0, NULL, 60, QS_ALLINPUT, 0), (int)WAIT_TIMEOUT);
    CHECKF(GetTickCount() - t0 < 3000, "took %lu ms", GetTickCount() - t0);
}

QTEST(msg_queue_post_peek_msgwait_signaled_handle_index) {
    HANDLE hs[3] = { mkevent(FALSE), mkevent(TRUE), mkevent(FALSE) };
    EQ(mq_wait(3, hs, 0, QS_ALLINPUT, 0), (int)WAIT_OBJECT_0 + 1);
    SetEvent(hs[0]);
    EQ(mq_wait(3, hs, 0, QS_ALLINPUT, 0), (int)WAIT_OBJECT_0);      /* lowest signaled index wins */
    for (int i = 0; i < 3; i++) CloseHandle(hs[i]);
}

QTEST(msg_queue_post_peek_msgwait_waitall_needs_all_handles_and_input) {
    /* MWMO_WAITALL waits for every handle AND the input event: with no input queued even a fully
     * signaled handle set does not satisfy the wait. */
    HWND h = mq_win(); QNEED(h);
    HANDLE hs[2] = { mkevent(TRUE), mkevent(FALSE) };
    EQ(mq_wait(2, hs, 0, QS_ALLINPUT, MWMO_WAITALL), (int)WAIT_TIMEOUT);
    SetEvent(hs[1]);
    EQ(mq_wait(2, hs, 0, QS_ALLINPUT, MWMO_WAITALL), (int)WAIT_TIMEOUT);
    CHECK(mq_post(h, M1, 0, 0));
    int r = mq_wait(2, hs, 0, QS_ALLINPUT, MWMO_WAITALL);
    if (r != (int)WAIT_OBJECT_0) t_note("all handles signaled + new input: result 0x%x", r);
    mq_drain();
    CloseHandle(hs[0]); CloseHandle(hs[1]);
    U(DestroyWindow)(h);
}

/* ---- message arrival ---------------------------------------------------------------------------- */

QTEST(msg_queue_post_peek_msgwait_pending_message_returns_ncount) {
    HWND h = mq_win(); QNEED(h);
    HANDLE hs[2] = { mkevent(FALSE), mkevent(FALSE) };
    for (DWORD n = 0; n <= 2; n++) {
        CHECK(mq_post(h, M1, 0, 0));
        EQ(mq_wait(n, hs, 0, QS_POSTMESSAGE, 0), (int)(WAIT_OBJECT_0 + n));
        mq_drain();
    }
    CloseHandle(hs[0]); CloseHandle(hs[1]);
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_msgwait_new_message_wins_over_signaled_handle) {
    /* The wake-mask test runs before the handle wait, so the message is reported first. */
    HWND h = mq_win(); QNEED(h);
    HANDLE e = mkevent(TRUE);
    CHECK(mq_post(h, M1, 0, 0));
    EQ(mq_wait(1, &e, 0, QS_ALLINPUT, 0), (int)WAIT_OBJECT_0 + 1);
    mq_drain();
    EQ(mq_wait(1, &e, 0, QS_ALLINPUT, 0), (int)WAIT_OBJECT_0);
    CloseHandle(e);
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_msgwait_waitall_skips_early_message_return) {
    HWND h = mq_win(); QNEED(h);
    HANDLE e = mkevent(FALSE);
    CHECK(mq_post(h, M1, 0, 0));
    int r = mq_wait(1, &e, 0, QS_ALLINPUT, MWMO_WAITALL);          /* handle is not signaled: all-wait cannot finish */
    EQ(r, (int)WAIT_TIMEOUT);
    mq_drain();
    CloseHandle(e);
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_msgwait_wake_mask_selects_queue_bits) {
    HWND h = mq_win(); QNEED(h);
    CHECK(mq_post(h, M1, 0, 0));
    EQ(mq_wait(0, NULL, 0, QS_PAINT, 0), (int)WAIT_TIMEOUT);
    EQ(mq_wait(0, NULL, 0, QS_SENDMESSAGE, 0), (int)WAIT_TIMEOUT);
    EQ(mq_wait(0, NULL, 0, QS_KEY | QS_MOUSEBUTTON, 0), (int)WAIT_TIMEOUT);
    EQ(mq_wait(0, NULL, 0, 0, 0), (int)WAIT_TIMEOUT);
    EQ(mq_wait(0, NULL, 0, QS_POSTMESSAGE, 0), (int)WAIT_OBJECT_0);
    mq_drain();
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_msgwait_seen_message_does_not_wake) {
    /* Once PeekMessage/GetQueueStatus looked at the queue, the message is no longer "new". */
    HWND h = mq_win(); QNEED(h);
    MSG m;
    CHECK(mq_post(h, M1, 0, 0));
    CHECK(mq_peek(&m, h, 0, 0, PM_NOREMOVE));
    EQ(mq_wait(0, NULL, 0, QS_ALLINPUT, 0), (int)WAIT_TIMEOUT);
    EQ(mq_wait(0, NULL, 0, QS_POSTMESSAGE, 0), (int)WAIT_TIMEOUT);
    mq_drain();
    CHECK(mq_post(h, M2, 0, 0));
    mq_status(QS_ALLINPUT);                              /* GetQueueStatus also resets the change bits */
    EQ(mq_wait(0, NULL, 0, QS_ALLINPUT, 0), (int)WAIT_TIMEOUT);
    mq_drain();
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_msgwait_inputavailable_sees_pending_message) {
    HWND h = mq_win(); QNEED(h);
    MSG m;
    CHECK(mq_post(h, M1, 0, 0));
    CHECK(mq_peek(&m, h, 0, 0, PM_NOREMOVE));
    EQ(mq_wait(0, NULL, 0, QS_POSTMESSAGE, MWMO_INPUTAVAILABLE), (int)WAIT_OBJECT_0);   /* wake bits (+6) are consulted */
    EQ(mq_wait(0, NULL, 0, QS_PAINT, MWMO_INPUTAVAILABLE), (int)WAIT_TIMEOUT);
    mq_drain();
    EQ(mq_wait(0, NULL, 0, QS_POSTMESSAGE, MWMO_INPUTAVAILABLE), (int)WAIT_TIMEOUT);    /* nothing pending any more */
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_msgwait_inputavailable_with_waitall_and_handles) {
    /* the early return is only taken when !WAITALL or nCount == 0 */
    HWND h = mq_win(); QNEED(h);
    HANDLE e = mkevent(TRUE);
    CHECK(mq_post(h, M1, 0, 0));
    int r = mq_wait(1, &e, 0, QS_POSTMESSAGE, MWMO_WAITALL | MWMO_INPUTAVAILABLE);
    CHECKF(r == (int)WAIT_OBJECT_0 || r == (int)WAIT_OBJECT_0 + 1, "r=0x%x", r);
    mq_drain();
    CloseHandle(e);
    U(DestroyWindow)(h);
}

struct sendst { HWND h; HANDLE d; };
static DWORD WINAPI sfn(LPVOID p) { struct sendst *x = p; Sleep(50); U(SendMessageW)(x->h, M1, 0, 0); SetEvent(x->d); return 0; }
QTEST(msg_queue_post_peek_msgwait_sent_message_is_processed_inside_wait) {
    /* The wait does NOT dispatch a sent message itself. With QS_SENDMESSAGE in the mask it ends as soon as one arrives
     * (index nCount) and the caller's next PeekMessage/GetMessage delivers it; with a mask that leaves QS_SENDMESSAGE out the
     * wait runs to its timeout and the sender stays blocked until the thread pumps. */
    HWND h = mq_win(); QNEED(h);
    struct sendst st = { h, CreateEventW(NULL, TRUE, FALSE, NULL) };
    HANDLE t = CreateThread(NULL, 0, sfn, &st, 0, NULL);
    int r = mq_wait(0, NULL, 400, QS_POSTMESSAGE, 0);
    EQ(r, WAIT_TIMEOUT);
    CHECK(WaitForSingleObject(st.d, 0) == WAIT_TIMEOUT);       /* nothing was dispatched inside the wait: the sender is still blocked */
    EQ(mq_count(M1), 0);
    MSG m; mq_peek(&m, NULL, 0, 0, PM_REMOVE);                 /* pumping delivers it */
    CHECK(WaitForSingleObject(st.d, 4000) == WAIT_OBJECT_0);
    EQ(mq_count(M1), 1);
    if (t) { WaitForSingleObject(t, 3000); CloseHandle(t); }
    CloseHandle(st.d);
    U(DestroyWindow)(h);
}
QTEST(msg_queue_post_peek_msgwait_send_mask_ends_wait_on_sent_message) {
    HWND h = mq_win(); QNEED(h);
    struct sendst st = { h, CreateEventW(NULL, TRUE, FALSE, NULL) };
    HANDLE t = CreateThread(NULL, 0, sfn, &st, 0, NULL);
    int r = mq_wait(0, NULL, 4000, QS_SENDMESSAGE, 0);
    EQ(r, WAIT_OBJECT_0);                                      /* index nCount (0): input of the masked kind is available */
    CHECK(WaitForSingleObject(st.d, 0) == WAIT_TIMEOUT);       /* still undelivered when the wait returns */
    MSG m; mq_peek(&m, NULL, 0, 0, PM_REMOVE);
    CHECK(WaitForSingleObject(st.d, 4000) == WAIT_OBJECT_0);
    EQ(mq_count(M1), 1);
    if (t) { WaitForSingleObject(t, 3000); CloseHandle(t); }
    CloseHandle(st.d);
    U(DestroyWindow)(h);
}

typedef struct { DWORD tid; HANDLE ready; int mode; int ret; DWORD err; volatile LONG apc; } wthr;
static void CALLBACK apc_fn(ULONG_PTR p) { InterlockedIncrement((LONG *)&((wthr *)p)->apc); }
static DWORD WINAPI wait_thread(LPVOID p) {
    wthr *w = p; MSG m;
    U(PeekMessageW)(&m, NULL, 0, 0, PM_NOREMOVE);
    w->tid = GetCurrentThreadId();
    SetEvent(w->ready);
    SetLastError(0);
    w->ret = mq_wait(0, NULL, 4000, QS_POSTMESSAGE, w->mode);
    w->err = GetLastError();
    return 0;
}

QTEST(msg_queue_post_peek_msgwait_alertable_returns_io_completion) {
    wthr w; memset(&w, 0, sizeof w); w.ready = CreateEventW(NULL, TRUE, FALSE, NULL); w.mode = MWMO_ALERTABLE;
    HANDLE t = CreateThread(NULL, 0, wait_thread, &w, 0, NULL);
    WaitForSingleObject(w.ready, 5000);
    Sleep(50);
    CHECK(QueueUserAPC(apc_fn, t, (ULONG_PTR)&w));
    WaitForSingleObject(t, 6000);
    EQ(w.ret, (int)WAIT_IO_COMPLETION);
    EQ(w.apc, 1);
    CloseHandle(t); CloseHandle(w.ready);
}

QTEST(msg_queue_post_peek_msgwait_nonalertable_ignores_apc) {
    wthr w; memset(&w, 0, sizeof w); w.ready = CreateEventW(NULL, TRUE, FALSE, NULL); w.mode = 0;
    HANDLE t = CreateThread(NULL, 0, wait_thread, &w, 0, NULL);
    WaitForSingleObject(w.ready, 5000);
    Sleep(50);
    QueueUserAPC(apc_fn, t, (ULONG_PTR)&w);
    Sleep(100);
    CHECK(WaitForSingleObject(t, 0) == WAIT_TIMEOUT);              /* still waiting: the APC did not end the wait */
    U(PostThreadMessageW)(w.tid, M1, 0, 0);
    WaitForSingleObject(t, 6000);
    EQ(w.ret, (int)WAIT_OBJECT_0);
    CloseHandle(t); CloseHandle(w.ready);
}

QTEST(msg_queue_post_peek_msgwait_cross_thread_post_wakes_wait) {
    wthr w; memset(&w, 0, sizeof w); w.ready = CreateEventW(NULL, TRUE, FALSE, NULL); w.mode = 0;
    HANDLE t = CreateThread(NULL, 0, wait_thread, &w, 0, NULL);
    WaitForSingleObject(w.ready, 5000);
    Sleep(50);
    DWORD t0 = GetTickCount();
    CHECK(U(PostThreadMessageW)(w.tid, M1, 0, 0));
    CHECK(WaitForSingleObject(t, 6000) == WAIT_OBJECT_0);
    CHECKF(GetTickCount() - t0 < 3500, "woke after %lu ms", GetTickCount() - t0);
    EQ(w.ret, (int)WAIT_OBJECT_0);
    CloseHandle(t); CloseHandle(w.ready);
}

/* ---- WaitMessage ---------------------------------------------------------------------------------- */

QTEST(msg_queue_post_peek_waitmessage_returns_for_unseen_message) {
    HWND h = mq_win(); QNEED(h);
    U(SetTimer)(NULL, 0x777, 3000, NULL);                          /* watchdog so a bug cannot hang the run */
    CHECK(mq_post(h, M1, 0, 0));
    DWORD t0 = GetTickCount();
    CHECK(U(WaitMessage)());
    CHECKF(GetTickCount() - t0 < 2000, "WaitMessage took %lu ms with a new message queued", GetTickCount() - t0);
    MSG m;
    CHECK(mq_peek(&m, h, M1, M1, PM_REMOVE));
    U(KillTimer)(NULL, 0x777);
    mq_drain();
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_waitmessage_blocks_on_seen_message_until_new_arrival) {
    HWND h = mq_win(); QNEED(h);
    MSG m;
    UINT_PTR wd = U(SetTimer)(NULL, 0, 500, NULL);                 /* watchdog fires after 500 ms */
    CHECK(mq_post(h, M1, 0, 0));
    CHECK(mq_peek(&m, h, M1, M1, PM_NOREMOVE));                    /* seen, not removed */
    DWORD t0 = GetTickCount();
    CHECK(U(WaitMessage)());
    DWORD dt = GetTickCount() - t0;
    CHECKF(dt >= 350, "WaitMessage returned after %lu ms although the only message was already seen", dt);
    CHECK(mq_peek(&m, NULL, WM_TIMER, WM_TIMER, PM_REMOVE));       /* what ended the wait was the timer */
    EQ(m.wParam, wd);
    U(KillTimer)(NULL, wd);
    mq_drain();
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_waitmessage_direct_returns_nonzero) {
    QNEED(UN(NtUserWaitMessage));
    HWND h = mq_win(); QNEED(h);
    U(SetTimer)(NULL, 0x778, 3000, NULL);
    CHECK(mq_post(h, M1, 0, 0));
    NE(UN(NtUserWaitMessage)(), 0);
    U(KillTimer)(NULL, 0x778);
    mq_drain();
    U(DestroyWindow)(h);
}

struct pst { DWORD tid; HWND h; };
static DWORD WINAPI pfn(LPVOID p) { struct pst *x = p; Sleep(100); U(PostMessageW)(x->h, M2, 1, 2); return 0; }
QTEST(msg_queue_post_peek_waitmessage_wakes_on_cross_thread_post) {
    HWND h = mq_win(); QNEED(h);
    struct pst st = { GetCurrentThreadId(), h };
    UINT_PTR wd = U(SetTimer)(NULL, 0, 4000, NULL);
    HANDLE t = CreateThread(NULL, 0, pfn, &st, 0, NULL);
    DWORD t0 = GetTickCount();
    CHECK(U(WaitMessage)());
    CHECKF(GetTickCount() - t0 < 3500, "waited %lu ms", GetTickCount() - t0);
    MSG m;
    CHECK(mq_peek(&m, h, M2, M2, PM_REMOVE));
    if (t) { WaitForSingleObject(t, 3000); CloseHandle(t); }
    U(KillTimer)(NULL, wd);
    mq_drain();
    U(DestroyWindow)(h);
}
