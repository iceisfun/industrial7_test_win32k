/* NtUserGetObjectInformation / NtUserSetObjectInformation: UOI_* protocol, buffer-size rules,
 * access checks, wrong-type handles, and the syscall stubs themselves. */
#include "wd.h"

static HDESK mk_d(const char *tag, WCHAR *n) { return wd_mk_desk(tag, DESK_FULL, FALSE, n); }
static HWINSTA mk_w(const char *tag, WCHAR *n) { return wd_mk_winsta(tag, WSTA_FULL, FALSE, n); }

/* ---- stub identity -------------------------------------------------------------------------- */
static const struct { const char *name; long number; } k_stubs[] = {
    { "NtUserGetProcessWindowStation", 4130 }, { "NtUserGetObjectInformation", 4202 },
    { "NtUserGetThreadDesktop", 4224 },        { "NtUserSetThreadDesktop", 4238 },
    { "NtUserOpenWindowStation", 4253 },       { "NtUserCloseDesktop", 4255 },
    { "NtUserOpenDesktop", 4256 },             { "NtUserSetProcessWindowStation", 4257 },
    { "NtUserBuildNameList", 4262 },           { "NtUserCloseWindowStation", 4269 },
    { "NtUserCreateDesktopEx", 5051 },         { "NtUserCreateWindowStation", 5056 },
    { "NtUserOpenInputDesktop", 5257 },        { "NtUserSetObjectInformation", 5347 },
};
WT(stubs_exported_and_valid) {
    for (unsigned i = 0; i < sizeof k_stubs / sizeof k_stubs[0]; i++) {
        FARPROC p = t_proc_in("win32u.dll", k_stubs[i].name);
        CKF(p != NULL, "%s", k_stubs[i].name);
        if (p) CKF(k_stub_valid(p), "%s prologue not a recognised syscall stub", k_stubs[i].name);
    }
}
WT(stubs_service_numbers_match_19041) {
    unsigned long b, u; k_win_build(&b, &u);
    if (!k_is_19041()) t_note("build %lu, service numbers below are 19041's", b);
    for (unsigned i = 0; i < sizeof k_stubs / sizeof k_stubs[0]; i++) {
        FARPROC p = t_proc_in("win32u.dll", k_stubs[i].name);
        if (!p || !k_stub_valid(p)) continue;
        long n = k_stub_number(p);
        if (k_is_19041()) CKF(n == k_stubs[i].number, "%s number %ld want %ld", k_stubs[i].name, n, k_stubs[i].number);
        else if (n != k_stubs[i].number) t_note("%s number %ld (19041: %ld)", k_stubs[i].name, n, k_stubs[i].number);
    }
}

/* ---- UOI_FLAGS ------------------------------------------------------------------------------ */
WT(uoi_flags_desktop_size_and_reserved) {
    REQUIRE(U(CreateDesktopW));
    HDESK d = mk_d("f1", NULL); if (!d) { wd_skip_create(); return; }
    USEROBJECTFLAGS f; memset(&f, 0xAA, sizeof f); DWORD need = 0;
    CHECK(U(GetUserObjectInformationW)(d, UOI_FLAGS_, &f, sizeof f, &need));
    CHECK(need == 12 && sizeof f == 12);
    CHECK(f.fReserved == 0);
    CHECK(f.fInherit == FALSE);
    U(CloseDesktop)(d);
}
WT(uoi_flags_winsta_size_and_reserved) {
    REQUIRE(U(CreateWindowStationW));
    HWINSTA w = mk_w("f2", NULL); if (!w) { wd_skip_create(); return; }
    USEROBJECTFLAGS f; memset(&f, 0xAA, sizeof f); DWORD need = 0;
    CHECK(U(GetUserObjectInformationW)(w, UOI_FLAGS_, &f, sizeof f, &need));
    CHECK(need == 12); CHECK(f.fReserved == 0); CHECK(f.fInherit == FALSE);
    CHECKF((f.dwFlags & ~(DWORD)(WSF_VISIBLE)) == 0, "dwFlags=0x%lx", f.dwFlags);   /* only bit 0 (WSF_VISIBLE) is ever stored for a winsta */
    U(CloseWindowStation)(w);
}
WT(uoi_flags_inherit_follows_handle) {
    REQUIRE(U(CreateDesktopW));
    HDESK a = wd_mk_desk("f3a", DESK_FULL, TRUE, NULL), b = wd_mk_desk("f3b", DESK_FULL, FALSE, NULL);
    if (!a || !b) { wd_skip_create(); if (a) U(CloseDesktop)(a); if (b) U(CloseDesktop)(b); return; }
    USEROBJECTFLAGS f; DWORD need;
 /* case 1: argument 3[0] = (handle attributes >> 1) & 1, i.e. OBJ_INHERIT */
    CHECK(U(GetUserObjectInformationW)(a, UOI_FLAGS_, &f, sizeof f, &need)); CHECK(f.fInherit == TRUE);
    CHECK(U(GetUserObjectInformationW)(b, UOI_FLAGS_, &f, sizeof f, &need)); CHECK(f.fInherit == FALSE);
    U(CloseDesktop)(a); U(CloseDesktop)(b);
}
WT(uoi_flags_winsta_inherit_follows_handle) {
    REQUIRE(U(CreateWindowStationW));
    HWINSTA a = wd_mk_winsta("f4a", WSTA_FULL, TRUE, NULL), b = wd_mk_winsta("f4b", WSTA_FULL, FALSE, NULL);
    if (!a || !b) { wd_skip_create(); if (a) U(CloseWindowStation)(a); if (b) U(CloseWindowStation)(b); return; }
    USEROBJECTFLAGS f; DWORD need;
    CHECK(U(GetUserObjectInformationW)(a, UOI_FLAGS_, &f, sizeof f, &need)); CHECK(f.fInherit == TRUE);
    CHECK(U(GetUserObjectInformationW)(b, UOI_FLAGS_, &f, sizeof f, &need)); CHECK(f.fInherit == FALSE);
    U(CloseWindowStation)(a); U(CloseWindowStation)(b);
}
WT(uoi_flags_short_buffer) {
    REQUIRE(U(CreateDesktopW));
    HDESK d = mk_d("f5", NULL); if (!d) { wd_skip_create(); return; }
    BYTE buf[16];
    for (DWORD len = 0; len < 12; len++) {
        DWORD need = 0; SetLastError(0);
        CKF(!U(GetUserObjectInformationW)(d, UOI_FLAGS_, buf, len, &need), "len=%lu", len);
        CKF(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "len=%lu gle=%lu", len, GetLastError());
        CKF(need == 12, "len=%lu need=%lu", len, need);
    }
    U(CloseDesktop)(d);
}
WT(uoi_flags_winsta_short_buffer) {
    REQUIRE(U(CreateWindowStationW));
    HWINSTA w = mk_w("f6", NULL); if (!w) { wd_skip_create(); return; }
    BYTE buf[16]; DWORD need = 0; SetLastError(0);
    CHECK(!U(GetUserObjectInformationW)(w, UOI_FLAGS_, buf, 11, &need));
    EXPECT_ERR(ERROR_INSUFFICIENT_BUFFER); CHECK(need == 12);
    U(CloseWindowStation)(w);
}
WT(uoi_flags_oversized_buffer_ok) {
    REQUIRE(U(CreateDesktopW));
    HDESK d = mk_d("f7", NULL); if (!d) { wd_skip_create(); return; }
    BYTE buf[64]; memset(buf, 0x5A, sizeof buf); DWORD need = 0;
    CHECK(U(GetUserObjectInformationW)(d, UOI_FLAGS_, buf, sizeof buf, &need));
    CHECK(need == 12);
    for (int i = 12; i < 64; i++) if (buf[i] != 0x5A) { CHECKF(0, "byte %d clobbered", i); break; }   /* only 12 bytes are written */
    U(CloseDesktop)(d);
}
WT(uoi_flags_WinSta0_visible_when_interactive) {
    REQUIRE(U(OpenWindowStationW));
    if (!wd_interactive()) { t_skip("not interactive"); return; }
    HWINSTA o = U(OpenWindowStationW)(L"WinSta0", FALSE, WINSTA_READATTRIBUTES);
    if (!o) { t_skip("open WinSta0 failed gle=%lu", GetLastError()); return; }
    USEROBJECTFLAGS f; DWORD need;
    CHECK(U(GetUserObjectInformationW)(o, UOI_FLAGS_, &f, sizeof f, &need));
    CHECKF(f.dwFlags == WSF_VISIBLE, "dwFlags=0x%lx", f.dwFlags);   /* flags&4 (non-interactive) clear -> 1 */
    U(CloseWindowStation)(o);
}

/* ---- UOI_NAME / UOI_TYPE buffer protocol ----------------------------------------------------- */
WT(uoi_name_needed_is_len_plus_nul) {
    REQUIRE(U(CreateDesktopW));
    WCHAR n[64]; HDESK d = mk_d("nn", n); if (!d) { wd_skip_create(); return; }
    DWORD need = 0; SetLastError(0);
    CHECK(!U(GetUserObjectInformationW)(d, UOI_NAME_, NULL, 0, &need));
    EXPECT_ERR(ERROR_INSUFFICIENT_BUFFER);
    CHECKF(need == (lstrlenW(n) + 1) * 2, "need=%lu", need);   /* Length + sizeof(WCHAR) */
    U(CloseDesktop)(d);
}
WT(uoi_name_exact_and_short_buffers) {
    REQUIRE(U(CreateDesktopW));
    WCHAR n[64], buf[128]; HDESK d = mk_d("ne", n); if (!d) { wd_skip_create(); return; }
    DWORD full = (lstrlenW(n) + 1) * 2, need = 0;
    CHECK(U(GetUserObjectInformationW)(d, UOI_NAME_, buf, full, &need)); CHECK(need == full); EXPECT_WSTR(buf, n);
    SetLastError(0);
    CHECK(!U(GetUserObjectInformationW)(d, UOI_NAME_, buf, full - 1, &need)); EXPECT_ERR(ERROR_INSUFFICIENT_BUFFER); CHECK(need == full);
    SetLastError(0);
    CHECK(!U(GetUserObjectInformationW)(d, UOI_NAME_, buf, full - 2, &need)); EXPECT_ERR(ERROR_INSUFFICIENT_BUFFER);
    U(CloseDesktop)(d);
}
WT(uoi_name_terminated_by_kernel) {
    REQUIRE(U(CreateDesktopW));
    WCHAR n[64], buf[128]; HDESK d = mk_d("nt", n); if (!d) { wd_skip_create(); return; }
    for (int i = 0; i < 128; i++) buf[i] = 0x7777;
    DWORD need;
    CHECK(U(GetUserObjectInformationW)(d, UOI_NAME_, buf, sizeof buf, &need));
    CHECK(buf[lstrlenW(n)] == 0);    /* memcpy(Length) then explicit WCHAR 0 at +Length */
    CHECK(buf[lstrlenW(n) + 1] == 0x7777);
    U(CloseDesktop)(d);
}
WT(uoi_type_needed_sizes) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(CreateWindowStationW));
    HDESK d = mk_d("ts1", NULL); HWINSTA w = mk_w("ts2", NULL);
    if (!d || !w) { wd_skip_create(); if (d) U(CloseDesktop)(d); if (w) U(CloseWindowStation)(w); return; }
    DWORD need = 0; SetLastError(0);
    CHECK(!U(GetUserObjectInformationW)(d, UOI_TYPE_, NULL, 0, &need)); CHECK(need == (DWORD)(lstrlenW(L"Desktop") + 1) * 2);
    CHECK(!U(GetUserObjectInformationW)(w, UOI_TYPE_, NULL, 0, &need)); CHECK(need == (DWORD)(lstrlenW(L"WindowStation") + 1) * 2);
    U(CloseDesktop)(d); U(CloseWindowStation)(w);
}
WT(uoi_type_exact_buffer) {
    REQUIRE(U(CreateDesktopW));
    HDESK d = mk_d("te", NULL); if (!d) { wd_skip_create(); return; }
    WCHAR b[16]; DWORD need = 0;
    CHECK(U(GetUserObjectInformationW)(d, UOI_TYPE_, b, 8 * 2, &need)); EXPECT_WSTR(b, L"Desktop");
    SetLastError(0); CHECK(!U(GetUserObjectInformationW)(d, UOI_TYPE_, b, 7 * 2 + 1, &need)); EXPECT_ERR(ERROR_INSUFFICIENT_BUFFER);
    U(CloseDesktop)(d);
}

/* ---- UOI_USER_SID --------------------------------------------------------------------------- */
WT(uoi_user_sid_protocol_winsta) {
    REQUIRE(U(CreateWindowStationW));
    HWINSTA w = mk_w("us", NULL); if (!w) { wd_skip_create(); return; }
    DWORD need = 0xFFFFFFFF; SetLastError(0);
    BOOL r = U(GetUserObjectInformationW)(w, UOI_SID_, NULL, 0, &need);
    if (r) { CHECK(need == 0); t_note("winstation has no user SID (success, need=0)"); }
    else {
        EXPECT_ERR(ERROR_INSUFFICIENT_BUFFER);
        BYTE sid[256]; DWORD n2 = 0;
        CHECK(need > 0 && need <= sizeof sid);
        CHECK(U(GetUserObjectInformationW)(w, UOI_SID_, sid, sizeof sid, &n2));
        CHECK(IsValidSid(sid));
        CHECK(n2 == GetLengthSid(sid));   /* RtlLengthSid */
        CHECK(n2 == need);
    }
    U(CloseWindowStation)(w);
}
WT(uoi_user_sid_desktop_equals_its_winsta) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(GetProcessWindowStation));
    HDESK d = mk_d("ud", NULL); if (!d) { wd_skip_create(); return; }
    BYTE a[256], b[256]; DWORD na = 0, nb = 0;
    HWINSTA ws = U(GetProcessWindowStation)();
    BOOL ra = U(GetUserObjectInformationW)(d, UOI_SID_, a, sizeof a, &na);
    BOOL rb = U(GetUserObjectInformationW)(ws, UOI_SID_, b, sizeof b, &nb);
    CHECK(ra == rb);
    /* case 4 reads the desktop's owning winstation's SID (obj+0x28) */
    if (ra && rb && na) { CHECK(na == nb); CHECK(EqualSid(a, b)); } else t_note("no SID ra=%d rb=%d na=%lu", ra, rb, na);
    U(CloseDesktop)(d);
}
WT(uoi_user_sid_short_buffer) {
    REQUIRE(U(OpenWindowStationW));
    if (!wd_interactive()) { t_skip("not interactive"); return; }
    HWINSTA o = U(OpenWindowStationW)(L"WinSta0", FALSE, WINSTA_READATTRIBUTES);
    if (!o) { t_skip("open failed"); return; }
    DWORD need = 0; BYTE b[4];
    SetLastError(0);
    BOOL r = U(GetUserObjectInformationW)(o, UOI_SID_, b, 1, &need);
    if (r) t_note("WinSta0 reports no SID"); else { EXPECT_ERR(ERROR_INSUFFICIENT_BUFFER); CHECK(need >= 8); }
    U(CloseWindowStation)(o);
}

/* ---- UOI_HEAPSIZE / UOI_IO ------------------------------------------------------------------ */
WT(uoi_heapsize_short_buffer) {
    REQUIRE(U(CreateDesktopW));
    HDESK d = mk_d("hs", NULL); if (!d) { wd_skip_create(); return; }
    DWORD v, need = 0;
    for (DWORD len = 0; len < 4; len++) {
        SetLastError(0);
        CKF(!U(GetUserObjectInformationW)(d, UOI_HEAP_, &v, len, &need), "len=%lu", len);
        CKF(GetLastError() == ERROR_INSUFFICIENT_BUFFER && need == 4, "len=%lu need=%lu", len, need);
    }
    U(CloseDesktop)(d);
}
WT(uoi_io_created_desktop_is_not_input) {
    REQUIRE(U(CreateDesktopW));
    HDESK d = mk_d("io", NULL); if (!d) { wd_skip_create(); return; }
    DWORD v = 0xFF, need = 0;
    CHECK(U(GetUserObjectInformationW)(d, UOI_IO_, &v, 4, &need));
    CHECK(need == 4); CHECK(v == 0);   /* case 6: desktop == grpdeskRitInput; no SwitchDesktop was done */
    U(CloseDesktop)(d);
}
WT(uoi_io_short_buffer) {
    REQUIRE(U(CreateDesktopW));
    HDESK d = mk_d("io2", NULL); if (!d) { wd_skip_create(); return; }
    DWORD v, need = 0; SetLastError(0);
    CHECK(!U(GetUserObjectInformationW)(d, UOI_IO_, &v, 3, &need)); EXPECT_ERR(ERROR_INSUFFICIENT_BUFFER); CHECK(need == 4);
    U(CloseDesktop)(d);
}
WT(uoi_io_input_desktop_is_true) {
    REQUIRE(U(OpenInputDesktop)); REQUIRE(U(CloseDesktop));
    HDESK i = U(OpenInputDesktop)(0, FALSE, DESKTOP_READOBJECTS);
    if (!i) { t_skip("OpenInputDesktop gle=%lu (secure desktop?)", GetLastError()); return; }
    DWORD v = 0, need = 0;
    CHECK(U(GetUserObjectInformationW)(i, UOI_IO_, &v, 4, &need));
    CHECK(v == 1);   /* the handle names grpdeskRitInput by definition */
    U(CloseDesktop)(i);
}

/* ---- invalid index / wrong type / bad handle ------------------------------------------------ */
static const int bad_idx[] = { 0, 7, 8, 9, 16, 0x100, -1, 0x7fffffff };
WT(uoi_get_invalid_index_desktop) {
    REQUIRE(U(CreateDesktopW));
    HDESK d = mk_d("bi1", NULL); if (!d) { wd_skip_create(); return; }
    BYTE b[64]; DWORD need;
    for (unsigned i = 0; i < sizeof bad_idx / sizeof bad_idx[0]; i++) {
        SetLastError(0);
        CKF(!U(GetUserObjectInformationW)(d, bad_idx[i], b, sizeof b, &need), "idx=%d", bad_idx[i]);
        CKF(GetLastError() == ERROR_INVALID_PARAMETER, "idx=%d gle=%lu", bad_idx[i], GetLastError());
    }
    U(CloseDesktop)(d);
}
WT(uoi_get_invalid_index_winsta) {
    REQUIRE(U(CreateWindowStationW));
    HWINSTA w = mk_w("bi2", NULL); if (!w) { wd_skip_create(); return; }
    BYTE b[64]; DWORD need;
    for (unsigned i = 0; i < sizeof bad_idx / sizeof bad_idx[0]; i++) {
        SetLastError(0);
        CKF(!U(GetUserObjectInformationW)(w, bad_idx[i], b, sizeof b, &need), "idx=%d", bad_idx[i]);
        CKF(GetLastError() == ERROR_INVALID_PARAMETER, "idx=%d gle=%lu", bad_idx[i], GetLastError());
    }
    U(CloseWindowStation)(w);
}
WT(uoi_get_wrong_type_handle) {
    REQUIRE(U(GetUserObjectInformationW));
    HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    BYTE b[64]; DWORD need = 0;
    static const int idx[] = { UOI_FLAGS_, UOI_NAME_, UOI_TYPE_, UOI_SID_, UOI_HEAP_, UOI_IO_ };
    for (unsigned i = 0; i < 6; i++) {
        SetLastError(0);
        /* _GetUserObjectInformation: object type is neither WindowStation nor Desktop -> UserSetLastError(ERROR_INVALID_FUNCTION) */
        CKF(!U(GetUserObjectInformationW)(ev, idx[i], b, sizeof b, &need), "idx=%d", idx[i]);
        CKF(GetLastError() == ERROR_INVALID_FUNCTION, "idx=%d gle=%lu", idx[i], GetLastError());
    }
    CloseHandle(ev);
}
WT(uoi_get_wrong_type_does_not_alter_handle_flags) {
    REQUIRE(U(GetUserObjectInformationW));
    HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    DWORD f0 = 0, f1 = 0, need; BYTE b[64];
    GetHandleInformation(ev, &f0);
    U(GetUserObjectInformationW)(ev, UOI_NAME_, b, sizeof b, &need);
    GetHandleInformation(ev, &f1);
    CHECK(f0 == f1);   /* the entry point sets and clears protect-from-close (SetHandleFlag 2) around the call */
    CloseHandle(ev);
}
WT(uoi_get_invalid_handles_fail) {
    REQUIRE(U(GetUserObjectInformationW));
    static const ULONG_PTR hs[] = { 0, 4, 0x7777770, 0xFFFFFFFFFFFF0000ull, (ULONG_PTR)-1 };
    BYTE b[64]; DWORD need = 0;
    for (unsigned i = 0; i < sizeof hs / sizeof hs[0]; i++) {
        CKF(!U(GetUserObjectInformationW)((HANDLE)hs[i], UOI_NAME_, b, sizeof b, &need), "handle=%p", (void *)hs[i]);   /* -1 is the process pseudo handle: wrong type, still a failure */
    }
}
WT(uoi_get_process_pseudo_handle_is_wrong_type) {
    REQUIRE(U(GetUserObjectInformationW));
    BYTE b[64]; DWORD need = 0; SetLastError(0);
    CHECK(!U(GetUserObjectInformationW)(GetCurrentProcess(), UOI_NAME_, b, sizeof b, &need));
}

/* ---- access checks -------------------------------------------------------------------------- */
/* Windows 10 adds DESKTOP_READOBJECTS and DESKTOP_WRITEOBJECTS to every desktop handle it opens, whatever was asked for,
 * so no desktop handle can be refused for lacking them (a window station handle can: see the winsta tests below). */
WT(uoi_get_desktop_works_with_any_access_mask) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(NTQO());
    WCHAR n[64]; HDESK d = mk_d("ar", n); if (!d) { wd_skip_create(); return; }
    static const DWORD ask[] = { 0, DESKTOP_WRITEOBJECTS, DESKTOP_READOBJECTS, DESKTOP_ENUMERATE };
    for (unsigned i = 0; i < sizeof ask / sizeof ask[0]; i++) {
        HDESK h = U(OpenDesktopW)(n, 0, FALSE, ask[i]);
        CHECKF(h != NULL, "ask 0x%lx", ask[i]);
        if (!h) continue;
        WCHAR b[64]; ULONG g = 0;
        CHECKF(wd_uoi_str(h, UOI_NAME_, b, 64), "ask 0x%lx", ask[i]);
        CHECK(!lstrcmpiW(b, n));
        CHECK(wd_granted(h, &g));
        CHECKF((g & (DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS)) == (DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS), "ask 0x%lx granted 0x%lx", ask[i], g);
        CHECKF((g & ask[i]) == ask[i], "ask 0x%lx granted 0x%lx", ask[i], g);
        U(CloseDesktop)(h);
    }
    U(CloseDesktop)(d);
}
WT(uoi_get_winsta_needs_readattributes) {
    REQUIRE(U(CreateWindowStationW));
    WCHAR n[64]; HWINSTA h = mk_w("ar2", n); if (!h) { wd_skip_create(); return; }
    HWINSTA e = U(OpenWindowStationW)(n, FALSE, WINSTA_ENUMDESKTOPS), r = U(OpenWindowStationW)(n, FALSE, WINSTA_READATTRIBUTES);
    CHECK(e && r);
    WCHAR b[64];
    if (e) { SetLastError(0); CHECK(!wd_uoi_str(e, UOI_NAME_, b, 64)); EXPECT_ERR(ERROR_ACCESS_DENIED); U(CloseWindowStation)(e); }
    if (r) { CHECK(wd_uoi_str(r, UOI_NAME_, b, 64)); U(CloseWindowStation)(r); }
    U(CloseWindowStation)(h);
}
WT(uoi_set_desktop_works_with_read_only_request) {
    REQUIRE(U(CreateDesktopW));
    WCHAR n[64]; HDESK d = mk_d("as", n); if (!d) { wd_skip_create(); return; }
    HDESK r = U(OpenDesktopW)(n, 0, FALSE, DESKTOP_READOBJECTS);
    CHECK(r != NULL);
    if (r) {
        /* the handle carries DESKTOP_WRITEOBJECTS although only DESKTOP_READOBJECTS was asked for, so the set is allowed */
        USEROBJECTFLAGS f = { TRUE, FALSE, 0 }; SetLastError(0);
        CHECK(!wd_is_inherit(r));
        CHECK(U(SetUserObjectInformationW)(r, UOI_FLAGS_, &f, sizeof f));
        CHECK(wd_is_inherit(r));
        U(CloseDesktop)(r);
    }
    U(CloseDesktop)(d);
}
WT(uoi_set_winsta_needs_writeattributes) {
    REQUIRE(U(CreateWindowStationW));
    WCHAR n[64]; HWINSTA h = mk_w("as2", n); if (!h) { wd_skip_create(); return; }
    HWINSTA r = U(OpenWindowStationW)(n, FALSE, WINSTA_READATTRIBUTES);
    CHECK(r != NULL);
    if (r) {
        USEROBJECTFLAGS f = { TRUE, FALSE, 0 }; SetLastError(0);
        CHECK(!U(SetUserObjectInformationW)(r, UOI_FLAGS_, &f, sizeof f)); EXPECT_ERR(ERROR_ACCESS_DENIED);
        U(CloseWindowStation)(r);
    }
    U(CloseWindowStation)(h);
}

/* ---- SetUserObjectInformation ---------------------------------------------------------------- */
WT(uoi_set_flags_toggles_handle_inherit_desktop) {
    REQUIRE(U(CreateDesktopW));
    HDESK d = mk_d("st", NULL); if (!d) { wd_skip_create(); return; }
    USEROBJECTFLAGS f = { TRUE, FALSE, 0 }, g;
    CHECK(!wd_is_inherit(d));
 /* _SetUserObjectInformation case 1: ObSetHandleAttributes */
    CHECK(U(SetUserObjectInformationW)(d, UOI_FLAGS_, &f, sizeof f));
    CHECK(wd_is_inherit(d));
    DWORD need; CHECK(U(GetUserObjectInformationW)(d, UOI_FLAGS_, &g, sizeof g, &need)); CHECK(g.fInherit);
    f.fInherit = FALSE;
    CHECK(U(SetUserObjectInformationW)(d, UOI_FLAGS_, &f, sizeof f));
    CHECK(!wd_is_inherit(d));
    CHECK(U(GetUserObjectInformationW)(d, UOI_FLAGS_, &g, sizeof g, &need)); CHECK(!g.fInherit);
    U(CloseDesktop)(d);
}
WT(uoi_set_flags_toggles_handle_inherit_winsta) {
    REQUIRE(U(CreateWindowStationW));
    HWINSTA w = mk_w("st2", NULL); if (!w) { wd_skip_create(); return; }
    USEROBJECTFLAGS f = { TRUE, FALSE, 0 };
    CHECK(U(SetUserObjectInformationW)(w, UOI_FLAGS_, &f, sizeof f)); CHECK(wd_is_inherit(w));
    f.fInherit = FALSE;
    CHECK(U(SetUserObjectInformationW)(w, UOI_FLAGS_, &f, sizeof f)); CHECK(!wd_is_inherit(w));
    U(CloseWindowStation)(w);
}
WT(uoi_set_flags_only_affects_that_handle) {
    REQUIRE(U(CreateDesktopW));
    WCHAR n[64]; HDESK a = mk_d("sh", n); if (!a) { wd_skip_create(); return; }
    HDESK b = U(OpenDesktopW)(n, 0, FALSE, DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS);
    CHECK(b != NULL);
    if (b) {
        USEROBJECTFLAGS f = { TRUE, FALSE, 0 };
        CHECK(U(SetUserObjectInformationW)(b, UOI_FLAGS_, &f, sizeof f));
        CHECK(wd_is_inherit(b)); CHECK(!wd_is_inherit(a));   /* handle attribute, not object state */
        U(CloseDesktop)(b);
    }
    U(CloseDesktop)(a);
}
WT(uoi_set_flags_short_length) {
    REQUIRE(U(CreateDesktopW));
    HDESK d = mk_d("sl", NULL); if (!d) { wd_skip_create(); return; }
    USEROBJECTFLAGS f = { TRUE, FALSE, 0 };
    for (DWORD len = 0; len < 12; len++) {
        SetLastError(0);
        CKF(!U(SetUserObjectInformationW)(d, UOI_FLAGS_, &f, len), "len=%lu", len);
        CKF(GetLastError() == ERROR_INVALID_DATA, "len=%lu gle=%lu", len, GetLastError());   /* UserSetLastError(0xd) */
        CKF(!wd_is_inherit(d), "len=%lu inherit changed", len);
    }
    U(CloseDesktop)(d);
}
WT(uoi_set_invalid_index) {
    REQUIRE(U(CreateDesktopW));
    HDESK d = mk_d("si", NULL); if (!d) { wd_skip_create(); return; }
    USEROBJECTFLAGS f = { TRUE, FALSE, 0 };
    static const int idx[] = { 0, 2, 3, 4, 5, 6, 8, 9, 100, -1 };
    for (unsigned i = 0; i < sizeof idx / sizeof idx[0]; i++) {
        SetLastError(0);
        CKF(!U(SetUserObjectInformationW)(d, idx[i], &f, sizeof f), "idx=%d", idx[i]);
        CKF(GetLastError() == ERROR_INVALID_PARAMETER, "idx=%d gle=%lu", idx[i], GetLastError());
    }
    CHECK(!wd_is_inherit(d));
    U(CloseDesktop)(d);
}
WT(uoi_set_wrong_type_handle) {
    REQUIRE(U(SetUserObjectInformationW));
    HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    USEROBJECTFLAGS f = { TRUE, FALSE, 0 }; SetLastError(0);
    CHECK(!U(SetUserObjectInformationW)(ev, UOI_FLAGS_, &f, sizeof f));
    EXPECT_ERR(ERROR_INVALID_FUNCTION);   /* _SetUserObjectInformation: not winsta/desktop -> UserSetLastError(1) */
    CloseHandle(ev);
}
WT(uoi_set_invalid_handle_fails) {
    REQUIRE(U(SetUserObjectInformationW));
    USEROBJECTFLAGS f = { TRUE, FALSE, 0 };
    CHECK(!U(SetUserObjectInformationW)(NULL, UOI_FLAGS_, &f, sizeof f));
    CHECK(!U(SetUserObjectInformationW)((HANDLE)(ULONG_PTR)0x7777770, UOI_FLAGS_, &f, sizeof f));
}
WT(uoi_set_index7_process_handle_ok) {
    REQUIRE(U(SetUserObjectInformationW));
    DWORD v = 0;
    /* user32!SetUserObjectInformationW handles index 7 locally: hObj == GetCurrentProcess() and nLength == 4 */
    CHECK(U(SetUserObjectInformationW)(GetCurrentProcess(), 7, &v, 4));
}
WT(uoi_set_index7_bad_args) {
    REQUIRE(U(CreateDesktopW));
    HDESK d = mk_d("w32kt_d", NULL); if (!d) { wd_skip_create(); return; }
    DWORD v = 0;
    SetLastError(0); CHECK(!U(SetUserObjectInformationW)(d, 7, &v, 4)); EXPECT_ERR(ERROR_INVALID_PARAMETER);   /* not the process pseudo handle */
    SetLastError(0); CHECK(!U(SetUserObjectInformationW)(GetCurrentProcess(), 7, &v, 8)); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SetLastError(0); CHECK(!U(SetUserObjectInformationW)(GetCurrentProcess(), 7, &v, 0)); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    U(CloseDesktop)(d);
}

/* ---- direct syscall stubs (same argument shape as the API) ---------------------------------- */
WT(nt_get_object_information_direct_name) {
    p_NtUserGetObjectInformation g = NTU(NtUserGetObjectInformation); REQUIRE(g); REQUIRE(U(CreateDesktopW));
    WCHAR n[64], buf[128]; HDESK d = mk_d("ng", n); if (!d) { wd_skip_create(); return; }
    DWORD need = 0;
    CHECK(g(d, UOI_NAME_, buf, sizeof buf, &need));
    EXPECT_WSTR(buf, n); CHECK(need == (DWORD)(lstrlenW(n) + 1) * 2);
    U(CloseDesktop)(d);
}
WT(nt_get_object_information_null_need_pointer) {
    p_NtUserGetObjectInformation g = NTU(NtUserGetObjectInformation); REQUIRE(g); REQUIRE(U(CreateDesktopW));
    HDESK d = mk_d("ng2", NULL); if (!d) { wd_skip_create(); return; }
    WCHAR buf[128];
 /* the entry point treats argument 5 == NULL as "caller does not want the length" (guarded stores) */
    CHECK(g(d, UOI_NAME_, buf, sizeof buf, NULL));
    U(CloseDesktop)(d);
}
WT(nt_get_object_information_flags_direct) {
    p_NtUserGetObjectInformation g = NTU(NtUserGetObjectInformation); REQUIRE(g); REQUIRE(U(CreateDesktopW));
    HDESK d = wd_mk_desk("ng3", DESK_FULL, TRUE, NULL); if (!d) { wd_skip_create(); return; }
    USEROBJECTFLAGS f; DWORD need = 0;
    CHECK(g(d, UOI_FLAGS_, &f, sizeof f, &need)); CHECK(f.fInherit == TRUE); CHECK(need == 12);
    U(CloseDesktop)(d);
}
WT(nt_set_object_information_direct_flags) {
    p_NtUserSetObjectInformation s = NTU(NtUserSetObjectInformation); REQUIRE(s); REQUIRE(U(CreateDesktopW));
    HDESK d = mk_d("ns", NULL); if (!d) { wd_skip_create(); return; }
    USEROBJECTFLAGS f = { TRUE, FALSE, 0 };
    CHECK(s(d, UOI_FLAGS_, &f, sizeof f)); CHECK(wd_is_inherit(d));
    U(CloseDesktop)(d);
}
WT(nt_set_object_information_index7_is_kernel_noop_failure) {
    p_NtUserSetObjectInformation s = NTU(NtUserSetObjectInformation); REQUIRE(s); REQUIRE(U(CreateDesktopW));
    HDESK d = mk_d("ns7", NULL); if (!d) { wd_skip_create(); return; }
    DWORD v = 0;
    /* NtUserSetObjectInformation: index 7 is only meaningful to user32; the kernel returns 0 without touching the handle */
    CHECK(!s(d, 7, &v, 4));
    CHECK(!wd_is_inherit(d));
    U(CloseDesktop)(d);
}
WT(nt_user32_get_and_stub_agree_on_every_index) {
    p_NtUserGetObjectInformation g = NTU(NtUserGetObjectInformation); REQUIRE(g); REQUIRE(U(CreateDesktopW));
    HDESK d = mk_d("na", NULL); if (!d) { wd_skip_create(); return; }
    for (int idx = 0; idx <= 8; idx++) {
        BYTE b1[256] = {0}, b2[256] = {0}; DWORD n1 = 0, n2 = 0;
        BOOL r1 = U(GetUserObjectInformationW)(d, idx, b1, sizeof b1, &n1), r2 = g(d, idx, b2, sizeof b2, &n2);
        CKF(r1 == r2 && n1 == n2, "idx=%d r1=%d r2=%d n1=%lu n2=%lu", idx, r1, r2, n1, n2);
        if (r1 && r2) CKF(!memcmp(b1, b2, n1), "idx=%d content", idx);
    }
    U(CloseDesktop)(d);
}
