/* Layered window attributes, UpdateLayeredWindow validation and window display affinity:
 * win32kfull!NtUserSetLayeredWindowAttributes / NtUserGetLayeredWindowAttributes / NtUserUpdateLayeredWindow
 * / NtUserSetWindowDisplayAffinity / NtUserGetWindowDisplayAffinity. */
#include "imhm.h"

#define SENT 0x7e57u
#define EX_LAYERED 0x00080000

#define NEED_LAY() do { if (!UN(NtUserSetLayeredWindowAttributes) || !UN(NtUserGetLayeredWindowAttributes) || !U(GetWindowLongW)) { t_skip("missing stub"); return; } } while (0)
static HWND mk_layered(void) { return im_mkwnd(WS_POPUP, EX_LAYERED, NULL); }
#define NEED_LW(w) NEED_LAY(); HWND w = mk_layered(); if (!(w)) { t_skip("cannot create a layered window (err %lu)", GetLastError()); return; }
static BOOL sla(HWND w, COLORREF k, BYTE a, DWORD f) { return UN(NtUserSetLayeredWindowAttributes)(w, k, a, f); }
static BOOL gla(HWND w, COLORREF *k, BYTE *a, DWORD *f) { return UN(NtUserGetLayeredWindowAttributes)(w, k, a, f); }

/* ---- Set: validation ----------------------------------------------------------------------- */
TEST(ime_hotkeys_misc_lay_set_invalid_flag_bits_layered) {
    NEED_LW(w);
    static const DWORD bad[] = { 4, 8, 5, 6, 7, 0x10, 0x80, 0x100, 0x10000, 0x80000000, 0xfffffffc, 0xffffffff };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SET_ERR(SENT);
        BOOL r = sla(w, 0, 255, bad[i]);
        CHECKF(r == 0, "flags 0x%lx", bad[i]);
        CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "flags 0x%lx err %lu", bad[i], GetLastError());
    }
    im_kill(w);
}
TEST(ime_hotkeys_misc_lay_set_invalid_flag_bits_not_layered) {
    NEED_LAY(); NEED_GUI(w);
    SET_ERR(SENT);
    EXPECT_FAIL(sla(w, 0, 255, 4) == 0, ERROR_INVALID_PARAMETER);
    im_kill(w);
}
TEST(ime_hotkeys_misc_lay_set_requires_layered_style) {
    NEED_LAY(); NEED_GUI(w);
    static const DWORD good[] = { 0, 1, 2, 3 };
    for (unsigned i = 0; i < 4; i++) {
        SET_ERR(SENT);
        BOOL r = sla(w, 0, 128, good[i]);
        CHECKF(r == 0, "flags %lu", good[i]);
        CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "flags %lu err %lu", good[i], GetLastError());  /* _SetLayeredWindowAttributes: WS_EX_LAYERED bit test */
    }
    im_kill(w);
}
TEST(ime_hotkeys_misc_lay_set_bogus_window) {
    NEED_LAY();
    SET_ERR(SENT);
    EXPECT_FAIL(sla(im_bogus_hwnd(), 0, 1, 2) == 0, ERROR_INVALID_WINDOW_HANDLE);
}
TEST(ime_hotkeys_misc_lay_set_null_window) {
    NEED_LAY();
    EXPECT_EQ(sla(NULL, 0, 1, 2), 0);
}
TEST(ime_hotkeys_misc_lay_set_destroyed_window) {
    NEED_LW(w);
    im_kill(w);
    EXPECT_EQ(sla(w, 0, 1, 2), 0);
}
TEST(ime_hotkeys_misc_lay_set_alpha_succeeds) {
    NEED_LW(w);
    SET_ERR(SENT);
    BOOL r = sla(w, 0, 100, LWA_ALPHA);
    if (!r) t_note("SetLayeredWindowAttributes(LWA_ALPHA) failed, err %lu", GetLastError());
    CHECK(r);
    im_kill(w);
}
TEST(ime_hotkeys_misc_lay_set_colorkey_succeeds) {
    NEED_LW(w);
    BOOL r = sla(w, RGB(255, 0, 255), 0, LWA_COLORKEY);
    if (!r) t_note("SetLayeredWindowAttributes(LWA_COLORKEY) failed, err %lu", GetLastError());
    CHECK(r);
    im_kill(w);
}
TEST(ime_hotkeys_misc_lay_set_flags_zero_succeeds_or_notes) {
    NEED_LW(w);
    BOOL r = sla(w, 0, 0, 0);
    t_note("flags 0 -> %d (err %lu)", r, r ? 0 : GetLastError());
    im_kill(w);
}

/* ---- Get -------------------------------------------------------------------------------------- */
TEST(ime_hotkeys_misc_lay_get_roundtrip_alpha) {
    NEED_LW(w);
    if (!sla(w, 0, 77, LWA_ALPHA)) { t_note("set failed err %lu", GetLastError()); im_kill(w); t_skip("set failed"); return; }
    COLORREF k = 0xdeadbeef; BYTE a = 0; DWORD f = 0xffff;
    SET_ERR(SENT);
    CHECK(gla(w, &k, &a, &f));
    EXPECT_EQ(a, 77);
    EXPECT_EQ(f, LWA_ALPHA);     /* the kernel returns flags & 3 */
    im_kill(w);
}
TEST(ime_hotkeys_misc_lay_get_roundtrip_colorkey) {
    NEED_LW(w);
    if (!sla(w, RGB(1, 2, 3), 0, LWA_COLORKEY)) { im_kill(w); t_skip("set failed"); return; }
    COLORREF k = 0; BYTE a = 9; DWORD f = 0;
    CHECK(gla(w, &k, &a, &f));
    EXPECT_EQ(k & 0xffffff, RGB(1, 2, 3));
    EXPECT_EQ(f, LWA_COLORKEY);
    im_kill(w);
}
TEST(ime_hotkeys_misc_lay_get_roundtrip_both) {
    NEED_LW(w);
    if (!sla(w, RGB(10, 20, 30), 200, LWA_ALPHA | LWA_COLORKEY)) { im_kill(w); t_skip("set failed"); return; }
    COLORREF k = 0; BYTE a = 0; DWORD f = 0;
    CHECK(gla(w, &k, &a, &f));
    EXPECT_EQ(k & 0xffffff, RGB(10, 20, 30));
    EXPECT_EQ(a, 200);
    EXPECT_EQ(f, 3);
    im_kill(w);
}
TEST(ime_hotkeys_misc_lay_get_alpha_values) {
    NEED_LW(w);
    static const BYTE vals[] = { 0, 1, 127, 128, 254, 255 };
    for (unsigned i = 0; i < sizeof vals; i++) {
        if (!sla(w, 0, vals[i], LWA_ALPHA)) { t_note("set %u failed", vals[i]); continue; }
        BYTE a = 0xcc; DWORD f = 0;
        CHECKF(gla(w, NULL, &a, &f), "alpha %u", vals[i]);
        CHECKF(a == vals[i], "alpha %u read %u", vals[i], a);
    }
    im_kill(w);
}
TEST(ime_hotkeys_misc_lay_get_null_out_pointers_allowed) {
    NEED_LW(w);
    if (!sla(w, 0, 50, LWA_ALPHA)) { im_kill(w); t_skip("set failed"); return; }
    CHECK(gla(w, NULL, NULL, NULL));     /* each store is guarded by `!= NULL` */
    BYTE a = 0; CHECK(gla(w, NULL, &a, NULL)); EXPECT_EQ(a, 50);
    DWORD f = 0; CHECK(gla(w, NULL, NULL, &f)); EXPECT_EQ(f, 2);
    COLORREF k = 0x55; CHECK(gla(w, &k, NULL, NULL));
    im_kill(w);
}
TEST(ime_hotkeys_misc_lay_get_not_layered) {
    NEED_LAY(); NEED_GUI(w);
    COLORREF k = 1; BYTE a = 2; DWORD f = 3;
    SET_ERR(SENT);
    EXPECT_FAIL(gla(w, &k, &a, &f) == 0, ERROR_INVALID_PARAMETER);
    EXPECT_EQ(k, 1); EXPECT_EQ(a, 2); EXPECT_EQ(f, 3);   /* outputs untouched */
    im_kill(w);
}
TEST(ime_hotkeys_misc_lay_get_layered_never_set) {
    NEED_LW(w);
    COLORREF k = 1; BYTE a = 2; DWORD f = 3;
    BOOL r = gla(w, &k, &a, &f);
    /* _GetLayeredWindowAttributes: GetRedirectionFlags() & 1 is clear until a Set call -> returns 0 without
     * setting a last error */
    t_note("Get on never-set layered window: %d, err %lu", r, GetLastError());
    if (!r) { EXPECT_EQ(k, 1); EXPECT_EQ(a, 2); EXPECT_EQ(f, 3); }
    im_kill(w);
}
TEST(ime_hotkeys_misc_lay_get_bogus_window) {
    NEED_LAY();
    COLORREF k; BYTE a; DWORD f;
    SET_ERR(SENT);
    EXPECT_FAIL(gla(im_bogus_hwnd(), &k, &a, &f) == 0, ERROR_INVALID_WINDOW_HANDLE);
    EXPECT_EQ(gla(NULL, &k, &a, &f), 0);
}
TEST(ime_hotkeys_misc_lay_get_after_style_removed) {
    NEED_LW(w);
    if (!sla(w, 0, 90, LWA_ALPHA)) { im_kill(w); t_skip("set failed"); return; }
    LONG ex = U(GetWindowLongW)(w, GWL_EXSTYLE);
    CHECK(ex & EX_LAYERED);
    U(SetWindowLongW)(w, GWL_EXSTYLE, ex & ~EX_LAYERED);
    BYTE a = 0;
    SET_ERR(SENT);
    EXPECT_FAIL(gla(w, NULL, &a, NULL) == 0, ERROR_INVALID_PARAMETER);
    U(SetWindowLongW)(w, GWL_EXSTYLE, ex);
    im_kill(w);
}
TEST(ime_hotkeys_misc_lay_style_added_later_allows_set) {
    NEED_LAY(); NEED_GUI(w);
    LONG ex = U(GetWindowLongW)(w, GWL_EXSTYLE);
    U(SetWindowLongW)(w, GWL_EXSTYLE, ex | EX_LAYERED);
    BOOL r = sla(w, 0, 10, LWA_ALPHA);
    if (!r) t_note("set after adding WS_EX_LAYERED failed, err %lu", GetLastError());
    CHECK(r);
    im_kill(w);
}
TEST(ime_hotkeys_misc_lay_style_visible_in_window_structure) {
    NEED_LW(w);
    void *p = k_wnd_ptr(w);
    if (!p) { t_skip("window structure not reachable"); im_kill(w); return; }
    unsigned long ex = 0;
    CHECK(k_try_read_u32((char *)p + IM_W_EXSTYLE, &ex));
    CHECKF(ex & EX_LAYERED, "ex style 0x%lx", ex);      /* the kernel and user32 share tagWND.ExStyle */
    EXPECT_EQ((unsigned long)U(GetWindowLongW)(w, GWL_EXSTYLE), ex);
    im_kill(w);
}
TEST(ime_hotkeys_misc_lay_plain_window_style_not_layered_in_structure) {
    NEED_LAY(); NEED_GUI(w);
    void *p = k_wnd_ptr(w);
    if (!p) { t_skip("window structure not reachable"); im_kill(w); return; }
    unsigned long ex = 0xffffffff;
    CHECK(k_try_read_u32((char *)p + IM_W_EXSTYLE, &ex));
    CHECK(!(ex & EX_LAYERED));
    im_kill(w);
}
TEST(ime_hotkeys_misc_lay_user32_path_identical) {
    NEED_LW(w);
    if (!U(SetLayeredWindowAttributes) || !U(GetLayeredWindowAttributes)) { im_kill(w); t_skip("no user32 exports"); return; }
    if (!U(SetLayeredWindowAttributes)(w, 0, 33, LWA_ALPHA)) { im_kill(w); t_skip("set failed"); return; }
    BYTE a = 0; DWORD f = 0;
    CHECK(gla(w, NULL, &a, &f));
    EXPECT_EQ(a, 33);
    BYTE a2 = 0; DWORD f2 = 0;
    CHECK(U(GetLayeredWindowAttributes)(w, NULL, &a2, &f2));
    EXPECT_EQ(a2, a);
    EXPECT_EQ(f2, f);
    im_kill(w);
}
TEST(ime_hotkeys_misc_lay_layered_child_window_note) {
    NEED_LW(w);
    HWND c = im_mkwnd(WS_CHILD, EX_LAYERED, w);
    t_note("WS_EX_LAYERED child creation: %s (err %lu)", c ? "ok" : "failed", c ? 0 : GetLastError());
    if (c) {
        BOOL r = sla(c, 0, 80, LWA_ALPHA);
        t_note("SetLayeredWindowAttributes on layered child: %d err %lu", r, r ? 0 : GetLastError());
        im_kill(c);
    }
    im_kill(w);
}
TEST(ime_hotkeys_misc_lay_two_windows_independent) {
    NEED_LW(w);
    HWND w2 = mk_layered();
    if (!w2) { im_kill(w); t_skip("second window"); return; }
    if (!sla(w, 0, 10, LWA_ALPHA) || !sla(w2, 0, 20, LWA_ALPHA)) { im_kill(w); im_kill(w2); t_skip("set failed"); return; }
    BYTE a = 0, b = 0;
    CHECK(gla(w, NULL, &a, NULL)); CHECK(gla(w2, NULL, &b, NULL));
    EXPECT_EQ(a, 10); EXPECT_EQ(b, 20);
    im_kill(w); im_kill(w2);
}
TEST(ime_hotkeys_misc_lay_set_twice_last_wins) {
    NEED_LW(w);
    if (!sla(w, 0, 10, LWA_ALPHA) || !sla(w, RGB(7, 8, 9), 99, LWA_ALPHA | LWA_COLORKEY)) { im_kill(w); t_skip("set failed"); return; }
    COLORREF k = 0; BYTE a = 0; DWORD f = 0;
    CHECK(gla(w, &k, &a, &f));
    EXPECT_EQ(a, 99); EXPECT_EQ(f, 3); EXPECT_EQ(k & 0xffffff, RGB(7, 8, 9));
    im_kill(w);
}
static DWORD WINAPI lay_foreign_set(void *p) { return (DWORD)UN(NtUserSetLayeredWindowAttributes)((HWND)p, 0, 40, LWA_ALPHA); }
TEST(ime_hotkeys_misc_lay_set_from_other_thread_same_process) {
    NEED_LW(w);
    DWORD r = im_on_thread(lay_foreign_set, w);   /* NtUserSetLayeredWindowAttributes has no thread-ownership test */
    if (r != 1) t_note("cross-thread set returned %lu (err %lu)", r, GetLastError());
    if (r == 1) { BYTE a = 0; CHECK(gla(w, NULL, &a, NULL)); EXPECT_EQ(a, 40); }
    im_kill(w);
}

/* ---- UpdateLayeredWindow validation (all before the layered-window requirement) ------------ */
#define NEED_ULW() do { if (!U(UpdateLayeredWindow)) { t_skip("no user32 UpdateLayeredWindow"); return; } } while (0)
TEST(ime_hotkeys_misc_ulw_bogus_window) {
    NEED_ULW();
    SET_ERR(SENT);
    EXPECT_FAIL(U(UpdateLayeredWindow)(im_bogus_hwnd(), NULL, NULL, NULL, NULL, NULL, 0, NULL, 0) == 0, ERROR_INVALID_WINDOW_HANDLE);
}
TEST(ime_hotkeys_misc_ulw_invalid_flags) {
    NEED_ULW(); NEED_GUI(w);
    /* NtUserUpdateLayeredWindow: (dwFlags & 0xffffffd0) != 0 -> ERROR_INVALID_PARAMETER */
    static const DWORD bad[] = { 0x10, 0x40, 0x80, 0x100, 0x10000, 0x80000000, 0xffffffff };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SET_ERR(SENT);
        BOOL r = U(UpdateLayeredWindow)(w, NULL, NULL, NULL, NULL, NULL, 0, NULL, bad[i]);
        CHECKF(r == 0, "flags 0x%lx", bad[i]);
        CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "flags 0x%lx err %lu", bad[i], GetLastError());
    }
    im_kill(w);
}
TEST(ime_hotkeys_misc_ulw_negative_size) {
    NEED_ULW(); NEED_GUI(w);
    SIZE neg[] = { { -1, 10 }, { 10, -1 }, { -5, -5 }, { 0x80000000, 0 } };
    for (unsigned i = 0; i < sizeof neg / sizeof neg[0]; i++) {
        SET_ERR(SENT);
        BOOL r = U(UpdateLayeredWindow)(w, NULL, NULL, &neg[i], NULL, NULL, 0, NULL, 0);
        CHECKF(r == 0, "size %ld,%ld", neg[i].cx, neg[i].cy);
        CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "size %ld,%ld err %lu", neg[i].cx, neg[i].cy, GetLastError());  /* either dimension negative */
    }
    im_kill(w);
}
TEST(ime_hotkeys_misc_ulw_negative_dirty_rect_origin) {
    NEED_ULW(); NEED_GUI(w);
    RECT bad[] = { { -1, 0, 5, 5 }, { 0, -1, 5, 5 }, { -3, -3, 5, 5 } };
    for (unsigned i = 0; i < 3; i++) {
        UPDATELAYEREDWINDOWINFO ui;
        memset(&ui, 0, sizeof ui);
        ui.cbSize = sizeof ui;
        ui.prcDirty = &bad[i];
        SET_ERR(SENT);
        BOOL r2 = U(UpdateLayeredWindowIndirect)(w, &ui);
        CHECKF(r2 == 0, "dirty %ld,%ld", bad[i].left, bad[i].top);
        CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "dirty %ld,%ld err %lu", bad[i].left, bad[i].top, GetLastError());  /* left/top < 0 */
    }
    im_kill(w);
}
TEST(ime_hotkeys_misc_ulw_indirect_null_info) {
    NEED_ULW(); NEED_GUI(w);
    if (!U(UpdateLayeredWindowIndirect)) { im_kill(w); t_skip("no export"); return; }
    SET_ERR(SENT);
    EXPECT_FAIL(U(UpdateLayeredWindowIndirect)(w, NULL) == 0, ERROR_INVALID_PARAMETER);   /* user32 guard */
    im_kill(w);
}
TEST(ime_hotkeys_misc_ulw_indirect_wrong_cbsize) {
    NEED_ULW(); NEED_GUI(w);
    if (!U(UpdateLayeredWindowIndirect)) { im_kill(w); t_skip("no export"); return; }
    static const DWORD sizes[] = { 0, 1, 0x4f, 0x51, 0x48, 0x58, 0xffffffff };
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        UPDATELAYEREDWINDOWINFO ui;
        memset(&ui, 0, sizeof ui);
        ui.cbSize = sizes[i];
        SET_ERR(SENT);
        BOOL r = U(UpdateLayeredWindowIndirect)(w, &ui);
        CHECKF(r == 0, "cbSize 0x%lx", sizes[i]);
        CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "cbSize 0x%lx err %lu", sizes[i], GetLastError());   /* cbSize != 0x50 */
    }
    EXPECT_EQ(sizeof(UPDATELAYEREDWINDOWINFO), 0x50);
    im_kill(w);
}
TEST(ime_hotkeys_misc_ulw_size_checked_before_flags_ok) {
    NEED_ULW(); NEED_GUI(w);
    SIZE neg = { -2, 4 };
    SET_ERR(SENT);
    /* psize is validated first; a bad size with bad flags still reports 87, and no state changes */
    EXPECT_FAIL(U(UpdateLayeredWindow)(w, NULL, NULL, &neg, NULL, NULL, 0, NULL, 0x40) == 0, ERROR_INVALID_PARAMETER);
    im_kill(w);
}
TEST(ime_hotkeys_misc_ulw_move_only_on_layered_window) {
    NEED_ULW(); NEED_LW(w);
    POINT p = { -31000, -31000 };
    SIZE s = { 21, 17 };
    SET_ERR(SENT);
    BOOL r = U(UpdateLayeredWindow)(w, NULL, &p, &s, NULL, NULL, 0, NULL, 0);
    if (!r) { t_note("move-only UpdateLayeredWindow failed, err %lu", GetLastError()); im_kill(w); return; }
    RECT rc; U(GetWindowRect)(w, &rc);
    EXPECT_EQ(rc.left, p.x); EXPECT_EQ(rc.top, p.y);          /* zzzUpdateLayeredWindow moves and resizes the window */
    EXPECT_EQ(rc.right - rc.left, s.cx); EXPECT_EQ(rc.bottom - rc.top, s.cy);
    im_kill(w);
}
TEST(ime_hotkeys_misc_ulw_not_layered_fails) {
    NEED_ULW(); NEED_GUI(w);
    POINT p = { -31000, -31000 }; SIZE s = { 10, 10 };
    SET_ERR(SENT);
    BOOL r = U(UpdateLayeredWindow)(w, NULL, &p, &s, NULL, NULL, 0, NULL, 0);
    CHECK(r == 0);
    t_note("UpdateLayeredWindow on non-layered window: err %lu", GetLastError());
    im_kill(w);
}

/* ---- display affinity ------------------------------------------------------------------------- */
#define NEED_WDA() do { if (!UN(NtUserSetWindowDisplayAffinity) || !UN(NtUserGetWindowDisplayAffinity)) { t_skip("missing stub"); return; } } while (0)
static BOOL swda(HWND w, DWORD a) { return UN(NtUserSetWindowDisplayAffinity)(w, a); }
static BOOL gwda(HWND w, DWORD *a) { return UN(NtUserGetWindowDisplayAffinity)(w, a); }
TEST(ime_hotkeys_misc_wda_get_default_none) {
    NEED_WDA(); NEED_GUI(w);
    DWORD a = 0xdead;
    SET_ERR(SENT);
    CHECK(gwda(w, &a));
    EXPECT_EQ(a, WDA_NONE);
    im_kill(w);
}
TEST(ime_hotkeys_misc_wda_set_invalid_values) {
    NEED_WDA(); NEED_GUI(w);
    /* (value != 0 && (value & 0x11) == 0) -> ERROR_INVALID_PARAMETER, before any state change */
    static const DWORD bad[] = { 2, 4, 6, 8, 0x20, 0x40, 0x80, 0x100, 0x1000, 0x80000000, 0xffffffee };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SET_ERR(SENT);
        BOOL r = swda(w, bad[i]);
        CHECKF(r == 0, "value 0x%lx", bad[i]);
        CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "value 0x%lx err %lu", bad[i], GetLastError());
    }
    DWORD a = 0xdead;
    CHECK(gwda(w, &a));
    EXPECT_EQ(a, WDA_NONE);
    im_kill(w);
}
TEST(ime_hotkeys_misc_wda_set_none_on_fresh_window) {
    NEED_WDA(); NEED_GUI(w);
    SET_ERR(SENT);
    BOOL r = swda(w, WDA_NONE);
    if (!r) t_note("SetWindowDisplayAffinity(NONE) failed, err %lu", GetLastError());
    CHECK(r);
    im_kill(w);
}
TEST(ime_hotkeys_misc_wda_set_monitor_roundtrip) {
    NEED_WDA(); NEED_GUI(w);
    if (!swda(w, WDA_MONITOR)) { t_note("set MONITOR failed, err %lu", GetLastError()); im_kill(w); t_skip("composition unavailable"); return; }
    DWORD a = 0;
    CHECK(gwda(w, &a));
    EXPECT_EQ(a, WDA_MONITOR);
    CHECK(swda(w, WDA_NONE));
    CHECK(gwda(w, &a));
    EXPECT_EQ(a, WDA_NONE);
    im_kill(w);
}
TEST(ime_hotkeys_misc_wda_set_exclude_from_capture_roundtrip) {
    NEED_WDA(); NEED_GUI(w);
    if (!swda(w, WDA_EXCLUDEFROMCAPTURE)) { t_note("set EXCLUDEFROMCAPTURE failed, err %lu", GetLastError()); im_kill(w); t_skip("not supported"); return; }
    DWORD a = 0;
    CHECK(gwda(w, &a));
    EXPECT_EQ(a, WDA_EXCLUDEFROMCAPTURE);
    swda(w, WDA_NONE);
    im_kill(w);
}
TEST(ime_hotkeys_misc_wda_switch_between_values) {
    NEED_WDA(); NEED_GUI(w);
    if (!swda(w, WDA_MONITOR)) { im_kill(w); t_skip("composition unavailable"); return; }
    BOOL r = swda(w, WDA_EXCLUDEFROMCAPTURE);
    DWORD a = 0;
    if (r) { CHECK(gwda(w, &a)); EXPECT_EQ(a, WDA_EXCLUDEFROMCAPTURE); }
    swda(w, WDA_NONE);
    CHECK(gwda(w, &a));
    EXPECT_EQ(a, WDA_NONE);
    im_kill(w);
}
TEST(ime_hotkeys_misc_wda_set_same_value_twice) {
    NEED_WDA(); NEED_GUI(w);
    if (!swda(w, WDA_MONITOR)) { im_kill(w); t_skip("composition unavailable"); return; }
    CHECK(swda(w, WDA_MONITOR));
    swda(w, WDA_NONE);
    im_kill(w);
}
TEST(ime_hotkeys_misc_wda_independent_per_window) {
    NEED_WDA(); NEED_GUI(w);
    HWND w2 = im_mkpopup();
    if (!w2 || !swda(w, WDA_MONITOR)) { im_kill(w); im_kill(w2); t_skip("setup"); return; }
    DWORD a = 9;
    CHECK(gwda(w2, &a)); EXPECT_EQ(a, WDA_NONE);
    swda(w, WDA_NONE);
    im_kill(w); im_kill(w2);
}
TEST(ime_hotkeys_misc_wda_set_child_window_invalid) {
    NEED_WDA(); NEED_GUI(w);
    HWND c = im_mkwnd(WS_CHILD, 0, w);
    if (!c) { im_kill(w); t_skip("child window"); return; }
    SET_ERR(SENT);
    EXPECT_FAIL(swda(c, WDA_MONITOR) == 0, ERROR_INVALID_PARAMETER);   /* _IsTopLevelWindow false */
    DWORD a = 5;
    SET_ERR(SENT);
    EXPECT_FAIL(gwda(c, &a) == 0, ERROR_INVALID_PARAMETER);
    EXPECT_EQ(a, 5);
    im_kill(w);
}
TEST(ime_hotkeys_misc_wda_bogus_window) {
    NEED_WDA();
    DWORD a = 5;
    SET_ERR(SENT);
    EXPECT_FAIL(swda(im_bogus_hwnd(), WDA_NONE) == 0, ERROR_INVALID_WINDOW_HANDLE);
    SET_ERR(SENT);
    EXPECT_FAIL(gwda(im_bogus_hwnd(), &a) == 0, ERROR_INVALID_WINDOW_HANDLE);
    EXPECT_EQ(a, 5);
}
TEST(ime_hotkeys_misc_wda_null_window) {
    NEED_WDA();
    DWORD a = 5;
    EXPECT_EQ(swda(NULL, WDA_NONE), 0);
    EXPECT_EQ(gwda(NULL, &a), 0);
}
TEST(ime_hotkeys_misc_wda_set_other_process_window_denied) {
    NEED_WDA();
    HWND d = U(GetDesktopWindow)();
    if (!d) { t_skip("no desktop window"); return; }
    SET_ERR(SENT);
    /* the window's owner process is not ours -> ERROR_ACCESS_DENIED, checked before the top-level test */
    EXPECT_FAIL(swda(d, WDA_MONITOR) == 0, ERROR_ACCESS_DENIED);
}
TEST(ime_hotkeys_misc_wda_set_shell_window_denied) {
    NEED_WDA();
    HWND s = U(GetShellWindow)();
    if (!s) { t_skip("no shell window"); return; }
    DWORD pid = 0; U(GetWindowThreadProcessId)(s, &pid);
    if (pid == GetCurrentProcessId()) { t_skip("shell is ours"); return; }
    SET_ERR(SENT);
    EXPECT_FAIL(swda(s, WDA_MONITOR) == 0, ERROR_ACCESS_DENIED);
    SET_ERR(SENT);
    EXPECT_FAIL(swda(s, 2) == 0, ERROR_ACCESS_DENIED);       /* ownership precedes the value check */
}
TEST(ime_hotkeys_misc_wda_get_other_process_window) {
    NEED_WDA();
    HWND s = U(GetShellWindow)();
    if (!s) { t_skip("no shell window"); return; }
    DWORD a = 0xdead;
    BOOL r = gwda(s, &a);      /* Get has no ownership test */
    t_note("GetWindowDisplayAffinity(shell) -> %d value 0x%lx err %lu", r, a, r ? 0 : GetLastError());
    if (r) CHECK(a == WDA_NONE || a == WDA_MONITOR || a == WDA_EXCLUDEFROMCAPTURE);
}
TEST(ime_hotkeys_misc_wda_user32_path_identical) {
    NEED_WDA(); NEED_GUI(w);
    if (!U(GetWindowDisplayAffinity) || !U(SetWindowDisplayAffinity)) { im_kill(w); t_skip("no user32 exports"); return; }
    DWORD a = 7, b = 8;
    CHECK(U(GetWindowDisplayAffinity)(w, &a));
    CHECK(gwda(w, &b));
    EXPECT_EQ(a, b);
    SET_ERR(SENT);
    EXPECT_FAIL(U(SetWindowDisplayAffinity)(w, 2) == 0, ERROR_INVALID_PARAMETER);
    im_kill(w);
}
TEST(ime_hotkeys_misc_wda_affinity_cleared_by_hide) {
    NEED_WDA(); NEED_GUI(w);
    if (!swda(w, WDA_MONITOR)) { im_kill(w); t_skip("composition unavailable"); return; }
    DWORD a = 0;
    CHECK(gwda(w, &a));
    EXPECT_EQ(a, WDA_MONITOR);
    U(ShowWindow)(w, SW_SHOWNOACTIVATE);
    CHECK(gwda(w, &a));
    EXPECT_EQ(a, WDA_MONITOR);              /* showing the window keeps the affinity */
    U(ShowWindow)(w, SW_HIDE);
    a = 0xdead;
    CHECK(gwda(w, &a));
    EXPECT_EQ(a, WDA_NONE);                 /* hiding it drops the affinity on Windows 10 */
    CHECK(swda(w, WDA_MONITOR));            /* and it can be set again */
    CHECK(gwda(w, &a));
    EXPECT_EQ(a, WDA_MONITOR);
    swda(w, WDA_NONE);
    im_kill(w);
}
