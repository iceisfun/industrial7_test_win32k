/* Foreground brokering, cursor and window-icon helpers, thread input block flag, raw-mouse marking,
 * keyboard message posting and mouse-in-pointer promotion. Only paths that cannot change the user's
 * foreground, cursor or clip are exercised here; the state-changing ones are in dx_r2_input_pointer_rim_state.
 *
 * Analysis facts (win32kfull unless noted):
 *  NtUserSetFallbackForeground: IAMThreadAccessGranted else 5; then (hwnd==0 xor set==TRUE) mismatches,
 *      the desktop window (FNID 0x29d), non-top-level windows and invalid handles all end in 0x57.
 *  NtUserSetBrokeredForeground: NULL/invalid/non-top-level hwnd -> FALSE, 0x57.
 *  NtUserCanBrokerForceForeground: not an immersive broker -> FALSE, 5, output untouched.
 *  NtUserSetForegroundWindowForApplication / NtUserNavigateFocus: invalid handle or the desktop-class
 *      windows (FNID 0x29d / 0x29f) are rejected without any foreground change.
 *  NtUserPostKeyboardInputMessage (win32kbase): only the DWM process may call it, else STATUS_ACCESS_DENIED.
 */
#include "r2ip.h"
#include <stdlib.h>

/* an own, destroyed window handle: stale, never revalidates */
static HWND stale_window(void) {
    HWND h = r2_window(WS_POPUP);
    if (h) U(DestroyWindow)(h);
    return h;
}

/* ---- NtUserSetFallbackForeground --------------------------------------------------------- */

/* (NULL, TRUE) never reaches _SetFallbackForeground: access gate (5) or the 0x57 branch. */
static DWORD fallback_probe(void) {
    SET_ERR(0);
    LONG r = UNW(NtUserSetFallbackForeground)(NULL, 1);
    CHECK((r & 0xFF) == 0);
    return GetLastError();
}
RT(fg_fallback_null_with_set_is_rejected) {
    REQUIRE(UNW(NtUserSetFallbackForeground) && U(PeekMessageW));
    MSG m; U(PeekMessageW)(&m, NULL, 0, 0, PM_NOREMOVE);
    DWORD e = fallback_probe();
    CHECKF(e == E_ACCESS || e == E_INVALID_PARAM, "e=%lu", e);
    t_note("SetFallbackForeground(NULL,TRUE): gle=%lu (%s)", e, e == E_ACCESS ? "thread access denied" : "access granted, 0x57 branch");
}
RT(fg_fallback_window_with_clear_is_rejected) {
    REQUIRE(UNW(NtUserSetFallbackForeground) && U(CreateWindowExW));
    HWND w = r2_window(WS_POPUP); if (!w) { t_skip("no window"); return; }
    SET_ERR(0);
    LONG r = UNW(NtUserSetFallbackForeground)(w, 0);     /* a window with set == FALSE never sets: 0x57 (after the access gate) */
    DWORD e = GetLastError();
    CHECK((r & 0xFF) == 0);
    CHECKF(e == E_ACCESS || e == E_INVALID_PARAM, "e=%lu", e);
    if (fallback_probe() == E_INVALID_PARAM) EXPECT_EQ(e, E_INVALID_PARAM);
    U(DestroyWindow)(w);
}
RT(fg_fallback_stale_window_is_rejected) {
    REQUIRE(UNW(NtUserSetFallbackForeground) && U(DestroyWindow));
    HWND w = stale_window(); REQUIRE(w);
    SET_ERR(0);
    LONG r = UNW(NtUserSetFallbackForeground)(w, 1);
    DWORD e = GetLastError();
    CHECK((r & 0xFF) == 0);
    CHECKF(e == E_ACCESS || e == E_INVALID_PARAM, "e=%lu", e);
}
RT(fg_fallback_desktop_window_is_rejected) {
    REQUIRE(UNW(NtUserSetFallbackForeground) && U(GetDesktopWindow));
    SET_ERR(0);
    LONG r = UNW(NtUserSetFallbackForeground)(U(GetDesktopWindow)(), 1);    /* FNID 0x29d is excluded */
    DWORD e = GetLastError();
    CHECK((r & 0xFF) == 0);
    CHECKF(e == E_ACCESS || e == E_INVALID_PARAM, "e=%lu", e);
}
RT(fg_fallback_child_window_is_rejected) {
    REQUIRE(UNW(NtUserSetFallbackForeground) && U(CreateWindowExW));
    HWND p = r2_window(WS_POPUP); REQUIRE(p);
    HWND c = U(CreateWindowExW)(0, L"STATIC", L"c", WS_CHILD, 0, 0, 10, 10, p, NULL, GetModuleHandleW(NULL), NULL);
    if (!c) { U(DestroyWindow)(p); t_skip("child window"); return; }
    SET_ERR(0);
    LONG r = UNW(NtUserSetFallbackForeground)(c, 1);                         /* _IsTopLevelWindow fails */
    DWORD e = GetLastError();
    CHECK((r & 0xFF) == 0);
    CHECKF(e == E_ACCESS || e == E_INVALID_PARAM, "e=%lu", e);
    U(DestroyWindow)(p);
}
RT(fg_fallback_does_not_change_foreground) {
    REQUIRE(UNW(NtUserSetFallbackForeground) && U(GetForegroundWindow));
    HWND f0 = U(GetForegroundWindow)();
    fallback_probe();
    EXPECT_EQ(U(GetForegroundWindow)(), f0);
}

/* ---- NtUserSetBrokeredForeground ---------------------------------------------------------- */

RT(fg_brokered_null_is_invalid_parameter) {
    REQUIRE(UNW(NtUserSetBrokeredForeground) && U(PeekMessageW));
    MSG m; U(PeekMessageW)(&m, NULL, 0, 0, PM_NOREMOVE);
    SET_ERR(0);
    EXPECT_EQ(UNW(NtUserSetBrokeredForeground)(NULL) & 0xFF, 0);
    EXPECT_ERR(E_INVALID_PARAM);
}
RT(fg_brokered_stale_window_is_invalid_parameter) {
    REQUIRE(UNW(NtUserSetBrokeredForeground));
    HWND w = stale_window(); REQUIRE(w);
    SET_ERR(0);
    EXPECT_EQ(UNW(NtUserSetBrokeredForeground)(w) & 0xFF, 0);
    EXPECT_ERR(E_INVALID_PARAM);                         /* ValidateHwnd failure falls into the shared 0x57 label */
}
RT(fg_brokered_garbage_handles_are_invalid_parameter) {
    REQUIRE(UNW(NtUserSetBrokeredForeground) && U(PeekMessageW));
    MSG m; U(PeekMessageW)(&m, NULL, 0, 0, PM_NOREMOVE);
    static const ULONG_PTR hs[] = { 0x7FFF0001, 0x7FFF0002, 0x7FFF0010, 0x7FFFFFFF, 0xFFFFFFFF };
    for (size_t i = 0; i < sizeof hs / sizeof *hs; i++) {
        SET_ERR(0);
        CHECKF((UNW(NtUserSetBrokeredForeground)((HWND)hs[i]) & 0xFF) == 0, "hwnd %p", (void *)hs[i]);
        EXPECT_ERR(E_INVALID_PARAM);
    }
}
RT(fg_brokered_child_window_is_invalid_parameter) {
    REQUIRE(UNW(NtUserSetBrokeredForeground) && U(CreateWindowExW));
    HWND p = r2_window(WS_POPUP); REQUIRE(p);
    HWND c = U(CreateWindowExW)(0, L"STATIC", L"c", WS_CHILD, 0, 0, 10, 10, p, NULL, GetModuleHandleW(NULL), NULL);
    if (!c) { U(DestroyWindow)(p); t_skip("child window"); return; }
    SET_ERR(0);
    EXPECT_EQ(UNW(NtUserSetBrokeredForeground)(c) & 0xFF, 0);   /* _IsTopLevelWindow == FALSE */
    EXPECT_ERR(E_INVALID_PARAM);
    U(DestroyWindow)(p);
}
RT(fg_brokered_message_only_window_is_invalid_parameter) {
    REQUIRE(UNW(NtUserSetBrokeredForeground) && U(CreateWindowExW));
    HWND w = U(CreateWindowExW)(0, L"STATIC", L"m", WS_POPUP, 0, 0, 1, 1, HWND_MESSAGE, NULL, GetModuleHandleW(NULL), NULL);
    REQUIRE(w);
    SET_ERR(0);
    EXPECT_EQ(UNW(NtUserSetBrokeredForeground)(w) & 0xFF, 0);   /* IsMessageOnlyWindow -> 0x57 */
    EXPECT_ERR(E_INVALID_PARAM);
    U(DestroyWindow)(w);
}
RT(fg_brokered_does_not_change_foreground) {
    REQUIRE(UNW(NtUserSetBrokeredForeground) && U(GetForegroundWindow));
    HWND f0 = U(GetForegroundWindow)();
    UNW(NtUserSetBrokeredForeground)(NULL);
    EXPECT_EQ(U(GetForegroundWindow)(), f0);
}

/* ---- NtUserCanBrokerForceForeground --------------------------------------------------------- */

RT(fg_canbroker_denied_for_ordinary_process) {
    REQUIRE(UNW(NtUserCanBrokerForceForeground) && U(PeekMessageW));
    MSG m; U(PeekMessageW)(&m, NULL, 0, 0, PM_NOREMOVE);
    DWORD out = 0x5a5a5a5a;
    SET_ERR(0);
    LONG r = UNW(NtUserCanBrokerForceForeground)(&out);
    DWORD e = GetLastError();
    if ((r & 0xFF) == 0) {
        EXPECT_EQ(e, E_ACCESS);            /* IsImmersiveBroker() == FALSE */
        EXPECT_EQ(out, 0x5a5a5a5a);        /* output only written for brokers */
    } else {
        t_note("process is an immersive broker: out=%lu", out);
        CHECK(out == 0 || out == 1);
    }
}
RT(fg_canbroker_result_stable) {
    REQUIRE(UNW(NtUserCanBrokerForceForeground) && U(PeekMessageW));
    MSG m; U(PeekMessageW)(&m, NULL, 0, 0, PM_NOREMOVE);
    DWORD o1 = 0x5a5a5a5a, o2 = 0x5a5a5a5a;
    LONG r1 = UNW(NtUserCanBrokerForceForeground)(&o1);
    LONG r2 = UNW(NtUserCanBrokerForceForeground)(&o2);
    EXPECT_EQ(r1 & 0xFF, r2 & 0xFF);
}

/* ---- NtUserSetForegroundWindowForApplication / NtUserNavigateFocus -------------------------- */

RT(fg_setfgforapp_invalid_windows_fail) {
    REQUIRE(UNW(NtUserSetForegroundWindowForApplication) && U(GetForegroundWindow));
    HWND f0 = U(GetForegroundWindow)();
    HWND st = stale_window();
    HWND hs[] = { NULL, st, (HWND)(ULONG_PTR)0x7FFF0010, (HWND)(ULONG_PTR)0xFFFFFFFF, U(GetDesktopWindow)() };
    for (size_t i = 0; i < sizeof hs / sizeof *hs; i++) {
        BOOL r = UNW(NtUserSetForegroundWindowForApplication)(hs[i]) & 0xFF;
        CHECKF(!r, "case %u hwnd %p", (unsigned)i, (void *)hs[i]);
    }
    EXPECT_EQ(U(GetForegroundWindow)(), f0);
}
RT(fg_setfgforapp_desktop_class_windows_rejected) {
    REQUIRE(UNW(NtUserSetForegroundWindowForApplication) && U(GetDesktopWindow));
    HWND f0 = U(GetForegroundWindow)();
    /* the desktop window has FNID 0x29d, excluded by the ((fnid & 0xffff2fff) - 0x29d & ~2) == 0 test */
    EXPECT_EQ(UNW(NtUserSetForegroundWindowForApplication)(U(GetDesktopWindow)()) & 0xFF, 0);
    EXPECT_EQ(U(GetForegroundWindow)(), f0);
}
RT(fg_navigatefocus_invalid_windows_return_zero) {
    REQUIRE(UNW(NtUserNavigateFocus) && U(GetForegroundWindow));
    HWND f0 = U(GetForegroundWindow)();
    HWND st = stale_window();
    HWND hs[] = { NULL, st, (HWND)(ULONG_PTR)0x7FFF0010, (HWND)(ULONG_PTR)0xFFFFFFFF, U(GetDesktopWindow)() };
    for (size_t i = 0; i < sizeof hs / sizeof *hs; i++) {
        ULONG_PTR r = UNW(NtUserNavigateFocus)(hs[i]);
        CHECKF(r == 0, "case %u hwnd %p r=%p", (unsigned)i, (void *)hs[i], (void *)r);
    }
    EXPECT_EQ(U(GetForegroundWindow)(), f0);
}

/* ---- PostKeyboardInputMessage: DWM only ---------------------------------------------------- */

RT(fg_postkeyboardinputmessage_denied_outside_dwm) {
    REQUIRE(UNW(NtUserPostKeyboardInputMessage));
    unsigned char kb[0x20] = { 0 };
    LONG r = UNW(NtUserPostKeyboardInputMessage)(0, kb);
    EXPECT_EQ(r, R2_ST_ACCESS_DENIED);                  /* PsGetCurrentProcess() != g_pepDwm */
    r = UNW(NtUserPostKeyboardInputMessage)(0xFFFFFFFF, NULL);
    EXPECT_EQ(r, R2_ST_ACCESS_DENIED);                  /* arguments are not looked at */
}
RT(fg_postkeyboardinputmessage_denied_posts_nothing) {
    REQUIRE(UNW(NtUserPostKeyboardInputMessage) && U(GetQueueStatus));
    unsigned char kb[0x20] = { 0 };
    DWORD q0 = U(GetQueueStatus)(QS_KEY | QS_POSTMESSAGE);
    UNW(NtUserPostKeyboardInputMessage)(0, kb);
    DWORD q1 = U(GetQueueStatus)(QS_KEY | QS_POSTMESSAGE);
    EXPECT_EQ(HIWORD(q0) & (QS_KEY | QS_POSTMESSAGE), HIWORD(q1) & (QS_KEY | QS_POSTMESSAGE));
}

/* ---- NtUserSetThreadInputBlocked ------------------------------------------------------------- */
/* the first argument must be 0x1000; setting the flag twice (or clearing it when clear) fails with 0x57.
 * The flag is a bit of the calling thread's THREADINFO, so each test runs on a throw-away thread and
 * clears the flag again before it exits. */

static void tib_wrong_magic(void) {
    static const int magics[] = { 0, 1, 0x800, 0x1001, 0x2000, -1 };
    for (size_t i = 0; i < sizeof magics / sizeof *magics; i++) {
        SET_ERR(0);
        CHECKF((UNW(NtUserSetThreadInputBlocked)(magics[i], 1) & 0xFF) == 0, "magic 0x%x set", magics[i]);
        EXPECT_ERR(E_INVALID_PARAM);
        SET_ERR(0);
        CHECKF((UNW(NtUserSetThreadInputBlocked)(magics[i], 0) & 0xFF) == 0, "magic 0x%x clear", magics[i]);
        EXPECT_ERR(E_INVALID_PARAM);
    }
}
RT(fg_threadinputblocked_wrong_magic_rejected) { REQUIRE(UNW(NtUserSetThreadInputBlocked)); r2_gui_thread(tib_wrong_magic); }

static void tib_toggle(void) {
    SET_ERR(0);
    CHECK((UNW(NtUserSetThreadInputBlocked)(0x1000, 0) & 0xFF) == 0);     /* clearing a clear flag */
    EXPECT_ERR(E_INVALID_PARAM);
    SET_ERR(0);
    CHECK((UNW(NtUserSetThreadInputBlocked)(0x1000, 1) & 0xFF) == 1);
    SET_ERR(0);
    CHECK((UNW(NtUserSetThreadInputBlocked)(0x1000, 1) & 0xFF) == 0);     /* setting a set flag */
    EXPECT_ERR(E_INVALID_PARAM);
    SET_ERR(0);
    CHECK((UNW(NtUserSetThreadInputBlocked)(0x1000, 5) & 0xFF) == 0);     /* any nonzero is "block" */
    EXPECT_ERR(E_INVALID_PARAM);
    CHECK((UNW(NtUserSetThreadInputBlocked)(0x1000, 0) & 0xFF) == 1);
    SET_ERR(0);
    CHECK((UNW(NtUserSetThreadInputBlocked)(0x1000, 0) & 0xFF) == 0);
    EXPECT_ERR(E_INVALID_PARAM);
}
RT(fg_threadinputblocked_toggle_semantics) { REQUIRE(UNW(NtUserSetThreadInputBlocked)); r2_gui_thread(tib_toggle); }

static volatile LONG g_tib_other_result;
static DWORD WINAPI tib_other(LPVOID p) {
    (void)p;
    MSG m; U(PeekMessageW)(&m, NULL, 0, 0, PM_NOREMOVE);
    g_tib_other_result = UNW(NtUserSetThreadInputBlocked)(0x1000, 1) & 0xFF;   /* its own flag is clear */
    UNW(NtUserSetThreadInputBlocked)(0x1000, 0);
    return 0;
}
static void tib_per_thread(void) {
    CHECK((UNW(NtUserSetThreadInputBlocked)(0x1000, 1) & 0xFF) == 1);
    g_tib_other_result = -1;
    CHECK(r2_run_thread(tib_other, NULL));
    EXPECT_EQ(g_tib_other_result, 1);                                    /* the flag lives in THREADINFO, not in the process */
    CHECK((UNW(NtUserSetThreadInputBlocked)(0x1000, 0) & 0xFF) == 1);
}
RT(fg_threadinputblocked_flag_is_per_thread) { REQUIRE(UNW(NtUserSetThreadInputBlocked) && U(PeekMessageW)); r2_gui_thread(tib_per_thread); }

/* ---- NtUserIsMouseInputEnabled ------------------------------------------------------------- */

RT(fg_ismouseinputenabled_is_a_stable_bool) {
    REQUIRE(UNW(NtUserIsMouseInputEnabled) && U(PeekMessageW));
    MSG m; U(PeekMessageW)(&m, NULL, 0, 0, PM_NOREMOVE);
    LONG a = UNW(NtUserIsMouseInputEnabled)() & 0xFF, b = UNW(NtUserIsMouseInputEnabled)() & 0xFF;
    CHECK(a == 0 || a == 1);          /* ((gCursorSuppressionState - 1) & ~4) != 0 */
    EXPECT_EQ(a, b);
    t_note("IsMouseInputEnabled = %ld, SM_MOUSEPRESENT = %d", a, U(GetSystemMetrics) ? U(GetSystemMetrics)(SM_MOUSEPRESENT) : -1);
}

/* ---- NtUserMarkWindowForRawMouse ------------------------------------------------------------- */
/* win32k.sys: a bare dispatcher through a function pointer that is only called when
 * non-NULL; there is no win32kbase/win32kfull body. The call must not fault or touch the window. */
RT(fg_markwindowforrawmouse_does_not_fault) {
    REQUIRE(UNW(NtUserMarkWindowForRawMouse) && U(IsWindow));
    HWND w = r2_window(WS_POPUP); if (!w) { t_skip("no window"); return; }
    DWORD sty = (DWORD)U(GetWindowLongW)(w, GWL_STYLE), ex = (DWORD)U(GetWindowLongW)(w, GWL_EXSTYLE);
    ULONG_PTR r = UNW(NtUserMarkWindowForRawMouse)(w, 1);
    t_note("MarkWindowForRawMouse(hwnd,1) -> %p", (void *)r);
    r = UNW(NtUserMarkWindowForRawMouse)(w, 0);
    CHECK(U(IsWindow)(w));
    EXPECT_EQ((DWORD)U(GetWindowLongW)(w, GWL_STYLE), sty);
    EXPECT_EQ((DWORD)U(GetWindowLongW)(w, GWL_EXSTYLE), ex);
    U(DestroyWindow)(w);
}

/* ---- NtUserInternalGetWindowIcon -------------------------------------------------------------- */

RT(fg_internalgetwindowicon_type_range) {
    REQUIRE(UNW(NtUserInternalGetWindowIcon));
    HWND w = r2_window(WS_POPUP); if (!w) { t_skip("no window"); return; }
    static const UINT bad[] = { 3, 4, 0x100, 0x80000000u, 0xFFFFFFFFu };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        SET_ERR(0);
        HANDLE h = UNW(NtUserInternalGetWindowIcon)(w, bad[i]);       /* type >= 3 -> 0x57 */
        CHECKF(h == NULL, "type %u", bad[i]);
        EXPECT_ERR(E_INVALID_PARAM);
    }
    U(DestroyWindow)(w);
}
RT(fg_internalgetwindowicon_invalid_window) {
    REQUIRE(UNW(NtUserInternalGetWindowIcon));
    HWND st = stale_window(); REQUIRE(st);
    HWND hs[] = { NULL, st, (HWND)(ULONG_PTR)0x7FFF0010, (HWND)(ULONG_PTR)0xFFFFFFFF };
    for (size_t i = 0; i < sizeof hs / sizeof *hs; i++) {
        for (UINT t = 0; t < 3; t++) {
            HANDLE h = UNW(NtUserInternalGetWindowIcon)(hs[i], t);
            CHECKF(h == NULL, "hwnd case %u type %u", (unsigned)i, t);
        }
    }
}
RT(fg_internalgetwindowicon_returns_duplicate_icon) {
    REQUIRE(UNW(NtUserInternalGetWindowIcon) && U(DeleteObject) && U(GetObjectW) && U(GetIconInfo) && U(DestroyIcon) && U(SendMessageW));
    HWND w = r2_window(WS_POPUP); if (!w) { t_skip("no window"); return; }
    for (UINT t = 0; t < 3; t++) {
        HICON a = UNW(NtUserInternalGetWindowIcon)(w, t), b = UNW(NtUserInternalGetWindowIcon)(w, t);
        if (!a) { t_note("no icon for type %u (no class icon and no default)", t); continue; }
        ICONINFO ii; memset(&ii, 0, sizeof ii);
        CHECK(U(GetIconInfo)(a, &ii));                 /* a real icon handle */
        if (ii.hbmColor) U(DeleteObject)(ii.hbmColor);
        if (ii.hbmMask) U(DeleteObject)(ii.hbmMask);
        CHECKF(b != NULL, "second call type %u", t);
        if (a == b) t_note("type %u: both calls returned the same handle", t);
        U(DestroyIcon)(a);
        if (b && b != a) U(DestroyIcon)(b);
    }
    U(DestroyWindow)(w);
}
RT(fg_internalgetwindowicon_follows_wm_seticon) {
    REQUIRE(UNW(NtUserInternalGetWindowIcon) && U(DeleteObject) && U(GetObjectW) && U(SendMessageW) && U(LoadIconW) && U(GetIconInfo) && U(DestroyIcon));
    HWND w = r2_window(WS_POPUP); if (!w) { t_skip("no window"); return; }
    HICON ic = U(LoadIconW)(NULL, MAKEINTRESOURCEW(32516) /* IDI_INFORMATION */);
    if (!ic) { U(DestroyWindow)(w); t_skip("no stock icon"); return; }
    U(SendMessageW)(w, WM_SETICON, ICON_BIG, (LPARAM)ic);
    HICON got = UNW(NtUserInternalGetWindowIcon)(w, 1);
    CHECK(got != NULL);
    if (got) {
        if (got == ic) t_note("InternalGetWindowIcon returned the window's own icon handle");
        ICONINFO a, b; memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
        CHECK(U(GetIconInfo)(got, &a));
        CHECK(U(GetIconInfo)(ic, &b));
        BITMAP ba, bb; memset(&ba, 0, sizeof ba); memset(&bb, 0, sizeof bb);
        if (a.hbmMask && b.hbmMask && U(GetObjectW)(a.hbmMask, sizeof ba, &ba) && U(GetObjectW)(b.hbmMask, sizeof bb, &bb)) {
            EXPECT_EQ(ba.bmWidth, bb.bmWidth);
            EXPECT_EQ(ba.bmHeight, bb.bmHeight);
        }
        if (a.hbmColor) U(DeleteObject)(a.hbmColor);
        if (a.hbmMask) U(DeleteObject)(a.hbmMask);
        if (b.hbmColor) U(DeleteObject)(b.hbmColor);
        if (b.hbmMask) U(DeleteObject)(b.hbmMask);
        if (got != ic) U(DestroyIcon)(got);
    }
    U(SendMessageW)(w, WM_SETICON, ICON_BIG, 0);
    U(DestroyWindow)(w);
}

/* ---- NtUserGetCursorFrameInfo -------------------------------------------------------------- */

RT(fg_cursorframeinfo_invalid_cursor) {
    REQUIRE(UNW(NtUserGetCursorFrameInfo) && U(PeekMessageW));
    MSG m; U(PeekMessageW)(&m, NULL, 0, 0, PM_NOREMOVE);
    static const ULONG_PTR cs[] = { 0, 0x7FFF0001, 0x7FFF0010, 0x7FFFFFFF, 0xFFFFFFFF };
    for (size_t i = 0; i < sizeof cs / sizeof *cs; i++) {
        DWORD rate = 0x5a5a5a5a; int steps = 0x5a5a5a5a;
        HANDLE h = UNW(NtUserGetCursorFrameInfo)((HCURSOR)cs[i], 0, &rate, &steps);   /* HMValidateHandle(TYPE_CURSOR) fails */
        CHECKF(h == NULL, "cursor %p", (void *)cs[i]);
        EXPECT_EQ(rate, 0x5a5a5a5a);
        EXPECT_EQ(steps, 0x5a5a5a5a);
    }
}
RT(fg_cursorframeinfo_non_window_object_handle) {
    REQUIRE(UNW(NtUserGetCursorFrameInfo) && U(CreateWindowExW));
    HWND w = r2_window(WS_POPUP); if (!w) { t_skip("no window"); return; }          /* a valid USER handle of the wrong type */
    DWORD rate = 0x5a5a5a5a; int steps = 0x5a5a5a5a;
    HANDLE h = UNW(NtUserGetCursorFrameInfo)((HCURSOR)w, 0, &rate, &steps);
    CHECK(h == NULL);
    EXPECT_EQ(rate, 0x5a5a5a5a);
    U(DestroyWindow)(w);
}
RT(fg_cursorframeinfo_static_cursor_has_one_step) {
    REQUIRE(UNW(NtUserGetCursorFrameInfo) && U(LoadCursorW));
    static const int ids[] = { 32512 /* ARROW */, 32513 /* IBEAM */, 32514 /* WAIT */, 32515 /* CROSS */, 32649 /* HAND */ };
    int tested = 0;
    for (size_t i = 0; i < sizeof ids / sizeof *ids; i++) {
        HCURSOR c = U(LoadCursorW)(NULL, MAKEINTRESOURCEW(ids[i]));
        if (!c) continue;
        DWORD rate = 0x5a5a5a5a; int steps = 0x5a5a5a5a;
        HANDLE h = UNW(NtUserGetCursorFrameInfo)(c, 0, &rate, &steps);
        t_info("cursor %d frame0: handle %p rate %lu steps %d", ids[i], h, rate, steps);   /* an animated cursor reports a rate that differs between guests */
        if (!h) continue;
        tested++;
        if (steps == 1) EXPECT_EQ(rate, 0);              /* _GetCursorFrameInfo: non-animated -> steps 1, rate 0 */
        CHECK(steps >= 1);
    }
    if (!tested) t_skip("no stock cursors");
}
RT(fg_cursorframeinfo_repeat_query_returns_cursor) {
    REQUIRE(UNW(NtUserGetCursorFrameInfo) && U(LoadCursorW) && U(GetCursorInfo));
    HCURSOR c = U(LoadCursorW)(NULL, MAKEINTRESOURCEW(32512)); REQUIRE(c);
    DWORD rate = 0; int steps = 0;
    HANDLE a = UNW(NtUserGetCursorFrameInfo)(c, 0, &rate, &steps);
    HANDLE b = UNW(NtUserGetCursorFrameInfo)(c, 0, &rate, &steps);
    t_note("GetCursorFrameInfo handles %p %p (a DPI-virtualized cursor may be a copy)", a, b);
    CHECK(a != NULL);
}

/* ---- NtUserPromoteMouseInPointer ---------------------------------------------------------- */

RT(fg_promotemouseinpointer_denied_without_mouse_in_pointer) {
    REQUIRE(UNW(NtUserPromoteMouseInPointer) && U(PeekMessageW));
    MSG m; U(PeekMessageW)(&m, NULL, 0, 0, PM_NOREMOVE);
    /* IsMiPEnabledForThread() is FALSE unless the process called EnableMouseInPointer: ERROR_ACCESS_DENIED */
    SET_ERR(0);
    LONG r = UNW(NtUserPromoteMouseInPointer)();
    DWORD e = GetLastError();
    if ((r & 0xFF) == 0) EXPECT_EQ(e, E_ACCESS);
    else t_note("mouse-in-pointer already enabled in this process");
}

/* Process-wide mouse-in-pointer state is switched only in a child that has not touched it before. */
#define KC(c) do { if (!(c)) { nfail++; printf("  FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)
CHILD(r2ip_promote_mip) {
    int nfail = 0; (void)argc; (void)argv;
    if (!UNW(NtUserPromoteMouseInPointer) || !U(EnableMouseInPointer)) return 0;
    SET_ERR(0);
    LONG r = UNW(NtUserPromoteMouseInPointer)();
    KC((r & 0xFF) == 0);                                 /* IsMiPEnabledForThread() == FALSE in a fresh process */
    KC(GetLastError() == 5);
    if (!U(EnableMouseInPointer)(TRUE)) { printf("  NOTE EnableMouseInPointer denied: %lu\n", GetLastError()); return nfail; }
    SET_ERR(0);
    r = UNW(NtUserPromoteMouseInPointer)();
    printf("  NOTE PromoteMouseInPointer after EnableMouseInPointer -> %ld gle=%lu\n", r, GetLastError());
    if ((r & 0xFF) == 0) KC(GetLastError() == 5);        /* the body's only failure code */
    return nfail;
}
RT(fg_promotemouseinpointer_in_pristine_child) {
    REQUIRE(UNW(NtUserPromoteMouseInPointer));
    struct k_child_result r = k_child_run("r2ip_promote_mip", "", 20000);
    CHECKF(r.exit_code == 0, "child exit 0x%lx", (unsigned long)r.exit_code);
}
