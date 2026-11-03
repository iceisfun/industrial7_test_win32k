/* NtUserFindExistingCursorIcon argument validation and a few cross-checks of kernel handle validation. */
#include "cim.h"

UDECL(HANDLE, NtUserFindExistingCursorIcon, (UNICODE_STRING *, UNICODE_STRING *, void *));

/* NtUserFindExistingCursorIcon reads three qwords from the find record; qword 0 is a cursor handle that,
 * when non-zero, goes through HMValidateHandle(h, TYPE_CURSOR) and returns 0 before the strings are read. */
static HANDLE find_with(HANDLE h) {
    WCHAR mod[] = L"user32.dll";
    UNICODE_STRING m = { 20, 22, mod }, r = { 0, 0, (PWSTR)(ULONG_PTR)1 };
    unsigned long long rec[3] = { (ULONG_PTR)h, 0, 0 };
    return UN(NtUserFindExistingCursorIcon)(&m, &r, rec);
}
TEST(cursor_icon_menu_find_existing_forged_handle_returns_null) {
    CIM_GUI(); REQUIRE(UN(NtUserFindExistingCursorIcon));
    EXPECT_EQ(find_with((HANDLE)(ULONG_PTR)0x1234), NULL);
    EXPECT_EQ(find_with((HANDLE)(ULONG_PTR)0xffff0001), NULL);
}
TEST(cursor_icon_menu_find_existing_wrong_type_handle_returns_null) {
    CIM_GUI(); REQUIRE(UN(NtUserFindExistingCursorIcon));
    HMENU m = U(CreateMenu)();
    if (!m) { t_skip("no menu"); return; }
    EXPECT_EQ(find_with((HANDLE)m), NULL); /* TYPE_MENU is not TYPE_CURSOR */
    U(DestroyMenu)(m);
}
TEST(cursor_icon_menu_find_existing_destroyed_cursor_returns_null) {
    CIM_GUI(); REQUIRE(UN(NtUserFindExistingCursorIcon));
    HICON c = cim_mk_cursor_ii(16, 16, 0, 0);
    if (!c) { t_skip("no cursor"); return; }
    U(DestroyCursor)(c);
    EXPECT_EQ(find_with((HANDLE)c), NULL); /* uniq no longer matches the freed entry */
}
TEST(cursor_icon_menu_menu_cursor_handles_not_interchangeable) {
    CIM_GUI();
    HMENU m = U(CreateMenu)();
    HICON c = cim_mk_cursor_ii(16, 16, 0, 0);
    if (!m || !c) { t_skip("setup"); if (m) U(DestroyMenu)(m); if (c) U(DestroyCursor)(c); return; }
    EXPECT_EQ(U(IsMenu)((HMENU)c), FALSE);
    BOOL r; CIM_CALL_ERR(r, U(DestroyMenu)((HMENU)c));
    EXPECT_EQ(r, FALSE);
    EXPECT_EQ(U(GetMenuItemCount)((HMENU)c), -1);
    ICONINFO ii;
    CIM_CALL_ERR(r, U(GetIconInfo)((HICON)m, &ii));
    EXPECT_EQ(r, FALSE);
    CHECK(U(DestroyCursor)(c));
    CHECK(U(DestroyMenu)(m));
}
