/* Layered attribute calls on windows that belong to ANOTHER THREAD of the same process: Set/Get/SetWindowLong issued from a
 * worker thread against a window of the main thread, and from the main thread against windows owned by a worker that either
 * pumps messages, sits blocked without a message loop, or has already exited (which destroys its windows). Measures that no
 * ownership test exists for these calls inside one process (they succeed and the stored values are visible to both threads),
 * that blocking in the owner thread does not stall them, and that a window whose thread is gone reports an invalid window
 * handle (1400) from every entry point. */
#include "xl.h"

struct xt { HWND w; HANDLE ready, go; int kind; };     /* kind 1 pump, 2 blocked, 3 exit at once */
static DWORD WINAPI owner_thread(LPVOID a) {
    struct xt *t = a;
    t->w = xl_mk(WS_POPUP, XL_EXL | WS_EX_TOOLWINDOW, NULL);
    SetEvent(t->ready);
    if (t->kind == 1) { MSG m; while (WaitForSingleObject(t->go, 0) != WAIT_OBJECT_0) { while (U(PeekMessageA)(&m, NULL, 0, 0, PM_REMOVE)) { U(TranslateMessage)(&m); U(DispatchMessageA)(&m); } Sleep(5); } }
    else if (t->kind == 2) WaitForSingleObject(t->go, 20000);
    return 0;
}
static HANDLE xt_start(struct xt *t, int kind) {
    memset(t, 0, sizeof *t); t->kind = kind;
    t->ready = CreateEventA(NULL, TRUE, FALSE, NULL); t->go = CreateEventA(NULL, TRUE, FALSE, NULL);
    HANDLE h = CreateThread(NULL, 0, owner_thread, t, 0, NULL);
    if (h) WaitForSingleObject(t->ready, 20000);
    return h;
}
static void xt_stop(struct xt *t, HANDLE h) { SetEvent(t->go); WaitForSingleObject(h, 20000); CloseHandle(h); CloseHandle(t->ready); CloseHandle(t->go); }

/* ---- worker thread acts on a main-thread window --------------------------------------------------------- */
struct wk { HWND w; int step; BOOL r[8]; DWORD e[8]; struct xl_attr a; LONG old; LONG now; };
static DWORD WINAPI worker(LPVOID p) {
    struct wk *k = p;
    switch (k->step) {
    case 0: SET_ERR(XL_ERR_SENT); k->r[0] = xl_set(k->w, 0x010203, 120, 3); k->e[0] = GetLastError(); break;
    case 1: xl_attr_clear(&k->a); SET_ERR(XL_ERR_SENT); k->r[0] = xl_getall(k->w, &k->a); k->e[0] = GetLastError(); break;
    case 2: k->old = U(SetWindowLongA)(k->w, GWL_EXSTYLE, xl_exstyle(k->w) & ~XL_EXL); k->now = xl_exstyle(k->w); break;
    case 3: { struct xl_img im; xl_img_make(&im, 4, 4); SET_ERR(XL_ERR_SENT); k->r[0] = xl_ulw(k->w, &im, ULW_ALPHA); k->e[0] = GetLastError(); xl_img_free(&im); } break;
    case 4: SET_ERR(XL_ERR_SENT); k->r[0] = xl_set(k->w, 1, 2, 0x10); k->e[0] = GetLastError(); break;
    case 5: SET_ERR(XL_ERR_SENT); k->r[0] = xl_set(xl_bogus(), 1, 2, 3); k->e[0] = GetLastError(); break;
    }
    return 0;
}
/* The main thread keeps dispatching its messages while it waits: SetWindowLong on a window of another thread is carried out
 * by the owner thread, so an owner that does not pump messages would stall the call. */
static void wait_pumping(HANDLE h) {
    MSG m; DWORD t0 = GetTickCount();
    while (GetTickCount() - t0 < 20000) {
        DWORD r = MsgWaitForMultipleObjects(1, &h, FALSE, 100, QS_ALLINPUT);
        if (r == WAIT_OBJECT_0) return;
        while (U(PeekMessageA)(&m, NULL, 0, 0, PM_REMOVE)) { U(TranslateMessage)(&m); U(DispatchMessageA)(&m); }
    }
}
static void run_worker(struct wk *k, int step) { k->step = step; HANDLE h = CreateThread(NULL, 0, worker, k, 0, NULL); wait_pumping(h); CloseHandle(h); }

TEST(thread_worker_sets_main_window) {
    XL_NEED(); struct wk k; memset(&k, 0, sizeof k); k.w = xl_lay();
    run_worker(&k, 0); CHECK(k.r[0]); EXPECT_EQ(k.e[0], XL_ERR_SENT);
    struct xl_attr a; CHECK(xl_getall(k.w, &a));
    EXPECT_EQ(a.key, 0x010203); EXPECT_EQ(a.alpha, 120); EXPECT_EQ(a.flags, 3);
    xl_kill(k.w);
}
TEST(thread_worker_reads_value_set_by_main) {
    XL_NEED(); struct wk k; memset(&k, 0, sizeof k); k.w = xl_lay(); CHECK(xl_set(k.w, 0x0a0b0c, 33, 2));
    run_worker(&k, 1); CHECK(k.r[0]); EXPECT_EQ(k.a.alpha, 33); EXPECT_EQ(k.a.flags, 2);
    xl_kill(k.w);
}
TEST(thread_worker_get_fresh_layered_fails_without_error) {
    XL_NEED(); struct wk k; memset(&k, 0, sizeof k); k.w = xl_lay();
    run_worker(&k, 1); CHECK(!k.r[0]); EXPECT_EQ(k.e[0], XL_ERR_SENT); EXPECT_EQ(k.a.flags, 0xfeedf00d);
    xl_kill(k.w);
}
TEST(thread_worker_bad_flags_87) {
    XL_NEED(); struct wk k; memset(&k, 0, sizeof k); k.w = xl_lay();
    run_worker(&k, 4); CHECK(!k.r[0]); EXPECT_EQ(k.e[0], ERROR_INVALID_PARAMETER);
    xl_kill(k.w);
}
TEST(thread_worker_bogus_handle_1400) {
    XL_NEED(); struct wk k; memset(&k, 0, sizeof k); k.w = xl_lay();
    run_worker(&k, 5); CHECK(!k.r[0]); EXPECT_EQ(k.e[0], ERROR_INVALID_WINDOW_HANDLE);
    xl_kill(k.w);
}
TEST(thread_worker_clears_layered_bit_of_main_window) {
    XL_NEED(); struct wk k; memset(&k, 0, sizeof k); k.w = xl_lay(); xl_set(k.w, 1, 2, 3);
    run_worker(&k, 2);
    EXPECT_EQ(k.old & XL_EXL, XL_EXL); EXPECT_EQ(k.now & XL_EXL, 0); CHECK(!xl_is_lay(k.w));
    struct xl_attr a; SET_ERR(XL_ERR_SENT); CHECK(!xl_getall(k.w, &a)); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    xl_kill(k.w);
}
TEST(thread_worker_fresh_ulw_then_main_set) {
    XL_NEED(); struct wk k; memset(&k, 0, sizeof k); k.w = xl_lay();
    run_worker(&k, 3); CHECKF(k.r[0], "worker ulw err %lu", k.e[0]);
    CHECK(xl_set(k.w, 1, 2, 3));     /* Set after an image update is accepted */
    xl_kill(k.w);
}

/* ---- main thread acts on a window owned by a worker --------------------------------------------------------- */
#define OWNER_SETGET(nm, kind) \
    TEST(thread_owner_##nm##_set_get) { XL_NEED(); struct xt t; HANDLE h = xt_start(&t, kind); if (!h) { t_skip("thread"); return; } \
        CHECK(U(IsWindow)(t.w)); CHECK(xl_is_lay(t.w)); \
        SET_ERR(XL_ERR_SENT); CHECK(xl_set(t.w, 0x000102, 9, 3)); EXPECT_EQ(GetLastError(), XL_ERR_SENT); \
        struct xl_attr a; CHECK(xl_getall(t.w, &a)); EXPECT_EQ(a.key, 0x000102); EXPECT_EQ(a.alpha, 9); EXPECT_EQ(a.flags, 3); \
        xt_stop(&t, h); }
OWNER_SETGET(pumping, 1)
OWNER_SETGET(blocked, 2)
TEST(thread_owner_pumping_setwindowlong) {
    XL_NEED(); struct xt t; HANDLE h = xt_start(&t, 1); if (!h) { t_skip("thread"); return; }
    LONG old = xl_exstyle(t.w); LONG r = U(SetWindowLongA)(t.w, GWL_EXSTYLE, old | WS_EX_TRANSPARENT);
    EXPECT_EQ(r, old); EXPECT_EQ(xl_exstyle(t.w), old | WS_EX_TRANSPARENT);
    xt_stop(&t, h);
}
TEST(thread_owner_pumping_attrs_survive_other_exstyle_change) {
    XL_NEED(); struct xt t; HANDLE h = xt_start(&t, 1); if (!h) { t_skip("thread"); return; }
    CHECK(xl_set(t.w, 5, 6, 3)); U(SetWindowLongA)(t.w, GWL_EXSTYLE, xl_exstyle(t.w) | WS_EX_TRANSPARENT);
    struct xl_attr a; CHECK(xl_getall(t.w, &a)); EXPECT_EQ(a.alpha, 6); xt_stop(&t, h);
}

TEST(thread_owner_exited_window_is_gone) {
    XL_NEED(); struct xt t; HANDLE h = xt_start(&t, 3); if (!h) { t_skip("thread"); return; }
    WaitForSingleObject(h, 20000); CloseHandle(h);
    CHECK(!U(IsWindow)(t.w));
    CloseHandle(t.ready); CloseHandle(t.go);
}
TEST(thread_owner_exited_set_get_1400) {
    XL_NEED(); struct xt t; HANDLE h = xt_start(&t, 3); if (!h) { t_skip("thread"); return; }
    WaitForSingleObject(h, 20000); CloseHandle(h);
    struct xl_attr a;
    SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_set(t.w, 1, 2, 3), ERROR_INVALID_WINDOW_HANDLE);
    SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_getall(t.w, &a), ERROR_INVALID_WINDOW_HANDLE);
    CloseHandle(t.ready); CloseHandle(t.go);
}
TEST(thread_owner_exited_ulw_1400) {
    XL_NEED(); struct xt t; HANDLE h = xt_start(&t, 3); if (!h) { t_skip("thread"); return; }
    WaitForSingleObject(h, 20000); CloseHandle(h);
    struct xl_img im; xl_img_make(&im, 4, 4);
    SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_ulw(t.w, &im, ULW_ALPHA), ERROR_INVALID_WINDOW_HANDLE);
    xl_img_free(&im); CloseHandle(t.ready); CloseHandle(t.go);
}
TEST(thread_owner_exited_after_set_still_1400) {
    XL_NEED(); struct xt t; HANDLE h = xt_start(&t, 2); if (!h) { t_skip("thread"); return; }
    CHECK(xl_set(t.w, 1, 2, 3)); xt_stop(&t, h);     /* thread has exited when xt_stop returns */
    struct xl_attr a; SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_getall(t.w, &a), ERROR_INVALID_WINDOW_HANDLE);
}
TEST(thread_owner_exited_setwindowlong_fails_1400) {
    XL_NEED(); struct xt t; HANDLE h = xt_start(&t, 3); if (!h) { t_skip("thread"); return; }
    WaitForSingleObject(h, 20000); CloseHandle(h);
    SET_ERR(XL_ERR_SENT); LONG r = U(SetWindowLongA)(t.w, GWL_EXSTYLE, 0);
    EXPECT_EQ(r, 0); EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
    CloseHandle(t.ready); CloseHandle(t.go);
}
