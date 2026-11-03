/* dx_desktop_switch: win32u stubs called directly for the desktop / window station handle syscalls that
 * user32 reaches through one-argument thunks (analysis of user32!SetThreadDesktop, CloseDesktop, CloseWindowStation,
 * GetProcessWindowStation, SetProcessWindowStation: each is a bare jump to the Nt stub with the caller's arguments):
 *   NtUserGetProcessWindowStation()          NtUserSetProcessWindowStation(hwinsta)
 *   NtUserSetThreadDesktop(hdesk)            NtUserGetThreadDesktop(dwThreadId)
 *   NtUserCloseDesktop(hdesk)                NtUserCloseWindowStation(hwinsta)
 * win32kfull!_GetThreadDesktop returns the very handle value stored in tagTHREADINFO+0x250 when the thread belongs
 * to the calling process. Nothing here moves the input desktop. */
#include "dxd.h"

typedef HANDLE (WINAPI *p_NtUserGetProcessWindowStation)(void);
typedef BOOL   (WINAPI *p_NtUserSetProcessWindowStation)(HANDLE);
typedef BOOL   (WINAPI *p_NtUserCloseWindowStation)(HANDLE);

DXT(direct_getprocesswindowstation_matches_user32) {
    p_NtUserGetProcessWindowStation g = (p_NtUserGetProcessWindowStation)t_proc_in("win32u.dll", "NtUserGetProcessWindowStation");
    REQUIRE(g); REQUIRE(U(GetProcessWindowStation));
    HANDLE a = g(), b = U(GetProcessWindowStation)();
    CHECK(a != NULL);
    CHECK(a == b);
    WCHAR t[32];
    CHECK(dxd_uoi_str(a, DXD_UOI_TYPE, t, 32)); EXPECT_WSTR(t, L"WindowStation");
    CHECK(g() == a);                        /* a stable handle value, not a fresh one per call */
}

DXT(direct_setprocesswindowstation_invalid_handles_fail_and_change_nothing) {
    p_NtUserSetProcessWindowStation s = (p_NtUserSetProcessWindowStation)t_proc_in("win32u.dll", "NtUserSetProcessWindowStation");
    REQUIRE(s); REQUIRE(U(GetProcessWindowStation));
    HANDLE before = U(GetProcessWindowStation)();
    SetLastError(0); CHECK(!s(NULL)); EXPECT_ERR(ERROR_INVALID_HANDLE);
    SetLastError(0); CHECK(!s((HANDLE)(ULONG_PTR)0x12344)); EXPECT_ERR(ERROR_INVALID_HANDLE);
    CHECK(U(GetProcessWindowStation)() == before);
}

DXT(direct_setthreaddesktop_null_and_bogus_fail_6) {
    p_NtUserSetThreadDesktop s = NTU(NtUserSetThreadDesktop);
    REQUIRE(s); REQUIRE(U(GetThreadDesktop));
    HDESK before = U(GetThreadDesktop)(GetCurrentThreadId());
    SetLastError(0); CHECK(!s(NULL)); EXPECT_ERR(ERROR_INVALID_HANDLE);
    SetLastError(0); CHECK(!s((HANDLE)(ULONG_PTR)0x12344)); EXPECT_ERR(ERROR_INVALID_HANDLE);
    CHECK(U(GetThreadDesktop)(GetCurrentThreadId()) == before);
}

static void rt_action(struct dxd_thr *t) {
    p_NtUserSetThreadDesktop set = NTU(NtUserSetThreadDesktop);
    p_NtUserGetThreadDesktop get = NTU(NtUserGetThreadDesktop);
    HANDLE orig = get(t->tid);
    t->r[0] = (LONG_PTR)orig;
    t->r[1] = set((HANDLE)t->user);
    t->r[2] = (LONG_PTR)get(t->tid);
    t->r[3] = (LONG_PTR)get(t->tid);
    t->r[4] = set(orig);
    t->r[5] = (LONG_PTR)get(t->tid);
}
DXT(direct_setthreaddesktop_roundtrip_returns_same_handle_values) {
    REQUIRE(NTU(NtUserSetThreadDesktop)); REQUIRE(NTU(NtUserGetThreadDesktop)); REQUIRE(U(CreateDesktopW));
    HDESK d = dxd_mk_desk("drt", DESKTOP_ALL_RIGHTS, NULL);
    if (!d) { t_skip("CreateDesktop failed"); return; }
    struct dxd_thr t; memset(&t, 0, sizeof t); t.user = d; t.action = rt_action;
    CHECK(dxd_thr_start(&t, "drt"));
    CHECK(t.r[0] != 0);
    CHECK(t.r[1] == 1);
    CHECKF(t.r[2] == (LONG_PTR)d, "thread desktop handle %p want %p", (void *)t.r[2], (void *)d);   /* tagTHREADINFO+0x250 holds the handle passed in */
    CHECK(t.r[3] == t.r[2]);
    CHECK(t.r[4] == 1);
    CHECK(t.r[5] == t.r[0]);
    dxd_thr_stop(&t);
    U(CloseDesktop)(d);
}

DXT(direct_getthreaddesktop_unknown_thread_returns_null) {
    p_NtUserGetThreadDesktop gt = NTU(NtUserGetThreadDesktop);
    REQUIRE(gt);
    CHECK(gt(0) == NULL);
    CHECK(gt(0xFFFFFFF0u) == NULL);        /* PtiFromThreadId and GetConsoleDesktop both fail */
}

DXT(direct_closedesktop_invalid_handles_fail_6) {
    p_NtUserCloseDesktop c = NTU(NtUserCloseDesktop);
    REQUIRE(c);
    SetLastError(0); CHECK(!c(NULL)); EXPECT_ERR(ERROR_INVALID_HANDLE);
    SetLastError(0); CHECK(!c((HANDLE)(ULONG_PTR)0x12344)); EXPECT_ERR(ERROR_INVALID_HANDLE);
}

DXT(direct_closedesktop_succeeds_once) {
    p_NtUserCloseDesktop c = NTU(NtUserCloseDesktop);
    REQUIRE(c); REQUIRE(U(CreateDesktopW));
    HDESK d = dxd_mk_desk("dcl", DESKTOP_ALL_RIGHTS, NULL);
    if (!d) { t_skip("CreateDesktop failed"); return; }
    CHECK(c(d) == TRUE);
    SetLastError(0);
    CHECK(c(d) == FALSE);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
}

DXT(direct_closedesktop_of_threads_current_desktop_busy_0xaa) {
    p_NtUserCloseDesktop c = NTU(NtUserCloseDesktop);
    p_NtUserGetThreadDesktop gt = NTU(NtUserGetThreadDesktop);
    REQUIRE(c); REQUIRE(gt); REQUIRE(U(CreateDesktopW));
    HDESK d = dxd_mk_desk("dbusy", DESKTOP_ALL_RIGHTS, NULL);
    if (!d) { t_skip("CreateDesktop failed"); return; }
    struct dxd_thr t; memset(&t, 0, sizeof t); t.desk = d;
    CHECK(dxd_thr_start(&t, "dbusy") && t.set_ok);
    SetLastError(0);
    CHECK(c(d) == FALSE);
    EXPECT_ERR(ERROR_BUSY);                 /* _CloseDesktop: a W32THREAD of this process is assigned this handle */
    dxd_thr_stop(&t);
    BOOL ok = FALSE;
    for (int i = 0; i < 50 && !ok; i++) { ok = c(d); if (!ok) Sleep(20); }
    CHECK(ok || t_is_wine());
}

DXT(direct_closewindowstation_invalid_and_double_close) {
    p_NtUserCloseWindowStation c = (p_NtUserCloseWindowStation)t_proc_in("win32u.dll", "NtUserCloseWindowStation");
    REQUIRE(c); REQUIRE(U(CreateWindowStationW));
    SetLastError(0); CHECK(!c(NULL)); EXPECT_ERR(ERROR_INVALID_HANDLE);
    WCHAR n[64]; dxd_name(n, "dcw");
    HWINSTA w = U(CreateWindowStationW)(n, 0, DXD_WINSTA_ALL, NULL);
    if (!w) { t_skip("CreateWindowStation failed, err %lu", GetLastError()); return; }
    CHECK(c(w) == TRUE);
    SetLastError(0);
    CHECK(c(w) == FALSE);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
}

DXT(direct_closedesktop_rejects_window_station_handle) {
    p_NtUserCloseDesktop c = NTU(NtUserCloseDesktop);
    REQUIRE(c); REQUIRE(U(CreateWindowStationW)); REQUIRE(U(CloseWindowStation));
    WCHAR n[64]; dxd_name(n, "dwt");
    HWINSTA w = U(CreateWindowStationW)(n, 0, DXD_WINSTA_ALL, NULL);
    if (!w) { t_skip("CreateWindowStation failed"); return; }
    SetLastError(0);
    CHECK(!c((HANDLE)w));                   /* type mismatch against ExDesktopObjectType */
    t_note("NtUserCloseDesktop(window station) LastError %lu", GetLastError());
    WCHAR t[32];
    CHECK(dxd_uoi_str(w, DXD_UOI_TYPE, t, 32));      /* the handle survived */
    CHECK(U(CloseWindowStation)(w));
}
