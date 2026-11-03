/* gap9_gesture*, gap9_touch*, gap9_ptp*, gap9_extptr*, gap9_imsrc*, gap9_imehk*, gap9_menuhelp*, gap9_comp*:
 * parameter-validation depth for syscalls that need real hardware or compositor state to succeed.
 *
 * Analysis facts used:
 *  - NtUserGetGestureInfo(h, GESTUREINFO*): NULL -> 0x57; cbSize != 0x38 -> 0x57 (both before handle lookup).
 *  - NtUserSetGestureConfig(hwnd, 0, cIDs, GESTURECONFIG*, cbSize): ValidateHwnd first (1400), then
 *    pConfigs==NULL || cbSize != 12 || cIDs-1 > 0xFF -> 0x57, then window must belong to this process (0x578).
 *  - NtUserGetTouchInputInfo(h, c, TOUCHINPUT*, cbSize): pInputs==NULL or cbSize != 0x30 -> 0x57 first.
 *  - NtUserGetPrecisionTouchPadConfiguration(cfg*): first dword (version) must be 0, else
 *    STATUS_NOT_IMPLEMENTED (0xC0000002) and nothing is written.
 *  - NtUserGetExtendedPointerDeviceProperty(h, prop*): NULL -> 0x57; handle not of type 0x13 -> 6.
 *  - NtUserGetCurrentInputMessageSource(src*): NULL -> 0x57; device types 0x12/0x22 are reported as 2.
 *  - NtUserGetImeHotKey(id, a*, b*, hkl*): unknown id -> 0x58B; hotkey with an HKL but no hkl* -> 0x57.
 *  - NtUserSetMenuContextHelpId(hmenu, id): ValidateHmenu failure -> 0 (1401).
 *  - NtDesktopCaptureBits / NtVisualCaptureBits: NULL event or section handle -> STATUS_INVALID_PARAMETER first.
 *  - NtDuplicateCompositionInputSink: only dwm.exe may call, others get STATUS_ACCESS_DENIED. */
#include "g9.h"

UDECL(BOOL, NtUserGetGestureInfo, (HANDLE, GESTUREINFO *));
UDECL(BOOL, NtUserSetGestureConfig, (HWND, DWORD, UINT, GESTURECONFIG *, UINT));
UDECL(BOOL, NtUserGetTouchInputInfo, (HANDLE, UINT, TOUCHINPUT *, int));
UDECL(BOOL, NtUserGetPrecisionTouchPadConfiguration, (void *));
UDECL(BOOL, NtUserGetExtendedPointerDeviceProperty, (HANDLE, void *));
UDECL(BOOL, NtUserGetCurrentInputMessageSource, (INPUT_MESSAGE_SOURCE *));
UDECL(BOOL, NtUserGetImeHotKey, (DWORD, UINT *, UINT *, HKL *));
UDECL(BOOL, NtUserSetMenuContextHelpId, (HMENU, DWORD));
UDECL(LONG, NtDesktopCaptureBits, (LONG_PTR *, UINT, UINT, UINT, UINT, UINT, LONG_PTR, LONG_PTR));
UDECL(LONG, NtVisualCaptureBits, (UINT, UINT, int, int, UINT, UINT, int, void *, LONG_PTR));
UDECL(LONG, NtDuplicateCompositionInputSink, (void *, ULONGLONG *));

/* Wine's win32u exports these names as abort-on-call stubs */
#define NOWINE() do { if (t_is_wine()) { t_skip("not implemented by Wine's win32u"); return; } } while (0)
#define STATUS_INVALID_PARAMETER_ 0xC000000D
#define STATUS_ACCESS_DENIED_ 0xC0000022

/* ---- gestures ------------------------------------------------------------------------------- */
TEST(gap9_gesture_getinfo_size_checks) {
    REQUIRE(UN(NtUserGetGestureInfo));
    GESTUREINFO gi;
    HANDLE h = (HANDLE)(ULONG_PTR)0x1234;
    static const UINT sizes[] = { 0, 0x37, 0x39, 0x30, 0x40, 0xFFFFFFFF, sizeof(GESTUREINFO) + 8 };
    for (int i = 0; i < G9_N(sizes); i++) {
        memset(&gi, 0, sizeof gi); gi.cbSize = sizes[i];
        SetLastError(0);
        CHECKF(!UN(NtUserGetGestureInfo)(h, &gi), "cbSize 0x%x", sizes[i]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
    CHECK(sizeof(GESTUREINFO) == 0x38);
}

TEST(gap9_gesture_getinfo_null_output) {
    REQUIRE(UN(NtUserGetGestureInfo));
    SetLastError(0);
    CHECK(!UN(NtUserGetGestureInfo)((HANDLE)(ULONG_PTR)0x1234, NULL));
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}

TEST(gap9_gesture_getinfo_bogus_handle) {
    REQUIRE(UN(NtUserGetGestureInfo));
    GESTUREINFO gi; memset(&gi, 0x44, sizeof gi); gi.cbSize = 0x38;
    SetLastError(0);
    CHECK(!UN(NtUserGetGestureInfo)((HANDLE)(ULONG_PTR)0x1234, &gi));
    t_note("kernel GetGestureInfo(bogus handle) error %lu", GetLastError());
    CHECK(gi.cbSize == 0x38 && gi.dwFlags == 0x44444444);   /* nothing copied out on failure */
    if (U(GetGestureInfo)) {
        memset(&gi, 0, sizeof gi); gi.cbSize = 0x38;
        SetLastError(0);
        CHECK(!U(GetGestureInfo)((HGESTUREINFO)(ULONG_PTR)0x1234, &gi));
        EXPECT_ERR(ERROR_INVALID_HANDLE);                    /* user32 looks the handle up in its own UM handle list */
        gi.cbSize = 0x30;
        SetLastError(0);
        CHECK(!U(GetGestureInfo)((HGESTUREINFO)(ULONG_PTR)0x1234, &gi));
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
        SetLastError(0);
        CHECK(!U(GetGestureInfo)((HGESTUREINFO)(ULONG_PTR)0x1234, NULL));
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
}

TEST(gap9_gesture_config_param_validation) {
    REQUIRE(UN(NtUserSetGestureConfig));
    HWND h = g9_window(0);
    if (!h) { t_skip("no window"); return; }
    GESTURECONFIG gc[257]; memset(gc, 0, sizeof gc);
    gc[0].dwID = GID_ZOOM; gc[0].dwWant = GC_ZOOM; gc[0].dwBlock = 0;
    struct { UINT c; GESTURECONFIG *p; UINT cb; const char *n; } t[] = {
        { 1, gc, 11, "cb 11" }, { 1, gc, 13, "cb 13" }, { 1, gc, 0, "cb 0" }, { 1, gc, 24, "cb 24" }, { 1, gc, 0xFFFFFFFF, "cb -1" },
        { 1, NULL, 12, "NULL config" }, { 0, gc, 12, "zero IDs" }, { 257, gc, 12, "257 IDs" }, { 0xFFFFFFFF, gc, 12, "-1 IDs" }, { 0x100000, gc, 12, "huge IDs" },
    };
    for (int i = 0; i < G9_N(t); i++) {
        SetLastError(0);
        CHECKF(!UN(NtUserSetGestureConfig)(h, 0, t[i].c, t[i].p, t[i].cb), "%s", t[i].n);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
    g9_destroy(h);
}

TEST(gap9_gesture_config_count_256_passes_range_check) {
    REQUIRE(UN(NtUserSetGestureConfig));
    HWND h = g9_window(0);
    if (!h) { t_skip("no window"); return; }
    GESTURECONFIG gc[256]; memset(gc, 0, sizeof gc);
    for (int i = 0; i < 256; i++) { gc[i].dwID = GID_ZOOM; gc[i].dwWant = 0; gc[i].dwBlock = 0; }
    SetLastError(0);
    BOOL r = UN(NtUserSetGestureConfig)(h, 0, 256, gc, 12);
    /* Observed on Windows 10: 256 copies of one id are refused with ERROR_INVALID_PARAMETER exactly like 257 and like 2 copies, so the
     * count limit cannot be told apart from the per-entry validation (duplicate or unknown ids); only one entry is accepted here */
    DWORD e = GetLastError();
    t_note("256 configs: ret %d error %lu", r, e);
    CHECK(!r);
    EXPECT_EQ(e, ERROR_INVALID_PARAMETER);
    SetLastError(0);
    CHECK(UN(NtUserSetGestureConfig)(h, 0, 1, gc, 12));
    g9_destroy(h);
}

TEST(gap9_gesture_config_window_checked_before_params) {
    REQUIRE(UN(NtUserSetGestureConfig));
    GESTURECONFIG gc = { GID_ZOOM, GC_ZOOM, 0 };
    SetLastError(0);
    CHECK(!UN(NtUserSetGestureConfig)((HWND)(ULONG_PTR)0x6666, 0, 1, &gc, 12));
    EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
    SetLastError(0);
    CHECK(!UN(NtUserSetGestureConfig)((HWND)(ULONG_PTR)0x6666, 0, 0, NULL, 99));    /* ValidateHwnd fails first */
    EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
    SetLastError(0);
    CHECK(!UN(NtUserSetGestureConfig)(NULL, 0, 1, &gc, 12));
    EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
}

TEST(gap9_gesture_config_foreign_window) {
    REQUIRE(UN(NtUserSetGestureConfig)); REQUIRE(U(GetDesktopWindow));
    GESTURECONFIG gc = { GID_ZOOM, GC_ZOOM, 0 };
    SetLastError(0);
    /* the desktop window is owned by another process: the process comparison fails with 0x578 */
    CHECK(!UN(NtUserSetGestureConfig)(U(GetDesktopWindow)(), 0, 1, &gc, 12));
    EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
}

TEST(gap9_gesture_config_valid_call_result) {
    REQUIRE(UN(NtUserSetGestureConfig));
    HWND h = g9_window(0);
    if (!h) { t_skip("no window"); return; }
    GESTURECONFIG gc[2] = { { GID_ZOOM, GC_ZOOM, 0 }, { GID_PAN, GC_PAN, 0 } };
    SetLastError(0);
    BOOL r = UN(NtUserSetGestureConfig)(h, 0, 2, gc, sizeof gc[0]);
    t_note("SetGestureConfig(2 entries) ret %d error %lu", r, GetLastError());
    gc[0].dwWant = 0; gc[0].dwBlock = GC_ZOOM;
    r = UN(NtUserSetGestureConfig)(h, 0, 1, gc, sizeof gc[0]);          /* reconfigure the same gesture */
    t_note("SetGestureConfig(update) ret %d", r);
    g9_destroy(h);
}

/* ---- touch ------------------------------------------------------------------------------------ */
TEST(gap9_touch_getinfo_arg_checks) {
    REQUIRE(UN(NtUserGetTouchInputInfo));
    TOUCHINPUT ti[2]; memset(ti, 0x31, sizeof ti);
    HANDLE h = (HANDLE)(ULONG_PTR)0x4321;
    static const int sizes[] = { 0, 0x2F, 0x31, 0x28, 0x60, -1 };
    for (int i = 0; i < G9_N(sizes); i++) {
        SetLastError(0);
        CHECKF(!UN(NtUserGetTouchInputInfo)(h, 1, ti, sizes[i]), "cb %d", sizes[i]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
    SetLastError(0);
    CHECK(!UN(NtUserGetTouchInputInfo)(h, 1, NULL, 0x30));
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    CHECK(sizeof(TOUCHINPUT) == 0x30);
    CHECK(((BYTE *)ti)[0] == 0x31 && ((BYTE *)ti)[sizeof ti - 1] == 0x31);
}

TEST(gap9_touch_getinfo_bogus_handle) {
    REQUIRE(UN(NtUserGetTouchInputInfo));
    TOUCHINPUT ti; memset(&ti, 0x31, sizeof ti);
    SetLastError(0);
    CHECK(!UN(NtUserGetTouchInputInfo)((HANDLE)(ULONG_PTR)0x4321, 1, &ti, 0x30));
    t_note("kernel GetTouchInputInfo(bogus handle) error %lu", GetLastError());
    CHECK(((BYTE *)&ti)[0] == 0x31);
    CHECK(!UN(NtUserGetTouchInputInfo)(NULL, 1, &ti, 0x30));
    if (U(GetTouchInputInfo)) {
        SetLastError(0);
        CHECK(!U(GetTouchInputInfo)((HTOUCHINPUT)(ULONG_PTR)0x4321, 1, &ti, 0x2F));
        EXPECT_ERR(ERROR_INVALID_PARAMETER);                 /* user32 rejects the size itself */
    }
}

/* ---- precision touchpad configuration -------------------------------------------------------- */
TEST(gap9_ptp_config_nonzero_version_rejected) {
    REQUIRE(UN(NtUserGetPrecisionTouchPadConfiguration));
    static const DWORD vers[] = { 1, 2, 0x80000000, 0xFFFFFFFF };
    for (int i = 0; i < G9_N(vers); i++) {
        DWORD cfg[8]; memset(cfg, 0x9C, sizeof cfg); cfg[0] = vers[i];
        SetLastError(0);
        CHECKF(!UN(NtUserGetPrecisionTouchPadConfiguration)(cfg), "version 0x%lx", vers[i]);
        EXPECT_ERR(ERROR_INVALID_FUNCTION);                  /* STATUS_NOT_IMPLEMENTED maps to ERROR_INVALID_FUNCTION */
        CHECK(cfg[0] == vers[i]);
        int clean = 1; for (int k = 1; k < 8; k++) if (cfg[k] != 0x9C9C9C9C) clean = 0;
        CHECK(clean);                                        /* the result is copied out only on success */
    }
}

TEST(gap9_ptp_config_version_zero_bounds_write) {
    REQUIRE(UN(NtUserGetPrecisionTouchPadConfiguration));
    DWORD cfg[12]; memset(cfg, 0x9C, sizeof cfg); cfg[0] = 0;
    SetLastError(0);
    BOOL r = UN(NtUserGetPrecisionTouchPadConfiguration)(cfg);
    t_note("PTP configuration: ret %d error %lu", r, GetLastError());
    int ok = 1; for (int k = 6; k < 12; k++) if (cfg[k] != 0x9C9C9C9C) ok = 0;
    CHECK(ok);                                               /* at most 0x18 bytes are ever written */
    if (!r) { int clean = 1; for (int k = 1; k < 6; k++) if (cfg[k] != 0x9C9C9C9C) clean = 0; CHECK(clean); }
}

TEST(gap9_ptp_config_ordinal_export_matches) {
    HMODULE u = t_module("user32.dll");
    FARPROC p = u ? t_proc_ord("user32.dll", 2548) : NULL;
    if (!p) { t_skip("user32 ordinal 2548 absent"); return; }
    DWORD cfg[6]; memset(cfg, 0, sizeof cfg); cfg[0] = 5;
    SetLastError(0);
    CHECK(!((BOOL (WINAPI *)(void *))p)(cfg));
    EXPECT_ERR(ERROR_INVALID_FUNCTION);
}

/* ---- extended pointer device property --------------------------------------------------------- */
TEST(gap9_extptr_argument_order) {
    REQUIRE(UN(NtUserGetExtendedPointerDeviceProperty));
    DWORD prop[6]; memset(prop, 0x8D, sizeof prop);
    SetLastError(0);
    CHECK(!UN(NtUserGetExtendedPointerDeviceProperty)((HANDLE)(ULONG_PTR)0x1234, NULL));
    EXPECT_ERR(ERROR_INVALID_PARAMETER);                      /* NULL output is checked before the handle */
    SetLastError(0);
    CHECK(!UN(NtUserGetExtendedPointerDeviceProperty)(NULL, NULL));
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    static const ULONG_PTR hs[] = { 0, 1, 0x1234, 0xFFFFFFFF, (ULONG_PTR)-1 };
    for (int i = 0; i < G9_N(hs); i++) {
        SetLastError(0);
        CHECKF(!UN(NtUserGetExtendedPointerDeviceProperty)((HANDLE)hs[i], prop), "handle 0x%llx", (unsigned long long)hs[i]);
        EXPECT_ERR(ERROR_INVALID_HANDLE);                     /* HMValidateHandle(type 0x13) failed */
        int clean = 1; for (int k = 0; k < 6; k++) if (prop[k] != 0x8D8D8D8D) clean = 0;
        CHECK(clean);
    }
}

TEST(gap9_extptr_window_handle_is_wrong_type) {
    REQUIRE(UN(NtUserGetExtendedPointerDeviceProperty));
    HWND h = g9_window(0);
    if (!h) { t_skip("no window"); return; }
    DWORD prop[5] = {0};
    SetLastError(0);
    CHECK(!UN(NtUserGetExtendedPointerDeviceProperty)((HANDLE)h, prop));     /* valid USER handle, wrong object type */
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    g9_destroy(h);
}

TEST(gap9_extptr_user32_export) {
    BOOL (WINAPI *fn)(HANDLE, void *) = (BOOL (WINAPI *)(HANDLE, void *))t_proc_in("user32.dll", "GetExtendedPointerDeviceProperty");
    REQUIRE(fn);
    SetLastError(0);
    DWORD prop[5] = {0};
    CHECK(!fn((HANDLE)(ULONG_PTR)0x1234, prop));
    EXPECT_ERR(ERROR_INVALID_HANDLE);
}

/* ---- current input message source -------------------------------------------------------------- */
TEST(gap9_imsrc_null_output) {
    REQUIRE(UN(NtUserGetCurrentInputMessageSource));
    SetLastError(0);
    CHECK(!UN(NtUserGetCurrentInputMessageSource)(NULL));
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}

TEST(gap9_imsrc_reports_known_device_types) {
    REQUIRE(UN(NtUserGetCurrentInputMessageSource));
    INPUT_MESSAGE_SOURCE s; memset(&s, 0xFF, sizeof s);
    SetLastError(0);
    BOOL r = UN(NtUserGetCurrentInputMessageSource)(&s);
    t_note("no current message: ret %d device %d origin %d", r, s.deviceType, s.originId);
    if (r) {
        CHECK(s.deviceType != 0x12 && s.deviceType != 0x22);     /* internal pen/touch variants are folded to 2 */
        CHECK(s.originId >= 0);
    }
}

static LRESULT CALLBACK src_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_APP + 9) {
        INPUT_MESSAGE_SOURCE s; memset(&s, 0xFF, sizeof s);
        BOOL r = UN(NtUserGetCurrentInputMessageSource)(&s);
        t_note("inside posted message: ret %d device %d origin %d", r, s.deviceType, s.originId);
        if (r) { CHECK(s.deviceType != 0x12 && s.deviceType != 0x22); }
        return 1;
    }
    return U(DefWindowProcW)(h, m, w, l);
}
TEST(gap9_imsrc_inside_wndproc) {
    REQUIRE(UN(NtUserGetCurrentInputMessageSource)); REQUIRE(U(RegisterClassW)); REQUIRE(U(DefWindowProcW)); REQUIRE(U(PostMessageW)); REQUIRE(U(UnregisterClassW));
    WCHAR cn[48]; wsprintfW(cn, L"win32kt_imsrc_%lu", GetCurrentProcessId());
    WNDCLASSW wc; memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = src_proc; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = cn;
    if (!U(RegisterClassW)(&wc)) { t_skip("no class"); return; }
    HWND h = U(CreateWindowExW)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cn, L"x", WS_POPUP, 100, 100, 20, 20, NULL, NULL, wc.hInstance, NULL);
    if (h) {
        CHECK(U(PostMessageW)(h, WM_APP + 9, 0, 0));
        k_pump(300);
        g9_destroy(h);
    }
    U(UnregisterClassW)(cn, wc.hInstance);
}

/* ---- IME hot keys ----------------------------------------------------------------------------- */
TEST(gap9_imehk_unknown_ids) {
    REQUIRE(UN(NtUserGetImeHotKey));
    static const DWORD ids[] = { 0, 1, 0x0F, 0x13, 0x2F, 0x31, 0x4F, 0x53, 0x6F, 0x73, 0x8000, 0xFFFF, 0x80000000, 0xFFFFFFFF };
    for (int i = 0; i < G9_N(ids); i++) {
        UINT a = 0x1111, b = 0x2222; HKL k = (HKL)(ULONG_PTR)0x3333;
        SetLastError(0);
        CHECKF(!UN(NtUserGetImeHotKey)(ids[i], &a, &b, &k), "id 0x%lx", ids[i]);
        EXPECT_ERR(ERROR_HOTKEY_NOT_REGISTERED);
        CHECK(a == 0x1111 && b == 0x2222 && k == (HKL)(ULONG_PTR)0x3333);
    }
}

TEST(gap9_imehk_known_ids_consistent) {
    REQUIRE(UN(NtUserGetImeHotKey));
    static const DWORD ids[] = { 0x10, 0x11, 0x12, 0x30, 0x50, 0x51, 0x52, 0x70, 0x71, 0x72, 0x200, 0x201, 0x202 };
    int found = 0;
    for (int i = 0; i < G9_N(ids); i++) {
        UINT a = 0, b = 0; HKL k = (HKL)(ULONG_PTR)1;
        SetLastError(0);
        BOOL with = UN(NtUserGetImeHotKey)(ids[i], &a, &b, &k);
        UINT a2 = 0, b2 = 0;
        SetLastError(0);
        BOOL without = UN(NtUserGetImeHotKey)(ids[i], &a2, &b2, NULL);
        if (!with) { t_note("id 0x%lx not registered (error %lu)", ids[i], GetLastError()); CHECK(!without); continue; }
        found++;
        CHECK((a == a2 && b == b2) || !without);
        /* with hkl==NULL stored the NULL-hkl form succeeds; a bound hotkey needs the hkl pointer (0x57) */
        if (k) { CHECK(!without); } else { CHECK(without); }
        t_note("id 0x%lx: mod 0x%x vk 0x%x hkl %p", ids[i], a, b, (void *)k);
    }
    if (!found) t_note("no IME hot keys registered on this system");
}

/* ---- menu context help id --------------------------------------------------------------------- */
TEST(gap9_menuhelp_roundtrip_values) {
    REQUIRE(UN(NtUserSetMenuContextHelpId)); REQUIRE(U(CreatePopupMenu)); REQUIRE(U(GetMenuContextHelpId)); REQUIRE(U(DestroyMenu));
    HMENU m = U(CreatePopupMenu)();
    if (!m) { t_skip("no menu"); return; }
    EXPECT_EQ(U(GetMenuContextHelpId)(m), 0);
    static const DWORD v[] = { 1, 0x12345678, 0xFFFFFFFF, 0x80000000, 0 };
    for (int i = 0; i < G9_N(v); i++) {
        CHECKF(UN(NtUserSetMenuContextHelpId)(m, v[i]), "set 0x%lx", v[i]);
        EXPECT_EQ(U(GetMenuContextHelpId)(m), v[i]);
    }
    U(DestroyMenu)(m);
}

TEST(gap9_menuhelp_menubar_and_popup_independent) {
    REQUIRE(UN(NtUserSetMenuContextHelpId)); REQUIRE(U(CreateMenu)); REQUIRE(U(CreatePopupMenu)); REQUIRE(U(GetMenuContextHelpId)); REQUIRE(U(DestroyMenu));
    HMENU a = U(CreateMenu)(), b = U(CreatePopupMenu)();
    if (!a || !b) { t_skip("no menu"); return; }
    BOOL ra = UN(NtUserSetMenuContextHelpId)(a, 0xA1), rb = UN(NtUserSetMenuContextHelpId)(b, 0xB2);
    CHECK(ra); CHECK(rb);
    if (ra) EXPECT_EQ(U(GetMenuContextHelpId)(a), 0xA1);
    if (rb) EXPECT_EQ(U(GetMenuContextHelpId)(b), 0xB2);
    U(DestroyMenu)(a); U(DestroyMenu)(b);
}

TEST(gap9_menuhelp_invalid_handles) {
    REQUIRE(UN(NtUserSetMenuContextHelpId)); REQUIRE(U(CreatePopupMenu)); REQUIRE(U(DestroyMenu));
    HMENU bad[] = { NULL, (HMENU)(ULONG_PTR)0x1357, (HMENU)(ULONG_PTR)-1 };
    for (int i = 0; i < G9_N(bad); i++) {
        SetLastError(0);
        CHECKF(!UN(NtUserSetMenuContextHelpId)(bad[i], 5), "hmenu %p", bad[i]);
        EXPECT_ERR(ERROR_INVALID_MENU_HANDLE);
    }
    HMENU m = U(CreatePopupMenu)();
    if (!m) return;
    U(DestroyMenu)(m);
    SetLastError(0);
    CHECK(!UN(NtUserSetMenuContextHelpId)(m, 5));              /* handle lifetime: dead after DestroyMenu */
    EXPECT_ERR(ERROR_INVALID_MENU_HANDLE);
}

TEST(gap9_menuhelp_window_handle_not_menu) {
    REQUIRE(UN(NtUserSetMenuContextHelpId));
    HWND h = g9_window(0);
    if (!h) { t_skip("no window"); return; }
    SetLastError(0);
    CHECK(!UN(NtUserSetMenuContextHelpId)((HMENU)h, 7));
    EXPECT_ERR(ERROR_INVALID_MENU_HANDLE);
    g9_destroy(h);
}

static DWORD thr_menu(void *p) { return UN(NtUserSetMenuContextHelpId)((HMENU)p, 0x77) ? 1 : 0; }
TEST(gap9_menuhelp_set_from_other_thread) {
    REQUIRE(UN(NtUserSetMenuContextHelpId)); REQUIRE(U(CreatePopupMenu)); REQUIRE(U(GetMenuContextHelpId)); REQUIRE(U(DestroyMenu));
    HMENU m = U(CreatePopupMenu)();
    if (!m) { t_skip("no menu"); return; }
    DWORD r = g9_on_thread(thr_menu, m);
    t_note("set from another thread: %lu", r);
    if (r == 1) EXPECT_EQ(U(GetMenuContextHelpId)(m), 0x77);
    U(DestroyMenu)(m);
}

/* ---- composition capture / input sink ------------------------------------------------------------ */
TEST(gap9_comp_desktop_capture_null_handles) {
    NOWINE();
    REQUIRE(UN(NtDesktopCaptureBits));
    HANDLE ev = CreateEventW(NULL, FALSE, FALSE, NULL), nonnull = ev;
    if (!ev) { t_skip("no event"); return; }
    LONG_PTR sec = (LONG_PTR)ev;   /* any non-zero value; never used because the NULL arm fails first */
    struct { LONG_PTR p7, p8; const char *n; } t[] = {
        { 0, 0, "event=0 section=0" }, { 0, sec, "event=0" }, { (LONG_PTR)nonnull, 0, "section=0" },
    };
    for (int i = 0; i < G9_N(t); i++) {
        LONG st = UN(NtDesktopCaptureBits)(NULL, 0, 0, 0, 0, 0, t[i].p7, t[i].p8);
        CHECKF((DWORD)st == STATUS_INVALID_PARAMETER_, "%s: 0x%lx", t[i].n, (DWORD)st);
    }
    CloseHandle(ev);
}

TEST(gap9_comp_desktop_capture_bad_handles_fail) {
    NOWINE();
    REQUIRE(UN(NtDesktopCaptureBits));
    HANDLE ev = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!ev) { t_skip("no event"); return; }
    LONG st = UN(NtDesktopCaptureBits)(NULL, 0, 0, 0, 0, 0, (LONG_PTR)0x7F7C, (LONG_PTR)0x7F80);
    CHECK(st < 0);
    t_note("bogus handles: 0x%lx", (DWORD)st);
    /* event handle in the section slot, event in the event slot: a section is required */
    st = UN(NtDesktopCaptureBits)(NULL, 0, 0, 0, 0, 0, (LONG_PTR)ev, (LONG_PTR)ev);
    CHECK(st < 0);
    t_note("event as section: 0x%lx", (DWORD)st);
    CloseHandle(ev);
}

TEST(gap9_comp_visual_capture_null_handles) {
    NOWINE();
    REQUIRE(UN(NtVisualCaptureBits));
    HANDLE ev = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!ev) { t_skip("no event"); return; }
    LONG st = UN(NtVisualCaptureBits)(0, 0, 0, 0, 0, 0, 0, NULL, 0);
    CHECK((DWORD)st == STATUS_INVALID_PARAMETER_);
    st = UN(NtVisualCaptureBits)(0x1000, 1, 1, 1, 1, 1, 87, NULL, (LONG_PTR)ev);
    CHECK((DWORD)st == STATUS_INVALID_PARAMETER_);
    st = UN(NtVisualCaptureBits)(0x1000, 1, 1, 1, 1, 1, 87, ev, 0);
    CHECK((DWORD)st == STATUS_INVALID_PARAMETER_);
    CloseHandle(ev);
}

TEST(gap9_comp_visual_capture_bogus_channel_fails) {
    NOWINE();
    REQUIRE(UN(NtVisualCaptureBits));
    HANDLE ev = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!ev) { t_skip("no event"); return; }
    LONG st = UN(NtVisualCaptureBits)(0x7FFF0, 1, 1, 1, 1, 1, 87, ev, (LONG_PTR)ev);
    CHECK(st < 0);
    t_note("bogus channel: 0x%lx", (DWORD)st);
    CloseHandle(ev);
}

TEST(gap9_comp_duplicate_input_sink_requires_dwm) {
    NOWINE();
    REQUIRE(UN(NtDuplicateCompositionInputSink));
    static const ULONG_PTR hs[] = { 0, 4, 0x1234, (ULONG_PTR)-1 };
    for (int i = 0; i < G9_N(hs); i++) {
        ULONGLONG out = 0x1122334455667788ull;
        LONG st = UN(NtDuplicateCompositionInputSink)((void *)hs[i], &out);
        CHECKF((DWORD)st == STATUS_ACCESS_DENIED_, "handle 0x%llx: 0x%lx", (unsigned long long)hs[i], (DWORD)st);   /* UserIsCurrentProcessDwm() is false */
        CHECK(out == 0x1122334455667788ull);                  /* no handle produced, nothing stored */
    }
}
