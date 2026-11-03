/* msg_send_cross_thread: sends that cross a thread boundary inside one process.
 *
 * The receiver is a worker GUI thread (msc_worker) pumping its own queue. The sender (this test
 * thread) enters NtUserMessageCall, which queues a send record on the
 * receiver and sleeps in the blocking wait until the receiver replies, times out or dies. */
#include "msc.h"

#define BIGW ((WPARAM)0x0123456789ABCDEFULL)
#define BIGL ((LPARAM)0xFEDCBA9876543210ULL)

static HWND mk_main(struct msc_beh **b) { return msc_create(NULL, b, 0, NULL); }

/* ---- basic delivery ---------------------------------------------------------------------- */
TEST(msc_cross_send_runs_on_receiver_thread) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    msc_log_reset();
    LRESULT r = SendMessageW(wk.w, MSC_ECHO, BIGW, BIGL);
    EQ(r, msc_echo_ret(BIGW, BIGL));
    struct msc_ev e;
    if (msc_log_find(MSC_ECHO, 0, &e)) {
        EQ(e.tid, wk.tid);
        CHECK(e.tid != GetCurrentThreadId());
        EQ(e.hwnd, wk.w);
        EQ(e.w, BIGW); EQ(e.l, BIGL);
        CHECK(e.insend);                       /* InSendMessage() is TRUE inside a cross-thread send */
        EQ(e.ismex, ISMEX_SEND);        /* plain SendMessage => ISMEX_SEND only */
    } else CHECK(0);
    msc_worker_stop(&wk);
}
TEST(msc_cross_send_ansi_and_messageonly_windows) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    msc_log_reset();
    EQ(SendMessageA(wk.wa, MSC_ECHO, 1, 2), msc_echo_ret(1, 2));
    EQ(SendMessageW(wk.wa, MSC_ECHO, 3, 4), msc_echo_ret(3, 4));
    if (wk.wm) {
        EQ(SendMessageW(wk.wm, MSC_ECHO, 5, 6), msc_echo_ret(5, 6));
        EQ(SendMessageA(wk.wm, MSC_ECHO, 7, 8), msc_echo_ret(7, 8));
    } else t_note("message-only window not available");
    CHECK(!IsWindowUnicode(wk.wa));
    CHECK(IsWindowUnicode(wk.w));
    struct msc_ev e;
    for (int i = 0; msc_log_find(MSC_ECHO, i, &e); i++) {
        EQ(e.tid, wk.tid);
        if (e.hwnd == wk.wa) EQ(e.ansi, 1);
        if (e.hwnd == wk.w || e.hwnd == wk.wm) EQ(e.ansi, 0);
    }
    msc_worker_stop(&wk);
}
TEST(msc_cross_message_range_values_preserved) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    /* Messages with no pointer semantics are NtUserfnDWORD: wParam/lParam travel as raw 64-bit values. */
    static const UINT ms[] = { WM_NULL, 0x3FF, WM_USER, WM_APP, 0xBFFF, 0xC000, 0xFFFF, 0x10000, 0x1FFFF };
    msc_log_reset();
    for (unsigned i = 0; i < sizeof ms / sizeof *ms; i++) SendMessageW(wk.w, ms[i], BIGW, BIGL);
    for (unsigned i = 0; i < sizeof ms / sizeof *ms; i++) {
        struct msc_ev e;
        if (msc_log_find(ms[i], 0, &e)) { EQ(e.w, BIGW); EQ(e.l, BIGL); EQ(e.tid, wk.tid); }
        else CHECKF(0, "msg %x not delivered", ms[i]);
    }
    msc_worker_stop(&wk);
}
TEST(msc_cross_return_value_is_full_lresult) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    static const struct { WPARAM w; LPARAM l; } v[] = { { 0, 0 }, { (WPARAM)-1, 0 }, { 0, (LPARAM)-1 }, { BIGW, BIGL }, { 0x100000000ULL, 1 } };
    for (unsigned i = 0; i < sizeof v / sizeof *v; i++) EQ(SendMessageW(wk.w, MSC_ECHO, v[i].w, v[i].l), msc_echo_ret(v[i].w, v[i].l));
    msc_worker_stop(&wk);
}

/* ---- the sender blocks -------------------------------------------------------------------- */
TEST(msc_cross_sender_blocks_until_proc_returns) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    LONGLONG t0 = msc_qpc();
    LRESULT r = SendMessageW(wk.w, MSC_SLEEP, 300, 0);
    double ms = msc_ms_since(t0);
    EQ(r, 7);
    CHECKF(ms >= 280.0, "SendMessage returned after %.1f ms, handler sleeps 300", ms);
    msc_worker_stop(&wk);
}
TEST(msc_cross_smto_blocks_like_sendmessage) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    ULONG_PTR res = 0;
    LONGLONG t0 = msc_qpc();
    LRESULT r = SendMessageTimeoutW(wk.w, MSC_SLEEP, 250, 0, SMTO_NORMAL, 3000, &res);
    double ms = msc_ms_since(t0);
    CHECK(r != 0); EQ(res, 7);
    CHECKF(ms >= 230.0, "returned after %.1f ms", ms);
    msc_worker_stop(&wk);
}
TEST(msc_cross_smto_flag_combinations_succeed_on_healthy_thread) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    static const UINT f[] = { SMTO_NORMAL, SMTO_BLOCK, SMTO_ABORTIFHUNG, SMTO_BLOCK | SMTO_ABORTIFHUNG, SMTO_NOTIMEOUTIFNOTHUNG,
                              SMTO_ERRORONEXIT, SMTO_BLOCK | SMTO_ABORTIFHUNG | SMTO_NOTIMEOUTIFNOTHUNG | SMTO_ERRORONEXIT, 0x04, 0x10, 0x40, 0x7F };
    for (unsigned i = 0; i < sizeof f / sizeof *f; i++) {
        ULONG_PTR res = 0;
        SetLastError(0);
        LRESULT r = SendMessageTimeoutW(wk.w, MSC_ECHO, BIGW, BIGL, f[i], 3000, &res);
        CHECKF(r != 0 && res == (ULONG_PTR)msc_echo_ret(BIGW, BIGL), "flags %02x r=%lld res=%llx err=%lu", f[i], (long long)r, (unsigned long long)res, GetLastError());
        r = SendMessageTimeoutA(wk.wa, MSC_ECHO, 5, 6, f[i], 3000, &res);
        CHECKF(r != 0 && res == (ULONG_PTR)msc_echo_ret(5, 6), "A flags %02x r=%lld", f[i], (long long)r);
    }
    msc_worker_stop(&wk);
}
TEST(msc_cross_smto_zero_timeout_waits_indefinitely) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    ULONG_PTR res = 0;
    /* the cross-thread send path hands the raw timeout to the blocking wait; 0 is "no timeout", so a slow
     * receiver is simply waited for. */
    LONGLONG t0 = msc_qpc();
    LRESULT r = SendMessageTimeoutW(wk.w, MSC_SLEEP, 350, 0, SMTO_NORMAL, 0, &res);
    double ms = msc_ms_since(t0);
    CHECKF(r != 0 && res == 7, "r=%lld res=%llu err=%lu", (long long)r, (unsigned long long)res, GetLastError());
    CHECKF(ms >= 320.0, "returned after %.1f ms", ms);
    msc_worker_stop(&wk);
}
TEST(msc_cross_smto_timeout_reports_error_timeout) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    HANDLE done = CreateEventW(NULL, FALSE, FALSE, NULL);
    wk.beh->done = done;
    ULONG_PTR res = 0x5555;
    SetLastError(0);
    LONGLONG t0 = msc_qpc();
    LRESULT r = SendMessageTimeoutW(wk.w, MSC_SLEEP, 1200, 0, SMTO_NORMAL, 300, &res);
    double ms = msc_ms_since(t0);
    DWORD err = GetLastError();
    EQ(r, 0);
    EQ(err, ERROR_TIMEOUT);                    /* 1460, documented */
    CHECKF(ms >= 270.0 && ms < 1150.0, "timeout fired after %.1f ms (asked 300)", ms);
    t_note("result slot after timeout: 0x%llx", (unsigned long long)res);
    /* a timed-out message is not recalled: the receiver still runs it to completion */
    CHECK(WaitForSingleObject(done, 4000) == WAIT_OBJECT_0);
    CHECK(msc_log_count(MSC_SLEEP) >= 1);
    CloseHandle(done); wk.beh->done = NULL;
    msc_worker_stop(&wk);
}
TEST(msc_cross_smto_timeout_does_not_poison_next_send) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    HANDLE done = CreateEventW(NULL, FALSE, FALSE, NULL);
    wk.beh->done = done;
    ULONG_PTR res = 0;
    /* The sender can be descheduled for longer than the handler's sleep on a loaded guest, in which case the
     * send completes before the 100 ms timeout is ever observed. Such an attempt proves nothing, so it is
     * retried (bounded); what is asserted is that an attempt that was not stalled times out. */
    LRESULT tr = 1;
    for (int attempt = 0; attempt < 4 && tr != 0; attempt++) {
        LONGLONG t0 = msc_qpc();
        tr = SendMessageTimeoutW(wk.w, MSC_SLEEP, 500, 0, SMTO_NORMAL, 100, &res);
        double ms = msc_ms_since(t0);
        if (tr != 0) {
            if (ms < 100.0) break;              /* completed faster than the timeout: a real failure */
            WaitForSingleObject(done, 2000);
        }
    }
    CHECK(tr == 0);
    /* the follow-up send queues behind the still-running handler and completes normally */
    SetLastError(0);
    LRESULT r = SendMessageTimeoutW(wk.w, MSC_ECHO, 1, 2, SMTO_NORMAL, 4000, &res);
    CHECK(r != 0); EQ(res, msc_echo_ret(1, 2));
    WaitForSingleObject(done, 2000);
    CloseHandle(done); wk.beh->done = NULL;
    msc_worker_stop(&wk);
}
TEST(msc_cross_smto_notimeoutifnothung_waits_for_slow_but_alive_thread) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    ULONG_PTR res = 0;
    /* the cross-thread send path: with flag 8 a timeout is converted to a blocking wait while the target thread being hung
     * says the receiver is not hung (it only slept 0.9 s, far below the 5 s hung-app timeout). */
    LONGLONG t0 = msc_qpc();
    LRESULT r = SendMessageTimeoutW(wk.w, MSC_SLEEP, 900, 0, SMTO_NOTIMEOUTIFNOTHUNG, 250, &res);
    double ms = msc_ms_since(t0);
    CHECKF(r != 0 && res == 7, "r=%lld res=%llu err=%lu", (long long)r, (unsigned long long)res, GetLastError());
    CHECKF(ms >= 850.0, "returned after %.1f ms, should have waited for the 900 ms handler", ms);
    msc_worker_stop(&wk);
}

/* ---- SendNotifyMessage / SendMessageCallback / ReplyMessage ------------------------------- */
TEST(msc_cross_notify_returns_before_proc_finishes) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    HANDLE done = CreateEventW(NULL, FALSE, FALSE, NULL);
    wk.beh->done = done; msc_log_reset();
    LONGLONG t0 = msc_qpc();
    BOOL ok = SendNotifyMessageW(wk.w, MSC_SLEEP, 700, 0);
    double ms = msc_ms_since(t0);
    CHECK(ok);
    CHECKF(ms < 500.0, "SendNotifyMessage blocked %.1f ms behind a 700 ms handler", ms);
    CHECK(WaitForSingleObject(done, 4000) == WAIT_OBJECT_0);
    struct msc_ev e;
    if (msc_log_find(MSC_SLEEP, 0, &e)) {
        EQ(e.tid, wk.tid);
        EQ(e.ismex, ISMEX_NOTIFY);
        t_note("InSendMessage() inside a notify handler: %d", (int)e.insend);
    } else CHECK(0);
    CloseHandle(done); wk.beh->done = NULL;
    msc_worker_stop(&wk);
}
TEST(msc_cross_notify_a_and_w_deliver_asynchronously) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    msc_log_reset();
    CHECK(SendNotifyMessageW(wk.w, MSC_ECHO, BIGW, BIGL));
    CHECK(SendNotifyMessageA(wk.wa, MSC_ECHO, 1, 2));
    CHECK(msc_log_wait(MSC_ECHO, 2, 4000));
    struct msc_ev e;
    for (int i = 0; msc_log_find(MSC_ECHO, i, &e); i++) { EQ(e.tid, wk.tid); EQ(e.ismex, ISMEX_NOTIFY); }
    msc_worker_stop(&wk);
}

static volatile LONG g_cb_hits;
static HWND g_cb_hwnd; static UINT g_cb_msg; static ULONG_PTR g_cb_data; static LRESULT g_cb_res; static DWORD g_cb_tid;
static void CALLBACK cb_record(HWND h, UINT m, ULONG_PTR d, LRESULT r) {
    g_cb_hwnd = h; g_cb_msg = m; g_cb_data = d; g_cb_res = r; g_cb_tid = GetCurrentThreadId();
    InterlockedIncrement(&g_cb_hits);
}
TEST(msc_cross_callback_runs_on_sender_thread_when_it_pumps) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    g_cb_hits = 0; msc_log_reset();
    BOOL ok = SendMessageCallbackW(wk.w, MSC_ECHO, BIGW, BIGL, cb_record, (ULONG_PTR)BIGW);
    CHECK(ok);
    t_note("callbacks delivered before the sender retrieved a message: %ld", g_cb_hits);
    CHECKF(msc_pump_until(&g_cb_hits, 4000), "callback never arrived");
    CHECK(g_cb_hits == 1);
    EQ(g_cb_tid, GetCurrentThreadId());
    EQ(g_cb_hwnd, wk.w); EQ(g_cb_msg, MSC_ECHO);
    EQ(g_cb_data, BIGW);
    EQ(g_cb_res, msc_echo_ret(BIGW, BIGL));
    struct msc_ev e;
    if (msc_log_find(MSC_ECHO, 0, &e)) { EQ(e.tid, wk.tid); EQ(e.ismex, ISMEX_CALLBACK); } else CHECK(0);
    msc_pump(100);
    CHECK(g_cb_hits == 1);                    /* exactly once */
    msc_worker_stop(&wk);
}
TEST(msc_cross_callback_a_variant_and_many) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    g_cb_hits = 0;
    for (int i = 0; i < 5; i++) CHECK(SendMessageCallbackA(wk.wa, MSC_ECHO, i, i * 3, cb_record, 100 + i));
    for (int i = 0; i < 40 && g_cb_hits < 5; i++) msc_pump(100);
    CHECKF(g_cb_hits == 5, "callbacks %ld of 5", g_cb_hits);
    msc_worker_stop(&wk);
}
TEST(msc_cross_replymessage_unblocks_sender_early) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    HANDLE done = CreateEventW(NULL, FALSE, FALSE, NULL);
    wk.beh->done = done; msc_log_reset();
    LONGLONG t0 = msc_qpc();
    LRESULT r = SendMessageW(wk.w, MSC_REPLY, 700, 0);
    double ms = msc_ms_since(t0);
    /* The sender gets the ReplyMessage value, not the procedure's eventual 0x9999, and is released
     * long before the handler's post-reply sleep ends. */
    EQ(r, (LRESULT)0x1234567890ABCDEFLL);
    CHECKF(ms < 500.0, "sender waited %.1f ms although the handler replied immediately", ms);
    CHECK(WaitForSingleObject(done, 0) == WAIT_TIMEOUT);       /* handler is still sleeping */
    CHECK(WaitForSingleObject(done, 4000) == WAIT_OBJECT_0);
    CHECK(wk.beh->rep1 == TRUE);                              /* first ReplyMessage succeeds */
    CHECK(wk.beh->rep2 == FALSE);                             /* the kernel handler: SMS flag bit0 already set */
    EQ(wk.beh->ismex_before & 0xF, ISMEX_SEND);
    CHECKF((wk.beh->ismex_after & ISMEX_REPLIED) != 0, "InSendMessageEx after ReplyMessage = %lx", wk.beh->ismex_after);
    t_note("InSendMessageEx after reply = 0x%lx, InSendMessage() = %d", wk.beh->ismex_after, (int)wk.beh->insend_after);
    CloseHandle(done); wk.beh->done = NULL;
    msc_worker_stop(&wk);
}
TEST(msc_cross_replymessage_in_notify_handler_note) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    HANDLE done = CreateEventW(NULL, FALSE, FALSE, NULL);
    wk.beh->done = done;
    SendNotifyMessageW(wk.w, MSC_REPLY, 50, 0);
    CHECK(WaitForSingleObject(done, 4000) == WAIT_OBJECT_0);
    t_note("ReplyMessage inside SendNotifyMessage handler: first=%d second=%d, ISMEX before=0x%lx", wk.beh->rep1, wk.beh->rep2, wk.beh->ismex_before);
    EQ(wk.beh->ismex_before & 0xF, ISMEX_NOTIFY);
    CloseHandle(done); wk.beh->done = NULL;
    msc_worker_stop(&wk);
}
TEST(msc_cross_replymessage_value_reaches_smto_result) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    HANDLE done = CreateEventW(NULL, FALSE, FALSE, NULL);
    wk.beh->done = done;
    ULONG_PTR res = 0;
    /* The early ReplyMessage value is what SendMessageTimeout stores in *lpdwResult. */
    LRESULT r = SendMessageTimeoutW(wk.w, MSC_REPLY, 100, 0, SMTO_NORMAL, 150, &res);
    CHECK(r != 0); EQ(res, (ULONG_PTR)0x1234567890ABCDEFLL);
    WaitForSingleObject(done, 3000);
    CloseHandle(done); wk.beh->done = NULL;
    msc_worker_stop(&wk);
}

/* ---- ordering and re-entrancy -------------------------------------------------------------- */
TEST(msc_cross_sent_messages_precede_posted_messages) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    HANDLE done = CreateEventW(NULL, FALSE, FALSE, NULL), go = CreateEventW(NULL, FALSE, FALSE, NULL);
    wk.beh->done = done; wk.beh->go = go; msc_log_reset();
    /* helper 1 blocks in SendMessage(MSC_PREPOST); the handler posts MSC_POSTED to its own queue and
     * waits on `go`. While it waits, helper 2 sends MSC_SENT. Once the handler returns, the next
     * message retrieval must run the pending SENT message before it returns the posted one. */
    struct msc_sender s1 = { 0 }, s2 = { 0 };
    s1.target = wk.w; s1.msg = MSC_PREPOST; s1.l = 0;
    CHECK(msc_sender_start(&s1));
    CHECK(WaitForSingleObject(done, 4000) == WAIT_OBJECT_0);
    s2.target = wk.w; s2.msg = MSC_SENT; s2.w = 0x77;
    CHECK(msc_sender_start(&s2));
    LONGLONG tw = msc_qpc();
    while (!s2.started && msc_ms_since(tw) < 2000) Sleep(1);
    Sleep(150);
    LONGLONG t_go = msc_qpc();
    SetEvent(go);
    CHECK(msc_sender_join(&s1, 5000));
    CHECK(msc_sender_join(&s2, 5000));
    CHECK(msc_log_wait(MSC_POSTED, 1, 3000));
    struct msc_ev es, ep;
    if (msc_log_find(MSC_SENT, 0, &es) && msc_log_find(MSC_POSTED, 0, &ep)) {
        if (es.seq > ep.seq) t_note("SENT was handled after POSTED (helper entered SendMessage %.1f ms before the handler was released)", msc_ms_since(s2.t_enter) - msc_ms_since(t_go));
        CHECK(es.seq < ep.seq);
        EQ(es.tid, wk.tid); EQ(ep.tid, wk.tid);
        EQ(es.ismex, ISMEX_SEND);
        EQ(ep.ismex, ISMEX_NOSEND);       /* a posted message is not "in a send" */
    } else CHECK(0);
    CloseHandle(done); CloseHandle(go); wk.beh->done = wk.beh->go = NULL;
    msc_worker_stop(&wk);
}
TEST(msc_cross_nested_send_back_to_blocked_sender) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    struct msc_beh *mb; HWND mh = mk_main(&mb);
    if (!mh) { msc_worker_stop(&wk); t_skip("window"); return; }
    wk.beh->peer = mh; msc_log_reset();
    /* While this thread sleeps in SendMessage(NEST), the handler sends back to a window owned by this
     * very thread: the blocking wait dispatches the inbound sent message, so no deadlock. */
    LRESULT r = SendMessageW(wk.w, MSC_NEST, BIGW, 0);
    EQ(r, msc_echo_ret(BIGW, 0xBEEF));
    EQ(wk.beh->nest_ret, msc_echo_ret(BIGW, 0xBEEF));
    struct msc_ev e;
    if (msc_log_find(MSC_ECHO, 0, &e)) {
        EQ(e.tid, GetCurrentThreadId());     /* ran on the blocked sender thread */
        EQ(e.hwnd, mh);
        EQ(e.ismex, ISMEX_SEND);
        CHECK(e.insend);
    } else CHECK(0);
    CHECK(!InSendMessage());
    DestroyWindow(mh);
    msc_worker_stop(&wk);
}
TEST(msc_cross_depth_pingpong_alternates_threads) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    struct msc_beh *mb; HWND mh = mk_main(&mb);
    if (!mh) { msc_worker_stop(&wk); t_skip("window"); return; }
    msc_log_reset();
    LRESULT r = SendMessageW(wk.w, MSC_DEPTH, 6, (LPARAM)mh);
    EQ(r, 1006);
    struct msc_ev e;
    int n = msc_log_count(MSC_DEPTH);
    EQ(n, 7);
    for (int i = 0; i < n && msc_log_find(MSC_DEPTH, i, &e); i++) {
        DWORD want = (i % 2 == 0) ? wk.tid : GetCurrentThreadId();
        CHECKF(e.tid == want, "level %d ran on tid %lu, want %lu", i, e.tid, want);
        EQ(e.w, 6 - i);
    }
    DestroyWindow(mh);
    msc_worker_stop(&wk);
}
TEST(msc_cross_probe_from_inside_handler_times_out_when_sender_blocks) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    struct msc_beh *mb; HWND mh = mk_main(&mb);
    if (!mh) { msc_worker_stop(&wk); t_skip("window"); return; }
    wk.beh->peer = mh; msc_log_reset();
    /* SMTO_BLOCK "prevents the calling thread from processing any other requests until the function
     * returns": while this thread waits in SendMessageTimeout(BLOCK) the handler's own send back to
     * our window cannot be dispatched, so it times out after 250 ms with ERROR_TIMEOUT. */
    struct msc_beh *wb = wk.beh;
    ULONG_PTR res = 0;
    LRESULT r = SendMessageTimeoutW(wk.w, MSC_PROBE, SMTO_NORMAL, 250, SMTO_BLOCK, 5000, &res);
    CHECK(r != 0);
    EQ(wb->probe_ret, 0);
    EQ(wb->probe_err, ERROR_TIMEOUT);
    CHECK(msc_log_count(MSC_ECHO) == 0);
    /* without SMTO_BLOCK the same probe is serviced re-entrantly (flush the leftover from phase 1 first) */
    msc_pump(100); msc_log_reset();
    r = SendMessageTimeoutW(wk.w, MSC_PROBE, SMTO_NORMAL, 2000, SMTO_NORMAL, 5000, &res);
    CHECK(r != 0);
    CHECK(wb->probe_ret != 0);
    EQ(wb->probe_res, msc_echo_ret(0, 0));
    CHECK(msc_log_count(MSC_ECHO) == 1);
    DestroyWindow(mh);
    msc_worker_stop(&wk);
}

/* ---- destroyed / dying receivers ---------------------------------------------------------- */
TEST(msc_cross_wm_close_destroys_in_owner_thread) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    HWND w = wk.w; msc_log_reset();
    SendMessageW(w, WM_CLOSE, 0, 0);       /* DefWindowProc(WM_CLOSE) -> DestroyWindow, on the owner thread */
    CHECK(!IsWindow(w));
    struct msc_ev e;
    if (msc_log_find(WM_DESTROY, 0, &e)) { EQ(e.tid, wk.tid); EQ(e.hwnd, w); } else CHECK(0);
    CHECK(msc_log_count(WM_NCDESTROY) == 1);
    SetLastError(0); EQ(SendMessageW(w, MSC_ECHO, 1, 2), 0); EXPECT_ERR(1400);
    msc_worker_stop(&wk);
}
TEST(msc_cross_destroywindow_from_other_thread_denied) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    SetLastError(0);
    BOOL ok = DestroyWindow(wk.w);
    DWORD err = GetLastError();
    CHECK(!ok);
    EQ(err, ERROR_ACCESS_DENIED);
    CHECK(IsWindow(wk.w));
    msc_worker_stop(&wk);
}
TEST(msc_cross_selfdestroy_with_erroronexit_succeeds) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    ULONG_PTR res = 0xAA;
    SetLastError(0);
    LRESULT r = SendMessageTimeoutW(wk.w, MSC_SELFDESTROY, 0, 0, SMTO_ERRORONEXIT, 3000, &res);
    DWORD err = GetLastError();
    /* The window procedure destroys its own window and still returns normally: the send completed, so SMTO_ERRORONEXIT
     * (which reports a receiver that went away without replying) has nothing to report. */
    CHECK(r != 0);
    EQ(res, 5);
    EQ(err, 0);
    CHECK(!IsWindow(wk.w));
    msc_worker_stop(&wk);
}
TEST(msc_cross_selfdestroy_without_erroronexit_observed) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    ULONG_PTR res = 0xAA;
    SetLastError(0);
    LRESULT r = SendMessageTimeoutW(wk.wa, MSC_SELFDESTROY, 0, 0, SMTO_NORMAL, 3000, &res);
    t_note("window destroyed inside handler, no ERRORONEXIT: r=%lld res=%llu err=%lu", (long long)r, (unsigned long long)res, GetLastError());
    CHECK(!IsWindow(wk.wa));
    LRESULT r2 = SendMessageW(wk.w, MSC_ECHO, 1, 2);      /* thread and the other window are unaffected */
    EQ(r2, msc_echo_ret(1, 2));
    msc_worker_stop(&wk);
}
TEST(msc_cross_receiver_thread_exit_releases_sender) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    ULONG_PTR res = 0xAA;
    LONGLONG t0 = msc_qpc();
    /* The receiver calls ExitThread inside the handler. The kernel cleans up its queue and completes
     * the pending send record, so the sender must come back long before its 6 s limit. */
    LRESULT r = SendMessageTimeoutW(wk.w, MSC_EXITTHREAD, 0, 0, SMTO_NORMAL, 6000, &res);
    double ms = msc_ms_since(t0);
    CHECKF(ms < 4500.0, "sender still blocked after %.0f ms", ms);
    t_info("thread exit mid-send: r=%lld err=%lu after %.0f ms", (long long)r, GetLastError(), ms);
    WaitForSingleObject(wk.th, 3000);
    CHECK(!IsWindow(wk.w));
    SetLastError(0); EQ(SendMessageW(wk.w, MSC_ECHO, 1, 2), 0); EXPECT_ERR(1400);
    msc_worker_stop(&wk);
}
TEST(msc_cross_receiver_thread_exit_with_erroronexit_fails_1400) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    ULONG_PTR res = 0xAA;
    SetLastError(0);
    LRESULT r = SendMessageTimeoutW(wk.w, MSC_EXITTHREAD, 0, 0, SMTO_ERRORONEXIT, 6000, &res);
    DWORD err = GetLastError();
    EQ(r, 0);
    EQ(err, ERROR_INVALID_WINDOW_HANDLE);
    WaitForSingleObject(wk.th, 3000);
    msc_worker_stop(&wk);
}
TEST(msc_cross_windows_vanish_with_their_thread) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    HWND a = wk.w, b = wk.wa, c = wk.wm;
    msc_worker_stop(&wk);                 /* thread exits, destroying its windows */
    CHECK(!IsWindow(a)); CHECK(!IsWindow(b)); CHECK(c == NULL || !IsWindow(c));
    SetLastError(0); EQ(SendMessageW(a, MSC_ECHO, 1, 2), 0); EXPECT_ERR(1400);
    SetLastError(0); EQ(SendNotifyMessageW(b, MSC_ECHO, 1, 2), 0); EXPECT_ERR(1400);
}

/* ---- hung receiver (slow: the receiver does not pump for ~6.5 s) -------------------------- */
TEST(msc_cross_hung_receiver_abortifhung_waits_for_timeout) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, MSCW_NOPUMP, 6500);
    LONGLONG t0 = msc_qpc();
    /* Early on the thread has not been silent long enough to count as hung. */
    Sleep(300);
    CHECK(!IsHungAppWindow(wk.w));
    while (msc_ms_since(t0) < 5900) Sleep(50);
    BOOL hung = IsHungAppWindow(wk.w);
    if (!hung) {
        t_note("receiver silent for %.0f ms but not reported hung (HungAppTimeout configured above default?)", msc_ms_since(t0));
        msc_worker_stop(&wk);
        return;
    }
    ULONG_PTR res = 0xAA;
    LONGLONG t1 = msc_qpc();
    SetLastError(0);
    LRESULT r = SendMessageTimeoutW(wk.w, MSC_ECHO, 1, 2, SMTO_ABORTIFHUNG, 150, &res);
    double ms = msc_ms_since(t1);
    DWORD err = GetLastError();
    /* Windows 10 reports the receiver as hung, yet SMTO_ABORTIFHUNG does not cut the wait short (same for a receiver in a
     * busy loop and for one in another process): the call waits out its timeout and fails with ERROR_TIMEOUT. */
    EQ(r, 0);
    EQ(err, ERROR_TIMEOUT);
    CHECKF(ms >= 120.0 && ms < 1500.0, "send with a 150 ms timeout took %.0f ms", ms);
    /* the receiver wakes up at 6.5 s and pumps again: no longer hung, sends work */
    LONGLONG tw = msc_qpc();
    while (!wk.pumping && msc_ms_since(tw) < 4000) Sleep(20);
    res = 0;
    r = SendMessageTimeoutW(wk.w, MSC_ECHO, 3, 4, SMTO_ABORTIFHUNG, 3000, &res);
    CHECK(r != 0); EQ(res, msc_echo_ret(3, 4));
    CHECK(!IsHungAppWindow(wk.w));
    msc_worker_stop(&wk);
}

/* ---- queue status ------------------------------------------------------------------------- */
struct qs_ctx { HWND hwnd; DWORD tid; volatile LONG ready; DWORD seen_pending, after; BOOL polled_ok; };
static DWORD WINAPI qs_thread(LPVOID p) {
    struct qs_ctx *c = (struct qs_ctx *)p;
    MSG msg;
    PeekMessageW(&msg, NULL, 0, 0, PM_NOREMOVE);
    c->hwnd = msc_create(NULL, NULL, 0, NULL);
    c->tid = GetCurrentThreadId();
    InterlockedExchange(&c->ready, 1);
    if (!c->hwnd) return 1;
    LONGLONG t0 = msc_qpc();
    /* No message retrieval here: the sent message stays queued and shows up in the wake bits. */
    while (msc_ms_since(t0) < 3000) {
        DWORD s = GetQueueStatus(QS_ALLINPUT);
        if (HIWORD(s) & QS_SENDMESSAGE) { c->seen_pending = s; c->polled_ok = TRUE; break; }
        Sleep(5);
    }
    msc_pump(300);
    c->after = GetQueueStatus(QS_ALLINPUT);
    DestroyWindow(c->hwnd);
    return 0;
}
TEST(msc_cross_pending_sent_message_sets_qs_sendmessage) {
    MSC_NEED_GUI();
    struct qs_ctx c; memset(&c, 0, sizeof c);
    HANDLE th = CreateThread(NULL, 0, qs_thread, &c, 0, NULL);
    CHECK(th != NULL);
    LONGLONG t0 = msc_qpc();
    while (!c.ready && msc_ms_since(t0) < 5000) Sleep(5);
    if (!c.hwnd) { WaitForSingleObject(th, 3000); CloseHandle(th); t_skip("window"); return; }
    msc_log_reset();
    struct msc_sender s = { 0 };
    s.target = c.hwnd; s.msg = MSC_ECHO; s.w = 9; s.l = 10;
    CHECK(msc_sender_start(&s));
    CHECK(msc_sender_join(&s, 6000));
    CHECK(WaitForSingleObject(th, 6000) == WAIT_OBJECT_0);
    CloseHandle(th);
    CHECKF(c.polled_ok, "QS_SENDMESSAGE never became visible while the message was pending");
    CHECK((HIWORD(c.seen_pending) & QS_SENDMESSAGE) != 0);
    CHECK((HIWORD(c.after) & QS_SENDMESSAGE) == 0);        /* cleared once the message was serviced */
    EQ(s.ret, msc_echo_ret(9, 10));
}

/* ---- kernel callback counters -------------------------------------------------------------- */
#define SLOT_DWORD 2   /* user32!apfnDispatch[2] == __fnDWORD */
TEST(msc_cross_send_dispatches_through_fnDWORD_slot) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    if (k_cb_count() <= SLOT_DWORD) { msc_worker_stop(&wk); t_skip("no callback table"); return; }
    CHECK(k_cb_hook(SLOT_DWORD));
    unsigned long before = k_cb_hit_count(SLOT_DWORD);
    for (int i = 0; i < 5; i++) SendMessageW(wk.w, MSC_ECHO, i, i);
    unsigned long hits = k_cb_hit_count(SLOT_DWORD) - before;
    if (hits == 0 && t_is_wine()) t_note("no kernel callbacks under Wine");
    else CHECKF(hits >= 5, "__fnDWORD hits %lu for 5 cross-thread sends", hits);
    k_cb_unhook_all();
    msc_worker_stop(&wk);
}
TEST(msc_cross_notify_and_smto_dispatch_through_fnDWORD_slot) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    if (k_cb_count() <= SLOT_DWORD) { msc_worker_stop(&wk); t_skip("no callback table"); return; }
    CHECK(k_cb_hook(SLOT_DWORD));
    unsigned long before = k_cb_hit_count(SLOT_DWORD);
    ULONG_PTR res;
    for (int i = 0; i < 3; i++) SendMessageTimeoutW(wk.w, MSC_ECHO, i, i, SMTO_NORMAL, 2000, &res);
    for (int i = 0; i < 3; i++) SendNotifyMessageW(wk.w, MSC_ECHO, i, i);
    msc_log_wait(MSC_ECHO, 6, 4000);
    unsigned long hits = k_cb_hit_count(SLOT_DWORD) - before;
    if (hits == 0 && t_is_wine()) t_note("no kernel callbacks under Wine");
    else CHECKF(hits >= 6, "__fnDWORD hits %lu for 6 cross-thread deliveries", hits);
    k_cb_unhook_all();
    msc_worker_stop(&wk);
}
TEST(msc_samethread_send_does_not_use_fnDWORD_slot_for_user_message) {
    MSC_NEED_GUI();
    struct msc_beh *b; HWND h = mk_main(&b);
    if (!h || k_cb_count() <= SLOT_DWORD) { if (h) DestroyWindow(h); t_skip("no window/callback table"); return; }
    CHECK(k_cb_hook(SLOT_DWORD));
    unsigned long before = k_cb_hit_count(SLOT_DWORD);
    for (int i = 0; i < 5; i++) SendMessageW(h, MSC_ECHO, i, i);
    unsigned long hits = k_cb_hit_count(SLOT_DWORD) - before;
    /* user32!SendMessageWorker calls the procedure directly; at most unrelated system traffic shows up. */
    CHECKF(hits < 5, "same-thread sends produced %lu __fnDWORD callbacks", hits);
    k_cb_unhook_all();
    DestroyWindow(h);
}
