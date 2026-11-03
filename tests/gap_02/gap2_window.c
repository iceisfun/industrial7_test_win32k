/* gap_02 window group: NtUserIsTopLevelWindow, NtUserIs/EnableChildWindowDpiMessage,
 * NtUserIsWindowBroadcastingDpiToChildren, NtUserIsNonClientDpiScalingEnabled, NtUserGetWindowBand,
 * NtUserTrackPopupMenuEx (flag/handle early exits), NtUserWaitForInputIdle.
 *
 * Analysis facts (win32kfull.sys 19041):
 *  - All HWND queries do handle validation; an invalid handle returns 0 and nothing else is touched.
 *  - IsTopLevelWindow: the parent link must equal the owning desktop's desktop window.
 *  - IsWindowBroadcastingDpiToChildrenX: window dpi awareness (low nibble of the window DPI field) == 2 (per monitor),
 *    window is top level, flag 0x20000000 of the window flag word set.
 *  - the child DPI-message enable path: same awareness + top-level gate, otherwise returns 0 without changing flags;
 *    enabling sets 0x10000000|0x20000000, disabling clears them.
 *  - IsNonClientDpiScalingEnabled: awareness==2, top level, flag 0x10000000.
 *  - IsChildWindowDpiMessageEnabled: awareness==2; a top-level window answers 1, a child answers with the top-level
 *    ancestor's 0x10000000 flag.
 */
#include "gap2.h"

/* ---------- stub identity: every syscall of this area ---------- */
static void stub_check(const char *name, long number) {
    FARPROC f = t_proc_in("win32u.dll", name);
    if (!f) { t_skip("win32u export %s missing", name); return; }
    if (t_is_wine()) { t_note("wine: %s present, number check skipped", name); return; }
    CHECKF(k_stub_valid(f), "%s stub shape", name);
    EXPECT_EQ(k_stub_number(f), number);
}
#define STUBTEST(nm, num) TEST(gap2_stub_##nm) { stub_check(#nm, num); }
STUBTEST(NtGdiUpdateTransform, 4902)
STUBTEST(NtUserIsTopLevelWindow, 5230)
STUBTEST(NtUserIsChildWindowDpiMessageEnabled, 5225)
STUBTEST(NtUserIsWindowBroadcastingDpiToChildren, 5232)
STUBTEST(NtUserTrackPopupMenuEx, 5384)
STUBTEST(NtUserWaitForInputIdle, 5403)
STUBTEST(NtUserGetUniformSpaceMapping, 5185)
STUBTEST(NtUserSetImeInfoEx, 5332)
STUBTEST(NtUserEnableChildWindowDpiMessage, 5089)
STUBTEST(NtUserEvent, 5102)
STUBTEST(NtGdiQueryFonts, 4855)
STUBTEST(NtGdiGetCharWidthW, 4283)
STUBTEST(NtGdiGetCharABCWidthsW, 4781)
STUBTEST(NtGdiStartDoc, 4892)
STUBTEST(NtGdiResetDC, 4858)
STUBTEST(NtGdiDrawStream, 4193)
STUBTEST(NtUserGetOemBitmapSize, 5150)
STUBTEST(NtUserGetWindowBand, 5188)
STUBTEST(NtUserBitBltSysBmp, 4287)
STUBTEST(NtUserIsNonClientDpiScalingEnabled, 5228)
STUBTEST(NtUserModifyUserStartupInfoFlags, 4353)
STUBTEST(NtGdiDdDDIOpenAdapterFromLuid, 4619)
STUBTEST(NtGdiDdDDICacheHybridQueryValue, 4526)
STUBTEST(NtGdiDdDDIGetCachedHybridQueryValue, 4580)
STUBTEST(NtGdiDdDDIGetMultiPlaneOverlayCaps, 4587)

/* ---------- IsTopLevelWindow ---------- */
TEST(gap2_toplevel_null_and_bad_handles) {
    NtUserIsTopLevelWindow_pfn f = GN(NtUserIsTopLevelWindow); REQUIRE(f); REQUIRE(U(CreateWindowExA));
 /* window handle validation fails -> the result stays 0 */
    EXPECT_EQ(f(NULL), 0);
    EXPECT_EQ(f(GAP2_BAD_HWND), 0);
    EXPECT_EQ(f((HWND)(ULONG_PTR)0xffffffff), 0);
    EXPECT_EQ(f(INVALID_HANDLE_VALUE), 0);
    EXPECT_EQ(f((HWND)(ULONG_PTR)1), 0);
}
TEST(gap2_toplevel_popup_window_is_toplevel) {
    NtUserIsTopLevelWindow_pfn f = GN(NtUserIsTopLevelWindow); REQUIRE(f);
    HWND h = gap2_top(); if (!h) { t_skip("window creation failed"); return; }
    EXPECT_EQ(f(h), 1);   /* spwndParent == desktop window */
    gap2_destroy(h);
}
TEST(gap2_toplevel_owned_popup_still_toplevel) {
    NtUserIsTopLevelWindow_pfn f = GN(NtUserIsTopLevelWindow); REQUIRE(f);
    HWND o = gap2_top(); if (!o) { t_skip("window creation failed"); return; }
    HWND p = gap2_win(WS_POPUP, 0, o);   /* owner, not parent */
    CHECK(p != NULL);
    if (p) { EXPECT_EQ(f(p), 1); EXPECT_EQ(f(o), 1); }
    gap2_destroy(p); gap2_destroy(o);
}
TEST(gap2_toplevel_child_windows_are_not) {
    NtUserIsTopLevelWindow_pfn f = GN(NtUserIsTopLevelWindow); REQUIRE(f);
    HWND t = gap2_top(); if (!t) { t_skip("window creation failed"); return; }
    HWND c = gap2_child(t), g = c ? gap2_child(c) : NULL;
    CHECK(c && g);
    if (c) EXPECT_EQ(f(c), 0);   /* parent is t, not the desktop window */
    if (g) EXPECT_EQ(f(g), 0);
    EXPECT_EQ(f(t), 1);
    gap2_destroy(t);
}
TEST(gap2_toplevel_desktop_window_is_not) {
    NtUserIsTopLevelWindow_pfn f = GN(NtUserIsTopLevelWindow); REQUIRE(f); REQUIRE(U(GetDesktopWindow));
    HWND d = U(GetDesktopWindow)();
    /* the desktop window has no parent (the parent link == 0) */
    EXPECT_EQ(f(d), 0);
}
TEST(gap2_toplevel_message_only_window_is_not) {
    NtUserIsTopLevelWindow_pfn f = GN(NtUserIsTopLevelWindow); REQUIRE(f);
    HWND m = gap2_win(WS_POPUP, 0, HWND_MESSAGE);
    if (!m) { t_skip("message-only window creation failed"); return; }
    /* parent is the message window, which is not the desktop window */
    EXPECT_EQ(f(m), 0);
    gap2_destroy(m);
}
TEST(gap2_toplevel_destroyed_window) {
    NtUserIsTopLevelWindow_pfn f = GN(NtUserIsTopLevelWindow); REQUIRE(f);
    HWND h = gap2_top(); if (!h) { t_skip("window creation failed"); return; }
    EXPECT_EQ(f(h), 1);
    gap2_destroy(h);
    EXPECT_EQ(f(h), 0);   /* stale handle fails window handle validation */
}
TEST(gap2_toplevel_reparent_to_desktop_and_back) {
    NtUserIsTopLevelWindow_pfn f = GN(NtUserIsTopLevelWindow); REQUIRE(f); REQUIRE(U(SetParent));
    HWND t = gap2_top(); if (!t) { t_skip("window creation failed"); return; }
    HWND c = gap2_child(t); if (!c) { gap2_destroy(t); t_skip("child failed"); return; }
    EXPECT_EQ(f(c), 0);
    U(SetParent)(c, NULL);   /* NULL parent == desktop */
    /* after SetParent the parent link is the desktop window; the style is still WS_CHILD but the query is purely structural */
    EXPECT_EQ(f(c), 1);
    U(SetParent)(c, t);
    EXPECT_EQ(f(c), 0);
    gap2_destroy(t);
}
TEST(gap2_toplevel_matches_getparent_structure) {
    NtUserIsTopLevelWindow_pfn f = GN(NtUserIsTopLevelWindow); REQUIRE(f); REQUIRE(U(GetParent)); REQUIRE(U(GetAncestor));
    HWND t = gap2_top(); if (!t) { t_skip("window creation failed"); return; }
    HWND c = gap2_child(t);
    /* GA_PARENT of a top-level window is the desktop window, GA_PARENT of a child is its parent */
    HWND ap = U(GetAncestor)(t, GA_PARENT);
    CHECK(ap == U(GetDesktopWindow)());
    CHECK(f(t) == (ap == U(GetDesktopWindow)()));
    if (c) CHECK(f(c) == (U(GetAncestor)(c, GA_PARENT) == U(GetDesktopWindow)()));
    gap2_destroy(t);
}
TEST(gap2_toplevel_user32_export_route) {
    FARPROC e = t_proc_in("user32.dll", "IsTopLevelWindow");
    if (!e) { t_skip("user32!IsTopLevelWindow not exported"); return; }
    BOOL (WINAPI *f)(HWND) = (BOOL (WINAPI *)(HWND))e;
    HWND h = gap2_top(); if (!h) { t_skip("window creation failed"); return; }
    CHECK(f(h) != 0);   /* user32 export is a tail-jump to the syscall stub */
    CHECK(f(NULL) == 0);
    gap2_destroy(h);
}
TEST(gap2_toplevel_foreign_shell_windows) {
    NtUserIsTopLevelWindow_pfn f = GN(NtUserIsTopLevelWindow); REQUIRE(f); REQUIRE(U(FindWindowA));
    /* no thread/process ownership check exists in the body, so windows of other processes answer structurally */
    HWND tray = U(FindWindowA)("Shell_TrayWnd", NULL);
    if (!tray) { t_skip("no taskbar window"); return; }
    EXPECT_EQ(f(tray), 1);
}

/* ---------- unaware process: DPI child-message family is a no-op ---------- */
static int process_is_dpi_unaware(void) {
    HMODULE u = t_module("user32.dll");
    if (!u) return 0;
    typedef DPI_AWARENESS_CONTEXT (WINAPI *gtdac)(void);
    typedef DPI_AWARENESS (WINAPI *gaf)(DPI_AWARENESS_CONTEXT);
    gtdac a = (gtdac)GetProcAddress(u, "GetThreadDpiAwarenessContext");
    gaf b = (gaf)GetProcAddress(u, "GetAwarenessFromDpiAwarenessContext");
    if (!a || !b) return 0;
    return b(a()) == DPI_AWARENESS_UNAWARE;
}
TEST(gap2_dpimsg_invalid_handles_all_zero) {
    NtUserIsChildWindowDpiMessageEnabled_pfn a = GN(NtUserIsChildWindowDpiMessageEnabled);
    NtUserIsWindowBroadcastingDpiToChildren_pfn b = GN(NtUserIsWindowBroadcastingDpiToChildren);
    NtUserEnableChildWindowDpiMessage_pfn c = GN(NtUserEnableChildWindowDpiMessage);
    NtUserIsNonClientDpiScalingEnabled_pfn d = GN(NtUserIsNonClientDpiScalingEnabled);
    REQUIRE(a); REQUIRE(b); REQUIRE(c); REQUIRE(d);
    HWND bad[] = { NULL, GAP2_BAD_HWND, (HWND)(ULONG_PTR)0xffffffff, (HWND)(ULONG_PTR)3 };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        EXPECT_EQ(a(bad[i]), 0);
        EXPECT_EQ(b(bad[i]), 0);
        EXPECT_EQ(c(bad[i], 1), 0);
        EXPECT_EQ(c(bad[i], 0), 0);
        EXPECT_EQ(d(bad[i]), 0);
    }
}
TEST(gap2_dpimsg_destroyed_window_all_zero) {
    NtUserIsChildWindowDpiMessageEnabled_pfn a = GN(NtUserIsChildWindowDpiMessageEnabled);
    NtUserIsWindowBroadcastingDpiToChildren_pfn b = GN(NtUserIsWindowBroadcastingDpiToChildren);
    NtUserEnableChildWindowDpiMessage_pfn c = GN(NtUserEnableChildWindowDpiMessage);
    NtUserIsNonClientDpiScalingEnabled_pfn d = GN(NtUserIsNonClientDpiScalingEnabled);
    REQUIRE(a); REQUIRE(b); REQUIRE(c); REQUIRE(d);
    HWND h = gap2_top(); if (!h) { t_skip("window creation failed"); return; }
    gap2_destroy(h);
    EXPECT_EQ(a(h), 0); EXPECT_EQ(b(h), 0); EXPECT_EQ(c(h, 1), 0); EXPECT_EQ(d(h), 0);
}
TEST(gap2_dpimsg_unaware_process_never_enables) {
    NtUserIsChildWindowDpiMessageEnabled_pfn a = GN(NtUserIsChildWindowDpiMessageEnabled);
    NtUserIsWindowBroadcastingDpiToChildren_pfn b = GN(NtUserIsWindowBroadcastingDpiToChildren);
    NtUserEnableChildWindowDpiMessage_pfn c = GN(NtUserEnableChildWindowDpiMessage);
    NtUserIsNonClientDpiScalingEnabled_pfn d = GN(NtUserIsNonClientDpiScalingEnabled);
    REQUIRE(a); REQUIRE(b); REQUIRE(c); REQUIRE(d);
    HWND h = gap2_top(); if (!h) { t_skip("window creation failed"); return; }
    if (!process_is_dpi_unaware()) { gap2_destroy(h); t_skip("harness process is not DPI unaware"); return; }
    /* awareness nibble is 0 != 2: every function in the family takes the not-per-monitor branch */
    EXPECT_EQ(a(h), 0);
    EXPECT_EQ(b(h), 0);
    EXPECT_EQ(d(h), 0);
    EXPECT_EQ(c(h, 1), 0);   /* the awareness gate in the child DPI-message enable path returns 0 and sets no flag */
    EXPECT_EQ(b(h), 0);
    EXPECT_EQ(d(h), 0);
    EXPECT_EQ(c(h, 0), 0);
    gap2_destroy(h);
}
TEST(gap2_dpimsg_unaware_child_window) {
    NtUserIsChildWindowDpiMessageEnabled_pfn a = GN(NtUserIsChildWindowDpiMessageEnabled);
    NtUserEnableChildWindowDpiMessage_pfn c = GN(NtUserEnableChildWindowDpiMessage);
    REQUIRE(a); REQUIRE(c);
    HWND t = gap2_top(); if (!t) { t_skip("window creation failed"); return; }
    HWND ch = gap2_child(t);
    if (!process_is_dpi_unaware() || !ch) { gap2_destroy(t); t_skip("prerequisite missing"); return; }
    EXPECT_EQ(a(ch), 0);
    EXPECT_EQ(c(ch, 1), 0);
    EXPECT_EQ(a(ch), 0);
    gap2_destroy(t);
}

/* child process: per-monitor (v1) awareness is irreversible, so the positive paths run in a child */
#define CC(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
CHILD(gap2_dpimsg_permonitor) {
    (void)argc; (void)argv;
    int fails = 0;
    NtUserIsChildWindowDpiMessageEnabled_pfn a = GN(NtUserIsChildWindowDpiMessageEnabled);
    NtUserIsWindowBroadcastingDpiToChildren_pfn b = GN(NtUserIsWindowBroadcastingDpiToChildren);
    NtUserEnableChildWindowDpiMessage_pfn c = GN(NtUserEnableChildWindowDpiMessage);
    NtUserIsNonClientDpiScalingEnabled_pfn d = GN(NtUserIsNonClientDpiScalingEnabled);
    BOOL (WINAPI *setctx)(DPI_AWARENESS_CONTEXT) = (BOOL (WINAPI *)(DPI_AWARENESS_CONTEXT))t_proc_in("user32.dll", "SetProcessDpiAwarenessContext");
    if (!a || !b || !c || !d || !setctx) return 77;
    if (!setctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE)) return 77;
    HWND t = gap2_top(); HWND ch = t ? gap2_child(t) : NULL; HWND gch = ch ? gap2_child(ch) : NULL;
    if (!t || !ch || !gch) return 77;
    /* top-level + per-monitor awareness: IsChildWindowDpiMessageEnabled returns 1 for the top-level itself */
    CC(a(t) == 1);
    /* initial broadcast flag clear */
    CC(b(t) == 0);
    INT_PTR en = c(t, 1);
    printf("NOTE EnableChildWindowDpiMessage(top,1) = %lld\n", (long long)en);
    CC(b(t) == 1);      /* 0x20000000 set unconditionally on the enable branch */
    CC(d(t) == 1);      /* shares the 0x10000000 flag */
    CC(a(t) == 1);
    CC(a(ch) == 1);     /* child answers with the ancestor's 0x10000000 flag */
    CC(a(gch) == 1);
    CC(b(ch) == 0);     /* IsWindowBroadcastingDpiToChildrenX requires a top-level window */
    CC(d(ch) == 0);
    /* child windows are rejected by the top-level gate: returns 0 and flags unchanged */
    CC(c(ch, 0) == 0);
    CC(a(ch) == 1);
    CC(b(t) == 1);
    c(t, 0);
    CC(b(t) == 0);
    CC(d(t) == 0);
    CC(a(t) == 1);      /* top-level self answer does not depend on the flag */
    CC(a(ch) == 0);     /* ancestor flag cleared */
    CC(a(gch) == 0);
    /* re-enable then disable is repeatable */
    c(t, 1); CC(b(t) == 1); c(t, 0); CC(b(t) == 0);
    gap2_destroy(t);
    printf("gap2_dpimsg_permonitor fails=%d\n", fails);
    return fails ? 1 : 0;
}
TEST(gap2_dpimsg_permonitor_child_process) {
    REQUIRE(GN(NtUserEnableChildWindowDpiMessage));
    DWORD r = t_run_child("gap2_dpimsg_permonitor", NULL, 20000);
    if (r == 77) { t_skip("child prerequisite missing"); return; }
    EXPECT_EQ(r, 0);
}

/* ---------- GetWindowBand ---------- */
TEST(gap2_band_invalid_window_leaves_output) {
    NtUserGetWindowBand_pfn f = GN(NtUserGetWindowBand); REQUIRE(f);
    HWND bad[] = { NULL, GAP2_BAD_HWND, (HWND)(ULONG_PTR)0xffffffff };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        DWORD band = 0xCCCCCCCC;
        EXPECT_EQ(f(bad[i], &band), 0);
        EXPECT_EQ(band, 0xCCCCCCCC);   /* the store happens only after window handle validation succeeds */
        EXPECT_EQ(f(bad[i], NULL), 0);
    }
}
TEST(gap2_band_null_output_sets_invalid_parameter) {
    NtUserGetWindowBand_pfn f = GN(NtUserGetWindowBand); REQUIRE(f);
    HWND h = gap2_top(); if (!h) { t_skip("window creation failed"); return; }
    SET_ERR(0);
    EXPECT_EQ(f(h, NULL), 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);   /* UserSetLastError(0x57) */
    gap2_destroy(h);
}
TEST(gap2_band_valid_window_writes_band) {
    NtUserGetWindowBand_pfn f = GN(NtUserGetWindowBand); REQUIRE(f);
    HWND h = gap2_top(); if (!h) { t_skip("window creation failed"); return; }
    DWORD band = 0xCCCCCCCC;
    EXPECT_EQ(f(h, &band), 1);
    CHECK(band != 0xCCCCCCCC);
    t_note("ordinary popup window band = %lu", (unsigned long)band);
    DWORD b2 = 0xDDDDDDDD;
    EXPECT_EQ(f(h, &b2), 1);
    EXPECT_EQ(b2, band);   /* pure read */
    gap2_destroy(h);
    DWORD b3 = 0xEEEEEEEE;
    EXPECT_EQ(f(h, &b3), 0);
    EXPECT_EQ(b3, 0xEEEEEEEE);
}
TEST(gap2_band_user32_export_route) {
    FARPROC e = t_proc_in("user32.dll", "GetWindowBand");
    if (!e) { t_skip("user32!GetWindowBand not exported"); return; }
    HWND h = gap2_top(); if (!h) { t_skip("window creation failed"); return; }
    DWORD viaApi = 0x11111111, viaSys = 0x22222222;
    NtUserGetWindowBand_pfn f = GN(NtUserGetWindowBand); REQUIRE(f);
    BOOL (WINAPI *g)(HWND, DWORD *) = (BOOL (WINAPI *)(HWND, DWORD *))e;
    CHECK(g(h, &viaApi));
    CHECK(f(h, &viaSys));
    EXPECT_EQ(viaApi, viaSys);
    gap2_destroy(h);
}
TEST(gap2_band_child_window_has_own_value) {
    NtUserGetWindowBand_pfn f = GN(NtUserGetWindowBand); REQUIRE(f);
    HWND t = gap2_top(); if (!t) { t_skip("window creation failed"); return; }
    HWND c = gap2_child(t);
    DWORD bt = 0xCCCCCCCC, bc = 0xCCCCCCCC;
    CHECK(f(t, &bt) == 1);
    if (c) { CHECK(f(c, &bc) == 1); t_note("band top=%lu child=%lu", (unsigned long)bt, (unsigned long)bc); }
    gap2_destroy(t);
}

/* ---------- TrackPopupMenuEx early exits (no menu is ever shown) ---------- */
TEST(gap2_trackpopup_reserved_flags_invalid_flags) {
    NtUserTrackPopupMenuEx_pfn f = GN(NtUserTrackPopupMenuEx); REQUIRE(f); REQUIRE(U(CreatePopupMenu));
    HMENU m = U(CreatePopupMenu)(); HWND h = gap2_top();
    if (!m || !h) { if (m) U(DestroyMenu)(m); gap2_destroy(h); t_skip("fixture failed"); return; }
    /* (flags & 0xfffe0200) != 0 -> UserSetLastError(0x3ec), return 0 before any validation */
    UINT bits[] = { 0x200, 0x20000, 0x40000, 0x80000, 0x100000, 0x200000, 0x400000, 0x800000, 0x1000000,
                    0x2000000, 0x4000000, 0x8000000, 0x10000000, 0x20000000, 0x40000000, 0x80000000, 0xfffe0200 };
    for (size_t i = 0; i < sizeof bits / sizeof *bits; i++) {
        SET_ERR(0);
        INT_PTR r = f(m, bits[i] | TPM_RETURNCMD, 0, 0, h, NULL);
        CHECKF(r == 0, "flags %08x -> %lld", bits[i], (long long)r);
        EXPECT_ERR(ERROR_INVALID_FLAGS);
    }
    U(DestroyMenu)(m); gap2_destroy(h);
}
TEST(gap2_trackpopup_reserved_flags_checked_before_handles) {
    NtUserTrackPopupMenuEx_pfn f = GN(NtUserTrackPopupMenuEx); REQUIRE(f);
    SET_ERR(0);
    EXPECT_EQ(f(NULL, 0x200, 0, 0, NULL, NULL), 0);
    EXPECT_ERR(ERROR_INVALID_FLAGS);
    SET_ERR(0);
    EXPECT_EQ(f((HMENU)(ULONG_PTR)0x1234, 0x80000000, 5, 5, GAP2_BAD_HWND, NULL), 0);
    EXPECT_ERR(ERROR_INVALID_FLAGS);
}
TEST(gap2_trackpopup_invalid_menu_returns_zero) {
    NtUserTrackPopupMenuEx_pfn f = GN(NtUserTrackPopupMenuEx); REQUIRE(f);
    HWND h = gap2_top(); if (!h) { t_skip("window creation failed"); return; }
    HMENU bad[] = { NULL, (HMENU)(ULONG_PTR)0x7ffe4321, (HMENU)(ULONG_PTR)0xffffffff };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++)
        EXPECT_EQ(f(bad[i], TPM_RETURNCMD, 0, 0, h, NULL), 0);   /* ValidateHmenu fails, no menu loop entered */
    gap2_destroy(h);
}
TEST(gap2_trackpopup_invalid_window_returns_zero) {
    NtUserTrackPopupMenuEx_pfn f = GN(NtUserTrackPopupMenuEx); REQUIRE(f); REQUIRE(U(CreatePopupMenu));
    HMENU m = U(CreatePopupMenu)(); if (!m) { t_skip("menu creation failed"); return; }
    HWND bad[] = { NULL, GAP2_BAD_HWND, (HWND)(ULONG_PTR)0xffffffff };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++)
        EXPECT_EQ(f(m, TPM_RETURNCMD, 0, 0, bad[i], NULL), 0);   /* window handle validation fails before popup tracking */
    HWND h = gap2_top();
    if (h) { gap2_destroy(h); EXPECT_EQ(f(m, TPM_RETURNCMD, 0, 0, h, NULL), 0); }
    U(DestroyMenu)(m);
}
TEST(gap2_trackpopup_menu_survives_failed_call) {
    NtUserTrackPopupMenuEx_pfn f = GN(NtUserTrackPopupMenuEx); REQUIRE(f); REQUIRE(U(CreatePopupMenu));
    REQUIRE(U(AppendMenuA)); REQUIRE(U(GetMenuItemCount)); REQUIRE(U(IsMenu));
    HMENU m = U(CreatePopupMenu)(); if (!m) { t_skip("menu creation failed"); return; }
    U(AppendMenuA)(m, MF_STRING, 1, "one");
    U(AppendMenuA)(m, MF_STRING, 2, "two");
    f(m, 0x200, 0, 0, NULL, NULL);
    f(m, TPM_RETURNCMD, 0, 0, GAP2_BAD_HWND, NULL);
    CHECK(U(IsMenu)(m));
    EXPECT_EQ(U(GetMenuItemCount)(m), 2);
    U(DestroyMenu)(m);
}

/* ---------- WaitForInputIdle ---------- */
TEST(gap2_waitidle_unknown_process_id) {
    NtUserWaitForInputIdle_pfn f = GN(NtUserWaitForInputIdle); REQUIRE(f);
    /* LockProcessByClientId fails for a pid that cannot exist -> falls out to the final 0xffffffff */
    ULONG_PTR pids[] = { 0xfffffff8, 0xfffffff0, 0x7fffffff0ULL };
    for (size_t i = 0; i < sizeof pids / sizeof *pids; i++)
        EXPECT_EQ(f(pids[i], 0, 0), 0xffffffff);
}
TEST(gap2_waitidle_wow_list_lookup_empty) {
    NtUserWaitForInputIdle_pfn f = GN(NtUserWaitForInputIdle); REQUIRE(f);
    /* third argument non-zero walks gpwtiFirst (WOW task list) for (process id, task id); a 64-bit process has no entry */
    EXPECT_EQ(f(0xfffffff8, 0, 1), 0xffffffff);
    EXPECT_EQ(f(GetCurrentProcessId(), 0, 1), 0xffffffff);
}
CHILD(gap2_sleeper) {
    (void)argc; (void)argv;
    Sleep(4000);
    return 0;
}
TEST(gap2_waitidle_non_gui_child_process) {
    REQUIRE(U(WaitForInputIdle));
    if (t_is_wine()) { t_skip("win32k semantics (ERROR_NOT_GUI_PROCESS) are not emulated"); return; }
    PROCESS_INFORMATION pi; memset(&pi, 0, sizeof pi);
    HANDLE hp = t_spawn_child("gap2_sleeper", NULL, 0, NULL, NULL, NULL, FALSE, NULL, &pi);
    if (!hp) { t_skip("spawn failed"); return; }
    SET_ERR(0);
    DWORD r = U(WaitForInputIdle)(hp, 2000);
    DWORD e = GetLastError();
    t_note("WaitForInputIdle(console child) = 0x%lx, last error %lu", (unsigned long)r, (unsigned long)e);
    /* the call must return one of its three documented results; a non-GUI process never hangs it for the whole timeout */
    CHECK(r == 0 || r == WAIT_TIMEOUT || r == WAIT_FAILED);
    if (r == WAIT_FAILED) CHECKF(e == 1471 /* ERROR_NOT_GUI_PROCESS */ || e == ERROR_INVALID_PARAMETER || e == ERROR_INVALID_HANDLE, "err %lu", (unsigned long)e);
    TerminateProcess(hp, 0);
    WaitForSingleObject(hp, 5000);
    CloseHandle(pi.hThread); CloseHandle(hp);
}
TEST(gap2_waitidle_bad_process_handle_user32) {
    REQUIRE(U(WaitForInputIdle));
    /* NtQueryInformationProcess fails -> user32 sets ERROR_INVALID_HANDLE (6) and returns -1 without a syscall */
    SET_ERR(0);
    EXPECT_EQ(U(WaitForInputIdle)((HANDLE)(ULONG_PTR)0x7ffe0004, 0), 0xffffffff);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    SET_ERR(0);
    EXPECT_EQ(U(WaitForInputIdle)(NULL, 0), 0xffffffff);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
}
