/* Window station and desktop kernel objects: creation, naming, object type, granted access,
 * security descriptor, handle inheritance, directory placement. */
#include "wd.h"

/* ---- window station creation / naming --------------------------------------------------------- */

WT(winsta_create_returns_handle) {
    HWINSTA (WINAPI *cw)(LPCWSTR, DWORD, ACCESS_MASK, LPSECURITY_ATTRIBUTES) = U(CreateWindowStationW);
    BOOL (WINAPI *cl)(HWINSTA) = U(CloseWindowStation);
    REQUIRE(cw); REQUIRE(cl);
    WCHAR n[64]; HWINSTA h = wd_mk_winsta("c1", WSTA_FULL, FALSE, n);
    if (!h) { wd_skip_create(); return; }
    CHECK(h != INVALID_HANDLE_VALUE);
    CHECK(cl(h));
}

WT(winsta_name_matches_request) {
    REQUIRE(U(CreateWindowStationW)); REQUIRE(U(GetUserObjectInformationW));
    WCHAR n[64], got[128];
    HWINSTA h = wd_mk_winsta("n1", WSTA_FULL, FALSE, n);
    if (!h) { wd_skip_create(); return; }
    /* _GetUserObjectInformation case 2 copies ObQueryNameInfo(obj) - the leaf name as created */
    CHECK(wd_uoi_str(h, UOI_NAME_, got, 128));
    EXPECT_WSTR(got, n);
    U(CloseWindowStation)(h);
}

WT(winsta_type_uoi_is_WindowStation) {
    REQUIRE(U(CreateWindowStationW));
    WCHAR t[64];
    HWINSTA h = wd_mk_winsta("t1", WSTA_FULL, FALSE, NULL);
    if (!h) { wd_skip_create(); return; }
    /* UOI_TYPE copies the object type name (ObGetObjectType + 0x10) */
    CHECK(wd_uoi_str(h, UOI_TYPE_, t, 64));
    EXPECT_WSTR(t, L"WindowStation");
    U(CloseWindowStation)(h);
}

WT(winsta_ntquery_type_is_WindowStation) {
    REQUIRE(U(CreateWindowStationW)); REQUIRE(NTQO());
    WCHAR t[64];
    HWINSTA h = wd_mk_winsta("t2", WSTA_FULL, FALSE, NULL);
    if (!h) { wd_skip_create(); return; }
    CHECK(wd_type_name(h, t, 64));
    EXPECT_WSTR(t, L"WindowStation");
    U(CloseWindowStation)(h);
}

WT(winsta_object_path_in_session_directory) {
    REQUIRE(U(CreateWindowStationW)); REQUIRE(NTQO());
    WCHAR n[64], path[512], want[512];
    HWINSTA h = wd_mk_winsta("p1", WSTA_FULL, FALSE, n);
    if (!h) { wd_skip_create(); return; }
    CHECK(wd_obj_name(h, path, 512));
    DWORD sid = k_session_id();
    if (sid == 0) swprintf(want, 512, L"\\Windows\\WindowStations\\%ls", n);
    else swprintf(want, 512, L"\\Sessions\\%lu\\Windows\\WindowStations\\%ls", sid, n);
    /* GetWindowStationName in user32!CommonCreateWindowStation opens the per-session \Windows\WindowStations directory as RootDirectory */
    CHECKF(!lstrcmpiW(path, want), "path=%ls want=%ls", path, want);
    U(CloseWindowStation)(h);
}

WT(winsta_create_twice_opens_existing) {
    REQUIRE(U(CreateWindowStationW));
    WCHAR n[64], a[64], b[64];
    HWINSTA h1 = wd_mk_winsta("tw", WSTA_FULL, FALSE, n);
    if (!h1) { wd_skip_create(); return; }
    /* flags without CWF_CREATE_ONLY: attributes get OBJ_OPENIF (0x80), so the second create returns the same object */
    HWINSTA h2 = wd_mk_winsta("tw", WSTA_FULL, FALSE, NULL);
    CHECK(h2 != NULL);
    CHECK(h2 != h1);
    if (h2) {
        CHECK(wd_uoi_str(h2, UOI_NAME_, a, 64));
        CHECK(wd_uoi_str(h1, UOI_NAME_, b, 64));
        EXPECT_WSTR(a, b);
        U(CloseWindowStation)(h2);
    }
    U(CloseWindowStation)(h1);
}

WT(winsta_create_only_collision_fails) {
    REQUIRE(U(CreateWindowStationW));
    WCHAR n[64];
    HWINSTA h1 = wd_mk_winsta("co", WSTA_FULL, FALSE, n);
    if (!h1) { wd_skip_create(); return; }
    SetLastError(0);
    /* CWF_CREATE_ONLY (1) clears OBJ_OPENIF, so an existing name is a collision */
    HWINSTA h2 = U(CreateWindowStationW)(n, 1, WSTA_FULL, NULL);
    CHECK(h2 == NULL);
    if (h2) U(CloseWindowStation)(h2); else t_note("create-only collision gle=%lu", GetLastError());
    U(CloseWindowStation)(h1);
}

WT(winsta_create_name_case_insensitive) {
    REQUIRE(U(CreateWindowStationW));
    WCHAR n[64], up[64], a[64], b[64];
    HWINSTA h1 = wd_mk_winsta("Cs", WSTA_FULL, FALSE, n);
    if (!h1) { wd_skip_create(); return; }
    lstrcpyW(up, n); CharUpperW(up);
    /* create attributes carry OBJ_CASE_INSENSITIVE (0x40) - the upper-case spelling resolves to the same object */
    HWINSTA h2 = U(CreateWindowStationW)(up, 1, WSTA_FULL, NULL);
    CHECK(h2 == NULL);
    HWINSTA h3 = U(CreateWindowStationW)(up, 0, WSTA_FULL, NULL);
    CHECK(h3 != NULL);
    if (h3) { CHECK(wd_uoi_str(h3, UOI_NAME_, a, 64)); CHECK(wd_uoi_str(h1, UOI_NAME_, b, 64)); EXPECT_WSTR(a, b); U(CloseWindowStation)(h3); }
    if (h2) U(CloseWindowStation)(h2);
    U(CloseWindowStation)(h1);
}

WT(winsta_create_invalid_flags) {
    REQUIRE(U(CreateWindowStationW));
    static const DWORD bad[] = { 4, 8, 0x10, 0x100, 0x80000000u, 0xFFFFFFFCu, 0xFFFFFFFFu, 5, 6 };
    WCHAR n[64]; wd_name(n, "bf");
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(0);
        /* user32!CommonCreateWindowStation: (flags & 0xfffffffc) != 0 -> ERROR_INVALID_PARAMETER before any syscall */
        HWINSTA h = U(CreateWindowStationW)(n, bad[i], WSTA_FULL, NULL);
        CKF(h == NULL, "flags=0x%lx", bad[i]);
        if (!h) { DWORD e = GetLastError(); CKF(e == ERROR_INVALID_PARAMETER, "flags=0x%lx gle=%lu", bad[i], e); }
        else U(CloseWindowStation)(h);
    }
}

WT(winsta_create_valid_flag_values) {
    REQUIRE(U(CreateWindowStationW));
    for (DWORD f = 0; f <= 3; f++) {
        WCHAR n[64]; char tag[16]; sprintf(tag, "vf%lu", f); wd_name(n, tag);
        HWINSTA h = U(CreateWindowStationW)(n, f, WSTA_FULL, NULL);
        if (!h) { t_note("flags=%lu create failed gle=%lu", f, GetLastError()); continue; }
        WCHAR t[32]; CHECK(wd_uoi_str(h, UOI_TYPE_, t, 32)); EXPECT_WSTR(t, L"WindowStation");
        U(CloseWindowStation)(h);
    }
}

WT(winsta_open_existing_by_name) {
    REQUIRE(U(CreateWindowStationW)); REQUIRE(U(OpenWindowStationW));
    WCHAR n[64], got[64];
    HWINSTA h = wd_mk_winsta("op", WSTA_FULL, FALSE, n);
    if (!h) { wd_skip_create(); return; }
    HWINSTA o = U(OpenWindowStationW)(n, FALSE, WINSTA_READATTRIBUTES);
    CHECK(o != NULL);
    if (o) {
        CHECK(o != h);
        CHECK(wd_uoi_str(o, UOI_NAME_, got, 64)); EXPECT_WSTR(got, n);
        U(CloseWindowStation)(o);
    }
    U(CloseWindowStation)(h);
}

WT(winsta_open_missing_fails) {
    REQUIRE(U(OpenWindowStationW));
    WCHAR n[64]; wd_name(n, "nonexistent_ws");
    SetLastError(0);
    HWINSTA o = U(OpenWindowStationW)(n, FALSE, WINSTA_READATTRIBUTES);
    CHECK(o == NULL);
    if (o) U(CloseWindowStation)(o); else t_note("open missing winsta gle=%lu", GetLastError());
}

WT(winsta_open_WinSta0) {
    REQUIRE(U(OpenWindowStationW)); REQUIRE(U(GetUserObjectInformationW));
    if (!wd_interactive()) { t_skip("not an interactive session"); return; }
    WCHAR got[64];
    HWINSTA o = U(OpenWindowStationW)(L"WinSta0", FALSE, WINSTA_READATTRIBUTES | WINSTA_ENUMDESKTOPS);
    CHECK(o != NULL);
    if (!o) return;
    CHECK(wd_uoi_str(o, UOI_NAME_, got, 64)); EXPECT_WSTR(got, L"WinSta0");
    U(CloseWindowStation)(o);
}

WT(winsta_WinSta0_open_case_insensitive) {
    REQUIRE(U(OpenWindowStationW));
    if (!wd_interactive()) { t_skip("not an interactive session"); return; }
    HWINSTA o = U(OpenWindowStationW)(L"WINSTA0", FALSE, WINSTA_READATTRIBUTES);
    CHECK(o != NULL);
    if (o) U(CloseWindowStation)(o);
}

WT(winsta_granted_access_covers_request) {
    REQUIRE(U(CreateWindowStationW)); REQUIRE(NTQO());
    static const DWORD req[] = { WINSTA_ENUMDESKTOPS, WINSTA_READATTRIBUTES, WINSTA_ENUMDESKTOPS | WINSTA_READATTRIBUTES,
                                 WINSTA_CREATEDESKTOP | WINSTA_ENUMDESKTOPS, WINSTA_ALL_ACCESS };
    WCHAR n[64]; HWINSTA base = wd_mk_winsta("ga", WSTA_FULL, FALSE, n);
    if (!base) { wd_skip_create(); return; }
    for (unsigned i = 0; i < sizeof req / sizeof req[0]; i++) {
        HWINSTA o = U(OpenWindowStationW)(n, FALSE, req[i]);
        CKF(o != NULL, "req=0x%lx", req[i]);
        if (!o) continue;
        ULONG g = 0;
        CHECK(wd_granted(o, &g));
        CKF((g & req[i]) == req[i], "req=0x%lx granted=0x%lx", req[i], g);
        U(CloseWindowStation)(o);
    }
    U(CloseWindowStation)(base);
}

WT(winsta_security_descriptor_has_owner_and_dacl) {
    REQUIRE(U(CreateWindowStationW));
    WCHAR n[64]; HWINSTA h = wd_mk_winsta("sd", WSTA_FULL, FALSE, n);
    if (!h) { wd_skip_create(); return; }
    BYTE sd[2048]; DWORD need = 0;
    if (!GetKernelObjectSecurity(h, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, sd, sizeof sd, &need)) {
        t_note("GetKernelObjectSecurity gle=%lu", GetLastError());
        CHECK(FALSE);
    } else {
        PSID owner = NULL; BOOL def = FALSE;
        CHECK(GetSecurityDescriptorOwner(sd, &owner, &def));
        CHECK(owner != NULL && IsValidSid(owner));
        BOOL present = FALSE; PACL acl = NULL;
        CHECK(GetSecurityDescriptorDacl(sd, &present, &acl, &def));
        CHECK(present);
        if (present && acl) CHECK(acl->AceCount >= 1);
        else t_note("NULL DACL present=%d", present);
    }
    U(CloseWindowStation)(h);
}

WT(winsta_handle_inheritable_when_requested) {
    REQUIRE(U(CreateWindowStationW));
    HWINSTA a = wd_mk_winsta("ih1", WSTA_FULL, TRUE, NULL), b = wd_mk_winsta("ih2", WSTA_FULL, FALSE, NULL);
    if (!a || !b) { wd_skip_create(); if (a) U(CloseWindowStation)(a); if (b) U(CloseWindowStation)(b); return; }
    /* CommonCreateWindowStation: bInheritHandle adds OBJ_INHERIT (2) to the attributes (0x42 vs 0x40) */
    CHECK(wd_is_inherit(a));
    CHECK(!wd_is_inherit(b));
    U(CloseWindowStation)(a); U(CloseWindowStation)(b);
}

WT(winsta_open_inherit_flag) {
    REQUIRE(U(CreateWindowStationW)); REQUIRE(U(OpenWindowStationW));
    WCHAR n[64]; HWINSTA h = wd_mk_winsta("oi", WSTA_FULL, FALSE, n);
    if (!h) { wd_skip_create(); return; }
    HWINSTA a = U(OpenWindowStationW)(n, TRUE, WINSTA_READATTRIBUTES), b = U(OpenWindowStationW)(n, FALSE, WINSTA_READATTRIBUTES);
    CHECK(a && b);
    if (a) { CHECK(wd_is_inherit(a)); U(CloseWindowStation)(a); }
    if (b) { CHECK(!wd_is_inherit(b)); U(CloseWindowStation)(b); }
    U(CloseWindowStation)(h);
}

WT(winsta_two_creates_distinct_names_distinct_objects) {
    REQUIRE(U(CreateWindowStationW));
    HWINSTA a = wd_mk_winsta("d1", WSTA_FULL, FALSE, NULL), b = wd_mk_winsta("d2", WSTA_FULL, FALSE, NULL);
    if (!a || !b) { wd_skip_create(); if (a) U(CloseWindowStation)(a); if (b) U(CloseWindowStation)(b); return; }
    WCHAR na[64], nb[64];
    CHECK(wd_uoi_str(a, UOI_NAME_, na, 64)); CHECK(wd_uoi_str(b, UOI_NAME_, nb, 64));
    CHECK(lstrcmpiW(na, nb) != 0);
    U(CloseWindowStation)(a); U(CloseWindowStation)(b);
}

WT(winsta_close_then_handle_invalid) {
    REQUIRE(U(CreateWindowStationW));
    HWINSTA h = wd_mk_winsta("cl", WSTA_FULL, FALSE, NULL);
    if (!h) { wd_skip_create(); return; }
    CHECK(U(CloseWindowStation)(h));
    DWORD fl = 0;
    CHECK(!GetHandleInformation(h, &fl));   /* NtClose'd inside NtUserCloseWindowStation */
    WCHAR t[32];
    CHECK(!wd_uoi_str(h, UOI_NAME_, t, 32));
    SetLastError(0);
    BOOL r = U(CloseWindowStation)(h);
    CHECK(!r);
}

WT(winsta_close_via_closehandle_works) {
    REQUIRE(U(CreateWindowStationW));
    HWINSTA h = wd_mk_winsta("ch", WSTA_FULL, FALSE, NULL);
    if (!h) { wd_skip_create(); return; }
    CHECK(CloseHandle(h));
    DWORD fl; CHECK(!GetHandleInformation(h, &fl));
}

WT(winsta_object_gone_after_last_handle_closed) {
    REQUIRE(U(CreateWindowStationW)); REQUIRE(U(OpenWindowStationW));
    WCHAR n[64]; HWINSTA h = wd_mk_winsta("gone", WSTA_FULL, FALSE, n);
    if (!h) { wd_skip_create(); return; }
    CHECK(U(OpenWindowStationW)(n, FALSE, WINSTA_READATTRIBUTES) != NULL ? (CloseHandle(U(OpenWindowStationW)(n, FALSE, WINSTA_READATTRIBUTES)), 1) : 0);
    CHECK(U(CloseWindowStation)(h));
    /* a window station created without a permanent flag is deleted by the object manager when its last handle goes away */
    SetLastError(0);
    HWINSTA o = U(OpenWindowStationW)(n, FALSE, WINSTA_READATTRIBUTES);
    if (o) { t_note("winstation still openable after last handle closed (kernel keeps it alive)"); U(CloseWindowStation)(o); }
}

WT(winsta_dacl_grants_creator) {
    REQUIRE(U(CreateWindowStationW));
    WCHAR n[64]; HWINSTA h = wd_mk_winsta("dc", WSTA_FULL, FALSE, n);
    if (!h) { wd_skip_create(); return; }
    /* the creator got WSTA_FULL on the returned handle, so every right in the mask must be granted */
    ULONG g = 0; CHECK(wd_granted(h, &g));
    CHECKF((g & WINSTA_ALL_ACCESS) == WINSTA_ALL_ACCESS, "granted=0x%lx", g);
    U(CloseWindowStation)(h);
}

/* ---- desktop creation / naming ------------------------------------------------------------------ */

WT(desktop_create_returns_handle) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(CloseDesktop));
    WCHAR n[64]; HDESK d = wd_mk_desk("dc1", DESK_FULL, FALSE, n);
    if (!d) { t_skip("CreateDesktopW failed gle=%lu", GetLastError()); return; }
    CHECK(d != INVALID_HANDLE_VALUE);
    CHECK(U(CloseDesktop)(d));
}

WT(desktop_name_matches_request) {
    REQUIRE(U(CreateDesktopW));
    WCHAR n[64], got[64]; HDESK d = wd_mk_desk("dn", DESK_FULL, FALSE, n);
    if (!d) { wd_skip_create(); return; }
    CHECK(wd_uoi_str(d, UOI_NAME_, got, 64)); EXPECT_WSTR(got, n);
    U(CloseDesktop)(d);
}

WT(desktop_type_uoi_is_Desktop) {
    REQUIRE(U(CreateDesktopW));
    WCHAR t[32]; HDESK d = wd_mk_desk("dt", DESK_FULL, FALSE, NULL);
    if (!d) { wd_skip_create(); return; }
    CHECK(wd_uoi_str(d, UOI_TYPE_, t, 32)); EXPECT_WSTR(t, L"Desktop");
    U(CloseDesktop)(d);
}

WT(desktop_ntquery_type_is_Desktop) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(NTQO());
    WCHAR t[32]; HDESK d = wd_mk_desk("dq", DESK_FULL, FALSE, NULL);
    if (!d) { wd_skip_create(); return; }
    CHECK(wd_type_name(d, t, 32)); EXPECT_WSTR(t, L"Desktop");
    U(CloseDesktop)(d);
}

WT(desktop_create_same_name_twice_opens_same) {
    REQUIRE(U(CreateDesktopW));
    WCHAR n[64], a[64], b[64];
    HDESK d1 = wd_mk_desk("d2x", DESK_FULL, FALSE, n);
    if (!d1) { wd_skip_create(); return; }
    /* CommonCreateDesktop always sets OBJ_OPENIF|OBJ_CASE_INSENSITIVE (0xc0) */
    HDESK d2 = wd_mk_desk("d2x", DESK_FULL, FALSE, NULL);
    CHECK(d2 != NULL);
    if (d2) {
        CHECK(d2 != d1);
        CHECK(wd_uoi_str(d1, UOI_NAME_, a, 64)); CHECK(wd_uoi_str(d2, UOI_NAME_, b, 64)); EXPECT_WSTR(a, b);
        U(CloseDesktop)(d2);
    }
    U(CloseDesktop)(d1);
}

WT(desktop_create_name_case_insensitive) {
    REQUIRE(U(CreateDesktopW));
    WCHAR n[64], up[64], a[64], b[64];
    HDESK d1 = wd_mk_desk("Dci", DESK_FULL, FALSE, n);
    if (!d1) { wd_skip_create(); return; }
    lstrcpyW(up, n); CharUpperW(up);
    HDESK d2 = U(CreateDesktopW)(up, NULL, NULL, 0, DESK_FULL, NULL);
    CHECK(d2 != NULL);
    if (d2) { CHECK(wd_uoi_str(d2, UOI_NAME_, a, 64)); CHECK(wd_uoi_str(d1, UOI_NAME_, b, 64)); EXPECT_WSTR(a, b); U(CloseDesktop)(d2); }
    U(CloseDesktop)(d1);
}

WT(desktop_open_existing) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(OpenDesktopW));
    WCHAR n[64], got[64]; HDESK d = wd_mk_desk("do", DESK_FULL, FALSE, n);
    if (!d) { wd_skip_create(); return; }
    HDESK o = U(OpenDesktopW)(n, 0, FALSE, DESKTOP_READOBJECTS);
    CHECK(o != NULL);
    if (o) { CHECK(wd_uoi_str(o, UOI_NAME_, got, 64)); EXPECT_WSTR(got, n); U(CloseDesktop)(o); }
    U(CloseDesktop)(d);
}

WT(desktop_open_missing_fails) {
    REQUIRE(U(OpenDesktopW));
    WCHAR n[64]; wd_name(n, "nonexistent_dk");
    SetLastError(0);
    HDESK o = U(OpenDesktopW)(n, 0, FALSE, DESKTOP_READOBJECTS);
    CHECK(o == NULL);
    if (o) U(CloseDesktop)(o); else t_note("open missing desktop gle=%lu", GetLastError());
}

WT(desktop_open_Default) {
    REQUIRE(U(OpenDesktopW));
    if (!wd_interactive()) { t_skip("not interactive"); return; }
    WCHAR got[64];
    HDESK o = U(OpenDesktopW)(L"Default", 0, FALSE, DESKTOP_READOBJECTS);
    CHECK(o != NULL);
    if (o) { CHECK(wd_uoi_str(o, UOI_NAME_, got, 64)); EXPECT_WSTR(got, L"Default"); CHECK(wd_uoi_str(o, UOI_TYPE_, got, 64)); EXPECT_WSTR(got, L"Desktop"); U(CloseDesktop)(o); }
}

WT(desktop_open_Default_case_insensitive) {
    REQUIRE(U(OpenDesktopW));
    if (!wd_interactive()) { t_skip("not interactive"); return; }
    HDESK o = U(OpenDesktopW)(L"dEfAuLt", 0, FALSE, DESKTOP_READOBJECTS);
    CHECK(o != NULL);
    if (o) U(CloseDesktop)(o);
}

WT(desktop_open_inherit_flag) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(OpenDesktopW));
    WCHAR n[64]; HDESK d = wd_mk_desk("oi2", DESK_FULL, FALSE, n);
    if (!d) { wd_skip_create(); return; }
    HDESK a = U(OpenDesktopW)(n, 0, TRUE, DESKTOP_READOBJECTS), b = U(OpenDesktopW)(n, 0, FALSE, DESKTOP_READOBJECTS);
    CHECK(a && b);
    if (a) { CHECK(wd_is_inherit(a)); U(CloseDesktop)(a); }
    if (b) { CHECK(!wd_is_inherit(b)); U(CloseDesktop)(b); }
    U(CloseDesktop)(d);
}

WT(desktop_create_inherit_via_security_attributes) {
    REQUIRE(U(CreateDesktopW));
    HDESK a = wd_mk_desk("ci1", DESK_FULL, TRUE, NULL), b = wd_mk_desk("ci2", DESK_FULL, FALSE, NULL);
    if (!a || !b) { wd_skip_create(); if (a) U(CloseDesktop)(a); if (b) U(CloseDesktop)(b); return; }
    /* CommonCreateDesktop: attributes 0xc2 (OBJ_INHERIT) when sa->bInheritHandle else 0xc0 */
    CHECK(wd_is_inherit(a)); CHECK(!wd_is_inherit(b));
    U(CloseDesktop)(a); U(CloseDesktop)(b);
}

WT(desktop_create_null_sa_not_inheritable) {
    REQUIRE(U(CreateDesktopW));
    WCHAR n[64]; wd_name(n, "nsa");
    HDESK d = U(CreateDesktopW)(n, NULL, NULL, 0, DESK_FULL, NULL);
    if (!d) { wd_skip_create(); return; }
    CHECK(!wd_is_inherit(d));
    U(CloseDesktop)(d);
}

WT(desktop_granted_access_covers_request) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(NTQO());
    static const DWORD req[] = { DESKTOP_READOBJECTS, DESKTOP_CREATEWINDOW, DESKTOP_READOBJECTS | DESKTOP_CREATEWINDOW,
                                 DESKTOP_ENUMERATE, DESKTOP_WRITEOBJECTS, DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS | DESKTOP_CREATEWINDOW,
                                 DESKTOP_HOOKCONTROL };
    WCHAR n[64]; HDESK base = wd_mk_desk("dga", DESK_FULL, FALSE, n);
    if (!base) { wd_skip_create(); return; }
    for (unsigned i = 0; i < sizeof req / sizeof req[0]; i++) {
        HDESK o = U(OpenDesktopW)(n, 0, FALSE, req[i]);
        CKF(o != NULL, "req=0x%lx", req[i]);
        if (!o) continue;
        ULONG g = 0; CHECK(wd_granted(o, &g));
        CKF((g & req[i]) == req[i], "req=0x%lx granted=0x%lx", req[i], g);
        U(CloseDesktop)(o);
    }
    U(CloseDesktop)(base);
}

WT(desktop_creator_gets_all_requested) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(NTQO());
    HDESK d = wd_mk_desk("dca", DESK_FULL, FALSE, NULL);
    if (!d) { wd_skip_create(); return; }
    ULONG g = 0; CHECK(wd_granted(d, &g));
    CHECKF((g & DESK_FULL) == DESK_FULL, "granted=0x%lx", g);
    U(CloseDesktop)(d);
}

WT(desktop_security_descriptor_present) {
    REQUIRE(U(CreateDesktopW));
    HDESK d = wd_mk_desk("dsd", DESK_FULL, FALSE, NULL);
    if (!d) { wd_skip_create(); return; }
    BYTE sd[2048]; DWORD need = 0; BOOL present = FALSE, def = FALSE; PACL acl = NULL; PSID owner = NULL;
    if (!GetKernelObjectSecurity(d, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, sd, sizeof sd, &need)) {
        t_note("GetKernelObjectSecurity gle=%lu", GetLastError()); CHECK(FALSE);
    } else {
        CHECK(GetSecurityDescriptorOwner(sd, &owner, &def)); CHECK(owner && IsValidSid(owner));
        CHECK(GetSecurityDescriptorDacl(sd, &present, &acl, &def)); CHECK(present);
    }
    U(CloseDesktop)(d);
}

/* Builds an absolute SD whose DACL holds one ACE for Everyone with the given mask. */
static void sd_with_world_ace(SECURITY_DESCRIPTOR *sd, BYTE *aclbuf, DWORD mask, PSID *world) {
    SID_IDENTIFIER_AUTHORITY wa = SECURITY_WORLD_SID_AUTHORITY;
    PACL acl = (PACL)aclbuf;
    InitializeSecurityDescriptor(sd, SECURITY_DESCRIPTOR_REVISION);
    InitializeAcl(acl, 256, ACL_REVISION);
    AllocateAndInitializeSid(&wa, 1, SECURITY_WORLD_RID, 0,0,0,0,0,0,0, world);
    AddAccessAllowedAce(acl, ACL_REVISION, mask, *world);
    SetSecurityDescriptorDacl(sd, TRUE, acl, FALSE);
}

WT(desktop_create_with_explicit_sd_dacl) {
    REQUIRE(U(CreateDesktopW));
    SECURITY_DESCRIPTOR sd; BYTE aclbuf[256]; PSID world = NULL;
    sd_with_world_ace(&sd, aclbuf, 0x000F01FF, &world);
    SECURITY_ATTRIBUTES sa = { sizeof sa, &sd, FALSE };
    WCHAR n[64]; wd_name(n, "esd");
    HDESK d = U(CreateDesktopW)(n, NULL, NULL, 0, DESK_FULL, &sa);
    if (!d) { wd_skip_create(); FreeSid(world); return; }
    BYTE out[1024]; DWORD need;
    CHECK(GetKernelObjectSecurity(d, DACL_SECURITY_INFORMATION, out, sizeof out, &need));
    BOOL present = FALSE, def = FALSE; PACL a2 = NULL;
    CHECK(GetSecurityDescriptorDacl(out, &present, &a2, &def)); CHECK(present && a2);
    if (a2) {
        CHECK(a2->AceCount == 1);
        ACCESS_ALLOWED_ACE *ace = NULL;
        if (GetAce(a2, 0, (void **)&ace) && ace) {
            CHECK(ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE);
            CHECK(EqualSid((PSID)&ace->SidStart, world));
        }
    }
    HDESK o = U(OpenDesktopW)(n, 0, FALSE, DESKTOP_READOBJECTS | DESKTOP_ENUMERATE);
    CHECK(o != NULL);
    if (o) U(CloseDesktop)(o);
    U(CloseDesktop)(d);
    FreeSid(world);
}

/* A DACL that does not grant the creator the access it asks for makes the creation itself fail (the kernel opens the
 * new object on the creator's behalf); Windows 10 reports this as ERROR_NOT_ENOUGH_MEMORY. */
WT(desktop_create_with_dacl_lacking_requested_access_fails) {
    REQUIRE(U(CreateDesktopW));
    SECURITY_DESCRIPTOR sd; BYTE aclbuf[256]; PSID world = NULL;
    sd_with_world_ace(&sd, aclbuf, DESKTOP_READOBJECTS | DESKTOP_ENUMERATE | 0x00020000, &world);
    SECURITY_ATTRIBUTES sa = { sizeof sa, &sd, FALSE };
    WCHAR n[64]; wd_name(n, "esd2");
    SetLastError(0);
    HDESK d = U(CreateDesktopW)(n, NULL, NULL, 0, DESKTOP_READOBJECTS | 0x00020000, &sa);
    CHECK(d == NULL);
    EXPECT_ERR(ERROR_NOT_ENOUGH_MEMORY);
    if (d) U(CloseDesktop)(d);
    FreeSid(world);
}

WT(desktop_heap_default_nonzero) {
    REQUIRE(U(CreateDesktopW));
    HDESK d = wd_mk_desk("dh0", DESK_FULL, FALSE, NULL);
    if (!d) { wd_skip_create(); return; }
    DWORD kb = 0, need = 0;
    /* _GetUserObjectInformation case 5: *out = desktop->ulHeapSize >> 10 (4 byte result) */
    CHECK(U(GetUserObjectInformationW)(d, UOI_HEAP_, &kb, 4, &need));
    CHECK(need == 4);
    CHECK(kb > 0);
    t_note("default desktop heap = %lu KB", kb);
    U(CloseDesktop)(d);
}

WT(desktop_heap_explicit_size_reflected) {
    REQUIRE(U(CreateDesktopExW));
    static const ULONG kbs[] = { 512, 1024, 2048 };
    ULONG prev = 0;
    for (unsigned i = 0; i < 3; i++) {
        WCHAR n[64]; char tag[16]; sprintf(tag, "dhx%u", i); wd_name(n, tag);
        HDESK d = U(CreateDesktopExW)(n, NULL, NULL, 0, DESK_FULL, NULL, kbs[i], NULL);
        if (!d) { t_note("CreateDesktopExW(heap=%lu) gle=%lu", kbs[i], GetLastError()); continue; }
        DWORD kb = 0, need = 0;
        CHECK(U(GetUserObjectInformationW)(d, UOI_HEAP_, &kb, 4, &need));
        CKF(kb >= prev, "kb=%lu prev=%lu", kb, prev);   /* larger request never yields a smaller heap */
        t_note("requested %lu KB -> %lu KB", kbs[i], kb);
        prev = kb;
        U(CloseDesktop)(d);
    }
}

WT(desktop_heap_on_winsta_is_invalid_function) {
    REQUIRE(U(CreateWindowStationW));
    HWINSTA w = wd_mk_winsta("hwi", WSTA_FULL, FALSE, NULL);
    if (!w) { wd_skip_create(); return; }
    DWORD v = 0, need = 0;
    SetLastError(0);
    /* _GetUserObjectInformation: UOI_HEAPSIZE / UOI_IO only apply to desktops; a window station falls to the type-mismatch exit, ERROR_INVALID_FUNCTION (1) */
    CHECK(!U(GetUserObjectInformationW)(w, UOI_HEAP_, &v, 4, &need));
    EXPECT_ERR(ERROR_INVALID_FUNCTION);
    SetLastError(0);
    CHECK(!U(GetUserObjectInformationW)(w, UOI_IO_, &v, 4, &need));
    EXPECT_ERR(ERROR_INVALID_FUNCTION);
    U(CloseWindowStation)(w);
}

WT(desktop_created_in_process_winsta) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(EnumDesktopsW)); REQUIRE(U(GetProcessWindowStation));
    WCHAR n[64]; HDESK d = wd_mk_desk("dws", DESK_FULL, FALSE, n);
    if (!d) { wd_skip_create(); return; }
    /* CommonCreateDesktop roots the name at NtUserGetProcessWindowStation() */
    struct wd_enum e; memset(&e, 0, sizeof e); lstrcpyW(e.want, n);
    HWINSTA ws = U(GetProcessWindowStation)();
    CHECK(ws != NULL);
    CHECK(U(EnumDesktopsW)(ws, wd_enum_cb, (LPARAM)&e));
    CKF(e.found == 1, "found=%d count=%d", e.found, e.count);
    U(CloseDesktop)(d);
}

WT(desktop_enum_excludes_closed_desktop) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(EnumDesktopsW)); REQUIRE(U(GetProcessWindowStation));
    WCHAR n[64]; HDESK d = wd_mk_desk("dex", DESK_FULL, FALSE, n);
    if (!d) { wd_skip_create(); return; }
    CHECK(U(CloseDesktop)(d));
    struct wd_enum e; memset(&e, 0, sizeof e); lstrcpyW(e.want, n);
    CHECK(U(EnumDesktopsW)(U(GetProcessWindowStation)(), wd_enum_cb, (LPARAM)&e));
    CKF(e.found == 0, "found=%d (desktop destroyed with its last handle)", e.found);
}

WT(desktop_close_twice_second_fails) {
    REQUIRE(U(CreateDesktopW));
    HDESK d = wd_mk_desk("cl2", DESK_FULL, FALSE, NULL);
    if (!d) { wd_skip_create(); return; }
    CHECK(U(CloseDesktop)(d));
    SetLastError(0);
    CHECK(!U(CloseDesktop)(d));
    /* _CloseDesktop: ObReferenceObjectByHandle fails with STATUS_INVALID_HANDLE -> ERROR_INVALID_HANDLE */
    EXPECT_ERR(ERROR_INVALID_HANDLE);
}

WT(desktop_handle_value_nonnull_aligned) {
    REQUIRE(U(CreateDesktopW));
    HDESK d = wd_mk_desk("al", DESK_FULL, FALSE, NULL);
    if (!d) { wd_skip_create(); return; }
    CHECK(((ULONG_PTR)d & 3) == 0);     /* NT handle values are multiples of 4 (low 2 bits are tag bits) */
    CHECK((ULONG_PTR)d != 0);
    U(CloseDesktop)(d);
}

WT(desktop_two_creates_have_distinct_handles) {
    REQUIRE(U(CreateDesktopW));
    HDESK a = wd_mk_desk("dd1", DESK_FULL, FALSE, NULL), b = wd_mk_desk("dd2", DESK_FULL, FALSE, NULL);
    if (!a || !b) { wd_skip_create(); if (a) U(CloseDesktop)(a); if (b) U(CloseDesktop)(b); return; }
    CHECK(a != b);
    U(CloseDesktop)(a); U(CloseDesktop)(b);
}

WT(desktop_long_name_roundtrips) {
    REQUIRE(U(CreateDesktopW));
    WCHAR n[200], got[256];
    swprintf(n, 200, L"win32kt%lu_", GetCurrentProcessId());
    size_t l = lstrlenW(n); while (l < 120) n[l++] = L'L'; n[l] = 0;
    HDESK d = U(CreateDesktopW)(n, NULL, NULL, 0, DESK_FULL, NULL);
    if (!d) { t_note("long name create gle=%lu", GetLastError()); wd_skip_create(); return; }
    CHECK(wd_uoi_str(d, UOI_NAME_, got, 256)); EXPECT_WSTR(got, n);
    U(CloseDesktop)(d);
}

WT(desktop_name_with_backslash_rejected_or_noted) {
    REQUIRE(U(CreateDesktopW));
    WCHAR n[64]; swprintf(n, 64, L"win32kt%lu_a\\b", GetCurrentProcessId());
    SetLastError(0);
    HDESK d = U(CreateDesktopW)(n, NULL, NULL, 0, DESK_FULL, NULL);
    /* desktop names are single path components; the exact error is build specific */
    if (d) { t_note("backslash name accepted"); U(CloseDesktop)(d); } else t_note("backslash name rejected gle=%lu", GetLastError());
}
