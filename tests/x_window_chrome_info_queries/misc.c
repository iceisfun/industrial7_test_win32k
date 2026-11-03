/* x_window_chrome_info_queries / misc: NtUserGetCaretPos, NtUserGetInternalWindowPos, NtUserGetWindowMinimizeRect,
 * NtUserGetTopLevelWindow, NtUserCheckWindowThreadDesktop, NtUserGetDesktopID, NtUserGetSharedWindowData.
 *
 * GetCaretPos(POINT *): copies the thread queue's caret position, succeeds with or without a caret, and an unwritable
 *   point is ERROR_NOACCESS. The position is per thread queue and is not changed by a caret of another thread.
 * GetInternalWindowPos(hwnd, RECT *normal, POINT *min): returns the show command (SW_SHOWNORMAL, SW_SHOWMINIMIZED or
 *   SW_SHOWMAXIMIZED by the style bits) and the normal rectangle and minimized position as GetWindowPlacement reports
 *   them; both outputs are optional; a NULL or stale window is 0 with ERROR_INVALID_WINDOW_HANDLE.
 * GetWindowMinimizeRect(hwnd, RECT *): a non-empty rectangle on the virtual screen whose size does not depend on where the
 *   window is; stale windows and the desktop window fail.
 * GetTopLevelWindow(hwnd): walks up the parent chain: a top-level window is its own answer, a child gives its top-level
 *   ancestor, an owned popup is its own answer; the desktop window and a message-only window have no top-level answer.
 * CheckWindowThreadDesktop(hwnd, tid): TRUE when the thread tid exists and runs on the desktop of the window; a thread id
 *   that does not exist is ERROR_INVALID_PARAMETER, a window the caller cannot see ERROR_INVALID_WINDOW_HANDLE.
 * GetDesktopID(type, ULONGLONG *): refused for callers that are not the compositor, with ERROR_ACCESS_DENIED, before the
 *   type or the output pointer are looked at; the output is not written.
 * GetSharedWindowData(a, b, c, d): answers 0 and sets no error for every window handle, stale or not.
 *
 * Determinism: only symbolic results are printed.
 */
#define XPFX "misc_"
#include "x.h"

/* ================= GetCaretPos ================= */
XT(caret_pos_without_caret_succeeds) {
    NEED(NtUserGetCaretPos);
    POINT p = { 0x1234, 0x5678 };
    XOK(UN(NtUserGetCaretPos)(&p));
    CHECK(p.x != 0x1234 || p.y != 0x5678);   /* the point is always written */
}
XT(caret_pos_null_pointer) { NEED(NtUserGetCaretPos); XFAIL(UN(NtUserGetCaretPos)(NULL), XE_NOACCESS); }
XT(caret_pos_unwritable_pointer) {
    NEED(NtUserGetCaretPos);
    XFAIL(UN(NtUserGetCaretPos)((POINT *)XP_BADPTR), XE_NOACCESS);
    XFAIL(UN(NtUserGetCaretPos)((POINT *)XP_KERNELPTR), XE_NOACCESS);
}
XT(caret_pos_follows_set_caret_pos) {
    NEED(NtUserGetCaretPos);
    HWND w = xw_win(WS_OVERLAPPEDWINDOW, 0, L"STATIC");
    if (!U(CreateCaret)(w, NULL, 2, 8)) { t_skip("no caret on a hidden window"); xw_kill(w); return; }
    static const int xy[][2] = { { 0, 0 }, { 5, 6 }, { 100, 3 }, { 7, 250 }, { 1, 1 }, { 33, 44 } };
    for (unsigned i = 0; i < sizeof xy / sizeof xy[0]; i++) {
        CHECK(U(SetCaretPos)(xy[i][0], xy[i][1]));
        POINT p = { -9, -9 }, g = { -9, -9 };
        XOK(UN(NtUserGetCaretPos)(&p)); CHECK(U(GetCaretPos)(&g));
        CHECKF(p.x == xy[i][0] && p.y == xy[i][1], "case %u: got (%ld,%ld)", i, p.x, p.y);
        CHECK(p.x == g.x && p.y == g.y);
    }
    U(DestroyCaret)();
    xw_kill(w);
}
XT(caret_pos_survives_destroying_the_caret) {
    NEED(NtUserGetCaretPos);
    HWND w = xw_win(WS_OVERLAPPEDWINDOW, 0, L"STATIC");
    if (!U(CreateCaret)(w, NULL, 2, 8)) { t_skip("no caret on a hidden window"); xw_kill(w); return; }
    CHECK(U(SetCaretPos)(12, 13)); CHECK(U(DestroyCaret)());
    POINT p = { -9, -9 }; XOK(UN(NtUserGetCaretPos)(&p));
    CHECKF(p.x == 12 && p.y == 13, "(%ld,%ld)", p.x, p.y);
    xw_kill(w);
}
XT(caret_pos_negative_coordinates_are_kept) {
    NEED(NtUserGetCaretPos);
    HWND w = xw_win(WS_OVERLAPPEDWINDOW, 0, L"STATIC");
    if (!U(CreateCaret)(w, NULL, 2, 8)) { t_skip("no caret on a hidden window"); xw_kill(w); return; }
    CHECK(U(SetCaretPos)(-5, -7));
    POINT p = { 0, 0 }; XOK(UN(NtUserGetCaretPos)(&p));
    CHECKF(p.x == -5 && p.y == -7, "(%ld,%ld)", p.x, p.y);
    U(DestroyCaret)(); xw_kill(w);
}
static DWORD WINAPI caret_worker(LPVOID arg) {
    HWND w = xw_win(WS_OVERLAPPEDWINDOW, 0, L"STATIC"); int *res = arg;
    res[0] = 0;
    if (w && U(CreateCaret)(w, NULL, 2, 8) && U(SetCaretPos)(99, 77)) {
        POINT p = { 0, 0 }; UN(NtUserGetCaretPos)(&p);
        res[0] = (p.x == 99 && p.y == 77); res[1] = 1;
        U(DestroyCaret)();
    }
    xw_kill(w);
    return 0;
}
XT(caret_pos_is_per_thread) {
    NEED(NtUserGetCaretPos);
    HWND w = xw_win(WS_OVERLAPPEDWINDOW, 0, L"STATIC");
    if (!U(CreateCaret)(w, NULL, 2, 8)) { t_skip("no caret on a hidden window"); xw_kill(w); return; }
    CHECK(U(SetCaretPos)(21, 22));
    int res[2] = { 0, 0 };
    HANDLE t = CreateThread(NULL, 0, caret_worker, res, 0, NULL);
    if (!t) { t_skip("no thread"); U(DestroyCaret)(); xw_kill(w); return; }
    WaitForSingleObject(t, 10000); CloseHandle(t);
    if (!res[1]) t_skip("helper thread could not create a caret");
    else {
        CHECK(res[0]);
        POINT p = { 0, 0 }; XOK(UN(NtUserGetCaretPos)(&p));
        CHECKF(p.x == 21 && p.y == 22, "(%ld,%ld)", p.x, p.y);
    }
    U(DestroyCaret)(); xw_kill(w);
}
XT(caret_pos_user32_front_door_matches) {
    NEED(NtUserGetCaretPos);
    POINT a = { 1, 1 }, b = { 2, 2 };
    CHECK(U(GetCaretPos)(&a)); XOK(UN(NtUserGetCaretPos)(&b));
    CHECK(a.x == b.x && a.y == b.y);
}
XT(caret_pos_user32_null_fails) {
    NEED(NtUserGetCaretPos);
    SET_ERR(XSENT); CHECK(!U(GetCaretPos)(NULL)); EXPECT_ERR(XE_NOACCESS);
}

/* ================= GetInternalWindowPos ================= */
static HWND pw(DWORD style) { return xw_win(WS_OVERLAPPEDWINDOW | style, 0, L"STATIC"); }
XT(internal_pos_normal_window) {
    NEED(NtUserGetInternalWindowPos);
    HWND w = pw(0); RECT rc = { 7, 7, 7, 7 }, wr; POINT pt = { 7, 7 };
    ULONG_PTR cmd = UN(NtUserGetInternalWindowPos)(w, &rc, &pt); U(GetWindowRect)(w, &wr);
    EXPECT_EQ(R32(cmd), SW_SHOWNORMAL);
    CHECK(!memcmp(&rc, &wr, sizeof rc));
    CHECK(pt.x == -1 && pt.y == -1);
    xw_kill(w);
}
XT(internal_pos_agrees_with_get_window_placement) {
    NEED(NtUserGetInternalWindowPos);
    HWND w = pw(0); RECT rc; POINT pt; WINDOWPLACEMENT wp; memset(&wp, 0, sizeof wp); wp.length = sizeof wp;
    ULONG_PTR cmd = UN(NtUserGetInternalWindowPos)(w, &rc, &pt); CHECK(U(GetWindowPlacement)(w, &wp));
    EXPECT_EQ(R32(cmd), wp.showCmd);
    CHECK(!memcmp(&rc, &wp.rcNormalPosition, sizeof rc));
    CHECK(pt.x == wp.ptMinPosition.x && pt.y == wp.ptMinPosition.y);
    xw_kill(w);
}
static void ipos_style(DWORD bit) {
    NEED(NtUserGetInternalWindowPos);
    HWND w = pw(bit); RECT rc; POINT pt; WINDOWPLACEMENT wp; memset(&wp, 0, sizeof wp); wp.length = sizeof wp;
    ULONG_PTR cmd = UN(NtUserGetInternalWindowPos)(w, &rc, &pt); CHECK(U(GetWindowPlacement)(w, &wp));
    EXPECT_EQ(R32(cmd), wp.showCmd);
    CHECK(pt.x == wp.ptMinPosition.x && pt.y == wp.ptMinPosition.y);
    t_note("style bit %s", bit == WS_MINIMIZE ? "minimize" : "maximize");
    if (bit == WS_MINIMIZE) EXPECT_EQ(R32(cmd), SW_SHOWMINIMIZED); else EXPECT_EQ(R32(cmd), SW_SHOWMAXIMIZED);
    xw_kill(w);
}
XT(internal_pos_minimized_style) { ipos_style(WS_MINIMIZE); }
XT(internal_pos_maximized_style) { ipos_style(WS_MAXIMIZE); }
XT(internal_pos_normal_rect_survives_move) {
    NEED(NtUserGetInternalWindowPos);
    HWND w = pw(0); RECT rc, wr; POINT pt;
    U(SetWindowPos)(w, NULL, 40, 50, 260, 180, SWP_NOZORDER | SWP_NOACTIVATE);
    UN(NtUserGetInternalWindowPos)(w, &rc, &pt); U(GetWindowRect)(w, &wr);
    CHECK(!memcmp(&rc, &wr, sizeof rc));
    xw_kill(w);
}
XT(internal_pos_null_outputs_allowed) {
    NEED(NtUserGetInternalWindowPos);
    HWND w = pw(0); RECT rc; POINT pt;
    EXPECT_EQ(R32(UN(NtUserGetInternalWindowPos)(w, NULL, NULL)), SW_SHOWNORMAL);
    EXPECT_EQ(R32(UN(NtUserGetInternalWindowPos)(w, &rc, NULL)), SW_SHOWNORMAL);
    EXPECT_EQ(R32(UN(NtUserGetInternalWindowPos)(w, NULL, &pt)), SW_SHOWNORMAL);
    xw_kill(w);
}
XT(internal_pos_unwritable_outputs_fail_quietly) {
    NEED(NtUserGetInternalWindowPos);
    HWND w = xw_win(WS_OVERLAPPEDWINDOW, 0, L"STATIC"); RECT rc; POINT pt;
    SET_ERR(XSENT); EXPECT_EQ(R32(UN(NtUserGetInternalWindowPos)(w, (RECT *)XP_BADPTR, &pt)), 0); EXPECT_ERR(XSENT);
    SET_ERR(XSENT); EXPECT_EQ(R32(UN(NtUserGetInternalWindowPos)(w, &rc, (POINT *)XP_BADPTR)), 0); EXPECT_ERR(XSENT);
    SET_ERR(XSENT); EXPECT_EQ(R32(UN(NtUserGetInternalWindowPos)(w, (RECT *)XP_KERNELPTR, NULL)), 0); EXPECT_ERR(XSENT);
    xw_kill(w);
}
static void ipos_bad(HWND h, const char *what) {
    RECT rc; POINT pt; memset(&rc, XFILL, sizeof rc); memset(&pt, XFILL, sizeof pt);
    SET_ERR(XSENT); ULONG_PTR r = UN(NtUserGetInternalWindowPos)(h, &rc, &pt);
    CHECKF(R32(r) == 0 && GetLastError() == XE_INVALID_HWND, "%s: r=%lu gle=%lu", what, (unsigned long)R32(r), GetLastError());
    CHECKF(xw_untouched(&rc, sizeof rc) && xw_untouched(&pt, sizeof pt), "%s: output written", what);
}
XT(internal_pos_stale_or_garbage_window) {
    NEED(NtUserGetInternalWindowPos);
    ipos_bad(NULL, "null"); ipos_bad((HWND)(ULONG_PTR)0x12345678, "garbage"); ipos_bad((HWND)INVALID_HANDLE_VALUE, "invalid handle value"); ipos_bad(xw_dead(), "destroyed");
}
XT(internal_pos_desktop_window_is_normal) {
    NEED(NtUserGetInternalWindowPos);
    SET_ERR(XSENT);
    EXPECT_EQ(R32(UN(NtUserGetInternalWindowPos)(U(GetDesktopWindow)(), NULL, NULL)), SW_SHOWNORMAL);
}
XT(internal_pos_other_thread_window) {
    NEED(NtUserGetInternalWindowPos);
    struct xw_other o;
    if (!xw_other_start(&o, WS_OVERLAPPEDWINDOW, 0, L"STATIC")) { t_skip("helper thread window"); return; }
    RECT rc, wr; POINT pt; XOK(UN(NtUserGetInternalWindowPos)(o.hwnd, &rc, &pt)); U(GetWindowRect)(o.hwnd, &wr);
    CHECK(!memcmp(&rc, &wr, sizeof rc));
    xw_other_stop(&o);
}

/* ================= GetWindowMinimizeRect ================= */
static void vscreen(RECT *v) {
    v->left = U(GetSystemMetrics)(SM_XVIRTUALSCREEN); v->top = U(GetSystemMetrics)(SM_YVIRTUALSCREEN);
    v->right = v->left + U(GetSystemMetrics)(SM_CXVIRTUALSCREEN); v->bottom = v->top + U(GetSystemMetrics)(SM_CYVIRTUALSCREEN);
}
XT(minimize_rect_is_on_the_virtual_screen) {
    NEED(NtUserGetWindowMinimizeRect);
    HWND w = pw(0); RECT r, v; XOK(UN(NtUserGetWindowMinimizeRect)(w, &r)); vscreen(&v);
    CHECK(r.right > r.left && r.bottom > r.top);
    CHECK(r.left >= v.left && r.top >= v.top && r.right <= v.right && r.bottom <= v.bottom);
    xw_kill(w);
}
XT(minimize_rect_size_ignores_window_position) {
    NEED(NtUserGetWindowMinimizeRect);
    HWND w = pw(0); RECT a, b; XOK(UN(NtUserGetWindowMinimizeRect)(w, &a));
    U(SetWindowPos)(w, NULL, -20000, -20000, 300, 300, SWP_NOZORDER | SWP_NOACTIVATE);
    XOK(UN(NtUserGetWindowMinimizeRect)(w, &b));
    EXPECT_EQ(b.right - b.left, a.right - a.left); EXPECT_EQ(b.bottom - b.top, a.bottom - a.top);
    xw_kill(w);
}
XT(minimize_rect_stable_between_calls) {
    NEED(NtUserGetWindowMinimizeRect);
    HWND w = pw(0); RECT a, b; XOK(UN(NtUserGetWindowMinimizeRect)(w, &a));
    int same = 1; for (int i = 0; i < 20; i++) { XOK(UN(NtUserGetWindowMinimizeRect)(w, &b)); if (memcmp(&a, &b, sizeof a)) same = 0; }
    CHECK(same);
    xw_kill(w);
}
XT(minimize_rect_user32_front_door_matches) {
    NEED(NtUserGetWindowMinimizeRect);
    BOOL (WINAPI *f)(HWND, RECT *) = (void *)t_proc_in("user32.dll", "GetWindowMinimizeRect");
    if (!f) { t_skip("user32!GetWindowMinimizeRect not exported"); return; }
    HWND w = pw(0); RECT a, b; XOK(UN(NtUserGetWindowMinimizeRect)(w, &a)); CHECK(f(w, &b));
    CHECK(!memcmp(&a, &b, sizeof a));
    xw_kill(w);
}
static void minrect_bad(HWND h, const char *what) {
    RECT r; memset(&r, XFILL, sizeof r);
    SET_ERR(XSENT); ULONG_PTR rv = UN(NtUserGetWindowMinimizeRect)(h, &r);
    CHECKF(R32(rv) == 0 && GetLastError() == XE_INVALID_HWND, "%s: r=%lu gle=%lu", what, (unsigned long)R32(rv), GetLastError());
    CHECKF(xw_untouched(&r, sizeof r), "%s: output written", what);
}
XT(minimize_rect_stale_or_garbage_window) {
    NEED(NtUserGetWindowMinimizeRect);
    minrect_bad(NULL, "null"); minrect_bad((HWND)(ULONG_PTR)0x12345678, "garbage"); minrect_bad(xw_dead(), "destroyed");
}
XT(minimize_rect_null_output) {
    NEED(NtUserGetWindowMinimizeRect);
    HWND w = pw(0); XFAIL(UN(NtUserGetWindowMinimizeRect)(w, NULL), XE_INVALID_PARAMETER); xw_kill(w);
}
XT(minimize_rect_desktop_window_fails_quietly) {
    NEED(NtUserGetWindowMinimizeRect);
    RECT r; memset(&r, XFILL, sizeof r); SET_ERR(XSENT);
    EXPECT_EQ(R32(UN(NtUserGetWindowMinimizeRect)(U(GetDesktopWindow)(), &r)), 0);
    EXPECT_ERR(XSENT); CHECK(xw_untouched(&r, sizeof r));
}
/* ================= GetTopLevelWindow ================= */
static ULONG_PTR tlw(HWND h) { return UN(NtUserGetTopLevelWindow)(h); }
XT(toplevel_of_top_level_is_itself) { NEED(NtUserGetTopLevelWindow); HWND w = pw(0); EXPECT_EQ(tlw(w), w); xw_kill(w); }
XT(toplevel_of_popup_is_itself) { NEED(NtUserGetTopLevelWindow); HWND w = xw_win(WS_POPUP, 0, L"STATIC"); EXPECT_EQ(tlw(w), w); xw_kill(w); }
XT(toplevel_of_child_is_parent) {
    NEED(NtUserGetTopLevelWindow);
    HWND p = pw(0), c = xw_child(p, 0, L"STATIC"); EXPECT_EQ(tlw(c), p); xw_kill(p);
}
XT(toplevel_of_deep_child_is_root) {
    NEED(NtUserGetTopLevelWindow);
    HWND p = pw(0), c = p; HWND ch[5];
    for (int i = 0; i < 5; i++) { ch[i] = xw_child(c, 0, L"STATIC"); c = ch[i]; }
    for (int i = 0; i < 5; i++) EXPECT_EQ(tlw(ch[i]), p);
    xw_kill(p);
}
XT(toplevel_of_owned_popup_is_the_popup) {
    NEED(NtUserGetTopLevelWindow);
    HWND o = pw(0); HWND p = U(CreateWindowExW)(0, L"STATIC", L"", WS_POPUP, 0, 0, 10, 10, o, NULL, NULL, NULL);
    EXPECT_EQ(tlw(p), p); xw_kill(o);
}
XT(toplevel_matches_get_ancestor_root) {
    NEED(NtUserGetTopLevelWindow);
    HWND p = pw(0), c = xw_child(p, 0, L"STATIC"), g = xw_child(c, 0, L"STATIC");
    EXPECT_EQ(tlw(g), U(GetAncestor)(g, GA_ROOT)); EXPECT_EQ(tlw(c), U(GetAncestor)(c, GA_ROOT)); EXPECT_EQ(tlw(p), U(GetAncestor)(p, GA_ROOT));
    xw_kill(p);
}
XT(toplevel_of_desktop_window_is_null) { NEED(NtUserGetTopLevelWindow); EXPECT_EQ(tlw(U(GetDesktopWindow)()), 0); }
XT(toplevel_of_message_only_window_is_null) {
    NEED(NtUserGetTopLevelWindow);
    HWND m = U(CreateWindowExW)(0, L"STATIC", L"", WS_POPUP, 0, 0, 10, 10, HWND_MESSAGE, NULL, NULL, NULL);
    if (!m) { t_skip("message-only window"); return; }
    EXPECT_EQ(tlw(m), 0);
    xw_kill(m);
}
XT(toplevel_stale_or_garbage_window) {
    NEED(NtUserGetTopLevelWindow);
    XFAIL(tlw(NULL), XE_INVALID_HWND); XFAIL(tlw((HWND)(ULONG_PTR)0x12345678), XE_INVALID_HWND);
    XFAIL(tlw((HWND)INVALID_HANDLE_VALUE), XE_INVALID_HWND); XFAIL(tlw(xw_dead()), XE_INVALID_HWND);
}
XT(toplevel_of_child_of_other_thread_window) {
    NEED(NtUserGetTopLevelWindow);
    struct xw_other o;
    if (!xw_other_start(&o, WS_OVERLAPPEDWINDOW, 0, L"STATIC")) { t_skip("helper thread window"); return; }
    EXPECT_EQ(tlw(o.hwnd), o.hwnd);
    HWND c = U(CreateWindowExW)(0, L"STATIC", L"", WS_CHILD, 0, 0, 10, 10, o.hwnd, NULL, NULL, NULL);
    if (c) { EXPECT_EQ(tlw(c), o.hwnd); U(DestroyWindow)(c); }
    xw_other_stop(&o);
}
XT(toplevel_after_reparenting) {
    NEED(NtUserGetTopLevelWindow);
    HWND a = pw(0), b = pw(0), c = xw_child(a, 0, L"STATIC");
    EXPECT_EQ(tlw(c), a);
    U(SetParent)(c, b);
    EXPECT_EQ(tlw(c), b);
    xw_kill(a); xw_kill(b);
}
XT(toplevel_user32_export_agrees) {
    NEED(NtUserGetTopLevelWindow);
    HWND (WINAPI *f)(HWND) = (void *)t_proc_in("user32.dll", "GetTopLevelWindow");
    if (!f) { t_skip("user32!GetTopLevelWindow not exported"); return; }
    HWND p = pw(0), c = xw_child(p, 0, L"STATIC");
    CHECK(f(c) == p); CHECK(f(p) == p);
    xw_kill(p);
}

/* ================= CheckWindowThreadDesktop ================= */
static ULONG_PTR cwtd(HWND h, DWORD tid) { return UN(NtUserCheckWindowThreadDesktop)(h, tid); }
XT(cwtd_own_window_own_thread) { NEED(NtUserCheckWindowThreadDesktop); HWND w = pw(0); XOK(cwtd(w, GetCurrentThreadId())); xw_kill(w); }
XT(cwtd_desktop_window_own_thread) { NEED(NtUserCheckWindowThreadDesktop); XOK(cwtd(U(GetDesktopWindow)(), GetCurrentThreadId())); }
XT(cwtd_window_of_other_thread_same_desktop) {
    NEED(NtUserCheckWindowThreadDesktop);
    struct xw_other o;
    if (!xw_other_start(&o, WS_POPUP, 0, L"STATIC")) { t_skip("helper thread window"); return; }
    XOK(cwtd(o.hwnd, GetCurrentThreadId()));
    XOK(cwtd(o.hwnd, o.tid));
    HWND w = pw(0); XOK(cwtd(w, o.tid)); xw_kill(w);
    xw_other_stop(&o);
}
static void cwtd_bad_tid(DWORD tid) {
    HWND w = pw(0); XFAIL(cwtd(w, tid), XE_INVALID_PARAMETER); xw_kill(w);
}
XT(cwtd_thread_ids_that_do_not_exist_are_invalid_parameters) {
    NEED(NtUserCheckWindowThreadDesktop);
    static const DWORD tids[] = { 0, 1, 2, 3, 4, 0x10, 0xFFFFF0, 0xFFFFFFFF };
    for (unsigned i = 0; i < sizeof tids / sizeof tids[0]; i++) cwtd_bad_tid(tids[i]);
}
XT(cwtd_process_id_is_not_a_thread) { NEED(NtUserCheckWindowThreadDesktop); cwtd_bad_tid(GetCurrentProcessId()); }
XT(cwtd_thread_that_has_exited) {
    NEED(NtUserCheckWindowThreadDesktop);
    DWORD tid = 0; HANDLE t = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)(void *)Sleep, (LPVOID)(ULONG_PTR)0, 0, &tid);
    if (!t) { t_skip("no thread"); return; }
    WaitForSingleObject(t, 10000); CloseHandle(t);
    HWND w = pw(0); XFAIL(cwtd(w, tid), XE_INVALID_PARAMETER); xw_kill(w);
}
XT(cwtd_stale_or_garbage_window) {
    NEED(NtUserCheckWindowThreadDesktop);
    DWORD t = GetCurrentThreadId();
    XFAIL(cwtd(NULL, t), XE_INVALID_HWND); XFAIL(cwtd((HWND)(ULONG_PTR)0x12345678, t), XE_INVALID_HWND);
    XFAIL(cwtd((HWND)INVALID_HANDLE_VALUE, t), XE_INVALID_HWND); XFAIL(cwtd(xw_dead(), t), XE_INVALID_HWND);
}
XT(cwtd_window_is_checked_before_thread) {
    NEED(NtUserCheckWindowThreadDesktop);
    XFAIL(cwtd(NULL, 0), XE_INVALID_HWND);
}

struct dsk { HANDLE ready, stop; HWND hwnd; DWORD tid; int ok; };
static DWORD WINAPI dsk_main(LPVOID p) {
    struct dsk *d = p; HDESK dk = CreateDesktopW(L"w32kt_chrome_q", NULL, NULL, 0, GENERIC_ALL, NULL);
    d->tid = GetCurrentThreadId();
    if (dk && SetThreadDesktop(dk)) { d->hwnd = xw_win(WS_POPUP, 0, L"STATIC"); d->ok = d->hwnd != NULL; }
    SetEvent(d->ready);
    if (d->ok) WaitForSingleObject(d->stop, 20000);
    if (d->hwnd) U(DestroyWindow)(d->hwnd);
    if (dk) CloseDesktop(dk);
    return 0;
}
static int dsk_start(struct dsk *d, HANDLE *th) {
    memset(d, 0, sizeof *d); d->ready = CreateEventA(NULL, TRUE, FALSE, NULL); d->stop = CreateEventA(NULL, TRUE, FALSE, NULL);
    *th = CreateThread(NULL, 0, dsk_main, d, 0, NULL);
    if (!*th || WaitForSingleObject(d->ready, 10000) != WAIT_OBJECT_0 || !d->ok) return 0;
    return 1;
}
static void dsk_stop(struct dsk *d, HANDLE th) {
    SetEvent(d->stop); if (th) { WaitForSingleObject(th, 10000); CloseHandle(th); }
    CloseHandle(d->ready); CloseHandle(d->stop);
}
XT(cwtd_window_of_another_desktop_is_not_visible_to_the_caller) {
    NEED(NtUserCheckWindowThreadDesktop);
    struct dsk d; HANDLE th;
    if (!dsk_start(&d, &th)) { t_skip("cannot create a second desktop here"); dsk_stop(&d, th); return; }
    XFAIL(cwtd(d.hwnd, GetCurrentThreadId()), XE_INVALID_HWND);
    dsk_stop(&d, th);
}
XT(cwtd_thread_of_another_desktop_does_not_match_a_local_window) {
    NEED(NtUserCheckWindowThreadDesktop);
    struct dsk d; HANDLE th;
    if (!dsk_start(&d, &th)) { t_skip("cannot create a second desktop here"); dsk_stop(&d, th); return; }
    HWND w = pw(0); XFAIL(cwtd(w, d.tid), XE_INVALID_HWND); xw_kill(w);
    dsk_stop(&d, th);
}
XT(cwtd_window_of_another_desktop_matches_its_own_thread) {
    NEED(NtUserCheckWindowThreadDesktop);
    struct dsk d; HANDLE th;
    if (!dsk_start(&d, &th)) { t_skip("cannot create a second desktop here"); dsk_stop(&d, th); return; }
    /* observed: the window of the other desktop is still matched with the thread that owns it */
    XOK(cwtd(d.hwnd, d.tid));
    dsk_stop(&d, th);
}

/* ================= GetDesktopID ================= */
static void did_denied(DWORD type, ULONGLONG *out) {
    NEED(NtUserGetDesktopID);
    if (out && (void *)out != XP_BADPTR && (void *)out != XP_KERNELPTR) *out = 0xCCCCCCCCCCCCCCCCull;
    XFAIL(UN(NtUserGetDesktopID)(type, out), XE_ACCESS_DENIED);
    if (out && (void *)out != XP_BADPTR && (void *)out != XP_KERNELPTR) CHECK(*out == 0xCCCCCCCCCCCCCCCCull);
}
XT(desktop_id_denied_for_every_type) {
    static const DWORD types[] = { 0, 1, 2, 3, 0x7FFFFFFF, 0xFFFFFFFF };
    for (unsigned i = 0; i < sizeof types / sizeof types[0]; i++) { ULONGLONG v; did_denied(types[i], &v); }
}
XT(desktop_id_denied_before_the_output_pointer_is_looked_at) {
    did_denied(1, NULL); did_denied(1, (ULONGLONG *)XP_BADPTR); did_denied(1, (ULONGLONG *)XP_KERNELPTR);
}
XT(desktop_id_denied_on_another_thread) {
    NEED(NtUserGetDesktopID);
    struct xw_other o;
    if (!xw_other_start(&o, WS_POPUP, 0, L"STATIC")) { t_skip("helper thread window"); return; }
    ULONGLONG v = 0xCCCCCCCCCCCCCCCCull; XFAIL(UN(NtUserGetDesktopID)(1, &v), XE_ACCESS_DENIED);
    xw_other_stop(&o);
}
XT(desktop_id_user32_export_denied) {
    NEED(NtUserGetDesktopID);
    BOOL (WINAPI *f)(DWORD, ULONGLONG *) = (void *)t_proc_in("user32.dll", "GetDesktopID");
    if (!f) { t_skip("user32!GetDesktopID not exported"); return; }
    ULONGLONG v = 0xCCCCCCCCCCCCCCCCull; SET_ERR(XSENT);
    CHECK(!f(1, &v)); EXPECT_ERR(XE_ACCESS_DENIED); CHECK(v == 0xCCCCCCCCCCCCCCCCull);
}

/* ================= GetSharedWindowData ================= */
static void swd(HWND h, ULONG_PTR b) {
    NEED(NtUserGetSharedWindowData);
    SET_ERR(XSENT);
    EXPECT_EQ(UN(NtUserGetSharedWindowData)((ULONG_PTR)h, b, 0, 0), 0);
    EXPECT_ERR(XSENT);
}
XT(shared_window_data_answers_zero_without_an_error) {
    HWND w = pw(0);
    swd(w, 0); swd(w, 1); swd(w, 2); swd(w, 0x1000);
    xw_kill(w);
}
XT(shared_window_data_answers_zero_for_stale_and_special_windows) {
    swd(NULL, 0); swd((HWND)(ULONG_PTR)0x12345678, 0); swd(xw_dead(), 0); swd(U(GetDesktopWindow)(), 0);
}
XT(shared_window_data_other_thread_window) {
    NEED(NtUserGetSharedWindowData);
    struct xw_other o;
    if (!xw_other_start(&o, WS_POPUP, 0, L"STATIC")) { t_skip("helper thread window"); return; }
    swd(o.hwnd, 0);
    xw_other_stop(&o);
}
XT(shared_window_data_does_not_disturb_the_window) {
    NEED(NtUserGetSharedWindowData);
    HWND w = pw(0); LONG s0 = U(GetWindowLongW)(w, GWL_STYLE), e0 = U(GetWindowLongW)(w, GWL_EXSTYLE);
    for (int i = 0; i < 10; i++) UN(NtUserGetSharedWindowData)((ULONG_PTR)w, i, 0, 0);
    EXPECT_EQ(U(GetWindowLongW)(w, GWL_STYLE), s0); EXPECT_EQ(U(GetWindowLongW)(w, GWL_EXSTYLE), e0); CHECK(U(IsWindow)(w));
    xw_kill(w);
}
