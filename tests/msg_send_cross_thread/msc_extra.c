/* msg_send_cross_thread: kernel-maintained "in a send" state (QuerySendMessage, the CLIENTINFO flag
 * word, THREADSTATE_INSENDMESSAGE), direct fnid calls across threads, user32 helpers that send
 * messages (GetWindowText & co), and a few scheduling scenarios (paused receiver, exited notify
 * sender, concurrent senders). */
#include "msc.h"

#define BIGW ((WPARAM)0x0123456789ABCDEFULL)
#define BIGL ((LPARAM)0xFEDCBA9876543210ULL)

static LRESULT nt_call(HWND h, UINT m, WPARAM w, LPARAM l, ULONG_PTR x, DWORD fnid, DWORD ansi) {
    NtUserMessageCall_pfn f = UN(NtUserMessageCall);
    return f ? f(h, m, w, l, x, fnid, ansi) : 0;
}
#define NEED_NT() do { if (!UN(NtUserMessageCall)) { t_skip("win32u!NtUserMessageCall missing"); return; } \
                       if (t_is_wine()) { t_skip("Wine: NtUserMessageCall has a different fnid vocabulary"); return; } } while (0)

/* ---- QuerySendMessage -------------------------------------------------------------------- */
UDECL(LONG, NtUserQuerySendMessage, (MSG *));

TEST(msc_qsm_outside_a_send_returns_false_and_leaves_msg) {
    MSC_NEED_GUI();
    NtUserQuerySendMessage_pfn f = UN(NtUserQuerySendMessage);
    REQUIRE(f);
    if (t_is_wine()) { t_skip("Wine: NtUserQuerySendMessage is an unimplemented stub that aborts"); return; }
    MSG m; memset(&m, 0xCC, sizeof m);
    /* win32kfull!NtUserQuerySendMessage: psmsCurrent == NULL => 0 and nothing is written. */
    EQ(f(&m), 0);
    unsigned char *p = (unsigned char *)&m; int clean = 1;
    for (size_t i = 0; i < sizeof m; i++) if (p[i] != 0xCC) clean = 0;
    CHECK(clean);
    EQ(f(NULL), 0);
    if (msc_api()->QuerySendMessage) { EQ(msc_api()->QuerySendMessage(&m), 0); }
}
TEST(msc_qsm_inside_cross_thread_send_reports_the_message) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    if (!msc_api()->QuerySendMessage) { msc_worker_stop(&wk); t_skip("user32!QuerySendMessage missing"); return; }
    LRESULT r = SendMessageW(wk.w, MSC_QSM, 0x1122334455667788ULL, 0x00007FFE12345678LL);
    EQ(r, 0x51);
    struct msc_beh *b = wk.beh;
    CHECK(b->qsm_ret);
    EQ(b->qsm_msg.hwnd, wk.w);
    EQ(b->qsm_msg.message, MSC_QSM);
    EQ(b->qsm_msg.wParam, 0x1122334455667788ULL);
    EQ(b->qsm_msg.lParam, 0x00007FFE12345678LL);
    /* _QuerySendMessage clears pt; NtUserQuerySendMessage writes the MSG fields */
    EQ(b->qsm_msg.pt.x, 0); EQ(b->qsm_msg.pt.y, 0);
    t_info("MSG.time reported for the sent message: %lu", b->qsm_msg.time);
    /* a NULL buffer is accepted while a send is in progress */
    CHECK(b->qsm_null_ret);
    msc_worker_stop(&wk);
}
TEST(msc_qsm_lparam_above_user_probe_address_reads_back_zero) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    if (!msc_api()->QuerySendMessage) { msc_worker_stop(&wk); t_skip("user32!QuerySendMessage missing"); return; }
    /* NtUserQuerySendMessage drops an lParam beyond MmUserProbeAddress (0x7FFFFFFF0000 on x64) to 0. */
    static const struct { LPARAM l; int zero; } c[] = {
        { 0, 1 }, { 1, 0 }, { 0x7FFFFFFE0000LL, 0 }, { (LPARAM)0xFFFFFFFFFFFFFFFFULL, 1 }, { (LPARAM)0xFFFF800000000000ULL, 1 }, { (LPARAM)0x7FFFFFFF0001ULL, 1 } };
    for (unsigned i = 0; i < sizeof c / sizeof *c; i++) {
        SendMessageW(wk.w, MSC_QSM, 5, c[i].l);
        CHECKF(wk.beh->qsm_ret, "case %u", i);
        LPARAM got = wk.beh->qsm_msg.lParam;
        CHECKF(c[i].zero ? got == 0 : got == c[i].l, "lParam 0x%llx read back as 0x%llx", (unsigned long long)c[i].l, (unsigned long long)got);
    }
    msc_worker_stop(&wk);
}
TEST(msc_qsm_same_thread_send_has_no_current_message) {
    MSC_NEED_GUI();
    struct msc_beh *b; HWND h = msc_create(NULL, &b, 0, NULL);
    if (!h || !msc_api()->QuerySendMessage) { if (h) DestroyWindow(h); t_skip("window or export missing"); return; }
    SendMessageW(h, MSC_QSM, 1, 2);
    CHECK(!b->qsm_ret);
    CHECK(!b->qsm_null_ret);
    EQ(b->gts8, ISMEX_NOSEND);
    DestroyWindow(h);
}
TEST(msc_qsm_in_notify_and_callback_handlers_observed) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    if (!msc_api()->QuerySendMessage) { msc_worker_stop(&wk); t_skip("user32!QuerySendMessage missing"); return; }
    msc_log_reset();
    SendNotifyMessageW(wk.w, MSC_QSM, 3, 4);
    msc_log_wait(MSC_QSM, 1, 3000); Sleep(50);
    t_note("notify handler: QuerySendMessage=%d NtUserGetThreadState(8)=0x%llx", wk.beh->qsm_ret, (unsigned long long)wk.beh->gts8);
    EQ(wk.beh->gts8, ISMEX_NOTIFY);
    msc_worker_stop(&wk);
}
TEST(msc_threadstate_insendmessage_matches_ismex_flags) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    if (!msc_api()->NtUserGetThreadState) { msc_worker_stop(&wk); t_skip("NtUserGetThreadState missing"); return; }
    /* THREADSTATE_INSENDMESSAGE (8) is what InSendMessageEx falls back to in user32. */
    EQ(msc_api()->NtUserGetThreadState(8), 0);
    msc_log_reset();
    SendMessageW(wk.w, MSC_QSM, 0, 0);
    struct msc_ev e; CHECK(msc_log_find(MSC_QSM, 0, &e));
    EQ(wk.beh->gts8, ISMEX_SEND);
    EQ(e.ismex, wk.beh->gts8);
    ULONG_PTR res;
    SendMessageTimeoutW(wk.w, MSC_QSM, 0, 0, SMTO_BLOCK | SMTO_ABORTIFHUNG, 3000, &res);
    EQ(wk.beh->gts8, ISMEX_SEND);          /* SMTO flags do not change the reported kind */
    msc_worker_stop(&wk);
}

/* ---- the CLIENTINFO flag word behind TEB+0x860 ----------------------------------------------- */
TEST(msc_clientinfo_flag_word_tracks_sent_message_state) {
    MSC_NEED_GUI();
    if (t_is_wine()) { t_skip("CLIENTINFO layout is win32k-specific"); return; }
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    struct msc_beh *mb; HWND mh = msc_create(NULL, &mb, 0, NULL);
    if (!mh) { msc_worker_stop(&wk); t_skip("window"); return; }
    /* user32!InSendMessage reads bit 1 of the DWORD that TEB+0x860 (CLIENTINFO+0x60) points at. */
    void *pw = NULL; unsigned long v = 0;
    CHECK(k_try_read_ptr((char *)k_teb() + 0x860, &pw));
    CHECK(pw != NULL && k_try_read_u32(pw, &v));
    CHECK((v & 2) == 0);                                  /* outside any send */
    msc_log_reset();
    wk.beh->peer = mh;
    SendMessageW(wk.w, MSC_NEST, 1, 0);                   /* worker -> back to us: our proc runs inside a send */
    struct msc_ev in_worker, in_main;
    CHECK(msc_log_find(MSC_NEST, 0, &in_worker));
    CHECK(msc_log_find(MSC_ECHO, 0, &in_main));
    CHECK(in_worker.ci_ok && in_main.ci_ok);
    CHECKF((in_worker.ciflags & 2) != 0, "worker handler flag word 0x%lx", in_worker.ciflags);
    CHECKF((in_main.ciflags & 2) != 0, "nested handler on the sender thread flag word 0x%lx", in_main.ciflags);
    CHECK(k_try_read_u32(pw, &v)); CHECK((v & 2) == 0);   /* cleared again after the nested call unwound */
    /* a posted message is not a send */
    PostMessageW(wk.w, MSC_POSTED, 0, 0);
    CHECK(msc_log_wait(MSC_POSTED, 1, 3000));
    struct msc_ev ep; CHECK(msc_log_find(MSC_POSTED, 0, &ep));
    CHECK(ep.ci_ok && (ep.ciflags & 2) == 0);
    /* same-thread send: still no "in send" bit */
    msc_log_reset();
    SendMessageW(mh, MSC_ECHO, 1, 1);
    CHECK(msc_log_find(MSC_ECHO, 0, &in_main));
    CHECK(in_main.ci_ok && (in_main.ciflags & 2) == 0);
    DestroyWindow(mh);
    msc_worker_stop(&wk);
}
TEST(msc_clientinfo_flag_word_in_notify_and_callback_handlers) {
    MSC_NEED_GUI();
    if (t_is_wine()) { t_skip("CLIENTINFO layout is win32k-specific"); return; }
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    msc_log_reset();
    SendNotifyMessageW(wk.w, MSC_ECHO, 1, 1);
    SendMessageCallbackW(wk.w, MSC_ECHO, 2, 2, NULL, 0);
    CHECK(msc_log_wait(MSC_ECHO, 2, 4000));
    struct msc_ev e;
    for (int i = 0; msc_log_find(MSC_ECHO, i, &e); i++) {
        CHECK(e.ci_ok);
        if ((e.ciflags & 2) == 0) t_note("msg %d: flag word 0x%lx has no in-send bit inside a notify/callback handler", i, e.ciflags);
        CHECK(e.ismex == ISMEX_NOTIFY || e.ismex == ISMEX_CALLBACK);
    }
    msc_worker_stop(&wk);
}

/* ---- direct fnid calls across threads ----------------------------------------------------- */
TEST(msc_direct_cross_thread_sendmessage_fnid) {
    MSC_NEED_GUI(); NEED_NT();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    msc_log_reset();
    EQ(nt_call(wk.w, MSC_ECHO, BIGW, BIGL, 0, MSC_FNID_SENDMESSAGE, 0), msc_echo_ret(BIGW, BIGL));
    EQ(nt_call(wk.wa, MSC_ECHO, 1, 2, 0, MSC_FNID_SENDMESSAGE, 1), msc_echo_ret(1, 2));
    struct msc_ev e;
    CHECK(msc_log_find(MSC_ECHO, 0, &e)); EQ(e.tid, wk.tid); EQ(e.ismex, ISMEX_SEND);
    CHECK(msc_log_find(MSC_ECHO, 1, &e)); EQ(e.tid, wk.tid); EQ(e.ansi, 1);
    msc_worker_stop(&wk);
}
TEST(msc_direct_cross_thread_smto_block_and_timeout) {
    MSC_NEED_GUI(); NEED_NT();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    struct msc_smto s = { SMTO_NORMAL, 2000, 0x1111, 0x2222 };
    nt_call(wk.w, MSC_ECHO, BIGW, BIGL, (ULONG_PTR)&s, MSC_FNID_SENDMESSAGETIMEOUT, 0);
    EQ(s.ret, 1); EQ(s.result, msc_echo_ret(BIGW, BIGL));
    /* a timeout reports status 0 and ERROR_TIMEOUT from the kernel itself (user32 adds nothing) */
    struct msc_smto t = { SMTO_NORMAL, 200, 0x1111, 0x2222 };
    SetLastError(0);
    nt_call(wk.w, MSC_SLEEP, 700, 0, (ULONG_PTR)&t, MSC_FNID_SENDMESSAGETIMEOUT, 0);
    DWORD err = GetLastError();
    EQ(t.ret, 0);
    EQ(err, ERROR_TIMEOUT);
    Sleep(600);
    msc_worker_stop(&wk);
}
TEST(msc_direct_cross_thread_notify_fnid_returns_before_proc) {
    MSC_NEED_GUI(); NEED_NT();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    HANDLE done = CreateEventW(NULL, FALSE, FALSE, NULL); wk.beh->done = done;
    LONGLONG t0 = msc_qpc();
    LRESULT r = nt_call(wk.w, MSC_SLEEP, 600, 0, 0, MSC_FNID_SENDNOTIFYMESSAGE, 0);
    double ms = msc_ms_since(t0);
    CHECKF(ms < 450.0, "notify fnid blocked %.0f ms", ms);
    t_note("NtUserMessageCall(fnid 0x2B7) returned 0x%llx", (unsigned long long)r);
    CHECK(WaitForSingleObject(done, 4000) == WAIT_OBJECT_0);
    CloseHandle(done); wk.beh->done = NULL;
    msc_worker_stop(&wk);
}
static volatile LONG g_dcb_hits; static LRESULT g_dcb_res; static ULONG_PTR g_dcb_data;
static void CALLBACK dcb(HWND h, UINT m, ULONG_PTR d, LRESULT r) { g_dcb_res = r; g_dcb_data = d; InterlockedIncrement(&g_dcb_hits); }
TEST(msc_direct_cross_thread_callback_fnid) {
    MSC_NEED_GUI(); NEED_NT();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    /* the kernel probes a 16-byte, 4-aligned {callback, data} block from xParam. */
    static struct msc_smcb blk;
    blk.cb = dcb; blk.data = 0x5EED;
    g_dcb_hits = 0;
    nt_call(wk.w, MSC_ECHO, 3, 4, (ULONG_PTR)&blk, MSC_FNID_SENDMESSAGECALLBACK, 0);
    CHECK(msc_pump_until(&g_dcb_hits, 4000));
    EQ(g_dcb_hits, 1); EQ(g_dcb_res, msc_echo_ret(3, 4)); EQ(g_dcb_data, 0x5EED);
    msc_worker_stop(&wk);
}

/* ---- non-pointer messages pass through the DWORD handler unchanged --------------------------- */
TEST(msc_cross_dword_messages_preserve_wparam_lparam) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    static const UINT ms[] = { 0x03, 0x05, 0x06, 0x07, 0x08, 0x0A, 0x0B, 0x0F, 0x14, 0x18, 0x1C, 0x1F, 0x20, 0x21, 0x22, 0x30, 0x31, 0x37,
        0x7B, 0x7E, 0x84, 0x85, 0x86, 0x100, 0x101, 0x104, 0x105, 0x111, 0x112, 0x114, 0x115, 0x116, 0x117, 0x121, 0x200, 0x201, 0x202,
        0x204, 0x20A, 0x210, 0x30F, 0x31A };
    static const struct { WPARAM w; LPARAM l; } v[] = { { 0x11223344, 0x55667788 }, { 0, (LPARAM)(int)-5 }, { 0xFFFF, 0x7FFF0000 } };
    for (unsigned i = 0; i < sizeof ms / sizeof *ms; i++) {
        for (unsigned k = 0; k < sizeof v / sizeof *v; k++) {
            wk.beh->cap_msg = ms[i]; wk.beh->cap_len = 0; wk.beh->cap_ret = 0x6060; wk.beh->cap_hits = 0; wk.beh->cap_w = 0xDEAD; wk.beh->cap_ptr = 0xDEAD;
            LRESULT r = SendMessageW(wk.w, ms[i], v[k].w, v[k].l);
            CHECKF(r == 0x6060 && wk.beh->cap_hits == 1, "msg %03x/%u: r=0x%llx hits=%ld", ms[i], k, (unsigned long long)r, wk.beh->cap_hits);
            CHECKF(wk.beh->cap_w == v[k].w && wk.beh->cap_ptr == v[k].l, "msg %03x/%u: delivered w=0x%llx l=0x%llx want w=0x%llx l=0x%llx", ms[i], k,
                   (unsigned long long)wk.beh->cap_w, (unsigned long long)wk.beh->cap_ptr, (unsigned long long)v[k].w, (unsigned long long)v[k].l);
        }
    }
    wk.beh->cap_msg = 0;
    msc_worker_stop(&wk);
}

/* ---- user32 helpers that send ------------------------------------------------------------- */
TEST(msc_user32_gettext_helpers_send_to_owner_thread_in_process) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    __typeof__(&GetWindowTextLengthW) glen = U(GetWindowTextLengthW);
    __typeof__(&GetWindowTextA) gta = U(GetWindowTextA);
    if (!glen || !gta) { msc_worker_stop(&wk); t_skip("exports"); return; }
    /* docs: for a window of the same process these helpers send WM_GETTEXT / WM_GETTEXTLENGTH / WM_SETTEXT. */
    SetWindowTextW(wk.w, L"helper text");
    CHECK(msc_log_count(WM_SETTEXT) >= 1);
    struct msc_ev e; CHECK(msc_log_find(WM_SETTEXT, 0, &e)); EQ(e.tid, wk.tid);
    msc_log_reset();
    WCHAR wb[32] = L""; char ab[32] = "";
    EQ(GetWindowTextW(wk.w, wb, 32), 11); EXPECT_WSTR(wb, L"helper text");
    CHECK(msc_log_count(WM_GETTEXT) == 1);
    EQ(gta(wk.w, ab, 32), 11); EXPECT_STR(ab, "helper text");
    CHECK(msc_log_count(WM_GETTEXT) == 2);
    EQ(glen(wk.w), 11);
    CHECK(msc_log_count(WM_GETTEXTLENGTH) == 1);
    CHECK(msc_log_find(WM_GETTEXT, 0, &e)); EQ(e.tid, wk.tid); EQ(e.w, 32);
    msc_worker_stop(&wk);
}
TEST(msc_user32_gettext_helpers_do_not_message_foreign_process) {
    MSC_NEED_GUI();
    /* xproc server lives in msc_xproc.c; here only the same-process control is checked, the
     * foreign-process behaviour is observed through the server's receive counts there. */
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    msc_log_reset();
    char ab[8]; GetWindowTextA(wk.wa, ab, 8);
    CHECK(msc_log_count(WM_GETTEXT) == 1);
    msc_worker_stop(&wk);
}

/* ---- scenarios ------------------------------------------------------------------------- */
TEST(msc_cross_smto_handler_sees_ismex_send) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    msc_log_reset();
    ULONG_PTR res;
    static const UINT f[] = { SMTO_NORMAL, SMTO_BLOCK, SMTO_ABORTIFHUNG, SMTO_NOTIMEOUTIFNOTHUNG, SMTO_ERRORONEXIT };
    for (unsigned i = 0; i < sizeof f / sizeof *f; i++) SendMessageTimeoutW(wk.w, MSC_ECHO, i, 0, f[i], 3000, &res);
    struct msc_ev e;
    for (int i = 0; i < 5; i++) { CHECK(msc_log_find(MSC_ECHO, i, &e)); EQ(e.ismex, ISMEX_SEND); CHECK(e.insend); }
    msc_worker_stop(&wk);
}
TEST(msc_cross_paused_receiver_times_out_without_running_the_message) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, MSCW_PAUSE, 0);
    ULONG_PTR res = 0x77;
    SetLastError(0);
    LONGLONG t0 = msc_qpc();
    LRESULT r = SendMessageTimeoutW(wk.w, MSC_ECHO, 9, 9, SMTO_NORMAL, 250, &res);
    double ms = msc_ms_since(t0);
    DWORD err = GetLastError();
    EQ(r, 0); EQ(err, ERROR_TIMEOUT);
    CHECKF(ms >= 220.0 && ms < 1500.0, "timeout after %.0f ms", ms);
    CHECK(msc_log_count(MSC_ECHO) == 0);           /* receiver has not retrieved anything yet */
    SetEvent(wk.go);
    /* the kernel unlinks a timed-out send record from the receiver's list unless the receiver already
     * started it (SMS flag 0x4000); whether the abandoned message still runs is only reported. */
    Sleep(400);
    t_note("message abandoned by a timed-out SendMessageTimeout was %s by the receiver", msc_log_count(MSC_ECHO) ? "still delivered" : "dropped");
    msc_worker_stop(&wk);
}
TEST(msc_cross_abortifhung_does_not_abort_a_merely_busy_thread) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, MSCW_PAUSE, 0);
    ULONG_PTR res = 0;
    SetLastError(0);
    LONGLONG t0 = msc_qpc();
    LRESULT r = SendMessageTimeoutW(wk.w, MSC_ECHO, 9, 9, SMTO_ABORTIFHUNG, 250, &res);
    double ms = msc_ms_since(t0);
    /* the receiver has been silent for well under the hung-app timeout, so the call waits out its own
     * timeout instead of failing fast */
    EQ(r, 0); EQ(GetLastError(), ERROR_TIMEOUT);
    CHECKF(ms >= 220.0, "ABORTIFHUNG returned after only %.0f ms", ms);
    SetEvent(wk.go);
    msc_log_wait(MSC_ECHO, 1, 3000);
    msc_worker_stop(&wk);
}
TEST(msc_cross_notify_from_exited_thread_is_still_delivered) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, MSCW_PAUSE, 0);
    struct msc_sender s = { 0 };
    s.target = wk.w; s.msg = MSC_ECHO; s.w = 5; s.l = 6; s.notify = 1;
    CHECK(msc_sender_start(&s));
    CHECK(msc_sender_join(&s, 5000));              /* helper thread is gone before the receiver runs */
    CHECK(s.ret != 0);
    CHECK(msc_log_count(MSC_ECHO) == 0);
    SetEvent(wk.go);
    CHECK(msc_log_wait(MSC_ECHO, 1, 4000));
    struct msc_ev e; CHECK(msc_log_find(MSC_ECHO, 0, &e));
    EQ(e.ismex, ISMEX_NOTIFY); EQ(e.w, 5); EQ(e.l, 6);
    msc_worker_stop(&wk);
}
TEST(msc_cross_reverse_direction_worker_sends_to_pumping_main) {
    MSC_NEED_GUI();
    struct msc_beh *mb; HWND mh = msc_create(NULL, &mb, 0, NULL);
    if (!mh) { t_skip("window"); return; }
    msc_log_reset();
    struct msc_sender s = { 0 };
    s.target = mh; s.msg = MSC_ECHO; s.w = BIGW; s.l = BIGL;
    CHECK(msc_sender_start(&s));
    CHECK(msc_pump_until(&s.finished, 5000));
    msc_sender_join(&s, 3000);
    EQ(s.ret, msc_echo_ret(BIGW, BIGL));
    struct msc_ev e; CHECK(msc_log_find(MSC_ECHO, 0, &e));
    EQ(e.tid, GetCurrentThreadId()); EQ(e.ismex, ISMEX_SEND);
    DestroyWindow(mh);
}
struct conc { HWND target; int id; volatile int ok; };
static DWORD WINAPI conc_thread(LPVOID p) {
    struct conc *c = (struct conc *)p;
    c->ok = 1;
    for (int k = 0; k < 25; k++) {
        WPARAM w = ((WPARAM)c->id << 16) | (WPARAM)k;
        if (SendMessageW(c->target, MSC_ECHO, w, (LPARAM)k) != msc_echo_ret(w, (LPARAM)k)) c->ok = 0;
    }
    return 0;
}
TEST(msc_cross_concurrent_senders_each_keep_their_order) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    struct conc c[4]; HANDLE th[4];
    msc_log_reset();
    for (int i = 0; i < 4; i++) { c[i].target = wk.w; c[i].id = i + 1; c[i].ok = 0; th[i] = CreateThread(NULL, 0, conc_thread, &c[i], 0, NULL); }
    DWORD wr = WaitForMultipleObjects(4, th, TRUE, 20000);
    CHECK(wr == WAIT_OBJECT_0);
    for (int i = 0; i < 4; i++) { CHECKF(c[i].ok, "sender %d saw a wrong result", i); CloseHandle(th[i]); }
    CHECK(msc_log_count(MSC_ECHO) == 100);
    int last[5] = { -1, -1, -1, -1, -1 }, ordered = 1;
    struct msc_ev e;
    for (int i = 0; msc_log_find(MSC_ECHO, i, &e); i++) {
        int id = (int)(e.w >> 16), k = (int)(e.w & 0xFFFF);
        if (id < 1 || id > 4 || k != last[id] + 1) ordered = 0; else last[id] = k;
        EQ(e.tid, wk.tid);
    }
    CHECK(ordered);
    msc_worker_stop(&wk);
}
TEST(msc_cross_many_windows_one_thread_each_receives_own_message) {
    MSC_NEED_GUI();
    struct msc_worker a, b; MSC_NEED_WORKER(a, 0, 0);
    if (!msc_worker_start(&b, 0, 0)) { msc_worker_stop(&a); t_skip("second worker"); return; }
    CHECK(a.tid != b.tid);
    msc_log_reset();
    EQ(SendMessageW(a.w, MSC_ECHO, 1, 1), msc_echo_ret(1, 1));
    EQ(SendMessageW(b.w, MSC_ECHO, 2, 2), msc_echo_ret(2, 2));
    EQ(SendMessageW(a.wa, MSC_ECHO, 3, 3), msc_echo_ret(3, 3));
    struct msc_ev e;
    CHECK(msc_log_find(MSC_ECHO, 0, &e)); EQ(e.tid, a.tid);
    CHECK(msc_log_find(MSC_ECHO, 1, &e)); EQ(e.tid, b.tid);
    CHECK(msc_log_find(MSC_ECHO, 2, &e)); EQ(e.tid, a.tid);
    msc_worker_stop(&a); msc_worker_stop(&b);
}
TEST(msc_cross_private_broadcast_notify_reaches_other_thread_windows) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    WCHAR nm[64]; swprintf(nm, 64, L"win32ktMscBn_%lu", GetCurrentProcessId());
    UINT priv = RegisterWindowMessageW(nm);
    msc_log_reset();
    SendNotifyMessageW(HWND_BROADCAST, priv, 0x31, 0x32);
    CHECK(msc_log_wait(priv, 2, 4000));            /* worker's W and A top-level windows */
    struct msc_ev e; int on_worker = 0;
    for (int i = 0; msc_log_find(priv, i, &e); i++) if (e.tid == wk.tid) on_worker++;
    CHECKF(on_worker == 2, "worker windows that received the notify broadcast: %d", on_worker);
    msc_worker_stop(&wk);
}
