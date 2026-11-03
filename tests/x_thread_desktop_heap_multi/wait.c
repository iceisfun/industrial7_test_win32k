/* x_thread_desktop_heap_multi / wait: NtUserRealWaitMessageEx and NtUserRealInternalGetMessage act on the calling thread's queue.
 *
 * RealWaitMessageEx(mask, ms) returns 1 as soon as one of the mask's CHANGE bits of the caller's queue is set (a message posted
 * to another thread never wakes it) and otherwise times out with 0 and ERROR_TIMEOUT; a mask with bits in 0xffffa200 is refused at
 * once with ERROR_INVALID_FLAGS. RealInternalGetMessage fills a MSG from the caller's own queue like PeekMessage; flags with bits
 * in 0xe300fffc are refused with ERROR_INVALID_FLAGS. The handshake with the poster is done with events; a wait that must
 * time out uses a short bounded timeout and asserts only the outcome.
 */
#include "x.h"

struct wa { DWORD r[8]; DWORD gle[8]; MSG m; HANDLE ev; };

static void job_rwm_timeout(struct xt_wk *w, void *p) {
    struct wa *a = p;
    xt_drain();
    SET_ERR(SENT); a->r[0] = UN(NtUserRealWaitMessageEx)(QS_POSTMESSAGE, 30); a->gle[0] = GetLastError();
    SET_ERR(SENT); a->r[1] = UN(NtUserRealWaitMessageEx)(QS_ALLINPUT & 0x5DFF, 30); a->gle[1] = GetLastError();
}
static void job_rwm_wait_for_post(struct xt_wk *w, void *p) {
    struct wa *a = p;
    xt_drain();
    SetEvent(w->sig);                              /* tell the main thread it may post now */
    SET_ERR(SENT); a->r[0] = UN(NtUserRealWaitMessageEx)(QS_POSTMESSAGE, 20000); a->gle[0] = GetLastError();
}
static void job_rwm_bad_mask(struct xt_wk *w, void *p) {
    struct wa *a = p;
    static const DWORD bad[] = { 0x200, 0x2000, 0x8000, 0x10000, 0x80000000u, 0xFFFFFFFFu };
    for (int i = 0; i < 6; i++) { SET_ERR(SENT); a->r[i] = UN(NtUserRealWaitMessageEx)(bad[i], 5000); a->gle[i] = GetLastError(); }
}
XT(wait_times_out_on_an_idle_worker) {
    NEED(NtUserRealWaitMessageEx);
    struct xt_wk w; struct wa a;
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_rwm_timeout, &a));
    EXPECT_EQ(a.r[0], 0); EXPECT_EQ(a.gle[0], E_TIMEOUT);
    xt_wk_stop(&w);
}
XT(wait_with_the_full_valid_mask_times_out_on_an_idle_worker) {
    NEED(NtUserRealWaitMessageEx);
    struct xt_wk w; struct wa a;
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_rwm_timeout, &a));
    EXPECT_EQ(a.r[1], 0); EXPECT_EQ(a.gle[1], E_TIMEOUT);
    xt_wk_stop(&w);
}
XT(post_from_another_thread_wakes_the_waiting_worker) {
    NEED(NtUserRealWaitMessageEx);
    struct xt_wk w; struct wa a;
    CHECK(xt_wk_start(&w, 1));
    xt_wk_go(&w, job_rwm_wait_for_post, &a);
    CHECK(xt_wk_sig(&w, 10000));
    CHECK(U(PostThreadMessageW)(w.tid, WM_APP + 1, 0, 0));
    CHECK(xt_wk_wait(&w, 25000));
    EXPECT_EQ(a.r[0], 1);
    xt_wk_stop(&w);
}
XT(post_to_the_main_thread_does_not_wake_the_waiting_worker) {
    NEED(NtUserRealWaitMessageEx);
    struct xt_wk w; struct wa a;
    xt_drain();
    CHECK(xt_wk_start(&w, 1));
    CHECK(U(PostThreadMessageW)(GetCurrentThreadId(), WM_APP + 1, 0, 0));
    CHECK(xt_wk_run(&w, job_rwm_timeout, &a));
    EXPECT_EQ(a.r[0], 0); EXPECT_EQ(a.gle[0], E_TIMEOUT);
    xt_drain();
    xt_wk_stop(&w);
}
XT(post_to_the_other_worker_does_not_wake_this_worker) {
    NEED(NtUserRealWaitMessageEx);
    struct xt_wk a, b; struct wa ra;
    CHECK(xt_wk_start(&a, 1)); CHECK(xt_wk_start(&b, 1));
    CHECK(xt_wk_run(&b, job_rwm_timeout, &ra));          /* consume B's start-up bits */
    CHECK(U(PostThreadMessageW)(b.tid, WM_APP + 1, 0, 0));
    CHECK(xt_wk_run(&a, job_rwm_timeout, &ra));
    EXPECT_EQ(ra.r[0], 0); EXPECT_EQ(ra.gle[0], E_TIMEOUT);
    xt_wk_stop(&a); xt_wk_stop(&b);
}
XT(invalid_wait_masks_are_refused_at_once_on_a_worker) {
    NEED(NtUserRealWaitMessageEx);
    struct xt_wk w; struct wa a;
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_rwm_bad_mask, &a));
    for (int i = 0; i < 6; i++) { EXPECT_EQ(a.r[i], 0); EXPECT_EQ(a.gle[i], E_INVALID_FLAGS); }
    xt_wk_stop(&w);
}
static void fresh_bad_mask(void *p) {
    DWORD *r = p;
    SET_ERR(SENT); r[0] = UN(NtUserRealWaitMessageEx)(0x8000, 5000); r[1] = GetLastError();
}
XT(invalid_wait_mask_is_refused_on_a_fresh_thread_too) {
    NEED(NtUserRealWaitMessageEx);
    DWORD r[2] = { 9, 9 };
    CHECK(xt_on_thread(fresh_bad_mask, r));
    EXPECT_EQ(r[0], 0); EXPECT_EQ(r[1], E_INVALID_FLAGS);
}
static void job_wait_pending(struct xt_wk *w, void *p) {
    struct wa *a = p;
    xt_drain();
    U(PostThreadMessageW)(GetCurrentThreadId(), WM_APP + 1, 0, 0);
    a->r[0] = UN(NtUserRealWaitMessageEx)(QS_POSTMESSAGE, 20000);
    a->r[1] = UN(NtUserRealWaitMessageEx)(QS_POSTMESSAGE, 20000);       /* waiting consumes no change bit */
    UN(NtUserGetQueueStatus)(QS_POSTMESSAGE);
    SET_ERR(SENT); a->r[2] = UN(NtUserRealWaitMessageEx)(QS_POSTMESSAGE, 30); a->gle[2] = GetLastError();
}
XT(wait_returns_at_once_for_a_set_change_bit_until_it_is_consumed) {
    NEED(NtUserRealWaitMessageEx);
    struct xt_wk w; struct wa a;
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_wait_pending, &a));
    EXPECT_EQ(a.r[0], 1); EXPECT_EQ(a.r[1], 1);
    EXPECT_EQ(a.r[2], 0); EXPECT_EQ(a.gle[2], E_TIMEOUT);
    xt_wk_stop(&w);
}

/* ---- RealInternalGetMessage ---- */
static void job_rigm(struct xt_wk *w, void *p) {
    struct wa *a = p; MSG m;
        memset(&m, 0xCC, sizeof m);
    a->r[0] = R_D(UN(NtUserRealInternalGetMessage)(&m, NULL, WM_APP + 1, WM_APP + 1, PM_REMOVE, 0));
    a->m = m;
    memset(&m, 0xCC, sizeof m);
    a->r[1] = R_D(UN(NtUserRealInternalGetMessage)(&m, NULL, WM_APP + 1, WM_APP + 1, PM_REMOVE, 0));
}
XT(realinternalgetmessage_takes_a_message_posted_by_another_thread) {
    NEED(NtUserRealInternalGetMessage);
    struct xt_wk w; struct wa a;
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_drain, NULL));
    CHECK(U(PostThreadMessageW)(w.tid, WM_APP + 1, 0x11, 0x22));
    CHECK(xt_wk_run(&w, job_rigm, &a));
    EXPECT_EQ(a.m.message, WM_APP + 1);
    EXPECT_EQ(a.m.wParam, 0x11);
    EXPECT_EQ(a.m.lParam, 0x22);
    EXPECT_EQ(a.m.hwnd, NULL);
    xt_wk_stop(&w);
}
XT(realinternalgetmessage_removes_the_message_from_the_workers_queue) {
    NEED(NtUserRealInternalGetMessage);
    struct xt_wk w; struct wa a;
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_drain, NULL));
    CHECK(U(PostThreadMessageW)(w.tid, WM_APP + 1, 1, 2));
    CHECK(xt_wk_run(&w, job_rigm, &a));
    EXPECT_EQ(a.r[0], 1);                             /* TRUE when it took a message ... */
    EXPECT_EQ(a.r[1], 0);                             /* ... and FALSE when the queue is empty */
    xt_wk_stop(&w);
}
XT(realinternalgetmessage_finds_nothing_for_a_message_posted_to_another_thread) {
    NEED(NtUserRealInternalGetMessage);
    struct xt_wk w, o; struct wa a, b;
    CHECK(xt_wk_start(&w, 1)); CHECK(xt_wk_start(&o, 1));
    CHECK(xt_wk_run(&w, job_drain, NULL)); CHECK(xt_wk_run(&o, job_drain, NULL));
    CHECK(U(PostThreadMessageW)(o.tid, WM_APP + 1, 3, 4));       /* the other worker's queue */
    CHECK(xt_wk_run(&w, job_rigm, &a));
    EXPECT_EQ(a.r[0], 0);
    EXPECT_EQ(a.m.message, 0);                                    /* no message for this thread */
    CHECK(xt_wk_run(&o, job_rigm, &b));
    EXPECT_EQ(b.m.message, WM_APP + 1);                           /* it was waiting in the other queue all along */
    EXPECT_EQ(b.m.wParam, 3);
    xt_wk_stop(&w); xt_wk_stop(&o);
}
static void job_rigm_order(struct xt_wk *w, void *p) {
    struct wa *a = p; MSG m;
        for (int i = 0; i < 4; i++) {
        memset(&m, 0, sizeof m);
        UN(NtUserRealInternalGetMessage)(&m, NULL, 0, 0, PM_REMOVE, 0);
        a->r[i] = (DWORD)m.wParam;
    }
}
XT(realinternalgetmessage_keeps_posting_order) {
    NEED(NtUserRealInternalGetMessage);
    struct xt_wk w; struct wa a;
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_drain, NULL));
    for (int i = 1; i <= 4; i++) CHECK(U(PostThreadMessageW)(w.tid, WM_APP + 1, i * 10, 0));
    CHECK(xt_wk_run(&w, job_rigm_order, &a));
    EXPECT_EQ(a.r[0], 10); EXPECT_EQ(a.r[1], 20); EXPECT_EQ(a.r[2], 30); EXPECT_EQ(a.r[3], 40);
    xt_wk_stop(&w);
}
static void job_rigm_filter(struct xt_wk *w, void *p) {
    struct wa *a = p; MSG m;
        memset(&m, 0, sizeof m);
    UN(NtUserRealInternalGetMessage)(&m, NULL, WM_APP + 2, WM_APP + 2, PM_REMOVE, 0);
    a->r[0] = m.message;
    memset(&m, 0, sizeof m);
    UN(NtUserRealInternalGetMessage)(&m, NULL, 0, 0, PM_REMOVE, 0);
    a->r[1] = m.message;
}
XT(realinternalgetmessage_range_filter_skips_the_earlier_message) {
    NEED(NtUserRealInternalGetMessage);
    struct xt_wk w; struct wa a;
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_drain, NULL));
    CHECK(U(PostThreadMessageW)(w.tid, WM_APP + 1, 0, 0));
    CHECK(U(PostThreadMessageW)(w.tid, WM_APP + 2, 0, 0));
    CHECK(xt_wk_run(&w, job_rigm_filter, &a));
    EXPECT_EQ(a.r[0], WM_APP + 2);
    EXPECT_EQ(a.r[1], WM_APP + 1);
    xt_wk_stop(&w);
}
static void job_rigm_bad(struct xt_wk *w, void *p) {
    struct wa *a = p; MSG m;
    static const UINT bad[] = { 0x4, 0x100, 0x8000, 0x1000000, 0x80000000u };
    for (int i = 0; i < 5; i++) {
        memset(&m, 0xCC, sizeof m);
        SET_ERR(SENT); UN(NtUserRealInternalGetMessage)(&m, NULL, 0, 0, bad[i], 0);
        a->gle[i] = GetLastError(); a->r[i] = ((unsigned char *)&m)[0] == 0xCC;
    }
}
XT(realinternalgetmessage_bad_flags_are_refused_on_a_worker) {
    NEED(NtUserRealInternalGetMessage);
    struct xt_wk w; struct wa a;
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_rigm_bad, &a));
    for (int i = 0; i < 5; i++) { EXPECT_EQ(a.gle[i], E_INVALID_FLAGS); EXPECT_EQ(a.r[i], 1); }
    xt_wk_stop(&w);
}
static void job_rigm_hwnd(struct xt_wk *w, void *p) {
    struct wa *a = p; MSG m;
        memset(&m, 0, sizeof m);
    UN(NtUserRealInternalGetMessage)(&m, w->w[0], 0, 0, PM_REMOVE, 0);
    a->m = m;
}
XT(realinternalgetmessage_window_filter_picks_the_window_message) {
    NEED(NtUserRealInternalGetMessage);
    struct xt_wk w; struct wa a;
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_drain, NULL));
    CHECK(U(PostThreadMessageW)(w.tid, WM_APP + 1, 1, 0));
    CHECK(U(PostMessageW)(w.w[0], WM_APP + 1, 2, 0));
    CHECK(xt_wk_run(&w, job_rigm_hwnd, &a));
    EXPECT_EQ(a.m.hwnd, w.w[0]);
    EXPECT_EQ(a.m.wParam, 2);
    xt_wk_stop(&w);
}
static void job_rigm_foreign(struct xt_wk *w, void *p) {
    struct wa *a = p; MSG m;
        HWND f = a->m.hwnd;
    memset(&m, 0xCC, sizeof m);
    SET_ERR(SENT);
    a->r[0] = R_D(UN(NtUserRealInternalGetMessage)(&m, f, 0, 0, PM_REMOVE, 0));
    a->gle[0] = GetLastError();
}
XT(realinternalgetmessage_window_of_another_thread_gives_nothing) {
    NEED(NtUserRealInternalGetMessage);
    struct xt_wk w; struct wa a;
    HWND mine = xt_window(); CHECK(mine);
    CHECK(xt_wk_start(&w, 1));
    a.m.hwnd = mine;
    xt_wk_go(&w, job_rigm_foreign, &a);
    CHECK(xt_wk_wait(&w, 20000));
    EXPECT_EQ(a.r[0], 0);
    EXPECT_EQ(a.gle[0], SENT);
    xt_wk_stop(&w);
    xt_destroy(mine);
}

/* ---- per-thread message extra info ---- */
static void job_extra(struct xt_wk *w, void *p) {
    struct wa *a = p;
    a->r[0] = (DWORD)U(GetMessageExtraInfo)();
    a->r[1] = (DWORD)U(SetMessageExtraInfo)((LPARAM)0x1234);
    a->r[2] = (DWORD)U(GetMessageExtraInfo)();
}
XT(message_extra_info_set_on_a_worker_is_not_the_main_threads) {
    NEED_ALL;
    struct xt_wk w; struct wa a;
    LPARAM main0 = U(GetMessageExtraInfo)();
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_extra, &a));
    EXPECT_EQ(a.r[1], a.r[0]);                         /* SetMessageExtraInfo returns the previous value */
    EXPECT_EQ(a.r[2], 0x1234);
    EXPECT_EQ(U(GetMessageExtraInfo)(), main0);
    xt_wk_stop(&w);
}
static void extra_fresh(void *p) { DWORD *r = p; r[0] = (DWORD)U(GetMessageExtraInfo)(); }
XT(message_extra_info_is_not_inherited_by_a_new_thread) {
    NEED_ALL;
    DWORD r[1] = { 9 };
    LPARAM prev = U(SetMessageExtraInfo)((LPARAM)0x4321);
    CHECK(xt_on_thread(extra_fresh, r));
    EXPECT_EQ(r[0], 0);
    EXPECT_EQ(U(GetMessageExtraInfo)(), 0x4321);
    U(SetMessageExtraInfo)(prev);
}
static void extra_two(struct xt_wk *w, void *p) { DWORD *r = p; r[0] = (DWORD)U(SetMessageExtraInfo)((LPARAM)0x55); r[1] = (DWORD)U(GetMessageExtraInfo)(); }
XT(message_extra_info_of_two_workers_is_independent) {
    NEED_ALL;
    struct xt_wk a, b; DWORD ra[2]; struct wa rb;
    CHECK(xt_wk_start(&a, 1)); CHECK(xt_wk_start(&b, 1));
    CHECK(xt_wk_run(&a, extra_two, ra));
    CHECK(xt_wk_run(&b, job_extra, &rb));
    EXPECT_EQ(ra[1], 0x55);
    EXPECT_EQ(rb.r[0], 0);                               /* B did not see A's 0x55 */
    xt_wk_stop(&a); xt_wk_stop(&b);
}
