/* gap_11 / NtUserGetWindowDisplayAffinity (win32kfull 0x1fc570):
 *   EnterSharedCrit; pwnd = ValidateHwnd(hwnd) -> NULL: return 0 (error 1400)
 *   if (!_IsTopLevelWindow(pwnd)) { SetLastError(0x57); return 0; }       top level == parent is the desktop window
 *   GetDisplayAffinity(pwnd, &v)  (v = 0, then the SysDispAffinity property if the "has affinity" bit is set)
 *   store v through the probe-clamped out pointer; return 1.
 * Only the Get side is exercised here; Set changes composition state and lives in dx_gap_11. */
#include "g11.h"

#define WG_NEED() NtUserGetWindowDisplayAffinity_pfn gw = (NtUserGetWindowDisplayAffinity_pfn)g11_nt("NtUserGetWindowDisplayAffinity"); \
    if (!gw) { t_skip("missing stub"); return; }

static DWORD wg_call(NtUserGetWindowDisplayAffinity_pfn gw, HWND h, DWORD buf[2], DWORD *err) {
    buf[0] = buf[1] = 0xDEADBEEF;
    SET_ERR(G11_SENT);
    DWORD r = gw(h, buf);
    *err = GetLastError();
    return r;
}

TEST(gap11_wda_top_level_returns_one_and_zero_by_default) {
    WG_NEED(); G11_NEED_WND(w);
    DWORD b[2], e;
    G11_EQ(wg_call(gw, w, b, &e), 1);
    EXPECT_EQ(b[0], 0);                                      /* no affinity bit: GetDisplayAffinity leaves 0 */
    EXPECT_EQ(b[1], 0xDEADBEEF);                             /* exactly one DWORD stored */
    g11_kill(w);
}

TEST(gap11_wda_style_matrix_top_level) {
    WG_NEED();
    static const struct { DWORD style, ex; } t[] = {
        { WS_POPUP, 0 }, { WS_OVERLAPPED, 0 }, { WS_OVERLAPPEDWINDOW, 0 }, { WS_POPUP | WS_DISABLED, 0 },
        { WS_POPUP | WS_VISIBLE, 0 }, { WS_POPUP, WS_EX_LAYERED }, { WS_POPUP, WS_EX_TRANSPARENT },
        { WS_POPUP, WS_EX_TOPMOST }, { WS_POPUP, WS_EX_APPWINDOW }, { WS_POPUP | WS_MINIMIZE, 0 },
    };
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
        HWND w = g11_mk(t[i].style, t[i].ex, NULL);
        if (!w) { t_note("style row %u not creatable (err %lu)", i, GetLastError()); continue; }
        DWORD b[2], e;
        DWORD r = wg_call(gw, w, b, &e);
        CHECKF(r == 1 && b[0] == 0, "row %u: r=%lu v=%lu", i, r, b[0]);
        g11_kill(w);
    }
}

TEST(gap11_wda_owned_popup_is_top_level) {
    WG_NEED(); G11_NEED_WND(o);
    HWND p = g11_mk(WS_POPUP, 0, o);                         /* owned (not parented) when created WS_POPUP with hWndParent */
    if (!p) { g11_kill(o); t_skip("no owned window"); return; }
    DWORD b[2], e;
    G11_EQ(wg_call(gw, p, b, &e), 1);                     /* parent is still the desktop; owner is not consulted */
    g11_kill(o);
}

TEST(gap11_wda_child_window_invalid_parameter) {
    WG_NEED(); G11_NEED_WND(p);
    HWND c = g11_mk(WS_CHILD, 0, p);
    if (!c) { g11_kill(p); t_skip("no child"); return; }
    DWORD b[2], e;
    DWORD r = wg_call(gw, c, b, &e);
    CHECK(r == 0); EXPECT_EQ(e, ERROR_INVALID_PARAMETER);
    EXPECT_EQ(b[0], 0xDEADBEEF);                             /* failure stores nothing */
    g11_kill(p);
}

TEST(gap11_wda_grandchild_invalid_parameter) {
    WG_NEED(); G11_NEED_WND(p);
    HWND c = g11_mk(WS_CHILD, 0, p);
    HWND g = c ? g11_mk(WS_CHILD, 0, c) : NULL;
    if (!g) { g11_kill(p); t_skip("no grandchild"); return; }
    DWORD b[2], e;
    CHECK(wg_call(gw, g, b, &e) == 0); EXPECT_EQ(e, ERROR_INVALID_PARAMETER);
    g11_kill(p);
}

TEST(gap11_wda_message_only_window_not_top_level) {
    WG_NEED();
    HWND w = g11_mk(WS_POPUP, 0, HWND_MESSAGE);
    if (!w) { t_skip("no message-only window"); return; }
    DWORD b[2], e;
    DWORD r = wg_call(gw, w, b, &e);
    /* its parent is the message-only root, not the desktop window */
    CHECK(r == 0); EXPECT_EQ(e, ERROR_INVALID_PARAMETER);
    g11_kill(w);
}

TEST(gap11_wda_desktop_window_not_top_level) {
    WG_NEED(); REQUIRE(U(GetDesktopWindow));
    DWORD b[2], e;
    DWORD r = wg_call(gw, U(GetDesktopWindow)(), b, &e);
    /* the desktop window has no parent, so it is not "a child of the desktop" */
    CHECK(r == 0); EXPECT_EQ(e, ERROR_INVALID_PARAMETER);
}

TEST(gap11_wda_reparent_child_to_desktop_becomes_top_level) {
    WG_NEED(); REQUIRE(U(SetParent)); G11_NEED_WND(p);
    HWND c = g11_mk(WS_CHILD, 0, p);
    if (!c) { g11_kill(p); t_skip("no child"); return; }
    DWORD b[2], e;
    CHECK(wg_call(gw, c, b, &e) == 0);
    if (!U(SetParent)(c, NULL)) { g11_kill(p); g11_kill(c); t_skip("SetParent failed %lu", GetLastError()); return; }
    DWORD r = wg_call(gw, c, b, &e);                         /* top-level-ness follows the parent, not the style bits */
    CHECK(r == 1); EXPECT_EQ(b[0], 0);
    g11_kill(c); g11_kill(p);
}

TEST(gap11_wda_invalid_handles) {
    WG_NEED();
    HWND dead = g11_mk(WS_POPUP, 0, NULL); g11_kill(dead);
    HMENU m = U(CreatePopupMenu) ? U(CreatePopupMenu)() : NULL;
    HWND bad[] = { NULL, (HWND)(ULONG_PTR)-1, (HWND)(ULONG_PTR)0x7FFE7777, (HWND)(ULONG_PTR)0x0FFFFFF0, dead, (HWND)m };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        if (i >= 4 && !bad[i]) continue;
        DWORD b[2], e;
        DWORD r = wg_call(gw, bad[i], b, &e);
        CHECKF(r == 0 && e == ERROR_INVALID_WINDOW_HANDLE, "handle %p: r=%lu err %lu", (void *)bad[i], r, e);
        CHECKF(b[0] == 0xDEADBEEF, "handle %p wrote the output", (void *)bad[i]);
    }
    if (m) U(DestroyMenu)(m);
}

TEST(gap11_wda_window_of_other_thread) {
    WG_NEED();
    struct g11_peer p;
    if (!g11_peer_start(&p)) { g11_peer_stop(&p); t_skip("no peer window"); return; }
    DWORD b[2], e;
    G11_EQ(wg_call(gw, p.hwnd, b, &e), 1);                /* shared-crit query, no thread/process ownership test */
    EXPECT_EQ(b[0], 0);
    g11_peer_stop(&p);
}

TEST(gap11_wda_foreign_process_window_readable) {
    WG_NEED(); REQUIRE(U(GetShellWindow)); REQUIRE(U(GetWindowThreadProcessId));
    HWND sh = U(GetShellWindow)(); DWORD pid = 0;
    if (!sh) { t_skip("no shell window"); return; }
    U(GetWindowThreadProcessId)(sh, &pid);
    if (!pid || pid == GetCurrentProcessId()) { t_skip("shell in this process"); return; }
    DWORD b[2], e;
    DWORD r = wg_call(gw, sh, b, &e);
    /* unlike Set, Get has no process check; whether the shell window counts as top level is shell dependent */
    CHECK(r == 0 || r == 1);
    if (r) CHECK(b[0] == 0 || b[0] == 1 || b[0] == 0x11);
    t_note("Get(shell window) -> %lu value 0x%lx err %lu", r, b[0], e);
}

TEST(gap11_wda_value_fits_known_set) {
    WG_NEED(); G11_NEED_WND(w);
    DWORD b[2], e;
    CHECK(wg_call(gw, w, b, &e) == 1);
    CHECK(b[0] == 0 || b[0] == 1 || b[0] == 0x11);          /* WDA_NONE / WDA_MONITOR / WDA_EXCLUDEFROMCAPTURE are the only stored values */
    g11_kill(w);
}

TEST(gap11_wda_user32_path_matches_stub) {
    WG_NEED();
    FARPROC u = t_proc_in("user32.dll", "GetWindowDisplayAffinity");
    if (!u) { t_skip("no user32 export"); return; }
    BOOL (WINAPI *f)(HWND, DWORD *) = (BOOL (WINAPI *)(HWND, DWORD *))u;
    G11_NEED_WND(w);
    DWORD a[2], b[2], e;
    DWORD r = wg_call(gw, w, a, &e);
    b[0] = b[1] = 0xDEADBEEF;
    BOOL r2 = f(w, b);
    EXPECT_EQ(r2 != 0, r != 0);
    EXPECT_EQ(a[0], b[0]);
    SET_ERR(G11_SENT);
    CHECK(f(NULL, b) == 0); EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
    g11_kill(w);
}

TEST(gap11_wda_destroyed_after_query_fails) {
    WG_NEED(); G11_NEED_WND(w);
    DWORD b[2], e;
    CHECK(wg_call(gw, w, b, &e) == 1);
    g11_kill(w);
    CHECK(wg_call(gw, w, b, &e) == 0); EXPECT_EQ(e, ERROR_INVALID_WINDOW_HANDLE);
}
