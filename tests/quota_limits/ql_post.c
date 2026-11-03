/* Per-thread posted message limit. win32kfull!_PostTransformableMessageExtended (PostMessage path) and
 * AllocQEntryEx compare the RECEIVING thread's posted list count (tagTHREADINFO+0x338, tagMLIST.cMsgs)
 * with gUserPostMessageLimit and fail with UserSetLastError(0x718 = ERROR_NOT_ENOUGH_QUOTA). The count
 * is per receiving thread, is decremented when a message is removed from the queue, and is not touched by
 * sent messages. NtUserPostMessage / NtUserPostThreadMessage reject (msg & 0xfffe0000) with 0x57. */
#include "ql.h"

static volatile LONG g_proc_hits;
static LRESULT CALLBACK qproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_APP + 9) { InterlockedIncrement(&g_proc_hits); return 0x5151; }
    return U(DefWindowProcW)(h, m, w, l);
}
static const WCHAR *proc_class(void) {
    static WCHAR name[64];
    if (!name[0]) {
        WNDCLASSEXW wc; memset(&wc, 0, sizeof wc);
        _snwprintf(name, 64, L"win32kt_qlp_%lu", GetCurrentProcessId());
        wc.cbSize = sizeof wc; wc.lpfnWndProc = qproc; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = name;
        if (!U(RegisterClassExW)(&wc)) name[0] = 0;
    }
    return name[0] ? name : NULL;
}
static HWND proc_window(void) {
    const WCHAR *c = proc_class();
    if (!c) return NULL;
    return U(CreateWindowExW)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, c, L"qlp", WS_POPUP, 0, 0, 1, 1, HWND_MESSAGE, NULL, GetModuleHandleW(NULL), NULL);
}

static unsigned post_prep(struct ql_quota *pl) {
    if (!ql_connect()) { t_skip("win32k not reachable"); return 0; }
    *pl = ql_post_limit();
    if (!pl->std) t_note("USERPostMessageLimit configured as %u (registry %u)", pl->q, pl->reg);
    if (pl->q > 20000) { t_skip("post limit %u too large for the test budget", pl->q); return 0; }
    return pl->q;
}

/* mode 0: PostMessage(hwnd), 1: PostThreadMessage(tid), 2: alternate */
static BOOL post_one(int mode, HWND hwnd, DWORD tid, int i) {
    UINT msg = WM_APP + 1 + (i & 7);          /* never WM_APP+9, which the test window procedure answers */
    if (mode == 1 || (mode == 2 && (i & 1))) return U(PostThreadMessageW)(tid, msg, (WPARAM)i, 0);
    return U(PostMessageW)(hwnd, msg, (WPARAM)i, 0);
}

/* Posts until refused or `cap` reached. Returns the number accepted; *err = last error of the refusal. */
static int post_fill(int mode, HWND hwnd, DWORD tid, int cap, DWORD *err, int *refused) {
    int n = 0;
    *refused = 0; *err = 0;
    for (int i = 0; i < cap; i++) {
        SetLastError(0);
        if (!post_one(mode, hwnd, tid, i)) { *err = GetLastError(); *refused = 1; break; }
        n++;
    }
    return n;
}

/* Removes every posted message from this thread's queue; returns how many. Bounded. */
static int post_drain(int bound) {
    MSG m; int n = 0;
    while (n < bound && U(PeekMessageW)(&m, NULL, 0, 0, PM_REMOVE)) {
        if (m.message > WM_APP && m.message < WM_APP + 9) n++;
    }
    return n;
}

static void post_check_boundary(const struct ql_quota *pl, int S, DWORD err, int refused, const char *what) {
    if (!refused) {
        if (pl->std) QCF(t_is_wine(), "%s: no refusal after %d posts", what, S);
        return;
    }
    QCF(err == ERROR_NOT_ENOUGH_QUOTA, "%s: refusal last error %lu want 1816", what, err);
    QCF((unsigned)S <= pl->q, "%s: %d posts accepted, limit %u", what, S, pl->q);
    QCF((unsigned)S + 8 >= pl->q, "%s: only %d posts accepted, limit %u", what, S, pl->q);
    t_note("%s: %d posts accepted (limit %u)", what, S, pl->q);
}

static void post_own_window(void) {
    struct ql_quota pl; unsigned L = post_prep(&pl);
    if (!L) return;
    HWND w = proc_window();
    if (!w) { t_skip("no window"); return; }
    U(PeekMessageW)(&(MSG){0}, NULL, 0, 0, PM_NOREMOVE);       /* make sure the queue exists */
    DWORD err; int refused;
    int S = post_fill(0, w, 0, (int)L + 200, &err, &refused);
    post_check_boundary(&pl, S, err, refused, "PostMessage");
    if (refused) {
        for (int r = 0; r < 3; r++) {
            SetLastError(0);
            BOOL ok = U(PostMessageW)(w, WM_APP + 1, 0, 0);
            QC(!ok); EXPECT_ERR(ERROR_NOT_ENOUGH_QUOTA);
        }
        /* GetQueueStatus: LOWORD = message types currently queued (QS_POSTMESSAGE = 0x8) */
        QC((U(GetQueueStatus)(QS_POSTMESSAGE) & 0xffff) & QS_POSTMESSAGE);
    }
    int d = post_drain(S + 1000);
    QCF(d == S, "drained %d messages, %d were accepted", d, S);
    SetLastError(0);
    QC(U(PostMessageW)(w, WM_APP + 1, 0, 0));          /* the quota is released by removing messages */
    QC(post_drain(10) == 1);
    QC(U(DestroyWindow)(w));
}
QL_SCENARIO(quota_limits_post_own_window_limit, post_own_window())

static void post_thread_msg(void) {
    struct ql_quota pl; unsigned L = post_prep(&pl);
    if (!L) return;
    U(PeekMessageW)(&(MSG){0}, NULL, 0, 0, PM_NOREMOVE);
    DWORD tid = GetCurrentThreadId(), err; int refused;
    int S = post_fill(1, NULL, tid, (int)L + 200, &err, &refused);
    post_check_boundary(&pl, S, err, refused, "PostThreadMessage");
    if (refused) {
        SetLastError(0);
        QC(!U(PostThreadMessageW)(tid, WM_APP + 1, 0, 0)); EXPECT_ERR(ERROR_NOT_ENOUGH_QUOTA);
    }
    QCF(post_drain(S + 1000) == S, "drain count");
    QC(U(PostThreadMessageW)(tid, WM_APP + 1, 0, 0));
    QC(post_drain(10) == 1);
}
QL_SCENARIO(quota_limits_post_thread_message_limit, post_thread_msg())

static void post_mixed_styles(void) {
    struct ql_quota pl; unsigned L = post_prep(&pl);
    if (!L) return;
    HWND w = proc_window();
    if (!w) { t_skip("no window"); return; }
    U(PeekMessageW)(&(MSG){0}, NULL, 0, 0, PM_NOREMOVE);
    DWORD err; int refused;
    /* window posts and thread posts feed the same per-thread list, so their sum hits the limit */
    int S = post_fill(2, w, GetCurrentThreadId(), (int)L + 200, &err, &refused);
    post_check_boundary(&pl, S, err, refused, "alternating");
    QCF(post_drain(S + 1000) == S, "drain count");
    QC(U(DestroyWindow)(w));
}
QL_SCENARIO(quota_limits_post_shared_between_styles, post_mixed_styles())

static void post_send_unaffected(void) {
    struct ql_quota pl; unsigned L = post_prep(&pl);
    if (!L) return;
    HWND w = proc_window();
    if (!w) { t_skip("no window"); return; }
    U(PeekMessageW)(&(MSG){0}, NULL, 0, 0, PM_NOREMOVE);
    DWORD err; int refused;
    int S = post_fill(0, w, 0, (int)L + 200, &err, &refused);
    if (refused) {
        /* sent messages never enter the posted list: same-thread SendMessage calls the procedure directly */
        LONG before = g_proc_hits;
        QC(U(SendMessageW)(w, WM_APP + 9, 0, 0) == 0x5151);
        QC(g_proc_hits == before + 1);
        DWORD_PTR res = 0;
        QC(U(SendMessageTimeoutW)(w, WM_APP + 9, 0, 0, SMTO_NORMAL, 1000, &res) && res == 0x5151);
        QC(U(SendNotifyMessageW)(w, WM_APP + 9, 0, 0));
        QC(g_proc_hits == before + 3);
        /* peeking without removing does not change the count: still full */
        MSG m; QC(U(PeekMessageW)(&m, NULL, WM_APP, WM_APP + 8, PM_NOREMOVE));
        SetLastError(0);
        QC(!U(PostMessageW)(w, WM_APP + 1, 0, 0)); EXPECT_ERR(ERROR_NOT_ENOUGH_QUOTA);
        /* removing exactly one message frees exactly one slot */
        QC(U(PeekMessageW)(&m, NULL, WM_APP, WM_APP + 8, PM_REMOVE));
        QC(U(PostMessageW)(w, WM_APP + 1, 0, 0));
        SetLastError(0);
        QC(!U(PostMessageW)(w, WM_APP + 1, 0, 0)); EXPECT_ERR(ERROR_NOT_ENOUGH_QUOTA);
    } else if (pl.std) QCF(t_is_wine(), "no refusal");
    post_drain(S + 1000);
    U(DestroyWindow)(w);
}
QL_SCENARIO(quota_limits_post_full_queue_send_and_slot_release, post_send_unaffected())

static void post_cycles(void) {
    struct ql_quota pl; unsigned L = post_prep(&pl);
    if (!L) return;
    HWND w = proc_window();
    if (!w) { t_skip("no window"); return; }
    U(PeekMessageW)(&(MSG){0}, NULL, 0, 0, PM_NOREMOVE);
    int first = -1;
    for (int cycle = 0; cycle < 3; cycle++) {
        DWORD err; int refused;
        int S = post_fill(0, w, 0, (int)L + 200, &err, &refused);
        if (!refused) { if (pl.std) QCF(t_is_wine(), "cycle %d: no refusal", cycle); post_drain(S + 1000); break; }
        if (first < 0) first = S;
        QCF(S == first, "cycle %d accepted %d, first cycle %d", cycle, S, first);
        QCF(post_drain(S + 1000) == S, "cycle %d drain", cycle);
    }
    QC(U(DestroyWindow)(w));
}
QL_SCENARIO(quota_limits_post_refill_after_drain_is_stable, post_cycles())

/* ---- limit is per receiving thread ---- */
struct pthr { HANDLE ready, done; HWND w; DWORD tid; int use_window; };
static DWORD WINAPI pthr_main(void *p) {
    struct pthr *t = p;
    t->tid = GetCurrentThreadId();
    if (t->use_window) t->w = proc_window();
    U(PeekMessageW)(&(MSG){0}, NULL, 0, 0, PM_NOREMOVE);
    SetEvent(t->ready);
    WaitForSingleObject(t->done, 30000);       /* never pumps: the queue stays full */
    if (t->w) U(DestroyWindow)(t->w);
    return 0;
}
static HANDLE pthr_start(struct pthr *t, int use_window) {
    memset(t, 0, sizeof *t);
    t->ready = CreateEventA(NULL, TRUE, FALSE, NULL);
    t->done = CreateEventA(NULL, TRUE, FALSE, NULL);
    t->use_window = use_window;
    HANDLE h = CreateThread(NULL, 0, pthr_main, t, 0, NULL);
    if (!h || WaitForSingleObject(t->ready, 10000) != WAIT_OBJECT_0) { if (h) { SetEvent(t->done); CloseHandle(h); } return NULL; }
    return h;
}
static void pthr_stop(struct pthr *t, HANDLE h) {
    SetEvent(t->done);
    WaitForSingleObject(h, 10000);
    CloseHandle(h); CloseHandle(t->ready); CloseHandle(t->done);
}

static void post_other_thread(int by_window) {
    struct ql_quota pl; unsigned L = post_prep(&pl);
    if (!L) return;
    struct pthr t; HANDLE h = pthr_start(&t, by_window);
    if (!h || (by_window && !t.w)) { t_skip("worker thread setup failed"); if (h) pthr_stop(&t, h); return; }
    HWND mine = proc_window();
    U(PeekMessageW)(&(MSG){0}, NULL, 0, 0, PM_NOREMOVE);
    DWORD err; int refused;
    int S = post_fill(by_window ? 0 : 1, t.w, t.tid, (int)L + 200, &err, &refused);
    post_check_boundary(&pl, S, err, refused, by_window ? "post to other thread window" : "post to other thread id");
    if (refused) {
        /* the other thread's full list does not limit this thread's own list */
        DWORD e2; int r2;
        int S2 = post_fill(0, mine, 0, 500, &e2, &r2);
        QCF(S2 == 500 && !r2, "own queue refused after %d posts while the other thread's queue was full", S2);
        QCF(post_drain(1000) == 500, "own drain");
        /* both addressing styles hit the same full list of the other thread */
        SetLastError(0);
        QC(!U(PostThreadMessageW)(t.tid, WM_APP + 1, 0, 0)); EXPECT_ERR(ERROR_NOT_ENOUGH_QUOTA);
        if (by_window) { SetLastError(0); QC(!U(PostMessageW)(t.w, WM_APP + 1, 0, 0)); EXPECT_ERR(ERROR_NOT_ENOUGH_QUOTA); }
    }
    pthr_stop(&t, h);
    if (mine) U(DestroyWindow)(mine);
}
static void post_other_window(void) { post_other_thread(1); }
static void post_other_tid(void) { post_other_thread(0); }
QL_SCENARIO(quota_limits_post_other_thread_window_limit, post_other_window())
QL_SCENARIO(quota_limits_post_other_thread_id_limit, post_other_tid())

static void post_two_threads_each_full(void) {
    struct ql_quota pl; unsigned L = post_prep(&pl);
    if (!L) return;
    struct pthr a, b; HANDLE ha = pthr_start(&a, 1), hb = pthr_start(&b, 1);
    if (!ha || !hb || !a.w || !b.w) { t_skip("worker thread setup failed"); if (ha) pthr_stop(&a, ha); if (hb) pthr_stop(&b, hb); return; }
    DWORD err; int refused;
    int Sa = post_fill(0, a.w, 0, (int)L + 200, &err, &refused);
    post_check_boundary(&pl, Sa, err, refused, "thread A");
    int Sb = post_fill(0, b.w, 0, (int)L + 200, &err, &refused);
    post_check_boundary(&pl, Sb, err, refused, "thread B");
    /* the limit is per thread: B accepted a full limit's worth although A already held one */
    if (refused) QCF(Sb == Sa, "A accepted %d, B accepted %d", Sa, Sb);
    pthr_stop(&a, ha); pthr_stop(&b, hb);
}
QL_SCENARIO(quota_limits_post_limit_is_per_thread, post_two_threads_each_full())

/* ---- argument validation on the same paths ---- */
static void post_invalid_args(void) {
    if (!ql_connect()) { t_skip("win32k not reachable"); return; }
    HWND w = proc_window();
    if (!w) { t_skip("no window"); return; }
    U(PeekMessageW)(&(MSG){0}, NULL, 0, 0, PM_NOREMOVE);
    /* NtUserPostMessage / NtUserPostThreadMessage: any of bits 17..31 set in the message id -> 0x57 */
    static const UINT bad[] = { 0x20000, 0x20001, 0x40000, 0x80000000u, 0xffffffffu, 0x12345678u };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(0);
        QCF(!U(PostMessageW)(w, bad[i], 0, 0), "PostMessage accepted message 0x%x", bad[i]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
        SetLastError(0);
        QCF(!U(PostThreadMessageW)(GetCurrentThreadId(), bad[i], 0, 0), "PostThreadMessage accepted message 0x%x", bad[i]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
    QC(post_drain(100) == 0);
    /* the highest id that passes the mask is queued like any other */
    SetLastError(0);
    QC(U(PostMessageW)(w, 0x1ffff, 7, 9));
    MSG m;
    QC(U(PeekMessageW)(&m, NULL, 0x1ffff, 0x1ffff, PM_REMOVE));
    QC(m.message == 0x1ffff && m.wParam == 7 && m.lParam == 9 && m.hwnd == w);
    /* bad window handles: ValidateHwnd fails */
    static const ULONG_PTR badh[] = { 0x12345678, 0xdeadbeef };
    for (unsigned i = 0; i < sizeof badh / sizeof badh[0]; i++) {
        SetLastError(0);
        QCF(!U(PostMessageW)((HWND)badh[i], WM_APP + 1, 0, 0), "PostMessage to bogus window %p", (void *)badh[i]);
        EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
    }
    QC(U(DestroyWindow)(w));
    SetLastError(0);
    QC(!U(PostMessageW)(w, WM_APP + 1, 0, 0));               /* destroyed window */
    EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
}
QL_SCENARIO(quota_limits_post_invalid_message_and_window, post_invalid_args())

/* direct syscall form of the same validation */
UDECL(BOOL, NtUserPostMessage, (HWND, UINT, WPARAM, LPARAM));
UDECL(BOOL, NtUserPostThreadMessage, (DWORD, UINT, WPARAM, LPARAM));
static void post_nt_stub(void) {
    if (!ql_connect()) { t_skip("win32k not reachable"); return; }
    NtUserPostMessage_pfn pm = UN(NtUserPostMessage);
    NtUserPostThreadMessage_pfn pt = UN(NtUserPostThreadMessage);
    if (!pm || !pt) { t_skip("missing export"); return; }
    HWND w = proc_window();
    if (!w) { t_skip("no window"); return; }
    U(PeekMessageW)(&(MSG){0}, NULL, 0, 0, PM_NOREMOVE);
    SetLastError(0);
    QC(!pm(w, 0x20000, 0, 0)); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SetLastError(0);
    QC(!pt(GetCurrentThreadId(), 0x30000, 0, 0)); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    QC(pm(w, WM_APP + 2, 1, 2));
    QC(pt(GetCurrentThreadId(), WM_APP + 3, 3, 4));
    MSG m;
    QC(U(PeekMessageW)(&m, NULL, WM_APP + 2, WM_APP + 2, PM_REMOVE) && m.wParam == 1 && m.lParam == 2);
    QC(U(PeekMessageW)(&m, NULL, WM_APP + 3, WM_APP + 3, PM_REMOVE) && m.wParam == 3 && m.lParam == 4 && m.hwnd == NULL);
    QC(post_drain(10) == 0);
    U(DestroyWindow)(w);
}
QL_SCENARIO(quota_limits_post_nt_stub_validation, post_nt_stub())
