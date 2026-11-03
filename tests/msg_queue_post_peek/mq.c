#include "mq.h"
#include <stdlib.h>
#include <wchar.h>

mq_rec mq_log[MQ_LOGMAX];
volatile LONG mq_n;
mq_hook_fn mq_hook;

BOOL mq_user(void) {
    return U(PostMessageW) && U(PostThreadMessageW) && U(PeekMessageW) && U(GetMessageW) &&
           U(GetQueueStatus) && U(MsgWaitForMultipleObjectsEx) && U(CreateWindowExW) &&
           U(DestroyWindow) && U(RegisterClassExW) && U(DefWindowProcW) && U(IsWindow) &&
           U(DispatchMessageW) && U(TranslateMessage) && U(RegisterWindowMessageW) &&
           U(SetTimer) && U(KillTimer) && U(ShowWindow) && U(GetInputState) &&
           U(GetMessageTime) && U(GetMessagePos) && U(SetMessageExtraInfo) &&
           U(GetMessageExtraInfo) && U(PostQuitMessage) && U(WaitMessage);
}

void mq_reset(void) { mq_n = 0; memset(mq_log, 0, sizeof mq_log); mq_hook = NULL; }
int mq_count(UINT msg) {
    int c = 0; LONG n = mq_n; if (n > MQ_LOGMAX) n = MQ_LOGMAX;
    for (int i = 0; i < n; i++) if (mq_log[i].m == msg) c++;
    return c;
}
int mq_find(UINT msg, int from) {
    LONG n = mq_n; if (n > MQ_LOGMAX) n = MQ_LOGMAX;
    for (int i = from < 0 ? 0 : from; i < n; i++) if (mq_log[i].m == msg) return i;
    return -1;
}

static LRESULT CALLBACK mq_wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    LONG i = InterlockedIncrement(&mq_n) - 1;
    if (i < MQ_LOGMAX) {
        mq_log[i].h = h; mq_log[i].m = m; mq_log[i].w = w; mq_log[i].l = l; mq_log[i].tid = GetCurrentThreadId();
    }
    if (mq_hook) { LRESULT r = 0; if (mq_hook(h, m, w, l, &r)) return r; }
    return U(DefWindowProcW)(h, m, w, l);
}

static void mq_class(void) {
    static volatile LONG done;
    if (done) return;
    WNDCLASSEXW wc; memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc; wc.lpfnWndProc = mq_wndproc; wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"win32ktMQ";
    ATOM a = U(RegisterClassExW)(&wc);   /* a second registration (race / re-run) fails harmlessly */
    if (!a && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) t_note("RegisterClassExW failed: %lu", GetLastError());
    done = 1;
}
HWND mq_win(void) {
    mq_class();
    HWND h = U(CreateWindowExW)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"win32ktMQ", L"mq", WS_POPUP,
                              -32000, -32000, 64, 64, NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!h) t_note("CreateWindowExW failed: %lu", GetLastError());
    return h;
}
HWND mq_win_visible(void) {
    HWND h = mq_win();
    if (h) U(ShowWindow)(h, SW_SHOWNOACTIVATE);
    return h;
}
HWND mq_msgwin(void) {
    mq_class();
    return U(CreateWindowExW)(0, L"win32ktMQ", L"mqm", WS_POPUP, 0, 0, 1, 1, HWND_MESSAGE, NULL,
                              GetModuleHandleW(NULL), NULL);
}
UINT mq_regmsg(const char *tag) {
    WCHAR n[96]; swprintf(n, 96, L"win32kt_mq_%S_%lu", tag, GetCurrentProcessId());
    return U(RegisterWindowMessageW)(n);
}
int mq_peek(MSG *m, HWND h, UINT lo, UINT hi, UINT fl) { return U(PeekMessageW)(m, h, lo, hi, fl); }
int mq_drain(void) {
    MSG m; int n = 0;
    while (n < 20000 && U(PeekMessageW)(&m, NULL, 0, 0, PM_REMOVE | PM_QS_POSTMESSAGE)) n++;
    return n;
}
BOOL mq_post(HWND h, UINT m, WPARAM w, LPARAM l) { return U(PostMessageW)(h, m, w, l); }
DWORD mq_status(UINT f) { return U(GetQueueStatus)(f); }
LONG mq_getmsg(MSG *m, HWND h, UINT lo, UINT hi) {
    MSG t;
    if (!U(PeekMessageW)(&t, h, lo, hi, PM_NOREMOVE)) { CHECKF(0, "no message pending before GetMessage"); return -2; }
    return U(GetMessageW)(m, h, lo, hi);
}
int mq_wait(DWORD n, const HANDLE *h, DWORD ms, DWORD mask, DWORD fl) {
    return (int)U(MsgWaitForMultipleObjectsEx)(n, h, ms, mask, fl);
}

static DWORD WINAPI mq_run_thunk(LPVOID p) { U(GetQueueStatus)(0x5DFF); /* creates the queue, resets change bits */ ((mq_body)p)(); return 0; }
void mq_run(mq_body b) {
    mq_reset();
    HANDLE h = CreateThread(NULL, 0, mq_run_thunk, (LPVOID)b, 0, NULL);
    if (!h) { t_note("CreateThread failed"); return; }
    if (WaitForSingleObject(h, 60000) != WAIT_OBJECT_0) {
        CHECKF(0, "test thread did not finish within 60s");
        TerminateThread(h, 1);
    }
    CloseHandle(h);
}

struct mq_hstart { mq_helper *h; mq_helper_fn fn; };
static DWORD WINAPI mq_hs_thunk(LPVOID p) {
    struct mq_hstart s = *(struct mq_hstart *)p;
    free(p);
    return s.fn(s.h);
}
BOOL mq_helper_start(mq_helper *h, mq_helper_fn fn, void *arg) {
    memset(h, 0, sizeof *h);
    h->arg = arg;
    h->ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    h->go = CreateEventW(NULL, FALSE, FALSE, NULL);
    h->done = CreateEventW(NULL, FALSE, FALSE, NULL);
    struct mq_hstart *s = malloc(sizeof *s);
    s->h = h; s->fn = fn;
    h->th = CreateThread(NULL, 0, mq_hs_thunk, s, 0, &h->tid);
    if (!h->th) { free(s); return FALSE; }
    return TRUE;
}
void mq_helper_finish(mq_helper *h) {
    h->stop = 1;
    if (h->go) SetEvent(h->go);
    if (h->th) {
        if (WaitForSingleObject(h->th, 8000) != WAIT_OBJECT_0) {
            CHECKF(0, "helper thread did not exit");
            TerminateThread(h->th, 1);
        }
        CloseHandle(h->th);
    }
    if (h->ready) CloseHandle(h->ready);
    if (h->go) CloseHandle(h->go);
    if (h->done) CloseHandle(h->done);
    memset(h, 0, sizeof *h);
}
