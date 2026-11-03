/* PostThreadMessage, PostQuitMessage/WM_QUIT, GetMessage, TranslateMessage, DispatchMessage.
 *
 * win32kfull!NtUserPostThreadMessage resolves the target thread: a thread that has
 * not been converted to a GUI thread has no THREADINFO, so the lookup fails and the syscall sets
 * ERROR_INVALID_THREAD_ID (0x5a4). The conversion is observable locally as TEB.Win32ThreadInfo.
 */
#include "mq.h"

#define M1 (WM_USER + 0x221)
#define M2 (WM_USER + 0x222)

typedef struct { LONG Exit; PVOID Teb; ULONG_PTR Pid, Tid, Aff; LONG Pri, Base; } mq_tbi;
UDECL(LONG, NtQueryInformationThread, (HANDLE, ULONG, PVOID, ULONG, PULONG));

/* TEB.Win32ThreadInfo of another thread of this process; (void*)-1 when it cannot be read. */
static void *thread_w32info(HANDLE th) {
    if (!UN(NtQueryInformationThread)) return (void *)-1;
    mq_tbi tbi; memset(&tbi, 0, sizeof tbi);
    if (UN(NtQueryInformationThread)(th, 0, &tbi, sizeof tbi, NULL) < 0 || !tbi.Teb) return (void *)-1;
    void *p = NULL;
    if (!k_try_read_ptr((char *)tbi.Teb + TEB_Win32ThreadInfo, &p)) return (void *)-1;
    return p;
}

/* ---- helper thread: modes -------------------------------------------------------------------- */
enum { MODE_IDLE_NO_USER, MODE_CREATE_QUEUE_WAIT_MSG, MODE_MSGWAIT, MODE_GETMESSAGE, MODE_EXIT_AFTER_QUEUE };
typedef struct { int mode; MSG got; LONG gm_ret; LONG waitret; DWORD elapsed; int timed_out; } targ;

static DWORD WINAPI thr_fn(mq_helper *self) {
    targ *a = self->arg;
    if (a->mode == MODE_IDLE_NO_USER) {              /* never calls into win32k */
        SetEvent(self->ready);
        WaitForSingleObject(self->go, 8000);
        return 0;
    }
    MSG m; U(PeekMessageW)(&m, NULL, 0, 0, PM_NOREMOVE);   /* first win32k call: creates the queue */
    SetEvent(self->ready);
    if (a->mode == MODE_EXIT_AFTER_QUEUE) return 0;
    if (a->mode == MODE_CREATE_QUEUE_WAIT_MSG) {
        WaitForSingleObject(self->go, 8000);
        a->gm_ret = U(PeekMessageW)(&a->got, NULL, 0, 0, PM_REMOVE);
    } else if (a->mode == MODE_MSGWAIT) {
        DWORD t0 = GetTickCount();
        a->waitret = (LONG)U(MsgWaitForMultipleObjectsEx)(0, NULL, 5000, QS_POSTMESSAGE, 0);
        a->elapsed = GetTickCount() - t0;
        a->gm_ret = U(PeekMessageW)(&a->got, NULL, 0, 0, PM_REMOVE);
    } else if (a->mode == MODE_GETMESSAGE) {
        U(SetTimer)(NULL, 0x5151, 4000, NULL);          /* watchdog: a failed post must not hang the run */
        for (;;) {
            a->gm_ret = U(GetMessageW)(&a->got, NULL, 0, 0);
            if (a->gm_ret <= 0) break;
            if (a->got.message == WM_TIMER && a->got.hwnd == NULL) { a->timed_out = 1; break; }
            break;
        }
    }
    SetEvent(self->done);
    return 0;
}

/* ---- PostThreadMessage ----------------------------------------------------------------------- */

QTEST(msg_queue_post_peek_thread_post_to_non_gui_thread_fails_invalid_thread_id) {
    QNEED(UN(NtQueryInformationThread));
    mq_helper s; targ a = { MODE_IDLE_NO_USER };
    if (!mq_helper_start(&s, thr_fn, &a)) { t_note("thread"); return; }
    WaitForSingleObject(s.ready, 5000);
    void *w32 = thread_w32info(s.th);
    SetLastError(0);
    BOOL ok = U(PostThreadMessageW)(s.tid, M1, 1, 2);
    if (w32 == NULL) {                                 /* never made a win32k call: no THREADINFO */
        CHECK(!ok);
        EXPECT_ERR(MQ_ERR_INVALID_THREAD_ID);
    } else if (w32 == (void *)-1) {
        t_note("Win32ThreadInfo of helper thread unreadable");
    } else {
        t_note("helper thread already converted to GUI (Win32ThreadInfo=%p): post %s", w32, ok ? "ok" : "failed");
        CHECK(ok);
    }
    mq_helper_finish(&s);
}

QTEST(msg_queue_post_peek_thread_post_after_queue_creation_succeeds) {
    mq_helper s; targ a = { MODE_CREATE_QUEUE_WAIT_MSG };
    if (!mq_helper_start(&s, thr_fn, &a)) { t_note("thread"); return; }
    WaitForSingleObject(s.ready, 5000);
    void *w32 = thread_w32info(s.th);
    if (w32 != (void *)-1 && !t_is_wine()) CHECKF(w32 != NULL, "Win32ThreadInfo after first win32k call = %p", w32);
    CHECK(U(PostThreadMessageW)(s.tid, M1, 0x1122334455667788ull, 0x0102030405060708ll));
    SetEvent(s.go);
    WaitForSingleObject(s.th, 5000);
    CHECK(a.gm_ret);
    CHECK(a.got.hwnd == NULL);
    EQ(a.got.message, M1);
    EQ(a.got.wParam, 0x1122334455667788ull);
    EQ(a.got.lParam, 0x0102030405060708ll);
    mq_helper_finish(&s);
}

QTEST(msg_queue_post_peek_thread_post_invalid_thread_ids) {
    static const DWORD tids[] = { 0, 0xFFFFFFFF, 0xFFFFFFFC, 0xFFFFFFF0, 4, 0x7FFFFFFC, 0x80000000 };
    for (unsigned i = 0; i < sizeof tids / sizeof tids[0]; i++) {
        SetLastError(0);
        CHECKF(!U(PostThreadMessageW)(tids[i], M1, 0, 0), "tid 0x%lx", tids[i]);
        EXPECT_ERR(MQ_ERR_INVALID_THREAD_ID);
        if (UN(NtUserPostThreadMessage)) {
            SetLastError(0);
            EQ(UN(NtUserPostThreadMessage)(tids[i], M1, 0, 0), 0);
            EXPECT_ERR(MQ_ERR_INVALID_THREAD_ID);
        }
    }
}

QTEST(msg_queue_post_peek_thread_post_to_exited_thread_fails) {
    mq_helper s; targ a = { MODE_EXIT_AFTER_QUEUE };
    if (!mq_helper_start(&s, thr_fn, &a)) { t_note("thread"); return; }
    WaitForSingleObject(s.th, 5000);                 /* thread ran its queue-creating call and exited */
    BOOL ok = TRUE;
    for (int i = 0; i < 50 && ok; i++) {             /* THREADINFO teardown runs in the thread's exit path; allow a short grace */
        SetLastError(0);
        ok = U(PostThreadMessageW)(s.tid, M1, 0, 0);
        if (ok) Sleep(10);
    }
    CHECK(!ok);
    EXPECT_ERR(MQ_ERR_INVALID_THREAD_ID);            /* its THREADINFO was torn down with the thread */
    mq_helper_finish(&s);
}

QTEST(msg_queue_post_peek_thread_post_wakes_msgwait) {
    mq_helper s; targ a = { MODE_MSGWAIT };
    if (!mq_helper_start(&s, thr_fn, &a)) { t_note("thread"); return; }
    WaitForSingleObject(s.ready, 5000);
    Sleep(50);
    CHECK(U(PostThreadMessageW)(s.tid, M2, 5, 6));
    CHECK(WaitForSingleObject(s.done, 6000) == WAIT_OBJECT_0);
    EQ(a.waitret, WAIT_OBJECT_0);             /* returns nCount (0): the queue event */
    CHECKF(a.elapsed < 4000, "waited %lu ms", a.elapsed);
    CHECK(a.gm_ret);
    EQ(a.got.message, M2);
    mq_helper_finish(&s);
}

QTEST(msg_queue_post_peek_thread_post_wakes_getmessage) {
    mq_helper s; targ a = { MODE_GETMESSAGE };
    if (!mq_helper_start(&s, thr_fn, &a)) { t_note("thread"); return; }
    WaitForSingleObject(s.ready, 5000);
    Sleep(50);
    CHECK(U(PostThreadMessageW)(s.tid, M1, 7, 8));
    CHECK(WaitForSingleObject(s.done, 6000) == WAIT_OBJECT_0);
    CHECK(a.gm_ret > 0);
    EQ(a.timed_out, 0);
    EQ(a.got.message, M1);
    EQ(a.got.wParam, 7);
    mq_helper_finish(&s);
}

QTEST(msg_queue_post_peek_thread_post_wm_quit_ends_getmessage_loop) {
    mq_helper s; targ a = { MODE_GETMESSAGE };
    if (!mq_helper_start(&s, thr_fn, &a)) { t_note("thread"); return; }
    WaitForSingleObject(s.ready, 5000);
    Sleep(50);
    CHECK(U(PostThreadMessageW)(s.tid, WM_QUIT, 31, 0));
    CHECK(WaitForSingleObject(s.done, 6000) == WAIT_OBJECT_0);
    EQ(a.gm_ret, 0);                          /* GetMessage returns 0 for WM_QUIT */
    EQ(a.got.message, WM_QUIT);
    EQ(a.got.wParam, 31);
    mq_helper_finish(&s);
}

/* ---- PostQuitMessage -------------------------------------------------------------------------- */

QTEST(msg_queue_post_peek_postquit_delivers_wm_quit_once) {
    MSG m; memset(&m, 0, sizeof m);
    U(PostQuitMessage)(42);
    CHECK(mq_peek(&m, NULL, 0, 0, PM_REMOVE));
    EQ(m.message, WM_QUIT);
    EQ(m.wParam, 42);
    EQ(m.lParam, 0);
    CHECK(m.hwnd == NULL);
    CHECK(!mq_peek(&m, NULL, 0, 0, PM_REMOVE));
}

QTEST(msg_queue_post_peek_postquit_exit_codes) {
    static const int codes[] = { 0, 1, 42, 255, 0x7FFFFFFF, 0x12345678 };
    for (unsigned i = 0; i < sizeof codes / sizeof codes[0]; i++) {
        MSG m;
        U(PostQuitMessage)(codes[i]);
        CHECKF(mq_peek(&m, NULL, 0, 0, PM_REMOVE), "code %d", codes[i]);
        EQ(m.wParam, (WPARAM)codes[i]);
    }
    MSG m;
    U(PostQuitMessage)(-1);
    CHECK(mq_peek(&m, NULL, 0, 0, PM_REMOVE));
    t_note("PostQuitMessage(-1) -> wParam 0x%llx", (unsigned long long)m.wParam);
}

QTEST(msg_queue_post_peek_postquit_peek_noremove_keeps_flag) {
    MSG m;
    U(PostQuitMessage)(3);
    if (mq_peek(&m, NULL, 0, 0, PM_NOREMOVE)) {       /* the quit flag is only cleared by a PM_REMOVE retrieval */
        for (int i = 0; i < 3; i++) { CHECK(mq_peek(&m, NULL, 0, 0, PM_NOREMOVE)); EQ(m.message, WM_QUIT); }
    } else t_note("PM_NOREMOVE peek did not report the pending quit request");
    CHECK(mq_peek(&m, NULL, 0, 0, PM_REMOVE));
    EQ(m.message, WM_QUIT);
    CHECK(!mq_peek(&m, NULL, 0, 0, PM_NOREMOVE));
}

QTEST(msg_queue_post_peek_postquit_comes_after_posted_messages) {
    /* WM_QUIT is synthesised by the message retrieval only once no posted message is left. */
    HWND h = mq_win(); QNEED(h);
    MSG m;
    CHECK(mq_post(h, M1, 1, 0));
    U(PostQuitMessage)(9);
    CHECK(mq_post(h, M2, 2, 0));                      /* posted after the quit request, still delivered first */
    CHECK(mq_peek(&m, NULL, 0, 0, PM_REMOVE)); EQ(m.message, M1);
    CHECK(mq_peek(&m, NULL, 0, 0, PM_REMOVE)); EQ(m.message, M2);
    CHECK(mq_peek(&m, NULL, 0, 0, PM_REMOVE)); EQ(m.message, WM_QUIT);
    CHECK(!mq_peek(&m, NULL, 0, 0, PM_REMOVE));
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_postquit_message_filter) {
    MSG m;
    U(PostQuitMessage)(4);
    /* WM_QUIT is not a queued message: the filter range does not apply to it, so a range that excludes WM_QUIT (0x12)
     * still retrieves it on Windows 10 */
    CHECK(mq_peek(&m, NULL, WM_USER, 0x7FFF, PM_REMOVE));
    EQ(m.message, WM_QUIT);
    EQ(m.wParam, 4);
    CHECK(!mq_peek(&m, NULL, WM_QUIT, WM_QUIT, PM_REMOVE));            /* consumed: exactly one WM_QUIT per PostQuitMessage */
}

QTEST(msg_queue_post_peek_postquit_getmessage_returns_zero) {
    MSG m; memset(&m, 0, sizeof m);
    U(PostQuitMessage)(17);
    MSG t;
    if (!mq_peek(&t, NULL, 0, 0, PM_NOREMOVE)) { t_note("quit request not visible to PeekMessage; not calling GetMessage"); mq_peek(&t, NULL, 0, 0, PM_REMOVE); return; }
    UINT_PTR wd = U(SetTimer)(NULL, 0, 3000, NULL);   /* watchdog: WM_QUIT outranks WM_TIMER, a regression cannot hang */
    EQ(U(GetMessageW)(&m, NULL, 0, 0), 0);
    EQ(m.message, WM_QUIT);
    EQ(m.wParam, 17);
    U(KillTimer)(NULL, wd);
}

QTEST(msg_queue_post_peek_postquit_sets_postmessage_wake_bits) {
    /* PostQuitMessage sets the post-message wake bits */
    U(PostQuitMessage)(1);
    DWORD s = mq_status(QS_POSTMESSAGE | QS_ALLPOSTMESSAGE);
    CHECKF(((s >> 16) & (QS_POSTMESSAGE | QS_ALLPOSTMESSAGE)) == (QS_POSTMESSAGE | QS_ALLPOSTMESSAGE), "pending 0x%lx", s >> 16);
    CHECKF((s & (QS_POSTMESSAGE | QS_ALLPOSTMESSAGE)) == (QS_POSTMESSAGE | QS_ALLPOSTMESSAGE), "new 0x%lx", s & 0xFFFF);
    EQ(mq_wait(0, NULL, 0, QS_POSTMESSAGE, MWMO_INPUTAVAILABLE), (int)WAIT_OBJECT_0);
    MSG m; mq_peek(&m, NULL, 0, 0, PM_REMOVE);
}

QTEST(msg_queue_post_peek_postquit_is_per_thread) {
    mq_helper s; targ a = { MODE_CREATE_QUEUE_WAIT_MSG };
    if (!mq_helper_start(&s, thr_fn, &a)) { t_note("thread"); return; }
    WaitForSingleObject(s.ready, 5000);
    U(PostQuitMessage)(5);                             /* quit flag lives on this thread's THREADINFO */
    SetEvent(s.go);
    WaitForSingleObject(s.th, 5000);
    EQ(a.gm_ret, 0);
    MSG m; CHECK(mq_peek(&m, NULL, 0, 0, PM_REMOVE));
    EQ(m.message, WM_QUIT);
    mq_helper_finish(&s);
}

/* ---- GetMessage ----------------------------------------------------------------------------- */

QTEST(msg_queue_post_peek_getmessage_returns_positive_for_message) {
    HWND h = mq_win(); QNEED(h);
    MSG m;
    CHECK(mq_post(h, M1, 12, 34));
    LONG r = mq_getmsg(&m, h, 0, 0);
    CHECK(r > 0);
    EQ(m.message, M1);
    EQ(m.wParam, 12);
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_getmessage_filtered_by_window_and_range) {
    HWND a = mq_win(), b = mq_win(); QNEED(a); QNEED(b);
    MSG m;
    CHECK(mq_post(a, M1, 1, 0)); CHECK(mq_post(b, M2, 2, 0));
    CHECK(mq_getmsg(&m, b, M2, M2) > 0);
    CHECK(m.hwnd == b); EQ(m.wParam, 2);
    CHECK(mq_getmsg(&m, NULL, M1, M1) > 0);
    CHECK(m.hwnd == a);
    U(DestroyWindow)(a); U(DestroyWindow)(b);
}

QTEST(msg_queue_post_peek_getmessage_wm_char_wparam_masked_in_user32) {
    HWND h = mq_win(); QNEED(h);
    MSG m;
    CHECK(mq_post(h, WM_CHAR, 0xABCD0058ull, 0));
    CHECK(mq_getmsg(&m, h, 0, 0) > 0);
    EQ(m.wParam, 0x58);                        /* user32!GetMessageW: wParam &= 0xffff for WM_CHAR */
    U(DestroyWindow)(h);
}

/* ---- TranslateMessage ------------------------------------------------------------------------ */

QTEST(msg_queue_post_peek_translate_non_key_messages_return_false) {
    HWND h = mq_win(); QNEED(h);
    static const UINT ids[] = { WM_NULL, WM_USER, WM_CHAR, WM_SYSCHAR, WM_PAINT, WM_LBUTTONDOWN, WM_TIMER, WM_QUIT, WM_COMMAND };
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        MSG m; memset(&m, 0, sizeof m); m.hwnd = h; m.message = ids[i];
        SetLastError(0);
        CHECKF(!U(TranslateMessage)(&m), "id 0x%x", ids[i]);   /* only WM_KEYDOWN/KEYUP/SYSKEYDOWN/SYSKEYUP reach the kernel */
    }
    EQ(mq_drain(), 0);
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_translate_message_bits_error) {
    MSG m; memset(&m, 0, sizeof m); m.message = 0x20000;
    SetLastError(0);
    CHECK(!U(TranslateMessage)(&m));
    EXPECT_ERR(ERROR_INVALID_PARAMETER);              /* user32!TranslateMessage rejects ids with bits in 0xfffe0000 */
    m.message = 0x80000100;
    SetLastError(0);
    CHECK(!U(TranslateMessage)(&m));
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}

QTEST(msg_queue_post_peek_translate_return_keydown_posts_wm_char) {
    HWND h = mq_win(); QNEED(h);
    MSG m; memset(&m, 0, sizeof m);
    m.hwnd = h; m.message = WM_KEYDOWN; m.wParam = VK_RETURN; m.lParam = (0x1C << 16) | 1; m.time = GetTickCount();
    BOOL r = U(TranslateMessage)(&m);
    CHECK(r);
    MSG c;
    if (mq_peek(&c, h, WM_CHAR, WM_CHAR, PM_REMOVE)) {
        CHECK(c.hwnd == h);
        SOFT_EQ(c.wParam, 0x0D, "WM_CHAR for VK_RETURN");
    } else t_note("TranslateMessage returned %d but no WM_CHAR queued", r);
    mq_drain();
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_translate_direct_invalid_window_returns_false) {
    QNEED(UN(NtUserTranslateMessage));
    MSG m; memset(&m, 0, sizeof m);
    m.message = WM_KEYDOWN; m.wParam = VK_RETURN; m.lParam = (0x1C << 16) | 1;
    m.hwnd = NULL;
    EQ(UN(NtUserTranslateMessage)(&m, 0), 0);  /* ValidateHwnd(NULL) fails before translation */
    m.hwnd = (HWND)(ULONG_PTR)0xDEAD0001;
    EQ(UN(NtUserTranslateMessage)(&m, 0), 0);
    EQ(mq_drain(), 0);
}

/* ---- DispatchMessage ------------------------------------------------------------------------- */

static int disp_hook(HWND h, UINT m, WPARAM w, LPARAM l, LRESULT *r) { if (m == M1) { *r = 0x1234; return 1; } return 0; }

QTEST(msg_queue_post_peek_dispatch_calls_window_proc_with_fields) {
    HWND h = mq_win(); QNEED(h);
    mq_hook = disp_hook;
    MSG m; memset(&m, 0, sizeof m);
    m.hwnd = h; m.message = M1; m.wParam = 0xAABBCCDD11223344ull; m.lParam = 0x1122334455667788ll;
    mq_reset(); mq_hook = disp_hook;
    EQ(U(DispatchMessageW)(&m), 0x1234);       /* wndproc result is returned */
    int i = mq_find(M1, 0);
    CHECK(i >= 0);
    if (i >= 0) {
        CHECK(mq_log[i].h == h);
        EQ(mq_log[i].w, m.wParam);
        EQ(mq_log[i].l, m.lParam);
        EQ(mq_log[i].tid, GetCurrentThreadId());
    }
    EQ(mq_count(M1), 1);
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_dispatch_posted_message_roundtrip) {
    HWND h = mq_win(); QNEED(h);
    mq_hook = disp_hook;
    MSG m;
    CHECK(mq_post(h, M1, 21, 22));
    CHECK(mq_getmsg(&m, h, 0, 0) > 0);
    EQ(mq_count(M1), 0);                       /* retrieving does not run the wndproc */
    EQ(U(DispatchMessageW)(&m), 0x1234);
    EQ(mq_count(M1), 1);
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_dispatch_destroyed_window_is_noop) {
    HWND h = mq_win(); QNEED(h);
    U(DestroyWindow)(h);
    MSG m; memset(&m, 0, sizeof m); m.hwnd = h; m.message = M1;
    mq_reset(); mq_hook = disp_hook;
    EQ(U(DispatchMessageW)(&m), 0);
    EQ(mq_count(M1), 0);
}

QTEST(msg_queue_post_peek_dispatch_direct_message_bits_rejected) {
    QNEED(UN(NtUserDispatchMessage));
    HWND h = mq_win(); QNEED(h);
    MSG m; memset(&m, 0, sizeof m); m.hwnd = h;
    static const UINT bad[] = { 0x20000, 0x80000000, 0xFFFFFFFF };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        m.message = bad[i];
        SetLastError(0);
        EQ((int)UN(NtUserDispatchMessage)(&m), 0);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
    EQ(mq_count(0x20000), 0);
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_dispatch_direct_reaches_window_proc) {
    QNEED(UN(NtUserDispatchMessage));
    HWND h = mq_win(); QNEED(h);
    mq_reset(); mq_hook = disp_hook;
    MSG m; memset(&m, 0, sizeof m); m.hwnd = h; m.message = M1; m.wParam = 5; m.lParam = 6;
    LRESULT r = UN(NtUserDispatchMessage)(&m);       /* dispatch calls the client window proc back */
    EQ(mq_count(M1), 1);
    SOFT_EQ(r, 0x1234, "NtUserDispatchMessage result");
    U(DestroyWindow)(h);
}

/* ---- message time, position, extra info ------------------------------------------------------ */

static int tick_close(DWORD a, DWORD b, int slack) { int d = (int)(a - b); return d >= -slack && d <= slack; }

QTEST(msg_queue_post_peek_msgtime_matches_retrieved_message) {
    HWND h = mq_win(); QNEED(h);
    MSG m;
    DWORD t0 = GetTickCount();
    CHECK(mq_post(h, M1, 0, 0));
    DWORD t1 = GetTickCount();
    CHECK(mq_getmsg(&m, h, 0, 0) > 0);
    EQ((DWORD)U(GetMessageTime)(), m.time);
    CHECKF(tick_close(m.time, t0, 1000) && tick_close(m.time, t1, 1000), "time %lu ticks %lu..%lu", m.time, t0, t1);
    if (UN(NtUserGetThreadState)) EQ((DWORD)UN(NtUserGetThreadState)(TS_MSGTIME), m.time);
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_msgtime_follows_each_retrieval) {
    HWND h = mq_win(); QNEED(h);
    MSG m;
    CHECK(mq_post(h, M1, 0, 0));
    CHECK(mq_getmsg(&m, h, 0, 0) > 0);
    DWORD t_a = m.time;
    Sleep(120);
    CHECK(mq_post(h, M2, 0, 0));
    CHECK(mq_getmsg(&m, h, 0, 0) > 0);
    EQ((DWORD)U(GetMessageTime)(), m.time);
    CHECKF((int)(m.time - t_a) >= 50, "second message %lu ms after first", m.time - t_a);   /* generous: posted 120 ms later */
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_msgpos_matches_retrieved_message_pt) {
    HWND h = mq_win(); QNEED(h);
    MSG m;
    CHECK(mq_post(h, M1, 0, 0));
    CHECK(mq_peek(&m, h, 0, 0, PM_REMOVE));
    DWORD pos = U(GetMessagePos)();
    CHECKF(pos == (DWORD)MAKELONG(m.pt.x, m.pt.y), "GetMessagePos 0x%lx vs msg.pt (%ld,%ld)", pos, m.pt.x, m.pt.y);
    /* msg.pt is the cursor position when the message was posted; the cursor is not moved here, so it still matches.
       The position itself depends on the session (not asserted or printed). */
    POINT cur;
    if (!U(GetCursorPos) || !U(GetCursorPos)(&cur)) t_skip("GetCursorPos unavailable");
    else {
        t_info("msg.pt (%ld,%ld) cursor (%ld,%ld)", m.pt.x, m.pt.y, cur.x, cur.y);
        CHECK(cur.x == m.pt.x && cur.y == m.pt.y);
    }
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_extrainfo_set_returns_previous_value) {
    static const LPARAM vals[] = { 0, 1, 0x12345678, 0x7FFFFFFF, 0xFFFFFFFF, (LPARAM)0xFF515700 };
    LPARAM prev = U(GetMessageExtraInfo)();
    LPARAM cur = prev;
    for (unsigned i = 0; i < sizeof vals / sizeof vals[0]; i++) {
        LPARAM r = U(SetMessageExtraInfo)(vals[i]);
        EQ(r, cur);                            /* _SetMessageExtraInfo returns the old queue value */
        EQ(U(GetMessageExtraInfo)(), vals[i]);
        if (UN(NtUserGetThreadState)) EQ(UN(NtUserGetThreadState)(TS_EXTRAINFO), vals[i]);
        cur = vals[i];
    }
    U(SetMessageExtraInfo)(prev);
}

QTEST(msg_queue_post_peek_extrainfo_full_pointer_width) {
    LPARAM prev = U(SetMessageExtraInfo)(0x1122334455667788ll);
    LPARAM got = U(GetMessageExtraInfo)();
    if (got != 0x1122334455667788ll) t_note("GetMessageExtraInfo kept 0x%llx of 0x1122334455667788", (unsigned long long)got);
    U(SetMessageExtraInfo)(prev);
}

static DWORD WINAPI extra_other_thread(LPVOID p) { *(LPARAM *)p = U(GetMessageExtraInfo)(); return 0; }
QTEST(msg_queue_post_peek_extrainfo_is_per_input_queue) {
    LPARAM prev = U(SetMessageExtraInfo)(0x5A5A1234);
    LPARAM seen = 0xBAD;
    HANDLE t = CreateThread(NULL, 0, extra_other_thread, &seen, 0, NULL);
    if (t) { WaitForSingleObject(t, 5000); CloseHandle(t); }
    CHECKF(seen != 0x5A5A1234, "another thread's queue saw 0x%llx", (unsigned long long)seen);
    U(SetMessageExtraInfo)(prev);
}

/* ---- NtUserGetThreadState selectors -------------------------------------------------------------- */

QTEST(msg_queue_post_peek_thread_state_unknown_selectors_are_zero) {
    QNEED(UN(NtUserGetThreadState));
    static const LONG sel[] = { 0x10, 0x11, 0x20, 0x100, 0x7FFFFFFF, -1, -100, (LONG)0x80000000 };
    for (unsigned i = 0; i < sizeof sel / sizeof sel[0]; i++) {
        SetLastError(0);
        EQ(UN(NtUserGetThreadState)(sel[i]), 0);
        EXPECT_ERR(0);
    }
}

QTEST(msg_queue_post_peek_thread_state_fresh_thread_queue_is_empty) {
    QNEED(UN(NtUserGetThreadState));
    EQ(UN(NtUserGetThreadState)(TS_ACTIVE), 0);
    EQ(UN(NtUserGetThreadState)(TS_FOCUS), 0);
    EQ(UN(NtUserGetThreadState)(TS_CAPTURE), 0);
    EQ(UN(NtUserGetThreadState)(TS_INSENDMSG), 0);
    EQ(UN(NtUserGetThreadState)(TS_INPUTSTATE), 0);
    EQ(UN(NtUserGetThreadState)(TS_CHANGEBITS) & (QS_POSTMESSAGE | QS_SENDMESSAGE | QS_TIMER), 0);
}

QTEST(msg_queue_post_peek_thread_state_constant_selectors) {
    QNEED(UN(NtUserGetThreadState));
    EQ(UN(NtUserGetThreadState)(0xD), 0);      /* logon-thread test: false for an ordinary process */
    EQ(UN(NtUserGetThreadState)(0xE), 1);      /* selector 0xe falls into the "return 1" arm */
}

QTEST(msg_queue_post_peek_thread_state_capture_matches_getcapture) {
    QNEED(UN(NtUserGetThreadState)); QNEED(U(SetCapture)); QNEED(U(ReleaseCapture)); QNEED(U(GetCapture));
    HWND h = mq_win(); QNEED(h);
    U(SetCapture)(h);
    if (U(GetCapture)() != h) { t_note("SetCapture did not take effect for a background thread"); U(DestroyWindow)(h); return; }
    EQ(UN(NtUserGetThreadState)(TS_CAPTURE), (ULONG_PTR)h);
    CHECK(U(ReleaseCapture)());
    CHECK(U(GetCapture)() == NULL);
    EQ(UN(NtUserGetThreadState)(TS_CAPTURE), 0);
    U(DestroyWindow)(h);
}

/* ---- syscall numbers of the entry points exercised here (19041) --------------------------------- */

QTEST(msg_queue_post_peek_stub_service_numbers) {
    static const struct { const char *n; long num; } t[] = {
        { "NtUserGetThreadState", 4096 }, { "NtUserPeekMessage", 4097 }, { "NtUserGetMessage", 4102 },
        { "NtUserWaitMessage", 4108 }, { "NtUserTranslateMessage", 4109 }, { "NtUserPostMessage", 4111 },
        { "NtUserDispatchMessage", 4150 }, { "NtUserPostThreadMessage", 4190 }, { "NtUserAttachThreadInput", 4319 },
        { "NtUserGetQueueStatus", 5170 }, { "NtUserMsgWaitForMultipleObjectsEx", 5254 }, { "NtUserQuerySendMessage", 5273 },
    };
    if (t_is_wine()) { t_note("Wine's win32u stubs are not win32k syscall stubs"); return; }
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
        FARPROC p = t_proc_in("win32u.dll", t[i].n);
        CHECKF(p != NULL, "%s missing", t[i].n);
        if (!p) continue;
        CHECKF(k_stub_valid(p), "%s prologue", t[i].n);
        if (k_stub_valid(p)) EQ(k_stub_number(p), t[i].num);
    }
}
