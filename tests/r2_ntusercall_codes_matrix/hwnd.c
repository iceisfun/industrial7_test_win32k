/* Per-code semantics of the HWND dispatchers: NtUserCallHwnd (0x54..0x5b), HwndParam (0x5e..0x68),
 * HwndLock (0x69..0x75), HwndParamLock (0x76..0x7d), HwndParamLockSafe (0x7d). Each handler runs
 * with the validated window locked . */
#include "mx.h"

static BOOL set_help(HWND w, DWORD id) { return (BOOL)(B8(HWP(w, id, 0x65)) != 0); }
static DWORD get_help(HWND w) { return LO32(HW(w, 0x57)); }

/* ---- 0x57 GetWindowContextHelpId / 0x65 SetWindowContextHelpId ----------------------------------- */
T(hwnd_0x57_context_help_id_defaults_to_zero) {
    NEED_ALL_HWND();
    HWND w = k_make_window("r2ncm_help0");
    if (!w) { t_skip("no window"); return; }
    SetLastError(SENTINEL_ERR);
    CHECK(get_help(w) == 0);
    CHECK(GetLastError() == SENTINEL_ERR);
    CHECK(U(GetWindowContextHelpId)(w) == 0);
    U(DestroyWindow)(w);
}
T(hwnd_0x65_set_context_help_id_roundtrip) {
    NEED_ALL_HWND();
    HWND w = k_make_window("r2ncm_help1");
    if (!w) { t_skip("no window"); return; }
    static const DWORD ids[] = { 1, 0x1234, 0x7fffffff, 0xffffffffu, 0x80000000u, 0, 42 };
    for (size_t i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        SetLastError(SENTINEL_ERR);
        CHECKF(B8(HWP(w, ids[i], 0x65)) == 1, "set 0x%lx", (unsigned long)ids[i]);
        CHECKF(get_help(w) == ids[i], "readback 0x%lx got 0x%lx", (unsigned long)ids[i], (unsigned long)get_help(w));
        CHECKF(U(GetWindowContextHelpId)(w) == ids[i], "public readback");
        CHECK(GetLastError() == SENTINEL_ERR);
    }
    U(DestroyWindow)(w);
}
T(hwnd_0x65_public_set_is_visible_to_direct_get) {
    NEED_ALL_HWND();
    HWND w = k_make_window("r2ncm_help2");
    if (!w) { t_skip("no window"); return; }
    CHECK(U(SetWindowContextHelpId)(w, 0xabcd) != 0);
    CHECK(get_help(w) == 0xabcd);
    CHECK(set_help(w, 0xdcba));
    CHECK(U(GetWindowContextHelpId)(w) == 0xdcba);
    U(DestroyWindow)(w);
}
T(hwnd_0x65_help_ids_are_per_window) {
    NEED_ALL_HWND();
    HWND a = k_make_window("r2ncm_helpa"), b = k_make_window("r2ncm_helpb");
    if (!a || !b) { t_skip("no window"); if (a) U(DestroyWindow)(a); if (b) U(DestroyWindow)(b); return; }
    CHECK(set_help(a, 11)); CHECK(set_help(b, 22));
    CHECK(get_help(a) == 11); CHECK(get_help(b) == 22);
    CHECK(set_help(a, 0)); CHECK(get_help(b) == 22);
    U(DestroyWindow)(a); U(DestroyWindow)(b);
}
T(hwnd_0x65_help_id_dies_with_the_window) {
    NEED_ALL_HWND();
    HWND w = k_make_window("r2ncm_help3");
    if (!w) { t_skip("no window"); return; }
    CHECK(set_help(w, 99));
    U(DestroyWindow)(w);
    SetLastError(SENTINEL_ERR);
    CHECK(get_help(w) == 0);
    CHECK(GetLastError() == ERR_INVALID_WINDOW_HANDLE);
    SetLastError(SENTINEL_ERR);
    CHECK(B8(HWP(w, 5, 0x65)) == 0);
    CHECK(GetLastError() == ERR_INVALID_WINDOW_HANDLE);
}
T(hwnd_0x65_help_id_from_other_thread_window) {
    NEED_ALL_HWND();
    struct mx_bg bg;
    if (!mx_bg_start(&bg, 0x4141)) { t_skip("bg"); mx_bg_stop(&bg); return; }
    CHECK(get_help(bg.hwnd) == 0x4141);
    SetLastError(SENTINEL_ERR);
    DWORD r = B8(HWP(bg.hwnd, 0x4142, 0x65));
    t_note("setting the help id of another thread's window: ret %lu err %lu", (unsigned long)r, GetLastError());
    if (r) CHECK(get_help(bg.hwnd) == 0x4142);
    mx_bg_stop(&bg);
}
T(hwnd_0x57_help_id_of_other_process_window_is_readable) {
    NEED_ALL_HWND();
    HWND sh = U(GetShellWindow)();
    DWORD pid = 0;
    if (!sh || !U(GetWindowThreadProcessId)(sh, &pid) || pid == GetCurrentProcessId()) { t_skip("no foreign shell window"); return; }
    SetLastError(SENTINEL_ERR);
    DWORD d = get_help(sh), p = U(GetWindowContextHelpId)(sh);
    DWORD e = GetLastError();
    if (e == ERR_ACCESS_DENIED) { t_note("shell window refused (UIPI, error 5)"); return; }
    CHECKF(d == p, "direct 0x%lx public 0x%lx", (unsigned long)d, (unsigned long)p);
    CHECKF(e == SENTINEL_ERR || e == 0, "last error %lu after a cross-process read", e);
}

/* ---- 0x54 / 0x58 DeregisterShellHookWindow / RegisterShellHookWindow --------------------------------- */
T(hwnd_0x58_0x54_shell_hook_registration) {
    NEED_ALL_HWND();
    HWND w = k_make_window("r2ncm_shell");
    if (!w) { t_skip("no window"); return; }
    SetLastError(SENTINEL_ERR);
    DWORD notyet = B8(HW(w, 0x54));             /* not registered yet */
    t_note("Deregister before Register: %lu (err %lu)", (unsigned long)notyet, GetLastError());
    DWORD reg = B8(HW(w, 0x58));
    if (!reg) { t_note("RegisterShellHookWindow refused (err %lu)", GetLastError()); U(DestroyWindow)(w); return; }
    CHECK(reg == 1);
    /* _DeregisterShellHookWindow returns 1 whenever the window has a desktop-info link (VWPLRemoveBase only
     * decides whether the WF flag is cleared), so the result does not say whether a registration existed. */
    CHECK(B8(HW(w, 0x54)) == 1);
    SetLastError(SENTINEL_ERR);
    DWORD again = B8(HW(w, 0x54));
    t_note("second Deregister: %lu (err %lu)", (unsigned long)again, GetLastError());
    CHECK(U(RegisterShellHookWindow)(w) != 0);
    CHECK(U(DeregisterShellHookWindow)(w) != 0);
    U(DestroyWindow)(w);
}
T(hwnd_0x58_shell_hook_window_destroyed_while_registered) {
    NEED_ALL_HWND();
    HWND w = k_make_window("r2ncm_shell2");
    if (!w) { t_skip("no window"); return; }
    if (!HW(w, 0x58)) { t_skip("register refused"); U(DestroyWindow)(w); return; }
    U(DestroyWindow)(w);
    SetLastError(SENTINEL_ERR);
    CHECK(B8(HW(w, 0x54)) == 0);               /* dead handle: ValidateHwnd fails first */
    CHECK(GetLastError() == ERR_INVALID_WINDOW_HANDLE);
}

/* ---- 0x69 ArrangeIconicWindows ------------------------------------------------------------------------ */
T(hwnd_0x69_arrange_iconic_windows_without_children) {
    NEED_ALL_HWND();
    HWND w = k_make_window("r2ncm_arr");
    if (!w) { t_skip("no window"); return; }
    SetLastError(SENTINEL_ERR);
    CHECK(LO32(HWL(w, 0x69)) == 0);               /* nothing iconic to arrange */
    CHECK(U(ArrangeIconicWindows)(w) == 0);
    CHECK(GetLastError() == SENTINEL_ERR);
    U(DestroyWindow)(w);
}
T(hwnd_0x69_arrange_iconic_child_matches_public) {
    NEED_ALL_HWND();
    HWND p = mx_visible_window(NULL, 0);
    if (!p) { t_skip("no parent"); return; }
    HWND c = U(CreateWindowExA)(0, "win32ktHelperWnd", "ic", WS_CHILD | WS_MINIMIZE | WS_VISIBLE, 0, 0, 20, 20, p, NULL, GetModuleHandleA(NULL), NULL);
    if (!c) { t_skip("no child"); U(DestroyWindow)(p); return; }
    DWORD a = LO32(HWL(p, 0x69));
    DWORD b = U(ArrangeIconicWindows)(p);
    t_note("arrange: direct %lu public %lu", (unsigned long)a, (unsigned long)b);
    CHECKF(a == b, "direct %lu vs public %lu", (unsigned long)a, (unsigned long)b);
    CHECK(U(IsIconic)(c));
    U(DestroyWindow)(p);
}

/* ---- 0x6a DrawMenuBar ---------------------------------------------------------------------------------------- */
/* NtUserCallHwndLock: when the code is 0x6a (or 0x7d) and the window's FNID (as read from the shared window structure)
 * is 0x29d (desktop) or 0x29f (message box dialog class), validation succeeds but nothing is dispatched. */
T(hwnd_0x6a_draw_menu_bar_skips_desktop_window) {
    NEED_ALL_HWND();
    HWND d = U(GetDesktopWindow)();
    if (!d) { t_skip("no desktop window"); return; }
    void *wp = k_wnd_ptr(d);
    unsigned short fnid = 0;
    if (wp && k_try_read_u16((char *)wp + 0x2a, &fnid)) CHECKF((fnid & 0x2fff) == 0x29d, "desktop FNID 0x%x", fnid);
    else t_note("desktop window object not readable");
    SetLastError(SENTINEL_ERR);
    CHECK(HWL(d, 0x6a) == 0);
    CHECK(GetLastError() == SENTINEL_ERR);
    CHECK(U(DrawMenuBar)(d) == 0);
    CHECK(GetLastError() == SENTINEL_ERR);
}
T(hwnd_0x6a_draw_menu_bar_normal_window_matches_public) {
    NEED_ALL_HWND();
    HWND w = k_make_window("r2ncm_dmb");
    if (!w) { t_skip("no window"); return; }
    DWORD a = LO32(HWL(w, 0x6a)), b = U(DrawMenuBar)(w);
    t_note("DrawMenuBar on a menu-less popup: direct %lu public %lu", (unsigned long)a, (unsigned long)b);
    CHECKF(a == b, "direct %lu public %lu", (unsigned long)a, (unsigned long)b);
    HMENU m = U(CreateMenu)();
    if (m && U(AppendMenuA)(m, MF_STRING, 1, "x") && U(SetMenu)(w, m)) {
        a = LO32(HWL(w, 0x6a)); b = U(DrawMenuBar)(w);
        CHECKF(a == b, "with menu: direct %lu public %lu", (unsigned long)a, (unsigned long)b);
        U(SetMenu)(w, NULL);
    }
    if (m) U(DestroyMenu)(m);
    U(DestroyWindow)(w);
}

/* ---- 0x73 UpdateWindow --------------------------------------------------------------------------------------- */
T(hwnd_0x73_update_window_with_nothing_to_paint) {
    NEED_ALL_HWND();
    HWND w = mx_visible_window(NULL, 0);
    if (!w) { t_skip("no window"); return; }
    k_pump(30);
    CHECK(U(UpdateWindow)(w) != 0);
    DWORD d = LO32(HWL(w, 0x73));
    t_note("NtUserCallHwndLock(UpdateWindow) on a clean window returned %lu", (unsigned long)d);
    RECT r; CHECK(!U(GetUpdateRect)(w, &r, FALSE));
    U(DestroyWindow)(w);
}

/* ---- 0x79 ShowOwnedPopups -------------------------------------------------------------------------------------- */
/* ShowOwnedPopups hides visible owned popups (marking them) and shows the marked ones again */
T(hwnd_0x79_show_owned_popups_hides_and_restores_popup) {
    NEED_ALL_HWND();
    HWND owner = mx_visible_window(NULL, 0);
    if (!owner) { t_skip("no owner"); return; }
    HWND pop = mx_visible_window(owner, 0);
    if (!pop) { t_skip("no popup"); U(DestroyWindow)(owner); return; }
    CHECK(U(GetWindow)(pop, GW_OWNER) == owner);
    CHECK(U(IsWindowVisible)(pop));
    SetLastError(SENTINEL_ERR);
    DWORD r = B8(HWPL(owner, 0, 0x79));
    CHECKF(r != 0, "ShowOwnedPopups(FALSE) = %lu", (unsigned long)r);
    CHECK(!U(IsWindowVisible)(pop));
    r = B8(HWPL(owner, 1, 0x79));
    CHECKF(r != 0, "ShowOwnedPopups(TRUE) = %lu", (unsigned long)r);
    CHECK(U(IsWindowVisible)(pop));
    CHECK(GetLastError() == SENTINEL_ERR);
    /* the public wrapper emits the same code */
    CHECK(U(ShowOwnedPopups)(owner, FALSE) != 0);
    CHECK(!U(IsWindowVisible)(pop));
    CHECK(U(ShowOwnedPopups)(owner, TRUE) != 0);
    CHECK(U(IsWindowVisible)(pop));
    U(DestroyWindow)(pop); U(DestroyWindow)(owner);
}
T(hwnd_0x79_only_the_paramlock_dispatcher_handles_it) {
    NEED_ALL_HWND();
    HWND owner = mx_visible_window(NULL, 0);
    HWND pop = owner ? mx_visible_window(owner, 0) : NULL;
    if (!owner || !pop) { t_skip("fixture"); if (pop) U(DestroyWindow)(pop); if (owner) U(DestroyWindow)(owner); return; }
    /* NtUserCallHwndParamLockSafe dispatches only 0x7d, NtUserCallHwndLock only 0x69..0x75 */
    CHECK(HWPLS(owner, 0, 0x79) == 0);
    CHECK(HWL(owner, 0x79) == 0);
    CHECK(HWP(owner, 0, 0x79) == 0);
    CHECK(U(IsWindowVisible)(pop));
    U(DestroyWindow)(pop); U(DestroyWindow)(owner);
}
T(hwnd_0x79_window_without_owned_popups) {
    NEED_ALL_HWND();
    HWND w = k_make_window("r2ncm_sop");
    if (!w) { t_skip("no window"); return; }
    DWORD a = B8(HWPL(w, 1, 0x79)), b = U(ShowOwnedPopups)(w, TRUE);
    CHECKF(a == b, "direct %lu public %lu", (unsigned long)a, (unsigned long)b);
    CHECK(!U(IsWindowVisible)(w));                /* the window itself is not touched */
    U(DestroyWindow)(w);
}

/* ---- 0x7c ValidateRgn ---------------------------------------------------------------------------------------------- */
T(hwnd_0x7c_validate_rgn_null_clears_update_region) {
    NEED_ALL_HWND();
    HWND w = mx_visible_window(NULL, 0);
    if (!w) { t_skip("no window"); return; }
    k_pump(30);
    U(InvalidateRect)(w, NULL, FALSE);
    RECT r;
    if (!U(GetUpdateRect)(w, &r, FALSE)) { t_skip("window has no update region (off-screen)"); U(DestroyWindow)(w); return; }
    DWORD v = B8(HWPL(w, 0, 0x7c));
    t_note("ValidateRgn(NULL) returned %lu", (unsigned long)v);
    CHECK(!U(GetUpdateRect)(w, &r, FALSE));
    U(InvalidateRect)(w, NULL, FALSE);
    CHECK(U(ValidateRgn)(w, NULL) == (BOOL)v);       /* the public wrapper emits 0x7c */
    CHECK(!U(GetUpdateRect)(w, &r, FALSE));
    U(DestroyWindow)(w);
}
T(hwnd_0x7c_validate_rgn_partial_region) {
    NEED_ALL_HWND();
    HWND w = U(CreateWindowExA)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, "win32ktHelperWnd", "vr", WS_POPUP, -32000, -32000, 100, 100, NULL, NULL, GetModuleHandleA(NULL), NULL);
    if (!w) {
        HWND probe = k_make_window("r2ncm_cls"); if (probe) U(DestroyWindow)(probe);
        w = U(CreateWindowExA)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, "win32ktHelperWnd", "vr", WS_POPUP, -32000, -32000, 100, 100, NULL, NULL, GetModuleHandleA(NULL), NULL);
    }
    if (!w) { t_skip("no window"); return; }
    U(ShowWindow)(w, SW_SHOWNOACTIVATE);
    k_pump(30);
    RECT all = { 0, 0, 100, 100 };
    U(InvalidateRect)(w, &all, FALSE);
    RECT u;
    if (!U(GetUpdateRect)(w, &u, FALSE)) { t_skip("no update region"); U(DestroyWindow)(w); return; }
    HRGN part = U(CreateRectRgn)(0, 0, 100, 50);
    HWPL(w, (ULONG_PTR)part, 0x7c);
    CHECK(U(GetUpdateRect)(w, &u, FALSE));
    CHECKF(u.top >= 50, "remaining update rect (%ld,%ld,%ld,%ld) should start at y>=50", u.left, u.top, u.right, u.bottom);
    U(DeleteObject)(part);
    U(DestroyWindow)(w);
}
/* ---- 0x7d EnableWindow (NtUserCallHwndParamLockSafe) ------------------------------------------------------------------ */
T(hwnd_0x7d_enable_window_roundtrip) {
    NEED_ALL_HWND();
    HWND w = k_make_window("r2ncm_en");
    if (!w) { t_skip("no window"); return; }
    CHECK(U(IsWindowEnabled)(w));
    CHECK(B8(HWPLS(w, 0, 0x7d)) == 0);          /* previously enabled -> FALSE */
    CHECK(!U(IsWindowEnabled)(w));
    CHECK((U(GetWindowLongA)(w, GWL_STYLE) & WS_DISABLED) != 0);
    CHECK(B8(HWPLS(w, 0, 0x7d)) == 1);          /* previously disabled -> TRUE */
    CHECK(!U(IsWindowEnabled)(w));
    CHECK(B8(HWPLS(w, 1, 0x7d)) == 1);
    CHECK(U(IsWindowEnabled)(w));
    CHECK((U(GetWindowLongA)(w, GWL_STYLE) & WS_DISABLED) == 0);
    CHECK(B8(HWPLS(w, 1, 0x7d)) == 0);
    CHECK(U(IsWindowEnabled)(w));
    U(DestroyWindow)(w);
}
T(hwnd_0x7d_enable_window_public_parity) {
    NEED_ALL_HWND();
    HWND w = k_make_window("r2ncm_en2");
    if (!w) { t_skip("no window"); return; }
    CHECK(U(EnableWindow)(w, FALSE) == 0);
    CHECK(B8(HWPLS(w, 0, 0x7d)) == 1);          /* public disable is visible to the direct call */
    CHECK(B8(HWPLS(w, 1, 0x7d)) == 1);
    CHECK(U(EnableWindow)(w, TRUE) == 0);
    CHECK(U(IsWindowEnabled)(w));
    U(DestroyWindow)(w);
}
T(hwnd_0x7d_enable_window_nonzero_param_values) {
    NEED_ALL_HWND();
    HWND w = k_make_window("r2ncm_en3");
    if (!w) { t_skip("no window"); return; }
    HWPLS(w, 0, 0x7d);
    CHECK(!U(IsWindowEnabled)(w));
    HWPLS(w, 2, 0x7d);
    t_note("EnableWindow with param 2: enabled=%d", U(IsWindowEnabled)(w) ? 1 : 0);
    HWPLS(w, 1, 0x7d);
    CHECK(U(IsWindowEnabled)(w));
    U(DestroyWindow)(w);
}
T(hwnd_0x7d_enable_window_on_other_thread_window) {
    NEED_ALL_HWND();
    struct mx_bg bg;
    if (!mx_bg_start(&bg, 0)) { t_skip("bg"); mx_bg_stop(&bg); return; }
    SetLastError(SENTINEL_ERR);
    DWORD r = B8(HWPLS(bg.hwnd, 0, 0x7d));
    CHECK(r == 0);
    CHECK(!U(IsWindowEnabled)(bg.hwnd));
    CHECK(B8(HWPLS(bg.hwnd, 1, 0x7d)) == 1);
    CHECK(U(IsWindowEnabled)(bg.hwnd));
    mx_bg_stop(&bg);
}
T(hwnd_0x7d_enable_window_child_gets_wm_enable_independent_of_parent) {
    NEED_ALL_HWND();
    HWND p = k_make_window("r2ncm_enp");
    if (!p) { t_skip("no window"); return; }
    HWND c = U(CreateWindowExA)(0, "win32ktHelperWnd", "c", WS_CHILD, 0, 0, 4, 4, p, NULL, GetModuleHandleA(NULL), NULL);
    if (!c) { t_skip("no child"); U(DestroyWindow)(p); return; }
    HWPLS(p, 0, 0x7d);
    CHECK(!U(IsWindowEnabled)(p));
    CHECK(U(IsWindowEnabled)(c));                  /* WS_DISABLED is per window; IsWindowEnabled(child) ignores the parent */
    HWPLS(p, 1, 0x7d);
    U(DestroyWindow)(p);
}

/* ---- unreachable-by-design Hwnd codes ----------------------------------------------------------------------------------- */
/* 0x55/0x56 window worker / modern-app window queries, 0x59 MB_DlgProc setup, 0x5a/0x5b messaging IOCP
 * hooks, 0x5c/0x5d SetProgmanWindow/SetTaskmanWindow (shell singletons: NULL would unregister the running
 * shell's), 0x5e..0x68 class-data / window-state / overlay / callout registrations, 0x6b IME context,
 * 0x6c..0x75 menu-close, MDI, SetForegroundWindow (steals foreground), track info, 0x76..0x78 IME / modern app,
 * 0x7a SwitchToThisWindow (activates), and 0x7a/0x7b through NtUserCallHwndLock (which skips validation entirely)
 * change shell or focus state: they are only reached by the inert and invalid-handle matrices in ranges.c. */
