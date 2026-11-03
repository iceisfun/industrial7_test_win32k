/* GetWindowInfo (WINDOWINFO), AdjustWindowRect[Ex][ForDpi] and WM_NCCALCSIZE against the kernel-maintained
 * tagWND geometry (rcWindow +0x58, rcClient +0x68 in the read-only desktop-heap view).
 * user32!GetWindowInfo copies the tagWND fields directly; AdjustWindowRectExForDpi is pure client code
 * (GetSystemMetricsForDpi + GetWindowBordersForDpiWithCompatFlags2), so the expected values below come from
 * those two analysed routines. */
#include "sib.h"

typedef int  (WINAPI *GSMFD_fn)(int, UINT);
typedef UINT (WINAPI *GDFS_fn)(void);
typedef BOOL (WINAPI *ADJFD_fn)(LPRECT, DWORD, BOOL, DWORD, UINT);

static GSMFD_fn gsmfd(void) { return (GSMFD_fn)t_proc("GetSystemMetricsForDpi"); }
static UINT sysdpi(void) { GDFS_fn f = (GDFS_fn)t_proc("GetDpiForSystem"); return f ? f() : 96; }
static int metric(int i) { GSMFD_fn f = gsmfd(); return f ? f(i, sysdpi()) : U(GetSystemMetrics)(i); }

static HWND mk(DWORD style, DWORD ex, int w, int h, int vis) {
    if (!U(CreateWindowExW)) return NULL;
    HWND hw = U(CreateWindowExW)(ex | WS_EX_NOACTIVATE, sib_class(), L"win32kt_wi", style & ~WS_VISIBLE, -30000, -30000, w, h,
                                 NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (hw && vis) U(ShowWindow)(hw, SW_SHOWNOACTIVATE);
    return hw;
}
static int wi_get(HWND h, WINDOWINFO *wi) {
    memset(wi, 0xCC, sizeof *wi);
    wi->cbSize = sizeof *wi;
    return U(GetWindowInfo)(h, wi);
}

/* ---- GetWindowInfo ------------------------------------------------------------------------ */

TEST(wi_invalid_hwnd) {
    REQUIRE(U(GetWindowInfo)); REQUIRE(U(CreateWindowExW));
    HWND gone = mk(WS_OVERLAPPEDWINDOW, 0, 100, 100, 0); SIB_NEED_WND(gone);
    U(DestroyWindow)(gone);
    HWND bad[] = { NULL, (HWND)(ULONG_PTR)0x4242, (HWND)(ULONG_PTR)-1, (HWND)(ULONG_PTR)0x7fffffff, gone };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        WINDOWINFO wi;
        SET_ERR(0);
        /* user32!GetWindowInfo: handle table lookup fails => ERROR_INVALID_WINDOW_HANDLE, FALSE. */
        CHECK(!wi_get(bad[i], &wi));
        EXPECT_ERR(E_INVALID_HWND);
    }
}

TEST(wi_bad_cbsize_sets_error) {
    REQUIRE(U(GetWindowInfo)); REQUIRE(U(CreateWindowExW));
    HWND h = mk(WS_OVERLAPPEDWINDOW, 0, 200, 200, 0); SIB_NEED_WND(h);
    static const DWORD sz[] = { 0, 4, 0x38, 0x3b, 0x3d, 0x40, 0x100 };
    for (unsigned i = 0; i < sizeof sz / sizeof *sz; i++) {
        WINDOWINFO wi; memset(&wi, 0, sizeof wi); wi.cbSize = sz[i];
        SET_ERR(0);
        BOOL r = U(GetWindowInfo)(h, &wi);
        /* user32!GetWindowInfo records ERROR_INVALID_PARAMETER when cbSize != 0x3c (pwi == NULL is never passed here). */
        EXPECT_ERR(E_INVALID_PARAM);
        t_note("cbSize %#lx -> return %d", sz[i], r);
    }
    U(DestroyWindow)(h);
}

TEST(wi_good_cbsize_clears_nothing) {
    REQUIRE(U(GetWindowInfo)); REQUIRE(U(CreateWindowExW));
    HWND h = mk(WS_OVERLAPPEDWINDOW, 0, 200, 200, 0); SIB_NEED_WND(h);
    WINDOWINFO wi;
    SET_ERR(0);
    CHECK(wi_get(h, &wi));
    EXPECT_ERR(0);
    CHECK(wi.cbSize == sizeof wi);
    U(DestroyWindow)(h);
}

TEST(wi_rects_match_getwindowrect) {
    REQUIRE(U(GetWindowInfo)); REQUIRE(U(GetWindowRect)); REQUIRE(U(GetClientRect)); REQUIRE(U(ClientToScreen));
    static const struct { DWORD style, ex; } t[] = {
        { WS_OVERLAPPEDWINDOW, 0 }, { WS_POPUP, 0 }, { WS_POPUP | WS_BORDER, 0 }, { WS_CAPTION, 0 }, { WS_THICKFRAME, 0 },
        { WS_OVERLAPPED | WS_VSCROLL, 0 }, { WS_OVERLAPPED | WS_HSCROLL | WS_VSCROLL, 0 }, { WS_POPUP, WS_EX_CLIENTEDGE },
        { WS_POPUP, WS_EX_DLGMODALFRAME }, { WS_CAPTION, WS_EX_TOOLWINDOW }, { WS_CAPTION | WS_SYSMENU, 0 },
    };
    for (unsigned i = 0; i < sizeof t / sizeof *t; i++) {
        for (int vis = 0; vis < 2; vis++) {
            HWND h = mk(t[i].style, t[i].ex, 300, 250, vis); SIB_NEED_WND(h);
            WINDOWINFO wi; RECT wr, cr; POINT o = { 0, 0 };
            CHECK(wi_get(h, &wi) && U(GetWindowRect)(h, &wr) && U(GetClientRect)(h, &cr) && U(ClientToScreen)(h, &o));
            /* Both come from tagWND.rcWindow / rcClient (screen coordinates). */
            CHECKF(!memcmp(&wi.rcWindow, &wr, sizeof wr), "row %u vis %d window rect", i, vis);
            CHECK(wi.rcClient.right - wi.rcClient.left == cr.right - cr.left && wi.rcClient.bottom - wi.rcClient.top == cr.bottom - cr.top);
            CHECKF(wi.rcClient.left == o.x && wi.rcClient.top == o.y, "row %u client origin %ld,%ld vs %ld,%ld", i, wi.rcClient.left, wi.rcClient.top, o.x, o.y);
            CHECK(wi.rcClient.left >= wi.rcWindow.left && wi.rcClient.top >= wi.rcWindow.top && wi.rcClient.right <= wi.rcWindow.right && wi.rcClient.bottom <= wi.rcWindow.bottom);
            U(DestroyWindow)(h);
        }
    }
}

TEST(wi_window_size_as_created) {
    REQUIRE(U(GetWindowInfo)); REQUIRE(U(CreateWindowExW));
    HWND h = mk(WS_POPUP, 0, 321, 123, 0); SIB_NEED_WND(h);
    WINDOWINFO wi;
    CHECK(wi_get(h, &wi));
    CHECK(wi.rcWindow.right - wi.rcWindow.left == 321 && wi.rcWindow.bottom - wi.rcWindow.top == 123);
    CHECK(wi.rcWindow.left == -30000 && wi.rcWindow.top == -30000);
    /* A popup without any border or scroll bar: client area == window area. */
    CHECK(!memcmp(&wi.rcClient, &wi.rcWindow, sizeof(RECT)));
    U(DestroyWindow)(h);
}

TEST(wi_tracks_setwindowpos) {
    REQUIRE(U(GetWindowInfo)); REQUIRE(U(SetWindowPos)); REQUIRE(U(CreateWindowExW));
    HWND h = mk(WS_POPUP | WS_BORDER, 0, 100, 100, 1); SIB_NEED_WND(h);
    WINDOWINFO wi;
    CHECK(U(SetWindowPos)(h, NULL, -20000, -21000, 210, 130, SWP_NOZORDER | SWP_NOACTIVATE));
    CHECK(wi_get(h, &wi));
    CHECK(wi.rcWindow.left == -20000 && wi.rcWindow.top == -21000 && wi.rcWindow.right == -20000 + 210 && wi.rcWindow.bottom == -21000 + 130);
    /* WS_BORDER removes one pixel per side from the client rectangle. */
    CHECK(wi.rcClient.left == wi.rcWindow.left + 1 && wi.rcClient.top == wi.rcWindow.top + 1);
    CHECK(wi.rcClient.right == wi.rcWindow.right - 1 && wi.rcClient.bottom == wi.rcWindow.bottom - 1);
    U(DestroyWindow)(h);
}

TEST(wi_style_fields) {
    REQUIRE(U(GetWindowInfo)); REQUIRE(U(GetWindowLongW)); REQUIRE(U(CreateWindowExW));
    static const struct { DWORD style, ex; } t[] = {
        { WS_OVERLAPPEDWINDOW, 0 }, { WS_POPUP, WS_EX_TOOLWINDOW }, { WS_CAPTION | WS_SYSMENU, WS_EX_DLGMODALFRAME },
        { WS_POPUP | WS_DISABLED, WS_EX_TRANSPARENT }, { WS_OVERLAPPED | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, WS_EX_CONTROLPARENT },
    };
    for (unsigned i = 0; i < sizeof t / sizeof *t; i++) for (int vis = 0; vis < 2; vis++) {
        HWND h = mk(t[i].style, t[i].ex, 200, 200, vis); SIB_NEED_WND(h);
        WINDOWINFO wi;
        CHECK(wi_get(h, &wi));
        CHECK(wi.dwStyle == (DWORD)U(GetWindowLongW)(h, GWL_STYLE));
        CHECKF(!!(wi.dwStyle & WS_VISIBLE) == vis, "row %u vis %d style %#lx", i, vis, wi.dwStyle);
        CHECK((wi.dwStyle & (t[i].style & ~WS_VISIBLE)) == (t[i].style & ~WS_VISIBLE));
        /* tagWND.ExStyle (+0x18) minus WS_EX_LAYERED handling; creation flags we asked for must be present. */
        CHECKF((wi.dwExStyle & (t[i].ex | WS_EX_NOACTIVATE)) == (t[i].ex | WS_EX_NOACTIVATE), "row %u ex %#lx", i, wi.dwExStyle);
        DWORD gl = (DWORD)U(GetWindowLongW)(h, GWL_EXSTYLE);
        /* GetWindowInfo reports the window's internal extended-style word, which carries two bits GetWindowLong hides
         * (0x20000000 = redirected to the compositor, 0x00000800); WS_EX_LAYERED may also differ. Nothing else may. */
        CHECKF(((wi.dwExStyle ^ gl) & ~(DWORD)(WS_EX_LAYERED | 0x20000800)) == 0, "row %u wi ex %#lx GWL_EXSTYLE %#lx", i, wi.dwExStyle, gl);
        U(DestroyWindow)(h);
    }
}

TEST(wi_status_inactive_window) {
    REQUIRE(U(GetWindowInfo)); REQUIRE(U(CreateWindowExW));
    HWND h = mk(WS_OVERLAPPEDWINDOW, 0, 200, 200, 0); SIB_NEED_WND(h);
    WINDOWINFO wi;
    CHECK(wi_get(h, &wi));
    /* dwWindowStatus = WS_ACTIVECAPTION (1) only when tagWND state bit 0x40 is set, i.e. the frame is drawn active;
     * a window that was never shown or activated cannot have it. */
    EQ(wi.dwWindowStatus, 0);
    U(ShowWindow)(h, SW_SHOWNOACTIVATE);
    CHECK(wi_get(h, &wi));
    CHECK(wi.dwWindowStatus <= WS_ACTIVECAPTION);
    t_note("dwWindowStatus after SW_SHOWNOACTIVATE: %lu", wi.dwWindowStatus);
    U(DestroyWindow)(h);
}

TEST(wi_status_child_window) {
    REQUIRE(U(GetWindowInfo)); REQUIRE(U(CreateWindowExW));
    HWND p = mk(WS_OVERLAPPEDWINDOW, 0, 200, 200, 0); SIB_NEED_WND(p);
    HWND c = sib_child(p, L"Static", 0, 0, 0, 50, 20);
    if (!c) { t_skip("child"); U(DestroyWindow)(p); return; }
    WINDOWINFO wi;
    CHECK(wi_get(c, &wi));
    CHECK(wi.dwWindowStatus == 0);
    CHECK(wi.dwStyle & WS_CHILD);
    /* Child rectangles are still reported in screen coordinates. */
    WINDOWINFO pi;
    CHECK(wi_get(p, &pi));
    CHECK(wi.rcWindow.left >= pi.rcClient.left && wi.rcWindow.top >= pi.rcClient.top);
    U(DestroyWindow)(p);
}

TEST(wi_atom_window_type) {
    REQUIRE(U(GetWindowInfo)); REQUIRE(U(CreateWindowExW)); REQUIRE(U(GetClassWord));
    HWND p = mk(WS_OVERLAPPEDWINDOW, 0, 200, 200, 0); SIB_NEED_WND(p);
    WINDOWINFO wi;
    CHECK(wi_get(p, &wi));
    /* atomWindowType is the class atom (documented); same value GetClassWord(GCW_ATOM) returns. */
    CHECK(wi.atomWindowType != 0);
    CHECK(wi.atomWindowType == (ATOM)U(GetClassWord)(p, GCW_ATOM));
    static const wchar_t *const sys[] = { L"Static", L"Button", L"Edit", L"ListBox", L"ComboBox", L"ScrollBar" };
    for (unsigned i = 0; i < 6; i++) {
        HWND c = sib_child(p, sys[i], 0, 0, 0, 40, 20);
        if (!c) continue;
        CHECK(wi_get(c, &wi));
        CHECKF(wi.atomWindowType == (ATOM)U(GetClassWord)(c, GCW_ATOM), "class %ls", sys[i]);
        CHECK(wi.atomWindowType != 0);
        /* System classes have atoms in the 0xC000 string-atom range. */
        CHECKF(wi.atomWindowType >= 0xC000, "class %ls atom %#x", sys[i], wi.atomWindowType);
    }
    U(DestroyWindow)(p);
}

TEST(wi_creator_version) {
    REQUIRE(U(GetWindowInfo)); REQUIRE(U(CreateWindowExW));
    HWND a = mk(WS_OVERLAPPEDWINDOW, 0, 100, 100, 0), b = mk(WS_POPUP, 0, 100, 100, 0);
    if (!a || !b) { t_skip("window"); return; }
    WINDOWINFO wa, wb;
    CHECK(wi_get(a, &wa) && wi_get(b, &wb));
    /* Derived from three tagWND bits in GetWindowInfo: 0x500, 0x400, 0x30a or 0x300. */
    CHECKF(wa.wCreatorVersion == 0x500 || wa.wCreatorVersion == 0x400 || wa.wCreatorVersion == 0x30a || wa.wCreatorVersion == 0x300, "version %#x", wa.wCreatorVersion);
    CHECK(wa.wCreatorVersion == wb.wCreatorVersion);      /* same process, same expected-version stamp */
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((char *)GetModuleHandleW(NULL) + ((IMAGE_DOS_HEADER *)GetModuleHandleW(NULL))->e_lfanew);
    t_note("wCreatorVersion %#x (exe subsystem %u.%u)", wa.wCreatorVersion, nt->OptionalHeader.MajorSubsystemVersion, nt->OptionalHeader.MinorSubsystemVersion);
    U(DestroyWindow)(a); U(DestroyWindow)(b);
}

TEST(wi_borders_x_equals_y) {
    REQUIRE(U(GetWindowInfo)); REQUIRE(U(CreateWindowExW));
    static const struct { DWORD style, ex; } t[] = {
        { WS_POPUP, 0 }, { WS_BORDER, 0 }, { WS_DLGFRAME, 0 }, { WS_CAPTION, 0 }, { WS_THICKFRAME, 0 }, { WS_OVERLAPPEDWINDOW, 0 },
        { WS_POPUP, WS_EX_CLIENTEDGE }, { WS_POPUP, WS_EX_STATICEDGE }, { WS_POPUP, WS_EX_DLGMODALFRAME }, { WS_POPUP, WS_EX_WINDOWEDGE },
    };
    for (unsigned i = 0; i < sizeof t / sizeof *t; i++) {
        HWND h = mk(t[i].style, t[i].ex, 200, 200, 0); SIB_NEED_WND(h);
        WINDOWINFO wi;
        CHECK(wi_get(h, &wi));
        CHECKF(wi.cxWindowBorders == wi.cyWindowBorders, "row %u %u vs %u", i, wi.cxWindowBorders, wi.cyWindowBorders);
        U(DestroyWindow)(h);
    }
}

/* Border model from user32!GetWindowInfo for styles without WS_THICKFRAME and without a complete WS_CAPTION. */
static int simple_border(DWORD style, DWORD ex) {
    int e = (ex & WS_EX_WINDOWEDGE) ? 2 : ((ex & WS_EX_STATICEDGE) ? 1 : 0);
    if ((ex & WS_EX_DLGMODALFRAME) || (style & WS_CAPTION)) e++;
    return e;
}

TEST(wi_borders_simple_styles) {
    REQUIRE(U(GetWindowInfo)); REQUIRE(U(CreateWindowExW));
    static const struct { DWORD style, ex; } t[] = {
        { WS_POPUP, 0 }, { WS_POPUP | WS_BORDER, 0 }, { WS_POPUP | WS_DLGFRAME, 0 }, { WS_POPUP, WS_EX_DLGMODALFRAME },
        { WS_POPUP, WS_EX_STATICEDGE }, { WS_POPUP, WS_EX_WINDOWEDGE }, { WS_POPUP, WS_EX_CLIENTEDGE },
        { WS_POPUP | WS_BORDER, WS_EX_STATICEDGE }, { WS_POPUP, WS_EX_WINDOWEDGE | WS_EX_DLGMODALFRAME },
        { WS_POPUP | WS_DLGFRAME, WS_EX_STATICEDGE }, { WS_POPUP | WS_BORDER, WS_EX_CLIENTEDGE },
    };
    for (unsigned i = 0; i < sizeof t / sizeof *t; i++) {
        HWND h = mk(t[i].style, t[i].ex, 200, 200, 0); SIB_NEED_WND(h);
        WINDOWINFO wi;
        CHECK(wi_get(h, &wi));
        /* cxWindowBorders = edge(WINDOWEDGE 2 / STATICEDGE 1) + 1 if DLGMODALFRAME or any caption bit; CLIENTEDGE is not counted. */
        int want = simple_border(wi.dwStyle, wi.dwExStyle);
        CHECKF((int)wi.cxWindowBorders == want, "row %u style %#lx ex(reported) %#lx borders %u want %d", i, t[i].style, wi.dwExStyle, wi.cxWindowBorders, want);
        U(DestroyWindow)(h);
    }
}

TEST(wi_borders_literals) {
    REQUIRE(U(GetWindowInfo)); REQUIRE(U(CreateWindowExW));
    static const struct { DWORD style, ex; unsigned want; } t[] = {
        { WS_POPUP, 0, 0 }, { WS_POPUP | WS_BORDER, 0, 1 }, { WS_POPUP, WS_EX_STATICEDGE, 1 },
    };
    for (unsigned i = 0; i < sizeof t / sizeof *t; i++) {
        HWND h = mk(t[i].style, t[i].ex, 200, 200, 0); SIB_NEED_WND(h);
        WINDOWINFO wi;
        CHECK(wi_get(h, &wi));
        CHECKF(wi.cxWindowBorders == t[i].want, "row %u -> %u want %u", i, wi.cxWindowBorders, t[i].want);
        U(DestroyWindow)(h);
    }
}

TEST(wi_borders_thickframe_lower_bound) {
    REQUIRE(U(GetWindowInfo)); REQUIRE(U(CreateWindowExW));
    static const DWORD styles[] = { WS_POPUP | WS_THICKFRAME, WS_OVERLAPPEDWINDOW, WS_CAPTION | WS_THICKFRAME };
    for (unsigned i = 0; i < sizeof styles / sizeof *styles; i++) {
        HWND h = mk(styles[i], 0, 300, 300, 0); SIB_NEED_WND(h);
        WINDOWINFO wi;
        CHECK(wi_get(h, &wi));
        int padded = metric(SM_CXPADDEDBORDER);
        int base = simple_border(wi.dwStyle, wi.dwExStyle);
        /* WS_THICKFRAME adds (scaled resize width + SM_CXPADDEDBORDER) on top of the edge/caption part. */
        CHECKF((int)wi.cxWindowBorders >= base + padded, "style %#lx borders %u base %d padded %d", styles[i], wi.cxWindowBorders, base, padded);
        t_note("style %#lx cxWindowBorders %u (edge part %d, padded %d)", styles[i], wi.cxWindowBorders, base, padded);
        U(DestroyWindow)(h);
    }
}

TEST(wi_sysmetrics_frame_equalities) {
    REQUIRE(U(GetSystemMetrics));
    /* Both pairs are the same SERVERINFO value read through two metric indices. */
    EQ(U(GetSystemMetrics)(SM_CXFRAME), U(GetSystemMetrics)(SM_CXSIZEFRAME));
    EQ(U(GetSystemMetrics)(SM_CYFRAME), U(GetSystemMetrics)(SM_CYSIZEFRAME));
    EQ(U(GetSystemMetrics)(SM_CXFIXEDFRAME), U(GetSystemMetrics)(SM_CXDLGFRAME));
    EQ(U(GetSystemMetrics)(SM_CYFIXEDFRAME), U(GetSystemMetrics)(SM_CYDLGFRAME));
    EQ(U(GetSystemMetrics)(SM_CXMINSPACING) > 0, 1);
    CHECK(U(GetSystemMetrics)(SM_CXVSCROLL) > 0 && U(GetSystemMetrics)(SM_CYHSCROLL) > 0 && U(GetSystemMetrics)(SM_CYCAPTION) > 0);
    CHECK(U(GetSystemMetrics)(SM_CYSMCAPTION) > 0 && U(GetSystemMetrics)(SM_CYSMCAPTION) <= U(GetSystemMetrics)(SM_CYCAPTION));
}

/* ---- AdjustWindowRect --------------------------------------------------------------------- */

static void adj(RECT *r, DWORD st, BOOL menu, DWORD ex) { RECT in = { 100, 200, 500, 600 }; *r = in; CHECK(U(AdjustWindowRectEx)(r, st, menu, ex)); }

TEST(adj_returns_true_and_symmetric) {
    REQUIRE(U(AdjustWindowRectEx));
    static const DWORD styles[] = { 0, WS_POPUP, WS_BORDER, WS_DLGFRAME, WS_CAPTION, WS_THICKFRAME, WS_OVERLAPPEDWINDOW, WS_POPUP | WS_THICKFRAME,
                                    WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_HSCROLL, WS_CAPTION | WS_SYSMENU };
    static const DWORD exs[] = { 0, WS_EX_CLIENTEDGE, WS_EX_TOOLWINDOW, WS_EX_DLGMODALFRAME, WS_EX_STATICEDGE, WS_EX_WINDOWEDGE, WS_EX_APPWINDOW };
    for (unsigned i = 0; i < sizeof styles / sizeof *styles; i++) for (unsigned j = 0; j < sizeof exs / sizeof *exs; j++) for (int m = 0; m < 2; m++) {
        RECT r; adj(&r, styles[i], m, exs[j]);
        int b = 100 - r.left;
        /* Left, right and bottom grow by the same border width; only the top also gets caption and menu. */
        CHECKF(b >= 0 && r.right - 500 == b && r.bottom - 600 == b, "style %#lx ex %#lx menu %d: l %ld r %ld b %ld", styles[i], exs[j], m, r.left - 100, r.right - 500, r.bottom - 600);
        CHECK(r.top <= 200 - b);
    }
}

TEST(adj_menu_adds_cymenu) {
    REQUIRE(U(AdjustWindowRectEx));
    static const DWORD styles[] = { 0, WS_POPUP, WS_CAPTION, WS_OVERLAPPEDWINDOW, WS_BORDER, WS_THICKFRAME };
    for (unsigned i = 0; i < sizeof styles / sizeof *styles; i++) {
        RECT a, b; adj(&a, styles[i], FALSE, 0); adj(&b, styles[i], TRUE, 0);
        /* _AdjustWindowRectExForDpi: bMenu => top -= GetSystemMetricsForDpi(SM_CYMENU, systemDpi), independent of the style. */
        CHECKF(a.top - b.top == metric(SM_CYMENU), "style %#lx delta %ld cymenu %d", styles[i], a.top - b.top, metric(SM_CYMENU));
        CHECK(a.left == b.left && a.right == b.right && a.bottom == b.bottom);
    }
}

TEST(adj_caption_height) {
    REQUIRE(U(AdjustWindowRectEx));
    RECT none, cap, tool_none, tool_cap;
    adj(&none, WS_THICKFRAME, FALSE, 0); adj(&cap, WS_THICKFRAME | WS_CAPTION, FALSE, 0);
    /* Differences between two styles also change the border, so isolate the caption term with the same border bits: WS_CAPTION =
     * WS_BORDER|WS_DLGFRAME and the border part is the same for "caption" vs "dlgframe|border" - i.e. identical style values. */
    RECT a, b;
    adj(&a, WS_CAPTION, FALSE, 0);
    adj(&b, WS_CAPTION, FALSE, WS_EX_TOOLWINDOW);
    /* Tool windows use SM_CYSMCAPTION (index 0x33) instead of SM_CYCAPTION (4); borders do not depend on WS_EX_TOOLWINDOW. */
    CHECKF(b.top - a.top == metric(SM_CYCAPTION) - metric(SM_CYSMCAPTION), "tool %ld normal %ld", b.top, a.top);
    CHECK(a.left == b.left && a.right == b.right && a.bottom == b.bottom);
    (void)none; (void)cap; (void)tool_none; (void)tool_cap;
    /* Without the full 0xC00000 mask no caption is added: WS_BORDER and WS_DLGFRAME alone leave the top equal to the other sides. */
    RECT bd, df;
    adj(&bd, WS_BORDER, FALSE, 0); adj(&df, WS_DLGFRAME, FALSE, 0);
    CHECK(100 - bd.left == 200 - bd.top);
    CHECK(100 - df.left == 200 - df.top);
    /* Full caption: top grows by SM_CYCAPTION beyond the side border. */
    CHECKF((100 - a.left) + metric(SM_CYCAPTION) == 200 - a.top, "caption top inflate %ld side %ld cycaption %d", 200 - a.top, 100 - a.left, metric(SM_CYCAPTION));
}

TEST(adj_literal_borders) {
    REQUIRE(U(AdjustWindowRectEx));
    static const struct { DWORD style, ex; int want; } t[] = {
        { WS_POPUP, 0, 0 }, { WS_POPUP | WS_BORDER, 0, 1 }, { WS_POPUP | WS_DLGFRAME, 0, 3 },
        { WS_POPUP, WS_EX_DLGMODALFRAME, 3 }, { WS_POPUP, WS_EX_STATICEDGE, 1 }, { WS_POPUP, WS_EX_CLIENTEDGE, 2 },
        { WS_POPUP, WS_EX_WINDOWEDGE, 0 },                       /* WS_EX_WINDOWEDGE is dropped unless implied by other bits */
        { WS_POPUP | WS_BORDER, WS_EX_CLIENTEDGE, 3 }, { WS_POPUP, WS_EX_STATICEDGE | WS_EX_CLIENTEDGE, 3 },
        { WS_POPUP | WS_DLGFRAME, WS_EX_STATICEDGE, 2 },         /* STATICEDGE suppresses the implied WINDOWEDGE */
    };
    for (unsigned i = 0; i < sizeof t / sizeof *t; i++) {
        RECT r; adj(&r, t[i].style, FALSE, t[i].ex);
        /* GetWindowBordersForDpiWithCompatFlags2 with bIncludeClientEdge: 2/1 edge + 1 for DLGMODALFRAME|caption bits + 2 for CLIENTEDGE. */
        CHECKF(100 - r.left == t[i].want && 200 - r.top == t[i].want, "row %u style %#lx ex %#lx border %ld top %ld want %d", i, t[i].style, t[i].ex, 100 - r.left, 200 - r.top, t[i].want);
    }
}

TEST(adj_scrollbar_styles_ignored) {
    REQUIRE(U(AdjustWindowRectEx));
    static const DWORD styles[] = { WS_POPUP, WS_CAPTION, WS_OVERLAPPEDWINDOW };
    for (unsigned i = 0; i < sizeof styles / sizeof *styles; i++) {
        RECT a, b, c;
        adj(&a, styles[i], FALSE, 0);
        adj(&b, styles[i] | WS_VSCROLL, FALSE, 0);
        adj(&c, styles[i] | WS_HSCROLL | WS_VSCROLL, FALSE, 0);
        /* The routine never looks at WS_HSCROLL/WS_VSCROLL (documented: scroll bars are not accounted for). */
        CHECK(!memcmp(&a, &b, sizeof a) && !memcmp(&a, &c, sizeof a));
    }
}

TEST(adj_thickframe_matches_sizeframe) {
    REQUIRE(U(AdjustWindowRectEx));
    RECT r; adj(&r, WS_OVERLAPPEDWINDOW, FALSE, 0);
    int side = 100 - r.left;
    /* A resizable captioned frame is SM_CXSIZEFRAME plus the invisible SM_CXPADDEDBORDER (Windows 10). */
    int want = metric(SM_CXSIZEFRAME) + metric(SM_CXPADDEDBORDER);
    if (sysdpi() == 96) CHECKF(side == want, "side %d want %d (frame %d + padded %d)", side, want, metric(SM_CXSIZEFRAME), metric(SM_CXPADDEDBORDER));
    else if (side != want) t_note("overlapped frame side %d, metrics say %d", side, want);
    CHECK(side >= metric(SM_CXSIZEFRAME));
    CHECKF(200 - r.top == side + metric(SM_CYCAPTION), "top %ld", 200 - r.top);
}

TEST(adj_forDpi_matches_default) {
    GSMFD_fn m = gsmfd();
    ADJFD_fn f = (ADJFD_fn)t_proc("AdjustWindowRectExForDpi");
    REQUIRE(f); REQUIRE(m); REQUIRE(U(AdjustWindowRectEx));
    static const DWORD styles[] = { WS_POPUP, WS_BORDER, WS_CAPTION, WS_OVERLAPPEDWINDOW };
    for (unsigned i = 0; i < sizeof styles / sizeof *styles; i++) for (int menu = 0; menu < 2; menu++) {
        RECT a, b = { 100, 200, 500, 600 };
        adj(&a, styles[i], menu, 0);
        CHECK(f(&b, styles[i], menu, 0, sysdpi()));
        CHECK(!memcmp(&a, &b, sizeof a));
    }
}

TEST(adj_forDpi_scales_metrics) {
    GSMFD_fn m = gsmfd();
    ADJFD_fn f = (ADJFD_fn)t_proc("AdjustWindowRectExForDpi");
    REQUIRE(f); REQUIRE(m);
    static const UINT dpis[] = { 96, 120, 144, 192 };
    for (unsigned i = 0; i < 4; i++) {
        RECT r = { 100, 200, 500, 600 };
        SET_ERR(0);
        CHECK(f(&r, WS_POPUP, TRUE, 0, dpis[i]));
        /* Menu height comes from GetSystemMetricsForDpi(SM_CYMENU, dpi); a plain popup has no borders. */
        CHECKF(200 - r.top == m(SM_CYMENU, dpis[i]), "dpi %u: %ld vs %d", dpis[i], 200 - r.top, m(SM_CYMENU, dpis[i]));
        CHECK(r.left == 100 && r.right == 500 && r.bottom == 600);
        RECT c = { 100, 200, 500, 600 };
        CHECK(f(&c, WS_CAPTION, FALSE, 0, dpis[i]));
        int side = 100 - c.left;
        CHECKF(200 - c.top == side + m(SM_CYCAPTION, dpis[i]), "dpi %u caption top %ld side %d cy %d", dpis[i], 200 - c.top, side, m(SM_CYCAPTION, dpis[i]));
    }
}

TEST(adj_forDpi_zero_dpi) {
    ADJFD_fn f = (ADJFD_fn)t_proc("AdjustWindowRectExForDpi");
    REQUIRE(f);
    RECT r = { 1, 2, 3, 4 };
    SET_ERR(0);
    /* user32!AdjustWindowRectExForDpi: dpi == 0 => ERROR_INVALID_PARAMETER, FALSE, rectangle untouched. */
    CHECK(!f(&r, WS_OVERLAPPEDWINDOW, TRUE, 0, 0));
    EXPECT_ERR(E_INVALID_PARAM);
    CHECK(r.left == 1 && r.top == 2 && r.right == 3 && r.bottom == 4);
}

TEST(adj_adjustwindowrect_equals_ex_zero) {
    REQUIRE(U(AdjustWindowRect)); REQUIRE(U(AdjustWindowRectEx));
    static const DWORD styles[] = { WS_POPUP, WS_CAPTION, WS_OVERLAPPEDWINDOW, WS_BORDER | WS_DLGFRAME };
    for (unsigned i = 0; i < sizeof styles / sizeof *styles; i++) for (int menu = 0; menu < 2; menu++) {
        RECT a = { 10, 20, 410, 320 }, b = { 10, 20, 410, 320 };
        CHECK(U(AdjustWindowRect)(&a, styles[i], menu));
        CHECK(U(AdjustWindowRectEx)(&b, styles[i], menu, 0));
        CHECK(!memcmp(&a, &b, sizeof a));
    }
}

TEST(adj_empty_and_negative_rects) {
    REQUIRE(U(AdjustWindowRectEx));
    RECT r = { 0, 0, 0, 0 };
    CHECK(U(AdjustWindowRectEx)(&r, WS_OVERLAPPEDWINDOW, TRUE, 0));
    RECT s = { 0, 0, 0, 0 };
    CHECK(U(AdjustWindowRectEx)(&s, WS_POPUP, FALSE, 0));
    CHECK(s.left == 0 && s.top == 0 && s.right == 0 && s.bottom == 0);
    /* Arithmetic is pure inflation: a negative-size input stays negative-size by the same border amounts. */
    RECT n = { 50, 50, 10, 10 };
    CHECK(U(AdjustWindowRectEx)(&n, WS_POPUP | WS_BORDER, FALSE, 0));
    CHECK(n.left == 49 && n.top == 49 && n.right == 11 && n.bottom == 11);
    int b = -r.left;
    CHECK(r.right == b && r.bottom == b);
}

/* ---- created window vs AdjustWindowRectEx -------------------------------------------------- */

static const struct { DWORD style, ex; } real_tab[] = {
    { WS_POPUP, 0 }, { WS_POPUP | WS_BORDER, 0 }, { WS_POPUP | WS_DLGFRAME, 0 }, { WS_CAPTION, 0 }, { WS_CAPTION | WS_SYSMENU, 0 },
    { WS_OVERLAPPEDWINDOW, 0 }, { WS_POPUP, WS_EX_CLIENTEDGE }, { WS_CAPTION, WS_EX_TOOLWINDOW },
    { WS_CAPTION, WS_EX_DLGMODALFRAME }, { WS_POPUP, WS_EX_STATICEDGE }, { WS_OVERLAPPEDWINDOW, WS_EX_CLIENTEDGE },
    { WS_POPUP | WS_THICKFRAME, 0 },
};

TEST(adj_roundtrip_with_real_window) {
    REQUIRE(U(AdjustWindowRectEx)); REQUIRE(U(GetClientRect)); REQUIRE(U(CreateWindowExW));
    for (unsigned i = 0; i < sizeof real_tab / sizeof *real_tab; i++) {
        RECT r = { 0, 0, 400, 300 };
        CHECK(U(AdjustWindowRectEx)(&r, real_tab[i].style, FALSE, real_tab[i].ex));
        HWND h = mk(real_tab[i].style, real_tab[i].ex, r.right - r.left, r.bottom - r.top, 1); SIB_NEED_WND(h);
        RECT c; CHECK(U(GetClientRect)(h, &c));
        /* Documented contract of AdjustWindowRectEx: a window of the adjusted size has the wanted client size. */
        CHECKF(c.right == 400 && c.bottom == 300, "row %u style %#lx ex %#lx client %ldx%ld", i, real_tab[i].style, real_tab[i].ex, c.right, c.bottom);
        U(DestroyWindow)(h);
    }
}

TEST(adj_roundtrip_real_window_matches_rects) {
    REQUIRE(U(AdjustWindowRectEx)); REQUIRE(U(GetWindowInfo)); REQUIRE(U(CreateWindowExW));
    for (unsigned i = 0; i < sizeof real_tab / sizeof *real_tab; i++) {
        RECT r = { 0, 0, 400, 300 };
        CHECK(U(AdjustWindowRectEx)(&r, real_tab[i].style, FALSE, real_tab[i].ex));
        HWND h = mk(real_tab[i].style, real_tab[i].ex, r.right - r.left, r.bottom - r.top, 1); SIB_NEED_WND(h);
        WINDOWINFO wi; CHECK(wi_get(h, &wi));
        /* The inset of the desktop-heap rcClient inside rcWindow equals the adjustment AdjustWindowRectEx predicted. */
        CHECKF(wi.rcClient.left - wi.rcWindow.left == -r.left && wi.rcClient.top - wi.rcWindow.top == -r.top, "row %u left %ld/%ld top %ld/%ld", i,
               wi.rcClient.left - wi.rcWindow.left, -r.left, wi.rcClient.top - wi.rcWindow.top, -r.top);
        CHECK(wi.rcWindow.right - wi.rcClient.right == r.right - 400 && wi.rcWindow.bottom - wi.rcClient.bottom == r.bottom - 300);
        U(DestroyWindow)(h);
    }
}

TEST(adj_real_window_with_scrollbar_styles) {
    REQUIRE(U(AdjustWindowRectEx)); REQUIRE(U(GetClientRect)); REQUIRE(U(CreateWindowExW));
    static const struct { DWORD bits; int dx, dy; } t[] = { { WS_VSCROLL, 1, 0 }, { WS_HSCROLL, 0, 1 }, { WS_VSCROLL | WS_HSCROLL, 1, 1 } };
    for (unsigned i = 0; i < 3; i++) {
        RECT r = { 0, 0, 400, 300 };
        CHECK(U(AdjustWindowRectEx)(&r, WS_CAPTION | t[i].bits, FALSE, 0));
        HWND h = mk(WS_CAPTION | t[i].bits, 0, r.right - r.left, r.bottom - r.top, 1); SIB_NEED_WND(h);
        RECT c; CHECK(U(GetClientRect)(h, &c));
        /* The adjusted size ignores scroll bars, so the kernel's non-client calculation takes them out of the client area. */

        if (sysdpi() == 96) {
            CHECKF(c.right == 400 - t[i].dx * U(GetSystemMetrics)(SM_CXVSCROLL), "row %u cx %ld", i, c.right);
            CHECKF(c.bottom == 300 - t[i].dy * U(GetSystemMetrics)(SM_CYHSCROLL), "row %u cy %ld", i, c.bottom);
        } else t_note("row %u client %ldx%ld at system dpi %u", i, c.right, c.bottom, sysdpi());
        U(DestroyWindow)(h);
    }
}

/* ---- WM_NCCALCSIZE ---------------------------------------------------------------------- */

static struct {
    int mode;                    /* 0 pass through, 1 shrink client by 10 on each side */
    int calls, last_wparam;
    int saw_params;
    RECT r0, r1, r2, out;
    WINDOWPOS wp;
    LRESULT lret;
} g_nc;

static LRESULT CALLBACK nc_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_NCCALCSIZE) {
        g_nc.calls++;
        g_nc.last_wparam = (int)w;
        if (w && l) {
            NCCALCSIZE_PARAMS *p = (NCCALCSIZE_PARAMS *)l;
            g_nc.saw_params = 1;
            g_nc.r0 = p->rgrc[0]; g_nc.r1 = p->rgrc[1]; g_nc.r2 = p->rgrc[2];
            if (p->lppos) g_nc.wp = *p->lppos;
            LRESULT r = DefWindowProcW(h, m, w, l);
            if (g_nc.mode == 1) { p->rgrc[0].left += 10; p->rgrc[0].top += 10; p->rgrc[0].right -= 10; p->rgrc[0].bottom -= 10; }
            g_nc.out = p->rgrc[0];
            g_nc.lret = r;
            return r;
        }
    }
    return DefWindowProcW(h, m, w, l);
}
static const wchar_t *nc_class(void) {
    static wchar_t name[48];
    static int done;
    if (!done) {
        WNDCLASSEXW wc = { sizeof wc };
        swprintf(name, 48, L"win32kt_sib_nccalc_%lu", GetCurrentProcessId());
        wc.lpfnWndProc = nc_proc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.lpszClassName = name;
        if (U(RegisterClassExW)(&wc)) done = 1;
    }
    return done ? name : NULL;
}
static HWND mk_nc(DWORD style, DWORD ex, int w, int h, int vis) {
    if (!nc_class()) return NULL;
    memset(&g_nc, 0, sizeof g_nc);
    HWND hw = U(CreateWindowExW)(ex | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, nc_class(), L"win32kt_nc", style & ~WS_VISIBLE, -30000, -30000, w, h,
                                 NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (hw && vis) U(ShowWindow)(hw, SW_SHOWNOACTIVATE);
    return hw;
}

TEST(nc_default_proc_reproduces_rcclient) {
    REQUIRE(U(GetWindowInfo)); REQUIRE(U(CreateWindowExW));
    static const struct { DWORD style, ex; } t[] = {
        { WS_OVERLAPPEDWINDOW, 0 }, { WS_POPUP, 0 }, { WS_POPUP | WS_BORDER, 0 }, { WS_CAPTION | WS_SYSMENU, 0 }, { WS_OVERLAPPED | WS_VSCROLL | WS_HSCROLL, 0 },
        { WS_POPUP, WS_EX_CLIENTEDGE }, { WS_CAPTION, WS_EX_TOOLWINDOW }, { WS_THICKFRAME, 0 },
    };
    for (unsigned i = 0; i < sizeof t / sizeof *t; i++) {
        HWND h = mk_nc(t[i].style, t[i].ex, 300, 250, 1); SIB_NEED_WND(h);
        WINDOWINFO wi; CHECK(wi_get(h, &wi));
        RECT rc = wi.rcWindow;
        /* DefWindowProc(WM_NCCALCSIZE, FALSE, &rcWindow) runs the same client-rect calculation the kernel used. */
        LRESULT r = DefWindowProcW(h, WM_NCCALCSIZE, FALSE, (LPARAM)&rc);
        CHECK(r == 0);
        CHECKF(!memcmp(&rc, &wi.rcClient, sizeof rc), "row %u calc %ld,%ld,%ld,%ld vs rcClient %ld,%ld,%ld,%ld", i, rc.left, rc.top, rc.right, rc.bottom,
               wi.rcClient.left, wi.rcClient.top, wi.rcClient.right, wi.rcClient.bottom);
        U(DestroyWindow)(h);
    }
}

TEST(nc_menu_bar_included) {
    REQUIRE(U(GetWindowInfo)); REQUIRE(U(CreateWindowExW)); REQUIRE(U(CreateMenu)); REQUIRE(U(AppendMenuW)); REQUIRE(U(SetMenu));
    HWND h = mk_nc(WS_OVERLAPPEDWINDOW, 0, 400, 300, 1); SIB_NEED_WND(h);
    WINDOWINFO before, after; CHECK(wi_get(h, &before));
    HMENU m = U(CreateMenu)();
    U(AppendMenuW)(m, MF_STRING, 1, L"File");
    U(AppendMenuW)(m, MF_STRING, 2, L"Edit");
    CHECK(U(SetMenu)(h, m));
    CHECK(wi_get(h, &after));
    /* Installing a menu bar re-runs the frame calculation: client top moves down, bottom and sides stay. */
    int dy = after.rcClient.top - before.rcClient.top;
    CHECKF(dy > 0, "client top moved %d", dy);
    CHECK(after.rcClient.left == before.rcClient.left && after.rcClient.right == before.rcClient.right && after.rcClient.bottom == before.rcClient.bottom);
    CHECK(!memcmp(&after.rcWindow, &before.rcWindow, sizeof(RECT)));
    /* A single-row bar is SM_CYMENU high (the menu text fits the window width). */
    t_note("menu bar consumed %d px, SM_CYMENU %d", dy, U(GetSystemMetrics)(SM_CYMENU));
    RECT rc = after.rcWindow;
    DefWindowProcW(h, WM_NCCALCSIZE, FALSE, (LPARAM)&rc);
    CHECK(!memcmp(&rc, &after.rcClient, sizeof rc));
    U(SetMenu)(h, NULL);
    U(DestroyMenu)(m);
    U(DestroyWindow)(h);
}

TEST(nc_framechanged_delivers_params) {
    REQUIRE(U(GetWindowInfo)); REQUIRE(U(SetWindowPos)); REQUIRE(U(CreateWindowExW));
    HWND h = mk_nc(WS_OVERLAPPEDWINDOW, 0, 300, 250, 1); SIB_NEED_WND(h);
    WINDOWINFO wi; CHECK(wi_get(h, &wi));
    memset(&g_nc, 0, sizeof g_nc);
    CHECK(U(SetWindowPos)(h, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE));
    /* SetWindowPos sends WM_NCCALCSIZE with wParam TRUE and an NCCALCSIZE_PARAMS in user memory. */
    CHECKF(g_nc.calls >= 1 && g_nc.saw_params && g_nc.last_wparam == 1, "calls %d params %d wparam %d", g_nc.calls, g_nc.saw_params, g_nc.last_wparam);
    CHECK(!memcmp(&g_nc.r0, &wi.rcWindow, sizeof(RECT)));          /* new window rectangle */
    CHECK(!memcmp(&g_nc.r1, &wi.rcWindow, sizeof(RECT)));          /* old window rectangle */
    CHECK(!memcmp(&g_nc.r2, &wi.rcClient, sizeof(RECT)));          /* old client rectangle */
    CHECK(g_nc.wp.hwnd == h);
    t_note("lppos for NOMOVE|NOSIZE frame change: x=%d y=%d cx=%d cy=%d flags=%#x", g_nc.wp.x, g_nc.wp.y, g_nc.wp.cx, g_nc.wp.cy, g_nc.wp.flags);
    CHECK(g_nc.wp.flags & SWP_FRAMECHANGED);
    WINDOWINFO after; CHECK(wi_get(h, &after));
    CHECK(!memcmp(&after.rcClient, &g_nc.out, sizeof(RECT)));      /* passthrough keeps rcClient */
    CHECK(!memcmp(&after.rcClient, &wi.rcClient, sizeof(RECT)));
    U(DestroyWindow)(h);
}

TEST(nc_resize_params_old_and_new) {
    REQUIRE(U(GetWindowInfo)); REQUIRE(U(SetWindowPos)); REQUIRE(U(CreateWindowExW));
    HWND h = mk_nc(WS_OVERLAPPEDWINDOW, 0, 300, 250, 1); SIB_NEED_WND(h);
    WINDOWINFO old; CHECK(wi_get(h, &old));
    memset(&g_nc, 0, sizeof g_nc);
    CHECK(U(SetWindowPos)(h, NULL, -25000, -24000, 420, 310, SWP_NOZORDER | SWP_NOACTIVATE));
    CHECKF(g_nc.saw_params && g_nc.last_wparam == 1, "params %d wparam %d", g_nc.saw_params, g_nc.last_wparam);
    RECT neww = { -25000, -24000, -25000 + 420, -24000 + 310 };
    CHECK(!memcmp(&g_nc.r0, &neww, sizeof neww));
    CHECK(!memcmp(&g_nc.r1, &old.rcWindow, sizeof(RECT)));
    CHECK(!memcmp(&g_nc.r2, &old.rcClient, sizeof(RECT)));
    WINDOWINFO nw; CHECK(wi_get(h, &nw));
    CHECK(!memcmp(&nw.rcWindow, &neww, sizeof neww));
    CHECK(!memcmp(&nw.rcClient, &g_nc.out, sizeof(RECT)));
    /* Frame sizes are unchanged by a move/resize: the insets stay what they were. */
    CHECK(nw.rcClient.left - nw.rcWindow.left == old.rcClient.left - old.rcWindow.left);
    CHECK(nw.rcClient.top - nw.rcWindow.top == old.rcClient.top - old.rcWindow.top);
    CHECK(nw.rcWindow.right - nw.rcClient.right == old.rcWindow.right - old.rcClient.right);
    U(DestroyWindow)(h);
}

TEST(nc_proc_result_becomes_rcclient) {
    REQUIRE(U(GetWindowInfo)); REQUIRE(U(SetWindowPos)); REQUIRE(U(GetClientRect)); REQUIRE(U(CreateWindowExW));
    HWND h = mk_nc(WS_POPUP | WS_BORDER, 0, 300, 250, 1); SIB_NEED_WND(h);
    WINDOWINFO base; CHECK(wi_get(h, &base));
    g_nc.mode = 1;                    /* handler shrinks the DefWindowProc result by 10 px per side */
    CHECK(U(SetWindowPos)(h, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE));
    WINDOWINFO wi; CHECK(wi_get(h, &wi));
    /* What the window procedure leaves in rgrc[0] is what the kernel stores as tagWND.rcClient. */
    RECT want = { base.rcClient.left + 10, base.rcClient.top + 10, base.rcClient.right - 10, base.rcClient.bottom - 10 };
    CHECK(!memcmp(&wi.rcClient, &want, sizeof want));
    CHECK(!memcmp(&wi.rcWindow, &base.rcWindow, sizeof(RECT)));
    RECT c; CHECK(U(GetClientRect)(h, &c));
    CHECK(c.left == 0 && c.top == 0 && c.right == want.right - want.left && c.bottom == want.bottom - want.top);
    /* Back to passthrough restores the original client rectangle. */
    g_nc.mode = 0;
    CHECK(U(SetWindowPos)(h, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE));
    CHECK(wi_get(h, &wi) && !memcmp(&wi.rcClient, &base.rcClient, sizeof(RECT)));
    U(DestroyWindow)(h);
}

TEST(nc_client_origin_follows_move) {
    REQUIRE(U(GetWindowInfo)); REQUIRE(U(SetWindowPos)); REQUIRE(U(ClientToScreen)); REQUIRE(U(CreateWindowExW));
    HWND h = mk_nc(WS_CAPTION | WS_THICKFRAME, 0, 300, 250, 1); SIB_NEED_WND(h);
    WINDOWINFO a, b;
    CHECK(wi_get(h, &a));
    CHECK(U(SetWindowPos)(h, NULL, -22000, -23000, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE));
    CHECK(wi_get(h, &b));
    CHECK(b.rcWindow.left - a.rcWindow.left == -22000 - a.rcWindow.left);
    /* A pure move translates the client rectangle by exactly the same amount. */
    CHECK(b.rcClient.left - a.rcClient.left == b.rcWindow.left - a.rcWindow.left);
    CHECK(b.rcClient.top - a.rcClient.top == b.rcWindow.top - a.rcWindow.top);
    POINT o = { 0, 0 };
    CHECK(U(ClientToScreen)(h, &o) && o.x == b.rcClient.left && o.y == b.rcClient.top);
    U(DestroyWindow)(h);
}

TEST(nc_wparam_false_leaves_no_state) {
    REQUIRE(U(GetWindowInfo)); REQUIRE(U(CreateWindowExW));
    HWND h = mk_nc(WS_OVERLAPPEDWINDOW, 0, 300, 250, 1); SIB_NEED_WND(h);
    WINDOWINFO a, b; CHECK(wi_get(h, &a));
    RECT bogus = { 5, 6, 905, 806 };
    /* Sending the message by hand only asks for a computation: tagWND is not modified. */
    DefWindowProcW(h, WM_NCCALCSIZE, FALSE, (LPARAM)&bogus);
    CHECK(wi_get(h, &b));
    CHECK(!memcmp(&a, &b, sizeof a));
    CHECK(bogus.right - bogus.left < 900);     /* but the rectangle was inflated inward */
    U(DestroyWindow)(h);
}
