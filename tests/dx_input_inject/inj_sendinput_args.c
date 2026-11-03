/* dx_input_inject: NtUserSendInput argument validation and counting.
 *
 * Kernel facts (NtUserSendInput, 19041):
 *   NtUserSendInput(UINT cInputs, INPUT *pInputs, int cbSize): cbSize must be exactly 0x28 (sizeof(INPUT) on x64) or
 *   the call fails with ERROR_INVALID_PARAMETER (UserSetLastError(0x57)) and returns 0; cInputs == 0 takes the same
 *   exit (0x57, 0) *before* pInputs is probed, so a NULL/garbage pointer with a zero count is safe.
 *   SendInput walks the array; INPUT_MOUSE (0) -> the mouse injection path, INPUT_KEYBOARD (1) -> the keyboard injection path,
 *   INPUT_HARDWARE (2) -> ERROR_CALL_NOT_IMPLEMENTED (0x78) and stop when the caller's compat version
 *   is above 0x500, otherwise it is skipped but still counted. Any other type value matches no branch and is counted as
 *   inserted. The return value is the number of events walked before a failure.
 *   user32!SendInput is a bare jump to the win32u stub, user32!keybd_event/mouse_event build one INPUT and call it.
 */
#include "inj.h"

UDECL(UINT, NtUserSendInput, (UINT, INPUT *, int));

/* INPUT layout as SendInput indexes it (stride 0x28, type @0, mi/ki payload @8). */
TEST(dx_input_inject_input_layout) {
    EXPECT_EQ(sizeof(INPUT), 0x28);
    EXPECT_EQ(offsetof(INPUT, type), 0);
    EXPECT_EQ(offsetof(INPUT, mi.dx), 8);
    EXPECT_EQ(offsetof(INPUT, mi.dy), 12);
    EXPECT_EQ(offsetof(INPUT, mi.mouseData), 16);
    EXPECT_EQ(offsetof(INPUT, mi.dwFlags), 20);
    EXPECT_EQ(offsetof(INPUT, mi.time), 24);
    EXPECT_EQ(offsetof(INPUT, mi.dwExtraInfo), 32);
    EXPECT_EQ(offsetof(INPUT, ki.wVk), 8);
    EXPECT_EQ(offsetof(INPUT, ki.wScan), 10);
    EXPECT_EQ(offsetof(INPUT, ki.dwFlags), 12);
    EXPECT_EQ(offsetof(INPUT, ki.time), 16);
    EXPECT_EQ(offsetof(INPUT, ki.dwExtraInfo), 24);
    EXPECT_EQ(offsetof(INPUT, hi.uMsg), 8);
}

/* Service numbers of the input syscalls on 19041 (win32u stubs: mov r10,rcx; mov eax,N; syscall). */
TEST(dx_input_inject_stub_numbers) {
    static const struct { const char *n; long num; } t[] = {
        { "NtUserSendInput", 4223 }, { "NtUserBlockInput", 5025 }, { "NtUserClipCursor", 5043 }, { "NtUserGetClipCursor", 5112 },
        { "NtUserInitializeTouchInjection", 5214 }, { "NtUserSetCursorPos", 5319 }, { "NtUserGetKeyState", 4099 },
        { "NtUserGetAsyncKeyState", 4164 }, { "NtUserGetKeyboardState", 4214 }, { "NtUserSetKeyboardState", 4322 },
    };
    HMODULE m = t_module("win32u.dll");
    if (!m) { t_skip("no win32u"); return; }
    if (!k_is_19041()) t_note("not build 19041, service numbers may differ");
    for (size_t i = 0; i < sizeof t / sizeof *t; i++) {
        FARPROC p = GetProcAddress(m, t[i].n);
        if (!p) { t_note("%s not exported", t[i].n); continue; }
        if (!k_stub_valid(p)) { t_note("%s stub layout unknown", t[i].n); continue; }
        if (k_is_19041()) CHECKF(k_stub_number(p) == t[i].num, "%s got %ld want %ld", t[i].n, k_stub_number(p), t[i].num);
        CHECKF(k_stub_number(p) >= 0x1000, "%s", t[i].n);
    }
}

/* Wrong cbSize: rejected before anything is read. Table-driven over both the user32 export and the raw stub. */
static void cbsize_table(UINT (*send)(UINT, INPUT *, int), const char *what) {
    static const int bad[] = { 0, 1, 4, 8, 0x1c, 0x20, 0x27, 0x29, 0x2c, 0x30, 0x50, 0x100, -1, -0x28, 0x7fffffff, (int)0x80000028 };
    INPUT in; inj_mouse(&in, 0, 0, 0, 0, 0);
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        SET_ERR(0xABCD);
        UINT r = send(1, &in, bad[i]);
        CHECKF(r == 0, "%s cb=%d r=%u", what, bad[i], r);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
}
static UINT call_u32(UINT n, INPUT *p, int cb) { return U(SendInput)(n, p, cb); }
static UINT call_nt(UINT n, INPUT *p, int cb) { return UN(NtUserSendInput)(n, p, cb); }
TEST(dx_input_inject_sendinput_cbsize_user32) { INJ_ENV(); REQUIRE(U(SendInput)); cbsize_table(call_u32, "user32"); }
TEST(dx_input_inject_sendinput_cbsize_win32u) { INJ_ENV(); REQUIRE(UN(NtUserSendInput)); cbsize_table(call_nt, "win32u"); }

/* cbSize validity is independent of the count: a bad size with count 0 / several still reports 0x57. */
TEST(dx_input_inject_sendinput_cbsize_with_counts) {
    INJ_ENV(); REQUIRE(UN(NtUserSendInput));
    INPUT in[3]; for (int i = 0; i < 3; i++) inj_mouse(&in[i], 0, 0, 0, 0, 0);
    static const UINT cnt[] = { 0, 1, 2, 3 };
    for (size_t i = 0; i < 4; i++) {
        SET_ERR(0);
        EXPECT_EQ(UN(NtUserSendInput)(cnt[i], in, 0x30), 0);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
}

/* cInputs == 0 with a correct cbSize: SendInput is not reached, 0x57 set; the pointer is never probed. */
TEST(dx_input_inject_sendinput_count_zero) {
    INJ_ENV(); REQUIRE(UN(NtUserSendInput)); REQUIRE(U(SendInput));
    INPUT in; inj_mouse(&in, 0, 0, 0, 0, 0);
    SET_ERR(0); EXPECT_EQ(UN(NtUserSendInput)(0, &in, 0x28), 0); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SET_ERR(0); EXPECT_EQ(U(SendInput)(0, &in, sizeof in), 0); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    /* the count==0 branch precedes ProbeForRead, so a NULL array is rejected with the same code, not an exception */
    SET_ERR(0); EXPECT_EQ(UN(NtUserSendInput)(0, NULL, 0x28), 0); EXPECT_ERR(ERROR_INVALID_PARAMETER);
}

/* A no-op mouse event (no MOUSEEVENTF flags, dx=dy=0) walks cleanly: the mouse injection path returns 1 and nothing moves. */
TEST(dx_input_inject_sendinput_noop_mouse_counts) {
    INJ_ENV(); inj_arm(); REQUIRE(UN(NtUserSendInput));
    POINT a, b; U(GetCursorPos)(&a);
    INPUT in[8]; for (int i = 0; i < 8; i++) inj_mouse(&in[i], 0, 0, 0, 0, 0);
    for (UINT n = 1; n <= 8; n *= 2) {
        UINT r = UN(NtUserSendInput)(n, in, 0x28);
        CHECKF(r == n, "n=%u r=%u", n, r);
    }
    U(GetCursorPos)(&b);
    CHECK(inj_near(a.x, b.x, 3) && inj_near(a.y, b.y, 3));   /* a real user may nudge the mouse a little */
    inj_restore();
}

/* Unknown INPUT types match no branch in SendInput and are still counted. */
TEST(dx_input_inject_sendinput_unknown_type_counted) {
    INJ_ENV(); inj_arm(); REQUIRE(UN(NtUserSendInput));
    static const DWORD types[] = { 3, 4, 7, 0x100, 0x80000000u, 0xFFFFFFFFu };
    for (size_t i = 0; i < sizeof types / sizeof *types; i++) {
        INPUT in; memset(&in, 0, sizeof in); in.type = types[i];
        UINT r = UN(NtUserSendInput)(1, &in, 0x28);
        CHECKF(r == 1, "type=0x%lx r=%u", types[i], r);
    }
    INPUT mix[3]; inj_mouse(&mix[0], 0, 0, 0, 0, 0); memset(&mix[1], 0, sizeof mix[1]); mix[1].type = 9; inj_mouse(&mix[2], 0, 0, 0, 0, 0);
    EXPECT_EQ(UN(NtUserSendInput)(3, mix, 0x28), 3);
}

/* INPUT_HARDWARE: compat version above 0x500 -> ERROR_CALL_NOT_IMPLEMENTED and stop; otherwise skipped but counted. */
TEST(dx_input_inject_sendinput_hardware) {
    INJ_ENV(); inj_arm(); REQUIRE(UN(NtUserSendInput));
    INPUT in; memset(&in, 0, sizeof in); in.type = INPUT_HARDWARE; in.hi.uMsg = WM_NULL;
    SET_ERR(0);
    UINT r = UN(NtUserSendInput)(1, &in, 0x28);
    DWORD e = GetLastError();
    CHECKF((r == 1) || (r == 0 && e == ERROR_CALL_NOT_IMPLEMENTED), "r=%u err=%lu", r, e);
    t_note("INPUT_HARDWARE -> r=%u err=%lu (r==0 means the process compat version is above 0x500)", r, e);
    /* the walk stops at the hardware entry when it fails: a trailing no-op mouse event is not reached */
    INPUT two[2]; two[0] = in; inj_mouse(&two[1], 0, 0, 0, 0, 0);
    SET_ERR(0);
    UINT r2 = UN(NtUserSendInput)(2, two, 0x28);
    if (r == 0) { EXPECT_EQ(r2, 0); EXPECT_ERR(ERROR_CALL_NOT_IMPLEMENTED); }
    else EXPECT_EQ(r2, 2);
    /* a hardware entry after a good one: the good one is counted first */
    INPUT rev[2]; rev[0] = two[1]; rev[1] = in;
    UINT r3 = UN(NtUserSendInput)(2, rev, 0x28);
    CHECK(r == 0 ? r3 == 1 : r3 == 2);
}

/* Keyboard events with the F24 key and both directions are accepted and counted; the keys are released again. */
TEST(dx_input_inject_sendinput_key_counts) {
    INJ_ENV(); inj_arm(); REQUIRE(UN(NtUserSendInput));
    INPUT in[4];
    inj_key(&in[0], VK_F24, 0, 0, 0); inj_key(&in[1], VK_F24, 0, KEYEVENTF_KEYUP, 0);
    inj_key(&in[2], VK_F23, 0, 0, 0); inj_key(&in[3], VK_F23, 0, KEYEVENTF_KEYUP, 0);
    for (UINT n = 1; n <= 4; n++) {
        UINT r = UN(NtUserSendInput)(n, in, 0x28);
        CHECKF(r == n, "n=%u r=%u", n, r);
        if (n & 1) { INPUT up; inj_key(&up, in[n - 1].ki.wVk, 0, KEYEVENTF_KEYUP, 0); UN(NtUserSendInput)(1, &up, 0x28); }
    }
    inj_restore();
}

/* Mixed keyboard + mouse in one call: the order is preserved and every entry counted. */
TEST(dx_input_inject_sendinput_mixed_types) {
    INJ_ENV(); inj_arm(); REQUIRE(U(SendInput));
    INPUT in[5];
    inj_key(&in[0], VK_F24, 0, 0, 0);
    inj_mouse(&in[1], 0, 0, 0, 0, 0);
    inj_key(&in[2], VK_F24, 0, KEYEVENTF_KEYUP, 0);
    inj_mouse(&in[3], 0, 0, 0, 0, 0);
    memset(&in[4], 0, sizeof in[4]); in[4].type = 99;
    EXPECT_EQ(U(SendInput)(5, in, sizeof(INPUT)), 5);
    inj_restore();
}

/* keybd_event / mouse_event are NtUserSendInput(1, &input, 0x28) wrappers; the observable result is the same as SendInput. */
TEST(dx_input_inject_wrappers_equal_sendinput) {
    INJ_ENV(); inj_arm(); REQUIRE(U(keybd_event)); REQUIRE(U(mouse_event)); REQUIRE(U(GetLastInputInfo));
    if (!inj_hook_begin(1, 1)) { t_skip("cannot install LL hooks"); return; }
    ULONG_PTR t1 = inj_tag(), t2 = inj_tag(), t3 = inj_tag(), t4 = inj_tag();
    U(keybd_event)(VK_F24, 0, 0, t1);
    U(keybd_event)(VK_F24, 0, KEYEVENTF_KEYUP, t2);
    U(mouse_event)(0, 0, 0, 0, t3);
    inj_keydown(VK_F24, 0, t4); inj_keyup(VK_F24, 0, t4);
    inj_pump(60);
    int a = inj_ev_find(0, t1, 0), b = inj_ev_find(0, t2, 0), c = inj_ev_find(1, t3, 0), d = inj_ev_find(0, t4, 0);
    CHECK(a >= 0 && b >= 0 && d >= 0);
    if (a >= 0 && d >= 0) {
        EXPECT_EQ(inj_ev(a)->vk, inj_ev(d)->vk);
        EXPECT_EQ(inj_ev(a)->flags, inj_ev(d)->flags);
        EXPECT_EQ(inj_ev(a)->wp, inj_ev(d)->wp);
    }
    if (b >= 0) CHECK(inj_ev(b)->flags & LLKHF_UP);
    if (c < 0) t_note("a zero-flag mouse_event produced no LL mouse hook call");
    inj_hook_end(); inj_restore();
}

/* The wVk/flags truncation: the keyboard injection path takes BYTEs, so only the low byte of wVk selects the key. */
TEST(dx_input_inject_key_vk_low_byte) {
    INJ_ENV(); inj_arm(); REQUIRE(U(SendInput));
    if (!inj_hook_begin(1, 0)) { t_skip("cannot install LL hooks"); return; }
    ULONG_PTR t = inj_tag();
    INPUT in; inj_key(&in, 0x0187, 0, 0, t); EXPECT_EQ(inj_send(1, &in), 1);
    inj_key(&in, 0x0187, 0, KEYEVENTF_KEYUP, t); EXPECT_EQ(inj_send(1, &in), 1);
    inj_pump(60);
    int i = inj_ev_find(0, t, 0);
    CHECK(i >= 0);
    if (i >= 0) EXPECT_EQ(inj_ev(i)->vk, 0x87);
    inj_hook_end(); inj_restore();
}

/* A fresh thread becomes a GUI thread on its first win32k syscall, including a rejected NtUserSendInput. */
static DWORD th_first(void *a) {
    void *before = NULL, *after = NULL;
    k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &before);
    SET_ERR(0);
    UINT r = UN(NtUserSendInput)(0, NULL, 0x28);
    DWORD e = GetLastError();
    k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &after);
    *(int *)a = (before == NULL) | ((after != NULL) << 1) | ((r == 0) << 2) | ((e == 87) << 3);
    return 0;
}
TEST(dx_input_inject_first_syscall_converts_thread) {
    INJ_ENV(); REQUIRE(UN(NtUserSendInput));
    if (t_is_wine()) { t_skip("TEB.Win32ThreadInfo is a Windows concept"); return; }
    volatile int res = 0;
    k_run_gui_thread((k_thread_fn)th_first, (void *)&res);
    if (!(res & 1)) t_note("thread already had Win32ThreadInfo before its first explicit syscall (loader/user32 attach)");
    CHECK(res & 2);   /* TEB.Win32ThreadInfo set by the win32k thunk's PsConvertToGuiThread */
    CHECK(res & 4);
    CHECK(res & 8);
}
