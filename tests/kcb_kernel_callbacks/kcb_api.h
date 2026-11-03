/* kcb_kernel_callbacks: user32/gdi32 entry points used by this area, resolved at call time.
 *
 * The test exe must not import user32 or gdi32 at link time: a static USER32 import would load the
 * DLL (and connect the process to win32k, creating PEB->KernelCallbackTable) before main(), which
 * destroys the pristine-state property the table tests below rely on, and would make user32 an
 * implicit dependency instead of "the DLL under test". Each name is therefore rewritten to a
 * U() lookup, which loads user32/gdi32 lazily on first use. */
#ifndef KCB_API_H
#define KCB_API_H
#include "t.h"
#include "k.h"
#define SendMessageA            U(SendMessageA)
#define SendMessageW            U(SendMessageW)
#define SendMessageTimeoutW     U(SendMessageTimeoutW)
#define SendNotifyMessageW      U(SendNotifyMessageW)
#define PostMessageW            U(PostMessageW)
#define DestroyWindow           U(DestroyWindow)
#define CreateWindowExA         U(CreateWindowExA)
#define CreateWindowExW         U(CreateWindowExW)
#define RegisterClassA          U(RegisterClassA)
#define RegisterClassW          U(RegisterClassW)
#define UnregisterClassA        U(UnregisterClassA)
#define UnregisterClassW        U(UnregisterClassW)
#define GetWindowTextA          U(GetWindowTextA)
#define SetWindowTextA          U(SetWindowTextA)
#define DefWindowProcA          U(DefWindowProcA)
#define DefWindowProcW          U(DefWindowProcW)
#define InvalidateRect          U(InvalidateRect)
#define UpdateWindow            U(UpdateWindow)
#define FillRect                U(FillRect)
#define GetDC                   U(GetDC)
#define ReleaseDC               U(ReleaseDC)
#define SetWindowPos            U(SetWindowPos)
#define ShowWindow              U(ShowWindow)
#define RegisterWindowMessageW  U(RegisterWindowMessageW)

/* The apfnDispatch ABI (slot count and meaning of each index) is shared by every 2004/20H2/21H1/21H2/22H2
 * servicing build (19041..19045); the exact RVAs inside user32.dll are specific to the revision the
 * corpus was derived from (19041.2965). */
static inline int kcb_win10_2004_family(void) {
    unsigned long b = 0; k_win_build(&b, NULL);
    return !t_is_wine() && b >= 19041 && b <= 19045;
}
static inline int kcb_ref_revision(void) {
    unsigned long b = 0, u = 0; k_win_build(&b, &u);
    return !t_is_wine() && b == 19041 && u == 2965;
}

/* Callback argument blocks carry the window as a PWND (a pointer into the desktop heap, mapped read-only into the process),
 * not as an HWND. The window object starts with its handle (THRDESKHEAD.h), so the HWND is the first qword behind the
 * pointer. A value with no bits above 32 is already a handle. */
static inline HWND kcb_pwnd_hwnd(ULONG_PTR v) {
    HWND h = NULL;
    if ((v >> 32) && k_try_read((const void *)v, &h, sizeof h)) return h;
    return (HWND)v;
}

/* Once a thread has made its first top-level window the default IME window exists; the first window creation on a thread
 * therefore issues two creations (and two CREATESTRUCT/CBT callbacks). Tests that count creations call this first. */
static inline void kcb_warm_thread(void) {
    static __thread HWND warm;
    if (!warm) warm = k_make_window("kcb_warm");
}
#endif
