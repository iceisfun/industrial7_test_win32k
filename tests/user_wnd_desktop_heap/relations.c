/* user_wnd_desktop_heap: the window-to-window links stored in the structure (parent, child,
 * owner, next, prev, last active popup) and the rebasing user32 applies to them
 * (client = this + (kernel pointer - *(this+8))), against GetParent / GetWindow / GetAncestor. */
#define WDH_TAG L"r"
#include "wdh.h"

static bool wv_follow(const struct wv *cur, unsigned off, struct wv *nx) {
    memset(nx, 0, sizeof *nx);
    uint64_t k = W64(cur, off);
    if (!k) return false;
    uint64_t a = wv_fix(cur, k);
    if (!wv_read_at(a, nx->b, WV_SNAP)) return false;
    nx->pc = a;
    nx->delta = cur->delta;
    nx->hwnd = (HWND)(ULONG_PTR)(uint32_t)W64(nx, W_HWND);
    return true;
}
/* hwnds along `linkoff` starting at (and including) `first` */
static int raw_chain(const struct wv *first, unsigned linkoff, HWND *out, int max) {
    struct wv cur = *first, nx;
    int n = 0;
    while (n < max) {
        out[n++] = cur.hwnd;
        if (!wv_follow(&cur, linkoff, &nx)) break;
        cur = nx;
    }
    return n;
}
static bool chain_is(const HWND *got, int n, const HWND *want, int m) {
    if (n != m) return false;
    for (int i = 0; i < n; i++) if (got[i] != want[i]) return false;
    return true;
}
/* children of `parent` top to bottom according to the structure */
static int children_raw(HWND parent, HWND *out, int max) {
    struct wv p, f;
    if (!wv_get(parent, &p)) return -1;
    if (!wv_follow(&p, W_CHILD, &f)) return 0;
    return raw_chain(&f, W_NEXT, out, max);
}
static int children_api(HWND parent, HWND *out, int max) {
    int n = 0;
    for (HWND c = U(GetWindow)(parent, GW_CHILD); c && n < max; c = U(GetWindow)(c, GW_HWNDNEXT)) out[n++] = c;
    return n;
}
struct enumctx { HWND *out; int n, max; };
static BOOL CALLBACK enum_cb(HWND h, LPARAM lp) {
    struct enumctx *c = (struct enumctx *)lp;
    if (c->n < c->max) c->out[c->n++] = h;
    return TRUE;
}
static HWND link_hwnd(HWND h, unsigned off) {
    struct wv w;
    return wv_get(h, &w) ? wv_link(&w, off) : (HWND)(ULONG_PTR)-1;
}

/* Z-order helpers for top-level windows. Windows 10 creates a default IME window (and a text-services window) for the
 * first window a thread makes; they are owned by that window and sit directly above it in the z-order, so adjacency
 * checks between test windows have to look past windows owned by one of them. */
static bool owned_by_any(HWND n, const HWND *own, int cnt) {
    HWND o = U(GetWindow)(n, GW_OWNER);
    for (int i = 0; o && i < cnt; i++) if (own[i] == o) return true;
    return false;
}
static HWND next_skip_owned(HWND h, const HWND *own, int cnt) {
    HWND n = U(GetWindow)(h, GW_HWNDNEXT);
    while (n && owned_by_any(n, own, cnt)) n = U(GetWindow)(n, GW_HWNDNEXT);
    return n;
}
static HWND prev_skip_owned(HWND h, const HWND *own, int cnt) {
    HWND n = U(GetWindow)(h, GW_HWNDPREV);
    while (n && owned_by_any(n, own, cnt)) n = U(GetWindow)(n, GW_HWNDPREV);
    return n;
}
/* ---- top-level windows -------------------------------------------------------------------------- */

TEST(user_wnd_desktop_heap_rel_toplevel_parent_is_desktop) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    EXPECT_EQ((ULONG_PTR)wv_link(&w, W_PARENT), (ULONG_PTR)U(GetDesktopWindow)());
    EXPECT_EQ(W64(&w, W_OWNER), 0);
    EXPECT_EQ(W64(&w, W_CHILD), 0);
    CHECK(U(GetParent)(h) == NULL);                 /* popup without owner: GetParent reads +0x40 == 0 */
    CHECK(U(GetAncestor)(h, GA_PARENT) == U(GetDesktopWindow)());
    CHECK(U(GetWindow)(h, GW_OWNER) == NULL);
    CHECK(U(GetWindow)(h, GW_CHILD) == NULL);
    END();
}
TEST(user_wnd_desktop_heap_rel_toplevel_parent_pointer_is_desktop_window_object) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    struct wv d;
    CHECK(wv_get(U(GetDesktopWindow)(), &d));
    EXPECT_EQ(wv_fix(&w, W64(&w, W_PARENT)), d.pc);
    EXPECT_EQ(W64(&w, W_PARENT), d.e.phead);   /* kernel pointers in the structure are phead values */
    END();
}
TEST(user_wnd_desktop_heap_rel_toplevel_in_desktop_child_list) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    HWND d = U(GetDesktopWindow)();
    struct wv dw, c;
    CHECK(wv_get(d, &dw));
    CHECK(W64(&dw, W_CHILD) != 0);
    int found = 0, n = 0;
    if (wv_follow(&dw, W_CHILD, &c)) {
        static HWND chain[16384];
        n = raw_chain(&c, W_NEXT, chain, 16384);
        for (int i = 0; i < n; i++) if (chain[i] == h) found = 1;
    }
    CHECKF(found, "window not in the %d-entry top-level list", n);
    END();
}
TEST(user_wnd_desktop_heap_rel_toplevel_newer_is_above_older) {
    T_BEGIN();
    HWND a = mk_plain();
    HWND b = mk_plain();
    NEED_WND(a, wa);
    NEED_WND(b, wb);
    /* a new non-topmost window goes to the top of its band: walking down from b reaches a */
    int found = 0, steps = 0;
    for (HWND c = U(GetWindow)(b, GW_HWNDNEXT); c && steps < 5000; c = U(GetWindow)(c, GW_HWNDNEXT), steps++)
        if (c == a) { found = 1; break; }
    CHECK(found);
    found = 0; steps = 0;
    for (HWND c = U(GetWindow)(a, GW_HWNDPREV); c && steps < 5000; c = U(GetWindow)(c, GW_HWNDPREV), steps++)
        if (c == b) { found = 1; break; }
    CHECK(found);
    END();
}
static int has_owned_window(HWND owner) {
    int steps = 0;
    for (HWND c = U(GetWindow)(U(GetDesktopWindow)(), GW_CHILD); c && steps < 5000; c = U(GetWindow)(c, GW_HWNDNEXT), steps++)
        if (U(GetWindow)(c, GW_OWNER) == owner) return 1;
    return 0;
}
TEST(user_wnd_desktop_heap_rel_toplevel_setwindowpos_top_reorders) {
    T_BEGIN();
    HWND a = mk_plain();
    HWND b = mk_plain();
    NEED_WND(a, wa);
    CHECK(U(SetWindowPos)(a, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE));
    int found = 0, steps = 0;
    for (HWND c = U(GetWindow)(a, GW_HWNDNEXT); c && steps < 5000; c = U(GetWindow)(c, GW_HWNDNEXT), steps++)
        if (c == b) { found = 1; break; }
    if (!found && has_owned_window(a)) {
        /* Windows 10: the call succeeds but a window that owns the thread's default IME window stays below a newer
         * sibling, with its IME window between the two */
        wk_reap();
        t_skip("HWND_TOP does not move a window that owns a default IME window");
        return;
    }
    CHECK(found);
    END();
}
TEST(user_wnd_desktop_heap_rel_toplevel_insert_after) {
    T_BEGIN();
    HWND a = mk_plain();
    HWND b = mk_plain();
    HWND c = mk_plain();
    NEED_WND(a, wa);
    /* SWP_NOACTIVATE insert-after puts a directly below c (apart from windows owned by the test windows) */
    CHECK(U(SetWindowPos)(a, c, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE));
    HWND own[3] = { a, b, c };
    EXPECT_EQ((ULONG_PTR)next_skip_owned(c, own, 3), (ULONG_PTR)a);
    EXPECT_EQ((ULONG_PTR)prev_skip_owned(a, own, 3), (ULONG_PTR)c);
    /* the stored links agree: following next from c reaches a before any other window that is not owned by a test window */
    struct wv cur;
    CHECK(wv_get(c, &cur));
    HWND hit = NULL;
    for (int i = 0; i < 8 && !hit; i++) {
        struct wv nx;
        if (!wv_follow(&cur, W_NEXT, &nx)) break;
        if (!owned_by_any(nx.hwnd, own, 3)) hit = nx.hwnd;
        cur = nx;
    }
    EXPECT_EQ((ULONG_PTR)hit, (ULONG_PTR)a);
    (void)b;
    END();
}

/* ---- owner ----------------------------------------------------------------------------------------- */

TEST(user_wnd_desktop_heap_rel_owner_link) {
    T_BEGIN();
    HWND o = mk_plain();
    HWND p = mk_top(plain_class(), WS_POPUP, 0, -30000, -30000, 20, 20, o);
    NEED_WND(p, w);
    EXPECT_EQ((ULONG_PTR)wv_link(&w, W_OWNER), (ULONG_PTR)o);
    EXPECT_EQ((ULONG_PTR)U(GetWindow)(p, GW_OWNER), (ULONG_PTR)o);
    /* GetParent for a popup returns the owner (bits 31/30 of the style select +0x40 or +0x30) */
    EXPECT_EQ((ULONG_PTR)U(GetParent)(p), (ULONG_PTR)o);
    END();
}
TEST(user_wnd_desktop_heap_rel_owner_parent_stays_desktop) {
    T_BEGIN();
    HWND o = mk_plain();
    HWND p = mk_top(plain_class(), WS_POPUP, 0, -30000, -30000, 20, 20, o);
    NEED_WND(p, w);
    EXPECT_EQ((ULONG_PTR)wv_link(&w, W_PARENT), (ULONG_PTR)U(GetDesktopWindow)());
    EXPECT_EQ((ULONG_PTR)U(GetAncestor)(p, GA_PARENT), (ULONG_PTR)U(GetDesktopWindow)());
    EXPECT_EQ((ULONG_PTR)U(GetAncestor)(p, GA_ROOT), (ULONG_PTR)p);
    EXPECT_EQ((ULONG_PTR)U(GetAncestor)(p, GA_ROOTOWNER), (ULONG_PTR)o);
    END();
}
TEST(user_wnd_desktop_heap_rel_owner_chain_roots) {
    T_BEGIN();
    HWND o1 = mk_plain();
    HWND o2 = mk_top(plain_class(), WS_POPUP, 0, -30000, -30000, 20, 20, o1);
    HWND o3 = mk_top(plain_class(), WS_POPUP, 0, -30000, -30000, 20, 20, o2);
    NEED_WND(o3, w);
    EXPECT_EQ((ULONG_PTR)wv_link(&w, W_OWNER), (ULONG_PTR)o2);
    EXPECT_EQ((ULONG_PTR)U(GetAncestor)(o3, GA_ROOTOWNER), (ULONG_PTR)o1);
    EXPECT_EQ((ULONG_PTR)link_hwnd(o2, W_OWNER), (ULONG_PTR)o1);
    EXPECT_EQ((ULONG_PTR)link_hwnd(o1, W_OWNER), 0);
    END();
}
TEST(user_wnd_desktop_heap_rel_owned_above_owner) {
    T_BEGIN();
    HWND o = mk_plain();
    HWND p = mk_top(plain_class(), WS_POPUP, 0, -30000, -30000, 20, 20, o);
    NEED_WND(p, w);
    /* owned windows are kept above their owner: walking down from the popup meets the owner */
    int found = 0, steps = 0;
    for (HWND c = U(GetWindow)(p, GW_HWNDNEXT); c && steps < 5000; c = U(GetWindow)(c, GW_HWNDNEXT), steps++)
        if (c == o) { found = 1; break; }
    CHECK(found);
    END();
}
TEST(user_wnd_desktop_heap_rel_set_owner_via_gwlp_hwndparent) {
    T_BEGIN();
    HWND o = mk_plain();
    HWND h = mk_plain();
    NEED_WND(h, w);
    EXPECT_EQ((ULONG_PTR)U(GetWindowLongPtrW)(h, GWLP_HWNDPARENT), 0);   /* desktop parent, no owner -> 0 */
    U(SetWindowLongPtrW)(h, GWLP_HWNDPARENT, (LONG_PTR)o);
    CHECK(wv_get(h, &w));
    EXPECT_EQ((ULONG_PTR)wv_link(&w, W_OWNER), (ULONG_PTR)o);
    EXPECT_EQ((ULONG_PTR)U(GetWindow)(h, GW_OWNER), (ULONG_PTR)o);
    /* GetWindowData(-8): parent is the desktop -> the owner's handle */
    EXPECT_EQ((ULONG_PTR)U(GetWindowLongPtrW)(h, GWLP_HWNDPARENT), (ULONG_PTR)o);
    EXPECT_EQ((ULONG_PTR)wv_link(&w, W_PARENT), (ULONG_PTR)U(GetDesktopWindow)());
    U(SetWindowLongPtrW)(h, GWLP_HWNDPARENT, 0);
    CHECK(wv_get(h, &w));
    EXPECT_EQ(W64(&w, W_OWNER), 0);
    END();
}
TEST(user_wnd_desktop_heap_rel_owner_destroyed_with_owner) {
    T_BEGIN();
    HWND o = mk_plain();
    HWND p = mk_top(plain_class(), WS_POPUP, 0, -30000, -30000, 20, 20, o);
    NEED_WND(p, w);
    CHECK(U(DestroyWindow)(o));
    CHECK(!U(IsWindow)(o));
    CHECK(!U(IsWindow)(p));   /* owned windows are destroyed with their owner */
    struct wv w2;
    CHECK(!wv_get(p, &w2));
    END();
}
TEST(user_wnd_desktop_heap_rel_lastactive_defaults_to_self) {
    T_BEGIN();
    HWND o = mk_plain();
    NEED_WND(o, w);
    EXPECT_EQ((ULONG_PTR)U(GetLastActivePopup)(o), (ULONG_PTR)o);
    uint64_t k = W64(&w, W_LASTACT);
    HWND la = wv_link(&w, W_LASTACT);
    t_info("spwndLastActive kernel pointer 0x%llx -> %p", (unsigned long long)k, la);
    CHECK(la == NULL || la == o);
    END();
}

/* ---- children ----------------------------------------------------------------------------------------- */

TEST(user_wnd_desktop_heap_rel_child_parent_link) {
    T_BEGIN();
    HWND p = mk_host();
    HWND c = mk_child(p, plain_class(), WS_CHILD, 1, 1, 10, 10, 7);
    NEED_WND(c, w);
    EXPECT_EQ((ULONG_PTR)wv_link(&w, W_PARENT), (ULONG_PTR)p);
    EXPECT_EQ(W64(&w, W_OWNER), 0);
    EXPECT_EQ((ULONG_PTR)U(GetParent)(c), (ULONG_PTR)p);
    EXPECT_EQ((ULONG_PTR)U(GetAncestor)(c, GA_PARENT), (ULONG_PTR)p);
    EXPECT_EQ((ULONG_PTR)U(GetAncestor)(c, GA_ROOT), (ULONG_PTR)p);
    EXPECT_EQ((ULONG_PTR)U(GetAncestor)(c, GA_ROOTOWNER), (ULONG_PTR)p);
    CHECK(U(IsChild)(p, c));
    CHECK(!U(IsChild)(c, p));
    END();
}
TEST(user_wnd_desktop_heap_rel_child_parent_pointer_rebased) {
    T_BEGIN();
    HWND p = mk_host();
    HWND c = mk_child(p, plain_class(), WS_CHILD, 1, 1, 10, 10, 7);
    NEED_WND(c, w);
    struct wv wp;
    CHECK(wv_get(p, &wp));
    /* the kernel pointer is the parent's phead; rebasing it with (this - *(this+8)) gives the parent's client pointer */
    EXPECT_EQ(W64(&w, W_PARENT), wp.e.phead);
    EXPECT_EQ(wv_fix(&w, W64(&w, W_PARENT)), wp.pc);
    END();
}
TEST(user_wnd_desktop_heap_rel_parent_child_link_single) {
    T_BEGIN();
    HWND p = mk_host();
    NEED_WND(p, wp);
    EXPECT_EQ(W64(&wp, W_CHILD), 0);
    HWND c = mk_child(p, plain_class(), WS_CHILD, 1, 1, 10, 10, 7);
    CHECK(wv_get(p, &wp));
    EXPECT_EQ((ULONG_PTR)wv_link(&wp, W_CHILD), (ULONG_PTR)c);
    EXPECT_EQ((ULONG_PTR)U(GetWindow)(p, GW_CHILD), (ULONG_PTR)c);
    EXPECT_EQ((ULONG_PTR)U(GetTopWindow)(p), (ULONG_PTR)c);
    struct wv wc;
    CHECK(wv_get(c, &wc));
    EXPECT_EQ(W64(&wc, W_NEXT), 0);
    EXPECT_EQ(W64(&wc, W_PREV), 0);
    END();
}
TEST(user_wnd_desktop_heap_rel_siblings_creation_order) {
    T_BEGIN();
    HWND p = mk_host();
    HWND c1 = mk_child(p, plain_class(), WS_CHILD, 0, 0, 10, 10, 1);
    HWND c2 = mk_child(p, plain_class(), WS_CHILD, 0, 0, 10, 10, 2);
    HWND c3 = mk_child(p, plain_class(), WS_CHILD, 0, 0, 10, 10, 3);
    NEED_WND(p, wp);
    HWND got[8], want[3] = { c1, c2, c3 };
    int n = children_raw(p, got, 8);
    /* a new child is inserted below its siblings: the first child created stays on top */
    CHECK(chain_is(got, n, want, 3));
    EXPECT_EQ((ULONG_PTR)U(GetWindow)(p, GW_CHILD), (ULONG_PTR)c1);
    EXPECT_EQ((ULONG_PTR)U(GetWindow)(c3, GW_HWNDFIRST), (ULONG_PTR)c1);
    EXPECT_EQ((ULONG_PTR)U(GetWindow)(c1, GW_HWNDLAST), (ULONG_PTR)c3);
    EXPECT_EQ((ULONG_PTR)U(GetWindow)(c1, GW_HWNDNEXT), (ULONG_PTR)c2);
    EXPECT_EQ((ULONG_PTR)U(GetWindow)(c3, GW_HWNDPREV), (ULONG_PTR)c2);
    CHECK(U(GetWindow)(c1, GW_HWNDPREV) == NULL);
    CHECK(U(GetWindow)(c3, GW_HWNDNEXT) == NULL);
    END();
}
TEST(user_wnd_desktop_heap_rel_siblings_prev_matches_next) {
    T_BEGIN();
    HWND p = mk_host();
    HWND c[5];
    for (int i = 0; i < 5; i++) c[i] = mk_child(p, plain_class(), WS_CHILD, 0, 0, 10, 10, i + 1);
    NEED_WND(p, wp);
    HWND got[8];
    int n = children_raw(p, got, 8);
    EXPECT_EQ(n, 5);
    for (int i = 0; i + 1 < n; i++) {
        /* doubly linked: the successor's prev link points back */
        EXPECT_EQ((ULONG_PTR)link_hwnd(got[i + 1], W_PREV), (ULONG_PTR)got[i]);
        EXPECT_EQ((ULONG_PTR)link_hwnd(got[i], W_NEXT), (ULONG_PTR)got[i + 1]);
    }
    EXPECT_EQ((ULONG_PTR)link_hwnd(got[0], W_PREV), 0);
    EXPECT_EQ((ULONG_PTR)link_hwnd(got[n - 1], W_NEXT), 0);
    END();
}
TEST(user_wnd_desktop_heap_rel_siblings_api_walk_matches_raw) {
    T_BEGIN();
    HWND p = mk_host();
    for (int i = 0; i < 6; i++) mk_child(p, plain_class(), WS_CHILD, 0, 0, 10, 10, i + 1);
    NEED_WND(p, wp);
    HWND a[16], r[16];
    int na = children_api(p, a, 16), nr = children_raw(p, r, 16);
    CHECK(chain_is(a, na, r, nr));
    EXPECT_EQ(nr, 6);
    END();
}
TEST(user_wnd_desktop_heap_rel_siblings_enumchildwindows_order) {
    T_BEGIN();
    HWND p = mk_host();
    for (int i = 0; i < 4; i++) mk_child(p, plain_class(), WS_CHILD, 0, 0, 10, 10, i + 1);
    NEED_WND(p, wp);
    HWND e[16], r[16];
    struct enumctx ctx = { e, 0, 16 };
    CHECK(U(EnumChildWindows)(p, enum_cb, (LPARAM)&ctx));
    int nr = children_raw(p, r, 16);
    CHECK(chain_is(e, ctx.n, r, nr));
    END();
}
static void reorder_case(int which) {
    T_BEGIN();
    HWND p = mk_host();
    HWND c1 = mk_child(p, plain_class(), WS_CHILD, 0, 0, 10, 10, 1);
    HWND c2 = mk_child(p, plain_class(), WS_CHILD, 0, 0, 10, 10, 2);
    HWND c3 = mk_child(p, plain_class(), WS_CHILD, 0, 0, 10, 10, 3);   /* top to bottom: c1 c2 c3 */
    NEED_WND(p, wp);
    HWND got[8], want[3];
    int n;
    UINT f = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE;
    switch (which) {
    case 0: CHECK(U(SetWindowPos)(c3, HWND_TOP, 0, 0, 0, 0, f));    want[0] = c3; want[1] = c1; want[2] = c2; break;
    case 1: CHECK(U(SetWindowPos)(c1, HWND_BOTTOM, 0, 0, 0, 0, f)); want[0] = c2; want[1] = c3; want[2] = c1; break;
    case 2: CHECK(U(SetWindowPos)(c3, c1, 0, 0, 0, 0, f));          want[0] = c1; want[1] = c3; want[2] = c2; break;
    case 3: CHECK(U(SetWindowPos)(c1, c2, 0, 0, 0, 0, f));          want[0] = c2; want[1] = c1; want[2] = c3; break;
    case 4: CHECK(U(SetWindowPos)(c2, HWND_TOP, 0, 0, 0, 0, f));    want[0] = c2; want[1] = c1; want[2] = c3; break;
    default: CHECK(U(SetWindowPos)(c2, HWND_BOTTOM, 0, 0, 0, 0, f)); want[0] = c1; want[1] = c3; want[2] = c2; break;
    }
    n = children_raw(p, got, 8);
    CHECKF(chain_is(got, n, want, 3), "case %d: got %d entries", which, n);
    HWND api[8];
    int na = children_api(p, api, 8);
    CHECK(chain_is(api, na, got, n));
    EXPECT_EQ((ULONG_PTR)link_hwnd(want[1], W_PREV), (ULONG_PTR)want[0]);
    EXPECT_EQ((ULONG_PTR)link_hwnd(want[2], W_PREV), (ULONG_PTR)want[1]);
    END();
}
#define RO(n) TEST(user_wnd_desktop_heap_rel_reorder_##n) { reorder_case(n); }
RO(0) RO(1) RO(2) RO(3) RO(4) RO(5)

TEST(user_wnd_desktop_heap_rel_destroy_middle_child_relinks) {
    T_BEGIN();
    HWND p = mk_host();
    HWND c1 = mk_child(p, plain_class(), WS_CHILD, 0, 0, 10, 10, 1);
    HWND c2 = mk_child(p, plain_class(), WS_CHILD, 0, 0, 10, 10, 2);
    HWND c3 = mk_child(p, plain_class(), WS_CHILD, 0, 0, 10, 10, 3);
    NEED_WND(p, wp);
    CHECK(U(DestroyWindow)(c2));
    HWND got[8], want[2] = { c1, c3 };
    int n = children_raw(p, got, 8);
    CHECK(chain_is(got, n, want, 2));
    EXPECT_EQ((ULONG_PTR)link_hwnd(c3, W_PREV), (ULONG_PTR)c1);
    EXPECT_EQ((ULONG_PTR)link_hwnd(c1, W_NEXT), (ULONG_PTR)c3);
    END();
}
TEST(user_wnd_desktop_heap_rel_destroy_top_child_updates_parent) {
    T_BEGIN();
    HWND p = mk_host();
    HWND c1 = mk_child(p, plain_class(), WS_CHILD, 0, 0, 10, 10, 1);
    HWND c2 = mk_child(p, plain_class(), WS_CHILD, 0, 0, 10, 10, 2);
    NEED_WND(p, wp);
    CHECK(U(DestroyWindow)(c2));
    CHECK(wv_get(p, &wp));
    EXPECT_EQ((ULONG_PTR)wv_link(&wp, W_CHILD), (ULONG_PTR)c1);
    CHECK(U(DestroyWindow)(c1));
    CHECK(wv_get(p, &wp));
    EXPECT_EQ(W64(&wp, W_CHILD), 0);
    END();
}
TEST(user_wnd_desktop_heap_rel_destroy_parent_destroys_children) {
    T_BEGIN();
    HWND p = mk_host();
    HWND c = mk_child(p, plain_class(), WS_CHILD, 0, 0, 10, 10, 1);
    HWND g = mk_child(c, plain_class(), WS_CHILD, 0, 0, 5, 5, 2);
    NEED_WND(g, wg);
    CHECK(U(DestroyWindow)(p));
    CHECK(!U(IsWindow)(p));
    CHECK(!U(IsWindow)(c));
    CHECK(!U(IsWindow)(g));
    struct wv w2;
    CHECK(!wv_get(g, &w2));
    END();
}
TEST(user_wnd_desktop_heap_rel_setparent_moves_links) {
    T_BEGIN();
    HWND p1 = mk_host();
    HWND p2 = mk_host();
    HWND c = mk_child(p1, plain_class(), WS_CHILD, 0, 0, 10, 10, 1);
    NEED_WND(c, wc);
    HWND prev = U(SetParent)(c, p2);
    EXPECT_EQ((ULONG_PTR)prev, (ULONG_PTR)p1);   /* returns the previous parent */
    CHECK(wv_get(c, &wc));
    EXPECT_EQ((ULONG_PTR)wv_link(&wc, W_PARENT), (ULONG_PTR)p2);
    EXPECT_EQ((ULONG_PTR)U(GetParent)(c), (ULONG_PTR)p2);
    HWND got[4];
    EXPECT_EQ(children_raw(p1, got, 4), 0);
    EXPECT_EQ(children_raw(p2, got, 4), 1);
    EXPECT_EQ((ULONG_PTR)got[0], (ULONG_PTR)c);
    END();
}
TEST(user_wnd_desktop_heap_rel_setparent_desktop_child) {
    T_BEGIN();
    HWND p = mk_host();
    HWND c = mk_child(p, plain_class(), WS_CHILD, 0, 0, 10, 10, 1);
    NEED_WND(c, wc);
    CHECK(U(SetParent)(c, NULL) != NULL);
    CHECK(wv_get(c, &wc));
    /* SetParent(NULL) reparents to the desktop; the window keeps WS_CHILD */
    EXPECT_EQ((ULONG_PTR)wv_link(&wc, W_PARENT), (ULONG_PTR)U(GetDesktopWindow)());
    CHECK((wv_style(&wc) & WS_CHILD) != 0);
    EXPECT_EQ((ULONG_PTR)U(GetParent)(c), (ULONG_PTR)U(GetDesktopWindow)());
    END();
}
TEST(user_wnd_desktop_heap_rel_gwlp_hwndparent_of_child_is_parent) {
    T_BEGIN();
    HWND p = mk_host();
    HWND c = mk_child(p, plain_class(), WS_CHILD, 0, 0, 10, 10, 1);
    NEED_WND(c, wc);
    EXPECT_EQ((ULONG_PTR)U(GetWindowLongPtrW)(c, GWLP_HWNDPARENT), (ULONG_PTR)p);
    END();
}
TEST(user_wnd_desktop_heap_rel_grandchild_roots) {
    T_BEGIN();
    HWND p = mk_host();
    HWND c = mk_child(p, plain_class(), WS_CHILD, 0, 0, 40, 40, 1);
    HWND g = mk_child(c, plain_class(), WS_CHILD, 0, 0, 5, 5, 2);
    NEED_WND(g, wg);
    EXPECT_EQ((ULONG_PTR)wv_link(&wg, W_PARENT), (ULONG_PTR)c);
    EXPECT_EQ((ULONG_PTR)U(GetAncestor)(g, GA_PARENT), (ULONG_PTR)c);
    EXPECT_EQ((ULONG_PTR)U(GetAncestor)(g, GA_ROOT), (ULONG_PTR)p);
    CHECK(U(IsChild)(p, g));
    CHECK(U(IsChild)(c, g));
    CHECK(!U(IsChild)(g, c));
    END();
}
TEST(user_wnd_desktop_heap_rel_message_only_parent_not_desktop) {
    T_BEGIN();
    HWND h = wk_add(U(CreateWindowExW)(0, plain_class(), L"m", WS_POPUP, 0, 0, 0, 0, HWND_MESSAGE, NULL, g_hinst(), NULL));
    NEED_WND(h, w);
    HWND par = wv_link(&w, W_PARENT);
    CHECK(par != NULL && par != (HWND)(ULONG_PTR)-1);
    CHECK(par != U(GetDesktopWindow)());
    t_note("message-only parent hwnd %p (desktop %p)", par, U(GetDesktopWindow)());
    CHECK(!U(IsWindowVisible)(h));
    END();
}

/* ---- all links stay inside the mapped heap ------------------------------------------------------------- */

TEST(user_wnd_desktop_heap_rel_links_resolve_into_same_view) {
    T_BEGIN();
    HWND o = mk_plain();
    HWND p = mk_top(plain_class(), WS_POPUP, 0, -30000, -30000, 80, 60, o);
    HWND c1 = mk_child(p, plain_class(), WS_CHILD, 0, 0, 10, 10, 1);
    HWND c2 = mk_child(p, plain_class(), WS_CHILD, 0, 0, 10, 10, 2);
    NEED_WND(p, wp);
    HWND set[] = { p, c1, c2, o };
    for (size_t i = 0; i < 4; i++) {
        struct wv w;
        CHECK(wv_get(set[i], &w));
        unsigned offs[] = { W_PARENT, W_CHILD, W_OWNER, W_NEXT, W_PREV, W_PCLS };
        for (size_t j = 0; j < 6; j++) {
            uint64_t k = W64(&w, offs[j]);
            if (!k) continue;
            CHECKF(same_view(w.pc, wv_fix(&w, k)), "window %p link 0x%x", set[i], offs[j]);
        }
    }
    END();
}
TEST(user_wnd_desktop_heap_rel_link_targets_have_matching_entries) {
    T_BEGIN();
    HWND p = mk_host();
    HWND c1 = mk_child(p, plain_class(), WS_CHILD, 0, 0, 10, 10, 1);
    HWND c2 = mk_child(p, plain_class(), WS_CHILD, 0, 0, 10, 10, 2);
    NEED_WND(c2, w1);
    struct wv w2;
    CHECK(wv_get(c1, &w2));
    /* c1 is the top child: its next link is c2, and rebasing it equals c2's own entry phead + ulClientDelta */
    EXPECT_EQ(W64(&w2, W_NEXT), w1.e.phead);
    EXPECT_EQ(wv_fix(&w2, W64(&w2, W_NEXT)), w1.e.phead + w1.delta);
    EXPECT_EQ(wv_fix(&w2, W64(&w2, W_NEXT)), w1.pc);
    (void)p;
    END();
}
