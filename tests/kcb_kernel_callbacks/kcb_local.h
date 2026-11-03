/* kcb_kernel_callbacks: shared "second GUI thread" helper for cross-thread SendMessage tests.
 *
 * Several __fn* table slots (DWORD/INSTRING/OUTSTRING/...) are only exercised by a SENT message
 * that crosses a thread boundary: a same-thread SendMessage calls the window procedure directly
 * in user mode (no kernel round trip at all), so the kernel's callback mechanism only fires when
 * the target window belongs to a DIFFERENT thread. Real Windows delivers a pending sent message
 * to its owning thread the next time that thread's own message loop calls GetMessage/PeekMessage
 * (NtUserGetMessage notices queued sent-message work for the calling thread and the kernel calls
 * back into that same thread, mid-syscall, through PEB->KernelCallbackTable, before returning the
 * next posted message) - so the receiver thread must be pumping, not merely alive.
 */
#ifndef KCB_LOCAL_H
#define KCB_LOCAL_H
#include "t.h"
#include "k.h"
#include "kcb_api.h"

struct kcb_worker {
    HANDLE thread;
    HANDLE ready;
    volatile LONG stop;
    HWND hwnd;
    char class_name[32];
};

/* Message-only (HWND_MESSAGE) so system broadcasts never reach the window and perturb hit counts. */
static LRESULT CALLBACK kcb_worker_wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    return DefWindowProcA(h, m, w, l);
}

static DWORD WINAPI kcb_worker_proc(LPVOID arg) {
    struct kcb_worker *wk = (struct kcb_worker *)arg;
    WNDCLASSA wc; memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = kcb_worker_wndproc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = wk->class_name;
    RegisterClassA(&wc);
    wk->hwnd = CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wk->class_name, "kcb_worker",
                                WS_POPUP, 0, 0, 1, 1, HWND_MESSAGE, NULL, GetModuleHandleA(NULL), NULL);
    SetEvent(wk->ready);
    if (!wk->hwnd) return 1;
    /* Bounded pump: up to ~5s total, checked every 20ms so the test's SendMessage gets delivered
     * promptly without this thread ever blocking unboundedly (SAFETY.md: bounded waits only). */
    for (int i = 0; i < 250 && !wk->stop; i++) k_pump(20);
    DestroyWindow(wk->hwnd);
    UnregisterClassA(wk->class_name, GetModuleHandleA(NULL));
    return 0;
}

static void kcb_worker_stop(struct kcb_worker *wk);

/* Starts the worker thread and waits (bounded) for its window to exist. Returns FALSE (and leaves
 * nothing to clean up) if the window could not be created - typical under a headless/no-driver
 * environment, which callers should treat as a skip, not a failure. */
static BOOL kcb_worker_start(struct kcb_worker *wk, const char *tag) {
    memset(wk, 0, sizeof *wk);
    snprintf(wk->class_name, sizeof wk->class_name, "win32ktKcb_%s_%lu", tag, GetCurrentProcessId());
    wk->ready = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!wk->ready) return FALSE;
    wk->thread = CreateThread(NULL, 0, kcb_worker_proc, wk, 0, NULL);
    if (!wk->thread) { CloseHandle(wk->ready); return FALSE; }
    WaitForSingleObject(wk->ready, 5000);
    if (!wk->hwnd) { kcb_worker_stop(wk); return FALSE; }
    return TRUE;
}

static void kcb_worker_stop(struct kcb_worker *wk) {
    if (!wk->thread) { if (wk->ready) CloseHandle(wk->ready); return; }
    InterlockedExchange(&wk->stop, 1);
    WaitForSingleObject(wk->thread, 5000);
    CloseHandle(wk->thread);
    CloseHandle(wk->ready);
    memset(wk, 0, sizeof *wk);
}

#endif
