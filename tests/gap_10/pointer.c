/* Pointer-input syscalls: NtUserGetPointerType / GetPointerProprietaryId / GetPointerDeviceOrientation /
 * GetPointerInputTransform / GetPointerFrameTimes / RegisterPointerDeviceNotifications / GetGestureExtArgs.
 * No pointer is ever injected: only parameter validation and the always-present mouse pointer (id 1) are exercised. */
#include "g10.h"

/* ---- NtUserGetPointerType(id, &type): id must be non-zero with HIWORD 0, out non-NULL; id 1 is the mouse (PT_MOUSE=4) ---- */
TEST(gap10_ptrtype_mouse_pointer_id_is_pt_mouse) {
    REQUIRE(UN(NtUserGetPointerType)); if (t_is_wine()) { t_skip("Wine aborts in NtUserGetPointerType"); return; }
    DWORD t = 0xCDCDCDCD;
    BOOL r = UN(NtUserGetPointerType)(1, &t);
    CHECK(r);
    G10_EQ(t, 4);
}
TEST(gap10_ptrtype_public_api_agrees) {
    REQUIRE(UN(NtUserGetPointerType)); if (t_is_wine()) { t_skip("Wine aborts in NtUserGetPointerType"); return; }
    POINTER_INPUT_TYPE t = 0; DWORD d = 0;
    BOOL a = U(GetPointerType)(1, &t), b = UN(NtUserGetPointerType)(1, &d);
    CHECK(a == b);
    G10_EQ((DWORD)t, d);
}
TEST(gap10_ptrtype_invalid_ids_fail_with_invalid_parameter) {
    REQUIRE(UN(NtUserGetPointerType)); if (t_is_wine()) { t_skip("Wine aborts in NtUserGetPointerType"); return; }
    /* id 0 and any id with a non-zero HIWORD are rejected up front; unknown 16-bit ids fail in GetThreadPointerData */
    const UINT ids[] = { 0, 0x10000, 0x10001, 0xFFFF0001u, 0x80000000u, 0xFFFFFFFFu, 2, 0x1234, 0x7FFF, 0xFFFF };
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        DWORD t = 0xCDCDCDCD; SET_ERR(0);
        BOOL r = UN(NtUserGetPointerType)(ids[i], &t);
        CHECKF(!r, "id 0x%x unexpectedly succeeded", ids[i]);
        CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "id 0x%x: error %lu", ids[i], GetLastError());
        CHECKF(t == 0xCDCDCDCD, "id 0x%x: output written", ids[i]);
    }
}
TEST(gap10_ptrtype_null_output_pointer) {
    REQUIRE(UN(NtUserGetPointerType)); if (t_is_wine()) { t_skip("Wine aborts in NtUserGetPointerType"); return; }
    SET_ERR(0);
    BOOL r = UN(NtUserGetPointerType)(1, NULL);
    CHECK(!r);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SET_ERR(0);
    CHECK(!U(GetPointerType)(1, NULL));
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}
TEST(gap10_ptrtype_repeatable) {
    REQUIRE(UN(NtUserGetPointerType)); if (t_is_wine()) { t_skip("Wine aborts in NtUserGetPointerType"); return; }
    for (int i = 0; i < 20; i++) { DWORD t = 0; CHECK(UN(NtUserGetPointerType)(1, &t)); G10_EQ(t, 4); }
}

/* ---- NtUserGetPointerProprietaryId(id, GUID*): FALSE with ERROR_NO_DATA (0xe8) when the pointer has no proprietary id ---- */
TEST(gap10_ptrpropid_unknown_pointers_report_no_data) {
    REQUIRE(UN(NtUserGetPointerProprietaryId)); if (t_is_wine()) { t_skip("Wine aborts in NtUserGetPointerProprietaryId"); return; }
    const UINT ids[] = { 0, 1, 2, 0x1234, 0xFFFF, 0x10001, 0xFFFFFFFFu };
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        GUID g; memset(&g, 0xCD, sizeof g); SET_ERR(0);
        BOOL r = UN(NtUserGetPointerProprietaryId)(ids[i], &g);
        if (r) { t_note("id 0x%x has a proprietary id", ids[i]); continue; }
        CHECKF(GetLastError() == ERROR_NO_DATA, "id 0x%x: error %lu", ids[i], GetLastError());
        int untouched = 1;
        for (unsigned k = 0; k < sizeof g; k++) if (((unsigned char *)&g)[k] != 0xCD) untouched = 0;
        CHECKF(untouched, "id 0x%x: GUID written on failure", ids[i]);
    }
}

/* ---- NtUserGetPointerDeviceOrientation(hDevice, &rotation) ---- */
TEST(gap10_ptrorient_mouse_pseudo_handle_gives_a_rotation) {
    REQUIRE(UN(NtUserGetPointerDeviceOrientation)); if (t_is_wine()) { t_skip("Wine aborts in NtUserGetPointerDeviceOrientation"); return; }
    DWORD o = 0xCDCDCDCD;
    BOOL r = UN(NtUserGetPointerDeviceOrientation)((HANDLE)(LONG_PTR)-1, &o);
    CHECK(r);
    /* DISPLAYCONFIG_ROTATION_IDENTITY..ROTATE270 = 1..4 */
    CHECKF(o >= 1 && o <= 4, "orientation %lu", o);
}
TEST(gap10_ptrorient_invalid_device_handles) {
    REQUIRE(UN(NtUserGetPointerDeviceOrientation)); if (t_is_wine()) { t_skip("Wine aborts in NtUserGetPointerDeviceOrientation"); return; }
    const ULONG_PTR h[] = { 0, 0x1234, 0x10010, 0xFFFFFFFFull, 0x7FFFFFFFFFFFFFFFull };
    for (unsigned i = 0; i < sizeof h / sizeof h[0]; i++) {
        DWORD o = 0xCDCDCDCD; SET_ERR(0);
        BOOL r = UN(NtUserGetPointerDeviceOrientation)((HANDLE)h[i], &o);
        CHECKF(!r, "handle %p accepted", (void *)h[i]);
        CHECKF(GetLastError() == ERROR_INVALID_HANDLE, "handle %p: error %lu", (void *)h[i], GetLastError());
        CHECKF(o == 0xCDCDCDCD, "handle %p: output written", (void *)h[i]);
    }
}
TEST(gap10_ptrorient_window_handle_is_not_a_device) {
    REQUIRE(UN(NtUserGetPointerDeviceOrientation)); if (t_is_wine()) { t_skip("Wine aborts in NtUserGetPointerDeviceOrientation"); return; }
    HWND w = k_make_window("gap10-orient"); REQUIRE(w);
    DWORD o = 0xCDCDCDCD; SET_ERR(0);
    CHECK(!UN(NtUserGetPointerDeviceOrientation)((HANDLE)w, &o));   /* HMValidateHandle type 0x13 != window */
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    U(DestroyWindow)(w);
}

/* ---- NtUserGetPointerInputTransform(id, count, INPUT_TRANSFORM[count]) ---- */
static float g_xf[100 * 16];
TEST(gap10_ptrxform_validation_table) {
    REQUIRE(UN(NtUserGetPointerInputTransform)); if (t_is_wine()) { t_skip("Wine aborts in NtUserGetPointerInputTransform"); return; }
    static const struct { UINT id, count; int use_buf; const char *why; } t[] = {
        { 0, 1, 1, "id 0" }, { 0x10001, 1, 1, "HIWORD(id) set" }, { 0xFFFFFFFFu, 1, 1, "id -1" },
        { 2, 0, 1, "count 0" }, { 2, 101, 1, "count 101" }, { 2, 0xFFFFFFFFu, 1, "count -1" },
        { 2, 1, 0, "NULL buffer" }, { 1, 2, 1, "mouse id with count 2" }, { 1, 100, 1, "mouse id with count 100" },
        { 1, 0, 1, "mouse id with count 0" }, { 1, 1, 0, "mouse id with NULL buffer" },
    };
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
        SET_ERR(0);
        BOOL r = UN(NtUserGetPointerInputTransform)(t[i].id, t[i].count, t[i].use_buf ? g_xf : NULL);
        CHECKF(!r, "%s: succeeded", t[i].why);
        CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "%s: error %lu", t[i].why, GetLastError());
    }
}
TEST(gap10_ptrxform_count_boundaries_reach_the_pointer_lookup) {
    REQUIRE(UN(NtUserGetPointerInputTransform)); if (t_is_wine()) { t_skip("Wine aborts in NtUserGetPointerInputTransform"); return; }
    /* counts 1 and 100 pass the range check; an unknown pointer id then fails inside CTouchProcessor */
    UINT counts[] = { 1, 2, 50, 100 };
    for (unsigned i = 0; i < 4; i++) {
        memset(g_xf, 0, sizeof g_xf);
        BOOL r = UN(NtUserGetPointerInputTransform)(0x1234, counts[i], g_xf);
        t_note("unknown id, count %u -> %d (error %lu)", counts[i], r, GetLastError());
        CHECK(!r);
    }
}
TEST(gap10_ptrxform_mouse_transform_if_available) {
    REQUIRE(UN(NtUserGetPointerInputTransform)); if (t_is_wine()) { t_skip("Wine aborts in NtUserGetPointerInputTransform"); return; }
    float m[16]; for (int i = 0; i < 16; i++) m[i] = -12345.0f;
    BOOL r = UN(NtUserGetPointerInputTransform)(1, 1, m);
    t_note("mouse transform -> %d", r);
    if (!r) return;
    int finite = 1;
    for (int i = 0; i < 16; i++) if (!(m[i] > -1e9f && m[i] < 1e9f)) finite = 0;
    CHECK(finite);
    int written = 0; for (int i = 0; i < 16; i++) if (m[i] != -12345.0f) written = 1;
    CHECK(written);
}

/* ---- NtUserGetPointerFrameTimes(id, count, TELEMETRY_POINTER_FRAME_TIMES[count] (0x28 bytes each)) ---- */
TEST(gap10_ptrframes_validation_table) {
    REQUIRE(UN(NtUserGetPointerFrameTimes)); if (t_is_wine()) { t_skip("Wine aborts in NtUserGetPointerFrameTimes"); return; }
    static const struct { UINT id, count; int use_buf; const char *why; } t[] = {
        { 0, 1, 1, "id 0" }, { 0x10001, 1, 1, "HIWORD(id) set" }, { 2, 0, 1, "count 0" }, { 2, 101, 1, "count 101" },
        { 2, 0xFFFFFFFFu, 1, "count -1" }, { 2, 1, 0, "NULL buffer" }, { 1, 1, 1, "mouse id is excluded" },
        { 1, 100, 1, "mouse id, count 100" },
    };
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
        SET_ERR(0);
        BOOL r = UN(NtUserGetPointerFrameTimes)(t[i].id, t[i].count, t[i].use_buf ? g_xf : NULL);
        CHECKF(!r, "%s: succeeded", t[i].why);
        CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "%s: error %lu", t[i].why, GetLastError());
    }
}
TEST(gap10_ptrframes_unknown_pointer_fails) {
    REQUIRE(UN(NtUserGetPointerFrameTimes)); if (t_is_wine()) { t_skip("Wine aborts in NtUserGetPointerFrameTimes"); return; }
    static BYTE buf[100 * 0x28];
    UINT counts[] = { 1, 100 };
    for (unsigned i = 0; i < 2; i++) {
        BOOL r = UN(NtUserGetPointerFrameTimes)(0x1234, counts[i], buf);
        t_note("unknown id count %u -> %d error %lu", counts[i], r, GetLastError());
        CHECK(!r);
    }
}

/* ---- NtUserRegisterPointerDeviceNotifications(hwnd, bNotify): returns 0 + error, or a literal 5 for a foreign window ---- */
TEST(gap10_ptrnotify_invalid_window_handles) {
    REQUIRE(UN(NtUserRegisterPointerDeviceNotifications)); if (t_is_wine()) { t_skip("Wine aborts in NtUserRegisterPointerDeviceNotifications"); return; }
    const ULONG_PTR h[] = { 0, 1, 0x1234, 0xFFFFFFFFull };
    for (unsigned i = 0; i < sizeof h / sizeof h[0]; i++)
        for (int on = 0; on < 2; on++) {
            SET_ERR(0);
            INT_PTR r = UN(NtUserRegisterPointerDeviceNotifications)((HWND)h[i], on);
            CHECKF(r == 0, "hwnd %p notify %d -> %Id", (void *)h[i], on, r);
            CHECKF(GetLastError() == ERROR_INVALID_WINDOW_HANDLE, "hwnd %p: error %lu", (void *)h[i], GetLastError());
        }
}
TEST(gap10_ptrnotify_destroyed_window) {
    REQUIRE(UN(NtUserRegisterPointerDeviceNotifications)); if (t_is_wine()) { t_skip("Wine aborts in NtUserRegisterPointerDeviceNotifications"); return; }
    HWND w = k_make_window("gap10-notify"); REQUIRE(w);
    U(DestroyWindow)(w);
    SET_ERR(0);
    CHECK(UN(NtUserRegisterPointerDeviceNotifications)(w, FALSE) == 0);
    EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
}
TEST(gap10_ptrnotify_foreign_process_window_returns_literal_5) {
    REQUIRE(UN(NtUserRegisterPointerDeviceNotifications)); if (t_is_wine()) { t_skip("Wine aborts in NtUserRegisterPointerDeviceNotifications"); return; }
    HWND d = U(GetDesktopWindow)(); REQUIRE(d);
    /* the desktop window belongs to another process: the kernel returns the value 5 (not FALSE) without setting an error */
    SET_ERR(0);
    INT_PTR r = UN(NtUserRegisterPointerDeviceNotifications)(d, FALSE);
    G10_EQ(r, 5);
    r = UN(NtUserRegisterPointerDeviceNotifications)(d, TRUE);
    G10_EQ(r, 5);
}

/* ---- NtUserGetGestureExtArgs(hGestureInfo, cb, buf): NULL buffer is rejected before the handle is looked at ---- */
TEST(gap10_gesture_null_buffer_is_invalid_parameter) {
    REQUIRE(UN(NtUserGetGestureExtArgs)); if (t_is_wine()) { t_skip("Wine aborts in NtUserGetGestureExtArgs"); return; }
    const ULONG_PTR h[] = { 0, 1, 0x1234, 0xFFFFFFFFull };
    for (unsigned i = 0; i < sizeof h / sizeof h[0]; i++) {
        SET_ERR(0);
        CHECK(!UN(NtUserGetGestureExtArgs)((HANDLE)h[i], 0, NULL));
        CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "handle %p: error %lu", (void *)h[i], GetLastError());
    }
}
TEST(gap10_gesture_invalid_handle_fails_without_touching_buffer) {
    REQUIRE(UN(NtUserGetGestureExtArgs)); if (t_is_wine()) { t_skip("Wine aborts in NtUserGetGestureExtArgs"); return; }
    BYTE b[32]; memset(b, 0xCD, sizeof b);
    const ULONG_PTR h[] = { 0, 1, 0x1234, 0x10010, 0xFFFFFFFFull };
    for (unsigned i = 0; i < sizeof h / sizeof h[0]; i++) {
        CHECKF(!UN(NtUserGetGestureExtArgs)((HANDLE)h[i], sizeof b, b), "handle %p accepted", (void *)h[i]);
        for (unsigned k = 0; k < sizeof b; k++) if (b[k] != 0xCD) { CHECKF(0, "handle %p: buffer written", (void *)h[i]); break; }
    }
}
TEST(gap10_gesture_window_handle_is_not_a_gesture_info) {
    REQUIRE(UN(NtUserGetGestureExtArgs)); if (t_is_wine()) { t_skip("Wine aborts in NtUserGetGestureExtArgs"); return; }
    HWND w = k_make_window("gap10-gesture"); REQUIRE(w);
    BYTE b[16]; memset(b, 0xCD, sizeof b);
    CHECK(!UN(NtUserGetGestureExtArgs)((HANDLE)w, sizeof b, b));    /* HMValidateHandle(type 0x15) rejects a TYPE_WINDOW handle */
    CHECK(b[0] == 0xCD);
    U(DestroyWindow)(w);
}
