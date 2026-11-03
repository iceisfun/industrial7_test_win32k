/* Miscellaneous display/DPI syscalls: stub numbering, NtGdiGetBitmapDpiScaleValue, NtUserGetProcessDpiAwarenessContext handle
 * protocol, NtUserGetDisplayAutoRotationPreferencesByProcessId, NtUserGetPhysicalDeviceRect, NtUserSetDisplayMapping guards,
 * NtUserPaintMonitor guards, NtUserSetMonitorWorkArea (stub identity only - the handler is a bare indirect call that could move
 * a monitor's work area, so it is never invoked).
 *
 * Analysis facts:
 *  - bitmap DPI scale query: surface handle check (type 5); returns the stored scale only for a surface flagged 0x800, else 1.
 *    Bad handle, wrong type, ordinary bitmap -> 1, no error.
 *  - NtUserGetProcessDpiAwarenessContext(h): h == 0 or -1 -> current process; otherwise ObReferenceObjectByHandle(h, 0x1000,
 *    PsProcessType, UserMode) and failure -> UserSetLastError(0x57), return 0; a process without win32k state returns 0.
 *  - NtUserGetDisplayAutoRotationPreferencesByProcessId(pid, &pref, &flags): LockProcessByClientId failure -> 0x57; no W32PROCESS ->
 *    ERROR_ACCESS_DENIED (5); success copies ppi+0x398 and stores 0 into the second output.
 *  - NtUserGetPhysicalDeviceRect(dev, rc): dev == 0 or rc == NULL -> 0x57; ResolveMouseOrPointerDevice failure -> 6.
 *  - NtUserSetDisplayMapping(devinfo, hmonitor): first argument is a TYPE 0x13 (device info) USER handle; a failed UIPI-style
 *    CheckAccess gives 5, a handle of the wrong type gives 6.
 *  - NtUserPaintMonitor(hmonitor, hdc, &rect): ValidateHmonitor failure returns 0 (the rect is read first, unguarded). */
#define R2_DEFINE_GUARD
#include "r2.h"

static const struct { const char *name; unsigned num; } stubs[] = {
    { "NtGdiGetDeviceGammaRamp", 4789 },
    { "NtGdiSetDeviceGammaRamp", 4877 },
    { "NtGdiGetAppliedDeviceGammaRamp", 4773 },
    { "NtGdiGetGammaRampCapability", 4801 },
    { "NtGdiSetPrivateDeviceGammaRamp", 4886 },
    { "NtGdiGetNumberOfPhysicalMonitors", 4809 },
    { "NtGdiGetPhysicalMonitors", 4816 },
    { "NtGdiGetPhysicalMonitorDescription", 4815 },
    { "NtGdiDestroyPhysicalMonitor", 4716 },
    { "NtGdiDDCCISetVCPFeature", 4518 },
    { "NtGdiDDCCIGetVCPFeature", 4516 },
    { "NtGdiDDCCISaveCurrentSettings", 4517 },
    { "NtGdiDDCCIGetCapabilitiesString", 4513 },
    { "NtGdiDDCCIGetCapabilitiesStringLength", 4514 },
    { "NtGdiDDCCIGetTimingReport", 4515 },
    { "NtGdiGetBitmapDpiScaleValue", 4774 },
    { "NtUserLogicalToPhysicalDpiPointForWindow", 5241 },
    { "NtUserPhysicalToLogicalDpiPointForWindow", 5261 },
    { "NtUserDeferWindowDpiChanges", 5058 },
    { "NtUserRestoreWindowDpiChanges", 5304 },
    { "NtUserEnableWindowGDIScaledDpiMessage", 5098 },
    { "NtUserIsWindowGDIScaledDpiMessageEnabled", 5233 },
    { "NtUserSetDialogControlDpiChangeBehavior", 5322 },
    { "NtUserSetProcessDpiAwarenessContext", 5349 },
    { "NtUserGetOwnerTransformedMonitorRect", 4879 },
    { "NtUserGetPhysicalDeviceRect", 5152 },
    { "NtUserSetMonitorWorkArea", 5345 },
    { "NtUserPaintMonitor", 5259 },
    { "NtUserGetDisplayAutoRotationPreferencesByProcessId", 5123 },
    { "NtUserSetDisplayMapping", 5325 },
    { "NtGdiCreateOPMProtectedOutput", 4508 },
    { "NtGdiCreateOPMProtectedOutputs", 4509 },
    { "NtGdiDestroyOPMProtectedOutput", 4715 },
    { "NtGdiGetCertificate", 4777 },
    { "NtGdiGetCertificateSize", 4779 },
    { "NtGdiGetCertificateByHandle", 4778 },
    { "NtGdiGetCertificateSizeByHandle", 4780 },
    { "NtGdiGetOPMRandomNumber", 4811 },
    { "NtGdiSetOPMSigningKeyAndSequenceNumbers", 4883 },
    { "NtGdiConfigureOPMProtectedOutput", 4500 },
    { "NtGdiGetOPMInformation", 4810 },
    { "NtGdiGetCOPPCompatibleOPMInformation", 4776 },
    { "NtGdiGetSuggestedOPMProtectedOutputArraySize", 4824 },
    { "NtUserGetProcessDpiAwarenessContext", 5168 }
};

T2(misc_stub_numbers_match_win10_19041_table) {
    unsigned long b, u; k_win_build(&b, &u);
    int exact = (b == 19041);
    if (!exact) t_note("build %lu: service numbers are the 19041 values; mismatches are reported as notes", b);
    for (unsigned i = 0; i < sizeof stubs / sizeof *stubs; i++) {
        FARPROC p = t_proc_in("win32u.dll", stubs[i].name);
        if (!p) { if (!t_is_wine()) CHECKF(0, "win32u!%s missing", stubs[i].name); continue; }
        if (t_is_wine()) continue;
        CHECKF(k_stub_valid(p), "%s stub shape", stubs[i].name);
        long n = k_stub_number(p);
        if (exact) CHECKF(n == (long)stubs[i].num, "%s: service number %ld want %u", stubs[i].name, n, stubs[i].num);
        else if (n != (long)stubs[i].num) t_note("%s: service number %ld (19041: %u)", stubs[i].name, n, stubs[i].num);
    }
}
T2(misc_set_monitor_work_area_stub_exists_but_is_not_called) {
    FARPROC p = t_proc_in("win32u.dll", "NtUserSetMonitorWorkArea");
    if (!p) { if (t_is_wine()) { t_skip("not exported by this win32u"); return; } CHECK(p != NULL); return; }
    if (!t_is_wine()) { CHECK(k_stub_valid(p)); CHECK(k_stub_number(p) == 5345); }
}

/* ---- NtGdiGetBitmapDpiScaleValue ---- */
T2(misc_bitmap_dpi_scale_defaults_to_one_for_everything) {
    NEED_NT(NtGdiGetBitmapDpiScaleValue); NEED_GUI();
    HDC dc = r2_screen_dc();
    HBITMAP compat = U(CreateCompatibleBitmap)(dc, 8, 8);
    HBITMAP mono = U(CreateBitmap)(4, 4, 1, 1, NULL);
    BITMAPINFO bi; memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = 4; bi.bmiHeader.biHeight = 4; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void *bits = NULL;
    HBITMAP dib = U(CreateDIBSection)(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    HBITMAP dead = U(CreateBitmap)(2, 2, 1, 1, NULL); U(DeleteObject)(dead);
    HDC mem = U(CreateCompatibleDC)(dc);
    HGDIOBJ objs[] = { NULL, (HGDIOBJ)1, (HGDIOBJ)(ULONG_PTR)0x1234, (HGDIOBJ)(ULONG_PTR)-1, U(GetStockObject)(BLACK_PEN), U(GetStockObject)(WHITE_BRUSH),
        U(GetStockObject)(SYSTEM_FONT), (HGDIOBJ)mem, (HGDIOBJ)dc, (HGDIOBJ)dead, (HGDIOBJ)compat, (HGDIOBJ)mono, (HGDIOBJ)dib };
    for (unsigned i = 0; i < sizeof objs / sizeof *objs; i++) {
        ULONG v = UN(NtGdiGetBitmapDpiScaleValue)((HBITMAP)objs[i]);
        CHECKF(v == 1, "object %p -> %lu want 1", objs[i], v);
    }
    U(DeleteDC)(mem);
    if (compat) U(DeleteObject)(compat);
    if (mono) U(DeleteObject)(mono);
    if (dib) U(DeleteObject)(dib);
    r2_release_dc(dc);
}
T2(misc_bitmap_dpi_scale_for_stock_bitmap_in_dc) {
    NEED_NT(NtGdiGetBitmapDpiScaleValue); NEED_GUI();
    HDC dc = r2_screen_dc(), mem = U(CreateCompatibleDC)(dc);
    HBITMAP one = (HBITMAP)U(GetCurrentObject)(mem, OBJ_BITMAP);               /* the 1x1 monochrome default bitmap */
    CHECK(one != NULL);
    CHECK(UN(NtGdiGetBitmapDpiScaleValue)(one) == 1);
    U(DeleteDC)(mem); r2_release_dc(dc);
}

/* ---- NtUserGetProcessDpiAwarenessContext ---- */
T2(misc_process_dpi_context_current_process_spellings_agree) {
    NEED_NT(NtUserGetProcessDpiAwarenessContext); NEED_GUI();
    ULONG a = UN(NtUserGetProcessDpiAwarenessContext)(NULL);
    ULONG b = UN(NtUserGetProcessDpiAwarenessContext)((HANDLE)(ULONG_PTR)-1);
    HANDLE real = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentProcessId());
    ULONG c = real ? UN(NtUserGetProcessDpiAwarenessContext)(real) : a;
    CHECKF(a == b && a == c, "NULL 0x%lx, -1 0x%lx, real handle 0x%lx", a, b, c);
    if (U(IsProcessDPIAware) && !U(IsProcessDPIAware)()) CHECKF((a & 0xf) == 0, "unaware process reports context 0x%lx", a);
    if (real) CloseHandle(real);
}
T2(misc_process_dpi_context_bad_handles_fail_with_57) {
    NEED_NT(NtUserGetProcessDpiAwarenessContext); NEED_GUI();
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    HANDLE weak = OpenProcess(SYNCHRONIZE, FALSE, GetCurrentProcessId());      /* lacks PROCESS_QUERY_LIMITED_INFORMATION (0x1000) */
    HANDLE list[] = { (HANDLE)1, (HANDLE)0x1234, (HANDLE)(ULONG_PTR)0xFFFF0, ev, weak };
    const char *what[] = { "1", "0x1234", "0xFFFF0", "event handle", "SYNCHRONIZE-only process handle" };
    for (unsigned i = 0; i < 5; i++) {
        if (!list[i]) continue;
        SetLastError(0);
        ULONG r = UN(NtUserGetProcessDpiAwarenessContext)(list[i]);
        CHECKF(r == 0, "%s: returned 0x%lx", what[i], r);
        CHECKF(GetLastError() == 0x57, "%s: error %lu want 87", what[i], GetLastError());
    }
    if (ev) CloseHandle(ev);
    if (weak) CloseHandle(weak);
}
CHILD(r2_idle) { Sleep(10000); return 0; }
T2(misc_process_dpi_context_non_gui_process_is_zero_without_error) {
    NEED_NT(NtUserGetProcessDpiAwarenessContext); NEED_GUI();
    if (t_is_wine()) { t_skip("Wine: child process handling differs"); return; }
    PROCESS_INFORMATION pi;
    HANDLE h = t_spawn_child("r2_idle", "", 0, NULL, NULL, NULL, FALSE, NULL, &pi);
    if (!h) { t_skip("cannot spawn child"); return; }
    Sleep(300);
    HANDLE q = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pi.dwProcessId);
    if (q) {
        SetLastError(0x4321);
        ULONG r = UN(NtUserGetProcessDpiAwarenessContext)(q);
        if (r == 0) CHECKF(GetLastError() == 0x4321, "error changed to %lu", GetLastError());   /* no W32PROCESS: neither value nor error is produced */
        else t_note("console child already has win32k process state (context 0x%lx)", r);
        CloseHandle(q);
    }
    TerminateProcess(h, 0); WaitForSingleObject(h, 5000);
    CloseHandle(pi.hThread); CloseHandle(h);
}

/* ---- NtUserGetDisplayAutoRotationPreferencesByProcessId ---- */
T2(misc_autorotation_invalid_pid_fails_with_57_and_leaves_outputs) {
    NEED_NT(NtUserGetDisplayAutoRotationPreferencesByProcessId); NEED_GUI();
    static const DWORD pids[] = { 0, 1, 3, 0xFFFFFFFF, 0x7FFFFFF0, 0xFFFFFFFC };
    for (unsigned i = 0; i < sizeof pids / sizeof *pids; i++) {
        DWORD o = 0x5A5A5A5A, f = 0x5A5A5A5A;
        SetLastError(0);
        BOOL r = UN(NtUserGetDisplayAutoRotationPreferencesByProcessId)(pids[i], &o, &f);
        CHECKF(r == 0, "pid %lu: returned %d", pids[i], r);
        CHECKF(GetLastError() == 0x57, "pid %lu: error %lu want 87", pids[i], GetLastError());
        CHECKF(o == 0x5A5A5A5A && f == 0x5A5A5A5A, "pid %lu: outputs written on failure", pids[i]);
    }
}
T2(misc_autorotation_system_process_has_no_win32k_state) {
    NEED_NT(NtUserGetDisplayAutoRotationPreferencesByProcessId); NEED_GUI();
    DWORD o = 0x5A5A5A5A, f = 0x5A5A5A5A;
    SetLastError(0);
    BOOL r = UN(NtUserGetDisplayAutoRotationPreferencesByProcessId)(4, &o, &f);   /* the System process has no win32k process state */
    CHECK(r == 0);
    EXPECT_ERR(0x57);       /* Windows 10: the same invalid-parameter error as for a pid that does not exist */
    CHECK(o == 0x5A5A5A5A && f == 0x5A5A5A5A);
}
T2(misc_autorotation_own_pid_succeeds_and_matches_user_api) {
    NEED_NT(NtUserGetDisplayAutoRotationPreferencesByProcessId); NEED_GUI();
    U(GetDpiForSystem);
    DWORD o = 0x5A5A5A5A, f = 0x5A5A5A5A;
    BOOL r = UN(NtUserGetDisplayAutoRotationPreferencesByProcessId)(GetCurrentProcessId(), &o, &f);
    CHECKF(r == 1, "own pid returned %d (error %lu)", r, GetLastError());
    if (r != 1) return;
    CHECKF(f == 0, "second output (kernel always stores 0) = %lu", f);
    typedef BOOL (WINAPI *GP)(DWORD *);
    GP gp = (GP)t_proc_in("user32.dll", "GetDisplayAutoRotationPreferences");
    if (gp) { DWORD v = 0x5A5A5A5A; if (gp(&v)) CHECKF(v == o, "GetDisplayAutoRotationPreferences %lu vs by-pid %lu", v, o); }
    t_note("orientation preference word = 0x%lx", o);
}
T2(misc_autorotation_non_gui_child_denied_5) {
    NEED_NT(NtUserGetDisplayAutoRotationPreferencesByProcessId); NEED_GUI();
    if (t_is_wine()) { t_skip("Wine: child process handling differs"); return; }
    PROCESS_INFORMATION pi;
    HANDLE h = t_spawn_child("r2_idle", "", 0, NULL, NULL, NULL, FALSE, NULL, &pi);
    if (!h) { t_skip("cannot spawn child"); return; }
    Sleep(300);
    DWORD o = 0x5A5A5A5A, f = 0x5A5A5A5A;
    SetLastError(0);
    BOOL r = UN(NtUserGetDisplayAutoRotationPreferencesByProcessId)(pi.dwProcessId, &o, &f);
    if (r) t_note("console child already has win32k process state; output 0x%lx", o);
    else { EXPECT_ERR(5); CHECK(o == 0x5A5A5A5A); }
    TerminateProcess(h, 0); WaitForSingleObject(h, 5000);
    CloseHandle(pi.hThread); CloseHandle(h);
}

/* ---- NtUserGetPhysicalDeviceRect ---- */
T2(misc_physical_device_rect_argument_guards) {
    NEED_NT(NtUserGetPhysicalDeviceRect); NEED_GUI();
    RECT rc = { 9, 8, 7, 6 };
    SetLastError(0); CHECK(UN(NtUserGetPhysicalDeviceRect)(NULL, &rc) == 0); EXPECT_ERR(0x57);
    SetLastError(0); CHECK(UN(NtUserGetPhysicalDeviceRect)((HANDLE)0x1234, NULL) == 0); EXPECT_ERR(0x57);
    SetLastError(0); CHECK(UN(NtUserGetPhysicalDeviceRect)(NULL, NULL) == 0); EXPECT_ERR(0x57);
    CHECK(rc.left == 9 && rc.top == 8 && rc.right == 7 && rc.bottom == 6);
}
T2(misc_physical_device_rect_unknown_device_fails_6) {
    NEED_NT(NtUserGetPhysicalDeviceRect); NEED_GUI();
    HANDLE devs[] = { (HANDLE)1, (HANDLE)0x1234, (HANDLE)(ULONG_PTR)-1, (HANDLE)(ULONG_PTR)0xDEADBEEF, (HANDLE)(ULONG_PTR)0x7FFFFFFF };
    for (unsigned i = 0; i < 5; i++) {
        RECT rc = { 9, 8, 7, 6 };
        SetLastError(0);
        CHECKF(UN(NtUserGetPhysicalDeviceRect)(devs[i], &rc) == 0, "device %p", devs[i]);
        EXPECT_ERR(6);
        CHECKF(rc.left == 9 && rc.top == 8 && rc.right == 7 && rc.bottom == 6, "device %p: rect written on failure", devs[i]);
    }
}
T2(misc_physical_device_rect_for_raw_input_devices) {
    NEED_NT(NtUserGetPhysicalDeviceRect); NEED_GUI();
    typedef UINT (WINAPI *GL)(PRAWINPUTDEVICELIST, PUINT, UINT);
    GL gl = (GL)t_proc_in("user32.dll", "GetRawInputDeviceList");
    if (!gl) { t_skip("GetRawInputDeviceList missing"); return; }
    UINT n = 0;
    if (gl(NULL, &n, sizeof(RAWINPUTDEVICELIST)) == (UINT)-1 || n == 0 || n > 64) { t_skip("no raw input devices"); return; }
    RAWINPUTDEVICELIST l[64];
    n = gl(l, &n, sizeof(RAWINPUTDEVICELIST));
    if (n == (UINT)-1) { t_skip("device list unavailable"); return; }
    int mouse = 0;
    for (UINT i = 0; i < n; i++) {
        RECT rc = { 9, 8, 7, 6 };
        SetLastError(0);
        BOOL r = UN(NtUserGetPhysicalDeviceRect)(l[i].hDevice, &rc);
        if (l[i].dwType == RIM_TYPEKEYBOARD) {                                  /* only mouse/pointer devices resolve */
            if (r) t_note("keyboard device %p resolved as pointing device", l[i].hDevice);   /* ResolveMouseOrPointerDevice flag semantics not decoded */
        } else if (l[i].dwType == RIM_TYPEMOUSE) {
            mouse++;
            CHECKF(r == 0 || r == 1, "mouse %p -> %d", l[i].hDevice, r);
            if (r) { if (rc.right <= rc.left || rc.bottom <= rc.top) t_note("mouse %p: empty rect", l[i].hDevice); }
            else { EXPECT_ERR(6); CHECK(rc.left == 9); }
            t_note("mouse %p -> %d (%ld,%ld,%ld,%ld)", l[i].hDevice, r, rc.left, rc.top, rc.right, rc.bottom);
        }
    }
    if (!mouse) t_note("no mouse device in the raw input list");
}

/* ---- NtUserSetDisplayMapping ---- */
T2(misc_set_display_mapping_rejects_non_device_handles) {
    NEED_NT(NtUserSetDisplayMapping); NEED_GUI();
    HWND w = k_make_window("r2_mapping");
    HMONITOR m = U(MonitorFromPoint)((POINT){0, 0}, MONITOR_DEFAULTTOPRIMARY);
    HANDLE bad[] = { NULL, (HANDLE)1, (HANDLE)0x1234, (HANDLE)(ULONG_PTR)-1, (HANDLE)w, (HANDLE)m, (HANDLE)U(GetDesktopWindow)() };
    const char *what[] = { "NULL", "1", "0x1234", "-1", "HWND", "HMONITOR", "desktop HWND" };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        SetLastError(0);
        BOOL r = UN(NtUserSetDisplayMapping)(bad[i], m);
        DWORD e = GetLastError();
        CHECKF(r == 0, "%s: returned %d", what[i], r);
        CHECKF(e == 6 || e == 5, "%s: error %lu (want 6, or 5 when the access check fails first)", what[i], e);
    }
    SetLastError(0);
    CHECK(UN(NtUserSetDisplayMapping)((HANDLE)0x1234, NULL) == 0);
    CHECK(GetLastError() == 6 || GetLastError() == 5);
    if (w) U(DestroyWindow)(w);
}

/* ---- NtUserPaintMonitor ---- */
T2(misc_paint_monitor_invalid_monitor_returns_zero) {
    NEED_NT(NtUserPaintMonitor); NEED_GUI();
    HMONITOR bad[] = { NULL, (HMONITOR)1, (HMONITOR)0x1234, (HMONITOR)(ULONG_PTR)-1, (HMONITOR)U(GetDesktopWindow)() };
    HDC dc = r2_screen_dc();
    for (unsigned i = 0; i < 5; i++) {
        RECT rc = { 0, 0, 16, 16 };
        BOOL r = UN(NtUserPaintMonitor)(bad[i], dc, &rc);
        CHECKF(r == 0, "hmonitor %p: returned %d", bad[i], r);
        r = UN(NtUserPaintMonitor)(bad[i], NULL, &rc);
        CHECKF(r == 0, "hmonitor %p, NULL dc: returned %d", bad[i], r);
        CHECKF(rc.left == 0 && rc.right == 16, "rect modified");
    }
    r2_release_dc(dc);
}
