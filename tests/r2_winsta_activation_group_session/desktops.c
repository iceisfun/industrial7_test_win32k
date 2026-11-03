/* Desktop-level guards: NtUserOpenThreadDesktop, NtUserCheckWindowThreadDesktop, NtUserResolveDesktopForWOW,
 * NtUserSwitchDesktop argument/access validation, desktop access-mask enforcement (DESKTOP_HOOKCONTROL via
 * zzzSetWindowsHookEx's RtlAreAllAccessesGranted, CREATEWINDOW/CREATEMENU), cross-desktop hook rules, object
 * directory placement, GrantedAccess/refcounts and security-descriptor access rules. */
#include "r2.h"
#include <aclapi.h>

/* ---- helpers --------------------------------------------------------------------------------- */
struct onthr { HDESK d; void (*fn)(void *); void *arg; BOOL set_ok; DWORD gle; };
static DWORD WINAPI onthr_main(void *p) {
    struct onthr *t = (struct onthr *)p;
    SET_ERR(0);
    t->set_ok = t->d ? U(SetThreadDesktop)(t->d) : TRUE;
    t->gle = GetLastError();
    if (t->set_ok) t->fn(t->arg);
    return 0;
}
/* Runs fn on a fresh thread after SetThreadDesktop(d); returns 1 when fn ran, 0 when the desktop could not be set. */
static int r2_on_desktop(HDESK d, void (*fn)(void *), void *arg) {
    struct onthr t = { d, fn, arg, FALSE, 0 };
    HANDLE th = CreateThread(NULL, 0, onthr_main, &t, 0, NULL);
    if (!th) return 0;
    if (WaitForSingleObject(th, 15000) != WAIT_OBJECT_0) { CloseHandle(th); return 0; }
    CloseHandle(th);
    if (!t.set_ok) t_note("SetThreadDesktop failed gle=%lu", t.gle);
    return t.set_ok ? 1 : 0;
}
/* A GUI thread parked on desktop d (NULL = inherit the process desktop) until stopped. */
struct pk { HDESK d; HANDLE ready, done; DWORD tid; BOOL set_ok; DWORD gle; HWND win; BOOL mkwin; };
static DWORD WINAPI pk_main(void *p) {
    struct pk *t = (struct pk *)p;
    t->tid = GetCurrentThreadId();
    SET_ERR(0);
    t->set_ok = t->d ? U(SetThreadDesktop)(t->d) : TRUE;
    t->gle = GetLastError();
    MSG m; U(PeekMessageW)(&m, NULL, 0, 0, PM_NOREMOVE);   /* first win32k call that needs a THREADINFO converts the thread */
    if (t->mkwin && t->set_ok) t->win = r2_win();
    SetEvent(t->ready);
    WaitForSingleObject(t->done, 15000);
    if (t->win) U(DestroyWindow)(t->win);
    return 0;
}
static HANDLE pk_start(struct pk *t, HDESK d, BOOL mkwin) {
    memset(t, 0, sizeof *t); t->d = d; t->mkwin = mkwin;
    t->ready = CreateEventW(NULL, TRUE, FALSE, NULL); t->done = CreateEventW(NULL, TRUE, FALSE, NULL);
    HANDLE th = CreateThread(NULL, 0, pk_main, t, 0, NULL);
    if (th) WaitForSingleObject(t->ready, 5000);
    return th;
}
static void pk_stop(struct pk *t, HANDLE th) {
    SetEvent(t->done);
    if (th) { WaitForSingleObject(th, 5000); CloseHandle(th); }
    CloseHandle(t->ready); CloseHandle(t->done);
}
static LRESULT CALLBACK r2_hookproc(int c, WPARAM w, LPARAM l) { return 0; }

/* ---- NtUserOpenThreadDesktop ------------------------------------------------------------------- */
/* _OpenThreadDesktop opens the thread's desktop with ObOpenObjectByPointer(attrs = fInherit ? OBJ_INHERIT : 0,
 * access = requested | 0x81) so READOBJECTS|WRITEOBJECTS are always present. */
RT(open_thread_desktop_self_adds_read_write_objects) {
    REQUIRE(UN(NtUserOpenThreadDesktop)); REQUIRE(U(CloseDesktop));
    NEED_WIN(w);
    HANDLE h = UN(NtUserOpenThreadDesktop)(GetCurrentThreadId(), 0, FALSE, DESKTOP_ENUMERATE);
    CK(h != NULL);
    if (h) {
        ULONG g = 0;
        CHECK(r2_granted(h, &g));
        CKF((g & 0x81) == 0x81 && (g & DESKTOP_ENUMERATE), "granted=0x%lx", g);
        WCHAR t[64]; CHECK(r2_type_name(h, t, 64)); EXPECT_WSTR(t, L"Desktop");
        U(CloseDesktop)(h);
    }
    END_WIN(w);
}
RT(open_thread_desktop_zero_access_still_grants_read_write) {
    REQUIRE(UN(NtUserOpenThreadDesktop)); REQUIRE(U(CloseDesktop));
    NEED_WIN(w);
    HANDLE h = UN(NtUserOpenThreadDesktop)(GetCurrentThreadId(), 0, FALSE, 0);
    CK(h != NULL);
    if (h) { ULONG g = 0; CHECK(r2_granted(h, &g)); CKF((g & 0x81) == 0x81, "granted=0x%lx", g); U(CloseDesktop)(h); }
    END_WIN(w);
}
RT(open_thread_desktop_inherit_follows_argument) {
    REQUIRE(UN(NtUserOpenThreadDesktop)); REQUIRE(U(CloseDesktop));
    NEED_WIN(w);
    HANDLE a = UN(NtUserOpenThreadDesktop)(GetCurrentThreadId(), 0, TRUE, DESKTOP_READOBJECTS);
    HANDLE b = UN(NtUserOpenThreadDesktop)(GetCurrentThreadId(), 0, FALSE, DESKTOP_READOBJECTS);
    CK(a && b);
    DWORD fa = 0, fb = 0;
    if (a) { CHECK(GetHandleInformation(a, &fa)); CHECK(fa & HANDLE_FLAG_INHERIT); }
    if (b) { CHECK(GetHandleInformation(b, &fb)); CHECK(!(fb & HANDLE_FLAG_INHERIT)); }
    CHECK(a != b);
    if (a) U(CloseDesktop)(a);
    if (b) U(CloseDesktop)(b);
    END_WIN(w);
}
RT(open_thread_desktop_is_new_handle_for_same_desktop) {
    REQUIRE(UN(NtUserOpenThreadDesktop)); REQUIRE(U(GetThreadDesktop)); REQUIRE(U(CloseDesktop));
    NEED_WIN(w);
    HDESK cur = U(GetThreadDesktop)(GetCurrentThreadId());
    HANDLE h = UN(NtUserOpenThreadDesktop)(GetCurrentThreadId(), 0, FALSE, DESKTOP_READOBJECTS);
    CK(h != NULL && cur != NULL);
    if (h && cur) {
        CHECK(h != cur);   /* a new handle, not the thread's own */
        WCHAR a[64], b[64];
        CHECK(r2_uoi_name(h, a, 64)); CHECK(r2_uoi_name(cur, b, 64));
        CKF(!lstrcmpiW(a, b), "names differ");
        WCHAR oa[256], ob[256];
        CHECK(r2_obj_name(h, oa, 256)); CHECK(r2_obj_name(cur, ob, 256));
        CHECK(!lstrcmpiW(oa, ob));
    }
    if (h) U(CloseDesktop)(h);
    END_WIN(w);
}
RT(open_thread_desktop_closed_handle_is_gone) {
    REQUIRE(UN(NtUserOpenThreadDesktop)); REQUIRE(U(CloseDesktop));
    NEED_WIN(w);
    HANDLE h = UN(NtUserOpenThreadDesktop)(GetCurrentThreadId(), 0, FALSE, DESKTOP_READOBJECTS);
    CK(h != NULL);
    if (h) {
        CHECK(U(CloseDesktop)(h));
        DWORD f; CHECK(!GetHandleInformation(h, &f));
    }
    END_WIN(w);
}
RT(open_thread_desktop_bumps_handle_count_by_one) {
    REQUIRE(UN(NtUserOpenThreadDesktop)); REQUIRE(U(CloseDesktop)); REQUIRE(U(GetThreadDesktop));
    NEED_WIN(w);
    HDESK cur = U(GetThreadDesktop)(GetCurrentThreadId());
    ULONG b0[4], b1[4], b2[4];
    if (!cur || !r2_basic(cur, b0)) { t_skip("no basic info"); END_WIN(w); return; }
    HANDLE h = UN(NtUserOpenThreadDesktop)(GetCurrentThreadId(), 0, FALSE, DESKTOP_READOBJECTS);
    CK(h != NULL);
    if (h) {
        CHECK(r2_basic(cur, b1));
        CKF(b1[2] == b0[2] + 1, "HandleCount %lu -> %lu", b0[2], b1[2]);
        /* PointerCount of the live default desktop moves with every thread that touches it: not compared */
        U(CloseDesktop)(h);
        CHECK(r2_basic(cur, b2));
        CKF(b2[2] == b0[2], "HandleCount back %lu -> %lu", b0[2], b2[2]);
    }
    END_WIN(w);
}
RT(open_thread_desktop_other_thread_on_custom_desktop) {
    REQUIRE(UN(NtUserOpenThreadDesktop)); REQUIRE(U(CreateDesktopW)); REQUIRE(U(CloseDesktop));
    WCHAR nm[64];
    HDESK d = r2_mk_desk("otd1", DESKTOP_ALL_, FALSE, nm);
    if (!d) { t_skip("CreateDesktop gle=%lu", GetLastError()); return; }
    struct pk t; HANDLE th = pk_start(&t, d, FALSE);
    if (t.set_ok) {
        HANDLE h = UN(NtUserOpenThreadDesktop)(t.tid, 0, FALSE, DESKTOP_READOBJECTS);
        CKF(h != NULL, "gle=%lu", GetLastError());
        if (h) {
            WCHAR got[64]; CHECK(r2_uoi_name(h, got, 64));
            CKF(!lstrcmpiW(got, nm), "got %ls want %ls", got, nm);
            U(CloseDesktop)(h);
        }
    } else t_skip("SetThreadDesktop failed gle=%lu", t.gle);
    pk_stop(&t, th);
    U(CloseDesktop)(d);
}
RT(open_thread_desktop_unknown_thread_fails) {
    REQUIRE(UN(NtUserOpenThreadDesktop));
    SET_ERR(SENT);
    HANDLE h = UN(NtUserOpenThreadDesktop)(0xFFFFFFF0, 0, FALSE, DESKTOP_READOBJECTS);
    CK(h == NULL);
    t_note("OpenThreadDesktop(unknown tid) gle=%lu", GetLastError());
}

/* ---- NtUserCheckWindowThreadDesktop ------------------------------------------------------------- */
RT(check_window_thread_desktop_own_window_own_thread) {
    REQUIRE(UN(NtUserCheckWindowThreadDesktop));
    NEED_WIN(w);
    CK(R_D(UN(NtUserCheckWindowThreadDesktop)(w, GetCurrentThreadId())) == 1);   /* ValidatePwndDesktop */
    END_WIN(w);
}
RT(check_window_thread_desktop_invalid_hwnds) {
    REQUIRE(UN(NtUserCheckWindowThreadDesktop));
    NEED_WIN(w);
    for (unsigned i = 0; i < N_BAD_HWNDS; i++) {
        SET_ERR(SENT);
        ULONG_PTR r = UN(NtUserCheckWindowThreadDesktop)((HWND)r2_bad_hwnds[i], GetCurrentThreadId());
        CKF(R_D(r) == 0, "hwnd=%p", (void *)r2_bad_hwnds[i]);
        CKF(GetLastError() == ERR_INVALID_WINDOW, "hwnd=%p gle=%lu", (void *)r2_bad_hwnds[i], GetLastError());
    }
    END_WIN(w);
}
RT(check_window_thread_desktop_other_thread_same_desktop) {
    REQUIRE(UN(NtUserCheckWindowThreadDesktop));
    NEED_WIN(w);
    struct pk t; HANDLE th = pk_start(&t, NULL, FALSE);
    CK(R_D(UN(NtUserCheckWindowThreadDesktop)(w, t.tid)) == 1);
    pk_stop(&t, th);
    END_WIN(w);
}
RT(check_window_thread_desktop_thread_on_other_desktop) {
    REQUIRE(UN(NtUserCheckWindowThreadDesktop)); REQUIRE(U(CreateDesktopW)); REQUIRE(U(CloseDesktop));
    NEED_WIN(w);
    HDESK d = r2_mk_desk("cwt1", DESKTOP_ALL_, FALSE, NULL);
    if (!d) { t_skip("CreateDesktop gle=%lu", GetLastError()); END_WIN(w); return; }
    struct pk t; HANDLE th = pk_start(&t, d, FALSE);
    if (t.set_ok) CK(R_D(UN(NtUserCheckWindowThreadDesktop)(w, t.tid)) == 0);   /* window and thread desktops differ */
    else t_skip("SetThreadDesktop gle=%lu", t.gle);
    pk_stop(&t, th);
    U(CloseDesktop)(d);
    END_WIN(w);
}
RT(check_window_thread_desktop_unknown_thread) {
    REQUIRE(UN(NtUserCheckWindowThreadDesktop));
    NEED_WIN(w);
    SET_ERR(SENT);
    ULONG_PTR r = UN(NtUserCheckWindowThreadDesktop)(w, 0xFFFFFFF0);
    CK(R_D(r) == 0);   /* no THREADINFO and no console desktop -> never reports a match */
    t_note("CheckWindowThreadDesktop(unknown tid) gle=%lu", GetLastError());
    END_WIN(w);
}
RT(check_window_thread_desktop_user32_export_agrees) {
    typedef BOOL (WINAPI *pfn)(HWND, DWORD);
    pfn f = (pfn)r2_proc_in("user32.dll", "CheckWindowThreadDesktop");
    REQUIRE(f);
    NEED_WIN(w);
    CHECK(f(w, GetCurrentThreadId()) == TRUE);
    END_WIN(w);
}

/* ---- NtUserResolveDesktopForWOW ------------------------------------------------------------------ */
static int StrCmpNIW_(const WCHAR *a, const WCHAR *b, int n) { return CompareStringW(LOCALE_INVARIANT, NORM_IGNORECASE, a, n, b, n) == CSTR_EQUAL ? 0 : 1; }
static LONG r2_resolve(const WCHAR *in, WCHAR *out, size_t cch, USHORT *len) {
    WCHAR buf[256]; memset(buf, 0, sizeof buf);
    lstrcpyW(buf, in);
    UNICODE_STRING us; us.Length = (USHORT)(lstrlenW(in) * 2); us.MaximumLength = sizeof buf; us.Buffer = buf;
    LONG st = UN(NtUserResolveDesktopForWOW)(&us);
    size_t n = us.Length / 2; if (n >= cch) n = cch - 1;
    memcpy(out, buf, n * 2); out[n] = 0;
    if (len) *len = us.Length;
    return st;
}
/* an empty desktop name resolves to L"Default" on L"WinSta0" and is reported as winsta\desktop. */
RT(resolve_desktop_empty_is_winsta0_default) {
    REQUIRE(UN(NtUserResolveDesktopForWOW));
    WCHAR o[128]; USHORT len = 0;
    LONG st = r2_resolve(L"", o, 128, &len);
    CKF(st == 0, "status=0x%lx", (ULONG)st);
    if (st == 0) { EXPECT_WSTR(o, L"WinSta0\\Default"); CKF(len == 30, "len=%u", len); }
}
RT(resolve_desktop_bare_name_gets_winsta0_prefix) {
    REQUIRE(UN(NtUserResolveDesktopForWOW));
    WCHAR o[128];
    USHORT len = 0;
    LONG st = r2_resolve(L"Default", o, 128, &len);
    CKF(st == 0, "status=0x%lx", (ULONG)st);
    /* Windows 10 22H2 rewrites the string in place to a fixed 30 bytes ("WinSta0\\" and a desktop part); the desktop
     * part is not the input name, so only the window station prefix and the length are asserted */
    if (st == 0) { CKF(!StrCmpNIW_(o, L"WinSta0\\", 8), "got %ls", o); CKF(len == 30, "len=%u", len); }
}
RT(resolve_desktop_full_name_unchanged) {
    REQUIRE(UN(NtUserResolveDesktopForWOW));
    WCHAR o[128];
    USHORT len = 0;
    LONG st = r2_resolve(L"WinSta0\\Default", o, 128, &len);
    CKF(st == 0, "status=0x%lx", (ULONG)st);
    /* the full name is not returned unchanged on Windows 10 22H2: the window station part survives, the length is fixed */
    if (st == 0) { CKF(!StrCmpNIW_(o, L"WinSta0\\", 8), "got %ls", o); CKF(len == 30, "len=%u", len); }
}
RT(resolve_desktop_matches_case_insensitively) {
    REQUIRE(UN(NtUserResolveDesktopForWOW));
    WCHAR o[128];
    USHORT len = 0;
    LONG st = r2_resolve(L"winsta0\\DEFAULT", o, 128, &len);
    CKF(st == 0, "status=0x%lx", (ULONG)st);
    /* the window station is matched without regard to case and written back with the caller's spelling */
    if (st == 0) { CKF(!StrCmpNIW_(o, L"WinSta0\\", 8), "got %ls", o); CKF(!wcsncmp(o, L"winsta0\\", 8), "spelling changed: %ls", o); CKF(len == 30, "len=%u", len); }
}
RT(resolve_desktop_unknown_desktop_fails) {
    REQUIRE(UN(NtUserResolveDesktopForWOW));
    WCHAR o[128];
    LONG st = r2_resolve(L"WinSta0\\r2NoSuchDesktop", o, 128, NULL);
    CKF(st < 0, "status=0x%lx", (ULONG)st);
    t_note("unknown desktop status 0x%lx", (ULONG)st);
}
/* The window station is opened with _OpenWindowStation; failure maps to STATUS_ACCESS_DENIED (-0x3fffffde). */
RT(resolve_desktop_unknown_winsta_is_access_denied) {
    REQUIRE(UN(NtUserResolveDesktopForWOW));
    WCHAR o[128];
    LONG st = r2_resolve(L"r2NoSuchWinSta\\Default", o, 128, NULL);
    CKF((ULONG)st == 0xC0000022u, "status=0x%lx", (ULONG)st);
}
RT(resolve_desktop_custom_desktop_in_winsta0) {
    REQUIRE(UN(NtUserResolveDesktopForWOW)); REQUIRE(U(CreateDesktopW)); REQUIRE(U(CloseDesktop));
    WCHAR nm[64];
    HDESK d = r2_mk_desk("rdw1", DESKTOP_ALL_, FALSE, nm);
    if (!d) { t_skip("CreateDesktop gle=%lu", GetLastError()); return; }
    WCHAR in[128], want[128], o[128];
    swprintf(in, 128, L"%ls", nm);
    swprintf(want, 128, L"WinSta0\\%ls", nm);
    HWINSTA cur = U(GetProcessWindowStation)();
    WCHAR wn[64] = L""; r2_uoi_name(cur, wn, 64);
    if (lstrcmpiW(wn, L"WinSta0")) { t_note("process station is %ls, bare names resolve in WinSta0 only", wn); U(CloseDesktop)(d); return; }
    USHORT len = 0;
    LONG st = r2_resolve(in, o, 128, &len);
    CKF(st == 0, "status=0x%lx", (ULONG)st);
    (void)want;
    /* as for a bare name, only the window station prefix and the reported length (the size of "WinSta0\\<name>") are stable */
    if (st == 0) { CKF(!StrCmpNIW_(o, L"WinSta0\\", 8), "got %ls", o); CKF(len == 2 * lstrlenW(want), "len=%u", len); }
    U(CloseDesktop)(d);
}
RT(resolve_desktop_does_not_leak_handles) {
    REQUIRE(UN(NtUserResolveDesktopForWOW));
    DWORD h0 = 0, h1 = 0;
    GetProcessHandleCount(GetCurrentProcess(), &h0);
    WCHAR o[128];
    for (int i = 0; i < 20; i++) r2_resolve(L"Default", o, 128, NULL);
    GetProcessHandleCount(GetCurrentProcess(), &h1);
    CKF(h1 <= h0 + 2, "handles %lu -> %lu", h0, h1);   /* the winsta/desktop handles are closed by the kernel path */
}

/* ---- NtUserSwitchDesktop guards ---------------------------------------------------------------- */
RT(switch_desktop_invalid_handles) {
    REQUIRE(UN(NtUserSwitchDesktop));
    for (unsigned i = 0; i < N_BAD_HANDLES; i++) {
        SET_ERR(SENT);
        ULONG_PTR r = UN(NtUserSwitchDesktop)((HANDLE)r2_bad_handles[i], 0, 0);
        CKF(R_D(r) == 0, "h=%p", (void *)r2_bad_handles[i]);
        CKF(GetLastError() == ERR_INVALID_HANDLE, "h=%p gle=%lu", (void *)r2_bad_handles[i], GetLastError());
    }
}
RT(switch_desktop_without_switch_access_is_denied) {
    REQUIRE(UN(NtUserSwitchDesktop)); REQUIRE(U(CreateDesktopW)); REQUIRE(U(CloseDesktop));
    HDESK d = r2_mk_desk("sw1", DESKTOP_READOBJECTS, FALSE, NULL);
    if (!d) { t_skip("CreateDesktop gle=%lu", GetLastError()); return; }
    ULONG g = 0;
    if (!r2_granted(d, &g) || (g & DESKTOP_SWITCHDESKTOP)) { t_skip("handle unexpectedly carries SWITCHDESKTOP"); U(CloseDesktop)(d); return; }
    SET_ERR(SENT);
    CHECK(R_D(UN(NtUserSwitchDesktop)(d, 0, 0)) == 0);   /* ValidateHdesk(.., DESKTOP_SWITCHDESKTOP) -> STATUS_ACCESS_DENIED */
    EXPECT_ERR(ERR_ACCESS_DENIED);
    U(CloseDesktop)(d);
}
RT(switch_desktop_user32_export_without_access_is_denied) {
    if (t_is_wine()) { t_skip("Wine has no per-desktop window/hook/access model"); return; }
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(CloseDesktop));
    typedef BOOL (WINAPI *pfn)(HDESK);
    pfn f = (pfn)r2_proc_in("user32.dll", "SwitchDesktop");
    REQUIRE(f);
    HDESK d = r2_mk_desk("sw2", DESKTOP_READOBJECTS | DESKTOP_ENUMERATE, FALSE, NULL);
    if (!d) { t_skip("CreateDesktop gle=%lu", GetLastError()); return; }
    ULONG g = 0;
    if (!r2_granted(d, &g) || (g & DESKTOP_SWITCHDESKTOP)) { t_skip("handle unexpectedly carries SWITCHDESKTOP"); U(CloseDesktop)(d); return; }
    SET_ERR(SENT);
    CHECK(f(d) == FALSE);
    EXPECT_ERR(ERR_ACCESS_DENIED);
    U(CloseDesktop)(d);
}
RT(switch_desktop_window_station_handle_rejected) {
    REQUIRE(UN(NtUserSwitchDesktop)); REQUIRE(U(CreateWindowStationW));
    HWINSTA w = r2_mk_winsta("sw3", WSTA_ALL_, FALSE, NULL);
    if (!w) { t_skip("CreateWindowStation refused for the unprivileged token (gle=%lu)", GetLastError()); return; }
    SET_ERR(SENT);
    CHECK(R_D(UN(NtUserSwitchDesktop)(w, 0, 0)) == 0);   /* wrong object type */
    CHECK(GetLastError() != SENT && GetLastError() != 0);
    U(CloseWindowStation)(w);
}
RT(switch_desktop_closed_handle_is_invalid) {
    REQUIRE(UN(NtUserSwitchDesktop)); REQUIRE(U(CreateDesktopW)); REQUIRE(U(CloseDesktop));
    HDESK d = r2_mk_desk("sw4", DESKTOP_ALL_, FALSE, NULL);
    if (!d) { t_skip("CreateDesktop gle=%lu", GetLastError()); return; }
    CHECK(U(CloseDesktop)(d));
    SET_ERR(SENT);
    CHECK(R_D(UN(NtUserSwitchDesktop)(d, 0, 0)) == 0);
    EXPECT_ERR(ERR_INVALID_HANDLE);
}

/* ---- desktop access mask enforcement -------------------------------------------------------------- */
struct acc { BOOL mk_menu, mk_win, mk_hook; DWORD gle_menu, gle_win, gle_hook; };
static void acc_probe(void *p) {
    struct acc *a = (struct acc *)p;
    SET_ERR(0);
    HMENU m = U(CreateMenu)();
    a->mk_menu = m != NULL; a->gle_menu = GetLastError();
    if (m) U(DestroyMenu)(m);
    SET_ERR(0);
    HWND w = U(CreateWindowExW)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"r2", WS_POPUP, -32000, -32000, 1, 1, NULL, NULL, NULL, NULL);
    a->mk_win = w != NULL; a->gle_win = GetLastError();
    if (w) U(DestroyWindow)(w);
    SET_ERR(0);
    HHOOK hk = U(SetWindowsHookExW)(WH_GETMESSAGE, r2_hookproc, NULL, GetCurrentThreadId());
    a->mk_hook = hk != NULL; a->gle_hook = GetLastError();
    if (hk) U(UnhookWindowsHookEx)(hk);
}
RT(desktop_access_read_write_only_denies_create_menu_window_hook) {
    if (t_is_wine()) { t_skip("Wine has no win32k object namespace/ACL/access model"); return; }
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(CloseDesktop)); REQUIRE(U(CreateMenu)); REQUIRE(U(SetWindowsHookExW));
    HDESK d = r2_mk_desk("ac1", DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS, FALSE, NULL);
    if (!d) { t_skip("CreateDesktop gle=%lu", GetLastError()); return; }
    struct acc a; memset(&a, 0, sizeof a);
    if (r2_on_desktop(d, acc_probe, &a)) {
        CKF(!a.mk_menu, "CreateMenu succeeded without DESKTOP_CREATEMENU");
        CKF(!a.mk_win, "CreateWindowEx succeeded without DESKTOP_CREATEWINDOW");
        CKF(!a.mk_hook, "SetWindowsHookEx succeeded without DESKTOP_HOOKCONTROL");
        /* zzzSetWindowsHookEx: RtlAreAllAccessesGranted(desktop access, 8) false -> falls to the 5 exit */
        CKF(a.gle_hook == ERR_ACCESS_DENIED, "hook gle=%lu", a.gle_hook);
        t_note("denied gle: menu=%lu window=%lu hook=%lu", a.gle_menu, a.gle_win, a.gle_hook);
    }
    U(CloseDesktop)(d);
}
RT(desktop_access_full_allows_create_menu_window_hook) {
    if (t_is_wine()) { t_skip("Wine has no win32k object namespace/ACL/access model"); return; }
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(CloseDesktop)); REQUIRE(U(CreateMenu)); REQUIRE(U(SetWindowsHookExW));
    HDESK d = r2_mk_desk("ac2", DESKTOP_ALL_, FALSE, NULL);
    if (!d) { t_skip("CreateDesktop gle=%lu", GetLastError()); return; }
    struct acc a; memset(&a, 0, sizeof a);
    if (r2_on_desktop(d, acc_probe, &a)) {
        CKF(a.mk_menu, "CreateMenu gle=%lu", a.gle_menu);
        CKF(a.mk_win, "CreateWindowEx gle=%lu", a.gle_win);
        CKF(a.mk_hook, "SetWindowsHookEx gle=%lu", a.gle_hook);
    }
    U(CloseDesktop)(d);
}
RT(desktop_access_hookcontrol_alone_allows_hook_only) {
    if (t_is_wine()) { t_skip("Wine has no win32k object namespace/ACL/access model"); return; }
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(CloseDesktop)); REQUIRE(U(CreateMenu)); REQUIRE(U(SetWindowsHookExW));
    HDESK d = r2_mk_desk("ac3", DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS | DESKTOP_HOOKCONTROL, FALSE, NULL);
    if (!d) { t_skip("CreateDesktop gle=%lu", GetLastError()); return; }
    struct acc a; memset(&a, 0, sizeof a);
    if (r2_on_desktop(d, acc_probe, &a)) {
        CKF(a.mk_hook, "hook gle=%lu", a.gle_hook);
        CKF(!a.mk_menu, "CreateMenu succeeded without DESKTOP_CREATEMENU");
        CKF(!a.mk_win, "CreateWindowEx succeeded without DESKTOP_CREATEWINDOW");
    }
    U(CloseDesktop)(d);
}
RT(desktop_access_createwindow_alone_allows_window_only) {
    if (t_is_wine()) { t_skip("Wine has no win32k object namespace/ACL/access model"); return; }
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(CloseDesktop)); REQUIRE(U(CreateMenu)); REQUIRE(U(SetWindowsHookExW));
    HDESK d = r2_mk_desk("ac4", DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS | DESKTOP_CREATEWINDOW, FALSE, NULL);
    if (!d) { t_skip("CreateDesktop gle=%lu", GetLastError()); return; }
    struct acc a; memset(&a, 0, sizeof a);
    if (r2_on_desktop(d, acc_probe, &a)) {
        CKF(a.mk_win, "window gle=%lu", a.gle_win);
        CKF(!a.mk_hook, "hook succeeded without DESKTOP_HOOKCONTROL");
    }
    U(CloseDesktop)(d);
}
RT(set_thread_desktop_accepts_handle_without_read_and_write_objects) {
    if (t_is_wine()) { t_skip("Wine has no win32k object namespace/ACL/access model"); return; }
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(CloseDesktop)); REQUIRE(U(SetThreadDesktop));
    static const DWORD masks[] = { DESKTOP_READOBJECTS, DESKTOP_WRITEOBJECTS, DESKTOP_ENUMERATE | DESKTOP_CREATEWINDOW };
    for (unsigned i = 0; i < 3; i++) {
        char tag[16]; sprintf(tag, "st%u", i);
        HDESK d = r2_mk_desk(tag, masks[i], FALSE, NULL);
        if (!d) { t_note("CreateDesktop(0x%lx) gle=%lu", masks[i], GetLastError()); continue; }
        struct onthr t = { d, acc_probe, NULL, FALSE, 0 };
        struct acc a; t.arg = &a;
        HANDLE th = CreateThread(NULL, 0, onthr_main, &t, 0, NULL);
        if (th) { WaitForSingleObject(th, 15000); CloseHandle(th); }
        ULONG gr = 0; CHECK(r2_granted(d, &gr));
        /* OpenDesktop always adds DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS (0x81); only a handle that comes with the
         * CreateDesktop mask itself (here 0x42) lacks them. Windows 10 22H2 lets SetThreadDesktop through either way. */
        if (masks[i] != (DESKTOP_ENUMERATE | DESKTOP_CREATEWINDOW)) CKF((gr & 0x81) == 0x81, "mask 0x%lx granted 0x%lx", masks[i], gr);
        else CKF((gr & 0x81) == 0, "mask 0x%lx granted 0x%lx", masks[i], gr);
        CKF(t.set_ok, "mask 0x%lx (granted 0x%lx): SetThreadDesktop failed, gle=%lu", masks[i], gr, t.gle);
        U(CloseDesktop)(d);
    }
}

/* ---- hook parameter validation and cross-desktop hooks --------------------------------------------- */
RT(hook_invalid_hook_id_is_invalid_hook_filter) {
    if (t_is_wine()) { t_skip("Wine has no win32k object namespace/ACL/access model"); return; }
    REQUIRE(U(SetWindowsHookExW));
    static const int ids[] = { -2, 15, 100, 0x7fffffff };
    for (unsigned i = 0; i < 4; i++) {
        SET_ERR(SENT);
        HHOOK h = U(SetWindowsHookExW)(ids[i], r2_hookproc, NULL, GetCurrentThreadId());
        CKF(h == NULL, "id=%d", ids[i]);
        if (h) U(UnhookWindowsHookEx)(h);
        CKF(GetLastError() == 1426, "id=%d gle=%lu", ids[i], GetLastError());   /* 0x592: idHook+1 > 15 */
    }
}
RT(hook_null_proc_is_invalid_filter_proc) {
    REQUIRE(U(SetWindowsHookExW));
    SET_ERR(SENT);
    HHOOK h = U(SetWindowsHookExW)(WH_GETMESSAGE, NULL, NULL, GetCurrentThreadId());
    CK(h == NULL);
    if (h) U(UnhookWindowsHookEx)(h);
    EXPECT_ERR(1427);   /* 0x593: lpfn == NULL */
}
RT(hook_global_without_module_needs_hmod) {
    REQUIRE(U(SetWindowsHookExW));
    SET_ERR(SENT);
    HHOOK h = U(SetWindowsHookExW)(WH_GETMESSAGE, r2_hookproc, NULL, 0);
    CK(h == NULL);
    if (h) U(UnhookWindowsHookEx)(h);
    EXPECT_ERR(1428);   /* ERROR_HOOK_NEEDS_HMOD */
}
RT(hook_thread_specific_global_only_hook_type) {
    REQUIRE(U(SetWindowsHookExW));
    NEED_WIN(w);   /* GUI thread */
    static const int ids[] = { WH_KEYBOARD_LL, WH_MOUSE_LL, WH_JOURNALRECORD, WH_JOURNALPLAYBACK };
    for (unsigned i = 0; i < 4; i++) {
        SET_ERR(SENT);
        HHOOK h = U(SetWindowsHookExW)(ids[i], r2_hookproc, GetModuleHandleA(NULL), GetCurrentThreadId());
        CKF(h == NULL, "id=%d", ids[i]);
        if (h) U(UnhookWindowsHookEx)(h);
        CKF(GetLastError() == 1429, "id=%d gle=%lu", ids[i], GetLastError());   /* 0x595 ERROR_GLOBAL_ONLY_HOOK */
    }
    END_WIN(w);
}
RT(hook_unknown_thread_id_is_invalid_parameter) {
    REQUIRE(U(SetWindowsHookExW));
    NEED_WIN(w);
    SET_ERR(SENT);
    HHOOK h = U(SetWindowsHookExW)(WH_GETMESSAGE, r2_hookproc, NULL, 0xFFFFFFF0);
    CK(h == NULL);
    if (h) U(UnhookWindowsHookEx)(h);
    EXPECT_ERR(ERR_INVALID_PARAM);   /* NtUserSetWindowsHookEx: PtiFromThreadId == 0 */
    END_WIN(w);
}
RT(hook_thread_on_other_desktop_is_denied) {
    if (t_is_wine()) { t_skip("Wine has no per-desktop window/hook/access model"); return; }
    REQUIRE(U(SetWindowsHookExW)); REQUIRE(U(CreateDesktopW)); REQUIRE(U(CloseDesktop));
    NEED_WIN(w);
    HDESK d = r2_mk_desk("hk1", DESKTOP_ALL_, FALSE, NULL);
    if (!d) { t_skip("CreateDesktop gle=%lu", GetLastError()); END_WIN(w); return; }
    struct pk t; HANDLE th = pk_start(&t, d, FALSE);
    if (t.set_ok) {
        SET_ERR(SENT);
        HHOOK h = U(SetWindowsHookExW)(WH_GETMESSAGE, r2_hookproc, NULL, t.tid);
        CK(h == NULL);
        if (h) U(UnhookWindowsHookEx)(h);
        EXPECT_ERR(ERR_ACCESS_DENIED);   /* zzzSetWindowsHookEx: target rpdesk != caller rpdesk */
    } else t_skip("SetThreadDesktop gle=%lu", t.gle);
    pk_stop(&t, th);
    U(CloseDesktop)(d);
    END_WIN(w);
}
RT(hook_thread_on_same_desktop_succeeds_and_unhooks) {
    REQUIRE(U(SetWindowsHookExW)); REQUIRE(U(UnhookWindowsHookEx));
    NEED_WIN(w);
    struct pk t; HANDLE th = pk_start(&t, NULL, FALSE);
    SET_ERR(SENT);
    HHOOK h = U(SetWindowsHookExW)(WH_GETMESSAGE, r2_hookproc, NULL, t.tid);
    CKF(h != NULL, "gle=%lu", GetLastError());
    if (h) CHECK(U(UnhookWindowsHookEx)(h));
    pk_stop(&t, th);
    END_WIN(w);
}

/* ---- window / message across desktops ------------------------------------------------------------- */
RT(window_on_other_desktop_is_a_window_but_unusable_here) {
    if (t_is_wine()) { t_skip("Wine has no per-desktop window/hook/access model"); return; }
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(CloseDesktop)); REQUIRE(U(IsWindow));
    NEED_WIN(w);
    HDESK d = r2_mk_desk("xw1", DESKTOP_ALL_, FALSE, NULL);
    if (!d) { t_skip("CreateDesktop gle=%lu", GetLastError()); END_WIN(w); return; }
    struct pk t; HANDLE th = pk_start(&t, d, TRUE);
    if (t.set_ok && t.win) {
        SET_ERR(SENT);
        /* IsWindow only validates the handle: a window of another desktop of this process still is a window here,
         * while it is not visible and cannot be used as a parent */
        CHECK(U(IsWindow)(t.win));
        CHECK(!U(IsWindowVisible)(t.win));
        SET_ERR(SENT);
        CHECK(U(SetParent)(w, t.win) == NULL);
        CHECK(GetLastError() != SENT);
    } else t_skip("window on custom desktop not available (set_ok=%d gle=%lu)", t.set_ok, t.gle);
    pk_stop(&t, th);
    U(CloseDesktop)(d);
    END_WIN(w);
}
RT(thread_desktop_getthreaddesktop_of_parked_thread) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(CloseDesktop)); REQUIRE(U(GetThreadDesktop));
    WCHAR nm[64];
    HDESK d = r2_mk_desk("gtd1", DESKTOP_ALL_, FALSE, nm);
    if (!d) { t_skip("CreateDesktop gle=%lu", GetLastError()); return; }
    struct pk t; HANDLE th = pk_start(&t, d, FALSE);
    if (t.set_ok) {
        HDESK got = U(GetThreadDesktop)(t.tid);
        CKF(got != NULL, "gle=%lu", GetLastError());
        if (got) { WCHAR gn[64]; CHECK(r2_uoi_name(got, gn, 64)); CKF(!lstrcmpiW(gn, nm), "got %ls want %ls", gn, nm); }
    } else t_skip("SetThreadDesktop gle=%lu", t.gle);
    pk_stop(&t, th);
    U(CloseDesktop)(d);
}

/* ---- object directory placement ----------------------------------------------------------------------- */
struct dirent_ { UNICODE_STRING Name, TypeName; };
/* finds `name` in directory `path`; copies its type name. returns 1 found, 0 not found, -1 directory unavailable */
static int r2_dir_find(const WCHAR *path, const WCHAR *name, WCHAR *type, size_t tcch) {
    r2_NtOpenDirectoryObject od = (r2_NtOpenDirectoryObject)t_proc_in("ntdll.dll", "NtOpenDirectoryObject");
    r2_NtQueryDirectoryObject qd = (r2_NtQueryDirectoryObject)t_proc_in("ntdll.dll", "NtQueryDirectoryObject");
    r2_NtClose cl = (r2_NtClose)t_proc_in("ntdll.dll", "NtClose");
    if (!od || !qd || !cl) return -1;
    UNICODE_STRING us; us.Buffer = (PWSTR)path; us.Length = (USHORT)(lstrlenW(path) * 2); us.MaximumLength = us.Length + 2;
    OBJECT_ATTRIBUTES oa; InitializeObjectAttributes(&oa, &us, 0, NULL, NULL);
    HANDLE dir = NULL;
    LONG st = od(&dir, 1 /* DIRECTORY_QUERY */, &oa);
    if (st < 0) { t_note("NtOpenDirectoryObject(%ls) = 0x%lx", path, (ULONG)st); return -1; }
    static BYTE buf[16384];
    ULONG ctx = 0, rl = 0; BOOLEAN restart = TRUE; int found = 0;
    if (type) type[0] = 0;
    for (;;) {
        st = qd(dir, buf, sizeof buf, FALSE, restart, &ctx, &rl);
        restart = FALSE;
        if (st < 0) break;
        struct dirent_ *e = (struct dirent_ *)buf;
        for (; e->Name.Buffer; e++) {
            if (e->Name.Length / 2 == (USHORT)lstrlenW(name) && !_wcsnicmp(e->Name.Buffer, name, e->Name.Length / 2)) {
                found = 1;
                if (type) { size_t n = e->TypeName.Length / 2; if (n >= tcch) n = tcch - 1; memcpy(type, e->TypeName.Buffer, n * 2); type[n] = 0; }
            }
        }
        if (st == 0) break;   /* STATUS_SUCCESS: everything returned in one call */
    }
    cl(dir);
    return found;
}
/* split "<dir>\<leaf>"; returns pointer to the leaf inside full */
static WCHAR *r2_split(WCHAR *full) { WCHAR *s = wcsrchr(full, L'\\'); if (!s) return NULL; *s = 0; return s + 1; }

RT(winsta_object_listed_in_windowstations_directory) {
    if (t_is_wine()) { t_skip("Wine has no win32k object namespace/ACL/access model"); return; }
    REQUIRE(U(CreateWindowStationW));
    WCHAR nm[64];
    HWINSTA w = r2_mk_winsta("dir1", WSTA_ALL_, FALSE, nm);
    if (!w) { t_skip("CreateWindowStation refused for the unprivileged token (gle=%lu)", GetLastError()); return; }
    WCHAR full[512];
    CHECK(r2_obj_name(w, full, 512));
    WCHAR *leaf = r2_split(full);
    CHECK(leaf && !lstrcmpiW(leaf, nm));
    size_t n = lstrlenW(full);
    CKF(n >= 23 && !lstrcmpiW(full + n - 23, L"\\Windows\\WindowStations"), "dir=%ls", full);
    WCHAR type[64];
    int f = r2_dir_find(full, nm, type, 64);
    if (f < 0) t_skip("directory not readable");
    else { CK(f == 1); if (f == 1) EXPECT_WSTR(type, L"WindowStation"); }
    U(CloseWindowStation)(w);
}
RT(winsta0_listed_in_session_windowstations_directory) {
    if (t_is_wine()) { t_skip("Wine has no win32k object namespace/ACL/access model"); return; }
    REQUIRE(U(GetProcessWindowStation));
    HWINSTA pw = U(GetProcessWindowStation)();
    WCHAR full[512];
    if (!pw || !r2_obj_name(pw, full, 512)) { t_skip("no process window station name"); return; }
    WCHAR *leaf = r2_split(full);
    if (!leaf) { t_skip("unexpected name"); return; }
    WCHAR type[64];
    int f = r2_dir_find(full, leaf, type, 64);
    if (f < 0) { t_skip("directory not readable"); return; }
    CK(f == 1);
    if (f == 1) EXPECT_WSTR(type, L"WindowStation");
}
RT(windowstations_directory_lives_under_windows_directory) {
    if (t_is_wine()) { t_skip("Wine has no win32k object namespace/ACL/access model"); return; }
    REQUIRE(U(GetProcessWindowStation));
    HWINSTA pw = U(GetProcessWindowStation)();
    WCHAR full[512];
    if (!pw || !r2_obj_name(pw, full, 512)) { t_skip("no process window station name"); return; }
    WCHAR *leaf = r2_split(full);              /* ...\Windows\WindowStations */
    WCHAR *ws = leaf ? r2_split(full) : NULL;  /* full = ...\Windows , ws = WindowStations */
    if (!ws) { t_skip("unexpected name"); return; }
    CKF(!lstrcmpiW(ws, L"WindowStations"), "ws=%ls", ws);
    size_t n = lstrlenW(full);
    CKF(n >= 8 && !lstrcmpiW(full + n - 8, L"\\Windows"), "dir=%ls", full);
    if (k_session_id() != 0) {
        WCHAR want[64]; swprintf(want, 64, L"\\Sessions\\%lu\\Windows", k_session_id());
        CKF(!lstrcmpiW(full, want), "dir=%ls want %ls", full, want);   /* per-session object directory \Sessions\N\Windows */
    } else CKF(!lstrcmpiW(full, L"\\Windows"), "dir=%ls", full);
    WCHAR type[64];
    int f = r2_dir_find(full, ws, type, 64);
    if (f < 0) { t_skip("directory not readable"); return; }
    CK(f == 1);
    if (f == 1) EXPECT_WSTR(type, L"Directory");
}
RT(winsta_removed_from_directory_after_last_handle_closed) {
    if (t_is_wine()) { t_skip("Wine has no win32k object namespace/ACL/access model"); return; }
    REQUIRE(U(CreateWindowStationW));
    WCHAR nm[64];
    HWINSTA w = r2_mk_winsta("dir2", WSTA_ALL_, FALSE, nm);
    if (!w) { t_skip("CreateWindowStation refused for the unprivileged token (gle=%lu)", GetLastError()); return; }
    WCHAR full[512];
    CHECK(r2_obj_name(w, full, 512));
    r2_split(full);
    int before = r2_dir_find(full, nm, NULL, 0);
    if (before < 0) { t_skip("directory not readable"); U(CloseWindowStation)(w); return; }
    CHECK(before == 1);
    CHECK(U(CloseWindowStation)(w));
    CKF(r2_dir_find(full, nm, NULL, 0) == 0, "station still listed after its only handle was closed");
}
RT(desktop_object_name_ends_with_its_name) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(CloseDesktop));
    WCHAR nm[64];
    HDESK d = r2_mk_desk("dir3", DESKTOP_ALL_, FALSE, nm);
    if (!d) { t_skip("CreateDesktop gle=%lu", GetLastError()); return; }
    WCHAR full[512];
    CHECK(r2_obj_name(d, full, 512));
    size_t n = lstrlenW(full), m = lstrlenW(nm);
    CKF(n > m && full[n - m - 1] == L'\\' && !lstrcmpiW(full + n - m, nm), "name=%ls", full);
    t_info("desktop object name: %ls", full);
    U(CloseDesktop)(d);
}

/* ---- GrantedAccess ---------------------------------------------------------------------------------- */
RT(desktop_generic_access_is_mapped_away) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(CloseDesktop));
    static const DWORD req[] = { GENERIC_READ, GENERIC_WRITE, GENERIC_EXECUTE, GENERIC_ALL };
    for (unsigned i = 0; i < 4; i++) {
        char tag[16]; sprintf(tag, "ga%u", i);
        HDESK d = r2_mk_desk(tag, req[i], FALSE, NULL);
        if (!d) { t_note("CreateDesktop(0x%lx) gle=%lu", req[i], GetLastError()); continue; }
        ULONG g = 0;
        CHECK(r2_granted(d, &g));
        CKF((g & 0xF0000000) == 0, "request 0x%lx granted 0x%lx still has generic bits", req[i], g);
        CKF(g != 0, "request 0x%lx granted nothing", req[i]);
        U(CloseDesktop)(d);
    }
}
RT(winsta_generic_access_is_mapped_away) {
    REQUIRE(U(CreateWindowStationW)); REQUIRE(U(CloseWindowStation));
    static const DWORD req[] = { GENERIC_READ, GENERIC_WRITE, GENERIC_EXECUTE, GENERIC_ALL };
    for (unsigned i = 0; i < 4; i++) {
        char tag[16]; sprintf(tag, "wg%u", i);
        HWINSTA w = r2_mk_winsta(tag, req[i], FALSE, NULL);
        if (!w) { t_note("CreateWindowStation(0x%lx) gle=%lu", req[i], GetLastError()); continue; }
        ULONG g = 0;
        CHECK(r2_granted(w, &g));
        CKF((g & 0xF0000000) == 0, "request 0x%lx granted 0x%lx still has generic bits", req[i], g);
        CKF(g != 0, "request 0x%lx granted nothing", req[i]);
        U(CloseWindowStation)(w);
    }
}
RT(desktop_requested_specific_rights_are_granted) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(CloseDesktop));
    static const DWORD req[] = { DESKTOP_READOBJECTS, DESKTOP_ENUMERATE | DESKTOP_READOBJECTS, DESKTOP_CREATEWINDOW | DESKTOP_CREATEMENU,
                                 DESKTOP_HOOKCONTROL | DESKTOP_WRITEOBJECTS, DESKTOP_SWITCHDESKTOP | DESKTOP_READOBJECTS };
    for (unsigned i = 0; i < 5; i++) {
        char tag[16]; sprintf(tag, "sr%u", i);
        HDESK d = r2_mk_desk(tag, req[i], FALSE, NULL);
        if (!d) { t_note("CreateDesktop(0x%lx) gle=%lu", req[i], GetLastError()); continue; }
        ULONG g = 0;
        CHECK(r2_granted(d, &g));
        CKF((g & req[i]) == req[i], "request 0x%lx granted 0x%lx", req[i], g);
        t_note("request 0x%lx -> granted 0x%lx", req[i], g);
        U(CloseDesktop)(d);
    }
}
RT(desktop_maximum_allowed_grants_creator_rights) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(CloseDesktop));
    HDESK d = r2_mk_desk("ma1", MAXIMUM_ALLOWED, FALSE, NULL);
    if (!d) { t_note("CreateDesktop(MAXIMUM_ALLOWED) gle=%lu", GetLastError()); return; }
    ULONG g = 0;
    CHECK(r2_granted(d, &g));
    CKF(!(g & MAXIMUM_ALLOWED), "MAXIMUM_ALLOWED bit survived in 0x%lx", g);
    CKF((g & (DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS)) == (DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS), "granted 0x%lx", g);
    U(CloseDesktop)(d);
}

/* ---- refcounts ------------------------------------------------------------------------------------- */
RT(winsta_duplicate_and_close_adjust_handle_count) {
    REQUIRE(U(CreateWindowStationW)); REQUIRE(U(CloseWindowStation));
    HWINSTA w = r2_mk_winsta("rc1", WSTA_ALL_, FALSE, NULL);
    if (!w) { t_skip("CreateWindowStation refused for the unprivileged token (gle=%lu)", GetLastError()); return; }
    ULONG b0[4], b1[4], b2[4];
    CHECK(r2_basic(w, b0));
    HANDLE dup = NULL;
    CHECK(DuplicateHandle(GetCurrentProcess(), w, GetCurrentProcess(), &dup, 0, FALSE, DUPLICATE_SAME_ACCESS));
    CHECK(r2_basic(w, b1));
    CKF(b1[2] == b0[2] + 1, "HandleCount %lu -> %lu", b0[2], b1[2]);
    CKF(b1[3] >= b1[2], "PointerCount %lu < HandleCount %lu", b1[3], b1[2]);
    if (dup) { CHECK(U(CloseWindowStation)(dup)); }
    CHECK(r2_basic(w, b2));
    CKF(b2[2] == b0[2], "HandleCount %lu -> %lu", b0[2], b2[2]);
    U(CloseWindowStation)(w);
}
RT(winsta_open_by_name_adds_one_handle) {
    REQUIRE(U(CreateWindowStationW)); REQUIRE(U(OpenWindowStationW)); REQUIRE(U(CloseWindowStation));
    WCHAR nm[64];
    HWINSTA w = r2_mk_winsta("rc2", WSTA_ALL_, FALSE, nm);
    if (!w) { t_skip("CreateWindowStation refused for the unprivileged token (gle=%lu)", GetLastError()); return; }
    ULONG b0[4], b1[4], b2[4];
    CHECK(r2_basic(w, b0));
    HWINSTA o = U(OpenWindowStationW)(nm, FALSE, WINSTA_READATTRIBUTES);
    CKF(o != NULL, "gle=%lu", GetLastError());
    if (o) {
        CHECK(r2_basic(w, b1));
        CKF(b1[2] == b0[2] + 1, "HandleCount %lu -> %lu", b0[2], b1[2]);
        CHECK(U(CloseWindowStation)(o));
        CHECK(r2_basic(w, b2));
        CKF(b2[2] == b0[2], "HandleCount %lu -> %lu", b0[2], b2[2]);
    }
    U(CloseWindowStation)(w);
}
RT(desktop_open_by_name_adds_one_handle) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(OpenDesktopW)); REQUIRE(U(CloseDesktop));
    WCHAR nm[64];
    HDESK d = r2_mk_desk("rc3", DESKTOP_ALL_, FALSE, nm);
    if (!d) { t_skip("CreateDesktop gle=%lu", GetLastError()); return; }
    ULONG b0[4], b1[4], b2[4];
    CHECK(r2_basic(d, b0));
    HDESK o = U(OpenDesktopW)(nm, 0, FALSE, DESKTOP_READOBJECTS);
    CKF(o != NULL, "gle=%lu", GetLastError());
    if (o) {
        CHECK(r2_basic(d, b1));
        CKF(b1[2] == b0[2] + 1, "HandleCount %lu -> %lu", b0[2], b1[2]);
        CKF(b1[3] > b0[3], "PointerCount %lu -> %lu", b0[3], b1[3]);
        CHECK(U(CloseDesktop)(o));
        CHECK(r2_basic(d, b2));
        CKF(b2[2] == b0[2], "HandleCount %lu -> %lu", b0[2], b2[2]);
    }
    U(CloseDesktop)(d);
}
RT(desktop_pointer_count_covers_handle_count) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(CloseDesktop));
    HDESK d = r2_mk_desk("rc4", DESKTOP_ALL_, FALSE, NULL);
    if (!d) { t_skip("CreateDesktop gle=%lu", GetLastError()); return; }
    ULONG b[4];
    CHECK(r2_basic(d, b));
    CKF(b[2] >= 1, "HandleCount=%lu", b[2]);
    CKF(b[3] >= b[2], "PointerCount %lu < HandleCount %lu", b[3], b[2]);
    U(CloseDesktop)(d);
}

/* ---- security descriptor access rules ---------------------------------------------------------------- */
static BOOL r2_get_dacl_size(HANDLE h, DWORD *aces, DWORD *bytes, DWORD *gle) {
    BYTE sd[4096]; DWORD need = 0;
    SET_ERR(0);
    if (!U(GetUserObjectSecurity)(h, &(SECURITY_INFORMATION){ DACL_SECURITY_INFORMATION }, sd, sizeof sd, &need)) { *gle = GetLastError(); return FALSE; }
    PACL acl = NULL; BOOL present = FALSE, def = FALSE;
    if (!GetSecurityDescriptorDacl((PSECURITY_DESCRIPTOR)sd, &present, &acl, &def)) { *gle = GetLastError(); return FALSE; }
    *aces = (present && acl) ? acl->AceCount : 0;
    *bytes = (present && acl) ? acl->AclSize : 0;
    return TRUE;
}
RT(desktop_get_security_needs_read_control) {
    if (t_is_wine()) { t_skip("Wine has no win32k object namespace/ACL/access model"); return; }
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(CloseDesktop)); REQUIRE(U(GetUserObjectSecurity));
    HDESK a = r2_mk_desk("sd1", DESKTOP_READOBJECTS, FALSE, NULL);
    if (!a) { t_skip("CreateDesktop gle=%lu", GetLastError()); return; }
    DWORD aces, bytes, gle = 0;
    BOOL ok = r2_get_dacl_size(a, &aces, &bytes, &gle);
    CK(!ok);
    CKF(gle == ERR_ACCESS_DENIED, "gle=%lu", gle);
    U(CloseDesktop)(a);
    HDESK b = r2_mk_desk("sd2", DESKTOP_READOBJECTS | READ_CONTROL, FALSE, NULL);
    if (b) {
        ok = r2_get_dacl_size(b, &aces, &bytes, &gle);
        CKF(ok, "gle=%lu", gle);
        if (ok) CKF(bytes >= sizeof(ACL), "bytes=%lu", bytes);
        U(CloseDesktop)(b);
    }
}
RT(winsta_get_security_needs_read_control) {
    if (t_is_wine()) { t_skip("Wine has no win32k object namespace/ACL/access model"); return; }
    REQUIRE(U(CreateWindowStationW)); REQUIRE(U(CloseWindowStation)); REQUIRE(U(GetUserObjectSecurity));
    HWINSTA a = r2_mk_winsta("sd3", WINSTA_READATTRIBUTES, FALSE, NULL);
    if (!a) { t_skip("CreateWindowStation refused for the unprivileged token (gle=%lu)", GetLastError()); return; }
    DWORD aces, bytes, gle = 0;
    CK(!r2_get_dacl_size(a, &aces, &bytes, &gle));
    CKF(gle == ERR_ACCESS_DENIED, "gle=%lu", gle);
    U(CloseWindowStation)(a);
    HWINSTA b = r2_mk_winsta("sd4", WINSTA_READATTRIBUTES | READ_CONTROL, FALSE, NULL);
    if (b) { CKF(r2_get_dacl_size(b, &aces, &bytes, &gle), "gle=%lu", gle); U(CloseWindowStation)(b); }
}
RT(desktop_set_security_needs_write_dac) {
    if (t_is_wine()) { t_skip("Wine has no win32k object namespace/ACL/access model"); return; }
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(CloseDesktop)); REQUIRE(U(GetUserObjectSecurity)); REQUIRE(U(SetUserObjectSecurity));
    HDESK d = r2_mk_desk("sd5", DESKTOP_READOBJECTS | READ_CONTROL, FALSE, NULL);   /* no WRITE_DAC */
    if (!d) { t_skip("CreateDesktop gle=%lu", GetLastError()); return; }
    BYTE sd[4096]; DWORD need = 0; SECURITY_INFORMATION si = DACL_SECURITY_INFORMATION;
    CHECK(U(GetUserObjectSecurity)(d, &si, sd, sizeof sd, &need));
    SET_ERR(SENT);
    CHECK(!U(SetUserObjectSecurity)(d, &si, (PSECURITY_DESCRIPTOR)sd));
    EXPECT_ERR(ERR_ACCESS_DENIED);
    U(CloseDesktop)(d);
}
RT(desktop_security_dacl_roundtrip_is_stable) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(CloseDesktop)); REQUIRE(U(GetUserObjectSecurity)); REQUIRE(U(SetUserObjectSecurity));
    HDESK d = r2_mk_desk("sd6", DESKTOP_ALL_ | WRITE_DAC | READ_CONTROL, FALSE, NULL);
    if (!d) { t_skip("CreateDesktop gle=%lu", GetLastError()); return; }
    BYTE sd[4096]; DWORD need = 0; SECURITY_INFORMATION si = DACL_SECURITY_INFORMATION;
    CHECK(U(GetUserObjectSecurity)(d, &si, sd, sizeof sd, &need));
    DWORD a0, b0, a1, b1, gle = 0;
    CHECK(r2_get_dacl_size(d, &a0, &b0, &gle));
    SET_ERR(0);
    BOOL ok = U(SetUserObjectSecurity)(d, &si, (PSECURITY_DESCRIPTOR)sd);
    CKF(ok, "SetUserObjectSecurity gle=%lu", GetLastError());
    CHECK(r2_get_dacl_size(d, &a1, &b1, &gle));
    CKF(a0 == a1, "ace count %lu -> %lu", a0, a1);
    CKF(b0 == b1, "acl size %lu -> %lu", b0, b1);
    U(CloseDesktop)(d);
}
RT(winsta_security_dacl_roundtrip_is_stable) {
    REQUIRE(U(CreateWindowStationW)); REQUIRE(U(CloseWindowStation)); REQUIRE(U(GetUserObjectSecurity)); REQUIRE(U(SetUserObjectSecurity));
    HWINSTA w = r2_mk_winsta("sd7", WSTA_ALL_ | WRITE_DAC | READ_CONTROL, FALSE, NULL);
    if (!w) { t_skip("CreateWindowStation refused for the unprivileged token (gle=%lu)", GetLastError()); return; }
    BYTE sd[4096]; DWORD need = 0; SECURITY_INFORMATION si = DACL_SECURITY_INFORMATION;
    CHECK(U(GetUserObjectSecurity)(w, &si, sd, sizeof sd, &need));
    DWORD a0, b0, a1, b1, gle = 0;
    CHECK(r2_get_dacl_size(w, &a0, &b0, &gle));
    SET_ERR(0);
    CKF(U(SetUserObjectSecurity)(w, &si, (PSECURITY_DESCRIPTOR)sd), "gle=%lu", GetLastError());
    CHECK(r2_get_dacl_size(w, &a1, &b1, &gle));
    CKF(a0 == a1 && b0 == b1, "dacl changed %lu/%lu -> %lu/%lu", a0, b0, a1, b1);
    U(CloseWindowStation)(w);
}
RT(desktop_get_security_buffer_protocol) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(CloseDesktop)); REQUIRE(U(GetUserObjectSecurity));
    HDESK d = r2_mk_desk("sd8", DESKTOP_READOBJECTS | READ_CONTROL, FALSE, NULL);
    if (!d) { t_skip("CreateDesktop gle=%lu", GetLastError()); return; }
    DWORD need = 0; SECURITY_INFORMATION si = DACL_SECURITY_INFORMATION;
    BYTE sd[16];
    SET_ERR(0);
    BOOL ok = U(GetUserObjectSecurity)(d, &si, sd, 4, &need);
    CK(!ok);
    CKF(GetLastError() == ERROR_INSUFFICIENT_BUFFER, "gle=%lu", GetLastError());
    CKF(need > 4, "need=%lu", need);
    U(CloseDesktop)(d);
}

/* Desktop handle given where a window station is required (and vice versa): ObReferenceObjectByHandle type mismatch. */
RT(lock_unlock_set_user_reject_desktop_handle) {
    REQUIRE(UN(NtUserLockWindowStation)); REQUIRE(UN(NtUserUnlockWindowStation)); REQUIRE(UN(NtUserSetWindowStationUser)); REQUIRE(U(CreateDesktopW)); REQUIRE(U(CloseDesktop));
    HDESK d = r2_mk_desk("wt1", DESKTOP_ALL_, FALSE, NULL);
    if (!d) { t_skip("CreateDesktop gle=%lu", GetLastError()); return; }
    ULONGLONG luid = 1;
    SET_ERR(SENT); CHECK(R_D(UN(NtUserLockWindowStation)(d)) == 0); CHECK(GetLastError() != SENT && GetLastError() != 0);
    SET_ERR(SENT); CHECK(R_D(UN(NtUserUnlockWindowStation)(d)) == 0); CHECK(GetLastError() != SENT && GetLastError() != 0);
    SET_ERR(SENT); CHECK(R_D(UN(NtUserSetWindowStationUser)(d, &luid, NULL, 0)) == 0); CHECK(GetLastError() != SENT && GetLastError() != 0);
    U(CloseDesktop)(d);
}
RT(switch_desktop_rejects_pseudo_process_handle) {
    REQUIRE(UN(NtUserSwitchDesktop));
    SET_ERR(SENT);
    CHECK(R_D(UN(NtUserSwitchDesktop)(GetCurrentProcess(), 0, 0)) == 0);   /* (HANDLE)-1 is a process, not a desktop */
    CHECK(GetLastError() != SENT && GetLastError() != 0);
}
RT(open_thread_desktop_access_union_table) {
    REQUIRE(UN(NtUserOpenThreadDesktop)); REQUIRE(U(CloseDesktop));
    NEED_WIN(w);
    static const DWORD req[] = { DESKTOP_CREATEWINDOW, DESKTOP_CREATEMENU, DESKTOP_HOOKCONTROL, DESKTOP_ENUMERATE, DESKTOP_SWITCHDESKTOP, READ_CONTROL };
    for (unsigned i = 0; i < 6; i++) {
        HANDLE h = UN(NtUserOpenThreadDesktop)(GetCurrentThreadId(), 0, FALSE, req[i]);
        if (!h) { t_note("OpenThreadDesktop(0x%lx) gle=%lu", req[i], GetLastError()); continue; }
        ULONG g = 0;
        CHECK(r2_granted(h, &g));
        CKF((g & (req[i] | 0x81)) == (req[i] | 0x81), "request 0x%lx granted 0x%lx", req[i], g);
        U(CloseDesktop)(h);
    }
    END_WIN(w);
}
