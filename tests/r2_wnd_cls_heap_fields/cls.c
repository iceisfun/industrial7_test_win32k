/* r2_wnd_cls_heap_fields: tagCLS fields (client view) and their reaction to SetClassLong, plus the
 * server-class (fnid) bookkeeping of system-class windows.
 *
 * tagCLS offsets are the ones user32!_GetClassData uses (table at user32 rva 0x95342 / 0x95362):
 * +0x02 atom, +0x06 CSF_ flags, +0x08 style (GetClassLong returns it & 0x37bff), +0x0c cbClsExtra,
 * +0x20 window procedure, +0x28 menu name (small values are returned as-is), +0x40 hModule,
 * +0x48 background brush, +0x50/+0x54 cbWndExtra (sum). */
#define R2H_TAG L"cls"
#include "r2h.h"

static LRESULT CALLBACK cls_wp_a(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }
static LRESULT CALLBACK cls_wp_b(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }

static HWND mk_of_class(const wchar_t *cn) { return mk_top(cn, WS_POPUP, 0, -30000, -30000, 20, 20, NULL); }

/* ---- CS_ style bits ----------------------------------------------------------------------------- */
static void style_case(UINT style, bool must) {
    T_BEGIN();
    wchar_t cn[64];
    ATOM a = reg_class(cn, style, 0, 0, cls_wp_a, FALSE, NULL, NULL);
    if (!a) { wk_reap(); t_skip("RegisterClass(style 0x%x) failed: %lu", style, GetLastError()); return; }
    HWND h = mk_of_class(cn);
    NEED_WND(h, w);
    struct cv c;
    CHECK(cv_get(&w, &c));
    uint32_t raw = C32(&c, C_STYLE);
    EXPECT_EQ((uint32_t)U(GetClassLongW)(h, GCL_STYLE), raw & 0x37bff);    /* _GetClassData: GCL_STYLE masks with 0x37bff */
    EXPECT_EQ((uint32_t)U(GetClassLongPtrW)(h, GCL_STYLE), raw & 0x37bff);
    if (must) CHECKF((raw & style) == style, "requested 0x%x stored 0x%x", style, raw);
    else if ((raw & style) != style) t_note("style 0x%x stored as 0x%x", style, raw);
    END();
}
#define CS(id, v, must) TEST(r2_wnd_cls_heap_fields_cls_style_##id) { style_case(v, must); }
CS(none, 0, true)
CS(vredraw, CS_VREDRAW, true)
CS(hredraw, CS_HREDRAW, true)
CS(dblclks, CS_DBLCLKS, true)
CS(owndc, CS_OWNDC, true)
CS(classdc, CS_CLASSDC, true)
CS(parentdc, CS_PARENTDC, true)
CS(noclose, CS_NOCLOSE, true)
CS(savebits, CS_SAVEBITS, true)
CS(bytealignclient, CS_BYTEALIGNCLIENT, true)
CS(bytealignwindow, CS_BYTEALIGNWINDOW, true)
CS(globalclass, CS_GLOBALCLASS, true)
CS(dropshadow, 0x00020000, false)
CS(hv_dbl, CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS, true)
CS(owndc_savebits, CS_OWNDC | CS_SAVEBITS, true)

/* ---- atoms and names ---------------------------------------------------------------------------- */
TEST(r2_wnd_cls_heap_fields_cls_private_atom_in_class_range) {
    T_BEGIN();
    wchar_t cn[64];
    ATOM a = reg_class(cn, 0, 0, 0, cls_wp_a, FALSE, NULL, NULL);
    if (!a) { wk_reap(); t_skip("register failed"); return; }
    CHECKF(a >= 0xC000, "atom 0x%x is not an integer atom of the 0xC000 range", a);
    HWND h = mk_of_class(cn);
    NEED_WND(h, w);
    struct cv c;
    CHECK(cv_get(&w, &c));
    EXPECT_EQ(C16(&c, C_ATOM), a);
    END();
}
TEST(r2_wnd_cls_heap_fields_cls_two_classes_two_atoms_two_objects) {
    T_BEGIN();
    wchar_t c1[64], c2[64];
    ATOM a1 = reg_class(c1, 0, 0, 0, cls_wp_a, FALSE, NULL, NULL), a2 = reg_class(c2, 0, 0, 0, cls_wp_b, FALSE, NULL, NULL);
    if (!a1 || !a2) { wk_reap(); t_skip("register failed"); return; }
    CHECK(a1 != a2);
    HWND h1 = mk_of_class(c1), h2 = mk_of_class(c2);
    NEED_WND(h1, w1);
    NEED_WND(h2, w2);
    EXPECT_NE(W64(&w1, W_PCLS), W64(&w2, W_PCLS));
    struct cv x, y;
    CHECK(cv_get(&w1, &x) && cv_get(&w2, &y));
    EXPECT_EQ(C64(&x, C_WNDPROC), (ULONG_PTR)cls_wp_a);
    EXPECT_EQ(C64(&y, C_WNDPROC), (ULONG_PTR)cls_wp_b);
    END();
}
TEST(r2_wnd_cls_heap_fields_cls_windows_of_one_class_share_object_and_proc) {
    T_BEGIN();
    wchar_t cn[64];
    if (!reg_class(cn, 0, 0, 0, cls_wp_a, FALSE, NULL, NULL)) { wk_reap(); t_skip("register failed"); return; }
    HWND a = mk_of_class(cn), b = mk_of_class(cn), c = mk_of_class(cn);
    NEED_WND(a, wa); NEED_WND(b, wb); NEED_WND(c, wc);
    EXPECT_EQ(W64(&wa, W_PCLS), W64(&wb, W_PCLS));
    EXPECT_EQ(W64(&wb, W_PCLS), W64(&wc, W_PCLS));
    EXPECT_EQ(W64(&wa, W_WNDPROC), (ULONG_PTR)cls_wp_a);     /* the window copies the class procedure at creation */
    EXPECT_EQ(W64(&wb, W_WNDPROC), (ULONG_PTR)cls_wp_a);
    END();
}
TEST(r2_wnd_cls_heap_fields_cls_refcount_probe_note) {
    T_BEGIN();
    wchar_t cn[64];
    if (!reg_class(cn, 0, 0, 0, cls_wp_a, FALSE, NULL, NULL)) { wk_reap(); t_skip("register failed"); return; }
    HWND a = mk_of_class(cn);
    NEED_WND(a, w);
    struct cv c;
    CHECK(cv_get(&w, &c));
    uint32_t d0 = C32(&c, 0);
    HWND b = mk_of_class(cn), d = mk_of_class(cn);
    CHECK(b && d);
    struct wv w2; struct cv c2;
    if (wv_get(a, &w2) && cv_get(&w2, &c2))
        t_note("first dword of tagCLS with 1 / 3 windows: 0x%08lx / 0x%08lx; word at +4: 0x%04x", (unsigned long)d0, (unsigned long)C32(&c2, 0), C16(&c2, 4));
    END();
}

/* ---- SetClassLong effects ----------------------------------------------------------------------- */
TEST(r2_wnd_cls_heap_fields_cls_setclass_wndproc_updates_class_not_old_window) {
    T_BEGIN();
    wchar_t cn[64];
    if (!reg_class(cn, 0, 0, 0, cls_wp_a, FALSE, NULL, NULL)) { wk_reap(); t_skip("register failed"); return; }
    HWND old = mk_of_class(cn);
    NEED_WND(old, w0);
    ULONG_PTR prev = U(SetClassLongPtrW)(old, GCLP_WNDPROC, (LONG_PTR)cls_wp_b);
    EXPECT_EQ(prev, (ULONG_PTR)cls_wp_a);
    struct wv w1; struct cv c;
    CHECK(wv_get(old, &w1) && cv_get(&w1, &c));
    EXPECT_EQ(C64(&c, C_WNDPROC), (ULONG_PTR)cls_wp_b);
    EXPECT_EQ((ULONG_PTR)U(GetClassLongPtrW)(old, GCLP_WNDPROC), (ULONG_PTR)cls_wp_b);
    HWND nw = mk_of_class(cn);
    NEED_WND(nw, w2);
    EXPECT_EQ(W64(&w2, W_WNDPROC), (ULONG_PTR)cls_wp_b);     /* windows created afterwards start with the new procedure */
    if (W64(&w1, W_WNDPROC) != (ULONG_PTR)cls_wp_a) t_note("existing window procedure became 0x%llx after SetClassLongPtr(GCLP_WNDPROC)", (unsigned long long)W64(&w1, W_WNDPROC));
    END();
}
TEST(r2_wnd_cls_heap_fields_cls_setclass_hbrbackground) {
    T_BEGIN();
    wchar_t cn[64];
    HBRUSH b1 = (HBRUSH)(ULONG_PTR)(COLOR_WINDOW + 1), b2 = (HBRUSH)(ULONG_PTR)(COLOR_BTNFACE + 1);
    if (!reg_class(cn, 0, 0, 0, cls_wp_a, FALSE, b1, NULL)) { wk_reap(); t_skip("register failed"); return; }
    HWND h = mk_of_class(cn);
    NEED_WND(h, w);
    struct cv c;
    CHECK(cv_get(&w, &c));
    EXPECT_EQ(C64(&c, C_HBR), (ULONG_PTR)b1);
    EXPECT_EQ(U(SetClassLongPtrW)(h, GCLP_HBRBACKGROUND, (LONG_PTR)b2), (ULONG_PTR)b1);
    CHECK(cv_get(&w, &c));
    EXPECT_EQ(C64(&c, C_HBR), (ULONG_PTR)b2);
    EXPECT_EQ((ULONG_PTR)U(GetClassLongPtrW)(h, GCLP_HBRBACKGROUND), (ULONG_PTR)b2);
    EXPECT_EQ(U(SetClassLongPtrW)(h, GCLP_HBRBACKGROUND, 0), (ULONG_PTR)b2);
    CHECK(cv_get(&w, &c));
    EXPECT_EQ(C64(&c, C_HBR), 0);
    END();
}
TEST(r2_wnd_cls_heap_fields_cls_setclass_hmodule_not_in_window) {
    T_BEGIN();
    wchar_t cn[64];
    if (!reg_class(cn, 0, 0, 0, cls_wp_a, FALSE, NULL, NULL)) { wk_reap(); t_skip("register failed"); return; }
    HWND h = mk_of_class(cn);
    NEED_WND(h, w);
    HMODULE k = GetModuleHandleW(L"kernel32.dll");
    EXPECT_EQ(U(SetClassLongPtrW)(h, GCLP_HMODULE, (LONG_PTR)k), (ULONG_PTR)g_hinst());
    struct wv w1; struct cv c;
    CHECK(wv_get(h, &w1) && cv_get(&w1, &c));
    EXPECT_EQ(C64(&c, C_HMODULE), (ULONG_PTR)k);
    EXPECT_EQ(W64(&w1, W_HMODULE), (ULONG_PTR)g_hinst());   /* the window's own hInstance was fixed at creation */
    EXPECT_EQ((ULONG_PTR)U(GetWindowLongPtrW)(h, GWLP_HINSTANCE), (ULONG_PTR)g_hinst());
    U(SetClassLongPtrW)(h, GCLP_HMODULE, (LONG_PTR)g_hinst());
    END();
}
TEST(r2_wnd_cls_heap_fields_cls_setclass_cbwndextra_for_new_windows_only) {
    T_BEGIN();
    wchar_t cn[64];
    if (!reg_class(cn, 0, 0, 16, cls_wp_a, FALSE, NULL, NULL)) { wk_reap(); t_skip("register failed"); return; }
    HWND old = mk_of_class(cn);
    NEED_WND(old, w0);
    EXPECT_EQ(WI(&w0, W_CBEXTRA), 16);
    EXPECT_EQ(U(SetClassLongW)(old, GCL_CBWNDEXTRA, 48), 16);
    struct wv w1; struct cv c;
    CHECK(wv_get(old, &w1) && cv_get(&w1, &c));
    EXPECT_EQ(C32(&c, C_CBWND0) + C32(&c, C_CBWND1), 48);
    EXPECT_EQ((ULONG)U(GetClassLongW)(old, GCL_CBWNDEXTRA), 48);
    EXPECT_EQ(WI(&w1, W_CBEXTRA), 16);                       /* allocated size of the existing window is unchanged */
    HWND nw = mk_of_class(cn);
    NEED_WND(nw, w2);
    EXPECT_EQ(WI(&w2, W_CBEXTRA), 48);
    END();
}
TEST(r2_wnd_cls_heap_fields_cls_setclass_style_roundtrip) {
    T_BEGIN();
    wchar_t cn[64];
    if (!reg_class(cn, CS_HREDRAW, 0, 0, cls_wp_a, FALSE, NULL, NULL)) { wk_reap(); t_skip("register failed"); return; }
    HWND h = mk_of_class(cn);
    NEED_WND(h, w);
    ULONG prev = U(SetClassLongW)(h, GCL_STYLE, CS_VREDRAW | CS_DBLCLKS);
    EXPECT_EQ(prev & 0x37bff, CS_HREDRAW);
    struct cv c;
    CHECK(cv_get(&w, &c));
    EXPECT_EQ(C32(&c, C_STYLE) & (CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS), CS_VREDRAW | CS_DBLCLKS);
    EXPECT_EQ((ULONG)U(GetClassLongW)(h, GCL_STYLE) & (CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS), CS_VREDRAW | CS_DBLCLKS);
    END();
}
TEST(r2_wnd_cls_heap_fields_cls_setclass_invalid_window) {
    T_BEGIN();
    SetLastError(0);
    EXPECT_EQ(U(SetClassLongPtrW)((HWND)(ULONG_PTR)0x777777, GCLP_HBRBACKGROUND, 0), 0);
    EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
    SetLastError(0);
    EXPECT_EQ(U(GetClassLongPtrW)((HWND)(ULONG_PTR)0x777777, GCLP_HBRBACKGROUND), 0);
    EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
    END();
}

/* ---- menu name ----------------------------------------------------------------------------------- */
TEST(r2_wnd_cls_heap_fields_cls_menuname_integer_resource) {
    T_BEGIN();
    wchar_t cn[64];
    _snwprintf(cn, 64, L"win32kt_r2h_menu_%lu", GetCurrentProcessId());
    WNDCLASSW wc; memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = cls_wp_a; wc.hInstance = g_hinst(); wc.lpszClassName = cn; wc.lpszMenuName = MAKEINTRESOURCEW(101);
    if (!U(RegisterClassW)(&wc)) { wk_reap(); t_skip("register failed"); return; }
    HWND h = U(CreateWindowExW)(EX_GUEST, cn, L"m", WS_POPUP, -30000, -30000, 20, 20, NULL, NULL, g_hinst(), NULL);
    wk_add(h);
    NEED_WND(h, w);
    /* Windows 10 22H2: an integer menu resource id is stored, GetClassInfo reports it, but GetClassLongPtr(GCLP_MENUNAME)
     * answers NULL for it (only a string menu name is returned there) */
    WNDCLASSW back; memset(&back, 0, sizeof back);
    CHECK(U(GetClassInfoW)(g_hinst(), cn, &back));
    EXPECT_EQ((ULONG_PTR)back.lpszMenuName, 101);
    EXPECT_EQ((ULONG_PTR)U(GetClassLongPtrW)(h, GCLP_MENUNAME), 0);
    EXPECT_EQ((ULONG_PTR)U(GetClassLongPtrA)(h, GCLP_MENUNAME), 0);
    U(DestroyWindow)(h);
    U(UnregisterClassW)(cn, g_hinst());
    END();
}
TEST(r2_wnd_cls_heap_fields_cls_menuname_string) {
    T_BEGIN();
    wchar_t cn[64];
    _snwprintf(cn, 64, L"win32kt_r2h_menus_%lu", GetCurrentProcessId());
    WNDCLASSW wc; memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = cls_wp_a; wc.hInstance = g_hinst(); wc.lpszClassName = cn; wc.lpszMenuName = L"MyMenuName";
    if (!U(RegisterClassW)(&wc)) { wk_reap(); t_skip("register failed"); return; }
    HWND h = U(CreateWindowExW)(EX_GUEST, cn, L"m", WS_POPUP, -30000, -30000, 20, 20, NULL, NULL, g_hinst(), NULL);
    wk_add(h);
    NEED_WND(h, w);
    ULONG_PTR pw = (ULONG_PTR)U(GetClassLongPtrW)(h, GCLP_MENUNAME), pa = (ULONG_PTR)U(GetClassLongPtrA)(h, GCLP_MENUNAME);
    CHECK(pw > 0xffff && pa > 0xffff);
    wchar_t ws[32] = L""; char as[32] = "";
    if (k_try_read((void *)pw, ws, sizeof ws - 2)) { ws[31] = 0; EXPECT_WSTR(ws, L"MyMenuName"); } else t_note("Unicode menu name pointer 0x%llx not readable", (unsigned long long)pw);
    if (k_try_read((void *)pa, as, sizeof as - 1)) { as[31] = 0; EXPECT_STR(as, "MyMenuName"); } else t_note("ANSI menu name pointer 0x%llx not readable", (unsigned long long)pa);
    CHECK(pw != pa);                                         /* separate Unicode and ANSI copies are kept */
    U(DestroyWindow)(h);
    U(UnregisterClassW)(cn, g_hinst());
    END();
}

/* ---- global classes ------------------------------------------------------------------------------ */
TEST(r2_wnd_cls_heap_fields_cls_global_class_usable_with_other_hinstance) {
    T_BEGIN();
    wchar_t cn[64];
    if (!reg_class(cn, CS_GLOBALCLASS, 0, 0, cls_wp_a, FALSE, NULL, NULL)) { wk_reap(); t_skip("register failed"); return; }
    HWND own = mk_of_class(cn);
    HMODULE k = GetModuleHandleW(L"kernel32.dll");
    HWND other = wk_add(U(CreateWindowExW)(EX_GUEST, cn, L"g", WS_POPUP, -30000, -30000, 20, 20, NULL, NULL, k, NULL));
    NEED_WND(own, w1);
    CHECK(other != NULL);
    if (!other) { END(); return; }
    struct wv w2;
    CHECK(wv_get(other, &w2));
    EXPECT_EQ(wv_fix(&w2, W64(&w2, W_PCLS)), wv_fix(&w1, W64(&w1, W_PCLS)));   /* one class object, whatever the hInstance used */
    EXPECT_EQ(W64(&w2, W_HMODULE), (ULONG_PTR)k);            /* the window records the hInstance it was created with */
    EXPECT_EQ(W64(&w1, W_HMODULE), (ULONG_PTR)g_hinst());
    END();
}
TEST(r2_wnd_cls_heap_fields_cls_private_class_needs_matching_hinstance) {
    T_BEGIN();
    wchar_t cn[64];
    if (!reg_class(cn, 0, 0, 0, cls_wp_a, FALSE, NULL, NULL)) { wk_reap(); t_skip("register failed"); return; }
    HMODULE k = GetModuleHandleW(L"kernel32.dll");
    SetLastError(0);
    HWND other = U(CreateWindowExW)(EX_GUEST, cn, L"g", WS_POPUP, 0, 0, 20, 20, NULL, NULL, k, NULL);
    wk_add(other);
    CHECK(other == NULL);
    EXPECT_ERR(ERROR_CANNOT_FIND_WND_CLASS);
    END();
}

/* ---- system classes and fnid ---------------------------------------------------------------------- */
struct sysc { const wchar_t *name; DWORD style; int fnid; };
static const struct sysc g_sys[] = {
    { L"Button", BS_PUSHBUTTON, 0 }, { L"Static", SS_LEFT, 0 }, { L"Edit", ES_LEFT, 0x2a5 },
    { L"ListBox", LBS_STANDARD & ~WS_VISIBLE, 0 }, { L"ComboBox", CBS_DROPDOWN, 0 }, { L"ScrollBar", SBS_HORZ, 0x29a },
};
static HWND mk_sys(const struct sysc *s, HWND host) {
    return wk_add(U(CreateWindowExW)(0, s->name, L"x", s->style | WS_CHILD, 0, 0, 80, 40, host, (HMENU)3, g_hinst(), NULL));
}
static void sysfnid_case(int i) {
    T_BEGIN();
    HWND host = mk_host();
    HWND h = host ? mk_sys(&g_sys[i], host) : NULL;
    NEED_WND(h, w);
    unsigned fnid = wv_fnid(&w);
    CHECKF(fnid >= 0x29a && fnid <= 0x2aa, "fnid 0x%x of %ls outside the server-class range", fnid, g_sys[i].name);
    if (g_sys[i].fnid) EXPECT_EQ(fnid, g_sys[i].fnid);
    if (W16(&w, W_FNID) & 0xc000) t_note("%ls: fnid status bits 0x%x set on a live window", g_sys[i].name, W16(&w, W_FNID) & 0xc000);
    /* user32 bounds the server-side extra area per FNID */
    struct k_user_shared_info si;
    if (k_shared_info(&si) && si.psi) {
        uint16_t t = read_w((uint64_t)(ULONG_PTR)si.psi + GPSI_FNIDEXTRA + (fnid - 0x29a) * 2);
        CHECKF(t >= 0x140, "server window size %u below sizeof(WND)", t);
        unsigned srv = t - 0x140;
        if (WI(&w, W_EXTRABASE) != (int)srv) t_note("%ls: fnid 0x%x table extra %u, window first user extra index %d", g_sys[i].name, fnid, srv, WI(&w, W_EXTRABASE));
    }
    /* a system-class window is a server-side class (fnid set) while an application class window is not */
    END();
}
#define SF(id, i) TEST(r2_wnd_cls_heap_fields_cls_sysfnid_##id) { sysfnid_case(i); }
SF(button, 0) SF(static, 1) SF(edit, 2) SF(listbox, 3) SF(combobox, 4) SF(scrollbar, 5)

TEST(r2_wnd_cls_heap_fields_cls_sysfnid_distinct_per_class) {
    T_BEGIN();
    HWND host = mk_host();
    unsigned f[6];
    int n = 0;
    for (int i = 0; i < 6; i++) {
        HWND h = host ? mk_sys(&g_sys[i], host) : NULL;
        struct wv w;
        if (!h || !wv_get(h, &w)) { wk_reap(); t_skip("view of %ls not decodable", g_sys[i].name); return; }
        f[n++] = wv_fnid(&w);
    }
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) CHECKF(f[i] != f[j], "%ls and %ls share fnid 0x%x", g_sys[i].name, g_sys[j].name, f[i]);
    END();
}
TEST(r2_wnd_cls_heap_fields_cls_appclass_window_has_no_fnid) {
    T_BEGIN();
    wchar_t cn[64];
    if (!reg_class(cn, 0, 0, 8, cls_wp_a, FALSE, NULL, NULL)) { wk_reap(); t_skip("register failed"); return; }
    HWND h = mk_of_class(cn);
    NEED_WND(h, w);
    EXPECT_EQ(wv_fnid(&w), 0);                               /* _GetWindowLongPtr: (fnid & 0x2fff) == 0 means a plain application window */
    EXPECT_EQ(W32(&w, W_STATE) & ST_SRVPROC, 0);
    END();
}

/* ---- NtUserSetWindowFNID status bit ---------------------------------------------------------------- */
UDECL(DWORD, NtUserSetWindowFNID, (HWND, WORD));
TEST(r2_wnd_cls_heap_fields_cls_setwindowfnid_status_bit_is_or_ed) {
    T_BEGIN();
    REQUIRE(UN(NtUserSetWindowFNID));
    HWND h = mk_plain();
    NEED_WND(h, w0);
    EXPECT_EQ(W16(&w0, W_FNID), 0);
    /* NtUserSetWindowFNID: param 0x4000 is OR-ed into the 16-bit fnid of a window of this process */
    EXPECT_EQ(UN(NtUserSetWindowFNID)(h, 0x4000), 1);
    struct wv w1;
    CHECK(wv_get(h, &w1));
    EXPECT_EQ(W16(&w1, W_FNID), 0x4000);
    EXPECT_EQ(wv_fnid(&w1), 0);                              /* user32 masks it out with 0x2fff */
    EXPECT_EQ(UN(NtUserSetWindowFNID)(h, 0x4000), 1);        /* idempotent */
    CHECK(wv_get(h, &w1));
    EXPECT_EQ(W16(&w1, W_FNID), 0x4000);
    END();
}
TEST(r2_wnd_cls_heap_fields_cls_setwindowfnid_other_values_rejected) {
    T_BEGIN();
    REQUIRE(UN(NtUserSetWindowFNID));
    HWND h = mk_plain();
    NEED_WND(h, w0);
    static const WORD bad[] = { 0x0001, 0x8000, 0x2000, 0x29a, 0x2ab, 0x2a0, 0xffff, 0x4001 };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        SetLastError(0);
        EXPECT_EQ(UN(NtUserSetWindowFNID)(h, bad[i]), 0);   /* only 0x4000 and the class fnids 0x2a1..0x2aa pass */
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
        struct wv w1;
        CHECK(wv_get(h, &w1));
        EXPECT_EQ(W16(&w1, W_FNID), 0);
    }
    END();
}
TEST(r2_wnd_cls_heap_fields_cls_setwindowfnid_foreign_process_window) {
    T_BEGIN();
    REQUIRE(UN(NtUserSetWindowFNID));
    HWND d = U(GetDesktopWindow)();
    struct wv w0;
    if (!wv_get(d, &w0)) { wk_reap(); t_skip("desktop view not decodable"); return; }
    SetLastError(0);
    EXPECT_EQ(UN(NtUserSetWindowFNID)(d, 0x4000), 0);        /* the desktop window belongs to another process */
    EXPECT_ERR(ERROR_ACCESS_DENIED);
    struct wv w1;
    CHECK(wv_get(d, &w1));
    EXPECT_EQ(W16(&w1, W_FNID), W16(&w0, W_FNID));
    SetLastError(0);
    EXPECT_EQ(UN(NtUserSetWindowFNID)((HWND)(ULONG_PTR)0x777777, 0x4000), 0);
    EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
    END();
}
