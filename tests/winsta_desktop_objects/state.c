/* Thread desktop assignment, process window station, enumeration (NtUserBuildNameList),
 * cross-desktop isolation, handle inheritance into a child, pristine-process state. */
#include "wd.h"

/* ============================ GetThreadDesktop / SetThreadDesktop ============================ */

WT(thread_get_current_desktop_is_default_named) {
    REQUIRE(U(GetThreadDesktop));
    if (!wd_interactive()) { t_skip("not interactive"); return; }
    HDESK d = U(GetThreadDesktop)(GetCurrentThreadId());
    CHECK(d != NULL);
    if (!d) return;
    WCHAR n[64], t[32];
    CHECK(wd_uoi_str(d, UOI_NAME_, n, 64)); CHECK(wd_uoi_str(d, UOI_TYPE_, t, 32));
    EXPECT_WSTR(t, L"Desktop");
    if (lstrcmpiW(n, L"Default")) t_note("test thread's desktop is \"%ls\"", n);
}

WT(thread_get_desktop_handle_is_not_closed_by_caller_rules) {
    REQUIRE(U(GetThreadDesktop)); REQUIRE(U(GetProcessWindowStation));
    HDESK d = U(GetThreadDesktop)(GetCurrentThreadId());
    CHECK(d != NULL);
    if (!d) return;
    DWORD fl; CHECK(GetHandleInformation(d, &fl));    /* an ordinary live handle in this process */
    HDESK d2 = U(GetThreadDesktop)(GetCurrentThreadId());
    CKF(d2 == d, "two queries returned %p and %p", d, d2);   /* same desktop both times */
}

WT(thread_get_desktop_invalid_tid) {
    REQUIRE(U(GetThreadDesktop));
    SetLastError(0);
    HDESK d = U(GetThreadDesktop)(0);
    CHECK(d == NULL);
    d = U(GetThreadDesktop)(0xFFFFFFF8);
    CHECK(d == NULL);
    DWORD tid = 0x7FFFFFF0;
    d = U(GetThreadDesktop)(tid);
    CHECK(d == NULL);
}

WT(thread_get_desktop_other_thread_same_process) {
    REQUIRE(U(GetThreadDesktop)); REQUIRE(U(CreateDesktopW));
    struct wd_worker w; memset(&w, 0, sizeof w);
    HANDLE th = wd_worker_start(&w, "gtd");
    WD_NEED_HWND(w, th);
    HDESK mine = U(GetThreadDesktop)(GetCurrentThreadId()), theirs = U(GetThreadDesktop)(w.tid);
    CHECK(theirs != NULL);
    if (mine && theirs) {
        WCHAR a[64], b[64];
        CHECK(wd_uoi_str(mine, UOI_NAME_, a, 64)); CHECK(wd_uoi_str(theirs, UOI_NAME_, b, 64));
        EXPECT_WSTR(a, b);   /* a new thread inherits its creator's desktop */
    }
    wd_worker_stop(&w, th);
}

WT(thread_set_desktop_new_thread_to_custom) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(SetThreadDesktop));
    WCHAR dn[64]; HDESK d = wd_mk_desk("std", DESK_FULL, FALSE, dn);
    if (!d) { wd_skip_create(); return; }
    struct wd_worker w; memset(&w, 0, sizeof w); w.desk = d;
    HANDLE th = wd_worker_start(&w, "std");
    CHECK(w.set_ok);
    WD_NEED_HWND(w, th);
    CHECK(w.got_desk != NULL);
    if (w.got_desk) {
        WCHAR n[64]; CHECK(wd_uoi_str(w.got_desk, UOI_NAME_, n, 64)); EXPECT_WSTR(n, dn);
        /* _GetThreadDesktop: a thread of the calling process returns the stored handle value unchanged */
        CKF(w.got_desk == d, "GetThreadDesktop returned %p, SetThreadDesktop was given %p", w.got_desk, d);
    }
    wd_worker_stop(&w, th);
    U(CloseDesktop)(d);
}

WT(thread_set_desktop_null_handle_fails) {
    REQUIRE(U(SetThreadDesktop));
    struct wd_worker dummy; (void)dummy;
    /* NtUserSetThreadDesktop(NULL): ValidateHdesk fails, only csrss may pass NULL */
    SetLastError(0);
    CHECK(!U(SetThreadDesktop)(NULL));
}

WT(thread_set_desktop_invalid_handle_fails) {
    REQUIRE(U(SetThreadDesktop));
    SetLastError(0);
    CHECK(!U(SetThreadDesktop)((HDESK)(ULONG_PTR)0x7777770));
    HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    CHECK(!U(SetThreadDesktop)((HDESK)ev));   /* wrong object type */
    CloseHandle(ev);
}

WT(thread_set_desktop_busy_with_window) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(SetThreadDesktop));
    HDESK alt = wd_mk_desk("busy1", DESK_FULL, FALSE, NULL);
    if (!alt) { wd_skip_create(); return; }
    struct wd_worker w; memset(&w, 0, sizeof w); w.alt = alt;
    HANDLE th = wd_worker_start(&w, "busy1");
    WD_NEED_HWND(w, th);
    {   /* the refused SetThreadDesktop must leave the thread on its original desktop */
        WCHAR a[64], b[64]; HDESK mine = U(GetThreadDesktop)(GetCurrentThreadId()), theirs = U(GetThreadDesktop)(w.tid);
        if (mine && theirs) { CHECK(wd_uoi_str(mine, UOI_NAME_, a, 64)); CHECK(wd_uoi_str(theirs, UOI_NAME_, b, 64)); EXPECT_WSTR(a, b); }
    }
    /* SetThreadDesktop with windows owned by the thread -> ERROR_BUSY (0xaa), STATUS_DEVICE_BUSY */
    CHECK(!w.set_after_ok);
    CHECKF(w.set_after_err == ERROR_BUSY, "gle=%lu", w.set_after_err);
    wd_worker_stop(&w, th);
    U(CloseDesktop)(alt);
}

WT(thread_set_desktop_busy_with_hook) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(SetThreadDesktop));
    HDESK alt = wd_mk_desk("busy2", DESK_FULL, FALSE, NULL);
    if (!alt) { wd_skip_create(); return; }
    struct wd_worker w; memset(&w, 0, sizeof w); w.alt = alt; w.with_hook = 1;
    HANDLE th = wd_worker_start(&w, "busy2");
    WD_NEED_HWND(w, th);
    if (!w.hook) t_note("hook install failed gle=%lu", GetLastError());
    CHECK(!w.set_after_ok);
    CHECKF(w.set_after_err == ERROR_BUSY, "gle=%lu", w.set_after_err);
    wd_worker_stop(&w, th);
    U(CloseDesktop)(alt);
}

WT(thread_set_desktop_same_desktop_with_window) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(SetThreadDesktop));
    HDESK d = wd_mk_desk("same", DESK_FULL, FALSE, NULL);
    if (!d) { wd_skip_create(); return; }
    struct wd_worker w; memset(&w, 0, sizeof w); w.desk = d; w.alt = d;
    HANDLE th = wd_worker_start(&w, "same");
    CHECK(w.set_ok);
    /* the busy test in SetThreadDesktop precedes the "already on this desktop" shortcut, so even same-desktop is refused with a window */
    t_note("same-desktop SetThreadDesktop with window: ok=%d gle=%lu", w.set_after_ok, w.set_after_err);
    wd_worker_stop(&w, th);
    U(CloseDesktop)(d);
}

WT(thread_set_desktop_needs_no_extra_access) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(OpenDesktopW));
    WCHAR n[64]; HDESK d = wd_mk_desk("acc", DESK_FULL, FALSE, n);
    if (!d) { wd_skip_create(); return; }
    HDESK r = U(OpenDesktopW)(n, 0, FALSE, DESKTOP_ENUMERATE);   /* no DESKTOP_READOBJECTS/CREATEWINDOW */
    CHECK(r != NULL);
    if (r) {
        struct wd_worker w; memset(&w, 0, sizeof w); w.desk = r;
        HANDLE th = wd_worker_start(&w, "acc");
        /* every desktop handle carries DESKTOP_READOBJECTS/WRITEOBJECTS (added at open), so even this one is accepted */
        CHECK(w.set_ok);
        wd_worker_stop(&w, th);
        U(CloseDesktop)(r);
    }
    U(CloseDesktop)(d);
}

WT(thread_close_desktop_in_use_is_busy) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(CloseDesktop));
    HDESK d = wd_mk_desk("use", DESK_FULL, FALSE, NULL);
    if (!d) { wd_skip_create(); return; }
    struct wd_worker w; memset(&w, 0, sizeof w); w.desk = d;
    HANDLE th = wd_worker_start(&w, "use");
    CHECK(w.set_ok);
    SetLastError(0);
    /* _CloseDesktop walks the process thread list: a thread whose desktop handle equals hDesktop -> ERROR_BUSY (0xaa) */
    CHECK(!U(CloseDesktop)(d));
    EXPECT_ERR(ERROR_BUSY);
    DWORD fl; CHECK(GetHandleInformation(d, &fl));   /* still open */
    wd_worker_stop(&w, th);
    SetLastError(0);
    CHECK(U(CloseDesktop)(d));    /* thread gone -> no user left */
}

WT(thread_close_other_handle_of_used_desktop_succeeds) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(OpenDesktopW));
    WCHAR n[64]; HDESK d = wd_mk_desk("use2", DESK_FULL, FALSE, n);
    if (!d) { wd_skip_create(); return; }
    HDESK d2 = U(OpenDesktopW)(n, 0, FALSE, DESKTOP_READOBJECTS);
    CHECK(d2 != NULL);
    struct wd_worker w; memset(&w, 0, sizeof w); w.desk = d;
    HANDLE th = wd_worker_start(&w, "use2");
    CHECK(w.set_ok);
    /* the in-use test compares handle values (stored handle ^ hDesktop), so a second handle to the same object closes fine */
    if (d2) CHECK(U(CloseDesktop)(d2));
    wd_worker_stop(&w, th);
    U(CloseDesktop)(d);
}

WT(thread_close_desktop_invalid_handle) {
    REQUIRE(U(CloseDesktop));
    static const ULONG_PTR hs[] = { 0, 4, 0x7777770 };
    for (unsigned i = 0; i < 3; i++) {
        SetLastError(0);
        CKF(!U(CloseDesktop)((HDESK)hs[i]), "h=%p", (void *)hs[i]);
        CKF(GetLastError() == ERROR_INVALID_HANDLE, "h=%p gle=%lu", (void *)hs[i], GetLastError());   /* STATUS_INVALID_HANDLE -> 6 */
    }
}

WT(thread_close_desktop_wrong_type) {
    REQUIRE(U(CloseDesktop)); REQUIRE(U(CreateWindowStationW));
    HWINSTA w = wd_mk_winsta("wt", WSTA_FULL, FALSE, NULL);
    if (!w) { wd_skip_create(); return; }
    SetLastError(0);
    CHECK(!U(CloseDesktop)((HDESK)w));              /* ObReferenceObjectByHandle(.., ExDesktopObjectType) -> type mismatch */
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    DWORD fl; CHECK(GetHandleInformation(w, &fl));
    SetLastError(0);
    CHECK(!U(CloseWindowStation)((HWINSTA)CreateEventW(NULL, TRUE, FALSE, NULL)));   /* wrong type for NtUserCloseWindowStation too */
    U(CloseWindowStation)(w);
}

/* ============================ cross-desktop isolation ======================================== */

WT(xdesk_window_visible_via_enum_desktop_windows) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(EnumDesktopWindows));
    HDESK d = wd_mk_desk("xe", DESK_FULL, FALSE, NULL);
    if (!d) { wd_skip_create(); return; }
    struct wd_worker w; memset(&w, 0, sizeof w); w.desk = d;
    HANDLE th = wd_worker_start(&w, "xe");
    CHECK(w.set_ok); WD_NEED_HWND(w, th);
    struct wd_wnd_enum e = { w.hwnd, 0, 0 };
    CHECK(U(EnumDesktopWindows)(d, wd_wnd_cb, (LPARAM)&e));
    CKF(e.found == 1, "found=%d count=%d", e.found, e.count);
    wd_worker_stop(&w, th);
    U(CloseDesktop)(d);
}

WT(xdesk_window_not_in_current_desktop_enum) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(EnumDesktopWindows)); REQUIRE(U(GetThreadDesktop));
    HDESK d = wd_mk_desk("xn", DESK_FULL, FALSE, NULL);
    if (!d) { wd_skip_create(); return; }
    struct wd_worker w; memset(&w, 0, sizeof w); w.desk = d;
    HANDLE th = wd_worker_start(&w, "xn");
    struct wd_wnd_enum e = { w.hwnd, 0, 0 };
    HDESK mine = U(GetThreadDesktop)(GetCurrentThreadId());
    WD_NEED_HWND(w, th);
    CHECK(U(EnumDesktopWindows)(mine, wd_wnd_cb, (LPARAM)&e));
    CHECK(e.found == 0);
    struct wd_wnd_enum e2 = { w.hwnd, 0, 0 };
    CHECK(U(EnumWindows)(wd_wnd_cb, (LPARAM)&e2));
    CHECK(e2.found == 0);
    wd_worker_stop(&w, th);
    U(CloseDesktop)(d);
}

WT(xdesk_findwindow_isolated) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(FindWindowW));
    HDESK d = wd_mk_desk("xf", DESK_FULL, FALSE, NULL);
    if (!d) { wd_skip_create(); return; }
    struct wd_worker w; memset(&w, 0, sizeof w); w.desk = d;
    HANDLE th = wd_worker_start(&w, "xf");
    WD_NEED_HWND(w, th);
    CHECK(U(FindWindowW)(w.cls, NULL) == NULL);   /* FindWindow walks only the calling thread's desktop */
    wd_worker_stop(&w, th);
    U(CloseDesktop)(d);
}

WT(xdesk_sendmessage_reaches_window_on_other_desktop) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(SendMessageTimeoutW));
    HDESK d = wd_mk_desk("xs", DESK_FULL, FALSE, NULL);
    if (!d) { wd_skip_create(); return; }
    struct wd_worker w; memset(&w, 0, sizeof w); w.desk = d;
    HANDLE th = wd_worker_start(&w, "xs");
    WD_NEED_HWND(w, th);
    DWORD_PTR res = 0; SetLastError(0);
    LRESULT r = U(SendMessageTimeoutW)(w.hwnd, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 500, &res);
    t_note("SendMessageTimeout cross-desktop -> %lld gle=%lu", (long long)r, GetLastError());
    CHECK(r != 0);   /* window handles are global: a message to a window on another desktop is delivered and answered */
    SetLastError(0);
    BOOL pm = U(PostMessageW)(w.hwnd, WM_NULL, 0, 0);
    t_note("PostMessage cross-desktop -> %d gle=%lu", pm, GetLastError());
    CHECK(pm);
    wd_worker_stop(&w, th);
    U(CloseDesktop)(d);
}

WT(xdesk_window_thread_process_queries) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(GetWindowThreadProcessId));
    HDESK d = wd_mk_desk("xp", DESK_FULL, FALSE, NULL);
    if (!d) { wd_skip_create(); return; }
    struct wd_worker w; memset(&w, 0, sizeof w); w.desk = d;
    HANDLE th = wd_worker_start(&w, "xp");
    DWORD pid = 0; DWORD tid = U(GetWindowThreadProcessId)(w.hwnd, &pid);
    t_info("cross-desktop GetWindowThreadProcessId tid=%lu pid=%lu (expected tid=%lu pid=%lu)", tid, pid, w.tid, GetCurrentProcessId());
    wd_worker_stop(&w, th);
    U(CloseDesktop)(d);
}

WT(xdesk_two_desktops_independent_windows) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(EnumDesktopWindows));
    HDESK a = wd_mk_desk("xa", DESK_FULL, FALSE, NULL), b = wd_mk_desk("xb", DESK_FULL, FALSE, NULL);
    if (!a || !b) { wd_skip_create(); if (a) U(CloseDesktop)(a); if (b) U(CloseDesktop)(b); return; }
    struct wd_worker wa, wb; memset(&wa, 0, sizeof wa); memset(&wb, 0, sizeof wb); wa.desk = a; wb.desk = b;
    HANDLE ta = wd_worker_start(&wa, "xa"), tb = wd_worker_start(&wb, "xb");
    if (!wa.hwnd || !wb.hwnd) { wd_worker_stop(&wa, ta); wd_worker_stop(&wb, tb); U(CloseDesktop)(a); U(CloseDesktop)(b); if (t_is_wine()) t_skip("worker window creation unsupported here"); else CHECK(0); return; }
    struct wd_wnd_enum ea = { wa.hwnd, 0, 0 }, eab = { wb.hwnd, 0, 0 };
    CHECK(U(EnumDesktopWindows)(a, wd_wnd_cb, (LPARAM)&ea)); CHECK(ea.found == 1);
    CHECK(U(EnumDesktopWindows)(a, wd_wnd_cb, (LPARAM)&eab)); CHECK(eab.found == 0);
    wd_worker_stop(&wa, ta); wd_worker_stop(&wb, tb);
    U(CloseDesktop)(a); U(CloseDesktop)(b);
}

WT(xdesk_enum_desktop_windows_needs_read_access) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(EnumDesktopWindows));
    WCHAR n[64]; HDESK d = wd_mk_desk("xr", DESK_FULL, FALSE, n);
    if (!d) { wd_skip_create(); return; }
    HDESK w = U(OpenDesktopW)(n, 0, FALSE, DESKTOP_WRITEOBJECTS);
    CHECK(w != NULL);
    if (w) {
        struct wd_wnd_enum e = { 0, 0, 0 }; SetLastError(0);
        /* EnumDesktopWindows demands DESKTOP_READOBJECTS on the handle */
        CHECK(!U(EnumDesktopWindows)(w, wd_wnd_cb, (LPARAM)&e));
        U(CloseDesktop)(w);
    }
    U(CloseDesktop)(d);
}

WT(xdesk_window_hwnd_table_entry_survives_in_shared_info) {
    REQUIRE(U(CreateDesktopW));
    HDESK d = wd_mk_desk("xh", DESK_FULL, FALSE, NULL);
    if (!d) { wd_skip_create(); return; }
    struct wd_worker w; memset(&w, 0, sizeof w); w.desk = d;
    HANDLE th = wd_worker_start(&w, "xh");
    struct k_user_entry ue;
    if (w.hwnd && k_user_entry((HANDLE)w.hwnd, &ue)) {
        /* the window object lives in the OTHER desktop's heap: its handle-table entry is still session-global and typed WINDOW */
        t_note("cross-desktop hwnd entry type=%u", (unsigned)ue.type);
        CHECK(ue.type == K_TYPE_WINDOW);
    } else t_skip("handle-table entry unreadable (non-Windows or layout differs)");
    wd_worker_stop(&w, th);
    U(CloseDesktop)(d);
}

/* ============================ enumeration ==================================================== */

WT(enum_winstations_contains_WinSta0) {
    REQUIRE(U(EnumWindowStationsW));
    if (!wd_interactive()) { t_skip("not interactive"); return; }
    struct wd_enum e; memset(&e, 0, sizeof e); lstrcpyW(e.want, L"WinSta0");
    CHECK(U(EnumWindowStationsW)((WINSTAENUMPROCW)wd_enum_cb, (LPARAM)&e));
    CHECKF(e.found == 1, "found=%d count=%d", e.found, e.count);
}
WT(enum_winstations_contains_created) {
    REQUIRE(U(EnumWindowStationsW)); REQUIRE(U(CreateWindowStationW));
    WCHAR n[64]; HWINSTA w = wd_mk_winsta("en1", WSTA_FULL, FALSE, n); if (!w) { wd_skip_create(); return; }
    struct wd_enum e; memset(&e, 0, sizeof e); lstrcpyW(e.want, n);
    CHECK(U(EnumWindowStationsW)((WINSTAENUMPROCW)wd_enum_cb, (LPARAM)&e));
    CKF(e.found == 1, "found=%d count=%d", e.found, e.count);
    U(CloseWindowStation)(w);
}
WT(enum_winstations_stop_on_false) {
    REQUIRE(U(EnumWindowStationsW));
    struct wd_enum e; memset(&e, 0, sizeof e); e.stop_after = 1;
    BOOL r = U(EnumWindowStationsW)((WINSTAENUMPROCW)wd_enum_cb, (LPARAM)&e);
    CHECK(e.count == 1);   /* callback returning FALSE ends the walk after one name */
    t_note("EnumWindowStations returned %d after early stop", r);
}
WT(enum_winstations_ansi_matches_unicode_count) {
    REQUIRE(U(EnumWindowStationsA)); REQUIRE(U(EnumWindowStationsW));
    struct wd_enum e; memset(&e, 0, sizeof e);
    int ca = 0;
    CHECK(U(EnumWindowStationsW)((WINSTAENUMPROCW)wd_enum_cb, (LPARAM)&e));
    CHECK(U(EnumWindowStationsA)((WINSTAENUMPROCA)wd_enum_cb_a, (LPARAM)&ca));
    CKF(ca == e.count, "ansi=%d wide=%d", ca, e.count);
}
WT(enum_desktops_contains_Default) {
    REQUIRE(U(EnumDesktopsW)); REQUIRE(U(GetProcessWindowStation));
    if (!wd_interactive()) { t_skip("not interactive"); return; }
    struct wd_enum e; memset(&e, 0, sizeof e); lstrcpyW(e.want, L"Default");
    HWINSTA ws = U(GetProcessWindowStation)();
    CHECK(U(EnumDesktopsW)(ws, (DESKTOPENUMPROCW)wd_enum_cb, (LPARAM)&e));
    CKF(e.found == 1, "found=%d count=%d", e.found, e.count);
}
WT(enum_desktops_null_winsta_uses_process_winsta) {
    REQUIRE(U(EnumDesktopsW)); REQUIRE(U(GetProcessWindowStation));
    struct wd_enum e1, e2; memset(&e1, 0, sizeof e1); memset(&e2, 0, sizeof e2);
    HWINSTA ws = U(GetProcessWindowStation)();
    BOOL r1 = U(EnumDesktopsW)(ws, (DESKTOPENUMPROCW)wd_enum_cb, (LPARAM)&e1);
    BOOL r2 = U(EnumDesktopsW)(NULL, (DESKTOPENUMPROCW)wd_enum_cb, (LPARAM)&e2);
    t_note("explicit=%d/%d null=%d/%d", r1, e1.count, r2, e2.count);
    CHECK(r1);
}
WT(enum_desktops_invalid_winsta_fails) {
    REQUIRE(U(EnumDesktopsW));
    struct wd_enum e; memset(&e, 0, sizeof e);
    SetLastError(0);
    CHECK(!U(EnumDesktopsW)((HWINSTA)(ULONG_PTR)0x7777770, (DESKTOPENUMPROCW)wd_enum_cb, (LPARAM)&e));
    CHECK(e.count == 0);
    HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    CHECK(!U(EnumDesktopsW)((HWINSTA)ev, (DESKTOPENUMPROCW)wd_enum_cb, (LPARAM)&e));
    CloseHandle(ev);
}
WT(enum_desktops_requires_enumdesktops_access) {
    REQUIRE(U(CreateWindowStationW)); REQUIRE(U(EnumDesktopsW));
    WCHAR n[64]; HWINSTA w = wd_mk_winsta("en2", WSTA_FULL, FALSE, n); if (!w) { wd_skip_create(); return; }
    HWINSTA r = U(OpenWindowStationW)(n, FALSE, WINSTA_READATTRIBUTES);
    CHECK(r != NULL);
    if (r) {
        struct wd_enum e; memset(&e, 0, sizeof e);
        /* NtUserBuildNameList: ValidateHwinsta(h, 1, 1 ...) requires WINSTA_ENUMDESKTOPS */
        CHECK(!U(EnumDesktopsW)(r, (DESKTOPENUMPROCW)wd_enum_cb, (LPARAM)&e));
        CHECK(e.count == 0);
        U(CloseWindowStation)(r);
    }
    U(CloseWindowStation)(w);
}
WT(enum_desktops_stop_on_false) {
    REQUIRE(U(EnumDesktopsW)); REQUIRE(U(GetProcessWindowStation));
    struct wd_enum e; memset(&e, 0, sizeof e); e.stop_after = 1;
    U(EnumDesktopsW)(U(GetProcessWindowStation)(), (DESKTOPENUMPROCW)wd_enum_cb, (LPARAM)&e);
    CHECK(e.count == 1);
}
WT(enum_desktops_created_desktops_appear_and_disappear) {
    REQUIRE(U(EnumDesktopsW)); REQUIRE(U(CreateDesktopW)); REQUIRE(U(GetProcessWindowStation));
    HDESK a = wd_mk_desk("ea", DESK_FULL, FALSE, NULL), b = wd_mk_desk("eb", DESK_FULL, FALSE, NULL);
    if (!a || !b) { wd_skip_create(); if (a) U(CloseDesktop)(a); if (b) U(CloseDesktop)(b); return; }
    WCHAR na[64], nb[64]; wd_name(na, "ea"); wd_name(nb, "eb");
    struct wd_enum ea, eb; memset(&ea, 0, sizeof ea); memset(&eb, 0, sizeof eb); lstrcpyW(ea.want, na); lstrcpyW(eb.want, nb);
    HWINSTA ws = U(GetProcessWindowStation)();
    CHECK(U(EnumDesktopsW)(ws, (DESKTOPENUMPROCW)wd_enum_cb, (LPARAM)&ea));
    CHECK(U(EnumDesktopsW)(ws, (DESKTOPENUMPROCW)wd_enum_cb, (LPARAM)&eb));
    CHECK(ea.found == 1 && eb.found == 1);
    U(CloseDesktop)(a);
    memset(&ea, 0, sizeof ea); lstrcpyW(ea.want, na);
    CHECK(U(EnumDesktopsW)(ws, (DESKTOPENUMPROCW)wd_enum_cb, (LPARAM)&ea)); CHECK(ea.found == 0);
    U(CloseDesktop)(b);
}

/* ---- NtUserBuildNameList direct --------------------------------------------------------------- */
WT(namelist_winstations_direct) {
    p_NtUserBuildNameList bl = NTU(NtUserBuildNameList); REQUIRE(bl);
    if (!wd_interactive()) { t_skip("not interactive"); return; }
    BYTE buf[4096]; DWORD need = 0; memset(buf, 0xCC, sizeof buf);
    LONG st = bl(NULL, sizeof buf, buf, &need);
    CKF(st >= 0, "status=0x%lx", (unsigned long)st);
    int cnt = 0; int f = wd_namelist_has(buf, sizeof buf, L"WinSta0", &cnt);
    CKF(f == 1, "found=%d count=%d", f, cnt);
    CHECK(cnt == (int)((DWORD *)buf)[1]);
    CHECK(need > 12);
}
WT(namelist_desktops_direct) {
    p_NtUserBuildNameList bl = NTU(NtUserBuildNameList); REQUIRE(bl); REQUIRE(U(GetProcessWindowStation));
    if (!wd_interactive()) { t_skip("not interactive"); return; }
    BYTE buf[4096]; DWORD need = 0;
    LONG st = bl(U(GetProcessWindowStation)(), sizeof buf, buf, &need);
    CKF(st >= 0, "status=0x%lx", (unsigned long)st);
    int cnt = 0; CHECK(wd_namelist_has(buf, sizeof buf, L"Default", &cnt) == 1);
}
WT(namelist_too_small_buffer_status) {
    p_NtUserBuildNameList bl = NTU(NtUserBuildNameList); REQUIRE(bl);
    BYTE buf[16]; DWORD need = 0;
    for (DWORD cb = 0; cb < 12; cb++) {
        LONG st = bl(NULL, cb, buf, &need);
        /* NtUserBuildNameList: cb < 12 -> 0xC0000008 before touching the buffer */
        CKF((ULONG)st == 0xC0000008, "cb=%lu status=0x%lx", cb, (unsigned long)st);
    }
}
WT(namelist_invalid_hwinsta) {
    p_NtUserBuildNameList bl = NTU(NtUserBuildNameList); REQUIRE(bl);
    BYTE buf[64]; DWORD need = 0; memset(buf, 0xCC, sizeof buf);
    LONG st = bl((HANDLE)(ULONG_PTR)0x7777770, sizeof buf, buf, &need);
    CHECK(st < 0);
    /* failure path: *(DWORD*)buf = 1; *(WORD*)(buf+8) = 0 - an empty list */
    CHECK(((DWORD *)buf)[0] == 1);
    CHECK(*(WORD *)(buf + 8) == 0);
}
WT(namelist_small_but_legal_buffer) {
    p_NtUserBuildNameList bl = NTU(NtUserBuildNameList); REQUIRE(bl);
    BYTE buf[12]; DWORD need = 0;
    LONG st = bl(NULL, sizeof buf, buf, &need);
    t_note("12 byte list: status=0x%lx need=%lu", (unsigned long)st, need);
    if (need) CHECK(need >= 12);
}
WT(namelist_required_size_is_stable_and_sufficient) {
    p_NtUserBuildNameList bl = NTU(NtUserBuildNameList); REQUIRE(bl);
    BYTE small[12], big[8192]; DWORD n1 = 0, n2 = 0, n3 = 0;
    bl(NULL, sizeof small, small, &n1);
    LONG st = bl(NULL, sizeof big, big, &n2);
    CHECK(st >= 0);
    bl(NULL, sizeof small, small, &n3);
    CHECK(n1 == n3);
    t_note("list needs %lu (probe) / %lu (full)", n1, n2);
}
WT(namelist_matches_enumwindowstations) {
    p_NtUserBuildNameList bl = NTU(NtUserBuildNameList); REQUIRE(bl); REQUIRE(U(EnumWindowStationsW));
    BYTE buf[8192]; DWORD need = 0;
    LONG st = bl(NULL, sizeof buf, buf, &need);
    CHECK(st >= 0);
    struct wd_enum e; memset(&e, 0, sizeof e);
    CHECK(U(EnumWindowStationsW)((WINSTAENUMPROCW)wd_enum_cb, (LPARAM)&e));
    CKF(e.count == (int)((DWORD *)buf)[1], "enum=%d list=%lu", e.count, ((DWORD *)buf)[1]);
}

/* ============================ process window station ========================================= */

WT(procwinsta_get_matches_user32_and_nt) {
    p_NtUserGetProcessWindowStation g = NTU(NtUserGetProcessWindowStation); REQUIRE(g); REQUIRE(U(GetProcessWindowStation));
    HWINSTA a = U(GetProcessWindowStation)(), b = g();
    CHECK(a != NULL);
    CHECKF(a == b, "%p vs %p", a, b);
}
WT(procwinsta_type_and_name) {
    REQUIRE(U(GetProcessWindowStation));
    HWINSTA a = U(GetProcessWindowStation)();
    if (!a) { t_skip("no process window station"); return; }
    WCHAR t[32], n[64], p[256];
    CHECK(wd_uoi_str(a, UOI_TYPE_, t, 32)); EXPECT_WSTR(t, L"WindowStation");
    CHECK(wd_type_name(a, p, 256)); EXPECT_WSTR(p, L"WindowStation");
    CHECK(wd_uoi_str(a, UOI_NAME_, n, 64));
    if (wd_interactive()) EXPECT_WSTR(n, L"WinSta0"); else t_note("process window station is \"%ls\"", n);
}
WT(procwinsta_handle_not_closeable_pitfall) {
    REQUIRE(U(GetProcessWindowStation));
    HWINSTA a = U(GetProcessWindowStation)();
    if (!a) { t_skip("no process window station"); return; }
    DWORD fl = 0;
    CHECK(GetHandleInformation(a, &fl));
    t_note("process winsta handle flags=0x%lx", fl);
}
WT(procwinsta_set_invalid_handle_fails_unchanged) {
    p_NtUserSetProcessWindowStation s = NTU(NtUserSetProcessWindowStation); REQUIRE(s); REQUIRE(U(GetProcessWindowStation));
    HWINSTA before = U(GetProcessWindowStation)();
    SetLastError(0);
    CHECK(!s((HANDLE)(ULONG_PTR)0x7777770));
    CHECK(!s(NULL));
    HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    CHECK(!s(ev));    /* SetProcessWindowStation: ObReferenceObjectByHandle(.., ExWindowStationObjectType) mismatch */
    CloseHandle(ev);
    CHECK(U(GetProcessWindowStation)() == before);
}
WT(procwinsta_set_same_station_ok) {
    REQUIRE(U(SetProcessWindowStation)); REQUIRE(U(GetProcessWindowStation));
    HWINSTA cur = U(GetProcessWindowStation)();
    if (!cur) { t_skip("none"); return; }
    CHECK(U(SetProcessWindowStation)(cur));
    CHECK(U(GetProcessWindowStation)() == cur);
}

/* child: swap the process window station to a private one, build a desktop inside it, verify, restore. */
CHILD(wsdo_procwinsta) {
    (void)argc; (void)argv; g_cf = 0;
    HWINSTA (WINAPI *cw)(LPCWSTR, DWORD, ACCESS_MASK, LPSECURITY_ATTRIBUTES) = U(CreateWindowStationW);
    if (!cw) { printf("NOTE no CreateWindowStationW\n"); return 0; }
    HWINSTA orig = U(GetProcessWindowStation)();
    CK(orig != NULL);
    WCHAR on[64], wn[64], dn[64], got[64];
    wd_uoi_str(orig, UOI_NAME_, on, 64);
    HWINSTA w = wd_mk_winsta("pw", WSTA_FULL, FALSE, wn);
    if (!w) { printf("NOTE create winsta failed gle=%lu\n", GetLastError()); return 0; }
    BOOL ok = U(SetProcessWindowStation)(w);
    CKF(ok, "SetProcessWindowStation gle=%lu", GetLastError());
    if (ok) {
        HWINSTA now = U(GetProcessWindowStation)();
        CK(wd_uoi_str(now, UOI_NAME_, got, 64)); CK(!lstrcmpiW(got, wn));
        /* CommonCreateDesktop uses NtUserGetProcessWindowStation() as the root directory: the desktop lands in the new station */
        HDESK d = wd_mk_desk("pwd", DESK_FULL, FALSE, dn);
        CK(d != NULL);
        if (d) {
            struct wd_enum e; memset(&e, 0, sizeof e); lstrcpyW(e.want, dn);
            CK(U(EnumDesktopsW)(w, (DESKTOPENUMPROCW)wd_enum_cb, (LPARAM)&e));
            CKF(e.found == 1 && e.count == 1, "found=%d count=%d", e.found, e.count);
            struct wd_enum e2; memset(&e2, 0, sizeof e2); lstrcpyW(e2.want, L"Default");
            CK(U(EnumDesktopsW)(w, (DESKTOPENUMPROCW)wd_enum_cb, (LPARAM)&e2));
            CK(e2.found == 0);   /* a fresh station has no Default */
            CK(U(CloseDesktop)(d));
        }
        CK(U(SetProcessWindowStation)(orig));
        CK(wd_uoi_str(U(GetProcessWindowStation)(), UOI_NAME_, got, 64)); CK(!lstrcmpiW(got, on));
    }
    U(CloseWindowStation)(w);
    return g_cf;
}
WT(procwinsta_swap_in_child) {
    struct k_child_result r = k_child_run("wsdo_procwinsta", NULL, 30000);
    EXPECT_EQ(r.exit_code, 0);
}

/* child: SetProcessWindowStation by a process whose thread already runs windows on the old station. */
CHILD(wsdo_procwinsta_gui) {
    (void)argc; (void)argv; g_cf = 0;
    if (!U(CreateWindowStationW)) return 0;
    HWND h = k_make_window("wsdo_pwg");
    if (!h && t_is_wine()) { printf("NOTE no window creation under this emulator\n"); return 0; }
    CK(h != NULL);
    HWINSTA orig = U(GetProcessWindowStation)();
    HWINSTA w = wd_mk_winsta("pwg", WSTA_FULL, FALSE, NULL);
    if (!w) { printf("NOTE create gle=%lu\n", GetLastError()); return g_cf; }
    BOOL ok = U(SetProcessWindowStation)(w);
    t_note("SetProcessWindowStation with live window: ok=%d gle=%lu", ok, GetLastError());
    CK(U(IsWindow)(h));      /* the window object stays alive on its desktop */
    if (ok) CK(U(SetProcessWindowStation)(orig));
    CK(U(GetProcessWindowStation)() == orig || ok == 0);
    if (h) U(DestroyWindow)(h);
    U(CloseWindowStation)(w);
    return g_cf;
}
WT(procwinsta_swap_with_gui_thread_in_child) {
    struct k_child_result r = k_child_run("wsdo_procwinsta_gui", NULL, 30000);
    EXPECT_EQ(r.exit_code, 0);
}

/* ============================ handle inheritance into a child ================================= */

static int hexarg(char **argv, int i, HANDLE *h) { unsigned long long v = strtoull(argv[i], NULL, 16); *h = (HANDLE)(ULONG_PTR)v; return 1; }

/* argv: [0]=name [1]=handle(hex) [2]=kind W|D [3]=expected name (ASCII) [4]=expect I|N (inherited / not) */
CHILD(wsdo_inherit) {
    g_cf = 0;
    if (argc < 5) return 1;
    HANDLE h; hexarg(argv, 1, &h);
    WCHAR want[64], got[64], t[32];
    MultiByteToWideChar(CP_ACP, 0, argv[3], -1, want, 64);
    if (argv[4][0] == 'I') {
        CK(wd_uoi_str(h, UOI_NAME_, got, 64));
        CKF(!lstrcmpiW(got, want), "got '%ls' want '%ls'", got, want);
        CK(wd_uoi_str(h, UOI_TYPE_, t, 32));
        CK(!lstrcmpW(t, argv[2][0] == 'W' ? L"WindowStation" : L"Desktop"));
        DWORD fl = 0; CK(GetHandleInformation(h, &fl)); CK(fl & HANDLE_FLAG_INHERIT);   /* inherited handles keep the inherit attribute */
        USEROBJECTFLAGS f; DWORD need;
        CK(U(GetUserObjectInformationW)(h, UOI_FLAGS_, &f, sizeof f, &need)); CK(f.fInherit);
    } else {
        BOOL ok = wd_uoi_str(h, UOI_NAME_, got, 64);
        CK(!ok || lstrcmpiW(got, want) != 0);    /* not inherited: the value is meaningless here */
    }
    return g_cf;
}
static void run_inherit(BOOL winsta, BOOL inheritable) {
    char args[200]; WCHAR n[64]; HANDLE h;
    if (winsta) h = wd_mk_winsta("ihc", WSTA_FULL, inheritable, n); else h = wd_mk_desk("ihc", DESK_FULL, inheritable, n);
    if (!h) { wd_skip_create(); return; }
    char nn[64]; WideCharToMultiByte(CP_ACP, 0, n, -1, nn, 64, NULL, NULL);
    sprintf(args, "%llx %c %s %c", (unsigned long long)(ULONG_PTR)h, winsta ? 'W' : 'D', nn, inheritable ? 'I' : 'N');
    struct k_child_result r = k_child_run("wsdo_inherit", args, 30000);
    EXPECT_EQ(r.exit_code, 0);
    if (winsta) U(CloseWindowStation)(h); else U(CloseDesktop)(h);
}
WT(inherit_desktop_handle_into_child) { REQUIRE(U(CreateDesktopW)); run_inherit(FALSE, TRUE); }
WT(inherit_winsta_handle_into_child) { REQUIRE(U(CreateWindowStationW)); run_inherit(TRUE, TRUE); }
WT(inherit_noninheritable_desktop_absent_in_child) { REQUIRE(U(CreateDesktopW)); run_inherit(FALSE, FALSE); }
WT(inherit_noninheritable_winsta_absent_in_child) { REQUIRE(U(CreateWindowStationW)); run_inherit(TRUE, FALSE); }

WT(inherit_set_flags_then_child_sees_it) {
    REQUIRE(U(CreateDesktopW));
    WCHAR n[64]; HDESK d = wd_mk_desk("ihs", DESK_FULL, FALSE, n); if (!d) { wd_skip_create(); return; }
    USEROBJECTFLAGS f = { TRUE, FALSE, 0 };
    CHECK(U(SetUserObjectInformationW)(d, UOI_FLAGS_, &f, sizeof f));
    char nn[64], args[200]; WideCharToMultiByte(CP_ACP, 0, n, -1, nn, 64, NULL, NULL);
    sprintf(args, "%llx D %s I", (unsigned long long)(ULONG_PTR)d, nn);
    struct k_child_result r = k_child_run("wsdo_inherit", args, 30000);
    EXPECT_EQ(r.exit_code, 0);
    U(CloseDesktop)(d);
}

/* ============================ pristine process ================================================ */

/* Runs before this process touched user32/gdi32: only win32u has been loaded by the harness entry. */
CHILD(wsdo_pristine) {
    (void)argc; (void)argv; g_cf = 0;
    HMODULE w32u = LoadLibraryA("win32u.dll");
    p_NtUserGetProcessWindowStation g = (p_NtUserGetProcessWindowStation)GetProcAddress(w32u, "NtUserGetProcessWindowStation");
    p_NtUserGetThreadDesktop gt = (p_NtUserGetThreadDesktop)GetProcAddress(w32u, "NtUserGetThreadDesktop");
    CK(g != NULL && gt != NULL);
    if (!g || !gt) return g_cf;
    void *gui_before = NULL; k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &gui_before);
    printf("NOTE pristine Win32ThreadInfo=%p\n", gui_before);
    HANDLE ws = g(), dk = gt(GetCurrentThreadId());
    printf("NOTE pristine process winsta=%p thread desktop=%p\n", ws, dk);
    WCHAR t[64], b[256];
    if (ws) { CK(wd_type_name(ws, b, 256)); CK(!lstrcmpW(b, L"WindowStation")); }
    if (dk) { CK(wd_type_name(dk, b, 256)); CK(!lstrcmpW(b, L"Desktop")); }
    /* after the first win32k call the thread is a GUI thread with a desktop */
    HDESK now = U(GetThreadDesktop)(GetCurrentThreadId());
    CK(now != NULL);
    void *gui_after = NULL; k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &gui_after);
    CK(gui_after != NULL);
    if (now) { CK(wd_type_name(now, b, 256)); CK(!lstrcmpW(b, L"Desktop")); }
    (void)t;
    return g_cf;
}
WT(pristine_process_desktop_state) {
    struct k_child_result r = k_child_run("wsdo_pristine", NULL, 30000);
    EXPECT_EQ(r.exit_code, 0);
}
