/* dx_desktop_switch: GetUserObjectInformation (NtUserGetObjectInformation -> win32kfull!_GetUserObjectInformation)
 * on desktops and window stations. None of these tests switches the input desktop.
 *
 * _GetUserObjectInformation(handle, index, buf, len, &needed), in order:
 *   - ObReferenceObjectByHandle(handle, any type) failure -> RtlNtStatusToDosError (NULL/closed -> 6)
 *   - object neither WindowStation nor Desktop                          -> LastError 1 (ERROR_INVALID_FUNCTION)
 *   - index 5 (UOI_HEAPSIZE) / 6 (UOI_IO) on a window station            -> LastError 1
 *   - UOI_FLAGS(1): needs 12 bytes; fInherit = handle attribute OBJ_INHERIT (bit 1); for a station dwFlags is
 *       WSF_VISIBLE unless the station has the NOIO flag; reserved word is 0
 *   - UOI_NAME(2)/UOI_TYPE(3): needed = byte length + 2, too small buffer -> ERROR_INSUFFICIENT_BUFFER (0x7A)
 *       with *needed set
 *   - UOI_HEAPSIZE(5): 4 bytes, desktop heap size in KB; UOI_IO(6): 4 bytes, 1 iff the desktop is the input desktop
 *   - any other index -> LastError 0x57 with *needed = 0
 */
#include "dxd.h"

static BOOL uoi(HANDLE h, int idx, void *buf, DWORD len, DWORD *need) {
    return U(GetUserObjectInformationW)(h, idx, buf, len, need);
}

DXT(uoi_name_too_small_reports_needed_size) {
    REQUIRE(U(GetUserObjectInformationW)); REQUIRE(U(CreateDesktopW));
    WCHAR n[64]; HDESK d = dxd_mk_desk("uoin", DESKTOP_ALL_RIGHTS, n);
    if (!d) { t_skip("CreateDesktop failed"); return; }
    WCHAR b[128]; DWORD need = 0xCCCC;
    DWORD want = (DWORD)(lstrlenW(n) + 1) * sizeof(WCHAR);
    SetLastError(0);
    CHECK(!uoi(d, DXD_UOI_NAME, b, want - 2, &need));
    EXPECT_ERR(ERROR_INSUFFICIENT_BUFFER);
    CHECKF(need == want, "needed %lu want %lu", need, want);
    need = 0;
    CHECK(uoi(d, DXD_UOI_NAME, b, want, &need));
    CHECKF(need == want, "needed %lu want %lu", need, want);
    EXPECT_WSTR(b, n);
    U(CloseDesktop)(d);
}

DXT(uoi_type_buffer_protocol) {
    REQUIRE(U(GetUserObjectInformationW)); REQUIRE(U(CreateDesktopW));
    HDESK d = dxd_mk_desk("uoit", DESKTOP_ALL_RIGHTS, NULL);
    HWINSTA w = U(GetProcessWindowStation)();
    if (!d || !w) { t_skip("no desktop/station"); if (d) U(CloseDesktop)(d); return; }
    WCHAR b[32]; DWORD need = 0;
    SetLastError(0);
    CHECK(!uoi(d, DXD_UOI_TYPE, b, 2, &need));
    EXPECT_ERR(ERROR_INSUFFICIENT_BUFFER);
    CHECKF(need == (DWORD)(7 + 1) * 2, "Desktop needed %lu", need);        /* L"Desktop" + NUL */
    SetLastError(0); need = 0;
    CHECK(!uoi(w, DXD_UOI_TYPE, b, 0, &need));
    EXPECT_ERR(ERROR_INSUFFICIENT_BUFFER);
    CHECKF(need == (DWORD)(13 + 1) * 2, "WindowStation needed %lu", need);  /* L"WindowStation" + NUL */
    U(CloseDesktop)(d);
}

DXT(uoi_flags_buffer_too_small) {
    if (t_is_wine()) { t_skip("Wine's GetUserObjectInformation does not follow win32kfull!_GetUserObjectInformation"); return; }
    REQUIRE(U(GetUserObjectInformationW));
    HWINSTA w = U(GetProcessWindowStation)();
    if (!w) { t_skip("no window station"); return; }
    BYTE b[16]; DWORD need = 0;
    for (DWORD len = 0; len < 12; len += 4) {
        SetLastError(0); need = 0;
        CHECKF(!uoi(w, DXD_UOI_FLAGS, b, len, &need), "len %lu", len);
        EXPECT_ERR(ERROR_INSUFFICIENT_BUFFER);
        CHECKF(need == 12, "len %lu needed %lu", len, need);
    }
}

DXT(uoi_flags_fInherit_follows_open_inherit) {
    if (t_is_wine()) { t_skip("Wine's GetUserObjectInformation does not follow win32kfull!_GetUserObjectInformation"); return; }
    REQUIRE(U(GetUserObjectInformationW)); REQUIRE(U(CreateDesktopW)); REQUIRE(U(OpenDesktopW));
    WCHAR n[64]; HDESK d = dxd_mk_desk("uoiinh", DESKTOP_ALL_RIGHTS, n);
    if (!d) { t_skip("CreateDesktop failed"); return; }
    HDESK hi = U(OpenDesktopW)(n, 0, TRUE, DESKTOP_READOBJECTS), hn = U(OpenDesktopW)(n, 0, FALSE, DESKTOP_READOBJECTS);
    CHECK(hi && hn);
    USEROBJECTFLAGS f; DWORD need = 0;
    if (hi) { memset(&f, 0xCC, sizeof f); CHECK(uoi(hi, DXD_UOI_FLAGS, &f, sizeof f, &need)); CHECK(f.fInherit == TRUE); }
    if (hn) { memset(&f, 0xCC, sizeof f); CHECK(uoi(hn, DXD_UOI_FLAGS, &f, sizeof f, &need)); CHECK(f.fInherit == FALSE); CHECK(need == 12); }
    if (hi) U(CloseDesktop)(hi);
    if (hn) U(CloseDesktop)(hn);
    U(CloseDesktop)(d);
}

DXT(uoi_station_flags_reserved_is_zero) {
    if (t_is_wine()) { t_skip("Wine's GetUserObjectInformation does not follow win32kfull!_GetUserObjectInformation"); return; }
    REQUIRE(U(GetUserObjectInformationW));
    HWINSTA w = U(GetProcessWindowStation)();
    if (!w) { t_skip("no window station"); return; }
    DWORD f[3] = { 0xCCCCCCCC, 0xCCCCCCCC, 0xCCCCCCCC }, need = 0;
    CHECK(uoi(w, DXD_UOI_FLAGS, f, sizeof f, &need));
    CHECK(f[1] == 0);
    CHECK(f[0] == 0 || f[0] == 1);          /* fInherit is one bit of the handle attributes */
    CHECK(f[2] == 0 || f[2] == 1);          /* WSF_VISIBLE or nothing */
}

DXT(uoi_heapsize_on_desktop_is_positive_and_station_rejects_it) {
    if (t_is_wine()) { t_skip("Wine's GetUserObjectInformation does not follow win32kfull!_GetUserObjectInformation"); return; }
    REQUIRE(U(GetUserObjectInformationW)); REQUIRE(U(CreateDesktopW));
    HDESK d = dxd_mk_desk("uoihp", DESKTOP_ALL_RIGHTS, NULL);
    HWINSTA w = U(GetProcessWindowStation)();
    if (!d || !w) { t_skip("no desktop/station"); if (d) U(CloseDesktop)(d); return; }
    DWORD kb = 0, need = 0;
    CHECK(uoi(d, 5, &kb, 4, &need));
    CHECKF(need == 4, "needed %lu", need);
    CHECKF(kb > 0 && kb < 1024 * 1024, "heap size %lu KB", kb);
    SetLastError(0); need = 0xCCCC;
    CHECK(!uoi(d, 5, &kb, 3, &need));
    EXPECT_ERR(ERROR_INSUFFICIENT_BUFFER);
    CHECK(need == 4);
    SetLastError(0);
    CHECK(!uoi(w, 5, &kb, 4, &need));
    EXPECT_ERR(ERROR_INVALID_FUNCTION);       /* UOI_HEAPSIZE is a desktop-only index */
    U(CloseDesktop)(d);
}

DXT(uoi_io_flag_marks_only_the_input_desktop) {
    if (t_is_wine()) { t_skip("Wine's GetUserObjectInformation does not follow win32kfull!_GetUserObjectInformation"); return; }
    REQUIRE(U(GetUserObjectInformationW)); REQUIRE(U(CreateDesktopW)); REQUIRE(U(OpenInputDesktop));
    if (!k_has_interactive_desktop()) { t_skip("no interactive input desktop"); return; }
    HDESK d = dxd_mk_desk("uoiio", DESKTOP_ALL_RIGHTS, NULL);
    HDESK in = U(OpenInputDesktop)(0, FALSE, DESKTOP_READOBJECTS);
    if (!d || !in) { t_skip("no desktop"); if (d) U(CloseDesktop)(d); if (in) U(CloseDesktop)(in); return; }
    DWORD v = 0xCC, need = 0;
    CHECK(uoi(in, 6, &v, 4, &need)); CHECKF(v == 1, "input desktop UOI_IO %lu", v);     /* desktop == grpdeskRitInput */
    v = 0xCC;
    CHECK(uoi(d, 6, &v, 4, &need)); CHECKF(v == 0, "private desktop UOI_IO %lu", v);
    HWINSTA w = U(GetProcessWindowStation)();
    SetLastError(0);
    CHECK(!uoi(w, 6, &v, 4, &need));
    EXPECT_ERR(ERROR_INVALID_FUNCTION);
    U(CloseDesktop)(d); U(CloseDesktop)(in);
}

DXT(uoi_bad_index_fails_87_with_zero_needed) {
    if (t_is_wine()) { t_skip("Wine's GetUserObjectInformation does not follow win32kfull!_GetUserObjectInformation"); return; }
    REQUIRE(U(GetUserObjectInformationW)); REQUIRE(U(CreateDesktopW));
    HDESK d = dxd_mk_desk("uoibad", DESKTOP_ALL_RIGHTS, NULL);
    if (!d) { t_skip("CreateDesktop failed"); return; }
    static const int idx[] = { 0, 7, 8, 100, -1 };
    for (size_t i = 0; i < sizeof idx / sizeof idx[0]; i++) {
        BYTE b[64]; DWORD need = 0xCCCC;
        SetLastError(0);
        CHECKF(!uoi(d, idx[i], b, sizeof b, &need), "index %d", idx[i]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
        CHECKF(need == 0, "index %d needed %lu", idx[i], need);
    }
    U(CloseDesktop)(d);
}

DXT(uoi_non_usertype_handles_fail) {
    if (t_is_wine()) { t_skip("Wine's GetUserObjectInformation does not follow win32kfull!_GetUserObjectInformation"); return; }
    REQUIRE(U(GetUserObjectInformationW));
    HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    BYTE b[64]; DWORD need = 0;
    SetLastError(0);
    CHECK(!uoi(ev, DXD_UOI_NAME, b, sizeof b, &need));
    EXPECT_ERR(ERROR_INVALID_FUNCTION);        /* object is neither WindowStation nor Desktop */
    CloseHandle(ev);
    SetLastError(0);
    CHECK(!uoi(NULL, DXD_UOI_NAME, b, sizeof b, &need));
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    SetLastError(0);
    CHECK(!uoi((HANDLE)(ULONG_PTR)0x12344, DXD_UOI_NAME, b, sizeof b, &need));
    EXPECT_ERR(ERROR_INVALID_HANDLE);
}

DXT(uoi_user_sid_protocol_consistent) {
    if (t_is_wine()) { t_skip("Wine's GetUserObjectInformation does not follow win32kfull!_GetUserObjectInformation"); return; }
    REQUIRE(U(GetUserObjectInformationW)); REQUIRE(U(CreateDesktopW));
    HDESK d = dxd_mk_desk("uoisid", DESKTOP_ALL_RIGHTS, NULL);
    if (!d) { t_skip("CreateDesktop failed"); return; }
    DWORD need = 0xCCCC;
    BYTE small[1];
    SetLastError(0);
    BOOL rc = uoi(d, 4, small, 0, &need);
    if (rc) CHECKF(need == 0, "no SID stored but needed %lu", need);          /* SID pointer NULL: success, nothing to copy */
    else {
        EXPECT_ERR(ERROR_INSUFFICIENT_BUFFER);
        CHECK(need >= 8);
        BYTE big[256]; DWORD n2 = 0;
        if (need <= sizeof big) {
            CHECK(uoi(d, 4, big, sizeof big, &n2));
            CHECK(n2 == need);
            CHECK(IsValidSid(big));
            CHECK(GetLengthSid(big) == need);
        }
    }
    U(CloseDesktop)(d);
}

DXT(uoi_closed_desktop_handle_fails_6) {
    REQUIRE(U(GetUserObjectInformationW)); REQUIRE(U(CreateDesktopW));
    HDESK d = dxd_mk_desk("uoicl", DESKTOP_ALL_RIGHTS, NULL);
    if (!d) { t_skip("CreateDesktop failed"); return; }
    CHECK(U(CloseDesktop)(d));
    WCHAR b[32]; DWORD need = 0;
    SetLastError(0);
    CHECK(!uoi(d, DXD_UOI_NAME, b, sizeof b, &need));
    EXPECT_ERR(ERROR_INVALID_HANDLE);
}
