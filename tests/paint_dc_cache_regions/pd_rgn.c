/* Window regions (SetWindowRgn / GetWindowRgn / GetWindowRgnBox / NtUserGetWindowRgnEx / NtUserSetWindowRgnEx) and scrolling
 * (NtUserScrollDC on a DIB-section memory DC, NtUserScrollWindowEx on a window). */
#include "pd.h"

#define NEED_RGN() REQUIRE(U(SetWindowRgn)); REQUIRE(U(GetWindowRgn)); REQUIRE(U(GetWindowRgnBox)); REQUIRE(UN(NtUserSetWindowRgn)); \
                   REQUIRE(U(CreateRectRgn)); REQUIRE(U(GetRgnBox)); REQUIRE(U(PtInRegion)); REQUIRE(U(EqualRgn)); REQUIRE(U(GetObjectType)); \
                   REQUIRE(U(CreateEllipticRgn)); REQUIRE(UN(NtUserInvalidateRect)); REQUIRE(UN(NtUserGetUpdateRect))

static RECT client(HWND h) { RECT c; U(GetClientRect)(h, &c); return c; }
static int rgn_valid(HRGN r) { return U(GetObjectType)(r) == OBJ_REGION; }

/* ---- ownership: the kernel copies, user32 deletes the original on success ---- */
PT(setwindowrgn_nt_copies_the_region) {
    PD_BEGIN(); NEED_RGN();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    HRGN rg = pd_rgn(5, 6, 40, 50);
    CHECK(UN(NtUserSetWindowRgn)(h, rg, FALSE) != 0);
    /* SetWindowRgn stores a copy of the region: a fresh region, the caller's one is untouched */
    CHECK(rgn_valid(rg));
    RECT b, e = pd_rc(5, 6, 40, 50);
    CHECK(pd_rgn_box(rg, &b) == SIMPLEREGION); CHECK(pd_eq(&b, &e));
    ULONG_PTR clip = 0;
    if (pd_hrgn_clip(h, &clip)) {
        CHECK(clip != 0);
        CHECK(clip != (ULONG_PTR)rg);
        t_note("hrgnClip=%p (caller's region %p)", (void *)clip, (void *)rg);
    }
    /* the stored copy is independent: changing the original later does not change the window region */
    U(OffsetRgn)(rg, 20, 20);
    HRGN out = pd_rgn(0, 0, 1, 1);
    CHECK(U(GetWindowRgn)(h, out) == SIMPLEREGION);
    CHECK(pd_rgn_box(out, &b) == SIMPLEREGION); CHECK(pd_eq(&b, &e));
    pd_rgn_free(out); pd_rgn_free(rg);
    PD_END();
}
PT(setwindowrgn_user32_deletes_original_on_success) {
    PD_BEGIN(); NEED_RGN();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    HRGN rg = pd_rgn(5, 6, 40, 50);
    struct pd_cell before; int have = pd_cell(rg, &before);
    CHECK(rgn_valid(rg));
    CHECK(U(SetWindowRgn)(h, rg, FALSE) != 0);
    /* user32!RealSetWindowRgn: if NtUserSetWindowRgn succeeded, DeleteObject(hRgn) */
    CHECK(U(GetObjectType)(rg) == 0);
    if (have) {
        struct pd_cell after;
        /* the freed slot is either empty or already reused for another object: it no longer describes this handle */
        if (pd_cell(rg, &after)) CHECK(after.objt == 0 || after.upper != (unsigned short)((ULONG_PTR)rg >> 16));
    }
    PD_END();
}
PT(setwindowrgn_user32_failure_keeps_original) {
    PD_BEGIN(); NEED_RGN();
    HRGN rg = pd_rgn(5, 6, 40, 50);
    CHECK(U(SetWindowRgn)((HWND)(ULONG_PTR)0x12345678, rg, FALSE) == 0);
    CHECK(rgn_valid(rg));
    pd_rgn_free(rg);
    PD_END();
}
PT(setwindowrgn_invalid_region_handle_fails_and_window_unchanged) {
    PD_BEGIN(); NEED_RGN();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    HGDIOBJ br = U(GetStockObject)(WHITE_BRUSH);
    CHECK(U(SetWindowRgn)(h, (HRGN)(ULONG_PTR)0x1234, FALSE) == 0);   /* region copy fails: handle validation */
    CHECK(UN(NtUserSetWindowRgn)(h, (HRGN)br, FALSE) == 0);           /* a brush is not a region */
    ULONG_PTR clip = 7;
    if (pd_hrgn_clip(h, &clip)) CHECK(clip == 0);
    HRGN out = pd_rgn(0, 0, 1, 1);
    CHECK(U(GetWindowRgn)(h, out) == 0);
    pd_rgn_free(out);
    PD_END();
}
PT(setwindowrgn_null_removes_region) {
    PD_BEGIN(); NEED_RGN();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    HRGN rg = pd_rgn(5, 6, 40, 50);
    CHECK(U(SetWindowRgn)(h, rg, FALSE));
    ULONG_PTR clip = 0;
    int have = pd_hrgn_clip(h, &clip);
    if (have) CHECK(clip != 0);
    CHECK(UN(NtUserSetWindowRgn)(h, NULL, FALSE) != 0);
    if (pd_hrgn_clip(h, &clip)) CHECK(clip == 0);
    HRGN out = pd_rgn(0, 0, 1, 1);
    CHECK(U(GetWindowRgn)(h, out) == 0);
    pd_rgn_free(out);
    PD_END();
}
PT(setwindowrgn_null_via_user32_succeeds) {
    PD_BEGIN(); NEED_RGN();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    CHECK(U(SetWindowRgn)(h, NULL, FALSE) != 0);     /* RealSetWindowRgn then runs DeleteObject(NULL), harmless */
    PD_END();
}
PT(setwindowrgn_stored_handle_is_gdi_region) {
    PD_BEGIN(); NEED_RGN();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    HRGN rg = pd_rgn(5, 6, 40, 50);
    CHECK(UN(NtUserSetWindowRgn)(h, rg, FALSE));
    ULONG_PTR clip = 0;
    if (!pd_hrgn_clip(h, &clip)) { pd_rgn_free(rg); t_skip("client-side window block not readable"); return; }
    CHECK(clip > 1);
    CHECK(PD_TYPE7((void *)clip) == 4);               /* region type bits in the GDI handle */
    struct pd_cell c;
    if (pd_cell((void *)clip, &c)) {
        CHECK(c.objt == 4);
        t_info("window region cell: owner=0x%x pid=%lu", c.owner, GetCurrentProcessId());
    }
    RECT b;
    int t = pd_rgn_box((HRGN)clip, &b);
    t_note("GetRgnBox(hrgnClip)=%d (%ld,%ld)-(%ld,%ld)", t, b.left, b.top, b.right, b.bottom);
    if (t == SIMPLEREGION) {
        POINT o = {0, 0}; U(ClientToScreen)(h, &o);       /* popup without frame: window origin == client origin; stored in screen space */
        RECT e = pd_rc(5 + o.x, 6 + o.y, 40 + o.x, 50 + o.y);
        CHECK(pd_eq(&b, &e));
    }
    pd_rgn_free(rg);
    PD_END();
}
PT(setwindowrgn_replace_and_remove_note_on_old_handle) {
    PD_BEGIN(); NEED_RGN();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    HRGN a = pd_rgn(5, 6, 40, 50), b = pd_rgn(1, 1, 20, 20);
    UN(NtUserSetWindowRgn)(h, a, FALSE);
    ULONG_PTR c1 = 0, c2 = 0;
    if (!pd_hrgn_clip(h, &c1)) { pd_rgn_free(a); pd_rgn_free(b); t_skip("client-side window block not readable"); return; }
    UN(NtUserSetWindowRgn)(h, b, FALSE);
    pd_hrgn_clip(h, &c2);
    CHECK(c2 != 0); CHECK(c2 != c1);
    t_note("old window region %p GetObjectType after replacement = %lu", (void *)c1, (unsigned long)U(GetObjectType)((HGDIOBJ)c1));
    UN(NtUserSetWindowRgn)(h, NULL, FALSE);
    t_note("last window region %p GetObjectType after removal = %lu", (void *)c2, (unsigned long)U(GetObjectType)((HGDIOBJ)c2));
    pd_rgn_free(a); pd_rgn_free(b);
    PD_END();
}
PT(setwindowrgn_empty_region_is_nullregion) {
    PD_BEGIN(); NEED_RGN();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    HRGN rg = pd_rgn(0, 0, 0, 0);
    CHECK(UN(NtUserSetWindowRgn)(h, rg, FALSE) != 0);
    HRGN out = pd_rgn(1, 1, 9, 9);
    CHECK(U(GetWindowRgn)(h, out) == NULLREGION);
    RECT b; CHECK(pd_rgn_box(out, &b) == NULLREGION);
    pd_rgn_free(out); pd_rgn_free(rg);
    PD_END();
}
PT(setwindowrgn_bad_hwnd_via_nt) {
    PD_BEGIN(); NEED_RGN();
    HRGN rg = pd_rgn(5, 6, 40, 50);
    SET_ERR(0);
    CHECK(UN(NtUserSetWindowRgn)((HWND)(ULONG_PTR)0x12345678, rg, FALSE) == 0);
    EXPECT_ERR(PD_ERR_INVALID_WINDOW_HANDLE);
    CHECK(rgn_valid(rg));
    pd_rgn_free(rg);
    PD_END();
}
PT(setwindowrgn_desktop_window_rejected) {
    PD_BEGIN(); NEED_RGN();
    HWND d = U(GetDesktopWindow)();
    REQUIRE(d);
    HRGN rg = pd_rgn(5, 6, 40, 50);
    /* NtUserSetWindowRgn: ((fnid & 0xffff2fff) - 0x29d) & ~2 == 0 (desktop 0x29d / message window 0x29f) is refused */
    CHECK(UN(NtUserSetWindowRgn)(d, rg, FALSE) == 0);
    CHECK(rgn_valid(rg));
    pd_rgn_free(rg);
    PD_END();
}
PT(setwindowrgn_popup_with_message_parent_accepted) {
    PD_BEGIN(); NEED_RGN();
    HWND m = U(CreateWindowExA)(0, pd_cls(PDC_PLAIN), "msg", WS_POPUP, 0, 0, 10, 10, HWND_MESSAGE, NULL, pd_hinst(), NULL);
    if (!m) { t_skip("message-only window creation failed"); return; }
    HRGN rg = pd_rgn(1, 1, 5, 5);
    int r = UN(NtUserSetWindowRgn)(m, rg, FALSE);
    /* only the message-only root window itself (fnid 0x29f) is refused; a window created below HWND_MESSAGE takes a region */
    if (t_is_wine()) t_note("SetWindowRgn(window below HWND_MESSAGE) = %d", r); else CHECK(r != 0);
    CHECK(rgn_valid(rg));
    pd_rgn_free(rg);
    U(DestroyWindow)(m);
    PD_END();
}
PT(setwindowrgn_on_child_window) {
    PD_BEGIN(); NEED_RGN();
    HWND p = pd_top(PDC_PLAIN);
    REQUIRE(p);
    HWND c = pd_child(p, PDC_PLAIN, 0, 10, 10, 40, 30);
    REQUIRE(c);
    HRGN rg = pd_rgn(2, 3, 20, 25);
    CHECK(U(SetWindowRgn)(c, rg, FALSE));
    HRGN out = pd_rgn(0, 0, 1, 1);
    RECT b, e = pd_rc(2, 3, 20, 25);
    CHECK(U(GetWindowRgn)(c, out) == SIMPLEREGION);
    pd_rgn_box(out, &b); CHECK(pd_eq(&b, &e));
    pd_rgn_free(out);
    PD_END();
}

/* ---- GetWindowRgn / GetWindowRgnBox ---- */
PT(getwindowrgn_returns_window_relative_rect) {
    PD_BEGIN(); NEED_RGN();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    HRGN rg = pd_rgn(5, 6, 40, 50);
    U(SetWindowRgn)(h, rg, FALSE);
    HRGN out = pd_rgn(100, 100, 101, 101);
    RECT b, e = pd_rc(5, 6, 40, 50);
    CHECK(U(GetWindowRgn)(h, out) == SIMPLEREGION);   /* CombineRgn(out, hrgnClip, NULL, RGN_COPY), then offset by -rcWindow origin */
    CHECK(pd_rgn_box(out, &b) == SIMPLEREGION); CHECK(pd_eq(&b, &e));
    pd_rgn_free(out);
    PD_END();
}
PT(getwindowrgn_complex_region_roundtrip) {
    PD_BEGIN(); NEED_RGN();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    HRGN el = pd_rgn(0, 0, 1, 1), src = U(CreateEllipticRgn)(2, 3, 62, 63);
    U(CombineRgn)(el, src, NULL, RGN_COPY);
    U(SetWindowRgn)(h, src, FALSE);
    HRGN out = pd_rgn(0, 0, 1, 1);
    CHECK(U(GetWindowRgn)(h, out) == COMPLEXREGION);
    CHECK(U(EqualRgn)(out, el));
    pd_rgn_free(out); pd_rgn_free(el);
    PD_END();
}
PT(getwindowrgn_without_region_is_error) {
    PD_BEGIN(); NEED_RGN();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    HRGN out = pd_rgn(1, 1, 9, 9);
    CHECK(U(GetWindowRgn)(h, out) == ERROR);          /* tagWND.hrgnClip == NULL -> 0 */
    RECT b; CHECK(pd_rgn_box(out, &b) == SIMPLEREGION);   /* target untouched */
    pd_rgn_free(out);
    PD_END();
}
PT(getwindowrgn_null_target_and_bad_hwnd) {
    PD_BEGIN(); NEED_RGN();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    HRGN rg = pd_rgn(5, 6, 40, 50);
    U(SetWindowRgn)(h, rg, FALSE);
    CHECK(U(GetWindowRgn)(h, NULL) == 0);
    HRGN out = pd_rgn(0, 0, 1, 1);
    CHECK(U(GetWindowRgn)((HWND)(ULONG_PTR)0x12345678, out) == 0);
    pd_rgn_free(out);
    PD_END();
}
PT(getwindowrgnbox_matches_region_box) {
    PD_BEGIN(); NEED_RGN();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    RECT r = pd_rc(9, 9, 9, 9), e = pd_rc(5, 6, 40, 50);
    CHECK(U(GetWindowRgnBox)(h, &r) == ERROR);        /* no region */
    HRGN rg = pd_rgn(5, 6, 40, 50);
    U(SetWindowRgn)(h, rg, FALSE);
    CHECK(U(GetWindowRgnBox)(h, &r) == SIMPLEREGION);
    CHECK(pd_eq(&r, &e));
    CHECK(U(GetWindowRgnBox)(h, NULL) == 0);
    CHECK(U(GetWindowRgnBox)((HWND)(ULONG_PTR)0x12345678, &r) == 0);
    PD_END();
}
PT(getwindowrgnbox_complex_region_gives_bounding_box) {
    PD_BEGIN(); NEED_RGN();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    HRGN a = pd_rgn(0, 0, 10, 10), b = pd_rgn(30, 30, 50, 50), u = pd_rgn(0, 0, 1, 1);
    U(CombineRgn)(u, a, b, RGN_OR);
    U(SetWindowRgn)(h, u, FALSE);
    RECT r = {0}, e = pd_rc(0, 0, 50, 50);
    CHECK(U(GetWindowRgnBox)(h, &r) == COMPLEXREGION);
    CHECK(pd_eq(&r, &e));
    pd_rgn_free(a); pd_rgn_free(b);
    PD_END();
}
PT(window_region_is_window_relative_after_move) {
    PD_BEGIN(); NEED_RGN();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    HRGN rg = pd_rgn(5, 6, 40, 50);
    U(SetWindowRgn)(h, rg, FALSE);
    ULONG_PTR c1 = 0;
    RECT b1 = {0}, b2 = {0};
    int have = pd_hrgn_clip(h, &c1) && c1 > 1 && pd_rgn_box((HRGN)c1, &b1) == SIMPLEREGION;
    U(SetWindowPos)(h, NULL, PD_X + 31, PD_Y + 17, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    RECT r = {0}, e = pd_rc(5, 6, 40, 50);
    CHECK(U(GetWindowRgnBox)(h, &r) == SIMPLEREGION);
    CHECK(pd_eq(&r, &e));                              /* the API result does not depend on the window's screen position */
    ULONG_PTR c2 = 0;
    if (have && pd_hrgn_clip(h, &c2) && c2 > 1 && pd_rgn_box((HRGN)c2, &b2) == SIMPLEREGION) {
        /* the stored region is in screen space and was moved together with the window */
        CHECK(b2.left - b1.left == 31); CHECK(b2.top - b1.top == 17);
    } else t_note("stored region box not comparable after the move");
    PD_END();
}

/* ---- NtUserGetWindowRgnEx / NtUserSetWindowRgnEx ---- */
PT(getwindowrgnex_flag_validation) {
    PD_BEGIN(); NEED_RGN(); REQUIRE(UN(NtUserGetWindowRgnEx));
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    HRGN rg = pd_rgn(5, 6, 40, 50);
    U(SetWindowRgn)(h, rg, FALSE);
    HRGN out = pd_rgn(0, 0, 1, 1);
    /* non-DWM callers: flags & ~0x3f -> ERROR_INVALID_FLAGS; the flag test precedes the NULL-hrgn test */
    UINT bad[] = { 0x40, 0x80, 0x100, 0x8000, 0x80000000 };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SET_ERR(0);
        CHECKF(UN(NtUserGetWindowRgnEx)(h, out, bad[i]) == 0, "flags 0x%x", bad[i]);
        EXPECT_ERR(PD_ERR_INVALID_FLAGS);
        SET_ERR(0);
        CHECKF(UN(NtUserGetWindowRgnEx)(h, NULL, bad[i]) == 0, "flags 0x%x null hrgn", bad[i]);
        EXPECT_ERR(PD_ERR_INVALID_FLAGS);
    }
    pd_rgn_free(out);
    PD_END();
}
PT(getwindowrgnex_null_region_is_invalid_parameter) {
    PD_BEGIN(); NEED_RGN(); REQUIRE(UN(NtUserGetWindowRgnEx));
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    SET_ERR(0);
    CHECK(UN(NtUserGetWindowRgnEx)(h, NULL, 0) == 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    PD_END();
}
PT(getwindowrgnex_flags_zero_copies_window_region) {
    PD_BEGIN(); NEED_RGN(); REQUIRE(UN(NtUserGetWindowRgnEx));
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    HRGN out = pd_rgn(1, 1, 3, 3);
    CHECK(UN(NtUserGetWindowRgnEx)(h, out, 0) == 0);        /* no region: result 0, target not touched */
    HRGN rg = pd_rgn(5, 6, 40, 50);
    U(SetWindowRgn)(h, rg, FALSE);
    RECT b, e = pd_rc(5, 6, 40, 50);
    CHECK(UN(NtUserGetWindowRgnEx)(h, out, 0) == SIMPLEREGION);
    pd_rgn_box(out, &b); CHECK(pd_eq(&b, &e));
    pd_rgn_free(out);
    PD_END();
}
PT(getwindowrgnex_bad_hwnd) {
    PD_BEGIN(); NEED_RGN(); REQUIRE(UN(NtUserGetWindowRgnEx));
    HRGN out = pd_rgn(1, 1, 3, 3);
    SET_ERR(0);
    CHECK(UN(NtUserGetWindowRgnEx)((HWND)(ULONG_PTR)0x12345678, out, 0) == 0);
    EXPECT_ERR(PD_ERR_INVALID_WINDOW_HANDLE);
    pd_rgn_free(out);
    PD_END();
}
PT(setwindowrgnex_access_denied_for_non_dwm) {
    PD_BEGIN(); NEED_RGN(); REQUIRE(UN(NtUserSetWindowRgnEx));
    if (t_is_wine()) { t_skip("win32u.NtUserSetWindowRgnEx is an aborting stub in Wine"); return; }
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    HRGN rg = pd_rgn(5, 6, 40, 50);
    SET_ERR(0);
    CHECK(UN(NtUserSetWindowRgnEx)(h, rg, 0) == 0);        /* IsProcessDwm() is false for this process: ERROR_ACCESS_DENIED */
    EXPECT_ERR(ERROR_ACCESS_DENIED);
    SET_ERR(0);
    CHECK(UN(NtUserSetWindowRgnEx)(h, rg, 4) == 0);
    EXPECT_ERR(ERROR_ACCESS_DENIED);
    CHECK(rgn_valid(rg));
    ULONG_PTR clip = 5;
    if (pd_hrgn_clip(h, &clip)) CHECK(clip == 0);
    pd_rgn_free(rg);
    PD_END();
}
PT(setwindowrgnex_bad_hwnd_and_desktop_fail_without_access_error) {
    PD_BEGIN(); NEED_RGN(); REQUIRE(UN(NtUserSetWindowRgnEx));
    if (t_is_wine()) { t_skip("win32u.NtUserSetWindowRgnEx is an aborting stub in Wine"); return; }
    HRGN rg = pd_rgn(5, 6, 40, 50);
    SET_ERR(0);
    CHECK(UN(NtUserSetWindowRgnEx)((HWND)(ULONG_PTR)0x12345678, rg, 0) == 0);
    EXPECT_ERR(PD_ERR_INVALID_WINDOW_HANDLE);
    SET_ERR(0);
    CHECK(UN(NtUserSetWindowRgnEx)(U(GetDesktopWindow)(), rg, 0) == 0);   /* the FNID test fails before the DWM check */
    CHECK(GetLastError() != ERROR_ACCESS_DENIED);
    pd_rgn_free(rg);
    PD_END();
}

/* ---- the window region as visible region / hit-test shape ---- */
PT(window_region_clips_client_dc) {
    PD_BEGIN(); NEED_RGN();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    PD_NEED_EXPOSED(h);
    HRGN rg = pd_rgn(5, 6, 40, 50);
    U(SetWindowRgn)(h, rg, FALSE);
    HDC dc = U(GetDC)(h);
    REQUIRE(dc);
    RECT b, e = pd_rc(5, 6, 40, 50), in = pd_rc(10, 10, 20, 20), out = pd_rc(50, 60, 70, 70);
    CHECK(U(GetClipBox)(dc, &b) == SIMPLEREGION);
    CHECK(pd_eq(&b, &e));
    CHECK(U(RectVisible)(dc, &in)); CHECK(!U(RectVisible)(dc, &out));
    U(ReleaseDC)(h, dc);
    U(SetWindowRgn)(h, NULL, FALSE);
    dc = U(GetDC)(h);
    RECT c = client(h);
    CHECK(U(GetClipBox)(dc, &b) == SIMPLEREGION); CHECK(pd_eq(&b, &c));
    U(ReleaseDC)(h, dc);
    PD_END();
}
PT(window_region_clips_window_dc_of_framed_window) {
    PD_BEGIN(); NEED_RGN();
    HWND h = pd_top_style(PDC_PLAIN, WS_CAPTION);
    REQUIRE(h);
    PD_NEED_EXPOSED(h);
    HRGN rg = pd_rgn(5, 6, 40, 50);
    U(SetWindowRgn)(h, rg, FALSE);
    HDC dc = U(GetWindowDC)(h);
    REQUIRE(dc);
    RECT b, e = pd_rc(5, 6, 40, 50);
    CHECK(U(GetClipBox)(dc, &b) == SIMPLEREGION);
    CHECK(pd_eq(&b, &e));                              /* window regions are window-relative: the window DC is clipped by them exactly */
    U(ReleaseDC)(h, dc);
    PD_END();
}
PT(window_region_empty_makes_dc_clip_empty) {
    PD_BEGIN(); NEED_RGN();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    PD_NEED_EXPOSED(h);
    HRGN rg = pd_rgn(0, 0, 0, 0);
    U(SetWindowRgn)(h, rg, FALSE);
    HDC dc = U(GetDC)(h);
    REQUIRE(dc);
    RECT b;
    CHECK(U(GetClipBox)(dc, &b) == NULLREGION);
    U(ReleaseDC)(h, dc);
    PD_END();
}
PT(window_region_limits_paint_rect) {
    PD_BEGIN(); NEED_RGN();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    PD_NEED_EXPOSED(h);
    HRGN rg = pd_rgn(5, 6, 40, 50);
    U(SetWindowRgn)(h, rg, FALSE);
    pd_settle(h);
    pd_beh = PB_PAINT_BP;
    UN(NtUserInvalidateRect)(h, NULL, FALSE);
    pd_pump(40, 30);
    REQUIRE(pd_obs.valid);
    RECT e = pd_rc(5, 6, 40, 50);
    CHECK(pd_eq(&pd_obs.ps.rcPaint, &e));              /* BeginPaint's DC = update region ∩ visible region (which includes the window region) */
    pd_beh = 0;
    PD_END();
}
PT(window_region_changes_hit_testing) {
    PD_BEGIN(); NEED_RGN(); REQUIRE(UN(NtUserWindowFromPoint));
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    /* a fully transparent layered window (alpha 0) is skipped by hit testing: make it all but invisible instead (alpha 1/255) */
    U(SetLayeredWindowAttributes)(h, 0, 1, LWA_ALPHA); pd_pump(30, 30);
    RECT wr; U(GetWindowRect)(h, &wr);
    POINT left = { wr.left + 10, wr.top + PD_CH / 2 }, right = { wr.left + PD_CW - 10, wr.top + PD_CH / 2 };
    if (UN(NtUserWindowFromPoint)(left) != h || UN(NtUserWindowFromPoint)(right) != h) {
        t_skip("fixture window is not hit-testable at its own points (covered or headless)");
        return;
    }
    HRGN rg = pd_rgn(0, 0, PD_CW / 2, PD_CH);          /* left half only */
    CHECK(U(SetWindowRgn)(h, rg, FALSE));
    CHECK(UN(NtUserWindowFromPoint)(left) == h);
    CHECK(UN(NtUserWindowFromPoint)(right) != h);       /* outside the window region the window is not hit */
    CHECK(U(WindowFromPoint)(right) != h);
    U(SetWindowRgn)(h, NULL, FALSE);
    CHECK(UN(NtUserWindowFromPoint)(right) == h);
    PD_END();
}
PT(window_region_hit_test_via_ellipse_corner) {
    PD_BEGIN(); NEED_RGN(); REQUIRE(UN(NtUserWindowFromPoint));
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    U(SetLayeredWindowAttributes)(h, 0, 1, LWA_ALPHA); pd_pump(30, 30);   /* alpha 0 would be skipped by hit testing */
    RECT wr; U(GetWindowRect)(h, &wr);
    POINT center = { wr.left + PD_CW / 2, wr.top + PD_CH / 2 }, corner = { wr.left + 1, wr.top + 1 };
    if (UN(NtUserWindowFromPoint)(center) != h || UN(NtUserWindowFromPoint)(corner) != h) {
        t_skip("fixture window is not hit-testable at its own points (covered or headless)");
        return;
    }
    HRGN rg = U(CreateEllipticRgn)(0, 0, PD_CW, PD_CH);
    U(SetWindowRgn)(h, rg, FALSE);
    CHECK(UN(NtUserWindowFromPoint)(center) == h);
    CHECK(UN(NtUserWindowFromPoint)(corner) != h);
    PD_END();
}

/* ---- ScrollDC on a DIB section ---- */
#define SD_W 16
#define SD_H 16
static unsigned sd_px(int x, int y) { return 0x00800000u | ((unsigned)y << 8) | (unsigned)x; }
static void sd_fill(struct pd_dib *d) { for (int y = 0; y < SD_H; y++) for (int x = 0; x < SD_W; x++) d->bits[y * SD_W + x] = sd_px(x, y); }
#define NEED_SCROLLDC() REQUIRE(UN(NtUserScrollDC)); REQUIRE(U(CreateCompatibleDC)); REQUIRE(U(CreateDIBSection)); REQUIRE(U(GdiFlush)); \
                        REQUIRE(U(CreateRectRgn)); REQUIRE(U(GetRgnBox)); REQUIRE(U(PtInRegion))

static int sd_moved_ok(struct pd_dib *d, int dx, int dy, const RECT *clip) {
    int bad = 0;
    for (int y = clip->top; y < clip->bottom; y++)
        for (int x = clip->left; x < clip->right; x++) {
            int sx = x - dx, sy = y - dy;
            if (sx < 0 || sy < 0 || sx >= SD_W || sy >= SD_H) continue;
            if (pd_dib_px(d, x, y) != (sd_px(sx, sy) & 0xFFFFFF)) bad++;
        }
    return bad;
}
struct sd_row { const char *name; int dx, dy; RECT upd; };
static void run_sd(const struct sd_row *r) {
    PD_BEGIN(); NEED_SCROLLDC();
    struct pd_dib d;
    if (!pd_dib_make(&d, SD_W, SD_H)) { t_skip("cannot create a DIB section"); return; }
    sd_fill(&d);
    RECT all = pd_rc(0, 0, SD_W, SD_H), upd = pd_rc(-1, -1, -1, -1);
    HRGN rg = pd_rgn(50, 50, 60, 60);
    BOOL ok = UN(NtUserScrollDC)(d.dc, r->dx, r->dy, &all, &all, rg, &upd);
    CHECKF(ok == TRUE, "%s: ScrollDC failed (err %lu)", r->name, GetLastError());
    int bad = sd_moved_ok(&d, r->dx, r->dy, &all);
    CHECKF(bad == 0, "%s: %d pixels not moved by (%d,%d)", r->name, bad, r->dx, r->dy);
    CHECKF(pd_eq(&upd, &r->upd), "%s: update rect (%ld,%ld)-(%ld,%ld) want (%ld,%ld)-(%ld,%ld)", r->name, upd.left, upd.top, upd.right, upd.bottom,
           r->upd.left, r->upd.top, r->upd.right, r->upd.bottom);
    RECT b;
    pd_rgn_box(rg, &b);
    CHECKF(pd_eq(&b, &r->upd), "%s: update region box (%ld,%ld)-(%ld,%ld)", r->name, b.left, b.top, b.right, b.bottom);
    pd_rgn_free(rg);
    pd_dib_free(&d);
    PD_END();
}
#define SD_ROW(id, dx, dy, l, t, r, b) \
    static const struct sd_row sd_##id = { #id, dx, dy, { l, t, r, b } }; \
    PT(scrolldc_dib_##id) { run_sd(&sd_##id); }
SD_ROW(right4,      4,  0,  0,  0,  4, 16)
SD_ROW(left4,      -4,  0, 12,  0, 16, 16)
SD_ROW(down3,       0,  3,  0,  0, 16,  3)
SD_ROW(up3,         0, -3,  0, 13, 16, 16)
SD_ROW(right_down,  4,  2,  0,  0, 16, 16)
SD_ROW(left_up,    -4, -2,  0,  0, 16, 16)
SD_ROW(right_up,    4, -2,  0,  0, 16, 16)
SD_ROW(one_pixel,   1,  0,  0,  0,  1, 16)
SD_ROW(whole_width, 16, 0,  0,  0, 16, 16)
SD_ROW(beyond_size, 40, 0,  0,  0, 16, 16)

PT(scrolldc_dib_update_region_is_l_shape) {
    PD_BEGIN(); NEED_SCROLLDC();
    struct pd_dib d;
    if (!pd_dib_make(&d, SD_W, SD_H)) { t_skip("cannot create a DIB section"); return; }
    sd_fill(&d);
    RECT all = pd_rc(0, 0, SD_W, SD_H);
    HRGN rg = pd_rgn(0, 0, 1, 1);
    CHECK(UN(NtUserScrollDC)(d.dc, 4, 2, &all, &all, rg, NULL));
    CHECK(U(PtInRegion)(rg, 1, 10));       /* left strip */
    CHECK(U(PtInRegion)(rg, 10, 1));       /* top strip */
    CHECK(!U(PtInRegion)(rg, 10, 10));     /* covered by the moved bits */
    CHECK(!U(PtInRegion)(rg, 5, 3));
    pd_rgn_free(rg);
    pd_dib_free(&d);
    PD_END();
}
PT(scrolldc_dib_clip_rect_limits_destination) {
    PD_BEGIN(); NEED_SCROLLDC();
    struct pd_dib d;
    if (!pd_dib_make(&d, SD_W, SD_H)) { t_skip("cannot create a DIB section"); return; }
    sd_fill(&d);
    RECT all = pd_rc(0, 0, SD_W, SD_H), clip = pd_rc(4, 4, 12, 12);
    CHECK(UN(NtUserScrollDC)(d.dc, 2, 0, &all, &clip, NULL, NULL));
    CHECK(pd_dib_px(&d, 6, 6) == (sd_px(4, 6) & 0xFFFFFF));       /* inside the clip: moved */
    CHECK(pd_dib_px(&d, 11, 9) == (sd_px(9, 9) & 0xFFFFFF));
    CHECK(pd_dib_px(&d, 14, 6) == (sd_px(14, 6) & 0xFFFFFF));     /* outside the clip: untouched */
    CHECK(pd_dib_px(&d, 3, 6) == (sd_px(3, 6) & 0xFFFFFF));
    CHECK(pd_dib_px(&d, 6, 2) == (sd_px(6, 2) & 0xFFFFFF));
    CHECK(pd_dib_px(&d, 6, 13) == (sd_px(6, 13) & 0xFFFFFF));
    pd_dib_free(&d);
    PD_END();
}
PT(scrolldc_dib_scroll_rect_limits_source) {
    PD_BEGIN(); NEED_SCROLLDC();
    struct pd_dib d;
    if (!pd_dib_make(&d, SD_W, SD_H)) { t_skip("cannot create a DIB section"); return; }
    sd_fill(&d);
    RECT scroll = pd_rc(0, 0, 8, 16), all = pd_rc(0, 0, SD_W, SD_H);
    CHECK(UN(NtUserScrollDC)(d.dc, 2, 0, &scroll, &all, NULL, NULL));
    CHECK(pd_dib_px(&d, 9, 3) == (sd_px(7, 3) & 0xFFFFFF));       /* right edge of the scroll rect moved into x 2..10 */
    CHECK(pd_dib_px(&d, 5, 3) == (sd_px(3, 3) & 0xFFFFFF));
    CHECK(pd_dib_px(&d, 12, 3) == (sd_px(12, 3) & 0xFFFFFF));     /* beyond x 10: untouched */
    CHECK(pd_dib_px(&d, 15, 15) == (sd_px(15, 15) & 0xFFFFFF));
    pd_dib_free(&d);
    PD_END();
}
PT(scrolldc_dib_null_outputs_ok) {
    PD_BEGIN(); NEED_SCROLLDC();
    struct pd_dib d;
    if (!pd_dib_make(&d, SD_W, SD_H)) { t_skip("cannot create a DIB section"); return; }
    sd_fill(&d);
    RECT all = pd_rc(0, 0, SD_W, SD_H);
    CHECK(UN(NtUserScrollDC)(d.dc, 3, 0, &all, &all, NULL, NULL) == TRUE);
    CHECK(sd_moved_ok(&d, 3, 0, &all) == 0);
    pd_dib_free(&d);
    PD_END();
}
PT(scrolldc_dib_second_scroll_accumulates) {
    PD_BEGIN(); NEED_SCROLLDC();
    struct pd_dib d;
    if (!pd_dib_make(&d, SD_W, SD_H)) { t_skip("cannot create a DIB section"); return; }
    sd_fill(&d);
    RECT all = pd_rc(0, 0, SD_W, SD_H);
    CHECK(UN(NtUserScrollDC)(d.dc, 3, 0, &all, &all, NULL, NULL));
    CHECK(UN(NtUserScrollDC)(d.dc, 0, 5, &all, &all, NULL, NULL));
    CHECK(pd_dib_px(&d, 10, 10) == (sd_px(7, 5) & 0xFFFFFF));
    CHECK(pd_dib_px(&d, 15, 15) == (sd_px(12, 10) & 0xFFFFFF));
    pd_dib_free(&d);
    PD_END();
}
PT(scrolldc_user32_zero_offset_shortcut) {
    PD_BEGIN(); NEED_SCROLLDC(); REQUIRE(U(ScrollDC));
    struct pd_dib d;
    if (!pd_dib_make(&d, SD_W, SD_H)) { t_skip("cannot create a DIB section"); return; }
    sd_fill(&d);
    RECT all = pd_rc(0, 0, SD_W, SD_H), upd = pd_rc(3, 3, 7, 7), z = {0};
    HRGN rg = pd_rgn(1, 1, 9, 9);
    /* user32!ScrollDC: dx == dy == 0 -> SetRectRgn(hrgnUpdate, 0,0,0,0), zero *lprcUpdate, return TRUE; no syscall */
    CHECK(U(ScrollDC)(d.dc, 0, 0, &all, &all, rg, &upd) == TRUE);
    CHECK(pd_eq(&upd, &z));
    RECT b; CHECK(pd_rgn_box(rg, &b) == NULLREGION);
    CHECK(sd_moved_ok(&d, 0, 0, &all) == 0);
    CHECK(U(ScrollDC)(NULL, 0, 0, &all, &all, rg, &upd) == FALSE);
    pd_rgn_free(rg);
    pd_dib_free(&d);
    PD_END();
}
PT(scrolldc_user32_path_matches_syscall) {
    PD_BEGIN(); NEED_SCROLLDC(); REQUIRE(U(ScrollDC));
    struct pd_dib d;
    if (!pd_dib_make(&d, SD_W, SD_H)) { t_skip("cannot create a DIB section"); return; }
    sd_fill(&d);
    RECT all = pd_rc(0, 0, SD_W, SD_H), upd = {0}, e = pd_rc(0, 0, 4, 16);
    CHECK(U(ScrollDC)(d.dc, 4, 0, &all, &all, NULL, &upd));
    CHECK(pd_eq(&upd, &e));
    CHECK(sd_moved_ok(&d, 4, 0, &all) == 0);
    pd_dib_free(&d);
    PD_END();
}

/* ---- ScrollWindowEx ---- */
struct sw_row { const char *name; int dx, dy; int expect_type; RECT upd; };
static void run_sw(const struct sw_row *r) {
    PD_BEGIN(); NEED_RGN(); REQUIRE(UN(NtUserScrollWindowEx));
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    PD_NEED_EXPOSED(h);
    HRGN rg = pd_rgn(50, 50, 60, 60);
    RECT upd = pd_rc(-1, -1, -1, -1);
    int t = UN(NtUserScrollWindowEx)(h, r->dx, r->dy, NULL, NULL, rg, &upd, 0);
    CHECKF(t == r->expect_type, "%s: region type %d want %d", r->name, t, r->expect_type);
    CHECKF(pd_eq(&upd, &r->upd), "%s: update rect (%ld,%ld)-(%ld,%ld)", r->name, upd.left, upd.top, upd.right, upd.bottom);
    RECT b; pd_rgn_box(rg, &b);
    CHECKF(pd_eq(&b, &r->upd), "%s: region box (%ld,%ld)-(%ld,%ld)", r->name, b.left, b.top, b.right, b.bottom);
    CHECKF(UN(NtUserGetUpdateRect)(h, NULL, FALSE) == 0, "%s: window invalidated without SW_INVALIDATE", r->name);
    pd_rgn_free(rg);
    PD_END();
}
#define SW_ROW(id, dx, dy, type, l, t, r, b) \
    static const struct sw_row sw_##id = { #id, dx, dy, type, { l, t, r, b } }; \
    PT(scrollwindowex_##id) { run_sw(&sw_##id); }
SW_ROW(right10,    10,  0, SIMPLEREGION,  0,  0, 10,     PD_CH)
SW_ROW(left10,    -10,  0, SIMPLEREGION,  PD_CW - 10, 0, PD_CW, PD_CH)
SW_ROW(down7,       0,  7, SIMPLEREGION,  0, 0, PD_CW, 7)
SW_ROW(up7,         0, -7, SIMPLEREGION,  0, PD_CH - 7, PD_CW, PD_CH)
SW_ROW(diagonal,   10,  5, COMPLEXREGION, 0, 0, PD_CW, PD_CH)
#undef SW_ROW

PT(scrollwindowex_invalidate_flag_sets_update_region) {
    PD_BEGIN(); NEED_RGN(); REQUIRE(UN(NtUserScrollWindowEx));
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    PD_NEED_EXPOSED(h);
    RECT upd = {0}, got = {0}, e = pd_rc(0, 0, 10, PD_CH);
    int t = UN(NtUserScrollWindowEx)(h, 10, 0, NULL, NULL, NULL, &upd, SW_INVALIDATE);
    CHECK(t == SIMPLEREGION);
    CHECK(UN(NtUserGetUpdateRect)(h, &got, FALSE) == 1);
    CHECK(pd_eq(&got, &e));
    PD_END();
}
PT(scrollwindowex_scroll_rect_restricts_uncovered_area) {
    PD_BEGIN(); NEED_RGN(); REQUIRE(UN(NtUserScrollWindowEx));
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    PD_NEED_EXPOSED(h);
    RECT sc = pd_rc(0, 0, 50, 40), upd = {0}, e = pd_rc(0, 0, 10, 40);
    int t = UN(NtUserScrollWindowEx)(h, 10, 0, &sc, NULL, NULL, &upd, 0);
    CHECK(t == SIMPLEREGION);
    CHECK(pd_eq(&upd, &e));
    PD_END();
}
PT(scrollwindowex_scrollchildren_moves_child_windows) {
    PD_BEGIN(); NEED_RGN(); REQUIRE(UN(NtUserScrollWindowEx));
    HWND p = pd_top(PDC_PLAIN);
    REQUIRE(p);
    HWND c = pd_child(p, PDC_PLAIN, 0, 10, 10, 30, 30);
    REQUIRE(c);
    RECT r1, r2;
    POINT o1 = {0, 0}, o2 = {0, 0};
    U(ClientToScreen)(c, &o1);
    UN(NtUserScrollWindowEx)(p, 5, 3, NULL, NULL, NULL, NULL, 0);
    U(ClientToScreen)(c, &o2);
    CHECK(o2.x == o1.x && o2.y == o1.y);        /* children stay put without SW_SCROLLCHILDREN */
    UN(NtUserScrollWindowEx)(p, 5, 3, NULL, NULL, NULL, NULL, SW_SCROLLCHILDREN);
    o2.x = 0; o2.y = 0;                         /* ClientToScreen converts in place: start from the client origin again */
    U(ClientToScreen)(c, &o2);
    CHECK(o2.x == o1.x + 5); CHECK(o2.y == o1.y + 3);
    (void)r1; (void)r2;
    PD_END();
}
PT(scrollwindowex_bad_hwnd) {
    PD_BEGIN(); NEED_RGN(); REQUIRE(UN(NtUserScrollWindowEx));
    SET_ERR(0);
    CHECK(UN(NtUserScrollWindowEx)((HWND)(ULONG_PTR)0x12345678, 5, 0, NULL, NULL, NULL, NULL, 0) == 0);
    EXPECT_ERR(PD_ERR_INVALID_WINDOW_HANDLE);
    PD_END();
}
PT(scrollwindowex_zero_offset_note) {
    PD_BEGIN(); NEED_RGN(); REQUIRE(UN(NtUserScrollWindowEx));
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    RECT upd = pd_rc(5, 5, 6, 6);
    int t = UN(NtUserScrollWindowEx)(h, 0, 0, NULL, NULL, NULL, &upd, 0);
    t_note("ScrollWindowEx(0,0) = %d, update (%ld,%ld)-(%ld,%ld)", t, upd.left, upd.top, upd.right, upd.bottom);
    CHECK(UN(NtUserGetUpdateRect)(h, NULL, FALSE) == 0);
    PD_END();
}
