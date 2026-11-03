/* Thin-coverage USER syscalls reached through user32 thunks that jump straight to win32u:
 * NtUserGetTitleBarInfo, NtUserGetScrollBarInfo, NtUserGetComboBoxInfo, NtUserGetGestureConfig, NtUserGetDpiForMonitor,
 * NtUserLogicalToPerMonitorDPIPhysicalPoint, NtUserIsClipboardFormatAvailable, NtUserMsgWaitForMultipleObjectsEx.
 *
 * Analysis facts (kernel behaviour) used for the hard CHECKs:
 *  - GetTitleBarInfo/GetScrollBarInfo/GetComboBoxInfo/GetGestureConfig all start with ValidateHwnd: bad HWND -> FALSE, ERROR_INVALID_WINDOW_HANDLE.
 *  - TITLEBARINFO.cbSize must be 0x2c (else 0x57). the kernel always ORs STATE_SYSTEM_FOCUSABLE (0x100000) into rgstate[0];
 *    a window without WS_CAPTION (WS_CAPTION bits not both set) gets STATE_SYSTEM_INVISIBLE (0x8000) and returns an all-zero rectangle.
 *  - SCROLLBARINFO.cbSize must be 0x3c and idObject one of OBJID_CLIENT/-4, OBJID_VSCROLL/-5, OBJID_HSCROLL/-6 (else 0x57); a window without
 *    WS_VSCROLL / WS_HSCROLL answers TRUE with rgstate[0] = STATE_SYSTEM_INVISIBLE; the ESB_DISABLE_* bits map to STATE_SYSTEM_UNAVAILABLE on
 *    rgstate[1]+[2] (LTUP), [4]+[5] (RTDN) and [0] (both).
 *  - COMBOBOXINFO.cbSize must be 0x40 for a real combo box; other windows are sent WM_GETCOMBOBOXINFO (0x164) and answer FALSE through DefWindowProc.
 *  - NtUserGetGestureConfig: cbSize must be 12, flags only bit 0, *pcIDs in 1..256, config buffer non-NULL, in that order.
 *  - GetDpiForMonitorInternal rejects type >= 3 in user32 (ERROR_BAD_ARGUMENTS); NtUserGetDpiForMonitor rejects an invalid HMONITOR with error 6.
 *  - NtUserMsgWaitForMultipleObjectsEx: flags & ~0x1f or nCount > 63 -> WAIT_FAILED/0x57. */
#include "g8.h"

#define NEED_USER() do { if (!U(CreateWindowExW) || !U(DestroyWindow) || !U(GetMessageW)) { t_skip("user32 missing"); return; } } while (0)

/* A hidden-from-focus, off-screen window of a system class. */
static HWND mkwin(DWORD style, DWORD ex, HWND parent, const wchar_t *cls) {
    return U(CreateWindowExW)(ex | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls, L"gap8", style, -30000, -30000, 200, 160, parent, NULL, GetModuleHandleW(NULL), NULL);
}
static HWND mkpopup(void) { return mkwin(WS_POPUP, 0, NULL, L"Static"); }
static void drain(void) { MSG m; while (U(PeekMessageW)(&m, NULL, 0, 0, PM_REMOVE)) { U(TranslateMessage)(&m); U(DispatchMessageW)(&m); } }

static HWND dead_window(void) { HWND w = mkpopup(); if (w) U(DestroyWindow)(w); return w; }

struct badhwnd { const char *what; HWND h; };
static int bad_hwnds(struct badhwnd *t, int cap) {
    int n = 0;
    if (n < cap) { t[n].what = "NULL"; t[n].h = NULL; n++; }
    if (n < cap) { t[n].what = "garbage"; t[n].h = (HWND)(ULONG_PTR)0x12345678; n++; }
    if (n < cap) { t[n].what = "INVALID_HANDLE_VALUE"; t[n].h = (HWND)INVALID_HANDLE_VALUE; n++; }
    if (n < cap) { t[n].what = "destroyed"; t[n].h = dead_window(); n++; }
    if (n < cap) { t[n].what = "low index"; t[n].h = (HWND)(ULONG_PTR)1; n++; }
    return n;
}

/* ================================================================= NtUserGetTitleBarInfo ============= */

#define TB_SIZE 0x2c
struct tbi { DWORD cb; RECT rc; DWORD st[6]; };

TEST(gap8_titlebar_invalid_hwnd) {
    NEED_USER(); REQUIRE(U(GetTitleBarInfo));
    struct badhwnd t[8]; int n = bad_hwnds(t, 8);
    for (int i = 0; i < n; i++) {
        struct tbi b; memset(&b, 0x5a, sizeof b); b.cb = TB_SIZE;
        SetLastError(0);
        BOOL r = U(GetTitleBarInfo)(t[i].h, (PTITLEBARINFO)&b);
        CHECKF(r == FALSE, "%s", t[i].what);
        CHECKF(GetLastError() == ERROR_INVALID_WINDOW_HANDLE, "%s err %lu", t[i].what, GetLastError());
        CHECKF(b.st[0] == 0x5a5a5a5a, "%s: output must stay untouched", t[i].what);   /* copy-out only when cbSize matched */
    }
}

TEST(gap8_titlebar_cbsize_protocol) {
    NEED_USER(); REQUIRE(U(GetTitleBarInfo));
    HWND w = mkwin(WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, NULL, L"Static");
    WCHK(w);
    static const DWORD bad[] = { 0, 1, 0x2b, 0x2d, 0x30, 0x58, 0x2c0000, 0xffffffffu, 0x2c00000000ull & 0xffffffffu };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        union { struct tbi b; BYTE raw[0x80]; } u; memset(&u, 0x5a, sizeof u); u.b.cb = bad[i];
        SetLastError(0);
        BOOL r = U(GetTitleBarInfo)(w, (PTITLEBARINFO)&u.b);
        CHECKF(r == FALSE, "cbSize 0x%lx", bad[i]);
        CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "cbSize 0x%lx err %lu", bad[i], GetLastError());
        CHECKF(u.b.st[0] == 0x5a5a5a5a && u.b.cb == bad[i], "cbSize 0x%lx: output modified on failure", bad[i]);
    }
    struct tbi b; memset(&b, 0, sizeof b); b.cb = TB_SIZE;
    G8EQ(U(GetTitleBarInfo)(w, (PTITLEBARINFO)&b), TRUE);
    G8EQ(b.cb, TB_SIZE);
    U(DestroyWindow)(w);
}

TEST(gap8_titlebar_focusable_always_set) {
    NEED_USER(); REQUIRE(U(GetTitleBarInfo));
    static const DWORD styles[] = { WS_POPUP, WS_POPUP | WS_BORDER, WS_POPUP | WS_CAPTION, WS_OVERLAPPEDWINDOW, WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX,
                                    WS_POPUP | WS_DLGFRAME, WS_OVERLAPPED | WS_CAPTION };
    for (int vis = 0; vis < 2; vis++)
        for (unsigned i = 0; i < sizeof styles / sizeof styles[0]; i++) {
            HWND w = mkwin(styles[i] | (vis ? WS_VISIBLE : 0), 0, NULL, L"Static");
            if (!w && t_is_wine()) { t_skip("no window support in this environment"); return; } CHECKF(w != NULL, "style 0x%lx", styles[i]); if (!w) continue;
            struct tbi b; memset(&b, 0xcc, sizeof b); b.cb = TB_SIZE;
            BOOL r = U(GetTitleBarInfo)(w, (PTITLEBARINFO)&b);
            CHECKF(r, "style 0x%lx vis %d", styles[i], vis);
            CHECKF(b.st[0] & STATE_SYSTEM_FOCUSABLE, "style 0x%lx vis %d rgstate[0]=0x%lx", styles[i], vis, b.st[0]);
            t_note("style 0x%lx vis %d rgstate[0..5]=%lx %lx %lx %lx %lx %lx rc(%ld,%ld,%ld,%ld)", styles[i], vis, b.st[0], b.st[1], b.st[2], b.st[3], b.st[4], b.st[5], b.rc.left, b.rc.top, b.rc.right, b.rc.bottom);
            U(DestroyWindow)(w);
        }
}

TEST(gap8_titlebar_no_caption_is_invisible_and_empty) {
    NEED_USER(); REQUIRE(U(GetTitleBarInfo));
    /* WS_CAPTION bits not both set: WS_CAPTION is both WS_BORDER and WS_DLGFRAME, so either bit alone is not a caption. */
    static const DWORD styles[] = { WS_POPUP, WS_POPUP | WS_BORDER, WS_POPUP | WS_DLGFRAME, WS_POPUP | WS_THICKFRAME, WS_POPUP | WS_SYSMENU, WS_CHILD * 0 };
    for (unsigned i = 0; i < 5; i++)
        for (int vis = 0; vis < 2; vis++) {
            HWND w = mkwin(styles[i] | (vis ? WS_VISIBLE : 0), 0, NULL, L"Static");
            if (!w && t_is_wine()) { t_skip("no window support in this environment"); return; } CHECKF(w != NULL, "row %u", i); if (!w) continue;
            struct tbi b; memset(&b, 0xcc, sizeof b); b.cb = TB_SIZE;
            BOOL r = U(GetTitleBarInfo)(w, (PTITLEBARINFO)&b);
            CHECKF(r, "row %u vis %d", i, vis);
            CHECKF(b.st[0] == (STATE_SYSTEM_FOCUSABLE | STATE_SYSTEM_INVISIBLE), "row %u vis %d rgstate[0]=0x%lx", i, vis, b.st[0]);
            for (int k = 1; k < 6; k++) CHECKF(b.st[k] == 0, "row %u vis %d rgstate[%d]=0x%lx", i, vis, k, b.st[k]);
            CHECKF(b.rc.left == 0 && b.rc.top == 0 && b.rc.right == 0 && b.rc.bottom == 0, "row %u vis %d rc (%ld,%ld,%ld,%ld)", i, vis, b.rc.left, b.rc.top, b.rc.right, b.rc.bottom);
            U(DestroyWindow)(w);
        }
}

TEST(gap8_titlebar_caption_rect_inside_window) {
    NEED_USER(); REQUIRE(U(GetTitleBarInfo)); REQUIRE(U(GetWindowRect));
    HWND w = mkwin(WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, NULL, L"Static");
    WCHK(w);
    struct tbi b; memset(&b, 0, sizeof b); b.cb = TB_SIZE;
    G8EQ(U(GetTitleBarInfo)(w, (PTITLEBARINFO)&b), TRUE);
    RECT wr; U(GetWindowRect)(w, &wr);
    if (b.st[0] & (STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_OFFSCREEN)) { t_note("window reported state 0x%lx; rectangle checks skipped", b.st[0]); }
    else {
        CHECKF(b.rc.right > b.rc.left && b.rc.bottom > b.rc.top, "empty title bar rc (%ld,%ld,%ld,%ld)", b.rc.left, b.rc.top, b.rc.right, b.rc.bottom);
        CHECKF(b.rc.left >= wr.left && b.rc.top >= wr.top && b.rc.right <= wr.right && b.rc.bottom <= wr.bottom, "title bar (%ld,%ld,%ld,%ld) not inside window (%ld,%ld,%ld,%ld)",
               b.rc.left, b.rc.top, b.rc.right, b.rc.bottom, wr.left, wr.top, wr.right, wr.bottom);
        CHECKF(b.rc.bottom - b.rc.top <= 80, "title bar height %ld", b.rc.bottom - b.rc.top);
    }
    U(DestroyWindow)(w);
}

TEST(gap8_titlebar_hidden_and_minimized_state_note) {
    NEED_USER(); REQUIRE(U(GetTitleBarInfo)); REQUIRE(U(ShowWindow));
    HWND w = mkwin(WS_OVERLAPPEDWINDOW, 0, NULL, L"Static");     /* not visible */
    WCHK(w);
    struct tbi b; memset(&b, 0, sizeof b); b.cb = TB_SIZE;
    G8EQ(U(GetTitleBarInfo)(w, (PTITLEBARINFO)&b), TRUE);
    CHECK(b.st[0] & STATE_SYSTEM_FOCUSABLE);
    t_note("hidden captioned window rgstate[0]=0x%lx rc(%ld,%ld,%ld,%ld)", b.st[0], b.rc.left, b.rc.top, b.rc.right, b.rc.bottom);
    U(ShowWindow)(w, SW_SHOWMINNOACTIVE);
    memset(&b, 0, sizeof b); b.cb = TB_SIZE;
    G8EQ(U(GetTitleBarInfo)(w, (PTITLEBARINFO)&b), TRUE);
    t_note("minimized captioned window rgstate[0]=0x%lx", b.st[0]);
    CHECK(b.st[0] & STATE_SYSTEM_FOCUSABLE);
    U(DestroyWindow)(w);
}

TEST(gap8_titlebar_output_independent_of_input_garbage) {
    NEED_USER(); REQUIRE(U(GetTitleBarInfo));
    HWND w = mkwin(WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, NULL, L"Static");
    WCHK(w);
    /* Input fields beyond cbSize are zeroed in kernel space before the call: whatever the caller left in rcTitleBar / rgstate must not leak through. */
    struct tbi a, b;
    memset(&a, 0xff, sizeof a); a.cb = TB_SIZE;
    memset(&b, 0x00, sizeof b); b.cb = TB_SIZE;
    G8EQ(U(GetTitleBarInfo)(w, (PTITLEBARINFO)&a), TRUE);
    G8EQ(U(GetTitleBarInfo)(w, (PTITLEBARINFO)&b), TRUE);
    CHECK(!memcmp(&a, &b, sizeof a));
    U(DestroyWindow)(w);
}

/* ================================================================= NtUserGetScrollBarInfo ============ */

#define SB_SIZE 0x3c
struct sbi { DWORD cb; RECT rc; int dxyLineButton, xyThumbTop, xyThumbBottom, reserved; DWORD st[6]; };

TEST(gap8_scrollbarinfo_invalid_hwnd) {
    NEED_USER(); REQUIRE(U(GetScrollBarInfo));
    struct badhwnd t[8]; int n = bad_hwnds(t, 8);
    for (int i = 0; i < n; i++)
        for (int id = -6; id <= -5; id++) {
            struct sbi b; memset(&b, 0x5a, sizeof b); b.cb = SB_SIZE;
            SetLastError(0);
            BOOL r = U(GetScrollBarInfo)(t[i].h, id, (PSCROLLBARINFO)&b);
            CHECKF(r == FALSE, "%s id %d", t[i].what, id);
            CHECKF(GetLastError() == ERROR_INVALID_WINDOW_HANDLE, "%s id %d err %lu", t[i].what, id, GetLastError());
        }
}

TEST(gap8_scrollbarinfo_cbsize_protocol) {
    NEED_USER(); REQUIRE(U(GetScrollBarInfo));
    HWND w = mkwin(WS_POPUP | WS_VSCROLL | WS_HSCROLL | WS_VISIBLE, 0, NULL, L"Static");
    WCHK(w);
    static const DWORD bad[] = { 0, 4, 0x3b, 0x3d, 0x40, 0x2c, 0xffffffffu };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++)
        for (int id = -6; id <= -5; id++) {
            union { struct sbi b; BYTE raw[0x80]; } u; memset(&u, 0x5a, sizeof u); u.b.cb = bad[i];
            SetLastError(0);
            BOOL r = U(GetScrollBarInfo)(w, id, (PSCROLLBARINFO)&u.b);
            CHECKF(r == FALSE, "cbSize 0x%lx id %d", bad[i], id);
            CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "cbSize 0x%lx id %d err %lu", bad[i], id, GetLastError());
        }
    U(DestroyWindow)(w);
}

TEST(gap8_scrollbarinfo_bad_object_id) {
    NEED_USER(); REQUIRE(U(GetScrollBarInfo));
    HWND w = mkwin(WS_POPUP | WS_VSCROLL | WS_HSCROLL | WS_VISIBLE, 0, NULL, L"Static");
    WCHK(w);
    static const LONG ids[] = { 0, 1, -1, -2, -3, -7, -8, 5, 6, 0x7fffffff, (LONG)0x80000000, -0x10000 };
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        struct sbi b; memset(&b, 0x5a, sizeof b); b.cb = SB_SIZE;
        SetLastError(0);
        BOOL r = U(GetScrollBarInfo)(w, ids[i], (PSCROLLBARINFO)&b);
        CHECKF(r == FALSE, "idObject %ld", ids[i]);
        CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "idObject %ld err %lu", ids[i], GetLastError());
        CHECKF(b.st[0] == 0x5a5a5a5a, "idObject %ld: output modified", ids[i]);
    }
    U(DestroyWindow)(w);
}

TEST(gap8_scrollbarinfo_absent_bars_are_invisible) {
    NEED_USER(); REQUIRE(U(GetScrollBarInfo));
    /* Window with neither scroll bar style: both objects answer TRUE with STATE_SYSTEM_INVISIBLE (0x8000) and nothing else. */
    HWND w = mkwin(WS_OVERLAPPEDWINDOW & ~(WS_VSCROLL | WS_HSCROLL), 0, NULL, L"Static");
    WCHK(w);
    for (int id = -6; id <= -5; id++) {
        struct sbi b; memset(&b, 0, sizeof b); b.cb = SB_SIZE;
        SetLastError(0);
        G8EQ(U(GetScrollBarInfo)(w, id, (PSCROLLBARINFO)&b), TRUE);
        CHECKF(b.st[0] & STATE_SYSTEM_INVISIBLE, "id %d rgstate[0]=0x%lx", id, b.st[0]);
        CHECKF(b.cb == SB_SIZE, "id %d cbSize changed", id);
    }
    U(DestroyWindow)(w);
}

TEST(gap8_scrollbarinfo_only_requested_bar_present) {
    NEED_USER(); REQUIRE(U(GetScrollBarInfo)); REQUIRE(U(SetScrollRange));
    HWND wv = mkwin(WS_POPUP | WS_VSCROLL | WS_VISIBLE, 0, NULL, L"Static");
    HWND wh = mkwin(WS_POPUP | WS_HSCROLL | WS_VISIBLE, 0, NULL, L"Static");
    if (!(wv && wh) && t_is_wine()) { t_skip("no window support in this environment"); return; }
    CHECK(wv && wh);
    if (wv && wh) {
        U(SetScrollRange)(wv, SB_VERT, 0, 100, FALSE);
        U(SetScrollRange)(wh, SB_HORZ, 0, 100, FALSE);
        struct sbi b;
        memset(&b, 0, sizeof b); b.cb = SB_SIZE;
        G8EQ(U(GetScrollBarInfo)(wv, OBJID_HSCROLL, (PSCROLLBARINFO)&b), TRUE);
        CHECK(b.st[0] & STATE_SYSTEM_INVISIBLE);                 /* no WS_HSCROLL on wv */
        memset(&b, 0, sizeof b); b.cb = SB_SIZE;
        G8EQ(U(GetScrollBarInfo)(wv, OBJID_VSCROLL, (PSCROLLBARINFO)&b), TRUE);
        CHECKF(!(b.st[0] & STATE_SYSTEM_INVISIBLE), "vscroll present, rgstate[0]=0x%lx", b.st[0]);
        memset(&b, 0, sizeof b); b.cb = SB_SIZE;
        G8EQ(U(GetScrollBarInfo)(wh, OBJID_VSCROLL, (PSCROLLBARINFO)&b), TRUE);
        CHECK(b.st[0] & STATE_SYSTEM_INVISIBLE);
        memset(&b, 0, sizeof b); b.cb = SB_SIZE;
        G8EQ(U(GetScrollBarInfo)(wh, OBJID_HSCROLL, (PSCROLLBARINFO)&b), TRUE);
        CHECKF(!(b.st[0] & STATE_SYSTEM_INVISIBLE), "hscroll present, rgstate[0]=0x%lx", b.st[0]);
    }
    if (wv) U(DestroyWindow)(wv);
    if (wh) U(DestroyWindow)(wh);
}

TEST(gap8_scrollbarinfo_geometry_of_nc_bar) {
    NEED_USER(); REQUIRE(U(GetScrollBarInfo)); REQUIRE(U(SetScrollInfo)); REQUIRE(U(GetWindowRect));
    HWND w = mkwin(WS_POPUP | WS_VSCROLL | WS_HSCROLL | WS_VISIBLE, 0, NULL, L"Static");
    WCHK(w);
    SCROLLINFO si = { sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS, 0, 99, 10, 30, 0 };
    U(SetScrollInfo)(w, SB_VERT, &si, FALSE);
    U(SetScrollInfo)(w, SB_HORZ, &si, FALSE);
    RECT wr; U(GetWindowRect)(w, &wr);
    for (int id = -6; id <= -5; id++) {
        struct sbi b; memset(&b, 0, sizeof b); b.cb = SB_SIZE;
        G8EQ(U(GetScrollBarInfo)(w, id, (PSCROLLBARINFO)&b), TRUE);
        if (b.st[0] & (STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_OFFSCREEN)) { t_note("id %d state 0x%lx", id, b.st[0]); continue; }
        CHECKF(b.rc.right > b.rc.left && b.rc.bottom > b.rc.top, "id %d empty rc", id);
        CHECKF(b.rc.left >= wr.left && b.rc.top >= wr.top && b.rc.right <= wr.right && b.rc.bottom <= wr.bottom, "id %d rc outside the window", id);
        int len = id == -5 ? b.rc.bottom - b.rc.top : b.rc.right - b.rc.left;
        CHECKF(b.dxyLineButton > 0 && 2 * b.dxyLineButton <= len, "id %d dxyLineButton %d length %d", id, b.dxyLineButton, len);
        CHECKF(b.xyThumbTop >= b.dxyLineButton && b.xyThumbBottom >= b.xyThumbTop && b.xyThumbBottom <= len - b.dxyLineButton, "id %d thumb [%d,%d] length %d button %d", id, b.xyThumbTop, b.xyThumbBottom, len, b.dxyLineButton);
        t_note("id %d rc(%ld,%ld,%ld,%ld) line %d thumb [%d,%d]", id, b.rc.left, b.rc.top, b.rc.right, b.rc.bottom, b.dxyLineButton, b.xyThumbTop, b.xyThumbBottom);
        G8EQ(b.reserved, 0);
        G8EQ(b.st[3], 0u);                                   /* thumb: not pressed, not disabled in a fresh bar */
    }
    U(DestroyWindow)(w);
}

TEST(gap8_scrollbarinfo_enable_flags_map_to_states) {
    NEED_USER(); REQUIRE(U(GetScrollBarInfo)); REQUIRE(U(EnableScrollBar)); REQUIRE(U(SetScrollRange));
    HWND w = mkwin(WS_POPUP | WS_VSCROLL | WS_HSCROLL | WS_VISIBLE, 0, NULL, L"Static");
    WCHK(w);
    U(SetScrollRange)(w, SB_VERT, 0, 100, FALSE);
    U(SetScrollRange)(w, SB_HORZ, 0, 100, FALSE);
    static const struct { UINT flags; DWORD mask[6]; } rows[] = {
        { ESB_DISABLE_LTUP,  { 0, 1, 1, 0, 0, 0 } },
        { ESB_DISABLE_RTDN,  { 0, 0, 0, 0, 1, 1 } },
        { ESB_DISABLE_BOTH,  { 1, 1, 1, 0, 1, 1 } },
        { ESB_ENABLE_BOTH,   { 0, 0, 0, 0, 0, 0 } },
    };
    for (int bar = 0; bar < 2; bar++)
        for (unsigned i = 0; i < sizeof rows / sizeof rows[0]; i++) {
            /* the disable bits accumulate (a button stays disabled until ESB_ENABLE_BOTH), so every row starts from the enabled state */
            U(EnableScrollBar)(w, bar ? SB_HORZ : SB_VERT, ESB_ENABLE_BOTH);
            U(EnableScrollBar)(w, bar ? SB_HORZ : SB_VERT, rows[i].flags);
            struct sbi b; memset(&b, 0, sizeof b); b.cb = SB_SIZE;
            G8EQ(U(GetScrollBarInfo)(w, bar ? OBJID_HSCROLL : OBJID_VSCROLL, (PSCROLLBARINFO)&b), TRUE);
            for (int k = 0; k < 6; k++)
                CHECKF((b.st[k] & STATE_SYSTEM_UNAVAILABLE) == rows[i].mask[k], "bar %d flags %u rgstate[%d]=0x%lx want unavailable=%lu", bar, rows[i].flags, k, b.st[k], rows[i].mask[k]);
        }
    U(DestroyWindow)(w);
}

TEST(gap8_scrollbarinfo_scrollbar_control_client_object) {
    NEED_USER(); REQUIRE(U(GetScrollBarInfo));
    HWND par = mkpopup();
    HWND sb = par ? U(CreateWindowExW)(0, L"ScrollBar", L"", WS_CHILD | WS_VISIBLE | SBS_HORZ, 0, 0, 150, 17, par, NULL, GetModuleHandleW(NULL), NULL) : NULL;
    if (!sb && t_is_wine()) { t_skip("no window support in this environment"); return; }
    CHECK(sb != NULL);
    if (sb) {
        struct sbi b; memset(&b, 0, sizeof b); b.cb = SB_SIZE;
        SetLastError(0);
        BOOL r = U(GetScrollBarInfo)(sb, OBJID_CLIENT, (PSCROLLBARINFO)&b);
        CHECKF(r, "OBJID_CLIENT on a ScrollBar control err %lu", GetLastError());
        if (r) {
            CHECKF(b.rc.right > b.rc.left && b.rc.bottom > b.rc.top, "empty rc");
            t_note("sb control rgstate[0]=0x%lx line %d thumb [%d,%d]", b.st[0], b.dxyLineButton, b.xyThumbTop, b.xyThumbBottom);
        }
        memset(&b, 0, sizeof b); b.cb = SB_SIZE + 4;
        SetLastError(0);
        CHECK(!U(GetScrollBarInfo)(sb, OBJID_CLIENT, (PSCROLLBARINFO)&b));
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
    if (par) U(DestroyWindow)(par);
}

TEST(gap8_scrollbarinfo_client_object_on_plain_window_not_supported) {
    NEED_USER(); REQUIRE(U(GetScrollBarInfo));
    /* OBJID_CLIENT on a window that is not a scroll bar control sends SBM_GETSCROLLBARINFO (0xEB), which DefWindowProc ignores: no data returned. */
    HWND w = mkwin(WS_POPUP | WS_VISIBLE, 0, NULL, L"Static");
    WCHK(w);
    struct sbi b; memset(&b, 0, sizeof b); b.cb = SB_SIZE;
    SetLastError(0);
    BOOL r = U(GetScrollBarInfo)(w, OBJID_CLIENT, (PSCROLLBARINFO)&b);
    CHECKF(r == FALSE, "plain window answered OBJID_CLIENT");
    U(DestroyWindow)(w);
}

/* ================================================================= NtUserGetComboBoxInfo ============= */

#define CB_SIZE 0x40

TEST(gap8_combobox_invalid_hwnd) {
    NEED_USER(); REQUIRE(U(GetComboBoxInfo));
    struct badhwnd t[8]; int n = bad_hwnds(t, 8);
    for (int i = 0; i < n; i++) {
        COMBOBOXINFO ci; memset(&ci, 0x5a, sizeof ci); ci.cbSize = CB_SIZE;
        SetLastError(0);
        BOOL r = U(GetComboBoxInfo)(t[i].h, &ci);
        CHECKF(r == FALSE, "%s", t[i].what);
        CHECKF(GetLastError() == ERROR_INVALID_WINDOW_HANDLE, "%s err %lu", t[i].what, GetLastError());
    }
}

TEST(gap8_combobox_struct_size_is_0x40) {
    CHECK(sizeof(COMBOBOXINFO) == CB_SIZE);
    CHECK(offsetof(COMBOBOXINFO, stateButton) == 0x24);
    CHECK(offsetof(COMBOBOXINFO, hwndCombo) == 0x28);
    CHECK(offsetof(COMBOBOXINFO, hwndList) == 0x38);
}

static HWND mkcombo(DWORD cbs, HWND *par) {
    *par = mkpopup();
    if (!*par) return NULL;
    return U(CreateWindowExW)(0, L"ComboBox", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | cbs, 0, 0, 160, 120, *par, NULL, GetModuleHandleW(NULL), NULL);
}

TEST(gap8_combobox_cbsize_protocol) {
    NEED_USER(); REQUIRE(U(GetComboBoxInfo));
    HWND par, cb = mkcombo(CBS_DROPDOWNLIST, &par);
    if (!cb) { if (par) U(DestroyWindow)(par); WCHK(cb); }
    static const DWORD bad[] = { 0, 4, 0x3c, 0x3f, 0x41, 0x44, 0x80, 0xffffffffu };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        union { COMBOBOXINFO c; BYTE raw[0x100]; } u; memset(&u, 0x5a, sizeof u); u.c.cbSize = bad[i];
        SetLastError(0);
        BOOL r = U(GetComboBoxInfo)(cb, &u.c);
        CHECKF(r == FALSE, "cbSize 0x%lx", bad[i]);
        CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "cbSize 0x%lx err %lu", bad[i], GetLastError());
        CHECKF(u.raw[0x30] == 0x5a, "cbSize 0x%lx: output modified", bad[i]);
    }
    U(DestroyWindow)(par);
}

TEST(gap8_combobox_info_fields_per_style) {
    NEED_USER(); REQUIRE(U(GetComboBoxInfo)); REQUIRE(U(GetClassNameW));
    static const struct { DWORD cbs; int hasEdit; const char *name; } rows[] = {
        { CBS_SIMPLE, 1, "simple" }, { CBS_DROPDOWN, 1, "dropdown" }, { CBS_DROPDOWNLIST, 0, "dropdownlist" },
    };
    for (unsigned i = 0; i < 3; i++) {
        HWND par, cb = mkcombo(rows[i].cbs, &par);
        CHECKF(cb != NULL, "%s", rows[i].name);
        if (cb) {
            COMBOBOXINFO ci; memset(&ci, 0, sizeof ci); ci.cbSize = CB_SIZE;
            BOOL r = U(GetComboBoxInfo)(cb, &ci);
            CHECKF(r, "%s", rows[i].name);
            if (r) {
                CHECKF(ci.hwndCombo == cb, "%s hwndCombo %p", rows[i].name, (void *)ci.hwndCombo);
                CHECKF(ci.hwndList != NULL, "%s hwndList NULL", rows[i].name);
                CHECKF(ci.cbSize == CB_SIZE, "%s cbSize %lu", rows[i].name, ci.cbSize);
                if (rows[i].hasEdit) {
                    CHECKF(ci.hwndItem != NULL, "%s hwndItem NULL", rows[i].name);
                    wchar_t cn[32] = L""; if (ci.hwndItem) U(GetClassNameW)(ci.hwndItem, cn, 32);
                    CHECKF(!lstrcmpiW(cn, L"Edit"), "%s item class %ls", rows[i].name, cn);
                } else t_note("%s hwndItem %p", rows[i].name, (void *)ci.hwndItem);
                if (ci.hwndList) { wchar_t cn[32] = L""; U(GetClassNameW)(ci.hwndList, cn, 32); CHECKF(!lstrcmpiW(cn, L"ComboLBox"), "%s list class %ls", rows[i].name, cn); }
                CHECKF(!(ci.stateButton & STATE_SYSTEM_PRESSED), "%s: button pressed on a fresh combo (0x%lx)", rows[i].name, ci.stateButton);
                CHECKF(ci.rcItem.right > ci.rcItem.left && ci.rcItem.bottom > ci.rcItem.top, "%s empty rcItem", rows[i].name);
                t_note("%s rcItem(%ld,%ld,%ld,%ld) rcButton(%ld,%ld,%ld,%ld) state 0x%lx", rows[i].name, ci.rcItem.left, ci.rcItem.top, ci.rcItem.right, ci.rcItem.bottom, ci.rcButton.left, ci.rcButton.top, ci.rcButton.right, ci.rcButton.bottom, ci.stateButton);
                if (rows[i].cbs != CBS_SIMPLE) CHECKF(ci.rcButton.right > ci.rcButton.left, "%s empty rcButton", rows[i].name);
            }
        }
        if (par) U(DestroyWindow)(par);
    }
}

TEST(gap8_combobox_info_via_list_window) {
    NEED_USER(); REQUIRE(U(GetComboBoxInfo));
    HWND par, cb = mkcombo(CBS_DROPDOWN, &par);
    if (!cb) { if (par) U(DestroyWindow)(par); WCHK(cb); }
    COMBOBOXINFO ci; memset(&ci, 0, sizeof ci); ci.cbSize = CB_SIZE;
    if (U(GetComboBoxInfo)(cb, &ci) && ci.hwndList) {
        COMBOBOXINFO l; memset(&l, 0, sizeof l); l.cbSize = CB_SIZE;
        SetLastError(0);
        BOOL r = U(GetComboBoxInfo)(ci.hwndList, &l);
        t_note("GetComboBoxInfo(list) -> %d, hwndCombo %p (owner combo %p)", r, (void *)l.hwndCombo, (void *)cb);
        if (r) CHECK(l.hwndCombo == cb);
        l.cbSize = CB_SIZE + 4; SetLastError(0);
        CHECK(!U(GetComboBoxInfo)(ci.hwndList, &l));
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
    U(DestroyWindow)(par);
}

TEST(gap8_combobox_non_combo_windows_answer_false) {
    NEED_USER(); REQUIRE(U(GetComboBoxInfo));
    static const wchar_t *cls[] = { L"Static", L"Button", L"Edit", L"ListBox" };
    HWND par = mkpopup();
    WCHK(par);
    for (unsigned i = 0; i < 4; i++) {
        HWND c = U(CreateWindowExW)(0, cls[i], L"", WS_CHILD, 0, 0, 50, 50, par, NULL, GetModuleHandleW(NULL), NULL);
        if (!c && t_is_wine()) { t_skip("no window support in this environment"); return; } CHECKF(c != NULL, "%ls", cls[i]); if (!c) continue;
        COMBOBOXINFO ci; memset(&ci, 0x5a, sizeof ci); ci.cbSize = CB_SIZE;
        BOOL r = U(GetComboBoxInfo)(c, &ci);
        CHECKF(r == FALSE, "%ls answered WM_GETCOMBOBOXINFO", cls[i]);
    }
    COMBOBOXINFO ci; memset(&ci, 0, sizeof ci); ci.cbSize = CB_SIZE;
    CHECK(!U(GetComboBoxInfo)(par, &ci));
    U(DestroyWindow)(par);
}

/* ================================================================= NtUserGetGestureConfig ============ */

UDECL(BOOL, NtUserGetGestureConfig, (HWND, DWORD, DWORD, UINT *, void *, UINT));
struct gcfg { DWORD id, want, block; };

static BOOL ggc(int viaNt, HWND h, DWORD flags, UINT *cnt, void *cfg, UINT cb) {
    if (viaNt) return UN(NtUserGetGestureConfig)(h, 0, flags, cnt, cfg, cb);
    return ((BOOL (WINAPI *)(HWND, DWORD, DWORD, UINT *, void *, UINT))t_proc_in("user32.dll", "GetGestureConfig"))(h, 0, flags, cnt, cfg, cb);
}
static int ggc_ready(void) { if (t_is_wine()) return 0; /* Wine aborts on its unimplemented stub */ return t_proc_in("user32.dll", "GetGestureConfig") && UN(NtUserGetGestureConfig); }

TEST(gap8_gestureconfig_invalid_hwnd) {
    NEED_USER(); if (!ggc_ready()) { t_skip("GetGestureConfig missing"); return; }
    struct badhwnd t[8]; int n = bad_hwnds(t, 8);
    for (int v = 0; v < 2; v++)
        for (int i = 0; i < n; i++) {
            struct gcfg g[2] = { { 3, 0, 0 }, { 4, 0, 0 } }; UINT c = 2;
            SetLastError(0);
            BOOL r = ggc(v, t[i].h, 0, &c, g, 12);
            CHECKF(r == FALSE, "route %d %s", v, t[i].what);
            CHECKF(GetLastError() == ERROR_INVALID_WINDOW_HANDLE, "route %d %s err %lu", v, t[i].what, GetLastError());
        }
}

TEST(gap8_gestureconfig_cbsize_must_be_12) {
    NEED_USER(); if (!ggc_ready()) { t_skip("GetGestureConfig missing"); return; }
    HWND w = mkpopup(); WCHK(w);
    static const UINT bad[] = { 0, 1, 4, 8, 11, 13, 16, 24, 0x100, 0xffffffffu };
    for (int v = 0; v < 2; v++)
        for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
            struct gcfg g[4] = { { 3, 0, 0 } }; UINT c = 1;
            SetLastError(0);
            BOOL r = ggc(v, w, 0, &c, g, bad[i]);
            CHECKF(r == FALSE, "route %d cbSize %u", v, bad[i]);
            CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "route %d cbSize %u err %lu", v, bad[i], GetLastError());
        }
    U(DestroyWindow)(w);
}

TEST(gap8_gestureconfig_flags_only_bit0) {
    NEED_USER(); if (!ggc_ready()) { t_skip("GetGestureConfig missing"); return; }
    HWND w = mkpopup(); WCHK(w);
    static const DWORD bad[] = { 2, 3, 4, 0x10, 0x100, 0x80000000u, 0xfffffffeu, 0xffffffffu };
    for (int v = 0; v < 2; v++)
        for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
            struct gcfg g[1] = { { 3, 0, 0 } }; UINT c = 1;
            SetLastError(0);
            BOOL r = ggc(v, w, bad[i], &c, g, 12);
            CHECKF(r == FALSE, "route %d flags 0x%lx", v, bad[i]);
            CHECKF(GetLastError() == ERROR_INVALID_FLAGS, "route %d flags 0x%lx err %lu", v, bad[i], GetLastError());
        }
    U(DestroyWindow)(w);
}

TEST(gap8_gestureconfig_size_checked_before_flags) {
    NEED_USER(); if (!ggc_ready()) { t_skip("GetGestureConfig missing"); return; }
    HWND w = mkpopup(); WCHK(w);
    struct gcfg g[1] = { { 3, 0, 0 } }; UINT c = 1;
    SetLastError(0);
    CHECK(!ggc(1, w, 2, &c, g, 8));
    EXPECT_ERR(ERROR_INVALID_PARAMETER);          /* wrong cbSize wins over the bad flag */
    U(DestroyWindow)(w);
}

TEST(gap8_gestureconfig_count_and_buffer_validation) {
    NEED_USER(); if (!ggc_ready()) { t_skip("GetGestureConfig missing"); return; }
    HWND w = mkpopup(); WCHK(w);
    static struct gcfg big[300];
    static const UINT counts[] = { 0, 257, 258, 300, 0x10000, 0xffffffffu };
    for (int v = 0; v < 2; v++) {
        for (unsigned i = 0; i < sizeof counts / sizeof counts[0]; i++) {
            UINT c = counts[i];
            SetLastError(0);
            BOOL r = ggc(v, w, 0, &c, big, 12);
            CHECKF(r == FALSE, "route %d count %u", v, counts[i]);
            CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "route %d count %u err %lu", v, counts[i], GetLastError());
            CHECKF(c == counts[i], "route %d count %u: *pcIDs rewritten to %u on failure", v, counts[i], c);
        }
        UINT c = 1;
        SetLastError(0);
        CHECKF(!ggc(v, w, 0, &c, NULL, 12), "route %d NULL config buffer", v);
        CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "route %d NULL config err %lu", v, GetLastError());
    }
    U(DestroyWindow)(w);
}

TEST(gap8_gestureconfig_valid_requests_note) {
    NEED_USER(); if (!ggc_ready()) { t_skip("GetGestureConfig missing"); return; }
    HWND w = mkpopup(); WCHK(w);
    /* Boundary counts are accepted by the count test (1 and 256): the call may still fail later, but never with ERROR_INVALID_PARAMETER from the count. */
    static struct gcfg g[256];
    static const UINT counts[] = { 1, 2, 5, 256 };
    for (int v = 0; v < 2; v++)
        for (int fl = 0; fl < 2; fl++)
            for (unsigned i = 0; i < sizeof counts / sizeof counts[0]; i++) {
                for (UINT k = 0; k < counts[i]; k++) { g[k].id = 1 + k % 8; g[k].want = 0xdead; g[k].block = 0xbeef; }
                UINT c = counts[i];
                SetLastError(0);
                BOOL r = ggc(v, w, fl, &c, g, 12);
                DWORD e = GetLastError();
                t_note("route %d flags %d count %u -> %d err %lu first{id %lu want 0x%lx block 0x%lx}", v, fl, counts[i], r, e, g[0].id, g[0].want, g[0].block);
                CHECKF(r || e != ERROR_INVALID_FLAGS, "route %d flags %d rejected as invalid flags", v, fl);
            }
    U(DestroyWindow)(w);
}

/* ================================================================= NtUserGetDpiForMonitor ============ */

UDECL(BOOL, GetDpiForMonitorInternal, (HMONITOR, UINT, UINT *, UINT *));
UDECL(LONG, NtUserGetDpiForMonitor, (HMONITOR, int, UINT *, UINT *));
UDECL(BOOL, LogicalToPhysicalPointForPerMonitorDPI, (HWND, POINT *));
UDECL(BOOL, NtUserLogicalToPerMonitorDPIPhysicalPoint, (HWND, POINT *));

static HMONITOR primary_mon(void) { return U(MonitorFromPoint) ? U(MonitorFromPoint)((POINT){0, 0}, MONITOR_DEFAULTTOPRIMARY) : NULL; }

TEST(gap8_dpimonitor_type_out_of_range_rejected_in_user32) {
    NEED_USER(); REQUIRE(UN(GetDpiForMonitorInternal)); REQUIRE(U(MonitorFromPoint));
    HMONITOR m = primary_mon(); if (!m && t_is_wine()) { t_skip("no monitors in this environment"); return; } CHECK(m != NULL); if (!m) return;
    static const UINT bad[] = { 3, 4, 5, 0x80, 0x10000, 0xffffffffu };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        UINT x = 0x5a5a, y = 0x5a5a;
        SetLastError(0);
        BOOL r = UN(GetDpiForMonitorInternal)(m, bad[i], &x, &y);
        CHECKF(r == FALSE, "type %u", bad[i]);
        CHECKF(GetLastError() == ERROR_BAD_ARGUMENTS, "type %u err %lu", bad[i], GetLastError());
        CHECKF(x == 0x5a5a && y == 0x5a5a, "type %u: outputs written", bad[i]);
    }
}

TEST(gap8_dpimonitor_invalid_monitor_handle) {
    NEED_USER(); REQUIRE(UN(GetDpiForMonitorInternal)); REQUIRE(UN(NtUserGetDpiForMonitor));
    HWND w = mkpopup();
    HMONITOR bad[] = { NULL, (HMONITOR)(ULONG_PTR)1, (HMONITOR)(ULONG_PTR)0x12345678, (HMONITOR)INVALID_HANDLE_VALUE };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++)
        for (UINT type = 0; type < 3; type++) {
            UINT x = 0x5a5a, y = 0x5a5a;
            SetLastError(0);
            BOOL r = UN(GetDpiForMonitorInternal)(bad[i], type, &x, &y);
            CHECKF(r == FALSE, "row %u type %u", i, type);
            CHECKF(GetLastError() == ERROR_INVALID_HANDLE, "row %u type %u err %lu", i, type, GetLastError());
            CHECKF(x == 0x5a5a && y == 0x5a5a, "row %u type %u: outputs written", i, type);
            SetLastError(0);
            LONG r2 = UN(NtUserGetDpiForMonitor)(bad[i], type, &x, &y);
            CHECKF(r2 == 0, "syscall row %u type %u ret %ld", i, type, r2);
            CHECKF(GetLastError() == ERROR_INVALID_HANDLE, "syscall row %u type %u err %lu", i, type, GetLastError());
        }
    if (w) U(DestroyWindow)(w);
}

TEST(gap8_dpimonitor_primary_monitor_values) {
    NEED_USER(); REQUIRE(UN(GetDpiForMonitorInternal)); REQUIRE(U(MonitorFromPoint));
    HMONITOR m = primary_mon(); if (!m && t_is_wine()) { t_skip("no monitors in this environment"); return; } CHECK(m != NULL); if (!m) return;
    UINT x = 0, y = 0;
    SetLastError(0);
    BOOL r = UN(GetDpiForMonitorInternal)(m, 0, &x, &y);
    CHECKF(r, "effective DPI err %lu", GetLastError());
    if (r) {
        CHECKF(x == y, "effective %u x %u", x, y);
        CHECKF(x >= 48 && x <= 960, "effective DPI %u", x);
        t_note("effective DPI %u,%u", x, y);
    }
    x = y = 0;
    r = UN(GetDpiForMonitorInternal)(m, 1, &x, &y);
    CHECKF(r, "angular DPI err %lu", GetLastError());
    if (r) { CHECKF(x > 0 && y > 0, "angular %u,%u", x, y); t_note("angular DPI %u,%u", x, y); }
    x = y = 0xffff;
    SetLastError(0);
    r = UN(GetDpiForMonitorInternal)(m, 2, &x, &y);
    t_note("raw DPI -> %d %u,%u err %lu (a virtual display may not report one)", r, x, y, GetLastError());
    if (r) CHECK(x > 0 && y > 0);
}

TEST(gap8_dpimonitor_syscall_matches_wrapper) {
    NEED_USER(); REQUIRE(UN(GetDpiForMonitorInternal)); REQUIRE(UN(NtUserGetDpiForMonitor));
    HMONITOR m = primary_mon(); if (!m && t_is_wine()) { t_skip("no monitors in this environment"); return; } CHECK(m != NULL); if (!m) return;
    for (UINT type = 0; type < 3; type++) {
        UINT x1 = 0, y1 = 0, x2 = 0, y2 = 0;
        BOOL r1 = UN(GetDpiForMonitorInternal)(m, type, &x1, &y1);
        LONG r2 = UN(NtUserGetDpiForMonitor)(m, type, &x2, &y2);
        CHECKF((r1 != 0) == (r2 != 0), "type %u: %d vs %ld", type, r1, r2);
        if (r1 && r2) CHECKF(x1 == x2 && y1 == y2, "type %u: %u,%u vs %u,%u", type, x1, y1, x2, y2);
    }
}

TEST(gap8_dpimonitor_unaware_thread_sees_96_or_system) {
    NEED_USER(); REQUIRE(UN(GetDpiForMonitorInternal)); REQUIRE(U(GetDpiForSystem)); REQUIRE(U(MonitorFromPoint));
    HMONITOR m = primary_mon(); if (!m && t_is_wine()) { t_skip("no monitors in this environment"); return; } CHECK(m != NULL); if (!m) return;
    UINT x = 0, y = 0;
    if (!UN(GetDpiForMonitorInternal)(m, 0, &x, &y)) { t_skip("effective DPI query failed"); return; }
    /* effective DPI is 0x60 for an unaware thread, the process system DPI for a system-aware one, the monitor DPI for per-monitor awareness */
    UINT sys = U(GetDpiForSystem)();
    t_note("effective %u, system %u", x, sys);
    CHECKF(x == 96 || x == sys || (x >= 96 && x <= 960), "effective DPI %u", x);
}

/* ================================================================= NtUserLogicalToPerMonitorDPIPhysicalPoint */

TEST(gap8_l2p_invalid_hwnd) {
    NEED_USER(); REQUIRE(UN(LogicalToPhysicalPointForPerMonitorDPI)); REQUIRE(UN(NtUserLogicalToPerMonitorDPIPhysicalPoint));
    struct badhwnd t[8]; int n = bad_hwnds(t, 8);
    for (int i = 0; i < n; i++) {
        if (!t[i].h) continue;       /* NULL is the valid "desktop" argument, tested separately */
        POINT p = { 11, 22 };
        SetLastError(0);
        BOOL r = UN(LogicalToPhysicalPointForPerMonitorDPI)(t[i].h, &p);
        CHECKF(r == FALSE, "%s", t[i].what);
        CHECKF(p.x == 11 && p.y == 22, "%s: point modified", t[i].what);
        t_note("%s -> err %lu", t[i].what, GetLastError());
        SetLastError(0);
        r = UN(NtUserLogicalToPerMonitorDPIPhysicalPoint)(t[i].h, &p);
        CHECKF(r == FALSE, "syscall %s", t[i].what);
    }
}

TEST(gap8_l2p_null_hwnd_converts) {
    NEED_USER(); REQUIRE(UN(LogicalToPhysicalPointForPerMonitorDPI));
    static const POINT pts[] = { {0,0}, {10,10}, {100,200}, {-5,-5}, {2000,1000} };
    for (unsigned i = 0; i < sizeof pts / sizeof pts[0]; i++) {
        POINT p = pts[i];
        SetLastError(0);
        BOOL r = UN(LogicalToPhysicalPointForPerMonitorDPI)(NULL, &p);
        CHECKF(r, "point %u err %lu", i, GetLastError());
        t_note("(%ld,%ld) -> (%ld,%ld)", pts[i].x, pts[i].y, p.x, p.y);
    }
}

TEST(gap8_l2p_point_outside_window_is_rejected) {
    NEED_USER(); REQUIRE(UN(LogicalToPhysicalPointForPerMonitorDPI)); REQUIRE(U(GetWindowRect));
    HWND w = mkwin(WS_POPUP | WS_VISIBLE, 0, NULL, L"Static");
    WCHK(w);
    RECT r; U(GetWindowRect)(w, &r);
    POINT in = { (r.left + r.right) / 2, (r.top + r.bottom) / 2 };
    POINT outs[] = { { r.left - 50, in.y }, { r.right + 50, in.y }, { in.x, r.top - 50 }, { in.x, r.bottom + 50 }, { r.right + 500, r.bottom + 500 } };
    POINT p = in;
    int hidpi = U(GetDpiForSystem) && U(GetDpiForSystem)() != 96;     /* an unaware thread's GetWindowRect is virtualised at other scales */
    SetLastError(0);
    BOOL ok = UN(LogicalToPhysicalPointForPerMonitorDPI)(w, &p);
    if (hidpi) t_note("system DPI != 96: rectangle comparisons are in virtualised units, only recorded (ok=%d)", ok);
    else CHECKF(ok, "centre point err %lu", GetLastError());
    if (ok) t_note("window-centre (%ld,%ld) -> (%ld,%ld)", in.x, in.y, p.x, p.y);
    for (unsigned i = 0; i < 5; i++) {
        POINT q = outs[i];
        BOOL rr = UN(LogicalToPhysicalPointForPerMonitorDPI)(w, &q);
        if (!hidpi) CHECKF(rr == FALSE, "outside point %u accepted", i);
        if (!rr) CHECKF(q.x == outs[i].x && q.y == outs[i].y, "outside point %u modified", i);
    }
    U(DestroyWindow)(w);
}

TEST(gap8_l2p_same_awareness_is_identity) {
    NEED_USER(); REQUIRE(UN(LogicalToPhysicalPointForPerMonitorDPI)); REQUIRE(U(GetWindowRect)); REQUIRE(U(GetDpiForSystem));
    HWND w = mkwin(WS_POPUP | WS_VISIBLE, 0, NULL, L"Static");
    WCHK(w);
    RECT r; U(GetWindowRect)(w, &r);
    POINT p = { r.left + 10, r.top + 10 }, q = p;
    if (UN(LogicalToPhysicalPointForPerMonitorDPI)(w, &q)) {
        UINT dpi = U(GetDpiForSystem)();
        t_note("system dpi %u: (%ld,%ld) -> (%ld,%ld)", dpi, p.x, p.y, q.x, q.y);
        if (dpi == 96) CHECK(q.x == p.x && q.y == p.y);      /* no scaling to apply at 100% */
    }
    U(DestroyWindow)(w);
}

/* ================================================================= NtUserIsClipboardFormatAvailable == */

TEST(gap8_clipfmt_zero_and_unregistered) {
    NEED_USER(); REQUIRE(U(IsClipboardFormatAvailable)); REQUIRE(U(RegisterClipboardFormatW));
    G8EQ(U(IsClipboardFormatAvailable)(0), FALSE);
    wchar_t nm[64];
    for (int i = 0; i < 6; i++) {
        wsprintfW(nm, L"win32kt_gap8_%lu_%d", GetCurrentProcessId(), i);
        UINT f = U(RegisterClipboardFormatW)(nm);
        CHECKF(f >= 0xC000 && f <= 0xFFFF, "registered id 0x%x", f);
        CHECKF(U(IsClipboardFormatAvailable)(f) == FALSE, "private format 0x%x", f);
    }
}

TEST(gap8_clipfmt_registered_ids_stable_and_in_range) {
    NEED_USER(); REQUIRE(U(RegisterClipboardFormatW)); REQUIRE(U(GetClipboardFormatNameW));
    wchar_t nm[64]; wsprintfW(nm, L"win32kt_gap8_stable_%lu", GetCurrentProcessId());
    UINT a = U(RegisterClipboardFormatW)(nm), b = U(RegisterClipboardFormatW)(nm);
    G8EQ(a, b);
    CHECK(a >= 0xC000 && a <= 0xFFFF);
    wchar_t back[64] = L"";
    G8EQ(U(GetClipboardFormatNameW)(a, back, 64), lstrlenW(nm));
    EXPECT_WSTR(back, nm);
    CHECK(!U(IsClipboardFormatAvailable)(a));
}

TEST(gap8_clipfmt_matches_enumeration) {
    NEED_USER(); REQUIRE(U(IsClipboardFormatAvailable)); REQUIRE(U(OpenClipboard)); REQUIRE(U(CloseClipboard)); REQUIRE(U(EnumClipboardFormats)); REQUIRE(U(GetClipboardSequenceNumber)); REQUIRE(U(CountClipboardFormats));
    HWND w = mkpopup(); WCHK(w);
    int tries = 0, done = 0;
    while (!done && tries++ < 5) {
        if (!U(OpenClipboard)(w)) { Sleep(100); continue; }
        DWORD seq = U(GetClipboardSequenceNumber)();
        UINT fmts[128]; int n = 0;
        UINT f = 0;
        while ((f = U(EnumClipboardFormats)(f)) != 0 && n < 128) fmts[n++] = f;
        int cnt = U(CountClipboardFormats)();
        BOOL avail[128];
        for (int i = 0; i < n; i++) avail[i] = U(IsClipboardFormatAvailable)(fmts[i]);
        int probe_ok = U(IsClipboardFormatAvailable)(0xC000 + 0x3ff0) == FALSE;
        DWORD seq2 = U(GetClipboardSequenceNumber)();
        U(CloseClipboard)();
        if (seq != seq2) continue;                       /* another process changed it meanwhile */
        G8EQ(cnt, n);
        for (int i = 0; i < n; i++) CHECKF(avail[i], "enumerated format 0x%x reported unavailable", fmts[i]);
        t_note("%d clipboard formats", n);
        (void)probe_ok;
        done = 1;
    }
    if (!done) t_note("clipboard busy; enumeration comparison skipped");
    U(DestroyWindow)(w);
}

TEST(gap8_clipfmt_query_does_not_need_open_clipboard) {
    NEED_USER(); REQUIRE(U(IsClipboardFormatAvailable)); REQUIRE(U(GetOpenClipboardWindow));
    /* The query takes only the shared USER lock and an access check; it works with the clipboard closed. */
    HWND ow = U(GetOpenClipboardWindow)();
    SetLastError(0);
    BOOL a = U(IsClipboardFormatAvailable)(CF_UNICODETEXT);
    DWORD e = GetLastError();
    t_note("CF_UNICODETEXT available=%d err=%lu (open clipboard window %p)", a, e, (void *)ow);
    CHECK(a == TRUE || a == FALSE);
    CHECKF(e != ERROR_CLIPBOARD_NOT_OPEN, "query must not demand an open clipboard");
}

TEST(gap8_clipfmt_standard_ids_do_not_crash_note) {
    NEED_USER(); REQUIRE(U(IsClipboardFormatAvailable));
    for (UINT f = 1; f <= 20; f++) { BOOL a = U(IsClipboardFormatAvailable)(f); t_note("CF %u -> %d", f, a); }
    UINT odd[] = { 0x7fffffff, 0x80000000, 0xffffffff, 0x200, 0x2ff, 0x300, 0x3ff, 0x4000, 0xbfff };
    for (unsigned i = 0; i < sizeof odd / sizeof odd[0]; i++) { BOOL a = U(IsClipboardFormatAvailable)(odd[i]); (void)a; }
}

/* ================================================================= NtUserMsgWaitForMultipleObjectsEx = */

UDECL(DWORD, NtUserMsgWaitForMultipleObjectsEx, (DWORD, const HANDLE *, DWORD, DWORD, DWORD));
#define MWFMO_USER(n, h, ms, mask, fl) U(MsgWaitForMultipleObjectsEx)((n), (h), (ms), (mask), (fl))

static DWORD mw(int viaNt, DWORD n, const HANDLE *h, DWORD ms, DWORD mask, DWORD fl) {
    return viaNt ? UN(NtUserMsgWaitForMultipleObjectsEx)(n, h, ms, mask, fl) : MWFMO_USER(n, h, ms, mask, fl);
}
static int mw_ready(void) { return U(MsgWaitForMultipleObjectsEx) && UN(NtUserMsgWaitForMultipleObjectsEx) && U(PostMessageW) && U(PeekMessageW); }
#define NEED_MW() do { NEED_USER(); if (!mw_ready()) { t_skip("MsgWait exports missing"); return; } } while (0)

TEST(gap8_msgwait_rejects_unknown_flag_bits) {
    NEED_MW();
    static const DWORD bad[] = { 0x20, 0x40, 0x80, 0x100, 0x1000, 0x10000, 0x80000000u, 0xffffffe0u, 0xffffffffu, 0x21 };
    HANDLE h[1] = { NULL };
    for (int v = 0; v < 2; v++)
        for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
            SetLastError(0);
            DWORD r = mw(v, 0, h, 0, QS_POSTMESSAGE, bad[i]);
            CHECKF(r == WAIT_FAILED, "route %d flags 0x%lx -> 0x%lx", v, bad[i], r);
            CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "route %d flags 0x%lx err %lu", v, bad[i], GetLastError());
        }
}

TEST(gap8_msgwait_rejects_count_above_63) {
    NEED_MW();
    HANDLE h[66]; for (int i = 0; i < 66; i++) h[i] = NULL;
    static const DWORD bad[] = { 64, 65, 100, 0xff, 0x10000, 0x7fffffff, 0xffffffffu };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(0);
        DWORD r = mw(1, bad[i], h, 0, QS_POSTMESSAGE, 0);        /* handles are never read: the count is tested first */
        CHECKF(r == WAIT_FAILED, "nCount %lu -> 0x%lx", bad[i], r);
        CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "nCount %lu err %lu", bad[i], GetLastError());
    }
}

TEST(gap8_msgwait_count_63_is_accepted) {
    NEED_MW();
    HANDLE ev[63]; int n = 0;
    for (; n < 63; n++) { ev[n] = CreateEventW(NULL, TRUE, FALSE, NULL); if (!ev[n]) break; }
    CHECK(n == 63);
    if (n == 63) {
        drain();
        SetLastError(0);
        DWORD r = mw(1, 63, ev, 0, QS_POSTMESSAGE, 0);
        CHECKF(r == WAIT_TIMEOUT, "63 unsignalled handles -> 0x%lx err %lu", r, GetLastError());
        SetEvent(ev[62]);
        r = mw(1, 63, ev, 0, QS_POSTMESSAGE, 0);
        CHECKF(r == WAIT_OBJECT_0 + 62, "last of 63 signalled -> 0x%lx", r);
    }
    for (int i = 0; i < n; i++) CloseHandle(ev[i]);
}

TEST(gap8_msgwait_valid_flag_bits_not_rejected) {
    NEED_MW();
    HANDLE h[1] = { NULL };
    drain();
    for (int v = 0; v < 2; v++)
        for (DWORD fl = 0; fl < 0x20; fl++) {
            if ((fl & 1) && 0) continue;
            SetLastError(0);
            DWORD r = mw(v, 0, h, 0, QS_POSTMESSAGE, fl);
            CHECKF(!(r == WAIT_FAILED && GetLastError() == ERROR_INVALID_PARAMETER), "route %d flags 0x%lx rejected", v, fl);
            drain();
        }
}

TEST(gap8_msgwait_timeout_with_empty_queue) {
    NEED_MW();
    drain();
    for (int v = 0; v < 2; v++) {
        DWORD r = mw(v, 0, NULL, 0, QS_POSTMESSAGE, 0);
        CHECKF(r == WAIT_TIMEOUT, "route %d no handles -> 0x%lx", v, r);
        HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
        r = mw(v, 1, &ev, 0, QS_POSTMESSAGE, 0);
        CHECKF(r == WAIT_TIMEOUT, "route %d unsignalled event -> 0x%lx", v, r);
        DWORD t0 = GetTickCount();
        r = mw(v, 1, &ev, 120, QS_POSTMESSAGE, 0);
        DWORD dt = GetTickCount() - t0;
        CHECKF(r == WAIT_TIMEOUT, "route %d timed wait -> 0x%lx", v, r);
        CHECKF(dt >= 80 && dt < 3000, "route %d elapsed %lu ms", v, dt);
        CloseHandle(ev);
    }
}

TEST(gap8_msgwait_signalled_handle_index) {
    NEED_MW();
    drain();
    for (int v = 0; v < 2; v++) {
        HANDLE ev[3]; for (int i = 0; i < 3; i++) ev[i] = CreateEventW(NULL, TRUE, FALSE, NULL);
        for (int k = 0; k < 3; k++) {
            ResetEvent(ev[0]); ResetEvent(ev[1]); ResetEvent(ev[2]);
            SetEvent(ev[k]);
            DWORD r = mw(v, 3, ev, 0, QS_POSTMESSAGE, 0);
            CHECKF(r == WAIT_OBJECT_0 + k, "route %d event %d -> 0x%lx", v, k, r);
        }
        for (int i = 0; i < 3; i++) CloseHandle(ev[i]);
    }
}

TEST(gap8_msgwait_auto_reset_event_consumed) {
    NEED_MW();
    drain();
    for (int v = 0; v < 2; v++) {
        HANDLE ev = CreateEventW(NULL, FALSE, TRUE, NULL);
        CHECKF(mw(v, 1, &ev, 0, QS_POSTMESSAGE, 0) == WAIT_OBJECT_0, "route %d first wait", v);
        CHECKF(mw(v, 1, &ev, 0, QS_POSTMESSAGE, 0) == WAIT_TIMEOUT, "route %d auto-reset event satisfied twice", v);
        HANDLE man = CreateEventW(NULL, TRUE, TRUE, NULL);
        CHECKF(mw(v, 1, &man, 0, QS_POSTMESSAGE, 0) == WAIT_OBJECT_0, "route %d manual first", v);
        CHECKF(mw(v, 1, &man, 0, QS_POSTMESSAGE, 0) == WAIT_OBJECT_0, "route %d manual second", v);
        CloseHandle(ev); CloseHandle(man);
    }
}

TEST(gap8_msgwait_posted_message_wakes_with_count_index) {
    NEED_MW();
    HWND w = mkpopup(); WCHK(w);
    drain();
    for (int v = 0; v < 2; v++)
        for (DWORD n = 0; n <= 2; n++) {
            HANDLE ev[2]; ev[0] = CreateEventW(NULL, TRUE, FALSE, NULL); ev[1] = CreateEventW(NULL, TRUE, FALSE, NULL);
            drain();
            CHECK(U(PostMessageW)(w, WM_NULL, 0, 0));
            DWORD r = mw(v, n, ev, 1000, QS_POSTMESSAGE, 0);
            CHECKF(r == WAIT_OBJECT_0 + n, "route %d nCount %lu -> 0x%lx", v, n, r);   /* index == nCount means "input available" */
            drain();
            CloseHandle(ev[0]); CloseHandle(ev[1]);
        }
    U(DestroyWindow)(w);
}

TEST(gap8_msgwait_wake_mask_selects_queue_bits) {
    NEED_MW();
    HWND w = mkpopup(); WCHK(w);
    drain();
    CHECK(U(PostMessageW)(w, WM_NULL, 0, 0));
    /* a posted message sets QS_POSTMESSAGE only; waiting on other bits stays asleep */
    CHECK(mw(1, 0, NULL, 0, QS_SENDMESSAGE, 0) == WAIT_TIMEOUT);
    CHECK(mw(1, 0, NULL, 0, QS_TIMER, 0) == WAIT_TIMEOUT);
    CHECK(mw(1, 0, NULL, 0, QS_HOTKEY, 0) == WAIT_TIMEOUT);
    CHECK(mw(1, 0, NULL, 0, QS_POSTMESSAGE, 0) == 0);
    t_note("QS_ALLPOSTMESSAGE wait with a pending post -> 0x%lx", mw(1, 0, NULL, 0, QS_ALLPOSTMESSAGE, 0));
    drain();
    U(DestroyWindow)(w);
}

TEST(gap8_msgwait_inputavailable_sees_already_peeked_message) {
    NEED_MW();
    HWND w = mkpopup(); WCHK(w);
    drain();
    for (int v = 0; v < 2; v++) {
        CHECK(U(PostMessageW)(w, WM_NULL, 0, 0));
        MSG m;
        CHECK(U(PeekMessageW)(&m, NULL, 0, 0, PM_NOREMOVE));        /* marks the queue contents as seen */
        DWORD r = mw(v, 0, NULL, 0, QS_POSTMESSAGE, 0);
        CHECKF(r == WAIT_TIMEOUT, "route %d without MWMO_INPUTAVAILABLE -> 0x%lx (already-seen message must not wake)", v, r);
        r = mw(v, 0, NULL, 0, QS_POSTMESSAGE, MWMO_INPUTAVAILABLE);
        CHECKF(r == 0, "route %d with MWMO_INPUTAVAILABLE -> 0x%lx", v, r);
        drain();
        r = mw(v, 0, NULL, 0, QS_POSTMESSAGE, MWMO_INPUTAVAILABLE);
        CHECKF(r == WAIT_TIMEOUT, "route %d drained queue with MWMO_INPUTAVAILABLE -> 0x%lx", v, r);
    }
    U(DestroyWindow)(w);
}

TEST(gap8_msgwait_waitall_needs_every_handle) {
    NEED_MW();
    HWND w = mkpopup(); WCHK(w);
    drain();
    HANDLE ev[2]; ev[0] = CreateEventW(NULL, TRUE, TRUE, NULL); ev[1] = CreateEventW(NULL, TRUE, FALSE, NULL);
    /* WAITALL with an empty input queue: one of two handles signalled is not enough. */
    DWORD r = mw(1, 2, ev, 0, QS_POSTMESSAGE, MWMO_WAITALL);
    CHECKF(r == WAIT_TIMEOUT, "one of two signalled, WAITALL -> 0x%lx", r);
    r = mw(0, 2, ev, 0, QS_POSTMESSAGE, MWMO_WAITALL);
    CHECKF(r == WAIT_TIMEOUT, "user32 route: one of two signalled, WAITALL -> 0x%lx", r);
    /* With a posted message pending the result depends on how the input event is folded into the wait-all; recorded only. */
    CHECK(U(PostMessageW)(w, WM_NULL, 0, 0));
    r = mw(1, 2, ev, 0, QS_POSTMESSAGE, MWMO_WAITALL);
    t_note("WAITALL, one of two signalled + posted message -> 0x%lx", r);
    SetEvent(ev[1]);
    r = mw(1, 2, ev, 0, QS_POSTMESSAGE, MWMO_WAITALL);
    t_note("WAITALL, both signalled + posted message -> 0x%lx", r);
    drain();
    CloseHandle(ev[0]); CloseHandle(ev[1]);
    U(DestroyWindow)(w);
}

TEST(gap8_msgwait_invalid_handle) {
    NEED_MW();
    drain();
    HANDLE bad[] = { (HANDLE)(ULONG_PTR)0x7770, (HANDLE)(ULONG_PTR)0xdeadbeef0, INVALID_HANDLE_VALUE };
    for (unsigned i = 0; i < 3; i++) {
        SetLastError(0);
        DWORD r = mw(1, 1, &bad[i], 0, QS_POSTMESSAGE, 0);
        t_note("bad handle %p -> 0x%lx err %lu", bad[i], r, GetLastError());
        CHECKF(r == WAIT_FAILED || r == WAIT_TIMEOUT, "bad handle %u -> 0x%lx", i, r);
    }
    HANDLE c = CreateEventW(NULL, TRUE, TRUE, NULL);
    CloseHandle(c);
    SetLastError(0);
    DWORD r = mw(1, 1, &c, 0, QS_POSTMESSAGE, 0);
    CHECKF(r == WAIT_FAILED, "closed handle -> 0x%lx", r);
    t_note("closed handle err %lu", GetLastError());
}

struct wk { DWORD tid; HANDLE ev; int mode; };
static DWORD WINAPI waker(void *p) {
    struct wk *w = p;
    Sleep(80);
    if (w->mode == 0) PostThreadMessageW(w->tid, WM_USER + 5, 0, 0);
    else SetEvent(w->ev);
    return 0;
}

TEST(gap8_msgwait_blocking_wake_from_other_thread) {
    NEED_MW();
    drain();
    for (int mode = 0; mode < 2; mode++)
        for (int v = 0; v < 2; v++) {
            struct wk w = { GetCurrentThreadId(), CreateEventW(NULL, TRUE, FALSE, NULL), mode };
            drain();
            HANDLE t = CreateThread(NULL, 0, waker, &w, 0, NULL);
            DWORD t0 = GetTickCount();
            DWORD r = mw(v, 1, &w.ev, 3000, QS_POSTMESSAGE, 0);
            DWORD dt = GetTickCount() - t0;
            CHECKF(r == (mode == 0 ? WAIT_OBJECT_0 + 1 : WAIT_OBJECT_0), "mode %d route %d -> 0x%lx", mode, v, r);
            CHECKF(dt < 2900, "mode %d route %d took %lu ms", mode, v, dt);
            WaitForSingleObject(t, 2000); CloseHandle(t); CloseHandle(w.ev);
            drain();
        }
}

static volatile LONG g_apc_ran;
static VOID CALLBACK apc_fn(ULONG_PTR p) { InterlockedIncrement(&g_apc_ran); }

TEST(gap8_msgwait_alertable_runs_apc) {
    NEED_MW();
    drain();
    for (int v = 0; v < 2; v++) {
        g_apc_ran = 0;
        CHECK(QueueUserAPC(apc_fn, GetCurrentThread(), 0));
        DWORD r = mw(v, 0, NULL, 2000, QS_POSTMESSAGE, MWMO_ALERTABLE);
        CHECKF(r == WAIT_IO_COMPLETION, "route %d -> 0x%lx", v, r);
        CHECKF(g_apc_ran == 1, "route %d APC ran %ld times", v, g_apc_ran);
    }
}

TEST(gap8_msgwait_non_alertable_leaves_apc_queued) {
    NEED_MW();
    drain();
    g_apc_ran = 0;
    CHECK(QueueUserAPC(apc_fn, GetCurrentThread(), 0));
    DWORD r = mw(1, 0, NULL, 50, QS_POSTMESSAGE, 0);
    CHECKF(r == WAIT_TIMEOUT, "-> 0x%lx", r);
    CHECKF(g_apc_ran == 0, "APC ran in a non-alertable wait");
    SleepEx(0, TRUE);                       /* now deliver it so no queued APC leaks into later tests */
    CHECK(g_apc_ran == 1);
}
