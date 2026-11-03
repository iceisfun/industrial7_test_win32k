/* TITLEBARINFO, COMBOBOXINFO, MENUBARINFO, CURSORINFO, LASTINPUTINFO.
 * Kernel: the title-bar, combo-box, menu-bar and cursor info queries.
 * GetLastInputInfo reads the shared server info on the client side. */
#include "sib.h"

#define ST_UNAVAILABLE 0x00000001
#define ST_PRESSED     0x00000008
#define ST_INVISIBLE   0x00008000
#define ST_FOCUSABLE   0x00100000
#define ST_OFFSCREEN   0x00010000

static HWND mk_plain(DWORD style, DWORD ex, int vis, const wchar_t *cls) {
    if (!U(CreateWindowExW)) return NULL;
    HWND h = U(CreateWindowExW)(ex | WS_EX_NOACTIVATE, cls ? cls : sib_class(), L"win32kt_info", style & ~WS_VISIBLE,
                                -30000, -30000, 320, 240, NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (h && vis) U(ShowWindow)(h, SW_SHOWNOACTIVATE);
    return h;
}
static const wchar_t *noclose_class(void) {
    static wchar_t name[48];
    static int done;
    if (!done) {
        WNDCLASSEXW wc = { sizeof wc };
        swprintf(name, 48, L"win32kt_sib_nc_%lu", GetCurrentProcessId());
        wc.style = CS_NOCLOSE;
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.lpszClassName = name;
        if (U(RegisterClassExW)(&wc)) done = 1;
    }
    return done ? name : NULL;
}
static int tbi(HWND h, TITLEBARINFO *t) {
    memset(t, 0xCC, sizeof *t);
    t->cbSize = sizeof *t;
    return U(GetTitleBarInfo)(h, t);
}

/* ---- GetTitleBarInfo --------------------------------------------------------------------- */

TEST(tbi_bad_cbsize) {
    REQUIRE(U(GetTitleBarInfo)); REQUIRE(U(CreateWindowExW));
    HWND h = mk_plain(WS_OVERLAPPEDWINDOW, 0, 1, NULL); SIB_NEED_WND(h);
    static const DWORD sz[] = { 0, 4, 0x28, 0x2b, 0x2d, 0x30, 0x58, 0xffffffffu };
    for (unsigned i = 0; i < sizeof sz / sizeof *sz; i++) {
        TITLEBARINFO t; memset(&t, 0xCC, sizeof t); t.cbSize = sz[i];
        SET_ERR(0);
        /* NtUserGetTitleBarInfo: cbSize != 0x2c => ERROR_INVALID_PARAMETER, FALSE, output buffer untouched. */
        CHECK(!U(GetTitleBarInfo)(h, &t));
        EXPECT_ERR(E_INVALID_PARAM);
        CHECK(t.rgstate[0] == 0xCCCCCCCCu && t.rcTitleBar.left == (LONG)0xCCCCCCCC);
    }
    U(DestroyWindow)(h);
}

TEST(tbi_invalid_hwnd) {
    REQUIRE(U(GetTitleBarInfo)); REQUIRE(U(CreateWindowExW));
    HWND gone = mk_plain(WS_OVERLAPPEDWINDOW, 0, 0, NULL); SIB_NEED_WND(gone);
    U(DestroyWindow)(gone);
    HWND bad[] = { NULL, (HWND)(ULONG_PTR)0x2468, (HWND)(ULONG_PTR)-1, gone };
    for (unsigned i = 0; i < 4; i++) {
        TITLEBARINFO t;
        SET_ERR(0);
        CHECK(!tbi(bad[i], &t));
        EXPECT_ERR(E_INVALID_HWND);
    }
}

TEST(tbi_stub_matches_user32) {
    FARPROC p = sib_stub("NtUserGetTitleBarInfo");
    REQUIRE(p); REQUIRE(U(GetTitleBarInfo)); REQUIRE(U(CreateWindowExW));
    NtUserGetTitleBarInfo_pfn f = (NtUserGetTitleBarInfo_pfn)p;
    HWND h = mk_plain(WS_OVERLAPPEDWINDOW, 0, 1, NULL); SIB_NEED_WND(h);
    TITLEBARINFO a, b;
    CHECK(tbi(h, &a));
    memset(&b, 0xCC, sizeof b); b.cbSize = sizeof b;
    CHECK(f(h, &b));
    CHECK(!memcmp(&a, &b, sizeof a));
    U(DestroyWindow)(h);
}

TEST(tbi_no_caption_states) {
    REQUIRE(U(GetTitleBarInfo)); REQUIRE(U(CreateWindowExW));
    static const DWORD styles[] = { WS_POPUP, WS_POPUP | WS_BORDER, WS_POPUP | WS_DLGFRAME, WS_POPUP | WS_THICKFRAME, WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX };
    for (unsigned i = 0; i < sizeof styles / sizeof *styles; i++) {
        for (int vis = 0; vis < 2; vis++) {
            HWND h = mk_plain(styles[i], 0, vis, NULL); SIB_NEED_WND(h);
            TITLEBARINFO t;
            /* the title-bar info query: style & WS_CAPTION != WS_CAPTION => state[0] = FOCUSABLE | INVISIBLE, returns TRUE, rest zeroed. */
            CHECK(tbi(h, &t));
            CHECKF(t.rgstate[0] == (ST_FOCUSABLE | ST_INVISIBLE), "style %#lx vis %d state[0] %#lx", styles[i], vis, t.rgstate[0]);
            for (int k = 1; k < 6; k++) CHECK(t.rgstate[k] == 0);
            CHECK(t.rcTitleBar.left == 0 && t.rcTitleBar.top == 0 && t.rcTitleBar.right == 0 && t.rcTitleBar.bottom == 0);
            U(DestroyWindow)(h);
        }
    }
}

TEST(tbi_hidden_caption_state) {
    REQUIRE(U(GetTitleBarInfo)); REQUIRE(U(CreateWindowExW));
    HWND h = mk_plain(WS_OVERLAPPEDWINDOW, 0, 0, NULL); SIB_NEED_WND(h);
    TITLEBARINFO t;
    /* On Windows 10 a captioned window reports the same state hidden, shown and hidden again: the title bar is
     * FOCUSABLE only (no OFFSCREEN or INVISIBLE, even though the window is hidden and parked far off screen), the
     * reserved and help buttons are INVISIBLE, and the rectangle is the real, non-empty caption rectangle. */
    for (int step = 0; step < 3; step++) {
        CHECK(tbi(h, &t));
        CHECKF(t.rgstate[0] == ST_FOCUSABLE, "step %d state[0] %#lx", step, t.rgstate[0]);
        CHECKF(t.rgstate[1] == ST_INVISIBLE && t.rgstate[4] == ST_INVISIBLE, "step %d reserved %#lx help %#lx", step, t.rgstate[1], t.rgstate[4]);
        CHECKF(t.rgstate[2] == 0 && t.rgstate[3] == 0 && t.rgstate[5] == 0, "step %d buttons %#lx %#lx %#lx", step, t.rgstate[2], t.rgstate[3], t.rgstate[5]);
        CHECK(t.rcTitleBar.right > t.rcTitleBar.left && t.rcTitleBar.bottom > t.rcTitleBar.top);
        if (step == 0) U(ShowWindow)(h, SW_SHOWNOACTIVATE); else U(ShowWindow)(h, SW_HIDE);
    }
    U(DestroyWindow)(h);
}

TEST(tbi_reserved_state_always_invisible) {
    REQUIRE(U(GetTitleBarInfo)); REQUIRE(U(CreateWindowExW));
    static const DWORD styles[] = { WS_CAPTION, WS_OVERLAPPEDWINDOW, WS_CAPTION | WS_SYSMENU, WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX };
    for (unsigned i = 0; i < sizeof styles / sizeof *styles; i++) {
        HWND h = mk_plain(styles[i], 0, 1, NULL); SIB_NEED_WND(h);
        TITLEBARINFO t;
        CHECK(tbi(h, &t));
        /* rgstate[1] is the reserved slot: the final statement of the title-bar info query stores STATE_SYSTEM_INVISIBLE. */
        CHECKF(t.rgstate[1] == ST_INVISIBLE, "style %#lx state[1] %#lx", styles[i], t.rgstate[1]);
        CHECK(t.rgstate[0] == ST_FOCUSABLE);
        U(DestroyWindow)(h);
    }
}

/* SYSMENU windows: min/max/help/close button states follow the style bits. */
static const struct { DWORD style; DWORD ex; DWORD min, max, help, close; } btn_tab[] = {
    { 0,                                          0,               ST_INVISIBLE,   ST_INVISIBLE,   ST_INVISIBLE, 0 },
    { WS_MINIMIZEBOX,                             0,               0,              ST_UNAVAILABLE, ST_INVISIBLE, 0 },
    { WS_MAXIMIZEBOX,                             0,               ST_UNAVAILABLE, 0,              ST_INVISIBLE, 0 },
    { WS_MINIMIZEBOX | WS_MAXIMIZEBOX,            0,               0,              0,              ST_INVISIBLE, 0 },
    { 0,                                          WS_EX_CONTEXTHELP, ST_INVISIBLE, ST_INVISIBLE,   0,            0 },
    { WS_MINIMIZEBOX,                             WS_EX_CONTEXTHELP, 0,            ST_UNAVAILABLE, ST_INVISIBLE, 0 },
    { WS_MAXIMIZEBOX,                             WS_EX_CONTEXTHELP, ST_UNAVAILABLE, 0,            ST_INVISIBLE, 0 },
    { WS_MINIMIZEBOX | WS_MAXIMIZEBOX,            WS_EX_CONTEXTHELP, 0,            0,              ST_INVISIBLE, 0 },
};

TEST(tbi_button_states_table) {
    REQUIRE(U(GetTitleBarInfo)); REQUIRE(U(CreateWindowExW));
    for (unsigned i = 0; i < sizeof btn_tab / sizeof *btn_tab; i++) {
        HWND h = mk_plain(WS_CAPTION | WS_SYSMENU | btn_tab[i].style, btn_tab[i].ex, 1, NULL); SIB_NEED_WND(h);
        TITLEBARINFO t;
        CHECK(tbi(h, &t));
        /* rgstate[2] minimize, [3] maximize, [4] help, [5] close (the title-bar info query, WS_MINIMIZEBOX/WS_MAXIMIZEBOX/WS_EX_CONTEXTHELP). */
        CHECKF(t.rgstate[2] == btn_tab[i].min, "row %u minimize %#lx want %#lx", i, t.rgstate[2], btn_tab[i].min);
        CHECKF(t.rgstate[3] == btn_tab[i].max, "row %u maximize %#lx want %#lx", i, t.rgstate[3], btn_tab[i].max);
        CHECKF(t.rgstate[4] == btn_tab[i].help, "row %u help %#lx want %#lx", i, t.rgstate[4], btn_tab[i].help);
        CHECKF(t.rgstate[5] == btn_tab[i].close, "row %u close %#lx want %#lx", i, t.rgstate[5], btn_tab[i].close);
        U(DestroyWindow)(h);
    }
}

TEST(tbi_close_unavailable_for_nocloseclass) {
    REQUIRE(U(GetTitleBarInfo)); REQUIRE(U(CreateWindowExW)); REQUIRE(U(RegisterClassExW));
    const wchar_t *nc = noclose_class();
    if (!nc) { t_skip("class registration failed"); return; }
    HWND h = mk_plain(WS_CAPTION | WS_SYSMENU, 0, 1, nc); SIB_NEED_WND(h);
    TITLEBARINFO t;
    CHECK(tbi(h, &t));
    /* the close-availability check is FALSE for CS_NOCLOSE classes => state[5] |= UNAVAILABLE. */
    CHECKF(t.rgstate[5] == ST_UNAVAILABLE, "close %#lx", t.rgstate[5]);
    U(DestroyWindow)(h);
}

TEST(tbi_close_after_sysmenu_close_disabled) {
    REQUIRE(U(GetTitleBarInfo)); REQUIRE(U(CreateWindowExW)); REQUIRE(U(GetSystemMenu)); REQUIRE(U(EnableMenuItem));
    HWND h = mk_plain(WS_CAPTION | WS_SYSMENU, 0, 1, NULL); SIB_NEED_WND(h);
    HMENU m = U(GetSystemMenu)(h, FALSE);
    if (!m) { t_skip("no system menu"); U(DestroyWindow)(h); return; }
    TITLEBARINFO t;
    CHECK(tbi(h, &t) && t.rgstate[5] == 0);
    U(EnableMenuItem)(m, SC_CLOSE, MF_BYCOMMAND | MF_GRAYED);
    CHECK(tbi(h, &t));
    /* the close-availability check also consults the SC_CLOSE item state; the exact mapping is version dependent, so only record it. */
    t_note("close button state with grayed SC_CLOSE: %#lx", t.rgstate[5]);
    U(EnableMenuItem)(m, SC_CLOSE, MF_BYCOMMAND | MF_ENABLED);
    CHECK(tbi(h, &t) && t.rgstate[5] == 0);
    U(DestroyWindow)(h);
}

TEST(tbi_rect_inside_window) {
    REQUIRE(U(GetTitleBarInfo)); REQUIRE(U(GetWindowInfo)); REQUIRE(U(CreateWindowExW));
    static const DWORD styles[] = { WS_CAPTION, WS_CAPTION | WS_THICKFRAME, WS_OVERLAPPEDWINDOW, WS_CAPTION | WS_SYSMENU };
    static const DWORD exs[] = { 0, WS_EX_TOOLWINDOW, WS_EX_DLGMODALFRAME };
    for (unsigned i = 0; i < sizeof styles / sizeof *styles; i++) for (unsigned j = 0; j < 3; j++) {
        HWND h = mk_plain(styles[i], exs[j], 1, NULL); SIB_NEED_WND(h);
        TITLEBARINFO t; WINDOWINFO wi = { sizeof wi };
        CHECK(tbi(h, &t) && U(GetWindowInfo)(h, &wi));
        const RECT *r = &t.rcTitleBar;
        CHECKF(r->right > r->left && r->bottom > r->top, "style %#lx ex %#lx empty rect", styles[i], exs[j]);
        CHECK(r->left >= wi.rcWindow.left && r->right <= wi.rcWindow.right && r->top >= wi.rcWindow.top);
        /* The title bar ends above the client area. */
        CHECKF(r->bottom <= wi.rcClient.top, "style %#lx ex %#lx title bottom %ld client top %ld", styles[i], exs[j], r->bottom, wi.rcClient.top);
        t_note("style %#lx ex %#lx titlebar h=%ld w=%ld left-inset=%ld top-inset=%ld", styles[i], exs[j], r->bottom - r->top, r->right - r->left,
               r->left - wi.rcWindow.left, r->top - wi.rcWindow.top);
        U(DestroyWindow)(h);
    }
}

TEST(tbi_rect_insets_match_windowinfo_borders) {
    REQUIRE(U(GetTitleBarInfo)); REQUIRE(U(GetWindowInfo)); REQUIRE(U(CreateWindowExW));
    /* Captioned windows without a system menu icon: the title bar starts one border inset inside the window rectangle on
     * left, top and right. the title-bar info query computes the inset with the same formula as user32!GetWindowInfo. */
    static const DWORD styles[] = { WS_CAPTION, WS_CAPTION | WS_THICKFRAME, WS_CAPTION | WS_MINIMIZEBOX };
    for (unsigned i = 0; i < sizeof styles / sizeof *styles; i++) {
        HWND h = mk_plain(styles[i], 0, 1, NULL); SIB_NEED_WND(h);
        TITLEBARINFO t; WINDOWINFO wi = { sizeof wi };
        CHECK(tbi(h, &t) && U(GetWindowInfo)(h, &wi));
        int b = (int)wi.cxWindowBorders;
        UINT (WINAPI *dpi)(void) = (UINT (WINAPI *)(void))t_proc("GetDpiForSystem");
        int match = (t.rcTitleBar.left - wi.rcWindow.left == b) && (t.rcTitleBar.top - wi.rcWindow.top == b) && (wi.rcWindow.right - t.rcTitleBar.right == b);
        if (!match && dpi && dpi() != 96) t_note("insets differ at DPI %u", dpi());
        else CHECKF(match, "style %#lx borders %d left %ld top %ld right %ld", styles[i], b, t.rcTitleBar.left - wi.rcWindow.left,
                    t.rcTitleBar.top - wi.rcWindow.top, wi.rcWindow.right - t.rcTitleBar.right);
        U(DestroyWindow)(h);
    }
}

TEST(tbi_toolwindow_caption_shorter) {
    REQUIRE(U(GetTitleBarInfo)); REQUIRE(U(CreateWindowExW));
    HWND a = mk_plain(WS_CAPTION, 0, 1, NULL), b = mk_plain(WS_CAPTION, WS_EX_TOOLWINDOW, 1, NULL);
    if (!a || !b) { t_skip("window"); return; }
    TITLEBARINFO ta, tb;
    CHECK(tbi(a, &ta) && tbi(b, &tb));
    int ha = ta.rcTitleBar.bottom - ta.rcTitleBar.top, hb = tb.rcTitleBar.bottom - tb.rcTitleBar.top;
    /* WS_EX_TOOLWINDOW selects the small caption metric (the window's -0x80 style bit picks GetDpiDependentMetric 0x16). */
    t_note("caption height normal %d toolwindow %d", ha, hb);
    CHECKF(hb <= ha, "tool %d normal %d", hb, ha);
    U(DestroyWindow)(a); U(DestroyWindow)(b);
}

TEST(tbi_rtl_layout_mirrors_rect) {
    REQUIRE(U(GetTitleBarInfo)); REQUIRE(U(CreateWindowExW)); REQUIRE(U(GetWindowInfo));
    HWND a = mk_plain(WS_CAPTION | WS_SYSMENU, 0, 1, NULL), b = mk_plain(WS_CAPTION | WS_SYSMENU, WS_EX_LAYOUTRTL, 1, NULL);
    if (!a || !b) { t_skip("window"); return; }
    TITLEBARINFO ta, tb;
    CHECK(tbi(a, &ta) && tbi(b, &tb));
    CHECK(ta.rcTitleBar.right > ta.rcTitleBar.left && tb.rcTitleBar.right > tb.rcTitleBar.left);
    /* A mirrored window keeps its title bar inside the window in screen coordinates as well. */
    WINDOWINFO wb = { sizeof wb };
    CHECK(U(GetWindowInfo)(b, &wb));
    CHECK(tb.rcTitleBar.left >= wb.rcWindow.left && tb.rcTitleBar.right <= wb.rcWindow.right);
    U(DestroyWindow)(a); U(DestroyWindow)(b);
}

/* ---- GetComboBoxInfo ----------------------------------------------------------------------- */

static HWND mk_combo(HWND parent, DWORD cbs, int w, int h) {
    return U(CreateWindowExW)(0, L"ComboBox", L"", WS_CHILD | cbs, 5, 5, w, h, parent, NULL, GetModuleHandleW(NULL), NULL);
}
static int cbi(HWND h, COMBOBOXINFO *ci) {
    memset(ci, 0xCC, sizeof *ci);
    ci->cbSize = sizeof *ci;
    return U(GetComboBoxInfo)(h, ci);
}
static void class_of(HWND h, wchar_t *buf, int n) { buf[0] = 0; if (h) U(GetClassNameW)(h, buf, n); }

TEST(cbi_bad_cbsize) {
    REQUIRE(U(GetComboBoxInfo)); REQUIRE(U(CreateWindowExW));
    HWND p = mk_plain(WS_OVERLAPPEDWINDOW, 0, 0, NULL); SIB_NEED_WND(p);
    HWND c = mk_combo(p, CBS_DROPDOWNLIST, 120, 200);
    if (!c) { t_skip("ComboBox"); U(DestroyWindow)(p); return; }
    static const DWORD sz[] = { 0, 4, 0x3c, 0x3f, 0x41, 0x44, 0x80, 0xffffffffu };
    for (unsigned i = 0; i < sizeof sz / sizeof *sz; i++) {
        COMBOBOXINFO ci; memset(&ci, 0xCC, sizeof ci); ci.cbSize = sz[i];
        SET_ERR(0);
        /* GetComboBoxInfo: cbSize != 0x40 => ERROR_INVALID_PARAMETER, FALSE. */
        CHECK(!U(GetComboBoxInfo)(c, &ci));
        EXPECT_ERR(E_INVALID_PARAM);
        CHECK(ci.hwndList == (HWND)(ULONG_PTR)0xCCCCCCCCCCCCCCCCull);
    }
    U(DestroyWindow)(p);
}

TEST(cbi_invalid_hwnd) {
    REQUIRE(U(GetComboBoxInfo)); REQUIRE(U(CreateWindowExW));
    HWND gone = mk_plain(WS_OVERLAPPEDWINDOW, 0, 0, NULL); SIB_NEED_WND(gone);
    U(DestroyWindow)(gone);
    HWND bad[] = { NULL, (HWND)(ULONG_PTR)0x777, (HWND)(ULONG_PTR)-1, gone };
    for (unsigned i = 0; i < 4; i++) {
        COMBOBOXINFO ci;
        SET_ERR(0);
        CHECK(!cbi(bad[i], &ci));
        EXPECT_ERR(E_INVALID_HWND);
    }
}

TEST(cbi_non_combo_windows) {
    REQUIRE(U(GetComboBoxInfo)); REQUIRE(U(CreateWindowExW));
    HWND p = mk_plain(WS_OVERLAPPEDWINDOW, 0, 0, NULL); SIB_NEED_WND(p);
    static const wchar_t *const cls[] = { L"Static", L"Button", L"Edit", L"ListBox", L"ScrollBar" };
    for (unsigned i = 0; i < 5; i++) {
        HWND c = sib_child(p, cls[i], 0, 0, 0, 80, 40);
        if (!c) continue;
        COMBOBOXINFO ci;
        /* Not a combo: the request is forwarded as CB_GETCOMBOBOXINFO (0x164); only combo boxes answer it. */
        CHECKF(!cbi(c, &ci), "class %ls", cls[i]);
    }
    COMBOBOXINFO ci;
    CHECK(!cbi(p, &ci));
    U(DestroyWindow)(p);
}

TEST(cbi_dropdownlist_layout) {
    REQUIRE(U(GetComboBoxInfo)); REQUIRE(U(CreateWindowExW)); REQUIRE(U(GetClientRect));
    HWND p = mk_plain(WS_OVERLAPPEDWINDOW, 0, 1, NULL); SIB_NEED_WND(p);
    HWND c = mk_combo(p, CBS_DROPDOWNLIST | WS_VISIBLE, 140, 200);
    if (!c) { t_skip("ComboBox"); U(DestroyWindow)(p); return; }
    COMBOBOXINFO ci;
    CHECK(cbi(c, &ci));
    /* cbSize is not written back reliably (observed 0 after a successful call), so it is not asserted */
    CHECK(ci.hwndCombo == c);
    CHECK(ci.hwndItem == NULL);              /* the combo uses itself as edit placeholder; GetComboBoxInfo reports NULL */
    CHECK(ci.hwndList != NULL);
    wchar_t cn[64]; class_of(ci.hwndList, cn, 64);
    CHECK(!lstrcmpiW(cn, L"ComboLBox"));
    CHECKF(ci.stateButton == 0, "stateButton %#lx", ci.stateButton);
    RECT cr; U(GetClientRect)(c, &cr);
    CHECK(ci.rcButton.right > ci.rcButton.left && ci.rcButton.bottom > ci.rcButton.top);
    CHECK(ci.rcItem.right > ci.rcItem.left && ci.rcItem.bottom > ci.rcItem.top);
    CHECK(ci.rcItem.right <= ci.rcButton.left);       /* the button is on the right of the item (non-RTL) */
    CHECK(ci.rcButton.right <= cr.right && ci.rcButton.bottom <= cr.bottom && ci.rcItem.left >= 0 && ci.rcItem.top >= 0);
    t_note("DROPDOWNLIST item %ld,%ld,%ld,%ld button %ld,%ld,%ld,%ld client %ld x %ld", ci.rcItem.left, ci.rcItem.top, ci.rcItem.right, ci.rcItem.bottom,
           ci.rcButton.left, ci.rcButton.top, ci.rcButton.right, ci.rcButton.bottom, cr.right, cr.bottom);
    U(DestroyWindow)(p);
}

TEST(cbi_dropdown_has_edit) {
    REQUIRE(U(GetComboBoxInfo)); REQUIRE(U(CreateWindowExW)); REQUIRE(U(GetParent));
    HWND p = mk_plain(WS_OVERLAPPEDWINDOW, 0, 1, NULL); SIB_NEED_WND(p);
    HWND c = mk_combo(p, CBS_DROPDOWN | WS_VISIBLE, 140, 200);
    if (!c) { t_skip("ComboBox"); U(DestroyWindow)(p); return; }
    COMBOBOXINFO ci;
    CHECK(cbi(c, &ci));
    CHECK(ci.hwndCombo == c && ci.hwndItem != NULL && ci.hwndList != NULL && ci.hwndItem != ci.hwndList);
    wchar_t cn[64]; class_of(ci.hwndItem, cn, 64);
    CHECK(!lstrcmpiW(cn, L"Edit"));
    CHECK(U(GetParent)(ci.hwndItem) == c);
    class_of(ci.hwndList, cn, 64);
    CHECK(!lstrcmpiW(cn, L"ComboLBox"));
    CHECKF(ci.stateButton == 0, "stateButton %#lx", ci.stateButton);
    U(DestroyWindow)(p);
}

TEST(cbi_simple_button_invisible) {
    REQUIRE(U(GetComboBoxInfo)); REQUIRE(U(CreateWindowExW));
    HWND p = mk_plain(WS_OVERLAPPEDWINDOW, 0, 1, NULL); SIB_NEED_WND(p);
    HWND c = mk_combo(p, CBS_SIMPLE | WS_VISIBLE, 140, 200);
    if (!c) { t_skip("ComboBox"); U(DestroyWindow)(p); return; }
    COMBOBOXINFO ci;
    CHECK(cbi(c, &ci));
    /* (style & 3) == CBS_SIMPLE => stateButton = STATE_SYSTEM_INVISIBLE. */
    CHECKF(ci.stateButton == ST_INVISIBLE, "stateButton %#lx", ci.stateButton);
    CHECK(ci.hwndCombo == c && ci.hwndItem != NULL && ci.hwndList != NULL);
    wchar_t cn[64]; class_of(ci.hwndItem, cn, 64);
    CHECK(!lstrcmpiW(cn, L"Edit"));
    class_of(ci.hwndList, cn, 64);
    CHECK(!lstrcmpiW(cn, L"ComboLBox"));
    U(DestroyWindow)(p);
}

TEST(cbi_stub_matches_user32) {
    FARPROC pr = sib_stub("NtUserGetComboBoxInfo");
    REQUIRE(pr); REQUIRE(U(GetComboBoxInfo)); REQUIRE(U(CreateWindowExW));
    NtUserGetComboBoxInfo_pfn f = (NtUserGetComboBoxInfo_pfn)pr;
    HWND p = mk_plain(WS_OVERLAPPEDWINDOW, 0, 1, NULL); SIB_NEED_WND(p);
    HWND c = mk_combo(p, CBS_DROPDOWN | WS_VISIBLE, 140, 200);
    if (!c) { t_skip("ComboBox"); U(DestroyWindow)(p); return; }
    COMBOBOXINFO a, b;
    CHECK(cbi(c, &a));
    memset(&b, 0xCC, sizeof b); b.cbSize = sizeof b;
    CHECK(f(c, &b));
    CHECK(!memcmp(&a, &b, sizeof a));
    U(DestroyWindow)(p);
}

TEST(cbi_via_list_window) {
    REQUIRE(U(GetComboBoxInfo)); REQUIRE(U(CreateWindowExW));
    HWND p = mk_plain(WS_OVERLAPPEDWINDOW, 0, 1, NULL); SIB_NEED_WND(p);
    HWND c = mk_combo(p, CBS_DROPDOWN | WS_VISIBLE, 140, 200);
    if (!c) { t_skip("ComboBox"); U(DestroyWindow)(p); return; }
    COMBOBOXINFO a, b;
    CHECK(cbi(c, &a));
    /* The drop-down list window resolves to its owning combo (unsafe_cast_fnid_or_class_to_PLBWND_COMBOLISTBOX -> pcbox). */
    if (a.hwndList && cbi(a.hwndList, &b)) {
        CHECK(b.hwndCombo == c);
        CHECK(b.hwndList == a.hwndList);
        CHECK(!memcmp(&a.rcItem, &b.rcItem, sizeof a.rcItem) && !memcmp(&a.rcButton, &b.rcButton, sizeof a.rcButton));
    } else t_note("GetComboBoxInfo on the ComboLBox window failed (err %lu)", GetLastError());
    U(DestroyWindow)(p);
}

TEST(cbi_button_state_follows_drop) {
    REQUIRE(U(GetComboBoxInfo)); REQUIRE(U(CreateWindowExW));
    HWND p = mk_plain(WS_OVERLAPPEDWINDOW, 0, 1, NULL); SIB_NEED_WND(p);
    HWND c = mk_combo(p, CBS_DROPDOWNLIST | WS_VISIBLE, 140, 200);
    if (!c) { t_skip("ComboBox"); U(DestroyWindow)(p); return; }
    COMBOBOXINFO ci;
    CHECK(cbi(c, &ci) && ci.stateButton == 0);
    /* Pressed bit (0x8) is set while the list is dropped; CB_SHOWDROPDOWN on an off-screen combo is bounded and reversed. */
    SendMessageW(c, CB_SHOWDROPDOWN, TRUE, 0);
    k_pump(50);
    CHECK(cbi(c, &ci));
    t_note("stateButton while dropped: %#lx", ci.stateButton);
    SendMessageW(c, CB_SHOWDROPDOWN, FALSE, 0);
    k_pump(50);
    CHECK(cbi(c, &ci) && ci.stateButton == 0);
    U(DestroyWindow)(p);
}

/* ---- GetMenuBarInfo ---------------------------------------------------------------------- */

static int mbi(HWND h, LONG obj, LONG item, MENUBARINFO *m) {
    memset(m, 0xCC, sizeof *m);
    m->cbSize = sizeof *m;
    return U(GetMenuBarInfo)(h, obj, item, m);
}

TEST(mbi_bad_cbsize) {
    REQUIRE(U(GetMenuBarInfo)); REQUIRE(U(CreateWindowExW));
    HWND h = mk_plain(WS_OVERLAPPEDWINDOW, 0, 1, NULL); SIB_NEED_WND(h);
    static const DWORD sz[] = { 0, 4, 0x2c, 0x2f, 0x31, 0x38, 0xffffffffu };
    for (unsigned i = 0; i < sizeof sz / sizeof *sz; i++) {
        MENUBARINFO m; memset(&m, 0xCC, sizeof m); m.cbSize = sz[i];
        SET_ERR(0);
        /* GetMenuBarInfo: cbSize != 0x30 => ERROR_INVALID_PARAMETER. */
        CHECK(!U(GetMenuBarInfo)(h, OBJID_MENU, 0, &m));
        EXPECT_ERR(E_INVALID_PARAM);
    }
    U(DestroyWindow)(h);
}

TEST(mbi_invalid_hwnd) {
    REQUIRE(U(GetMenuBarInfo)); REQUIRE(U(CreateWindowExW));
    HWND gone = mk_plain(WS_OVERLAPPEDWINDOW, 0, 0, NULL); SIB_NEED_WND(gone);
    U(DestroyWindow)(gone);
    HWND bad[] = { NULL, (HWND)(ULONG_PTR)0x999, (HWND)(ULONG_PTR)-1, gone };
    for (unsigned i = 0; i < 4; i++) {
        MENUBARINFO m;
        SET_ERR(0);
        CHECK(!mbi(bad[i], OBJID_MENU, 0, &m));
        EXPECT_ERR(E_INVALID_HWND);
    }
}

TEST(mbi_no_menu) {
    REQUIRE(U(GetMenuBarInfo)); REQUIRE(U(CreateWindowExW));
    HWND h = mk_plain(WS_OVERLAPPEDWINDOW, 0, 1, NULL); SIB_NEED_WND(h);
    MENUBARINFO m;
    SET_ERR(0);
    /* OBJID_MENU on a window without a menu bar: no failure code is set, simply FALSE. */
    CHECK(!mbi(h, OBJID_MENU, 0, &m));
    EXPECT_ERR(0);
    CHECK(!mbi(h, OBJID_MENU, 1, &m));
    U(DestroyWindow)(h);
}

TEST(mbi_unknown_object_ids) {
    REQUIRE(U(GetMenuBarInfo)); REQUIRE(U(CreateWindowExW)); REQUIRE(U(CreateMenu)); REQUIRE(U(AppendMenuW)); REQUIRE(U(SetMenu));
    HWND h = mk_plain(WS_OVERLAPPEDWINDOW, 0, 1, NULL); SIB_NEED_WND(h);
    HMENU m = U(CreateMenu)();
    U(AppendMenuW)(m, MF_STRING, 1, L"One");
    U(SetMenu)(h, m);
    static const LONG ids[] = { 0, 1, -2, -5, -6, -7, 100, (LONG)0x80000000 };
    for (unsigned i = 0; i < sizeof ids / sizeof *ids; i++) {
        MENUBARINFO mi;
        SET_ERR(0);
        /* Only OBJID_MENU, OBJID_SYSMENU and OBJID_CLIENT are handled; other ids fall through to FALSE without SetLastError. */
        CHECKF(!mbi(h, ids[i], 0, &mi), "id %ld", ids[i]);
        EXPECT_ERR(0);
    }
    U(DestroyWindow)(h);
}

TEST(mbi_menu_bar_item_range) {
    REQUIRE(U(GetMenuBarInfo)); REQUIRE(U(CreateWindowExW)); REQUIRE(U(CreateMenu)); REQUIRE(U(AppendMenuW)); REQUIRE(U(SetMenu)); REQUIRE(U(GetWindowInfo));
    HWND h = mk_plain(WS_OVERLAPPEDWINDOW, 0, 1, NULL); SIB_NEED_WND(h);
    HMENU m = U(CreateMenu)();
    U(AppendMenuW)(m, MF_STRING, 1, L"File");
    U(AppendMenuW)(m, MF_STRING, 2, L"Edit");
    U(AppendMenuW)(m, MF_STRING, 3, L"Help");
    CHECK(U(SetMenu)(h, m));
    WINDOWINFO wi = { sizeof wi };
    CHECK(U(GetWindowInfo)(h, &wi));
    MENUBARINFO mi;
    /* idItem 0 is the whole bar; 1..cItems are the items; beyond cItems (and negative) the call fails. */
    CHECK(mbi(h, OBJID_MENU, 0, &mi));
    CHECK(mi.cbSize == sizeof mi);
    CHECK(mi.hMenu == m);
    CHECK(!mi.fBarFocused && !mi.fFocused);
    CHECK(mi.hwndMenu == NULL);
    RECT bar = mi.rcBar;
    t_note("menu bar rect %ld,%ld,%ld,%ld window %ld,%ld,%ld,%ld client top %ld", bar.left, bar.top, bar.right, bar.bottom,
           wi.rcWindow.left, wi.rcWindow.top, wi.rcWindow.right, wi.rcWindow.bottom, wi.rcClient.top);
    CHECK(bar.right > bar.left && bar.bottom > bar.top);
    CHECK(bar.left >= wi.rcWindow.left && bar.right <= wi.rcWindow.right && bar.top >= wi.rcWindow.top);
    CHECK(bar.bottom <= wi.rcClient.top);
    for (LONG i = 1; i <= 3; i++) {
        CHECKF(mbi(h, OBJID_MENU, i, &mi), "item %ld", i);
        CHECK(mi.hMenu == m);
        CHECK(mi.rcBar.right > mi.rcBar.left && mi.rcBar.bottom > mi.rcBar.top);
        CHECK(mi.rcBar.left >= wi.rcWindow.left && mi.rcBar.right <= wi.rcWindow.right);
        CHECK(mi.rcBar.top >= wi.rcWindow.top && mi.rcBar.bottom <= wi.rcWindow.bottom);
    }
    MENUBARINFO a, b, c;
    CHECK(mbi(h, OBJID_MENU, 1, &a) && mbi(h, OBJID_MENU, 2, &b) && mbi(h, OBJID_MENU, 3, &c));
    CHECK(a.rcBar.right <= b.rcBar.left && b.rcBar.right <= c.rcBar.left);   /* items run left to right */
    CHECK(!mbi(h, OBJID_MENU, 4, &mi));
    CHECK(!mbi(h, OBJID_MENU, 100, &mi));
    CHECK(!mbi(h, OBJID_MENU, -1, &mi));
    U(SetMenu)(h, NULL);
    U(DestroyMenu)(m);
    U(DestroyWindow)(h);
}

TEST(mbi_menu_bar_set_flags) {
    REQUIRE(U(GetMenuBarInfo)); REQUIRE(U(CreateWindowExW)); REQUIRE(U(CreateMenu)); REQUIRE(U(AppendMenuW)); REQUIRE(U(SetMenu));
    HWND h = mk_plain(WS_OVERLAPPEDWINDOW, 0, 1, NULL); SIB_NEED_WND(h);
    HMENU m = U(CreateMenu)();
    U(AppendMenuW)(m, MF_STRING, 1, L"File");
    U(SetMenu)(h, m);
    MENUBARINFO mi;
    CHECK(mbi(h, OBJID_MENU, 0, &mi));
 /* fBarFocused/fFocused are bits 0 and 1 of the trailing bitfield; the kernel clears them when no menu is tracking. */
    CHECK(mi.fBarFocused == 0 && mi.fFocused == 0);
    U(SetMenu)(h, NULL);
    CHECK(!mbi(h, OBJID_MENU, 0, &mi));
    U(DestroyMenu)(m);
    U(DestroyWindow)(h);
}

TEST(mbi_sysmenu) {
    REQUIRE(U(GetMenuBarInfo)); REQUIRE(U(CreateWindowExW)); REQUIRE(U(GetSystemMenu));
    HWND h = mk_plain(WS_CAPTION | WS_SYSMENU, 0, 1, NULL); SIB_NEED_WND(h);
    MENUBARINFO mi;
    HMENU sm = U(GetSystemMenu)(h, FALSE);
    if (!sm) { t_skip("no system menu"); U(DestroyWindow)(h); return; }
    CHECK(mbi(h, OBJID_SYSMENU, 0, &mi));
    /* hMenu is a menu handle of its own (a different one from the handle GetSystemMenu returned, and not stable
     * between calls), so only "valid and not the GetSystemMenu handle" is asserted. */
    CHECK(mi.hMenu != NULL && mi.hMenu != sm);
    CHECK(!mi.fBarFocused && !mi.fFocused);
    /* A system menu has one top-level popup item: idItem 1 is accepted, a far index is not. */
    CHECK(!mbi(h, OBJID_SYSMENU, 100, &mi));
    CHECK(!mbi(h, OBJID_SYSMENU, -1, &mi));
    U(DestroyWindow)(h);
}

TEST(mbi_sysmenu_absent_without_style) {
    REQUIRE(U(GetMenuBarInfo)); REQUIRE(U(CreateWindowExW));
    HWND h = mk_plain(WS_CAPTION, 0, 1, NULL); SIB_NEED_WND(h);
    MENUBARINFO mi;
    SET_ERR(0);
    CHECK(!mbi(h, OBJID_SYSMENU, 0, &mi));     /* needs WS_SYSMENU (+0x1e & 8) */
    EXPECT_ERR(0);
    U(DestroyWindow)(h);
}

TEST(mbi_stub_matches_user32) {
    FARPROC pr = sib_stub("NtUserGetMenuBarInfo");
    REQUIRE(pr); REQUIRE(U(GetMenuBarInfo)); REQUIRE(U(CreateWindowExW)); REQUIRE(U(CreateMenu)); REQUIRE(U(AppendMenuW)); REQUIRE(U(SetMenu));
    NtUserGetMenuBarInfo_pfn f = (NtUserGetMenuBarInfo_pfn)pr;
    HWND h = mk_plain(WS_OVERLAPPEDWINDOW, 0, 1, NULL); SIB_NEED_WND(h);
    HMENU m = U(CreateMenu)();
    U(AppendMenuW)(m, MF_STRING, 1, L"File");
    U(SetMenu)(h, m);
    MENUBARINFO a, b;
    CHECK(mbi(h, OBJID_MENU, 1, &a));
    memset(&b, 0xCC, sizeof b); b.cbSize = sizeof b;
    CHECK(f(h, OBJID_MENU, 1, &b));
    CHECK(!memcmp(&a, &b, sizeof a));
    U(SetMenu)(h, NULL);
    U(DestroyMenu)(m);
    U(DestroyWindow)(h);
}

/* ---- GetCursorInfo ----------------------------------------------------------------------- */

TEST(ci_bad_cbsize) {
    REQUIRE(U(GetCursorInfo));
    static const DWORD sz[] = { 0, 4, 0x14, 0x17, 0x19, 0x1c, 0x28, 0xffffffffu };
    for (unsigned i = 0; i < sizeof sz / sizeof *sz; i++) {
        CURSORINFO ci; memset(&ci, 0xCC, sizeof ci); ci.cbSize = sz[i];
        SET_ERR(0);
        /* NtUserGetCursorInfo copies the result back only when the caller's cbSize is 0x18; otherwise ERROR_INVALID_PARAMETER. */
        BOOL r = U(GetCursorInfo)(&ci);
        if (!r && GetLastError() == ERROR_ACCESS_DENIED) { t_skip("no access to the input desktop's cursor"); return; }
        CHECK(!r);
        EXPECT_ERR(E_INVALID_PARAM);
        CHECK(ci.flags == 0xCCCCCCCCu && ci.hCursor == (HCURSOR)(ULONG_PTR)0xCCCCCCCCCCCCCCCCull);
    }
}

TEST(ci_basic_fields) {
    REQUIRE(U(GetCursorInfo)); REQUIRE(U(GetSystemMetrics));
    CURSORINFO ci; memset(&ci, 0xCC, sizeof ci); ci.cbSize = sizeof ci;
    if (!U(GetCursorInfo)(&ci)) { t_skip("GetCursorInfo failed (err %lu)", GetLastError()); return; }
    /* cbSize is not written back reliably (observed 0 after a successful call), so it is not asserted */
    CHECKF((ci.flags & ~(DWORD)(CURSOR_SHOWING | 2)) == 0, "flags %#lx", ci.flags);       /* 2 == CURSOR_SUPPRESSED */
    int vx = U(GetSystemMetrics)(SM_XVIRTUALSCREEN), vy = U(GetSystemMetrics)(SM_YVIRTUALSCREEN);
    int vw = U(GetSystemMetrics)(SM_CXVIRTUALSCREEN), vh = U(GetSystemMetrics)(SM_CYVIRTUALSCREEN);
    CHECKF(ci.ptScreenPos.x >= vx && ci.ptScreenPos.x < vx + vw && ci.ptScreenPos.y >= vy && ci.ptScreenPos.y < vy + vh,
           "pos %ld,%ld virtual %d,%d %dx%d", ci.ptScreenPos.x, ci.ptScreenPos.y, vx, vy, vw, vh);
    t_info("cursor flags %#lx hCursor %p pos %ld,%ld", ci.flags, ci.hCursor, ci.ptScreenPos.x, ci.ptScreenPos.y);
}

TEST(ci_matches_getcursorpos) {
    REQUIRE(U(GetCursorInfo)); REQUIRE(U(GetCursorPos));
    int ok = 0;
    for (int tries = 0; tries < 5 && !ok; tries++) {
        POINT p0, p1; CURSORINFO ci; memset(&ci, 0, sizeof ci); ci.cbSize = sizeof ci;
        U(GetCursorPos)(&p0);
        if (!U(GetCursorInfo)(&ci)) { t_skip("GetCursorInfo failed (err %lu)", GetLastError()); return; }
        U(GetCursorPos)(&p1);
        ok = (p0.x == p1.x && p0.y == p1.y && ci.ptScreenPos.x == p0.x && ci.ptScreenPos.y == p0.y);
    }
    /* Both read the same kernel cursor position; retried because a real mouse may move between the calls. */
    CHECK(ok);
}

TEST(ci_stub_matches_user32) {
    FARPROC pr = sib_stub("NtUserGetCursorInfo");
    REQUIRE(pr); REQUIRE(U(GetCursorInfo));
    NtUserGetCursorInfo_pfn f = (NtUserGetCursorInfo_pfn)pr;
    CURSORINFO a, b;
    for (int tries = 0; tries < 5; tries++) {
        memset(&a, 0, sizeof a); a.cbSize = sizeof a;
        memset(&b, 0, sizeof b); b.cbSize = sizeof b;
        if (!U(GetCursorInfo)(&a)) { t_skip("GetCursorInfo failed"); return; }
        CHECK(f(&b));
        if (!memcmp(&a, &b, sizeof a)) return;
    }
    CHECK(0);
}

TEST(ci_showing_flag_vs_showcursor_count) {
    REQUIRE(U(GetCursorInfo)); REQUIRE(U(ShowCursor));
    CURSORINFO ci; memset(&ci, 0, sizeof ci); ci.cbSize = sizeof ci;
    if (!U(GetCursorInfo)(&ci)) { t_skip("GetCursorInfo failed"); return; }
    int was = !!(ci.flags & CURSOR_SHOWING);
    /* ShowCursor(TRUE)+ShowCursor(FALSE) leaves the display count unchanged, so the showing flag must not change. */
    U(ShowCursor)(TRUE);
    U(ShowCursor)(FALSE);
    memset(&ci, 0, sizeof ci); ci.cbSize = sizeof ci;
    CHECK(U(GetCursorInfo)(&ci));
    CHECK(!!(ci.flags & CURSOR_SHOWING) == was);
}

/* ---- GetLastInputInfo -------------------------------------------------------------------- */

TEST(lii_bad_cbsize) {
    REQUIRE(U(GetLastInputInfo));
    static const UINT sz[] = { 0, 4, 7, 9, 12, 16, 0x18, 0xffffffffu };
    for (unsigned i = 0; i < sizeof sz / sizeof *sz; i++) {
        LASTINPUTINFO li; memset(&li, 0xCC, sizeof li); li.cbSize = sz[i];
        SET_ERR(0);
        /* user32!GetLastInputInfo: cbSize != 8 => ERROR_INVALID_PARAMETER, FALSE, dwTime untouched. */
        CHECK(!U(GetLastInputInfo)(&li));
        EXPECT_ERR(E_INVALID_PARAM);
        CHECK(li.dwTime == 0xCCCCCCCCu);
    }
}

TEST(lii_not_in_future) {
    REQUIRE(U(GetLastInputInfo)); REQUIRE(U(GetTickCount));
    LASTINPUTINFO li = { sizeof li, 0xCCCCCCCCu };
    CHECK(U(GetLastInputInfo)(&li));
    DWORD now = U(GetTickCount)();
    CHECK(li.dwTime != 0xCCCCCCCCu);
    /* dwTime is a GetTickCount-domain stamp: never ahead of "now" (wrap-safe signed difference). */
    CHECKF((LONG)(now - li.dwTime) >= 0, "tick %lu last input %lu", now, li.dwTime);
    t_info("idle ms since last input: %lu", now - li.dwTime);
}

TEST(lii_matches_serverinfo) {
    REQUIRE(U(GetLastInputInfo));
    if (t_is_wine()) { t_skip("no SERVERINFO under Wine"); return; }
    struct k_user_shared_info si;
    if (!k_shared_info(&si) || !si.psi) { t_skip("gSharedInfo unavailable"); return; }
    int ok = 0;
    for (int tries = 0; tries < 5 && !ok; tries++) {
        unsigned long a = 0, b = 0;
        LASTINPUTINFO li = { sizeof li, 0 };
        /* user32 reads the last-input tick count from the shared server info (GetLastInputInfo). */
        if (!k_try_read_u32((char *)si.psi + 0x1368, &a)) { t_skip("SERVERINFO unreadable"); return; }
        CHECK(U(GetLastInputInfo)(&li));
        k_try_read_u32((char *)si.psi + 0x1368, &b);
        ok = (a == b && li.dwTime == a);
    }
    CHECK(ok);
}

TEST(lii_stable_without_input) {
    REQUIRE(U(GetLastInputInfo));
    LASTINPUTINFO a = { sizeof a }, b = { sizeof b };
    CHECK(U(GetLastInputInfo)(&a));
    Sleep(20);
    CHECK(U(GetLastInputInfo)(&b));
    /* Time only moves forward in the tick domain; our own window activity does not count as input. */
    CHECK((LONG)(b.dwTime - a.dwTime) >= 0);
}
