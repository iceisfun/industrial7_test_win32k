/* dx_desktop_switch: desktop isolation, thread/desktop assignment, CloseDesktop busy rules, a process
 * created on another desktop through STARTUPINFO.lpDesktop. None of the tests here moves the input
 * desktop: they work with threads assigned to a private desktop of the same window station.
 *
 * Analysis anchors:
 *  - NtUserGetForegroundWindow: returns the foreground window only if its desktop  equals the
 *    caller thread's desktop .
 *  - the kernel handler: walks the process's threads; a thread whose assigned desktop handle
 *    equals the handle being closed => LastError 0xAA (ERROR_BUSY), FALSE.
 *  - NtUserSetThreadDesktop: ValidateHdesk(handle, UserMode, access 0) failures map the NTSTATUS
 *    to LastError (NULL/closed => 6); SetThreadDesktop refuses a thread that owns windows or hooks with 0xAA.
 *  - Switching desktops updates the per-thread client info in the TEB (desktop info pointer and heap delta).
 */
#include "dxd.h"

#define SKIPPED 77

static HDESK mk(const char *tag, WCHAR *n) { return dxd_mk_desk(tag, DESKTOP_ALL_RIGHTS, n); }

/* ================================ assignment ====================================================== */

DXT(thread_assigned_to_new_desktop_reports_it) {
    REQUIRE(U(CreateDesktopW));
    WCHAR n[64]; HDESK d = mk("asg", n);
    if (!d) { t_skip("CreateDesktop failed (%lu)", GetLastError()); return; }
    struct dxd_thr t; memset(&t, 0, sizeof t); t.desk = d;
    CHECK(dxd_thr_start(&t, "asg"));
    CHECK(t.set_ok);
    CHECK(t.got != NULL);
    WCHAR g[64];
    if (t.got) { CHECK(dxd_uoi_str(t.got, DXD_UOI_NAME, g, 64)); EXPECT_WSTR(g, n); }
    HDESK seen = U(GetThreadDesktop)(t.tid);            /* queried from another thread (NtUserGetThreadDesktop(tid)) */
    CHECK(seen != NULL);
    if (seen) { CHECK(dxd_uoi_str(seen, DXD_UOI_NAME, g, 64)); EXPECT_WSTR(g, n); }
    dxd_thr_stop(&t);
    U(CloseDesktop)(d);
}

DXT(thread_getthreaddesktop_via_win32u_stub) {
    p_NtUserGetThreadDesktop gt = NTU(NtUserGetThreadDesktop);
    REQUIRE(gt); REQUIRE(U(GetThreadDesktop));
    HDESK a = gt(GetCurrentThreadId()), b = U(GetThreadDesktop)(GetCurrentThreadId());
    CHECK(a != NULL);
    CHECK(a == b);
    SetLastError(0);
    CHECK(gt(0) == NULL);
}

DXT(thread_setthreaddesktop_null_fails_6) {
    REQUIRE(U(SetThreadDesktop));
    SetLastError(0);
    CHECK(!U(SetThreadDesktop)(NULL));
    EXPECT_ERR(ERROR_INVALID_HANDLE);                 /* ValidateHdesk: ObReferenceObjectByHandle(NULL) = STATUS_INVALID_HANDLE */
}

DXT(thread_setthreaddesktop_closed_handle_fails_6) {
    REQUIRE(U(SetThreadDesktop)); REQUIRE(U(CreateDesktopW));
    HDESK d = mk("stc", NULL);
    if (!d) { t_skip("CreateDesktop failed"); return; }
    CHECK(U(CloseDesktop)(d));
    SetLastError(0);
    CHECK(!U(SetThreadDesktop)(d));
    EXPECT_ERR(ERROR_INVALID_HANDLE);
}

DXT(thread_setthreaddesktop_wrong_object_type_fails) {
    REQUIRE(U(SetThreadDesktop));
    HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    SetLastError(0);
    CHECK(!U(SetThreadDesktop)((HDESK)ev));
    t_note("SetThreadDesktop(event) LastError %lu", GetLastError());
    CloseHandle(ev);
    HWINSTA ws = U(GetProcessWindowStation)();
    SetLastError(0);
    CHECK(!U(SetThreadDesktop)((HDESK)ws));
    t_note("SetThreadDesktop(window station) LastError %lu", GetLastError());
}

static void settd_action(struct dxd_thr *t) {
    SetLastError(0);
    t->r[0] = U(SetThreadDesktop)((HDESK)t->user);
    t->r[1] = GetLastError();
}
DXT(thread_setthreaddesktop_busy_when_thread_owns_window) {
    REQUIRE(U(SetThreadDesktop)); REQUIRE(U(CreateDesktopW));
    WCHAR n[64]; HDESK d = mk("busy", n);
    HDESK cur = U(GetThreadDesktop)(GetCurrentThreadId());
    if (!d || !cur) { t_skip("CreateDesktop failed"); if (d) U(CloseDesktop)(d); return; }
    struct dxd_thr t; memset(&t, 0, sizeof t);
    t.want_window = 1; t.user = d; t.action = settd_action;
    CHECK(dxd_thr_start(&t, "busy"));
    if (t.hw) {
        /* SetThreadDesktop: window count != 0 => ERROR_BUSY without changing anything */
        CHECK(t.r[0] == 0);
        CHECKF(t.r[1] == ERROR_BUSY, "err %ld", (long)t.r[1]);
        WCHAR g[64]; HDESK now = U(GetThreadDesktop)(t.tid);
        WCHAR c0[64]; CHECK(dxd_uoi_str(cur, DXD_UOI_NAME, c0, 64));
        CHECK(now && dxd_uoi_str(now, DXD_UOI_NAME, g, 64)); EXPECT_WSTR(g, c0);
    } else t_skip("worker window creation failed");
    dxd_thr_stop(&t);
    U(CloseDesktop)(d);
}

static void sethook_action(struct dxd_thr *t) {
    /* a thread that installed a thread-local hook may not change desktop either */
    t->r[2] = (LONG_PTR)U(SetWindowsHookExW)(WH_GETMESSAGE, (HOOKPROC)U(DefWindowProcW), NULL, GetCurrentThreadId());
    SetLastError(0);
    t->r[0] = U(SetThreadDesktop)((HDESK)t->user);
    t->r[1] = GetLastError();
    if (t->r[2]) U(UnhookWindowsHookEx)((HHOOK)t->r[2]);
}
DXT(thread_setthreaddesktop_busy_when_thread_has_hook_note) {
    REQUIRE(U(SetThreadDesktop)); REQUIRE(U(SetWindowsHookExW));
    HDESK d = mk("hookbusy", NULL);
    if (!d) { t_skip("CreateDesktop failed"); return; }
    struct dxd_thr t; memset(&t, 0, sizeof t);
    t.user = d; t.action = sethook_action;
    CHECK(dxd_thr_start(&t, "hookbusy"));
    if (t.r[2]) { CHECK(t.r[0] == 0); t_note("SetThreadDesktop with a hook installed: err %ld", (long)t.r[1]); }
    else t_note("hook could not be installed");
    dxd_thr_stop(&t);
    U(CloseDesktop)(d);
}

/* ================================ CloseDesktop ==================================================== */

DXT(close_desktop_with_assigned_thread_fails_busy) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(CloseDesktop));
    HDESK d = mk("cbusy", NULL);
    if (!d) { t_skip("CreateDesktop failed"); return; }
    struct dxd_thr t; memset(&t, 0, sizeof t); t.desk = d;
    CHECK(dxd_thr_start(&t, "cbusy") && t.set_ok);
    SetLastError(0);
    CHECK(!U(CloseDesktop)(d));
    EXPECT_ERR(ERROR_BUSY);                          /* _CloseDesktop: handle matches a thread's assigned desktop => 0xAA */
    dxd_thr_stop(&t);
    BOOL ok = FALSE;
    for (int i = 0; i < 50 && !ok; i++) { ok = U(CloseDesktop)(d); if (!ok) Sleep(20); }
    CHECK(ok || t_is_wine());                        /* the thread is gone: the handle can be closed */
}

DXT(close_desktop_of_current_thread_fails_busy) {
    REQUIRE(U(GetThreadDesktop)); REQUIRE(U(CloseDesktop));
    HDESK d = U(GetThreadDesktop)(GetCurrentThreadId());
    if (!d) { t_skip("no thread desktop"); return; }
    SetLastError(0);
    CHECK(!U(CloseDesktop)(d));
    EXPECT_ERR(ERROR_BUSY);
}

DXT(close_desktop_duplicate_handle_of_assigned_desktop_succeeds) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(CloseDesktop));
    HDESK d = mk("cdup", NULL);
    if (!d) { t_skip("CreateDesktop failed"); return; }
    HANDLE dup = NULL;
    CHECK(DuplicateHandle(GetCurrentProcess(), d, GetCurrentProcess(), &dup, 0, FALSE, DUPLICATE_SAME_ACCESS));
    struct dxd_thr t; memset(&t, 0, sizeof t); t.desk = d;
    CHECK(dxd_thr_start(&t, "cdup") && t.set_ok);
    /* only a handle EQUAL to the one the thread was assigned is protected by the busy test */
    if (dup) { SetLastError(0); BOOL ok = U(CloseDesktop)((HDESK)dup); CHECKF(ok, "closing a different handle value failed, err %lu", GetLastError()); }
    SetLastError(0);
    CHECK(!U(CloseDesktop)(d));
    EXPECT_ERR(ERROR_BUSY);
    dxd_thr_stop(&t);
    for (int i = 0; i < 50 && !U(CloseDesktop)(d); i++) Sleep(20);
}

DXT(close_desktop_null_and_bogus_fail) {
    REQUIRE(U(CloseDesktop));
    SetLastError(0); CHECK(!U(CloseDesktop)(NULL)); EXPECT_ERR(ERROR_INVALID_HANDLE);
    SetLastError(0); CHECK(!U(CloseDesktop)((HDESK)(ULONG_PTR)0x12344)); EXPECT_ERR(ERROR_INVALID_HANDLE);
}

DXT(close_desktop_twice_second_fails_6) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(CloseDesktop));
    HDESK d = mk("ctwice", NULL);
    if (!d) { t_skip("CreateDesktop failed"); return; }
    CHECK(U(CloseDesktop)(d));
    SetLastError(0);
    CHECK(!U(CloseDesktop)(d));
    EXPECT_ERR(ERROR_INVALID_HANDLE);
}

DXT(close_desktop_wrong_type_handle_fails) {
    REQUIRE(U(CloseDesktop));
    HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    SetLastError(0);
    CHECK(!U(CloseDesktop)((HDESK)ev));            /* ObReferenceObjectByHandle(.., ExDesktopObjectType) type mismatch */
    t_note("CloseDesktop(event) LastError %lu", GetLastError());
    DWORD f = 0; CHECK(GetHandleInformation(ev, &f));   /* the event handle is untouched */
    CloseHandle(ev);
}

/* ================================ cross-desktop isolation ========================================= */

static void fg_action(struct dxd_thr *t) { t->r[0] = (LONG_PTR)U(GetForegroundWindow)(); }
DXT(isolation_foreground_null_for_thread_on_private_desktop) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(GetForegroundWindow));
    HDESK d = mk("fg2", NULL);
    if (!d) { t_skip("CreateDesktop failed"); return; }
    struct dxd_thr t; memset(&t, 0, sizeof t); t.desk = d; t.r[0] = -1; t.action = fg_action;
    CHECK(dxd_thr_start(&t, "fg2") && t.set_ok);
    CHECKF(t.r[0] == 0, "foreground %p", (void *)t.r[0]);       /* nothing is foreground on a desktop that is not the input desktop */
    dxd_thr_stop(&t);
    U(CloseDesktop)(d);
}

DXT(isolation_findwindow_sees_only_own_desktop) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(FindWindowW));
    HDESK d = mk("find", NULL);
    if (!d) { t_skip("CreateDesktop failed"); return; }
    struct dxd_thr t; memset(&t, 0, sizeof t); t.desk = d; t.want_window = 1;
    struct dxd_thr m; memset(&m, 0, sizeof m); m.want_window = 1;
    dxd_thr_start(&t, "findp"); CHECK(dxd_have_window(&t));
    dxd_thr_start(&m, "findm"); CHECK(dxd_have_window(&m));
    if (t.hw && m.hw) {
        CHECK(U(FindWindowW)(t.cls, NULL) == NULL);      /* class lives on the private desktop: invisible from here */
        HWND own = U(FindWindowW)(m.cls, NULL);
        CHECK(own == m.hw);                              /* positive control on the caller's own desktop */
    }
    dxd_thr_stop(&m); dxd_thr_stop(&t);
    U(CloseDesktop)(d);
}

struct en { HWND want; int found; int count; };
static BOOL CALLBACK en_cb(HWND h, LPARAM lp) {
    struct en *e = (struct en *)lp; e->count++; if (h == e->want) e->found++;
    return TRUE;
}
DXT(isolation_enum_desktop_windows_lists_only_that_desktop) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(EnumDesktopWindows));
    HDESK d = mk("enum", NULL);
    HDESK cur = U(GetThreadDesktop)(GetCurrentThreadId());
    if (!d || !cur) { t_skip("CreateDesktop failed"); if (d) U(CloseDesktop)(d); return; }
    struct dxd_thr t; memset(&t, 0, sizeof t); t.desk = d; t.want_window = 1;
    dxd_thr_start(&t, "enum"); CHECK(dxd_have_window(&t));
    if (t.hw) {
        struct en e1 = { t.hw, 0, 0 }, e2 = { t.hw, 0, 0 };
        SetLastError(0);
        CHECK(U(EnumDesktopWindows)(d, en_cb, (LPARAM)&e1));
        CHECKF(e1.found == 1, "found %d", e1.found);
        CHECK(U(EnumDesktopWindows)(cur, en_cb, (LPARAM)&e2));
        CHECKF(e2.found == 0, "private-desktop window also listed on the default one");
        t_note("EnumDesktopWindows: private desktop %d windows, current desktop %d", e1.count, e2.count);
    }
    dxd_thr_stop(&t);
    U(CloseDesktop)(d);
}

DXT(isolation_enum_desktop_windows_requires_read_access) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(EnumDesktopWindows));
    HDESK d = dxd_mk_desk("enumna", DESKTOP_CREATEWINDOW, NULL);     /* no DESKTOP_READOBJECTS */
    if (!d) { t_skip("CreateDesktop failed"); return; }
    struct en e = { 0, 0, 0 };
    SetLastError(0);
    BOOL rc = U(EnumDesktopWindows)(d, en_cb, (LPARAM)&e);
    DWORD err = GetLastError();
    t_note("EnumDesktopWindows without READOBJECTS -> %d err %lu count %d", rc, err, e.count);
    if (!rc) CHECKF(err == ERROR_ACCESS_DENIED, "err %lu", err);
    U(CloseDesktop)(d);
}

static void post_action(struct dxd_thr *t) { (void)t; }
DXT(isolation_postmessage_to_window_on_other_desktop_not_delivered) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(PostMessageW));
    HDESK d = mk("post", NULL);
    if (!d) { t_skip("CreateDesktop failed"); return; }
    struct dxd_thr t; memset(&t, 0, sizeof t); t.desk = d; t.want_window = 1; t.action = post_action;
    dxd_thr_start(&t, "post"); CHECK(dxd_have_window(&t));
    if (t.hw) {
        SetLastError(0);
        BOOL rc = U(PostMessageW)(t.hw, DXD_WM_COUNT, 0, 0);
        DWORD e = GetLastError();
        Sleep(250);
        CHECKF(t.hits == 0, "message crossed desktops (post rc=%d err=%lu)", rc, e);
        t_note("PostMessage across desktops: rc=%d err=%lu", rc, e);
        /* control: a window on the sender's desktop does receive it */
        struct dxd_thr m; memset(&m, 0, sizeof m); m.want_window = 1;
        if (dxd_thr_start(&m, "postm") && m.hw) {
            CHECK(U(PostMessageW)(m.hw, DXD_WM_COUNT, 0, 0));
            for (int i = 0; i < 50 && !m.hits; i++) Sleep(10);
            CHECK(m.hits == 1);
        }
        dxd_thr_stop(&m);
    }
    dxd_thr_stop(&t);
    U(CloseDesktop)(d);
}

DXT(isolation_sendmessage_timeout_to_window_on_other_desktop_fails) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(SendMessageTimeoutW));
    HDESK d = mk("send", NULL);
    if (!d) { t_skip("CreateDesktop failed"); return; }
    struct dxd_thr t; memset(&t, 0, sizeof t); t.desk = d; t.want_window = 1;
    dxd_thr_start(&t, "send"); CHECK(dxd_have_window(&t));
    if (t.hw) {
        DWORD_PTR res = 0x1234;
        SetLastError(0);
        LRESULT rc = U(SendMessageTimeoutW)(t.hw, DXD_WM_COUNT, 0, 0, SMTO_ABORTIFHUNG, 500, &res);
        DWORD e = GetLastError();
        CHECKF(t.hits == 0, "message was delivered across desktops (rc=%ld)", (long)rc);
        CHECK(rc == 0);
        t_note("SendMessageTimeout across desktops: rc=%ld err=%lu res=%lx", (long)rc, e, (unsigned long)res);
    }
    dxd_thr_stop(&t);
    U(CloseDesktop)(d);
}

static void bc_action(struct dxd_thr *t) {
    DWORD_PTR res = 0;
    t->r[0] = (LONG_PTR)U(SendMessageTimeoutW)(HWND_BROADCAST, DXD_WM_COUNT, 0, 0, SMTO_ABORTIFHUNG, 1000, &res);
}
DXT(isolation_broadcast_stays_on_senders_desktop) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(SendMessageTimeoutW));
    HDESK d1 = mk("bc1", NULL), d2 = mk("bc2", NULL);
    if (!d1 || !d2) { t_skip("CreateDesktop failed"); if (d1) U(CloseDesktop)(d1); if (d2) U(CloseDesktop)(d2); return; }
    struct dxd_thr a, b; memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
    a.desk = d1; a.want_window = 1;
    b.desk = d2; b.want_window = 1; b.action = bc_action;
    dxd_thr_start(&a, "bc1"); CHECK(dxd_have_window(&a));
    dxd_thr_start(&b, "bc2"); CHECK(dxd_have_window(&b));
    if (a.hw && b.hw) {
        CHECKF(b.hits == 1, "sender's own desktop window saw %ld", b.hits);    /* broadcast reaches unowned top-level windows of the sender's desktop */
        CHECKF(a.hits == 0, "window on another desktop saw the broadcast (%ld)", a.hits);
    }
    dxd_thr_stop(&b); dxd_thr_stop(&a);
    U(CloseDesktop)(d1); U(CloseDesktop)(d2);
}

typedef struct { WCHAR want[64]; int found, count; } ENUMD;
static BOOL CALLBACK enumd_cb(LPWSTR name, LPARAM lp) { ENUMD *x = (ENUMD *)lp; x->count++; if (!lstrcmpiW(name, x->want)) x->found++; return TRUE; }
DXT(isolation_desktop_is_listed_by_enumdesktops) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(EnumDesktopsW));
    WCHAR n[64]; HDESK d = mk("lst", n);
    if (!d) { t_skip("CreateDesktop failed"); return; }
    ENUMD e; memset(&e, 0, sizeof e); lstrcpyW(e.want, n);
    CHECK(U(EnumDesktopsW)(U(GetProcessWindowStation)(), enumd_cb, (LPARAM)&e));
    CHECKF(e.found == 1, "found %d among %d", e.found, e.count);
    U(CloseDesktop)(d);
}

/* ================================ _CLIENTINFO follows the desktop ================================= */

struct ci_res { HDESK target, back; void *pdi[3]; void *delta[3]; void *wti[3]; BOOL ok[3]; BOOL set1, set2; };
static void *rd_ptr(unsigned off) { void *p = NULL; k_try_read_ptr((char *)k_teb() + off, &p); return p; }
static DWORD WINAPI ci_thread(void *arg) {
    struct ci_res *r = (struct ci_res *)arg;
    r->back = U(GetThreadDesktop)(GetCurrentThreadId());           /* first win32k syscall: converts this thread to a GUI thread */
    r->pdi[0] = rd_ptr(TEB_Win32ClientInfo + 0x20); r->delta[0] = rd_ptr(TEB_Win32ClientInfo + 0x28); r->wti[0] = rd_ptr(TEB_Win32ThreadInfo);
    r->set1 = U(SetThreadDesktop)(r->target);
    r->pdi[1] = rd_ptr(TEB_Win32ClientInfo + 0x20); r->delta[1] = rd_ptr(TEB_Win32ClientInfo + 0x28); r->wti[1] = rd_ptr(TEB_Win32ThreadInfo);
    r->set2 = U(SetThreadDesktop)(r->back);
    r->pdi[2] = rd_ptr(TEB_Win32ClientInfo + 0x20); r->delta[2] = rd_ptr(TEB_Win32ClientInfo + 0x28); r->wti[2] = rd_ptr(TEB_Win32ThreadInfo);
    return 0;
}
DXT(clientinfo_deskinfo_follows_thread_desktop) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(SetThreadDesktop));
    HDESK d = mk("ci", NULL);
    if (!d) { t_skip("CreateDesktop failed"); return; }
    struct ci_res r; memset(&r, 0, sizeof r); r.target = d;
    HANDLE th = CreateThread(NULL, 0, ci_thread, &r, 0, NULL);
    if (!th || WaitForSingleObject(th, 8000) != WAIT_OBJECT_0) { t_skip("worker did not finish"); if (th) CloseHandle(th); return; }
    CloseHandle(th);
    if (!r.wti[0]) { t_skip("no Win32ThreadInfo (no win32k in this environment)"); U(CloseDesktop)(d); return; }
    CHECK(r.set1); CHECK(r.set2);
    CHECK(r.pdi[0] != NULL); CHECK(r.pdi[1] != NULL);
    CHECKF(r.pdi[0] != r.pdi[1], "pDeskInfo unchanged (%p) after SetThreadDesktop", r.pdi[0]);   /* each desktop owns its own heap + DESKTOPINFO */
    CHECKF(r.pdi[2] == r.pdi[0], "pDeskInfo %p -> %p after going back", r.pdi[0], r.pdi[2]);
    CHECK(r.delta[0] != NULL); CHECK(r.delta[1] != NULL);
    CHECKF(r.delta[2] == r.delta[0], "ulClientDelta %p -> %p after going back", r.delta[0], r.delta[2]);
    /* tagTHREADINFO is not reallocated by a desktop change */
    CHECK(r.wti[1] == r.wti[0]); CHECK(r.wti[2] == r.wti[0]);
    U(CloseDesktop)(d);
}

/* ================================ process placed on a desktop ===================================== */

CHILD(dxd_deskproc) {
    /* argv: dxd_deskproc <expected desktop> <parent class> <my class> <ready event> <done event> */
    dxd_cf = 0;
    if (argc < 6) return 2;
    WCHAR want[64], pcls[96], mcls[96];
    MultiByteToWideChar(CP_ACP, 0, argv[1], -1, want, 64);
    MultiByteToWideChar(CP_ACP, 0, argv[2], -1, pcls, 96);
    MultiByteToWideChar(CP_ACP, 0, argv[3], -1, mcls, 96);
    HANDLE ready = OpenEventA(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, argv[4]);
    HANDLE done = OpenEventA(EVENT_MODIFY_STATE | SYNCHRONIZE, FALSE, argv[5]);
    if (!ready || !done) return 3;
    HDESK d = U(GetThreadDesktop)(GetCurrentThreadId());
    WCHAR got[64];
    CK(d != NULL);
    CK(d && dxd_uoi_str(d, DXD_UOI_NAME, got, 64));
    CKF(!lstrcmpiW(got, want), "child desktop \"%ls\" want \"%ls\"", got, want);
    HINSTANCE hi = GetModuleHandleW(NULL);
    WNDCLASSW wc; memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = (WNDPROC)U(DefWindowProcW); wc.hInstance = hi; wc.lpszClassName = mcls;
    U(RegisterClassW)(&wc);
    HWND w = U(CreateWindowExW)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, mcls, L"dxd", WS_POPUP, -32000, -32000, 8, 8, NULL, NULL, hi, NULL);
    CK(w != NULL);
    CK(U(FindWindowW)(pcls, NULL) == NULL);        /* the parent's window is on another desktop */
    CK(U(FindWindowW)(mcls, NULL) == w);
    CK(U(GetForegroundWindow)() == NULL);
    SetEvent(ready);
    WaitForSingleObject(done, 10000);
    if (w) U(DestroyWindow)(w);
    return dxd_cf;
}

struct clsfind { WCHAR cls[96]; int found; HWND h; };
static BOOL CALLBACK clsfind_cb(HWND h, LPARAM lp) {
    struct clsfind *x = (struct clsfind *)lp; WCHAR c[96];
    if (U(GetClassNameW)(h, c, 96) && !lstrcmpW(c, x->cls)) { x->found++; x->h = h; }
    return TRUE;
}
DXT(process_on_private_desktop_via_lpDesktop) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(EnumDesktopWindows));
    WCHAR dn[64], wn[64]; HDESK d = mk("proc", dn);
    if (!d) { t_skip("CreateDesktop failed"); return; }
    if (!dxd_winsta_name(wn, 64)) { t_skip("no window station name"); U(CloseDesktop)(d); return; }
    struct dxd_thr p; memset(&p, 0, sizeof p); p.want_window = 1;
    dxd_thr_start(&p, "procp");
    if (!p.hw) { CHECK(dxd_have_window(&p)); dxd_thr_stop(&p); U(CloseDesktop)(d); return; }
    char evr[64], evd[64], dn_a[64], pcls_a[96], mcls_a[96], desk_a[160], extra[512];
    WCHAR mcls[96]; swprintf(mcls, 96, L"win32kt%lu_procc", GetCurrentProcessId());
    snprintf(evr, sizeof evr, "win32kt%lu_dpr", GetCurrentProcessId());
    snprintf(evd, sizeof evd, "win32kt%lu_dpd", GetCurrentProcessId());
    WideCharToMultiByte(CP_ACP, 0, dn, -1, dn_a, sizeof dn_a, NULL, NULL);
    WideCharToMultiByte(CP_ACP, 0, p.cls, -1, pcls_a, sizeof pcls_a, NULL, NULL);
    WideCharToMultiByte(CP_ACP, 0, mcls, -1, mcls_a, sizeof mcls_a, NULL, NULL);
    char wn_a[64]; WideCharToMultiByte(CP_ACP, 0, wn, -1, wn_a, sizeof wn_a, NULL, NULL);
    snprintf(desk_a, sizeof desk_a, "%s\\%s", wn_a, dn_a);
    snprintf(extra, sizeof extra, "%s %s %s %s %s", dn_a, pcls_a, mcls_a, evr, evd);
    HANDLE er = CreateEventA(NULL, TRUE, FALSE, evr), ed = CreateEventA(NULL, TRUE, FALSE, evd);
    PROCESS_INFORMATION pi;
    HANDLE hp = dxd_spawn_on_desktop("dxd_deskproc", extra, desk_a, &pi);
    if (!hp) {
        t_note("CreateProcess on the private desktop failed, err %lu", GetLastError());
        t_skip("cannot start a process on the private desktop");
    } else {
        BOOL up = WaitForSingleObject(er, 10000) == WAIT_OBJECT_0;
        CHECK(up);
        if (up) {
            /* visible only there: not from this desktop, but enumerable through the desktop handle */
            CHECK(U(FindWindowW)(mcls, NULL) == NULL);
            struct clsfind f; memset(&f, 0, sizeof f); lstrcpyW(f.cls, mcls);
            CHECK(U(EnumDesktopWindows)(d, clsfind_cb, (LPARAM)&f));
            CHECKF(f.found == 1, "child's window found %d times on its desktop", f.found);
            if (f.h) {
                DWORD pid = 0, tid = U(GetWindowThreadProcessId)(f.h, &pid);
                if (tid) CHECKF(pid == pi.dwProcessId, "owner pid %lu want %lu", pid, pi.dwProcessId);
                else t_note("GetWindowThreadProcessId across desktops failed, err %lu", GetLastError());
            }
        }
        SetEvent(ed);
        DWORD code = 0xDEAD0001;
        if (WaitForSingleObject(hp, 10000) == WAIT_OBJECT_0) GetExitCodeProcess(hp, &code); else TerminateProcess(hp, 1);
        CHECKF(code == 0, "child exit code %lu", code);
        CloseHandle(pi.hThread); CloseHandle(hp);
    }
    CloseHandle(er); CloseHandle(ed);
    dxd_thr_stop(&p);
    BOOL ok = FALSE;
    for (int i = 0; i < 100 && !ok; i++) { ok = U(CloseDesktop)(d); if (!ok) Sleep(30); }
    CHECK(ok);
}
