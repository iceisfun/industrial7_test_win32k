/* Input injection entry points: win32kbase!NtUserInject{Keyboard,Mouse,Device,GenericHid}Input,
 * NtUserInitialize{GenericHid,InputDevice,PointerDevice}Injection(Ex), NtUserRemoveInjectionDevice.
 *
 * All of them are gated by RIMIDECheckCurrentProcessForInjectionPermissions, which is
 * RtlCapabilityCheck(NULL, L"inputInjection"): a classic desktop process lacks the capability and gets
 * ERROR_ACCESS_DENIED (5) with a FALSE result and no input generated. The count/size validation of the
 * Keyboard/Mouse/Device variants runs BEFORE the gate, the Initialize* variants run the gate first.
 * If a host does grant the capability the tests switch to the post-gate expectations, and the payloads they
 * send are inert (no key, no mouse movement bits).
 */
#include "r2ip.h"
#include <stdlib.h>

static int denied_expected(void) { return r2_injection_permitted() != 1; }   /* -1 (unknown) counts as "not permitted" */

/* a harmless KEYBDINPUT (24 bytes on x64): VK 0xFF is the reserved "no key" code, key-up only */
static void inert_key(unsigned char *b) {
    memset(b, 0, 24);
    *(WORD *)(b + 0) = 0xFF;
    *(DWORD *)(b + 4) = KEYEVENTF_KEYUP;
}
/* a harmless MOUSEINPUT (32 bytes): dwFlags 0 -> neither move nor button */
static void inert_mouse(unsigned char *b) { memset(b, 0, 32); }

RT(inject_probe_is_side_effect_free_and_stable) {
    REQUIRE(UNW(NtUserInitializeInputDeviceInjection));
    int a = r2_injection_permitted(), b = r2_injection_permitted();
    EXPECT_EQ(a, b);
    t_note("inputInjection capability granted: %d", a);
    if (a == 0) {
        /* denied: no last-error other than 5, result FALSE, output handle not written */
        HANDLE out = (HANDLE)(ULONG_PTR)0x5a5a5a5a;
        SET_ERR(0);
        LONG r = UNW(NtUserInitializeInputDeviceInjection)(0, 0, NULL, 4, NULL, 1, &out);
        EXPECT_EQ(r, 0); EXPECT_ERR(E_ACCESS); EXPECT_EQ(out, 0x5a5a5a5a);
    }
}

/* ---- NtUserInjectKeyboardInput ---------------------------------------------------------- */
/* win32kbase: (count - 1) > 0xF -> ERROR_INVALID_PARAMETER, checked before the capability. */

RT(inject_keyboard_count_zero_is_invalid_parameter) {
    REQUIRE(UNW(NtUserInjectKeyboardInput));
    unsigned char b[24 * 2]; inert_key(b);
    SET_ERR(0);
    EXPECT_EQ(UNW(NtUserInjectKeyboardInput)(b, 0), 0);
    EXPECT_ERR(E_INVALID_PARAM);
}
RT(inject_keyboard_count_over_16_is_invalid_parameter) {
    REQUIRE(UNW(NtUserInjectKeyboardInput));
    unsigned char b[24 * 20]; for (int i = 0; i < 20; i++) inert_key(b + 24 * i);
    static const UINT counts[] = { 17, 18, 32, 0x10000, 0xFFFFFFFFu };
    for (size_t i = 0; i < sizeof counts / sizeof *counts; i++) {
        SET_ERR(0);
        CHECKF(UNW(NtUserInjectKeyboardInput)(b, counts[i]) == 0, "count %u", counts[i]);
        EXPECT_ERR(E_INVALID_PARAM);
    }
}
RT(inject_keyboard_count_check_precedes_capability_check) {
    REQUIRE(UNW(NtUserInjectKeyboardInput));
    SET_ERR(0);
    EXPECT_EQ(UNW(NtUserInjectKeyboardInput)(NULL, 0), 0);    /* the buffer is not touched when the count is bad */
    EXPECT_ERR(E_INVALID_PARAM);
}
RT(inject_keyboard_valid_count_requires_capability) {
    REQUIRE(UNW(NtUserInjectKeyboardInput));
    unsigned char b[24 * 16]; for (int i = 0; i < 16; i++) inert_key(b + 24 * i);
    for (UINT n = 1; n <= 16; n += (n == 1 ? 14 : 1)) {   /* 1, 15, 16 */
        SET_ERR(0);
        LONG r = UNW(NtUserInjectKeyboardInput)(b, n);
        DWORD e = GetLastError();
        if (denied_expected()) { CHECKF(r == 0 && e == E_ACCESS, "n=%u r=%ld e=%lu", n, r, e); }
        else t_note("InjectKeyboardInput(n=%u) with capability -> %ld gle=%lu", n, r, e);
    }
}

/* ---- NtUserInjectMouseInput ------------------------------------------------------------- */

RT(inject_mouse_count_zero_is_invalid_parameter) {
    REQUIRE(UNW(NtUserInjectMouseInput));
    unsigned char b[32]; inert_mouse(b);
    SET_ERR(0);
    EXPECT_EQ(UNW(NtUserInjectMouseInput)(b, 0), 0);
    EXPECT_ERR(E_INVALID_PARAM);
}
RT(inject_mouse_count_over_16_is_invalid_parameter) {
    REQUIRE(UNW(NtUserInjectMouseInput));
    unsigned char b[32 * 20]; memset(b, 0, sizeof b);
    static const UINT counts[] = { 17, 24, 0x100, 0x7FFFFFFFu, 0xFFFFFFFFu };
    for (size_t i = 0; i < sizeof counts / sizeof *counts; i++) {
        SET_ERR(0);
        CHECKF(UNW(NtUserInjectMouseInput)(b, counts[i]) == 0, "count %u", counts[i]);
        EXPECT_ERR(E_INVALID_PARAM);
    }
}
RT(inject_mouse_valid_count_requires_capability) {
    REQUIRE(UNW(NtUserInjectMouseInput));
    unsigned char b[32 * 16]; memset(b, 0, sizeof b);
    for (UINT n = 1; n <= 16; n += (n == 1 ? 15 : 1)) {   /* 1, 16 */
        SET_ERR(0);
        LONG r = UNW(NtUserInjectMouseInput)(b, n);
        DWORD e = GetLastError();
        if (denied_expected()) { CHECKF(r == 0 && e == E_ACCESS, "n=%u r=%ld e=%lu", n, r, e); }
        else t_note("InjectMouseInput(n=%u) with capability -> %ld gle=%lu", n, r, e);
    }
}
RT(inject_mouse_does_not_move_cursor) {
    REQUIRE(UNW(NtUserInjectMouseInput) && U(GetCursorPos));
    POINT a, b; unsigned char m[32]; inert_mouse(m);
    CHECK(U(GetCursorPos)(&a));
    UNW(NtUserInjectMouseInput)(m, 1);
    CHECK(U(GetCursorPos)(&b));
    CHECK(a.x == b.x && a.y == b.y);        /* denied, or an inert MOUSEINPUT with no MOUSEEVENTF_MOVE */
}

/* ---- NtUserInjectDeviceInput ------------------------------------------------------------ */
/* count == 0 -> 0x57 first; then the capability (the variant that also honours the edition's
 * "RIM injection blocked" switch); then handle resolution -> ERROR_INVALID_HANDLE (6). */

RT(inject_deviceinput_count_zero_is_invalid_parameter) {
    REQUIRE(UNW(NtUserInjectDeviceInput));
    unsigned char b[12] = { 0 };
    SET_ERR(0);
    EXPECT_EQ(UNW(NtUserInjectDeviceInput)(NULL, b, 0), 0);
    EXPECT_ERR(E_INVALID_PARAM);
    SET_ERR(0);
    EXPECT_EQ(UNW(NtUserInjectDeviceInput)((HANDLE)(ULONG_PTR)8, NULL, 0), 0);
    EXPECT_ERR(E_INVALID_PARAM);
}
RT(inject_deviceinput_nonzero_count_checks_capability_then_handle) {
    REQUIRE(UNW(NtUserInjectDeviceInput));
    unsigned char b[12 * 2] = { 0 };
    HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    HANDLE hs[3] = { NULL, (HANDLE)(ULONG_PTR)0x7ffc, ev };
    for (int i = 0; i < 3; i++) {
        SET_ERR(0);
        LONG r = UNW(NtUserInjectDeviceInput)(hs[i], b, 1);
        DWORD e = GetLastError();
        CHECKF(r == 0, "case %d r=%ld", i, r);
        if (denied_expected()) CHECKF(e == E_ACCESS, "case %d e=%lu", i, e);
        else CHECKF(e == E_INVALID_HANDLE, "case %d e=%lu", i, e);     /* RawInputManagerDeviceObjectResolveHandle failed */
    }
    CloseHandle(ev);
}

/* ---- NtUserInjectGenericHidInput -------------------------------------------------------- */

RT(inject_generichid_requires_capability_first) {
    REQUIRE(UNW(NtUserInjectGenericHidInput));
    unsigned char b[8] = { 0 };
    SET_ERR(0);
    LONG r = UNW(NtUserInjectGenericHidInput)(NULL, b, 0);
    DWORD e = GetLastError();
    EXPECT_EQ(r, 0);
    if (denied_expected()) EXPECT_EQ(e, E_ACCESS);            /* the gate precedes the cb == 0 check */
    else EXPECT_EQ(e, E_INVALID_PARAM);                       /* cb == 0 */
}
RT(inject_generichid_bad_handle) {
    REQUIRE(UNW(NtUserInjectGenericHidInput));
    unsigned char b[8] = { 0 };
    HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    HANDLE hs[3] = { NULL, (HANDLE)(ULONG_PTR)0x7ffc, ev };
    for (int i = 0; i < 3; i++) {
        SET_ERR(0);
        LONG r = UNW(NtUserInjectGenericHidInput)(hs[i], b, 8);
        DWORD e = GetLastError();
        CHECKF(r == 0, "case %d r=%ld", i, r);
        CHECKF(e == (denied_expected() ? E_ACCESS : E_INVALID_HANDLE), "case %d e=%lu", i, e);
    }
    CloseHandle(ev);
}

/* ---- NtUserInitializeGenericHidInjection ------------------------------------------------ */

RT(inject_init_generichid_denied_or_invalid) {
    REQUIRE(UNW(NtUserInitializeGenericHidInjection));
    unsigned char desc[0x40]; memset(desc, 0, sizeof desc);     /* NULL report descriptor -> 0x57 after the gate */
    HANDLE out = (HANDLE)(ULONG_PTR)0x5a5a5a5a;
    SET_ERR(0);
    LONG r = UNW(NtUserInitializeGenericHidInjection)(desc, &out);
    DWORD e = GetLastError();
    EXPECT_EQ(r & 0xFF, 0);
    EXPECT_EQ(out, 0x5a5a5a5a);
    if (denied_expected()) EXPECT_EQ(e, E_ACCESS);
    else EXPECT_EQ(e, E_INVALID_PARAM);
}
RT(inject_init_generichid_denied_does_not_read_descriptor) {
    REQUIRE(UNW(NtUserInitializeGenericHidInjection));
    if (!denied_expected()) { t_skip("inputInjection is granted to this process on this system, the denied path is not reachable: the descriptor would be read"); return; }
    HANDLE out = (HANDLE)(ULONG_PTR)0x5a5a5a5a;
    SET_ERR(0);
    LONG r = UNW(NtUserInitializeGenericHidInjection)(NULL, &out);    /* gate is checked before the 0x40-byte descriptor is probed */
    EXPECT_EQ(r & 0xFF, 0);
    EXPECT_ERR(E_ACCESS);
    EXPECT_EQ(out, 0x5a5a5a5a);
}

/* ---- NtUserInitializeInputDeviceInjection ----------------------------------------------- */

RT(inject_init_inputdevice_gate_precedes_validation) {
    REQUIRE(UNW(NtUserInitializeInputDeviceInjection));
    HANDLE out = (HANDLE)(ULONG_PTR)0x5a5a5a5a;
    SET_ERR(0);
    LONG r = UNW(NtUserInitializeInputDeviceInjection)(0, 0, NULL, 0, NULL, 99, &out);
    DWORD e = GetLastError();
    EXPECT_EQ(r, 0);
    EXPECT_EQ(e, denied_expected() ? E_ACCESS : E_INVALID_PARAM);
    EXPECT_EQ(out, 0x5a5a5a5a);
}
RT(inject_init_inputdevice_mode_out_of_range) {
    REQUIRE(UNW(NtUserInitializeInputDeviceInjection));
    static const int modes[] = { 0, 4, 5, -1 };
    unsigned char items[32] = { 0 };
    for (size_t i = 0; i < sizeof modes / sizeof *modes; i++) {
        HANDLE out = (HANDLE)(ULONG_PTR)0x5a5a5a5a;
        SET_ERR(0);
        LONG r = UNW(NtUserInitializeInputDeviceInjection)(1, 2, items, 1, NULL, modes[i], &out);   /* mode - 1 must be < 3 */
        CHECKF(r == 0, "mode %d r=%ld", modes[i], r);
        EXPECT_ERR(denied_expected() ? E_ACCESS : E_INVALID_PARAM);
        EXPECT_EQ(out, 0x5a5a5a5a);
    }
}

/* ---- NtUserInitializePointerDeviceInjection(Ex) ------------------------------------------ */

RT(inject_init_pointerex_denied_or_invalid_type) {
    REQUIRE(UNW(NtUserInitializePointerDeviceInjectionEx));
    /* device type must be 2 (touch) or 3 (pen); anything else is 0x57 after the gate */
    static const int types[] = { 0, 1, 4, 5, -1, 0x100 };
    for (size_t i = 0; i < sizeof types / sizeof *types; i++) {
        HANDLE out = (HANDLE)(ULONG_PTR)0x5a5a5a5a;
        SET_ERR(0);
        LONG r = UNW(NtUserInitializePointerDeviceInjectionEx)(types[i], 1, NULL, 1, 0, &out);
        CHECKF((r & 0xFF) == 0 || r == 0, "type %d r=%ld", types[i], r);
        EXPECT_ERR(denied_expected() ? E_ACCESS : E_INVALID_PARAM);
        EXPECT_EQ(out, 0x5a5a5a5a);
    }
}
RT(inject_init_pointerex_contact_and_mode_ranges) {
    REQUIRE(UNW(NtUserInitializePointerDeviceInjectionEx));
    struct { int type, contacts, mode; } c[] = {
        { 3, 0, 1 },      /* pen: exactly 1 contact */
        { 3, 2, 1 },
        { 3, 1, 0 },      /* mode - 1 must be < 3 */
        { 3, 1, 4 },
        { 2, 0, 1 },      /* touch: 1..256 contacts */
        { 2, 257, 1 },
        { 2, 1, 0 },
        { 2, 1, 4 },
    };
    for (size_t i = 0; i < sizeof c / sizeof *c; i++) {
        HANDLE out = (HANDLE)(ULONG_PTR)0x5a5a5a5a;
        SET_ERR(0);
        LONG r = UNW(NtUserInitializePointerDeviceInjectionEx)(c[i].type, c[i].contacts, NULL, c[i].mode, 0, &out);
        CHECKF((r & 0xFF) == 0, "case %u r=%ld", (unsigned)i, r);
        EXPECT_ERR(denied_expected() ? E_ACCESS : E_INVALID_PARAM);
        EXPECT_EQ(out, 0x5a5a5a5a);
    }
}
RT(inject_init_pointer_legacy_forwards_to_ex) {
    REQUIRE(UNW(NtUserInitializePointerDeviceInjection));
    HANDLE out = (HANDLE)(ULONG_PTR)0x5a5a5a5a;
    SET_ERR(0);
    LONG r = UNW(NtUserInitializePointerDeviceInjection)(9, 1, NULL, 1, 0, &out);   /* NtUserInitializePointerDeviceInjection tail-calls the Ex body */
    EXPECT_EQ(r & 0xFF, 0);
    EXPECT_ERR(denied_expected() ? E_ACCESS : E_INVALID_PARAM);
    EXPECT_EQ(out, 0x5a5a5a5a);
}
RT(inject_init_pointer_bad_shared_handle) {
    REQUIRE(UNW(NtUserInitializePointerDeviceInjectionEx));
    if (!denied_expected()) { t_skip("inputInjection is granted to this process on this system, the denied path is not reachable: a valid request would create a device"); return; }
    HANDLE out = (HANDLE)(ULONG_PTR)0x5a5a5a5a;
    SET_ERR(0);
    EXPECT_EQ(UNW(NtUserInitializePointerDeviceInjectionEx)(3, 1, (HANDLE)(ULONG_PTR)0x1234, 1, 0, &out) & 0xFF, 0);
    EXPECT_ERR(E_ACCESS);
    EXPECT_EQ(out, 0x5a5a5a5a);
}

/* ---- NtUserInitializeTouchInjection: parameter validation (no gate before it) ------------- */
/* win32kfull: (contacts - 1) < 0x100 && (mode - 1) < 3, else ERROR_INVALID_PARAMETER */

RT(inject_init_touch_invalid_contacts) {
    REQUIRE(UNW(NtUserInitializeTouchInjection));
    static const UINT contacts[] = { 0, 257, 0x1000, 0xFFFFFFFFu };
    for (size_t i = 0; i < sizeof contacts / sizeof *contacts; i++) {
        SET_ERR(0);
        CHECKF((UNW(NtUserInitializeTouchInjection)(contacts[i], 1) & 0xFF) == 0, "contacts %u", contacts[i]);
        EXPECT_ERR(E_INVALID_PARAM);
    }
}
RT(inject_init_touch_invalid_mode) {
    REQUIRE(UNW(NtUserInitializeTouchInjection));
    static const DWORD modes[] = { 0, 4, 5, 0x80000000u, 0xFFFFFFFFu };
    for (size_t i = 0; i < sizeof modes / sizeof *modes; i++) {
        SET_ERR(0);
        CHECKF((UNW(NtUserInitializeTouchInjection)(1, modes[i]) & 0xFF) == 0, "mode %lu", modes[i]);
        EXPECT_ERR(E_INVALID_PARAM);
    }
}

/* ---- NtUserRemoveInjectionDevice --------------------------------------------------------- */
/* No capability gate: RawInputManagerDeviceObjectResolveHandle failure is ERROR_INVALID_HANDLE. */

RT(inject_remove_device_bad_handles) {
    REQUIRE(UNW(NtUserRemoveInjectionDevice));
    HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    HANDLE closed = CreateEventW(NULL, TRUE, FALSE, NULL);
    CloseHandle(closed);
    HANDLE hs[4] = { NULL, (HANDLE)(ULONG_PTR)0x7ffc, ev, closed };
    for (int i = 0; i < 4; i++) {
        SET_ERR(0);
        LONG r = UNW(NtUserRemoveInjectionDevice)(hs[i]);
        CHECKF((r & 0xFF) == 0, "case %d r=%ld", i, r);
        EXPECT_ERR(E_INVALID_HANDLE);
    }
    DWORD fl;
    CHECK(GetHandleInformation(ev, &fl));       /* a failed removal does not close the caller's handle */
    CloseHandle(ev);
}

/* ---- DeviceInput never returns TRUE for a non-device ------------------------------------- */
RT(inject_all_variants_fail_for_unprivileged_caller_without_side_effects) {
    REQUIRE(UNW(NtUserInjectKeyboardInput) && UNW(NtUserInjectMouseInput) && U(GetAsyncKeyState));
    if (!denied_expected()) { t_skip("inputInjection is granted to this process on this system, the denied path is not reachable"); return; }
    SHORT before = U(GetAsyncKeyState)(0xFF);
    unsigned char k[24], m[32]; inert_key(k); inert_mouse(m);
    SET_ERR(0); EXPECT_EQ(UNW(NtUserInjectKeyboardInput)(k, 1), 0); EXPECT_ERR(E_ACCESS);
    SET_ERR(0); EXPECT_EQ(UNW(NtUserInjectMouseInput)(m, 1), 0); EXPECT_ERR(E_ACCESS);
    SHORT after = U(GetAsyncKeyState)(0xFF);
    EXPECT_EQ(before, after);
}
