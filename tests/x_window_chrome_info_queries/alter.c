/* x_window_chrome_info_queries / alter: NtUserAlterWindowStyle(hwnd, mask, flags).
 *
 * Sets the style bits selected by mask to the matching bits of flags: style = (style & ~mask) | (flags & mask), but only
 * for the bits the service lets through. Observed on Windows 10 22H2, independent of the window class: bits 0..5
 * (0x3f), bit 9 (0x200) and the two scroll bar styles WS_HSCROLL and WS_VSCROLL (0x300000) are changeable; every other
 * bit - visibility, disabled, frame, caption, clip, tab stop, group, maximize and minimize and the box styles - stays as
 * it was. The service answers 1 for every live window, changeable bits or not, 0 with ERROR_INVALID_WINDOW_HANDLE for a
 * stale one, works on windows of other threads, and leaves the extended style alone.
 *
 * Every test restores the style bits it touched. Determinism: only symbolic results are printed.
 */
#define XPFX "alter_"
#include "x.h"

#define ALTER_OK_MASK (0x0030003Fu | 0x200u)

static LONG sty(HWND w) { return U(GetWindowLongW)(w, GWL_STYLE); }
static void alter_bit(const WCHAR *cls, int bit) {
    NEED(NtUserAlterWindowStyle);
    HWND w = xw_win(WS_OVERLAPPEDWINDOW, 0, cls);
    if (!w) { t_skip("window creation failed"); return; }
    DWORD m = 1u << bit; int changeable = (ALTER_OK_MASK & m) != 0;
    DWORD before = (DWORD)sty(w);
    XOK(UN(NtUserAlterWindowStyle)(w, m, m));
    DWORD set = (DWORD)sty(w);
    XOK(UN(NtUserAlterWindowStyle)(w, m, 0));
    DWORD cleared = (DWORD)sty(w);
    if (changeable) {
        CHECKF(set == (before | m), "set: 0x%lx from 0x%lx", (unsigned long)set, (unsigned long)before);
        CHECKF(cleared == (before & ~m), "clear: 0x%lx", (unsigned long)cleared);
    } else {
        CHECKF(set == before, "bit %d changed the style", bit);
        CHECKF(cleared == before, "bit %d changed the style", bit);
    }
    UN(NtUserAlterWindowStyle)(w, ALTER_OK_MASK, before);   /* restore the changeable bits */
    xw_kill(w);
}
XT(every_style_bit_on_a_static_window) { for (int bit = 0; bit < 32; bit++) alter_bit(L"STATIC", bit); }
XT(every_style_bit_on_a_button_window) { for (int bit = 0; bit < 32; bit++) alter_bit(L"BUTTON", bit); }
XT(selected_bits_on_edit_listbox_and_scrollbar_classes) {
    static const WCHAR *cls[] = { L"EDIT", L"LISTBOX", L"SCROLLBAR" }; static const int bits[] = { 0, 9, 21, 16, 30 };
    for (unsigned c = 0; c < 3; c++) for (unsigned b = 0; b < 5; b++) alter_bit(cls[c], bits[b]);
}

XT(low_nibble_semantics) {
    NEED(NtUserAlterWindowStyle);
    HWND w = xw_win(WS_OVERLAPPEDWINDOW, 0, L"STATIC"); LONG base = sty(w) & ~0xF;
    XOK(UN(NtUserAlterWindowStyle)(w, 0xF, 0x5)); EXPECT_EQ(sty(w), base | 0x5);
    XOK(UN(NtUserAlterWindowStyle)(w, 0xF, 0xA)); EXPECT_EQ(sty(w), base | 0xA);
    XOK(UN(NtUserAlterWindowStyle)(w, 0x3, 0xC)); EXPECT_EQ(sty(w), base | 0x8);   /* mask 3 clears the two low bits, 0xC & 3 sets none */
    XOK(UN(NtUserAlterWindowStyle)(w, 0x0, 0xF)); EXPECT_EQ(sty(w), base | 0x8);   /* an empty mask changes nothing */
    XOK(UN(NtUserAlterWindowStyle)(w, 0xF, 0x0)); EXPECT_EQ(sty(w), base);
    xw_kill(w);
}
XT(flags_outside_the_mask_are_ignored) {
    NEED(NtUserAlterWindowStyle);
    HWND w = xw_win(WS_OVERLAPPEDWINDOW, 0, L"STATIC"); LONG base = sty(w);
    XOK(UN(NtUserAlterWindowStyle)(w, 0x1, 0x6)); EXPECT_EQ(sty(w), base);      /* flags 6 has no bit inside mask 1 */
    XOK(UN(NtUserAlterWindowStyle)(w, 0x6, 0x7)); EXPECT_EQ(sty(w), base | 0x6);
    UN(NtUserAlterWindowStyle)(w, 0x6, 0);
    EXPECT_EQ(sty(w), base);
    xw_kill(w);
}
XT(unchangeable_bits_in_a_mixed_mask_are_dropped_not_the_whole_call) {
    NEED(NtUserAlterWindowStyle);
    HWND w = xw_win(WS_OVERLAPPEDWINDOW, 0, L"STATIC"); LONG base = sty(w);
    XOK(UN(NtUserAlterWindowStyle)(w, WS_VISIBLE | WS_DISABLED | 0x1, WS_VISIBLE | WS_DISABLED | 0x1));
    EXPECT_EQ(sty(w), base | 0x1);
    CHECK(!U(IsWindowVisible)(w)); CHECK(U(IsWindowEnabled)(w));
    UN(NtUserAlterWindowStyle)(w, 0x1, 0);
    xw_kill(w);
}
XT(all_ones_mask_changes_only_the_changeable_bits) {
    NEED(NtUserAlterWindowStyle);
    HWND w = xw_win(WS_OVERLAPPEDWINDOW, 0, L"STATIC"); LONG base = sty(w);
    XOK(UN(NtUserAlterWindowStyle)(w, 0xFFFFFFFF, 0xFFFFFFFF)); LONG all = sty(w);
    EXPECT_EQ(all, base | ALTER_OK_MASK);
    XOK(UN(NtUserAlterWindowStyle)(w, 0xFFFFFFFF, 0)); LONG none = sty(w);
    EXPECT_EQ(none, base & ~ALTER_OK_MASK);
    UN(NtUserAlterWindowStyle)(w, ALTER_OK_MASK, base);
    EXPECT_EQ(sty(w), base);
    xw_kill(w);
}
XT(high_halves_of_the_arguments_are_ignored) {
    NEED(NtUserAlterWindowStyle);
    HWND w = xw_win(WS_OVERLAPPEDWINDOW, 0, L"STATIC"); LONG base = sty(w);
    XOK(UN(NtUserAlterWindowStyle)(w, 0x1234567800000001ull, 0xABCDEF0100000001ull)); EXPECT_EQ(sty(w), base | 0x1);
    UN(NtUserAlterWindowStyle)(w, 0x1, 0);
    xw_kill(w);
}
XT(extended_style_is_not_touched) {
    NEED(NtUserAlterWindowStyle);
    HWND w = xw_win(WS_OVERLAPPEDWINDOW, WS_EX_TOOLWINDOW, L"STATIC"); LONG ex = U(GetWindowLongW)(w, GWL_EXSTYLE);
    UN(NtUserAlterWindowStyle)(w, 0xFFFFFFFF, 0xFFFFFFFF);
    EXPECT_EQ(U(GetWindowLongW)(w, GWL_EXSTYLE), ex);
    UN(NtUserAlterWindowStyle)(w, 0xFFFFFFFF, 0);
    xw_kill(w);
}
XT(window_stays_alive_and_hidden) {
    NEED(NtUserAlterWindowStyle);
    HWND w = xw_win(WS_OVERLAPPEDWINDOW, 0, L"STATIC");
    UN(NtUserAlterWindowStyle)(w, 0xFFFFFFFF, 0xFFFFFFFF); UN(NtUserAlterWindowStyle)(w, 0xFFFFFFFF, 0);
    CHECK(U(IsWindow)(w)); CHECK(!U(IsWindowVisible)(w));
    xw_kill(w);
}
XT(user_data_and_class_are_untouched) {
    NEED(NtUserAlterWindowStyle);
    HWND w = xw_win(WS_OVERLAPPEDWINDOW, 0, L"STATIC"); U(SetWindowLongPtrW)(w, GWLP_USERDATA, 0x1234);
    UN(NtUserAlterWindowStyle)(w, 0xFFFFFFFF, 0xFFFFFFFF);
    EXPECT_EQ(U(GetWindowLongPtrW)(w, GWLP_USERDATA), 0x1234);
    UN(NtUserAlterWindowStyle)(w, 0xFFFFFFFF, 0);
    xw_kill(w);
}
XT(changing_the_scroll_styles_shows_the_bars_to_info_queries) {
    NEED(NtUserAlterWindowStyle);
    HWND w = xw_win(WS_OVERLAPPEDWINDOW, 0, L"STATIC");
    XOK(UN(NtUserAlterWindowStyle)(w, WS_VSCROLL, WS_VSCROLL));
    CHECK(sty(w) & WS_VSCROLL);
    SCROLLBARINFO s; memset(&s, 0, sizeof s); s.cbSize = sizeof s;
    CHECK(U(GetScrollBarInfo)(w, OBJID_VSCROLL, &s));
    XOK(UN(NtUserAlterWindowStyle)(w, WS_VSCROLL, 0));
    CHECK(!(sty(w) & WS_VSCROLL));
    xw_kill(w);
}

/* ---- handles ---- */
XT(stale_or_garbage_window_is_invalid_window_handle) {
    NEED(NtUserAlterWindowStyle);
    XFAIL(UN(NtUserAlterWindowStyle)(NULL, 1, 1), XE_INVALID_HWND);
    XFAIL(UN(NtUserAlterWindowStyle)((HWND)(ULONG_PTR)0x12345678, 1, 1), XE_INVALID_HWND);
    XFAIL(UN(NtUserAlterWindowStyle)((HWND)INVALID_HANDLE_VALUE, 1, 1), XE_INVALID_HWND);
    XFAIL(UN(NtUserAlterWindowStyle)(xw_dead(), 1, 1), XE_INVALID_HWND);
    XFAIL(UN(NtUserAlterWindowStyle)((HWND)(ULONG_PTR)1, 1, 1), XE_INVALID_HWND);
}
XT(window_checked_before_arguments) {
    NEED(NtUserAlterWindowStyle);
    XFAIL(UN(NtUserAlterWindowStyle)(NULL, 0, 0), XE_INVALID_HWND);
    XFAIL(UN(NtUserAlterWindowStyle)(NULL, 0xFFFFFFFF, 0xFFFFFFFF), XE_INVALID_HWND);
}
XT(empty_mask_succeeds_for_a_live_window) {
    NEED(NtUserAlterWindowStyle);
    HWND w = xw_win(WS_OVERLAPPEDWINDOW, 0, L"STATIC"); LONG base = sty(w);
    XOK(UN(NtUserAlterWindowStyle)(w, 0, 0)); EXPECT_EQ(sty(w), base);
    xw_kill(w);
}
XT(child_window_bits_follow_the_same_rule) {
    NEED(NtUserAlterWindowStyle);
    HWND p = xw_win(WS_OVERLAPPEDWINDOW, 0, L"STATIC"), c = xw_child(p, 0, L"STATIC"); LONG base = sty(c);
    XOK(UN(NtUserAlterWindowStyle)(c, 0x1, 0x1)); EXPECT_EQ(sty(c), base | 0x1);
    XOK(UN(NtUserAlterWindowStyle)(c, WS_VISIBLE | WS_DISABLED, WS_VISIBLE | WS_DISABLED)); EXPECT_EQ(sty(c), base | 0x1);
    UN(NtUserAlterWindowStyle)(c, 0x1, 0);
    xw_kill(p);
}
XT(window_of_another_thread_is_altered) {
    NEED(NtUserAlterWindowStyle);
    struct xw_other o;
    if (!xw_other_start(&o, WS_OVERLAPPEDWINDOW, 0, L"STATIC")) { t_skip("helper thread window"); return; }
    LONG base = sty(o.hwnd);
    XOK(UN(NtUserAlterWindowStyle)(o.hwnd, 0x1, 0x1)); EXPECT_EQ(sty(o.hwnd), base | 0x1);
    XOK(UN(NtUserAlterWindowStyle)(o.hwnd, 0x1, 0x0)); EXPECT_EQ(sty(o.hwnd), base);
    xw_other_stop(&o);
}
XT(desktop_window_is_accepted_without_change) {
    NEED(NtUserAlterWindowStyle);
    HWND d = U(GetDesktopWindow)(); LONG base = sty(d);
    SET_ERR(XSENT);
    UN(NtUserAlterWindowStyle)(d, WS_VISIBLE, WS_VISIBLE);
    EXPECT_EQ(sty(d), base);   /* visibility is not changeable, so nothing moves whatever the answer is */
}
