/* Display adapters, monitors-as-devices, display modes and the display DC.
 *
 *  user32!EnumDisplayDevicesW -> win32u!NtUserEnumDisplayDevices(PUNICODE_STRING dev, idx, DISPLAY_DEVICEW*, flags):
 *     the kernel reads cb from the buffer, caps it at 0x348 and fills the
 *     structure in stages (cb thresholds 4, 0x44, 0x144, 0x148, 0x248, 0x348); user32 always passes cb = 0x348 in a
 *     private buffer and copies back only its caller's cb - 4 bytes. Failure is STATUS_UNSUCCESSFUL (0xC0000001).
 *  user32!EnumDisplaySettingsExW -> NtUserEnumDisplaySettings(dev, mode, DEVMODEW*, flags): the kernel requires
 *     dmSize == 0xdc (STATUS_BUFFER_TOO_SMALL otherwise), an unknown device is STATUS_INVALID_PARAMETER_1 and a mode
 *     number past the list STATUS_INVALID_PARAMETER_2; specials: -1 current, -2 registry, -3 preferred.
 *  user32!ChangeDisplaySettingsExW -> NtUserChangeDisplaySettings: hwnd != NULL is rejected in user32 (-5); the
 *     kernel rejects flag bit 27 with ERROR_INVALID_PARAMETER / DISP_CHANGE_FAILED. Nothing here ever applies a mode.
 */
#include "dm.h"

#define DM_MAX_ADAPTERS 64

/* ---- EnumDisplayDevices -------------------------------------------------------------------------- */

static int enum_adapters(DISPLAY_DEVICEW *out, int max) {
    int n = 0;
    for (DWORD i = 0; i < DM_MAX_ADAPTERS && n < max; i++) {
        DISPLAY_DEVICEW dd; memset(&dd, 0, sizeof dd); dd.cb = sizeof dd;
        if (!U(EnumDisplayDevicesW)(NULL, i, &dd, 0)) break;
        out[n++] = dd;
    }
    return n;
}

TEST(display_monitors_dpi_devices_adapter_zero_has_name_and_string) {
    NEED_GUI(); REQUIRE(U(EnumDisplayDevicesW));
    DISPLAY_DEVICEW dd; memset(&dd, 0, sizeof dd); dd.cb = sizeof dd;
    CHECK(sizeof dd == 0x348);
    CHECK(U(EnumDisplayDevicesW)(NULL, 0, &dd, 0) == TRUE);
    CHECK(dd.cb == sizeof dd);
    CHECKF(wcsncmp(dd.DeviceName, L"\\\\.\\DISPLAY", 11) == 0, "DeviceName %ls", dd.DeviceName);
    CHECK(dd.DeviceString[0] != 0);
}

TEST(display_monitors_dpi_devices_enumeration_terminates_with_false_past_the_end) {
    NEED_GUI(); REQUIRE(U(EnumDisplayDevicesW));
    int n = 0; DISPLAY_DEVICEW dd;
    for (DWORD i = 0; i < 256; i++) { memset(&dd, 0, sizeof dd); dd.cb = sizeof dd; if (!U(EnumDisplayDevicesW)(NULL, i, &dd, 0)) break; n++; }
    CHECK(n >= 1 && n < 256);
    memset(&dd, 0, sizeof dd); dd.cb = sizeof dd;
    CHECK(U(EnumDisplayDevicesW)(NULL, (DWORD)n, &dd, 0) == FALSE);           /* index >= device count -> STATUS_UNSUCCESSFUL */
    CHECK(U(EnumDisplayDevicesW)(NULL, 0xffffffffu, &dd, 0) == FALSE);
}

TEST(display_monitors_dpi_devices_one_primary_and_attached_adapter) {
    NEED_GUI(); REQUIRE(U(EnumDisplayDevicesW));
    DISPLAY_DEVICEW a[DM_MAX_ADAPTERS]; int n = enum_adapters(a, DM_MAX_ADAPTERS);
    int prim = 0, att = 0;
    for (int i = 0; i < n; i++) { if (a[i].StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) prim++; if (a[i].StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) att++; }
    CHECKF(prim == 1, "%d primary adapters", prim);
    CHECK(att >= 1);
}

TEST(display_monitors_dpi_devices_adapter_names_are_unique_and_prefixed) {
    NEED_GUI(); REQUIRE(U(EnumDisplayDevicesW));
    DISPLAY_DEVICEW a[DM_MAX_ADAPTERS]; int n = enum_adapters(a, DM_MAX_ADAPTERS);
    for (int i = 0; i < n; i++) {
        CHECK(wcsncmp(a[i].DeviceName, L"\\\\.\\DISPLAY", 11) == 0);
        for (int j = i + 1; j < n; j++) CHECK(wcscmp(a[i].DeviceName, a[j].DeviceName) != 0);
    }
}

TEST(display_monitors_dpi_devices_monitor_names_follow_adapter_name) {
    NEED_GUI(); REQUIRE(U(EnumDisplayDevicesW));
    DISPLAY_DEVICEW a[DM_MAX_ADAPTERS]; int n = enum_adapters(a, DM_MAX_ADAPTERS);
    int checked = 0;
    for (int i = 0; i < n; i++) {
        if (!(a[i].StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)) continue;
        DISPLAY_DEVICEW m; memset(&m, 0, sizeof m); m.cb = sizeof m;
        if (!U(EnumDisplayDevicesW)(a[i].DeviceName, 0, &m, 0)) { t_note("adapter %ls has no monitor 0", a[i].DeviceName); continue; }
        WCHAR want[64]; swprintf(want, 64, L"%ls\\Monitor0", a[i].DeviceName);
        CHECKF(!wcscmp(m.DeviceName, want), "monitor name '%ls' want '%ls'", m.DeviceName, want);   /* swprintf_s(L"%ws\\Monitor%d") in DrvEnumDisplayDevices */
        checked++;
    }
    if (!checked) t_note("no attached adapter exposes a monitor child");
}

TEST(display_monitors_dpi_devices_monitor_index_past_end_fails) {
    NEED_GUI(); REQUIRE(U(EnumDisplayDevicesW));
    DISPLAY_DEVICEW dd; memset(&dd, 0, sizeof dd); dd.cb = sizeof dd;
    DISPLAY_DEVICEW a0; memset(&a0, 0, sizeof a0); a0.cb = sizeof a0;
    REQUIRE(U(EnumDisplayDevicesW)(NULL, 0, &a0, 0));
    CHECK(U(EnumDisplayDevicesW)(a0.DeviceName, 1000, &dd, 0) == FALSE);          /* idx >= *(device+0xd8) child count */
    CHECK(U(EnumDisplayDevicesW)(L"\\\\.\\NOSUCHDISPLAY99", 0, &dd, 0) == FALSE); /* DrvGetDeviceFromName == NULL */
}

TEST(display_monitors_dpi_devices_current_monitors_match_attached_adapters) {
    NEED_GUI(); REQUIRE(U(EnumDisplayDevicesW));
    struct dm_enum e; dm_enum_all(&e, NULL, NULL);
    DISPLAY_DEVICEW a[DM_MAX_ADAPTERS]; int n = enum_adapters(a, DM_MAX_ADAPTERS);
    for (int i = 0; i < e.n; i++) {
        MONITORINFOEXW mi; if (!dm_info(e.m[i].h, &mi)) continue;
        int found = 0;
        for (int j = 0; j < n; j++) if (!wcscmp(a[j].DeviceName, mi.szDevice)) { found = 1; CHECK(a[j].StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP); }
        CHECKF(found, "monitor device '%ls' is not an enumerable adapter", mi.szDevice);
    }
}

TEST(display_monitors_dpi_devices_cb_progressive_fill) {
    NEED_GUI(); REQUIRE(U(EnumDisplayDevicesW));
    /* user32 clears cb-4 bytes, the kernel fills a 0x348-byte private buffer, and only cb-4 bytes come back */
    DISPLAY_DEVICEW full; memset(&full, 0, sizeof full); full.cb = sizeof full;
    REQUIRE(U(EnumDisplayDevicesW)(NULL, 0, &full, 0));
    struct { DWORD cb; BOOL expect_name, expect_string, expect_flags; } rows[] = {
        { 0x44, 1, 0, 0 }, { 0x144, 1, 1, 0 }, { 0x148, 1, 1, 1 },
    };
    for (unsigned r = 0; r < 3; r++) {
        DISPLAY_DEVICEW dd; memset(&dd, 0xcc, sizeof dd); dd.cb = rows[r].cb;
        CHECKF(U(EnumDisplayDevicesW)(NULL, 0, &dd, 0) == TRUE, "cb 0x%lx", rows[r].cb);
        if (rows[r].expect_name) CHECKF(!wcscmp(dd.DeviceName, full.DeviceName), "cb 0x%lx name", rows[r].cb);
        if (rows[r].expect_string) CHECKF(!wcscmp(dd.DeviceString, full.DeviceString), "cb 0x%lx string", rows[r].cb);
        else CHECKF(dd.DeviceString[0] == 0xcccc, "cb 0x%lx: bytes beyond cb must stay untouched", rows[r].cb);
        if (rows[r].expect_flags) CHECKF(dd.StateFlags == full.StateFlags, "cb 0x%lx flags 0x%lx vs 0x%lx", rows[r].cb, dd.StateFlags, full.StateFlags);
        else CHECKF(dd.StateFlags == 0xcccccccc, "cb 0x%lx: StateFlags must stay untouched", rows[r].cb);
    }
}

TEST(display_monitors_dpi_devices_cb_too_small_to_hold_data_succeeds_untouched) {
    NEED_GUI(); REQUIRE(U(EnumDisplayDevicesW));
    DWORD cbs[] = { 0, 1, 4 };
    for (unsigned i = 0; i < 3; i++) {
        DISPLAY_DEVICEW dd; memset(&dd, 0xcc, sizeof dd); dd.cb = cbs[i];
        DISPLAY_DEVICEW ref = dd;
        CHECKF(U(EnumDisplayDevicesW)(NULL, 0, &dd, 0) == TRUE, "cb %lu", cbs[i]);   /* guards both the memset and the copy-back */
        CHECKF(memcmp(&dd, &ref, sizeof dd) == 0, "cb %lu changed the buffer", cbs[i]);
    }
}

TEST(display_monitors_dpi_devices_nt_direct_stage_sizes) {
    NEED_GUI(); NEED_NT(NtUserEnumDisplayDevices);
    /* DrvEnumDisplayDevices writes *buf = highest completed stage <= cb, zero fills the rest of the cb bytes */
    struct { DWORD in_cb, out_cb; } rows[] = { { 0x348, 0x348 }, { 0x400, 0x348 }, { 0x300, 0x248 }, { 0x200, 0x148 }, { 0x146, 0x144 }, { 0x100, 0x44 }, { 0x50, 0x44 }, { 0x10, 4 } };
    for (unsigned r = 0; r < sizeof rows / sizeof *rows; r++) {
        DWORD buf[0x348 / 4 + 4] __attribute__((aligned(8)));
        memset(buf, 0xcc, sizeof buf);
        buf[0] = rows[r].in_cb;
        LONG st = UN(NtUserEnumDisplayDevices)(NULL, 0, (DISPLAY_DEVICEW *)buf, 0);
        CHECKF(st == 0, "in cb 0x%lx status 0x%lx", rows[r].in_cb, (unsigned long)st);
        DWORD used = rows[r].in_cb > 0x348 ? 0x348 : rows[r].in_cb;
        CHECKF(buf[0] == rows[r].out_cb, "in cb 0x%lx -> out cb 0x%lx want 0x%lx", rows[r].in_cb, buf[0], rows[r].out_cb);
        CHECKF(((unsigned char *)buf)[used] == 0xcc && ((unsigned char *)buf)[used + 3] == 0xcc, "bytes past cb 0x%lx were written", used);
    }
}

TEST(display_monitors_dpi_devices_nt_direct_failure_status) {
    NEED_GUI(); NEED_NT(NtUserEnumDisplayDevices);
    DISPLAY_DEVICEW dd; memset(&dd, 0, sizeof dd); dd.cb = sizeof dd;
    CHECK(UN(NtUserEnumDisplayDevices)(NULL, 0xffff, &dd, 0) == STATUS_UNSUCCESSFUL_);
    UNICODE_STRING us; static WCHAR nm[] = L"\\\\.\\NOSUCHDEV";
    us.Buffer = nm; us.Length = sizeof nm - sizeof(WCHAR); us.MaximumLength = sizeof nm;
    memset(&dd, 0, sizeof dd); dd.cb = sizeof dd;
    CHECK(UN(NtUserEnumDisplayDevices)(&us, 0, &dd, 0) == STATUS_UNSUCCESSFUL_);
}

TEST(display_monitors_dpi_devices_nt_direct_matches_user32_adapter_zero) {
    NEED_GUI(); NEED_NT(NtUserEnumDisplayDevices);
    DISPLAY_DEVICEW a, b; memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
    a.cb = b.cb = sizeof a;
    CHECK(U(EnumDisplayDevicesW)(NULL, 0, &a, 0));
    DWORD raw[0x348 / 4 + 2] __attribute__((aligned(8))); memset(raw, 0, sizeof raw); raw[0] = 0x348;
    CHECK(UN(NtUserEnumDisplayDevices)(NULL, 0, (DISPLAY_DEVICEW *)raw, 0) == 0);
    memcpy(&b, raw, sizeof b);
    CHECK(!wcscmp(a.DeviceName, b.DeviceName) && !wcscmp(a.DeviceString, b.DeviceString) && a.StateFlags == b.StateFlags);
    CHECK(!wcscmp(a.DeviceID, b.DeviceID) && !wcscmp(a.DeviceKey, b.DeviceKey));
}

TEST(display_monitors_dpi_devices_device_key_is_a_registry_path) {
    NEED_GUI(); REQUIRE(U(EnumDisplayDevicesW));
    DISPLAY_DEVICEW dd; memset(&dd, 0, sizeof dd); dd.cb = sizeof dd;
    REQUIRE(U(EnumDisplayDevicesW)(NULL, 0, &dd, 0));
    if (dd.DeviceKey[0] == 0) { t_note("empty DeviceKey for adapter 0"); return; }
    CHECKF(wcsncmp(dd.DeviceKey, L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Video", 56) == 0
           || wcsstr(dd.DeviceKey, L"\\Registry\\Machine") == dd.DeviceKey, "DeviceKey %ls", dd.DeviceKey);
}

/* ---- EnumDisplaySettings(Ex) --------------------------------------------------------------------- */

TEST(display_monitors_dpi_settings_current_mode_fields) {
    NEED_GUI(); REQUIRE(U(EnumDisplaySettingsW));
    DEVMODEW dm; CHECK(sizeof dm == 220);
    CHECK(dm_cur_mode(&dm) == TRUE);
    /* Windows 10 returns the size of the mode record it filled in, not the caller's dmSize (0xbc on the basic display driver) */
    CHECKF(dm.dmSize >= 0xa8 && dm.dmSize <= sizeof dm, "dmSize 0x%x", dm.dmSize);
    CHECK(dm.dmPelsWidth > 0 && dm.dmPelsHeight > 0);
    CHECKF(dm.dmBitsPerPel == 8 || dm.dmBitsPerPel == 16 || dm.dmBitsPerPel == 24 || dm.dmBitsPerPel == 32, "bpp %lu", dm.dmBitsPerPel);
    CHECK((dm.dmFields & (DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL)) == (DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL));
    CHECK(dm.dmSpecVersion != 0);
    if (dm.dmBitsPerPel != 32) t_note("current mode is %lu bpp", dm.dmBitsPerPel);
    t_note("current mode %lux%lu@%lu %lubpp fields 0x%lx", dm.dmPelsWidth, dm.dmPelsHeight, dm.dmDisplayFrequency, dm.dmBitsPerPel, dm.dmFields);
}

TEST(display_monitors_dpi_settings_current_mode_has_position_for_primary_at_origin) {
    NEED_GUI(); REQUIRE(U(EnumDisplaySettingsW));
    DEVMODEW dm; REQUIRE(dm_cur_mode(&dm));
    if (!(dm.dmFields & DM_POSITION)) { t_note("current mode lacks DM_POSITION (fields 0x%lx)", dm.dmFields); return; }
    CHECKF(dm.dmPosition.x == 0 && dm.dmPosition.y == 0, "primary position (%ld,%ld)", dm.dmPosition.x, dm.dmPosition.y);
}

TEST(display_monitors_dpi_settings_current_mode_matches_pmv2_primary_monitor_size) {
    NEED_GUI(); REQUIRE(U(EnumDisplaySettingsW) && U(SetThreadDpiAwarenessContext));
    DM_CTX_GUARD();
    dm_set_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    DEVMODEW dm; REQUIRE(dm_cur_mode(&dm));
    MONITORINFO mi = { sizeof mi }; U(GetMonitorInfoW)(dm_primary(), &mi);
    int w = mi.rcMonitor.right - mi.rcMonitor.left, h = mi.rcMonitor.bottom - mi.rcMonitor.top;
    if (dm.dmDisplayOrientation == DMDO_90 || dm.dmDisplayOrientation == DMDO_270) { int t = w; w = h; h = t; }
    if ((int)dm.dmPelsWidth != w || (int)dm.dmPelsHeight != h) t_note("current mode %lux%lu vs primary monitor %dx%d (scaling or rotation)", dm.dmPelsWidth, dm.dmPelsHeight, w, h);
    else CHECK(1);
    CHECK((int)dm.dmPelsWidth > 0);
}

TEST(display_monitors_dpi_settings_mode_list_is_nonempty_and_terminates) {
    NEED_GUI(); REQUIRE(U(EnumDisplaySettingsW));
    int n = 0; DEVMODEW dm;
    for (DWORD i = 0; i < 100000; i++) { dm_devmode_init(&dm); if (!U(EnumDisplaySettingsW)(NULL, i, &dm)) break; n++; CHECK(dm.dmPelsWidth > 0 && dm.dmBitsPerPel > 0); if (n > 5000) break; }
    CHECK(n >= 1 && n <= 5000);
    dm_devmode_init(&dm);
    CHECK(U(EnumDisplaySettingsW)(NULL, (DWORD)n, &dm) == FALSE);
    CHECK(U(EnumDisplaySettingsW)(NULL, 0x10000, &dm) == FALSE);
    t_note("%d display modes", n);
}

TEST(display_monitors_dpi_settings_mode_list_is_deterministic) {
    NEED_GUI(); REQUIRE(U(EnumDisplaySettingsW));
    for (DWORD i = 0; i < 6; i++) {
        DEVMODEW a, b; dm_devmode_init(&a); dm_devmode_init(&b);
        BOOL ra = U(EnumDisplaySettingsW)(NULL, i, &a), rb = U(EnumDisplaySettingsW)(NULL, i, &b);
        CHECK(ra == rb);
        if (ra && rb) CHECK(a.dmPelsWidth == b.dmPelsWidth && a.dmPelsHeight == b.dmPelsHeight && a.dmBitsPerPel == b.dmBitsPerPel && a.dmDisplayFrequency == b.dmDisplayFrequency);
    }
}

TEST(display_monitors_dpi_settings_named_device_equals_default_for_primary) {
    NEED_GUI(); REQUIRE(U(EnumDisplaySettingsW) && U(EnumDisplayDevicesW));
    DISPLAY_DEVICEW a[DM_MAX_ADAPTERS]; int n = enum_adapters(a, DM_MAX_ADAPTERS);
    for (int i = 0; i < n; i++) {
        if (!(a[i].StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE)) continue;
        DEVMODEW d0, d1; dm_devmode_init(&d0); dm_devmode_init(&d1);
        CHECK(U(EnumDisplaySettingsW)(NULL, ENUM_CURRENT_SETTINGS, &d0));
        CHECK(U(EnumDisplaySettingsW)(a[i].DeviceName, ENUM_CURRENT_SETTINGS, &d1));
        CHECK(d0.dmPelsWidth == d1.dmPelsWidth && d0.dmPelsHeight == d1.dmPelsHeight && d0.dmBitsPerPel == d1.dmBitsPerPel);
        CHECK(!wcscmp((WCHAR *)d0.dmDeviceName, (WCHAR *)d1.dmDeviceName));
    }
}

TEST(display_monitors_dpi_settings_device_name_in_devmode_is_adapter_name) {
    NEED_GUI(); REQUIRE(U(EnumDisplaySettingsW));
    DEVMODEW dm; REQUIRE(dm_cur_mode(&dm));
    CHECKF(dm.dmDeviceName[0] != 0, "dmDeviceName empty");
    t_note("dmDeviceName '%ls' driver version 0x%x spec 0x%x", (WCHAR *)dm.dmDeviceName, dm.dmDriverVersion, dm.dmSpecVersion);
}

TEST(display_monitors_dpi_settings_unknown_device_fails) {
    NEED_GUI(); REQUIRE(U(EnumDisplaySettingsW));
    DEVMODEW dm; dm_devmode_init(&dm);
    CHECK(U(EnumDisplaySettingsW)(L"\\\\.\\NOSUCHDISPLAY99", ENUM_CURRENT_SETTINGS, &dm) == FALSE);   /* DrvGetDeviceFromName == NULL -> STATUS_INVALID_PARAMETER_1 */
}

TEST(display_monitors_dpi_settings_raw_and_rotated_flags_widen_the_list) {
    NEED_GUI(); REQUIRE(U(EnumDisplaySettingsExW));
    int c[8] = { 0 };
    DWORD flags[] = { 0, EDS_RAWMODE, EDS_ROTATEDMODE, EDS_RAWMODE | EDS_ROTATEDMODE };
    for (int f = 0; f < 4; f++) {
        DEVMODEW dm;
        for (DWORD i = 0; i < 20000; i++) { dm_devmode_init(&dm); if (!U(EnumDisplaySettingsExW)(NULL, i, &dm, flags[f])) break; c[f]++; }
    }
    t_note("mode counts: default %d raw %d rotated %d both %d", c[0], c[1], c[2], c[3]);
    CHECK(c[1] >= c[0]);          /* the DrvEnumDisplaySettings filter admits entries with (flags & 2) in addition to unflagged ones */
    CHECK(c[2] >= c[0]);          /* (flags & 4) lifts the same-orientation restriction */
    CHECK(c[3] >= c[1] && c[3] >= c[2]);
}

TEST(display_monitors_dpi_settings_small_dmsize_copies_prefix_only) {
    NEED_GUI(); REQUIRE(U(EnumDisplaySettingsW));
    /* Windows 10: the caller's dmSize does not limit the copy; the whole mode record is returned and dmSize reports the
     * record's own size, identical to a full-size request. Nothing beyond that record is written. */
    DEVMODEW ref; CHECK(dm_cur_mode(&ref) == TRUE);
    unsigned char raw[sizeof(DEVMODEW) + 16];
    memset(raw, 0xcc, sizeof raw);
    DEVMODEW *dm = (DEVMODEW *)raw;
    dm->dmSize = 0xc0; dm->dmDriverExtra = 0;
    CHECK(U(EnumDisplaySettingsW)(NULL, ENUM_CURRENT_SETTINGS, dm) == TRUE);
    CHECK(dm->dmSize == ref.dmSize);
    CHECK(dm->dmFields == ref.dmFields);
    CHECK(dm->dmPelsWidth == ref.dmPelsWidth && dm->dmPelsHeight == ref.dmPelsHeight);
    for (unsigned i = ref.dmSize; i < sizeof raw; i++) if (raw[i] != 0xcc) { CHECKF(0, "byte at +0x%x beyond the mode record was written", i); break; }
}

TEST(display_monitors_dpi_settings_dmsize_zero_returns_true_without_copy) {
    NEED_GUI(); REQUIRE(U(EnumDisplaySettingsW));
    unsigned char raw[sizeof(DEVMODEW)];
    DEVMODEW ref; CHECK(dm_cur_mode(&ref) == TRUE);
    memset(raw, 0, sizeof raw);
    DEVMODEW *dm = (DEVMODEW *)raw;
    dm->dmSize = 0;
    /* Windows 10 ignores the zero dmSize: the call succeeds and fills the whole mode record */
    CHECK(U(EnumDisplaySettingsW)(NULL, ENUM_CURRENT_SETTINGS, dm) == TRUE);
    CHECK(dm->dmSize == ref.dmSize);
    CHECK(dm->dmPelsWidth == ref.dmPelsWidth && dm->dmPelsWidth != 0);
}

TEST(display_monitors_dpi_settings_driver_extra_never_exceeds_request) {
    NEED_GUI(); REQUIRE(U(EnumDisplaySettingsW));
    static unsigned char raw[sizeof(DEVMODEW) + 4096];
    WORD reqs[] = { 0, 1, 16, 256, 4096 };
    for (unsigned i = 0; i < 5; i++) {
        memset(raw, 0xcc, sizeof raw);
        DEVMODEW *dm = (DEVMODEW *)raw;
        memset(dm, 0, sizeof *dm); dm->dmSize = sizeof *dm; dm->dmDriverExtra = reqs[i];
        CHECKF(U(EnumDisplaySettingsW)(NULL, ENUM_CURRENT_SETTINGS, dm) == TRUE, "extra %u", reqs[i]);
        CHECKF(dm->dmDriverExtra <= reqs[i], "request %u returned %u", reqs[i], dm->dmDriverExtra);   /* kernel writes back min(request, actual) */
        for (unsigned k = sizeof *dm + dm->dmDriverExtra; k < sizeof *dm + reqs[i]; k++) if (raw[k] != 0xcc) { CHECKF(0, "extra %u: byte +0x%x beyond the returned count changed", reqs[i], k); break; }
    }
}

TEST(display_monitors_dpi_settings_nt_direct_dmsize_must_be_kernel_size) {
    NEED_GUI(); NEED_NT(NtUserEnumDisplaySettings);
    static unsigned char raw[0x200] __attribute__((aligned(8)));
    WORD sizes[] = { 0, 0x44, 0xd4, 0xd8, 0xdb, 0xdd, 0x100, 0xffff };
    for (unsigned i = 0; i < sizeof sizes / sizeof *sizes; i++) {
        memset(raw, 0, sizeof raw);
        *(WORD *)(raw + 0x44) = sizes[i]; *(WORD *)(raw + 0x46) = 0;
        LONG st = UN(NtUserEnumDisplaySettings)(NULL, ENUM_CURRENT_SETTINGS, (DEVMODEW *)raw, 0);
        CHECKF(st == STATUS_BUFFER_TOO_SMALL_, "dmSize 0x%x status 0x%lx", sizes[i], (unsigned long)st);   /* else 0xc0000023 */
    }
}

TEST(display_monitors_dpi_settings_nt_direct_status_codes) {
    NEED_GUI(); NEED_NT(NtUserEnumDisplaySettings);
    static unsigned char raw[0x200] __attribute__((aligned(8)));
    UNICODE_STRING us; static WCHAR nm[] = L"\\\\.\\NOSUCHDEV";
    us.Buffer = nm; us.Length = sizeof nm - sizeof(WCHAR); us.MaximumLength = sizeof nm;
    memset(raw, 0, sizeof raw); *(WORD *)(raw + 0x44) = 0xdc;
    CHECK(UN(NtUserEnumDisplaySettings)(NULL, ENUM_CURRENT_SETTINGS, (DEVMODEW *)raw, 0) == 0);
    memset(raw, 0, sizeof raw); *(WORD *)(raw + 0x44) = 0xdc;
    LONG st = UN(NtUserEnumDisplaySettings)(&us, ENUM_CURRENT_SETTINGS, (DEVMODEW *)raw, 0);
    CHECKF(st == STATUS_INVALID_PARAM_1, "unknown device status 0x%lx", (unsigned long)st);
    memset(raw, 0, sizeof raw); *(WORD *)(raw + 0x44) = 0xdc;
    st = UN(NtUserEnumDisplaySettings)(NULL, 0x10000, (DEVMODEW *)raw, 0);
    CHECKF(st == STATUS_INVALID_PARAM_2, "mode 0x10000 status 0x%lx", (unsigned long)st);
}

TEST(display_monitors_dpi_settings_nt_direct_matches_user32_current_mode) {
    NEED_GUI(); NEED_NT(NtUserEnumDisplaySettings);
    DEVMODEW dm; REQUIRE(dm_cur_mode(&dm));
    static unsigned char raw[0x200] __attribute__((aligned(8)));
    memset(raw, 0, sizeof raw); *(WORD *)(raw + 0x44) = 0xdc;
    CHECK(UN(NtUserEnumDisplaySettings)(NULL, ENUM_CURRENT_SETTINGS, (DEVMODEW *)raw, 0) == 0);
    DEVMODEW *k = (DEVMODEW *)raw;
    CHECK(k->dmPelsWidth == dm.dmPelsWidth && k->dmPelsHeight == dm.dmPelsHeight && k->dmBitsPerPel == dm.dmBitsPerPel);
    CHECK(k->dmDisplayFrequency == dm.dmDisplayFrequency);
    CHECK(k->dmSize == 0xdc || k->dmSize == sizeof(DEVMODEW));
}

TEST(display_monitors_dpi_settings_nt_current_mode_clears_dmfields_bits_25_26) {
    NEED_GUI(); NEED_NT(NtUserEnumDisplaySettings);
    static unsigned char raw[0x200] __attribute__((aligned(8)));
    memset(raw, 0, sizeof raw); *(WORD *)(raw + 0x44) = 0xdc;
    CHECK(UN(NtUserEnumDisplaySettings)(NULL, ENUM_CURRENT_SETTINGS, (DEVMODEW *)raw, 0) == 0);
    DWORD fields = *(DWORD *)(raw + 0x48);
    t_note("kernel dmFields 0x%lx", fields);
    CHECK((*(DWORD *)(raw + 0x48) & 0x06000000) == 0);
}

TEST(display_monitors_dpi_settings_registry_and_preferred_modes_are_wellformed_or_fail_cleanly) {
    NEED_GUI(); REQUIRE(U(EnumDisplaySettingsW));
    DWORD sel[] = { ENUM_REGISTRY_SETTINGS, (DWORD)-3 };
    for (unsigned i = 0; i < 2; i++) {
        DEVMODEW dm; dm_devmode_init(&dm);
        BOOL r = U(EnumDisplaySettingsW)(NULL, sel[i], &dm);
        if (r) { CHECKF(dm.dmPelsWidth > 0 && dm.dmPelsHeight > 0, "mode %ld: %lux%lu", (long)(LONG)sel[i], dm.dmPelsWidth, dm.dmPelsHeight); }
        else t_note("mode %ld not available on this display driver", (long)(LONG)sel[i]);
    }
}

/* ---- ChangeDisplaySettingsEx (test only) --------------------------------------------------------- */

TEST(display_monitors_dpi_change_rejects_window_argument) {
    NEED_GUI(); REQUIRE(U(ChangeDisplaySettingsExW));
    DEVMODEW dm; REQUIRE(dm_cur_mode(&dm));
    HWND w = k_make_window("dm_cds"); REQUIRE(w);
    LONG r = U(ChangeDisplaySettingsExW)(NULL, &dm, w, CDS_TEST, NULL);
    CHECKF(r == DISP_CHANGE_BADPARAM, "got %ld", r);          /* user32: hwnd != NULL -> -5 before any syscall */
    U(DestroyWindow)(w);
}

TEST(display_monitors_dpi_change_reserved_flag_bit27_fails_with_invalid_parameter) {
    NEED_GUI(); NEED_NT(NtUserChangeDisplaySettings);
    SetLastError(0);
    LONG r = UN(NtUserChangeDisplaySettings)(NULL, NULL, 0x08000000, NULL);
    CHECKF(r == DISP_CHANGE_FAILED, "got %ld", r);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);                        /* NtUserChangeDisplaySettings: bit 27 of dwFlags -> UserSetLastError(0x57), -1 */
}

TEST(display_monitors_dpi_change_reserved_flag_bit27_through_user32) {
    NEED_GUI(); REQUIRE(U(ChangeDisplaySettingsExW));
    LONG r = U(ChangeDisplaySettingsExW)(NULL, NULL, NULL, 0x08000000, NULL);
    CHECKF(r == DISP_CHANGE_FAILED, "got %ld", r);
}

TEST(display_monitors_dpi_change_test_flag_with_current_mode_never_applies) {
    NEED_GUI(); REQUIRE(U(ChangeDisplaySettingsExW) && U(GetSystemMetrics));
    DEVMODEW dm; REQUIRE(dm_cur_mode(&dm));
    DEVMODEW before = dm;
    int cx0 = U(GetSystemMetrics)(SM_CXVIRTUALSCREEN), cy0 = U(GetSystemMetrics)(SM_CYVIRTUALSCREEN);
    LONG r = U(ChangeDisplaySettingsExW)(NULL, &dm, NULL, CDS_TEST, NULL);
    CHECKF(r == DISP_CHANGE_SUCCESSFUL || r == DISP_CHANGE_BADMODE || r == DISP_CHANGE_NOTUPDATED || r == DISP_CHANGE_FAILED || r == DISP_CHANGE_RESTART || r == DISP_CHANGE_BADFLAGS || r == DISP_CHANGE_BADDUALVIEW,
           "result %ld is not a DISP_CHANGE_* code", r);
    if (r != DISP_CHANGE_SUCCESSFUL) t_note("CDS_TEST of the current mode returned %ld", r);
    DEVMODEW after; dm_cur_mode(&after);
    CHECK(after.dmPelsWidth == before.dmPelsWidth && after.dmPelsHeight == before.dmPelsHeight && after.dmBitsPerPel == before.dmBitsPerPel);
    CHECK(U(GetSystemMetrics)(SM_CXVIRTUALSCREEN) == cx0 && U(GetSystemMetrics)(SM_CYVIRTUALSCREEN) == cy0);
}

TEST(display_monitors_dpi_change_test_flag_for_every_listed_mode_changes_nothing) {
    NEED_GUI(); REQUIRE(U(ChangeDisplaySettingsExW) && U(EnumDisplaySettingsW));
    DEVMODEW cur; REQUIRE(dm_cur_mode(&cur));
    int tested = 0, ok = 0;
    for (DWORD i = 0; i < 12; i++) {
        DEVMODEW dm; dm_devmode_init(&dm);
        if (!U(EnumDisplaySettingsW)(NULL, i, &dm)) break;
        LONG r = U(ChangeDisplaySettingsExW)(NULL, &dm, NULL, CDS_TEST, NULL);
        tested++; if (r == DISP_CHANGE_SUCCESSFUL) ok++;
        CHECKF(r <= 1 && r >= -6, "mode %lu returned %ld", i, r);
    }
    t_note("%d of %d listed modes pass CDS_TEST", ok, tested);
    DEVMODEW now; dm_cur_mode(&now);
    CHECK(now.dmPelsWidth == cur.dmPelsWidth && now.dmPelsHeight == cur.dmPelsHeight && now.dmBitsPerPel == cur.dmBitsPerPel && now.dmDisplayFrequency == cur.dmDisplayFrequency);
}

/* ---- display DC capabilities --------------------------------------------------------------------- */

static const struct { int idx; const char *name; int val; } fixed_caps[] = {
    { NUMBRUSHES, "NUMBRUSHES", -1 }, { CURVECAPS, "CURVECAPS", 0x1ff }, { LINECAPS, "LINECAPS", 0xfe }, { POLYGONALCAPS, "POLYGONALCAPS", 0xff },
    { CLIPCAPS, "CLIPCAPS", 1 }, { NUMRESERVED, "NUMRESERVED", 20 }, { TECHNOLOGY, "TECHNOLOGY", DT_RASDISPLAY }, { PLANES, "PLANES", 1 },
};

TEST(display_monitors_dpi_caps_screen_dc_fixed_values) {
    NEED_GUI(); REQUIRE(U(GetDC) && U(ReleaseDC) && U(GetDeviceCaps));
    HDC dc = U(GetDC)(NULL); REQUIRE(dc);
    for (unsigned i = 0; i < sizeof fixed_caps / sizeof *fixed_caps; i++)
        CHECKF(U(GetDeviceCaps)(dc, fixed_caps[i].idx) == fixed_caps[i].val, "%s = %d want %d", fixed_caps[i].name, U(GetDeviceCaps)(dc, fixed_caps[i].idx), fixed_caps[i].val);
    U(ReleaseDC)(NULL, dc);
}

TEST(display_monitors_dpi_caps_screen_dc_color_depth) {
    NEED_GUI(); REQUIRE(U(GetDC) && U(ReleaseDC) && U(GetDeviceCaps));
    HDC dc = U(GetDC)(NULL); REQUIRE(dc);
    int bpp = U(GetDeviceCaps)(dc, BITSPIXEL), colres = U(GetDeviceCaps)(dc, COLORRES), nc = U(GetDeviceCaps)(dc, NUMCOLORS);
    DEVMODEW dm; dm_cur_mode(&dm);
    CHECKF(bpp == 8 || bpp == 16 || bpp == 24 || bpp == 32, "BITSPIXEL %d", bpp);
    CHECKF((DWORD)bpp == dm.dmBitsPerPel || (bpp == 16 && dm.dmBitsPerPel == 15), "BITSPIXEL %d vs mode %lu", bpp, dm.dmBitsPerPel);
    if (bpp > 8) CHECKF(nc == -1, "NUMCOLORS %d for %d bpp", nc, bpp);          /* NUMCOLORS is -1 for >8 bpp */
    t_note("BITSPIXEL %d COLORRES %d NUMCOLORS %d SIZEPALETTE %d", bpp, colres, nc, U(GetDeviceCaps)(dc, SIZEPALETTE));
    U(ReleaseDC)(NULL, dc);
}

TEST(display_monitors_dpi_caps_screen_dc_size_matches_system_metrics_same_context) {
    NEED_GUI(); REQUIRE(U(GetDC) && U(ReleaseDC) && U(GetDeviceCaps) && U(SetThreadDpiAwarenessContext));
    DM_CTX_GUARD();
    DPI_AWARENESS_CONTEXT ctxs[] = { DPI_AWARENESS_CONTEXT_UNAWARE, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 };
    for (int k = 0; k < 2; k++) {
        dm_set_ctx(ctxs[k]);
        HDC dc = U(GetDC)(NULL); REQUIRE(dc);
        int hr = U(GetDeviceCaps)(dc, HORZRES), vr = U(GetDeviceCaps)(dc, VERTRES), cx = U(GetSystemMetrics)(SM_CXSCREEN), cy = U(GetSystemMetrics)(SM_CYSCREEN);
        if (k == 1) {
            CHECKF(hr == cx, "ctx %d HORZRES %d SM_CXSCREEN %d", k, hr, cx);
            CHECKF(vr == cy, "ctx %d VERTRES %d SM_CYSCREEN %d", k, vr, cy);
        } else { /* virtualized: both scaled by different rounding paths (EngMulDiv vs user32), allow +-1 */
            CHECKF(hr - cx <= 1 && cx - hr <= 1 && vr - cy <= 1 && cy - vr <= 1, "ctx %d HORZRES %d/%d SM %d/%d", k, hr, vr, cx, cy);
        }
        U(ReleaseDC)(NULL, dc);
    }
}

TEST(display_monitors_dpi_caps_logpixels_unaware_is_96) {
    NEED_GUI(); REQUIRE(U(GetDC) && U(ReleaseDC) && U(GetDeviceCaps) && U(SetThreadDpiAwarenessContext));
    DM_CTX_GUARD();
    dm_set_ctx(DPI_AWARENESS_CONTEXT_UNAWARE);
    HDC dc = U(GetDC)(NULL); REQUIRE(dc);
    CHECK(U(GetDeviceCaps)(dc, LOGPIXELSX) == 96);
    CHECK(U(GetDeviceCaps)(dc, LOGPIXELSY) == 96);
    U(ReleaseDC)(NULL, dc);
}

TEST(display_monitors_dpi_caps_logpixels_system_aware_equals_system_dpi) {
    NEED_GUI(); REQUIRE(U(GetDC) && U(ReleaseDC) && U(GetDeviceCaps) && U(SetThreadDpiAwarenessContext) && U(GetDpiForSystem));
    DM_CTX_GUARD();
    dm_set_ctx(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    HDC dc = U(GetDC)(NULL); REQUIRE(dc);
    UINT sys = U(GetDpiForSystem)();
    CHECKF(U(GetDeviceCaps)(dc, LOGPIXELSX) == (int)sys, "LOGPIXELSX %d GetDpiForSystem %u", U(GetDeviceCaps)(dc, LOGPIXELSX), sys);
    CHECK(U(GetDeviceCaps)(dc, LOGPIXELSY) == U(GetDeviceCaps)(dc, LOGPIXELSX));
    U(ReleaseDC)(NULL, dc);
}

TEST(display_monitors_dpi_caps_logpixels_pmv2_reports_something_sane) {
    NEED_GUI(); REQUIRE(U(GetDC) && U(ReleaseDC) && U(GetDeviceCaps) && U(SetThreadDpiAwarenessContext));
    DM_CTX_GUARD();
    dm_set_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HDC dc = U(GetDC)(NULL); REQUIRE(dc);
    int x = U(GetDeviceCaps)(dc, LOGPIXELSX), y = U(GetDeviceCaps)(dc, LOGPIXELSY);
    CHECK(x >= 96 && y >= 96);
    t_note("PMv2 screen DC LOGPIXELS %d,%d", x, y);
    U(ReleaseDC)(NULL, dc);
}

TEST(display_monitors_dpi_caps_raster_caps_core_bits) {
    NEED_GUI(); REQUIRE(U(GetDC) && U(ReleaseDC) && U(GetDeviceCaps));
    HDC dc = U(GetDC)(NULL); REQUIRE(dc);
    int rc = U(GetDeviceCaps)(dc, RASTERCAPS);
    CHECKF((rc & RC_BITBLT) && (rc & RC_STRETCHBLT) && (rc & RC_DI_BITMAP), "RASTERCAPS 0x%x", rc);
    int bpp = U(GetDeviceCaps)(dc, BITSPIXEL);
    if (bpp > 8) CHECKF((rc & RC_PALETTE) == 0, "RC_PALETTE set on a %d bpp display", bpp);
    else t_note("palettized display (%d bpp), RASTERCAPS 0x%x", bpp, rc);
    U(ReleaseDC)(NULL, dc);
}

TEST(display_monitors_dpi_caps_unknown_index_returns_zero) {
    NEED_GUI(); REQUIRE(U(GetDC) && U(ReleaseDC) && U(GetDeviceCaps));
    HDC dc = U(GetDC)(NULL); REQUIRE(dc);
    int idx[] = { 1, 3, 5, 7, 9, 999, -1, 0x7fff };
    for (unsigned i = 0; i < sizeof idx / sizeof *idx; i++) CHECKF(U(GetDeviceCaps)(dc, idx[i]) == 0, "index %d -> %d", idx[i], U(GetDeviceCaps)(dc, idx[i]));
    U(ReleaseDC)(NULL, dc);
}

TEST(display_monitors_dpi_caps_invalid_dc_sets_invalid_handle) {
    NEED_GUI(); REQUIRE(U(GetDeviceCaps) && U(CreatePen) && U(DeleteObject));
    HGDIOBJ pen = U(CreatePen)(PS_SOLID, 1, 0);
    HDC bad[] = { NULL, (HDC)(ULONG_PTR)0x12345678, (HDC)pen, (HDC)(ULONG_PTR)-1 };
    for (unsigned i = 0; i < 4; i++) {
        SetLastError(0);
        CHECKF(U(GetDeviceCaps)(bad[i], HORZRES) == 0, "hdc %p", (void *)bad[i]);
        EXPECT_ERR(ERROR_INVALID_HANDLE);                    /* pldcGet == 0 -> TEB.LastErrorValue = 6 */
    }
    U(DeleteObject)(pen);
}

TEST(display_monitors_dpi_caps_nt_direct_indices_match_gdi32) {
    NEED_GUI(); NEED_NT(NtGdiGetDeviceCaps); REQUIRE(U(GetDC) && U(ReleaseDC) && U(GetDeviceCaps));
    HDC dc = U(GetDC)(NULL); REQUIRE(dc);
    /* HORZSIZE/VERTSIZE/ASPECT* are plain device fields (no DPI scaling) so they must agree exactly. */
    int fixed[] = { HORZSIZE, VERTSIZE, ASPECTX, ASPECTY, ASPECTXY };
    for (unsigned i = 0; i < sizeof fixed / sizeof *fixed; i++)
        CHECKF(UN(NtGdiGetDeviceCaps)(dc, fixed[i]) == U(GetDeviceCaps)(dc, fixed[i]), "index %d: nt %d gdi32 %d", fixed[i], UN(NtGdiGetDeviceCaps)(dc, fixed[i]), U(GetDeviceCaps)(dc, fixed[i]));
 /* HORZRES/VERTRES/LOGPIXELS go through the thread DPI scaling branch, gdi32 may scale on its own: note only */
    int scaled[] = { HORZRES, VERTRES, LOGPIXELSX, LOGPIXELSY };
    for (unsigned i = 0; i < sizeof scaled / sizeof *scaled; i++) {
        int nt = UN(NtGdiGetDeviceCaps)(dc, scaled[i]), g = U(GetDeviceCaps)(dc, scaled[i]);
        if (nt != g) t_note("DPI scaled index %d: nt %d gdi32 %d", scaled[i], nt, g);
        CHECKF(nt > 0, "index %d: nt %d", scaled[i], nt);
    }
    U(ReleaseDC)(NULL, dc);
}

TEST(display_monitors_dpi_caps_nt_direct_fixed_indices_are_kernel_constants) {
    NEED_GUI(); NEED_NT(NtGdiGetDeviceCaps); REQUIRE(U(GetDC) && U(ReleaseDC));
    HDC dc = U(GetDC)(NULL); REQUIRE(dc);
    CHECK(UN(NtGdiGetDeviceCaps)(dc, CLIPCAPS) == 1);             /* CLIPCAPS is 1 */
    CHECK(UN(NtGdiGetDeviceCaps)(dc, NUMBRUSHES) == -1);          /* 0x10 -> -1 */
    CHECK(UN(NtGdiGetDeviceCaps)(dc, CURVECAPS) == 0x1ff);
    CHECK(UN(NtGdiGetDeviceCaps)(dc, LINECAPS) == 0xfe);
    CHECK(UN(NtGdiGetDeviceCaps)(dc, POLYGONALCAPS) == 0xff);
    CHECK(UN(NtGdiGetDeviceCaps)(dc, NUMRESERVED) == 0x14);
    CHECK(UN(NtGdiGetDeviceCaps)(dc, 20) == 0);                   /* NUMMARKERS */
    CHECK(UN(NtGdiGetDeviceCaps)(dc, 26) == 0);                   /* PDEVICESIZE */
    U(ReleaseDC)(NULL, dc);
}

TEST(display_monitors_dpi_caps_nt_direct_invalid_dc) {
    NEED_GUI(); NEED_NT(NtGdiGetDeviceCaps);
    SetLastError(0);
    CHECK(UN(NtGdiGetDeviceCaps)(NULL, HORZRES) == 0);
    EXPECT_ERR(ERROR_INVALID_HANDLE);                              /* invalid DC handle -> ERROR_INVALID_HANDLE (6) */
    SetLastError(0);
    CHECK(UN(NtGdiGetDeviceCaps)((HDC)(ULONG_PTR)0x5ffff, BITSPIXEL) == 0);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
}

TEST(display_monitors_dpi_caps_shared_devcaps_page_matches_screen_dc) {
    NEED_GUI(); REQUIRE(U(GetDC) && U(ReleaseDC) && U(GetDeviceCaps));
    if (t_is_wine()) { t_skip("Wine has no GDI shared devcaps page"); return; }
    void *base = NULL;
    if (!k_try_read_ptr((char *)k_peb() + PEB_GdiSharedHandleTable, &base) || !base) { t_skip("GdiSharedHandleTable unavailable"); return; }
    int caps[0x40]; memset(caps, 0, sizeof caps);
    if (!k_try_read((char *)base + K_GDI_SHARED_DEVCAPS, caps, sizeof caps)) { t_skip("devcaps page unreadable"); return; }
    HDC dc = U(GetDC)(NULL); REQUIRE(dc);
    /* gdi32!GetDeviceCaps index -> slot of pGdiDevCaps: 0->[0] 2->[1] 0xc->[8] 0xe->[9] 0x18->[0xc] 0x26->[0xd] */
    struct { int idx, slot; } map[] = { { DRIVERVERSION, 0 }, { TECHNOLOGY, 1 }, { BITSPIXEL, 8 }, { PLANES, 9 }, { NUMCOLORS, 0xc }, { RASTERCAPS, 0xd } };
    for (unsigned i = 0; i < sizeof map / sizeof *map; i++)
        CHECKF(caps[map[i].slot] == U(GetDeviceCaps)(dc, map[i].idx), "index %d: shared slot %d = %d, GetDeviceCaps = %d", map[i].idx, map[i].slot, caps[map[i].slot], U(GetDeviceCaps)(dc, map[i].idx));
    U(ReleaseDC)(NULL, dc);
}

TEST(display_monitors_dpi_caps_memory_dc_inherits_display_caps) {
    NEED_GUI(); REQUIRE(U(CreateCompatibleDC) && U(DeleteDC) && U(GetDC) && U(ReleaseDC) && U(GetDeviceCaps));
    HDC scr = U(GetDC)(NULL), mem = U(CreateCompatibleDC)(scr); REQUIRE(scr && mem);
    CHECK(U(GetDeviceCaps)(mem, TECHNOLOGY) == DT_RASDISPLAY);       /* compatible DC inherits the display PDEV */
    CHECK(U(GetDeviceCaps)(mem, BITSPIXEL) == U(GetDeviceCaps)(scr, BITSPIXEL));
    U(DeleteDC)(mem); U(ReleaseDC)(NULL, scr);
}

TEST(display_monitors_dpi_caps_create_dc_display_matches_screen_dc) {
    NEED_GUI(); REQUIRE(U(CreateDCW) && U(DeleteDC) && U(GetDC) && U(ReleaseDC) && U(GetDeviceCaps));
    HDC d = U(CreateDCW)(L"DISPLAY", NULL, NULL, NULL), scr = U(GetDC)(NULL);
    REQUIRE(d && scr);
    int idx[] = { BITSPIXEL, PLANES, TECHNOLOGY, HORZRES, VERTRES };
    for (unsigned i = 0; i < 5; i++) CHECKF(U(GetDeviceCaps)(d, idx[i]) == U(GetDeviceCaps)(scr, idx[i]), "index %d", idx[i]);
    U(DeleteDC)(d); U(ReleaseDC)(NULL, scr);
}

TEST(display_monitors_dpi_caps_create_dc_named_adapter_has_matching_mode) {
    NEED_GUI(); REQUIRE(U(CreateDCW) && U(DeleteDC) && U(EnumDisplayDevicesW) && U(GetDeviceCaps));
    DISPLAY_DEVICEW a[DM_MAX_ADAPTERS]; int n = enum_adapters(a, DM_MAX_ADAPTERS);
    for (int i = 0; i < n; i++) {
        if (!(a[i].StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)) continue;
        HDC d = U(CreateDCW)(a[i].DeviceName, NULL, NULL, NULL);
        if (!d) { t_note("CreateDC(%ls) failed %lu", a[i].DeviceName, GetLastError()); continue; }
        DEVMODEW dm; dm_devmode_init(&dm); U(EnumDisplaySettingsW)(a[i].DeviceName, ENUM_CURRENT_SETTINGS, &dm);
        CHECKF((DWORD)U(GetDeviceCaps)(d, BITSPIXEL) == dm.dmBitsPerPel || dm.dmBitsPerPel == 15, "%ls bpp %d vs %lu", a[i].DeviceName, U(GetDeviceCaps)(d, BITSPIXEL), dm.dmBitsPerPel);
        U(DeleteDC)(d);
    }
}
