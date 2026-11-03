/* r2_wnd_cls_heap_fields: tagWND state word and the client-visible effects of window mutation.
 *
 * state (dword at +0x10): byte 0x12 bit 0 is the dialog flag (ValidateDialogPwnd), bit 2 the server-side
 * window procedure flag (SendMessageWorker, _GetWindowLongPtr), bit 3 the ANSI window procedure flag
 * (IsWindowUnicode returns its inverse); byte 0x13 bit 5 is "created by CreateWindowExA", which
 * RealDefWindowProcWorker reports for WM_NOTIFYFORMAT/NF_QUERY as NFR_ANSI (1), else NFR_UNICODE (2). */
#define R2H_TAG L"st"
#include "r2h.h"

#define CREATION_BITS (ST_DIALOG | ST_SRVPROC | ST_ANSIPROC | ST_ANSICREATOR)

static LRESULT CALLBACK ansi_wp(HWND h, UINT m, WPARAM wp, LPARAM lp) { return DefWindowProcA(h, m, wp, lp); }

static HWND create_with(bool ansi_class, bool create_a, int *outclass_ok) {
    wchar_t cn[64];
    if (!reg_class(cn, 0, 0, 0, ansi_class ? (WNDPROC)ansi_wp : (WNDPROC)r2h_wp2, ansi_class, NULL, NULL)) return NULL;
    if (create_a) {
        char a[64]; wcstombs(a, cn, 64);
        return wk_add(U(CreateWindowExA)(EX_GUEST, a, "t", WS_POPUP, -30000, -30000, 20, 20, NULL, NULL, g_hinst(), NULL));
    }
    return wk_add(U(CreateWindowExW)(EX_GUEST, cn, L"t", WS_POPUP, -30000, -30000, 20, 20, NULL, NULL, g_hinst(), NULL));
}

static void creator_case(bool ansi_class, bool create_a) {
    T_BEGIN();
    HWND h = create_with(ansi_class, create_a, NULL);
    NEED_WND(h, w);
    uint32_t st = W32(&w, W_STATE);
    EXPECT_EQ((st & ST_ANSICREATOR) != 0, create_a);
    /* WM_NOTIFYFORMAT(NF_QUERY=3) goes to the class procedure, which forwards to DefWindowProc */
    LRESULT r = U(SendMessageW)(h, WM_NOTIFYFORMAT, (WPARAM)h, NF_QUERY);
    EXPECT_EQ(r, create_a ? NFR_ANSI : NFR_UNICODE);
    /* the procedure kind comes from the class registration, not from the creating function */
    EXPECT_EQ((st & ST_ANSIPROC) != 0, ansi_class);
    EXPECT_EQ(U(IsWindowUnicode)(h), !ansi_class);
    END();
}
#define CC4(id, ac, ca) TEST(r2_wnd_cls_heap_fields_state_creator_##id) { creator_case(ac, ca); }
CC4(wclass_wcreate, false, false)
CC4(wclass_acreate, false, true)
CC4(aclass_wcreate, true, false)
CC4(aclass_acreate, true, true)

TEST(r2_wnd_cls_heap_fields_state_ansi_wndproc_set_by_setwindowlongptr) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w0);
    CHECK(U(IsWindowUnicode)(h));
    EXPECT_EQ(W32(&w0, W_STATE) & ST_ANSIPROC, 0);
    LONG_PTR old = U(SetWindowLongPtrA)(h, GWLP_WNDPROC, (LONG_PTR)ansi_wp);
    CHECK(old != 0);
    struct wv w1;
    CHECK(wv_get(h, &w1));
    CHECK(!U(IsWindowUnicode)(h));
    EXPECT_EQ(W32(&w1, W_STATE) & ST_ANSIPROC, ST_ANSIPROC);     /* IsWindowUnicode == !bit 0x80000 */
    U(SetWindowLongPtrW)(h, GWLP_WNDPROC, (LONG_PTR)r2h_wp2);
    CHECK(wv_get(h, &w1));
    CHECK(U(IsWindowUnicode)(h));
    EXPECT_EQ(W32(&w1, W_STATE) & ST_ANSIPROC, 0);
    /* a window procedure change never touches the creator flag */
    EXPECT_EQ(W32(&w1, W_STATE) & ST_ANSICREATOR, W32(&w0, W_STATE) & ST_ANSICREATOR);
    END();
}

/* Creation attributes do not move under ordinary mutation. */
static uint32_t cbits(HWND h) { struct wv w; return wv_get(h, &w) ? (W32(&w, W_STATE) & CREATION_BITS) : 0xffffffffu; }
TEST(r2_wnd_cls_heap_fields_state_creation_bits_stable_under_mutation) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w0);
    uint32_t base = W32(&w0, W_STATE) & CREATION_BITS;
    U(ShowWindow)(h, SW_SHOWNA);       EXPECT_EQ(cbits(h), base);
    U(MoveWindow)(h, -30000, -30000, 70, 50, TRUE);   EXPECT_EQ(cbits(h), base);
    U(SetWindowTextW)(h, L"mutated"); EXPECT_EQ(cbits(h), base);
    U(EnableWindow)(h, FALSE);        EXPECT_EQ(cbits(h), base);
    U(EnableWindow)(h, TRUE);         EXPECT_EQ(cbits(h), base);
    U(SetWindowPos)(h, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    EXPECT_EQ(cbits(h), base);
    U(ShowWindow)(h, SW_HIDE);        EXPECT_EQ(cbits(h), base);
    END();
}

/* ---- window region (hrgnClip, +0xa8 = GetWindowRgn) ------------------------------------------------ */
TEST(r2_wnd_cls_heap_fields_state_region_none_by_default) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    EXPECT_EQ(W64(&w, W_HRGN), 0);
    HRGN r = U(CreateRectRgn)(0, 0, 1, 1);
    EXPECT_EQ(U(GetWindowRgn)(h, r), ERROR);          /* GetWindowRgn: ERROR when hrgnClip is NULL */
    U(DeleteObject)(r);
    END();
}
TEST(r2_wnd_cls_heap_fields_state_region_set_and_clear) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w0);
    HRGN rg = U(CreateRectRgn)(2, 3, 40, 30);
    CHECK(U(SetWindowRgn)(h, rg, FALSE) != 0);
    struct wv w;
    CHECK(wv_get(h, &w));
    CHECK(W64(&w, W_HRGN) != 0);                       /* GDI handle of the system-owned copy */
    HRGN copy = U(CreateRectRgn)(0, 0, 0, 0);
    int t = U(GetWindowRgn)(h, copy);
    EXPECT_EQ(t, SIMPLEREGION);                        /* CombineRgn(copy, hrgnClip, NULL, RGN_COPY) */
    RECT rc = {0};
    EXPECT_EQ(U(GetRgnBox)(copy, &rc), SIMPLEREGION);
    if (dpi96(h)) CHECK(rc.left == 2 && rc.top == 3 && rc.right == 40 && rc.bottom == 30);
    EXPECT_EQ(U(GetWindowRgnBox)(h, &rc), SIMPLEREGION);
    if (dpi96(h)) CHECK(rc.left == 2 && rc.top == 3 && rc.right == 40 && rc.bottom == 30);
    CHECK(U(SetWindowRgn)(h, NULL, FALSE) != 0);
    CHECK(wv_get(h, &w));
    EXPECT_EQ(W64(&w, W_HRGN), 0);
    EXPECT_EQ(U(GetWindowRgn)(h, copy), ERROR);
    U(DeleteObject)(copy);
    END();
}
TEST(r2_wnd_cls_heap_fields_state_region_complex_shape) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w0);
    HRGN a = U(CreateRectRgn)(0, 0, 10, 10), b = U(CreateRectRgn)(20, 20, 30, 30);
    HRGN u = U(CreateRectRgn)(0, 0, 0, 0);
    U(CombineRgn)(u, a, b, RGN_OR);
    CHECK(U(SetWindowRgn)(h, u, FALSE) != 0);
    HRGN copy = U(CreateRectRgn)(0, 0, 0, 0);
    EXPECT_EQ(U(GetWindowRgn)(h, copy), COMPLEXREGION);
    RECT rc;
    U(GetRgnBox)(copy, &rc);
    if (dpi96(h)) CHECK(rc.left == 0 && rc.top == 0 && rc.right == 30 && rc.bottom == 30);
    U(SetWindowRgn)(h, NULL, FALSE);
    U(DeleteObject)(copy); U(DeleteObject)(a); U(DeleteObject)(b);
    END();
}
TEST(r2_wnd_cls_heap_fields_state_region_not_in_window_rects) {
    T_BEGIN();
    HWND h = mk_top(plain_class(), WS_POPUP, 0, -30000, -30000, 100, 80, NULL);
    NEED_WND(h, w0);
    RECT before = WRC(&w0, W_RCWINDOW);
    HRGN rg = U(CreateRectRgn)(5, 5, 20, 20);
    CHECK(U(SetWindowRgn)(h, rg, FALSE) != 0);
    struct wv w;
    CHECK(wv_get(h, &w));
    RECT after = WRC(&w, W_RCWINDOW);
    CHECK(!memcmp(&before, &after, sizeof before));  /* the region clips, it does not resize the window */
    U(SetWindowRgn)(h, NULL, FALSE);
    END();
}
TEST(r2_wnd_cls_heap_fields_state_region_replace_changes_handle) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w0);
    HRGN r1 = U(CreateRectRgn)(0, 0, 10, 10), r2 = U(CreateRectRgn)(0, 0, 20, 20);
    CHECK(U(SetWindowRgn)(h, r1, FALSE) != 0);
    struct wv wa; CHECK(wv_get(h, &wa));
    uint64_t ha = W64(&wa, W_HRGN);
    CHECK(U(SetWindowRgn)(h, r2, FALSE) != 0);
    struct wv wb; CHECK(wv_get(h, &wb));
    uint64_t hb = W64(&wb, W_HRGN);
    CHECK(ha != 0 && hb != 0);
    CHECK(ha != hb);                                   /* each SetWindowRgn installs its own region object */
    RECT rc; HRGN c = U(CreateRectRgn)(0, 0, 0, 0);
    U(GetWindowRgn)(h, c); U(GetRgnBox)(c, &rc);
    if (dpi96(h)) CHECK(rc.right == 20 && rc.bottom == 20);
    U(SetWindowRgn)(h, NULL, FALSE);
    U(DeleteObject)(c);
    END();
}
TEST(r2_wnd_cls_heap_fields_state_region_invalid_window) {
    T_BEGIN();
    HRGN rg = U(CreateRectRgn)(0, 0, 4, 4);
    SetLastError(0);
    EXPECT_EQ(U(SetWindowRgn)((HWND)(ULONG_PTR)0x12345678, rg, FALSE), 0);
    EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
    HRGN c = U(CreateRectRgn)(0, 0, 0, 0);
    EXPECT_EQ(U(GetWindowRgn)((HWND)(ULONG_PTR)0x12345678, c), 0);
    EXPECT_EQ(U(GetWindowRgn)(U(GetDesktopWindow)(), NULL), 0);
    U(DeleteObject)(rg); U(DeleteObject)(c);
    END();
}

/* ---- lifetime ------------------------------------------------------------------------------------ */
struct life { HWND h; int n; uint32_t dm_state; uint64_t dm_hwnd; unsigned char dm_type, dm_flags; bool dm_ok; bool nc_ok; unsigned char nc_type, nc_flags; bool isw_dm, isw_nc; };
static struct life g_life;
static LRESULT CALLBACK life_wp(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (g_life.h == h && (m == WM_DESTROY || m == WM_NCDESTROY)) {
        struct wv w;
        bool ok = wv_get(h, &w);
        struct he e; bool eok = he_read(h, &e);
        if (m == WM_DESTROY) {
            g_life.dm_ok = ok; g_life.isw_dm = U(IsWindow)(h) != 0;
            if (eok) { g_life.dm_type = e.type; g_life.dm_flags = e.flags; }
            if (ok) { g_life.dm_state = W32(&w, W_STATE); g_life.dm_hwnd = W64(&w, W_HWND); }
        } else {
            g_life.nc_ok = ok; g_life.isw_nc = U(IsWindow)(h) != 0;
            if (eok) { g_life.nc_type = e.type; g_life.nc_flags = e.flags; }
        }
        g_life.n++;
    }
    return DefWindowProcW(h, m, wp, lp);
}
TEST(r2_wnd_cls_heap_fields_state_destroy_callbacks_see_live_object) {
    T_BEGIN();
    wchar_t cn[64];
    if (!reg_class(cn, 0, 0, 0, life_wp, FALSE, NULL, NULL)) { t_skip("register failed"); return; }
    HWND h = mk_top(cn, WS_POPUP, 0, -30000, -30000, 20, 20, NULL);
    NEED_WND(h, w0);
    memset(&g_life, 0, sizeof g_life);
    g_life.h = h;
    CHECK(U(DestroyWindow)(h));
    EXPECT_EQ(g_life.n, 2);
    CHECK(g_life.isw_dm);                              /* the window is still valid in WM_DESTROY */
    EXPECT_EQ(g_life.dm_hwnd & 0xffffffff, (uint32_t)(ULONG_PTR)h);
    EXPECT_EQ(g_life.dm_type, 1);                      /* entry still describes a window */
    CHECK(!U(IsWindow)(h));                            /* and gone afterwards */
    struct he e;
    if (he_read(h, &e)) CHECK(e.type != 1 || (e.flags & 1) || e.uniq != (unsigned short)((ULONG_PTR)h >> 16));
    t_note("WM_NCDESTROY: client view decodable=%d IsWindow=%d entry type=%u flags=0x%x; WM_DESTROY entry flags=0x%x",
           g_life.nc_ok, g_life.isw_nc, g_life.nc_type, g_life.nc_flags, g_life.dm_flags);
    g_life.h = NULL;
    END();
}
TEST(r2_wnd_cls_heap_fields_state_destroy_frees_handle_slot) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w0);
    uint16_t idx = (uint16_t)((ULONG_PTR)h & 0xffff);
    uint64_t body = w0.pc;
    CHECK(U(DestroyWindow)(h));
    struct he e;
    CHECK(he_read(h, &e));
    CHECK(e.type != 1 || e.uniq != (unsigned short)((ULONG_PTR)h >> 16));
    (void)idx; (void)body;
    struct wv w;
    CHECK(!wv_get(h, &w));                             /* the decoder refuses a stale handle like HMValidateHandle */
    SetLastError(0);
    CHECK(!U(IsWindowVisible)(h));
    CHECK(!U(IsWindowEnabled)(h));
    CHECK(!U(IsIconic)(h));
    CHECK(!U(IsZoomed)(h));
    END();
}
TEST(r2_wnd_cls_heap_fields_state_destroy_twice_fails) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    CHECK(U(DestroyWindow)(h));
    SetLastError(0);
    CHECK(!U(DestroyWindow)(h));
    EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
    END();
}
TEST(r2_wnd_cls_heap_fields_state_destroy_parent_invalidates_children_entries) {
    T_BEGIN();
    HWND host = mk_host();
    HWND c1 = mk_child(host, plain_class(), WS_CHILD, 1, 1, 10, 10, 1);
    HWND c2 = mk_child(host, plain_class(), WS_CHILD, 2, 2, 10, 10, 2);
    HWND g = mk_child(c1, plain_class(), WS_CHILD, 0, 0, 5, 5, 3);
    NEED_WND(host, wh);
    struct wv w;
    CHECK(wv_get(c1, &w)); CHECK(wv_get(c2, &w)); CHECK(wv_get(g, &w));
    CHECK(U(DestroyWindow)(host));
    CHECK(!wv_get(c1, &w)); CHECK(!wv_get(c2, &w)); CHECK(!wv_get(g, &w)); CHECK(!wv_get(host, &w));
    CHECK(!U(IsWindow)(c1) && !U(IsWindow)(c2) && !U(IsWindow)(g));
    END();
}
TEST(r2_wnd_cls_heap_fields_state_destroy_sibling_links_fixed_in_view) {
    T_BEGIN();
    HWND host = mk_host();
    HWND a = mk_child(host, plain_class(), WS_CHILD, 1, 1, 10, 10, 1);
    HWND b = mk_child(host, plain_class(), WS_CHILD, 2, 2, 10, 10, 2);
    HWND c = mk_child(host, plain_class(), WS_CHILD, 3, 3, 10, 10, 3);
    NEED_WND(b, wb);
    CHECK(wv_link(&wb, W_PARENT) == host);
    HWND nx = wv_link(&wb, W_NEXT), pv = wv_link(&wb, W_PREV);
    CHECK(U(DestroyWindow)(b));
    struct wv wn, wp;
    HWND keep_next = nx, keep_prev = pv;
    if (keep_next && keep_next != (HWND)(ULONG_PTR)-1) {
        CHECK(wv_get(keep_next, &wn));
        EXPECT_EQ(wv_link(&wn, W_PREV), keep_prev);       /* neighbours now point at each other */
    }
    if (keep_prev && keep_prev != (HWND)(ULONG_PTR)-1) {
        CHECK(wv_get(keep_prev, &wp));
        EXPECT_EQ(wv_link(&wp, W_NEXT), keep_next);
    }
    (void)a; (void)c;
    END();
}
