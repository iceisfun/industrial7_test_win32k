/* x_menu_accel_hotkey_lifetime / endmenu: NtUserEndMenu.
 *
 * The menu state belongs to the calling thread's input state. With no menu open EndMenu does nothing and answers TRUE (without
 * touching the last error), on any thread, in any process. With a menu open on thread A, EndMenu on A closes it (TrackPopupMenu
 * returns 0 for TPM_RETURNCMD, the thread leaves menu mode) while EndMenu on any other thread or in another process answers TRUE
 * and leaves A's menu open. A thread can open another menu after one was ended.
 *
 * The menus here are two-item popups opened by a worker thread off to the side of the desktop and closed by a timer inside the
 * thread's own modal loop; no input is sent. Determinism: only symbolic results, flags are masked.
 */
#include "x.h"

#define MENUMODE (GUI_INMENUMODE | GUI_POPUPMENUMODE)

XT(no_menu_returns_true) {
    NEED(NtUserEndMenu);
    HWND w = k_make_window("xm");
    SET_ERR(XE_SENT);
    EXPECT_EQ(UN(NtUserEndMenu)(), 1);
    EXPECT_ERR(XE_SENT);
    U(DestroyWindow)(w);
}
XT(no_menu_repeated_calls_all_true) {
    NEED(NtUserEndMenu);
    k_make_window("xm");
    int bad = 0;
    for (int i = 0; i < 200; i++) if (UN(NtUserEndMenu)() != 1) bad++;
    EXPECT_EQ(bad, 0);
}
XT(no_menu_leaves_the_thread_out_of_menu_mode) {
    NEED(NtUserEndMenu);
    HWND w = k_make_window("xm");
    UN(NtUserEndMenu)();
    EXPECT_EQ(xm_gui_flags(GetCurrentThreadId()) & GUI_INMENUMODE, 0);
    EXPECT_EQ(xm_gui_flags(GetCurrentThreadId()) & GUI_POPUPMENUMODE, 0);
    U(DestroyWindow)(w);
}
XT(no_menu_posts_nothing_to_the_queue) {
    NEED(NtUserEndMenu);
    HWND w = k_make_window("xm");
    MSG m; while (U(PeekMessageW)(&m, NULL, 0, 0, PM_REMOVE)) {}
    UN(NtUserEndMenu)();
    CHECK(!U(PeekMessageW)(&m, NULL, 0, 0, PM_NOREMOVE));
    U(DestroyWindow)(w);
}
XT(user32_end_menu_answers_like_the_syscall_and_keeps_the_error) {
    NEED(NtUserEndMenu);
    k_make_window("xm");
    SET_ERR(XE_SENT);
    EXPECT_EQ(U(EndMenu)(), 1);
    EXPECT_ERR(XE_SENT);
}
static DWORD WINAPI fresh_thread(LPVOID p) {
    t_module("user32.dll");
    int *r = p;
    SET_ERR(XE_SENT);
    r[0] = UN(NtUserEndMenu)();    /* the first win32k call of the thread converts it to a GUI thread */
    r[1] = GetLastError();
    r[2] = UN(NtUserEndMenu)();
    return 0;
}
XT(first_win32k_call_of_a_new_thread_may_be_end_menu) {
    NEED(NtUserEndMenu);
    int r[3] = { -1, -1, -1 };
    HANDLE t = CreateThread(NULL, 0, fresh_thread, r, 0, NULL); WaitForSingleObject(t, 10000); CloseHandle(t);
    EXPECT_EQ(r[0], 1); EXPECT_EQ(r[1], XE_SENT); EXPECT_EQ(r[2], 1);
}
static DWORD WINAPI many_calls(LPVOID p) { t_module("user32.dll"); int *bad = p; for (int i = 0; i < 100; i++) if (UN(NtUserEndMenu)() != 1) (*bad)++; return 0; }
XT(parallel_calls_on_many_threads_are_all_true) {
    NEED(NtUserEndMenu);
    int bad[6] = {0}; HANDLE t[6];
    for (int i = 0; i < 6; i++) t[i] = CreateThread(NULL, 0, many_calls, &bad[i], 0, NULL);
    int total = 0;
    for (int i = 0; i < 6; i++) { WaitForSingleObject(t[i], 20000); CloseHandle(t[i]); total += bad[i]; }
    EXPECT_EQ(total, 0);
}

/* ---- a menu is open ---- */
XT(menu_mode_flags_while_a_popup_is_tracked) {
    NEED(NtUserEndMenu);
    struct xm_menu m;
    CHECK(xm_menu_start(&m, 0, NULL, NULL));
    DWORD f = xm_gui_flags(m.tid);
    EXPECT_EQ(f & MENUMODE, MENUMODE);
    EXPECT_EQ(xm_menu_end(&m), 0);
}
XT(end_menu_on_the_owning_thread_closes_it_and_track_returns_zero) {
    NEED(NtUserEndMenu);
    struct xm_menu m;
    CHECK(xm_menu_start(&m, 0, NULL, NULL));
    LONG r = xm_menu_end(&m);
    EXPECT_EQ(r, 0);
    EXPECT_EQ(m.endret, 1);
    EXPECT_EQ(m.state, 2);
    EXPECT_EQ(xm_gui_flags(m.tid) & GUI_INMENUMODE, 0);
}
XT(end_menu_from_another_thread_answers_true_and_leaves_the_menu_open) {
    NEED(NtUserEndMenu);
    struct xm_menu m;
    CHECK(xm_menu_start(&m, 0, NULL, NULL));
    LONG ticks0 = m.ticks;
    SET_ERR(XE_SENT);
    EXPECT_EQ(UN(NtUserEndMenu)(), 1);
    EXPECT_ERR(XE_SENT);
    EXPECT_EQ(U(EndMenu)(), 1);
    Sleep(300);
    CHECK(xm_menu_alive(&m));
    EXPECT_EQ(m.state, 1);
    CHECK(m.ticks > ticks0);   /* its modal loop kept running */
    EXPECT_EQ(xm_menu_end(&m), 0);
}
static DWORD WINAPI end_on_thread(LPVOID p) { t_module("user32.dll"); *(int *)p = UN(NtUserEndMenu)(); return 0; }
XT(end_menu_from_a_third_thread_with_its_own_window_leaves_the_menu_open) {
    NEED(NtUserEndMenu);
    struct xm_menu m; struct xm_thr th;
    CHECK(xm_menu_start(&m, 0, NULL, NULL));
    CHECK(xm_thr_start(&th));
    int r = -1;
    HANDLE t = CreateThread(NULL, 0, end_on_thread, &r, 0, NULL); WaitForSingleObject(t, 10000); CloseHandle(t);
    EXPECT_EQ(r, 1);
    Sleep(200);
    CHECK(xm_menu_alive(&m));
    xm_thr_stop(&th);
    EXPECT_EQ(xm_menu_end(&m), 0);
}
CHILD(xm_endmenu_other_process) {
    t_module("user32.dll");
    HWND w = k_make_window("c");
    int bad = 0;
    for (int i = 0; i < 20; i++) if (UN(NtUserEndMenu)() != 1) bad++;
    if (w) U(DestroyWindow)(w);
    return bad;
}
XT(end_menu_from_another_process_answers_true_and_leaves_the_menu_open) {
    NEED(NtUserEndMenu);
    struct xm_menu m;
    CHECK(xm_menu_start(&m, 0, NULL, NULL));
    EXPECT_EQ(t_run_child("xm_endmenu_other_process", "", 30000), 0);
    CHECK(xm_menu_alive(&m));
    EXPECT_EQ(xm_menu_end(&m), 0);
}
XT(the_menu_loop_keeps_ticking_after_a_foreign_end_menu) {
    NEED(NtUserEndMenu);
    struct xm_menu m;
    CHECK(xm_menu_start(&m, 0, NULL, NULL));
    for (int i = 0; i < 10; i++) UN(NtUserEndMenu)();
    LONG a = m.ticks; Sleep(250);
    CHECK(m.ticks >= a + 2);
    EXPECT_EQ(xm_menu_end(&m), 0);
}
XT(end_menu_is_idempotent_inside_the_loop) {
    NEED(NtUserEndMenu);
    struct xm_menu m;
    CHECK(xm_menu_start(&m, 0, NULL, NULL));
    xm_menu_end(&m);
    EXPECT_EQ(m.endret, 1);
    SET_ERR(XE_SENT);
    EXPECT_EQ(UN(NtUserEndMenu)(), 1);   /* and nothing is open on this thread either */
}
XT(menu_with_drag_drop_style_is_ended_the_same_way) {
    NEED(NtUserEndMenu);
    struct xm_menu m;
    CHECK(xm_menu_start(&m, MNS_DRAGDROP, NULL, NULL));
    EXPECT_EQ(xm_gui_flags(m.tid) & MENUMODE, MENUMODE);
    EXPECT_EQ(xm_menu_end(&m), 0);
    EXPECT_EQ(m.endret, 1);
}
XT(menu_with_check_by_position_style_is_ended_the_same_way) {
    NEED(NtUserEndMenu);
    struct xm_menu m;
    CHECK(xm_menu_start(&m, MNS_CHECKORBMP, NULL, NULL));
    EXPECT_EQ(xm_menu_end(&m), 0);
    EXPECT_EQ(m.endret, 1);
}
XT(a_cancel_mode_message_from_another_thread_also_closes_the_menu) {
    NEED(NtUserEndMenu);
    struct xm_menu m;
    CHECK(xm_menu_start(&m, 0, NULL, NULL));
    DWORD_PTR res = 0;
    U(SendMessageTimeoutW)(m.w, WM_CANCELMODE, 0, 0, SMTO_ABORTIFHUNG, 3000, &res);
    DWORD t0 = GetTickCount();
    while (m.state != 2 && GetTickCount() - t0 < 3000) Sleep(10);
    EXPECT_EQ(m.state, 2);
    EXPECT_EQ(m.ret, 0);
    EXPECT_EQ(xm_menu_end(&m), 0);
}
XT(menu_thread_leaves_menu_mode_when_the_loop_returns) {
    NEED(NtUserEndMenu);
    struct xm_menu m;
    CHECK(xm_menu_start(&m, 0, NULL, NULL));
    xm_menu_end(&m);
    EXPECT_EQ(m.state, 2);
    EXPECT_EQ(xm_gui_flags(m.tid) & GUI_INMENUMODE, 0);   /* the thread may already be gone: no flags */
}

/* ---- several rounds on one thread ---- */
static volatile LONG rd_end, rd_n; static HANDLE rd_rel;
static VOID CALLBACK rd_timer(HWND h, UINT m, UINT_PTR id, DWORD t) { if (rd_rel && WaitForSingleObject(rd_rel, 0) == WAIT_OBJECT_0) { ResetEvent(rd_rel); rd_end = U(EndMenu)(); } }
static DWORD WINAPI rounds_thread(LPVOID p) {
    int *ret = p;
    HWND w = k_make_window("xm_rounds");
    HMENU m = U(CreatePopupMenu)(); U(AppendMenuA)(m, MF_STRING, 7, "one");
    UINT_PTR t = U(SetTimer)(NULL, 0, 50, rd_timer);
    for (int i = 0; i < 3; i++) {
        rd_n = i + 1;
        ret[i] = U(TrackPopupMenu)(m, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN, 200, 200, 0, w, NULL);
        ret[3 + i] = rd_end;
    }
    U(KillTimer)(NULL, t); U(DestroyMenu)(m); U(DestroyWindow)(w);
    return 0;
}
XT(three_menus_in_a_row_on_one_thread_are_each_ended_by_end_menu) {
    NEED(NtUserEndMenu);
    int ret[6] = { 9, 9, 9, 9, 9, 9 };
    rd_rel = CreateEventA(NULL, TRUE, FALSE, NULL); rd_end = 0; rd_n = 0;
    HANDLE t = CreateThread(NULL, 0, rounds_thread, ret, 0, NULL);
    for (int i = 1; i <= 3; i++) {
        DWORD t0 = GetTickCount();
        while (GetTickCount() - t0 < 5000) { if (rd_n == i) { DWORD f = 0; DWORD tid = GetThreadId(t); f = xm_gui_flags(tid); if (f & GUI_INMENUMODE) break; } Sleep(10); }
        Sleep(120);
        rd_end = 0;
        SetEvent(rd_rel);
        t0 = GetTickCount();
        while (rd_n == i && GetTickCount() - t0 < 5000) Sleep(10);
    }
    WaitForSingleObject(t, 10000); CloseHandle(t); CloseHandle(rd_rel); rd_rel = NULL;
    for (int i = 0; i < 3; i++) { EXPECT_EQ(ret[i], 0); EXPECT_EQ(ret[3 + i], 1); }
}
