/* dx_desktop_switch: the input desktop and the validation front of NtUserSwitchDesktop.
 *
 * win32kfull!NtUserSwitchDesktop(hdesk, fade, flags), in order:
 *   1. thread restricted (tagTHREADINFO+0x1e8 bit 0x20000000 and process flag 0x40)  -> LastError 5
 *   2. ValidateHdesk(hdesk, UserMode, access 0x100 = DESKTOP_SWITCHDESKTOP)
 *        ObReferenceObjectByHandle failure -> RtlNtStatusToDosError -> LastError
 *          NULL / closed / bogus handle  STATUS_INVALID_HANDLE   -> 6
 *          missing DESKTOP_SWITCHDESKTOP STATUS_ACCESS_DENIED    -> 5
 *        object belongs to another session or is dying (flags 0xe)  -> STATUS_INVALID_HANDLE -> 6
 *   3. the desktop's window station has WSF_NOIO (flag 4)           -> LastError 5
 *   4. desktop switch: target already the input desktop => success without side effects (the very
 *      first branch), otherwise the switch proper.
 * Return value is BOOL (~status >> 31).
 *
 * EditionOpenInputDesktopEntryPoint(flags, fInherit, access): access|0x81 is the mask for
 * ObOpenObjectByPointer, inherit selects OBJ_INHERIT, process window station NOIO -> LastError 1,
 * no input desktop -> 110.
 */
#include "dxd.h"

#define FULL_SWITCH (DESKTOP_SWITCHDESKTOP | DESKTOP_READOBJECTS)

/* ============================ OpenInputDesktop ================================================== */

DXT(input_open_basic) {
    REQUIRE(U(OpenInputDesktop)); REQUIRE(U(CloseDesktop));
    if (!k_has_interactive_desktop()) { t_skip("no interactive input desktop"); return; }
    HDESK h = U(OpenInputDesktop)(0, FALSE, DESKTOP_READOBJECTS);
    CHECK(h != NULL);
    if (!h) return;
    WCHAR t[32], n[128];
    CHECK(dxd_uoi_str(h, DXD_UOI_TYPE, t, 32)); EXPECT_WSTR(t, L"Desktop");
    CHECK(dxd_uoi_str(h, DXD_UOI_NAME, n, 128)); CHECK(n[0] != 0);
    CHECK(U(CloseDesktop)(h));
}

DXT(input_open_via_win32u_stub_matches_user32) {
    REQUIRE(U(OpenInputDesktop)); REQUIRE(U(CloseDesktop));
    p_NtUserOpenInputDesktop nt = NTU(NtUserOpenInputDesktop);
    REQUIRE(nt);
    if (!k_has_interactive_desktop()) { t_skip("no interactive input desktop"); return; }
    HDESK a = U(OpenInputDesktop)(0, FALSE, DESKTOP_READOBJECTS), b = nt(0, FALSE, DESKTOP_READOBJECTS);
    CHECK(a != NULL); CHECK(b != NULL);
    if (a && b) {
        WCHAR na[128], nb[128];
        CHECK(dxd_uoi_str(a, DXD_UOI_NAME, na, 128)); CHECK(dxd_uoi_str(b, DXD_UOI_NAME, nb, 128));
        EXPECT_WSTR(na, nb);
        CHECK(a != b);                           /* every call returns a fresh handle (ObOpenObjectByPointer) */
        CHECK(dxd_same_object(a, b));
    }
    if (a) U(CloseDesktop)(a);
    if (b) U(CloseDesktop)(b);
}

DXT(input_open_access_or_0x81) {
    REQUIRE(U(OpenInputDesktop)); REQUIRE(U(CloseDesktop));
    if (!k_has_interactive_desktop()) { t_skip("no interactive input desktop"); return; }
    if (t_is_wine()) { t_skip("Wine does not add the 0x81 access bits"); return; }
    static const struct { DWORD req, must; } v[] = {
        { 0, 0x81 }, { DESKTOP_READOBJECTS, 0x81 }, { DESKTOP_CREATEWINDOW, 0x83 },
        { DESKTOP_ENUMERATE, 0xC1 }, { DESKTOP_SWITCHDESKTOP, 0x181 }, { DESKTOP_CREATEMENU | DESKTOP_HOOKCONTROL, 0x8D },
    };
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        HDESK h = U(OpenInputDesktop)(0, FALSE, v[i].req);
        if (!h) { t_note("OpenInputDesktop(access 0x%lx) failed, err %lu", v[i].req, GetLastError()); continue; }
        ULONG g = 0;
        if (dxd_granted(h, &g)) CHECKF((g & v[i].must) == v[i].must, "req 0x%lx granted 0x%lx want bits 0x%lx", v[i].req, g, v[i].must);
        U(CloseDesktop)(h);
    }
}

DXT(input_open_inherit_flag) {
    REQUIRE(U(OpenInputDesktop)); REQUIRE(U(CloseDesktop));
    if (!k_has_interactive_desktop()) { t_skip("no interactive input desktop"); return; }
    HDESK hi = U(OpenInputDesktop)(0, TRUE, DESKTOP_READOBJECTS);
    HDESK hn = U(OpenInputDesktop)(0, FALSE, DESKTOP_READOBJECTS);
    CHECK(hi != NULL); CHECK(hn != NULL);
    DWORD fi = 0, fn = 0;
    /* fInherit != 0 => HandleAttributes OBJ_INHERIT (0x2) in the ObOpenObjectByPointer call */
    if (hi) { CHECK(GetHandleInformation(hi, &fi)); CHECK(fi & HANDLE_FLAG_INHERIT); }
    if (hn) { CHECK(GetHandleInformation(hn, &fn)); CHECK(!(fn & HANDLE_FLAG_INHERIT)); }
    if (hi) U(CloseDesktop)(hi);
    if (hn) U(CloseDesktop)(hn);
}

DXT(input_open_handle_protected_from_close_note) {
    REQUIRE(U(OpenInputDesktop)); REQUIRE(U(CloseDesktop));
    if (!k_has_interactive_desktop()) { t_skip("no interactive input desktop"); return; }
    HDESK h = U(OpenInputDesktop)(0, FALSE, DESKTOP_READOBJECTS);
    CHECK(h != NULL);
    if (!h) return;
    DWORD f = 0;
    /* EditionOpenInputDesktopEntryPoint calls SetHandleFlag(h, 1, 1) after OpenDesktopCompletion. */
    if (GetHandleInformation(h, &f)) t_note("OpenInputDesktop handle flags 0x%lx (protect-from-close %s)", f, (f & HANDLE_FLAG_PROTECT_FROM_CLOSE) ? "set" : "clear");
    CHECK(U(CloseDesktop)(h));     /* _CloseDesktop clears the flag (SetHandleFlag(h,0,0)) before CloseProtectedHandle */
    SetLastError(0);
    WCHAR n[64];
    CHECK(!dxd_uoi_str(h, DXD_UOI_NAME, n, 64));           /* closed: ValidateHdesk fails */
    EXPECT_ERR(ERROR_INVALID_HANDLE);
}

DXT(input_open_each_call_yields_distinct_handles) {
    REQUIRE(U(OpenInputDesktop)); REQUIRE(U(CloseDesktop));
    if (!k_has_interactive_desktop()) { t_skip("no interactive input desktop"); return; }
    HDESK h[8]; int n = 0;
    for (int i = 0; i < 8; i++) { h[i] = U(OpenInputDesktop)(0, FALSE, DESKTOP_READOBJECTS); if (h[i]) n++; }
    CHECK(n == 8);
    for (int i = 0; i < 8; i++) for (int j = i + 1; j < 8; j++) if (h[i] && h[j]) CHECK(h[i] != h[j]);
    for (int i = 0; i < 8; i++) if (h[i]) U(CloseDesktop)(h[i]);
}

DXT(input_open_is_on_process_window_station) {
    REQUIRE(U(OpenInputDesktop)); REQUIRE(U(GetProcessWindowStation));
    if (!k_has_interactive_desktop()) { t_skip("no interactive input desktop"); return; }
    WCHAR w[64]; CHECK(dxd_winsta_name(w, 64));
    HWINSTA ws = U(GetProcessWindowStation)();
    USEROBJECTFLAGS uf; DWORD need = 0;
    memset(&uf, 0, sizeof uf);
    CHECK(U(GetUserObjectInformationW)(ws, DXD_UOI_FLAGS, &uf, sizeof uf, &need));
    /* a process whose window station is interactive (WSF_VISIBLE) can open the input desktop */
    if (!lstrcmpW(w, L"WinSta0")) CHECK(uf.dwFlags & WSF_VISIBLE);
    else t_note("process window station is \"%ls\", flags 0x%lx", w, uf.dwFlags);
}

DXT(input_open_input_desktop_name_matches_thread_desktop_note) {
    REQUIRE(U(OpenInputDesktop)); REQUIRE(U(GetThreadDesktop)); REQUIRE(U(CloseDesktop));
    if (!k_has_interactive_desktop()) { t_skip("no interactive input desktop"); return; }
    WCHAR in[128], th[128];
    CHECK(dxd_input_name(in, 128));
    HDESK d = U(GetThreadDesktop)(GetCurrentThreadId());
    CHECK(d != NULL);
    if (d && dxd_uoi_str(d, DXD_UOI_NAME, th, 128)) {
        if (lstrcmpiW(in, th)) t_note("test thread's desktop \"%ls\" differs from the input desktop \"%ls\"", th, in);
        else CHECK(!lstrcmpiW(in, th));
    }
}

DXT(input_open_closed_input_handle_then_switch_fails_6) {
    DXD_BEGIN();
    HDESK h = U(OpenInputDesktop)(0, FALSE, FULL_SWITCH);
    CHECK(h != NULL);
    if (!h) return;
    CHECK(U(CloseDesktop)(h));
    SetLastError(0);
    CHECK(!U(SwitchDesktop)(h));
    EXPECT_ERR(ERROR_INVALID_HANDLE);     /* ValidateHdesk: STATUS_INVALID_HANDLE -> 6 */    dxd_expect_unchanged();
}

/* ============================ SwitchDesktop validation ========================================== */

DXT(switch_to_current_input_desktop_succeeds) {
    DXD_BEGIN();
    HDESK h = U(OpenInputDesktop)(0, FALSE, FULL_SWITCH);
    CHECK(h != NULL);
    if (!h) return;
    SetLastError(0);
    BOOL rc = U(SwitchDesktop)(h);
    DWORD e = GetLastError();
 /* desktop switch: argument 2 == the input desktop => status 0 before any other check */
    CHECKF(rc, "SwitchDesktop(current input) failed, err %lu", e);
    dxd_expect_unchanged();
    U(CloseDesktop)(h);
}

DXT(switch_to_current_input_desktop_repeatedly) {
    DXD_BEGIN();
    HDESK h = U(OpenInputDesktop)(0, FALSE, FULL_SWITCH);
    CHECK(h != NULL);
    if (!h) return;
    int ok = 0;
    for (int i = 0; i < 50; i++) if (U(SwitchDesktop)(h)) ok++;
    CHECK(ok == 50);
    dxd_expect_unchanged();
    U(CloseDesktop)(h);
}

DXT(switch_to_current_via_win32u_stub) {
    DXD_BEGIN();
    p_NtUserSwitchDesktop sw = NTU(NtUserSwitchDesktop);
    REQUIRE(sw);
    HDESK h = U(OpenInputDesktop)(0, FALSE, FULL_SWITCH);
    CHECK(h != NULL);
    if (!h) return;
    SetLastError(0);
    DWORD rc = sw(h, 0, 0);       /* the exact argument list user32!SwitchDesktop passes */
    CHECK(rc == 1);
    CHECK(rc <= 1);                /* ~status >> 31: strictly 0 or 1 */
    U(CloseDesktop)(h);
}

DXT(switch_via_stub_return_is_strictly_boolean_on_failure) {
    DXD_BEGIN();
    p_NtUserSwitchDesktop sw = NTU(NtUserSwitchDesktop);
    REQUIRE(sw);
    SetLastError(0);
    DWORD rc = sw(NULL, 0, 0);
    CHECK(rc == 0);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
}

DXT(switch_null_handle_fails_6) {
    DXD_BEGIN();
    SetLastError(0);
    CHECK(!U(SwitchDesktop)(NULL));
    EXPECT_ERR(ERROR_INVALID_HANDLE);    dxd_expect_unchanged();
}

DXT(switch_bogus_handle_values_fail_6) {
    DXD_BEGIN();
    static const ULONG_PTR v[] = { 4, 0x7FFC, 0xFFFC, 0x12344, (ULONG_PTR)-1 /* current-process pseudo handle */, (ULONG_PTR)-2 };
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        SetLastError(0);
        BOOL rc = U(SwitchDesktop)((HDESK)v[i]);
        DWORD e = GetLastError();
        CHECKF(!rc, "handle %p switched", (void *)v[i]);
        if (!rc) CHECKF(e == ERROR_INVALID_HANDLE || e == ERROR_ACCESS_DENIED, "handle %p err %lu", (void *)v[i], e);
    }
    dxd_expect_unchanged();
}

DXT(switch_closed_desktop_handle_fails_6) {
    DXD_BEGIN();
    HDESK d = dxd_mk_desk("swclosed", DESKTOP_ALL_RIGHTS, NULL);
    if (!d) { t_skip("CreateDesktop failed (%lu)", GetLastError()); return; }
    CHECK(U(CloseDesktop)(d));
    SetLastError(0);
    CHECK(!U(SwitchDesktop)(d));
    EXPECT_ERR(ERROR_INVALID_HANDLE);    dxd_expect_unchanged();
}

DXT(switch_window_station_handle_is_wrong_type) {
    DXD_BEGIN();
    HWINSTA ws = U(GetProcessWindowStation)();
    CHECK(ws != NULL);
    SetLastError(0);
    /* ObReferenceObjectByHandle(handle, 0x100, ExDesktopObjectType): a WindowStation object is rejected */
    CHECK(!U(SwitchDesktop)((HDESK)ws));
    t_note("SwitchDesktop(window station handle) LastError %lu", GetLastError());
    dxd_expect_unchanged();
}

DXT(switch_event_handle_is_wrong_type) {
    DXD_BEGIN();
    HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    CHECK(ev != NULL);
    SetLastError(0);
    CHECK(!U(SwitchDesktop)((HDESK)ev));
    t_note("SwitchDesktop(event handle) LastError %lu", GetLastError());
    CloseHandle(ev);    dxd_expect_unchanged();
}

DXT(switch_without_switchdesktop_access_fails_5) {
    DXD_BEGIN();
    /* ValidateHdesk asks for 0x100; every mask lacking it must come back STATUS_ACCESS_DENIED -> 5. */
    static const DWORD masks[] = {
        DESKTOP_READOBJECTS, DESKTOP_CREATEWINDOW, DESKTOP_CREATEMENU, DESKTOP_HOOKCONTROL, DESKTOP_ENUMERATE,
        DESKTOP_WRITEOBJECTS, DESKTOP_JOURNALRECORD, DESKTOP_JOURNALPLAYBACK, 0xFF,
    };
    for (size_t i = 0; i < sizeof masks / sizeof masks[0]; i++) {
        char tag[16]; snprintf(tag, sizeof tag, "sa%zu", i);
        HDESK d = dxd_mk_desk(tag, masks[i], NULL);
        if (!d) { t_note("CreateDesktop(access 0x%lx) failed, err %lu", masks[i], GetLastError()); continue; }
        SetLastError(0);
        BOOL rc = U(SwitchDesktop)(d);
        DWORD e = GetLastError();
        CHECKF(!rc, "access 0x%lx switched", masks[i]);
        if (!rc) CHECKF(e == ERROR_ACCESS_DENIED, "access 0x%lx err %lu", masks[i], e);
        U(CloseDesktop)(d);
    }
    dxd_expect_unchanged();
}

DXT(switch_handle_reopened_with_switch_access_passes_validation) {
    DXD_BEGIN();
    /* A desktop created with READOBJECTS only is denied; re-opened by name with SWITCHDESKTOP the
     * validation (steps 1-3) passes. The final result then depends on step 4 (see switch.c), so only
     * the error code is asserted: it must no longer be ACCESS_DENIED from ValidateHdesk when it succeeds. */
    WCHAR n[64];
    HDESK d = dxd_mk_desk("sareopen", DESKTOP_READOBJECTS, n);
    if (!d) { t_skip("CreateDesktop failed"); return; }
    SetLastError(0); CHECK(!U(SwitchDesktop)(d)); EXPECT_ERR(ERROR_ACCESS_DENIED);
    HDESK d2 = U(OpenDesktopW)(n, 0, FALSE, DESKTOP_SWITCHDESKTOP);
    CHECK(d2 != NULL);
    if (d2) {
        ULONG g = 0;
        if (dxd_granted(d2, &g)) CHECKF(g & DESKTOP_SWITCHDESKTOP, "granted 0x%lx", g);
        struct dxd_sw s; dxd_do_switch(d2, &s, 150);
        if (s.rc) CHECK(!lstrcmpiW(s.in_name, n));
        else t_note("switch to a fresh desktop refused, LastError %lu", s.err);
        dxd_restore();
        U(CloseDesktop)(d2);
    }
    U(CloseDesktop)(d);
}

DXT(switch_fade_flag_noop_for_current_desktop) {
    DXD_BEGIN();
    p_NtUserSwitchDesktop sw = NTU(NtUserSwitchDesktop);
    REQUIRE(sw);
    HDESK h = U(OpenInputDesktop)(0, FALSE, FULL_SWITCH);
    CHECK(h != NULL);
    if (!h) return;
 /* argument 2 != 0 selects the fade variant of the switch; its first branch for a caller that is not winlogon is
     * implementation detail, so only the invariant "the input desktop is unchanged" is asserted. */
    SetLastError(0);
    DWORD rc = sw(h, 1, 0);
    t_note("NtUserSwitchDesktop(current, fade=1) -> %lu, LastError %lu", rc, GetLastError());
    CHECK(rc <= 1);
    dxd_expect_unchanged();
    U(CloseDesktop)(h);
}

/* ============================ window station / non-interactive facts ============================ */

DXT(winsta0_userobjectflags_layout) {
    REQUIRE(U(GetProcessWindowStation));
    HWINSTA ws = U(GetProcessWindowStation)();
    if (!ws) { t_skip("no window station"); return; }
    USEROBJECTFLAGS f; DWORD need = 0;
    memset(&f, 0xCC, sizeof f);
    CHECK(U(GetUserObjectInformationW)(ws, DXD_UOI_FLAGS, &f, sizeof f, &need));
    CHECK(need == sizeof f);                 /* 12 bytes */
    if (f.fReserved) t_note("USEROBJECTFLAGS.fReserved = %d", f.fReserved);
    WCHAR w[64]; dxd_winsta_name(w, 64);
    if (!lstrcmpW(w, L"WinSta0")) CHECK(f.dwFlags & WSF_VISIBLE);
}
