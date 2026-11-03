/* BeginPaint / EndPaint / WM_PAINT delivery / UpdateWindow / ExcludeUpdateRgn, observed through the win32u stubs
 * and through the window procedure of the fixture class (message order, PAINTSTRUCT contents, the paint DC). */
#include "pd.h"

#define NEED_PAINT() REQUIRE(UN(NtUserBeginPaint)); REQUIRE(UN(NtUserEndPaint)); REQUIRE(UN(NtUserInvalidateRect)); \
                     REQUIRE(UN(NtUserGetUpdateRect)); REQUIRE(UN(NtUserReleaseDC)); REQUIRE(UN(NtUserWindowFromDC)); \
                     REQUIRE(U(GetClipBox)); REQUIRE(U(CreateRectRgn)); REQUIRE(U(GetObjectType))

static BOOL upd(HWND h, RECT *r) { return UN(NtUserGetUpdateRect)(h, r, FALSE); }
static RECT client(HWND h) { RECT c; U(GetClientRect)(h, &c); return c; }

PT(beginpaint_fills_paintstruct) {
    PD_BEGIN(); NEED_PAINT();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    RECT in = pd_rc(4, 5, 20, 30);
    UN(NtUserInvalidateRect)(h, &in, TRUE);
    PAINTSTRUCT ps;
    memset(&ps, 0xCC, sizeof ps);
    HDC dc = UN(NtUserBeginPaint)(h, &ps);
    CHECK(dc != NULL);
    CHECK(ps.hdc == dc);                       /* NtUserBeginPaint copies the kernel PAINTSTRUCT out unchanged */
    CHECK(ps.fRestore == FALSE);               /* BeginPaint zeroes fRestore and fIncUpdate */
    CHECK(ps.fIncUpdate == FALSE);
    int rsv = 0;
    for (int i = 0; i < 32; i++) rsv |= ps.rgbReserved[i];
    CHECKF(rsv == 0, "rgbReserved not zeroed");   /* the 0x48-byte local was memset to 0 */
    CHECK(UN(NtUserEndPaint)(h, &ps) == 1);
    PD_END();
}
PT(beginpaint_writes_whole_0x48_bytes) {
    PD_BEGIN(); NEED_PAINT();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    UN(NtUserInvalidateRect)(h, NULL, FALSE);
    unsigned char buf[sizeof(PAINTSTRUCT) + 16];
    memset(buf, 0xCC, sizeof buf);
    HDC dc = UN(NtUserBeginPaint)(h, (PAINTSTRUCT *)buf);
    CHECK(dc != NULL);
    int tail = 0;
    for (int i = (int)sizeof(PAINTSTRUCT); i < (int)sizeof buf; i++) tail |= buf[i] != 0xCC;
    CHECKF(!tail, "bytes beyond PAINTSTRUCT were modified");
    int rsv = 0;
    for (int i = 0x24; i < 0x44; i++) rsv |= buf[i];
    CHECKF(rsv == 0, "rgbReserved not zeroed");
    CHECK(sizeof(PAINTSTRUCT) == 0x48);
    UN(NtUserEndPaint)(h, (PAINTSTRUCT *)buf);
    PD_END();
}
PT(beginpaint_clears_update_region) {
    PD_BEGIN(); NEED_PAINT();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    UN(NtUserInvalidateRect)(h, NULL, FALSE);
    CHECK(upd(h, NULL) == 1);
    PAINTSTRUCT ps;
    HDC dc = UN(NtUserBeginPaint)(h, &ps);
    CHECK(dc != NULL);
    CHECK(upd(h, NULL) == 0);                  /* BeginPaint: update region = 0 */
    ULONG_PTR v = 5;
    if (pd_hrgn_update(h, &v)) CHECK(v == 0);
    UN(NtUserEndPaint)(h, &ps);
    CHECK(upd(h, NULL) == 0);
    PD_END();
}
PT(beginpaint_rcpaint_is_invalid_rect_and_dc_clip) {
    PD_BEGIN(); NEED_PAINT();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    PD_NEED_EXPOSED(h);
    RECT in = pd_rc(4, 5, 20, 30), cb = {0};
    UN(NtUserInvalidateRect)(h, &in, FALSE);
    PAINTSTRUCT ps;
    HDC dc = UN(NtUserBeginPaint)(h, &ps);
    REQUIRE(dc);
    /* the DC is GetDCEx(h, hrgnUpdate, DCX_USESTYLE|DCX_INTERSECTRGN|..): its clip box is the update region box */
    CHECK(pd_eq(&ps.rcPaint, &in));
    CHECK(U(GetClipBox)(dc, &cb) == SIMPLEREGION);
    CHECK(pd_eq(&cb, &in));
    UN(NtUserEndPaint)(h, &ps);
    PD_END();
}
PT(beginpaint_two_rect_update_is_complex_clip) {
    PD_BEGIN(); NEED_PAINT();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    PD_NEED_EXPOSED(h);
    RECT a = pd_rc(0, 0, 10, 10), b = pd_rc(30, 30, 40, 40), bb = pd_rc(0, 0, 40, 40), cb = {0};
    UN(NtUserInvalidateRect)(h, &a, FALSE);
    UN(NtUserInvalidateRect)(h, &b, FALSE);
    PAINTSTRUCT ps;
    HDC dc = UN(NtUserBeginPaint)(h, &ps);
    REQUIRE(dc);
    CHECK(pd_eq(&ps.rcPaint, &bb));
    CHECK(U(GetClipBox)(dc, &cb) == COMPLEXREGION);
    CHECK(U(RectVisible)(dc, &a)); CHECK(U(RectVisible)(dc, &b));
    RECT gap = pd_rc(15, 15, 25, 25);
    CHECK(!U(RectVisible)(dc, &gap));
    UN(NtUserEndPaint)(h, &ps);
    PD_END();
}
PT(beginpaint_without_invalid_has_empty_paint_rect) {
    PD_BEGIN(); NEED_PAINT();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    PD_NEED_EXPOSED(h);
    PAINTSTRUCT ps;
    HDC dc = UN(NtUserBeginPaint)(h, &ps);
    REQUIRE(dc);
    CHECK(pd_eq(&ps.rcPaint, &(RECT){0, 0, 0, 0}));   /* nothing invalid: an empty paint rectangle ... */
    { RECT cb; CHECK(U(GetClipBox)(dc, &cb) == NULLREGION); }   /* ... and an empty clip region on the DC */
    CHECK(ps.fErase == FALSE);
    UN(NtUserEndPaint)(h, &ps);
    PD_END();
}
PT(beginpaint_rcpaint_within_client_always) {
    PD_BEGIN(); NEED_PAINT();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    RECT in = pd_rc(-10, -10, 20, 20);
    UN(NtUserInvalidateRect)(h, &in, FALSE);
    PAINTSTRUCT ps;
    HDC dc = UN(NtUserBeginPaint)(h, &ps);
    REQUIRE(dc);
    RECT c = client(h), x;
    CHECK(U(IntersectRect)(&x, &ps.rcPaint, &c) || (ps.rcPaint.right == ps.rcPaint.left));
    CHECK(ps.rcPaint.left >= c.left && ps.rcPaint.top >= c.top && ps.rcPaint.right <= c.right && ps.rcPaint.bottom <= c.bottom);
    UN(NtUserEndPaint)(h, &ps);
    PD_END();
}

/* ---- the paint DC ---- */
PT(paint_dc_is_cache_dc_of_the_window) {
    PD_BEGIN(); NEED_PAINT();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    pd_beh = PB_PAINT_BP;
    UN(NtUserInvalidateRect)(h, NULL, FALSE);
    pd_pump(40, 30);
    REQUIRE(pd_obs.valid);
    CHECK(pd_obs.hdc_ret != NULL);
    CHECK(pd_obs.wfd == h);                    /* _WindowFromDC -> the DCE's window */
    CHECK(pd_obs.objtype == OBJ_DC);
    /* the cell is read inside the handler: once EndPaint released the DC its cell is reset (owner and attribute page cleared) */
    if (pd_obs.cell_ok) {
        struct pd_cell c = pd_obs.cell;
        CHECK(c.objt == 1);
        CHECK(PD_TYPE7(pd_obs.hdc_ret) == 1);
        CHECK(c.puser != 0);                   /* DC_ATTR page */
        CHECK(c.q0 >= PD_KERNEL_FLOOR);
    } else t_note("GDI cell for the paint DC unreadable");
    CHECK(pd_obs.end_ret == TRUE);
    PD_END();
}
PT(paint_dc_released_by_endpaint) {
    PD_BEGIN(); NEED_PAINT();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    UN(NtUserInvalidateRect)(h, NULL, FALSE);
    PAINTSTRUCT ps;
    HDC dc = UN(NtUserBeginPaint)(h, &ps);
    REQUIRE(dc);
    CHECK(UN(NtUserEndPaint)(h, &ps) == 1);    /* ReleaseCacheDC(hdc, 1) */
    /* the DCE is no longer busy: ReleaseCacheDC returns 2 -> NtUserReleaseDC returns 0 */
    CHECK(pd_nt_release(dc) == 0);
    PD_END();
}
PT(paint_dc_can_be_released_early_with_releasedc) {
    PD_BEGIN(); NEED_PAINT();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    UN(NtUserInvalidateRect)(h, NULL, FALSE);
    PAINTSTRUCT ps;
    HDC dc = UN(NtUserBeginPaint)(h, &ps);
    REQUIRE(dc);
    CHECK(pd_nt_release(dc) == 1);       /* busy cache DCE: released */
    CHECK(UN(NtUserEndPaint)(h, &ps) == 1);    /* still fine afterwards */
    CHECK(pd_nt_release(dc) == 0);
    PD_END();
}
PT(paint_dc_windowfromdc_inside_paint) {
    PD_BEGIN(); NEED_PAINT();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    UN(NtUserInvalidateRect)(h, NULL, FALSE);
    PAINTSTRUCT ps;
    HDC dc = UN(NtUserBeginPaint)(h, &ps);
    REQUIRE(dc);
    CHECK(UN(NtUserWindowFromDC)(dc) == h);
    CHECK(U(WindowFromDC) == NULL || U(WindowFromDC)(dc) == h);
    UN(NtUserEndPaint)(h, &ps);
    PD_END();
}
PT(erase_message_wparam_is_the_paint_dc) {
    PD_BEGIN(); NEED_PAINT();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    PD_NEED_EXPOSED(h);
    pd_beh = PB_PAINT_BP | PB_ERASE_ZERO;
    UN(NtUserInvalidateRect)(h, NULL, TRUE);
    pd_pump(40, 30);
    REQUIRE(pd_obs.valid);
    REQUIRE(pd_eobs.valid);
    CHECK(pd_eobs.hdc == pd_obs.hdc_ret);      /* the erase message is handed BeginPaint's own DC */
    CHECK(pd_eobs.wfd == h);
    CHECK(pd_eobs.objtype == OBJ_DC);
    PD_END();
}
PT(wm_paint_handler_sees_update_rect_before_and_none_after_beginpaint) {
    PD_BEGIN(); NEED_PAINT();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    pd_beh = PB_PAINT_BP;
    RECT in = pd_rc(7, 8, 17, 18);
    UN(NtUserInvalidateRect)(h, &in, FALSE);
    pd_pump(40, 30);
    REQUIRE(pd_obs.valid);
    CHECK(pd_obs.upd_before == TRUE);
    CHECK(pd_eq(&pd_obs.upd_rect_before, &in));
    CHECK(pd_obs.upd_after == FALSE);
    PD_END();
}

/* ---- fErase ---- */
struct ferase_row { const char *name; enum pd_class cls; unsigned beh; BOOL inv_erase; BOOL want; };
static void run_ferase(const struct ferase_row *r) {
    PD_BEGIN(); NEED_PAINT();
    HWND h = pd_top(r->cls);
    REQUIRE(h);
    PD_NEED_EXPOSED(h);          /* WM_ERASEBKGND is only sent when the paint DC has a non-empty clip box (BeginPaint) */
    pd_beh = PB_PAINT_BP | r->beh;
    UN(NtUserInvalidateRect)(h, NULL, r->inv_erase);
    pd_pump(40, 30);
    REQUIRE(pd_obs.valid);
    CHECKF(pd_obs.ps.fErase == r->want, "%s: fErase=%d want %d", r->name, pd_obs.ps.fErase, r->want);
    CHECKF(pd_count(h, WM_ERASEBKGND) == (r->inv_erase ? 1 : 0), "%s: %d WM_ERASEBKGND", r->name, pd_count(h, WM_ERASEBKGND));
    PD_END();
}
#define FE_ROW(id, cls, beh, ie, want) \
    static const struct ferase_row fe_##id = { #id, cls, beh, ie, want }; \
    PT(ferase_##id) { run_ferase(&fe_##id); }
FE_ROW(class_brush_erase_handled,       PDC_PLAIN, 0,             TRUE,  FALSE)   /* DefWindowProc fills with hbrBackground and returns 1 */
FE_ROW(class_brush_no_erase_requested,  PDC_PLAIN, 0,             FALSE, FALSE)
FE_ROW(no_brush_erase_not_handled,      PDC_NOBG,  0,             TRUE,  TRUE)    /* DefWindowProc returns 0 without a class brush -> app must erase */
FE_ROW(no_brush_no_erase_requested,     PDC_NOBG,  0,             FALSE, TRUE)     /* no class brush: fErase stays TRUE although no WM_ERASEBKGND was requested or sent */
FE_ROW(handler_returns_zero,            PDC_PLAIN, PB_ERASE_ZERO, TRUE,  TRUE)
FE_ROW(handler_returns_one,             PDC_NOBG,  PB_ERASE_FILL, TRUE,  FALSE)
FE_ROW(owndc_class_brush,               PDC_OWN,   0,             TRUE,  FALSE)
FE_ROW(classdc_no_erase,                PDC_CLASS, 0,             FALSE, FALSE)

PT(second_beginpaint_without_invalidation_has_no_erase) {
    PD_BEGIN(); NEED_PAINT();
    HWND h = pd_top(PDC_NOBG);
    REQUIRE(h);
    PD_NEED_EXPOSED(h);
    UN(NtUserInvalidateRect)(h, NULL, TRUE);
    PAINTSTRUCT ps;
    UN(NtUserBeginPaint)(h, &ps);
    UN(NtUserEndPaint)(h, &ps);
    UN(NtUserBeginPaint)(h, &ps);
    t_note("second BeginPaint fErase=%d", ps.fErase);
    CHECK(upd(h, NULL) == 0);
    UN(NtUserEndPaint)(h, &ps);
    PD_END();
}

/* ---- failures ---- */
PT(beginpaint_bad_hwnd_returns_null_and_leaves_ps) {
    PD_BEGIN(); NEED_PAINT();
    PAINTSTRUCT ps;
    memset(&ps, 0xCC, sizeof ps);
    SET_ERR(0);
    CHECK(UN(NtUserBeginPaint)((HWND)(ULONG_PTR)0x12345678, &ps) == NULL);
    EXPECT_ERR(PD_ERR_INVALID_WINDOW_HANDLE);
    unsigned char *b = (unsigned char *)&ps;
    int untouched = 1;
    for (unsigned i = 0; i < sizeof ps; i++) if (b[i] != 0xCC) untouched = 0;
    CHECK(untouched);                          /* the copy-out is inside the "window validated" branch */
    PD_END();
}
PT(beginpaint_destroyed_hwnd) {
    PD_BEGIN(); NEED_PAINT();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    U(DestroyWindow)(h);
    PAINTSTRUCT ps;
    SET_ERR(0);
    CHECK(UN(NtUserBeginPaint)(h, &ps) == NULL);
    EXPECT_ERR(PD_ERR_INVALID_WINDOW_HANDLE);
    PD_END();
}
PT(endpaint_bad_hwnd_returns_zero) {
    PD_BEGIN(); NEED_PAINT();
    PAINTSTRUCT ps;
    memset(&ps, 0, sizeof ps);
    SET_ERR(0);
    CHECK(UN(NtUserEndPaint)((HWND)(ULONG_PTR)0x12345678, &ps) == 0);
    EXPECT_ERR(PD_ERR_INVALID_WINDOW_HANDLE);
    PD_END();
}
PT(endpaint_unknown_hdc_succeeds_with_dc_not_found_error) {
    PD_BEGIN(); NEED_PAINT();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    PAINTSTRUCT ps;
    memset(&ps, 0, sizeof ps);
    ps.hdc = (HDC)(ULONG_PTR)0x00010123;
    SET_ERR(0);
    /* EndPaint ignores ReleaseCacheDC's verdict and always returns 1; ReleaseCacheDC set ERROR_DC_NOT_FOUND */
    CHECK(UN(NtUserEndPaint)(h, &ps) == 1);
    EXPECT_ERR(PD_ERR_DC_NOT_FOUND);
    PD_END();
}
PT(endpaint_twice_succeeds_both_times) {
    PD_BEGIN(); NEED_PAINT();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    UN(NtUserInvalidateRect)(h, NULL, FALSE);
    PAINTSTRUCT ps;
    UN(NtUserBeginPaint)(h, &ps);
    CHECK(UN(NtUserEndPaint)(h, &ps) == 1);
    CHECK(UN(NtUserEndPaint)(h, &ps) == 1);
    PD_END();
}
PT(endpaint_without_beginpaint_on_valid_dc_does_not_release_unrelated_dc) {
    PD_BEGIN(); NEED_PAINT();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    HDC dc = U(GetDC)(h);
    REQUIRE(dc);
    PAINTSTRUCT ps;
    memset(&ps, 0, sizeof ps);
    ps.hdc = dc;
    /* ReleaseCacheDC(hdc, 1) releases any busy cache DC handle it is given, EndPaint does not check it came from BeginPaint */
    CHECK(UN(NtUserEndPaint)(h, &ps) == 1);
    CHECK(pd_nt_release(dc) == 0);
    PD_END();
}

/* ---- WM_PAINT delivery ---- */
PT(defwindowproc_paint_validates_and_erases_once) {
    PD_BEGIN(); NEED_PAINT();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    UN(NtUserInvalidateRect)(h, NULL, TRUE);
    pd_pump(40, 30);
    CHECK(pd_count(h, WM_PAINT) == 1);
    CHECK(upd(h, NULL) == 0);
    if (pd_exposed(h)) CHECK(pd_count(h, WM_ERASEBKGND) == 1);
    PD_END();
}
PT(wm_paint_not_validated_repeats) {
    PD_BEGIN(); NEED_PAINT();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    pd_beh = PB_PAINT_NONE;
    UN(NtUserInvalidateRect)(h, NULL, FALSE);
    MSG m; int got = 0;
    for (int i = 0; i < 3; i++) {
        if (U(PeekMessageA)(&m, h, WM_PAINT, WM_PAINT, PM_REMOVE)) { U(DispatchMessageA)(&m); got++; }
    }
    CHECK(got == 3);                           /* still invalid, so WM_PAINT is available again each time */
    CHECK(pd_count(h, WM_PAINT) == 3);
    CHECK(upd(h, NULL) == 1);
    pd_beh = 0;
    PD_END();
}
PT(wm_paint_validaterect_only_does_not_erase) {
    PD_BEGIN(); NEED_PAINT();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    pd_beh = PB_PAINT_VALIDATE;
    UN(NtUserInvalidateRect)(h, NULL, TRUE);
    pd_pump(40, 30);
    CHECK(pd_count(h, WM_PAINT) == 1);
    CHECK(pd_count(h, WM_ERASEBKGND) == 0);
    CHECK(upd(h, NULL) == 0);
    pd_beh = 0;
    PD_END();
}
PT(wm_paint_wparam_lparam_zero) {
    PD_BEGIN(); NEED_PAINT();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    pd_beh = PB_PAINT_BP;
    UN(NtUserInvalidateRect)(h, NULL, FALSE);
    pd_pump(40, 30);
    int i = pd_find(h, WM_PAINT, 0);
    REQUIRE(i >= 0);
    CHECK(pd_log[i].w == 0); CHECK(pd_log[i].l == 0);
    PD_END();
}
PT(peek_wm_paint_noremove_and_fields) {
    PD_BEGIN(); NEED_PAINT();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    MSG m;
    CHECK(!pd_peek_paint(h, &m, 0));            /* nothing invalid -> no WM_PAINT */
    UN(NtUserInvalidateRect)(h, NULL, FALSE);
    memset(&m, 0, sizeof m);
    CHECK(pd_peek_paint(h, &m, 0));
    CHECK(m.hwnd == h); CHECK(m.message == WM_PAINT); CHECK(m.wParam == 0); CHECK(m.lParam == 0);
    CHECK(pd_peek_paint(h, &m, 0));             /* NOREMOVE: still there */
    pd_beh = PB_PAINT_VALIDATE;
    CHECK(pd_peek_paint(h, &m, 1));
    U(DispatchMessageA)(&m);
    CHECK(!pd_peek_paint(h, &m, 0));
    pd_beh = 0;
    PD_END();
}
PT(peek_wm_paint_filtered_by_window) {
    PD_BEGIN(); NEED_PAINT();
    HWND a = pd_top(PDC_PLAIN), b = pd_top(PDC_PLAIN);
    REQUIRE(a); REQUIRE(b);
    UN(NtUserInvalidateRect)(a, NULL, FALSE);
    MSG m;
    CHECK(pd_peek_paint(a, &m, 0));
    CHECK(!pd_peek_paint(b, &m, 0));
    PD_END();
}
PT(wm_paint_is_lowest_priority_message) {
    PD_BEGIN(); NEED_PAINT();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    UN(NtUserInvalidateRect)(h, NULL, FALSE);
    U(PostMessageA)(h, WM_APP + 1, 0, 0);
    MSG m;
    CHECK(U(PeekMessageA)(&m, NULL, 0, 0, PM_REMOVE));
    CHECK(m.message == WM_APP + 1);             /* posted messages come before paint */
    pd_beh = PB_PAINT_VALIDATE;
    CHECK(U(PeekMessageA)(&m, NULL, 0, 0, PM_REMOVE));
    CHECK(m.message == WM_PAINT);
    U(DispatchMessageA)(&m);
    pd_beh = 0;
    PD_END();
}
PT(qs_paint_follows_update_regions) {
    PD_BEGIN(); NEED_PAINT();
    HWND a = pd_top(PDC_PLAIN), b = pd_top(PDC_PLAIN);
    REQUIRE(a); REQUIRE(b);
    CHECK(pd_qs_paint() == 0);
    UN(NtUserInvalidateRect)(a, NULL, FALSE);
    CHECK(pd_qs_paint() == QS_PAINT);
    UN(NtUserInvalidateRect)(b, NULL, FALSE);
    CHECK(pd_qs_paint() == QS_PAINT);
    UN(NtUserValidateRect)(a, NULL);
    CHECK(pd_qs_paint() == QS_PAINT);           /* b still needs painting */
    UN(NtUserValidateRect)(b, NULL);
    CHECK(pd_qs_paint() == 0);
    PD_END();
}
PT(qs_paint_cleared_by_beginpaint) {
    PD_BEGIN(); NEED_PAINT();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    UN(NtUserInvalidateRect)(h, NULL, FALSE);
    CHECK(pd_qs_paint() == QS_PAINT);
    PAINTSTRUCT ps;
    UN(NtUserBeginPaint)(h, &ps);
    CHECK(pd_qs_paint() == 0);                  /* BeginPaint: the pending-paint count is decremented and QS_PAINT cleared at zero */
    UN(NtUserEndPaint)(h, &ps);
    PD_END();
}
PT(qs_paint_set_by_internalpaint) {
    PD_BEGIN(); NEED_PAINT(); REQUIRE(UN(NtUserRedrawWindow));
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    UN(NtUserRedrawWindow)(h, NULL, NULL, RDW_INTERNALPAINT);
    CHECK(pd_qs_paint() == QS_PAINT);
    UN(NtUserRedrawWindow)(h, NULL, NULL, RDW_NOINTERNALPAINT);
    CHECK(pd_qs_paint() == 0);
    PD_END();
}
PT(showwindow_invalidates_whole_client) {
    PD_BEGIN(); NEED_PAINT();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    U(ShowWindow)(h, SW_HIDE);
    pd_settle(h);
    U(ShowWindow)(h, SW_SHOWNOACTIVATE);
    RECT r = {0}, c = client(h);
    CHECK(upd(h, &r) == 1);
    CHECK(pd_eq(&r, &c));
    PD_END();
}

/* ---- UpdateWindow ---- */
PT(updatewindow_sends_wm_paint_synchronously) {
    PD_BEGIN(); NEED_PAINT(); REQUIRE(U(UpdateWindow)); REQUIRE(U(InvalidateRect));
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    pd_beh = PB_PAINT_BP;
    U(InvalidateRect)(h, NULL, FALSE);
    CHECK(pd_count(h, WM_PAINT) == 0);
    CHECK(U(UpdateWindow)(h));
    CHECK(pd_count(h, WM_PAINT) == 1);          /* delivered before UpdateWindow returned, queue never pumped */
    CHECK(upd(h, NULL) == 0);
    pd_beh = 0;
    PD_END();
}
PT(updatewindow_clean_window_sends_nothing) {
    PD_BEGIN(); NEED_PAINT(); REQUIRE(U(UpdateWindow));
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    pd_beh = PB_PAINT_BP;
    CHECK(U(UpdateWindow)(h) == TRUE);
    CHECK(pd_count(h, WM_PAINT) == 0);
    pd_beh = 0;
    PD_END();
}
PT(updatewindow_bad_hwnd_fails) {
    PD_BEGIN(); REQUIRE(U(UpdateWindow));
    SET_ERR(0);
    CHECK(U(UpdateWindow)((HWND)(ULONG_PTR)0x12345678) == FALSE);
    EXPECT_ERR(PD_ERR_INVALID_WINDOW_HANDLE);
    PD_END();
}
PT(callhwndlock_0x73_is_updatewindow) {
    PD_BEGIN(); NEED_PAINT(); REQUIRE(UN(NtUserCallHwndLock));
    if (t_is_wine()) { t_skip("win32u.NtUserCallHwndLock is an aborting stub in Wine"); return; }
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    pd_beh = PB_PAINT_BP;
    UN(NtUserInvalidateRect)(h, NULL, FALSE);
    CHECK(UN(NtUserCallHwndLock)(h, 0x73) != 0);
    CHECK(pd_count(h, WM_PAINT) == 1);
    CHECK(upd(h, NULL) == 0);
    pd_beh = 0;
    PD_END();
}
PT(updatewindow_internalpaint_only_delivers_paint) {
    PD_BEGIN(); NEED_PAINT(); REQUIRE(U(UpdateWindow)); REQUIRE(UN(NtUserRedrawWindow));
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    pd_beh = PB_PAINT_BP;
    UN(NtUserRedrawWindow)(h, NULL, NULL, RDW_INTERNALPAINT);
    CHECK(U(UpdateWindow)(h));
    CHECK(pd_count(h, WM_PAINT) == 1);          /* user32 forwards when the internal-paint flag (+0x11 & 0x10) is set */
    pd_beh = 0;
    PD_END();
}
PT(updatewindow_parent_paints_invalid_child) {
    PD_BEGIN(); NEED_PAINT(); REQUIRE(U(UpdateWindow));
    HWND p = pd_top(PDC_PLAIN);
    REQUIRE(p);
    HWND c = pd_child(p, PDC_PLAIN, 0, 10, 10, 30, 30);
    REQUIRE(c);
    pd_beh = PB_PAINT_BP;
    UN(NtUserInvalidateRect)(c, NULL, FALSE);
    CHECK(upd(p, NULL) == 0);
    CHECK(U(UpdateWindow)(p));
    CHECK(pd_count(c, WM_PAINT) == 1);
    CHECK(upd(c, NULL) == 0);
    pd_beh = 0;
    PD_END();
}

/* ---- ExcludeUpdateRgn ---- */
PT(excludeupdatergn_whole_invalid_empties_clip) {
    PD_BEGIN(); NEED_PAINT(); REQUIRE(UN(NtUserExcludeUpdateRgn));
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    PD_NEED_EXPOSED(h);
    HDC dc = U(GetDC)(h);
    REQUIRE(dc);
    UN(NtUserInvalidateRect)(h, NULL, FALSE);
    RECT cb;
    int r = UN(NtUserExcludeUpdateRgn)(dc, h);
    /* the whole client is invalid, so nothing is left of the DC's clip (checked below); the call itself reports COMPLEXREGION */
    CHECK(r == COMPLEXREGION);
    CHECK(U(GetClipBox)(dc, &cb) == NULLREGION);
    U(ReleaseDC)(h, dc);
    PD_END();
}
PT(excludeupdatergn_partial_invalid_excludes_that_area) {
    PD_BEGIN(); NEED_PAINT(); REQUIRE(UN(NtUserExcludeUpdateRgn));
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    PD_NEED_EXPOSED(h);
    HDC dc = U(GetDC)(h);
    REQUIRE(dc);
    RECT in = pd_rc(10, 10, 30, 30);
    UN(NtUserInvalidateRect)(h, &in, FALSE);
    int r = UN(NtUserExcludeUpdateRgn)(dc, h);
    CHECK(r == COMPLEXREGION);
    CHECK(!U(RectVisible)(dc, &in));
    RECT out = pd_rc(40, 40, 50, 50), edge = pd_rc(0, 0, 5, 5);
    CHECK(U(RectVisible)(dc, &out)); CHECK(U(RectVisible)(dc, &edge));
    U(ReleaseDC)(h, dc);
    PD_END();
}
PT(excludeupdatergn_clean_window_keeps_clip) {
    PD_BEGIN(); NEED_PAINT(); REQUIRE(UN(NtUserExcludeUpdateRgn));
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    PD_NEED_EXPOSED(h);
    HDC dc = U(GetDC)(h);
    REQUIRE(dc);
    RECT cb, c = client(h);
    int r = UN(NtUserExcludeUpdateRgn)(dc, h);
    CHECK(r == SIMPLEREGION);                   /* returns the clip-box result unchanged */
    CHECK(U(GetClipBox)(dc, &cb) == SIMPLEREGION); CHECK(pd_eq(&cb, &c));
    U(ReleaseDC)(h, dc);
    PD_END();
}
PT(excludeupdatergn_null_dc_zero) {
    PD_BEGIN(); NEED_PAINT(); REQUIRE(UN(NtUserExcludeUpdateRgn));
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    CHECK(UN(NtUserExcludeUpdateRgn)(NULL, h) == 0);   /* NtUserExcludeUpdateRgn: hdc == 0 short-circuits */
    PD_END();
}
PT(excludeupdatergn_bad_hwnd_zero) {
    PD_BEGIN(); NEED_PAINT(); REQUIRE(UN(NtUserExcludeUpdateRgn));
    HDC dc = U(GetDC)(NULL);
    REQUIRE(dc);
    SET_ERR(0);
    CHECK(UN(NtUserExcludeUpdateRgn)(dc, (HWND)(ULONG_PTR)0x12345678) == 0);
    EXPECT_ERR(PD_ERR_INVALID_WINDOW_HANDLE);
    U(ReleaseDC)(NULL, dc);
    PD_END();
}
PT(excludeupdatergn_user32_matches_syscall) {
    PD_BEGIN(); NEED_PAINT(); REQUIRE(U(ExcludeUpdateRgn));
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    PD_NEED_EXPOSED(h);
    HDC dc = U(GetDC)(h);
    REQUIRE(dc);
    RECT in = pd_rc(10, 10, 30, 30);
    UN(NtUserInvalidateRect)(h, &in, FALSE);
    CHECK(U(ExcludeUpdateRgn)(dc, h) == COMPLEXREGION);
    CHECK(!U(RectVisible)(dc, &in));
    U(ReleaseDC)(h, dc);
    PD_END();
}

/* ---- two windows, counts ---- */
PT(two_windows_each_get_one_wm_paint) {
    PD_BEGIN(); NEED_PAINT();
    HWND a = pd_top(PDC_PLAIN), b = pd_top(PDC_NOBG);
    REQUIRE(a); REQUIRE(b);
    UN(NtUserInvalidateRect)(a, NULL, FALSE);
    UN(NtUserInvalidateRect)(b, NULL, FALSE);
    pd_pump(60, 40);
    CHECK(pd_count(a, WM_PAINT) == 1);
    CHECK(pd_count(b, WM_PAINT) == 1);
    CHECK(upd(a, NULL) == 0); CHECK(upd(b, NULL) == 0);
    PD_END();
}
PT(invalidate_after_paint_paints_again) {
    PD_BEGIN(); NEED_PAINT();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    for (int i = 1; i <= 3; i++) {
        UN(NtUserInvalidateRect)(h, NULL, FALSE);
        pd_pump(40, 20);
        CHECKF(pd_count(h, WM_PAINT) == i, "round %d: %d WM_PAINT", i, pd_count(h, WM_PAINT));
    }
    PD_END();
}
