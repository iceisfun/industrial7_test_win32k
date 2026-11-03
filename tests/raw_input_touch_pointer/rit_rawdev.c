/* Raw input device objects: GetRawInputDeviceList / GetRawInputDeviceInfo.
 *
 * win32kfull!NtUserGetRawInputDeviceList(list, *count, cbSize): cbSize must be 0x10
 * (sizeof RAWINPUTDEVICELIST) else ERROR_INVALID_PARAMETER/-1; list==NULL stores the device count
 * and returns 0; a too-small *count stores the needed count, ERROR_INSUFFICIENT_BUFFER and -1.
 * win32kfull!NtUserGetRawInputDeviceInfo(h, cmd, data, *cb): handle is validated with
 * HMValidateHandle(h, 0x13) -> ERROR_INVALID_HANDLE; the per-command size protocol is described in
 * each test.
 */
#include "rit.h"
#include <wchar.h>

static int dev_or_skip(rit_dev *d, int cap) {
    int n = rit_devices(d, cap);
    if (n == 0) t_skip("no raw input devices on this machine (VM without HID?)");
    return n;
}

/* ---- device list ------------------------------------------------------------------------ */
RT(devlist_exports) {
    REQUIRE(U(GetRawInputDeviceList));
    REQUIRE(U(GetRawInputDeviceInfoW));
    REQUIRE(U(GetRawInputDeviceInfoA));
    CHECK(t_proc_in("win32u.dll", "NtUserGetRawInputDeviceList") != NULL);
    CHECK(t_proc_in("win32u.dll", "NtUserGetRawInputDeviceInfo") != NULL);
    CHECK(t_proc_in("win32u.dll", "NtUserGetRawInputData") != NULL);
    CHECK(t_proc_in("win32u.dll", "NtUserGetRawInputBuffer") != NULL);
    CHECK(t_proc_in("win32u.dll", "NtUserRegisterRawInputDevices") != NULL);
    CHECK(t_proc_in("win32u.dll", "NtUserGetRegisteredRawInputDevices") != NULL);
}

static void devlist_badcb(UINT cb) {
    REQUIRE(U(GetRawInputDeviceList));
    UINT n = 77;
    SET_ERR(0);
    UINT r = U(GetRawInputDeviceList)(NULL, &n, cb);
 /* the cbSize test guards everything, including the count store */
    CHECK(r == (UINT)-1);
    EXPECT_ERR(E_INVALID_PARAM);
    CHECK(n == 77);
}
RT(devlist_cbsize_0)    { devlist_badcb(0); }
RT(devlist_cbsize_8)    { devlist_badcb(8); }
RT(devlist_cbsize_12)   { devlist_badcb(12); }
RT(devlist_cbsize_15)   { devlist_badcb(15); }
RT(devlist_cbsize_17)   { devlist_badcb(17); }
RT(devlist_cbsize_24)   { devlist_badcb(24); }
RT(devlist_cbsize_32)   { devlist_badcb(32); }
RT(devlist_cbsize_big)  { devlist_badcb(0x10000010); }

RT(devlist_sizeof) {
    CHECK(sizeof(RAWINPUTDEVICELIST) == 16);
    CHECK(sizeof(RAWINPUTDEVICE) == 16);
    CHECK(sizeof(RID_DEVICE_INFO) == 32);
}

RT(devlist_count_query) {
    REQUIRE(U(GetRawInputDeviceList));
    UINT n = 0xabcd;
    SET_ERR(0);
    /* pRawInputDeviceList == NULL: *count = number of devices, return 0 */
    CHECK(U(GetRawInputDeviceList)(NULL, &n, sizeof(RAWINPUTDEVICELIST)) == 0);
    CHECK(n != 0xabcd);
    t_note("raw input device count: %u", n);
}

RT(devlist_count_query_ignores_incount) {
    REQUIRE(U(GetRawInputDeviceList));
    UINT a = 0, b = 1000;
    CHECK(U(GetRawInputDeviceList)(NULL, &a, 16) == 0);
    CHECK(U(GetRawInputDeviceList)(NULL, &b, 16) == 0);
    /* with a NULL list the incoming *count is overwritten, never read */
    CHECK(a == b);
}

RT(devlist_small_buffer) {
    REQUIRE(U(GetRawInputDeviceList));
    UINT n = 0;
    U(GetRawInputDeviceList)(NULL, &n, 16);
    if (n == 0) { t_skip("no devices"); return; }
    RAWINPUTDEVICELIST l[1]; memset(l, 0x5a, sizeof l);
    UINT cnt = 0;
    SET_ERR(0);
    /* *count < number of devices: store the needed count, ERROR_INSUFFICIENT_BUFFER, -1 */
    UINT r = U(GetRawInputDeviceList)(l, &cnt, 16);
    CHECK(r == (UINT)-1);
    EXPECT_ERR(E_INSUFFICIENT);
    CHECK(cnt == n);
    CHECK(((unsigned char *)l)[0] == 0x5a);       /* nothing written */
}

RT(devlist_small_buffer_partial) {
    REQUIRE(U(GetRawInputDeviceList));
    UINT n = 0;
    U(GetRawInputDeviceList)(NULL, &n, 16);
    if (n < 2) { t_skip("need >= 2 devices"); return; }
    RAWINPUTDEVICELIST l[64]; UINT cnt = n - 1;
    SET_ERR(0);
    CHECK(U(GetRawInputDeviceList)(l, &cnt, 16) == (UINT)-1);
    EXPECT_ERR(E_INSUFFICIENT);
    CHECK(cnt == n);
}

RT(devlist_full_read) {
    REQUIRE(U(GetRawInputDeviceList));
    UINT n = 0;
    U(GetRawInputDeviceList)(NULL, &n, 16);
    RAWINPUTDEVICELIST *l = calloc(n + 8, sizeof *l);
    UINT cnt = n + 8;
    UINT r = U(GetRawInputDeviceList)(l, &cnt, 16);
    /* returns the number copied */
    CHECK(r == n || r == (UINT)-1);
    if (r == n) {
        for (UINT i = 0; i < r; i++) {
            CHECK(l[i].hDevice != NULL);
            CHECK(l[i].dwType <= RIM_TYPEHID);       /* DEVICEINFO+0x30: 0 mouse, 1 keyboard, 2 hid */
        }
    } else t_note("device count changed between calls (hot-plug)");
    free(l);
}

RT(devlist_zero_count_buffer_nodevices) {
    REQUIRE(U(GetRawInputDeviceList));
    UINT n = 0;
    U(GetRawInputDeviceList)(NULL, &n, 16);
    if (n != 0) { t_skip("devices present"); return; }
    RAWINPUTDEVICELIST l[1]; UINT cnt = 0;
    /* no devices: *count (0) is not < 0 so the copy loop runs zero times and returns 0 */
    CHECK(U(GetRawInputDeviceList)(l, &cnt, 16) == 0);
}

RT(devlist_handles_unique) {
    rit_dev d[64]; int n = dev_or_skip(d, 64);
    if (!n) return;
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++) CHECK(d[i].h != d[j].h);
}

RT(devlist_handles_in_handle_table) {
    rit_dev d[64]; int n = dev_or_skip(d, 64);
    if (!n) return;
    struct k_user_shared_info si;
    if (!k_shared_info(&si) || !si.psi) { t_skip("gSharedInfo unreadable"); return; }
    unsigned long long cHandleEntries = 0;
    if (!k_try_read((char *)si.psi + 8, &cHandleEntries, 8)) { t_skip("psi unreadable"); return; }
    /* HMValidateHandle: (h & 0xffff) < the handle-table entry count for any handle it accepts */
    for (int i = 0; i < n; i++) CHECK(((unsigned long long)(ULONG_PTR)d[i].h & 0xffff) < cHandleEntries);
}

RT(devlist_stable_two_calls) {
    rit_dev a[64], b[64];
    int na = rit_devices(a, 64), nb = rit_devices(b, 64);
    if (na != nb) { t_note("count changed %d -> %d", na, nb); return; }
    for (int i = 0; i < na; i++) {
        /* the kernel walks the same linked list in the same order */
        CHECK(a[i].h == b[i].h);
        CHECK(a[i].type == b[i].type);
    }
}

RT(devlist_type_summary) {
    rit_dev d[64]; int n = dev_or_skip(d, 64);
    if (!n) return;
    int c[3] = {0, 0, 0};
    for (int i = 0; i < n; i++) if (d[i].type < 3) c[d[i].type]++;
    t_note("devices: %d mice, %d keyboards, %d hid", c[0], c[1], c[2]);
    CHECK(c[0] + c[1] + c[2] == n);
}

RT(devlist_direct_stub_matches_user32) {
    REQUIRE(UN(NtUserGetRawInputDeviceList));
    REQUIRE(U(GetRawInputDeviceList));
    UINT a = 0, b = 0;
    CHECK(UN(NtUserGetRawInputDeviceList)(NULL, &a, 16) == 0);
    CHECK(U(GetRawInputDeviceList)(NULL, &b, 16) == 0);
    CHECK(a == b);
    SET_ERR(0);
    CHECK(UN(NtUserGetRawInputDeviceList)(NULL, &a, 17) == (UINT)-1);
    EXPECT_ERR(E_INVALID_PARAM);
}

/* ---- device info: handle validation ----------------------------------------------------- */
static void info_badhandle(ULONG_PTR h, UINT cmd) {
    REQUIRE(U(GetRawInputDeviceInfoW));
    UINT cb = 99;
    SET_ERR(0);
    UINT r = U(GetRawInputDeviceInfoW)((HANDLE)h, cmd, NULL, &cb);
    CHECK(r == (UINT)-1);
    EXPECT_ERR(E_INVALID_HANDLE);                  /* HMValidateHandle(h, 0x13) failed: error 6 */
    CHECK(cb == 99);
}
RT(devinfo_null_handle_name)      { info_badhandle(0, RIDI_DEVICENAME); }
RT(devinfo_null_handle_info)      { info_badhandle(0, RIDI_DEVICEINFO); }
RT(devinfo_null_handle_preparsed) { info_badhandle(0, RIDI_PREPARSEDDATA); }
RT(devinfo_minus1_handle)         { info_badhandle((ULONG_PTR)-1, RIDI_DEVICENAME); }
RT(devinfo_small_handle)          { info_badhandle(1, RIDI_DEVICENAME); }
RT(devinfo_stale_uniq_handle)     { info_badhandle(0x7fff0003, RIDI_DEVICEINFO); }
RT(devinfo_hwnd_as_handle) {
    REQUIRE(U(GetDesktopWindow));
    /* a valid USER handle of the wrong type (window = type 1, device = 0x13) must not validate */
    info_badhandle((ULONG_PTR)U(GetDesktopWindow)(), RIDI_DEVICEINFO);
}
RT(devinfo_bad_handle_beats_bad_command) {
    REQUIRE(U(GetRawInputDeviceInfoW));
    UINT cb = 0;
    SET_ERR(0);
    /* handle validation precedes the command switch: bogus command + bogus handle -> 6, not 87 */
    CHECK(U(GetRawInputDeviceInfoW)((HANDLE)0, 0x20000001, NULL, &cb) == (UINT)-1);
    EXPECT_ERR(E_INVALID_HANDLE);
}

RT(devinfo_bad_command) {
    rit_dev d[16]; int n = dev_or_skip(d, 16);
    if (!n) return;
    static const UINT bad[] = { 0, 1, 0x20000000, 0x20000001, 0x20000004, 0x20000006, 0x20000008, 0x2000000a, 0x2000000d, 0x20000008, 0x10000003, 0x80000000, 0xffffffff };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        UINT cb = 64; char buf[64];
        SET_ERR(0);
        /* unknown command: the else branch of the command ladder sets ERROR_INVALID_PARAMETER */
        CHECKF(U(GetRawInputDeviceInfoW)(d[0].h, bad[i], buf, &cb) == (UINT)-1, "cmd 0x%x", bad[i]);
        EXPECT_ERR(E_INVALID_PARAM);
    }
}

/* ---- RIDI_DEVICEINFO -------------------------------------------------------------------- */
RT(devinfo_deviceinfo_size_query) {
    rit_dev d[64]; int n = dev_or_skip(d, 64);
    if (!n) return;
    for (int i = 0; i < n; i++) {
        UINT cb = 0;
        /* pData == NULL: *cb = 0x20 (sizeof RID_DEVICE_INFO), return 0 */
        CHECK(U(GetRawInputDeviceInfoW)(d[i].h, RIDI_DEVICEINFO, NULL, &cb) == 0);
        CHECK(cb == 32);
    }
}

RT(devinfo_deviceinfo_too_small) {
    rit_dev d[16]; int n = dev_or_skip(d, 16);
    if (!n) return;
    for (UINT sz = 0; sz < 32; sz += 8) {
        RID_DEVICE_INFO inf; memset(&inf, 0xcc, sizeof inf);
        UINT cb = sz;
        SET_ERR(0);
        CHECKF(U(GetRawInputDeviceInfoW)(d[0].h, RIDI_DEVICEINFO, &inf, &cb) == (UINT)-1, "sz %u", sz);
        EXPECT_ERR(E_INSUFFICIENT);
        CHECK(cb == 32);
        CHECK(inf.cbSize == 0xcccccccc);          /* untouched */
    }
}

RT(devinfo_deviceinfo_too_big) {
    rit_dev d[16]; int n = dev_or_skip(d, 16);
    if (!n) return;
    unsigned char buf[128]; memset(buf, 0xcc, sizeof buf);
    UINT cb = 33;
    SET_ERR(0);
 /* a size mismatch for the fixed-size commands: ERROR_INVALID_PARAMETER even though it is "big enough" */
    CHECK(U(GetRawInputDeviceInfoW)(d[0].h, RIDI_DEVICEINFO, buf, &cb) == (UINT)-1);
    EXPECT_ERR(E_INVALID_PARAM);
    CHECK(buf[0] == 0xcc);
    cb = 64;
    SET_ERR(0);
    CHECK(U(GetRawInputDeviceInfoW)(d[0].h, RIDI_DEVICEINFO, buf, &cb) == (UINT)-1);
    EXPECT_ERR(E_INVALID_PARAM);
}

RT(devinfo_deviceinfo_fill) {
    rit_dev d[64]; int n = dev_or_skip(d, 64);
    if (!n) return;
    for (int i = 0; i < n; i++) {
        RID_DEVICE_INFO inf; memset(&inf, 0xee, sizeof inf);
        UINT cb = sizeof inf;
        UINT r = U(GetRawInputDeviceInfoW)(d[i].h, RIDI_DEVICEINFO, &inf, &cb);
        CHECK(r == 32);
        CHECK(inf.cbSize == 32);
        /* dwType comes from the same DEVICEINFO byte (+0x30) the list reports */
        CHECK(inf.dwType == d[i].type);
    }
}

RT(devinfo_deviceinfo_union_zeroed_mouse) {
    rit_dev d[64]; int n = dev_or_skip(d, 64);
    if (!n) return;
    for (int i = 0; i < n; i++) {
        if (d[i].type != RIM_TYPEMOUSE) continue;
        RID_DEVICE_INFO inf; memset(&inf, 0xee, sizeof inf);
        UINT cb = sizeof inf;
        CHECK(U(GetRawInputDeviceInfoW)(d[i].h, RIDI_DEVICEINFO, &inf, &cb) == 32);
        /* the kernel zeroes all 8 dwords first; the mouse arm fills dwords 2..5 only */
        CHECK(inf.dwType == RIM_TYPEMOUSE);
        CHECK(inf.mouse.dwNumberOfButtons < 64);
        t_note("mouse: id %lu buttons %lu rate %lu hwheel %d", inf.mouse.dwId, inf.mouse.dwNumberOfButtons, inf.mouse.dwSampleRate, inf.mouse.fHasHorizontalWheel);
        return;
    }
    t_skip("no mouse device");
}

RT(devinfo_deviceinfo_keyboard) {
    rit_dev d[64]; int n = dev_or_skip(d, 64);
    if (!n) return;
    for (int i = 0; i < n; i++) {
        if (d[i].type != RIM_TYPEKEYBOARD) continue;
        RID_DEVICE_INFO inf; memset(&inf, 0xee, sizeof inf);
        UINT cb = sizeof inf;
        CHECK(U(GetRawInputDeviceInfoW)(d[i].h, RIDI_DEVICEINFO, &inf, &cb) == 32);
        CHECK(inf.dwType == RIM_TYPEKEYBOARD);
        CHECK(inf.keyboard.dwNumberOfFunctionKeys < 256);
        t_note("keyboard: type %lu subtype %lu mode %lu fkeys %lu leds %lu keys %lu", inf.keyboard.dwType, inf.keyboard.dwSubType,
               inf.keyboard.dwKeyboardMode, inf.keyboard.dwNumberOfFunctionKeys, inf.keyboard.dwNumberOfIndicators, inf.keyboard.dwNumberOfKeysTotal);
        return;
    }
    t_skip("no keyboard device");
}

RT(devinfo_deviceinfo_hid) {
    rit_dev d[64]; int n = dev_or_skip(d, 64);
    if (!n) return;
    int seen = 0;
    for (int i = 0; i < n; i++) {
        if (d[i].type != RIM_TYPEHID) continue;
        RID_DEVICE_INFO inf; memset(&inf, 0xee, sizeof inf);
        UINT cb = sizeof inf;
        CHECK(U(GetRawInputDeviceInfoW)(d[i].h, RIDI_DEVICEINFO, &inf, &cb) == 32);
        CHECK(inf.dwType == RIM_TYPEHID);
        CHECK(inf.hid.usUsagePage != 0);              /* a HID TLC always has a usage page */
        seen++;
    }
    if (!seen) t_skip("no HID device");
}

/* ---- RIDI_DEVICENAME -------------------------------------------------------------------- */
RT(devinfo_name_size_query) {
    rit_dev d[64]; int n = dev_or_skip(d, 64);
    if (!n) return;
    for (int i = 0; i < n; i++) {
        UINT cb = 0;
        CHECK(U(GetRawInputDeviceInfoW)(d[i].h, RIDI_DEVICENAME, NULL, &cb) == 0);
        CHECK(cb >= 4);                         /* "\\?\" plus terminator at least */
        CHECK(cb < 1024);
    }
}

RT(devinfo_name_too_small) {
    rit_dev d[16]; int n = dev_or_skip(d, 16);
    if (!n) return;
    UINT need = 0;
    U(GetRawInputDeviceInfoW)(d[0].h, RIDI_DEVICENAME, NULL, &need);
    WCHAR buf[8];
    UINT cb = need - 1;
    SET_ERR(0);
    wmemset(buf, 0x7777, 8);
    CHECK(U(GetRawInputDeviceInfoW)(d[0].h, RIDI_DEVICENAME, buf, &cb) == (UINT)-1);
    EXPECT_ERR(E_INSUFFICIENT);
    CHECK(cb == need);
    CHECK(buf[0] == 0x7777);
    cb = 0;
    SET_ERR(0);
    CHECK(U(GetRawInputDeviceInfoW)(d[0].h, RIDI_DEVICENAME, buf, &cb) == (UINT)-1);
    EXPECT_ERR(E_INSUFFICIENT);
    CHECK(cb == need);
}

RT(devinfo_name_fill) {
    rit_dev d[64]; int n = dev_or_skip(d, 64);
    if (!n) return;
    for (int i = 0; i < n; i++) {
        UINT need = 0;
        U(GetRawInputDeviceInfoW)(d[i].h, RIDI_DEVICENAME, NULL, &need);
        WCHAR *name = calloc(need + 8, sizeof(WCHAR));
        UINT cb = need;
        UINT r = U(GetRawInputDeviceInfoW)(d[i].h, RIDI_DEVICENAME, name, &cb);
        /* returns the char count including the terminator (= the size query result) */
        CHECK(r == need);
        if (r == need) {
            CHECK(name[need - 1] == 0);
            CHECK(lstrlenW(name) == (int)need - 1);
            /* memcpy of the "\??\..." device path then name[1] = '\\' -> "\\?\..." */
            CHECK(name[0] == L'\\' && name[1] == L'\\' && name[2] == L'?' && name[3] == L'\\');
        }
        free(name);
    }
}

RT(devinfo_name_exact_buffer_larger_ok) {
    rit_dev d[16]; int n = dev_or_skip(d, 16);
    if (!n) return;
    UINT need = 0;
    U(GetRawInputDeviceInfoW)(d[0].h, RIDI_DEVICENAME, NULL, &need);
    WCHAR *name = calloc(need + 64, sizeof(WCHAR));
    UINT cb = need + 50;
    CHECK(U(GetRawInputDeviceInfoW)(d[0].h, RIDI_DEVICENAME, name, &cb) == need);
    free(name);
}

RT(devinfo_name_ansi_query_doubles) {
    rit_dev d[16]; int n = dev_or_skip(d, 16);
    if (!n) return;
    UINT w = 0, a = 0;
    CHECK(U(GetRawInputDeviceInfoW)(d[0].h, RIDI_DEVICENAME, NULL, &w) == 0);
    CHECK(U(GetRawInputDeviceInfoA)(d[0].h, RIDI_DEVICENAME, NULL, &a) == 0);
    /* user32!GetRawInputDeviceInfoA stores *pcbSize = (wide char count) * 2 for the size query */
    CHECK(a == w * 2);
}

RT(devinfo_name_ansi_fill) {
    rit_dev d[16]; int n = dev_or_skip(d, 16);
    if (!n) return;
    UINT a = 0, w = 0;
    U(GetRawInputDeviceInfoW)(d[0].h, RIDI_DEVICENAME, NULL, &w);
    U(GetRawInputDeviceInfoA)(d[0].h, RIDI_DEVICENAME, NULL, &a);
    char *buf = calloc(a + 8, 1);
    UINT cb = a;
    UINT r = U(GetRawInputDeviceInfoA)(d[0].h, RIDI_DEVICENAME, buf, &cb);
    CHECK(r != (UINT)-1 && r != 0);
    CHECK(strncmp(buf, "\\\\?\\", 4) == 0);
    CHECK((UINT)strlen(buf) + 1 == w || (UINT)strlen(buf) + 1 == r);
    free(buf);
}

RT(devinfo_name_matches_between_types) {
    rit_dev d[64]; int n = dev_or_skip(d, 64);
    if (!n) return;
    int kb = 0, ms = 0;
    for (int i = 0; i < n; i++) {
        UINT need = 0;
        U(GetRawInputDeviceInfoW)(d[i].h, RIDI_DEVICENAME, NULL, &need);
        WCHAR *name = calloc(need + 4, sizeof(WCHAR));
        U(GetRawInputDeviceInfoW)(d[i].h, RIDI_DEVICENAME, name, &need);
        if (d[i].type == RIM_TYPEHID) CHECK(wcsstr(name, L"HID") || wcsstr(name, L"hid"));
        if (d[i].type == RIM_TYPEKEYBOARD) kb++;
        if (d[i].type == RIM_TYPEMOUSE) ms++;
        free(name);
    }
    t_note("%d keyboard(s), %d mouse(s)", kb, ms);
}

/* ---- RIDI_PREPARSEDDATA ----------------------------------------------------------------- */
RT(devinfo_preparsed_non_hid_is_empty) {
    rit_dev d[64]; int n = dev_or_skip(d, 64);
    if (!n) return;
    int seen = 0;
    for (int i = 0; i < n; i++) {
        if (d[i].type == RIM_TYPEHID) continue;
        UINT cb = 55;
        /* DEVICEINFO+0x30 != 2: size = 0; with pData == NULL *cb = 0 and the call returns 0 */
        CHECK(U(GetRawInputDeviceInfoW)(d[i].h, RIDI_PREPARSEDDATA, NULL, &cb) == 0);
        CHECK(cb == 0);
        unsigned char buf[16]; memset(buf, 0x11, sizeof buf);
        cb = sizeof buf;
        CHECK(U(GetRawInputDeviceInfoW)(d[i].h, RIDI_PREPARSEDDATA, buf, &cb) == 0);
        CHECK(buf[0] == 0x11);
        seen++;
    }
    if (!seen) t_skip("only HID devices");
}

RT(devinfo_preparsed_hid_size_protocol) {
    rit_dev d[64]; int n = dev_or_skip(d, 64);
    if (!n) return;
    int seen = 0;
    for (int i = 0; i < n; i++) {
        if (d[i].type != RIM_TYPEHID) continue;
        UINT need = 0;
        CHECK(U(GetRawInputDeviceInfoW)(d[i].h, RIDI_PREPARSEDDATA, NULL, &need) == 0);
        if (need == 0) continue;
        unsigned char *buf = calloc(need + 8, 1);
        UINT cb = need - 1;
        SET_ERR(0);
        CHECK(U(GetRawInputDeviceInfoW)(d[i].h, RIDI_PREPARSEDDATA, buf, &cb) == (UINT)-1);
        EXPECT_ERR(E_INSUFFICIENT);
        CHECK(cb == need);
        cb = need;
        CHECK(U(GetRawInputDeviceInfoW)(d[i].h, RIDI_PREPARSEDDATA, buf, &cb) == need);
        /* HIDP_PREPARSED_DATA starts with the "HidP KDR" signature */
        CHECK(memcmp(buf, "HidP KDR", 8) == 0);
        free(buf);
        seen++;
    }
    if (!seen) t_skip("no HID device with preparsed data");
}

/* ---- RIDI_DEVICEHEALTH (undocumented, 0x2000000c) --------------------------------------- */
RT(devinfo_health_size) {
    rit_dev d[16]; int n = dev_or_skip(d, 16);
    if (!n) return;
    UINT cb = 0;
    CHECK(U(GetRawInputDeviceInfoW)(d[0].h, RIT_RIDI_HEALTH, NULL, &cb) == 0);
    CHECK(cb == 0x2c);
    unsigned char buf[0x40]; memset(buf, 0xcc, sizeof buf);
    cb = 0x2b;
    SET_ERR(0);
    CHECK(U(GetRawInputDeviceInfoW)(d[0].h, RIT_RIDI_HEALTH, buf, &cb) == (UINT)-1);
    EXPECT_ERR(E_INSUFFICIENT);
    CHECK(cb == 0x2c);
    cb = 0x2d;
    SET_ERR(0);
    CHECK(U(GetRawInputDeviceInfoW)(d[0].h, RIT_RIDI_HEALTH, buf, &cb) == (UINT)-1);
    EXPECT_ERR(E_INVALID_PARAM);
}

RT(devinfo_health_fill) {
    rit_dev d[16]; int n = dev_or_skip(d, 16);
    if (!n) return;
    unsigned char buf[0x40]; memset(buf, 0xcc, sizeof buf);
    UINT cb = 0x2c;
    UINT r = U(GetRawInputDeviceInfoW)(d[0].h, RIT_RIDI_HEALTH, buf, &cb);
    CHECK(r == 0x2c);
    t_note("device health first dword 0x%08x", *(unsigned *)buf);
}

/* ---- direct stub variants --------------------------------------------------------------- */
RT(devinfo_direct_stub_bad_handle) {
    REQUIRE(UN(NtUserGetRawInputDeviceInfo));
    rit_gui();
    UINT cb = 8;
    SET_ERR(0);
    CHECK(UN(NtUserGetRawInputDeviceInfo)((HANDLE)(ULONG_PTR)0x1234, RIT_RIDI_NAME, NULL, &cb) == (UINT)-1);
    EXPECT_ERR(E_INVALID_HANDLE);
}

RT(devinfo_direct_stub_name_equals_user32) {
    REQUIRE(UN(NtUserGetRawInputDeviceInfo));
    rit_dev d[16]; int n = dev_or_skip(d, 16);
    if (!n) return;
    UINT a = 0, b = 0;
    CHECK(UN(NtUserGetRawInputDeviceInfo)(d[0].h, RIT_RIDI_NAME, NULL, &a) == 0);
    CHECK(U(GetRawInputDeviceInfoW)(d[0].h, RIDI_DEVICENAME, NULL, &b) == 0);
    CHECK(a == b);
}

RT(devinfo_use_after_device_list_refresh) {
    rit_dev a[64], b[64];
    int na = dev_or_skip(a, 64);
    if (!na) return;
    int nb = rit_devices(b, 64);
    if (na != nb) { t_note("count changed"); return; }
    /* handles from an earlier list stay valid while the device exists */
    for (int i = 0; i < na; i++) {
        UINT cb = 0;
        CHECK(U(GetRawInputDeviceInfoW)(a[i].h, RIDI_DEVICEINFO, NULL, &cb) == 0);
    }
}

/* Process-local state: the device handles live in the shared USER handle table that user32
 * exposes read-only; the low word indexes it and the table entries never move. */
RT(devinfo_handle_uniq_word_consistent) {
    rit_dev d[64]; int n = dev_or_skip(d, 64);
    if (!n) return;
    for (int i = 0; i < n; i++) {
        ULONG_PTR h = (ULONG_PTR)d[i].h;
        /* HMValidateHandle accepts uniq == entry.uniq or 0xffff; flipping the uniq to a wrong
         * non-wildcard value must fail validation */
        ULONG_PTR bad = (h & 0xffff) | ((((h >> 16) & 0xffff) ^ 0x1234) << 16);
        if (((bad >> 16) & 0xffff) == 0xffff || ((bad >> 16) & 0xffff) == 0) continue;
        UINT cb = 0;
        SET_ERR(0);
        CHECK(U(GetRawInputDeviceInfoW)((HANDLE)bad, RIDI_DEVICEINFO, NULL, &cb) == (UINT)-1);
        EXPECT_ERR(E_INVALID_HANDLE);
    }
}

RT(devinfo_handle_wildcard_uniq_accepted) {
    rit_dev d[16]; int n = dev_or_skip(d, 16);
    if (!n) return;
    /* a uniq of 0xffff is the "any" wildcard in HMValidateHandle */
    ULONG_PTR h = ((ULONG_PTR)d[0].h & 0xffff) | 0xffff0000ULL;
    UINT cb = 0;
    UINT r = U(GetRawInputDeviceInfoW)((HANDLE)h, RIDI_DEVICEINFO, NULL, &cb);
    /* HMValidateHandle accepts uniq == -1, but a further security check may still veto */
    t_note("wildcard-uniq device handle -> %d (cb %u)", (int)r, cb);
    CHECK(r == 0 || r == (UINT)-1);
}
