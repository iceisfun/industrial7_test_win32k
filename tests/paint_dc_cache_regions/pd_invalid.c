/* Invalidation / validation / update-region queries through the win32u stubs
 * (NtUserInvalidateRect, NtUserInvalidateRgn, NtUserValidateRect, NtUserRedrawWindow,
 * NtUserGetUpdateRect, NtUserGetUpdateRgn), validated against the window's own hrgnUpdate field. */
#include "pd.h"

#define NEED_INV() REQUIRE(UN(NtUserInvalidateRect)); REQUIRE(UN(NtUserGetUpdateRect)); REQUIRE(UN(NtUserValidateRect)); \
                   REQUIRE(UN(NtUserRedrawWindow)); REQUIRE(UN(NtUserGetUpdateRgn)); REQUIRE(UN(NtUserInvalidateRgn)); \
                   REQUIRE(U(CreateRectRgn)); REQUIRE(U(GetRgnBox)); REQUIRE(U(PtInRegion)); REQUIRE(U(DeleteObject))

static BOOL upd(HWND h, RECT *r) { return UN(NtUserGetUpdateRect)(h, r, FALSE); }
static RECT client(HWND h) { RECT c; U(GetClientRect)(h, &c); return c; }

PT(invalidate_rect_null_marks_whole_client) {
    PD_BEGIN(); NEED_INV();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    RECT r = pd_rc(-1, -1, -1, -1), c = client(h);
    /* NtUserInvalidateRect -> RedrawWindow(hwnd, NULL, 0, RDW_INVALIDATE) */
    CHECK(UN(NtUserInvalidateRect)(h, NULL, FALSE) == 1);
    CHECK(upd(h, &r) == 1);
    CHECK(pd_eq(&r, &c));
    ULONG_PTR v = 0;
    if (pd_hrgn_update(h, &v)) { CHECK(v != 0); t_note("hrgnUpdate after InvalidateRect(NULL) = %p", (void *)v); }
    PD_END();
}
PT(invalidate_rect_subrect_exact) {
    PD_BEGIN(); NEED_INV();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    RECT in = pd_rc(4, 5, 20, 30), r = {0};
    CHECK(UN(NtUserInvalidateRect)(h, &in, FALSE));
    CHECK(upd(h, &r));
    CHECK(pd_eq(&r, &in));
    HRGN rg = pd_rgn(0, 0, 1, 1);
    CHECK(UN(NtUserGetUpdateRgn)(h, rg, FALSE) == SIMPLEREGION);
    RECT b; pd_rgn_box(rg, &b);
    CHECK(pd_eq(&b, &in));
    pd_rgn_free(rg);
    PD_END();
}
PT(invalidate_rect_clipped_to_client) {
    PD_BEGIN(); NEED_INV();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    RECT in = pd_rc(-50, -60, 10000, 9000), r = {0}, c = client(h);
    CHECK(UN(NtUserInvalidateRect)(h, &in, FALSE));
    CHECK(upd(h, &r));
    /* GetUpdateRect intersects the region box with the window's rcClient */
    CHECK(pd_eq(&r, &c));
    PD_END();
}
PT(invalidate_rect_outside_client_not_invalid) {
    PD_BEGIN(); NEED_INV();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    RECT in = pd_rc(PD_CW + 10, PD_CH + 10, PD_CW + 40, PD_CH + 40), r = {1, 1, 1, 1};
    UN(NtUserInvalidateRect)(h, &in, FALSE);
    BOOL got = upd(h, &r);
    t_note("GetUpdateRect after invalidating a rect outside the client = %d", got);
    if (got) { RECT e = pd_rc(0, 0, 0, 0); CHECK(pd_eq(&r, &e) || r.right <= r.left || r.bottom <= r.top); }
    PD_END();
}
PT(invalidate_two_rects_union_bbox_and_complex) {
    PD_BEGIN(); NEED_INV();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    RECT a = pd_rc(0, 0, 10, 10), b = pd_rc(20, 20, 30, 30), r = {0}, bb = pd_rc(0, 0, 30, 30);
    UN(NtUserInvalidateRect)(h, &a, FALSE);
    UN(NtUserInvalidateRect)(h, &b, FALSE);
    CHECK(upd(h, &r)); CHECK(pd_eq(&r, &bb));
    HRGN rg = pd_rgn(0, 0, 1, 1);
    CHECK(UN(NtUserGetUpdateRgn)(h, rg, FALSE) == COMPLEXREGION);
    CHECK(U(PtInRegion)(rg, 5, 5)); CHECK(!U(PtInRegion)(rg, 15, 15)); CHECK(U(PtInRegion)(rg, 25, 25));
    pd_rgn_free(rg);
    PD_END();
}
PT(invalidate_adjacent_rects_merge_to_simple) {
    PD_BEGIN(); NEED_INV();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    RECT a = pd_rc(0, 0, 10, 10), b = pd_rc(10, 0, 20, 10);
    UN(NtUserInvalidateRect)(h, &a, FALSE);
    UN(NtUserInvalidateRect)(h, &b, FALSE);
    HRGN rg = pd_rgn(0, 0, 1, 1);
    /* CombineRgn(.., RGN_OR) of two touching rectangles normalises to one rectangle: SIMPLEREGION */
    CHECK(UN(NtUserGetUpdateRgn)(h, rg, FALSE) == SIMPLEREGION);
    RECT bx, e = pd_rc(0, 0, 20, 10); pd_rgn_box(rg, &bx); CHECK(pd_eq(&bx, &e));
    pd_rgn_free(rg);
    PD_END();
}
PT(invalidate_overlapping_rects_l_shape_complex) {
    PD_BEGIN(); NEED_INV();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    RECT a = pd_rc(0, 0, 10, 10), b = pd_rc(5, 5, 15, 15);
    UN(NtUserInvalidateRect)(h, &a, FALSE);
    UN(NtUserInvalidateRect)(h, &b, FALSE);
    HRGN rg = pd_rgn(0, 0, 1, 1);
    CHECK(UN(NtUserGetUpdateRgn)(h, rg, FALSE) == COMPLEXREGION);
    CHECK(U(PtInRegion)(rg, 2, 2)); CHECK(U(PtInRegion)(rg, 12, 12)); CHECK(!U(PtInRegion)(rg, 12, 2)); CHECK(!U(PtInRegion)(rg, 2, 12));
    pd_rgn_free(rg);
    PD_END();
}
PT(invalidate_rect_user32_path_matches_syscall) {
    PD_BEGIN(); NEED_INV(); REQUIRE(U(InvalidateRect)); REQUIRE(U(GetUpdateRect));
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    RECT in = pd_rc(3, 4, 33, 44), r1 = {0}, r2 = {0};
    CHECK(U(InvalidateRect)(h, &in, FALSE));
    CHECK(U(GetUpdateRect)(h, &r1, FALSE));
    CHECK(upd(h, &r2));
    CHECK(pd_eq(&r1, &r2)); CHECK(pd_eq(&r1, &in));
    PD_END();
}
PT(validate_rect_removes_corner_leaves_complex) {
    PD_BEGIN(); NEED_INV();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    RECT corner = pd_rc(0, 0, 10, 10);
    UN(NtUserInvalidateRect)(h, NULL, FALSE);
    CHECK(UN(NtUserValidateRect)(h, &corner) == 1);
    HRGN rg = pd_rgn(0, 0, 1, 1);
    CHECK(UN(NtUserGetUpdateRgn)(h, rg, FALSE) == COMPLEXREGION);
    CHECK(!U(PtInRegion)(rg, 5, 5)); CHECK(U(PtInRegion)(rg, 50, 50));
    pd_rgn_free(rg);
    PD_END();
}
PT(validate_rect_null_clears_everything) {
    PD_BEGIN(); NEED_INV();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    UN(NtUserInvalidateRect)(h, NULL, FALSE);
    CHECK(UN(NtUserValidateRect)(h, NULL) == 1);
    RECT r = {5, 5, 5, 5};
    CHECK(upd(h, &r) == 0);
    RECT z = {0};
    CHECK(pd_eq(&r, &z));   /* GetUpdateRect zeroes the output when hrgnUpdate == 0 */
    ULONG_PTR v = 7;
    if (pd_hrgn_update(h, &v)) CHECK(v == 0);
    PD_END();
}
PT(validate_rect_whole_client_clears) {
    PD_BEGIN(); NEED_INV();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    RECT c = client(h);
    UN(NtUserInvalidateRect)(h, NULL, FALSE);
    CHECK(UN(NtUserValidateRect)(h, &c) == 1);
    CHECK(upd(h, NULL) == 0);
    PD_END();
}
PT(validate_rect_on_clean_window_succeeds) {
    PD_BEGIN(); NEED_INV();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    RECT a = pd_rc(1, 1, 5, 5);
    CHECK(UN(NtUserValidateRect)(h, &a) == 1);
    CHECK(UN(NtUserValidateRect)(h, NULL) == 1);
    CHECK(upd(h, NULL) == 0);
    PD_END();
}
PT(invalidate_rgn_exact_box_and_region_survives) {
    PD_BEGIN(); NEED_INV();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    HRGN rg = pd_rgn(2, 2, 12, 12);
    CHECK(UN(NtUserInvalidateRgn)(h, rg, FALSE) == 1);
    RECT r = {0}, e = pd_rc(2, 2, 12, 12);
    CHECK(upd(h, &r)); CHECK(pd_eq(&r, &e));
    /* RedrawWindow(hwnd, 0, hrgn, ..) only reads the region: the caller keeps ownership */
    CHECK(U(GetObjectType)(rg) == OBJ_REGION);
    RECT b; CHECK(pd_rgn_box(rg, &b) == SIMPLEREGION); CHECK(pd_eq(&b, &e));
    pd_rgn_free(rg);
    PD_END();
}
PT(invalidate_rgn_null_means_whole_client) {
    PD_BEGIN(); NEED_INV();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    RECT r = {0}, c = client(h);
    CHECK(UN(NtUserInvalidateRgn)(h, NULL, FALSE) == 1);
    CHECK(upd(h, &r)); CHECK(pd_eq(&r, &c));
    PD_END();
}
PT(invalidate_rgn_complex_region_pointwise) {
    PD_BEGIN(); NEED_INV();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    HRGN a = pd_rgn(0, 0, 20, 20), b = pd_rgn(10, 10, 40, 40), u = pd_rgn(0, 0, 1, 1);
    U(CombineRgn)(u, a, b, RGN_XOR);
    CHECK(UN(NtUserInvalidateRgn)(h, u, FALSE));
    HRGN out = pd_rgn(0, 0, 1, 1);
    CHECK(UN(NtUserGetUpdateRgn)(h, out, FALSE) == COMPLEXREGION);
    CHECK(U(EqualRgn)(out, u));
    pd_rgn_free(a); pd_rgn_free(b); pd_rgn_free(u); pd_rgn_free(out);
    PD_END();
}
PT(validate_rgn_removes_region_and_null_clears_all) {
    PD_BEGIN(); NEED_INV(); REQUIRE(U(ValidateRgn));
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    HRGN rg = pd_rgn(0, 0, 30, 30);
    UN(NtUserInvalidateRect)(h, NULL, FALSE);
    CHECK(U(ValidateRgn)(h, rg));
    HRGN out = pd_rgn(0, 0, 1, 1);
    CHECK(UN(NtUserGetUpdateRgn)(h, out, FALSE) == COMPLEXREGION);
    CHECK(!U(PtInRegion)(out, 10, 10)); CHECK(U(PtInRegion)(out, 60, 60));
    /* user32!ValidateRgn(hwnd, NULL) -> NtUserCallHwndParamLock(.., 0x7c): NULL region validates the whole client */
    CHECK(U(ValidateRgn)(h, NULL));
    CHECK(upd(h, NULL) == 0);
    pd_rgn_free(rg); pd_rgn_free(out);
    PD_END();
}
PT(validate_rgn_via_callhwndparamlock_code_7c) {
    PD_BEGIN(); NEED_INV(); REQUIRE(UN(NtUserCallHwndParamLock));
    if (t_is_wine()) { t_skip("win32u.NtUserCallHwndParamLock is an aborting stub in Wine"); return; }
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    HRGN rg = pd_rgn(0, 0, 40, 40);
    UN(NtUserInvalidateRect)(h, NULL, FALSE);
    ULONG_PTR r = UN(NtUserCallHwndParamLock)(h, (ULONG_PTR)rg, 0x7c);
    CHECK(r != 0);
    CHECK(upd(h, NULL) == 1);
    RECT b; RECT e = pd_rc(0, 0, PD_CW, PD_CH);
    CHECK(upd(h, &b)); CHECK(pd_eq(&b, &e));   /* bounding box of "client minus the 40x40 corner" is still the client */
    CHECK(UN(NtUserCallHwndParamLock)(h, 0, 0x7c) != 0);
    CHECK(upd(h, NULL) == 0);
    pd_rgn_free(rg);
    PD_END();
}
PT(update_region_is_public_gdi_region_in_screen_coords) {
    PD_BEGIN(); NEED_INV();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    RECT in = pd_rc(6, 7, 26, 37);
    UN(NtUserInvalidateRect)(h, &in, FALSE);
    ULONG_PTR v = 0;
    if (!pd_hrgn_update(h, &v)) { t_skip("client-side window block not readable"); return; }
    CHECK(v > 1);               /* a partial invalidation is stored as an HRGN, not the "whole client" sentinel 1 */
    if (v > 1) {
        CHECK(PD_TYPE7((void *)v) == 4);   /* GDI region handle type bits */
        struct pd_cell c;
        if (pd_cell((void *)v, &c)) CHECK(c.objt == 4);
        RECT b = {0}, cr = client(h);
        POINT o = {0, 0}; U(ClientToScreen)(h, &o);
        int t = pd_rgn_box((HRGN)v, &b);
        t_note("GetRgnBox(hrgnUpdate) = %d (%ld,%ld)-(%ld,%ld)", t, b.left, b.top, b.right, b.bottom);
        if (t == SIMPLEREGION) {
            /* GetUpdateRgn offsets the stored region by -rcClient.left/top, so it is stored in screen coordinates */
            RECT e = pd_rc(in.left + o.x, in.top + o.y, in.right + o.x, in.bottom + o.y);
            CHECK(pd_eq(&b, &e));
        }
    }
    PD_END();
}
PT(update_region_cleared_field_after_validate) {
    PD_BEGIN(); NEED_INV();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    RECT in = pd_rc(6, 7, 26, 37);
    UN(NtUserInvalidateRect)(h, &in, FALSE);
    ULONG_PTR v = 0;
    if (!pd_hrgn_update(h, &v)) { t_skip("client-side window block not readable"); return; }
    CHECK(v != 0);
    UN(NtUserValidateRect)(h, NULL);
    CHECK(pd_hrgn_update(h, &v) && v == 0);
    PD_END();
}

/* ---- invalid handles: ValidateHwndEx(hwnd, 1) sets ERROR_INVALID_WINDOW_HANDLE ---- */
static int call_by(int which, HWND h) {
    RECT r; HRGN rg = pd_rgn(0, 0, 4, 4); int ret = 0;
    switch (which) {
    case 0: ret = UN(NtUserInvalidateRect)(h, NULL, FALSE); break;
    case 1: ret = UN(NtUserInvalidateRgn)(h, rg, FALSE); break;
    case 2: ret = UN(NtUserValidateRect)(h, NULL); break;
    case 3: ret = UN(NtUserGetUpdateRect)(h, &r, FALSE); break;
    case 4: ret = UN(NtUserGetUpdateRgn)(h, rg, FALSE); break;
    case 5: ret = UN(NtUserRedrawWindow)(h, NULL, NULL, RDW_INVALIDATE); break;
    case 6: ret = (int)UN(NtUserCallHwndLock)(h, 0x73); break;
    case 7: ret = (int)UN(NtUserCallHwndParamLock)(h, 0, 0x7c); break;
    }
    pd_rgn_free(rg);
    return ret;
}
static const char *const call_names[] = { "InvalidateRect", "InvalidateRgn", "ValidateRect", "GetUpdateRect", "GetUpdateRgn", "RedrawWindow", "CallHwndLock(UpdateWindow)", "CallHwndParamLock(ValidateRgn)" };

PT(invalid_hwnd_constant_fails_with_1400) {
    PD_BEGIN(); NEED_INV(); REQUIRE(UN(NtUserCallHwndLock)); REQUIRE(UN(NtUserCallHwndParamLock));
    HWND bogus[] = { (HWND)(ULONG_PTR)0x12345678, (HWND)(ULONG_PTR)0xFFFF, (HWND)(ULONG_PTR)0x7FFFFFF0 };
    for (int b = 0; b < 3; b++)
        for (int w = 0; w < (t_is_wine() ? 6 : 8); w++) {
            SET_ERR(0);
            int r = call_by(w, bogus[b]);
            DWORD e = GetLastError();
            CHECKF(r == 0, "%s(%p) = %d", call_names[w], bogus[b], r);
            CHECKF(e == PD_ERR_INVALID_WINDOW_HANDLE, "%s(%p) last error %lu", call_names[w], bogus[b], e);
        }
    PD_END();
}
PT(destroyed_hwnd_fails_with_1400) {
    PD_BEGIN(); NEED_INV(); REQUIRE(UN(NtUserCallHwndLock)); REQUIRE(UN(NtUserCallHwndParamLock));
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    U(DestroyWindow)(h);
    for (int w = 0; w < (t_is_wine() ? 6 : 8); w++) {
        SET_ERR(0);
        int r = call_by(w, h);
        DWORD e = GetLastError();
        CHECKF(r == 0, "%s(destroyed) = %d", call_names[w], r);
        CHECKF(e == PD_ERR_INVALID_WINDOW_HANDLE, "%s(destroyed) last error %lu", call_names[w], e);
    }
    PD_END();
}
PT(getupdaterect_bad_hwnd_leaves_rect_untouched) {
    PD_BEGIN(); NEED_INV();
    RECT r = pd_rc(11, 22, 33, 44), keep = r;
    CHECK(UN(NtUserGetUpdateRect)((HWND)(ULONG_PTR)0x12345678, &r, FALSE) == 0);
    CHECK(pd_eq(&r, &keep));    /* the copy-out sits behind the ValidateHwnd success path */
    PD_END();
}

/* ---- NtUserRedrawWindow flag handling ---- */
PT(redraw_flag_bits_above_fff_rejected) {
    PD_BEGIN(); NEED_INV();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    /* NtUserRedrawWindow: (flags & 0xfffff000) != 0 -> UserSetLastError(ERROR_INVALID_FLAGS), result 0, before any state change */
    UINT bits[] = { 0x1000, 0x2000, 0x4000, 0x8000, 0x10000, 0x100000, 0x1000000, 0x80000000, 0xfffff000, RDW_INVALIDATE | 0x1000 };
    for (unsigned i = 0; i < sizeof bits / sizeof bits[0]; i++) {
        SET_ERR(0);
        BOOL r = UN(NtUserRedrawWindow)(h, NULL, NULL, bits[i]);
        DWORD e = GetLastError();
        CHECKF(r == 0, "flags 0x%x returned %d", bits[i], r);
        CHECKF(e == PD_ERR_INVALID_FLAGS, "flags 0x%x last error %lu", bits[i], e);
    }
    CHECK(upd(h, NULL) == 0);   /* RDW_INVALIDATE|0x1000 must not have invalidated */
    PD_END();
}
PT(redraw_flag_bits_rejected_for_null_hwnd_without_side_effect) {
    PD_BEGIN(); NEED_INV();
    SET_ERR(0);
    CHECK(UN(NtUserRedrawWindow)(NULL, NULL, NULL, 0x80000000 | RDW_INVALIDATE) == 0);
    EXPECT_ERR(PD_ERR_INVALID_FLAGS);
    PD_END();
}
PT(redraw_all_defined_flags_accepted) {
    PD_BEGIN(); NEED_INV();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    /* every defined RDW_* bit (0x001..0xFFF space) passes the 0xfffff000 mask test */
    UINT valid[] = { RDW_INVALIDATE, RDW_INTERNALPAINT, RDW_ERASE, RDW_VALIDATE, RDW_NOINTERNALPAINT, RDW_NOERASE, RDW_NOCHILDREN,
                     RDW_ALLCHILDREN, RDW_FRAME, RDW_NOFRAME, RDW_VALIDATE | RDW_NOERASE | RDW_NOFRAME };
    for (unsigned i = 0; i < sizeof valid / sizeof valid[0]; i++) {
        SET_ERR(0);
        BOOL r = UN(NtUserRedrawWindow)(h, NULL, NULL, valid[i]);
        DWORD e = GetLastError();
        CHECKF(e != PD_ERR_INVALID_FLAGS, "flags 0x%x rejected as invalid", valid[i]);
        if (r != 1) t_note("flags 0x%x returned %d (err %lu)", valid[i], r, e);
    }
    PD_END();
}

struct rdw_row { const char *name; UINT pre; UINT flags; int dirty; };
static void run_rdw_row(const struct rdw_row *w) {
    PD_BEGIN(); NEED_INV();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    if (w->pre) CHECKF(UN(NtUserRedrawWindow)(h, NULL, NULL, w->pre), "pre %s", w->name);
    BOOL rr = UN(NtUserRedrawWindow)(h, NULL, NULL, w->flags);
    if (w->dirty || w->pre) CHECKF(rr == 1, "%s returned %d", w->name, rr);
    else if (rr != 1) t_note("%s returned %d", w->name, rr);
    BOOL got = upd(h, NULL);
    CHECKF((got != 0) == (w->dirty != 0), "%s: GetUpdateRect=%d want %d", w->name, got, w->dirty);
    PD_END();
}
#define RDW_ROW(id, pre, flags, dirty) \
    static const struct rdw_row row_##id = { #id, pre, flags, dirty }; \
    PT(redraw_row_##id) { run_rdw_row(&row_##id); }
RDW_ROW(invalidate,                    0,              RDW_INVALIDATE,                                   1)
RDW_ROW(invalidate_erase,              0,              RDW_INVALIDATE | RDW_ERASE,                       1)
RDW_ROW(invalidate_noerase,            0,              RDW_INVALIDATE | RDW_NOERASE,                     1)
RDW_ROW(invalidate_frame,              0,              RDW_INVALIDATE | RDW_FRAME,                       1)
RDW_ROW(invalidate_noframe,            0,              RDW_INVALIDATE | RDW_NOFRAME,                     1)
RDW_ROW(invalidate_allchildren,        0,              RDW_INVALIDATE | RDW_ALLCHILDREN,                 1)
RDW_ROW(invalidate_nochildren,         0,              RDW_INVALIDATE | RDW_NOCHILDREN,                  1)
RDW_ROW(erase_alone_is_inert,          0,              RDW_ERASE,                                        0)
RDW_ROW(frame_alone_no_update_region,  0,              RDW_FRAME,                                        0)
RDW_ROW(internalpaint_no_update_region,0,              RDW_INTERNALPAINT,                                0)
RDW_ROW(validate_on_clean,             0,              RDW_VALIDATE,                                     0)
RDW_ROW(noerase_alone,                 0,              RDW_NOERASE,                                      0)
RDW_ROW(nointernalpaint_alone,         0,              RDW_NOINTERNALPAINT,                              0)
RDW_ROW(zero_flags,                    0,              0,                                                0)
RDW_ROW(validate_after_invalidate,     RDW_INVALIDATE,RDW_VALIDATE,                                     0)
RDW_ROW(validate_noerase_after_inv,    RDW_INVALIDATE,RDW_VALIDATE | RDW_NOERASE,                       0)
RDW_ROW(validate_allchildren_after_inv,RDW_INVALIDATE,RDW_VALIDATE | RDW_ALLCHILDREN,                   0)
RDW_ROW(internalpaint_keeps_update,    RDW_INVALIDATE,RDW_INTERNALPAINT,                                1)
RDW_ROW(nointernalpaint_keeps_update,  RDW_INVALIDATE,RDW_NOINTERNALPAINT,                              1)
RDW_ROW(erase_keeps_update,            RDW_INVALIDATE,RDW_ERASE,                                        1)
RDW_ROW(noerase_keeps_update,          RDW_INVALIDATE,RDW_NOERASE,                                      1)
RDW_ROW(frame_keeps_update,            RDW_INVALIDATE,RDW_FRAME,                                        1)

PT(redraw_rect_and_region_region_wins) {
    PD_BEGIN(); NEED_INV();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    RECT rc = pd_rc(0, 0, 8, 8), got = {0}, e = pd_rc(30, 30, 50, 50);
    HRGN rg = pd_rgn(30, 30, 50, 50);
    /* RedrawWindow(.., prc, hrgn, ..): the region parameter takes precedence over the rectangle */
    CHECK(UN(NtUserRedrawWindow)(h, &rc, rg, RDW_INVALIDATE));
    CHECK(upd(h, &got)); CHECK(pd_eq(&got, &e));
    pd_rgn_free(rg);
    PD_END();
}
PT(redraw_rect_validate_partial) {
    PD_BEGIN(); NEED_INV();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    RECT half = pd_rc(0, 0, PD_CW, PD_CH / 2), got = {0}, e = pd_rc(0, PD_CH / 2, PD_CW, PD_CH);
    UN(NtUserRedrawWindow)(h, NULL, NULL, RDW_INVALIDATE);
    CHECK(UN(NtUserRedrawWindow)(h, &half, NULL, RDW_VALIDATE));
    CHECK(upd(h, &got)); CHECK(pd_eq(&got, &e));
    PD_END();
}
PT(redraw_internalpaint_posts_paint_without_update_region) {
    PD_BEGIN(); NEED_INV();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    MSG m;
    CHECK(!pd_peek_paint(h, &m, 0));
    CHECK(UN(NtUserRedrawWindow)(h, NULL, NULL, RDW_INTERNALPAINT));
    CHECK(upd(h, NULL) == 0);
    CHECK(pd_qs_paint() == QS_PAINT);      /* the wake bit is up ... (looking at the message consumes the internal-paint flag, so check first) */
    CHECK(pd_peek_paint(h, &m, 0));        /* ... and WM_PAINT is available although nothing is invalid */
    CHECK(m.message == WM_PAINT); CHECK(m.hwnd == h);
    PD_END();
}
PT(redraw_nointernalpaint_cancels_internalpaint) {
    PD_BEGIN(); NEED_INV();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    UN(NtUserRedrawWindow)(h, NULL, NULL, RDW_INTERNALPAINT);
    CHECK(pd_peek_paint(h, NULL, 0));
    UN(NtUserRedrawWindow)(h, NULL, NULL, RDW_NOINTERNALPAINT);
    CHECK(!pd_peek_paint(h, NULL, 0));
    PD_END();
}
PT(redraw_updatenow_delivers_wm_paint_synchronously) {
    PD_BEGIN(); NEED_INV();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    pd_beh = PB_PAINT_BP;
    CHECK(UN(NtUserRedrawWindow)(h, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW) == 1);
    CHECK(pd_count(h, WM_PAINT) == 1);      /* no message pump was run */
    CHECK(pd_obs.valid);
    CHECK(upd(h, NULL) == 0);
    pd_beh = 0;
    PD_END();
}
PT(redraw_updatenow_without_invalid_sends_nothing) {
    PD_BEGIN(); NEED_INV();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    pd_beh = PB_PAINT_BP;
    CHECK(UN(NtUserRedrawWindow)(h, NULL, NULL, RDW_UPDATENOW) == 1);
    CHECK(pd_count(h, WM_PAINT) == 0);
    PD_END();
}
PT(redraw_erasenow_sends_erase_but_not_paint) {
    PD_BEGIN(); NEED_INV();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    PD_NEED_EXPOSED(h);
    CHECK(UN(NtUserRedrawWindow)(h, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ERASENOW));
    CHECK(pd_count(h, WM_ERASEBKGND) == 1);
    CHECK(pd_count(h, WM_PAINT) == 0);
    CHECK(upd(h, NULL) == 1);               /* the update region itself is untouched */
    /* the erase was done and handled (class brush) -> BeginPaint reports fErase == FALSE */
    pd_beh = PB_PAINT_BP;
    pd_pump(30, 20);
    CHECK(pd_obs.valid);
    CHECK(pd_obs.ps.fErase == FALSE);
    CHECK(pd_count(h, WM_ERASEBKGND) == 1);
    pd_beh = 0;
    PD_END();
}
PT(redraw_message_order_ncpaint_then_paint_then_erase) {
    PD_BEGIN(); NEED_INV();
    HWND h = pd_top_style(PDC_PLAIN, WS_CAPTION);
    REQUIRE(h);
    PD_NEED_EXPOSED(h);
    pd_beh = PB_PAINT_BP;
    CHECK(UN(NtUserRedrawWindow)(h, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_UPDATENOW));
    int nc = pd_find(h, WM_NCPAINT, 0), pa = pd_find(h, WM_PAINT, 0), er = pd_find(h, WM_ERASEBKGND, 0);
    CHECK(nc >= 0); CHECK(pa >= 0); CHECK(er >= 0);
    /* BeginPaint sends WM_NCPAINT and WM_ERASEBKGND from inside the window's WM_PAINT handling */
    if (nc >= 0 && er >= 0) CHECK(nc < er);
    if (pa >= 0 && er >= 0) CHECK(pa < er);
    t_note("order: NCPAINT=%d PAINT=%d ERASEBKGND=%d", nc, pa, er);
    pd_beh = 0;
    PD_END();
}
PT(redraw_frame_ncpaint_wparam_is_region_or_one) {
    PD_BEGIN(); NEED_INV();
    HWND h = pd_top_style(PDC_PLAIN, WS_CAPTION);
    REQUIRE(h);
    pd_beh = PB_PAINT_BP;
    UN(NtUserRedrawWindow)(h, NULL, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW);
    CHECK(pd_ncpaint_seen >= 1);
    if (pd_ncpaint_seen) t_note("WM_NCPAINT wParam = %p", (void *)pd_ncpaint_wparam);
    pd_beh = 0;
    PD_END();
}
PT(redraw_allchildren_reaches_clipchildren_child) {
    PD_BEGIN(); NEED_INV();
    HWND p = pd_top_style(PDC_PLAIN, WS_CLIPCHILDREN);
    REQUIRE(p);
    HWND c = pd_child(p, PDC_PLAIN, 0, 10, 10, 40, 30);
    REQUIRE(c);
    UN(NtUserRedrawWindow)(p, NULL, NULL, RDW_INVALIDATE);
    CHECK(upd(p, NULL) == 1);
    CHECK(upd(c, NULL) == 0);               /* WS_CLIPCHILDREN parent: children are not part of its invalid area */
    UN(NtUserRedrawWindow)(p, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
    RECT r = {0}, e = pd_rc(0, 0, 40, 30);
    CHECK(upd(c, &r) == 1); CHECK(pd_eq(&r, &e));
    UN(NtUserRedrawWindow)(p, NULL, NULL, RDW_VALIDATE | RDW_ALLCHILDREN);
    CHECK(upd(c, NULL) == 0); CHECK(upd(p, NULL) == 0);
    PD_END();
}
PT(redraw_nochildren_leaves_child_clean) {
    PD_BEGIN(); NEED_INV();
    HWND p = pd_top(PDC_PLAIN);
    REQUIRE(p);
    HWND c = pd_child(p, PDC_PLAIN, 0, 10, 10, 40, 30);
    REQUIRE(c);
    UN(NtUserRedrawWindow)(p, NULL, NULL, RDW_INVALIDATE | RDW_NOCHILDREN);
    CHECK(upd(p, NULL) == 1);
    CHECK(upd(c, NULL) == 0);
    PD_END();
}
PT(redraw_child_update_clipped_to_parent_client) {
    PD_BEGIN(); NEED_INV();
    HWND p = pd_top(PDC_PLAIN);
    REQUIRE(p);
    HWND c = pd_child(p, PDC_PLAIN, 0, PD_CW - 30, 20, 60, 30);   /* sticks out 30 px on the right */
    REQUIRE(c);
    UN(NtUserInvalidateRect)(c, NULL, FALSE);
    RECT r = {0}, e = pd_rc(0, 0, 30, 30);
    CHECK(upd(c, &r) == 1);
    CHECK(pd_eq(&r, &e));                   /* GetUpdateRect: intersected with parents */
    HRGN rg = pd_rgn(0, 0, 1, 1);
    CHECK(UN(NtUserGetUpdateRgn)(c, rg, FALSE) == SIMPLEREGION);
    RECT b; pd_rgn_box(rg, &b); CHECK(pd_eq(&b, &e));
    pd_rgn_free(rg);
    PD_END();
}
PT(redraw_hidden_window_not_invalidated_note) {
    PD_BEGIN(); NEED_INV();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    U(ShowWindow)(h, SW_HIDE);
    pd_settle(h);
    BOOL r = UN(NtUserInvalidateRect)(h, NULL, FALSE);
    BOOL g = upd(h, NULL);
    t_note("hidden window: InvalidateRect=%d GetUpdateRect=%d", r, g);
    CHECK(r == 1);
    PD_END();
}

/* ---- NtUserGetUpdateRect / NtUserGetUpdateRgn results ---- */
PT(getupdaterect_clean_zeroes_output) {
    PD_BEGIN(); NEED_INV();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    RECT r = pd_rc(9, 9, 9, 9), z = {0};
    CHECK(UN(NtUserGetUpdateRect)(h, &r, FALSE) == 0);
    CHECK(pd_eq(&r, &z));
    r = pd_rc(9, 9, 9, 9);
    CHECK(UN(NtUserGetUpdateRect)(h, &r, TRUE) == 0);
    CHECK(pd_eq(&r, &z));
    PD_END();
}
PT(getupdaterect_user32_clean_zeroes_output) {
    PD_BEGIN(); REQUIRE(U(GetUpdateRect));
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    RECT r = pd_rc(9, 9, 9, 9), z = {0};
    /* user32!GetUpdateRect answers a clean window itself (hrgnUpdate == 0 and no paint flags) */
    CHECK(U(GetUpdateRect)(h, &r, FALSE) == 0);
    CHECK(pd_eq(&r, &z));
    PD_END();
}
PT(getupdaterect_null_rect_pointer_ok) {
    PD_BEGIN(); NEED_INV();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    CHECK(UN(NtUserGetUpdateRect)(h, NULL, FALSE) == 0);
    UN(NtUserInvalidateRect)(h, NULL, FALSE);
    CHECK(UN(NtUserGetUpdateRect)(h, NULL, FALSE) == 1);
    PD_END();
}
PT(getupdaterect_erase_true_sends_erasebkgnd_once) {
    PD_BEGIN(); NEED_INV();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    PD_NEED_EXPOSED(h);
    UN(NtUserInvalidateRect)(h, NULL, TRUE);
    CHECK(UN(NtUserGetUpdateRect)(h, NULL, FALSE) == 1);
    CHECK(pd_count(h, WM_ERASEBKGND) == 0);
    CHECK(UN(NtUserGetUpdateRect)(h, NULL, TRUE) == 1);
    CHECK(pd_count(h, WM_ERASEBKGND) == 1);
    CHECK(UN(NtUserGetUpdateRect)(h, NULL, TRUE) == 1);
    CHECK(pd_count(h, WM_ERASEBKGND) == 1);   /* the erase request was consumed */
    PD_END();
}
PT(getupdaterect_erase_false_does_not_erase) {
    PD_BEGIN(); NEED_INV();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    UN(NtUserInvalidateRect)(h, NULL, TRUE);
    CHECK(UN(NtUserGetUpdateRect)(h, NULL, FALSE) == 1);
    CHECK(pd_count(h, WM_ERASEBKGND) == 0);
    PD_END();
}
PT(getupdatergn_clean_is_nullregion_and_empties_target) {
    PD_BEGIN(); NEED_INV();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    HRGN rg = pd_rgn(5, 5, 50, 50);
    CHECK(UN(NtUserGetUpdateRgn)(h, rg, FALSE) == NULLREGION);   /* SetEmptyRgn(target), result 1 */
    RECT b; CHECK(pd_rgn_box(rg, &b) == NULLREGION);
    pd_rgn_free(rg);
    PD_END();
}
PT(getupdatergn_whole_client_is_simple_client_rect) {
    PD_BEGIN(); NEED_INV();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    UN(NtUserInvalidateRect)(h, NULL, FALSE);
    HRGN rg = pd_rgn(0, 0, 1, 1);
    CHECK(UN(NtUserGetUpdateRgn)(h, rg, FALSE) == SIMPLEREGION);
    RECT b, c = client(h); pd_rgn_box(rg, &b); CHECK(pd_eq(&b, &c));
    pd_rgn_free(rg);
    PD_END();
}
PT(getupdatergn_overwrites_previous_contents) {
    PD_BEGIN(); NEED_INV();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    RECT in = pd_rc(10, 10, 20, 20);
    UN(NtUserInvalidateRect)(h, &in, FALSE);
    HRGN rg = pd_rgn(50, 50, 70, 70);
    CHECK(UN(NtUserGetUpdateRgn)(h, rg, FALSE) == SIMPLEREGION);
    RECT b; pd_rgn_box(rg, &b); CHECK(pd_eq(&b, &in));
    pd_rgn_free(rg);
    PD_END();
}
PT(getupdatergn_is_client_relative) {
    PD_BEGIN(); NEED_INV();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    RECT in = pd_rc(1, 2, 3, 4);
    UN(NtUserInvalidateRect)(h, &in, FALSE);
    HRGN rg = pd_rgn(0, 0, 1, 1);
    UN(NtUserGetUpdateRgn)(h, rg, FALSE);
    RECT b; pd_rgn_box(rg, &b);
    CHECK(b.left == 1 && b.top == 2);        /* GetUpdateRgn: region offset by -rcClient.left/top */
    CHECK(b.left != PD_X);                   /* not screen coordinates */
    pd_rgn_free(rg);
    PD_END();
}
PT(getupdatergn_erase_true_erases) {
    PD_BEGIN(); NEED_INV();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    PD_NEED_EXPOSED(h);
    UN(NtUserInvalidateRect)(h, NULL, TRUE);
    HRGN rg = pd_rgn(0, 0, 1, 1);
    UN(NtUserGetUpdateRgn)(h, rg, TRUE);
    CHECK(pd_count(h, WM_ERASEBKGND) == 1);
    UN(NtUserGetUpdateRgn)(h, rg, FALSE);
    CHECK(pd_count(h, WM_ERASEBKGND) == 1);
    pd_rgn_free(rg);
    PD_END();
}
PT(getupdatergn_user32_null_region_error) {
    PD_BEGIN(); REQUIRE(U(GetUpdateRgn));
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    SET_ERR(0);
    CHECK(U(GetUpdateRgn)(h, NULL, FALSE) == 0);   /* user32!GetUpdateRgn: NULL hRgn -> RtlSetLastWin32Error(ERROR_INVALID_HANDLE) */
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    PD_END();
}
PT(getupdatergn_user32_clean_is_nullregion) {
    PD_BEGIN(); REQUIRE(U(GetUpdateRgn)); REQUIRE(U(CreateRectRgn)); REQUIRE(U(GetRgnBox));
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    HRGN rg = pd_rgn(1, 1, 9, 9);
    /* user32 shortcut: SetRectRgn(hRgn,0,0,0,0) and return 1 */
    CHECK(U(GetUpdateRgn)(h, rg, FALSE) == NULLREGION);
    RECT b; CHECK(pd_rgn_box(rg, &b) == NULLREGION);
    pd_rgn_free(rg);
    PD_END();
}
PT(getupdatergn_result_table) {
    PD_BEGIN(); NEED_INV();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    struct { const char *n; int rects; int expect; } t[] = { { "clean", 0, NULLREGION }, { "one", 1, SIMPLEREGION }, { "two-apart", 2, COMPLEXREGION } };
    for (int i = 0; i < 3; i++) {
        pd_settle(h);
        RECT a = pd_rc(0, 0, 8, 8), b = pd_rc(40, 40, 50, 50);
        if (t[i].rects >= 1) UN(NtUserInvalidateRect)(h, &a, FALSE);
        if (t[i].rects >= 2) UN(NtUserInvalidateRect)(h, &b, FALSE);
        HRGN rg = pd_rgn(0, 0, 1, 1);
        int r = UN(NtUserGetUpdateRgn)(h, rg, FALSE);
        CHECKF(r == t[i].expect, "%s: GetUpdateRgn = %d want %d", t[i].n, r, t[i].expect);
        pd_rgn_free(rg);
    }
    PD_END();
}
PT(getupdate_does_not_clear_region) {
    PD_BEGIN(); NEED_INV();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    RECT in = pd_rc(2, 3, 12, 13), a = {0}, b = {0};
    UN(NtUserInvalidateRect)(h, &in, FALSE);
    HRGN rg = pd_rgn(0, 0, 1, 1);
    UN(NtUserGetUpdateRgn)(h, rg, FALSE);
    CHECK(upd(h, &a)); CHECK(upd(h, &b)); CHECK(pd_eq(&a, &in)); CHECK(pd_eq(&b, &in));
    pd_rgn_free(rg);
    PD_END();
}
PT(update_region_survives_window_move) {
    PD_BEGIN(); NEED_INV();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    RECT in = pd_rc(6, 7, 26, 27), got = {0};
    UN(NtUserInvalidateRect)(h, &in, FALSE);
    U(SetWindowPos)(h, NULL, PD_X + 12, PD_Y + 9, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
    BOOL g = upd(h, &got);
    t_note("after move: GetUpdateRect=%d (%ld,%ld)-(%ld,%ld)", g, got.left, got.top, got.right, got.bottom);
    CHECK(g == 1);                           /* a move without NOCOPYBITS handling keeps (or enlarges) the pending region */
    PD_END();
}
PT(redraw_class_redraw_flags_full_invalidate_on_width_growth) {
    PD_BEGIN(); NEED_INV();
    HWND h = pd_top(PDC_REDRAW);
    REQUIRE(h);
    /* CS_HREDRAW|CS_VREDRAW: a size change invalidates the entire client area (WVR_REDRAW from the non-client calc-size pass) */
    U(SetWindowPos)(h, NULL, 0, 0, PD_CW + 20, PD_CH, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    RECT r = {0}, c = client(h);
    CHECK(upd(h, &r)); CHECK(pd_eq(&r, &c));
    pd_settle(h);
    U(SetWindowPos)(h, NULL, 0, 0, PD_CW + 20, PD_CH + 16, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    c = client(h);
    CHECK(upd(h, &r)); CHECK(pd_eq(&r, &c));
    PD_END();
}
PT(redraw_plain_class_growth_invalidates_new_area_only_note) {
    PD_BEGIN(); NEED_INV();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    U(SetWindowPos)(h, NULL, 0, 0, PD_CW + 20, PD_CH, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    RECT r = {0};
    BOOL g = upd(h, &r);
    t_note("plain class, +20 width: GetUpdateRect=%d (%ld,%ld)-(%ld,%ld)", g, r.left, r.top, r.right, r.bottom);
    CHECK(g == 1);
    PD_END();
}
