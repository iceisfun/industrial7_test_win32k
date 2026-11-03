/* ENHMETAHEADER fields, raw bits, description, palette, SetEnhMetaFileBits, CopyEnhMetaFile.
 *
 * gdi32full!GetEnhMetaFileHeader: record nSize is returned for a NULL buffer; a buffer < 0x58 fails with
 * ERROR_INSUFFICIENT_BUFFER (0x7a); 0x58 <= n < 100 copies 0x58, 100 <= n < 0x6c copies 100, larger copies 0x6c (when the
 * stored header record is that big) and the returned nSize field is the number of bytes copied.
 * GetEnhMetaFileBits returns header.nBytes for a NULL buffer and fails with 0x7a when the buffer is smaller.
 * SetEnhMetaFileBits: nSize < 0x58, nSize < header.nBytes or header.nBytes < 0x58 -> ERROR_INVALID_DATA (13).
 * InternalGetEnhMetaFileDescription: W form returns/copies header.nDescription characters, invalid handle -> GDI_ERROR.
 * GetEnhMetaFilePaletteEntries: header.nPalEntries; invalid handle -> GDI_ERROR.
 */
#include "mf.h"

static const wchar_t DESC[] = L"App\0Title\0";   /* + implicit terminator: 11 characters */
#define DESC_CCH 11

#define MF_NEED_HDR() do { MF_NEED_EMF(); if (!U(GetEnhMetaFileBits) || !U(SetEnhMetaFileBits)) { t_skip("bits exports missing"); return; } } while (0)

TEST(gdi_metafile_print_neg_hdr_fixed_fields) {
    MF_NEED_HDR(); MF_SCREEN(ref);
    HENHMETAFILE e = mf_make(ref, NULL, DESC, 2);
    ENHMETAHEADER h;
    REQUIRE(e);
    CHECK(mf_hdr(e, &h));
    EXPECT_EQ(h.iType, EMR_HEADER);
    EXPECT_EQ(h.dSignature, ENHMETA_SIGNATURE);          /* ' EMF' */
    EXPECT_EQ(h.nVersion, 0x10000);
    CHECK(h.nSize >= 88 && h.nSize <= 108 + 4 * (unsigned)DESC_CCH + 8);
    CHECK((h.nSize & 3) == 0);
    CHECK(h.nBytes >= h.nSize + 20);                      /* header record + EOF record at least */
    CHECK((h.nBytes & 3) == 0);
    CHECK(h.nRecords >= 3);                               /* header + >= 1 drawing record + EOF */
    CHECK(h.nHandles >= 1);
    U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_hdr_nbytes_equals_getbits_size) {
    MF_NEED_HDR(); MF_SCREEN(ref);
    HENHMETAFILE e = mf_make(ref, NULL, NULL, 3);
    ENHMETAHEADER h;
    REQUIRE(e);
    CHECK(mf_hdr(e, &h));
    EXPECT_EQ(U(GetEnhMetaFileBits)(e, 0, NULL), h.nBytes);
    U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_hdr_size_query_returns_record_size) {
    MF_NEED_HDR(); MF_SCREEN(ref);
    HENHMETAFILE e = mf_make(ref, NULL, DESC, 1);
    ENHMETAHEADER h;
    REQUIRE(e);
    UINT q = U(GetEnhMetaFileHeader)(e, 0, NULL);        /* lpEnhMetaHeader == NULL -> record's nSize */
    CHECK(q >= 88);
    CHECK(mf_hdr(e, &h));
    /* the stored record includes the description, the copy is capped at sizeof(ENHMETAHEADER)==0x6c */
    CHECK(h.nSize <= q);
    CHECK(q >= h.nSize);
    U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_hdr_buffer_too_small_fails_122) {
    MF_NEED_HDR(); MF_SCREEN(ref);
    HENHMETAFILE e = mf_make(ref, NULL, NULL, 1);
    static const UINT small[] = { 0, 1, 4, 40, 87 };
    REQUIRE(e);
    for (unsigned i = 0; i < sizeof small / sizeof small[0]; i++) {
        BYTE buf[256]; memset(buf, 0xcc, sizeof buf);
        SetLastError(0);
        EXPECT_EQ(U(GetEnhMetaFileHeader)(e, small[i], (LPENHMETAHEADER)buf), 0);
        EXPECT_ERR(ERROR_INSUFFICIENT_BUFFER);
        CHECK(buf[0] == 0xcc && buf[87] == 0xcc);         /* nothing copied */
    }
    U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_hdr_copy_size_ladder) {
    MF_NEED_HDR(); MF_SCREEN(ref);
    HENHMETAFILE e = mf_make(ref, NULL, NULL, 1);
    REQUIRE(e);
    UINT rec = U(GetEnhMetaFileHeader)(e, 0, NULL);
    if (rec < 108) { t_note("header record is %u bytes (<108); ladder not checked", rec); U(DeleteEnhMetaFile)(e); mf_screen_free(ref); return; }
    static const struct { UINT in, out; } t[] = { {88, 88}, {90, 88}, {99, 88}, {100, 100}, {104, 100}, {107, 100}, {108, 108}, {200, 108}, {1000, 108} };
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
        BYTE buf[1200]; memset(buf, 0xcc, sizeof buf);
        UINT r = U(GetEnhMetaFileHeader)(e, t[i].in, (LPENHMETAHEADER)buf);
        CHECKF(r == t[i].out, "nSize %u -> %u want %u", t[i].in, r, t[i].out);
        CHECK(((ENHMETAHEADER *)buf)->nSize == r);        /* nSize field rewritten to the copied size */
        CHECK(buf[r] == 0xcc);                            /* nothing written past the copy */
    }
    U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_hdr_short_forms_match_full_prefix) {
    MF_NEED_HDR(); MF_SCREEN(ref);
    HENHMETAFILE e = mf_make(ref, NULL, NULL, 1);
    BYTE a[256], b[256];
    REQUIRE(e);
    memset(a, 0, sizeof a); memset(b, 0, sizeof b);
    CHECK(U(GetEnhMetaFileHeader)(e, 88, (LPENHMETAHEADER)a) == 88);
    CHECK(U(GetEnhMetaFileHeader)(e, 108, (LPENHMETAHEADER)b) != 0);
    /* identical except the rewritten nSize field */
    EXPECT_EQ(*(UINT *)(a + 0), EMR_HEADER);
    CHECK(memcmp(a + 8, b + 8, 80) == 0);
    U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_hdr_invalid_handles_fail) {
    MF_NEED_HDR(); MF_SCREEN(ref);
    ENHMETAHEADER h;
    HGDIOBJ br = U(CreateSolidBrush)(0);
    HENHMETAFILE bad[] = { NULL, (HENHMETAFILE)(ULONG_PTR)0x460001, (HENHMETAFILE)(ULONG_PTR)0xfffffff0, (HENHMETAFILE)br, (HENHMETAFILE)ref };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(0);
        EXPECT_EQ(U(GetEnhMetaFileHeader)(bad[i], sizeof h, &h), 0);
        EXPECT_ERR(ERROR_INVALID_HANDLE);                 /* pvClientObjGet -> GdiSetLastError(6) */
        SetLastError(0);
        EXPECT_EQ(U(GetEnhMetaFileBits)(bad[i], 0, NULL), 0);
        EXPECT_ERR(ERROR_INVALID_HANDLE);
    }
    U(DeleteObject)(br);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_hdr_deleted_handle_fails) {
    MF_NEED_HDR(); MF_SCREEN(ref);
    HENHMETAFILE e = mf_make(ref, NULL, NULL, 1);
    ENHMETAHEADER h;
    REQUIRE(e);
    CHECK(U(DeleteEnhMetaFile)(e));
    SetLastError(0);
    EXPECT_EQ(U(GetEnhMetaFileHeader)(e, sizeof h, &h), 0);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    EXPECT_EQ(U(GetEnhMetaFileBits)(e, 0, NULL), 0);
    EXPECT_EQ(U(GetEnhMetaFileDescriptionW)(e, 0, NULL), GDI_ERROR);
    EXPECT_EQ(U(GetEnhMetaFilePaletteEntries)(e, 0, NULL), GDI_ERROR);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_hdr_device_sizes_from_reference_dc) {
    MF_NEED_HDR(); MF_SCREEN(ref);
    HENHMETAFILE e = mf_make(ref, NULL, NULL, 1);
    ENHMETAHEADER h;
    REQUIRE(e);
    CHECK(mf_hdr(e, &h));
    EXPECT_EQ(h.szlDevice.cx, U(GetDeviceCaps)(ref, HORZRES));
    EXPECT_EQ(h.szlDevice.cy, U(GetDeviceCaps)(ref, VERTRES));
    EXPECT_EQ(h.szlMillimeters.cx, U(GetDeviceCaps)(ref, HORZSIZE));
    EXPECT_EQ(h.szlMillimeters.cy, U(GetDeviceCaps)(ref, VERTSIZE));
    if (h.nSize >= 108 && U(GetEnhMetaFileHeader)(e, 108, &h) == 108)
        t_note("szlMicrometers=%ldx%ld (mm*1000 would be %dx%d)", h.szlMicrometers.cx, h.szlMicrometers.cy,
               U(GetDeviceCaps)(ref, HORZSIZE) * 1000, U(GetDeviceCaps)(ref, VERTSIZE) * 1000);
    U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_hdr_explicit_frame_stored_exactly) {
    MF_NEED_HDR(); MF_SCREEN(ref);
    static const RECT fr[] = { {0, 0, 10000, 5000}, {100, 200, 10100, 5200}, {-500, -300, 500, 300}, {1, 1, 2, 2} };
    for (unsigned i = 0; i < sizeof fr / sizeof fr[0]; i++) {
        HENHMETAFILE e = mf_make(ref, &fr[i], NULL, 1);
        ENHMETAHEADER h;
        CHECK(e != NULL);
        if (!e) continue;
        CHECK(mf_hdr(e, &h));
        /* MDC+0x3c..0x48 receives lprc and is copied verbatim into the header's rclFrame (0.01 mm units) */
        EXPECT_EQ(h.rclFrame.left, fr[i].left);
        EXPECT_EQ(h.rclFrame.top, fr[i].top);
        EXPECT_EQ(h.rclFrame.right, fr[i].right);
        EXPECT_EQ(h.rclFrame.bottom, fr[i].bottom);
        U(DeleteEnhMetaFile)(e);
    }
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_hdr_frame_derived_from_bounds_when_null) {
    MF_NEED_HDR(); MF_SCREEN(ref);
    HENHMETAFILE e = mf_make(ref, NULL, NULL, 1);
    ENHMETAHEADER h;
    REQUIRE(e);
    CHECK(mf_hdr(e, &h));
    /* CloseEnhMetaFile: empty frame -> MulDiv(bounds*100, szlMillimeters, szlDevice) */
    int dx = U(GetDeviceCaps)(ref, HORZRES), mx = U(GetDeviceCaps)(ref, HORZSIZE);
    int dy = U(GetDeviceCaps)(ref, VERTRES), my = U(GetDeviceCaps)(ref, VERTSIZE);
    if (dx > 0 && dy > 0) {
        CHECK(abs(h.rclFrame.left - MulDiv(h.rclBounds.left * 100, mx, dx)) <= 1); /* rounding of the real conversion is not derived: +-1 */
        CHECK(abs(h.rclFrame.right - MulDiv(h.rclBounds.right * 100, mx, dx)) <= 1); /* rounding of the real conversion is not derived: +-1 */
        CHECK(abs(h.rclFrame.top - MulDiv(h.rclBounds.top * 100, my, dy)) <= 1); /* rounding of the real conversion is not derived: +-1 */
        CHECK(abs(h.rclFrame.bottom - MulDiv(h.rclBounds.bottom * 100, my, dy)) <= 1); /* rounding of the real conversion is not derived: +-1 */
    }
    U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_hdr_bounds_cover_drawing) {
    MF_NEED_HDR(); MF_SCREEN(ref);
    HDC m = U(CreateEnhMetaFileW)(ref, NULL, NULL, NULL);
    ENHMETAHEADER h;
    REQUIRE(m);
    U(Rectangle)(m, 10, 10, 50, 40);
    HENHMETAFILE e = U(CloseEnhMetaFile)(m);
    REQUIRE(e);
    CHECK(mf_hdr(e, &h));
    /* bounds are accumulated by the kernel (NtGdiSetBoundsRect 0x8005 on the metafile DC): inclusive device rectangle */
    CHECK(h.rclBounds.left <= 10 && h.rclBounds.top <= 10);
    CHECK(h.rclBounds.right >= 49 && h.rclBounds.bottom >= 39);
    CHECK(h.rclBounds.left >= 8 && h.rclBounds.top >= 8 && h.rclBounds.right <= 52 && h.rclBounds.bottom <= 42);
    t_note("bounds after Rectangle(10,10,50,40): {%ld,%ld,%ld,%ld}", h.rclBounds.left, h.rclBounds.top, h.rclBounds.right, h.rclBounds.bottom);
    U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_hdr_bounds_grow_with_more_drawing) {
    MF_NEED_HDR(); MF_SCREEN(ref);
    HDC m = U(CreateEnhMetaFileW)(ref, NULL, NULL, NULL);
    ENHMETAHEADER h;
    REQUIRE(m);
    U(Rectangle)(m, 20, 20, 30, 30);
    U(Rectangle)(m, 100, 5, 140, 90);
    HENHMETAFILE e = U(CloseEnhMetaFile)(m);
    REQUIRE(e);
    CHECK(mf_hdr(e, &h));
    CHECK(h.rclBounds.left <= 20 && h.rclBounds.top <= 5 && h.rclBounds.right >= 139 && h.rclBounds.bottom >= 89);
    U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_hdr_empty_metafile_shape) {
    MF_NEED_HDR(); MF_SCREEN(ref);
    HDC m = U(CreateEnhMetaFileW)(ref, NULL, NULL, NULL);
    ENHMETAHEADER h;
    REQUIRE(m);
    HENHMETAFILE e = U(CloseEnhMetaFile)(m);
    REQUIRE(e);
    CHECK(mf_hdr(e, &h));
    EXPECT_EQ(h.iType, EMR_HEADER);
    CHECK(h.nRecords >= 2);
    EXPECT_EQ(h.nHandles, 1);                             /* handle 0 is reserved for the metafile itself */
    struct mf_enum en;
    mf_walk(e, &en);
    EXPECT_EQ(en.first, EMR_HEADER);
    EXPECT_EQ(en.last, EMR_EOF);
    EXPECT_EQ((unsigned)en.total, (unsigned)h.nRecords);
    t_note("empty metafile: nRecords=%lu bounds={%ld,%ld,%ld,%ld}", h.nRecords, h.rclBounds.left, h.rclBounds.top, h.rclBounds.right, h.rclBounds.bottom);
    U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_hdr_nhandles_counts_created_objects) {
    MF_NEED_HDR(); MF_SCREEN(ref);
    HDC m = U(CreateEnhMetaFileW)(ref, NULL, NULL, NULL);
    ENHMETAHEADER h;
    REQUIRE(m);
    HGDIOBJ br = U(CreateSolidBrush)(RGB(255, 0, 0)), pen = U(CreatePen)(PS_SOLID, 3, RGB(0, 0, 255));
    U(SelectObject)(m, br);
    U(SelectObject)(m, pen);
    U(Rectangle)(m, 5, 5, 25, 25);
    HENHMETAFILE e = U(CloseEnhMetaFile)(m);
    REQUIRE(e);
    CHECK(mf_hdr(e, &h));
    EXPECT_EQ(h.nHandles, 3);                             /* index 0 + brush + pen */
    struct mf_enum en;
    mf_enum(e, &en);
    EXPECT_EQ(en.nobj, 3);                                /* the handle table length handed to the callback */
    EXPECT_EQ(en.counts[EMR_CREATEBRUSHINDIRECT], 1);
    EXPECT_EQ(en.counts[EMR_CREATEPEN], 1);
    EXPECT_EQ(en.counts[EMR_SELECTOBJECT], 2);
    U(DeleteEnhMetaFile)(e);
    U(DeleteObject)(br); U(DeleteObject)(pen);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_hdr_stock_objects_need_no_handles) {
    MF_NEED_HDR(); MF_SCREEN(ref);
    HDC m = U(CreateEnhMetaFileW)(ref, NULL, NULL, NULL);
    ENHMETAHEADER h;
    REQUIRE(m);
    U(SelectObject)(m, U(GetStockObject)(GRAY_BRUSH));
    U(SelectObject)(m, U(GetStockObject)(NULL_PEN));
    U(Rectangle)(m, 5, 5, 25, 25);
    HENHMETAFILE e = U(CloseEnhMetaFile)(m);
    REQUIRE(e);
    CHECK(mf_hdr(e, &h));
    EXPECT_EQ(h.nHandles, 1);
    struct mf_enum en;
    mf_enum(e, &en);
    EXPECT_EQ(en.counts[EMR_SELECTOBJECT], 2);            /* 0x80000000|stock index form */
    EXPECT_EQ(en.counts[EMR_CREATEPEN] + en.counts[EMR_CREATEBRUSHINDIRECT], 0);
    U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_hdr_description_fields) {
    MF_NEED_HDR(); MF_SCREEN(ref);
    HENHMETAFILE e = mf_make(ref, NULL, DESC, 1);
    ENHMETAHEADER h;
    REQUIRE(e);
    CHECK(mf_hdr(e, &h));
    EXPECT_EQ(h.nDescription, DESC_CCH);
    CHECK(h.offDescription >= 88 && h.offDescription < h.nBytes);
    CHECK((h.offDescription & 1) == 0);
    U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_hdr_no_description_fields) {
    MF_NEED_HDR(); MF_SCREEN(ref);
    HENHMETAFILE e = mf_make(ref, NULL, NULL, 1);
    ENHMETAHEADER h;
    REQUIRE(e);
    CHECK(mf_hdr(e, &h));
    EXPECT_EQ(h.nDescription, 0);
    EXPECT_EQ(U(GetEnhMetaFileDescriptionW)(e, 0, NULL), 0);
    EXPECT_EQ(U(GetEnhMetaFileDescriptionA)(e, 0, NULL), 0);
    U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_desc_w_query_and_copy) {
    MF_NEED_HDR(); MF_SCREEN(ref);
    HENHMETAFILE e = mf_make(ref, NULL, DESC, 1);
    wchar_t buf[64];
    REQUIRE(e);
    EXPECT_EQ(U(GetEnhMetaFileDescriptionW)(e, 0, NULL), DESC_CCH);       /* header.nDescription */
    memset(buf, 0x7f, sizeof buf);
    EXPECT_EQ(U(GetEnhMetaFileDescriptionW)(e, 64, buf), DESC_CCH);       /* never more than nDescription */
    CHECK(memcmp(buf, DESC, sizeof DESC) == 0);
    CHECK(buf[DESC_CCH] == 0x7f7f);                                        /* nothing beyond */
    U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_desc_w_truncated_by_buffer_size) {
    MF_NEED_HDR(); MF_SCREEN(ref);
    HENHMETAFILE e = mf_make(ref, NULL, DESC, 1);
    REQUIRE(e);
    static const UINT cch[] = { 1, 3, 4, 10, 11 };   /* 0 with a non-NULL buffer is not derived */
    for (unsigned i = 0; i < sizeof cch / sizeof cch[0]; i++) {
        wchar_t buf[32]; memset(buf, 0x7f, sizeof buf);
        UINT r = U(GetEnhMetaFileDescriptionW)(e, cch[i], buf);
        EXPECT_EQ(r, cch[i]);                                              /* min(cchBuffer, nDescription) */
        CHECK(memcmp(buf, DESC, cch[i] * 2) == 0);
        CHECK(buf[cch[i]] == 0x7f7f);
    }
    U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_desc_a_query_and_copy) {
    MF_NEED_HDR(); MF_SCREEN(ref);
    HENHMETAFILE e = mf_make(ref, NULL, DESC, 1);
    char buf[64];
    REQUIRE(e);
    UINT q = U(GetEnhMetaFileDescriptionA)(e, 0, NULL);
    EXPECT_EQ(q, DESC_CCH);                                                /* ASCII only: one byte per character */
    memset(buf, 0x7f, sizeof buf);
    EXPECT_EQ(U(GetEnhMetaFileDescriptionA)(e, 64, buf), DESC_CCH);
    CHECK(memcmp(buf, "App\0Title\0", 11) == 0);
    U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_desc_invalid_handles_gdi_error) {
    MF_NEED_HDR();
    EXPECT_EQ(U(GetEnhMetaFileDescriptionW)(NULL, 0, NULL), GDI_ERROR);
    EXPECT_EQ(U(GetEnhMetaFileDescriptionA)(NULL, 0, NULL), GDI_ERROR);
    EXPECT_EQ(U(GetEnhMetaFileDescriptionW)((HENHMETAFILE)(ULONG_PTR)0x460005, 10, (LPWSTR)NULL), GDI_ERROR);
}

TEST(gdi_metafile_print_neg_palette_none_returns_zero) {
    MF_NEED_HDR(); MF_SCREEN(ref);
    HENHMETAFILE e = mf_make(ref, NULL, NULL, 1);
    ENHMETAHEADER h;
    PALETTEENTRY pe[4];
    REQUIRE(e);
    CHECK(mf_hdr(e, &h));
    EXPECT_EQ(h.nPalEntries, 0);
    EXPECT_EQ(U(GetEnhMetaFilePaletteEntries)(e, 0, NULL), 0);
    memset(pe, 0xee, sizeof pe);
    EXPECT_EQ(U(GetEnhMetaFilePaletteEntries)(e, 4, pe), 0);
    CHECK(*(DWORD *)&pe[0] == 0xeeeeeeee);                                 /* no entries copied */
    U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_palette_invalid_handle_gdi_error) {
    MF_NEED_HDR();
    EXPECT_EQ(U(GetEnhMetaFilePaletteEntries)(NULL, 0, NULL), GDI_ERROR);
    EXPECT_EQ(U(GetEnhMetaFilePaletteEntries)((HENHMETAFILE)(ULONG_PTR)0x12345, 0, NULL), GDI_ERROR);
}

TEST(gdi_metafile_print_neg_palette_selected_palette_entries) {
    MF_NEED_HDR(); MF_SCREEN(ref);
    struct { WORD ver, n; PALETTEENTRY e[4]; } lp = { 0x300, 4, { {1, 2, 3, 0}, {4, 5, 6, 0}, {7, 8, 9, 0}, {10, 11, 12, 0} } };
    HPALETTE pal = U(CreatePalette)((LOGPALETTE *)&lp);
    HDC m = U(CreateEnhMetaFileW)(ref, NULL, NULL, NULL);
    ENHMETAHEADER h;
    REQUIRE(pal && m);
    U(SelectPalette)(m, pal, FALSE);
    U(Rectangle)(m, 1, 1, 9, 9);
    HENHMETAFILE e = U(CloseEnhMetaFile)(m);
    REQUIRE(e);
    CHECK(mf_hdr(e, &h));
    UINT n = U(GetEnhMetaFilePaletteEntries)(e, 0, NULL);
    CHECK(n != GDI_ERROR);
    EXPECT_EQ(n, h.nPalEntries);
    t_note("selected 4-entry palette: nPalEntries=%lu", h.nPalEntries);
    if (n == 4) {
        PALETTEENTRY got[4]; memset(got, 0, sizeof got);
        EXPECT_EQ(U(GetEnhMetaFilePaletteEntries)(e, 4, got), 4);
        CHECK(got[0].peRed == 1 && got[3].peBlue == 12);
    }
    U(DeleteEnhMetaFile)(e);
    U(DeleteObject)(pal);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_bits_buffer_protocol) {
    MF_NEED_HDR(); MF_SCREEN(ref);
    HENHMETAFILE e = mf_make(ref, NULL, NULL, 2);
    REQUIRE(e);
    UINT n = U(GetEnhMetaFileBits)(e, 0, NULL);
    CHECK(n >= 88);
    BYTE *big = (BYTE *)malloc(n + 64);
    REQUIRE(big);
    SetLastError(0);
    EXPECT_EQ(U(GetEnhMetaFileBits)(e, n - 1, big), 0);                    /* nSize < header.nBytes */
    EXPECT_ERR(ERROR_INSUFFICIENT_BUFFER);
    memset(big, 0xcc, n + 64);
    EXPECT_EQ(U(GetEnhMetaFileBits)(e, n, big), n);
    CHECK(big[n] == 0xcc);
    memset(big, 0xcc, n + 64);
    EXPECT_EQ(U(GetEnhMetaFileBits)(e, n + 64, big), n);                   /* larger buffer: still nBytes */
    CHECK(big[n] == 0xcc);
    free(big);
    U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_bits_records_walk_to_header_counts) {
    MF_NEED_HDR(); MF_SCREEN(ref);
    HDC m = U(CreateEnhMetaFileW)(ref, NULL, NULL, DESC);
    REQUIRE(m);
    U(MoveToEx)(m, 1, 1, NULL); U(LineTo)(m, 40, 40); U(Ellipse)(m, 2, 2, 30, 30); U(Rectangle)(m, 3, 3, 9, 9);
    HENHMETAFILE e = U(CloseEnhMetaFile)(m);
    UINT n; BYTE *b;
    REQUIRE(e);
    b = mf_getbits(e, &n);
    CHECK(b != NULL);
    if (b) {
        ENHMETAHEADER *h = (ENHMETAHEADER *)b;
        UINT off = 0; int nrec = 0, bad = 0, lastType = 0; UINT lastSize = 0;
        while (off + 8 <= n) {
            DWORD ty = *(DWORD *)(b + off), sz = *(DWORD *)(b + off + 4);
            if (sz < 8 || (sz & 3) || off + sz > n) { bad++; break; }
            lastType = (int)ty; lastSize = sz; off += sz; nrec++;
        }
        CHECK(bad == 0);
        EXPECT_EQ(off, n);                                                  /* record sizes tile the file exactly */
        EXPECT_EQ(nrec, h->nRecords);
        EXPECT_EQ(lastType, EMR_EOF);
        EXPECT_EQ(*(DWORD *)b, EMR_HEADER);
        EXPECT_EQ(*(DWORD *)(b + 4), h->nSize);
        EXPECT_EQ(*(DWORD *)(b + n - 4), lastSize);                         /* EMREOF.nSizeLast */
        free(b);
    }
    U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_bits_eof_record_layout) {
    MF_NEED_HDR(); MF_SCREEN(ref);
    HENHMETAFILE e = mf_make(ref, NULL, NULL, 1);
    UINT n; BYTE *b;
    REQUIRE(e);
    b = mf_getbits(e, &n);
    REQUIRE(b);
    ENHMETAHEADER *h = (ENHMETAHEADER *)b;
    if (h->nPalEntries == 0) {
        EXPECT_EQ(*(DWORD *)(b + n - 20), EMR_EOF);
        EXPECT_EQ(*(DWORD *)(b + n - 16), 20);          /* EMREOF without palette */
        EXPECT_EQ(*(DWORD *)(b + n - 12), 0);           /* nPalEntries */
        EXPECT_EQ(*(DWORD *)(b + n - 8), 16);           /* offPalEntries (points at nSizeLast) */
        EXPECT_EQ(*(DWORD *)(b + n - 4), 20);
    }
    free(b);
    U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_bits_raw_header_matches_api_header) {
    MF_NEED_HDR(); MF_SCREEN(ref);
    HENHMETAFILE e = mf_make(ref, NULL, DESC, 2);
    UINT n; BYTE *b;
    ENHMETAHEADER h;
    REQUIRE(e);
    b = mf_getbits(e, &n);
    REQUIRE(b);
    CHECK(mf_hdr(e, &h));
    CHECK(memcmp(b + 8, &h.rclBounds, 80) == 0);   /* bounds .. szlMillimeters */
    EXPECT_EQ(*(DWORD *)(b + 0x30), h.nBytes);
    EXPECT_EQ(*(DWORD *)(b + 0x3c), h.nDescription);
    EXPECT_EQ(*(DWORD *)(b + 0x40), h.offDescription);
    CHECK(memcmp(b + h.offDescription, DESC, DESC_CCH * 2) == 0);   /* description stored as UTF-16 inside the header record */
    free(b);
    U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_setbits_roundtrip_new_handle) {
    MF_NEED_HDR(); MF_SCREEN(ref);
    HENHMETAFILE e = mf_make(ref, NULL, DESC, 3);
    UINT n; BYTE *b;
    REQUIRE(e);
    b = mf_getbits(e, &n);
    REQUIRE(b);
    HENHMETAFILE e2 = U(SetEnhMetaFileBits)(n, b);
    CHECK(e2 != NULL && e2 != e);
    if (e2) {
        UINT n2; BYTE *b2 = mf_getbits(e2, &n2);
        EXPECT_EQ(MF_TYPE7(e2), MF_T7_EMF);
        EXPECT_EQ(U(GetObjectType)(e2), OBJ_ENHMETAFILE);
        EXPECT_EQ(n2, n);
        CHECK(b2 && memcmp(b, b2, n) == 0);
        free(b2);
        U(DeleteEnhMetaFile)(e);                    /* the copy is independent of the original */
        e = NULL;
        EXPECT_EQ(U(GetEnhMetaFileBits)(e2, 0, NULL), n);
        CHECK(U(DeleteEnhMetaFile)(e2));
    }
    if (e) U(DeleteEnhMetaFile)(e);
    free(b);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_setbits_too_small_fails_13) {
    MF_NEED_HDR();
    static const UINT sizes[] = { 0, 1, 8, 40, 87 };
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        BYTE dummy[128]; memset(dummy, 0, sizeof dummy);
        SetLastError(0);
        CHECK(U(SetEnhMetaFileBits)(sizes[i], dummy) == NULL);   /* nSize < 0x58 is rejected before pb is read */
        EXPECT_ERR(ERROR_INVALID_DATA);
    }
    SetLastError(0);
    CHECK(U(SetEnhMetaFileBits)(0, NULL) == NULL);
    EXPECT_ERR(ERROR_INVALID_DATA);
}

TEST(gdi_metafile_print_neg_setbits_truncated_stream_fails_13) {
    MF_NEED_HDR(); MF_SCREEN(ref);
    HENHMETAFILE e = mf_make(ref, NULL, NULL, 2);
    UINT n; BYTE *b;
    REQUIRE(e);
    b = mf_getbits(e, &n);
    REQUIRE(b);
    for (UINT cut = 1; cut <= 8; cut += 7) {
        SetLastError(0);
        CHECK(U(SetEnhMetaFileBits)(n - cut, b) == NULL);        /* nSize < header.nBytes */
        EXPECT_ERR(ERROR_INVALID_DATA);
    }
    free(b);
    U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_setbits_header_nbytes_too_small_fails_13) {
    MF_NEED_HDR(); MF_SCREEN(ref);
    HENHMETAFILE e = mf_make(ref, NULL, NULL, 2);
    UINT n; BYTE *b;
    REQUIRE(e);
    b = mf_getbits(e, &n);
    REQUIRE(b);
    *(DWORD *)(b + 0x30) = 0x50;                                 /* header.nBytes < 0x58 */
    SetLastError(0);
    CHECK(U(SetEnhMetaFileBits)(n, b) == NULL);
    EXPECT_ERR(ERROR_INVALID_DATA);
    *(DWORD *)(b + 0x30) = n + 4;                                /* header.nBytes > nSize */
    SetLastError(0);
    CHECK(U(SetEnhMetaFileBits)(n, b) == NULL);
    EXPECT_ERR(ERROR_INVALID_DATA);
    free(b);
    U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_setbits_bad_signature_observed) {
    MF_NEED_HDR(); MF_SCREEN(ref);
    HENHMETAFILE e = mf_make(ref, NULL, NULL, 1);
    UINT n; BYTE *b;
    REQUIRE(e);
    b = mf_getbits(e, &n);
    REQUIRE(b);
    *(DWORD *)(b + 0x28) = 0x12345678;                           /* dSignature */
    HENHMETAFILE e2 = U(SetEnhMetaFileBits)(n, b);
    t_note("SetEnhMetaFileBits with a corrupt dSignature -> %p (err %lu)", (void *)e2, GetLastError());
    if (e2) U(DeleteEnhMetaFile)(e2);
    free(b);
    U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_setbits_trailing_bytes_ignored) {
    MF_NEED_HDR(); MF_SCREEN(ref);
    HENHMETAFILE e = mf_make(ref, NULL, NULL, 1);
    UINT n; BYTE *b;
    REQUIRE(e);
    b = mf_getbits(e, &n);
    REQUIRE(b);
    BYTE *pad = (BYTE *)calloc(1, n + 100);
    REQUIRE(pad);
    memcpy(pad, b, n);
    HENHMETAFILE e2 = U(SetEnhMetaFileBits)(n + 100, pad);       /* only nSize < nBytes is an error */
    CHECK(e2 != NULL);
    if (e2) {
        EXPECT_EQ(U(GetEnhMetaFileBits)(e2, 0, NULL), n);        /* GetEnhMetaFileBits reports header.nBytes */
        U(DeleteEnhMetaFile)(e2);
    }
    free(pad); free(b);
    U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_copy_memory_equal_bits_new_handle) {
    MF_NEED_HDR(); MF_SCREEN(ref);
    HENHMETAFILE e = mf_make(ref, NULL, DESC, 2);
    REQUIRE(e);
    if (!U(CopyEnhMetaFileW)) { t_skip("CopyEnhMetaFileW missing"); U(DeleteEnhMetaFile)(e); mf_screen_free(ref); return; }
    HENHMETAFILE c = U(CopyEnhMetaFileW)(e, NULL);
    CHECK(c != NULL && c != e);
    if (c) {
        UINT n1, n2; BYTE *b1 = mf_getbits(e, &n1), *b2 = mf_getbits(c, &n2);
        EXPECT_EQ(MF_TYPE7(c), MF_T7_EMF);
        EXPECT_EQ(n1, n2);
        CHECK(b1 && b2 && n1 == n2 && memcmp(b1, b2, n1) == 0);
        free(b1); free(b2);
        CHECK(U(DeleteEnhMetaFile)(e)); e = NULL;
        CHECK(U(GetEnhMetaFileBits)(c, 0, NULL) != 0);                /* copy outlives the source */
        CHECK(U(DeleteEnhMetaFile)(c));
    }
    if (e) U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_copy_to_file_creates_file_metafile) {
    MF_NEED_HDR(); MF_SCREEN(ref);
    HENHMETAFILE e = mf_make(ref, NULL, NULL, 2);
    char a[MAX_PATH]; wchar_t w[MAX_PATH];
    REQUIRE(e);
    if (!U(CopyEnhMetaFileW)) { t_skip("CopyEnhMetaFileW missing"); U(DeleteEnhMetaFile)(e); mf_screen_free(ref); return; }
    t_tmpfile(a, sizeof a, "copy.emf");
    MultiByteToWideChar(CP_ACP, 0, a, -1, w, MAX_PATH);
    HENHMETAFILE c = U(CopyEnhMetaFileW)(e, w);
    CHECK(c != NULL);
    if (c) {
        ENHMETAHEADER h;
        WIN32_FILE_ATTRIBUTE_DATA fa;
        CHECK(mf_hdr(c, &h));
        CHECK(GetFileAttributesExA(a, GetFileExInfoStandard, &fa));
        EXPECT_EQ(fa.nFileSizeLow, h.nBytes);                          /* CopyEnhMetaFileW writes nBytes then GetEnhMetaFileW */
        EXPECT_EQ(h.nBytes, U(GetEnhMetaFileBits)(e, 0, NULL));
        U(DeleteEnhMetaFile)(c);
    }
    DeleteFileA(a);
    U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_copy_invalid_handle_fails) {
    MF_NEED_HDR();
    if (!U(CopyEnhMetaFileW)) { t_skip("CopyEnhMetaFileW missing"); return; }
    HENHMETAFILE bad[] = { NULL, (HENHMETAFILE)(ULONG_PTR)0x460010, (HENHMETAFILE)(ULONG_PTR)0x00210002 };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(0);
        CHECK(U(CopyEnhMetaFileW)(bad[i], NULL) == NULL);
        EXPECT_ERR(ERROR_INVALID_HANDLE);                              /* pvClientObjGet */
    }
}

TEST(gdi_metafile_print_neg_copy_a_variant_matches_w) {
    MF_NEED_HDR(); MF_SCREEN(ref);
    HENHMETAFILE e = mf_make(ref, NULL, NULL, 1);
    REQUIRE(e);
    if (!U(CopyEnhMetaFileA)) { t_skip("CopyEnhMetaFileA missing"); U(DeleteEnhMetaFile)(e); mf_screen_free(ref); return; }
    HENHMETAFILE c = U(CopyEnhMetaFileA)(e, NULL);
    CHECK(c != NULL);
    if (c) {
        EXPECT_EQ(U(GetEnhMetaFileBits)(c, 0, NULL), U(GetEnhMetaFileBits)(e, 0, NULL));
        U(DeleteEnhMetaFile)(c);
    }
    U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_many_metafile_handles_have_distinct_indices) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    enum { N = 64 };
    HENHMETAFILE e[N]; int made = 0;
    for (int i = 0; i < N; i++) { e[i] = mf_make(ref, NULL, NULL, 1); if (!e[i]) break; made++; }
    EXPECT_EQ(made, N);
    for (int i = 0; i < made; i++)
        for (int j = i + 1; j < made; j++) if (MF_IDX(e[i]) == MF_IDX(e[j]) || e[i] == e[j]) { CHECKF(0, "handles %d and %d collide", i, j); i = j = made; }
    for (int i = 0; i < made; i++) CHECK(U(DeleteEnhMetaFile)(e[i]));
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_deleted_handle_index_reuse_bumps_uniq) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    HENHMETAFILE a = mf_make(ref, NULL, NULL, 1);
    REQUIRE(a);
    CHECK(U(DeleteEnhMetaFile)(a));
    HENHMETAFILE b = mf_make(ref, NULL, NULL, 1);
    REQUIRE(b);
    if (MF_IDX(a) == MF_IDX(b)) CHECK(MF_UNIQ(a) != MF_UNIQ(b) || a != b);   /* reused slot must not alias the stale handle */
    CHECK(a != b || U(GetObjectType)(a) == OBJ_ENHMETAFILE);
    U(DeleteEnhMetaFile)(b);
    mf_screen_free(ref);
}
