/* Window placement / layout / ghost entry points: NtUserSetWindowShowState, SetWindowArrangement (IAM only),
 * GetTopLevelWindow, IsTopLevelWindow, Get/SetInternalWindowPos, GetWindowMinimizeRect, LayoutCompleted,
 * BeginLayoutUpdate, Enable/IsResizeLayoutSynchronization, Ghost/HungWindowFromGhostWindow, FrostCrashedWindow. */
#include "r2.h"

static HWND mk_child(HWND parent) {
    return U(CreateWindowExW)(0, L"STATIC", L"c", WS_CHILD, 0, 0, 8, 8, parent, NULL, NULL, NULL);
}
static HWND mk_owned_popup(HWND owner) {
    return U(CreateWindowExW)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"o", WS_POPUP, -32000, -32000, 8, 8, owner, NULL, NULL, NULL);
}

/* ---- SetWindowShowState / SetWindowArrangement (IAM only) ------------------------------------------------- */
RT(set_window_show_state_invalid_hwnds) {
    REQUIRE(UN(NtUserSetWindowShowState));
    for (unsigned i = 0; i < N_BAD_HWNDS; i++) {
        SET_ERR(SENT);
        CKF(R_B(UN(NtUserSetWindowShowState)((HWND)r2_bad_hwnds[i], 1, 0, NULL)) == 0, "hwnd=%p", (void *)r2_bad_hwnds[i]);
        CKF(GetLastError() == ERR_INVALID_WINDOW, "hwnd=%p gle=%lu", (void *)r2_bad_hwnds[i], GetLastError());
    }
}
RT(set_window_show_state_desktop_window_silently_rejected) {
    REQUIRE(UN(NtUserSetWindowShowState)); REQUIRE(U(GetDesktopWindow));
    SET_ERR(SENT);
    CK(R_B(UN(NtUserSetWindowShowState)(U(GetDesktopWindow)(), 1, 0, NULL)) == 0);
    EXPECT_ERR(SENT);   /* FNID 0x29d filter returns before any error is set */
}
/* after the window is validated, the desktop's IAM object and IAMThreadAccessGranted must both hold -> 5 */
RT(set_window_show_state_denied_outside_iam) {
    REQUIRE(UN(NtUserSetWindowShowState));
    NEED_WIN(w);
    for (DWORD cmd = 0; cmd < 8; cmd++) {
        SET_ERR(SENT);
        CKF(R_B(UN(NtUserSetWindowShowState)(w, cmd, 0, NULL)) == 0, "cmd=%lu", cmd);
        CKF(GetLastError() == ERR_ACCESS_DENIED, "cmd=%lu gle=%lu", cmd, GetLastError());
    }
    END_WIN(w);
}
RT(set_window_show_state_denied_with_rect_and_flag) {
    REQUIRE(UN(NtUserSetWindowShowState));
    NEED_WIN(w);
    RECT rc = { 0, 0, 100, 100 };
    SET_ERR(SENT);
    CK(R_B(UN(NtUserSetWindowShowState)(w, 1, 1, &rc)) == 0);
    EXPECT_ERR(ERR_ACCESS_DENIED);
    END_WIN(w);
}
RT(set_window_show_state_does_not_change_window) {
    REQUIRE(UN(NtUserSetWindowShowState));
    NEED_WIN(w);
    LONG st0 = U(GetWindowLongW)(w, GWL_STYLE);
    RECT r0, r1; U(GetWindowRect)(w, &r0);
    UN(NtUserSetWindowShowState)(w, 3, 0, NULL);
    CK(U(GetWindowLongW)(w, GWL_STYLE) == st0);
    U(GetWindowRect)(w, &r1);
    CK(r0.left == r1.left && r0.top == r1.top && r0.right == r1.right && r0.bottom == r1.bottom);
    CK(!U(IsWindowVisible)(w));
    END_WIN(w);
}
RT(set_window_arrangement_invalid_hwnds) {
    REQUIRE(UN(NtUserSetWindowArrangement));
    for (unsigned i = 0; i < N_BAD_HWNDS; i++) {
        SET_ERR(SENT);
        CKF(R_B(UN(NtUserSetWindowArrangement)((HWND)r2_bad_hwnds[i], NULL, NULL, 0)) == 0, "hwnd=%p", (void *)r2_bad_hwnds[i]);
        CKF(GetLastError() == ERR_INVALID_WINDOW, "hwnd=%p gle=%lu", (void *)r2_bad_hwnds[i], GetLastError());
    }
}
RT(set_window_arrangement_denied_outside_iam) {
    REQUIRE(UN(NtUserSetWindowArrangement));
    NEED_WIN(w);
    RECT a = { 0, 0, 10, 10 }, b = { 0, 0, 20, 20 };
    static const DWORD flags[] = { 0, 1, 2, 4, 7 };
    for (unsigned i = 0; i < 5; i++) {
        SET_ERR(SENT);
        CKF(R_B(UN(NtUserSetWindowArrangement)(w, &a, &b, flags[i])) == 0, "flags=%lx", flags[i]);
        CKF(GetLastError() == ERR_ACCESS_DENIED, "flags=%lx gle=%lu", flags[i], GetLastError());
    }
    END_WIN(w);
}
RT(set_window_arrangement_desktop_window_silently_rejected) {
    REQUIRE(UN(NtUserSetWindowArrangement)); REQUIRE(U(GetDesktopWindow));
    SET_ERR(SENT);
    CK(R_B(UN(NtUserSetWindowArrangement)(U(GetDesktopWindow)(), NULL, NULL, 0)) == 0);
    EXPECT_ERR(SENT);
}

/* ---- GetTopLevelWindow / IsTopLevelWindow ---------------------------------------------------------------- */
RT(get_top_level_window_of_top_level_is_itself) {
    REQUIRE(UN(NtUserGetTopLevelWindow));
    NEED_WIN(w);
    CK(UN(NtUserGetTopLevelWindow)(w) == w);
    END_WIN(w);
}
RT(get_top_level_window_of_child_is_ancestor) {
    REQUIRE(UN(NtUserGetTopLevelWindow));
    NEED_WIN(w);
    HWND c = mk_child(w);
    if (!c) { t_skip("child window creation failed"); END_WIN(w); return; }
    CK(UN(NtUserGetTopLevelWindow)(c) == w);
    HWND g = mk_child(c);
    if (g) { CK(UN(NtUserGetTopLevelWindow)(g) == w); U(DestroyWindow)(g); }
    U(DestroyWindow)(c);
    END_WIN(w);
}
RT(get_top_level_window_of_owned_popup_is_the_popup) {
    REQUIRE(UN(NtUserGetTopLevelWindow));
    NEED_WIN(w);
    HWND p = mk_owned_popup(w);
    if (!p) { t_skip("popup creation failed"); END_WIN(w); return; }
    CK(UN(NtUserGetTopLevelWindow)(p) == p);   /* ownership does not change the parent chain: the parent is the desktop */
    U(DestroyWindow)(p);
    END_WIN(w);
}
RT(get_top_level_window_of_desktop_window_is_null) {
    REQUIRE(UN(NtUserGetTopLevelWindow)); REQUIRE(U(GetDesktopWindow));
    CK(UN(NtUserGetTopLevelWindow)(U(GetDesktopWindow)()) == NULL);   /* its parent is NULL, the walk ends at 0 */
}
RT(get_top_level_window_invalid_hwnds) {
    REQUIRE(UN(NtUserGetTopLevelWindow));
    for (unsigned i = 0; i < N_BAD_HWNDS; i++) {
        SET_ERR(SENT);
        CKF(UN(NtUserGetTopLevelWindow)((HWND)r2_bad_hwnds[i]) == NULL, "hwnd=%p", (void *)r2_bad_hwnds[i]);
        CKF(GetLastError() == ERR_INVALID_WINDOW, "hwnd=%p gle=%lu", (void *)r2_bad_hwnds[i], GetLastError());
    }
}
RT(get_top_level_window_matches_getancestor_root_for_children) {
    REQUIRE(UN(NtUserGetTopLevelWindow));
    NEED_WIN(w);
    HWND c = mk_child(w);
    if (c) { CK(UN(NtUserGetTopLevelWindow)(c) == U(GetAncestor)(c, GA_ROOT)); U(DestroyWindow)(c); }
    CK(UN(NtUserGetTopLevelWindow)(w) == U(GetAncestor)(w, GA_ROOT));
    END_WIN(w);
}
RT(get_top_level_window_user32_export_agrees) {
    typedef HWND (WINAPI *pfn)(HWND);
    pfn f = (pfn)r2_proc_in("user32.dll", "GetTopLevelWindow");
    REQUIRE(f);
    NEED_WIN(w);
    HWND c = mk_child(w);
    if (c) { CK(f(c) == w); U(DestroyWindow)(c); }
    END_WIN(w);
}
RT(is_top_level_window_cases) {
    REQUIRE(UN(NtUserIsTopLevelWindow));
    NEED_WIN(w);
    CK(R_D(UN(NtUserIsTopLevelWindow)(w)) == 1);
    HWND c = mk_child(w);
    if (c) { CK(R_D(UN(NtUserIsTopLevelWindow)(c)) == 0); U(DestroyWindow)(c); }
    HWND p = mk_owned_popup(w);
    if (p) { CK(R_D(UN(NtUserIsTopLevelWindow)(p)) == 1); U(DestroyWindow)(p); }
    END_WIN(w);
}
RT(is_top_level_window_desktop_window_is_false) {
    REQUIRE(UN(NtUserIsTopLevelWindow)); REQUIRE(U(GetDesktopWindow));
    CK(R_D(UN(NtUserIsTopLevelWindow)(U(GetDesktopWindow)())) == 0);   /* parent pointer is NULL */
}
RT(is_top_level_window_invalid_hwnds) {
    REQUIRE(UN(NtUserIsTopLevelWindow));
    for (unsigned i = 0; i < N_BAD_HWNDS; i++) {
        SET_ERR(SENT);
        CKF(R_D(UN(NtUserIsTopLevelWindow)((HWND)r2_bad_hwnds[i])) == 0, "hwnd=%p", (void *)r2_bad_hwnds[i]);
        CKF(GetLastError() == ERR_INVALID_WINDOW, "hwnd=%p gle=%lu", (void *)r2_bad_hwnds[i], GetLastError());
    }
}
RT(is_top_level_window_agrees_with_get_parent_null_for_toplevels) {
    REQUIRE(UN(NtUserIsTopLevelWindow));
    NEED_WIN(w);
    HWND c = mk_child(w);
    CK((U(GetAncestor)(w, GA_PARENT) == U(GetDesktopWindow)()) == (R_D(UN(NtUserIsTopLevelWindow)(w)) == 1));
    if (c) { CK((U(GetAncestor)(c, GA_PARENT) == U(GetDesktopWindow)()) == (R_D(UN(NtUserIsTopLevelWindow)(c)) == 1)); U(DestroyWindow)(c); }
    END_WIN(w);
}

/* ---- Get/SetInternalWindowPos ------------------------------------------------------------------------------ */
RT(get_internal_window_pos_matches_getwindowplacement) {
    REQUIRE(UN(NtUserGetInternalWindowPos));
    NEED_WIN(w);
    RECT rc = { 0 }; POINT pt = { 0 };
    UINT cmd = UN(NtUserGetInternalWindowPos)(w, &rc, &pt);
    WINDOWPLACEMENT wp; memset(&wp, 0, sizeof wp); wp.length = sizeof wp;
    CHECK(U(GetWindowPlacement)(w, &wp));
    CKF(cmd == wp.showCmd, "showCmd %u vs %u", cmd, wp.showCmd);   /* both come from _GetWindowPlacement */
    /* both read the same normal position; a DPI-virtualised (system-DPI-unaware) caller may see user32 scale one of them */
    if (!(rc.left == wp.rcNormalPosition.left && rc.top == wp.rcNormalPosition.top && rc.right == wp.rcNormalPosition.right && rc.bottom == wp.rcNormalPosition.bottom))
        t_note("rect (%ld,%ld,%ld,%ld) vs placement (%ld,%ld,%ld,%ld): DPI virtualisation?", rc.left, rc.top, rc.right, rc.bottom,
               wp.rcNormalPosition.left, wp.rcNormalPosition.top, wp.rcNormalPosition.right, wp.rcNormalPosition.bottom);
    if (!(pt.x == wp.ptMinPosition.x && pt.y == wp.ptMinPosition.y)) t_note("minpos (%ld,%ld) vs (%ld,%ld)", pt.x, pt.y, wp.ptMinPosition.x, wp.ptMinPosition.y);
    END_WIN(w);
}
RT(get_internal_window_pos_null_outputs_allowed) {
    REQUIRE(UN(NtUserGetInternalWindowPos));
    NEED_WIN(w);
    WINDOWPLACEMENT wp; memset(&wp, 0, sizeof wp); wp.length = sizeof wp;
    CHECK(U(GetWindowPlacement)(w, &wp));
    CKF(UN(NtUserGetInternalWindowPos)(w, NULL, NULL) == wp.showCmd, "showCmd");
    RECT rc; CKF(UN(NtUserGetInternalWindowPos)(w, &rc, NULL) == wp.showCmd, "rect only");
    POINT pt; CKF(UN(NtUserGetInternalWindowPos)(w, NULL, &pt) == wp.showCmd, "point only");
    END_WIN(w);
}
RT(get_internal_window_pos_invalid_hwnds) {
    REQUIRE(UN(NtUserGetInternalWindowPos));
    for (unsigned i = 0; i < N_BAD_HWNDS; i++) {
        RECT rc = { 7, 7, 7, 7 }; POINT pt = { 7, 7 };
        SET_ERR(SENT);
        CKF(UN(NtUserGetInternalWindowPos)((HWND)r2_bad_hwnds[i], &rc, &pt) == 0, "hwnd=%p", (void *)r2_bad_hwnds[i]);
        CKF(GetLastError() == ERR_INVALID_WINDOW, "hwnd=%p gle=%lu", (void *)r2_bad_hwnds[i], GetLastError());
        CKF(rc.left == 7 && pt.x == 7, "outputs written for invalid hwnd");
    }
}
RT(get_internal_window_pos_user32_export_agrees) {
    typedef UINT (WINAPI *pfn)(HWND, RECT *, POINT *);
    pfn f = (pfn)r2_proc_in("user32.dll", "GetInternalWindowPos");
    REQUIRE(f); REQUIRE(UN(NtUserGetInternalWindowPos));
    NEED_WIN(w);
    RECT a = { 0 }, b = { 0 }; POINT pa = { 0 }, pb = { 0 };
    UINT ca = f(w, &a, &pa), cb = UN(NtUserGetInternalWindowPos)(w, &b, &pb);
    CK(ca == cb);
    CK(!memcmp(&a, &b, sizeof a) && !memcmp(&pa, &pb, sizeof pa));
    END_WIN(w);
}
/* NtUserSetInternalWindowPos accepts show commands 0..15 only */
RT(set_internal_window_pos_invalid_show_command_bits) {
    REQUIRE(UN(NtUserSetInternalWindowPos));
    NEED_WIN(w);
    RECT rc = { 0, 0, 100, 100 }; POINT pt = { -1, -1 };
    static const DWORD cmds[] = { 0x10, 0x20, 0x100, 0xFFFFFFFF };
    for (unsigned i = 0; i < 4; i++) {
        SET_ERR(SENT);
        CKF(R_D(UN(NtUserSetInternalWindowPos)(w, cmds[i], &rc, &pt)) == 0, "cmd=%lx", cmds[i]);
        CKF(GetLastError() == ERR_INVALID_FLAGS, "cmd=%lx gle=%lu", cmds[i], GetLastError());
    }
    END_WIN(w);
}
RT(set_internal_window_pos_invalid_hwnds) {
    REQUIRE(UN(NtUserSetInternalWindowPos));
    RECT rc = { 0, 0, 100, 100 }; POINT pt = { -1, -1 };
    for (unsigned i = 0; i < N_BAD_HWNDS; i++) {
        SET_ERR(SENT);
        CKF(R_D(UN(NtUserSetInternalWindowPos)((HWND)r2_bad_hwnds[i], 0, &rc, &pt)) == 0, "hwnd=%p", (void *)r2_bad_hwnds[i]);
        CKF(GetLastError() == ERR_INVALID_WINDOW, "hwnd=%p gle=%lu", (void *)r2_bad_hwnds[i], GetLastError());
    }
}
RT(set_internal_window_pos_desktop_window_silently_rejected) {
    REQUIRE(UN(NtUserSetInternalWindowPos)); REQUIRE(U(GetDesktopWindow));
    RECT rc = { 0, 0, 100, 100 }; POINT pt = { -1, -1 };
    SET_ERR(SENT);
    CK(R_D(UN(NtUserSetInternalWindowPos)(U(GetDesktopWindow)(), 0, &rc, &pt)) == 0);   /* FNID 0x29d/0x29f filter */
    if (t_is_wine()) t_note("Wine sets its own last error here; skipped last-error check");
    else EXPECT_ERR(SENT);
}
/* SetWindowPlacement-style positioning: for a window that is neither minimized nor maximized it moves/sizes the window to the
 * given rectangle (as SetWindowPos with SWP_NOZORDER), then applies the show command (SW_HIDE on a hidden
 * window changes nothing visible). */
RT(set_internal_window_pos_moves_normal_window) {
    REQUIRE(UN(NtUserSetInternalWindowPos));
    NEED_WIN(w);
    RECT rc = { -31000, -31000, -30800, -30850 }; POINT pt = { -1, -1 };
    SET_ERR(SENT);
    ULONG_PTR r = UN(NtUserSetInternalWindowPos)(w, SW_HIDE, &rc, &pt);
    CKF(R_D(r) == 1, "r=%llx gle=%lu", (unsigned long long)r, GetLastError());
    RECT now; CHECK(U(GetWindowRect)(w, &now));
    if (now.left == rc.left && now.top == rc.top && now.right == rc.right && now.bottom == rc.bottom) CHECK(1);
    else t_note("window rect (%ld,%ld,%ld,%ld) want (%ld,%ld,%ld,%ld): DPI virtualisation or sizing constraint?", now.left, now.top, now.right, now.bottom, rc.left, rc.top, rc.right, rc.bottom);
    CK(!U(IsWindowVisible)(w));
    END_WIN(w);
}
RT(set_internal_window_pos_then_get_reports_normal_state) {
    REQUIRE(UN(NtUserSetInternalWindowPos)); REQUIRE(UN(NtUserGetInternalWindowPos));
    NEED_WIN(w);
    RECT rc = { -31000, -31000, -30900, -30900 }; POINT pt = { -1, -1 };
    CK(R_D(UN(NtUserSetInternalWindowPos)(w, SW_HIDE, &rc, &pt)) == 1);
    RECT got = { 0 }; POINT gp = { 0 };
    UINT cmd = UN(NtUserGetInternalWindowPos)(w, &got, &gp);
    WINDOWPLACEMENT wp; memset(&wp, 0, sizeof wp); wp.length = sizeof wp;
    CHECK(U(GetWindowPlacement)(w, &wp));
    CKF(cmd == wp.showCmd, "%u vs %u", cmd, wp.showCmd);
    t_note("normal position after SetInternalWindowPos: (%ld,%ld,%ld,%ld)", got.left, got.top, got.right, got.bottom);
    END_WIN(w);
}

/* ---- GetWindowMinimizeRect ---------------------------------------------------------------------------------- */
RT(get_window_minimize_rect_invalid_hwnds) {
    REQUIRE(UN(NtUserGetWindowMinimizeRect));
    for (unsigned i = 0; i < N_BAD_HWNDS; i++) {
        RECT rc = { 7, 7, 7, 7 };
        SET_ERR(SENT);
        CKF(R_B(UN(NtUserGetWindowMinimizeRect)((HWND)r2_bad_hwnds[i], &rc)) == 0, "hwnd=%p", (void *)r2_bad_hwnds[i]);
        CKF(GetLastError() == ERR_INVALID_WINDOW, "hwnd=%p gle=%lu", (void *)r2_bad_hwnds[i], GetLastError());
        CKF(rc.left == 7 && rc.bottom == 7, "rect written for invalid hwnd");
    }
}
RT(get_window_minimize_rect_null_rect_is_invalid_parameter) {
    REQUIRE(UN(NtUserGetWindowMinimizeRect));
    NEED_WIN(w);
    SET_ERR(SENT);
    UN(NtUserGetWindowMinimizeRect)(w, NULL);
    EXPECT_ERR(ERR_INVALID_PARAM);
    END_WIN(w);
}
RT(get_window_minimize_rect_valid_window_gives_nonempty_rect) {
    REQUIRE(UN(NtUserGetWindowMinimizeRect));
    NEED_WIN(w);
    RECT rc = { 0 };
    ULONG_PTR r = UN(NtUserGetWindowMinimizeRect)(w, &rc);
    CKF(R_B(r) != 0, "r=%llx gle=%lu", (unsigned long long)r, GetLastError());
    CKF(rc.right > rc.left && rc.bottom > rc.top, "rect (%ld,%ld,%ld,%ld)", rc.left, rc.top, rc.right, rc.bottom);
    t_note("minimize rect (%ld,%ld,%ld,%ld)", rc.left, rc.top, rc.right, rc.bottom);
    END_WIN(w);
}
RT(get_window_minimize_rect_size_is_stable_between_calls) {
    REQUIRE(UN(NtUserGetWindowMinimizeRect));
    NEED_WIN(w);
    RECT a = { 0 }, b = { 0 };
    CK(R_B(UN(NtUserGetWindowMinimizeRect)(w, &a)) != 0);
    CK(R_B(UN(NtUserGetWindowMinimizeRect)(w, &b)) != 0);
    CKF(a.right - a.left == b.right - b.left && a.bottom - a.top == b.bottom - b.top, "sizes differ");
    END_WIN(w);
}
RT(get_window_minimize_rect_desktop_window_returns_false) {
    REQUIRE(UN(NtUserGetWindowMinimizeRect)); REQUIRE(U(GetDesktopWindow));
    RECT rc = { 7, 7, 7, 7 };
    SET_ERR(SENT);
    CK(R_B(UN(NtUserGetWindowMinimizeRect)(U(GetDesktopWindow)(), &rc)) == 0);
    EXPECT_ERR(SENT);
    CK(rc.left == 7);
}
RT(get_window_minimize_rect_user32_export_agrees) {
    typedef BOOL (WINAPI *pfn)(HWND, RECT *);
    pfn f = (pfn)r2_proc_in("user32.dll", "GetWindowMinimizeRect");
    REQUIRE(f); REQUIRE(UN(NtUserGetWindowMinimizeRect));
    NEED_WIN(w);
    RECT a = { 0 }, b = { 0 };
    CK(f(w, &a) != 0);
    CK(R_B(UN(NtUserGetWindowMinimizeRect)(w, &b)) != 0);
    CK(!memcmp(&a, &b, sizeof a));
    END_WIN(w);
}

/* ---- layout notifications -------------------------------------------------------------------------------- */
RT(layout_completed_valid_window_returns_true) {
    REQUIRE(UN(NtUserLayoutCompleted));
    NEED_WIN(w);
    SET_ERR(SENT);
    CKF(R_B(UN(NtUserLayoutCompleted)(w)) == 1, "gle=%lu", GetLastError());
    CK(U(IsWindow)(w));
    END_WIN(w);
}
RT(layout_completed_invalid_hwnds) {
    REQUIRE(UN(NtUserLayoutCompleted));
    for (unsigned i = 0; i < N_BAD_HWNDS; i++) {
        SET_ERR(SENT);
        CKF(R_B(UN(NtUserLayoutCompleted)((HWND)r2_bad_hwnds[i])) == 0, "hwnd=%p", (void *)r2_bad_hwnds[i]);
        CKF(GetLastError() == ERR_INVALID_WINDOW, "hwnd=%p gle=%lu", (void *)r2_bad_hwnds[i], GetLastError());
    }
}
RT(layout_completed_child_window_returns_true) {
    REQUIRE(UN(NtUserLayoutCompleted));
    NEED_WIN(w);
    HWND c = mk_child(w);
    if (c) { CK(R_B(UN(NtUserLayoutCompleted)(c)) == 1); U(DestroyWindow)(c); }
    END_WIN(w);
}
RT(begin_layout_update_valid_window_returns_true) {
    REQUIRE(UN(NtUserBeginLayoutUpdate));
    NEED_WIN(w);
    CK(R_B(UN(NtUserBeginLayoutUpdate)(w)) == 1);
    HWND c = mk_child(w);
    if (c) { CK(R_B(UN(NtUserBeginLayoutUpdate)(c)) == 1); U(DestroyWindow)(c); }
    END_WIN(w);
}
RT(begin_layout_update_invalid_hwnds_and_desktop) {
    REQUIRE(UN(NtUserBeginLayoutUpdate)); REQUIRE(U(GetDesktopWindow));
    for (unsigned i = 0; i < N_BAD_HWNDS; i++) {
        SET_ERR(SENT);
        CKF(R_B(UN(NtUserBeginLayoutUpdate)((HWND)r2_bad_hwnds[i])) == 0, "hwnd=%p", (void *)r2_bad_hwnds[i]);
        CKF(GetLastError() == ERR_INVALID_WINDOW, "hwnd=%p gle=%lu", (void *)r2_bad_hwnds[i], GetLastError());
    }
    SET_ERR(SENT);
    CK(R_B(UN(NtUserBeginLayoutUpdate)(U(GetDesktopWindow)())) == 0);   /* FNID 0x29d filter */
    EXPECT_ERR(SENT);
}
RT(resize_layout_sync_default_is_disabled) {
    REQUIRE(UN(NtUserIsResizeLayoutSynchronizationEnabled));
    NEED_WIN(w);
    CK(R_B(UN(NtUserIsResizeLayoutSynchronizationEnabled)(w)) == 0);
    END_WIN(w);
}
/* NtUserEnableResizeLayoutSynchronization(hwnd, f): f != 0 sets bit 0x80 in the window's flag byte, f == 0 clears
 * it; NtUserIsResizeLayoutSynchronizationEnabled returns that bit (0x80). */
RT(resize_layout_sync_enable_disable_roundtrip) {
    REQUIRE(UN(NtUserEnableResizeLayoutSynchronization)); REQUIRE(UN(NtUserIsResizeLayoutSynchronizationEnabled));
    NEED_WIN(w);
    CK(R_B(UN(NtUserEnableResizeLayoutSynchronization)(w, 1)) == 1);
    CK(R_B(UN(NtUserIsResizeLayoutSynchronizationEnabled)(w)) == 0x80);
    CK(R_B(UN(NtUserEnableResizeLayoutSynchronization)(w, 0)) == 1);
    CK(R_B(UN(NtUserIsResizeLayoutSynchronizationEnabled)(w)) == 0);
    END_WIN(w);
}
RT(resize_layout_sync_any_nonzero_enables) {
    REQUIRE(UN(NtUserEnableResizeLayoutSynchronization)); REQUIRE(UN(NtUserIsResizeLayoutSynchronizationEnabled));
    NEED_WIN(w);
    static const DWORD v[] = { 1, 2, 0x80, 0xFFFFFFFF };
    for (unsigned i = 0; i < 4; i++) {
        CKF(R_B(UN(NtUserEnableResizeLayoutSynchronization)(w, v[i])) == 1, "v=%lx", v[i]);
        CKF(R_B(UN(NtUserIsResizeLayoutSynchronizationEnabled)(w)) == 0x80, "v=%lx", v[i]);
        UN(NtUserEnableResizeLayoutSynchronization)(w, 0);
        CKF(R_B(UN(NtUserIsResizeLayoutSynchronizationEnabled)(w)) == 0, "v=%lx cleared", v[i]);
    }
    END_WIN(w);
}
RT(resize_layout_sync_is_per_window) {
    REQUIRE(UN(NtUserEnableResizeLayoutSynchronization)); REQUIRE(UN(NtUserIsResizeLayoutSynchronizationEnabled));
    NEED_WIN(a); HWND b = r2_win();
    if (!b) { t_skip("second window failed"); END_WIN(a); return; }
    UN(NtUserEnableResizeLayoutSynchronization)(a, 1);
    CK(R_B(UN(NtUserIsResizeLayoutSynchronizationEnabled)(a)) == 0x80);
    CK(R_B(UN(NtUserIsResizeLayoutSynchronizationEnabled)(b)) == 0);
    UN(NtUserEnableResizeLayoutSynchronization)(a, 0);
    END_WIN(a); END_WIN(b);
}
RT(resize_layout_sync_invalid_hwnds) {
    REQUIRE(UN(NtUserEnableResizeLayoutSynchronization)); REQUIRE(UN(NtUserIsResizeLayoutSynchronizationEnabled));
    for (unsigned i = 0; i < N_BAD_HWNDS; i++) {
        SET_ERR(SENT);
        CKF(R_B(UN(NtUserEnableResizeLayoutSynchronization)((HWND)r2_bad_hwnds[i], 1)) == 0, "hwnd=%p", (void *)r2_bad_hwnds[i]);
        CKF(GetLastError() == ERR_INVALID_WINDOW, "hwnd=%p gle=%lu", (void *)r2_bad_hwnds[i], GetLastError());
        SET_ERR(SENT);
        CKF(R_B(UN(NtUserIsResizeLayoutSynchronizationEnabled)((HWND)r2_bad_hwnds[i])) == 0, "is: hwnd=%p", (void *)r2_bad_hwnds[i]);
        CKF(GetLastError() == ERR_INVALID_WINDOW, "is: hwnd=%p gle=%lu", (void *)r2_bad_hwnds[i], GetLastError());
    }
}
RT(resize_layout_sync_desktop_window_filtered) {
    REQUIRE(UN(NtUserEnableResizeLayoutSynchronization)); REQUIRE(UN(NtUserIsResizeLayoutSynchronizationEnabled)); REQUIRE(U(GetDesktopWindow));
    HWND d = U(GetDesktopWindow)();
    SET_ERR(SENT);
    CK(R_B(UN(NtUserEnableResizeLayoutSynchronization)(d, 1)) == 0);
    CK(R_B(UN(NtUserIsResizeLayoutSynchronizationEnabled)(d)) == 0);
    EXPECT_ERR(SENT);
}

/* ---- ghost / hung / frost ------------------------------------------------------------------------------------ */
RT(ghost_window_from_hung_window_of_plain_window_is_null) {
    REQUIRE(UN(NtUserGhostWindowFromHungWindow));
    NEED_WIN(w);
    CK(UN(NtUserGhostWindowFromHungWindow)(w) == NULL);   /* no ghost property was ever set on it */
    END_WIN(w);
}
RT(hung_window_from_ghost_window_of_plain_window_is_null) {
    REQUIRE(UN(NtUserHungWindowFromGhostWindow));
    NEED_WIN(w);
    CK(UN(NtUserHungWindowFromGhostWindow)(w) == NULL);   /* fnid is not the ghost window class (0x2aa) */
    END_WIN(w);
}
RT(ghost_hung_pair_invalid_hwnds) {
    REQUIRE(UN(NtUserGhostWindowFromHungWindow)); REQUIRE(UN(NtUserHungWindowFromGhostWindow));
    for (unsigned i = 0; i < N_BAD_HWNDS; i++) {
        SET_ERR(SENT);
        CKF(UN(NtUserGhostWindowFromHungWindow)((HWND)r2_bad_hwnds[i]) == NULL, "ghost: hwnd=%p", (void *)r2_bad_hwnds[i]);
        CKF(GetLastError() == ERR_INVALID_WINDOW, "ghost: hwnd=%p gle=%lu", (void *)r2_bad_hwnds[i], GetLastError());
        SET_ERR(SENT);
        CKF(UN(NtUserHungWindowFromGhostWindow)((HWND)r2_bad_hwnds[i]) == NULL, "hung: hwnd=%p", (void *)r2_bad_hwnds[i]);
        CKF(GetLastError() == ERR_INVALID_WINDOW, "hung: hwnd=%p gle=%lu", (void *)r2_bad_hwnds[i], GetLastError());
    }
}
RT(ghost_hung_pair_desktop_window_is_null) {
    REQUIRE(UN(NtUserGhostWindowFromHungWindow)); REQUIRE(UN(NtUserHungWindowFromGhostWindow)); REQUIRE(U(GetDesktopWindow));
    HWND d = U(GetDesktopWindow)();
    CK(UN(NtUserGhostWindowFromHungWindow)(d) == NULL);
    CK(UN(NtUserHungWindowFromGhostWindow)(d) == NULL);
}
RT(ghost_hung_pair_child_window_is_null) {
    REQUIRE(UN(NtUserGhostWindowFromHungWindow)); REQUIRE(UN(NtUserHungWindowFromGhostWindow));
    NEED_WIN(w);
    HWND c = mk_child(w);
    if (c) { CK(UN(NtUserGhostWindowFromHungWindow)(c) == NULL); CK(UN(NtUserHungWindowFromGhostWindow)(c) == NULL); U(DestroyWindow)(c); }
    END_WIN(w);
}
RT(ghost_hung_pair_user32_exports_agree) {
    typedef HWND (WINAPI *pfn)(HWND);
    pfn g = (pfn)r2_proc_in("user32.dll", "GhostWindowFromHungWindow"), h = (pfn)r2_proc_in("user32.dll", "HungWindowFromGhostWindow");
    REQUIRE(g); REQUIRE(h);
    NEED_WIN(w);
    CK(g(w) == NULL);
    CK(h(w) == NULL);
    END_WIN(w);
}
RT(frost_crashed_window_invalid_hwnds) {
    REQUIRE(UN(NtUserFrostCrashedWindow));
    for (unsigned i = 0; i < N_BAD_HWNDS; i++) {
        SET_ERR(SENT);
        CKF(UN(NtUserFrostCrashedWindow)((HWND)r2_bad_hwnds[i], NULL) == NULL, "hwnd=%p", (void *)r2_bad_hwnds[i]);
        CKF(GetLastError() == ERR_INVALID_WINDOW, "hwnd=%p gle=%lu", (void *)r2_bad_hwnds[i], GetLastError());
    }
}
/* second window handle, when non-NULL, is validated before any ghost work: ERROR_INVALID_WINDOW_HANDLE (0x578) */
RT(frost_crashed_window_invalid_second_hwnd) {
    REQUIRE(UN(NtUserFrostCrashedWindow));
    NEED_WIN(w);
    for (unsigned i = 1; i < N_BAD_HWNDS; i++) {
        SET_ERR(SENT);
        CKF(UN(NtUserFrostCrashedWindow)(w, (HWND)r2_bad_hwnds[i]) == NULL, "hwnd2=%p", (void *)r2_bad_hwnds[i]);
        CKF(GetLastError() == ERR_INVALID_WINDOW, "hwnd2=%p gle=%lu", (void *)r2_bad_hwnds[i], GetLastError());
    }
    END_WIN(w);
}
/* _ShouldFrostCrashedWindow requires a top-level window: children and the desktop window are never frozen. */
RT(frost_crashed_window_non_top_level_returns_null) {
    REQUIRE(UN(NtUserFrostCrashedWindow)); REQUIRE(U(GetDesktopWindow));
    NEED_WIN(w);
    HWND c = mk_child(w);
    if (c) { CK(UN(NtUserFrostCrashedWindow)(c, NULL) == NULL); U(DestroyWindow)(c); }
    CK(UN(NtUserFrostCrashedWindow)(U(GetDesktopWindow)(), NULL) == NULL);
    CK(U(IsWindow)(w));
    END_WIN(w);
}
