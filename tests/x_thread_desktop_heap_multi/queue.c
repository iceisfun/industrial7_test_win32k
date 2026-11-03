/* x_thread_desktop_heap_multi / queue: the message-queue state is per thread.
 *
 * NtUserGetQueueStatusReadonly(flags) answers {change bits, wake bits} of the CALLING thread's queue masked by flags and
 * consumes nothing; GetQueueStatus answers the same and clears the change bits it was asked about. A message posted to one
 * thread sets QS_POSTMESSAGE (wake 0x8 and change 0x8) on that thread only; another thread, the poster included, sees
 * nothing. A thread created later starts with an empty queue whatever its creator holds; a thread that never entered
 * win32k has no queue and cannot be posted to; once a thread is gone posting to it fails. Each job runs on a worker thread
 * that waits on an event between jobs, so which thread sees what is decided by the order of the events, never by sleeping.
 */
#include "x.h"

#define POST (QS_POSTMESSAGE)
#define BOTH_POST 0x00080008u

/* ---- jobs ---- */
struct qa { DWORD v[8]; DWORD tid_other; HWND wnd; LONG n; };

static void job_ro_post(struct xt_wk *w, void *p) { struct qa *a = p; a->v[0] = RO(POST); a->v[1] = RO(0xFFFF) & 0x00FF00FF; }
static void job_self_post(struct xt_wk *w, void *p) {
    struct qa *a = p;
    xt_drain();
    a->v[0] = U(PostThreadMessageW)(GetCurrentThreadId(), WM_APP + 1, 11, 22);
    a->v[1] = RO(POST);
}
static void job_get_qs(struct xt_wk *w, void *p) { struct qa *a = p; a->v[0] = UN(NtUserGetQueueStatus)(POST); a->v[1] = RO(POST); }
static void job_fresh_bits(struct xt_wk *w, void *p) {
    struct qa *a = p;
    a->v[0] = RO(QS_POSTMESSAGE | QS_SENDMESSAGE | QS_HOTKEY | QS_KEY);
    a->v[1] = RO(0);
}
static void job_remove_one(struct xt_wk *w, void *p) {
    struct qa *a = p; MSG m;
    a->v[0] = U(PeekMessageW)(&m, NULL, WM_APP + 1, WM_APP + 1, PM_REMOVE);
    a->v[1] = m.message; a->v[2] = (DWORD)m.wParam; a->v[3] = (DWORD)m.lParam;
    a->v[4] = RO(POST);
}
static void job_count_msgs(struct xt_wk *w, void *p) {
    struct qa *a = p; MSG m; a->n = 0;
    while (U(PeekMessageW)(&m, NULL, WM_APP, WM_APP + 100, PM_REMOVE)) a->n++;
}

/* ---- fresh threads ---- */
static void fresh_thread_fn(void *p) {
    DWORD *r = p;
    r[0] = RO(QS_POSTMESSAGE | QS_SENDMESSAGE | QS_HOTKEY | QS_KEY);
    r[1] = RO(0);
}
XT(fresh_thread_has_no_post_send_hotkey_key_bits) {
    NEED(NtUserGetQueueStatusReadonly);
    DWORD r[2] = { 1, 1 };
    CHECK(xt_on_thread(fresh_thread_fn, r));
    EXPECT_EQ(r[0], 0);
}
XT(fresh_thread_readonly_with_no_flags_is_zero) {
    NEED(NtUserGetQueueStatusReadonly);
    DWORD r[2] = { 1, 1 };
    CHECK(xt_on_thread(fresh_thread_fn, r));
    EXPECT_EQ(r[1], 0);
}
XT(worker_with_windows_has_no_post_bits) {
    NEED(NtUserGetQueueStatusReadonly);
    struct xt_wk w; struct qa a = { { 7, 7 } };
    CHECK(xt_wk_start(&w, 2));
    CHECK(xt_wk_run(&w, job_fresh_bits, &a));
    EXPECT_EQ(a.v[0], 0);
    xt_wk_stop(&w);
}

/* ---- posting ---- */
XT(post_thread_message_sets_both_post_bits_on_target) {
    NEED(NtUserGetQueueStatusReadonly);
    struct xt_wk w; struct qa a;
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_drain, NULL));
    CHECK(U(PostThreadMessageW)(w.tid, WM_APP + 1, 1, 2));
    CHECK(xt_wk_run(&w, job_ro_post, &a));
    EXPECT_EQ(a.v[0], BOTH_POST);
    xt_wk_stop(&w);
}
XT(post_thread_message_leaves_the_poster_queue_alone) {
    NEED(NtUserGetQueueStatusReadonly);
    struct xt_wk w;
    xt_drain();
    CHECK(xt_wk_start(&w, 1));
    CHECK(U(PostThreadMessageW)(w.tid, WM_APP + 1, 1, 2));
    EXPECT_EQ(RO(POST), 0);
    EXPECT_EQ(RO(0xFFFF) & QS_POSTMESSAGE, 0);
    xt_wk_stop(&w);
}
XT(post_to_one_worker_is_invisible_to_the_other) {
    NEED(NtUserGetQueueStatusReadonly);
    struct xt_wk a, b; struct qa ra, rb;
    CHECK(xt_wk_start(&a, 1)); CHECK(xt_wk_start(&b, 1));
    CHECK(xt_wk_run(&a, job_drain, NULL)); CHECK(xt_wk_run(&b, job_drain, NULL));
    CHECK(U(PostThreadMessageW)(a.tid, WM_APP + 1, 0, 0));
    CHECK(xt_wk_run(&a, job_ro_post, &ra)); CHECK(xt_wk_run(&b, job_ro_post, &rb));
    EXPECT_EQ(ra.v[0], BOTH_POST);
    EXPECT_EQ(rb.v[0], 0);
    xt_wk_stop(&a); xt_wk_stop(&b);
}
XT(post_to_both_workers_each_sees_its_own_bits) {
    NEED(NtUserGetQueueStatusReadonly);
    struct xt_wk a, b; struct qa ra, rb;
    CHECK(xt_wk_start(&a, 1)); CHECK(xt_wk_start(&b, 1));
    CHECK(xt_wk_run(&a, job_drain, NULL)); CHECK(xt_wk_run(&b, job_drain, NULL));
    CHECK(U(PostThreadMessageW)(a.tid, WM_APP + 1, 0, 0));
    CHECK(U(PostThreadMessageW)(b.tid, WM_APP + 2, 0, 0));
    CHECK(xt_wk_run(&a, job_ro_post, &ra)); CHECK(xt_wk_run(&b, job_ro_post, &rb));
    EXPECT_EQ(ra.v[0], BOTH_POST);
    EXPECT_EQ(rb.v[0], BOTH_POST);
    xt_wk_stop(&a); xt_wk_stop(&b);
}
XT(consuming_change_bits_on_one_thread_keeps_the_other_thread_bits) {
    NEED(NtUserGetQueueStatusReadonly);
    struct xt_wk a, b; struct qa ra, rb;
    CHECK(xt_wk_start(&a, 1)); CHECK(xt_wk_start(&b, 1));
    CHECK(xt_wk_run(&a, job_drain, NULL)); CHECK(xt_wk_run(&b, job_drain, NULL));
    CHECK(U(PostThreadMessageW)(a.tid, WM_APP + 1, 0, 0));
    CHECK(U(PostThreadMessageW)(b.tid, WM_APP + 1, 0, 0));
    CHECK(xt_wk_run(&a, job_get_qs, &ra));     /* A: GetQueueStatus clears A's change bit only */
    CHECK(xt_wk_run(&b, job_ro_post, &rb));
    EXPECT_EQ(ra.v[0], BOTH_POST);
    EXPECT_EQ(ra.v[1], 0x00080000);
    EXPECT_EQ(rb.v[0], BOTH_POST);
    xt_wk_stop(&a); xt_wk_stop(&b);
}
XT(getqueuestatus_on_the_main_thread_does_not_touch_a_worker) {
    NEED(NtUserGetQueueStatusReadonly);
    struct xt_wk w; struct qa a;
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_drain, NULL));
    CHECK(U(PostThreadMessageW)(w.tid, WM_APP + 1, 0, 0));
    UN(NtUserGetQueueStatus)(0x5DFF);
    CHECK(xt_wk_run(&w, job_ro_post, &a));
    EXPECT_EQ(a.v[0], BOTH_POST);
    xt_wk_stop(&w);
}
XT(self_post_is_seen_by_self_only) {
    NEED(NtUserGetQueueStatusReadonly);
    struct xt_wk w; struct qa a;
    xt_drain();
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_self_post, &a));
    EXPECT_EQ(a.v[0], 1);
    EXPECT_EQ(a.v[1], BOTH_POST);
    EXPECT_EQ(RO(POST), 0);
    xt_wk_stop(&w);
}
XT(readonly_on_main_sees_main_posts_only) {
    NEED(NtUserGetQueueStatusReadonly);
    struct xt_wk w; struct qa a;
    xt_drain();
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_drain, NULL));
    CHECK(U(PostThreadMessageW)(GetCurrentThreadId(), WM_APP + 1, 0, 0));
    EXPECT_EQ(RO(POST), BOTH_POST);
    CHECK(xt_wk_run(&w, job_ro_post, &a));
    EXPECT_EQ(a.v[0], 0);                     /* the worker sees nothing of the main thread's post */
    xt_drain();
    xt_wk_stop(&w);
}
XT(readonly_answers_do_not_consume_across_threads) {
    NEED(NtUserGetQueueStatusReadonly);
    struct xt_wk w; struct qa a, b;
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_drain, NULL));
    CHECK(U(PostThreadMessageW)(w.tid, WM_APP + 1, 0, 0));
    CHECK(xt_wk_run(&w, job_ro_post, &a));
    CHECK(xt_wk_run(&w, job_ro_post, &b));
    EXPECT_EQ(a.v[0], BOTH_POST);
    EXPECT_EQ(b.v[0], BOTH_POST);
    xt_wk_stop(&w);
}
XT(thread_message_params_arrive_intact_on_the_target) {
    NEED(NtUserGetQueueStatusReadonly);
    struct xt_wk w; struct qa a;
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_drain, NULL));
    CHECK(U(PostThreadMessageW)(w.tid, WM_APP + 1, 0x1234, 0x5678));
    CHECK(xt_wk_run(&w, job_remove_one, &a));
    EXPECT_EQ(a.v[0], 1);
    EXPECT_EQ(a.v[1], WM_APP + 1);
    EXPECT_EQ(a.v[2], 0x1234);
    EXPECT_EQ(a.v[3], 0x5678);
    EXPECT_EQ(HI(a.v[4]) & QS_POSTMESSAGE, 0);
    xt_wk_stop(&w);
}
XT(wake_bit_stays_until_the_last_posted_message_is_removed) {
    NEED(NtUserGetQueueStatusReadonly);
    struct xt_wk w; struct qa a;
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_drain, NULL));
    CHECK(U(PostThreadMessageW)(w.tid, WM_APP + 1, 1, 0));
    CHECK(U(PostThreadMessageW)(w.tid, WM_APP + 1, 2, 0));
    CHECK(U(PostThreadMessageW)(w.tid, WM_APP + 1, 3, 0));
    CHECK(xt_wk_run(&w, job_remove_one, &a));
    EXPECT_EQ(a.v[2], 1);
    EXPECT_EQ(HI(a.v[4]) & QS_POSTMESSAGE, QS_POSTMESSAGE);
    CHECK(xt_wk_run(&w, job_remove_one, &a));
    EXPECT_EQ(a.v[2], 2);
    EXPECT_EQ(HI(a.v[4]) & QS_POSTMESSAGE, QS_POSTMESSAGE);
    CHECK(xt_wk_run(&w, job_remove_one, &a));
    EXPECT_EQ(a.v[2], 3);
    EXPECT_EQ(HI(a.v[4]) & QS_POSTMESSAGE, 0);
    xt_wk_stop(&w);
}
XT(bounded_burst_of_posts_all_arrive_on_the_target_thread) {
    NEED(NtUserGetQueueStatusReadonly);
    struct xt_wk w; struct qa a;
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_drain, NULL));
    for (int i = 0; i < 64; i++) CHECK(U(PostThreadMessageW)(w.tid, WM_APP + 1 + (i % 4), i, 0));
    CHECK(xt_wk_run(&w, job_count_msgs, &a));
    EXPECT_EQ(a.n, 64);
    xt_wk_stop(&w);
}
XT(post_to_a_window_goes_to_the_owner_thread_queue) {
    NEED(NtUserGetQueueStatusReadonly);
    struct xt_wk w; struct qa a;
    xt_drain();
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_drain, NULL));
    CHECK(U(PostMessageW)(w.w[0], WM_APP + 1, 0, 0));
    EXPECT_EQ(RO(POST), 0);
    CHECK(xt_wk_run(&w, job_ro_post, &a));
    EXPECT_EQ(a.v[0], BOTH_POST);
    xt_wk_stop(&w);
}
XT(posted_mouse_message_is_a_post_not_input) {
    NEED(NtUserGetQueueStatusReadonly);
    struct xt_wk w; struct qa a;
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_drain, NULL));
    CHECK(U(PostMessageW)(w.w[0], WM_MOUSEMOVE, 0, 0));
    CHECK(U(PostMessageW)(w.w[0], WM_KEYDOWN, 'A', 0));
    CHECK(xt_wk_run(&w, job_ro_post, &a));
    EXPECT_EQ(a.v[0], BOTH_POST);
    EXPECT_EQ(RO(QS_MOUSEMOVE | QS_KEY | QS_MOUSEBUTTON), 0);
    xt_wk_stop(&w);
}

/* ---- threads without a queue / gone ---- */
static DWORD WINAPI idle_main(LPVOID p) { HANDLE *e = p; SetEvent(e[0]); WaitForSingleObject(e[1], 30000); return 0; }
XT(thread_that_never_entered_win32k_cannot_be_posted_to) {
    NEED(NtUserGetQueueStatusReadonly);
    HANDLE e[2] = { CreateEventA(NULL, TRUE, FALSE, NULL), CreateEventA(NULL, TRUE, FALSE, NULL) };
    DWORD tid = 0; HANDLE th = CreateThread(NULL, 0, idle_main, e, 0, &tid);
    CHECK(th && WaitForSingleObject(e[0], 10000) == WAIT_OBJECT_0);
    SET_ERR(SENT);
    CHECK(!U(PostThreadMessageW)(tid, WM_APP + 1, 0, 0));
    EXPECT_ERR(E_INVALID_THREAD_ID);
    SetEvent(e[1]); WaitForSingleObject(th, 10000);
    CloseHandle(th); CloseHandle(e[0]); CloseHandle(e[1]);
}
static DWORD WINAPI make_queue_main(LPVOID p) {
    HANDLE *e = p; MSG m;
    t_module("user32.dll");
    U(PeekMessageW)(&m, NULL, 0, 0, PM_NOREMOVE);       /* the first call creates the queue */
    SetEvent(e[0]); WaitForSingleObject(e[1], 30000); return 0;
}
XT(thread_becomes_postable_after_its_first_queue_call) {
    NEED(NtUserGetQueueStatusReadonly);
    HANDLE e[2] = { CreateEventA(NULL, TRUE, FALSE, NULL), CreateEventA(NULL, TRUE, FALSE, NULL) };
    DWORD tid = 0; HANDLE th = CreateThread(NULL, 0, make_queue_main, e, 0, &tid);
    CHECK(th && WaitForSingleObject(e[0], 10000) == WAIT_OBJECT_0);
    CHECK(U(PostThreadMessageW)(tid, WM_APP + 1, 0, 0));
    SetEvent(e[1]); WaitForSingleObject(th, 10000);
    CloseHandle(th); CloseHandle(e[0]); CloseHandle(e[1]);
}
XT(post_to_a_finished_thread_fails_with_invalid_thread_id) {
    NEED(NtUserGetQueueStatusReadonly);
    struct xt_wk w; DWORD tid;
    CHECK(xt_wk_start(&w, 1));
    tid = w.tid;
    CHECK(U(PostThreadMessageW)(tid, WM_APP + 1, 0, 0));
    xt_wk_stop(&w);
    SET_ERR(SENT);
    CHECK(!U(PostThreadMessageW)(tid, WM_APP + 1, 0, 0));
    EXPECT_ERR(E_INVALID_THREAD_ID);
}
XT(post_to_a_window_of_a_finished_thread_fails_with_invalid_window) {
    NEED(NtUserGetQueueStatusReadonly);
    struct xt_wk w; HWND h;
    CHECK(xt_wk_start(&w, 1));
    h = w.w[0];
    CHECK(U(IsWindow)(h));
    xt_wk_stop(&w);
    int gone = 0; for (int i = 0; i < 500 && !(gone = !U(IsWindow)(h)); i++) Sleep(10);
    CHECK(gone);
    SET_ERR(SENT);
    CHECK(!U(PostMessageW)(h, WM_APP + 1, 0, 0));
    EXPECT_ERR(E_INVALID_HWND);
}

/* ---- not inherited ---- */
struct inh { DWORD child_bits; DWORD parent_bits; };
static void inh_child(void *p) { struct inh *i = p; i->child_bits = RO(0xFFFF) & 0x00FF00FF & ~(QS_PAINT | (QS_PAINT << 16)); }
static void job_spawn_inherit(struct xt_wk *w, void *p) {
    struct inh *i = p;
    xt_drain();
    U(PostThreadMessageW)(GetCurrentThreadId(), WM_APP + 1, 0, 0);
    i->parent_bits = RO(POST);
    xt_on_thread(inh_child, i);
}
XT(new_thread_does_not_inherit_its_creators_pending_posts) {
    NEED(NtUserGetQueueStatusReadonly);
    struct xt_wk w; struct inh i = { 0xFFFF, 0 };
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_spawn_inherit, &i));
    EXPECT_EQ(i.parent_bits, BOTH_POST);
    EXPECT_EQ(i.child_bits, 0);
    xt_wk_stop(&w);
}
XT(new_thread_does_not_inherit_the_main_threads_pending_posts) {
    NEED(NtUserGetQueueStatusReadonly);
    struct inh i = { 0xFFFF, 0 };
    xt_drain();
    CHECK(U(PostThreadMessageW)(GetCurrentThreadId(), WM_APP + 1, 0, 0));
    i.parent_bits = RO(POST);
    CHECK(xt_on_thread(inh_child, &i));
    EXPECT_EQ(i.parent_bits, BOTH_POST);
    EXPECT_EQ(i.child_bits, 0);
    xt_drain();
}
XT(sequential_fresh_threads_each_start_empty) {
    NEED(NtUserGetQueueStatusReadonly);
    int bad = 0;
    xt_drain();
    CHECK(U(PostThreadMessageW)(GetCurrentThreadId(), WM_APP + 1, 0, 0));
    for (int k = 0; k < 4; k++) {
        DWORD r[2] = { 1, 1 };
        if (!xt_on_thread(fresh_thread_fn, r) || r[0] != 0) bad++;
    }
    EXPECT_EQ(bad, 0);
    xt_drain();
}

/* ---- sent messages and paint ---- */
static void job_sent_bits(struct xt_wk *w, void *p) {
    struct qa *a = p; MSG m;
    a->v[0] = RO(QS_SENDMESSAGE);
    U(PeekMessageW)(&m, NULL, 0, 0, PM_NOREMOVE);          /* delivers the sent message */
    a->v[1] = RO(QS_SENDMESSAGE);
}
XT(sent_notify_message_sets_send_wake_bit_on_the_target_only) {
    NEED(NtUserGetQueueStatusReadonly);
    struct xt_wk w; struct qa a;
    xt_drain();
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_drain, NULL));
    CHECK(U(SendNotifyMessageW)(w.w[0], WM_APP + 1, 0, 0));
    EXPECT_EQ(HI(RO(QS_SENDMESSAGE)) & QS_SENDMESSAGE, 0);
    CHECK(xt_wk_run(&w, job_sent_bits, &a));
    EXPECT_EQ(HI(a.v[0]) & QS_SENDMESSAGE, QS_SENDMESSAGE);
    EXPECT_EQ(HI(a.v[1]) & QS_SENDMESSAGE, 0);
    xt_wk_stop(&w);
}
static void job_paint_bits(struct xt_wk *w, void *p) {
    struct qa *a = p;
    U(UpdateWindow)(w->w[0]);
    a->v[0] = HI(RO(QS_PAINT)) & QS_PAINT;
    U(InvalidateRect)(w->w[0], NULL, FALSE);
    a->v[1] = HI(RO(QS_PAINT)) & QS_PAINT;
    U(UpdateWindow)(w->w[0]);
    a->v[2] = HI(RO(QS_PAINT)) & QS_PAINT;
}
XT(paint_wake_bit_follows_the_update_region_of_the_threads_own_windows) {
    NEED(NtUserGetQueueStatusReadonly);
    struct xt_wk w; struct qa a;
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_paint_bits, &a));
    EXPECT_EQ(a.v[0], 0);
    EXPECT_EQ(a.v[1], QS_PAINT);
    EXPECT_EQ(a.v[2], 0);
    xt_wk_stop(&w);
}
XT(paint_wake_bit_of_a_worker_window_is_not_the_main_threads) {
    NEED(NtUserGetQueueStatusReadonly);
    struct xt_wk w;
    HWND m = xt_window();
    CHECK(m);
    U(UpdateWindow)(m);
    xt_drain();
    CHECK(xt_wk_start(&w, 1));
    EXPECT_EQ(HI(RO(QS_PAINT)) & QS_PAINT, 0);    /* the worker's unpainted window is the worker's business */
    xt_wk_stop(&w);
    xt_destroy(m);
}
static void job_quit(struct xt_wk *w, void *p) {
    struct qa *a = p; MSG m;
    U(PostQuitMessage)(7);
    a->v[0] = U(PeekMessageW)(&m, NULL, WM_QUIT, WM_QUIT, PM_REMOVE);
    a->v[1] = (DWORD)m.wParam;
    a->v[2] = U(PeekMessageW)(&m, NULL, WM_QUIT, WM_QUIT, PM_REMOVE);
}
XT(post_quit_message_is_per_thread) {
    NEED(NtUserGetQueueStatusReadonly);
    struct xt_wk w; struct qa a; MSG m;
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_quit, &a));
    EXPECT_EQ(a.v[0], 1);
    EXPECT_EQ(a.v[1], 7);
    EXPECT_EQ(a.v[2], 0);                                           /* WM_QUIT is delivered once */
    CHECK(!U(PeekMessageW)(&m, NULL, WM_QUIT, WM_QUIT, PM_REMOVE));  /* ... and only on the posting thread */
    xt_wk_stop(&w);
}
