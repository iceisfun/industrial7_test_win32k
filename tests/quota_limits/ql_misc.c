/* Capacity and ownership rules around the per-process counters: argument validation of the creation
 * calls, objects that do or do not consume handles, and who owns (and therefore frees) what. */
#include "ql.h"

#define ATM(i) ((LPCWSTR)(ULONG_PTR)(i))
static DWORD gur1(void) { return ql_gur(GR_USER_CUR); }
static DWORD gur0(void) { return ql_gur(GR_GDI_CUR); }

/* ---- accelerator table argument validation (NtUserCreateAcceleratorTable) ---- */
UDECL(HANDLE, NtUserCreateAcceleratorTable, (void *, int));

TEST(quota_limits_accel_count_validation_table) {
    REQUIRE(U(CreateAcceleratorTableW));
    QL_NEED();
    /* count - 1 must be below 0x7fff, i.e. 1..0x7fff; anything else is rejected with 0x57 before the buffer is read */
    static const int bad[] = { 0, -1, -2, 0x8000, 0x8001, 0x10000, 0x7fffffff, (int)0x80000000, (int)0xfffffff0 };
    ACCEL a QL_ACCEL_ALIGN; memset(&a, 0, sizeof a); a.fVirt = FVIRTKEY; a.key = 'Q'; a.cmd = 1;
    DWORD base = gur1();
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(0);
        HACCEL h = U(CreateAcceleratorTableW)(&a, bad[i]);
        CHECKF(h == NULL, "count %d created a table", bad[i]);
        if (h) U(DestroyAcceleratorTable)(h);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
        CHECK(gur1() == base);
    }
}

TEST(quota_limits_accel_count_validation_nt_stub) {
    NtUserCreateAcceleratorTable_pfn f = UN(NtUserCreateAcceleratorTable);
    REQUIRE(f);
    QL_NEED();
    ACCEL a QL_ACCEL_ALIGN; memset(&a, 0, sizeof a); a.fVirt = FVIRTKEY; a.key = 'Q'; a.cmd = 1;
    static const int bad[] = { 0, -1, 0x8000, 0x7fffffff };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(0);
        HANDLE h = f(&a, bad[i]);
        CHECKF(h == NULL, "count %d created a table", bad[i]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
    DWORD base = gur1();
    HANDLE h = f(&a, 1);
    CHECK(h != NULL);
    CHECK(gur1() == base + 1);
    if (h) CHECK(U(DestroyAcceleratorTable)((HACCEL)h));
    CHECK(gur1() == base);
}

TEST(quota_limits_accel_max_entry_count) {
    REQUIRE(U(CreateAcceleratorTableW));
    QL_NEED();
    CHECK(sizeof(ACCEL) == 6);                       /* the kernel copies count * 6 bytes */
    enum { N = 0x7fff };
    ACCEL *a = calloc(N, sizeof *a);
    if (!a) { t_skip("no memory"); return; }
    for (int i = 0; i < N; i++) { a[i].fVirt = FVIRTKEY; a[i].key = 0x41 + (i % 26); a[i].cmd = (WORD)i; }
    DWORD base = gur1();
    HACCEL h = U(CreateAcceleratorTableW)(a, N);
    CHECK(h != NULL);                                // 0x7fff entries is the largest accepted count
    if (h) {
        CHECK(gur1() == base + 1);                  // one handle however many entries
        CHECK(U(CopyAcceleratorTableW)(h, NULL, 0) == N);
        ACCEL *out = calloc(N, sizeof *out);
        if (out) {
            CHECK(U(CopyAcceleratorTableW)(h, out, N) == N);
            CHECK(out[0].cmd == 0 && out[N - 1].cmd == N - 1);
            free(out);
        }
        CHECK(U(DestroyAcceleratorTable)(h));
    }
    CHECK(gur1() == base);
    free(a);
}

TEST(quota_limits_accel_destroy_twice) {
    REQUIRE(U(CreateAcceleratorTableW));
    QL_NEED();
    ACCEL a QL_ACCEL_ALIGN; memset(&a, 0, sizeof a); a.fVirt = FVIRTKEY; a.key = 'Q'; a.cmd = 1;
    DWORD base = gur1();
    HACCEL h = U(CreateAcceleratorTableW)(&a, 1);
    CHECK(h != NULL);
    CHECK(U(DestroyAcceleratorTable)(h));
    CHECK(gur1() == base);
    SetLastError(0);
    CHECK(!U(DestroyAcceleratorTable)(h));
    CHECK(gur1() == base);
}

/* ---- classes and window class registrations are not handle table objects ---- */
static void class_capacity(void) {
    if (!ql_connect()) { t_skip("win32k not reachable"); return; }
    enum { N = 1000 };
    static WCHAR names[N][40];
    static ATOM at[N];
    DWORD u0 = gur1(), g0 = gur0();
    int made = 0;
    for (int i = 0; i < N; i++) {
        WNDCLASSEXW wc; memset(&wc, 0, sizeof wc);
        _snwprintf(names[i], 40, L"win32kt_qlc_%lu_%d", GetCurrentProcessId(), i);
        wc.cbSize = sizeof wc; wc.lpfnWndProc = (WNDPROC)U(DefWindowProcW);
        wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = names[i];
        at[i] = U(RegisterClassExW)(&wc);
        if (!at[i]) { t_note("RegisterClassEx failed at %d (error %lu)", i, GetLastError()); break; }
        made++;
    }
    QC(made == N || t_is_wine());
    static unsigned char seen[0x10000];
    int dup = 0, range = 0;
    for (int i = 0; i < made; i++) {
        if (at[i] < 0xc000) range++;            /* string atoms live in 0xC000..0xFFFF */
        if (seen[at[i]]) dup++;
        seen[at[i]] = 1;
    }
    QCF(range == 0, "%d class atoms below 0xC000", range);
    QCF(dup == 0, "%d duplicate class atoms", dup);
    QCF(gur1() == u0, "class registrations changed the USER count %lu -> %lu", u0, gur1());
    QCF(gur0() == g0, "class registrations changed the GDI count %lu -> %lu", g0, gur0());
    for (int i = 0; i < made; i += 97) {
        WNDCLASSEXW out; memset(&out, 0, sizeof out); out.cbSize = sizeof out;
        QC(U(GetClassInfoExW)(GetModuleHandleW(NULL), names[i], &out) != 0);
        QC(out.lpfnWndProc != NULL);
    }
    /* a class can be registered once per name */
    if (made) {
        WNDCLASSEXW wc; memset(&wc, 0, sizeof wc);
        wc.cbSize = sizeof wc; wc.lpfnWndProc = (WNDPROC)U(DefWindowProcW); wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = names[0];
        SetLastError(0);
        QC(U(RegisterClassExW)(&wc) == 0); EXPECT_ERR(ERROR_CLASS_ALREADY_EXISTS);
    }
    for (int i = 0; i < made; i++) QC(U(UnregisterClassW)(names[i], GetModuleHandleW(NULL)));
    if (made) {
        SetLastError(0);
        QC(!U(UnregisterClassW)(names[0], GetModuleHandleW(NULL))); EXPECT_ERR(ERROR_CLASS_DOES_NOT_EXIST);
    }
    QC(gur1() == u0);
}
QL_SCENARIO(quota_limits_class_capacity_registrations, class_capacity())

/* ---- menu items and sub menus ---- */
TEST(quota_limits_menu_items_use_no_handles) {
    REQUIRE(U(CreateMenu));
    QL_NEED();
    DWORD u0 = gur1(), g0 = gur0();
    HMENU m = U(CreateMenu)();
    if (!m) { t_skip("no menu"); return; }
    /* well below the size at which a menu's item array stops growing (about 1170 items were accepted on Windows 10) */
    enum { N = 1000 };
    int ok = 0;
    for (int i = 0; i < N; i++) ok += U(AppendMenuW)(m, MF_STRING, 1000 + i, L"item") ? 1 : 0;
    CHECK(ok == N);
    CHECK(U(GetMenuItemCount)(m) == N);
    CHECK(U(GetMenuItemID)(m, 0) == 1000 && U(GetMenuItemID)(m, N - 1) == 1000 + N - 1);
    CHECKF(gur1() == u0 + 1, "USER count %lu base %lu", gur1(), u0);
    CHECK(gur0() == g0);
    CHECK(U(DestroyMenu)(m));
    CHECK(gur1() == u0);
}

TEST(quota_limits_menu_popups_are_handles_freed_with_parent) {
    REQUIRE(U(CreateMenu));
    QL_NEED();
    DWORD u0 = gur1();
    HMENU m = U(CreateMenu)();
    if (!m) { t_skip("no menu"); return; }
    enum { N = 200 };
    HMENU sub[N];
    for (int i = 0; i < N; i++) {
        sub[i] = U(CreatePopupMenu)();
        CHECK(sub[i] != NULL);
        CHECK(U(AppendMenuW)(m, MF_POPUP, (UINT_PTR)sub[i], L"p"));
    }
    CHECKF(gur1() == u0 + 1 + N, "USER count %lu with 1 menu and %d popups (base %lu)", gur1(), N, u0);
    /* DestroyMenu frees the whole tree */
    CHECK(U(DestroyMenu)(m));
    CHECKF(gur1() == u0, "USER count %lu after destroying the tree, base %lu", gur1(), u0);
    for (int i = 0; i < N; i += 37) CHECK(!U(IsMenu)(sub[i]));
}

TEST(quota_limits_menu_remove_vs_delete_submenu_handles) {
    REQUIRE(U(CreateMenu));
    QL_NEED();
    DWORD u0 = gur1();
    HMENU m = U(CreateMenu)(), s1 = U(CreatePopupMenu)(), s2 = U(CreatePopupMenu)();
    if (!m || !s1 || !s2) { t_skip("no menu"); return; }
    CHECK(U(AppendMenuW)(m, MF_POPUP, (UINT_PTR)s1, L"a"));
    CHECK(U(AppendMenuW)(m, MF_POPUP, (UINT_PTR)s2, L"b"));
    CHECK(gur1() == u0 + 3);
    /* RemoveMenu detaches the submenu but keeps it alive; DeleteMenu destroys it */
    CHECK(U(RemoveMenu)(m, 0, MF_BYPOSITION));
    CHECK(U(IsMenu)(s1));
    CHECK(gur1() == u0 + 3);
    CHECK(U(DeleteMenu)(m, 0, MF_BYPOSITION));
    CHECK(!U(IsMenu)(s2));
    CHECK(gur1() == u0 + 2);
    CHECK(U(DestroyMenu)(s1));
    CHECK(U(DestroyMenu)(m));
    CHECK(gur1() == u0);
}

/* ---- window properties ---- */
TEST(quota_limits_props_capacity_and_enumeration) {
    REQUIRE(U(SetPropW));
    QL_NEED();
    HWND w = ql_msg_window();
    if (!w) { t_skip("no window"); return; }
    DWORD u0 = gur1(), g0 = gur0();
    enum { N = 1500 };
    int ok = 0;
    for (int i = 0; i < N; i++) ok += U(SetPropW)(w, ATM(0x100 + i), (HANDLE)(ULONG_PTR)(0x1000 + i)) ? 1 : 0;
    CHECK(ok == N);
    int bad = 0;
    for (int i = 0; i < N; i++) if (U(GetPropW)(w, ATM(0x100 + i)) != (HANDLE)(ULONG_PTR)(0x1000 + i)) bad++;
    CHECKF(bad == 0, "%d properties read back wrong", bad);
    CHECK(gur1() == u0 && gur0() == g0);               /* property list entries are not handle table objects */
    /* overwriting replaces the value instead of adding an entry */
    CHECK(U(SetPropW)(w, ATM(0x100), (HANDLE)(ULONG_PTR)0x77));
    CHECK(U(GetPropW)(w, ATM(0x100)) == (HANDLE)(ULONG_PTR)0x77);
    CHECK(U(SetPropW)(w, ATM(0x100), (HANDLE)(ULONG_PTR)0x1000));
    int removed_ok = 0;
    for (int i = 0; i < N; i += 2) removed_ok += U(RemovePropW)(w, ATM(0x100 + i)) == (HANDLE)(ULONG_PTR)(0x1000 + i);
    CHECK(removed_ok == (N + 1) / 2);
    for (int i = 0; i < N; i++) {
        HANDLE v = U(GetPropW)(w, ATM(0x100 + i));
        CHECKF((i & 1) ? v == (HANDLE)(ULONG_PTR)(0x1000 + i) : v == NULL, "prop %d after removing the even ones: %p", i, v);
    }
    CHECK(U(RemovePropW)(w, ATM(0x100)) == NULL);        /* already removed */
    /* destroying the window drops the remaining properties */
    CHECK(U(DestroyWindow)(w));
    CHECK(U(GetPropW)(w, ATM(0x101)) == NULL);
    CHECK(gur1() == u0 - 1);
}

static int CALLBACK count_props(HWND h, LPCWSTR s, HANDLE d) { return 1; }
static BOOL CALLBACK count_props_ex(HWND h, LPWSTR s, HANDLE d, ULONG_PTR user) { (*(int *)user)++; return TRUE; }
TEST(quota_limits_props_enum_count) {
    REQUIRE(U(EnumPropsExW));
    QL_NEED();
    HWND w = ql_msg_window();
    if (!w) { t_skip("no window"); return; }
    int n = 0;
    CHECK(U(EnumPropsExW)(w, (PROPENUMPROCEXW)count_props_ex, (LPARAM)&n) == -1 || n == 0);
    enum { N = 300 };
    for (int i = 0; i < N; i++) CHECK(U(SetPropW)(w, ATM(0x200 + i), (HANDLE)1));
    n = 0;
    U(EnumPropsExW)(w, (PROPENUMPROCEXW)count_props_ex, (LPARAM)&n);
    CHECKF(n == N, "enumerated %d properties, set %d", n, N);
    for (int i = 0; i < N; i++) CHECK(U(RemovePropW)(w, ATM(0x200 + i)) == (HANDLE)1);
    n = 0;
    U(EnumPropsExW)(w, (PROPENUMPROCEXW)count_props_ex, (LPARAM)&n);
    CHECK(n == 0);
    U(DestroyWindow)(w);
}

/* ---- timers per window ---- */
TEST(quota_limits_timers_many_ids_one_window) {
    REQUIRE(U(SetTimer));
    QL_NEED();
    HWND w = ql_msg_window();
    if (!w) { t_skip("no window"); return; }
    DWORD base = gur1();
    enum { N = 1000 };
    int ok = 0;
    for (int i = 1; i <= N; i++) ok += U(SetTimer)(w, i, 0x7ffffff0, NULL) == (UINT_PTR)i;
    CHECK(ok == N);
    CHECKF(gur1() == base + N, "USER count %lu base %lu (one timer object per (window, id))", gur1(), base);
    int killed = 0;
    for (int i = 1; i <= N; i++) killed += U(KillTimer)(w, i) ? 1 : 0;
    CHECK(killed == N);
    CHECK(gur1() == base);
    SetLastError(0);
    CHECK(!U(KillTimer)(w, 5));                       /* nothing left to kill */
    CHECK(U(DestroyWindow)(w));
}

TEST(quota_limits_timers_destroyed_with_window) {
    REQUIRE(U(SetTimer));
    QL_NEED();
    HWND w = ql_msg_window();
    if (!w) { t_skip("no window"); return; }
    DWORD base = gur1();
    for (int i = 1; i <= 50; i++) CHECK(U(SetTimer)(w, i, 0x7ffffff0, NULL) == (UINT_PTR)i);
    CHECK(gur1() == base + 50);
    CHECK(U(DestroyWindow)(w));
    k_pump(0);
    /* the window and its 50 timers are all gone; base included the window itself */
    CHECKF(gur1() + 1 == base, "USER count %lu after DestroyWindow (base %lu includes the window)", gur1(), base);
}

/* ---- icons and cursors ---- */
static HICON mk_icon_plain(void) {
    static BYTE andb[32], xorb[32];
    memset(andb, 0xff, sizeof andb);
    return U(CreateIcon)(GetModuleHandleW(NULL), 16, 16, 1, 1, andb, xorb);
}
TEST(quota_limits_icons_each_one_user_object) {
    REQUIRE(U(CreateIcon));
    QL_NEED();
    DWORD u0 = gur1();
    HICON ic[100];
    for (int i = 0; i < 100; i++) {
        ic[i] = mk_icon_plain();
        CHECK(ic[i] != NULL);
        CHECKF(gur1() == u0 + i + 1, "USER count %lu after %d icons (base %lu)", gur1(), i + 1, u0);
    }
    for (int i = 0; i < 100; i++) CHECK(U(DestroyIcon)(ic[i]));
    CHECK(gur1() == u0);
    SetLastError(0);
    CHECK(!U(DestroyIcon)(ic[0]));
    EXPECT_ERR(ERROR_INVALID_CURSOR_HANDLE);
}

TEST(quota_limits_icon_indirect_releases_gdi_with_icon) {
    REQUIRE(U(CreateIconIndirect));
    QL_NEED();
    DWORD u0 = gur1(), g0 = gur0();
    HBITMAP mask = U(CreateBitmap)(16, 16, 1, 1, NULL), color = U(CreateBitmap)(16, 16, 1, 32, NULL);
    ICONINFO ii; memset(&ii, 0, sizeof ii); ii.fIcon = TRUE; ii.hbmMask = mask; ii.hbmColor = color;
    DWORD g1 = gur0();
    HICON ic[20];
    for (int i = 0; i < 20; i++) { ic[i] = U(CreateIconIndirect)(&ii); CHECK(ic[i] != NULL); }
    CHECKF(gur1() == u0 + 20, "USER count %lu base %lu", gur1(), u0);
    DWORD g2 = gur0();
    t_note("20 icons added %ld GDI handles (source bitmaps stay owned by the caller)", (long)g2 - (long)g1);
    for (int i = 0; i < 20; i++) CHECK(U(DestroyIcon)(ic[i]));
    CHECKF(gur0() == g1, "GDI count %lu after destroying icons, before creating %lu", gur0(), g1);
    CHECK(gur1() == u0);
    CHECK(U(DeleteObject)(mask)); CHECK(U(DeleteObject)(color));
    CHECK(gur0() == g0);
}

TEST(quota_limits_copy_icon_is_new_object) {
    REQUIRE(U(CopyIcon));
    QL_NEED();
    HICON a = mk_icon_plain();
    if (!a) { t_skip("no icon"); return; }
    DWORD u0 = gur1();
    HICON b = U(CopyIcon)(a);
    CHECK(b != NULL && b != a);
    CHECKF(gur1() == u0 + 1, "CopyIcon changed USER count by %ld", (long)gur1() - (long)u0);
    CHECK(U(DestroyIcon)(a));
    CHECK(U(DestroyIcon)(b));
    CHECK(gur1() == u0 - 1);
}

/* ---- ownership: thread owned objects die with the thread, process owned ones survive ---- */
struct own_ctx { HANDLE up, go; int windows; int hooks; int menus; int accels; HMENU m[16]; HACCEL a[16]; HWND w[32]; HHOOK hk[16]; int fail; };
static LRESULT CALLBACK own_hookproc(int c, WPARAM w, LPARAM l) { return U(CallNextHookEx)(NULL, c, w, l); }
static DWORD WINAPI own_thread(void *p) {
    struct own_ctx *x = p;
    ACCEL ac QL_ACCEL_ALIGN; memset(&ac, 0, sizeof ac); ac.fVirt = FVIRTKEY; ac.key = 'Z'; ac.cmd = 9;
    for (int i = 0; i < x->windows; i++) x->w[i] = ql_msg_window();
    for (int i = 0; i < x->hooks; i++) x->hk[i] = U(SetWindowsHookExW)(WH_GETMESSAGE, own_hookproc, NULL, GetCurrentThreadId());
    for (int i = 0; i < x->menus; i++) x->m[i] = U(CreateMenu)();
    for (int i = 0; i < x->accels; i++) x->a[i] = U(CreateAcceleratorTableW)(&ac, 1);
    SetEvent(x->up);
    WaitForSingleObject(x->go, 15000);
    return 0;                                  /* thread exit: the kernel destroys what the thread owns */
}
static int wait_count(DWORD want, int exact_eq, DWORD ms) {
    DWORD t0 = GetTickCount();
    do {
        if (exact_eq ? gur1() == want : gur1() <= want) return 1;
        Sleep(20);
    } while (GetTickCount() - t0 < ms);
    return 0;
}
TEST(quota_limits_owner_thread_exit_frees_thread_owned) {
    REQUIRE(U(CreateMenu));
    QL_NEED();
    static struct own_ctx x;
    memset(&x, 0, sizeof x);
    x.up = CreateEventA(NULL, TRUE, FALSE, NULL); x.go = CreateEventA(NULL, TRUE, FALSE, NULL);
    x.windows = 20; x.hooks = 10; x.menus = 6; x.accels = 4;
    DWORD base = gur1();
    HANDLE th = CreateThread(NULL, 0, own_thread, &x, 0, NULL);
    if (!th || WaitForSingleObject(x.up, 10000) != WAIT_OBJECT_0) { t_skip("thread did not start"); SetEvent(x.go); return; }
    int nw = 0, nh = 0, nm = 0, na = 0;
    for (int i = 0; i < x.windows; i++) nw += x.w[i] != NULL;
    for (int i = 0; i < x.hooks; i++) nh += x.hk[i] != NULL;
    for (int i = 0; i < x.menus; i++) nm += x.m[i] != NULL;
    for (int i = 0; i < x.accels; i++) na += x.a[i] != NULL;
    CHECK(nw == 20 && nh == 10 && nm == 6 && na == 4);
    /* the count is per process: objects made on another thread are visible to this one */
    /* plus at most the hidden helper window that the thread's first window brings along (freed with the thread below) */
    { DWORD now = gur1(), want = base + nw + nh + nm + na;
      CHECKF(now >= want && now - want <= 1, "USER count %lu, expected %lu (+1 helper window at most)", now, want); }
    SetEvent(x.go);
    WaitForSingleObject(th, 10000);
    CloseHandle(th);
    /* windows (type flags 0x239) and hooks (0x101) are thread owned and freed at thread cleanup; menus (0x212)
     * and accelerator tables (0x002) are process owned and stay */
    CHECK(wait_count(base + nm + na, 1, 3000));
    CHECKF(gur1() == base + nm + na, "USER count %lu after thread exit, expected %d", gur1(), (int)base + nm + na);
    for (int i = 0; i < x.windows; i++) CHECK(!U(IsWindow)(x.w[i]));
    for (int i = 0; i < x.menus; i++) { CHECK(U(IsMenu)(x.m[i])); CHECK(U(DestroyMenu)(x.m[i])); }
    for (int i = 0; i < x.accels; i++) CHECK(U(DestroyAcceleratorTable)(x.a[i]));
    CHECK(gur1() == base);
    CloseHandle(x.up); CloseHandle(x.go);
}

TEST(quota_limits_owner_foreign_thread_cannot_destroy_window) {
    REQUIRE(U(DestroyWindow));
    QL_NEED();
    static struct own_ctx x;
    memset(&x, 0, sizeof x);
    x.up = CreateEventA(NULL, TRUE, FALSE, NULL); x.go = CreateEventA(NULL, TRUE, FALSE, NULL);
    x.windows = 3;
    DWORD base = gur1();
    HANDLE th = CreateThread(NULL, 0, own_thread, &x, 0, NULL);
    if (!th || WaitForSingleObject(x.up, 10000) != WAIT_OBJECT_0) { t_skip("thread did not start"); SetEvent(x.go); return; }
    if (x.w[0]) {
        /* DestroyWindow demands that the caller own the window's thread (otherwise ERROR_ACCESS_DENIED) */
        SetLastError(0);
        BOOL r = U(DestroyWindow)(x.w[0]);
        CHECK(!r);
        EXPECT_ERR(ERROR_ACCESS_DENIED);
        CHECK(U(IsWindow)(x.w[0]));
        { DWORD now = gur1(); CHECKF(now >= base + 3 && now - (base + 3) <= 1, "USER count %lu, base %lu", now, base); }   /* +1 helper window at most */
    }
    SetEvent(x.go);
    WaitForSingleObject(th, 10000);
    CloseHandle(th);
    CHECK(wait_count(base, 1, 3000));
    CloseHandle(x.up); CloseHandle(x.go);
}

/* ---- the raw syscall stub for GetGuiResources ---- */
UDECL(DWORD, NtUserGetGuiResources, (HANDLE, DWORD));
TEST(quota_limits_nt_stub_get_gui_resources) {
    NtUserGetGuiResources_pfn f = UN(NtUserGetGuiResources);
    REQUIRE(f);
    QL_NEED();
    CHECK(k_stub_valid((FARPROC)f) || t_is_wine());
    static const DWORD fl[] = { 0, 1, 2, 4 };
    for (int i = 0; i < 4; i++) {
        DWORD a = f(GetCurrentProcess(), fl[i]), b = ql_gur(fl[i]);
        CHECKF(a == b, "stub %lu vs GetGuiResources %lu (flags %lu)", a, b, fl[i]);
    }
    SetLastError(0);
    CHECK(f(GetCurrentProcess(), 5) == 0); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SetLastError(0);
    CHECK(f(NULL, 0) == 0); EXPECT_ERR(ERROR_INVALID_PARAMETER);
}

/* ---- hooks of several types all draw on the same count ---- */
TEST(quota_limits_hooks_thread_local_types_counted) {
    REQUIRE(U(SetWindowsHookExW));
    QL_NEED();
    static const int types[] = { WH_MSGFILTER, WH_KEYBOARD, WH_GETMESSAGE, WH_CALLWNDPROC, WH_MOUSE, WH_FOREGROUNDIDLE, WH_CALLWNDPROCRET };
    DWORD base = gur1();
    HHOOK hk[7][10]; int n = 0;
    for (unsigned t = 0; t < 7; t++)
        for (int i = 0; i < 10; i++) {
            hk[t][i] = U(SetWindowsHookExW)(types[t], own_hookproc, NULL, GetCurrentThreadId());
            CHECKF(hk[t][i] != NULL, "hook type %d #%d failed (error %lu)", types[t], i, GetLastError());
            if (hk[t][i]) n++;
            CHECKF(gur1() == base + n, "USER count %lu after %d hooks", gur1(), n);
        }
    for (unsigned t = 0; t < 7; t++) for (int i = 0; i < 10; i++) if (hk[t][i]) CHECK(U(UnhookWindowsHookEx)(hk[t][i]));
    CHECK(gur1() == base);
    SetLastError(0);
    CHECK(!U(UnhookWindowsHookEx)(hk[0][0]));        /* already removed */
    EXPECT_ERR(ERROR_INVALID_HOOK_HANDLE);
}

/* ---- window DC cache ---- */
static void dc_cache(void) {
    if (!ql_connect()) { t_skip("win32k not reachable"); return; }
    struct ql_quota gq = ql_gdi_quota();
    if (gq.q + QL_SLACK > QL_MAX_OBJ) { t_skip("GDI quota %u exceeds the test budget", gq.q); return; }
    HWND w = ql_msg_window();
    if (!w) { t_skip("no window"); return; }
    DWORD g0 = gur0();
    int cap = (int)gq.q + QL_SLACK, n = 0, refused = 0;
    static unsigned char seen[0x10000];
    int dup = 0, badtype = 0;
    for (int i = 0; i < cap; i++) {
        SetLastError(0);
        HDC dc = U(GetDC)(w);
        if (!dc) { refused = 1; t_note("GetDC refused after %d held DCs, GDI count %lu, error %lu", n, gur0(), GetLastError()); break; }
        ql_f.h[n++] = dc;
        unsigned idx = (unsigned)((ULONG_PTR)dc & 0xffff);
        if (seen[idx]) dup++;
        seen[idx] = 1;
        if (U(GetObjectType)(dc) != OBJ_DC) badtype++;     /* window DCs, not memory DCs */
    }
    QCF(dup == 0, "%d DCs handed out twice while still held", dup);
    QCF(badtype == 0, "%d window DCs with a wrong object type", badtype);
    if (refused) {
        DWORD F = gur0();
        QCF(F <= gq.q && F + 2 >= gq.q, "GetDC refused at GDI count %lu, quota %u", F, gq.q);
    } else QCF(t_is_wine(), "GetDC never refused within %d held DCs", n);
    int rel = 0;
    for (int i = 0; i < n; i++) rel += U(ReleaseDC)(w, (HDC)ql_f.h[i]) == 1;
    QCF(rel == n, "ReleaseDC returned 1 for %d of %d", rel, n);
    t_note("GDI count: base %lu, after release %lu (released cache entries may stay allocated)", g0, gur0());
    SetLastError(0);
    QC(U(ReleaseDC)(w, (HDC)ql_f.h[0]) == 0);                  /* not in use any more */
    QC(U(DestroyWindow)(w));
    k_pump(0);
    t_note("GDI count after DestroyWindow: %lu", gur0());
}
QL_SCENARIO(quota_limits_dc_cache_getdc_held, dc_cache())

/* The kernel reads the ACCEL entries straight from the caller's buffer and demands DWORD alignment: an array at an odd
 * or 2-byte-aligned address is refused with ERROR_NOACCESS (998) however valid the entries are. */
TEST(quota_limits_accel_entries_need_dword_alignment) {
    REQUIRE(U(CreateAcceleratorTableW));
    QL_NEED();
    static unsigned char buf[64] __attribute__((aligned(16)));
    DWORD base = gur1();
    for (int off = 0; off < 8; off++) {
        ACCEL *a = (ACCEL *)(buf + off);
        memset(buf, 0, sizeof buf);
        a->fVirt = FVIRTKEY; a->key = 'Q'; a->cmd = 1;
        SetLastError(0);
        HACCEL h = U(CreateAcceleratorTableW)(a, 1);
        if (off % 4 == 0) { CHECKF(h != NULL, "aligned entry at offset %d refused (error %lu)", off, GetLastError()); }
        else { CHECKF(h == NULL, "misaligned entry at offset %d accepted", off); EXPECT_ERR(ERROR_NOACCESS); }
        if (h) CHECK(U(DestroyAcceleratorTable)(h));
    }
    CHECK(gur1() == base);
}
