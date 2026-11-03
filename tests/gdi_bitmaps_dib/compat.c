/* NtGdiCreateCompatibleBitmap (win32kfull 0x106fc0), NtGdiSelectBitmap (0xf7940) and
 * NtGdiGetDCforBitmap (0x103210).
 *  - Nt wrapper: the height argument is masked with 0xf0ffffff (bits 24..27 are CCB_ flag space and are dropped).
 *  - the kernel compatible-bitmap path: cx < 1, cy < 1 or cx*cy > 0x3fffffff -> ERROR_INVALID_PARAMETER; hdc == NULL -> a 1x1-bpp
 *    bitmap of the requested size (1 plane, 1 bpp); otherwise a surface in the DC's format.
 *  - gdi32full!CreateCompatibleBitmap: zero cx/cy -> stock 1x1 bitmap; a memory DC with a DIB section selected gets a new DIB section.
 *  - NtGdiGetDCforBitmap reads SURFACE+0xa0 under a share lock of type 5: the DC a bitmap is selected into, or NULL. */
#include "bd.h"

/* ---- direct syscall behaviour -------------------------------------------------------------------------------------------- */
TEST(bd_ccb_sys_null_dc_gives_mono_bitmap) {
    BD_NEED(); BD_NEED_NT(NtGdiCreateCompatibleBitmap);
    HBITMAP hb = UN(NtGdiCreateCompatibleBitmap)(NULL, 13, 7);
    CHECK(hb != NULL); if (!hb) return;
    BITMAP bm; CHECK(bd_getobj(hb, &bm) == sizeof bm);
    CHECK(bm.bmWidth == 13 && bm.bmHeight == 7 && bm.bmBitsPixel == 1 && bm.bmPlanes == 1 && bm.bmBits == NULL);
    U(DeleteObject)(hb);
}
#define CC_BAD(X) X(cx0, 0, 4) X(cy0, 4, 0) X(cx_neg, -1, 4) X(cy_neg, 4, -1) X(both_neg, -4, -4) X(cx_min, (int)0x80000000, 4) \
    X(area_over, 0x8000, 0x8000) X(area_2_32, 0x10000, 0x10000) X(wide, 0x40000000, 1) X(tall, 1, 0x40000000) X(cy_hi_nibble, 4, 0x10000004)
#define X(nm, W, H) \
TEST(bd_ccb_sys_reject_##nm) { \
    BD_NEED(); BD_NEED_NT(NtGdiCreateCompatibleBitmap); \
    SET_ERR(0); \
    HBITMAP hb = UN(NtGdiCreateCompatibleBitmap)(NULL, W, H); \
    CHECKF(hb == NULL, "got %p", hb); \
    if (hb) { U(DeleteObject)(hb); return; } \
    EXPECT_ERR(BD_ERR_INVALID_PARAM); \
}
CC_BAD(X)
#undef X
TEST(bd_ccb_sys_height_flag_bits_dropped) {
    BD_NEED(); BD_NEED_NT(NtGdiCreateCompatibleBitmap);
    /* 0xf0ffffff clears bits 24..27 of cy before the size check */
    static const unsigned flags[] = { 0x01000000, 0x02000000, 0x04000000, 0x08000000, 0x0f000000 };
    for (size_t i = 0; i < sizeof flags / sizeof flags[0]; i++) {
        HBITMAP hb = UN(NtGdiCreateCompatibleBitmap)(NULL, 5, (int)(4 | flags[i]));
        CHECKF(hb != NULL, "flags 0x%x", flags[i]);
        if (!hb) continue;
        BITMAP bm; bd_getobj(hb, &bm);
        CHECKF(bm.bmWidth == 5 && bm.bmHeight == 4, "flags 0x%x -> %ldx%ld", flags[i], bm.bmWidth, bm.bmHeight);
        U(DeleteObject)(hb);
    }
}
TEST(bd_ccb_sys_invalid_dc_handle) {
    BD_NEED(); BD_NEED_NT(NtGdiCreateCompatibleBitmap);
    HDC dc = bd_mem_dc(); REQUIRE(dc);
    U(DeleteDC)(dc);
    /* locking the DC fails for a dead DC: no bitmap */
    HBITMAP hb = UN(NtGdiCreateCompatibleBitmap)(dc, 4, 4);
    CHECK(hb == NULL); if (hb) U(DeleteObject)(hb);
    HBRUSH br = U(CreateSolidBrush)(RGB(1, 2, 3));
    if (br) { hb = UN(NtGdiCreateCompatibleBitmap)((HDC)br, 4, 4); CHECK(hb == NULL); if (hb) U(DeleteObject)(hb); U(DeleteObject)(br); }
}
TEST(bd_ccb_sys_mem_dc_default_bitmap_mono) {
    BD_NEED(); BD_NEED_NT(NtGdiCreateCompatibleBitmap);
    HDC dc = bd_mem_dc(); REQUIRE(dc);
    HBITMAP hb = UN(NtGdiCreateCompatibleBitmap)(dc, 9, 5);
    CHECK(hb != NULL);
    if (hb) { BITMAP bm; bd_getobj(hb, &bm); CHECK(bm.bmBitsPixel == 1 && bm.bmWidth == 9 && bm.bmHeight == 5); U(DeleteObject)(hb); }
    U(DeleteDC)(dc);
}
TEST(bd_ccb_sys_display_dc_matches_device) {
    BD_NEED(); BD_NEED_NT(NtGdiCreateCompatibleBitmap); REQUIRE(U(GetDeviceCaps));
    HDC dc = bd_screen_dc();
    if (!dc) { t_skip("no display DC"); return; }
    HBITMAP hb = UN(NtGdiCreateCompatibleBitmap)(dc, 17, 9);
    CHECK(hb != NULL);
    if (hb) {
        BITMAP bm; bd_getobj(hb, &bm);
        int planes = U(GetDeviceCaps)(dc, PLANES), bpp = U(GetDeviceCaps)(dc, BITSPIXEL);
        CHECK(bm.bmWidth == 17 && bm.bmHeight == 9);
        CHECKF(bm.bmPlanes * bm.bmBitsPixel == planes * bpp, "bitmap %u*%u device %d*%d", bm.bmPlanes, bm.bmBitsPixel, planes, bpp);
        CHECK(bm.bmBits == NULL);
        U(DeleteObject)(hb);
    }
    U(DeleteDC)(dc);
}

/* ---- userland path ------------------------------------------------------------------------------------------------------ */
TEST(bd_ccb_null_dc_returns_null) {
    BD_NEED(); REQUIRE(U(CreateCompatibleBitmap));
    /* gdi32full!CreateCompatibleBitmap validates hdc against the shared cell table first: entry 0 is not a DC */
    HBITMAP hb = U(CreateCompatibleBitmap)(NULL, 4, 4);
    CHECK(hb == NULL); if (hb) U(DeleteObject)(hb);
}
TEST(bd_ccb_zero_size_returns_stock_bitmap) {
    BD_NEED(); REQUIRE(U(CreateCompatibleBitmap)); REQUIRE(U(GetStockObject));
    HDC dc = bd_mem_dc(); REQUIRE(dc);
    CHECK(U(CreateCompatibleBitmap)(dc, 0, 5) == U(GetStockObject)(0x15));
    CHECK(U(CreateCompatibleBitmap)(dc, 5, 0) == U(GetStockObject)(0x15));
    CHECK(U(CreateCompatibleBitmap)(dc, 0, 0) == U(GetStockObject)(0x15));
    U(DeleteDC)(dc);
}
TEST(bd_ccb_negative_size_fails) {
    BD_NEED(); REQUIRE(U(CreateCompatibleBitmap));
    HDC dc = bd_mem_dc(); REQUIRE(dc);
    SET_ERR(0);
    HBITMAP hb = U(CreateCompatibleBitmap)(dc, -3, 4);
    CHECK(hb == NULL); if (hb) U(DeleteObject)(hb); else EXPECT_ERR(BD_ERR_INVALID_PARAM);
    hb = U(CreateCompatibleBitmap)(dc, 3, -4);
    CHECK(hb == NULL); if (hb) U(DeleteObject)(hb);
    U(DeleteDC)(dc);
}
TEST(bd_ccb_memory_dc_is_monochrome) {
    BD_NEED(); REQUIRE(U(CreateCompatibleBitmap));
    HDC dc = bd_mem_dc(); REQUIRE(dc);
    HBITMAP hb = U(CreateCompatibleBitmap)(dc, 11, 3);
    CHECK(hb != NULL);
    if (hb) { BITMAP bm; CHECK(bd_getobj(hb, &bm) == sizeof bm); CHECK(bm.bmBitsPixel == 1 && bm.bmPlanes == 1 && bm.bmWidth == 11 && bm.bmHeight == 3); U(DeleteObject)(hb); }
    U(DeleteDC)(dc);
}
TEST(bd_ccb_screen_dc_matches_device_format) {
    BD_NEED(); REQUIRE(U(CreateCompatibleBitmap)); REQUIRE(U(GetDeviceCaps));
    HDC dc = bd_screen_dc();
    if (!dc) { t_skip("no display DC"); return; }
    HBITMAP hb = U(CreateCompatibleBitmap)(dc, 21, 13);
    CHECK(hb != NULL);
    if (hb) {
        BITMAP bm; CHECK(bd_getobj(hb, &bm) == sizeof bm);
        CHECK(bm.bmPlanes * bm.bmBitsPixel == U(GetDeviceCaps)(dc, PLANES) * U(GetDeviceCaps)(dc, BITSPIXEL));
        CHECK(bm.bmWidth == 21 && bm.bmHeight == 13 && bm.bmBits == NULL && bm.bmType == 0);
        CHECK(bm.bmWidthBytes == (LONG)bd_ddb_stride(21, bm.bmBitsPixel));
        t_note("screen compatible bitmap: %u bpp", bm.bmBitsPixel);
        U(DeleteObject)(hb);
    }
    U(DeleteDC)(dc);
}
TEST(bd_ccb_screen_dc_bitmap_is_ddb_not_section) {
    BD_NEED(); REQUIRE(U(CreateCompatibleBitmap));
    HDC dc = bd_screen_dc();
    if (!dc) { t_skip("no display DC"); return; }
    HBITMAP hb = U(CreateCompatibleBitmap)(dc, 8, 8); REQUIRE(hb);
    DIBSECTION ds; CHECK(U(GetObjectW)(hb, sizeof ds, &ds) == (int)sizeof(BITMAP));
    U(DeleteObject)(hb); U(DeleteDC)(dc);
}
TEST(bd_ccb_screen_dc_size_via_getbitmapbits) {
    BD_NEED(); REQUIRE(U(CreateCompatibleBitmap)); BD_NEED_NT(NtGdiGetBitmapBits);
    HDC dc = bd_screen_dc();
    if (!dc) { t_skip("no display DC"); return; }
    HBITMAP hb = U(CreateCompatibleBitmap)(dc, 21, 13); REQUIRE(hb);
    BITMAP bm; bd_getobj(hb, &bm);
    LONG n = UN(NtGdiGetBitmapBits)(hb, 0x7fffffff, NULL);
    CHECKF(n == (LONG)(bd_ddb_stride(21, bm.bmBitsPixel) * 13), "size %ld", n);
    U(DeleteObject)(hb); U(DeleteDC)(dc);
}
TEST(bd_ccb_screen_dc_pixel_roundtrip) {
    BD_NEED(); REQUIRE(U(CreateCompatibleBitmap)); REQUIRE(U(SelectObject)); REQUIRE(U(SetPixel));
    HDC sdc = bd_screen_dc(), mdc = bd_mem_dc();
    if (!sdc || !mdc) { if (sdc) U(DeleteDC)(sdc); if (mdc) U(DeleteDC)(mdc); t_skip("no DCs"); return; }
    HBITMAP hb = U(CreateCompatibleBitmap)(sdc, 4, 4); REQUIRE(hb);
    HGDIOBJ old = U(SelectObject)(mdc, hb);
    U(SetPixel)(mdc, 1, 1, RGB(255, 0, 0));
    COLORREF c = U(GetPixel)(mdc, 1, 1);
    int bpp = U(GetDeviceCaps)(sdc, BITSPIXEL);
    if (bpp >= 24) CHECKF(c == RGB(255, 0, 0), "pixel 0x%08lx", c);
    else t_note("pixel 0x%08lx at %d bpp", c, bpp);
    U(SelectObject)(mdc, old);
    U(DeleteObject)(hb); U(DeleteDC)(mdc); U(DeleteDC)(sdc);
}

/* memory DC with a DIB section selected: the compatible bitmap is a DIB section of the same depth (gdi32full CreateCompatibleBitmap
 * reads the selected DIBSECTION and calls CreateDIBSectionInternal) */
#define CC_DIB(X) X(b4, 4) X(b8, 8) X(b16, 16) X(b24, 24) X(b32, 32)
#define X(nm, bpp) \
TEST(bd_ccb_dib_selected_gives_dib_section_##nm) { \
    BD_NEED(); REQUIRE(U(CreateCompatibleBitmap)); REQUIRE(U(SelectObject)); \
    HDC dc = bd_mem_dc(); void *bits = NULL; HBITMAP src = bd_dib(8, 8, bpp, &bits); \
    if (!dc || !src) { if (dc) U(DeleteDC)(dc); if (src) U(DeleteObject)(src); t_skip("setup"); return; } \
    HGDIOBJ old = U(SelectObject)(dc, src); \
    HBITMAP hb = U(CreateCompatibleBitmap)(dc, 12, 6); \
    CHECK(hb != NULL); \
    if (hb) { \
        DIBSECTION ds; int n = bd_getdib(hb, &ds); \
        CHECKF(n == (int)sizeof ds, "GetObject %d: a DDB came back", n); \
        if (n == (int)sizeof ds) { \
            CHECK(ds.dsBm.bmBitsPixel == (bpp) && ds.dsBm.bmWidth == 12 && ds.dsBm.bmHeight == 6); \
            CHECK(ds.dsBm.bmBits != NULL && ds.dsBm.bmBits != bits); \
            CHECK(ds.dsBm.bmWidthBytes == (LONG)bd_stride(12, bpp)); } \
        U(DeleteObject)(hb); } \
    U(SelectObject)(dc, old); U(DeleteObject)(src); U(DeleteDC)(dc); \
}
CC_DIB(X)
#undef X
TEST(bd_ccb_dib_deselected_gives_mono_again) {
    BD_NEED(); REQUIRE(U(CreateCompatibleBitmap)); REQUIRE(U(SelectObject));
    HDC dc = bd_mem_dc(); HBITMAP src = bd_dib(8, 8, 32, NULL);
    if (!dc || !src) { if (dc) U(DeleteDC)(dc); if (src) U(DeleteObject)(src); t_skip("setup"); return; }
    HGDIOBJ old = U(SelectObject)(dc, src);
    U(SelectObject)(dc, old);
    HBITMAP hb = U(CreateCompatibleBitmap)(dc, 5, 5); REQUIRE(hb);
    BITMAP bm; bd_getobj(hb, &bm);
    CHECK(bm.bmBitsPixel == 1);
    U(DeleteObject)(hb); U(DeleteObject)(src); U(DeleteDC)(dc);
}
TEST(bd_ccb_ddb_selected_follows_its_format_note) {
    BD_NEED(); REQUIRE(U(CreateCompatibleBitmap)); REQUIRE(U(SelectObject));
    HDC dc = bd_mem_dc(); HBITMAP src = U(CreateBitmap)(4, 4, 1, 32, NULL);
    if (!dc || !src) { if (dc) U(DeleteDC)(dc); if (src) U(DeleteObject)(src); t_skip("setup"); return; }
    HGDIOBJ old = U(SelectObject)(dc, src);
    HBITMAP hb = U(CreateCompatibleBitmap)(dc, 5, 5); REQUIRE(hb);
    BITMAP bm; bd_getobj(hb, &bm);
    t_note("compatible bitmap of a memory DC holding a 32bpp DDB: %u bpp", bm.bmBitsPixel);
    U(SelectObject)(dc, old);
    U(DeleteObject)(hb); U(DeleteObject)(src); U(DeleteDC)(dc);
}
TEST(bd_ccb_handles_are_distinct_and_typed) {
    BD_NEED(); REQUIRE(U(CreateCompatibleBitmap));
    HDC dc = bd_screen_dc();
    if (!dc) { t_skip("no display DC"); return; }
    HBITMAP a = U(CreateCompatibleBitmap)(dc, 4, 4), b = U(CreateCompatibleBitmap)(dc, 4, 4);
    CHECK(a && b && a != b);
    if (a) { CHECK(BD_TYPE7(a) == 5 && U(GetObjectType)(a) == OBJ_BITMAP); }
    if (a) U(DeleteObject)(a);
    if (b) U(DeleteObject)(b);
    U(DeleteDC)(dc);
}
TEST(bd_ccb_counts_one_gdi_object) {
    BD_NEED(); REQUIRE(U(CreateCompatibleBitmap)); REQUIRE(U(GetGuiResources));
    HDC dc = bd_screen_dc();
    if (!dc) { t_skip("no display DC"); return; }
    DWORD a = U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS);
    HBITMAP hb = U(CreateCompatibleBitmap)(dc, 4, 4);
    CHECK(U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS) == a + 1);
    U(DeleteObject)(hb);
    CHECK(U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS) == a);
    U(DeleteDC)(dc);
}

/* ---- selection rules ----------------------------------------------------------------------------------------------------- */
TEST(bd_sel_default_bitmap_returned_as_previous) {
    BD_NEED(); REQUIRE(U(SelectObject)); REQUIRE(U(GetCurrentObject));
    HDC dc = bd_mem_dc(); HBITMAP hb = U(CreateBitmap)(4, 4, 1, 1, NULL);
    if (!dc || !hb) { if (dc) U(DeleteDC)(dc); if (hb) U(DeleteObject)(hb); t_skip("setup"); return; }
    HGDIOBJ cur = U(GetCurrentObject)(dc, OBJ_BITMAP);
    HGDIOBJ old = U(SelectObject)(dc, hb);
    CHECK(old != NULL && old == cur);
    CHECK(old && BD_TYPE7(old) == 5);
    CHECK(U(GetCurrentObject)(dc, OBJ_BITMAP) == hb);
    U(SelectObject)(dc, old);
    CHECK(U(GetCurrentObject)(dc, OBJ_BITMAP) == old);
    U(DeleteDC)(dc); U(DeleteObject)(hb);
}
TEST(bd_sel_into_second_dc_fails_while_selected) {
    BD_NEED(); REQUIRE(U(SelectObject));
    HDC d1 = bd_mem_dc(), d2 = bd_mem_dc(); HBITMAP hb = U(CreateBitmap)(4, 4, 1, 1, NULL);
    if (!d1 || !d2 || !hb) { if (d1) U(DeleteDC)(d1); if (d2) U(DeleteDC)(d2); if (hb) U(DeleteObject)(hb); t_skip("setup"); return; }
    HGDIOBJ o1 = U(SelectObject)(d1, hb);
    CHECK(o1 != NULL);
    /* the bitmap-selection path refuses a surface that another DC already owns */
    HGDIOBJ o2 = U(SelectObject)(d2, hb);
    CHECK(o2 == NULL);
    U(SelectObject)(d1, o1);
    o2 = U(SelectObject)(d2, hb);
    CHECK(o2 != NULL);
    if (o2) U(SelectObject)(d2, o2);
    U(DeleteDC)(d1); U(DeleteDC)(d2); U(DeleteObject)(hb);
}
TEST(bd_sel_dib_into_second_dc_fails_while_selected) {
    BD_NEED(); REQUIRE(U(SelectObject));
    HDC d1 = bd_mem_dc(), d2 = bd_mem_dc(); HBITMAP hb = bd_dib(4, 4, 24, NULL);
    if (!d1 || !d2 || !hb) { if (d1) U(DeleteDC)(d1); if (d2) U(DeleteDC)(d2); if (hb) U(DeleteObject)(hb); t_skip("setup"); return; }
    HGDIOBJ o1 = U(SelectObject)(d1, hb);
    CHECK(o1 != NULL);
    CHECK(U(SelectObject)(d2, hb) == NULL);
    U(SelectObject)(d1, o1);
    HGDIOBJ o2 = U(SelectObject)(d2, hb);
    CHECK(o2 != NULL);
    U(DeleteDC)(d1); U(DeleteDC)(d2); U(DeleteObject)(hb);
}
TEST(bd_sel_into_display_dc_fails) {
    BD_NEED(); REQUIRE(U(SelectObject));
    HDC dc = bd_screen_dc(); HBITMAP hb = U(CreateBitmap)(4, 4, 1, 1, NULL);
    if (!dc || !hb) { if (dc) U(DeleteDC)(dc); if (hb) U(DeleteObject)(hb); t_skip("setup"); return; }
    /* NtGdiSelectBitmap: only memory-DC types pass the `type < 2` test in the bitmap-selection path's caller; a display DC selects nothing */
    HGDIOBJ o = U(SelectObject)(dc, hb);
    t_note("SelectObject(display DC, bitmap) -> %p", o);
    CHECK(o == NULL);
    if (o) U(SelectObject)(dc, o);
    U(DeleteDC)(dc); U(DeleteObject)(hb);
}
TEST(bd_sel_reselect_same_bitmap_same_dc) {
    BD_NEED(); REQUIRE(U(SelectObject));
    HDC dc = bd_mem_dc(); HBITMAP hb = U(CreateBitmap)(4, 4, 1, 1, NULL);
    if (!dc || !hb) { if (dc) U(DeleteDC)(dc); if (hb) U(DeleteObject)(hb); t_skip("setup"); return; }
    HGDIOBJ o1 = U(SelectObject)(dc, hb);
    HGDIOBJ o2 = U(SelectObject)(dc, hb);
    t_note("second select of the same bitmap into the same DC -> %p (bitmap %p)", o2, hb);
    CHECK(U(GetCurrentObject)(dc, OBJ_BITMAP) == hb);
    U(SelectObject)(dc, o1);
    U(DeleteDC)(dc); U(DeleteObject)(hb);
}
TEST(bd_sel_swap_between_bitmaps) {
    BD_NEED(); REQUIRE(U(SelectObject));
    HDC dc = bd_mem_dc(); HBITMAP a = U(CreateBitmap)(4, 4, 1, 1, NULL), b = U(CreateBitmap)(6, 6, 1, 32, NULL);
    if (!dc || !a || !b) { if (dc) U(DeleteDC)(dc); if (a) U(DeleteObject)(a); if (b) U(DeleteObject)(b); t_skip("setup"); return; }
    HGDIOBJ def = U(SelectObject)(dc, a);
    CHECK(U(SelectObject)(dc, b) == a);
    CHECK(U(SelectObject)(dc, a) == b);
    /* a bitmap leaves the DC it was swapped out of, so it can move on */
    HDC d2 = bd_mem_dc();
    if (d2) { HGDIOBJ o = U(SelectObject)(d2, b); CHECK(o != NULL); if (o) U(SelectObject)(d2, o); U(DeleteDC)(d2); }
    U(SelectObject)(dc, def);
    U(DeleteDC)(dc); U(DeleteObject)(a); U(DeleteObject)(b);
}
TEST(bd_sel_deleting_dc_releases_bitmap) {
    BD_NEED(); REQUIRE(U(SelectObject));
    HDC d1 = bd_mem_dc(), d2 = bd_mem_dc(); HBITMAP hb = U(CreateBitmap)(4, 4, 1, 1, NULL);
    if (!d1 || !d2 || !hb) { if (d1) U(DeleteDC)(d1); if (d2) U(DeleteDC)(d2); if (hb) U(DeleteObject)(hb); t_skip("setup"); return; }
    U(SelectObject)(d1, hb);
    U(DeleteDC)(d1);
    HGDIOBJ o = U(SelectObject)(d2, hb);
    CHECK(o != NULL);
    if (o) U(SelectObject)(d2, o);
    U(DeleteDC)(d2); U(DeleteObject)(hb);
}
TEST(bd_sel_non_bitmap_through_nt_select_bitmap) {
    BD_NEED(); BD_NEED_NT(NtGdiSelectBitmap); REQUIRE(U(CreateSolidBrush));
    HDC dc = bd_mem_dc(); HBRUSH br = U(CreateSolidBrush)(RGB(9, 9, 9));
    if (!dc || !br) { if (dc) U(DeleteDC)(dc); if (br) U(DeleteObject)(br); t_skip("setup"); return; }
    HGDIOBJ before = U(GetCurrentObject)(dc, OBJ_BITMAP);
    CHECK(UN(NtGdiSelectBitmap)(dc, (HBITMAP)br) == NULL); /* the bitmap-selection path: the handle is not a type-5 object */
    CHECK(U(GetCurrentObject)(dc, OBJ_BITMAP) == before);
    CHECK(UN(NtGdiSelectBitmap)(NULL, (HBITMAP)before) == NULL); /* the DC validity check fails */
    U(DeleteDC)(dc); U(DeleteObject)(br);
}
TEST(bd_sel_nt_select_bitmap_matches_selectobject) {
    BD_NEED(); BD_NEED_NT(NtGdiSelectBitmap); BD_NEED_NT(NtGdiGetDCforBitmap);
    HDC dc = bd_mem_dc(); HBITMAP hb = U(CreateBitmap)(4, 4, 1, 1, NULL);
    if (!dc || !hb) { if (dc) U(DeleteDC)(dc); if (hb) U(DeleteObject)(hb); t_skip("setup"); return; }
    HGDIOBJ before = U(GetCurrentObject)(dc, OBJ_BITMAP);
    HGDIOBJ old = UN(NtGdiSelectBitmap)(dc, hb);
    CHECK(old == before);
    CHECK(U(GetCurrentObject)(dc, OBJ_BITMAP) == hb);
    CHECK(UN(NtGdiGetDCforBitmap)(hb) == dc);
    UN(NtGdiSelectBitmap)(dc, (HBITMAP)old);
    CHECK(UN(NtGdiGetDCforBitmap)(hb) == NULL);
    U(DeleteDC)(dc); U(DeleteObject)(hb);
}
TEST(bd_sel_getdcforbitmap_tracks_selection) {
    BD_NEED(); BD_NEED_NT(NtGdiGetDCforBitmap);
    HDC d1 = bd_mem_dc(), d2 = bd_mem_dc(); HBITMAP hb = bd_dib(4, 4, 32, NULL);
    if (!d1 || !d2 || !hb) { if (d1) U(DeleteDC)(d1); if (d2) U(DeleteDC)(d2); if (hb) U(DeleteObject)(hb); t_skip("setup"); return; }
    CHECK(UN(NtGdiGetDCforBitmap)(hb) == NULL);
    HGDIOBJ o1 = U(SelectObject)(d1, hb);
    CHECK(UN(NtGdiGetDCforBitmap)(hb) == d1);
    U(SelectObject)(d1, o1);
    CHECK(UN(NtGdiGetDCforBitmap)(hb) == NULL);
    HGDIOBJ o2 = U(SelectObject)(d2, hb);
    CHECK(UN(NtGdiGetDCforBitmap)(hb) == d2);
    U(SelectObject)(d2, o2);
    U(DeleteDC)(d1); U(DeleteDC)(d2); U(DeleteObject)(hb);
}
TEST(bd_sel_getdcforbitmap_rejects_other_objects) {
    BD_NEED(); BD_NEED_NT(NtGdiGetDCforBitmap); REQUIRE(U(CreateSolidBrush));
    HDC dc = bd_mem_dc(); HBRUSH br = U(CreateSolidBrush)(RGB(1, 1, 1));
    if (!dc || !br) { if (dc) U(DeleteDC)(dc); if (br) U(DeleteObject)(br); t_skip("setup"); return; }
    /* only surface-type objects pass the handle check */
    CHECK(UN(NtGdiGetDCforBitmap)((HBITMAP)br) == NULL);
    CHECK(UN(NtGdiGetDCforBitmap)((HBITMAP)dc) == NULL);
    CHECK(UN(NtGdiGetDCforBitmap)(NULL) == NULL);
    HBITMAP hb = U(CreateBitmap)(2, 2, 1, 1, NULL);
    if (hb) { U(DeleteObject)(hb); CHECK(UN(NtGdiGetDCforBitmap)(hb) == NULL); }
    U(DeleteDC)(dc); U(DeleteObject)(br);
}
TEST(bd_sel_getdcforbitmap_after_dc_deleted) {
    BD_NEED(); BD_NEED_NT(NtGdiGetDCforBitmap);
    HDC dc = bd_mem_dc(); HBITMAP hb = U(CreateBitmap)(2, 2, 1, 1, NULL);
    if (!dc || !hb) { if (dc) U(DeleteDC)(dc); if (hb) U(DeleteObject)(hb); t_skip("setup"); return; }
    U(SelectObject)(dc, hb);
    CHECK(UN(NtGdiGetDCforBitmap)(hb) == dc);
    U(DeleteDC)(dc);
    HDC after = UN(NtGdiGetDCforBitmap)(hb);
    if (after) t_note("GetDCforBitmap after DeleteDC still reports %p", after);
    U(DeleteObject)(hb);
}
TEST(bd_sel_gdi_default_bitmap_dc_attr) {
    BD_NEED(); REQUIRE(U(GetCurrentObject)); REQUIRE(U(GetObjectType));
    HDC dc = bd_mem_dc(); REQUIRE(dc);
    HGDIOBJ b = U(GetCurrentObject)(dc, OBJ_BITMAP);
    CHECK(b != NULL); CHECK(b && U(GetObjectType)(b) == OBJ_BITMAP);
    BITMAP bm; if (b) { CHECK(bd_getobj(b, &bm) == sizeof bm); CHECK(bm.bmWidth == 1 && bm.bmHeight == 1 && bm.bmBitsPixel == 1); }
    U(DeleteDC)(dc);
}
TEST(bd_sel_delete_selected_bitmap_survives_note) {
    BD_NEED(); REQUIRE(U(SelectObject));
    HDC dc = bd_mem_dc(); HBITMAP hb = U(CreateBitmap)(2, 2, 1, 1, NULL);
    if (!dc || !hb) { if (dc) U(DeleteDC)(dc); if (hb) U(DeleteObject)(hb); t_skip("setup"); return; }
    HGDIOBJ old = U(SelectObject)(dc, hb);
    BOOL r = U(DeleteObject)(hb);
    t_note("DeleteObject(selected bitmap) -> %d, GetObjectType afterwards %lu", r, U(GetObjectType)(hb));
    U(SelectObject)(dc, old);
    if (U(GetObjectType)(hb)) U(DeleteObject)(hb);
    U(DeleteDC)(dc);
}
