/* NtUserCreateWindowEx / NtUserDestroyWindow called directly through the win32u.dll stubs.
 *
 * Argument roles are taken from user32!VerNtUserCreateWindowEx (what the real caller pushes) and
 * NtUserCreateWindowEx / CreateWindowEx (what the kernel does with each one):
 *   a2 = class as the caller spelled it (becomes CREATESTRUCT.lpszClass, and is what the kernel
 *        registers via RegisterDefaultClass on a miss), a3 = the class name or atom actually looked
 *        up, a4 = window text, a14 = band (ZBID), a15 = (CW_FLAGS_* & 0xc0000000) | expWinVer,
 *        a16 = creation flags (only bits 0..2 are legal), a17 = activation context.
 */
#include "wcd.h"
#include <wchar.h>

#define NEED_NT() do { if (!wcd_user() || !UN(NtUserCreateWindowEx) || !UN(NtUserDestroyWindow)) { t_skip("user32/win32u entry points missing"); return; } } while (0)
#define HIDE_EX (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)

static HWND nt_hidden(DWORD style) {
    return wcd_nt_create(HIDE_EX, wcd_cls(), L"ntw", style, -32000, -32000, 100, 80, NULL, NULL, wcd_hinst(), NULL);
}

TEST(win_create_destroy_direct_basic_popup) {
    NEED_NT();
    HWND h = nt_hidden(WS_POPUP);
    CHECK(h != NULL);
    if (!h) return;
    CHECK(U(IsWindow)(h));
    WCHAR buf[64] = { 0 };
    CHECK(U(GetClassNameW)(h, buf, 64) > 0);
    EXPECT_WSTR(buf, wcd_cls());
    CHECK(U(GetWindowTextW)(h, buf, 64) == 3);
    EXPECT_WSTR(buf, L"ntw");
    CHECK((U(GetWindowLongW)(h, GWL_STYLE) & WS_POPUP) != 0);
    CHECK(!U(IsWindowVisible)(h));
    wcd_destroy(h);
    CHECK(!U(IsWindow)(h));
}

TEST(win_create_destroy_direct_handle_is_32bit_value) {
    NEED_NT();
    HWND h = nt_hidden(WS_POPUP);
    REQUIRE(h);
    /* USER handles are index | uniq<<16 and never use the upper dword (ValidateHwndEx masks
     * with 0xffff and compares only the next 16 bits). */
    CHECKF(((ULONG_PTR)h >> 32) == 0, "handle %p", h);
    CHECK(((ULONG_PTR)h & 0xffff) != 0);
    unsigned long long cnt = wcd_handle_count();
    if (cnt) CHECKF(((ULONG_PTR)h & 0xffff) < cnt, "index %lu cnt %llu", (unsigned long)((ULONG_PTR)h & 0xffff), cnt);
    else t_note("SERVERINFO handle count unreadable");
    wcd_destroy(h);
}

TEST(win_create_destroy_direct_handle_table_entry) {
    NEED_NT();
    HWND h = nt_hidden(WS_POPUP);
    REQUIRE(h);
    struct k_user_entry e;
    if (!k_user_entry(h, &e)) { t_skip("USER handle table not readable (non-Windows)"); wcd_destroy(h); return; }
    /* user32!IsWindow accepts an entry only when type byte == 1 (window), flags bit0 clear and
     * wUniq == HIWORD(handle). */
    EXPECT_EQ(e.type, 1);
    CHECK((e.flags & 1) == 0);
    EXPECT_EQ(e.uniq, (unsigned short)((ULONG_PTR)h >> 16));
    wcd_destroy(h);
    CHECK(!wcd_entry_live(h));
}

TEST(win_create_destroy_direct_sw_owner_is_this_thread) {
    NEED_NT();
    HWND h = nt_hidden(WS_POPUP);
    REQUIRE(h);
    void *sw = wcd_sw(h);
    if (!sw) { t_skip("client mapping of window not reachable"); wcd_destroy(h); return; }
    /* CreateWindowEx links the new window to the calling thread; user32 keeps that THREADINFO in
     * TEB.Win32ThreadInfo, so both agree for a window made by this thread. The shared structure's
     * style/exstyle words mirror GWL_*. */
    unsigned long st = 0, ex = 0;
    CHECK(k_try_read_u32((char *)sw + SW_STYLE, &st));
    CHECK(k_try_read_u32((char *)sw + SW_EXSTYLE, &ex));
    CHECKF(st == (unsigned long)U(GetWindowLongW)(h, GWL_STYLE), "st %lx", st);
    CHECKF(ex == (unsigned long)U(GetWindowLongW)(h, GWL_EXSTYLE), "ex %lx", ex);
    wcd_destroy(h);
}

TEST(win_create_destroy_direct_sw_rects) {
    NEED_NT();
    HWND h = wcd_nt_create(HIDE_EX, wcd_cls(), L"r", WS_POPUP, 10, 20, 111, 77, NULL, NULL, wcd_hinst(), NULL);
    REQUIRE(h);
    void *sw = wcd_sw(h);
    RECT wr, cr, a, b;
    CHECK(U(GetWindowRect)(h, &wr));
    CHECK(U(GetClientRect)(h, &cr));
    if (wcd_dpi_plain()) {
        EXPECT_EQ(wr.left, 10); EXPECT_EQ(wr.top, 20);
        EXPECT_EQ(wr.right, 121); EXPECT_EQ(wr.bottom, 97);
        EXPECT_EQ(cr.right, 111); EXPECT_EQ(cr.bottom, 77);
    } else t_note("DPI virtualised, absolute rects not asserted");
    if (sw && k_try_read((char *)sw + SW_RCWND, &a, sizeof a) && k_try_read((char *)sw + SW_RCCLI, &b, sizeof b) && wcd_dpi_plain()) {
        /* rcWindow/rcClient are the rectangles CreateWindowEx feeds through WM_NCCALCSIZE. */
        CHECK(a.left == wr.left && a.top == wr.top && a.right == wr.right && a.bottom == wr.bottom);
        CHECK(b.left == wr.left && b.top == wr.top && b.right == wr.right && b.bottom == wr.bottom);
    } else t_note("shared rect words not checked");
    wcd_destroy(h);
}

TEST(win_create_destroy_direct_name_roundtrip) {
    NEED_NT();
    static const WCHAR *names[] = { L"", L"a", L"hello world", L"\x4e2d\x6587\x00e9", L"tab\there" };
    for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++) {
        HWND h = wcd_nt_create(HIDE_EX, wcd_cls(), names[i], WS_POPUP, -32000, -32000, 50, 50, NULL, NULL, wcd_hinst(), NULL);
        CHECK(h != NULL);
        if (!h) continue;
        WCHAR buf[64] = { 1 };
        int n = U(GetWindowTextW)(h, buf, 64);
        CHECKF(n == (int)wcslen(names[i]), "name %u len %d", i, n);
        CHECK(!wcscmp(buf, names[i]));
        CHECK(U(GetWindowTextLengthW)(h) == (int)wcslen(names[i]));
        wcd_destroy(h);
    }
}

TEST(win_create_destroy_direct_name_null_is_empty) {
    NEED_NT();
    HWND h = wcd_nt_create(HIDE_EX, wcd_cls(), NULL, WS_POPUP, -32000, -32000, 50, 50, NULL, NULL, wcd_hinst(), NULL);
    REQUIRE(h);
    WCHAR buf[8] = { 7 };
    CHECK(U(GetWindowTextW)(h, buf, 8) == 0);
    CHECK(buf[0] == 0);
    wcd_destroy(h);
}

TEST(win_create_destroy_direct_name_long) {
    NEED_NT();
    WCHAR big[1200];
    for (int i = 0; i < 1199; i++) big[i] = L'a' + (i % 26);
    big[1199] = 0;
    HWND h = wcd_nt_create(HIDE_EX, wcd_cls(), big, WS_POPUP, -32000, -32000, 50, 50, NULL, NULL, wcd_hinst(), NULL);
    CHECK(h != NULL);
    if (!h) return;
    CHECK(U(GetWindowTextLengthW)(h) == 1199);
    WCHAR out[1300] = { 0 };
    CHECK(U(GetWindowTextW)(h, out, 1300) == 1199);
    CHECK(!wcscmp(out, big));
    wcd_destroy(h);
}

TEST(win_create_destroy_direct_class_lookup_null) {
    NEED_NT();
    /* a3 (lookup name) NULL -> atom 0 -> no class found, kernel reports ERROR_CANNOT_FIND_WND_CLASS. */
    SetLastError(0);
    HWND h = wcd_nt_create_full(HIDE_EX, wcd_cls(), NULL, L"x", WS_POPUP, 0, 0, 10, 10, NULL, NULL, wcd_hinst(), NULL, 0, WCD_WINVER, 0, NULL);
    CHECK(h == NULL);
    EXPECT_ERR(1407);
    wcd_destroy(h);
}

TEST(win_create_destroy_direct_class_both_null) {
    NEED_NT();
    SetLastError(0);
    HWND h = wcd_nt_create_full(HIDE_EX, NULL, NULL, L"x", WS_POPUP, 0, 0, 10, 10, NULL, NULL, wcd_hinst(), NULL, 0, WCD_WINVER, 0, NULL);
    CHECK(h == NULL);
    EXPECT_ERR(1407);
    wcd_destroy(h);
}

TEST(win_create_destroy_direct_class_unknown_string) {
    NEED_NT();
    WCHAR n[96]; wcd_uniq_name(n, 96, "nosuch");
    SetLastError(0);
    HWND h = wcd_nt_create(HIDE_EX, n, L"x", WS_POPUP, 0, 0, 10, 10, NULL, NULL, wcd_hinst(), NULL);
    CHECK(h == NULL);
    EXPECT_ERR(1407);
    wcd_destroy(h);
}

TEST(win_create_destroy_direct_class_unknown_atom) {
    NEED_NT();
    SetLastError(0);
    /* integer atom with no class behind it: _InnerGetClassPtr finds nothing in either list. */
    HWND h = wcd_nt_create(HIDE_EX, MAKEINTATOM(0xBFFE), L"x", WS_POPUP, 0, 0, 10, 10, NULL, NULL, wcd_hinst(), NULL);
    CHECK(h == NULL);
    EXPECT_ERR(1407);
    wcd_destroy(h);
}

TEST(win_create_destroy_direct_class_name_too_long) {
    NEED_NT();
    /* CreateWindowEx copies the lookup name into a 0x100-WCHAR scratch buffer with
     * RtlStringCchCopyW; overflow sets the "no default class retry" flag and ends in 0x57f. */
    WCHAR big[400];
    for (int i = 0; i < 399; i++) big[i] = L'k';
    big[399] = 0;
    SetLastError(0);
    HWND h = wcd_nt_create(HIDE_EX, big, L"x", WS_POPUP, 0, 0, 10, 10, NULL, NULL, wcd_hinst(), NULL);
    CHECK(h == NULL);
    EXPECT_ERR(1407);
    wcd_destroy(h);
}

TEST(win_create_destroy_direct_class_by_atom) {
    NEED_NT();
    ATOM a = wcd_cls_atom();
    REQUIRE(a);
    HWND h = wcd_nt_create(HIDE_EX, MAKEINTATOM(a), L"atom", WS_POPUP, -32000, -32000, 40, 40, NULL, NULL, wcd_hinst(), NULL);
    CHECK(h != NULL);
    if (!h) return;
    WCHAR buf[96];
    CHECK(U(GetClassNameW)(h, buf, 96) > 0);
    EXPECT_WSTR(buf, wcd_cls());
    wcd_destroy(h);
}

TEST(win_create_destroy_direct_class_atom_vs_string_same_class) {
    NEED_NT();
    ATOM a = wcd_cls_atom();
    REQUIRE(a);
    HWND h1 = wcd_nt_create(HIDE_EX, MAKEINTATOM(a), L"1", WS_POPUP, -32000, -32000, 40, 40, NULL, NULL, wcd_hinst(), NULL);
    HWND h2 = wcd_nt_create(HIDE_EX, wcd_cls(), L"2", WS_POPUP, -32000, -32000, 40, 40, NULL, NULL, wcd_hinst(), NULL);
    CHECK(h1 && h2);
    if (h1 && h2) {
        CHECK(U(GetClassLongW)(h1, GCW_ATOM) == a);
        CHECK(U(GetClassLongW)(h2, GCW_ATOM) == a);
        CHECK(h1 != h2);
    }
    wcd_destroy(h1); wcd_destroy(h2);
}

TEST(win_create_destroy_direct_class_case_insensitive) {
    NEED_NT();
    WCHAR up[96]; wcsncpy(up, wcd_cls(), 95); up[95] = 0;
    for (WCHAR *p = up; *p; p++) *p = (WCHAR)towupper(*p);
    HWND h = wcd_nt_create(HIDE_EX, up, L"u", WS_POPUP, -32000, -32000, 40, 40, NULL, NULL, wcd_hinst(), NULL);
    /* the lookup goes through UserFindAtom on the case-insensitive atom table */
    CHECK(h != NULL);
    wcd_destroy(h);
}

TEST(win_create_destroy_direct_class_instance_null_matches_any) {
    NEED_NT();
    /* _InnerGetClassPtr: a NULL module accepts any class module. */
    HWND h = wcd_nt_create(HIDE_EX, wcd_cls(), L"i0", WS_POPUP, -32000, -32000, 40, 40, NULL, NULL, NULL, NULL);
    CHECK(h != NULL);
    wcd_destroy(h);
}

TEST(win_create_destroy_direct_class_instance_mismatch) {
    NEED_NT();
    /* private class registered with the exe module; a different module's high word fails every
     * one of the four lookups (private/public x caller/user32 module). */
    HINSTANCE bad = (HINSTANCE)(ULONG_PTR)0x00010000;
    if (((ULONG_PTR)wcd_hinst() >> 16 & 0xffff) == 1) { t_skip("exe high word collides"); return; }
    SetLastError(0);
    HWND h = wcd_nt_create(HIDE_EX, wcd_cls(), L"bad", WS_POPUP, 0, 0, 10, 10, NULL, NULL, bad, NULL);
    CHECK(h == NULL);
    EXPECT_ERR(1407);
    wcd_destroy(h);
}

TEST(win_create_destroy_direct_class_instance_recorded) {
    NEED_NT();
    HWND h = nt_hidden(WS_POPUP);
    REQUIRE(h);
    CHECK((HINSTANCE)U(GetWindowLongPtrW)(h, GWLP_HINSTANCE) == wcd_hinst());
    void *sw = wcd_sw(h);
    void *hi = NULL;
    if (sw && k_try_read_ptr((char *)sw + SW_HINST, &hi)) CHECK(hi == (void *)wcd_hinst());
    wcd_destroy(h);
}

TEST(win_create_destroy_direct_flags2_illegal_bits) {
    NEED_NT();
    /* NtUserCreateWindowEx: (a16 & 0xfffffff8) != 0 -> SetLastError(ERROR_INVALID_PARAMETER). */
    static const DWORD bad[] = { 8, 0x10, 0x100, 0x10000, 0x80000000, 0xfffffff8, 0xffffffff };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(0);
        HWND h = wcd_nt_create_full(HIDE_EX, wcd_cls(), wcd_cls(), L"f", WS_POPUP, 0, 0, 10, 10, NULL, NULL, wcd_hinst(), NULL, 0, WCD_WINVER, bad[i], NULL);
        CHECKF(h == NULL, "flags2 0x%lx", (unsigned long)bad[i]);
        EXPECT_ERR(87);
        wcd_destroy(h);
    }
}

TEST(win_create_destroy_direct_flags2_immersive_bit_desktop_app) {
    NEED_NT();
    /* bit0 asks for an immersive (non-desktop) window; CreateWindowEx only goes on when the
     * process is not a desktop app (IsDesktopApp == 0), otherwise it falls through to 5. */
    SetLastError(0);
    HWND h = wcd_nt_create_full(HIDE_EX, wcd_cls(), wcd_cls(), L"f", WS_POPUP, 0, 0, 10, 10, NULL, NULL, wcd_hinst(), NULL, 0, WCD_WINVER, 1, NULL);
    CHECK(h == NULL);
    EXPECT_ERR(5);
    wcd_destroy(h);
}

TEST(win_create_destroy_direct_flags2_bit1_requires_broker) {
    NEED_NT();
    SetLastError(0);
    HWND h = wcd_nt_create_full(HIDE_EX, wcd_cls(), wcd_cls(), L"f", WS_POPUP, 0, 0, 10, 10, NULL, NULL, wcd_hinst(), NULL, 0, WCD_WINVER, 2, NULL);
    CHECK(h == NULL);
    t_note("flags2=2 error %lu (immersive-broker only path)", GetLastError());
    wcd_destroy(h);
}

TEST(win_create_destroy_direct_flags2_immersive_message_parent) {
    NEED_NT();
    /* parent == -3 (HWND_MESSAGE) with bit0 set is rejected before anything else (a10 == -3 branch). */
    SetLastError(0);
    HWND h = wcd_nt_create_full(HIDE_EX, wcd_cls(), wcd_cls(), L"f", WS_POPUP, 0, 0, 10, 10, WCD_HWND_MESSAGE, NULL, wcd_hinst(), NULL, 0, WCD_WINVER, 1, NULL);
    CHECK(h == NULL);
    EXPECT_ERR(87);
    wcd_destroy(h);
}

TEST(win_create_destroy_direct_flags2_bad_with_message_parent) {
    NEED_NT();
    SetLastError(0);
    HWND h = wcd_nt_create_full(HIDE_EX, wcd_cls(), wcd_cls(), L"f", WS_POPUP, 0, 0, 10, 10, NULL, NULL, wcd_hinst(), NULL, 0, WCD_WINVER, 0x40, NULL);
    CHECK(h == NULL);
    EXPECT_ERR(87);
}

TEST(win_create_destroy_direct_zbid_invalid) {
    NEED_NT();
    /* IsValidBand walks gazbidOrder (18 entries); 0xf is explicitly rejected, arbitrary values
     * are not in the table. */
    static const DWORD z[] = { 0xf, 0x7fffffff, 0x1234, 0xffffffff, 0x100 };
    for (unsigned i = 0; i < sizeof z / sizeof z[0]; i++) {
        SetLastError(0);
        HWND h = wcd_nt_create_full(HIDE_EX, wcd_cls(), wcd_cls(), L"z", WS_POPUP, 0, 0, 10, 10, NULL, NULL, wcd_hinst(), NULL, z[i], WCD_WINVER, 0, NULL);
        CHECKF(h == NULL, "zbid 0x%lx", (unsigned long)z[i]);
        EXPECT_ERR(87);
        wcd_destroy(h);
    }
}

TEST(win_create_destroy_direct_zbid_desktop_ok) {
    NEED_NT();
    /* ZBID_DESKTOP (1) is always accepted by IsValidBandForProcess. */
    HWND h = wcd_nt_create_full(HIDE_EX, wcd_cls(), wcd_cls(), L"z", WS_POPUP, -32000, -32000, 10, 10, NULL, NULL, wcd_hinst(), NULL, 1, WCD_WINVER, 0, NULL);
    CHECK(h != NULL);
    wcd_destroy(h);
}

TEST(win_create_destroy_direct_zbid_immersive_for_desktop_process) {
    NEED_NT();
    SetLastError(0);
    HWND h = wcd_nt_create_full(HIDE_EX, wcd_cls(), wcd_cls(), L"z", WS_POPUP, 0, 0, 10, 10, NULL, NULL, wcd_hinst(), NULL, 2, WCD_WINVER, 0, NULL);
    /* band 2 needs a UIAccess-class process; the test exe is a plain desktop app */
    if (h) t_note("band 2 accepted (process has the band privilege)");
    else t_note("band 2 rejected, error %lu", GetLastError());
    wcd_destroy(h);
}

TEST(win_create_destroy_direct_invalid_parent_value) {
    NEED_NT();
    static const ULONG_PTR p[] = { 0x12345678, 0x7fffffff, 0xffff, 0x10000, 0x7ffe0000 };
    for (unsigned i = 0; i < sizeof p / sizeof p[0]; i++) {
        SetLastError(0);
        HWND h = wcd_nt_create(HIDE_EX, wcd_cls(), L"p", WS_POPUP, 0, 0, 10, 10, (HWND)p[i], NULL, wcd_hinst(), NULL);
        CHECKF(h == NULL, "parent 0x%llx", (unsigned long long)p[i]);
        EXPECT_ERR(1400);
        wcd_destroy(h);
    }
}

TEST(win_create_destroy_direct_destroyed_parent) {
    NEED_NT();
    HWND p = nt_hidden(WS_POPUP);
    REQUIRE(p);
    wcd_destroy(p);
    SetLastError(0);
    HWND c = wcd_nt_create(HIDE_EX, wcd_cls(), L"c", WS_CHILD, 0, 0, 10, 10, p, (HMENU)1, wcd_hinst(), NULL);
    CHECK(c == NULL);
    EXPECT_ERR(1400);
    wcd_destroy(c);
}

TEST(win_create_destroy_direct_invalid_menu) {
    NEED_NT();
    /* a top-level window's hMenu goes through ValidateHmenu -> HMValidateHandle(type 2) which
     * reports 0x579 ERROR_INVALID_MENU_HANDLE on a miss. */
    SetLastError(0);
    HWND h = wcd_nt_create(HIDE_EX, wcd_cls(), L"m", WS_POPUP, 0, 0, 10, 10, NULL, (HMENU)(ULONG_PTR)0x4321, wcd_hinst(), NULL);
    CHECK(h == NULL);
    EXPECT_ERR(1401);
    wcd_destroy(h);
}

TEST(win_create_destroy_direct_menu_valid) {
    NEED_NT();
    if (!U(CreateMenu) || !U(GetMenu) || !U(DestroyMenu)) { t_skip("menu API"); return; }
    HMENU m = U(CreateMenu)();
    REQUIRE(m);
    HWND h = wcd_nt_create(HIDE_EX, wcd_cls(), L"m", WS_POPUP, -32000, -32000, 40, 40, NULL, m, wcd_hinst(), NULL);
    CHECK(h != NULL);
    if (h) {
        CHECK(U(GetMenu)(h) == m);
        wcd_destroy(h);
    }
    /* destroying the window destroys the menu it owns */
    CHECK(!U(IsMenu)(m));
    if (U(IsMenu)(m)) U(DestroyMenu)(m);
}

TEST(win_create_destroy_direct_child_without_parent) {
    NEED_NT();
    SetLastError(0);
    HWND h = wcd_nt_create(HIDE_EX, wcd_cls(), L"c", WS_CHILD, 0, 0, 10, 10, NULL, (HMENU)1, wcd_hinst(), NULL);
    CHECK(h == NULL);
    /* CreateWindowEx: WS_CHILD with no parent -> 0x57e ERROR_TLW_WITH_WSCHILD */
    EXPECT_ERR(1406);
    wcd_destroy(h);
}

TEST(win_create_destroy_direct_child_without_parent_visible) {
    NEED_NT();
    SetLastError(0);
    HWND h = wcd_nt_create(0, wcd_cls(), L"c", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, NULL, NULL, wcd_hinst(), NULL);
    CHECK(h == NULL);
    EXPECT_ERR(1406);
    wcd_destroy(h);
}

TEST(win_create_destroy_direct_child_id_is_not_a_menu) {
    NEED_NT();
    HWND p = nt_hidden(WS_POPUP);
    REQUIRE(p);
    static const ULONG_PTR ids[] = { 0, 1, 0x4321, 0xffff, 0x7fffffff };
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        /* for (style & 0xc0000000) == WS_CHILD the hMenu argument is taken verbatim as the id */
        HWND c = wcd_nt_create(0, wcd_cls(), L"c", WS_CHILD, 0, 0, 10, 10, p, (HMENU)ids[i], wcd_hinst(), NULL);
        CHECKF(c != NULL, "id %llx", (unsigned long long)ids[i]);
        if (c) {
            CHECKF(U(GetDlgCtrlID)(c) == (int)ids[i], "got %d", U(GetDlgCtrlID)(c));
            CHECK(U(GetParent)(c) == p);
            void *sw = wcd_sw(c); unsigned long long v = 0;
            if (sw && k_try_read((char *)sw + SW_ID, &v, 8)) CHECK((unsigned)v == (unsigned)ids[i]);
        }
    }
    wcd_destroy(p);
}

TEST(win_create_destroy_direct_child_of_message_window) {
    NEED_NT();
    HWND m = wcd_nt_create(0, wcd_cls(), L"msg", WS_POPUP, 0, 0, 10, 10, WCD_HWND_MESSAGE, NULL, wcd_hinst(), NULL);
    REQUIRE(m);
    HWND c = wcd_nt_create(0, wcd_cls(), L"c", WS_CHILD, 0, 0, 10, 10, m, (HMENU)5, wcd_hinst(), NULL);
    CHECK(c != NULL);
    if (c) CHECK(U(GetParent)(c) == m);
    wcd_destroy(m);
    CHECK(!U(IsWindow)(c));
}

TEST(win_create_destroy_direct_lparam_reaches_createstruct) {
    NEED_NT();
    wcd_reset();
    LPVOID tag = (LPVOID)(ULONG_PTR)0x1122334455667788ull;
    HWND h = wcd_nt_create(HIDE_EX, wcd_cls(), L"lp", WS_POPUP, -32000, -32000, 40, 40, NULL, NULL, wcd_hinst(), tag);
    REQUIRE(h);
    int nc = wcd_find(WM_NCCREATE, 0), cr = wcd_find(WM_CREATE, 0);
    CHECK(nc >= 0 && cr >= 0);
    if (nc >= 0) CHECK(wcd_log[nc].has_cs && wcd_log[nc].cs.lpCreateParams == tag);
    if (cr >= 0) CHECK(wcd_log[cr].has_cs && wcd_log[cr].cs.lpCreateParams == tag);
    wcd_destroy(h);
}

TEST(win_create_destroy_direct_createstruct_fields) {
    NEED_NT();
    wcd_reset();
    HWND h = wcd_nt_create(HIDE_EX, wcd_cls(), L"cs-name", WS_POPUP | WS_DISABLED, 12, 34, 56, 78, NULL, NULL, wcd_hinst(), NULL);
    REQUIRE(h);
    int nc = wcd_find(WM_NCCREATE, 0);
    REQUIRE(nc >= 0);
    CHECK(wcd_log[nc].has_cs);
    CREATESTRUCTW *cs = &wcd_log[nc].cs;
    EXPECT_EQ(cs->x, 12); EXPECT_EQ(cs->y, 34); EXPECT_EQ(cs->cx, 56); EXPECT_EQ(cs->cy, 78);
    CHECK((cs->style & (WS_POPUP | WS_DISABLED)) == (WS_POPUP | WS_DISABLED));
    CHECK((cs->dwExStyle & HIDE_EX) == HIDE_EX);
    CHECK(cs->hInstance == wcd_hinst());
    CHECK(cs->hwndParent == NULL);
    CHECK(cs->hMenu == NULL);
    CHECK(!wcscmp(wcd_log[nc].cs_name, L"cs-name"));
    CHECK(!wcsicmp(wcd_log[nc].cs_cls, wcd_cls()) || wcd_log[nc].cs_cls_is_atom);
    wcd_destroy(h);
}

TEST(win_create_destroy_direct_createstruct_parent_and_id) {
    NEED_NT();
    HWND p = nt_hidden(WS_POPUP);
    REQUIRE(p);
    wcd_reset();
    HWND c = wcd_nt_create(0, wcd_cls(), L"c", WS_CHILD, 1, 2, 30, 40, p, (HMENU)(ULONG_PTR)77, wcd_hinst(), NULL);
    REQUIRE(c);
    int nc = wcd_find_hwnd(WM_NCCREATE, c, 0);
    REQUIRE(nc >= 0);
    CHECK(wcd_log[nc].cs.hwndParent == p);
    CHECK(wcd_log[nc].cs.hMenu == (HMENU)(ULONG_PTR)77);
    EXPECT_EQ(wcd_log[nc].cs.x, 1); EXPECT_EQ(wcd_log[nc].cs.cy, 40);
    wcd_destroy(p);
}

TEST(win_create_destroy_direct_class_arg_vs_lookup_arg) {
    NEED_NT();
    wcd_reset();
    /* a2 is only the spelling reported in CREATESTRUCT.lpszClass; a3 decides which class is used */
    HWND h = wcd_nt_create_full(HIDE_EX, L"SpelledDifferently", wcd_cls(), L"x", WS_POPUP, -32000, -32000, 20, 20, NULL, NULL, wcd_hinst(), NULL, 0, WCD_WINVER, 0, NULL);
    CHECK(h != NULL);
    if (!h) return;
    WCHAR buf[96];
    U(GetClassNameW)(h, buf, 96);
    EXPECT_WSTR(buf, wcd_cls());
    int nc = wcd_find(WM_NCCREATE, 0);
    if (nc >= 0) t_note("CREATESTRUCT.lpszClass seen: %ls (atom=%d)", wcd_log[nc].cs_cls, wcd_log[nc].cs_cls_is_atom);
    wcd_destroy(h);
}

TEST(win_create_destroy_direct_cbwndextra_zeroed) {
    NEED_NT();
    WCHAR n[96]; wcd_uniq_name(n, 96, "extra");
    ATOM a = wcd_register(n, NULL, 0, 0, 32);
    REQUIRE(a);
    HWND h = wcd_nt_create(HIDE_EX, n, L"e", WS_POPUP, -32000, -32000, 20, 20, NULL, NULL, wcd_hinst(), NULL);
    CHECK(h != NULL);
    if (h) {
        for (int off = 0; off <= 24; off += 8) CHECKF(U(GetWindowLongPtrW)(h, off) == 0, "extra at %d", off);
        CHECK(U(SetWindowLongPtrW)(h, 8, 0x55aa) == 0);
        CHECK(U(GetWindowLongPtrW)(h, 8) == 0x55aa);
        void *sw = wcd_sw(h); unsigned long cb = 0;
        if (sw && k_try_read_u32((char *)sw + SW_CBWND, &cb)) CHECKF(cb == 32, "cbWndExtra %lu", cb);
        SetLastError(0);
        U(GetWindowLongPtrW)(h, 32);
        EXPECT_ERR(1413);
        wcd_destroy(h);
    }
    wcd_unregister(n);
}

TEST(win_create_destroy_direct_destroy_valid) {
    NEED_NT();
    HWND h = nt_hidden(WS_POPUP);
    REQUIRE(h);
    NtUserDestroyWindow_pfn d = UN(NtUserDestroyWindow);
    SetLastError(0);
    DWORD r = d(h);
    CHECK((int)r != 0);
    CHECK(!U(IsWindow)(h));
}

TEST(win_create_destroy_direct_destroy_twice) {
    NEED_NT();
    HWND h = nt_hidden(WS_POPUP);
    REQUIRE(h);
    NtUserDestroyWindow_pfn d = UN(NtUserDestroyWindow);
    CHECK((int)d(h) != 0);
    SetLastError(0);
    CHECK((int)d(h) == 0);
    EXPECT_ERR(1400);
}

TEST(win_create_destroy_direct_destroy_invalid_values) {
    NEED_NT();
    NtUserDestroyWindow_pfn d = UN(NtUserDestroyWindow);
    static const ULONG_PTR v[] = { 0, 0x12345678, 0x7fffffff, 0x00010000, 0x7ffe0000 };
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) {
        SetLastError(0);
        DWORD r = d((HWND)v[i]);
        CHECKF((int)r == 0, "hwnd 0x%llx returned %ld", (unsigned long long)v[i], (long)r);
        EXPECT_ERR(1400);
    }
}

TEST(win_create_destroy_direct_destroy_desktop_window) {
    NEED_NT();
    HWND dw = U(GetDesktopWindow)();
    REQUIRE(dw);
    NtUserDestroyWindow_pfn d = UN(NtUserDestroyWindow);
    SetLastError(0);
    /* the desktop window belongs to another thread: DestroyWindow refuses before doing anything */
    CHECK((int)d(dw) == 0);
    EXPECT_ERR(5);
    CHECK(U(IsWindow)(dw));
}

TEST(win_create_destroy_direct_destroy_shell_window) {
    NEED_NT();
    HWND sh = U(GetShellWindow)();
    if (!sh) { t_skip("no shell window"); return; }
    NtUserDestroyWindow_pfn d = UN(NtUserDestroyWindow);
    SetLastError(0);
    CHECK((int)d(sh) == 0);
    EXPECT_ERR(5);
    CHECK(U(IsWindow)(sh));
}

TEST(win_create_destroy_direct_exstyle_unsupported_bits_stripped) {
    NEED_NT();
    /* NtUserCreateWindowEx masks the extended style with 0x8a7f77ff; the bits below are the ones
     * outside that mask (bit 11, 15, 24, 26, 28..30). */
    static const DWORD bits[] = { 0x00000800, 0x00008000, 0x01000000, 0x04000000, 0x10000000, 0x20000000, 0x40000000 };
    for (unsigned i = 0; i < sizeof bits / sizeof bits[0]; i++) {
        HWND h = wcd_nt_create(HIDE_EX | bits[i], wcd_cls(), L"x", WS_POPUP, -32000, -32000, 20, 20, NULL, NULL, wcd_hinst(), NULL);
        CHECKF(h != NULL, "ex bit 0x%lx", (unsigned long)bits[i]);
        if (!h) continue;
        LONG ex = U(GetWindowLongW)(h, GWL_EXSTYLE);
        CHECKF((ex & bits[i]) == 0, "bit 0x%lx survived: ex=0x%lx", (unsigned long)bits[i], (unsigned long)ex);
        CHECK((ex & HIDE_EX) == HIDE_EX);
        wcd_destroy(h);
    }
}

TEST(win_create_destroy_direct_exstyle_supported_bits_kept) {
    NEED_NT();
    static const DWORD bits[] = { WS_EX_TOOLWINDOW, WS_EX_NOACTIVATE, WS_EX_TOPMOST, WS_EX_TRANSPARENT,
                                  WS_EX_ACCEPTFILES, WS_EX_CONTEXTHELP, WS_EX_APPWINDOW, WS_EX_NOPARENTNOTIFY };
    for (unsigned i = 0; i < sizeof bits / sizeof bits[0]; i++) {
        HWND h = wcd_nt_create(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | bits[i], wcd_cls(), L"x", WS_POPUP, -32000, -32000, 20, 20, NULL, NULL, wcd_hinst(), NULL);
        CHECKF(h != NULL, "ex 0x%lx", (unsigned long)bits[i]);
        if (!h) continue;
        LONG ex = U(GetWindowLongW)(h, GWL_EXSTYLE);
        CHECKF((ex & bits[i]) == bits[i], "bit 0x%lx lost: ex=0x%lx", (unsigned long)bits[i], (unsigned long)ex);
        wcd_destroy(h);
    }
}

TEST(win_create_destroy_direct_exstyle_composited_and_noredirection) {
    NEED_NT();
    SetLastError(0);
    /* CreateWindowEx: (ex & 0x2200000) == 0x2200000 (COMPOSITED | NOREDIRECTIONBITMAP) skips
     * the class lookup and ends in the ERROR_INVALID_PARAMETER exit. */
    HWND h = wcd_nt_create(HIDE_EX | 0x02000000 | 0x00200000, wcd_cls(), L"x", WS_POPUP, 0, 0, 20, 20, NULL, NULL, wcd_hinst(), NULL);
    CHECK(h == NULL);
    EXPECT_ERR(87);
    wcd_destroy(h);
}

/* NeedsWindowEdge(style, exstyle, winver >= 0x400): WS_EX_WINDOWEDGE is recomputed by the kernel. */
struct edge_case { DWORD style, ex; DWORD ver; int want; const char *why; };
static const struct edge_case edge_cases[] = {
    { WS_POPUP,                               0,                  0xA00, 0, "plain popup" },
    { WS_POPUP,                               WS_EX_WINDOWEDGE,   0xA00, 0, "explicit edge stripped without need" },
    { WS_POPUP,                               WS_EX_DLGMODALFRAME,0xA00, 1, "dlgmodalframe" },
    { WS_POPUP | WS_THICKFRAME,               0,                  0xA00, 1, "thickframe" },
    { WS_POPUP | WS_THICKFRAME,               WS_EX_STATICEDGE,   0xA00, 0, "staticedge cancels thickframe" },
    { WS_POPUP | WS_DLGFRAME,                 0,                  0xA00, 1, "dlgframe alone" },
    { WS_POPUP | WS_BORDER,                   0,                  0xA00, 0, "border alone" },
    { WS_POPUP | WS_CAPTION,                  0,                  0xA00, 1, "caption, new app" },
    { WS_POPUP | WS_CAPTION,                  0,                  0x300, 0, "caption, pre-4.0 app" },
    { WS_POPUP | WS_CAPTION,                  0,                  0x400, 1, "caption, 4.0 app" },
    { WS_POPUP | WS_DLGFRAME,                 0,                  0x300, 1, "dlgframe regardless of version" },
    { WS_POPUP | WS_CAPTION,                  WS_EX_STATICEDGE,   0xA00, 0, "caption + staticedge" },
    { WS_POPUP | WS_DLGFRAME,                 WS_EX_STATICEDGE,   0xA00, 0, "dlgframe + staticedge" },
    { WS_POPUP | WS_THICKFRAME | WS_CAPTION,  0,                  0xA00, 1, "thickframe + caption" },
    { WS_POPUP,                               WS_EX_CLIENTEDGE,   0xA00, 0, "clientedge does not imply windowedge" },
    { WS_POPUP,                               WS_EX_DLGMODALFRAME | WS_EX_STATICEDGE, 0xA00, 1, "dlgmodalframe wins over staticedge" },
};
TEST(win_create_destroy_direct_windowedge_table) {
    NEED_NT();
    for (unsigned i = 0; i < sizeof edge_cases / sizeof edge_cases[0]; i++) {
        const struct edge_case *c = &edge_cases[i];
        HWND h = wcd_nt_create_full(HIDE_EX | c->ex, wcd_cls(), wcd_cls(), L"e", c->style, -32000, -32000, 200, 150, NULL, NULL, wcd_hinst(), NULL, 0, c->ver, 0, NULL);
        CHECKF(h != NULL, "case %u (%s)", i, c->why);
        if (!h) continue;
        LONG ex = U(GetWindowLongW)(h, GWL_EXSTYLE);
        CHECKF(!!(ex & WS_EX_WINDOWEDGE) == c->want, "case %u (%s): ex=0x%lx", i, c->why, (unsigned long)ex);
        wcd_destroy(h);
    }
}

TEST(win_create_destroy_direct_windowedge_child) {
    NEED_NT();
    HWND p = nt_hidden(WS_POPUP);
    REQUIRE(p);
    HWND c = wcd_nt_create(0, wcd_cls(), L"c", WS_CHILD | WS_THICKFRAME, 0, 0, 50, 50, p, (HMENU)1, wcd_hinst(), NULL);
    REQUIRE(c);
    CHECK((U(GetWindowLongW)(c, GWL_EXSTYLE) & WS_EX_WINDOWEDGE) != 0);
    wcd_destroy(p);
}

TEST(win_create_destroy_direct_style_readback_table) {
    NEED_NT();
    static const DWORD styles[] = { WS_POPUP, WS_POPUP | WS_DISABLED, WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        WS_POPUP | WS_BORDER, WS_POPUP | WS_CAPTION | WS_SYSMENU, WS_POPUP | WS_VSCROLL | WS_HSCROLL,
        WS_POPUP | WS_MINIMIZEBOX | WS_MAXIMIZEBOX, WS_POPUP | WS_GROUP | WS_TABSTOP };
    for (unsigned i = 0; i < sizeof styles / sizeof styles[0]; i++) {
        HWND h = wcd_nt_create(HIDE_EX, wcd_cls(), L"s", styles[i], -32000, -32000, 200, 100, NULL, NULL, wcd_hinst(), NULL);
        CHECKF(h != NULL, "style 0x%lx", (unsigned long)styles[i]);
        if (!h) continue;
        DWORD s = (DWORD)U(GetWindowLongW)(h, GWL_STYLE);
        CHECKF((s & styles[i]) == styles[i], "style 0x%lx read 0x%lx", (unsigned long)styles[i], (unsigned long)s);
        CHECK(!(s & WS_VISIBLE));
        CHECK(U(IsWindowEnabled)(h) == !(styles[i] & WS_DISABLED));
        wcd_destroy(h);
    }
}

TEST(win_create_destroy_direct_zero_size_ok) {
    NEED_NT();
    HWND h = wcd_nt_create(HIDE_EX, wcd_cls(), L"z", WS_POPUP, 5, 6, 0, 0, NULL, NULL, wcd_hinst(), NULL);
    CHECK(h != NULL);
    if (h) {
        RECT r; U(GetWindowRect)(h, &r);
        if (wcd_dpi_plain()) { EXPECT_EQ(r.right - r.left, 0); EXPECT_EQ(r.bottom - r.top, 0); }
        wcd_destroy(h);
    }
}

TEST(win_create_destroy_direct_negative_size_note) {
    NEED_NT();
    /* a negative cx/cy is not CW_USEDEFAULT; the window rect never gets negative extent */
    HWND h = wcd_nt_create(HIDE_EX, wcd_cls(), L"n", WS_POPUP, 5, 6, -50, -60, NULL, NULL, wcd_hinst(), NULL);
    CHECK(h != NULL);
    if (h) {
        RECT r; U(GetWindowRect)(h, &r);
        t_note("negative cx/cy window rect: %ld,%ld,%ld,%ld", r.left, r.top, r.right, r.bottom);
        wcd_destroy(h);
    }
}

TEST(win_create_destroy_direct_user_object_accounting) {
    NEED_NT();
    HWND warm = nt_hidden(WS_POPUP);
    REQUIRE(warm);
    int base = wcd_user_objects();
    if (base <= 0) { wcd_destroy(warm); t_skip("GetGuiResources unusable"); return; }
    HWND h[10];
    for (int i = 0; i < 10; i++) h[i] = nt_hidden(WS_POPUP);
    int mid = wcd_user_objects();
    CHECKF(mid == base + 10, "base %d mid %d", base, mid);
    for (int i = 0; i < 10; i++) wcd_destroy(h[i]);
    CHECKF(wcd_user_objects() == base, "after destroy %d base %d", wcd_user_objects(), base);
    wcd_destroy(warm);
}

TEST(win_create_destroy_direct_handles_unique_while_alive) {
    NEED_NT();
    HWND h[32];
    for (int i = 0; i < 32; i++) h[i] = nt_hidden(WS_POPUP);
    for (int i = 0; i < 32; i++) {
        CHECK(h[i] != NULL);
        for (int j = 0; j < i; j++) CHECK(h[i] != h[j]);
        if (h[i]) for (int j = 0; j < i; j++) if (h[j]) CHECK(((ULONG_PTR)h[i] & 0xffff) != ((ULONG_PTR)h[j] & 0xffff));
    }
    for (int i = 0; i < 32; i++) wcd_destroy(h[i]);
}

TEST(win_create_destroy_direct_flagsver_high_bits_ignored) {
    NEED_NT();
    /* only the low 16 bits (expWinVer) of a15 are used by CreateWindowEx; CW_FLAGS_VERSIONCLASS /
     * the ANSI marker in the top bits do not change the outcome of a Unicode creation. */
    static const DWORD f[] = { 0x40000000 | WCD_WINVER, 0xC0000000 | WCD_WINVER, 0x40000000 | 0x0500, WCD_WINVER };
    for (unsigned i = 0; i < sizeof f / sizeof f[0]; i++) {
        HWND h = wcd_nt_create_full(HIDE_EX, wcd_cls(), wcd_cls(), L"fv", WS_POPUP | WS_CAPTION, -32000, -32000, 100, 60, NULL, NULL, wcd_hinst(), NULL, 0, f[i], 0, NULL);
        CHECKF(h != NULL, "flagsver 0x%lx", (unsigned long)f[i]);
        if (!h) continue;
        /* low word >= 0x400 here, so WS_CAPTION gets WS_EX_WINDOWEDGE */
        CHECK((U(GetWindowLongW)(h, GWL_EXSTYLE) & WS_EX_WINDOWEDGE) != 0);
        wcd_destroy(h);
    }
}
