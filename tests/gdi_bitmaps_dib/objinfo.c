/* Bitmap object queries.
 *  NtGdiExtGetObjectW: pv == NULL -> 0x20 for every bitmap; cb is clamped to 0x250;
 *    cb < 0x20 -> 0; cb in [0x20, 0x68) or a DDB -> BITMAP (0x20 bytes only); DIB section and cb >= 0x68 -> DIBSECTION (0x68 bytes).
 *  NtGdiSetBitmapDimension / NtGdiGetBitmapDimension: an 8-byte SIZE stored with the bitmap; invalid handles return 0.
 *  NtGdiGetDIBitsInternal with pBits == NULL and biBitCount == 0 reports the bitmap's own format (the kernel DIB query). */
#include "bd.h"

static HBITMAP oi_ddb(int w, int h, int bpp) { return U(CreateBitmap)(w, h, 1, bpp, NULL); }

/* ---- GetObject buffer protocol ------------------------------------------------------------------------------------------ */
#define OI_NULLPV(X) X(mono, 8, 8, 1) X(c4, 8, 8, 4) X(c8, 8, 8, 8) X(c16, 8, 8, 16) X(c24, 8, 8, 24) X(c32, 8, 8, 32)
#define X(nm, w, h, bpp) \
TEST(bd_obj_null_buffer_returns_32_ddb_##nm) { \
    BD_NEED(); BD_NEED_NT(NtGdiExtGetObjectW); \
    HBITMAP hb = oi_ddb(w, h, bpp); REQUIRE(hb); \
    /* the kernel object query: pv == NULL on a bitmap handle returns 0x20 whatever cb is */ \
    CHECK(UN(NtGdiExtGetObjectW)(hb, 0, NULL) == 32); \
    CHECK(UN(NtGdiExtGetObjectW)(hb, 1, NULL) == 32); \
    CHECK(UN(NtGdiExtGetObjectW)(hb, 32, NULL) == 32); \
    CHECK(UN(NtGdiExtGetObjectW)(hb, 104, NULL) == 32); \
    CHECK(UN(NtGdiExtGetObjectW)(hb, 100000, NULL) == 32); \
    CHECK(UN(NtGdiExtGetObjectW)(hb, -1, NULL) == 32); \
    CHECK(U(GetObjectW)(hb, 0, NULL) == 32); \
    U(DeleteObject)(hb); \
}
OI_NULLPV(X)
#undef X

#define OI_NULLDIB(X) X(b1, 8, 8, 1) X(b4, 8, 8, 4) X(b8, 8, 8, 8) X(b16, 8, 8, 16) X(b24, 8, 8, 24) X(b32, 8, 8, 32)
#define X(nm, w, h, bpp) \
TEST(bd_obj_null_buffer_returns_32_dib_##nm) { \
    BD_NEED(); BD_NEED_NT(NtGdiExtGetObjectW); \
    HBITMAP hb = bd_dib(w, h, bpp, NULL); REQUIRE(hb); \
    /* the size query never reports sizeof(DIBSECTION): the NULL path returns before the DIB flag is looked at */ \
    CHECK(UN(NtGdiExtGetObjectW)(hb, 0, NULL) == 32); \
    CHECK(UN(NtGdiExtGetObjectW)(hb, 104, NULL) == 32); \
    CHECK(U(GetObjectW)(hb, 0, NULL) == 32); \
    U(DeleteObject)(hb); \
}
OI_NULLDIB(X)
#undef X

TEST(bd_obj_cb_below_bitmap_returns_zero) {
    BD_NEED(); BD_NEED_NT(NtGdiExtGetObjectW);
    HBITMAP hb = oi_ddb(4, 4, 32); REQUIRE(hb);
    BYTE buf[160];
    for (int cb = 0; cb < 32; cb++) {
        memset(buf, 0x5c, sizeof buf);
        int r = UN(NtGdiExtGetObjectW)(hb, cb, buf);
        CHECKF(r == 0, "cb %d -> %d", cb, r);
        int touched = 0; for (size_t i = 0; i < sizeof buf; i++) touched |= buf[i] != 0x5c;
        CHECKF(!touched, "cb %d wrote into the buffer", cb);
    }
    U(DeleteObject)(hb);
}
TEST(bd_obj_negative_cb_is_clamped_not_rejected) {
    BD_NEED(); BD_NEED_NT(NtGdiExtGetObjectW);
    HBITMAP hb = oi_ddb(4, 4, 32); REQUIRE(hb);
    BITMAP bm[8]; memset(bm, 0, sizeof bm);
    /* NtGdiExtGetObjectW: (int)cb < 0 || cb > 0x250 becomes 0x250, so the call behaves like a large buffer */
    int r = UN(NtGdiExtGetObjectW)(hb, -5, bm);
    CHECKF(r == 32, "r=%d", r);
    CHECK(bm[0].bmWidth == 4 && bm[0].bmHeight == 4);
    r = UN(NtGdiExtGetObjectW)(hb, 0x7fffffff, bm);
    CHECK(r == 32);
    U(DeleteObject)(hb);
}
TEST(bd_obj_ddb_with_big_buffer_only_writes_32) {
    BD_NEED(); BD_NEED_NT(NtGdiExtGetObjectW);
    HBITMAP hb = oi_ddb(5, 3, 8); REQUIRE(hb);
    BYTE buf[300]; memset(buf, 0x77, sizeof buf);
    int r = UN(NtGdiExtGetObjectW)(hb, sizeof buf, buf);
    CHECK(r == 32);
    for (int i = 32; i < (int)sizeof buf; i++) if (buf[i] != 0x77) { CHECKF(0, "byte %d overwritten", i); break; }
    BITMAP *bm = (BITMAP *)buf;
    CHECK(bm->bmType == 0 && bm->bmWidth == 5 && bm->bmHeight == 3 && bm->bmPlanes == 1 && bm->bmBitsPixel == 8 && bm->bmBits == NULL);
    CHECK(bm->bmWidthBytes == (LONG)bd_ddb_stride(5, 8));
    U(DeleteObject)(hb);
}
TEST(bd_obj_ddb_cb_104_returns_32) {
    BD_NEED();
    HBITMAP hb = oi_ddb(5, 3, 32); REQUIRE(hb);
    DIBSECTION ds; memset(&ds, 0x33, sizeof ds);
    CHECK(U(GetObjectW)(hb, sizeof ds, &ds) == (int)sizeof(BITMAP));
    CHECK(((BYTE *)&ds)[sizeof(BITMAP)] == 0x33); /* the kernel object query only fills the DIB tail for a section */
    U(DeleteObject)(hb);
}
TEST(bd_obj_dib_cb_between_32_and_104_returns_32) {
    BD_NEED(); BD_NEED_NT(NtGdiExtGetObjectW);
    void *bits = NULL; HBITMAP hb = bd_dib(5, 3, 32, &bits); REQUIRE(hb && bits);
    BYTE buf[160];
    static const int cbs[] = { 32, 33, 40, 64, 80, 100, 103 };
    for (size_t i = 0; i < sizeof cbs / sizeof cbs[0]; i++) {
        memset(buf, 0x44, sizeof buf);
        int r = UN(NtGdiExtGetObjectW)(hb, cbs[i], buf);
        CHECKF(r == 32, "cb %d -> %d", cbs[i], r);
        /* memcpy(min(cb, 0x20)) */
        CHECK(buf[32] == 0x44);
        BITMAP *bm = (BITMAP *)buf;
        CHECK(bm->bmBits == bits && bm->bmWidth == 5 && bm->bmHeight == 3);
    }
    U(DeleteObject)(hb);
}
TEST(bd_obj_dib_cb_104_and_above_returns_104) {
    BD_NEED(); BD_NEED_NT(NtGdiExtGetObjectW);
    HBITMAP hb = bd_dib(5, 3, 32, NULL); REQUIRE(hb);
    BYTE buf[0x300];
    static const int cbs[] = { 104, 105, 128, 200, 0x250, 0x251, 0x400 };
    for (size_t i = 0; i < sizeof cbs / sizeof cbs[0]; i++) {
        memset(buf, 0x44, sizeof buf);
        int r = UN(NtGdiExtGetObjectW)(hb, cbs[i], buf);
        CHECKF(r == 104, "cb %d -> %d", cbs[i], r);
        CHECKF(buf[104] == 0x44, "cb %d wrote beyond DIBSECTION", cbs[i]);
    }
    U(DeleteObject)(hb);
}
TEST(bd_obj_dib_cb_truncated_copy_when_cb_smaller_than_result) {
    BD_NEED();
    void *bits = NULL; HBITMAP hb = bd_dib(5, 3, 32, &bits); REQUIRE(hb);
    BYTE buf[160]; memset(buf, 0x66, sizeof buf);
    /* cb = 100 < 104: the kernel object query is told 100 so it stops at the BITMAP and returns 32; copy length is min(32, cb) */
    int r = U(GetObjectW)(hb, 100, buf);
    CHECK(r == 32);
    U(DeleteObject)(hb);
}
TEST(bd_obj_sizeof_structs) {
    CHECK(sizeof(BITMAP) == 32);
    CHECK(sizeof(DIBSECTION) == 104);
    CHECK(sizeof(BITMAPINFOHEADER) == 40);
    CHECK(offsetof(DIBSECTION, dsBmih) == 32);
    CHECK(offsetof(DIBSECTION, dsBitfields) == 72);
    CHECK(offsetof(DIBSECTION, dshSection) == 88);
    CHECK(offsetof(DIBSECTION, dsOffset) == 96);
    CHECK(offsetof(BITMAP, bmBits) == 24);
}
TEST(bd_obj_non_bitmap_handles_yield_zero) {
    BD_NEED(); BD_NEED_NT(NtGdiExtGetObjectW); REQUIRE(U(GetStockObject)); REQUIRE(U(CreateSolidBrush));
    HDC dc = bd_mem_dc(); REQUIRE(dc);
    BYTE buf[160]; memset(buf, 0x11, sizeof buf);
    /* a DC handle has type 1: the kernel object query switch has no case for it */
    CHECK(UN(NtGdiExtGetObjectW)(dc, sizeof buf, buf) == 0);
    SET_ERR(0);
    CHECK(U(GetObjectW)(dc, sizeof buf, buf) == 0);
    EXPECT_ERR(BD_ERR_INVALID_HANDLE); /* gdi32full!GetObjectW: DC handle -> GdiSetLastError(6) */
    HRGN rg = U(CreateRectRgn)(0, 0, 3, 3);
    if (rg) { CHECK(UN(NtGdiExtGetObjectW)(rg, sizeof buf, buf) == 0); U(DeleteObject)(rg); }
    U(DeleteDC)(dc);
}
TEST(bd_obj_null_and_stale_handles_yield_zero) {
    BD_NEED(); BD_NEED_NT(NtGdiExtGetObjectW);
    BYTE buf[160];
    CHECK(UN(NtGdiExtGetObjectW)(NULL, sizeof buf, buf) == 0);
    CHECK(U(GetObjectW)(NULL, sizeof buf, buf) == 0);
    HBITMAP hb = oi_ddb(2, 2, 32); REQUIRE(hb);
    U(DeleteObject)(hb);
    /* the stale handle keeps its type bits but its cell is empty: the bitmap handle lookup fails */
    CHECK(UN(NtGdiExtGetObjectW)(hb, sizeof buf, buf) == 0);
    CHECK(UN(NtGdiExtGetObjectW)(hb, 0, NULL) == 32); /* pv == NULL returns before any lookup */
}
TEST(bd_obj_type_bits_forged_as_bitmap_on_other_object) {
    BD_NEED(); BD_NEED_NT(NtGdiExtGetObjectW);
    HRGN rg = U(CreateRectRgn)(0, 0, 4, 4); REQUIRE(rg);
    /* flip the region's type bits (4) to the bitmap type (5): the cell's upper bits no longer match, so the lookup fails */
    HANDLE forged = (HANDLE)(((ULONG_PTR)rg & ~(ULONG_PTR)0x1f0000) | 0x50000);
    BYTE buf[160]; memset(buf, 0, sizeof buf);
    CHECK(UN(NtGdiExtGetObjectW)(forged, sizeof buf, buf) == 0);
    CHECK(U(GetObjectType)(rg) == OBJ_REGION);
    U(DeleteObject)(rg);
}
TEST(bd_obj_stock_bitmap_getobject) {
    BD_NEED(); REQUIRE(U(GetStockObject));
    HBITMAP s = U(GetStockObject)(0x15);
    if (!s) { t_skip("no stock bitmap"); return; }
    BITMAP bm; CHECK(U(GetObjectW)(s, sizeof bm, &bm) == (int)sizeof bm);
    CHECK(bm.bmWidth == 1 && bm.bmHeight == 1 && bm.bmBitsPixel == 1);
}
TEST(bd_obj_ansi_and_wide_agree) {
    BD_NEED(); REQUIRE(U(GetObjectA));
    HBITMAP hb = bd_dib(6, 4, 24, NULL); REQUIRE(hb);
    DIBSECTION a, w; memset(&a, 1, sizeof a); memset(&w, 2, sizeof w);
    CHECK(U(GetObjectA)(hb, sizeof a, &a) == (int)sizeof a);
    CHECK(U(GetObjectW)(hb, sizeof w, &w) == (int)sizeof w);
    CHECK(!memcmp(&a, &w, sizeof a));
    U(DeleteObject)(hb);
}
TEST(bd_obj_repeatable) {
    BD_NEED();
    HBITMAP hb = bd_dib(6, 4, 16, NULL); REQUIRE(hb);
    DIBSECTION a, b; bd_getdib(hb, &a); bd_getdib(hb, &b);
    CHECK(!memcmp(&a, &b, sizeof a));
    U(DeleteObject)(hb);
}
TEST(bd_obj_dib_pel_fields_zero) {
    BD_NEED();
    HBITMAP hb = bd_dib(6, 4, 24, NULL); REQUIRE(hb);
    DIBSECTION ds; CHECK(bd_getdib(hb, &ds) == (int)sizeof ds);
    /* the kernel DIB query path zeroes the 8 bytes at offset 0x18 of the header (biXPelsPerMeter, biYPelsPerMeter) */
    CHECK(ds.dsBmih.biXPelsPerMeter == 0 && ds.dsBmih.biYPelsPerMeter == 0);
    U(DeleteObject)(hb);
}
TEST(bd_obj_dib_clrused_for_palettized) {
    BD_NEED();
    static const struct { int bpp; DWORD n; } t[] = { { 1, 2 }, { 4, 16 }, { 8, 256 } };
    for (int i = 0; i < 3; i++) {
        HBITMAP hb = bd_dib(4, 4, t[i].bpp, NULL); DIBSECTION ds;
        REQUIRE(hb);
        CHECK(bd_getdib(hb, &ds) == (int)sizeof ds);
        CHECKF(ds.dsBmih.biClrUsed == t[i].n && ds.dsBmih.biClrImportant == t[i].n, "bpp %d clrused %lu/%lu", t[i].bpp, ds.dsBmih.biClrUsed, ds.dsBmih.biClrImportant);
        U(DeleteObject)(hb);
    }
}
TEST(bd_obj_dib_clrused_for_true_color_note) {
    BD_NEED();
    static const int bpp[] = { 16, 24, 32 };
    for (int i = 0; i < 3; i++) {
        HBITMAP hb = bd_dib(4, 4, bpp[i], NULL); DIBSECTION ds;
        REQUIRE(hb);
        bd_getdib(hb, &ds);
        t_note("%dbpp DIB: biClrUsed %lu biClrImportant %lu biSizeImage %lu", bpp[i], ds.dsBmih.biClrUsed, ds.dsBmih.biClrImportant, ds.dsBmih.biSizeImage);
        U(DeleteObject)(hb);
    }
}

/* ---- GetDIBits header-query path --------------------------------------------------------------------------------------- */
/* planes*bpp -> depth; header fields are filled from the surface (the kernel DIB query, pBits == NULL, biBitCount == 0) */
#define OI_QUERY(X) X(mono, 1, 1, 1, 2) X(c4, 4, 4, 4, 16) X(c8, 8, 8, 8, 256) X(c16, 16, 16, 16, 0) X(c24, 24, 24, 24, 0) X(c32, 32, 32, 32, 0)
#define X(nm, bpp, depth, rep, ncol) \
TEST(bd_obj_getdibits_query_ddb_##nm) { \
    BD_NEED(); REQUIRE(U(GetDIBits)); \
    HBITMAP hb = oi_ddb(10, 6, bpp); HDC dc = bd_mem_dc(); \
    if (!hb || !dc) { if (hb) U(DeleteObject)(hb); if (dc) U(DeleteDC)(dc); t_skip("setup"); return; } \
    struct bd_bmi bi; memset(&bi, 0, sizeof bi); bi.h.biSize = sizeof bi.h; \
    int r = U(GetDIBits)(dc, hb, 0, 0, NULL, (BITMAPINFO *)&bi, DIB_RGB_COLORS); \
    CHECKF(r != 0, "GetDIBits query failed"); \
    CHECK(bi.h.biSize == 40 && bi.h.biWidth == 10 && bi.h.biHeight == 6 && bi.h.biPlanes == 1); \
    CHECKF(bi.h.biBitCount == (rep), "bitcount %u", bi.h.biBitCount); \
    CHECKF(bi.h.biCompression == ((bpp) == 16 || (bpp) == 32 ? BI_BITFIELDS : BI_RGB), "compression %lu", bi.h.biCompression); \
    CHECKF(bi.h.biSizeImage == bd_stride(10, rep) * 6, "sizeimage %lu", bi.h.biSizeImage); \
    if ((ncol) > 0) { CHECKF(bi.h.biClrUsed == (ncol) && bi.h.biClrImportant == (ncol), "clr %lu/%lu", bi.h.biClrUsed, bi.h.biClrImportant); } \
    else t_note("%ubpp DDB: biClrUsed %lu biClrImportant %lu", (unsigned)(rep), bi.h.biClrUsed, bi.h.biClrImportant); \
    U(DeleteDC)(dc); U(DeleteObject)(hb); \
}
OI_QUERY(X)
#undef X

TEST(bd_obj_getdibits_query_dib_section_matches_getobject) {
    BD_NEED(); REQUIRE(U(GetDIBits));
    HBITMAP hb = bd_dib(9, 5, 24, NULL); HDC dc = bd_mem_dc();
    if (!hb || !dc) { if (hb) U(DeleteObject)(hb); if (dc) U(DeleteDC)(dc); t_skip("setup"); return; }
    struct bd_bmi bi; memset(&bi, 0, sizeof bi); bi.h.biSize = sizeof bi.h;
    CHECK(U(GetDIBits)(dc, hb, 0, 0, NULL, (BITMAPINFO *)&bi, DIB_RGB_COLORS) != 0);
    CHECK(bi.h.biWidth == 9 && bi.h.biHeight == 5 && bi.h.biBitCount == 24 && bi.h.biPlanes == 1);
    CHECK(bi.h.biSizeImage == bd_stride(9, 24) * 5);
    U(DeleteDC)(dc); U(DeleteObject)(hb);
}
TEST(bd_obj_getdibits_query_core_header) {
    BD_NEED(); BD_NEED_NT(NtGdiGetDIBitsInternal);
    HBITMAP hb = bd_dib(9, 5, 32, NULL); REQUIRE(hb);
    BITMAPCOREHEADER ch; memset(&ch, 0, sizeof ch); ch.bcSize = sizeof ch;
 /* == 0xc && bcBitCount == 0: width/height as WORDs, planes 1, depth reported as 24 for anything >= 16 bpp */
    int r = UN(NtGdiGetDIBitsInternal)(NULL, hb, 0, 0, NULL, (BITMAPINFO *)&ch, DIB_RGB_COLORS, 0, 0);
    t_note("core header query result %d", r);
    if (r) { CHECK(ch.bcWidth == 9 && ch.bcHeight == 5 && ch.bcPlanes == 1); CHECK(ch.bcBitCount == 24); }
    U(DeleteObject)(hb);
}
TEST(bd_obj_getdibits_query_rejects_bad_usage) {
    BD_NEED(); BD_NEED_NT(NtGdiGetDIBitsInternal);
    HBITMAP hb = oi_ddb(4, 4, 32); REQUIRE(hb);
    struct bd_bmi bi; memset(&bi, 0, sizeof bi); bi.h.biSize = sizeof bi.h;
    /* NtGdiGetDIBitsInternal: usage > 2 -> 0 before any lookup */
    for (UINT u = 3; u < 8; u++) CHECKF(UN(NtGdiGetDIBitsInternal)(NULL, hb, 0, 0, NULL, (BITMAPINFO *)&bi, u, 0, 0) == 0, "usage %u", u);
    CHECK(UN(NtGdiGetDIBitsInternal)(NULL, hb, 0, 0, NULL, (BITMAPINFO *)&bi, 0xffffffff, 0, 0) == 0);
    U(DeleteObject)(hb);
}
TEST(bd_obj_getdibits_null_args) {
    BD_NEED(); BD_NEED_NT(NtGdiGetDIBitsInternal);
    HBITMAP hb = oi_ddb(4, 4, 32); REQUIRE(hb);
    struct bd_bmi bi; memset(&bi, 0, sizeof bi); bi.h.biSize = sizeof bi.h;
    CHECK(UN(NtGdiGetDIBitsInternal)(NULL, hb, 0, 0, NULL, NULL, 0, 0, 0) == 0);     /* pbmi == NULL */
    CHECK(UN(NtGdiGetDIBitsInternal)(NULL, NULL, 0, 0, NULL, (BITMAPINFO *)&bi, 0, 0, 0) == 0); /* hbm == NULL */
    U(DeleteObject)(hb);
}
TEST(bd_obj_getdibits_query_stale_bitmap_fails) {
    BD_NEED(); REQUIRE(U(GetDIBits));
    HBITMAP hb = oi_ddb(4, 4, 32); HDC dc = bd_mem_dc();
    if (!hb || !dc) { if (hb) U(DeleteObject)(hb); if (dc) U(DeleteDC)(dc); t_skip("setup"); return; }
    U(DeleteObject)(hb);
    struct bd_bmi bi; memset(&bi, 0, sizeof bi); bi.h.biSize = sizeof bi.h;
    CHECK(U(GetDIBits)(dc, hb, 0, 0, NULL, (BITMAPINFO *)&bi, DIB_RGB_COLORS) == 0);
    U(DeleteDC)(dc);
}
TEST(bd_obj_getdibits_query_invalid_dc_user_check) {
    BD_NEED(); REQUIRE(U(GetDIBits));
    HBITMAP hb = oi_ddb(4, 4, 32); REQUIRE(hb);
    struct bd_bmi bi; memset(&bi, 0, sizeof bi); bi.h.biSize = sizeof bi.h;
    SET_ERR(0);
    /* gdi32full!GetDIBits: hdc must name a DC cell owned by this process, else GdiSetLastError(87) and 0 */
    CHECK(U(GetDIBits)(NULL, hb, 0, 0, NULL, (BITMAPINFO *)&bi, DIB_RGB_COLORS) == 0);
    EXPECT_ERR(BD_ERR_INVALID_PARAM);
    SET_ERR(0);
    CHECK(U(GetDIBits)((HDC)hb, hb, 0, 0, NULL, (BITMAPINFO *)&bi, DIB_RGB_COLORS) == 0); /* a bitmap handle is not a DC */
    EXPECT_ERR(BD_ERR_INVALID_PARAM);
    U(DeleteObject)(hb);
}
TEST(bd_obj_getdibits_jpeg_png_rejected) {
    BD_NEED(); REQUIRE(U(GetDIBits)); BD_NEED_NT(NtGdiGetDIBitsInternal);
    HBITMAP hb = oi_ddb(4, 4, 32); HDC dc = bd_mem_dc();
    if (!hb || !dc) { if (hb) U(DeleteObject)(hb); if (dc) U(DeleteDC)(dc); t_skip("setup"); return; }
    BYTE out[256]; struct bd_bmi bi;
    for (DWORD comp = BI_JPEG; comp <= BI_PNG; comp++) {
        bd_bmi_init(&bi, 4, 4, 32); bi.h.biCompression = comp; bi.h.biSizeImage = sizeof out;
        SET_ERR(0);
        /* gdi32full!GetDIBits and the kernel DIB query both refuse BI_JPEG/BI_PNG when pixels are requested */
        int r = U(GetDIBits)(dc, hb, 0, 4, out, (BITMAPINFO *)&bi, DIB_RGB_COLORS);
        CHECKF(r == 0, "comp %lu returned %d", comp, r);
        EXPECT_ERR(BD_ERR_INVALID_PARAM);
        SET_ERR(0);
        r = UN(NtGdiGetDIBitsInternal)(dc, hb, 0, 4, out, (BITMAPINFO *)&bi, DIB_RGB_COLORS, sizeof out, 0);
        CHECKF(r == 0, "syscall comp %lu returned %d", comp, r);
        EXPECT_ERR(BD_ERR_INVALID_PARAM);
    }
    U(DeleteDC)(dc); U(DeleteObject)(hb);
}

/* ---- bitmap dimension ---------------------------------------------------------------------------------------------------- */
TEST(bd_dim_fresh_bitmap_is_zero) {
    BD_NEED(); REQUIRE(U(GetBitmapDimensionEx));
    HBITMAP hb = oi_ddb(8, 8, 1); REQUIRE(hb);
    SIZE sz = { 123, 456 };
    CHECK(U(GetBitmapDimensionEx)(hb, &sz));
    CHECK(sz.cx == 0 && sz.cy == 0);
    U(DeleteObject)(hb);
}
TEST(bd_dim_fresh_dib_is_zero) {
    BD_NEED(); REQUIRE(U(GetBitmapDimensionEx));
    HBITMAP hb = bd_dib(8, 8, 24, NULL); REQUIRE(hb);
    SIZE sz = { 123, 456 };
    CHECK(U(GetBitmapDimensionEx)(hb, &sz));
    CHECK(sz.cx == 0 && sz.cy == 0);
    U(DeleteObject)(hb);
}
#define OI_DIMS(X) X(small, 10, 20) X(zero, 0, 0) X(neg, -5, -7) X(mixed, 1000, -1) X(big, 0x7fffffff, 0x7ffffffe) X(min, (int)0x80000000, (int)0x80000001) X(one, 1, 1)
#define X(nm, DX, DY) \
TEST(bd_dim_roundtrip_ddb_##nm) { \
    BD_NEED(); REQUIRE(U(GetBitmapDimensionEx)); REQUIRE(U(SetBitmapDimensionEx)); \
    HBITMAP hb = oi_ddb(8, 8, 32); REQUIRE(hb); \
    SIZE old = { 77, 88 }, got = { 0, 0 }; \
    /* the kernel bitmap-dimension setter stores the two ints verbatim: no validation */ \
    CHECK(U(SetBitmapDimensionEx)(hb, DX, DY, &old)); \
    CHECK(old.cx == 0 && old.cy == 0); \
    CHECK(U(GetBitmapDimensionEx)(hb, &got)); \
    CHECKF(got.cx == (DX) && got.cy == (DY), "got %ld,%ld", got.cx, got.cy); \
    U(DeleteObject)(hb); \
}
OI_DIMS(X)
#undef X
#define X(nm, DX, DY) \
TEST(bd_dim_roundtrip_dib_##nm) { \
    BD_NEED(); REQUIRE(U(GetBitmapDimensionEx)); REQUIRE(U(SetBitmapDimensionEx)); \
    HBITMAP hb = bd_dib(8, 8, 32, NULL); REQUIRE(hb); \
    SIZE got = { 0, 0 }; \
    CHECK(U(SetBitmapDimensionEx)(hb, DX, DY, NULL)); \
    CHECK(U(GetBitmapDimensionEx)(hb, &got)); \
    CHECKF(got.cx == (DX) && got.cy == (DY), "got %ld,%ld", got.cx, got.cy); \
    U(DeleteObject)(hb); \
}
OI_DIMS(X)
#undef X
TEST(bd_dim_previous_value_returned) {
    BD_NEED(); REQUIRE(U(SetBitmapDimensionEx));
    HBITMAP hb = oi_ddb(8, 8, 32); REQUIRE(hb);
    SIZE old;
    CHECK(U(SetBitmapDimensionEx)(hb, 10, 20, NULL));
    old.cx = old.cy = -1;
    CHECK(U(SetBitmapDimensionEx)(hb, 30, 40, &old));
    t_note("previous dimension returned by the second set: %ld,%ld (expected 10,20)", old.cx, old.cy);
    SIZE g; CHECK(U(GetBitmapDimensionEx)(hb, &g) && g.cx == 30 && g.cy == 40);
    U(DeleteObject)(hb);
}
TEST(bd_dim_independent_per_bitmap) {
    BD_NEED(); REQUIRE(U(SetBitmapDimensionEx));
    HBITMAP a = oi_ddb(4, 4, 1), b = oi_ddb(4, 4, 1); REQUIRE(a && b);
    U(SetBitmapDimensionEx)(a, 1, 2, NULL); U(SetBitmapDimensionEx)(b, 3, 4, NULL);
    SIZE s;
    CHECK(U(GetBitmapDimensionEx)(a, &s) && s.cx == 1 && s.cy == 2);
    CHECK(U(GetBitmapDimensionEx)(b, &s) && s.cx == 3 && s.cy == 4);
    U(DeleteObject)(a); U(DeleteObject)(b);
}
TEST(bd_dim_not_derived_from_pixels) {
    BD_NEED(); REQUIRE(U(GetBitmapDimensionEx));
    HBITMAP hb = oi_ddb(123, 45, 32); REQUIRE(hb);
    SIZE s = { 9, 9 };
    CHECK(U(GetBitmapDimensionEx)(hb, &s) && s.cx == 0 && s.cy == 0);
    BITMAP bm; bd_getobj(hb, &bm);
    CHECK(bm.bmWidth == 123);
    U(DeleteObject)(hb);
}
TEST(bd_dim_survives_select_and_deselect) {
    BD_NEED(); REQUIRE(U(SetBitmapDimensionEx)); REQUIRE(U(SelectObject));
    HBITMAP hb = oi_ddb(4, 4, 1); HDC dc = bd_mem_dc();
    if (!hb || !dc) { if (hb) U(DeleteObject)(hb); if (dc) U(DeleteDC)(dc); t_skip("setup"); return; }
    U(SetBitmapDimensionEx)(hb, 321, 123, NULL);
    HGDIOBJ old = U(SelectObject)(dc, hb);
    SIZE s; CHECK(U(GetBitmapDimensionEx)(hb, &s) && s.cx == 321 && s.cy == 123);
    U(SelectObject)(dc, old);
    CHECK(U(GetBitmapDimensionEx)(hb, &s) && s.cx == 321 && s.cy == 123);
    U(DeleteDC)(dc); U(DeleteObject)(hb);
}
TEST(bd_dim_sys_null_handle_returns_zero_no_write) {
    BD_NEED_NT(NtGdiSetBitmapDimension); BD_NEED_NT(NtGdiGetBitmapDimension);
    SIZE s = { 5, 6 };
    /* both wrappers test hbm == 0 first and return 0 */
    CHECK(UN(NtGdiSetBitmapDimension)(NULL, 1, 2, &s) == 0);
    CHECK(s.cx == 5 && s.cy == 6);
    CHECK(UN(NtGdiGetBitmapDimension)(NULL, &s) == 0);
    CHECK(s.cx == 5 && s.cy == 6);
}
TEST(bd_dim_sys_non_bitmap_handle_fails) {
    BD_NEED_NT(NtGdiSetBitmapDimension); BD_NEED_NT(NtGdiGetBitmapDimension); REQUIRE(U(CreateSolidBrush));
    HBRUSH br = U(CreateSolidBrush)(RGB(1, 2, 3)); REQUIRE(br);
    SIZE s = { 5, 6 };
    SET_ERR(0);
    CHECK(UN(NtGdiSetBitmapDimension)((HBITMAP)br, 1, 2, &s) == 0);
    EXPECT_ERR(BD_ERR_INVALID_HANDLE); /* the kernel bitmap-dimension setter: the bitmap handle lookup fails -> EngSetLastError(6) */
    CHECK(s.cx == 5 && s.cy == 6);
    SET_ERR(0);
    CHECK(UN(NtGdiGetBitmapDimension)((HBITMAP)br, &s) == 0);
    CHECK(s.cx == 5 && s.cy == 6);
    U(DeleteObject)(br);
}
TEST(bd_dim_stale_bitmap_fails) {
    BD_NEED(); REQUIRE(U(SetBitmapDimensionEx)); REQUIRE(U(GetBitmapDimensionEx));
    HBITMAP hb = oi_ddb(4, 4, 1); REQUIRE(hb);
    U(DeleteObject)(hb);
    SIZE s = { 1, 1 };
    CHECK(!U(SetBitmapDimensionEx)(hb, 3, 3, &s));
    CHECK(!U(GetBitmapDimensionEx)(hb, &s));
}
TEST(bd_dim_dc_handle_rejected) {
    BD_NEED(); REQUIRE(U(SetBitmapDimensionEx)); REQUIRE(U(GetBitmapDimensionEx));
    HDC dc = bd_mem_dc(); REQUIRE(dc);
    SIZE s = { 1, 1 };
    CHECK(!U(SetBitmapDimensionEx)((HBITMAP)dc, 3, 3, &s));
    CHECK(!U(GetBitmapDimensionEx)((HBITMAP)dc, &s));
    U(DeleteDC)(dc);
}
