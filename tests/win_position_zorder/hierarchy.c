/* Window-hierarchy syscalls: NtUserGetAncestor, GetWindow (client-side walk of the desktop-heap
 * links), NtUserSetParent, owner changes through NtUserSetWindowLong(Ptr)(GWLP_HWNDPARENT), IsChild.
 *
 * win32kfull!NtUserGetAncestor: ValidateHwnd (1400) first, then flag must be 1..3 (GA_PARENT, GA_ROOT,
 * GA_ROOTOWNER) else ERROR_INVALID_PARAMETER (87); the desktop window has no ancestor.
 * user32!GetWindow -> GetWindowWorker walks spwndParent/Child/Owner/Next/Prev (+0x30/38/40/48/50)
 * of the read-only desktop-heap copy; it returns NULL silently for the desktop window (except
 * GW_CHILD), and sets ERROR_INVALID_GW_COMMAND (0x5a3 = 1443) for a command above 6.
 * NtUserSetParent: HWND 0 = desktop, -3 = HWND_MESSAGE root, fnid 0x29d/0x29f windows are refused
 * silently; SetParent hides the window, relinks it, keeps its parent-relative position
 * and re-shows it, returning the previous parent.
 */
#include "zo.h"

/* top T, children C1 (of T) and C2 (of C1), popup P owned by T, child PC of P */
struct tree { HWND T, C1, C2, P, PC; };
static int mktree(struct tree *t) {
    memset(t, 0, sizeof *t);
    t->T = zo_top("T", ZO_OFFX, ZO_OFFY, 300, 200, 1);
    if (!t->T) return 0;
    t->C1 = zo_child(t->T, "C1", 10, 10, 120, 100, WS_VISIBLE, 0);
    t->C2 = t->C1 ? zo_child(t->C1, "C2", 5, 5, 50, 40, WS_VISIBLE, 0) : NULL;
    t->P = zo_top_ex("P", ZO_OFFX + 400, ZO_OFFY, 100, 80, WS_POPUP, 0, t->T, 1, NULL);
    t->PC = t->P ? zo_child(t->P, "PC", 1, 1, 20, 20, WS_VISIBLE, 0) : NULL;
    return t->T && t->C1 && t->C2 && t->P && t->PC;
}

TEST(win_position_zorder_hier_getancestor_parent) {
    NEED_GUI(); NEED_NT(NtUserGetAncestor);
    struct tree t; REQUIRE(mktree(&t));
    HWND d = zo_desktop();
    EXPECT_EQ(UN(NtUserGetAncestor)(t.C2, GA_PARENT), t.C1);
    EXPECT_EQ(UN(NtUserGetAncestor)(t.C1, GA_PARENT), t.T);
    EXPECT_EQ(UN(NtUserGetAncestor)(t.T, GA_PARENT), d);
    EXPECT_EQ(UN(NtUserGetAncestor)(t.P, GA_PARENT), d);          /* owner is not the parent */
    EXPECT_EQ(UN(NtUserGetAncestor)(t.PC, GA_PARENT), t.P);
    EXPECT_EQ(UN(NtUserGetAncestor)(d, GA_PARENT), NULL);
    zo_reset();
}

TEST(win_position_zorder_hier_getancestor_root) {
    NEED_GUI(); NEED_NT(NtUserGetAncestor);
    struct tree t; REQUIRE(mktree(&t));
    EXPECT_EQ(UN(NtUserGetAncestor)(t.C2, GA_ROOT), t.T);
    EXPECT_EQ(UN(NtUserGetAncestor)(t.C1, GA_ROOT), t.T);
    EXPECT_EQ(UN(NtUserGetAncestor)(t.T, GA_ROOT), t.T);
    EXPECT_EQ(UN(NtUserGetAncestor)(t.P, GA_ROOT), t.P);          /* GA_ROOT ignores ownership */
    EXPECT_EQ(UN(NtUserGetAncestor)(t.PC, GA_ROOT), t.P);
    zo_reset();
}

TEST(win_position_zorder_hier_getancestor_rootowner) {
    NEED_GUI(); NEED_NT(NtUserGetAncestor);
    struct tree t; REQUIRE(mktree(&t));
    EXPECT_EQ(UN(NtUserGetAncestor)(t.C2, GA_ROOTOWNER), t.T);
    EXPECT_EQ(UN(NtUserGetAncestor)(t.T, GA_ROOTOWNER), t.T);
    EXPECT_EQ(UN(NtUserGetAncestor)(t.P, GA_ROOTOWNER), t.T);     /* follows the owner chain */
    EXPECT_EQ(UN(NtUserGetAncestor)(t.PC, GA_ROOTOWNER), t.T);
    zo_reset();
}

TEST(win_position_zorder_hier_getancestor_invalid_flag_table) {
    NEED_GUI(); NEED_NT(NtUserGetAncestor);
    struct tree t; REQUIRE(mktree(&t));
    UINT v[] = { 0, 4, 5, 6, 7, 8, 100, 0x10000, 0x80000000u, 0xffffffffu };
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) {
        SetLastError(0);
        CHECKF(UN(NtUserGetAncestor)(t.C1, v[i]) == NULL, "flag %u", v[i]);
        CHECKF(GetLastError() == 87, "flag %u error %lu want 87", v[i], GetLastError());
    }
    zo_reset();
}

TEST(win_position_zorder_hier_getancestor_invalid_hwnd_before_flag) {
    NEED_GUI(); NEED_NT(NtUserGetAncestor);
    HWND bad[] = { NULL, (HWND)1, (HWND)0xFFFF, (HWND)(ULONG_PTR)-1 };
    for (unsigned i = 0; i < 4; i++) {
        SetLastError(0);
        CHECKF(UN(NtUserGetAncestor)(bad[i], GA_PARENT) == NULL, "hwnd %p", bad[i]);
        CHECKF(GetLastError() == 1400, "hwnd %p error %lu want 1400", bad[i], GetLastError());
        SetLastError(0);
        CHECK(UN(NtUserGetAncestor)(bad[i], 77) == NULL);
        CHECKF(GetLastError() == 1400, "hwnd %p + bad flag error %lu want 1400 (handle checked first)", bad[i], GetLastError());
    }
}

TEST(win_position_zorder_hier_getancestor_destroyed_window) {
    NEED_GUI(); NEED_NT(NtUserGetAncestor);
    HWND w = zo_top("ga_gone", ZO_OFFX, ZO_OFFY, 20, 20, 1);
    REQUIRE(w);
    U(DestroyWindow)(w);
    SetLastError(0);
    CHECK(UN(NtUserGetAncestor)(w, GA_ROOT) == NULL);
    EXPECT_ERR(1400);
    zo_reset();
}

TEST(win_position_zorder_hier_getancestor_matches_user32_and_stub_number) {
    NEED_GUI(); NEED_NT(NtUserGetAncestor);
    struct tree t; REQUIRE(mktree(&t));
    for (UINT f = 1; f <= 3; f++) EXPECT_EQ(U(GetAncestor)(t.C2, f), UN(NtUserGetAncestor)(t.C2, f));
    FARPROC p = t_proc_in("win32u.dll", "NtUserGetAncestor");
    if (!t_is_wine()) EXPECT_EQ(k_stub_number(p), 4266);
    zo_reset();
}

TEST(win_position_zorder_hier_getancestor_message_only_window) {
    NEED_GUI(); NEED_NT(NtUserGetAncestor);
    HWND m = zo_top_ex("msgonly", 0, 0, 10, 10, WS_POPUP, 0, HWND_MESSAGE, 0, NULL);
    if (!m) {
        m = U(CreateWindowExA)(0, zo_class(), "msgonly", WS_POPUP, 0, 0, 10, 10, HWND_MESSAGE, NULL, zo_hinst(), NULL);
    }
    if (!m) { t_skip("message-only windows unsupported"); return; }
    HWND par = UN(NtUserGetAncestor)(m, GA_PARENT);
    CHECK(par != NULL);
    CHECK(par != zo_desktop());
    EXPECT_EQ(UN(NtUserGetAncestor)(m, GA_ROOT), m);              /* the message root is not a "desktop" parent for GA_ROOT */
    t_note("message-only parent = %p desktop = %p", par, zo_desktop());
    CHECK(U(IsWindow)(par));
    U(DestroyWindow)(m);
    zo_reset();
}

TEST(win_position_zorder_hier_getwindow_siblings_table) {
    NEED_GUI();
    HWND p = zo_top("gwp", ZO_OFFX, ZO_OFFY, 200, 150, 1);
    REQUIRE(p);
    HWND c[3];
    for (int i = 0; i < 3; i++) { c[i] = zo_child(p, "gw", 5 * i, 5 * i, 40, 40, WS_VISIBLE, 0); REQUIRE(c[i]); }
    zo_arrange(c, 3);                    /* c0 top, c1, c2 bottom */
    EXPECT_EQ(U(GetWindow)(c[1], GW_HWNDFIRST), c[0]);
    EXPECT_EQ(U(GetWindow)(c[1], GW_HWNDLAST), c[2]);
    EXPECT_EQ(U(GetWindow)(c[0], GW_HWNDFIRST), c[0]);
    EXPECT_EQ(U(GetWindow)(c[2], GW_HWNDLAST), c[2]);
    EXPECT_EQ(U(GetWindow)(c[0], GW_HWNDNEXT), c[1]);
    EXPECT_EQ(U(GetWindow)(c[1], GW_HWNDNEXT), c[2]);
    EXPECT_EQ(U(GetWindow)(c[2], GW_HWNDNEXT), NULL);
    EXPECT_EQ(U(GetWindow)(c[0], GW_HWNDPREV), NULL);
    EXPECT_EQ(U(GetWindow)(c[1], GW_HWNDPREV), c[0]);
    EXPECT_EQ(U(GetWindow)(c[2], GW_HWNDPREV), c[1]);
    EXPECT_EQ(U(GetWindow)(p, GW_CHILD), c[0]);
    EXPECT_EQ(U(GetWindow)(c[0], GW_CHILD), NULL);
    EXPECT_EQ(U(GetWindow)(c[0], GW_OWNER), NULL);                 /* children have no owner */
    EXPECT_EQ(U(GetTopWindow)(p), c[0]);
    zo_reset();
}

TEST(win_position_zorder_hier_getwindow_owner) {
    NEED_GUI();
    struct tree t; REQUIRE(mktree(&t));
    EXPECT_EQ(U(GetWindow)(t.P, GW_OWNER), t.T);
    EXPECT_EQ(U(GetWindow)(t.T, GW_OWNER), NULL);
    EXPECT_EQ(U(GetWindow)(t.PC, GW_OWNER), NULL);
    EXPECT_EQ(U(GetParent)(t.P), t.T);        /* a popup's "parent" is its owner (user32!GetParent, WS_POPUP branch) */
    EXPECT_EQ(U(GetParent)(t.T), NULL);
    EXPECT_EQ(U(GetParent)(t.C1), t.T);
    EXPECT_EQ(U(GetParent)(t.PC), t.P);
    zo_reset();
}

TEST(win_position_zorder_hier_getwindow_invalid_command_table) {
    NEED_GUI();
    HWND w = zo_top("gwbad", ZO_OFFX, ZO_OFFY, 20, 20, 1);
    REQUIRE(w);
    UINT v[] = { 7, 8, 9, 100, 0x10000, 0xffffffffu };
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) {
        SetLastError(0);
        CHECKF(U(GetWindow)(w, v[i]) == NULL, "cmd %u", v[i]);
        CHECKF(GetLastError() == 1443, "cmd %u error %lu want 1443 (ERROR_INVALID_GW_COMMAND)", v[i], GetLastError());
    }
    zo_reset();
}

TEST(win_position_zorder_hier_getwindow_invalid_hwnd) {
    NEED_GUI();
    HWND bad[] = { NULL, (HWND)1, (HWND)0xFFFF, (HWND)(ULONG_PTR)-1 };
    for (unsigned i = 0; i < 4; i++) {
        SetLastError(0);
        CHECKF(U(GetWindow)(bad[i], GW_HWNDNEXT) == NULL, "hwnd %p", bad[i]);
        CHECKF(GetLastError() == 1400, "hwnd %p error %lu want 1400", bad[i], GetLastError());
    }
}

TEST(win_position_zorder_hier_getwindow_desktop_window) {
    NEED_GUI();
    HWND d = zo_desktop();
    REQUIRE(d);
    /* GetWindowWorker: fnid 0x29d + cmd != GW_CHILD -> NULL, no error */
    UINT cmds[] = { GW_HWNDFIRST, GW_HWNDLAST, GW_HWNDNEXT, GW_HWNDPREV, GW_OWNER, GW_ENABLEDPOPUP };
    for (unsigned i = 0; i < 6; i++) {
        SetLastError(0);
        CHECKF(U(GetWindow)(d, cmds[i]) == NULL, "cmd %u on the desktop window", cmds[i]);
        CHECKF(GetLastError() == 0, "cmd %u error %lu", cmds[i], GetLastError());
    }
    HWND top = U(GetWindow)(d, GW_CHILD);
    CHECK(top != NULL);
    EXPECT_EQ(top, U(GetTopWindow)(NULL));
    EXPECT_EQ(top, U(GetTopWindow)(d));
}

TEST(win_position_zorder_hier_getwindow_enabledpopup) {
    NEED_GUI();
    struct tree t; REQUIRE(mktree(&t));
    EXPECT_EQ(U(GetWindow)(t.T, GW_ENABLEDPOPUP), t.P);           /* the enabled popup owned by T */
    EXPECT_EQ(U(GetWindow)(t.P, GW_ENABLEDPOPUP), 0);             /* none: NULL (the documented "the window itself" is not what Windows 10 returns) */
    U(EnableWindow)(t.P, FALSE);
    EXPECT_EQ(U(GetWindow)(t.T, GW_ENABLEDPOPUP), 0);             /* disabled popups don't count */
    U(EnableWindow)(t.P, TRUE);
    zo_reset();
}

TEST(win_position_zorder_hier_getwindow_toplevel_chain_matches_heap) {
    NEED_GUI();
    if (t_is_wine() || !zo_heap_ok()) { t_skip("desktop heap windows not readable"); return; }
    HWND w[4];
    for (int i = 0; i < 4; i++) { w[i] = zo_top("chain", ZO_OFFX + i, ZO_OFFY, 30, 30, 1); REQUIRE(w[i]); }
    zo_arrange(w, 4);
    for (int i = 0; i < 4; i++) {
        CHECK(zo_link(w[i], ZO_WND_Parent) == zo_desktop());
        CHECK(zo_link(w[i], ZO_WND_Next) == U(GetWindow)(w[i], GW_HWNDNEXT));
        CHECK(zo_link(w[i], ZO_WND_Prev) == U(GetWindow)(w[i], GW_HWNDPREV));
        CHECK(zo_link(w[i], ZO_WND_Child) == U(GetWindow)(w[i], GW_CHILD));
        CHECK(zo_link(w[i], ZO_WND_Owner) == U(GetWindow)(w[i], GW_OWNER));
    }
    /* The desktop's first child in the heap may be a window the public APIs skip (windows of other bands), so the
     * public top window must be reachable from it through the heap sibling links. */
    HWND first = zo_link(zo_desktop(), ZO_WND_Child), top = U(GetWindow)(zo_desktop(), GW_CHILD);
    int found = 0;
    for (HWND c = first; c && !found; c = zo_link(c, ZO_WND_Next)) { if (c == top) found = 1; if (c == zo_link(c, ZO_WND_Next)) break; }
    CHECK(found);
    CHECK(top == U(GetTopWindow)(NULL));
    zo_reset();
}

TEST(win_position_zorder_hier_heap_links_for_tree) {
    NEED_GUI();
    if (t_is_wine() || !zo_heap_ok()) { t_skip("desktop heap windows not readable"); return; }
    struct tree t; REQUIRE(mktree(&t));
    CHECK(zo_link(t.C1, ZO_WND_Parent) == t.T);
    CHECK(zo_link(t.C2, ZO_WND_Parent) == t.C1);
    CHECK(zo_link(t.T, ZO_WND_Child) == t.C1);
    CHECK(zo_link(t.C1, ZO_WND_Child) == t.C2);
    CHECK(zo_link(t.C2, ZO_WND_Child) == NULL);
    CHECK(zo_link_valid(t.C2, ZO_WND_Child));
    CHECK(zo_link(t.P, ZO_WND_Owner) == t.T);
    CHECK(zo_link(t.P, ZO_WND_Parent) == zo_desktop());
    CHECK(zo_link(t.PC, ZO_WND_Parent) == t.P);
    CHECK(zo_link(t.PC, ZO_WND_Owner) == NULL);
    zo_reset();
}

TEST(win_position_zorder_hier_heap_style_bits_child_popup) {
    NEED_GUI();
    if (t_is_wine() || !zo_heap_ok()) { t_skip("desktop heap windows not readable"); return; }
    struct tree t; REQUIRE(mktree(&t));
    int ok;
    unsigned long sc = zo_style_heap(t.C1, &ok);  CHECK(ok && (sc & WS_CHILD) && !(sc & WS_POPUP));
    unsigned long sp = zo_style_heap(t.P, &ok);   CHECK(ok && (sp & WS_POPUP) && !(sp & WS_CHILD));
    CHECK((unsigned long)U(GetWindowLongA)(t.C1, GWL_STYLE) == sc);
    CHECK((unsigned long)U(GetWindowLongA)(t.P, GWL_STYLE) == sp);
    unsigned long ex = zo_exstyle_heap(t.T, &ok);
    /* GetWindowData(GWL_EXSTYLE) masks the stored dword with 0x0a7777ff (+0x8000 when bit 5 of +0xe8 is clear) */
    CHECK(ok && ((ULONG)U(GetWindowLongA)(t.T, GWL_EXSTYLE) & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)) == (ex & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)));
    CHECK(ok && (ex & WS_EX_TOOLWINDOW) && (ex & WS_EX_NOACTIVATE));
    zo_reset();
}

TEST(win_position_zorder_hier_ischild_table) {
    NEED_GUI();
    struct tree t; REQUIRE(mktree(&t));
    CHECK(U(IsChild)(t.T, t.C1));
    CHECK(U(IsChild)(t.T, t.C2));          /* any descendant */
    CHECK(U(IsChild)(t.C1, t.C2));
    CHECK(!U(IsChild)(t.C1, t.T));
    CHECK(!U(IsChild)(t.T, t.P));          /* owned, not a child */
    CHECK(!U(IsChild)(t.T, t.PC));
    CHECK(U(IsChild)(t.P, t.PC));
    CHECK(!U(IsChild)(t.C2, t.C1));
    SetLastError(0);
    CHECK(!U(IsChild)((HWND)1, t.C1));
    CHECK(!U(IsChild)(t.T, (HWND)1));
    t_note("IsChild(w, w) = %d", U(IsChild)(t.C1, t.C1));
    zo_reset();
}

/* ---- SetParent ---- */

TEST(win_position_zorder_hier_setparent_returns_previous_and_relinks) {
    NEED_GUI(); NEED_NT(NtUserSetParent);
    HWND p1 = zo_top("sp1", ZO_OFFX, ZO_OFFY, 200, 150, 1), p2 = zo_top("sp2", ZO_OFFX + 300, ZO_OFFY, 200, 150, 1);
    REQUIRE(p1 && p2);
    HWND c = zo_child(p1, "spc", 11, 12, 40, 30, WS_VISIBLE, 0);
    REQUIRE(c);
    CHECK(UN(NtUserSetParent)(c, p2) == p1);
    EXPECT_EQ(U(GetParent)(c), p2);
    EXPECT_EQ(U(GetAncestor)(c, GA_PARENT), p2);
    CHECK(U(IsChild)(p2, c)); CHECK(!U(IsChild)(p1, c));
    CHECK(U(GetWindow)(p1, GW_CHILD) == NULL);
    CHECK(U(GetWindow)(p2, GW_CHILD) == c);
    CHECK(U(IsWindowVisible)(c));                                  /* hidden for the move, shown again afterwards */
    zo_reset();
}

TEST(win_position_zorder_hier_setparent_keeps_parent_relative_position) {
    NEED_GUI(); NEED_NT(NtUserSetParent);
    HWND p1 = zo_top("spr1", ZO_OFFX, ZO_OFFY, 200, 150, 1), p2 = zo_top("spr2", ZO_OFFX + 300, ZO_OFFY + 50, 200, 150, 1);
    REQUIRE(p1 && p2);
    HWND c = zo_child(p1, "sprc", 11, 12, 40, 30, WS_VISIBLE, 0);
    REQUIRE(c);
    UN(NtUserSetParent)(c, p2);
    /* SetParent converts the window origin to the OLD parent's client coordinates and applies those same numbers in the new parent */
    RECT r = zo_wr(c), pc = zo_cr_screen(p2);
    EXPECT_EQ(r.left, pc.left + 11); EXPECT_EQ(r.top, pc.top + 12);
    EXPECT_EQ(r.right - r.left, 40); EXPECT_EQ(r.bottom - r.top, 30);
    zo_reset();
}

TEST(win_position_zorder_hier_setparent_heap_links_updated) {
    NEED_GUI(); NEED_NT(NtUserSetParent);
    if (t_is_wine() || !zo_heap_ok()) { t_skip("desktop heap windows not readable"); return; }
    HWND p1 = zo_top("sph1", ZO_OFFX, ZO_OFFY, 200, 150, 1), p2 = zo_top("sph2", ZO_OFFX + 300, ZO_OFFY, 200, 150, 1);
    REQUIRE(p1 && p2);
    HWND c = zo_child(p1, "sphc", 1, 1, 40, 30, WS_VISIBLE, 0), c2 = zo_child(p1, "sphc2", 1, 1, 40, 30, WS_VISIBLE, 0);
    REQUIRE(c && c2);
    CHECK(zo_link(c, ZO_WND_Parent) == p1);
    UN(NtUserSetParent)(c, p2);
    CHECK(zo_link(c, ZO_WND_Parent) == p2);
    CHECK(zo_link(p2, ZO_WND_Child) == c);
    CHECK(zo_link(p1, ZO_WND_Child) == c2);
    CHECK(zo_link(c2, ZO_WND_Next) == NULL && zo_link(c2, ZO_WND_Prev) == NULL);
    RECT hw, api = zo_wr(c);
    CHECK(zo_rect_heap(c, ZO_WND_RcWnd, &hw)); CHECK(zo_same_rect(&hw, &api));
    zo_reset();
}

TEST(win_position_zorder_hier_setparent_null_means_desktop) {
    NEED_GUI(); NEED_NT(NtUserSetParent);
    HWND p = zo_top("spn", ZO_OFFX, ZO_OFFY, 200, 150, 1);
    REQUIRE(p);
    HWND c = zo_child(p, "spnc", 20, 30, 40, 30, 0, 0);       /* hidden: it lands at screen (20,30) and must not flash */
    REQUIRE(c);
    RECT before = zo_wr(c), pc = zo_cr_screen(p);
    CHECK(UN(NtUserSetParent)(c, NULL) == p);
    EXPECT_EQ(U(GetAncestor)(c, GA_PARENT), zo_desktop());
    EXPECT_EQ(U(GetParent)(c), zo_desktop());             /* WS_CHILD survives SetParent: GetParent returns spwndParent */
    CHECK(U(GetWindowLongA)(c, GWL_STYLE) & WS_CHILD);
    CHECK(!U(IsChild)(p, c));
    RECT r = zo_wr(c);
    /* old-parent-relative (20,30) is applied against the desktop, i.e. as screen coordinates */
    EXPECT_EQ(r.left, 20); EXPECT_EQ(r.top, 30);
    t_note("child moved from screen (%ld,%ld) to (%ld,%ld); parent client origin was (%ld,%ld)", before.left, before.top, r.left, r.top, pc.left, pc.top);
    /* park it off-screen again before it is destroyed */
    U(SetWindowPos)(c, NULL, ZO_OFFX, ZO_OFFY, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    zo_reset();
}

TEST(win_position_zorder_hier_setparent_toplevel_becomes_child) {
    NEED_GUI(); NEED_NT(NtUserSetParent);
    zo_ghost = 0;    /* layered top-level windows cannot become children */
    HWND p = zo_top("spt", ZO_OFFX + 500, ZO_OFFY + 500, 300, 200, 1);
    REQUIRE(p);
    HWND w = zo_top("sptw", ZO_OFFX + 13, ZO_OFFY + 17, 60, 50, 1);
    REQUIRE(w);
    HWND old = UN(NtUserSetParent)(w, p);
    CHECK(old == zo_desktop());                           /* previous parent of a top-level window is the desktop */
    EXPECT_EQ(U(GetAncestor)(w, GA_PARENT), p);
    EXPECT_EQ(U(GetParent)(w), NULL);                     /* still WS_POPUP: GetParent returns the OWNER (none), not spwndParent */
    EXPECT_EQ(U(GetWindowLongPtrA)(w, GWLP_HWNDPARENT), (LONG_PTR)p);
    RECT r = zo_wr(w), pc = zo_cr_screen(p);
    EXPECT_EQ(r.left, pc.left + (ZO_OFFX + 13)); EXPECT_EQ(r.top, pc.top + (ZO_OFFY + 17));
    CHECK(U(GetWindowLongA)(w, GWL_STYLE) & WS_POPUP);    /* the style is left alone */
    CHECK(!(U(GetWindowLongA)(w, GWL_STYLE) & WS_CHILD));
    UN(NtUserSetParent)(w, NULL);                         /* restore */
    EXPECT_EQ(U(GetAncestor)(w, GA_PARENT), zo_desktop());
    U(SetWindowPos)(w, NULL, ZO_OFFX + 13, ZO_OFFY + 17, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    zo_reset();
}

TEST(win_position_zorder_hier_setparent_visible_state_preserved) {
    NEED_GUI(); NEED_NT(NtUserSetParent);
    HWND p1 = zo_top("spv1", ZO_OFFX, ZO_OFFY, 200, 150, 1), p2 = zo_top("spv2", ZO_OFFX + 300, ZO_OFFY, 200, 150, 1);
    REQUIRE(p1 && p2);
    HWND vis = zo_child(p1, "v", 0, 0, 20, 20, WS_VISIBLE, 0), hid = zo_child(p1, "h", 0, 0, 20, 20, 0, 0);
    REQUIRE(vis && hid);
    UN(NtUserSetParent)(vis, p2);
    UN(NtUserSetParent)(hid, p2);
    CHECK(U(IsWindowVisible)(vis));
    CHECK(!U(IsWindowVisible)(hid));
    CHECK(!(U(GetWindowLongA)(hid, GWL_STYLE) & WS_VISIBLE));
    zo_reset();
}

TEST(win_position_zorder_hier_setparent_into_hidden_parent) {
    NEED_GUI(); NEED_NT(NtUserSetParent);
    HWND p1 = zo_top("sph", ZO_OFFX, ZO_OFFY, 200, 150, 1), p2 = zo_top("sph_h", ZO_OFFX + 300, ZO_OFFY, 200, 150, 0);
    REQUIRE(p1 && p2);
    HWND c = zo_child(p1, "c", 0, 0, 20, 20, WS_VISIBLE, 0);
    REQUIRE(c);
    UN(NtUserSetParent)(c, p2);
    CHECK(U(GetWindowLongA)(c, GWL_STYLE) & WS_VISIBLE);
    CHECK(!U(IsWindowVisible)(c));
    zo_reset();
}

TEST(win_position_zorder_hier_setparent_invalid_arguments) {
    NEED_GUI(); NEED_NT(NtUserSetParent);
    HWND p = zo_top("spi", ZO_OFFX, ZO_OFFY, 200, 150, 1);
    REQUIRE(p);
    HWND c = zo_child(p, "spic", 0, 0, 20, 20, WS_VISIBLE, 0);
    REQUIRE(c);
    SetLastError(0);
    CHECK(UN(NtUserSetParent)((HWND)1, p) == NULL);  EXPECT_ERR(1400);
    SetLastError(0);
    CHECK(UN(NtUserSetParent)(c, (HWND)0x1234) == NULL);  EXPECT_ERR(1400);
    SetLastError(0);
    CHECK(UN(NtUserSetParent)(NULL, p) == NULL);  EXPECT_ERR(1400);
    EXPECT_EQ(U(GetParent)(c), p);                   /* nothing happened */
    zo_reset();
}

TEST(win_position_zorder_hier_setparent_desktop_window_as_child_is_refused) {
    NEED_GUI(); NEED_NT(NtUserSetParent);
    HWND p = zo_top("spd", ZO_OFFX, ZO_OFFY, 200, 150, 1);
    REQUIRE(p);
    /* fnid 0x29d (desktop) child argument: NtUserSetParent returns NULL right after ValidateHwnd */
    CHECK(UN(NtUserSetParent)(zo_desktop(), p) == NULL);
    EXPECT_EQ(U(GetAncestor)(zo_desktop(), GA_PARENT), NULL);
    zo_reset();
}

TEST(win_position_zorder_hier_setparent_self_and_cycles_refused) {
    NEED_GUI(); NEED_NT(NtUserSetParent);
    struct tree t; REQUIRE(mktree(&t));
    CHECK(UN(NtUserSetParent)(t.C1, t.C1) == NULL);
    EXPECT_EQ(U(GetParent)(t.C1), t.T);
    SetLastError(0);
    CHECK(UN(NtUserSetParent)(t.T, t.C2) == NULL);        /* a window cannot become a child of its own descendant */
    t_note("cycle SetParent error %lu", GetLastError());
    EXPECT_EQ(U(GetAncestor)(t.T, GA_PARENT), zo_desktop());
    EXPECT_EQ(U(GetParent)(t.C2), t.C1);
    CHECK(UN(NtUserSetParent)(t.C1, t.C2) == NULL);
    EXPECT_EQ(U(GetParent)(t.C1), t.T);
    zo_reset();
}

TEST(win_position_zorder_hier_setparent_same_parent_returns_parent) {
    NEED_GUI(); NEED_NT(NtUserSetParent);
    HWND p = zo_top("spsame", ZO_OFFX, ZO_OFFY, 200, 150, 1);
    REQUIRE(p);
    HWND c = zo_child(p, "spsc", 4, 5, 20, 20, WS_VISIBLE, 0);
    REQUIRE(c);
    RECT before = zo_wr(c);
    HWND r = UN(NtUserSetParent)(c, p);
    CHECK(r == p);
    CHECK(U(GetParent)(c) == p);
    RECT after = zo_wr(c);
    CHECK(zo_same_rect(&before, &after));
    zo_reset();
}

struct seenctx { HWND w; int seen; };
static BOOL CALLBACK seen_cb(HWND h, LPARAM l) { struct seenctx *c = (struct seenctx *)l; if (h == c->w) c->seen++; return TRUE; }

TEST(win_position_zorder_hier_setparent_message_root_round_trip) {
    NEED_GUI(); NEED_NT(NtUserSetParent);
    zo_ghost = 0;    /* layered top-level windows cannot become children */
    HWND w = zo_top("spm", ZO_OFFX, ZO_OFFY, 60, 50, 1);
    REQUIRE(w);
    HWND d = zo_desktop();
    HWND old = UN(NtUserSetParent)(w, HWND_MESSAGE);
    CHECK(old == d);
    HWND root = U(GetAncestor)(w, GA_PARENT);
    CHECK(root != NULL && root != d);
    /* a message-only window is invisible to desktop enumeration */
    struct seenctx ctx = { w, 0 };
    U(EnumWindows)(seen_cb, (LPARAM)&ctx);
    CHECK(ctx.seen == 0);
    HWND back = UN(NtUserSetParent)(w, NULL);
    CHECK(back == root);                                  /* the previous parent returned is the message root */
    EXPECT_EQ(U(GetAncestor)(w, GA_PARENT), d);
    ctx.seen = 0;
    U(EnumWindows)(seen_cb, (LPARAM)&ctx);
    CHECK(ctx.seen == 1);
    zo_reset();
}

TEST(win_position_zorder_hier_setparent_user32_matches_stub) {
    NEED_GUI(); NEED_NT(NtUserSetParent);
    HWND p1 = zo_top("spu1", ZO_OFFX, ZO_OFFY, 100, 100, 1), p2 = zo_top("spu2", ZO_OFFX + 200, ZO_OFFY, 100, 100, 1);
    REQUIRE(p1 && p2);
    HWND c = zo_child(p1, "spuc", 0, 0, 10, 10, WS_VISIBLE, 0);
    REQUIRE(c);
    CHECK(U(SetParent)(c, p2) == p1);
    CHECK(UN(NtUserSetParent)(c, p1) == p2);
    FARPROC p = t_proc_in("win32u.dll", "NtUserSetParent");
    if (!t_is_wine()) EXPECT_EQ(k_stub_number(p), 4213);
    zo_reset();
}

TEST(win_position_zorder_hier_setparent_new_child_zorder_note) {
    NEED_GUI(); NEED_NT(NtUserSetParent);
    HWND p1 = zo_top("spz1", ZO_OFFX, ZO_OFFY, 100, 100, 1), p2 = zo_top("spz2", ZO_OFFX + 200, ZO_OFFY, 100, 100, 1);
    REQUIRE(p1 && p2);
    HWND a = zo_child(p2, "a", 0, 0, 10, 10, WS_VISIBLE, 0), b = zo_child(p2, "b", 0, 0, 10, 10, WS_VISIBLE, 0);
    HWND c = zo_child(p1, "c", 0, 0, 10, 10, WS_VISIBLE, 0);
    REQUIRE(a && b && c);
    UN(NtUserSetParent)(c, p2);
    HWND first = U(GetWindow)(p2, GW_CHILD), last = U(GetWindow)(c, GW_HWNDLAST);
    t_note("reparented child is %s of its new siblings", first == c ? "first (topmost)" : (last == c ? "last (bottom)" : "in the middle"));
    CHECK(first == c || last == c);
    zo_reset();
}

TEST(win_position_zorder_hier_setparent_removes_from_owner_chain_note) {
    NEED_GUI(); NEED_NT(NtUserSetParent);
    zo_ghost = 0;    /* layered top-level windows cannot become children */
    struct tree t; REQUIRE(mktree(&t));
    HWND p2 = zo_top("spo", ZO_OFFX + 700, ZO_OFFY, 100, 100, 1);
    REQUIRE(p2);
    HWND old = UN(NtUserSetParent)(t.P, p2);                       /* an OWNED popup becomes a child */
    t_note("SetParent(owned popup) -> %p (T=%p desktop=%p)", old, t.T, zo_desktop());
    t_note("owner after: %p, parent after: %p", U(GetWindow)(t.P, GW_OWNER), U(GetAncestor)(t.P, GA_PARENT));
    EXPECT_EQ(U(GetAncestor)(t.P, GA_PARENT), p2);
    UN(NtUserSetParent)(t.P, NULL);
    zo_reset();
}

/* cross-thread: both threads sit in GetMessage, so the synchronous messages SetParent sends complete */
struct xargs { HWND child, parent, result; };
static ULONG_PTR xsp_fn(void *p) { struct xargs *a = (struct xargs *)p; a->result = UN(NtUserSetParent)(a->child, a->parent); return 1; }

TEST(win_position_zorder_hier_setparent_cross_thread_child_into_local_parent) {
    NEED_GUI(); NEED_NT(NtUserSetParent);
    zo_ghost = 0;    /* layered top-level windows cannot become children */
    struct zo_thread *t = zo_thread_start();
    if (!t) { t_skip("could not start GUI thread"); return; }
    HWND w = zo_thread_window(t, "xsp_child", ZO_OFFX + 400, ZO_OFFY, 60, 50, 1);
    if (!w) { zo_thread_stop(t); t_skip("window on worker thread failed"); return; }
    HWND p = zo_top("xsp_parent", ZO_OFFX, ZO_OFFY, 300, 200, 1);
    REQUIRE(p);
    SetLastError(0);
    HWND old = UN(NtUserSetParent)(w, p);
    t_note("cross-thread SetParent -> %p err %lu", old, GetLastError());
    CHECK(old == zo_desktop());
    EXPECT_EQ(U(GetAncestor)(w, GA_PARENT), p);
    CHECK(U(GetWindowThreadProcessId)(w, NULL) == t->tid);        /* ownership of the window itself does not move */
    UN(NtUserSetParent)(w, NULL);
    EXPECT_EQ(U(GetAncestor)(w, GA_PARENT), zo_desktop());
    zo_reset();
    zo_thread_stop(t);
}

TEST(win_position_zorder_hier_setparent_cross_thread_called_on_owner_thread) {
    NEED_GUI(); NEED_NT(NtUserSetParent);
    zo_ghost = 0;    /* layered top-level windows cannot become children */
    struct zo_thread *t = zo_thread_start();
    if (!t) { t_skip("could not start GUI thread"); return; }
    HWND w = zo_thread_window(t, "xsp2_child", ZO_OFFX + 400, ZO_OFFY, 60, 50, 1);
    if (!w) { zo_thread_stop(t); t_skip("window on worker thread failed"); return; }
    HWND p = zo_top("xsp2_parent", ZO_OFFX, ZO_OFFY, 300, 200, 1);
    REQUIRE(p);
    struct xargs a = { w, p, NULL };
    zo_thread_call(t, xsp_fn, &a);                                 /* owner thread reparents its window under a window of ANOTHER thread */
    CHECK(a.result == zo_desktop());
    EXPECT_EQ(U(GetAncestor)(w, GA_PARENT), p);
    a.parent = NULL;
    zo_thread_call(t, xsp_fn, &a);
    EXPECT_EQ(a.result, p);
    zo_reset();
    zo_thread_stop(t);
}

/* ---- owner via SetWindowLongPtr(GWLP_HWNDPARENT) ---- */

TEST(win_position_zorder_hier_owner_set_and_clear) {
    NEED_GUI();
    HWND o = zo_top("own1", ZO_OFFX, ZO_OFFY, 80, 80, 1), w = zo_top("own2", ZO_OFFX + 100, ZO_OFFY, 60, 60, 1);
    REQUIRE(o && w);
    EXPECT_EQ(U(GetWindow)(w, GW_OWNER), NULL);
    SetLastError(0);
    LONG_PTR prev = U(SetWindowLongPtrA)(w, GWLP_HWNDPARENT, (LONG_PTR)o);
    t_note("SetWindowLongPtr(GWLP_HWNDPARENT) previous value = %p", (void *)prev);
    EXPECT_EQ(U(GetWindow)(w, GW_OWNER), o);
    EXPECT_EQ(U(GetParent)(w), o);
    EXPECT_EQ(U(GetWindowLongPtrA)(w, GWLP_HWNDPARENT), (LONG_PTR)o);   /* GetWindowData(-8): parent is the desktop -> owner */
    EXPECT_EQ(U(GetAncestor)(w, GA_ROOTOWNER), o);
    EXPECT_EQ(U(GetAncestor)(w, GA_PARENT), zo_desktop());
    if (!t_is_wine() && zo_heap_ok()) CHECK(zo_link(w, ZO_WND_Owner) == o);
    prev = U(SetWindowLongPtrA)(w, GWLP_HWNDPARENT, 0);
    EXPECT_EQ(prev, (LONG_PTR)o);                                       /* previous owner is returned */
    EXPECT_EQ(U(GetWindow)(w, GW_OWNER), NULL);
    EXPECT_EQ(U(GetAncestor)(w, GA_ROOTOWNER), w);
    zo_reset();
}

TEST(win_position_zorder_hier_owner_change_reorders_owned_above_owner) {
    NEED_GUI();
    HWND o = zo_top("ownz1", ZO_OFFX, ZO_OFFY, 80, 80, 1), w = zo_top("ownz2", ZO_OFFX + 100, ZO_OFFY, 60, 60, 1);
    REQUIRE(o && w);
    HWND in[2] = { o, w }, got[2];
    zo_arrange(in, 2);                                 /* o above w */
    U(SetWindowLongPtrA)(w, GWLP_HWNDPARENT, (LONG_PTR)o);
    CHECK(zo_rank(in, 2, got) == 2);
    t_note("after making w owned by o: top is %s", got[0] == w ? "w (owned)" : "o (owner)");
    U(SetWindowPos)(o, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    CHECK(zo_rank(in, 2, got) == 2 && got[0] == w);    /* any z change of the owner re-asserts owned-above-owner */
    zo_reset();
}

TEST(win_position_zorder_hier_owner_self_and_cycle_refused) {
    NEED_GUI();
    HWND a = zo_top("cyc_a", ZO_OFFX, ZO_OFFY, 40, 40, 1), b = zo_top("cyc_b", ZO_OFFX + 50, ZO_OFFY, 40, 40, 1);
    REQUIRE(a && b);
    U(SetWindowLongPtrA)(b, GWLP_HWNDPARENT, (LONG_PTR)a);
    EXPECT_EQ(U(GetWindow)(b, GW_OWNER), a);
    SetLastError(0);
    U(SetWindowLongPtrA)(a, GWLP_HWNDPARENT, (LONG_PTR)b);        /* a owned by its own owned window */
    t_note("cycle attempt error %lu, a's owner now %p", GetLastError(), U(GetWindow)(a, GW_OWNER));
    CHECK(U(GetWindow)(a, GW_OWNER) != b);
    SetLastError(0);
    U(SetWindowLongPtrA)(a, GWLP_HWNDPARENT, (LONG_PTR)a);
    CHECK(U(GetWindow)(a, GW_OWNER) != a);
    CHECK(U(GetAncestor)(a, GA_ROOTOWNER) != NULL);                /* ROOTOWNER terminates */
    zo_reset();
}

TEST(win_position_zorder_hier_owner_invalid_handle) {
    NEED_GUI();
    HWND w = zo_top("owninv", ZO_OFFX, ZO_OFFY, 40, 40, 1);
    REQUIRE(w);
    SetLastError(0);
    U(SetWindowLongPtrA)(w, GWLP_HWNDPARENT, 0x1234);
    t_note("owner = bogus handle -> error %lu, owner %p", GetLastError(), U(GetWindow)(w, GW_OWNER));
    CHECK(U(GetWindow)(w, GW_OWNER) == NULL);
    zo_reset();
}

TEST(win_position_zorder_hier_owner_destroyed_with_owner) {
    NEED_GUI();
    HWND o = zo_top("ownd1", ZO_OFFX, ZO_OFFY, 80, 80, 1);
    REQUIRE(o);
    HWND p = zo_top_ex("ownd2", ZO_OFFX + 100, ZO_OFFY, 60, 60, WS_POPUP, 0, o, 1, NULL);
    REQUIRE(p);
    U(DestroyWindow)(o);
    CHECK(!U(IsWindow)(p));                              /* owned windows die with their owner */
    zo_reset();
}

TEST(win_position_zorder_hier_child_destroyed_with_parent_and_handles_invalid) {
    NEED_GUI();
    struct tree t; REQUIRE(mktree(&t));
    U(DestroyWindow)(t.T);
    CHECK(!U(IsWindow)(t.T)); CHECK(!U(IsWindow)(t.C1)); CHECK(!U(IsWindow)(t.C2));
    CHECK(!U(IsWindow)(t.P));  CHECK(!U(IsWindow)(t.PC));
    SetLastError(0);
    CHECK(U(GetWindow)(t.C2, GW_HWNDNEXT) == NULL);
    EXPECT_ERR(1400);
    zo_reset();
}
