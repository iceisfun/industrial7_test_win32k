/* Direct syscall-stub coverage for the desktop entry points that have no other direct caller, plus
 * OpenInputDesktop semantics (EditionOpenInputDesktopEntryPoint) and thread-desktop handle bookkeeping. */
#include "wd.h"

typedef HANDLE (WINAPI *p_NtUserOpenInputDesktop)(DWORD, BOOL, ACCESS_MASK);

/* EditionOpenInputDesktopEntryPoint opens grpdeskRitInput with (access | 0x81), i.e. READOBJECTS|WRITEOBJECTS always added */
WT(open_input_desktop_adds_read_and_write_objects) {
    REQUIRE(U(OpenInputDesktop)); REQUIRE(U(CloseDesktop));
    HDESK i = U(OpenInputDesktop)(0, FALSE, DESKTOP_ENUMERATE);
    if (!i) { t_skip("OpenInputDesktop gle=%lu", GetLastError()); return; }
    ULONG g = 0;
    if (wd_granted(i, &g)) CKF((g & 0x81) == 0x81 && (g & DESKTOP_ENUMERATE), "granted=0x%lx", g);
    WCHAR t[32];
    CHECK(wd_uoi_str(i, UOI_TYPE_, t, 32)); EXPECT_WSTR(t, L"Desktop");
    U(CloseDesktop)(i);
}
/* ObOpenObjectByPointer attributes = fInherit ? OBJ_INHERIT(2) : 0 */
WT(open_input_desktop_inherit_follows_argument) {
    REQUIRE(U(OpenInputDesktop)); REQUIRE(U(CloseDesktop));
    HDESK a = U(OpenInputDesktop)(0, TRUE, DESKTOP_READOBJECTS), b = U(OpenInputDesktop)(0, FALSE, DESKTOP_READOBJECTS);
    if (!a || !b) { t_skip("OpenInputDesktop gle=%lu", GetLastError()); if (a) U(CloseDesktop)(a); if (b) U(CloseDesktop)(b); return; }
    CHECK(wd_is_inherit(a)); CHECK(!wd_is_inherit(b));
    USEROBJECTFLAGS f; DWORD need;
    CHECK(U(GetUserObjectInformationW)(a, UOI_FLAGS_, &f, sizeof f, &need)); CHECK(f.fInherit);
    CHECK(U(GetUserObjectInformationW)(b, UOI_FLAGS_, &f, sizeof f, &need)); CHECK(!f.fInherit);
    CHECK(a != b);
    U(CloseDesktop)(a); U(CloseDesktop)(b);
}
WT(nt_open_input_desktop_direct_matches_api) {
    p_NtUserOpenInputDesktop o = (p_NtUserOpenInputDesktop)t_proc_in("win32u.dll", "NtUserOpenInputDesktop");
    REQUIRE(o); REQUIRE(U(OpenInputDesktop)); REQUIRE(U(CloseDesktop));
    HDESK a = o(0, FALSE, DESKTOP_READOBJECTS), b = U(OpenInputDesktop)(0, FALSE, DESKTOP_READOBJECTS);
    if (!a || !b) { t_skip("OpenInputDesktop gle=%lu", GetLastError()); if (a) U(CloseDesktop)(a); if (b) U(CloseDesktop)(b); return; }
    WCHAR na[64], nb[64], ta[256];
    CHECK(wd_uoi_str(a, UOI_NAME_, na, 64)); CHECK(wd_uoi_str(b, UOI_NAME_, nb, 64)); EXPECT_WSTR(na, nb);
    DWORD ioa = 0, iob = 0, need;
    CHECK(U(GetUserObjectInformationW)(a, UOI_IO_, &ioa, 4, &need)); CHECK(U(GetUserObjectInformationW)(b, UOI_IO_, &iob, 4, &need));
    CHECK(ioa == 1 && iob == 1);
    CHECK(wd_type_name(a, ta, 256)); EXPECT_WSTR(ta, L"Desktop");
    U(CloseDesktop)(a); U(CloseDesktop)(b);
}
/* A non-interactive window station (flags & 4, i.e. UOI dwFlags without WSF_VISIBLE) makes the entry point fail with ERROR_INVALID_FUNCTION (1). */
CHILD(wsdo_inputdesk_custom_winsta) {
    (void)argc; (void)argv; g_cf = 0;
    if (!U(OpenInputDesktop)) return 0;
    HWINSTA orig = U(GetProcessWindowStation)();
    HWINSTA w = wd_mk_winsta("oid", WSTA_FULL, FALSE, NULL);
    if (!w) { printf("NOTE create winsta failed gle=%lu\n", GetLastError()); return 0; }
    USEROBJECTFLAGS f; DWORD need = 0;
    CK(U(GetUserObjectInformationW)(w, UOI_FLAGS_, &f, sizeof f, &need));
    if (U(SetProcessWindowStation)(w)) {
        SetLastError(0);
        HDESK i = U(OpenInputDesktop)(0, FALSE, DESKTOP_READOBJECTS);
        if (!(f.dwFlags & WSF_VISIBLE)) {
            CK(i == NULL); CKF(GetLastError() == ERROR_INVALID_FUNCTION, "gle=%lu", GetLastError());
        } else printf("NOTE custom winsta reports WSF_VISIBLE; OpenInputDesktop -> %p gle=%lu\n", (void *)i, GetLastError());
        if (i) U(CloseDesktop)(i);
        CK(U(SetProcessWindowStation)(orig));
    }
    U(CloseWindowStation)(w);
    return g_cf;
}
WT(open_input_desktop_noninteractive_winsta_in_child) {
    struct k_child_result r = k_child_run("wsdo_inputdesk_custom_winsta", NULL, 30000);
    EXPECT_EQ(r.exit_code, 0);
}

/* ---- NtUserCloseDesktop / CloseWindowStation direct ---------------------------------------- */
WT(nt_close_desktop_direct_closes_and_second_fails) {
    p_NtUserCloseDesktop c = NTU(NtUserCloseDesktop); REQUIRE(c); REQUIRE(U(CreateDesktopW));
    HDESK d = wd_mk_desk("ncd", DESK_FULL, FALSE, NULL); if (!d) { wd_skip_create(); return; }
    CHECK(c(d));
    DWORD fl; CHECK(!GetHandleInformation(d, &fl));
    SetLastError(0);
    CHECK(!c(d));
    EXPECT_ERR(ERROR_INVALID_HANDLE);   /* ObReferenceObjectByHandle -> STATUS_INVALID_HANDLE */
}
WT(nt_close_desktop_direct_bad_handles) {
    p_NtUserCloseDesktop c = NTU(NtUserCloseDesktop); REQUIRE(c);
    static const ULONG_PTR hs[] = { 0, 0x7777770, 0xFFFF000000000000ull };
    for (unsigned i = 0; i < 3; i++) {
        SetLastError(0);
        CKF(!c((HANDLE)hs[i]), "h=%p", (void *)hs[i]);
        CKF(GetLastError() == ERROR_INVALID_HANDLE, "h=%p gle=%lu", (void *)hs[i], GetLastError());
    }
}
WT(nt_close_desktop_direct_wrong_type) {
    p_NtUserCloseDesktop c = NTU(NtUserCloseDesktop); REQUIRE(c); REQUIRE(U(CreateWindowStationW));
    HWINSTA w = wd_mk_winsta("ncdw", WSTA_FULL, FALSE, NULL); if (!w) { wd_skip_create(); return; }
    HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    SetLastError(0); CHECK(!c(w)); CHECK(GetLastError() != 0);   /* a window station is not a desktop */
    SetLastError(0); CHECK(!c(ev)); CHECK(GetLastError() != 0);
    DWORD fl; CHECK(GetHandleInformation(w, &fl)); CHECK(GetHandleInformation(ev, &fl));   /* neither was closed */
    CloseHandle(ev); U(CloseWindowStation)(w);
}
WT(nt_close_window_station_direct_closes) {
    p_NtUserCloseWindowStation c = NTU(NtUserCloseWindowStation); REQUIRE(c); REQUIRE(U(CreateWindowStationW));
    HWINSTA w = wd_mk_winsta("ncw", WSTA_FULL, FALSE, NULL); if (!w) { wd_skip_create(); return; }
    CHECK(c(w));
    DWORD fl; CHECK(!GetHandleInformation(w, &fl));
    SetLastError(0); CHECK(!c(w));
}

/* ---- NtUserGetThreadDesktop / NtUserSetThreadDesktop direct ---------------------------------- */
WT(nt_get_thread_desktop_direct_matches_api) {
    p_NtUserGetThreadDesktop g = NTU(NtUserGetThreadDesktop); REQUIRE(g); REQUIRE(U(GetThreadDesktop));
    DWORD tid = GetCurrentThreadId();
    HDESK a = g(tid), b = U(GetThreadDesktop)(tid);
    CHECK(a != NULL); CHECKF(a == b, "%p vs %p", a, b);
    WCHAR t[256];
    if (a) { CHECK(wd_type_name(a, t, 256)); EXPECT_WSTR(t, L"Desktop"); }
}

struct dthr { HDESK d; HANDLE ready, done; DWORD tid; BOOL set_ok; DWORD gle; HDESK got; };
static DWORD WINAPI dthr_main(void *p) {
    struct dthr *t = (struct dthr *)p;
    p_NtUserSetThreadDesktop s = NTU(NtUserSetThreadDesktop);
    p_NtUserGetThreadDesktop g = NTU(NtUserGetThreadDesktop);
    t->tid = GetCurrentThreadId();
    SetLastError(0);
    t->set_ok = s ? s(t->d) : FALSE; t->gle = GetLastError();     /* thread owns no window/hook: allowed */
    t->got = g ? g(t->tid) : NULL;
    SetEvent(t->ready);
    WaitForSingleObject(t->done, 8000);
    return 0;
}
static HANDLE dthr_start(struct dthr *t, HDESK d) {
    memset(t, 0, sizeof *t); t->d = d;
    t->ready = CreateEventW(NULL, TRUE, FALSE, NULL); t->done = CreateEventW(NULL, TRUE, FALSE, NULL);
    HANDLE th = CreateThread(NULL, 0, dthr_main, t, 0, NULL);
    if (th) WaitForSingleObject(t->ready, 5000);
    return th;
}
static void dthr_stop(struct dthr *t, HANDLE th) {
    SetEvent(t->done); if (th) { WaitForSingleObject(th, 5000); CloseHandle(th); }
    CloseHandle(t->ready); CloseHandle(t->done);
}
WT(nt_set_thread_desktop_direct_assigns_and_get_returns_same_handle) {
    REQUIRE(NTU(NtUserSetThreadDesktop)); REQUIRE(NTU(NtUserGetThreadDesktop)); REQUIRE(U(CreateDesktopW));
    WCHAR dn[64]; HDESK d = wd_mk_desk("nst", DESK_FULL, FALSE, dn); if (!d) { wd_skip_create(); return; }
    struct dthr t; HANDLE th = dthr_start(&t, d);
    CKF(t.set_ok, "gle=%lu", t.gle);
    if (t.set_ok) {
        CKF(t.got == d, "got %p want %p", t.got, d);          /* _GetThreadDesktop returns pti->hdesk for a same-process thread */
        WCHAR n[64]; HDESK other = NTU(NtUserGetThreadDesktop)(t.tid);
        CKF(other == d, "from main thread: %p", other);
        if (other) { CHECK(wd_uoi_str(other, UOI_NAME_, n, 64)); EXPECT_WSTR(n, dn); }
        /* _CloseDesktop: a process thread whose desktop handle equals hDesktop -> ERROR_BUSY */
        SetLastError(0); CHECK(!NTU(NtUserCloseDesktop)(d)); EXPECT_ERR(ERROR_BUSY);
        DWORD fl; CHECK(GetHandleInformation(d, &fl));
    }
    dthr_stop(&t, th);
    SetLastError(0);
    CHECK(NTU(NtUserCloseDesktop)(d));    /* the user thread has exited */
}
WT(nt_set_thread_desktop_direct_invalid_handles) {
    p_NtUserSetThreadDesktop s = NTU(NtUserSetThreadDesktop); REQUIRE(s);
    HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    static const ULONG_PTR hs[] = { 0x7777770, 0xFFFF000000000000ull };
    for (unsigned i = 0; i < 2; i++) { SetLastError(0); CKF(!s((HANDLE)hs[i]), "h=%p", (void *)hs[i]); CKF(GetLastError() != 0, "gle h=%p", (void *)hs[i]); }
    SetLastError(0); CHECK(!s(ev)); CHECK(GetLastError() != 0);
    CloseHandle(ev);
}
/* ValidateHdesk rejects a window station handle (wrong type) */
WT(nt_set_thread_desktop_direct_rejects_winsta_handle) {
    p_NtUserSetThreadDesktop s = NTU(NtUserSetThreadDesktop); REQUIRE(s); REQUIRE(U(CreateWindowStationW));
    HWINSTA w = wd_mk_winsta("nsw", WSTA_FULL, FALSE, NULL); if (!w) { wd_skip_create(); return; }
    SetLastError(0); CHECK(!s((HANDLE)w)); CHECK(GetLastError() != 0);
    U(CloseWindowStation)(w);
}
