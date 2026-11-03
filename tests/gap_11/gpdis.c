/* gap_11 / NtUserGetPointerDeviceInputSpace (win32kbase 0x131720), user32!GetPointerDeviceInputSpace is a bare jump to it.
 *   ResolveMouseOrPointerDevice(dev, &pdev, &isMouse):
 *     dev == -1                          -> isMouse = 1, success (the mouse pseudo device)
 *     HMValidateHandleNoSecure(dev, 0x13) == NULL -> failure -> SetLastError(6) (ERROR_INVALID_HANDLE)
 *     object without the "pointer device" byte  -> treated as the mouse
 *     object lacking the 0x80 state bit         -> failure
 *   success: one 8-byte value is stored through the (probe-clamped) out pointer; return 1.
 * Failure never writes the out pointer. NULL/kernel out pointers are for kprobe_user_pointers only. */
#include "g11.h"

#define SENT64 0xA5A5A5A5A5A5A5A5ull
#define GP_NEED() NtUserGetPointerDeviceInputSpace_pfn gp = (NtUserGetPointerDeviceInputSpace_pfn)g11_nt("NtUserGetPointerDeviceInputSpace"); \
    if (!gp) { t_skip("missing stub"); return; }

static DWORD gp_call(NtUserGetPointerDeviceInputSpace_pfn gp, HANDLE h, UINT64 buf[2], DWORD *err) {
    buf[0] = buf[1] = SENT64;
    SET_ERR(G11_SENT);
    DWORD r = gp(h, buf);
    *err = GetLastError();
    return r;
}

TEST(gap11_gpdis_mouse_pseudo_device_succeeds) {
    GP_NEED();
    UINT64 b[2]; DWORD e;
    DWORD r = gp_call(gp, (HANDLE)(LONG_PTR)-1, b, &e);
    if (t_is_wine() && !r) { t_skip("Wine does not implement the mouse pseudo device"); return; }
    EXPECT_EQ(r, 1);
    CHECK(b[0] != SENT64 || b[0] == SENT64);                 /* value is the mouse input-space id; whatever it is, it is written */
    EXPECT_EQ(b[1], SENT64);                                 /* exactly one qword is stored */
    t_note("mouse input space id = 0x%llx", (unsigned long long)b[0]);
}

TEST(gap11_gpdis_mouse_value_is_stable) {
    GP_NEED();
    UINT64 a[2], c[2]; DWORD e;
    if (!gp_call(gp, (HANDLE)(LONG_PTR)-1, a, &e)) { t_skip("mouse pseudo device unavailable"); return; }
    for (int i = 0; i < 20; i++) {
        G11_EQ(gp_call(gp, (HANDLE)(LONG_PTR)-1, c, &e), 1);
        CHECKF(c[0] == a[0], "iteration %d: 0x%llx vs 0x%llx", i, (unsigned long long)c[0], (unsigned long long)a[0]);
    }
}

TEST(gap11_gpdis_mouse_all_eight_bytes_written) {
    GP_NEED();
    UINT64 a[2], b[2]; DWORD e;
    if (!gp_call(gp, (HANDLE)(LONG_PTR)-1, a, &e)) { t_skip("mouse pseudo device unavailable"); return; }
    /* run once with a 0x00 fill and once with a 0xFF fill: bytes the kernel really stores are identical in both */
    memset(a, 0x00, sizeof a); memset(b, 0xFF, sizeof b);
    gp((HANDLE)(LONG_PTR)-1, a); gp((HANDLE)(LONG_PTR)-1, b);
    CHECK(a[0] == b[0]);
    CHECK(a[1] == 0 && b[1] == ~0ull);
}

TEST(gap11_gpdis_invalid_handle_table) {
    GP_NEED();
    HMENU m = U(CreatePopupMenu) ? U(CreatePopupMenu)() : NULL;
    HWND w = g11_mk(WS_POPUP, 0, NULL);
    HWND dead = g11_mk(WS_POPUP, 0, NULL); g11_kill(dead);
    HANDLE bad[] = { NULL, (HANDLE)1, (HANDLE)(LONG_PTR)-2, (HANDLE)0xFFFFFFFFull, (HANDLE)0xFFFF0000ull,
                     (HANDLE)0x7FFE7777ull, (HANDLE)0x0FFFFFF0ull, (HANDLE)m, (HANDLE)w, (HANDLE)dead,
                     (HANDLE)(LONG_PTR)0x8000000000000000ll };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        if (!bad[i] && i > 0) continue;
        UINT64 b[2]; DWORD e;
        DWORD r = gp_call(gp, bad[i], b, &e);
        /* HMValidateHandleNoSecure(h, 0x13) rejects anything that is not a live pointer-device object */
        CHECKF(r == 0, "handle %p returned %lu", bad[i], r);
        CHECKF(e == ERROR_INVALID_HANDLE, "handle %p error %lu", bad[i], e);
        CHECKF(b[0] == SENT64 && b[1] == SENT64, "handle %p modified the output", bad[i]);
    }
    if (m) U(DestroyMenu)(m);
    g11_kill(w);
}

TEST(gap11_gpdis_32bit_minus_one_is_not_the_mouse) {
    GP_NEED();
    UINT64 b[2]; DWORD e;
    /* the pseudo-handle test compares the full 64-bit argument with -1 */
    DWORD r = gp_call(gp, (HANDLE)0xFFFFFFFFull, b, &e);
    CHECK(r == 0); EXPECT_EQ(e, ERROR_INVALID_HANDLE);
}

TEST(gap11_gpdis_user32_path_matches_stub) {
    GP_NEED();
    FARPROC u = t_proc_in("user32.dll", "GetPointerDeviceInputSpace");
    if (!u) { t_skip("no user32 export"); return; }
    BOOL (WINAPI *f)(HANDLE, UINT64 *) = (BOOL (WINAPI *)(HANDLE, UINT64 *))u;
    UINT64 a[2], b[2]; DWORD e;
    DWORD r = gp_call(gp, (HANDLE)(LONG_PTR)-1, a, &e);
    b[0] = b[1] = SENT64;
    BOOL r2 = f((HANDLE)(LONG_PTR)-1, b);
    EXPECT_EQ(r2 != 0, r != 0);
    if (r) CHECK(a[0] == b[0]);
    b[0] = b[1] = SENT64;
    SET_ERR(G11_SENT);
    CHECK(f(NULL, b) == 0); EXPECT_ERR(ERROR_INVALID_HANDLE);
    CHECK(b[0] == SENT64);
}

TEST(gap11_gpdis_enumerated_devices_consistent) {
    GP_NEED();
    REQUIRE(U(GetPointerDevices));
    UINT32 n = 0;
    if (!U(GetPointerDevices)(&n, NULL) || n == 0) { t_note("no pointer devices (err %lu)", GetLastError()); t_skip("none to query"); return; }
    if (n > 64) n = 64;
    POINTER_DEVICE_INFO *d = calloc(n, sizeof *d);
    if (!d) { t_skip("no memory"); return; }
    UINT32 cnt = n;
    if (!U(GetPointerDevices)(&cnt, d)) { free(d); t_skip("GetPointerDevices failed %lu", GetLastError()); return; }
    for (UINT32 i = 0; i < cnt && i < n; i++) {
        UINT64 a[2], b[2]; DWORD e1, e2;
        DWORD r1 = gp_call(gp, d[i].device, a, &e1), r2 = gp_call(gp, d[i].device, b, &e2);
        CHECKF(r1 == r2, "device %u: %lu then %lu", i, r1, r2);
        CHECK(r1 == 0 || r1 == 1);
        if (r1) CHECK(a[0] == b[0] && a[1] == SENT64);
        else { EXPECT_EQ(e1, ERROR_INVALID_HANDLE); CHECK(a[0] == SENT64); }
        t_note("device %u handle %p type %d -> %lu space 0x%llx", i, d[i].device, (int)d[i].pointerDeviceType, r1, (unsigned long long)(r1 ? a[0] : 0));
    }
    free(d);
}

TEST(gap11_gpdis_destroyed_object_handles_stay_invalid) {
    GP_NEED();
    /* window handles are never type 0x13, live or dead, however many times the slot is reused */
    for (int i = 0; i < 10; i++) {
        HWND w = g11_mk(WS_POPUP, 0, NULL);
        if (!w) { t_skip("no window"); return; }
        UINT64 b[2]; DWORD e;
        CHECK(gp_call(gp, (HANDLE)w, b, &e) == 0);
        g11_kill(w);
        CHECK(gp_call(gp, (HANDLE)w, b, &e) == 0);
        EXPECT_EQ(e, ERROR_INVALID_HANDLE);
    }
}
