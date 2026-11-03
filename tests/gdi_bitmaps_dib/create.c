/* NtGdiCreateBitmap: win32kfull!NtGdiCreateBitmap -> the kernel bitmap-creation path.
 * bitmap creation rejects cx < 1 || cx > 0x7ffffff, cy < 1, planes > 32, bpp > 32, planes*bpp > 32 and any bitmap whose
 * WORD-aligned size exceeds 0xffffffff with ERROR_INVALID_PARAMETER (EngSetLastError(0x57)) and returns NULL.
 * The format is chosen from planes*bpp: <2 mono, <5 4bpp, <9 8bpp, <=16 16bpp, <=24 24bpp, else 32bpp. */
#include "bd.h"

/* ---- format selection: planes * bpp -> bmBitsPixel ------------------------------------------------------------------- */
#define CB_ROWS(X) \
    X(p1_b0,   1, 0,  1) X(p0_b1, 0, 1, 1) X(p0_b0, 0, 0, 1) X(p1_b1, 1, 1, 1) X(p1_b2, 1, 2, 4) X(p1_b3, 1, 3, 4) \
    X(p1_b4,   1, 4,  4) X(p1_b5, 1, 5, 8) X(p1_b7, 1, 7, 8) X(p1_b8, 1, 8, 8) X(p1_b9, 1, 9, 16) X(p1_b15, 1, 15, 16) \
    X(p1_b16,  1, 16, 16) X(p1_b17, 1, 17, 24) X(p1_b24, 1, 24, 24) X(p1_b25, 1, 25, 32) X(p1_b32, 1, 32, 32) \
    X(p2_b1,   2, 1,  4) X(p2_b2, 2, 2, 4) X(p3_b1, 3, 1, 4) X(p2_b4, 2, 4, 8) X(p4_b2, 4, 2, 8) X(p4_b4, 4, 4, 16) \
    X(p2_b8,   2, 8,  16) X(p2_b12, 2, 12, 24) X(p2_b16, 2, 16, 32) X(p32_b1, 32, 1, 32) X(p8_b4, 8, 4, 32) \
    X(p3_b5,   3, 5,  16) X(p5_b5, 5, 5, 32) X(p6_b4, 6, 4, 24)

#define X(nm, pl, bp, want) \
TEST(bd_create_format_##nm) { \
    BD_NEED(); BD_NEED_NT(NtGdiCreateBitmap); \
    /* direct syscall and the gdi32 wrapper must build the same format; bmBitsPixel comes from galBitsPerPixel[format] */ \
    HBITMAP a = UN(NtGdiCreateBitmap)(6, 5, pl, bp, NULL), b = U(CreateBitmap)(6, 5, pl, bp, NULL); \
    BITMAP ba, bb; \
    CHECK(a != NULL); CHECK(b != NULL); \
    if (a) { CHECK(bd_getobj(a, &ba) == sizeof(BITMAP)); \
        CHECKF(ba.bmBitsPixel == (want), "syscall %u*%u: bmBitsPixel %u want %u", pl, bp, ba.bmBitsPixel, (unsigned)(want)); \
        CHECK(ba.bmWidth == 6 && ba.bmHeight == 5 && ba.bmPlanes == 1 && ba.bmType == 0); \
        CHECK(ba.bmBits == NULL); /* a DDB has no user-visible bits (GetObject leaves bmBits 0) */ \
        CHECKF(ba.bmWidthBytes == bd_ddb_stride(6, ba.bmBitsPixel), "stride %d", ba.bmWidthBytes); } \
    if (b) { CHECK(bd_getobj(b, &bb) == sizeof(BITMAP)); CHECK(bb.bmBitsPixel == (want)); } \
    if (a) { U(DeleteObject)(a); } if (b) { U(DeleteObject)(b); } \
}
CB_ROWS(X)
#undef X

/* ---- parameter rejection ------------------------------------------------------------------------------------------- */
#define CB_BAD(X) \
    X(cx0,    0, 4, 1, 1) X(cx_neg1, -1, 4, 1, 1) X(cx_min, (int)0x80000000, 4, 1, 1) X(cx_over, 0x8000000, 4, 1, 1) \
    X(cx_max_int, 0x7fffffff, 4, 1, 1) X(cy0, 4, 0, 1, 1) X(cy_neg1, 4, -1, 1, 1) X(cy_min, 4, (int)0x80000000, 1, 1) \
    X(bpp33, 4, 4, 1, 33) X(bpp64, 4, 4, 1, 64) X(planes33, 4, 4, 33, 1) X(prod34, 4, 4, 2, 17) X(prod35, 4, 4, 5, 7) \
    X(planes_hi, 4, 4, 0x10001, 1) X(bpp_hi, 4, 4, 1, 0x10001) X(planes_ffff, 4, 4, 0xffffffffu, 1) \
    X(bpp_ffff, 4, 4, 1, 0xffffffffu) X(both_over, 4, 4, 33, 33) X(huge_bytes, 0x7ffffff, 64, 1, 32) \
    X(huge_both, 0x7ffffff, 0x7ffffff, 1, 1) X(huge_h, 1, 0x7fffffff, 1, 32)

#define X(nm, cx, cy, pl, bp) \
TEST(bd_create_reject_##nm) { \
    BD_NEED_NT(NtGdiCreateBitmap); \
    SET_ERR(0); \
    HBITMAP h = UN(NtGdiCreateBitmap)(cx, cy, pl, bp, NULL); \
    /* bitmap creation: any range violation or a size above 0xffffffff bytes -> EngSetLastError(ERROR_INVALID_PARAMETER) */ \
    CHECKF(h == NULL, "handle %p", h); \
    if (h) { U(DeleteObject)(h); return; } \
    EXPECT_ERR(BD_ERR_INVALID_PARAM); \
}
CB_BAD(X)
#undef X

/* gdi32full!CreateBitmap: a zero width or height never reaches the kernel; it returns the stock 1x1 bitmap (index 0x15). */
TEST(bd_create_zero_width_is_stock_bitmap) {
    BD_NEED(); REQUIRE(U(GetStockObject));
    HBITMAP a = U(CreateBitmap)(0, 7, 1, 32, NULL);
    CHECK(a == U(GetStockObject)(0x15));
    if (a) { CHECK(U(GetObjectType)(a) == OBJ_BITMAP); }
}
TEST(bd_create_zero_height_is_stock_bitmap) {
    BD_NEED(); REQUIRE(U(GetStockObject));
    HBITMAP a = U(CreateBitmap)(7, 0, 1, 32, NULL);
    CHECK(a == U(GetStockObject)(0x15));
}
TEST(bd_create_zero_both_is_stock_bitmap) {
    BD_NEED(); REQUIRE(U(GetStockObject));
    CHECK(U(CreateBitmap)(0, 0, 1, 1, NULL) == U(GetStockObject)(0x15));
}
TEST(bd_create_zero_with_bits_is_stock_bitmap) {
    BD_NEED(); REQUIRE(U(GetStockObject));
    BYTE junk[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    CHECK(U(CreateBitmap)(0, 4, 1, 1, junk) == U(GetStockObject)(0x15));
}
TEST(bd_create_stock_bitmap_is_1x1_mono) {
    BD_NEED(); REQUIRE(U(GetStockObject));
    HBITMAP s = U(GetStockObject)(0x15);
    if (!s) { t_skip("no stock bitmap"); return; }
    BITMAP bm;
    CHECK(U(GetObjectType)(s) == OBJ_BITMAP);
    CHECK(bd_getobj(s, &bm) == sizeof bm);
    CHECK(bm.bmWidth == 1 && bm.bmHeight == 1 && bm.bmBitsPixel == 1 && bm.bmPlanes == 1);
    CHECK(BD_TYPE7(s) == 5);
    if (!BD_STOCK(s)) t_note("stock bitmap handle %p has no stock bit", s);
}
TEST(bd_create_stock_bitmap_delete_is_refused) {
    BD_NEED(); REQUIRE(U(GetStockObject));
    HBITMAP s = U(GetStockObject)(0x15);
    if (!s) { t_skip("no stock bitmap"); return; }
    /* NtGdiDeleteObjectApp: a handle with the stock bit (0x800000) returns TRUE immediately and deletes nothing */
    U(DeleteObject)(s);
    CHECK(U(GetObjectType)(s) == OBJ_BITMAP);
    CHECK(U(GetStockObject)(0x15) == s);
}
TEST(bd_create_neg_width_userland_fails) {
    BD_NEED();
    SET_ERR(0);
    HBITMAP h = U(CreateBitmap)(-3, 4, 1, 1, NULL);
    CHECK(h == NULL); if (h) U(DeleteObject)(h); else EXPECT_ERR(BD_ERR_INVALID_PARAM);
}
TEST(bd_create_neg_height_userland_fails) {
    BD_NEED();
    SET_ERR(0);
    HBITMAP h = U(CreateBitmap)(4, -3, 1, 1, NULL);
    CHECK(h == NULL); if (h) U(DeleteObject)(h); else EXPECT_ERR(BD_ERR_INVALID_PARAM);
}
TEST(bd_create_userland_int_overflow_with_bits) {
    BD_NEED();
    /* gdi32full!CreateBitmap: with pBits != NULL, (stride * height) as an int < 0 -> GdiSetLastError(87) before any syscall */
    BYTE b[4] = { 0 };
    SET_ERR(0);
    HBITMAP h = U(CreateBitmap)(0x7ffffff, 0x7ffffff, 1, 1, b);
    CHECK(h == NULL); if (h) U(DeleteObject)(h); else EXPECT_ERR(BD_ERR_INVALID_PARAM);
}
TEST(bd_create_direct_size_overflow_with_bits_no_access) {
    BD_NEED_NT(NtGdiCreateBitmap);
    /* NtGdiCreateBitmap: stride*cy >= 2^32 skips MmSecureVirtualMemory and never creates the object; the buffer is not read */
    BYTE b[4] = { 0 };
    HBITMAP h = UN(NtGdiCreateBitmap)(0x7ffffff, 0x7ffffff, 1, 1, b);
    CHECK(h == NULL); if (h) U(DeleteObject)(h);
}

/* ---- bit copy-in -------------------------------------------------------------------------------------------------- */
#define CB_COPY(X) \
    X(mono_8x8, 8, 8, 1) X(mono_16x3, 16, 3, 1) X(mono_32x2, 32, 2, 1) X(mono_64x1, 64, 1, 1) \
    X(bpp4_8x4, 8, 4, 4) X(bpp4_6x3, 6, 3, 4) \
    X(bpp8_4x4, 4, 4, 8) X(bpp8_3x5, 3, 5, 8) X(bpp8_7x2, 7, 2, 8) X(bpp8_16x16, 16, 16, 8) \
    X(bpp16_2x2, 2, 2, 16) X(bpp16_5x3, 5, 3, 16) \
    X(bpp24_2x2, 2, 2, 24) X(bpp24_3x3, 3, 3, 24) X(bpp24_5x2, 5, 2, 24) \
    X(bpp32_1x1, 1, 1, 32) X(bpp32_4x4, 4, 4, 32) X(bpp32_9x7, 9, 7, 32) X(bpp32_64x64, 64, 64, 32)

static void bd_fill(BYTE *p, size_t n, unsigned seed) {
    for (size_t i = 0; i < n; i++) p[i] = (BYTE)((i * 37 + 11 + seed * 13) & 0xff);
}
/* Compare only the pixel bytes of each row: padding to the WORD boundary is kernel business. */
static int bd_rows_equal(const BYTE *a, const BYTE *b, int w, int h, int bpp, unsigned stride) {
    unsigned rb = ((unsigned)w * bpp + 7) / 8;
    for (int y = 0; y < h; y++)
        if (memcmp(a + (size_t)y * stride, b + (size_t)y * stride, rb)) return 0;
    return 1;
}

#define X(nm, w, h, bpp) \
TEST(bd_create_copyin_##nm) { \
    BD_NEED(); BD_NEED_NT(NtGdiCreateBitmap); BD_NEED_NT(NtGdiGetBitmapBits); \
    unsigned st = bd_ddb_stride(w, bpp); size_t n = (size_t)st * (h); \
    BYTE src[16384], got[16384]; \
    CHECK(n <= sizeof src); \
    bd_fill(src, n, bpp); memset(got, 0xee, sizeof got); \
    /* NtGdiCreateBitmap secures the buffer (stride*cy bytes) and bitmap creation copies it into the new surface */ \
    HBITMAP hb = UN(NtGdiCreateBitmap)(w, h, 1, bpp, src); \
    CHECK(hb != NULL); if (!hb) return; \
    LONG r = UN(NtGdiGetBitmapBits)(hb, (ULONG)n, got); \
    CHECKF(r == (LONG)n, "GetBitmapBits returned %ld want %zu", r, n); \
    CHECK(bd_rows_equal(src, got, w, h, bpp, st)); \
    /* the userland wrapper builds the same bitmap */ \
    HBITMAP hb2 = U(CreateBitmap)(w, h, 1, bpp, src); \
    CHECK(hb2 != NULL); \
    if (hb2) { memset(got, 0xee, sizeof got); r = UN(NtGdiGetBitmapBits)(hb2, (ULONG)n, got); \
        CHECK(r == (LONG)n); CHECK(bd_rows_equal(src, got, w, h, bpp, st)); U(DeleteObject)(hb2); } \
    U(DeleteObject)(hb); \
}
CB_COPY(X)
#undef X

TEST(bd_create_no_bits_leaves_zero_fill) {
    BD_NEED(); BD_NEED_NT(NtGdiCreateBitmap); BD_NEED_NT(NtGdiGetBitmapBits);
    /* bitmap creation with pBits == NULL does not call the copy hook, and DIB creation zero-fills nothing visible
     * to us other than what GetBitmapBits reports; a mono bitmap created blank reads back as zero (black). */
    HBITMAP hb = UN(NtGdiCreateBitmap)(16, 4, 1, 1, NULL);
    REQUIRE(hb);
    BYTE got[64]; memset(got, 0xaa, sizeof got);
    LONG r = UN(NtGdiGetBitmapBits)(hb, 8, got);
    CHECK(r == 8);
    int nz = 0; for (int i = 0; i < 8; i++) nz |= got[i];
    if (nz) t_note("blank mono bitmap not all-zero: first bytes %02x %02x", got[0], got[1]);
    U(DeleteObject)(hb);
}

/* ---- handles produced by the family ------------------------------------------------------------------------------- */
TEST(bd_create_handle_type_bits) {
    BD_NEED(); BD_NEED_NT(NtGdiCreateBitmap);
    HBITMAP a = UN(NtGdiCreateBitmap)(4, 4, 1, 1, NULL), b = UN(NtGdiCreateBitmap)(4, 4, 1, 32, NULL);
    REQUIRE(a && b);
    /* GetObject / surface handle check (type 5): bitmap handles carry type 5 in bits 16..20 */
    CHECK(BD_TYPE7(a) == 5 && BD_TYPE7(b) == 5);
    CHECK(!BD_STOCK(a) && !BD_STOCK(b));
    CHECK(a != b);
    CHECK((ULONG_PTR)a == (ULONG_PTR)(LONG_PTR)(int)(unsigned)(ULONG_PTR)a);
    CHECK(U(GetObjectType)(a) == OBJ_BITMAP && U(GetObjectType)(b) == OBJ_BITMAP);
    U(DeleteObject)(a); U(DeleteObject)(b);
}
TEST(bd_create_cell_fields) {
    BD_NEED(); BD_NEED_NT(NtGdiCreateBitmap);
    HBITMAP h = UN(NtGdiCreateBitmap)(4, 4, 1, 32, NULL);
    REQUIRE(h);
    struct bd_cell c;
    if (!bd_cell_read(h, &c)) { U(DeleteObject)(h); t_skip("GDI shared table unreadable"); return; }
    /* ENTRYOBJ::hSetup: Objt = handle type & 0x1f, UpperHandleBits = HIWORD(handle), owner = our pid */
    CHECKF(c.objt == 5, "objt 0x%x", c.objt);
    CHECKF(c.upper == (unsigned short)((ULONG_PTR)h >> 16), "upper 0x%x", c.upper);
    CHECKF((c.owner & ~1u) == GetCurrentProcessId(), "owner 0x%x", c.owner);
    CHECKF(c.puser == 0, "pUser 0x%llx (a DDB has no client attribute page)", c.puser);
    CHECKF(c.q0 >= 0xFFFF800000000000ull, "q0 0x%llx not a kernel-range value", c.q0);
    U(DeleteObject)(h);
}
TEST(bd_create_cell_cleared_on_delete) {
    BD_NEED(); BD_NEED_NT(NtGdiCreateBitmap);
    HBITMAP h = UN(NtGdiCreateBitmap)(4, 4, 1, 1, NULL);
    REQUIRE(h);
    CHECK(U(DeleteObject)(h));
    struct bd_cell c;
    if (!bd_cell_read(h, &c)) { t_skip("GDI shared table unreadable"); return; }
    CHECKF(c.objt == 0, "objt after delete 0x%x", c.objt);
    CHECKF(c.puser == 0, "pUser after delete 0x%llx", c.puser);
    BITMAP bmi;
    CHECK(U(GetObjectW)(h, sizeof bmi, &bmi) == 0);
    t_note("GetObjectType of a deleted bitmap handle: %lu (decoded from the handle)", U(GetObjectType)(h));
}
TEST(bd_create_delete_twice_second_fails) {
    BD_NEED(); BD_NEED_NT(NtGdiCreateBitmap); BD_NEED_NT(NtGdiDeleteObjectApp);
    HBITMAP h = UN(NtGdiCreateBitmap)(2, 2, 1, 1, NULL);
    REQUIRE(h);
    CHECK((UN(NtGdiDeleteObjectApp)(h) & 0xff) != 0);
    CHECK((UN(NtGdiDeleteObjectApp)(h) & 0xff) == 0); /* the cell is Objt 0 now: nothing to lock */
}
TEST(bd_create_count_in_gui_resources) {
    BD_NEED(); REQUIRE(U(GetGuiResources));
    DWORD a = U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS);
    HBITMAP h[5];
    for (int i = 0; i < 5; i++) { h[i] = U(CreateBitmap)(2 + i, 2, 1, 1, NULL); CHECK(h[i] != NULL); }
    DWORD b = U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS);
    CHECKF(b == a + 5, "GDI object count %lu -> %lu", a, b);
    for (int i = 0; i < 5; i++) if (h[i]) U(DeleteObject)(h[i]);
    CHECK(U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS) == a);
}
TEST(bd_create_many_distinct_handles) {
    BD_NEED(); BD_NEED_NT(NtGdiCreateBitmap);
    enum { N = 64 };
    HBITMAP h[N];
    int ok = 1;
    for (int i = 0; i < N; i++) h[i] = UN(NtGdiCreateBitmap)(1 + i % 7, 1 + i % 5, 1, 1 << (i % 6), NULL);
    for (int i = 0; i < N; i++) { if (!h[i]) ok = 0; for (int j = 0; j < i; j++) if (h[j] == h[i]) ok = 0; }
    CHECK(ok);
    for (int i = 0; i < N; i++) if (h[i]) CHECK(U(DeleteObject)(h[i]));
}
TEST(bd_create_dimensions_roundtrip_table) {
    BD_NEED(); BD_NEED_NT(NtGdiCreateBitmap);
    static const int sz[][2] = { {1,1},{2,1},{1,2},{7,3},{16,16},{17,9},{100,1},{1,100},{255,3},{256,256},{1000,2} };
    for (size_t i = 0; i < sizeof sz / sizeof sz[0]; i++) {
        HBITMAP h = UN(NtGdiCreateBitmap)(sz[i][0], sz[i][1], 1, 32, NULL);
        BITMAP bm;
        CHECK(h != NULL); if (!h) continue;
        CHECK(bd_getobj(h, &bm) == sizeof bm);
        CHECKF(bm.bmWidth == sz[i][0] && bm.bmHeight == sz[i][1], "%dx%d -> %ldx%ld", sz[i][0], sz[i][1], bm.bmWidth, bm.bmHeight);
        U(DeleteObject)(h);
    }
}
TEST(bd_create_getbitmapbits_size_matches_formula) {
    BD_NEED(); BD_NEED_NT(NtGdiCreateBitmap); BD_NEED_NT(NtGdiGetBitmapBits);
    static const int bpp[] = { 1, 4, 8, 16, 24, 32 };
    for (int k = 0; k < 6; k++) {
        HBITMAP h = UN(NtGdiCreateBitmap)(13, 7, 1, bpp[k], NULL);
        REQUIRE(h);
        /* GetBitmapBits: ((bpp*cx + 15) >> 3 & ~1) * cy; with a NULL buffer the Nt wrapper returns exactly that size */
        LONG r = UN(NtGdiGetBitmapBits)(h, 0x7fffffff, NULL);
        CHECKF(r == (LONG)(bd_ddb_stride(13, bpp[k]) * 7), "bpp %d size %ld", bpp[k], r);
        U(DeleteObject)(h);
    }
}
