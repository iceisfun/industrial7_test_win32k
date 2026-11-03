/* Per-window DPI syscalls, exercised in this (DPI-unaware, manifest-less) process:
 *   NtUserLogicalToPhysicalDpiPointForWindow / NtUserPhysicalToLogicalDpiPointForWindow
 *   NtUserDeferWindowDpiChanges / NtUserRestoreWindowDpiChanges
 *   NtUserEnableWindowGDIScaledDpiMessage / NtUserIsWindowGDIScaledDpiMessageEnabled
 *   NtUserSetDialogControlDpiChangeBehavior, NtUserGetOwnerTransformedMonitorRect
 *
 * Analysis facts (win32kfull):
 *  - all of them go through ValidateHwnd; an invalid HWND returns 0 (ERROR_INVALID_WINDOW_HANDLE from ValidateHwndEx).
 *  - Defer/Restore: after ValidateHwnd the window's class FNID (tagWND+0x2a of the shared struct) 0x29d (desktop) and 0x29f (switch) are
 *    filtered out silently (return 0, no error). Otherwise the window's DPI-awareness word (shared tagWND+0x120) low nibble must be 2
 *    (per-monitor), else UserSetLastError(0x57) and 0.
 *  - Enable/IsWindowGDIScaledDpiMessageEnabled act only when (tagWND+0x120 & 0x4000000f) == 0x40000000 (a GDI-scaled unaware window).
 *  - SetDialogControlDpiChangeBehavior: owner process must be the caller (else ERROR_ACCESS_DENIED), then
 *    tagWND+0x130 = (old & ~(mask & 3)) | (values & mask & 3); user32!GetDialogControlDpiChangeBehavior returns that word. */
#include "r2.h"

static HWND mkwin(void) { return k_make_window("r2_dpi"); }
static unsigned short rd16(HWND w, unsigned off, int *ok) {
    unsigned short v = 0; void *p = k_wnd_ptr(w);
    *ok = p && k_try_read_u16((char *)p + off, &v);
    return v;
}
static int unaware_process(void) {
    return U(IsProcessDPIAware) && !U(IsProcessDPIAware)();
}
/* True when the window's monitor runs at 96 dpi, so DPI virtualisation is the identity. +0x11c of the shared tagWND is the monitor dpi
 * (user32!GetDpiForWindow falls back to it); when unreadable the process-wide system dpi decides. */
static int virtualisation_is_identity(HWND w) {
    int ok; unsigned short cur = rd16(w, 0x11c, &ok);
    if (ok && cur) return cur == 96;
    return 0;
}
static unsigned sys_dpi(void) { return UIN("user32.dll", GetDpiForSystem) ? UIN("user32.dll", GetDpiForSystem)() : 96; }

static HWND bad_hwnds[] = { NULL, (HWND)1, (HWND)0x1234, (HWND)(ULONG_PTR)-1, (HWND)(ULONG_PTR)0x7FFFFFFF };
#define NBADW (sizeof bad_hwnds / sizeof bad_hwnds[0])

#define BAD_WINDOW_TEST(tname, nt, extra_call) \
T2(tname) { \
    NEED_NT(nt); NEED_GUI(); \
    for (unsigned i = 0; i < NBADW; i++) { \
        SetLastError(0); \
        BOOL r = extra_call(bad_hwnds[i]); \
        CHECKF(r == 0, "hwnd %p: returned %d", bad_hwnds[i], r); \
        CHECKF(GetLastError() == 1400, "hwnd %p: error %lu want 1400", bad_hwnds[i], GetLastError()); \
    } \
    HWND w = mkwin(); \
    if (w) { U(DestroyWindow)(w); SetLastError(0); BOOL r = extra_call(w); CHECKF(r == 0, "destroyed hwnd returned %d", r); EXPECT_ERR(1400); } \
}
#define CALL_DEFER(h)    UN(NtUserDeferWindowDpiChanges)(h)
#define CALL_RESTORE(h)  UN(NtUserRestoreWindowDpiChanges)(h)
#define CALL_ENABLE(h)   UN(NtUserEnableWindowGDIScaledDpiMessage)(h, TRUE)
#define CALL_ISENAB(h)   UN(NtUserIsWindowGDIScaledDpiMessageEnabled)(h)
#define CALL_DIALOG(h)   UN(NtUserSetDialogControlDpiChangeBehavior)(h, 3, 3)
BAD_WINDOW_TEST(dpi_defer_invalid_hwnd_fails_1400, NtUserDeferWindowDpiChanges, CALL_DEFER)
BAD_WINDOW_TEST(dpi_restore_invalid_hwnd_fails_1400, NtUserRestoreWindowDpiChanges, CALL_RESTORE)
BAD_WINDOW_TEST(dpi_enable_gdiscaled_invalid_hwnd_fails_1400, NtUserEnableWindowGDIScaledDpiMessage, CALL_ENABLE)
BAD_WINDOW_TEST(dpi_is_gdiscaled_invalid_hwnd_fails_1400, NtUserIsWindowGDIScaledDpiMessageEnabled, CALL_ISENAB)
BAD_WINDOW_TEST(dpi_dialog_behavior_invalid_hwnd_fails_1400, NtUserSetDialogControlDpiChangeBehavior, CALL_DIALOG)

T2(dpi_point_for_window_invalid_hwnd_leaves_point_alone) {
    NEED_NT(NtUserLogicalToPhysicalDpiPointForWindow); NEED_NT(NtUserPhysicalToLogicalDpiPointForWindow); NEED_GUI();
    for (unsigned i = 0; i < NBADW; i++) {
        POINT p = { 123, 456 };
        SetLastError(0);
        CHECKF(UN(NtUserLogicalToPhysicalDpiPointForWindow)(bad_hwnds[i], &p) == 0, "l2p hwnd %p", bad_hwnds[i]);
        EXPECT_ERR(1400);
        CHECKF(p.x == 123 && p.y == 456, "l2p modified point for invalid hwnd %p", bad_hwnds[i]);
        SetLastError(0);
        CHECKF(UN(NtUserPhysicalToLogicalDpiPointForWindow)(bad_hwnds[i], &p) == 0, "p2l hwnd %p", bad_hwnds[i]);
        EXPECT_ERR(1400);
        CHECKF(p.x == 123 && p.y == 456, "p2l modified point for invalid hwnd %p", bad_hwnds[i]);
    }
}
T2(dpi_point_for_window_unaware_identity_at_96) {
    NEED_NT(NtUserLogicalToPhysicalDpiPointForWindow); NEED_NT(NtUserPhysicalToLogicalDpiPointForWindow); NEED_GUI();
    HWND w = mkwin();
    if (!w) { t_skip("no window"); return; }
    if (!unaware_process() || !virtualisation_is_identity(w)) { t_note("process aware or window monitor not at 96 dpi: identity expectations not asserted"); U(DestroyWindow)(w); return; }
    static const POINT pts[] = { {0,0}, {1,1}, {100,200}, {-50,70}, {1919,1079}, {-32000,-32000} };
    for (unsigned i = 0; i < sizeof pts / sizeof *pts; i++) {
        POINT p = pts[i];
        BOOL r = UN(NtUserLogicalToPhysicalDpiPointForWindow)(w, &p);
        CHECKF(r == 0 || r == 1, "l2p returned %d", r);
        CHECKF(p.x == pts[i].x && p.y == pts[i].y, "l2p (%ld,%ld) -> (%ld,%ld) at scale 1.0", pts[i].x, pts[i].y, p.x, p.y);
        p = pts[i];
        r = UN(NtUserPhysicalToLogicalDpiPointForWindow)(w, &p);
        CHECKF(r == 0 || r == 1, "p2l returned %d", r);
        CHECKF(p.x == pts[i].x && p.y == pts[i].y, "p2l (%ld,%ld) -> (%ld,%ld) at scale 1.0", pts[i].x, pts[i].y, p.x, p.y);
    }
    U(DestroyWindow)(w);
}
T2(dpi_point_for_window_failure_return_leaves_point_unchanged) {
    NEED_NT(NtUserLogicalToPhysicalDpiPointForWindow); NEED_NT(NtUserPhysicalToLogicalDpiPointForWindow); NEED_GUI();
    HWND w = mkwin();
    if (!w) { t_skip("no window"); return; }
    POINT p = { 321, 654 };
    BOOL r = UN(NtUserLogicalToPhysicalDpiPointForWindow)(w, &p);      /* writes the point back only when the helper returned 1 */
    if (r == 0) CHECK(p.x == 321 && p.y == 654);
    POINT q = { 321, 654 };
    r = UN(NtUserPhysicalToLogicalDpiPointForWindow)(w, &q);
    if (r == 0) CHECK(q.x == 321 && q.y == 654);
    U(DestroyWindow)(w);
}
T2(dpi_defer_restore_on_unaware_window_fail_with_57) {
    NEED_NT(NtUserDeferWindowDpiChanges); NEED_NT(NtUserRestoreWindowDpiChanges); NEED_GUI();
    if (!unaware_process()) { t_skip("process is DPI aware"); return; }
    HWND w = mkwin();
    if (!w) { t_skip("no window"); return; }
    SetLastError(0); CHECK(UN(NtUserDeferWindowDpiChanges)(w) == 0); EXPECT_ERR(0x57);
    SetLastError(0); CHECK(UN(NtUserRestoreWindowDpiChanges)(w) == 0); EXPECT_ERR(0x57);
    int ok; unsigned short deferred = rd16(w, 0x11e, &ok);
    if (ok) CHECKF(deferred == 0, "deferred-DPI word 0x%x set on a window that refused deferral", deferred);
    U(DestroyWindow)(w);
}
T2(dpi_defer_restore_desktop_window_silently_returns_zero) {
    NEED_NT(NtUserDeferWindowDpiChanges); NEED_NT(NtUserRestoreWindowDpiChanges); NEED_GUI();
    HWND d = U(GetDesktopWindow)();
    SetLastError(0x2468); CHECK(UN(NtUserDeferWindowDpiChanges)(d) == 0); EXPECT_ERR(0x2468);       /* FNID_DESKTOP is filtered before any error is raised */
    SetLastError(0x2468); CHECK(UN(NtUserRestoreWindowDpiChanges)(d) == 0); EXPECT_ERR(0x2468);
}
T2(dpi_gdiscaled_message_on_unaware_window_is_off_and_cannot_be_enabled) {
    NEED_NT(NtUserEnableWindowGDIScaledDpiMessage); NEED_NT(NtUserIsWindowGDIScaledDpiMessageEnabled); NEED_GUI();
    if (!unaware_process()) { t_skip("process is DPI aware"); return; }
    HWND w = mkwin();
    if (!w) { t_skip("no window"); return; }
    { int ok; unsigned long c = 0; void *p = k_wnd_ptr(w);
      ok = p && k_try_read_u32((char *)p + 0x120, &c);
      if (ok && (c & 0x4000000f) == 0x40000000) { t_note("window is GDI-scaled (ctx 0x%lx): plain-unaware expectations not asserted", c); U(DestroyWindow)(w); return; } }
    CHECK(UN(NtUserIsWindowGDIScaledDpiMessageEnabled)(w) == 0);
    CHECK(UN(NtUserEnableWindowGDIScaledDpiMessage)(w, TRUE) == 0);        /* (ctx & 0x4000000f) != 0x40000000 for a plain unaware window */
    CHECK(UN(NtUserIsWindowGDIScaledDpiMessageEnabled)(w) == 0);
    CHECK(UN(NtUserEnableWindowGDIScaledDpiMessage)(w, FALSE) == 0);
    int ok; unsigned short lo = rd16(w, 0x120, &ok);
    if (ok) CHECKF((lo & 0xf) == 0, "plain window awareness nibble 0x%x", lo & 0xf);
    U(DestroyWindow)(w);
}
T2(dpi_gdiscaled_message_other_process_window) {
    NEED_NT(NtUserIsWindowGDIScaledDpiMessageEnabled); NEED_GUI();
    HWND d = U(GetDesktopWindow)();
    BOOL r = UN(NtUserIsWindowGDIScaledDpiMessageEnabled)(d);               /* read-only query; the desktop window is never GDI-scaled */
    CHECK(r == 0);
}
T2(dpi_dialog_behavior_sets_and_masks_low_two_bits) {
    NEED_NT(NtUserSetDialogControlDpiChangeBehavior); NEED_GUI();
    typedef USHORT (WINAPI *GDB)(HWND);
    GDB get = (GDB)t_proc_in("user32.dll", "GetDialogControlDpiChangeBehavior");
    HWND w = mkwin();
    if (!w) { t_skip("no window"); return; }
    struct step { USHORT mask, val; int expect_bits; };      /* expect_bits: -1 = unchanged */
    USHORT base = get ? get(w) : 0;
    USHORT hi = base & ~3;
    if (base & 3) t_note("fresh window already has dialog-DPI behaviour bits 0x%x", base & 3);
    static const struct step steps[] = {
        { 3, 3, 3 }, { 1, 0, 2 }, { 2, 0, 0 }, { 1, 1, 1 }, { 0, 3, -1 }, { 2, 2, 3 }, { 0xFFFC, 0xFFFF, -1 }, { 0xFFFF, 0, 0 },
        { 0x0100, 0x0100, -1 }, { 1, 3, 1 }, { 2, 3, 3 }, { 3, 0, 0 },
    };
    int cur = base & 3;
    for (unsigned i = 0; i < sizeof steps / sizeof *steps; i++) {
        SetLastError(0);
        BOOL r = UN(NtUserSetDialogControlDpiChangeBehavior)(w, steps[i].mask, steps[i].val);
        CHECKF(r == TRUE, "step %u: returned %d", i, r);
        if (steps[i].expect_bits >= 0) cur = steps[i].expect_bits;
        if (get) {
            USHORT now = get(w);
            CHECKF((now & 3) == (USHORT)cur, "step %u (mask 0x%x val 0x%x): bits %u want %d", i, steps[i].mask, steps[i].val, now & 3, cur);
            CHECKF((now & ~3) == hi, "step %u: bits outside the mask changed: 0x%x vs 0x%x", i, now & ~3, hi);
        }
        int ok; unsigned short raw = rd16(w, 0x130, &ok);
        if (ok) CHECKF((raw & 3) == (USHORT)cur && (raw & ~3) == hi, "step %u: shared-struct word 0x%x", i, raw);
    }
    U(DestroyWindow)(w);
}
T2(dpi_dialog_behavior_user32_export_forwards_to_syscall) {
    NEED_NT(NtUserSetDialogControlDpiChangeBehavior); NEED_GUI();
    typedef BOOL (WINAPI *SDB)(HWND, DWORD, DWORD);
    typedef USHORT (WINAPI *GDB)(HWND);
    SDB set = (SDB)t_proc_in("user32.dll", "SetDialogControlDpiChangeBehavior");
    GDB get = (GDB)t_proc_in("user32.dll", "GetDialogControlDpiChangeBehavior");
    if (!set || !get) { t_skip("exports missing"); return; }
    HWND w = mkwin();
    if (!w) { t_skip("no window"); return; }
    CHECK(set(w, 3, 2) == TRUE);
    CHECK((get(w) & 3) == 2);
    CHECK(set(w, 1, 1) == TRUE);
    CHECK((get(w) & 3) == 3);
    CHECK(set(w, 3, 0) == TRUE);
    CHECK((get(w) & 3) == 0);
    SetLastError(0); CHECK(set(NULL, 3, 3) == FALSE); EXPECT_ERR(1400);
    CHECK(get(NULL) == 0);
    U(DestroyWindow)(w);
}
T2(dpi_dialog_behavior_foreign_window_denied_57_5) {
    NEED_NT(NtUserSetDialogControlDpiChangeBehavior); NEED_GUI();
    HWND d = U(GetDesktopWindow)();
    DWORD pid = 0; U(GetWindowThreadProcessId)(d, &pid);
    if (pid == GetCurrentProcessId()) { t_skip("desktop window owned by this process"); return; }
    SetLastError(0);
    CHECK(UN(NtUserSetDialogControlDpiChangeBehavior)(d, 3, 3) == 0);         /* ppi of the window != caller's ppi -> ERROR_ACCESS_DENIED */
    EXPECT_ERR(5);
    HWND tray = U(FindWindowA)("Shell_TrayWnd", NULL);
    if (tray) {
        SetLastError(0);
        CHECK(UN(NtUserSetDialogControlDpiChangeBehavior)(tray, 3, 3) == 0);
        EXPECT_ERR(5);
    }
}
T2(dpi_owner_transformed_monitor_rect_invalid_arguments) {
    NEED_NT(NtUserGetOwnerTransformedMonitorRect); NEED_GUI();
    HWND w = mkwin();
    if (!w) { t_skip("no window"); return; }
    HMONITOR m = U(MonitorFromWindow)(w, MONITOR_DEFAULTTOPRIMARY);
    RECT rc = { 11, 22, 33, 44 };
    for (unsigned i = 0; i < NBADW; i++) {
        SetLastError(0);
        CHECKF(UN(NtUserGetOwnerTransformedMonitorRect)(bad_hwnds[i], m, 0, &rc) == 0, "hwnd %p", bad_hwnds[i]);
        EXPECT_ERR(1400);
    }
    HMONITOR badm[] = { NULL, (HMONITOR)1, (HMONITOR)0x1234, (HMONITOR)(ULONG_PTR)-1, (HMONITOR)w };
    for (unsigned i = 0; i < 5; i++) {
        SetLastError(0);
        CHECKF(UN(NtUserGetOwnerTransformedMonitorRect)(w, badm[i], 0, &rc) == 0, "hmonitor %p", badm[i]);
        EXPECT_ERR(0x57);
    }
    SetLastError(0);
    CHECK(UN(NtUserGetOwnerTransformedMonitorRect)(w, m, 0, NULL) == 0);        /* NULL output rect is rejected explicitly */
    EXPECT_ERR(0x57);
    CHECKF(rc.left == 11 && rc.top == 22 && rc.right == 33 && rc.bottom == 44, "rect written by a failing call");
    U(DestroyWindow)(w);
}
T2(dpi_owner_transformed_monitor_rect_matches_monitor_info_at_96) {
    NEED_NT(NtUserGetOwnerTransformedMonitorRect); NEED_GUI();
    HWND w = mkwin();
    if (!w) { t_skip("no window"); return; }
    HMONITOR m = U(MonitorFromWindow)(w, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi; memset(&mi, 0, sizeof mi); mi.cbSize = sizeof mi;
    if (!m || !U(GetMonitorInfoA)(m, &mi)) { t_skip("no monitor info"); U(DestroyWindow)(w); return; }
    RECT full = { -1, -1, -1, -1 }, work = { -1, -1, -1, -1 };
    CHECK(UN(NtUserGetOwnerTransformedMonitorRect)(w, m, 0, &full) == 1);
    CHECK(UN(NtUserGetOwnerTransformedMonitorRect)(w, m, 1, &work) == 1);
    CHECKF(full.right > full.left && full.bottom > full.top, "empty monitor rect");
    CHECKF(work.right > work.left && work.bottom > work.top, "empty work rect");
    CHECKF(work.left >= full.left && work.top >= full.top && work.right <= full.right && work.bottom <= full.bottom, "work rect not inside monitor rect");
    if (unaware_process() && virtualisation_is_identity(w)) {
        CHECKF(!memcmp(&full, &mi.rcMonitor, sizeof full), "monitor rect (%ld,%ld,%ld,%ld) vs GetMonitorInfo (%ld,%ld,%ld,%ld)", full.left, full.top, full.right, full.bottom, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right, mi.rcMonitor.bottom);
        CHECKF(!memcmp(&work, &mi.rcWork, sizeof work), "work rect differs from GetMonitorInfo rcWork");
    } else t_note("DPI virtualisation active: rects not compared with GetMonitorInfo");
    /* any non-zero 'work' selector means work area */
    RECT w2 = { -1, -1, -1, -1 };
    CHECK(UN(NtUserGetOwnerTransformedMonitorRect)(w, m, 0x100, &w2) == 1);
    CHECK(!memcmp(&w2, &work, sizeof w2));
    U(DestroyWindow)(w);
}
T2(dpi_get_dpi_for_window_invalid_is_zero_with_57) {
    NEED_GUI();
    typedef UINT (WINAPI *GDW)(HWND);
    GDW f = (GDW)t_proc_in("user32.dll", "GetDpiForWindow");
    if (!f) { t_skip("GetDpiForWindow missing"); return; }
    for (unsigned i = 0; i < NBADW; i++) {
        SetLastError(0);
        CHECKF(f(bad_hwnds[i]) == 0, "hwnd %p", bad_hwnds[i]);
        EXPECT_ERR(0x57);
    }
}
T2(dpi_window_dpi_words_in_desktop_heap_agree_with_get_dpi_for_window) {
    NEED_GUI();
    typedef UINT (WINAPI *GDW)(HWND);
    GDW f = (GDW)t_proc_in("user32.dll", "GetDpiForWindow");
    HWND w = mkwin();
    if (!f || !w) { t_skip("prerequisites missing"); return; }
    void *p = k_wnd_ptr(w);
    unsigned long ctx = 0; unsigned short cur = 0, def = 0;
    if (!p || !k_try_read_u32((char *)p + 0x120, &ctx) || !k_try_read_u16((char *)p + 0x11c, &cur) || !k_try_read_u16((char *)p + 0x11e, &def)) {
        t_skip("shared window struct not readable"); U(DestroyWindow)(w); return;
    }
    unsigned expect = (ctx >> 8) & 0x1ff;                                      /* user32!GetDpiForWindow: ctx dpi, else +0x11e, else +0x11c */
    if (!expect) expect = def;
    if (!expect) expect = cur;
    CHECKF(f(w) == expect, "GetDpiForWindow %u vs words ctx=0x%lx def=%u cur=%u", f(w), ctx, def, cur);
    if (def != 0) t_note("fresh window has deferred-DPI word %u", def);
    if (cur < 96 || cur > 1000) t_note("monitor DPI word %u outside 96..1000", cur);
    U(DestroyWindow)(w);
}
T2(dpi_context_token_helpers_classify_pseudo_handles) {
    NEED_GUI();
    typedef int (WINAPI *GA)(DPI_AWARENESS_CONTEXT);
    typedef BOOL (WINAPI *IV)(DPI_AWARENESS_CONTEXT);
    typedef BOOL (WINAPI *EQ)(DPI_AWARENESS_CONTEXT, DPI_AWARENESS_CONTEXT);
    GA ga = (GA)t_proc_in("user32.dll", "GetAwarenessFromDpiAwarenessContext");
    IV iv = (IV)t_proc_in("user32.dll", "IsValidDpiAwarenessContext");
    EQ eq = (EQ)t_proc_in("user32.dll", "AreDpiAwarenessContextsEqual");
    if (!ga || !iv || !eq) { t_skip("exports missing"); return; }
    static const struct { LONG_PTR tok; int aw; } tab[] = { {-1, 0}, {-2, 1}, {-3, 2}, {-4, 2}, {-5, 0} };
    for (unsigned i = 0; i < 5; i++) {
        DPI_AWARENESS_CONTEXT c = (DPI_AWARENESS_CONTEXT)tab[i].tok;
        CHECKF(iv(c), "token %ld invalid", (long)tab[i].tok);
        CHECKF(ga(c) == tab[i].aw, "token %ld awareness %d want %d", (long)tab[i].tok, ga(c), tab[i].aw);
        CHECKF(eq(c, c), "token %ld not equal to itself", (long)tab[i].tok);
        if (i < 4) for (unsigned j = 0; j < 4; j++) if (i != j) CHECKF(!eq(c, (DPI_AWARENESS_CONTEXT)tab[j].tok), "tokens %ld/%ld compare equal", (long)tab[i].tok, (long)tab[j].tok);
    }
    static const LONG_PTR badtok[] = { 0, -6, -7, -100 };
    for (unsigned i = 0; i < sizeof badtok / sizeof *badtok; i++) {
        DPI_AWARENESS_CONTEXT c = (DPI_AWARENESS_CONTEXT)badtok[i];
        CHECKF(!iv(c), "token %ld valid", (long)badtok[i]);
        CHECKF(ga(c) == -1, "token %ld awareness %d want -1 (DPI_AWARENESS_INVALID)", (long)badtok[i], ga(c));
    }
}
