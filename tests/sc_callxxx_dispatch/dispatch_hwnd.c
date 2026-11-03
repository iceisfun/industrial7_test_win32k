/* sc_callxxx_dispatch - the HWND-taking dispatchers: NtUserCallHwnd, NtUserCallHwndOpt,
 * NtUserCallHwndSafe, NtUserCallHwndLock, NtUserCallHwndLockSafe, NtUserCallHwndParam,
 * NtUserCallHwndParamLock, NtUserCallHwndParamLockSafe.
 *
 * Every one of these shares the same skeleton in win32kfull.sys (analysis cited per function
 * below): EnterCrit, ValidateHwnd(hwnd) [HwndOpt alone also accepts hwnd==0 without validating],
 * range-check the code, and only if both succeed, lock the resolved window and call the
 * table entry; ThreadUnlock1/UserSessionSwitchLeaveCrit always run on the way out. So there are
 * exactly two independent ways to get a 0/NULL return with no dispatch:
 *   (a) the handle fails ValidateHwnd (win32kbase!ValidateHwndEx) -> UserSetLastError(0x578 ==
 *       ERROR_INVALID_WINDOW_HANDLE == 1400) and return 0 - this happens BEFORE the code is even
 *       looked at, so it fires for every code, valid or not;
 *   (b) the handle validates fine but the code is outside that dispatcher's valid range -> lock/
 *       Unlock still happens (so a valid handle is still locked and unlocked), but no
 *       UserSetLastError call exists on this path, so last error is left untouched.
 * ValidateHwndEx's slot checks (index >= cHandleEntries, uniq-stamp mismatch, entry type != window,
 * freed bit) and its cross-desktop branch all end at the SAME UserSetLastError(0x578) label, so a
 * stale or bogus handle gives 1400. A live window of ANOTHER process on the SAME desktop is not
 * rejected (validation only requires same thread, same desktop, or a flag bit), and the only
 * ERROR_ACCESS_DENIED (5) exit is the UIPI integrity-mismatch branch, which cannot fire between
 * same-integrity windows of this session - so 5 is never asserted here.
 *
 * Per-dispatcher valid-range and safe-to-invoke-code facts (win32kfull.sys analysis, 19041.2965):
 *   NtUserCallHwnd   : 0x54..0x5b   - (callers: GetWindowWorker 0x55, GetModernAppWindow 0x56,
 *                                                               GetWindowContextHelpId 0x57, RegisterShellHookWindow 0x58,
 *                                                               MB_DlgProc 0x59, DeregisterShellHookWindow 0x54).
 *   NtUserCallHwndOpt   : 0x5c,0x5d    - SetProgmanWindow/SetTaskmanWindow;
 *                                                               both re-register a shell-wide
 *                                                               singleton window, never invoked.
 *   NtUserCallHwndSafe   : 0x5a,0x5b    - no caller found; range-only tests.
 *   NtUserCallHwndLock : 0x69..0x75   - 0x69 ArrangeIconicWindows (safe,
 *                                                               invoked below); 0x70
 *                                                               SetForegroundWindow (documented,
 *                                                               never invoked - steals foreground).
 *   NtUserCallHwndLockSafe                    : 0x6b ONLY    - despite also special-casing
 *                                                               0x70/0x75/0x78/0x7a/0x7b for a
 *                                                               side effect (gbValidateHandleForIL),
 *                                                               the actual dispatch range-check is
 *                                                               , nothing else -
 *                                                               no caller found for 0x6b.
 *   NtUserCallHwndParam : 0x5e..0x68   - no caller found; range-only tests.
 *   NtUserCallHwndParamLock  : 0x76..0x7d   - 0x79 ShowOwnedPopups (safe, invoked
 *                                                               below on a window with no owned
 *                                                               popups, so it is a true no-op).
 *   NtUserCallHwndParamLockSafe                : 0x7d ONLY    - user32!EnableWindow, confirmed:
 *                                                               "NtUserCallHwndParamLockSafe(hWnd,
 *                                                               bEnable,0x7d)" - invoked below,
 *                                                               reversibly, on our own window.
 */
#include "t.h"
#include "k.h"

UDECL(void *, NtUserCallHwnd,            (HWND hwnd, DWORD code));
UDECL(void *, NtUserCallHwndOpt,         (HWND hwnd, DWORD code));
UDECL(void *, NtUserCallHwndSafe,        (HWND hwnd, DWORD code));
UDECL(void *, NtUserCallHwndLock,        (HWND hwnd, DWORD code));
UDECL(void *, NtUserCallHwndLockSafe,    (HWND hwnd, DWORD code));
UDECL(void *, NtUserCallHwndParam,       (HWND hwnd, ULONG_PTR param, DWORD code));
UDECL(void *, NtUserCallHwndParamLock,   (HWND hwnd, ULONG_PTR param, DWORD code));
UDECL(void *, NtUserCallHwndParamLockSafe,(HWND hwnd, ULONG_PTR param, DWORD code));


/* Wine's win32u exports the generic NtUserCallHwndXxx family but several of them (confirmed:
 * NtUserCallHwndOpt) are literally unimplemented and abort the whole process on invocation
 * rather than returning an error - not a normal test failure Wine's own emulation differences
 * would produce, but a hard process-level abort this corpus must never let happen. Every TEST
 * below that invokes one of these stubs directly calls this guard first. */
#define SKIP_RAW_DISPATCH_UNDER_WINE() \
    do { if (t_is_wine()) { t_skip("Wine's win32u NtUserCallHwndXxx stubs are not reliably " \
        "implemented (may abort unconditionally on invocation) - skipped, Windows is the " \
        "authority for this area"); return; } } while (0)

#define ERR_INVALID_WINDOW_HANDLE 1400u /* 0x578 - window validation */

/* A hidden, off-screen, non-activating window of a private class - the standard "a real HWND to
 * probe" object from k.h, safe to ArrangeIconicWindows/ShowOwnedPopups/EnableWindow against since
 * it has no children and no owned popups. */
static HWND probe_window(void) { return k_make_window("sc_callxxx_probe"); }

/* An HWND integer whose index is at the far end of the handle table and whose uniqueness stamp
 * (high word 0x5A5A) is one no live slot can carry: ValidateHwndEx rejects it either at the
 * "index >= the handle table size" test or at the uniq-stamp compare, both ending in
 * UserSetLastError(0x578). (A small value such as 0x00011111 could be a real window: uniq stamps
 * start at 1 and HWNDs like 0x00010204 are common.) */
static HWND bogus_handle(void) { return (HWND)(ULONG_PTR)0x5A5AFFF0; }

/* =============================================================================================
 * Generic: NULL / bogus / stale handle -> 0 + ERROR_INVALID_WINDOW_HANDLE, for every variant,
 * using each dispatcher's own lowest valid code (so the "invalid handle" path is reached, not
 * the "invalid code" path - ValidateHwnd runs before the code is ever inspected on every one of
 * these functions per the analysis cited in the file header).
 * ============================================================================================= */
TEST(sc_callxxx_dispatch_hwnd_null_handle_fails) {
    SKIP_RAW_DISPATCH_UNDER_WINE();
    REQUIRE(UN(NtUserCallHwnd));
    SetLastError(0);
    void *r = UN(NtUserCallHwnd)(NULL, 0x54);
    EXPECT_EQ((ULONG_PTR)r, 0);
    EXPECT_ERR(ERR_INVALID_WINDOW_HANDLE);
}
TEST(sc_callxxx_dispatch_hwnd_bogus_handle_fails) {
    SKIP_RAW_DISPATCH_UNDER_WINE();
    REQUIRE(UN(NtUserCallHwnd));
    SetLastError(0);
    void *r = UN(NtUserCallHwnd)(bogus_handle(), 0x54);
    EXPECT_EQ((ULONG_PTR)r, 0);
    EXPECT_ERR(ERR_INVALID_WINDOW_HANDLE);
}
TEST(sc_callxxx_dispatch_hwndsafe_null_handle_fails) {
    SKIP_RAW_DISPATCH_UNDER_WINE();
    REQUIRE(UN(NtUserCallHwndSafe));
    SetLastError(0);
    void *r = UN(NtUserCallHwndSafe)(NULL, 0x5a);
    EXPECT_EQ((ULONG_PTR)r, 0);
    EXPECT_ERR(ERR_INVALID_WINDOW_HANDLE);
}
TEST(sc_callxxx_dispatch_hwndlock_null_handle_fails) {
    SKIP_RAW_DISPATCH_UNDER_WINE();
    REQUIRE(UN(NtUserCallHwndLock));
    SetLastError(0);
    void *r = UN(NtUserCallHwndLock)(NULL, 0x69);
    EXPECT_EQ((ULONG_PTR)r, 0);
    EXPECT_ERR(ERR_INVALID_WINDOW_HANDLE);
}
TEST(sc_callxxx_dispatch_hwndlocksafe_bogus_handle_fails) {
    SKIP_RAW_DISPATCH_UNDER_WINE();
    REQUIRE(UN(NtUserCallHwndLockSafe));
    SetLastError(0);
    void *r = UN(NtUserCallHwndLockSafe)(bogus_handle(), 0x6b);
    EXPECT_EQ((ULONG_PTR)r, 0);
    EXPECT_ERR(ERR_INVALID_WINDOW_HANDLE);
}
TEST(sc_callxxx_dispatch_hwndparam_null_handle_fails) {
    SKIP_RAW_DISPATCH_UNDER_WINE();
    REQUIRE(UN(NtUserCallHwndParam));
    SetLastError(0);
    void *r = UN(NtUserCallHwndParam)(NULL, 0, 0x5e);
    EXPECT_EQ((ULONG_PTR)r, 0);
    EXPECT_ERR(ERR_INVALID_WINDOW_HANDLE);
}
TEST(sc_callxxx_dispatch_hwndparamlock_bogus_handle_fails) {
    SKIP_RAW_DISPATCH_UNDER_WINE();
    REQUIRE(UN(NtUserCallHwndParamLock));
    SetLastError(0);
    void *r = UN(NtUserCallHwndParamLock)(bogus_handle(), 0, 0x79);
    EXPECT_EQ((ULONG_PTR)r, 0);
    EXPECT_ERR(ERR_INVALID_WINDOW_HANDLE);
}
TEST(sc_callxxx_dispatch_hwndparamlocksafe_null_handle_fails) {
    SKIP_RAW_DISPATCH_UNDER_WINE();
    REQUIRE(UN(NtUserCallHwndParamLockSafe));
    SetLastError(0);
    void *r = UN(NtUserCallHwndParamLockSafe)(NULL, 0, 0x7d);
    EXPECT_EQ((ULONG_PTR)r, 0);
    EXPECT_ERR(ERR_INVALID_WINDOW_HANDLE);
}

/* Handle lifetime after close: a window this process validly owned a moment ago must fail the
 * exact same way as a handle that was never allocated, once DestroyWindow has run - verified
 * against ValidateHwndEx's own freed-object check ( gates
 * the whole success path; DestroyWindow's kernel side sets that bit before the handle slot is
 * ever reused, which is exactly the HANDLEENTRY.flags bit0 this corpus's k.h documents as
 * "set once the object is destroyed/freed"). */
TEST(sc_callxxx_dispatch_hwndlock_stale_handle_after_destroy_fails) {
    SKIP_RAW_DISPATCH_UNDER_WINE();
    REQUIRE(UN(NtUserCallHwndLock));
    HWND h = probe_window();
    if (!h) { t_skip("k_make_window failed, err %lu", GetLastError()); return; }
    REQUIRE(U(DestroyWindow));
    CHECK(U(DestroyWindow)(h));
    SetLastError(0);
    void *r = UN(NtUserCallHwndLock)(h, 0x69);
    EXPECT_EQ((ULONG_PTR)r, 0);
    EXPECT_ERR(ERR_INVALID_WINDOW_HANDLE);
}
TEST(sc_callxxx_dispatch_hwndparamlocksafe_stale_handle_after_destroy_fails) {
    SKIP_RAW_DISPATCH_UNDER_WINE();
    REQUIRE(UN(NtUserCallHwndParamLockSafe));
    HWND h = probe_window();
    if (!h) { t_skip("k_make_window failed, err %lu", GetLastError()); return; }
    REQUIRE(U(DestroyWindow));
    CHECK(U(DestroyWindow)(h));
    SetLastError(0);
    void *r = UN(NtUserCallHwndParamLockSafe)(h, 0, 0x7d);
    EXPECT_EQ((ULONG_PTR)r, 0);
    EXPECT_ERR(ERR_INVALID_WINDOW_HANDLE);
}

/* =============================================================================================
 * Out-of-range code with a VALID handle: ValidateHwnd succeeds (the handle gets locked
 * and ThreadUnlock1'd), but the dispatch itself is skipped - 0 back, last error untouched, no
 * crash, no observable side effect on the window.
 * ============================================================================================= */
TEST(sc_callxxx_dispatch_hwnd_valid_handle_bad_code_returns_null_no_error) {
    SKIP_RAW_DISPATCH_UNDER_WINE();
    REQUIRE(UN(NtUserCallHwnd));
    HWND h = probe_window();
    if (!h) { t_skip("k_make_window failed, err %lu", GetLastError()); return; }
    DWORD codes[] = { 0, 0x53, 0x5c, 0xffffffff };
    for (size_t i = 0; i < sizeof(codes)/sizeof(codes[0]); i++) {
        SetLastError(0x9000 + (DWORD)i);
        void *r = UN(NtUserCallHwnd)(h, codes[i]);
        CHECKF((ULONG_PTR)r == 0, "code 0x%lx returned %p", (unsigned long)codes[i], r);
        CHECKF(GetLastError() == 0x9000 + (DWORD)i, "code 0x%lx touched last error: %lu", (unsigned long)codes[i], GetLastError());
    }
    REQUIRE(U(IsWindow));
    CHECK(U(IsWindow)(h)); /* still a perfectly valid window - the lock/unlock left no mark */
    U(DestroyWindow)(h);
}
TEST(sc_callxxx_dispatch_hwndparamlock_valid_handle_bad_code_returns_null_no_error) {
    SKIP_RAW_DISPATCH_UNDER_WINE();
    REQUIRE(UN(NtUserCallHwndParamLock));
    HWND h = probe_window();
    if (!h) { t_skip("k_make_window failed, err %lu", GetLastError()); return; }
    SetLastError(0x9500);
    void *r = UN(NtUserCallHwndParamLock)(h, 0, 0x75 /* in HwndLock's range, not HwndParamLock's */);
    /* 0x75 IS in HwndLock's [0x69,0x75] but HwndParamLock's own range is [0x76,0x7d] - 0x75 is
     * one below it, so this is still out-of-range for THIS dispatcher. */
    CHECKF((ULONG_PTR)r == 0, "returned %p", r);
    EXPECT_EQ(GetLastError(), 0x9500u);
    U(DestroyWindow)(h);
}

/* =============================================================================================
 * NtUserCallHwndOpt: the one variant that accepts hwnd == NULL without even calling ValidateHwnd
 * ("if ( ||) && code-in-range) dispatch
 * with == 0"). SetProgmanWindow/SetTaskmanWindow (the only two valid codes) are both
 * shell-singleton setters this corpus will not invoke, so this is a range/NULL-handling test
 * only, via the raw out-of-range path (safe regardless of hwnd since no dispatch occurs).
 * ============================================================================================= */
TEST(sc_callxxx_dispatch_hwndopt_null_handle_out_of_range_code_is_safe_no_dispatch) {
    SKIP_RAW_DISPATCH_UNDER_WINE();
    REQUIRE(UN(NtUserCallHwndOpt));
    /* code 0 is out of HwndOpt's [0x5c,0x5d] range, so NULL hwnd short-circuits to "no dispatch,
     * return 0" without ever reaching the two real (shell-affecting) codes. */
    void *r = UN(NtUserCallHwndOpt)(NULL, 0);
    EXPECT_EQ((ULONG_PTR)r, 0);
}
TEST(sc_callxxx_dispatch_hwndopt_valid_codes_documented_not_invoked) {
    CHECK(0x5c - 0x5c < 2);
    CHECK(0x5d - 0x5c < 2);
    t_note("NtUserCallHwndOpt(0x5c/0x5d) == SetProgmanWindow/SetTaskmanWindow: re-registers the "
           "session's Program Manager / Task Manager singleton window; not invoked by this corpus");
}

/* =============================================================================================
 * Result equality: direct dispatch vs the real public API, on our own hidden window, for the
 * three codes this corpus confirmed are safe and reversible/no-op on a childless window.
 * ============================================================================================= */
TEST(sc_callxxx_dispatch_hwndlock_arrange_iconic_matches_public_api) {
    SKIP_RAW_DISPATCH_UNDER_WINE();
    /* ArrangeIconicWindows(hWnd) == NtUserCallHwndLock(hWnd, 0x69) (user32 forwards it unchanged). On a window with no iconic children this is a
     * true no-op either way, so calling both back to back is safe. */
    REQUIRE(UN(NtUserCallHwndLock));
    REQUIRE(U(ArrangeIconicWindows));
    HWND h = probe_window();
    if (!h) { t_skip("k_make_window failed, err %lu", GetLastError()); return; }
    void *direct = UN(NtUserCallHwndLock)(h, 0x69);
    UINT viaapi = U(ArrangeIconicWindows)(h);
    CHECKF((ULONG_PTR)direct == (ULONG_PTR)viaapi, "direct=%p via-api=%u", direct, viaapi);
    U(DestroyWindow)(h);
}
TEST(sc_callxxx_dispatch_hwndparamlock_show_owned_popups_matches_public_api) {
    SKIP_RAW_DISPATCH_UNDER_WINE();
    /* ShowOwnedPopups(hWnd,fShow) == NtUserCallHwndParamLock(hWnd,fShow,0x79) (user32 forwards it unchanged). Our probe window owns no
     * popups, so toggling this is a true no-op both ways. */
    REQUIRE(UN(NtUserCallHwndParamLock));
    REQUIRE(U(ShowOwnedPopups));
    HWND h = probe_window();
    if (!h) { t_skip("k_make_window failed, err %lu", GetLastError()); return; }
    void *direct = UN(NtUserCallHwndParamLock)(h, TRUE, 0x79);
    BOOL viaapi = U(ShowOwnedPopups)(h, TRUE);
    CHECKF((ULONG_PTR)direct == (ULONG_PTR)viaapi, "direct=%p via-api=%d", direct, viaapi);
    U(DestroyWindow)(h);
}
TEST(sc_callxxx_dispatch_hwndparamlocksafe_enable_window_matches_public_api_and_is_only_valid_code) {
    SKIP_RAW_DISPATCH_UNDER_WINE();
    /* EnableWindow(hWnd,bEnable) == NtUserCallHwndParamLockSafe(hWnd,bEnable,0x7d) (user32 analysis,
     * exact match). This is also the ONLY code NtUserCallHwndParamLockSafe actually dispatches -
     * every other code in the generic HwndParamLock numbering space (0x76..0x7c) falls through to
     * "return 0" for this Safe variant even though those codes are "side-effect" codes for the
     * non-Safe NtUserCallHwndParamLock (gbValidateHandleForIL toggle) - confirmed by re-reading
     * the window's enabled state back after each call. */
    REQUIRE(UN(NtUserCallHwndParamLockSafe));
    REQUIRE(U(EnableWindow));
    REQUIRE(U(IsWindowEnabled));
    HWND h = probe_window();
    if (!h) { t_skip("k_make_window failed, err %lu", GetLastError()); return; }
    CHECK(U(IsWindowEnabled)(h)); /* newly created windows start enabled */

    void *direct_disable = UN(NtUserCallHwndParamLockSafe)(h, FALSE, 0x7d);
    BOOL now_disabled = !U(IsWindowEnabled)(h);
    CHECKF(now_disabled, "direct disable (ret %p) did not take effect", direct_disable);

    BOOL viaapi_enable = U(EnableWindow)(h, TRUE); /* restore */
    CHECKF(viaapi_enable, "EnableWindow(TRUE) restore failed, err %lu", GetLastError());
    CHECK(U(IsWindowEnabled)(h));

    /* every other code in [0x76,0x7c] must be a documented no-dispatch no-op for this Safe
     * variant specifically, even the ones that ARE "side effect" codes for the generic variant */
    DWORD other_codes[] = { 0x76, 0x77, 0x78, 0x79, 0x7a, 0x7b, 0x7c };
    for (size_t i = 0; i < sizeof(other_codes)/sizeof(other_codes[0]); i++) {
        SetLastError(0x7000 + (DWORD)i);
        void *r = UN(NtUserCallHwndParamLockSafe)(h, TRUE, other_codes[i]);
        CHECKF((ULONG_PTR)r == 0, "code 0x%lx returned %p", (unsigned long)other_codes[i], r);
        CHECKF(GetLastError() == 0x7000 + (DWORD)i, "code 0x%lx touched last error", (unsigned long)other_codes[i]);
        CHECK(U(IsWindowEnabled)(h)); /* confirms no dispatch happened - window state unchanged */
    }
    U(DestroyWindow)(h);
}

/* =============================================================================================
 * Lock vs non-lock variants: NtUserCallHwndLock/HwndParamLock lock the resolved window
 * around the dispatch call (win32kfull analysis: ),
 * while plain NtUserCallHwnd/HwndParam do NOT (no lock/unlock pair in their
 * analysis). This corpus has no public counter for "is this object currently HM-locked", so the
 * distinction is not independently observable from user mode - documented as an analysis fact
 * rather than asserted as a CHECK against process state. */
TEST(sc_callxxx_dispatch_lock_variants_wrap_dispatch_in_hmlockobject_decomp_fact) {
    t_note("NtUserCallHwndLock/HwndLockSafe/HwndParamLock/HwndParamLockSafe lock the "
           "resolved tagWND around the table call and ThreadUnlock1() after; NtUserCallHwnd/"
           "HwndParam/HwndOpt/HwndSafe do not - win32kfull.sys analysis fact, not independently "
           "observable from user mode in this corpus");
}

/* =============================================================================================
 * Foreign-process window on the same desktop: a CHILD creates a window and prints its HWND; the
 * parent holds a live handle belonging to a different EPROCESS and runs a harmless code on it.
 * ============================================================================================= */
CHILD(sc_callxxx_dispatch_hwnd_foreign_window_child) {
    (void)argc; (void)argv;
    HWND h = k_make_window("sc_callxxx_foreign_probe");
    if (!h) { printf("NOTE sc_callxxx_dispatch_hwnd_foreign_window_child: k_make_window failed, err %lu\n", GetLastError()); return 0; }
    printf("HWND=0x%llx\n", (unsigned long long)(ULONG_PTR)h);
    fflush(stdout);
    Sleep(2000); /* stay alive long enough for the parent to read the line and issue its call */
    return 0;
}
TEST(sc_callxxx_dispatch_hwndlock_foreign_process_same_desktop_validates) {
    SKIP_RAW_DISPATCH_UNDER_WINE();
    REQUIRE(UN(NtUserCallHwndLock));
    PROCESS_INFORMATION pi; HANDLE hread = NULL, hwrite = NULL;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    if (!CreatePipe(&hread, &hwrite, &sa, 0)) { t_skip("CreatePipe failed, err %lu", GetLastError()); return; }
    SetHandleInformation(hread, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOA si; ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hwrite;
    si.hStdError = hwrite;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    char cmdline[MAX_PATH + 64];
    wsprintfA(cmdline, "\"%s\" --child sc_callxxx_dispatch_hwnd_foreign_window_child", t_self_path());
    ZeroMemory(&pi, sizeof(pi));
    BOOL ok = CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
    CloseHandle(hwrite);
    if (!ok) { CloseHandle(hread); t_skip("CreateProcess failed, err %lu", GetLastError()); return; }

    /* The child's stdout is a pipe and its C runtime may write the line in several pieces, so read until the
     * newline (or the child ends, or the buffer is full). */
    char buf[256]; DWORD got = 0, n = 0; ULONG_PTR foreign = 0;
    while (got < sizeof(buf) - 1 && ReadFile(hread, buf + got, sizeof(buf) - 1 - got, &n, NULL) && n > 0) {
        got += n; buf[got] = 0;
        if (strchr(buf, '\n')) break;
    }
    if (got > 0) {
        buf[got] = 0;
        unsigned long long v = 0;
        if (sscanf(buf, "HWND=0x%llx", &v) == 1) foreign = (ULONG_PTR)v;
    }
    CloseHandle(hread);

    if (foreign == 0) {
        t_skip("foreign child did not report a window handle");
    } else {
 /* ValidateHwndEx accepts a window of ANOTHER process when it lives on the
 * same desktop and UIPI sees equal integrity - that is how every
         * cross-process user32 call works. So unlike a stale/bogus handle this one validates: the
         * dispatch runs and last error is not 1400 (nor touched: ArrangeIconicWindows on a
         * childless window sets none). */
        SetLastError(0x4242);
        NtUserCallHwndLock_pfn f = UN(NtUserCallHwndLock);
        (void)f(( HWND)foreign, 0x69);
        CHECKF(GetLastError() == 0x4242, "foreign same-desktop window: err %lu (1400 would mean it was rejected)", GetLastError());
    }
    TerminateProcess(pi.hProcess, 0);
    WaitForSingleObject(pi.hProcess, 5000);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
}

/* =============================================================================================
 * Variant table: (dispatcher, code, user32 API that emits it, safe-to-run). The codes come from
 * the user32 call sites; the ranges are the win32kfull range checks quoted in the file header.
 * ============================================================================================= */
enum { V_HWND, V_OPT, V_SAFE, V_LOCK, V_LOCKSAFE, V_PARAM, V_PARAMLOCK, V_PARAMLOCKSAFE, V_N };
static const struct { const char *name; DWORD lo, n; int has_param; } g_vr[V_N] = {
    [V_HWND]          = { "NtUserCallHwnd",             0x54, 8,   0 },
    [V_OPT]           = { "NtUserCallHwndOpt",          0x5c, 2,   0 },
    [V_SAFE]          = { "NtUserCallHwndSafe",         0x5a, 2,   0 },
    [V_LOCK]          = { "NtUserCallHwndLock",         0x69, 0xd, 0 },
    [V_LOCKSAFE]      = { "NtUserCallHwndLockSafe",     0x6b, 1,   0 },
    [V_PARAM]         = { "NtUserCallHwndParam",        0x5e, 0xb, 1 },
    [V_PARAMLOCK]     = { "NtUserCallHwndParamLock",    0x76, 8,   1 },
    [V_PARAMLOCKSAFE] = { "NtUserCallHwndParamLockSafe",0x7d, 1,   1 },
};

static void *callv(int v, HWND h, ULONG_PTR param, DWORD code) {
    switch (v) {
    case V_HWND:          return UN(NtUserCallHwnd)(h, code);
    case V_OPT:           return UN(NtUserCallHwndOpt)(h, code);
    case V_SAFE:          return UN(NtUserCallHwndSafe)(h, code);
    case V_LOCK:          return UN(NtUserCallHwndLock)(h, code);
    case V_LOCKSAFE:      return UN(NtUserCallHwndLockSafe)(h, code);
    case V_PARAM:         return UN(NtUserCallHwndParam)(h, param, code);
    case V_PARAMLOCK:     return UN(NtUserCallHwndParamLock)(h, param, code);
    case V_PARAMLOCKSAFE: return UN(NtUserCallHwndParamLockSafe)(h, param, code);
    }
    return NULL;
}
static int have_all_variants(void) {
    return UN(NtUserCallHwnd) && UN(NtUserCallHwndOpt) && UN(NtUserCallHwndSafe) && UN(NtUserCallHwndLock) &&
           UN(NtUserCallHwndLockSafe) && UN(NtUserCallHwndParam) && UN(NtUserCallHwndParamLock) && UN(NtUserCallHwndParamLockSafe);
}
#define REQUIRE_ALL_VARIANTS() do { SKIP_RAW_DISPATCH_UNDER_WINE(); if (!have_all_variants()) { t_skip("a win32u NtUserCallHwnd* export is missing"); return; } } while (0)

typedef void (*row_fn)(HWND h);
struct hrow { int v; DWORD code; const char *api; row_fn run; };
#define LO32H(p) ((DWORD)((ULONG_PTR)(p) & 0xffffffffu))

static void run_ctxhelp(HWND h) {
    /* GetWindowContextHelpId == NtUserCallHwnd(h,0x57); SetWindowContextHelpId == NtUserCallHwndParam(h,id,0x65) */
    CHECK(LO32H(UN(NtUserCallHwnd)(h, 0x57)) == U(GetWindowContextHelpId)(h));
    void *r = UN(NtUserCallHwndParam)(h, 0x2468, 0x65);
    CHECK(LO32H(r) == 1);
    CHECK(LO32H(UN(NtUserCallHwnd)(h, 0x57)) == 0x2468);
    CHECK(U(GetWindowContextHelpId)(h) == 0x2468);
    U(SetWindowContextHelpId)(h, 0);
}
static void run_arrange(HWND h) {
    void *d = UN(NtUserCallHwndLock)(h, 0x69);
    CHECK(LO32H(d) == U(ArrangeIconicWindows)(h));
}
static void run_drawmenubar(HWND h) {
    void *d = UN(NtUserCallHwndLock)(h, 0x6a);
    CHECK(LO32H(d) == (DWORD)U(DrawMenuBar)(h));
}
static void run_showowned(HWND h) {
    void *d = UN(NtUserCallHwndParamLock)(h, TRUE, 0x79);
    CHECK(LO32H(d) == (DWORD)U(ShowOwnedPopups)(h, TRUE));
}
static void run_enable(HWND h) {
    UN(NtUserCallHwndParamLockSafe)(h, FALSE, 0x7d);
    CHECK(!U(IsWindowEnabled)(h));
    U(EnableWindow)(h, TRUE);
    CHECK(U(IsWindowEnabled)(h));
}
static const struct hrow g_rows[] = {
    { V_HWND,  0x54, "DeregisterShellHookWindow",  NULL },            /* unregisters a shell hook window - not run */
    { V_HWND,  0x55, "GetWindowWorker",            NULL },            /* internal helper */
    { V_HWND,  0x56, "GetModernAppWindow",         NULL },            /* undocumented export, result is shell-state dependent */
    { V_HWND,  0x57, "GetWindowContextHelpId",     run_ctxhelp },
    { V_HWND,  0x58, "RegisterShellHookWindow",    NULL },            /* adds the window to the session shell-hook list - not run */
    { V_HWND,  0x59, "MB_DlgProc",                 NULL },            /* message-box internal */
    { V_OPT,   0x5c, "SetProgmanWindow",           NULL },            /* shell singleton */
    { V_OPT,   0x5d, "SetTaskmanWindow",           NULL },            /* shell singleton */
    { V_PARAM, 0x5e, "class data",              NULL },            /* internal, every class lookup */
    { V_PARAM, 0x5f, "ClearWindowState",           NULL },
    { V_PARAM, 0x60, "MLEditWndProc",              NULL },
    { V_PARAM, 0x61, "NotifyOverlayWindow",        NULL },
    { V_PARAM, 0x62, "RegisterKeyboardCorrectionCallout", NULL },
    { V_PARAM, 0x63, "DefDlgProcWorker",           NULL },
    { V_PARAM, 0x64, "MDI next",                 NULL },
    { V_PARAM, 0x65, "SetWindowContextHelpId",     run_ctxhelp },
    { V_PARAM, 0x66, "SetWindowState",             NULL },
    { V_PARAM, 0x67, "RegisterWindowArrangementCallout", NULL },
    { V_PARAM, 0x68, "EnableModernAppWindowKeyboardIntercept", NULL },
    { V_LOCK,  0x69, "ArrangeIconicWindows",       run_arrange },
    { V_LOCK,  0x6a, "DrawMenuBar",                run_drawmenubar },
    { V_LOCK,  0x6c, "menu can-close",              NULL },
    { V_LOCK,  0x6d, "RealMDIRedrawFrame",         NULL },
    { V_LOCK,  0x6e, "set frame title",           NULL },
    { V_LOCK,  0x70, "SetForegroundWindow",        NULL },            /* steals foreground */
    { V_LOCK,  0x71, "DefFrameProcWorker",         NULL },
    { V_LOCK,  0x73, "UpdateWindow",               NULL },
    { V_LOCK,  0x74, "SetCancelRotationDelayHintWindow", NULL },
    { V_LOCK,  0x75, "GetWindowTrackInfoAsync",    NULL },
    { V_PARAMLOCK, 0x79, "ShowOwnedPopups",        run_showowned },
    { V_PARAMLOCKSAFE, 0x7d, "EnableWindow",       run_enable },
};
#define NROWS (sizeof(g_rows)/sizeof(g_rows[0]))

/* Every row's code lies inside the range its dispatcher accepts - a wrong row would mean the
 * table (not the kernel) is wrong. */
TEST(sc_callxxx_dispatch_variant_table_codes_inside_dispatcher_ranges) {
    for (size_t i = 0; i < NROWS; i++) {
        int v = g_rows[i].v;
        CHECKF(g_rows[i].code - g_vr[v].lo < g_vr[v].n, "%s: code 0x%lx outside [0x%lx,+%lu)", g_rows[i].api, g_rows[i].code, g_vr[v].lo, g_vr[v].n);
    }
}
/* Run every safe row against a fresh hidden window. */
TEST(sc_callxxx_dispatch_variant_table_safe_rows) {
    REQUIRE_ALL_VARIANTS();
    REQUIRE(U(GetWindowContextHelpId)); REQUIRE(U(SetWindowContextHelpId)); REQUIRE(U(ArrangeIconicWindows));
    REQUIRE(U(DrawMenuBar)); REQUIRE(U(ShowOwnedPopups)); REQUIRE(U(EnableWindow)); REQUIRE(U(IsWindowEnabled)); REQUIRE(U(DestroyWindow));
    int ran = 0;
    for (size_t i = 0; i < NROWS; i++) {
        if (!g_rows[i].run) continue;
        HWND h = probe_window();
        if (!h) { t_skip("k_make_window failed, err %lu", GetLastError()); return; }
        g_rows[i].run(h);
        U(DestroyWindow)(h);
        ran++;
    }
    CHECKF(ran >= 5, "only %d safe rows ran", ran);
}

/* Bad handle x every dispatcher. Validation precedes the code check in all eight, so even each
 * dispatcher's real first code is never reached: 0 and ERROR_INVALID_WINDOW_HANDLE. NtUserCallHwndOpt
 * is the one exception for NULL (no validation, and no dispatch either since code 0 is out of range). */
TEST(sc_callxxx_dispatch_all_variants_bad_handles_fail_1400) {
    REQUIRE_ALL_VARIANTS();
    REQUIRE(U(DestroyWindow));
    HWND stale = probe_window();
    if (!stale) { t_skip("k_make_window failed, err %lu", GetLastError()); return; }
    U(DestroyWindow)(stale);
    HWND bad[3] = { NULL, bogus_handle(), stale };
    for (int v = 0; v < V_N; v++) {
        for (int k = 0; k < 3; k++) {
            if (v == V_OPT && k == 0) continue;
            SetLastError(0);
            void *r = callv(v, bad[k], 0, g_vr[v].lo);
            DWORD e = GetLastError();
            CHECKF(r == NULL, "%s(handle #%d) returned %p", g_vr[v].name, k, r);
            CHECKF(e == ERR_INVALID_WINDOW_HANDLE, "%s(handle #%d) err %lu want 1400", g_vr[v].name, k, e);
        }
    }
    /* NULL on Opt with an in-range code would dispatch into the shell setters: not run. With a
     * dead code it returns 0 and leaves last error alone. */
    SetLastError(0x5151);
    CHECK(UN(NtUserCallHwndOpt)(NULL, 0x100) == NULL);
    EXPECT_EQ(GetLastError(), 0x5151u);
}

/* Dead codes with a VALID handle: every dispatcher returns 0 and leaves last error alone. The
 * excluded codes are the ones that, before/instead of dispatching, flip gbValidateHandleForIL
 * (0x70/0x75/0x78/0x7a/0x7b on the Lock family, 0x5e/-1 on HwndParam) or reach the extra
 * pre-validation helper (HwndLock 0x7a/0x7b). */
static int is_flag_code(DWORD c) { return c == 0x70 || c == 0x75 || c == 0x78 || c == 0x7a || c == 0x7b || c == 0x5e || c == 0xffffffffu; }
TEST(sc_callxxx_dispatch_all_variants_dead_codes_valid_handle_sweep) {
    REQUIRE_ALL_VARIANTS();
    HWND h = probe_window();
    if (!h) { t_skip("k_make_window failed, err %lu", GetLastError()); return; }
    for (int v = 0; v < V_N; v++) {
        int bad = 0, n = 0;
        for (DWORD c = 0; c < 0x200; c++) {
            if (c - g_vr[v].lo < g_vr[v].n) continue;
            if (is_flag_code(c)) continue;
            SetLastError(0x6C6C);
            void *r = callv(v, h, 0, c);
            n++;
            if (r != NULL || GetLastError() != 0x6C6C) { bad++; if (bad < 4) printf("  %s code 0x%lx: ret %p err %lu\n", g_vr[v].name, c, r, GetLastError()); }
        }
        CHECKF(bad == 0, "%s: %d of %d dead codes dispatched or touched last error", g_vr[v].name, bad, n);
    }
    CHECK(U(IsWindow)(h));
    U(DestroyWindow)(h);
}

/* NtUserCallHwndLockSafe and NtUserCallHwndParamLockSafe dispatch exactly one code each
 * (0x6b and 0x7d), unlike the generic Lock/ParamLock ranges: the neighbours that ARE live for the
 * non-Safe dispatchers are dead here. Checked with the harmless live codes only. */
TEST(sc_callxxx_dispatch_safe_variants_have_single_live_code) {
    REQUIRE_ALL_VARIANTS();
    HWND h = probe_window();
    if (!h) { t_skip("k_make_window failed, err %lu", GetLastError()); return; }
    DWORD lock_live[] = { 0x69, 0x6a, 0x6c, 0x6d, 0x6e, 0x71, 0x73, 0x74 };
    for (size_t i = 0; i < sizeof(lock_live)/sizeof(lock_live[0]); i++) {
        SetLastError(0x7A7A);
        void *r = UN(NtUserCallHwndLockSafe)(h, lock_live[i]);
        CHECKF(r == NULL, "LockSafe code 0x%lx returned %p", lock_live[i], r);
        CHECK(GetLastError() == 0x7A7A);
    }
    U(DestroyWindow)(h);
}

/* lock/unlock balance: the Lock family locks the tagWND around the call, also
 * when the code is dead. A leaked lock would keep the destroyed window alive, so the process's
 * USER object count (GetGuiResources) would stay above the baseline after DestroyWindow. */
TEST(sc_callxxx_dispatch_lock_variants_balance_hmlock_user_object_count) {
    REQUIRE_ALL_VARIANTS();
    REQUIRE(U(GetGuiResources)); REQUIRE(U(DestroyWindow));
    HWND warm = probe_window();   /* make sure the helper class etc. exist before the baseline */
    if (!warm) { t_skip("k_make_window failed, err %lu", GetLastError()); return; }
    U(DestroyWindow)(warm);
    DWORD base = U(GetGuiResources)(GetCurrentProcess(), GR_USEROBJECTS);
    HWND h = probe_window();
    if (!h) { t_skip("k_make_window failed, err %lu", GetLastError()); return; }
    t_note("USER objects: baseline %lu, with window %lu", base, U(GetGuiResources)(GetCurrentProcess(), GR_USEROBJECTS));
    CHECKF(U(GetGuiResources)(GetCurrentProcess(), GR_USEROBJECTS) > base, "window creation did not add a USER object");
    for (int i = 0; i < 200; i++) {
        UN(NtUserCallHwndLock)(h, 0x69);
        UN(NtUserCallHwndLock)(h, 0x7e);                /* dead code, handle still locked/unlocked */
        UN(NtUserCallHwndParamLock)(h, TRUE, 0x79);
        UN(NtUserCallHwndParamLock)(h, 0, 0x7e);
        UN(NtUserCallHwndLockSafe)(h, 0x69);
        UN(NtUserCallHwndParam)(h, 0, 0x7e);
        UN(NtUserCallHwnd)(h, 0x7e);
        UN(NtUserCallHwndSafe)(h, 0x7e);
    }
    CHECK(U(DestroyWindow)(h));
    DWORD after = U(GetGuiResources)(GetCurrentProcess(), GR_USEROBJECTS);
    CHECKF(after == base, "USER objects %lu after destroy, baseline %lu", after, base);
}

/* Window owned by ANOTHER THREAD of this process: ValidateHwnd does not require the
 * caller to own the window, so a Lock-family call runs and matches the public API. */
struct xt { HANDLE ready, done; HWND h; };
static DWORD WINAPI xt_thread(LPVOID p) {
    struct xt *x = (struct xt *)p;
    x->h = k_make_window("sc_callxxx_xthread");
    SetEvent(x->ready);
    WaitForSingleObject(x->done, 5000);
    if (x->h && U(DestroyWindow)) U(DestroyWindow)(x->h);
    return 0;
}
TEST(sc_callxxx_dispatch_hwndlock_other_thread_window_validates) {
    REQUIRE_ALL_VARIANTS();
    REQUIRE(U(ArrangeIconicWindows));
    struct xt x = { CreateEventA(NULL, TRUE, FALSE, NULL), CreateEventA(NULL, TRUE, FALSE, NULL), NULL };
    HANDLE th = CreateThread(NULL, 0, xt_thread, &x, 0, NULL);
    if (th && WaitForSingleObject(x.ready, 5000) == WAIT_OBJECT_0 && x.h) {
        SetLastError(0x4343);
        void *d = UN(NtUserCallHwndLock)(x.h, 0x69);
        CHECKF(GetLastError() == 0x4343, "other-thread window rejected: err %lu", GetLastError());
        UINT a = U(ArrangeIconicWindows)(x.h);
        CHECKF(LO32H(d) == a, "direct=%p api=%u", d, a);
        CHECK(U(IsWindow)(x.h));
    } else {
        t_skip("helper thread/window creation failed");
    }
    SetEvent(x.done);
    if (th) { WaitForSingleObject(th, 5000); CloseHandle(th); }
    CloseHandle(x.ready); CloseHandle(x.done);
}

/* Context help id lives with the window: default 0 (documented), then round-trips, and a dead
 * handle fails with 1400 through the public wrappers as well. */
TEST(sc_callxxx_dispatch_hwnd_context_help_id_roundtrip) {
    REQUIRE(U(GetWindowContextHelpId)); REQUIRE(U(SetWindowContextHelpId)); REQUIRE(U(DestroyWindow));
    HWND h = probe_window();
    if (!h) { t_skip("k_make_window failed, err %lu", GetLastError()); return; }
    CHECKF(U(GetWindowContextHelpId)(h) == 0, "new window help id %lu", U(GetWindowContextHelpId)(h));
    CHECK(U(SetWindowContextHelpId)(h, 0x1234));
    CHECK(U(GetWindowContextHelpId)(h) == 0x1234);
    CHECK(U(SetWindowContextHelpId)(h, 0));
    CHECK(U(GetWindowContextHelpId)(h) == 0);
    U(DestroyWindow)(h);
    SetLastError(0);
    CHECK(!U(SetWindowContextHelpId)(h, 5));
    if (!t_is_wine()) EXPECT_ERR(ERR_INVALID_WINDOW_HANDLE);
}
TEST(sc_callxxx_dispatch_hwnd_context_help_id_direct) {
    SKIP_RAW_DISPATCH_UNDER_WINE();
    REQUIRE(UN(NtUserCallHwnd)); REQUIRE(UN(NtUserCallHwndParam)); REQUIRE(U(DestroyWindow));
    REQUIRE(U(GetWindowContextHelpId));
    HWND h = probe_window();
    if (!h) { t_skip("k_make_window failed, err %lu", GetLastError()); return; }
    CHECK(LO32H(UN(NtUserCallHwnd)(h, 0x57)) == 0);
    CHECK(LO32H(UN(NtUserCallHwndParam)(h, 0xBEEF, 0x65)) == 1);
    CHECK(LO32H(UN(NtUserCallHwnd)(h, 0x57)) == 0xBEEF);
    CHECK(U(GetWindowContextHelpId)(h) == 0xBEEF);
    U(DestroyWindow)(h);
    SetLastError(0);
    CHECK(UN(NtUserCallHwnd)(h, 0x57) == NULL);
    EXPECT_ERR(ERR_INVALID_WINDOW_HANDLE);
}

/* DrawMenuBar on a handle that is not a window fails through user32 with 1400 as well (the
 * public wrapper is a bare NtUserCallHwndLock(hWnd,0x6a)). */
TEST(sc_callxxx_dispatch_hwndlock_draw_menu_bar_invalid_handle) {
    SKIP_RAW_DISPATCH_UNDER_WINE();
    REQUIRE(U(DrawMenuBar));
    SetLastError(0);
    CHECK(!U(DrawMenuBar)(NULL));
    EXPECT_ERR(ERR_INVALID_WINDOW_HANDLE);
    SetLastError(0);
    CHECK(!U(DrawMenuBar)(bogus_handle()));
    EXPECT_ERR(ERR_INVALID_WINDOW_HANDLE);
}
TEST(sc_callxxx_dispatch_hwndparamlocksafe_enable_window_invalid_handle) {
    SKIP_RAW_DISPATCH_UNDER_WINE();
    REQUIRE(U(EnableWindow));
    SetLastError(0);
    CHECK(!U(EnableWindow)(bogus_handle(), TRUE));
    EXPECT_ERR(ERR_INVALID_WINDOW_HANDLE);
}
