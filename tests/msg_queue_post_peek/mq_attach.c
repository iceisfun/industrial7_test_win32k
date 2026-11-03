/* AttachThreadInput: two GUI threads sharing one input queue.
 *
 * win32kfull!NtUserAttachThreadInput: both thread ids must resolve through PtiFromThreadId and
 * both must have a desktop (+0x1c8) - otherwise ERROR_INVALID_PARAMETER. zzzAttachThreadInput then
 * re-points the threads at one shared queue, which is visible through the queue-owned state:
 * capture window (NtUserGetThreadState(2)) and the message extra-info word.
 */
#include "mq.h"

#define M1 (WM_USER + 0x241)

enum { C_NONE, C_CAPTURE, C_RELEASE, C_SETEXTRA, C_SAMPLE, C_EXIT };
typedef struct {
    int mode;
    HWND h;                 /* window owned by this thread (mode 1) */
    DWORD tid;
    HANDLE ready, go, done;
    volatile LONG cmd;
    volatile ULONG_PTR arg;
    volatile ULONG_PTR capture_seen;
    volatile LPARAM extra_seen;
    volatile LONG ok;
} athr;

static DWORD WINAPI athr_fn(LPVOID p) {
    athr *a = p;
    if (a->mode == 1) a->h = mq_win();             /* owner thread: creates the window it will capture with */
    else { MSG m; U(PeekMessageW)(&m, NULL, 0, 0, PM_NOREMOVE); }
    a->tid = GetCurrentThreadId();
    SetEvent(a->ready);
    for (;;) {
        WaitForSingleObject(a->go, 10000);
        LONG c = a->cmd;
        if (c == C_EXIT) break;
        if (c == C_CAPTURE) { U(SetCapture)(a->h); a->ok = 1; }
        else if (c == C_RELEASE) a->ok = U(ReleaseCapture)();
        else if (c == C_SETEXTRA) U(SetMessageExtraInfo)((LPARAM)a->arg);
        a->capture_seen = UN(NtUserGetThreadState) ? UN(NtUserGetThreadState)(TS_CAPTURE) : 0;
        a->extra_seen = U(GetMessageExtraInfo)();
        SetEvent(a->done);
    }
    if (a->mode == 1 && a->h) U(DestroyWindow)(a->h);
    return 0;
}
static HANDLE spawn(athr *a, int mode) {
    memset(a, 0, sizeof *a);
    a->mode = mode;
    a->ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    a->go = CreateEventW(NULL, FALSE, FALSE, NULL);
    a->done = CreateEventW(NULL, FALSE, FALSE, NULL);
    HANDLE t = CreateThread(NULL, 0, athr_fn, a, 0, NULL);
    WaitForSingleObject(a->ready, 5000);
    return t;
}
static void cmd(athr *a, int c, ULONG_PTR arg) { a->cmd = c; a->arg = arg; SetEvent(a->go); WaitForSingleObject(a->done, 5000); }
static void reap(athr *a, HANDLE t) {
    a->cmd = C_EXIT; SetEvent(a->go);
    WaitForSingleObject(t, 5000); CloseHandle(t);
    CloseHandle(a->ready); CloseHandle(a->go); CloseHandle(a->done);
}

QTEST(msg_queue_post_peek_attach_invalid_thread_ids) {
    QNEED(U(AttachThreadInput));
    DWORD me = GetCurrentThreadId();
    HWND h = mq_win(); QNEED(h);                    /* make sure this thread has a queue and a desktop */
    static const DWORD bad[] = { 0, 0xFFFFFFFF, 0x7FFFFFF0, 4 };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(0);
        CHECKF(!U(AttachThreadInput)(bad[i], me, TRUE), "idAttach 0x%lx", bad[i]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
        SetLastError(0);
        CHECKF(!U(AttachThreadInput)(me, bad[i], TRUE), "idAttachTo 0x%lx", bad[i]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
        SetLastError(0);
        CHECKF(!U(AttachThreadInput)(bad[i], me, FALSE), "detach idAttach 0x%lx", bad[i]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
    U(DestroyWindow)(h);
}

static DWORD WINAPI noqueue_fn(LPVOID p) { SetEvent(((HANDLE *)p)[0]); WaitForSingleObject(((HANDLE *)p)[1], 8000); return 0; }
QTEST(msg_queue_post_peek_attach_thread_without_queue_fails) {
    QNEED(U(AttachThreadInput));
    HWND h = mq_win(); QNEED(h);
    HANDLE ev[2] = { CreateEventW(NULL, TRUE, FALSE, NULL), CreateEventW(NULL, TRUE, FALSE, NULL) };
    DWORD tid;
    HANDLE t = CreateThread(NULL, 0, noqueue_fn, ev, 0, &tid);
    WaitForSingleObject(ev[0], 5000);
    SetLastError(0);
    BOOL r = U(AttachThreadInput)(tid, GetCurrentThreadId(), TRUE);   /* PtiFromThreadId(tid) is NULL: it never entered win32k */
    if (!r) EXPECT_ERR(ERROR_INVALID_PARAMETER);
    else { t_note("thread without win32k calls could be attached"); U(AttachThreadInput)(tid, GetCurrentThreadId(), FALSE); }
    SetEvent(ev[1]);
    WaitForSingleObject(t, 5000);
    CloseHandle(t); CloseHandle(ev[0]); CloseHandle(ev[1]);
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_attach_self_fails) {
    QNEED(U(AttachThreadInput));
    HWND h = mq_win(); QNEED(h);
    SetLastError(0);
    CHECK(!U(AttachThreadInput)(GetCurrentThreadId(), GetCurrentThreadId(), TRUE));
    t_note("self attach last error %lu", GetLastError());
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_attach_shares_capture_between_threads) {
    QNEED(U(AttachThreadInput)); QNEED(UN(NtUserGetThreadState)); QNEED(U(SetCapture));
    athr own, oth;
    HANDLE t1 = spawn(&own, 1), t2 = spawn(&oth, 0);
    if (!own.h) { t_note("window creation failed"); reap(&own, t1); reap(&oth, t2); return; }
    cmd(&own, C_CAPTURE, 0);
    if (own.capture_seen != (ULONG_PTR)own.h) { t_note("SetCapture did not take effect for a background thread"); reap(&own, t1); reap(&oth, t2); return; }
    cmd(&oth, C_NONE, 0);
    EQ(oth.capture_seen, 0);                 /* separate queues: the other thread has no capture */
    SetLastError(0);
    BOOL at = U(AttachThreadInput)(oth.tid, own.tid, TRUE);
    CHECKF(at, "attach failed, error %lu", GetLastError());
    if (at) {
        cmd(&oth, C_NONE, 0);
        EQ(oth.capture_seen, (ULONG_PTR)own.h);   /* shared input queue: capture is queue state */
        cmd(&own, C_RELEASE, 0);
        cmd(&oth, C_NONE, 0);
        EQ(oth.capture_seen, 0);
        cmd(&own, C_CAPTURE, 0);
        SetLastError(0);
        CHECK(U(AttachThreadInput)(oth.tid, own.tid, FALSE));
        cmd(&oth, C_NONE, 0);
        SOFT_EQ(oth.capture_seen, 0, "detached thread capture");     /* back to its own queue */
        cmd(&own, C_NONE, 0);
        SOFT_EQ(own.capture_seen, (ULONG_PTR)own.h, "owner capture after detach");
    }
    cmd(&own, C_RELEASE, 0);
    reap(&own, t1); reap(&oth, t2);
}

QTEST(msg_queue_post_peek_attach_shares_message_extra_info) {
    QNEED(U(AttachThreadInput));
    athr a, b;
    HANDLE t1 = spawn(&a, 0), t2 = spawn(&b, 0);
    cmd(&a, C_SETEXTRA, 0x13572468);
    cmd(&b, C_SETEXTRA, 0x24681357);
    EQ(a.extra_seen, 0x13572468);
    EQ(b.extra_seen, 0x24681357);            /* independent while not attached */
    BOOL at = U(AttachThreadInput)(b.tid, a.tid, TRUE);
    CHECKF(at, "attach error %lu", GetLastError());
    if (at) {
        cmd(&b, C_SETEXTRA, 0x0BADF00D);
        cmd(&a, C_NONE, 0);
        EQ(a.extra_seen, 0x0BADF00D);        /* the extra-info word lives in the shared queue */
        CHECK(U(AttachThreadInput)(b.tid, a.tid, FALSE));
    }
    reap(&a, t1); reap(&b, t2);
}

QTEST(msg_queue_post_peek_attach_detach_when_not_attached_probe) {
    QNEED(U(AttachThreadInput));
    athr a, b;
    HANDLE t1 = spawn(&a, 0), t2 = spawn(&b, 0);
    SetLastError(0);
    BOOL r = U(AttachThreadInput)(b.tid, a.tid, FALSE);
    t_note("detach of unattached pair: %d (error %lu)", r, GetLastError());
    r = U(AttachThreadInput)(b.tid, a.tid, TRUE);
    CHECK(r);
    SetLastError(0);
    BOOL r2 = U(AttachThreadInput)(b.tid, a.tid, TRUE);
    t_note("second attach of same pair: %d (error %lu)", r2, GetLastError());
    U(AttachThreadInput)(b.tid, a.tid, FALSE);
    reap(&a, t1); reap(&b, t2);
}
