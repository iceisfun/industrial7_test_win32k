/* x_thread_desktop_heap_multi / focus: focus, active window, capture and caret belong to a thread's input state.
 *
 * SetFocus / SetActiveWindow / SetCapture / CreateCaret on one thread are seen by GetFocus / GetActiveWindow / GetCapture and
 * GetGUIThreadInfo of that thread and by nobody else's; a thread created afterwards starts with none of them even when its
 * creator holds all four; destroying the window that holds one clears it for that thread only; a window of another thread
 * cannot be focused from here. NtUserNavigateFocus and NtUserClearForeground are asked from different threads and processes
 * and answer alike. All windows are tiny off-screen tool windows shown without activation; nothing is sent to other
 * processes' windows. Which window is the foreground one is never asserted.
 */
#include "x.h"

struct fa { HWND h[6]; DWORD v[8]; GUITHREADINFO gi; };

static void gti(DWORD tid, GUITHREADINFO *g) { memset(g, 0, sizeof *g); g->cbSize = sizeof *g; U(GetGUIThreadInfo)(tid, g); }

static void job_setfocus(struct xt_wk *w, void *p) {
    struct fa *a = p;
    U(SetFocus)(w->w[0]);
    a->h[0] = U(GetFocus)(); a->h[1] = U(GetActiveWindow)(); a->h[2] = U(GetCapture)();
}
static void job_none(struct xt_wk *w, void *p) {
    struct fa *a = p;
    a->h[0] = U(GetFocus)(); a->h[1] = U(GetActiveWindow)(); a->h[2] = U(GetCapture)();
}

/* ---- focus and active ---- */
XT(setfocus_on_a_worker_window_is_the_workers_focus) {
    NEED_ALL;
    struct xt_wk w; struct fa a;
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_setfocus, &a));
    EXPECT_EQ(a.h[0], w.w[0]);
    xt_wk_stop(&w);
}
XT(setfocus_on_a_worker_window_activates_it_for_the_worker) {
    NEED_ALL;
    struct xt_wk w; struct fa a;
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_setfocus, &a));
    EXPECT_EQ(a.h[1], w.w[0]);
    xt_wk_stop(&w);
}
XT(worker_focus_is_not_the_main_threads_focus) {
    NEED_ALL;
    struct xt_wk w; struct fa a;
    HWND f0 = U(GetFocus)(), a0 = U(GetActiveWindow)();
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_setfocus, &a));
    EXPECT_EQ(U(GetFocus)(), f0);
    EXPECT_EQ(U(GetActiveWindow)(), a0);
    xt_wk_stop(&w);
}
XT(guithreadinfo_of_the_worker_reports_its_focus_and_active_window) {
    NEED_ALL;
    struct xt_wk w; struct fa a; GUITHREADINFO g;
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_setfocus, &a));
    gti(w.tid, &g);
    EXPECT_EQ(g.hwndFocus, w.w[0]);
    EXPECT_EQ(g.hwndActive, w.w[0]);
    xt_wk_stop(&w);
}
XT(guithreadinfo_of_the_main_thread_does_not_show_the_workers_focus) {
    NEED_ALL;
    struct xt_wk w; struct fa a; GUITHREADINFO g;
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_setfocus, &a));
    gti(GetCurrentThreadId(), &g);
    CHECK(g.hwndFocus != w.w[0]);
    CHECK(g.hwndActive != w.w[0]);
    xt_wk_stop(&w);
}
XT(two_workers_hold_separate_focus_windows) {
    NEED_ALL;
    struct xt_wk x, y; struct fa a, b; GUITHREADINFO gx, gy;
    CHECK(xt_wk_start(&x, 1)); CHECK(xt_wk_start(&y, 1));
    CHECK(xt_wk_run(&x, job_setfocus, &a)); CHECK(xt_wk_run(&y, job_setfocus, &b));
    EXPECT_EQ(a.h[0], x.w[0]);
    EXPECT_EQ(b.h[0], y.w[0]);
    gti(x.tid, &gx); gti(y.tid, &gy);
    EXPECT_EQ(gx.hwndFocus, x.w[0]);
    EXPECT_EQ(gy.hwndFocus, y.w[0]);
    xt_wk_stop(&x); xt_wk_stop(&y);
}
XT(focus_of_one_worker_stays_when_the_other_worker_takes_focus) {
    NEED_ALL;
    struct xt_wk x, y; struct fa a, b; GUITHREADINFO gx;
    CHECK(xt_wk_start(&x, 1)); CHECK(xt_wk_start(&y, 1));
    CHECK(xt_wk_run(&x, job_setfocus, &a));
    CHECK(xt_wk_run(&y, job_setfocus, &b));
    gti(x.tid, &gx);
    EXPECT_EQ(gx.hwndFocus, x.w[0]);          /* x was not asked to give it up */
    CHECK(xt_wk_run(&x, job_none, &a));
    EXPECT_EQ(a.h[0], x.w[0]);
    xt_wk_stop(&x); xt_wk_stop(&y);
}
XT(a_new_worker_without_calls_has_no_focus_active_or_capture) {
    NEED_ALL;
    struct xt_wk w; struct fa a;
    CHECK(xt_wk_start(&w, 0));
    CHECK(xt_wk_run(&w, job_none, &a));
    EXPECT_EQ(a.h[0], NULL); EXPECT_EQ(a.h[1], NULL); EXPECT_EQ(a.h[2], NULL);
    xt_wk_stop(&w);
}
XT(a_new_worker_with_unactivated_windows_has_no_focus_or_capture) {
    NEED_ALL;
    struct xt_wk w; struct fa a;
    CHECK(xt_wk_start(&w, 2));
    CHECK(xt_wk_run(&w, job_none, &a));
    EXPECT_EQ(a.h[0], NULL); EXPECT_EQ(a.h[2], NULL);
    xt_wk_stop(&w);
}
static void fresh_state(void *p) {
    struct fa *a = p;
    a->h[0] = U(GetFocus)(); a->h[1] = U(GetActiveWindow)(); a->h[2] = U(GetCapture)();
    GUITHREADINFO g; gti(GetCurrentThreadId(), &g);
    a->h[3] = g.hwndFocus; a->h[4] = g.hwndActive; a->h[5] = g.hwndCapture;
}
XT(focus_active_and_capture_are_not_inherited_by_a_new_thread) {
    NEED_ALL;
    struct fa a; memset(&a, 0xCC, sizeof a);
    HWND m = xt_window(); CHECK(m);
    U(SetFocus)(m);
    if (U(GetFocus)() != m) { t_skip("the main thread could not take focus"); xt_destroy(m); return; }
    U(SetCapture)(m);
    CHECK(xt_on_thread(fresh_state, &a));
    for (int i = 0; i < 6; i++) CHECK(a.h[i] == NULL);
    EXPECT_EQ(U(GetFocus)(), m);                       /* and the creator keeps its own */
    U(ReleaseCapture)();
    U(SetFocus)(NULL);
    xt_destroy(m);
}
XT(focus_of_the_main_thread_is_not_inherited_by_a_worker_with_windows) {
    NEED_ALL;
    struct xt_wk w; struct fa a;
    HWND m = xt_window(); CHECK(m);
    U(SetFocus)(m);
    if (U(GetFocus)() != m) { t_skip("the main thread could not take focus"); xt_destroy(m); return; }
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_none, &a));
    EXPECT_EQ(a.h[0], NULL);
    EXPECT_EQ(U(GetFocus)(), m);
    xt_wk_stop(&w);
    U(SetFocus)(NULL);
    xt_destroy(m);
}
static void job_setfocus_foreign(struct xt_wk *w, void *p) {
    struct fa *a = p; HWND f = a->h[5];
    SET_ERR(SENT);
    HWND r = U(SetFocus)(f);
    a->v[0] = GetLastError(); a->h[0] = r; a->h[1] = U(GetFocus)();
}
XT(setfocus_on_a_window_of_another_thread_does_not_change_either_focus) {
    NEED_ALL;
    struct xt_wk w; struct fa a; memset(&a, 0, sizeof a);
    HWND m = xt_window(); CHECK(m);
    U(SetFocus)(m);
    HWND f0 = U(GetFocus)();
    CHECK(xt_wk_start(&w, 1));
    a.h[5] = m;
    CHECK(xt_wk_run(&w, job_setfocus_foreign, &a));
    EXPECT_EQ(a.v[0], E_ACCESS_DENIED);
    EXPECT_EQ(a.h[1], NULL);                          /* the worker did not get it */
    EXPECT_EQ(U(GetFocus)(), f0);                     /* nor did the owner lose it */
    xt_wk_stop(&w);
    U(SetFocus)(NULL);
    xt_destroy(m);
}
static void job_setactive_foreign(struct xt_wk *w, void *p) {
    struct fa *a = p; HWND f = a->h[5];
    U(SetActiveWindow)(f);
    a->h[0] = U(GetActiveWindow)();
}
XT(setactivewindow_on_a_window_of_another_thread_does_not_activate_it_here) {
    NEED_ALL;
    struct xt_wk w; struct fa a; memset(&a, 0, sizeof a);
    HWND m = xt_window(); CHECK(m);
    CHECK(xt_wk_start(&w, 1));
    a.h[5] = m;
    CHECK(xt_wk_run(&w, job_setactive_foreign, &a));
    CHECK(a.h[0] != m);
    xt_wk_stop(&w);
    xt_destroy(m);
}
static void job_focus_clear(struct xt_wk *w, void *p) {
    struct fa *a = p;
    U(SetFocus)(w->w[0]);
    a->h[0] = U(GetFocus)();
    U(SetFocus)(NULL);
    a->h[1] = U(GetFocus)();
}
XT(setfocus_null_clears_the_workers_focus_only) {
    NEED_ALL;
    struct xt_wk w, o; struct fa a, b;
    CHECK(xt_wk_start(&w, 1)); CHECK(xt_wk_start(&o, 1));
    CHECK(xt_wk_run(&o, job_setfocus, &b));
    CHECK(xt_wk_run(&w, job_focus_clear, &a));
    EXPECT_EQ(a.h[0], w.w[0]);
    EXPECT_EQ(a.h[1], NULL);
    CHECK(xt_wk_run(&o, job_none, &b));
    EXPECT_EQ(b.h[0], o.w[0]);
    xt_wk_stop(&w); xt_wk_stop(&o);
}
static void job_destroy_focus(struct xt_wk *w, void *p) {
    struct fa *a = p;
    U(SetFocus)(w->w[0]);
    U(DestroyWindow)(w->w[0]);
    a->h[0] = U(GetFocus)(); a->h[1] = U(GetActiveWindow)();
}
XT(destroying_the_focus_window_clears_focus_and_active_for_that_worker) {
    NEED_ALL;
    struct xt_wk w, o; struct fa a, b;
    CHECK(xt_wk_start(&w, 1)); CHECK(xt_wk_start(&o, 1));
    CHECK(xt_wk_run(&o, job_setfocus, &b));
    CHECK(xt_wk_run(&w, job_destroy_focus, &a));
    EXPECT_EQ(a.h[0], NULL); EXPECT_EQ(a.h[1], NULL);
    CHECK(xt_wk_run(&o, job_none, &b));
    EXPECT_EQ(b.h[0], o.w[0]);
    xt_wk_stop(&w); xt_wk_stop(&o);
}

/* ---- capture ---- */
static void job_capture(struct xt_wk *w, void *p) {
    struct fa *a = p;
    U(SetCapture)(w->w[0]);
    a->h[0] = U(GetCapture)();
    a->h[1] = U(SetCapture)(w->w[0]);       /* the previous capture window is returned */
}
XT(setcapture_on_a_worker_is_the_workers_capture) {
    NEED_ALL;
    struct xt_wk w; struct fa a;
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_capture, &a));
    EXPECT_EQ(a.h[0], w.w[0]);
    EXPECT_EQ(a.h[1], w.w[0]);
    xt_wk_stop(&w);
}
XT(worker_capture_is_not_the_main_threads_capture) {
    NEED_ALL;
    struct xt_wk w; struct fa a;
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_capture, &a));
    EXPECT_EQ(U(GetCapture)(), NULL);
    xt_wk_stop(&w);
}
XT(guithreadinfo_reports_the_capture_of_that_worker_only) {
    NEED_ALL;
    struct xt_wk w; struct fa a; GUITHREADINFO g;
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_capture, &a));
    gti(w.tid, &g);
    EXPECT_EQ(g.hwndCapture, w.w[0]);
    gti(GetCurrentThreadId(), &g);
    EXPECT_EQ(g.hwndCapture, NULL);
    xt_wk_stop(&w);
}
XT(releasecapture_on_the_main_thread_leaves_the_worker_capture) {
    NEED_ALL;
    struct xt_wk w; struct fa a; GUITHREADINFO g;
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_capture, &a));
    U(ReleaseCapture)();
    gti(w.tid, &g);
    EXPECT_EQ(g.hwndCapture, w.w[0]);
    xt_wk_stop(&w);
}
static void job_release(struct xt_wk *w, void *p) { struct fa *a = p; U(ReleaseCapture)(); a->h[0] = U(GetCapture)(); }
XT(releasecapture_on_a_worker_clears_that_capture) {
    NEED_ALL;
    struct xt_wk w; struct fa a;
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_capture, &a));
    CHECK(xt_wk_run(&w, job_release, &a));
    EXPECT_EQ(a.h[0], NULL);
    xt_wk_stop(&w);
}
XT(main_capture_is_not_seen_by_a_worker) {
    NEED_ALL;
    struct xt_wk w; struct fa a;
    HWND m = xt_window(); CHECK(m);
    U(SetCapture)(m);
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_none, &a));
    EXPECT_EQ(a.h[2], NULL);
    EXPECT_EQ(U(GetCapture)(), m);
    U(ReleaseCapture)();
    xt_wk_stop(&w);
    xt_destroy(m);
}
XT(two_workers_hold_separate_captures) {
    NEED_ALL;
    struct xt_wk x, y; struct fa a, b;
    CHECK(xt_wk_start(&x, 1)); CHECK(xt_wk_start(&y, 1));
    CHECK(xt_wk_run(&x, job_capture, &a)); CHECK(xt_wk_run(&y, job_capture, &b));
    EXPECT_EQ(a.h[0], x.w[0]);
    EXPECT_EQ(b.h[0], y.w[0]);
    CHECK(xt_wk_run(&x, job_none, &a));
    EXPECT_EQ(a.h[2], x.w[0]);
    xt_wk_stop(&x); xt_wk_stop(&y);
}
static void job_capture_destroy(struct xt_wk *w, void *p) {
    struct fa *a = p;
    U(SetCapture)(w->w[0]);
    U(DestroyWindow)(w->w[0]);
    a->h[0] = U(GetCapture)();
}
XT(destroying_the_capture_window_releases_capture) {
    NEED_ALL;
    struct xt_wk w; struct fa a;
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_capture_destroy, &a));
    EXPECT_EQ(a.h[0], NULL);
    xt_wk_stop(&w);
}

/* ---- caret ---- */
static void job_caret(struct xt_wk *w, void *p) {
    struct fa *a = p; GUITHREADINFO g;
    a->v[0] = U(CreateCaret)(w->w[0], NULL, 2, 6);
    U(SetCaretPos)(3, 4);
    gti(GetCurrentThreadId(), &g);
    a->h[0] = g.hwndCaret; a->v[1] = g.rcCaret.left; a->v[2] = g.rcCaret.top;
    a->v[3] = g.rcCaret.right - g.rcCaret.left; a->v[4] = g.rcCaret.bottom - g.rcCaret.top;
    POINT pt = { 0, 0 }; U(GetCaretPos)(&pt);
    a->v[5] = pt.x; a->v[6] = pt.y;
}
XT(caret_created_on_a_worker_belongs_to_that_worker) {
    NEED_ALL;
    struct xt_wk w; struct fa a; GUITHREADINFO g;
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_caret, &a));
    EXPECT_EQ(a.v[0], 1);
    EXPECT_EQ(a.h[0], w.w[0]);
    gti(w.tid, &g);
    EXPECT_EQ(g.hwndCaret, w.w[0]);
    gti(GetCurrentThreadId(), &g);
    EXPECT_EQ(g.hwndCaret, NULL);
    xt_wk_stop(&w);
}
XT(caret_position_and_size_are_reported_for_the_creating_thread) {
    NEED_ALL;
    struct xt_wk w; struct fa a;
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_caret, &a));
    EXPECT_EQ(a.v[1], 3); EXPECT_EQ(a.v[2], 4);
    EXPECT_EQ(a.v[3], 2); EXPECT_EQ(a.v[4], 6);
    EXPECT_EQ(a.v[5], 3); EXPECT_EQ(a.v[6], 4);
    xt_wk_stop(&w);
}
XT(caret_position_of_the_main_thread_is_not_the_workers) {
    NEED_ALL;
    struct xt_wk w; struct fa a; POINT p0 = { 0x55, 0x66 }, p1 = { 0x55, 0x66 };
    SET_ERR(SENT);
    BOOL r0 = U(GetCaretPos)(&p0);
    DWORD e0 = GetLastError();
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_caret, &a));
    BOOL r1 = U(GetCaretPos)(&p1);
    EXPECT_EQ(r0, r1);
    CHECK(p0.x == p1.x && p0.y == p1.y);
    EXPECT_EQ(r0, 1);
    EXPECT_EQ(e0, SENT);
    xt_wk_stop(&w);
}
static void job_caret_destroy(struct xt_wk *w, void *p) {
    struct fa *a = p; GUITHREADINFO g;
    U(CreateCaret)(w->w[0], NULL, 2, 6);
    a->v[0] = U(DestroyCaret)();
    gti(GetCurrentThreadId(), &g);
    a->h[0] = g.hwndCaret;
    SET_ERR(SENT); a->v[1] = U(DestroyCaret)(); a->v[2] = GetLastError();
}
XT(destroycaret_ends_the_workers_caret_and_a_second_call_fails) {
    NEED_ALL;
    struct xt_wk w; struct fa a;
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_caret_destroy, &a));
    EXPECT_EQ(a.v[0], 1);
    EXPECT_EQ(a.h[0], NULL);
    EXPECT_EQ(a.v[1], 0);
    xt_wk_stop(&w);
}
static void job_caret_foreign(struct xt_wk *w, void *p) {
    struct fa *a = p; HWND f = a->h[5];
    SET_ERR(SENT);
    a->v[0] = U(CreateCaret)(f, NULL, 2, 6); a->v[1] = GetLastError();
}
XT(caret_cannot_be_created_in_a_window_of_another_thread) {
    NEED_ALL;
    struct xt_wk w; struct fa a; memset(&a, 0, sizeof a);
    HWND m = xt_window(); CHECK(m);
    CHECK(xt_wk_start(&w, 1));
    a.h[5] = m;
    CHECK(xt_wk_run(&w, job_caret_foreign, &a));
    EXPECT_EQ(a.v[0], 0);
    EXPECT_EQ(a.v[1], SENT);                          /* the call fails without setting a last error */
    xt_wk_stop(&w);
    xt_destroy(m);
}

/* ---- NtUserNavigateFocus / NtUserClearForeground ---- */
static void job_nav(struct xt_wk *w, void *p) {
    struct fa *a = p;
    SET_ERR(SENT);
    a->v[0] = R_D(UN(NtUserNavigateFocus)(w->w[0]));
    a->v[1] = GetLastError();
    a->h[0] = U(GetFocus)();
}
XT(navigatefocus_on_a_worker_window_is_denied_and_moves_no_focus) {
    NEED(NtUserNavigateFocus);
    struct xt_wk w; struct fa a; memset(&a, 0, sizeof a);
    HWND f0 = U(GetFocus)();
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_nav, &a));
    EXPECT_EQ(a.v[0], 0);                             /* an ordinary process is not allowed to navigate focus */
    EXPECT_EQ(a.v[1], E_ACCESS_DENIED);
    EXPECT_EQ(a.h[0], NULL);
    EXPECT_EQ(U(GetFocus)(), f0);                     /* the main thread's focus is not touched */
    xt_wk_stop(&w);
}
static void job_nav_foreign(struct xt_wk *w, void *p) {
    struct fa *a = p; HWND f = a->h[5];
    SET_ERR(SENT);
    a->v[0] = R_D(UN(NtUserNavigateFocus)(f)); a->v[1] = GetLastError();
    a->h[0] = U(GetFocus)();
}
XT(navigatefocus_on_a_window_of_another_thread_is_denied_too) {
    NEED(NtUserNavigateFocus);
    struct xt_wk w; struct fa a; memset(&a, 0, sizeof a);
    HWND m = xt_window(); CHECK(m);
    CHECK(xt_wk_start(&w, 1));
    a.h[5] = m;
    CHECK(xt_wk_run(&w, job_nav_foreign, &a));
    EXPECT_EQ(a.v[0], 0);
    EXPECT_EQ(a.v[1], E_ACCESS_DENIED);
    EXPECT_EQ(a.h[0], NULL);
    xt_wk_stop(&w);
    xt_destroy(m);
}
static void job_nav_bad(struct xt_wk *w, void *p) {
    struct fa *a = p;
    static const ULONG_PTR bad[] = { 0, 0x7FFF0010, 0xFFFFFFFF };
    for (int i = 0; i < 3; i++) { SET_ERR(SENT); a->v[i] = (DWORD)UN(NtUserNavigateFocus)((HWND)bad[i]); a->v[4 + i] = GetLastError(); }
}
XT(navigatefocus_with_invalid_windows_returns_zero_on_a_worker) {
    NEED(NtUserNavigateFocus);
    struct xt_wk w; struct fa a;
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_nav_bad, &a));
    EXPECT_EQ(a.v[0], 0); EXPECT_EQ(a.v[1], 0); EXPECT_EQ(a.v[2], 0);
    for (int i = 4; i < 7; i++) t_info("NavigateFocus bad window gle %lu", a.v[i]);
    xt_wk_stop(&w);
}
static void nav_fresh(void *p) { DWORD *r = p; SET_ERR(SENT); r[0] = (DWORD)UN(NtUserNavigateFocus)(NULL); r[1] = GetLastError(); }
XT(navigatefocus_on_a_fresh_thread_is_denied_like_on_any_other) {
    NEED(NtUserNavigateFocus);
    DWORD r[2] = { 9, 9 };
    CHECK(xt_on_thread(nav_fresh, r));
    EXPECT_EQ(r[0], 0);
    t_info("NavigateFocus(NULL) on a fresh thread: gle %lu", r[1]);
}

