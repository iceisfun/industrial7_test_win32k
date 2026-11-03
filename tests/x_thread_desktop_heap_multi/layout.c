/* x_thread_desktop_heap_multi / layout: resize layout synchronisation belongs to a window, not to a thread or process.
 *
 * NtUserEnableResizeLayoutSynchronization(hwnd, on) sets (on != 0) or clears the 0x80 flag of that window and
 * NtUserIsResizeLayoutSynchronizationEnabled(hwnd) answers it; BeginLayoutUpdate / LayoutCompleted answer TRUE for a window
 * and FALSE with ERROR_INVALID_WINDOW_HANDLE for a stale one. The flag is not inherited by a child window, by a window made
 * later by the same thread, by a window of another thread, or by a window of the same thread that was not asked; it is
 * visible from every thread that asks about the window; and it ends with the window. The layout notifications change
 * nothing the flag query can see. ConfirmResizeCommit is for the compositor process: here it answers FALSE for every window,
 * from every thread and process.
 */
#include "x.h"

struct la { DWORD v[16]; HWND h[4]; };

static DWORD ISL(HWND h) { return R_B(UN(NtUserIsResizeLayoutSynchronizationEnabled)(h)); }
static DWORD ENL(HWND h, DWORD on) { return R_B(UN(NtUserEnableResizeLayoutSynchronization)(h, on)); }

#define NEED_LAYOUT do { NEED(NtUserEnableResizeLayoutSynchronization); NEED(NtUserIsResizeLayoutSynchronizationEnabled); } while (0)

static void job_enable_own(struct xt_wk *w, void *p) {
    struct la *a = p;
    a->v[0] = ENL(w->w[0], 1);
    a->v[1] = ISL(w->w[0]);
    a->v[2] = ISL(w->w[1]);
}
static void job_is_window(struct xt_wk *w, void *p) { struct la *a = p; a->v[3] = ISL(a->h[0]); }

XT(enable_on_a_worker_window_sets_the_flag) {
    NEED_LAYOUT;
    struct xt_wk w; struct la a;
    CHECK(xt_wk_start(&w, 2));
    CHECK(xt_wk_run(&w, job_enable_own, &a));
    EXPECT_EQ(a.v[0], 1);
    EXPECT_EQ(a.v[1], 0x80);
    xt_wk_stop(&w);
}
XT(flag_of_one_worker_window_is_not_on_its_sibling_window) {
    NEED_LAYOUT;
    struct xt_wk w; struct la a;
    CHECK(xt_wk_start(&w, 2));
    CHECK(xt_wk_run(&w, job_enable_own, &a));
    EXPECT_EQ(a.v[2], 0);
    xt_wk_stop(&w);
}
XT(flag_set_on_a_worker_window_is_seen_from_the_main_thread) {
    NEED_LAYOUT;
    struct xt_wk w; struct la a;
    CHECK(xt_wk_start(&w, 2));
    CHECK(xt_wk_run(&w, job_enable_own, &a));
    EXPECT_EQ(ISL(w.w[0]), 0x80);
    EXPECT_EQ(ISL(w.w[1]), 0);
    xt_wk_stop(&w);
}
XT(flag_set_on_a_worker_window_is_seen_from_another_worker) {
    NEED_LAYOUT;
    struct xt_wk w, o; struct la a, b;
    CHECK(xt_wk_start(&w, 1)); CHECK(xt_wk_start(&o, 1));
    CHECK(xt_wk_run(&w, job_enable_own, &a));
    b.h[0] = w.w[0];
    CHECK(xt_wk_run(&o, job_is_window, &b));
    EXPECT_EQ(b.v[3], 0x80);
    xt_wk_stop(&w); xt_wk_stop(&o);
}
XT(flag_of_a_main_window_is_not_a_workers_flag) {
    NEED_LAYOUT;
    struct xt_wk w; struct la a;
    HWND m = xt_window(); CHECK(m);
    CHECK(ENL(m, 1) == 1);
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_enable_own, &a));
    EXPECT_EQ(a.v[1], 0x80);
    CHECK(ENL(m, 0) == 1);
    EXPECT_EQ(ISL(w.w[0]), 0x80);               /* clearing the main window's flag does not clear the worker's */
    EXPECT_EQ(ISL(m), 0);
    xt_wk_stop(&w);
    xt_destroy(m);
}
static void job_enable_foreign(struct xt_wk *w, void *p) {
    struct la *a = p; HWND f = a->h[0];
    SET_ERR(SENT);
    a->v[0] = ENL(f, 1); a->v[1] = GetLastError();
    a->v[2] = ISL(f);
}
XT(enable_on_a_window_of_another_thread_sets_the_flag_for_everybody) {
    NEED_LAYOUT;
    struct xt_wk w; struct la a; memset(&a, 0, sizeof a);
    HWND m = xt_window(); CHECK(m);
    CHECK(xt_wk_start(&w, 1));
    a.h[0] = m;
    CHECK(xt_wk_run(&w, job_enable_foreign, &a));
    EXPECT_EQ(a.v[0], 1);                       /* a window of another thread is not refused ... */
    EXPECT_EQ(a.v[1], SENT);
    EXPECT_EQ(a.v[2], 0x80);                    /* ... and the owner thread sees the flag at once */
    EXPECT_EQ(ISL(m), 0x80);
    ENL(m, 0);
    xt_wk_stop(&w);
    xt_destroy(m);
}
XT(flag_is_not_inherited_by_a_child_window) {
    NEED_LAYOUT;
    HWND p = xt_window(), c; CHECK(p);
    ENL(p, 1);
    c = xt_child_window(p); CHECK(c);
    EXPECT_EQ(ISL(p), 0x80);
    EXPECT_EQ(ISL(c), 0);
    xt_destroy(p);
}
XT(flag_is_not_inherited_by_a_window_made_later_on_the_same_thread) {
    NEED_LAYOUT;
    HWND a = xt_window(), b; CHECK(a);
    ENL(a, 1);
    b = xt_window(); CHECK(b);
    EXPECT_EQ(ISL(b), 0);
    ENL(a, 0);
    xt_destroy(a); xt_destroy(b);
}
static void job_make_after(struct xt_wk *w, void *p) {
    struct la *a = p;
    ENL(w->w[0], 1);
    HWND n = xt_window();
    a->v[0] = ISL(n); a->v[1] = ISL(w->w[0]);
    xt_destroy(n);
}
XT(flag_is_not_inherited_by_a_window_made_later_on_a_worker) {
    NEED_LAYOUT;
    struct xt_wk w; struct la a;
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_make_after, &a));
    EXPECT_EQ(a.v[0], 0); EXPECT_EQ(a.v[1], 0x80);
    xt_wk_stop(&w);
}
static void fresh_enable(void *p) {
    DWORD *r = p;
    HWND h = xt_window();
    r[0] = ISL(h);
    r[1] = ENL(h, 1);
    r[2] = ISL(h);
    xt_destroy(h);
}
XT(new_thread_window_starts_without_the_flag_whatever_the_main_window_holds) {
    NEED_LAYOUT;
    DWORD r[3] = { 9, 9, 9 };
    HWND m = xt_window(); CHECK(m);
    ENL(m, 1);
    CHECK(xt_on_thread(fresh_enable, r));
    EXPECT_EQ(r[0], 0); EXPECT_EQ(r[1], 1); EXPECT_EQ(r[2], 0x80);
    EXPECT_EQ(ISL(m), 0x80);
    xt_destroy(m);
}
XT(enable_with_zero_clears_and_any_nonzero_value_sets) {
    NEED_LAYOUT;
    HWND h = xt_window(); CHECK(h);
    static const DWORD v[] = { 1, 2, 0x80, 0x80000000u, 0xFFFFFFFFu };
    int bad = 0;
    for (int i = 0; i < 5; i++) {
        ENL(h, v[i]); if (ISL(h) != 0x80) bad++;
        ENL(h, 0); if (ISL(h) != 0) bad++;
    }
    EXPECT_EQ(bad, 0);
    xt_destroy(h);
}
XT(enable_is_idempotent) {
    NEED_LAYOUT;
    HWND h = xt_window(); CHECK(h);
    ENL(h, 1); ENL(h, 1);
    EXPECT_EQ(ISL(h), 0x80);
    ENL(h, 0); ENL(h, 0);
    EXPECT_EQ(ISL(h), 0);
    xt_destroy(h);
}
XT(enable_keeps_the_last_error_for_a_valid_window) {
    NEED_LAYOUT;
    HWND h = xt_window(); CHECK(h);
    SET_ERR(SENT); ENL(h, 1); EXPECT_ERR(SENT);
    SET_ERR(SENT); ISL(h); EXPECT_ERR(SENT);
    ENL(h, 0);
    xt_destroy(h);
}
XT(flag_does_not_survive_the_window) {
    NEED_LAYOUT;
    HWND h = xt_window(); CHECK(h);
    ENL(h, 1);
    xt_destroy(h);
    SET_ERR(SENT);
    EXPECT_EQ(ISL(h), 0);
    HWND n = xt_window(); CHECK(n);
    EXPECT_EQ(ISL(n), 0);                        /* a later window, even one that reuses the slot, starts clear */
    xt_destroy(n);
}
XT(enable_on_a_destroyed_window_fails_with_invalid_window_handle) {
    NEED_LAYOUT;
    HWND h = xt_window(); CHECK(h);
    xt_destroy(h);
    SET_ERR(SENT);
    EXPECT_EQ(ENL(h, 1), 0);
    EXPECT_ERR(E_INVALID_HWND);
}

/* ---- BeginLayoutUpdate / LayoutCompleted ---- */
static void job_layout(struct xt_wk *w, void *p) {
    struct la *a = p;
    SET_ERR(SENT);
    a->v[0] = R_B(UN(NtUserBeginLayoutUpdate)(w->w[0])); a->v[1] = GetLastError();
    a->v[2] = R_B(UN(NtUserLayoutCompleted)(w->w[0])); a->v[3] = GetLastError();
    a->v[4] = ISL(w->w[0]);
}
XT(begin_layout_update_and_layout_completed_are_true_on_a_worker_window) {
    NEED(NtUserBeginLayoutUpdate); NEED(NtUserLayoutCompleted); NEED_LAYOUT;
    struct xt_wk w; struct la a;
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_layout, &a));
    EXPECT_EQ(a.v[0], 1); EXPECT_EQ(a.v[1], SENT);
    EXPECT_EQ(a.v[2], 1); EXPECT_EQ(a.v[3], SENT);
    xt_wk_stop(&w);
}
XT(layout_notifications_do_not_set_the_resize_flag) {
    NEED(NtUserBeginLayoutUpdate); NEED(NtUserLayoutCompleted); NEED_LAYOUT;
    struct xt_wk w; struct la a;
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_layout, &a));
    EXPECT_EQ(a.v[4], 0);
    xt_wk_stop(&w);
}
static void job_layout_foreign(struct xt_wk *w, void *p) {
    struct la *a = p; HWND f = a->h[0];
    SET_ERR(SENT); a->v[0] = R_B(UN(NtUserBeginLayoutUpdate)(f)); a->v[1] = GetLastError();
    SET_ERR(SENT); a->v[2] = R_B(UN(NtUserLayoutCompleted)(f)); a->v[3] = GetLastError();
}
XT(layout_notifications_on_a_window_of_another_thread_are_true) {
    NEED(NtUserBeginLayoutUpdate); NEED(NtUserLayoutCompleted);
    struct xt_wk w; struct la a; memset(&a, 0, sizeof a);
    HWND m = xt_window(); CHECK(m);
    CHECK(xt_wk_start(&w, 1));
    a.h[0] = m;
    CHECK(xt_wk_run(&w, job_layout_foreign, &a));
    EXPECT_EQ(a.v[0], 1); EXPECT_EQ(a.v[2], 1);
    EXPECT_EQ(a.v[1], SENT); EXPECT_EQ(a.v[3], SENT);
    xt_wk_stop(&w);
    xt_destroy(m);
}
XT(layout_notifications_from_the_main_thread_on_a_worker_window_are_true) {
    NEED(NtUserBeginLayoutUpdate); NEED(NtUserLayoutCompleted);
    struct xt_wk w;
    CHECK(xt_wk_start(&w, 1));
    SET_ERR(SENT);
    DWORD b = R_B(UN(NtUserBeginLayoutUpdate)(w.w[0])), eb = GetLastError();
    SET_ERR(SENT);
    DWORD c = R_B(UN(NtUserLayoutCompleted)(w.w[0])), ec = GetLastError();
    EXPECT_EQ(b, 1); EXPECT_EQ(c, 1);
    EXPECT_EQ(eb, SENT); EXPECT_EQ(ec, SENT);
    xt_wk_stop(&w);
}
XT(layout_notifications_on_stale_and_invalid_windows_fail_with_invalid_window_handle) {
    NEED(NtUserBeginLayoutUpdate); NEED(NtUserLayoutCompleted);
    HWND h = xt_window(); CHECK(h);
    xt_destroy(h);
    HWND bad[] = { NULL, h, (HWND)(ULONG_PTR)0x7FFF0010, (HWND)(ULONG_PTR)0xFFFFFFFF };
    int b1 = 0, b2 = 0, e1 = 0, e2 = 0;
    for (int i = 0; i < 4; i++) {
        SET_ERR(SENT); if (R_B(UN(NtUserBeginLayoutUpdate)(bad[i]))) b1++; if (GetLastError() != E_INVALID_HWND) e1++;
        SET_ERR(SENT); if (R_B(UN(NtUserLayoutCompleted)(bad[i]))) b2++; if (GetLastError() != E_INVALID_HWND) e2++;
    }
    EXPECT_EQ(b1, 0); EXPECT_EQ(e1, 0); EXPECT_EQ(b2, 0); EXPECT_EQ(e2, 0);
}
XT(layout_notifications_are_true_for_a_child_window) {
    NEED(NtUserBeginLayoutUpdate); NEED(NtUserLayoutCompleted);
    HWND p = xt_window(), c; CHECK(p);
    c = xt_child_window(p); CHECK(c);
    EXPECT_EQ(R_B(UN(NtUserBeginLayoutUpdate)(c)), 1);
    EXPECT_EQ(R_B(UN(NtUserLayoutCompleted)(c)), 1);
    xt_destroy(p);
}
XT(layout_notifications_are_refused_for_the_desktop_window) {
    NEED(NtUserBeginLayoutUpdate); NEED(NtUserLayoutCompleted);
    HWND d = U(GetDesktopWindow)();
    EXPECT_EQ(R_B(UN(NtUserBeginLayoutUpdate)(d)), 0);
    EXPECT_EQ(R_B(UN(NtUserLayoutCompleted)(d)), 1);
}

/* ---- ConfirmResizeCommit (compositor only) ---- */
static void job_confirm(struct xt_wk *w, void *p) {
    struct la *a = p;
    SET_ERR(SENT);
    a->v[0] = R_B(UN(NtUserConfirmResizeCommit)(w->w[0])); a->v[1] = GetLastError();
    a->v[2] = R_B(UN(NtUserConfirmResizeCommit)(NULL));
}
XT(confirm_resize_commit_is_refused_on_a_worker) {
    NEED(NtUserConfirmResizeCommit);
    struct xt_wk w; struct la a;
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_confirm, &a));
    EXPECT_EQ(a.v[0], 0); EXPECT_EQ(a.v[2], 0);
    EXPECT_EQ(a.v[1], SENT);                    /* refusal does not touch the last error */
    xt_wk_stop(&w);
}
XT(confirm_resize_commit_is_refused_on_the_main_thread) {
    NEED(NtUserConfirmResizeCommit);
    HWND h = xt_window(); CHECK(h);
    EXPECT_EQ(R_B(UN(NtUserConfirmResizeCommit)(h)), 0);
    EXPECT_EQ(R_B(UN(NtUserConfirmResizeCommit)(NULL)), 0);
    EXPECT_EQ(R_B(UN(NtUserConfirmResizeCommit)((HWND)(ULONG_PTR)0x7FFF0010)), 0);
    xt_destroy(h);
}
XT(confirm_resize_commit_is_refused_for_the_desktop_window) {
    NEED(NtUserConfirmResizeCommit);
    EXPECT_EQ(R_B(UN(NtUserConfirmResizeCommit)(U(GetDesktopWindow)())), 0);
}
XT(confirm_resize_commit_leaves_the_flag_alone) {
    NEED(NtUserConfirmResizeCommit); NEED_LAYOUT;
    HWND h = xt_window(); CHECK(h);
    ENL(h, 1);
    UN(NtUserConfirmResizeCommit)(h);
    EXPECT_EQ(ISL(h), 0x80);
    ENL(h, 0);
    UN(NtUserConfirmResizeCommit)(h);
    EXPECT_EQ(ISL(h), 0);
    xt_destroy(h);
}

/* ---- in a child process ---- */
XC(layout_child) {
    if (!UN(NtUserEnableResizeLayoutSynchronization) || !UN(NtUserIsResizeLayoutSynchronizationEnabled)) return 0;
    HWND own = xt_window();
    CK(own != NULL);
    HWND parent_w = (HWND)(ULONG_PTR)s->v[0];
    CK_EQ(ISL(own), 0);                          /* the parent's flag on its window is not the child's */
    CK_EQ(ENL(own, 1), 1);
    CK_EQ(ISL(own), 0x80);
    s->v[2] = ISL(parent_w);                     /* the parent window's flag, read from another process */
    SET_ERR(SENT);
    s->v[3] = ENL(parent_w, 1); s->v[4] = GetLastError();
    s->v[5] = ISL(parent_w);
    xt_destroy(own);
    s->done = 1;
    return 0;
}
static void run_layout_child(HWND h, int parent_flag, struct xt_shm **ps, HANDLE *hm) {
    const char *tag = xt_tag();
    struct xt_shm *s = xt_shm_create(tag, hm); CHECK(s);
    ENL(h, parent_flag);
    s->v[0] = (LONG64)(ULONG_PTR)h;
    PROCESS_INFORMATION pi; HANDLE pr = xt_spawn("layout_child", tag, NULL, &pi); CHECK(pr);
    EXPECT_EQ(pr ? xt_reap(pr, &pi, 60000) : 1, 0);
    *ps = s;
}
XT(child_process_sees_the_flag_of_a_parent_window_and_its_own_window_is_separate) {
    NEED_LAYOUT;
    HANDLE hm; struct xt_shm *s; HWND h = xt_window(); CHECK(h);
    run_layout_child(h, 1, &s, &hm);
    EXPECT_EQ((DWORD)s->v[2], 0x80);
    EXPECT_EQ(ISL(h), 0x80);
    ENL(h, 0);
    xt_destroy(h);
    UnmapViewOfFile((void *)s); CloseHandle(hm);
}
XT(child_process_may_set_the_flag_of_a_parent_window) {
    NEED_LAYOUT;
    HANDLE hm; struct xt_shm *s; HWND h = xt_window(); CHECK(h);
    run_layout_child(h, 0, &s, &hm);
    EXPECT_EQ((DWORD)s->v[2], 0);
    EXPECT_EQ((DWORD)s->v[3], 1);
    EXPECT_EQ((DWORD)s->v[4], SENT);
    EXPECT_EQ((DWORD)s->v[5], 0x80);
    EXPECT_EQ(ISL(h), 0x80);                       /* the flag is the window's: it outlives the process that set it */
    ENL(h, 0);
    xt_destroy(h);
    UnmapViewOfFile((void *)s); CloseHandle(hm);
}
