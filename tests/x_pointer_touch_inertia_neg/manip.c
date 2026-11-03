/* x_pointer_touch_inertia_neg / manip: the manipulation-thread and inertia entry points.
 *
 * Three of them belong to the desktop compositor process alone: NtUserRegisterManipulationThread(),
 * NtUserSetManipulationInputTarget(id, target, count, array) and NtUserStopAndEndInertia(id, flags, region). Any other
 * process gets FALSE with ERROR_ACCESS_DENIED, from any thread, with any arguments (the caller is checked before the
 * arguments are looked at: wild pointers, huge counts and zero counts all give the same answer), and the process is
 * not left registered (repeating the call gives the same answer).
 *
 * NtUserReportInertia(id, flags, hwnd, routing, info, region) screens its arguments in a fixed order, all observed here:
 *   id 0                              -> ERROR_INVALID_PARAMETER
 *   flags with a bit outside 7        -> ERROR_INVALID_PARAMETER
 *   flags must carry exactly one of 1 (add info) and 2 (invalidate): 0 and 3 -> ERROR_INVALID_PARAMETER
 *   flags 2 (with or without 4)       -> invalidate: for flags 2 the answer is TRUE for any id, window and pointers
 *   flags with bit 4 and a window     -> ERROR_INVALID_PARAMETER; with bit 4 and no window, a non-compositor caller gets ERROR_ACCESS_DENIED
 *   flags 1                           -> info must be present (ERROR_INVALID_PARAMETER), the window must be valid
 *                                        (ERROR_INVALID_WINDOW_HANDLE) and, if a routing structure is passed, a minimal-user handle;
 *                                        then the window's thread must be the caller (ERROR_INVALID_PARAMETER otherwise) and
 *                                        the caller must have received touch input within the last two seconds (ERROR_ACCESS_DENIED)
 *
 * Determinism: only symbolic results are printed.
 */
#include "x.h"

/* ---- compositor-only entry points ---- */
XT(register_manipulation_thread_denied) {
    NEED(NtUserRegisterManipulationThread);
    XFAIL(UN(NtUserRegisterManipulationThread)(), XE_ACCESS_DENIED);
}
XT(register_manipulation_thread_denied_every_time) {
    NEED(NtUserRegisterManipulationThread);
    for (int i = 0; i < 20; i++) XFAIL(UN(NtUserRegisterManipulationThread)(), XE_ACCESS_DENIED);
}
static DWORD WINAPI reg_worker(LPVOID p) {
    t_module("user32.dll"); U(GetDesktopWindow)();
    int *f = p;
    SET_ERR(0x4D2); if (UN(NtUserRegisterManipulationThread)() || GetLastError() != XE_ACCESS_DENIED) (*f)++;
    SET_ERR(0x4D2); if (UN(NtUserSetManipulationInputTarget)(1, NULL, 1, NULL) || GetLastError() != XE_ACCESS_DENIED) (*f)++;
    SET_ERR(0x4D2); if (UN(NtUserStopAndEndInertia)(1, 0, NULL) || GetLastError() != XE_ACCESS_DENIED) (*f)++;
    return 0;
}
XT(compositor_only_entry_points_denied_on_another_thread) {
    NEED(NtUserRegisterManipulationThread);
    int fails = 0;
    HANDLE t = CreateThread(NULL, 0, reg_worker, &fails, 0, NULL);
    if (!t) { t_skip("no thread"); return; }
    WaitForSingleObject(t, 10000); CloseHandle(t);
    EXPECT_EQ(fails, 0);
}
XT(register_manipulation_thread_denied_for_a_gui_thread_with_a_window) {
    NEED(NtUserRegisterManipulationThread);
    HWND h = k_make_window("xp_manip"); if (!h) { t_skip("no window"); return; }
    XFAIL(UN(NtUserRegisterManipulationThread)(), XE_ACCESS_DENIED);
    U(DestroyWindow)(h);
}

#define SETT(nm, id, tgt, cnt, arr) XT(set_target_##nm) { NEED(NtUserSetManipulationInputTarget); XFAIL(UN(NtUserSetManipulationInputTarget)(id, tgt, cnt, arr), XE_ACCESS_DENIED); }
static int buf4[4];
SETT(all_zero, 0, NULL, 0, NULL)
SETT(count_zero_valid_array, 1, NULL, 0, buf4)
SETT(count_one_null_array, 1, NULL, 1, NULL)
SETT(count_one_valid_array, 1, NULL, 1, buf4)
SETT(count_four_valid_array, 1, buf4, 4, buf4)
SETT(huge_count, 1, NULL, 0xFFFFFFFFu, buf4)
SETT(count_2g, 1, NULL, 0x80000000u, buf4)
SETT(unreadable_array, 1, NULL, 4, XP_BADPTR)
SETT(kernel_array, 1, NULL, 4, XP_KERNELPTR)
SETT(misaligned_array, 1, NULL, 1, ((char *)buf4 + 1))
SETT(unreadable_target, 1, XP_BADPTR, 1, buf4)
SETT(kernel_target, 1, XP_KERNELPTR, 1, buf4)
SETT(id_all_ones, 0xFFFFFFFFu, NULL, 1, buf4)
SETT(id_big, 0x10001, buf4, 1, buf4)
XT(set_target_with_a_real_window_as_target) {
    NEED(NtUserSetManipulationInputTarget);
    HWND h = k_make_window("xp_manip"); if (!h) { t_skip("no window"); return; }
    UINT ids[2] = { 1, 2 };
    XFAIL(UN(NtUserSetManipulationInputTarget)(1, h, 2, ids), XE_ACCESS_DENIED);
    U(DestroyWindow)(h);
}

#define STOP(nm, id, fl, rg) XT(stop_inertia_##nm) { NEED(NtUserStopAndEndInertia); XFAIL(UN(NtUserStopAndEndInertia)(id, fl, rg), XE_ACCESS_DENIED); }
STOP(all_zero, 0, 0, NULL)
STOP(id_one, 1, 0, NULL)
STOP(id_big, 0xFFFFFFFFFFFFFFFFULL, 0, NULL)
STOP(flags_one, 1, 1, NULL)
STOP(flags_all_ones, 1, 0xFFFFFFFFu, NULL)
STOP(unreadable_region, 1, 1, XP_BADPTR)
STOP(kernel_region, 1, 1, XP_KERNELPTR)
STOP(valid_region, 1, 1, buf4)

/* ---- ReportInertia ---- */
static LONGLONG ri(ULONG_PTR id, UINT flags, HWND h, void *routing, void *info, void *region) {
    SET_ERR(0x4D2);
    return UN(NtUserReportInertia)(id, flags, h, routing, info, region);
}
static void ri_fail(ULONG_PTR id, UINT flags, HWND h, void *routing, void *info, void *region, DWORD want, const char *what) {
    LONGLONG r = ri(id, flags, h, routing, info, region);
    DWORD gle = GetLastError();
    CHECKF((r & 0xFFFFFFFF) == 0, "%s: returned 0x%llx", what, (unsigned long long)r);
    CHECKF(gle == want, "%s: last error %lu want %lu", what, gle, want);
}
static void ri_ok(ULONG_PTR id, UINT flags, HWND h, void *routing, void *info, void *region, const char *what) {
    LONGLONG r = ri(id, flags, h, routing, info, region);
    CHECKF((r & 0xFFFFFFFF) == 1, "%s: returned 0x%llx", what, (unsigned long long)r);
}
static char info[0x100];

XT(report_id_zero_invalid_parameter) {
    NEED(NtUserReportInertia);
    static const UINT f[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 0xFFFFFFFFu };
    for (unsigned i = 0; i < sizeof f / sizeof f[0]; i++) ri_fail(0, f[i], NULL, NULL, info, NULL, XE_INVALID_PARAMETER, "id 0");
}
XT(report_flags_outside_mask_invalid_parameter) {
    NEED(NtUserReportInertia);
    static const UINT f[] = { 8, 9, 10, 0x10, 0x11, 0x12, 0x100, 0x80000002u, 0xFFFFFFF8u, 0xFFFFFFFFu, 0xFFFFFFFAu };
    for (unsigned i = 0; i < sizeof f / sizeof f[0]; i++) {
        ri_fail(1, f[i], NULL, NULL, info, NULL, XE_INVALID_PARAMETER, "flags outside the mask, no window");
        ri_fail(0x1234, f[i], (HWND)0x1234, NULL, NULL, NULL, XE_INVALID_PARAMETER, "flags outside the mask, bad window");
    }
}
XT(report_flags_without_add_or_invalidate_invalid_parameter) {
    NEED(NtUserReportInertia);
    static const UINT f[] = { 0, 4, 3, 7 };
    for (unsigned i = 0; i < sizeof f / sizeof f[0]; i++) {
        ri_fail(1, f[i], NULL, NULL, info, NULL, XE_INVALID_PARAMETER, "neither or both of add and invalidate");
        ri_fail(1, f[i], (HWND)0x1234, NULL, NULL, NULL, XE_INVALID_PARAMETER, "neither or both, bad window");
        ri_fail(1, f[i], NULL, NULL, NULL, NULL, XE_INVALID_PARAMETER, "neither or both, nothing else");
    }
}
XT(report_invalidate_succeeds_for_any_id) {
    NEED(NtUserReportInertia);
    static const ULONG_PTR ids[] = { 1, 2, 0x1234, 0xFFFF, 0x10000, 0x7FFFFFFF, 0x80000000u, 0xFFFFFFFFu, 1ULL << 40, 0xFFFFFFFFFFFFFFFFULL };
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++) ri_ok(ids[i], 2, NULL, NULL, NULL, NULL, "invalidate");
}
XT(report_invalidate_ignores_window_and_pointers) {
    NEED(NtUserReportInertia);
    HWND h = k_make_window("xp_ri"); if (!h) { t_skip("no window"); return; }
    ri_ok(1, 2, h, NULL, NULL, NULL, "own window");
    ri_ok(1, 2, U(GetDesktopWindow)(), NULL, NULL, NULL, "desktop window");
    ri_ok(1, 2, NULL, NULL, info, NULL, "info pointer");
    ri_ok(1, 2, h, info, info, info, "all pointers");
    ri_ok(1, 2, h, XP_BADPTR, XP_BADPTR, XP_BADPTR, "unreadable pointers");
    U(DestroyWindow)(h);
}
XT(report_invalidate_other_thread_window_and_bad_window) {
    NEED(NtUserReportInertia);
    struct xp_other o; if (!xp_other_start(&o, -1)) { t_skip("no helper thread"); return; }
    ri_ok(1, 2, o.hwnd, NULL, NULL, NULL, "other thread window");
    xp_other_stop(&o);
    ri_ok(1, 2, o.hwnd, NULL, NULL, NULL, "destroyed window");
}
XT(report_invalidate_is_repeatable) {
    NEED(NtUserReportInertia);
    for (int i = 0; i < 50; i++) ri_ok(7, 2, NULL, NULL, NULL, NULL, "invalidate repeat");
}
XT(report_bit4_with_a_window_invalid_parameter) {
    NEED(NtUserReportInertia);
    HWND h = k_make_window("xp_ri"); if (!h) { t_skip("no window"); return; }
    static const UINT f[] = { 5, 6 };
    for (unsigned i = 0; i < 2; i++) {
        ri_fail(1, f[i], h, NULL, NULL, NULL, XE_INVALID_PARAMETER, "own window");
        ri_fail(1, f[i], h, NULL, info, NULL, XE_INVALID_PARAMETER, "own window, info");
        ri_fail(1, f[i], (HWND)0x1234, NULL, NULL, NULL, XE_INVALID_PARAMETER, "bad window");
        ri_fail(1, f[i], U(GetDesktopWindow)(), NULL, NULL, NULL, XE_INVALID_PARAMETER, "desktop window");
    }
    U(DestroyWindow)(h);
}
XT(report_bit4_without_a_window_access_denied) {
    NEED(NtUserReportInertia);
    static const UINT f[] = { 5, 6 };
    for (unsigned i = 0; i < 2; i++) {
        ri_fail(1, f[i], NULL, NULL, NULL, NULL, XE_ACCESS_DENIED, "no info");
        ri_fail(1, f[i], NULL, NULL, info, NULL, XE_ACCESS_DENIED, "info");
        ri_fail(0xFFFFFFFFFFFFFFFFULL, f[i], NULL, XP_BADPTR, XP_BADPTR, XP_BADPTR, XE_ACCESS_DENIED, "wild pointers");
    }
}
XT(report_add_without_info_invalid_parameter) {
    NEED(NtUserReportInertia);
    HWND h = k_make_window("xp_ri"); if (!h) { t_skip("no window"); return; }
    ri_fail(1, 1, h, NULL, NULL, NULL, XE_INVALID_PARAMETER, "own window");
    ri_fail(1, 1, NULL, NULL, NULL, NULL, XE_INVALID_PARAMETER, "no window");
    ri_fail(1, 1, U(GetDesktopWindow)(), NULL, NULL, info, XE_INVALID_PARAMETER, "region only");
    U(DestroyWindow)(h);
}
XT(report_add_bad_window_invalid_window_handle) {
    NEED(NtUserReportInertia);
    static const ULONG_PTR h[] = { 1, 0x1234, 0xDEADBEEF, 0x10000, (ULONG_PTR)-1 };
    for (unsigned i = 0; i < sizeof h / sizeof h[0]; i++) ri_fail(1, 1, (HWND)h[i], NULL, info, NULL, XE_INVALID_HWND, "invalid window");
}
XT(report_add_no_window_invalid_window_handle) {
    NEED(NtUserReportInertia);
    ri_fail(1, 1, NULL, NULL, info, NULL, XE_INVALID_HWND, "no window, info");
    ri_fail(1, 1, NULL, NULL, info, info, XE_INVALID_HWND, "no window, info and region");
}
XT(report_add_destroyed_window_invalid_window_handle) {
    NEED(NtUserReportInertia);
    HWND h = k_make_window("xp_ri"); if (!h) { t_skip("no window"); return; }
    U(DestroyWindow)(h);
    ri_fail(1, 1, h, NULL, info, NULL, XE_INVALID_HWND, "destroyed window");
}
XT(report_add_routing_with_a_window_handle_invalid_window_handle) {
    NEED(NtUserReportInertia);
    HWND h = k_make_window("xp_ri"); if (!h) { t_skip("no window"); return; }
    ri_fail(1, 1, h, info, info, NULL, XE_INVALID_HWND, "routing structure present");
    ri_fail(1, 1, h, XP_BADPTR, info, NULL, XE_INVALID_HWND, "unreadable routing structure");
    U(DestroyWindow)(h);
}
XT(report_add_other_thread_window_invalid_parameter) {
    NEED(NtUserReportInertia);
    struct xp_other o; if (!xp_other_start(&o, -1)) { t_skip("no helper thread"); return; }
    ri_fail(1, 1, o.hwnd, NULL, info, NULL, XE_INVALID_PARAMETER, "window of another thread");
    xp_other_stop(&o);
    ri_fail(1, 1, U(GetDesktopWindow)(), NULL, info, NULL, XE_INVALID_PARAMETER, "desktop window");
}
XT(report_add_own_window_without_recent_touch_input_access_denied) {
    NEED(NtUserReportInertia);
    HWND h = k_make_window("xp_ri"); if (!h) { t_skip("no window"); return; }
    ri_fail(1, 1, h, NULL, info, NULL, XE_ACCESS_DENIED, "info only");
    ri_fail(1, 1, h, NULL, info, info, XE_ACCESS_DENIED, "info and region");
    ri_fail(1, 1, h, NULL, XP_BADPTR, NULL, XE_ACCESS_DENIED, "unreadable info is never read");
    ri_fail(0x1234, 1, h, NULL, info, NULL, XE_ACCESS_DENIED, "other id");
    ri_fail(0xFFFFFFFFFFFFFFFFULL, 1, h, NULL, info, NULL, XE_ACCESS_DENIED, "id all ones");
    U(DestroyWindow)(h);
}
XT(report_add_on_a_thread_that_owns_a_child_window) {
    NEED(NtUserReportInertia);
    HWND p = k_make_window("xp_ri"); if (!p) { t_skip("no window"); return; }
    HWND c = U(CreateWindowExA)(0, "win32ktHelperWnd", "child", WS_CHILD, 0, 0, 1, 1, p, NULL, GetModuleHandleA(NULL), NULL);
    if (!c) { U(DestroyWindow)(p); t_skip("no child window"); return; }
    ri_fail(1, 1, c, NULL, info, NULL, XE_ACCESS_DENIED, "child window of this thread");
    U(DestroyWindow)(p);
}
static DWORD WINAPI ri_worker(LPVOID p) {
    t_module("user32.dll");
    int *f = p;
    HWND h = k_make_window("xp_ri_w");
    if (!h) { (*f) += 100; return 0; }
    SET_ERR(0x4D2); if (UN(NtUserReportInertia)(1, 1, h, NULL, info, NULL) || GetLastError() != XE_ACCESS_DENIED) (*f)++;
    SET_ERR(0x4D2); if (UN(NtUserReportInertia)(1, 2, NULL, NULL, NULL, NULL) != 1) (*f)++;
    SET_ERR(0x4D2); if (UN(NtUserReportInertia)(0, 2, NULL, NULL, NULL, NULL) || GetLastError() != XE_INVALID_PARAMETER) (*f)++;
    U(DestroyWindow)(h);
    return 0;
}
XT(report_same_answers_on_another_gui_thread) {
    NEED(NtUserReportInertia);
    int fails = 0;
    HANDLE t = CreateThread(NULL, 0, ri_worker, &fails, 0, NULL);
    if (!t) { t_skip("no thread"); return; }
    WaitForSingleObject(t, 10000); CloseHandle(t);
    EXPECT_EQ(fails, 0);
}
