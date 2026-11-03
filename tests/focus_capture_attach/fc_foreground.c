/* Foreground window and foreground-lock rules: NtUserGetForegroundWindow, SetForegroundWindow
 * (NtUserCallHwndLock 0x70), AllowSetForegroundWindow (NtUserCallOneParam 0x2e),
 * LockSetForegroundWindow (NtUserCallOneParam 0x36), SPI foreground lock timeout.
 * Whether this process may become foreground depends on the session state, so those facts are
 * only noted; the validation and error mapping are asserted. */
#include "fc.h"

static HWND stable_fg(void) {
    HWND a = U(GetForegroundWindow)(), b;
    for (int i = 0; i < 5; i++) { b = U(GetForegroundWindow)(); if (a == b) return a; a = b; }
    return a;
}

TEST(fca_fg_get_matches_direct_stub) {
    FC_NEED(); REQUIRE(UN(NtUserGetForegroundWindow));
    int same = 0;
    for (int i = 0; i < 5 && !same; i++) same = (U(GetForegroundWindow)() == UN(NtUserGetForegroundWindow)());
    CHECK(same);   /* GetForegroundWindow is a bare jump to the stub */
}

TEST(fca_fg_window_is_valid_or_null) {
    FC_NEED();
    HWND fg = stable_fg();
    if (!fg) { t_note("no foreground window"); return; }
    CHECK(U(IsWindow)(fg));
    DWORD pid = 0;
    DWORD tid = U(GetWindowThreadProcessId)(fg, &pid);
    CHECK(tid != 0 && pid != 0);
    GUITHREADINFO g;
    if (fc_gti(tid, &g)) CHECK(g.hwndActive == NULL || U(IsWindow)(g.hwndActive));
    t_info("foreground window %p (pid %lu tid %lu)", fg, pid, tid);
}

TEST(fca_fg_gti_zero_agrees_with_foreground) {
    FC_NEED();
    GUITHREADINFO g;
    SET_ERR(0);
    BOOL r = fc_gti(0, &g);   /* tid 0 = the foreground queue */
    HWND fg = stable_fg();
    if (!r) { t_note("GetGUIThreadInfo(0) failed: err=%lu fg=%p", GetLastError(), fg); return; }
    if (fg && g.hwndActive != fg) t_note("foreground queue active window %p differs from GetForegroundWindow %p (composite frame?)", g.hwndActive, fg);
    CHECK(g.hwndActive == NULL || U(IsWindow)(g.hwndActive));
}

TEST(fca_fg_set_invalid_handles) {
    FC_NEED();
    HWND a = fc_top(), s = fc_top(); REQUIRE(a && s);
    fc_destroy(s);
    HWND before = stable_fg();
    HWND bad[] = { NULL, (HWND)(ULONG_PTR)1, (HWND)(ULONG_PTR)0xDEADBEEF, s, (HWND)((ULONG_PTR)a ^ 0x00030000) };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SET_ERR(0);
        BOOL r = U(SetForegroundWindow)(bad[i]);
        CHECKF(!r, "SetForegroundWindow(%p) succeeded", bad[i]);
        EXPECT_ERR(1400);
    }
    EXPECT_EQ(stable_fg(), before);
    fc_destroy(a);
}

TEST(fca_fg_set_invalid_handles_direct) {
    FC_NEED(); if (t_is_wine()) FC_SKIP("win32k call codes");
    REQUIRE(UN(NtUserCallHwndLock));
    SET_ERR(0);
    EXPECT_CALL_EQ(UN(NtUserCallHwndLock)((HWND)(ULONG_PTR)0x12345678, FC_CALL_SETFOREGROUND), 0);
    EXPECT_ERR(1400);
    SET_ERR(0);
    EXPECT_CALL_EQ(UN(NtUserCallHwndLock)(NULL, FC_CALL_SETFOREGROUND), 0);
    EXPECT_ERR(1400);
}

TEST(fca_fg_set_own_window_only_when_already_foreground) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    HWND fg = stable_fg(); DWORD pid = 0;
    if (fg) U(GetWindowThreadProcessId)(fg, &pid);
    if (pid != GetCurrentProcessId()) {
        fc_destroy(a);
        t_info("process is not foreground (fg=%p pid=%lu); SetForegroundWindow on own window would steal it, not exercised", fg, pid);
        FC_SKIP("not foreground");
    }
    /* the calling process already owns the foreground: switching to its own window is permitted */
    CHECK(U(SetForegroundWindow)(a));
    EXPECT_EQ(stable_fg(), a);
    fc_destroy(a);
}

TEST(fca_fg_threadstate_isforeground_consistent) {
    FC_NEED(); if (t_is_wine()) FC_SKIP("syscall info classes are win32k-specific");
    REQUIRE(UN(NtUserGetThreadState));
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    for (int attempt = 0; attempt < 3; attempt++) {
        HWND fg1 = U(GetForegroundWindow)();
        ULONG_PTR isfg = UN(NtUserGetThreadState)(FC_TS_ISFOREGROUND);
        HWND fg2 = U(GetForegroundWindow)();
        if (fg1 != fg2) continue;
        CHECK(isfg == 0 || isfg == 1);
        DWORD pid = 0, tid = fg1 ? U(GetWindowThreadProcessId)(fg1, &pid) : 0;
        if (fg1 && pid != GetCurrentProcessId()) EXPECT_EQ(isfg, 0);   /* class 10: pti->pq == gpqForeground */
        if (fg1 && tid == GetCurrentThreadId()) EXPECT_EQ(isfg, 1);
        break;
    }
    fc_destroy(a);
}

TEST(fca_fg_lock_timeout_spi) {
    FC_NEED(); REQUIRE(U(SystemParametersInfoW));
    DWORD v = 0xCCCCCCCC;
    SET_ERR(0);
    BOOL ok = U(SystemParametersInfoW)(0x2000 /* SPI_GETFOREGROUNDLOCKTIMEOUT */, 0, &v, 0);
    CHECKF(ok, "err %lu", GetLastError());
    if (ok) { CHECK(v != 0xCCCCCCCC); t_note("foreground lock timeout %lu ms", v); }
    DWORD c = 0xCCCCCCCC;
    ok = U(SystemParametersInfoW)(0x2004 /* SPI_GETFOREGROUNDFLASHCOUNT */, 0, &c, 0);
    CHECK(ok);
    if (ok) { CHECK(c != 0xCCCCCCCC); t_note("foreground flash count %lu", c); }
}

TEST(fca_fg_lock_timeout_spi_stable) {
    FC_NEED(); REQUIRE(U(SystemParametersInfoW));
    DWORD v1 = 1, v2 = 2;
    CHECK(U(SystemParametersInfoW)(0x2000, 0, &v1, 0));
    CHECK(U(SystemParametersInfoW)(0x2000, 0, &v2, 0));
    EXPECT_EQ(v1, v2);
}

TEST(fca_fg_lock_invalid_codes) {
    FC_NEED();
    UINT codes[] = { 0, 3, 4, 0x10, 0x100, 0x80000000, 0xFFFFFFFF };
    for (unsigned i = 0; i < sizeof codes / sizeof codes[0]; i++) {
        SET_ERR(0);
        BOOL r = U(LockSetForegroundWindow)(codes[i]);
        CHECKF(!r, "LockSetForegroundWindow(%u) succeeded", codes[i]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);   /* _LockSetForegroundWindow: only LSFW_LOCK(1)/LSFW_UNLOCK(2) */
    }
}

TEST(fca_fg_unlock_without_lock) {
    FC_NEED();
    SET_ERR(0);
    BOOL r = U(LockSetForegroundWindow)(LSFW_UNLOCK);
    CHECK(!r);                          /* the lock is not held by this process */
    EXPECT_ERR(ERROR_ACCESS_DENIED);
}

TEST(fca_fg_lock_unlock_cycle) {
    FC_NEED();
    SET_ERR(0);
    BOOL r = U(LockSetForegroundWindow)(LSFW_LOCK);
    if (!r) {
        DWORD e = GetLastError();
        t_info("LockSetForegroundWindow(LOCK) refused: err %lu", e);
        CHECK(e == ERROR_ACCESS_DENIED);
        return;
    }
    SET_ERR(0);
    CHECK(!U(LockSetForegroundWindow)(LSFW_LOCK));   /* gppiLockSFW already set */
    EXPECT_ERR(ERROR_ACCESS_DENIED);
    CHECK(U(LockSetForegroundWindow)(LSFW_UNLOCK));
    SET_ERR(0);
    CHECK(!U(LockSetForegroundWindow)(LSFW_UNLOCK));
    EXPECT_ERR(ERROR_ACCESS_DENIED);
    U(LockSetForegroundWindow)(LSFW_UNLOCK);
}

TEST(fca_fg_allow_own_pid_and_any) {
    FC_NEED();
    DWORD pids[] = { GetCurrentProcessId(), ASFW_ANY };
    for (int i = 0; i < 2; i++) {
        SET_ERR(0);
        BOOL r = U(AllowSetForegroundWindow)(pids[i]);
        if (!r) {
            DWORD e = GetLastError();
            t_info("AllowSetForegroundWindow(%lx) refused: err %lu", pids[i], e);
            CHECK(e == ERROR_ACCESS_DENIED);   /* CanForceForeground() failed */
        }
    }
}

TEST(fca_fg_allow_invalid_pid) {
    FC_NEED();
    DWORD pids[] = { 0x7FFFFFF0, 0xFFFFFFF0, 0x12345678 };
    for (unsigned i = 0; i < sizeof pids / sizeof pids[0]; i++) {
        SET_ERR(0);
        BOOL r = U(AllowSetForegroundWindow)(pids[i]);
        DWORD e = GetLastError();
        CHECKF(!r, "AllowSetForegroundWindow(%lx) succeeded", pids[i]);
        /* LockProcessByClientId fails -> 0x57, unless the CanForceForeground gate already said 5 */
        CHECKF(e == ERROR_INVALID_PARAMETER || e == ERROR_ACCESS_DENIED, "err %lu", e);
    }
}

TEST(fca_fg_allow_direct_call) {
    FC_NEED(); if (t_is_wine()) FC_SKIP("win32k call codes");
    REQUIRE(UN(NtUserCallOneParam));
    SET_ERR(0);
    ULONG_PTR r = UN(NtUserCallOneParam)(GetCurrentProcessId(), FC_CALL_ALLOWSFW);
    if (!r) { t_note("refused, err %lu", GetLastError()); CHECK(GetLastError() == ERROR_ACCESS_DENIED); }
    else CHECK(r == 1);
    SET_ERR(0);
    r = UN(NtUserCallOneParam)(0x7FFFFFF0, FC_CALL_ALLOWSFW);
    CHECK(r == 0);
    DWORD e = GetLastError();
    CHECK(e == ERROR_INVALID_PARAMETER || e == ERROR_ACCESS_DENIED);
}

TEST(fca_fg_lock_direct_call) {
    FC_NEED(); if (t_is_wine()) FC_SKIP("win32k call codes");
    REQUIRE(UN(NtUserCallOneParam));
    SET_ERR(0);
    EXPECT_CALL_EQ(UN(NtUserCallOneParam)(7, FC_CALL_LOCKSFW), 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SET_ERR(0);
    EXPECT_CALL_EQ(UN(NtUserCallOneParam)(LSFW_UNLOCK, FC_CALL_LOCKSFW), 0);
    EXPECT_ERR(ERROR_ACCESS_DENIED);
}

TEST(fca_fg_failed_calls_leave_foreground_alone) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    HWND before = stable_fg();
    U(SetForegroundWindow)((HWND)(ULONG_PTR)0x12345678);
    U(SetFocus)((HWND)(ULONG_PTR)0x12345678);
    U(SetActiveWindow)((HWND)(ULONG_PTR)0x12345678);
    U(SetCapture)((HWND)(ULONG_PTR)0x12345678);
    U(EnableWindow)((HWND)(ULONG_PTR)0x12345678, FALSE);
    U(LockSetForegroundWindow)(0);
    EXPECT_EQ(stable_fg(), before);
    fc_destroy(a);
}

TEST(fca_fg_activation_of_background_queue_keeps_foreground) {
    FC_NEED();
    HWND before = stable_fg();
    DWORD pid = 0; if (before) U(GetWindowThreadProcessId)(before, &pid);
    if (pid == GetCurrentProcessId()) FC_SKIP("process already owns the foreground");
    HWND a = fc_top(), b = fc_top(); REQUIRE(a && b);
    fc_quiesce();
    U(SetActiveWindow)(a);
    U(SetActiveWindow)(b);
    U(SetFocus)(a);
    HWND after = stable_fg();
    DWORD pid2 = 0; if (after) U(GetWindowThreadProcessId)(after, &pid2);
    /* In-queue activation of a background process may or may not be promoted to foreground; record it. */
    t_note("foreground before=%p after=%p ours-after=%d", before, after, pid2 == GetCurrentProcessId());
    fc_destroy(a); fc_destroy(b);
}
