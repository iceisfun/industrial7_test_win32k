/* RegisterRawInputDevices / GetRegisteredRawInputDevices.
 *
 * win32kfull!NtUserRegisterRawInputDevices(devs, count, cbSize): devs != NULL, count != 0 and
 * cbSize == 0x10, else ERROR_INVALID_PARAMETER. Every element is first run through
 * HidRequestValidityCheck (all elements are checked before any is applied), whose branches:
 *   dwFlags & 0xffffc0ce         -> ERROR_INVALID_FLAGS (1004)
 *   usUsagePage == 0             -> 87
 *   RIDEV_CAPTUREMOUSE(0x200)    -> only (1,2)+NOLEGACY+owned hwnd, or (1,6); else 1004
 *   RIDEV_APPKEYS(0x400)         -> only (1,6)+NOLEGACY; else 1004
 *   RIDEV_REMOVE + EXCLUDE/NOLEGACY -> 1004; REMOVE + hwnd -> 87
 *   mode 0 needs usage != 0; PAGEONLY(0x20) needs usage == 0; EXCLUDE(0x10) needs usage != 0 and
 *   no hwnd / sink; NOLEGACY(0x30) only for the legacy devices (1,2) and (1,6)
 *   hwnd: must validate and belong to the calling process, else 87
 * Registrations are process state: every case restores it.
 */
#include "rit.h"

enum { HW_NONE, HW_OWN, HW_BOGUS, HW_DESKTOP, HW_OTHERTHREAD };
#define VP 0xFF00   /* vendor-defined usage page: never a legacy device */

static rit_owner g_other;

static void rr_run(USHORT page, USHORT usage, DWORD flags, int hw, int want_ok, DWORD want_err) {
    REQUIRE(U(RegisterRawInputDevices));
    REQUIRE(U(GetRegisteredRawInputDevices));
    rit_unreg_all();
    HWND h = NULL;
    HWND own = NULL;
    int other = 0;
    if (hw == HW_OWN) { own = k_make_window("rr"); h = own; if (!h) { t_skip("no window"); return; } }
    else if (hw == HW_BOGUS) h = (HWND)(ULONG_PTR)0x7fff1234;
    else if (hw == HW_DESKTOP) h = U(GetDesktopWindow)();
    else if (hw == HW_OTHERTHREAD) { if (!rit_owner_start(&g_other)) { t_skip("no owner thread"); return; } other = 1; h = g_other.hwnd; }
    UINT before = rit_reg_count();
    RAWINPUTDEVICE d = { page, usage, flags, h };
    SET_ERR(0);
    BOOL r = U(RegisterRawInputDevices)(&d, 1, sizeof d);
    DWORD e = GetLastError();
    if (want_ok) {
        CHECKF(r, "flags 0x%lx page 0x%x usage 0x%x: failed, err %lu", flags, page, usage, e);
        if (r && !(flags & RIDEV_REMOVE)) {
            /* the entry is visible through the registered list */
            CHECK(rit_reg_count() == before + 1);
            CHECK(rit_reg_find(page, usage, NULL));
        }
    } else {
        CHECKF(!r, "flags 0x%lx page 0x%x usage 0x%x unexpectedly succeeded", flags, page, usage);
        CHECKF(e == want_err, "flags 0x%lx: err %lu want %lu", flags, e, want_err);
        CHECK(rit_reg_count() == before);          /* nothing registered by a failing call */
    }
    rit_unreg_all();
    if (own) U(DestroyWindow)(own);
    if (other) rit_owner_stop(&g_other);
}

#define RR(name, page, usage, flags, hw, ok, err) RT(reg_##name) { rr_run(page, usage, flags, hw, ok, err); }

/* ---- accepted combinations -------------------------------------------------------------- */
RR(ok_vendor_plain,        VP, 1, 0,                          HW_NONE, 1, 0)
RR(ok_vendor_usage_ffff,   VP, 0xffff, 0,                     HW_NONE, 1, 0)
RR(ok_page_ffff,           0xffff, 1, 0,                      HW_NONE, 1, 0)
RR(ok_vendor_hwnd,         VP, 1, 0,                          HW_OWN,  1, 0)
RR(ok_vendor_sink_hwnd,    VP, 1, RIDEV_INPUTSINK,            HW_OWN,  1, 0)
RR(ok_vendor_exsink_hwnd,  VP, 1, RIDEV_EXINPUTSINK,          HW_OWN,  1, 0)
RR(ok_vendor_devnotify,    VP, 1, RIDEV_DEVNOTIFY,            HW_NONE, 1, 0)
RR(ok_vendor_exclude,      VP, 1, RIDEV_EXCLUDE,              HW_NONE, 1, 0)
RR(ok_pageonly,            VP, 0, RIDEV_PAGEONLY,             HW_NONE, 1, 0)
RR(ok_pageonly_hwnd,       VP, 0, RIDEV_PAGEONLY,             HW_OWN,  1, 0)
RR(ok_pageonly_sink_hwnd,  VP, 0, RIDEV_PAGEONLY | RIDEV_INPUTSINK, HW_OWN, 1, 0)
RR(ok_hwnd_other_thread,   VP, 1, RIDEV_INPUTSINK,            HW_OTHERTHREAD, 1, 0)  /* same process, different thread: ppi matches */
RR(ok_mouse,               1, 2, 0,                           HW_NONE, 1, 0)
RR(ok_keyboard,            1, 6, 0,                           HW_NONE, 1, 0)
RR(ok_mouse_hwnd,          1, 2, 0,                           HW_OWN,  1, 0)
RR(ok_keyboard_sink,       1, 6, RIDEV_INPUTSINK,             HW_OWN,  1, 0)
RR(ok_mouse_nolegacy,      1, 2, RIDEV_NOLEGACY,              HW_NONE, 1, 0)
RR(ok_keyboard_nolegacy,   1, 6, RIDEV_NOLEGACY,              HW_NONE, 1, 0)
RR(ok_keyboard_nolegacy_appkeys, 1, 6, RIDEV_NOLEGACY | RIDEV_APPKEYS, HW_NONE, 1, 0)
RR(ok_keyboard_nohotkeys,  1, 6, RIDEV_NOHOTKEYS,             HW_NONE, 1, 0)
RR(ok_mouse_capturemouse,  1, 2, RIDEV_NOLEGACY | RIDEV_CAPTUREMOUSE, HW_OWN, 1, 0)
RR(ok_generic_desktop_pageonly, 1, 0, RIDEV_PAGEONLY,         HW_NONE, 1, 0)
RR(ok_digitizer_page,      0x0D, 4, 0,                        HW_NONE, 1, 0)
RR(ok_consumer_control,    0x0C, 1, 0,                        HW_NONE, 1, 0)

/* ---- RIDEV_REMOVE ----------------------------------------------------------------------- */
RR(remove_unregistered_ok,       VP, 1, RIDEV_REMOVE,                 HW_NONE, 1, 0)
RR(remove_pageonly_ok,           VP, 0, RIDEV_REMOVE | RIDEV_PAGEONLY, HW_NONE, 1, 0)
RR(remove_with_hwnd_87,          VP, 1, RIDEV_REMOVE,                 HW_OWN,  0, 87)
RR(remove_with_bogus_hwnd_87,    VP, 1, RIDEV_REMOVE,                 HW_BOGUS, 0, 87)
RR(remove_pageonly_with_hwnd_87, VP, 0, RIDEV_REMOVE | RIDEV_PAGEONLY, HW_OWN, 0, 87)
RR(remove_usage0_nomode_87,      VP, 0, RIDEV_REMOVE,                 HW_NONE, 0, 87)
RR(remove_exclude_1004,          VP, 1, RIDEV_REMOVE | RIDEV_EXCLUDE, HW_NONE, 0, ERROR_INVALID_FLAGS)
RR(remove_nolegacy_1004,         1, 6, RIDEV_REMOVE | RIDEV_NOLEGACY, HW_NONE, 0, ERROR_INVALID_FLAGS)
RR(remove_mouse_nolegacy_1004,   1, 2, RIDEV_REMOVE | RIDEV_NOLEGACY, HW_NONE, 0, ERROR_INVALID_FLAGS)
RR(remove_page0_87,              0, 1, RIDEV_REMOVE,                  HW_NONE, 0, 87)

/* ---- usage page / usage rules ------------------------------------------------------------ */
RR(page0_usage0_87,        0, 0, 0,                          HW_NONE, 0, 87)
RR(page0_usage1_87,        0, 1, 0,                          HW_NONE, 0, 87)
RR(page0_pageonly_87,      0, 0, RIDEV_PAGEONLY,             HW_NONE, 0, 87)
RR(page0_exclude_87,       0, 1, RIDEV_EXCLUDE,              HW_NONE, 0, 87)
RR(usage0_noflags_87,      VP, 0, 0,                         HW_NONE, 0, 87)
RR(usage0_exclude_87,      VP, 0, RIDEV_EXCLUDE,             HW_NONE, 0, 87)
RR(usage0_hwnd_87,         VP, 0, 0,                         HW_OWN,  0, 87)
RR(pageonly_with_usage_87, VP, 5, RIDEV_PAGEONLY,            HW_NONE, 0, 87)
RR(pageonly_usage1_87,     1, 1, RIDEV_PAGEONLY,             HW_NONE, 0, 87)
RR(exclude_with_hwnd_87,   VP, 1, RIDEV_EXCLUDE,             HW_OWN,  0, 87)
RR(exclude_with_sink_87,   VP, 1, RIDEV_EXCLUDE | RIDEV_INPUTSINK, HW_OWN, 0, 87)
RR(nolegacy_nonlegacy_87,  VP, 1, RIDEV_NOLEGACY,            HW_NONE, 0, 87)
RR(nolegacy_usage0_87,     VP, 0, RIDEV_NOLEGACY,            HW_NONE, 0, 87)
RR(nolegacy_page1_usage1_87, 1, 1, RIDEV_NOLEGACY,           HW_NONE, 0, 87)
RR(nolegacy_page1_usage5_87, 1, 5, RIDEV_NOLEGACY,           HW_NONE, 0, 87)

/* ---- hwnd rules --------------------------------------------------------------------------- */
RR(sink_without_hwnd_87,    VP, 1, RIDEV_INPUTSINK,          HW_NONE, 0, 87)
RR(exsink_without_hwnd_87,  VP, 1, RIDEV_EXINPUTSINK,        HW_NONE, 0, 87)
RR(pageonly_sink_nohwnd_87, VP, 0, RIDEV_PAGEONLY | RIDEV_INPUTSINK, HW_NONE, 0, 87)
RR(bogus_hwnd_87,           VP, 1, 0,                        HW_BOGUS, 0, 87)
RR(bogus_hwnd_sink_87,      VP, 1, RIDEV_INPUTSINK,          HW_BOGUS, 0, 87)
RR(bogus_hwnd_pageonly_87,  VP, 0, RIDEV_PAGEONLY,           HW_BOGUS, 0, 87)
RR(desktop_hwnd_87,         VP, 1, 0,                        HW_DESKTOP, 0, 87)   /* desktop window belongs to another process */
RR(desktop_hwnd_sink_87,    VP, 1, RIDEV_INPUTSINK,          HW_DESKTOP, 0, 87)
RR(keyboard_bogus_hwnd_87,  1, 6, 0,                         HW_BOGUS, 0, 87)
RR(mouse_desktop_hwnd_87,   1, 2, 0,                         HW_DESKTOP, 0, 87)

/* ---- invalid flag bits (dwFlags & 0xffffc0ce) -> ERROR_INVALID_FLAGS --------------------- */
RR(flag_0002, VP, 1, 0x00000002, HW_NONE, 0, ERROR_INVALID_FLAGS)
RR(flag_0004, VP, 1, 0x00000004, HW_NONE, 0, ERROR_INVALID_FLAGS)
RR(flag_0008, VP, 1, 0x00000008, HW_NONE, 0, ERROR_INVALID_FLAGS)
RR(flag_0040, VP, 1, 0x00000040, HW_NONE, 0, ERROR_INVALID_FLAGS)
RR(flag_0080, VP, 1, 0x00000080, HW_NONE, 0, ERROR_INVALID_FLAGS)
RR(flag_4000, VP, 1, 0x00004000, HW_NONE, 0, ERROR_INVALID_FLAGS)
RR(flag_8000, VP, 1, 0x00008000, HW_NONE, 0, ERROR_INVALID_FLAGS)
RR(flag_10000, VP, 1, 0x00010000, HW_NONE, 0, ERROR_INVALID_FLAGS)
RR(flag_100000, VP, 1, 0x00100000, HW_NONE, 0, ERROR_INVALID_FLAGS)
RR(flag_1000000, VP, 1, 0x01000000, HW_NONE, 0, ERROR_INVALID_FLAGS)
RR(flag_80000000, VP, 1, 0x80000000, HW_NONE, 0, ERROR_INVALID_FLAGS)
RR(flag_ffffffff, VP, 1, 0xffffffff, HW_NONE, 0, ERROR_INVALID_FLAGS)
RR(flag_invalid_beats_page0, 0, 0, 0x2, HW_NONE, 0, ERROR_INVALID_FLAGS)   /* mask test precedes the usage-page test */
RR(flag_invalid_with_remove, VP, 1, RIDEV_REMOVE | 0x4, HW_NONE, 0, ERROR_INVALID_FLAGS)
RR(flag_invalid_with_valid_hwnd, VP, 1, RIDEV_INPUTSINK | 0x40, HW_OWN, 0, ERROR_INVALID_FLAGS)
RR(flag_invalid_keyboard, 1, 6, 0x8, HW_NONE, 0, ERROR_INVALID_FLAGS)

/* ---- CAPTUREMOUSE / APPKEYS restrictions ------------------------------------------------- */
RR(capturemouse_vendor_1004,   VP, 1, RIDEV_CAPTUREMOUSE,       HW_NONE, 0, ERROR_INVALID_FLAGS)
RR(capturemouse_vendor_hwnd_1004, VP, 1, RIDEV_CAPTUREMOUSE,    HW_OWN,  0, ERROR_INVALID_FLAGS)
RR(capturemouse_mouse_plain_1004, 1, 2, RIDEV_CAPTUREMOUSE,     HW_OWN,  0, ERROR_INVALID_FLAGS)
RR(capturemouse_mouse_nolegacy_nohwnd_1004, 1, 2, RIDEV_NOLEGACY | RIDEV_CAPTUREMOUSE, HW_NONE, 0, ERROR_INVALID_FLAGS)
RR(capturemouse_mouse_nolegacy_desktop_1004, 1, 2, RIDEV_NOLEGACY | RIDEV_CAPTUREMOUSE, HW_DESKTOP, 0, ERROR_INVALID_FLAGS)
RR(capturemouse_pageonly_1004, 1, 0, RIDEV_PAGEONLY | RIDEV_CAPTUREMOUSE, HW_NONE, 0, ERROR_INVALID_FLAGS)
RR(appkeys_keyboard_plain_1004, 1, 6, RIDEV_APPKEYS,            HW_NONE, 0, ERROR_INVALID_FLAGS)
RR(appkeys_keyboard_sink_1004,  1, 6, RIDEV_APPKEYS | RIDEV_INPUTSINK, HW_OWN, 0, ERROR_INVALID_FLAGS)
RR(appkeys_mouse_nolegacy_1004, 1, 2, RIDEV_APPKEYS | RIDEV_NOLEGACY, HW_NONE, 0, ERROR_INVALID_FLAGS)
RR(appkeys_vendor_nolegacy_1004, VP, 1, RIDEV_APPKEYS | RIDEV_NOLEGACY, HW_NONE, 0, ERROR_INVALID_FLAGS)
RR(appkeys_vendor_1004,         VP, 1, RIDEV_APPKEYS,           HW_NONE, 0, ERROR_INVALID_FLAGS)
RR(appkeys_pageonly_1004,       1, 0, RIDEV_APPKEYS | RIDEV_PAGEONLY, HW_NONE, 0, ERROR_INVALID_FLAGS)

/* ---- argument protocol ------------------------------------------------------------------- */
static void reg_args(const void *p, UINT cnt, UINT cb, DWORD want_err) {
    REQUIRE(UN(NtUserRegisterRawInputDevices));
    rit_gui();
    rit_unreg_all();
    UINT before = rit_reg_count();
    SET_ERR(0);
    LONGLONG r = UN(NtUserRegisterRawInputDevices)(p, cnt, (int)cb);
    CHECK((r & 0xffffffff) == 0);
    EXPECT_ERR(want_err);
    CHECK(rit_reg_count() == before);
}
static const RAWINPUTDEVICE ok1 = { VP, 1, 0, NULL };
RT(reg_args_null_array)     { reg_args(NULL, 1, 16, 87); }
RT(reg_args_zero_count)     { reg_args(&ok1, 0, 16, 87); }
RT(reg_args_cb_0)           { reg_args(&ok1, 1, 0, 87); }
RT(reg_args_cb_8)           { reg_args(&ok1, 1, 8, 87); }
RT(reg_args_cb_12)          { reg_args(&ok1, 1, 12, 87); }
RT(reg_args_cb_15)          { reg_args(&ok1, 1, 15, 87); }
RT(reg_args_cb_17)          { reg_args(&ok1, 1, 17, 87); }
RT(reg_args_cb_24)          { reg_args(&ok1, 1, 24, 87); }
RT(reg_args_cb_32)          { reg_args(&ok1, 1, 32, 87); }
RT(reg_args_cb_ffffffff)    { reg_args(&ok1, 1, 0xffffffff, 87); }
RT(reg_args_user32_wrapper_same) {
    REQUIRE(U(RegisterRawInputDevices));
    rit_unreg_all();
    SET_ERR(0);
    CHECK(!U(RegisterRawInputDevices)(&ok1, 1, 15));
    EXPECT_ERR(87);
    CHECK(U(RegisterRawInputDevices)(&ok1, 1, sizeof ok1));
    rit_unreg_all();
}

/* ---- multi-element calls: validated as a whole before anything is applied ----------------- */
RT(reg_multi_all_valid) {
    REQUIRE(U(RegisterRawInputDevices));
    rit_unreg_all();
    RAWINPUTDEVICE d[4] = { {VP,1,0,NULL}, {VP,2,0,NULL}, {VP,3,RIDEV_EXCLUDE,NULL}, {0x0D,0,RIDEV_PAGEONLY,NULL} };
    UINT before = rit_reg_count();
    CHECK(U(RegisterRawInputDevices)(d, 4, sizeof d[0]));
    CHECK(rit_reg_count() == before + 4);
    CHECK(rit_reg_find(VP, 1, NULL) && rit_reg_find(VP, 2, NULL) && rit_reg_find(VP, 3, NULL) && rit_reg_find(0x0D, 0, NULL));
    rit_unreg_all();
    CHECK(rit_reg_count() == before);
}
RT(reg_multi_second_invalid_registers_nothing) {
    REQUIRE(U(RegisterRawInputDevices));
    rit_unreg_all();
    RAWINPUTDEVICE d[3] = { {VP,1,0,NULL}, {VP,2,0x4,NULL}, {VP,3,0,NULL} };
    UINT before = rit_reg_count();
    SET_ERR(0);
    CHECK(!U(RegisterRawInputDevices)(d, 3, sizeof d[0]));
    EXPECT_ERR(ERROR_INVALID_FLAGS);
    /* the validity loop runs over the whole array before SetProcDeviceRequest touches anything */
    CHECK(rit_reg_count() == before);
    CHECK(!rit_reg_find(VP, 1, NULL));
}
RT(reg_multi_last_invalid_registers_nothing) {
    REQUIRE(U(RegisterRawInputDevices));
    rit_unreg_all();
    RAWINPUTDEVICE d[3] = { {VP,1,0,NULL}, {VP,2,0,NULL}, {0,3,0,NULL} };
    UINT before = rit_reg_count();
    SET_ERR(0);
    CHECK(!U(RegisterRawInputDevices)(d, 3, sizeof d[0]));
    EXPECT_ERR(87);
    CHECK(rit_reg_count() == before);
}
RT(reg_multi_error_is_that_of_first_bad_element) {
    REQUIRE(U(RegisterRawInputDevices));
    rit_unreg_all();
    RAWINPUTDEVICE d[3] = { {VP,1,0,NULL}, {0,2,0,NULL}, {VP,3,0x4,NULL} };
    SET_ERR(0);
    CHECK(!U(RegisterRawInputDevices)(d, 3, sizeof d[0]));
    EXPECT_ERR(87);                       /* element 1 (page 0) fails before element 2 is looked at */
}
RT(reg_multi_duplicates_collapse) {
    REQUIRE(U(RegisterRawInputDevices));
    rit_unreg_all();
    RAWINPUTDEVICE d[3] = { {VP,1,0,NULL}, {VP,1,0,NULL}, {VP,1,0,NULL} };
    UINT before = rit_reg_count();
    CHECK(U(RegisterRawInputDevices)(d, 3, sizeof d[0]));
    /* SetProcDeviceRequest removes the existing request for the same page/usage before inserting */
    CHECK(rit_reg_count() == before + 1);
    rit_unreg_all();
}
RT(reg_multi_register_then_remove_in_one_call) {
    REQUIRE(U(RegisterRawInputDevices));
    rit_unreg_all();
    RAWINPUTDEVICE d[2] = { {VP,1,0,NULL}, {VP,1,RIDEV_REMOVE,NULL} };
    UINT before = rit_reg_count();
    CHECK(U(RegisterRawInputDevices)(d, 2, sizeof d[0]));
    CHECK(rit_reg_count() == before);
}
RT(reg_bulk_64_usages) {
    REQUIRE(U(RegisterRawInputDevices));
    rit_unreg_all();
    RAWINPUTDEVICE d[64];
    for (int i = 0; i < 64; i++) { d[i].usUsagePage = VP + 1; d[i].usUsage = (USHORT)(i + 1); d[i].dwFlags = 0; d[i].hwndTarget = NULL; }
    UINT before = rit_reg_count();
    CHECK(U(RegisterRawInputDevices)(d, 64, sizeof d[0]));
    CHECK(rit_reg_count() == before + 64);
    for (int i = 0; i < 64; i++) d[i].dwFlags = RIDEV_REMOVE;
    CHECK(U(RegisterRawInputDevices)(d, 64, sizeof d[0]));
    CHECK(rit_reg_count() == before);
}
RT(reg_bulk_256_usages) {
    REQUIRE(U(RegisterRawInputDevices));
    rit_unreg_all();
    RAWINPUTDEVICE *d = calloc(256, sizeof *d);
    for (int i = 0; i < 256; i++) { d[i].usUsagePage = VP + 2; d[i].usUsage = (USHORT)(i + 1); }
    UINT before = rit_reg_count();
    BOOL r = U(RegisterRawInputDevices)(d, 256, sizeof d[0]);
    CHECK(r);                                    /* no documented ceiling: limited only by pool quota */
    if (r) CHECK(rit_reg_count() == before + 256);
    rit_unreg_all();
    CHECK(rit_reg_count() == before);
    free(d);
}

/* ---- round trip through GetRegisteredRawInputDevices ------------------------------------- */
RT(reg_roundtrip_plain) {
    REQUIRE(U(RegisterRawInputDevices));
    rit_unreg_all();
    RAWINPUTDEVICE d = { VP, 7, 0, NULL }, g;
    CHECK(U(RegisterRawInputDevices)(&d, 1, sizeof d));
    CHECK(rit_reg_find(VP, 7, &g));
    CHECK(g.usUsagePage == VP && g.usUsage == 7);
    CHECK(g.dwFlags == 0);
    CHECK(g.hwndTarget == NULL);
    rit_unreg_all();
}
RT(reg_roundtrip_hwnd) {
    REQUIRE(U(RegisterRawInputDevices));
    rit_unreg_all();
    HWND h = k_make_window("rt"); if (!h) { t_skip("no window"); return; }
    RAWINPUTDEVICE d = { VP, 7, 0, h }, g;
    CHECK(U(RegisterRawInputDevices)(&d, 1, sizeof d));
    CHECK(rit_reg_find(VP, 7, &g));
    CHECK(g.hwndTarget == h);                    /* reported from the locked tagWND's head.h */
    rit_unreg_all();
    U(DestroyWindow)(h);
}
RT(reg_roundtrip_sink) {
    REQUIRE(U(RegisterRawInputDevices));
    rit_unreg_all();
    HWND h = k_make_window("rt"); if (!h) { t_skip("no window"); return; }
    RAWINPUTDEVICE d = { VP, 7, RIDEV_INPUTSINK, h }, g;
    CHECK(U(RegisterRawInputDevices)(&d, 1, sizeof d));
    CHECK(rit_reg_find(VP, 7, &g));
    CHECK(g.dwFlags == RIDEV_INPUTSINK);         /* entry flag bit0 -> 0x100 */
    CHECK(g.hwndTarget == h);
    rit_unreg_all();
    U(DestroyWindow)(h);
}
RT(reg_roundtrip_exsink) {
    REQUIRE(U(RegisterRawInputDevices));
    rit_unreg_all();
    HWND h = k_make_window("rt"); if (!h) { t_skip("no window"); return; }
    RAWINPUTDEVICE d = { VP, 7, RIDEV_EXINPUTSINK, h }, g;
    CHECK(U(RegisterRawInputDevices)(&d, 1, sizeof d));
    CHECK(rit_reg_find(VP, 7, &g));
    /* Win10 22H2 reports the registration back as RIDEV_EXINPUTSINK (0x1000) alone, without the RIDEV_INPUTSINK bit */
    CHECK(g.dwFlags == RIDEV_EXINPUTSINK);
    rit_unreg_all();
    U(DestroyWindow)(h);
}
RT(reg_roundtrip_pageonly) {
    REQUIRE(U(RegisterRawInputDevices));
    rit_unreg_all();
    RAWINPUTDEVICE d = { VP, 0, RIDEV_PAGEONLY, NULL }, g;
    CHECK(U(RegisterRawInputDevices)(&d, 1, sizeof d));
    CHECK(rit_reg_find(VP, 0, &g));
    CHECK(g.dwFlags == RIDEV_PAGEONLY);          /* page-only list reports 0x20 (0x120 with sink) */
    rit_unreg_all();
    CHECK(!rit_reg_find(VP, 0, NULL));
}
RT(reg_roundtrip_pageonly_sink) {
    REQUIRE(U(RegisterRawInputDevices));
    rit_unreg_all();
    HWND h = k_make_window("rt"); if (!h) { t_skip("no window"); return; }
    RAWINPUTDEVICE d = { VP, 0, RIDEV_PAGEONLY | RIDEV_INPUTSINK, h }, g;
    CHECK(U(RegisterRawInputDevices)(&d, 1, sizeof d));
    CHECK(rit_reg_find(VP, 0, &g));
    CHECK(g.dwFlags == (RIDEV_PAGEONLY | RIDEV_INPUTSINK));
    CHECK(g.hwndTarget == h);
    rit_unreg_all();
    U(DestroyWindow)(h);
}
RT(reg_roundtrip_exclude) {
    REQUIRE(U(RegisterRawInputDevices));
    rit_unreg_all();
    RAWINPUTDEVICE d = { VP, 9, RIDEV_EXCLUDE, NULL }, g;
    CHECK(U(RegisterRawInputDevices)(&d, 1, sizeof d));
    CHECK(rit_reg_find(VP, 9, &g));
    CHECK(g.dwFlags == RIDEV_EXCLUDE);           /* third list is reported with dwFlags 0x10, hwnd NULL */
    CHECK(g.hwndTarget == NULL);
    rit_unreg_all();
    CHECK(!rit_reg_find(VP, 9, NULL));
}
RT(reg_roundtrip_reregister_replaces) {
    REQUIRE(U(RegisterRawInputDevices));
    rit_unreg_all();
    HWND h = k_make_window("rt"); if (!h) { t_skip("no window"); return; }
    RAWINPUTDEVICE a = { VP, 3, 0, NULL }, b = { VP, 3, RIDEV_INPUTSINK, h }, g;
    UINT before = rit_reg_count();
    CHECK(U(RegisterRawInputDevices)(&a, 1, sizeof a));
    CHECK(U(RegisterRawInputDevices)(&b, 1, sizeof b));
    CHECK(rit_reg_count() == before + 1);
    CHECK(rit_reg_find(VP, 3, &g));
    CHECK(g.hwndTarget == h && g.dwFlags == RIDEV_INPUTSINK);
    CHECK(U(RegisterRawInputDevices)(&a, 1, sizeof a));
    CHECK(rit_reg_find(VP, 3, &g));
    CHECK(g.hwndTarget == NULL);
    rit_unreg_all();
    U(DestroyWindow)(h);
}
RT(reg_roundtrip_remove_restores_count) {
    REQUIRE(U(RegisterRawInputDevices));
    rit_unreg_all();
    UINT c0 = rit_reg_count();
    RAWINPUTDEVICE d = { VP, 4, 0, NULL };
    CHECK(U(RegisterRawInputDevices)(&d, 1, sizeof d));
    CHECK(rit_reg_count() == c0 + 1);
    d.dwFlags = RIDEV_REMOVE;
    CHECK(U(RegisterRawInputDevices)(&d, 1, sizeof d));
    CHECK(rit_reg_count() == c0);
    CHECK(U(RegisterRawInputDevices)(&d, 1, sizeof d));     /* removing again is not an error */
    CHECK(rit_reg_count() == c0);
}
RT(reg_roundtrip_exclude_removed_by_plain_remove) {
    REQUIRE(U(RegisterRawInputDevices));
    rit_unreg_all();
    RAWINPUTDEVICE d = { VP, 4, RIDEV_EXCLUDE, NULL };
    UINT c0 = rit_reg_count();
    CHECK(U(RegisterRawInputDevices)(&d, 1, sizeof d));
    d.dwFlags = RIDEV_REMOVE;                    /* SearchProcessHidRequest checks all three lists by page+usage */
    CHECK(U(RegisterRawInputDevices)(&d, 1, sizeof d));
    CHECK(rit_reg_count() == c0);
}
RT(reg_roundtrip_page_and_usage_independent) {
    REQUIRE(U(RegisterRawInputDevices));
    rit_unreg_all();
    RAWINPUTDEVICE d[3] = { {VP,1,0,NULL}, {VP,0,RIDEV_PAGEONLY,NULL}, {VP,2,RIDEV_EXCLUDE,NULL} };
    UINT c0 = rit_reg_count();
    CHECK(U(RegisterRawInputDevices)(d, 3, sizeof d[0]));
    CHECK(rit_reg_count() == c0 + 3);
    RAWINPUTDEVICE r = { VP, 1, RIDEV_REMOVE, NULL };
    CHECK(U(RegisterRawInputDevices)(&r, 1, sizeof r));
    CHECK(rit_reg_count() == c0 + 2);
    CHECK(!rit_reg_find(VP, 1, NULL));
    CHECK(rit_reg_find(VP, 2, NULL));
    rit_unreg_all();
}
static DWORD WINAPI count_thread(LPVOID p) {
    /* the table hangs off the process (tagPROCESSINFO+0x340), not the thread */
    *(UINT *)p = rit_reg_count();
    return rit_reg_find(VP, 5, NULL) ? 1 : 0;
}
RT(reg_visible_from_other_thread) {
    REQUIRE(U(RegisterRawInputDevices));
    rit_unreg_all();
    RAWINPUTDEVICE d = { VP, 5, 0, NULL };
    CHECK(U(RegisterRawInputDevices)(&d, 1, sizeof d));
    UINT n = 0;
    HANDLE t = CreateThread(NULL, 0, count_thread, &n, 0, NULL);
    if (!t) { t_skip("thread"); return; }
    CHECK(WaitForSingleObject(t, 5000) == WAIT_OBJECT_0);
    DWORD code = 0; GetExitCodeThread(t, &code); CloseHandle(t);
    CHECK(code == 1);
    CHECK(n == rit_reg_count());
    rit_unreg_all();
}
RT(reg_legacy_keyboard_flags_visible) {
    REQUIRE(U(RegisterRawInputDevices));
    rit_unreg_all();
    RAWINPUTDEVICE d = { 1, 6, RIDEV_NOLEGACY, NULL }, g;
    CHECK(U(RegisterRawInputDevices)(&d, 1, sizeof d));
    if (rit_reg_find(1, 6, &g)) {
        t_note("keyboard NOLEGACY reported back as 0x%lx", g.dwFlags);
        CHECK((g.dwFlags & RIDEV_EXMODEMASK) == RIDEV_NOLEGACY || (g.dwFlags & RIDEV_EXMODEMASK) == 0);
    }
    rit_unreg_all();
    CHECK(!rit_reg_find(1, 6, NULL));
}
RT(reg_legacy_mouse_flags_visible) {
    REQUIRE(U(RegisterRawInputDevices));
    rit_unreg_all();
    RAWINPUTDEVICE d = { 1, 2, RIDEV_NOLEGACY, NULL }, g;
    CHECK(U(RegisterRawInputDevices)(&d, 1, sizeof d));
    if (rit_reg_find(1, 2, &g)) t_note("mouse NOLEGACY reported back as 0x%lx", g.dwFlags);
    rit_unreg_all();
    CHECK(!rit_reg_find(1, 2, NULL));
}
RT(reg_window_destroyed_keeps_or_drops_entry) {
    REQUIRE(U(RegisterRawInputDevices));
    rit_unreg_all();
    HWND h = k_make_window("rt"); if (!h) { t_skip("no window"); return; }
    RAWINPUTDEVICE d = { VP, 6, RIDEV_INPUTSINK, h }, g;
    CHECK(U(RegisterRawInputDevices)(&d, 1, sizeof d));
    U(DestroyWindow)(h);
    if (rit_reg_find(VP, 6, &g)) t_note("entry survived window destruction; hwndTarget now %p", g.hwndTarget);
    else t_note("entry dropped with its window");
    CHECK(1);
    rit_unreg_all();
}

/* ---- GetRegisteredRawInputDevices buffer protocol --------------------------------------- */
static void getreg_fixture(void) {
    rit_unreg_all();
    RAWINPUTDEVICE d[3] = { {VP,1,0,NULL}, {VP,2,0,NULL}, {VP,3,0,NULL} };
    U(RegisterRawInputDevices)(d, 3, sizeof d[0]);
}
RT(getreg_cbsize_wrong) {
    REQUIRE(U(GetRegisteredRawInputDevices));
    UINT cbs[] = { 0, 8, 12, 15, 17, 24, 32, 0xffffffff };
    for (unsigned i = 0; i < sizeof cbs / sizeof *cbs; i++) {
        UINT n = 5;
        SET_ERR(0);
        CHECKF(U(GetRegisteredRawInputDevices)(NULL, &n, cbs[i]) == (UINT)-1, "cb %u", cbs[i]);
        EXPECT_ERR(87);
        CHECK(n == 5);
    }
}
RT(getreg_count_query) {
    REQUIRE(U(GetRegisteredRawInputDevices));
    getreg_fixture();
    UINT n = 0;
    CHECK(U(GetRegisteredRawInputDevices)(NULL, &n, sizeof(RAWINPUTDEVICE)) == 0);
    CHECK(n >= 3);
    CHECK(n == rit_reg_count());
    rit_unreg_all();
    CHECK(U(GetRegisteredRawInputDevices)(NULL, &n, sizeof(RAWINPUTDEVICE)) == 0);
    CHECK(n == 0 || n == rit_reg_count());
}
RT(getreg_zero_capacity_with_buffer_87) {
    REQUIRE(U(GetRegisteredRawInputDevices));
    getreg_fixture();
    RAWINPUTDEVICE b[8]; UINT n = 0;
    SET_ERR(0);
    CHECK(U(GetRegisteredRawInputDevices)(b, &n, sizeof b[0]) == (UINT)-1);
    EXPECT_ERR(87);
    rit_unreg_all();
}
RT(getreg_small_buffer_122) {
    REQUIRE(U(GetRegisteredRawInputDevices));
    getreg_fixture();
    UINT need = rit_reg_count();
    RAWINPUTDEVICE b[8]; UINT n = 1;
    memset(b, 0x77, sizeof b);
    SET_ERR(0);
    CHECK(U(GetRegisteredRawInputDevices)(b, &n, sizeof b[0]) == (UINT)-1);
    EXPECT_ERR(E_INSUFFICIENT);
    CHECK(n == need);
    CHECK(((unsigned char *)b)[0] == 0x77);
    n = need - 1;
    SET_ERR(0);
    CHECK(U(GetRegisteredRawInputDevices)(b, &n, sizeof b[0]) == (UINT)-1);
    EXPECT_ERR(E_INSUFFICIENT);
    CHECK(n == need);
    rit_unreg_all();
}
RT(getreg_exact_and_larger) {
    REQUIRE(U(GetRegisteredRawInputDevices));
    getreg_fixture();
    UINT need = rit_reg_count();
    RAWINPUTDEVICE *b = calloc(need + 10, sizeof *b);
    UINT n = need;
    CHECK(U(GetRegisteredRawInputDevices)(b, &n, sizeof *b) == need);
    n = need + 10;
    CHECK(U(GetRegisteredRawInputDevices)(b, &n, sizeof *b) == need);
    int found = 0;
    for (UINT i = 0; i < need; i++) if (b[i].usUsagePage == VP && b[i].usUsage >= 1 && b[i].usUsage <= 3) found++;
    CHECK(found == 3);
    free(b);
    rit_unreg_all();
}
RT(getreg_sorted_unique) {
    REQUIRE(U(GetRegisteredRawInputDevices));
    getreg_fixture();
    RAWINPUTDEVICE *b; int n = rit_reg_get(&b);
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++) CHECK(!(b[i].usUsagePage == b[j].usUsagePage && b[i].usUsage == b[j].usUsage && b[i].dwFlags == b[j].dwFlags));
    free(b);
    rit_unreg_all();
}
RT(getreg_direct_stub_cb_protocol) {
    REQUIRE(UN(NtUserGetRegisteredRawInputDevices));
    rit_gui();
    UINT n = 3;
    SET_ERR(0);
    CHECK(UN(NtUserGetRegisteredRawInputDevices)(NULL, &n, 8) == (UINT)-1);
    EXPECT_ERR(87);
    CHECK(UN(NtUserGetRegisteredRawInputDevices)(NULL, &n, 16) == 0);
}

/* ---- fresh process: nothing registered ---------------------------------------------------- */
CHILD(rit_fresh_registrations) {
    int nfail = 0; (void)argc; (void)argv;
    UINT n = 0xfeed;
    if (!U(GetRegisteredRawInputDevices)) return 0;
    KC(U(GetRegisteredRawInputDevices)(NULL, &n, 16) == 0);
    KC(n == 0);
    RAWINPUTDEVICE b[4];
    n = 4;
    SET_ERR(0);
    /* no process HID table or empty lists: the copy loops are skipped and 0 is returned */
    KC(U(GetRegisteredRawInputDevices)(b, &n, 16) == 0);
    n = 0;
    SET_ERR(0);
    KC(U(GetRegisteredRawInputDevices)(b, &n, 16) == (UINT)-1);
    KC(GetLastError() == 87);
    /* a failed registration must not create anything visible */
    RAWINPUTDEVICE bad = { 0, 0, 0, NULL };
    KC(!U(RegisterRawInputDevices)(&bad, 1, 16));
    n = 9;
    KC(U(GetRegisteredRawInputDevices)(NULL, &n, 16) == 0);
    KC(n == 0);
    return nfail;
}
RT(getreg_fresh_process) {
    struct k_child_result r = k_child_run("rit_fresh_registrations", "", 20000);
    CHECKF(r.exit_code == 0, "child exit 0x%lx", (unsigned long)r.exit_code);
}

/* registrations die with the process and are not inherited */
CHILD(rit_reg_child_registers) {
    int nfail = 0; (void)argc; (void)argv;
    RAWINPUTDEVICE d = { VP, 1, 0, NULL };
    KC(U(RegisterRawInputDevices)(&d, 1, 16));
    UINT n = 0;
    U(GetRegisteredRawInputDevices)(NULL, &n, 16);
    KC(n == 1);
    return nfail;
}
RT(reg_not_shared_between_processes) {
    REQUIRE(U(RegisterRawInputDevices));
    rit_unreg_all();
    UINT c0 = rit_reg_count();
    struct k_child_result r = k_child_run("rit_reg_child_registers", "", 20000);
    CHECK(r.exit_code == 0);
    CHECK(rit_reg_count() == c0);
}
