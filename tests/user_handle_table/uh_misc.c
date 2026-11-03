/* System-provided handles, object hierarchies, timers, GetGuiResources protocol. */
#include "uh.h"

UDECL(DWORD, NtUserGetGuiResources, (HANDLE, DWORD));

static int entry_is(HANDLE h, int type, uh_entry_t *e) {
    return uh_read_h(h, e) && e->type == type && e->uniq == UH_UNIQ(h) && !(e->flags & 1);
}

/* ---- handles handed out by the system ---- */
TEST(user_handle_table_desktop_window_entry) {
    UH_NEED();
    HWND d = U(GetDesktopWindow)(); REQUIRE(d);
    uh_entry_t e;
    CHECK(entry_is(d, UT_WINDOW, &e));
    CHECK(U(IsWindow)(d));
    CHECK((UH_IDX(d) & 1) == 0);
    DWORD pid = 0, tid = U(GetWindowThreadProcessId)(d, &pid);
    EXPECT_EQ(e.owner, tid); /* entry owner is the owning thread id */
    CHECK(tid != 0);
}
TEST(user_handle_table_desktop_window_client_head) {
    UH_NEED();
    HWND d = U(GetDesktopWindow)(); REQUIRE(d);
    void *p = uh_client_ptr(d);
    if (!p) { t_skip("fast path not usable"); return; }
    unsigned long long q[2]; uh_entry_t e;
    CHECK(k_try_read(p, q, sizeof q) && uh_read_h(d, &e));
    EXPECT_EQ((unsigned)q[0], (unsigned)(ULONG_PTR)d);
    EXPECT_EQ(q[1], e.delta);
}
static BOOL CALLBACK enum_cb(HWND h, LPARAM l) {
    struct { unsigned n, bad, badowner, foreign; } *s = (void *)l;
    s->n++;
    uh_entry_t e;
    if (!entry_is(h, UT_WINDOW, &e)) { if (!U(IsWindow)(h)) return TRUE; s->bad++; return TRUE; }
    if ((UH_IDX(h) & 1)) s->bad++;
    DWORD pid; DWORD tid = U(GetWindowThreadProcessId)(h, &pid);
    if (tid && e.owner != tid) {
        uh_entry_t e2; Sleep(1);
        if (!(uh_read_h(h, &e2) && e2.owner == tid)) {
            /* a window of another process can be owned by a different thread id than the one it reports (seen on
             * Windows 10 for a shell window); only windows of this process are held to the rule */
            if (pid == GetCurrentProcessId()) s->badowner++; else s->foreign++;
        }
    }
    return s->n < 4000;
}
TEST(user_handle_table_enumwindows_handles_match_table) {
    UH_NEED();
    struct { unsigned n, bad, badowner, foreign; } s = {0, 0, 0, 0};
    U(EnumWindows)(enum_cb, (LPARAM)&s);
    CHECK(s.n > 0);
    EXPECT_EQ(s.bad, 0);
    EXPECT_EQ(s.badowner, 0); /* GetWindowThreadProcessId tid == entry.owner for every live top-level window of this process */
    CHECK(s.foreign * 20 <= s.n); /* and for nearly all windows of other processes */
    if (s.foreign) t_note("%u of %u windows of other processes report a different thread id than the entry owner", s.foreign, s.n);
}
static BOOL CALLBACK enum_child_cb(HWND h, LPARAM l) { struct { unsigned n, bad; } *s = (void *)l; uh_entry_t e; s->n++; if (!entry_is(h, UT_WINDOW, &e) && U(IsWindow)(h)) s->bad++; return s->n < 4000; }
TEST(user_handle_table_enum_desktop_children_match_table) {
    UH_NEED();
    struct { unsigned n, bad; } s = {0, 0};
    U(EnumChildWindows)(U(GetDesktopWindow)(), enum_child_cb, (LPARAM)&s);
    EXPECT_EQ(s.bad, 0);
}
TEST(user_handle_table_foreground_and_shell_entries) {
    UH_NEED();
    HWND list[3] = { U(GetForegroundWindow)(), U(GetShellWindow)(), U(GetActiveWindow)() };
    for (int i = 0; i < 3; i++) {
        if (!list[i]) continue;
        uh_entry_t e;
        if (U(IsWindow)(list[i])) CHECKF(entry_is(list[i], UT_WINDOW, &e), "window %d entry", i);
    }
}
static BOOL CALLBACK mon_cb(HMONITOR m, HDC dc, LPRECT r, LPARAM l) {
    HMONITOR *a = (HMONITOR *)l; for (int i = 0; i < 16; i++) if (!a[i]) { a[i] = m; break; } return TRUE;
}
TEST(user_handle_table_monitor_entries) {
    UH_NEED();
    HMONITOR mons[16] = {0};
    U(EnumDisplayMonitors)(NULL, NULL, mon_cb, (LPARAM)mons);
    if (!mons[0]) { t_skip("no monitors enumerated"); return; }
    struct k_user_shared_info si; CHECK(k_shared_info(&si));
    unsigned long long shared_delta = 0; k_try_read(k_shared_info_ptr() ? (char *)k_shared_info_ptr() + 0x20 : NULL, &shared_delta, 8);
    for (int i = 0; i < 16 && mons[i]; i++) {
        uh_entry_t e;
        CHECK(entry_is(mons[i], UT_MONITOR, &e));
        EXPECT_EQ(e.owner, 0);   /* type flags 0x40: no thread/process owner */
        EXPECT_EQ(e.f10, 0);
        CHECK(e.delta != 0);
        /* user32!GetMonitorInfoW reads the object at entry.delta + gSharedInfo.ulSharedDelta; its head is {handle, delta} */
        unsigned long long q[2] = {0, 0};
        if (k_try_read((void *)(ULONG_PTR)(e.delta + shared_delta), q, sizeof q)) {
            EXPECT_EQ((unsigned)q[0], (unsigned)(ULONG_PTR)mons[i]);
            EXPECT_EQ(q[1], e.delta);
        } else t_note("monitor shared part not readable at delta+ulSharedDelta");
        MONITORINFO mi; memset(&mi, 0, sizeof mi); mi.cbSize = sizeof mi;
        CHECK(U(GetMonitorInfoW)(mons[i], &mi));
    }
}
TEST(user_handle_table_monitor_from_window_is_enumerated) {
    UH_NEED();
    HMONITOR mons[16] = {0};
    U(EnumDisplayMonitors)(NULL, NULL, mon_cb, (LPARAM)mons);
    if (!mons[0]) { t_skip("no monitors"); return; }
    HMONITOR p = U(MonitorFromWindow)(U(GetDesktopWindow)(), MONITOR_DEFAULTTOPRIMARY);
    int found = 0; for (int i = 0; i < 16; i++) if (mons[i] == p) found = 1;
    CHECK(found);
}
TEST(user_handle_table_monitor_forged_uniq_rejected) {
    UH_NEED();
    HMONITOR mons[16] = {0};
    U(EnumDisplayMonitors)(NULL, NULL, mon_cb, (LPARAM)mons);
    if (!mons[0]) { t_skip("no monitors"); return; }
    HMONITOR f = (HMONITOR)uh_forge(mons[0], UH_UNIQ(mons[0]) == 9 ? 10 : 9);
    MONITORINFO mi; memset(&mi, 0, sizeof mi); mi.cbSize = sizeof mi;
    SetLastError(UH_SENTINEL);
    CHECK(!U(GetMonitorInfoW)(f, &mi));
    EXPECT_ERR(0x5b5); /* ERROR_INVALID_MONITOR_HANDLE, user32!GetMonitorInfoW table check */
    f = (HMONITOR)uh_forge(mons[0], 0);
    SetLastError(UH_SENTINEL);
    CHECK(!U(GetMonitorInfoW)(f, &mi));
    EXPECT_ERR(0x5b5);
}
TEST(user_handle_table_monitor_wildcard_uniq_accepted) {
    UH_NEED();
    HMONITOR mons[16] = {0};
    U(EnumDisplayMonitors)(NULL, NULL, mon_cb, (LPARAM)mons);
    if (!mons[0]) { t_skip("no monitors"); return; }
    MONITORINFO mi; memset(&mi, 0, sizeof mi); mi.cbSize = sizeof mi;
    CHECK(U(GetMonitorInfoW)((HMONITOR)uh_forge(mons[0], 0xffff), &mi));
}
TEST(user_handle_table_monitor_bad_cbsize_is_invalid_parameter) {
    UH_NEED();
    HMONITOR mons[16] = {0};
    U(EnumDisplayMonitors)(NULL, NULL, mon_cb, (LPARAM)mons);
    if (!mons[0]) { t_skip("no monitors"); return; }
    MONITORINFO mi; memset(&mi, 0, sizeof mi); mi.cbSize = 0x30;
    SetLastError(UH_SENTINEL);
    CHECK(!U(GetMonitorInfoW)(mons[0], &mi));
    EXPECT_ERR(0x57);
}
TEST(user_handle_table_system_cursor_entry) {
    UH_NEED();
    HCURSOR c = U(LoadCursorW)(NULL, MAKEINTRESOURCEW(32512)); REQUIRE(c);
    uh_entry_t e;
    CHECK(entry_is(c, UT_CURSOR, &e));
    EXPECT_EQ(e.delta, 0); EXPECT_EQ(e.f10, 0);
    CHECK(e.owner != 0);
    CHECK(U(LoadCursorW)(NULL, MAKEINTRESOURCEW(32512)) == c); /* shared system cursor: same handle */
}
TEST(user_handle_table_system_icon_entry) {
    UH_NEED();
    HICON i = U(LoadIconW)(NULL, MAKEINTRESOURCEW(32512)); REQUIRE(i);
    uh_entry_t e;
    CHECK(entry_is(i, UT_CURSOR, &e)); /* icons and cursors share TYPE_CURSOR */
}
TEST(user_handle_table_class_cursor_handle_entry) {
    UH_NEED();
    WNDCLASSEXW wc; memset(&wc, 0, sizeof wc);
    WCHAR name[64]; _snwprintf(name, 64, L"win32kt_uh_cur_%lu", GetCurrentProcessId());
    wc.cbSize = sizeof wc; wc.lpfnWndProc = (WNDPROC)U(DefWindowProcW); wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = name; wc.hCursor = U(LoadCursorW)(NULL, MAKEINTRESOURCEW(32515));
    REQUIRE(wc.hCursor);
    ATOM a = U(RegisterClassExW)(&wc); REQUIRE(a);
    HWND w = U(CreateWindowExW)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, name, L"x", WS_POPUP, -32000, -32000, 1, 1, NULL, NULL, wc.hInstance, NULL);
    if (w) {
        HCURSOR got = (HCURSOR)U(GetClassLongPtrW)(w, GCLP_HCURSOR);
        CHECK(got == wc.hCursor);
        uh_entry_t e; CHECK(entry_is(got, UT_CURSOR, &e));
        U(DestroyWindow)(w);
    }
    U(UnregisterClassW)(name, wc.hInstance);
    CHECK(entry_is(wc.hCursor, UT_CURSOR, &(uh_entry_t){0})); /* shared cursor outlives the class */
}
TEST(user_handle_table_cursor_info_handle_entry) {
    UH_NEED();
    CURSORINFO ci; memset(&ci, 0, sizeof ci); ci.cbSize = sizeof ci;
    if (!U(GetCursorInfo)(&ci)) { t_skip("GetCursorInfo failed (%lu)", GetLastError()); return; }
    if (ci.hCursor) { uh_entry_t e; CHECK(entry_is(ci.hCursor, UT_CURSOR, &e)); }
}

/* ---- hierarchies ---- */
TEST(user_handle_table_destroy_parent_frees_children) {
    UH_NEED();
    DWORD base = uh_gr(GR_USEROBJECTS);
    HWND p = uh_make_top(); REQUIRE(p);
    HWND c[3]; for (int i = 0; i < 3; i++) { c[i] = uh_make_child(p); CHECK(c[i] != NULL); }
    HWND g = uh_make_child(c[0]); CHECK(g != NULL);
    uh_entry_t e;
    for (int i = 0; i < 3; i++) if (c[i]) CHECK(entry_is(c[i], UT_WINDOW, &e));
    U(DestroyWindow)(p);
    for (int i = 0; i < 3; i++) if (c[i]) { CHECK(!U(IsWindow)(c[i])); CHECK(uh_read_h(c[i], &e) && (e.type == 0 || (e.flags & 1))); }
    if (g) CHECK(!U(IsWindow)(g));
    CHECK(!U(IsWindow)(p));
    DWORD fin = uh_gr(GR_USEROBJECTS);
    if (fin != base) t_note("count %lu -> %lu after tree destroy (implicit helper windows)", base, fin);
    CHECK(fin <= base + 2);
}
TEST(user_handle_table_destroy_owner_frees_owned_popup) {
    UH_NEED();
    HWND o = uh_make_top(); REQUIRE(o);
    HWND p = U(CreateWindowExW)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, uh_class(), L"owned", WS_POPUP, -32000, -32000, 1, 1, o, NULL, GetModuleHandleW(NULL), NULL);
    REQUIRE(p);
    CHECK(U(GetWindow)(p, GW_OWNER) == o);
    U(DestroyWindow)(o);
    CHECK(!U(IsWindow)(p)); /* DestroyWindow destroys owned windows */
}
TEST(user_handle_table_destroy_window_destroys_assigned_menu) {
    UH_NEED();
    HWND w = uh_make_top(); REQUIRE(w);
    HMENU m = U(CreateMenu)(); REQUIRE(m);
    CHECK(U(SetMenu)(w, m));
    CHECK(U(GetMenu)(w) == m);
    CHECK(U(IsMenu)(m));
    U(DestroyWindow)(w);
    CHECK(!U(IsMenu)(m)); /* a menu assigned to a window is destroyed with it */
    uh_entry_t e; CHECK(uh_read_h(m, &e) && (e.type == 0 || (e.flags & 1)));
}
TEST(user_handle_table_destroy_menu_is_recursive) {
    UH_NEED();
    DWORD base = uh_gr(GR_USEROBJECTS);
    HMENU m = U(CreateMenu)(), s1 = U(CreatePopupMenu)(), s2 = U(CreatePopupMenu)(), ss = U(CreatePopupMenu)();
    REQUIRE(m && s1 && s2 && ss);
    CHECK(U(AppendMenuW)(m, MF_POPUP, (UINT_PTR)s1, L"a"));
    CHECK(U(AppendMenuW)(m, MF_POPUP, (UINT_PTR)s2, L"b"));
    CHECK(U(AppendMenuW)(s1, MF_POPUP, (UINT_PTR)ss, L"c"));
    EXPECT_EQ(uh_gr(GR_USEROBJECTS), base + 4);
    uh_entry_t e;
    HMENU all[4] = { m, s1, s2, ss };
    for (int i = 0; i < 4; i++) { CHECK(entry_is(all[i], UT_MENU, &e)); EXPECT_EQ(e.owner, GetCurrentProcessId()); }
    CHECK(U(GetSubMenu)(m, 0) == s1 && U(GetSubMenu)(m, 1) == s2 && U(GetSubMenu)(s1, 0) == ss);
    CHECK(U(DestroyMenu)(m)); /* DestroyMenu destroys submenus recursively */
    for (int i = 0; i < 4; i++) CHECK(!U(IsMenu)(all[i]));
    EXPECT_EQ(uh_gr(GR_USEROBJECTS), base);
}
TEST(user_handle_table_system_menu_entry) {
    UH_NEED();
    const WCHAR *c = uh_class(); REQUIRE(c);
    HWND w = U(CreateWindowExW)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, c, L"sys", WS_POPUP | WS_CAPTION | WS_SYSMENU, -32000, -32000, 100, 50, NULL, NULL, GetModuleHandleW(NULL), NULL);
    REQUIRE(w);
    HMENU sm = U(GetSystemMenu)(w, FALSE);
    if (!sm) { t_skip("no system menu"); U(DestroyWindow)(w); return; }
    uh_entry_t e;
    CHECK(entry_is(sm, UT_MENU, &e));
    EXPECT_EQ(e.owner, GetCurrentProcessId());
    CHECK(U(IsMenu)(sm));
    CHECK(U(GetSystemMenu)(w, FALSE) == sm); /* created once per window */
    U(DestroyWindow)(w);
    CHECK(!U(IsMenu)(sm));
}
TEST(user_handle_table_menu_handle_distinct_and_unique) {
    UH_NEED();
    HMENU m[32]; int n = 0;
    for (; n < 32; n++) { m[n] = U(CreateMenu)(); if (!m[n]) break; }
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) CHECK(m[i] != m[j] && UH_IDX(m[i]) != UH_IDX(m[j]));
    for (int i = 0; i < n; i++) U(DestroyMenu)(m[i]);
}
TEST(user_handle_table_window_handles_distinct_indices) {
    UH_NEED();
    HWND w[32]; int n = 0;
    for (; n < 32; n++) { w[n] = uh_make_top(); if (!w[n]) break; }
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) CHECK(UH_IDX(w[i]) != UH_IDX(w[j]));
    for (int i = 0; i < n; i++) U(DestroyWindow)(w[i]);
}
TEST(user_handle_table_destroy_in_reverse_then_recreate_is_lifo) {
    UH_NEED();
    HMENU m[4]; for (int i = 0; i < 4; i++) m[i] = U(CreateMenu)();
    for (int i = 0; i < 4; i++) REQUIRE(m[i]);
    for (int i = 0; i < 4; i++) U(DestroyMenu)(m[i]);
    HMENU n = U(CreateMenu)(); REQUIRE(n);
    /* freeing a handle pushes the freed index on the head of its parity list; the next allocation pops it */
    if (UH_IDX(n) != UH_IDX(m[3])) t_note("last freed index %u not reused (got %u): concurrent allocation", UH_IDX(m[3]), UH_IDX(n));
    else CHECK(UH_UNIQ(n) == uh_next_uniq(UH_UNIQ(m[3])));
    U(DestroyMenu)(n);
}

/* ---- timers (process-owned pool objects, type 16) ---- */
TEST(user_handle_table_timer_entry_lifecycle) {
    UH_NEED();
    unsigned n0 = uh_scan_owned(UT_TIMER); DWORD g0 = uh_gr(GR_USEROBJECTS);
    UINT_PTR id = U(SetTimer)(NULL, 0, 60000, NULL);
    REQUIRE(id);
    EXPECT_EQ(uh_scan_owned(UT_TIMER), n0 + 1);
    EXPECT_EQ(uh_gr(GR_USEROBJECTS), g0 + 1);
    CHECK(U(KillTimer)(NULL, id));
    EXPECT_EQ(uh_scan_owned(UT_TIMER), n0);
    EXPECT_EQ(uh_gr(GR_USEROBJECTS), g0);
}
TEST(user_handle_table_timer_entry_fields) {
    UH_NEED();
    unsigned nb; uh_entry_t *before = uh_snap(&nb); REQUIRE(before);
    UINT_PTR id = U(SetTimer)(NULL, 0, 60000, NULL);
    if (!id) { free(before); t_skip("SetTimer failed"); return; }
    unsigned na; uh_entry_t *after = uh_snap(&na); CHECK(after);
    int found = 0;
    for (unsigned i = 0; after && i < na && i < nb; i++) {
        if (after[i].type != UT_TIMER || after[i].owner != GetCurrentProcessId()) continue;
        if (before[i].type == UT_TIMER && before[i].uniq == after[i].uniq) continue;
        found++;
        EXPECT_EQ(after[i].delta, 0); EXPECT_EQ(after[i].f10, 0);
        CHECK((after[i].flags & 1) == 0);
        /* the entry was free: freeing a handle had already stored the successor uniq, which the new handle carries */
        if (before[i].type == 0) EXPECT_EQ(after[i].uniq, before[i].uniq);
    }
    EXPECT_EQ(found, 1);
    free(before); free(after);
    U(KillTimer)(NULL, id);
}
TEST(user_handle_table_timer_same_id_reuses_object) {
    UH_NEED();
    HWND w = uh_make_top(); REQUIRE(w);
    unsigned n0 = uh_scan_owned(UT_TIMER);
    CHECK(U(SetTimer)(w, 77, 60000, NULL) == 77);
    unsigned n1 = uh_scan_owned(UT_TIMER);
    CHECK(U(SetTimer)(w, 77, 50000, NULL) == 77); /* same (hwnd,id): the existing timer is reset, not duplicated */
    EXPECT_EQ(uh_scan_owned(UT_TIMER), n1);
    EXPECT_EQ(n1, n0 + 1);
    CHECK(U(SetTimer)(w, 78, 60000, NULL) == 78);
    EXPECT_EQ(uh_scan_owned(UT_TIMER), n0 + 2);
    CHECK(U(KillTimer)(w, 77));
    CHECK(U(KillTimer)(w, 78));
    EXPECT_EQ(uh_scan_owned(UT_TIMER), n0);
    U(DestroyWindow)(w);
}
TEST(user_handle_table_destroy_window_kills_its_timers) {
    UH_NEED();
    HWND w = uh_make_top(); REQUIRE(w);
    unsigned n0 = uh_scan_owned(UT_TIMER);
    for (UINT_PTR i = 1; i <= 5; i++) CHECK(U(SetTimer)(w, i, 60000, NULL) == i);
    EXPECT_EQ(uh_scan_owned(UT_TIMER), n0 + 5);
    U(DestroyWindow)(w);
    EXPECT_EQ(uh_scan_owned(UT_TIMER), n0);
}
TEST(user_handle_table_killtimer_unknown_fails) {
    UH_NEED();
    HWND w = uh_make_top(); REQUIRE(w);
    unsigned n0 = uh_scan_owned(UT_TIMER);
    SetLastError(UH_SENTINEL);
    CHECK(!U(KillTimer)(w, 4242));
    EXPECT_EQ(uh_scan_owned(UT_TIMER), n0);
    U(DestroyWindow)(w);
}

/* ---- callproc objects (type 7) are created lazily when an A/W mismatch needs a thunk ---- */
TEST(user_handle_table_callproc_entry_if_created) {
    UH_NEED();
    HWND w = uh_make_top(); REQUIRE(w);
    unsigned n0 = uh_scan_owned(UT_CALLPROC);
    LONG_PTR p = U(GetWindowLongPtrA)(w, GWLP_WNDPROC); /* unicode window seen through the ANSI API */
    unsigned n1 = uh_scan_owned(UT_CALLPROC);
    t_note("GWLP_WNDPROC via ANSI = %p, callproc entries %u -> %u", (void *)p, n0, n1);
    if (n1 > n0) {
        unsigned n; uh_entry_t *t = uh_snap(&n);
        for (unsigned i = 0; t && i < n; i++) if (t[i].type == UT_CALLPROC && t[i].owner == GetCurrentProcessId()) {
            CHECK(t[i].delta != 0); CHECK(t[i].f10 != 0); /* desktop-heap type (flags 0x12) */
        }
        free(t);
    }
    U(DestroyWindow)(w);
}

/* ---- GetGuiResources protocol (NtUserGetGuiResources) ---- */
TEST(user_handle_table_getguiresources_invalid_flag) {
    UH_NEED();
    DWORD f[] = { 5, 6, 0x10, 0x80000000, 0xffffffff };
    for (unsigned i = 0; i < sizeof f / sizeof *f; i++) {
        SetLastError(UH_SENTINEL);
        EXPECT_EQ(U(GetGuiResources)(GetCurrentProcess(), f[i]), 0);
        EXPECT_ERR(0x57); /* 4 < flag => ERROR_INVALID_PARAMETER */
    }
}
TEST(user_handle_table_getguiresources_flag3_is_silent_zero) {
    UH_NEED();
    SetLastError(UH_SENTINEL);
    EXPECT_EQ(U(GetGuiResources)(GetCurrentProcess(), 3), 0); /* no branch handles 3: result 0, no error */
    EXPECT_ERR(UH_SENTINEL);
}
TEST(user_handle_table_getguiresources_bad_handles) {
    UH_NEED();
    SetLastError(UH_SENTINEL);
    EXPECT_EQ(U(GetGuiResources)(NULL, GR_USEROBJECTS), 0); EXPECT_ERR(0x57);
    SetLastError(UH_SENTINEL);
    EXPECT_EQ(U(GetGuiResources)((HANDLE)(ULONG_PTR)0x7fff0, GR_USEROBJECTS), 0); EXPECT_ERR(0x57);
    HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    SetLastError(UH_SENTINEL);
    EXPECT_EQ(U(GetGuiResources)(ev, GR_USEROBJECTS), 0); EXPECT_ERR(0x57); /* ObReferenceObjectByHandle(PsProcessType) fails */
    CloseHandle(ev);
}
TEST(user_handle_table_getguiresources_requires_query_limited_access) {
    UH_NEED();
    HANDLE weak = OpenProcess(SYNCHRONIZE, FALSE, GetCurrentProcessId()); REQUIRE(weak);
    SetLastError(UH_SENTINEL);
    EXPECT_EQ(U(GetGuiResources)(weak, GR_USEROBJECTS), 0);
    EXPECT_ERR(0x57); /* access mask 0x1000 (PROCESS_QUERY_LIMITED_INFORMATION) required */
    CloseHandle(weak);
    HANDLE ok = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentProcessId()); REQUIRE(ok);
    HMENU m = U(CreateMenu)();
    EXPECT_EQ(U(GetGuiResources)(ok, GR_USEROBJECTS), uh_gr(GR_USEROBJECTS));
    EXPECT_EQ(U(GetGuiResources)(ok, 4), uh_gr(4));
    if (m) U(DestroyMenu)(m);
    CloseHandle(ok);
}
TEST(user_handle_table_getguiresources_direct_stub_matches_user32) {
    UH_NEED(); REQUIRE(UN(NtUserGetGuiResources));
    for (DWORD f = 0; f <= 4; f++) {
        DWORD a = UN(NtUserGetGuiResources)(GetCurrentProcess(), f);
        EXPECT_EQ(a, uh_gr(f));
    }
    SetLastError(UH_SENTINEL);
    EXPECT_EQ(UN(NtUserGetGuiResources)(GetCurrentProcess(), 9), 0); EXPECT_ERR(0x57);
}
TEST(user_handle_table_getguiresources_gdi_vs_user_independent) {
    UH_NEED();
    DWORD u0 = uh_gr(GR_USEROBJECTS), g0 = uh_gr(GR_GDIOBJECTS);
    HMENU m = U(CreateMenu)(); REQUIRE(m);
    EXPECT_EQ(uh_gr(GR_USEROBJECTS), u0 + 1);
    EXPECT_EQ(uh_gr(GR_GDIOBJECTS), g0); /* a USER menu is not a GDI object */
    U(DestroyMenu)(m);
}
TEST(user_handle_table_getguiresources_system_pseudo_handle_flags) {
    UH_NEED();
    HANDLE sys = (HANDLE)(LONG_PTR)-2;
    DWORD g = U(GetGuiResources)(sys, GR_GDIOBJECTS), gp = U(GetGuiResources)(sys, 2);
    CHECK(g > 0); CHECK(gp >= g); /* session GDI handle count / peak */
    SetLastError(UH_SENTINEL);
    EXPECT_EQ(U(GetGuiResources)(sys, 7), 0); EXPECT_ERR(0x57);
}
