/* Class names live in the same session atom table as registered messages (win32kfull!NtUserRegisterClassExWOW
 * resolves the name with UserFindAtom/UserAddAtom, NtUserGetClassName returns UserGetAtomName(class atom)),
 * and window properties use the kernel32 global atom table (user32!SetPropW -> GlobalAddAtomW). */
#include "mta.h"

static UINT rwm(const WCHAR *s) { return U(RegisterWindowMessageW)(s); }
#define MIA(a) ((LPCWSTR)(ULONG_PTR)(WORD)(a))
static void uname(WCHAR *out, const char *tag) { mta_name(out, 300, tag); }

static ATOM reg_class(const WCHAR *name) {
    WNDCLASSEXW wc; memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc; wc.lpfnWndProc = (WNDPROC)U(DefWindowProcW);
    wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = name;
    return U(RegisterClassExW)(&wc);
}
static HWND make_of_class(const WCHAR *cls) {
    return U(CreateWindowExW)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls, L"c", WS_POPUP, -32000, -32000, 1, 1, NULL, NULL, GetModuleHandleW(NULL), NULL);
}
static int wceq_i(const WCHAR *a, const WCHAR *b) { return !lstrcmpiW(a, b); }

TEST(msg_timers_atoms_class_atom_is_user_atom) {
    MTA_NEED();
    WCHAR n[300], b[300]; uname(n, "cls_atom");
    ATOM a = reg_class(n); REQUIRE(a);
    CHECKF(a >= 0xC000, "class atom 0x%x", a);
    memset(b, 0, sizeof b);
    MEQ(U(GetClipboardFormatNameW)(a, b, 300), wcslen(n));    /* class atoms are readable like message atoms */
    CHECK(!wcscmp(b, n));
    U(UnregisterClassW)(n, GetModuleHandleW(NULL));
}
TEST(msg_timers_atoms_class_name_shares_atom_with_message_name) {
    MTA_NEED();
    WCHAR n[300]; uname(n, "cls_share");
    ATOM a = reg_class(n); REQUIRE(a);
    MEQ(rwm(n), a);              /* same string, same table, same atom */
    WCHAR up[300]; wcscpy(up, n); for (WCHAR *p = up; *p; p++) if (*p >= L'a' && *p <= L'z') *p -= 32;
    MEQ(rwm(up), a);
    U(UnregisterClassW)(n, GetModuleHandleW(NULL));
}
TEST(msg_timers_atoms_message_registered_first_gives_class_the_same_atom) {
    MTA_NEED();
    WCHAR n[300]; uname(n, "cls_msgfirst");
    UINT m = rwm(n); REQUIRE(m);
    ATOM a = reg_class(n); REQUIRE(a);
    MEQ(a, m);
    U(UnregisterClassW)(n, GetModuleHandleW(NULL));
}
TEST(msg_timers_atoms_class_getclassname_and_gcw_atom) {
    MTA_NEED();
    WCHAR n[300], b[300]; uname(n, "cls_gcn");
    ATOM a = reg_class(n); REQUIRE(a);
    HWND w = make_of_class(n); REQUIRE(w);
    MEQ(U(GetClassNameW)(w, b, 300), wcslen(n));
    CHECK(!wcscmp(b, n));
    MEQ((ATOM)U(GetClassLongW)(w, GCW_ATOM), a);
    U(DestroyWindow)(w);
    U(UnregisterClassW)(n, GetModuleHandleW(NULL));
}
TEST(msg_timers_atoms_class_getclassname_truncation_table) {
    MTA_NEED();
    WCHAR n[300], b[300]; uname(n, "cls_trunc");
    ATOM a = reg_class(n); REQUIRE(a);
    HWND w = make_of_class(n); REQUIRE(w);
    unsigned len = (unsigned)wcslen(n);
    static const unsigned cch[] = { 2, 3, 6, 12 };
    for (unsigned i = 0; i < sizeof cch / sizeof cch[0]; i++) {
        memset(b, 0xAA, sizeof b);
        UINT r = U(GetClassNameW)(w, b, cch[i]);
        CHECKF(r == cch[i] - 1, "cch %u -> %u", cch[i], r);     /* UserGetAtomName cuts to cch-1 characters */
        CHECK(b[cch[i] - 1] == 0 && !wcsncmp(b, n, cch[i] - 1));
        CHECK(b[cch[i]] == 0xAAAA);
    }
    memset(b, 0xAA, sizeof b);
    MEQ(U(GetClassNameW)(w, b, len + 1), len);
    memset(b, 0xAA, sizeof b);
    MEQ(U(GetClassNameW)(w, b, len), len - 1);
    U(DestroyWindow)(w);
    U(UnregisterClassW)(n, GetModuleHandleW(NULL));
}
TEST(msg_timers_atoms_class_getclassname_zero_and_one_char_buffer) {
    MTA_NEED();
    WCHAR n[300], b[4]; uname(n, "cls_tiny");
    ATOM a = reg_class(n); REQUIRE(a);
    HWND w = make_of_class(n); REQUIRE(w);
    for (int cch = 0; cch <= 1; cch++) {
        b[0] = 0xAAAA;
        SET_ERR(0);
        MEQ(U(GetClassNameW)(w, b, cch), 0);
        EXPECT_ERR(ERROR_INSUFFICIENT_BUFFER);
        CHECK(b[0] == 0xAAAA);
    }
    U(DestroyWindow)(w);
    U(UnregisterClassW)(n, GetModuleHandleW(NULL));
}
TEST(msg_timers_atoms_class_getclassname_invalid_window) {
    MTA_NEED();
    WCHAR b[16];
    SET_ERR(0);
    MEQ(U(GetClassNameW)((HWND)(ULONG_PTR)0x12345678, b, 16), 0);
    EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
    HWND w = mta_win(); REQUIRE(w);
    mta_destroy(w);
    SET_ERR(0);
    MEQ(U(GetClassNameW)(w, b, 16), 0);
    EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
}
TEST(msg_timers_atoms_class_direct_getclassname_matches_user32) {
    MTA_NEED();
    NtUserGetClassName_pfn f = UN(NtUserGetClassName); REQUIRE(f);
    HWND w = mta_win(); REQUIRE(w);
    WCHAR b1[300], b2[300];
    UNICODE_STRING u; u.Buffer = b2; u.Length = 0; u.MaximumLength = sizeof b2;
    memset(b2, 0, sizeof b2);
    UINT r2 = f(w, 0, &u), r1 = U(GetClassNameW)(w, b1, 300);
    MEQ(r1, r2);
    CHECK(r1 != 0 && !wcscmp(b1, b2));
    mta_destroy(w);
}
TEST(msg_timers_atoms_class_getclassname_stub_number) {
    FARPROC p = t_proc_in("win32u.dll", "NtUserGetClassName");
    REQUIRE(p);
    MEQ(k_stub_number(p), 4217);
}
TEST(msg_timers_atoms_class_getclassinfo_by_atom) {
    MTA_NEED();
    WCHAR n[300]; uname(n, "cls_info");
    ATOM a = reg_class(n); REQUIRE(a);
    WNDCLASSEXW wc; memset(&wc, 0, sizeof wc); wc.cbSize = sizeof wc;
    ATOM r = (ATOM)U(GetClassInfoExW)(GetModuleHandleW(NULL), MIA(a), &wc);
    CHECK(r != 0);
    if (r != a) t_note("GetClassInfoExW returned 0x%x for class atom 0x%x", r, a);
    U(UnregisterClassW)(n, GetModuleHandleW(NULL));
}
TEST(msg_timers_atoms_class_create_window_by_atom) {
    MTA_NEED();
    WCHAR n[300], b[300]; uname(n, "cls_byatom");
    ATOM a = reg_class(n); REQUIRE(a);
    HWND w = make_of_class(MIA(a));
    CHECK(w != NULL);
    if (w) {
        MEQ(U(GetClassNameW)(w, b, 300), wcslen(n));
        CHECK(!wcscmp(b, n));
        U(DestroyWindow)(w);
    }
    U(UnregisterClassW)(n, GetModuleHandleW(NULL));
}
TEST(msg_timers_atoms_class_name_case_insensitive_lookup) {
    MTA_NEED();
    WCHAR n[300], up[300]; uname(n, "cls_case");
    ATOM a = reg_class(n); REQUIRE(a);
    wcscpy(up, n); for (WCHAR *p = up; *p; p++) if (*p >= L'a' && *p <= L'z') *p -= 32;
    HWND w = make_of_class(up);                  /* class lookup resolves the name through the case-insensitive atom table */
    CHECK(w != NULL);
    if (w) { MEQ((ATOM)U(GetClassLongW)(w, GCW_ATOM), a); U(DestroyWindow)(w); }
    U(UnregisterClassW)(n, GetModuleHandleW(NULL));
}
TEST(msg_timers_atoms_class_two_names_two_atoms) {
    MTA_NEED();
    WCHAR a[300], b[300]; uname(a, "cls_two_a"); uname(b, "cls_two_b");
    ATOM x = reg_class(a), y = reg_class(b);
    REQUIRE(x); REQUIRE(y);
    CHECK(x != y);
    U(UnregisterClassW)(a, GetModuleHandleW(NULL)); U(UnregisterClassW)(b, GetModuleHandleW(NULL));
}
TEST(msg_timers_atoms_class_unregister_atom_state_note) {
    MTA_NEED();
    WCHAR n[300], b[300]; uname(n, "cls_unreg");
    ATOM a = reg_class(n); REQUIRE(a);
    CHECK(U(UnregisterClassW)(n, GetModuleHandleW(NULL)));
    SET_ERR(0);
    UINT r = U(GetClipboardFormatNameW)(a, b, 300);
    t_info("after UnregisterClass the class atom 0x%x %s (ret %u err %lu)", a, r ? "is still named" : "is gone", r, GetLastError());
}
TEST(msg_timers_atoms_class_registered_message_keeps_atom_after_unregister) {
    MTA_NEED();
    WCHAR n[300], b[300]; uname(n, "cls_keep");
    ATOM a = reg_class(n); REQUIRE(a);
    UINT m = rwm(n);          /* an extra reference taken by RegisterWindowMessage */
    MEQ(m, a);
    CHECK(U(UnregisterClassW)(n, GetModuleHandleW(NULL)));
    memset(b, 0, sizeof b);
    UINT r = U(GetClipboardFormatNameW)(a, b, 300);
    if (r != wcslen(n)) t_note("atom 0x%x lost after unregistering a class that was also a message name", a);
    else CHECK(!wcscmp(b, n));
}

/* ---- built-in classes ----------------------------------------------------------------------------------- */
TEST(msg_timers_atoms_builtin_class_names_readable) {
    MTA_NEED();
    static const WCHAR *cls[] = { L"Button", L"Static", L"Edit", L"ListBox", L"ComboBox", L"ScrollBar" };
    for (unsigned i = 0; i < sizeof cls / sizeof cls[0]; i++) {
        HWND w = U(CreateWindowExW)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls[i], L"", WS_POPUP, -32000, -32000, 1, 1, NULL, NULL, GetModuleHandleW(NULL), NULL);
        if (!w) { t_note("cannot create %ls (err %lu)", cls[i], GetLastError()); continue; }
        WCHAR b[64], c[64];
        UINT r = U(GetClassNameW)(w, b, 64);
        CHECKF(r != 0 && wceq_i(b, cls[i]), "%ls -> \"%ls\"", cls[i], b);
        ATOM a = (ATOM)U(GetClassLongW)(w, GCW_ATOM);
        CHECKF(a >= 0xC000, "%ls atom 0x%x", cls[i], a);
        memset(c, 0, sizeof c);
        UINT q = U(GetClipboardFormatNameW)(a, c, 64);
        CHECKF(q != 0 && wceq_i(c, cls[i]), "%ls atom name \"%ls\"", cls[i], c);
        if (wcscmp(b, cls[i])) t_note("%ls reported as \"%ls\"", cls[i], b);
        U(DestroyWindow)(w);
    }
}
TEST(msg_timers_atoms_builtin_dialog_class_integer_atom) {
    MTA_NEED();
    HWND w = U(CreateWindowExW)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"#32770", L"", WS_POPUP, -32000, -32000, 1, 1, NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!w) { t_skip("dialog class window not created (err %lu)", GetLastError()); return; }
    WCHAR b[32];
    MEQ(U(GetClassNameW)(w, b, 32), 6);
    CHECK(!wcscmp(b, L"#32770"));         /* integer class atom 0x8002 is rendered as "#32770" */
    t_note("GCW_ATOM of #32770 is 0x%x", (unsigned)(ATOM)U(GetClassLongW)(w, GCW_ATOM));
    U(DestroyWindow)(w);
}
TEST(msg_timers_atoms_desktop_window_class_name) {
    MTA_NEED();
    HWND d = U(GetDesktopWindow)();
    WCHAR b[32];
    UINT r = U(GetClassNameW)(d, b, 32);
    CHECK(r == 6);
    CHECK(!wcscmp(b, L"#32769"));          /* DESKTOPCLASS = 0x8001 */
}
TEST(msg_timers_atoms_menu_class_atom_name) {
    MTA_NEED();
    WCHAR b[32]; NtUserGetAtomName_pfn g = UN(NtUserGetAtomName); REQUIRE(g);
    UNICODE_STRING u; u.Buffer = b; u.Length = 0; u.MaximumLength = sizeof b;
    MEQ(g(0x8000, &u), 6);           /* MENUCLASS */
    CHECK(!wcscmp(b, L"#32768"));
}

/* ---- window properties and atoms --------------------------------------------------------------------------- */
TEST(msg_timers_atoms_prop_string_uses_global_atom) {
    MTA_WIN(w);
    WCHAR n[300]; uname(n, "prop_str");
    HANDLE h = (HANDLE)(ULONG_PTR)0x1234;
    CHECK(U(SetPropW)(w, n, h));
    ATOM g = GlobalFindAtomW(n);
    CHECK(g != 0);                         /* SetPropW registers the name with GlobalAddAtomW */
    MEQ(U(GetPropW)(w, n), h);
    MEQ(U(GetPropW)(w, MIA(g)), h);
    MEQ(U(RemovePropW)(w, n), h);
    MEQ(U(GetPropW)(w, n), 0);
    if (g) GlobalDeleteAtom(g);            /* drop the reference SetPropW took */
    mta_destroy(w);
}
TEST(msg_timers_atoms_prop_int_atom_from_global_add) {
    MTA_WIN(w);
    WCHAR n[300]; uname(n, "prop_int");
    ATOM g = GlobalAddAtomW(n); REQUIRE(g);
    HANDLE h = (HANDLE)(ULONG_PTR)0x4321;
    CHECK(U(SetPropW)(w, MIA(g), h));
    MEQ(U(GetPropW)(w, n), h);                  /* name and atom are interchangeable keys */
    MEQ(U(GetPropW)(w, MIA(g)), h);
    MEQ(U(RemovePropW)(w, MIA(g)), h);
    MEQ(U(GetPropW)(w, n), 0);
    GlobalDeleteAtom(g);
    mta_destroy(w);
}
TEST(msg_timers_atoms_prop_user_atom_is_a_distinct_key) {
    MTA_WIN(w);
    WCHAR n[300]; uname(n, "prop_user");
    UINT u = rwm(n); REQUIRE(u);
    HANDLE h = (HANDLE)(ULONG_PTR)0x77;
    CHECK(U(SetPropW)(w, MIA(u), h));          /* the integer is stored verbatim as the key */
    MEQ(U(GetPropW)(w, MIA(u)), h);
    SET_ERR(0);
    MEQ(GlobalFindAtomW(n), 0);                  /* never entered in the global table */
    MEQ(U(GetPropW)(w, n), 0);                   /* GetPropW(name) goes through GlobalFindAtomW, which fails */
    MEQ(U(RemovePropW)(w, MIA(u)), h);
    mta_destroy(w);
}
TEST(msg_timers_atoms_prop_get_unknown_atom) {
    MTA_WIN(w);
    MEQ(U(GetPropW)(w, MIA(0xC123)), 0);
    MEQ(U(GetPropW)(w, MIA(1)), 0);
    MEQ(U(RemovePropW)(w, MIA(0xC124)), 0);
    WCHAR n[300]; uname(n, "prop_never");
    MEQ(U(GetPropW)(w, n), 0);                   /* name not in the global table: GetPropW returns early */
    mta_destroy(w);
}
TEST(msg_timers_atoms_prop_int_atom_only_low_16_bits_matter_for_keys) {
    MTA_WIN(w);
    HANDLE h = (HANDLE)(ULONG_PTR)0x55;
    CHECK(U(SetPropW)(w, MIA(0x0123), h));
    MEQ(U(GetPropW)(w, MIA(0x0123)), h);
    MEQ(U(GetPropW)(w, MIA(0x0124)), 0);
    MEQ(U(RemovePropW)(w, MIA(0x0123)), h);
    mta_destroy(w);
}
TEST(msg_timers_atoms_prop_failed_set_releases_new_global_atom) {
    MTA_NEED();
    WCHAR n[300]; uname(n, "prop_fail");
    SET_ERR(0);
    CHECK(!U(SetPropW)((HWND)(ULONG_PTR)0x12345678, n, (HANDLE)1));
    /* SetPropW added the name to the global table, the failing NtUserSetProp makes it GlobalDeleteAtom it again */
    SET_ERR(0);
    MEQ(GlobalFindAtomW(n), 0);
}
TEST(msg_timers_atoms_prop_two_names_two_keys) {
    MTA_WIN(w);
    WCHAR a[300], b[300]; uname(a, "prop_k1"); uname(b, "prop_k2");
    CHECK(U(SetPropW)(w, a, (HANDLE)1)); CHECK(U(SetPropW)(w, b, (HANDLE)2));
    MEQ(U(GetPropW)(w, a), 1); MEQ(U(GetPropW)(w, b), 2);
    CHECK(U(SetPropW)(w, a, (HANDLE)3));
    MEQ(U(GetPropW)(w, a), 3); MEQ(U(GetPropW)(w, b), 2);
    U(RemovePropW)(w, a); U(RemovePropW)(w, b);
    mta_destroy(w);
}
TEST(msg_timers_atoms_prop_global_atom_after_remove_note) {
    MTA_WIN(w);
    WCHAR n[300]; uname(n, "prop_gone");
    CHECK(U(SetPropW)(w, n, (HANDLE)1));
    U(RemovePropW)(w, n);
    SET_ERR(0);
    ATOM g = GlobalFindAtomW(n);
    t_note("global atom for a removed string property is %s", g ? "still present" : "released");
    if (g) GlobalDeleteAtom(g);
    mta_destroy(w);
}
