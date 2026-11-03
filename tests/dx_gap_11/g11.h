/* gap_11 - shared helpers.
 *
 * Syscall signatures (read from the win32kfull / win32kbase analysis; argument order from the
 * user32 callers, which are plain jumps into the win32u stubs):
 *   ULONG_PTR NtUserRemoveClipboardFormatListener(HWND)           1 ok, 0 fail, 5 = window of another process
 *   DWORD     NtUserGetPointerDeviceInputSpace(HANDLE dev, UINT64 *out)
 *   BYTE      NtUserNotifyIMEStatus(HWND, DWORD open, DWORD conversion)
 *   DWORD     NtUserGetWindowDisplayAffinity(HWND, DWORD *out)
 *   HACCEL    NtUserCreateAcceleratorTable(ACCEL *, int count)    (count * 6 bytes are copied)
 *   int       NtUserCopyAcceleratorTable(HACCEL, ACCEL *or NULL, int cap)
 *   BOOL      NtUserDestroyAcceleratorTable(HACCEL)
 */
#ifndef G11_H
#define G11_H
#include "k.h"
#include <stdint.h>

#ifndef ERROR_INVALID_WINDOW_HANDLE
#define ERROR_INVALID_WINDOW_HANDLE 1400
#endif
#ifndef ERROR_INVALID_ACCEL_HANDLE
#define ERROR_INVALID_ACCEL_HANDLE 1403
#endif
#define G11_SENT 0x7e57u
#define G11_TYPE_ACCEL 8      /* handle type 8 for accelerator tables; validated as type 8 in Copy/Destroy */
#define G11_WM_CLIPBOARDUPDATE 0x031D

UDECL(ULONG_PTR, NtUserRemoveClipboardFormatListener, (HWND));
UDECL(ULONG_PTR, NtUserAddClipboardFormatListener, (HWND));
UDECL(DWORD, NtUserGetPointerDeviceInputSpace, (HANDLE, UINT64 *));
UDECL(BYTE,  NtUserNotifyIMEStatus, (HWND, DWORD, DWORD));
UDECL(DWORD, NtUserGetWindowDisplayAffinity, (HWND, DWORD *));
UDECL(HANDLE, NtUserCreateAcceleratorTable, (void *, int));
UDECL(int,   NtUserCopyAcceleratorTable, (HANDLE, void *, int));
UDECL(DWORD, NtUserDestroyAcceleratorTable, (HANDLE));
UDECL(BOOL,  g11_GetPointerDeviceInputSpace, (HANDLE, UINT64 *));

/* Wine's win32u stubs some of these as abort-on-call; real Windows implements them all. */
static FARPROC g11_nt(const char *name) {
    static const char *const wine_stubs[] = { "NtUserGetWindowDisplayAffinity", "NtUserSetWindowDisplayAffinity",
        "NtUserGetPointerDeviceInputSpace", "NtUserNotifyIMEStatus", "NtUserCopyAcceleratorTable" };
    if (t_is_wine())
        for (unsigned i = 0; i < sizeof wine_stubs / sizeof wine_stubs[0]; i++)
            if (!strcmp(name, wine_stubs[i])) return NULL;
    return t_proc_in("win32u.dll", name);
}

static volatile LONG g11_updates, g11_commands;
static volatile WPARAM g11_last_cmd_w;
static LRESULT CALLBACK g11_wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == G11_WM_CLIPBOARDUPDATE) InterlockedIncrement(&g11_updates);
    else if (m == WM_COMMAND) { InterlockedIncrement(&g11_commands); g11_last_cmd_w = w; return 0; }
    return U(DefWindowProcW)(h, m, w, l);
}
static ATOM g11_class(void) {
    static ATOM a; static int tried;
    if (!tried) {
        tried = 1;
        WNDCLASSEXW wc; memset(&wc, 0, sizeof wc);
        wc.cbSize = sizeof wc; wc.lpfnWndProc = g11_wndproc;
        wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = L"win32kt_gap11";
        if (U(RegisterClassExW)) a = U(RegisterClassExW)(&wc);
    }
    return a;
}
/* Hidden, off-screen, non-activating window of the calling thread. */
static HWND g11_mk(DWORD style, DWORD ex, HWND parent) {
    if (!g11_class() || !U(CreateWindowExW)) return NULL;
    return U(CreateWindowExW)(ex | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"win32kt_gap11", L"gap11",
                              style, -32000, -32000, 40, 40, parent, NULL, GetModuleHandleW(NULL), NULL);
}
static void g11_kill(HWND h) { if (h && U(DestroyWindow)) U(DestroyWindow)(h); }
#define G11_EQ(call, want) do { ULONG_PTR v_ = (ULONG_PTR)(call); EXPECT_EQ(v_, want); } while (0)
#define G11_NEED_WND(w) HWND w = g11_mk(WS_POPUP, 0, NULL); if (!(w)) { t_skip("cannot create a window (err %lu)", GetLastError()); return; }

/* A window owned by a second thread of this process, kept alive (and pumped) until g11_peer_stop(). */
struct g11_peer { HANDLE th, ready, stop; HWND hwnd; DWORD style; HWND parent; };
static DWORD WINAPI g11_peer_main(LPVOID p) {
    struct g11_peer *c = p;
    c->hwnd = g11_mk(c->style, 0, c->parent);
    SetEvent(c->ready);
    if (!c->hwnd) return 1;
    for (int i = 0; i < 400; i++) {
        DWORD r = MsgWaitForMultipleObjects(1, &c->stop, FALSE, 20, QS_ALLINPUT);
        MSG m;
        while (U(PeekMessageW)(&m, NULL, 0, 0, PM_REMOVE)) { U(TranslateMessage)(&m); U(DispatchMessageW)(&m); }
        if (r == WAIT_OBJECT_0) break;
    }
    U(DestroyWindow)(c->hwnd);
    return 0;
}
static int g11_peer_start(struct g11_peer *c) {
    memset(c, 0, sizeof *c); c->style = WS_POPUP;
    c->ready = CreateEventW(NULL, TRUE, FALSE, NULL); c->stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    c->th = CreateThread(NULL, 0, g11_peer_main, c, 0, NULL);
    if (!c->th) return 0;
    WaitForSingleObject(c->ready, 5000);
    return c->hwnd != NULL;
}
static void g11_peer_stop(struct g11_peer *c) {
    if (c->stop) SetEvent(c->stop);
    if (c->th) { WaitForSingleObject(c->th, 5000); CloseHandle(c->th); }
    if (c->ready) CloseHandle(c->ready);
    if (c->stop) CloseHandle(c->stop);
}
#endif
