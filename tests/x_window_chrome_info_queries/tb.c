/* x_window_chrome_info_queries / tb: NtUserGetTitleBarInfo(hwnd, TITLEBARINFO *).
 *
 * Measures, on hidden windows:
 *  - the argument protocol: cbSize must be exactly sizeof(TITLEBARINFO) (0x2c), otherwise ERROR_INVALID_PARAMETER with the
 *    output untouched; a stale or garbage window is ERROR_INVALID_WINDOW_HANDLE with the output untouched; an unwritable
 *    output is ERROR_NOACCESS;
 *  - the state array against the window style: element 0 is the bar itself (focusable), element 1 is the reserved slot
 *    (always invisible on a captioned window), 2 minimize, 3 maximize, 4 help, 5 close. Buttons exist only with a system
 *    menu; the minimize and maximize buttons need their style bit, and when only one of the two bits is set the other
 *    button is shown unavailable; the help button needs the context-help extended style and neither box; a grayed Close
 *    item of the system menu makes the close button unavailable while grayed Minimize and Maximize items do not change
 *    their buttons; a disabled or hidden window does not change the answer;
 *  - windows without a caption: only the focusable and invisible bits and an empty rectangle;
 *  - the rectangle against GetWindowRect and ClientToScreen: it starts a frame width inside the window on the top and
 *    right, is as high as the system caption (small caption for tool windows), ends where the client area begins when
 *    there is no menu bar, and leaves room for the system menu icon on the left only when the window has a system menu
 *    and is not a tool window.
 *
 * Determinism: only symbolic results are printed.
 */
#define XPFX "tb_"
#include "x.h"

#define TB_CB 0x2c
struct tbi { DWORD cb; RECT rc; DWORD st[6]; };

static BOOL q(HWND w, struct tbi *t) {
    memset(t, XFILL, sizeof *t); t->cb = TB_CB;
    SET_ERR(XSENT);
    return R32(UN(NtUserGetTitleBarInfo)(w, t)) != 0;
}
static void client_screen(HWND w, RECT *cs) {
    POINT o = { 0, 0 }; RECT cr; U(ClientToScreen)(w, &o); U(GetClientRect)(w, &cr);
    cs->left = o.x; cs->top = o.y; cs->right = o.x + cr.right; cs->bottom = o.y + cr.bottom;
}

XT(struct_size_is_0x2c) { EXPECT_EQ(sizeof(TITLEBARINFO), TB_CB); }

/* ---- argument protocol ---- */
static void bad_cb(DWORD cb) {
    HWND w = xw_win(WS_OVERLAPPEDWINDOW, 0, L"STATIC");
    union { struct tbi t; unsigned char raw[0x100]; } u; memset(&u, XFILL, sizeof u); u.t.cb = cb;
    SET_ERR(XSENT);
    ULONG_PTR r = UN(NtUserGetTitleBarInfo)(w, &u.t);
    CHECKF(R32(r) == 0 && GetLastError() == XE_INVALID_PARAMETER, "cbSize 0x%lx: r=%lu gle=%lu", (unsigned long)cb, (unsigned long)R32(r), GetLastError());
    CHECKF(u.t.cb == cb && xw_untouched(u.raw + 4, sizeof u.raw - 4), "cbSize 0x%lx: output written", (unsigned long)cb);
    xw_kill(w);
}
XT(cbsize_anything_but_0x2c_is_invalid) {
    NEED(NtUserGetTitleBarInfo);
    static const DWORD cbs[] = { 0, 1, 4, 0x2b, 0x2d, 0x30, 0x58, 0x2c0000, 0xFFFFFFFFu, 0x3c };
    for (unsigned i = 0; i < sizeof cbs / sizeof cbs[0]; i++) bad_cb(cbs[i]);
}
XT(cbsize_exact_accepted_and_kept) {
    NEED(NtUserGetTitleBarInfo);
    HWND w = xw_win(WS_OVERLAPPEDWINDOW, 0, L"STATIC"); struct tbi t; XOK(q(w, &t)); EXPECT_EQ(t.cb, TB_CB);
    xw_kill(w);
}
static void bad_hwnd(HWND h, const char *what) {
    struct tbi t; memset(&t, XFILL, sizeof t); t.cb = TB_CB;
    SET_ERR(XSENT); ULONG_PTR r = UN(NtUserGetTitleBarInfo)(h, &t);
    CHECKF(R32(r) == 0 && GetLastError() == XE_INVALID_HWND, "%s: r=%lu gle=%lu", what, (unsigned long)R32(r), GetLastError());
    CHECKF(xw_untouched(&t.rc, sizeof t - 4), "%s: output written", what);
}
XT(hwnd_stale_or_garbage_is_invalid_window_handle) {
    NEED(NtUserGetTitleBarInfo);
    bad_hwnd(NULL, "null"); bad_hwnd((HWND)(ULONG_PTR)0x12345678, "garbage"); bad_hwnd((HWND)INVALID_HANDLE_VALUE, "invalid handle value");
    bad_hwnd(xw_dead(), "destroyed"); bad_hwnd((HWND)(ULONG_PTR)1, "low index");
}
XT(hwnd_checked_before_size) {
    NEED(NtUserGetTitleBarInfo);
    struct tbi t; memset(&t, XFILL, sizeof t); t.cb = 3;
    XFAIL(UN(NtUserGetTitleBarInfo)(NULL, &t), XE_INVALID_HWND);
}
XT(out_null) {
    NEED(NtUserGetTitleBarInfo);
    HWND w = xw_win(WS_OVERLAPPEDWINDOW, 0, L"STATIC");
    XFAIL(UN(NtUserGetTitleBarInfo)(w, NULL), XE_NOACCESS);
    xw_kill(w);
}
XT(out_unwritable) {
    NEED(NtUserGetTitleBarInfo);
    HWND w = xw_win(WS_OVERLAPPEDWINDOW, 0, L"STATIC");
    XFAIL(UN(NtUserGetTitleBarInfo)(w, XP_BADPTR), XE_NOACCESS);
    XFAIL(UN(NtUserGetTitleBarInfo)(w, XP_KERNELPTR), XE_NOACCESS);
    xw_kill(w);
}

/* ---- state array against the style ---- */
#define UA STATE_UNAVAIL
#define IV STATE_INVIS
static void states(DWORD style, DWORD ex, DWORD s2, DWORD s3, DWORD s4, DWORD s5) {
    NEED(NtUserGetTitleBarInfo);
    HWND w = xw_win(style, ex, L"STATIC");
    if (!w) { t_skip("window creation failed"); return; }
    struct tbi t; XOK(q(w, &t));
    CHECK(t.st[0] == STATE_FOCUS);
    CHECK(t.st[1] == IV);
    CHECKF(t.st[2] == s2, "minimize state 0x%lx want 0x%lx", t.st[2], s2);
    CHECKF(t.st[3] == s3, "maximize state 0x%lx want 0x%lx", t.st[3], s3);
    CHECKF(t.st[4] == s4, "help state 0x%lx want 0x%lx", t.st[4], s4);
    CHECKF(t.st[5] == s5, "close state 0x%lx want 0x%lx", t.st[5], s5);
    xw_kill(w);
}
#define CAP (WS_OVERLAPPED | WS_CAPTION)
#define ST(nm, style, ex, s2, s3, s4, s5) XT(states_##nm) { states(style, ex, s2, s3, s4, s5); }
ST(overlappedwindow,            WS_OVERLAPPEDWINDOW, 0,                  0,  0,  IV, 0)
ST(style_zero_gets_a_caption,   WS_OVERLAPPED, 0,                        IV, IV, IV, IV)
ST(border_only_overlapped_gets_a_caption, WS_OVERLAPPED | WS_BORDER, 0,  IV, IV, IV, IV)
ST(tool_window_keeps_min_max_buttons, WS_OVERLAPPEDWINDOW, WS_EX_TOOLWINDOW, 0, 0, IV, 0)
ST(caption_only,                CAP, 0,                                  IV, IV, IV, IV)
ST(caption_sysmenu,             CAP | WS_SYSMENU, 0,                     IV, IV, IV, 0)
ST(caption_sysmenu_minbox,      CAP | WS_SYSMENU | WS_MINIMIZEBOX, 0,    0,  UA, IV, 0)
ST(caption_sysmenu_maxbox,      CAP | WS_SYSMENU | WS_MAXIMIZEBOX, 0,    UA, 0,  IV, 0)
ST(caption_sysmenu_both_boxes,  CAP | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX, 0, 0, 0, IV, 0)
ST(boxes_without_sysmenu,       CAP | WS_MINIMIZEBOX | WS_MAXIMIZEBOX, 0, IV, IV, IV, IV)
ST(popup_caption_sysmenu,       WS_POPUP | WS_CAPTION | WS_SYSMENU, 0,   IV, IV, IV, 0)
ST(popup_caption_sysmenu_boxes, WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX, 0, 0, 0, IV, 0)
ST(thickframe_does_not_add_buttons, CAP | WS_THICKFRAME | WS_SYSMENU, 0, IV, IV, IV, 0)
ST(help_button_with_context_help, CAP | WS_SYSMENU, WS_EX_CONTEXTHELP,  IV, IV, 0,  0)
ST(help_button_hidden_by_boxes, WS_OVERLAPPEDWINDOW, WS_EX_CONTEXTHELP,  0,  0,  IV, 0)
ST(help_button_hidden_by_minbox_only, CAP | WS_SYSMENU | WS_MINIMIZEBOX, WS_EX_CONTEXTHELP, 0, UA, IV, 0)
ST(dlgmodalframe_caption_sysmenu, CAP | WS_SYSMENU, WS_EX_DLGMODALFRAME, IV, IV, IV, 0)
ST(clipchildren_visible_bits_do_not_matter, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 0, IV, 0)

static void states_after(void (*mut)(HWND), DWORD s2, DWORD s3, DWORD s5) {
    NEED(NtUserGetTitleBarInfo);
    HWND w = xw_win(WS_OVERLAPPEDWINDOW, 0, L"STATIC");
    mut(w);
    struct tbi t; XOK(q(w, &t));
    CHECK(t.st[0] == STATE_FOCUS); CHECK(t.st[1] == IV); CHECK(t.st[4] == IV);
    CHECKF(t.st[2] == s2, "minimize 0x%lx", t.st[2]); CHECKF(t.st[3] == s3, "maximize 0x%lx", t.st[3]); CHECKF(t.st[5] == s5, "close 0x%lx", t.st[5]);
    xw_kill(w);
}
static void m_close_gray(HWND w) { U(EnableMenuItem)(U(GetSystemMenu)(w, FALSE), SC_CLOSE, MF_BYCOMMAND | MF_GRAYED); }
static void m_min_gray(HWND w) { U(EnableMenuItem)(U(GetSystemMenu)(w, FALSE), SC_MINIMIZE, MF_BYCOMMAND | MF_GRAYED); }
static void m_max_gray(HWND w) { U(EnableMenuItem)(U(GetSystemMenu)(w, FALSE), SC_MAXIMIZE, MF_BYCOMMAND | MF_GRAYED); }
static void m_close_disabled(HWND w) { U(EnableMenuItem)(U(GetSystemMenu)(w, FALSE), SC_CLOSE, MF_BYCOMMAND | MF_DISABLED); }
static void m_close_regray_reset(HWND w) { m_close_gray(w); U(GetSystemMenu)(w, TRUE); }
static void m_disable_window(HWND w) { U(EnableWindow)(w, FALSE); }
static void m_remove_minbox(HWND w) { U(SetWindowLongW)(w, GWL_STYLE, U(GetWindowLongW)(w, GWL_STYLE) & ~WS_MINIMIZEBOX); }
static void m_remove_maxbox(HWND w) { U(SetWindowLongW)(w, GWL_STYLE, U(GetWindowLongW)(w, GWL_STYLE) & ~WS_MAXIMIZEBOX); }
static void m_remove_sysmenu(HWND w) { U(SetWindowLongW)(w, GWL_STYLE, U(GetWindowLongW)(w, GWL_STYLE) & ~WS_SYSMENU); }
XT(states_follow_the_close_item_of_the_system_menu) { states_after(m_close_gray, 0, 0, UA); }
XT(states_close_item_disabled_not_grayed) { states_after(m_close_disabled, 0, 0, UA); }
XT(states_close_item_restored_by_menu_reset) { states_after(m_close_regray_reset, 0, 0, 0); }
XT(states_ignore_grayed_minimize_item) { states_after(m_min_gray, 0, 0, 0); }
XT(states_ignore_grayed_maximize_item) { states_after(m_max_gray, 0, 0, 0); }
XT(states_ignore_disabled_window) { states_after(m_disable_window, 0, 0, 0); }
XT(states_style_change_removes_minbox) { states_after(m_remove_minbox, UA, 0, 0); }
XT(states_style_change_removes_maxbox) { states_after(m_remove_maxbox, 0, UA, 0); }
XT(states_style_change_removes_sysmenu) { states_after(m_remove_sysmenu, IV, IV, IV); }

XT(states_do_not_depend_on_visibility_or_position) {
    NEED(NtUserGetTitleBarInfo);
    HWND w = xw_win(WS_OVERLAPPEDWINDOW, 0, L"STATIC"); struct tbi a, b;
    XOK(q(w, &a));
    U(SetWindowPos)(w, NULL, -20000, -20000, 300, 300, SWP_NOZORDER | SWP_NOACTIVATE);
    XOK(q(w, &b));
    CHECK(!memcmp(a.st, b.st, sizeof a.st));
    xw_kill(w);
}

/* ---- windows without a caption ---- */
static void no_caption(DWORD style, DWORD ex) {
    NEED(NtUserGetTitleBarInfo);
    HWND w = xw_win(style, ex, L"STATIC");
    if (!w) { t_skip("window creation failed"); return; }
    struct tbi t; XOK(q(w, &t));
    CHECK(t.st[0] == (STATE_FOCUS | IV));
    for (int i = 1; i < 6; i++) CHECKF(t.st[i] == 0, "state[%d]=0x%lx", i, t.st[i]);
    CHECK(t.rc.left == 0 && t.rc.top == 0 && t.rc.right == 0 && t.rc.bottom == 0);
    EXPECT_EQ(t.cb, TB_CB);
    xw_kill(w);
}
XT(styles_without_a_caption_have_an_empty_focusable_invisible_bar) {
    static const struct { DWORD st, ex; } rows[] = { { WS_POPUP, 0 }, { WS_POPUP | WS_BORDER, 0 }, { WS_POPUP | WS_DLGFRAME, 0 }, { WS_POPUP | WS_THICKFRAME, 0 },
        { WS_POPUP | WS_SYSMENU, 0 }, { WS_POPUP, WS_EX_TOOLWINDOW }, { WS_POPUP, WS_EX_CLIENTEDGE } };
    for (unsigned i = 0; i < sizeof rows / sizeof rows[0]; i++) no_caption(rows[i].st, rows[i].ex);
}

XT(child_window_is_focusable_and_invisible_with_empty_rect) {
    NEED(NtUserGetTitleBarInfo);
    HWND p = xw_win(WS_OVERLAPPEDWINDOW, 0, L"STATIC"); HWND c = xw_child(p, 0, L"STATIC");
    struct tbi t; XOK(q(c, &t));
    CHECK(t.st[0] == (STATE_FOCUS | IV));
    for (int i = 1; i < 6; i++) CHECK(t.st[i] == 0);
    CHECK(t.rc.left == 0 && t.rc.top == 0 && t.rc.right == 0 && t.rc.bottom == 0);
    xw_kill(p);
}
XT(child_with_caption_style_has_a_title_bar_of_its_own) {
    NEED(NtUserGetTitleBarInfo);
    HWND p = xw_win(WS_OVERLAPPEDWINDOW, 0, L"STATIC"); HWND c = xw_child(p, WS_CAPTION | WS_SYSMENU, L"STATIC");
    struct tbi t; XOK(q(c, &t));
    CHECK(t.st[0] == STATE_FOCUS); CHECK(t.st[1] == IV); CHECK(t.st[2] == IV && t.st[3] == IV && t.st[4] == IV); CHECK(t.st[5] == 0);
    CHECK(t.rc.right > t.rc.left && t.rc.bottom > t.rc.top);
    xw_kill(p);
}

/* ---- geometry ---- */
static void geom_basic(DWORD style, DWORD ex) {
    NEED(NtUserGetTitleBarInfo);
    HWND w = xw_win(style, ex, L"STATIC"); struct tbi t; RECT wr, cs;
    XOK(q(w, &t)); U(GetWindowRect)(w, &wr); client_screen(w, &cs);
    int frame = cs.left - wr.left;                       /* the left frame width, equal to the top frame */
    EXPECT_EQ(t.rc.top, wr.top + frame);
    EXPECT_EQ(t.rc.right, wr.right - frame);
    EXPECT_EQ(t.rc.bottom, cs.top);                      /* the client area starts below the caption (no menu bar) */
    CHECK(t.rc.left >= wr.left + frame && t.rc.left < t.rc.right);
    CHECK(t.rc.bottom > t.rc.top);
    xw_kill(w);
}
XT(geometry_relations_hold_for_every_captioned_style) {
    static const struct { DWORD st, ex; } rows[] = { { WS_OVERLAPPEDWINDOW, 0 }, { CAP, 0 }, { CAP | WS_SYSMENU, 0 }, { WS_POPUP | WS_CAPTION | WS_SYSMENU, 0 },
        { WS_OVERLAPPEDWINDOW, WS_EX_TOOLWINDOW }, { CAP | WS_SYSMENU, WS_EX_DLGMODALFRAME }, { CAP | WS_THICKFRAME | WS_SYSMENU, 0 },
        { WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_HSCROLL, 0 } };
    for (unsigned i = 0; i < sizeof rows / sizeof rows[0]; i++) geom_basic(rows[i].st, rows[i].ex);
}

XT(height_is_the_system_caption_height) {
    NEED(NtUserGetTitleBarInfo);
    HWND w = xw_win(WS_OVERLAPPEDWINDOW, 0, L"STATIC"); struct tbi t; XOK(q(w, &t));
    EXPECT_EQ(t.rc.bottom - t.rc.top, U(GetSystemMetrics)(SM_CYCAPTION));
    xw_kill(w);
}
XT(tool_window_height_is_the_small_caption_height) {
    NEED(NtUserGetTitleBarInfo);
    HWND w = xw_win(WS_OVERLAPPEDWINDOW, WS_EX_TOOLWINDOW, L"STATIC"); struct tbi t; XOK(q(w, &t));
    EXPECT_EQ(t.rc.bottom - t.rc.top, U(GetSystemMetrics)(SM_CYSMCAPTION));
    xw_kill(w);
}
XT(system_menu_icon_shifts_the_left_edge) {
    NEED(NtUserGetTitleBarInfo);
    HWND a = xw_win(CAP | WS_SYSMENU, 0, L"STATIC"), b = xw_win(CAP, 0, L"STATIC");
    struct tbi ta, tb; RECT wa, wb; XOK(q(a, &ta)); XOK(q(b, &tb)); U(GetWindowRect)(a, &wa); U(GetWindowRect)(b, &wb);
    CHECK(ta.rc.left - wa.left > tb.rc.left - wb.left);
    EXPECT_EQ(ta.rc.right - wa.right, tb.rc.right - wb.right);
    xw_kill(a); xw_kill(b);
}
XT(tool_window_has_no_icon_gap) {
    NEED(NtUserGetTitleBarInfo);
    HWND a = xw_win(CAP | WS_SYSMENU, WS_EX_TOOLWINDOW, L"STATIC"), b = xw_win(CAP, WS_EX_TOOLWINDOW, L"STATIC");
    struct tbi ta, tb; RECT wa, wb; XOK(q(a, &ta)); XOK(q(b, &tb)); U(GetWindowRect)(a, &wa); U(GetWindowRect)(b, &wb);
    EXPECT_EQ(ta.rc.left - wa.left, tb.rc.left - wb.left);
    xw_kill(a); xw_kill(b);
}
XT(width_follows_window_width) {
    NEED(NtUserGetTitleBarInfo);
    HWND w = xw_win(WS_OVERLAPPEDWINDOW, 0, L"STATIC"); struct tbi a, b; XOK(q(w, &a));
    U(SetWindowPos)(w, NULL, 0, 0, 300, 150, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    XOK(q(w, &b));
    EXPECT_EQ((b.rc.right - b.rc.left) - (a.rc.right - a.rc.left), 100);
    EXPECT_EQ(b.rc.bottom - b.rc.top, a.rc.bottom - a.rc.top);
    xw_kill(w);
}
XT(rect_follows_window_move) {
    NEED(NtUserGetTitleBarInfo);
    HWND w = xw_win(WS_OVERLAPPEDWINDOW, 0, L"STATIC"); struct tbi a, b; XOK(q(w, &a));
    U(SetWindowPos)(w, NULL, 60, 90, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    XOK(q(w, &b));
    EXPECT_EQ(b.rc.left - a.rc.left, 50); EXPECT_EQ(b.rc.top - a.rc.top, 80);
    EXPECT_EQ(b.rc.right - a.rc.right, 50); EXPECT_EQ(b.rc.bottom - a.rc.bottom, 80);
    xw_kill(w);
}
XT(menu_bar_sits_between_title_bar_and_client) {
    NEED(NtUserGetTitleBarInfo);
    HWND w = xw_win(WS_OVERLAPPEDWINDOW, 0, L"STATIC"); HMENU m = U(CreateMenu)(); U(AppendMenuW)(m, MF_STRING, 1, L"File");
    U(SetMenu)(w, m);
    struct tbi t; RECT cs; XOK(q(w, &t)); client_screen(w, &cs);
    CHECK(t.rc.bottom < cs.top);
    xw_kill(w);
}
XT(title_bar_unchanged_by_adding_a_menu) {
    NEED(NtUserGetTitleBarInfo);
    HWND w = xw_win(WS_OVERLAPPEDWINDOW, 0, L"STATIC"); struct tbi a, b; XOK(q(w, &a));
    HMENU m = U(CreateMenu)(); U(AppendMenuW)(m, MF_STRING, 1, L"File"); U(SetMenu)(w, m);
    XOK(q(w, &b));
    CHECK(!memcmp(&a.rc, &b.rc, sizeof a.rc));
    xw_kill(w);
}
XT(rect_is_inside_window_rectangle) {
    NEED(NtUserGetTitleBarInfo);
    HWND w = xw_win(WS_OVERLAPPEDWINDOW | WS_VSCROLL, 0, L"STATIC"); struct tbi t; RECT wr; XOK(q(w, &t)); U(GetWindowRect)(w, &wr);
    CHECK(t.rc.left >= wr.left && t.rc.top >= wr.top && t.rc.right <= wr.right && t.rc.bottom <= wr.bottom);
    xw_kill(w);
}

/* ---- other thread, front door, determinism ---- */
XT(window_of_another_thread_is_queried_like_a_local_one) {
    NEED(NtUserGetTitleBarInfo);
    struct xw_other o;
    if (!xw_other_start(&o, WS_OVERLAPPEDWINDOW, 0, L"STATIC")) { t_skip("helper thread window"); return; }
    struct tbi t; RECT wr, cs; XOK(q(o.hwnd, &t)); U(GetWindowRect)(o.hwnd, &wr); client_screen(o.hwnd, &cs);
    EXPECT_EQ(t.rc.bottom, cs.top); EXPECT_EQ(t.rc.right, wr.right - (cs.left - wr.left));
    CHECK(t.st[0] == STATE_FOCUS);
    xw_other_stop(&o);
}
XT(user32_front_door_matches) {
    NEED(NtUserGetTitleBarInfo);
    HWND w = xw_win(WS_OVERLAPPEDWINDOW, 0, L"STATIC"); struct tbi a, b; XOK(q(w, &a));
    memset(&b, XFILL, sizeof b); b.cb = TB_CB;
    CHECK(U(GetTitleBarInfo)(w, (PTITLEBARINFO)&b)); CHECK(!memcmp(&a, &b, sizeof a));
    xw_kill(w);
}
XT(user32_front_door_wrong_size_fails) {
    NEED(NtUserGetTitleBarInfo);
    HWND w = xw_win(WS_OVERLAPPEDWINDOW, 0, L"STATIC"); union { struct tbi t; unsigned char raw[0x80]; } u; memset(&u, 0, sizeof u); u.t.cb = TB_CB - 4;
    SET_ERR(XSENT); CHECK(!U(GetTitleBarInfo)(w, (PTITLEBARINFO)&u.t)); EXPECT_ERR(XE_INVALID_PARAMETER);
    xw_kill(w);
}
XT(output_does_not_depend_on_caller_garbage) {
    NEED(NtUserGetTitleBarInfo);
    HWND w = xw_win(WS_OVERLAPPEDWINDOW, 0, L"STATIC"); struct tbi a, b;
    memset(&a, 0xFF, sizeof a); a.cb = TB_CB; memset(&b, 0, sizeof b); b.cb = TB_CB;
    XOK(UN(NtUserGetTitleBarInfo)(w, &a)); XOK(UN(NtUserGetTitleBarInfo)(w, &b));
    CHECK(!memcmp(&a, &b, sizeof a));
    xw_kill(w);
}
XT(repeated_queries_are_stable) {
    NEED(NtUserGetTitleBarInfo);
    HWND w = xw_win(WS_OVERLAPPEDWINDOW, 0, L"STATIC"); struct tbi a, b; XOK(q(w, &a));
    int same = 1; for (int i = 0; i < 50; i++) { XOK(q(w, &b)); if (memcmp(&a, &b, sizeof a)) same = 0; }
    CHECK(same);
    xw_kill(w);
}
XT(hidden_and_shown_flags_leave_no_trace_in_rect) {
    NEED(NtUserGetTitleBarInfo);
    HWND w = xw_win(WS_OVERLAPPEDWINDOW, 0, L"STATIC"); struct tbi a, b; XOK(q(w, &a));
    U(SetWindowLongW)(w, GWL_STYLE, U(GetWindowLongW)(w, GWL_STYLE) | WS_VISIBLE);   /* visible bit only, nothing is shown or painted */
    XOK(q(w, &b));
    CHECK(!memcmp(&a, &b, sizeof a));
    xw_kill(w);
}
XT(size_grip_class_window_with_caption_style) {
    NEED(NtUserGetTitleBarInfo);
    HWND w = U(CreateWindowExW)(0, L"SCROLLBAR", L"", WS_POPUP | WS_CAPTION | SBS_SIZEGRIP, 10, 10, 100, 60, NULL, NULL, NULL, NULL);
    struct tbi t; XOK(q(w, &t)); CHECK(t.st[0] == STATE_FOCUS);
    xw_kill(w);
}
