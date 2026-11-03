/* dx_gap_06: syscalls that WOULD change system state or inject input if the caller were privileged,
 * called here only along the analysis-proven rejection branch. They live in a dx_ area so they
 * only run with /destructive: if the harness were ever run as the input thread or as SYSTEM with
 * TCB the same calls could register an API hook or synthesize input. */
#include "dxgap6.h"

/* NtUserRegisterUserApiHook(HMODULE, UNICODE_STRING *fn, HMODULE wow, UNICODE_STRING *fnWow):
 * win32kfull!_RegisterUserApiHook returns 0 with UserSetLastError(5) when ANY of: a hook is
 * already registered, the thread is restricted, the process logon LUID is not the SYSTEM LUID,
 * or the caller lacks SeTcbPrivilege. It does that before reading either string. For a process
 * that is not running as LocalSystem the LUID test alone guarantees the rejection, so the test
 * skips when it finds the SYSTEM logon session. */
static int dxg6_is_system_logon(void) {
    HANDLE tok;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return 1;      /* unknown: treat as unsafe */
    TOKEN_STATISTICS ts; DWORD n = 0;
    int sys = 1;
    if (GetTokenInformation(tok, TokenStatistics, &ts, sizeof ts, &n))
        sys = (ts.AuthenticationId.LowPart == 0x3e7 && ts.AuthenticationId.HighPart == 0);
    CloseHandle(tok);
    return sys;
}
TEST(dx_gap6_register_user_api_hook_unprivileged) {
    dxg6_fn14 f = dxg6_stub("NtUserRegisterUserApiHook"); REQUIRE(f);
    if (dxg6_is_system_logon()) { t_skip("running in the SYSTEM logon session: the call could really register a hook"); return; }
    for (int i = 0; i < 2; i++) {
        SetLastError(0);
        ULONG_PTR r = f(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        CHECK((unsigned long)r == 0);
        EXPECT_ERR(ERROR_ACCESS_DENIED);
    }
}

/* NtMITPostMouseInputMessage(ULONGLONG *src, int msg, ULONGLONG *ctx): not the input thread ->
 * UserSetLastError(5), result 0, nothing is posted. */
TEST(dx_gap6_mit_post_mouse_input_message_rejected) {
    dxg6_fn14 f = dxg6_stub("NtMITPostMouseInputMessage"); REQUIRE(f);
    ULONGLONG src = 0, ctx = 0;
    SetLastError(0);
    CHECK((unsigned long)f((ULONG_PTR)&src, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) == 0);
    EXPECT_ERR(ERROR_ACCESS_DENIED);
    SetLastError(0);
    CHECK((unsigned long)f((ULONG_PTR)&src, 0, (ULONG_PTR)&ctx, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) == 0);
    EXPECT_ERR(ERROR_ACCESS_DENIED);
}
/* NtMITSynthesizeMouseInput(ULONGLONG in[5], options, ULONGLONG *extra): IsInputThread() false ->
 * UserSetLastError(5), result 0; no mouse event is generated. */
TEST(dx_gap6_mit_synthesize_mouse_input_rejected) {
    dxg6_fn14 f = dxg6_stub("NtMITSynthesizeMouseInput"); REQUIRE(f);
    ULONGLONG in[5] = { 0 };
    SetLastError(0);
    CHECK(f((ULONG_PTR)in, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) == 0);
    EXPECT_ERR(ERROR_ACCESS_DENIED);
}
/* NtMITSynthesizeKeyboardInput(options, DWORD in[9], BYTE *keystate): same rejection, result 0. */
TEST(dx_gap6_mit_synthesize_keyboard_input_rejected) {
    dxg6_fn14 f = dxg6_stub("NtMITSynthesizeKeyboardInput"); REQUIRE(f);
    DWORD in[9] = { 0 };
    SetLastError(0);
    CHECK(f(0, (ULONG_PTR)in, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) == 0);
    EXPECT_ERR(ERROR_ACCESS_DENIED);
    SetLastError(0);
    CHECK(f(8, (ULONG_PTR)in, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) == 0);
    EXPECT_ERR(ERROR_ACCESS_DENIED);
}
/* NtMITSynthesizeTouchInput(void *in, 0x5b0 bytes): rejected with error 5, result 0. */
TEST(dx_gap6_mit_synthesize_touch_input_rejected) {
    dxg6_fn14 f = dxg6_stub("NtMITSynthesizeTouchInput"); REQUIRE(f);
    static BYTE in[0x5b0];
    SetLastError(0);
    CHECK((unsigned long)f((ULONG_PTR)in, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) == 0);
    EXPECT_ERR(ERROR_ACCESS_DENIED);
}
