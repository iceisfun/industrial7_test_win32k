/* x_pointer_touch_inertia_neg / misc: mouse-in-pointer promotion, touch pad registration, himetric scale factors, raw-mouse
 * window marking, mouse input state and the syscall stubs.
 *
 * NtUserPromoteMouseInPointer() and NtUserAutoPromoteMouseInPointer(): ERROR_ACCESS_DENIED while mouse-in-pointer is not
 * enabled for the thread, and still after the process called EnableMouseInPointer (helper process: the setting is
 * process-wide and cannot be undone) as long as no pointer input reached the thread.
 * NtUserRegisterTouchPadCapable(flag): marks the calling thread and always answers 1, sets no last error; any non-zero
 * flag means "capable".
 * NtUserGetHimetricScaleFactorFromPixelLocation(device, point, &x, &y): both output pointers are required
 * (ERROR_INVALID_PARAMETER); the mouse pseudo-handle (all ones) and the raw-input handle of any mouse give the identity
 * factor 1 / 1 whatever the point; every other handle (NULL, small values, keyboard handles, garbage) is
 * ERROR_INVALID_PARAMETER with the outputs untouched (the current-process pseudo handle is the same all-ones value as the
 * mouse handle); an unwritable output is ERROR_NOACCESS.
 * NtUserMarkWindowForRawMouse(hwnd, on): always answers 0 and leaves the last error alone.
 * NtUserIsMouseInputEnabled(): TRUE while mouse input is not suppressed, the same on every thread.
 * NtUserEnableTouchPad is only checked as a stub: calling it would enable or disable touch pad devices of the machine.
 *
 * Determinism: only symbolic results are printed.
 */
#include "x.h"

/* ---- promotion ---- */
XT(promote_denied_without_mouse_in_pointer) {
    NEED(NtUserPromoteMouseInPointer);
    XFAIL(UN(NtUserPromoteMouseInPointer)(), XE_ACCESS_DENIED);
    XFAIL(UN(NtUserPromoteMouseInPointer)(), XE_ACCESS_DENIED);
}
XT(auto_promote_denied_without_mouse_in_pointer) {
    NEED(NtUserAutoPromoteMouseInPointer);
    XFAIL(UN(NtUserAutoPromoteMouseInPointer)(), XE_ACCESS_DENIED);
    XFAIL(UN(NtUserAutoPromoteMouseInPointer)(), XE_ACCESS_DENIED);
}
static DWORD WINAPI promote_worker(LPVOID p) {
    t_module("user32.dll"); U(GetDesktopWindow)();
    int *f = p;
    SET_ERR(0x4D2); if (UN(NtUserPromoteMouseInPointer)() || GetLastError() != XE_ACCESS_DENIED) (*f)++;
    SET_ERR(0x4D2); if (UN(NtUserAutoPromoteMouseInPointer)() || GetLastError() != XE_ACCESS_DENIED) (*f)++;
    return 0;
}
XT(promote_denied_on_a_fresh_thread) {
    NEED(NtUserPromoteMouseInPointer);
    int fails = 0;
    HANDLE t = CreateThread(NULL, 0, promote_worker, &fails, 0, NULL);
    if (!t) { t_skip("no thread"); return; }
    WaitForSingleObject(t, 10000); CloseHandle(t);
    EXPECT_EQ(fails, 0);
}
XT(promotion_does_not_enable_mouse_in_pointer) {
    NEED(NtUserPromoteMouseInPointer);
    BOOL (WINAPI *is)(void) = (void *)t_proc_in("user32.dll", "IsMouseInPointerEnabled");
    if (!is) { t_skip("user32!IsMouseInPointerEnabled not exported"); return; }
    BOOL before = is();
    UN(NtUserPromoteMouseInPointer)(); UN(NtUserAutoPromoteMouseInPointer)();
    EXPECT_EQ(is(), before);
}

/* EnableMouseInPointer switches the process to mouse-in-pointer input, but a thread only becomes a mouse-in-pointer
 * thread when pointer input is delivered to it: with none delivered, promotion and the id-1 queries keep answering as
 * before, on the enabling thread, after it created and pumped a window, and on a thread created afterwards. */
static int still_denied(const char *where) {
    int fails = 0;
    #define CF(c) do { if (!(c)) { fails++; printf("  FAIL %s %s:%d: %s\n", where, __FILE__, __LINE__, #c); } } while (0)
    unsigned char out[0x60]; UINT a = 1, b = 1;
    SET_ERR(0x4D2); CF(!UN(NtUserPromoteMouseInPointer)() && GetLastError() == XE_ACCESS_DENIED);
    SET_ERR(0x4D2); CF(!UN(NtUserAutoPromoteMouseInPointer)() && GetLastError() == XE_ACCESS_DENIED);
    SET_ERR(0x4D2); CF(!UN(NtUserDiscardPointerFrameMessages)(1) && GetLastError() == XE_ACCESS_DENIED);
    SET_ERR(0x4D2); CF(!UN(NtUserGetPointerInfoList)(1, 1, 0, 0, 0x60, &a, &b, out) && GetLastError() == XE_INVALID_PARAMETER);
    #undef CF
    return fails;
}
static DWORD WINAPI after_enable_thread(LPVOID p) { t_module("user32.dll"); U(GetDesktopWindow)(); *(int *)p = still_denied("new thread"); return 0; }
CHILD(x_promote_after_enable) {
    int fails = 0;
    t_module("user32.dll"); U(GetDesktopWindow)();
    BOOL (WINAPI *en)(BOOL) = (void *)t_proc_in("user32.dll", "EnableMouseInPointer");
    BOOL (WINAPI *is)(void) = (void *)t_proc_in("user32.dll", "IsMouseInPointerEnabled");
    if (!en || !is || !UN(NtUserPromoteMouseInPointer) || !UN(NtUserAutoPromoteMouseInPointer)) { printf("  SKIP exports missing\n"); return 77; }
    if (is()) { printf("  SKIP mouse-in-pointer already on\n"); return 77; }
    fails += still_denied("before");
    if (!en(TRUE) || !is()) { printf("  SKIP EnableMouseInPointer refused\n"); return 77; }
    fails += still_denied("after enable");
    HWND h = k_make_window("xp_mip"); k_pump(30);
    fails += still_denied("after window");
    if (h) U(DestroyWindow)(h);
    int nf = 0; HANDLE t = CreateThread(NULL, 0, after_enable_thread, &nf, 0, NULL);
    if (t) { WaitForSingleObject(t, 10000); CloseHandle(t); } else nf = 1;
    return fails + nf;
}
XT(promotion_still_denied_after_enable_mouse_in_pointer_without_pointer_input) {
    NEED(NtUserPromoteMouseInPointer);
    struct k_child_result r = k_child_run("x_promote_after_enable", "", 60000);
    if (r.exit_code == 77) { t_skip("EnableMouseInPointer unavailable or already on"); return; }
    EXPECT_EQ(r.exit_code, 0);
}

/* ---- touch pad registration ---- */
XT(touchpad_capable_always_answers_one) {
    NEED(NtUserRegisterTouchPadCapable);
    static const int v[] = { 0, 1, 2, 5, 0x100, -1, 0x7FFFFFFF, (int)0x80000000 };
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) {
        SET_ERR(0x4D2);
        ULONGLONG r = UN(NtUserRegisterTouchPadCapable)(v[i]);
        CHECKF(r == 1, "flag %d: returned 0x%llx", v[i], (unsigned long long)r);
        CHECKF(GetLastError() == 0x4D2, "flag %d: last error changed to %lu", v[i], GetLastError());
    }
    UN(NtUserRegisterTouchPadCapable)(0);
}
static DWORD WINAPI tp_worker(LPVOID p) {
    t_module("user32.dll"); U(GetDesktopWindow)();
    int *f = p;
    for (int i = 0; i < 4; i++) if (UN(NtUserRegisterTouchPadCapable)(i & 1) != 1) (*f)++;
    return 0;
}
XT(touchpad_capable_on_worker_threads) {
    NEED(NtUserRegisterTouchPadCapable);
    int fails = 0;
    for (int k = 0; k < 3; k++) {
        HANDLE t = CreateThread(NULL, 0, tp_worker, &fails, 0, NULL);
        if (!t) { t_skip("no thread"); return; }
        WaitForSingleObject(t, 10000); CloseHandle(t);
    }
    EXPECT_EQ(fails, 0);
}
XT(touchpad_capable_does_not_disturb_other_state) {
    NEED(NtUserRegisterTouchPadCapable);
    HWND h = k_make_window("xp_tp"); if (!h) { t_skip("no window"); return; }
    UN(NtUserModifyWindowTouchCapability)(h, 3, 1);
    UN(NtUserRegisterTouchPadCapable)(1); UN(NtUserRegisterTouchPadCapable)(0);
    CHECK(xp_touch_flags(h) == 3);
    EXPECT_EQ(UN(NtUserIsMouseInputEnabled)() & 0xFF, 1);
    U(DestroyWindow)(h);
}

/* ---- himetric scale factors ---- */
#define MOUSE_PSEUDO ((HANDLE)(LONG_PTR)-1)
static void himetric_mouse(POINT pt) {
    UINT x = 0x77, y = 0x88;
    SET_ERR(0x4D2);
    CHECK(UN(NtUserGetHimetricScaleFactorFromPixelLocation)(MOUSE_PSEUDO, pt, &x, &y));
    EXPECT_EQ(x, 1); EXPECT_EQ(y, 1);
    EXPECT_EQ(GetLastError(), 0x4D2);
}
XT(himetric_mouse_pseudo_handle_origin) { NEED(NtUserGetHimetricScaleFactorFromPixelLocation); POINT p = { 0, 0 }; himetric_mouse(p); }
XT(himetric_mouse_pseudo_handle_positive) { NEED(NtUserGetHimetricScaleFactorFromPixelLocation); POINT p = { 100, 200 }; himetric_mouse(p); }
XT(himetric_mouse_pseudo_handle_negative) { NEED(NtUserGetHimetricScaleFactorFromPixelLocation); POINT p = { -100000, -100000 }; himetric_mouse(p); }
XT(himetric_mouse_pseudo_handle_extremes) { NEED(NtUserGetHimetricScaleFactorFromPixelLocation); POINT p = { 0x7FFFFFFF, (LONG)0x80000000 }; himetric_mouse(p); POINT q = { (LONG)0x80000000, 0x7FFFFFFF }; himetric_mouse(q); }
XT(himetric_mouse_pseudo_handle_screen_corners) {
    NEED(NtUserGetHimetricScaleFactorFromPixelLocation);
    POINT p = { U(GetSystemMetrics)(SM_XVIRTUALSCREEN), U(GetSystemMetrics)(SM_YVIRTUALSCREEN) }; himetric_mouse(p);
    POINT q = { p.x + U(GetSystemMetrics)(SM_CXVIRTUALSCREEN), p.y + U(GetSystemMetrics)(SM_CYVIRTUALSCREEN) }; himetric_mouse(q);
}
XT(himetric_requires_both_outputs) {
    NEED(NtUserGetHimetricScaleFactorFromPixelLocation);
    UINT v = 0x77; POINT p = { 5, 5 };
    XFAIL(UN(NtUserGetHimetricScaleFactorFromPixelLocation)(MOUSE_PSEUDO, p, NULL, &v), XE_INVALID_PARAMETER);
    XFAIL(UN(NtUserGetHimetricScaleFactorFromPixelLocation)(MOUSE_PSEUDO, p, &v, NULL), XE_INVALID_PARAMETER);
    XFAIL(UN(NtUserGetHimetricScaleFactorFromPixelLocation)(MOUSE_PSEUDO, p, NULL, NULL), XE_INVALID_PARAMETER);
    EXPECT_EQ(v, 0x77);
}
XT(himetric_unwritable_output_is_noaccess) {
    NEED(NtUserGetHimetricScaleFactorFromPixelLocation);
    UINT v = 0x77; POINT p = { 5, 5 };
    XFAIL(UN(NtUserGetHimetricScaleFactorFromPixelLocation)(MOUSE_PSEUDO, p, XP_BADPTR, &v), XE_NOACCESS);
    XFAIL(UN(NtUserGetHimetricScaleFactorFromPixelLocation)(MOUSE_PSEUDO, p, &v, XP_BADPTR), XE_NOACCESS);
    XFAIL(UN(NtUserGetHimetricScaleFactorFromPixelLocation)(MOUSE_PSEUDO, p, XP_KERNELPTR, &v), XE_NOACCESS);
    XFAIL(UN(NtUserGetHimetricScaleFactorFromPixelLocation)(MOUSE_PSEUDO, p, &v, XP_KERNELPTR), XE_NOACCESS);
}
static void himetric_bad(HANDLE h) {
    NEED(NtUserGetHimetricScaleFactorFromPixelLocation);
    UINT x = 0x77, y = 0x88; POINT p = { 5, 5 };
    XFAIL(UN(NtUserGetHimetricScaleFactorFromPixelLocation)(h, p, &x, &y), XE_INVALID_PARAMETER);
    EXPECT_EQ(x, 0x77); EXPECT_EQ(y, 0x88);
    XFAIL(UN(NtUserGetHimetricScaleFactorFromPixelLocation)(h, p, NULL, &y), XE_INVALID_PARAMETER);
}
#define HBAD(nm, v) XT(himetric_handle_##nm##_invalid_parameter) { himetric_bad((HANDLE)(ULONG_PTR)(v)); }
HBAD(null, 0) HBAD(one, 1) HBAD(0x1234, 0x1234) HBAD(minus2, (ULONG_PTR)-2)
HBAD(0xDEADBEEF, 0xDEADBEEF) HBAD(0x80000000, 0x80000000u) HBAD(0xFFFFFFFF, 0xFFFFFFFFu) HBAD(kernel_address, 0xFFFF800000000000ULL)
XT(himetric_window_and_object_handles_invalid_parameter) {
    HWND w = k_make_window("xp_hm"); if (!w) { t_skip("no window"); return; }
    himetric_bad((HANDLE)w);
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL); himetric_bad(ev); CloseHandle(ev);
    HANDLE th = GetCurrentThread(); himetric_bad(th);                      /* (the current-process pseudo handle is the mouse handle) */
    U(DestroyWindow)(w);
}
XT(himetric_raw_input_device_handles) {
    NEED(NtUserGetHimetricScaleFactorFromPixelLocation);
    UINT n = 0;
    if (!U(GetRawInputDeviceList) || U(GetRawInputDeviceList)(NULL, &n, sizeof(RAWINPUTDEVICELIST)) != 0 || n == 0) { t_skip("no raw input devices"); return; }
    RAWINPUTDEVICELIST *l = calloc(n + 4, sizeof *l);
    UINT c = n + 4;
    UINT got = U(GetRawInputDeviceList)(l, &c, sizeof *l);
    if (got == (UINT)-1 || got == 0) { free(l); t_skip("device list unavailable"); return; }
    int mice = 0, others = 0;
    POINT p = { 7, 9 };
    for (UINT i = 0; i < got; i++) {
        UINT x = 0x77, y = 0x88;
        SET_ERR(0x4D2);
        BOOL r = UN(NtUserGetHimetricScaleFactorFromPixelLocation)(l[i].hDevice, p, &x, &y);
        if (l[i].dwType == RIM_TYPEMOUSE) { mice++; CHECK(r); EXPECT_EQ(x, 1); EXPECT_EQ(y, 1); }
        else { others++; CHECK(!r); EXPECT_EQ(GetLastError(), XE_INVALID_PARAMETER); EXPECT_EQ(x, 0x77); EXPECT_EQ(y, 0x88); }
    }
    free(l);
    if (!mice) t_skip("no mouse raw-input device");
}

/* ---- MarkWindowForRawMouse / IsMouseInputEnabled ---- */
XT(mark_raw_mouse_answers_zero_and_keeps_the_error) {
    NEED(NtUserMarkWindowForRawMouse);
    HWND h = k_make_window("xp_raw"); if (!h) { t_skip("no window"); return; }
    static const int on[] = { 0, 1, 2, -1 };
    for (unsigned i = 0; i < sizeof on / sizeof on[0]; i++) {
        SET_ERR(0x4D2);
        ULONG_PTR r = UN(NtUserMarkWindowForRawMouse)(h, on[i]);
        CHECKF(r == 0, "on %d: returned 0x%llx", on[i], (unsigned long long)r);
        CHECKF(GetLastError() == 0x4D2, "on %d: last error changed to %lu", on[i], GetLastError());
    }
    U(DestroyWindow)(h);
}
XT(mark_raw_mouse_ignores_the_window) {
    NEED(NtUserMarkWindowForRawMouse);
    static const ULONG_PTR h[] = { 0, 1, 0x1234, (ULONG_PTR)-1, 0xDEADBEEF, 0xFFFF800000000000ULL };
    for (unsigned i = 0; i < sizeof h / sizeof h[0]; i++) {
        SET_ERR(0x4D2);
        CHECKF(UN(NtUserMarkWindowForRawMouse)((HWND)h[i], 1) == 0, "window %llx", (unsigned long long)h[i]);
        CHECKF(GetLastError() == 0x4D2, "window %llx: last error %lu", (unsigned long long)h[i], GetLastError());
    }
    CHECK(UN(NtUserMarkWindowForRawMouse)(U(GetDesktopWindow)(), 1) == 0);
}
XT(mark_raw_mouse_other_thread_and_destroyed_window) {
    NEED(NtUserMarkWindowForRawMouse);
    struct xp_other o; if (!xp_other_start(&o, -1)) { t_skip("no helper thread"); return; }
    SET_ERR(0x4D2); CHECK(UN(NtUserMarkWindowForRawMouse)(o.hwnd, 1) == 0); EXPECT_EQ(GetLastError(), 0x4D2);
    HWND h = o.hwnd;
    xp_other_stop(&o);
    SET_ERR(0x4D2); CHECK(UN(NtUserMarkWindowForRawMouse)(h, 1) == 0); EXPECT_EQ(GetLastError(), 0x4D2);
}
XT(mark_raw_mouse_does_not_mark_touch_state) {
    NEED(NtUserMarkWindowForRawMouse);
    HWND h = k_make_window("xp_raw"); if (!h) { t_skip("no window"); return; }
    UN(NtUserMarkWindowForRawMouse)(h, 1);
    CHECK(xp_touch_flags(h) == -1);
    U(DestroyWindow)(h);
}
XT(mouse_input_enabled_is_true_and_stable) {
    NEED(NtUserIsMouseInputEnabled);
    for (int i = 0; i < 50; i++) EXPECT_EQ(UN(NtUserIsMouseInputEnabled)() & 0xFF, 1);
}
XT(mouse_input_enabled_keeps_the_error) {
    NEED(NtUserIsMouseInputEnabled);
    SET_ERR(0x4D2);
    UN(NtUserIsMouseInputEnabled)();
    EXPECT_EQ(GetLastError(), 0x4D2);
}
static DWORD WINAPI mi_worker(LPVOID p) {
    t_module("user32.dll"); U(GetDesktopWindow)();
    *(LONG *)p = UN(NtUserIsMouseInputEnabled)() & 0xFF;
    return 0;
}
XT(mouse_input_enabled_same_on_another_thread) {
    NEED(NtUserIsMouseInputEnabled);
    LONG v = -1;
    HANDLE t = CreateThread(NULL, 0, mi_worker, &v, 0, NULL);
    if (!t) { t_skip("no thread"); return; }
    WaitForSingleObject(t, 10000); CloseHandle(t);
    EXPECT_EQ(v, UN(NtUserIsMouseInputEnabled)() & 0xFF);
}
XT(mouse_input_enabled_matches_cursor_visibility_rules) {
    NEED(NtUserIsMouseInputEnabled);
    /* hiding the cursor counter does not suppress mouse input */
    int c = U(ShowCursor)(FALSE);
    EXPECT_EQ(UN(NtUserIsMouseInputEnabled)() & 0xFF, 1);
    int d = U(ShowCursor)(TRUE);
    EXPECT_EQ(d, c + 1);
    EXPECT_EQ(UN(NtUserIsMouseInputEnabled)() & 0xFF, 1);
}

/* ---- stubs ---- */
static const char *const names[] = {
    "NtUserGetPointerInfoList", "NtUserGetPointerProprietaryId", "NtUserDiscardPointerFrameMessages", "NtUserInjectPointerInput",
    "NtUserIsTouchWindow", "NtUserModifyWindowTouchCapability", "NtUserGetTouchValidationStatus", "NtUserRegisterManipulationThread",
    "NtUserSetManipulationInputTarget", "NtUserStopAndEndInertia", "NtUserReportInertia", "NtUserAutoPromoteMouseInPointer",
    "NtUserPromoteMouseInPointer", "NtUserEnableTouchPad", "NtUserRegisterTouchPadCapable", "NtUserGetHimetricScaleFactorFromPixelLocation",
    "NtUserMarkWindowForRawMouse", "NtUserIsMouseInputEnabled",
};
#define NNAMES (sizeof names / sizeof *names)
static void check_stub(unsigned i) {
    if (t_is_wine()) { t_skip("emulator"); return; }
    FARPROC p = t_proc_in("win32u.dll", names[i]);
    if (!p) { CHECKF(0, "win32u!%s missing", names[i]); return; }
    CHECKF(k_stub_valid(p), "%s: not a syscall stub", names[i]);
    long n = k_stub_number(p);
    CHECKF(n >= 0x1000, "%s: service number %ld below the win32k range", names[i], n);
    CHECKF(p == t_proc(names[i]), "%s: the search order does not resolve it to win32u", names[i]);
}
XT(stubs_are_well_formed_in_the_win32k_range) { for (unsigned i = 0; i < NNAMES; i++) check_stub(i); }
XT(stub_service_numbers_are_distinct) {
    if (t_is_wine()) { t_skip("emulator"); return; }
    long n[NNAMES];
    for (unsigned i = 0; i < NNAMES; i++) { FARPROC p = t_proc_in("win32u.dll", names[i]); n[i] = p ? k_stub_number(p) : -1; }
    for (unsigned i = 0; i < NNAMES; i++) for (unsigned j = i + 1; j < NNAMES; j++)
        CHECKF(n[i] < 0 || n[i] != n[j], "%s and %s share number %ld", names[i], names[j], n[i]);
}
XT(user32_touch_front_doors_reach_the_same_services) {
    if (t_is_wine()) { t_skip("emulator"); return; }
    /* IsTouchWindow is a bare jump to the win32u stub; the others wrap it. Both routes must answer alike for one window. */
    BOOL (WINAPI *is)(HWND, PULONG) = (void *)t_proc_in("user32.dll", "IsTouchWindow");
    if (!is || !UN(NtUserIsTouchWindow)) { t_skip("exports missing"); return; }
    HWND h = k_make_window("xp_stub"); if (!h) { t_skip("no window"); return; }
    ULONG a = 0x77, b = 0x77;
    CHECK(is(h, &a) == UN(NtUserIsTouchWindow)(h, &b)); EXPECT_EQ(a, b);
    UN(NtUserModifyWindowTouchCapability)(h, 3, 1);
    CHECK(is(h, &a)); CHECK(UN(NtUserIsTouchWindow)(h, &b)); EXPECT_EQ(a, b);
    U(DestroyWindow)(h);
}
