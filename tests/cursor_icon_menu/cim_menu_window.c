/* MENUINFO (MNS_* styles, background brush, help id, menu data) and the menu <-> window relation
 * (NtUserSetMenu, destruction with the window, menu bar info). */
#include "cim.h"

static MENUINFO mi_new(DWORD mask) { MENUINFO m; memset(&m, 0, sizeof m); m.cbSize = sizeof m; m.fMask = mask; return m; }
static HMENU popup(void) { return U(CreatePopupMenu)(); }
static HMENU bar(void) { return U(CreateMenu)(); }
#define MKM(var, ctor) HMENU var = ctor(); if (!var) { t_skip("cannot create menu"); return; }

/* ---------------- SetMenuInfo / GetMenuInfo ---------------- */
TEST(cursor_icon_menu_info_maxheight_roundtrip) {
    CIM_GUI(); MKM(h, popup);
    MENUINFO s = mi_new(MIM_MAXHEIGHT); s.cyMax = 123;
    CHECK(U(SetMenuInfo)(h, &s));
    MENUINFO g = mi_new(MIM_MAXHEIGHT);
    CHECK(U(GetMenuInfo)(h, &g));
    EXPECT_EQ(g.cyMax, 123);
    struct cim_menu m;
    if (cim_menu_read(h, &m)) EXPECT_EQ(m.cymax, 123); /* SetMenuInfo stores cyMax */
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_info_helpid_roundtrip) {
    CIM_GUI(); MKM(h, popup);
    MENUINFO s = mi_new(MIM_HELPID); s.dwContextHelpID = 0xCAFE0001;
    CHECK(U(SetMenuInfo)(h, &s));
    MENUINFO g = mi_new(MIM_HELPID);
    CHECK(U(GetMenuInfo)(h, &g));
    EXPECT_EQ(g.dwContextHelpID, 0xCAFE0001);
    struct cim_menu m;
    if (cim_menu_read(h, &m)) EXPECT_EQ(m.helpid, 0xCAFE0001);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_info_menudata_roundtrip_64bit) {
    CIM_GUI(); MKM(h, bar);
    MENUINFO s = mi_new(MIM_MENUDATA); s.dwMenuData = 0x1122334455667788ull;
    CHECK(U(SetMenuInfo)(h, &s));
    MENUINFO g = mi_new(MIM_MENUDATA);
    CHECK(U(GetMenuInfo)(h, &g));
    EXPECT_EQ(g.dwMenuData, 0x1122334455667788ull);
    struct cim_menu m;
    if (cim_menu_read(h, &m)) EXPECT_EQ(m.menudata, 0x1122334455667788ull);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_info_style_bits_table) {
    CIM_GUI();
    static const DWORD styles[] = { MNS_NOCHECK, MNS_MODELESS, MNS_DRAGDROP, MNS_AUTODISMISS, MNS_NOTIFYBYPOS, MNS_CHECKORBMP,
        MNS_NOCHECK | MNS_NOTIFYBYPOS, 0xfc000000, 0 };
    for (unsigned i = 0; i < sizeof styles / sizeof *styles; i++) {
        MKM(h, popup);
        MENUINFO s = mi_new(MIM_STYLE); s.dwStyle = styles[i];
        BOOL ok = U(SetMenuInfo)(h, &s);
        CHECKF(ok, "style 0x%lx", styles[i]);
        MENUINFO g = mi_new(MIM_STYLE);
        CHECK(U(GetMenuInfo)(h, &g));
        CHECKF(g.dwStyle == styles[i], "style 0x%lx got 0x%lx", styles[i], g.dwStyle);
        struct cim_menu m;
        if (cim_menu_read(h, &m)) {
            /* only the top six bits come from dwStyle; the popup bit stays */
            CHECKF((m.flags & 0xfc000000) == styles[i], "flags 0x%x", m.flags);
            CHECKF((m.flags & 1) == 1, "popup bit lost: 0x%x", m.flags);
        }
        U(DestroyMenu)(h);
    }
}
TEST(cursor_icon_menu_info_style_leaves_other_flag_bits) {
    CIM_GUI(); MKM(h, popup);
    CHECK(U(AppendMenuW)(h, MF_STRING, 1, L"a"));
    CHECK(U(SetMenuDefaultItem)(h, 1, FALSE));
    struct cim_menu before, after;
    if (!cim_menu_read(h, &before)) { U(DestroyMenu)(h); t_skip("view not mappable"); return; }
    CHECK(before.flags & 0x400);
    MENUINFO s = mi_new(MIM_STYLE); s.dwStyle = MNS_NOCHECK;
    CHECK(U(SetMenuInfo)(h, &s));
    CHECK(cim_menu_read(h, &after));
    EXPECT_EQ(after.flags & 0x03ffffff, before.flags & 0x03ffffff); /* SetMenuInfo changes only the style bits in the high mask */
    EXPECT_EQ(after.flags & 0xfc000000, MNS_NOCHECK);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_info_style_low_bits_rejected) {
    CIM_GUI(); MKM(h, popup);
    static const DWORD bad[] = { 0x1, 0x100, 0x10000, 0x03ffffff, MNS_NOCHECK | 1 };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        MENUINFO s = mi_new(MIM_STYLE); s.dwStyle = bad[i];
        BOOL r; CIM_CALL_ERR(r, U(SetMenuInfo)(h, &s));
        CHECKF(r == 0 && GetLastError() == E_INVALID_PARAMETER, "style 0x%lx err %lu", bad[i], GetLastError()); /* ValidateMENUINFO: dwStyle & 0x3ffffff */
    }
    /* the same bits are ignored when MIM_STYLE is not requested */
    MENUINFO s = mi_new(MIM_MAXHEIGHT); s.dwStyle = 0x1; s.cyMax = 9;
    CHECK(U(SetMenuInfo)(h, &s));
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_info_fmask_invalid_bits_table) {
    CIM_GUI(); MKM(h, popup);
    static const DWORD bad[] = { 0x20, 0x40, 0x100, 0x10000, 0x40000000, 0x7fffffe0, 0x7fffffff };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        MENUINFO s = mi_new(bad[i]);
        BOOL r; CIM_CALL_ERR(r, U(SetMenuInfo)(h, &s));
        CHECKF(r == 0 && GetLastError() == E_INVALID_PARAMETER, "set mask 0x%lx err %lu", bad[i], GetLastError());
        MENUINFO g = mi_new(bad[i]);
        CIM_CALL_ERR(r, U(GetMenuInfo)(h, &g));
        CHECKF(r == 0 && GetLastError() == E_INVALID_PARAMETER, "get mask 0x%lx err %lu", bad[i], GetLastError());
    }
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_info_cbsize_table) {
    CIM_GUI(); MKM(h, popup);
    static const DWORD sizes[] = { 0, 4, 0x1c, 0x24, 0x27, 0x29, 0x30, 0x40, 0xffffffff };
    for (unsigned i = 0; i < sizeof sizes / sizeof *sizes; i++) {
        MENUINFO s = mi_new(MIM_MAXHEIGHT); s.cbSize = sizes[i];
        BOOL r; CIM_CALL_ERR(r, U(SetMenuInfo)(h, &s));
        CHECKF(r == 0 && GetLastError() == E_INVALID_PARAMETER, "set cbSize 0x%lx err %lu", sizes[i], GetLastError());
        s.cbSize = sizes[i];
        CIM_CALL_ERR(r, U(GetMenuInfo)(h, &s));
        CHECKF(r == 0 && GetLastError() == E_INVALID_PARAMETER, "get cbSize 0x%lx err %lu", sizes[i], GetLastError());
    }
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_info_null_pointer_rejected) {
    CIM_GUI(); MKM(h, popup);
    BOOL r;
    CIM_CALL_ERR(r, U(SetMenuInfo)(h, NULL)); EXPECT_FAIL(r == 0, E_INVALID_PARAMETER);
    CIM_CALL_ERR(r, U(GetMenuInfo)(h, NULL)); EXPECT_FAIL(r == 0, E_INVALID_PARAMETER);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_info_get_mask_zero_changes_nothing) {
    CIM_GUI(); MKM(h, popup);
    MENUINFO g = mi_new(0); g.cyMax = 77; g.dwStyle = 0x55; g.dwContextHelpID = 99;
    CHECK(U(GetMenuInfo)(h, &g));
    EXPECT_EQ(g.cyMax, 77); EXPECT_EQ(g.dwStyle, 0x55); EXPECT_EQ(g.dwContextHelpID, 99);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_info_get_defaults_zero) {
    CIM_GUI(); MKM(h, bar);
    MENUINFO g = mi_new(MIM_STYLE | MIM_MAXHEIGHT | MIM_BACKGROUND | MIM_HELPID | MIM_MENUDATA);
    g.cyMax = 5; g.hbrBack = (HBRUSH)5; g.dwContextHelpID = 5; g.dwMenuData = 5; g.dwStyle = 5;
    CHECK(U(GetMenuInfo)(h, &g));
    EXPECT_EQ(g.cyMax, 0); EXPECT_EQ(g.hbrBack, NULL); EXPECT_EQ(g.dwContextHelpID, 0); EXPECT_EQ(g.dwMenuData, 0); EXPECT_EQ(g.dwStyle, 0);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_info_get_style_masks_to_mns_bits) {
    CIM_GUI(); MKM(h, popup);
    MENUINFO g = mi_new(MIM_STYLE);
    CHECK(U(GetMenuInfo)(h, &g));
    EXPECT_EQ(g.dwStyle, 0); /* the popup bit (flags & 1) is not part of dwStyle: GetMenuInfo masks with 0xfc000000 */
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_info_background_brush_roundtrip_and_ownership) {
    CIM_NEED(); CIM_GUI(); MKM(h, popup);
    HBRUSH br = U(CreateSolidBrush)(RGB(1, 2, 3));
    if (!br) { U(DestroyMenu)(h); t_skip("no brush"); return; }
    MENUINFO s = mi_new(MIM_BACKGROUND); s.hbrBack = br;
    CHECK(U(SetMenuInfo)(h, &s));
    MENUINFO g = mi_new(MIM_BACKGROUND);
    CHECK(U(GetMenuInfo)(h, &g));
    EXPECT_EQ(g.hbrBack, br);
    struct cim_menu m;
    if (cim_menu_read(h, &m)) EXPECT_EQ(m.hbr, (unsigned long long)(ULONG_PTR)br);
    CHECK(U(DestroyMenu)(h));
    /* the kernel only stores the handle: the brush is still ours and still deletable */
    CHECK(U(GetObjectType)(br) == OBJ_BRUSH);
    CHECK(U(DeleteObject)(br));
}
TEST(cursor_icon_menu_info_background_invalid_brush_rejected) {
    CIM_GUI(); MKM(h, popup);
    /* The kernel resolves the brush handle through the GDI handle table: a value whose slot holds no live object is refused with
     * ERROR_INVALID_PARAMETER and nothing is stored. (A small value such as 0x1234 would be accepted whenever that slot happens
     * to hold some live object of the session, so the handle used here has an index far above any slot in use.) */
    MENUINFO s = mi_new(MIM_BACKGROUND); s.hbrBack = (HBRUSH)(ULONG_PTR)0x10fff0;
    BOOL r; CIM_CALL_ERR(r, U(SetMenuInfo)(h, &s));
    CHECK(r == 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    MENUINFO g = mi_new(MIM_BACKGROUND);
    CHECK(U(GetMenuInfo)(h, &g));
    CHECK(g.hbrBack == NULL);
    s.hbrBack = NULL; /* NULL clears the background and is legal */
    CHECK(U(SetMenuInfo)(h, &s));
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_info_background_stock_brush_ok) {
    CIM_GUI(); MKM(h, popup);
    HBRUSH sb = (HBRUSH)U(GetStockObject)(WHITE_BRUSH);
    MENUINFO s = mi_new(MIM_BACKGROUND); s.hbrBack = sb;
    CHECK(U(SetMenuInfo)(h, &s));
    MENUINFO g = mi_new(MIM_BACKGROUND);
    CHECK(U(GetMenuInfo)(h, &g));
    EXPECT_EQ(g.hbrBack, sb);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_info_apply_to_submenus) {
    CIM_GUI();
    HMENU p = popup(), s = popup(), ss = popup();
    if (!p || !s || !ss) { t_skip("no menu"); return; }
    CHECK(U(AppendMenuW)(s, MF_POPUP | MF_STRING, (UINT_PTR)ss, L"ss"));
    CHECK(U(AppendMenuW)(p, MF_POPUP | MF_STRING, (UINT_PTR)s, L"s"));
    MENUINFO w = mi_new(MIM_MAXHEIGHT); w.cyMax = 40;
    CHECK(U(SetMenuInfo)(p, &w));
    MENUINFO g = mi_new(MIM_MAXHEIGHT);
    CHECK(U(GetMenuInfo)(s, &g)); EXPECT_EQ(g.cyMax, 0);     /* without MIM_APPLYTOSUBMENUS the submenu is untouched */
    w = mi_new(MIM_MAXHEIGHT | MIM_APPLYTOSUBMENUS); w.cyMax = 55;
    CHECK(U(SetMenuInfo)(p, &w));
    g = mi_new(MIM_MAXHEIGHT);
    CHECK(U(GetMenuInfo)(p, &g)); EXPECT_EQ(g.cyMax, 55);
    CHECK(U(GetMenuInfo)(s, &g)); EXPECT_EQ(g.cyMax, 55);   /* SetMenuInfo recurses over every item's submenu */
    CHECK(U(GetMenuInfo)(ss, &g)); EXPECT_EQ(g.cyMax, 55);
    U(DestroyMenu)(p);
}
TEST(cursor_icon_menu_info_apply_to_submenus_style) {
    CIM_GUI();
    HMENU p = popup(), s = popup();
    if (!p || !s) { t_skip("no menu"); return; }
    CHECK(U(AppendMenuW)(p, MF_POPUP | MF_STRING, (UINT_PTR)s, L"s"));
    MENUINFO w = mi_new(MIM_STYLE | MIM_APPLYTOSUBMENUS); w.dwStyle = MNS_CHECKORBMP;
    CHECK(U(SetMenuInfo)(p, &w));
    MENUINFO g = mi_new(MIM_STYLE);
    CHECK(U(GetMenuInfo)(s, &g));
    EXPECT_EQ(g.dwStyle, MNS_CHECKORBMP);
    U(DestroyMenu)(p);
}
TEST(cursor_icon_menu_info_nt_direct_roundtrip) {
    CIM_GUI(); REQUIRE(UN(NtUserThunkedMenuInfo)); MKM(h, popup);
    MENUINFO s = mi_new(MIM_MAXHEIGHT | MIM_HELPID | MIM_MENUDATA | MIM_STYLE);
    s.cyMax = 11; s.dwContextHelpID = 22; s.dwMenuData = 33; s.dwStyle = MNS_NOTIFYBYPOS;
    EXPECT_EQ(UN(NtUserThunkedMenuInfo)(h, &s), 1);
    MENUINFO g = mi_new(MIM_MAXHEIGHT | MIM_HELPID | MIM_MENUDATA | MIM_STYLE);
    CHECK(U(GetMenuInfo)(h, &g));
    EXPECT_EQ(g.cyMax, 11); EXPECT_EQ(g.dwContextHelpID, 22); EXPECT_EQ(g.dwMenuData, 33); EXPECT_EQ(g.dwStyle, MNS_NOTIFYBYPOS);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_info_nt_direct_skips_user32_validation) {
    CIM_GUI(); REQUIRE(UN(NtUserThunkedMenuInfo)); MKM(h, popup);
    /* the kernel copies the 0x28-byte struct and applies fMask bits it knows; unknown bits are ignored */
    MENUINFO s = mi_new(MIM_MAXHEIGHT | 0x40000000); s.cyMax = 31;
    EXPECT_EQ(UN(NtUserThunkedMenuInfo)(h, &s), 1);
    MENUINFO g = mi_new(MIM_MAXHEIGHT);
    CHECK(U(GetMenuInfo)(h, &g));
    EXPECT_EQ(g.cyMax, 31);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_info_destroyed_menu_rejected) {
    CIM_GUI(); REQUIRE(UN(NtUserThunkedMenuInfo));
    HMENU h = popup(); if (!h) { t_skip("no menu"); return; }
    U(DestroyMenu)(h);
    MENUINFO s = mi_new(MIM_MAXHEIGHT); s.cyMax = 1;
    EXPECT_EQ(UN(NtUserThunkedMenuInfo)(h, &s), 0);
    MENUINFO g = mi_new(MIM_MAXHEIGHT);
    BOOL r = U(GetMenuInfo)(h, &g);
    EXPECT_EQ(r, 0); /* user32!GetMenuInfo: HMValidateHandle(,2) fails client side */
}

/* ---------------- SetMenu / GetMenu / destruction with the window ---------------- */
TEST(cursor_icon_menu_window_setmenu_getmenu_roundtrip) {
    CIM_GUI();
    HWND w = cim_make_top(); if (!w) { t_skip("no window"); return; }
    MKM(m, bar);
    CHECK(U(SetMenu)(w, m));
    EXPECT_EQ(U(GetMenu)(w), m);
    CHECK(U(SetMenu)(w, NULL));
    EXPECT_EQ(U(GetMenu)(w), NULL);
    CHECK(U(IsMenu)(m)); /* detaching does not destroy */
    U(DestroyMenu)(m);
    U(DestroyWindow)(w);
}
TEST(cursor_icon_menu_window_menu_destroyed_with_window) {
    CIM_GUI();
    DWORD before = cim_gr(GR_USEROBJECTS);
    HWND w = cim_make_top(); if (!w) { t_skip("no window"); return; }
    MKM(m, bar);
    CHECK(U(AppendMenuW)(m, MF_STRING, 1, L"x"));
    CHECK(U(SetMenu)(w, m));
    DWORD grown = cim_gr(GR_USEROBJECTS);
    CHECK(grown >= before + 2);  /* window + menu (Windows 10 also creates the thread's default IME window with the first window) */
    CHECK(U(DestroyWindow)(w));
    CHECK(!U(IsMenu)(m));        /* destroying the window destroys its menu */
    EXPECT_EQ(cim_gr(GR_USEROBJECTS), before); /* window, its menu and everything created with it are gone */
    if (cim_ready()) CHECK(!cim_entry_is(m, CT_MENU));
}
TEST(cursor_icon_menu_window_submenus_destroyed_with_window) {
    CIM_GUI();
    HWND w = cim_make_top(); if (!w) { t_skip("no window"); return; }
    HMENU m = bar(), s = popup();
    if (!m || !s) { U(DestroyWindow)(w); t_skip("no menu"); return; }
    CHECK(U(AppendMenuW)(s, MF_STRING, 1, L"x"));
    CHECK(U(AppendMenuW)(m, MF_POPUP | MF_STRING, (UINT_PTR)s, L"file"));
    CHECK(U(SetMenu)(w, m));
    U(DestroyWindow)(w);
    CHECK(!U(IsMenu)(m)); CHECK(!U(IsMenu)(s));
}
TEST(cursor_icon_menu_window_detached_menu_survives_window) {
    CIM_GUI();
    HWND w = cim_make_top(); if (!w) { t_skip("no window"); return; }
    MKM(m, bar);
    CHECK(U(SetMenu)(w, m));
    CHECK(U(SetMenu)(w, NULL));
    U(DestroyWindow)(w);
    CHECK(U(IsMenu)(m));
    CHECK(U(DestroyMenu)(m));
}
TEST(cursor_icon_menu_window_replace_menu_keeps_old_alive) {
    CIM_GUI();
    HWND w = cim_make_top(); if (!w) { t_skip("no window"); return; }
    MKM(a, bar); MKM(b, bar);
    CHECK(U(SetMenu)(w, a));
    CHECK(U(SetMenu)(w, b));
    EXPECT_EQ(U(GetMenu)(w), b);
    CHECK(U(IsMenu)(a));
    U(DestroyWindow)(w);
    CHECK(!U(IsMenu)(b));
    CHECK(U(IsMenu)(a));
    U(DestroyMenu)(a);
}
TEST(cursor_icon_menu_window_setmenu_popup_menu_rejected) {
    CIM_GUI();
    HWND w = cim_make_top(); if (!w) { t_skip("no window"); return; }
    MKM(p, popup);
    BOOL r; CIM_CALL_ERR(r, U(SetMenu)(w, p));
    EXPECT_FAIL(r == 0, E_INVALID_PARAMETER); /* NtUserSetMenu: menu flag bit0 (popup) set -> 0x57 */
    EXPECT_EQ(U(GetMenu)(w), NULL);
    U(DestroyMenu)(p);
    U(DestroyWindow)(w);
}
TEST(cursor_icon_menu_window_setmenu_bar_used_as_submenu_rejected) {
    CIM_GUI();
    HWND w = cim_make_top(); if (!w) { t_skip("no window"); return; }
    MKM(par, popup); MKM(b, bar);
    CHECK(U(AppendMenuW)(par, MF_POPUP | MF_STRING, (UINT_PTR)b, L"b")); /* the bar menu gets the popup bit */
    BOOL r; CIM_CALL_ERR(r, U(SetMenu)(w, b));
    EXPECT_FAIL(r == 0, E_INVALID_PARAMETER);
    U(DestroyMenu)(par);
    U(DestroyWindow)(w);
}
TEST(cursor_icon_menu_window_setmenu_child_window_error) {
    CIM_GUI();
    HWND top = cim_make_top(); if (!top) { t_skip("no window"); return; }
    HWND c = cim_make_child(top);
    if (!c) { U(DestroyWindow)(top); t_skip("no child"); return; }
    MKM(m, bar);
    BOOL r; CIM_CALL_ERR(r, U(SetMenu)(c, m));
    EXPECT_FAIL(r == 0, E_CHILD_WINDOW_MENU); /* SetMenu: WS_CHILD without WS_POPUP -> 0x59c */
    CHECK(U(IsMenu)(m));
    U(DestroyMenu)(m);
    U(DestroyWindow)(top);
}
TEST(cursor_icon_menu_window_setmenu_invalid_window) {
    CIM_GUI(); REQUIRE(UN(NtUserSetMenu));
    MKM(m, bar);
    BOOL r;
    CIM_CALL_ERR(r, UN(NtUserSetMenu)(NULL, m, TRUE));
    EXPECT_FAIL(r == 0, E_INVALID_WINDOW_HANDLE);
    HWND dead = cim_make_top(); if (dead) {
        U(DestroyWindow)(dead);
        CIM_CALL_ERR(r, UN(NtUserSetMenu)(dead, m, TRUE));
        EXPECT_FAIL(r == 0, E_INVALID_WINDOW_HANDLE);
    }
    CHECK(U(IsMenu)(m));
    U(DestroyMenu)(m);
}
TEST(cursor_icon_menu_window_nt_setmenu_matches_userland) {
    CIM_GUI(); REQUIRE(UN(NtUserSetMenu));
    HWND w = cim_make_top(); if (!w) { t_skip("no window"); return; }
    MKM(m, bar);
    EXPECT_EQ(UN(NtUserSetMenu)(w, m, FALSE), 1);
    EXPECT_EQ(U(GetMenu)(w), m);
    EXPECT_EQ(UN(NtUserSetMenu)(w, NULL, FALSE), 1);
    EXPECT_EQ(U(GetMenu)(w), NULL);
    U(DestroyMenu)(m);
    U(DestroyWindow)(w);
}
TEST(cursor_icon_menu_window_menu_count_unchanged_by_setmenu) {
    CIM_GUI();
    HWND w = cim_make_top(); if (!w) { t_skip("no window"); return; }
    MKM(m, bar);
    DWORD base = cim_gr(GR_USEROBJECTS);
    CHECK(U(SetMenu)(w, m));
    EXPECT_EQ(cim_gr(GR_USEROBJECTS), base);
    CHECK(U(SetMenu)(w, NULL));
    EXPECT_EQ(cim_gr(GR_USEROBJECTS), base);
    U(DestroyMenu)(m);
    U(DestroyWindow)(w);
}
TEST(cursor_icon_menu_window_menu_shared_by_two_windows) {
    CIM_GUI();
    HWND a = cim_make_top(), b = cim_make_top();
    if (!a || !b) { if (a) U(DestroyWindow)(a); if (b) U(DestroyWindow)(b); t_skip("no window"); return; }
    MKM(m, bar);
    BOOL ra = U(SetMenu)(a, m);
    BOOL rb = U(SetMenu)(b, m);
    CHECK(ra);
    t_note("second SetMenu of the same menu on another window returned %d", rb);
    U(DestroyWindow)(a);
    t_note("menu alive after first owner destroyed: %d", U(IsMenu)(m));
    U(DestroyWindow)(b);
    CHECK(!U(IsMenu)(m));
}
TEST(cursor_icon_menu_window_getmenu_child_returns_control_id) {
    CIM_GUI();
    HWND top = cim_make_top(); if (!top) { t_skip("no window"); return; }
    HWND c = U(CreateWindowExW)(0, cim_class(), L"c", WS_CHILD, 0, 0, 1, 1, top, (HMENU)(ULONG_PTR)0x4321, GetModuleHandleW(NULL), NULL);
    if (!c) { U(DestroyWindow)(top); t_skip("no child"); return; }
    /* user32!GetMenu: for WS_CHILD windows the "menu" slot of tagWND is the control id */
    EXPECT_EQ(U(GetMenu)(c), (HMENU)(ULONG_PTR)0x4321);
    U(DestroyWindow)(top);
}
TEST(cursor_icon_menu_window_getmenu_invalid_window_null) {
    CIM_GUI();
    EXPECT_EQ(U(GetMenu)(NULL), NULL);
    EXPECT_EQ(U(GetMenu)((HWND)(ULONG_PTR)0x7ffe0), NULL);
}
TEST(cursor_icon_menu_window_menubarinfo_invalid_window) {
    CIM_GUI(); REQUIRE(UN(NtUserGetMenuBarInfo));
    MENUBARINFO mbi; memset(&mbi, 0, sizeof mbi); mbi.cbSize = sizeof mbi;
    BOOL r; CIM_CALL_ERR(r, UN(NtUserGetMenuBarInfo)(NULL, OBJID_MENU, 0, &mbi));
    EXPECT_FAIL(r == 0, E_INVALID_WINDOW_HANDLE);
    CIM_CALL_ERR(r, U(GetMenuBarInfo)((HWND)(ULONG_PTR)0x7ffe0, OBJID_MENU, 0, &mbi));
    EXPECT_FAIL(r == 0, E_INVALID_WINDOW_HANDLE);
}
TEST(cursor_icon_menu_window_menubarinfo_no_menu_fails) {
    CIM_GUI();
    HWND w = cim_make_top(); if (!w) { t_skip("no window"); return; }
    MENUBARINFO mbi; memset(&mbi, 0, sizeof mbi); mbi.cbSize = sizeof mbi;
    BOOL r = U(GetMenuBarInfo)(w, OBJID_MENU, 0, &mbi);
    EXPECT_EQ(r, 0);
    U(DestroyWindow)(w);
}
TEST(cursor_icon_menu_window_menubarinfo_with_menu) {
    CIM_GUI();
    HWND w = cim_make_top(); if (!w) { t_skip("no window"); return; }
    MKM(m, bar);
    CHECK(U(AppendMenuW)(m, MF_STRING, 1, L"File"));
    CHECK(U(SetMenu)(w, m));
    MENUBARINFO mbi; memset(&mbi, 0, sizeof mbi); mbi.cbSize = sizeof mbi;
    BOOL r = U(GetMenuBarInfo)(w, OBJID_MENU, 0, &mbi);
    if (r) {
        EXPECT_EQ(mbi.hMenu, m);
        EXPECT_EQ(mbi.fBarFocused, 0);
        EXPECT_EQ(mbi.fFocused, 0);
    } else t_note("GetMenuBarInfo failed err %lu on a hidden popup window", GetLastError());
    U(DestroyWindow)(w);
}
TEST(cursor_icon_menu_window_menubarinfo_bad_object_id_fails) {
    CIM_GUI();
    HWND w = cim_make_top(); if (!w) { t_skip("no window"); return; }
    MKM(m, bar);
    CHECK(U(AppendMenuW)(m, MF_STRING, 1, L"File"));
    CHECK(U(SetMenu)(w, m));
    MENUBARINFO mbi; memset(&mbi, 0, sizeof mbi); mbi.cbSize = sizeof mbi;
    BOOL r = U(GetMenuBarInfo)(w, OBJID_CLIENT + 100, 0, &mbi);
    t_note("GetMenuBarInfo(bogus object id) -> %d err %lu", r, GetLastError());
    U(DestroyWindow)(w);
}
TEST(cursor_icon_menu_window_menu_item_rect_requires_window_menu) {
    CIM_GUI();
    HWND w = cim_make_top(); if (!w) { t_skip("no window"); return; }
    MKM(m, bar);
    CHECK(U(AppendMenuW)(m, MF_STRING, 1, L"File"));
    RECT rc = {0};
    BOOL r = U(GetMenuItemRect)(w, m, 0, &rc);
    t_note("GetMenuItemRect on a detached menu -> %d", r);
    CHECK(U(SetMenu)(w, m));
    r = U(GetMenuItemRect)(w, m, 0, &rc);
    if (r) CHECK(rc.right >= rc.left && rc.bottom >= rc.top);
    BOOL r2 = U(GetMenuItemRect)(w, m, 5, &rc);
    EXPECT_EQ(r2, 0); /* position beyond cItems */
    U(DestroyWindow)(w);
}
TEST(cursor_icon_menu_window_hilite_menu_item_no_crash_flags_only) {
    CIM_GUI(); REQUIRE(UN(NtUserHiliteMenuItem));
    HWND w = cim_make_top(); if (!w) { t_skip("no window"); return; }
    MKM(m, bar);
    CHECK(U(AppendMenuW)(m, MF_STRING, 1, L"File"));
    CHECK(U(SetMenu)(w, m));
    BOOL r = UN(NtUserHiliteMenuItem)(w, m, 0, MF_BYPOSITION | MF_HILITE);
    t_note("HiliteMenuItem(by pos, hilite) -> %d", r);
    r = UN(NtUserHiliteMenuItem)(w, m, 0, MF_BYPOSITION | MF_UNHILITE);
    t_note("HiliteMenuItem(by pos, unhilite) -> %d", r);
    U(DestroyWindow)(w);
}
