/* Process-level behaviour of classes: lookup matrix (name/atom x instance), visibility across threads and
 * processes, call-proc (WNDPROC translation) objects in the user handle table, many-class churn and name
 * spellings. win32kfull!GetClassPtr searches, in order: the private list with the instance, the public
 * (CS_GLOBALCLASS) list with any instance, then both lists again with user32's module. */
#include "cr.h"

/* ----- lookup matrix -------------------------------------------------------------------------- */

TEST(class_registration_lookup_matrix_private_class) {
    CR_NEED();
    const WCHAR *n = cr_name();
    ATOM a = cr_reg(n, 0, 0, 0);
    REQUIRE(a);
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    const struct { const char *what; HINSTANCE inst; int want; } inst[] = {
        { "exact", cr_hinst(), 1 },
        { "same 64K block", (HINSTANCE)((ULONG_PTR)cr_hinst() + 0x2000), 1 },
        { "next 64K block", (HINSTANCE)((ULONG_PTR)cr_hinst() + 0x10000), 0 },
        { "NULL", NULL, 0 },
        { "user32", u32, 0 },
        { "kernel32", GetModuleHandleW(L"kernel32.dll"), 0 },
    };
    for (unsigned i = 0; i < sizeof inst / sizeof *inst; i++) {
        for (int byatom = 0; byatom < 2; byatom++) {
            WNDCLASSEXW w; memset(&w, 0, sizeof w); w.cbSize = sizeof w;
            SET_ERR(0);
            BOOL ok = U(GetClassInfoExW)(inst[i].inst, byatom ? CR_ATOM(a) : n, &w);
            CHECKF(!!ok == inst[i].want, "%s by %s: ok=%d want %d", inst[i].what, byatom ? "atom" : "name", ok, inst[i].want);
            if (!inst[i].want) EXPECT_ERR(CR_ERR_DOES_NOT_EXIST);
        }
    }
    cr_unreg(n);
}

TEST(class_registration_lookup_matrix_global_class) {
    CR_NEED();
    const WCHAR *n = cr_name();
    ATOM a = cr_reg(n, CS_GLOBALCLASS, 0, 0);
    REQUIRE(a);
    /* the public list is searched with instance 0, i.e. any module: every spelling finds it */
    HINSTANCE insts[] = { cr_hinst(), NULL, GetModuleHandleW(L"user32.dll"), GetModuleHandleW(L"kernel32.dll"),
                          (HINSTANCE)((ULONG_PTR)cr_hinst() + 0x10000) };
    for (unsigned i = 0; i < sizeof insts / sizeof *insts; i++) {
        for (int byatom = 0; byatom < 2; byatom++) {
            WNDCLASSEXW w; memset(&w, 0, sizeof w); w.cbSize = sizeof w;
            SET_ERR(0);
            BOOL ok = U(GetClassInfoExW)(insts[i], byatom ? CR_ATOM(a) : n, &w);
            CHECKF(ok, "inst %p by %s failed, err %lu", insts[i], byatom ? "atom" : "name", GetLastError());
            if (ok) EXPECT_EQ(w.lpfnWndProc, cr_proc);
        }
    }
    cr_unreg(n);
}

TEST(class_registration_lookup_global_unregister_needs_no_instance_match) {
    CR_NEED();
    const WCHAR *n = cr_name();
    REQUIRE(cr_reg(n, CS_GLOBALCLASS, 0, 0));
    /* _UnregisterClass looks in the private list (instance-checked) and then the public list (no instance) */
    BOOL ok = U(UnregisterClassW)(n, (HINSTANCE)((ULONG_PTR)cr_hinst() + 0x30000));
    CHECK(ok);
    if (!ok) cr_unreg(n);
}

TEST(class_registration_lookup_private_shadows_nothing_of_global) {
    CR_NEED();
    /* a private class and a global class cannot share a name inside one process:
     * the global registration finds the private one first only when its instance matches */
    const WCHAR *n = cr_name();
    REQUIRE(cr_reg(n, 0, 0, 0));
    SET_ERR(0);
    ATOM g = cr_reg(n, CS_GLOBALCLASS, 0, 0);
    CHECK(g == 0);
    EXPECT_ERR(CR_ERR_ALREADY_EXISTS);
    if (g) cr_unreg(n);
    cr_unreg(n);
}

TEST(class_registration_lookup_global_blocks_private_same_name) {
    CR_NEED();
    const WCHAR *n = cr_name();
    REQUIRE(cr_reg(n, CS_GLOBALCLASS, 0, 0));
    /* private registration with the same name and instance: found in the public list through the
     * same-atom check only when CS_GLOBALCLASS is requested; a plain one is a separate private entry */
    SET_ERR(0);
    ATOM p = cr_reg(n, 0, 0, 0);
    t_info("private class over a same-named global: atom=0x%x err=%lu", p, GetLastError());
    if (p) cr_unreg(n);      /* removes the private one first (private list is searched first) */
    cr_unreg(n);
    WNDCLASSEXW w; memset(&w, 0, sizeof w); w.cbSize = sizeof w;
    if (U(GetClassInfoExW)(cr_hinst(), n, &w)) cr_unreg(n);
}

/* ----- visibility across threads and processes ------------------------------------------------ */

struct thr { const WCHAR *name; int made; int found; };
static DWORD thr_use(void *p) {
    struct thr *t = (struct thr *)p;
    WNDCLASSEXW w; memset(&w, 0, sizeof w); w.cbSize = sizeof w;
    t->found = U(GetClassInfoExW)(cr_hinst(), t->name, &w) ? 1 : 0;
    HWND h = cr_win(t->name);
    t->made = h != NULL;
    if (h) U(DestroyWindow)(h);
    return 0;
}

TEST(class_registration_visible_from_other_thread) {
    CR_NEED();
    struct thr t = { cr_name(), 0, 0 };
    REQUIRE(cr_reg(t.name, 0, 0, 0));
    k_run_gui_thread(thr_use, &t);
    CHECK(t.found);          /* classes belong to the process, not the registering thread */
    cr_unreg(t.name);
}

struct reg_thr { const WCHAR *name; ATOM atom; };
static DWORD thr_register(void *p) {
    struct reg_thr *r = (struct reg_thr *)p;
    r->atom = cr_reg(r->name, 0, 4, 0);
    return 0;
}

TEST(class_registration_survives_registering_thread_exit) {
    CR_NEED();
    struct reg_thr r = { cr_name(), 0 };
    k_run_gui_thread(thr_register, &r);
    REQUIRE(r.atom);
    WNDCLASSEXW w; memset(&w, 0, sizeof w); w.cbSize = sizeof w;
    CHECK(U(GetClassInfoExW)(cr_hinst(), r.name, &w));
    EXPECT_EQ(w.cbClsExtra, 4);
    CHECK(cr_unreg(r.name));
}

CHILD(cr_lookup) {
    int bad = 0;
    if (argc < 3) return 99;
    WCHAR priv[96], glob[96];
    MultiByteToWideChar(CP_ACP, 0, argv[1], -1, priv, 96);
    MultiByteToWideChar(CP_ACP, 0, argv[2], -1, glob, 96);
    if (!U(GetClassInfoExW)) return 98;
    WNDCLASSEXW w; memset(&w, 0, sizeof w); w.cbSize = sizeof w;
    SetLastError(0);
    BOOL ok = U(GetClassInfoExW)(GetModuleHandleW(NULL), priv, &w);
    CHECK(!ok); bad += ok;
    DWORD e = GetLastError(); CHECK(e == CR_ERR_DOES_NOT_EXIST); bad += e != CR_ERR_DOES_NOT_EXIST;
    SetLastError(0);
    ok = U(GetClassInfoExW)(GetModuleHandleW(NULL), glob, &w);
    CHECK(!ok); bad += ok;     /* "global" means visible to every module of the process only */
    e = GetLastError(); CHECK(e == CR_ERR_DOES_NOT_EXIST); bad += e != CR_ERR_DOES_NOT_EXIST;
    /* registering the same names here must work: no clash with the parent's classes */
    WNDCLASSEXW wc; cr_wc(&wc, priv, 0, 0, 0);
    ATOM a = U(RegisterClassExW)(&wc);
    CHECK(a != 0); bad += a == 0;
    if (a) U(UnregisterClassW)(priv, GetModuleHandleW(NULL));
    return bad;
}

TEST(class_registration_not_visible_in_other_process) {
    CR_NEED();
    /* a child launched while the parent owns registered classes never starts under the headless Wine prefix */
    if (t_is_wine()) { t_skip("child spawn with parent-registered classes stalls under headless Wine"); return; }
    const WCHAR *np = cr_name(), *ng = cr_name();
    REQUIRE(cr_reg(np, 0, 0, 0));
    REQUIRE(cr_reg(ng, CS_GLOBALCLASS, 0, 0));
    char a[300], pa[100], ga[100];
    WideCharToMultiByte(CP_ACP, 0, np, -1, pa, sizeof pa, NULL, NULL);
    WideCharToMultiByte(CP_ACP, 0, ng, -1, ga, sizeof ga, NULL, NULL);
    snprintf(a, sizeof a, "%s %s", pa, ga);
    struct k_child_result r = k_child_run("cr_lookup", a, 30000);
    CHECKF(r.exit_code == 0, "child exit 0x%lx", (unsigned long)r.exit_code);
    cr_unreg(np); cr_unreg(ng);
}

CHILD(cr_sysclasses) {
    int bad = 0;
    (void)argc; (void)argv;
    /* every process gets its own copies of the system classes on connection */
    static const WCHAR *const names[] = { L"Button", L"Edit", L"Static", L"ListBox", L"ComboBox", L"ScrollBar", L"#32770", L"#32768" };
    if (!U(GetClassInfoExW)) return 98;
    for (unsigned i = 0; i < sizeof names / sizeof *names; i++) {
        WNDCLASSEXW w; memset(&w, 0, sizeof w); w.cbSize = sizeof w;
        BOOL ok = U(GetClassInfoExW)(NULL, names[i], &w);
        CHECKF(ok, "%ls missing in a fresh process, err %lu", names[i], GetLastError());
        bad += !ok;
    }
    return bad;
}

TEST(class_registration_system_classes_in_fresh_process) {
    CR_NEED();
    struct k_child_result r = k_child_run("cr_sysclasses", NULL, 30000);
    CHECKF(r.exit_code == 0, "child exit 0x%lx", (unsigned long)r.exit_code);
}

/* ----- call-proc objects ------------------------------------------------------------------------- */

TEST(class_registration_callproc_handle_in_user_table) {
    CR_NEED();
    const WCHAR *n = cr_name();
    REQUIRE(cr_reg(n, 0, 0, 0));
    HWND h = cr_win(n);
    if (!h) { t_skip("no window"); cr_unreg(n); return; }
    ULONG_PTR w = U(GetWindowLongPtrW)(h, GWLP_WNDPROC);
    ULONG_PTR a = U(GetWindowLongPtrA)(h, GWLP_WNDPROC);
    EXPECT_EQ(w, (ULONG_PTR)cr_proc);
    CHECK(a != w && a != 0);
    EXPECT_EQ(U(GetWindowLongPtrA)(h, GWLP_WNDPROC), a);      /* the same object is handed out again */
    ULONG_PTR ca = U(GetClassLongPtrA)(h, GCLP_WNDPROC);
    CHECK(ca != 0 && ca != w);
    EXPECT_EQ(U(GetClassLongPtrA)(h, GCLP_WNDPROC), ca);
    struct k_user_entry e;
    if (k_user_entry((HANDLE)a, &e)) {
        EXPECT_EQ(e.type, 7);                    /* TYPE_CALLPROC */
        CHECK((e.flags & 1) == 0);
        t_info("call-proc handle 0x%p uniq 0x%x", (void *)a, e.uniq);
    } else t_note("handle 0x%p not decodable", (void *)a);
    U(DestroyWindow)(h);
    cr_unreg(n);
}

TEST(class_registration_callproc_distinct_per_proc) {
    CR_NEED();
    const WCHAR *n1 = cr_name(), *n2 = cr_name();
    WNDCLASSEXW wc; cr_wc(&wc, n1, 0, 0, 0);
    REQUIRE(U(RegisterClassExW)(&wc));
    cr_wc(&wc, n2, 0, 0, 0); wc.lpfnWndProc = cr_proc2;
    REQUIRE(U(RegisterClassExW)(&wc));
    WNDCLASSEXW a1, a2;
    if (cr_nt() && cr_nt_info(cr_hinst(), n1, &a1, TRUE) && cr_nt_info(cr_hinst(), n2, &a2, TRUE)) {
        CHECK(a1.lpfnWndProc != a2.lpfnWndProc);
        CHECK(a1.lpfnWndProc != cr_proc && a2.lpfnWndProc != cr_proc2);
    }
    cr_unreg(n1); cr_unreg(n2);
}

TEST(class_registration_callproc_ansi_caller_unicode_window) {
    CR_NEED();
    const WCHAR *n = cr_name();
    REQUIRE(cr_reg(n, 0, 0, 0));
    HWND h = cr_win(n);
    if (!h) { t_skip("no window"); cr_unreg(n); return; }
    WNDPROC a = (WNDPROC)U(GetWindowLongPtrA)(h, GWLP_WNDPROC);
    /* an ANSI caller through the translation object: the text arrives converted to Unicode */
    LONG before = cr_hits;
    LRESULT r = U(CallWindowProcA)(a, h, WM_SETTEXT, 0, (LPARAM)"win32kt-ansi");
    EXPECT_EQ(r, TRUE);
    CHECK(cr_hits > before);
    WCHAR b[64] = { 0 };
    U(GetWindowTextW)(h, b, 64);
    EXPECT_WSTR(b, L"win32kt-ansi");
    U(DestroyWindow)(h);
    cr_unreg(n);
}

TEST(class_registration_callproc_unicode_caller_ansi_window) {
    CR_NEED();
    const WCHAR *n = cr_name();
    char an[96]; WideCharToMultiByte(CP_ACP, 0, n, -1, an, sizeof an, NULL, NULL);
    WNDCLASSEXA wa; memset(&wa, 0, sizeof wa);
    wa.cbSize = sizeof wa; wa.lpfnWndProc = cr_procA; wa.hInstance = cr_hinst(); wa.lpszClassName = an;
    REQUIRE(U(RegisterClassExA)(&wa));
    HWND h = U(CreateWindowExA)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, an, "", WS_POPUP, -32000, -32000, 10, 10, NULL, NULL, cr_hinst(), NULL);
    if (!h) { t_skip("no window"); U(UnregisterClassA)(an, cr_hinst()); return; }
    WNDPROC w = (WNDPROC)U(GetWindowLongPtrW)(h, GWLP_WNDPROC);
    CHECK((ULONG_PTR)w != (ULONG_PTR)cr_procA);
    LONG before = cr_hits;
    LRESULT r = U(CallWindowProcW)(w, h, WM_SETTEXT, 0, (LPARAM)L"win32kt-wide");
    EXPECT_EQ(r, TRUE);
    CHECK(cr_hits > before);
    char b[64] = { 0 };
    U(GetWindowTextA)(h, b, 64);
    EXPECT_STR(b, "win32kt-wide");
    EXPECT_EQ(U(GetWindowLongPtrA)(h, GWLP_WNDPROC), (ULONG_PTR)cr_procA);
    U(DestroyWindow)(h);
    U(UnregisterClassA)(an, cr_hinst());
}

/* ----- churn and names ----------------------------------------------------------------------- */

TEST(class_registration_churn_many_classes) {
    CR_NEED();
    enum { N = 200 };
    static WCHAR names[N][48];
    static ATOM atoms[N];
    int made = 0;
    for (int i = 0; i < N; i++) {
        swprintf(names[i], 48, L"win32kt_cr_%lu_churn_%d", GetCurrentProcessId(), i);
        atoms[i] = cr_reg(names[i], 0, i % 17, i % 5);
        if (atoms[i]) made++;
    }
    CHECKF(made == N, "only %d of %d classes registered", made, N);
    for (int i = 0; i < N; i++) {
        if (!atoms[i]) continue;
        for (int j = i + 1; j < N; j++) if (atoms[j] && atoms[i] == atoms[j]) { CHECKF(0, "atoms %d and %d equal", i, j); }
        WNDCLASSEXW w; memset(&w, 0, sizeof w); w.cbSize = sizeof w;
        CHECK(U(GetClassInfoExW)(cr_hinst(), names[i], &w));
        if (w.cbClsExtra != i % 17) CHECKF(0, "class %d cbClsExtra %d", i, w.cbClsExtra);
    }
    for (int i = 0; i < N; i++) if (atoms[i]) CHECK(cr_unreg(names[i]));
    WNDCLASSEXW w; memset(&w, 0, sizeof w); w.cbSize = sizeof w;
    for (int i = 0; i < N; i += 37) CHECK(!U(GetClassInfoExW)(cr_hinst(), names[i], &w));
}

TEST(class_registration_churn_register_unregister_cycles) {
    CR_NEED();
    const WCHAR *n = cr_name();
    ATOM first = 0;
    for (int i = 0; i < 100; i++) {
        ATOM a = cr_reg(n, 0, 8, 0);
        if (!a) { CHECKF(0, "cycle %d failed err %lu", i, GetLastError()); break; }
        if (!i) first = a;
        if (!cr_unreg(n)) { CHECKF(0, "unregister cycle %d err %lu", i, GetLastError()); break; }
    }
    t_info("first atom 0x%x", first);
}

TEST(class_registration_name_spellings) {
    CR_NEED();
    static const WCHAR *const sp[] = { L"a", L"A b c", L"name-with-dash", L"dots.in.name", L"12345", L"été", L"中文",
        L"UPPER", L"x y\tz", L"quote'\"s", L"back\\slash", L"sl/ash", L"percent%20" };
    for (unsigned i = 0; i < sizeof sp / sizeof *sp; i++) {
        WCHAR nm[96];
        swprintf(nm, 96, L"%u_%lu_%ls", i, GetCurrentProcessId(), sp[i]);
        ATOM a = cr_reg(nm, 0, 0, 0);
        CHECKF(a != 0, "spelling %u refused, err %lu", i, GetLastError());
        if (!a) continue;
        HWND h = cr_win(nm);
        if (h) {
            WCHAR buf[96] = { 0 };
            EXPECT_EQ(U(GetClassNameW)(h, buf, 96), wcslen(nm));
            EXPECT_WSTR(buf, nm);
            U(DestroyWindow)(h);
        }
        CHECK(cr_unreg(nm));
    }
}

TEST(class_registration_name_pound_numeric_is_integer_atom) {
    CR_NEED_NT();
    /* RtlAddAtomToAtomTable folds "#nnnn" into the integer atom nnnn; a class named that way has an atom below
     * 0xC000. The desktop window's class "#32769" is the system example. */
    HWND d = GetDesktopWindow();
    ATOM a = (ATOM)U(GetClassLongW)(d, CR_GCW_ATOM);
    EXPECT_EQ(a, 32769);
    WCHAR buf[32] = { 0 }; UNICODE_STRING o = { 0, sizeof buf, buf };
    EXPECT_EQ(UN(NtUserGetAtomName)(a, &o), 6);
    EXPECT_WSTR(buf, L"#32769");
}

TEST(class_registration_system_class_atoms_fixed) {
    CR_NEED_NT();
    /* "#nnnn" names are the integer atoms nnnn: menu 32768, desktop 32769, dialog 32770, switch 32771,
     * icon title 32772 */
    static const struct { const WCHAR *n; int atom; } c[] = { { L"#32768", 32768 }, { L"#32769", 32769 }, { L"#32770", 32770 }, { L"#32771", 32771 }, { L"#32772", 32772 } };
    for (unsigned i = 0; i < sizeof c / sizeof *c; i++) {
        /* the user-mode caller turns a "#nnnn" name into the integer atom and passes it in the name pointer */
        WNDCLASSEXW w; memset(&w, 0, sizeof w); w.cbSize = sizeof w;
        CHECK(U(GetClassInfoExW)(NULL, c[i].n, &w));    /* user32 registers some of the system classes lazily, on the first lookup by name */
        memset(&w, 0, sizeof w); w.cbSize = sizeof w;
        UNICODE_STRING us = { 0, 0, (PWSTR)(ULONG_PTR)c[i].atom }; PVOID menu[4] = { 0 };
        ATOM a = UN(NtUserGetClassInfoEx)(NULL, &us, &w, menu, FALSE);
        CHECKF(a == c[i].atom, "%ls atom 0x%x want 0x%x", c[i].n, a, c[i].atom);
    }
}
