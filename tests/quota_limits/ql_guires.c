/* NtUserGetGuiResources (win32kfull): the counters behind GetGuiResources. Analysis facts:
 *  - flags > 4 -> UserSetLastError(0x57), return 0 (before anything else, for every handle kind)
 *  - handle -2 (GetCurrentThread() pseudo handle) selects the session wide counters
 *    (0: GDI handle count, 1: USER handle count, 2: GDI peak, 4: USER peak; 3 -> 0 silently)
 *  - handle -1 -> this process' W32PROCESS: 0 -> +0x3c (GDI), 1 -> +0x44 (USER), 2 -> +0x40, 4 -> +0x48; 3 -> 0 silently
 *  - any other handle: ObReferenceObjectByHandle(h, 0x1000 PROCESS_QUERY_LIMITED_INFORMATION, PsProcessType);
 *    failure, a process of another session, or a process without a W32PROCESS all give 0x57 / 0. */
#include "ql.h"

static HANDLE open_self(DWORD access) { return OpenProcess(access, FALSE, GetCurrentProcessId()); }
static DWORD gur(HANDLE h, DWORD f) { return U(GetGuiResources)(h, f); }

TEST(quota_limits_guires_flag_constants) {
    REQUIRE(U(GetGuiResources));
    QL_NEED();
    CHECK(GR_GDIOBJECTS == 0 && GR_USEROBJECTS == 1);
    CHECK(GR_GDIOBJECTS_PEAK == 2 && GR_USEROBJECTS_PEAK == 4);
    for (DWORD f = 0; f <= 4; f++) {
        if (f == 3) continue;
        SetLastError(0);
        (void)gur(GetCurrentProcess(), f);
        EXPECT_ERR(0);
    }
}

TEST(quota_limits_guires_invalid_flags_self) {
    REQUIRE(U(GetGuiResources));
    QL_NEED();
    static const DWORD bad[] = { 5, 6, 7, 8, 9, 15, 16, 0x100, 0x10000, 0x7fffffff, 0x80000000, 0xffffffff };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(0);
        DWORD r = gur(GetCurrentProcess(), bad[i]);
        CHECKF(r == 0, "flags 0x%lx returned %lu", bad[i], r);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
}

TEST(quota_limits_guires_invalid_flags_real_handle) {
    REQUIRE(U(GetGuiResources));
    QL_NEED();
    HANDLE h = open_self(PROCESS_QUERY_LIMITED_INFORMATION);
    if (!h) { t_skip("OpenProcess failed"); return; }
    static const DWORD bad[] = { 5, 6, 0x40, 0xffffffff };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(0);
        CHECK(gur(h, bad[i]) == 0);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
    CloseHandle(h);
}

TEST(quota_limits_guires_invalid_flags_global) {
    REQUIRE(U(GetGuiResources));
    QL_NEED();
    /* the flag range check precedes the pseudo handle dispatch */
    static const DWORD bad[] = { 5, 8, 0x1000, 0xffffffff };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(0);
        CHECK(gur((HANDLE)(LONG_PTR)-2, bad[i]) == 0);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
}

TEST(quota_limits_guires_flag3_is_zero_without_error) {
    REQUIRE(U(GetGuiResources));
    QL_NEED();
    HANDLE hs[3] = { GetCurrentProcess(), (HANDLE)(LONG_PTR)-2, open_self(PROCESS_QUERY_LIMITED_INFORMATION) };
    for (int i = 0; i < 3; i++) {
        if (!hs[i]) continue;
        SetLastError(0);
        CHECK(gur(hs[i], 3) == 0);       /* 3 is within the range check but selects no counter */
        EXPECT_ERR(0);
    }
    if (hs[2]) CloseHandle(hs[2]);
}

TEST(quota_limits_guires_null_and_bogus_handles) {
    REQUIRE(U(GetGuiResources));
    QL_NEED();
    static const ULONG_PTR bad[] = { 0, 4, 0x7ffffff0, 0xdeadbee0 };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        for (DWORD f = 0; f <= 4; f++) {
            if (f == 3) continue;
            SetLastError(0);
            DWORD r = gur((HANDLE)bad[i], f);
            CHECKF(r == 0, "handle %p flags %lu returned %lu", (void *)bad[i], f, r);
            EXPECT_ERR(ERROR_INVALID_PARAMETER);
        }
    }
}

TEST(quota_limits_guires_wrong_object_type_handles) {
    REQUIRE(U(GetGuiResources));
    QL_NEED();
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    HANDLE th = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentThreadId());
    HANDLE mx = CreateMutexA(NULL, FALSE, NULL);
    HANDLE hs[3] = { ev, th, mx };
    for (int i = 0; i < 3; i++) {
        if (!hs[i]) continue;
        /* ObReferenceObjectByHandle(.., PsProcessType) rejects any other object type */
        for (DWORD f = 0; f <= 4; f++) {
            if (f == 3) continue;
            SetLastError(0);
            CHECK(gur(hs[i], f) == 0);
            EXPECT_ERR(ERROR_INVALID_PARAMETER);
        }
        CloseHandle(hs[i]);
    }
}

TEST(quota_limits_guires_process_handle_access_rights) {
    REQUIRE(U(GetGuiResources));
    QL_NEED();
    /* 0x1000 (PROCESS_QUERY_LIMITED_INFORMATION) is demanded by ObReferenceObjectByHandle */
    HANDLE sync = open_self(SYNCHRONIZE);
    if (sync) {
        SetLastError(0);
        CHECK(gur(sync, GR_USEROBJECTS) == 0);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
        CloseHandle(sync);
    }
    HANDLE term;
    if (DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(), &term, PROCESS_TERMINATE, FALSE, 0)) {
        SetLastError(0);
        CHECK(gur(term, GR_GDIOBJECTS) == 0);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
        CloseHandle(term);
    }
    HANDLE ok;
    if (DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(), &ok, PROCESS_QUERY_LIMITED_INFORMATION, FALSE, 0)) {
        SetLastError(0);
        DWORD a = gur(ok, GR_USEROBJECTS), b = gur(GetCurrentProcess(), GR_USEROBJECTS);
        CHECKF(a == b, "real handle %lu pseudo handle %lu", a, b);
        EXPECT_ERR(0);
        CloseHandle(ok);
    }
}

TEST(quota_limits_guires_real_handle_equals_pseudo_handle) {
    REQUIRE(U(GetGuiResources));
    QL_NEED();
    HANDLE h = open_self(PROCESS_QUERY_LIMITED_INFORMATION);
    if (!h) { t_skip("OpenProcess failed"); return; }
    static const DWORD fl[] = { 0, 1, 2, 4 };
    for (int i = 0; i < 4; i++) {
        DWORD a = gur(GetCurrentProcess(), fl[i]), b = gur(h, fl[i]), c = gur((HANDLE)(LONG_PTR)-1, fl[i]);
        CHECKF(a == b && a == c, "flags %lu: pseudo %lu real %lu literal -1 %lu", fl[i], a, b, c);
    }
    CloseHandle(h);
    CHECK(GetCurrentProcess() == (HANDLE)(LONG_PTR)-1);
    CHECK(GetCurrentThread() == (HANDLE)(LONG_PTR)-2);
}

TEST(quota_limits_guires_peak_not_below_current) {
    REQUIRE(U(GetGuiResources));
    QL_NEED();
    DWORD g = gur(GetCurrentProcess(), 0), gp = gur(GetCurrentProcess(), 2);
    DWORD u = gur(GetCurrentProcess(), 1), up = gur(GetCurrentProcess(), 4);
    CHECKF(gp >= g, "GDI peak %lu < count %lu", gp, g);
    CHECKF(up >= u, "USER peak %lu < count %lu", up, u);
}

TEST(quota_limits_guires_global_counters_cover_process) {
    REQUIRE(U(GetGuiResources));
    QL_NEED();
    HANDLE gl = (HANDLE)(LONG_PTR)-2;
    DWORD own_u = gur(GetCurrentProcess(), 1), own_g = gur(GetCurrentProcess(), 0);
    DWORD g_u = gur(gl, 1), g_g = gur(gl, 0);
    DWORD gp_u = gur(gl, 4), gp_g = gur(gl, 2);
    /* the USER handle count counts every USER handle table entry */
    CHECKF(g_u >= own_u, "session USER count %lu < process count %lu", g_u, own_u);
    CHECKF(gp_u >= g_u, "session USER peak %lu < count %lu", gp_u, g_u);
    CHECKF(gp_g >= g_g, "session GDI peak %lu < count %lu", gp_g, g_g);
    if (g_g < own_g) t_note("session GDI count %lu below process count %lu", g_g, own_g);
    CHECK(g_u > 0);
    t_info("session counters: GDI %lu (peak %lu) USER %lu (peak %lu)", g_g, gp_g, g_u, gp_u);
}

TEST(quota_limits_guires_global_peak_never_drops_here) {
    REQUIRE(U(GetGuiResources));
    QL_NEED();
    HANDLE gl = (HANDLE)(LONG_PTR)-2;
    DWORD p1 = gur(gl, 4), q1 = gur(gl, 2);
    HMENU m[100];
    for (int i = 0; i < 100; i++) m[i] = U(CreateMenu)();
    HBRUSH b[100];
    for (int i = 0; i < 100; i++) b[i] = U(CreateSolidBrush)(RGB(i, 0, 0));
    for (int i = 0; i < 100; i++) { if (m[i]) U(DestroyMenu)(m[i]); if (b[i]) U(DeleteObject)(b[i]); }
    DWORD p2 = gur(gl, 4), q2 = gur(gl, 2);
    CHECKF(p2 >= p1, "session USER peak decreased %lu -> %lu", p1, p2);
    CHECKF(q2 >= q1, "session GDI peak decreased %lu -> %lu", q1, q2);
}

/* ---- the counters track creations exactly ---- */
TEST(quota_limits_guires_gdi_count_tracks_brushes) {
    REQUIRE(U(GetGuiResources));
    QL_NEED();
    struct ql_drain dr; ql_drain_take(&dr);      /* earlier tests left deleted brushes in the handle cache: empty it first */
    DWORD base = gur(GetCurrentProcess(), 0);
    HBRUSH b[50];
    for (int i = 0; i < 50; i++) {
        b[i] = U(CreateSolidBrush)(RGB(i, i, i));
        CHECKF(gur(GetCurrentProcess(), 0) == base + i + 1, "count after %d brushes: %lu base %lu", i + 1, gur(GetCurrentProcess(), 0), base);
    }
    DWORD peak = gur(GetCurrentProcess(), 2);
    CHECK(peak >= base + 50);
    for (int i = 49; i >= 0; i--) {
        CHECK(U(DeleteObject)(b[i]));
        /* the first deleted brushes only move into the handle cache and stay counted (QL_CACHE_MAX) */
        DWORD c = gur(GetCurrentProcess(), 0);
        CHECKF(c >= base + i && c <= base + i + QL_CACHE_MAX, "count after deleting down to %d: %lu (base %lu)", i, c, base);
    }
    CHECK(gur(GetCurrentProcess(), 2) == peak);
    ql_drain_give(&dr);
}

TEST(quota_limits_guires_gdi_count_stock_objects_free) {
    REQUIRE(U(GetGuiResources));
    QL_NEED();
    /* stock objects are allocated with the no-quota flag and are shared: fetching them costs nothing */
    DWORD base = gur(GetCurrentProcess(), 0);
    for (int i = 0; i <= STOCK_LAST; i++) (void)U(GetStockObject)(i);
    CHECK(gur(GetCurrentProcess(), 0) == base);
}

TEST(quota_limits_guires_gdi_count_dc_and_selection) {
    REQUIRE(U(GetGuiResources));
    QL_NEED();
    DWORD base = gur(GetCurrentProcess(), 0);
    HDC dc = U(CreateCompatibleDC)(NULL);
    if (!dc) { t_skip("no DC"); return; }
    DWORD with_dc = gur(GetCurrentProcess(), 0);
    CHECKF(with_dc == base + 1, "DC adds %lu handles", with_dc - base);
    HBITMAP bm = U(CreateBitmap)(8, 8, 1, 1, NULL);
    HGDIOBJ old = U(SelectObject)(dc, bm);
    CHECK(gur(GetCurrentProcess(), 0) == with_dc + 1);        /* selecting does not create a handle */
    U(SelectObject)(dc, old);
    CHECK(U(DeleteObject)(bm));
    CHECK(U(DeleteDC)(dc));
    CHECK(gur(GetCurrentProcess(), 0) == base);
}

TEST(quota_limits_guires_user_count_tracks_menus) {
    REQUIRE(U(GetGuiResources));
    QL_NEED();
    DWORD base = gur(GetCurrentProcess(), 1);
    HMENU m[40];
    for (int i = 0; i < 40; i++) {
        m[i] = U(CreateMenu)();
        CHECKF(gur(GetCurrentProcess(), 1) == base + i + 1, "USER count after %d menus: %lu base %lu", i + 1, gur(GetCurrentProcess(), 1), base);
    }
    /* appending items to a menu does not allocate handle table entries */
    for (int i = 0; i < 40; i++) U(AppendMenuW)(m[i], MF_STRING, 100 + i, L"x");
    CHECK(gur(GetCurrentProcess(), 1) == base + 40);
    DWORD peak = gur(GetCurrentProcess(), 4);
    CHECK(peak >= base + 40);
    for (int i = 0; i < 40; i++) CHECK(U(DestroyMenu)(m[i]));
    CHECK(gur(GetCurrentProcess(), 1) == base);
    CHECK(gur(GetCurrentProcess(), 4) == peak);
}

TEST(quota_limits_guires_user_count_tracks_accel_tables) {
    REQUIRE(U(GetGuiResources));
    QL_NEED();
    DWORD base = gur(GetCurrentProcess(), 1);
    HACCEL a[30];
    ACCEL e[3] QL_ACCEL_ALIGN; memset(e, 0, sizeof e);
    for (int i = 0; i < 3; i++) { e[i].fVirt = FVIRTKEY; e[i].key = 'A' + i; e[i].cmd = 10 + i; }
    for (int i = 0; i < 30; i++) {
        a[i] = U(CreateAcceleratorTableW)(e, 3);
        CHECKF(gur(GetCurrentProcess(), 1) == base + i + 1, "USER count after %d tables: %lu base %lu", i + 1, gur(GetCurrentProcess(), 1), base);
        if (a[i]) CHECK(U(CopyAcceleratorTableW)(a[i], NULL, 0) == 3);
    }
    for (int i = 0; i < 30; i++) CHECK(U(DestroyAcceleratorTable)(a[i]));
    CHECK(gur(GetCurrentProcess(), 1) == base);
}

TEST(quota_limits_guires_timer_reuse_does_not_allocate) {
    REQUIRE(U(GetGuiResources));
    QL_NEED();
    HWND w = ql_msg_window();
    if (!w) { t_skip("no window"); return; }
    DWORD base = gur(GetCurrentProcess(), 1);
    /* InternalSetTimer finds an existing (hwnd, id) timer in gTimerHashTable and updates it in place */
    for (int i = 0; i < 5; i++) {
        UINT_PTR r = U(SetTimer)(w, 77, 0x7ffffff0 - i, NULL);
        CHECK(r == 77);
        CHECKF(gur(GetCurrentProcess(), 1) == base + 1, "iteration %d: USER count %lu base %lu", i, gur(GetCurrentProcess(), 1), base);
    }
    CHECK(U(SetTimer)(w, 78, 0x7ffffff0, NULL) == 78);
    CHECK(gur(GetCurrentProcess(), 1) == base + 2);
    CHECK(U(KillTimer)(w, 77));
    CHECK(gur(GetCurrentProcess(), 1) == base + 1);
    CHECK(U(KillTimer)(w, 78));
    CHECK(gur(GetCurrentProcess(), 1) == base);
    CHECK(U(DestroyWindow)(w));
}

TEST(quota_limits_guires_timer_without_window_allocates_each_time) {
    REQUIRE(U(GetGuiResources));
    QL_NEED();
    DWORD base = gur(GetCurrentProcess(), 1);
    UINT_PTR id[20];
    for (int i = 0; i < 20; i++) {
        id[i] = U(SetTimer)(NULL, 0, 0x7ffffff0, NULL);
        CHECK(id[i] != 0);
        CHECKF(gur(GetCurrentProcess(), 1) == base + i + 1, "USER count after %d timers: %lu", i + 1, gur(GetCurrentProcess(), 1));
        for (int j = 0; j < i; j++) CHECK(id[i] != id[j]);
    }
    for (int i = 0; i < 20; i++) CHECK(U(KillTimer)(NULL, id[i]));
    CHECK(gur(GetCurrentProcess(), 1) == base);
}

TEST(quota_limits_guires_window_counts_include_children) {
    REQUIRE(U(GetGuiResources));
    QL_NEED();
    HWND p = ql_msg_window();
    if (!p) { t_skip("no window"); return; }
    DWORD base = gur(GetCurrentProcess(), 1);
    HWND c[10];
    for (int i = 0; i < 10; i++) {
        c[i] = U(CreateWindowExW)(0, ql_class(), L"c", WS_CHILD, 0, 0, 1, 1, p, NULL, GetModuleHandleW(NULL), NULL);
        CHECK(c[i] != NULL);
    }
    CHECKF(gur(GetCurrentProcess(), 1) >= base + 10, "10 child windows: count %lu base %lu", gur(GetCurrentProcess(), 1), base);
    /* destroying the parent destroys the children with it */
    CHECK(U(DestroyWindow)(p));
    for (int i = 0; i < 10; i++) CHECK(!U(IsWindow)(c[i]));
    k_pump(0);
    CHECKF(gur(GetCurrentProcess(), 1) + 1 == base, "count %lu after destroying parent (base %lu includes the parent)", gur(GetCurrentProcess(), 1), base);
}

/* ---- other processes ---- */
CHILD(ql_gui) {
    /* argv[1]: ready event, argv[2]: go event (inherited handles, decimal) */
    HANDLE ready = (HANDLE)(ULONG_PTR)strtoull(argv[1], NULL, 10), go = (HANDLE)(ULONG_PTR)strtoull(argv[2], NULL, 10);
    if (!ql_connect()) return 0x7000;
    HMENU m[7]; HBRUSH b[5];
    for (int i = 0; i < 7; i++) m[i] = U(CreateMenu)();
    for (int i = 0; i < 5; i++) b[i] = U(CreateSolidBrush)(RGB(i, 0, 0));
    SetEvent(ready);
    if (WaitForSingleObject(go, 15000) != WAIT_OBJECT_0) return 0x7001;
    /* exit code: USER count in the high half, GDI count in the low half */
    return (int)((ql_gur(GR_USER_CUR) << 16) | (ql_gur(GR_GDI_CUR) & 0xffff));
}

TEST(quota_limits_guires_other_gui_process_counts) {
    REQUIRE(U(GetGuiResources));
    QL_NEED();
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    HANDLE ready = CreateEventA(&sa, TRUE, FALSE, NULL), go = CreateEventA(&sa, TRUE, FALSE, NULL);
    char args[64];
    snprintf(args, sizeof args, "%llu %llu", (unsigned long long)(ULONG_PTR)ready, (unsigned long long)(ULONG_PTR)go);
    PROCESS_INFORMATION pi;
    HANDLE hp = t_spawn_child("ql_gui", args, 0, NULL, NULL, NULL, TRUE, NULL, &pi);
    if (!hp) { t_skip("cannot spawn"); CloseHandle(ready); CloseHandle(go); return; }
    if (WaitForSingleObject(ready, 15000) == WAIT_OBJECT_0) {
        HANDLE q;
        if (DuplicateHandle(GetCurrentProcess(), hp, GetCurrentProcess(), &q, PROCESS_QUERY_LIMITED_INFORMATION, FALSE, 0)) {
            DWORD u = gur(q, 1), g = gur(q, 0), up = gur(q, 4), gp = gur(q, 2);
            CHECKF(u >= 7, "child USER count %lu (7 menus)", u);
            CHECKF(g >= 5, "child GDI count %lu (5 brushes)", g);
            CHECK(up >= u && gp >= g);
            SetLastError(0);
            CHECK(gur(q, 7) == 0); EXPECT_ERR(ERROR_INVALID_PARAMETER);
            SetEvent(go);
            DWORD code = 0;
            if (WaitForSingleObject(hp, 15000) == WAIT_OBJECT_0 && GetExitCodeProcess(hp, &code)) {
                CHECKF((code >> 16) == u, "child reported USER %lu, queried %lu", code >> 16, u);
                CHECKF((code & 0xffff) == g, "child reported GDI %lu, queried %lu", code & 0xffff, g);
            } else CHECK(0);
            /* a handle to an exited process: the W32PROCESS is detached at process exit, so 0 / 0x57 is expected */
            SetLastError(0);
            DWORD after = gur(q, 1);
            if (after != 0) t_note("exited process still reports %lu USER objects", after);
            else EXPECT_ERR(ERROR_INVALID_PARAMETER);
            CloseHandle(q);
        } else { t_skip("DuplicateHandle failed"); SetEvent(go); }
    } else { CHECK(0); SetEvent(go); TerminateProcess(hp, 1); }
    WaitForSingleObject(hp, 5000);
    CloseHandle(pi.hThread); CloseHandle(hp); CloseHandle(ready); CloseHandle(go);
}

TEST(quota_limits_guires_process_without_win32k_state) {
    REQUIRE(U(GetGuiResources));
    QL_NEED();
    /* a suspended process has never run a single instruction, so it cannot have a W32PROCESS */
    PROCESS_INFORMATION pi;
    HANDLE hp = t_spawn_child("ql_gui", "0 0", CREATE_SUSPENDED, NULL, NULL, NULL, FALSE, NULL, &pi);
    if (!hp) { t_skip("cannot spawn"); return; }
    HANDLE q;
    if (DuplicateHandle(GetCurrentProcess(), hp, GetCurrentProcess(), &q, PROCESS_QUERY_LIMITED_INFORMATION, FALSE, 0)) {
        static const DWORD fl[] = { 0, 1, 2, 4 };
        for (int i = 0; i < 4; i++) {
            SetLastError(0);
            DWORD r = gur(q, fl[i]);
            CHECKF(r == 0, "flags %lu: %lu for a process with no win32k state", fl[i], r);
            EXPECT_ERR(ERROR_INVALID_PARAMETER);
        }
        CloseHandle(q);
    }
    TerminateProcess(hp, 0);
    WaitForSingleObject(hp, 5000);
    CloseHandle(pi.hThread); CloseHandle(hp);
}

TEST(quota_limits_guires_registry_quota_values) {
    int f; unsigned v;
    v = ql_reg_dword("GDIProcessHandleQuota", &f);
    if (f) t_note("GDIProcessHandleQuota = %u", v); else t_note("GDIProcessHandleQuota absent (kernel default 10000)");
    struct ql_quota g = ql_gdi_quota();
    CHECK(g.q <= 65536);
    v = ql_reg_dword("USERProcessHandleQuota", &f);
    if (f) t_note("USERProcessHandleQuota = %u", v); else t_note("USERProcessHandleQuota absent (default 10000)");
    v = ql_reg_dword("USERPostMessageLimit", &f);
    if (f) t_note("USERPostMessageLimit = %u", v); else t_note("USERPostMessageLimit absent (default 10000)");
    t_note("effective: GDI %u, USER %u, post %u", g.q, ql_user_quota().q, ql_post_limit().q);
}
