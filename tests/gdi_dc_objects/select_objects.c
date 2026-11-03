/* Selecting objects into DCs: SelectObjectImpl (gdi32full) routes by handle type - brush/pen/font only write the
 * DC_ATTR page (+ dirty bit), bitmap goes to NtGdiSelectBitmap, region to ExtSelectClipRgn, palette fails with
 * ERROR_INVALID_FUNCTION. GetCurrentObject reads the page for brush/pen and NtGdiGetDCObject for font/palette/bitmap;
 * NtGdiGetDCObject first applies any pending page selection (brush/pen selection) and clears the dirty bit. */
#include "dc.h"

static HBITMAP mk_bm(HDC scr, int w, int h) { return U(CreateCompatibleBitmap)(scr, w, h); }

TEST(gdi_dc_objects_default_pen_brush_are_stock) {
    FX(f);
    /* New DCs start with BLACK_PEN / WHITE_BRUSH / the default palette. */
    EXPECT_EQ(U(GetCurrentObject)(f.mem, OBJ_PEN), U(GetStockObject)(BLACK_PEN));
    EXPECT_EQ(U(GetCurrentObject)(f.mem, OBJ_BRUSH), U(GetStockObject)(WHITE_BRUSH));
    EXPECT_EQ(U(GetCurrentObject)(f.mem, OBJ_PAL), U(GetStockObject)(DEFAULT_PALETTE));
    EXPECT_EQ(U(GetCurrentObject)(f.scr, OBJ_PEN), U(GetStockObject)(BLACK_PEN));
    EXPECT_EQ(U(GetCurrentObject)(f.scr, OBJ_BRUSH), U(GetStockObject)(WHITE_BRUSH));
    dcf_close(&f);
}

TEST(gdi_dc_objects_default_bitmap_is_stock_1x1_mono) {
    FX(f);
    HGDIOBJ b = U(GetCurrentObject)(f.mem, OBJ_BITMAP);
    CHECK(b != NULL);
    EXPECT_EQ(U(GetObjectType)(b), OBJ_BITMAP);
    /* the bitmap-selection path returns the default bitmap handle when nothing is selected; DC restore re-selects
     * gahStockObjects[0x15] to get back to it. */
    EXPECT_EQ(b, U(GetStockObject)(21));
    EXPECT_EQ(H_STOCKED(b), 1);
    BITMAP bm; memset(&bm, 0xcc, sizeof bm);
    EXPECT_EQ(U(GetObjectW)(b, sizeof bm, &bm), sizeof bm);
    EXPECT_EQ(bm.bmWidth, 1); EXPECT_EQ(bm.bmHeight, 1); EXPECT_EQ(bm.bmPlanes, 1); EXPECT_EQ(bm.bmBitsPixel, 1);
    dcf_close(&f);
}

TEST(gdi_dc_objects_default_font_is_a_font) {
    FX(f);
    HGDIOBJ ft = U(GetCurrentObject)(f.mem, OBJ_FONT);
    CHECK(ft != NULL);
    EXPECT_EQ(U(GetObjectType)(ft), OBJ_FONT);
    t_note("default font %p, system font %p, device default %p", ft, U(GetStockObject)(SYSTEM_FONT), U(GetStockObject)(DEVICE_DEFAULT_FONT));
    dcf_close(&f);
}

TEST(gdi_dc_objects_get_current_object_invalid_types) {
    FX(f);
    static const UINT bad[] = { 0, 3, 4, 8, 9, 10, 12, 13, 15, 16, 100, 0xffffffff };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        SET_ERR(0);
        /* gdi32full!GetCurrentObject: only types 1,2,5,6,7,11,14 are mapped; others -> ERROR_INVALID_PARAMETER. */
        CHECKF(U(GetCurrentObject)(f.mem, bad[i]) == NULL, "type %u", bad[i]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
    dcf_close(&f);
}

TEST(gdi_dc_objects_get_current_object_extpen_is_pen) {
    FX(f);
    HPEN p = U(CreatePen)(PS_SOLID, 1, RGB(1, 2, 3));
    U(SelectObject)(f.mem, p);
    EXPECT_EQ(U(GetCurrentObject)(f.mem, OBJ_PEN), p);
    EXPECT_EQ(U(GetCurrentObject)(f.mem, OBJ_EXTPEN), p);
    U(SelectObject)(f.mem, U(GetStockObject)(BLACK_PEN));
    U(GdiFlush)();
    U(DeleteObject)(p);
    dcf_close(&f);
}

TEST(gdi_dc_objects_get_current_object_bad_dc) {
    REQUIRE(U(CreateCompatibleDC));
    HDC dc = U(CreateCompatibleDC)(NULL);
    U(DeleteDC)(dc);
    static const UINT types[] = { OBJ_PEN, OBJ_BRUSH, OBJ_PAL, OBJ_FONT, OBJ_BITMAP };
    for (unsigned i = 0; i < sizeof types / sizeof *types; i++) {
        CHECKF(U(GetCurrentObject)(NULL, types[i]) == NULL, "NULL dc type %u", types[i]);
        CHECKF(U(GetCurrentObject)(dc, types[i]) == NULL, "dead dc type %u", types[i]);
    }
}

TEST(gdi_dc_objects_select_pen_returns_previous) {
    FX(f);
    HPEN a = U(CreatePen)(PS_SOLID, 1, RGB(1, 0, 0)), b = U(CreatePen)(PS_DASH, 1, RGB(0, 1, 0));
    EXPECT_EQ(U(SelectObject)(f.mem, a), U(GetStockObject)(BLACK_PEN));
    EXPECT_EQ(U(SelectObject)(f.mem, b), a);
    EXPECT_EQ(U(SelectObject)(f.mem, b), b);
    EXPECT_EQ(U(GetCurrentObject)(f.mem, OBJ_PEN), b);
    EXPECT_EQ(U(SelectObject)(f.mem, U(GetStockObject)(BLACK_PEN)), b);
    U(GdiFlush)();
    CHECK(U(DeleteObject)(a)); CHECK(U(DeleteObject)(b));
    dcf_close(&f);
}

TEST(gdi_dc_objects_select_brush_returns_previous) {
    FX(f);
    HBRUSH a = U(CreateSolidBrush)(RGB(1, 0, 0)), b = U(CreateHatchBrush)(HS_CROSS, RGB(0, 1, 0));
    EXPECT_EQ(U(SelectObject)(f.mem, a), U(GetStockObject)(WHITE_BRUSH));
    EXPECT_EQ(U(SelectObject)(f.mem, b), a);
    EXPECT_EQ(U(GetCurrentObject)(f.mem, OBJ_BRUSH), b);
    EXPECT_EQ(U(SelectObject)(f.mem, U(GetStockObject)(NULL_BRUSH)), b);
    EXPECT_EQ(U(GetCurrentObject)(f.mem, OBJ_BRUSH), U(GetStockObject)(NULL_BRUSH));
    U(GdiFlush)();
    CHECK(U(DeleteObject)(a)); CHECK(U(DeleteObject)(b));
    dcf_close(&f);
}

TEST(gdi_dc_objects_select_font_returns_previous) {
    FX(f);
    LOGFONTW lf; memset(&lf, 0, sizeof lf); lf.lfHeight = -12; lf.lfCharSet = DEFAULT_CHARSET;
    lstrcpyW(lf.lfFaceName, L"Arial");
    HFONT a = U(CreateFontIndirectW)(&lf);
    HGDIOBJ f0 = U(GetCurrentObject)(f.mem, OBJ_FONT);
    CHECK(a != NULL);
    EXPECT_EQ(U(SelectObject)(f.mem, a), f0);
    EXPECT_EQ(U(GetCurrentObject)(f.mem, OBJ_FONT), a);
    EXPECT_EQ(U(SelectObject)(f.mem, f0), a);
    U(GdiFlush)();
    CHECK(U(DeleteObject)(a));
    dcf_close(&f);
}

TEST(gdi_dc_objects_select_stock_font_and_back) {
    FX(f);
    HGDIOBJ f0 = U(GetCurrentObject)(f.mem, OBJ_FONT);
    static const int st[] = { OEM_FIXED_FONT, ANSI_FIXED_FONT, ANSI_VAR_FONT, SYSTEM_FONT, DEVICE_DEFAULT_FONT, SYSTEM_FIXED_FONT };
    for (unsigned i = 0; i < sizeof st / sizeof *st; i++) {
        HGDIOBJ s = U(GetStockObject)(st[i]);
        U(SelectObject)(f.mem, s);
        CHECKF(U(GetCurrentObject)(f.mem, OBJ_FONT) == s, "stock font %d", st[i]);
    }
    U(SelectObject)(f.mem, f0);
    EXPECT_EQ(U(GetCurrentObject)(f.mem, OBJ_FONT), f0);
    dcf_close(&f);
}

TEST(gdi_dc_objects_select_bitmap_returns_previous_and_is_current) {
    FX(f);
    HBITMAP a = mk_bm(f.scr, 4, 4), b = mk_bm(f.scr, 8, 8);
    HGDIOBJ def = U(GetCurrentObject)(f.mem, OBJ_BITMAP);
    EXPECT_EQ(U(SelectObject)(f.mem, a), def);
    EXPECT_EQ(U(GetCurrentObject)(f.mem, OBJ_BITMAP), a);
    EXPECT_EQ(U(SelectObject)(f.mem, b), a);
    EXPECT_EQ(U(GetCurrentObject)(f.mem, OBJ_BITMAP), b);
    EXPECT_EQ(U(SelectObject)(f.mem, b), b);    /* reselect: the bitmap-selection path returns the current handle */
    EXPECT_EQ(U(SelectObject)(f.mem, def), b);
    CHECK(U(DeleteObject)(a)); CHECK(U(DeleteObject)(b));
    dcf_close(&f);
}

TEST(gdi_dc_objects_bitmap_selected_in_two_dcs_fails) {
    FX(f);
    HDC d2 = U(CreateCompatibleDC)(f.scr);
    HBITMAP bm = mk_bm(f.scr, 4, 4);
    HGDIOBJ def = U(GetCurrentObject)(f.mem, OBJ_BITMAP);
    EXPECT_EQ(U(SelectObject)(f.mem, bm), def);
    /* the bitmap-selection path: surface+0xa8 (select count) != 0 and its owner DC (+0xa0) is another DC -> no-op, NULL. */
    CHECK(U(SelectObject)(d2, bm) == NULL);
    EXPECT_EQ(U(GetCurrentObject)(d2, OBJ_BITMAP), def);
    EXPECT_EQ(U(GetCurrentObject)(f.mem, OBJ_BITMAP), bm);
    /* Once released from the first DC the second may take it. */
    U(SelectObject)(f.mem, def);
    EXPECT_EQ(U(SelectObject)(d2, bm), def);
    CHECK(U(SelectObject)(f.mem, bm) == NULL);
    U(SelectObject)(d2, def);
    CHECK(U(DeleteObject)(bm));
    U(DeleteDC)(d2);
    dcf_close(&f);
}

TEST(gdi_dc_objects_delete_selected_bitmap_is_deferred_until_deselected) {
    FX(f);
    HBITMAP bm = mk_bm(f.scr, 4, 4);
    HGDIOBJ def = U(GetCurrentObject)(f.mem, OBJ_BITMAP);
    U(SelectObject)(f.mem, bm);
    /* Windows 10 accepts the delete of a selected bitmap (TRUE) but keeps the object alive and selected until it is
     * deselected; the deletion completes then, so a second DeleteObject is FALSE and the handle is dead. */
    CHECK(U(DeleteObject)(bm));
    EXPECT_EQ(U(GetObjectType)(bm), OBJ_BITMAP);
    EXPECT_EQ(U(GetCurrentObject)(f.mem, OBJ_BITMAP), bm);
    BITMAP info; CHECK(U(GetObjectW)(bm, sizeof info, &info) == (int)sizeof info);
    U(SelectObject)(f.mem, def);
    EXPECT_DEAD(bm);
    CHECK(U(GetObjectW)(bm, sizeof info, &info) == 0);
    CHECK(!U(DeleteObject)(bm));
    dcf_close(&f);
}

TEST(gdi_dc_objects_delete_dc_releases_selected_bitmap) {
    FX(f);
    HBITMAP bm = mk_bm(f.scr, 4, 4);
    U(SelectObject)(f.mem, bm);
    CHECK(U(DeleteObject)(bm));       /* accepted, deferred until the bitmap is no longer selected */
    /* Deleting the DC drops its reference on the selected bitmap, which completes the deferred deletion. */
    CHECK(U(DeleteDC)(f.mem));
    f.mem = NULL;
    EXPECT_DEAD(bm);
    CHECK(!U(DeleteObject)(bm));
    dcf_close(&f);
}

TEST(gdi_dc_objects_select_bitmap_into_screen_dc_fails) {
    DCWANT(UN(NtGdiSelectBitmap));
    FX(f);
    HBITMAP bm = mk_bm(f.scr, 4, 4);
    SET_ERR(0);
    /* NtGdiSelectBitmap builds an MDCOBJ: only memory DCs validate. */
    CHECK(U(SelectObject)(f.scr, bm) == NULL);
    CHECK(UN(NtGdiSelectBitmap)(f.scr, bm) == NULL);
    CHECK(UN(NtGdiSelectBitmap)(NULL, bm) == NULL);
    CHECK(U(DeleteObject)(bm));
    dcf_close(&f);
}

TEST(gdi_dc_objects_syscall_select_bitmap_returns_previous) {
    DCWANT(UN(NtGdiSelectBitmap)); DCWANT(UN(NtGdiGetDCObject));
    FX(f);
    HBITMAP bm = mk_bm(f.scr, 4, 4);
    HGDIOBJ def = U(GetCurrentObject)(f.mem, OBJ_BITMAP);
    EXPECT_EQ(UN(NtGdiSelectBitmap)(f.mem, bm), def);
    EXPECT_EQ(UN(NtGdiGetDCObject)(f.mem, K_DCOBJ_BITMAP), bm);
    EXPECT_EQ(UN(NtGdiSelectBitmap)(f.mem, def), bm);
    /* a pen handle is not a surface: the surface-type handle check fails -> NULL, selection untouched */
    CHECK(UN(NtGdiSelectBitmap)(f.mem, U(GetStockObject)(BLACK_PEN)) == NULL);
    EXPECT_EQ(UN(NtGdiGetDCObject)(f.mem, K_DCOBJ_BITMAP), def);
    U(DeleteObject)(bm);
    dcf_close(&f);
}

TEST(gdi_dc_objects_select_bitmap_dead_handle) {
    FX(f);
    HBITMAP bm = mk_bm(f.scr, 4, 4);
    U(DeleteObject)(bm);
    HGDIOBJ def = U(GetCurrentObject)(f.mem, OBJ_BITMAP);
    CHECK(U(SelectObject)(f.mem, bm) == NULL);
    EXPECT_EQ(U(GetCurrentObject)(f.mem, OBJ_BITMAP), def);
    dcf_close(&f);
}

TEST(gdi_dc_objects_select_palette_fails_invalid_function) {
    FX(f);
    /* SelectObjectImpl: palette-typed handles fall out of the dispatch to SetLastError(ERROR_INVALID_FUNCTION). */
    SET_ERR(0);
    CHECK(U(SelectObject)(f.mem, U(GetStockObject)(DEFAULT_PALETTE)) == NULL);
    EXPECT_ERR(ERROR_INVALID_FUNCTION);
    dcf_close(&f);
}

TEST(gdi_dc_objects_select_dc_handle_fails) {
    FX(f);
    HDC d2 = U(CreateCompatibleDC)(NULL);
    CHECK(U(SelectObject)(f.mem, d2) == NULL);
    U(DeleteDC)(d2);
    dcf_close(&f);
}

TEST(gdi_dc_objects_select_region_returns_complexity_and_sets_clip) {
    FX(f);
    HBITMAP bm = mk_bm(f.scr, 100, 100);
    U(SelectObject)(f.mem, bm);
    HRGN r1 = U(CreateRectRgn)(10, 10, 20, 20);
    HRGN r2 = U(CreateRectRgn)(40, 40, 50, 50);
    HRGN rc = U(CreateRectRgn)(0, 0, 1, 1);
    HRGN re = U(CreateRectRgn)(5, 5, 5, 5);
    U(CombineRgn)(rc, r1, r2, RGN_OR);
    RECT box;
    /* ExtSelectClipRgn(RGN_COPY) result is the region complexity, which SelectObject returns as the handle value. */
    EXPECT_EQ(U(SelectObject)(f.mem, r1), SIMPLEREGION);
    EXPECT_EQ(U(GetClipBox)(f.mem, &box), SIMPLEREGION);
    EXPECT_EQ(box.left, 10); EXPECT_EQ(box.top, 10); EXPECT_EQ(box.right, 20); EXPECT_EQ(box.bottom, 20);
    EXPECT_EQ(U(SelectObject)(f.mem, rc), COMPLEXREGION);
    EXPECT_EQ(U(GetClipBox)(f.mem, &box), COMPLEXREGION);
    EXPECT_EQ(box.left, 10); EXPECT_EQ(box.right, 50);
    EXPECT_EQ(U(SelectObject)(f.mem, re), NULLREGION);
    EXPECT_EQ(U(GetClipBox)(f.mem, &box), NULLREGION);
    /* the region is copied, not selected: still deletable */
    CHECK(U(DeleteObject)(r1)); CHECK(U(DeleteObject)(r2)); CHECK(U(DeleteObject)(rc)); CHECK(U(DeleteObject)(re));
    U(SelectObject)(f.mem, U(GetStockObject)(21));
    U(DeleteObject)(bm);
    dcf_close(&f);
}

TEST(gdi_dc_objects_select_null_and_junk_object) {
    FX(f);
    HDC dead = U(CreateCompatibleDC)(NULL);
    U(DeleteDC)(dead);
    HBITMAP deadbm = mk_bm(f.scr, 2, 2);
    U(DeleteObject)(deadbm);
    HGDIOBJ cur = U(GetCurrentObject)(f.mem, OBJ_BITMAP);
    CHECK(U(SelectObject)(f.mem, (HGDIOBJ)dead) == NULL);
    CHECK(U(SelectObject)(f.mem, deadbm) == NULL);    /* dead bitmap handle: entry validation fails */
    EXPECT_EQ(U(GetCurrentObject)(f.mem, OBJ_BITMAP), cur);
    SET_ERR(0);
    CHECK(U(SelectObject)(NULL, U(GetStockObject)(BLACK_PEN)) == NULL);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    dcf_close(&f);
}

/* ---- DC_ATTR dirty bits vs kernel state ---- */

TEST(gdi_dc_objects_brush_select_is_page_only_until_kernel_sync) {
    DCWANT(UN(NtGdiGetDCObject));
    FX(f);
    void *a = dc_attr(f.mem);
    if (!a) { dcf_close(&f); t_skip("DC_ATTR page unreadable"); return; }
    HBRUSH br = U(CreateSolidBrush)(RGB(0x10, 0x20, 0x30));
    U(SelectObject)(f.mem, br);
    /* SelectObjectImpl: writes hbrush at +0xa0 and sets dirty bit 0x1000; the kernel DC still has the old brush. */
    EXPECT_EQ(da_ptr(a, DA_HBRUSH), br);
    CHECK((da_u32(a, DA_DIRTY) & DIRTY_BRUSH_SEL) != 0);
    /* NtGdiGetDCObject applies the pending selection (brush selection, which clears 0x1000) before answering. */
    EXPECT_EQ(UN(NtGdiGetDCObject)(f.mem, K_DCOBJ_BRUSH), br);
    CHECK((da_u32(a, DA_DIRTY) & DIRTY_BRUSH_SEL) == 0);
    EXPECT_EQ(da_ptr(a, DA_HBRUSH), br);
    U(SelectObject)(f.mem, U(GetStockObject)(WHITE_BRUSH));
    UN(NtGdiGetDCObject)(f.mem, K_DCOBJ_BRUSH);
    U(DeleteObject)(br);
    dcf_close(&f);
}

TEST(gdi_dc_objects_pen_select_is_page_only_until_kernel_sync) {
    DCWANT(UN(NtGdiGetDCObject));
    FX(f);
    void *a = dc_attr(f.mem);
    if (!a) { dcf_close(&f); t_skip("DC_ATTR page unreadable"); return; }
    HPEN pen = U(CreatePen)(PS_SOLID, 2, RGB(0x30, 0x20, 0x10));
    U(SelectObject)(f.mem, pen);
    EXPECT_EQ(da_ptr(a, DA_HPEN), pen);
    CHECK((da_u32(a, DA_DIRTY) & DIRTY_PEN_SEL) != 0);
    EXPECT_EQ(UN(NtGdiGetDCObject)(f.mem, K_DCOBJ_PEN), pen);
    CHECK((da_u32(a, DA_DIRTY) & DIRTY_PEN_SEL) == 0);
    EXPECT_EQ(UN(NtGdiGetDCObject)(f.mem, K_DCOBJ_EXTPEN), pen);   /* 0x500000 maps to the same pen slot */
    U(SelectObject)(f.mem, U(GetStockObject)(BLACK_PEN));
    UN(NtGdiGetDCObject)(f.mem, K_DCOBJ_PEN);
    U(DeleteObject)(pen);
    dcf_close(&f);
}

TEST(gdi_dc_objects_syscall_getdcobject_kinds) {
    DCWANT(UN(NtGdiGetDCObject));
    FX(f);
    EXPECT_EQ(UN(NtGdiGetDCObject)(f.mem, K_DCOBJ_PEN), U(GetStockObject)(BLACK_PEN));
    EXPECT_EQ(UN(NtGdiGetDCObject)(f.mem, K_DCOBJ_BRUSH), U(GetStockObject)(WHITE_BRUSH));
    EXPECT_EQ(UN(NtGdiGetDCObject)(f.mem, K_DCOBJ_PAL), U(GetStockObject)(DEFAULT_PALETTE));
    EXPECT_EQ(UN(NtGdiGetDCObject)(f.mem, K_DCOBJ_BITMAP), U(GetStockObject)(21));
    HGDIOBJ ft = UN(NtGdiGetDCObject)(f.mem, K_DCOBJ_FONT);
    CHECK(ft != NULL);
    EXPECT_EQ(U(GetObjectType)(ft), OBJ_FONT);
    dcf_close(&f);
}

TEST(gdi_dc_objects_syscall_getdcobject_unknown_kinds_return_null) {
    DCWANT(UN(NtGdiGetDCObject));
    FX(f);
    /* Kinds the kernel switch does not know (colour space included - gdi32full reads that from the page) give NULL. */
    static const int k[] = { 0, 1, 2, 5, 6, 7, 0x10000, 0x90000, 0x200000, 0x400000, 0x600000, -1 };
    for (unsigned i = 0; i < sizeof k / sizeof *k; i++)
        CHECKF(UN(NtGdiGetDCObject)(f.mem, k[i]) == NULL, "kind 0x%x", k[i]);
    dcf_close(&f);
}

TEST(gdi_dc_objects_syscall_getdcobject_bad_dc) {
    DCWANT(UN(NtGdiGetDCObject));
    HDC dead = U(CreateCompatibleDC)(NULL);
    U(DeleteDC)(dead);
    HPEN pen = U(CreatePen)(PS_SOLID, 1, 0);
    static const int k[] = { K_DCOBJ_PEN, K_DCOBJ_BRUSH, K_DCOBJ_FONT, K_DCOBJ_PAL, K_DCOBJ_BITMAP };
    for (unsigned i = 0; i < sizeof k / sizeof *k; i++) {
        CHECKF(UN(NtGdiGetDCObject)(NULL, k[i]) == NULL, "NULL kind 0x%x", k[i]);
        CHECKF(UN(NtGdiGetDCObject)(dead, k[i]) == NULL, "dead kind 0x%x", k[i]);
        CHECKF(UN(NtGdiGetDCObject)((HDC)pen, k[i]) == NULL, "pen-as-dc kind 0x%x", k[i]);
    }
    U(DeleteObject)(pen);
}

TEST(gdi_dc_objects_syscall_select_brush_pen_return_previous) {
    DCWANT(UN(NtGdiSelectBrush)); DCWANT(UN(NtGdiSelectPen)); DCWANT(UN(NtGdiGetDCObject));
    FX(f);
    HBRUSH br = U(CreateSolidBrush)(RGB(1, 1, 1));
    HPEN pen = U(CreatePen)(PS_SOLID, 1, RGB(2, 2, 2));
    /* SelectObject on a brush returns the DC_ATTR brush that was current. */
    EXPECT_EQ(UN(NtGdiSelectBrush)(f.mem, br), U(GetStockObject)(WHITE_BRUSH));
    EXPECT_EQ(UN(NtGdiGetDCObject)(f.mem, K_DCOBJ_BRUSH), br);
    EXPECT_EQ(da_ptr(dc_attr(f.mem), DA_HBRUSH), br);
    EXPECT_EQ(UN(NtGdiSelectPen)(f.mem, pen), U(GetStockObject)(BLACK_PEN));
    EXPECT_EQ(UN(NtGdiGetDCObject)(f.mem, K_DCOBJ_PEN), pen);
    EXPECT_EQ(UN(NtGdiSelectBrush)(f.mem, U(GetStockObject)(WHITE_BRUSH)), br);
    EXPECT_EQ(UN(NtGdiSelectPen)(f.mem, U(GetStockObject)(BLACK_PEN)), pen);
    CHECK(U(DeleteObject)(br)); CHECK(U(DeleteObject)(pen));
    dcf_close(&f);
}

TEST(gdi_dc_objects_syscall_select_brush_pen_bad_args) {
    DCWANT(UN(NtGdiSelectBrush)); DCWANT(UN(NtGdiSelectPen));
    FX(f);
    HBRUSH dead = U(CreateSolidBrush)(RGB(1, 1, 1));
    U(DeleteObject)(dead); U(GdiFlush)();
    HGDIOBJ b0 = U(GetCurrentObject)(f.mem, OBJ_BRUSH), p0 = U(GetCurrentObject)(f.mem, OBJ_PEN);
    /* A handle that does not lock as a brush leaves the DC alone and the previous-handle slot reports 0. */
    CHECK(UN(NtGdiSelectBrush)(f.mem, dead) == NULL);
    CHECK(UN(NtGdiSelectPen)(f.mem, dead) == NULL);
    CHECK(UN(NtGdiSelectBrush)(NULL, U(GetStockObject)(BLACK_BRUSH)) == NULL);
    CHECK(UN(NtGdiSelectPen)(NULL, U(GetStockObject)(BLACK_PEN)) == NULL);
    EXPECT_EQ(U(GetCurrentObject)(f.mem, OBJ_BRUSH), b0);
    EXPECT_EQ(U(GetCurrentObject)(f.mem, OBJ_PEN), p0);
    dcf_close(&f);
}

TEST(gdi_dc_objects_syscall_select_font_returns_previous) {
    DCWANT(UN(NtGdiSelectFont)); DCWANT(UN(NtGdiGetDCObject));
    FX(f);
    LOGFONTW lf; memset(&lf, 0, sizeof lf); lf.lfHeight = -14; lf.lfCharSet = DEFAULT_CHARSET;
    lstrcpyW(lf.lfFaceName, L"Arial");
    HFONT a = U(CreateFontIndirectW)(&lf);
    HGDIOBJ f0 = UN(NtGdiGetDCObject)(f.mem, K_DCOBJ_FONT);
    /* SelectObject on a font returns the handle of the DC's current LFONT. */
    EXPECT_EQ(UN(NtGdiSelectFont)(f.mem, a), f0);
    EXPECT_EQ(UN(NtGdiSelectFont)(f.mem, a), a);          /* same font: returns it, no change */
    EXPECT_EQ(UN(NtGdiGetDCObject)(f.mem, K_DCOBJ_FONT), a);
    EXPECT_EQ(UN(NtGdiSelectFont)(f.mem, f0), a);
    CHECK(UN(NtGdiSelectFont)(NULL, a) == NULL);
    U(DeleteObject)(a);
    dcf_close(&f);
}

/* ---- deferred deletion of a selected brush ---- */

TEST(gdi_dc_objects_delete_synced_brush_is_deferred_while_selected) {
    DCWANT(UN(NtGdiGetDCObject));
    FX(f);
    HBRUSH br = U(CreateSolidBrush)(RGB(0x44, 0x55, 0x66));
    U(SelectObject)(f.mem, br);
    UN(NtGdiGetDCObject)(f.mem, K_DCOBJ_BRUSH);        /* kernel DC now holds a share reference on the brush */
    U(DeleteObject)(br);
    U(GdiFlush)();                                      /* DeleteObject on a brush may sit in the TEB batch */
    /* bDeleteBrush: share count != 0 -> mark for deletion (flag 2) and keep the object. */
    EXPECT_EQ(U(GetObjectType)(br), OBJ_BRUSH);
    EXPECT_EQ(U(GetCurrentObject)(f.mem, OBJ_BRUSH), br);
    U(SelectObject)(f.mem, U(GetStockObject)(WHITE_BRUSH));
    UN(NtGdiGetDCObject)(f.mem, K_DCOBJ_BRUSH);
    t_note("selected-then-deleted brush after unselect: GetObjectType=%lu", U(GetObjectType)(br));
    dcf_close(&f);
}

/* ---- DC_BRUSH / DC_PEN ---- */

TEST(gdi_dc_objects_dc_brush_pen_default_colors) {
    FX(f);
    /* Documented defaults: DC brush white, DC pen black. */
    EXPECT_EQ(U(GetDCBrushColor)(f.mem), RGB(255, 255, 255));
    EXPECT_EQ(U(GetDCPenColor)(f.mem), RGB(0, 0, 0));
    dcf_close(&f);
}

TEST(gdi_dc_objects_set_dc_brush_color_returns_previous_and_updates_page) {
    FX(f);
    void *a = dc_attr(f.mem);
    COLORREF c1 = RGB(0x12, 0x34, 0x56), c2 = RGB(0xa0, 0xb0, 0xc0);
    EXPECT_EQ(U(SetDCBrushColor)(f.mem, c1), RGB(255, 255, 255));
    EXPECT_EQ(U(GetDCBrushColor)(f.mem), c1);
    if (a) {
        /* gdi32full!SetDCBrushColor: +0xc4 holds the colour, +0xc0 its (ICM-translated) copy, dirty bit 0 flags the change. */
        EXPECT_EQ(da_u32(a, DA_DCBRUSH), c1);
        EXPECT_EQ(da_u32(a, DA_DCBRUSH_X), c1);
        CHECK((da_u32(a, DA_DIRTY) & 1) != 0);
    }
    EXPECT_EQ(U(SetDCBrushColor)(f.mem, c2), c1);
    EXPECT_EQ(U(GetDCBrushColor)(f.mem), c2);
    dcf_close(&f);
}

TEST(gdi_dc_objects_set_dc_pen_color_returns_previous_and_updates_page) {
    FX(f);
    void *a = dc_attr(f.mem);
    COLORREF c1 = RGB(0x65, 0x43, 0x21), c2 = RGB(0x0a, 0x0b, 0x0c);
    EXPECT_EQ(U(SetDCPenColor)(f.mem, c1), RGB(0, 0, 0));
    EXPECT_EQ(U(GetDCPenColor)(f.mem), c1);
    if (a) {
        EXPECT_EQ(da_u32(a, DA_DCPEN), c1);
        EXPECT_EQ(da_u32(a, DA_DCPEN_X), c1);
        CHECK((da_u32(a, DA_DIRTY) & 2) != 0);
    }
    EXPECT_EQ(U(SetDCPenColor)(f.mem, c2), c1);
    dcf_close(&f);
}

TEST(gdi_dc_objects_dc_brush_pen_colors_are_per_dc_and_independent) {
    FX(f);
    HDC d2 = U(CreateCompatibleDC)(f.scr);
    U(SetDCBrushColor)(f.mem, RGB(1, 2, 3));
    U(SetDCPenColor)(f.mem, RGB(4, 5, 6));
    EXPECT_EQ(U(GetDCBrushColor)(d2), RGB(255, 255, 255));
    EXPECT_EQ(U(GetDCPenColor)(d2), RGB(0, 0, 0));
    EXPECT_EQ(U(GetDCBrushColor)(f.mem), RGB(1, 2, 3));
    EXPECT_EQ(U(GetDCPenColor)(f.mem), RGB(4, 5, 6));
    U(DeleteDC)(d2);
    dcf_close(&f);
}

TEST(gdi_dc_objects_dc_brush_pen_invalid_dc) {
    HDC dead = U(CreateCompatibleDC)(NULL);
    U(DeleteDC)(dead);
    HDC bad[2] = { NULL, dead };
    for (int i = 0; i < 2; i++) {
        /* gdi32full: entry validation fails -> CLR_INVALID with ERROR_INVALID_PARAMETER. */
        SET_ERR(0); EXPECT_EQ(U(SetDCBrushColor)(bad[i], RGB(1, 1, 1)), CLR_INVALID); EXPECT_ERR(ERROR_INVALID_PARAMETER);
        SET_ERR(0); EXPECT_EQ(U(SetDCPenColor)(bad[i], RGB(1, 1, 1)), CLR_INVALID); EXPECT_ERR(ERROR_INVALID_PARAMETER);
        SET_ERR(0); EXPECT_EQ(U(GetDCBrushColor)(bad[i]), CLR_INVALID); EXPECT_ERR(ERROR_INVALID_PARAMETER);
        SET_ERR(0); EXPECT_EQ(U(GetDCPenColor)(bad[i]), CLR_INVALID); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
}

/* Draw with the DC brush/pen into a 32bpp DIB section and read the pixel back from the mapped memory: the kernel
 * must have picked up the colour stored in the page (dirty bits) when it rendered. */
static uint32_t *mk_dib(HDC dc, int w, int h, HBITMAP *out) {
    BITMAPINFO bi; memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    *out = U(CreateDIBSection)(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    return (uint32_t *)bits;
}

TEST(gdi_dc_objects_dc_brush_renders_page_color_into_dib) {
    FX(f);
    HBITMAP dib; uint32_t *px = mk_dib(f.scr, 4, 4, &dib);
    if (!px) { U(DeleteObject)(dib); dcf_close(&f); t_skip("no DIB section"); return; }
    HGDIOBJ def = U(SelectObject)(f.mem, dib);
    U(SelectObject)(f.mem, U(GetStockObject)(DC_BRUSH));
    U(SetDCBrushColor)(f.mem, RGB(0x12, 0x34, 0x56));
    CHECK(U(PatBlt)(f.mem, 0, 0, 4, 4, PATCOPY));
    U(GdiFlush)();
    EXPECT_EQ(px[0] & 0xffffff, 0x123456);
    EXPECT_EQ(px[15] & 0xffffff, 0x123456);
    U(SetDCBrushColor)(f.mem, RGB(0xfe, 0xdc, 0xba));
    CHECK(U(PatBlt)(f.mem, 1, 1, 2, 2, PATCOPY));
    U(GdiFlush)();
    EXPECT_EQ(px[5] & 0xffffff, 0xfedcba);
    EXPECT_EQ(px[0] & 0xffffff, 0x123456);
    U(SelectObject)(f.mem, def);
    U(DeleteObject)(dib);
    dcf_close(&f);
}

TEST(gdi_dc_objects_dc_pen_renders_page_color_into_dib) {
    FX(f);
    HBITMAP dib; uint32_t *px = mk_dib(f.scr, 8, 4, &dib);
    if (!px) { U(DeleteObject)(dib); dcf_close(&f); t_skip("no DIB section"); return; }
    HGDIOBJ def = U(SelectObject)(f.mem, dib);
    U(PatBlt)(f.mem, 0, 0, 8, 4, BLACKNESS);
    U(SelectObject)(f.mem, U(GetStockObject)(DC_PEN));
    U(SetDCPenColor)(f.mem, RGB(0xc8, 0x64, 0x32));
    U(MoveToEx)(f.mem, 0, 1, NULL);
    CHECK(U(LineTo)(f.mem, 6, 1));
    U(GdiFlush)();
    EXPECT_EQ(px[1 * 8 + 2] & 0xffffff, 0xc86432);
    EXPECT_EQ(px[0 * 8 + 2] & 0xffffff, 0);
    U(SelectObject)(f.mem, def);
    U(DeleteObject)(dib);
    dcf_close(&f);
}

TEST(gdi_dc_objects_selected_solid_brush_renders_exact_color_into_dib) {
    FX(f);
    HBITMAP dib; uint32_t *px = mk_dib(f.scr, 2, 2, &dib);
    if (!px) { U(DeleteObject)(dib); dcf_close(&f); t_skip("no DIB section"); return; }
    HGDIOBJ def = U(SelectObject)(f.mem, dib);
    HBRUSH br = U(CreateSolidBrush)(RGB(0x01, 0x80, 0xfe));
    U(SelectObject)(f.mem, br);
    U(PatBlt)(f.mem, 0, 0, 2, 2, PATCOPY);
    U(GdiFlush)();
    EXPECT_EQ(px[3] & 0xffffff, 0x0180fe);
    U(SelectObject)(f.mem, U(GetStockObject)(WHITE_BRUSH));
    U(SelectObject)(f.mem, def);
    U(GdiFlush)();
    U(DeleteObject)(br); U(DeleteObject)(dib);
    dcf_close(&f);
}

/* ---- stock objects ---- */

struct stock { int idx; unsigned t7; DWORD objtype; };
static const struct stock kStock[] = {
    { WHITE_BRUSH, T7_BRUSH, OBJ_BRUSH }, { LTGRAY_BRUSH, T7_BRUSH, OBJ_BRUSH }, { GRAY_BRUSH, T7_BRUSH, OBJ_BRUSH },
    { DKGRAY_BRUSH, T7_BRUSH, OBJ_BRUSH }, { BLACK_BRUSH, T7_BRUSH, OBJ_BRUSH }, { NULL_BRUSH, T7_BRUSH, OBJ_BRUSH },
    { WHITE_PEN, T7_PEN, OBJ_PEN }, { BLACK_PEN, T7_PEN, OBJ_PEN }, { NULL_PEN, T7_PEN, OBJ_PEN },
    { OEM_FIXED_FONT, T7_FONT, OBJ_FONT }, { ANSI_FIXED_FONT, T7_FONT, OBJ_FONT }, { ANSI_VAR_FONT, T7_FONT, OBJ_FONT },
    { SYSTEM_FONT, T7_FONT, OBJ_FONT }, { DEVICE_DEFAULT_FONT, T7_FONT, OBJ_FONT }, { DEFAULT_PALETTE, T7_PALETTE, OBJ_PAL },
    { SYSTEM_FIXED_FONT, T7_FONT, OBJ_FONT }, { DEFAULT_GUI_FONT, T7_FONT, OBJ_FONT },
    { DC_BRUSH, T7_BRUSH, OBJ_BRUSH }, { DC_PEN, T7_PEN, OBJ_PEN }, { 21, T7_BITMAP, OBJ_BITMAP },
};

TEST(gdi_dc_objects_stock_objects_types_and_stability) {
    REQUIRE(U(GetStockObject));
    for (unsigned i = 0; i < sizeof kStock / sizeof *kStock; i++) {
        HGDIOBJ h = U(GetStockObject)(kStock[i].idx);
        CHECKF(h != NULL, "stock %d", kStock[i].idx);
        if (!h) continue;
        CHECKF(H_STOCKED(h), "stock %d handle %p has no stock bit", kStock[i].idx, h);
        CHECKF(H_TYPE7(h) == kStock[i].t7, "stock %d type 0x%x", kStock[i].idx, H_TYPE7(h));
        CHECKF(U(GetObjectType)(h) == kStock[i].objtype, "stock %d GetObjectType %lu", kStock[i].idx, U(GetObjectType)(h));
        CHECKF(U(GetStockObject)(kStock[i].idx) == h, "stock %d unstable", kStock[i].idx);
    }
}

TEST(gdi_dc_objects_stock_object_out_of_range_is_null) {
    REQUIRE(U(GetStockObject));
    /* gdi32full!GetStockObject: (uint)i > 0x15 returns NULL (no last error). */
    static const int bad[] = { 22, 23, 31, 32, 100, 0x7fffffff, -1, (int)0x80000000 };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        SET_ERR(0);
        CHECKF(U(GetStockObject)(bad[i]) == NULL, "stock %d", bad[i]);
        EXPECT_ERR(0);
    }
}

TEST(gdi_dc_objects_delete_stock_objects_is_true_and_keeps_them) {
    DCWANT(UN(NtGdiDeleteObjectApp));
    for (unsigned i = 0; i < sizeof kStock / sizeof *kStock; i++) {
        HGDIOBJ h = U(GetStockObject)(kStock[i].idx);
        if (!h) continue;
        /* NtGdiDeleteObjectApp returns 1 immediately when the handle has the stock bit (bit 23). */
        CHECKF(UN(NtGdiDeleteObjectApp)(h) != 0, "syscall on stock %d", kStock[i].idx);
        BOOL r = U(DeleteObject)(h);
        t_note("DeleteObject(stock %d) = %d", kStock[i].idx, r);
        CHECKF(U(GetObjectType)(h) == kStock[i].objtype, "stock %d died", kStock[i].idx);
        CHECKF(U(GetStockObject)(kStock[i].idx) == h, "stock %d changed", kStock[i].idx);
    }
}

TEST(gdi_dc_objects_stock_brush_pen_logical_contents) {
    struct { int idx; COLORREF c; UINT style; } br[] = {
        { WHITE_BRUSH, RGB(255, 255, 255), BS_SOLID }, { BLACK_BRUSH, RGB(0, 0, 0), BS_SOLID }, { NULL_BRUSH, 0, BS_NULL },
    };
    for (unsigned i = 0; i < 3; i++) {
        LOGBRUSH lb; memset(&lb, 0xcc, sizeof lb);
        CHECK(U(GetObjectW)(U(GetStockObject)(br[i].idx), sizeof lb, &lb) == sizeof lb);
        EXPECT_EQ(lb.lbStyle, br[i].style);
        if (br[i].style == BS_SOLID) EXPECT_EQ(lb.lbColor, br[i].c);
    }
    struct { int idx; COLORREF c; UINT style; } pn[] = {
        { WHITE_PEN, RGB(255, 255, 255), PS_SOLID }, { BLACK_PEN, RGB(0, 0, 0), PS_SOLID }, { NULL_PEN, 0, PS_NULL },
    };
    for (unsigned i = 0; i < 3; i++) {
        LOGPEN lp; memset(&lp, 0xcc, sizeof lp);
        CHECK(U(GetObjectW)(U(GetStockObject)(pn[i].idx), sizeof lp, &lp) == sizeof lp);
        EXPECT_EQ(lp.lopnStyle, pn[i].style);
        if (pn[i].style == PS_SOLID) EXPECT_EQ(lp.lopnColor, pn[i].c);
    }
}

TEST(gdi_dc_objects_stock_pen_selected_in_dc_cannot_be_deleted_by_dc_delete) {
    FX(f);
    HGDIOBJ p = U(GetStockObject)(WHITE_PEN);
    EXPECT_EQ(U(SelectObject)(f.mem, p), U(GetStockObject)(BLACK_PEN));
    U(DeleteDC)(f.mem); f.mem = NULL;
    EXPECT_EQ(U(GetObjectType)(p), OBJ_PEN);
    dcf_close(&f);
}
