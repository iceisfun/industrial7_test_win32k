/* x_thread_desktop_heap_multi / thread: the desktop and the DPI awareness context are per thread, with the process as default.
 *
 * A new thread starts on the process's desktop (the same desktop object by name) and with the process's DPI awareness context
 * whatever the creating thread has set for itself; SetThreadDpiAwarenessContext changes only the calling thread and returns the
 * context it replaced; a thread that owns a window cannot move to another desktop. Only the calling thread's context is changed
 * and it is put back before the thread ends.
 */
#include "x.h"

static void desk_name(HDESK d, char *out, size_t cap) {
    DWORD n = 0;
    out[0] = 0;
    if (!d || !U(GetUserObjectInformationA)(d, UOI_NAME, out, (DWORD)cap, &n)) strcpy(out, "?");
}
static void fresh_desk(void *p) { char *n = p; desk_name(U(GetThreadDesktop)(GetCurrentThreadId()), n, 64); }
XT(new_thread_runs_on_the_process_desktop) {
    NEED_ALL;
    char mine[64], theirs[64] = "none";
    desk_name(U(GetThreadDesktop)(GetCurrentThreadId()), mine, sizeof mine);
    CHECK(xt_on_thread(fresh_desk, theirs));
    EXPECT_STR(theirs, mine);
    CHECK(strcmp(mine, "?") != 0);
}
static void job_desk(struct xt_wk *w, void *p) { desk_name(U(GetThreadDesktop)(GetCurrentThreadId()), (char *)p, 64); }
XT(worker_with_windows_is_on_the_process_desktop) {
    NEED_ALL;
    struct xt_wk w; char mine[64], theirs[64] = "none";
    desk_name(U(GetThreadDesktop)(GetCurrentThreadId()), mine, sizeof mine);
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_desk, theirs));
    EXPECT_STR(theirs, mine);
    xt_wk_stop(&w);
}
static void job_setdesk(struct xt_wk *w, void *p) {
    DWORD *r = p;
    HDESK d = U(GetThreadDesktop)(GetCurrentThreadId());
    SET_ERR(SENT);
    r[0] = U(SetThreadDesktop)(d);
    r[1] = GetLastError();
}
XT(set_thread_desktop_is_refused_for_a_worker_that_owns_windows) {
    NEED_ALL;
    struct xt_wk w; DWORD r[2];
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_setdesk, r));
    EXPECT_EQ(r[0], 0);
    EXPECT_EQ(r[1], 170);                         /* ERROR_BUSY */
    xt_wk_stop(&w);
}
XT(set_thread_desktop_on_a_worker_does_not_move_the_main_thread) {
    NEED_ALL;
    struct xt_wk w; DWORD r[2]; char before[64], after[64];
    desk_name(U(GetThreadDesktop)(GetCurrentThreadId()), before, sizeof before);
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_setdesk, r));
    desk_name(U(GetThreadDesktop)(GetCurrentThreadId()), after, sizeof after);
    EXPECT_STR(after, before);
    xt_wk_stop(&w);
}

typedef DPI_AWARENESS_CONTEXT (WINAPI *setdpi_t)(DPI_AWARENESS_CONTEXT);
typedef DPI_AWARENESS_CONTEXT (WINAPI *getdpi_t)(void);
typedef BOOL (WINAPI *eqdpi_t)(DPI_AWARENESS_CONTEXT, DPI_AWARENESS_CONTEXT);
static setdpi_t p_set(void) { return (setdpi_t)t_proc_in("user32.dll", "SetThreadDpiAwarenessContext"); }
static getdpi_t p_get(void) { return (getdpi_t)t_proc_in("user32.dll", "GetThreadDpiAwarenessContext"); }
static eqdpi_t p_eq(void) { return (eqdpi_t)t_proc_in("user32.dll", "AreDpiAwarenessContextsEqual"); }
#define NEED_DPI do { NEED_ALL; if (!p_set() || !p_get() || !p_eq()) { t_skip("missing export: thread DPI awareness context"); return; } } while (0)

struct dp { int same_as_start, same_as_set, prev_is_start; };
static void job_dpi(struct xt_wk *w, void *p) {
    struct dp *d = p;
    DPI_AWARENESS_CONTEXT start = p_get()();
    DPI_AWARENESS_CONTEXT prev = p_set()(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    d->prev_is_start = p_eq()(prev, start);
    d->same_as_set = p_eq()(p_get()(), DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    p_set()(prev);
    d->same_as_start = p_eq()(p_get()(), start);
}
XT(set_thread_dpi_context_on_a_worker_returns_the_previous_and_takes_effect) {
    NEED_DPI;
    struct xt_wk w; struct dp d = { 9, 9, 9 };
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_dpi, &d));
    EXPECT_EQ(d.prev_is_start, 1);
    EXPECT_EQ(d.same_as_set, 1);
    EXPECT_EQ(d.same_as_start, 1);               /* putting the returned context back restores it */
    xt_wk_stop(&w);
}
static void job_dpi_hold(struct xt_wk *w, void *p) {
    int *r = p;
    p_set()(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    *r = p_eq()(p_get()(), DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
}
XT(worker_dpi_context_is_not_the_main_threads) {
    NEED_DPI;
    struct xt_wk w; int r = 9;
    DPI_AWARENESS_CONTEXT m0 = p_get()();
    CHECK(xt_wk_start(&w, 1));
    CHECK(xt_wk_run(&w, job_dpi_hold, &r));
    EXPECT_EQ(r, 1);
    CHECK(p_eq()(p_get()(), m0));
    xt_wk_stop(&w);
}
static void job_dpi_two(struct xt_wk *w, void *p) {
    int *r = p;
    r[0] = p_eq()(p_get()(), DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
}
XT(worker_dpi_context_is_not_the_other_workers) {
    NEED_DPI;
    struct xt_wk a, b; int ra = 9, rb = 9;
    /* what B must still show: the process default, compared with system aware on the main thread (machine and manifest
     * dependent, so only the relation is used) */
    int proc_is_sys = p_eq()(p_get()(), DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    CHECK(xt_wk_start(&a, 1)); CHECK(xt_wk_start(&b, 1));
    CHECK(xt_wk_run(&a, job_dpi_hold, &ra));
    CHECK(xt_wk_run(&b, job_dpi_two, &rb));
    EXPECT_EQ(ra, 1);
    EXPECT_EQ(rb, proc_is_sys);                   /* B kept the process default, A's change did not reach it */
    xt_wk_stop(&a); xt_wk_stop(&b);
}
static void fresh_dpi(void *p) { int *r = p; r[0] = p_eq()(p_get()(), *(DPI_AWARENESS_CONTEXT *)(r + 2)); }
XT(new_thread_takes_the_process_dpi_context_not_its_creators) {
    NEED_DPI;
    int r[6] = { 9, 9 };
    DPI_AWARENESS_CONTEXT proc = p_get()();
    DPI_AWARENESS_CONTEXT prev = p_set()(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    *(DPI_AWARENESS_CONTEXT *)(r + 2) = proc;
    CHECK(xt_on_thread(fresh_dpi, r));
    p_set()(prev);
    EXPECT_EQ(r[0], 1);
}
