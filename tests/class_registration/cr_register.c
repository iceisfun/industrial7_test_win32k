/* NtUserRegisterClassExWOW called directly (win32u stub) and through user32!RegisterClassExW/A, then
 * validated through NtUserGetClassInfoEx / NtUserGetAtomName and the client-visible CLS image.
 * Analysis used: win32kfull!NtUserRegisterClassExWOW (argument capture and validation),
 * win32kfull!InternalRegisterClassEx (duplicate/atom/extra-size rules), win32kfull!ValidateAndLockCursor
 * (icon validation), user32!RegisterClassExWOWW / RegisterClassExWOWA (what the real callers push). */
#include "cr.h"

#define NEED_DIRECT() CR_NEED_NT()

/* ----- atoms and names --------------------------------------------------------------------- */

TEST(class_registration_direct_basic_returns_atom) {
    NEED_DIRECT();
    const WCHAR *n = cr_name();
    WNDCLASSEXW wc; cr_wc(&wc, n, CS_DBLCLKS, 0, 0);
    ATOM a = cr_nt_reg(&wc, n, 0x80);
    CHECKF(a != 0, "err %lu", GetLastError());
    if (!a) return;
    /* UserAddAtomEx(.., 2) adds to the user atom table: string atoms are 0xC000..0xFFFF */
    CHECKF(a >= 0xC000, "atom 0x%x", a);
    WNDCLASSEXW out; ATOM b = cr_nt_info(cr_hinst(), n, &out, FALSE);
    EXPECT_EQ(b, a);
    CHECK(cr_nt_unreg(n, cr_hinst()));
}

TEST(class_registration_direct_atoms_distinct_and_in_range) {
    NEED_DIRECT();
    const WCHAR *n[8]; ATOM a[8];
    for (int i = 0; i < 8; i++) {
        n[i] = cr_name();
        WNDCLASSEXW wc; cr_wc(&wc, n[i], 0, 0, 0);
        a[i] = cr_nt_reg(&wc, n[i], 0x80);
        CHECK(a[i] >= 0xC000);
    }
    for (int i = 0; i < 8; i++)
        for (int j = i + 1; j < 8; j++) CHECKF(a[i] != a[j], "%d/%d share atom 0x%x", i, j, a[i]);
    for (int i = 0; i < 8; i++) if (a[i]) cr_nt_unreg(n[i], cr_hinst());
}

TEST(class_registration_direct_atom_name_roundtrip) {
    NEED_DIRECT();
    const WCHAR *n = cr_name();
    WNDCLASSEXW wc; cr_wc(&wc, n, 0, 0, 0);
    ATOM a = cr_nt_reg(&wc, n, 0x80);
    REQUIRE(a);
    WCHAR buf[80] = { 0 };
    UNICODE_STRING o = { 0, sizeof buf, buf };
    /* UserGetAtomName -> RtlQueryAtomInAtomTable on the user atom table, returns characters copied */
    UINT cch = UN(NtUserGetAtomName)(a, &o);
    EXPECT_EQ(cch, wcslen(n));
    EXPECT_WSTR(buf, n);
    cr_nt_unreg(n, cr_hinst());
}

TEST(class_registration_direct_atom_survives_until_unregister) {
    NEED_DIRECT();
    const WCHAR *n = cr_name();
    WNDCLASSEXW wc; cr_wc(&wc, n, 0, 0, 0);
    ATOM a = cr_nt_reg(&wc, n, 0x80);
    REQUIRE(a);
    WCHAR buf[80] = { 0 }; UNICODE_STRING o = { 0, sizeof buf, buf };
    CHECK(UN(NtUserGetAtomName)(a, &o) != 0);
    CHECK(cr_nt_unreg(n, cr_hinst()));
    /* the class held the only reference (UserAddAtomEx on register, UserDeleteAtom on destroy) */
    memset(buf, 0, sizeof buf); o.Length = 0;
    SET_ERR(0);
    UINT cch = UN(NtUserGetAtomName)(a, &o);
    CHECKF(cch == 0, "atom 0x%x still resolvable after unregister: \"%ls\"", a, buf);
}

TEST(class_registration_direct_atom_name_caps) {
    NEED_DIRECT();
    const WCHAR *n = cr_name();
    WNDCLASSEXW wc; cr_wc(&wc, n, 0, 0, 0);
    ATOM a = cr_nt_reg(&wc, n, 0x80);
    REQUIRE(a);
    /* capacity 0: nothing can be written; no characters reported */
    WCHAR buf[4] = { 1, 1, 1, 1 };
    UNICODE_STRING o = { 0, 0, buf };
    SET_ERR(0);
    UINT cch = UN(NtUserGetAtomName)(a, &o);
    CHECK(cch == 0);
    CHECK(buf[0] == 1);              /* nothing written */
    cr_nt_unreg(n, cr_hinst());
}

TEST(class_registration_direct_name_max_length_255) {
    NEED_DIRECT();
    /* atom names are limited to 255 characters (RTL_ATOM_MAXIMUM_NAME_LENGTH); the class name goes
     * through UserAddAtomEx so 255 works and 256 does not. */
    static WCHAR nm[300];
    swprintf(nm, 48, L"win32kt_cr_%lu_long_", GetCurrentProcessId());
    size_t l = wcslen(nm);
    for (; l < 255; l++) nm[l] = L'x';
    nm[255] = 0;
    WNDCLASSEXW wc; cr_wc(&wc, nm, 0, 0, 0);
    ATOM a = cr_nt_reg(&wc, nm, 0x80);
    CHECKF(a != 0, "255 chars err %lu", GetLastError());
    if (a) cr_nt_unreg(nm, cr_hinst());
    nm[255] = L'x'; nm[256] = 0;
    cr_wc(&wc, nm, 0, 0, 0);
    SET_ERR(0);
    a = cr_nt_reg(&wc, nm, 0x80);
    CHECK(a == 0);
    if (a) cr_nt_unreg(nm, cr_hinst());
    t_note("256-char class name last error %lu", GetLastError());
}

TEST(class_registration_direct_empty_name_rejected) {
    NEED_DIRECT();
    WNDCLASSEXW wc; cr_wc(&wc, L"", 0, 0, 0);
    ATOM a = cr_nt_reg(&wc, L"", 0x80);
    CHECK(a == 0);
    if (a) cr_nt_unreg(L"", cr_hinst());
}

TEST(class_registration_direct_name_case_insensitive_duplicate) {
    NEED_DIRECT();
    WCHAR lo[64], up[64];
    swprintf(lo, 64, L"win32kt_cr_%lu_case", GetCurrentProcessId());
    wcscpy(up, lo);
    for (WCHAR *p = up; *p; p++) if (*p >= L'a' && *p <= L'z') *p -= 32;
    WNDCLASSEXW wc; cr_wc(&wc, lo, 0, 0, 0);
    ATOM a = cr_nt_reg(&wc, lo, 0x80);
    REQUIRE(a);
    /* UserFindAtom is case-insensitive (RtlLookupAtomInAtomTable) so the upper-case spelling is the same
     * atom and InternalRegisterClassEx finds it in the private list. */
    SET_ERR(0);
    cr_wc(&wc, up, 0, 0, 0);
    ATOM b = cr_nt_reg(&wc, up, 0x80);
    CHECK(b == 0);
    EXPECT_ERR(CR_ERR_ALREADY_EXISTS);
    if (b) cr_nt_unreg(up, cr_hinst());
    WNDCLASSEXW out;
    ATOM looked = cr_nt_info(cr_hinst(), up, &out, FALSE);   /* lookup is case-insensitive too */
    EXPECT_EQ(looked, a);
    cr_nt_unreg(lo, cr_hinst());
}

/* ----- flags and fnid ------------------------------------------------------------------------ */

TEST(class_registration_direct_invalid_flags_rejected) {
    NEED_DIRECT();
    /* NtUserRegisterClassExWOW: (flags & 0xffffff3d) != 0 -> UserSetLastError(0x3ec) before any state change */
    static const DWORD bad[] = { 0x01, 0x04, 0x08, 0x10, 0x20, 0x100, 0x200, 0x400, 0x1000, 0x10000, 0x80000000u, 0xffffffffu, 0x3d, 0xc3 };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        const WCHAR *n = cr_name();
        WNDCLASSEXW wc; cr_wc(&wc, n, 0, 0, 0);
        SET_ERR(0);
        ATOM a = cr_nt_reg(&wc, n, bad[i]);
        CHECKF(a == 0, "flags 0x%lx accepted", bad[i]);
        EXPECT_ERR(CR_ERR_INVALID_FLAGS);
        if (a) { cr_nt_unreg(n, cr_hinst()); continue; }
        WNDCLASSEXW out; SET_ERR(0);
        CHECKF(cr_nt_info(cr_hinst(), n, &out, FALSE) == 0, "class created despite flags 0x%lx", bad[i]);
        EXPECT_ERR(CR_ERR_DOES_NOT_EXIST);
    }
}

TEST(class_registration_direct_valid_flags_accepted) {
    NEED_DIRECT();
    static const DWORD good[] = { 0x00, 0x02, 0x40, 0x80, 0x42, 0x82, 0xc0, 0xc2 };
    for (unsigned i = 0; i < sizeof good / sizeof *good; i++) {
        const WCHAR *n = cr_name();
        WNDCLASSEXW wc; cr_wc(&wc, n, 0, 0, 0);
        ATOM a = cr_nt_reg(&wc, n, good[i]);
        CHECKF(a != 0, "flags 0x%lx rejected, err %lu", good[i], GetLastError());
        if (a) CHECK(cr_nt_unreg(n, cr_hinst()));
    }
}

TEST(class_registration_direct_system_fnid_rejected) {
    NEED_DIRECT();
    /* the loop over gDefaultServerClasses: a caller-supplied fnid equal to one of the kernel's own server
     * class fnids (scrollbar 0x29a, menu 0x29c, desktop 0x29d, message 0x29f, switch 0x2a0, 0x2b6) is
     * ERROR_INVALID_PARAMETER. Only these rejected values are ever sent: any accepted fnid would index
     * the kernel's per-fnid tables. */
    static const WORD fn[] = { 0x29a, 0x29c, 0x29d, 0x29f, 0x2a0, 0x2b6 };
    for (unsigned i = 0; i < sizeof fn / sizeof *fn; i++) {
        const WCHAR *n = cr_name();
        WNDCLASSEXW wc; cr_wc(&wc, n, 0, 0, 0);
        UNICODE_STRING us, v, m = { 0, 0, NULL };
        CR_MENU menu = { NULL, NULL, &m };
        cr_us(&us, n); v = us;
        SET_ERR(0);
        ATOM a = UN(NtUserRegisterClassExWOW)(&wc, &us, &v, &menu, fn[i], 0x80, NULL);
        CHECKF(a == 0, "fnid 0x%x accepted", fn[i]);
        EXPECT_ERR(CR_ERR_INVALID_PARAMETER);
        if (a) cr_nt_unreg(n, cr_hinst());
    }
}

TEST(class_registration_direct_wow_data_pointer_accepted) {
    NEED_DIRECT();
    /* pdwWowData is only read (8 bytes) when non-NULL */
    const WCHAR *n = cr_name();
    WNDCLASSEXW wc; cr_wc(&wc, n, 0, 0, 0);
    UNICODE_STRING us, v, m = { 0, 0, NULL };
    CR_MENU menu = { NULL, NULL, &m };
    DWORD wow[2] = { 0, 0 };
    cr_us(&us, n); v = us;
    ATOM a = UN(NtUserRegisterClassExWOW)(&wc, &us, &v, &menu, 0, 0x80, wow);
    CHECK(a != 0);
    if (a) cr_nt_unreg(n, cr_hinst());
}

/* ----- class extra sizes --------------------------------------------------------------------- */

TEST(class_registration_direct_negative_cbclsextra_rejected) {
    NEED_DIRECT();
    /* InternalRegisterClassEx: size = cbClsExtra + 0x58 computed in 32 bits; a negative value wraps below
     * 0x58 and the (0x58 <= size) test sends it to ERROR_INVALID_PARAMETER */
    static const int bad[] = { -1, -2, -8, -0x58, -0x1000, (int)0x80000000 };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        const WCHAR *n = cr_name();
        WNDCLASSEXW wc; cr_wc(&wc, n, 0, bad[i], 0);
        SET_ERR(0);
        ATOM a = cr_nt_reg(&wc, n, 0x80);
        CHECKF(a == 0, "cbClsExtra %d accepted", bad[i]);
        /* Windows 10 reports ERROR_INVALID_PARAMETER down to -0x58; the larger negative values are refused as well but
         * without setting any error */
        EXPECT_ERR(bad[i] >= -0x58 ? CR_ERR_INVALID_PARAMETER : 0);
        if (a) cr_nt_unreg(n, cr_hinst());
    }
}

TEST(class_registration_user32_negative_extra_rejected) {
    CR_NEED();
    /* user32!RegisterClassExWOWW: cbClsExtra < 0 || cbWndExtra < 0 -> RtlSetLastWin32Error(0x57) */
    static const int v[][2] = { { -1, 0 }, { 0, -1 }, { -100, -100 }, { (int)0x80000000, 0 }, { 0, (int)0x80000000 } };
    for (unsigned i = 0; i < sizeof v / sizeof *v; i++) {
        const WCHAR *n = cr_name();
        SET_ERR(0);
        ATOM a = cr_reg(n, 0, v[i][0], v[i][1]);
        CHECKF(a == 0, "extra %d/%d accepted", v[i][0], v[i][1]);
        EXPECT_ERR(CR_ERR_INVALID_PARAMETER);
        if (a) cr_unreg(n);
    }
}

TEST(class_registration_user32_extra_sizes_roundtrip) {
    CR_NEED();
    static const int sz[] = { 0, 1, 2, 3, 4, 7, 8, 9, 16, 40, 41, 64, 100, 255, 256, 1000, 2048 };
    for (unsigned i = 0; i < sizeof sz / sizeof *sz; i++) {
        const WCHAR *n = cr_name();
        ATOM a = cr_reg(n, 0, sz[i], sz[i] ? sz[i] + 3 : 0);
        if (!a) { t_note("cbClsExtra %d refused, err %lu", sz[i], GetLastError()); continue; }
        HWND h = cr_win(n);
        if (h) {
            /* user32!_GetClassData: GCL_CBCLSEXTRA returns cls->cbclsExtra, GCL_CBWNDEXTRA cb(wnd)+system part */
            EXPECT_EQ(U(GetClassLongW)(h, CR_GCL_CBCLSEXTRA), sz[i]);
            EXPECT_EQ(U(GetClassLongW)(h, CR_GCL_CBWNDEXTRA), sz[i] ? sz[i] + 3 : 0);
            U(DestroyWindow)(h);
        } else t_note("cannot create window for cbClsExtra %d", sz[i]);
        WNDCLASSEXW out; memset(&out, 0, sizeof out); out.cbSize = sizeof out;
        CHECK(U(GetClassInfoExW)(cr_hinst(), n, &out));
        EXPECT_EQ(out.cbClsExtra, sz[i]);
        EXPECT_EQ(out.cbWndExtra, sz[i] ? sz[i] + 3 : 0);
        CHECK(cr_unreg(n));
    }
}

TEST(class_registration_user32_class_extra_bytes_zeroed) {
    CR_NEED();
    /* WNDCLASS docs: the system initialises the class extra bytes to zero */
    const WCHAR *n = cr_name();
    ATOM a = cr_reg(n, 0, 64, 0);
    REQUIRE(a);
    HWND h = cr_win(n);
    if (!h) { t_skip("no window"); cr_unreg(n); return; }
    for (int off = 0; off + 8 <= 64; off += 8) {
        SET_ERR(0);
        EXPECT_EQ(U(GetClassLongPtrW)(h, off), 0);
    }
    U(DestroyWindow)(h);
    cr_unreg(n);
}

/* ----- hInstance rules ----------------------------------------------------------------------- */

TEST(class_registration_user32_null_hinstance_defaults_to_module) {
    CR_NEED();
    /* user32 substitutes GetModuleHandleW(NULL) for a NULL hInstance before the syscall */
    const WCHAR *n = cr_name();
    WNDCLASSEXW wc; cr_wc(&wc, n, 0, 0, 0);
    wc.hInstance = NULL;
    ATOM a = U(RegisterClassExW)(&wc);
    REQUIRE(a);
    CHECK(cr_unreg(n));        /* UnregisterClass with the module handle finds it */
    EXPECT_EQ(wc.hInstance, cr_hinst());  /* the caller's struct was updated in place */
}

TEST(class_registration_user32_user32_hinstance_rejected) {
    CR_NEED();
    HMODULE u = GetModuleHandleW(L"user32.dll");
    REQUIRE(u);
    if (!cr_modern()) { t_skip("image subsystem version 0x%lx is not modern", (unsigned long)cr_expwinver()); return; }
    /* user32!RegisterClassExWOWW and InternalRegisterClassEx: hInstance == user32's module (the system
     * module) with dwExpWinVer >= 0x400 is ERROR_INVALID_PARAMETER (only the system registers those) */
    const WCHAR *n = cr_name();
    WNDCLASSEXW wc; cr_wc(&wc, n, 0, 0, 0);
    wc.hInstance = u;
    SET_ERR(0);
    ATOM a = U(RegisterClassExW)(&wc);
    CHECK(a == 0);
    EXPECT_ERR(CR_ERR_INVALID_PARAMETER);
    if (a) U(UnregisterClassW)(n, u);
}

TEST(class_registration_direct_user32_hinstance_rejected) {
    NEED_DIRECT();
    HMODULE u = GetModuleHandleW(L"user32.dll");
    REQUIRE(u);
    if (!cr_modern()) { t_skip("not a modern image"); return; }
    const WCHAR *n = cr_name();
    WNDCLASSEXW wc; cr_wc(&wc, n, 0, 0, 0);
    wc.hInstance = u;
    SET_ERR(0);
    ATOM a = cr_nt_reg(&wc, n, 0x80 | 0x40);
    /* the kernel does not look at the module: on Windows 10 the refusal of user32's own hInstance is made by
     * user32!RegisterClassExW (class_registration_user32_hinstance_rejected), the syscall registers the class */
    CHECK(a != 0);
    EXPECT_ERR(0);
    if (a) cr_nt_unreg(n, u);
}

TEST(class_registration_direct_hinstance_compared_by_high_word) {
    NEED_DIRECT();
    /* _InnerGetClassPtr / _UnregisterClass compare only bits 16..31 of the module handle with the
     * class's: an instance in the same 64 KiB block unregisters it, another block does not. */
    const WCHAR *n = cr_name();
    WNDCLASSEXW wc; cr_wc(&wc, n, 0, 0, 0);
    ATOM a = cr_nt_reg(&wc, n, 0x80);
    REQUIRE(a);
    HINSTANCE other = (HINSTANCE)((ULONG_PTR)cr_hinst() + 0x20000);
    SET_ERR(0);
    CHECK(!cr_nt_unreg(n, other));
    EXPECT_ERR(CR_ERR_DOES_NOT_EXIST);
    HINSTANCE same = (HINSTANCE)((ULONG_PTR)cr_hinst() + 0x123);
    SET_ERR(0);
    BOOL ok = cr_nt_unreg(n, same);
    CHECK(ok);
    if (!ok) cr_nt_unreg(n, cr_hinst());
}

/* ----- names that are system classes --------------------------------------------------------- */

TEST(class_registration_direct_global_duplicate_of_system_class) {
    NEED_DIRECT();
    /* InternalRegisterClassEx: when the atom exists in the process's public list and the new class is
     * CS_GLOBALCLASS, registration fails with ERROR_CLASS_ALREADY_EXISTS (0x582). The system classes are
     * all in the public list of every process. */
    static const WCHAR *const sys[] = { L"Button", L"Edit", L"ListBox", L"ComboBox", L"Static", L"ScrollBar", L"ComboLBox", L"MDIClient" };
    if (!U(GetClassInfoExW)) { t_skip("no GetClassInfoExW"); return; }
    for (unsigned i = 0; i < sizeof sys / sizeof *sys; i++) {
        WNDCLASSEXW probe; memset(&probe, 0, sizeof probe); probe.cbSize = sizeof probe;
        if (!U(GetClassInfoExW)(NULL, sys[i], &probe)) { t_note("%ls not registered here", sys[i]); continue; }
        WNDCLASSEXW wc; cr_wc(&wc, sys[i], CS_GLOBALCLASS, 0, 0);
        SET_ERR(0);
        ATOM a = cr_nt_reg(&wc, sys[i], 0x80);
        CHECKF(a == 0, "%ls re-registered as global class", sys[i]);
        /* ScrollBar is a kernel-owned server class: it is refused without an error value */
        EXPECT_ERR(wcscmp(sys[i], L"ScrollBar") ? CR_ERR_ALREADY_EXISTS : 0);
        if (a) cr_nt_unreg(sys[i], cr_hinst());
    }
}

/* ----- menu names ---------------------------------------------------------------------------- */

TEST(class_registration_direct_menu_pointers_kept_by_class) {
    NEED_DIRECT();
 /* The kernel keeps the CLSMENUNAME client pointers verbatim (_RegisterClassEx stores argument 2[0..1] in
     * the class) and NtUserGetClassInfoEx hands back the unicode one (ansi==0) or the ansi one (ansi==1). */
    const WCHAR *n = cr_name();
    static WCHAR wm[] = L"win32kt_menu_w";
    static char am[] = "win32kt_menu_a";
    WNDCLASSEXW wc; cr_wc(&wc, n, 0, 0, 0);
    wc.lpszMenuName = wm;
    UNICODE_STRING us, v, mu;
    cr_us(&us, n); cr_us(&mu, wm); v = us;
    CR_MENU menu = { am, wm, &mu };
    ATOM a = UN(NtUserRegisterClassExWOW)(&wc, &us, &v, &menu, 0, 0x80, NULL);
    REQUIRE(a);
    WNDCLASSEXW o1; PVOID m1[4] = { 0 }; UNICODE_STRING q; cr_us(&q, n);
    memset(&o1, 0, sizeof o1); o1.cbSize = sizeof o1;
    ATOM r1 = UN(NtUserGetClassInfoEx)(cr_hinst(), &q, &o1, m1, FALSE);
    EXPECT_EQ(r1, a);
    EXPECT_EQ(m1[0], wm);
    PVOID m2[4] = { 0 };
    ATOM r2 = UN(NtUserGetClassInfoEx)(cr_hinst(), &q, &o1, m2, TRUE);
    EXPECT_EQ(r2, a);
    EXPECT_EQ(m2[0], am);
    CR_MENU out[2]; memset(out, 0, sizeof out);
    CHECK(UN(NtUserUnregisterClass)(&q, cr_hinst(), out) == 1);
    /* NtUserUnregisterClass returns the class's client menu name pointers so the caller can free them */
    EXPECT_EQ(out[0].ansi, am);
    EXPECT_EQ(out[0].unicode, wm);
}

TEST(class_registration_user32_menu_integer_resource) {
    CR_NEED();
    /* InitClsMenuNameW: a pointer below 0x10000 is passed through as the integer resource id for both
     * the ANSI and Unicode names */
    const WCHAR *n = cr_name();
    WNDCLASSEXW wc; cr_wc(&wc, n, 0, 0, 0);
    wc.lpszMenuName = MAKEINTRESOURCEW(123);
    ATOM a = U(RegisterClassExW)(&wc);
    REQUIRE(a);
    HWND h = cr_win(n);
    if (h) {
        /* Windows 10 does not hand the integer resource id back through GetClassLongPtr(GCLP_MENUNAME)
         * (it answers 0, in the Unicode and the ANSI form); GetClassInfoEx below does return it */
        EXPECT_EQ(U(GetClassLongPtrW)(h, CR_GCL_MENUNAME), 0);
        EXPECT_EQ(U(GetClassLongPtrA)(h, CR_GCL_MENUNAME), 0);
        U(DestroyWindow)(h);
    }
    WNDCLASSEXW out; memset(&out, 0, sizeof out); out.cbSize = sizeof out;
    CHECK(U(GetClassInfoExW)(cr_hinst(), n, &out));
    EXPECT_EQ(out.lpszMenuName, MAKEINTRESOURCEW(123));
    memset(&out, 0, sizeof out); out.cbSize = sizeof out;
    WNDCLASSEXA outA; memset(&outA, 0, sizeof outA); outA.cbSize = sizeof outA;
    char an[96]; WideCharToMultiByte(CP_ACP, 0, n, -1, an, sizeof an, NULL, NULL);
    CHECK(U(GetClassInfoExA)(cr_hinst(), an, &outA));
    EXPECT_EQ(outA.lpszMenuName, MAKEINTRESOURCEA(123));
    cr_unreg(n);
}

TEST(class_registration_user32_menu_string_copied) {
    CR_NEED();
    const WCHAR *n = cr_name();
    WCHAR menu[] = L"win32kt_MenuName";
    WNDCLASSEXW wc; cr_wc(&wc, n, 0, 0, 0);
    wc.lpszMenuName = menu;
    ATOM a = U(RegisterClassExW)(&wc);
    REQUIRE(a);
    /* user32 duplicates the string (StringDuplicateW) so the class does not alias the caller's buffer:
     * scribbling over the original does not change what the class reports */
    menu[0] = L'X';
    WNDCLASSEXW out; memset(&out, 0, sizeof out); out.cbSize = sizeof out;
    CHECK(U(GetClassInfoExW)(cr_hinst(), n, &out));
    CHECK(out.lpszMenuName != NULL && out.lpszMenuName != menu);
    if (out.lpszMenuName) EXPECT_WSTR(out.lpszMenuName, L"win32kt_MenuName");
    WNDCLASSEXA oa; memset(&oa, 0, sizeof oa); oa.cbSize = sizeof oa;
    char an[96]; WideCharToMultiByte(CP_ACP, 0, n, -1, an, sizeof an, NULL, NULL);
    CHECK(U(GetClassInfoExA)(cr_hinst(), an, &oa));
    if (oa.lpszMenuName) EXPECT_STR(oa.lpszMenuName, "win32kt_MenuName");
    else CHECK(oa.lpszMenuName != NULL);
    cr_unreg(n);
}

TEST(class_registration_user32_menu_null_reports_null) {
    CR_NEED();
    const WCHAR *n = cr_name();
    REQUIRE(cr_reg(n, 0, 0, 0));
    WNDCLASSEXW out; memset(&out, 0xcc, sizeof out); out.cbSize = sizeof out;
    CHECK(U(GetClassInfoExW)(cr_hinst(), n, &out));
    CHECK(out.lpszMenuName == NULL);
    cr_unreg(n);
}

/* ----- icons / cursors / brushes ------------------------------------------------------------- */

TEST(class_registration_user32_icon_cursor_roundtrip) {
    CR_NEED();
    if (!U(LoadIconW) || !U(LoadCursorW)) { t_skip("no LoadIcon/LoadCursor"); return; }
    HICON ic = U(LoadIconW)(NULL, MAKEINTRESOURCEW(32512));
    HICON sm = U(LoadIconW)(NULL, MAKEINTRESOURCEW(32514));
    HCURSOR cu = U(LoadCursorW)(NULL, MAKEINTRESOURCEW(32512));
    if (!ic || !sm || !cu) { t_skip("stock icons unavailable"); return; }
    const WCHAR *n = cr_name();
    WNDCLASSEXW wc; cr_wc(&wc, n, 0, 0, 0);
    wc.hIcon = ic; wc.hCursor = cu; wc.hIconSm = sm;
    REQUIRE(U(RegisterClassExW)(&wc));
    WNDCLASSEXW out; memset(&out, 0, sizeof out); out.cbSize = sizeof out;
    CHECK(U(GetClassInfoExW)(cr_hinst(), n, &out));
    /* _GetClassInfoEx returns the head handle of the locked tagCURSOR objects */
    EXPECT_EQ(out.hIcon, ic);
    EXPECT_EQ(out.hCursor, cu);
    EXPECT_EQ(out.hIconSm, sm);
    HWND h = cr_win(n);
    if (h) {
        EXPECT_EQ(U(GetClassLongPtrW)(h, GCLP_HICON), (ULONG_PTR)ic);
        EXPECT_EQ(U(GetClassLongPtrW)(h, GCLP_HCURSOR), (ULONG_PTR)cu);
        EXPECT_EQ(U(GetClassLongPtrW)(h, GCLP_HICONSM), (ULONG_PTR)sm);
        U(DestroyWindow)(h);
    }
    cr_unreg(n);
}

TEST(class_registration_user32_invalid_icon_handle_rejected) {
    CR_NEED();
    if (!cr_modern()) { t_skip("not a modern image"); return; }
    /* ValidateAndLockCursor(.., fModern): a non-NULL handle that is not a TYPE_CURSOR object fails the
     * registration with ERROR_INVALID_PARAMETER when flag 0x40 is set, which user32 does for every image
     * with a subsystem version above 3.10 */
    static const ULONG_PTR bad[] = { 0x1234, 0x7fff0001, 0xdeadbeef };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        for (int which = 0; which < 3; which++) {
            const WCHAR *n = cr_name();
            WNDCLASSEXW wc; cr_wc(&wc, n, 0, 0, 0);
            if (which == 0) wc.hIcon = (HICON)bad[i];
            if (which == 1) wc.hCursor = (HCURSOR)bad[i];
            if (which == 2) wc.hIconSm = (HICON)bad[i];
            SET_ERR(0);
            ATOM a = U(RegisterClassExW)(&wc);
            CHECKF(a == 0, "bogus %s 0x%llx accepted", which == 0 ? "hIcon" : which == 1 ? "hCursor" : "hIconSm", (unsigned long long)bad[i]);
            EXPECT_ERR(CR_ERR_INVALID_PARAMETER);
            if (a) cr_unreg(n);
        }
    }
}

TEST(class_registration_direct_invalid_icon_flag40_matrix) {
    NEED_DIRECT();
    /* same ValidateAndLockCursor rule at the syscall: flag 0x40 -> reject with 87; without it the bogus
     * handle is silently dropped and the class reports a NULL icon */
    const WCHAR *n1 = cr_name();
    WNDCLASSEXW wc; cr_wc(&wc, n1, 0, 0, 0);
    wc.hIcon = (HICON)(ULONG_PTR)0x4321;
    SET_ERR(0);
    CHECK(cr_nt_reg(&wc, n1, 0x80 | 0x40) == 0);
    EXPECT_ERR(CR_ERR_INVALID_PARAMETER);
    const WCHAR *n2 = cr_name();
    cr_wc(&wc, n2, 0, 0, 0);
    wc.hIcon = (HICON)(ULONG_PTR)0x4321;
    ATOM a = cr_nt_reg(&wc, n2, 0x80);
    CHECKF(a != 0, "legacy-flag registration with bogus icon failed, err %lu", GetLastError());
    if (a) {
        WNDCLASSEXW out; cr_nt_info(cr_hinst(), n2, &out, FALSE);
        CHECK(out.hIcon == NULL);
        cr_nt_unreg(n2, cr_hinst());
    }
}

TEST(class_registration_user32_wrong_object_type_as_icon_rejected) {
    CR_NEED();
    if (!cr_modern() || !U(GetStockObject)) { t_skip("n/a"); return; }
    /* a GDI brush handle is not a TYPE_CURSOR object: HMValidateHandleNoSecure(.., 3) fails */
    HGDIOBJ br = U(GetStockObject)(WHITE_BRUSH);
    const WCHAR *n = cr_name();
    WNDCLASSEXW wc; cr_wc(&wc, n, 0, 0, 0);
    wc.hIcon = (HICON)br;
    SET_ERR(0);
    ATOM a = U(RegisterClassExW)(&wc);
    CHECK(a == 0);
    EXPECT_ERR(CR_ERR_INVALID_PARAMETER);
    if (a) cr_unreg(n);
}

TEST(class_registration_user32_brush_background_forms) {
    CR_NEED();
    if (!U(GetStockObject)) { t_skip("no GetStockObject"); return; }
    /* user32 only validates hbrBackground values above 0x1f; COLOR_xxx+1 values are stored as is */
    static const ULONG_PTR cols[] = { 0, 1, COLOR_WINDOW + 1, COLOR_BTNFACE + 1, 0x1f };
    for (unsigned i = 0; i < sizeof cols / sizeof *cols; i++) {
        const WCHAR *n = cr_name();
        WNDCLASSEXW wc; cr_wc(&wc, n, 0, 0, 0);
        wc.hbrBackground = (HBRUSH)cols[i];
        ATOM a = U(RegisterClassExW)(&wc);
        CHECKF(a != 0, "color brush 0x%llx rejected", (unsigned long long)cols[i]);
        if (!a) continue;
        HWND h = cr_win(n);
        if (h) { EXPECT_EQ(U(GetClassLongPtrW)(h, CR_GCL_HBRBACKGROUND), cols[i]); U(DestroyWindow)(h); }
        WNDCLASSEXW out; memset(&out, 0, sizeof out); out.cbSize = sizeof out;
        CHECK(U(GetClassInfoExW)(cr_hinst(), n, &out));
        EXPECT_EQ(out.hbrBackground, (HBRUSH)cols[i]);
        cr_unreg(n);
    }
    HBRUSH st = (HBRUSH)U(GetStockObject)(LTGRAY_BRUSH);
    const WCHAR *n = cr_name();
    WNDCLASSEXW wc; cr_wc(&wc, n, 0, 0, 0);
    wc.hbrBackground = st;
    REQUIRE(U(RegisterClassExW)(&wc));
    WNDCLASSEXW out; memset(&out, 0, sizeof out); out.cbSize = sizeof out;
    CHECK(U(GetClassInfoExW)(cr_hinst(), n, &out));
    EXPECT_EQ(out.hbrBackground, st);
    cr_unreg(n);
}

TEST(class_registration_user32_invalid_brush_rejected) {
    CR_NEED();
    if (!cr_modern()) { t_skip("not a modern image"); return; }
    /* user32!RegisterClassExWOWW: hbrBackground > 0x1f that GdiValidateHandle rejects is an error for
     * images newer than 3.0 (RegisterClassExWOWA shows the explicit 0x300 cut-off) */
    const WCHAR *n = cr_name();
    WNDCLASSEXW wc; cr_wc(&wc, n, 0, 0, 0);
    wc.hbrBackground = (HBRUSH)(ULONG_PTR)0x77770;
    SET_ERR(0);
    ATOM a = U(RegisterClassExW)(&wc);
    CHECK(a == 0);
    if (a) cr_unreg(n);
    else EXPECT_ERR(CR_ERR_INVALID_PARAMETER);
}

/* ----- style bits ---------------------------------------------------------------------------- */

TEST(class_registration_user32_style_bits_roundtrip) {
    CR_NEED();
    static const UINT st[] = { 0, CS_VREDRAW, CS_HREDRAW, CS_DBLCLKS, CS_OWNDC, CS_CLASSDC, CS_PARENTDC, CS_NOCLOSE,
        CS_SAVEBITS, CS_BYTEALIGNCLIENT, CS_BYTEALIGNWINDOW, CS_GLOBALCLASS, 0x10000 /*CS_IME*/, 0x20000 /*CS_DROPSHADOW*/,
        CS_VREDRAW | CS_HREDRAW | CS_DBLCLKS | CS_OWNDC | CS_SAVEBITS, 0x37bff & ~0x400 };
    for (unsigned i = 0; i < sizeof st / sizeof *st; i++) {
        const WCHAR *n = cr_name();
        ATOM a = cr_reg(n, st[i], 0, 0);
        if (!a) { t_note("style 0x%x refused, err %lu", st[i], GetLastError()); continue; }
        WNDCLASSEXW out; memset(&out, 0, sizeof out); out.cbSize = sizeof out;
        CHECK(U(GetClassInfoExW)(cr_hinst(), n, &out));
        /* _GetClassInfoEx returns style & 0x37bff */
        EXPECT_EQ(out.style, st[i] & CR_STYLE_VISIBLE_MASK);
        HWND h = cr_win(n);
        if (h) {
            EXPECT_EQ(U(GetClassLongW)(h, CR_GCL_STYLE), st[i] & CR_STYLE_VISIBLE_MASK);
            U(DestroyWindow)(h);
        }
        CHECK(cr_unreg(n));
    }
}

TEST(class_registration_user32_invalid_style_bits_rejected) {
    CR_NEED();
    if (!cr_modern()) { t_skip("not a modern image"); return; }
    /* user32!RegisterClassExWOWW: style & 0xf7fc0114 (bits 2, 4, 8, 18..26, 28..31) is ERROR_INVALID_PARAMETER
     * for images above subsystem 3.10 */
    static const UINT bad[] = { 0x4, 0x10, 0x100, 0x40000, 0x80000, 0x100000, 0x400000, 0x4000000, 0x10000000, 0x40000000, 0x80000000u, 0xf7fc0114u };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        const WCHAR *n = cr_name();
        SET_ERR(0);
        ATOM a = cr_reg(n, bad[i], 0, 0);
        CHECKF(a == 0, "style 0x%x accepted", bad[i]);
        EXPECT_ERR(CR_ERR_INVALID_PARAMETER);
        if (a) cr_unreg(n);
    }
}

TEST(class_registration_direct_style_stored_unfiltered_reported_masked) {
    NEED_DIRECT();
    /* the syscall itself does not police style bits; the reported value is style & 0x37bff */
    static const UINT st[] = { 0x4, 0x10, 0x100, 0x400, 0x8000, 0x40000, 0x7fffffff };
    for (unsigned i = 0; i < sizeof st / sizeof *st; i++) {
        const WCHAR *n = cr_name();
        WNDCLASSEXW wc; cr_wc(&wc, n, st[i] & ~CS_GLOBALCLASS, 0, 0);
        ATOM a = cr_nt_reg(&wc, n, 0x80);
        if (!a) { t_note("style 0x%x refused by kernel, err %lu", st[i], GetLastError()); continue; }
        WNDCLASSEXW out; cr_nt_info(cr_hinst(), n, &out, FALSE);
        EXPECT_EQ(out.style, (st[i] & ~CS_GLOBALCLASS) & CR_STYLE_VISIBLE_MASK);
        cr_nt_unreg(n, cr_hinst());
    }
}

/* ----- duplicates ---------------------------------------------------------------------------- */

TEST(class_registration_user32_duplicate_registration) {
    CR_NEED();
    const WCHAR *n = cr_name();
    ATOM a = cr_reg(n, 0, 0, 0);
    REQUIRE(a);
    SET_ERR(0);
    ATOM b = cr_reg(n, CS_HREDRAW, 4, 4);          /* different parameters: still the same name */
    CHECK(b == 0);
    EXPECT_ERR(CR_ERR_ALREADY_EXISTS);
    if (b) cr_unreg(n);
    /* the first registration is unaffected */
    WNDCLASSEXW out; memset(&out, 0, sizeof out); out.cbSize = sizeof out;
    CHECK(U(GetClassInfoExW)(cr_hinst(), n, &out));
    EXPECT_EQ(out.style, 0);
    EXPECT_EQ(out.cbClsExtra, 0);
    CHECK(cr_unreg(n));
}

TEST(class_registration_user32_register_after_unregister) {
    CR_NEED();
    const WCHAR *n = cr_name();
    for (int i = 0; i < 5; i++) {
        ATOM a = cr_reg(n, 0, i, 0);
        CHECKF(a != 0, "round %d err %lu", i, GetLastError());
        if (!a) break;
        WNDCLASSEXW out; memset(&out, 0, sizeof out); out.cbSize = sizeof out;
        CHECK(U(GetClassInfoExW)(cr_hinst(), n, &out));
        EXPECT_EQ(out.cbClsExtra, i);
        CHECK(cr_unreg(n));
    }
}

TEST(class_registration_user32_ansi_duplicate_of_unicode) {
    CR_NEED();
    /* RegisterClassExA converts the name and then looks up the same atom: the A and W spellings clash */
    const WCHAR *n = cr_name();
    REQUIRE(cr_reg(n, 0, 0, 0));
    char an[96]; WideCharToMultiByte(CP_ACP, 0, n, -1, an, sizeof an, NULL, NULL);
    WNDCLASSEXA wa; memset(&wa, 0, sizeof wa);
    wa.cbSize = sizeof wa; wa.lpfnWndProc = cr_procA; wa.hInstance = cr_hinst(); wa.lpszClassName = an;
    SET_ERR(0);
    ATOM a = U(RegisterClassExA)(&wa);
    CHECK(a == 0);
    EXPECT_ERR(CR_ERR_ALREADY_EXISTS);
    if (a) cr_unreg(n);
    cr_unreg(n);
}

TEST(class_registration_user32_wrong_cbsize_rejected) {
    CR_NEED();
    static const UINT cb[] = { 0, 1, 0x30, 0x4f, 0x51, 0x60, 0xffffffffu };
    for (unsigned i = 0; i < sizeof cb / sizeof *cb; i++) {
        const WCHAR *n = cr_name();
        WNDCLASSEXW wc; cr_wc(&wc, n, 0, 0, 0);
        wc.cbSize = cb[i];
        SET_ERR(0);
        ATOM a = U(RegisterClassExW)(&wc);
        CHECKF(a == 0, "cbSize %u accepted", cb[i]);
        EXPECT_ERR(CR_ERR_INVALID_PARAMETER);       /* user32!RegisterClassExW: cbSize != 0x50 -> 0x57 */
        if (a) cr_unreg(n);
    }
}

/* ----- version (alternate) name -------------------------------------------------------------- */

TEST(class_registration_direct_version_name_is_the_lookup_key) {
    NEED_DIRECT();
    /* The third argument is the versioned class name: InternalRegisterClassEx keys the class on it
     * (cls[0]) while GetClassName/GetClassInfoEx report the plain name atom (cls[1]). */
    const WCHAR *plain = cr_name();
    const WCHAR *ver = cr_name();
    WNDCLASSEXW wc; cr_wc(&wc, plain, 0, 0, 0);
    UNICODE_STRING us, uv, m = { 0, 0, NULL };
    CR_MENU menu = { NULL, NULL, &m };
    cr_us(&us, plain); cr_us(&uv, ver);
    ATOM a = UN(NtUserRegisterClassExWOW)(&wc, &us, &uv, &menu, 0, 0x80, NULL);
    REQUIRE(a);
    WNDCLASSEXW out;
    ATOM b = cr_nt_info(cr_hinst(), ver, &out, FALSE);
    CHECK(b != 0);
    SET_ERR(0);
    ATOM c = cr_nt_info(cr_hinst(), plain, &out, FALSE);
    CHECK(c == 0);
    EXPECT_ERR(CR_ERR_DOES_NOT_EXIST);
    if (b) {
        WCHAR buf[80] = { 0 }; UNICODE_STRING o = { 0, sizeof buf, buf };
        CHECK(UN(NtUserGetAtomName)(b, &o) != 0);
        EXPECT_WSTR(buf, plain);              /* returned atom is the plain-name atom */
    }
    if (!cr_nt_unreg(ver, cr_hinst())) cr_nt_unreg(plain, cr_hinst());
}

TEST(class_registration_user32_unmanifested_has_no_version_redirect) {
    CR_NEED();
    /* ClassNameToVersion finds no activation-context redirection for an unmanifested image: the
     * versioned name equals the plain name, so a registered class is found by the same spelling. */
    const WCHAR *n = cr_name();
    REQUIRE(cr_reg(n, 0, 0, 0));
    WCHAR buf[80] = { 0 };
    HWND h = cr_win(n);
    REQUIRE(h);
    EXPECT_EQ(U(GetClassNameW)(h, buf, 80), wcslen(n));
    EXPECT_WSTR(buf, n);
    U(DestroyWindow)(h);
    cr_unreg(n);
}
