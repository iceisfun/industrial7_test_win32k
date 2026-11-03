#include "dx_g10.h"

static DWORD WINAPI wthread_main(LPVOID p) {
    G10W *w = (G10W *)p;
    w->hwnd = k_make_window("gap10-peer");
    w->tid = GetCurrentThreadId();
    SetEvent(w->ready);
    if (!w->hwnd) return 1;
    for (;;) {
        DWORD r = MsgWaitForMultipleObjects(1, &w->stop, FALSE, 8000, QS_ALLINPUT);
        MSG m;
        while (U(PeekMessageW)(&m, NULL, 0, 0, PM_REMOVE)) { U(TranslateMessage)(&m); U(DispatchMessageW)(&m); }
        if (r == WAIT_OBJECT_0 || r == WAIT_TIMEOUT) break;
    }
    U(DestroyWindow)(w->hwnd);
    return 0;
}
BOOL g10_wthread_start(G10W *w) {
    memset(w, 0, sizeof *w);
    w->ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    w->stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    w->th = CreateThread(NULL, 0, wthread_main, w, 0, NULL);
    if (!w->th || WaitForSingleObject(w->ready, 5000) != WAIT_OBJECT_0 || !w->hwnd) { g10_wthread_stop(w); return FALSE; }
    return TRUE;
}
void g10_wthread_stop(G10W *w) {
    if (w->stop) SetEvent(w->stop);
    if (w->th) { WaitForSingleObject(w->th, 5000); CloseHandle(w->th); }
    if (w->stop) CloseHandle(w->stop);
    if (w->ready) CloseHandle(w->ready);
    memset(w, 0, sizeof *w);
}
HDC g10_mem_dc(void) {
    HDC dc = U(CreateCompatibleDC)(NULL);
    return dc;
}
void g10_mem_dc_free(HDC dc) { if (dc) U(DeleteDC)(dc); }
