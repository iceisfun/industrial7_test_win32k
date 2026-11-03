/* Point-to-window syscalls: NtUserWindowFromPoint, NtUserChildWindowFromPointEx and
 * NtUserRealChildWindowFromPoint.
 *
 * win32kfull!_ChildWindowFromPointEx: the point is in PARENT CLIENT coordinates; it is converted
 * with the client rect at tagWND+0x68 and must lie inside that rect (else NULL); the direct child
 * chain (spwndChild, z-order top first) is then scanned, skipping WS_VISIBLE-less windows when
 * CWP_SKIPINVISIBLE (flag 1), WS_DISABLED when CWP_SKIPDISABLED (2) and WS_EX_TRANSPARENT when
 * CWP_SKIPTRANSPARENT (4). The parent itself is returned when no child matches.
 * user32!ChildWindowFromPoint is NtUserChildWindowFromPointEx(.., 0) (plus a private shortcut for
 * one internal window class).
 * WindowFromPoint walks the real desktop tree, so the point must lie inside the virtual screen: the
 * fixture first probes whether an off-screen window can be hit, and otherwise falls back to fully
 * transparent (alpha 1/255) layered topmost windows in a screen corner.
 */
#include "zo.h"

static int g_area = -1;         /* -1 unknown, 0 none, 1 usable */
static int g_onscreen;
static int g_bx, g_by, g_noted;

static HWND hmk_ex(const char *title, int dx, int dy, int w, int h, int show, HWND owner) {
    DWORD ex = g_onscreen ? (WS_EX_LAYERED | WS_EX_TOPMOST) : 0;
    HWND hw = zo_top_ex(title, g_bx + dx, g_by + dy, w, h, WS_POPUP, ex, owner, 0, NULL);
    if (hw && g_onscreen) U(SetLayeredWindowAttributes)(hw, 0, 1, LWA_ALPHA);
    if (hw && show) zo_show(hw);
    return hw;
}
static HWND hmk(const char *title, int dx, int dy, int w, int h, int show) { return hmk_ex(title, dx, dy, w, h, show, NULL); }
static POINT pt(int dx, int dy) { POINT p; p.x = g_bx + dx; p.y = g_by + dy; return p; }

static int probe_here(void) {
    HWND p = hmk("probe", 0, 0, 80, 80, 1);
    if (!p) return 0;
    int ok = (U(WindowFromPoint)(pt(40, 40)) == p);
    U(DestroyWindow)(p);
    return ok;
}
static int hit_area(void) {
    if (g_area >= 0) return g_area;
    g_area = 0;
    g_onscreen = 0; g_bx = ZO_OFFX; g_by = ZO_OFFY;
    if (probe_here()) { g_area = 1; return 1; }
    g_onscreen = 1;
    int vx = U(GetSystemMetrics)(SM_XVIRTUALSCREEN), vy = U(GetSystemMetrics)(SM_YVIRTUALSCREEN);
    int vw = U(GetSystemMetrics)(SM_CXVIRTUALSCREEN), vh = U(GetSystemMetrics)(SM_CYVIRTUALSCREEN);
    struct { int x, y; } cand[] = { { vx + 20, vy + 20 }, { vx + vw - 400, vy + 20 }, { vx + vw / 2, vy + vh / 3 } };
    for (unsigned i = 0; i < 3; i++) {
        g_bx = cand[i].x; g_by = cand[i].y;
        if (probe_here()) { g_area = 1; return 1; }
    }
    return 0;
}
#define NEED_HIT() do { NEED_GUI(); if (!hit_area()) { t_skip("no point on the desktop where a probe window can be hit"); return; } \
                        if (!g_noted) { g_noted = 1; t_note("hit-test area: %s at (%d,%d)", g_onscreen ? "on-screen transparent windows" : "off-screen windows", g_bx, g_by); } } while (0)


/* Wine's win32u takes (x, y) instead of a POINT by value for these three, so the raw stubs are only called on Windows;
 * under Wine the user32 export (which goes through Wine's own stub) is used to keep the logic exercised. */
static HWND cwpe(HWND p, POINT pt, UINT fl) { return t_is_wine() ? U(ChildWindowFromPointEx)(p, pt, fl) : UN(NtUserChildWindowFromPointEx)(p, pt, fl); }
static HWND rcwp(HWND p, POINT pt) { return t_is_wine() ? U(RealChildWindowFromPoint)(p, pt) : UN(NtUserRealChildWindowFromPoint)(p, pt); }
static HWND wfp(POINT pt) { return t_is_wine() ? U(WindowFromPoint)(pt) : UN(NtUserWindowFromPoint)(pt); }

/* ---- WindowFromPoint ---- */

TEST(win_position_zorder_hit_overlap_topmost_wins) {
    NEED_HIT();
    HWND a = hmk("ha", 0, 0, 100, 100, 1), b = hmk("hb", 50, 50, 100, 100, 1);
    REQUIRE(a && b);
    HWND ord[2] = { b, a };
    zo_arrange(ord, 2);
    EXPECT_EQ(U(WindowFromPoint)(pt(10, 10)), a);
    EXPECT_EQ(U(WindowFromPoint)(pt(140, 140)), b);
    EXPECT_EQ(U(WindowFromPoint)(pt(70, 70)), b);
    HWND ord2[2] = { a, b };
    zo_arrange(ord2, 2);
    EXPECT_EQ(U(WindowFromPoint)(pt(70, 70)), a);
    zo_reset();
}

TEST(win_position_zorder_hit_follows_z_changes_via_setwindowpos) {
    NEED_HIT(); NEED_NT(NtUserSetWindowPos);
    HWND a = hmk("hz1", 0, 0, 100, 100, 1), b = hmk("hz2", 0, 0, 100, 100, 1), c = hmk("hz3", 0, 0, 100, 100, 1);
    REQUIRE(a && b && c);
    HWND in[3] = { a, b, c };
    zo_arrange(in, 3);
    EXPECT_EQ(U(WindowFromPoint)(pt(50, 50)), a);
    UN(NtUserSetWindowPos)(a, HWND_BOTTOM, 0, 0, 0, 0, ZO_SWP_ZONLY);
    EXPECT_EQ(U(WindowFromPoint)(pt(50, 50)), b);
    UN(NtUserSetWindowPos)(c, HWND_TOP, 0, 0, 0, 0, ZO_SWP_ZONLY);
    EXPECT_EQ(U(WindowFromPoint)(pt(50, 50)), c);
    UN(NtUserSetWindowPos)(a, c, 0, 0, 0, 0, ZO_SWP_ZONLY);        /* a directly behind c, ahead of b */
    UN(NtUserSetWindowPos)(c, HWND_BOTTOM, 0, 0, 0, 0, ZO_SWP_ZONLY);
    EXPECT_EQ(U(WindowFromPoint)(pt(50, 50)), a);
    zo_reset();
}

TEST(win_position_zorder_hit_hidden_window_is_skipped) {
    NEED_HIT();
    HWND a = hmk("hh1", 0, 0, 100, 100, 1), b = hmk("hh2", 0, 0, 100, 100, 1);
    REQUIRE(a && b);
    HWND in[2] = { b, a };
    zo_arrange(in, 2);
    EXPECT_EQ(U(WindowFromPoint)(pt(50, 50)), b);
    zo_hide(b);
    EXPECT_EQ(U(WindowFromPoint)(pt(50, 50)), a);       /* WindowFromPoint never returns hidden windows */
    zo_show(b);
    EXPECT_EQ(U(WindowFromPoint)(pt(50, 50)), b);
    zo_reset();
}

TEST(win_position_zorder_hit_moves_with_window) {
    NEED_HIT(); NEED_NT(NtUserSetWindowPos);
    HWND a = hmk("hm", 0, 0, 60, 60, 1);
    REQUIRE(a);
    EXPECT_EQ(U(WindowFromPoint)(pt(30, 30)), a);
    UN(NtUserSetWindowPos)(a, NULL, g_bx + 200, g_by + 100, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    CHECK(U(WindowFromPoint)(pt(30, 30)) != a);
    EXPECT_EQ(U(WindowFromPoint)(pt(230, 130)), a);
    zo_reset();
}

TEST(win_position_zorder_hit_edges_are_right_bottom_exclusive) {
    NEED_HIT();
    HWND a = hmk("he", 0, 0, 60, 40, 1);
    REQUIRE(a);
    EXPECT_EQ(U(WindowFromPoint)(pt(0, 0)), a);
    EXPECT_EQ(U(WindowFromPoint)(pt(59, 39)), a);
    CHECK(U(WindowFromPoint)(pt(60, 20)) != a);
    CHECK(U(WindowFromPoint)(pt(30, 40)) != a);
    CHECK(U(WindowFromPoint)(pt(-1, 20)) != a);
    CHECK(U(WindowFromPoint)(pt(30, -1)) != a);
    zo_reset();
}

TEST(win_position_zorder_hit_returns_deepest_child) {
    NEED_HIT();
    HWND p = hmk("hp", 0, 0, 200, 200, 1);
    REQUIRE(p);
    HWND c = zo_child(p, "hc", 20, 20, 100, 100, WS_VISIBLE, 0);
    REQUIRE(c);
    HWND g = zo_child(c, "hg", 10, 10, 30, 30, WS_VISIBLE, 0);
    REQUIRE(g);
    EXPECT_EQ(U(WindowFromPoint)(pt(35, 35)), g);          /* inside the grandchild */
    EXPECT_EQ(U(WindowFromPoint)(pt(60, 60)), c);
    EXPECT_EQ(U(WindowFromPoint)(pt(5, 5)), p);
    zo_hide(c);
    EXPECT_EQ(U(WindowFromPoint)(pt(35, 35)), p);          /* hiding the child hides its subtree */
    zo_reset();
}

TEST(win_position_zorder_hit_child_is_clipped_to_parent) {
    NEED_HIT();
    HWND p = hmk("hcp", 0, 0, 100, 100, 1);
    REQUIRE(p);
    HWND c = zo_child(p, "hcc", 60, 60, 100, 100, WS_VISIBLE, 0);      /* hangs out of the parent's bottom-right corner */
    REQUIRE(c);
    EXPECT_EQ(U(WindowFromPoint)(pt(80, 80)), c);
    HWND outside = U(WindowFromPoint)(pt(130, 130));       /* inside the child's rectangle, outside the parent */
    CHECK(outside != c);
    CHECK(outside != p);
    RECT cr = zo_wr(c);
    CHECK(cr.right - cr.left == 100);                      /* the rectangle itself is not clipped, only the visible region */
    zo_reset();
}

TEST(win_position_zorder_hit_child_sibling_order) {
    NEED_HIT();
    HWND p = hmk("hs", 0, 0, 200, 200, 1);
    REQUIRE(p);
    HWND c1 = zo_child(p, "hs1", 10, 10, 100, 100, WS_VISIBLE, 0), c2 = zo_child(p, "hs2", 50, 50, 100, 100, WS_VISIBLE, 0);
    REQUIRE(c1 && c2);
    HWND in[2] = { c2, c1 };
    zo_arrange(in, 2);
    EXPECT_EQ(U(WindowFromPoint)(pt(70, 70)), c2);
    HWND in2[2] = { c1, c2 };
    zo_arrange(in2, 2);
    EXPECT_EQ(U(WindowFromPoint)(pt(70, 70)), c1);
    zo_reset();
}

TEST(win_position_zorder_hit_httransparent_falls_through_same_thread) {
    NEED_HIT();
    HWND a = hmk("ht1", 0, 0, 100, 100, 1), b = hmk("ht2", 0, 0, 100, 100, 1);
    REQUIRE(a && b);
    HWND in[2] = { b, a };
    zo_arrange(in, 2);
    EXPECT_EQ(U(WindowFromPoint)(pt(50, 50)), b);
    U(SetPropA)(b, "zoHTT", (HANDLE)1);                    /* the fixture's WM_NCHITTEST answers HTTRANSPARENT for this window */
    EXPECT_EQ(U(WindowFromPoint)(pt(50, 50)), a);          /* WindowFromPoint keeps looking at same-thread windows below */
    U(RemovePropA)(b, "zoHTT");
    EXPECT_EQ(U(WindowFromPoint)(pt(50, 50)), b);
    zo_reset();
}

TEST(win_position_zorder_hit_disabled_window_note) {
    NEED_HIT();
    HWND a = hmk("hd1", 0, 0, 100, 100, 1), b = hmk("hd2", 0, 0, 100, 100, 1);
    REQUIRE(a && b);
    HWND in[2] = { b, a };
    zo_arrange(in, 2);
    U(EnableWindow)(b, FALSE);
    HWND r = U(WindowFromPoint)(pt(50, 50));
    t_note("WindowFromPoint over a disabled top window returns %s", r == b ? "the disabled window" : (r == a ? "the window below" : "something else"));
    CHECK(r == a || r == b);
    U(EnableWindow)(b, TRUE);
    zo_reset();
}

TEST(win_position_zorder_hit_ex_transparent_note) {
    NEED_HIT();
    HWND a = hmk("hx1", 0, 0, 100, 100, 1);
    HWND b = zo_top_ex("hx2", g_bx, g_by, 100, 100, WS_POPUP, (g_onscreen ? (WS_EX_LAYERED | WS_EX_TOPMOST) : 0) | WS_EX_TRANSPARENT, NULL, 0, NULL);
    REQUIRE(a && b);
    if (g_onscreen) U(SetLayeredWindowAttributes)(b, 0, 1, LWA_ALPHA);
    zo_show(b);
    HWND in[2] = { b, a };
    zo_arrange(in, 2);
    HWND r = U(WindowFromPoint)(pt(50, 50));
    t_note("WindowFromPoint over a WS_EX_TRANSPARENT sibling (same thread) returns %s", r == b ? "the transparent window" : (r == a ? "the window below" : "something else"));
    CHECK(r == a || r == b);
    zo_reset();
}

TEST(win_position_zorder_hit_stub_matches_user32) {
    NEED_HIT(); NEED_NT(NtUserWindowFromPoint);
    HWND a = hmk("hst", 0, 0, 100, 100, 1);
    REQUIRE(a);
    POINT p = pt(20, 20);
    EXPECT_EQ(wfp(p), a);
    EXPECT_EQ(wfp(p), U(WindowFromPoint)(p));
    FARPROC f = t_proc_in("win32u.dll", "NtUserWindowFromPoint");
    if (!t_is_wine()) EXPECT_EQ(k_stub_number(f), 4116);
    zo_reset();
}

TEST(win_position_zorder_hit_far_away_point_is_not_our_window) {
    NEED_HIT();
    HWND a = hmk("hfar", 0, 0, 100, 100, 1);
    REQUIRE(a);
    POINT p; p.x = 0x7ffffff0; p.y = 0x7ffffff0;
    HWND r = U(WindowFromPoint)(p);
    CHECK(r != a);
    p.x = -0x7fffff00; p.y = -0x7fffff00;
    r = U(WindowFromPoint)(p);
    CHECK(r != a);
    zo_reset();
}

/* ---- ChildWindowFromPointEx / ChildWindowFromPoint / RealChildWindowFromPoint ---- */

struct cw { HWND P, c1, c2, g, hid, dis, trn; };
static int mkcw(struct cw *k) {
    memset(k, 0, sizeof *k);
    k->P = zo_top("cwP", ZO_OFFX, ZO_OFFY, 200, 200, 1);
    if (!k->P) return 0;
    k->c1 = zo_child(k->P, "cw1", 10, 10, 60, 60, WS_VISIBLE, 0);
    k->c2 = zo_child(k->P, "cw2", 40, 40, 60, 60, WS_VISIBLE, 0);
    k->g = k->c1 ? zo_child(k->c1, "cwg", 5, 5, 20, 20, WS_VISIBLE, 0) : NULL;
    k->hid = zo_child(k->P, "cwh", 120, 10, 40, 40, 0, 0);
    k->dis = zo_child(k->P, "cwd", 120, 60, 40, 40, WS_VISIBLE | WS_DISABLED, 0);
    k->trn = zo_child(k->P, "cwt", 120, 110, 40, 40, WS_VISIBLE, WS_EX_TRANSPARENT);
    if (!(k->c1 && k->c2 && k->g && k->hid && k->dis && k->trn)) return 0;
    HWND in[2] = { k->c2, k->c1 };
    zo_arrange(in, 2);
    return 1;
}
static POINT P2(int x, int y) { POINT p; p.x = x; p.y = y; return p; }

TEST(win_position_zorder_hit_child_ex_basic) {
    NEED_GUI(); NEED_NT(NtUserChildWindowFromPointEx);
    struct cw k; REQUIRE(mkcw(&k));
    EXPECT_EQ(cwpe(k.P, P2(50, 50), CWP_ALL), k.c2);     /* overlap: the topmost sibling */
    EXPECT_EQ(cwpe(k.P, P2(12, 12), CWP_ALL), k.c1);
    EXPECT_EQ(cwpe(k.P, P2(20, 20), CWP_ALL), k.c1);     /* grandchild area: still the direct child */
    EXPECT_EQ(cwpe(k.P, P2(5, 5), CWP_ALL), k.P);        /* no child here: the parent itself */
    EXPECT_EQ(cwpe(k.P, P2(199, 199), CWP_ALL), k.P);
    zo_reset();
}

TEST(win_position_zorder_hit_child_ex_outside_parent_client_is_null) {
    NEED_GUI(); NEED_NT(NtUserChildWindowFromPointEx);
    struct cw k; REQUIRE(mkcw(&k));
    POINT out[] = { P2(-1, 5), P2(5, -1), P2(200, 5), P2(5, 200), P2(200, 200), P2(-1000, -1000), P2(100000, 100000) };
    for (unsigned i = 0; i < sizeof out / sizeof out[0]; i++)
        CHECKF(cwpe(k.P, out[i], CWP_ALL) == NULL, "point (%ld,%ld)", out[i].x, out[i].y);
    zo_reset();
}

TEST(win_position_zorder_hit_child_ex_child_edges_exclusive) {
    NEED_GUI(); NEED_NT(NtUserChildWindowFromPointEx);
    struct cw k; REQUIRE(mkcw(&k));
    EXPECT_EQ(cwpe(k.P, P2(10, 10), CWP_ALL), k.c1);     /* c1 = [10,70) x [10,70) */
    EXPECT_EQ(cwpe(k.P, P2(9, 20), CWP_ALL), k.P);
    EXPECT_EQ(cwpe(k.P, P2(20, 9), CWP_ALL), k.P);
    EXPECT_EQ(cwpe(k.P, P2(99, 99), CWP_ALL), k.c2);     /* c2 = [40,100) */
    EXPECT_EQ(cwpe(k.P, P2(100, 99), CWP_ALL), k.P);
    EXPECT_EQ(cwpe(k.P, P2(99, 100), CWP_ALL), k.P);
    zo_reset();
}

TEST(win_position_zorder_hit_child_ex_skip_invisible) {
    NEED_GUI(); NEED_NT(NtUserChildWindowFromPointEx);
    struct cw k; REQUIRE(mkcw(&k));
    EXPECT_EQ(cwpe(k.P, P2(130, 20), CWP_ALL), k.hid);
    EXPECT_EQ(cwpe(k.P, P2(130, 20), CWP_SKIPINVISIBLE), k.P);
    EXPECT_EQ(cwpe(k.P, P2(130, 20), CWP_SKIPDISABLED), k.hid);
    EXPECT_EQ(cwpe(k.P, P2(130, 20), CWP_SKIPTRANSPARENT), k.hid);
    zo_reset();
}

TEST(win_position_zorder_hit_child_ex_skip_disabled) {
    NEED_GUI(); NEED_NT(NtUserChildWindowFromPointEx);
    struct cw k; REQUIRE(mkcw(&k));
    EXPECT_EQ(cwpe(k.P, P2(130, 70), CWP_ALL), k.dis);
    EXPECT_EQ(cwpe(k.P, P2(130, 70), CWP_SKIPDISABLED), k.P);
    EXPECT_EQ(cwpe(k.P, P2(130, 70), CWP_SKIPINVISIBLE), k.dis);
    zo_reset();
}

TEST(win_position_zorder_hit_child_ex_skip_transparent) {
    NEED_GUI(); NEED_NT(NtUserChildWindowFromPointEx);
    struct cw k; REQUIRE(mkcw(&k));
    EXPECT_EQ(cwpe(k.P, P2(130, 120), CWP_ALL), k.trn);
    EXPECT_EQ(cwpe(k.P, P2(130, 120), CWP_SKIPTRANSPARENT), k.P);
    EXPECT_EQ(cwpe(k.P, P2(130, 120), CWP_SKIPDISABLED), k.trn);
    zo_reset();
}

TEST(win_position_zorder_hit_child_ex_flag_combinations) {
    NEED_GUI(); NEED_NT(NtUserChildWindowFromPointEx);
    struct cw k; REQUIRE(mkcw(&k));
    UINT all = CWP_SKIPINVISIBLE | CWP_SKIPDISABLED | CWP_SKIPTRANSPARENT;
    EXPECT_EQ(cwpe(k.P, P2(130, 20), all), k.P);
    EXPECT_EQ(cwpe(k.P, P2(130, 70), all), k.P);
    EXPECT_EQ(cwpe(k.P, P2(130, 120), all), k.P);
    EXPECT_EQ(cwpe(k.P, P2(50, 50), all), k.c2);
    EXPECT_EQ(cwpe(k.P, P2(12, 12), all), k.c1);
    zo_reset();
}

TEST(win_position_zorder_hit_child_ex_skipped_sibling_reveals_one_below) {
    NEED_GUI(); NEED_NT(NtUserChildWindowFromPointEx);
    struct cw k; REQUIRE(mkcw(&k));
    EXPECT_EQ(cwpe(k.P, P2(50, 50), CWP_ALL), k.c2);
    U(EnableWindow)(k.c2, FALSE);
    EXPECT_EQ(cwpe(k.P, P2(50, 50), CWP_SKIPDISABLED), k.c1);
    U(EnableWindow)(k.c2, TRUE);
    zo_hide(k.c2);
    EXPECT_EQ(cwpe(k.P, P2(50, 50), CWP_SKIPINVISIBLE), k.c1);
    EXPECT_EQ(cwpe(k.P, P2(50, 50), CWP_ALL), k.c2);
    zo_reset();
}

TEST(win_position_zorder_hit_child_ex_z_order_change) {
    NEED_GUI(); NEED_NT(NtUserChildWindowFromPointEx);
    struct cw k; REQUIRE(mkcw(&k));
    U(SetWindowPos)(k.c1, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    EXPECT_EQ(cwpe(k.P, P2(50, 50), CWP_ALL), k.c1);
    U(SetWindowPos)(k.c1, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    EXPECT_EQ(cwpe(k.P, P2(50, 50), CWP_ALL), k.c2);
    zo_reset();
}

TEST(win_position_zorder_hit_child_ex_uses_client_coordinates_of_framed_parent) {
    NEED_GUI(); NEED_NT(NtUserChildWindowFromPointEx);
    HWND P = zo_top_ex("cwf", ZO_OFFX, ZO_OFFY, 200, 200, WS_POPUP | WS_BORDER, 0, NULL, 1, NULL);
    REQUIRE(P);
    HWND c = zo_child(P, "cwfc", 10, 10, 50, 50, WS_VISIBLE, 0);
    REQUIRE(c);
    RECT w = zo_wr(P), cl = zo_cr_screen(P);
    CHECK(cl.left > w.left);                                /* the border makes client != window */
    EXPECT_EQ(cwpe(P, P2(11, 11), CWP_ALL), c);          /* client coordinates, so (11,11) is inside the child */
    EXPECT_EQ(cwpe(P, P2(5, 5), CWP_ALL), P);
    EXPECT_EQ(cwpe(P, P2(-1, -1), CWP_ALL), NULL);       /* left of the client area although inside the window frame */
    zo_reset();
}

TEST(win_position_zorder_hit_child_ex_invalid_hwnd) {
    NEED_GUI(); NEED_NT(NtUserChildWindowFromPointEx);
    HWND bad[] = { NULL, (HWND)1, (HWND)0x1234, (HWND)0xFFFF, (HWND)(ULONG_PTR)-1 };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(0);
        CHECKF(cwpe(bad[i], P2(5, 5), CWP_ALL) == NULL, "hwnd %p", bad[i]);
        CHECKF(GetLastError() == 1400, "hwnd %p error %lu want 1400", bad[i], GetLastError());
    }
}

TEST(win_position_zorder_hit_child_ex_unknown_flag_bits_note) {
    NEED_GUI(); NEED_NT(NtUserChildWindowFromPointEx);
    struct cw k; REQUIRE(mkcw(&k));
    UINT v[] = { 8, 0x10, 0x100, 0xffff0000u, 0xfffffff8u };
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) {
        SetLastError(0);
        HWND r = cwpe(k.P, P2(50, 50), v[i]);
        t_note("flags 0x%x -> %p (c2=%p) error %lu", v[i], r, k.c2, GetLastError());
    }
    zo_reset();
}

TEST(win_position_zorder_hit_child_ex_desktop_parent) {
    NEED_GUI(); NEED_NT(NtUserChildWindowFromPointEx);
    HWND a = zo_top("cwd_top", ZO_OFFX, ZO_OFFY, 100, 100, 1);
    REQUIRE(a);
    /* the desktop's "client" is the virtual screen, so a point off the screen is outside it */
    POINT p = P2(ZO_OFFX + 10, ZO_OFFY + 10);
    HWND r = cwpe(zo_desktop(), p, CWP_ALL);
    t_note("ChildWindowFromPointEx(desktop, off-screen point) -> %p (window %p)", r, a);
    CHECK(r == NULL || r == a || r == zo_desktop());
    zo_reset();
}

TEST(win_position_zorder_hit_child_nonex_equals_cwp_all) {
    NEED_GUI(); NEED_NT(NtUserChildWindowFromPointEx);
    struct cw k; REQUIRE(mkcw(&k));
    POINT ps[] = { P2(50, 50), P2(12, 12), P2(5, 5), P2(130, 20), P2(130, 70), P2(130, 120), P2(300, 300) };
    for (unsigned i = 0; i < sizeof ps / sizeof ps[0]; i++)
        CHECKF(U(ChildWindowFromPoint)(k.P, ps[i]) == cwpe(k.P, ps[i], 0), "point (%ld,%ld)", ps[i].x, ps[i].y);
    FARPROC f = t_proc_in("win32u.dll", "NtUserChildWindowFromPointEx");
    if (!t_is_wine()) EXPECT_EQ(k_stub_number(f), 5041);
    zo_reset();
}

TEST(win_position_zorder_hit_child_user32_ex_matches_stub) {
    NEED_GUI(); NEED_NT(NtUserChildWindowFromPointEx);
    struct cw k; REQUIRE(mkcw(&k));
    UINT fl[] = { 0, 1, 2, 4, 7 };
    POINT ps[] = { P2(50, 50), P2(130, 20), P2(130, 70), P2(130, 120) };
    for (unsigned f = 0; f < 5; f++) for (unsigned i = 0; i < 4; i++)
        CHECK(U(ChildWindowFromPointEx)(k.P, ps[i], fl[f]) == cwpe(k.P, ps[i], fl[f]));
    zo_reset();
}

TEST(win_position_zorder_hit_real_child_basic) {
    NEED_GUI(); NEED_NT(NtUserRealChildWindowFromPoint);
    struct cw k; REQUIRE(mkcw(&k));
    EXPECT_EQ(rcwp(k.P, P2(50, 50)), k.c2);
    EXPECT_EQ(rcwp(k.P, P2(12, 12)), k.c1);
    EXPECT_EQ(rcwp(k.P, P2(5, 5)), k.P);
    EXPECT_EQ(rcwp(k.P, P2(-3, 5)), NULL);
    EXPECT_EQ(rcwp(k.P, P2(300, 300)), NULL);
    EXPECT_EQ(U(RealChildWindowFromPoint)(k.P, P2(50, 50)), k.c2);
    zo_reset();
}

TEST(win_position_zorder_hit_real_child_flags_note) {
    NEED_GUI(); NEED_NT(NtUserRealChildWindowFromPoint);
    struct cw k; REQUIRE(mkcw(&k));
    HWND h = rcwp(k.P, P2(130, 20));
    HWND d = rcwp(k.P, P2(130, 70));
    HWND t = rcwp(k.P, P2(130, 120));
    t_note("RealChildWindowFromPoint: hidden child -> %s, disabled child -> %s, WS_EX_TRANSPARENT child -> %s",
           h == k.hid ? "child" : (h == k.P ? "parent" : "other"), d == k.dis ? "child" : (d == k.P ? "parent" : "other"),
           t == k.trn ? "child" : (t == k.P ? "parent" : "other"));
    CHECK(h == k.hid || h == k.P);
    CHECK(d == k.dis || d == k.P);
    CHECK(t == k.trn || t == k.P);
    zo_reset();
}

TEST(win_position_zorder_hit_real_child_invalid_hwnd) {
    NEED_GUI(); NEED_NT(NtUserRealChildWindowFromPoint);
    HWND bad[] = { NULL, (HWND)1, (HWND)0xFFFF };
    for (unsigned i = 0; i < 3; i++) {
        SetLastError(0);
        CHECKF(rcwp(bad[i], P2(5, 5)) == NULL, "hwnd %p", bad[i]);
        CHECKF(GetLastError() == 1400, "hwnd %p error %lu want 1400", bad[i], GetLastError());
    }
    FARPROC f = t_proc_in("win32u.dll", "NtUserRealChildWindowFromPoint");
    if (!t_is_wine()) EXPECT_EQ(k_stub_number(f), 5274);
}

TEST(win_position_zorder_hit_real_child_httransparent_note) {
    NEED_GUI(); NEED_NT(NtUserRealChildWindowFromPoint);
    struct cw k; REQUIRE(mkcw(&k));
    U(SetPropA)(k.c2, "zoHTT", (HANDLE)1);
    HWND r = rcwp(k.P, P2(50, 50));
    t_note("RealChildWindowFromPoint over an HTTRANSPARENT top child -> %s", r == k.c2 ? "that child" : (r == k.c1 ? "the child below" : (r == k.P ? "parent" : "other")));
    U(RemovePropA)(k.c2, "zoHTT");
    zo_reset();
}
