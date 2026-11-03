/* x_window_chrome_info_queries / combo: NtUserGetComboBoxInfo(hwnd, COMBOBOXINFO *).
 *
 * Measures, on hidden combo boxes of this process and of a helper thread:
 *  - the argument protocol: cbSize must be exactly sizeof(COMBOBOXINFO) (0x40), otherwise ERROR_INVALID_PARAMETER; a stale
 *    or garbage window is ERROR_INVALID_WINDOW_HANDLE; a window that is not a combo box answers FALSE; nothing is written
 *    on failure;
 *  - the handles: hwndCombo is the combo itself; the edit control is a child of the combo and has the Edit class for the
 *    drop-down and simple styles and does not exist for the drop-down list style; the list window has the ComboLBox
 *    class and is not visible while the list is closed;
 *  - the rectangles against the client rectangle: item and button lie inside the client area in client coordinates, the
 *    button is to the right of the item, as wide as the system scroll bar, and follows the combo when it is widened; the
 *    simple style has no button and reports it invisible;
 *  - a disabled combo reports an unavailable button.
 *
 * Determinism: only symbolic results are printed.
 */
#define XPFX "combo_"
#include "x.h"

#define CB_CB 0x40

static HWND mkcombo(DWORD cbs) {
    return U(CreateWindowExW)(0, L"COMBOBOX", L"", WS_POPUP | cbs, 10, 10, 100, 200, NULL, NULL, NULL, NULL);
}
static BOOL q(HWND w, COMBOBOXINFO *c) {
    memset(c, XFILL, sizeof *c); c->cbSize = CB_CB;
    SET_ERR(XSENT);
    return R32(UN(NtUserGetComboBoxInfo)(w, c)) != 0;
}
static int cls_is(HWND h, const WCHAR *name) {
    WCHAR b[64] = L""; if (!h) return 0;
    U(GetClassNameW)(h, b, 64); return !lstrcmpiW(b, name);
}
static int inside(const RECT *r, const RECT *c) { return r->left >= c->left && r->top >= c->top && r->right <= c->right && r->bottom <= c->bottom; }

XT(struct_size_is_0x40) { EXPECT_EQ(sizeof(COMBOBOXINFO), CB_CB); }

static void bad_cb(DWORD cb) {
    HWND w = mkcombo(CBS_DROPDOWN);
    union { COMBOBOXINFO c; unsigned char raw[0x100]; } u; memset(&u, XFILL, sizeof u); u.c.cbSize = cb;
    SET_ERR(XSENT);
    ULONG_PTR r = UN(NtUserGetComboBoxInfo)(w, &u.c);
    CHECKF(R32(r) == 0 && GetLastError() == XE_INVALID_PARAMETER, "cbSize 0x%lx: r=%lu gle=%lu", (unsigned long)cb, (unsigned long)R32(r), GetLastError());
    CHECKF(u.c.cbSize == cb && xw_untouched(u.raw + 4, sizeof u.raw - 4), "cbSize 0x%lx: output written", (unsigned long)cb);
    xw_kill(w);
}
XT(cbsize_anything_but_0x40_is_invalid) {
    NEED(NtUserGetComboBoxInfo);
    static const DWORD cbs[] = { 0, 1, 4, 0x3f, 0x3e, 0x3d, 0x41, 0x48, 0x80, 0x28, 0xFFFFFFFFu, 0x400000 };
    for (unsigned i = 0; i < sizeof cbs / sizeof cbs[0]; i++) bad_cb(cbs[i]);
}
XT(cbsize_exact_accepted_and_kept) {
    NEED(NtUserGetComboBoxInfo);
    HWND w = mkcombo(CBS_DROPDOWN); COMBOBOXINFO c; XOK(q(w, &c)); EXPECT_EQ(c.cbSize, CB_CB);
    xw_kill(w);
}

static void bad_hwnd(HWND h, const char *what) {
    COMBOBOXINFO c; memset(&c, XFILL, sizeof c); c.cbSize = CB_CB;
    SET_ERR(XSENT); ULONG_PTR r = UN(NtUserGetComboBoxInfo)(h, &c);
    CHECKF(R32(r) == 0 && GetLastError() == XE_INVALID_HWND, "%s: r=%lu gle=%lu", what, (unsigned long)R32(r), GetLastError());
    CHECKF(xw_untouched(&c.rcItem, sizeof c - 4), "%s: output written", what);
}
XT(hwnd_stale_or_garbage_is_invalid_window_handle) {
    NEED(NtUserGetComboBoxInfo);
    bad_hwnd(NULL, "null"); bad_hwnd((HWND)(ULONG_PTR)0x12345678, "garbage"); bad_hwnd((HWND)INVALID_HANDLE_VALUE, "invalid handle value");
    bad_hwnd(xw_dead(), "destroyed"); bad_hwnd((HWND)(ULONG_PTR)1, "low index");
}
XT(hwnd_checked_before_size) {
    NEED(NtUserGetComboBoxInfo);
    COMBOBOXINFO c; memset(&c, XFILL, sizeof c); c.cbSize = 5;
    XFAIL(UN(NtUserGetComboBoxInfo)(NULL, &c), XE_INVALID_HWND);
}
static void not_a_combo(const WCHAR *cls, DWORD style) {
    HWND w = U(CreateWindowExW)(0, cls, L"", WS_POPUP | style, 10, 10, 100, 100, NULL, NULL, NULL, NULL);
    COMBOBOXINFO c; memset(&c, XFILL, sizeof c); c.cbSize = CB_CB;
    SET_ERR(XSENT);
    ULONG_PTR r = UN(NtUserGetComboBoxInfo)(w, &c);
    CHECK(R32(r) == 0);
    EXPECT_ERR(XSENT);   /* a window of another class is refused without an error code */
    CHECK(xw_untouched(&c.rcItem, sizeof c - 4));
    xw_kill(w);
}
XT(windows_of_other_classes_are_refused_without_an_error) {
    NEED(NtUserGetComboBoxInfo);
    not_a_combo(L"STATIC", 0); not_a_combo(L"EDIT", 0); not_a_combo(L"BUTTON", 0); not_a_combo(L"LISTBOX", 0); not_a_combo(L"SCROLLBAR", 0);
}
XT(desktop_window_is_not_a_combo) {
    NEED(NtUserGetComboBoxInfo);
    COMBOBOXINFO c; memset(&c, XFILL, sizeof c); c.cbSize = CB_CB;
    EXPECT_EQ(R32(UN(NtUserGetComboBoxInfo)(U(GetDesktopWindow)(), &c)), 0);
    CHECK(xw_untouched(&c.rcItem, sizeof c - 4));
}
XT(out_null) {
    NEED(NtUserGetComboBoxInfo);
    HWND w = mkcombo(CBS_DROPDOWN);
    XFAIL(UN(NtUserGetComboBoxInfo)(w, NULL), XE_NOACCESS);
    xw_kill(w);
}
XT(out_unwritable) {
    NEED(NtUserGetComboBoxInfo);
    HWND w = mkcombo(CBS_DROPDOWN);
    XFAIL(UN(NtUserGetComboBoxInfo)(w, XP_BADPTR), XE_NOACCESS);
    XFAIL(UN(NtUserGetComboBoxInfo)(w, XP_KERNELPTR), XE_NOACCESS);
    xw_kill(w);
}

/* ---- handles ---- */
XT(dropdown_handles) {
    NEED(NtUserGetComboBoxInfo);
    HWND w = mkcombo(CBS_DROPDOWN); COMBOBOXINFO c; XOK(q(w, &c));
    CHECK(c.hwndCombo == w);
    CHECK(c.hwndItem != NULL); CHECK(U(GetParent)(c.hwndItem) == w); CHECK(cls_is(c.hwndItem, L"Edit"));
    CHECK(c.hwndList != NULL); CHECK(cls_is(c.hwndList, L"ComboLBox"));
    CHECK(!U(IsWindowVisible)(c.hwndList));
    xw_kill(w);
}
XT(dropdownlist_handles) {
    NEED(NtUserGetComboBoxInfo);
    HWND w = mkcombo(CBS_DROPDOWNLIST); COMBOBOXINFO c; XOK(q(w, &c));
    CHECK(c.hwndCombo == w);
    CHECK(c.hwndItem == NULL);
    CHECK(c.hwndList != NULL); CHECK(cls_is(c.hwndList, L"ComboLBox")); CHECK(!U(IsWindowVisible)(c.hwndList));
    xw_kill(w);
}
XT(simple_handles) {
    NEED(NtUserGetComboBoxInfo);
    HWND w = mkcombo(CBS_SIMPLE); COMBOBOXINFO c; XOK(q(w, &c));
    CHECK(c.hwndCombo == w);
    CHECK(c.hwndItem != NULL); CHECK(U(GetParent)(c.hwndItem) == w); CHECK(cls_is(c.hwndItem, L"Edit"));
    CHECK(c.hwndList != NULL); CHECK(cls_is(c.hwndList, L"ComboLBox"));
    xw_kill(w);
}
XT(simple_list_is_a_child_of_the_combo) {
    NEED(NtUserGetComboBoxInfo);
    HWND w = mkcombo(CBS_SIMPLE); COMBOBOXINFO c; XOK(q(w, &c));
    CHECK(U(GetParent)(c.hwndList) == w);
    xw_kill(w);
}
XT(handles_match_get_window_children_of_the_combo) {
    NEED(NtUserGetComboBoxInfo);
    HWND w = mkcombo(CBS_DROPDOWN); COMBOBOXINFO c; XOK(q(w, &c));
    HWND e = U(FindWindowExW)(w, NULL, L"Edit", NULL);
    CHECK(e == c.hwndItem);
    xw_kill(w);
}
XT(combo_edit_handle_matches_combobox_child_of_user32) {
    NEED(NtUserGetComboBoxInfo);
    HWND w = mkcombo(CBS_DROPDOWN); COMBOBOXINFO c; XOK(q(w, &c));
    HWND e = U(GetWindow)(w, GW_CHILD);
    CHECK(e == c.hwndItem);
    xw_kill(w);
}
XT(combo_with_child_style_has_the_same_shape) {
    NEED(NtUserGetComboBoxInfo);
    HWND p = xw_win(WS_POPUP, 0, L"STATIC");
    HWND w = U(CreateWindowExW)(0, L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWN, 5, 5, 100, 200, p, NULL, NULL, NULL);
    COMBOBOXINFO c; XOK(q(w, &c));
    CHECK(c.hwndCombo == w); CHECK(c.hwndItem && c.hwndList);
    xw_kill(p);
}

/* ---- rectangles ---- */
static void rects(DWORD cbs) {
    NEED(NtUserGetComboBoxInfo);
    HWND w = mkcombo(cbs); COMBOBOXINFO c; RECT cr; XOK(q(w, &c)); U(GetClientRect)(w, &cr);
    CHECKF(inside(&c.rcItem, &cr), "item (%ld,%ld,%ld,%ld) outside the client area", c.rcItem.left, c.rcItem.top, c.rcItem.right, c.rcItem.bottom);
    CHECK(c.rcItem.right > c.rcItem.left && c.rcItem.bottom > c.rcItem.top);
    if (cbs != CBS_SIMPLE) {
        CHECKF(inside(&c.rcButton, &cr), "button outside the client area");
        CHECK(c.rcItem.right <= c.rcButton.left);
        EXPECT_EQ(c.rcButton.right - c.rcButton.left, U(GetSystemMetrics)(SM_CXVSCROLL));
        CHECK(c.rcButton.bottom > c.rcButton.top);
    }
    xw_kill(w);
}
XT(rectangles_lie_inside_the_client_area_for_every_style) {
    rects(CBS_DROPDOWN); rects(CBS_DROPDOWNLIST); rects(CBS_SIMPLE); rects(CBS_DROPDOWN | CBS_SORT); rects(CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED);
}

XT(button_state_clear_for_dropdown_styles) {
    NEED(NtUserGetComboBoxInfo);
    DWORD s[] = { CBS_DROPDOWN, CBS_DROPDOWNLIST };
    for (int i = 0; i < 2; i++) { HWND w = mkcombo(s[i]); COMBOBOXINFO c; XOK(q(w, &c)); CHECKF(c.stateButton == 0, "style %d state 0x%lx", i, c.stateButton); xw_kill(w); }
}
XT(button_state_invisible_for_simple) {
    NEED(NtUserGetComboBoxInfo);
    HWND w = mkcombo(CBS_SIMPLE); COMBOBOXINFO c; XOK(q(w, &c));
    EXPECT_EQ(c.stateButton, STATE_INVIS);
    xw_kill(w);
}
XT(disabling_the_combo_does_not_change_the_button_state) {
    NEED(NtUserGetComboBoxInfo);
    DWORD s[] = { CBS_DROPDOWN, CBS_DROPDOWNLIST, CBS_SIMPLE };
    for (int i = 0; i < 3; i++) {
        HWND w = mkcombo(s[i]); COMBOBOXINFO a, b; XOK(q(w, &a));
        U(EnableWindow)(w, FALSE); XOK(q(w, &b));
        CHECKF(a.stateButton == b.stateButton, "style %d: 0x%lx -> 0x%lx", i, a.stateButton, b.stateButton);
        xw_kill(w);
    }
}
XT(simple_style_has_no_button_rectangle_and_a_full_width_item) {
    NEED(NtUserGetComboBoxInfo);
    HWND w = mkcombo(CBS_SIMPLE); COMBOBOXINFO c; RECT cr; XOK(q(w, &c)); U(GetClientRect)(w, &cr);
    CHECK(c.rcButton.left == 0 && c.rcButton.top == 0 && c.rcButton.right == 0 && c.rcButton.bottom == 0);
    EXPECT_EQ(c.rcItem.left, c.rcItem.top);                 /* the same inset on the left and the top */
    EXPECT_EQ(cr.right - c.rcItem.right, c.rcItem.left);    /* and on the right */
    xw_kill(w);
}
XT(widening_moves_the_button_with_the_right_edge) {
    NEED(NtUserGetComboBoxInfo);
    HWND w = mkcombo(CBS_DROPDOWN); COMBOBOXINFO a, b; XOK(q(w, &a));
    U(SetWindowPos)(w, NULL, 0, 0, 150, 200, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    XOK(q(w, &b));
    EXPECT_EQ(b.rcButton.left - a.rcButton.left, 50);
    EXPECT_EQ(b.rcButton.right - a.rcButton.right, 50);
    EXPECT_EQ(b.rcItem.left, a.rcItem.left);
    EXPECT_EQ(b.rcItem.right - a.rcItem.right, 50);
    xw_kill(w);
}
XT(moving_does_not_change_client_relative_rectangles) {
    NEED(NtUserGetComboBoxInfo);
    HWND w = mkcombo(CBS_DROPDOWN); COMBOBOXINFO a, b; XOK(q(w, &a));
    U(SetWindowPos)(w, NULL, 300, 400, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    XOK(q(w, &b));
    CHECK(!memcmp(&a.rcItem, &b.rcItem, sizeof a.rcItem)); CHECK(!memcmp(&a.rcButton, &b.rcButton, sizeof a.rcButton));
    xw_kill(w);
}
XT(item_rectangle_matches_edit_control_position) {
    NEED(NtUserGetComboBoxInfo);
    HWND w = mkcombo(CBS_DROPDOWN); COMBOBOXINFO c; XOK(q(w, &c));
    RECT er; U(GetWindowRect)(c.hwndItem, &er);
    POINT tl = { er.left, er.top }; U(ScreenToClient)(w, &tl);
    EXPECT_EQ(tl.x, c.rcItem.left); EXPECT_EQ(tl.y, c.rcItem.top);
    xw_kill(w);
}

/* ---- other thread, front door, determinism ---- */
XT(combo_of_another_thread_is_queried_like_a_local_one) {
    NEED(NtUserGetComboBoxInfo);
    struct xw_other o;
    if (!xw_other_start(&o, WS_POPUP | CBS_DROPDOWN, 0, L"COMBOBOX")) { t_skip("helper thread window"); return; }
    COMBOBOXINFO c; XOK(q(o.hwnd, &c));
    CHECK(c.hwndCombo == o.hwnd); CHECK(c.hwndItem && c.hwndList);
    xw_other_stop(&o);
}
XT(user32_front_door_matches) {
    NEED(NtUserGetComboBoxInfo);
    HWND w = mkcombo(CBS_DROPDOWN); COMBOBOXINFO a, b; XOK(q(w, &a));
    memset(&b, XFILL, sizeof b); b.cbSize = CB_CB;
    CHECK(U(GetComboBoxInfo)(w, &b)); CHECK(!memcmp(&a, &b, sizeof a));
    xw_kill(w);
}
XT(user32_front_door_wrong_size_fails) {
    NEED(NtUserGetComboBoxInfo);
    HWND w = mkcombo(CBS_DROPDOWN); union { COMBOBOXINFO c; unsigned char raw[0x100]; } u; memset(&u, 0, sizeof u); u.c.cbSize = CB_CB + 8;
    SET_ERR(XSENT); CHECK(!U(GetComboBoxInfo)(w, &u.c)); EXPECT_ERR(XE_INVALID_PARAMETER);
    xw_kill(w);
}
XT(output_does_not_depend_on_caller_garbage) {
    NEED(NtUserGetComboBoxInfo);
    HWND w = mkcombo(CBS_DROPDOWN); COMBOBOXINFO a, b;
    memset(&a, 0xFF, sizeof a); a.cbSize = CB_CB; memset(&b, 0, sizeof b); b.cbSize = CB_CB;
    XOK(UN(NtUserGetComboBoxInfo)(w, &a)); XOK(UN(NtUserGetComboBoxInfo)(w, &b));
    CHECK(!memcmp(&a, &b, sizeof a));
    xw_kill(w);
}
XT(repeated_queries_are_stable) {
    NEED(NtUserGetComboBoxInfo);
    HWND w = mkcombo(CBS_DROPDOWN); COMBOBOXINFO a, b; XOK(q(w, &a));
    int same = 1; for (int i = 0; i < 50; i++) { XOK(q(w, &b)); if (memcmp(&a, &b, sizeof a)) same = 0; }
    CHECK(same);
    xw_kill(w);
}
XT(two_combos_have_distinct_handles) {
    NEED(NtUserGetComboBoxInfo);
    HWND a = mkcombo(CBS_DROPDOWN), b = mkcombo(CBS_DROPDOWN); COMBOBOXINFO ca, cb;
    XOK(q(a, &ca)); XOK(q(b, &cb));
    CHECK(ca.hwndCombo != cb.hwndCombo); CHECK(ca.hwndItem != cb.hwndItem); CHECK(ca.hwndList != cb.hwndList);
    xw_kill(a); xw_kill(b);
}
XT(destroying_the_combo_invalidates_the_inner_windows) {
    NEED(NtUserGetComboBoxInfo);
    HWND w = mkcombo(CBS_DROPDOWN); COMBOBOXINFO c; XOK(q(w, &c));
    xw_kill(w);
    CHECK(!U(IsWindow)(c.hwndItem)); CHECK(!U(IsWindow)(c.hwndList));
    COMBOBOXINFO d; memset(&d, XFILL, sizeof d); d.cbSize = CB_CB;
    XFAIL(UN(NtUserGetComboBoxInfo)(w, &d), XE_INVALID_HWND);
}
