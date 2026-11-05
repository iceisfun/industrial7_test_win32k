/* Pointer devices and the pointer-input query family.
 *
 * Unknown/invalid pointer ids: win32kbase!NtUserGetPointerType / NtUserGetPointerInfoList reject id 0
 * and any id with a non-zero high word with ERROR_INVALID_PARAMETER; pointer id 1 is the mouse (always
 * PT_MOUSE=4 for GetPointerType) and only yields GetPointerInfo data while the thread is processing a
 * mouse-in-pointer message; every other id is looked up in the thread's pointer list
 * (CTouchProcessor::GetThreadPointerData) and a miss is again ERROR_INVALID_PARAMETER.
 */
#include "rit.h"
#include "synth.h"


#include <stddef.h>

static const UINT bad_ids[] = { 0, 0x10000, 0x10001, 0x20002, 0x80000000u, 0xffffffffu };
#define NBADID (sizeof bad_ids / sizeof bad_ids[0])
static const UINT unknown_ids[] = { 2, 3, 0x1234, 0x7fff, 0xfffe, 0xffff };
#define NUNKNOWN (sizeof unknown_ids / sizeof unknown_ids[0])

RT(ptr_struct_sizes) {
    /* kernel-side GetPointerInfoSize: kind 1 -> 0x60, kind 2/5 -> 0x90, kind 3 -> 0x78 */
    CHECK(sizeof(POINTER_INFO) == 0x60);
    CHECK(sizeof(POINTER_TOUCH_INFO) == 0x90);
    CHECK(sizeof(POINTER_PEN_INFO) == 0x78);
    CHECK(sizeof(POINTER_DEVICE_INFO) == 0x438);        /* _GetPointerDeviceInfoProperties writes 0x438-byte records */
    CHECK(sizeof(POINTER_DEVICE_PROPERTY) == 0x1c);     /* properties are copied as count * 0x1c */
    CHECK(offsetof(POINTER_DEVICE_INFO, productString) == 0x26);
    CHECK(offsetof(POINTER_DEVICE_INFO, device) == 8);
    CHECK(offsetof(POINTER_DEVICE_INFO, pointerDeviceType) == 0x10);
}

/* ---- GetPointerType ---------------------------------------------------------------------- */
RT(ptr_type_invalid_ids) {
    REQUIRE(U(GetPointerType));
    for (unsigned i = 0; i < NBADID; i++) {
        POINTER_INPUT_TYPE t = 0x77;
        SET_ERR(0);
        CHECKF(!U(GetPointerType)(bad_ids[i], &t), "id 0x%x", bad_ids[i]);
        EXPECT_ERR(E_INVALID_PARAM);
        CHECK(t == 0x77);
    }
}
RT(ptr_type_null_out) {
    REQUIRE(U(GetPointerType));
    SET_ERR(0);
    CHECK(!U(GetPointerType)(1, NULL));
    EXPECT_ERR(E_INVALID_PARAM);
}
RT(ptr_type_mouse_id1) {
    REQUIRE(U(GetPointerType));
    POINTER_INPUT_TYPE t = 0;
 /* argument 1 == 1 short-circuits to PT_MOUSE without any pointer list lookup */
    CHECK(U(GetPointerType)(1, &t));
    CHECK(t == PT_MOUSE);
}
RT(ptr_type_unknown_ids) {
    REQUIRE(U(GetPointerType));
    for (unsigned i = 0; i < NUNKNOWN; i++) {
        POINTER_INPUT_TYPE t = 0x77;
        SET_ERR(0);
        CHECKF(!U(GetPointerType)(unknown_ids[i], &t), "id 0x%x", unknown_ids[i]);
        EXPECT_ERR(E_INVALID_PARAM);
        CHECK(t == 0x77);
    }
}
RT(ptr_type_direct_stub) {
    WINE_UNIMPL("NtUserGetPointerType");
    REQUIRE(UN(NtUserGetPointerType));
    rit_gui();
    ULONG t = 0;
    CHECK(UN(NtUserGetPointerType)(1, &t));
    CHECK(t == 4);
    SET_ERR(0);
    CHECK(!UN(NtUserGetPointerType)(0, &t));
    EXPECT_ERR(E_INVALID_PARAM);
}

/* ---- GetPointerInfo family: ids that cannot exist ---------------------------------------- */
#define PI_BADID(name, w, call) \
RT(ptr_info_##name##_badid) { \
    if (w) WINE_UNIMPL("pointer frame/history queries"); \
    REQUIRE(U(GetPointerInfo)); \
    for (unsigned i = 0; i < NBADID; i++) { UINT id = bad_ids[i]; SET_ERR(0); \
        CHECKF(!(call), "id 0x%x", id); EXPECT_ERR(E_INVALID_PARAM); } \
} \
RT(ptr_info_##name##_unknown) { \
    if (w) WINE_UNIMPL("pointer frame/history queries"); \
    REQUIRE(U(GetPointerInfo)); \
    for (unsigned i = 0; i < NUNKNOWN; i++) { UINT id = unknown_ids[i]; SET_ERR(0); \
        CHECKF(!(call), "id 0x%x", id); EXPECT_ERR(E_INVALID_PARAM); } \
}
static POINTER_INFO g_pi; static POINTER_TOUCH_INFO g_pt; static POINTER_PEN_INFO g_pp;
static UINT g_n1, g_n2;
PI_BADID(info, 0, U(GetPointerInfo)(id, &g_pi))
PI_BADID(touch, 0, U(GetPointerTouchInfo)(id, &g_pt))
PI_BADID(pen, 0, U(GetPointerPenInfo)(id, &g_pp))
PI_BADID(history, 1, (g_n1 = 1, U(GetPointerInfoHistory)(id, &g_n1, &g_pi)))
PI_BADID(touchhist, 1, (g_n1 = 1, U(GetPointerTouchInfoHistory)(id, &g_n1, &g_pt)))
PI_BADID(penhist, 1, (g_n1 = 1, U(GetPointerPenInfoHistory)(id, &g_n1, &g_pp)))
PI_BADID(frame, 1, (g_n1 = 1, U(GetPointerFrameInfo)(id, &g_n1, &g_pi)))
PI_BADID(frametouch, 1, (g_n1 = 1, U(GetPointerFrameTouchInfo)(id, &g_n1, &g_pt)))
PI_BADID(framepen, 1, (g_n1 = 1, U(GetPointerFramePenInfo)(id, &g_n1, &g_pp)))
PI_BADID(framehist, 1, (g_n1 = 1, g_n2 = 1, U(GetPointerFrameInfoHistory)(id, &g_n1, &g_n2, &g_pi)))
PI_BADID(frametouchhist, 1, (g_n1 = 1, g_n2 = 1, U(GetPointerFrameTouchInfoHistory)(id, &g_n1, &g_n2, &g_pt)))
PI_BADID(framepenhist, 1, (g_n1 = 1, g_n2 = 1, U(GetPointerFramePenInfoHistory)(id, &g_n1, &g_n2, &g_pp)))

RT(ptr_info_null_out) {
    REQUIRE(U(GetPointerInfo));
    SET_ERR(0);
    /* out==NULL with history==0 and frame==0 trips the "all three zero" test */
    CHECK(!U(GetPointerInfo)(0x1234, NULL));
    EXPECT_ERR(E_INVALID_PARAM);
    SET_ERR(0);
    CHECK(!U(GetPointerTouchInfo)(0x1234, NULL));
    EXPECT_ERR(E_INVALID_PARAM);
    SET_ERR(0);
    CHECK(!U(GetPointerPenInfo)(0x1234, NULL));
    EXPECT_ERR(E_INVALID_PARAM);
}
RT(ptr_info_frame_null_count) {
    REQUIRE(U(GetPointerFrameInfo));
    SET_ERR(0);
 /* the pointerCount pointer becomes pPointerCount: NULL -> 87 before any lookup */
    CHECK(!U(GetPointerFrameInfo)(0x1234, NULL, &g_pi));
    EXPECT_ERR(E_INVALID_PARAM);
    SET_ERR(0);
    CHECK(!U(GetPointerFrameTouchInfo)(0x1234, NULL, &g_pt));
    EXPECT_ERR(E_INVALID_PARAM);
}
RT(ptr_info_history_null_entries) {
    WINE_UNIMPL("pointer history");
    REQUIRE(U(GetPointerInfoHistory));
    SET_ERR(0);
    CHECK(!U(GetPointerInfoHistory)(0x1234, NULL, &g_pi));
    EXPECT_ERR(E_INVALID_PARAM);
}
RT(ptr_info_history_zero_entries_with_buffer) {
    WINE_UNIMPL("pointer history");
    REQUIRE(U(GetPointerInfoHistory));
    UINT n = 0;
    SET_ERR(0);
    /* *entries == 0 with an output buffer: (entries == 0 || count == 0) -> 87 */
    CHECK(!U(GetPointerInfoHistory)(0x1234, &n, &g_pi));
    EXPECT_ERR(E_INVALID_PARAM);
    CHECK(n == 0);
}
RT(ptr_info_frame_zero_count_with_buffer) {
    REQUIRE(U(GetPointerFrameInfo));
    UINT n = 0;
    SET_ERR(0);
    CHECK(!U(GetPointerFrameInfo)(0x1234, &n, &g_pi));
    EXPECT_ERR(E_INVALID_PARAM);
    CHECK(n == 0);
}
RT(ptr_info_outputs_untouched_on_failure) {
    REQUIRE(U(GetPointerInfo));
    POINTER_INFO pi; memset(&pi, 0x6b, sizeof pi);
    CHECK(!U(GetPointerInfo)(0x4321, &pi));
    CHECK(((unsigned char *)&pi)[0] == 0x6b && ((unsigned char *)&pi)[0x5f] == 0x6b);
    POINTER_TOUCH_INFO pt; memset(&pt, 0x6b, sizeof pt);
    CHECK(!U(GetPointerTouchInfo)(0x4321, &pt));
    CHECK(((unsigned char *)&pt)[0x8f] == 0x6b);
}
RT(ptr_info_mouse_id1_without_mouse_in_pointer) {
    WINE_UNIMPL("frame pointer info");
    REQUIRE(U(GetPointerInfo));
    rit_gui();
    POINTER_INFO pi;
    SET_ERR(0);
    /* id 1 needs the thread's mouse-in-pointer record (pti+0x500) to be live; a thread that is not
     * inside a mouse pointer message has none -> 87 */
    CHECK(!U(GetPointerInfo)(1, &pi));
    EXPECT_ERR(E_INVALID_PARAM);
    SET_ERR(0);
    CHECK(!U(GetPointerFrameInfo)(1, &g_n1, &pi));
    CHECK(GetLastError() == E_INVALID_PARAM);
}

/* ---- NtUserGetPointerInfoList direct: kind / size agreement ------------------------------ */
static void il(UINT id, ULONG kind, int hist, int frame, UINT cb, UINT *pe, UINT *pc, void *out, int expect_err_ok) {
    REQUIRE(UN(NtUserGetPointerInfoList));
    rit_gui();
    SET_ERR(0);
    LONGLONG r = UN(NtUserGetPointerInfoList)(id, kind, hist, frame, cb, pe, pc, out);
    CHECK((r & 0xffffffff) == 0);
    EXPECT_ERR(E_INVALID_PARAM);
}
RT(ptr_infolist_kind0_size0)  { UINT a = 1, b = 1; char o[0x90]; il(0x1234, 0, 0, 0, 0, &a, &b, o, 1); }
RT(ptr_infolist_kind1_size90) { UINT a = 1, b = 1; char o[0x90]; il(0x1234, 1, 0, 0, 0x90, &a, &b, o, 1); }
RT(ptr_infolist_kind2_size60) { UINT a = 1, b = 1; char o[0x90]; il(0x1234, 2, 0, 0, 0x60, &a, &b, o, 1); }
RT(ptr_infolist_kind3_size90) { UINT a = 1, b = 1; char o[0x90]; il(0x1234, 3, 0, 0, 0x90, &a, &b, o, 1); }
RT(ptr_infolist_kind4_size60) { UINT a = 1, b = 1; char o[0x90]; il(0x1234, 4, 0, 0, 0x60, &a, &b, o, 1); }
RT(ptr_infolist_kind5_size90_unknown) { UINT a = 1, b = 1; char o[0x90]; il(0x1234, 5, 0, 0, 0x90, &a, &b, o, 1); }
RT(ptr_infolist_kind99)       { UINT a = 1, b = 1; char o[0x90]; il(0x1234, 99, 0, 0, 0x60, &a, &b, o, 1); }
RT(ptr_infolist_null_entries) { UINT b = 1; char o[0x90]; il(0x1234, 1, 0, 0, 0x60, NULL, &b, o, 1); }
RT(ptr_infolist_null_count)   { UINT a = 1; char o[0x90]; il(0x1234, 1, 0, 0, 0x60, &a, NULL, o, 1); }
RT(ptr_infolist_all_null_out) { UINT a = 1, b = 1; il(0x1234, 1, 0, 0, 0x60, &a, &b, NULL, 1); }
RT(ptr_infolist_zero_entries_with_buffer) { UINT a = 0, b = 1; char o[0x90]; il(0x1234, 1, 1, 0, 0x60, &a, &b, o, 1); }
RT(ptr_infolist_zero_count_with_buffer)   { UINT a = 1, b = 0; char o[0x90]; il(0x1234, 1, 0, 1, 0x60, &a, &b, o, 1); }
RT(ptr_infolist_id0)          { UINT a = 1, b = 1; char o[0x90]; il(0, 1, 0, 0, 0x60, &a, &b, o, 1); }
RT(ptr_infolist_id_high)      { UINT a = 1, b = 1; char o[0x90]; il(0x10002, 1, 0, 0, 0x60, &a, &b, o, 1); }
RT(ptr_infolist_unknown_history) { UINT a = 4, b = 1; char o[0x60 * 4]; il(0x1234, 1, 1, 0, 0x60, &a, &b, o, 1); }
RT(ptr_infolist_unknown_frame)   { UINT a = 1, b = 4; char o[0x60 * 4]; il(0x1234, 1, 0, 1, 0x60, &a, &b, o, 1); }
RT(ptr_infolist_unknown_framehist) { UINT a = 4, b = 4; char o[0x60 * 16]; il(0x1234, 1, 1, 1, 0x60, &a, &b, o, 1); }
RT(ptr_infolist_mouse_id1_not_enabled) { UINT a = 1, b = 1; char o[0x90]; il(1, 1, 0, 0, 0x60, &a, &b, o, 1); }
RT(ptr_infolist_counts_untouched_on_failure) {
    REQUIRE(UN(NtUserGetPointerInfoList));
    rit_gui();
    UINT a = 7, b = 9; char o[0x90];
    UN(NtUserGetPointerInfoList)(0x1234, 1, 0, 0, 0x60, &a, &b, o);
    CHECK(a == 7 && b == 9);
}

/* ---- other per-pointer queries ------------------------------------------------------------ */
RT(ptr_cursorid_invalid) {
    REQUIRE(U(GetPointerCursorId));
    for (unsigned i = 0; i < NBADID; i++) {
        UINT c = 0x77; SET_ERR(0);
        CHECK(!U(GetPointerCursorId)(bad_ids[i], &c));
        EXPECT_ERR(E_INVALID_PARAM);
        CHECK(c == 0x77);
    }
    for (unsigned i = 0; i < NUNKNOWN; i++) {
        UINT c = 0x77; SET_ERR(0);
        CHECK(!U(GetPointerCursorId)(unknown_ids[i], &c));
        EXPECT_ERR(E_INVALID_PARAM);
    }
    SET_ERR(0);
    CHECK(!U(GetPointerCursorId)(0x1234, NULL));
    EXPECT_ERR(E_INVALID_PARAM);
}
RT(ptr_frametimes_invalid) {
    WINE_UNIMPL("NtUserGetPointerFrameTimes");
    REQUIRE(UN(NtUserGetPointerFrameTimes));
    rit_gui();
    char o[0x28 * 4];
    struct { UINT id, n; void *p; } c[] = {
        { 0, 1, o }, { 0x10000, 1, o }, { 1, 1, o }, { 0x1234, 0, o }, { 0x1234, 101, o },
        { 0x1234, 0xffffffff, o }, { 0x1234, 1, NULL }, { 2, 0, NULL },
    };
    for (unsigned i = 0; i < sizeof c / sizeof c[0]; i++) {
        SET_ERR(0);
        /* id 0/high word, id 1 (mouse), count outside [1,100], NULL buffer: all 87 before any probe */
        CHECKF((UN(NtUserGetPointerFrameTimes)(c[i].id, c[i].n, c[i].p) & 0xffffffff) == 0, "case %u", i);
        EXPECT_ERR(E_INVALID_PARAM);
    }
}
RT(ptr_frametimes_export) {
    REQUIRE(t_proc_in("user32.dll", "GetPointerFrameTimes"));
    CHECK(t_proc_in("win32u.dll", "NtUserGetPointerFrameTimes") != NULL);
}
RT(ptr_inputtransform_invalid) {
    WINE_UNIMPL("NtUserGetPointerInputTransform");
    REQUIRE(UN(NtUserGetPointerInputTransform));
    rit_gui();
    char o[0x40 * 4];
    struct { UINT id, n; void *p; } c[] = {
        { 0, 1, o }, { 0x10000, 1, o }, { 0x1234, 0, o }, { 0x1234, 101, o }, { 0x1234, 1, NULL },
        { 1, 2, o },        /* mouse id with count != 1 */
        { 1, 4, o },
    };
    for (unsigned i = 0; i < sizeof c / sizeof c[0]; i++) {
        SET_ERR(0);
        CHECKF((UN(NtUserGetPointerInputTransform)(c[i].id, c[i].n, c[i].p) & 0xffffffff) == 0, "case %u", i);
        EXPECT_ERR(E_INVALID_PARAM);
    }
}
RT(ptr_inputtransform_mouse_note) {
    WINE_UNIMPL("NtUserGetPointerInputTransform");
    REQUIRE(UN(NtUserGetPointerInputTransform));
    rit_gui();
    float m[16]; memset(m, 0, sizeof m);
    LONGLONG r = UN(NtUserGetPointerInputTransform)(1, 1, m);
    t_note("mouse input transform -> %lld, m[0]=%g m[5]=%g (err %lu)", r, m[0], m[5], GetLastError());
    CHECK(1);
}
RT(ptr_proprietary_id_unknown) {
    WINE_UNIMPL("NtUserGetPointerProprietaryId");
    REQUIRE(UN(NtUserGetPointerProprietaryId));
    rit_gui();
    unsigned char g[16]; memset(g, 0, sizeof g);
    SET_ERR(0);
    BOOL r = UN(NtUserGetPointerProprietaryId)(0x1234, g);
    CHECK(!r);
    t_note("unknown pointer proprietary id -> err %lu (kernel sets 232 when the lookup fails)", GetLastError());
}

/* ---- pointer device enumeration ----------------------------------------------------------- */
RT(ptr_devices_null_count) {
    REQUIRE(U(GetPointerDevices));
    SET_ERR(0);
    CHECK(!U(GetPointerDevices)(NULL, NULL));
    EXPECT_ERR(E_INVALID_PARAM);
}
RT(ptr_devices_count_query) {
    REQUIRE(U(GetPointerDevices));
    UINT n = 0xabcd;
    CHECK(U(GetPointerDevices)(&n, NULL));
    CHECK(n != 0xabcd);
    t_note("pointer devices: %u", n);
    UINT m = 7;
    CHECK(U(GetPointerDevices)(&m, NULL));
    CHECK(m == n);                       /* the in-count is not read when the buffer is NULL */
}
RT(ptr_devices_no_device_with_buffer) {
    REQUIRE(U(GetPointerDevices));
    UINT n = 0; U(GetPointerDevices)(&n, NULL);
    if (n != 0) { t_skip("pointer devices present"); return; }
    POINTER_DEVICE_INFO *d = calloc(2, sizeof *d);
    UINT c = 1;
    SET_ERR(0);
    CHECK(!U(GetPointerDevices)(&c, d));
    EXPECT_ERR(0x3bc3);                  /* no accessible device: (*count == 0 || n == 0) branch */
    free(d);
}
RT(ptr_devices_small_buffer_122) {
    REQUIRE(U(GetPointerDevices));
    UINT n = 0; U(GetPointerDevices)(&n, NULL);
    SYNTH_AUTO(sy); SYNTH_AUTO(sy2); SYNTH_ENSURE2_OR_SKIP(sy, sy2, n);
    POINTER_DEVICE_INFO *d = calloc(n + 2, sizeof *d);
    UINT c = n - 1;
    SET_ERR(0);
    CHECK(!U(GetPointerDevices)(&c, d));
    EXPECT_ERR(E_INSUFFICIENT);
    CHECK(c == n);
    free(d);
}
RT(ptr_devices_fill_and_roundtrip) {
    REQUIRE(U(GetPointerDevices));
    REQUIRE(U(GetPointerDevice));
    UINT n = 0; U(GetPointerDevices)(&n, NULL);
    SYNTH_AUTO(sy); SYNTH_ENSURE_OR_SKIP(sy, n);
    POINTER_DEVICE_INFO *d = calloc(n + 2, sizeof *d);
    UINT c = n;
    CHECK(U(GetPointerDevices)(&c, d));
    CHECK(c == n);
    for (UINT i = 0; i < n; i++) {
        CHECK(d[i].device != NULL);
        CHECK(d[i].pointerDeviceType >= POINTER_DEVICE_TYPE_INTEGRATED_PEN && d[i].pointerDeviceType <= POINTER_DEVICE_TYPE_TOUCH_PAD);
        CHECK(wcsnlen(d[i].productString, 520) < 520);
        POINTER_DEVICE_INFO one; memset(&one, 0, sizeof one);
        CHECK(U(GetPointerDevice)(d[i].device, &one));
        CHECK(one.device == d[i].device);
        CHECK(one.pointerDeviceType == d[i].pointerDeviceType);
        CHECK(one.maxActiveContacts == d[i].maxActiveContacts);
        CHECK(!lstrcmpW(one.productString, d[i].productString));
    }
    free(d);
}
RT(ptr_device_invalid_args) {
    REQUIRE(U(GetPointerDevice));
    POINTER_DEVICE_INFO info;
    SET_ERR(0);
    CHECK(!U(GetPointerDevice)(NULL, &info));
    EXPECT_ERR(E_INVALID_PARAM);
    SET_ERR(0);
    CHECK(!U(GetPointerDevice)((HANDLE)0x1234, NULL));
    EXPECT_ERR(E_INVALID_PARAM);
    SET_ERR(0);
    CHECK(!U(GetPointerDevice)(NULL, NULL));
    EXPECT_ERR(E_INVALID_PARAM);
}
RT(ptr_device_bad_handles_6) {
    REQUIRE(U(GetPointerDevice));
    static const ULONG_PTR hs[] = { 1, 2, 0x1234, 0xffff, 0x10001, (ULONG_PTR)-1, (ULONG_PTR)-2 };
    for (unsigned i = 0; i < sizeof hs / sizeof *hs; i++) {
        POINTER_DEVICE_INFO info; memset(&info, 0x3c, sizeof info);
        SET_ERR(0);
        /* ResolveMouseOrPointerDevice: not a type-0x13 handle (or -1 with no isMouse out) -> 6 */
        CHECKF(!U(GetPointerDevice)((HANDLE)hs[i], &info), "h 0x%llx", (unsigned long long)hs[i]);
        EXPECT_ERR(E_INVALID_HANDLE);
        CHECK(((unsigned char *)&info)[0] == 0x3c);
    }
}
RT(ptr_device_mouse_and_keyboard_are_not_pointer_devices) {
    REQUIRE(U(GetPointerDevice));
    REQUIRE(U(GetPointerDeviceProperties));
    REQUIRE(U(GetPointerDeviceCursors));
    rit_dev d[64]; int n = rit_devices(d, 64);
    int seen = 0;
    for (int i = 0; i < n; i++) {
        if (d[i].type == RIM_TYPEHID) continue;
        POINTER_DEVICE_INFO info; UINT cnt = 4; POINTER_DEVICE_PROPERTY pr[4]; POINTER_DEVICE_CURSOR_INFO cur[4];
        SET_ERR(0);
        /* a mouse/keyboard DEVICEINFO has no pointer flag (byte +200 bit 7) -> ResolveMouseOrPointerDevice fails */
        CHECK(!U(GetPointerDevice)(d[i].h, &info));
        EXPECT_ERR(E_INVALID_HANDLE);
        SET_ERR(0);
        CHECK(!U(GetPointerDeviceProperties)(d[i].h, &cnt, pr));
        EXPECT_ERR(E_INVALID_HANDLE);
        cnt = 4;
        SET_ERR(0);
        CHECK(!U(GetPointerDeviceCursors)(d[i].h, &cnt, cur));
        EXPECT_ERR(E_INVALID_HANDLE);
        seen++;
    }
    if (!seen) t_skip("no mouse/keyboard raw devices");
}
RT(ptr_device_properties_invalid) {
    REQUIRE(U(GetPointerDeviceProperties));
    UINT cnt = 1; POINTER_DEVICE_PROPERTY pr[2];
    SET_ERR(0); CHECK(!U(GetPointerDeviceProperties)(NULL, &cnt, pr)); EXPECT_ERR(E_INVALID_PARAM);
    SET_ERR(0); CHECK(!U(GetPointerDeviceProperties)((HANDLE)0x1234, NULL, pr)); EXPECT_ERR(E_INVALID_PARAM);
    SET_ERR(0); CHECK(!U(GetPointerDeviceProperties)((HANDLE)0x1234, &cnt, pr)); EXPECT_ERR(E_INVALID_HANDLE);
    SET_ERR(0); CHECK(!U(GetPointerDeviceProperties)((HANDLE)0x1234, &cnt, NULL)); EXPECT_ERR(E_INVALID_HANDLE);
    SET_ERR(0); CHECK(!U(GetPointerDeviceProperties)((HANDLE)(ULONG_PTR)-1, &cnt, pr)); EXPECT_ERR(E_INVALID_HANDLE);
}
RT(ptr_device_properties_valid_device) {
    REQUIRE(U(GetPointerDeviceProperties));
    UINT n = 0; U(GetPointerDevices)(&n, NULL);
    SYNTH_AUTO(sy); SYNTH_ENSURE_OR_SKIP(sy, n);
    POINTER_DEVICE_INFO *d = calloc(n + 1, sizeof *d); UINT c = n;
    U(GetPointerDevices)(&c, d);
    UINT np = 0;
    CHECK(U(GetPointerDeviceProperties)(d[0].device, &np, NULL));      /* size query */
    CHECK(np != 0xffffffff);
    POINTER_DEVICE_PROPERTY *p = calloc(np + 1, sizeof *p);
    UINT np2 = np;
    if (np) CHECK(U(GetPointerDeviceProperties)(d[0].device, &np2, p));
    if (np > 1) {
        UINT small = np - 1;
        SET_ERR(0);
        CHECK(!U(GetPointerDeviceProperties)(d[0].device, &small, p));
        EXPECT_ERR(E_INSUFFICIENT);
        CHECK(small == np);
    }
    free(p); free(d);
}
RT(ptr_device_cursors_invalid) {
    REQUIRE(U(GetPointerDeviceCursors));
    UINT cnt = 1; POINTER_DEVICE_CURSOR_INFO cur[2];
    SET_ERR(0); CHECK(!U(GetPointerDeviceCursors)(NULL, &cnt, cur)); EXPECT_ERR(E_INVALID_PARAM);
    SET_ERR(0); CHECK(!U(GetPointerDeviceCursors)((HANDLE)0x1234, NULL, cur)); EXPECT_ERR(E_INVALID_PARAM);
    SET_ERR(0); CHECK(!U(GetPointerDeviceCursors)((HANDLE)0x1234, &cnt, cur)); EXPECT_ERR(E_INVALID_HANDLE);
    SET_ERR(0); CHECK(!U(GetPointerDeviceCursors)((HANDLE)(ULONG_PTR)-1, &cnt, NULL)); EXPECT_ERR(E_INVALID_HANDLE);
}
RT(ptr_device_rects_invalid) {
    REQUIRE(U(GetPointerDeviceRects));
    RECT a, b;
    SET_ERR(0); CHECK(!U(GetPointerDeviceRects)(NULL, &a, &b)); EXPECT_ERR(E_INVALID_PARAM);
    SET_ERR(0); CHECK(!U(GetPointerDeviceRects)((HANDLE)0x1234, NULL, &b)); EXPECT_ERR(E_INVALID_PARAM);
    SET_ERR(0); CHECK(!U(GetPointerDeviceRects)((HANDLE)0x1234, &a, NULL)); EXPECT_ERR(E_INVALID_PARAM);
    /* a handle that does not resolve falls out of the body to the same 87 (not 6) */
    SET_ERR(0); CHECK(!U(GetPointerDeviceRects)((HANDLE)0x1234, &a, &b)); EXPECT_ERR(E_INVALID_PARAM);
}
RT(ptr_device_rects_mouse_minus1) {
    REQUIRE(U(GetPointerDeviceRects));
    RECT phys, disp; memset(&phys, 0, sizeof phys); memset(&disp, 0, sizeof disp);
    /* handle -1 is the mouse: ResolveMouseOrPointerDevice reports isMouse; display rect is the screen */
    BOOL r = U(GetPointerDeviceRects)((HANDLE)(ULONG_PTR)-1, &phys, &disp);
    CHECK(r);
    if (r) { CHECK(disp.right > disp.left && disp.bottom > disp.top); }
}
RT(ptr_device_orientation_inputspace) {
    WINE_UNIMPL("NtUserGetPointerDeviceOrientation");
    REQUIRE(UN(NtUserGetPointerDeviceOrientation));
    REQUIRE(UN(NtUserGetPointerDeviceInputSpace));
    rit_gui();
    UINT o = 0xdead; ULONGLONG sp = 0xdead;
    CHECK(UN(NtUserGetPointerDeviceOrientation)((HANDLE)(ULONG_PTR)-1, &o));
    CHECK(o != 0xdead);
    CHECK(UN(NtUserGetPointerDeviceInputSpace)((HANDLE)(ULONG_PTR)-1, &sp));
    t_note("mouse orientation %u, input space 0x%llx", o, sp);
    static const ULONG_PTR hs[] = { 0, 1, 0x1234, 0x10001 };
    for (unsigned i = 0; i < 4; i++) {
        o = 0xdead; sp = 0xdead;
        SET_ERR(0); CHECK(!UN(NtUserGetPointerDeviceOrientation)((HANDLE)hs[i], &o)); EXPECT_ERR(E_INVALID_HANDLE); CHECK(o == 0xdead);
        SET_ERR(0); CHECK(!UN(NtUserGetPointerDeviceInputSpace)((HANDLE)hs[i], &sp)); EXPECT_ERR(E_INVALID_HANDLE);
    }
}
RT(ptr_device_extended_property_invalid) {
    WINE_UNIMPL("NtUserGetExtendedPointerDeviceProperty");
    REQUIRE(UN(NtUserGetExtendedPointerDeviceProperty));
    rit_gui();
    UINT blob[8]; memset(blob, 0, sizeof blob);
    SET_ERR(0);
    CHECK(!UN(NtUserGetExtendedPointerDeviceProperty)((HANDLE)0x1234, NULL));
    EXPECT_ERR(E_INVALID_PARAM);
    SET_ERR(0);
    CHECK(!UN(NtUserGetExtendedPointerDeviceProperty)((HANDLE)0x1234, blob));
    EXPECT_ERR(E_INVALID_HANDLE);
}
RT(ptr_raw_data_invalid) {
    REQUIRE(U(GetRawPointerDeviceData));
    POINTER_DEVICE_PROPERTY pr[1]; LONG vals[4];
    struct { UINT id, h, np; void *p; void *v; } c[] = {
        { 0, 1, 1, pr, vals }, { 0x10000, 1, 1, pr, vals }, { 0x1234, 0, 1, pr, vals }, { 0x1234, 1, 0, pr, vals },
        { 0x1234, 1, 1, NULL, vals }, { 0x1234, 1, 1, pr, NULL }, { 0x1234, 0xffffffff, 0xffffffff, pr, vals },
    };
    for (unsigned i = 0; i < sizeof c / sizeof c[0]; i++) {
        SET_ERR(0);
        /* zero id/history/properties, NULL arrays and a 32-bit overflow of history*props are all 87 */
        CHECKF(!U(GetRawPointerDeviceData)(c[i].id, c[i].h, c[i].np, c[i].p, c[i].v), "case %u", i);
        EXPECT_ERR(E_INVALID_PARAM);
    }
}
RT(ptr_skipframe_unknown_id) {
    REQUIRE(U(SkipPointerFrameMessages));
    SET_ERR(0);
    BOOL r = U(SkipPointerFrameMessages)(0x1234);
    t_note("SkipPointerFrameMessages(unknown) -> %d err %lu", r, GetLastError());
    CHECK(1);
}

/* ---- notification / target registration ---------------------------------------------------- */
RT(ptr_notify_invalid_window) {
    REQUIRE(U(RegisterPointerDeviceNotifications));
    SET_ERR(0);
    CHECK(!U(RegisterPointerDeviceNotifications)(NULL, FALSE));
    SET_ERR(0);
    CHECK(!U(RegisterPointerDeviceNotifications)((HWND)(ULONG_PTR)0x7fff1234, FALSE));
    t_note("bogus hwnd notification registration err %lu", GetLastError());
}
RT(ptr_notify_register_twice) {
    REQUIRE(U(RegisterPointerDeviceNotifications));
    HWND h = k_make_window("pn");
    if (!h) { t_skip("no window"); return; }
    /* _RegisterPointerDeviceNotifications walks gPointerDeviceClients: first insert wins, a repeat for the same hwnd returns 0 */
    CHECK(U(RegisterPointerDeviceNotifications)(h, FALSE));
    CHECK(!U(RegisterPointerDeviceNotifications)(h, FALSE));
    U(DestroyWindow)(h);
}
RT(ptr_notify_other_process_window) {
    WINE_UNIMPL("NtUserRegisterPointerDeviceNotifications");
    REQUIRE(U(RegisterPointerDeviceNotifications));
    HWND dw = U(GetDesktopWindow)();
    SET_ERR(0);
    LONGLONG r = UN(NtUserRegisterPointerDeviceNotifications) ? UN(NtUserRegisterPointerDeviceNotifications)(dw, FALSE) : U(RegisterPointerDeviceNotifications)(dw, FALSE);
    /* window of another process: the body stores 5 into the return slot instead of setting a last error */
    t_note("desktop window notification registration -> %lld (err %lu)", r, GetLastError());
    CHECK(1);
}
RT(ptr_target_invalid_window) {
    REQUIRE(U(RegisterPointerInputTarget));
    SET_ERR(0);
    CHECK(!U(RegisterPointerInputTarget)(NULL, PT_TOUCH));
    SET_ERR(0);
    CHECK(!U(RegisterPointerInputTarget)((HWND)(ULONG_PTR)0x7fff1234, PT_PEN));
}
RT(ptr_target_bad_types) {
    REQUIRE(U(RegisterPointerInputTarget));
    HWND h = k_make_window("pt");
    if (!h) { t_skip("no window"); return; }
    static const UINT types[] = { 0, PT_POINTER, PT_MOUSE, PT_TOUCHPAD, 6, 0xffffffff };
    for (unsigned i = 0; i < sizeof types / sizeof *types; i++) {
        SET_ERR(0);
        BOOL r = U(RegisterPointerInputTarget)(h, types[i]);
        CHECKF(!r, "type %u", types[i]);
        DWORD e = GetLastError();
        /* only PT_TOUCH(2)/PT_PEN(3) pass `type - 2 < 2`; policy-enforced processes see 5 first */
        CHECKF(e == E_INVALID_PARAM || e == E_ACCESS_DENIED, "type %u err %lu", types[i], e);
    }
    U(DestroyWindow)(h);
}
RT(ptr_target_touch_pen_register_unregister) {
    REQUIRE(U(RegisterPointerInputTarget));
    REQUIRE(U(UnregisterPointerInputTarget));
    HWND h = k_make_window("pt");
    if (!h) { t_skip("no window"); return; }
    SET_ERR(0);
    BOOL r = U(RegisterPointerInputTarget)(h, PT_TOUCH);
    if (!r) { t_note("RegisterPointerInputTarget(PT_TOUCH) denied, err %lu (needs UIAccess when policy is enforced)", GetLastError()); }
    else {
        CHECK(U(RegisterPointerInputTarget)(h, PT_TOUCH));
        HWND h2 = k_make_window("pt2");
        if (h2) {
            SET_ERR(0);
            CHECK(!U(RegisterPointerInputTarget)(h2, PT_TOUCH));    /* slot already taken by another window */
            EXPECT_ERR(E_ACCESS_DENIED);
            U(DestroyWindow)(h2);
        }
        CHECK(U(UnregisterPointerInputTarget)(h, PT_TOUCH));
    }
    U(DestroyWindow)(h);
}

/* ---- synthetic devices (real devices made through the public injection API) ---------------------------------------
 * Windows 10 22H2: a synthetic touch device is a type-3 (touch) pointer device with the requested contacts, 10 properties
 * and one cursor per contact; a synthetic pen is a type-1 (integrated pen) device with 12 properties, 2 cursors (pen and
 * eraser) and one contact. Both are HID digitizers (usage page 0x0d, usage 4 touch screen / 2 pen) in the raw input list. */
static int rit_count_hid(WORD page, WORD usage) {
    rit_dev d[64]; int n = rit_devices(d, 64), c = 0;
    for (int i = 0; i < n; i++) {
        if (d[i].type != RIM_TYPEHID) continue;
        RID_DEVICE_INFO inf; memset(&inf, 0, sizeof inf); UINT cb = sizeof inf; inf.cbSize = sizeof inf;
        if (U(GetRawInputDeviceInfoW)(d[i].h, RIDI_DEVICEINFO, &inf, &cb) == (UINT)sizeof inf &&
            inf.hid.usUsagePage == page && inf.hid.usUsage == usage) c++;
    }
    return c;
}
static int rit_synth_shape(int type, ULONG contacts, int ptype, UINT nprops, UINT ncursors, WORD usage) {
    SYNTH_AUTO(sy);
    int hid0 = rit_count_hid(0x0d, usage);
    UINT before = synth_count();
    if (!synth_begin(&sy, type, contacts)) return 0;
    UINT n = synth_count();
    CHECK(n == before + 1);
    CHECK(rit_count_hid(0x0d, usage) == hid0 + 1);
    POINTER_DEVICE_INFO *d = calloc(n + 1, sizeof *d); UINT c = n;
    CHECK(U(GetPointerDevices)(&c, d));
    int found = 0;
    for (UINT i = 0; i < c; i++) {
        if (d[i].pointerDeviceType != (POINTER_DEVICE_TYPE)ptype || d[i].maxActiveContacts != contacts) continue;
        POINTER_DEVICE_INFO one; memset(&one, 0, sizeof one);
        CHECK(U(GetPointerDevice)(d[i].device, &one));
        CHECK(one.device == d[i].device && one.pointerDeviceType == d[i].pointerDeviceType);
        UINT np = 0, nc = 0;
        CHECK(U(GetPointerDeviceProperties)(d[i].device, &np, NULL));
        CHECK(U(GetPointerDeviceCursors)(d[i].device, &nc, NULL));
        if (np == nprops && nc == ncursors) {
            found++;
            RECT dr, sr; memset(&dr, 0, sizeof dr); memset(&sr, 0, sizeof sr);
            CHECK(U(GetPointerDeviceRects)(d[i].device, &dr, &sr));
            CHECK(dr.right > dr.left && dr.bottom > dr.top);
            CHECK(sr.right > sr.left && sr.bottom > sr.top);
        }
    }
    free(d);
    CHECK(found >= 1);
    return 1;
}
RT(ptr_synthetic_touch_device_shape) {
    REQUIRE(U(CreateSyntheticPointerDevice) && U(GetPointerDevices) && U(GetPointerDevice));
    REQUIRE(U(GetPointerDeviceProperties) && U(GetPointerDeviceCursors) && U(GetPointerDeviceRects));
    if (!rit_synth_shape(PT_TOUCH, 3, POINTER_DEVICE_TYPE_TOUCH, 10, 3, 4)) t_skip("a synthetic touch device cannot be created");
}
RT(ptr_synthetic_pen_device_shape) {
    REQUIRE(U(CreateSyntheticPointerDevice) && U(GetPointerDevices) && U(GetPointerDevice));
    REQUIRE(U(GetPointerDeviceProperties) && U(GetPointerDeviceCursors) && U(GetPointerDeviceRects));
    if (!rit_synth_shape(PT_PEN, 1, POINTER_DEVICE_TYPE_INTEGRATED_PEN, 12, 2, 2)) t_skip("a synthetic pen device cannot be created");
}
RT(ptr_synthetic_device_gone_after_destroy) {
    REQUIRE(U(CreateSyntheticPointerDevice) && U(GetPointerDevices));
    UINT before = synth_count();
    struct synth s;
    if (!synth_begin(&s, PT_PEN, 1)) { t_skip("a synthetic pen device cannot be created"); return; }
    CHECK(synth_count() == before + 1);
    synth_end(&s);
    CHECK(synth_count() == before);
}
static char rit_pt_log[64]; static int rit_pt_nlog;    /* message order, for diagnostics */
static LRESULT CALLBACK rit_pt_proc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    static int *seen; 
    if ((m == WM_POINTERDOWN || m == WM_POINTERUPDATE || m == WM_POINTERUP || m == WM_POINTERCAPTURECHANGED || m == WM_POINTERLEAVE || m == WM_POINTERENTER) && rit_pt_nlog < 60)
        rit_pt_log[rit_pt_nlog++] = m == WM_POINTERDOWN ? 'D' : m == WM_POINTERUPDATE ? 'M' : m == WM_POINTERUP ? 'U' : m == WM_POINTERCAPTURECHANGED ? 'C' : m == WM_POINTERLEAVE ? 'L' : 'E';
    if (m == WM_NCCREATE) { seen = (int *)((CREATESTRUCTW *)lp)->lpCreateParams; }
    if (m == WM_POINTERDOWN && seen) seen[0]++;
    if (m == WM_POINTERUPDATE && seen) seen[1]++;
    if (m == WM_POINTERUP && seen) seen[2]++;
    return U(DefWindowProcW)(w, m, wp, lp);
}
/* Pump until a counter reaches `want` (or `ms` pass): an injected contact is delivered asynchronously, and the next stage must
 * not be injected before the previous one arrived, otherwise a slow guest stretches the gap and the contact is not the same one. */
static void rit_pump_until(const int *cnt, int want, unsigned ms) {
    DWORD t0 = GetTickCount();
    while (*cnt < want && GetTickCount() - t0 < ms) k_pump(20);
    k_pump(20);
}
/* One injected contact: down, update, up, each stage injected only after the previous message arrived. Returns 1 for the clean
 * sequence (one down, at least one update, one up). A contact that is ended by something outside the test between the stages
 * (the update then arrives as a second contact) shows up as extra down/up messages; the caller repeats the whole cycle then. */
static int rit_inject_cycle(struct synth *sy, POINTER_TYPE_INFO *ti, POINTER_INFO *pi, RECT *rc, int *seen, BOOL ok[3], DWORD gle[3]) {
    seen[0] = seen[1] = seen[2] = 0; rit_pt_nlog = 0;
    k_pump(100);
    pi->ptPixelLocation.x = 400;
    if (rc) { rc->left = 395; rc->right = 405; }
    pi->pointerFlags = POINTER_FLAG_NEW | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT | POINTER_FLAG_DOWN;
    SET_ERR(0);
    ok[0] = U(InjectSyntheticPointerInput)(sy->h, ti, 1); gle[0] = GetLastError();
    rit_pump_until(&seen[0], 1, 3000);
    pi->pointerFlags = POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT | POINTER_FLAG_UPDATE;
    pi->ptPixelLocation.x = 410;
    if (rc) { rc->left = 405; rc->right = 415; }
    SET_ERR(0);
    ok[1] = U(InjectSyntheticPointerInput)(sy->h, ti, 1); gle[1] = GetLastError();
    rit_pump_until(&seen[1], 1, 3000);
    pi->pointerFlags = POINTER_FLAG_UP;
    SET_ERR(0);
    ok[2] = U(InjectSyntheticPointerInput)(sy->h, ti, 1); gle[2] = GetLastError();
    rit_pump_until(&seen[2], 1, 3000);
    k_pump(200);    /* settle: a stray extra message would show up in the counts */
    rit_pt_log[rit_pt_nlog] = 0;
    return seen[0] == 1 && seen[1] > 0 && seen[2] == 1;
}
#define RIT_CYCLES 5
RT(ptr_synthetic_touch_injection_reaches_window) {
    REQUIRE(U(CreateSyntheticPointerDevice) && U(InjectSyntheticPointerInput) && U(RegisterClassW) && U(CreateWindowExW));
    SYNTH_AUTO(sy);
    if (!synth_begin(&sy, PT_TOUCH, 2)) { t_skip("a synthetic touch device cannot be created"); return; }
    WNDCLASSW wc; memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = rit_pt_proc; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = L"rit_pt_inject";
    U(RegisterClassW)(&wc);
    int wseen[3] = { 0, 0, 0 };
    HWND w = U(CreateWindowExW)(WS_EX_TOPMOST, L"rit_pt_inject", L"x", WS_POPUP | WS_VISIBLE, 300, 200, 200, 200, NULL, NULL, wc.hInstance, wseen);
    if (!w) { U(UnregisterClassW)(L"rit_pt_inject", wc.hInstance); t_skip("no window"); return; }
    k_pump(200);
    POINTER_TYPE_INFO ti; memset(&ti, 0, sizeof ti);
    ti.type = PT_TOUCH;
    POINTER_TOUCH_INFO *t = &ti.touchInfo;
    t->pointerInfo.pointerType = PT_TOUCH; t->pointerInfo.pointerId = 0;
    t->pointerInfo.ptPixelLocation.y = 300;
    t->touchMask = TOUCH_MASK_CONTACTAREA | TOUCH_MASK_ORIENTATION | TOUCH_MASK_PRESSURE;
    t->rcContact.top = 295; t->rcContact.bottom = 305;
    t->orientation = 90; t->pressure = 32000;
    int *seen = wseen; BOOL ok[3]; DWORD gle[3]; int clean = 0, cycles = 0;
    while (!clean && cycles < RIT_CYCLES) { clean = rit_inject_cycle(&sy, &ti, &t->pointerInfo, &t->rcContact, seen, ok, gle); cycles++; }
    if (cycles > 1) t_info("injection cycles needed: %d", cycles);
    CHECKF(ok[0], "inject down gle=%lu", gle[0]);
    CHECKF(ok[1], "inject update gle=%lu", gle[1]);
    CHECKF(ok[2], "inject up gle=%lu", gle[2]);
    /* observed on Windows 10 22H2: one WM_POINTERDOWN, at least one WM_POINTERUPDATE, one WM_POINTERUP reach the window under the contact */
    CHECKF(seen[0] == 1, "WM_POINTERDOWN x%d (order %s)", seen[0], rit_pt_log);
    CHECKF(seen[1] > 0, "WM_POINTERUPDATE x%d", seen[1]);
    CHECKF(seen[2] == 1, "WM_POINTERUP x%d", seen[2]);
    U(DestroyWindow)(w);
    U(UnregisterClassW)(L"rit_pt_inject", wc.hInstance);
}
RT(ptr_synthetic_pen_injection_reaches_window) {
    REQUIRE(U(CreateSyntheticPointerDevice) && U(InjectSyntheticPointerInput) && U(RegisterClassW) && U(CreateWindowExW));
    SYNTH_AUTO(sy);
    if (!synth_begin(&sy, PT_PEN, 1)) { t_skip("a synthetic pen device cannot be created"); return; }
    WNDCLASSW wc; memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = rit_pt_proc; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = L"rit_pt_inject_pen";
    U(RegisterClassW)(&wc);
    int wseen[3] = { 0, 0, 0 };
    HWND w = U(CreateWindowExW)(WS_EX_TOPMOST, L"rit_pt_inject_pen", L"x", WS_POPUP | WS_VISIBLE, 300, 200, 200, 200, NULL, NULL, wc.hInstance, wseen);
    if (!w) { U(UnregisterClassW)(L"rit_pt_inject_pen", wc.hInstance); t_skip("no window"); return; }
    k_pump(200);
    POINTER_TYPE_INFO ti; memset(&ti, 0, sizeof ti);
    ti.type = PT_PEN;
    POINTER_PEN_INFO *p = &ti.penInfo;
    p->pointerInfo.pointerType = PT_PEN; p->pointerInfo.pointerId = 0;
    p->pointerInfo.ptPixelLocation.y = 300;
    p->penMask = PEN_MASK_PRESSURE; p->pressure = 512;
    int *seen = wseen; BOOL ok[3]; DWORD gle[3]; int clean = 0, cycles = 0;
    while (!clean && cycles < RIT_CYCLES) { clean = rit_inject_cycle(&sy, &ti, &p->pointerInfo, NULL, seen, ok, gle); cycles++; }
    if (cycles > 1) t_info("injection cycles needed: %d", cycles);
    CHECKF(ok[0], "inject down gle=%lu", gle[0]);
    CHECKF(ok[1], "inject update gle=%lu", gle[1]);
    CHECKF(ok[2], "inject up gle=%lu", gle[2]);
    /* observed on Windows 10 22H2: one WM_POINTERDOWN, at least one WM_POINTERUPDATE, one WM_POINTERUP reach the window under the contact */
    CHECKF(seen[0] == 1, "WM_POINTERDOWN x%d (order %s)", seen[0], rit_pt_log);
    CHECKF(seen[1] > 0, "WM_POINTERUPDATE x%d", seen[1]);
    CHECKF(seen[2] == 1, "WM_POINTERUP x%d", seen[2]);
    U(DestroyWindow)(w);
    U(UnregisterClassW)(L"rit_pt_inject_pen", wc.hInstance);
}

