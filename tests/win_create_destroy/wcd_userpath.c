/* CreateWindowExW/A through user32 -> VerNtUserCreateWindowEx -> NtUserCreateWindowEx, then
 * validation of the window state the kernel produced (handle table, shared window words, class
 * lookup outcome, parent/owner relations, extended-style normalisation). */
#include "wcd.h"
#include <wchar.h>

#define NEED_USER() do { if (!wcd_user() || !U(CreateWindowExA) || !U(GetParent) || !U(GetWindow)) { t_skip("user32 missing"); return; } } while (0)
#define HIDE_EX (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)

static HWND cw(DWORD ex, const WCHAR *cls, const WCHAR *name, DWORD style, int x, int y, int w, int h, HWND parent, HMENU menu, LPVOID lp) {
    return U(CreateWindowExW)(ex, cls, name, style, x, y, w, h, parent, menu, wcd_hinst(), lp);
}

/* ---- class lookup failures ------------------------------------------------------------------ */
TEST(win_create_destroy_user_unregistered_class_w) {
    NEED_USER();
    WCHAR n[96]; wcd_uniq_name(n, 96, "none");
    SetLastError(0);
    HWND h = cw(HIDE_EX, n, L"x", WS_POPUP, 0, 0, 10, 10, NULL, NULL, NULL);
    CHECK(h == NULL);
    EXPECT_ERR(1407);
    wcd_destroy(h);
}

TEST(win_create_destroy_user_unregistered_class_a) {
    NEED_USER();
    char n[96]; snprintf(n, sizeof n, "win32kt%lu_none_a", GetCurrentProcessId());
    SetLastError(0);
    HWND h = U(CreateWindowExA)(HIDE_EX, n, "x", WS_POPUP, 0, 0, 10, 10, NULL, NULL, wcd_hinst(), NULL);
    CHECK(h == NULL);
    EXPECT_ERR(1407);
    wcd_destroy(h);
}

static const WCHAR *bogus_names[] = { L"", L" ", L"no such class", L"BUTTONX", L"Edit2", L"\x4e2d\x6587", L"a\\b", L"#99999" };
TEST(win_create_destroy_user_bogus_class_names) {
    NEED_USER();
    for (unsigned i = 0; i < sizeof bogus_names / sizeof bogus_names[0]; i++) {
        SetLastError(0);
        HWND h = cw(HIDE_EX, bogus_names[i], L"x", WS_POPUP, 0, 0, 10, 10, NULL, NULL, NULL);
        CHECKF(h == NULL, "name %u", i);
        EXPECT_ERR(1407);
        wcd_destroy(h);
    }
}

TEST(win_create_destroy_user_class_null_pointer) {
    NEED_USER();
    SetLastError(0);
    HWND h = cw(HIDE_EX, NULL, L"x", WS_POPUP, 0, 0, 10, 10, NULL, NULL, NULL);
    CHECK(h == NULL);
    t_note("NULL class name: last error %lu", GetLastError());
    wcd_destroy(h);
}

TEST(win_create_destroy_user_class_name_over_limit) {
    NEED_USER();
    WCHAR big[600];
    for (int i = 0; i < 599; i++) big[i] = L'q';
    big[599] = 0;
    SetLastError(0);
    HWND h = cw(HIDE_EX, big, L"x", WS_POPUP, 0, 0, 10, 10, NULL, NULL, NULL);
    CHECK(h == NULL);
    EXPECT_ERR(1407);
    wcd_destroy(h);
}

TEST(win_create_destroy_user_bad_atom) {
    NEED_USER();
    SetLastError(0);
    HWND h = cw(HIDE_EX, ((const WCHAR *)(ULONG_PTR)(0xBFFD)), L"x", WS_POPUP, 0, 0, 10, 10, NULL, NULL, NULL);
    CHECK(h == NULL);
    EXPECT_ERR(1407);
    wcd_destroy(h);
}

TEST(win_create_destroy_user_instance_mismatch) {
    NEED_USER();
    SetLastError(0);
    HWND h = U(CreateWindowExW)(HIDE_EX, wcd_cls(), L"x", WS_POPUP, 0, 0, 10, 10, NULL, NULL, wcd_other_module(), NULL);
    CHECK(h == NULL);
    EXPECT_ERR(1407);
    wcd_destroy(h);
}

TEST(win_create_destroy_user_instance_null_hinstance_recorded) {
    NEED_USER();
    HWND h = U(CreateWindowExW)(HIDE_EX, wcd_cls(), L"x", WS_POPUP, -32000, -32000, 10, 10, NULL, NULL, NULL, NULL);
    REQUIRE(h);
 /* window creation stores the caller's hInstance verbatim. */
    CHECK(U(GetWindowLongPtrW)(h, GWLP_HINSTANCE) == 0);
    wcd_destroy(h);
}

TEST(win_create_destroy_user_instance_exe_recorded) {
    NEED_USER();
    HWND h = wcd_make_top();
    REQUIRE(h);
    CHECK((HINSTANCE)U(GetWindowLongPtrW)(h, GWLP_HINSTANCE) == wcd_hinst());
    wcd_destroy(h);
}

TEST(win_create_destroy_user_global_class_ignores_instance) {
    NEED_USER();
    WCHAR n[96]; wcd_uniq_name(n, 96, "glob");
    ATOM a = wcd_register(n, NULL, CS_GLOBALCLASS, 0, 0);
    REQUIRE(a);
    /* second lookup of window creation walks the public class list with module 0 (any). */
    HWND h = U(CreateWindowExW)(HIDE_EX, n, L"g", WS_POPUP, -32000, -32000, 10, 10, NULL, NULL, wcd_other_module(), NULL);
    CHECK(h != NULL);
    wcd_destroy(h);
    wcd_unregister(n);
}

TEST(win_create_destroy_user_class_locked_by_window) {
    NEED_USER();
    WCHAR n[96]; wcd_uniq_name(n, 96, "lock");
    ATOM a = wcd_register(n, NULL, 0, 0, 0);
    REQUIRE(a);
    HWND h = cw(HIDE_EX, n, L"l", WS_POPUP, -32000, -32000, 10, 10, NULL, NULL, NULL);
    REQUIRE(h);
    SetLastError(0);
    /* the new window holds a reference on its tagCLS (ClassLock), so the class cannot go away */
    CHECK(!U(UnregisterClassW)(n, wcd_hinst()));
    EXPECT_ERR(1412);
    wcd_destroy(h);
    CHECK(U(UnregisterClassW)(n, wcd_hinst()));
}

TEST(win_create_destroy_user_class_failed_create_does_not_lock) {
    NEED_USER();
    WCHAR n[96]; wcd_uniq_name(n, 96, "fail");
    ATOM a = wcd_register(n, NULL, 0, 0, 0);
    REQUIRE(a);
    SetLastError(0);
    HWND h = cw(HIDE_EX, n, L"l", WS_CHILD, 0, 0, 10, 10, NULL, NULL, NULL); /* no parent: fails */
    CHECK(h == NULL);
    CHECK(U(UnregisterClassW)(n, wcd_hinst()));
}

TEST(win_create_destroy_user_class_by_atom) {
    NEED_USER();
    ATOM a = wcd_cls_atom();
    REQUIRE(a);
    HWND h = cw(HIDE_EX, ((const WCHAR *)(ULONG_PTR)(a)), L"a", WS_POPUP, -32000, -32000, 10, 10, NULL, NULL, NULL);
    CHECK(h != NULL);
    if (h) {
        WCHAR b[96];
        CHECK(U(GetClassNameW)(h, b, 96) > 0);
        EXPECT_WSTR(b, wcd_cls());
        CHECK(U(GetClassLongW)(h, GCW_ATOM) == a);
    }
    wcd_destroy(h);
}

TEST(win_create_destroy_user_class_case_insensitive) {
    NEED_USER();
    WCHAR lo[96]; wcsncpy(lo, wcd_cls(), 95); lo[95] = 0;
    for (WCHAR *p = lo; *p; p++) *p = (WCHAR)towlower(*p);
    HWND h = cw(HIDE_EX, lo, L"c", WS_POPUP, -32000, -32000, 10, 10, NULL, NULL, NULL);
    CHECK(h != NULL);
    if (h) {
        WCHAR b[96]; U(GetClassNameW)(h, b, 96);
        EXPECT_WSTR(b, wcd_cls()); /* the registered spelling is reported, not the lookup spelling */
    }
    wcd_destroy(h);
}

/* ---- system (default) classes, registered lazily by RegisterDefaultClass --------------------- */
static const WCHAR *sys_classes[] = { L"BUTTON", L"STATIC", L"EDIT", L"LISTBOX", L"COMBOBOX", L"SCROLLBAR", L"Button", L"static" };
TEST(win_create_destroy_user_system_classes_as_children) {
    NEED_USER();
    HWND p = wcd_make_top();
    REQUIRE(p);
    for (unsigned i = 0; i < sizeof sys_classes / sizeof sys_classes[0]; i++) {
        HWND c = cw(0, sys_classes[i], L"t", WS_CHILD, 0, 0, 60, 30, p, (HMENU)(ULONG_PTR)(100 + i), NULL);
        CHECKF(c != NULL, "class %u err %lu", i, GetLastError());
        if (!c) continue;
        WCHAR b[64];
        CHECK(U(GetClassNameW)(c, b, 64) > 0);
        CHECKF(!wcsicmp(b, sys_classes[i]), "got %ls", b);
        CHECK(U(GetParent)(c) == p);
        CHECK(U(IsWindowUnicode)(c));
        CHECK((U(GetClassLongW)(c, GCW_ATOM)) != 0);
    }
    wcd_destroy(p);
}

TEST(win_create_destroy_user_system_class_registered_after_first_use) {
    NEED_USER();
    HWND p = wcd_make_top();
    REQUIRE(p);
    HWND c = cw(0, L"LISTBOX", L"", WS_CHILD, 0, 0, 30, 30, p, (HMENU)1, NULL);
    CHECK(c != NULL);
    /* after the (kernel- or user32-side) default registration the class is queryable */
    WNDCLASSEXW wc; memset(&wc, 0, sizeof wc); wc.cbSize = sizeof wc;
    CHECK(U(GetClassInfoExW)(NULL, L"LISTBOX", &wc) != 0);
    wcd_destroy(p);
}

TEST(win_create_destroy_user_dialog_class_toplevel) {
    NEED_USER();
    HWND h = cw(HIDE_EX, L"#32770", L"dlg", WS_POPUP, -32000, -32000, 100, 60, NULL, NULL, NULL);
    CHECK(h != NULL);
    if (h) {
        WCHAR b[32]; U(GetClassNameW)(h, b, 32);
        EXPECT_WSTR(b, L"#32770");
    }
    wcd_destroy(h);
}

/* ---- ANSI vs Unicode -------------------------------------------------------------------------- */
static char g_ansi_name[32], g_ansi_cls[32];
static LRESULT CALLBACK ansi_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_NCCREATE && l) {
        CREATESTRUCTA *cs = (CREATESTRUCTA *)l;
        if (cs->lpszName) { k_try_read(cs->lpszName, g_ansi_name, 8); g_ansi_name[8] = 0; }
        if (((ULONG_PTR)cs->lpszClass & ~(ULONG_PTR)0xffff) != 0) { k_try_read(cs->lpszClass, g_ansi_cls, 8); g_ansi_cls[8] = 0; }
    }
    return U(DefWindowProcA)(h, m, w, l);
}

TEST(win_create_destroy_user_unicode_class_created_from_ansi_call) {
    NEED_USER();
    char cn[128]; snprintf(cn, sizeof cn, "%ls", wcd_cls());
    wcd_reset();
    HWND h = U(CreateWindowExA)(HIDE_EX, cn, "ansi-title", WS_POPUP, -32000, -32000, 20, 20, NULL, NULL, wcd_hinst(), NULL);
    CHECK(h != NULL);
    if (!h) return;
    CHECK(U(IsWindowUnicode)(h));
    WCHAR b[32] = { 0 }; int tl = U(GetWindowTextW)(h, b, 32);
    CHECKF(tl == 10, "len %d", tl);
    EXPECT_WSTR(b, L"ansi-title");
    int nc = wcd_find(WM_NCCREATE, 0);
    if (nc >= 0) CHECK(!wcsncmp(wcd_log[nc].cs_name, L"ansi-ti", 7)); /* a Unicode proc sees CREATESTRUCTW */
    wcd_destroy(h);
}

TEST(win_create_destroy_user_ansi_class_created_from_unicode_call) {
    NEED_USER();
    char cn[96]; snprintf(cn, sizeof cn, "win32kt%lu_ansi", GetCurrentProcessId());
    WNDCLASSEXA wc; memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc; wc.lpfnWndProc = ansi_proc; wc.hInstance = wcd_hinst(); wc.lpszClassName = cn;
    ATOM a = U(RegisterClassExA)(&wc);
    REQUIRE(a);
    WCHAR wn[96]; swprintf(wn, 96, L"%hs", cn);
    memset(g_ansi_name, 0, sizeof g_ansi_name); memset(g_ansi_cls, 0, sizeof g_ansi_cls);
    HWND h = cw(HIDE_EX, wn, L"wide-title", WS_POPUP, -32000, -32000, 20, 20, NULL, NULL, NULL);
    CHECK(h != NULL);
    if (h) {
        CHECK(!U(IsWindowUnicode)(h));   /* WFANSIPROC follows the class (SetOrClrWF(class flag & 1, 0x204)) */
        CHECK(!strncmp(g_ansi_name, "wide-tit", 8)); /* kernel marshals CREATESTRUCT as ANSI for an ANSI proc */
        char b[32] = { 0 };
        CHECK(U(GetWindowTextA)(h, b, 32) == 10);
        CHECK(!strcmp(b, "wide-title"));
        wcd_destroy(h);
    }
    U(UnregisterClassA)(cn, wcd_hinst());
}

TEST(win_create_destroy_user_ansi_class_ansi_call) {
    NEED_USER();
    char cn[96]; snprintf(cn, sizeof cn, "win32kt%lu_ansi2", GetCurrentProcessId());
    WNDCLASSEXA wc; memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc; wc.lpfnWndProc = ansi_proc; wc.hInstance = wcd_hinst(); wc.lpszClassName = cn;
    REQUIRE(U(RegisterClassExA)(&wc));
    memset(g_ansi_name, 0, sizeof g_ansi_name);
    HWND h = U(CreateWindowExA)(HIDE_EX, cn, "plain", WS_POPUP, -32000, -32000, 20, 20, NULL, NULL, wcd_hinst(), NULL);
    CHECK(h != NULL);
    if (h) {
        CHECK(!U(IsWindowUnicode)(h));
        CHECK(!strcmp(g_ansi_name, "plain"));
        wcd_destroy(h);
    }
    U(UnregisterClassA)(cn, wcd_hinst());
}

/* ---- parent / owner rules ----------------------------------------------------------------------- */
TEST(win_create_destroy_user_child_requires_parent) {
    NEED_USER();
    SetLastError(0);
    HWND h = cw(0, wcd_cls(), L"c", WS_CHILD, 0, 0, 10, 10, NULL, (HMENU)1, NULL);
    CHECK(h == NULL);
    EXPECT_ERR(1406);
}

TEST(win_create_destroy_user_child_parent_relations) {
    NEED_USER();
    HWND p = wcd_make_top();
    REQUIRE(p);
    HWND c = cw(0, wcd_cls(), L"c", WS_CHILD, 5, 6, 30, 20, p, (HMENU)9, NULL);
    REQUIRE(c);
    CHECK(U(GetParent)(c) == p);
    CHECK(U(GetAncestor)(c, GA_PARENT) == p);
    CHECK(U(GetAncestor)(c, GA_ROOT) == p);
    CHECK(U(IsChild)(p, c));
    CHECK(!U(IsChild)(c, p));
    CHECK(U(GetWindow)(c, GW_OWNER) == NULL);
    CHECK(U(GetWindow)(p, GW_CHILD) == c);
    CHECK(U(GetDlgCtrlID)(c) == 9);
    CHECK((U(GetWindowLongW)(c, GWL_STYLE) & WS_CHILD) != 0);
    CHECK(U(GetAncestor)(p, GA_PARENT) == U(GetDesktopWindow)());
    wcd_destroy(p);
}

TEST(win_create_destroy_user_child_coordinates_parent_relative) {
    NEED_USER();
    HWND p = wcd_make(HIDE_EX, WS_POPUP, NULL, 100, 120, 200, 150);
    REQUIRE(p);
    HWND c = cw(0, wcd_cls(), L"c", WS_CHILD, 7, 9, 30, 20, p, (HMENU)1, NULL);
    REQUIRE(c);
    RECT r, pr; POINT pt = { 0, 0 };
    U(GetWindowRect)(c, &r); U(GetWindowRect)(p, &pr);
    if (wcd_dpi_plain()) {
        /* rcWindow is screen based: parent origin + (x,y) */
        EXPECT_EQ(r.left, pr.left + 7); EXPECT_EQ(r.top, pr.top + 9);
        EXPECT_EQ(r.right - r.left, 30); EXPECT_EQ(r.bottom - r.top, 20);
    }
    U(ClientToScreen)(c, &pt);
    CHECK(pt.x == r.left && pt.y == r.top);
    wcd_destroy(p);
}

TEST(win_create_destroy_user_popup_owner_relations) {
    NEED_USER();
    HWND o = wcd_make_top();
    REQUIRE(o);
    HWND p = cw(HIDE_EX, wcd_cls(), L"owned", WS_POPUP, -32000, -32000, 20, 20, o, NULL, NULL);
    REQUIRE(p);
    CHECK(U(GetWindow)(p, GW_OWNER) == o);
    CHECK(U(GetParent)(p) == o);                      /* GetParent reports the owner for popups */
    CHECK(U(GetAncestor)(p, GA_PARENT) == U(GetDesktopWindow)()); /* but the window is still a desktop child */
    CHECK(!U(IsChild)(o, p));
    wcd_destroy(o);
    CHECK(!U(IsWindow)(p));
}

TEST(win_create_destroy_user_popup_owned_by_child_gets_toplevel_owner) {
    NEED_USER();
    HWND top = wcd_make_top();
    REQUIRE(top);
    HWND child = cw(0, wcd_cls(), L"c", WS_CHILD, 0, 0, 20, 20, top, (HMENU)1, NULL);
    REQUIRE(child);
    HWND pop = cw(HIDE_EX, wcd_cls(), L"p", WS_POPUP, -32000, -32000, 20, 20, child, NULL, NULL);
    CHECK(pop != NULL);
    if (pop) {
        /* window creation: a non-child window whose parent argument is a child is owned by
         * GetNonChildAncestor(parent) */
        CHECK(U(GetWindow)(pop, GW_OWNER) == top);
    }
    wcd_destroy(top);
}

TEST(win_create_destroy_user_overlapped_with_owner) {
    NEED_USER();
    HWND o = wcd_make_top();
    REQUIRE(o);
    HWND w = cw(HIDE_EX, wcd_cls(), L"ov", WS_OVERLAPPEDWINDOW, -32000, -32000, 200, 150, o, NULL, NULL);
    CHECK(w != NULL);
    if (w) CHECK(U(GetWindow)(w, GW_OWNER) == o);
    wcd_destroy(o);
}

TEST(win_create_destroy_user_owner_survives_owned_destroy) {
    NEED_USER();
    HWND o = wcd_make_top();
    REQUIRE(o);
    HWND p = cw(HIDE_EX, wcd_cls(), L"p", WS_POPUP, -32000, -32000, 20, 20, o, NULL, NULL);
    REQUIRE(p);
    wcd_destroy(p);
    CHECK(U(IsWindow)(o));
    CHECK(U(GetWindow)(o, GW_OWNER) == NULL);
    wcd_destroy(o);
}

TEST(win_create_destroy_user_invalid_parent_handle) {
    NEED_USER();
    SetLastError(0);
    HWND h = cw(HIDE_EX, wcd_cls(), L"x", WS_POPUP, 0, 0, 10, 10, (HWND)(ULONG_PTR)0x12345678, NULL, NULL);
    CHECK(h == NULL);
    EXPECT_ERR(1400);
}

TEST(win_create_destroy_user_destroyed_parent_handle) {
    NEED_USER();
    HWND p = wcd_make_top();
    REQUIRE(p);
    wcd_destroy(p);
    SetLastError(0);
    HWND h = cw(0, wcd_cls(), L"x", WS_CHILD, 0, 0, 10, 10, p, (HMENU)1, NULL);
    CHECK(h == NULL);
    EXPECT_ERR(1400);
}

TEST(win_create_destroy_user_invalid_menu_handle) {
    NEED_USER();
    SetLastError(0);
    HWND h = cw(HIDE_EX, wcd_cls(), L"x", WS_POPUP, 0, 0, 10, 10, NULL, (HMENU)(ULONG_PTR)0x4321, NULL);
    CHECK(h == NULL);
    EXPECT_ERR(1401);
}

TEST(win_create_destroy_user_menu_assigned) {
    NEED_USER();
    if (!U(CreateMenu) || !U(GetMenu)) { t_skip("menu"); return; }
    HMENU m = U(CreateMenu)();
    REQUIRE(m);
    HWND h = U(CreateWindowExW)(HIDE_EX, wcd_cls(), L"m", WS_POPUP, -32000, -32000, 100, 60, NULL, m, wcd_hinst(), NULL);
    CHECK(h != NULL);
    if (h) {
        CHECK(U(GetMenu)(h) == m);
        wcd_destroy(h);
    } else U(DestroyMenu)(m);
}

TEST(win_create_destroy_user_nested_children) {
    NEED_USER();
    HWND chain[24];
    chain[0] = wcd_make_top();
    REQUIRE(chain[0]);
    int depth = 1;
    for (; depth < 24; depth++) {
        chain[depth] = cw(WS_EX_NOPARENTNOTIFY, wcd_cls(), L"n", WS_CHILD, 0, 0, 20, 20, chain[depth - 1], (HMENU)(ULONG_PTR)depth, NULL);
        if (!chain[depth]) break;
    }
    CHECKF(depth == 24, "stopped at depth %d err %lu", depth, GetLastError());
    for (int i = 1; i < depth; i++) {
        CHECK(U(GetParent)(chain[i]) == chain[i - 1]);
        CHECK(U(GetAncestor)(chain[i], GA_ROOT) == chain[0]);
    }
    wcd_destroy(chain[0]);
    for (int i = 0; i < depth; i++) CHECK(!U(IsWindow)(chain[i]));
}

TEST(win_create_destroy_user_child_of_child_sibling_order) {
    NEED_USER();
    HWND p = wcd_make_top();
    REQUIRE(p);
    HWND a = cw(0, wcd_cls(), L"a", WS_CHILD, 0, 0, 10, 10, p, (HMENU)1, NULL);
    HWND b = cw(0, wcd_cls(), L"b", WS_CHILD, 0, 0, 10, 10, p, (HMENU)2, NULL);
    HWND c = cw(0, wcd_cls(), L"c", WS_CHILD, 0, 0, 10, 10, p, (HMENU)3, NULL);
    REQUIRE(a && b && c);
    /* child windows are linked behind their siblings (creation order == Z order, the way dialog
     * controls get their tab order): oldest is GW_CHILD, newest is GW_HWNDLAST */
    CHECK(U(GetWindow)(p, GW_CHILD) == a);
    CHECK(U(GetWindow)(a, GW_HWNDNEXT) == b);
    CHECK(U(GetWindow)(b, GW_HWNDNEXT) == c);
    CHECK(U(GetWindow)(c, GW_HWNDNEXT) == NULL);
    CHECK(U(GetWindow)(a, GW_HWNDLAST) == c);
    CHECK(U(GetWindow)(c, GW_HWNDPREV) == b);
    wcd_destroy(p);
}

TEST(win_create_destroy_user_both_child_and_popup_style) {
    NEED_USER();
    SetLastError(0);
    HWND h = cw(HIDE_EX, wcd_cls(), L"cp", WS_CHILD | WS_POPUP, -32000, -32000, 10, 10, NULL, NULL, NULL);
    /* (style & 0xc0000000) != WS_CHILD, so the kernel handles it as a top-level popup */
    CHECK(h != NULL);
    if (h) {
        CHECK(U(GetParent)(h) == NULL);
        CHECK(U(GetAncestor)(h, GA_PARENT) == U(GetDesktopWindow)());
        wcd_destroy(h);
    }
}

/* ---- message-only windows -------------------------------------------------------------------- */
static BOOL CALLBACK enum_find(HWND h, LPARAM lp) { HWND *t = (HWND *)lp; if (h == t[0]) t[1] = h; return TRUE; }
TEST(win_create_destroy_user_message_only_basic) {
    NEED_USER();
    HWND h = U(CreateWindowExW)(0, wcd_cls(), L"msgonly", WS_POPUP, 0, 0, 0, 0, WCD_HWND_MESSAGE, NULL, wcd_hinst(), NULL);
    CHECK(h != NULL);
    if (!h) return;
    CHECK(U(IsWindow)(h));
    CHECK(!U(IsWindowVisible)(h));
    HWND probe[2] = { h, NULL };
    U(EnumWindows)(enum_find, (LPARAM)probe);
    CHECK(probe[1] == NULL);   /* message-only windows are not top-level children of the desktop */
    CHECK(wcd_entry_live(h) || !k_shared_info(NULL));
    DWORD pid = 0;
    CHECK(U(GetWindowThreadProcessId)(h, &pid) == GetCurrentThreadId());
    CHECK(pid == GetCurrentProcessId());
    HWND par = U(GetAncestor)(h, GA_PARENT);
    t_note("message-only GA_PARENT=%p desktop=%p GetParent=%p", par, U(GetDesktopWindow)(), U(GetParent)(h));
    CHECK(par != U(GetDesktopWindow)());
    wcd_destroy(h);
}

TEST(win_create_destroy_user_message_only_findwindow) {
    NEED_USER();
    WCHAR n[96]; wcd_uniq_name(n, 96, "mo");
    ATOM a = wcd_register(n, NULL, 0, 0, 0);
    REQUIRE(a);
    HWND h = U(CreateWindowExW)(0, n, L"mo-title", WS_POPUP, 0, 0, 0, 0, WCD_HWND_MESSAGE, NULL, wcd_hinst(), NULL);
    REQUIRE(h);
    CHECK(U(FindWindowW)(n, NULL) == h);                                 /* Windows 10 FindWindow does find a message-only window by class */
    CHECK(U(FindWindowExW)(WCD_HWND_MESSAGE, NULL, n, NULL) == h);        /* ... FindWindowEx(HWND_MESSAGE) finds them */
    CHECK(U(FindWindowExW)(WCD_HWND_MESSAGE, NULL, n, L"mo-title") == h);
    wcd_destroy(h);
    CHECK(U(FindWindowExW)(WCD_HWND_MESSAGE, NULL, n, NULL) == NULL);
    wcd_unregister(n);
}

TEST(win_create_destroy_user_message_only_receives_messages) {
    NEED_USER();
    wcd_reset();
    HWND h = U(CreateWindowExW)(0, wcd_cls(), L"mo", WS_POPUP, 0, 0, 0, 0, WCD_HWND_MESSAGE, NULL, wcd_hinst(), NULL);
    REQUIRE(h);
    CHECK(U(SendMessageW)(h, WM_USER + 5, 1, 2) == 0);
    CHECK(U(PostMessageW)(h, WM_USER + 6, 3, 4));
    k_pump(50);
    CHECK(wcd_find_hwnd(WM_USER + 5, h, 0) >= 0);
    CHECK(wcd_find_hwnd(WM_USER + 6, h, 0) >= 0);
    wcd_destroy(h);
}

TEST(win_create_destroy_user_message_only_child) {
    NEED_USER();
    HWND h = U(CreateWindowExW)(0, wcd_cls(), L"mo", WS_CHILD, 0, 0, 10, 10, WCD_HWND_MESSAGE, (HMENU)1, wcd_hinst(), NULL);
    CHECK(h != NULL);
    if (h) {
        HWND probe[2] = { h, NULL };
        U(EnumWindows)(enum_find, (LPARAM)probe);
        CHECK(probe[1] == NULL);
        wcd_destroy(h);
    }
}

TEST(win_create_destroy_user_message_only_parent_for_children) {
    NEED_USER();
    HWND m = U(CreateWindowExW)(0, wcd_cls(), L"mo", WS_POPUP, 0, 0, 0, 0, WCD_HWND_MESSAGE, NULL, wcd_hinst(), NULL);
    REQUIRE(m);
    HWND c = cw(0, wcd_cls(), L"c", WS_CHILD, 0, 0, 10, 10, m, (HMENU)1, NULL);
    CHECK(c != NULL);
    if (c) CHECK(U(GetParent)(c) == m);
    wcd_destroy(m);
}

/* ---- default sizes ------------------------------------------------------------------------------- */
TEST(win_create_destroy_user_usedefault_overlapped) {
    NEED_USER();
    HWND h = cw(HIDE_EX, wcd_cls(), L"d", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, NULL);
    CHECK(h != NULL);
    if (!h) return;
    RECT r; U(GetWindowRect)(h, &r);
    CHECKF(r.right - r.left > 0 && r.bottom - r.top > 0, "rect %ld,%ld,%ld,%ld", r.left, r.top, r.right, r.bottom);
    t_info("CW_USEDEFAULT overlapped rect %ld,%ld %ldx%ld", r.left, r.top, r.right - r.left, r.bottom - r.top);
    wcd_destroy(h);
}

TEST(win_create_destroy_user_usedefault_size_only) {
    NEED_USER();
    HWND h = cw(HIDE_EX, wcd_cls(), L"d", WS_OVERLAPPEDWINDOW, 40, 50, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, NULL);
    CHECK(h != NULL);
    if (!h) return;
    RECT r; U(GetWindowRect)(h, &r);
    CHECK(r.right - r.left > 0 && r.bottom - r.top > 0);
    if (wcd_dpi_plain()) { EXPECT_EQ(r.left, 40); EXPECT_EQ(r.top, 50); }
    wcd_destroy(h);
}

TEST(win_create_destroy_user_usedefault_popup_child_note) {
    NEED_USER();
    HWND pp = cw(HIDE_EX, wcd_cls(), L"d", WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, NULL);
    CHECK(pp != NULL);
    if (pp) {
        RECT r; U(GetWindowRect)(pp, &r);
        t_note("CW_USEDEFAULT popup rect %ld,%ld %ldx%ld", r.left, r.top, r.right - r.left, r.bottom - r.top);
        HWND c = cw(0, wcd_cls(), L"d", WS_CHILD, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, pp, (HMENU)1, NULL);
        CHECK(c != NULL);
        if (c) {
            RECT cr; U(GetClientRect)(c, &cr);
            t_note("CW_USEDEFAULT child client %ldx%ld", cr.right, cr.bottom);
        }
        wcd_destroy(pp);
    }
}

TEST(win_create_destroy_user_explicit_size_exact_for_popup) {
    NEED_USER();
    static const int sz[][2] = { { 1, 1 }, { 17, 33 }, { 100, 80 }, { 640, 480 }, { 3000, 2000 } };
    for (unsigned i = 0; i < sizeof sz / sizeof sz[0]; i++) {
        HWND h = cw(HIDE_EX, wcd_cls(), L"s", WS_POPUP, -32000, -32000, sz[i][0], sz[i][1], NULL, NULL, NULL);
        CHECKF(h != NULL, "size %u", i);
        if (!h) continue;
        RECT r; U(GetWindowRect)(h, &r);
        if (wcd_dpi_plain()) { EXPECT_EQ(r.right - r.left, sz[i][0]); EXPECT_EQ(r.bottom - r.top, sz[i][1]); }
        RECT c; U(GetClientRect)(h, &c);
        if (wcd_dpi_plain()) { EXPECT_EQ(c.right, sz[i][0]); EXPECT_EQ(c.bottom, sz[i][1]); }
        wcd_destroy(h);
    }
}

TEST(win_create_destroy_user_caption_window_client_smaller_than_window) {
    NEED_USER();
    HWND h = cw(HIDE_EX, wcd_cls(), L"cap", WS_OVERLAPPEDWINDOW, -32000, -32000, 300, 200, NULL, NULL, NULL);
    REQUIRE(h);
    RECT w, c; U(GetWindowRect)(h, &w); U(GetClientRect)(h, &c);
    CHECK(c.right < w.right - w.left);
    CHECK(c.bottom < w.bottom - w.top);
    wcd_destroy(h);
}

/* ---- extended style normalisation ---------------------------------------------------------------- */
TEST(win_create_destroy_user_exstyle_bit11_becomes_toolwindow) {
    NEED_USER();
    /* CreateWindowInternal: if (ex & 0x800) ex = (ex & ~0x800) | WS_EX_TOOLWINDOW */
    HWND h = cw(WS_EX_NOACTIVATE | 0x800, wcd_cls(), L"x", WS_POPUP, -32000, -32000, 10, 10, NULL, NULL, NULL);
    REQUIRE(h);
    LONG ex = U(GetWindowLongW)(h, GWL_EXSTYLE);
    CHECK((ex & WS_EX_TOOLWINDOW) != 0);
    CHECK((ex & 0x800) == 0);
    wcd_destroy(h);
}

TEST(win_create_destroy_user_exstyle_preserved_table) {
    NEED_USER();
    static const DWORD bits[] = { WS_EX_TOPMOST, WS_EX_TRANSPARENT, WS_EX_ACCEPTFILES, WS_EX_APPWINDOW,
        WS_EX_NOPARENTNOTIFY, WS_EX_RIGHT, WS_EX_RTLREADING, WS_EX_LEFTSCROLLBAR, WS_EX_CONTROLPARENT,
        WS_EX_NOINHERITLAYOUT, WS_EX_LAYERED, WS_EX_COMPOSITED, WS_EX_LAYOUTRTL, WS_EX_NOREDIRECTIONBITMAP };
    for (unsigned i = 0; i < sizeof bits / sizeof bits[0]; i++) {
        HWND h = cw(HIDE_EX | bits[i], wcd_cls(), L"x", WS_POPUP, -32000, -32000, 10, 10, NULL, NULL, NULL);
        CHECKF(h != NULL, "bit 0x%lx err %lu", (unsigned long)bits[i], GetLastError());
        if (!h) continue;
        LONG ex = U(GetWindowLongW)(h, GWL_EXSTYLE);
        CHECKF((ex & bits[i]) == bits[i], "bit 0x%lx ex 0x%lx", (unsigned long)bits[i], (unsigned long)ex);
        wcd_destroy(h);
    }
}

TEST(win_create_destroy_user_exstyle_windowedge_table) {
    NEED_USER();
    struct { DWORD style, ex; int want; const char *why; } t[] = {
        { WS_POPUP, 0, 0, "popup" },
        { WS_POPUP, WS_EX_WINDOWEDGE, 0, "stripped without need" },
        { WS_POPUP, WS_EX_DLGMODALFRAME, 1, "dlgmodalframe" },
        { WS_POPUP | WS_THICKFRAME, 0, 1, "thickframe" },
        { WS_POPUP | WS_DLGFRAME, 0, 1, "dlgframe" },
        { WS_POPUP | WS_CAPTION, 0, 1, "caption (exe expwinver >= 4.0)" },
        { WS_POPUP | WS_CAPTION, WS_EX_STATICEDGE, 0, "caption + staticedge" },
        { WS_OVERLAPPEDWINDOW, 0, 1, "overlappedwindow" },
        { WS_POPUP | WS_BORDER, 0, 0, "border" },
    };
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
        HWND h = cw(HIDE_EX | t[i].ex, wcd_cls(), L"x", t[i].style, -32000, -32000, 200, 150, NULL, NULL, NULL);
        CHECKF(h != NULL, "case %u", i);
        if (!h) continue;
        LONG ex = U(GetWindowLongW)(h, GWL_EXSTYLE);
        CHECKF(!!(ex & WS_EX_WINDOWEDGE) == t[i].want, "case %u %s: ex 0x%lx", i, t[i].why, (unsigned long)ex);
        wcd_destroy(h);
    }
}

TEST(win_create_destroy_user_exstyle_shared_word_matches) {
    NEED_USER();
    static const DWORD ex[] = { WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_DLGMODALFRAME,
                                WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_TRANSPARENT };
    for (unsigned i = 0; i < 3; i++) {
        HWND h = cw(ex[i], wcd_cls(), L"x", WS_POPUP | WS_CAPTION, -32000, -32000, 200, 100, NULL, NULL, NULL);
        REQUIRE(h);
        void *sw = wcd_sw(h);
        unsigned long st = 0, e = 0;
        if (sw && k_try_read_u32((char *)sw + SW_STYLE, &st) && k_try_read_u32((char *)sw + SW_EXSTYLE, &e)) {
            CHECK(st == (unsigned long)U(GetWindowLongW)(h, GWL_STYLE));
            CHECK(e == (unsigned long)U(GetWindowLongW)(h, GWL_EXSTYLE));
            CHECK((e & ex[i]) == ex[i]);
        } else t_note("shared words not reachable");
        wcd_destroy(h);
    }
}

/* ---- visibility ------------------------------------------------------------------------------------ */
TEST(win_create_destroy_user_visible_style_semantics) {
    NEED_USER();
    HWND hid = cw(HIDE_EX, wcd_cls(), L"h", WS_POPUP, -32000, -32000, 20, 20, NULL, NULL, NULL);
    HWND vis = cw(HIDE_EX, wcd_cls(), L"v", WS_POPUP | WS_VISIBLE, -32000, -32000, 20, 20, NULL, NULL, NULL);
    REQUIRE(hid && vis);
    CHECK(!(U(GetWindowLongW)(hid, GWL_STYLE) & WS_VISIBLE));
    CHECK((U(GetWindowLongW)(vis, GWL_STYLE) & WS_VISIBLE));
    CHECK(!U(IsWindowVisible)(hid));
    CHECK(U(IsWindowVisible)(vis));
    wcd_destroy(hid); wcd_destroy(vis);
}

TEST(win_create_destroy_user_visible_child_of_hidden_parent) {
    NEED_USER();
    HWND p = wcd_make_top();
    REQUIRE(p);
    HWND c = cw(0, wcd_cls(), L"c", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, p, (HMENU)1, NULL);
    REQUIRE(c);
    CHECK((U(GetWindowLongW)(c, GWL_STYLE) & WS_VISIBLE) != 0);
    CHECK(!U(IsWindowVisible)(c));   /* IsWindowVisible walks the parent chain */
    wcd_destroy(p);
}

TEST(win_create_destroy_user_visible_child_of_visible_parent) {
    NEED_USER();
    HWND p = cw(HIDE_EX, wcd_cls(), L"p", WS_POPUP | WS_VISIBLE, -32000, -32000, 50, 50, NULL, NULL, NULL);
    REQUIRE(p);
    HWND c = cw(0, wcd_cls(), L"c", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, p, (HMENU)1, NULL);
    REQUIRE(c);
    CHECK(U(IsWindowVisible)(c));
    U(ShowWindow)(p, SW_HIDE);
    CHECK(!U(IsWindowVisible)(c));
    wcd_destroy(p);
}

TEST(win_create_destroy_user_disabled_style) {
    NEED_USER();
    HWND h = cw(HIDE_EX, wcd_cls(), L"d", WS_POPUP | WS_DISABLED, -32000, -32000, 20, 20, NULL, NULL, NULL);
    REQUIRE(h);
    CHECK(!U(IsWindowEnabled)(h));
    wcd_destroy(h);
}

TEST(win_create_destroy_user_extra_bytes_and_class_extra) {
    NEED_USER();
    WCHAR n[96]; wcd_uniq_name(n, 96, "cbx");
    REQUIRE(wcd_register(n, NULL, 0, 16, 24));
    HWND h = cw(HIDE_EX, n, L"x", WS_POPUP, -32000, -32000, 10, 10, NULL, NULL, NULL);
    REQUIRE(h);
    CHECK(U(GetClassLongW)(h, GCL_CBWNDEXTRA) == 24);
    CHECK(U(GetClassLongW)(h, GCL_CBCLSEXTRA) == 16);
    for (int off = 0; off < 24; off += 8) CHECK(U(GetWindowLongPtrW)(h, off) == 0);
    wcd_destroy(h);
    wcd_unregister(n);
}

TEST(win_create_destroy_user_lpparam_roundtrip) {
    NEED_USER();
    wcd_reset();
    LPVOID tag = (LPVOID)(ULONG_PTR)0xA5A5A5A55A5A5A5Aull;
    HWND h = cw(HIDE_EX, wcd_cls(), L"lp", WS_POPUP, -32000, -32000, 10, 10, NULL, NULL, tag);
    REQUIRE(h);
    int nc = wcd_find(WM_NCCREATE, 0), cr = wcd_find(WM_CREATE, 0);
    REQUIRE(nc >= 0 && cr >= 0);
    CHECK(wcd_log[nc].cs.lpCreateParams == tag);
    CHECK(wcd_log[cr].cs.lpCreateParams == tag);
    wcd_destroy(h);
}

TEST(win_create_destroy_user_window_text_variants) {
    NEED_USER();
    static const WCHAR *tx[] = { L"", L"x", L"with spaces and \x00e4\x00f6", L"line\r\nbreak" };
    for (unsigned i = 0; i < sizeof tx / sizeof tx[0]; i++) {
        HWND h = cw(HIDE_EX, wcd_cls(), tx[i], WS_POPUP, -32000, -32000, 10, 10, NULL, NULL, NULL);
        CHECKF(h != NULL, "text %u", i);
        if (!h) continue;
        WCHAR b[64] = { 0 };
        U(GetWindowTextW)(h, b, 64);
        CHECKF(!wcscmp(b, tx[i]), "text %u got len %d first %x", i, (int)wcslen(b), b[0]);
        wcd_destroy(h);
    }
    HWND n = cw(HIDE_EX, wcd_cls(), NULL, WS_POPUP, -32000, -32000, 10, 10, NULL, NULL, NULL);
    CHECK(n != NULL);
    if (n) CHECK(U(GetWindowTextLengthW)(n) == 0);
    wcd_destroy(n);
}

TEST(win_create_destroy_user_cross_thread_child) {
    NEED_USER();
    struct wcd_gt g; memset(&g, 0, sizeof g);
    CHECK(wcd_gt_start(&g, 1));
    if (!g.top) { wcd_gt_stop(&g); t_skip("helper thread window"); return; }
    /* a child may live on a different thread than its parent; NOPARENTNOTIFY avoids waiting for
     * the parent's thread to run WM_PARENTNOTIFY */
    HWND c = cw(WS_EX_NOPARENTNOTIFY, wcd_cls(), L"xc", WS_CHILD, 0, 0, 10, 10, g.top, (HMENU)3, NULL);
    CHECK(c != NULL);
    if (c) {
        CHECK(U(GetParent)(c) == g.top);
        DWORD pid;
        CHECK(U(GetWindowThreadProcessId)(c, &pid) == GetCurrentThreadId());
        CHECK(U(GetWindowThreadProcessId)(g.top, &pid) == g.tid);
        wcd_destroy(c);
    }
    wcd_gt_stop(&g);
}

TEST(win_create_destroy_user_owner_on_other_thread) {
    NEED_USER();
    struct wcd_gt g; memset(&g, 0, sizeof g);
    CHECK(wcd_gt_start(&g, 1));
    if (!g.top) { wcd_gt_stop(&g); t_skip("helper thread window"); return; }
    HWND p = cw(HIDE_EX | WS_EX_NOPARENTNOTIFY, wcd_cls(), L"o", WS_POPUP, -32000, -32000, 10, 10, g.top, NULL, NULL);
    CHECK(p != NULL);
    if (p) {
        CHECK(U(GetWindow)(p, GW_OWNER) == g.top);
        wcd_destroy(p);
    }
    wcd_gt_stop(&g);
}

TEST(win_create_destroy_user_gui_resources_accounting) {
    NEED_USER();
    HWND warm = wcd_make_top();
    REQUIRE(warm);
    int base = wcd_user_objects();
    if (base <= 0) { wcd_destroy(warm); t_skip("GetGuiResources unusable"); return; }
    HWND p = wcd_make_top();
    HWND c[5];
    for (int i = 0; i < 5; i++) c[i] = cw(0, wcd_cls(), L"c", WS_CHILD, 0, 0, 5, 5, p, (HMENU)(ULONG_PTR)(i + 1), NULL);
    CHECKF(wcd_user_objects() == base + 6, "have %d base %d", wcd_user_objects(), base);
    wcd_destroy(p);   /* destroys the children with it */
    CHECKF(wcd_user_objects() == base, "after %d base %d", wcd_user_objects(), base);
    wcd_destroy(warm);
}
