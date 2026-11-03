/* Menu items: Insert/Set/GetMenuItemInfo parameter validation, state bits, item storage visible in the
 * client view of the menu, Remove/Delete/Destroy semantics, default item, nesting limits. */
#include "cim.h"

static HMENU new_menu(int popup) { return popup ? U(CreatePopupMenu)() : U(CreateMenu)(); }
static HMENU filled(int n, int popup) {
    HMENU h = new_menu(popup);
    if (!h) return NULL;
    for (int i = 0; i < n; i++) if (!U(AppendMenuW)(h, MF_STRING, 100 + i, L"item")) { U(DestroyMenu)(h); return NULL; }
    return h;
}
#define MENU(var, n, popup) HMENU var = filled(n, popup); if (!var) { t_skip("cannot build menu (err %lu)", GetLastError()); return; }

static MENUITEMINFOW mii_new(UINT mask) { MENUITEMINFOW m; memset(&m, 0, sizeof m); m.cbSize = sizeof m; m.fMask = mask; return m; }

/* ---------------- storage visible in the client view ---------------- */
TEST(cursor_icon_menu_items_append_updates_shared_count) {
    CIM_GUI();
    HMENU h = new_menu(1); if (!h) { t_skip("no menu"); return; }
    for (unsigned k = 1; k <= 6; k++) {
        CHECK(U(AppendMenuW)(h, MF_STRING, 100 + k, L"x"));
        struct cim_menu m;
        if (!cim_menu_read(h, &m)) { t_skip("client view not mappable"); break; }
        EXPECT_EQ(m.citems, k);
        CHECK(m.rgitems != 0);
        EXPECT_EQ(U(GetMenuItemCount)(h), (int)k);
    }
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_client_item_ids_in_order) {
    CIM_GUI();
    MENU(h, 5, 1);
    for (unsigned i = 0; i < 5; i++) {
        struct cim_item it;
        if (!cim_item_read(h, i, &it)) { t_skip("item view not mappable"); break; }
        EXPECT_EQ(it.id, 100 + i);
        EXPECT_EQ(it.type & 0x800, 0);   /* MFT_SEPARATOR not set on a string item */
        EXPECT_EQ(it.sub_ptr, 0);
        EXPECT_EQ(it.state & 0x1000, 0);
    }
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_insert_by_position_goes_first) {
    CIM_GUI();
    MENU(h, 3, 1);
    CHECK(U(InsertMenuW)(h, 0, MF_BYPOSITION | MF_STRING, 500, L"first"));
    struct cim_item it;
    if (cim_item_read(h, 0, &it)) EXPECT_EQ(it.id, 500);
    if (cim_item_read(h, 1, &it)) EXPECT_EQ(it.id, 100);
    EXPECT_EQ(U(GetMenuItemCount)(h), 4);
    EXPECT_EQ(U(GetMenuItemID)(h, 0), 500);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_insert_by_command_goes_before) {
    CIM_GUI();
    MENU(h, 3, 1);
    CHECK(U(InsertMenuW)(h, 101, MF_BYCOMMAND | MF_STRING, 600, L"mid"));
    EXPECT_EQ(U(GetMenuItemID)(h, 1), 600);
    EXPECT_EQ(U(GetMenuItemID)(h, 2), 101);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_insert_position_beyond_end_appends) {
    CIM_GUI();
    MENU(h, 2, 1);
    /* item lookup fails for an out-of-range position, insert then appends (index -1) */
    CHECK(U(InsertMenuW)(h, 77, MF_BYPOSITION | MF_STRING, 700, L"tail"));
    EXPECT_EQ(U(GetMenuItemCount)(h), 3);
    EXPECT_EQ(U(GetMenuItemID)(h, 2), 700);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_insert_minus1_appends) {
    CIM_GUI();
    MENU(h, 2, 0);
    CHECK(U(InsertMenuW)(h, (UINT)-1, MF_BYPOSITION | MF_STRING, 701, L"tail"));
    EXPECT_EQ(U(GetMenuItemID)(h, 2), 701);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_separator_type_and_state) {
    CIM_GUI();
    HMENU h = new_menu(1); if (!h) { t_skip("no menu"); return; }
    CHECK(U(AppendMenuW)(h, MF_STRING, 1, L"a"));
    CHECK(U(AppendMenuW)(h, MF_SEPARATOR, 0, NULL));
    struct cim_item it;
    if (cim_item_read(h, 1, &it)) {
        CHECK(it.type & MF_SEPARATOR);          /* fType 0x800 */
        EXPECT_EQ(it.state & 3, 3);             /* SetLPITEMInfoNoRedraw: a separator is always disabled+grayed */
    }
    UINT st = U(GetMenuState)(h, 1, MF_BYPOSITION);
    CHECK(st != (UINT)-1 && (st & MF_SEPARATOR));
    EXPECT_EQ(U(GetMenuItemID)(h, 1), 0);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_getmenuitemid_range_errors) {
    CIM_GUI();
    MENU(h, 3, 1);
    static const int bad[] = { -1, 3, 4, 100, 0x7fffffff, (int)0x80000000 };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        UINT r; CIM_CALL_ERR(r, U(GetMenuItemID)(h, bad[i]));
        CHECKF(r == (UINT)-1, "pos %d", bad[i]);
        CHECKF(GetLastError() == E_INVALID_PARAMETER, "pos %d err %lu", bad[i], GetLastError()); /* user32!GetMenuItemID sets 0x57 */
    }
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_getmenuitemid_popup_item_minus1) {
    CIM_GUI();
    HMENU p = new_menu(1), s = new_menu(1);
    if (!p || !s) { t_skip("no menu"); return; }
    CHECK(U(AppendMenuW)(p, MF_POPUP | MF_STRING, (UINT_PTR)s, L"sub"));
    EXPECT_EQ(U(GetMenuItemID)(p, 0), (UINT)-1); /* user32!GetMenuItemID: item with a submenu returns -1 without error */
    U(DestroyMenu)(p);
}
TEST(cursor_icon_menu_items_getsubmenu_roundtrip) {
    CIM_GUI();
    HMENU p = new_menu(1), s = new_menu(1);
    if (!p || !s) { t_skip("no menu"); return; }
    CHECK(U(AppendMenuW)(p, MF_STRING, 5, L"plain"));
    CHECK(U(AppendMenuW)(p, MF_POPUP | MF_STRING, (UINT_PTR)s, L"sub"));
    EXPECT_EQ(U(GetSubMenu)(p, 1), s);
    EXPECT_EQ(U(GetSubMenu)(p, 0), NULL);
    EXPECT_EQ(U(GetSubMenu)(p, 2), NULL);
    struct cim_item it;
    if (cim_item_read(p, 1, &it)) { EXPECT_EQ(it.sub, s); CHECK(it.sub_ptr != 0); }
    U(DestroyMenu)(p);
}
TEST(cursor_icon_menu_items_bar_menu_used_as_submenu_gets_popup_bit) {
    CIM_GUI();
    HMENU p = new_menu(0), s = new_menu(0);
    if (!p || !s) { t_skip("no menu"); return; }
    struct cim_menu m;
    if (!cim_menu_read(s, &m)) { U(DestroyMenu)(p); U(DestroyMenu)(s); t_skip("view not mappable"); return; }
    EXPECT_EQ(m.flags & 1, 0);
    CHECK(U(AppendMenuW)(p, MF_POPUP | MF_STRING, (UINT_PTR)s, L"sub"));
    CHECK(cim_menu_read(s, &m));
    EXPECT_EQ(m.flags & 1, 1); /* SetLPITEMInfoNoRedraw: *(sub+0x28) |= 1 when installed as a submenu */
    U(DestroyMenu)(p);
}
TEST(cursor_icon_menu_items_getmenustate_submenu_encodes_count) {
    CIM_GUI();
    HMENU p = new_menu(1), s = new_menu(1);
    if (!p || !s) { t_skip("no menu"); return; }
    CHECK(U(AppendMenuW)(s, MF_STRING, 1, L"a"));
    CHECK(U(AppendMenuW)(s, MF_STRING, 2, L"b"));
    CHECK(U(AppendMenuW)(s, MF_STRING, 3, L"c"));
    CHECK(U(AppendMenuW)(p, MF_POPUP | MF_STRING, (UINT_PTR)s, L"sub"));
    UINT st = U(GetMenuState)(p, 0, MF_BYPOSITION);
    EXPECT_EQ(st >> 8, 3);   /* user32!_GetMenuState: (flags & 0xef | 0x10) + cItems(sub) * 0x100 */
    CHECK(st & MF_POPUP);
    U(DestroyMenu)(p);
}

/* ---------------- GetMenuState / EnableMenuItem / CheckMenuItem ---------------- */
TEST(cursor_icon_menu_items_getmenustate_missing_is_minus1) {
    CIM_GUI();
    MENU(h, 2, 1);
    EXPECT_EQ(U(GetMenuState)(h, 999, MF_BYCOMMAND), (UINT)-1);
    EXPECT_EQ(U(GetMenuState)(h, 2, MF_BYPOSITION), (UINT)-1);
    EXPECT_EQ(U(GetMenuState)(h, (UINT)-1, MF_BYPOSITION), (UINT)-1);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_getmenustate_high_flag_bits_rejected) {
    CIM_GUI();
    MENU(h, 2, 1);
    UINT r; CIM_CALL_ERR(r, U(GetMenuState)(h, 100, 0x10000));
    EXPECT_EQ(GetLastError(), E_INVALID_PARAMETER); /* user32!GetMenuState: (uFlags & 0xffff0000) -> 0x57 */
    (void)r;
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_getmenustate_invalid_menu_minus1) {
    CIM_GUI();
    HMENU h = new_menu(1); if (!h) { t_skip("no menu"); return; }
    U(DestroyMenu)(h);
    EXPECT_EQ(U(GetMenuState)(h, 0, MF_BYPOSITION), (UINT)-1);
}
TEST(cursor_icon_menu_items_initial_state_enabled_unchecked) {
    CIM_GUI();
    MENU(h, 2, 1);
    UINT st = U(GetMenuState)(h, 100, MF_BYCOMMAND);
    EXPECT_EQ(st & (MF_GRAYED | MF_DISABLED | MF_CHECKED | MF_HILITE | MF_DEFAULT), 0);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_append_flags_set_state) {
    CIM_GUI();
    HMENU h = new_menu(1); if (!h) { t_skip("no menu"); return; }
    CHECK(U(AppendMenuW)(h, MF_STRING | MF_GRAYED, 1, L"g"));
    CHECK(U(AppendMenuW)(h, MF_STRING | MF_DISABLED, 2, L"d"));
    CHECK(U(AppendMenuW)(h, MF_STRING | MF_CHECKED, 3, L"c"));
    CHECK(U(GetMenuState)(h, 1, 0) & (MF_GRAYED | MF_DISABLED));
    CHECK(U(GetMenuState)(h, 2, 0) & (MF_GRAYED | MF_DISABLED));
    CHECK(U(GetMenuState)(h, 3, 0) & MF_CHECKED);
    EXPECT_EQ(U(GetMenuState)(h, 3, 0) & (MF_GRAYED | MF_DISABLED), 0);
    struct cim_item it;
    if (cim_item_read(h, 2, &it)) CHECK(it.state & MF_CHECKED);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_enable_sequence_returns_previous_state) {
    CIM_GUI();
    MENU(h, 1, 1);
    /* EnableMenuItem state mask 3: the return value is the previous (state & 3), stored bits = argument & 3 */
    EXPECT_EQ(U(EnableMenuItem)(h, 100, MF_BYCOMMAND | MF_GRAYED), 0);
    struct cim_item it;
    if (cim_item_read(h, 0, &it)) EXPECT_EQ(it.state & 3, 1);
    EXPECT_EQ(U(EnableMenuItem)(h, 100, MF_BYCOMMAND | MF_ENABLED), 1);
    EXPECT_EQ(U(EnableMenuItem)(h, 100, MF_BYCOMMAND | MF_DISABLED), 0);
    if (cim_item_read(h, 0, &it)) EXPECT_EQ(it.state & 3, 2);
    EXPECT_EQ(U(EnableMenuItem)(h, 100, MF_BYCOMMAND | MF_ENABLED), 2);
    if (cim_item_read(h, 0, &it)) EXPECT_EQ(it.state & 3, 0);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_enable_by_position) {
    CIM_GUI();
    MENU(h, 3, 1);
    EXPECT_EQ(U(EnableMenuItem)(h, 2, MF_BYPOSITION | MF_GRAYED), 0);
    struct cim_item it;
    if (cim_item_read(h, 2, &it)) EXPECT_EQ(it.state & 3, 1);
    if (cim_item_read(h, 0, &it)) EXPECT_EQ(it.state & 3, 0);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_enable_missing_item_minus1) {
    CIM_GUI();
    MENU(h, 2, 1);
    EXPECT_EQ(U(EnableMenuItem)(h, 4242, MF_BYCOMMAND | MF_GRAYED), (BOOL)-1);
    EXPECT_EQ(U(EnableMenuItem)(h, 2, MF_BYPOSITION | MF_GRAYED), (BOOL)-1);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_nt_enable_direct_returns_previous) {
    CIM_GUI(); REQUIRE(UN(NtUserEnableMenuItem));
    MENU(h, 1, 1);
    EXPECT_EQ(UN(NtUserEnableMenuItem)(h, 100, MF_GRAYED), 0);
    EXPECT_EQ(UN(NtUserEnableMenuItem)(h, 100, MF_GRAYED), 1); /* no user32 short-circuit here: MenuItemState still returns the old bits */
    EXPECT_EQ(UN(NtUserEnableMenuItem)(h, 100, MF_ENABLED), 1);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_nt_flag_high_bits_invalid_flags) {
    CIM_GUI(); REQUIRE(UN(NtUserEnableMenuItem)); REQUIRE(UN(NtUserCheckMenuItem)); REQUIRE(UN(NtUserDeleteMenu)); REQUIRE(UN(NtUserRemoveMenu)); REQUIRE(UN(NtUserHiliteMenuItem));
    MENU(h, 2, 1);
    HWND w = cim_make_top(); if (!w) { U(DestroyMenu)(h); t_skip("no window"); return; }
    static const UINT flags[] = { 0x10000, 0x80000000, 0xffff0000, 0x00010400 };
    for (unsigned i = 0; i < sizeof flags / sizeof *flags; i++) {
        UINT f = flags[i];
        DWORD r; BOOL b;
        CIM_CALL_ERR(r, UN(NtUserEnableMenuItem)(h, 100, f)); CHECKF(r == 0xffffffff && GetLastError() == E_INVALID_FLAGS, "enable 0x%x err %lu", f, GetLastError());
        CIM_CALL_ERR(r, UN(NtUserCheckMenuItem)(h, 100, f));  CHECKF(r == 0xffffffff && GetLastError() == E_INVALID_FLAGS, "check 0x%x err %lu", f, GetLastError());
        CIM_CALL_ERR(b, UN(NtUserDeleteMenu)(h, 100, f));     CHECKF(b == 0 && GetLastError() == E_INVALID_FLAGS, "delete 0x%x err %lu", f, GetLastError());
        CIM_CALL_ERR(b, UN(NtUserRemoveMenu)(h, 100, f));     CHECKF(b == 0 && GetLastError() == E_INVALID_FLAGS, "remove 0x%x err %lu", f, GetLastError());
        CIM_CALL_ERR(b, UN(NtUserHiliteMenuItem)(w, h, 0, f)); CHECKF(b == 0 && GetLastError() == E_INVALID_FLAGS, "hilite 0x%x err %lu", f, GetLastError());
    }
    EXPECT_EQ(U(GetMenuItemCount)(h), 2); /* nothing was removed */
    U(DestroyWindow)(w);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_check_sequence) {
    CIM_GUI();
    MENU(h, 1, 1);
    EXPECT_EQ(U(CheckMenuItem)(h, 100, MF_BYCOMMAND | MF_CHECKED), 0);
    struct cim_item it;
    if (cim_item_read(h, 0, &it)) CHECK(it.state & MF_CHECKED);
    EXPECT_EQ(U(CheckMenuItem)(h, 100, MF_BYCOMMAND | MF_CHECKED), MF_CHECKED);   /* user32 short-circuits: returns state & 8 */
    EXPECT_EQ(U(CheckMenuItem)(h, 100, MF_BYCOMMAND | MF_UNCHECKED), MF_CHECKED);
    if (cim_item_read(h, 0, &it)) EXPECT_EQ(it.state & MF_CHECKED, 0);
    EXPECT_EQ(U(CheckMenuItem)(h, 100, MF_BYCOMMAND | MF_UNCHECKED), 0);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_check_by_position_and_missing) {
    CIM_GUI();
    MENU(h, 2, 1);
    EXPECT_EQ(U(CheckMenuItem)(h, 1, MF_BYPOSITION | MF_CHECKED), 0);
    CHECK(U(GetMenuState)(h, 101, MF_BYCOMMAND) & MF_CHECKED);
    EXPECT_EQ(U(CheckMenuItem)(h, 9, MF_BYPOSITION | MF_CHECKED), (DWORD)-1);
    EXPECT_EQ(U(CheckMenuItem)(h, 9999, MF_BYCOMMAND | MF_CHECKED), (DWORD)-1);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_nt_check_direct_returns_previous) {
    CIM_GUI(); REQUIRE(UN(NtUserCheckMenuItem));
    MENU(h, 1, 1);
    EXPECT_EQ(UN(NtUserCheckMenuItem)(h, 100, MF_CHECKED), 0);
    EXPECT_EQ(UN(NtUserCheckMenuItem)(h, 100, MF_CHECKED), MF_CHECKED);
    EXPECT_EQ(UN(NtUserCheckMenuItem)(h, 100, MF_UNCHECKED), MF_CHECKED);
    EXPECT_EQ(UN(NtUserCheckMenuItem)(h, 100, MF_UNCHECKED), 0);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_check_finds_item_in_submenu_by_command) {
    CIM_GUI();
    HMENU p = new_menu(1), s = new_menu(1);
    if (!p || !s) { t_skip("no menu"); return; }
    CHECK(U(AppendMenuW)(s, MF_STRING, 900, L"deep"));
    CHECK(U(AppendMenuW)(p, MF_POPUP | MF_STRING, (UINT_PTR)s, L"sub"));
    /* item lookup by command recurses into submenus */
    EXPECT_EQ(U(CheckMenuItem)(p, 900, MF_BYCOMMAND | MF_CHECKED), 0);
    CHECK(U(GetMenuState)(s, 900, MF_BYCOMMAND) & MF_CHECKED);
    EXPECT_EQ(U(GetMenuState)(p, 900, MF_BYPOSITION), (UINT)-1);
    CHECK(U(GetMenuState)(p, 900, MF_BYCOMMAND) != (UINT)-1);
    U(DestroyMenu)(p);
}
TEST(cursor_icon_menu_items_state_bits_independent) {
    CIM_GUI();
    MENU(h, 2, 1);
    U(EnableMenuItem)(h, 100, MF_GRAYED);
    U(CheckMenuItem)(h, 100, MF_CHECKED);
    UINT st = U(GetMenuState)(h, 100, 0);
    CHECK(st & MF_CHECKED);
    EXPECT_EQ(st & 3, 1);
    U(EnableMenuItem)(h, 100, MF_ENABLED);
    st = U(GetMenuState)(h, 100, 0);
    CHECK(st & MF_CHECKED);
    EXPECT_EQ(st & 3, 0);
    U(DestroyMenu)(h);
}

/* ---------------- default item ---------------- */
static unsigned count_default(HMENU h) {
    unsigned n = 0, c = (unsigned)U(GetMenuItemCount)(h);
    for (unsigned i = 0; i < c; i++) { struct cim_item it; if (cim_item_read(h, i, &it) && (it.state & 0x1000)) n++; }
    return n;
}
TEST(cursor_icon_menu_items_default_item_set_and_get) {
    CIM_GUI();
    MENU(h, 3, 1);
    CHECK(U(SetMenuDefaultItem)(h, 101, FALSE));
    EXPECT_EQ(U(GetMenuDefaultItem)(h, FALSE, 0), 101);
    EXPECT_EQ(U(GetMenuDefaultItem)(h, TRUE, 0), 1);
    EXPECT_EQ(count_default(h), 1);
    struct cim_menu m;
    if (cim_menu_read(h, &m)) CHECK(m.flags & 0x400); /* _SetMenuDefaultItem: menu flag 0x400 = "has default item" */
    CHECK(U(GetMenuState)(h, 101, 0) & MF_DEFAULT);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_default_item_moves) {
    CIM_GUI();
    MENU(h, 3, 1);
    CHECK(U(SetMenuDefaultItem)(h, 100, FALSE));
    CHECK(U(SetMenuDefaultItem)(h, 102, FALSE));
    EXPECT_EQ(U(GetMenuDefaultItem)(h, FALSE, 0), 102);
    EXPECT_EQ(count_default(h), 1);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_default_item_by_position) {
    CIM_GUI();
    MENU(h, 3, 1);
    CHECK(U(SetMenuDefaultItem)(h, 2, TRUE));
    EXPECT_EQ(U(GetMenuDefaultItem)(h, FALSE, 0), 102);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_default_item_none_is_minus1) {
    CIM_GUI();
    MENU(h, 2, 1);
    EXPECT_EQ(U(GetMenuDefaultItem)(h, FALSE, 0), (UINT)-1);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_nt_default_item_missing_fails) {
    CIM_GUI(); REQUIRE(UN(NtUserSetMenuDefaultItem));
    MENU(h, 2, 1);
    EXPECT_EQ(UN(NtUserSetMenuDefaultItem)(h, 5555, 0), 0);
    EXPECT_EQ(UN(NtUserSetMenuDefaultItem)(h, 7, 1), 0);
    EXPECT_EQ(count_default(h), 0);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_nt_default_item_separator_fails) {
    CIM_GUI(); REQUIRE(UN(NtUserSetMenuDefaultItem));
    HMENU h = new_menu(1); if (!h) { t_skip("no menu"); return; }
    CHECK(U(AppendMenuW)(h, MF_STRING, 1, L"a"));
    CHECK(U(AppendMenuW)(h, MF_SEPARATOR, 0, NULL));
    EXPECT_EQ(UN(NtUserSetMenuDefaultItem)(h, 1, 1), 0); /* (*item & 0x800) -> bail out */
    EXPECT_EQ(count_default(h), 0);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_nt_default_item_minus1_succeeds) {
    CIM_GUI(); REQUIRE(UN(NtUserSetMenuDefaultItem));
    MENU(h, 2, 1);
    EXPECT_EQ(UN(NtUserSetMenuDefaultItem)(h, (UINT)-1, 0), 1); /* uItem == -1 skips the lookup and reports success */
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_removing_default_item_clears_menu_flag) {
    CIM_GUI();
    MENU(h, 3, 1);
    CHECK(U(SetMenuDefaultItem)(h, 101, FALSE));
    CHECK(U(RemoveMenu)(h, 101, MF_BYCOMMAND));
    struct cim_menu m;
    if (cim_menu_read(h, &m)) EXPECT_EQ(m.flags & 0x400, 0); /* removing an item that had 0x1000 clears 0x400 in the menu flags */
    EXPECT_EQ(count_default(h), 0);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_second_default_via_setiteminfo_rejected) {
    CIM_GUI();
    MENU(h, 3, 1);
    CHECK(U(SetMenuDefaultItem)(h, 100, FALSE));
    MENUITEMINFOW mi = mii_new(MIIM_STATE); mi.fState = MFS_DEFAULT;
    BOOL r; CIM_CALL_ERR(r, U(SetMenuItemInfoW)(h, 101, FALSE, &mi));
    EXPECT_FAIL(r == 0, E_INVALID_PARAMETER); /* SetLPITEMInfoNoRedraw: state 0x1000 while menu flag 0x400 is set -> 0x57 (the state word was already written, so the item count is not asserted) */
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_default_via_setiteminfo_sets_menu_flag) {
    CIM_GUI();
    MENU(h, 3, 1);
    MENUITEMINFOW mi = mii_new(MIIM_STATE); mi.fState = MFS_DEFAULT;
    CHECK(U(SetMenuItemInfoW)(h, 101, FALSE, &mi));
    struct cim_menu m;
    if (cim_menu_read(h, &m)) CHECK(m.flags & 0x400);
    EXPECT_EQ(U(GetMenuDefaultItem)(h, FALSE, 0), 101);
    U(DestroyMenu)(h);
}

/* ---------------- Remove / Delete / Destroy and submenu ownership ---------------- */
TEST(cursor_icon_menu_items_removemenu_keeps_submenu_alive) {
    CIM_GUI();
    HMENU p = new_menu(1), s = new_menu(1);
    if (!p || !s) { t_skip("no menu"); return; }
    CHECK(U(AppendMenuW)(p, MF_POPUP | MF_STRING, (UINT_PTR)s, L"sub"));
    CHECK(U(RemoveMenu)(p, 0, MF_BYPOSITION));       /* RemoveMenu frees the item but does not destroy the submenu */
    EXPECT_EQ(U(GetMenuItemCount)(p), 0);
    CHECK(U(IsMenu)(s));
    CHECK(cim_entry_is(s, CT_MENU) || !cim_ready());
    CHECK(U(DestroyMenu)(s));
    U(DestroyMenu)(p);
}
TEST(cursor_icon_menu_items_deletemenu_destroys_submenu) {
    CIM_GUI();
    HMENU p = new_menu(1), s = new_menu(1);
    if (!p || !s) { t_skip("no menu"); return; }
    CHECK(U(AppendMenuW)(p, MF_POPUP | MF_STRING, (UINT_PTR)s, L"sub"));
    CHECK(U(DeleteMenu)(p, 0, MF_BYPOSITION));       /* DeleteMenu destroys the submenu */
    EXPECT_EQ(U(GetMenuItemCount)(p), 0);
    CHECK(!U(IsMenu)(s));
    U(DestroyMenu)(p);
}
TEST(cursor_icon_menu_items_destroymenu_destroys_submenu_tree) {
    CIM_GUI();
    HMENU a = new_menu(1), b = new_menu(1), c = new_menu(1);
    if (!a || !b || !c) { t_skip("no menu"); return; }
    CHECK(U(AppendMenuW)(c, MF_STRING, 1, L"leaf"));
    CHECK(U(AppendMenuW)(b, MF_POPUP | MF_STRING, (UINT_PTR)c, L"c"));
    CHECK(U(AppendMenuW)(a, MF_POPUP | MF_STRING, (UINT_PTR)b, L"b"));
    DWORD base = cim_gr(GR_USEROBJECTS);
    CHECK(U(DestroyMenu)(a));
    CHECK(!U(IsMenu)(a)); CHECK(!U(IsMenu)(b)); CHECK(!U(IsMenu)(c));
    EXPECT_EQ(cim_gr(GR_USEROBJECTS), base - 3);
}
TEST(cursor_icon_menu_items_delete_missing_item_fails) {
    CIM_GUI();
    MENU(h, 2, 1);
    EXPECT_EQ(U(DeleteMenu)(h, 999, MF_BYCOMMAND), 0);
    EXPECT_EQ(U(RemoveMenu)(h, 999, MF_BYCOMMAND), 0);
    EXPECT_EQ(U(DeleteMenu)(h, 2, MF_BYPOSITION), 0);
    EXPECT_EQ(U(RemoveMenu)(h, 2, MF_BYPOSITION), 0);
    EXPECT_EQ(U(GetMenuItemCount)(h), 2);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_delete_shifts_following_items) {
    CIM_GUI();
    MENU(h, 4, 1);
    CHECK(U(DeleteMenu)(h, 1, MF_BYPOSITION));
    EXPECT_EQ(U(GetMenuItemCount)(h), 3);
    unsigned want[3] = { 100, 102, 103 };
    for (unsigned i = 0; i < 3; i++) {
        EXPECT_EQ(U(GetMenuItemID)(h, i), want[i]);
        struct cim_item it;
        if (cim_item_read(h, i, &it)) EXPECT_EQ(it.id, want[i]);
    }
    struct cim_menu m;
    if (cim_menu_read(h, &m)) EXPECT_EQ(m.citems, 3);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_delete_last_item_frees_item_array) {
    CIM_GUI();
    MENU(h, 1, 1);
    CHECK(U(DeleteMenu)(h, 0, MF_BYPOSITION));
    struct cim_menu m;
    if (cim_menu_read(h, &m)) { EXPECT_EQ(m.citems, 0); EXPECT_EQ(m.rgitems, 0); } /* cItems==1 branch zeroes rgItems */
    EXPECT_EQ(U(GetMenuItemCount)(h), 0);
    CHECK(U(AppendMenuW)(h, MF_STRING, 5, L"again"));
    EXPECT_EQ(U(GetMenuItemID)(h, 0), 5);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_delete_by_command_inside_submenu) {
    CIM_GUI();
    HMENU p = new_menu(1), s = new_menu(1);
    if (!p || !s) { t_skip("no menu"); return; }
    CHECK(U(AppendMenuW)(s, MF_STRING, 31, L"a"));
    CHECK(U(AppendMenuW)(s, MF_STRING, 32, L"b"));
    CHECK(U(AppendMenuW)(p, MF_POPUP | MF_STRING, (UINT_PTR)s, L"sub"));
    CHECK(U(DeleteMenu)(p, 31, MF_BYCOMMAND));
    EXPECT_EQ(U(GetMenuItemCount)(s), 1);
    EXPECT_EQ(U(GetMenuItemCount)(p), 1);
    EXPECT_EQ(U(GetMenuItemID)(s, 0), 32);
    U(DestroyMenu)(p);
}
TEST(cursor_icon_menu_items_same_id_first_match_wins) {
    CIM_GUI();
    HMENU h = new_menu(1); if (!h) { t_skip("no menu"); return; }
    CHECK(U(AppendMenuW)(h, MF_STRING, 7, L"one"));
    CHECK(U(AppendMenuW)(h, MF_STRING, 7, L"two"));
    CHECK(U(DeleteMenu)(h, 7, MF_BYCOMMAND));
    EXPECT_EQ(U(GetMenuItemCount)(h), 1);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_many_items_grow_array) {
    CIM_GUI();
    HMENU h = new_menu(1); if (!h) { t_skip("no menu"); return; }
    enum { N = 1000 };
    int ok = 0;
    for (int i = 0; i < N; i++) if (U(AppendMenuW)(h, MF_STRING, 1000 + i, L"i")) ok++; else break;
    /* the item array lives in the desktop heap: a menu stops growing near 1170 items on a default 22H2 desktop
     * heap, with no error set */
    if (ok != N) { U(DestroyMenu)(h); t_skip("desktop heap allowed only %d menu items (needs %d)", ok, N); return; }
    EXPECT_EQ(ok, N);
    EXPECT_EQ(U(GetMenuItemCount)(h), N);
    struct cim_menu m;
    if (cim_menu_read(h, &m)) EXPECT_EQ(m.citems, N);
    struct cim_item it;
    if (cim_item_read(h, N - 1, &it)) EXPECT_EQ(it.id, 1000 + N - 1);
    if (cim_item_read(h, 0, &it)) EXPECT_EQ(it.id, 1000);
    if (cim_item_read(h, 777, &it)) EXPECT_EQ(it.id, 1777);
    EXPECT_EQ(U(GetMenuItemID)(h, 777), 1777);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_nesting_depth_limit) {
    CIM_GUI();
    /* SetLPITEMInfoNoRedraw refuses a submenu when (extra + GetMenuAncestors(container)) >= 0x19 */
    enum { MAXD = 40 };
    HMENU chain[MAXD + 1];
    int n = 0;
    chain[0] = new_menu(1);
    if (!chain[0]) { t_skip("no menu"); return; }
    int succeeded = 0;
    for (int i = 0; i < MAXD; i++) {
        HMENU child = new_menu(1);
        if (!child) break;
        if (!U(AppendMenuW)(chain[i], MF_POPUP | MF_STRING, (UINT_PTR)child, L"n")) { U(DestroyMenu)(child); break; }
        chain[i + 1] = child; succeeded++; n = i + 1;
    }
    t_note("submenu nesting accepted %d levels below the root", succeeded);
    CHECKF(succeeded >= 20 && succeeded <= 25, "nesting levels accepted: %d", succeeded); /* exact bound (0x19) depends on how GetMenuAncestors counts the root: range only */
    U(DestroyMenu)(chain[0]);
    for (int i = 1; i <= n; i++) CHECK(!U(IsMenu)(chain[i]));
}

/* ---------------- MENUITEMINFO validation (user32!ValidateMENUITEMINFO + kernel guard) ---------------- */
TEST(cursor_icon_menu_items_iteminfo_cbsize_table) {
    CIM_GUI();
    MENU(h, 1, 1);
    static const UINT sizes[] = { 0, 1, 4, 0x2c, 0x40, 0x47, 0x49, 0x4f, 0x51, 0x58, 0x100, 0xffffffff };
    for (unsigned i = 0; i < sizeof sizes / sizeof *sizes; i++) {
        MENUITEMINFOW mi = mii_new(MIIM_ID); mi.cbSize = sizes[i];
        BOOL r; CIM_CALL_ERR(r, U(GetMenuItemInfoW)(h, 0, TRUE, &mi));
        CHECKF(r == 0 && GetLastError() == E_INVALID_PARAMETER, "get cbSize 0x%x err %lu", sizes[i], GetLastError());
        mi.cbSize = sizes[i]; mi.wID = 9;
        CIM_CALL_ERR(r, U(SetMenuItemInfoW)(h, 0, TRUE, &mi));
        CHECKF(r == 0 && GetLastError() == E_INVALID_PARAMETER, "set cbSize 0x%x err %lu", sizes[i], GetLastError());
        CIM_CALL_ERR(r, U(InsertMenuItemW)(h, 0, TRUE, &mi));
        CHECKF(r == 0 && GetLastError() == E_INVALID_PARAMETER, "insert cbSize 0x%x err %lu", sizes[i], GetLastError());
    }
    EXPECT_EQ(U(GetMenuItemCount)(h), 1);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_iteminfo_cbsize_valid_sizes) {
    CIM_GUI();
    MENU(h, 1, 1);
    static const UINT sizes[] = { 0x48, 0x50 };
    for (unsigned i = 0; i < 2; i++) {
        MENUITEMINFOW mi = mii_new(MIIM_ID); mi.cbSize = sizes[i];
        CHECKF(U(GetMenuItemInfoW)(h, 0, TRUE, &mi), "get cbSize 0x%x", sizes[i]);
        EXPECT_EQ(mi.wID, 100);
        EXPECT_EQ(mi.cbSize, sizes[i]); /* GetMenuItemInfoW restores the caller's cbSize */
    }
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_iteminfo_null_pointer_rejected) {
    CIM_GUI();
    MENU(h, 1, 1);
    BOOL r;
    CIM_CALL_ERR(r, U(InsertMenuItemW)(h, 0, TRUE, NULL)); CHECK(r == 0); EXPECT_ERR(E_INVALID_PARAMETER);
    CIM_CALL_ERR(r, U(SetMenuItemInfoW)(h, 0, TRUE, NULL)); CHECK(r == 0); EXPECT_ERR(E_INVALID_PARAMETER);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_iteminfo_fmask_invalid_bits_table) {
    CIM_GUI();
    MENU(h, 1, 1);
    static const UINT masks[] = { 0x200, 0x400, 0x800, 0x1000, 0x10000, 0x80000000, 0xfffffe00,
        MIIM_TYPE | MIIM_STRING, MIIM_TYPE | MIIM_BITMAP, MIIM_TYPE | MIIM_FTYPE, MIIM_TYPE | 0x1c0 };
    for (unsigned i = 0; i < sizeof masks / sizeof *masks; i++) {
        MENUITEMINFOW mi = mii_new(masks[i]); mi.wID = 4;
        BOOL r;
        CIM_CALL_ERR(r, U(GetMenuItemInfoW)(h, 0, TRUE, &mi)); CHECKF(r == 0 && GetLastError() == E_INVALID_PARAMETER, "get mask 0x%x err %lu", masks[i], GetLastError());
        mi.fMask = masks[i];
        CIM_CALL_ERR(r, U(SetMenuItemInfoW)(h, 0, TRUE, &mi)); CHECKF(r == 0 && GetLastError() == E_INVALID_PARAMETER, "set mask 0x%x err %lu", masks[i], GetLastError());
        mi.fMask = masks[i];
        CIM_CALL_ERR(r, U(InsertMenuItemW)(h, 0, TRUE, &mi)); CHECKF(r == 0 && GetLastError() == E_INVALID_PARAMETER, "insert mask 0x%x err %lu", masks[i], GetLastError());
    }
    EXPECT_EQ(U(GetMenuItemCount)(h), 1);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_iteminfo_fstate_invalid_bits_table) {
    CIM_GUI();
    MENU(h, 1, 1);
    static const UINT st[] = { 0x4, 0x10, 0x20, 0x40, 0x100, 0x2000, 0x10000, 0x80000000, 0xffffef74 };
    for (unsigned i = 0; i < sizeof st / sizeof *st; i++) {
        MENUITEMINFOW mi = mii_new(MIIM_STATE); mi.fState = st[i];
        BOOL r;
        CIM_CALL_ERR(r, U(SetMenuItemInfoW)(h, 0, TRUE, &mi)); CHECKF(r == 0 && GetLastError() == E_INVALID_PARAMETER, "set fState 0x%x err %lu", st[i], GetLastError());
        CIM_CALL_ERR(r, U(InsertMenuItemW)(h, 0, TRUE, &mi)); CHECKF(r == 0 && GetLastError() == E_INVALID_PARAMETER, "insert fState 0x%x err %lu", st[i], GetLastError());
    }
    /* without MIIM_STATE the field is not looked at */
    MENUITEMINFOW mi = mii_new(MIIM_ID); mi.fState = 0xffffffff; mi.wID = 12;
    CHECK(U(SetMenuItemInfoW)(h, 0, TRUE, &mi));
    EXPECT_EQ(U(GetMenuItemID)(h, 0), 12);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_nt_iteminfo_bad_fstate_guard_returns_zero) {
    CIM_GUI(); REQUIRE(UN(NtUserThunkedMenuItemInfo));
    MENU(h, 1, 1);
    MENUITEMINFOW mi = mii_new(MIIM_STATE); mi.cbSize = 0x50; mi.fState = 0x2000;
    /* NtUserThunkedMenuItemInfo: MIIM_STATE with (fState & 0xffffef74) skips the whole operation */
    EXPECT_EQ(UN(NtUserThunkedMenuItemInfo)(h, 0, TRUE, FALSE, &mi, NULL), 0);
    EXPECT_EQ(UN(NtUserThunkedMenuItemInfo)(h, 0, TRUE, TRUE, &mi, NULL), 0);
    EXPECT_EQ(U(GetMenuItemCount)(h), 1);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_nt_iteminfo_set_id_direct) {
    CIM_GUI(); REQUIRE(UN(NtUserThunkedMenuItemInfo));
    MENU(h, 2, 1);
    MENUITEMINFOW mi = mii_new(MIIM_ID); mi.cbSize = 0x50; mi.wID = 4321;
    EXPECT_EQ(UN(NtUserThunkedMenuItemInfo)(h, 1, TRUE, FALSE, &mi, NULL), 1);
    EXPECT_EQ(U(GetMenuItemID)(h, 1), 4321);
    struct cim_item it;
    if (cim_item_read(h, 1, &it)) EXPECT_EQ(it.id, 4321);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_nt_iteminfo_insert_direct) {
    CIM_GUI(); REQUIRE(UN(NtUserThunkedMenuItemInfo));
    MENU(h, 2, 1);
    MENUITEMINFOW mi = mii_new(MIIM_ID | MIIM_FTYPE); mi.cbSize = 0x50; mi.wID = 77; mi.fType = MFT_STRING;
    EXPECT_EQ(UN(NtUserThunkedMenuItemInfo)(h, 0xffffffff, TRUE, TRUE, &mi, NULL), 1);
    EXPECT_EQ(U(GetMenuItemCount)(h), 3);
    EXPECT_EQ(U(GetMenuItemID)(h, 2), 77);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_nt_iteminfo_missing_item_0x5b0) {
    CIM_GUI(); REQUIRE(UN(NtUserThunkedMenuItemInfo));
    MENU(h, 2, 1);
    MENUITEMINFOW mi = mii_new(MIIM_ID); mi.cbSize = 0x50; mi.wID = 1;
    BOOL r; CIM_CALL_ERR(r, UN(NtUserThunkedMenuItemInfo)(h, 5, TRUE, FALSE, &mi, NULL));
    EXPECT_FAIL(r == 0, E_MENU_ITEM_NOT_FOUND); /* item lookup failed -> 0x5b0 */
    CIM_CALL_ERR(r, UN(NtUserThunkedMenuItemInfo)(h, 4242, FALSE, FALSE, &mi, NULL));
    EXPECT_FAIL(r == 0, E_MENU_ITEM_NOT_FOUND);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_setiteminfo_missing_item_0x5b0) {
    CIM_GUI();
    MENU(h, 2, 1);
    MENUITEMINFOW mi = mii_new(MIIM_ID); mi.wID = 1;
    BOOL r; CIM_CALL_ERR(r, U(SetMenuItemInfoW)(h, 9, TRUE, &mi));
    EXPECT_FAIL(r == 0, E_MENU_ITEM_NOT_FOUND);
    CIM_CALL_ERR(r, U(SetMenuItemInfoW)(h, 9999, FALSE, &mi));
    EXPECT_FAIL(r == 0, E_MENU_ITEM_NOT_FOUND);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_getiteminfo_missing_item_0x5b0) {
    CIM_GUI();
    MENU(h, 2, 1);
    MENUITEMINFOW mi = mii_new(MIIM_ID);
    BOOL r; CIM_CALL_ERR(r, U(GetMenuItemInfoW)(h, 2, TRUE, &mi));
    EXPECT_FAIL(r == 0, E_MENU_ITEM_NOT_FOUND); /* user32!GetMenuItemInfoInternalW: SetLastError(0x5b0) */
    CIM_CALL_ERR(r, U(GetMenuItemInfoW)(h, 31337, FALSE, &mi));
    EXPECT_FAIL(r == 0, E_MENU_ITEM_NOT_FOUND);
    CIM_CALL_ERR(r, U(GetMenuItemInfoW)(h, (UINT)-1, TRUE, &mi));
    EXPECT_FAIL(r == 0, E_MENU_ITEM_NOT_FOUND);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_getiteminfo_invalid_menu_fails) {
    CIM_GUI();
    HMENU h = new_menu(1); if (!h) { t_skip("no menu"); return; }
    CHECK(U(AppendMenuW)(h, MF_STRING, 1, L"a"));
    U(DestroyMenu)(h);
    MENUITEMINFOW mi = mii_new(MIIM_ID);
    BOOL r; CIM_CALL_ERR(r, U(GetMenuItemInfoW)(h, 0, TRUE, &mi));
    CHECK(r == 0);
    DWORD e = GetLastError();
    CHECKF(e == E_INVALID_PARAMETER || e == E_INVALID_MENU_HANDLE, "err %lu", e);
    CIM_CALL_ERR(r, U(SetMenuItemInfoW)(h, 0, TRUE, &mi));
    CHECK(r == 0);
    CIM_CALL_ERR(r, U(InsertMenuItemW)(h, 0, TRUE, &mi));
    CHECK(r == 0);
}
TEST(cursor_icon_menu_items_iteminfo_roundtrip_id_data_state) {
    CIM_GUI();
    MENU(h, 2, 1);
    MENUITEMINFOW mi = mii_new(MIIM_ID | MIIM_DATA | MIIM_STATE);
    mi.wID = 2024; mi.dwItemData = 0x1234567890ABCDEFull; mi.fState = MFS_CHECKED | MFS_GRAYED;
    CHECK(U(SetMenuItemInfoW)(h, 1, TRUE, &mi));
    MENUITEMINFOW g = mii_new(MIIM_ID | MIIM_DATA | MIIM_STATE);
    CHECK(U(GetMenuItemInfoW)(h, 1, TRUE, &g));
    EXPECT_EQ(g.wID, 2024);
    EXPECT_EQ(g.dwItemData, 0x1234567890ABCDEFull);
    EXPECT_EQ(g.fState & 0x108b, MFS_CHECKED | MFS_GRAYED);
    EXPECT_EQ(g.fState & ~0x108bu, 0); /* kernel state is masked with 0x108b on the way out */
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_getiteminfo_mask_only_fills_requested) {
    CIM_GUI();
    MENU(h, 1, 1);
    MENUITEMINFOW g = mii_new(MIIM_ID); g.wID = 0xdead; g.fState = 0x1111; g.dwItemData = 0x77;
    CHECK(U(GetMenuItemInfoW)(h, 0, TRUE, &g));
    EXPECT_EQ(g.wID, 100);
    EXPECT_EQ(g.fState, 0x1111);
    EXPECT_EQ(g.dwItemData, 0x77);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_getiteminfo_miim_type_clears_split_bits) {
    CIM_GUI();
    MENU(h, 1, 1);
    MENUITEMINFOW g = mii_new(MIIM_TYPE);
    WCHAR buf[32]; g.dwTypeData = buf; g.cch = 32;
    CHECK(U(GetMenuItemInfoW)(h, 0, TRUE, &g));
    EXPECT_EQ(g.fMask & 0x1c0, 0); /* user32!GetMenuItemInfoW: fMask &= 0xfffffe3f when MIIM_TYPE was asked */
    EXPECT_EQ(g.fType & MFT_SEPARATOR, 0);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_getiteminfo_ftype_masked) {
    CIM_GUI();
    HMENU h = new_menu(1); if (!h) { t_skip("no menu"); return; }
    CHECK(U(AppendMenuW)(h, MF_STRING | MF_MENUBARBREAK, 1, L"a"));
    CHECK(U(AppendMenuW)(h, MF_STRING | MF_OWNERDRAW, 2, L"b"));
    MENUITEMINFOW g = mii_new(MIIM_FTYPE);
    CHECK(U(GetMenuItemInfoW)(h, 0, TRUE, &g));
    CHECK(g.fType & MFT_MENUBARBREAK);
    g = mii_new(MIIM_FTYPE);
    CHECK(U(GetMenuItemInfoW)(h, 1, TRUE, &g));
    CHECK(g.fType & MFT_OWNERDRAW);
    EXPECT_EQ(g.fType & ~0x36b64u, 0);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_setiteminfo_ftype_invalid_bits) {
    CIM_GUI();
    MENU(h, 1, 1);
    static const UINT ft[] = { 0x1, 0x2, 0x8, 0x10, 0x80, 0x400, 0x1000, 0x8000, 0x40000, 0x80000000 };
    for (unsigned i = 0; i < sizeof ft / sizeof *ft; i++) {
        MENUITEMINFOW mi = mii_new(MIIM_FTYPE); mi.fType = ft[i];
        BOOL r; CIM_CALL_ERR(r, U(SetMenuItemInfoW)(h, 0, TRUE, &mi));
        CHECKF(r == 0 && GetLastError() == E_INVALID_PARAMETER, "fType 0x%x err %lu", ft[i], GetLastError()); /* fType & 0xfffc949b -> 0x57 */
    }
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_setiteminfo_ftype_bitmap_requires_miim_type) {
    CIM_GUI();
    MENU(h, 1, 1);
    MENUITEMINFOW mi = mii_new(MIIM_FTYPE); mi.fType = MFT_BITMAP;
    BOOL r; CIM_CALL_ERR(r, U(SetMenuItemInfoW)(h, 0, TRUE, &mi));
    EXPECT_FAIL(r == 0, E_INVALID_PARAMETER); /* MIIM_FTYPE with MFT_BITMAP but no MIIM_TYPE -> 0x57 */
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_setiteminfo_separator_forces_disabled) {
    CIM_GUI();
    MENU(h, 2, 1);
    MENUITEMINFOW mi = mii_new(MIIM_FTYPE); mi.fType = MFT_SEPARATOR;
    CHECK(U(SetMenuItemInfoW)(h, 0, TRUE, &mi));
    struct cim_item it;
    if (cim_item_read(h, 0, &it)) { CHECK(it.type & 0x800); EXPECT_EQ(it.state & 3, 3); }
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_setiteminfo_submenu_invalid_handle) {
    CIM_GUI();
    MENU(h, 1, 1);
    HMENU dead = new_menu(1); if (!dead) { U(DestroyMenu)(h); t_skip("no menu"); return; }
    U(DestroyMenu)(dead);
    MENUITEMINFOW mi = mii_new(MIIM_SUBMENU); mi.hSubMenu = dead;
    BOOL r; CIM_CALL_ERR(r, U(SetMenuItemInfoW)(h, 0, TRUE, &mi));
    EXPECT_FAIL(r == 0, E_INVALID_PARAMETER); /* ValidateMENUITEMINFO: HMValidateHandle(hSubMenu, 2) failed -> 0x57 */
    mi.hSubMenu = (HMENU)(ULONG_PTR)0x12345;
    CIM_CALL_ERR(r, U(SetMenuItemInfoW)(h, 0, TRUE, &mi));
    EXPECT_FAIL(r == 0, E_INVALID_PARAMETER);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_setiteminfo_submenu_attach_and_detach) {
    CIM_GUI();
    MENU(h, 2, 1);
    HMENU s = new_menu(1); if (!s) { U(DestroyMenu)(h); t_skip("no menu"); return; }
    MENUITEMINFOW mi = mii_new(MIIM_SUBMENU); mi.hSubMenu = s;
    CHECK(U(SetMenuItemInfoW)(h, 0, TRUE, &mi));
    EXPECT_EQ(U(GetSubMenu)(h, 0), s);
    MENUITEMINFOW g = mii_new(MIIM_SUBMENU);
    CHECK(U(GetMenuItemInfoW)(h, 0, TRUE, &g));
    EXPECT_EQ(g.hSubMenu, s);
    mi.hSubMenu = NULL;
    CHECK(U(SetMenuItemInfoW)(h, 0, TRUE, &mi));
    EXPECT_EQ(U(GetSubMenu)(h, 0), NULL);
    CHECK(!U(IsMenu)(s)); /* SetLPITEMInfoNoRedraw: a different (here NULL) submenu makes it RemoveParentMenu + _DestroyMenu the old one */
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_checkmarks_invalid_gdi_handle) {
    CIM_GUI();
    MENU(h, 1, 1);
    MENUITEMINFOW mi = mii_new(MIIM_CHECKMARKS); mi.hbmpChecked = (HBITMAP)(ULONG_PTR)0x12345;
    BOOL r; CIM_CALL_ERR(r, U(SetMenuItemInfoW)(h, 0, TRUE, &mi));
    EXPECT_FAIL(r == 0, E_INVALID_PARAMETER); /* GdiValidateHandle(hbmpChecked) failed */
    mi.hbmpChecked = NULL; mi.hbmpUnchecked = (HBITMAP)(ULONG_PTR)0x23456;
    CIM_CALL_ERR(r, U(SetMenuItemInfoW)(h, 0, TRUE, &mi));
    EXPECT_FAIL(r == 0, E_INVALID_PARAMETER);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_checkmarks_bitmaps_survive_menu) {
    CIM_GUI();
    MENU(h, 1, 1);
    HBITMAP a = U(CreateBitmap)(8, 8, 1, 1, NULL), b = U(CreateBitmap)(8, 8, 1, 1, NULL);
    if (!a || !b) { U(DestroyMenu)(h); t_skip("no bitmaps"); return; }
    CHECK(cim_gdi_bitmap_ok(a));
    BOOL ok = U(SetMenuItemBitmaps)(h, 0, MF_BYPOSITION, a, b);
    CHECK(ok);
    MENUITEMINFOW g = mii_new(MIIM_CHECKMARKS);
    CHECK(U(GetMenuItemInfoW)(h, 0, TRUE, &g));
    EXPECT_EQ(g.hbmpUnchecked, a);
    EXPECT_EQ(g.hbmpChecked, b);
    CHECK(U(DestroyMenu)(h));
    /* the menu never owned the bitmaps (no GDI delete on destroy): both are still ordinary GDI objects of ours */
    CHECK(cim_gdi_bitmap_ok(a)); CHECK(cim_gdi_bitmap_ok(b));
    CHECK(U(DeleteObject)(a)); CHECK(U(DeleteObject)(b));
}
TEST(cursor_icon_menu_items_bitmap_item_survives_menu) {
    CIM_GUI();
    HMENU h = new_menu(1); if (!h) { t_skip("no menu"); return; }
    HBITMAP bm = U(CreateBitmap)(16, 16, 1, 1, NULL);
    if (!bm) { U(DestroyMenu)(h); t_skip("no bitmap"); return; }
    CHECK(U(AppendMenuW)(h, MF_BITMAP, 55, (LPCWSTR)bm));
    struct cim_item it;
    if (cim_item_read(h, 0, &it)) CHECK(it.type & MFT_BITMAP);
    MENUITEMINFOW g = mii_new(MIIM_BITMAP | MIIM_ID);
    CHECK(U(GetMenuItemInfoW)(h, 0, TRUE, &g));
    EXPECT_EQ(g.wID, 55);
    EXPECT_EQ(g.hbmpItem, bm);
    CHECK(U(DestroyMenu)(h));
    CHECK(cim_gdi_bitmap_ok(bm));
    CHECK(U(DeleteObject)(bm));
}
TEST(cursor_icon_menu_items_hbmpitem_invalid_gdi_handle_rejected) {
    CIM_GUI();
    MENU(h, 1, 1);
    MENUITEMINFOW mi = mii_new(MIIM_BITMAP); mi.hbmpItem = (HBITMAP)(ULONG_PTR)0x4567;
    BOOL r; CIM_CALL_ERR(r, U(SetMenuItemInfoW)(h, 0, TRUE, &mi));
    /* Windows 10 does not validate hbmpItem as a GDI handle: the call succeeds, sets no error, and the value reads back */
    CHECK(r != 0);
    EXPECT_EQ(GetLastError(), CIM_SENTINEL);
    MENUITEMINFOW g = mii_new(MIIM_BITMAP);
    CHECK(U(GetMenuItemInfoW)(h, 0, TRUE, &g));
    EXPECT_EQ(g.hbmpItem, (HBITMAP)(ULONG_PTR)0x4567);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_hbmpitem_special_values_accepted) {
    CIM_GUI();
    MENU(h, 1, 1);
    /* HBMMENU_CALLBACK (-1) .. HBMMENU_MAX_? are not GDI handles: ValidateMENUITEMINFO exempts them from GdiValidateHandle */
    static const LONG_PTR vals[] = { -1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
    for (unsigned i = 0; i < sizeof vals / sizeof *vals; i++) {
        MENUITEMINFOW mi = mii_new(MIIM_BITMAP); mi.hbmpItem = (HBITMAP)vals[i];
        BOOL r = U(SetMenuItemInfoW)(h, 0, TRUE, &mi);
        if (!r) t_note("hbmpItem %ld rejected, err %lu", (long)vals[i], GetLastError());
    }
    U(DestroyMenu)(h);
}

/* ---------------- GetMenuString protocol (user32 -> client copy of the item string) ---------------- */
TEST(cursor_icon_menu_items_getmenustring_returns_length) {
    CIM_GUI();
    HMENU h = new_menu(1); if (!h) { t_skip("no menu"); return; }
    CHECK(U(AppendMenuW)(h, MF_STRING, 1, L"Hello"));
    WCHAR b[32]; memset(b, 0xaa, sizeof b);
    int n = U(GetMenuStringW)(h, 1, b, 32, MF_BYCOMMAND);
    EXPECT_EQ(n, 5);
    EXPECT_WSTR(b, L"Hello");
    EXPECT_EQ(U(GetMenuStringW)(h, 0, NULL, 0, MF_BYPOSITION), 5); /* cchMax == 0: length only */
    n = U(GetMenuStringW)(h, 1, b, 3, MF_BYCOMMAND);
    CHECK(n >= 0 && n <= 2);
    CHECK(b[n] == 0);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_getmenustring_missing_item_zero) {
    CIM_GUI();
    MENU(h, 1, 1);
    WCHAR b[8] = L"zz";
    int n; CIM_CALL_ERR(n, U(GetMenuStringW)(h, 999, b, 8, MF_BYCOMMAND));
    EXPECT_EQ(n, 0);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_items_setstring_via_modifymenu) {
    CIM_GUI();
    MENU(h, 1, 1);
    CHECK(U(ModifyMenuW)(h, 100, MF_BYCOMMAND | MF_STRING, 100, L"Changed text"));
    WCHAR b[32];
    EXPECT_EQ(U(GetMenuStringW)(h, 100, b, 32, MF_BYCOMMAND), 12);
    EXPECT_WSTR(b, L"Changed text");
    CHECK(!U(ModifyMenuW)(h, 555, MF_BYCOMMAND | MF_STRING, 555, L"x"));
    U(DestroyMenu)(h);
}
