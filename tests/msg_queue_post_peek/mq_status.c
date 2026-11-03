/* GetQueueStatus / GetInputState / NtUserGetThreadState queue bits, timers, paint, sent messages.
 *
 * win32kfull!NtUserGetQueueStatus: flags &= 0x5dff; THREADINFO->pq-style queue state keeps
 * changebits (+4: "new since last check") and wakebits (+6: "currently pending"); the result is
 * MAKELONG(changebits & flags, (wakebits | wakebitsHung) & flags)... in the DWORD returned by
 * high word and low word combined: low word = changebits, HIGH word = wakebits. The requested bits of changebits
 * are cleared.  (MSDN: high word = types currently in the queue, low word = new since the last
 * call to GetQueueStatus/GetMessage/PeekMessage.)
 */
#include "mq.h"

#define M1 (WM_USER + 0x211)
#define M2 (WM_USER + 0x212)
#define LO(x) ((x) & 0xFFFF)
#define HI(x) (((x) >> 16) & 0xFFFF)

typedef UINT_PTR (WINAPI *SetTimerFn)(HWND, UINT_PTR, UINT, TIMERPROC);

/* ---- GetQueueStatus words ------------------------------------------------------------ */

QTEST(msg_queue_post_peek_qs_fresh_queue_has_no_posted_bits) {
    DWORD s = mq_status(QS_POSTMESSAGE | QS_ALLPOSTMESSAGE | QS_SENDMESSAGE | QS_TIMER);
    EQ(s, 0);
}

QTEST(msg_queue_post_peek_qs_post_sets_new_and_pending_bits) {
    HWND h = mq_win(); QNEED(h);
    CHECK(mq_post(h, M1, 0, 0));
    DWORD s = mq_status(QS_POSTMESSAGE);
    CHECKF(LO(s) & QS_POSTMESSAGE, "low (new) word 0x%lx", LO(s));
    CHECKF(HI(s) & QS_POSTMESSAGE, "high (pending) word 0x%lx", HI(s));
    s = mq_status(QS_POSTMESSAGE);        /* the first call cleared the change bit only */
    EQ(LO(s) & QS_POSTMESSAGE, 0);
    CHECKF(HI(s) & QS_POSTMESSAGE, "high word after second call 0x%lx", HI(s));
    mq_drain();
    s = mq_status(QS_POSTMESSAGE);
    EQ(s, 0);
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_qs_allpostmessage_bit_follows_posting) {
    HWND h = mq_win(); QNEED(h);
    CHECK(mq_post(h, M1, 0, 0));
    DWORD s = mq_status(QS_ALLPOSTMESSAGE);
    if (!(HI(s) & QS_ALLPOSTMESSAGE)) t_note("QS_ALLPOSTMESSAGE not pending after post: 0x%lx", s);
    mq_drain();
    s = mq_status(QS_ALLPOSTMESSAGE);
    EQ(s, 0);                      /* queue empty -> nothing pending, nothing new */
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_qs_result_is_masked_by_flags) {
    HWND h = mq_win(); QNEED(h);
    CHECK(mq_post(h, M1, 0, 0));
    EQ(mq_status(QS_KEY), 0);                    /* result &= requested flags */
    EQ(mq_status(QS_PAINT | QS_TIMER), 0);
    DWORD s = mq_status(QS_POSTMESSAGE | QS_KEY);       /* the unrelated calls above cleared nothing of QS_POSTMESSAGE */
    CHECKF(LO(s) & QS_POSTMESSAGE, "new word 0x%lx", LO(s));
    EQ(LO(s) & QS_KEY, 0);
    mq_drain();
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_qs_flags_zero_returns_zero_and_clears_nothing) {
    HWND h = mq_win(); QNEED(h);
    CHECK(mq_post(h, M1, 0, 0));
    EQ(mq_status(0), 0);
    DWORD s = mq_status(QS_POSTMESSAGE);
    CHECKF(LO(s) & QS_POSTMESSAGE, "0x%lx", s);
    mq_drain();
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_qs_noremove_peek_clears_new_keeps_pending) {
    HWND h = mq_win(); QNEED(h);
    MSG m;
    CHECK(mq_post(h, M1, 0, 0));
    CHECK(mq_peek(&m, h, 0, 0, PM_NOREMOVE));
    DWORD s = mq_status(QS_POSTMESSAGE);
    EQ(LO(s) & QS_POSTMESSAGE, 0);               /* seen by PeekMessage: no longer "new" */
    CHECKF(HI(s) & QS_POSTMESSAGE, "0x%lx", s);          /* but still pending */
    mq_drain();
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_qs_remove_of_last_message_clears_pending) {
    HWND h = mq_win(); QNEED(h);
    MSG m;
    CHECK(mq_post(h, M1, 0, 0)); CHECK(mq_post(h, M2, 0, 0));
    CHECK(mq_peek(&m, h, 0, 0, PM_REMOVE));
    DWORD s = mq_status(QS_POSTMESSAGE);
    CHECKF(HI(s) & QS_POSTMESSAGE, "one message still queued: 0x%lx", s);
    CHECK(mq_peek(&m, h, 0, 0, PM_REMOVE));
    s = mq_status(QS_POSTMESSAGE);
    EQ(HI(s) & QS_POSTMESSAGE, 0);
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_qs_second_post_after_peek_is_new_again) {
    HWND h = mq_win(); QNEED(h);
    MSG m;
    CHECK(mq_post(h, M1, 0, 0));
    CHECK(mq_peek(&m, h, 0, 0, PM_NOREMOVE));
    CHECK(mq_post(h, M2, 0, 0));
    DWORD s = mq_status(QS_POSTMESSAGE);
    CHECKF(LO(s) & QS_POSTMESSAGE, "0x%lx", s);          /* a new arrival sets the change bit again */
    mq_drain();
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_qs_changebits_via_thread_state_not_cleared) {
    /* NtUserGetThreadState(6) returns the raw changebits word (queue+4) without clearing it. */
    QNEED(UN(NtUserGetThreadState));
    HWND h = mq_win(); QNEED(h);
    CHECK(mq_post(h, M1, 0, 0));
    for (int i = 0; i < 3; i++) CHECK(UN(NtUserGetThreadState)(TS_CHANGEBITS) & QS_POSTMESSAGE);
    mq_status(QS_POSTMESSAGE);
    EQ(UN(NtUserGetThreadState)(TS_CHANGEBITS) & QS_POSTMESSAGE, 0);
    mq_drain();
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_qs_invalid_flag_bits_user32) {
    /* user32!GetQueueStatus: (flags & 0xffffa200) -> ERROR_INVALID_FLAGS, result 0. */
    static const UINT bad[] = { 0x200, 0x2000, 0x8000, 0x10000, 0x20000, 0x80000000, 0xFFFFFFFF, 0xA200 };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(0);
        EQ(mq_status(bad[i]), 0);
        EXPECT_ERR(MQ_ERR_INVALID_FLAGS);
    }
}

QTEST(msg_queue_post_peek_qs_valid_flag_bits_user32) {
    static const UINT ok[] = { 0, QS_KEY, QS_MOUSEMOVE, QS_MOUSEBUTTON, QS_POSTMESSAGE, QS_TIMER, QS_PAINT, QS_SENDMESSAGE, QS_HOTKEY, QS_ALLPOSTMESSAGE, QS_RAWINPUT, QS_TOUCH, QS_POINTER, 0x4000, QS_ALLINPUT, QS_INPUT, 0x5DFF };
    for (unsigned i = 0; i < sizeof ok / sizeof ok[0]; i++) {
        SetLastError(0);
        (void)mq_status(ok[i]);
        CHECKF(GetLastError() == 0, "flags 0x%x error %lu", ok[i], GetLastError());
    }
}

QTEST(msg_queue_post_peek_qs_direct_masks_flags_silently) {
    /* The kernel masks with 0x5dff and never fails; both result words stay inside that mask. */
    QNEED(UN(NtUserGetQueueStatus));
    HWND h = mq_win(); QNEED(h);
    CHECK(mq_post(h, M1, 0, 0));
    SetLastError(0);
    DWORD s = UN(NtUserGetQueueStatus)(0xFFFF);
    CHECKF((LO(s) & ~0x5DFF) == 0 && (HI(s) & ~0x5DFF) == 0, "0x%lx", s);
    CHECK(HI(s) & QS_POSTMESSAGE);
    EQ(GetLastError(), 0);
    EQ(UN(NtUserGetQueueStatus)(0x8000), 0);
    EQ(UN(NtUserGetQueueStatus)(0x2200), 0);
    s = UN(NtUserGetQueueStatus)(0x0200 | QS_POSTMESSAGE);
    CHECKF(HI(s) & QS_POSTMESSAGE, "0x%lx", s);
    EQ(HI(s) & 0x200, 0);
    mq_drain();
    U(DestroyWindow)(h);
}

/* ---- GetInputState --------------------------------------------------------------------- */

QTEST(msg_queue_post_peek_getinputstate_false_without_input) {
    HWND h = mq_win(); QNEED(h);
    EQ(U(GetInputState)(), 0);
    CHECK(mq_post(h, M1, 0, 0));
    EQ(U(GetInputState)(), 0);       /* posted messages are not input (QS_KEY|QS_MOUSEBUTTON only) */
    if (UN(NtUserGetThreadState)) EQ(UN(NtUserGetThreadState)(TS_INPUTSTATE), 0);
    mq_drain();
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_getinputstate_posted_key_is_not_input) {
    /* PostMessage(WM_KEYDOWN) is a posted message: it does not set QS_KEY. */
    HWND h = mq_win(); QNEED(h);
    CHECK(mq_post(h, WM_KEYDOWN, 'A', 0));
    EQ(U(GetInputState)(), 0);
    EQ(mq_status(QS_KEY) & QS_KEY, 0);
    mq_drain();
    U(DestroyWindow)(h);
}

/* ---- timers ---------------------------------------------------------------------------- */

static int g_tp_hits; static HWND g_tp_hwnd; static UINT g_tp_msg; static UINT_PTR g_tp_id; static DWORD g_tp_tick;
static void CALLBACK tproc(HWND h, UINT m, UINT_PTR id, DWORD t) { g_tp_hits++; g_tp_hwnd = h; g_tp_msg = m; g_tp_id = id; g_tp_tick = t; }

QTEST(msg_queue_post_peek_timer_sets_qs_timer_and_posts_wm_timer) {
    HWND h = mq_win(); QNEED(h);
    UINT_PTR id = U(SetTimer)(h, 7, 10, NULL);
    EQ(id, 7);
    int w = mq_wait(0, NULL, 3000, QS_TIMER, 0);
    EQ(w, WAIT_OBJECT_0);                         /* nCount = 0: the message event is index 0 */
    DWORD s = mq_status(QS_TIMER);
    CHECKF(HI(s) & QS_TIMER, "0x%lx", s);
    MSG m;
    CHECK(mq_peek(&m, h, WM_TIMER, WM_TIMER, PM_REMOVE));
    EQ(m.message, WM_TIMER);
    EQ(m.wParam, 7);
    CHECK(m.hwnd == h);
    CHECK(U(KillTimer)(h, 7));
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_timer_null_hwnd_id_matches_wparam) {
    UINT_PTR id = U(SetTimer)(NULL, 0, 10, NULL);
    CHECK(id != 0);
    CHECK(mq_wait(0, NULL, 3000, QS_TIMER, 0) == WAIT_OBJECT_0);
    MSG m;
    CHECK(mq_peek(&m, NULL, WM_TIMER, WM_TIMER, PM_REMOVE));
    CHECK(m.hwnd == NULL);
    EQ(m.wParam, id);
    CHECK(U(KillTimer)(NULL, id));
}

QTEST(msg_queue_post_peek_timer_proc_stored_in_lparam_and_called_by_dispatch) {
    HWND h = mq_win(); QNEED(h);
    g_tp_hits = 0;
    CHECK(U(SetTimer)(h, 9, 10, tproc) == 9);
    CHECK(mq_wait(0, NULL, 3000, QS_TIMER, 0) == WAIT_OBJECT_0);
    MSG m;
    CHECK(mq_peek(&m, h, WM_TIMER, WM_TIMER, PM_REMOVE));
    EQ(m.lParam, (LPARAM)tproc);
    int before = mq_n;
    U(DispatchMessageW)(&m);
    EQ(g_tp_hits, 1);                 /* user32!DispatchMessageWorker calls the TIMERPROC directly */
    CHECK(g_tp_hwnd == h);
    EQ(g_tp_msg, WM_TIMER);
    EQ(g_tp_id, 9);
    EQ(mq_n, before);                 /* the window procedure was bypassed */
    CHECK(U(KillTimer)(h, 9));
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_killtimer_invalid_fails) {
    HWND h = mq_win(); QNEED(h);
    SetLastError(0);
    CHECK(!U(KillTimer)(h, 12345));          /* no such timer */
    t_note("KillTimer of an unknown id: last error %lu", GetLastError());
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_timer_delivery_order_after_posted) {
    HWND h = mq_win(); QNEED(h);
    CHECK(U(SetTimer)(h, 5, 10, NULL) == 5);
    CHECK(mq_wait(0, NULL, 3000, QS_TIMER, 0) == WAIT_OBJECT_0);
    CHECK(mq_post(h, M1, 0, 0));
    MSG m;
    CHECK(mq_peek(&m, h, 0, 0, PM_REMOVE));
    EQ(m.message, M1);               /* posted messages outrank WM_TIMER */
    CHECK(mq_peek(&m, h, 0, 0, PM_REMOVE));
    EQ(m.message, WM_TIMER);
    U(KillTimer)(h, 5);
    U(DestroyWindow)(h);
}

/* ---- paint ----------------------------------------------------------------------------- */

QTEST(msg_queue_post_peek_paint_sets_qs_paint_and_wm_paint_retrievable) {
    HWND h = mq_win_visible(); QNEED(h);
    MSG m;
    /* drain the initial paint request */
    mq_peek(&m, h, WM_PAINT, WM_PAINT, PM_REMOVE | PM_QS_PAINT);
    U(UpdateWindow)(h);
    U(InvalidateRect)(h, NULL, FALSE);
    DWORD s = mq_status(QS_PAINT);
    if (!(HI(s) & QS_PAINT)) { t_note("window did not get a pending paint (off-screen clipping?): 0x%lx", s); U(DestroyWindow)(h); return; }
    CHECK(mq_peek(&m, h, 0, 0, PM_NOREMOVE | PM_QS_PAINT));
    EQ(m.message, WM_PAINT);
    CHECK(m.hwnd == h);
    U(ValidateRect)(h, NULL);
    s = mq_status(QS_PAINT);
    EQ(HI(s) & QS_PAINT, 0);
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_paint_is_lower_priority_than_posted) {
    HWND h = mq_win_visible(); QNEED(h);
    MSG m;
    while (mq_peek(&m, h, WM_PAINT, WM_PAINT, PM_REMOVE)) U(ValidateRect)(h, NULL);
    U(InvalidateRect)(h, NULL, FALSE);
    if (!(HI(mq_status(QS_PAINT)) & QS_PAINT)) { t_note("no pending paint"); U(DestroyWindow)(h); return; }
    CHECK(mq_post(h, M1, 0, 0));
    CHECK(mq_peek(&m, h, 0, 0, PM_REMOVE));
    EQ(m.message, M1);
    CHECK(mq_peek(&m, h, 0, 0, PM_NOREMOVE));
    EQ(m.message, WM_PAINT);
    U(ValidateRect)(h, NULL);
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_paint_range_filter_excludes_wm_paint) {
    HWND h = mq_win_visible(); QNEED(h);
    MSG m;
    U(InvalidateRect)(h, NULL, FALSE);
    if (!(HI(mq_status(QS_PAINT)) & QS_PAINT)) { t_note("no pending paint"); U(DestroyWindow)(h); return; }
    CHECK(!mq_peek(&m, h, WM_USER, 0x7FFF, PM_NOREMOVE));
    CHECK(mq_peek(&m, h, WM_PAINT, WM_PAINT, PM_NOREMOVE));
    CHECK(!mq_peek(&m, h, 0, 0, PM_NOREMOVE | PM_QS_POSTMESSAGE));
    U(ValidateRect)(h, NULL);
    U(DestroyWindow)(h);
}

/* ---- sent messages between threads ------------------------------------------------------ */

typedef struct { HWND target; UINT msg; WPARAM w; LPARAM l; int mode; HANDLE returned; } sendarg;
static DWORD WINAPI sender_fn(mq_helper *self) {
    sendarg *a = self->arg;
    U(PeekMessageW)(&(MSG){0}, NULL, 0, 0, PM_NOREMOVE);      /* give the sender a queue too */
    SetEvent(self->ready);
    WaitForSingleObject(self->go, 5000);
    if (self->stop) return 0;
    LRESULT r = 0;
    if (a->mode == 0) r = U(SendMessageW)(a->target, a->msg, a->w, a->l);
    else if (a->mode == 1) r = U(SendNotifyMessageW)(a->target, a->msg, a->w, a->l);
    self->result[0] = (LONG)r;
    if (a->returned) SetEvent(a->returned);
    SetEvent(self->done);
    return 0;
}

static volatile LONG h_ismex, h_insend, h_ismex_after, h_tid, h_qs_ok, h_replied_seen, h_hwnd_ok, h_msg_ok, h_w_ok, h_l_ok;
static HANDLE h_returned;
static int send_hook(HWND hw, UINT m, WPARAM w, LPARAM l, LRESULT *r) {
    if (m != M1) return 0;
    h_ismex = U(InSendMessageEx)(NULL);
    h_insend = U(InSendMessage)();
    h_tid = (LONG)GetCurrentThreadId();
    if (QSM()) {
        MSG q; memset(&q, 0, sizeof q);
        h_qs_ok = QSM()(&q) != 0;
        h_hwnd_ok = q.hwnd == hw; h_msg_ok = q.message == m; h_w_ok = q.wParam == w; h_l_ok = q.lParam == l;
    }
    if (w == 0x99 && h_returned) {          /* ReplyMessage must release the blocked sender */
        U(ReplyMessage)(0x4242);
        h_ismex_after = U(InSendMessageEx)(NULL);
        h_replied_seen = WaitForSingleObject(h_returned, 3000) == WAIT_OBJECT_0;
    }
    *r = 0x1234;
    return 1;
}

static int wait_status_bit(UINT bit, DWORD ms) {
    DWORD t0 = GetTickCount();
    while (GetTickCount() - t0 < ms) {
        if (HI(mq_status(bit)) & bit) return 1;
        Sleep(10);
    }
    return 0;
}

QTEST(msg_queue_post_peek_sent_message_sets_qs_sendmessage_and_runs_on_owner_thread) {
    QNEED(U(SendMessageW)); QNEED(U(InSendMessageEx));
    HWND h = mq_win(); QNEED(h);
    sendarg a = { h, M1, 0x55, 0x66, 0, NULL };
    mq_helper s;
    if (!mq_helper_start(&s, sender_fn, &a)) { t_note("thread"); return; }
    WaitForSingleObject(s.ready, 5000);
    h_ismex = h_insend = h_tid = h_qs_ok = h_hwnd_ok = h_msg_ok = h_w_ok = h_l_ok = -7;
    mq_hook = send_hook;
    SetEvent(s.go);
    int seen = wait_status_bit(QS_SENDMESSAGE, 3000);
    CHECK(seen);                                    /* queued as a sent message: QS_SENDMESSAGE pending */
    MSG m;
    EQ(mq_peek(&m, NULL, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE), 0);   /* PeekMessage dispatches the sent message internally */
    CHECK(WaitForSingleObject(s.done, 3000) == WAIT_OBJECT_0);
    EQ(s.result[0], 0x1234);
    EQ(h_tid, (LONG)GetCurrentThreadId());
    EQ(h_ismex, ISMEX_SEND);
    NE(h_insend, 0);
    if (QSM()) {
        EQ(h_qs_ok, 1);
        EQ(h_hwnd_ok, 1); EQ(h_msg_ok, 1); EQ(h_w_ok, 1); EQ(h_l_ok, 1);
    }
    EQ(U(InSendMessageEx)(NULL), 0);         /* outside the wndproc nothing is being sent */
    EQ(U(InSendMessage)(), 0);
    DWORD st = mq_status(QS_SENDMESSAGE);
    EQ(HI(st) & QS_SENDMESSAGE, 0);
    mq_helper_finish(&s);
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_replymessage_releases_sender_and_sets_replied_flag) {
    QNEED(U(ReplyMessage)); QNEED(U(SendMessageW)); QNEED(U(InSendMessageEx));
    HWND h = mq_win(); QNEED(h);
    h_returned = CreateEventW(NULL, TRUE, FALSE, NULL);
    sendarg a = { h, M1, 0x99, 0, 0, h_returned };
    mq_helper s;
    if (!mq_helper_start(&s, sender_fn, &a)) { t_note("thread"); return; }
    WaitForSingleObject(s.ready, 5000);
    h_replied_seen = h_ismex_after = -7;
    mq_hook = send_hook;
    SetEvent(s.go);
    CHECK(wait_status_bit(QS_SENDMESSAGE, 3000));
    MSG m; mq_peek(&m, NULL, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE);
    CHECK(WaitForSingleObject(s.done, 3000) == WAIT_OBJECT_0);
    EQ(h_replied_seen, 1);                    /* the sender returned while the wndproc was still running */
    EQ(s.result[0], 0x4242);                  /* SendMessage returned the ReplyMessage value */
    EQ(h_ismex_after, ISMEX_SEND | ISMEX_REPLIED);
    mq_helper_finish(&s);
    CloseHandle(h_returned); h_returned = NULL;
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_sendnotify_cross_thread_reports_ismex_notify) {
    QNEED(U(SendNotifyMessageW)); QNEED(U(InSendMessageEx));
    HWND h = mq_win(); QNEED(h);
    sendarg a = { h, M1, 1, 2, 1, NULL };
    mq_helper s;
    if (!mq_helper_start(&s, sender_fn, &a)) { t_note("thread"); return; }
    WaitForSingleObject(s.ready, 5000);
    h_ismex = -7;
    mq_hook = send_hook;
    SetEvent(s.go);
    CHECK(WaitForSingleObject(s.done, 3000) == WAIT_OBJECT_0);   /* SendNotifyMessage does not block */
    CHECK(wait_status_bit(QS_SENDMESSAGE, 3000));
    MSG m; mq_peek(&m, NULL, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE);
    EQ(h_ismex, ISMEX_NOTIFY);                 /* NtUserGetThreadState(8): no waiting sender -> 2 */
    EQ(U(InSendMessageEx)(NULL), 0);
    mq_helper_finish(&s);
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_sendmessage_same_thread_is_not_a_send) {
    HWND h = mq_win(); QNEED(h);
    h_ismex = -7; h_insend = -7; h_qs_ok = -7;
    mq_hook = send_hook;
    EQ(U(SendMessageW)(h, M1, 0, 0), 0x1234);
    EQ(h_ismex, ISMEX_NOSEND);                 /* direct call: no SMS is created */
    EQ(h_insend, 0);
    if (QSM()) EQ(h_qs_ok, 0);
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_insendmessage_state_outside_is_zero) {
    QNEED(UN(NtUserGetThreadState)); QNEED(QSM());
    EQ(UN(NtUserGetThreadState)(TS_INSENDMSG), 0);
    MSG q; memset(&q, 0x77, sizeof q);
    EQ(QSM()(&q), 0);
    EQ(((unsigned char *)&q)[0], 0x77);        /* untouched when no message is being handled */
    EQ(QSM()(NULL), 0);
}

QTEST(msg_queue_post_peek_peek_dispatches_pending_sent_message_even_with_noremove) {
    QNEED(U(SendMessageW));
    HWND h = mq_win(); QNEED(h);
    sendarg a = { h, M1, 3, 4, 0, NULL };
    mq_helper s;
    if (!mq_helper_start(&s, sender_fn, &a)) { t_note("thread"); return; }
    WaitForSingleObject(s.ready, 5000);
    mq_hook = send_hook;
    SetEvent(s.go);
    CHECK(wait_status_bit(QS_SENDMESSAGE, 3000));
    int c0 = mq_count(M1);
    MSG m;
    CHECK(!mq_peek(&m, NULL, 0, 0, PM_NOREMOVE));
    EQ(mq_count(M1), c0 + 1);
    CHECK(WaitForSingleObject(s.done, 3000) == WAIT_OBJECT_0);
    mq_helper_finish(&s);
    U(DestroyWindow)(h);
}
