/* Additional table-vs-kernel checks: reserved/out-of-range indices, failed creation, reparenting,
 * bulk allocation, copies of cursor objects and objects that must NOT consume handle slots. */
#include "uh.h"

/* HMValidateHandle (user32): the index is compared against the shared handle-entry count before the entry is read. */
TEST(user_handle_table_index_at_count_rejected) {
    UH_NEED();
    unsigned long long n = uh_n();
    if (n >= 0xffff) { t_skip("table at maximum size"); return; }
    HWND f = (HWND)(ULONG_PTR)((unsigned)n | (1u << 16));
    CHECK(!U(IsWindow)(f));
    DWORD pid = 5; SetLastError(UH_SENTINEL);
    EXPECT_EQ(U(GetWindowThreadProcessId)(f, &pid), 0);
    EXPECT_ERR(0x578);
}
/* Entry 0 is reserved (HMInitHandleTable: type 0, uniq 1) so a handle of 0x10000 can never validate. */
TEST(user_handle_table_reserved_entry_zero_never_validates) {
    UH_NEED();
    uh_entry_t e; CHECK(uh_read(0, &e));
    EXPECT_EQ(e.type, 0); EXPECT_EQ(e.uniq, 1);
    HWND h = (HWND)(ULONG_PTR)(1u << 16);
    CHECK(!U(IsWindow)(h));
    BOOL r; UH_CALL_ERR(r, U(IsMenu)((HMENU)h)); CHECK(!r); EXPECT_ERR(0x579);
    UH_CALL_ERR(r, U(DestroyWindow)(h)); CHECK(!r); EXPECT_ERR(0x578);
}
TEST(user_handle_table_null_handles_use_type_specific_errors) {
    UH_NEED();
    BOOL r;
    UH_CALL_ERR(r, U(IsMenu)(NULL)); CHECK(!r); EXPECT_ERR(0x579);
    UH_CALL_ERR(r, U(DestroyMenu)(NULL)); CHECK(!r); EXPECT_ERR(0x579);
    UH_CALL_ERR(r, U(DestroyWindow)(NULL)); CHECK(!r); EXPECT_ERR(0x578);
    UH_CALL_ERR(r, U(DestroyIcon)(NULL)); CHECK(!r); EXPECT_ERR(0x57a);
    UH_CALL_ERR(r, U(DestroyAcceleratorTable)(NULL)); CHECK(!r); EXPECT_ERR(0x57b);
    UH_CALL_ERR(r, U(UnhookWindowsHookEx)(NULL)); CHECK(!r); EXPECT_ERR(0x57c);
}
/* A failed CreateWindowEx must not leave an entry or a count behind. */
TEST(user_handle_table_failed_createwindow_leaks_nothing) {
    UH_NEED();
    DWORD g0 = uh_gr(GR_USEROBJECTS); unsigned s0 = uh_scan_owned(-1);
    for (int i = 0; i < 5; i++) {
        SetLastError(UH_SENTINEL);
        HWND w = U(CreateWindowExW)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"win32kt_no_such_class_uh", L"x", WS_POPUP, -32000, -32000, 1, 1, NULL, NULL, GetModuleHandleW(NULL), NULL);
        CHECK(w == NULL);
        EXPECT_ERR(1407); /* ERROR_CANNOT_FIND_WND_CLASS */
    }
    EXPECT_EQ(uh_gr(GR_USEROBJECTS), g0);
    EXPECT_EQ(uh_scan_owned(-1), s0);
}
/* Reparenting moves the window in the tree only; its table entry and handle stay identical. */
TEST(user_handle_table_setparent_keeps_entry) {
    UH_NEED();
    HWND p1 = uh_make_top(), p2 = uh_make_top(); REQUIRE(p1 && p2);
    HWND c = uh_make_child(p1);
    if (!c) { t_skip("child creation failed"); U(DestroyWindow)(p1); U(DestroyWindow)(p2); return; }
    uh_entry_t a, b; CHECK(uh_read_h(c, &a));
    CHECK(U(SetParent)(c, p2) == p1);
    CHECK(U(GetParent)(c) == p2);
    CHECK(U(IsWindow)(c));
    CHECK(uh_read_h(c, &b));
    EXPECT_EQ(a.delta, b.delta); EXPECT_EQ(a.owner, b.owner); EXPECT_EQ(a.f10, b.f10);
    EXPECT_EQ(a.type, b.type); EXPECT_EQ(a.uniq, b.uniq); EXPECT_EQ(a.flags, b.flags);
    U(DestroyWindow)(p1);
    CHECK(U(IsWindow)(c)); /* destroying the old parent does not touch the moved child */
    U(DestroyWindow)(p2);
    CHECK(!U(IsWindow)(c));
}
/* Bulk allocation: every child window gets its own even index and exactly one count. */
TEST(user_handle_table_bulk_children_distinct_and_counted) {
    UH_NEED();
    HWND p = uh_make_top(); REQUIRE(p);
    enum { N = 200 };
    static HWND w[N];
    unsigned s0 = uh_scan_owned(UT_WINDOW); DWORD g0 = uh_gr(GR_USEROBJECTS);
    int k = 0;
    for (; k < N; k++) { w[k] = uh_make_child(p); if (!w[k]) break; }
    CHECK(k == N);
    EXPECT_EQ(uh_scan_owned(UT_WINDOW), s0 + k);
    EXPECT_EQ(uh_gr(GR_USEROBJECTS), g0 + k);
    unsigned bad = 0;
    for (int i = 0; i < k; i++) {
        if (UH_IDX(w[i]) & 1) bad++;
        for (int j = i + 1; j < k; j++) if (UH_IDX(w[i]) == UH_IDX(w[j])) bad++;
    }
    EXPECT_EQ(bad, 0);
    for (int i = 0; i < k; i++) U(DestroyWindow)(w[i]);
    EXPECT_EQ(uh_scan_owned(UT_WINDOW), s0);
    EXPECT_EQ(uh_gr(GR_USEROBJECTS), g0);
    U(DestroyWindow)(p);
}
/* Menu items live in the menu object, not in the handle table. */
TEST(user_handle_table_menu_items_do_not_consume_handles) {
    UH_NEED();
    HMENU m = U(CreateMenu)(); REQUIRE(m);
    DWORD g0 = uh_gr(GR_USEROBJECTS); unsigned s0 = uh_scan_owned(-1);
    for (UINT i = 0; i < 50; i++) CHECK(U(AppendMenuW)(m, MF_STRING, 2000 + i, L"item"));
    EXPECT_EQ(uh_gr(GR_USEROBJECTS), g0);
    EXPECT_EQ(uh_scan_owned(-1), s0);
    EXPECT_EQ(U(GetMenuItemCount)(m), 50);
    U(DestroyMenu)(m);
}
/* One accelerator table object regardless of the number of entries. */
TEST(user_handle_table_accel_table_is_one_object) {
    UH_NEED();
    ACCEL a[6] __attribute__((aligned(4))); memset(a, 0, sizeof a);
    for (int i = 0; i < 6; i++) { a[i].fVirt = FVIRTKEY | FCONTROL; a[i].key = 'A' + i; a[i].cmd = 3000 + i; }
    unsigned s0 = uh_scan_owned(UT_ACCEL); DWORD g0 = uh_gr(GR_USEROBJECTS);
    HACCEL h = U(CreateAcceleratorTableW)(a, 6); REQUIRE(h);
    EXPECT_EQ(uh_scan_owned(UT_ACCEL), s0 + 1);
    EXPECT_EQ(uh_gr(GR_USEROBJECTS), g0 + 1);
    EXPECT_EQ(U(CopyAcceleratorTableW)(h, NULL, 0), 6); /* count query does not allocate */
    EXPECT_EQ(uh_scan_owned(UT_ACCEL), s0 + 1);
    CHECK(U(DestroyAcceleratorTable)(h));
    EXPECT_EQ(uh_scan_owned(UT_ACCEL), s0);
}
/* CopyIcon makes a second cursor object with its own entry. */
TEST(user_handle_table_copyicon_makes_new_cursor_entry) {
    UH_NEED();
    HICON i = (HICON)uh_d_icon.create(); REQUIRE(i);
    unsigned s0 = uh_scan_owned(UT_CURSOR);
    HICON c = U(CopyIcon)(i);
    if (!c) { t_skip("CopyIcon failed (%lu)", GetLastError()); uh_d_icon.destroy(i); return; }
    CHECK(c != i);
    uh_entry_t e; CHECK(uh_read_h(c, &e));
    EXPECT_EQ(e.type, UT_CURSOR); EXPECT_EQ(e.uniq, UH_UNIQ(c)); EXPECT_EQ(e.owner, GetCurrentProcessId());
    EXPECT_EQ(uh_scan_owned(UT_CURSOR), s0 + 1);
    CHECK(uh_d_icon.destroy(c));
    CHECK(uh_d_icon.destroy(i));
    EXPECT_EQ(uh_scan_owned(UT_CURSOR), s0 - 1);
}
/* GetIconInfo hands out GDI bitmaps only; the USER cursor count is unchanged. */
TEST(user_handle_table_geticoninfo_adds_gdi_not_user_objects) {
    UH_NEED();
    REQUIRE(U(DeleteObject)); REQUIRE(U(GetIconInfo));
    HICON i = (HICON)uh_d_icon.create(); REQUIRE(i);
    DWORD u0 = uh_gr(GR_USEROBJECTS), g0 = uh_gr(GR_GDIOBJECTS);
    ICONINFO ii; memset(&ii, 0, sizeof ii);
    if (!U(GetIconInfo)(i, &ii)) { t_skip("GetIconInfo failed (%lu)", GetLastError()); uh_d_icon.destroy(i); return; }
    EXPECT_EQ(uh_gr(GR_USEROBJECTS), u0);
    CHECK(uh_gr(GR_GDIOBJECTS) > g0);
    if (ii.hbmMask) U(DeleteObject)(ii.hbmMask);
    if (ii.hbmColor) U(DeleteObject)(ii.hbmColor);
    EXPECT_EQ(uh_gr(GR_GDIOBJECTS), g0);
    uh_d_icon.destroy(i);
}
/* The desktop heap base word every heap-backed entry carries must be the same for all types. */
TEST(user_handle_table_heap_types_share_desktop_base_word) {
    UH_NEED();
    HWND w = uh_make_top(); HMENU m = U(CreateMenu)(); REQUIRE(w && m);
    uh_entry_t a, b;
    CHECK(uh_read_h(w, &a) && uh_read_h(m, &b));
    CHECK(a.f10 != 0);
    EXPECT_EQ(a.f10, b.f10);
    U(DestroyMenu)(m); U(DestroyWindow)(w);
}
/* Timer handles: SetTimer(NULL) returns an id, not the table handle; the object is in the table by owner. */
TEST(user_handle_table_timer_id_is_not_table_handle) {
    UH_NEED();
    UINT_PTR id = U(SetTimer)(NULL, 0, 60000, NULL); REQUIRE(id);
    CHECK(id != 0);
    CHECK(!U(IsWindow)((HWND)id));
    CHECK(U(KillTimer)(NULL, id));
}
