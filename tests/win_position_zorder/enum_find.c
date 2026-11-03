/* NtUserBuildHwndList (the engine under EnumWindows / EnumChildWindows / EnumThreadWindows) and
 * NtUserFindWindowEx.
 *
 * NtUserBuildHwndList(hdesk, hwndNext, fEnumChildren, bRemoveImmersive, idThread, cHwndMax, phwnd,
 *                     pcHwndNeeded) -> NTSTATUS:
 *   - hwndNext != 0 must be a valid window (else STATUS_INVALID_HANDLE + 1400); the list starts AT
 *     that window and walks spwndNext; with fEnumChildren the walk starts at its first child and
 *     recurses into every child (pre-order) - that is EnumChildWindows;
 *   - idThread != 0 must belong to a GUI thread with a desktop, else STATUS_INVALID_HANDLE + 87;
 *     the list is then the top-level windows of that thread (EnumThreadWindows);
 *   - hwndNext == 0 starts at the thread desktop's first child (EnumWindows);
 *   - *pcHwndNeeded = count + 1 (a terminating 1 is stored after the handles) and the status is
 *     STATUS_BUFFER_TOO_SMALL when cHwndMax is smaller - user32!BuildHwndList retries with that size.
 * user32 then re-validates every handle just before calling the callback.
 */
#include "zo.h"

#define MAXL 8192
struct coll { HWND list[MAXL]; int n; int stop_after; int ret; void (*hook)(struct coll *, HWND); void *hookarg; };

static BOOL CALLBACK coll_cb(HWND h, LPARAM l) {
    struct coll *c = (struct coll *)l;
    if (c->n < MAXL) c->list[c->n++] = h;
    if (c->hook) c->hook(c, h);
    if (c->stop_after && c->n >= c->stop_after) return FALSE;
    return TRUE;
}
static void coll_init(struct coll *c) { memset(c, 0, sizeof *c); c->ret = 1; }

/* subset of 'all' that is in 'mine', in 'all' order */
static int subset(const HWND *all, int n, const HWND *mine, int m, HWND *out) {
    int k = 0;
    for (int i = 0; i < n; i++) if (zo_idx(mine, m, all[i]) >= 0 && k < m) out[k++] = all[i];
    return k;
}

static LONG bhl(HANDLE desk, HWND next, BOOL children, BOOL nonimm, DWORD tid, UINT cap, HWND *buf, UINT *need) {
    return UN(NtUserBuildHwndList)(desk, next, children, nonimm, tid, cap, buf, need);
}
/* full list via two-step protocol; returns count (without terminator) or -1 */
static int bhl_all(HWND next, BOOL children, DWORD tid, HWND *out, int cap) {
    UINT need = 0; HWND dummy[1];
    LONG st = bhl(NULL, next, children, TRUE, tid, 0, dummy, &need);
    if (st != STATUS_BUFFER_TOO_SMALL_ && st < 0) return -1;
    if (need == 0) return 0;
    if ((int)need > cap) return -1;
    st = bhl(NULL, next, children, TRUE, tid, need, out, &need);
    if (st < 0) return -1;
    return (int)need - 1;
}

/* ---- NtUserBuildHwndList ---- */

TEST(win_position_zorder_enum_bhl_buffer_protocol) {
    NEED_GUI(); NEED_NT(NtUserBuildHwndList);
    HWND w[3];
    for (int i = 0; i < 3; i++) { w[i] = zo_top("bhl", ZO_OFFX + i, ZO_OFFY, 30, 30, 1); REQUIRE(w[i]); }
    UINT need = 0; HWND dummy[1] = { (HWND)0x77 };
    LONG st = bhl(NULL, NULL, FALSE, TRUE, 0, 0, dummy, &need);
    EXPECT_EQ((ULONG)st, 0xC0000023u);                  /* STATUS_BUFFER_TOO_SMALL */
    CHECK(need >= 4);                                   /* at least our three windows + the terminator slot */
    CHECK(dummy[0] == (HWND)0x77);                      /* nothing was written */
    HWND *buf = (HWND *)calloc(need + 8, sizeof(HWND));
    UINT need2 = 0;
    st = bhl(NULL, NULL, FALSE, TRUE, 0, need, buf, &need2);
    CHECK(st >= 0);
    EXPECT_EQ(need2, need);
    EXPECT_EQ((ULONG_PTR)buf[need - 1], 1);             /* terminator qword */
    { UINT badent = 0;   /* one check for all entries: need is the desktop's window count, which varies */
      for (UINT i = 0; i + 1 < need; i++) if (buf[i] == NULL || buf[i] == (HWND)1) badent++;
      CHECKF(!badent, "%u list entries are null or the terminator value", badent); }
    for (int i = 0; i < 3; i++) CHECK(zo_idx(buf, need - 1, w[i]) >= 0);
    /* one slot short */
    UINT need3 = 0;
    st = bhl(NULL, NULL, FALSE, TRUE, 0, need - 1, buf, &need3);
    EXPECT_EQ((ULONG)st, 0xC0000023u);
    EXPECT_EQ(need3, need);
    free(buf);
    zo_reset();
}

TEST(win_position_zorder_enum_bhl_needed_counts_terminator) {
    NEED_GUI(); NEED_NT(NtUserBuildHwndList);
    /* a desktop whose only top-level windows are ours is not available, so compare against the user32 enumeration of the same moment */
    HWND w = zo_top("cnt", ZO_OFFX, ZO_OFFY, 30, 30, 1);
    REQUIRE(w);
    struct coll c; coll_init(&c);
    UINT need = 0; HWND d[1];
    LONG st = bhl(NULL, NULL, FALSE, TRUE, 0, 0, d, &need);
    CHECK((ULONG)st == 0xC0000023u);
    U(EnumWindows)(coll_cb, (LPARAM)&c);
    CHECKF((int)need - 1 >= c.n - 8 && (int)need - 1 <= c.n + 8, "BuildHwndList reports %u needed (incl. terminator), EnumWindows saw %d", need, c.n);   /* live desktop: allow a few windows of drift */
    zo_reset();
}

TEST(win_position_zorder_enum_bhl_toplevel_matches_enumwindows) {
    NEED_GUI(); NEED_NT(NtUserBuildHwndList);
    HWND w[4];
    for (int i = 0; i < 4; i++) { w[i] = zo_top("bm", ZO_OFFX + i, ZO_OFFY, 30, 30, i != 2); REQUIRE(w[i]); }
    HWND *all = (HWND *)calloc(MAXL, sizeof(HWND));
    int n = bhl_all(NULL, FALSE, 0, all, MAXL);
    REQUIRE(n > 0);
    struct coll c; coll_init(&c);
    U(EnumWindows)(coll_cb, (LPARAM)&c);
    HWND a[4], b[4], in[4] = { w[0], w[1], w[2], w[3] };
    int ka = subset(all, n, in, 4, a), kb = subset(c.list, c.n, in, 4, b);
    CHECK(ka == 4 && kb == 4 && zo_same(a, b, 4));
    free(all);
    zo_reset();
}

TEST(win_position_zorder_enum_bhl_order_is_zorder_including_hidden) {
    NEED_GUI(); NEED_NT(NtUserBuildHwndList);
    HWND w[4];
    for (int i = 0; i < 4; i++) { w[i] = zo_top("bz", ZO_OFFX + i, ZO_OFFY, 30, 30, i != 1); REQUIRE(w[i]); }
    HWND ord[4] = { w[2], w[0], w[3], w[1] };
    zo_arrange(ord, 4);
    HWND *all = (HWND *)calloc(MAXL, sizeof(HWND));
    int n = bhl_all(NULL, FALSE, 0, all, MAXL);
    REQUIRE(n > 0);
    HWND got[4];
    CHECK(subset(all, n, ord, 4, got) == 4 && zo_same(got, ord, 4));
    CHECK(zo_idx(all, n, w[1]) >= 0);                  /* the hidden window is listed too */
    CHECK(!U(IsWindowVisible)(w[1]));
    free(all);
    zo_reset();
}

TEST(win_position_zorder_enum_bhl_excludes_children) {
    NEED_GUI(); NEED_NT(NtUserBuildHwndList);
    HWND p = zo_top("bc", ZO_OFFX, ZO_OFFY, 100, 100, 1);
    REQUIRE(p);
    HWND c = zo_child(p, "bcc", 0, 0, 10, 10, WS_VISIBLE, 0);
    REQUIRE(c);
    HWND *all = (HWND *)calloc(MAXL, sizeof(HWND));
    int n = bhl_all(NULL, FALSE, 0, all, MAXL);
    REQUIRE(n > 0);
    CHECK(zo_idx(all, n, p) >= 0);
    CHECK(zo_idx(all, n, c) < 0);
    free(all);
    zo_reset();
}

TEST(win_position_zorder_enum_bhl_hwndnext_starts_at_that_window) {
    NEED_GUI(); NEED_NT(NtUserBuildHwndList);
    HWND w[3];
    for (int i = 0; i < 3; i++) { w[i] = zo_top("bn", ZO_OFFX + i, ZO_OFFY, 30, 30, 1); REQUIRE(w[i]); }
    zo_arrange(w, 3);
    HWND *l = (HWND *)calloc(MAXL, sizeof(HWND));
    int n = bhl_all(w[1], FALSE, 0, l, MAXL);
    REQUIRE(n >= 2);
    EXPECT_EQ(l[0], w[1]);                              /* the list begins with hwndNext itself ... */
    EXPECT_EQ(l[1], w[2]);                              /* ... and continues down the sibling chain */
    CHECK(zo_idx(l, n, w[0]) < 0);
    /* The list is the sibling chain from hwndNext onward, except that windows which have an owner are not interleaved
     * with their owners (Windows 10 puts them after all unowned windows). Compare the unowned windows in order, and the
     * whole chain as a set. */
    {
        HWND *ch = (HWND *)calloc(MAXL, sizeof(HWND));
        int nc = 0, lu = 0, cu = 0, same = 1;
        for (HWND cur = w[1]; cur && nc < MAXL; cur = U(GetWindow)(cur, GW_HWNDNEXT)) ch[nc++] = cur;
        CHECKF(nc == n, "chain length %d list length %d", nc, n);
        { int missing = 0, first = -1;   /* one check for the whole list: its length is the desktop's window count, which varies */
          for (int i = 0; i < n; i++) if (zo_idx(ch, nc, l[i]) < 0) { if (first < 0) first = i; missing++; }
          CHECKF(!missing, "%d list entries (first %d) are not in the sibling chain", missing, first); }
        HWND *ul = (HWND *)calloc(MAXL, sizeof(HWND)), *uc = (HWND *)calloc(MAXL, sizeof(HWND));
        for (int i = 0; i < n; i++) if (!U(GetWindow)(l[i], GW_OWNER)) ul[lu++] = l[i];
        for (int i = 0; i < nc; i++) if (!U(GetWindow)(ch[i], GW_OWNER)) uc[cu++] = ch[i];
        CHECKF(lu == cu, "unowned list %d chain %d", lu, cu);
        for (int i = 0; i < lu && i < cu; i++) if (ul[i] != uc[i]) { same = 0; t_note("first unowned mismatch at %d", i); break; }
        CHECK(same);
        free(ch); free(ul); free(uc);
    }
    free(l);
    zo_reset();
}

static int preorder(HWND parent, HWND *out, int cap, int n) {
    for (HWND c = U(GetWindow)(parent, GW_CHILD); c && n < cap; c = U(GetWindow)(c, GW_HWNDNEXT)) {
        out[n++] = c;
        n = preorder(c, out, cap, n);
    }
    return n;
}

static int mkdeep(HWND p, HWND *all) {
    /* p: A(a1(a11,a12),a2) B */
    HWND A = zo_child(p, "A", 0, 0, 100, 100, WS_VISIBLE, 0), B = zo_child(p, "B", 0, 0, 100, 100, WS_VISIBLE, 0);
    HWND a1 = zo_child(A, "a1", 0, 0, 50, 50, WS_VISIBLE, 0), a2 = zo_child(A, "a2", 0, 0, 50, 50, WS_VISIBLE, 0);
    HWND a11 = zo_child(a1, "a11", 0, 0, 20, 20, WS_VISIBLE, 0), a12 = zo_child(a1, "a12", 0, 0, 20, 20, 0, 0);
    if (!A || !B || !a1 || !a2 || !a11 || !a12) return 0;
    all[0] = A; all[1] = B; all[2] = a1; all[3] = a2; all[4] = a11; all[5] = a12;
    return 6;
}

TEST(win_position_zorder_enum_bhl_children_are_preorder_descendants) {
    NEED_GUI(); NEED_NT(NtUserBuildHwndList);
    HWND p = zo_top("bp", ZO_OFFX, ZO_OFFY, 300, 300, 1);
    REQUIRE(p);
    HWND all[6]; REQUIRE(mkdeep(p, all) == 6);
    HWND want[16]; int wn = preorder(p, want, 16, 0);
    CHECK(wn == 6);
    HWND got[32];
    int n = bhl_all(p, TRUE, 0, got, 32);
    CHECKF(n == wn, "got %d descendants, expected %d", n, wn);
    for (int i = 0; i < n && i < wn; i++) CHECKF(got[i] == want[i], "pre-order mismatch at %d", i);
    for (int i = 0; i < 6; i++) CHECK(zo_idx(got, n, all[i]) >= 0);       /* hidden descendants are listed as well */
    CHECK(zo_idx(got, n, p) < 0);                                           /* the parent itself is not part of its own child list */
    zo_reset();
}

TEST(win_position_zorder_enum_bhl_children_follow_zorder_changes) {
    NEED_GUI(); NEED_NT(NtUserBuildHwndList);
    HWND p = zo_top("bq", ZO_OFFX, ZO_OFFY, 300, 300, 1);
    REQUIRE(p);
    HWND all[6]; REQUIRE(mkdeep(p, all) == 6);
    U(SetWindowPos)(all[1], HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);     /* B before A */
    HWND want[16]; int wn = preorder(p, want, 16, 0);
    EXPECT_EQ(want[0], all[1]);
    HWND got[32];
    int n = bhl_all(p, TRUE, 0, got, 32);
    CHECK(n == wn);
    for (int i = 0; i < n && i < wn; i++) CHECK(got[i] == want[i]);
    zo_reset();
}

TEST(win_position_zorder_enum_bhl_children_of_leaf_is_empty) {
    NEED_GUI(); NEED_NT(NtUserBuildHwndList);
    HWND p = zo_top("bl", ZO_OFFX, ZO_OFFY, 100, 100, 1);
    REQUIRE(p);
    UINT need = 99; HWND d[2];
    LONG st = bhl(NULL, p, TRUE, TRUE, 0, 2, d, &need);
    CHECK(st >= 0);
    EXPECT_EQ(need, 1);                                 /* only the terminator */
    zo_reset();
}

TEST(win_position_zorder_enum_bhl_hwndnext_child_without_children_flag_stays_in_parent) {
    NEED_GUI(); NEED_NT(NtUserBuildHwndList);
    HWND p = zo_top("bs", ZO_OFFX, ZO_OFFY, 300, 300, 1);
    REQUIRE(p);
    HWND all[6]; REQUIRE(mkdeep(p, all) == 6);          /* p children: [A, B] */
    HWND got[32];
    int n = bhl_all(all[0], FALSE, 0, got, 32);
    EXPECT_EQ(n, 2);
    EXPECT_EQ(got[0], all[0]); EXPECT_EQ(got[1], all[1]);
    zo_reset();
}

TEST(win_position_zorder_enum_bhl_thread_filter) {
    NEED_GUI(); NEED_NT(NtUserBuildHwndList);
    HWND a = zo_top("bt1", ZO_OFFX, ZO_OFFY, 30, 30, 1), b = zo_top("bt2", ZO_OFFX + 40, ZO_OFFY, 30, 30, 0);
    REQUIRE(a && b);
    HWND c = zo_child(a, "btc", 0, 0, 5, 5, WS_VISIBLE, 0);
    HWND o = zo_top_ex("bto", ZO_OFFX + 80, ZO_OFFY, 30, 30, WS_POPUP, 0, a, 1, NULL);
    REQUIRE(c && o);
    HWND got[256];
    int n = bhl_all(NULL, FALSE, GetCurrentThreadId(), got, 256);
    REQUIRE(n > 0);
    for (int i = 0; i < n; i++) CHECKF(U(GetWindowThreadProcessId)(got[i], NULL) == GetCurrentThreadId(), "foreign window %p in thread list", got[i]);
    CHECK(zo_idx(got, n, a) >= 0 && zo_idx(got, n, b) >= 0 && zo_idx(got, n, o) >= 0);
    CHECK(zo_idx(got, n, c) < 0);                        /* top-level windows only */
    zo_reset();
}

static DWORD WINAPI idle_thread(LPVOID p) { WaitForSingleObject((HANDLE)p, 10000); return 0; }

TEST(win_position_zorder_enum_bhl_invalid_thread_ids) {
    NEED_GUI(); NEED_NT(NtUserBuildHwndList);
    HWND buf[4]; UINT need = 5;
    DWORD ids[] = { 1, 4, 0x7fffffff, 0xdeadbeef, 0xfffffff0 };
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        SetLastError(0); need = 5;
        LONG st = bhl(NULL, NULL, FALSE, TRUE, ids[i], 4, buf, &need);
        CHECKF((ULONG)st == 0xC0000008u, "tid %lu status 0x%lx", ids[i], (ULONG)st);   /* STATUS_INVALID_HANDLE */
        CHECKF(GetLastError() == 87, "tid %lu error %lu want 87", ids[i], GetLastError());
    }
    /* a thread that never touched win32k has no THREADINFO: PtiFromThreadId() fails the same way */
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    DWORD tid = 0;
    HANDLE th = CreateThread(NULL, 0, idle_thread, ev, 0, &tid);
    if (th) {
        Sleep(30);
        SetLastError(0);
        LONG st = bhl(NULL, NULL, FALSE, TRUE, tid, 4, buf, &need);
        CHECK((ULONG)st == 0xC0000008u);
        EXPECT_ERR(87);
        SetEvent(ev); WaitForSingleObject(th, 3000); CloseHandle(th);
    }
    CloseHandle(ev);
}

TEST(win_position_zorder_enum_bhl_invalid_hwndnext) {
    NEED_GUI(); NEED_NT(NtUserBuildHwndList);
    HWND buf[4]; UINT need = 5;
    HWND bad[] = { (HWND)1, (HWND)0x1234, (HWND)0xFFFF, (HWND)(ULONG_PTR)-1 };
    for (unsigned i = 0; i < 4; i++) {
        SetLastError(0);
        LONG st = bhl(NULL, bad[i], FALSE, TRUE, 0, 4, buf, &need);
        CHECKF((ULONG)st == 0xC0000008u, "hwnd %p status 0x%lx", bad[i], (ULONG)st);
        CHECKF(GetLastError() == 1400, "hwnd %p error %lu want 1400", bad[i], GetLastError());
    }
}

TEST(win_position_zorder_enum_bhl_invalid_desktop_handle) {
    NEED_GUI(); NEED_NT(NtUserBuildHwndList);
    HWND buf[4]; UINT need = 5;
    HANDLE bad[] = { (HANDLE)0x4, (HANDLE)0x12344, (HANDLE)0x7ffffff0 };
    for (unsigned i = 0; i < 3; i++) {
        LONG st = bhl(bad[i], NULL, FALSE, TRUE, 0, 4, buf, &need);
        CHECKF(st < 0, "desktop handle %p accepted (0x%lx)", bad[i], (ULONG)st);
    }
}

TEST(win_position_zorder_enum_bhl_explicit_thread_desktop) {
    NEED_GUI(); NEED_NT(NtUserBuildHwndList);
    REQUIRE(U(GetThreadDesktop));
    HDESK hd = U(GetThreadDesktop)(GetCurrentThreadId());
    REQUIRE(hd);
    HWND w = zo_top("bd", ZO_OFFX, ZO_OFFY, 30, 30, 1);
    REQUIRE(w);
    UINT need = 0; HWND d[1];
    LONG st = bhl(hd, NULL, FALSE, TRUE, 0, 0, d, &need);
    CHECK((ULONG)st == 0xC0000023u);
    HWND *buf = (HWND *)calloc(need + 4, sizeof(HWND));
    st = bhl(hd, NULL, FALSE, TRUE, 0, need, buf, &need);
    CHECK(st >= 0);
    CHECK(zo_idx(buf, need - 1, w) >= 0);                /* same desktop as the implicit one */
    free(buf);
    zo_reset();
}

TEST(win_position_zorder_enum_bhl_nonimmersive_flag_only_filters) {
    NEED_GUI(); NEED_NT(NtUserBuildHwndList);
    HWND w = zo_top("bi", ZO_OFFX, ZO_OFFY, 30, 30, 1);
    REQUIRE(w);
    HWND *a = (HWND *)calloc(MAXL, sizeof(HWND)), *b = (HWND *)calloc(MAXL, sizeof(HWND));
    UINT na = 0, nb = 0;
    LONG s1 = bhl(NULL, NULL, FALSE, FALSE, 0, 0, a, &na); (void)s1;
    HWND *ba = (HWND *)calloc(na + 4, sizeof(HWND));
    bhl(NULL, NULL, FALSE, FALSE, 0, na, ba, &na);
    LONG s2 = bhl(NULL, NULL, FALSE, TRUE, 0, 0, b, &nb); (void)s2;
    HWND *bb = (HWND *)calloc(nb + 4, sizeof(HWND));
    bhl(NULL, NULL, FALSE, TRUE, 0, nb, bb, &nb);
    CHECK(zo_idx(ba, na - 1, w) >= 0 && zo_idx(bb, nb - 1, w) >= 0);       /* default-band windows survive the filter */
    CHECKF(na + 8 >= nb, "unfiltered list (%u) shorter than filtered (%u)", na, nb);
    free(a); free(b); free(ba); free(bb);
    zo_reset();
}

TEST(win_position_zorder_enum_bhl_stub_number) {
    NEED_NT(NtUserBuildHwndList);
    FARPROC p = t_proc_in("win32u.dll", "NtUserBuildHwndList");
    REQUIRE(p);
    CHECK(k_stub_valid(p));
    if (!t_is_wine()) EXPECT_EQ(k_stub_number(p), 4124);
}

/* ---- EnumWindows ---- */

TEST(win_position_zorder_enum_windows_lists_visible_and_hidden_toplevels) {
    NEED_GUI();
    HWND w[3];
    w[0] = zo_top("ew1", ZO_OFFX, ZO_OFFY, 30, 30, 1); w[1] = zo_top("ew2", ZO_OFFX, ZO_OFFY, 30, 30, 0);
    w[2] = zo_top("ew3", ZO_OFFX, ZO_OFFY, 30, 30, 1);
    REQUIRE(w[0] && w[1] && w[2]);
    HWND c = zo_child(w[0], "ewc", 0, 0, 5, 5, WS_VISIBLE, 0);
    REQUIRE(c);
    struct coll *k = (struct coll *)malloc(sizeof *k); coll_init(k);
    CHECK(U(EnumWindows)(coll_cb, (LPARAM)k));
    for (int i = 0; i < 3; i++) CHECK(zo_idx(k->list, k->n, w[i]) >= 0);
    CHECK(zo_idx(k->list, k->n, c) < 0);
    HWND got[3], ord[3] = { w[2], w[1], w[0] };
    zo_arrange(ord, 3);
    k->n = 0;
    U(EnumWindows)(coll_cb, (LPARAM)k);
    CHECK(subset(k->list, k->n, ord, 3, got) == 3 && zo_same(got, ord, 3));
    free(k);
    zo_reset();
}

TEST(win_position_zorder_enum_windows_stop_when_callback_returns_false) {
    NEED_GUI();
    HWND w = zo_top("ews", ZO_OFFX, ZO_OFFY, 30, 30, 1);
    REQUIRE(w);
    struct coll *k = (struct coll *)malloc(sizeof *k); coll_init(k);
    k->stop_after = 1;
    BOOL r = U(EnumWindows)(coll_cb, (LPARAM)k);
    CHECK(!r);                          /* EnumWindowsWorker returns the callback's FALSE */
    EXPECT_EQ(k->n, 1);
    k->n = 0; k->stop_after = 0;
    CHECK(U(EnumWindows)(coll_cb, (LPARAM)k));
    CHECK(k->n >= 1);
    free(k);
    zo_reset();
}

static void destroy_hook(struct coll *c, HWND h) {
    HWND *victim = (HWND *)c->hookarg;
    if (victim[0] && h != victim[0] && !victim[1]) {       /* first callback for one of OUR windows: kill the other one */
        if (zo_idx(victim + 2, 2, h) >= 0) { U(DestroyWindow)(victim[0]); victim[1] = (HWND)1; }
    }
}

TEST(win_position_zorder_enum_windows_skips_window_destroyed_during_enumeration) {
    NEED_GUI();
    HWND a = zo_top("ed1", ZO_OFFX, ZO_OFFY, 30, 30, 1), b = zo_top("ed2", ZO_OFFX, ZO_OFFY, 30, 30, 1);
    REQUIRE(a && b);
    HWND ord[2] = { a, b };
    zo_arrange(ord, 2);                                     /* a first, b later in the snapshot */
    HWND ctl[4] = { b, NULL, a, b };                        /* victim, fired, watch-list... */
    struct coll *k = (struct coll *)malloc(sizeof *k); coll_init(k);
    k->hook = destroy_hook; k->hookarg = ctl;
    U(EnumWindows)(coll_cb, (LPARAM)k);
    CHECK(ctl[1] != NULL);                                  /* the hook ran */
    CHECK(zo_idx(k->list, k->n, a) >= 0);
    CHECK(zo_idx(k->list, k->n, b) < 0);                    /* user32 re-validates each handle (HMValidateHandle) before the call */
    free(k);
    zo_reset();
}

static void hide_hook(struct coll *c, HWND h) {
    HWND *ctl = (HWND *)c->hookarg;
    if (h == ctl[0]) U(ShowWindow)(ctl[1], SW_HIDE);
}
TEST(win_position_zorder_enum_windows_snapshot_keeps_window_hidden_during_enumeration) {
    NEED_GUI();
    HWND a = zo_top("eh1", ZO_OFFX, ZO_OFFY, 30, 30, 1), b = zo_top("eh2", ZO_OFFX, ZO_OFFY, 30, 30, 1);
    REQUIRE(a && b);
    HWND ord[2] = { a, b };
    zo_arrange(ord, 2);
    HWND ctl[2] = { a, b };
    struct coll *k = (struct coll *)malloc(sizeof *k); coll_init(k);
    k->hook = hide_hook; k->hookarg = ctl;
    U(EnumWindows)(coll_cb, (LPARAM)k);
    CHECK(zo_idx(k->list, k->n, b) >= 0);                   /* enumeration is of ALL top-level windows, visibility is irrelevant */
    CHECK(!U(IsWindowVisible)(b));
    free(k);
    zo_reset();
}

static void create_hook(struct coll *c, HWND h) {
    HWND *ctl = (HWND *)c->hookarg;
    if (!ctl[1]) ctl[1] = zo_top("ec_new", ZO_OFFX, ZO_OFFY, 20, 20, 1);
}
TEST(win_position_zorder_enum_windows_new_window_not_in_snapshot) {
    NEED_GUI();
    HWND a = zo_top("ec1", ZO_OFFX, ZO_OFFY, 30, 30, 1);
    REQUIRE(a);
    HWND ctl[2] = { a, NULL };
    struct coll *k = (struct coll *)malloc(sizeof *k); coll_init(k);
    k->hook = create_hook; k->hookarg = ctl;
    U(EnumWindows)(coll_cb, (LPARAM)k);
    CHECK(ctl[1] != NULL);
    CHECK(zo_idx(k->list, k->n, ctl[1]) < 0);              /* BuildHwndList took the snapshot before the first callback */
    k->n = 0; k->hook = NULL;
    U(EnumWindows)(coll_cb, (LPARAM)k);
    CHECK(zo_idx(k->list, k->n, ctl[1]) >= 0);
    free(k);
    zo_reset();
}

TEST(win_position_zorder_enum_windows_excludes_message_only_windows) {
    NEED_GUI();
    HWND m = U(CreateWindowExA)(0, zo_class(), "ewmsg", WS_POPUP, 0, 0, 10, 10, HWND_MESSAGE, NULL, zo_hinst(), NULL);
    if (!m) { t_skip("message-only window creation failed"); return; }
    HWND n = zo_top("ewnormal", ZO_OFFX, ZO_OFFY, 20, 20, 1);
    REQUIRE(n);
    struct coll *k = (struct coll *)malloc(sizeof *k); coll_init(k);
    U(EnumWindows)(coll_cb, (LPARAM)k);
    CHECK(zo_idx(k->list, k->n, m) < 0);
    CHECK(zo_idx(k->list, k->n, n) >= 0);
    /* but it is a descendant of the message-window root, so enumerating that root finds it */
    HWND root = U(GetAncestor)(m, GA_PARENT);
    CHECK(root != NULL && root != zo_desktop());
    k->n = 0;
    if (root) U(EnumChildWindows)(root, coll_cb, (LPARAM)k);
    CHECK(zo_idx(k->list, k->n, m) >= 0);
    /* and the thread enumeration (top-level windows of the thread under the thread's desktop window) leaves it out as well */
    k->n = 0;
    U(EnumThreadWindows)(GetCurrentThreadId(), coll_cb, (LPARAM)k);
    CHECK(zo_idx(k->list, k->n, m) < 0);
    CHECK(zo_idx(k->list, k->n, n) >= 0);
    free(k);
    U(DestroyWindow)(m);
    zo_reset();
}

/* ---- EnumChildWindows ---- */

TEST(win_position_zorder_enum_child_windows_preorder) {
    NEED_GUI();
    HWND p = zo_top("ecp", ZO_OFFX, ZO_OFFY, 300, 300, 1);
    REQUIRE(p);
    HWND all[6]; REQUIRE(mkdeep(p, all) == 6);
    HWND want[16]; int wn = preorder(p, want, 16, 0);
    struct coll *k = (struct coll *)malloc(sizeof *k); coll_init(k);
    CHECK(U(EnumChildWindows)(p, coll_cb, (LPARAM)k));
    CHECK(k->n == wn);
    for (int i = 0; i < k->n && i < wn; i++) CHECKF(k->list[i] == want[i], "pre-order mismatch at %d", i);
    free(k);
    zo_reset();
}

TEST(win_position_zorder_enum_child_windows_no_children_returns_false) {
    NEED_GUI();
    HWND p = zo_top("ecn", ZO_OFFX, ZO_OFFY, 30, 30, 1);
    REQUIRE(p);
    struct coll *k = (struct coll *)malloc(sizeof *k); coll_init(k);
    CHECK(!U(EnumChildWindows)(p, coll_cb, (LPARAM)k));      /* BuildHwndList returned 0 entries -> FALSE */
    EXPECT_EQ(k->n, 0);
    free(k);
    zo_reset();
}

TEST(win_position_zorder_enum_child_windows_invalid_parent) {
    NEED_GUI();
    struct coll *k = (struct coll *)malloc(sizeof *k); coll_init(k);
    HWND bad[] = { (HWND)1, (HWND)0x1234, (HWND)(ULONG_PTR)-1 };
    for (unsigned i = 0; i < 3; i++) {
        k->n = 0; SetLastError(0);
        CHECKF(!U(EnumChildWindows)(bad[i], coll_cb, (LPARAM)k), "parent %p", bad[i]);
        EXPECT_EQ(k->n, 0);
        EXPECT_ERR(1400);
    }
    free(k);
}

TEST(win_position_zorder_enum_child_windows_null_parent_is_toplevel_enumeration) {
    NEED_GUI();
    HWND w[3];
    for (int i = 0; i < 3; i++) { w[i] = zo_top("ecz", ZO_OFFX + i, ZO_OFFY, 30, 30, 1); REQUIRE(w[i]); }
    HWND c = zo_child(w[0], "eczc", 0, 0, 5, 5, WS_VISIBLE, 0);
    REQUIRE(c);
    struct coll *k = (struct coll *)malloc(sizeof *k); coll_init(k);
    U(EnumChildWindows)(NULL, coll_cb, (LPARAM)k);
    for (int i = 0; i < 3; i++) CHECK(zo_idx(k->list, k->n, w[i]) >= 0);
    CHECK(zo_idx(k->list, k->n, c) < 0);                     /* hwndNext == 0: only the desktop's direct children */
    free(k);
    zo_reset();
}

TEST(win_position_zorder_enum_child_windows_desktop_includes_descendants) {
    NEED_GUI();
    HWND p = zo_top("ecd", ZO_OFFX, ZO_OFFY, 100, 100, 1);
    REQUIRE(p);
    HWND c = zo_child(p, "ecdc", 0, 0, 5, 5, WS_VISIBLE, 0);
    REQUIRE(c);
    HWND gc = zo_child(c, "ecdg", 0, 0, 2, 2, WS_VISIBLE, 0);
    REQUIRE(gc);
    struct coll *k = (struct coll *)malloc(sizeof *k); coll_init(k);
    U(EnumChildWindows)(zo_desktop(), coll_cb, (LPARAM)k);
    int ip = zo_idx(k->list, k->n, p), ic = zo_idx(k->list, k->n, c), ig = zo_idx(k->list, k->n, gc);
    CHECK(ip >= 0 && ic >= 0 && ig >= 0);
    CHECK(ip < ic && ic < ig);                               /* parents precede their descendants */
    free(k);
    zo_reset();
}

TEST(win_position_zorder_enum_child_windows_stop_early) {
    NEED_GUI();
    HWND p = zo_top("ecs", ZO_OFFX, ZO_OFFY, 300, 300, 1);
    REQUIRE(p);
    HWND all[6]; REQUIRE(mkdeep(p, all) == 6);
    struct coll *k = (struct coll *)malloc(sizeof *k); coll_init(k);
    k->stop_after = 3;
    CHECK(!U(EnumChildWindows)(p, coll_cb, (LPARAM)k));
    EXPECT_EQ(k->n, 3);
    free(k);
    zo_reset();
}

TEST(win_position_zorder_enum_child_windows_only_own_subtree) {
    NEED_GUI();
    HWND p = zo_top("eco", ZO_OFFX, ZO_OFFY, 300, 300, 1), q = zo_top("eco2", ZO_OFFX + 400, ZO_OFFY, 100, 100, 1);
    REQUIRE(p && q);
    HWND all[6]; REQUIRE(mkdeep(p, all) == 6);
    HWND qc = zo_child(q, "qc", 0, 0, 5, 5, WS_VISIBLE, 0);
    REQUIRE(qc);
    struct coll *k = (struct coll *)malloc(sizeof *k); coll_init(k);
    U(EnumChildWindows)(p, coll_cb, (LPARAM)k);
    CHECK(zo_idx(k->list, k->n, qc) < 0);
    CHECK(k->n == 6);
    k->n = 0;
    U(EnumChildWindows)(all[0], coll_cb, (LPARAM)k);        /* A: a1, a11, a12, a2 */
    CHECK(k->n == 4);
    CHECK(k->list[0] == all[2] && k->list[1] == all[4] && k->list[2] == all[5] && k->list[3] == all[3]);
    free(k);
    zo_reset();
}

/* ---- EnumThreadWindows ---- */

TEST(win_position_zorder_enum_thread_windows_own_thread) {
    NEED_GUI();
    HWND a = zo_top("et1", ZO_OFFX, ZO_OFFY, 30, 30, 1), b = zo_top("et2", ZO_OFFX, ZO_OFFY, 30, 30, 0);
    REQUIRE(a && b);
    HWND c = zo_child(a, "etc", 0, 0, 5, 5, WS_VISIBLE, 0);
    HWND o = zo_top_ex("eto", ZO_OFFX, ZO_OFFY, 30, 30, WS_POPUP, 0, a, 1, NULL);
    REQUIRE(c && o);
    struct coll *k = (struct coll *)malloc(sizeof *k); coll_init(k);
    CHECK(U(EnumThreadWindows)(GetCurrentThreadId(), coll_cb, (LPARAM)k));
    CHECK(zo_idx(k->list, k->n, a) >= 0 && zo_idx(k->list, k->n, b) >= 0 && zo_idx(k->list, k->n, o) >= 0);
    CHECK(zo_idx(k->list, k->n, c) < 0);
    for (int i = 0; i < k->n; i++) CHECK(U(GetWindowThreadProcessId)(k->list[i], NULL) == GetCurrentThreadId());
    free(k);
    zo_reset();
}

TEST(win_position_zorder_enum_thread_windows_invalid_thread_returns_true_without_callbacks) {
    NEED_GUI();
    HWND w = zo_top("eti", ZO_OFFX, ZO_OFFY, 30, 30, 1);
    REQUIRE(w);
    struct coll *k = (struct coll *)malloc(sizeof *k); coll_init(k);
    SetLastError(0);
    BOOL r = U(EnumThreadWindows)(0xdeadbeef, coll_cb, (LPARAM)k);
    /* EnumWindowsWorker: BuildHwndList yields no list, idThread != 0 -> returns 1 */
    CHECK(r);
    EXPECT_EQ(k->n, 0);
    t_note("last error after EnumThreadWindows(bad tid) = %lu (NtUserBuildHwndList sets 87)", GetLastError());
    free(k);
    zo_reset();
}

TEST(win_position_zorder_enum_thread_windows_zero_tid_is_enumwindows) {
    NEED_GUI();
    HWND w = zo_top("etz", ZO_OFFX, ZO_OFFY, 30, 30, 1);
    REQUIRE(w);
    struct coll *k = (struct coll *)malloc(sizeof *k), *j = (struct coll *)malloc(sizeof *j); coll_init(k); coll_init(j);
    U(EnumThreadWindows)(0, coll_cb, (LPARAM)k);
    U(EnumWindows)(coll_cb, (LPARAM)j);
    CHECK(zo_idx(k->list, k->n, w) >= 0);
    CHECK(k->n >= j->n - 4 && k->n <= j->n + 4);
    free(k); free(j);
    zo_reset();
}

struct xt { HWND w; };
static ULONG_PTR xt_fn(void *p) { (void)p; return 1; }

TEST(win_position_zorder_enum_thread_windows_other_thread) {
    NEED_GUI();
    struct zo_thread *t = zo_thread_start();
    if (!t) { t_skip("could not start GUI thread"); return; }
    HWND w = zo_thread_window(t, "etx", ZO_OFFX, ZO_OFFY, 30, 30, 1);
    if (!w) { zo_thread_stop(t); t_skip("window on worker thread failed"); return; }
    HWND mine = zo_top("etx_mine", ZO_OFFX, ZO_OFFY, 30, 30, 1);
    REQUIRE(mine);
    struct coll *k = (struct coll *)malloc(sizeof *k); coll_init(k);
    CHECK(U(EnumThreadWindows)(t->tid, coll_cb, (LPARAM)k));
    CHECK(zo_idx(k->list, k->n, w) >= 0);
    CHECK(zo_idx(k->list, k->n, mine) < 0);
    for (int i = 0; i < k->n; i++) CHECK(U(GetWindowThreadProcessId)(k->list[i], NULL) == t->tid);   /* (a GUI thread may own further hidden IME windows) */
    free(k);
    zo_thread_call(t, xt_fn, NULL);
    zo_reset();
    zo_thread_stop(t);
}

TEST(win_position_zorder_enum_thread_windows_stop_early_returns_false) {
    NEED_GUI();
    HWND a = zo_top("etp1", ZO_OFFX, ZO_OFFY, 30, 30, 1), b = zo_top("etp2", ZO_OFFX, ZO_OFFY, 30, 30, 1);
    REQUIRE(a && b);
    struct coll *k = (struct coll *)malloc(sizeof *k); coll_init(k);
    k->stop_after = 1;
    CHECK(!U(EnumThreadWindows)(GetCurrentThreadId(), coll_cb, (LPARAM)k));
    EXPECT_EQ(k->n, 1);
    free(k);
    zo_reset();
}

/* ---- FindWindowEx ---- */

static HWND fw(HWND parent, HWND after, const wchar_t *cls, const wchar_t *title) { return U(FindWindowExW)(parent, after, cls, title); }
static wchar_t g_wcls[80], g_wcls2[80];
static const wchar_t *wcls(void) { if (!g_wcls[0]) MultiByteToWideChar(CP_ACP, 0, zo_class(), -1, g_wcls, 80); return g_wcls; }
static const wchar_t *wcls2(void) { if (!g_wcls2[0]) MultiByteToWideChar(CP_ACP, 0, zo_class2(), -1, g_wcls2, 80); return g_wcls2; }

TEST(win_position_zorder_find_by_class_returns_topmost_match) {
    NEED_GUI();
    HWND w[3];
    for (int i = 0; i < 3; i++) { w[i] = zo_top("fc", ZO_OFFX + i, ZO_OFFY, 30, 30, 1); REQUIRE(w[i]); }
    HWND ord[3] = { w[1], w[2], w[0] };
    zo_arrange(ord, 3);
    HWND r = fw(NULL, NULL, wcls(), NULL);
    EXPECT_EQ(r, w[1]);
    EXPECT_EQ(U(FindWindowW)(wcls(), NULL), w[1]);
    zo_reset();
}

TEST(win_position_zorder_find_iterates_in_zorder_with_childafter) {
    NEED_GUI();
    HWND w[3];
    for (int i = 0; i < 3; i++) { w[i] = zo_top("fi", ZO_OFFX + i, ZO_OFFY, 30, 30, i != 1); REQUIRE(w[i]); }
    HWND ord[3] = { w[2], w[0], w[1] };
    zo_arrange(ord, 3);
    HWND cur = NULL; int n = 0; HWND got[4] = {0};
    while ((cur = fw(NULL, cur, wcls(), NULL)) != NULL && n < 4) got[n++] = cur;
    EXPECT_EQ(n, 3);
    CHECK(zo_same(got, ord, 3));                                 /* hidden windows are included */
    zo_reset();
}

TEST(win_position_zorder_find_by_title) {
    NEED_GUI();
    char t1[64], t2[64]; wchar_t w1[64], w2[64];
    zo_unique_name(t1, sizeof t1, "title_a"); zo_unique_name(t2, sizeof t2, "title_b");
    MultiByteToWideChar(CP_ACP, 0, t1, -1, w1, 64); MultiByteToWideChar(CP_ACP, 0, t2, -1, w2, 64);
    HWND a = zo_top(t1, ZO_OFFX, ZO_OFFY, 30, 30, 1), b = zo_top(t2, ZO_OFFX, ZO_OFFY, 30, 30, 1);
    REQUIRE(a && b);
    EXPECT_EQ(fw(NULL, NULL, NULL, w1), a);
    EXPECT_EQ(fw(NULL, NULL, NULL, w2), b);
    EXPECT_EQ(fw(NULL, NULL, wcls(), w2), b);
    EXPECT_EQ(fw(NULL, NULL, wcls2(), w2), NULL);               /* class filter and title filter are ANDed */
    wchar_t nope[64]; MultiByteToWideChar(CP_ACP, 0, "win32kt_no_such_title_", -1, nope, 64);
    EXPECT_EQ(fw(NULL, NULL, NULL, nope), NULL);
    zo_reset();
}

TEST(win_position_zorder_find_title_must_match_whole_string) {
    NEED_GUI();
    char t1[64]; wchar_t w1[64];
    zo_unique_name(t1, sizeof t1, "whole");
    HWND a = zo_top(t1, ZO_OFFX, ZO_OFFY, 30, 30, 1);
    REQUIRE(a);
    MultiByteToWideChar(CP_ACP, 0, t1, -1, w1, 64);
    w1[wcslen(w1) - 1] = 0;                                      /* prefix */
    EXPECT_EQ(fw(NULL, NULL, NULL, w1), NULL);
    wcscat(w1, L"x");
    EXPECT_EQ(fw(NULL, NULL, NULL, w1), NULL);
    zo_reset();
}

TEST(win_position_zorder_find_by_class_picks_right_class) {
    NEED_GUI();
    HWND a = zo_top_ex("fcc1", ZO_OFFX, ZO_OFFY, 30, 30, WS_POPUP, 0, NULL, 1, zo_class());
    HWND b = zo_top_ex("fcc2", ZO_OFFX, ZO_OFFY, 30, 30, WS_POPUP, 0, NULL, 1, zo_class2());
    REQUIRE(a && b);
    EXPECT_EQ(fw(NULL, NULL, wcls(), NULL), a);
    EXPECT_EQ(fw(NULL, NULL, wcls2(), NULL), b);
    zo_reset();
}

TEST(win_position_zorder_find_by_class_atom) {
    NEED_GUI();
    HWND a = zo_top_ex("fat", ZO_OFFX, ZO_OFFY, 30, 30, WS_POPUP, 0, NULL, 1, zo_class());
    REQUIRE(a);
    ATOM at = zo_atom();
    REQUIRE(at);
    EXPECT_EQ(fw(NULL, NULL, (LPCWSTR)(ULONG_PTR)at, NULL), a);       /* user32 builds {Length 0, Buffer = atom} for the kernel */
    EXPECT_EQ(fw(NULL, NULL, (LPCWSTR)(ULONG_PTR)(at + 0x3000), NULL), NULL);
    zo_reset();
}

TEST(win_position_zorder_find_direct_stub_unicode_string_protocol) {
    NEED_GUI(); NEED_NT(NtUserFindWindowEx);
    HWND a = zo_top("fds", ZO_OFFX, ZO_OFFY, 30, 30, 1);
    REQUIRE(a);
    UNICODE_STRING cls, name;
    memset(&name, 0, sizeof name);
    cls.Buffer = (PWSTR)wcls(); cls.Length = (USHORT)(wcslen(wcls()) * 2); cls.MaximumLength = cls.Length + 2;
    EXPECT_EQ(UN(NtUserFindWindowEx)(NULL, NULL, &cls, &name, 0), a);
    /* class given as an atom: Length 0 and the atom in the Buffer field */
    UNICODE_STRING acls; memset(&acls, 0, sizeof acls); acls.Buffer = (PWSTR)(ULONG_PTR)zo_atom();
    EXPECT_EQ(UN(NtUserFindWindowEx)(NULL, NULL, &acls, &name, 0), a);
    /* empty class and empty name match any window: the first top-level window of the desktop */
    UNICODE_STRING none; memset(&none, 0, sizeof none);
    HWND any = UN(NtUserFindWindowEx)(NULL, NULL, &none, &none, 0);
    CHECK(any != NULL);
    t_note("empty class+name matched %p (top of z-order is %p)", any, U(GetTopWindow)(NULL));
    zo_reset();
}

TEST(win_position_zorder_find_hidden_window_is_found) {
    NEED_GUI();
    HWND a = zo_top("fhid", ZO_OFFX, ZO_OFFY, 30, 30, 0);
    REQUIRE(a);
    EXPECT_EQ(fw(NULL, NULL, wcls(), NULL), a);
    CHECK(!U(IsWindowVisible)(a));
    zo_reset();
}

TEST(win_position_zorder_find_children_only_direct) {
    NEED_GUI();
    HWND p = zo_top("fp", ZO_OFFX, ZO_OFFY, 200, 200, 1);
    REQUIRE(p);
    HWND c1 = zo_child(p, "c1", 0, 0, 50, 50, WS_VISIBLE, 0), c2 = zo_child(p, "c2", 0, 0, 50, 50, WS_VISIBLE, 0);
    HWND g = zo_child(c1, "g", 0, 0, 10, 10, WS_VISIBLE, 0);
    REQUIRE(c1 && c2 && g);
    HWND ord[2] = { c1, c2 };
    zo_arrange(ord, 2);
    HWND a = fw(p, NULL, wcls(), NULL), b = fw(p, a, wcls(), NULL), c = fw(p, b, wcls(), NULL);
    EXPECT_EQ(a, c1); EXPECT_EQ(b, c2); EXPECT_EQ(c, NULL);        /* the grandchild is not a direct child of p */
    EXPECT_EQ(fw(c1, NULL, wcls(), NULL), g);
    zo_reset();
}

TEST(win_position_zorder_find_desktop_handle_equals_null_parent) {
    NEED_GUI();
    HWND a = zo_top("fdp", ZO_OFFX, ZO_OFFY, 30, 30, 1);
    REQUIRE(a);
    EXPECT_EQ(fw(zo_desktop(), NULL, wcls(), NULL), fw(NULL, NULL, wcls(), NULL));
    EXPECT_EQ(fw(zo_desktop(), NULL, wcls(), NULL), a);
    zo_reset();
}

TEST(win_position_zorder_find_childafter_not_a_child_note) {
    NEED_GUI();
    HWND p = zo_top("fna", ZO_OFFX, ZO_OFFY, 100, 100, 1), q = zo_top("fnb", ZO_OFFX, ZO_OFFY, 100, 100, 1);
    REQUIRE(p && q);
    HWND c = zo_child(p, "fnc", 0, 0, 5, 5, WS_VISIBLE, 0), other = zo_child(q, "fno", 0, 0, 5, 5, WS_VISIBLE, 0);
    REQUIRE(c && other);
    SetLastError(0);
    HWND r = fw(p, other, wcls(), NULL);
    t_note("FindWindowEx(parent, child-of-another-parent) -> %p error %lu", r, GetLastError());
    zo_reset();
}

TEST(win_position_zorder_find_invalid_handles) {
    NEED_GUI(); NEED_NT(NtUserFindWindowEx);
    UNICODE_STRING none; memset(&none, 0, sizeof none);
    HWND bad[] = { (HWND)1, (HWND)0x1234, (HWND)0xFFFF };
    for (unsigned i = 0; i < 3; i++) {
        SetLastError(0);
        CHECKF(UN(NtUserFindWindowEx)(bad[i], NULL, &none, &none, 0) == NULL, "parent %p", bad[i]);
        CHECKF(GetLastError() == 1400, "parent %p error %lu want 1400", bad[i], GetLastError());
        SetLastError(0);
        CHECKF(UN(NtUserFindWindowEx)(NULL, bad[i], &none, &none, 0) == NULL, "childafter %p", bad[i]);
        CHECKF(GetLastError() == 1400, "childafter %p error %lu want 1400", bad[i], GetLastError());
    }
}

TEST(win_position_zorder_find_message_only_search) {
    NEED_GUI();
    HWND m = U(CreateWindowExA)(0, zo_class2(), "fmsg", WS_POPUP, 0, 0, 10, 10, HWND_MESSAGE, NULL, zo_hinst(), NULL);
    if (!m) { t_skip("message-only window creation failed"); return; }
    HWND n = zo_top_ex("fnormal", ZO_OFFX, ZO_OFFY, 30, 30, WS_POPUP, 0, NULL, 1, zo_class2());
    REQUIRE(n);
    /* a desktop search never sees message-only windows ... */
    HWND found = fw(NULL, NULL, wcls2(), NULL);
    EXPECT_EQ(found, n);
    /* ... HWND_MESSAGE as parent searches the message windows only */
    HWND mf = fw(HWND_MESSAGE, NULL, wcls2(), NULL);
    EXPECT_EQ(mf, m);
    EXPECT_EQ(fw(HWND_MESSAGE, m, wcls2(), NULL), NULL);
    U(DestroyWindow)(m);
    EXPECT_EQ(fw(HWND_MESSAGE, NULL, wcls2(), NULL), NULL);
    zo_reset();
}

TEST(win_position_zorder_find_after_destroyed_window_stops) {
    NEED_GUI();
    HWND a = zo_top("fad1", ZO_OFFX, ZO_OFFY, 30, 30, 1), b = zo_top("fad2", ZO_OFFX, ZO_OFFY, 30, 30, 1);
    REQUIRE(a && b);
    HWND ord[2] = { a, b };
    zo_arrange(ord, 2);
    HWND first = fw(NULL, NULL, wcls(), NULL);
    EXPECT_EQ(first, a);
    U(DestroyWindow)(a);
    SetLastError(0);
    HWND r = fw(NULL, a, wcls(), NULL);                         /* stale 'after' handle */
    CHECK(r == NULL);
    EXPECT_ERR(1400);
    zo_reset();
}

TEST(win_position_zorder_find_class_name_case_note) {
    NEED_GUI();
    HWND a = zo_top("fcase", ZO_OFFX, ZO_OFFY, 30, 30, 1);
    REQUIRE(a);
    wchar_t up[80]; wcscpy(up, wcls()); _wcsupr(up);
    HWND r = fw(NULL, NULL, up, NULL);
    t_note("FindWindowEx with the class name upper-cased -> %s", r == a ? "found (class names compare case-insensitively)" : "not found");
    zo_reset();
}

TEST(win_position_zorder_find_empty_title_note) {
    NEED_GUI();
    HWND a = zo_top("", ZO_OFFX, ZO_OFFY, 30, 30, 1);
    REQUIRE(a);
    HWND r = fw(NULL, NULL, wcls(), L"");
    t_note("FindWindowEx with title L\"\" -> %s", r == a ? "matches the untitled window" : (r ? "other window" : "not found"));
    EXPECT_EQ(fw(NULL, NULL, wcls(), NULL), a);
    zo_reset();
}

TEST(win_position_zorder_find_stub_number_and_user32_agree) {
    NEED_GUI(); NEED_NT(NtUserFindWindowEx);
    HWND a = zo_top("fag", ZO_OFFX, ZO_OFFY, 30, 30, 1);
    REQUIRE(a);
    FARPROC p = t_proc_in("win32u.dll", "NtUserFindWindowEx");
    if (!t_is_wine()) EXPECT_EQ(k_stub_number(p), 4204);
    UNICODE_STRING cls, name; memset(&name, 0, sizeof name);
    cls.Buffer = (PWSTR)wcls(); cls.Length = (USHORT)(wcslen(wcls()) * 2); cls.MaximumLength = cls.Length + 2;
    EXPECT_EQ(UN(NtUserFindWindowEx)(NULL, NULL, &cls, &name, 0), fw(NULL, NULL, wcls(), NULL));
    zo_reset();
}
