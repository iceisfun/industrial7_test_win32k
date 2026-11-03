/* NtUserGetClassInfoEx, NtUserUnregisterClass, NtUserGetClassName, NtUserGetAtomName, NtUserSetWindowFNID
 * (class side) - direct stubs and the user32 wrappers over them.
 * win32kfull!NtUserGetClassInfoEx / _GetClassInfoEx: lookup, hInstance and style rules, ERROR 0x583;
 * win32kfull!NtUserUnregisterClass / _UnregisterClass: 0x583 / 0x584, 64 KiB-granular module compare;
 * win32kfull!NtUserGetClassName: real-class-name mapping through the window's fnid. */
#include "cr.h"

UDECL(DWORD, NtUserSetWindowFNID, (HWND hwnd, WORD fnid));

/* ----- GetClassInfoEx ------------------------------------------------------------------------ */

TEST(class_registration_info_direct_fields) {
    CR_NEED_NT();
    const WCHAR *n = cr_name();
    WNDCLASSEXW wc; cr_wc(&wc, n, CS_HREDRAW | CS_DBLCLKS, 12, 20);
    ATOM a = U(RegisterClassExW)(&wc);
    REQUIRE(a);
    WNDCLASSEXW out; memset(&out, 0xAB, sizeof out);
    UNICODE_STRING us; PVOID menu[4] = { 0 };
    cr_us(&us, n);
    out.cbSize = 0x1234;                     /* the kernel never touches cbSize */
    ATOM r = UN(NtUserGetClassInfoEx)(cr_hinst(), &us, &out, menu, FALSE);
    EXPECT_EQ(r, a);
    EXPECT_EQ(out.cbSize, 0x1234);
    EXPECT_EQ(out.style, CS_HREDRAW | CS_DBLCLKS);
    EXPECT_EQ(out.lpfnWndProc, cr_proc);
    EXPECT_EQ(out.cbClsExtra, 12);
    EXPECT_EQ(out.cbWndExtra, 20);
    if (cr_modern()) EXPECT_EQ(out.hInstance, cr_hinst());   /* returns the hInstance asked with */
    /* _GetClassInfoEx zeroes lpszMenuName and lpszClassName first; user32 fills lpszClassName afterwards */
    EXPECT_EQ(out.lpszClassName, 0);
    EXPECT_EQ(out.lpszMenuName, 0);
    EXPECT_EQ(menu[0], 0);
    cr_unreg(n);
}

TEST(class_registration_info_unknown_class_fails_untouched) {
    CR_NEED_NT();
    const WCHAR *n = cr_name();
    WNDCLASSEXW out; memset(&out, 0x5a, sizeof out);
    UNICODE_STRING us; PVOID menu[4] = { 0 };
    cr_us(&us, n);
    SET_ERR(0);
    EXPECT_EQ(UN(NtUserGetClassInfoEx)(cr_hinst(), &us, &out, menu, FALSE), 0);
    EXPECT_ERR(CR_ERR_DOES_NOT_EXIST);
    unsigned char *b = (unsigned char *)&out; int dirty = 0;
    for (size_t i = 0; i < sizeof out; i++) dirty |= b[i] != 0x5a;
    CHECK(!dirty);               /* the output is copied back only on success */
}

TEST(class_registration_info_user32_unknown_class) {
    CR_NEED();
    WNDCLASSEXW out; memset(&out, 0, sizeof out); out.cbSize = sizeof out;
    SET_ERR(0);
    CHECK(!U(GetClassInfoExW)(cr_hinst(), cr_name(), &out));
    EXPECT_ERR(CR_ERR_DOES_NOT_EXIST);
    WNDCLASSA oa; memset(&oa, 0, sizeof oa);
    SET_ERR(0);
    CHECK(!U(GetClassInfoA)(cr_hinst(), "win32kt_nonexistent_class", &oa));
    EXPECT_ERR(CR_ERR_DOES_NOT_EXIST);
}

TEST(class_registration_info_user32_fills_class_name) {
    CR_NEED();
    const WCHAR *n = cr_name();
    REQUIRE(cr_reg(n, 0, 0, 0));
    WNDCLASSEXW out; memset(&out, 0, sizeof out); out.cbSize = sizeof out;
    CHECK(U(GetClassInfoExW)(cr_hinst(), n, &out));
    EXPECT_EQ(out.lpszClassName, n);         /* user32!GetClassInfoExW stores the caller's pointer back */
    cr_unreg(n);
}

TEST(class_registration_info_instance_null_misses_private_class) {
    CR_NEED_NT();
    /* hInstance NULL means user32's module (hModClient); a private class of the application's module is not
     * in that 64 KiB block, so the lookup fails with ERROR_CLASS_DOES_NOT_EXIST */
    const WCHAR *n = cr_name();
    REQUIRE(cr_reg(n, 0, 0, 0));
    WNDCLASSEXW out;
    SET_ERR(0);
    ATOM r = cr_nt_info(NULL, n, &out, FALSE);
    CHECK(r == 0);
    EXPECT_ERR(CR_ERR_DOES_NOT_EXIST);
    HMODULE u = GetModuleHandleW(L"user32.dll");
    SET_ERR(0);
    r = cr_nt_info(u, n, &out, FALSE);
    CHECK(r == 0);
    cr_unreg(n);
}

TEST(class_registration_info_instance_returned_is_instance_asked) {
    CR_NEED_NT();
    if (!cr_modern()) { t_skip("not a modern image"); return; }
    /* _GetClassInfoEx, dwExpWinVer >= 0x400: lpwcx->hInstance = the instance passed in (NULL if it was
     * user32), not the class's registered module */
    const WCHAR *n = cr_name();
    REQUIRE(cr_reg(n, 0, 0, 0));
    WNDCLASSEXW out;
    HINSTANCE inst = (HINSTANCE)((ULONG_PTR)cr_hinst() + 0x40);   /* same 64 KiB block */
    ATOM r = cr_nt_info(inst, n, &out, FALSE);
    CHECK(r != 0);
    EXPECT_EQ(out.hInstance, inst);
    cr_unreg(n);
}

TEST(class_registration_info_by_atom_form) {
    CR_NEED();
    const WCHAR *n = cr_name();
    ATOM a = cr_reg(n, CS_DBLCLKS, 0, 0);
    REQUIRE(a);
    WNDCLASSEXW out; memset(&out, 0, sizeof out); out.cbSize = sizeof out;
    /* integer-atom spelling of the name: user32 passes it as a UNICODE_STRING whose Buffer < 0x10000 */
    CHECK(U(GetClassInfoExW)(cr_hinst(), CR_ATOM(a), &out));
    EXPECT_EQ(out.style, CS_DBLCLKS);
    EXPECT_EQ(out.lpfnWndProc, cr_proc);
    EXPECT_EQ(out.lpszClassName, CR_ATOM(a));
    SET_ERR(0);
    CHECK(!U(GetClassInfoExW)(cr_hinst(), CR_ATOM(0xBFFF), &out));
    EXPECT_ERR(CR_ERR_DOES_NOT_EXIST);
    cr_unreg(n);
}

TEST(class_registration_info_direct_atom_as_name) {
    CR_NEED_NT();
    const WCHAR *n = cr_name();
    ATOM a = cr_reg(n, 0, 0, 0);
    REQUIRE(a);
    UNICODE_STRING us = { 0, 0, (PWSTR)(ULONG_PTR)a };
    WNDCLASSEXW out; memset(&out, 0, sizeof out); out.cbSize = sizeof out;
    PVOID menu[4] = { 0 };
    ATOM r = UN(NtUserGetClassInfoEx)(cr_hinst(), &us, &out, menu, FALSE);
    EXPECT_EQ(r, a);
    cr_unreg(n);
}

TEST(class_registration_info_name_256_chars_not_found) {
    CR_NEED_NT();
    /* _GetClassInfoEx copies the name with RtlStringCchCopyW into a 256 WCHAR scratch buffer: a name that
     * does not fit gets no atom and the lookup reports ERROR_CLASS_DOES_NOT_EXIST */
    static WCHAR nm[300];
    for (int i = 0; i < 256; i++) nm[i] = L'q';
    nm[256] = 0;
    WNDCLASSEXW out;
    SET_ERR(0);
    CHECK(cr_nt_info(cr_hinst(), nm, &out, FALSE) == 0);
    EXPECT_ERR(CR_ERR_DOES_NOT_EXIST);
}

TEST(class_registration_info_ansi_query_wndproc_translation) {
    CR_NEED_NT();
    /* _GetClassInfoEx: for a Unicode-registered class asked for with ansi==1 the returned procedure is a
     * call-proc-data handle (GetCPD), not the raw procedure; the Unicode query returns the raw one. */
    const WCHAR *n = cr_name();
    REQUIRE(cr_reg(n, 0, 0, 0));
    WNDCLASSEXW w, a1, a2;
    CHECK(cr_nt_info(cr_hinst(), n, &w, FALSE) != 0);
    CHECK(cr_nt_info(cr_hinst(), n, &a1, TRUE) != 0);
    CHECK(cr_nt_info(cr_hinst(), n, &a2, TRUE) != 0);
    EXPECT_EQ(w.lpfnWndProc, cr_proc);
    CHECK(a1.lpfnWndProc != cr_proc);
    EXPECT_EQ(a1.lpfnWndProc, a2.lpfnWndProc);        /* the CPD is cached per (class, direction) */
    struct k_user_entry e;
    if (k_user_entry((HANDLE)a1.lpfnWndProc, &e)) {
        EXPECT_EQ(e.type, 7);                    /* TYPE_CALLPROC: HMValidateHandleNoRip(proc, 7) in _RegisterClassEx */
        CHECK((e.flags & 1) == 0);
    } else t_note("call-proc handle 0x%p not decodable in the user handle table", a1.lpfnWndProc);
    cr_unreg(n);
}

TEST(class_registration_info_callproc_handle_roundtrips_through_register) {
    CR_NEED_NT();
    /* _RegisterClassEx: a wndproc whose low dword has 0xFFFF in the high word is a CPD handle; it is
     * validated as TYPE_CALLPROC and replaced by the procedure it wraps */
    const WCHAR *n1 = cr_name(), *n2 = cr_name();
    REQUIRE(cr_reg(n1, 0, 0, 0));
    WNDCLASSEXW a; CHECK(cr_nt_info(cr_hinst(), n1, &a, TRUE) != 0);
    if ((((ULONG_PTR)a.lpfnWndProc >> 16) & 0xffff) != 0xffff) {
        t_note("call-proc handle form 0x%p differs from 0xFFFFxxxx", a.lpfnWndProc);
        cr_unreg(n1); return;
    }
    WNDCLASSEXW wc; cr_wc(&wc, n2, 0, 0, 0);
    wc.lpfnWndProc = a.lpfnWndProc;
    ATOM r = cr_nt_reg(&wc, n2, 0x80);
    CHECKF(r != 0, "err %lu", GetLastError());
    if (r) {
        WNDCLASSEXW w2;
        CHECK(cr_nt_info(cr_hinst(), n2, &w2, FALSE) != 0);
        EXPECT_EQ(w2.lpfnWndProc, cr_proc);
        cr_nt_unreg(n2, cr_hinst());
    }
    cr_unreg(n1);
}

TEST(class_registration_info_ansi_class_roundtrip) {
    CR_NEED();
    /* RegisterClassExA with an ANSI procedure: GetClassInfoExA returns it untouched, GetClassInfoExW
     * returns a translation handle */
    const WCHAR *n = cr_name();
    char an[96]; WideCharToMultiByte(CP_ACP, 0, n, -1, an, sizeof an, NULL, NULL);
    WNDCLASSEXA wa; memset(&wa, 0, sizeof wa);
    wa.cbSize = sizeof wa; wa.lpfnWndProc = cr_procA; wa.hInstance = cr_hinst(); wa.lpszClassName = an;
    ATOM a = U(RegisterClassExA)(&wa);
    REQUIRE(a);
    WNDCLASSEXA oa; memset(&oa, 0, sizeof oa); oa.cbSize = sizeof oa;
    CHECK(U(GetClassInfoExA)(cr_hinst(), an, &oa));
    EXPECT_EQ(oa.lpfnWndProc, cr_procA);
    EXPECT_EQ(oa.lpszClassName, an);
    WNDCLASSEXW ow; memset(&ow, 0, sizeof ow); ow.cbSize = sizeof ow;
    CHECK(U(GetClassInfoExW)(cr_hinst(), n, &ow));
    CHECK((WNDPROC)ow.lpfnWndProc != (WNDPROC)cr_procA);
    CHECK(ow.lpfnWndProc != NULL);
    WCHAR buf[96] = { 0 }; UNICODE_STRING o = { 0, sizeof buf, buf };
    CHECK(UN(NtUserGetAtomName) ? UN(NtUserGetAtomName)(a, &o) == wcslen(n) : 1);
    if (UN(NtUserGetAtomName)) EXPECT_WSTR(buf, n);       /* the class name is stored as Unicode */
    U(UnregisterClassA)(an, cr_hinst());
}

TEST(class_registration_info_system_classes_present) {
    CR_NEED();
    /* user32 registers these on every process connection; GetClassInfoEx(NULL, name) finds them in the
     * process's public class list */
    static const WCHAR *const names[] = { L"Button", L"Edit", L"ListBox", L"ComboBox", L"Static", L"ScrollBar",
        L"ComboLBox", L"MDIClient", L"#32768", L"#32769", L"#32770", L"#32771", L"#32772", L"IME" };
    for (unsigned i = 0; i < sizeof names / sizeof *names; i++) {
        WNDCLASSEXW w; memset(&w, 0, sizeof w); w.cbSize = sizeof w;
        SET_ERR(0);
        BOOL ok = U(GetClassInfoExW)(NULL, names[i], &w);
        CHECKF(ok, "%ls missing, err %lu", names[i], GetLastError());
        if (!ok) continue;
        /* the task-switch window class (#32771) has no client-side window procedure on Windows 10 */
        if (!wcscmp(names[i], L"#32771")) EXPECT_EQ(w.lpfnWndProc, NULL);
        else CHECKF(w.lpfnWndProc != NULL, "%ls has no window procedure", names[i]);
        /* a system class never reports CS_GLOBALCLASS to a modern image when it has a fnid */
        if (cr_modern() && wcscmp(names[i], L"#32770") && wcscmp(names[i], L"IME") && wcscmp(names[i], L"#32772"))
            CHECKF((w.style & CS_GLOBALCLASS) == 0, "%ls style 0x%x", names[i], w.style);
        t_note("%ls: style=0x%x cbWnd=%d cbCls=%d hInst=%p proc=%p", names[i], w.style, w.cbWndExtra, w.cbClsExtra, w.hInstance, w.lpfnWndProc);
    }
}

TEST(class_registration_info_system_class_procs_in_user32) {
    CR_NEED();
    HMODULE u = GetModuleHandleW(L"user32.dll");
    IMAGE_DOS_HEADER d; IMAGE_NT_HEADERS64 nt;
    REQUIRE(u);
    if (!cr_rd(u, &d, sizeof d) || !cr_rd((BYTE *)u + d.e_lfanew, &nt, sizeof nt)) { t_skip("user32 header unreadable"); return; }
    static const WCHAR *const names[] = { L"Button", L"Edit", L"ListBox", L"ComboBox", L"Static", L"ScrollBar", L"#32770", L"MDIClient" };
    for (unsigned i = 0; i < sizeof names / sizeof *names; i++) {
        WNDCLASSEXW w; memset(&w, 0, sizeof w); w.cbSize = sizeof w;
        if (!U(GetClassInfoExW)(NULL, names[i], &w)) continue;
        ULONG_PTR p = (ULONG_PTR)w.lpfnWndProc, base = (ULONG_PTR)u;
        if (t_is_wine()) { t_note("%ls proc %p (wine)", names[i], w.lpfnWndProc); continue; }
        /* the control window procedures live in user32 or, on Windows 10, in the ntdll copies user32 forwards to */
        HMODULE owner = NULL;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)w.lpfnWndProc, &owner);
        int in_user32 = p >= base && p < base + nt.OptionalHeader.SizeOfImage;
        CHECKF(in_user32 || (owner && owner == GetModuleHandleW(L"ntdll.dll")), "%ls proc %p is in neither user32 nor ntdll", names[i], w.lpfnWndProc);
    }
}

TEST(class_registration_info_system_class_atoms_are_string_atoms) {
    CR_NEED();
    /* "#nnnn" names are integer atoms (RtlAddAtom folds them), so only plain names are string atoms */
    static const WCHAR *const names[] = { L"Button", L"Edit", L"ListBox", L"Static", L"ComboBox", L"ScrollBar" };
    for (unsigned i = 0; i < sizeof names / sizeof *names; i++) {
        WNDCLASSEXW w; memset(&w, 0, sizeof w); w.cbSize = sizeof w;
        ATOM a = (ATOM)U(GetClassInfoExW)(NULL, names[i], &w);   /* BOOL; atom via the direct stub below */
        if (!a) continue;
        if (!cr_nt()) continue;
        WNDCLASSEXW o2;
        ATOM at = cr_nt_info(NULL, names[i], &o2, FALSE);
        CHECKF(at >= 0xC000, "%ls atom 0x%x", names[i], at);
    }
}

/* ----- Unregister ---------------------------------------------------------------------------- */

TEST(class_registration_unreg_basic_and_gone) {
    CR_NEED();
    const WCHAR *n = cr_name();
    ATOM a = cr_reg(n, 0, 0, 0);
    REQUIRE(a);
    CHECK(cr_unreg(n));
    WNDCLASSEXW out; memset(&out, 0, sizeof out); out.cbSize = sizeof out;
    SET_ERR(0);
    CHECK(!U(GetClassInfoExW)(cr_hinst(), n, &out));
    EXPECT_ERR(CR_ERR_DOES_NOT_EXIST);
    SET_ERR(0);
    CHECK(!cr_unreg(n));                         /* second time: unknown class */
    EXPECT_ERR(CR_ERR_DOES_NOT_EXIST);
}

TEST(class_registration_unreg_unknown_class) {
    CR_NEED();
    SET_ERR(0);
    CHECK(!U(UnregisterClassW)(cr_name(), cr_hinst()));
    EXPECT_ERR(CR_ERR_DOES_NOT_EXIST);
    SET_ERR(0);
    CHECK(!U(UnregisterClassA)("win32kt_never_registered", cr_hinst()));
    EXPECT_ERR(CR_ERR_DOES_NOT_EXIST);
    SET_ERR(0);
    CHECK(!U(UnregisterClassW)(CR_ATOM(0x0001), cr_hinst()));
    EXPECT_ERR(CR_ERR_DOES_NOT_EXIST);
}

TEST(class_registration_unreg_direct_unknown) {
    CR_NEED_NT();
    SET_ERR(0);
    CHECK(!cr_nt_unreg(cr_name(), cr_hinst()));
    EXPECT_ERR(CR_ERR_DOES_NOT_EXIST);
}

TEST(class_registration_unreg_with_live_window) {
    CR_NEED();
    const WCHAR *n = cr_name();
    REQUIRE(cr_reg(n, 0, 0, 0));
    HWND h = cr_win(n);
    if (!h) { t_skip("no window"); cr_unreg(n); return; }
    /* _UnregisterClass: class->cWndReferenceCount != 0 -> ERROR_CLASS_HAS_WINDOWS */
    SET_ERR(0);
    CHECK(!cr_unreg(n));
    EXPECT_ERR(CR_ERR_HAS_WINDOWS);
    WNDCLASSEXW out; memset(&out, 0, sizeof out); out.cbSize = sizeof out;
    CHECK(U(GetClassInfoExW)(cr_hinst(), n, &out));       /* still registered */
    CHECK(U(DestroyWindow)(h));
    CHECK(cr_unreg(n));
}

TEST(class_registration_unreg_with_many_windows) {
    CR_NEED();
    const WCHAR *n = cr_name();
    REQUIRE(cr_reg(n, 0, 0, 0));
    HWND h[5]; int made = 0;
    for (int i = 0; i < 5; i++) { h[i] = cr_win(n); if (h[i]) made++; }
    if (!made) { t_skip("no window"); cr_unreg(n); return; }
    for (int i = 0; i < 5; i++) {
        if (!h[i]) continue;
        SET_ERR(0);
        CHECK(!cr_unreg(n));
        EXPECT_ERR(CR_ERR_HAS_WINDOWS);
        U(DestroyWindow)(h[i]);
    }
    CHECK(cr_unreg(n));
}

TEST(class_registration_unreg_direct_with_live_window) {
    CR_NEED_NT();
    const WCHAR *n = cr_name();
    WNDCLASSEXW wc; cr_wc(&wc, n, 0, 0, 0);
    REQUIRE(cr_nt_reg(&wc, n, 0x80));
    HWND h = cr_win(n);
    if (!h) { t_skip("no window"); cr_nt_unreg(n, cr_hinst()); return; }
    SET_ERR(0);
    CHECK(!cr_nt_unreg(n, cr_hinst()));
    EXPECT_ERR(CR_ERR_HAS_WINDOWS);
    U(DestroyWindow)(h);
    CHECK(cr_nt_unreg(n, cr_hinst()));
}

TEST(class_registration_unreg_system_class_with_window) {
    CR_NEED();
    /* a live Button window keeps the process's Button class referenced: unregistering it is refused and
     * the class survives */
    static const WCHAR *const names[] = { L"Button", L"Static", L"Edit", L"ListBox" };
    for (unsigned i = 0; i < sizeof names / sizeof *names; i++) {
        HWND h = U(CreateWindowExW)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, names[i], L"", WS_POPUP, -32000, -32000, 10, 10, NULL, NULL, cr_hinst(), NULL);
        if (!h) { t_note("cannot create %ls window", names[i]); continue; }
        SET_ERR(0);
        BOOL ok = U(UnregisterClassW)(names[i], NULL);
        DWORD e = GetLastError();
        CHECKF(!ok, "%ls unregistered under a live window", names[i]);
        if (!ok) CHECKF(e == CR_ERR_HAS_WINDOWS || e == CR_ERR_DOES_NOT_EXIST, "%ls err %lu", names[i], e);
        if (!ok && e == CR_ERR_DOES_NOT_EXIST) t_note("%ls not found by name+NULL instance", names[i]);
        WNDCLASSEXW w; memset(&w, 0, sizeof w); w.cbSize = sizeof w;
        CHECK(U(GetClassInfoExW)(NULL, names[i], &w));
        U(DestroyWindow)(h);
    }
}

TEST(class_registration_unreg_wrong_instance) {
    CR_NEED();
    const WCHAR *n = cr_name();
    REQUIRE(cr_reg(n, 0, 0, 0));
    SET_ERR(0);
    CHECK(!U(UnregisterClassW)(n, (HINSTANCE)((ULONG_PTR)cr_hinst() + 0x10000)));
    EXPECT_ERR(CR_ERR_DOES_NOT_EXIST);
    SET_ERR(0);
    CHECK(!U(UnregisterClassW)(n, GetModuleHandleW(L"kernel32.dll")));
    EXPECT_ERR(CR_ERR_DOES_NOT_EXIST);
    CHECK(cr_unreg(n));
}

TEST(class_registration_unreg_null_instance_matches_any_module) {
    CR_NEED_NT();
    /* _UnregisterClass skips the module comparison when hInstance == 0 */
    const WCHAR *n = cr_name();
    WNDCLASSEXW wc; cr_wc(&wc, n, 0, 0, 0);
    REQUIRE(cr_nt_reg(&wc, n, 0x80));
    BOOL ok = cr_nt_unreg(n, NULL);
    CHECK(ok);
    if (!ok) cr_nt_unreg(n, cr_hinst());
}

TEST(class_registration_unreg_by_atom) {
    CR_NEED();
    const WCHAR *n = cr_name();
    ATOM a = cr_reg(n, 0, 0, 0);
    REQUIRE(a);
    CHECK(U(UnregisterClassW)(CR_ATOM(a), cr_hinst()));
    WNDCLASSEXW out; memset(&out, 0, sizeof out); out.cbSize = sizeof out;
    CHECK(!U(GetClassInfoExW)(cr_hinst(), n, &out));
}

TEST(class_registration_unreg_ansi_and_case) {
    CR_NEED();
    const WCHAR *n = cr_name();
    REQUIRE(cr_reg(n, 0, 0, 0));
    char an[96]; WideCharToMultiByte(CP_ACP, 0, n, -1, an, sizeof an, NULL, NULL);
    for (char *p = an; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
    CHECK(U(UnregisterClassA)(an, cr_hinst()));
}

TEST(class_registration_unreg_global_class) {
    CR_NEED();
    const WCHAR *n = cr_name();
    REQUIRE(cr_reg(n, CS_GLOBALCLASS, 0, 0));
    WNDCLASSEXW out; memset(&out, 0, sizeof out); out.cbSize = sizeof out;
    CHECK(U(GetClassInfoExW)(cr_hinst(), n, &out));
    EXPECT_EQ(out.style & CS_GLOBALCLASS, CS_GLOBALCLASS);    /* kept for a class with no fnid */
    CHECK(cr_unreg(n));
    SET_ERR(0);
    CHECK(!U(GetClassInfoExW)(cr_hinst(), n, &out));
    EXPECT_ERR(CR_ERR_DOES_NOT_EXIST);
}

TEST(class_registration_unreg_returns_menu_names_to_caller) {
    CR_NEED_NT();
    /* with a string menu name registered through user32 the kernel hands the client allocations back so
     * user32!UnregisterClassW can free them; integer menus return the integers */
    const WCHAR *n = cr_name();
    WNDCLASSEXW wc; cr_wc(&wc, n, 0, 0, 0);
    wc.lpszMenuName = MAKEINTRESOURCEW(77);
    REQUIRE(U(RegisterClassExW)(&wc));
    UNICODE_STRING us; cr_us(&us, n);
    CR_MENU out[2]; memset(out, 0, sizeof out);
    CHECK(UN(NtUserUnregisterClass)(&us, cr_hinst(), out) == 1);
    EXPECT_EQ(out[0].unicode, 77);
    EXPECT_EQ(out[0].ansi, 77);
}

/* ----- GetClassName / real class name -------------------------------------------------------- */

TEST(class_registration_getname_userland_and_direct_agree) {
    CR_NEED_NT();
    const WCHAR *n = cr_name();
    REQUIRE(cr_reg(n, 0, 0, 0));
    HWND h = cr_win(n);
    if (!h) { t_skip("no window"); cr_unreg(n); return; }
    WCHAR b1[80] = { 0 }, b2[80] = { 0 };
    int l1 = U(GetClassNameW)(h, b1, 80);
    int l2 = cr_nt_getname(h, FALSE, b2, 80);
    EXPECT_EQ(l1, wcslen(n));
    EXPECT_EQ(l2, l1);
    EXPECT_WSTR(b1, n);
    EXPECT_WSTR(b2, n);
    U(DestroyWindow)(h);
    cr_unreg(n);
}

TEST(class_registration_getname_exact_fit_and_roomy) {
    CR_NEED_NT();
    const WCHAR *n = cr_name();
    REQUIRE(cr_reg(n, 0, 0, 0));
    HWND h = cr_win(n);
    if (!h) { t_skip("no window"); cr_unreg(n); return; }
    int len = (int)wcslen(n);
    WCHAR big[200];
    static const int caps[] = { 0, 0 };
    for (int cch = len + 1; cch <= 150; cch += 37) {
        for (int i = 0; i < 200; i++) big[i] = 0x7777;
        int r = cr_nt_getname(h, FALSE, big, cch);
        EXPECT_EQ(r, len);
        EXPECT_WSTR(big, n);
        EXPECT_EQ(big[len + 1], 0x7777);         /* nothing written past the terminator */
    }
    (void)caps;
    /* a buffer with no room for the terminator and a zero-size buffer: truncation behaviour is reported only */
    for (int i = 0; i < 200; i++) big[i] = 0x7777;
    int r = cr_nt_getname(h, FALSE, big, len);
    t_info("capacity=%d chars -> returned %d, big[len-1]=0x%x", len, r, big[len > 0 ? len - 1 : 0]);
    EXPECT_EQ(big[len + 1], 0x7777);
    r = cr_nt_getname(h, FALSE, big, 0);
    t_note("capacity=0 -> returned %d", r);
    U(DestroyWindow)(h);
    cr_unreg(n);
}

TEST(class_registration_getname_invalid_window) {
    CR_NEED_NT();
    WCHAR b[16] = { 1 };
    static const ULONG_PTR bad[] = { 0, 1, 0x1234, 0xffffffff, 0x10000 };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        SET_ERR(0);
        int r = cr_nt_getname((HWND)bad[i], FALSE, b, 16);
        CHECKF(r == 0, "hwnd 0x%llx returned %d", (unsigned long long)bad[i], r);
        EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);     /* ValidateHwnd failure */
    }
}

TEST(class_registration_getname_system_windows) {
    CR_NEED();
    HWND d = GetDesktopWindow();
    WCHAR b[64];
    memset(b, 0, sizeof b);
    EXPECT_EQ(U(GetClassNameW)(d, b, 64), 6);
    EXPECT_WSTR(b, L"#32769");
}

TEST(class_registration_getname_standard_controls) {
    CR_NEED();
    static const WCHAR *const names[] = { L"Button", L"Static", L"Edit", L"ListBox", L"ComboBox", L"ScrollBar" };
    for (unsigned i = 0; i < sizeof names / sizeof *names; i++) {
        HWND h = U(CreateWindowExW)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, names[i], L"", WS_POPUP, -32000, -32000, 10, 10, NULL, NULL, cr_hinst(), NULL);
        if (!h) { t_note("cannot create %ls", names[i]); continue; }
        WCHAR b[64] = { 0 }, r[64] = { 0 };
        int l1 = U(GetClassNameW)(h, b, 64);
        EXPECT_EQ(l1, wcslen(names[i]));
        CHECK(!lstrcmpiW(b, names[i]));           /* the atom table is case-insensitive: only compare folded */
        int l2 = U(RealGetWindowClassW)(h, r, 64);
        EXPECT_EQ(l2, wcslen(names[i]));
        CHECK(!lstrcmpiW(r, names[i]));
        U(DestroyWindow)(h);
    }
}

TEST(class_registration_getname_real_class_follows_fnid) {
    CR_NEED();
    /* NtUserGetClassName(.., real=1) maps a window whose fnid is a system control id (set by the control's
     * window procedure through NtUserSetWindowFNID) to the system class atom; real=0 reports the window's own
     * class. A class that reuses Static's procedure and extra bytes therefore reads as "Static". */
    WNDCLASSEXW st; memset(&st, 0, sizeof st); st.cbSize = sizeof st;
    if (!U(GetClassInfoExW)(NULL, L"Static", &st)) { t_skip("no Static class"); return; }
    const WCHAR *n = cr_name();
    WNDCLASSEXW wc; cr_wc(&wc, n, 0, 0, st.cbWndExtra);
    wc.lpfnWndProc = st.lpfnWndProc;
    REQUIRE(U(RegisterClassExW)(&wc));
    HWND h = cr_win(n);
    if (!h) { t_skip("no window"); cr_unreg(n); return; }
    WCHAR plain[64] = { 0 }, real[64] = { 0 };
    int l1 = U(GetClassNameW)(h, plain, 64);
    EXPECT_EQ(l1, wcslen(n));
    EXPECT_WSTR(plain, n);
    int l2 = U(RealGetWindowClassW)(h, real, 64);
    if (l2 == 6) { CHECK(!lstrcmpiW(real, L"Static")); }
    else { CHECKF(!t_is_wine(), "real class of a Static-derived window is \"%ls\"", real); t_note("real class \"%ls\" (wine)", real); }
    U(DestroyWindow)(h);
    cr_unreg(n);
}

TEST(class_registration_getname_real_class_plain_window_is_own_name) {
    CR_NEED_NT();
    const WCHAR *n = cr_name();
    REQUIRE(cr_reg(n, 0, 0, 0));
    HWND h = cr_win(n);
    if (!h) { t_skip("no window"); cr_unreg(n); return; }
    WCHAR b[64] = { 0 };
    EXPECT_EQ(cr_nt_getname(h, TRUE, b, 64), wcslen(n));
    EXPECT_WSTR(b, n);
    U(DestroyWindow)(h);
    cr_unreg(n);
}

/* ----- NtUserSetWindowFNID, class related rejections ---------------------------------------- */

TEST(class_registration_fnid_rejects_out_of_range_values) {
    CR_NEED();
    if (!UN(NtUserSetWindowFNID) || t_is_wine()) { t_skip("NtUserSetWindowFNID not implemented here"); return; }
    const WCHAR *n = cr_name();
    REQUIRE(cr_reg(n, 0, 0, 0));
    HWND h = cr_win(n);
    if (!h) { t_skip("no window"); cr_unreg(n); return; }
    /* only 0x2a1..0x2aa (and the 0x4000 flag, not sent here) are accepted: everything else is
     * ERROR_INVALID_PARAMETER and leaves the window alone */
    static const WORD bad[] = { 0, 1, 0x29a, 0x2a0, 0x2ab, 0x1234, 0x8000, 0xffff };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        SET_ERR(0);
        DWORD r = UN(NtUserSetWindowFNID)(h, bad[i]);
        CHECKF(r == 0, "fnid 0x%x accepted", bad[i]);
        EXPECT_ERR(CR_ERR_INVALID_PARAMETER);
    }
    U(DestroyWindow)(h);
    cr_unreg(n);
}

TEST(class_registration_fnid_foreign_window_denied) {
    CR_NEED();
    if (!UN(NtUserSetWindowFNID) || t_is_wine()) { t_skip("NtUserSetWindowFNID not implemented here"); return; }
    HWND d = GetDesktopWindow();
    /* the desktop window belongs to another process: the owner check comes first -> ERROR_ACCESS_DENIED */
    SET_ERR(0);
    CHECK(UN(NtUserSetWindowFNID)(d, 0x2a8) == 0);
    EXPECT_ERR(CR_ERR_ACCESS_DENIED);
}

TEST(class_registration_fnid_already_set_by_control_is_final) {
    CR_NEED();
    if (!UN(NtUserSetWindowFNID) || t_is_wine()) { t_skip("NtUserSetWindowFNID not implemented here"); return; }
    HWND h = U(CreateWindowExW)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"Static", L"", WS_POPUP, -32000, -32000, 10, 10, NULL, NULL, cr_hinst(), NULL);
    if (!h) { t_skip("no Static window"); return; }
    /* StaticWndProc set FNID_STATIC at WM_NCCREATE; the second assignment is refused (fnid & 0x2fff != 0) */
    SET_ERR(0);
    DWORD r = UN(NtUserSetWindowFNID)(h, 0x2a8);
    CHECK(r == 0);
    EXPECT_ERR(CR_ERR_INVALID_PARAMETER);
    U(DestroyWindow)(h);
}
