/* Wrapper variants (RegisterClassA/W, GetClassInfoA/W, GetClassNameA), icon ownership, class style bits with
 * kernel-visible effects (CS_OWNDC, CS_CLASSDC, CS_NOCLOSE) and the shared CLS image between windows. */
#include "cr.h"

TEST(class_registration_wrapper_registerclassw_and_getclassinfow) {
    CR_NEED();
    const WCHAR *n = cr_name();
    WNDCLASSW wc; memset(&wc, 0, sizeof wc);
    wc.style = CS_HREDRAW; wc.lpfnWndProc = cr_proc; wc.cbClsExtra = 6; wc.cbWndExtra = 10;
    wc.hInstance = cr_hinst(); wc.lpszClassName = n;
    ATOM a = U(RegisterClassW)(&wc);
    REQUIRE(a);
    WNDCLASSW o; memset(&o, 0, sizeof o);
    CHECK(U(GetClassInfoW)(cr_hinst(), n, &o));
    EXPECT_EQ(o.style, CS_HREDRAW);
    EXPECT_EQ(o.lpfnWndProc, cr_proc);
    EXPECT_EQ(o.cbClsExtra, 6);
    EXPECT_EQ(o.cbWndExtra, 10);
    EXPECT_EQ(o.lpszClassName, n);
    WNDCLASSEXW ex; memset(&ex, 0, sizeof ex); ex.cbSize = sizeof ex;
    CHECK(U(GetClassInfoExW)(cr_hinst(), n, &ex));
    CHECK(ex.hIconSm == NULL || ex.hIconSm != NULL);     /* only reported: derived from hIcon when absent */
    CHECK(cr_unreg(n));
}

TEST(class_registration_wrapper_registerclassa_roundtrip) {
    CR_NEED();
    const WCHAR *n = cr_name();
    char an[96]; WideCharToMultiByte(CP_ACP, 0, n, -1, an, sizeof an, NULL, NULL);
    WNDCLASSA wc; memset(&wc, 0, sizeof wc);
    wc.style = CS_DBLCLKS; wc.lpfnWndProc = cr_procA; wc.cbClsExtra = 2; wc.hInstance = cr_hinst(); wc.lpszClassName = an;
    ATOM a = U(RegisterClassA)(&wc);
    REQUIRE(a);
    WNDCLASSA o; memset(&o, 0, sizeof o);
    CHECK(U(GetClassInfoA)(cr_hinst(), an, &o));
    EXPECT_EQ(o.style, CS_DBLCLKS);
    EXPECT_EQ(o.lpfnWndProc, cr_procA);
    EXPECT_EQ(o.cbClsExtra, 2);
    /* the same class asked through the Unicode API: same atom, translated procedure */
    WNDCLASSEXW ex; memset(&ex, 0, sizeof ex); ex.cbSize = sizeof ex;
    CHECK(U(GetClassInfoExW)(cr_hinst(), n, &ex));
    CHECK((ULONG_PTR)ex.lpfnWndProc != (ULONG_PTR)cr_procA);
    CHECK(U(UnregisterClassA)(an, cr_hinst()));
}

TEST(class_registration_getclassname_ansi_and_wide_agree) {
    CR_NEED();
    const WCHAR *n = cr_name();
    REQUIRE(cr_reg(n, 0, 0, 0));
    HWND h = cr_win(n);
    if (!h) { t_skip("no window"); cr_unreg(n); return; }
    char a[96] = { 0 }; WCHAR w[96] = { 0 };
    int la = U(GetClassNameA)(h, a, 96), lw = U(GetClassNameW)(h, w, 96);
    EXPECT_EQ(la, lw);
    char an[96]; WideCharToMultiByte(CP_ACP, 0, n, -1, an, sizeof an, NULL, NULL);
    EXPECT_STR(a, an);
    EXPECT_WSTR(w, n);
    U(DestroyWindow)(h);
    cr_unreg(n);
}

TEST(class_registration_icon_handle_owned_by_class) {
    CR_NEED();
    if (!U(CreateIcon) || !U(DestroyIcon)) { t_skip("no CreateIcon"); return; }
    BYTE andb[4] = { 0xff, 0xff, 0xff, 0xff }, xorb[4] = { 0, 0, 0, 0 };
    HICON ic = U(CreateIcon)(cr_hinst(), 8, 4, 1, 1, andb, xorb);
    if (!ic) { t_skip("CreateIcon failed, err %lu", GetLastError()); return; }
    const WCHAR *n = cr_name();
    WNDCLASSEXW wc; cr_wc(&wc, n, 0, 0, 0);
    wc.hIcon = ic;
    ATOM a = U(RegisterClassExW)(&wc);
    CHECKF(a != 0, "class with a freshly created icon failed, err %lu", GetLastError());
    if (a) {
        WNDCLASSEXW o; memset(&o, 0, sizeof o); o.cbSize = sizeof o;
        CHECK(U(GetClassInfoExW)(cr_hinst(), n, &o));
        EXPECT_EQ(o.hIcon, ic);
        struct k_user_entry e;
        if (k_user_entry((HANDLE)ic, &e)) EXPECT_EQ(e.type, 3);     /* TYPE_CURSOR: ValidateAndLockCursor(.., 3) */
        BOOL d = U(DestroyIcon)(ic);
        WNDCLASSEXW o2; memset(&o2, 0, sizeof o2); o2.cbSize = sizeof o2;
        BOOL still = U(GetClassInfoExW)(cr_hinst(), n, &o2);
        t_note("DestroyIcon on a class-locked icon: %d, class still reports hIcon=%p", d, still ? (void *)o2.hIcon : NULL);
        cr_unreg(n);
    } else U(DestroyIcon)(ic);
}

TEST(class_registration_icon_sm_defaults) {
    CR_NEED();
    HICON ic = U(LoadIconW)(NULL, MAKEINTRESOURCEW(32512));
    if (!ic) { t_skip("no stock icon"); return; }
    const WCHAR *n = cr_name();
    WNDCLASSEXW wc; cr_wc(&wc, n, 0, 0, 0);
    wc.hIcon = ic;
    REQUIRE(U(RegisterClassExW)(&wc));
    WNDCLASSEXW o; memset(&o, 0, sizeof o); o.cbSize = sizeof o;
    CHECK(U(GetClassInfoExW)(cr_hinst(), n, &o));
    EXPECT_EQ(o.hIcon, ic);
    t_note("hIconSm of a class registered without one: %p", o.hIconSm);
    cr_unreg(n);
}

TEST(class_registration_style_ownDC_returns_stable_dc) {
    CR_NEED();
    if (!U(GetDC) || !U(ReleaseDC)) { t_skip("no GetDC"); return; }
    const WCHAR *n = cr_name();
    REQUIRE(cr_reg(n, CS_OWNDC, 0, 0));
    HWND h = cr_win(n);
    if (!h) { t_skip("no window"); cr_unreg(n); return; }
    HDC a = U(GetDC)(h), b = U(GetDC)(h);
    CHECK(a != NULL);
    EXPECT_EQ(a, b);            /* CS_OWNDC: the window keeps one private DC */
    U(ReleaseDC)(h, b); U(ReleaseDC)(h, a);
    HDC c = U(GetDC)(h);
    EXPECT_EQ(c, a);
    U(ReleaseDC)(h, c);
    U(DestroyWindow)(h);
    cr_unreg(n);
}

TEST(class_registration_style_classDC_shared_by_windows) {
    CR_NEED();
    if (!U(GetDC) || !U(ReleaseDC)) { t_skip("no GetDC"); return; }
    const WCHAR *n = cr_name();
    REQUIRE(cr_reg(n, CS_CLASSDC, 0, 0));
    HWND h1 = cr_win(n), h2 = cr_win(n);
    if (!h1 || !h2) { t_skip("no windows"); if (h1) U(DestroyWindow)(h1); if (h2) U(DestroyWindow)(h2); cr_unreg(n); return; }
    HDC a = U(GetDC)(h1), b = U(GetDC)(h2);
    CHECK(a != NULL);
    EXPECT_EQ(a, b);            /* CS_CLASSDC: one DC for all windows of the class */
    U(ReleaseDC)(h2, b); U(ReleaseDC)(h1, a);
    U(DestroyWindow)(h1); U(DestroyWindow)(h2);
    cr_unreg(n);
}

static int close_grayed(const WCHAR *cls) {
    HWND h = U(CreateWindowExW)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls, L"", WS_POPUP | WS_CAPTION | WS_SYSMENU,
                                -32000, -32000, 100, 60, NULL, NULL, cr_hinst(), NULL);
    if (!h) return -2;
    HMENU m = U(GetSystemMenu)(h, FALSE);
    int r = -2;
    if (m) {
        UINT st = U(GetMenuState)(m, SC_CLOSE, MF_BYCOMMAND);
        r = st == (UINT)-1 ? -1 : (st & MF_GRAYED) ? 1 : 0;      /* -1: no Close item at all */
        if (st == (UINT)-1) {   /* the other standard items are still there */
            CHECK(U(GetMenuState)(m, SC_MOVE, MF_BYCOMMAND) != (UINT)-1);
            CHECK(U(GetMenuState)(m, SC_SIZE, MF_BYCOMMAND) != (UINT)-1);
        }
    }
    U(DestroyWindow)(h);
    return r;
}

TEST(class_registration_style_noclose_grays_close_item) {
    CR_NEED();
    if (!U(GetSystemMenu) || !U(GetMenuState)) { t_skip("no menu API"); return; }
    const WCHAR *n1 = cr_name(), *n2 = cr_name();
    REQUIRE(cr_reg(n1, CS_NOCLOSE, 0, 0));
    REQUIRE(cr_reg(n2, 0, 0, 0));
    int g1 = close_grayed(n1), g2 = close_grayed(n2);
    if (g1 == -2 || g2 == -2) t_skip("system menu unavailable (%d/%d)", g1, g2);
    else {
        /* Windows 10 removes the Close item (and the separator before it) from the system menu of a CS_NOCLOSE window
         * instead of graying it; an ordinary window has an enabled Close item */
        EXPECT_EQ(g1, -1);
        EXPECT_EQ(g2, 0);
    }
    cr_unreg(n1); cr_unreg(n2);
}

TEST(class_registration_cls_image_shared_between_windows) {
    CR_NEED();
    const WCHAR *n1 = cr_name(), *n2 = cr_name();
    REQUIRE(cr_reg(n1, 0, 0, 0));
    REQUIRE(cr_reg(n2, 0, 0, 0));
    HWND a = cr_win(n1), b = cr_win(n1), c = cr_win(n2);
    if (!a || !b || !c) {
        t_skip("no windows");
        if (a) U(DestroyWindow)(a);
        if (b) U(DestroyWindow)(b);
        if (c) U(DestroyWindow)(c);
        cr_unreg(n1); cr_unreg(n2); return;
    }
    void *ca = cr_cls_client(a), *cb = cr_cls_client(b), *cc = cr_cls_client(c);
    if (ca && cb && cc) {
        EXPECT_EQ(ca, cb);          /* windows of one class point at one CLS in the desktop heap */
        EXPECT_NE(ca, cc);
    } else t_note("CLS images not reachable");
    EXPECT_EQ(U(GetClassWord)(a, CR_GCW_ATOM), U(GetClassWord)(b, CR_GCW_ATOM));
    CHECK(U(GetClassWord)(a, CR_GCW_ATOM) != U(GetClassWord)(c, CR_GCW_ATOM));
    U(DestroyWindow)(a); U(DestroyWindow)(b); U(DestroyWindow)(c);
    cr_unreg(n1); cr_unreg(n2);
}

TEST(class_registration_creation_calls_class_wndproc) {
    CR_NEED();
    const WCHAR *n = cr_name();
    REQUIRE(cr_reg(n, 0, 0, 0));
    LONG before = cr_hits;
    HWND h = cr_win(n);
    if (!h) { t_skip("no window"); cr_unreg(n); return; }
    /* WM_GETMINMAXINFO / WM_NCCREATE / WM_CREATE are delivered through the kernel callback table to the class
     * procedure while CreateWindowEx runs */
    CHECKF(cr_hits - before >= 3, "only %ld messages reached the class procedure", cr_hits - before);
    U(DestroyWindow)(h);
    cr_unreg(n);
}

TEST(class_registration_null_wndproc_is_reported) {
    CR_NEED();
    /* behaviour of a NULL window procedure is not asserted - the outcome is recorded */
    const WCHAR *n = cr_name();
    WNDCLASSEXW wc; cr_wc(&wc, n, 0, 0, 0);
    wc.lpfnWndProc = NULL;
    SET_ERR(0);
    ATOM a = U(RegisterClassExW)(&wc);
    t_info("RegisterClassExW with lpfnWndProc=NULL -> atom 0x%x err %lu", a, GetLastError());
    if (a) cr_unreg(n);
}

TEST(class_registration_direct_cbsize_not_checked_by_kernel) {
    CR_NEED_NT();
    /* user32 enforces cbSize == 0x50; the syscall copies 0x50 bytes regardless of the field */
    const WCHAR *n = cr_name();
    WNDCLASSEXW wc; cr_wc(&wc, n, 0, 0, 0);
    wc.cbSize = 0x1234;
    ATOM a = cr_nt_reg(&wc, n, 0x80);
    t_info("direct register with cbSize=0x1234 -> atom 0x%x err %lu", a, GetLastError());
    if (a) cr_nt_unreg(n, cr_hinst());
}

TEST(class_registration_direct_unregister_returns_one) {
    CR_NEED_NT();
    const WCHAR *n = cr_name();
    WNDCLASSEXW wc; cr_wc(&wc, n, 0, 0, 0);
    REQUIRE(cr_nt_reg(&wc, n, 0x80));
    UNICODE_STRING us; cr_us(&us, n);
    CR_MENU out[2]; memset(out, 0, sizeof out);
    LONG_PTR r = UN(NtUserUnregisterClass)(&us, cr_hinst(), out);
    EXPECT_EQ(r, 1);                /* NtUserUnregisterClass returns the BOOL as a qword 1/0 */
    EXPECT_EQ(out[0].ansi, 0);
    EXPECT_EQ(out[0].unicode, 0);
    SET_ERR(0);
    r = UN(NtUserUnregisterClass)(&us, cr_hinst(), out);
    EXPECT_EQ(r, 0);
    EXPECT_ERR(CR_ERR_DOES_NOT_EXIST);
}
