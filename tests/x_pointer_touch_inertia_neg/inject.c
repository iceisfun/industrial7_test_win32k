/* x_pointer_touch_inertia_neg / inject: NtUserInjectPointerInput(device, POINTER_TYPE_INFO[count], count) - rejections only.
 *
 * The record array is 0x98 bytes per element. Nothing in this file lets a record through: every call below is refused,
 * by construction, before any input exists. The gate for the whole service is the caller's injection permission; a
 * process without it gets ERROR_ACCESS_DENIED before anything else is looked at, which turns the file into a skip with
 * that reason (it is decided by a first probe, not by the account name).
 *
 * With permission, observed on Windows 10 22H2:
 *   - count 0 is ERROR_INVALID_PARAMETER whatever the device and array are (checked before the device);
 *   - a device handle that is not a synthetic pointer device (NULL, small values, all ones, a mouse/keyboard raw-input
 *     device handle, a kernel object handle, a window handle) is ERROR_INVALID_HANDLE, ahead of the array being looked at;
 *   - with a real synthetic touch device (created in a helper process, destroyed on its exit): record type other than
 *     touch is ERROR_INVALID_PARAMETER, more touch records than the device's maximum is ERROR_INVALID_PARAMETER, and an
 *     array that is NULL, misaligned or unreadable is ERROR_NOACCESS (the array is probed before any record is read).
 *     No call here carries a touch record count within the device's maximum.
 *
 * Determinism: only symbolic results are printed.
 */
#include "x.h"

/* 1 when the process may inject; 0 and a skip reason otherwise. */
static int permitted(void) {
    static int cached = -1;
    if (cached >= 0) return cached;
    SET_ERR(0x4D2);
    BOOL r = UN(NtUserInjectPointerInput)(NULL, NULL, 0);
    DWORD gle = GetLastError();
    cached = !r && gle == XE_INVALID_PARAMETER;
    if (!cached) t_note("injection not permitted for this caller (last error %lu)", gle);
    return cached;
}
#define NEEDINJ() do { NEED(NtUserInjectPointerInput); if (!permitted()) { t_skip("caller lacks pointer-injection permission"); return; } } while (0)

XT(count_zero_is_invalid_parameter_before_the_device) {
    NEED(NtUserInjectPointerInput);
    if (!permitted()) {
        /* without permission the answer is ACCESS_DENIED for every argument list, and nothing else */
        t_skip("caller lacks pointer-injection permission");
        return;
    }
    char rec[0x98] = { 0 };
    XFAIL(UN(NtUserInjectPointerInput)(NULL, NULL, 0), XE_INVALID_PARAMETER);
    XFAIL(UN(NtUserInjectPointerInput)(NULL, rec, 0), XE_INVALID_PARAMETER);
    XFAIL(UN(NtUserInjectPointerInput)((HANDLE)0x1234, NULL, 0), XE_INVALID_PARAMETER);
    XFAIL(UN(NtUserInjectPointerInput)((HANDLE)(LONG_PTR)-1, rec, 0), XE_INVALID_PARAMETER);
    XFAIL(UN(NtUserInjectPointerInput)((HANDLE)0xDEADBEEF, XP_BADPTR, 0), XE_INVALID_PARAMETER);
}
static void bad_handle(HANDLE h) {
    NEEDINJ();
    char rec[0x98 * 2] = { 0 };
    XFAIL(UN(NtUserInjectPointerInput)(h, rec, 1), XE_INVALID_HANDLE);
    XFAIL(UN(NtUserInjectPointerInput)(h, rec, 2), XE_INVALID_HANDLE);
    XFAIL(UN(NtUserInjectPointerInput)(h, NULL, 1), XE_INVALID_HANDLE);             /* the device is resolved before the array is read */
    XFAIL(UN(NtUserInjectPointerInput)(h, XP_BADPTR, 3), XE_INVALID_HANDLE);
    XFAIL(UN(NtUserInjectPointerInput)(h, rec, 0xFFFFFFFFu), XE_INVALID_HANDLE);
}
#define BADH(nm, v) XT(device_##nm##_is_invalid_handle) { bad_handle((HANDLE)(ULONG_PTR)(v)); }
BADH(null, 0)
BADH(one, 1)
BADH(0x1234, 0x1234)
BADH(all_ones, (ULONG_PTR)-1)
BADH(minus2, (ULONG_PTR)-2)
BADH(0xDEADBEEF, 0xDEADBEEF)
BADH(kernel_address, 0xFFFF800000000000ULL)

XT(device_window_handle_is_invalid_handle) {
    NEEDINJ();
    HWND w = k_make_window("inj"); if (!w) { t_skip("no window"); return; }
    bad_handle((HANDLE)w);
    U(DestroyWindow)(w);
}
XT(device_kernel_object_handles_are_invalid_handle) {
    NEEDINJ();
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    bad_handle(ev); bad_handle(GetCurrentProcess()); bad_handle(GetCurrentThread());
    CloseHandle(ev);
}
XT(device_raw_input_handles_are_invalid_handle) {
    NEEDINJ();
    UINT n = 0;
    if (!U(GetRawInputDeviceList) || U(GetRawInputDeviceList)(NULL, &n, sizeof(RAWINPUTDEVICELIST)) != 0 || n == 0) { t_skip("no raw input devices"); return; }
    RAWINPUTDEVICELIST *l = calloc(n + 4, sizeof *l);
    UINT c = n + 4;
    UINT got = U(GetRawInputDeviceList)(l, &c, sizeof *l);
    if (got == (UINT)-1 || got == 0) { free(l); t_skip("device list unavailable"); return; }
    for (UINT i = 0; i < got; i++) bad_handle(l[i].hDevice);
    free(l);
}

/* ---- a real synthetic device, in a helper process ---- */
typedef HANDLE (WINAPI *create_fn)(int type, ULONG maxContacts, int feedback);
typedef void (WINAPI *destroy_fn)(HANDLE);
#define POINTER_REC 0x98
#define XSKIP 77

/* args: none. A touch device with two contacts; every call below is refused. */
CHILD(x_inject_touch_device) {
    int fails = 0;
    t_module("user32.dll"); U(GetDesktopWindow)();
    create_fn cr = (create_fn)t_proc_in("user32.dll", "CreateSyntheticPointerDevice");
    destroy_fn de = (destroy_fn)t_proc_in("user32.dll", "DestroySyntheticPointerDevice");
    if (!cr || !de || !UN(NtUserInjectPointerInput)) { printf("  SKIP synthetic pointer device API missing\n"); return XSKIP; }
    SET_ERR(0x4D2);
    HANDLE d = cr(2 /*PT_TOUCH*/, 2, 1 /*POINTER_FEEDBACK_DEFAULT*/);
    if (!d) { printf("  SKIP no synthetic touch device\n"); return XSKIP; }
    static unsigned char rec[POINTER_REC * 4];
    /* record types: 0 none, 1 generic pointer, 3 pen, 4 mouse, 5 touch pad, 6 and 0xFFFFFFFF unknown */
    static const unsigned types[] = { 0, 1, 3, 4, 5, 6, 0x7FFFFFFF, 0xFFFFFFFFu };
    for (unsigned t = 0; t < sizeof types / sizeof types[0]; t++) for (UINT n = 1; n <= 3; n++) {
        memset(rec, 0, sizeof rec); for (UINT k = 0; k < 4; k++) *(unsigned *)(rec + POINTER_REC * k) = types[t];
        SET_ERR(0x4D2);
        BOOL r = UN(NtUserInjectPointerInput)(d, rec, n);
        if (r || GetLastError() != XE_INVALID_PARAMETER) { fails++; printf("  FAIL record type %x count %u: r=%d gle=%lu\n", types[t], n, r, GetLastError()); }
    }
    /* touch records beyond the two contacts the device was created for */
    memset(rec, 0, sizeof rec); for (UINT k = 0; k < 4; k++) *(unsigned *)(rec + POINTER_REC * k) = 2;
    SET_ERR(0x4D2); if (UN(NtUserInjectPointerInput)(d, rec, 3) || GetLastError() != XE_INVALID_PARAMETER) { fails++; printf("  FAIL 3 touch records\n"); }
    SET_ERR(0x4D2); if (UN(NtUserInjectPointerInput)(d, rec, 4) || GetLastError() != XE_INVALID_PARAMETER) { fails++; printf("  FAIL 4 touch records\n"); }
    /* the array is probed first: a huge count, a NULL, misaligned or unreadable array */
    SET_ERR(0x4D2); if (UN(NtUserInjectPointerInput)(d, rec, 0x1000000) || GetLastError() != XE_NOACCESS) { fails++; printf("  FAIL huge count\n"); }
    SET_ERR(0x4D2); if (UN(NtUserInjectPointerInput)(d, NULL, 1) || GetLastError() != XE_NOACCESS) { fails++; printf("  FAIL NULL array\n"); }
    SET_ERR(0x4D2); if (UN(NtUserInjectPointerInput)(d, rec + 1, 1) || GetLastError() != XE_NOACCESS) { fails++; printf("  FAIL misaligned array\n"); }
    SET_ERR(0x4D2); if (UN(NtUserInjectPointerInput)(d, XP_BADPTR, 1) || GetLastError() != XE_NOACCESS) { fails++; printf("  FAIL unreadable array\n"); }
    SET_ERR(0x4D2); if (UN(NtUserInjectPointerInput)(d, XP_KERNELPTR, 1) || GetLastError() != XE_NOACCESS) { fails++; printf("  FAIL kernel array\n"); }
    /* count 0 stays refused with a live device */
    SET_ERR(0x4D2); if (UN(NtUserInjectPointerInput)(d, rec, 0) || GetLastError() != XE_INVALID_PARAMETER) { fails++; printf("  FAIL count 0\n"); }
    de(d);
    return fails;
}
XT(synthetic_touch_device_refuses_every_wrong_record) {
    NEEDINJ();
    struct k_child_result r = k_child_run("x_inject_touch_device", "", 60000);
    if (r.exit_code == XSKIP) { t_skip("synthetic touch device cannot be created here"); return; }
    EXPECT_EQ(r.exit_code, 0);
}

/* A pen device takes exactly one contact: pen records in other counts and records of every other type are refused. */
CHILD(x_inject_pen_device) {
    int fails = 0;
    t_module("user32.dll"); U(GetDesktopWindow)();
    create_fn cr = (create_fn)t_proc_in("user32.dll", "CreateSyntheticPointerDevice");
    destroy_fn de = (destroy_fn)t_proc_in("user32.dll", "DestroySyntheticPointerDevice");
    if (!cr || !de || !UN(NtUserInjectPointerInput)) { printf("  SKIP synthetic pointer device API missing\n"); return XSKIP; }
    SET_ERR(0x4D2);
    HANDLE d = cr(3 /*PT_PEN*/, 1, 1);
    if (!d) { printf("  SKIP no synthetic pen device\n"); return XSKIP; }
    static unsigned char rec[POINTER_REC * 4];
    static const unsigned types[] = { 0, 1, 2, 4, 5, 6, 0xFFFFFFFFu };
    for (unsigned t = 0; t < sizeof types / sizeof types[0]; t++) for (UINT n = 1; n <= 3; n++) {
        memset(rec, 0, sizeof rec); for (UINT k = 0; k < 4; k++) *(unsigned *)(rec + POINTER_REC * k) = types[t];
        SET_ERR(0x4D2);
        BOOL r = UN(NtUserInjectPointerInput)(d, rec, n);
        if (r || GetLastError() != XE_INVALID_PARAMETER) { fails++; printf("  FAIL record type %x count %u: r=%d gle=%lu\n", types[t], n, r, GetLastError()); }
    }
    memset(rec, 0, sizeof rec); for (UINT k = 0; k < 4; k++) *(unsigned *)(rec + POINTER_REC * k) = 3;
    for (UINT n = 2; n <= 4; n++) { SET_ERR(0x4D2); if (UN(NtUserInjectPointerInput)(d, rec, n) || GetLastError() != XE_INVALID_PARAMETER) { fails++; printf("  FAIL %u pen records\n", n); } }
    SET_ERR(0x4D2); if (UN(NtUserInjectPointerInput)(d, rec, 0) || GetLastError() != XE_INVALID_PARAMETER) { fails++; printf("  FAIL count 0\n"); }
    de(d);
    return fails;
}
XT(synthetic_pen_device_refuses_every_wrong_record) {
    NEEDINJ();
    struct k_child_result r = k_child_run("x_inject_pen_device", "", 60000);
    if (r.exit_code == XSKIP) { t_skip("synthetic pen device cannot be created here"); return; }
    EXPECT_EQ(r.exit_code, 0);
}
