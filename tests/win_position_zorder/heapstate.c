/* Table-driven and state-machine tests that tie the kernel's z-order / show-state engine to what the
 * process can observe locally: the desktop-heap tagWND the kernel keeps current (style, rects, sibling
 * links) and the USER handle-table entry. Also raw SYSCALL entry (k_raw_syscall) for three syscalls.
 */
#include "zo.h"

/* ---- ShowWindow state machine ---- */

struct st { int vis, min, max; };
enum { ST_HIDDEN, ST_NORMAL, ST_MAX, ST_MIN, ST_COUNT };
static const char *sname[ST_COUNT] = { "hidden", "normal", "maximized", "minimized" };

static void park(HWND w) {
    WINDOWPLACEMENT wp; memset(&wp, 0, sizeof wp); wp.length = sizeof wp;
    U(GetWindowPlacement)(w, &wp);
    wp.flags |= WPF_SETMINPOSITION; wp.ptMinPosition.x = ZO_OFFX; wp.ptMinPosition.y = ZO_OFFY;
    wp.showCmd = U(IsWindowVisible)(w) ? SW_SHOWNA : SW_HIDE;
    if (U(IsIconic)(w)) wp.showCmd = SW_SHOWMINNOACTIVE;
    if (U(IsZoomed)(w)) wp.showCmd = SW_SHOWMAXIMIZED;
    U(SetWindowPlacement)(w, &wp);
}

static HWND in_state(int s) {
    HWND w = zo_top("state", ZO_OFFX + 10, ZO_OFFY + 10, 120, 90, s != ST_HIDDEN);
    if (!w) return NULL;
    park(w);
    if (s == ST_MAX) U(ShowWindow)(w, SW_SHOWMAXIMIZED);
    if (s == ST_MIN) U(ShowWindow)(w, SW_SHOWMINNOACTIVE);
    return w;
}
static struct st get_state(HWND w) { struct st s = { U(IsWindowVisible)(w) != 0, U(IsIconic)(w) != 0, U(IsZoomed)(w) != 0 }; return s; }

/* expected (vis,min,max) after ShowWindow(cmd) from a start state; -1 in min/max means "not asserted" */
struct trans { int start, cmd; struct st want; };
#define V(v, mi, ma) { v, mi, ma }
static const struct trans T[] = {
    /* from a hidden normal window */
    { ST_HIDDEN, SW_HIDE, V(0, 0, 0) }, { ST_HIDDEN, SW_SHOWNORMAL, V(1, 0, 0) }, { ST_HIDDEN, SW_SHOWMINIMIZED, V(1, 1, 0) },
    { ST_HIDDEN, SW_SHOWMAXIMIZED, V(1, 0, 1) }, { ST_HIDDEN, SW_SHOWNOACTIVATE, V(1, 0, 0) }, { ST_HIDDEN, SW_SHOW, V(1, 0, 0) },
    { ST_HIDDEN, SW_MINIMIZE, V(1, 1, 0) }, { ST_HIDDEN, SW_SHOWMINNOACTIVE, V(1, 1, 0) }, { ST_HIDDEN, SW_SHOWNA, V(1, 0, 0) },
    { ST_HIDDEN, SW_RESTORE, V(1, 0, 0) },
    /* from a visible normal window */
    { ST_NORMAL, SW_HIDE, V(0, 0, 0) }, { ST_NORMAL, SW_SHOWNORMAL, V(1, 0, 0) }, { ST_NORMAL, SW_SHOWMINIMIZED, V(1, 1, 0) },
    { ST_NORMAL, SW_SHOWMAXIMIZED, V(1, 0, 1) }, { ST_NORMAL, SW_SHOWNOACTIVATE, V(1, 0, 0) }, { ST_NORMAL, SW_SHOW, V(1, 0, 0) },
    { ST_NORMAL, SW_MINIMIZE, V(1, 1, 0) }, { ST_NORMAL, SW_SHOWMINNOACTIVE, V(1, 1, 0) }, { ST_NORMAL, SW_SHOWNA, V(1, 0, 0) },
    { ST_NORMAL, SW_RESTORE, V(1, 0, 0) },
    /* from a maximized window */
    { ST_MAX, SW_HIDE, V(0, 0, 1) }, { ST_MAX, SW_SHOWNORMAL, V(1, 0, 0) }, { ST_MAX, SW_SHOWMAXIMIZED, V(1, 0, 1) },
    { ST_MAX, SW_SHOWNOACTIVATE, V(1, 0, 0) }, { ST_MAX, SW_SHOW, V(1, 0, 1) }, { ST_MAX, SW_SHOWNA, V(1, 0, 1) },
    { ST_MAX, SW_RESTORE, V(1, 0, 0) },
    { ST_MAX, SW_SHOWMINIMIZED, V(1, 1, -1) }, { ST_MAX, SW_MINIMIZE, V(1, 1, -1) }, { ST_MAX, SW_SHOWMINNOACTIVE, V(1, 1, -1) },
    /* from a minimized window */
    { ST_MIN, SW_HIDE, V(0, -1, -1) }, { ST_MIN, SW_SHOWNORMAL, V(1, 0, 0) }, { ST_MIN, SW_SHOWMAXIMIZED, V(1, 0, 1) },
    { ST_MIN, SW_SHOWNOACTIVATE, V(1, 0, 0) }, { ST_MIN, SW_SHOW, V(1, 1, 0) }, { ST_MIN, SW_SHOWNA, V(1, 1, 0) },
    { ST_MIN, SW_RESTORE, V(1, 0, 0) },
    { ST_MIN, SW_SHOWMINIMIZED, V(1, 1, 0) }, { ST_MIN, SW_MINIMIZE, V(1, 1, 0) }, { ST_MIN, SW_SHOWMINNOACTIVE, V(1, 1, 0) },
};

TEST(win_position_zorder_state_showwindow_transition_table) {
    NEED_GUI(); NEED_NT(NtUserShowWindow);
    for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) {
        HWND w = in_state(T[i].start);
        REQUIRE(w);
        struct st before = get_state(w);
        BOOL prev = UN(NtUserShowWindow)(w, T[i].cmd);
        struct st a = get_state(w);
        CHECKF((prev != 0) == (before.vis != 0), "[%s] cmd %d: return %d but previous visibility was %d", sname[T[i].start], T[i].cmd, prev, before.vis);
        CHECKF(a.vis == T[i].want.vis, "[%s] cmd %d: visible=%d want %d", sname[T[i].start], T[i].cmd, a.vis, T[i].want.vis);
        if (T[i].want.min >= 0) CHECKF(a.min == T[i].want.min, "[%s] cmd %d: iconic=%d want %d", sname[T[i].start], T[i].cmd, a.min, T[i].want.min);
        if (T[i].want.max >= 0) CHECKF(a.max == T[i].want.max, "[%s] cmd %d: zoomed=%d want %d", sname[T[i].start], T[i].cmd, a.max, T[i].want.max);
        U(DestroyWindow)(w);
    }
    zo_reset();
}

TEST(win_position_zorder_state_showwindow_transition_table_heap_agrees) {
    NEED_GUI(); NEED_NT(NtUserShowWindow);
    if (t_is_wine() || !zo_heap_ok()) { t_skip("desktop heap windows not readable"); return; }
    for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) {
        HWND w = in_state(T[i].start);
        REQUIRE(w);
        UN(NtUserShowWindow)(w, T[i].cmd);
        int ok; unsigned long st = zo_style_heap(w, &ok);
        CHECK(ok);
        CHECKF(((st & WS_VISIBLE) != 0) == (U(GetWindowLongA)(w, GWL_STYLE) & WS_VISIBLE ? 1 : 0), "[%s] cmd %d visible bit", sname[T[i].start], T[i].cmd);
        CHECKF(((st & WS_MINIMIZE) != 0) == (U(IsIconic)(w) != 0), "[%s] cmd %d WS_MINIMIZE vs IsIconic", sname[T[i].start], T[i].cmd);
        CHECKF(((st & WS_MAXIMIZE) != 0) == (U(IsZoomed)(w) != 0), "[%s] cmd %d WS_MAXIMIZE vs IsZoomed", sname[T[i].start], T[i].cmd);
        RECT hw, api = zo_wr(w);
        CHECK(zo_rect_heap(w, ZO_WND_RcWnd, &hw) && zo_same_rect(&hw, &api));
        U(DestroyWindow)(w);
    }
    zo_reset();
}

TEST(win_position_zorder_state_restore_returns_to_original_rect_table) {
    NEED_GUI(); NEED_NT(NtUserShowWindow);
    static const int cmds[] = { SW_SHOWMAXIMIZED, SW_SHOWMINIMIZED, SW_MINIMIZE, SW_SHOWMINNOACTIVE };
    for (unsigned i = 0; i < sizeof cmds / sizeof cmds[0]; i++) {
        HWND w = in_state(ST_NORMAL);
        REQUIRE(w);
        RECT orig = zo_wr(w);
        UN(NtUserShowWindow)(w, cmds[i]);
        UN(NtUserShowWindow)(w, SW_RESTORE);
        RECT r = zo_wr(w);
        CHECKF(zo_same_rect(&orig, &r), "cmd %d then SW_RESTORE did not return the window to its rectangle", cmds[i]);
        U(DestroyWindow)(w);
    }
    zo_reset();
}

/* ---- exhaustive z-order model ---- */

TEST(win_position_zorder_state_zorder_model_exhaustive_three_windows) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND w[3];
    for (int i = 0; i < 3; i++) { w[i] = zo_top("model", ZO_OFFX + i, ZO_OFFY, 30, 30, 1); REQUIRE(w[i]); }
    static const int perms[6][3] = { {0,1,2}, {0,2,1}, {1,0,2}, {1,2,0}, {2,0,1}, {2,1,0} };
    HWND in[3] = { w[0], w[1], w[2] };
    int cases = 0, bad = 0;
    for (int p = 0; p < 6; p++) for (int t = 0; t < 3; t++) for (int op = 0; op < 4; op++) {
        int ord[3] = { perms[p][0], perms[p][1], perms[p][2] };
        HWND start[3] = { w[ord[0]], w[ord[1]], w[ord[2]] };
        zo_arrange(start, 3);
        int others[2], k = 0;
        for (int j = 0; j < 3; j++) if (j != t) others[k++] = j;
        /* model */
        int rest[2], rn = 0;
        for (int j = 0; j < 3; j++) if (ord[j] != t) rest[rn++] = ord[j];
        int model[3];
        HWND after;
        if (op == 0) { after = HWND_TOP; model[0] = t; model[1] = rest[0]; model[2] = rest[1]; }
        else if (op == 1) { after = HWND_BOTTOM; model[0] = rest[0]; model[1] = rest[1]; model[2] = t; }
        else {
            int x = others[op - 2];
            after = w[x];
            int m = 0;
            for (int j = 0; j < 2; j++) { model[m++] = rest[j]; if (rest[j] == x) model[m++] = t; }
        }
        BOOL r = UN(NtUserSetWindowPos)(w[t], after, 0, 0, 0, 0, ZO_SWP_ZONLY);
        HWND got[3], want[3] = { w[model[0]], w[model[1]], w[model[2]] };
        int ok = zo_rank(in, 3, got) == 3 && zo_same(got, want, 3) && r;
        cases++;
        if (!ok) { bad++; t_note("case perm=%d target=%d op=%d: wrong order (call returned %d)", p, t, op, r); }
        if (!t_is_wine() && zo_heap_ok()) {
            HWND h[3];
            if (zo_rank_heap(zo_desktop(), in, 3, h) != 3 || !zo_same(h, want, 3)) { bad++; t_note("case perm=%d target=%d op=%d: heap chain disagrees", p, t, op); }
        }
    }
    CHECKF(bad == 0, "%d of %d placements differ from the model", bad, cases);
    zo_reset();
}

TEST(win_position_zorder_state_zorder_model_children_exhaustive_four) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    HWND p = zo_top("modelp", ZO_OFFX, ZO_OFFY, 200, 200, 1);
    REQUIRE(p);
    HWND c[4];
    for (int i = 0; i < 4; i++) { c[i] = zo_child(p, "mc", i, i, 20, 20, WS_VISIBLE, 0); REQUIRE(c[i]); }
    HWND in[4] = { c[0], c[1], c[2], c[3] };
    int bad = 0, cases = 0;
    for (int t = 0; t < 4; t++) for (int x = -2; x < 4; x++) {
        if (x == t) continue;
        HWND start[4] = { c[0], c[1], c[2], c[3] };
        zo_arrange(start, 4);                                  /* c0 top ... c3 bottom */
        int model[4], m = 0;
        int rest[3], rn = 0;
        for (int j = 0; j < 4; j++) if (j != t) rest[rn++] = j;
        HWND after;
        if (x == -2) { after = HWND_TOP; model[m++] = t; for (int j = 0; j < 3; j++) model[m++] = rest[j]; }
        else if (x == -1) { after = HWND_BOTTOM; for (int j = 0; j < 3; j++) model[m++] = rest[j]; model[m++] = t; }
        else { after = c[x]; for (int j = 0; j < 3; j++) { model[m++] = rest[j]; if (rest[j] == x) model[m++] = t; } }
        UN(NtUserSetWindowPos)(c[t], after, 0, 0, 0, 0, ZO_SWP_ZONLY);
        HWND got[4], want[4];
        for (int j = 0; j < 4; j++) want[j] = c[model[j]];
        cases++;
        if (zo_rank(in, 4, got) != 4 || !zo_same(got, want, 4)) { bad++; t_note("child case target=%d after=%d wrong", t, x); }
        if (U(GetWindow)(p, GW_CHILD) != want[0] || U(GetWindow)(want[0], GW_HWNDLAST) != want[3]) { bad++; t_note("child case target=%d after=%d first/last wrong", t, x); }
    }
    CHECKF(bad == 0, "%d of %d child placements differ from the model", bad, cases);
    zo_reset();
}

/* ---- desktop-heap object invariants ---- */

TEST(win_position_zorder_state_handle_entry_describes_window) {
    NEED_GUI();
    if (t_is_wine()) { t_skip("no USER handle table under Wine"); return; }
    HWND w = zo_top("entry", ZO_OFFX, ZO_OFFY, 30, 30, 1);
    REQUIRE(w);
    struct k_user_entry e;
    if (!k_user_entry(w, &e)) { t_skip("USER handle entry unavailable"); return; }
    EXPECT_EQ(e.type, 1);                                         /* window */
    EXPECT_EQ(e.flags & 1, 0);                                    /* not freed/destroyed (HMValidateHandle tests bit 0) */
    EXPECT_EQ(e.uniq, (unsigned short)((ULONG_PTR)w >> 16));      /* handle HIWORD == entry uniq */
    CHECK(((ULONG_PTR)w & 0xffff) != 0);
    U(DestroyWindow)(w);
    CHECK(k_user_entry(w, &e));
    CHECK(e.type != 1 || (e.flags & 1));                          /* no longer a live window */
    zo_reset();
}

TEST(win_position_zorder_state_getwndptr_callone_param) {
    NEED_GUI(); NEED_NT(NtUserCallOneParam);
    if (t_is_wine()) { t_skip("Wine's NtUserCallOneParam uses a different function table"); return; }
    HWND a = zo_top("wp1", ZO_OFFX, ZO_OFFY, 30, 30, 1), b = zo_top("wp2", ZO_OFFX, ZO_OFFY, 30, 30, 1);
    REQUIRE(a && b);
    ULONG_PTR pa = UN(NtUserCallOneParam)((ULONG_PTR)a, 0x38), pb = UN(NtUserCallOneParam)((ULONG_PTR)b, 0x38);
    CHECK(pa != 0 && pb != 0 && pa != pb);
    ULONG_PTR ha = 0, hb = 0;
    CHECK(k_try_read((void *)pa, &ha, 8)); CHECK(k_try_read((void *)pb, &hb, 8));
    EXPECT_EQ(ha, (ULONG_PTR)a); EXPECT_EQ(hb, (ULONG_PTR)b);   /* head.h is the first qword of the client-side window object */
    EXPECT_EQ(UN(NtUserCallOneParam)((ULONG_PTR)a, 0x38), pa);   /* stable */
    SetLastError(0);
    CHECK(UN(NtUserCallOneParam)(0x1234, 0x38) == 0);
    void *k = k_wnd_ptr(a);
    if (k) EXPECT_EQ((ULONG_PTR)k, pa);                          /* the fast path used by user32 and the kernel call agree */
    zo_reset();
}

TEST(win_position_zorder_state_object_address_stable_across_geometry_changes) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    if (t_is_wine() || !zo_heap_ok()) { t_skip("desktop heap windows not readable"); return; }
    HWND w = zo_top("stable", ZO_OFFX, ZO_OFFY, 40, 40, 1);
    REQUIRE(w);
    const unsigned char *p0 = zo_wnd(w);
    REQUIRE(p0);
    UN(NtUserSetWindowPos)(w, NULL, ZO_OFFX + 9, ZO_OFFY + 9, 77, 55, SWP_NOZORDER | SWP_NOACTIVATE);
    UN(NtUserSetWindowPos)(w, HWND_TOPMOST, 0, 0, 0, 0, ZO_SWP_ZONLY);
    U(ShowWindow)(w, SW_HIDE); U(ShowWindow)(w, SW_SHOWNA);
    UN(NtUserSetWindowPos)(w, HWND_NOTOPMOST, 0, 0, 0, 0, ZO_SWP_ZONLY);
    EXPECT_EQ((ULONG_PTR)zo_wnd(w), (ULONG_PTR)p0);
    zo_reset();
}

TEST(win_position_zorder_state_desktop_window_fnid_and_rect) {
    NEED_GUI();
    HWND d = zo_desktop();
    REQUIRE(d);
    RECT r; CHECK(U(GetWindowRect)(d, &r));
    /* user32!GetWindowRect substitutes the primary monitor rectangle for a window with fnid 0x29d */
    MONITORINFO mi; memset(&mi, 0, sizeof mi); mi.cbSize = sizeof mi;
    POINT o = { 0, 0 };
    HMONITOR m = U(MonitorFromPoint)(o, MONITOR_DEFAULTTOPRIMARY);
    if (m && U(GetMonitorInfoA)(m, &mi)) { CHECK(zo_same_rect(&r, &mi.rcMonitor)); }
    if (!t_is_wine() && zo_heap_ok()) {
        const unsigned char *p = zo_wnd(d);
        unsigned short fnid = 0;
        CHECK(p && zo_rd(p, ZO_WND_Fnid, &fnid, 2));
        EXPECT_EQ(fnid & 0x2fff, 0x29d);
        HWND other = zo_top("notdesk", ZO_OFFX, ZO_OFFY, 20, 20, 1);
        if (other) { unsigned short f2 = 0; zo_rd(zo_wnd(other), ZO_WND_Fnid, &f2, 2); CHECK((f2 & 0x2fff) != 0x29d); }
        CHECK(zo_link(d, ZO_WND_Parent) == NULL);
        /* the heap head may be a window of a band the public APIs skip: the public top window must be on the heap chain */
        {
            HWND top = U(GetTopWindow)(NULL); int found = 0, guard = 0;
            for (HWND c = zo_link(d, ZO_WND_Child); c && guard++ < 20000; c = zo_link(c, ZO_WND_Next)) if (c == top) { found = 1; break; }
            CHECK(found);
        }
    }
    zo_reset();
}

TEST(win_position_zorder_state_topmost_bit_in_heap_exstyle) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    if (t_is_wine() || !zo_heap_ok()) { t_skip("desktop heap windows not readable"); return; }
    HWND w = zo_top("tmheap", ZO_OFFX, ZO_OFFY, 20, 20, 1);
    REQUIRE(w);
    int ok; unsigned long ex = zo_exstyle_heap(w, &ok);
    CHECK(ok && !(ex & WS_EX_TOPMOST));
    UN(NtUserSetWindowPos)(w, HWND_TOPMOST, 0, 0, 0, 0, ZO_SWP_ZONLY);
    ex = zo_exstyle_heap(w, &ok); CHECK(ok && (ex & WS_EX_TOPMOST));
    CHECK((U(GetWindowLongA)(w, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0);
    UN(NtUserSetWindowPos)(w, HWND_NOTOPMOST, 0, 0, 0, 0, ZO_SWP_ZONLY);
    ex = zo_exstyle_heap(w, &ok); CHECK(ok && !(ex & WS_EX_TOPMOST));
    zo_reset();
}

TEST(win_position_zorder_state_topmost_windows_precede_nontopmost_in_heap_chain) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    if (t_is_wine() || !zo_heap_ok()) { t_skip("desktop heap windows not readable"); return; }
    HWND a = zo_top("tc1", ZO_OFFX, ZO_OFFY, 20, 20, 1), b = zo_top("tc2", ZO_OFFX, ZO_OFFY, 20, 20, 1);
    REQUIRE(a && b);
    UN(NtUserSetWindowPos)(a, HWND_TOPMOST, 0, 0, 0, 0, ZO_SWP_ZONLY);
    /* walk the whole sibling chain through the heap: once a non-topmost window has been seen, no topmost window may follow */
    HWND cur = zo_link(zo_desktop(), ZO_WND_Child); int seen_non = 0, guard = 0, violations = 0;
    while (cur && guard++ < 5000) {
        int ok; unsigned long ex = zo_exstyle_heap(cur, &ok);
        if (ok) { if (ex & WS_EX_TOPMOST) { if (seen_non) violations++; } else seen_non = 1; }
        cur = zo_link(cur, ZO_WND_Next);
    }
    EXPECT_EQ(violations, 0);
    UN(NtUserSetWindowPos)(a, HWND_NOTOPMOST, 0, 0, 0, 0, ZO_SWP_ZONLY);
    zo_reset();
}

TEST(win_position_zorder_state_child_client_rect_in_heap_is_screen_space) {
    NEED_GUI();
    if (t_is_wine() || !zo_heap_ok()) { t_skip("desktop heap windows not readable"); return; }
    HWND p = zo_top_ex("crp", ZO_OFFX + 20, ZO_OFFY + 30, 200, 150, WS_POPUP | WS_BORDER, 0, NULL, 1, NULL);
    REQUIRE(p);
    HWND c = zo_child(p, "crc", 7, 8, 40, 30, WS_VISIBLE | WS_BORDER, 0);
    REQUIRE(c);
    RECT hc, hp, pc = zo_cr_screen(p), cc = zo_cr_screen(c), cw = zo_wr(c);
    CHECK(zo_rect_heap(p, ZO_WND_RcCli, &hp) && zo_same_rect(&hp, &pc));
    CHECK(zo_rect_heap(c, ZO_WND_RcCli, &hc) && zo_same_rect(&hc, &cc));
    EXPECT_EQ(cw.left, pc.left + 7); EXPECT_EQ(cw.top, pc.top + 8);
    CHECK(cc.left > cw.left && cc.right < cw.right);               /* the child's own border */
    zo_reset();
}

TEST(win_position_zorder_state_destroy_unlinks_from_chain) {
    NEED_GUI();
    HWND w[3];
    for (int i = 0; i < 3; i++) { w[i] = zo_top("unl", ZO_OFFX + i, ZO_OFFY, 20, 20, 1); REQUIRE(w[i]); }
    zo_arrange(w, 3);
    U(DestroyWindow)(w[1]);
    CHECK(U(GetWindow)(w[0], GW_HWNDNEXT) == w[2]);
    CHECK(U(GetWindow)(w[2], GW_HWNDPREV) == w[0]);
    if (!t_is_wine() && zo_heap_ok()) {
        CHECK(zo_link(w[0], ZO_WND_Next) == w[2]);
        CHECK(zo_link(w[2], ZO_WND_Prev) == w[0]);
    }
    zo_reset();
}

TEST(win_position_zorder_state_hidden_window_keeps_zorder_slot) {
    NEED_GUI();
    HWND w[3];
    for (int i = 0; i < 3; i++) { w[i] = zo_top("hz", ZO_OFFX + i, ZO_OFFY, 20, 20, 1); REQUIRE(w[i]); }
    zo_arrange(w, 3);
    zo_hide(w[1]);
    HWND got[3];
    CHECK(zo_rank(w, 3, got) == 3 && zo_same(got, w, 3));
    CHECK(U(GetWindow)(w[0], GW_HWNDNEXT) == w[1]);              /* GetWindow does not skip hidden windows */
    zo_show(w[1]);
    CHECK(zo_rank(w, 3, got) == 3 && zo_same(got, w, 3));
    zo_reset();
}

TEST(win_position_zorder_state_show_does_not_reorder) {
    NEED_GUI();
    HWND w[3];
    for (int i = 0; i < 3; i++) { w[i] = zo_top("sr", ZO_OFFX + i, ZO_OFFY, 20, 20, 1); REQUIRE(w[i]); }
    zo_arrange(w, 3);
    zo_hide(w[0]);
    zo_show(w[0]);                                               /* SWP_SHOWWINDOW|SWP_NOZORDER */
    HWND got[3];
    CHECK(zo_rank(w, 3, got) == 3 && zo_same(got, w, 3));
    U(ShowWindow)(w[2], SW_HIDE); U(ShowWindow)(w[2], SW_SHOWNA);
    /* ShowWindow passes hwndInsertAfter == 0 (HWND_TOP) and only adds SWP_NOZORDER on some paths: a hidden top-level
     * window that is shown through ShowWindow may come back at the front */
    CHECK(zo_rank(w, 3, got) == 3);
    t_note("after ShowWindow(SW_HIDE); ShowWindow(SW_SHOWNA) the bottom window is now %s", got[0] == w[2] ? "at the top" : (got[2] == w[2] ? "still at the bottom" : "in the middle"));
    zo_reset();
}

/* ---- raw SYSCALL entry ---- */

TEST(win_position_zorder_raw_getancestor_syscall) {
    NEED_GUI(); NEED_NT(NtUserGetAncestor);
    if (t_is_wine()) { t_skip("raw SYSCALL instruction is not meaningful under Wine"); return; }
    FARPROC st = t_proc_in("win32u.dll", "NtUserGetAncestor");
    REQUIRE(st && k_stub_valid(st));
    long n = k_stub_number(st);
    HWND p = zo_top("rawp", ZO_OFFX, ZO_OFFY, 40, 40, 1);
    REQUIRE(p);
    HWND c = zo_child(p, "rawc", 0, 0, 10, 10, WS_VISIBLE, 0);
    REQUIRE(c);
    K_NTSTATUS r = k_raw_syscall((unsigned long)n, (ULONG_PTR)c, GA_PARENT, 0, 0, 0, 0, 0, 0);
    EXPECT_EQ((ULONG_PTR)r, (ULONG_PTR)p);
    r = k_raw_syscall((unsigned long)n, (ULONG_PTR)c, GA_ROOT, 0, 0, 0, 0, 0, 0);
    EXPECT_EQ((ULONG_PTR)r, (ULONG_PTR)p);
    SetLastError(0);
    r = k_raw_syscall((unsigned long)n, (ULONG_PTR)c, 9, 0, 0, 0, 0, 0, 0);
    EXPECT_EQ((ULONG_PTR)r, 0);
    EXPECT_ERR(87);
    zo_reset();
}

TEST(win_position_zorder_raw_showwindow_syscall) {
    NEED_GUI(); NEED_NT(NtUserShowWindow);
    if (t_is_wine()) { t_skip("raw SYSCALL instruction is not meaningful under Wine"); return; }
    FARPROC st = t_proc_in("win32u.dll", "NtUserShowWindow");
    REQUIRE(st && k_stub_valid(st));
    long n = k_stub_number(st);
    HWND w = zo_top("rawsw", ZO_OFFX, ZO_OFFY, 40, 40, 0);
    REQUIRE(w);
    K_NTSTATUS r = k_raw_syscall((unsigned long)n, (ULONG_PTR)w, SW_SHOWNA, 0, 0, 0, 0, 0, 0);
    EXPECT_EQ((unsigned)r, 0);
    CHECK(U(IsWindowVisible)(w));
    r = k_raw_syscall((unsigned long)n, (ULONG_PTR)w, SW_HIDE, 0, 0, 0, 0, 0, 0);
    CHECK((unsigned)r != 0);
    CHECK(!U(IsWindowVisible)(w));
    SetLastError(0);
    r = k_raw_syscall((unsigned long)n, (ULONG_PTR)w, 12, 0, 0, 0, 0, 0, 0);
    EXPECT_EQ((unsigned)r, 0);
    EXPECT_ERR(87);
    zo_reset();
}

TEST(win_position_zorder_raw_setwindowpos_syscall) {
    NEED_GUI(); NEED_NT(NtUserSetWindowPos);
    if (t_is_wine()) { t_skip("raw SYSCALL instruction is not meaningful under Wine"); return; }
    FARPROC st = t_proc_in("win32u.dll", "NtUserSetWindowPos");
    REQUIRE(st && k_stub_valid(st));
    long n = k_stub_number(st);
    HWND w = zo_top("rawswp", ZO_OFFX, ZO_OFFY, 40, 40, 1);
    REQUIRE(w);
    K_NTSTATUS r = k_raw_syscall((unsigned long)n, (ULONG_PTR)w, 0, ZO_OFFX + 5, ZO_OFFY + 6, 33, 34, SWP_NOZORDER | SWP_NOACTIVATE, 0);
    EXPECT_EQ((unsigned)r, 1);
    RECT q = zo_wr(w);
    EXPECT_EQ(q.left, ZO_OFFX + 5); EXPECT_EQ(q.right - q.left, 33);
    SetLastError(0);
    r = k_raw_syscall((unsigned long)n, (ULONG_PTR)w, 0, 0, 0, 0, 0, 0x800 | ZO_SWP_NOTHING, 0);
    EXPECT_EQ((unsigned)r, 0);
    EXPECT_ERR(1004);
    zo_reset();
}
