/* hooks_winevents: SetWindowsHookAW (legacy SetWindowsHook), CallMsgFilter, CBT move/size and create structures,
 * keyboard/mouse hooks fed by posted messages, WinEvent filtering odds and ends. */
#include "hk.h"
#include <limits.h>

UDECL(ULONG_PTR, NtUserSetWindowsHookAW, (int, void *, int));
UDECL(void, NtUserCallMsgFilter, (MSG *, int));

static void gui(void) { U(GetDesktopWindow)(); }
#define NEEDH() do { REQUIRE(U(SetWindowsHookExW)); REQUIRE(U(UnhookWindowsHookEx)); gui(); } while (0)
#define NEEDAW() do { NEEDH(); REQUIRE(UN(NtUserSetWindowsHookAW)); if (t_is_wine()) { t_skip("Wine lacks NtUserSetWindowsHookAW (aborts)"); return; } } while (0)

/* ---- NtUserSetWindowsHookAW(nFilterType, pfn, ansi): installs the hook for the current thread.
 * failure is reported as (ULONG_PTR)-1, success as the handle only when the new hook has a successor in the chain
 * (phkNext != 0) or the attribute table bit 8 is set, otherwise 0. ---- */
TEST(hooks_winevents_hookaw_invalid_ids_return_minus_one) {
    NEEDAW();
    static const int ids[] = { -2, 15, 16, 99, INT_MIN };
    for (unsigned i = 0; i < sizeof ids / sizeof *ids; i++) {
        SetLastError(0xDEAD0001);
        ULONG_PTR r = UN(NtUserSetWindowsHookAW)(ids[i], hk_nop_proc, 0);
        CHECKF(r == (ULONG_PTR)-1, "id %d -> %llx", ids[i], (unsigned long long)r);
        EXPECT_ERR(HKE_INVALID_HOOK_FILTER);   /* 0xf < id + 1U */
    }
}
TEST(hooks_winevents_hookaw_null_proc_returns_minus_one) {
    NEEDAW();
    for (int id = -1; id <= 14; id++) {
        SetLastError(0xDEAD0001);
        ULONG_PTR r = UN(NtUserSetWindowsHookAW)(id, NULL, 0);
        CHECKF(r == (ULONG_PTR)-1, "id %d -> %llx", id, (unsigned long long)r);
        EXPECT_ERR(HKE_INVALID_FILTER_PROC);
    }
}
TEST(hooks_winevents_hookaw_global_only_ids_rejected) {
    NEEDAW();
    for (unsigned i = 0; i < sizeof hk_global_only_ids / sizeof *hk_global_only_ids; i++) {
        int id = hk_global_only_ids[i];
        SetLastError(0xDEAD0001);
        ULONG_PTR r = UN(NtUserSetWindowsHookAW)(id, hk_nop_proc, 0);
        CHECKF(r == (ULONG_PTR)-1, "id %d -> %llx", id, (unsigned long long)r);
        if (r != (ULONG_PTR)-1 && r) UN(NtUserUnhookWindowsHookEx)((HHOOK)r);
        EXPECT_ERR(HKE_GLOBAL_ONLY_HOOK);   /* the hook is always bound to the calling thread */
    }
}
/* installs two legacy hooks in a throw-away thread: its exit removes them */
static void legacy_two(hk_worker *w) {
    w->out[0] = UN(NtUserSetWindowsHookAW)(WH_GETMESSAGE, hk_nop_proc, 0);
    w->out[1] = UN(NtUserSetWindowsHookAW)(WH_GETMESSAGE, hk_nop_proc, 1);
    w->out[2] = GetLastError();
    w->out[3] = (ULONG_PTR)hk_user_obj_valid((HANDLE)w->out[1], 5);
}
TEST(hooks_winevents_hookaw_second_hook_returns_its_handle) {
    NEEDAW();
    hk_worker w;
    REQUIRE(hk_worker_start(&w, legacy_two, NULL));
    CHECK(w.out[0] != (ULONG_PTR)-1);
    CHECK(w.out[1] != (ULONG_PTR)-1);
 /* the second hook has the first as successor: the first hook's handle is returned */
    CHECKF(w.out[1] != 0, "second legacy hook returned 0");
    if (w.out[1] != 0 && w.out[1] != (ULONG_PTR)-1) CHECK(w.out[3]);
    CHECK(w.out[1] != w.out[0] || w.out[1] == 0);
    hk_worker_stop(&w);
}
static void legacy_user32(hk_worker *w) {
    typedef HHOOK (WINAPI *fn_t)(int, HOOKPROC);
    fn_t sw = (fn_t)GetProcAddress(GetModuleHandleA("user32.dll"), "SetWindowsHookW");
    if (!sw) { w->out[3] = 2; return; }
    w->out[0] = (ULONG_PTR)sw(WH_CBT, hk_nop_proc);
    w->out[1] = (ULONG_PTR)sw(WH_CBT, hk_nop_proc);
    w->out[3] = 1;
}
TEST(hooks_winevents_hookaw_user32_setwindowshookw_routes_to_syscall) {
    NEEDAW();
    hk_worker w;
    REQUIRE(hk_worker_start(&w, legacy_user32, NULL));
    if (w.out[3] == 2) { hk_worker_stop(&w); t_skip("SetWindowsHookW not exported"); return; }
    CHECK(w.out[0] != (ULONG_PTR)-1 && w.out[1] != (ULONG_PTR)-1);
    CHECK(w.out[1] != 0);
    hk_worker_stop(&w);
}

/* ---- CallMsgFilter ---- */
TEST(hooks_winevents_msgfilter_skip_messages) {
    NEEDH(); REQUIRE(U(CallMsgFilterW));
    hk_reset();
    hk_install(0, WH_MSGFILTER, HK_K_MSG, FALSE);
    /* NtUserCallMsgFilter / CallMsgFilterW skip 0x240 and (msg - 0x119) & ~2 == 0, i.e. 0x119 and 0x11B */
    static const struct { UINT msg; int called; } c[] = {
        { 0x240, 0 }, { 0x119, 0 }, { 0x11B, 0 }, { 0x11A, 1 }, { 0x11C, 1 }, { 0x118, 1 }, { WM_APP + 1, 1 }, { WM_KEYDOWN, 1 },
    };
    for (unsigned i = 0; i < sizeof c / sizeof *c; i++) {
        LONG before = hk_s[0].total;
        MSG m; memset(&m, 0, sizeof m); m.message = c[i].msg;
        U(CallMsgFilterW)(&m, MSGF_DIALOGBOX);
        CHECKF((hk_s[0].total - before) == c[i].called, "msg 0x%x: hook calls %ld", c[i].msg, (long)(hk_s[0].total - before));
    }
    hk_remove_all();
}
TEST(hooks_winevents_msgfilter_code_passed_through) {
    NEEDH(); REQUIRE(U(CallMsgFilterW));
    hk_reset();
    hk_install(0, WH_MSGFILTER, HK_K_MSG, FALSE);
    static const int codes[] = { MSGF_DIALOGBOX, 1, MSGF_MENU, 3, 4, MSGF_SCROLLBAR, 100, 0x7fff };
    for (unsigned i = 0; i < sizeof codes / sizeof *codes; i++) {
        MSG m; memset(&m, 0, sizeof m); m.message = WM_APP + 9; m.wParam = i; m.lParam = 0x55;
        hk_s[0].code = -99;
        U(CallMsgFilterW)(&m, codes[i]);
        CHECKF(hk_s[0].code == codes[i], "code %d seen as %d", codes[i], hk_s[0].code);
        EXPECT_EQ(hk_s[0].msg.wParam, i);
    }
    hk_remove_all();
}
TEST(hooks_winevents_msgfilter_nt_bad_message_bits) {
    NEEDH(); REQUIRE(UN(NtUserCallMsgFilter));
    hk_reset();
    hk_install(0, WH_MSGFILTER, HK_K_MSG, FALSE);
    static const UINT bad[] = { 0x20000, 0x40000, 0x80000000u, 0xFFFFFFFFu, 0xFFFE0000u };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        MSG m; memset(&m, 0, sizeof m); m.message = bad[i];
        SetLastError(0xDEAD0001);
        UN(NtUserCallMsgFilter)(&m, 4);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);   /* (message & 0xfffe0000) != 0 */
        EXPECT_EQ(m.message, bad[i]);
    }
    EXPECT_EQ(hk_s[0].total, 0);
    MSG m; memset(&m, 0, sizeof m); m.message = 0x1FFFF;   /* highest accepted value */
    UN(NtUserCallMsgFilter)(&m, 4);
    EXPECT_EQ(hk_s[0].total, 1);
    hk_remove_all();
}

/* ---- CBT ---- */
static void grab_rect(hk_slot *s, int c, WPARAM w, LPARAM l) {
    RECT r; memset(&r, 0, sizeof r);
    if (c == HCBT_MOVESIZE && k_try_read((void *)l, &r, sizeof r)) {
        s->user[0] = (ULONG_PTR)(LONG_PTR)r.left; s->user[1] = (ULONG_PTR)(LONG_PTR)r.top;
        s->user[2] = (ULONG_PTR)(LONG_PTR)r.right; s->user[3] = (ULONG_PTR)(LONG_PTR)r.bottom;
    }
}
TEST(hooks_winevents_hk_cbt_movesize_not_sent_by_setwindowpos) {
    /* HCBT_MOVESIZE belongs to the interactive move/size loop (a mouse or keyboard drag); a programmatic SetWindowPos never
     * produces it, so a hook that vetoes it cannot stop the move either. */
    NEEDH(); REQUIRE(U(SetWindowPos)); REQUIRE(U(GetWindowRect));
    hk_reset();
    HWND w = hk_window_ex("mv", WS_POPUP, -32000, -32000, 10, 10);
    REQUIRE(w);
    hk_s[0].only_code = HCBT_MOVESIZE; hk_s[0].extra = grab_rect;
    hk_install(0, WH_CBT, HK_K_RAW, FALSE);
    hk_s[0].ret = 1;     /* a veto, if the code were ever delivered */
    CHECK(U(SetWindowPos)(w, NULL, -31000, -30000, 20, 30, SWP_NOZORDER | SWP_NOACTIVATE));
    EXPECT_EQ(hk_s[0].calls, 0);
    RECT r; U(GetWindowRect)(w, &r);
    EXPECT_EQ(r.left, -31000); EXPECT_EQ(r.top, -30000);
    EXPECT_EQ(r.right - r.left, 20); EXPECT_EQ(r.bottom - r.top, 30);
    U(SetWindowPos)(w, NULL, -20000, -20000, 40, 40, SWP_NOZORDER | SWP_NOACTIVATE);
    EXPECT_EQ(hk_s[0].calls, 0);
    U(GetWindowRect)(w, &r);
    EXPECT_EQ(r.left, -20000); EXPECT_EQ(r.top, -20000);
    EXPECT_EQ(r.right - r.left, 40);
    hk_remove_all();
    U(DestroyWindow)(w);
}
static void grab_create(hk_slot *s, int c, WPARAM w, LPARAM l) {
    if (c != HCBT_CREATEWND) return;
    CBT_CREATEWNDW cw; CREATESTRUCTW cs;
    memset(&cw, 0, sizeof cw);
    if (!k_try_read((void *)l, &cw, sizeof cw) || !cw.lpcs) return;
    if (!k_try_read(cw.lpcs, &cs, sizeof cs)) return;
    s->user[0] = 1;
    s->user[1] = (ULONG_PTR)cs.style; s->user[2] = (ULONG_PTR)cs.dwExStyle;
    s->user[3] = (ULONG_PTR)(cs.x == -32000 && cs.y == -32000 && cs.cx == 1 && cs.cy == 1 && cs.hInstance == GetModuleHandleW(NULL) && cs.hwndParent == NULL);
}
TEST(hooks_winevents_hk_cbt_createwnd_createstruct) {
    NEEDH();
    hk_reset();
    hk_s[0].only_code = HCBT_CREATEWND; hk_s[0].extra = grab_create;
    hk_install(0, WH_CBT, HK_K_RAW, FALSE);
    HWND w = hk_window("cs");
    REQUIRE(w);
    EXPECT_EQ(hk_s[0].calls, 1);
    CHECK(hk_s[0].user[0] == 1);
    if (hk_s[0].user[0]) {
        CHECK(hk_s[0].user[1] & WS_POPUP);
        CHECK((hk_s[0].user[2] & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)) == (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE));
        CHECK(hk_s[0].user[3]);   /* x/y/cx/cy/hInstance/parent as passed to CreateWindowEx */
    }
    hk_remove_all();
    U(DestroyWindow)(w);
}

/* ---- keyboard / mouse hooks, fed by posted messages ---- */
TEST(hooks_winevents_hk_keyboard_posted_message_codes) {
    NEEDH();
    hk_reset();
    HWND w = hk_window("kb");
    REQUIRE(w);
    hk_install(0, WH_KEYBOARD, HK_K_RAW, FALSE);
    U(PostMessageW)(w, WM_KEYDOWN, VK_F24, 0x00010001);
    MSG m;
    CHECK(U(PeekMessageW)(&m, w, WM_KEYDOWN, WM_KEYDOWN, PM_NOREMOVE));
    LONG n1 = hk_s[0].total; int code1 = hk_s[0].code; WPARAM wp1 = hk_s[0].wp;
    CHECK(U(PeekMessageW)(&m, w, WM_KEYDOWN, WM_KEYDOWN, PM_REMOVE));
    t_note("keyboard hook: calls after PM_NOREMOVE=%ld (code %d), total=%ld", (long)n1, code1, (long)hk_s[0].total);
    if (n1) { EXPECT_EQ(code1, HC_NOREMOVE); EXPECT_EQ(wp1, VK_F24); }
    if (hk_s[0].total > n1) { EXPECT_EQ(hk_s[0].code, HC_ACTION); EXPECT_EQ(hk_s[0].wp, VK_F24); EXPECT_EQ(hk_s[0].lp, 0x00010001); }
    hk_remove_all();
    U(DestroyWindow)(w);
}
TEST(hooks_winevents_hk_mouse_posted_message_codes) {
    NEEDH();
    hk_reset();
    HWND w = hk_window("ms");
    REQUIRE(w);
    hk_install(0, WH_MOUSE, HK_K_RAW, FALSE);
    U(PostMessageW)(w, WM_MOUSEMOVE, 0, MAKELPARAM(3, 4));
    MSG m;
    CHECK(U(PeekMessageW)(&m, w, WM_MOUSEMOVE, WM_MOUSEMOVE, PM_REMOVE));
    t_note("mouse hook: total=%ld code=%d wp=0x%llx", (long)hk_s[0].total, hk_s[0].code, (unsigned long long)hk_s[0].wp);
    if (hk_s[0].total) { CHECK(hk_s[0].code == HC_ACTION || hk_s[0].code == HC_NOREMOVE); EXPECT_EQ(hk_s[0].wp, WM_MOUSEMOVE); }
    hk_remove_all();
    U(DestroyWindow)(w);
}

/* ---- hook handles ---- */
TEST(hooks_winevents_unhook_wildcard_uniqueness_word) {
    NEEDH(); REQUIRE(UN(NtUserUnhookWindowsHookEx));
    HHOOK h = U(SetWindowsHookExW)(WH_GETMESSAGE, hk_nop_proc, NULL, GetCurrentThreadId());
    REQUIRE(h);
    /* HMValidateHandle accepts a uniqueness word of 0xFFFF as "any": index with 0xFFFF in the high word is the same object */
    HHOOK wild = (HHOOK)(((ULONG_PTR)h & 0xFFFF) | 0xFFFF0000u);
    int r = UN(NtUserUnhookWindowsHookEx)(wild);
    t_note("unhook via wildcard handle 0x%llx -> %d", (unsigned long long)(ULONG_PTR)wild, r);
    if (r) { CHECK(hk_user_obj_dead(h)); }
    else { CHECK(hk_user_obj_valid(h, 5)); CHECK(U(UnhookWindowsHookEx)(h)); }
}
/* ---- WinEvent odds and ends ---- */
TEST(hooks_winevents_we_iswineventhookinstalled_tracks_category) {
    REQUIRE(UN(NtUserSetWinEventHook)); REQUIRE(UN(NtUserUnhookWinEvent)); REQUIRE(UN(IsWinEventHookInstalled));
    gui();
    /* EVENT_SYSTEM_MENUSTART (4) category 1; check another category (EVENT_OBJECT_LOCATIONCHANGE 0x800B -> 0x40) */
    DWORD ev = EVENT_OBJECT_LOCATIONCHANGE;
    BOOL pre = UN(IsWinEventHookInstalled)(ev);
    HWINEVENTHOOK h = (HWINEVENTHOOK)UN(NtUserSetWinEventHook)(ev, ev, NULL, NULL, we_proc, 0, 0, 0);
    REQUIRE(h);
    CHECK(UN(IsWinEventHookInstalled)(ev));
    CHECK(UN(NtUserUnhookWinEvent)(h) == 1);
    BOOL post = UN(IsWinEventHookInstalled)(ev);
    if (!pre) CHECK(!post);   /* category recomputed from the remaining hooks */
    else t_note("other WinEvent hooks cover this category (pre=%d post=%d)", pre, post);
}
TEST(hooks_winevents_we_user32_notify_filters_high_events) {
    REQUIRE(UN(NtUserSetWinEventHook)); REQUIRE(UN(NtUserUnhookWinEvent)); REQUIRE(U(NotifyWinEvent));
    gui();
    we_reset();
    HWND w = hk_window("high");
    REQUIRE(w);
    /* user32!NotifyWinEvent returns without a syscall for event >= 0x80000000 */
    HWINEVENTHOOK h = (HWINEVENTHOOK)UN(NtUserSetWinEventHook)(0x80000000u, 0xFFFFFFFFu, NULL, NULL, we_proc, 0, 0, 0);
    REQUIRE(h);
    U(NotifyWinEvent)(0x80000000u, w, 0, 0);
    U(NotifyWinEvent)(0xFFFFFFFFu, w, 0, 0);
    k_pump(60);
    EXPECT_EQ(we_count_event(0x80000000u), 0);
    EXPECT_EQ(we_count_event(0xFFFFFFFFu), 0);
    UN(NtUserUnhookWinEvent)(h);
    U(DestroyWindow)(w);
}
TEST(hooks_winevents_we_nt_notify_does_not_validate_event) {
    REQUIRE(UN(NtUserNotifyWinEvent)); gui();
    HWND w = hk_window("ne");
    REQUIRE(w);
    static const DWORD ev[] = { 0, 1, 0xFFFFFFFFu, 0x7FFFFFFFu };
    for (unsigned i = 0; i < sizeof ev / sizeof *ev; i++)
        CHECKF((UN(NtUserNotifyWinEvent)(ev[i], w, INT_MIN, INT_MAX) & 0xff) == 1, "event 0x%lx", (unsigned long)ev[i]);   /* only the HWND is validated */
    k_pump(30);
    U(DestroyWindow)(w);
}
static void idthread_init(hk_worker *w) { (void)w; }
TEST(hooks_winevents_we_idthread_other_thread_filter) {
    REQUIRE(UN(NtUserSetWinEventHook)); REQUIRE(UN(NtUserUnhookWinEvent)); REQUIRE(UN(NtUserNotifyWinEvent)); gui();
    hk_worker wk;
    REQUIRE(hk_worker_start(&wk, idthread_init, NULL));
    HWND mine = hk_window("it");
    REQUIRE(mine);
    we_reset();
    HWINEVENTHOOK h = (HWINEVENTHOOK)UN(NtUserSetWinEventHook)(0xA000, 0xA0FF, NULL, NULL, we_proc, 0, wk.tid, 0);
    CHECK(h != NULL);
    if (h) {
        UN(NtUserNotifyWinEvent)(0xA00B, mine, 0, 0);
        UN(NtUserNotifyWinEvent)(0xA00C, wk.hwnd, 0, 0);
        k_pump(100);
        EXPECT_EQ(we_count_event(0xA00B), 0);   /* event thread != idThread */
        EXPECT_EQ(we_count_event(0xA00C), 1);
        struct we_rec *r = we_find(0xA00C, 0);
        if (r) { EXPECT_EQ(r->tid, wk.tid); EXPECT_EQ(r->cbtid, GetCurrentThreadId()); }
        UN(NtUserUnhookWinEvent)(h);
    }
    U(DestroyWindow)(mine);
    hk_worker_stop(&wk);
}
