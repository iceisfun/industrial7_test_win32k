/* user_wnd_desktop_heap: style/exstyle/state words of the client view versus the user32 getters
 * that read them, and how ShowWindow/EnableWindow/SetWindowLong show up in the structure. */
#define WDH_TAG L"s"
#include "wdh.h"

/* GetWindowData(-0x10) returns style | WS_VISIBLE when exstyle bit 0x800 is set; GetWindowData(-0x14)
 * returns exstyle & (0x0A7777FF | (~state2 & 0x20) << 14). Both come straight from user32. */
static void api_matches_raw(HWND h, const struct wv *w) {
    uint32_t rs = wv_style(w), re = wv_ex(w), st2 = W32(w, W_STATE2);
    uint32_t want_s = rs | ((re & 0x800) ? WS_VISIBLE : 0);
    uint32_t want_e = re & (0x0A7777FFu | ((~st2 & 0x20u) << 14));
    EXPECT_EQ((uint32_t)U(GetWindowLongPtrW)(h, GWL_STYLE), want_s);
    EXPECT_EQ((uint32_t)U(GetWindowLongPtrW)(h, GWL_EXSTYLE), want_e);
    EXPECT_EQ((uint32_t)U(GetWindowLongW)(h, GWL_STYLE), want_s);
    EXPECT_EQ((uint32_t)U(GetWindowLongW)(h, GWL_EXSTYLE), want_e);
}

/* ---- table of creation styles ---------------------------------------------------------------- */

static void style_case(DWORD st, DWORD ex, int child) {
    T_BEGIN();
    HWND host = child ? mk_host() : NULL;
    HWND h = child ? mk_child(host, plain_class(), st, 2, 3, 40, 30, 9)
                   : mk_top(plain_class(), st, ex, -30000, -30000, 120, 90, NULL);
    NEED_WND(h, w);
    uint32_t rs = wv_style(&w);
    uint32_t want = (st & ~WS_VISIBLE) | (child ? WS_CHILD : 0);
    /* window creation stores dwStyle & 0xefffffff (WS_VISIBLE is applied by ShowWindow later) and
     * only adds bits afterwards, so everything requested is present */
    CHECKF((rs & want) == want, "requested 0x%08lx raw 0x%08lx", (unsigned long)want, (unsigned long)rs);
    CHECK((rs & WS_VISIBLE) == 0);
    uint32_t reqex = ex | (child ? 0 : EX_GUEST);
    uint32_t apiex = (uint32_t)U(GetWindowLongPtrW)(h, GWL_EXSTYLE);
    /* WS_EX_WINDOWEDGE is dropped again for a window whose style has no frame to need it (the table test in
     * win_create_destroy_user_exstyle_windowedge_table covers that rule); everything else requested stays */
    uint32_t mustex = reqex & ~(uint32_t)WS_EX_WINDOWEDGE;
    CHECKF((apiex & mustex) == mustex, "requested ex 0x%08lx got 0x%08lx", (unsigned long)reqex, (unsigned long)apiex);
    api_matches_raw(h, &w);
    CHECK(!U(IsWindowVisible)(h));
    CHECK(((rs & WS_CHILD) != 0) == (child != 0));
    CHECK(((rs & WS_POPUP) != 0) == (!child && (st & WS_POPUP) != 0));
    uint32_t extra = rs & ~want & ~WS_VISIBLE;
    if (extra) t_note("kernel added style bits 0x%08lx to 0x%08lx", (unsigned long)extra, (unsigned long)st);
    uint32_t xe = apiex & ~reqex;
    if (xe) t_note("kernel added exstyle bits 0x%08lx to 0x%08lx", (unsigned long)xe, (unsigned long)reqex);
    END();
}
#define SC(id, st, ex) TEST(user_wnd_desktop_heap_style_create_##id) { style_case((st), (ex), 0); }
#define SCC(id, st) TEST(user_wnd_desktop_heap_style_create_child_##id) { style_case((st), 0, 1); }
SC(popup, WS_POPUP, 0)
SC(popup_border, WS_POPUP | WS_BORDER, 0)
SC(popup_caption, WS_POPUP | WS_CAPTION, 0)
SC(popup_dlgframe, WS_POPUP | WS_DLGFRAME, 0)
SC(popup_thickframe, WS_POPUP | WS_THICKFRAME, 0)
SC(popup_sysmenu, WS_POPUP | WS_CAPTION | WS_SYSMENU, 0)
SC(popup_tabstop_group, WS_POPUP | WS_TABSTOP | WS_GROUP, 0)
SC(popup_disabled, WS_POPUP | WS_DISABLED, 0)
SC(popup_clip, WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0)
SC(popup_scrollbars, WS_POPUP | WS_HSCROLL | WS_VSCROLL, 0)
SC(overlapped_window, WS_OVERLAPPEDWINDOW, 0)
SC(overlapped_zero, WS_OVERLAPPED, 0)
SC(popup_ex_clientedge, WS_POPUP, WS_EX_CLIENTEDGE)
SC(popup_ex_staticedge, WS_POPUP, WS_EX_STATICEDGE)
SC(popup_ex_dlgmodalframe, WS_POPUP, WS_EX_DLGMODALFRAME)
SC(popup_ex_controlparent, WS_POPUP, WS_EX_CONTROLPARENT)
SC(popup_ex_acceptfiles, WS_POPUP, WS_EX_ACCEPTFILES)
SC(popup_ex_transparent, WS_POPUP, WS_EX_TRANSPARENT)
SC(popup_ex_topmost, WS_POPUP, WS_EX_TOPMOST)
SC(popup_ex_appwindow, WS_POPUP, WS_EX_APPWINDOW)
SC(popup_ex_contexthelp, WS_POPUP | WS_CAPTION, WS_EX_CONTEXTHELP)
SC(popup_ex_windowedge, WS_POPUP, WS_EX_WINDOWEDGE)
SCC(plain, WS_CHILD)
SCC(border, WS_CHILD | WS_BORDER)
SCC(tabstop, WS_CHILD | WS_TABSTOP)
SCC(disabled, WS_CHILD | WS_DISABLED)
SCC(group_clip, WS_CHILD | WS_GROUP | WS_CLIPSIBLINGS)
SCC(scrollbars, WS_CHILD | WS_HSCROLL | WS_VSCROLL)

TEST(user_wnd_desktop_heap_style_visible_not_stored_at_creation) {
    T_BEGIN();
    HWND h = U(CreateWindowExW)(EX_GUEST, plain_class(), L"v", WS_POPUP | WS_VISIBLE, -30000, -30000, 50, 50, NULL, NULL, g_hinst(), NULL);
    wk_add(h);
    NEED_WND(h, w);
    /* window creation masks 0x10000000 out when it stores the style and shows the window afterwards
 *; either way the window is reported visible */
    CHECK(U(IsWindowVisible)(h));
    CHECK((wv_style(&w) & WS_VISIBLE) != 0 || (wv_ex(&w) & 0x800) != 0);
    CHECK((U(GetWindowLongPtrW)(h, GWL_STYLE) & WS_VISIBLE) != 0);
    api_matches_raw(h, &w);
    END();
}

/* ---- ShowWindow ------------------------------------------------------------------------------- */

TEST(user_wnd_desktop_heap_show_hide_toggles_visible_state) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    CHECK(!U(IsWindowVisible)(h));
    CHECK(!U(ShowWindow)(h, SW_SHOWNOACTIVATE));      /* returns previous visibility */
    CHECK(wv_get(h, &w));
    CHECK(U(IsWindowVisible)(h));
    CHECK((wv_style(&w) & WS_VISIBLE) != 0 || (wv_ex(&w) & 0x800) != 0);
    CHECK((U(GetWindowLongPtrW)(h, GWL_STYLE) & WS_VISIBLE) != 0);
    CHECK(U(ShowWindow)(h, SW_HIDE));
    CHECK(wv_get(h, &w));
    CHECK(!U(IsWindowVisible)(h));
    CHECK((wv_style(&w) & WS_VISIBLE) == 0);
    api_matches_raw(h, &w);
    END();
}
TEST(user_wnd_desktop_heap_show_repeated_is_idempotent) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    U(ShowWindow)(h, SW_SHOWNOACTIVATE);
    CHECK(U(ShowWindow)(h, SW_SHOWNOACTIVATE));       /* already visible -> nonzero */
    CHECK(wv_get(h, &w));
    uint32_t s1 = wv_style(&w), e1 = wv_ex(&w);
    U(ShowWindow)(h, SW_SHOWNOACTIVATE);
    CHECK(wv_get(h, &w));
    EXPECT_EQ(wv_style(&w), s1);
    EXPECT_EQ(wv_ex(&w), e1);
    END();
}
TEST(user_wnd_desktop_heap_show_child_visible_requires_visible_parent) {
    T_BEGIN();
    HWND p = mk_host();
    HWND c = mk_child(p, plain_class(), WS_CHILD, 1, 1, 20, 20, 3);
    NEED_WND(c, wc);
    U(ShowWindow)(c, SW_SHOWNOACTIVATE);
    CHECK(wv_get(c, &wc));
    /* IsWindowVisible walks up the parent chain: the child has WS_VISIBLE but its hidden parent hides it */
    CHECK((wv_style(&wc) & WS_VISIBLE) != 0 || (wv_ex(&wc) & 0x800) != 0);
    CHECK(!U(IsWindowVisible)(c));
    U(ShowWindow)(p, SW_SHOWNOACTIVATE);
    CHECK(U(IsWindowVisible)(c));
    U(ShowWindow)(p, SW_HIDE);
    CHECK(!U(IsWindowVisible)(c));
    CHECK(wv_get(c, &wc));
    CHECK((wv_style(&wc) & WS_VISIBLE) != 0 || (wv_ex(&wc) & 0x800) != 0);   /* child's own bit is unchanged */
    END();
}
TEST(user_wnd_desktop_heap_show_child_visible_flag_independent_of_parent) {
    T_BEGIN();
    HWND p = mk_host();
    U(ShowWindow)(p, SW_SHOWNOACTIVATE);
    HWND c = mk_child(p, plain_class(), WS_CHILD, 1, 1, 20, 20, 3);
    NEED_WND(c, wc);
    CHECK(!U(IsWindowVisible)(c));
    CHECK((wv_style(&wc) & WS_VISIBLE) == 0);
    U(ShowWindow)(c, SW_SHOWNA);
    CHECK(U(IsWindowVisible)(c));
    CHECK(wv_get(c, &wc));
    CHECK((wv_style(&wc) & WS_VISIBLE) != 0);
    END();
}
TEST(user_wnd_desktop_heap_show_setwindowpos_showwindow_flag) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    CHECK(U(SetWindowPos)(h, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW));
    CHECK(wv_get(h, &w));
    CHECK(U(IsWindowVisible)(h));
    CHECK((wv_style(&w) & WS_VISIBLE) != 0 || (wv_ex(&w) & 0x800) != 0);
    CHECK(U(SetWindowPos)(h, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_HIDEWINDOW));
    CHECK(wv_get(h, &w));
    CHECK(!U(IsWindowVisible)(h));
    CHECK((wv_style(&w) & WS_VISIBLE) == 0);
    END();
}

/* ---- EnableWindow ----------------------------------------------------------------------------- */

TEST(user_wnd_desktop_heap_enable_toggles_disabled_bit) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    CHECK(U(IsWindowEnabled)(h));
    CHECK((wv_style(&w) & WS_DISABLED) == 0);
    CHECK(!U(EnableWindow)(h, FALSE));          /* returns "was disabled" */
    CHECK(wv_get(h, &w));
    CHECK((wv_style(&w) & WS_DISABLED) != 0);   /* IsWindowEnabled tests style bit 27 */
    CHECK(!U(IsWindowEnabled)(h));
    CHECK(U(EnableWindow)(h, TRUE));            /* was disabled -> nonzero */
    CHECK(wv_get(h, &w));
    CHECK((wv_style(&w) & WS_DISABLED) == 0);
    CHECK(U(IsWindowEnabled)(h));
    api_matches_raw(h, &w);
    END();
}
TEST(user_wnd_desktop_heap_enable_created_disabled) {
    T_BEGIN();
    HWND h = mk_top(plain_class(), WS_POPUP | WS_DISABLED, 0, -30000, -30000, 20, 20, NULL);
    NEED_WND(h, w);
    CHECK((wv_style(&w) & WS_DISABLED) != 0);
    CHECK(!U(IsWindowEnabled)(h));
    CHECK(U(EnableWindow)(h, TRUE) != 0);
    CHECK(U(IsWindowEnabled)(h));
    END();
}
TEST(user_wnd_desktop_heap_enable_parent_disable_does_not_flag_child) {
    T_BEGIN();
    HWND p = mk_host();
    HWND c = mk_child(p, plain_class(), WS_CHILD, 1, 1, 10, 10, 4);
    NEED_WND(c, wc);
    U(EnableWindow)(p, FALSE);
    CHECK(wv_get(c, &wc));
    /* IsWindowEnabled looks only at the window's own style bit */
    CHECK((wv_style(&wc) & WS_DISABLED) == 0);
    CHECK(U(IsWindowEnabled)(c));
    CHECK(!U(IsWindowEnabled)(p));
    U(EnableWindow)(p, TRUE);
    END();
}
TEST(user_wnd_desktop_heap_enable_child_roundtrip) {
    T_BEGIN();
    HWND p = mk_host();
    HWND c = mk_child(p, plain_class(), WS_CHILD, 1, 1, 10, 10, 4);
    NEED_WND(c, wc);
    U(EnableWindow)(c, FALSE);
    CHECK(wv_get(c, &wc));
    CHECK((wv_style(&wc) & WS_DISABLED) != 0);
    CHECK((wv_style(&wc) & WS_CHILD) != 0);
    U(EnableWindow)(c, TRUE);
    CHECK(wv_get(c, &wc));
    CHECK((wv_style(&wc) & WS_DISABLED) == 0);
    END();
}

/* ---- minimize / maximize (child windows of a hidden parent) ------------------------------------ */

static void minmax_case(int cmd, DWORD bit, int want_iconic, int want_zoomed) {
    T_BEGIN();
    HWND p = mk_host();
    HWND c = mk_child(p, plain_class(), WS_CHILD, 10, 10, 60, 40, 6);
    NEED_WND(c, wc);
    U(ShowWindow)(c, cmd);
    CHECK(wv_get(c, &wc));
    BOOL ic = U(IsIconic)(c), zm = U(IsZoomed)(c);
    /* IsIconic reads bit 29 and IsZoomed bit 24 of the style word; the API and the memory agree */
    EXPECT_EQ(ic != 0, (wv_style(&wc) & WS_MINIMIZE) != 0);
    EXPECT_EQ(zm != 0, (wv_style(&wc) & WS_MAXIMIZE) != 0);
    api_matches_raw(c, &wc);
    if (ic != want_iconic || zm != want_zoomed) t_note("ShowWindow(%d) on a child: iconic=%d zoomed=%d", cmd, ic, zm);
    (void)bit;
    U(ShowWindow)(c, SW_RESTORE);
    CHECK(wv_get(c, &wc));
    CHECK(!U(IsIconic)(c));
    CHECK(!U(IsZoomed)(c));
    CHECK((wv_style(&wc) & (WS_MINIMIZE | WS_MAXIMIZE)) == 0);
    END();
}
TEST(user_wnd_desktop_heap_minmax_child_minimize) { minmax_case(SW_SHOWMINNOACTIVE, WS_MINIMIZE, 1, 0); }
TEST(user_wnd_desktop_heap_minmax_child_maximize) { minmax_case(SW_SHOWMAXIMIZED, WS_MAXIMIZE, 0, 1); }
TEST(user_wnd_desktop_heap_minmax_child_minimize_via_minimize_cmd) { minmax_case(SW_MINIMIZE, WS_MINIMIZE, 1, 0); }
TEST(user_wnd_desktop_heap_minmax_normal_window_is_neither) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    CHECK(!U(IsIconic)(h));
    CHECK(!U(IsZoomed)(h));
    CHECK((wv_style(&w) & (WS_MINIMIZE | WS_MAXIMIZE)) == 0);
    END();
}
TEST(user_wnd_desktop_heap_minmax_invalid_handle_false) {
    T_BEGIN();
    HWND h = mk_plain();
    if (!h) { t_skip("window creation failed"); return; }
    HWND stale = h;
    U(DestroyWindow)(h);
    CHECK(!U(IsIconic)(stale));
    CHECK(!U(IsZoomed)(stale));
    CHECK(!U(IsWindowEnabled)(stale));
    CHECK(!U(IsWindowVisible)(stale));
    CHECK(!U(IsWindowUnicode)(stale));
}

/* ---- SetWindowLong on the style words ------------------------------------------------------------ */

TEST(user_wnd_desktop_heap_setstyle_tabstop_group_roundtrip) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    LONG_PTR old = U(GetWindowLongPtrW)(h, GWL_STYLE);
    LONG_PTR prev = U(SetWindowLongPtrW)(h, GWL_STYLE, old | WS_TABSTOP | WS_GROUP);
    EXPECT_EQ((uint32_t)prev, (uint32_t)old);   /* returns the previous value */
    CHECK(wv_get(h, &w));
    CHECK((wv_style(&w) & (WS_TABSTOP | WS_GROUP)) == (WS_TABSTOP | WS_GROUP));
    api_matches_raw(h, &w);
    U(SetWindowLongPtrW)(h, GWL_STYLE, old);
    CHECK(wv_get(h, &w));
    CHECK((wv_style(&w) & (WS_TABSTOP | WS_GROUP)) == 0);
    END();
}
TEST(user_wnd_desktop_heap_setstyle_32bit_variant) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    LONG old = U(GetWindowLongW)(h, GWL_STYLE);
    LONG prev = U(SetWindowLongW)(h, GWL_STYLE, old | WS_TABSTOP);
    EXPECT_EQ((uint32_t)prev, (uint32_t)old);
    CHECK(wv_get(h, &w));
    CHECK((wv_style(&w) & WS_TABSTOP) != 0);
    U(SetWindowLongW)(h, GWL_STYLE, old);
    END();
}
TEST(user_wnd_desktop_heap_setstyle_disabled_via_setwindowlong) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    LONG_PTR old = U(GetWindowLongPtrW)(h, GWL_STYLE);
    U(SetWindowLongPtrW)(h, GWL_STYLE, old | WS_DISABLED);
    CHECK(wv_get(h, &w));
    CHECK((wv_style(&w) & WS_DISABLED) != 0);
    CHECK(!U(IsWindowEnabled)(h));
    U(SetWindowLongPtrW)(h, GWL_STYLE, old & ~(LONG_PTR)WS_DISABLED);
    CHECK(U(IsWindowEnabled)(h));
    END();
}
TEST(user_wnd_desktop_heap_setstyle_exstyle_roundtrip) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    LONG_PTR old = U(GetWindowLongPtrW)(h, GWL_EXSTYLE);
    DWORD bits[] = { WS_EX_CLIENTEDGE, WS_EX_STATICEDGE, WS_EX_TRANSPARENT, WS_EX_ACCEPTFILES, WS_EX_CONTROLPARENT };
    for (size_t i = 0; i < sizeof bits / sizeof *bits; i++) {
        U(SetWindowLongPtrW)(h, GWL_EXSTYLE, old | bits[i]);
        CHECK(wv_get(h, &w));
        CHECKF((wv_ex(&w) & bits[i]) == bits[i], "bit 0x%lx", (unsigned long)bits[i]);
        CHECKF((U(GetWindowLongPtrW)(h, GWL_EXSTYLE) & bits[i]) == bits[i], "bit 0x%lx", (unsigned long)bits[i]);
        U(SetWindowLongPtrW)(h, GWL_EXSTYLE, old);
        CHECK(wv_get(h, &w));
        CHECKF((wv_ex(&w) & bits[i]) == (old & bits[i]), "bit 0x%lx", (unsigned long)bits[i]);
    }
    END();
}
TEST(user_wnd_desktop_heap_setstyle_exstyle_returns_previous) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    LONG_PTR old = U(GetWindowLongPtrW)(h, GWL_EXSTYLE);
    LONG_PTR prev = U(SetWindowLongPtrW)(h, GWL_EXSTYLE, old | WS_EX_TRANSPARENT);
    const uint32_t vis = 0x0A7777FFu;   /* the part GetWindowLong reports; the raw word may carry more */
    EXPECT_EQ((uint32_t)prev & vis, (uint32_t)old & vis);
    prev = U(SetWindowLongPtrW)(h, GWL_EXSTYLE, old);
    EXPECT_EQ((uint32_t)prev & vis, (uint32_t)(old | WS_EX_TRANSPARENT) & vis);
    END();
}
TEST(user_wnd_desktop_heap_setstyle_hidden_private_exstyle_bits_masked) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    /* GetWindowData(-0x14) masks the word: no bit outside 0x0A7777FF (+ WS_EX_LAYERED) is reported */
    uint32_t api = (uint32_t)U(GetWindowLongPtrW)(h, GWL_EXSTYLE);
    CHECK((api & ~(0x0A7777FFu | WS_EX_LAYERED)) == 0);
    uint32_t hidden = wv_ex(&w) & ~(0x0A7777FFu | WS_EX_LAYERED);
    t_note("raw exstyle bits the API hides: 0x%08lx", (unsigned long)hidden);
    END();
}

/* ---- ANSI / Unicode state bit and window procedure ------------------------------------------------ */

static LRESULT CALLBACK wp_a(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcA(h, m, w, l); }
static LRESULT CALLBACK wp_w(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }
static volatile int g_w2hits;
static LRESULT CALLBACK wp_w2(HWND h, UINT m, WPARAM w, LPARAM l) { g_w2hits++; return DefWindowProcW(h, m, w, l); }

TEST(user_wnd_desktop_heap_unicode_bit_unicode_class) {
    T_BEGIN();
    wchar_t cn[64];
    if (!reg_class(cn, 0, 0, 0, wp_w, FALSE, NULL, NULL)) { t_skip("register failed"); return; }
    HWND h = mk_top(cn, WS_POPUP, 0, -30000, -30000, 10, 10, NULL);
    NEED_WND(h, w);
    /* IsWindowUnicode = !(byte 0x12 & 8) */
    CHECK(U(IsWindowUnicode)(h));
    CHECK(((W32(&w, W_STATE) >> 16) & 8) == 0);
    END();
}
TEST(user_wnd_desktop_heap_unicode_bit_ansi_class) {
    T_BEGIN();
    wchar_t cn[64];
    if (!reg_class(cn, 0, 0, 0, wp_a, TRUE, NULL, NULL)) { t_skip("register failed"); return; }
    /* ANSI class: the window is an ANSI window whichever CreateWindowEx flavour created it */
    HWND h = mk_top(cn, WS_POPUP, 0, -30000, -30000, 10, 10, NULL);
    NEED_WND(h, w);
    CHECK(!U(IsWindowUnicode)(h));
    CHECK(((W32(&w, W_STATE) >> 16) & 8) != 0);
    END();
}
TEST(user_wnd_desktop_heap_unicode_bit_ansi_class_created_with_a) {
    T_BEGIN();
    wchar_t cn[64];
    if (!reg_class(cn, 0, 0, 0, wp_a, TRUE, NULL, NULL)) { t_skip("register failed"); return; }
    char an[64]; wcstombs(an, cn, 64);
    HWND h = wk_add(U(CreateWindowExA)(EX_GUEST, an, "a", WS_POPUP, -30000, -30000, 10, 10, NULL, NULL, g_hinst(), NULL));
    NEED_WND(h, w);
    CHECK(!U(IsWindowUnicode)(h));
    CHECK(((W32(&w, W_STATE) >> 16) & 8) != 0);
    END();
}
TEST(user_wnd_desktop_heap_unicode_bit_unicode_class_created_with_a) {
    T_BEGIN();
    wchar_t cn[64];
    if (!reg_class(cn, 0, 0, 0, wp_w, FALSE, NULL, NULL)) { t_skip("register failed"); return; }
    char an[64]; wcstombs(an, cn, 64);
    HWND h = wk_add(U(CreateWindowExA)(EX_GUEST, an, "a", WS_POPUP, -30000, -30000, 10, 10, NULL, NULL, g_hinst(), NULL));
    NEED_WND(h, w);
    CHECK(U(IsWindowUnicode)(h));
    CHECK(((W32(&w, W_STATE) >> 16) & 8) == 0);
    END();
}
TEST(user_wnd_desktop_heap_wndproc_raw_equals_registered_unicode) {
    T_BEGIN();
    wchar_t cn[64];
    if (!reg_class(cn, 0, 0, 0, wp_w, FALSE, NULL, NULL)) { t_skip("register failed"); return; }
    HWND h = mk_top(cn, WS_POPUP, 0, -30000, -30000, 10, 10, NULL);
    NEED_WND(h, w);
    /* GetWindowData(-4) returns *(wnd+0x78) unchanged when the caller's ANSI-ness matches the window's
     * and the procedure is not a server-side one (state bit 0x40000 clear) */
    CHECK(((W32(&w, W_STATE) >> 16) & 4) == 0);
    EXPECT_EQ(W64(&w, W_WNDPROC), (ULONG_PTR)wp_w);
    EXPECT_EQ((ULONG_PTR)U(GetWindowLongPtrW)(h, GWLP_WNDPROC), (ULONG_PTR)wp_w);
    END();
}
TEST(user_wnd_desktop_heap_wndproc_raw_equals_registered_ansi) {
    T_BEGIN();
    wchar_t cn[64];
    if (!reg_class(cn, 0, 0, 0, wp_a, TRUE, NULL, NULL)) { t_skip("register failed"); return; }
    HWND h = mk_top(cn, WS_POPUP, 0, -30000, -30000, 10, 10, NULL);
    NEED_WND(h, w);
    EXPECT_EQ(W64(&w, W_WNDPROC), (ULONG_PTR)wp_a);
    EXPECT_EQ((ULONG_PTR)U(GetWindowLongPtrA)(h, GWLP_WNDPROC), (ULONG_PTR)wp_a);
    /* the Unicode getter on an ANSI window hands out a translation thunk, not the raw procedure */
    ULONG_PTR wide = (ULONG_PTR)U(GetWindowLongPtrW)(h, GWLP_WNDPROC);
    CHECK(wide != 0);
    if (wide == (ULONG_PTR)wp_a) t_note("GetWindowLongPtrW on an ANSI window returned the raw procedure");
    END();
}
TEST(user_wnd_desktop_heap_wndproc_subclass_updates_raw) {
    T_BEGIN();
    wchar_t cn[64];
    if (!reg_class(cn, 0, 0, 0, wp_w, FALSE, NULL, NULL)) { t_skip("register failed"); return; }
    HWND h = mk_top(cn, WS_POPUP, 0, -30000, -30000, 10, 10, NULL);
    NEED_WND(h, w);
    LONG_PTR prev = U(SetWindowLongPtrW)(h, GWLP_WNDPROC, (LONG_PTR)wp_w2);
    EXPECT_EQ(prev, (ULONG_PTR)wp_w);
    CHECK(wv_get(h, &w));
    EXPECT_EQ(W64(&w, W_WNDPROC), (ULONG_PTR)wp_w2);
    prev = U(SetWindowLongPtrW)(h, GWLP_WNDPROC, (LONG_PTR)wp_w);
    EXPECT_EQ(prev, (ULONG_PTR)wp_w2);
    CHECK(wv_get(h, &w));
    EXPECT_EQ(W64(&w, W_WNDPROC), (ULONG_PTR)wp_w);
    END();
}
TEST(user_wnd_desktop_heap_wndproc_other_thread_window_readable) {
    T_BEGIN();
    if (!plain_class()) { t_skip("class registration failed"); return; }
    struct xt x;
    if (!xt_start(&x, plain_class())) { xt_stop(&x); t_skip("helper thread window failed"); return; }
    NEED_WND(x.hwnd, w);
    /* GetWindowData(-4) for a window of another thread of the same process (pid compares equal) */
    EXPECT_EQ(W64(&w, W_WNDPROC), (ULONG_PTR)wdh_wp);
    EXPECT_EQ((ULONG_PTR)U(GetWindowLongPtrW)(x.hwnd, GWLP_WNDPROC), (ULONG_PTR)wdh_wp);
    xt_stop(&x);
    END();
}

/* ---- dialog windows -------------------------------------------------------------------------------- */

static INT_PTR CALLBACK dlgproc(HWND h, UINT m, WPARAM w, LPARAM l) { (void)h; (void)w; (void)l; return m == WM_INITDIALOG ? FALSE : FALSE; }
static HWND mk_dialog(void) {
    WORD buf[32] __attribute__((aligned(8)));
    memset(buf, 0, sizeof buf);
    DLGTEMPLATE *t = (DLGTEMPLATE *)buf;
    t->style = WS_POPUP;
    t->dwExtendedStyle = EX_GUEST;
    t->cdit = 0;
    t->x = 0; t->y = 0; t->cx = 40; t->cy = 30;
    /* menu, class, title: three zero words follow the header */
    if (!U(CreateDialogIndirectParamW)) return NULL;
    return wk_add(U(CreateDialogIndirectParamW)(g_hinst(), t, NULL, dlgproc, 0));
}
TEST(user_wnd_desktop_heap_dialog_state_bit_set) {
    T_BEGIN();
    HWND d = mk_dialog();
    NEED_WND(d, w);
    HWND p = mk_plain();
    struct wv wp;
    CHECK(wv_get(p, &wp));
    /* byte 0x12 bit0 selects the DWLP_* index set in _GetWindowLongPtr and GetWindowWord */
    CHECK((W32(&w, W_STATE) >> 16 & 1) != 0);
    CHECK((W32(&wp, W_STATE) >> 16 & 1) == 0);
    t_note("dialog fnid=0x%x cbwndextra=%u extrabase=%u", wv_fnid(&w), W32(&w, W_CBEXTRA), W32(&w, W_EXTRABASE));
    END();
}
TEST(user_wnd_desktop_heap_dialog_dlgproc_index) {
    T_BEGIN();
    HWND d = mk_dialog();
    NEED_WND(d, w);
    CHECK(U(IsWindow)(d));
    EXPECT_EQ((ULONG_PTR)U(GetWindowLongPtrW)(d, DWLP_DLGPROC), (ULONG_PTR)dlgproc);
    END();
}
TEST(user_wnd_desktop_heap_dialog_msgresult_is_extra_qword_zero) {
    T_BEGIN();
    HWND d = mk_dialog();
    NEED_WND(d, w);
    SET_ERR(0);
    U(SetWindowLongPtrW)(d, DWLP_MSGRESULT, 0x1234);
    EXPECT_EQ((ULONG_PTR)U(GetWindowLongPtrW)(d, DWLP_MSGRESULT), 0x1234);
    uint64_t v = 0;
    CHECK(wv_get(d, &w));
    /* same-thread read: **(wnd+0x128) */
    CHECK(wv_read_at(W64(&w, W_EXTRAPTR), &v, 8));
    EXPECT_EQ(v, 0x1234);
    END();
}
TEST(user_wnd_desktop_heap_dialog_user_is_extra_at_0x10) {
    T_BEGIN();
    HWND d = mk_dialog();
    NEED_WND(d, w);
    U(SetWindowLongPtrW)(d, DWLP_USER, 0x5a5a5a5a5a5aULL);
    EXPECT_EQ((ULONG_PTR)U(GetWindowLongPtrW)(d, DWLP_USER), 0x5a5a5a5a5a5aULL);
    uint64_t v = 0;
    CHECK(wv_get(d, &w));
    CHECK(wv_read_at(W64(&w, W_EXTRAPTR) + 0x10, &v, 8));
    EXPECT_EQ(v, 0x5a5a5a5a5a5aULL);
    END();
}
TEST(user_wnd_desktop_heap_dialog_private_indices_rejected) {
    T_BEGIN();
    HWND d = mk_dialog();
    NEED_WND(d, w);
    /* dialog windows: indices below 0x1e other than 0, 8 and 0x10 belong to the system -> error 0x587 */
    int idx[] = { 4, 12, 16 + 4, 24 };
    for (size_t i = 0; i < sizeof idx / sizeof *idx; i++) {
        SET_ERR(0);
        ULONG_PTR v = (ULONG_PTR)U(GetWindowLongPtrW)(d, idx[i]);
        CHECKF(v == 0, "index %d", idx[i]);
        EXPECT_ERR(1415);   /* ERROR_PRIVATE_DIALOG_INDEX */
    }
    END();
}
TEST(user_wnd_desktop_heap_dialog_plain_window_has_no_dialog_indices) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    /* for an ordinary window DWLP_USER (0x10) is just extra byte 16 */
    U(SetWindowLongPtrW)(h, DWLP_USER, 0x77);
    EXPECT_EQ((ULONG_PTR)U(GetWindowLongPtrW)(h, 0x10), 0x77);
    SET_ERR(0);
    ULONG_PTR v4 = (ULONG_PTR)U(GetWindowLongPtrW)(h, 4);   /* extra byte 4 of a 64-byte area */
    EXPECT_EQ(v4, 0);
    if (GetLastError()) t_note("GetWindowLongPtrW(plain, 4) set error %lu", GetLastError());
    END();
}

/* ---- function ids of system classes ------------------------------------------------------------------ */

static const struct { const wchar_t *cls; DWORD style; } k_sysclass[] = {
    { L"Button", BS_PUSHBUTTON }, { L"Static", SS_LEFT }, { L"Edit", ES_LEFT }, { L"ListBox", LBS_STANDARD & ~WS_VISIBLE },
    { L"ComboBox", CBS_DROPDOWNLIST }, { L"ScrollBar", SBS_HORZ }, { L"MDIClient", 0 },
};
static unsigned sys_fnid(int i, int *ok) {
    HWND p = mk_host();
    HWND c = NULL;
    if (p) {
        CLIENTCREATESTRUCT ccs = { NULL, 1 };
        c = wk_add(U(CreateWindowExW)(0, k_sysclass[i].cls, L"x", WS_CHILD | k_sysclass[i].style, 1, 1, 80, 60, p, (HMENU)(ULONG_PTR)(100 + i), g_hinst(),
                                      wcscmp(k_sysclass[i].cls, L"MDIClient") ? NULL : &ccs));
    }
    struct wv w;
    *ok = c && wv_get(c, &w);
    return *ok ? wv_fnid(&w) : 0;
}
static void sysclass_case(int i) {
    T_BEGIN();
    int ok;
    unsigned f = sys_fnid(i, &ok);
    if (!ok) { wk_reap(); t_skip("cannot create or decode %ls", k_sysclass[i].cls); return; }
    /* window creation -> WM_NCCREATE in the control's server-side procedure records the control's
     * function id; plain windows of an application class would not have one of the control range */
    CHECKF(f != 0, "%ls fnid 0", k_sysclass[i].cls);
    t_note("%ls window fnid=0x%x", k_sysclass[i].cls, f);
    END();
}
#define SYS(n, i) TEST(user_wnd_desktop_heap_fnid_sysclass_##n) { sysclass_case(i); }
SYS(button, 0) SYS(static, 1) SYS(edit, 2) SYS(listbox, 3) SYS(combobox, 4) SYS(scrollbar, 5) SYS(mdiclient, 6)

TEST(user_wnd_desktop_heap_fnid_sysclasses_distinct) {
    T_BEGIN();
    unsigned f[7];
    int ok;
    for (int i = 0; i < 7; i++) { f[i] = sys_fnid(i, &ok); if (!ok) { wk_reap(); t_skip("cannot create %ls", k_sysclass[i].cls); return; } }
    for (int i = 0; i < 7; i++)
        for (int j = i + 1; j < 7; j++)
            CHECKF(f[i] != f[j], "%ls and %ls share fnid 0x%x", k_sysclass[i].cls, k_sysclass[j].cls, f[i]);
    END();
}
TEST(user_wnd_desktop_heap_fnid_same_class_same_fnid) {
    T_BEGIN();
    HWND p = mk_host();
    HWND a = mk_child(p, L"Button", WS_CHILD | BS_PUSHBUTTON, 0, 0, 30, 20, 1);
    HWND b = mk_child(p, L"Button", WS_CHILD | BS_CHECKBOX, 40, 0, 30, 20, 2);
    HWND c = mk_child(p, L"Static", WS_CHILD, 80, 0, 30, 20, 3);
    struct wv wa, wb, wc;
    NEED_WND(a, w0); (void)w0;
    CHECK(wv_get(a, &wa) && wv_get(b, &wb) && wv_get(c, &wc));
    EXPECT_EQ(wv_fnid(&wa), wv_fnid(&wb));
    CHECK(wv_fnid(&wa) != wv_fnid(&wc));
    END();
}
TEST(user_wnd_desktop_heap_fnid_system_class_wndproc_is_server_side) {
    T_BEGIN();
    HWND p = mk_host();
    HWND b = mk_child(p, L"Button", WS_CHILD | BS_PUSHBUTTON, 0, 0, 30, 20, 1);
    NEED_WND(b, w);
    /* GetWindowData(-4) treats state bit 0x40000 as "server-side procedure" and maps it back to a
     * client procedure; the raw word is then not a user-mode address and the API still returns one */
    uint32_t srv = (W32(&w, W_STATE) >> 16) & 4;
    ULONG_PTR api = (ULONG_PTR)U(GetWindowLongPtrW)(b, GWLP_WNDPROC);
    CHECK(api != 0);
    t_note("Button wndproc: server-side bit=%u raw=0x%llx api=%p", srv, (unsigned long long)W64(&w, W_WNDPROC), (void *)api);
    END();
}
TEST(user_wnd_desktop_heap_fnid_application_class_note) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    t_note("application-class window fnid=0x%x state=0x%08lx state2=0x%08lx band=%lu", wv_fnid(&w), (unsigned long)W32(&w, W_STATE),
           (unsigned long)W32(&w, W_STATE2), (unsigned long)W32(&w, W_BAND));
    END();
}
TEST(user_wnd_desktop_heap_fnid_deleted_bit_clear_on_live_window) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    /* the high bits of the fnid word are status bits; only the 0x2fff part is the function id user32 compares */
    uint16_t raw = W16(&w, W_FNID);
    t_note("raw fnid word=0x%04x", raw);
    CHECK((raw & 0x8000) == 0);   /* a live window is not marked deleted */
    END();
}

/* ---- identity words ------------------------------------------------------------------------------------ */

TEST(user_wnd_desktop_heap_hinstance_raw_matches_api) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    EXPECT_EQ(W64(&w, W_HMODULE), (ULONG_PTR)g_hinst());
    EXPECT_EQ((ULONG_PTR)U(GetWindowLongPtrW)(h, GWLP_HINSTANCE), (ULONG_PTR)g_hinst());
    END();
}
TEST(user_wnd_desktop_heap_userdata_raw_matches_api) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    EXPECT_EQ(W64(&w, W_USERDATA), 0);
    U(SetWindowLongPtrW)(h, GWLP_USERDATA, (LONG_PTR)0x123456789abcdefULL);
    CHECK(wv_get(h, &w));
    EXPECT_EQ(W64(&w, W_USERDATA), 0x123456789abcdefULL);
    EXPECT_EQ((ULONG_PTR)U(GetWindowLongPtrW)(h, GWLP_USERDATA), 0x123456789abcdefULL);
    EXPECT_EQ((uint32_t)U(GetWindowLongW)(h, GWLP_USERDATA), 0x89abcdefu);
    END();
}
TEST(user_wnd_desktop_heap_userdata_set_returns_previous) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    EXPECT_EQ((ULONG_PTR)U(SetWindowLongPtrW)(h, GWLP_USERDATA, 11), 0);
    EXPECT_EQ((ULONG_PTR)U(SetWindowLongPtrW)(h, GWLP_USERDATA, 22), 11);
    CHECK(wv_get(h, &w));
    EXPECT_EQ(W64(&w, W_USERDATA), 22);
    END();
}
TEST(user_wnd_desktop_heap_userdata_32bit_set_low_dword) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    U(SetWindowLongPtrW)(h, GWLP_USERDATA, (LONG_PTR)0x1111222233334444ULL);
    U(SetWindowLongW)(h, GWLP_USERDATA, 0x55556666);
    CHECK(wv_get(h, &w));
    /* the 32-bit write lands in the low dword; what happens to the high dword is only observed */
    EXPECT_EQ((uint32_t)W64(&w, W_USERDATA), 0x55556666u);
    EXPECT_EQ((uint32_t)U(GetWindowLongW)(h, GWLP_USERDATA), 0x55556666u);
    t_note("high dword after SetWindowLongW(GWLP_USERDATA): 0x%08lx", (unsigned long)(W64(&w, W_USERDATA) >> 32));
    END();
}
TEST(user_wnd_desktop_heap_id_child_raw_matches_api) {
    T_BEGIN();
    HWND p = mk_host();
    HWND c = mk_child(p, plain_class(), WS_CHILD, 0, 0, 5, 5, 0x4321);
    NEED_WND(c, w);
    /* GetWindowData(-0xc) for a child returns *(wnd+0x98) itself */
    EXPECT_EQ(W64(&w, W_ID), 0x4321);
    EXPECT_EQ((ULONG_PTR)U(GetWindowLongPtrW)(c, GWLP_ID), 0x4321);
    EXPECT_EQ((ULONG)U(GetDlgCtrlID)(c), 0x4321);
    U(SetWindowLongPtrW)(c, GWLP_ID, 0x777);
    CHECK(wv_get(c, &w));
    EXPECT_EQ(W64(&w, W_ID), 0x777);
    EXPECT_EQ((ULONG)U(GetDlgCtrlID)(c), 0x777);
    END();
}
TEST(user_wnd_desktop_heap_id_toplevel_without_menu_is_zero) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    EXPECT_EQ(W64(&w, W_ID), 0);
    EXPECT_EQ((ULONG_PTR)U(GetWindowLongPtrW)(h, GWLP_ID), 0);
    CHECK(U(GetMenu)(h) == NULL);
    END();
}
TEST(user_wnd_desktop_heap_id_toplevel_menu_handle) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    HMENU m = U(CreateMenu)();
    if (!m) { t_skip("CreateMenu failed"); return; }
    CHECK(U(SetMenu)(h, m));
    CHECK(wv_get(h, &w));
    CHECK(W64(&w, W_ID) != 0);   /* pointer to the menu object */
    /* GetWindowData(-0xc): non-child -> the HMENU stored in the menu object the word points to */
    EXPECT_EQ((ULONG_PTR)U(GetWindowLongPtrW)(h, GWLP_ID), (ULONG_PTR)m);
    EXPECT_EQ((ULONG_PTR)U(GetMenu)(h), (ULONG_PTR)m);
    U(SetMenu)(h, NULL);
    U(DestroyMenu)(m);
    END();
}
TEST(user_wnd_desktop_heap_band_and_dpi_words_note) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    t_note("band=%lu dpictx=0x%08lx monitor=0x%llx", (unsigned long)W32(&w, W_BAND), (unsigned long)W32(&w, W_DPICTX), (unsigned long long)W64(&w, W_MONITOR));
    END();
}
