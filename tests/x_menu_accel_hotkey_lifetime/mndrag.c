/* x_menu_accel_hotkey_lifetime / mndrag: NtUserMNDragOver(POINT *screen_point, void *info) and NtUserMNDragLeave().
 *
 * Both act on the calling thread's menu state. With no menu open on the thread, MNDragOver answers FALSE, writes nothing and sets
 * no last error, and so does MNDragLeave; the pointers are not even looked at. While the thread tracks a drag-and-drop menu
 * (MNS_DRAGDROP), MNDragOver with a point over an item answers TRUE with the last error cleared, and fills the first 0x20 bytes
 * of the output with: a kind word (4), the menu handle, the item index, and the handle of the window that owns the menu (the TrackPopupMenu owner); a point outside the
 * menu answers FALSE (last error cleared) and writes nothing. MNDragLeave inside the menu answers TRUE and leaves the menu open.
 * Unreadable pointers inside a menu are FALSE with the last error untouched and no exception. The calls from another thread or
 * process act on that caller's own (empty) menu state.
 *
 * The menu is a two-item popup at screen position (200, 200) on the thread that calls; the calls are made from a timer inside its
 * modal loop. No input is sent. Determinism: handle values are compared, never printed.
 */
#include "x.h"

struct drag {
    volatile LONG done;
    RECT r0, r1;
    BOOL over[4]; DWORD gle[4]; unsigned char buf[4][0x40];
    BOOL leave; DWORD leave_gle; DWORD flags_after;
    BOOL bad[6]; DWORD bad_gle[6]; unsigned long bad_exc[6];
    BOOL leave2;
    HWND owner; UINT64 q3;
};
struct a2 { POINT p; void *b; int mode; BOOL r; };
static void call_bad(void *q) {
    struct a2 *x = q;
    if (x->mode == 0) x->r = UN(NtUserMNDragOver)(&x->p, x->b);
    else if (x->mode == 1) x->r = UN(NtUserMNDragOver)(NULL, x->b);
    else x->r = UN(NtUserMNDragOver)((POINT *)1, x->b);
}
static void drag_tick(struct xm_menu *m, int n) {
    struct drag *d = m->arg;
    if (n != 2 || d->done) return;
    U(GetMenuItemRect)(NULL, m->menu, 0, &d->r0); U(GetMenuItemRect)(NULL, m->menu, 1, &d->r1);
    POINT pts[4] = { { (d->r0.left + d->r0.right) / 2, (d->r0.top + d->r0.bottom) / 2 }, { (d->r1.left + d->r1.right) / 2, (d->r1.top + d->r1.bottom) / 2 },
                     { d->r0.left - 300, d->r0.top - 300 }, { (d->r0.left + d->r0.right) / 2, (d->r0.top + d->r0.bottom) / 2 } };
    for (int i = 0; i < 4; i++) {
        memset(d->buf[i], 0xCD, sizeof d->buf[i]);
        SET_ERR(XE_SENT);
        d->over[i] = UN(NtUserMNDragOver)(&pts[i], d->buf[i]);
        d->gle[i] = GetLastError();
    }
    { UINT64 q[4]; memcpy(q, d->buf[0], sizeof q); d->q3 = q[3]; d->owner = m->w; }
    SET_ERR(XE_SENT);
    d->leave = UN(NtUserMNDragLeave)(); d->leave_gle = GetLastError();
    d->flags_after = xm_gui_flags(m->tid);
    struct a2 a = { pts[0], NULL, 0, 9 };
    static unsigned char ok[0x40];
    void *bufs[6] = { NULL, (void *)1, (void *)0xFFFF800000000000ULL, ok, ok, NULL };
    int modes[6] = { 0, 0, 0, 1, 2, 1 };
    for (int i = 0; i < 6; i++) {
        a.b = bufs[i]; a.mode = modes[i]; a.r = 9;
        SET_ERR(XE_SENT);
        d->bad_exc[i] = xm_try(call_bad, &a);
        d->bad[i] = a.r; d->bad_gle[i] = GetLastError();
    }
    SET_ERR(XE_SENT);
    d->leave2 = UN(NtUserMNDragLeave)();
    d->done = 1;
}
static int run_drag(struct drag *d, DWORD style, HMENU *menu) {
    memset(d, 0, sizeof *d);
    struct xm_menu m;
    if (!xm_menu_start(&m, style, drag_tick, d)) return 0;
    DWORD t0 = GetTickCount();
    while (!d->done && GetTickCount() - t0 < 4000) Sleep(10);
    if (menu) *menu = m.menu;
    xm_menu_end(&m);
    return d->done;
}
#define DRAG(d) struct drag d; HMENU hm = NULL; \
    if (!run_drag(&d, MNS_DRAGDROP, &hm)) { CHECK(0 && "the menu thread did not run the drag calls"); return; }

/* ---- inside a drag-and-drop menu ---- */
XT(over_the_first_item_is_true_with_the_error_cleared) {
    NEED(NtUserMNDragOver); DRAG(d);
    EXPECT_EQ(d.over[0], 1); EXPECT_EQ(d.gle[0], 0);
}
XT(over_the_first_item_reports_item_zero_and_the_menu) {
    NEED(NtUserMNDragOver); DRAG(d);
    UINT64 q[4]; memcpy(q, d.buf[0], sizeof q);
    EXPECT_EQ((DWORD)q[0], 4);
    EXPECT_EQ((DWORD)q[1], (DWORD)(ULONG_PTR)hm);
    EXPECT_EQ((DWORD)q[2], 0);
}
XT(over_the_second_item_reports_item_one) {
    NEED(NtUserMNDragOver); DRAG(d);
    EXPECT_EQ(d.over[1], 1); EXPECT_EQ(d.gle[1], 0);
    UINT64 q[4]; memcpy(q, d.buf[1], sizeof q);
    EXPECT_EQ((DWORD)q[0], 4);
    EXPECT_EQ((DWORD)q[2], 1);
    EXPECT_EQ((DWORD)q[1], (DWORD)(ULONG_PTR)hm);
}
XT(the_fourth_field_is_the_window_that_owns_the_menu) {
    NEED(NtUserMNDragOver); DRAG(d);
    CHECK(d.owner != NULL);
    EXPECT_EQ((DWORD)d.q3, (DWORD)(ULONG_PTR)d.owner);
}
XT(only_the_first_thirty_two_bytes_are_written) {
    NEED(NtUserMNDragOver); DRAG(d);
    int touched = 0;
    for (int i = 0x20; i < 0x40; i++) if (d.buf[0][i] != 0xCD) touched++;
    EXPECT_EQ(touched, 0);
    int upper = 0; for (int i = 4; i < 8; i++) if (d.buf[0][i] != 0) upper++;
    EXPECT_EQ(upper, 0);   /* the kind and index fields are 32 bits wide in 64-bit slots */
}
XT(outside_the_menu_is_false_with_the_error_cleared_and_nothing_written) {
    NEED(NtUserMNDragOver); DRAG(d);
    EXPECT_EQ(d.over[2], 0); EXPECT_EQ(d.gle[2], 0);
    int touched = 0; for (int i = 0; i < 0x40; i++) if (d.buf[2][i] != 0xCD) touched++;
    EXPECT_EQ(touched, 0);
}
XT(same_point_twice_gives_the_same_answer) {
    NEED(NtUserMNDragOver); DRAG(d);
    EXPECT_EQ(d.over[3], d.over[0]);
    EXPECT_EQ(memcmp(d.buf[0], d.buf[3], 0x20), 0);
}
XT(the_two_item_rects_are_stacked_and_adjacent) {
    NEED(NtUserMNDragOver); DRAG(d);
    EXPECT_EQ(d.r0.bottom, d.r1.top); EXPECT_EQ(d.r0.left, d.r1.left); EXPECT_EQ(d.r0.right, d.r1.right);
    CHECK(d.r0.bottom > d.r0.top);
}
XT(leave_inside_the_menu_is_true_and_keeps_the_error) {
    NEED(NtUserMNDragLeave); DRAG(d);
    EXPECT_EQ(d.leave, 1); EXPECT_EQ(d.leave_gle, XE_SENT);
    EXPECT_EQ(d.leave2, 1);
}
XT(drag_calls_do_not_end_the_menu) {
    NEED(NtUserMNDragOver); DRAG(d);
    EXPECT_EQ(d.flags_after & (GUI_INMENUMODE | GUI_POPUPMENUMODE), GUI_INMENUMODE | GUI_POPUPMENUMODE);
}
XT(unreadable_pointers_inside_a_menu_are_false_without_exception_or_error) {
    NEED(NtUserMNDragOver); DRAG(d);
    const char *what[6] = { "NULL out", "1 out", "kernel out", "NULL point", "1 point", "NULL point, NULL out" };
    for (int i = 0; i < 6; i++) {
        CHECKF(d.bad_exc[i] == 0, "%s: exception 0x%lx", what[i], d.bad_exc[i]);
        CHECKF(d.bad[i] == 0, "%s: returned %d", what[i], d.bad[i]);
        CHECKF(d.bad_gle[i] == XE_SENT, "%s: last error %lu", what[i], d.bad_gle[i]);
    }
}
XT(menu_without_the_drag_drop_style_answers_the_same_for_a_point_outside) {
    NEED(NtUserMNDragOver);
    struct drag d; if (!run_drag(&d, 0, NULL)) { CHECK(0 && "the menu thread did not run the drag calls"); return; }
    EXPECT_EQ(d.over[2], 0);
    int touched = 0; for (int i = 0; i < 0x40; i++) if (d.buf[2][i] != 0xCD) touched++;
    EXPECT_EQ(touched, 0);
}

/* ---- no menu ---- */
XT(drag_over_without_a_menu_is_false_and_writes_nothing) {
    NEED(NtUserMNDragOver);
    k_make_window("xm");
    POINT p = { 250, 210 }; unsigned char b[0x40]; memset(b, 0xCD, sizeof b);
    SET_ERR(XE_SENT);
    EXPECT_EQ(UN(NtUserMNDragOver)(&p, b), 0);
    EXPECT_ERR(XE_SENT);
    int touched = 0; for (int i = 0; i < 0x40; i++) if (b[i] != 0xCD) touched++;
    EXPECT_EQ(touched, 0);
}
XT(drag_leave_without_a_menu_is_false_and_keeps_the_error) {
    NEED(NtUserMNDragLeave);
    k_make_window("xm");
    SET_ERR(XE_SENT);
    EXPECT_EQ(UN(NtUserMNDragLeave)(), 0);
    EXPECT_ERR(XE_SENT);
}
XT(drag_over_without_a_menu_never_reads_the_pointers) {
    NEED(NtUserMNDragOver);
    k_make_window("xm");
    POINT p = { 0, 0 };
    void *bad[] = { NULL, (void *)1, (void *)0xFFFF800000000000ULL };
    for (int i = 0; i < 3; i++) {
        SET_ERR(XE_SENT);
        EXPECT_EQ(UN(NtUserMNDragOver)(&p, bad[i]), 0);
        EXPECT_ERR(XE_SENT);
        SET_ERR(XE_SENT);
        EXPECT_EQ(UN(NtUserMNDragOver)((POINT *)bad[i], bad[i]), 0);
        EXPECT_ERR(XE_SENT);
    }
}
XT(repeated_calls_without_a_menu_stay_false) {
    NEED(NtUserMNDragOver); NEED(NtUserMNDragLeave);
    k_make_window("xm");
    POINT p = { 1, 1 }; unsigned char b[0x20];
    int bad = 0;
    for (int i = 0; i < 100; i++) { if (UN(NtUserMNDragOver)(&p, b)) bad++; if (UN(NtUserMNDragLeave)()) bad++; }
    EXPECT_EQ(bad, 0);
}
static DWORD WINAPI no_menu_thread(LPVOID p) {
    int *r = p; POINT pt = { 250, 210 }; unsigned char b[0x20];
    t_module("user32.dll");
    k_make_window("xm_t");
    r[0] = UN(NtUserMNDragOver)(&pt, b); r[1] = UN(NtUserMNDragLeave)();
    return 0;
}
XT(a_thread_with_no_menu_is_false_even_while_another_thread_tracks_one) {
    NEED(NtUserMNDragOver); NEED(NtUserMNDragLeave);
    struct xm_menu m;
    CHECK(xm_menu_start(&m, MNS_DRAGDROP, NULL, NULL));
    int r[2] = { 9, 9 };
    HANDLE t = CreateThread(NULL, 0, no_menu_thread, r, 0, NULL); WaitForSingleObject(t, 10000); CloseHandle(t);
    EXPECT_EQ(r[0], 0); EXPECT_EQ(r[1], 0);
    POINT p = { 250, 210 }; unsigned char b[0x40]; memset(b, 0xCD, sizeof b);
    SET_ERR(XE_SENT);
    EXPECT_EQ(UN(NtUserMNDragOver)(&p, b), 0);   /* this thread, over the other thread's menu */
    EXPECT_ERR(XE_SENT);
    EXPECT_EQ(b[0], 0xCD);
    CHECK(xm_menu_alive(&m));
    xm_menu_end(&m);
}
CHILD(xm_mndrag_other_process) {
    t_module("user32.dll");
    HWND w = k_make_window("c");
    POINT p = { 250, 210 }; unsigned char b[0x40]; memset(b, 0xCD, sizeof b);
    int bad = 0;
    if (UN(NtUserMNDragOver)(&p, b)) bad++;
    if (UN(NtUserMNDragLeave)()) bad++;
    if (b[0] != 0xCD) bad++;
    if (w) U(DestroyWindow)(w);
    return bad;
}
XT(another_process_is_false_while_a_menu_is_tracked) {
    NEED(NtUserMNDragOver);
    struct xm_menu m;
    CHECK(xm_menu_start(&m, MNS_DRAGDROP, NULL, NULL));
    EXPECT_EQ(t_run_child("xm_mndrag_other_process", "", 30000), 0);
    CHECK(xm_menu_alive(&m));
    xm_menu_end(&m);
}
