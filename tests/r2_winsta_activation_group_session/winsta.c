/* Window-station level guards: NtUserLockWindowStation / UnlockWindowStation / SetWindowStationUser (only the
 * logon process may use them: _LockWindowStation, _UnlockWindowStation and _SetWindowStationUser compare
 * PsGetCurrentProcessId() with gpidLogon and otherwise UserSetLastError(5)), NtUserCheckProcessSession and
 * NtUserGetDesktopID (DWM only), NtUserCheckAccessForIntegrityLevel. */
#include "r2.h"

/* ---- Lock / Unlock ---------------------------------------------------------------------------- */

/* ValidateHwinsta -> ObReferenceObjectByHandle(STATUS_INVALID_HANDLE) -> RtlNtStatusToDosError -> 6 */
RT(lock_winsta_invalid_handles) {
    REQUIRE(UN(NtUserLockWindowStation));
    for (unsigned i = 0; i < N_BAD_HANDLES; i++) {
        SET_ERR(SENT);
        ULONG_PTR r = UN(NtUserLockWindowStation)((HANDLE)r2_bad_handles[i]);
        CKF(R_D(r) == 0, "h=%p r=%llx", (void *)r2_bad_handles[i], (unsigned long long)r);
        CKF(GetLastError() == ERR_INVALID_HANDLE, "h=%p gle=%lu", (void *)r2_bad_handles[i], GetLastError());
    }
}
RT(unlock_winsta_invalid_handles) {
    REQUIRE(UN(NtUserUnlockWindowStation));
    for (unsigned i = 0; i < N_BAD_HANDLES; i++) {
        SET_ERR(SENT);
        ULONG_PTR r = UN(NtUserUnlockWindowStation)((HANDLE)r2_bad_handles[i]);
        CKF(R_D(r) == 0, "h=%p r=%llx", (void *)r2_bad_handles[i], (unsigned long long)r);
        CKF(GetLastError() == ERR_INVALID_HANDLE, "h=%p gle=%lu", (void *)r2_bad_handles[i], GetLastError());
    }
}
RT(lock_unlock_winsta_wrong_type_handle) {
    REQUIRE(UN(NtUserLockWindowStation)); REQUIRE(UN(NtUserUnlockWindowStation));
    HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    SET_ERR(SENT);
    CHECK(R_D(UN(NtUserLockWindowStation)(ev)) == 0);
    DWORD e1 = GetLastError();
    SET_ERR(SENT);
    CHECK(R_D(UN(NtUserUnlockWindowStation)(ev)) == 0);
    DWORD e2 = GetLastError();
    CHECK(e1 != SENT && e1 != 0);   /* ObReferenceObjectByHandle type mismatch is reported, never silently ignored */
    CHECK(e1 == e2);
    t_note("event handle as window station: gle=%lu", e1);
    CloseHandle(ev);
}
RT(lock_winsta_denied_for_non_logon_process) {
    REQUIRE(UN(NtUserLockWindowStation)); REQUIRE(U(CreateWindowStationW));
    HWINSTA w = r2_mk_winsta("lk1", WSTA_ALL_, FALSE, NULL);
    if (!w) { t_skip("CreateWindowStation refused for the unprivileged token (gle=%lu)", GetLastError()); return; }
    SET_ERR(SENT);
    ULONG_PTR r = UN(NtUserLockWindowStation)(w);   /* _LockWindowStation: current pid != gpidLogon -> 5 */
    CK(R_D(r) == 0);
    EXPECT_ERR(ERR_ACCESS_DENIED);
    USEROBJECTFLAGS f; DWORD need = 0;
    CHECK(U(GetUserObjectInformationW)(w, 1, &f, sizeof f, &need));
    t_note("flags after denied lock: 0x%lx", f.dwFlags);
    U(CloseWindowStation)(w);
}
RT(unlock_winsta_denied_for_non_logon_process) {
    REQUIRE(UN(NtUserUnlockWindowStation)); REQUIRE(U(CreateWindowStationW));
    HWINSTA w = r2_mk_winsta("ulk1", WSTA_ALL_, FALSE, NULL);
    if (!w) { t_skip("CreateWindowStation refused for the unprivileged token (gle=%lu)", GetLastError()); return; }
    SET_ERR(SENT);
    CHECK(R_D(UN(NtUserUnlockWindowStation)(w)) == 0);   /* _UnlockWindowStation: current pid != gpidLogon -> 5 */
    EXPECT_ERR(ERR_ACCESS_DENIED);
    U(CloseWindowStation)(w);
}
RT(lock_unlock_process_winsta_denied) {
    REQUIRE(UN(NtUserLockWindowStation)); REQUIRE(UN(NtUserUnlockWindowStation)); REQUIRE(U(GetProcessWindowStation));
    HWINSTA w = U(GetProcessWindowStation)();
    if (!w) { t_skip("no process window station"); return; }
    SET_ERR(SENT); CHECK(R_D(UN(NtUserLockWindowStation)(w)) == 0); EXPECT_ERR(ERR_ACCESS_DENIED);
    SET_ERR(SENT); CHECK(R_D(UN(NtUserUnlockWindowStation)(w)) == 0); EXPECT_ERR(ERR_ACCESS_DENIED);
    /* the session's interactive station must still be usable afterwards */
    USEROBJECTFLAGS f; DWORD need = 0;
    CHECK(U(GetUserObjectInformationW)(w, 1, &f, sizeof f, &need) || GetLastError() == ERR_ACCESS_DENIED);
}
/* Lock needs only a valid station handle (access 0 is requested), so the access error comes from the pid check
 * and not from the handle: a handle opened with the bare minimum rights fails the same way. */
RT(lock_winsta_minimal_access_handle_still_reaches_pid_check) {
    REQUIRE(UN(NtUserLockWindowStation)); REQUIRE(U(CreateWindowStationW));
    HWINSTA w = r2_mk_winsta("lk2", WINSTA_ENUMERATE, FALSE, NULL);
    if (!w) { t_skip("CreateWindowStation refused for the unprivileged token (gle=%lu)", GetLastError()); return; }
    ULONG g = 0;
    if (r2_granted(w, &g)) CHECK((g & WINSTA_ENUMERATE) != 0);
    SET_ERR(SENT);
    CHECK(R_D(UN(NtUserLockWindowStation)(w)) == 0);
    EXPECT_ERR(ERR_ACCESS_DENIED);
    U(CloseWindowStation)(w);
}
RT(lock_unlock_after_close_is_invalid_handle) {
    REQUIRE(UN(NtUserLockWindowStation)); REQUIRE(UN(NtUserUnlockWindowStation)); REQUIRE(U(CreateWindowStationW));
    HWINSTA w = r2_mk_winsta("lk3", WSTA_ALL_, FALSE, NULL);
    if (!w) { t_skip("CreateWindowStation refused for the unprivileged token (gle=%lu)", GetLastError()); return; }
    CHECK(U(CloseWindowStation)(w));
    SET_ERR(SENT); CHECK(R_D(UN(NtUserLockWindowStation)(w)) == 0); EXPECT_ERR(ERR_INVALID_HANDLE);
    SET_ERR(SENT); CHECK(R_D(UN(NtUserUnlockWindowStation)(w)) == 0); EXPECT_ERR(ERR_INVALID_HANDLE);
}
/* user32 exports forward straight to the same stubs */
RT(lock_unlock_user32_exports_match_stub_results) {
    typedef BOOL (WINAPI *pfn)(HWINSTA);
    pfn lk = (pfn)r2_proc_in("user32.dll", "LockWindowStation"), ul = (pfn)r2_proc_in("user32.dll", "UnlockWindowStation");
    REQUIRE(lk); REQUIRE(ul);
    SET_ERR(SENT);
    CHECK(lk((HWINSTA)0x7777770) == FALSE);
    EXPECT_ERR(ERR_INVALID_HANDLE);
    SET_ERR(SENT);
    CHECK(ul((HWINSTA)0x7777770) == FALSE);
    EXPECT_ERR(ERR_INVALID_HANDLE);
}

/* ---- SetWindowStationUser --------------------------------------------------------------------- */
RT(set_winsta_user_invalid_handles) {
    REQUIRE(UN(NtUserSetWindowStationUser));
    ULONGLONG luid = 0x1234;
    for (unsigned i = 0; i < N_BAD_HANDLES; i++) {
        SET_ERR(SENT);
        ULONG_PTR r = UN(NtUserSetWindowStationUser)((HANDLE)r2_bad_handles[i], &luid, NULL, 0);
        CKF(R_D(r) == 0, "h=%p", (void *)r2_bad_handles[i]);
        CKF(GetLastError() == ERR_INVALID_HANDLE, "h=%p gle=%lu", (void *)r2_bad_handles[i], GetLastError());
    }
}
RT(set_winsta_user_denied_for_non_logon_process) {
    REQUIRE(UN(NtUserSetWindowStationUser)); REQUIRE(U(CreateWindowStationW));
    HWINSTA w = r2_mk_winsta("swu1", WSTA_ALL_, FALSE, NULL);
    if (!w) { t_skip("CreateWindowStation refused for the unprivileged token (gle=%lu)", GetLastError()); return; }
    ULONGLONG luid = 0x1234;
    SET_ERR(SENT);
    CHECK(R_D(UN(NtUserSetWindowStationUser)(w, &luid, NULL, 0)) == 0);   /* _SetWindowStationUser pid check -> 5 */
    EXPECT_ERR(ERR_ACCESS_DENIED);
    U(CloseWindowStation)(w);
}
RT(set_winsta_user_with_sid_buffer_denied_for_non_logon_process) {
    REQUIRE(UN(NtUserSetWindowStationUser)); REQUIRE(U(CreateWindowStationW));
    HWINSTA w = r2_mk_winsta("swu2", WSTA_ALL_, FALSE, NULL);
    if (!w) { t_skip("CreateWindowStation refused for the unprivileged token (gle=%lu)", GetLastError()); return; }
    ULONGLONG luid = 0x1234;
    DWORD sidbuf[8] = { 0 };   /* ProbeForRead(sid, len, alignment 4) needs a 4-aligned readable buffer */
    SET_ERR(SENT);
    CHECK(R_D(UN(NtUserSetWindowStationUser)(w, &luid, sidbuf, sizeof sidbuf)) == 0);
    EXPECT_ERR(ERR_ACCESS_DENIED);
    U(CloseWindowStation)(w);
}
/* NtUserSetWindowStationUser: with a non-NULL sid pointer and length 0 neither branch runs, so the call returns
 * 0 without raising an error. */
RT(set_winsta_user_sid_pointer_with_zero_length_is_silent_noop) {
    REQUIRE(UN(NtUserSetWindowStationUser)); REQUIRE(U(CreateWindowStationW));
    HWINSTA w = r2_mk_winsta("swu3", WSTA_ALL_, FALSE, NULL);
    if (!w) { t_skip("CreateWindowStation refused for the unprivileged token (gle=%lu)", GetLastError()); return; }
    ULONGLONG luid = 0x1234; DWORD sidbuf[4] = { 0 };
    SET_ERR(SENT);
    CHECK(R_D(UN(NtUserSetWindowStationUser)(w, &luid, sidbuf, 0)) == 0);
    EXPECT_ERR(SENT);
    U(CloseWindowStation)(w);
}

/* ---- CheckProcessSession / GetDesktopID (DWM only) -------------------------------------------- */
RT(check_process_session_denied_outside_dwm) {
    REQUIRE(UN(NtUserCheckProcessSession));
    static const DWORD args[] = { 0, 1, 2, 0x1234, 0xFFFFFFFF };
    for (unsigned i = 0; i < 5; i++) {
        SET_ERR(SENT);
        ULONG_PTR r = UN(NtUserCheckProcessSession)(args[i]);   /* process != g_pepDwm -> 5, result 0 */
        CKF(R_D(r) == 0, "arg=%lx", args[i]);
        CKF(GetLastError() == ERR_ACCESS_DENIED, "arg=%lx gle=%lu", args[i], GetLastError());
    }
}
RT(check_process_session_current_pid_denied) {
    REQUIRE(UN(NtUserCheckProcessSession));
    SET_ERR(SENT);
    CHECK(R_D(UN(NtUserCheckProcessSession)(GetCurrentProcessId())) == 0);
    EXPECT_ERR(ERR_ACCESS_DENIED);
}
RT(get_desktop_id_denied_outside_dwm) {
    REQUIRE(UN(NtUserGetDesktopID));
    static const DWORD types[] = { 0, 1, 2, 3, 4, 5, 0xFFFFFFFF };
    for (unsigned i = 0; i < 7; i++) {
        ULONGLONG id = 0xCCCCCCCCCCCCCCCCull;
        SET_ERR(SENT);
        UN(NtUserGetDesktopID)(types[i], &id);   /* IsProcessDwm() is checked before the type: UserSetLastError(5) */
        CKF(GetLastError() == ERR_ACCESS_DENIED, "type=%lu gle=%lu", types[i], GetLastError());
        CKF(id == 0xCCCCCCCCCCCCCCCCull, "type=%lu out overwritten 0x%llx", types[i], (unsigned long long)id);
    }
}
RT(get_desktop_id_user32_export_denied_outside_dwm) {
    typedef BOOL (WINAPI *pfn)(DWORD, ULONGLONG *);
    pfn f = (pfn)r2_proc_in("user32.dll", "GetDesktopID");
    REQUIRE(f);
    ULONGLONG id = 0;
    SET_ERR(SENT);
    f(1, &id);
    EXPECT_ERR(ERR_ACCESS_DENIED);
}

/* ---- CheckAccessForIntegrityLevel -------------------------------------------------------------- */
#define STATUS_ACCESS_DENIED_ 0xC0000022u
#define STATUS_INVALID_PARAMETER_ 0xC000000Du

/* NtUserCheckAccessForIntegrityLevel(pidA, pidB, &out): both pids must lock in this session and have a Win32
 * process; equal levels yield STATUS_SUCCESS and out=1 (CheckAccess: equal level and equal uiaccess word). */
RT(check_access_il_self_self_allowed) {
    REQUIRE(UN(NtUserCheckAccessForIntegrityLevel));
    NEED_WIN(h);   /* makes this process a GUI process (PsGetProcessWin32Process != 0) */
    DWORD out = 0xCCCCCCCC;
    LONG st = UN(NtUserCheckAccessForIntegrityLevel)(GetCurrentProcessId(), GetCurrentProcessId(), &out);
    CKF((ULONG)st == 0, "status=0x%lx", (ULONG)st);
    CKF(out == 1, "out=%lu", out);
    END_WIN(h);
}
RT(check_access_il_unknown_first_pid_is_invalid_parameter) {
    REQUIRE(UN(NtUserCheckAccessForIntegrityLevel));
    NEED_WIN(h);
    static const ULONG_PTR pids[] = { 0, 0xFFFFFFF0, 0x7FFFFFFC };
    for (unsigned i = 0; i < 3; i++) {
        DWORD out = 0xCCCCCCCC;
        LONG st = UN(NtUserCheckAccessForIntegrityLevel)(pids[i], GetCurrentProcessId(), &out);
        CKF((ULONG)st == STATUS_INVALID_PARAMETER_, "pid=%llx status=0x%lx", (unsigned long long)pids[i], (ULONG)st);
        CKF(out == 0xCCCCCCCC, "out written on failure");
    }
    END_WIN(h);
}
RT(check_access_il_unknown_second_pid_is_invalid_parameter) {
    REQUIRE(UN(NtUserCheckAccessForIntegrityLevel));
    NEED_WIN(h);
    static const ULONG_PTR pids[] = { 0, 0xFFFFFFF0, 0x7FFFFFFC };
    for (unsigned i = 0; i < 3; i++) {
        DWORD out = 0xCCCCCCCC;
        LONG st = UN(NtUserCheckAccessForIntegrityLevel)(GetCurrentProcessId(), pids[i], &out);
        CKF((ULONG)st == STATUS_INVALID_PARAMETER_, "pid=%llx status=0x%lx", (unsigned long long)pids[i], (ULONG)st);
        CKF(out == 0xCCCCCCCC, "out written on failure");
    }
    END_WIN(h);
}
/* The System process lives in session 0; LockProcessByClientId rejects processes of other sessions. */
RT(check_access_il_system_process_other_session_rejected) {
    REQUIRE(UN(NtUserCheckAccessForIntegrityLevel));
    if (k_session_id() == 0) { t_skip("running in session 0"); return; }
    NEED_WIN(h);
    DWORD out = 0xCCCCCCCC;
    LONG st = UN(NtUserCheckAccessForIntegrityLevel)(4, GetCurrentProcessId(), &out);
    CKF((ULONG)st == STATUS_INVALID_PARAMETER_, "status=0x%lx", (ULONG)st);
    st = UN(NtUserCheckAccessForIntegrityLevel)(GetCurrentProcessId(), 4, &out);
    CKF((ULONG)st == STATUS_INVALID_PARAMETER_, "status=0x%lx", (ULONG)st);
    END_WIN(h);
}
/* The first process must be the caller's own Win32 process (or the caller must be csrss): another GUI process
 * in the session, such as the shell, as pidA gives STATUS_ACCESS_DENIED. */
RT(check_access_il_other_gui_process_as_first_is_denied) {
    REQUIRE(UN(NtUserCheckAccessForIntegrityLevel)); REQUIRE(U(GetShellWindow)); REQUIRE(U(GetWindowThreadProcessId));
    NEED_WIN(h);
    HWND sh = U(GetShellWindow)();
    DWORD spid = 0;
    if (sh) U(GetWindowThreadProcessId)(sh, &spid);
    if (!spid || spid == GetCurrentProcessId()) { t_skip("no separate shell process"); END_WIN(h); return; }
    DWORD out = 0xCCCCCCCC;
    LONG st = UN(NtUserCheckAccessForIntegrityLevel)(spid, GetCurrentProcessId(), &out);
    CKF((ULONG)st == STATUS_ACCESS_DENIED_, "status=0x%lx", (ULONG)st);
    CKF(out == 0xCCCCCCCC, "out written on failure");
    END_WIN(h);
}
RT(check_access_il_self_vs_shell_reports_boolean) {
    REQUIRE(UN(NtUserCheckAccessForIntegrityLevel)); REQUIRE(U(GetShellWindow)); REQUIRE(U(GetWindowThreadProcessId));
    NEED_WIN(h);
    HWND sh = U(GetShellWindow)();
    DWORD spid = 0;
    if (sh) U(GetWindowThreadProcessId)(sh, &spid);
    if (!spid || spid == GetCurrentProcessId()) { t_skip("no separate shell process"); END_WIN(h); return; }
    DWORD out = 0xCCCCCCCC;
    LONG st = UN(NtUserCheckAccessForIntegrityLevel)(GetCurrentProcessId(), spid, &out);
    CKF((ULONG)st == 0, "status=0x%lx", (ULONG)st);
    CKF(out == 0 || out == 1, "out=0x%lx", out);   /* CheckAccess yields a 0/1 byte, stored as a DWORD */
    t_note("integrity access self(%lu) -> shell: %lu", k_integrity_level(), out);
    END_WIN(h);
}
