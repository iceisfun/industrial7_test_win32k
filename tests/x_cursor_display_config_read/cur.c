/* Cursor services: NtUserGetCursor, NtUserSetCursor, NtUserGetRequiredCursorSizes, NtUserLinkDpiCursor,
 * NtUserSetCursorContents.
 *
 * Measured on Windows 10 22H2: the current cursor is state of the thread's input queue; SetCursor returns the
 * previous one and a failed call (invalid cursor handle, last error 1402) leaves it alone; SetCursor(NULL) hides
 * the cursor and GetCursor then answers NULL. A thread with its own queue neither sees nor changes another
 * thread's cursor. GetRequiredCursorSizes answers five {valid, size} pairs. LinkDpiCursor refuses cursors that
 * are not DPI cursors (87). SetCursorContents moves the image and hotspot of its second cursor into the first and leaves that second handle dead
 * (no longer a cursor handle, not destroyable).
 * Every test restores the cursor it found; cursors created here are destroyed only when they are not current.
 */
#include "x.h"

#define NEEDCUR() do { NEED(NtUserGetCursor); NEED(NtUserSetCursor); } while (0)

XT(getcursor_matches_user32_getcursor) {
    NEED(NtUserGetCursor);
    CHECK(UN(NtUserGetCursor)() == U(GetCursor)());
}
XT(getcursor_is_stable_between_calls) {
    NEED(NtUserGetCursor);
    HANDLE a = UN(NtUserGetCursor)(), b = UN(NtUserGetCursor)();
    CHECK(a == b);
}
XT(getcursor_does_not_set_last_error) {
    NEED(NtUserGetCursor);
    SET_ERR(XSENT);
    UN(NtUserGetCursor)();
    EXPECT_EQ(GetLastError(), XSENT);
}
XT(setcursor_returns_previous_and_get_reflects) {
    NEEDCUR(); XC_CUR_GUARD();
    HCURSOR a = xc_stock((int)(ULONG_PTR)IDC_ARROW), w = xc_stock((int)(ULONG_PTR)IDC_WAIT);
    CHECK(a && w && a != w);
    UN(NtUserSetCursor)(a);
    EXPECT_EQ(UN(NtUserSetCursor)(w), a);
    EXPECT_EQ(UN(NtUserGetCursor)(), w);
    EXPECT_EQ(U(GetCursor)(), w);
    EXPECT_EQ(UN(NtUserSetCursor)(a), w);
    EXPECT_EQ(UN(NtUserGetCursor)(), a);
}
XT(setcursor_first_return_is_the_saved_cursor) {
    NEEDCUR();
    HANDLE before = UN(NtUserGetCursor)();
    HCURSOR a = xc_stock((int)(ULONG_PTR)IDC_CROSS);
    HANDLE r = UN(NtUserSetCursor)(a);
    EXPECT_EQ(r, before);
    UN(NtUserSetCursor)(before);
    EXPECT_EQ(UN(NtUserGetCursor)(), before);
}
XT(setcursor_same_cursor_twice_returns_it_back) {
    NEEDCUR(); XC_CUR_GUARD();
    HCURSOR w = xc_stock((int)(ULONG_PTR)IDC_WAIT);
    UN(NtUserSetCursor)(w);
    EXPECT_EQ(UN(NtUserSetCursor)(w), w);
    EXPECT_EQ(UN(NtUserGetCursor)(), w);
}
XT(setcursor_success_does_not_set_last_error) {
    NEEDCUR(); XC_CUR_GUARD();
    HCURSOR a = xc_stock((int)(ULONG_PTR)IDC_ARROW);
    SET_ERR(XSENT);
    UN(NtUserSetCursor)(a);
    EXPECT_EQ(GetLastError(), XSENT);
}
XT(setcursor_chain_of_five_returns_each_predecessor) {
    NEEDCUR(); XC_CUR_GUARD();
    static const int ids[] = { 32512, 32513, 32514, 32515, 32649 };   /* arrow, ibeam, wait, cross, hand */
    HCURSOR c[5];
    for (int i = 0; i < 5; i++) { c[i] = xc_stock(ids[i]); CHECK(c[i] != NULL); }
    UN(NtUserSetCursor)(c[0]);
    for (int i = 1; i < 5; i++) {
        HANDLE r = UN(NtUserSetCursor)(c[i]);
        CHECKF(r == c[i - 1], "step %d returned a different predecessor", i);
    }
}
XT(setcursor_null_hides_and_returns_previous) {
    NEEDCUR(); XC_CUR_GUARD();
    HCURSOR a = xc_stock((int)(ULONG_PTR)IDC_ARROW);
    UN(NtUserSetCursor)(a);
    SET_ERR(XSENT);
    EXPECT_EQ(UN(NtUserSetCursor)(NULL), a);
    EXPECT_EQ(GetLastError(), XSENT);
    EXPECT_EQ(UN(NtUserGetCursor)(), NULL);
    EXPECT_EQ(U(GetCursor)(), NULL);
}
XT(setcursor_after_null_returns_null_then_shows) {
    NEEDCUR(); XC_CUR_GUARD();
    HCURSOR a = xc_stock((int)(ULONG_PTR)IDC_ARROW);
    UN(NtUserSetCursor)(NULL);
    EXPECT_EQ(UN(NtUserSetCursor)(a), NULL);
    EXPECT_EQ(UN(NtUserGetCursor)(), a);
}
XT(setcursor_null_twice_both_succeed) {
    NEEDCUR(); XC_CUR_GUARD();
    UN(NtUserSetCursor)(NULL);
    SET_ERR(XSENT);
    EXPECT_EQ(UN(NtUserSetCursor)(NULL), NULL);
    EXPECT_EQ(GetLastError(), XSENT);
    EXPECT_EQ(UN(NtUserGetCursor)(), NULL);
}

static void bad_handle_case(HANDLE bad, const char *what) {
    HCURSOR a = xc_stock((int)(ULONG_PTR)IDC_ARROW);
    UN(NtUserSetCursor)(a);
    SET_ERR(XSENT);
    HANDLE r = UN(NtUserSetCursor)(bad);
    DWORD e = GetLastError();
    CHECKF(r == NULL, "%s: returned a cursor", what);
    CHECKF(e == XE_INVALID_CURSOR_HANDLE, "%s: last error %lu want 1402", what, e);
    CHECKF(UN(NtUserGetCursor)() == a, "%s: the current cursor changed", what);
}
XT(setcursor_invalid_small_values) {
    NEEDCUR(); XC_CUR_GUARD();
    bad_handle_case((HANDLE)(ULONG_PTR)1, "1");
    bad_handle_case((HANDLE)(ULONG_PTR)0x1234, "0x1234");
    bad_handle_case((HANDLE)(ULONG_PTR)0xFFFF, "0xFFFF");
}
XT(setcursor_invalid_large_values) {
    NEEDCUR(); XC_CUR_GUARD();
    bad_handle_case((HANDLE)(ULONG_PTR)0xDEADBEEF, "0xDEADBEEF");
    bad_handle_case((HANDLE)(ULONG_PTR)0xFFFFFFFF, "0xFFFFFFFF");
    bad_handle_case((HANDLE)(ULONG_PTR)0x7FFFFFFFFFFFFFFFULL, "0x7FFF...");
}
XT(setcursor_destroyed_cursor_is_invalid) {
    NEEDCUR(); XC_CUR_GUARD();
    HCURSOR c = xc_cursor(32, 32, 0, 0, 0);
    CHECK(c != NULL);
    U(DestroyCursor)(c);
    bad_handle_case(c, "destroyed cursor");
}
XT(setcursor_window_handle_is_invalid) {
    NEEDCUR(); XC_CUR_GUARD();
    HWND w = xc_hidden_window();
    CHECK(w != NULL);
    bad_handle_case(w, "window");
    if (w) U(DestroyWindow)(w);
}
XT(setcursor_menu_handle_is_invalid) {
    NEEDCUR(); XC_CUR_GUARD();
    HMENU m = U(CreateMenu)();
    CHECK(m != NULL);
    bad_handle_case(m, "menu");
    if (m) U(DestroyMenu)(m);
}
XT(setcursor_destroyed_icon_is_invalid) {
    NEEDCUR(); XC_CUR_GUARD();
    BYTE a[128], x[128]; memset(a, 0xff, sizeof a); memset(x, 0, sizeof x);
    HICON ic = U(CreateIcon)(GetModuleHandleA(NULL), 32, 32, 1, 1, a, x);
    CHECK(ic != NULL);
    if (ic) U(DestroyIcon)(ic);
    bad_handle_case(ic, "destroyed icon");
}
XT(setcursor_private_cursor_round_trip) {
    NEEDCUR(); XC_CUR_GUARD();
    HANDLE before = UN(NtUserGetCursor)();
    HCURSOR c = xc_cursor(32, 32, 5, 6, 0);
    CHECK(c != NULL);
    EXPECT_EQ(UN(NtUserSetCursor)(c), before);
    EXPECT_EQ(UN(NtUserGetCursor)(), c);
    EXPECT_EQ(UN(NtUserSetCursor)(before), c);
    EXPECT_EQ(UN(NtUserGetCursor)(), before);
    CHECK(U(DestroyCursor)(c));
}
XT(setcursor_two_private_cursors_chain) {
    NEEDCUR(); XC_CUR_GUARD();
    HCURSOR a = xc_cursor(32, 32, 1, 1, 0), b = xc_cursor(32, 32, 2, 2, 0xff);
    CHECK(a && b && a != b);
    UN(NtUserSetCursor)(a);
    EXPECT_EQ(UN(NtUserSetCursor)(b), a);
    EXPECT_EQ(UN(NtUserSetCursor)(a), b);
    UN(NtUserSetCursor)(xc_stock((int)(ULONG_PTR)IDC_ARROW));
    U(DestroyCursor)(a); U(DestroyCursor)(b);
}
XT(setcursor_stock_cursors_are_distinct_handles) {
    NEED(NtUserSetCursor);
    static const int ids[] = { 32512, 32513, 32514, 32515, 32516, 32642, 32643, 32644, 32645, 32646, 32648, 32649 };
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++)
        for (unsigned j = i + 1; j < sizeof ids / sizeof ids[0]; j++)
            CHECKF(xc_stock(ids[i]) != xc_stock(ids[j]), "stock cursors %d and %d share a handle", ids[i], ids[j]);
}
XT(setcursor_stock_cursor_is_accepted_for_every_standard_id) {
    NEEDCUR(); XC_CUR_GUARD();
    static const int ids[] = { 32512, 32513, 32514, 32515, 32516, 32642, 32643, 32644, 32645, 32646, 32648, 32649, 32650, 32651 };
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        HCURSOR c = xc_stock(ids[i]);
        if (!c) { t_skip("standard cursor %d is not available", ids[i]); return; }
        SET_ERR(XSENT);
        UN(NtUserSetCursor)(c);
        CHECKF(GetLastError() == XSENT, "cursor %d: last error set", ids[i]);
        CHECKF(UN(NtUserGetCursor)() == c, "cursor %d not current after the call", ids[i]);
    }
}
XT(setcursor_failure_after_hide_keeps_hidden) {
    NEEDCUR(); XC_CUR_GUARD();
    UN(NtUserSetCursor)(NULL);
    SET_ERR(XSENT);
    HANDLE r = UN(NtUserSetCursor)((HANDLE)(ULONG_PTR)0x1234);
    CHECK(r == NULL);
    EXPECT_ERR(XE_INVALID_CURSOR_HANDLE);
    EXPECT_EQ(UN(NtUserGetCursor)(), NULL);
}

/* ---- another thread has its own queue ------------------------------------------------------------------------ */
struct thr_cur { HANDLE first_get; HANDLE set_prev; HANDLE after_set; HANDLE after_main_set; HANDLE new_cursor; HANDLE hide_prev; HANDLE after_hide; HANDLE main_set_to; HANDLE ev_go, ev_done; };
static DWORD WINAPI thr_basic(LPVOID p) {
    struct thr_cur *t = p;
    t->first_get = UN(NtUserGetCursor)();
    t->set_prev = UN(NtUserSetCursor)(t->new_cursor);
    t->after_set = UN(NtUserGetCursor)();
    UN(NtUserSetCursor)(t->first_get);
    return 0;
}
XT(thread_set_does_not_change_the_main_threads_cursor) {
    NEEDCUR(); XC_CUR_GUARD();
    HCURSOR a = xc_stock((int)(ULONG_PTR)IDC_ARROW), w = xc_stock((int)(ULONG_PTR)IDC_WAIT);
    UN(NtUserSetCursor)(a);
    struct thr_cur t; memset(&t, 0, sizeof t); t.new_cursor = w;
    CHECK(xc_on_thread(thr_basic, &t));
    EXPECT_EQ(t.after_set, w);
    EXPECT_EQ(UN(NtUserGetCursor)(), a);
}
XT(thread_set_returns_that_threads_own_previous) {
    NEEDCUR(); XC_CUR_GUARD();
    HCURSOR w = xc_stock((int)(ULONG_PTR)IDC_WAIT);
    struct thr_cur t; memset(&t, 0, sizeof t); t.new_cursor = w;
    CHECK(xc_on_thread(thr_basic, &t));
    EXPECT_EQ(t.set_prev, t.first_get);
}
XT(thread_get_is_not_the_cursor_main_set_before_the_thread_started) {
    NEEDCUR(); XC_CUR_GUARD();
    /* an unusual cursor that no default queue state can equal */
    HCURSOR c = xc_cursor(32, 32, 7, 7, 0x55);
    CHECK(c != NULL);
    UN(NtUserSetCursor)(c);
    struct thr_cur t; memset(&t, 0, sizeof t); t.new_cursor = xc_stock((int)(ULONG_PTR)IDC_ARROW);
    CHECK(xc_on_thread(thr_basic, &t));
    CHECK(t.first_get != c);
    UN(NtUserSetCursor)(xc_stock((int)(ULONG_PTR)IDC_ARROW));
    U(DestroyCursor)(c);
}
static DWORD WINAPI thr_wait_main(LPVOID p) {
    struct thr_cur *t = p;
    t->first_get = UN(NtUserGetCursor)();
    SetEvent(t->ev_done);
    WaitForSingleObject(t->ev_go, 10000);
    t->after_main_set = UN(NtUserGetCursor)();
    return 0;
}
static DWORD WINAPI thr_wait_runner(LPVOID p) { return thr_wait_main(p); }
XT(main_set_after_thread_start_is_not_seen_by_the_thread) {
    NEEDCUR(); XC_CUR_GUARD();
    struct thr_cur t; memset(&t, 0, sizeof t);
    t.ev_go = CreateEventA(NULL, TRUE, FALSE, NULL); t.ev_done = CreateEventA(NULL, TRUE, FALSE, NULL);
    HANDLE th = CreateThread(NULL, 0, thr_wait_runner, &t, 0, NULL);
    CHECK(th != NULL);
    if (th) {
        CHECK(WaitForSingleObject(t.ev_done, 10000) == WAIT_OBJECT_0);
        HCURSOR c = xc_cursor(32, 32, 3, 3, 0x33);
        UN(NtUserSetCursor)(c);
        SetEvent(t.ev_go);
        WaitForSingleObject(th, 10000); CloseHandle(th);
        CHECK(t.after_main_set == t.first_get);
        CHECK(t.after_main_set != c);
        UN(NtUserSetCursor)(xc_stock((int)(ULONG_PTR)IDC_ARROW));
        U(DestroyCursor)(c);
    }
    CloseHandle(t.ev_go); CloseHandle(t.ev_done);
}
static DWORD WINAPI thr_hide(LPVOID p) {
    struct thr_cur *t = p;
    t->first_get = UN(NtUserGetCursor)();
    t->hide_prev = UN(NtUserSetCursor)(NULL);
    t->after_hide = UN(NtUserGetCursor)();
    return 0;
}
XT(thread_hide_does_not_hide_the_main_threads_cursor) {
    NEEDCUR(); XC_CUR_GUARD();
    HCURSOR a = xc_stock((int)(ULONG_PTR)IDC_ARROW);
    UN(NtUserSetCursor)(a);
    struct thr_cur t; memset(&t, 0, sizeof t);
    CHECK(xc_on_thread(thr_hide, &t));
    EXPECT_EQ(t.after_hide, NULL);
    EXPECT_EQ(t.hide_prev, t.first_get);
    EXPECT_EQ(UN(NtUserGetCursor)(), a);
}
static DWORD WINAPI thr_invalid(LPVOID p) {
    struct thr_cur *t = p;
    SET_ERR(XSENT);
    t->set_prev = UN(NtUserSetCursor)((HANDLE)(ULONG_PTR)0x1234);
    t->main_set_to = (HANDLE)(ULONG_PTR)GetLastError();
    return 0;
}
XT(thread_invalid_handle_fails_the_same_way) {
    NEEDCUR();
    struct thr_cur t; memset(&t, 0, sizeof t);
    CHECK(xc_on_thread(thr_invalid, &t));
    EXPECT_EQ(t.set_prev, NULL);
    EXPECT_EQ(t.main_set_to, XE_INVALID_CURSOR_HANDLE);
}
static DWORD WINAPI thr_window(LPVOID p) {
    struct thr_cur *t = p;
    HWND w = xc_hidden_window();
    t->first_get = UN(NtUserGetCursor)();
    t->set_prev = UN(NtUserSetCursor)(t->new_cursor);
    t->after_set = UN(NtUserGetCursor)();
    UN(NtUserSetCursor)(t->first_get);
    if (w) U(DestroyWindow)(w);
    return 0;
}
XT(thread_with_window_still_independent_of_main) {
    NEEDCUR(); XC_CUR_GUARD();
    HCURSOR a = xc_stock((int)(ULONG_PTR)IDC_ARROW), w = xc_stock((int)(ULONG_PTR)IDC_WAIT);
    UN(NtUserSetCursor)(a);
    struct thr_cur t; memset(&t, 0, sizeof t); t.new_cursor = w;
    CHECK(xc_on_thread(thr_window, &t));
    EXPECT_EQ(t.after_set, w);
    EXPECT_EQ(UN(NtUserGetCursor)(), a);
}

/* ---- NtUserGetRequiredCursorSizes(HCURSOR, int out[10]) ---------------------------------------------------- */
#define NEEDSZ() NEED(NtUserGetRequiredCursorSizes)
XT(reqsizes_private_cursor_succeeds) {
    NEEDSZ();
    HCURSOR c = xc_cursor(32, 32, 0, 0, 0);
    CHECK(c != NULL);
    int sz[10]; memset(sz, XFILL, sizeof sz);
    SET_ERR(XSENT);
    EXPECT_EQ(UN(NtUserGetRequiredCursorSizes)(c, sz), 1);
    EXPECT_EQ(GetLastError(), XSENT);
    U(DestroyCursor)(c);
}
XT(reqsizes_writes_all_ten_ints_and_nothing_after) {
    NEEDSZ();
    HCURSOR c = xc_cursor(32, 32, 0, 0, 0);
    int sz[12]; memset(sz, XFILL, sizeof sz);
    CHECK(UN(NtUserGetRequiredCursorSizes)(c, sz) == 1);
    for (int i = 0; i < 10; i++) CHECKF(sz[i] != (int)0xAAAAAAAA, "int %d not written", i);
    EXPECT_EQ((unsigned)sz[10], 0xAAAAAAAAu);
    EXPECT_EQ((unsigned)sz[11], 0xAAAAAAAAu);
    U(DestroyCursor)(c);
}
XT(reqsizes_flags_are_zero_or_one) {
    NEEDSZ();
    HCURSOR c = xc_cursor(32, 32, 0, 0, 0);
    int sz[10]; memset(sz, XFILL, sizeof sz);
    CHECK(UN(NtUserGetRequiredCursorSizes)(c, sz) == 1);
    for (int i = 0; i < 5; i++) CHECKF(sz[2 * i] == 0 || sz[2 * i] == 1, "pair %d flag %d", i, sz[2 * i]);
    U(DestroyCursor)(c);
}
XT(reqsizes_sizes_strictly_increase) {
    NEEDSZ();
    HCURSOR c = xc_cursor(32, 32, 0, 0, 0);
    int sz[10]; memset(sz, XFILL, sizeof sz);
    CHECK(UN(NtUserGetRequiredCursorSizes)(c, sz) == 1);
    for (int i = 1; i < 5; i++) CHECKF(sz[2 * i + 1] > sz[2 * i - 1], "pair %d size %d not above %d", i, sz[2 * i + 1], sz[2 * i - 1]);
    U(DestroyCursor)(c);
}
XT(reqsizes_sizes_are_the_five_cursor_sizes) {
    NEEDSZ();
    HCURSOR c = xc_cursor(32, 32, 0, 0, 0);
    int sz[10]; memset(sz, XFILL, sizeof sz);
    CHECK(UN(NtUserGetRequiredCursorSizes)(c, sz) == 1);
    static const int want[5] = { 32, 48, 64, 96, 128 };
    for (int i = 0; i < 5; i++) EXPECT_EQ(sz[2 * i + 1], want[i]);
    U(DestroyCursor)(c);
}
XT(reqsizes_system_cursor_width_is_the_first_size) {
    NEEDSZ();
    HCURSOR c = xc_cursor(32, 32, 0, 0, 0);
    int sz[10]; memset(sz, XFILL, sizeof sz);
    CHECK(UN(NtUserGetRequiredCursorSizes)(c, sz) == 1);
    int cx = U(GetSystemMetrics)(SM_CXCURSOR), at = -1;
    for (int i = 0; i < 5; i++) if (sz[2 * i + 1] == cx) at = i;
    if (at < 0) { t_skip("the system cursor width is not one of the table sizes"); U(DestroyCursor)(c); return; }
    EXPECT_EQ(at, 0);
    U(DestroyCursor)(c);
}
XT(reqsizes_marks_the_size_matching_the_system_cursor) {
    NEEDSZ();
    HCURSOR c = xc_cursor(32, 32, 0, 0, 0);
    int sz[10]; memset(sz, XFILL, sizeof sz);
    CHECK(UN(NtUserGetRequiredCursorSizes)(c, sz) == 1);
    int cx = U(GetSystemMetrics)(SM_CXCURSOR);
    for (int i = 0; i < 5; i++) if (sz[2 * i + 1] == cx) EXPECT_EQ(sz[2 * i], 1);
    U(DestroyCursor)(c);
}
XT(reqsizes_is_idempotent) {
    NEEDSZ();
    HCURSOR c = xc_cursor(32, 32, 0, 0, 0);
    int a[10], b[10]; memset(a, XFILL, sizeof a); memset(b, 0x11, sizeof b);
    CHECK(UN(NtUserGetRequiredCursorSizes)(c, a) == 1);
    CHECK(UN(NtUserGetRequiredCursorSizes)(c, b) == 1);
    CHECK(!memcmp(a, b, sizeof a));
    U(DestroyCursor)(c);
}
XT(reqsizes_does_not_depend_on_the_cursor_image) {
    NEEDSZ();
    HCURSOR a = xc_cursor(32, 32, 0, 0, 0), b = xc_cursor(16, 16, 4, 4, 0xff);
    if (!b) { t_skip("a 16x16 cursor could not be created"); if (a) U(DestroyCursor)(a); return; }
    int x[10], y[10]; memset(x, XFILL, sizeof x); memset(y, 0x11, sizeof y);
    CHECK(UN(NtUserGetRequiredCursorSizes)(a, x) == 1);
    CHECK(UN(NtUserGetRequiredCursorSizes)(b, y) == 1);
    CHECK(!memcmp(x, y, sizeof x));
    U(DestroyCursor)(a); U(DestroyCursor)(b);
}
XT(reqsizes_stock_cursor_gets_the_same_table) {
    NEEDSZ();
    HCURSOR c = xc_cursor(32, 32, 0, 0, 0), s = xc_stock((int)(ULONG_PTR)IDC_ARROW);
    int x[10], y[10]; memset(x, XFILL, sizeof x); memset(y, 0x11, sizeof y);
    CHECK(UN(NtUserGetRequiredCursorSizes)(c, x) == 1);
    CHECK(UN(NtUserGetRequiredCursorSizes)(s, y) == 1);
    CHECK(!memcmp(x, y, sizeof x));
    U(DestroyCursor)(c);
}
XT(reqsizes_does_not_change_the_current_cursor) {
    NEEDSZ(); NEEDCUR(); XC_CUR_GUARD();
    HANDLE before = UN(NtUserGetCursor)();
    HCURSOR c = xc_cursor(32, 32, 0, 0, 0);
    int x[10];
    UN(NtUserGetRequiredCursorSizes)(c, x);
    EXPECT_EQ(UN(NtUserGetCursor)(), before);
    U(DestroyCursor)(c);
}
static void reqsizes_bad(HCURSOR h, const char *what) {
    int sz[10]; memset(sz, XFILL, sizeof sz);
    SET_ERR(XSENT);
    BOOL r = UN(NtUserGetRequiredCursorSizes)(h, sz);
    DWORD e = GetLastError();
    CHECKF(r == 0, "%s: returned %d", what, r);
    CHECKF(e == XE_INVALID_CURSOR_HANDLE, "%s: last error %lu want 1402", what, e);
    int untouched = 1; for (int i = 0; i < 10; i++) if ((unsigned)sz[i] != 0xAAAAAAAAu) untouched = 0;
    CHECKF(untouched, "%s: the output was written on failure", what);
}
XT(reqsizes_null_cursor_fails) { NEEDSZ(); reqsizes_bad(NULL, "NULL"); }
XT(reqsizes_garbage_handles_fail) {
    NEEDSZ();
    reqsizes_bad((HCURSOR)(ULONG_PTR)1, "1"); reqsizes_bad((HCURSOR)(ULONG_PTR)0x1234, "0x1234"); reqsizes_bad((HCURSOR)(ULONG_PTR)0xFFFFFFFF, "0xFFFFFFFF");
}
XT(reqsizes_destroyed_cursor_fails) {
    NEEDSZ();
    HCURSOR c = xc_cursor(32, 32, 0, 0, 0);
    U(DestroyCursor)(c);
    reqsizes_bad(c, "destroyed");
}
XT(reqsizes_window_handle_fails) {
    NEEDSZ();
    HWND w = xc_hidden_window();
    reqsizes_bad((HCURSOR)w, "window");
    if (w) U(DestroyWindow)(w);
}
XT(reqsizes_menu_handle_fails) {
    NEEDSZ();
    HMENU m = U(CreateMenu)();
    reqsizes_bad((HCURSOR)m, "menu");
    if (m) U(DestroyMenu)(m);
}
XT(reqsizes_null_buffer_returns_false) {
    NEEDSZ();
    HCURSOR c = xc_cursor(32, 32, 0, 0, 0);
    EXPECT_EQ(UN(NtUserGetRequiredCursorSizes)(c, NULL), 0);
    U(DestroyCursor)(c);
}
XT(reqsizes_unwritable_buffers_return_false) {
    NEEDSZ();
    HCURSOR c = xc_cursor(32, 32, 0, 0, 0);
    EXPECT_EQ(UN(NtUserGetRequiredCursorSizes)(c, (int *)(ULONG_PTR)1), 0);
    EXPECT_EQ(UN(NtUserGetRequiredCursorSizes)(c, (int *)0xFFFF800000000000ULL), 0);
    U(DestroyCursor)(c);
}
XT(reqsizes_buffer_ending_at_a_guard_page_is_filled) {
    NEEDSZ();
    char *pg = VirtualAlloc(NULL, 8192, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    CHECK(pg != NULL);
    if (!pg) return;
    DWORD old; VirtualProtect(pg + 4096, 4096, PAGE_NOACCESS, &old);
    int *buf = (int *)(pg + 4096 - 40);
    HCURSOR c = xc_cursor(32, 32, 0, 0, 0);
    EXPECT_EQ(UN(NtUserGetRequiredCursorSizes)(c, buf), 1);
    EXPECT_EQ(buf[1], 32);
    U(DestroyCursor)(c);
    VirtualFree(pg, 0, MEM_RELEASE);
}
XT(reqsizes_buffer_one_int_short_of_the_end_fails) {
    NEEDSZ();
    char *pg = VirtualAlloc(NULL, 8192, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    CHECK(pg != NULL);
    if (!pg) return;
    DWORD old; VirtualProtect(pg + 4096, 4096, PAGE_NOACCESS, &old);
    int *buf = (int *)(pg + 4096 - 36);
    HCURSOR c = xc_cursor(32, 32, 0, 0, 0);
    EXPECT_EQ(UN(NtUserGetRequiredCursorSizes)(c, buf), 0);
    U(DestroyCursor)(c);
    VirtualFree(pg, 0, MEM_RELEASE);
}

/* ---- NtUserLinkDpiCursor(HCURSOR base, HCURSOR dpi, UINT dpi) ---------------------------------------------- */
#define NEEDLINK() NEED(NtUserLinkDpiCursor)
XT(link_zero_dpi_is_invalid_parameter) {
    NEEDLINK();
    HCURSOR a = xc_cursor(32, 32, 0, 0, 0), b = xc_cursor(32, 32, 0, 0, 0);
    XFAIL(UN(NtUserLinkDpiCursor)(a, b, 0), XE_INVALID_PARAMETER);
    U(DestroyCursor)(a); U(DestroyCursor)(b);
}
XT(link_zero_dpi_is_checked_before_the_handles) {
    NEEDLINK();
    XFAIL(UN(NtUserLinkDpiCursor)(NULL, NULL, 0), XE_INVALID_PARAMETER);
    XFAIL(UN(NtUserLinkDpiCursor)((HCURSOR)(ULONG_PTR)0x1234, (HCURSOR)(ULONG_PTR)0x5678, 0), XE_INVALID_PARAMETER);
}
XT(link_plain_cursors_are_refused) {
    NEEDLINK();
    HCURSOR a = xc_cursor(32, 32, 0, 0, 0), b = xc_cursor(32, 32, 0, 0, 0);
    XFAIL(UN(NtUserLinkDpiCursor)(a, b, 96), XE_INVALID_PARAMETER);
    U(DestroyCursor)(a); U(DestroyCursor)(b);
}
XT(link_refused_for_every_common_dpi) {
    NEEDLINK();
    HCURSOR a = xc_cursor(32, 32, 0, 0, 0), b = xc_cursor(32, 32, 0, 0, 0);
    static const UINT dpis[] = { 96, 120, 144, 168, 192, 240, 288, 384, 1, 0x7FFFFFFF, 0xFFFFFFFF };
    for (unsigned i = 0; i < sizeof dpis / sizeof dpis[0]; i++) {
        SET_ERR(XSENT);
        BOOL r = UN(NtUserLinkDpiCursor)(a, b, dpis[i]);
        CHECKF(r == 0 && GetLastError() == XE_INVALID_PARAMETER, "dpi %u: ret %d err %lu", dpis[i], r, GetLastError());
    }
    U(DestroyCursor)(a); U(DestroyCursor)(b);
}
XT(link_cursor_with_itself_is_refused) {
    NEEDLINK();
    HCURSOR a = xc_cursor(32, 32, 0, 0, 0);
    XFAIL(UN(NtUserLinkDpiCursor)(a, a, 96), XE_INVALID_PARAMETER);
    U(DestroyCursor)(a);
}
XT(link_stock_base_is_refused) {
    NEEDLINK();
    HCURSOR b = xc_cursor(32, 32, 0, 0, 0);
    XFAIL(UN(NtUserLinkDpiCursor)(xc_stock((int)(ULONG_PTR)IDC_ARROW), b, 96), XE_INVALID_PARAMETER);
    U(DestroyCursor)(b);
}
XT(link_invalid_base_is_invalid_cursor_handle) {
    NEEDLINK();
    HCURSOR b = xc_cursor(32, 32, 0, 0, 0);
    XFAIL(UN(NtUserLinkDpiCursor)(NULL, b, 96), XE_INVALID_CURSOR_HANDLE);
    XFAIL(UN(NtUserLinkDpiCursor)((HCURSOR)(ULONG_PTR)0x1234, b, 96), XE_INVALID_CURSOR_HANDLE);
    U(DestroyCursor)(b);
}
XT(link_invalid_second_is_invalid_cursor_handle) {
    NEEDLINK();
    HCURSOR a = xc_cursor(32, 32, 0, 0, 0);
    XFAIL(UN(NtUserLinkDpiCursor)(a, NULL, 96), XE_INVALID_CURSOR_HANDLE);
    XFAIL(UN(NtUserLinkDpiCursor)(a, (HCURSOR)(ULONG_PTR)0x1234, 96), XE_INVALID_CURSOR_HANDLE);
    U(DestroyCursor)(a);
}
XT(link_window_handle_is_invalid_cursor_handle) {
    NEEDLINK();
    HCURSOR a = xc_cursor(32, 32, 0, 0, 0);
    HWND w = xc_hidden_window();
    XFAIL(UN(NtUserLinkDpiCursor)((HCURSOR)w, a, 96), XE_INVALID_CURSOR_HANDLE);
    XFAIL(UN(NtUserLinkDpiCursor)(a, (HCURSOR)w, 96), XE_INVALID_CURSOR_HANDLE);
    if (w) U(DestroyWindow)(w);
    U(DestroyCursor)(a);
}
XT(link_destroyed_cursor_is_invalid_cursor_handle) {
    NEEDLINK();
    HCURSOR a = xc_cursor(32, 32, 0, 0, 0), b = xc_cursor(32, 32, 0, 0, 0);
    U(DestroyCursor)(b);
    XFAIL(UN(NtUserLinkDpiCursor)(a, b, 96), XE_INVALID_CURSOR_HANDLE);
    XFAIL(UN(NtUserLinkDpiCursor)(b, a, 96), XE_INVALID_CURSOR_HANDLE);
    U(DestroyCursor)(a);
}
XT(link_failure_leaves_the_size_table_unchanged) {
    NEEDLINK(); NEEDSZ();
    HCURSOR a = xc_cursor(32, 32, 0, 0, 0), b = xc_cursor(32, 32, 0, 0, 0);
    int x[10], y[10]; memset(x, XFILL, sizeof x); memset(y, 0x11, sizeof y);
    UN(NtUserGetRequiredCursorSizes)(a, x);
    UN(NtUserLinkDpiCursor)(a, b, 96);
    UN(NtUserGetRequiredCursorSizes)(a, y);
    CHECK(!memcmp(x, y, sizeof x));
    U(DestroyCursor)(a); U(DestroyCursor)(b);
}

/* ---- NtUserSetCursorContents(HCURSOR dst, HCURSOR src) ----------------------------------------------------- */
#define NEEDSCC() NEED(NtUserSetCursorContents)
XT(contents_copies_the_hotspot) {
    NEEDSCC();
    HCURSOR d = xc_cursor(32, 32, 3, 4, 0), s = xc_cursor(32, 32, 9, 10, 0xff);
    int hx, hy;
    CHECK(xc_mask_dims(d, NULL, NULL, &hx, &hy) && hx == 3 && hy == 4);
    SET_ERR(XSENT);
    EXPECT_EQ(UN(NtUserSetCursorContents)(d, s), 1);
    EXPECT_EQ(GetLastError(), XSENT);
    CHECK(xc_mask_dims(d, NULL, NULL, &hx, &hy));
    EXPECT_EQ(hx, 9); EXPECT_EQ(hy, 10);
    U(DestroyCursor)(d); U(DestroyCursor)(s);
}
XT(contents_consumes_the_source_cursor) {
    NEEDSCC();
    HCURSOR d = xc_cursor(32, 32, 3, 4, 0), s = xc_cursor(32, 32, 9, 10, 0xff);
    EXPECT_EQ(UN(NtUserSetCursorContents)(d, s), 1);
    ICONINFO ii; memset(&ii, 0, sizeof ii);
    CHECK(!U(GetIconInfo)(s, &ii));
    U(DestroyCursor)(d);
}
XT(contents_keeps_the_destination_usable) {
    NEEDSCC();
    HCURSOR d = xc_cursor(32, 32, 3, 4, 0), s = xc_cursor(32, 32, 9, 10, 0xff);
    UN(NtUserSetCursorContents)(d, s);
    int hx;
    CHECK(xc_mask_dims(d, NULL, NULL, &hx, NULL));
    CHECK(U(DestroyCursor)(d));
}
XT(contents_source_is_no_longer_a_valid_cursor_handle) {
    NEEDSCC(); NEEDCUR();
    HCURSOR d = xc_cursor(32, 32, 3, 4, 0), s = xc_cursor(32, 32, 9, 10, 0xff);
    UN(NtUserSetCursorContents)(d, s);
    XFAIL(UN(NtUserSetCursor)(s), XE_INVALID_CURSOR_HANDLE);
    XFAIL(UN(NtUserSetCursorContents)(d, s), XE_INVALID_CURSOR_HANDLE);
    XFAIL(UN(NtUserSetCursorContents)(s, d), XE_INVALID_CURSOR_HANDLE);
    U(DestroyCursor)(d);
}
XT(contents_source_cannot_be_destroyed_afterwards) {
    NEEDSCC();
    HCURSOR d = xc_cursor(32, 32, 3, 4, 0), s = xc_cursor(32, 32, 9, 10, 0xff);
    UN(NtUserSetCursorContents)(d, s);
    CHECK(!U(DestroyCursor)(s));
    U(DestroyCursor)(d);
}
XT(contents_copies_the_size) {
    NEEDSCC();
    HCURSOR d = xc_cursor(32, 32, 1, 1, 0), s = xc_cursor(16, 16, 2, 2, 0xff);
    if (!s) { t_skip("a 16x16 cursor could not be created"); U(DestroyCursor)(d); return; }
    int dw, dh, sw, sh;
    CHECK(xc_mask_dims(d, &dw, &dh, NULL, NULL) && xc_mask_dims(s, &sw, &sh, NULL, NULL));
    CHECK(dw != sw || dh != sh);
    EXPECT_EQ(UN(NtUserSetCursorContents)(d, s), 1);
    CHECK(xc_mask_dims(d, &dw, &dh, NULL, NULL));
    EXPECT_EQ(dw, sw); EXPECT_EQ(dh, sh);
    U(DestroyCursor)(d); U(DestroyCursor)(s);
}
XT(contents_repeated_with_the_same_source_fails_and_keeps_the_copy) {
    NEEDSCC();
    HCURSOR d = xc_cursor(32, 32, 3, 4, 0), s = xc_cursor(32, 32, 9, 10, 0xff);
    EXPECT_EQ(UN(NtUserSetCursorContents)(d, s), 1);
    XFAIL(UN(NtUserSetCursorContents)(d, s), XE_INVALID_CURSOR_HANDLE);
    int hx, hy;
    CHECK(xc_mask_dims(d, NULL, NULL, &hx, &hy));
    EXPECT_EQ(hx, 9); EXPECT_EQ(hy, 10);
    U(DestroyCursor)(d);
}
XT(contents_with_itself_succeeds) {
    NEEDSCC();
    HCURSOR c = xc_cursor(32, 32, 5, 6, 0);
    EXPECT_EQ(UN(NtUserSetCursorContents)(c, c), 1);
    int hx, hy;
    CHECK(xc_mask_dims(c, NULL, NULL, &hx, &hy));
    EXPECT_EQ(hx, 5); EXPECT_EQ(hy, 6);
    U(DestroyCursor)(c);
}
XT(contents_second_source_replaces_the_first_copy) {
    NEEDSCC();
    HCURSOR a = xc_cursor(32, 32, 1, 2, 0), b = xc_cursor(32, 32, 3, 4, 0), c = xc_cursor(32, 32, 5, 6, 0);
    EXPECT_EQ(UN(NtUserSetCursorContents)(a, b), 1);
    EXPECT_EQ(UN(NtUserSetCursorContents)(a, c), 1);
    int hx, hy;
    CHECK(xc_mask_dims(a, NULL, NULL, &hx, &hy)); EXPECT_EQ(hx, 5); EXPECT_EQ(hy, 6);
    U(DestroyCursor)(a);
}
XT(contents_into_the_current_cursor_keeps_it_current) {
    NEEDSCC(); NEEDCUR(); XC_CUR_GUARD();
    HCURSOR d = xc_cursor(32, 32, 3, 4, 0), s = xc_cursor(32, 32, 9, 10, 0xff);
    UN(NtUserSetCursor)(d);
    EXPECT_EQ(UN(NtUserSetCursorContents)(d, s), 1);
    EXPECT_EQ(UN(NtUserGetCursor)(), d);
    UN(NtUserSetCursor)(xc_stock((int)(ULONG_PTR)IDC_ARROW));
    U(DestroyCursor)(d); U(DestroyCursor)(s);
}
XT(contents_from_an_icon_into_a_cursor_succeeds) {
    NEEDSCC();
    BYTE a[128], x[128]; memset(a, 0xff, sizeof a); memset(x, 0, sizeof x);
    HICON ic = U(CreateIcon)(GetModuleHandleA(NULL), 32, 32, 1, 1, a, x);
    HCURSOR c = xc_cursor(32, 32, 5, 5, 0);
    CHECK(ic != NULL);
    if (ic) EXPECT_EQ(UN(NtUserSetCursorContents)(c, ic), 1);
    if (ic) U(DestroyIcon)(ic);
    U(DestroyCursor)(c);
}
XT(contents_into_an_icon_from_a_cursor_succeeds) {
    NEEDSCC();
    BYTE a[128], x[128]; memset(a, 0xff, sizeof a); memset(x, 0, sizeof x);
    HICON ic = U(CreateIcon)(GetModuleHandleA(NULL), 32, 32, 1, 1, a, x);
    HCURSOR c = xc_cursor(32, 32, 5, 5, 0);
    CHECK(ic != NULL);
    if (ic) EXPECT_EQ(UN(NtUserSetCursorContents)((HCURSOR)ic, c), 1);
    if (ic) U(DestroyIcon)(ic);
}
static void contents_bad(HCURSOR d, HCURSOR s, const char *what) {
    SET_ERR(XSENT);
    BOOL r = UN(NtUserSetCursorContents)(d, s);
    DWORD e = GetLastError();
    CHECKF(r == 0, "%s: returned %d", what, r);
    CHECKF(e == XE_INVALID_CURSOR_HANDLE, "%s: last error %lu want 1402", what, e);
}
XT(contents_null_handles_fail) {
    NEEDSCC();
    HCURSOR c = xc_cursor(32, 32, 0, 0, 0);
    contents_bad(NULL, NULL, "NULL,NULL"); contents_bad(NULL, c, "NULL,c"); contents_bad(c, NULL, "c,NULL");
    U(DestroyCursor)(c);
}
XT(contents_garbage_handles_fail) {
    NEEDSCC();
    HCURSOR c = xc_cursor(32, 32, 0, 0, 0);
    contents_bad((HCURSOR)(ULONG_PTR)0x1234, c, "0x1234,c"); contents_bad(c, (HCURSOR)(ULONG_PTR)0x1234, "c,0x1234");
    contents_bad((HCURSOR)(ULONG_PTR)0xFFFFFFFF, (HCURSOR)(ULONG_PTR)0xFFFFFFFF, "-1,-1");
    U(DestroyCursor)(c);
}
XT(contents_window_handles_fail) {
    NEEDSCC();
    HCURSOR c = xc_cursor(32, 32, 0, 0, 0);
    HWND w = xc_hidden_window();
    contents_bad((HCURSOR)w, c, "hwnd,c"); contents_bad(c, (HCURSOR)w, "c,hwnd");
    if (w) U(DestroyWindow)(w);
    U(DestroyCursor)(c);
}
XT(contents_destroyed_handles_fail) {
    NEEDSCC();
    HCURSOR a = xc_cursor(32, 32, 0, 0, 0), b = xc_cursor(32, 32, 0, 0, 0);
    U(DestroyCursor)(b);
    contents_bad(a, b, "a,dead"); contents_bad(b, a, "dead,a");
    U(DestroyCursor)(a);
}
XT(contents_failed_call_does_not_touch_the_destination) {
    NEEDSCC();
    HCURSOR d = xc_cursor(32, 32, 3, 4, 0);
    UN(NtUserSetCursorContents)(d, (HCURSOR)(ULONG_PTR)0x1234);
    int hx, hy;
    CHECK(xc_mask_dims(d, NULL, NULL, &hx, &hy));
    EXPECT_EQ(hx, 3); EXPECT_EQ(hy, 4);
    U(DestroyCursor)(d);
}
XT(user32_setcursorcontents_behaves_like_the_syscall) {
    NEEDSCC();
    SetCursorContents_pfn f = UN(SetCursorContents);
    REQUIRE(f);
    HCURSOR d = xc_cursor(32, 32, 3, 4, 0), s = xc_cursor(32, 32, 9, 10, 0xff);
    EXPECT_EQ(f(d, s), 1);
    int hx, hy;
    CHECK(xc_mask_dims(d, NULL, NULL, &hx, &hy));
    EXPECT_EQ(hx, 9); EXPECT_EQ(hy, 10);
    XFAIL(f((HCURSOR)(ULONG_PTR)0x1234, s), XE_INVALID_CURSOR_HANDLE);
    U(DestroyCursor)(d); U(DestroyCursor)(s);
}
