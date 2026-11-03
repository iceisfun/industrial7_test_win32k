/* Window-manager / input syscalls whose failure paths (and read-only success paths) leave no state behind:
 * RegisterHotKey, SetWindowDisplayAffinity, EnumDisplayMonitors, GetDisplayConfigBufferSizes, QueryInputContext,
 * GetClipboardAccessToken, ImpersonateDdeClientWindow, SetWindowsHookEx, TranslateMessage. */
#include "g10.h"

/* ---- RegisterHotKey: fsModifiers & 0xffff95f0 -> ERROR_INVALID_FLAGS before the hwnd is looked at (win32kfull NtUserRegisterHotKey) ---- */
TEST(gap10_hotkey_invalid_modifier_bits_one_at_a_time) {
    for (int bit = 4; bit < 32; bit++) {
        UINT m = 1u << bit;
        if (m & 0x6a0f) continue;               /* MOD_* (1,2,4,8), 0x200, 0x800, 0x2000, MOD_NOREPEAT 0x4000 pass the mask */
        SET_ERR(0);
        BOOL r = U(RegisterHotKey)(NULL, 0x7e57, m, VK_F24);
        if (r) { CHECKF(0, "modifier bit 0x%x accepted", m); U(UnregisterHotKey)(NULL, 0x7e57); continue; }
        DWORD e = GetLastError();
        if (e == ERROR_ACCESS_DENIED) { t_note("restricted process: error 5 for 0x%x", m); continue; }   /* IsImmersiveAppRestricted */
        CHECKF(e == ERROR_INVALID_FLAGS, "modifier 0x%x: error %lu", m, e);
    }
}
TEST(gap10_hotkey_invalid_flags_win_over_invalid_hwnd) {
    SET_ERR(0);
    CHECK(!U(RegisterHotKey)((HWND)(ULONG_PTR)0x1234, 1, 0x10, VK_F24));
    DWORD e = GetLastError();
    CHECKF(e == ERROR_INVALID_FLAGS || e == ERROR_ACCESS_DENIED, "error %lu", e);   /* flags are checked first */
}
TEST(gap10_hotkey_valid_flags_invalid_hwnd) {
    const ULONG_PTR h[] = { 1, 0x1234, 0xFFFFFFFFull };
    for (unsigned i = 0; i < 3; i++) {
        SET_ERR(0);
        BOOL r = U(RegisterHotKey)((HWND)h[i], 1, MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN, VK_F24);
        if (r) { CHECKF(0, "hwnd %p accepted", (void *)h[i]); U(UnregisterHotKey)((HWND)h[i], 1); continue; }
        DWORD e = GetLastError();
        if (e == ERROR_ACCESS_DENIED) { t_skip("restricted process"); return; }
        CHECKF(e == ERROR_INVALID_WINDOW_HANDLE, "hwnd %p: error %lu", (void *)h[i], e);
    }
}
TEST(gap10_hotkey_destroyed_hwnd) {
    HWND w = k_make_window("gap10-hk"); REQUIRE(w);
    U(DestroyWindow)(w);
    SET_ERR(0);
    BOOL r = U(RegisterHotKey)(w, 1, MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN, VK_F24);
    if (r) { CHECK(0); U(UnregisterHotKey)(w, 1); return; }
    if (GetLastError() == ERROR_ACCESS_DENIED) { t_skip("restricted process"); return; }
    EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
}
TEST(gap10_hotkey_unregister_of_nothing) {
    SET_ERR(0);
    CHECK(!U(UnregisterHotKey)(NULL, 0x7e58));
    EXPECT_ERR(ERROR_HOTKEY_NOT_REGISTERED);
}

/* ---- SetWindowDisplayAffinity validation (win32kfull NtUserSetWindowDisplayAffinity) ---- */
TEST(gap10_affinity_invalid_window_handles) {
    REQUIRE(UN(NtUserSetWindowDisplayAffinity)); if (t_is_wine()) { t_skip("Wine aborts in NtUserSetWindowDisplayAffinity"); return; }
    const ULONG_PTR h[] = { 0, 1, 0x1234, 0xFFFFFFFFull };
    for (unsigned i = 0; i < 4; i++) {
        SET_ERR(0);
        CHECK(!UN(NtUserSetWindowDisplayAffinity)((HWND)h[i], 0));
        CHECKF(GetLastError() == ERROR_INVALID_WINDOW_HANDLE, "hwnd %p: error %lu", (void *)h[i], GetLastError());
    }
}
TEST(gap10_affinity_flags_without_monitor_or_exclude_bit_rejected) {
    REQUIRE(UN(NtUserSetWindowDisplayAffinity)); if (t_is_wine()) { t_skip("Wine aborts in NtUserSetWindowDisplayAffinity"); return; }
    HWND w = k_make_window("gap10-wda"); REQUIRE(w);
    /* non-zero flags must carry bit 0 (WDA_MONITOR) or bit 4 (WDA_EXCLUDEFROMCAPTURE=0x11 uses both): (flags & 0x11) == 0 -> 0x57 */
    const UINT bad[] = { 2, 4, 8, 0xE, 0x20, 0x100, 0x8000, 0x80000000u, 0xFFFFFFEEu };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SET_ERR(0);
        CHECKF(!UN(NtUserSetWindowDisplayAffinity)(w, bad[i]), "flags 0x%x accepted", bad[i]);
        CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "flags 0x%x: error %lu", bad[i], GetLastError());
    }
    DWORD a = 0xCD;
    if (U(GetWindowDisplayAffinity)(w, &a)) G10_EQ(a, 0);     /* nothing was applied */
    U(DestroyWindow)(w);
}
TEST(gap10_affinity_child_window_is_not_top_level) {
    REQUIRE(UN(NtUserSetWindowDisplayAffinity)); if (t_is_wine()) { t_skip("Wine aborts in NtUserSetWindowDisplayAffinity"); return; }
    HWND p = k_make_window("gap10-wda-parent"); REQUIRE(p);
    HWND c = U(CreateWindowExW)(WS_EX_NOACTIVATE, L"STATIC", L"c", WS_CHILD, 0, 0, 1, 1, p, NULL, GetModuleHandleW(NULL), NULL);
    if (!c) { U(DestroyWindow)(p); t_skip("no child"); return; }
    SET_ERR(0);
    CHECK(!UN(NtUserSetWindowDisplayAffinity)(c, 0));       /* _IsTopLevelWindow fails -> ERROR_INVALID_PARAMETER even for WDA_NONE */
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SET_ERR(0);
    CHECK(!UN(NtUserSetWindowDisplayAffinity)(c, 1));
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    U(DestroyWindow)(c); U(DestroyWindow)(p);
}
TEST(gap10_affinity_foreign_process_window_denied) {
    REQUIRE(UN(NtUserSetWindowDisplayAffinity)); if (t_is_wine()) { t_skip("Wine aborts in NtUserSetWindowDisplayAffinity"); return; }
    HWND d = U(GetDesktopWindow)(); REQUIRE(d);
    SET_ERR(0);
    CHECK(!UN(NtUserSetWindowDisplayAffinity)(d, 0));         /* owner process != caller -> ERROR_ACCESS_DENIED */
    EXPECT_ERR(ERROR_ACCESS_DENIED);
    SET_ERR(0);
    CHECK(!U(SetWindowDisplayAffinity)(d, WDA_MONITOR));
    EXPECT_ERR(ERROR_ACCESS_DENIED);
}

/* ---- EnumDisplayMonitors (the kernel calls back into user mode) ---- */
typedef struct { int n; int stop_after; HMONITOR mon[16]; RECT rc[16]; HDC dc[16]; ULONG_PTR data[16]; DWORD tid[16]; } ENUMLOG;
static BOOL CALLBACK enum_cb(HMONITOR m, HDC dc, LPRECT rc, LPARAM lp) {
    ENUMLOG *l = (ENUMLOG *)lp;
    if (l->n < 16) { l->mon[l->n] = m; l->dc[l->n] = dc; l->data[l->n] = (ULONG_PTR)lp; l->tid[l->n] = GetCurrentThreadId(); if (rc) l->rc[l->n] = *rc; }
    l->n++;
    return !(l->stop_after && l->n >= l->stop_after);
}
TEST(gap10_enummon_all_monitors_count) {
    ENUMLOG l; memset(&l, 0, sizeof l);
    SET_ERR(0);
    BOOL r = U(EnumDisplayMonitors)(NULL, NULL, enum_cb, (LPARAM)&l);
    CHECK(r);
    CHECKF(l.n >= 1, "n=%d", l.n);
    G10_EQ(l.n, U(GetSystemMetrics)(SM_CMONITORS));
}
TEST(gap10_enummon_callback_arguments) {
    ENUMLOG l; memset(&l, 0, sizeof l);
    U(EnumDisplayMonitors)(NULL, NULL, enum_cb, (LPARAM)&l);
    for (int i = 0; i < l.n && i < 16; i++) {
        CHECK(l.dc[i] == NULL);                          /* hdcMonitor is NULL when hdc was NULL */
        CHECK(l.tid[i] == GetCurrentThreadId());         /* the callback runs on the calling thread */
        MONITORINFO mi; memset(&mi, 0, sizeof mi); mi.cbSize = sizeof mi;
        CHECK(U(GetMonitorInfoW)(l.mon[i], &mi));
        CHECKF(EqualRect(&mi.rcMonitor, &l.rc[i]), "monitor %d rect differs from GetMonitorInfo", i);
        CHECK(l.mon[i] == U(MonitorFromRect)(&mi.rcMonitor, MONITOR_DEFAULTTONULL));
    }
}
TEST(gap10_enummon_distinct_monitor_handles) {
    ENUMLOG l; memset(&l, 0, sizeof l);
    U(EnumDisplayMonitors)(NULL, NULL, enum_cb, (LPARAM)&l);
    for (int i = 0; i < l.n && i < 16; i++) for (int j = i + 1; j < l.n && j < 16; j++) CHECK(l.mon[i] != l.mon[j]);
}
TEST(gap10_enummon_callback_false_stops_enumeration) {
    ENUMLOG l; memset(&l, 0, sizeof l); l.stop_after = 1;
    BOOL r = U(EnumDisplayMonitors)(NULL, NULL, enum_cb, (LPARAM)&l);
    G10_EQ(l.n, 1);
    t_note("EnumDisplayMonitors after an aborting callback returned %d", r);
}
TEST(gap10_enummon_clip_rect_selects_monitors) {
    MONITORINFO mi; memset(&mi, 0, sizeof mi); mi.cbSize = sizeof mi;
    HMONITOR pm = U(MonitorFromPoint)((POINT){0, 0}, MONITOR_DEFAULTTOPRIMARY);
    REQUIRE(pm);
    CHECK(U(GetMonitorInfoW)(pm, &mi));
    RECT clip = { mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.left + 1, mi.rcMonitor.top + 1 };
    ENUMLOG l; memset(&l, 0, sizeof l);
    CHECK(U(EnumDisplayMonitors)(NULL, &clip, enum_cb, (LPARAM)&l));
    G10_EQ(l.n, 1);
    if (l.n == 1) {
        CHECK(l.mon[0] == pm);
        CHECKF(EqualRect(&l.rc[0], &clip), "lprcMonitor %ld,%ld,%ld,%ld is not the clip∩monitor", l.rc[0].left, l.rc[0].top, l.rc[0].right, l.rc[0].bottom);
    }
}
TEST(gap10_enummon_empty_clip_rect_no_callbacks) {
    RECT clip = { 0, 0, 0, 0 };
    ENUMLOG l; memset(&l, 0, sizeof l);
    BOOL r = U(EnumDisplayMonitors)(NULL, &clip, enum_cb, (LPARAM)&l);
    G10_EQ(l.n, 0);
    t_note("empty clip rect -> returned %d", r);
}
TEST(gap10_enummon_clip_rect_far_outside) {
    RECT clip = { 1000000, 1000000, 1000010, 1000010 };
    ENUMLOG l; memset(&l, 0, sizeof l);
    U(EnumDisplayMonitors)(NULL, &clip, enum_cb, (LPARAM)&l);
    G10_EQ(l.n, 0);
}
TEST(gap10_enummon_screen_dc_gets_monitor_dcs) {
    HDC scr = U(GetDC)(NULL); REQUIRE(scr);
    ENUMLOG l; memset(&l, 0, sizeof l);
    BOOL r = U(EnumDisplayMonitors)(scr, NULL, enum_cb, (LPARAM)&l);
    CHECK(r);
    CHECK(l.n >= 1);
    for (int i = 0; i < l.n && i < 16; i++) CHECK(l.dc[i] != NULL);
    U(ReleaseDC)(NULL, scr);
}
TEST(gap10_enummon_lparam_roundtrip_64bit) {
    ENUMLOG l; memset(&l, 0, sizeof l);
    U(EnumDisplayMonitors)(NULL, NULL, enum_cb, (LPARAM)&l);
    for (int i = 0; i < l.n && i < 16; i++) CHECK(l.data[i] == (ULONG_PTR)&l);
}
TEST(gap10_enummon_invalid_hdc_enumerates_nothing) {
    ENUMLOG l; memset(&l, 0, sizeof l);
    BOOL r = U(EnumDisplayMonitors)((HDC)(ULONG_PTR)0x1234, NULL, enum_cb, (LPARAM)&l);
    t_note("invalid hdc -> %d, %d callbacks, error %lu", r, l.n, GetLastError());
    G10_EQ(l.n, 0);
}

/* ---- GetDisplayConfigBufferSizes: user32 maps STATUS_INVALID_PARAMETER and derives numModes from numPaths ---- */
TEST(gap10_dispcfg_null_outputs_rejected_by_user32) {
    UINT32 p = 0x55, m = 0x55;
    G10_EQ(U(GetDisplayConfigBufferSizes)(QDC_ONLY_ACTIVE_PATHS, NULL, &m), ERROR_INVALID_PARAMETER);
    G10_EQ(U(GetDisplayConfigBufferSizes)(QDC_ONLY_ACTIVE_PATHS, &p, NULL), ERROR_INVALID_PARAMETER);
    G10_EQ(U(GetDisplayConfigBufferSizes)(QDC_ONLY_ACTIVE_PATHS, NULL, NULL), ERROR_INVALID_PARAMETER);
    CHECK(p == 0x55 && m == 0x55);
}
TEST(gap10_dispcfg_invalid_flags) {
    /* 0xFFFFFFFF is not in this list: Windows 10 accepts it (any value carrying a valid path-selection bit passes) */
    const UINT32 bad[] = { 0, 8, 0x10, 0x20, 0x80000000u };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        UINT32 p = 0x55555555, m = 0x55555555;
        LONG r = U(GetDisplayConfigBufferSizes)(bad[i], &p, &m);
        CHECKF(r == ERROR_INVALID_PARAMETER, "flags 0x%x -> %ld", bad[i], r);
        CHECKF(m == 0x55555555, "flags 0x%x: numModes written on failure", bad[i]);
        t_note("flags 0x%x: numPaths now 0x%x", bad[i], p);
    }
}
TEST(gap10_dispcfg_all_flag_bits_accepted) {
    UINT32 p = 0x55555555, m = 0x55555555;
    LONG r = U(GetDisplayConfigBufferSizes)(0xFFFFFFFFu, &p, &m);
    G10_EQ(r, ERROR_SUCCESS);
    CHECK(m >= p);                  /* modes are derived from the path count, never fewer */
    CHECK(p != 0x55555555 && m != 0x55555555);
}
TEST(gap10_dispcfg_mode_count_is_derived_from_path_count) {
    UINT32 p = 0, m = 0;
    LONG r = U(GetDisplayConfigBufferSizes)(QDC_ONLY_ACTIVE_PATHS, &p, &m);
    if (r != ERROR_SUCCESS) { t_skip("display config unavailable: %ld", r); return; }
    CHECK(p >= 1);
    G10_EQ(m, p * 2);                      /* user32: numModes = 2 * numPaths without QDC_VIRTUAL_MODE_AWARE */
    UINT32 p2 = 0, m2 = 0;
    r = U(GetDisplayConfigBufferSizes)(QDC_ONLY_ACTIVE_PATHS | 0x10, &p2, &m2);
    if (r == ERROR_SUCCESS) { G10_EQ(p2, p); G10_EQ(m2, p2 * 3); }   /* QDC_VIRTUAL_MODE_AWARE: numModes = 3 * numPaths */
    else t_note("virtual-mode-aware query -> %ld", r);
}
TEST(gap10_dispcfg_all_paths_cover_active_paths) {
    UINT32 pa = 0, ma = 0, pall = 0, mall = 0;
    if (U(GetDisplayConfigBufferSizes)(QDC_ONLY_ACTIVE_PATHS, &pa, &ma) != ERROR_SUCCESS) { t_skip("no display config"); return; }
    if (U(GetDisplayConfigBufferSizes)(QDC_ALL_PATHS, &pall, &mall) != ERROR_SUCCESS) { t_skip("QDC_ALL_PATHS unavailable"); return; }
    CHECKF(pall >= pa, "all %u < active %u", pall, pa);
    G10_EQ(mall, pall * 2);
}
TEST(gap10_dispcfg_active_paths_cover_monitors) {
    UINT32 p = 0, m = 0;
    if (U(GetDisplayConfigBufferSizes)(QDC_ONLY_ACTIVE_PATHS, &p, &m) != ERROR_SUCCESS) { t_skip("no display config"); return; }
    int mons = U(GetSystemMetrics)(SM_CMONITORS);
    t_note("active paths %u, SM_CMONITORS %d", p, mons);
    CHECK(p >= 1);
}
TEST(gap10_dispcfg_stable_between_calls) {
    UINT32 p1 = 0, m1 = 0, p2 = 0, m2 = 0;
    if (U(GetDisplayConfigBufferSizes)(QDC_ONLY_ACTIVE_PATHS, &p1, &m1) != ERROR_SUCCESS) { t_skip("no display config"); return; }
    CHECK(U(GetDisplayConfigBufferSizes)(QDC_ONLY_ACTIVE_PATHS, &p2, &m2) == ERROR_SUCCESS);
    G10_EQ(p1, p2); G10_EQ(m1, m2);
}

/* ---- NtUserQueryInputContext(hIMC, type): 0 = owner pid, 1 = owner tid, 2/3 = pointers, else 0x57 ---- */
static HANDLE new_imc(void) {
    HANDLE (WINAPI *f)(void) = (HANDLE (WINAPI *)(void))t_proc_in("imm32.dll", "ImmCreateContext");
    return f ? f() : NULL;
}
static void free_imc(HANDLE h) {
    BOOL (WINAPI *f)(HANDLE) = (BOOL (WINAPI *)(HANDLE))t_proc_in("imm32.dll", "ImmDestroyContext");
    if (f && h) f(h);
}
static int imc_ok(HANDLE h) {      /* returns 0 and skips when the session has no IME support (error 0x78) */
    SET_ERR(0);
    ULONG_PTR r = UN(NtUserQueryInputContext)(h, 0);
    if (r == 0 && GetLastError() == ERROR_CALL_NOT_IMPLEMENTED) { t_skip("IME disabled in this session (IME disabled by a kernel flag)"); return 0; }
    return 1;
}
TEST(gap10_imc_query_owner_process_and_thread) {
    REQUIRE(UN(NtUserQueryInputContext));
    HANDLE h = new_imc(); if (!h) { t_skip("no IMC"); return; }
    if (!imc_ok(h)) { free_imc(h); return; }
    G10_EQ(UN(NtUserQueryInputContext)(h, 0), GetCurrentProcessId());
    G10_EQ(UN(NtUserQueryInputContext)(h, 1), GetCurrentThreadId());
    free_imc(h);
}
TEST(gap10_imc_query_invalid_type_values) {
    REQUIRE(UN(NtUserQueryInputContext));
    HANDLE h = new_imc(); if (!h) { t_skip("no IMC"); return; }
    if (!imc_ok(h)) { free_imc(h); return; }
    const int bad[] = { 4, 5, 100, -1, (int)0x80000000 };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SET_ERR(0);
        G10_EQ(UN(NtUserQueryInputContext)(h, bad[i]), 0);
        CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "type %d: error %lu", bad[i], GetLastError());
    }
    free_imc(h);
}
TEST(gap10_imc_query_pointer_types_are_stable) {
    REQUIRE(UN(NtUserQueryInputContext));
    HANDLE h = new_imc(); if (!h) { t_skip("no IMC"); return; }
    if (!imc_ok(h)) { free_imc(h); return; }
    for (int t = 2; t <= 3; t++) {
        ULONG_PTR a = UN(NtUserQueryInputContext)(h, t), b = UN(NtUserQueryInputContext)(h, t);
        CHECK(a == b);
        t_note("type %d -> %p", t, (void *)a);
    }
    free_imc(h);
}
TEST(gap10_imc_query_invalid_handles) {
    REQUIRE(UN(NtUserQueryInputContext));
    HANDLE h = new_imc(); if (!h) { t_skip("no IMC"); return; }
    if (!imc_ok(h)) { free_imc(h); return; }
    const ULONG_PTR bad[] = { 0, 1, 0x1234, 0xFFFFFFFFull };
    for (unsigned i = 0; i < 4; i++) for (int t = 0; t < 4; t++) {
        SET_ERR(0);
        CHECKF(UN(NtUserQueryInputContext)((HANDLE)bad[i], t) == 0, "handle %p type %d", (void *)bad[i], t);
    }
    free_imc(h);
}
TEST(gap10_imc_query_destroyed_and_wrong_type_handles) {
    REQUIRE(UN(NtUserQueryInputContext));
    HANDLE h = new_imc(); if (!h) { t_skip("no IMC"); return; }
    if (!imc_ok(h)) { free_imc(h); return; }
    struct k_user_entry e;
    if (k_user_entry(h, &e)) G10_EQ(e.type, G10_TYPE_IMC);
    HWND w = k_make_window("gap10-imc");
    if (w) { G10_EQ(UN(NtUserQueryInputContext)((HANDLE)w, 0), 0); U(DestroyWindow)(w); }
    free_imc(h);
    G10_EQ(UN(NtUserQueryInputContext)(h, 0), 0);               /* stale handle after ImmDestroyContext */
}
TEST(gap10_imc_two_contexts_distinct_handles_same_owner) {
    REQUIRE(UN(NtUserQueryInputContext));
    HANDLE a = new_imc(), b = new_imc();
    if (!a || !b) { free_imc(a); free_imc(b); t_skip("no IMC"); return; }
    if (imc_ok(a)) {
        CHECK(a != b);
        G10_EQ(UN(NtUserQueryInputContext)(a, 1), UN(NtUserQueryInputContext)(b, 1));
    }
    free_imc(a); free_imc(b);
}

/* ---- NtUserGetClipboardAccessToken(&hToken, access): FALSE+5 leaves the output alone, TRUE yields a usable token ---- */
TEST(gap10_cbtoken_result_is_self_consistent) {
    REQUIRE(UN(NtUserGetClipboardAccessToken)); if (t_is_wine()) { t_skip("Wine aborts in NtUserGetClipboardAccessToken"); return; }
    HANDLE t = (HANDLE)(ULONG_PTR)0xCDCDCDCDCDCDCDCDull;
    SET_ERR(0);
    BOOL r = UN(NtUserGetClipboardAccessToken)(&t, TOKEN_QUERY);
    if (r) {
        TOKEN_STATISTICS ts; DWORD n = 0;
        CHECK(GetTokenInformation(t, TokenStatistics, &ts, sizeof ts, &n));
        CloseHandle(t);
    } else {
        DWORD e = GetLastError();
        t_note("no clipboard owner token: error %lu", e);
        if (e == ERROR_ACCESS_DENIED) CHECK(t == (HANDLE)(ULONG_PTR)0xCDCDCDCDCDCDCDCDull);   /* output only written once an owner token was found */
    }
}
TEST(gap10_cbtoken_zero_access_mask) {
    REQUIRE(UN(NtUserGetClipboardAccessToken)); if (t_is_wine()) { t_skip("Wine aborts in NtUserGetClipboardAccessToken"); return; }
    HANDLE t = (HANDLE)(ULONG_PTR)0xCDCDCDCDCDCDCDCDull;
    BOOL r = UN(NtUserGetClipboardAccessToken)(&t, 0);
    t_note("access 0 -> %d token %p", r, t);
    if (r && t && t != (HANDLE)(ULONG_PTR)0xCDCDCDCDCDCDCDCDull) CloseHandle(t);
}
TEST(gap10_cbtoken_repeated_calls_give_distinct_handles) {
    REQUIRE(UN(NtUserGetClipboardAccessToken)); if (t_is_wine()) { t_skip("Wine aborts in NtUserGetClipboardAccessToken"); return; }
    /* a token only exists while the clipboard has an owner: give it one (an own text item), and empty it again at the end */
    HWND ow = k_make_window("gap10-cb"); REQUIRE(ow);
    int filled = 0;
    if (U(OpenClipboard)(ow)) {
        if (U(EmptyClipboard)()) {
            HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, 8 * sizeof(WCHAR));
            WCHAR *p = g ? (WCHAR *)GlobalLock(g) : NULL;
            if (p) { lstrcpyW(p, L"win32kt"); GlobalUnlock(g); filled = U(SetClipboardData)(CF_UNICODETEXT, g) != NULL; }
            if (!filled && g) GlobalFree(g);
        }
        U(CloseClipboard)();
    }
    HANDLE a = NULL, b = NULL;
    if (!filled || !UN(NtUserGetClipboardAccessToken)(&a, TOKEN_QUERY)) {
        t_skip("no clipboard token available (clipboard filled: %d)", filled);
    } else {
        BOOL r2 = UN(NtUserGetClipboardAccessToken)(&b, TOKEN_QUERY);
        CHECK(r2);
        CHECK(a != b || b == NULL);   /* a second call while the first handle is open yields a second handle */
        if (r2 && b) CloseHandle(b);
        CloseHandle(a);
    }
    if (U(OpenClipboard)(ow)) { U(EmptyClipboard)(); U(CloseClipboard)(); }
    U(DestroyWindow)(ow);
}

/* ---- NtUserImpersonateDdeClientWindow(hwndClient, hwndServer) ---- */
TEST(gap10_ddeimp_invalid_client_or_server) {
    REQUIRE(UN(NtUserImpersonateDdeClientWindow)); if (t_is_wine()) { t_skip("Wine aborts in NtUserImpersonateDdeClientWindow"); return; }
    HWND w = k_make_window("gap10-dde"); REQUIRE(w);
    HWND bad = (HWND)(ULONG_PTR)0x1234;
    SET_ERR(0);
    CHECK(!UN(NtUserImpersonateDdeClientWindow)(bad, w));
    EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
    SET_ERR(0);
    CHECK(!UN(NtUserImpersonateDdeClientWindow)(w, bad));
    EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
    SET_ERR(0);
    CHECK(!UN(NtUserImpersonateDdeClientWindow)(NULL, NULL));
    EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
    U(DestroyWindow)(w);
}
TEST(gap10_ddeimp_same_process_is_a_no_op_success) {
    REQUIRE(UN(NtUserImpersonateDdeClientWindow)); if (t_is_wine()) { t_skip("Wine aborts in NtUserImpersonateDdeClientWindow"); return; }
    HWND a = k_make_window("gap10-dde-a"), b = k_make_window("gap10-dde-b"); REQUIRE(a && b);
    /* server window on the calling thread and both windows in one process: returns TRUE without impersonating */
    CHECK(UN(NtUserImpersonateDdeClientWindow)(a, b));
    CHECK(UN(NtUserImpersonateDdeClientWindow)(a, a));
    CHECK(UN(NtUserImpersonateDdeClientWindow)(b, a));
    U(DestroyWindow)(a); U(DestroyWindow)(b);
}
TEST(gap10_ddeimp_server_must_belong_to_calling_thread) {
    REQUIRE(UN(NtUserImpersonateDdeClientWindow)); if (t_is_wine()) { t_skip("Wine aborts in NtUserImpersonateDdeClientWindow"); return; }
    G10W peer; if (!g10_wthread_start(&peer)) { t_skip("peer thread failed"); return; }
    HWND mine = k_make_window("gap10-dde-mine"); REQUIRE(mine);
    SET_ERR(0);
    CHECK(!UN(NtUserImpersonateDdeClientWindow)(mine, peer.hwnd));   /* server window owned by another thread -> 0x57 */
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SET_ERR(0);
    CHECK(UN(NtUserImpersonateDdeClientWindow)(peer.hwnd, mine));    /* client may live on another thread of the same process */
    U(DestroyWindow)(mine);
    g10_wthread_stop(&peer);
}

/* ---- SetWindowsHookEx failure paths that install nothing ---- */
static LRESULT CALLBACK nop_hook(int c, WPARAM w, LPARAM l) { return CallNextHookEx(NULL, c, w, l); }
TEST(gap10_hook_nonexistent_thread_id) {
    const DWORD tids[] = { 0x7FFFFFF0, 0xFFFFFFF0u, 0x3FFFFC };
    for (unsigned i = 0; i < 3; i++) {
        SET_ERR(0);
        HHOOK h = U(SetWindowsHookExW)(WH_GETMESSAGE, nop_hook, NULL, tids[i]);
        if (h) { CHECKF(0, "tid 0x%lx accepted", tids[i]); U(UnhookWindowsHookEx)(h); continue; }
        CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "tid 0x%lx: error %lu", tids[i], GetLastError());
    }
}
TEST(gap10_hook_nonexistent_thread_id_direct_syscall) {
    REQUIRE(UN(NtUserSetWindowsHookEx));
    /* NtUserSetWindowsHookEx(hmod, pstrLib, tid, idHook, pfn, flags): PtiFromThreadId(tid) == NULL -> 0x57 before any other argument is used */
    SET_ERR(0);
    HHOOK h = UN(NtUserSetWindowsHookEx)(NULL, NULL, 0x7FFFFFF0, WH_GETMESSAGE, nop_hook, 0);
    if (h) { CHECK(0); U(UnhookWindowsHookEx)(h); return; }
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}
TEST(gap10_hook_global_hook_needs_module) {
    SET_ERR(0);
    HHOOK h = U(SetWindowsHookExW)(WH_GETMESSAGE, nop_hook, NULL, 0);
    if (h) { CHECK(0); U(UnhookWindowsHookEx)(h); return; }
    EXPECT_ERR(ERROR_HOOK_NEEDS_HMOD);
}
TEST(gap10_hook_null_procedure) {
    SET_ERR(0);
    HHOOK h = U(SetWindowsHookExW)(WH_GETMESSAGE, NULL, NULL, GetCurrentThreadId());
    if (h) { CHECK(0); U(UnhookWindowsHookEx)(h); return; }
    EXPECT_ERR(ERROR_INVALID_FILTER_PROC);      /* zzzSetWindowsHookEx: pfn == NULL -> 0x593 */
}
TEST(gap10_hook_invalid_hook_ids) {
    const int ids[] = { -2, -100, 15, 16, 99, 0x7FFFFFFF, (int)0x80000000 };
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        SET_ERR(0);
        HHOOK h = U(SetWindowsHookExW)(ids[i], nop_hook, NULL, GetCurrentThreadId());
        if (h) { CHECKF(0, "hook id %d accepted", ids[i]); U(UnhookWindowsHookEx)(h); continue; }
        /* zzzSetWindowsHookEx: (idHook + 1) > 0xf -> 0x592 (ERROR_INVALID_HOOK_FILTER); valid ids are -1..14 */
        CHECKF(GetLastError() == ERROR_INVALID_HOOK_FILTER, "hook id %d: error %lu", ids[i], GetLastError());
    }
}
TEST(gap10_hook_unhook_invalid_handle) {
    const ULONG_PTR h[] = { 0, 1, 0x1234, 0xFFFFFFFFull };
    for (unsigned i = 0; i < 4; i++) {
        SET_ERR(0);
        CHECK(!U(UnhookWindowsHookEx)((HHOOK)h[i]));
        CHECKF(GetLastError() == ERROR_INVALID_HOOK_HANDLE, "handle %p: error %lu", (void *)h[i], GetLastError());
    }
}

/* ---- TranslateMessage (user32 filters 0x100..0x105 minus WM_CHAR/WM_DEADCHAR; the rest reaches NtUserTranslateMessage) ---- */
static BOOL take_msg(HWND w, UINT lo, UINT hi, MSG *out) { return U(PeekMessageW)(out, w, lo, hi, PM_REMOVE); }
static void drain(HWND w) { MSG m; while (U(PeekMessageW)(&m, w, 0, 0, PM_REMOVE)) ; }
TEST(gap10_translate_keydown_letter_posts_wm_char) {
    HWND w = k_make_window("gap10-tm"); REQUIRE(w);
    drain(w);
    MSG m = { w, WM_KEYDOWN, 'A', (LPARAM)(1 | (0x1e << 16)), 0, {0, 0} };
    BOOL r = U(TranslateMessage)(&m);
    MSG c; BOOL got = take_msg(w, WM_CHAR, WM_CHAR, &c);
    CHECK(r);
    CHECK(got);
    if (got) {
        CHECK(c.hwnd == w);
        G10_EQ(c.lParam & 0xFFFFFF, m.lParam & 0xFFFFFF);      /* repeat count, scan code and extended bit are copied */
        t_note("VK_A produced WM_CHAR 0x%x", (unsigned)c.wParam);
        CHECK(c.wParam != 0);
    }
    U(DestroyWindow)(w);
}
TEST(gap10_translate_modifier_keys_return_true_but_post_nothing) {
    HWND w = k_make_window("gap10-tm"); REQUIRE(w);
    const WPARAM vk[] = { VK_SHIFT, VK_CONTROL, VK_MENU, VK_LSHIFT, VK_CAPITAL, VK_F5, VK_LEFT };
    for (unsigned i = 0; i < sizeof vk / sizeof vk[0]; i++) {
        drain(w);
        MSG m = { w, WM_KEYDOWN, vk[i], 1, 0, {0, 0} };
        /* TranslateMessage: no character -> result is ~(flags >> 1) & 1, i.e. TRUE for the flags value 0 that user32 passes */
        CHECKF(U(TranslateMessage)(&m), "vk 0x%x", (unsigned)vk[i]);
        MSG c; CHECKF(!take_msg(w, WM_CHAR, WM_DEADCHAR, &c), "vk 0x%x queued WM_CHAR", (unsigned)vk[i]);
    }
    U(DestroyWindow)(w);
}
TEST(gap10_translate_syscall_flags_bit1_turns_no_char_result_into_false) {
    REQUIRE(UN(NtUserTranslateMessage));
    HWND w = k_make_window("gap10-tm"); REQUIRE(w);
    drain(w);
    MSG m = { w, WM_KEYDOWN, VK_SHIFT, 1, 0, {0, 0} };
    CHECK(UN(NtUserTranslateMessage)(&m, 0));
    CHECK(!UN(NtUserTranslateMessage)(&m, 2));
    CHECK(UN(NtUserTranslateMessage)(&m, 1));
    MSG k = { w, WM_KEYDOWN, 'A', (LPARAM)(1 | (0x1e << 16)), 0, {0, 0} };
    CHECK(UN(NtUserTranslateMessage)(&k, 2));          /* a generated WM_CHAR always yields TRUE */
    drain(w);
    U(DestroyWindow)(w);
}
TEST(gap10_translate_keyup_returns_true_and_posts_nothing) {
    HWND w = k_make_window("gap10-tm"); REQUIRE(w);
    drain(w);
    MSG m = { w, WM_KEYUP, 'A', (LPARAM)(1 | (0x1e << 16) | (1u << 30) | (1u << 31)), 0, {0, 0} };
    CHECK(U(TranslateMessage)(&m));
    MSG c; CHECK(!take_msg(w, WM_CHAR, WM_DEADCHAR, &c));
    U(DestroyWindow)(w);
}
TEST(gap10_translate_syskeydown_posts_wm_syschar) {
    HWND w = k_make_window("gap10-tm"); REQUIRE(w);
    drain(w);
    MSG m = { w, WM_SYSKEYDOWN, 'A', (LPARAM)(1 | (0x1e << 16) | (1u << 29)), 0, {0, 0} };
    BOOL r = U(TranslateMessage)(&m);
    MSG c; BOOL got = take_msg(w, WM_SYSCHAR, WM_SYSCHAR, &c);
    CHECK(r);
    CHECK(got);
    if (got) G10_EQ(c.hwnd, w);
    U(DestroyWindow)(w);
}
TEST(gap10_translate_user32_filters_non_key_messages) {
    HWND w = k_make_window("gap10-tm"); REQUIRE(w);
    const UINT msgs[] = { WM_CHAR, WM_DEADCHAR, WM_NULL, WM_MOUSEMOVE, WM_PAINT, WM_USER, 0xFFFF, WM_KEYDOWN - 1 };
    for (unsigned i = 0; i < sizeof msgs / sizeof msgs[0]; i++) {
        drain(w);
        MSG m = { w, msgs[i], 'A', 1, 0, {0, 0} };
        SET_ERR(0);
        BOOL tr = U(TranslateMessage)(&m);
        DWORD tr_err = GetLastError();
        CHECKF(!tr, "message 0x%x translated", msgs[i]);
        CHECKF(tr_err == 0, "message 0x%x set error %lu", msgs[i], tr_err);   /* in-range messages leave the error alone */
        MSG c; CHECK(!take_msg(w, WM_CHAR, WM_SYSDEADCHAR, &c));
    }
    U(DestroyWindow)(w);
}
TEST(gap10_translate_message_with_high_bits_sets_invalid_parameter) {
    HWND w = k_make_window("gap10-tm"); REQUIRE(w);
    const UINT msgs[] = { 0x20000, 0x80000000u, 0xFFFFFFFFu, 0x12345678 };
    for (unsigned i = 0; i < 4; i++) {
        MSG m = { w, msgs[i], 'A', 1, 0, {0, 0} };
        SET_ERR(0);
        CHECK(!U(TranslateMessage)(&m));
        CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "message 0x%x: error %lu", msgs[i], GetLastError());
    }
    U(DestroyWindow)(w);
}
TEST(gap10_translate_syscall_invalid_window) {
    REQUIRE(UN(NtUserTranslateMessage));
    const ULONG_PTR h[] = { 0, 1, 0x1234, 0xFFFFFFFFull };
    for (unsigned i = 0; i < 4; i++) {
        MSG m = { (HWND)h[i], WM_KEYDOWN, 'A', 1, 0, {0, 0} };
        SET_ERR(0);
        CHECK(!UN(NtUserTranslateMessage)(&m, 0));       /* ValidateHwnd(msg->hwnd) fails */
        CHECKF(GetLastError() == ERROR_INVALID_WINDOW_HANDLE, "hwnd %p: error %lu", (void *)h[i], GetLastError());
    }
}
TEST(gap10_translate_syscall_destroyed_window) {
    REQUIRE(UN(NtUserTranslateMessage));
    HWND w = k_make_window("gap10-tm"); REQUIRE(w);
    U(DestroyWindow)(w);
    MSG m = { w, WM_KEYDOWN, 'A', 1, 0, {0, 0} };
    SET_ERR(0);
    CHECK(!UN(NtUserTranslateMessage)(&m, 0));
    EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
}
TEST(gap10_translate_syscall_copies_message_without_modifying_it) {
    REQUIRE(UN(NtUserTranslateMessage));
    HWND w = k_make_window("gap10-tm"); REQUIRE(w);
    drain(w);
    MSG m = { w, WM_KEYDOWN, 'B', (LPARAM)(1 | (0x30 << 16)), 0x1234, {7, 9} };
    MSG before = m;
    UN(NtUserTranslateMessage)(&m, 0);
    CHECK(!memcmp(&before, &m, sizeof m));              /* the kernel works on a private copy of the 0x30-byte MSG */
    drain(w);
    U(DestroyWindow)(w);
}
TEST(gap10_translate_char_repeat_count_is_preserved) {
    HWND w = k_make_window("gap10-tm"); REQUIRE(w);
    drain(w);
    MSG m = { w, WM_KEYDOWN, 'C', (LPARAM)(5 | (0x2e << 16) | (1u << 30)), 0, {0, 0} };
    BOOL r = U(TranslateMessage)(&m);
    MSG c;
    if (r && take_msg(w, WM_CHAR, WM_CHAR, &c)) G10_EQ(c.lParam & 0xFFFF, 5);
    else t_note("no WM_CHAR for VK_C (r=%d)", r);
    U(DestroyWindow)(w);
}
