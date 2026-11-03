/* DestroyWindow / NtUserDestroyWindow: message order, child and owned windows, handle lifetime and
 * re-use, ownership rules across threads, windows dying with their thread, and the USER state
 * (handle table entry, GetGuiResources) that must follow. */
#include "wcd.h"
#include <wchar.h>

#define NEED_USER() do { if (!wcd_user() || !U(CreateWindowExA)) { t_skip("user32 missing"); return; } } while (0)
#define HIDE_EX (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)

static HWND cw(DWORD ex, DWORD style, HWND parent, HMENU m) {
    return U(CreateWindowExW)(ex, wcd_cls(), L"d", style, -32000, -32000, 30, 30, parent, m, wcd_hinst(), NULL);
}

TEST(win_create_destroy_destroy_returns_true_and_invalidates) {
    NEED_USER();
    HWND h = wcd_make_top();
    REQUIRE(h);
    SetLastError(0);
    CHECK(U(DestroyWindow)(h));
    CHECK(!U(IsWindow)(h));
}

TEST(win_create_destroy_destroy_twice_fails_invalid_handle) {
    NEED_USER();
    HWND h = wcd_make_top();
    REQUIRE(h);
    CHECK(U(DestroyWindow)(h));
    SetLastError(0);
    CHECK(!U(DestroyWindow)(h));
    EXPECT_ERR(1400);
}

static const ULONG_PTR bad_handles[] = { 0, 0x12345678, 0x7fffffff, 0x00010000, 0x7ffe0000, 0xdeadbeef };
TEST(win_create_destroy_destroy_invalid_handles) {
    NEED_USER();
    for (unsigned i = 0; i < sizeof bad_handles / sizeof bad_handles[0]; i++) {
        SetLastError(0);
        BOOL r = U(DestroyWindow)((HWND)bad_handles[i]);
        CHECKF(!r, "handle 0x%llx", (unsigned long long)bad_handles[i]);
        EXPECT_ERR(1400);
    }
}

TEST(win_create_destroy_destroyed_handle_operations_fail) {
    NEED_USER();
    HWND h = wcd_make_top();
    REQUIRE(h);
    wcd_destroy(h);
    SetLastError(0);
    CHECK(!U(IsWindow)(h));
    CHECK(!U(IsWindowVisible)(h));
    CHECK(!U(IsWindowEnabled)(h));
    CHECK(!U(IsIconic)(h));
    CHECK(!U(IsZoomed)(h));
    SetLastError(0);
    CHECK(U(GetWindowLongW)(h, GWL_STYLE) == 0);
    EXPECT_ERR(1400);
    SetLastError(0);
    CHECK(U(GetParent)(h) == NULL);
    SetLastError(0);
    RECT r;
    CHECK(!U(GetWindowRect)(h, &r));
    EXPECT_ERR(1400);
    SetLastError(0);
    CHECK(!U(GetClientRect)(h, &r));
    EXPECT_ERR(1400);
    SetLastError(0);
    CHECK(!U(ShowWindow)(h, SW_SHOW));
    SetLastError(0);
    CHECK(!U(SetWindowTextW)(h, L"x"));
    SetLastError(0);
    CHECK(U(GetWindowTextLengthW)(h) == 0);
    SetLastError(0);
    CHECK(U(SendMessageW)(h, WM_NULL, 0, 0) == 0);
    EXPECT_ERR(1400);
    SetLastError(0);
    CHECK(!U(PostMessageW)(h, WM_NULL, 0, 0));
    EXPECT_ERR(1400);
    DWORD pid = 77;
    SetLastError(0);
    CHECK(U(GetWindowThreadProcessId)(h, &pid) == 0);
    EXPECT_ERR(1400);
}

TEST(win_create_destroy_destroyed_handle_entry_state) {
    NEED_USER();
    HWND h = wcd_make_top();
    REQUIRE(h);
    struct k_user_entry e;
    if (!k_user_entry(h, &e)) { t_skip("USER handle table not readable"); wcd_destroy(h); return; }
    CHECK(wcd_entry_live(h));
    wcd_destroy(h);
    CHECK(!wcd_entry_live(h));
    CHECK(k_wnd_ptr(h) == NULL || !U(IsWindow)(h));
}

TEST(win_create_destroy_destroyed_handle_not_reused_value) {
    NEED_USER();
    HWND hist[48];
    int n = 0;
    int same_index = 0;
    for (int i = 0; i < 48; i++) {
        HWND h = wcd_make_top();
        REQUIRE(h);
        for (int j = 0; j < n; j++) {
            CHECKF(h != hist[j], "handle %p handed out again (cycle %d, earlier %d)", h, i, j);
            if (((ULONG_PTR)h & 0xffff) == ((ULONG_PTR)hist[j] & 0xffff)) same_index++;
        }
        wcd_destroy(h);
        hist[n++] = h;
    }
    t_note("slot index reused for %d of %d later creations", same_index, 48 * 47 / 2);
    /* a stale handle stays invalid even when its table slot is occupied again (wUniq mismatch) */
    for (int j = 0; j < n; j++) CHECK(!U(IsWindow)(hist[j]));
}

TEST(win_create_destroy_stale_handle_after_slot_reuse) {
    NEED_USER();
    HWND a = wcd_make_top();
    REQUIRE(a);
    wcd_destroy(a);
    HWND b = wcd_make_top();
    REQUIRE(b);
    if (((ULONG_PTR)a & 0xffff) == ((ULONG_PTR)b & 0xffff)) {
        /* same slot: only the uniqueness word tells them apart */
        CHECK(((ULONG_PTR)a >> 16) != ((ULONG_PTR)b >> 16));
        t_info("slot %lx reused, uniq %lx -> %lx", (unsigned long)((ULONG_PTR)a & 0xffff), (unsigned long)((ULONG_PTR)a >> 16), (unsigned long)((ULONG_PTR)b >> 16));
    } else t_note("slot not reused immediately (%p -> %p)", a, b);
    CHECK(!U(IsWindow)(a));
    CHECK(U(IsWindow)(b));
    SetLastError(0);
    CHECK(!U(DestroyWindow)(a));
    EXPECT_ERR(1400);
    CHECK(U(IsWindow)(b));
    wcd_destroy(b);
}

/* ---- message order on destroy ------------------------------------------------------------------ */
TEST(win_create_destroy_destroy_msg_order_simple) {
    NEED_USER();
    HWND h = wcd_make_top();
    REQUIRE(h);
    wcd_reset();
    CHECK(U(DestroyWindow)(h));
    int d = wcd_find_hwnd(WM_DESTROY, h, 0), n = wcd_find_hwnd(WM_NCDESTROY, h, 0);
    CHECK(d >= 0 && n > d);
    CHECK(wcd_count(WM_DESTROY) == 1 && wcd_count(WM_NCDESTROY) == 1);
    if (d >= 0) CHECK(wcd_log[d].is_window);   /* the window is fully alive during WM_DESTROY */
    if (n >= 0) t_note("IsWindow during WM_NCDESTROY: %d", wcd_log[n].is_window);
    if (d >= 0) { CHECK(wcd_log[d].wp == 0 && wcd_log[d].lp == 0); }
    if (n >= 0) { CHECK(wcd_log[n].wp == 0 && wcd_log[n].lp == 0); }
}

TEST(win_create_destroy_destroy_msg_order_children) {
    NEED_USER();
    HWND p = wcd_make_top();
    REQUIRE(p);
    HWND c1 = cw(0, WS_CHILD, p, (HMENU)1);
    HWND c2 = cw(0, WS_CHILD, p, (HMENU)2);
    HWND g = cw(0, WS_CHILD, c1, (HMENU)3);
    REQUIRE(c1 && c2 && g);
    wcd_reset();
    CHECK(U(DestroyWindow)(p));
    int dp = wcd_find_hwnd(WM_DESTROY, p, 0), d1 = wcd_find_hwnd(WM_DESTROY, c1, 0),
        d2 = wcd_find_hwnd(WM_DESTROY, c2, 0), dg = wcd_find_hwnd(WM_DESTROY, g, 0);
    int np = wcd_find_hwnd(WM_NCDESTROY, p, 0), n1 = wcd_find_hwnd(WM_NCDESTROY, c1, 0),
        n2 = wcd_find_hwnd(WM_NCDESTROY, c2, 0), ng = wcd_find_hwnd(WM_NCDESTROY, g, 0);
    CHECK(dp >= 0 && d1 >= 0 && d2 >= 0 && dg >= 0);
    CHECK(np >= 0 && n1 >= 0 && n2 >= 0 && ng >= 0);
    /* destruction: WM_DESTROY to the window, then recursively to each child */
    CHECK(dp < d1 && dp < d2 && d1 < dg);
    /* window freeing frees children before the parent's own WM_NCDESTROY */
    CHECK(n1 < np && n2 < np && ng < n1);
    CHECK(!U(IsWindow)(p) && !U(IsWindow)(c1) && !U(IsWindow)(c2) && !U(IsWindow)(g));
}

TEST(win_create_destroy_destroy_children_only_one_branch) {
    NEED_USER();
    HWND p = wcd_make_top();
    HWND c1 = cw(0, WS_CHILD, p, (HMENU)1);
    HWND c2 = cw(0, WS_CHILD, p, (HMENU)2);
    REQUIRE(p && c1 && c2);
    CHECK(U(DestroyWindow)(c1));
    CHECK(U(IsWindow)(p) && U(IsWindow)(c2));
    CHECK(!U(IsWindow)(c1));
    CHECK(U(GetWindow)(p, GW_CHILD) == c2);
    CHECK(!U(IsChild)(p, c1));
    wcd_destroy(p);
}

TEST(win_create_destroy_destroy_parentnotify_on_child_destroy) {
    NEED_USER();
    HWND p = wcd_make_top();
    HWND c = cw(0, WS_CHILD, p, (HMENU)0x55);
    REQUIRE(p && c);
    wcd_reset();
    CHECK(U(DestroyWindow)(c));
    int k = wcd_find_hwnd(WM_PARENTNOTIFY, p, 0);
    CHECK(k >= 0);
    if (k >= 0) {
        CHECK(LOWORD(wcd_log[k].wp) == WM_DESTROY);   /* DestroyWindow: 0x210, (id<<16)|2, hwnd */
        CHECK(HIWORD(wcd_log[k].wp) == 0x55);
        CHECK((HWND)wcd_log[k].lp == c);
        int d = wcd_find_hwnd(WM_DESTROY, c, 0);
        CHECK(d >= 0 && k < d);
    }
    wcd_destroy(p);
}

TEST(win_create_destroy_destroy_no_parentnotify_with_exstyle) {
    NEED_USER();
    HWND p = wcd_make_top();
    HWND c = cw(WS_EX_NOPARENTNOTIFY, WS_CHILD, p, (HMENU)1);
    REQUIRE(p && c);
    wcd_reset();
    CHECK(U(DestroyWindow)(c));
    CHECK(wcd_find_hwnd(WM_PARENTNOTIFY, p, 0) < 0);
    wcd_destroy(p);
}

TEST(win_create_destroy_destroy_owned_windows_first) {
    NEED_USER();
    HWND o = wcd_make_top();
    REQUIRE(o);
    HWND a = U(CreateWindowExW)(HIDE_EX, wcd_cls(), L"a", WS_POPUP, -32000, -32000, 10, 10, o, NULL, wcd_hinst(), NULL);
    HWND b = U(CreateWindowExW)(HIDE_EX, wcd_cls(), L"b", WS_POPUP, -32000, -32000, 10, 10, o, NULL, wcd_hinst(), NULL);
    REQUIRE(a && b);
    wcd_reset();
    CHECK(U(DestroyWindow)(o));
    CHECK(!U(IsWindow)(a) && !U(IsWindow)(b) && !U(IsWindow)(o));
    int da = wcd_find_hwnd(WM_DESTROY, a, 0), db = wcd_find_hwnd(WM_DESTROY, b, 0), dow = wcd_find_hwnd(WM_DESTROY, o, 0);
    int nA = wcd_find_hwnd(WM_NCDESTROY, a, 0), nO = wcd_find_hwnd(WM_NCDESTROY, o, 0);
    CHECK(da >= 0 && db >= 0 && dow >= 0);
    /* owned-window destruction runs before the owner's own WM_DESTROY */
    CHECK(da < dow && db < dow);
    CHECK(nA >= 0 && nA < nO);
}

TEST(win_create_destroy_destroy_owned_chain) {
    NEED_USER();
    HWND w[5];
    w[0] = wcd_make_top();
    REQUIRE(w[0]);
    for (int i = 1; i < 5; i++) {
        w[i] = U(CreateWindowExW)(HIDE_EX, wcd_cls(), L"o", WS_POPUP, -32000, -32000, 10, 10, w[i - 1], NULL, wcd_hinst(), NULL);
        REQUIRE(w[i]);
        CHECK(U(GetWindow)(w[i], GW_OWNER) == w[i - 1]);
    }
    wcd_destroy(w[0]);
    for (int i = 0; i < 5; i++) CHECK(!U(IsWindow)(w[i]));
}

TEST(win_create_destroy_destroy_owned_with_children) {
    NEED_USER();
    HWND o = wcd_make_top();
    HWND p = U(CreateWindowExW)(HIDE_EX, wcd_cls(), L"p", WS_POPUP, -32000, -32000, 10, 10, o, NULL, wcd_hinst(), NULL);
    HWND c = cw(0, WS_CHILD, p, (HMENU)1);
    REQUIRE(o && p && c);
    wcd_destroy(o);
    CHECK(!U(IsWindow)(p));
    CHECK(!U(IsWindow)(c));
}

TEST(win_create_destroy_destroy_menu_with_window) {
    NEED_USER();
    if (!U(CreateMenu) || !U(AppendMenuW) || !U(IsMenu)) { t_skip("menu API"); return; }
    HMENU m = U(CreateMenu)(), sub = U(CreatePopupMenu)();
    REQUIRE(m && sub);
    U(AppendMenuW)(m, MF_POPUP, (UINT_PTR)sub, L"sub");
    HWND h = U(CreateWindowExW)(HIDE_EX, wcd_cls(), L"m", WS_POPUP, -32000, -32000, 10, 10, NULL, m, wcd_hinst(), NULL);
    REQUIRE(h);
    CHECK(U(IsMenu)(m) && U(IsMenu)(sub));
    wcd_destroy(h);
    CHECK(!U(IsMenu)(m));
    CHECK(!U(IsMenu)(sub));
}

TEST(win_create_destroy_destroy_menu_detached_survives) {
    NEED_USER();
    if (!U(CreateMenu) || !U(SetMenu) || !U(IsMenu)) { t_skip("menu API"); return; }
    HMENU m = U(CreateMenu)();
    REQUIRE(m);
    HWND h = U(CreateWindowExW)(HIDE_EX, wcd_cls(), L"m", WS_POPUP, -32000, -32000, 10, 10, NULL, m, wcd_hinst(), NULL);
    REQUIRE(h);
    CHECK(U(SetMenu)(h, NULL));
    wcd_destroy(h);
    CHECK(U(IsMenu)(m));   /* no longer owned by the window */
    U(DestroyMenu)(m);
}

TEST(win_create_destroy_destroy_timer_dies_with_window) {
    NEED_USER();
    if (!U(SetTimer) || !U(KillTimer)) { t_skip("timer API"); return; }
    HWND h = wcd_make_top();
    REQUIRE(h);
    CHECK(U(SetTimer)(h, 7, 20, NULL) == 7);
    wcd_destroy(h);
    wcd_reset();
    k_pump(100);
    CHECK(wcd_count(WM_TIMER) == 0);
    SetLastError(0);
    CHECK(!U(KillTimer)(h, 7));
}

TEST(win_create_destroy_destroy_props_die_with_window) {
    NEED_USER();
    if (!U(SetPropW) || !U(GetPropW)) { t_skip("prop API"); return; }
    HWND h = wcd_make_top();
    REQUIRE(h);
    CHECK(U(SetPropW)(h, L"win32kt_prop", (HANDLE)0x99));
    CHECK(U(GetPropW)(h, L"win32kt_prop") == (HANDLE)0x99);
    wcd_destroy(h);
    SetLastError(0);
    CHECK(U(GetPropW)(h, L"win32kt_prop") == NULL);
}

TEST(win_create_destroy_destroy_posted_messages_discarded) {
    NEED_USER();
    HWND h = wcd_make_top();
    REQUIRE(h);
    wcd_reset();
    for (int i = 0; i < 10; i++) U(PostMessageW)(h, WM_USER + 1, i, 0);
    wcd_destroy(h);
    k_pump(60);
    /* messages queued for a window are discarded together with it */
    CHECK(wcd_count(WM_USER + 1) == 0);
    MSG m;
    CHECK(!U(PeekMessageW)(&m, NULL, WM_USER + 1, WM_USER + 1, PM_REMOVE));
}

TEST(win_create_destroy_destroy_from_own_wndproc) {
    NEED_USER();
    wcd_reset();
    HWND h = wcd_make_top();
    REQUIRE(h);
    /* DestroyWindow reached through a sent message of the window's own thread */
    CHECK(U(SendMessageW)(h, WM_CLOSE, 0, 0) == 0);  /* DefWindowProc(WM_CLOSE) = DestroyWindow */
    CHECK(!U(IsWindow)(h));
    CHECK(wcd_count(WM_DESTROY) == 1);
    CHECK(wcd_count(WM_NCDESTROY) == 1);
}

static HWND g_reent;
static int g_reent_ret = -1;
static void reent_cb(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY && g_reent == h) {
        g_reent = NULL;                      /* once only: do not rely on the kernel to stop a recursion */
        SetLastError(0);
        g_reent_ret = U(DestroyWindow)(h);   /* second DestroyWindow while destroying */
    }
}
TEST(win_create_destroy_destroy_reentrant_in_wm_destroy) {
    NEED_USER();
    HWND h = wcd_make_top();
    REQUIRE(h);
    wcd_reset();
    g_reent = h; g_reent_ret = -1;
    wcd_msg_cb = reent_cb;
    CHECK(U(DestroyWindow)(h));
    wcd_msg_cb = NULL;
    t_note("nested DestroyWindow inside WM_DESTROY returned %d", g_reent_ret);
    t_note("WM_DESTROY delivered %d times, WM_NCDESTROY %d times", wcd_count(WM_DESTROY), wcd_count(WM_NCDESTROY));
    CHECK(wcd_count(WM_NCDESTROY) >= 1);
    CHECK(!U(IsWindow)(h));
}

static HWND g_kill;
static void destroy_other_cb(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY && g_kill) { HWND k = g_kill; g_kill = NULL; U(DestroyWindow)(k); }
}
TEST(win_create_destroy_destroy_sibling_inside_wm_destroy) {
    NEED_USER();
    HWND a = wcd_make_top(), b = wcd_make_top();
    REQUIRE(a && b);
    wcd_reset();
    g_kill = b;
    wcd_msg_cb = destroy_other_cb;
    CHECK(U(DestroyWindow)(a));
    wcd_msg_cb = NULL;
    CHECK(!U(IsWindow)(a));
    CHECK(!U(IsWindow)(b));
}

/* ---- cross thread ------------------------------------------------------------------------------- */
TEST(win_create_destroy_destroy_other_thread_access_denied) {
    NEED_USER();
    struct wcd_gt g; memset(&g, 0, sizeof g);
    CHECK(wcd_gt_start(&g, 1));
    if (!g.top) { wcd_gt_stop(&g); t_skip("helper window"); return; }
    SetLastError(0);
    /* DestroyWindow: window belongs to another thread and the entry is not flagged for destruction */
    CHECK(!U(DestroyWindow)(g.top));
    EXPECT_ERR(5);
    CHECK(U(IsWindow)(g.top));
    wcd_gt_stop(&g);
}

TEST(win_create_destroy_destroy_other_thread_access_denied_direct) {
    NEED_USER();
    if (!UN(NtUserDestroyWindow)) { t_skip("no stub"); return; }
    struct wcd_gt g; memset(&g, 0, sizeof g);
    CHECK(wcd_gt_start(&g, 1));
    if (!g.top) { wcd_gt_stop(&g); t_skip("helper window"); return; }
    SetLastError(0);
    CHECK((int)UN(NtUserDestroyWindow)(g.top) == 0);
    EXPECT_ERR(5);
    CHECK(U(IsWindow)(g.top));
    wcd_gt_stop(&g);
}

TEST(win_create_destroy_destroy_own_child_of_foreign_parent) {
    NEED_USER();
    struct wcd_gt g; memset(&g, 0, sizeof g);
    CHECK(wcd_gt_start(&g, 1));
    if (!g.top) { wcd_gt_stop(&g); t_skip("helper window"); return; }
    HWND c = U(CreateWindowExW)(WS_EX_NOPARENTNOTIFY, wcd_cls(), L"c", WS_CHILD, 0, 0, 5, 5, g.top, (HMENU)1, wcd_hinst(), NULL);
    REQUIRE(c);
    SetLastError(0);
    CHECK(U(DestroyWindow)(c));   /* created by this thread: we own it, even though the parent is foreign */
    CHECK(!U(IsWindow)(c));
    CHECK(U(IsWindow)(g.top));
    wcd_gt_stop(&g);
}

struct creator { HWND h; HANDLE ev_made, ev_go; DWORD ret_destroy; DWORD err; };
static DWORD WINAPI creator_proc(LPVOID p) {
    struct creator *c = (struct creator *)p;
    c->h = wcd_make_top();
    SetEvent(c->ev_made);
    WaitForSingleObject(c->ev_go, 5000);
    return 0;
}
TEST(win_create_destroy_window_dies_with_creating_thread) {
    NEED_USER();
    struct creator c; memset(&c, 0, sizeof c);
    c.ev_made = CreateEventW(NULL, TRUE, FALSE, NULL);
    c.ev_go = CreateEventW(NULL, TRUE, FALSE, NULL);
    HANDLE th = CreateThread(NULL, 0, creator_proc, &c, 0, NULL);
    REQUIRE(th);
    CHECK(WaitForSingleObject(c.ev_made, 5000) == WAIT_OBJECT_0);
    HWND h = c.h;
    CHECK(h != NULL);
    CHECK(U(IsWindow)(h));
    SetEvent(c.ev_go);
    WaitForSingleObject(th, 5000);
    CloseHandle(th);
    /* win32k's thread cleanup destroys the thread's windows */
    for (int i = 0; i < 100 && U(IsWindow)(h); i++) Sleep(20);
    CHECK(!U(IsWindow)(h));
    CHECK(!wcd_entry_live(h));
    CloseHandle(c.ev_made); CloseHandle(c.ev_go);
}

TEST(win_create_destroy_children_die_with_parent_thread) {
    NEED_USER();
    struct wcd_gt g; memset(&g, 0, sizeof g);
    CHECK(wcd_gt_start(&g, 1));
    if (!g.top) { wcd_gt_stop(&g); t_skip("helper window"); return; }
    HWND own = g.top;
    HWND c = U(CreateWindowExW)(WS_EX_NOPARENTNOTIFY, wcd_cls(), L"c", WS_CHILD, 0, 0, 5, 5, own, (HMENU)1, wcd_hinst(), NULL);
    REQUIRE(c);
    wcd_gt_stop(&g);   /* helper thread exits; its top-level window goes away */
    for (int i = 0; i < 100 && U(IsWindow)(own); i++) Sleep(20);
    CHECK(!U(IsWindow)(own));
    /* a child lives and dies with its parent, even when another thread created it; the creating thread has to pump
     * messages for the destruction to reach it */
    for (int i = 0; i < 100 && U(IsWindow)(c); i++) {
        MSG m; while (U(PeekMessageW)(&m, NULL, 0, 0, PM_REMOVE)) { U(TranslateMessage)(&m); U(DispatchMessageW)(&m); }
        Sleep(20);
    }
    CHECK(!U(IsWindow)(c));
}

TEST(win_create_destroy_owned_window_of_dead_thread_owner) {
    NEED_USER();
    struct wcd_gt g; memset(&g, 0, sizeof g);
    CHECK(wcd_gt_start(&g, 1));
    if (!g.top) { wcd_gt_stop(&g); t_skip("helper window"); return; }
    HWND own = g.top;
    HWND p = U(CreateWindowExW)(HIDE_EX | WS_EX_NOPARENTNOTIFY, wcd_cls(), L"p", WS_POPUP, -32000, -32000, 10, 10, own, NULL, wcd_hinst(), NULL);
    REQUIRE(p);
    CHECK(U(GetWindow)(p, GW_OWNER) == own);
    wcd_gt_stop(&g);
    for (int i = 0; i < 100 && U(IsWindow)(own); i++) Sleep(20);
    CHECK(!U(IsWindow)(own));
    /* DestroyWindow only posts an "unowned" notification to owned windows of other threads
     * (PostUnownedNotification), so whether p is already gone depends on this thread pumping */
    t_note("owned window after owner thread death: alive=%d owner=%p", U(IsWindow)(p), U(IsWindow)(p) ? U(GetWindow)(p, GW_OWNER) : NULL);
    k_pump(100);
    t_note("... after pumping: alive=%d", U(IsWindow)(p));
    wcd_destroy(p);
    CHECK(!U(IsWindow)(p));
}

TEST(win_create_destroy_many_threads_create_and_die) {
    NEED_USER();
    enum { N = 8 };
    struct creator c[N]; HANDLE th[N];
    memset(c, 0, sizeof c);
    for (int i = 0; i < N; i++) {
        c[i].ev_made = CreateEventW(NULL, TRUE, FALSE, NULL);
        c[i].ev_go = CreateEventW(NULL, TRUE, FALSE, NULL);
        th[i] = CreateThread(NULL, 0, creator_proc, &c[i], 0, NULL);
    }
    for (int i = 0; i < N; i++) WaitForSingleObject(c[i].ev_made, 5000);
    for (int i = 0; i < N; i++) { CHECK(c[i].h != NULL); CHECK(U(IsWindow)(c[i].h)); }
    for (int i = 0; i < N; i++) SetEvent(c[i].ev_go);
    for (int i = 0; i < N; i++) { WaitForSingleObject(th[i], 5000); CloseHandle(th[i]); }
    for (int i = 0; i < N; i++) {
        for (int k = 0; k < 100 && U(IsWindow)(c[i].h); k++) Sleep(20);
        CHECKF(!U(IsWindow)(c[i].h), "thread %d window survived", i);
        CloseHandle(c[i].ev_made); CloseHandle(c[i].ev_go);
    }
}

TEST(win_create_destroy_thread_window_belongs_to_that_thread) {
    NEED_USER();
    struct wcd_gt g; memset(&g, 0, sizeof g);
    CHECK(wcd_gt_start(&g, 1));
    if (!g.top) { wcd_gt_stop(&g); t_skip("helper window"); return; }
    DWORD pid = 0;
    CHECK(U(GetWindowThreadProcessId)(g.top, &pid) == g.tid);
    CHECK(pid == GetCurrentProcessId());
    CHECK(g.tid != GetCurrentThreadId());
    void *sw = wcd_sw(g.top);
    CHECK(sw == NULL || U(IsWindow)(g.top));
    wcd_gt_stop(&g);
}

/* ---- foreign windows ---------------------------------------------------------------------------------- */
TEST(win_create_destroy_destroy_desktop_window_denied) {
    NEED_USER();
    HWND d = U(GetDesktopWindow)();
    REQUIRE(d);
    SetLastError(0);
    CHECK(!U(DestroyWindow)(d));
    EXPECT_ERR(5);
    CHECK(U(IsWindow)(d));
}

TEST(win_create_destroy_destroy_shell_and_tray_denied) {
    NEED_USER();
    HWND list[3] = { U(GetShellWindow)(), U(FindWindowW)(L"Shell_TrayWnd", NULL), U(GetForegroundWindow)() };
    int tested = 0;
    for (int i = 0; i < 3; i++) {
        if (!list[i]) continue;
        DWORD pid = 0;
        U(GetWindowThreadProcessId)(list[i], &pid);
        if (pid == GetCurrentProcessId()) continue;
        SetLastError(0);
        CHECKF(!U(DestroyWindow)(list[i]), "foreign window %d destroyed?!", i);
        EXPECT_ERR(5);
        CHECK(U(IsWindow)(list[i]));
        tested++;
    }
    if (!tested) t_skip("no foreign top-level window to test with");
}

/* ---- accounting ----------------------------------------------------------------------------------------- */
TEST(win_create_destroy_destroy_user_objects_returned) {
    NEED_USER();
    HWND warm = wcd_make_top();
    REQUIRE(warm);
    int base = wcd_user_objects();
    if (base <= 0) { wcd_destroy(warm); t_skip("GetGuiResources unusable"); return; }
    HWND p = wcd_make_top();
    HWND k[6];
    for (int i = 0; i < 6; i++) k[i] = cw(0, WS_CHILD, i < 3 ? p : k[0], (HMENU)(ULONG_PTR)(i + 1));
    CHECK(wcd_user_objects() == base + 7);
    wcd_destroy(k[0]);                    /* k[0] plus k[3..5] below it */
    CHECKF(wcd_user_objects() == base + 3, "after subtree destroy %d", wcd_user_objects());
    wcd_destroy(p);
    CHECKF(wcd_user_objects() == base, "after all %d base %d", wcd_user_objects(), base);
    wcd_destroy(warm);
}

TEST(win_create_destroy_destroy_failed_create_leaks_nothing) {
    NEED_USER();
    HWND warm = wcd_make_top();
    REQUIRE(warm);
    int base = wcd_user_objects();
    if (base <= 0) { wcd_destroy(warm); t_skip("GetGuiResources unusable"); return; }
    for (int i = 0; i < 20; i++) {
        wcd_reset();
        wcd_create_ret = -1;
        HWND h = U(CreateWindowExW)(HIDE_EX, wcd_cls(), L"f", WS_POPUP, 0, 0, 5, 5, NULL, NULL, wcd_hinst(), NULL);
        wcd_create_ret = 0;
        CHECK(h == NULL);
        wcd_ncreate_ret = FALSE;
        h = U(CreateWindowExW)(HIDE_EX, wcd_cls(), L"f", WS_POPUP, 0, 0, 5, 5, NULL, NULL, wcd_hinst(), NULL);
        wcd_ncreate_ret = TRUE;
        CHECK(h == NULL);
        h = U(CreateWindowExW)(0, wcd_cls(), L"f", WS_CHILD, 0, 0, 5, 5, NULL, NULL, wcd_hinst(), NULL);
        CHECK(h == NULL);
    }
    wcd_reset();
    CHECKF(wcd_user_objects() == base, "objects %d base %d", wcd_user_objects(), base);
    wcd_destroy(warm);
}

TEST(win_create_destroy_destroy_bulk_create_destroy_cycles) {
    NEED_USER();
    HWND warm = wcd_make_top();
    REQUIRE(warm);
    int base = wcd_user_objects();
    for (int round = 0; round < 6; round++) {
        HWND h[100];
        for (int i = 0; i < 100; i++) { h[i] = wcd_make_top(); if (!h[i]) { CHECKF(0, "create failed round %d i %d err %lu", round, i, GetLastError()); break; } }
        for (int i = 0; i < 100; i++) if (h[i]) CHECK(U(DestroyWindow)(h[i]));
    }
    if (base > 0) CHECK(wcd_user_objects() == base);
    wcd_destroy(warm);
}

TEST(win_create_destroy_destroy_deep_tree) {
    NEED_USER();
    HWND root = wcd_make_top();
    REQUIRE(root);
    HWND all[1 + 4 + 16];
    int n = 0;
    all[n++] = root;
    HWND lvl1[4], lvl2[16];
    for (int i = 0; i < 4; i++) { lvl1[i] = cw(WS_EX_NOPARENTNOTIFY, WS_CHILD, root, (HMENU)(ULONG_PTR)(i + 1)); all[n++] = lvl1[i]; }
    for (int i = 0; i < 16; i++) { lvl2[i] = cw(WS_EX_NOPARENTNOTIFY, WS_CHILD, lvl1[i % 4], (HMENU)(ULONG_PTR)(i + 1)); all[n++] = lvl2[i]; }
    wcd_reset();
    CHECK(U(DestroyWindow)(root));
    for (int i = 0; i < n; i++) CHECKF(!U(IsWindow)(all[i]), "window %d survived", i);
    CHECK(wcd_count(WM_DESTROY) == 21);
    CHECK(wcd_count(WM_NCDESTROY) == 21);
}

TEST(win_create_destroy_destroy_enumeration_excludes_dead) {
    NEED_USER();
    WCHAR n[96]; wcd_uniq_name(n, 96, "enum");
    REQUIRE(wcd_register(n, NULL, 0, 0, 0));
    HWND h = U(CreateWindowExW)(HIDE_EX, n, L"e", WS_POPUP | WS_VISIBLE, -32000, -32000, 10, 10, NULL, NULL, wcd_hinst(), NULL);
    REQUIRE(h);
    CHECK(U(FindWindowW)(n, NULL) == h);
    wcd_destroy(h);
    CHECK(U(FindWindowW)(n, NULL) == NULL);
    wcd_unregister(n);
}

TEST(win_create_destroy_destroy_then_class_unregisterable) {
    NEED_USER();
    WCHAR n[96]; wcd_uniq_name(n, 96, "unreg");
    REQUIRE(wcd_register(n, NULL, 0, 0, 0));
    HWND a = U(CreateWindowExW)(HIDE_EX, n, L"e", WS_POPUP, -32000, -32000, 10, 10, NULL, NULL, wcd_hinst(), NULL);
    HWND b = U(CreateWindowExW)(HIDE_EX, n, L"e", WS_POPUP, -32000, -32000, 10, 10, NULL, NULL, wcd_hinst(), NULL);
    REQUIRE(a && b);
    wcd_destroy(a);
    SetLastError(0);
    CHECK(!U(UnregisterClassW)(n, wcd_hinst()));   /* one window still holds the class */
    EXPECT_ERR(1412);
    wcd_destroy(b);
    CHECK(U(UnregisterClassW)(n, wcd_hinst()));
}

static DWORD WINAPI gui_conv_proc(LPVOID p) {
    void **out = (void **)p;
    void *before = NULL, *after = NULL;
    k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &before);
    HWND h = wcd_make_top();
    k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &after);
    out[0] = before; out[1] = after; out[2] = (void *)h;
    HWND h2 = wcd_make_top();
    void *after2 = NULL;
    k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &after2);
    out[3] = after2;
    wcd_destroy(h); wcd_destroy(h2);
    return 0;
}
TEST(win_create_destroy_first_window_makes_thread_gui) {
    NEED_USER();
    void *out[4] = { 0 };
    HANDLE th = CreateThread(NULL, 0, gui_conv_proc, out, 0, NULL);
    REQUIRE(th);
    WaitForSingleObject(th, 10000);
    CloseHandle(th);
    t_note("Win32ThreadInfo before first window: %p", out[0]);
    if (!out[2]) { t_skip("no window created"); return; }
    if (t_is_wine()) { t_note("Win32ThreadInfo after: %p", out[1]); return; }
    /* the first win32k call of the thread converts it (PTHREADINFO stored in the TEB) and the
     * pointer then stays the same for every later window */
    CHECK(out[1] != NULL);
    CHECK(out[1] == out[3]);
}
