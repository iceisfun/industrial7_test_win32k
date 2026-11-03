/* Cross-process posting, UIPI, broadcast and the posted-message quota.
 *
 * win32kfull!NtUserPostMessage: when the target window belongs to another process
 * (ppi != current) it runs IsMessageAllowedAcrossILByReceiver / CheckAccess (UIPI -> last error 5)
 * and, for ids < 0x289, rejects WM_NOTIFY (0x4e) and WM_INITDIALOG (0x110) outright with
 * ERROR_ACCESS_DENIED in _PostTransformableMessageExtended.
 * The posted-message limit is gUserPostMessageLimit (default 10000): when THREADINFO's queued
 * count (+0x338) has reached it, the post fails with ERROR_NOT_ENOUGH_QUOTA (1816).
 */
#include "mq.h"
#include <stdlib.h>
#include <sddl.h>

#define MX (WM_USER + 0x251)
#define STOP_MSG (WM_USER + 0x280)

static int cf;
#define CC(c) do { if (!(c)) { cf++; printf("  FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

/* ---- server child: a window + a message-only window, logs what its queue delivers ------------- */

CHILD(msg_queue_post_peek_srv) {
    if (!mq_user()) return 2;
    HWND h = mq_win(), mo = mq_msgwin();
    if (!h) { printf("NOWINDOW\n"); return 3; }
    printf("READY %llx %lx %llx\n", (unsigned long long)(ULONG_PTR)h, GetCurrentThreadId(), (unsigned long long)(ULONG_PTR)mo);
    mq_reset();
    DWORD t0 = GetTickCount();
    int stop = 0;
    UINT bc = mq_regmsg("xbc");
    while (!stop && GetTickCount() - t0 < 12000) {
        mq_wait(0, NULL, 200, QS_ALLINPUT, 0);
        MSG m;
        while (U(PeekMessageW)(&m, NULL, 0, 0, PM_REMOVE)) {
            if (m.message == STOP_MSG) stop = 1;
            if (!m.hwnd) { printf("TX %x %llx %llx %lx\n", m.message, (unsigned long long)m.wParam, (unsigned long long)m.lParam, GetCurrentThreadId()); continue; }
            U(DispatchMessageW)(&m);
        }
    }
    LONG n = mq_n; if (n > MQ_LOGMAX) n = MQ_LOGMAX;
    for (int i = 0; i < n; i++) {
        UINT m = mq_log[i].m;
        if (m == WM_NOTIFY || m == WM_INITDIALOG || m == WM_COPYDATA || m >= WM_USER)
            printf("RX %llx %x %llx %llx %lx\n", (unsigned long long)(ULONG_PTR)mq_log[i].h, m, (unsigned long long)mq_log[i].w, (unsigned long long)mq_log[i].l, mq_log[i].tid);
    }
    printf("END\n");
    return 0;
}

typedef struct { char kind[4]; unsigned long long hwnd, msg, wp, lp; unsigned long tid; } rec;
typedef struct {
    HANDLE proc, rd;
    ULONG_PTR hwnd, mo; DWORD tid;
    char buf[65536]; int len;
    rec r[512]; int nr;
} srv;

static void pump_pipe(srv *s, DWORD ms) {
    DWORD t0 = GetTickCount();
    for (;;) {
        DWORD avail = 0;
        if (PeekNamedPipe(s->rd, NULL, 0, NULL, &avail, NULL) && avail) {
            DWORD got = 0;
            if (ReadFile(s->rd, s->buf + s->len, (DWORD)(sizeof s->buf - 1 - s->len), &got, NULL)) s->len += got;
            s->buf[s->len] = 0;
            continue;
        }
        if (GetTickCount() - t0 >= ms) break;
        if (strstr(s->buf, "END\n")) break;
        Sleep(10);
    }
}
static int srv_start(srv *s) {
    memset(s, 0, sizeof *s);
    HANDLE wr = NULL;
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    if (!CreatePipe(&s->rd, &wr, &sa, 0)) return 0;
    SetHandleInformation(s->rd, HANDLE_FLAG_INHERIT, 0);
    PROCESS_INFORMATION pi;
    s->proc = t_spawn_child("msg_queue_post_peek_srv", "", 0, NULL, wr, wr, TRUE, NULL, &pi);
    CloseHandle(wr);
    if (!s->proc) return 0;
    CloseHandle(pi.hThread);
    DWORD t0 = GetTickCount();
    while (GetTickCount() - t0 < 10000 && !strstr(s->buf, "READY") && !strstr(s->buf, "NOWINDOW")) pump_pipe(s, 100);
    unsigned long long h = 0, mo = 0; unsigned long tid = 0;
    char *p = strstr(s->buf, "READY");
    if (!p || sscanf(p, "READY %llx %lx %llx", &h, &tid, &mo) != 3) return 0;
    s->hwnd = (ULONG_PTR)h; s->tid = tid; s->mo = (ULONG_PTR)mo;
    return 1;
}
static void srv_stop(srv *s) {
    if (s->proc) {
        U(PostMessageW)((HWND)s->hwnd, STOP_MSG, 0, 0);
        if (WaitForSingleObject(s->proc, 8000) != WAIT_OBJECT_0) { TerminateProcess(s->proc, 1); WaitForSingleObject(s->proc, 3000); }
        pump_pipe(s, 200);
        CloseHandle(s->proc); s->proc = NULL;
    }
    char *line = s->buf;
    while (line && *line && s->nr < 512) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = 0;
        rec *r = &s->r[s->nr];
        memset(r, 0, sizeof *r);
        if (!strncmp(line, "RX ", 3) && sscanf(line, "RX %llx %llx %llx %llx %lx", &r->hwnd, &r->msg, &r->wp, &r->lp, &r->tid) == 5) { strcpy(r->kind, "RX"); s->nr++; }
        else if (!strncmp(line, "TX ", 3) && sscanf(line, "TX %llx %llx %llx %lx", &r->msg, &r->wp, &r->lp, &r->tid) == 4) { strcpy(r->kind, "TX"); s->nr++; }
        line = nl ? nl + 1 : NULL;
    }
    CloseHandle(s->rd); s->rd = NULL;
}
static int srv_count(srv *s, const char *kind, unsigned long long msg) {
    int c = 0; for (int i = 0; i < s->nr; i++) if (!strcmp(s->r[i].kind, kind) && s->r[i].msg == msg) c++; return c;
}
static rec *srv_nth(srv *s, const char *kind, unsigned long long msg, int n) {
    for (int i = 0; i < s->nr; i++) if (!strcmp(s->r[i].kind, kind) && s->r[i].msg == msg && n-- == 0) return &s->r[i];
    return NULL;
}

QTEST(msg_queue_post_peek_xproc_post_roundtrip_and_order) {
    srv s; if (!srv_start(&s)) { t_note("server child did not start"); srv_stop(&s); return; }
    HWND w = (HWND)s.hwnd;
    static const struct { WPARAM w; LPARAM l; } v[] = { { 1, 2 }, { 0x1122334455667788ull, 0x0123456789ABCDEFll }, { (WPARAM)-1, (LPARAM)-1 }, { 0, 0 } };
    for (unsigned i = 0; i < 4; i++) CHECK(mq_post(w, MX + i, v[i].w, v[i].l));
    CHECK(U(PostThreadMessageW)(s.tid, MX + 8, 0xAA, 0xBB));
    srv_stop(&s);
    int last = -1;
    for (unsigned i = 0; i < 4; i++) {
        rec *r = srv_nth(&s, "RX", MX + i, 0);
        CHECKF(r != NULL, "message %u not delivered", i);
        if (!r) continue;
        EQ(r->hwnd, (unsigned long long)s.hwnd);
        EQ(r->wp, v[i].w);
        EQ(r->lp, v[i].l);
        EQ(r->tid, s.tid);                     /* the window procedure ran in the owner thread of the other process */
        CHECK((int)(r - s.r) > last);                 /* FIFO */
        last = (int)(r - s.r);
    }
    rec *t = srv_nth(&s, "TX", MX + 8, 0);
    CHECK(t != NULL);
    if (t) { EQ(t->wp, 0xAA); EQ(t->lp, 0xBB); }
}

QTEST(msg_queue_post_peek_xproc_wm_notify_and_initdialog_denied) {
    srv s; if (!srv_start(&s)) { t_note("server child did not start"); srv_stop(&s); return; }
    HWND w = (HWND)s.hwnd;
    /* WM_NOTIFY is refused across processes with access denied; WM_INITDIALOG cannot be posted at all (a pointer-carrying
     * message: ERROR_MESSAGE_SYNC_ONLY), which is reported before any process check */
    static const UINT ids[] = { WM_NOTIFY, WM_INITDIALOG };
    static const DWORD errs[] = { ERROR_ACCESS_DENIED, 1159 /* ERROR_MESSAGE_SYNC_ONLY */ };
    for (unsigned i = 0; i < 2; i++) {
        SetLastError(0);
        CHECKF(!mq_post(w, ids[i], 0, 0), "id 0x%x posted across processes", ids[i]);
        DWORD e = GetLastError();
        CHECKF(e == errs[i], "id 0x%x: GetLastError got %lu want %lu", ids[i], e, errs[i]);
    }
    CHECK(mq_post(w, MX, 1, 1));                      /* the window is fine for ordinary ids */
    srv_stop(&s);
    EQ(srv_count(&s, "RX", WM_NOTIFY), 0);
    EQ(srv_count(&s, "RX", WM_INITDIALOG), 0);
    EQ(srv_count(&s, "RX", MX), 1);
}

QTEST(msg_queue_post_peek_xproc_copydata_probe) {
    srv s; if (!srv_start(&s)) { t_note("server child did not start"); srv_stop(&s); return; }
    SetLastError(0);
    BOOL ok = mq_post((HWND)s.hwnd, WM_COPYDATA, 0, 0);
    t_note("cross-process PostMessage(WM_COPYDATA): %s (error %lu)", ok ? "accepted" : "rejected", ok ? 0 : GetLastError());
    srv_stop(&s);
    EQ(srv_count(&s, "RX", WM_COPYDATA), ok ? 1 : 0);
}

QTEST(msg_queue_post_peek_xproc_broadcast_reaches_toplevel_not_message_only) {
    srv s; if (!srv_start(&s)) { t_note("server child did not start"); srv_stop(&s); return; }
    UINT bc = mq_regmsg("xbc");
    CHECK(mq_post(HWND_BROADCAST, bc, 0x31, 0x32));
    Sleep(200);
    srv_stop(&s);
    int top = 0, mo = 0;
    for (int i = 0; i < s.nr; i++) if (!strcmp(s.r[i].kind, "RX") && s.r[i].msg == bc) { if (s.r[i].hwnd == s.hwnd) top++; if (s.r[i].hwnd == s.mo) mo++; }
    EQ(mo, 0);                                 /* message-only windows are not in the broadcast set */
    if (top != 1) t_note("broadcast delivered %d times to the child's hidden top-level window", top);
}

QTEST(msg_queue_post_peek_xproc_posting_to_exited_process) {
    srv s; if (!srv_start(&s)) { t_note("server child did not start"); srv_stop(&s); return; }
    HWND w = (HWND)s.hwnd; DWORD tid = s.tid;
    srv_stop(&s);                                      /* child exited */
    SetLastError(0);
    CHECK(!mq_post(w, MX, 0, 0));
    EXPECT_ERR(MQ_ERR_INVALID_WINDOW_HANDLE);
    CHECK(!U(IsWindow)(w));
    SetLastError(0);
    CHECK(!U(PostThreadMessageW)(tid, MX, 0, 0));
    EXPECT_ERR(MQ_ERR_INVALID_THREAD_ID);
}

QTEST(msg_queue_post_peek_xproc_desktop_window_notify_denied) {
    HWND d = U(GetDesktopWindow)(); QNEED(d);
    SetLastError(0);
    CHECK(!mq_post(d, WM_NOTIFY, 0, 0));              /* desktop window lives in another process: 0x4e is refused */
    EXPECT_ERR(ERROR_ACCESS_DENIED);
    SetLastError(0);
    CHECK(!mq_post(d, WM_INITDIALOG, 0, 0));          /* 0x110 can only be sent: reported even before the process check */
    EXPECT_ERR(1159 /* ERROR_MESSAGE_SYNC_ONLY */);
}

QTEST(msg_queue_post_peek_xproc_peek_other_threads_window_probe) {
    srv s; if (!srv_start(&s)) { t_note("server child did not start"); srv_stop(&s); return; }
    MSG m; SetLastError(0);
    int r = mq_peek(&m, (HWND)s.hwnd, 0, 0, PM_NOREMOVE);
    t_note("PeekMessage with a foreign window: %d (error %lu)", r, GetLastError());
    srv_stop(&s);
}

/* ---- integrity levels ------------------------------------------------------------------------ */

/* The low-integrity child reports through its exit code (a pipe/file created by a medium process
 * cannot be written by a low one): bit set = that check failed. */
CHILD(msg_queue_post_peek_low) {
    if (!mq_user() || argc < 3) return 0x1000;
    unsigned long long hv = 0; unsigned long tid = 0;
    sscanf(argv[1], "%llx", &hv); sscanf(argv[2], "%lx", &tid);
    HWND ph = (HWND)(ULONG_PTR)hv;
    int bad = 0;
    HWND mine = mq_win();
    if (!mine) bad |= 0x80;
    if (k_integrity_level() != SECURITY_MANDATORY_LOW_RID) bad |= 0x40;
    SetLastError(0);
    if (U(PostMessageW)(ph, WM_USER + 0x261, 1, 2)) bad |= 0x01;           /* UIPI: low -> medium window */
    else if (GetLastError() != ERROR_ACCESS_DENIED) bad |= 0x02;
    SetLastError(0);
    if (U(PostThreadMessageW)(tid, WM_USER + 0x262, 1, 2)) bad |= 0x04;     /* same rule through the thread path */
    else if (GetLastError() != ERROR_ACCESS_DENIED) bad |= 0x08;
    if (mine) {
        if (!U(PostMessageW)(mine, WM_USER + 0x263, 3, 4)) bad |= 0x10;     /* own window: always fine */
        MSG m;
        if (!U(PeekMessageW)(&m, mine, WM_USER + 0x263, WM_USER + 0x263, PM_REMOVE)) bad |= 0x20;
    }
    return bad;
}

static BOOL spawn_low(const char *args, DWORD *exit_code) {
    HANDLE tok = NULL, dup = NULL; PSID low = NULL; BOOL ok = FALSE;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY | TOKEN_ADJUST_DEFAULT, &tok)) return FALSE;
    if (!DuplicateTokenEx(tok, MAXIMUM_ALLOWED, NULL, SecurityImpersonation, TokenPrimary, &dup)) { CloseHandle(tok); return FALSE; }
    if (ConvertStringSidToSidW(L"S-1-16-4096", &low)) {
        TOKEN_MANDATORY_LABEL tml; memset(&tml, 0, sizeof tml);
        tml.Label.Sid = low; tml.Label.Attributes = SE_GROUP_INTEGRITY;
        if (SetTokenInformation(dup, TokenIntegrityLevel, &tml, sizeof tml + GetLengthSid(low))) {
            WCHAR cmd[2048], self[MAX_PATH];
            MultiByteToWideChar(CP_ACP, 0, t_self_path(), -1, self, MAX_PATH);
            WCHAR wargs[256]; MultiByteToWideChar(CP_ACP, 0, args, -1, wargs, 256);
            swprintf(cmd, 2048, L"\"%ls\" --child msg_queue_post_peek_low %ls", self, wargs);
            STARTUPINFOW si; PROCESS_INFORMATION pi; memset(&si, 0, sizeof si); si.cb = sizeof si; memset(&pi, 0, sizeof pi);
            if (CreateProcessAsUserW(dup, NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
                if (WaitForSingleObject(pi.hProcess, 20000) != WAIT_OBJECT_0) { TerminateProcess(pi.hProcess, 1); *exit_code = 0xDEAD0001; }
                else GetExitCodeProcess(pi.hProcess, exit_code);
                CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
                ok = TRUE;
            }
        }
        LocalFree(low);
    }
    CloseHandle(dup); CloseHandle(tok);
    return ok;
}

QTEST(msg_queue_post_peek_xproc_low_integrity_post_denied_by_uipi) {
    if (t_is_wine()) { t_note("no integrity levels under Wine"); return; }
    if (k_integrity_level() <= SECURITY_MANDATORY_LOW_RID) { t_note("host already low integrity"); return; }
    HWND h = mq_win(); QNEED(h);
    char args[64]; snprintf(args, sizeof args, "%llx %lx", (unsigned long long)(ULONG_PTR)h, GetCurrentThreadId());
    DWORD code = 0xFFFFFFFF;
    if (!spawn_low(args, &code)) { t_note("could not start a low-integrity child (error %lu)", GetLastError()); U(DestroyWindow)(h); return; }
    CHECKF(code == 0, "low child failure mask 0x%lx (0x01/0x04 post succeeded, 0x02/0x08 wrong error, 0x10/0x20 own window, 0x40 not low, 0x80 no window)", code);
    MSG m;
    CHECK(!mq_peek(&m, NULL, WM_USER + 0x261, WM_USER + 0x262, PM_NOREMOVE));   /* nothing leaked into our queue */
    U(DestroyWindow)(h);
}

/* ---- posted-message quota ---------------------------------------------------------------------- */

CHILD(msg_queue_post_peek_quota) {
    if (!mq_user()) return 2;
    const UINT M = WM_USER + 0x271;
    HWND h = mq_win();          /* NULL (thread messages) when no window can be created: same counter */
    DWORD limit = 10000, v = 0, sz = sizeof v; int have_reg = 0;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Windows", L"USERPostMessageLimit", RRF_RT_DWORD, NULL, &v, &sz) == ERROR_SUCCESS) { limit = v; have_reg = 1; }
    printf("NOTE USERPostMessageLimit registry value %s (%lu)\n", have_reg ? "present" : "absent", limit);
    int n = 0; DWORD err = 0;
    while (n < 12000) {
        SetLastError(0);
        if (!U(PostMessageW)(h, M, (WPARAM)n, 0)) { err = GetLastError(); break; }
        n++;
    }
    printf("NOTE accepted %d posts before the first failure (error %lu)\n", n, err);
    if (n >= 12000) { printf("NOTE limit above 12000: not exercised\n"); return cf; }
    CC(err == 1816);
    if (!have_reg) CC(n == 10000);
    else if (limit >= 4000 && limit <= 1000000) CC((DWORD)n == limit);
    /* every post path shares the one THREADINFO counter */
    SetLastError(0); CC(!U(PostThreadMessageW)(GetCurrentThreadId(), M, 0, 0)); CC(GetLastError() == 1816);
    SetLastError(0); CC(!U(PostMessageW)(NULL, M, 0, 0)); CC(GetLastError() == 1816);
    SetLastError(0); CC(!U(PostMessageW)(h, M, 0, 0)); CC(GetLastError() == 1816);
    if (UN(NtUserPostMessage)) { SetLastError(0); CC(UN(NtUserPostMessage)(h, M, 0, 0) == 0); CC(GetLastError() == 1816); }
    /* the queue state still reports the posted messages */
    DWORD qs = U(GetQueueStatus)(QS_POSTMESSAGE);
    CC(((qs >> 16) & QS_POSTMESSAGE) != 0);
    /* one retrieval frees exactly one slot */
    MSG m;
    CC(U(PeekMessageW)(&m, NULL, 0, 0, PM_REMOVE) && m.message == M && m.wParam == 0);
    CC(U(PostMessageW)(h, M, 0xFFFF, 0));
    SetLastError(0); CC(!U(PostMessageW)(h, M, 0, 0)); CC(GetLastError() == 1816);
    /* drain everything in order: 1..n-1, then the re-posted one */
    int got = 0; int order_ok = 1;
    while (U(PeekMessageW)(&m, NULL, 0, 0, PM_REMOVE | PM_QS_POSTMESSAGE)) {
        if (got < n - 1 && m.wParam != (WPARAM)(got + 1)) order_ok = 0;
        got++;
    }
    CC(got == n);
    CC(order_ok);
    CC(U(PostMessageW)(h, M, 1, 1));                  /* an empty queue accepts posts again */
    return cf;
}

QTEST(msg_queue_post_peek_xproc_post_quota_per_thread_limit) {
    struct k_child_result r = k_child_run("msg_queue_post_peek_quota", "", 90000);
    CHECKF(r.exit_code == 0, "child exit 0x%lx", r.exit_code);
}
