/* State-changing foreground / cursor syscalls of the r2_input_pointer_rim_state area (run with /destructive).
 * Each test restores the foreground window, the system cursor visibility and the cursor clip before it ends,
 * whether or not its checks pass.
 *
 * win32kfull / win32kbase facts:
 *  NtUserClearForeground: IAMThreadAccessGranted else FALSE/5; otherwise releases the desktop's foreground
 *      lock and returns 1.
 *  NtUserShowSystemCursor(show): CheckWinstaAttributeAccess(0x10) else FALSE/5; the pointer is hidden when show == 0.
 *  NtUserLockCursor(rect): ApiSetCheckCursorClipAccess(2) else FALSE/5; NULL clears the clip.
 *  NtUserSetFallbackForeground(NULL, FALSE) clears the fallback foreground window.
 */
#include "t.h"
#include "k.h"

UDECL(LONG, NtUserClearForeground, (void));
UDECL(LONG, NtUserShowSystemCursor, (int show));
UDECL(LONG, NtUserLockCursor, (const RECT *r));
UDECL(LONG, NtUserSetFallbackForeground, (HWND h, int set));

/* Wine's win32u aborts the process on these unimplemented syscalls; nothing here is meaningful there. */
#define DXT(n) \
    static void dxb_##n(void); \
    TEST(dx_r2_input_pointer_rim_state_##n) { if (t_is_wine()) { t_skip("emulated win32u"); return; } dxb_##n(); } \
    static void dxb_##n(void)

static HWND dx_window(void) {
    static ATOM a;
    if (!U(RegisterClassExW) || !U(CreateWindowExW) || !U(DefWindowProcW)) return NULL;
    if (!a) {
        WNDCLASSEXW wc; memset(&wc, 0, sizeof wc);
        wc.cbSize = sizeof wc; wc.lpfnWndProc = (WNDPROC)U(DefWindowProcW);
        wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = L"win32kt_dxr2ip";
        a = U(RegisterClassExW)(&wc);
    }
    return U(CreateWindowExW)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"win32kt_dxr2ip", L"dxr2ip", WS_POPUP,
                              -32000, -32000, 40, 30, NULL, NULL, GetModuleHandleW(NULL), NULL);
}
static void restore_foreground(HWND fg) {
    if (fg && U(IsWindow)(fg) && U(GetForegroundWindow)() != fg) U(SetForegroundWindow)(fg);
}

DXT(clearforeground_true_or_access_denied) {
    REQUIRE(UN(NtUserClearForeground) && U(GetForegroundWindow) && U(PeekMessageW));
    MSG m; U(PeekMessageW)(&m, NULL, 0, 0, PM_NOREMOVE);
    HWND fg = U(GetForegroundWindow)();
    SET_ERR(0);
    LONG r = UN(NtUserClearForeground)();
    DWORD e = GetLastError();
    t_note("NtUserClearForeground -> %ld gle=%lu", r, e);
    CHECKF((r & 0xFF) == 1 || ((r & 0xFF) == 0 && e == 5), "r=%ld e=%lu", r, e);
    restore_foreground(fg);
}

DXT(clearforeground_twice_is_idempotent) {
    REQUIRE(UN(NtUserClearForeground) && U(GetForegroundWindow) && U(PeekMessageW));
    MSG m; U(PeekMessageW)(&m, NULL, 0, 0, PM_NOREMOVE);
    HWND fg = U(GetForegroundWindow)();
    LONG a = UN(NtUserClearForeground)() & 0xFF, b = UN(NtUserClearForeground)() & 0xFF;
    EXPECT_EQ(a, b);                                /* nothing in the body depends on the previous call's outcome */
    restore_foreground(fg);
}

DXT(showsystemcursor_hide_then_show) {
    REQUIRE(UN(NtUserShowSystemCursor) && U(PeekMessageW));
    MSG m; U(PeekMessageW)(&m, NULL, 0, 0, PM_NOREMOVE);
    SET_ERR(0);
    LONG h = UN(NtUserShowSystemCursor)(0);
    DWORD e = GetLastError();
    t_note("ShowSystemCursor(FALSE) -> %ld gle=%lu", h, e);
    if ((h & 0xFF) == 0) EXPECT_EQ(e, 5);          /* the only failure path in the body is the winsta attribute check */
    LONG s = UN(NtUserShowSystemCursor)(1);        /* always restore */
    t_note("ShowSystemCursor(TRUE) -> %ld", s);
    if ((h & 0xFF) == 0) EXPECT_EQ(s & 0xFF, 0);   /* a denied caller is denied both ways */
}

DXT(showsystemcursor_nonzero_means_show) {
    REQUIRE(UN(NtUserShowSystemCursor) && U(PeekMessageW));
    MSG m; U(PeekMessageW)(&m, NULL, 0, 0, PM_NOREMOVE);
    LONG a = UN(NtUserShowSystemCursor)(1) & 0xFF, b = UN(NtUserShowSystemCursor)(0x1234) & 0xFF;
    EXPECT_EQ(a, b);                                /* hide request when param == 0: every nonzero argument is the same request */
    UN(NtUserShowSystemCursor)(1);
}

DXT(lockcursor_null_clears) {
    REQUIRE(UN(NtUserLockCursor) && U(PeekMessageW) && U(GetClipCursor) && U(ClipCursor));
    MSG m; U(PeekMessageW)(&m, NULL, 0, 0, PM_NOREMOVE);
    RECT before; memset(&before, 0, sizeof before);
    CHECK(U(GetClipCursor)(&before));
    SET_ERR(0);
    LONG r = UN(NtUserLockCursor)(NULL);
    DWORD e = GetLastError();
    CHECKF((r & 0xFF) == 1 || ((r & 0xFF) == 0 && e == 5), "r=%ld e=%lu", r, e);
    if ((r & 0xFF) == 1) {
        RECT after; memset(&after, 0, sizeof after);
        CHECK(U(GetClipCursor)(&after));
        RECT vs = { GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN), 0, 0 };
        vs.right = vs.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
        vs.bottom = vs.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
        t_note("clip after clear: %ld,%ld,%ld,%ld (virtual screen %ld,%ld,%ld,%ld)", after.left, after.top, after.right, after.bottom, vs.left, vs.top, vs.right, vs.bottom);
        CHECK(after.left <= vs.left + 1 && after.top <= vs.top + 1 && after.right >= vs.right - 1 && after.bottom >= vs.bottom - 1);
    }
    U(ClipCursor)(&before);                         /* restore whatever was there */
    if (before.right - before.left == GetSystemMetrics(SM_CXVIRTUALSCREEN)) U(ClipCursor)(NULL);
}

DXT(lockcursor_rect_then_clear) {
    REQUIRE(UN(NtUserLockCursor) && U(PeekMessageW) && U(GetClipCursor) && U(ClipCursor));
    MSG m; U(PeekMessageW)(&m, NULL, 0, 0, PM_NOREMOVE);
    RECT scr = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    SET_ERR(0);
    LONG r = UN(NtUserLockCursor)(&scr);
    DWORD e = GetLastError();
    t_note("LockCursor(primary screen) -> %ld gle=%lu", r, e);
    if ((r & 0xFF) == 0) EXPECT_EQ(e, 5);
    UN(NtUserLockCursor)(NULL);                     /* always release */
    U(ClipCursor)(NULL);
}

DXT(fallbackforeground_clear_request) {
    REQUIRE(UN(NtUserSetFallbackForeground) && U(GetForegroundWindow) && U(PeekMessageW));
    MSG m; U(PeekMessageW)(&m, NULL, 0, 0, PM_NOREMOVE);
    HWND fg = U(GetForegroundWindow)();
    SET_ERR(0);
    LONG r = UN(NtUserSetFallbackForeground)(NULL, 0);     /* (NULL, FALSE) is the only accepted NULL form: clear */
    DWORD e = GetLastError();
    t_note("SetFallbackForeground(NULL,FALSE) -> %ld gle=%lu", r, e);
    if ((r & 0xFF) == 0) CHECKF(e == 5 || e == 0, "e=%lu", e);
    restore_foreground(fg);
}
