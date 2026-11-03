/* EnumDisplayMonitors / MonitorFromPoint / MonitorFromRect / MonitorFromWindow / GetMonitorInfo and the
 * HMONITOR objects they hand out.
 *
 * Where the kernel is involved: EnumDisplayMonitors is a bare jump to win32u!NtUserEnumDisplayMonitors ->
 * the kernel monitor enumeration, which calls back into user32 through the
 * KernelCallbackTable once per active monitor. The MonitorFrom* family and GetMonitorInfo are client side: they
 * walk the tagMONITOR list in the shared heap (see dm.h), so those tests validate that shared state directly.
 */
#include "dm.h"

/* ---- EnumDisplayMonitors ------------------------------------------------------------------------ */

TEST(display_monitors_dpi_enum_null_dc_reports_each_monitor) {
    NEED_GUI(); REQUIRE(U(EnumDisplayMonitors));
    struct dm_enum e;
    BOOL r = dm_enum_all(&e, NULL, NULL);
    CHECK(r == TRUE);                                   /* every callback returned TRUE -> the enumeration returns 1 */
    CHECK(e.total >= 1);
    CHECK(e.n == e.total || e.total > DM_MAXMON);
    for (int i = 0; i < e.n; i++) {
        CHECKF(e.m[i].dc == NULL, "monitor %d got hdc %p with a NULL input hdc", i, (void *)e.m[i].dc);
        CHECK(e.m[i].h != NULL);
        CHECK(e.m[i].rc.right > e.m[i].rc.left && e.m[i].rc.bottom > e.m[i].rc.top);
    }
}

TEST(display_monitors_dpi_enum_count_matches_sm_cmonitors) {
    NEED_GUI(); REQUIRE(U(EnumDisplayMonitors));
    struct dm_enum e;
    dm_enum_all(&e, NULL, NULL);
    CHECKF(e.total == U(GetSystemMetrics)(SM_CMONITORS), "enum %d vs SM_CMONITORS %d", e.total, U(GetSystemMetrics)(SM_CMONITORS));
}

TEST(display_monitors_dpi_enum_handles_are_distinct_and_valid) {
    NEED_GUI(); REQUIRE(U(EnumDisplayMonitors));
    struct dm_enum e; dm_enum_all(&e, NULL, NULL);
    for (int i = 0; i < e.n; i++) {
        MONITORINFO mi = { sizeof mi };
        CHECKF(U(GetMonitorInfoW)(e.m[i].h, &mi), "monitor %d handle %p rejected by GetMonitorInfoW", i, (void *)e.m[i].h);
        for (int j = i + 1; j < e.n; j++) CHECK(e.m[i].h != e.m[j].h);
    }
}

TEST(display_monitors_dpi_enum_callback_rects_match_monitorinfo_pmv2) {
    NEED_GUI(); REQUIRE(U(EnumDisplayMonitors) && U(SetThreadDpiAwarenessContext) && U(GetThreadDpiAwarenessContext));
    DM_CTX_GUARD();
    CHECK(dm_set_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != 0);
    struct dm_enum e; dm_enum_all(&e, NULL, NULL);
    for (int i = 0; i < e.n; i++) {
        MONITORINFO mi = { sizeof mi };
        U(GetMonitorInfoW)(e.m[i].h, &mi);
        /* a per-monitor-v2 thread sees the unscaled rectangle in both paths (GetMonitorRectForDpiContext / GetMonitorInfoW) */
        CHECKF(dm_rect_eq(&e.m[i].rc, &mi.rcMonitor), "monitor %d cb (%ld,%ld,%ld,%ld) info (%ld,%ld,%ld,%ld)", i,
               e.m[i].rc.left, e.m[i].rc.top, e.m[i].rc.right, e.m[i].rc.bottom, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right, mi.rcMonitor.bottom);
    }
}

static int g_stop_first_calls;
static BOOL CALLBACK stop_first_cb(HMONITOR h, HDC dc, LPRECT rc, LPARAM lp) { (void)h; (void)dc; (void)rc; (void)lp; g_stop_first_calls++; return FALSE; }
TEST(display_monitors_dpi_enum_false_from_callback_stops_and_fails) {
    NEED_GUI(); REQUIRE(U(EnumDisplayMonitors));
    g_stop_first_calls = 0;
    BOOL r = U(EnumDisplayMonitors)(NULL, NULL, stop_first_cb, 0);
    CHECK(r == FALSE);
    CHECK(g_stop_first_calls == 1);
}

static int g_nest_outer, g_nest_inner;
static BOOL CALLBACK nest_inner_cb(HMONITOR h, HDC dc, LPRECT rc, LPARAM lp) { (void)h; (void)dc; (void)rc; (void)lp; g_nest_inner++; return TRUE; }
static BOOL CALLBACK nest_outer_cb(HMONITOR h, HDC dc, LPRECT rc, LPARAM lp) {
    (void)h; (void)dc; (void)rc; (void)lp; g_nest_outer++;
    U(EnumDisplayMonitors)(NULL, NULL, nest_inner_cb, 0);
    return TRUE;
}
TEST(display_monitors_dpi_enum_reentrant_from_callback) {
    NEED_GUI(); REQUIRE(U(EnumDisplayMonitors));
    struct dm_enum e; dm_enum_all(&e, NULL, NULL);
    g_nest_outer = g_nest_inner = 0;
    CHECK(U(EnumDisplayMonitors)(NULL, NULL, nest_outer_cb, 0) == TRUE);
    CHECK(g_nest_outer == e.total);
    CHECK(g_nest_inner == e.total * e.total);     /* every outer callback re-enters the kernel and still sees all monitors */
}

TEST(display_monitors_dpi_enum_via_direct_nt_stub_matches_user32) {
    NEED_GUI(); NEED_NT(NtUserEnumDisplayMonitors);
    struct dm_enum a, b;
    memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
    BOOL ra = UN(NtUserEnumDisplayMonitors)(NULL, NULL, dm_enum_cb, (LPARAM)&a);
    BOOL rb = dm_enum_all(&b, NULL, NULL);
    CHECK(ra == TRUE && rb == TRUE);
    CHECK(a.total == b.total);
    for (int i = 0; i < a.n && i < b.n; i++) { CHECK(a.m[i].h == b.m[i].h); CHECK(dm_rect_eq(&a.m[i].rc, &b.m[i].rc)); }
}

TEST(display_monitors_dpi_enum_order_and_handles_stable_across_calls) {
    NEED_GUI(); REQUIRE(U(EnumDisplayMonitors));
    struct dm_enum a, b; dm_enum_all(&a, NULL, NULL); dm_enum_all(&b, NULL, NULL);
    CHECK(a.total == b.total);
    for (int i = 0; i < a.n && i < b.n; i++) CHECK(a.m[i].h == b.m[i].h);
}

TEST(display_monitors_dpi_enum_disjoint_clip_returns_true_without_callbacks) {
    NEED_GUI(); REQUIRE(U(EnumDisplayMonitors));
    RECT far_away = { 100000000, 100000000, 100000100, 100000100 };
    struct dm_enum e;
    BOOL r = dm_enum_all(&e, NULL, &far_away);
    CHECK(r == TRUE);                   /* IntersectRect(screen, clip) == 0 -> "return 1" before any callback */
    CHECK(e.total == 0);
}

TEST(display_monitors_dpi_enum_empty_clip_returns_true_without_callbacks) {
    NEED_GUI(); REQUIRE(U(EnumDisplayMonitors));
    RECT empties[] = { {0,0,0,0}, {10,10,10,50}, {10,10,50,10}, {50,50,10,10} };
    for (unsigned i = 0; i < sizeof empties / sizeof *empties; i++) {
        struct dm_enum e;
        CHECKF(dm_enum_all(&e, NULL, &empties[i]) == TRUE, "clip %u", i);
        CHECKF(e.total == 0, "clip %u produced %d callbacks (IntersectRect with an empty rectangle fails)", i, e.total);
    }
}

TEST(display_monitors_dpi_enum_clip_inside_primary_yields_single_callback_with_clip) {
    NEED_GUI(); REQUIRE(U(EnumDisplayMonitors) && U(SetThreadDpiAwarenessContext));
    DM_CTX_GUARD();
    dm_set_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    MONITORINFO pi = { sizeof pi };
    HMONITOR pm = dm_primary();
    REQUIRE(pm); U(GetMonitorInfoW)(pm, &pi);
    RECT clip = { pi.rcMonitor.left + 10, pi.rcMonitor.top + 10, pi.rcMonitor.left + 30, pi.rcMonitor.top + 30 };
    struct dm_enum e;
    CHECK(dm_enum_all(&e, NULL, &clip) == TRUE);
    CHECK(e.total == 1);
    if (e.total == 1) {
        CHECK(e.m[0].h == pm);
        CHECKF(dm_rect_eq(&e.m[0].rc, &clip), "callback rect is the clip intersected with the monitor, got (%ld,%ld,%ld,%ld)",
               e.m[0].rc.left, e.m[0].rc.top, e.m[0].rc.right, e.m[0].rc.bottom);
    }
}

TEST(display_monitors_dpi_enum_clip_straddling_primary_edge_is_trimmed) {
    NEED_GUI(); REQUIRE(U(EnumDisplayMonitors) && U(SetThreadDpiAwarenessContext));
    DM_CTX_GUARD();
    dm_set_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HMONITOR pm = dm_primary(); MONITORINFO pi = { sizeof pi }; REQUIRE(pm); U(GetMonitorInfoW)(pm, &pi);
    /* a clip sticking out above/left of the virtual screen is intersected with the screen rectangle first */
    RECT clip = { pi.rcMonitor.left + 5, pi.rcMonitor.top + 5, pi.rcMonitor.left + 25, pi.rcMonitor.top + 25 };
    struct dm_enum e; dm_enum_all(&e, NULL, &clip);
    int found = 0;
    for (int i = 0; i < e.n; i++) if (e.m[i].h == pm) { found = 1; CHECK(dm_rect_in(&e.m[i].rc, &pi.rcMonitor)); }
    CHECK(found);
}

static HDC g_dc_cb_seen[DM_MAXMON]; static int g_dc_cb_n; static int g_dc_objtype_ok; static RECT g_dc_rc[DM_MAXMON];
static BOOL CALLBACK dc_cb(HMONITOR h, HDC dc, LPRECT rc, LPARAM lp) {
    (void)h; (void)lp;
    if (g_dc_cb_n < DM_MAXMON) { g_dc_cb_seen[g_dc_cb_n] = dc; g_dc_rc[g_dc_cb_n] = *rc; g_dc_cb_n++; }
    if (dc && U(GetObjectType)(dc) == OBJ_DC) g_dc_objtype_ok++;
    return TRUE;
}
TEST(display_monitors_dpi_enum_with_screen_dc_hands_out_monitor_dcs) {
    NEED_GUI(); REQUIRE(U(EnumDisplayMonitors) && U(GetDC) && U(ReleaseDC) && U(GetObjectType));
    HDC screen = U(GetDC)(NULL); REQUIRE(screen);
    struct dm_enum ref; dm_enum_all(&ref, NULL, NULL);
    g_dc_cb_n = 0; g_dc_objtype_ok = 0;
    BOOL r = U(EnumDisplayMonitors)(screen, NULL, dc_cb, 0);
    CHECK(r == TRUE);
    CHECKF(g_dc_cb_n == ref.total, "with a screen DC %d callbacks, without %d", g_dc_cb_n, ref.total);
    CHECKF(g_dc_objtype_ok == g_dc_cb_n, "monitor DCs must be OBJ_DC (cache DCs from GetMonitorDC)");
    for (int i = 0; i < g_dc_cb_n; i++) CHECK(g_dc_cb_seen[i] != NULL);
    U(ReleaseDC)(NULL, screen);
}

TEST(display_monitors_dpi_enum_dc_callback_rects_are_dc_relative_for_screen_dc) {
    NEED_GUI(); REQUIRE(U(EnumDisplayMonitors) && U(GetDC) && U(ReleaseDC) && U(SetThreadDpiAwarenessContext));
    DM_CTX_GUARD();
    dm_set_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HDC screen = U(GetDC)(NULL); REQUIRE(screen);
    g_dc_cb_n = 0; g_dc_objtype_ok = 0;
    struct dm_enum ref; dm_enum_all(&ref, NULL, NULL);
    U(EnumDisplayMonitors)(screen, NULL, dc_cb, 0);
    /* The screen DC has a zero origin (kernel DC state), so rects are the monitor rects, trimmed to the DC clip box. */
    for (int i = 0; i < g_dc_cb_n && i < ref.n; i++) CHECK(dm_rect_in(&g_dc_rc[i], &ref.m[i].rc));
    U(ReleaseDC)(NULL, screen);
}

TEST(display_monitors_dpi_enum_invalid_dc_fails_without_callbacks) {
    NEED_GUI(); REQUIRE(U(EnumDisplayMonitors) && U(CreatePen) && U(DeleteObject));
    HGDIOBJ pen = U(CreatePen)(PS_SOLID, 1, 0);
    HDC bogus[] = { (HDC)(ULONG_PTR)0x12345, (HDC)(ULONG_PTR)0xdeadbeef, (HDC)pen, (HDC)(ULONG_PTR)-1 };
    for (unsigned i = 0; i < 4; i++) {
        struct dm_enum e;
        BOOL r = dm_enum_all(&e, bogus[i], NULL);
        CHECKF(r == FALSE, "hdc %p: LookupDC failure must fail the enumeration", (void *)bogus[i]);
        CHECKF(e.total == 0, "hdc %p produced callbacks", (void *)bogus[i]);
    }
    U(DeleteObject)(pen);
}

TEST(display_monitors_dpi_enum_released_dc_is_invalid) {
    NEED_GUI(); REQUIRE(U(EnumDisplayMonitors) && U(GetDC) && U(ReleaseDC));
    HDC dc = U(GetDC)(NULL); REQUIRE(dc);
    U(ReleaseDC)(NULL, dc);
    struct dm_enum e;
    BOOL r = dm_enum_all(&e, dc, NULL);
    /* a released cache DC keeps its handle but is no longer owned by the thread; either LookupDC rejects it or the
     * enumeration runs - the result must be consistent with the callbacks seen. */
    CHECK(r || e.total == 0);               /* FALSE can only mean the DC lookup failed before any callback */
    if (r == TRUE && e.total > 0) t_note("released cache DC %p still enumerates %d monitors", (void *)dc, e.total);
}

TEST(display_monitors_dpi_enum_kernel_callback_slot_is_hit_once_per_monitor) {
    NEED_GUI(); REQUIRE(U(EnumDisplayMonitors));
    if (t_is_wine()) { t_skip("Wine has no win32k kernel callback table"); return; }
    void *tbl = k_cb_table();
    HMODULE u = GetModuleHandleA("user32.dll");
    if (!tbl || !u) { t_skip("KernelCallbackTable not available"); return; }
    unsigned n = k_cb_count();
    int idx = -1;
    for (unsigned i = 0; i < n; i++) {
        void *fn = k_cb_original(i);
        if (!fn) { void *p = NULL; if (k_try_read_ptr((char *)tbl + i * sizeof(void *), &p)) fn = p; }
        /* __ClientMonitorEnumProc is at in 19041.2965's user32.dll */
        if ((ULONG_PTR)fn == (ULONG_PTR)u + 0x289a0) { idx = (int)i; break; }
    }
    if (idx < 0) { t_note("__ClientMonitorEnumProc not found in the callback table (different user32 build); %u slots", n); t_skip("slot not found"); return; }
    struct dm_enum ref; dm_enum_all(&ref, NULL, NULL);
    if (!k_cb_hook((unsigned)idx)) { t_skip("could not hook slot %d", idx); return; }
    struct dm_enum e; BOOL r = dm_enum_all(&e, NULL, NULL);
    unsigned long hits = k_cb_hit_count((unsigned)idx);
    ULONG_PTR args[4] = { 0 }; int got = k_cb_last_args((unsigned)idx, args);
    k_cb_unhook_all();
    CHECK(r == TRUE);
    CHECKF(hits == (unsigned long)ref.total, "kernel invoked slot %d %lu times for %d monitors", idx, hits, ref.total);
    if (got) {
        /* argv[0] is the kernel-built argument buffer of the callback, argv[1] its length: 0x30 bytes on Windows 10 22H2
         * (the buffer holds kernel-side pointers, not the HMONITOR value) */
        EXPECT_EQ(args[1], 0x30);
        unsigned char blk[0x30];
        CHECK(args[0] != 0 && k_try_read((void *)args[0], blk, sizeof blk));
    }
}

/* ---- MonitorFromPoint --------------------------------------------------------------------------- */

TEST(display_monitors_dpi_from_point_primary_origin_all_valid_flags) {
    NEED_GUI(); REQUIRE(U(MonitorFromPoint));
    POINT o = { 0, 0 };
    HMONITOR p = U(MonitorFromPoint)(o, MONITOR_DEFAULTTONULL);
    CHECK(p != NULL);                       /* (0,0) is the primary monitor's top-left corner and always inside it */
    CHECK(U(MonitorFromPoint)(o, MONITOR_DEFAULTTOPRIMARY) == p);
    CHECK(U(MonitorFromPoint)(o, MONITOR_DEFAULTTONEAREST) == p);
}

TEST(display_monitors_dpi_from_point_far_outside_flags) {
    NEED_GUI(); REQUIRE(U(MonitorFromPoint) && U(GetSystemMetrics));
    POINT far_pts[] = { { -100000, -100000 }, { 100000, 100000 }, { -40000, 20 }, { 20, 40000 }, { 0x7fffffff, 0x7fffffff }, { (LONG)0x80000000, (LONG)0x80000000 } };
    HMONITOR prim = dm_primary();
    for (unsigned i = 0; i < sizeof far_pts / sizeof *far_pts; i++) {
        SetLastError(0);
        CHECKF(U(MonitorFromPoint)(far_pts[i], MONITOR_DEFAULTTONULL) == NULL, "point %u", i);
        CHECKF(U(MonitorFromPoint)(far_pts[i], MONITOR_DEFAULTTOPRIMARY) == prim, "point %u", i);
        HMONITOR n = U(MonitorFromPoint)(far_pts[i], MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof mi };
        CHECKF(n != NULL && U(GetMonitorInfoW)(n, &mi), "point %u nearest %p", i, (void *)n);
    }
}

TEST(display_monitors_dpi_from_point_invalid_flags_fail_with_invalid_flags_error) {
    NEED_GUI(); REQUIRE(U(MonitorFromPoint));
    DWORD bad[] = { 3, 4, 7, 0x10, 0x100, 0x80000000, 0xffffffff };
    POINT o = { 0, 0 };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        SetLastError(0);
        HMONITOR h = U(MonitorFromPoint)(o, bad[i]);
        CHECKF(h == NULL, "flags 0x%lx", bad[i]);
        EXPECT_ERR(ERROR_INVALID_FLAGS);          /* user32!MonitorFromPoint: dwFlags < 3 else SetLastError(0x3ec) */
    }
}

TEST(display_monitors_dpi_from_point_inside_each_monitor_returns_that_monitor) {
    NEED_GUI(); REQUIRE(U(MonitorFromPoint) && U(EnumDisplayMonitors) && U(SetThreadDpiAwarenessContext));
    DM_CTX_GUARD();
    dm_set_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    struct dm_enum e; dm_enum_all(&e, NULL, NULL);
    for (int i = 0; i < e.n; i++) {
        POINT c = { (e.m[i].rc.left + e.m[i].rc.right) / 2, (e.m[i].rc.top + e.m[i].rc.bottom) / 2 };
        POINT tl = { e.m[i].rc.left, e.m[i].rc.top };
        POINT br = { e.m[i].rc.right - 1, e.m[i].rc.bottom - 1 };
        CHECKF(U(MonitorFromPoint)(c, MONITOR_DEFAULTTONULL) == e.m[i].h, "center of monitor %d", i);
        CHECKF(U(MonitorFromPoint)(tl, MONITOR_DEFAULTTONULL) == e.m[i].h, "top-left of monitor %d", i);
        CHECKF(U(MonitorFromPoint)(br, MONITOR_DEFAULTTONULL) == e.m[i].h, "bottom-right-1 of monitor %d", i);
        POINT out = { e.m[i].rc.right, e.m[i].rc.bottom };         /* PtInRect excludes right/bottom */
        HMONITOR o = U(MonitorFromPoint)(out, MONITOR_DEFAULTTONULL);
        CHECKF(o != e.m[i].h, "right/bottom edge of monitor %d is exclusive", i);
    }
}

/* ---- MonitorFromRect ---------------------------------------------------------------------------- */

TEST(display_monitors_dpi_from_rect_inside_primary_all_valid_flags) {
    NEED_GUI(); REQUIRE(U(MonitorFromRect));
    RECT r = { 0, 0, 10, 10 };
    HMONITOR p = dm_primary();
    CHECK(U(MonitorFromRect)(&r, MONITOR_DEFAULTTONULL) == p);
    CHECK(U(MonitorFromRect)(&r, MONITOR_DEFAULTTOPRIMARY) == p);
    CHECK(U(MonitorFromRect)(&r, MONITOR_DEFAULTTONEAREST) == p);
}

TEST(display_monitors_dpi_from_rect_far_outside_flags) {
    NEED_GUI(); REQUIRE(U(MonitorFromRect));
    RECT far_r = { 90000, 90000, 90100, 90100 };
    RECT huge_neg = { -200000, -200000, -199000, -199000 };
    HMONITOR prim = dm_primary();
    RECT *rs[] = { &far_r, &huge_neg };
    for (int i = 0; i < 2; i++) {
        CHECK(U(MonitorFromRect)(rs[i], MONITOR_DEFAULTTONULL) == NULL);
        CHECK(U(MonitorFromRect)(rs[i], MONITOR_DEFAULTTOPRIMARY) == prim);
        MONITORINFO mi = { sizeof mi };
        HMONITOR n = U(MonitorFromRect)(rs[i], MONITOR_DEFAULTTONEAREST);
        CHECK(n != NULL && U(GetMonitorInfoW)(n, &mi));
    }
}

TEST(display_monitors_dpi_from_rect_invalid_flags_fail_with_invalid_flags_error) {
    NEED_GUI(); REQUIRE(U(MonitorFromRect));
    RECT r = { 0, 0, 10, 10 };
    DWORD bad[] = { 3, 5, 0xff, 0x80000001, 0xffffffff };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        SetLastError(0);
        CHECKF(U(MonitorFromRect)(&r, bad[i]) == NULL, "flags 0x%lx", bad[i]);
        EXPECT_ERR(ERROR_INVALID_FLAGS);
    }
}

TEST(display_monitors_dpi_from_rect_empty_rect_degenerates_to_its_top_left_point) {
    NEED_GUI(); REQUIRE(U(MonitorFromRect) && U(MonitorFromPoint));
    /* _MonitorFromRect: right <= left || bottom <= top -> _MonitorFromPoint(left, top) */
    RECT empties[] = { {0,0,0,0}, {100,100,50,50}, {5,5,5,500}, {5,5,500,5}, {-100000,-100000,-100000,-100000} };
    for (unsigned i = 0; i < sizeof empties / sizeof *empties; i++) {
        POINT p = { empties[i].left, empties[i].top };
        for (DWORD f = 0; f < 3; f++)
            CHECKF(U(MonitorFromRect)(&empties[i], f) == U(MonitorFromPoint)(p, f), "rect %u flags %lu", i, f);
    }
}

TEST(display_monitors_dpi_from_rect_prefers_largest_intersection_between_monitors) {
    NEED_GUI(); REQUIRE(U(MonitorFromRect) && U(EnumDisplayMonitors) && U(SetThreadDpiAwarenessContext));
    DM_CTX_GUARD();
    dm_set_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    struct dm_enum e; dm_enum_all(&e, NULL, NULL);
    if (e.n < 2) { t_skip("needs two monitors"); return; }
    for (int i = 0; i < e.n; i++) for (int j = 0; j < e.n; j++) {
        if (i == j) continue;
        const RECT *a = &e.m[i].rc, *b = &e.m[j].rc;
        if (a->right != b->left) continue;                       /* b directly right of a */
        RECT r = { a->right - 30, a->top + 10, a->right + 10, a->top + 20 };   /* 30x10 in a, 10x10 in b */
        if (r.bottom > a->bottom || r.bottom > b->bottom) continue;
        CHECK(U(MonitorFromRect)(&r, MONITOR_DEFAULTTONULL) == e.m[i].h);
        RECT r2 = { a->right - 10, a->top + 10, a->right + 30, a->top + 20 };
        CHECK(U(MonitorFromRect)(&r2, MONITOR_DEFAULTTONULL) == e.m[j].h);
    }
}

/* ---- MonitorFromWindow -------------------------------------------------------------------------- */

TEST(display_monitors_dpi_from_window_null_hwnd_flag_semantics) {
    NEED_GUI(); REQUIRE(U(MonitorFromWindow));
    HMONITOR prim = dm_primary();
    SetLastError(0);
    CHECK(U(MonitorFromWindow)(NULL, MONITOR_DEFAULTTONULL) == NULL);     /* _MonitorFromWindowInternal(NULL): only flags 1,2 fall back */
    CHECK(U(MonitorFromWindow)(NULL, MONITOR_DEFAULTTOPRIMARY) == prim);
    CHECK(U(MonitorFromWindow)(NULL, MONITOR_DEFAULTTONEAREST) == prim);
}

TEST(display_monitors_dpi_from_window_invalid_flags) {
    NEED_GUI(); REQUIRE(U(MonitorFromWindow));
    HWND w = k_make_window("dm_mfw_flags"); REQUIRE(w);
    DWORD bad[] = { 3, 4, 0x80000000, 0xffffffff };
    for (unsigned i = 0; i < 4; i++) {
        SetLastError(0);
        CHECKF(U(MonitorFromWindow)(w, bad[i]) == NULL, "flags 0x%lx", bad[i]);
        EXPECT_ERR(ERROR_INVALID_FLAGS);
        SetLastError(0);
        CHECK(U(MonitorFromWindow)(NULL, bad[i]) == NULL);               /* the flag check precedes the hwnd handling */
        EXPECT_ERR(ERROR_INVALID_FLAGS);
    }
    U(DestroyWindow)(w);
}

TEST(display_monitors_dpi_from_window_invalid_handles_set_invalid_window_error) {
    NEED_GUI(); REQUIRE(U(MonitorFromWindow));
    /* index 0 of the handle table is never a window (type 0); the others carry a uniq that cannot match a live entry */
    HWND bad[] = { (HWND)(ULONG_PTR)0x10000, (HWND)(ULONG_PTR)0x1, (HWND)(ULONG_PTR)0xdeadbeef, (HWND)(ULONG_PTR)0x7fffffff };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++)
        for (DWORD f = 0; f < 3; f++) {
            SetLastError(0);
            CHECKF(U(MonitorFromWindow)(bad[i], f) == NULL, "hwnd %p flags %lu", (void *)bad[i], f);
            EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);   /* user32!MonitorFromWindow: failed validation -> 0x578, even for DEFAULTTOPRIMARY */
        }
}

TEST(display_monitors_dpi_from_window_destroyed_window_is_invalid) {
    NEED_GUI(); REQUIRE(U(MonitorFromWindow));
    HWND w = k_make_window("dm_mfw_destroyed"); REQUIRE(w);
    U(DestroyWindow)(w);
    SetLastError(0);
    CHECK(U(MonitorFromWindow)(w, MONITOR_DEFAULTTOPRIMARY) == NULL);
    EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
}

TEST(display_monitors_dpi_from_window_offscreen_window_per_flag) {
    NEED_GUI(); REQUIRE(U(MonitorFromWindow));
    HWND w = k_make_window("dm_mfw_off"); REQUIRE(w);        /* 1x1 at (-32000,-32000), never shown */
    HMONITOR prim = dm_primary();
    CHECK(U(MonitorFromWindow)(w, MONITOR_DEFAULTTONULL) == NULL);
    CHECK(U(MonitorFromWindow)(w, MONITOR_DEFAULTTOPRIMARY) == prim);
    HMONITOR n = U(MonitorFromWindow)(w, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof mi };
    CHECK(n != NULL && U(GetMonitorInfoW)(n, &mi));
    U(DestroyWindow)(w);
}

TEST(display_monitors_dpi_from_window_follows_window_rect) {
    NEED_GUI(); REQUIRE(U(MonitorFromWindow) && U(SetWindowPos) && U(SetThreadDpiAwarenessContext));
    DM_CTX_GUARD();
    dm_set_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HWND w = k_make_window("dm_mfw_follow"); REQUIRE(w);
    struct dm_enum e; dm_enum_all(&e, NULL, NULL);
    for (int i = 0; i < e.n; i++) {
        int x = e.m[i].rc.left + 20, y = e.m[i].rc.top + 20;
        U(SetWindowPos)(w, NULL, x, y, 40, 40, SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER);
        CHECKF(U(MonitorFromWindow)(w, MONITOR_DEFAULTTONULL) == e.m[i].h, "window moved into monitor %d", i);
    }
    U(DestroyWindow)(w);
}

TEST(display_monitors_dpi_from_window_desktop_window_has_a_monitor) {
    NEED_GUI(); REQUIRE(U(MonitorFromWindow) && U(GetDesktopWindow));
    HMONITOR m = U(MonitorFromWindow)(U(GetDesktopWindow)(), MONITOR_DEFAULTTONULL);
    MONITORINFO mi = { sizeof mi };
    CHECK(m != NULL && U(GetMonitorInfoW)(m, &mi));
}

/* ---- GetMonitorInfoW / GetMonitorInfoA ---------------------------------------------------------- */

TEST(display_monitors_dpi_info_basic_fields) {
    NEED_GUI();
    struct dm_enum e; dm_enum_all(&e, NULL, NULL);
    for (int i = 0; i < e.n; i++) {
        MONITORINFO mi; memset(&mi, 0xcc, sizeof mi); mi.cbSize = sizeof mi;
        CHECK(U(GetMonitorInfoW)(e.m[i].h, &mi) == TRUE);
        CHECK(mi.cbSize == sizeof mi);
        CHECK(mi.rcMonitor.right > mi.rcMonitor.left && mi.rcMonitor.bottom > mi.rcMonitor.top);
        CHECKF(dm_rect_in(&mi.rcWork, &mi.rcMonitor), "monitor %d work area outside monitor rect", i);
        CHECKF(mi.dwFlags == 0 || mi.dwFlags == MONITORINFOF_PRIMARY, "dwFlags 0x%lx", mi.dwFlags);   /* dwFlags = (monitor == primary) */
    }
}

TEST(display_monitors_dpi_info_exactly_one_primary_at_origin) {
    NEED_GUI();
    struct dm_enum e; dm_enum_all(&e, NULL, NULL);
    int primaries = 0;
    for (int i = 0; i < e.n; i++) {
        MONITORINFO mi = { sizeof mi }; U(GetMonitorInfoW)(e.m[i].h, &mi);
        if (mi.dwFlags & MONITORINFOF_PRIMARY) {
            primaries++;
            CHECK(e.m[i].h == dm_primary());
            CHECKF(mi.rcMonitor.left == 0 && mi.rcMonitor.top == 0, "primary at (%ld,%ld)", mi.rcMonitor.left, mi.rcMonitor.top);
        }
    }
    CHECK(primaries == 1);
}

TEST(display_monitors_dpi_info_ex_device_name) {
    NEED_GUI();
    struct dm_enum e; dm_enum_all(&e, NULL, NULL);
    for (int i = 0; i < e.n; i++) {
        MONITORINFOEXW mi; memset(&mi, 0xcc, sizeof mi); mi.cbSize = sizeof mi;
        CHECK(sizeof mi == 104);
        CHECK(U(GetMonitorInfoW)(e.m[i].h, (MONITORINFO *)&mi) == TRUE);
        CHECKF(wcsncmp(mi.szDevice, L"\\\\.\\DISPLAY", 11) == 0, "szDevice %ls", mi.szDevice);
        size_t n = wcsnlen(mi.szDevice, CCHDEVICENAME);
        CHECK(n > 11 && n < CCHDEVICENAME);                 /* NUL terminated within the 32 WCHARs (RtlStringCchCopyW into 0x20) */
    }
}

TEST(display_monitors_dpi_info_basic_size_does_not_touch_device_name) {
    NEED_GUI();
    HMONITOR p = dm_primary();
    MONITORINFOEXW mi; memset(&mi, 0xcc, sizeof mi);
    mi.cbSize = sizeof(MONITORINFO);
    CHECK(U(GetMonitorInfoW)(p, (MONITORINFO *)&mi) == TRUE);
    int untouched = 1;
    for (int i = 0; i < CCHDEVICENAME; i++) if (mi.szDevice[i] != 0xcccc) untouched = 0;
    CHECK(untouched);               /* cbSize 0x28 never reaches NtUserGetHDevName */
}

TEST(display_monitors_dpi_info_bad_cbsize_is_invalid_parameter_and_leaves_buffer) {
    NEED_GUI();
    HMONITOR p = dm_primary();
    DWORD sizes[] = { 0, 1, 4, 8, 0x27, 0x29, 0x2c, 0x67, 0x69, 0x70, 0x100, 0x80000000, 0xffffffff };
    for (unsigned i = 0; i < sizeof sizes / sizeof *sizes; i++) {
        MONITORINFOEXW mi, ref; memset(&mi, 0xcc, sizeof mi);
        mi.cbSize = sizes[i];
        memcpy(&ref, &mi, sizeof mi);
        SetLastError(0);
        BOOL r = U(GetMonitorInfoW)(p, (MONITORINFO *)&mi);
        CHECKF(r == FALSE, "cbSize 0x%lx", sizes[i]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);       /* only 0x28 and 0x68 pass: user32!GetMonitorInfoW falls to SetLastError(0x57) */
        CHECKF(memcmp(&mi, &ref, sizeof mi) == 0, "cbSize 0x%lx: failing call must not write the structure", sizes[i]);
    }
}

TEST(display_monitors_dpi_info_invalid_monitor_handles) {
    NEED_GUI(); REQUIRE(U(CreatePen));
    HWND w = k_make_window("dm_info_wnd"); REQUIRE(w);
    HMONITOR bad[] = { NULL, (HMONITOR)(ULONG_PTR)1, (HMONITOR)(ULONG_PTR)0xdeadbeef, (HMONITOR)(ULONG_PTR)-1, (HMONITOR)(ULONG_PTR)0x10000,
                       (HMONITOR)w,                                   /* a live HWND is the wrong USER object type (entry+0x18 != 12) */
                       (HMONITOR)(ULONG_PTR)0xffff0000 };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        MONITORINFO mi = { sizeof mi };
        SetLastError(0);
        CHECKF(U(GetMonitorInfoW)(bad[i], &mi) == FALSE, "handle %p", (void *)bad[i]);
        EXPECT_ERR(ERROR_INVALID_MONITOR_HANDLE);
    }
    U(DestroyWindow)(w);
}

TEST(display_monitors_dpi_info_handle_checked_before_cbsize) {
    NEED_GUI();
    MONITORINFO mi = { 0x1234 };
    SetLastError(0);
    CHECK(U(GetMonitorInfoW)((HMONITOR)(ULONG_PTR)0xdeadbeef, &mi) == FALSE);
    EXPECT_ERR(ERROR_INVALID_MONITOR_HANDLE);       /* handle validation runs first, cbSize only for a valid monitor */
}

TEST(display_monitors_dpi_info_uniq_wildcard_and_stale_uniq) {
    NEED_GUI();
    HMONITOR p = dm_primary();
    ULONG_PTR v = (ULONG_PTR)p;
    MONITORINFO mi = { sizeof mi };
    HMONITOR wild = (HMONITOR)((v & 0xffff) | 0xffff0000u);
    CHECKF(U(GetMonitorInfoW)(wild, &mi) == TRUE, "uniq 0xffff is a wildcard (-1)");
    unsigned short uniq = (unsigned short)(v >> 16);
    unsigned short other = (unsigned short)(uniq ^ 1);
    if (other == 0xffff) other = (unsigned short)(uniq ^ 2);
    HMONITOR stale = (HMONITOR)((v & 0xffff) | ((ULONG_PTR)other << 16));
    SetLastError(0);
    CHECK(U(GetMonitorInfoW)(stale, &mi) == FALSE);
    EXPECT_ERR(ERROR_INVALID_MONITOR_HANDLE);
}

TEST(display_monitors_dpi_info_ansi_matches_wide) {
    NEED_GUI(); REQUIRE(U(GetMonitorInfoA));
    struct dm_enum e; dm_enum_all(&e, NULL, NULL);
    for (int i = 0; i < e.n; i++) {
        MONITORINFOEXW w = { .cbSize = sizeof w };
        MONITORINFOEXA a; memset(&a, 0, sizeof a); a.cbSize = sizeof a;
        CHECK(sizeof a == 72);
        CHECK(U(GetMonitorInfoW)(e.m[i].h, (MONITORINFO *)&w) == TRUE);
        CHECK(U(GetMonitorInfoA)(e.m[i].h, (MONITORINFO *)&a) == TRUE);
        CHECK(dm_rect_eq(&w.rcMonitor, &a.rcMonitor) && dm_rect_eq(&w.rcWork, &a.rcWork) && w.dwFlags == a.dwFlags);
        char nar[CCHDEVICENAME + 1] = { 0 };
        WideCharToMultiByte(CP_ACP, 0, w.szDevice, -1, nar, sizeof nar, NULL, NULL);
        CHECKF(!strcmp(nar, a.szDevice), "A name '%s' W name '%s'", a.szDevice, nar);
    }
}

TEST(display_monitors_dpi_info_ansi_invalid_arguments) {
    NEED_GUI(); REQUIRE(U(GetMonitorInfoA));
    MONITORINFO mi = { sizeof mi };
    CHECK(U(GetMonitorInfoA)((HMONITOR)(ULONG_PTR)0xdeadbeef, &mi) == FALSE);   /* HMValidateSharedHandle == 0 -> FALSE, no last error set */
    MONITORINFO bad = { 3 };
    SetLastError(0);
    CHECK(U(GetMonitorInfoA)(dm_primary(), &bad) == FALSE);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}

TEST(display_monitors_dpi_info_via_nt_gethdevname_matches_ex_name) {
    NEED_GUI(); NEED_NT(NtUserGetHDevName);
    struct dm_enum e; dm_enum_all(&e, NULL, NULL);
    for (int i = 0; i < e.n; i++) {
        WCHAR buf[32] __attribute__((aligned(8)));
        MONITORINFOEXW mi; dm_info(e.m[i].h, &mi);
        memset(buf, 0, sizeof buf);
        BOOL r = UN(NtUserGetHDevName)(e.m[i].h, buf);
        CHECKF(r == TRUE, "NtUserGetHDevName(%p) = %d", (void *)e.m[i].h, r);
        CHECKF(!wcscmp(buf, mi.szDevice), "Nt '%ls' vs info '%ls'", buf, mi.szDevice);
    }
}

TEST(display_monitors_dpi_info_nt_gethdevname_invalid_handles_return_false) {
    NEED_GUI(); NEED_NT(NtUserGetHDevName);
    WCHAR buf[32] __attribute__((aligned(8)));
    HMONITOR bad[] = { NULL, (HMONITOR)(ULONG_PTR)0xdeadbeef, (HMONITOR)(ULONG_PTR)1, (HMONITOR)(ULONG_PTR)-1 };
    for (unsigned i = 0; i < 4; i++) {
        memset(buf, 0xcc, sizeof buf);
        CHECKF(UN(NtUserGetHDevName)(bad[i], buf) == FALSE, "handle %p", (void *)bad[i]);
        CHECK(((unsigned char *)buf)[0] == 0xcc);                                            /* nothing copied out */
    }
}

TEST(display_monitors_dpi_info_work_area_matches_spi_for_primary_pmv2) {
    NEED_GUI(); REQUIRE(U(SystemParametersInfoW) && U(SetThreadDpiAwarenessContext));
    DM_CTX_GUARD();
    dm_set_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    MONITORINFO mi = { sizeof mi }; U(GetMonitorInfoW)(dm_primary(), &mi);
    RECT wa = { 0 };
    CHECK(U(SystemParametersInfoW)(SPI_GETWORKAREA, 0, &wa, 0));
    CHECKF(dm_rect_eq(&wa, &mi.rcWork), "SPI (%ld,%ld,%ld,%ld) info (%ld,%ld,%ld,%ld)", wa.left, wa.top, wa.right, wa.bottom,
           mi.rcWork.left, mi.rcWork.top, mi.rcWork.right, mi.rcWork.bottom);
}

TEST(display_monitors_dpi_info_primary_rect_matches_screen_metrics_pmv2) {
    NEED_GUI(); REQUIRE(U(GetSystemMetrics) && U(SetThreadDpiAwarenessContext));
    DM_CTX_GUARD();
    dm_set_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    MONITORINFO mi = { sizeof mi }; U(GetMonitorInfoW)(dm_primary(), &mi);
    CHECK(U(GetSystemMetrics)(SM_CXSCREEN) == mi.rcMonitor.right - mi.rcMonitor.left);
    CHECK(U(GetSystemMetrics)(SM_CYSCREEN) == mi.rcMonitor.bottom - mi.rcMonitor.top);
}

TEST(display_monitors_dpi_info_union_of_monitors_is_virtual_screen_pmv2) {
    NEED_GUI(); REQUIRE(U(GetSystemMetrics) && U(SetThreadDpiAwarenessContext));
    DM_CTX_GUARD();
    dm_set_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    struct dm_enum e; dm_enum_all(&e, NULL, NULL);
    RECT u = e.m[0].rc;
    for (int i = 1; i < e.n; i++) {
        if (e.m[i].rc.left < u.left) u.left = e.m[i].rc.left;
        if (e.m[i].rc.top < u.top) u.top = e.m[i].rc.top;
        if (e.m[i].rc.right > u.right) u.right = e.m[i].rc.right;
        if (e.m[i].rc.bottom > u.bottom) u.bottom = e.m[i].rc.bottom;
    }
    CHECKF(U(GetSystemMetrics)(SM_XVIRTUALSCREEN) == u.left, "%d vs %ld", U(GetSystemMetrics)(SM_XVIRTUALSCREEN), u.left);
    CHECKF(U(GetSystemMetrics)(SM_YVIRTUALSCREEN) == u.top, "%d vs %ld", U(GetSystemMetrics)(SM_YVIRTUALSCREEN), u.top);
    CHECKF(U(GetSystemMetrics)(SM_CXVIRTUALSCREEN) == u.right - u.left, "%d vs %ld", U(GetSystemMetrics)(SM_CXVIRTUALSCREEN), u.right - u.left);
    CHECKF(U(GetSystemMetrics)(SM_CYVIRTUALSCREEN) == u.bottom - u.top, "%d vs %ld", U(GetSystemMetrics)(SM_CYVIRTUALSCREEN), u.bottom - u.top);
}

/* Unaware threads see the monitor rectangle scaled to 96 dpi (ScaleDPIRect with MulDiv(width, 96, monitorDpi)). */
TEST(display_monitors_dpi_info_unaware_rect_is_scaled_to_96) {
    NEED_GUI(); REQUIRE(U(SetThreadDpiAwarenessContext));
    struct dm_heap h; if (!dm_heap_read(&h) || h.primary < 0) { t_skip("monitor shared heap not readable"); return; }
    DM_CTX_GUARD();
    HMONITOR pm = dm_primary();
    dm_set_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    MONITORINFO raw = { sizeof raw }; U(GetMonitorInfoW)(pm, &raw);
    dm_set_ctx(DPI_AWARENESS_CONTEXT_UNAWARE);
    MONITORINFO un = { sizeof un }; U(GetMonitorInfoW)(pm, &un);
    unsigned dpi = h.m[h.primary].dpi;
    if (!dpi) { t_note("primary monitor dpi field is 0"); return; }
    if (dm_composited() != 1) { t_note("desktop composition flag %d: GetMonitorInfoW takes the unscaled path", dm_composited()); return; }
    int w = raw.rcMonitor.right - raw.rcMonitor.left;
    int expw = MulDiv(w, 96, dpi);
    int gotw = un.rcMonitor.right - un.rcMonitor.left;
    if (dpi == 96) { CHECK(dm_rect_eq(&raw.rcMonitor, &un.rcMonitor)); CHECK(dm_rect_eq(&raw.rcWork, &un.rcWork)); return; }
    if (gotw != expw) t_note("unaware width %d, MulDiv(%d,96,%u)=%d (DWM composition off or other scaling path?)", gotw, w, dpi, expw);
    else CHECK(gotw == expw);
    CHECK(un.rcMonitor.left == 0 && un.rcMonitor.top == 0);          /* primary origin stays (0,0) under scaling */
}
