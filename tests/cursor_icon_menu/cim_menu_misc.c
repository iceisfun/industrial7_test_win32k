/* Smaller menu syscalls: context help id, parent/submenu index lookup, MenuItemFromPoint argument
 * validation, per-window system menu. */
#include "cim.h"

static HMENU popup(void) { return U(CreatePopupMenu)(); }

TEST(cursor_icon_menu_misc_contexthelp_roundtrip) {
    CIM_GUI();
    HMENU h = popup(); if (!h) { t_skip("no menu"); return; }
    EXPECT_EQ(U(GetMenuContextHelpId)(h), 0);
    CHECK(U(SetMenuContextHelpId)(h, 0xABCD1234));
    EXPECT_EQ(U(GetMenuContextHelpId)(h), 0xABCD1234);
    struct cim_menu m;
    if (cim_menu_read(h, &m)) EXPECT_EQ(m.helpid, 0xABCD1234); /* NtUserSetMenuContextHelpId: *(shared+0x30) = id */
    MENUINFO mi; memset(&mi, 0, sizeof mi); mi.cbSize = sizeof mi; mi.fMask = MIM_HELPID;
    CHECK(U(GetMenuInfo)(h, &mi));
    EXPECT_EQ(mi.dwContextHelpID, 0xABCD1234);       /* same storage as MENUINFO.dwContextHelpID */
    CHECK(U(SetMenuContextHelpId)(h, 0));
    EXPECT_EQ(U(GetMenuContextHelpId)(h), 0);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_misc_contexthelp_unaffected_by_items) {
    CIM_GUI();
    HMENU h = popup(); if (!h) { t_skip("no menu"); return; }
    CHECK(U(SetMenuContextHelpId)(h, 77));
    for (int i = 0; i < 10; i++) CHECK(U(AppendMenuW)(h, MF_STRING, 10 + i, L"x"));
    CHECK(U(DeleteMenu)(h, 10, MF_BYCOMMAND));
    EXPECT_EQ(U(GetMenuContextHelpId)(h), 77);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_misc_contexthelp_is_per_menu) {
    CIM_GUI();
    HMENU a = popup(), b = popup();
    if (!a || !b) { t_skip("no menu"); return; }
    CHECK(U(SetMenuContextHelpId)(a, 1)); CHECK(U(SetMenuContextHelpId)(b, 2));
    EXPECT_EQ(U(GetMenuContextHelpId)(a), 1); EXPECT_EQ(U(GetMenuContextHelpId)(b), 2);
    U(DestroyMenu)(a); U(DestroyMenu)(b);
}
TEST(cursor_icon_menu_misc_contexthelp_invalid_menu) {
    CIM_GUI(); REQUIRE(UN(NtUserSetMenuContextHelpId));
    HMENU h = popup(); if (!h) { t_skip("no menu"); return; }
    U(DestroyMenu)(h);
    BOOL r; CIM_CALL_ERR(r, UN(NtUserSetMenuContextHelpId)(h, 5));
    EXPECT_FAIL(r == 0, E_INVALID_MENU_HANDLE);
    CIM_CALL_ERR(r, UN(NtUserSetMenuContextHelpId)(NULL, 5));
    EXPECT_FAIL(r == 0, E_INVALID_MENU_HANDLE);
    EXPECT_EQ(U(GetMenuContextHelpId)(h), 0);
    HWND w = cim_make_top();
    if (w) { CIM_CALL_ERR(r, UN(NtUserSetMenuContextHelpId)((HMENU)w, 5)); EXPECT_FAIL(r == 0, E_INVALID_MENU_HANDLE); U(DestroyWindow)(w); }
}
TEST(cursor_icon_menu_misc_getmenuindex_finds_submenu_slot) {
    CIM_GUI(); REQUIRE(UN(NtUserGetMenuIndex));
    if (t_is_wine()) { t_skip("NtUserGetMenuIndex is unimplemented in Wine win32u (process abort)"); return; }
    HMENU p = popup(), s1 = popup(), s2 = popup();
    if (!p || !s1 || !s2) { t_skip("no menu"); return; }
    CHECK(U(AppendMenuW)(p, MF_STRING, 1, L"plain"));
    CHECK(U(AppendMenuW)(p, MF_POPUP | MF_STRING, (UINT_PTR)s1, L"s1"));
    CHECK(U(AppendMenuW)(p, MF_POPUP | MF_STRING, (UINT_PTR)s2, L"s2"));
    EXPECT_EQ(UN(NtUserGetMenuIndex)(p, s1), 1);
    EXPECT_EQ(UN(NtUserGetMenuIndex)(p, s2), 2);
    CHECK(U(RemoveMenu)(p, 0, MF_BYPOSITION));
    EXPECT_EQ(UN(NtUserGetMenuIndex)(p, s1), 0);   /* indexes shift with the item array */
    EXPECT_EQ(UN(NtUserGetMenuIndex)(p, s2), 1);
    U(DestroyMenu)(p);
}
TEST(cursor_icon_menu_misc_getmenuindex_not_found_minus1) {
    CIM_GUI(); REQUIRE(UN(NtUserGetMenuIndex));
    if (t_is_wine()) { t_skip("NtUserGetMenuIndex is unimplemented in Wine win32u (process abort)"); return; }
    HMENU p = popup(), s = popup();
    if (!p || !s) { t_skip("no menu"); return; }
    CHECK(U(AppendMenuW)(p, MF_STRING, 1, L"plain"));
    EXPECT_EQ(UN(NtUserGetMenuIndex)(p, s), 0xffffffff);
    EXPECT_EQ(UN(NtUserGetMenuIndex)(s, p), 0xffffffff);   /* empty menu: the loop never runs */
    EXPECT_EQ(UN(NtUserGetMenuIndex)(p, p), 0xffffffff);
    U(DestroyMenu)(p); U(DestroyMenu)(s);
}
TEST(cursor_icon_menu_misc_getmenuindex_invalid_handles_zero) {
    CIM_GUI(); REQUIRE(UN(NtUserGetMenuIndex));
    if (t_is_wine()) { t_skip("NtUserGetMenuIndex is unimplemented in Wine win32u (process abort)"); return; }
    HMENU p = popup(), dead = popup();
    if (!p || !dead) { t_skip("no menu"); return; }
    U(DestroyMenu)(dead);
    EXPECT_EQ(UN(NtUserGetMenuIndex)(p, dead), 0);   /* either handle failing ValidateHmenu yields 0, not -1 */
    EXPECT_EQ(UN(NtUserGetMenuIndex)(dead, p), 0);
    EXPECT_EQ(UN(NtUserGetMenuIndex)(NULL, p), 0);
    U(DestroyMenu)(p);
}
TEST(cursor_icon_menu_misc_menuitemfrompoint_invalid_menu) {
    CIM_GUI();
    HMENU dead = popup(); if (!dead) { t_skip("no menu"); return; }
    U(DestroyMenu)(dead);
    POINT pt = { 0, 0 };
    EXPECT_EQ(U(MenuItemFromPoint)(NULL, dead, pt), -1);
    EXPECT_EQ(U(MenuItemFromPoint)(NULL, NULL, pt), -1);
}
TEST(cursor_icon_menu_misc_menuitemfrompoint_invalid_window) {
    CIM_GUI();
    HMENU m = popup(); if (!m) { t_skip("no menu"); return; }
    HWND w = cim_make_top(); if (!w) { U(DestroyMenu)(m); t_skip("no window"); return; }
    U(DestroyWindow)(w);
    POINT pt = { 0, 0 };
    EXPECT_EQ(U(MenuItemFromPoint)(w, m, pt), -1);   /* ValidateHwnd failed */
    U(DestroyMenu)(m);
}
TEST(cursor_icon_menu_misc_menuitemfrompoint_empty_menu_no_item) {
    CIM_GUI();
    HMENU m = popup(); if (!m) { t_skip("no menu"); return; }
    POINT pt = { 5, 5 };
    int r = U(MenuItemFromPoint)(NULL, m, pt);
    t_note("MenuItemFromPoint(NULL, empty popup, 5,5) -> %d", r);
    CHECK(r == -1);
    U(DestroyMenu)(m);
}

static HWND sysmenu_window(void) {
    return U(CreateWindowExW)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cim_class(), L"sys", WS_OVERLAPPEDWINDOW, -32000, -32000, 120, 120, NULL, NULL, GetModuleHandleW(NULL), NULL);
}
TEST(cursor_icon_menu_misc_getsystemmenu_returns_menu_with_close) {
    CIM_GUI();
    HWND w = sysmenu_window(); if (!w) { t_skip("no window"); return; }
    HMENU s = U(GetSystemMenu)(w, FALSE);
    CHECK(s != NULL);
    if (s) {
        CHECK(U(IsMenu)(s));
        CHECK(U(GetMenuItemCount)(s) > 0);
        CHECK(U(GetMenuState)(s, 0xF060 /* SC_CLOSE */, MF_BYCOMMAND) != (UINT)-1);
        CHECK(U(GetMenuState)(s, 0xF010 /* SC_MOVE */, MF_BYCOMMAND) != (UINT)-1);
        if (cim_ready()) CHECK(cim_entry_is(s, CT_MENU));
    }
    U(DestroyWindow)(w);
}
TEST(cursor_icon_menu_misc_getsystemmenu_same_handle_twice) {
    CIM_GUI();
    HWND w = sysmenu_window(); if (!w) { t_skip("no window"); return; }
    HMENU a = U(GetSystemMenu)(w, FALSE), b = U(GetSystemMenu)(w, FALSE);
    CHECK(a != NULL);
    EXPECT_EQ(a, b);
    U(DestroyWindow)(w);
}
TEST(cursor_icon_menu_misc_getsystemmenu_per_window_copy) {
    CIM_GUI();
    HWND w1 = sysmenu_window(), w2 = sysmenu_window();
    if (!w1 || !w2) { if (w1) U(DestroyWindow)(w1); if (w2) U(DestroyWindow)(w2); t_skip("no window"); return; }
    HMENU a = U(GetSystemMenu)(w1, FALSE), b = U(GetSystemMenu)(w2, FALSE);
    CHECK(a && b);
    CHECK(a != b);       /* GetSystemMenu(FALSE) gives each window its own modifiable copy */
    U(DestroyWindow)(w1); U(DestroyWindow)(w2);
}
TEST(cursor_icon_menu_misc_getsystemmenu_modifiable_copy) {
    CIM_GUI();
    HWND w = sysmenu_window(); if (!w) { t_skip("no window"); return; }
    HMENU s = U(GetSystemMenu)(w, FALSE);
    if (!s) { U(DestroyWindow)(w); t_skip("no system menu"); return; }
    int n = U(GetMenuItemCount)(s);
    CHECK(U(AppendMenuW)(s, MF_STRING, 0x1234, L"extra"));
    EXPECT_EQ(U(GetMenuItemCount)(s), n + 1);
    HWND w2 = sysmenu_window();
    if (w2) {
        HMENU s2 = U(GetSystemMenu)(w2, FALSE);
        if (s2) EXPECT_EQ(U(GetMenuState)(s2, 0x1234, MF_BYCOMMAND), (UINT)-1); /* the other window's copy is untouched */
        U(DestroyWindow)(w2);
    }
    U(DestroyWindow)(w);
}
TEST(cursor_icon_menu_misc_getsystemmenu_revert_returns_null) {
    CIM_GUI();
    HWND w = sysmenu_window(); if (!w) { t_skip("no window"); return; }
    HMENU s = U(GetSystemMenu)(w, FALSE);
    HMENU r = U(GetSystemMenu)(w, TRUE);
    EXPECT_EQ(r, NULL);
    if (s) t_note("system menu %p after revert: IsMenu=%d", s, U(IsMenu)(s));
    HMENU again = U(GetSystemMenu)(w, FALSE);
    CHECK(again != NULL);
    U(DestroyWindow)(w);
}
TEST(cursor_icon_menu_misc_getsystemmenu_destroyed_with_window) {
    CIM_GUI();
    HWND w = sysmenu_window(); if (!w) { t_skip("no window"); return; }
    HMENU s = U(GetSystemMenu)(w, FALSE);
    if (!s) { U(DestroyWindow)(w); t_skip("no system menu"); return; }
    DWORD base = cim_gr(GR_USEROBJECTS);
    U(DestroyWindow)(w);
    CHECK(!U(IsMenu)(s));      /* window destruction: the window's own system menu is destroyed with it */
    CHECK(cim_gr(GR_USEROBJECTS) < base);
}
TEST(cursor_icon_menu_misc_getsystemmenu_invalid_window_null) {
    CIM_GUI(); REQUIRE(UN(NtUserGetSystemMenu));
    EXPECT_EQ(UN(NtUserGetSystemMenu)(NULL, FALSE), NULL);
    HWND w = sysmenu_window(); if (!w) { t_skip("no window"); return; }
    U(DestroyWindow)(w);
    EXPECT_EQ(UN(NtUserGetSystemMenu)(w, FALSE), NULL);
}
TEST(cursor_icon_menu_misc_getsystemmenu_no_sysmenu_style_null) {
    CIM_GUI();
    HWND w = cim_make_top(); if (!w) { t_skip("no window"); return; }
    EXPECT_EQ(U(GetSystemMenu)(w, FALSE), NULL);  /* GetSystemMenu requires WS_SYSMENU */
    U(DestroyWindow)(w);
}
TEST(cursor_icon_menu_misc_menu_count_stress_create_destroy) {
    CIM_GUI();
    DWORD base = cim_gr(GR_USEROBJECTS);
    for (int round = 0; round < 20; round++) {
        HMENU hs[50]; int n = 0;
        for (; n < 50; n++) { hs[n] = (n & 1) ? popup() : U(CreateMenu)(); if (!hs[n]) break; }
        EXPECT_EQ(n, 50);
        for (int i = 0; i < n; i++) U(AppendMenuW)(hs[i], MF_STRING, i, L"x");
        for (int i = 0; i < n; i++) CHECK(U(DestroyMenu)(hs[i]));
    }
    EXPECT_EQ(cim_gr(GR_USEROBJECTS), base);
}
TEST(cursor_icon_menu_misc_bitmap_item_owner_draw_data_roundtrip) {
    CIM_GUI();
    HMENU h = popup(); if (!h) { t_skip("no menu"); return; }
    CHECK(U(AppendMenuW)(h, MF_OWNERDRAW, 8, (LPCWSTR)(ULONG_PTR)0x0123456789ABCDEFull));
    MENUITEMINFOW g; memset(&g, 0, sizeof g); g.cbSize = sizeof g; g.fMask = MIIM_DATA | MIIM_FTYPE | MIIM_ID;
    CHECK(U(GetMenuItemInfoW)(h, 0, TRUE, &g));
    EXPECT_EQ(g.dwItemData, 0x0123456789ABCDEFull);  /* owner-draw items keep the 64-bit item data verbatim */
    CHECK(g.fType & MFT_OWNERDRAW);
    EXPECT_EQ(g.wID, 8);
    U(DestroyMenu)(h);
}
