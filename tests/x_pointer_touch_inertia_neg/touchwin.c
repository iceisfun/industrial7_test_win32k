/* x_pointer_touch_inertia_neg / touchwin: touch capability of a window and the touch validation status.
 *
 * NtUserModifyWindowTouchCapability(hwnd, flags, add): the window handle is validated first (ERROR_INVALID_WINDOW_HANDLE,
 * 1400), then flags other than TWF_FINETOUCH (1) and TWF_WANTPALM (2) are ERROR_INVALID_FLAGS (1004), then the window must
 * belong to the calling thread (ERROR_ACCESS_DENIED), then add != 0 registers the window with those flags (flags 0 is a
 * valid registration) and add == 0 removes the registration whatever flags are given. Success returns 1.
 * NtUserIsTouchWindow(hwnd, &flags): FALSE for an invalid handle (ERROR_INVALID_WINDOW_HANDLE) and for a window that is not
 * registered; no thread restriction; the flags are written only on success; the output pointer is optional.
 * The registration belongs to the window: other windows are unaffected, and destroying the window ends it.
 *
 * NtUserGetTouchValidationStatus(handle): for a handle that is not a touch-input handle the answer is 0 and the thread's
 * last error is cleared to 0 even if it was set before; NULL asks about the current input, and with no touch digitizer in
 * the system the answer is 3.
 *
 * Determinism: only symbolic results are printed.
 */
#include "x.h"

#define TWF_FINETOUCH_ 1
#define TWF_WANTPALM_ 2

static HWND mk(void) { return k_make_window("xp_touch"); }
static void rm(HWND h) { if (h) U(DestroyWindow)(h); }

/* ---- own window: register / read back / remove ---- */
static void roundtrip(ULONG flags) {
    NEED(NtUserModifyWindowTouchCapability);
    HWND h = mk(); if (!h) { t_skip("no window"); return; }
    EXPECT_EQ(xp_touch_flags(h) + 1, 0);                              /* not registered */
    SET_ERR(0x4D2);
    EXPECT_EQ(UN(NtUserModifyWindowTouchCapability)(h, flags, 1), 1);
    CHECKF(xp_touch_flags(h) == (int)flags, "read back %d want %lu", xp_touch_flags(h), flags);
    EXPECT_EQ(UN(NtUserModifyWindowTouchCapability)(h, flags, 0), 1);
    EXPECT_EQ(xp_touch_flags(h) + 1, 0);                              /* removed */
    rm(h);
}
XT(roundtrip_flags_0) { roundtrip(0); }
XT(roundtrip_flags_finetouch) { roundtrip(TWF_FINETOUCH_); }
XT(roundtrip_flags_wantpalm) { roundtrip(TWF_WANTPALM_); }
XT(roundtrip_flags_both) { roundtrip(TWF_FINETOUCH_ | TWF_WANTPALM_); }

XT(register_with_flags_zero_is_a_registration) {
    NEED(NtUserIsTouchWindow);
    HWND h = mk(); if (!h) { t_skip("no window"); return; }
    ULONG g = 0x77;
    CHECK(!UN(NtUserIsTouchWindow)(h, &g)); EXPECT_EQ(g, 0x77);
    EXPECT_EQ(UN(NtUserModifyWindowTouchCapability)(h, 0, 1), 1);
    g = 0x77;
    CHECK(UN(NtUserIsTouchWindow)(h, &g)); EXPECT_EQ(g, 0);
    rm(h);
}
XT(remove_ignores_the_flags_given) {
    NEED(NtUserModifyWindowTouchCapability);
    static const ULONG f[] = { 0, 1, 2, 3 };
    for (unsigned i = 0; i < 4; i++) for (unsigned j = 0; j < 4; j++) {
        HWND h = mk(); if (!h) { t_skip("no window"); return; }
        UN(NtUserModifyWindowTouchCapability)(h, f[i], 1);
        EXPECT_EQ(UN(NtUserModifyWindowTouchCapability)(h, f[j], 0), 1);
        CHECKF(xp_touch_flags(h) == -1, "registered with %lu, removed with %lu: still a touch window", f[i], f[j]);
        rm(h);
    }
}
XT(add_is_any_non_zero_value) {
    NEED(NtUserModifyWindowTouchCapability);
    static const int a[] = { 1, 2, 5, 0x100, 0x7FFFFFFF, -1, (int)0x80000000 };
    for (unsigned i = 0; i < sizeof a / sizeof a[0]; i++) {
        HWND h = mk(); if (!h) { t_skip("no window"); return; }
        CHECKF(UN(NtUserModifyWindowTouchCapability)(h, 1, a[i]) == 1, "add %d", a[i]);
        CHECKF(xp_touch_flags(h) == 1, "add %d: flags %d", a[i], xp_touch_flags(h));
        rm(h);
    }
}
XT(reregister_replaces_the_flags) {
    NEED(NtUserModifyWindowTouchCapability);
    HWND h = mk(); if (!h) { t_skip("no window"); return; }
    static const ULONG seq[] = { 1, 2, 3, 0, 3, 1, 2, 2, 0 };
    for (unsigned i = 0; i < sizeof seq / sizeof seq[0]; i++) {
        EXPECT_EQ(UN(NtUserModifyWindowTouchCapability)(h, seq[i], 1), 1);
        CHECKF(xp_touch_flags(h) == (int)seq[i], "step %u: flags %d want %lu", i, xp_touch_flags(h), seq[i]);
    }
    rm(h);
}
XT(remove_twice_is_harmless) {
    NEED(NtUserModifyWindowTouchCapability);
    HWND h = mk(); if (!h) { t_skip("no window"); return; }
    UN(NtUserModifyWindowTouchCapability)(h, 3, 1);
    EXPECT_EQ(UN(NtUserModifyWindowTouchCapability)(h, 0, 0), 1);
    SET_ERR(0x4D2);
    LONGLONG r = UN(NtUserModifyWindowTouchCapability)(h, 0, 0);
    t_note("second removal returns nonzero: %d", r != 0);
    CHECK(xp_touch_flags(h) == -1);
    rm(h);
}
XT(unregister_never_registered_window) {
    NEED(NtUserModifyWindowTouchCapability);
    HWND h = mk(); if (!h) { t_skip("no window"); return; }
    SET_ERR(0x4D2);
    LONGLONG r = UN(NtUserModifyWindowTouchCapability)(h, 0, 0);
    t_note("removal on an unregistered window returns nonzero: %d", r != 0);
    CHECK(xp_touch_flags(h) == -1);
    rm(h);
}

/* ---- flags screening ---- */
static void badflags(ULONG flags) {
    NEED(NtUserModifyWindowTouchCapability);
    HWND h = mk(); if (!h) { t_skip("no window"); return; }
    XFAIL(UN(NtUserModifyWindowTouchCapability)(h, flags, 1), XE_INVALID_FLAGS);
    XFAIL(UN(NtUserModifyWindowTouchCapability)(h, flags, 0), XE_INVALID_FLAGS);       /* removal screens the flags too */
    EXPECT_EQ(xp_touch_flags(h) + 1, 0);
    UN(NtUserModifyWindowTouchCapability)(h, 2, 1);
    XFAIL(UN(NtUserModifyWindowTouchCapability)(h, flags, 1), XE_INVALID_FLAGS);
    XFAIL(UN(NtUserModifyWindowTouchCapability)(h, flags, 0), XE_INVALID_FLAGS);
    CHECKF(xp_touch_flags(h) == 2, "refused call changed the registration to %d", xp_touch_flags(h));
    rm(h);
}
#define BADF(nm, v) XT(flags_##nm##_invalid_flags) { badflags(v); }
BADF(4, 4) BADF(5, 5) BADF(6, 6) BADF(7, 7) BADF(8, 8) BADF(0x10, 0x10) BADF(0x100, 0x100) BADF(0x10000, 0x10000)
BADF(0x80000000, 0x80000000u) BADF(0xFFFFFFFC, 0xFFFFFFFCu) BADF(all_ones, 0xFFFFFFFFu) BADF(0x10003, 0x10003)

/* ---- invalid windows: the handle is validated before the flags ---- */
static void badhwnd(HWND h) {
    NEED(NtUserModifyWindowTouchCapability);
    ULONG g = 0x77;
    XFAIL(UN(NtUserModifyWindowTouchCapability)(h, 1, 1), XE_INVALID_HWND);
    XFAIL(UN(NtUserModifyWindowTouchCapability)(h, 1, 0), XE_INVALID_HWND);
    XFAIL(UN(NtUserModifyWindowTouchCapability)(h, 0xFF, 1), XE_INVALID_HWND);          /* handle first, flags second */
    XFAIL(UN(NtUserIsTouchWindow)(h, &g), XE_INVALID_HWND);
    XFAIL(UN(NtUserIsTouchWindow)(h, NULL), XE_INVALID_HWND);
    EXPECT_EQ(g, 0x77);
}
#define BADHW(nm, v) XT(hwnd_##nm##_invalid_window_handle) { badhwnd((HWND)(ULONG_PTR)(v)); }
BADHW(null, 0) BADHW(one, 1) BADHW(0x1234, 0x1234) BADHW(minus1, (ULONG_PTR)-1) BADHW(0xDEADBEEF, 0xDEADBEEF)
BADHW(0x10000, 0x10000) BADHW(kernel_address, 0xFFFF800000000000ULL)

XT(hwnd_destroyed_window_invalid_window_handle) {
    NEED(NtUserModifyWindowTouchCapability);
    HWND h = mk(); if (!h) { t_skip("no window"); return; }
    UN(NtUserModifyWindowTouchCapability)(h, 3, 1);
    CHECK(xp_touch_flags(h) == 3);
    rm(h);
    badhwnd(h);
}
XT(hwnd_wrong_uniqueness_invalid_window_handle) {
    NEED(NtUserModifyWindowTouchCapability);
    HWND h = mk(); if (!h) { t_skip("no window"); return; }
    badhwnd((HWND)((ULONG_PTR)h ^ 0x20000000));         /* same table slot, other uniqueness */
    badhwnd((HWND)((ULONG_PTR)h ^ 0x40000000));
    CHECK(UN(NtUserModifyWindowTouchCapability)(h, 1, 1) == 1);
    rm(h);
}
XT(hwnd_menu_handle_is_not_a_window) {
    NEED(NtUserModifyWindowTouchCapability);
    HMENU m = U(CreateMenu)(); if (!m) { t_skip("no menu"); return; }
    badhwnd((HWND)m);
    U(DestroyMenu)(m);
}
XT(hwnd_cursor_handle_is_not_a_window) {
    NEED(NtUserModifyWindowTouchCapability);
    HCURSOR c = U(CopyIcon)(U(LoadCursorW)(NULL, (LPCWSTR)IDC_ARROW)); if (!c) { t_skip("no cursor"); return; }
    badhwnd((HWND)c);
    U(DestroyIcon)(c);
}
XT(hwnd_gdi_handle_is_not_a_window) {
    NEED(NtUserModifyWindowTouchCapability);
    HDC dc = U(GetDC)(NULL); if (!dc) { t_skip("no dc"); return; }
    badhwnd((HWND)dc);
    U(ReleaseDC)(NULL, dc);
}
XT(destroyed_window_slot_reuse_is_not_a_touch_window) {
    NEED(NtUserModifyWindowTouchCapability);
    HWND h = mk(); if (!h) { t_skip("no window"); return; }
    UN(NtUserModifyWindowTouchCapability)(h, 3, 1);
    rm(h);
    for (int i = 0; i < 8; i++) {
        HWND n = mk(); if (!n) break;
        CHECKF(xp_touch_flags(n) == -1, "a new window starts as a touch window (%d)", xp_touch_flags(n));
        rm(n);
    }
}

/* ---- per-window isolation ---- */
XT(isolation_between_sibling_windows) {
    NEED(NtUserModifyWindowTouchCapability);
    HWND a = mk(), b = mk(), c = mk();
    if (!a || !b || !c) { rm(a); rm(b); rm(c); t_skip("no windows"); return; }
    UN(NtUserModifyWindowTouchCapability)(a, 1, 1);
    UN(NtUserModifyWindowTouchCapability)(b, 2, 1);
    CHECK(xp_touch_flags(a) == 1); CHECK(xp_touch_flags(b) == 2); CHECK(xp_touch_flags(c) == -1);
    UN(NtUserModifyWindowTouchCapability)(c, 3, 1);
    CHECK(xp_touch_flags(a) == 1); CHECK(xp_touch_flags(b) == 2); CHECK(xp_touch_flags(c) == 3);
    UN(NtUserModifyWindowTouchCapability)(b, 0, 0);
    CHECK(xp_touch_flags(a) == 1); CHECK(xp_touch_flags(b) == -1); CHECK(xp_touch_flags(c) == 3);
    rm(a);
    CHECK(xp_touch_flags(b) == -1); CHECK(xp_touch_flags(c) == 3);
    rm(b); rm(c);
}
XT(isolation_between_parent_and_child) {
    NEED(NtUserModifyWindowTouchCapability);
    HWND p = mk(); if (!p) { t_skip("no window"); return; }
    HWND c = U(CreateWindowExA)(0, "win32ktHelperWnd", "child", WS_CHILD, 0, 0, 1, 1, p, NULL, GetModuleHandleA(NULL), NULL);
    if (!c) { rm(p); t_skip("no child window"); return; }
    UN(NtUserModifyWindowTouchCapability)(p, 1, 1);
    CHECK(xp_touch_flags(p) == 1); CHECK(xp_touch_flags(c) == -1);
    UN(NtUserModifyWindowTouchCapability)(c, 2, 1);
    CHECK(xp_touch_flags(p) == 1); CHECK(xp_touch_flags(c) == 2);
    UN(NtUserModifyWindowTouchCapability)(p, 0, 0);
    CHECK(xp_touch_flags(p) == -1); CHECK(xp_touch_flags(c) == 2);
    rm(p);                                                    /* destroys the child too */
    badhwnd(c);
}
XT(isolation_registration_survives_hide_move_and_text_changes) {
    NEED(NtUserModifyWindowTouchCapability);
    HWND h = mk(); if (!h) { t_skip("no window"); return; }
    UN(NtUserModifyWindowTouchCapability)(h, 3, 1);
    U(ShowWindow)(h, SW_HIDE); U(MoveWindow)(h, -32000, -32000, 2, 2, FALSE); U(SetWindowTextA)(h, "renamed");
    U(SetWindowLongPtrA)(h, GWLP_USERDATA, 7);
    CHECK(xp_touch_flags(h) == 3);
    rm(h);
}
XT(isolation_registration_not_visible_as_window_property_count) {
    NEED(NtUserModifyWindowTouchCapability);
    HWND h = mk(); if (!h) { t_skip("no window"); return; }
    UN(NtUserModifyWindowTouchCapability)(h, 3, 1);
    CHECK(U(GetPropA)(h, "xp_not_there") == NULL);
    CHECK(U(SetPropA)(h, "xp_mine", (HANDLE)5));
    CHECK(xp_touch_flags(h) == 3);
    U(RemovePropA)(h, "xp_mine");
    CHECK(xp_touch_flags(h) == 3);
    rm(h);
}

/* ---- IsTouchWindow output ---- */
XT(istouch_output_written_only_on_success) {
    NEED(NtUserIsTouchWindow);
    HWND h = mk(); if (!h) { t_skip("no window"); return; }
    ULONG g = 0x55AA55AA;
    CHECK(!UN(NtUserIsTouchWindow)(h, &g)); EXPECT_EQ(g, 0x55AA55AA);
    UN(NtUserModifyWindowTouchCapability)(h, 2, 1);
    CHECK(UN(NtUserIsTouchWindow)(h, &g)); EXPECT_EQ(g, 2);
    UN(NtUserModifyWindowTouchCapability)(h, 0, 0);
    g = 0x55AA55AA;
    CHECK(!UN(NtUserIsTouchWindow)(h, &g)); EXPECT_EQ(g, 0x55AA55AA);
    rm(h);
}
XT(istouch_null_output_is_allowed) {
    NEED(NtUserIsTouchWindow);
    HWND h = mk(); if (!h) { t_skip("no window"); return; }
    CHECK(!UN(NtUserIsTouchWindow)(h, NULL));
    UN(NtUserModifyWindowTouchCapability)(h, 1, 1);
    CHECK(UN(NtUserIsTouchWindow)(h, NULL));
    rm(h);
}
XT(istouch_unwritable_output_on_a_touch_window_is_noaccess) {
    NEED(NtUserIsTouchWindow);
    HWND h = mk(); if (!h) { t_skip("no window"); return; }
    UN(NtUserModifyWindowTouchCapability)(h, 1, 1);
    XFAIL(UN(NtUserIsTouchWindow)(h, XP_BADPTR), XE_NOACCESS);
    XFAIL(UN(NtUserIsTouchWindow)(h, XP_KERNELPTR), XE_NOACCESS);
    XFAIL(UN(NtUserIsTouchWindow)(h, (ULONG *)(ULONG_PTR)0xFFFFFFFFFFFFFFFCULL), XE_NOACCESS);
    CHECK(xp_touch_flags(h) == 1);
    rm(h);
}
XT(istouch_unwritable_output_on_a_plain_window_is_never_touched) {
    NEED(NtUserIsTouchWindow);
    HWND h = mk(); if (!h) { t_skip("no window"); return; }
    CHECK(!UN(NtUserIsTouchWindow)(h, XP_BADPTR));
    CHECK(!UN(NtUserIsTouchWindow)(h, XP_KERNELPTR));
    rm(h);
}
XT(istouch_repeated_reads_agree) {
    NEED(NtUserIsTouchWindow);
    HWND h = mk(); if (!h) { t_skip("no window"); return; }
    UN(NtUserModifyWindowTouchCapability)(h, 3, 1);
    for (int i = 0; i < 100; i++) CHECK(xp_touch_flags(h) == 3);
    rm(h);
}

/* ---- windows of other threads ---- */
XT(other_thread_window_refused_with_access_denied) {
    NEED(NtUserModifyWindowTouchCapability);
    struct xp_other o; if (!xp_other_start(&o, -1)) { t_skip("no helper thread"); return; }
    XFAIL(UN(NtUserModifyWindowTouchCapability)(o.hwnd, 1, 1), XE_ACCESS_DENIED);
    XFAIL(UN(NtUserModifyWindowTouchCapability)(o.hwnd, 3, 1), XE_ACCESS_DENIED);
    XFAIL(UN(NtUserModifyWindowTouchCapability)(o.hwnd, 0, 1), XE_ACCESS_DENIED);
    XFAIL(UN(NtUserModifyWindowTouchCapability)(o.hwnd, 1, 0), XE_ACCESS_DENIED);
    XFAIL(UN(NtUserModifyWindowTouchCapability)(o.hwnd, 0, 0), XE_ACCESS_DENIED);
    CHECK(xp_touch_flags(o.hwnd) == -1);
    xp_other_stop(&o);
}
XT(other_thread_window_flags_screened_before_the_owner) {
    NEED(NtUserModifyWindowTouchCapability);
    struct xp_other o; if (!xp_other_start(&o, -1)) { t_skip("no helper thread"); return; }
    XFAIL(UN(NtUserModifyWindowTouchCapability)(o.hwnd, 4, 1), XE_INVALID_FLAGS);
    XFAIL(UN(NtUserModifyWindowTouchCapability)(o.hwnd, 0xFFFFFFFFu, 0), XE_INVALID_FLAGS);
    xp_other_stop(&o);
}
XT(other_thread_registration_visible_but_not_removable) {
    NEED(NtUserModifyWindowTouchCapability);
    struct xp_other o; if (!xp_other_start(&o, 3)) { t_skip("no helper thread"); return; }
    CHECK(xp_touch_flags(o.hwnd) == 3);                  /* IsTouchWindow has no thread restriction */
    XFAIL(UN(NtUserModifyWindowTouchCapability)(o.hwnd, 0, 0), XE_ACCESS_DENIED);
    XFAIL(UN(NtUserModifyWindowTouchCapability)(o.hwnd, 1, 1), XE_ACCESS_DENIED);
    CHECK(xp_touch_flags(o.hwnd) == 3);
    xp_other_stop(&o);
}
XT(other_thread_registration_does_not_leak_to_our_windows) {
    NEED(NtUserModifyWindowTouchCapability);
    struct xp_other o; if (!xp_other_start(&o, 2)) { t_skip("no helper thread"); return; }
    HWND h = mk(); if (!h) { xp_other_stop(&o); t_skip("no window"); return; }
    CHECK(xp_touch_flags(h) == -1);
    UN(NtUserModifyWindowTouchCapability)(h, 1, 1);
    CHECK(xp_touch_flags(h) == 1); CHECK(xp_touch_flags(o.hwnd) == 2);
    rm(h);
    xp_other_stop(&o);
}
XT(other_thread_window_gone_after_thread_exit) {
    NEED(NtUserModifyWindowTouchCapability);
    struct xp_other o; if (!xp_other_start(&o, 1)) { t_skip("no helper thread"); return; }
    HWND h = o.hwnd;
    CHECK(xp_touch_flags(h) == 1);
    xp_other_stop(&o);
    badhwnd(h);
}
XT(desktop_window_is_owned_by_another_thread) {
    NEED(NtUserModifyWindowTouchCapability);
    HWND d = U(GetDesktopWindow)();
    XFAIL(UN(NtUserModifyWindowTouchCapability)(d, 1, 1), XE_ACCESS_DENIED);
    XFAIL(UN(NtUserModifyWindowTouchCapability)(d, 0, 0), XE_ACCESS_DENIED);
    XFAIL(UN(NtUserModifyWindowTouchCapability)(d, 4, 1), XE_INVALID_FLAGS);
    CHECK(xp_touch_flags(d) == -1);
}
XT(shell_window_is_owned_by_another_thread) {
    NEED(NtUserModifyWindowTouchCapability);
    HWND t = U(FindWindowA)("Shell_TrayWnd", NULL);
    if (!t) { t_skip("no taskbar window in this session"); return; }
    DWORD pid = 0; DWORD tid = U(GetWindowThreadProcessId)(t, &pid);
    if (tid == GetCurrentThreadId()) { t_skip("taskbar owned by this thread"); return; }
    XFAIL(UN(NtUserModifyWindowTouchCapability)(t, 1, 1), XE_ACCESS_DENIED);
    XFAIL(UN(NtUserModifyWindowTouchCapability)(t, 0, 0), XE_ACCESS_DENIED);
}
XT(message_only_window_of_this_thread_can_register) {
    NEED(NtUserModifyWindowTouchCapability);
    HWND k = mk(); if (!k) { t_skip("no window"); return; }          /* registers the helper class */
    HWND m = U(CreateWindowExA)(0, "win32ktHelperWnd", "msgonly", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, GetModuleHandleA(NULL), NULL);
    rm(k);
    if (!m) { t_skip("no message-only window"); return; }
    EXPECT_EQ(UN(NtUserModifyWindowTouchCapability)(m, 2, 1), 1);
    CHECK(xp_touch_flags(m) == 2);
    rm(m);
}

/* ---- the user32 front doors say the same ---- */
XT(front_doors_agree_with_the_syscalls) {
    NEED(NtUserModifyWindowTouchCapability);
    BOOL (WINAPI *reg)(HWND, ULONG) = (void *)t_proc_in("user32.dll", "RegisterTouchWindow");
    BOOL (WINAPI *unreg)(HWND) = (void *)t_proc_in("user32.dll", "UnregisterTouchWindow");
    BOOL (WINAPI *is)(HWND, PULONG) = (void *)t_proc_in("user32.dll", "IsTouchWindow");
    if (!reg || !unreg || !is) { t_skip("user32 touch window exports missing"); return; }
    HWND h = mk(); if (!h) { t_skip("no window"); return; }
    ULONG g = 0x77;
    CHECK(!is(h, &g));
    CHECK(reg(h, 3)); CHECK(xp_touch_flags(h) == 3); CHECK(is(h, &g)); EXPECT_EQ(g, 3);
    XFAIL(reg(h, 4), XE_INVALID_FLAGS); CHECK(xp_touch_flags(h) == 3);
    CHECK(unreg(h)); CHECK(xp_touch_flags(h) == -1);
    XFAIL(reg((HWND)0x1234, 1), XE_INVALID_HWND);
    XFAIL(unreg((HWND)0x1234), XE_INVALID_HWND);
    struct xp_other o; if (xp_other_start(&o, -1)) { XFAIL(reg(o.hwnd, 1), XE_ACCESS_DENIED); XFAIL(unreg(o.hwnd), XE_ACCESS_DENIED); xp_other_stop(&o); }
    rm(h);
}

/* ---- touch validation status ---- */
XT(validation_status_bad_handles_return_zero_and_clear_the_error) {
    NEED(NtUserGetTouchValidationStatus);
    static const ULONG_PTR h[] = { 1, 4, 0x1234, 0x10000, 0xDEADBEEF, (ULONG_PTR)-1, 0xFFFF800000000000ULL };
    for (unsigned i = 0; i < sizeof h / sizeof h[0]; i++) {
        SET_ERR(0x4D2);
        ULONG r = UN(NtUserGetTouchValidationStatus)((HANDLE)h[i]);
        CHECKF(r == 0, "handle %llx: status %lu", (unsigned long long)h[i], r);
        CHECKF(GetLastError() == 0, "handle %llx: last error %lu", (unsigned long long)h[i], GetLastError());
    }
}
XT(validation_status_window_and_menu_handles_are_not_touch_input_handles) {
    NEED(NtUserGetTouchValidationStatus);
    HWND w = mk(); HMENU m = U(CreateMenu)();
    SET_ERR(0x4D2); CHECK(UN(NtUserGetTouchValidationStatus)((HANDLE)w) == 0); EXPECT_EQ(GetLastError(), 0);
    SET_ERR(0x4D2); CHECK(UN(NtUserGetTouchValidationStatus)((HANDLE)m) == 0); EXPECT_EQ(GetLastError(), 0);
    SET_ERR(0x4D2); CHECK(UN(NtUserGetTouchValidationStatus)(GetCurrentProcess()) == 0); EXPECT_EQ(GetLastError(), 0);
    rm(w); if (m) U(DestroyMenu)(m);
}
XT(validation_status_null_is_stable_and_three_without_a_digitizer) {
    NEED(NtUserGetTouchValidationStatus);
    ULONG a = UN(NtUserGetTouchValidationStatus)(NULL), b = UN(NtUserGetTouchValidationStatus)(NULL);
    EXPECT_EQ(a, b);
    int dig = U(GetSystemMetrics)(SM_DIGITIZER);
    if (dig & 0x80 /*NID_READY*/) { t_skip("a digitizer is present; the NULL answer is hardware dependent"); return; }
    EXPECT_EQ(a, 3);
}
static DWORD WINAPI val_worker(LPVOID p) {
    t_module("user32.dll"); U(GetDesktopWindow)();
    *(ULONG *)p = UN(NtUserGetTouchValidationStatus)(NULL);
    return 0;
}
XT(validation_status_null_same_on_another_thread) {
    NEED(NtUserGetTouchValidationStatus);
    ULONG a = UN(NtUserGetTouchValidationStatus)(NULL), b = 0xFFFFFFFF;
    HANDLE t = CreateThread(NULL, 0, val_worker, &b, 0, NULL);
    if (!t) { t_skip("no thread"); return; }
    WaitForSingleObject(t, 10000); CloseHandle(t);
    EXPECT_EQ(b, a);
}
