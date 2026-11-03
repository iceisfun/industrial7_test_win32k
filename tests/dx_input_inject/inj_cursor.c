/* dx_input_inject: cursor position, clipping and absolute/relative mouse injection.
 *
 * Kernel facts:
 *   NtUserSetCursorPos(x, y): requires the thread's desktop to be NULL or the input desktop and the
 *     foreground to be UIPI-accessible; it moves the cursor, clamping to the desktop/clip rectangle,
 *     returns TRUE. A thread on a non-input desktop gets FALSE (no last error is set on that branch).
 *   user32!GetCursorPos / GetPhysicalCursorPos = NtUserCallTwoParam(lpPoint, 1, 0x7f).
 *   NtUserClipCursor(RECT*): NULL clears the clip (CCursorClip::ClearClip), else the rect is DPI-converted and stored;
 *     NtUserGetClipCursor(RECT*) returns the stored clip (the full virtual screen when none).
 *   mouse_event: MOUSEEVENTF_ABSOLUTE (0x8000) normalized 0..65535 -> pixels; VIRTUALDESK (0x4000) -> virtual desktop.
 */
#include "inj.h"

UDECL(ULONG_PTR, NtUserCallTwoParam, (ULONG_PTR, ULONG_PTR, DWORD));
UDECL(BOOL, NtUserSetCursorPos, (int, int));
UDECL(BOOL, NtUserClipCursor, (const RECT *));
UDECL(BOOL, NtUserGetClipCursor, (RECT *));
UDECL(BOOL, NtUserGetCursorInfo, (CURSORINFO *));

static int rect_eq(const RECT *a, const RECT *b) { return a->left == b->left && a->top == b->top && a->right == b->right && a->bottom == b->bottom; }

/* SetCursorPos / GetCursorPos round trip inside the primary monitor. */
TEST(dx_input_inject_cursor_roundtrip_table) {
    INJ_ENV(); inj_arm(); REQUIRE(U(SetCursorPos)); REQUIRE(U(GetCursorPos));
    RECT p; inj_screen(&p, NULL);
    int W = p.right, H = p.bottom;
    const POINT pts[] = { {0, 0}, {1, 1}, {W - 1, H - 1}, {W / 2, H / 2}, {10, H - 10}, {W - 10, 10}, {200, 200}, {W / 3, H / 3} };
    for (size_t i = 0; i < sizeof pts / sizeof *pts; i++) {
        POINT g = { -1, -1 };
        BOOL r = U(SetCursorPos)(pts[i].x, pts[i].y);
        CHECKF(r, "set %ld,%ld", pts[i].x, pts[i].y);
        CHECK(U(GetCursorPos)(&g));
        CHECKF(inj_near(g.x, pts[i].x, 1) && inj_near(g.y, pts[i].y, 1), "set %ld,%ld got %ld,%ld", pts[i].x, pts[i].y, g.x, g.y);
        if (g.x != pts[i].x || g.y != pts[i].y) t_note("cursor %ld,%ld read back as %ld,%ld (DPI virtualization rounding?)", pts[i].x, pts[i].y, g.x, g.y);
    }
    inj_restore();
}

/* The raw syscall behaves like the export, GetCursorPos is the CallTwoParam(…, 1, 0x7f) thunk. */
TEST(dx_input_inject_cursor_direct_syscalls) {
    INJ_ENV(); inj_arm(); REQUIRE(UN(NtUserSetCursorPos)); REQUIRE(UN(NtUserCallTwoParam)); REQUIRE(U(GetCursorPos));
    RECT p; inj_screen(&p, NULL);
    EXPECT_EQ(UN(NtUserSetCursorPos)(p.right / 4, p.bottom / 4), 1);
    POINT a = { -1, -1 }, b = { -2, -2 };
    EXPECT_EQ(UN(NtUserCallTwoParam)((ULONG_PTR)&a, 1, 0x7f), 1);
    CHECK(U(GetCursorPos)(&b));
    CHECK(inj_near(a.x, p.right / 4, 1) && inj_near(a.y, p.bottom / 4, 1));
    CHECK(inj_near(a.x, b.x, 2) && inj_near(a.y, b.y, 2));
    inj_restore();
}

/* The cursor never leaves the virtual desktop: out-of-range targets are clamped by the kernel. */
TEST(dx_input_inject_cursor_clamped_to_desktop) {
    INJ_ENV(); inj_arm(); REQUIRE(U(SetCursorPos)); REQUIRE(U(GetCursorPos));
    RECT v; inj_screen(NULL, &v);
    const POINT pts[] = { {-5000, -5000}, {100000, 100000}, {-1, 50}, {50, -1}, {v.right + 500, 0}, {0, v.bottom + 500}, {-100000, 100000} };
    for (size_t i = 0; i < sizeof pts / sizeof *pts; i++) {
        POINT g = { -9999, -9999 };
        BOOL r = U(SetCursorPos)(pts[i].x, pts[i].y);
        U(GetCursorPos)(&g);
        CHECKF(r, "set %ld,%ld", pts[i].x, pts[i].y);
        CHECKF(g.x >= v.left && g.x < v.right && g.y >= v.top && g.y < v.bottom, "set %ld,%ld -> %ld,%ld outside [%ld,%ld)x[%ld,%ld)",
               pts[i].x, pts[i].y, g.x, g.y, v.left, v.right, v.top, v.bottom);
    }
    inj_restore();
}

/* GetCursorInfo.ptScreenPos is the same position the thunk reports. */
TEST(dx_input_inject_cursor_info_matches) {
    INJ_ENV(); inj_arm(); REQUIRE(U(SetCursorPos)); REQUIRE(U(GetCursorInfo));
    RECT p; inj_screen(&p, NULL);
    U(SetCursorPos)(p.right / 2, p.bottom / 2);
    CURSORINFO ci; memset(&ci, 0, sizeof ci); ci.cbSize = sizeof ci;
    CHECK(U(GetCursorInfo)(&ci));
    CHECK(inj_near(ci.ptScreenPos.x, p.right / 2, 1) && inj_near(ci.ptScreenPos.y, p.bottom / 2, 1));
    CHECK((ci.flags & ~(CURSOR_SHOWING | 2u)) == 0);   /* CURSOR_SHOWING (1), CURSOR_SUPPRESSED (2) only */
    if (!(ci.flags & CURSOR_SHOWING)) t_note("cursor not showing (flags %lu)", ci.flags);
    ci.cbSize = sizeof ci - 1;
    SET_ERR(0); BOOL gr = U(GetCursorInfo)(&ci);
    DWORD e = GetLastError(); CHECKF(e == ERROR_INVALID_PARAMETER, "err %lu", e);
    t_note("GetCursorInfo with a wrong cbSize returned %d", gr);
    inj_restore();
}

/* ClipCursor: stored rect round trips through GetClipCursor and bounds SetCursorPos. */
TEST(dx_input_inject_clip_roundtrip) {
    INJ_ENV(); inj_arm(); REQUIRE(U(ClipCursor)); REQUIRE(U(GetClipCursor)); REQUIRE(U(SetCursorPos));
    RECT p, v; inj_screen(&p, &v);
    RECT rc = { p.right / 4, p.bottom / 4, p.right / 2, p.bottom / 2 };
    CHECK(U(ClipCursor)(&rc));
    RECT got; memset(&got, 0xCC, sizeof got);
    CHECK(U(GetClipCursor)(&got));
    CHECKF(rect_eq(&got, &rc), "got %ld,%ld,%ld,%ld want %ld,%ld,%ld,%ld", got.left, got.top, got.right, got.bottom, rc.left, rc.top, rc.right, rc.bottom);
    POINT g;
    U(SetCursorPos)(0, 0); U(GetCursorPos)(&g);
    CHECK(g.x >= rc.left && g.x < rc.right && g.y >= rc.top && g.y < rc.bottom);
    U(SetCursorPos)(p.right - 1, p.bottom - 1); U(GetCursorPos)(&g);
    CHECK(g.x >= rc.left && g.x < rc.right && g.y >= rc.top && g.y < rc.bottom);
    U(SetCursorPos)((rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2); U(GetCursorPos)(&g);
    CHECK(inj_near(g.x, (rc.left + rc.right) / 2, 1) && inj_near(g.y, (rc.top + rc.bottom) / 2, 1));
    CHECK(U(ClipCursor)(NULL));
    memset(&got, 0xCC, sizeof got);
    CHECK(U(GetClipCursor)(&got));
    CHECKF(got.left <= v.left && got.top <= v.top && got.right >= v.right && got.bottom >= v.bottom, "cleared clip %ld,%ld,%ld,%ld", got.left, got.top, got.right, got.bottom);
    inj_restore();
}

/* Same through the win32u stubs (NtUserClipCursor(RECT*) / NtUserGetClipCursor(RECT*)). */
TEST(dx_input_inject_clip_direct_syscalls) {
    if (t_is_wine()) { t_skip("Wine aborts on its unimplemented win32u.NtUserGetClipCursor"); return; }
    INJ_ENV(); inj_arm(); REQUIRE(UN(NtUserClipCursor)); REQUIRE(UN(NtUserGetClipCursor));
    RECT p; inj_screen(&p, NULL);
    RECT rc = { 20, 30, 220, 130 }, got;
    EXPECT_EQ(UN(NtUserClipCursor)(&rc), 1);
    memset(&got, 0, sizeof got);
    EXPECT_EQ(UN(NtUserGetClipCursor)(&got), 1);
    CHECK(rect_eq(&got, &rc));
    EXPECT_EQ(UN(NtUserClipCursor)(NULL), 1);
    EXPECT_EQ(UN(NtUserGetClipCursor)(&got), 1);
    CHECK(!rect_eq(&got, &rc));
    inj_restore();
}

/* Relative injected motion is bounded by the clip rect too. */
TEST(dx_input_inject_clip_bounds_relative_motion) {
    INJ_ENV(); inj_arm(); REQUIRE(U(ClipCursor)); REQUIRE(U(SetCursorPos));
    RECT p; inj_screen(&p, NULL);
    RECT rc = { 100, 100, 300, 260 };
    CHECK(U(ClipCursor)(&rc));
    U(SetCursorPos)(200, 180);
    static const int d[][2] = { {5000, 0}, {-5000, 0}, {0, 5000}, {0, -5000}, {4000, 4000}, {-4000, -4000} };
    for (size_t i = 0; i < sizeof d / sizeof *d; i++) {
        inj_mouse1(d[i][0], d[i][1], 0, MOUSEEVENTF_MOVE, 0);
        POINT g; U(GetCursorPos)(&g);
        CHECKF(g.x >= rc.left && g.x < rc.right && g.y >= rc.top && g.y < rc.bottom, "d=%d,%d -> %ld,%ld", d[i][0], d[i][1], g.x, g.y);
    }
    U(ClipCursor)(NULL);
    inj_restore();
}

/* MOUSEEVENTF_ABSOLUTE maps 0..65535 onto the primary monitor. */
TEST(dx_input_inject_abs_primary_mapping) {
    INJ_ENV(); inj_arm(); REQUIRE(U(GetCursorPos));
    RECT p; inj_screen(&p, NULL);
    const POINT px[] = { {0, 0}, {p.right - 1, p.bottom - 1}, {p.right / 2, p.bottom / 2}, {17, 33}, {p.right / 4, p.bottom * 3 / 4} };
    for (size_t i = 0; i < sizeof px / sizeof *px; i++) {
        LONG ax = inj_abs(px[i].x, p.right), ay = inj_abs(px[i].y, p.bottom);
        UINT r = inj_mouse1(ax, ay, 0, MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE, 0);
        CHECK(r == 1);
        POINT g; U(GetCursorPos)(&g);
        CHECKF(inj_near(g.x, px[i].x, 1) && inj_near(g.y, px[i].y, 1), "want %ld,%ld got %ld,%ld (abs %ld,%ld)", px[i].x, px[i].y, g.x, g.y, ax, ay);
    }
    /* extremes: 0 -> first pixel, 65535 -> last pixel */
    inj_mouse1(0, 0, 0, MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE, 0);
    POINT g; U(GetCursorPos)(&g); CHECK(g.x == 0 && g.y == 0);
    inj_mouse1(65535, 65535, 0, MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE, 0);
    U(GetCursorPos)(&g);
    CHECKF(inj_near(g.x, p.right - 1, 1) && inj_near(g.y, p.bottom - 1, 1), "got %ld,%ld", g.x, g.y);
    inj_restore();
}

/* ABSOLUTE|VIRTUALDESK maps onto the whole virtual desktop. */
TEST(dx_input_inject_abs_virtualdesk_mapping) {
    INJ_ENV(); inj_arm(); REQUIRE(U(GetCursorPos));
    RECT v; inj_screen(NULL, &v);
    int VW = v.right - v.left, VH = v.bottom - v.top;
    const POINT px[] = { {0, 0}, {VW - 1, VH - 1}, {VW / 2, VH / 2}, {VW / 3, VH / 5} };
    for (size_t i = 0; i < sizeof px / sizeof *px; i++) {
        LONG ax = inj_abs(px[i].x, VW), ay = inj_abs(px[i].y, VH);
        UINT r = inj_mouse1(ax, ay, 0, MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK, 0);
        CHECK(r == 1);
        POINT g; U(GetCursorPos)(&g);
        CHECKF(inj_near(g.x, v.left + px[i].x, 2) && inj_near(g.y, v.top + px[i].y, 2), "want %ld,%ld got %ld,%ld", v.left + px[i].x, v.top + px[i].y, g.x, g.y);
    }
    inj_restore();
}

/* ABSOLUTE without MOVE moves nothing: the position fields are only applied when MOUSEEVENTF_MOVE (bit 0) is set. */
TEST(dx_input_inject_abs_without_move_is_ignored) {
    INJ_ENV(); inj_arm(); REQUIRE(U(GetCursorPos)); REQUIRE(U(SetCursorPos));
    RECT p; inj_screen(&p, NULL);
    U(SetCursorPos)(p.right / 2, p.bottom / 2);
    POINT a, b; U(GetCursorPos)(&a);
    inj_mouse1(0, 0, 0, MOUSEEVENTF_ABSOLUTE, 0);
    U(GetCursorPos)(&b);
    CHECKF(inj_near(a.x, b.x, 2) && inj_near(a.y, b.y, 2), "%ld,%ld -> %ld,%ld", a.x, a.y, b.x, b.y);
    inj_restore();
}

/* Relative moves: zero is a no-op, positive deltas move right/down, negative left/up (magnitude depends on pointer speed). */
TEST(dx_input_inject_relative_motion_direction) {
    INJ_ENV(); inj_arm(); REQUIRE(U(GetCursorPos)); REQUIRE(U(SetCursorPos));
    RECT p; inj_screen(&p, NULL);
    U(SetCursorPos)(p.right / 2, p.bottom / 2);
    POINT a, b;
    U(GetCursorPos)(&a);
    inj_mouse1(0, 0, 0, MOUSEEVENTF_MOVE, 0);
    U(GetCursorPos)(&b);
    CHECK(inj_near(a.x, b.x, 1) && inj_near(a.y, b.y, 1));
    inj_mouse1(40, 40, 0, MOUSEEVENTF_MOVE, 0);
    U(GetCursorPos)(&b);
    CHECKF(b.x > a.x && b.y > a.y, "%ld,%ld -> %ld,%ld", a.x, a.y, b.x, b.y);
    t_note("40 counts moved the cursor %ld,%ld pixels (pointer speed/acceleration dependent)", b.x - a.x, b.y - a.y);
    a = b;
    inj_mouse1(-40, -40, 0, MOUSEEVENTF_MOVE, 0);
    U(GetCursorPos)(&b);
    CHECKF(b.x < a.x && b.y < a.y, "%ld,%ld -> %ld,%ld", a.x, a.y, b.x, b.y);
    inj_restore();
}

/* mouse_event is NtUserSendInput with a mouse INPUT: absolute move through it lands on the same pixel. */
TEST(dx_input_inject_mouse_event_absolute) {
    INJ_ENV(); inj_arm(); REQUIRE(U(mouse_event)); REQUIRE(U(GetCursorPos));
    RECT p; inj_screen(&p, NULL);
    U(mouse_event)(MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE, inj_abs(p.right / 2, p.right), inj_abs(p.bottom / 2, p.bottom), 0, 0);
    POINT g; U(GetCursorPos)(&g);
    CHECKF(inj_near(g.x, p.right / 2, 1) && inj_near(g.y, p.bottom / 2, 1), "got %ld,%ld", g.x, g.y);
    inj_restore();
}

/* Position written by SetCursorPos is what an LL mouse hook reports for the next injected event. */
TEST(dx_input_inject_cursor_pos_seen_by_ll_hook) {
    INJ_ENV(); inj_arm(); REQUIRE(U(SetCursorPos));
    if (!inj_hook_begin(0, 1)) { t_skip("cannot install LL mouse hook"); return; }
    RECT p; inj_screen(&p, NULL);
    ULONG_PTR t = inj_tag();
    int tx = p.right / 3, ty = p.bottom / 3;
    inj_mouse1(inj_abs(tx, p.right), inj_abs(ty, p.bottom), 0, MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE, t);
    inj_pump(60);
    int i = inj_ev_find(1, t, 0);
    CHECK(i >= 0);
    if (i >= 0) {
        CHECK(inj_ev(i)->wp == WM_MOUSEMOVE);
        CHECK(inj_ev(i)->flags & LLMHF_INJECTED);
        CHECKF(inj_near(inj_ev(i)->x, tx, 1) && inj_near(inj_ev(i)->y, ty, 1), "hook pt %ld,%ld want %d,%d", inj_ev(i)->x, inj_ev(i)->y, tx, ty);
    }
    inj_hook_end(); inj_restore();
}

/* A thread on a different (non-input) desktop: SetCursorPos fails (the thread is not on the input desktop), SendInput and
 * BlockInput fail with ERROR_ACCESS_DENIED (RtlNtStatusToDosError(STATUS_ACCESS_DENIED) == 5). */
struct dsk { int res; DWORD sierr_k, sierr_m, bierr; UINT sir_k, sir_m; BOOL cur, blk; };
static DWORD th_desk(void *a) {
    struct dsk *d = a;
    WCHAR nm[64]; wsprintfW(nm, L"win32kt_inj_%lu", GetCurrentProcessId());
    HDESK hd = U(CreateDesktopW)(nm, NULL, NULL, 0, GENERIC_ALL, NULL);
    if (!hd) { d->res = -1; return 0; }
    if (!U(SetThreadDesktop)(hd)) { d->res = -2; U(CloseDesktop)(hd); return 0; }
    INPUT in;
    inj_key(&in, VK_F24, 0, 0, 0);
    SET_ERR(0); d->sir_k = inj_send(1, &in); d->sierr_k = GetLastError();
    inj_key(&in, VK_F24, 0, KEYEVENTF_KEYUP, 0); inj_send(1, &in);
    inj_mouse(&in, 0, 0, 0, MOUSEEVENTF_MOVE, 0);
    SET_ERR(0); d->sir_m = inj_send(1, &in); d->sierr_m = GetLastError();
    SET_ERR(0); d->cur = U(SetCursorPos)(5, 5);
    SET_ERR(0); d->blk = U(BlockInput)(TRUE); d->bierr = GetLastError();
    if (d->blk) U(BlockInput)(FALSE);
    U(CloseDesktop)(hd);
    d->res = 1;
    return 0;
}
TEST(dx_input_inject_other_desktop_denied) {
    INJ_ENV(); inj_arm(); REQUIRE(U(CreateDesktopW)); REQUIRE(U(SetThreadDesktop));
    if (t_is_wine()) { t_skip("desktop switching semantics differ under Wine"); return; }
    struct dsk d; memset(&d, 0, sizeof d);
    POINT a, b; U(GetCursorPos)(&a);
    k_run_gui_thread(th_desk, &d);
    if (d.res < 0) { t_skip("cannot create/assign a private desktop (%d)", d.res); return; }
    CHECKF(d.sierr_k == ERROR_ACCESS_DENIED, "key err %lu", d.sierr_k);
    CHECKF(d.sierr_m == ERROR_ACCESS_DENIED, "mouse err %lu", d.sierr_m);
    t_note("SendInput from a non-input desktop returned key=%u mouse=%u (old compat versions count a failed event)", d.sir_k, d.sir_m);
    CHECK(d.sir_k <= 1 && d.sir_m <= 1);
    CHECK(!d.cur);
    CHECK(!d.blk);
    if (!d.blk) CHECKF(d.bierr == ERROR_ACCESS_DENIED, "BlockInput err %lu", d.bierr);
    U(GetCursorPos)(&b);
    CHECK(inj_near(a.x, b.x, 3) && inj_near(a.y, b.y, 3));   /* the injection and move never reached the input desktop */
    inj_restore();
}
