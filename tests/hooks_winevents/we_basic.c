/* hooks_winevents: WinEvent validation and out-of-context delivery, plus hook dispatch basics.
 * Kernel order in NtUserSetWinEventHook/_SetWinEventHook: flags & 0xfffffff0 -> 1004; pfn NULL -> 1427;
 * min > max -> 1426; WINEVENT_INCONTEXT without hmod -> 1428; idThread not a GUI thread -> 1444.
 * _UnhookWinEvent only succeeds for the thread that installed the hook (else ERROR_INVALID_HANDLE, 6).
 * NtUserNotifyWinEvent returns whether the HWND validated. */
#include "hk.h"

#define NEEDW() do { REQUIRE(UN(NtUserSetWinEventHook)); REQUIRE(UN(NtUserUnhookWinEvent)); REQUIRE(UN(NtUserNotifyWinEvent)); REQUIRE(U(GetDesktopWindow)); U(GetDesktopWindow)(); } while (0)

static HWINEVENTHOOK sw(DWORD mn, DWORD mx, HMODULE m, void *pfn, DWORD pid, DWORD tid, DWORD fl) {
    SetLastError(0xDEAD0001);
    return (HWINEVENTHOOK)UN(NtUserSetWinEventHook)(mn, mx, m, NULL, pfn, pid, tid, fl);
}

TEST(hooks_winevents_we_invalid_flags) {
    NEEDW();
    static const DWORD fl[] = { 0x10, 0x20, 0x80, 0x100, 0xFFFFFFF0u, 0x80000000u };
    for (unsigned i = 0; i < sizeof fl / sizeof *fl; i++) {
        HWINEVENTHOOK h = sw(0xA000, 0xA0FF, NULL, we_proc, 0, 0, fl[i]);
        CHECKF(h == NULL, "flags 0x%lx", (unsigned long)fl[i]);
        if (h) UN(NtUserUnhookWinEvent)(h);
        EXPECT_ERR(HKE_INVALID_FLAGS);
    }
}
TEST(hooks_winevents_we_flags_checked_before_proc) {
    NEEDW();
    HWINEVENTHOOK h = sw(0xA000, 0xA0FF, NULL, NULL, 0, 0, 0x10);
    CHECK(h == NULL); EXPECT_ERR(HKE_INVALID_FLAGS);
}
TEST(hooks_winevents_we_null_proc) {
    NEEDW();
    HWINEVENTHOOK h = sw(0xA000, 0xA0FF, NULL, NULL, 0, 0, 0);
    CHECK(h == NULL); EXPECT_ERR(HKE_INVALID_FILTER_PROC);
    h = sw(0xA100, 0xA000, NULL, NULL, 0, 0, 0); /* proc is checked before the range */
    CHECK(h == NULL); EXPECT_ERR(HKE_INVALID_FILTER_PROC);
}
TEST(hooks_winevents_we_inverted_range) {
    NEEDW();
    HWINEVENTHOOK h = sw(0xA100, 0xA000, NULL, we_proc, 0, 0, 0);
    CHECK(h == NULL); EXPECT_ERR(HKE_INVALID_HOOK_FILTER);
    h = sw(0xFFFFFFFFu, 0, NULL, we_proc, 0, 0, 0);
    CHECK(h == NULL); EXPECT_ERR(HKE_INVALID_HOOK_FILTER);
}
TEST(hooks_winevents_we_incontext_needs_module) {
    NEEDW();
    HWINEVENTHOOK h = sw(0xA000, 0xA0FF, NULL, we_proc, 0, 0, WINEVENT_INCONTEXT);
    CHECK(h == NULL); EXPECT_ERR(HKE_HOOK_NEEDS_HMOD);
    h = sw(0xA000, 0xA0FF, NULL, NULL, 0, 0, WINEVENT_INCONTEXT); /* proc first */
    CHECK(h == NULL); EXPECT_ERR(HKE_INVALID_FILTER_PROC);
}
TEST(hooks_winevents_we_incontext_null_library_name) {
    NEEDW();
    HMODULE m = GetModuleHandleA("kernel32.dll");
    HWINEVENTHOOK h = sw(0xA000, 0xA0FF, m, we_proc, 0, 0, WINEVENT_INCONTEXT);
    CHECK(h == NULL);
    if (h) UN(NtUserUnhookWinEvent)(h);
    EXPECT_ERR(HKE_DLL_NOT_FOUND);
}
TEST(hooks_winevents_we_nonexistent_thread) {
    NEEDW();
    static const DWORD t[] = { 0xFFFFFFF0u, 0x7FFFFFF0u, 0x00FFFFFCu };
    for (unsigned i = 0; i < 3; i++) {
        HWINEVENTHOOK h = sw(0xA000, 0xA0FF, NULL, we_proc, 0, t[i], 0);
        CHECKF(h == NULL, "tid 0x%lx", (unsigned long)t[i]);
        if (h) UN(NtUserUnhookWinEvent)(h);
        EXPECT_ERR(1444);
    }
}
TEST(hooks_winevents_we_ranges_accepted_and_handle_type) {
    NEEDW();
    static const DWORD r[][2] = { {1,1}, {0x8000,0x8000}, {0xA000,0xAFFF}, {5,5}, {0x8005,0x800C} };
    for (unsigned i = 0; i < 5; i++) {
        HWINEVENTHOOK h = sw(r[i][0], r[i][1], NULL, we_proc, 0, 0, 0);
        CHECKF(h != NULL, "range %u", i);
        if (!h) continue;
        CHECK(hk_user_obj_valid(h, 15));
        EXPECT_EQ(UN(NtUserUnhookWinEvent)(h), 1);
        SetLastError(0xDEAD0001);
        EXPECT_EQ(UN(NtUserUnhookWinEvent)(h), 0);
        EXPECT_ERR(ERROR_INVALID_HANDLE);
    }
}
TEST(hooks_winevents_we_idprocess_not_validated) {
    NEEDW();
    HWINEVENTHOOK h = sw(0xA000, 0xA0FF, NULL, we_proc, 0xFFFFFFF0u, 0, 0);
    CHECK(h != NULL);
    if (h) CHECK(UN(NtUserUnhookWinEvent)(h) == 1);
}
TEST(hooks_winevents_we_unhook_invalid_handles) {
    NEEDW();
    static const ULONG_PTR b[] = { 0, 1, 0xFFFF, 0x10000, 0x12345678u };
    for (unsigned i = 0; i < 5; i++) {
        SetLastError(0xDEAD0001);
        CHECKF(UN(NtUserUnhookWinEvent)((HWINEVENTHOOK)b[i]) == 0, "0x%llx", (unsigned long long)b[i]);
        EXPECT_ERR(ERROR_INVALID_HANDLE);
    }
}
static void unhook_other(hk_worker *w) {
    w->out[0] = (ULONG_PTR)UN(NtUserUnhookWinEvent)((HWINEVENTHOOK)w->arg);
    w->out[1] = GetLastError();
}
TEST(hooks_winevents_we_unhook_from_other_thread_fails) {
    NEEDW();
    HWINEVENTHOOK h = sw(0xA000, 0xA0FF, NULL, we_proc, 0, 0, 0);
    REQUIRE(h);
    hk_worker w;
    if (hk_worker_start(&w, unhook_other, (void *)h)) {
        /* _UnhookWinEvent: hook->pti must equal the calling thread's pti, else UserSetLastError(6) */
        EXPECT_EQ(w.out[0] & 0xffffffff, 0);
        EXPECT_EQ(w.out[1], ERROR_INVALID_HANDLE);
        hk_worker_stop(&w);
    }
    CHECK(hk_user_obj_valid(h, 15));
    CHECK(UN(NtUserUnhookWinEvent)(h) == 1);
}
TEST(hooks_winevents_notify_hwnd_validation) {
    NEEDW();
    HWND w = hk_window("notify");
    REQUIRE(w);
    EXPECT_EQ(UN(NtUserNotifyWinEvent)(0xA000, w, 0, 0) & 0xff, 1);
    EXPECT_EQ(UN(NtUserNotifyWinEvent)(0xA000, NULL, 0, 0) & 0xff, 0);
    EXPECT_EQ(UN(NtUserNotifyWinEvent)(0xA000, (HWND)(ULONG_PTR)0x12345678, 0, 0) & 0xff, 0);
    EXPECT_EQ(UN(NtUserNotifyWinEvent)(0xA000, U(GetDesktopWindow)(), 0, 0) & 0xff, 1);
    U(DestroyWindow)(w);
    EXPECT_EQ(UN(NtUserNotifyWinEvent)(0xA000, w, 0, 0) & 0xff, 0);
}

/* ---- WinEvent category mask on the SERVERINFO page (shared server-info page) ---- */
static const struct { DWORD ev; unsigned long bit; } cat[] = {
    { 0x0003, 0x8000 }, { 0x0004, 1 }, { 0x0007, 1 }, { 0x4001, 2 }, { 0x4007, 2 }, { 0x8000, 0x8000 },
    { 0x8005, 4 }, { 0x800A, 0x20 }, { 0x800B, 0x40 }, { 0x800C, 8 }, { 0x800E, 0x10 }, { 0xA000, 0x8000 },
};
TEST(hooks_winevents_we_category_mask_per_event) {
    NEEDW();
    struct k_user_shared_info si;
    if (!k_shared_info(&si) || !si.psi) { t_skip("no SERVERINFO"); return; }
    for (unsigned i = 0; i < sizeof cat / sizeof *cat; i++) {
        unsigned long before = 0, during = 0, after = 0;
        k_try_read_u32((char *)si.psi + 0x764, &before);
        HWINEVENTHOOK h = sw(cat[i].ev, cat[i].ev, NULL, we_proc, 0, 0, 0);
        REQUIRE(h);
        k_try_read_u32((char *)si.psi + 0x764, &during);
        CHECKF((during & cat[i].bit) == cat[i].bit, "event 0x%lx mask 0x%lx", (unsigned long)cat[i].ev, during);
        CHECKF((during & ~before & ~cat[i].bit) == 0, "event 0x%lx added unexpected bits 0x%lx", (unsigned long)cat[i].ev, during & ~before);
        UN(NtUserUnhookWinEvent)(h);
        k_try_read_u32((char *)si.psi + 0x764, &after);
        CHECKF((after & cat[i].bit & ~before) == 0, "event 0x%lx bit not recomputed away (after 0x%lx)", (unsigned long)cat[i].ev, after);
    }
}

/* ---- delivery ---- */
TEST(hooks_winevents_we_notify_delivers_exact_args_after_pump) {
    NEEDW();
    we_reset();
    HWND w = hk_window("deliver");
    REQUIRE(w);
    HWINEVENTHOOK h = sw(0xA000, 0xA0FF, NULL, we_proc, 0, 0, 0);
    REQUIRE(h);
    DWORD t0 = GetTickCount();
    U(NotifyWinEvent)(0xA001, w, 7, 9);
    k_pump(60);
    struct we_rec *r = we_find(0xA001, 0);
    CHECK(r != NULL);
    if (r) {
        CHECK(r->h == h); CHECK(r->hwnd == w); EXPECT_EQ(r->obj, 7); EXPECT_EQ(r->child, 9);
        EXPECT_EQ(r->tid, GetCurrentThreadId());   /* event thread = owner of the window */
        EXPECT_EQ(r->cbtid, GetCurrentThreadId()); /* callback runs in the hooking thread */
        CHECK((DWORD)(r->time - t0) < 5000);
    }
    EXPECT_EQ(we_count_event(0xA001), 1);
    UN(NtUserUnhookWinEvent)(h);
    U(DestroyWindow)(w);
}
TEST(hooks_winevents_we_range_filter) {
    NEEDW();
    we_reset();
    HWND w = hk_window("range");
    REQUIRE(w);
    HWINEVENTHOOK h = sw(0xA010, 0xA020, NULL, we_proc, 0, 0, 0);
    REQUIRE(h);
    static const DWORD ev[] = { 0xA00F, 0xA010, 0xA015, 0xA020, 0xA021 };
    for (int i = 0; i < 5; i++) U(NotifyWinEvent)(ev[i], w, 0, 0);
    k_pump(60);
    EXPECT_EQ(we_count_event(0xA00F), 0); EXPECT_EQ(we_count_event(0xA010), 1);
    EXPECT_EQ(we_count_event(0xA015), 1); EXPECT_EQ(we_count_event(0xA020), 1);
    EXPECT_EQ(we_count_event(0xA021), 0);
    UN(NtUserUnhookWinEvent)(h);
    U(DestroyWindow)(w);
}
TEST(hooks_winevents_we_unhook_stops_delivery) {
    NEEDW();
    we_reset();
    HWND w = hk_window("stop");
    REQUIRE(w);
    HWINEVENTHOOK h = sw(0xA000, 0xA0FF, NULL, we_proc, 0, 0, 0);
    REQUIRE(h);
    UN(NtUserUnhookWinEvent)(h);
    U(NotifyWinEvent)(0xA002, w, 0, 0);
    k_pump(60);
    EXPECT_EQ(we_count_event(0xA002), 0);
    U(DestroyWindow)(w);
}
TEST(hooks_winevents_we_skip_own_thread_and_process) {
    NEEDW();
    HWND w = hk_window("skip");
    REQUIRE(w);
    DWORD fl[3] = { WINEVENT_SKIPOWNTHREAD, WINEVENT_SKIPOWNPROCESS, 0 };
    int want[3] = { 0, 0, 1 };
    for (int i = 0; i < 3; i++) {
        we_reset();
        HWINEVENTHOOK h = sw(0xA000, 0xA0FF, NULL, we_proc, 0, 0, fl[i]);
        REQUIRE(h);
        U(NotifyWinEvent)(0xA003, w, 0, 0);
        k_pump(60);
        EXPECT_EQ(we_count_event(0xA003), want[i]);
        UN(NtUserUnhookWinEvent)(h);
    }
    U(DestroyWindow)(w);
}
TEST(hooks_winevents_we_idprocess_idthread_filters) {
    NEEDW();
    HWND w = hk_window("filt");
    REQUIRE(w);
    DWORD me = GetCurrentProcessId(), tid = GetCurrentThreadId();
    struct { DWORD pid, tid; int want; } c[] = { {me,0,1}, {0xFFFFFFF0u,0,0}, {0,tid,1}, {0,tid+0x10000,0}, {me,tid,1} };
    for (unsigned i = 0; i < 5; i++) {
        we_reset();
        HWINEVENTHOOK h = sw(0xA000, 0xA0FF, NULL, we_proc, c[i].pid, c[i].tid, 0);
        if (!h) { t_note("case %u: hook refused (err %lu)", i, GetLastError()); continue; }
        U(NotifyWinEvent)(0xA004, w, 0, 0);
        k_pump(60);
        CHECKF(we_count_event(0xA004) == c[i].want, "case %u got %d want %d", i, we_count_event(0xA004), c[i].want);
        UN(NtUserUnhookWinEvent)(h);
    }
    U(DestroyWindow)(w);
}
TEST(hooks_winevents_we_window_lifecycle_events) {
    NEEDW();
    we_reset();
    HWINEVENTHOOK h = sw(EVENT_OBJECT_CREATE, EVENT_OBJECT_NAMECHANGE, NULL, we_proc, 0, 0, 0);
    REQUIRE(h);
    HWND w = hk_window("life");
    REQUIRE(w);
    U(SetWindowTextW)(w, L"renamed");
    U(DestroyWindow)(w);
    k_pump(80);
    struct we_rec *c = we_find(EVENT_OBJECT_CREATE, 0);
    CHECK(c && c->hwnd == w && c->obj == OBJID_WINDOW);
    struct we_rec *n = we_find(EVENT_OBJECT_NAMECHANGE, 0);
    CHECK(n && n->hwnd == w && n->obj == OBJID_WINDOW);
    struct we_rec *d = we_find(EVENT_OBJECT_DESTROY, 0);
    CHECK(d && d->hwnd == w);
    UN(NtUserUnhookWinEvent)(h);
}
TEST(hooks_winevents_we_kcb_slot_counts_deliveries) {
    NEEDW();
    if (!hk_win10_2004_family()) { t_skip("callback slot indices are for the 19041..19045 builds"); return; }
    HWND w = hk_window("kcb");
    REQUIRE(w);
    U(NotifyWinEvent)(0xA000, w, 0, 0);
    k_pump(10);
    if (!k_cb_hook(HK_CB_WINEVENT)) { U(DestroyWindow)(w); t_skip("cannot hook slot"); return; }
    we_reset();
    HWINEVENTHOOK h = sw(0xA000, 0xA0FF, NULL, we_proc, 0, 0, 0);
    REQUIRE(h);
    for (int i = 0; i < 3; i++) U(NotifyWinEvent)(0xA000 + i, w, 0, 0);
    k_pump(80);
    EXPECT_EQ(we_n, 3);
    EXPECT_EQ(k_cb_hit_count(HK_CB_WINEVENT), 3); /* one user-mode callback (0x58) per event */
    k_cb_unhook_all();
    UN(NtUserUnhookWinEvent)(h);
    U(DestroyWindow)(w);
}

/* ---- hook dispatch basics ---- */
TEST(hooks_winevents_hk_state_fshooks_bit) {
    REQUIRE(U(SetWindowsHookExW)); U(GetDesktopWindow)();
    for (unsigned i = 0; i < 11; i++) {
        int id = hk_thread_ids[i];
        HHOOK h = U(SetWindowsHookExW)(id, hk_nop_proc, NULL, GetCurrentThreadId());
        REQUIRE(h);
        CHECKF(hk_ci_fshooks() & HK_BIT(id), "id %d fsHooks 0x%lx", id, hk_ci_fshooks());
        U(UnhookWindowsHookEx)(h);
        CHECKF(!(hk_ci_fshooks() & HK_BIT(id)), "id %d fsHooks 0x%lx after unhook", id, hk_ci_fshooks());
    }
}
TEST(hooks_winevents_hk_getmessage_sees_posted_message_and_may_modify) {
    REQUIRE(U(SetWindowsHookExW)); U(GetDesktopWindow)();
    hk_reset();
    HWND w = hk_window("gm");
    REQUIRE(w);
    hk_s[0].only_msg = WM_APP + 1;
    hk_install(0, WH_GETMESSAGE, HK_K_MSG, FALSE);
    REQUIRE(hk_s[0].hh);
    U(PostMessageW)(w, WM_APP + 1, 11, 22);
    MSG m; memset(&m, 0, sizeof m);
    CHECK(U(PeekMessageW)(&m, w, WM_APP + 1, WM_APP + 1, PM_REMOVE));
    CHECK(hk_s[0].calls >= 1);
    EXPECT_EQ(hk_s[0].code, HC_ACTION);
    CHECK(hk_s[0].wp & PM_REMOVE);
    EXPECT_EQ(hk_s[0].msg.wParam, 11); EXPECT_EQ(hk_s[0].msg.lParam, 22); CHECK(hk_s[0].msg.hwnd == w);
    EXPECT_EQ(hk_s[0].ci_hookcur >> 16, WH_GETMESSAGE);   /* DispatchHookW: CI+0x58 = id << 16 */
    EXPECT_EQ(hk_ci_hookcur(), 0);                          /* restored after the call */
    EXPECT_EQ(hk_ci_phkcurrent(), 0);
    hk_remove_all();
    U(DestroyWindow)(w);
}
static void bump(hk_slot *s, int c, WPARAM w, LPARAM l) { ((MSG *)l)->wParam = 999; }
TEST(hooks_winevents_hk_getmessage_modification_is_copied_back) {
    REQUIRE(U(SetWindowsHookExW)); U(GetDesktopWindow)();
    hk_reset();
    HWND w = hk_window("gmmod");
    REQUIRE(w);
    hk_s[0].only_msg = WM_APP + 2; hk_s[0].extra = bump;
    hk_install(0, WH_GETMESSAGE, HK_K_MSG, FALSE);
    U(PostMessageW)(w, WM_APP + 2, 1, 2);
    MSG m; memset(&m, 0, sizeof m);
    CHECK(U(PeekMessageW)(&m, w, WM_APP + 2, WM_APP + 2, PM_REMOVE));
    EXPECT_EQ(m.wParam, 999); /* fnHkINLPMSG copies the callback's MSG back into the caller's */
    hk_remove_all();
    U(DestroyWindow)(w);
}
TEST(hooks_winevents_hk_chain_newest_first_and_next_handle) {
    REQUIRE(U(SetWindowsHookExW)); U(GetDesktopWindow)();
    hk_reset();
    HWND w = hk_window("chain");
    REQUIRE(w);
    hk_s[0].only_msg = hk_s[1].only_msg = WM_APP + 3;
    hk_install(0, WH_GETMESSAGE, HK_K_MSG, FALSE);   /* older */
    hk_install(1, WH_GETMESSAGE, HK_K_MSG, FALSE);   /* newer */
    hk_s[1].callnext = 1;
    U(PostMessageW)(w, WM_APP + 3, 0, 0);
    MSG m;
    CHECK(U(PeekMessageW)(&m, w, WM_APP + 3, WM_APP + 3, PM_REMOVE));
    CHECK(hk_s[1].calls >= 1 && hk_s[0].calls >= 1);
    CHECK(hk_s[1].first_order < hk_s[0].first_order);            /* most recent hook is called first */
    EXPECT_EQ(hk_s[1].ci_cur, (ULONG_PTR)hk_s[0].hh);             /* CI.phkCurrent = next hook in the chain */
    hk_remove_all();
    U(DestroyWindow)(w);
}
TEST(hooks_winevents_hk_chain_stops_without_callnext) {
    REQUIRE(U(SetWindowsHookExW)); U(GetDesktopWindow)();
    hk_reset();
    HWND w = hk_window("chain2");
    REQUIRE(w);
    hk_s[0].only_msg = hk_s[1].only_msg = WM_APP + 4;
    hk_install(0, WH_GETMESSAGE, HK_K_MSG, FALSE);
    hk_install(1, WH_GETMESSAGE, HK_K_MSG, FALSE);
    U(PostMessageW)(w, WM_APP + 4, 0, 0);
    MSG m;
    CHECK(U(PeekMessageW)(&m, w, WM_APP + 4, WM_APP + 4, PM_REMOVE));
    CHECK(hk_s[1].calls >= 1);
    EXPECT_EQ(hk_s[0].calls, 0);
    hk_remove_all();
    U(DestroyWindow)(w);
}
TEST(hooks_winevents_hk_msgfilter_return_value_chain) {
    REQUIRE(U(SetWindowsHookExW)); REQUIRE(U(CallMsgFilterW)); U(GetDesktopWindow)();
    hk_reset();
    hk_s[0].ret = 7;
    hk_install(0, WH_MSGFILTER, HK_K_MSG, FALSE);
    hk_s[1].callnext = 1; hk_s[1].add = 1;
    hk_install(1, WH_MSGFILTER, HK_K_MSG, FALSE);
    MSG m; memset(&m, 0, sizeof m); m.message = WM_APP + 5; m.wParam = 3; m.lParam = 4;
    int r = (int)U(CallMsgFilterW)(&m, 4);
    EXPECT_EQ(r, 8);   /* newer hook returns CallNextHookEx() + 1, older returns 7 */
    EXPECT_EQ(hk_s[1].code, 4);
    EXPECT_EQ(hk_s[1].msg.wParam, 3);
    hk_remove_all();
}
TEST(hooks_winevents_hk_msgfilter_bad_message_bits) {
    REQUIRE(U(SetWindowsHookExW)); REQUIRE(U(CallMsgFilterW)); U(GetDesktopWindow)();
    hk_reset();
    hk_install(0, WH_MSGFILTER, HK_K_MSG, FALSE);
    MSG m; memset(&m, 0, sizeof m); m.message = 0x20000;
    SetLastError(0xDEAD0001);
    EXPECT_EQ(U(CallMsgFilterW)(&m, 4), 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    m.message = 0x240;
    EXPECT_EQ(U(CallMsgFilterW)(&m, 4), 0);
    EXPECT_EQ(hk_s[0].total, 0);
    hk_remove_all();
}
TEST(hooks_winevents_hk_cwp_cwpret_sequence) {
    REQUIRE(U(SetWindowsHookExW)); U(GetDesktopWindow)();
    hk_reset();
    HWND w = hk_window("cwp");
    REQUIRE(w);
    hk_s[0].only_msg = hk_s[1].only_msg = WM_APP + 6;
    hk_install(0, WH_CALLWNDPROC, HK_K_CWP, FALSE);
    hk_install(1, WH_CALLWNDPROCRET, HK_K_CWPRET, FALSE);
    LRESULT r = U(SendMessageW)(w, WM_APP + 6, 5, 6);
    EXPECT_EQ(r, 11);
    int c = hk_log_find('C', WM_APP + 6), wp = hk_log_find('W', WM_APP + 6), rr = hk_log_find('R', WM_APP + 6);
    CHECK(c >= 0 && wp > c && rr > wp);
    EXPECT_EQ(hk_s[0].cwp.wParam, 5); EXPECT_EQ(hk_s[0].cwp.lParam, 6); CHECK(hk_s[0].cwp.hwnd == w);
    EXPECT_EQ(hk_s[0].wp, 0);            /* wParam of a CallWndProc hook: zero for a message sent from the receiving thread itself */
    EXPECT_EQ(hk_s[0].ci_hookcur, 0x40000);
    EXPECT_EQ(hk_s[1].cwpr.lResult, 11);
    EXPECT_EQ(hk_s[1].ci_hookcur, 0xC0000);
    hk_remove_all();
    U(DestroyWindow)(w);
}
static HWND g_cwp_target;
static DWORD WINAPI cwp_sender(LPVOID p) { U(SendMessageW)(g_cwp_target, WM_APP + 6, 5, 6); return 0; }
TEST(hooks_winevents_hk_cwp_wparam_set_for_message_from_another_thread) {
    REQUIRE(U(SetWindowsHookExW)); U(GetDesktopWindow)();
    hk_reset();
    HWND w = hk_window("cwpx");
    REQUIRE(w);
    g_cwp_target = w;
    hk_s[0].only_msg = WM_APP + 6;
    hk_install(0, WH_CALLWNDPROC, HK_K_CWP, FALSE);
    HANDLE t = CreateThread(NULL, 0, cwp_sender, NULL, 0, NULL);
    for (int i = 0; i < 100 && hk_s[0].calls == 0; i++) k_pump(20);   /* the sent message is delivered while this thread pumps */
    WaitForSingleObject(t, 3000); CloseHandle(t);
    EXPECT_EQ(hk_s[0].calls, 1);
    EXPECT_EQ(hk_s[0].wp, 1);            /* nonzero: sent by another thread */
    EXPECT_EQ(hk_s[0].tid, GetCurrentThreadId());   /* the hook runs on the receiving thread */
    EXPECT_EQ(hk_s[0].cwp.wParam, 5); EXPECT_EQ(hk_s[0].cwp.lParam, 6);
    hk_remove_all();
    U(DestroyWindow)(w);
}
TEST(hooks_winevents_hk_cwp_ansi_flag_in_clientinfo) {
    REQUIRE(U(SetWindowsHookExA)); U(GetDesktopWindow)();
    hk_reset();
    HWND w = hk_window("cwpa");
    REQUIRE(w);
    hk_s[0].only_msg = WM_APP + 7;
    hk_install(0, WH_CALLWNDPROC, HK_K_CWP, TRUE);
    U(SendMessageW)(w, WM_APP + 7, 0, 0);
    EXPECT_EQ(hk_s[0].calls, 1);
    EXPECT_EQ(hk_s[0].ci_hookcur, 0x40001); /* DispatchHookA sets the low bit */
    hk_remove_all();
    U(DestroyWindow)(w);
}
TEST(hooks_winevents_hk_cbt_createwnd_and_veto) {
    REQUIRE(U(SetWindowsHookExW)); U(GetDesktopWindow)();
    hk_reset();
    hk_s[0].only_code = HCBT_CREATEWND;
    hk_install(0, WH_CBT, HK_K_RAW, FALSE);
    HWND w = hk_window("cbt");
    CHECK(w != NULL);
    EXPECT_EQ(hk_s[0].calls, 1);
    CHECK(w && (HWND)hk_s[0].wp == w);  /* wParam is the new window's handle */
    if (w) U(DestroyWindow)(w);
    hk_s[0].ret = 1; hk_s[0].calls = 0;
    int nc0 = hk_msg_seen(WM_NCCREATE);
    HWND w2 = hk_window("cbtveto");
    CHECK(w2 == NULL);  /* nonzero from HCBT_CREATEWND aborts creation */
    EXPECT_EQ(hk_s[0].calls, 1);
    t_note("WM_NCCREATE deliveries during vetoed create: %d", hk_msg_seen(WM_NCCREATE) - nc0); /* ordering vs the CBT call is not proven */
    if (hk_s[0].calls) CHECK(hk_user_obj_dead((HANDLE)hk_s[0].wp));
    hk_remove_all();
}
TEST(hooks_winevents_hk_cbt_destroywnd_veto) {
    REQUIRE(U(SetWindowsHookExW)); U(GetDesktopWindow)();
    hk_reset();
    HWND w = hk_window("cbtd");
    REQUIRE(w);
    hk_s[0].only_code = HCBT_DESTROYWND; hk_s[0].ret = 1;
    hk_install(0, WH_CBT, HK_K_RAW, FALSE);
    U(DestroyWindow)(w);
    EXPECT_EQ(hk_s[0].calls, 1);
    CHECK((HWND)hk_s[0].wp == w);
    CHECK(U(IsWindow)(w));
    hk_remove_all();
    U(DestroyWindow)(w);
    CHECK(!U(IsWindow)(w));
}
TEST(hooks_winevents_hk_cbt_syscommand_and_setfocus_veto) {
    REQUIRE(U(SetWindowsHookExW)); U(GetDesktopWindow)();
    hk_reset();
    HWND w = hk_window("cbts");
    REQUIRE(w);
    hk_s[0].ret = 1;
    hk_s[0].only_code = HCBT_SYSCOMMAND;
    hk_install(0, WH_CBT, HK_K_RAW, FALSE);
    U(SendMessageW)(w, WM_SYSCOMMAND, SC_MINIMIZE, 0);
    EXPECT_EQ(hk_s[0].calls, 1);
    EXPECT_EQ(hk_s[0].wp, SC_MINIMIZE);
    CHECK(!U(IsIconic)(w));
    hk_s[0].only_code = HCBT_SETFOCUS; hk_s[0].calls = 0;
    U(SetFocus)(NULL);
    /* only called when the focus actually changes; the thread may have no focus window at all */
    t_note("HCBT_SETFOCUS calls for SetFocus(NULL): %ld", (long)hk_s[0].calls);
    if (hk_s[0].calls) EXPECT_EQ(hk_s[0].wp, 0);
    hk_remove_all();
    U(DestroyWindow)(w);
}
TEST(hooks_winevents_hk_kcb_getmessage_slot) {
    REQUIRE(U(SetWindowsHookExW)); U(GetDesktopWindow)();
    if (!hk_win10_2004_family()) { t_skip("callback slot indices are for the 19041..19045 builds"); return; }
    hk_reset();
    HWND w = hk_window("kcbgm");
    REQUIRE(w);
    if (!k_cb_hook(HK_CB_HKINLPMSG)) { U(DestroyWindow)(w); t_skip("cannot hook slot"); return; }
    hk_install(0, WH_GETMESSAGE, HK_K_MSG, FALSE);
    for (int i = 0; i < 3; i++) U(PostMessageW)(w, WM_APP + 8, i, 0);
    MSG m;
    while (U(PeekMessageW)(&m, w, WM_APP + 8, WM_APP + 8, PM_REMOVE)) {}
    /* every hook invocation is one fnHkINLPMSG callback */
    EXPECT_EQ(k_cb_hit_count(HK_CB_HKINLPMSG), (unsigned long)hk_s[0].total);
    CHECK(hk_s[0].total >= 3);
    hk_remove_all();
    k_cb_unhook_all();
    U(DestroyWindow)(w);
}
static void exit_with_hook(hk_worker *w) {
    HHOOK h = U(SetWindowsHookExW)(WH_GETMESSAGE, hk_nop_proc, NULL, GetCurrentThreadId());
    w->out[0] = (ULONG_PTR)h;
}
TEST(hooks_winevents_hk_removed_when_owner_thread_exits) {
    REQUIRE(U(SetWindowsHookExW)); U(GetDesktopWindow)();
    hk_worker w;
    REQUIRE(hk_worker_start(&w, exit_with_hook, NULL));
    HHOOK h = (HHOOK)w.out[0];
    CHECK(h != NULL);
    if (h) CHECK(hk_user_obj_valid(h, 5));
    hk_worker_stop(&w);
    for (int i = 0; i < 40 && h && !hk_user_obj_dead(h); i++) Sleep(50);
    if (h) {
        CHECK(hk_user_obj_dead(h));
        SetLastError(0xDEAD0001);
        CHECK(!U(UnhookWindowsHookEx)(h));
        EXPECT_ERR(HKE_INVALID_HOOK_HANDLE);
    }
}
static void do_unhook(hk_worker *w) { w->out[0] = (ULONG_PTR)U(UnhookWindowsHookEx)((HHOOK)w->arg); }
TEST(hooks_winevents_hk_unhook_from_other_thread_allowed) {
    REQUIRE(U(SetWindowsHookExW)); U(GetDesktopWindow)();
    HHOOK h = U(SetWindowsHookExW)(WH_GETMESSAGE, hk_nop_proc, NULL, GetCurrentThreadId());
    REQUIRE(h);
    hk_worker w;
    REQUIRE(hk_worker_start(&w, do_unhook, (void *)h));
    CHECK(w.out[0] != 0);   /* zzzUnhookWindowsHookEx has no ownership check */
    hk_worker_stop(&w);
    CHECK(hk_user_obj_dead(h));
    CHECK(!(hk_ci_fshooks() & HK_BIT(WH_GETMESSAGE)));
}
static void noop_init(hk_worker *w) {}
TEST(hooks_winevents_hk_hook_other_thread_sets_its_teb_bit) {
    REQUIRE(U(SetWindowsHookExW)); U(GetDesktopWindow)();
    hk_worker w;
    REQUIRE(hk_worker_start(&w, noop_init, NULL));
    HHOOK h = U(SetWindowsHookExW)(WH_CBT, hk_nop_proc, NULL, w.tid);
    CHECK(h != NULL);
    HANDLE th = OpenThread(THREAD_QUERY_INFORMATION, FALSE, w.tid);
    typedef struct { LONG st; PVOID teb; ULONG_PTR rest[6]; } TBI;
    typedef LONG (WINAPI *qit_t)(HANDLE, int, PVOID, ULONG, PULONG);
    qit_t qit = (qit_t)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationThread");
    TBI tbi; memset(&tbi, 0, sizeof tbi);
    if (th && qit && qit(th, 0, &tbi, 48 /* sizeof(THREAD_BASIC_INFORMATION): any other length is rejected */, NULL) >= 0 && tbi.teb) {
        CHECK(hk_teb_fshooks(tbi.teb) & HK_BIT(WH_CBT));   /* the kernel updates the TARGET thread's _CLIENTINFO */
        CHECK(!(hk_ci_fshooks() & HK_BIT(WH_CBT)));
    } else t_skip("cannot read worker TEB");
    if (th) CloseHandle(th);
    if (h) U(UnhookWindowsHookEx)(h);
    hk_worker_stop(&w);
}
TEST(hooks_winevents_hk_peer_thread_hook_needs_module) {
    REQUIRE(U(SetWindowsHookExW)); U(GetDesktopWindow)();
    hk_peer p;
    if (!hk_peer_start(&p)) { t_skip("peer process did not start"); return; }
    for (unsigned i = 0; i < 11; i++) {
        SetLastError(0xDEAD0001);
        HHOOK h = U(SetWindowsHookExW)(hk_thread_ids[i], hk_nop_proc, NULL, p.tid);
        CHECKF(h == NULL, "id %d", hk_thread_ids[i]);
        if (h) U(UnhookWindowsHookEx)(h);
        EXPECT_ERR(HKE_HOOK_NEEDS_HMOD);  /* different process, no module: zzzSetWindowsHookEx 0x594 */
    }
    hk_peer_stop(&p);
}
TEST(hooks_winevents_we_peer_events_delivered_out_of_context) {
    NEEDW();
    hk_peer p;
    if (!hk_peer_start(&p)) { t_skip("peer process did not start"); return; }
    we_reset();
    HWINEVENTHOOK h = sw(0xA000, 0xA0FF, NULL, we_proc, 0, 0, 0);
    REQUIRE(h);
    CHECK((UN(NtUserNotifyWinEvent)(0xA009, p.hwnd, 1, 2) & 0xff) == 1);
    k_pump(100);
    struct we_rec *r = we_find(0xA009, 0);
    CHECK(r != NULL);
    if (r) { EXPECT_EQ(r->tid, p.tid); CHECK(r->hwnd == p.hwnd); EXPECT_EQ(r->cbtid, GetCurrentThreadId()); }
    UN(NtUserUnhookWinEvent)(h);
    hk_peer_stop(&p);
}
