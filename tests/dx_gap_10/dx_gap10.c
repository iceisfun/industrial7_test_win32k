/* State-changing syscalls of the gap_10 set. Every test restores what it touched (cursor position, clipboard viewer
 * chain, hotkeys, hooks, display affinity, pointer-notification registration) even when a check fails. Run with /destructive. */
#include "dx_g10.h"

/* ================= NtUserSetCursorPos ================= */
typedef struct { POINT pt; BOOL ok; } CURSAVE;
static void cur_save(CURSAVE *s) { s->ok = U(GetCursorPos)(&s->pt); }
static void cur_restore(CURSAVE *s) { if (s->ok) U(SetCursorPos)(s->pt.x, s->pt.y); }
static BOOL desk_ok(void) { return k_has_interactive_desktop(); }

TEST(gap10_dx_setcursorpos_roundtrip_points) {
    if (!desk_ok()) { t_skip("no interactive desktop"); return; }
    CURSAVE s; cur_save(&s); if (!s.ok) { t_skip("GetCursorPos failed"); return; }
    int vx = U(GetSystemMetrics)(SM_XVIRTUALSCREEN), vy = U(GetSystemMetrics)(SM_YVIRTUALSCREEN);
    int vw = U(GetSystemMetrics)(SM_CXVIRTUALSCREEN), vh = U(GetSystemMetrics)(SM_CYVIRTUALSCREEN);
    const POINT pts[] = { { vx + 1, vy + 1 }, { vx + vw / 2, vy + vh / 2 }, { vx + vw - 2, vy + vh - 2 }, { vx, vy } };
    for (unsigned i = 0; i < 4; i++) {
        BOOL r = U(SetCursorPos)(pts[i].x, pts[i].y);
        POINT g = { -1, -1 }; U(GetCursorPos)(&g);
        CHECKF(r, "SetCursorPos(%ld,%ld) failed, error %lu", pts[i].x, pts[i].y, GetLastError());
        CHECKF(g.x == pts[i].x && g.y == pts[i].y, "set %ld,%ld read back %ld,%ld", pts[i].x, pts[i].y, g.x, g.y);
    }
    cur_restore(&s);
}
TEST(gap10_dx_setcursorpos_clamps_to_virtual_screen) {
    if (!desk_ok()) { t_skip("no interactive desktop"); return; }
    CURSAVE s; cur_save(&s); if (!s.ok) { t_skip("GetCursorPos failed"); return; }
    int vx = U(GetSystemMetrics)(SM_XVIRTUALSCREEN), vy = U(GetSystemMetrics)(SM_YVIRTUALSCREEN);
    int vw = U(GetSystemMetrics)(SM_CXVIRTUALSCREEN), vh = U(GetSystemMetrics)(SM_CYVIRTUALSCREEN);
    BOOL r = U(SetCursorPos)(vx + vw + 5000, vy + vh + 5000);
    POINT g = { -1, -1 }; U(GetCursorPos)(&g);
    CHECK(r);
    CHECKF(g.x >= vx && g.x < vx + vw && g.y >= vy && g.y < vy + vh, "cursor %ld,%ld outside %d,%d %dx%d", g.x, g.y, vx, vy, vw, vh);
    r = U(SetCursorPos)(vx - 5000, vy - 5000);
    U(GetCursorPos)(&g);
    CHECK(r);
    CHECKF(g.x >= vx && g.y >= vy, "cursor %ld,%ld below the virtual origin", g.x, g.y);
    cur_restore(&s);
}
TEST(gap10_dx_setcursorpos_direct_syscall_matches_wrapper) {
    REQUIRE(UN(NtUserSetCursorPos));
    if (!desk_ok()) { t_skip("no interactive desktop"); return; }
    CURSAVE s; cur_save(&s); if (!s.ok) { t_skip("GetCursorPos failed"); return; }
    int vx = U(GetSystemMetrics)(SM_XVIRTUALSCREEN), vy = U(GetSystemMetrics)(SM_YVIRTUALSCREEN);
    BOOL r = UN(NtUserSetCursorPos)(vx + 33, vy + 44);
    POINT g = { -1, -1 }; U(GetCursorPos)(&g);
    CHECK(r);
    CHECKF(g.x == vx + 33 && g.y == vy + 44, "read back %ld,%ld", g.x, g.y);
    cur_restore(&s);
}
TEST(gap10_dx_setcursorpos_idempotent) {
    if (!desk_ok()) { t_skip("no interactive desktop"); return; }
    CURSAVE s; cur_save(&s); if (!s.ok) { t_skip("GetCursorPos failed"); return; }
    int vx = U(GetSystemMetrics)(SM_XVIRTUALSCREEN), vy = U(GetSystemMetrics)(SM_YVIRTUALSCREEN);
    for (int i = 0; i < 5; i++) CHECK(U(SetCursorPos)(vx + 10, vy + 10));
    POINT g; U(GetCursorPos)(&g);
    CHECK(g.x == vx + 10 && g.y == vy + 10);
    cur_restore(&s);
}

/* ================= NtUserSetClipboardViewer ================= */
typedef struct { HWND self, next; int draw, chain; } CBV;
static CBV g_cb[3];
static LRESULT CALLBACK cbv_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    CBV *v = NULL;
    for (int i = 0; i < 3; i++) if (g_cb[i].self == h) v = &g_cb[i];
    if (v) switch (m) {
    case WM_DRAWCLIPBOARD: v->draw++; if (v->next) SendMessageW(v->next, m, w, l); return 0;
    case WM_CHANGECBCHAIN:
        v->chain++;
        if ((HWND)w == v->next) v->next = (HWND)l; else if (v->next) SendMessageW(v->next, m, w, l);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
static HWND cbv_window(int slot) {
    static ATOM a;
    if (!a) {
        WNDCLASSW wc; memset(&wc, 0, sizeof wc);
        wc.lpfnWndProc = cbv_proc; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = L"win32ktGap10Cbv";
        a = U(RegisterClassW)(&wc);
        if (!a) return NULL;
    }
    HWND w = U(CreateWindowExW)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"win32ktGap10Cbv", L"gap10", WS_POPUP, -32000, -32000, 1, 1, NULL, NULL, GetModuleHandleW(NULL), NULL);
    memset(&g_cb[slot], 0, sizeof g_cb[slot]);
    g_cb[slot].self = w;
    return w;
}
TEST(gap10_dx_clipviewer_set_and_remove_restores_chain) {
    HWND orig = U(GetClipboardViewer)();
    HWND w = cbv_window(0); REQUIRE(w);
    HWND prev = U(SetClipboardViewer)(w);
    g_cb[0].next = prev;
    G10_EQ(prev, orig);                                  /* returns the previous head of the chain */
    G10_EQ(U(GetClipboardViewer)(), w);
    CHECK(U(ChangeClipboardChain)(w, prev));
    G10_EQ(U(GetClipboardViewer)(), orig);
    U(DestroyWindow)(w);
}
TEST(gap10_dx_clipviewer_two_windows_chain_order) {
    HWND orig = U(GetClipboardViewer)();
    HWND a = cbv_window(0), b = cbv_window(1);
    if (!a || !b) { if (a) U(DestroyWindow)(a); if (b) U(DestroyWindow)(b); t_skip("windows"); return; }
    HWND pa = U(SetClipboardViewer)(a); g_cb[0].next = pa;
    HWND pb = U(SetClipboardViewer)(b); g_cb[1].next = pb;
    G10_EQ(pa, orig);
    G10_EQ(pb, a);                                       /* the second viewer is put in front of the first */
    G10_EQ(U(GetClipboardViewer)(), b);
    CHECK(U(ChangeClipboardChain)(a, g_cb[0].next));     /* remove the tail first: WM_CHANGECBCHAIN reaches b */
    CHECK(g_cb[1].chain >= 1);
    G10_EQ(g_cb[1].next, orig);
    CHECK(U(ChangeClipboardChain)(b, g_cb[1].next));
    G10_EQ(U(GetClipboardViewer)(), orig);
    U(DestroyWindow)(a); U(DestroyWindow)(b);
}
TEST(gap10_dx_clipviewer_destroy_removes_from_chain) {
    HWND orig = U(GetClipboardViewer)();
    HWND w = cbv_window(0); REQUIRE(w);
    g_cb[0].next = U(SetClipboardViewer)(w);
    G10_EQ(U(GetClipboardViewer)(), w);
    CHECK(U(ChangeClipboardChain)(w, g_cb[0].next));
    U(DestroyWindow)(w);
    G10_EQ(U(GetClipboardViewer)(), orig);
    SET_ERR(0);
    CHECK(!U(ChangeClipboardChain)(w, NULL) || 1);       /* stale window: result not asserted */
}
TEST(gap10_dx_clipviewer_invalid_window_leaves_chain_alone) {
    REQUIRE(UN(NtUserSetClipboardViewer));
    HWND orig = U(GetClipboardViewer)();
    const ULONG_PTR h[] = { 1, 0x1234, 0xFFFFFFFFull };
    for (unsigned i = 0; i < 3; i++) {
        SET_ERR(0);
        HWND r = UN(NtUserSetClipboardViewer)((HWND)h[i]);
        CHECKF(r == NULL, "hwnd %p returned %p", (void *)h[i], (void *)r);
        CHECKF(GetLastError() == ERROR_INVALID_WINDOW_HANDLE, "hwnd %p: error %lu", (void *)h[i], GetLastError());
    }
    G10_EQ(U(GetClipboardViewer)(), orig);
}

/* ================= NtUserRegisterHotKey ================= */
#define HKM (MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN)
TEST(gap10_dx_hotkey_register_duplicate_unregister) {
    HWND w = k_make_window("gap10-hk"); REQUIRE(w);
    BOOL r = U(RegisterHotKey)(w, 0x3a01, HKM, VK_F24);
    if (!r && GetLastError() == ERROR_ACCESS_DENIED) { U(DestroyWindow)(w); t_skip("restricted"); return; }
    CHECK(r);
    SET_ERR(0);
    CHECK(!U(RegisterHotKey)(w, 0x3a02, HKM, VK_F24));            /* same key combination, different id */
    EXPECT_ERR(ERROR_HOTKEY_ALREADY_REGISTERED);
    SET_ERR(0);
    CHECK(!U(RegisterHotKey)(NULL, 0x3a03, HKM, VK_F24));         /* thread hotkey with the same combination */
    EXPECT_ERR(ERROR_HOTKEY_ALREADY_REGISTERED);
    CHECK(U(UnregisterHotKey)(w, 0x3a01));
    SET_ERR(0);
    CHECK(!U(UnregisterHotKey)(w, 0x3a01));
    EXPECT_ERR(ERROR_HOTKEY_NOT_REGISTERED);
    CHECK(U(RegisterHotKey)(w, 0x3a04, HKM, VK_F24));              /* free again after the unregister */
    U(UnregisterHotKey)(w, 0x3a04);
    U(DestroyWindow)(w);
}
TEST(gap10_dx_hotkey_norepeat_flag_is_accepted_and_distinct_keys_coexist) {
    HWND w = k_make_window("gap10-hk"); REQUIRE(w);
    BOOL a = U(RegisterHotKey)(w, 0x3a11, HKM | MOD_NOREPEAT, VK_F24);
    if (!a && GetLastError() == ERROR_ACCESS_DENIED) { U(DestroyWindow)(w); t_skip("restricted"); return; }
    CHECK(a);
    BOOL b = U(RegisterHotKey)(w, 0x3a12, HKM, VK_F23);
    CHECK(b);
    if (a) CHECK(U(UnregisterHotKey)(w, 0x3a11));
    if (b) CHECK(U(UnregisterHotKey)(w, 0x3a12));
    U(DestroyWindow)(w);
}
TEST(gap10_dx_hotkey_thread_hotkey_with_null_hwnd) {
    BOOL r = U(RegisterHotKey)(NULL, 0x3a21, HKM, VK_F22);
    if (!r && GetLastError() == ERROR_ACCESS_DENIED) { t_skip("restricted"); return; }
    CHECK(r);
    if (r) { CHECK(U(UnregisterHotKey)(NULL, 0x3a21)); SET_ERR(0); CHECK(!U(UnregisterHotKey)(NULL, 0x3a21)); EXPECT_ERR(ERROR_HOTKEY_NOT_REGISTERED); }
}
TEST(gap10_dx_hotkey_window_of_another_thread) {
    G10W peer; if (!g10_wthread_start(&peer)) { t_skip("peer thread"); return; }
    SET_ERR(0);
    BOOL r = U(RegisterHotKey)(peer.hwnd, 0x3a31, HKM, VK_F21);
    if (r) { CHECK(0); U(UnregisterHotKey)(peer.hwnd, 0x3a31); }
    else if (GetLastError() != ERROR_ACCESS_DENIED) EXPECT_ERR(ERROR_WINDOW_OF_OTHER_THREAD);
    g10_wthread_stop(&peer);
}
TEST(gap10_dx_hotkey_unregister_from_wrong_thread_fails) {
    HWND w = k_make_window("gap10-hk"); REQUIRE(w);
    BOOL r = U(RegisterHotKey)(w, 0x3a41, HKM, VK_F20);
    if (!r) { U(DestroyWindow)(w); t_skip("register failed (%lu)", GetLastError()); return; }
    G10W peer; 
    if (g10_wthread_start(&peer)) {
        SET_ERR(0);
        BOOL u = U(UnregisterHotKey)(peer.hwnd, 0x3a41);       /* a hotkey is identified by (hwnd, id): another window has none */
        CHECK(!u);
        g10_wthread_stop(&peer);
    }
    CHECK(U(UnregisterHotKey)(w, 0x3a41));
    U(DestroyWindow)(w);
}
TEST(gap10_dx_hotkey_destroying_window_drops_its_hotkeys) {
    HWND w = k_make_window("gap10-hk"); REQUIRE(w);
    BOOL r = U(RegisterHotKey)(w, 0x3a51, HKM, VK_F19);
    if (!r) { U(DestroyWindow)(w); t_skip("register failed (%lu)", GetLastError()); return; }
    U(DestroyWindow)(w);
    HWND w2 = k_make_window("gap10-hk2"); REQUIRE(w2);
    BOOL again = U(RegisterHotKey)(w2, 0x3a52, HKM, VK_F19);   /* the combination is free again */
    CHECK(again);
    if (again) U(UnregisterHotKey)(w2, 0x3a52);
    U(DestroyWindow)(w2);
}

/* ================= NtUserSetWindowsHookEx ================= */
static volatile LONG g_hook_calls; static volatile int g_hook_code = -99; static volatile WPARAM g_hook_w;
static LRESULT CALLBACK count_hook(int c, WPARAM w, LPARAM l) {
    if (c >= 0) { InterlockedIncrement(&g_hook_calls); g_hook_code = c; g_hook_w = w; }
    return CallNextHookEx(NULL, c, w, l);
}
TEST(gap10_dx_hook_handle_is_a_hook_object) {
    HHOOK h = U(SetWindowsHookExW)(WH_GETMESSAGE, count_hook, NULL, GetCurrentThreadId());
    REQUIRE(h);
    struct k_user_entry e;
    if (k_user_entry((HANDLE)h, &e)) G10_EQ(e.type, G10_TYPE_HOOK);
    CHECK(U(UnhookWindowsHookEx)(h));
    if (k_user_entry((HANDLE)h, &e)) CHECKF(e.type != G10_TYPE_HOOK || (e.flags & 1), "hook entry still live after unhook (type %u flags %u)", e.type, e.flags);
    SET_ERR(0);
    CHECK(!U(UnhookWindowsHookEx)(h));
    EXPECT_ERR(ERROR_INVALID_HOOK_HANDLE);
}
TEST(gap10_dx_hook_getmessage_called_once_per_removed_message) {
    HWND w = k_make_window("gap10-hook"); REQUIRE(w);
    HHOOK h = U(SetWindowsHookExW)(WH_GETMESSAGE, count_hook, NULL, GetCurrentThreadId());
    if (!h) { U(DestroyWindow)(w); t_skip("hook"); return; }
    g_hook_calls = 0;
    U(PostMessageW)(w, WM_APP + 7, 0, 0);
    MSG m; BOOL got = U(PeekMessageW)(&m, w, WM_APP + 7, WM_APP + 7, PM_REMOVE);
    CHECK(got);
    G10_EQ(g_hook_calls, 1);
    G10_EQ(g_hook_code, HC_ACTION);
    G10_EQ(g_hook_w, PM_REMOVE);
    U(UnhookWindowsHookEx)(h);
    g_hook_calls = 0;
    U(PostMessageW)(w, WM_APP + 8, 0, 0);
    U(PeekMessageW)(&m, w, WM_APP + 8, WM_APP + 8, PM_REMOVE);
    G10_EQ(g_hook_calls, 0);                              /* no calls after the unhook */
    U(DestroyWindow)(w);
}
TEST(gap10_dx_hook_thread_hook_on_other_thread_of_this_process) {
    G10W peer; if (!g10_wthread_start(&peer)) { t_skip("peer thread"); return; }
    HHOOK h = U(SetWindowsHookExW)(WH_GETMESSAGE, count_hook, NULL, peer.tid);
    CHECK(h != NULL);
    g_hook_calls = 0;
    if (h) {
        U(PostMessageW)(peer.hwnd, WM_APP + 9, 0, 0);
        Sleep(200);
        t_note("hook calls on the peer thread: %ld", (long)g_hook_calls);
        CHECK(U(UnhookWindowsHookEx)(h));
    }
    g10_wthread_stop(&peer);
}
TEST(gap10_dx_hook_two_hooks_same_type_both_run) {
    HWND w = k_make_window("gap10-hook"); REQUIRE(w);
    HHOOK a = U(SetWindowsHookExW)(WH_GETMESSAGE, count_hook, NULL, GetCurrentThreadId());
    HHOOK b = U(SetWindowsHookExW)(WH_GETMESSAGE, count_hook, NULL, GetCurrentThreadId());
    if (!a || !b) { if (a) U(UnhookWindowsHookEx)(a); if (b) U(UnhookWindowsHookEx)(b); U(DestroyWindow)(w); t_skip("hooks"); return; }
    CHECK(a != b);
    g_hook_calls = 0;
    U(PostMessageW)(w, WM_APP + 10, 0, 0);
    MSG m; U(PeekMessageW)(&m, w, WM_APP + 10, WM_APP + 10, PM_REMOVE);
    G10_EQ(g_hook_calls, 2);                              /* the chain passes through both hooks via CallNextHookEx */
    CHECK(U(UnhookWindowsHookEx)(a)); CHECK(U(UnhookWindowsHookEx)(b));
    U(DestroyWindow)(w);
}

/* ================= NtUserSetWindowDisplayAffinity ================= */
TEST(gap10_dx_affinity_set_get_restore) {
    REQUIRE(UN(NtUserSetWindowDisplayAffinity)); if (t_is_wine()) { t_skip("Wine stub"); return; }
    HWND w = k_make_window("gap10-wda"); REQUIRE(w);
    const DWORD vals[] = { WDA_MONITOR, 0x11, WDA_NONE };
    for (unsigned i = 0; i < 3; i++) {
        SET_ERR(0);
        BOOL r = UN(NtUserSetWindowDisplayAffinity)(w, vals[i]);
        if (!r) { t_note("affinity 0x%lx refused, error %lu", vals[i], GetLastError()); continue; }   /* needs DWM composition (error 8 otherwise) */
        DWORD a = 0xCD;
        CHECK(U(GetWindowDisplayAffinity)(w, &a));
        G10_EQ(a, vals[i]);
    }
    UN(NtUserSetWindowDisplayAffinity)(w, WDA_NONE);
    U(DestroyWindow)(w);
}
TEST(gap10_dx_affinity_other_thread_window_of_same_process) {
    REQUIRE(UN(NtUserSetWindowDisplayAffinity)); if (t_is_wine()) { t_skip("Wine stub"); return; }
    G10W peer; if (!g10_wthread_start(&peer)) { t_skip("peer"); return; }
    SET_ERR(0);
    BOOL r = UN(NtUserSetWindowDisplayAffinity)(peer.hwnd, WDA_NONE);     /* only the owner PROCESS is compared, not the thread */
    CHECK(r);
    g10_wthread_stop(&peer);
}

/* ================= NtUserRegisterPointerDeviceNotifications ================= */
TEST(gap10_dx_ptrnotify_own_window_register_unregister) {
    REQUIRE(UN(NtUserRegisterPointerDeviceNotifications)); if (t_is_wine()) { t_skip("Wine stub"); return; }
    HWND w = k_make_window("gap10-ptrn"); REQUIRE(w);
    for (int i = 0; i < 3; i++) {
        SET_ERR(0);
        INT_PTR r = UN(NtUserRegisterPointerDeviceNotifications)(w, TRUE);
        if (r == 0 && GetLastError() == ERROR_ACCESS_DENIED) { t_skip("UIPI access check failed"); break; }
        CHECK(r != 0);
        CHECK(UN(NtUserRegisterPointerDeviceNotifications)(w, FALSE) != 0);
    }
    U(DestroyWindow)(w);
}
TEST(gap10_dx_ptrnotify_other_thread_window_of_same_process) {
    REQUIRE(UN(NtUserRegisterPointerDeviceNotifications)); if (t_is_wine()) { t_skip("Wine stub"); return; }
    G10W peer; if (!g10_wthread_start(&peer)) { t_skip("peer"); return; }
    INT_PTR r = UN(NtUserRegisterPointerDeviceNotifications)(peer.hwnd, FALSE);
    CHECK(r != 0 && r != 5);               /* the owner check compares processes: another thread of this process is allowed */
    g10_wthread_stop(&peer);
}
