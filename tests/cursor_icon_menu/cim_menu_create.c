/* Menu object creation / destruction: the TYPE_MENU entry the kernel allocates (handle type 2
 * from InternalCreateMenu), the shared part the client can map, ownership and handle validation. */
#include "cim.h"

#define CREATE_BAR()   U(CreateMenu)()
#define CREATE_POPUP() U(CreatePopupMenu)()

static int mk(HMENU *h, int popup) {
    *h = popup ? CREATE_POPUP() : CREATE_BAR();
    if (!*h) { t_skip("cannot create menu (err %lu)", GetLastError()); return 0; }
    return 1;
}

/* the handle allocator: entry type 2, uniq == HIWORD(handle), alive. */
static void entry_checks(HMENU h) {
    cim_he e;
    CHECK(CIM_IDX(h) < cim_nentries());
    CHECK(cim_he_read(h, &e));
    EXPECT_EQ(e.type, CT_MENU);
    EXPECT_EQ(e.uniq, CIM_UNIQ(h));
    CHECK(e.uniq != 0 && e.uniq != 0xffff);
    CHECK((e.flags & 1) == 0);
    EXPECT_EQ(e.owner, GetCurrentProcessId()); /* menus are process owned: the handle allocator stores the process id */
    CHECK(e.delta != 0);                      /* shared part lives in the desktop heap */
    CHECK(e.f10 != 0);
}

TEST(cursor_icon_menu_create_bar_entry) { CIM_NEED(); HMENU h; if (!mk(&h, 0)) return; entry_checks(h); U(DestroyMenu)(h); }
TEST(cursor_icon_menu_create_popup_entry) { CIM_NEED(); HMENU h; if (!mk(&h, 1)) return; entry_checks(h); U(DestroyMenu)(h); }

TEST(cursor_icon_menu_handle_is_sign_extended_int) {
    CIM_NEED();
    HMENU h; if (!mk(&h, 0)) return;
    long long v = (long long)(ULONG_PTR)h;
    EXPECT_EQ(v, (long long)(int)(unsigned)(ULONG_PTR)h); /* the handle allocator returns (int)(uniq<<16|index) */
    CHECK(CIM_UNIQ(h) != 0);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_ismenu_true_while_alive) {
    CIM_GUI();
    HMENU h; if (!mk(&h, 0)) return;
    CHECK(U(IsMenu)(h));
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_destroy_returns_true_then_ismenu_false) {
    CIM_GUI();
    HMENU h; if (!mk(&h, 1)) return;
    BOOL r = U(DestroyMenu)(h);
    EXPECT_EQ(r, 1);
    CHECK(!U(IsMenu)(h));
    CHECK(!cim_entry_is(h, CT_MENU));
}
TEST(cursor_icon_menu_destroy_frees_entry_and_bumps_uniq) {
    CIM_NEED();
    HMENU h; if (!mk(&h, 0)) return;
    cim_he before, after;
    CHECK(cim_he_read(h, &before));
    CHECK(U(DestroyMenu)(h));
    CHECK(cim_he_read(h, &after));
    /* DestroyMenu -> the handle free path: entry type cleared; uniq becomes the successor (1 instead of 0xffff) */
    if (after.type == 0) EXPECT_EQ(after.uniq, cim_next_uniq(before.uniq));
    else t_note("entry index reused already by type %u", after.type);
}
TEST(cursor_icon_menu_second_destroy_fails_invalid_menu_handle) {
    CIM_GUI();
    REQUIRE(UN(NtUserDestroyMenu));
    HMENU h; if (!mk(&h, 0)) return;
    CHECK(U(DestroyMenu)(h));
    BOOL r; CIM_CALL_ERR(r, UN(NtUserDestroyMenu)(h));
    EXPECT_FAIL(r == 0, E_INVALID_MENU_HANDLE); /* handle validation -> 0x579 */
}
TEST(cursor_icon_menu_userland_second_destroy_fails) {
    CIM_GUI();
    HMENU h; if (!mk(&h, 0)) return;
    CHECK(U(DestroyMenu)(h));
    BOOL r; CIM_CALL_ERR(r, U(DestroyMenu)(h));
    EXPECT_FAIL(r == 0, E_INVALID_MENU_HANDLE);
}
TEST(cursor_icon_menu_bar_flags_word_is_zero) {
    CIM_NEED();
    HMENU h; if (!mk(&h, 0)) return;
    struct cim_menu m;
    if (!cim_menu_read(h, &m)) { U(DestroyMenu)(h); t_skip("client view of the menu not mappable"); return; }
    EXPECT_EQ(m.flags, 0); /* InternalCreateMenu(0) leaves the shared flag word untouched (zero) */
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_popup_flags_word_is_one) {
    CIM_NEED();
    HMENU h; if (!mk(&h, 1)) return;
    struct cim_menu m;
    if (!cim_menu_read(h, &m)) { U(DestroyMenu)(h); t_skip("client view of the menu not mappable"); return; }
    EXPECT_EQ(m.flags, 1); /* CreateMenu: the shared flags word = 1 (popup bit) */
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_initial_shared_fields_zero) {
    CIM_NEED();
    for (int popup = 0; popup < 2; popup++) {
        HMENU h; if (!mk(&h, popup)) return;
        struct cim_menu m;
        if (!cim_menu_read(h, &m)) { U(DestroyMenu)(h); t_skip("client view not mappable"); return; }
        EXPECT_EQ(m.citems, 0);
        EXPECT_EQ(m.rgitems, 0);
        EXPECT_EQ(m.cymax, 0);
        EXPECT_EQ(m.helpid, 0);
        EXPECT_EQ(m.menudata, 0);
        EXPECT_EQ(m.hbr, 0);
        U(DestroyMenu)(h);
    }
}
TEST(cursor_icon_menu_client_head_is_handle_and_heap_offset) {
    CIM_NEED();
    HMENU h; if (!mk(&h, 0)) return;
    void *p = cim_menu_ptr(h);
    if (!p) { U(DestroyMenu)(h); t_skip("fast path not usable"); return; }
    unsigned long long q[2]; cim_he e;
    CHECK(k_try_read(p, q, sizeof q) && cim_he_read(h, &e));
    EXPECT_EQ((unsigned)q[0], (unsigned)(ULONG_PTR)h);
    EXPECT_EQ(q[1], e.delta);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_getmenuitemcount_zero_for_new) {
    CIM_GUI();
    for (int popup = 0; popup < 2; popup++) {
        HMENU h; if (!mk(&h, popup)) return;
        EXPECT_EQ(U(GetMenuItemCount)(h), 0);
        U(DestroyMenu)(h);
    }
}
TEST(cursor_icon_menu_gui_resources_user_objects_exact) {
    CIM_GUI();
    DWORD base = cim_gr(GR_USEROBJECTS);
    HMENU a, b;
    if (!mk(&a, 0) || !mk(&b, 1)) return;
    EXPECT_EQ(cim_gr(GR_USEROBJECTS), base + 2);
    U(DestroyMenu)(a);
    EXPECT_EQ(cim_gr(GR_USEROBJECTS), base + 1);
    U(DestroyMenu)(b);
    EXPECT_EQ(cim_gr(GR_USEROBJECTS), base);
}
TEST(cursor_icon_menu_table_owned_menu_count_tracks_create) {
    CIM_NEED();
    unsigned base = cim_count_owned(CT_MENU);
    HMENU hs[16];
    for (int i = 0; i < 16; i++) { hs[i] = (i & 1) ? CREATE_POPUP() : CREATE_BAR(); if (!hs[i]) { t_skip("create failed"); return; } }
    EXPECT_EQ(cim_count_owned(CT_MENU), base + 16);
    for (int i = 0; i < 16; i++) U(DestroyMenu)(hs[i]);
    EXPECT_EQ(cim_count_owned(CT_MENU), base);
}
TEST(cursor_icon_menu_many_distinct_handles) {
    CIM_NEED();
    enum { N = 300 };
    static HMENU hs[N];
    int n = 0;
    for (; n < N; n++) { hs[n] = (n & 1) ? CREATE_POPUP() : CREATE_BAR(); if (!hs[n]) break; }
    EXPECT_EQ(n, N);
    int dup = 0, bad = 0;
    for (int i = 0; i < n; i++) {
        if (!cim_entry_is(hs[i], CT_MENU)) bad++;
        for (int j = i + 1; j < n; j++) if (hs[i] == hs[j]) dup++;
    }
    EXPECT_EQ(dup, 0);
    EXPECT_EQ(bad, 0);
    for (int i = 0; i < n; i++) U(DestroyMenu)(hs[i]);
}
TEST(cursor_icon_menu_reuse_slot_gets_new_uniq) {
    CIM_NEED();
    HMENU a; if (!mk(&a, 0)) return;
    cim_he ea; CHECK(cim_he_read(a, &ea));
    U(DestroyMenu)(a);
    HMENU b = CREATE_BAR();
    if (!b) { t_skip("create failed"); return; }
    if (CIM_IDX(b) == CIM_IDX(a)) {
        /* same slot: the handle free path advanced the uniq, so the stale handle must not alias the new object */
        EXPECT_EQ(CIM_UNIQ(b), cim_next_uniq(ea.uniq));
        CHECK(a != b);
        CHECK(!U(IsMenu)(a));
        CHECK(U(IsMenu)(b));
    } else t_note("slot not reused immediately (index %u -> %u)", CIM_IDX(a), CIM_IDX(b));
    U(DestroyMenu)(b);
}
TEST(cursor_icon_menu_nt_callnoparam_creates_bar) {
    CIM_NEED();
    REQUIRE(UN(NtUserCallNoParam));
    HMENU h = (HMENU)UN(NtUserCallNoParam)(0); /* user32!CreateMenu */
    if (!h) { t_skip("NtUserCallNoParam(0) returned NULL err %lu", GetLastError()); return; }
    entry_checks(h);
    CHECK(U(IsMenu)(h));
    struct cim_menu m;
    if (cim_menu_read(h, &m)) EXPECT_EQ(m.flags, 0);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_nt_callnoparam_creates_popup) {
    CIM_NEED();
    REQUIRE(UN(NtUserCallNoParam));
    HMENU h = (HMENU)UN(NtUserCallNoParam)(1); /* user32!CreatePopupMenu */
    if (!h) { t_skip("NtUserCallNoParam(1) returned NULL err %lu", GetLastError()); return; }
    entry_checks(h);
    struct cim_menu m;
    if (cim_menu_read(h, &m)) EXPECT_EQ(m.flags, 1);
    U(DestroyMenu)(h);
}
TEST(cursor_icon_menu_nt_destroymenu_matches_userland) {
    CIM_GUI();
    REQUIRE(UN(NtUserDestroyMenu));
    HMENU h; if (!mk(&h, 0)) return;
    EXPECT_EQ(UN(NtUserDestroyMenu)(h), 1);
    CHECK(!U(IsMenu)(h));
}

/* ---- bad handle matrix: every menu entry point rejects non-menu handles with 0x579 ---- */
struct bad { const char *name; HANDLE h; };
static int make_bads(struct bad *b, HWND *wnd, HICON *icon) {
    int n = 0;
    HMENU dead = CREATE_BAR();
    if (dead) { U(DestroyMenu)(dead); b[n].name = "destroyed"; b[n++].h = dead; }
    HMENU live = CREATE_BAR();
    if (live) { b[n].name = "wrong uniq"; b[n++].h = cim_forge(live, CIM_UNIQ(live) ^ 0x5a5a); U(DestroyMenu)(live); }
    b[n].name = "null"; b[n++].h = NULL;
    b[n].name = "index only 1"; b[n++].h = (HANDLE)(ULONG_PTR)1;
    b[n].name = "index only 0x7fff"; b[n++].h = (HANDLE)(ULONG_PTR)0x7fff;
    *wnd = cim_make_top();
    if (*wnd) { b[n].name = "window"; b[n++].h = *wnd; }
    *icon = cim_mk_icon(16, 16, 0xff, 0);
    if (*icon) { b[n].name = "cursor-type"; b[n++].h = *icon; }
    return n;
}
static void free_bads(HWND w, HICON i) { if (w) U(DestroyWindow)(w); if (i) U(DestroyIcon)(i); }

#define BAD_LOOP(body) do { struct bad b[8]; HWND w; HICON ic; int n = make_bads(b, &w, &ic); \
    for (int i = 0; i < n; i++) { HMENU m = (HMENU)b[i].h; (void)m; const char *nm = b[i].name; (void)nm; body } free_bads(w, ic); } while (0)

TEST(cursor_icon_menu_bad_handle_ismenu_false) { CIM_GUI(); BAD_LOOP(CHECKF(!U(IsMenu)(m), "%s", nm);); }
TEST(cursor_icon_menu_bad_handle_getmenuitemcount_minus1) { CIM_GUI(); BAD_LOOP(CHECKF(U(GetMenuItemCount)(m) == -1, "%s", nm);); }
TEST(cursor_icon_menu_bad_handle_nt_destroymenu) {
    CIM_GUI(); REQUIRE(UN(NtUserDestroyMenu));
    BAD_LOOP(BOOL r; CIM_CALL_ERR(r, UN(NtUserDestroyMenu)(m)); CHECKF(r == 0, "%s", nm); CHECKF(GetLastError() == E_INVALID_MENU_HANDLE, "%s err %lu", nm, GetLastError()););
}
TEST(cursor_icon_menu_bad_handle_nt_deletemenu) {
    CIM_GUI(); REQUIRE(UN(NtUserDeleteMenu));
    BAD_LOOP(BOOL r; CIM_CALL_ERR(r, UN(NtUserDeleteMenu)(m, 0, 0)); CHECKF(r == 0, "%s", nm); CHECKF(GetLastError() == E_INVALID_MENU_HANDLE, "%s err %lu", nm, GetLastError()););
}
TEST(cursor_icon_menu_bad_handle_nt_removemenu) {
    CIM_GUI(); REQUIRE(UN(NtUserRemoveMenu));
    BAD_LOOP(BOOL r; CIM_CALL_ERR(r, UN(NtUserRemoveMenu)(m, 0, 0)); CHECKF(r == 0, "%s", nm); CHECKF(GetLastError() == E_INVALID_MENU_HANDLE, "%s err %lu", nm, GetLastError()););
}
TEST(cursor_icon_menu_bad_handle_nt_checkmenuitem) {
    CIM_GUI(); REQUIRE(UN(NtUserCheckMenuItem));
    BAD_LOOP(DWORD r; CIM_CALL_ERR(r, UN(NtUserCheckMenuItem)(m, 1, MF_CHECKED)); CHECKF(r == 0xffffffff, "%s got 0x%lx", nm, r); CHECKF(GetLastError() == E_INVALID_MENU_HANDLE, "%s err %lu", nm, GetLastError()););
}
TEST(cursor_icon_menu_bad_handle_nt_enablemenuitem) {
    CIM_GUI(); REQUIRE(UN(NtUserEnableMenuItem));
    BAD_LOOP(DWORD r; CIM_CALL_ERR(r, UN(NtUserEnableMenuItem)(m, 1, MF_GRAYED)); CHECKF(r == 0xffffffff, "%s got 0x%lx", nm, r); CHECKF(GetLastError() == E_INVALID_MENU_HANDLE, "%s err %lu", nm, GetLastError()););
}
TEST(cursor_icon_menu_bad_handle_nt_setdefaultitem) {
    CIM_GUI(); REQUIRE(UN(NtUserSetMenuDefaultItem));
    BAD_LOOP(BOOL r; CIM_CALL_ERR(r, UN(NtUserSetMenuDefaultItem)(m, 1, 0)); CHECKF(r == 0, "%s", nm); CHECKF(GetLastError() == E_INVALID_MENU_HANDLE, "%s err %lu", nm, GetLastError()););
}
TEST(cursor_icon_menu_bad_handle_nt_thunkedmenuinfo) {
    CIM_GUI(); REQUIRE(UN(NtUserThunkedMenuInfo));
    MENUINFO mi; memset(&mi, 0, sizeof mi); mi.cbSize = sizeof mi; mi.fMask = MIM_MAXHEIGHT; mi.cyMax = 10;
    BAD_LOOP(BOOL r; CIM_CALL_ERR(r, UN(NtUserThunkedMenuInfo)(m, &mi)); CHECKF(r == 0, "%s", nm); CHECKF(GetLastError() == E_INVALID_MENU_HANDLE, "%s err %lu", nm, GetLastError()););
}
TEST(cursor_icon_menu_bad_handle_nt_thunkedmenuiteminfo) {
    CIM_GUI(); REQUIRE(UN(NtUserThunkedMenuItemInfo));
    MENUITEMINFOW mii; memset(&mii, 0, sizeof mii); mii.cbSize = 0x50; mii.fMask = MIIM_ID; mii.wID = 5;
    BAD_LOOP(for (int ins = 0; ins < 2; ins++) { BOOL r; CIM_CALL_ERR(r, UN(NtUserThunkedMenuItemInfo)(m, 0, TRUE, ins, &mii, NULL)); CHECKF(r == 0, "%s ins=%d", nm, ins); CHECKF(GetLastError() == E_INVALID_MENU_HANDLE, "%s err %lu", nm, GetLastError()); });
}
TEST(cursor_icon_menu_bad_handle_nt_hilite) {
    CIM_GUI(); REQUIRE(UN(NtUserHiliteMenuItem));
    HWND w = cim_make_top(); if (!w) { t_skip("no window"); return; }
    struct bad b[8]; HWND w2; HICON ic; int n = make_bads(b, &w2, &ic);
    for (int i = 0; i < n; i++) {
        BOOL r; CIM_CALL_ERR(r, UN(NtUserHiliteMenuItem)(w, (HMENU)b[i].h, 0, MF_BYPOSITION | MF_HILITE));
        CHECKF(r == 0, "%s", b[i].name);
        CHECKF(GetLastError() == E_INVALID_MENU_HANDLE, "%s err %lu", b[i].name, GetLastError());
    }
    free_bads(w2, ic);
    U(DestroyWindow)(w);
}
TEST(cursor_icon_menu_bad_handle_nt_setmenu_menu_arg) {
    CIM_GUI(); REQUIRE(UN(NtUserSetMenu));
    HWND w = cim_make_top(); if (!w) { t_skip("no window"); return; }
    struct bad b[8]; HWND w2; HICON ic; int n = make_bads(b, &w2, &ic);
    for (int i = 0; i < n; i++) {
        if (!b[i].h) continue; /* NULL means "remove the menu" and is legal */
        BOOL r; CIM_CALL_ERR(r, UN(NtUserSetMenu)(w, (HMENU)b[i].h, FALSE));
        CHECKF(r == 0, "%s", b[i].name);
        CHECKF(GetLastError() == E_INVALID_MENU_HANDLE, "%s err %lu", b[i].name, GetLastError());
        CHECKF(U(GetMenu)(w) == NULL, "%s", b[i].name);
    }
    free_bads(w2, ic);
    U(DestroyWindow)(w);
}
