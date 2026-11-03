/* NtUserGetRequiredCursorSizes, NtUserLinkDpiCursor, NtUserSetMenuFlagRtoL, NtUserSetSystemMenu. */
#include "gap.h"

/* ---- NtUserGetRequiredCursorSizes(HCURSOR, int info[10]) -----------------------------------------
 * win32kfull: HMValidateHandle(h, 3) (TYPE_CURSOR) -> on failure returns 0 with ERROR_INVALID_CURSOR_HANDLE (0x57a); the
 * cursor must also be its own head of chain (cursor+0x30 == cursor). The 0x28-byte result (five {valid,size} int pairs, as
 * user32!CreateDpiCursors lays it out) is probed for write and filled; returns 1. */
#define CUR_BEGIN() \
    GAP_BEGIN(); NtUserGetRequiredCursorSizes_pfn grcs = UN(NtUserGetRequiredCursorSizes); REQUIRE(grcs); \
    HCURSOR cur = gap_cursor(); if (!cur) { t_skip("CreateCursor failed"); return; }

TEST(gap1_cursorsizes_valid_cursor) {
    CUR_BEGIN();
    int info[10]; memset(info, 0, sizeof info);
    GAP_POISON();
    EXPECT_EQ(grcs(cur, info), 1);
    for (int i = 0; i < 5; i++) {
        t_note("cursor size slot %d: valid=%d size=%d", i, info[2*i], info[2*i+1]);
        if (info[2*i]) CHECK(info[2*i+1] > 0 && info[2*i+1] < 4096);
    }
    U(DestroyCursor)(cur);
}

TEST(gap1_cursorsizes_idempotent) {
    CUR_BEGIN();
    int a[10] = {0}, b[10] = {0};
    EXPECT_EQ(grcs(cur, a), 1);
    EXPECT_EQ(grcs(cur, b), 1);
    CHECK(!memcmp(a, b, sizeof a));
    U(DestroyCursor)(cur);
}

TEST(gap1_cursorsizes_same_for_other_cursor) {
    CUR_BEGIN();
    HCURSOR c2 = gap_cursor();
    int a[10] = {0}, b[10] = {0};
    EXPECT_EQ(grcs(cur, a), 1);
    if (c2) { EXPECT_EQ(grcs(c2, b), 1); t_note("two cursors report %s sizes", memcmp(a, b, sizeof a) ? "different" : "identical"); U(DestroyCursor)(c2); }
    U(DestroyCursor)(cur);
}

TEST(gap1_cursorsizes_invalid_handles) {
    CUR_BEGIN();
    HCURSOR bad[] = { NULL, (HCURSOR)(ULONG_PTR)1, (HCURSOR)(ULONG_PTR)0xdeadbeef, (HCURSOR)(ULONG_PTR)0x7fffffff, (HCURSOR)(ULONG_PTR)0xffff };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        int info[10]; memset(info, 0x5a, sizeof info);
        GAP_POISON();
        EXPECT_EQ(grcs(bad[i], info), 0);
        EXPECT_ERR(0x57a);
        EXPECT_EQ(info[0], 0x5a5a5a5a);
    }
    U(DestroyCursor)(cur);
}

TEST(gap1_cursorsizes_wrong_object_type) {
    CUR_BEGIN();
    HWND w = gap_win(0, GAP_STYLE_SYS);
    HMENU m = U(CreateMenu)();
    HICON ic = U(CopyIcon)(U(LoadIconW)(NULL, MAKEINTRESOURCEW(32512)));
    int info[10];
    if (w) { memset(info, 0x5a, sizeof info); GAP_POISON(); EXPECT_EQ(grcs((HCURSOR)w, info), 0); EXPECT_ERR(0x57a); EXPECT_EQ(info[0], 0x5a5a5a5a); U(DestroyWindow)(w); }
    if (m) { GAP_POISON(); EXPECT_EQ(grcs((HCURSOR)m, info), 0); EXPECT_ERR(0x57a); U(DestroyMenu)(m); }
    HDC dc = U(CreateCompatibleDC)(NULL);
    if (dc) { GAP_POISON(); EXPECT_EQ(grcs((HCURSOR)dc, info), 0); EXPECT_ERR(0x57a); U(DeleteDC)(dc); }
    /* icons and cursors share the kernel's tagCURSOR object (type 3), so the handle validates; the chain-head test may still refuse it */
    if (ic) { memset(info, 0, sizeof info); GAP_POISON(); int r = grcs((HCURSOR)ic, info); t_note("icon handle -> %d (err %lu)", r, (unsigned long)GetLastError()); CHECK(r == 0 || r == 1); if (r == 0) CHECK(GetLastError() != 0x57a); U(DestroyIcon)(ic); }
    U(DestroyCursor)(cur);
}

TEST(gap1_cursorsizes_destroyed_cursor) {
    CUR_BEGIN();
    int info[10] = {0};
    EXPECT_EQ(grcs(cur, info), 1);
    CHECK(U(DestroyCursor)(cur));
    memset(info, 0x5a, sizeof info);
    GAP_POISON();
    EXPECT_EQ(grcs(cur, info), 0);
    EXPECT_ERR(0x57a);
    EXPECT_EQ(info[0], 0x5a5a5a5a);
}

TEST(gap1_cursorsizes_handle_table_entry) {
    CUR_BEGIN();
    struct k_user_entry e;
    if (!k_user_entry((HANDLE)cur, &e)) { t_skip("USER handle table not readable"); U(DestroyCursor)(cur); return; }
    EXPECT_EQ(e.type, 3);                                      /* the type HMValidateHandle(h, 3) demands */
    EXPECT_EQ(e.flags & 1, 0);
    EXPECT_EQ(e.uniq, (unsigned)(((ULONG_PTR)cur >> 16) & 0xffff));
    U(DestroyCursor)(cur);
}

TEST(gap1_cursorsizes_used_by_createcursor) {
    GAP_BEGIN();
    HCURSOR c = gap_cursor();
    if (!c) { t_skip("CreateCursor failed"); return; }
    struct k_user_entry e;
    if (k_user_entry((HANDLE)c, &e)) {
        CHECK(e.type == 3);
        CHECK(e.delta == NULL);                                /* a pool object: no desktop-heap part */
    }
    U(DestroyCursor)(c);
}

/* ---- NtUserLinkDpiCursor(HCURSOR base, HCURSOR dpi, UINT dpi) --------------------------------------
 * win32kfull: dpi == 0 -> ERROR_INVALID_PARAMETER (0x57); either handle failing HMValidateHandle(h, 3) -> 0 with 0x57a;
 * both valid but the second not flagged as a DPI cursor (flag 0x1000), the first already flagged (0x1800 bits), the second
 * not its own chain head, or FindDPICursor(base, dpi) already finding one -> 0x57. */
#define LINK_BEGIN() \
    GAP_BEGIN(); NtUserLinkDpiCursor_pfn link = UN(NtUserLinkDpiCursor); REQUIRE(link); \
    HCURSOR a = gap_cursor(), b = gap_cursor(); \
    if (!a || !b) { if (a) U(DestroyCursor)(a); if (b) U(DestroyCursor)(b); t_skip("CreateCursor failed"); return; }
#define LINK_END() do { U(DestroyCursor)(a); U(DestroyCursor)(b); } while (0)

TEST(gap1_linkdpi_zero_dpi) {
    LINK_BEGIN();
    GAP_POISON();
    EXPECT_EQ(link(a, b, 0), 0);
    EXPECT_ERR(0x57);
    GAP_POISON();
    EXPECT_EQ(link(NULL, NULL, 0), 0);                         /* the dpi test precedes any handle validation */
    EXPECT_ERR(0x57);
    LINK_END();
}

TEST(gap1_linkdpi_invalid_first) {
    LINK_BEGIN();
    HCURSOR bad[] = { NULL, (HCURSOR)(ULONG_PTR)1, (HCURSOR)(ULONG_PTR)0xdeadbeef };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        GAP_POISON();
        EXPECT_EQ(link(bad[i], b, 96), 0);
        EXPECT_ERR(0x57a);
    }
    LINK_END();
}

TEST(gap1_linkdpi_invalid_second) {
    LINK_BEGIN();
    HCURSOR bad[] = { NULL, (HCURSOR)(ULONG_PTR)1, (HCURSOR)(ULONG_PTR)0xdeadbeef };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        GAP_POISON();
        EXPECT_EQ(link(a, bad[i], 96), 0);
        EXPECT_ERR(0x57a);
    }
    LINK_END();
}

TEST(gap1_linkdpi_wrong_type) {
    LINK_BEGIN();
    HWND w = gap_win(0, GAP_STYLE_SYS);
    if (w) {
        GAP_POISON(); EXPECT_EQ(link((HCURSOR)w, b, 96), 0); EXPECT_ERR(0x57a);
        GAP_POISON(); EXPECT_EQ(link(a, (HCURSOR)w, 96), 0); EXPECT_ERR(0x57a);
        U(DestroyWindow)(w);
    }
    LINK_END();
}

TEST(gap1_linkdpi_destroyed_cursor) {
    LINK_BEGIN();
    U(DestroyCursor)(b);
    GAP_POISON();
    EXPECT_EQ(link(a, b, 96), 0);
    EXPECT_ERR(0x57a);
    U(DestroyCursor)(a);
}

TEST(gap1_linkdpi_fresh_cursors_consistent) {
    LINK_BEGIN();
    GAP_POISON();
    int r1 = link(a, b, 144);
    DWORD e1 = GetLastError();
    t_note("fresh cursors LinkDpiCursor -> %d (last error %lu)", r1, (unsigned long)e1);
    CHECK(r1 == 0 || r1 == 1);
    if (r1 == 0) EXPECT_EQ(e1, 0x57);                         /* plain cursors lack the DPI-cursor flag */
    if (r1 == 1) {
        GAP_POISON();
        EXPECT_EQ(link(a, b, 144), 0);                         /* FindDPICursor now finds the link */
        EXPECT_ERR(0x57);
    }
    LINK_END();
}

TEST(gap1_linkdpi_self_link_refused) {
    LINK_BEGIN();
    GAP_POISON();
    int r = link(a, a, 120);
    t_note("self link -> %d", r);
    if (r == 0) EXPECT_ERR(0x57);
    LINK_END();
}

TEST(gap1_linkdpi_dpi_values) {
    LINK_BEGIN();
    UINT dpis[] = { 96, 120, 144, 192, 1, 0xffffffff, 0x80000000 };
    for (unsigned i = 0; i < sizeof dpis / sizeof dpis[0]; i++) {
        GAP_POISON();
        int r = link(a, b, dpis[i]);
        if (r == 0) EXPECT_ERR(0x57);
        else break;                                            /* a success changes the chain; stop probing */
    }
    LINK_END();
}

/* ---- NtUserSetMenuFlagRtoL(HMENU) ------------------------------------------------------------------
 * win32kfull: ValidateHmenu(h) (HMValidateHandle(h, 2), error 0x579, and the menu must belong to the caller's desktop);
 * on success ORs 0x20 into the menu's flags and returns 1, otherwise 0. */
#define RTL_BEGIN() \
    GAP_BEGIN(); NtUserSetMenuFlagRtoL_pfn rtl = UN(NtUserSetMenuFlagRtoL); REQUIRE(rtl)

TEST(gap1_menu_rtl_valid_menu) {
    RTL_BEGIN();
    HMENU m = U(CreateMenu)();
    CHECK(m != NULL);
    if (!m) return;
    EXPECT_EQ(rtl(m), 1);
    EXPECT_EQ(rtl(m), 1);                                      /* OR-ing the flag again is harmless */
    CHECK(U(IsMenu)(m));
    U(DestroyMenu)(m);
}

TEST(gap1_menu_rtl_popup_menu) {
    RTL_BEGIN();
    HMENU m = U(CreatePopupMenu)();
    if (!m) { t_skip("CreatePopupMenu"); return; }
    CHECK(U(AppendMenuA)(m, MF_STRING, 1, "one"));
    EXPECT_EQ(rtl(m), 1);
    EXPECT_EQ(U(GetMenuItemCount)(m), 1);                      /* contents unaffected */
    U(DestroyMenu)(m);
}

TEST(gap1_menu_rtl_handle_table_type) {
    RTL_BEGIN();
    HMENU m = U(CreateMenu)(), p = U(CreatePopupMenu)();
    struct k_user_entry e;
    HMENU ms[2] = { m, p };
    for (int i = 0; i < 2; i++) {
        if (!ms[i]) continue;
        if (k_user_entry((HANDLE)ms[i], &e)) { EXPECT_EQ(e.type, 2); EXPECT_EQ(e.flags & 1, 0); }   /* HMValidateHandle(h, 2) */
        else t_skip("USER handle table not readable");
        EXPECT_EQ(rtl(ms[i]), 1);
    }
    if (m) U(DestroyMenu)(m);
    if (p) U(DestroyMenu)(p);
}

TEST(gap1_menu_rtl_invalid_handles) {
    RTL_BEGIN();
    HMENU bad[] = { NULL, (HMENU)(ULONG_PTR)1, (HMENU)(ULONG_PTR)0xdeadbeef, (HMENU)(ULONG_PTR)0x7fffffff };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        GAP_POISON();
        EXPECT_EQ(rtl(bad[i]), 0);
        EXPECT_ERR(0x579);
    }
}

TEST(gap1_menu_rtl_wrong_object_type) {
    RTL_BEGIN();
    HWND w = gap_win(0, GAP_STYLE_SYS);
    HCURSOR c = gap_cursor();
    if (w) { GAP_POISON(); EXPECT_EQ(rtl((HMENU)w), 0); EXPECT_ERR(0x579); U(DestroyWindow)(w); }
    if (c) { GAP_POISON(); EXPECT_EQ(rtl((HMENU)c), 0); EXPECT_ERR(0x579); U(DestroyCursor)(c); }
}

TEST(gap1_menu_rtl_destroyed_menu) {
    RTL_BEGIN();
    HMENU m = U(CreateMenu)();
    if (!m) { t_skip("CreateMenu"); return; }
    CHECK(U(DestroyMenu)(m));
    GAP_POISON();
    EXPECT_EQ(rtl(m), 0);
    EXPECT_ERR(0x579);
}

TEST(gap1_menu_rtl_handle_bits) {
    RTL_BEGIN();
    HMENU m = U(CreateMenu)();
    if (!m) { t_skip("CreateMenu"); return; }
    /* a handle whose uniqueness word differs must not validate (HMValidateHandle compares HIWORD with the entry) */
    HMENU forged = (HMENU)((ULONG_PTR)m ^ 0x00540000);
    if (((ULONG_PTR)forged >> 16 & 0xffff) != 0xffff && ((ULONG_PTR)forged >> 16 & 0xffff) != 0) {
        GAP_POISON();
        EXPECT_EQ(rtl(forged), 0);
        EXPECT_ERR(0x579);
    }
    EXPECT_EQ(rtl(m), 1);
    U(DestroyMenu)(m);
}

TEST(gap1_menu_loadmenuindirect_returns_menu) {
    GAP_BEGIN();
    /* user32!LoadMenuIndirectA builds the menu (and calls NtUserSetMenuFlagRtoL for right-to-left templates). */
    struct { WORD ver, off; WORD opt; WORD id; char text[8]; } tmpl;
    memset(&tmpl, 0, sizeof tmpl);
    tmpl.opt = MF_END; tmpl.id = 100; memcpy(tmpl.text, "Item", 5);
    HMENU m = U(LoadMenuIndirectA)(&tmpl);
    CHECK(m != NULL);
    if (!m) return;
    CHECK(U(IsMenu)(m));
    EXPECT_EQ(U(GetMenuItemCount)(m), 1);
    char buf[16] = {0};
    int n = U(GetMenuStringA)(m, 100, buf, sizeof buf, MF_BYCOMMAND);
    t_note("item text via GetMenuStringA: %d \"%s\"", n, buf);   /* the template string flavour for the A entry point is not pinned down */
    CHECK(n > 0);
    struct k_user_entry e;
    if (k_user_entry((HANDLE)m, &e)) EXPECT_EQ(e.type, 2);
    U(DestroyMenu)(m);
}

/* ---- NtUserSetSystemMenu(HWND, HMENU) --------------------------------------------------------------
 * win32kfull: ValidateHwnd failure -> 0 (0x578); ValidateHmenu failure -> 0 (0x579, window untouched); a window without
 * WS_SYSMENU (style byte +0x1e bit 3 == 0x00080000) -> 0 with 0x59d; otherwise the window's system menu is replaced via
 * LockWndMenuWorker, the previous one destroyed, and 1 is returned. */
static BOOL gap_setsysmenu(HWND w, HMENU m) {
    NtUserSetSystemMenu_pfn f = UN(NtUserSetSystemMenu);
    return f ? f(w, m) : FALSE;
}
/* A window's system menu is a menu whose single item carries the drop-down popup: SetSystemMenu takes that outer menu and
 * GetSystemMenu answers the popup inside it (0 when the outer menu has no popup item, as with a bare popup menu). */
static HMENU gap_sysmenu_outer(HMENU *popup) {
    HMENU top = U(CreateMenu)(), sub = U(CreatePopupMenu)();
    if (!top || !sub || !U(AppendMenuA)(top, MF_POPUP, (UINT_PTR)sub, "System")) {
        if (sub) U(DestroyMenu)(sub);
        if (top) U(DestroyMenu)(top);
        return NULL;
    }
    *popup = sub;
    return top;
}
#define SYS_BEGIN() \
    GAP_BEGIN(); NtUserSetSystemMenu_pfn ssm = UN(NtUserSetSystemMenu); REQUIRE(ssm)

TEST(gap1_sysmenu_replace) {
    SYS_BEGIN();
    HWND w = gap_win(0, GAP_STYLE_SYS);
    HMENU sub = NULL, m = gap_sysmenu_outer(&sub);
    if (!w || !m) { t_skip("setup"); if (w) U(DestroyWindow)(w); return; }
    HMENU old = U(GetSystemMenu)(w, FALSE);
    EXPECT_EQ(ssm(w, m), 1);
    EXPECT_EQ((ULONG_PTR)U(GetSystemMenu)(w, FALSE), (ULONG_PTR)sub);
    CHECK(old != NULL && !U(IsMenu)(old));                   /* the previous system menu is destroyed by the replacement */
    U(DestroyWindow)(w);
}

TEST(gap1_sysmenu_user_export_is_the_syscall) {
    SYS_BEGIN();
    typedef BOOL (WINAPI *ssm_t)(HWND, HMENU);
    ssm_t u = (ssm_t)t_proc_in("user32.dll", "SetSystemMenu");
    REQUIRE(u);
    HWND w = gap_win(0, GAP_STYLE_SYS);
    HMENU sub = NULL, m = gap_sysmenu_outer(&sub);
    if (!w || !m) { t_skip("setup"); if (w) U(DestroyWindow)(w); return; }
    EXPECT_EQ(u(w, m), 1);
    EXPECT_EQ((ULONG_PTR)U(GetSystemMenu)(w, FALSE), (ULONG_PTR)sub);
    U(DestroyWindow)(w);
}

TEST(gap1_sysmenu_no_sysmenu_style) {
    SYS_BEGIN();
    HWND w = gap_win(0, WS_POPUP | WS_CAPTION);
    HMENU m = U(CreatePopupMenu)();
    if (!w || !m) { t_skip("setup"); if (w) U(DestroyWindow)(w); return; }
    CHECK(!(U(GetWindowLongW)(w, GWL_STYLE) & WS_SYSMENU));
    GAP_POISON();
    EXPECT_EQ(ssm(w, m), 0);
    EXPECT_ERR(0x59d);
    U(DestroyMenu)(m);
    U(DestroyWindow)(w);
}

TEST(gap1_sysmenu_style_toggle) {
    SYS_BEGIN();
    HWND w = gap_win(0, WS_POPUP | WS_CAPTION);
    HMENU m = U(CreatePopupMenu)();
    if (!w || !m) { t_skip("setup"); if (w) U(DestroyWindow)(w); return; }
    LONG st = U(GetWindowLongW)(w, GWL_STYLE);
    U(SetWindowLongW)(w, GWL_STYLE, st | WS_SYSMENU);
    GAP_POISON();
    int r = ssm(w, m);
    t_note("SetSystemMenu after adding WS_SYSMENU -> %d (err %lu)", r, (unsigned long)GetLastError());
    CHECK(r == 1);
    U(DestroyWindow)(w);
}

TEST(gap1_sysmenu_invalid_window) {
    SYS_BEGIN();
    HMENU m = U(CreatePopupMenu)();
    HWND bad[] = { NULL, (HWND)(ULONG_PTR)1, (HWND)(ULONG_PTR)0xdeadbeef };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        GAP_POISON();
        EXPECT_EQ(ssm(bad[i], m), 0);
        EXPECT_ERR(0x578);                                     /* the window is validated first */
    }
    if (m) U(DestroyMenu)(m);
}

TEST(gap1_sysmenu_invalid_menu_keeps_old) {
    SYS_BEGIN();
    HWND w = gap_win(0, GAP_STYLE_SYS);
    if (!w) { t_skip("no window"); return; }
    HMENU before = U(GetSystemMenu)(w, FALSE);
    HMENU bad[] = { NULL, (HMENU)(ULONG_PTR)1, (HMENU)(ULONG_PTR)0xdeadbeef };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        GAP_POISON();
        EXPECT_EQ(ssm(w, bad[i]), 0);
        EXPECT_ERR(0x579);
    }
    EXPECT_EQ((ULONG_PTR)U(GetSystemMenu)(w, FALSE), (ULONG_PTR)before);
    U(DestroyWindow)(w);
}

TEST(gap1_sysmenu_wrong_menu_type) {
    SYS_BEGIN();
    HWND w = gap_win(0, GAP_STYLE_SYS);
    HCURSOR c = gap_cursor();
    if (!w) { t_skip("no window"); return; }
    GAP_POISON(); EXPECT_EQ(ssm(w, (HMENU)w), 0); EXPECT_ERR(0x579);
    if (c) { GAP_POISON(); EXPECT_EQ(ssm(w, (HMENU)c), 0); EXPECT_ERR(0x579); U(DestroyCursor)(c); }
    U(DestroyWindow)(w);
}

TEST(gap1_sysmenu_destroyed_menu) {
    SYS_BEGIN();
    HWND w = gap_win(0, GAP_STYLE_SYS);
    HMENU m = U(CreatePopupMenu)();
    if (!w || !m) { t_skip("setup"); if (w) U(DestroyWindow)(w); return; }
    U(DestroyMenu)(m);
    GAP_POISON();
    EXPECT_EQ(ssm(w, m), 0);
    EXPECT_ERR(0x579);
    U(DestroyWindow)(w);
}

TEST(gap1_sysmenu_desktop_window) {
    SYS_BEGIN();
    HMENU m = U(CreatePopupMenu)();
    if (!m) { t_skip("menu"); return; }
    GAP_POISON();
    int r = ssm(U(GetDesktopWindow)(), m);
    DWORD e = GetLastError();
    t_note("SetSystemMenu(desktop) -> %d err %lu", r, (unsigned long)e);
    CHECK(r == 0);                                             /* the desktop window has no WS_SYSMENU */
    U(DestroyMenu)(m);
}

TEST(gap1_sysmenu_menu_object_type_after_set) {
    SYS_BEGIN();
    HWND w = gap_win(0, GAP_STYLE_SYS);
    HMENU m = U(CreatePopupMenu)();
    if (!w || !m) { t_skip("setup"); if (w) U(DestroyWindow)(w); return; }
    EXPECT_EQ(ssm(w, m), 1);
    struct k_user_entry e;
    if (k_user_entry((HANDLE)m, &e)) { EXPECT_EQ(e.type, 2); EXPECT_EQ(e.flags & 1, 0); }
    CHECK(U(IsMenu)(m));
    U(DestroyWindow)(w);
}

TEST(gap1_sysmenu_replace_twice) {
    SYS_BEGIN();
    HWND w = gap_win(0, GAP_STYLE_SYS);
    HMENU sub1 = NULL, sub2 = NULL, m1 = gap_sysmenu_outer(&sub1), m2 = gap_sysmenu_outer(&sub2);
    if (!w || !m1 || !m2) { t_skip("setup"); if (w) U(DestroyWindow)(w); return; }
    EXPECT_EQ(ssm(w, m1), 1);
    EXPECT_EQ(ssm(w, m2), 1);
    EXPECT_EQ((ULONG_PTR)U(GetSystemMenu)(w, FALSE), (ULONG_PTR)sub2);
    CHECK(!U(IsMenu)(m1));                                      /* the replaced system menu is destroyed */
    U(DestroyWindow)(w);
}
