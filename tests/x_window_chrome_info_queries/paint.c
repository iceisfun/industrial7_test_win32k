/* x_window_chrome_info_queries / paint: NtUserGetControlBrush, NtUserGetControlColor, NtUserFillWindow,
 * NtUserDrawCaptionTemp, NtUserDrawMenuBarTemp, NtUserPaintMenuBar.
 *
 * GetControlBrush(hwnd, hdc, msg) / GetControlColor(hwndParent, hwnd, hdc, msg): msg is one of WM_CTLCOLORMSGBOX..
 *   WM_CTLCOLORSTATIC (0x132..0x138). The message is sent to the parent of hwnd (to the window itself when it has none,
 *   or to hwndParent for GetControlColor) with the device context as wParam and the control as lParam; the brush the
 *   window procedure returns is the answer. Without an answer the default colours apply: the face colour for message
 *   box, button, dialog and static, the window colour for edit and list box, a dither brush for the scroll bar; the device
 *   context receives the matching text and background colours. A message outside the range, or a NULL device context,
 *   answers 0 without sending anything; a stale window is ERROR_INVALID_WINDOW_HANDLE.
 * FillWindow(hwndBrush, hwndPaint, hdc, hbr): fills the client rectangle of hwndPaint (from the device context origin)
 *   with a brush: hbr 0..6 selects the control colour of message 0x132+hbr asked from hwndBrush, hbr 7.. is a system colour
 *   index plus one, anything larger is a brush handle. Pixels outside the client rectangle are not touched.
 * DrawCaptionTemp(hwnd, hdc, rc, font, icon, text, flags): with DC_TEXT paints the caption background in the active or
 *   inactive caption colour (DC_ACTIVE), the face colour for DC_INBUTTON, a gradient to the second caption colour for
 *   DC_GRADIENT; other flags alone paint nothing; flag 0x4000 and all-ones are refused; NULL or empty rectangle and a NULL
 *   text with DC_TEXT are refused.
 * DrawMenuBarTemp and PaintMenuBar: the first answers 0 and draws nothing for hidden windows; the second answers the
 *   height of the menu bar as GetMenuBarInfo reports it, 0 for a window without a menu.
 *
 * Everything is drawn into memory device contexts. Determinism: only symbolic results are printed.
 */
#define XPFX "paint_"
#include "x.h"

/* ---- a parent class that records the control colour messages ---- */
static volatile int g_cnt; static volatile UINT g_last; static HWND g_lparam; static HDC g_wparam; static HBRUSH g_reply;
static LRESULT CALLBACK cp_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m >= 0x132 && m <= 0x138) { g_cnt++; g_last = m; g_lparam = (HWND)l; g_wparam = (HDC)w; if (g_reply) return (LRESULT)g_reply; }
    return U(DefWindowProcW)(h, m, w, l);
}
static int reg_done;
static const WCHAR *cp_class(void) {
    if (!reg_done) {
        WNDCLASSW wc; memset(&wc, 0, sizeof wc);
        wc.lpfnWndProc = cp_proc; wc.lpszClassName = L"xw_ctlparent"; wc.hInstance = GetModuleHandleW(NULL);
        U(RegisterClassW)(&wc); reg_done = 1;
    }
    return L"xw_ctlparent";
}
static HWND cparent(void) { return U(CreateWindowExW)(0, cp_class(), L"p", WS_POPUP, 10, 10, 200, 150, NULL, NULL, GetModuleHandleW(NULL), NULL); }
static void reset(HBRUSH reply) { g_cnt = 0; g_last = 0; g_lparam = NULL; g_wparam = NULL; g_reply = reply; }
static COLORREF sys(int i) { return U(GetSysColor)(i); }
static HBRUSH sysbr(int i) { return (HBRUSH)U(GetSysColorBrush)(i); }
static HBRUSH solid(COLORREF c) { return U(CreateSolidBrush)(c); }

/* ================= GetControlBrush ================= */
struct cbrow { UINT msg; int sysbrush; int bk, tx; };   /* sysbrush -1: not a system brush */
static const struct cbrow cb_rows[] = {
    { 0x132, COLOR_BTNFACE, COLOR_BTNFACE, COLOR_WINDOWTEXT },
    { 0x133, COLOR_WINDOW,  COLOR_WINDOW,  COLOR_WINDOWTEXT },
    { 0x134, COLOR_WINDOW,  COLOR_WINDOW,  COLOR_WINDOWTEXT },
    { 0x135, COLOR_BTNFACE, COLOR_BTNFACE, COLOR_WINDOWTEXT },
    { 0x136, COLOR_BTNFACE, COLOR_BTNFACE, COLOR_WINDOWTEXT },
    { 0x137, -1,            COLOR_WINDOW,  COLOR_BTNFACE },
    { 0x138, COLOR_BTNFACE, COLOR_BTNFACE, COLOR_WINDOWTEXT },
};
static void cb_default(int row) {
    NEED(NtUserGetControlBrush);
    const struct cbrow *r = &cb_rows[row];
    HWND p = cparent(), c = xw_child(p, 0, L"STATIC"); HDC dc = U(GetDC)(c);
    reset(NULL);
    U(SetBkColor)(dc, RGB(4, 5, 6)); U(SetTextColor)(dc, RGB(1, 2, 3));
    SET_ERR(XSENT);
    ULONG_PTR b = UN(NtUserGetControlBrush)(c, dc, r->msg);
    CHECK(b != 0);
    CHECK(U(GetObjectType)((HGDIOBJ)b) == OBJ_BRUSH);
    if (r->sysbrush >= 0) CHECK((HBRUSH)b == sysbr(r->sysbrush)); else CHECK((HBRUSH)b != sysbr(COLOR_BTNFACE) && (HBRUSH)b != sysbr(COLOR_WINDOW));
    CHECKF(U(GetBkColor)(dc) == sys(r->bk), "bk 0x%lx", (unsigned long)U(GetBkColor)(dc));
    CHECKF(U(GetTextColor)(dc) == sys(r->tx), "text 0x%lx", (unsigned long)U(GetTextColor)(dc));
    EXPECT_EQ(g_cnt, 1); EXPECT_EQ(g_last, r->msg);
    CHECK(g_lparam == c); CHECK(g_wparam == dc);
    U(ReleaseDC)(c, dc); xw_kill(p);
}
#define CBD(nm, row) XT(brush_default_##nm) { cb_default(row); }
CBD(msgbox_132, 0) CBD(edit_133, 1) CBD(listbox_134, 2) CBD(button_135, 3) CBD(dialog_136, 4) CBD(scrollbar_137, 5) CBD(static_138, 6)

static void cb_custom(UINT msg) {
    NEED(NtUserGetControlBrush);
    HWND p = cparent(), c = xw_child(p, 0, L"STATIC"); HDC dc = U(GetDC)(c); HBRUSH mine = solid(RGB(1, 2, 3));
    reset(mine);
    ULONG_PTR b = UN(NtUserGetControlBrush)(c, dc, msg);
    CHECK((HBRUSH)b == mine); EXPECT_EQ(g_cnt, 1); EXPECT_EQ(g_last, msg); CHECK(g_lparam == c);
    U(ReleaseDC)(c, dc); xw_kill(p); U(DeleteObject)(mine);
}
XT(brush_reply_of_the_parent_is_the_answer_for_every_message) { for (UINT m = 0x132; m <= 0x138; m++) cb_custom(m); }

XT(brush_top_level_window_asks_itself) {
    NEED(NtUserGetControlBrush);
    HWND p = cparent(); HDC dc = U(GetDC)(p); HBRUSH mine = solid(RGB(9, 8, 7));
    reset(mine);
    CHECK((HBRUSH)UN(NtUserGetControlBrush)(p, dc, 0x138) == mine); EXPECT_EQ(g_cnt, 1); CHECK(g_lparam == p);
    U(ReleaseDC)(p, dc); xw_kill(p); U(DeleteObject)(mine);
}
XT(brush_grandchild_asks_its_direct_parent_only) {
    NEED(NtUserGetControlBrush);
    HWND p = cparent(), c = xw_child(p, 0, L"STATIC"), g = xw_child(c, 0, L"STATIC"); HDC dc = U(GetDC)(g); HBRUSH mine = solid(RGB(9, 8, 7));
    reset(mine);
    ULONG_PTR b = UN(NtUserGetControlBrush)(g, dc, 0x138);
    CHECK((HBRUSH)b != mine);   /* the static parent answers with its default; the grandparent is never asked */
    EXPECT_EQ(g_cnt, 0);
    U(ReleaseDC)(g, dc); xw_kill(p); U(DeleteObject)(mine);
}
static void cb_nomsg(UINT msg) {
    NEED(NtUserGetControlBrush);
    HWND p = cparent(), c = xw_child(p, 0, L"STATIC"); HDC dc = U(GetDC)(c);
    reset(NULL);
    SET_ERR(XSENT);
    EXPECT_EQ(UN(NtUserGetControlBrush)(c, dc, msg), 0);
    EXPECT_ERR(XSENT); EXPECT_EQ(g_cnt, 0);
    U(ReleaseDC)(c, dc); xw_kill(p);
}
XT(brush_message_outside_the_range_answers_zero_without_sending) {
    static const UINT msgs[] = { 0, 0x111, 0x131, 0x139, 0x13a, 0x19, 0xFFFFFFFF, 0x400 };
    for (unsigned i = 0; i < sizeof msgs / sizeof msgs[0]; i++) cb_nomsg(msgs[i]);
}
XT(brush_null_dc) {
    NEED(NtUserGetControlBrush);
    HWND p = cparent(), c = xw_child(p, 0, L"STATIC"); reset(NULL);
    SET_ERR(XSENT); EXPECT_EQ(UN(NtUserGetControlBrush)(c, NULL, 0x138), 0); EXPECT_ERR(XSENT); EXPECT_EQ(g_cnt, 0);
    xw_kill(p);
}
XT(brush_stale_window) {
    NEED(NtUserGetControlBrush);
    HWND p = cparent(); HDC dc = U(GetDC)(p);
    XFAIL(UN(NtUserGetControlBrush)(xw_dead(), dc, 0x138), XE_INVALID_HWND);
    XFAIL(UN(NtUserGetControlBrush)((HWND)(ULONG_PTR)0x12345678, dc, 0x138), XE_INVALID_HWND);
    XFAIL(UN(NtUserGetControlBrush)(NULL, dc, 0x138), XE_INVALID_HWND);
    XFAIL(UN(NtUserGetControlBrush)((HWND)INVALID_HANDLE_VALUE, dc, 0x138), XE_INVALID_HWND);
    U(ReleaseDC)(p, dc); xw_kill(p);
}
XT(brush_window_checked_before_message) {
    NEED(NtUserGetControlBrush);
    XFAIL(UN(NtUserGetControlBrush)(NULL, NULL, 0), XE_INVALID_HWND);
}
XT(brush_default_answer_is_stable) {
    NEED(NtUserGetControlBrush);
    HWND p = cparent(), c = xw_child(p, 0, L"STATIC"); HDC dc = U(GetDC)(c); reset(NULL);
    ULONG_PTR a = UN(NtUserGetControlBrush)(c, dc, 0x138), b = UN(NtUserGetControlBrush)(c, dc, 0x138);
    CHECK(a == b && a != 0);
    ULONG_PTR s1 = UN(NtUserGetControlBrush)(c, dc, 0x137), s2 = UN(NtUserGetControlBrush)(c, dc, 0x137);
    CHECK(s1 == s2 && s1 != 0);
    U(ReleaseDC)(c, dc); xw_kill(p);
}
XT(brush_dialog_class_window_uses_the_face_brush) {
    NEED(NtUserGetControlBrush);
    HWND d = U(CreateWindowExW)(0, L"#32770", L"", WS_POPUP, 0, 0, 100, 100, NULL, NULL, NULL, NULL);
    if (!d) { t_skip("dialog class window"); return; }
    HDC dc = U(GetDC)(d);
    CHECK((HBRUSH)UN(NtUserGetControlBrush)(d, dc, 0x138) == sysbr(COLOR_BTNFACE));
    U(ReleaseDC)(d, dc); xw_kill(d);
}
/* ================= GetControlColor ================= */
XT(color_asks_the_given_parent_with_the_given_control) {
    NEED(NtUserGetControlColor);
    HWND p = cparent(), c = xw_child(p, 0, L"STATIC"); HDC dc = U(GetDC)(c); HBRUSH mine = solid(RGB(1, 2, 3));
    reset(mine);
    CHECK((HBRUSH)UN(NtUserGetControlColor)(p, c, dc, 0x138) == mine);
    EXPECT_EQ(g_cnt, 1); EXPECT_EQ(g_last, 0x138); CHECK(g_lparam == c); CHECK(g_wparam == dc);
    U(ReleaseDC)(c, dc); xw_kill(p); U(DeleteObject)(mine);
}
XT(color_parent_need_not_be_the_real_parent) {
    NEED(NtUserGetControlColor);
    HWND p = cparent(), other = xw_win(WS_POPUP, 0, L"STATIC"); HDC dc = U(GetDC)(p); HBRUSH mine = solid(RGB(1, 2, 3));
    reset(mine);
    CHECK((HBRUSH)UN(NtUserGetControlColor)(p, other, dc, 0x135) == mine);
    CHECK(g_lparam == other); EXPECT_EQ(g_last, 0x135);
    U(ReleaseDC)(p, dc); xw_kill(p); xw_kill(other); U(DeleteObject)(mine);
}
XT(color_window_procedure_without_answer_gives_default_brush) {
    NEED(NtUserGetControlColor);
    HWND p = cparent(), c = xw_child(p, 0, L"STATIC"); HDC dc = U(GetDC)(c); reset(NULL);
    CHECK((HBRUSH)UN(NtUserGetControlColor)(p, c, dc, 0x138) == sysbr(COLOR_BTNFACE));
    CHECK((HBRUSH)UN(NtUserGetControlColor)(p, c, dc, 0x133) == sysbr(COLOR_WINDOW));
    EXPECT_EQ(g_cnt, 2);
    U(ReleaseDC)(c, dc); xw_kill(p);
}
XT(color_static_parent_gives_default_brush_without_callback) {
    NEED(NtUserGetControlColor);
    HWND p = cparent(), c = xw_child(p, 0, L"STATIC"); HDC dc = U(GetDC)(c); reset(NULL);
    CHECK((HBRUSH)UN(NtUserGetControlColor)(c, c, dc, 0x138) == sysbr(COLOR_BTNFACE));
    EXPECT_EQ(g_cnt, 0);
    U(ReleaseDC)(c, dc); xw_kill(p);
}
static void color_default(int row) {
    NEED(NtUserGetControlColor);
    const struct cbrow *r = &cb_rows[row];
    HWND p = cparent(), c = xw_child(p, 0, L"STATIC"); HDC dc = U(GetDC)(c); reset(NULL);
    U(SetBkColor)(dc, RGB(4, 5, 6)); U(SetTextColor)(dc, RGB(1, 2, 3));
    ULONG_PTR b = UN(NtUserGetControlColor)(p, c, dc, r->msg);
    CHECK(b != 0);
    if (r->sysbrush >= 0) CHECK((HBRUSH)b == sysbr(r->sysbrush));
    CHECK(U(GetBkColor)(dc) == sys(r->bk)); CHECK(U(GetTextColor)(dc) == sys(r->tx));
    U(ReleaseDC)(c, dc); xw_kill(p);
}
XT(color_defaults_per_message) { for (int i = 0; i < 7; i++) color_default(i); }
static void color_nomsg(UINT msg) {
    NEED(NtUserGetControlColor);
    HWND p = cparent(), c = xw_child(p, 0, L"STATIC"); HDC dc = U(GetDC)(c); reset(NULL);
    SET_ERR(XSENT); EXPECT_EQ(UN(NtUserGetControlColor)(p, c, dc, msg), 0); EXPECT_ERR(XSENT); EXPECT_EQ(g_cnt, 0);
    U(ReleaseDC)(c, dc); xw_kill(p);
}
XT(color_message_outside_the_range_answers_zero_without_sending) { color_nomsg(0); color_nomsg(0x131); color_nomsg(0x139); color_nomsg(0xFFFFFFFF); }
XT(color_null_dc) {
    NEED(NtUserGetControlColor);
    HWND p = cparent(), c = xw_child(p, 0, L"STATIC"); reset(NULL);
    SET_ERR(XSENT); EXPECT_EQ(UN(NtUserGetControlColor)(p, c, NULL, 0x138), 0); EXPECT_ERR(XSENT); EXPECT_EQ(g_cnt, 0);
    xw_kill(p);
}
XT(color_stale_parent_or_control) {
    NEED(NtUserGetControlColor);
    HWND p = cparent(), c = xw_child(p, 0, L"STATIC"); HDC dc = U(GetDC)(c); reset(NULL);
    XFAIL(UN(NtUserGetControlColor)(NULL, c, dc, 0x138), XE_INVALID_HWND);
    XFAIL(UN(NtUserGetControlColor)(xw_dead(), c, dc, 0x138), XE_INVALID_HWND);
    XFAIL(UN(NtUserGetControlColor)(p, xw_dead(), dc, 0x138), XE_INVALID_HWND);
    XFAIL(UN(NtUserGetControlColor)(p, NULL, dc, 0x138), XE_INVALID_HWND);
    XFAIL(UN(NtUserGetControlColor)((HWND)(ULONG_PTR)0x1234, c, dc, 0x138), XE_INVALID_HWND);
    EXPECT_EQ(g_cnt, 0);
    U(ReleaseDC)(c, dc); xw_kill(p);
}

/* ================= FillWindow ================= */
#define FW 64
static void fw_setup(HWND *p, HWND *c, struct xw_mem *m) {
    *p = cparent(); *c = xw_child(*p, 0, L"STATIC"); xw_mem_open(m, FW, FW);
    xw_clear(m->dc, FW, FW, RGB(250, 250, 250));
}
static void fw_ctl(int n) {
    NEED(NtUserFillWindow);
    HWND p, c; struct xw_mem m; fw_setup(&p, &c, &m);
    HBRUSH mine = solid(RGB(0, 200, 0)); reset(mine);
    XOK(UN(NtUserFillWindow)(p, c, m.dc, (HBRUSH)(ULONG_PTR)n));
    EXPECT_EQ(g_cnt, 1); EXPECT_EQ(g_last, 0x132 + n); CHECK(g_lparam == c); CHECK(g_wparam == m.dc);
    CHECK(xw_px(m.dc, 5, 5) == RGB(0, 200, 0));
    xw_mem_close(&m); xw_kill(p); U(DeleteObject)(mine);
}
XT(fill_small_values_select_the_control_colour_message) { for (int n = 0; n <= 6; n++) fw_ctl(n); }
static void fw_default(int n, int sysidx) {
    NEED(NtUserFillWindow);
    HWND p, c; struct xw_mem m; fw_setup(&p, &c, &m); reset(NULL);
    XOK(UN(NtUserFillWindow)(c, c, m.dc, (HBRUSH)(ULONG_PTR)n));
    CHECK(xw_px(m.dc, 5, 5) == sys(sysidx));
    EXPECT_EQ(g_cnt, 0);   /* the static control answers by itself */
    xw_mem_close(&m); xw_kill(p);
}
XT(fill_small_values_without_an_answer_use_the_default_colours) {
    fw_default(0, COLOR_BTNFACE); fw_default(1, COLOR_WINDOW); fw_default(2, COLOR_WINDOW); fw_default(3, COLOR_BTNFACE); fw_default(4, COLOR_BTNFACE); fw_default(6, COLOR_BTNFACE);
}
static void fw_sysidx(int n, int sysidx) {
    NEED(NtUserFillWindow);
    HWND p, c; struct xw_mem m; fw_setup(&p, &c, &m); reset(NULL);
    XOK(UN(NtUserFillWindow)(p, c, m.dc, (HBRUSH)(ULONG_PTR)n));
    CHECK(xw_px(m.dc, 5, 5) == sys(sysidx));
    EXPECT_EQ(g_cnt, 0);   /* values above 6 are colour indexes plus one, no message is sent */
    xw_mem_close(&m); xw_kill(p);
}
XT(fill_values_above_six_are_system_colour_indexes_plus_one) {
    fw_sysidx(7, COLOR_WINDOWFRAME); fw_sysidx(8, COLOR_MENUTEXT); fw_sysidx(9, COLOR_WINDOWTEXT); fw_sysidx(16, COLOR_BTNFACE);
    fw_sysidx(17, COLOR_BTNSHADOW); fw_sysidx(22, COLOR_3DDKSHADOW);
}
XT(fill_system_brush_handle) {
    NEED(NtUserFillWindow);
    HWND p, c; struct xw_mem m; fw_setup(&p, &c, &m); reset(NULL);
    XOK(UN(NtUserFillWindow)(p, c, m.dc, sysbr(COLOR_HIGHLIGHT)));
    CHECK(xw_px(m.dc, 5, 5) == sys(COLOR_HIGHLIGHT)); EXPECT_EQ(g_cnt, 0);
    xw_mem_close(&m); xw_kill(p);
}
XT(fill_solid_brush_handle) {
    NEED(NtUserFillWindow);
    HWND p, c; struct xw_mem m; fw_setup(&p, &c, &m); reset(NULL); HBRUSH b = solid(RGB(200, 0, 0));
    XOK(UN(NtUserFillWindow)(p, c, m.dc, b));
    CHECK(xw_px(m.dc, 5, 5) == RGB(200, 0, 0)); EXPECT_EQ(g_cnt, 0);
    xw_mem_close(&m); xw_kill(p); U(DeleteObject)(b);
}
XT(fill_covers_exactly_the_client_rectangle) {
    NEED(NtUserFillWindow);
    HWND p, c; struct xw_mem m; fw_setup(&p, &c, &m); reset(NULL); HBRUSH b = solid(RGB(200, 0, 0)); RECT cr; U(GetClientRect)(c, &cr);
    XOK(UN(NtUserFillWindow)(p, c, m.dc, b));
    int w = cr.right, h = cr.bottom;
    CHECK(xw_px(m.dc, 0, 0) == RGB(200, 0, 0)); CHECK(xw_px(m.dc, w - 1, h - 1) == RGB(200, 0, 0));
    CHECK(xw_px(m.dc, w, h - 1) == RGB(250, 250, 250)); CHECK(xw_px(m.dc, w - 1, h) == RGB(250, 250, 250)); CHECK(xw_px(m.dc, w, h) == RGB(250, 250, 250));
    xw_mem_close(&m); xw_kill(p); U(DeleteObject)(b);
}
XT(fill_follows_the_client_size) {
    NEED(NtUserFillWindow);
    HWND p, c; struct xw_mem m; fw_setup(&p, &c, &m); reset(NULL); HBRUSH b = solid(RGB(200, 0, 0));
    U(SetWindowPos)(c, NULL, 0, 0, 20, 30, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    XOK(UN(NtUserFillWindow)(p, c, m.dc, b));
    CHECK(xw_px(m.dc, 19, 29) == RGB(200, 0, 0)); CHECK(xw_px(m.dc, 20, 29) == RGB(250, 250, 250)); CHECK(xw_px(m.dc, 19, 30) == RGB(250, 250, 250));
    xw_mem_close(&m); xw_kill(p); U(DeleteObject)(b);
}
XT(fill_ignores_the_window_position) {
    NEED(NtUserFillWindow);
    HWND p, c; struct xw_mem m; fw_setup(&p, &c, &m); reset(NULL); HBRUSH b = solid(RGB(200, 0, 0));
    U(SetWindowPos)(c, NULL, 100, 100, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    XOK(UN(NtUserFillWindow)(p, c, m.dc, b));
    CHECK(xw_px(m.dc, 0, 0) == RGB(200, 0, 0));
    xw_mem_close(&m); xw_kill(p); U(DeleteObject)(b);
}
XT(fill_honours_the_device_context_origin) {
    NEED(NtUserFillWindow);
    HWND p, c; struct xw_mem m; fw_setup(&p, &c, &m); reset(NULL); HBRUSH b = solid(RGB(200, 0, 0));
    U(SetViewportOrgEx)(m.dc, 10, 12, NULL);
    XOK(UN(NtUserFillWindow)(p, c, m.dc, b));
    U(SetViewportOrgEx)(m.dc, 0, 0, NULL);
    CHECK(xw_px(m.dc, 9, 11) == RGB(250, 250, 250)); CHECK(xw_px(m.dc, 10, 12) == RGB(200, 0, 0));
    xw_mem_close(&m); xw_kill(p); U(DeleteObject)(b);
}
XT(fill_of_a_zero_sized_window_paints_nothing) {
    NEED(NtUserFillWindow);
    HWND p, c; struct xw_mem m; fw_setup(&p, &c, &m); reset(NULL); HBRUSH b = solid(RGB(200, 0, 0));
    U(SetWindowPos)(c, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    XOK(UN(NtUserFillWindow)(p, c, m.dc, b));
    CHECK(xw_px(m.dc, 0, 0) == RGB(250, 250, 250));
    xw_mem_close(&m); xw_kill(p); U(DeleteObject)(b);
}
XT(fill_stale_windows) {
    NEED(NtUserFillWindow);
    HWND p, c; struct xw_mem m; fw_setup(&p, &c, &m); HBRUSH b = solid(RGB(200, 0, 0));
    XFAIL(UN(NtUserFillWindow)(xw_dead(), c, m.dc, b), XE_INVALID_HWND);
    XFAIL(UN(NtUserFillWindow)(p, xw_dead(), m.dc, b), XE_INVALID_HWND);
    XFAIL(UN(NtUserFillWindow)(NULL, NULL, m.dc, b), XE_INVALID_HWND);
    XFAIL(UN(NtUserFillWindow)(p, NULL, m.dc, b), XE_INVALID_HWND);
    CHECK(xw_px(m.dc, 5, 5) == RGB(250, 250, 250));
    xw_mem_close(&m); xw_kill(p); U(DeleteObject)(b);
}
XT(fill_null_brush_window_is_accepted_for_a_real_brush) {
    NEED(NtUserFillWindow);
    HWND p, c; struct xw_mem m; fw_setup(&p, &c, &m); HBRUSH b = solid(RGB(200, 0, 0));
    XOK(UN(NtUserFillWindow)(NULL, c, m.dc, b));
    CHECK(xw_px(m.dc, 5, 5) == RGB(200, 0, 0));
    xw_mem_close(&m); xw_kill(p); U(DeleteObject)(b);
}
XT(fill_null_device_context) {
    NEED(NtUserFillWindow);
    HWND p, c; struct xw_mem m; fw_setup(&p, &c, &m); HBRUSH b = solid(RGB(200, 0, 0));
    SET_ERR(XSENT); EXPECT_EQ(UN(NtUserFillWindow)(p, c, NULL, b), 0); EXPECT_ERR(XSENT);
    xw_mem_close(&m); xw_kill(p); U(DeleteObject)(b);
}
/* ================= DrawCaptionTemp ================= */
static UNICODE_STRING us_make(WCHAR *s) { UNICODE_STRING u; u.Buffer = s; u.Length = (USHORT)(lstrlenW(s) * 2); u.MaximumLength = u.Length + 2; return u; }
#define CW 100
#define CH 24
static ULONG_PTR dct(HWND w, HDC dc, const RECT *rc, UINT flags) {
    static WCHAR txt[] = L"abc"; UNICODE_STRING u = us_make(txt);
    return UN(NtUserDrawCaptionTemp)(w, dc, rc, NULL, NULL, &u, flags);
}
static void dct_setup(HWND *p, struct xw_mem *m) {
    *p = xw_win(WS_POPUP, 0, L"STATIC"); xw_mem_open(m, 200, 100); xw_clear(m->dc, 200, 100, RGB(1, 2, 3));
}
static void dct_paint(UINT flags, COLORREF want) {
    NEED(NtUserDrawCaptionTemp);
    HWND p; struct xw_mem m; dct_setup(&p, &m); RECT rc = { 0, 0, CW, CH };
    XOK(dct(p, m.dc, &rc, flags));
    CHECKF(xw_px(m.dc, 3, 3) == want, "pixel 0x%lx want 0x%lx", (unsigned long)xw_px(m.dc, 3, 3), (unsigned long)want);
    xw_mem_close(&m); xw_kill(p);
}
XT(caption_text_paints_inactive_colour) { dct_paint(DC_TEXT, sys(COLOR_INACTIVECAPTION)); }
XT(caption_text_active_paints_active_colour) { dct_paint(DC_TEXT | DC_ACTIVE, sys(COLOR_ACTIVECAPTION)); }
XT(caption_text_inbutton_paints_face_colour) { dct_paint(DC_TEXT | DC_INBUTTON, sys(COLOR_BTNFACE)); }
XT(caption_text_with_icon_and_buttons_flags) { dct_paint(DC_TEXT | DC_ACTIVE | DC_ICON | DC_BUTTONS | DC_SMALLCAP, sys(COLOR_ACTIVECAPTION)); }
XT(caption_text_ignores_unknown_high_flags) { dct_paint(DC_TEXT | DC_ACTIVE | 0x10000, sys(COLOR_ACTIVECAPTION)); }
XT(caption_text_ignores_top_bit) { dct_paint(DC_TEXT | 0x40000000, sys(COLOR_INACTIVECAPTION)); }
static void dct_nopaint(UINT flags) {
    NEED(NtUserDrawCaptionTemp);
    HWND p; struct xw_mem m; dct_setup(&p, &m); RECT rc = { 0, 0, CW, CH };
    XOK(dct(p, m.dc, &rc, flags));
    CHECK(xw_px(m.dc, 3, 3) == RGB(1, 2, 3));
    xw_mem_close(&m); xw_kill(p);
}
XT(caption_flags_zero_paint_nothing) { dct_nopaint(0); }
XT(caption_active_alone_paints_nothing) { dct_nopaint(DC_ACTIVE); }
XT(caption_smallcap_alone_paints_nothing) { dct_nopaint(DC_SMALLCAP); }
XT(caption_icon_alone_paints_nothing) { dct_nopaint(DC_ICON); }
XT(caption_inbutton_alone_paints_nothing) { dct_nopaint(DC_INBUTTON); }
XT(caption_gradient_alone_paints_nothing) { dct_nopaint(DC_GRADIENT); }
XT(caption_buttons_alone_paints_nothing) { dct_nopaint(DC_BUTTONS); }
XT(caption_flag_0x8000_alone_is_accepted_and_paints_nothing) { dct_nopaint(0x8000); }

static int chan(COLORREF c, int i) { return (c >> (8 * i)) & 0xFF; }
static void gradient(UINT extra, int c0, int c1) {
    NEED(NtUserDrawCaptionTemp);
    HWND p; struct xw_mem m; dct_setup(&p, &m); RECT rc = { 0, 0, CW, CH };
    XOK(dct(p, m.dc, &rc, DC_TEXT | DC_GRADIENT | extra));
    COLORREF left = xw_px(m.dc, 0, 5), right = xw_px(m.dc, CW - 1, 5), a = sys(c0), b = sys(c1);
    CHECKF(left == a, "left 0x%lx want 0x%lx", (unsigned long)left, (unsigned long)a);
    CHECK(right != left);
    for (int i = 0; i < 3; i++) {   /* the right edge lies between the two caption colours, channel by channel */
        int lo = chan(a, i) < chan(b, i) ? chan(a, i) : chan(b, i), hi = chan(a, i) > chan(b, i) ? chan(a, i) : chan(b, i);
        CHECKF(chan(right, i) >= lo && chan(right, i) <= hi, "channel %d out of range", i);
    }
    xw_mem_close(&m); xw_kill(p);
}
XT(caption_gradient_inactive) { gradient(0, COLOR_INACTIVECAPTION, COLOR_GRADIENTINACTIVECAPTION); }
XT(caption_gradient_active) { gradient(DC_ACTIVE, COLOR_ACTIVECAPTION, COLOR_GRADIENTACTIVECAPTION); }
XT(caption_gradient_is_uniform_down_a_column) {
    NEED(NtUserDrawCaptionTemp);
    HWND p; struct xw_mem m; dct_setup(&p, &m); RECT rc = { 0, 0, CW, CH };
    XOK(dct(p, m.dc, &rc, DC_TEXT | DC_GRADIENT));
    CHECK(xw_px(m.dc, 50, 2) == xw_px(m.dc, 50, 18));
    xw_mem_close(&m); xw_kill(p);
}
XT(caption_paint_stays_inside_the_rectangle) {
    NEED(NtUserDrawCaptionTemp);
    HWND p; struct xw_mem m; dct_setup(&p, &m); RECT rc = { 10, 5, 60, 20 };
    XOK(dct(p, m.dc, &rc, DC_TEXT | DC_ACTIVE));
    CHECK(xw_px(m.dc, 10, 5) == sys(COLOR_ACTIVECAPTION)); CHECK(xw_px(m.dc, 59, 19) == sys(COLOR_ACTIVECAPTION));
    CHECK(xw_px(m.dc, 9, 10) == RGB(1, 2, 3)); CHECK(xw_px(m.dc, 60, 10) == RGB(1, 2, 3));
    CHECK(xw_px(m.dc, 30, 4) == RGB(1, 2, 3)); CHECK(xw_px(m.dc, 30, 20) == RGB(1, 2, 3));
    xw_mem_close(&m); xw_kill(p);
}
XT(caption_window_may_be_null) {
    NEED(NtUserDrawCaptionTemp);
    struct xw_mem m; xw_mem_open(&m, 200, 100); xw_clear(m.dc, 200, 100, RGB(1, 2, 3)); RECT rc = { 0, 0, CW, CH };
    XOK(dct(NULL, m.dc, &rc, DC_TEXT | DC_ACTIVE));
    CHECK(xw_px(m.dc, 3, 3) == sys(COLOR_ACTIVECAPTION));
    xw_mem_close(&m);
}
XT(caption_window_style_does_not_matter) {
    NEED(NtUserDrawCaptionTemp);
    HWND p = xw_win(WS_POPUP, 0, L"STATIC"); struct xw_mem m; xw_mem_open(&m, 200, 100); xw_clear(m.dc, 200, 100, RGB(1, 2, 3)); RECT rc = { 0, 0, CW, CH };
    XOK(dct(p, m.dc, &rc, DC_TEXT | DC_ACTIVE));
    CHECK(xw_px(m.dc, 3, 3) == sys(COLOR_ACTIVECAPTION));
    xw_mem_close(&m); xw_kill(p);
}
XT(caption_flag_0x4000_answers_zero_and_clears_the_error_but_still_paints) {
    NEED(NtUserDrawCaptionTemp);
    HWND p; struct xw_mem m; dct_setup(&p, &m); RECT rc = { 0, 0, CW, CH };
    SET_ERR(XSENT); EXPECT_EQ(R32(dct(p, m.dc, &rc, DC_TEXT | 0x4000)), 0); EXPECT_ERR(0);   /* the answer is 0 and the error is cleared, yet the background was painted */
    CHECK(xw_px(m.dc, 3, 3) == sys(COLOR_INACTIVECAPTION));
    xw_mem_close(&m); xw_kill(p);
}
XT(caption_all_flags_is_refused) {
    NEED(NtUserDrawCaptionTemp);
    HWND p; struct xw_mem m; dct_setup(&p, &m); RECT rc = { 0, 0, CW, CH };
    SET_ERR(XSENT); EXPECT_EQ(R32(dct(p, m.dc, &rc, 0xFFFFFFFF)), 0); EXPECT_ERR(0);
    xw_mem_close(&m); xw_kill(p);
}
XT(caption_null_rectangle_is_refused) {
    NEED(NtUserDrawCaptionTemp);
    HWND p; struct xw_mem m; dct_setup(&p, &m);
    SET_ERR(XSENT); EXPECT_EQ(R32(dct(p, m.dc, NULL, DC_TEXT)), 0); EXPECT_ERR(XSENT);
    CHECK(xw_px(m.dc, 3, 3) == RGB(1, 2, 3));
    xw_mem_close(&m); xw_kill(p);
}
XT(caption_unreadable_rectangle_is_refused_quietly) {
    NEED(NtUserDrawCaptionTemp);
    HWND p; struct xw_mem m; dct_setup(&p, &m);
    SET_ERR(XSENT); EXPECT_EQ(R32(dct(p, m.dc, (const RECT *)XP_BADPTR, DC_TEXT)), 0); EXPECT_ERR(XSENT);
    SET_ERR(XSENT); EXPECT_EQ(R32(dct(p, m.dc, (const RECT *)XP_KERNELPTR, DC_TEXT)), 0); EXPECT_ERR(XSENT);
    xw_mem_close(&m); xw_kill(p);
}
XT(caption_empty_rectangle_is_refused) {
    NEED(NtUserDrawCaptionTemp);
    HWND p; struct xw_mem m; dct_setup(&p, &m); RECT rc = { 10, 10, 10, 10 };
    SET_ERR(XSENT); EXPECT_EQ(R32(dct(p, m.dc, &rc, DC_TEXT)), 0); EXPECT_ERR(XSENT);
    CHECK(xw_px(m.dc, 10, 10) == RGB(1, 2, 3));
    xw_mem_close(&m); xw_kill(p);
}
XT(caption_null_text_with_dc_text_is_refused) {
    NEED(NtUserDrawCaptionTemp);
    HWND p; struct xw_mem m; dct_setup(&p, &m); RECT rc = { 0, 0, CW, CH };
    SET_ERR(XSENT); EXPECT_EQ(R32(UN(NtUserDrawCaptionTemp)(p, m.dc, &rc, NULL, NULL, NULL, DC_TEXT)), 0); EXPECT_ERR(XSENT);
    CHECK(xw_px(m.dc, 3, 3) == RGB(1, 2, 3));
    xw_mem_close(&m); xw_kill(p);
}
XT(caption_unreadable_text_with_dc_text_is_refused_quietly) {
    NEED(NtUserDrawCaptionTemp);
    HWND p; struct xw_mem m; dct_setup(&p, &m); RECT rc = { 0, 0, CW, CH };
    SET_ERR(XSENT); EXPECT_EQ(R32(UN(NtUserDrawCaptionTemp)(p, m.dc, &rc, NULL, NULL, (UNICODE_STRING *)XP_BADPTR, DC_TEXT)), 0); EXPECT_ERR(XSENT);
    xw_mem_close(&m); xw_kill(p);
}
XT(caption_stale_window) {
    NEED(NtUserDrawCaptionTemp);
    HWND p; struct xw_mem m; dct_setup(&p, &m); RECT rc = { 0, 0, CW, CH };
    XFAIL(dct(xw_dead(), m.dc, &rc, DC_TEXT | DC_ACTIVE), XE_INVALID_HWND);
    XFAIL(dct((HWND)(ULONG_PTR)0x1234, m.dc, &rc, DC_TEXT | DC_ACTIVE), XE_INVALID_HWND);
    CHECK(xw_px(m.dc, 3, 3) == RGB(1, 2, 3));
    xw_mem_close(&m); xw_kill(p);
}
XT(caption_empty_text_still_paints_the_background) {
    NEED(NtUserDrawCaptionTemp);
    HWND p; struct xw_mem m; dct_setup(&p, &m); RECT rc = { 0, 0, CW, CH }; static WCHAR e[] = L""; UNICODE_STRING u = us_make(e);
    XOK(UN(NtUserDrawCaptionTemp)(p, m.dc, &rc, NULL, NULL, &u, DC_TEXT | DC_ACTIVE));
    CHECK(xw_px(m.dc, 3, 3) == sys(COLOR_ACTIVECAPTION));
    xw_mem_close(&m); xw_kill(p);
}
XT(caption_text_that_does_not_fit_answers_zero_but_the_background_is_painted) {
    NEED(NtUserDrawCaptionTemp);
    HWND p; struct xw_mem m; dct_setup(&p, &m); RECT rc = { 0, 0, CW, CH }; static WCHAR t[] = L"A fairly long caption text that will not fit"; UNICODE_STRING u = us_make(t);
    EXPECT_EQ(R32(UN(NtUserDrawCaptionTemp)(p, m.dc, &rc, NULL, NULL, &u, DC_TEXT | DC_ACTIVE)), 0);
    CHECK(xw_px(m.dc, 0, 0) == sys(COLOR_ACTIVECAPTION));
    CHECK(xw_px(m.dc, CW - 1, CH - 1) == sys(COLOR_ACTIVECAPTION));
    xw_mem_close(&m); xw_kill(p);
}
XT(caption_short_text_answers_one) {
    NEED(NtUserDrawCaptionTemp);
    HWND p; struct xw_mem m; dct_setup(&p, &m); RECT rc = { 0, 0, CW, CH };
    EXPECT_EQ(R32(dct(p, m.dc, &rc, DC_TEXT | DC_ACTIVE)), 1);
    xw_mem_close(&m); xw_kill(p);
}
XT(caption_window_with_system_menu_leaves_the_icon_area_unpainted) {
    NEED(NtUserDrawCaptionTemp);
    HWND p = xw_win(WS_OVERLAPPEDWINDOW, 0, L"STATIC"); struct xw_mem m; xw_mem_open(&m, 200, 100); xw_clear(m.dc, 200, 100, RGB(1, 2, 3)); RECT rc = { 0, 0, CW, CH };
    XOK(dct(p, m.dc, &rc, DC_TEXT | DC_ACTIVE));
    CHECK(xw_px(m.dc, 3, 3) == RGB(1, 2, 3));                       /* where the icon would be */
    CHECK(xw_px(m.dc, CW - 3, 3) == sys(COLOR_ACTIVECAPTION));      /* the rest of the bar */
    xw_mem_close(&m); xw_kill(p);
}

/* ================= DrawMenuBarTemp / PaintMenuBar ================= */
static HMENU mkmenu(int items) {
    HMENU m = U(CreateMenu)(); for (int i = 0; i < items; i++) U(AppendMenuW)(m, MF_STRING, 100 + i, L"Menu item");
    return m;
}
static int mbar_height(HWND w) {
    MENUBARINFO mb; memset(&mb, 0, sizeof mb); mb.cbSize = sizeof mb;
    if (!U(GetMenuBarInfo)(w, OBJID_MENU, 0, &mb)) return -1;
    return mb.rcBar.bottom - mb.rcBar.top;
}
XT(menu_bar_temp_answers_zero_and_draws_nothing) {
    NEED(NtUserDrawMenuBarTemp);
    HWND w = xw_win(WS_OVERLAPPEDWINDOW, 0, L"STATIC"); HMENU m = mkmenu(2); U(SetMenu)(w, m);
    struct xw_mem mem; xw_mem_open(&mem, 200, 60); xw_clear(mem.dc, 200, 60, RGB(1, 2, 3)); RECT rc = { 0, 0, 200, 30 };
    SET_ERR(XSENT);
    EXPECT_EQ(R32(UN(NtUserDrawMenuBarTemp)(w, mem.dc, &rc, m, (HFONT)U(GetStockObject)(DEFAULT_GUI_FONT))), 0);
    EXPECT_ERR(XSENT);
    CHECK(xw_px(mem.dc, 5, 5) == RGB(1, 2, 3)); CHECK(xw_px(mem.dc, 100, 15) == RGB(1, 2, 3));
    xw_mem_close(&mem); xw_kill(w);
}
XT(menu_bar_temp_leaves_the_rectangle_alone) {
    NEED(NtUserDrawMenuBarTemp);
    HWND w = xw_win(WS_OVERLAPPEDWINDOW, 0, L"STATIC"); HMENU m = mkmenu(2); U(SetMenu)(w, m);
    struct xw_mem mem; xw_mem_open(&mem, 200, 60); RECT rc = { 3, 4, 150, 40 };
    UN(NtUserDrawMenuBarTemp)(w, mem.dc, &rc, m, NULL);
    CHECK(rc.left == 3 && rc.top == 4 && rc.right == 150 && rc.bottom == 40);
    xw_mem_close(&mem); xw_kill(w);
}
XT(menu_bar_temp_with_null_and_stale_arguments) {
    NEED(NtUserDrawMenuBarTemp);
    HWND w = xw_win(WS_OVERLAPPEDWINDOW, 0, L"STATIC"); HMENU m = mkmenu(1);
    struct xw_mem mem; xw_mem_open(&mem, 200, 60); RECT rc = { 0, 0, 200, 30 };
    EXPECT_EQ(R32(UN(NtUserDrawMenuBarTemp)(w, mem.dc, &rc, NULL, NULL)), 0);
    EXPECT_EQ(R32(UN(NtUserDrawMenuBarTemp)(w, mem.dc, &rc, (HMENU)(ULONG_PTR)0x1234, NULL)), 0);
    EXPECT_EQ(R32(UN(NtUserDrawMenuBarTemp)(xw_dead(), mem.dc, &rc, m, NULL)), 0);
    EXPECT_EQ(R32(UN(NtUserDrawMenuBarTemp)(w, mem.dc, NULL, m, NULL)), 0);
    xw_mem_close(&mem); xw_kill(w); U(DestroyMenu)(m);
}
XT(menu_bar_temp_user32_export_agrees) {
    NEED(NtUserDrawMenuBarTemp);
    DWORD (WINAPI *f)(HWND, HDC, LPRECT, HMENU, HFONT) = (void *)t_proc_in("user32.dll", "DrawMenuBarTemp");
    if (!f) { t_skip("user32!DrawMenuBarTemp not exported"); return; }
    HWND w = xw_win(WS_OVERLAPPEDWINDOW, 0, L"STATIC"); HMENU m = mkmenu(2); U(SetMenu)(w, m);
    struct xw_mem mem; xw_mem_open(&mem, 200, 60); RECT rc = { 0, 0, 200, 30 }, rc2 = rc;
    CHECK(f(w, mem.dc, &rc, m, NULL) == R32(UN(NtUserDrawMenuBarTemp)(w, mem.dc, &rc2, m, NULL)));
    xw_mem_close(&mem); xw_kill(w);
}

static void pmb_height(int items, int right) {
    NEED(NtUserPaintMenuBar);
    HWND w = xw_win(WS_OVERLAPPEDWINDOW, 0, L"STATIC"); HMENU m = mkmenu(items); U(SetMenu)(w, m);
    struct xw_mem mem; xw_mem_open(&mem, 400, 100);
    ULONG_PTR h = UN(NtUserPaintMenuBar)(w, mem.dc, 0, right, 0, 0);
    EXPECT_EQ(R32(h), mbar_height(w));
    CHECK(R32(h) > 0);
    xw_mem_close(&mem); xw_kill(w);
}
XT(paint_menu_bar_height_matches_menu_bar_info_1_item) { pmb_height(1, 200); }
XT(paint_menu_bar_height_matches_menu_bar_info_2_items) { pmb_height(2, 200); }
XT(paint_menu_bar_height_matches_menu_bar_info_6_items) { pmb_height(6, 200); }
XT(paint_menu_bar_height_matches_menu_bar_info_20_items) { pmb_height(20, 200); }
XT(paint_menu_bar_height_ignores_the_right_argument) { pmb_height(6, 1000); pmb_height(6, 60); }
XT(paint_menu_bar_height_grows_with_wrapped_rows) {
    NEED(NtUserPaintMenuBar);
    HWND w = xw_win(WS_OVERLAPPEDWINDOW, 0, L"STATIC"); HMENU m = mkmenu(1); U(SetMenu)(w, m);
    struct xw_mem mem; xw_mem_open(&mem, 400, 100);
    ULONG_PTR a = UN(NtUserPaintMenuBar)(w, mem.dc, 0, 200, 0, 0);
    for (int i = 0; i < 9; i++) U(AppendMenuW)(m, MF_STRING, 200 + i, L"Menu item");
    U(DrawMenuBar)(w);
    ULONG_PTR b = UN(NtUserPaintMenuBar)(w, mem.dc, 0, 200, 0, 0);
    CHECK(R32(b) > R32(a));
    xw_mem_close(&mem); xw_kill(w);
}
XT(paint_menu_bar_window_without_menu) {
    NEED(NtUserPaintMenuBar);
    HWND w = xw_win(WS_OVERLAPPEDWINDOW, 0, L"STATIC"); struct xw_mem mem; xw_mem_open(&mem, 400, 100);
    SET_ERR(XSENT); EXPECT_EQ(R32(UN(NtUserPaintMenuBar)(w, mem.dc, 0, 200, 0, 0)), 0); EXPECT_ERR(XSENT);
    xw_mem_close(&mem); xw_kill(w);
}
XT(paint_menu_bar_stale_window) {
    NEED(NtUserPaintMenuBar);
    struct xw_mem mem; xw_mem_open(&mem, 400, 100);
    XFAIL(UN(NtUserPaintMenuBar)(xw_dead(), mem.dc, 0, 200, 0, 0), XE_INVALID_HWND);
    XFAIL(UN(NtUserPaintMenuBar)(NULL, mem.dc, 0, 200, 0, 0), XE_INVALID_HWND);
    xw_mem_close(&mem);
}
XT(paint_menu_bar_null_device_context_still_measures) {
    NEED(NtUserPaintMenuBar);
    HWND w = xw_win(WS_OVERLAPPEDWINDOW, 0, L"STATIC"); HMENU m = mkmenu(2); U(SetMenu)(w, m);
    SET_ERR(XSENT);
    ULONG_PTR h = UN(NtUserPaintMenuBar)(w, NULL, 0, 200, 0, 0);
    EXPECT_EQ(R32(h), mbar_height(w));
    EXPECT_ERR(XE_INVALID_HANDLE);
    xw_kill(w);
}
XT(paint_menu_bar_is_repeatable) {
    NEED(NtUserPaintMenuBar);
    HWND w = xw_win(WS_OVERLAPPEDWINDOW, 0, L"STATIC"); HMENU m = mkmenu(3); U(SetMenu)(w, m);
    struct xw_mem mem; xw_mem_open(&mem, 400, 100);
    ULONG_PTR a = UN(NtUserPaintMenuBar)(w, mem.dc, 0, 200, 0, 0); int same = 1;
    for (int i = 0; i < 20; i++) if (UN(NtUserPaintMenuBar)(w, mem.dc, 0, 200, 0, 0) != a) same = 0;
    CHECK(same);
    xw_mem_close(&mem); xw_kill(w);
}
