/* x_window_chrome_info_queries / sb: NtUserGetScrollBarInfo(hwnd, idObject, SCROLLBARINFO *).
 *
 * Measures, on hidden windows of this process and of a helper thread:
 *  - the argument protocol: only OBJID_VSCROLL (-5), OBJID_HSCROLL (-6) and, for windows of the scroll bar class,
 *    OBJID_CLIENT (-4) are accepted; every other object id is ERROR_INVALID_PARAMETER; cbSize must be exactly
 *    sizeof(SCROLLBARINFO) (0x3c), anything else is ERROR_INVALID_PARAMETER; a stale or garbage window is
 *    ERROR_INVALID_WINDOW_HANDLE; on every failure the output is not written;
 *  - the geometry of the non-client bars as relations to GetWindowRect, GetClientRect and ClientToScreen: the vertical
 *    bar is right of the client area with the client's height, the horizontal bar is below it, both are the system
 *    scroll bar thickness, and share the corner;
 *  - thumb relations: the thumb is measured from the start of the bar, lies between the two line buttons, starts
 *    right behind the first button at the minimum position and ends right before the second at the maximum, never
 *    moves backwards when the position grows, and keeps its length while it moves;
 *  - the state array: arrow buttons and page regions, with the empty page region marked invisible at either end;
 *  - bars the window does not have (no style, hidden, minimized, page covering the range) answer success with an empty
 *    rectangle and an invisible bar state;
 *  - scroll bar controls: the rectangle is the window rectangle in screen coordinates; a disabled control is
 *    unavailable; the size grip answers its own rectangle.
 *
 * Determinism: only symbolic results are printed.
 */
#define XPFX "sb_"
#include "x.h"

#define SB_CB 0x3c
#define OBJ_V ((LONG)-5)
#define OBJ_H ((LONG)-6)
#define OBJ_C ((LONG)-4)

static HWND sbwin(DWORD extra, DWORD ex) {
    return xw_win(WS_OVERLAPPEDWINDOW | extra, ex, L"STATIC");
}
static void setinfo(HWND w, int bar, int mn, int mx, UINT page, int pos) {
    SCROLLINFO si = { sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS, mn, mx, page, pos, 0 };
    U(SetScrollInfo)(w, bar, &si, TRUE);
}
static BOOL q(HWND w, LONG id, SCROLLBARINFO *s) {
    memset(s, XFILL, sizeof *s); s->cbSize = SB_CB;
    SET_ERR(XSENT);
    return R32(UN(NtUserGetScrollBarInfo)(w, id, s)) != 0;
}
static void client_screen(HWND w, RECT *cs) {
    POINT o = { 0, 0 }; RECT cr; U(ClientToScreen)(w, &o); U(GetClientRect)(w, &cr);
    cs->left = o.x; cs->top = o.y; cs->right = o.x + cr.right; cs->bottom = o.y + cr.bottom;
}
static int is_empty_invisible(const SCROLLBARINFO *s) {
    return s->rcScrollBar.left == 0 && s->rcScrollBar.top == 0 && s->rcScrollBar.right == 0 && s->rcScrollBar.bottom == 0 &&
           s->dxyLineButton == 0 && s->xyThumbTop == 0 && s->xyThumbBottom == 0 && s->rgstate[0] == STATE_INVIS &&
           !s->rgstate[1] && !s->rgstate[2] && !s->rgstate[3] && !s->rgstate[4] && !s->rgstate[5];
}

/* ---- argument protocol ---- */
static void bad_id(LONG id) {
    HWND w = sbwin(WS_VSCROLL | WS_HSCROLL, 0);
    SCROLLBARINFO s; memset(&s, XFILL, sizeof s); s.cbSize = SB_CB;
    SET_ERR(XSENT);
    ULONG_PTR r = UN(NtUserGetScrollBarInfo)(w, id, &s);
    CHECKF(R32(r) == 0 && GetLastError() == XE_INVALID_PARAMETER, "id %ld: r=%lu gle=%lu", (long)id, (unsigned long)R32(r), GetLastError());
    CHECKF(xw_untouched((char *)&s + 4, sizeof s - 4), "id %ld: output written", (long)id);
    xw_kill(w);
}
XT(id_everything_but_the_scroll_bar_objects_is_invalid) {
    NEED(NtUserGetScrollBarInfo);
    static const LONG ids[] = { -9, -8, -7, -3, -2, -1, 0, 1, 2, 3, 100, -100, 0x7fffffff, (LONG)0x80000000 };
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++) bad_id(ids[i]);
}
XT(id_client_on_non_scrollbar_class_fails_quietly) {
    NEED(NtUserGetScrollBarInfo);
    HWND w = sbwin(WS_VSCROLL | WS_HSCROLL, 0);
    SCROLLBARINFO s; memset(&s, XFILL, sizeof s); s.cbSize = SB_CB;
    SET_ERR(XSENT);
    EXPECT_EQ(R32(UN(NtUserGetScrollBarInfo)(w, OBJ_C, &s)), 0);
    EXPECT_ERR(XSENT);   /* an object id that is valid but not available on the class sets no error */
    CHECK(xw_untouched((char *)&s + 4, sizeof s - 4));
    xw_kill(w);
}

static void bad_cb(DWORD cb) {
    HWND w = sbwin(WS_VSCROLL, 0);
    union { SCROLLBARINFO s; unsigned char raw[0x100]; } u; memset(&u, XFILL, sizeof u); u.s.cbSize = cb;
    SET_ERR(XSENT);
    ULONG_PTR r = UN(NtUserGetScrollBarInfo)(w, OBJ_V, &u.s);
    CHECKF(R32(r) == 0 && GetLastError() == XE_INVALID_PARAMETER, "cbSize 0x%lx: r=%lu gle=%lu", (unsigned long)cb, (unsigned long)R32(r), GetLastError());
    CHECKF(u.s.cbSize == cb && xw_untouched(u.raw + 4, sizeof u.raw - 4), "cbSize 0x%lx: output written", (unsigned long)cb);
    xw_kill(w);
}
XT(cbsize_anything_but_0x3c_is_invalid) {
    NEED(NtUserGetScrollBarInfo);
    static const DWORD cbs[] = { 0, 1, 4, 0x3b, 0x3d, 0x40, 0x78, 0x3c0000, 0xFFFFFFFFu, 0x1000 };
    for (unsigned i = 0; i < sizeof cbs / sizeof cbs[0]; i++) bad_cb(cbs[i]);
}
XT(cbsize_exact_accepted_and_kept) {
    NEED(NtUserGetScrollBarInfo);
    HWND w = sbwin(WS_VSCROLL, 0);
    SCROLLBARINFO s; CHECK(q(w, OBJ_V, &s)); EXPECT_EQ(s.cbSize, SB_CB);
    xw_kill(w);
}
XT(struct_size_is_0x3c) { EXPECT_EQ(sizeof(SCROLLBARINFO), SB_CB); }

static void bad_hwnd(HWND h, const char *what) {
    SCROLLBARINFO s; memset(&s, XFILL, sizeof s); s.cbSize = SB_CB;
    SET_ERR(XSENT); ULONG_PTR r = UN(NtUserGetScrollBarInfo)(h, OBJ_V, &s);
    CHECKF(R32(r) == 0 && GetLastError() == XE_INVALID_HWND, "%s: r=%lu gle=%lu", what, (unsigned long)R32(r), GetLastError());
    CHECKF(xw_untouched((char *)&s + 4, sizeof s - 4), "%s: output written", what);
    SET_ERR(XSENT); r = UN(NtUserGetScrollBarInfo)(h, OBJ_H, &s);
    CHECKF(R32(r) == 0 && GetLastError() == XE_INVALID_HWND, "%s (horizontal): gle=%lu", what, GetLastError());
}
XT(hwnd_stale_or_garbage_is_invalid_window_handle) {
    NEED(NtUserGetScrollBarInfo);
    bad_hwnd(NULL, "null"); bad_hwnd((HWND)(ULONG_PTR)0x12345678, "garbage"); bad_hwnd((HWND)INVALID_HANDLE_VALUE, "invalid handle value");
    bad_hwnd(xw_dead(), "destroyed"); bad_hwnd((HWND)(ULONG_PTR)1, "low index");
}
XT(hwnd_checked_before_id) {
    NEED(NtUserGetScrollBarInfo);
    SCROLLBARINFO s; memset(&s, XFILL, sizeof s); s.cbSize = SB_CB;
    XFAIL(UN(NtUserGetScrollBarInfo)(NULL, 77, &s), XE_INVALID_HWND);
}
XT(out_null) {
    NEED(NtUserGetScrollBarInfo);
    HWND w = sbwin(WS_VSCROLL, 0);
    XFAIL(UN(NtUserGetScrollBarInfo)(w, OBJ_V, NULL), XE_NOACCESS);
    xw_kill(w);
}
XT(out_unwritable) {
    NEED(NtUserGetScrollBarInfo);
    HWND w = sbwin(WS_VSCROLL, 0);
    XFAIL(UN(NtUserGetScrollBarInfo)(w, OBJ_V, XP_BADPTR), XE_NOACCESS);
    XFAIL(UN(NtUserGetScrollBarInfo)(w, OBJ_V, XP_KERNELPTR), XE_NOACCESS);
    xw_kill(w);
}

/* ---- geometry of the non-client bars ---- */
XT(vertical_bar_beside_client) {
    NEED(NtUserGetScrollBarInfo);
    HWND w = sbwin(WS_VSCROLL | WS_HSCROLL, 0); setinfo(w, SB_VERT, 0, 100, 10, 0);
    SCROLLBARINFO s; RECT cs; XOK(q(w, OBJ_V, &s)); client_screen(w, &cs);
    EXPECT_EQ(s.rcScrollBar.left, cs.right);
    EXPECT_EQ(s.rcScrollBar.top, cs.top);
    EXPECT_EQ(s.rcScrollBar.bottom, cs.bottom);
    xw_kill(w);
}
XT(vertical_bar_width_is_system_scroll_width) {
    NEED(NtUserGetScrollBarInfo);
    HWND w = sbwin(WS_VSCROLL | WS_HSCROLL, 0); setinfo(w, SB_VERT, 0, 100, 10, 0);
    SCROLLBARINFO s; XOK(q(w, OBJ_V, &s));
    EXPECT_EQ(s.rcScrollBar.right - s.rcScrollBar.left, U(GetSystemMetrics)(SM_CXVSCROLL));
    xw_kill(w);
}
XT(horizontal_bar_below_client) {
    NEED(NtUserGetScrollBarInfo);
    HWND w = sbwin(WS_VSCROLL | WS_HSCROLL, 0); setinfo(w, SB_HORZ, 0, 100, 10, 0);
    SCROLLBARINFO s; RECT cs; XOK(q(w, OBJ_H, &s)); client_screen(w, &cs);
    EXPECT_EQ(s.rcScrollBar.left, cs.left);
    EXPECT_EQ(s.rcScrollBar.top, cs.bottom);
    xw_kill(w);
}
XT(horizontal_bar_height_is_system_scroll_height) {
    NEED(NtUserGetScrollBarInfo);
    HWND w = sbwin(WS_VSCROLL | WS_HSCROLL, 0); setinfo(w, SB_HORZ, 0, 100, 10, 0);
    SCROLLBARINFO s; XOK(q(w, OBJ_H, &s));
    EXPECT_EQ(s.rcScrollBar.bottom - s.rcScrollBar.top, U(GetSystemMetrics)(SM_CYHSCROLL));
    xw_kill(w);
}
XT(bars_share_the_corner) {
    NEED(NtUserGetScrollBarInfo);
    HWND w = sbwin(WS_VSCROLL | WS_HSCROLL, 0); setinfo(w, SB_VERT, 0, 100, 10, 0); setinfo(w, SB_HORZ, 0, 100, 10, 0);
    SCROLLBARINFO v, h; XOK(q(w, OBJ_V, &v)); XOK(q(w, OBJ_H, &h));
    EXPECT_EQ(h.rcScrollBar.top, v.rcScrollBar.bottom);       /* the horizontal bar starts where the vertical bar ends */
    EXPECT_EQ(h.rcScrollBar.right, v.rcScrollBar.left);       /* but stops at it: the corner square belongs to neither bar */
    xw_kill(w);
}
XT(bars_inside_the_window_rectangle) {
    NEED(NtUserGetScrollBarInfo);
    HWND w = sbwin(WS_VSCROLL | WS_HSCROLL, 0); setinfo(w, SB_VERT, 0, 100, 10, 0); setinfo(w, SB_HORZ, 0, 100, 10, 0);
    SCROLLBARINFO v, h; RECT wr; XOK(q(w, OBJ_V, &v)); XOK(q(w, OBJ_H, &h)); U(GetWindowRect)(w, &wr);
    const RECT *r[2] = { &v.rcScrollBar, &h.rcScrollBar };
    for (int i = 0; i < 2; i++)
        CHECKF(r[i]->left >= wr.left && r[i]->top >= wr.top && r[i]->right <= wr.right && r[i]->bottom <= wr.bottom, "bar %d outside the window", i);
    xw_kill(w);
}
XT(only_vertical_bar_spans_client_height) {
    NEED(NtUserGetScrollBarInfo);
    HWND w = sbwin(WS_VSCROLL, 0); setinfo(w, SB_VERT, 0, 100, 10, 0);
    SCROLLBARINFO s; RECT cs; XOK(q(w, OBJ_V, &s)); client_screen(w, &cs);
    EXPECT_EQ(s.rcScrollBar.top, cs.top);
    EXPECT_EQ(s.rcScrollBar.bottom, cs.bottom);
    EXPECT_EQ(s.rcScrollBar.left, cs.right);
    SCROLLBARINFO h; XOK(q(w, OBJ_H, &h)); CHECK(is_empty_invisible(&h));
    xw_kill(w);
}
XT(only_horizontal_bar_spans_client_width) {
    NEED(NtUserGetScrollBarInfo);
    HWND w = sbwin(WS_HSCROLL, 0); setinfo(w, SB_HORZ, 0, 100, 10, 0);
    SCROLLBARINFO s; RECT cs; XOK(q(w, OBJ_H, &s)); client_screen(w, &cs);
    EXPECT_EQ(s.rcScrollBar.left, cs.left);
    EXPECT_EQ(s.rcScrollBar.right, cs.right);
    EXPECT_EQ(s.rcScrollBar.top, cs.bottom);
    SCROLLBARINFO v; XOK(q(w, OBJ_V, &v)); CHECK(is_empty_invisible(&v));
    xw_kill(w);
}
XT(bar_follows_window_move) {
    NEED(NtUserGetScrollBarInfo);
    HWND w = sbwin(WS_VSCROLL, 0); setinfo(w, SB_VERT, 0, 100, 10, 0);
    SCROLLBARINFO a, b; XOK(q(w, OBJ_V, &a));
    U(SetWindowPos)(w, NULL, 50, 70, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    XOK(q(w, OBJ_V, &b));
    RECT wr; U(GetWindowRect)(w, &wr);
    EXPECT_EQ(b.rcScrollBar.left - a.rcScrollBar.left, wr.left - 10);
    EXPECT_EQ(b.rcScrollBar.top - a.rcScrollBar.top, wr.top - 10);
    EXPECT_EQ(b.rcScrollBar.bottom - b.rcScrollBar.top, a.rcScrollBar.bottom - a.rcScrollBar.top);
    EXPECT_EQ(b.xyThumbTop, a.xyThumbTop);   /* the thumb is relative to the bar, not to the screen */
    xw_kill(w);
}
XT(bar_follows_window_resize) {
    NEED(NtUserGetScrollBarInfo);
    HWND w = sbwin(WS_VSCROLL, 0); setinfo(w, SB_VERT, 0, 100, 10, 0);
    SCROLLBARINFO a, b; XOK(q(w, OBJ_V, &a));
    U(SetWindowPos)(w, NULL, 0, 0, 200, 250, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    XOK(q(w, OBJ_V, &b));
    EXPECT_EQ((b.rcScrollBar.bottom - b.rcScrollBar.top) - (a.rcScrollBar.bottom - a.rcScrollBar.top), 100);
    EXPECT_EQ(b.rcScrollBar.right, a.rcScrollBar.right);
    xw_kill(w);
}
XT(left_scrollbar_style_moves_vertical_bar_to_client_left) {
    NEED(NtUserGetScrollBarInfo);
    HWND w = sbwin(WS_VSCROLL, WS_EX_LEFTSCROLLBAR); setinfo(w, SB_VERT, 0, 100, 10, 0);
    SCROLLBARINFO s; RECT cs; XOK(q(w, OBJ_V, &s)); client_screen(w, &cs);
    EXPECT_EQ(s.rcScrollBar.right, cs.left);
    EXPECT_EQ(s.rcScrollBar.right - s.rcScrollBar.left, U(GetSystemMetrics)(SM_CXVSCROLL));
    xw_kill(w);
}
XT(reserved_is_zero) {
    NEED(NtUserGetScrollBarInfo);
    HWND w = sbwin(WS_VSCROLL | WS_HSCROLL, 0); setinfo(w, SB_VERT, 0, 100, 10, 5); setinfo(w, SB_HORZ, 0, 100, 10, 5);
    SCROLLBARINFO s; XOK(q(w, OBJ_V, &s)); EXPECT_EQ(s.reserved, 0);
    XOK(q(w, OBJ_H, &s)); EXPECT_EQ(s.reserved, 0);
    xw_kill(w);
}
XT(line_button_is_system_arrow_size) {
    NEED(NtUserGetScrollBarInfo);
    HWND w = sbwin(WS_VSCROLL | WS_HSCROLL, 0); setinfo(w, SB_VERT, 0, 100, 10, 5); setinfo(w, SB_HORZ, 0, 100, 10, 5);
    SCROLLBARINFO v, h; XOK(q(w, OBJ_V, &v)); XOK(q(w, OBJ_H, &h));
    EXPECT_EQ(v.dxyLineButton, U(GetSystemMetrics)(SM_CYVSCROLL));
    EXPECT_EQ(h.dxyLineButton, U(GetSystemMetrics)(SM_CXHSCROLL));
    xw_kill(w);
}

/* ---- thumb relations ---- */
static int barlen(const SCROLLBARINFO *s, int vert) { return vert ? s->rcScrollBar.bottom - s->rcScrollBar.top : s->rcScrollBar.right - s->rcScrollBar.left; }

static void thumb_min_max(int vert) {
    NEED(NtUserGetScrollBarInfo);
    HWND w = sbwin(vert ? WS_VSCROLL : WS_HSCROLL, 0);
    int bar = vert ? SB_VERT : SB_HORZ; LONG id = vert ? OBJ_V : OBJ_H;
    SCROLLBARINFO s;
    setinfo(w, bar, 0, 100, 10, 0); XOK(q(w, id, &s));
    EXPECT_EQ(s.xyThumbTop, s.dxyLineButton);                            /* right behind the first line button */
    CHECK(s.rgstate[2] == STATE_INVIS);                                  /* nothing before the thumb */
    CHECK(s.rgstate[4] == 0);
    CHECK(s.xyThumbBottom > s.xyThumbTop);
    setinfo(w, bar, 0, 100, 10, 91); XOK(q(w, id, &s));                  /* the largest position with a page of 10 */
    EXPECT_EQ(s.xyThumbBottom, barlen(&s, vert) - s.dxyLineButton);      /* right before the second line button */
    CHECK(s.rgstate[4] == STATE_INVIS);
    CHECK(s.rgstate[2] == 0);
    setinfo(w, bar, 0, 100, 10, 40); XOK(q(w, id, &s));
    CHECK(s.rgstate[2] == 0 && s.rgstate[4] == 0);
    CHECK(s.xyThumbTop > s.dxyLineButton && s.xyThumbBottom < barlen(&s, vert) - s.dxyLineButton);
    xw_kill(w);
}
XT(thumb_at_minimum_and_maximum_vertical) { thumb_min_max(1); }
XT(thumb_at_minimum_and_maximum_horizontal) { thumb_min_max(0); }

static void thumb_monotonic(int vert) {
    NEED(NtUserGetScrollBarInfo);
    HWND w = sbwin(vert ? WS_VSCROLL : WS_HSCROLL, 0);
    int bar = vert ? SB_VERT : SB_HORZ; LONG id = vert ? OBJ_V : OBJ_H;
    SCROLLBARINFO s; int prev = -1, len0 = -1, first = 0, last = 0, ok = 1, lenok = 1;
    for (int pos = 0; pos <= 91; pos += 7) {
        setinfo(w, bar, 0, 100, 10, pos); XOK(q(w, id, &s));
        if (prev >= 0 && s.xyThumbTop < prev) ok = 0;
        if (len0 < 0) { len0 = s.xyThumbBottom - s.xyThumbTop; first = s.xyThumbTop; }
        else if (s.xyThumbBottom - s.xyThumbTop != len0) lenok = 0;
        prev = s.xyThumbTop; last = s.xyThumbTop;
    }
    CHECK(ok); CHECK(lenok); CHECK(last > first);
    xw_kill(w);
}
XT(thumb_never_moves_backwards_vertical) { thumb_monotonic(1); }
XT(thumb_never_moves_backwards_horizontal) { thumb_monotonic(0); }

static void thumb_inside(int vert, int page, int pos) {
    NEED(NtUserGetScrollBarInfo);
    HWND w = sbwin(vert ? WS_VSCROLL : WS_HSCROLL, 0);
    int bar = vert ? SB_VERT : SB_HORZ; LONG id = vert ? OBJ_V : OBJ_H;
    SCROLLBARINFO s; setinfo(w, bar, 0, 100, page, pos); XOK(q(w, id, &s));
    CHECKF(s.xyThumbTop >= s.dxyLineButton, "top %d", s.xyThumbTop);
    CHECKF(s.xyThumbBottom <= barlen(&s, vert) - s.dxyLineButton, "bottom %d", s.xyThumbBottom);
    CHECK(s.xyThumbTop < s.xyThumbBottom);
    xw_kill(w);
}
XT(thumb_inside_the_track_for_page_and_position_grid) {
    static const int grid[][2] = { {0,0}, {0,50}, {0,100}, {1,99}, {10,45}, {50,50}, {99,1} };
    for (unsigned i = 0; i < sizeof grid / sizeof grid[0]; i++) { thumb_inside(1, grid[i][0], grid[i][1]); thumb_inside(0, grid[i][0], grid[i][1]); }
}

XT(page_zero_thumb_has_system_thumb_size_vertical) {
    NEED(NtUserGetScrollBarInfo);
    HWND w = sbwin(WS_VSCROLL, 0); SCROLLBARINFO s;
    setinfo(w, SB_VERT, 0, 100, 0, 30); XOK(q(w, OBJ_V, &s));
    EXPECT_EQ(s.xyThumbBottom - s.xyThumbTop, U(GetSystemMetrics)(SM_CYVTHUMB));
    xw_kill(w);
}
XT(page_zero_thumb_has_system_thumb_size_horizontal) {
    NEED(NtUserGetScrollBarInfo);
    HWND w = sbwin(WS_HSCROLL, 0); SCROLLBARINFO s;
    setinfo(w, SB_HORZ, 0, 100, 0, 30); XOK(q(w, OBJ_H, &s));
    EXPECT_EQ(s.xyThumbBottom - s.xyThumbTop, U(GetSystemMetrics)(SM_CXHTHUMB));
    xw_kill(w);
}
XT(thumb_grows_with_page_size) {
    NEED(NtUserGetScrollBarInfo);
    HWND w = sbwin(WS_HSCROLL, 0); SCROLLBARINFO s; int prev = 0, ok = 1;
    static const UINT pages[] = { 1, 5, 10, 20, 40, 80 };
    for (unsigned i = 0; i < 6; i++) {
        setinfo(w, SB_HORZ, 0, 100, pages[i], 0); XOK(q(w, OBJ_H, &s));
        int len = s.xyThumbBottom - s.xyThumbTop;
        if (len < prev) ok = 0;
        prev = len;
    }
    CHECK(ok);
    CHECK(prev > s.dxyLineButton);   /* a page of 80 of 100 is a long thumb */
    xw_kill(w);
}
XT(thumb_scales_with_range_not_with_window) {
    NEED(NtUserGetScrollBarInfo);
    HWND w = sbwin(WS_VSCROLL, 0); SCROLLBARINFO a, b;
    setinfo(w, SB_VERT, 0, 100, 10, 0); XOK(q(w, OBJ_V, &a));
    setinfo(w, SB_VERT, 0, 1000, 100, 0); XOK(q(w, OBJ_V, &b));
    EXPECT_EQ(a.xyThumbBottom - a.xyThumbTop, b.xyThumbBottom - b.xyThumbTop);   /* the same page fraction gives the same thumb */
    xw_kill(w);
}
XT(negative_range_works_like_zero_based) {
    NEED(NtUserGetScrollBarInfo);
    HWND w = sbwin(WS_VSCROLL, 0); SCROLLBARINFO a, b;
    setinfo(w, SB_VERT, 0, 100, 10, 30); XOK(q(w, OBJ_V, &a));
    setinfo(w, SB_VERT, -50, 50, 10, -20); XOK(q(w, OBJ_V, &b));
    EXPECT_EQ(a.xyThumbTop, b.xyThumbTop);
    EXPECT_EQ(a.xyThumbBottom, b.xyThumbBottom);
    xw_kill(w);
}

/* ---- the state array ---- */
XT(states_of_a_plain_visible_bar_are_clear) {
    NEED(NtUserGetScrollBarInfo);
    HWND w = sbwin(WS_VSCROLL | WS_HSCROLL, 0); setinfo(w, SB_VERT, 0, 100, 10, 40); setinfo(w, SB_HORZ, 0, 100, 10, 40);
    SCROLLBARINFO s;
    XOK(q(w, OBJ_V, &s)); for (int i = 0; i < 6; i++) CHECKF(s.rgstate[i] == 0, "vertical state[%d]", i);
    XOK(q(w, OBJ_H, &s)); for (int i = 0; i < 6; i++) CHECKF(s.rgstate[i] == 0, "horizontal state[%d]", i);
    xw_kill(w);
}
XT(disable_both_arrows) {
    NEED(NtUserGetScrollBarInfo);
    HWND w = sbwin(WS_VSCROLL, 0); setinfo(w, SB_VERT, 0, 100, 10, 40);
    U(EnableScrollBar)(w, SB_VERT, ESB_DISABLE_BOTH);
    SCROLLBARINFO s; XOK(q(w, OBJ_V, &s));
    CHECK(s.rgstate[1] & STATE_UNAVAIL); CHECK(s.rgstate[5] & STATE_UNAVAIL);
    CHECK(s.rgstate[0] & STATE_UNAVAIL);
    U(EnableScrollBar)(w, SB_VERT, ESB_ENABLE_BOTH);
    XOK(q(w, OBJ_V, &s)); for (int i = 0; i < 6; i++) CHECKF(s.rgstate[i] == 0, "state[%d] after enabling", i);
    xw_kill(w);
}
XT(disable_up_arrow_only) {
    NEED(NtUserGetScrollBarInfo);
    HWND w = sbwin(WS_VSCROLL, 0); setinfo(w, SB_VERT, 0, 100, 10, 40);
    U(EnableScrollBar)(w, SB_VERT, ESB_DISABLE_UP);
    SCROLLBARINFO s; XOK(q(w, OBJ_V, &s));
    CHECK(s.rgstate[1] & STATE_UNAVAIL); CHECK(!(s.rgstate[5] & STATE_UNAVAIL));
    xw_kill(w);
}
XT(disable_down_arrow_only) {
    NEED(NtUserGetScrollBarInfo);
    HWND w = sbwin(WS_HSCROLL, 0); setinfo(w, SB_HORZ, 0, 100, 10, 40);
    U(EnableScrollBar)(w, SB_HORZ, ESB_DISABLE_DOWN);
    SCROLLBARINFO s; XOK(q(w, OBJ_H, &s));
    CHECK(!(s.rgstate[1] & STATE_UNAVAIL)); CHECK(s.rgstate[5] & STATE_UNAVAIL);
    xw_kill(w);
}

/* ---- bars the window does not show ---- */
XT(no_vertical_style_gives_empty_invisible_bar) {
    NEED(NtUserGetScrollBarInfo);
    HWND w = sbwin(WS_HSCROLL, 0); SCROLLBARINFO s;
    XOK(q(w, OBJ_V, &s)); CHECK(is_empty_invisible(&s)); EXPECT_EQ(s.cbSize, SB_CB); EXPECT_EQ(s.reserved, 0);
    xw_kill(w);
}
XT(no_horizontal_style_gives_empty_invisible_bar) {
    NEED(NtUserGetScrollBarInfo);
    HWND w = sbwin(WS_VSCROLL, 0); SCROLLBARINFO s;
    XOK(q(w, OBJ_H, &s)); CHECK(is_empty_invisible(&s));
    xw_kill(w);
}
XT(window_without_bars_gives_empty_invisible_bars) {
    NEED(NtUserGetScrollBarInfo);
    HWND w = sbwin(0, 0); SCROLLBARINFO s;
    XOK(q(w, OBJ_V, &s)); CHECK(is_empty_invisible(&s));
    XOK(q(w, OBJ_H, &s)); CHECK(is_empty_invisible(&s));
    xw_kill(w);
}
XT(popup_without_bars_gives_empty_invisible_bars) {
    NEED(NtUserGetScrollBarInfo);
    HWND w = xw_win(WS_POPUP, 0, L"STATIC"); SCROLLBARINFO s;
    XOK(q(w, OBJ_V, &s)); CHECK(is_empty_invisible(&s));
    XOK(q(w, OBJ_H, &s)); CHECK(is_empty_invisible(&s));
    xw_kill(w);
}
XT(hiding_a_bar_empties_it_and_keeps_the_other) {
    NEED(NtUserGetScrollBarInfo);
    HWND w = sbwin(WS_VSCROLL | WS_HSCROLL, 0); setinfo(w, SB_VERT, 0, 100, 10, 40); setinfo(w, SB_HORZ, 0, 100, 10, 40);
    SCROLLBARINFO h0, s; XOK(q(w, OBJ_H, &h0));
    U(ShowScrollBar)(w, SB_VERT, FALSE);
    XOK(q(w, OBJ_V, &s)); CHECK(is_empty_invisible(&s));
    XOK(q(w, OBJ_H, &s)); CHECK(!is_empty_invisible(&s)); EXPECT_EQ(s.rgstate[0], 0);
    U(ShowScrollBar)(w, SB_HORZ, FALSE);
    XOK(q(w, OBJ_H, &s)); CHECK(is_empty_invisible(&s));
    U(ShowScrollBar)(w, SB_BOTH, TRUE);
    XOK(q(w, OBJ_V, &s)); CHECK(!is_empty_invisible(&s));
    xw_kill(w);
}
XT(bar_style_removal_empties_the_bar) {
    NEED(NtUserGetScrollBarInfo);
    HWND w = sbwin(WS_VSCROLL, 0); setinfo(w, SB_VERT, 0, 100, 10, 40);
    SCROLLBARINFO s; XOK(q(w, OBJ_V, &s)); CHECK(!is_empty_invisible(&s));
    U(SetWindowLongW)(w, GWL_STYLE, U(GetWindowLongW)(w, GWL_STYLE) & ~WS_VSCROLL);
    XOK(q(w, OBJ_V, &s)); CHECK(is_empty_invisible(&s));
    xw_kill(w);
}
XT(window_created_minimized_reports_bars_offscreen) {
    NEED(NtUserGetScrollBarInfo);
    HWND w = xw_win(WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_HSCROLL | WS_MINIMIZE, 0, L"STATIC");
    SCROLLBARINFO s; XOK(q(w, OBJ_V, &s));
    EXPECT_EQ(s.rgstate[0], STATE_OFFSCREEN);
    CHECK(s.rcScrollBar.left == 0 && s.rcScrollBar.top == 0 && s.rcScrollBar.right == 0 && s.rcScrollBar.bottom == 0);
    XOK(q(w, OBJ_H, &s)); EXPECT_EQ(s.rgstate[0], STATE_OFFSCREEN);
    xw_kill(w);
}
XT(page_covering_range_removes_the_bar) {
    NEED(NtUserGetScrollBarInfo);
    HWND w = sbwin(WS_VSCROLL, 0); setinfo(w, SB_VERT, 0, 100, 10, 0);
    SCROLLBARINFO s; XOK(q(w, OBJ_V, &s)); CHECK(!is_empty_invisible(&s));
    setinfo(w, SB_VERT, 0, 100, 200, 0);
    CHECK(!(U(GetWindowLongW)(w, GWL_STYLE) & WS_VSCROLL));   /* nothing to scroll: user32 drops the style */
    XOK(q(w, OBJ_V, &s)); CHECK(is_empty_invisible(&s));
    xw_kill(w);
}
XT(page_covering_range_with_disable_no_scroll_keeps_an_unavailable_bar) {
    NEED(NtUserGetScrollBarInfo);
    HWND w = sbwin(WS_VSCROLL, 0);
    SCROLLINFO si = { sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL, 0, 100, 200, 0, 0 };
    U(SetScrollInfo)(w, SB_VERT, &si, TRUE);
    CHECK(U(GetWindowLongW)(w, GWL_STYLE) & WS_VSCROLL);
    SCROLLBARINFO s; XOK(q(w, OBJ_V, &s));
    CHECK(!is_empty_invisible(&s));
    CHECK(s.rgstate[0] & STATE_UNAVAIL);
    xw_kill(w);
}

/* ---- window of another thread, user32 front door, input independence ---- */
XT(window_of_another_thread_is_queried_like_a_local_one) {
    NEED(NtUserGetScrollBarInfo);
    struct xw_other o;
    if (!xw_other_start(&o, WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_HSCROLL, 0, L"STATIC")) { t_skip("helper thread window"); return; }
    SCROLLBARINFO v, h; RECT cs;
    XOK(q(o.hwnd, OBJ_V, &v)); XOK(q(o.hwnd, OBJ_H, &h)); client_screen(o.hwnd, &cs);
    EXPECT_EQ(v.rcScrollBar.left, cs.right);
    EXPECT_EQ(h.rcScrollBar.top, cs.bottom);
    EXPECT_EQ(h.rcScrollBar.top, v.rcScrollBar.bottom);
    xw_other_stop(&o);
}
XT(user32_front_door_matches) {
    NEED(NtUserGetScrollBarInfo);
    HWND w = sbwin(WS_VSCROLL | WS_HSCROLL, 0); setinfo(w, SB_VERT, 0, 100, 10, 33);
    SCROLLBARINFO a, b; XOK(q(w, OBJ_V, &a));
    memset(&b, XFILL, sizeof b); b.cbSize = SB_CB;
    CHECK(U(GetScrollBarInfo)(w, OBJID_VSCROLL, &b));
    CHECK(!memcmp(&a, &b, sizeof a));
    xw_kill(w);
}
XT(user32_front_door_wrong_size_fails) {
    NEED(NtUserGetScrollBarInfo);
    HWND w = sbwin(WS_VSCROLL, 0); union { SCROLLBARINFO s; unsigned char raw[0x80]; } u; memset(&u, 0, sizeof u); u.s.cbSize = SB_CB + 4;
    SET_ERR(XSENT); CHECK(!U(GetScrollBarInfo)(w, OBJID_VSCROLL, &u.s)); EXPECT_ERR(XE_INVALID_PARAMETER);
    xw_kill(w);
}
XT(output_does_not_depend_on_caller_garbage) {
    NEED(NtUserGetScrollBarInfo);
    HWND w = sbwin(WS_VSCROLL | WS_HSCROLL, 0); setinfo(w, SB_VERT, 0, 100, 10, 33);
    SCROLLBARINFO a, b; memset(&a, 0xFF, sizeof a); a.cbSize = SB_CB; memset(&b, 0, sizeof b); b.cbSize = SB_CB;
    XOK(UN(NtUserGetScrollBarInfo)(w, OBJ_V, &a)); XOK(UN(NtUserGetScrollBarInfo)(w, OBJ_V, &b));
    CHECK(!memcmp(&a, &b, sizeof a));
    xw_kill(w);
}
XT(repeated_queries_are_stable) {
    NEED(NtUserGetScrollBarInfo);
    HWND w = sbwin(WS_VSCROLL | WS_HSCROLL, 0); setinfo(w, SB_VERT, 0, 100, 10, 33);
    SCROLLBARINFO a, b; XOK(q(w, OBJ_V, &a));
    int same = 1; for (int i = 0; i < 50; i++) { XOK(q(w, OBJ_V, &b)); if (memcmp(&a, &b, sizeof a)) same = 0; }
    CHECK(same);
    xw_kill(w);
}
XT(query_does_not_change_the_scroll_position) {
    NEED(NtUserGetScrollBarInfo);
    HWND w = sbwin(WS_VSCROLL, 0); setinfo(w, SB_VERT, 0, 100, 10, 33);
    SCROLLBARINFO s; XOK(q(w, OBJ_V, &s));
    EXPECT_EQ(U(GetScrollPos)(w, SB_VERT), 33);
    xw_kill(w);
}

/* ---- scroll bar controls ---- */
static HWND ctl(DWORD style, int w, int h) {
    HWND c = U(CreateWindowExW)(0, L"SCROLLBAR", L"", WS_POPUP | style, 30, 40, w, h, NULL, NULL, NULL, NULL);
    return c;
}
XT(control_client_rectangle_is_the_window_rectangle) {
    NEED(NtUserGetScrollBarInfo);
    HWND c = ctl(SBS_VERT, 17, 120); setinfo(c, SB_CTL, 0, 100, 10, 0);
    SCROLLBARINFO s; RECT wr; XOK(q(c, OBJ_C, &s)); U(GetWindowRect)(c, &wr);
    CHECK(!memcmp(&s.rcScrollBar, &wr, sizeof wr));
    xw_kill(c);
}
XT(control_horizontal_rectangle_is_the_window_rectangle) {
    NEED(NtUserGetScrollBarInfo);
    HWND c = ctl(SBS_HORZ, 120, 17); setinfo(c, SB_CTL, 0, 100, 10, 0);
    SCROLLBARINFO s; RECT wr; XOK(q(c, OBJ_C, &s)); U(GetWindowRect)(c, &wr);
    CHECK(!memcmp(&s.rcScrollBar, &wr, sizeof wr));
    xw_kill(c);
}
XT(control_rectangle_follows_the_window) {
    NEED(NtUserGetScrollBarInfo);
    HWND c = ctl(SBS_VERT, 17, 120); setinfo(c, SB_CTL, 0, 100, 10, 0);
    U(SetWindowPos)(c, NULL, 100, 120, 17, 90, SWP_NOZORDER | SWP_NOACTIVATE);
    SCROLLBARINFO s; RECT wr; XOK(q(c, OBJ_C, &s)); U(GetWindowRect)(c, &wr);
    CHECK(!memcmp(&s.rcScrollBar, &wr, sizeof wr));
    xw_kill(c);
}
XT(control_child_rectangle_is_in_screen_coordinates) {
    NEED(NtUserGetScrollBarInfo);
    HWND p = xw_win(WS_POPUP, 0, L"STATIC");
    HWND c = U(CreateWindowExW)(0, L"SCROLLBAR", L"", WS_CHILD | SBS_VERT, 5, 7, 17, 100, p, NULL, NULL, NULL);
    if (!c) { t_skip("child scroll bar"); xw_kill(p); return; }
    setinfo(c, SB_CTL, 0, 100, 10, 0);
    SCROLLBARINFO s; RECT wr; XOK(q(c, OBJ_C, &s)); U(GetWindowRect)(c, &wr);
    CHECK(!memcmp(&s.rcScrollBar, &wr, sizeof wr));
    xw_kill(p);
}
static void control_thumb(int vert) {
    NEED(NtUserGetScrollBarInfo);
    HWND c = vert ? ctl(SBS_VERT, 17, 120) : ctl(SBS_HORZ, 120, 17);
    SCROLLBARINFO s;
    setinfo(c, SB_CTL, 0, 100, 10, 0); XOK(q(c, OBJ_C, &s));
    EXPECT_EQ(s.xyThumbTop, s.dxyLineButton);
    CHECK(s.rgstate[2] == STATE_INVIS);
    EXPECT_EQ(s.dxyLineButton, vert ? U(GetSystemMetrics)(SM_CYVSCROLL) : U(GetSystemMetrics)(SM_CXHSCROLL));
    setinfo(c, SB_CTL, 0, 100, 10, 91); XOK(q(c, OBJ_C, &s));
    EXPECT_EQ(s.xyThumbBottom, barlen(&s, vert) - s.dxyLineButton);
    CHECK(s.rgstate[4] == STATE_INVIS);
    setinfo(c, SB_CTL, 0, 100, 10, 40); XOK(q(c, OBJ_C, &s));
    EXPECT_EQ(s.rgstate[0], 0); EXPECT_EQ(s.reserved, 0);
    xw_kill(c);
}
XT(control_thumb_vertical) { control_thumb(1); }
XT(control_thumb_horizontal) { control_thumb(0); }
XT(control_has_no_non_client_bars) {
    NEED(NtUserGetScrollBarInfo);
    HWND c = ctl(SBS_VERT, 17, 120); setinfo(c, SB_CTL, 0, 100, 10, 0);
    SCROLLBARINFO s; XOK(q(c, OBJ_V, &s)); CHECK(is_empty_invisible(&s));
    XOK(q(c, OBJ_H, &s)); CHECK(is_empty_invisible(&s));
    xw_kill(c);
}
XT(disabled_control_is_unavailable) {
    NEED(NtUserGetScrollBarInfo);
    HWND c = ctl(SBS_VERT, 17, 120); setinfo(c, SB_CTL, 0, 100, 10, 40);
    SCROLLBARINFO s; XOK(q(c, OBJ_C, &s)); EXPECT_EQ(s.rgstate[0], 0);
    U(EnableWindow)(c, FALSE);
    XOK(q(c, OBJ_C, &s)); CHECK(s.rgstate[0] & STATE_UNAVAIL);
    U(EnableWindow)(c, TRUE);
    XOK(q(c, OBJ_C, &s)); EXPECT_EQ(s.rgstate[0], 0);
    xw_kill(c);
}
XT(size_grip_answers_its_rectangle) {
    NEED(NtUserGetScrollBarInfo);
    HWND c = ctl(SBS_SIZEGRIP, 17, 17);
    SCROLLBARINFO s; RECT wr; XOK(q(c, OBJ_C, &s)); U(GetWindowRect)(c, &wr);
    CHECK(!memcmp(&s.rcScrollBar, &wr, sizeof wr));
    xw_kill(c);
}
XT(control_cbsize_protocol_matches_windows_bars) {
    NEED(NtUserGetScrollBarInfo);
    HWND c = ctl(SBS_VERT, 17, 120);
    union { SCROLLBARINFO s; unsigned char raw[0x100]; } u; memset(&u, XFILL, sizeof u); u.s.cbSize = 0x38;
    XFAIL(UN(NtUserGetScrollBarInfo)(c, OBJ_C, &u.s), XE_INVALID_PARAMETER);
    xw_kill(c);
}
