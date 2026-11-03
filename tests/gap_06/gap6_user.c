/* gap6_user: NtUserCalcMenuBar, NtUserSetProcessMousewheelRoutingMode and the stub identity of
 * NtUserBroadcastThemeChangeEvent. */
#include "gap6.h"

/* ---- NtUserCalcMenuBar(HWND, int leftWidth, int rightWidth, int top, RECT *) ------------------- */

/* win32kfull!NtUserCalcMenuBar: ValidateHwnd(hwnd) failing (ValidateHwndEx falls through to
 * UserSetLastError(0x578)) returns 0 without touching the other arguments. */
TEST(gap6_calcmenubar_invalid_hwnd) {
    g6_fn14 f = g6_stub("NtUserCalcMenuBar"); REQUIRE(f);
    SetLastError(0);
    CHECK((unsigned long)g6_call(f, 0, 0, 0, 0) == 0);
    EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
}
TEST(gap6_calcmenubar_destroyed_hwnd) {
    g6_fn14 f = g6_stub("NtUserCalcMenuBar"); REQUIRE(f);
    HWND w = k_make_window("win32kt gap6 calcmenubar"); REQUIRE(w);
    DestroyWindow(w);
    SetLastError(0);
    CHECK((unsigned long)g6_call(f, (ULONG_PTR)w, 0, 0, 0) == 0);
    EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
}
/* A valid window but a negative left width, right width or top: the third argument group is
 * checked right after the window is thread-locked and gives ERROR_INVALID_PARAMETER and result 0
 * before the RECT (arg 5) is read. */
TEST(gap6_calcmenubar_negative_arguments) {
    g6_fn14 f = g6_stub("NtUserCalcMenuBar"); REQUIRE(f);
    HWND w = k_make_window("win32kt gap6 calcmenubar neg"); REQUIRE(w);
    static const LONG bad[3][3] = { {-1, 0, 0}, {0, -1, 0}, {0, 0, -1} };
    for (int i = 0; i < 3; i++) {
        SetLastError(0);
        ULONG_PTR r = f((ULONG_PTR)w, (ULONG_PTR)(DWORD)bad[i][0], (ULONG_PTR)(DWORD)bad[i][1], (ULONG_PTR)(DWORD)bad[i][2],
                        0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        CHECK((unsigned long)r == 0);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
    SetLastError(0);
    CHECK((unsigned long)f((ULONG_PTR)w, 0x80000000u, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) == 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SetLastError(0);
    CHECK((unsigned long)f((ULONG_PTR)w, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) == 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    DestroyWindow(w);
}

/* ---- NtUserSetProcessMousewheelRoutingMode(int mode) -------------------------------------------- */

/* win32kbase!NtUserSetProcessMousewheelRoutingMode stores the mode into the process flags only
 * for 0, 1 and 2 (clearing/setting bits 0x8000 and 0x10000 of Win32Process+0x334) and returns 1;
 * any other value reaches UserSetLastError(0x57) and returns 0 without touching the flags. */
TEST(gap6_mousewheel_routing_mode_invalid_values) {
    g6_fn14 f = g6_stub("NtUserSetProcessMousewheelRoutingMode"); REQUIRE(f);
    static const DWORD bad[] = { 3, 4, 100, 0x80000000u, 0xFFFFFFFFu };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(0);
        CHECK(g6_call(f, bad[i], 0, 0, 0) == 0);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
}
/* The valid values only flip bits of this process's flags; the child restores mode 0 at the end. */
CHILD(gap6_mousewheel_modes) {
    (void)argc; (void)argv;
    int bad = 0;
    g6_fn14 f = g6_stub("NtUserSetProcessMousewheelRoutingMode");
    if (!f) { printf("  missing export\n"); return 0; }
#define CB(x) do { int ok_ = !!(x); if (!ok_) { printf("  FAIL %s\n", #x); bad++; } } while (0)
    static const int seq[] = { 1, 2, 0, 2, 1, 0 };
    for (unsigned i = 0; i < sizeof seq / sizeof seq[0]; i++)
        CB(g6_call(f, seq[i], 0, 0, 0) == 1);
    SetLastError(0);
    CB(g6_call(f, 7, 0, 0, 0) == 0);
    CB(GetLastError() == ERROR_INVALID_PARAMETER);
    CB(g6_call(f, 0, 0, 0, 0) == 1);
    return bad;
}
TEST(gap6_mousewheel_routing_mode_valid_values_child) {
    REQUIRE(g6_stub("NtUserSetProcessMousewheelRoutingMode"));
    struct k_child_result r = k_child_run("gap6_mousewheel_modes", NULL, 20000);
    EXPECT_EQ(r.exit_code, 0);
}

/* ---- stub identity ------------------------------------------------------------------------------ */

/* NtUserBroadcastThemeChangeEvent posts internal event 0x19 to every themed thread of every
 * process (win32kfull 0x122970 walks gppiList), so it is never called here; only the stub and its
 * service number (tools/win32u_syscalls.tsv) are checked. */
TEST(gap6_stub_numbers_user_misc) {
    g6_fn14 a = g6_stub("NtUserBroadcastThemeChangeEvent");
    g6_fn14 b = g6_stub("NtUserRegisterUserApiHook");
    g6_fn14 c = g6_stub("NtUserCalcMenuBar");
    g6_fn14 d = g6_stub("NtUserSetProcessMousewheelRoutingMode");
    REQUIRE(a && b && c && d);
    g6_check_stub_number("NtUserBroadcastThemeChangeEvent", 5026);
    g6_check_stub_number("NtUserRegisterUserApiHook", 5291);
    g6_check_stub_number("NtUserCalcMenuBar", 4244);
    g6_check_stub_number("NtUserSetProcessMousewheelRoutingMode", 5351);
}
