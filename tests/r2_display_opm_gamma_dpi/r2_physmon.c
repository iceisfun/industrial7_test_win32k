/* Physical monitor syscalls (win32kfull NtGdiGetNumberOfPhysicalMonitors/GetPhysicalMonitors/GetPhysicalMonitorDescription/
 * DestroyPhysicalMonitor/DDCCISetVCPFeature; win32kbase DrvGetDeviceFromNameAndValidateDevice, DrvGetNumberOfPhysicalMonitors,
 * DrvCreatePhysicalMonitorObjects).
 *
 * Facts proven by the analysis:
 *  - GetNumberOfPhysicalMonitors/GetPhysicalMonitors take the display device NAME as a UNICODE_STRING (dxva2 passes MONITORINFOEX.szDevice),
 *    not an HMONITOR. A name no graphics device carries yields 0xC01E05E1; a known but detached device 0xC01E05E2; a mirroring pseudo
 *    device 0xC01E05E3; an attached device without any monitor object 0xC01E05E5.
 *  - GetPhysicalMonitors(name, size, &count, handles): size < number of monitors -> 0xC01E05E6 before anything is allocated.
 *  - GetPhysicalMonitorDescription(h, n, buf): n must be exactly 0x80 (WCHARs) else 0xC000000D, checked before h is looked at.
 *  - the OPM suggested-array-size call is the same DrvGetSuggestedPhysicalMonitorArraySize helper, so it must agree with the count. */
#include "r2.h"

static const WCHAR *bogus_names[] = { L"\\\\.\\DISPLAY99999", L"r2_no_such_device", L"\\\\.\\DISPLAYX", L"\\\\.\\DISPLAYV99", L"\\\\.\\DISPLAY-1" };
#define NBOGUS (sizeof bogus_names / sizeof bogus_names[0])
static HANDLE bogus_handles[] = { NULL, (HANDLE)1, (HANDLE)0x1234, (HANDLE)(ULONG_PTR)-1, (HANDLE)(ULONG_PTR)0xDEADBEEF, (HANDLE)(ULONG_PTR)0x7FFFFFFFFFFF0000ULL };
#define NBOGUSH (sizeof bogus_handles / sizeof bogus_handles[0])

T2(physmon_count_unknown_device_name_status) {
    NEED_NT(NtGdiGetNumberOfPhysicalMonitors);
    for (unsigned i = 0; i < NBOGUS; i++) {
        UNICODE_STRING u; r2_us(&u, bogus_names[i]);
        ULONG cnt = 0x5A5A5A5A;
        LONG st = UN(NtGdiGetNumberOfPhysicalMonitors)(&u, &cnt);
        CHECKF(R2_NTST(st) == R2_ST_NO_DEVICE, "%ls: status 0x%lx", bogus_names[i], R2_NTST(st));
        CHECKF(cnt == 0x5A5A5A5A, "%ls: count written (%lu) although the call failed", bogus_names[i], cnt);
    }
}
T2(physmon_array_unknown_device_name_status) {
    NEED_NT(NtGdiGetPhysicalMonitors);
    for (unsigned i = 0; i < NBOGUS; i++) {
        UNICODE_STRING u; r2_us(&u, bogus_names[i]);
        ULONG cnt = 0x5A5A5A5A; HANDLE h[4] = { (HANDLE)0x77, (HANDLE)0x77, (HANDLE)0x77, (HANDLE)0x77 };
        LONG st = UN(NtGdiGetPhysicalMonitors)(&u, 4, &cnt, h);
        CHECKF(R2_NTST(st) == R2_ST_NO_DEVICE, "%ls: status 0x%lx", bogus_names[i], R2_NTST(st));
        CHECKF(cnt == 0x5A5A5A5A && h[0] == (HANDLE)0x77, "%ls: outputs written on failure", bogus_names[i]);
    }
}
T2(physmon_suggested_opm_array_size_unknown_device_status) {
    NEED_NT(NtGdiGetSuggestedOPMProtectedOutputArraySize);
    for (unsigned i = 0; i < NBOGUS; i++) {
        UNICODE_STRING u; r2_us(&u, bogus_names[i]);
        ULONG cnt = 0x5A5A5A5A;
        LONG st = UN(NtGdiGetSuggestedOPMProtectedOutputArraySize)(&u, &cnt);
        CHECKF(R2_NTST(st) == R2_ST_NO_DEVICE, "%ls: status 0x%lx", bogus_names[i], R2_NTST(st));
        CHECK(cnt == 0x5A5A5A5A);
    }
}
T2(physmon_create_opm_outputs_unknown_device_status) {
    NEED_NT(NtGdiCreateOPMProtectedOutputs);
    for (unsigned i = 0; i < NBOGUS; i++) {
        UNICODE_STRING u; r2_us(&u, bogus_names[i]);
        ULONG cnt = 0x5A5A5A5A; HANDLE h[2] = { (HANDLE)0x77, (HANDLE)0x77 };
        LONG st = UN(NtGdiCreateOPMProtectedOutputs)(&u, 0, 2, &cnt, h);
        CHECKF(R2_NTST(st) == R2_ST_NO_DEVICE, "%ls: status 0x%lx", bogus_names[i], R2_NTST(st));
        CHECK(h[0] == (HANDLE)0x77);
    }
}
T2(physmon_description_wrong_length_is_invalid_parameter_for_any_handle) {
    NEED_NT(NtGdiGetPhysicalMonitorDescription);
    static WCHAR buf[160];
    static const ULONG lens[] = { 0, 1, 0x7F, 0x81, 0x100, 0x7FFFFFFF, 0xFFFFFFFF };
    for (unsigned i = 0; i < NBOGUSH; i++)
        for (unsigned j = 0; j < sizeof lens / sizeof lens[0]; j++) {
            memset(buf, 0xCC, sizeof buf);
            LONG st = UN(NtGdiGetPhysicalMonitorDescription)(bogus_handles[i], lens[j], buf);
            CHECKF(R2_NTST(st) == R2_ST_INVALID_PARAM, "handle %p len 0x%lx: status 0x%lx", bogus_handles[i], lens[j], R2_NTST(st));
            CHECK(buf[0] == 0xCCCC);
        }
}
T2(physmon_description_bogus_handle_fails_with_correct_length) {
    NEED_NT(NtGdiGetPhysicalMonitorDescription);
    static WCHAR buf[160];
    for (unsigned i = 0; i < NBOGUSH; i++) {
        memset(buf, 0xCC, sizeof buf);
        LONG st = UN(NtGdiGetPhysicalMonitorDescription)(bogus_handles[i], 0x80, buf);
        CHECKF(st < 0, "handle %p: status 0x%lx (success on a handle nobody created)", bogus_handles[i], R2_NTST(st));
        CHECKF(buf[0] == 0xCCCC, "handle %p: buffer written on failure", bogus_handles[i]);   /* copy-out only after GetMonitorDescription succeeded */
    }
}
T2(physmon_destroy_bogus_handles_fail) {
    NEED_NT(NtGdiDestroyPhysicalMonitor);
    for (unsigned i = 0; i < NBOGUSH; i++) {
        LONG st = UN(NtGdiDestroyPhysicalMonitor)(bogus_handles[i]);
        CHECKF(st < 0, "handle %p: status 0x%lx", bogus_handles[i], R2_NTST(st));
    }
}
T2(physmon_ddcci_set_vcp_bogus_handles_fail) {
    NEED_NT(NtGdiDDCCISetVCPFeature);
    for (unsigned i = 0; i < NBOGUSH; i++) {
        LONG st = UN(NtGdiDDCCISetVCPFeature)(bogus_handles[i], 0x10, 50);       /* VCP 0x10 = brightness; never reaches a monitor */
        CHECKF(st < 0, "handle %p: status 0x%lx", bogus_handles[i], R2_NTST(st));
        st = UN(NtGdiDDCCISetVCPFeature)(bogus_handles[i], 0, 0);
        CHECKF(st < 0, "handle %p code 0: status 0x%lx", bogus_handles[i], R2_NTST(st));
    }
}

/* ---- real devices ---- */
static const struct { ULONG st; } allowed_dev_status[] = { {0}, {(ULONG)R2_ST_DEV_NOT_ATTACHED}, {(ULONG)R2_ST_DEV_MIRROR}, {(ULONG)R2_ST_NO_MONITORS} };
static int status_allowed(LONG st) {
    for (unsigned i = 0; i < sizeof allowed_dev_status / sizeof *allowed_dev_status; i++) if ((ULONG)st == allowed_dev_status[i].st) return 1;
    return 0;
}
T2(physmon_enumerated_devices_report_only_documented_statuses) {
    NEED_NT(NtGdiGetNumberOfPhysicalMonitors); NEED_GUI();
    struct r2_dev d[16]; int n = r2_enum_devices(d, 16);
    if (!n) { t_skip("no display devices"); return; }
    for (int i = 0; i < n; i++) {
        UNICODE_STRING u; r2_us(&u, d[i].name);
        ULONG cnt = 0xFFFF;
        LONG st = UN(NtGdiGetNumberOfPhysicalMonitors)(&u, &cnt);
        CHECKF(status_allowed(st), "%ls (state 0x%lx): status 0x%lx", d[i].name, d[i].flags, R2_NTST(st));
        if (st == 0) CHECKF(cnt >= 1 && cnt < 64, "%ls: count %lu", d[i].name, cnt);
        else CHECKF(cnt == 0xFFFF, "%ls: count written on failure", d[i].name);
        /* the device state flags (attached, primary, mirroring...) depend on how the guest display came up: info only */
        t_info("%ls state=0x%lx", d[i].name, d[i].flags);
        t_note("%ls -> status 0x%lx count=%lu", d[i].name, R2_NTST(st), cnt);
    }
}
T2(physmon_count_agrees_with_opm_suggested_size_and_is_stable) {
    NEED_NT(NtGdiGetNumberOfPhysicalMonitors); NEED_NT(NtGdiGetSuggestedOPMProtectedOutputArraySize); NEED_GUI();
    struct r2_dev d[16]; int n = r2_enum_devices(d, 16);
    for (int i = 0; i < n; i++) {
        UNICODE_STRING u; r2_us(&u, d[i].name);
        ULONG c1 = 1000, c2 = 2000, c3 = 3000;
        LONG s1 = UN(NtGdiGetNumberOfPhysicalMonitors)(&u, &c1);
        LONG s2 = UN(NtGdiGetSuggestedOPMProtectedOutputArraySize)(&u, &c2);
        LONG s3 = UN(NtGdiGetNumberOfPhysicalMonitors)(&u, &c3);
        CHECKF(s1 == s2 && s1 == s3, "%ls: statuses 0x%lx 0x%lx 0x%lx", d[i].name, R2_NTST(s1), R2_NTST(s2), R2_NTST(s3));
        if (s1 == 0) CHECKF(c1 == c2 && c1 == c3, "%ls: counts %lu %lu %lu", d[i].name, c1, c2, c3);
    }
}
T2(physmon_handles_protocol_create_describe_destroy) {
    NEED_NT(NtGdiGetNumberOfPhysicalMonitors); NEED_NT(NtGdiGetPhysicalMonitors); NEED_NT(NtGdiGetPhysicalMonitorDescription);
    NEED_NT(NtGdiDestroyPhysicalMonitor); NEED_GUI();
    struct r2_dev d[16]; int n = r2_enum_devices(d, 16), tested = 0;
    for (int i = 0; i < n; i++) {
        UNICODE_STRING u; r2_us(&u, d[i].name);
        ULONG cnt = 0;
        if (UN(NtGdiGetNumberOfPhysicalMonitors)(&u, &cnt) != 0 || cnt == 0 || cnt > 16) continue;
        tested++;
        HANDLE h[16]; ULONG got = 0xEEEE;
        /* array smaller than the monitor count is refused before allocation */
        LONG st = UN(NtGdiGetPhysicalMonitors)(&u, cnt - 1, &got, h);
        CHECKF(R2_NTST(st) == R2_ST_ARRAY_TOO_SMALL, "%ls: size %lu-1: status 0x%lx", d[i].name, cnt, R2_NTST(st));
        for (ULONG k = 0; k < 16; k++) h[k] = (HANDLE)0x55;
        got = 0xEEEE;
        st = UN(NtGdiGetPhysicalMonitors)(&u, cnt, &got, h);
        CHECKF(st == 0, "%ls: status 0x%lx", d[i].name, R2_NTST(st));
        if (st) continue;
        CHECKF(got == cnt, "%ls: returned count %lu want %lu", d[i].name, got, cnt);
        for (ULONG a = 0; a < cnt; a++) {
            /* the handle is a small table index (a generic monitor in a VM gets the value 0): written, but not necessarily non-zero */
            CHECKF(h[a] != (HANDLE)0x55 && h[a] != INVALID_HANDLE_VALUE, "%ls: handle[%lu] = %p", d[i].name, a, h[a]);
            for (ULONG b = a + 1; b < cnt; b++) CHECK(h[a] != h[b]);
        }
        CHECKF(h[cnt] == (HANDLE)0x55 || cnt == 16, "%ls: wrote past the returned count", d[i].name);
        for (ULONG a = 0; a < cnt; a++) {
            WCHAR desc[0x80 + 8]; for (int k = 0; k < 0x88; k++) desc[k] = 0xCCCC;
            st = UN(NtGdiGetPhysicalMonitorDescription)(h[a], 0x80, desc);
            CHECKF(st == 0, "%ls[%lu]: description status 0x%lx", d[i].name, a, R2_NTST(st));
            if (st == 0) {
                CHECK(wmemchr(desc, 0, 0x80) != NULL);
                CHECK(desc[0x80] == 0xCCCC);                                     /* exactly 0x100 bytes copied */
                t_note("%ls[%lu] description \"%ls\"", d[i].name, a, desc);
            }
        }
        for (ULONG a = 0; a < cnt; a++) {
            st = UN(NtGdiDestroyPhysicalMonitor)(h[a]);
            CHECKF(st >= 0, "%ls[%lu]: destroy status 0x%lx", d[i].name, a, R2_NTST(st));
            st = UN(NtGdiDestroyPhysicalMonitor)(h[a]);
            CHECKF(st < 0, "%ls[%lu]: second destroy status 0x%lx (handle must be dead)", d[i].name, a, R2_NTST(st));
            WCHAR desc[0x80];
            st = UN(NtGdiGetPhysicalMonitorDescription)(h[a], 0x80, desc);
            CHECKF(st < 0, "%ls[%lu]: description after destroy status 0x%lx", d[i].name, a, R2_NTST(st));
        }
    }
    if (!tested) t_skip("no display device exposes a physical monitor object on this machine");
}
T2(physmon_larger_array_still_reports_actual_count) {
    NEED_NT(NtGdiGetNumberOfPhysicalMonitors); NEED_NT(NtGdiGetPhysicalMonitors); NEED_NT(NtGdiDestroyPhysicalMonitor); NEED_GUI();
    struct r2_dev d[16]; int n = r2_enum_devices(d, 16), tested = 0;
    for (int i = 0; i < n; i++) {
        UNICODE_STRING u; r2_us(&u, d[i].name);
        ULONG cnt = 0;
        if (UN(NtGdiGetNumberOfPhysicalMonitors)(&u, &cnt) != 0 || cnt == 0 || cnt > 8) continue;
        tested++;
        HANDLE h[16]; ULONG got = 0;
        for (int k = 0; k < 16; k++) h[k] = (HANDLE)0x66;
        LONG st = UN(NtGdiGetPhysicalMonitors)(&u, 16, &got, h);
        CHECKF(st == 0 && got == cnt, "%ls: status 0x%lx count %lu want %lu", d[i].name, R2_NTST(st), got, cnt);
        for (ULONG a = 0; a < got && a < 16; a++) UN(NtGdiDestroyPhysicalMonitor)(h[a]);
        for (ULONG a = got; a < 16; a++) CHECKF(h[a] == (HANDLE)0x66, "slot %lu beyond the count was written", a);
    }
    if (!tested) t_skip("no physical monitors");
}
T2(physmon_dxva2_user_path_agrees_with_syscall) {
    NEED_NT(NtGdiGetNumberOfPhysicalMonitors); NEED_GUI();
    HMODULE dx = t_module("dxva2.dll");
    if (!dx) { t_skip("dxva2.dll not available"); return; }
    typedef BOOL (WINAPI *GN)(HMONITOR, DWORD *);
    GN gn = (GN)GetProcAddress(dx, "GetNumberOfPhysicalMonitorsFromHMONITOR");
    if (!gn) { t_skip("dxva2 export missing"); return; }
    HMONITOR pm = U(MonitorFromPoint)((POINT){0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFOEXW mi; memset(&mi, 0, sizeof mi); mi.cbSize = sizeof mi;
    if (!pm || !U(GetMonitorInfoW)(pm, (MONITORINFO *)&mi)) { t_skip("no primary monitor"); return; }
    DWORD viaDx = 0xFFFF; BOOL ok = gn(pm, &viaDx);
    UNICODE_STRING u; r2_us(&u, mi.szDevice);
    ULONG viaNt = 0xFFFF; LONG st = UN(NtGdiGetNumberOfPhysicalMonitors)(&u, &viaNt);
    CHECKF(ok == (st == 0), "dxva2 says %d, syscall status 0x%lx", ok, R2_NTST(st));
    if (ok && st == 0) CHECKF(viaDx == viaNt, "dxva2 count %lu vs syscall %lu", viaDx, viaNt);
}
T2(physmon_dxva2_rejects_invalid_hmonitor) {
    NEED_GUI();
    HMODULE dx = t_module("dxva2.dll");
    if (!dx) { t_skip("dxva2.dll not available"); return; }
    typedef BOOL (WINAPI *GN)(HMONITOR, DWORD *);
    GN gn = (GN)GetProcAddress(dx, "GetNumberOfPhysicalMonitorsFromHMONITOR");
    if (!gn) { t_skip("dxva2 export missing"); return; }
    HMONITOR bad[] = { NULL, (HMONITOR)1, (HMONITOR)0x12345678, (HMONITOR)(ULONG_PTR)-1 };
    for (unsigned i = 0; i < 4; i++) {
        DWORD c = 0x4242;
        SetLastError(0);
        BOOL r = gn(bad[i], &c);
        CHECKF(r == FALSE, "HMONITOR %p accepted", bad[i]);
        CHECKF(GetLastError() != 0, "no last error for HMONITOR %p", bad[i]);
    }
}
