/* UIPI / integrity / band: NtUserChangeWindowMessageFilterEx argument validation and ownership rule,
 * NtUserDisableProcessWindowFiltering and NtUserSetProcessUIAccessZorder (process-wide flags, set in a child),
 * NtUserGetWindowBand / NtUserSetWindowBand argument handling. */
#include "r2.h"

struct cfs { DWORD cbSize, ExtStatus; };

/* ---- ChangeWindowMessageFilterEx ---------------------------------------------------------------- */
RT(cwmfex_invalid_hwnds) {
    REQUIRE(UN(NtUserChangeWindowMessageFilterEx));
    for (unsigned i = 0; i < N_BAD_HWNDS; i++) {
        SET_ERR(SENT);
        ULONG_PTR r = UN(NtUserChangeWindowMessageFilterEx)((HWND)r2_bad_hwnds[i], WM_USER + 1, 1, NULL);
        CKF(R_D(r) == 0, "hwnd=%p", (void *)r2_bad_hwnds[i]);
        CKF(GetLastError() == ERR_INVALID_WINDOW, "hwnd=%p gle=%lu", (void *)r2_bad_hwnds[i], GetLastError());
    }
}
/* _ChangeWindowMessageFilterEx is reached only for windows of the caller's own process: another process's
 * window (here the desktop window, owned by the window-manager thread of another process) gives ERROR_ACCESS_DENIED. */
RT(cwmfex_window_of_other_process_is_denied) {
    REQUIRE(UN(NtUserChangeWindowMessageFilterEx)); REQUIRE(U(GetDesktopWindow));
    HWND dw = U(GetDesktopWindow)();
    DWORD pid = 0;
    if (U(GetWindowThreadProcessId)(dw, &pid) && pid == GetCurrentProcessId()) { t_skip("desktop window owned by this process"); return; }
    SET_ERR(SENT);
    CHECK(R_D(UN(NtUserChangeWindowMessageFilterEx)(dw, WM_USER + 1, 1, NULL)) == 0);
    EXPECT_ERR(ERR_ACCESS_DENIED);
}
/* A helper process owning one hidden window (class named after the parent's process id), pumping until WM_CLOSE. */
static LRESULT CALLBACK uipi_xw_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_CLOSE) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(h, m, w, l);
}
CHILD(r2_uipi_xwin) {
    (void)argc;
    char cls[64];
    wsprintfA(cls, "win32kt_r2xw_%lu", strtoul(argv[1], NULL, 10));
    WNDCLASSA wc; memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = uipi_xw_proc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = cls;
    RegisterClassA(&wc);
    HWND h = CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls, "xw", WS_POPUP, 10, 10, 40, 40, NULL, NULL, wc.hInstance, NULL);
    if (!h) return 90;
    MSG m; DWORD t0 = GetTickCount();
    while (GetTickCount() - t0 < 25000) {
        while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) { if (m.message == WM_QUIT) return 100; TranslateMessage(&m); DispatchMessageA(&m); }
        Sleep(5);
    }
    return 93;
}
RT(cwmfex_window_of_other_process_is_denied_helper_window) {
    REQUIRE(UN(NtUserChangeWindowMessageFilterEx)); REQUIRE(U(FindWindowA)); REQUIRE(U(PostMessageA));
    NEED_WIN(own);
    char a[32], cls[64];
    wsprintfA(a, "%lu", GetCurrentProcessId());
    wsprintfA(cls, "win32kt_r2xw_%lu", GetCurrentProcessId());
    PROCESS_INFORMATION pi;
    HANDLE hp = t_spawn_child("r2_uipi_xwin", a, 0, NULL, NULL, NULL, FALSE, NULL, &pi);
    CHECK(hp != NULL);
    HWND xw = NULL;
    for (int i = 0; hp && i < 500 && !xw; i++) { xw = U(FindWindowA)(cls, NULL); if (!xw) Sleep(20); }
    CHECK(xw != NULL);
    if (xw) {
        DWORD pid = 0;
        U(GetWindowThreadProcessId)(xw, &pid);
        CHECK(pid != 0 && pid != GetCurrentProcessId());
        /* a plain user window of another process: the same ownership rule as for the desktop window */
        SET_ERR(SENT);
        CHECK(R_D(UN(NtUserChangeWindowMessageFilterEx)(xw, WM_USER + 1, 1, NULL)) == 0);
        EXPECT_ERR(ERR_ACCESS_DENIED);
        U(PostMessageA)(xw, WM_CLOSE, 0, 0);
    }
    if (hp) {
        if (WaitForSingleObject(hp, 8000) != WAIT_OBJECT_0) { TerminateProcess(hp, 1); WaitForSingleObject(hp, 3000); }
        DWORD c = 0; GetExitCodeProcess(hp, &c);
        EXPECT_EQ(c, 100);
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    }
    END_WIN(own);
}
/* action must be 0 (reset), 1 (allow) or 2 (disallow): anything else is ERROR_INVALID_PARAMETER */
RT(cwmfex_invalid_action_values) {
    REQUIRE(UN(NtUserChangeWindowMessageFilterEx));
    NEED_WIN(w);
    static const DWORD acts[] = { 3, 4, 0x10, 0x80000000, 0xFFFFFFFF };
    for (unsigned i = 0; i < 5; i++) {
        SET_ERR(SENT);
        CKF(R_D(UN(NtUserChangeWindowMessageFilterEx)(w, WM_USER + 1, acts[i], NULL)) == 0, "action=%lx", acts[i]);
        CKF(GetLastError() == ERR_INVALID_PARAM, "action=%lx gle=%lu", acts[i], GetLastError());
    }
    END_WIN(w);
}
/* reset (action 0) takes message 0 only; allow/disallow take a non-zero message */
RT(cwmfex_message_zero_rules) {
    REQUIRE(UN(NtUserChangeWindowMessageFilterEx));
    NEED_WIN(w);
    SET_ERR(SENT); CHECK(R_D(UN(NtUserChangeWindowMessageFilterEx)(w, WM_USER + 1, 0, NULL)) == 0); EXPECT_ERR(ERR_INVALID_PARAM);
    SET_ERR(SENT); CHECK(R_D(UN(NtUserChangeWindowMessageFilterEx)(w, 0, 1, NULL)) == 0); EXPECT_ERR(ERR_INVALID_PARAM);
    SET_ERR(SENT); CHECK(R_D(UN(NtUserChangeWindowMessageFilterEx)(w, 0, 2, NULL)) == 0); EXPECT_ERR(ERR_INVALID_PARAM);
    END_WIN(w);
}
RT(cwmfex_struct_size_must_be_eight) {
    REQUIRE(UN(NtUserChangeWindowMessageFilterEx));
    NEED_WIN(w);
    static const DWORD sizes[] = { 0, 4, 7, 9, 12, 16, 0x100 };
    for (unsigned i = 0; i < 7; i++) {
        struct cfs s = { sizes[i], 0x55555555 };
        SET_ERR(SENT);
        CKF(R_D(UN(NtUserChangeWindowMessageFilterEx)(w, WM_USER + 0x2B1, 1, &s)) == 0, "cbSize=%lu", sizes[i]);
        CKF(GetLastError() == ERR_INVALID_PARAM, "cbSize=%lu gle=%lu", sizes[i], GetLastError());
        CKF(s.ExtStatus == 0x55555555, "cbSize=%lu ExtStatus rewritten", sizes[i]);
    }
    END_WIN(w);
}
RT(cwmfex_allow_then_reset_succeeds) {
    REQUIRE(UN(NtUserChangeWindowMessageFilterEx));
    NEED_WIN(w);
    struct cfs s = { 8, 0x55555555 };
    SET_ERR(SENT);
    ULONG_PTR r = UN(NtUserChangeWindowMessageFilterEx)(w, WM_USER + 0x2B2, 1, &s);   /* MSGFLT_ALLOW */
    CKF(R_D(r) == 1, "allow gle=%lu", GetLastError());
    CKF(s.ExtStatus <= 3, "ExtStatus=%lu", s.ExtStatus);   /* MSGFLTINFO_NONE..ALLOWED_HIGHER */
    t_note("first allow ExtStatus=%lu", s.ExtStatus);
    s.cbSize = 8; s.ExtStatus = 0x55555555;
    r = UN(NtUserChangeWindowMessageFilterEx)(w, WM_USER + 0x2B2, 1, &s);
    CK(R_D(r) == 1);
    t_note("second allow ExtStatus=%lu", s.ExtStatus);   /* MSGFLTINFO_ALREADYALLOWED_FORWND (1) when UIPI is enforced */
    s.cbSize = 8; s.ExtStatus = 0x55555555;
    r = UN(NtUserChangeWindowMessageFilterEx)(w, 0, 0, &s);   /* MSGFLT_RESET */
    CKF(R_D(r) == 1, "reset gle=%lu", GetLastError());
    END_WIN(w);
}
RT(cwmfex_allow_then_disallow_succeeds) {
    REQUIRE(UN(NtUserChangeWindowMessageFilterEx));
    NEED_WIN(w);
    struct cfs s = { 8, 0 };
    CK(R_D(UN(NtUserChangeWindowMessageFilterEx)(w, WM_USER + 0x2B3, 1, &s)) == 1);
    s.cbSize = 8; s.ExtStatus = 0x55555555;
    CK(R_D(UN(NtUserChangeWindowMessageFilterEx)(w, WM_USER + 0x2B3, 2, &s)) == 1);   /* MSGFLT_DISALLOW */
    CKF(s.ExtStatus <= 3, "ExtStatus=%lu", s.ExtStatus);
    CK(R_D(UN(NtUserChangeWindowMessageFilterEx)(w, 0, 0, NULL)) == 1);
    END_WIN(w);
}
RT(cwmfex_null_struct_is_accepted) {
    REQUIRE(UN(NtUserChangeWindowMessageFilterEx));
    NEED_WIN(w);
    SET_ERR(SENT);
    CK(R_D(UN(NtUserChangeWindowMessageFilterEx)(w, WM_USER + 0x2B4, 1, NULL)) == 1);   /* a cbSize=8 struct stands in */
    CK(R_D(UN(NtUserChangeWindowMessageFilterEx)(w, 0, 0, NULL)) == 1);
    END_WIN(w);
}
RT(cwmfex_user32_export_matches_stub) {
    typedef BOOL (WINAPI *pfn)(HWND, UINT, DWORD, void *);
    pfn f = (pfn)r2_proc_in("user32.dll", "ChangeWindowMessageFilterEx");
    REQUIRE(f);
    NEED_WIN(w);
    struct cfs s = { 8, 0 };
    CK(f(w, WM_USER + 0x2B5, MSGFLT_ALLOW, &s) == TRUE);
    CK(f(w, 0, MSGFLT_RESET, NULL) == TRUE);
    SET_ERR(SENT);
    CK(f(w, WM_USER + 0x2B5, 9, NULL) == FALSE);
    EXPECT_ERR(ERR_INVALID_PARAM);
    END_WIN(w);
}

/* ---- process-wide UIPI flags (child process) ------------------------------------------------------------ */
/* NtUserDisableProcessWindowFiltering sets bit 0x800 in the PROCESSINFO flags and 4 in the thread's extra flags,
 * always returning 1; NtUserSetProcessUIAccessZorder sets bit 2 unless the token is a limited one, also always 1. */
RC(uipi_flags) {
    (void)argc; (void)argv; g_cf = 0;
    if (!UN(NtUserDisableProcessWindowFiltering) || !UN(NtUserSetProcessUIAccessZorder) || !U(CreateWindowExW)) return 0;
    HWND w = r2_win();
    if (!w) { printf("NOTE no window\n"); return 0; }
    SET_ERR(SENT);
    CK(R_D(UN(NtUserDisableProcessWindowFiltering)()) == 1);
    CK(GetLastError() == SENT);
    CK(R_D(UN(NtUserDisableProcessWindowFiltering)()) == 1);   /* idempotent */
    SET_ERR(SENT);
    CK(R_D(UN(NtUserSetProcessUIAccessZorder)()) == 1);
    CK(GetLastError() == SENT);
    CK(R_D(UN(NtUserSetProcessUIAccessZorder)()) == 1);
    /* the process still owns a working window afterwards */
    CK(U(IsWindow)(w));
    CK(R_D(UN(NtUserChangeWindowMessageFilterEx)(w, WM_USER + 5, 1, NULL)) == 1);
    CK(R_D(UN(NtUserChangeWindowMessageFilterEx)(w, 0, 0, NULL)) == 1);
    U(DestroyWindow)(w);
    return g_cf;
}
RT(disable_process_window_filtering_and_uiaccess_zorder_in_child) {
    REQUIRE(UN(NtUserDisableProcessWindowFiltering));
    struct k_child_result r = k_child_run(RCN(uipi_flags), NULL, 30000);
    EXPECT_EQ(r.exit_code, 0);
}
/* ---- GetWindowBand / SetWindowBand -------------------------------------------------------------------- */
RT(get_window_band_invalid_hwnds) {
    REQUIRE(UN(NtUserGetWindowBand));
    for (unsigned i = 0; i < N_BAD_HWNDS; i++) {
        DWORD band = 0xCCCCCCCC;
        SET_ERR(SENT);
        ULONG_PTR r = UN(NtUserGetWindowBand)((HWND)r2_bad_hwnds[i], &band);
        CKF(R_D(r) == 0, "hwnd=%p", (void *)r2_bad_hwnds[i]);
        CKF(GetLastError() == ERR_INVALID_WINDOW, "hwnd=%p gle=%lu", (void *)r2_bad_hwnds[i], GetLastError());
        CKF(band == 0xCCCCCCCC, "band written for invalid hwnd");
    }
}
RT(get_window_band_null_out_pointer) {
    REQUIRE(UN(NtUserGetWindowBand));
    NEED_WIN(w);
    SET_ERR(SENT);
    UN(NtUserGetWindowBand)(w, NULL);
    EXPECT_ERR(ERR_INVALID_PARAM);
    END_WIN(w);
}
RT(get_window_band_reads_window_band_field) {
    REQUIRE(UN(NtUserGetWindowBand));
    NEED_WIN(w);
    DWORD band = 0xCCCCCCCC;
    ULONG_PTR r = UN(NtUserGetWindowBand)(w, &band);
    CKF(R_D(r) == 1, "r=%llx", (unsigned long long)r);
    CKF(band != 0xCCCCCCCC, "band not written");
    t_note("band of a plain hidden popup: %lu", band);
    END_WIN(w);
}
RT(get_window_band_same_for_peer_windows) {
    REQUIRE(UN(NtUserGetWindowBand));
    NEED_WIN(a); HWND b = r2_win();
    if (!b) { t_skip("second window failed"); END_WIN(a); return; }
    DWORD ba = 1, bb = 2;
    CK(R_D(UN(NtUserGetWindowBand)(a, &ba)) == 1);
    CK(R_D(UN(NtUserGetWindowBand)(b, &bb)) == 1);
    CKF(ba == bb, "bands %lu vs %lu", ba, bb);
    END_WIN(a); END_WIN(b);
}
RT(get_window_band_user32_export_matches_stub) {
    typedef BOOL (WINAPI *pfn)(HWND, DWORD *);
    pfn f = (pfn)r2_proc_in("user32.dll", "GetWindowBand");
    REQUIRE(f); REQUIRE(UN(NtUserGetWindowBand));
    NEED_WIN(w);
    DWORD a = 1, b = 2;
    CK(f(w, &a) == TRUE);
    CK(R_D(UN(NtUserGetWindowBand)(w, &b)) == 1);
    CKF(a == b, "%lu vs %lu", a, b);
    END_WIN(w);
}
RT(get_window_band_of_desktop_window_is_readable) {
    REQUIRE(UN(NtUserGetWindowBand)); REQUIRE(U(GetDesktopWindow));
    DWORD band = 0xCCCCCCCC;
    ULONG_PTR r = UN(NtUserGetWindowBand)(U(GetDesktopWindow)(), &band);
    CKF(R_D(r) == 1, "gle=%lu", GetLastError());
    t_note("desktop window band=%lu", band);
}
RT(set_window_band_invalid_hwnds) {
    REQUIRE(UN(NtUserSetWindowBand));
    for (unsigned i = 0; i < N_BAD_HWNDS; i++) {
        SET_ERR(SENT);
        ULONG_PTR r = UN(NtUserSetWindowBand)((HWND)r2_bad_hwnds[i], NULL, 1);
        CKF(R_D(r) == 0, "hwnd=%p", (void *)r2_bad_hwnds[i]);
    }
}
RT(set_window_band_desktop_window_rejected) {
    REQUIRE(UN(NtUserSetWindowBand)); REQUIRE(U(GetDesktopWindow));
    /* the FNID filter ((fnid & 0xffff2fff) - 0x29d & ~2 == 0 -> desktop/switch windows) returns 0 before any other check */
    SET_ERR(SENT);
    CK(R_D(UN(NtUserSetWindowBand)(U(GetDesktopWindow)(), NULL, 1)) == 0);
    EXPECT_ERR(SENT);
}
