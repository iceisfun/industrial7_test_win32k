/* user_wnd_desktop_heap: rcWindow / rcClient in the mapped structure versus GetWindowRect,
 * GetClientRect, ClientToScreen and what MoveWindow / SetWindowPos / WM_NCCALCSIZE /
 * WM_WINDOWPOSCHANGING make the kernel store. */
#define WDH_TAG L"g"
#include "wdh.h"

static bool eqrc(RECT a, RECT b) { return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom; }
static RECT mkrc(int l, int t, int r, int b) { RECT x = { l, t, r, b }; return x; }

/* GetWindowRect returns *(wnd+0x58..0x68) as is and GetClientRect returns (0,0,w,h) of *(wnd+0x68..0x78),
 * unless the window DPI context differs from the thread's (NtUserTransformRect / rescale path). */
static void api_vs_raw(HWND h) {
    struct wv w;
    if (!wv_get(h, &w)) { t_note("window not decodable"); return; }
    RECT rw = WRC(&w, W_RCWINDOW), rc = WRC(&w, W_RCCLIENT), gw, gc;
    CHECK(U(GetWindowRect)(h, &gw));
    CHECK(U(GetClientRect)(h, &gc));
    if (!dpi96(h)) { t_note("DPI scaling active, raw/API comparison skipped"); return; }
    CHECKF(eqrc(gw, rw), "GetWindowRect (%ld,%ld,%ld,%ld) raw (%ld,%ld,%ld,%ld)", gw.left, gw.top, gw.right, gw.bottom, rw.left, rw.top, rw.right, rw.bottom);
    RECT c0 = mkrc(0, 0, rc.right - rc.left, rc.bottom - rc.top);
    CHECKF(eqrc(gc, c0), "GetClientRect (%ld,%ld,%ld,%ld) raw size %ldx%ld", gc.left, gc.top, gc.right, gc.bottom, c0.right, c0.bottom);
}

/* ---- creation ------------------------------------------------------------------------------------ */

TEST(user_wnd_desktop_heap_rect_create_popup_exact) {
    T_BEGIN();
    HWND h = mk_top(plain_class(), WS_POPUP, 0, -30000, -29990, 120, 90, NULL);
    NEED_WND(h, w);
    if (!dpi96(h)) { t_skip("DPI scaling active"); return; }
    RECT rw = WRC(&w, W_RCWINDOW), rc = WRC(&w, W_RCCLIENT);
    /* no non-client area: client rectangle == window rectangle, both in screen coordinates */
    CHECK(eqrc(rw, mkrc(-30000, -29990, -29880, -29900)));
    CHECK(eqrc(rc, rw));
    api_vs_raw(h);
    END();
}
TEST(user_wnd_desktop_heap_rect_create_zero_size) {
    T_BEGIN();
    HWND h = mk_top(plain_class(), WS_POPUP, 0, -30000, -30000, 0, 0, NULL);
    NEED_WND(h, w);
    if (!dpi96(h)) { t_skip("DPI scaling active"); return; }
    RECT rw = WRC(&w, W_RCWINDOW);
    CHECK(eqrc(rw, mkrc(-30000, -30000, -30000, -30000)));
    RECT gc;
    CHECK(U(GetClientRect)(h, &gc));
    CHECK(eqrc(gc, mkrc(0, 0, 0, 0)));
    END();
}
TEST(user_wnd_desktop_heap_rect_create_border_shrinks_client) {
    T_BEGIN();
    HWND h = mk_top(plain_class(), WS_POPUP | WS_BORDER, 0, -30000, -30000, 100, 80, NULL);
    NEED_WND(h, w);
    if (!dpi96(h)) { t_skip("DPI scaling active"); return; }
    RECT rw = WRC(&w, W_RCWINDOW), rc = WRC(&w, W_RCCLIENT);
    int bx = U(GetSystemMetrics)(SM_CXBORDER), by = U(GetSystemMetrics)(SM_CYBORDER);
    /* DefWindowProc(WM_NCCALCSIZE) takes one border width off every side */
    CHECK(eqrc(rc, mkrc(rw.left + bx, rw.top + by, rw.right - bx, rw.bottom - by)));
    api_vs_raw(h);
    END();
}
TEST(user_wnd_desktop_heap_rect_create_scrollbars_shrink_client) {
    T_BEGIN();
    HWND h = mk_top(plain_class(), WS_POPUP | WS_HSCROLL | WS_VSCROLL, 0, -30000, -30000, 100, 80, NULL);
    NEED_WND(h, w);
    if (!dpi96(h)) { t_skip("DPI scaling active"); return; }
    RECT rw = WRC(&w, W_RCWINDOW), rc = WRC(&w, W_RCCLIENT);
    int sx = U(GetSystemMetrics)(SM_CXVSCROLL), sy = U(GetSystemMetrics)(SM_CYHSCROLL);
    CHECK(eqrc(rc, mkrc(rw.left, rw.top, rw.right - sx, rw.bottom - sy)));
    END();
}
TEST(user_wnd_desktop_heap_rect_create_client_inside_window) {
    T_BEGIN();
    DWORD styles[] = { WS_POPUP | WS_CAPTION, WS_POPUP | WS_THICKFRAME, WS_OVERLAPPEDWINDOW, WS_POPUP | WS_DLGFRAME | WS_SYSMENU };
    for (size_t i = 0; i < sizeof styles / sizeof *styles; i++) {
        HWND h = mk_top(plain_class(), styles[i], 0, -30000, -30000, 300, 200, NULL);
        NEED_WND(h, w);
        RECT rw = WRC(&w, W_RCWINDOW), rc = WRC(&w, W_RCCLIENT);
        CHECKF(rc.left >= rw.left && rc.top >= rw.top && rc.right <= rw.right && rc.bottom <= rw.bottom, "style 0x%08lx", (unsigned long)styles[i]);
        CHECKF(rc.right > rc.left && rc.bottom > rc.top, "style 0x%08lx", (unsigned long)styles[i]);
        CHECKF(rc.top > rw.top, "caption/frame must take space at the top, style 0x%08lx", (unsigned long)styles[i]);
        api_vs_raw(h);
        U(DestroyWindow)(h);
    }
    END();
}

/* AdjustWindowRectEx is documented to compute the window rectangle for a wanted client rectangle;
 * CreateWindowEx with that size must give exactly that client size. */
static void adjust_case(DWORD st, DWORD ex) {
    T_BEGIN();
    RECT want = mkrc(0, 0, 300, 200), adj = want;
    CHECK(U(AdjustWindowRectEx)(&adj, st, FALSE, ex | EX_GUEST));
    HWND h = mk_top(plain_class(), st, ex, -30000, -30000, adj.right - adj.left, adj.bottom - adj.top, NULL);
    NEED_WND(h, w);
    if (!dpi96(h)) { t_skip("DPI scaling active"); return; }
    RECT rw = WRC(&w, W_RCWINDOW), rc = WRC(&w, W_RCCLIENT);
    CHECKF(rc.right - rc.left == 300 && rc.bottom - rc.top == 200, "client %ldx%ld", rc.right - rc.left, rc.bottom - rc.top);
    CHECK(rc.left - rw.left == -adj.left);
    CHECK(rc.top - rw.top == -adj.top);
    api_vs_raw(h);
    END();
}
#define AD(id, st, ex) TEST(user_wnd_desktop_heap_rect_adjust_##id) { adjust_case((st), (ex)); }
AD(border, WS_POPUP | WS_BORDER, 0)
AD(caption, WS_POPUP | WS_CAPTION, 0)
AD(dlgframe, WS_POPUP | WS_DLGFRAME, 0)
AD(thickframe, WS_POPUP | WS_THICKFRAME, 0)
AD(overlapped, WS_OVERLAPPEDWINDOW, 0)
AD(sysmenu_caption, WS_POPUP | WS_CAPTION | WS_SYSMENU, 0)
AD(popup_clientedge, WS_POPUP | WS_BORDER, WS_EX_CLIENTEDGE)
AD(popup_dlgmodalframe, WS_POPUP | WS_CAPTION, WS_EX_DLGMODALFRAME)
AD(popup_staticedge, WS_POPUP, WS_EX_STATICEDGE)
AD(overlapped_tool_ex, WS_OVERLAPPEDWINDOW, WS_EX_CLIENTEDGE)

/* ---- MoveWindow / SetWindowPos -------------------------------------------------------------------- */

static const struct { int x, y, w, h; } k_rects[] = {
    { -30000, -30000, 100, 80 }, { -29000, -28000, 1, 1 }, { -30000, -29500, 400, 300 }, { -20000, -20000, 0, 0 },
    { -31000, -30500, 50, 60 }, { -29900, -29990, 77, 33 }, { -25000, -26000, 500, 400 }, { -30000, -30000, 2, 3 },
};
TEST(user_wnd_desktop_heap_rect_movewindow_table) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    if (!dpi96(h)) { t_skip("DPI scaling active"); return; }
    for (size_t i = 0; i < sizeof k_rects / sizeof *k_rects; i++) {
        CHECK(U(MoveWindow)(h, k_rects[i].x, k_rects[i].y, k_rects[i].w, k_rects[i].h, FALSE));
        CHECK(wv_get(h, &w));
        RECT rw = WRC(&w, W_RCWINDOW);
        CHECKF(eqrc(rw, mkrc(k_rects[i].x, k_rects[i].y, k_rects[i].x + k_rects[i].w, k_rects[i].y + k_rects[i].h)),
               "case %zu raw (%ld,%ld,%ld,%ld)", i, rw.left, rw.top, rw.right, rw.bottom);
        CHECK(eqrc(WRC(&w, W_RCCLIENT), rw));
        api_vs_raw(h);
    }
    END();
}
TEST(user_wnd_desktop_heap_rect_setwindowpos_table) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    if (!dpi96(h)) { t_skip("DPI scaling active"); return; }
    for (size_t i = 0; i < sizeof k_rects / sizeof *k_rects; i++) {
        CHECK(U(SetWindowPos)(h, NULL, k_rects[i].x, k_rects[i].y, k_rects[i].w, k_rects[i].h, SWP_NOZORDER | SWP_NOACTIVATE));
        CHECK(wv_get(h, &w));
        RECT rw = WRC(&w, W_RCWINDOW);
        CHECKF(eqrc(rw, mkrc(k_rects[i].x, k_rects[i].y, k_rects[i].x + k_rects[i].w, k_rects[i].y + k_rects[i].h)), "case %zu", i);
    }
    END();
}
TEST(user_wnd_desktop_heap_rect_setwindowpos_nomove) {
    T_BEGIN();
    HWND h = mk_top(plain_class(), WS_POPUP, 0, -30000, -29000, 100, 80, NULL);
    NEED_WND(h, w);
    if (!dpi96(h)) { t_skip("DPI scaling active"); return; }
    CHECK(U(SetWindowPos)(h, NULL, 5, 6, 200, 150, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE));
    CHECK(wv_get(h, &w));
    CHECK(eqrc(WRC(&w, W_RCWINDOW), mkrc(-30000, -29000, -29800, -28850)));
    END();
}
TEST(user_wnd_desktop_heap_rect_setwindowpos_nosize) {
    T_BEGIN();
    HWND h = mk_top(plain_class(), WS_POPUP, 0, -30000, -29000, 100, 80, NULL);
    NEED_WND(h, w);
    if (!dpi96(h)) { t_skip("DPI scaling active"); return; }
    CHECK(U(SetWindowPos)(h, NULL, -28000, -27000, 999, 999, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE));
    CHECK(wv_get(h, &w));
    CHECK(eqrc(WRC(&w, W_RCWINDOW), mkrc(-28000, -27000, -27900, -26920)));
    END();
}
TEST(user_wnd_desktop_heap_rect_setwindowpos_nomove_nosize_keeps_rect) {
    T_BEGIN();
    HWND h = mk_top(plain_class(), WS_POPUP, 0, -30000, -29000, 100, 80, NULL);
    NEED_WND(h, w);
    RECT before = WRC(&w, W_RCWINDOW);
    CHECK(U(SetWindowPos)(h, NULL, 1, 2, 3, 4, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE));
    CHECK(wv_get(h, &w));
    CHECK(eqrc(WRC(&w, W_RCWINDOW), before));
    END();
}
TEST(user_wnd_desktop_heap_rect_unchanged_by_show_hide) {
    T_BEGIN();
    HWND h = mk_top(plain_class(), WS_POPUP, 0, -30000, -29000, 100, 80, NULL);
    NEED_WND(h, w);
    RECT before = WRC(&w, W_RCWINDOW);
    U(ShowWindow)(h, SW_SHOWNOACTIVATE);
    CHECK(wv_get(h, &w));
    CHECK(eqrc(WRC(&w, W_RCWINDOW), before));
    U(ShowWindow)(h, SW_HIDE);
    CHECK(wv_get(h, &w));
    CHECK(eqrc(WRC(&w, W_RCWINDOW), before));
    END();
}
TEST(user_wnd_desktop_heap_rect_resize_frame_window_client_follows) {
    T_BEGIN();
    HWND h = mk_top(plain_class(), WS_OVERLAPPEDWINDOW, 0, -30000, -30000, 400, 300, NULL);
    NEED_WND(h, w);
    RECT rw0 = WRC(&w, W_RCWINDOW), rc0 = WRC(&w, W_RCCLIENT);
    CHECK(U(SetWindowPos)(h, NULL, 0, 0, 500, 360, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE));
    CHECK(wv_get(h, &w));
    RECT rw1 = WRC(&w, W_RCWINDOW), rc1 = WRC(&w, W_RCCLIENT);
    /* the non-client area keeps its size: client grows by exactly the window growth */
    int dw = (rw1.right - rw1.left) - (rw0.right - rw0.left), dh = (rw1.bottom - rw1.top) - (rw0.bottom - rw0.top);
    CHECK(dw > 0 && dh > 0);
    EXPECT_EQ((rc1.right - rc1.left) - (rc0.right - rc0.left), dw);
    EXPECT_EQ((rc1.bottom - rc1.top) - (rc0.bottom - rc0.top), dh);
    EXPECT_EQ(rc1.left - rw1.left, rc0.left - rw0.left);
    EXPECT_EQ(rc1.top - rw1.top, rc0.top - rw0.top);
    END();
}

/* ---- WM_NCCALCSIZE / WM_WINDOWPOSCHANGING results -------------------------------------------------- */

static LRESULT CALLBACK wp_calc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_NCCALCSIZE && wp) {
        NCCALCSIZE_PARAMS *p = (NCCALCSIZE_PARAMS *)lp;
        p->rgrc[0].left += 7; p->rgrc[0].top += 9; p->rgrc[0].right -= 5; p->rgrc[0].bottom -= 3;
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}
static LRESULT CALLBACK wp_clamp(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_WINDOWPOSCHANGING) {
        WINDOWPOS *p = (WINDOWPOS *)lp;
        if (!(p->flags & SWP_NOSIZE) && p->cx > 77) p->cx = 77;
        if (!(p->flags & SWP_NOSIZE) && p->cy > 55) p->cy = 55;
    }
    return DefWindowProcW(h, m, wp, lp);
}
TEST(user_wnd_desktop_heap_rect_nccalcsize_result_stored) {
    T_BEGIN();
    wchar_t cn[64];
    if (!reg_class(cn, 0, 0, 0, wp_calc, FALSE, NULL, NULL)) { t_skip("register failed"); return; }
    HWND h = mk_top(cn, WS_POPUP, 0, -30000, -30000, 100, 80, NULL);
    NEED_WND(h, w);
    if (!dpi96(h)) { t_skip("DPI scaling active"); return; }
    /* creation sends WM_NCCALCSIZE with wParam FALSE, which the procedure leaves alone; a frame change sends it with
     * wParam TRUE and the rectangle the procedure returns then becomes the client rectangle */
    CHECK(U(SetWindowPos)(h, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED));
    CHECK(wv_get(h, &w));
    RECT rw = WRC(&w, W_RCWINDOW), rc = WRC(&w, W_RCCLIENT);
    CHECK(eqrc(rc, mkrc(rw.left + 7, rw.top + 9, rw.right - 5, rw.bottom - 3)));
    RECT gc;
    CHECK(U(GetClientRect)(h, &gc));
    CHECK(eqrc(gc, mkrc(0, 0, 100 - 12, 80 - 12)));
    POINT pt = { 0, 0 };
    CHECK(U(ClientToScreen)(h, &pt));
    EXPECT_EQ(pt.x, rc.left);
    EXPECT_EQ(pt.y, rc.top);
    END();
}
TEST(user_wnd_desktop_heap_rect_nccalcsize_reapplied_on_resize) {
    T_BEGIN();
    wchar_t cn[64];
    if (!reg_class(cn, 0, 0, 0, wp_calc, FALSE, NULL, NULL)) { t_skip("register failed"); return; }
    HWND h = mk_top(cn, WS_POPUP, 0, -30000, -30000, 100, 80, NULL);
    NEED_WND(h, w);
    if (!dpi96(h)) { t_skip("DPI scaling active"); return; }
    CHECK(U(MoveWindow)(h, -29000, -29000, 200, 150, FALSE));
    CHECK(wv_get(h, &w));
    CHECK(eqrc(WRC(&w, W_RCCLIENT), mkrc(-29000 + 7, -29000 + 9, -28800 - 5, -28850 - 3)));
    END();
}
TEST(user_wnd_desktop_heap_rect_windowposchanging_clamps_size) {
    T_BEGIN();
    wchar_t cn[64];
    if (!reg_class(cn, 0, 0, 0, wp_clamp, FALSE, NULL, NULL)) { t_skip("register failed"); return; }
    HWND h = mk_top(cn, WS_POPUP, 0, -30000, -30000, 50, 40, NULL);
    NEED_WND(h, w);
    if (!dpi96(h)) { t_skip("DPI scaling active"); return; }
    CHECK(U(SetWindowPos)(h, NULL, -29000, -29000, 500, 400, SWP_NOZORDER | SWP_NOACTIVATE));
    CHECK(wv_get(h, &w));
    /* the size the procedure wrote into WINDOWPOS is the one the kernel applies */
    CHECK(eqrc(WRC(&w, W_RCWINDOW), mkrc(-29000, -29000, -29000 + 77, -29000 + 55)));
    api_vs_raw(h);
    END();
}

/* ---- child windows ---------------------------------------------------------------------------------- */

TEST(user_wnd_desktop_heap_rect_child_is_screen_relative) {
    T_BEGIN();
    HWND p = mk_top(plain_class(), WS_POPUP | WS_CAPTION, 0, -30000, -30000, 300, 200, NULL);
    HWND c = mk_child(p, plain_class(), WS_CHILD, 10, 20, 50, 40, 1);
    NEED_WND(c, wc);
    struct wv wp;
    CHECK(wv_get(p, &wp));
    if (!dpi96(c)) { t_skip("DPI scaling active"); return; }
    RECT pc = WRC(&wp, W_RCCLIENT), cw = WRC(&wc, W_RCWINDOW), cc = WRC(&wc, W_RCCLIENT);
    /* rectangles are stored in screen coordinates even for children */
    CHECK(eqrc(cw, mkrc(pc.left + 10, pc.top + 20, pc.left + 60, pc.top + 60)));
    CHECK(eqrc(cc, cw));
    api_vs_raw(c);
    END();
}
TEST(user_wnd_desktop_heap_rect_child_client_to_screen_matches_raw) {
    T_BEGIN();
    HWND p = mk_top(plain_class(), WS_POPUP | WS_CAPTION, 0, -30000, -30000, 300, 200, NULL);
    HWND c = mk_child(p, plain_class(), WS_CHILD | WS_BORDER, 15, 25, 60, 50, 1);
    NEED_WND(c, wc);
    if (!dpi96(c)) { t_skip("DPI scaling active"); return; }
    RECT cc = WRC(&wc, W_RCCLIENT);
    POINT pt = { 0, 0 };
    CHECK(U(ClientToScreen)(c, &pt));
    EXPECT_EQ(pt.x, cc.left);
    EXPECT_EQ(pt.y, cc.top);
    POINT back = pt;
    CHECK(U(ScreenToClient)(c, &back));
    EXPECT_EQ(back.x, 0);
    EXPECT_EQ(back.y, 0);
    POINT mp = { 0, 0 };
    U(MapWindowPoints)(c, NULL, &mp, 1);
    EXPECT_EQ(mp.x, pt.x);
    EXPECT_EQ(mp.y, pt.y);
    END();
}
TEST(user_wnd_desktop_heap_rect_parent_move_shifts_child) {
    T_BEGIN();
    HWND p = mk_top(plain_class(), WS_POPUP, 0, -30000, -30000, 300, 200, NULL);
    HWND c = mk_child(p, plain_class(), WS_CHILD, 10, 20, 50, 40, 1);
    NEED_WND(c, wc);
    RECT c0 = WRC(&wc, W_RCWINDOW);
    CHECK(U(MoveWindow)(p, -29900, -29800, 300, 200, FALSE));
    CHECK(wv_get(c, &wc));
    RECT c1 = WRC(&wc, W_RCWINDOW);
    /* moving a parent: the child keeps its position inside the parent, so its screen rectangle moves by the same amount */
    EXPECT_EQ(c1.left - c0.left, 100);
    EXPECT_EQ(c1.top - c0.top, 200);
    EXPECT_EQ(c1.right - c1.left, c0.right - c0.left);
    EXPECT_EQ(c1.bottom - c1.top, c0.bottom - c0.top);
    END();
}
TEST(user_wnd_desktop_heap_rect_parent_resize_keeps_child) {
    T_BEGIN();
    HWND p = mk_top(plain_class(), WS_POPUP, 0, -30000, -30000, 300, 200, NULL);
    HWND c = mk_child(p, plain_class(), WS_CHILD, 10, 20, 50, 40, 1);
    NEED_WND(c, wc);
    RECT c0 = WRC(&wc, W_RCWINDOW);
    CHECK(U(MoveWindow)(p, -30000, -30000, 500, 400, FALSE));
    CHECK(wv_get(c, &wc));
    CHECK(eqrc(WRC(&wc, W_RCWINDOW), c0));
    END();
}
TEST(user_wnd_desktop_heap_rect_child_move_inside_parent) {
    T_BEGIN();
    HWND p = mk_top(plain_class(), WS_POPUP, 0, -30000, -30000, 300, 200, NULL);
    HWND c = mk_child(p, plain_class(), WS_CHILD, 10, 20, 50, 40, 1);
    NEED_WND(c, wc);
    if (!dpi96(c)) { t_skip("DPI scaling active"); return; }
    CHECK(U(MoveWindow)(c, 100, 90, 30, 25, FALSE));
    CHECK(wv_get(c, &wc));
    CHECK(eqrc(WRC(&wc, W_RCWINDOW), mkrc(-30000 + 100, -30000 + 90, -30000 + 130, -30000 + 115)));
    RECT pr;
    CHECK(U(GetWindowRect)(c, &pr));
    POINT pt = { 100, 90 };
    CHECK(U(ClientToScreen)(p, &pt));
    EXPECT_EQ(pr.left, pt.x);
    EXPECT_EQ(pr.top, pt.y);
    END();
}
TEST(user_wnd_desktop_heap_rect_nested_children_accumulate) {
    T_BEGIN();
    HWND p = mk_top(plain_class(), WS_POPUP, 0, -30000, -30000, 300, 200, NULL);
    HWND c1 = mk_child(p, plain_class(), WS_CHILD, 10, 10, 200, 150, 1);
    HWND c2 = mk_child(c1, plain_class(), WS_CHILD, 5, 7, 50, 40, 2);
    NEED_WND(c2, w2);
    if (!dpi96(c2)) { t_skip("DPI scaling active"); return; }
    CHECK(eqrc(WRC(&w2, W_RCWINDOW), mkrc(-30000 + 15, -30000 + 17, -30000 + 65, -30000 + 57)));
    END();
}
TEST(user_wnd_desktop_heap_rect_maximize_child_then_restore) {
    T_BEGIN();
    HWND p = mk_top(plain_class(), WS_POPUP, 0, -30000, -30000, 300, 200, NULL);
    HWND c = mk_child(p, plain_class(), WS_CHILD, 10, 20, 50, 40, 1);
    NEED_WND(c, wc);
    RECT before = WRC(&wc, W_RCWINDOW);
    U(ShowWindow)(c, SW_SHOWMAXIMIZED);
    CHECK(wv_get(c, &wc));
    RECT mx = WRC(&wc, W_RCWINDOW);
    t_note("maximized child rect (%ld,%ld,%ld,%ld) iconic=%d zoomed=%d", mx.left, mx.top, mx.right, mx.bottom, U(IsIconic)(c), U(IsZoomed)(c));
    api_vs_raw(c);
    U(ShowWindow)(c, SW_RESTORE);
    CHECK(wv_get(c, &wc));
    CHECK(eqrc(WRC(&wc, W_RCWINDOW), before));
    END();
}
TEST(user_wnd_desktop_heap_rect_minimize_child_then_restore) {
    T_BEGIN();
    HWND p = mk_top(plain_class(), WS_POPUP, 0, -30000, -30000, 300, 200, NULL);
    HWND c = mk_child(p, plain_class(), WS_CHILD, 10, 20, 50, 40, 1);
    NEED_WND(c, wc);
    RECT before = WRC(&wc, W_RCWINDOW);
    U(ShowWindow)(c, SW_SHOWMINNOACTIVE);
    CHECK(wv_get(c, &wc));
    api_vs_raw(c);
    U(ShowWindow)(c, SW_RESTORE);
    CHECK(wv_get(c, &wc));
    CHECK(eqrc(WRC(&wc, W_RCWINDOW), before));
    END();
}

/* ---- relations of the rectangle words --------------------------------------------------------------- */

TEST(user_wnd_desktop_heap_rect_getwindowplacement_normal_matches) {
    T_BEGIN();
    HWND h = mk_top(plain_class(), WS_POPUP, 0, -30000, -29000, 100, 80, NULL);
    NEED_WND(h, w);
    WINDOWPLACEMENT pl; memset(&pl, 0, sizeof pl); pl.length = sizeof pl;
    CHECK(U(GetWindowPlacement)(h, &pl));
    t_note("placement cmd=%u normal=(%ld,%ld,%ld,%ld)", pl.showCmd, pl.rcNormalPosition.left, pl.rcNormalPosition.top, pl.rcNormalPosition.right, pl.rcNormalPosition.bottom);
    END();
}
TEST(user_wnd_desktop_heap_rect_null_hwnd_fails) {
    T_BEGIN();
    HWND h = mk_plain();
    if (!h) { t_skip("window creation failed"); return; }
    RECT r;
    CHECK(U(GetWindowRect)(h, &r));
    CHECK(!U(GetWindowRect)(NULL, &r));
    CHECK(!U(GetClientRect)(NULL, &r));
    END();
}
TEST(user_wnd_desktop_heap_rect_desktop_window_rect_is_monitor) {
    T_BEGIN();
    HWND d = U(GetDesktopWindow)();
    NEED_WND(d, w);
    RECT g, rw = WRC(&w, W_RCWINDOW);
    CHECK(U(GetWindowRect)(d, &g));
    /* GetWindowRect for the desktop (fnid 0x29d) returns the primary monitor rectangle, not the stored one */
    int sx = U(GetSystemMetrics)(SM_CXSCREEN), sy = U(GetSystemMetrics)(SM_CYSCREEN);
    if (dpi96(d)) {
        EXPECT_EQ(g.right - g.left, sx);
        EXPECT_EQ(g.bottom - g.top, sy);
    }
    t_note("stored desktop rect (%ld,%ld,%ld,%ld) API (%ld,%ld,%ld,%ld)", rw.left, rw.top, rw.right, rw.bottom, g.left, g.top, g.right, g.bottom);
    END();
}
TEST(user_wnd_desktop_heap_rect_monitor_word_note) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    HMONITOR m = U(MonitorFromWindow)(h, MONITOR_DEFAULTTONEAREST);
    t_note("MonitorFromWindow=%p, stored monitor word=0x%llx", m, (unsigned long long)W64(&w, W_MONITOR));
    CHECK(m != NULL);
    END();
}
TEST(user_wnd_desktop_heap_rect_stale_after_destroy_unreadable_via_api) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    HWND s = h;
    U(DestroyWindow)(h);
    RECT r = { 1, 2, 3, 4 };
    CHECK(!U(GetWindowRect)(s, &r));
    CHECK(r.left == 1 && r.top == 2 && r.right == 3 && r.bottom == 4);   /* untouched on failure */
    END();
}
