/* Pixel transfer: NtGdiGetBitmapBits / NtGdiSetBitmapBits, NtGdiGetDIBitsInternal, NtGdiSetDIBitsToDeviceInternal,
 * NtGdiStretchDIBitsInternal and NtGdiCreateDIBitmapInternal, validated against the DIB section's own mapped memory.
 *
 * Bitmap bit order facts used below:
 *  - Get/SetBitmapBits work in surface order, WORD-aligned rows ((bpp*w+15)>>3 & ~1) * h, row 0 = TOP scan line.
 *  - A bottom-up DIB section stores its TOP picture row at the highest memory row; a top-down one at memory row 0.
 *  - SetDIBits/SetDIBitsToDevice/GetDIBits count scan lines from the BOTTOM of a bottom-up DIB. */
#include "bd.h"

static DWORD px32(int x, int y, unsigned seed) { return (DWORD)(((seed * 0x01010101u) ^ ((unsigned)y << 16) ^ ((unsigned)x << 8) ^ 0xff000000u) & 0x00ffffffu); }

/* fill a DIB image buffer in memory order: row r holds picture row (bottom_up ? h-1-r : r) */
static void fill32(DWORD *p, int w, int h, int bottom_up, unsigned seed) {
    for (int r = 0; r < h; r++) {
        int pic = bottom_up ? h - 1 - r : r;
        for (int x = 0; x < w; x++) p[r * w + x] = px32(x, pic, seed);
    }
}
static int same_rgb(DWORD a, DWORD b) { return (a & 0xffffff) == (b & 0xffffff); }

/* compare two 32bpp images in memory order, ignoring the alpha byte; returns count of differing pixels */
static int diff32(const DWORD *a, const DWORD *b, int n) { int d = 0; for (int i = 0; i < n; i++) if (!same_rgb(a[i], b[i])) d++; return d; }

/* ---- GetBitmapBits / SetBitmapBits --------------------------------------------------------------------------------------- */
#define BB_DIB(X) X(b1, 1) X(b4, 4) X(b8, 8) X(b16, 16) X(b24, 24) X(b32, 32)
#define X(nm, bpp) \
TEST(bd_bits_get_null_returns_size_dib_##nm) { \
    BD_NEED(); BD_NEED_NT(NtGdiGetBitmapBits); \
    HBITMAP hb = bd_dib(13, 7, bpp, NULL); REQUIRE(hb); \
    /* the kernel sizes every bitmap, DIB sections included, with the WORD-aligned formula (not the DWORD DIB stride) */ \
    LONG n = UN(NtGdiGetBitmapBits)(hb, 0x7fffffff, NULL); \
    CHECKF(n == (LONG)(bd_ddb_stride(13, bpp) * 7), "bpp %d: %ld", bpp, n); \
    U(DeleteObject)(hb); \
}
BB_DIB(X)
#undef X

TEST(bd_bits_get_short_buffer_returns_cb) {
    BD_NEED(); BD_NEED_NT(NtGdiGetBitmapBits); BD_NEED_NT(NtGdiSetBitmapBits);
    BYTE src[16 * 4 * 4], got[16 * 4 * 4];
    for (size_t i = 0; i < sizeof src; i++) src[i] = (BYTE)(i * 5 + 1);
    HBITMAP hb = U(CreateBitmap)(16, 4, 1, 32, src); REQUIRE(hb);
    static const ULONG cbs[] = { 1, 2, 3, 4, 7, 16, 17, 63, 64, 100, 255 };
    for (size_t i = 0; i < sizeof cbs / sizeof cbs[0]; i++) {
        memset(got, 0xee, sizeof got);
        LONG r = UN(NtGdiGetBitmapBits)(hb, cbs[i], got);
        CHECKF(r == (LONG)cbs[i], "cb %lu -> %ld", cbs[i], r);
        CHECKF(!memcmp(got, src, cbs[i]), "cb %lu prefix differs", cbs[i]);
        CHECKF(got[cbs[i]] == 0xee, "cb %lu wrote one byte too many", cbs[i]);
    }
    U(DeleteObject)(hb);
}
TEST(bd_bits_get_cb_above_size_is_clamped) {
    BD_NEED(); BD_NEED_NT(NtGdiGetBitmapBits);
    BYTE src[8 * 2 * 2], got[512];
    for (size_t i = 0; i < sizeof src; i++) src[i] = (BYTE)(i + 9);
    HBITMAP hb = U(CreateBitmap)(8, 2, 1, 16, src); REQUIRE(hb);
    memset(got, 0xee, sizeof got);
    LONG r = UN(NtGdiGetBitmapBits)(hb, sizeof got, got);
    CHECKF(r == (LONG)sizeof src, "ret %ld", r);
    CHECK(!memcmp(got, src, sizeof src));
    CHECK(got[sizeof src] == 0xee);
    U(DeleteObject)(hb);
}
TEST(bd_bits_get_cb_zero_with_buffer_returns_zero) {
    BD_NEED(); BD_NEED_NT(NtGdiGetBitmapBits);
    HBITMAP hb = U(CreateBitmap)(8, 2, 1, 16, NULL); REQUIRE(hb);
    BYTE got[16]; memset(got, 0xee, sizeof got);
    CHECK(UN(NtGdiGetBitmapBits)(hb, 0, got) == 0);
    CHECK(got[0] == 0xee);
    U(DeleteObject)(hb);
}
TEST(bd_bits_get_invalid_handles) {
    BD_NEED(); BD_NEED_NT(NtGdiGetBitmapBits); REQUIRE(U(CreateSolidBrush));
    BYTE got[16]; memset(got, 0xee, sizeof got);
    HBRUSH br = U(CreateSolidBrush)(RGB(1, 2, 3)); REQUIRE(br);
    HDC dc = bd_mem_dc();
    SET_ERR(0);
    /* handle lookup fails -> EngSetLastError(ERROR_INVALID_HANDLE) */
    CHECK(UN(NtGdiGetBitmapBits)((HBITMAP)br, sizeof got, got) == 0);
    EXPECT_ERR(BD_ERR_INVALID_HANDLE);
    SET_ERR(0);
    CHECK(UN(NtGdiGetBitmapBits)(NULL, sizeof got, got) == 0);
    EXPECT_ERR(BD_ERR_INVALID_HANDLE);
    if (dc) { SET_ERR(0); CHECK(UN(NtGdiGetBitmapBits)((HBITMAP)dc, sizeof got, got) == 0); EXPECT_ERR(BD_ERR_INVALID_HANDLE); U(DeleteDC)(dc); }
    HBITMAP hb = U(CreateBitmap)(2, 2, 1, 1, NULL);
    if (hb) { U(DeleteObject)(hb); SET_ERR(0); CHECK(UN(NtGdiGetBitmapBits)(hb, sizeof got, got) == 0); EXPECT_ERR(BD_ERR_INVALID_HANDLE); }
    CHECK(got[0] == 0xee);
    U(DeleteObject)(br);
}
TEST(bd_bits_set_invalid_handles) {
    BD_NEED(); BD_NEED_NT(NtGdiSetBitmapBits); REQUIRE(U(CreateSolidBrush));
    BYTE src[16] = { 1 };
    HBRUSH br = U(CreateSolidBrush)(RGB(1, 2, 3)); REQUIRE(br);
    SET_ERR(0);
    CHECK(UN(NtGdiSetBitmapBits)((HBITMAP)br, sizeof src, src) == 0);
    EXPECT_ERR(BD_ERR_INVALID_HANDLE);
    SET_ERR(0);
    CHECK(UN(NtGdiSetBitmapBits)(NULL, sizeof src, src) == 0);
    EXPECT_ERR(BD_ERR_INVALID_HANDLE);
    U(DeleteObject)(br);
}
TEST(bd_bits_set_cb_zero_returns_zero) {
    BD_NEED(); BD_NEED_NT(NtGdiSetBitmapBits);
    HBITMAP hb = U(CreateBitmap)(8, 2, 1, 16, NULL); REQUIRE(hb);
    BYTE src[8] = { 1 };
    SET_ERR(0);
    CHECK(UN(NtGdiSetBitmapBits)(hb, 0, src) == 0); /* cb == 0 returns before any lookup */
    U(DeleteObject)(hb);
}
TEST(bd_bits_set_partial_leaves_rest) {
    BD_NEED(); BD_NEED_NT(NtGdiSetBitmapBits); BD_NEED_NT(NtGdiGetBitmapBits);
    BYTE init[16 * 4], upd[20], got[16 * 4];
    memset(init, 0xaa, sizeof init); for (int i = 0; i < 20; i++) upd[i] = (BYTE)(0x10 + i);
    HBITMAP hb = U(CreateBitmap)(16, 4, 1, 8, init); REQUIRE(hb);
    LONG r = UN(NtGdiSetBitmapBits)(hb, sizeof upd, upd);
    CHECKF(r == (LONG)sizeof upd, "set returned %ld", r);
    memset(got, 0, sizeof got);
    CHECK(UN(NtGdiGetBitmapBits)(hb, sizeof got, got) == (LONG)sizeof got);
    CHECK(!memcmp(got, upd, sizeof upd));
    int rest = 1; for (size_t i = sizeof upd; i < sizeof got; i++) if (got[i] != 0xaa) rest = 0;
    CHECK(rest);
    U(DeleteObject)(hb);
}
TEST(bd_bits_set_full_roundtrip_ddb) {
    BD_NEED(); BD_NEED_NT(NtGdiSetBitmapBits); BD_NEED_NT(NtGdiGetBitmapBits);
    BYTE src[16 * 8 * 4], got[16 * 8 * 4];
    for (size_t i = 0; i < sizeof src; i++) src[i] = (BYTE)(i * 3 + 7);
    HBITMAP hb = U(CreateBitmap)(16, 8, 1, 32, NULL); REQUIRE(hb);
    CHECK(UN(NtGdiSetBitmapBits)(hb, sizeof src, src) == (LONG)sizeof src);
    memset(got, 0, sizeof got);
    CHECK(UN(NtGdiGetBitmapBits)(hb, sizeof got, got) == (LONG)sizeof got);
    CHECK(!memcmp(src, got, sizeof src));
    U(DeleteObject)(hb);
}
TEST(bd_bits_set_cb_over_size_note) {
    BD_NEED(); BD_NEED_NT(NtGdiSetBitmapBits);
    HBITMAP hb = U(CreateBitmap)(8, 2, 1, 16, NULL); REQUIRE(hb);
    BYTE src[256]; memset(src, 0x42, sizeof src);
    LONG r = UN(NtGdiSetBitmapBits)(hb, sizeof src, src);
    t_note("SetBitmapBits of 256 bytes into a 32 byte bitmap returned %ld", r);
    CHECK(r <= 32);
    U(DeleteObject)(hb);
}
#define BB_DIBO(X) X(b8, 8) X(b24, 24) X(b32, 32)
#define X(nm, bpp) \
TEST(bd_bits_dib_bottom_up_getbitmapbits_is_top_down_##nm) { \
    BD_NEED(); BD_NEED_NT(NtGdiGetBitmapBits); \
    enum { W = 8, H = 4 }; \
    void *bits = NULL; HBITMAP hb = bd_dib(W, H, bpp, &bits); REQUIRE(hb && bits); \
    unsigned st = bd_stride(W, bpp), rb = W * (bpp) / 8; \
    BYTE *m = bits; for (int r = 0; r < H; r++) for (unsigned i = 0; i < st; i++) m[r * st + i] = (BYTE)(r * 0x20 + i + 1); \
    BYTE got[512]; unsigned gst = bd_ddb_stride(W, bpp); \
    memset(got, 0, sizeof got); \
    LONG n = UN(NtGdiGetBitmapBits)(hb, sizeof got, got); \
    CHECKF(n == (LONG)(gst * H), "n %ld", n); \
    int bad = 0; \
    /* surface row 0 is the top picture row = the LAST memory row of a bottom-up DIB */ \
    for (int y = 0; y < H; y++) if (memcmp(got + y * gst, m + (H - 1 - y) * st, rb)) bad++; \
    CHECKF(bad == 0, "%d rows differ", bad); \
    U(DeleteObject)(hb); \
} \
TEST(bd_bits_dib_top_down_getbitmapbits_is_memory_order_##nm) { \
    BD_NEED(); BD_NEED_NT(NtGdiGetBitmapBits); \
    enum { W = 8, H = 4 }; \
    void *bits = NULL; HBITMAP hb = bd_dib(W, -H, bpp, &bits); REQUIRE(hb && bits); \
    unsigned st = bd_stride(W, bpp), rb = W * (bpp) / 8; \
    BYTE *m = bits; for (int r = 0; r < H; r++) for (unsigned i = 0; i < st; i++) m[r * st + i] = (BYTE)(r * 0x20 + i + 1); \
    BYTE got[512]; unsigned gst = bd_ddb_stride(W, bpp); \
    memset(got, 0, sizeof got); \
    CHECK(UN(NtGdiGetBitmapBits)(hb, sizeof got, got) == (LONG)(gst * H)); \
    int bad = 0; for (int y = 0; y < H; y++) if (memcmp(got + y * gst, m + y * st, rb)) bad++; \
    CHECKF(bad == 0, "%d rows differ", bad); \
    U(DeleteObject)(hb); \
} \
TEST(bd_bits_dib_setbitmapbits_writes_memory_##nm) { \
    BD_NEED(); BD_NEED_NT(NtGdiSetBitmapBits); \
    enum { W = 8, H = 4 }; \
    void *bits = NULL; HBITMAP hb = bd_dib(W, H, bpp, &bits); REQUIRE(hb && bits); \
    unsigned st = bd_stride(W, bpp), rb = W * (bpp) / 8, gst = bd_ddb_stride(W, bpp); \
    BYTE src[512]; for (unsigned i = 0; i < sizeof src; i++) src[i] = (BYTE)(i * 11 + 5); \
    LONG n = UN(NtGdiSetBitmapBits)(hb, gst * H, src); \
    CHECKF(n == (LONG)(gst * H), "n %ld", n); \
    bd_flush(); \
    BYTE *m = bits; int bad = 0; \
    for (int y = 0; y < H; y++) if (memcmp(src + y * gst, m + (H - 1 - y) * st, rb)) bad++; \
    CHECKF(bad == 0, "%d rows differ", bad); \
    U(DeleteObject)(hb); \
}
BB_DIBO(X)
#undef X

TEST(bd_bits_stock_bitmap_size) {
    BD_NEED(); BD_NEED_NT(NtGdiGetBitmapBits); REQUIRE(U(GetStockObject));
    HBITMAP s = U(GetStockObject)(0x15);
    if (!s) { t_skip("no stock bitmap"); return; }
    CHECK(UN(NtGdiGetBitmapBits)(s, 100, NULL) == 2); /* 1x1 mono: one WORD */
}
TEST(bd_bits_selected_ddb_shows_drawn_pixel) {
    BD_NEED(); BD_NEED_NT(NtGdiGetBitmapBits); REQUIRE(U(SetPixel)); REQUIRE(U(SelectObject));
    HDC dc = bd_mem_dc(); HBITMAP hb = U(CreateBitmap)(4, 4, 1, 32, NULL);
    if (!dc || !hb) { if (dc) U(DeleteDC)(dc); if (hb) U(DeleteObject)(hb); t_skip("setup"); return; }
    BYTE z[64] = { 0 }; UN(NtGdiSetBitmapBits)(hb, sizeof z, z);
    HGDIOBJ old = U(SelectObject)(dc, hb);
    U(SetPixel)(dc, 1, 2, RGB(0x12, 0x34, 0x56));
    bd_flush();
    BYTE got[64]; memset(got, 0, sizeof got);
    CHECK(UN(NtGdiGetBitmapBits)(hb, sizeof got, got) == 64);
    const BYTE *p = got + 2 * 16 + 1 * 4; /* row 2, column 1 of a 4 byte/pixel top-down surface */
    CHECKF(p[0] == 0x56 && p[1] == 0x34 && p[2] == 0x12, "pixel bytes %02x %02x %02x %02x", p[0], p[1], p[2], p[3]);
    U(SelectObject)(dc, old); U(DeleteDC)(dc); U(DeleteObject)(hb);
}

/* ---- SetDIBits / GetDIBits on a 32bpp section ---------------------------------------------------------------------------- */
enum { W = 8, H = 6 };

static HBITMAP mk32(int h, DWORD **bits) { void *p = NULL; HBITMAP b = bd_dib(W, h, 32, &p); *bits = p; return b; }
static void bmi32(struct bd_bmi *b, int h) { bd_bmi_init(b, W, h, 32); }

TEST(bd_setdibits_bottom_up_identity) {
    BD_NEED(); REQUIRE(U(SetDIBits));
    DWORD *m; HBITMAP hb = mk32(H, &m); HDC dc = bd_mem_dc();
    if (!hb || !dc) { if (hb) U(DeleteObject)(hb); if (dc) U(DeleteDC)(dc); t_skip("setup"); return; }
    DWORD src[W * H]; fill32(src, W, H, 1, 1);
    struct bd_bmi bi; bmi32(&bi, H);
    int r = U(SetDIBits)(dc, hb, 0, H, src, (BITMAPINFO *)&bi, DIB_RGB_COLORS);
    bd_flush();
    CHECKF(r == H, "SetDIBits returned %d", r);
    CHECKF(diff32(m, src, W * H) == 0, "%d pixels differ", diff32(m, src, W * H));
    U(DeleteDC)(dc); U(DeleteObject)(hb);
}
TEST(bd_setdibits_top_down_source_flips) {
    BD_NEED(); REQUIRE(U(SetDIBits));
    DWORD *m; HBITMAP hb = mk32(H, &m); HDC dc = bd_mem_dc();
    if (!hb || !dc) { if (hb) U(DeleteObject)(hb); if (dc) U(DeleteDC)(dc); t_skip("setup"); return; }
    DWORD src[W * H], want[W * H];
    fill32(src, W, H, 0, 2);   /* top-down source */
    fill32(want, W, H, 1, 2);  /* same picture stored bottom-up */
    struct bd_bmi bi; bmi32(&bi, -H);
    int r = U(SetDIBits)(dc, hb, 0, H, src, (BITMAPINFO *)&bi, DIB_RGB_COLORS);
    bd_flush();
    CHECKF(r == H, "returned %d", r);
    CHECKF(diff32(m, want, W * H) == 0, "%d pixels differ", diff32(m, want, W * H));
    U(DeleteDC)(dc); U(DeleteObject)(hb);
}
TEST(bd_setdibits_into_top_down_dest_flips) {
    BD_NEED(); REQUIRE(U(SetDIBits));
    void *p = NULL; HBITMAP hb = bd_dib(W, -H, 32, &p); HDC dc = bd_mem_dc();
    if (!hb || !dc) { if (hb) U(DeleteObject)(hb); if (dc) U(DeleteDC)(dc); t_skip("setup"); return; }
    DWORD src[W * H], want[W * H];
    fill32(src, W, H, 1, 3);  /* bottom-up source */
    fill32(want, W, H, 0, 3); /* same picture, top-down memory */
    struct bd_bmi bi; bmi32(&bi, H);
    int r = U(SetDIBits)(dc, hb, 0, H, src, (BITMAPINFO *)&bi, DIB_RGB_COLORS);
    bd_flush();
    CHECK(r == H);
    CHECKF(diff32(p, want, W * H) == 0, "%d pixels differ", diff32(p, want, W * H));
    U(DeleteDC)(dc); U(DeleteObject)(hb);
}
TEST(bd_setdibits_partial_scanlines) {
    BD_NEED(); REQUIRE(U(SetDIBits));
    DWORD *m; HBITMAP hb = mk32(H, &m); HDC dc = bd_mem_dc();
    if (!hb || !dc) { if (hb) U(DeleteObject)(hb); if (dc) U(DeleteDC)(dc); t_skip("setup"); return; }
    for (int i = 0; i < W * H; i++) m[i] = 0x005a5a5a;
    DWORD src[W * 3]; for (int i = 0; i < W * 3; i++) src[i] = 0x00010203 + i;
    struct bd_bmi bi; bmi32(&bi, H);
    /* scan lines 2..4 counted from the bottom are memory rows 2..4 of the bottom-up destination */
    int r = U(SetDIBits)(dc, hb, 2, 3, src, (BITMAPINFO *)&bi, DIB_RGB_COLORS);
    bd_flush();
    CHECKF(r == 3, "returned %d", r);
    int bad = 0;
    for (int row = 0; row < H; row++) for (int x = 0; x < W; x++) {
        DWORD want = (row >= 2 && row <= 4) ? 0x00010203 + (row - 2) * W + x : 0x005a5a5a;
        if (!same_rgb(m[row * W + x], want)) bad++;
    }
    CHECKF(bad == 0, "%d pixels differ", bad);
    U(DeleteDC)(dc); U(DeleteObject)(hb);
}
TEST(bd_setdibits_null_bits_returns_zero) {
    BD_NEED(); REQUIRE(U(SetDIBits));
    DWORD *m; HBITMAP hb = mk32(H, &m); HDC dc = bd_mem_dc();
    if (!hb || !dc) { if (hb) U(DeleteObject)(hb); if (dc) U(DeleteDC)(dc); t_skip("setup"); return; }
    struct bd_bmi bi; bmi32(&bi, H);
    CHECK(U(SetDIBits)(dc, hb, 0, H, NULL, (BITMAPINFO *)&bi, DIB_RGB_COLORS) == 0); /* gdi32full!SetDIBits: lpBits == NULL */
    U(DeleteDC)(dc); U(DeleteObject)(hb);
}
TEST(bd_setdibits_non_bitmap_handle_returns_zero) {
    BD_NEED(); REQUIRE(U(SetDIBits)); REQUIRE(U(CreateSolidBrush));
    HDC dc = bd_mem_dc(); HBRUSH br = U(CreateSolidBrush)(RGB(1, 2, 3));
    if (!dc || !br) { if (dc) U(DeleteDC)(dc); if (br) U(DeleteObject)(br); t_skip("setup"); return; }
    DWORD src[W * H]; fill32(src, W, H, 1, 4);
    struct bd_bmi bi; bmi32(&bi, H);
    /* gdi32full!SetDIBits: ((hbm >> 16) & 0x1f) != 5 -> 0 */
    CHECK(U(SetDIBits)(dc, (HBITMAP)br, 0, H, src, (BITMAPINFO *)&bi, DIB_RGB_COLORS) == 0);
    CHECK(U(SetDIBits)(dc, NULL, 0, H, src, (BITMAPINFO *)&bi, DIB_RGB_COLORS) == 0);
    U(DeleteDC)(dc); U(DeleteObject)(br);
}
TEST(bd_setdibits_jpeg_png_rejected) {
    BD_NEED(); REQUIRE(U(SetDIBits));
    DWORD *m; HBITMAP hb = mk32(H, &m); HDC dc = bd_mem_dc();
    if (!hb || !dc) { if (hb) U(DeleteObject)(hb); if (dc) U(DeleteDC)(dc); t_skip("setup"); return; }
    DWORD src[W * H]; fill32(src, W, H, 1, 4);
    for (DWORD comp = BI_JPEG; comp <= BI_PNG; comp++) {
        struct bd_bmi bi; bmi32(&bi, H); bi.h.biCompression = comp; bi.h.biSizeImage = sizeof src;
        SET_ERR(0);
        CHECK(U(SetDIBits)(dc, hb, 0, H, src, (BITMAPINFO *)&bi, DIB_RGB_COLORS) == 0); /* gdi32full!SetDIBits: biCompression 4/5 -> 87 */
        EXPECT_ERR(BD_ERR_INVALID_PARAM);
    }
    int untouched = 1; for (int i = 0; i < W * H; i++) if (m[i]) untouched = 0;
    CHECK(untouched);
    U(DeleteDC)(dc); U(DeleteObject)(hb);
}
TEST(bd_setdibits_bitmap_selected_elsewhere_still_updates) {
    BD_NEED(); REQUIRE(U(SetDIBits)); REQUIRE(U(SelectObject));
    DWORD *m; HBITMAP hb = mk32(H, &m); HDC sel = bd_mem_dc(), dc = bd_mem_dc();
    if (!hb || !sel || !dc) { if (hb) U(DeleteObject)(hb); if (sel) U(DeleteDC)(sel); if (dc) U(DeleteDC)(dc); t_skip("setup"); return; }
    HGDIOBJ old = U(SelectObject)(sel, hb);
    DWORD src[W * H]; fill32(src, W, H, 1, 5);
    struct bd_bmi bi; bmi32(&bi, H);
    /* gdi32full!SetDIBits asks NtGdiGetDCforBitmap for the owning DC and reuses it */
    int r = U(SetDIBits)(dc, hb, 0, H, src, (BITMAPINFO *)&bi, DIB_RGB_COLORS);
    bd_flush();
    CHECK(r == H);
    CHECK(diff32(m, src, W * H) == 0);
    CHECK(U(GetCurrentObject)(sel, OBJ_BITMAP) == hb);
    U(SelectObject)(sel, old); U(DeleteDC)(sel); U(DeleteDC)(dc); U(DeleteObject)(hb);
}
TEST(bd_setdibits_24bpp_source_into_32bpp) {
    BD_NEED(); REQUIRE(U(SetDIBits));
    DWORD *m; HBITMAP hb = mk32(4, &m); HDC dc = bd_mem_dc();
    if (!hb || !dc) { if (hb) U(DeleteObject)(hb); if (dc) U(DeleteDC)(dc); t_skip("setup"); return; }
    enum { SW = 3, SH = 4 };
    unsigned st = bd_stride(SW, 24); BYTE src[12 * SH];
    memset(src, 0xcd, sizeof src);
    for (int r = 0; r < SH; r++) for (int x = 0; x < SW; x++) { BYTE *p = src + r * st + x * 3; p[0] = (BYTE)(r * 16 + x); p[1] = (BYTE)(0x40 + r); p[2] = (BYTE)(0x80 + x); }
    struct bd_bmi bi; bd_bmi_init(&bi, SW, SH, 24);
    for (int i = 0; i < 8 * 4; i++) m[i] = 0;
    int r = U(SetDIBits)(dc, hb, 0, SH, src, (BITMAPINFO *)&bi, DIB_RGB_COLORS);
    bd_flush();
    CHECK(r == SH);
    int bad = 0;
    for (int row = 0; row < SH; row++) for (int x = 0; x < SW; x++) {
        const BYTE *p = src + row * st + x * 3;
        DWORD want = (DWORD)p[0] | ((DWORD)p[1] << 8) | ((DWORD)p[2] << 16);
        if (!same_rgb(m[row * W + x], want)) bad++;
    }
    CHECKF(bad == 0, "%d pixels differ", bad);
    for (int row = 0; row < SH; row++) for (int x = SW; x < W; x++) if (m[row * W + x] & 0xffffff) bad++; /* outside the 3 columns: untouched */
    CHECK(bad == 0);
    U(DeleteDC)(dc); U(DeleteObject)(hb);
}
TEST(bd_getdibits_32bpp_identity) {
    BD_NEED(); REQUIRE(U(GetDIBits));
    DWORD *m; HBITMAP hb = mk32(H, &m); HDC dc = bd_mem_dc();
    if (!hb || !dc) { if (hb) U(DeleteObject)(hb); if (dc) U(DeleteDC)(dc); t_skip("setup"); return; }
    fill32(m, W, H, 1, 6);
    DWORD out[W * H]; memset(out, 0, sizeof out);
    struct bd_bmi bi; bmi32(&bi, H);
    int r = U(GetDIBits)(dc, hb, 0, H, out, (BITMAPINFO *)&bi, DIB_RGB_COLORS);
    CHECKF(r == H, "returned %d", r);
    CHECKF(diff32(out, m, W * H) == 0, "%d pixels differ", diff32(out, m, W * H));
    U(DeleteDC)(dc); U(DeleteObject)(hb);
}
TEST(bd_getdibits_top_down_request_flips) {
    BD_NEED(); REQUIRE(U(GetDIBits));
    DWORD *m; HBITMAP hb = mk32(H, &m); HDC dc = bd_mem_dc();
    if (!hb || !dc) { if (hb) U(DeleteObject)(hb); if (dc) U(DeleteDC)(dc); t_skip("setup"); return; }
    fill32(m, W, H, 1, 7);
    DWORD out[W * H], want[W * H]; memset(out, 0, sizeof out);
    fill32(want, W, H, 0, 7);
    struct bd_bmi bi; bmi32(&bi, -H);
    int r = U(GetDIBits)(dc, hb, 0, H, out, (BITMAPINFO *)&bi, DIB_RGB_COLORS);
    CHECK(r == H);
    CHECKF(diff32(out, want, W * H) == 0, "%d pixels differ", diff32(out, want, W * H));
    U(DeleteDC)(dc); U(DeleteObject)(hb);
}
TEST(bd_getdibits_partial_scanlines) {
    BD_NEED(); REQUIRE(U(GetDIBits));
    DWORD *m; HBITMAP hb = mk32(H, &m); HDC dc = bd_mem_dc();
    if (!hb || !dc) { if (hb) U(DeleteObject)(hb); if (dc) U(DeleteDC)(dc); t_skip("setup"); return; }
    fill32(m, W, H, 1, 8);
    DWORD out[W * H]; memset(out, 0x77, sizeof out);
    struct bd_bmi bi; bmi32(&bi, H);
    int r = U(GetDIBits)(dc, hb, 1, 2, out, (BITMAPINFO *)&bi, DIB_RGB_COLORS);
    CHECKF(r == 2, "returned %d", r);
    CHECK(diff32(out, m + W * 1, W * 2) == 0); /* scan lines 1 and 2 from the bottom */
    CHECK(out[W * 2] == 0x77777777); /* nothing written past cLines */
    U(DeleteDC)(dc); U(DeleteObject)(hb);
}
TEST(bd_getdibits_to_24bpp_drops_alpha) {
    BD_NEED(); REQUIRE(U(GetDIBits));
    DWORD *m; HBITMAP hb = mk32(H, &m); HDC dc = bd_mem_dc();
    if (!hb || !dc) { if (hb) U(DeleteObject)(hb); if (dc) U(DeleteDC)(dc); t_skip("setup"); return; }
    fill32(m, W, H, 1, 9);
    unsigned st = bd_stride(W, 24); BYTE out[24 * H]; memset(out, 0xee, sizeof out);
    struct bd_bmi bi; bd_bmi_init(&bi, W, H, 24);
    int r = U(GetDIBits)(dc, hb, 0, H, out, (BITMAPINFO *)&bi, DIB_RGB_COLORS);
    CHECK(r == H);
    int bad = 0;
    for (int row = 0; row < H; row++) for (int x = 0; x < W; x++) {
        const BYTE *p = out + row * st + x * 3;
        DWORD got = p[0] | (p[1] << 8) | (p[2] << 16);
        if (!same_rgb(got, m[row * W + x])) bad++;
    }
    CHECKF(bad == 0, "%d pixels differ", bad);
    U(DeleteDC)(dc); U(DeleteObject)(hb);
}
TEST(bd_getdibits_cb_above_height_clamped) {
    BD_NEED(); REQUIRE(U(GetDIBits));
    DWORD *m; HBITMAP hb = mk32(H, &m); HDC dc = bd_mem_dc();
    if (!hb || !dc) { if (hb) U(DeleteObject)(hb); if (dc) U(DeleteDC)(dc); t_skip("setup"); return; }
    fill32(m, W, H, 1, 10);
    DWORD out[W * (H + 4)]; memset(out, 0, sizeof out);
    struct bd_bmi bi; bmi32(&bi, H);
    int r = U(GetDIBits)(dc, hb, 0, H + 4, out, (BITMAPINFO *)&bi, DIB_RGB_COLORS);
    t_note("GetDIBits(cLines = %d on a %d line bitmap) returned %d", H + 4, H, r);
    CHECK(r <= H + 4);
    int past = 0; for (int i = W * H; i < W * (H + 4); i++) past |= out[i] != 0;
    CHECK(!past);
    U(DeleteDC)(dc); U(DeleteObject)(hb);
}
TEST(bd_getdibits_start_beyond_height_note) {
    BD_NEED(); REQUIRE(U(GetDIBits));
    DWORD *m; HBITMAP hb = mk32(H, &m); HDC dc = bd_mem_dc();
    if (!hb || !dc) { if (hb) U(DeleteObject)(hb); if (dc) U(DeleteDC)(dc); t_skip("setup"); return; }
    DWORD out[W * H]; memset(out, 0x11, sizeof out);
    struct bd_bmi bi; bmi32(&bi, H);
    int r = U(GetDIBits)(dc, hb, H + 2, 2, out, (BITMAPINFO *)&bi, DIB_RGB_COLORS);
    t_note("GetDIBits(start %d beyond %d lines) -> %d", H + 2, H, r);
    if (r == 0) CHECK(out[0] == 0x11111111); /* nothing copied, nothing written */
    U(DeleteDC)(dc); U(DeleteObject)(hb);
}
TEST(bd_getdibits_8bpp_returns_color_table) {
    BD_NEED(); REQUIRE(U(GetDIBits));
    void *p = NULL; HBITMAP hb = bd_dib(4, 4, 8, &p); HDC dc = bd_mem_dc();
    if (!hb || !dc) { if (hb) U(DeleteObject)(hb); if (dc) U(DeleteDC)(dc); t_skip("setup"); return; }
    BYTE out[16]; struct bd_bmi bi; bd_bmi_init(&bi, 4, 4, 8);
    memset(bi.c, 0x99, sizeof bi.c);
    int r = U(GetDIBits)(dc, hb, 0, 4, out, (BITMAPINFO *)&bi, DIB_RGB_COLORS);
    CHECK(r == 4);
    int bad = 0; for (int i = 0; i < 256; i++) { BYTE v = (BYTE)i; if (bi.c[i].rgbRed != v || bi.c[i].rgbGreen != v || bi.c[i].rgbBlue != v) bad++; }
    CHECKF(bad == 0, "%d colour table entries differ from the creation ramp", bad);
    U(DeleteDC)(dc); U(DeleteObject)(hb);
}
TEST(bd_getdibits_8bpp_indices_roundtrip_through_setdibits) {
    BD_NEED(); REQUIRE(U(GetDIBits)); REQUIRE(U(SetDIBits));
    void *p = NULL; HBITMAP hb = bd_dib(4, 4, 8, &p); HDC dc = bd_mem_dc();
    if (!hb || !dc) { if (hb) U(DeleteObject)(hb); if (dc) U(DeleteDC)(dc); t_skip("setup"); return; }
    BYTE src[16], out[16]; for (int i = 0; i < 16; i++) src[i] = (BYTE)(i * 17);
    struct bd_bmi bi; bd_bmi_init(&bi, 4, 4, 8); bd_bmi_gray(&bi, 256);
    CHECK(U(SetDIBits)(dc, hb, 0, 4, src, (BITMAPINFO *)&bi, DIB_RGB_COLORS) == 4);
    bd_flush();
    CHECK(!memcmp(p, src, 16)); /* identical colour tables: indices copy straight through */
    memset(out, 0, sizeof out);
    CHECK(U(GetDIBits)(dc, hb, 0, 4, out, (BITMAPINFO *)&bi, DIB_RGB_COLORS) == 4);
    CHECK(!memcmp(out, src, 16));
    U(DeleteDC)(dc); U(DeleteObject)(hb);
}

/* ---- NtGdiSetDIBitsToDeviceInternal --------------------------------------------------------------------------------------- */
static int sdbd(HDC dc, int xd, int yd, int w, int h, int xs, int ys, DWORD start, DWORD lines, const void *bits, const struct bd_bmi *bi, UINT usage, UINT cjBits, UINT cjInfo) {
    return UN(NtGdiSetDIBitsToDeviceInternal)(dc, xd, yd, w, h, xs, ys, start, lines, bits, (const BITMAPINFO *)bi, usage, cjBits, cjInfo, TRUE, NULL);
}
static int sdbd_setup(HDC *dc, HBITMAP *hb, DWORD **m, HGDIOBJ *old) {
    *dc = bd_mem_dc(); *hb = mk32(H, m);
    if (!*dc || !*hb) { if (*dc) U(DeleteDC)(*dc); if (*hb) U(DeleteObject)(*hb); return 0; }
    *old = U(SelectObject)(*dc, *hb);
    for (int i = 0; i < W * H; i++) (*m)[i] = 0x005a5a5a;
    return 1;
}
static void sdbd_done(HDC dc, HBITMAP hb, HGDIOBJ old) { U(SelectObject)(dc, old); U(DeleteDC)(dc); U(DeleteObject)(hb); }

TEST(bd_sdbd_sys_full_copy) {
    BD_NEED(); BD_NEED_NT(NtGdiSetDIBitsToDeviceInternal);
    HDC dc; HBITMAP hb; DWORD *m; HGDIOBJ old;
    if (!sdbd_setup(&dc, &hb, &m, &old)) { t_skip("setup"); return; }
    DWORD src[W * H]; fill32(src, W, H, 1, 11);
    struct bd_bmi bi; bmi32(&bi, H);
    int r = sdbd(dc, 0, 0, W, H, 0, 0, 0, H, src, &bi, DIB_RGB_COLORS, sizeof src, bd_hdr_size((BITMAPINFO *)&bi, 0));
    bd_flush();
    CHECKF(r == H, "returned %d", r);
    CHECKF(diff32(m, src, W * H) == 0, "%d pixels differ", diff32(m, src, W * H));
    sdbd_done(dc, hb, old);
}
TEST(bd_sdbd_sys_destination_offset) {
    BD_NEED(); BD_NEED_NT(NtGdiSetDIBitsToDeviceInternal);
    HDC dc; HBITMAP hb; DWORD *m; HGDIOBJ old;
    if (!sdbd_setup(&dc, &hb, &m, &old)) { t_skip("setup"); return; }
    enum { SW = 3, SH = 2 };
    DWORD src[SW * SH]; fill32(src, SW, SH, 1, 12);
    struct bd_bmi bi; bd_bmi_init(&bi, SW, SH, 32);
    int r = sdbd(dc, 2, 1, SW, SH, 0, 0, 0, SH, src, &bi, DIB_RGB_COLORS, sizeof src, 0x28);
    bd_flush();
    CHECKF(r == SH, "returned %d", r);
    int bad = 0;
    for (int py = 0; py < H; py++) for (int x = 0; x < W; x++) {
        int row = H - 1 - py; /* memory row of picture row py in the bottom-up destination */
        DWORD want = 0x005a5a5a;
        if (x >= 2 && x < 2 + SW && py >= 1 && py < 1 + SH) want = px32(x - 2, py - 1, 12);
        if (!same_rgb(m[row * W + x], want)) bad++;
    }
    CHECKF(bad == 0, "%d pixels differ", bad);
    sdbd_done(dc, hb, old);
}
TEST(bd_sdbd_sys_cjinfo_mismatch_returns_zero) {
    BD_NEED(); BD_NEED_NT(NtGdiSetDIBitsToDeviceInternal);
    HDC dc; HBITMAP hb; DWORD *m; HGDIOBJ old;
    if (!sdbd_setup(&dc, &hb, &m, &old)) { t_skip("setup"); return; }
    DWORD src[W * H]; fill32(src, W, H, 1, 13);
    struct bd_bmi bi; bmi32(&bi, H);
    static const UINT bad[] = { 0, 4, 0x24, 0x2c, 0x30, 0x400 };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        /* the captured info size must equal the bitmap info size (0x28 here) */
        int r = sdbd(dc, 0, 0, W, H, 0, 0, 0, H, src, &bi, DIB_RGB_COLORS, sizeof src, bad[i]);
        CHECKF(r == 0, "cjInfo 0x%x returned %d", bad[i], r);
    }
    int touched = 0; for (int i = 0; i < W * H; i++) touched |= m[i] != 0x005a5a5a;
    CHECK(!touched);
    sdbd_done(dc, hb, old);
}
TEST(bd_sdbd_sys_null_bits_fails) {
    BD_NEED(); BD_NEED_NT(NtGdiSetDIBitsToDeviceInternal);
    HDC dc; HBITMAP hb; DWORD *m; HGDIOBJ old;
    if (!sdbd_setup(&dc, &hb, &m, &old)) { t_skip("setup"); return; }
    struct bd_bmi bi; bmi32(&bi, H);
    SET_ERR(0);
    int r = sdbd(dc, 0, 0, W, H, 0, 0, 0, H, NULL, &bi, DIB_RGB_COLORS, 0, 0x28);
    CHECK(r == 0);
    EXPECT_ERR(BD_ERR_INVALID_PARAM); /* pBits == NULL falls to the common ERROR_INVALID_PARAMETER exit */
    sdbd_done(dc, hb, old);
}
TEST(bd_sdbd_sys_invalid_dc_fails) {
    BD_NEED(); BD_NEED_NT(NtGdiSetDIBitsToDeviceInternal);
    DWORD src[W * H]; fill32(src, W, H, 1, 14);
    struct bd_bmi bi; bmi32(&bi, H);
    CHECK(sdbd(NULL, 0, 0, W, H, 0, 0, 0, H, src, &bi, DIB_RGB_COLORS, sizeof src, 0x28) == 0);
    HDC dc = bd_mem_dc();
    if (dc) { U(DeleteDC)(dc); CHECK(sdbd(dc, 0, 0, W, H, 0, 0, 0, H, src, &bi, DIB_RGB_COLORS, sizeof src, 0x28) == 0); }
}
TEST(bd_sdbd_sys_usage_masked_to_two_bits) {
    BD_NEED(); BD_NEED_NT(NtGdiSetDIBitsToDeviceInternal);
    HDC dc; HBITMAP hb; DWORD *m; HGDIOBJ old;
    if (!sdbd_setup(&dc, &hb, &m, &old)) { t_skip("setup"); return; }
    DWORD src[W * H]; fill32(src, W, H, 1, 15);
    struct bd_bmi bi; bmi32(&bi, H);
    /* NtGdiSetDIBitsToDeviceInternal passes usage & 3 on: 4 and 0x100 behave as DIB_RGB_COLORS, 3 is rejected inside Gre (usage < 3 required) */
    int r4 = sdbd(dc, 0, 0, W, H, 0, 0, 0, H, src, &bi, 4, sizeof src, 0x28);
    CHECKF(r4 == H, "usage 4 returned %d", r4);
    bd_flush();
    CHECK(diff32(m, src, W * H) == 0);
    for (int i = 0; i < W * H; i++) m[i] = 0x005a5a5a;
    int r3 = sdbd(dc, 0, 0, W, H, 0, 0, 0, H, src, &bi, 3, sizeof src, 0x28);
    CHECKF(r3 == 0, "usage 3 returned %d", r3);
    sdbd_done(dc, hb, old);
}
TEST(bd_sdbd_sys_bad_geometry_fails) {
    BD_NEED(); BD_NEED_NT(NtGdiSetDIBitsToDeviceInternal);
    HDC dc; HBITMAP hb; DWORD *m; HGDIOBJ old;
    if (!sdbd_setup(&dc, &hb, &m, &old)) { t_skip("setup"); return; }
    DWORD src[W * H]; fill32(src, W, H, 1, 16);
    struct bd_bmi bi;
    SET_ERR(0);
    bd_bmi_init(&bi, 0, H, 32);   /* biWidth == 0: the call needs biWidth > 0 */
    CHECK(sdbd(dc, 0, 0, W, H, 0, 0, 0, H, src, &bi, DIB_RGB_COLORS, sizeof src, 0x28) == 0);
    bd_bmi_init(&bi, W, 0, 32);   /* biHeight == 0 */
    CHECK(sdbd(dc, 0, 0, W, H, 0, 0, 0, H, src, &bi, DIB_RGB_COLORS, sizeof src, 0x28) == 0);
    bd_bmi_init(&bi, -W, H, 32);  /* negative width */
    CHECK(sdbd(dc, 0, 0, W, H, 0, 0, 0, H, src, &bi, DIB_RGB_COLORS, sizeof src, 0x28) == 0);
    int touched = 0; for (int i = 0; i < W * H; i++) touched |= m[i] != 0x005a5a5a;
    CHECK(!touched);
    sdbd_done(dc, hb, old);
}
TEST(bd_sdbd_sys_rle_top_down_rejected) {
    BD_NEED(); BD_NEED_NT(NtGdiSetDIBitsToDeviceInternal);
    HDC dc; HBITMAP hb; DWORD *m; HGDIOBJ old;
    if (!sdbd_setup(&dc, &hb, &m, &old)) { t_skip("setup"); return; }
    BYTE rle[8] = { 0, 0, 0, 1, 0, 0, 0, 1 };
    struct bd_bmi bi; bd_bmi_init(&bi, 4, -4, 8); bi.h.biCompression = BI_RLE8; bi.h.biSizeImage = sizeof rle; bd_bmi_gray(&bi, 256);
    SET_ERR(0);
    /* a negative biHeight is only legal for the compressions in the 0x1c39 mask (RGB, BITFIELDS, JPEG, PNG, CMYK); RLE -> ERROR_INVALID_PARAMETER */
    int r = sdbd(dc, 0, 0, 4, 4, 0, 0, 0, 4, rle, &bi, DIB_RGB_COLORS, sizeof rle, bd_hdr_size((BITMAPINFO *)&bi, 0));
    CHECK(r == 0);
    EXPECT_ERR(BD_ERR_INVALID_PARAM);
    sdbd_done(dc, hb, old);
}
TEST(bd_sdbd_sys_jpeg_png_on_memory_dc) {
    BD_NEED(); BD_NEED_NT(NtGdiSetDIBitsToDeviceInternal);
    HDC dc; HBITMAP hb; DWORD *m; HGDIOBJ old;
    if (!sdbd_setup(&dc, &hb, &m, &old)) { t_skip("setup"); return; }
    BYTE blob[64]; memset(blob, 0x42, sizeof blob);
    for (DWORD comp = BI_JPEG; comp <= BI_PNG; comp++) {
        struct bd_bmi bi; bd_bmi_init(&bi, 4, 4, 0); bi.h.biCompression = comp; bi.h.biSizeImage = sizeof blob;
        /* a memory DC has no pass-through support (XDCOBJ::bSupportsPassthroughImage), so nothing is drawn */
        int r = sdbd(dc, 0, 0, 4, 4, 0, 0, 0, 4, blob, &bi, DIB_RGB_COLORS, sizeof blob, 0x28);
        CHECKF(r == 0, "comp %lu returned %d", comp, r);
    }
    int touched = 0; for (int i = 0; i < W * H; i++) touched |= m[i] != 0x005a5a5a;
    CHECK(!touched);
    sdbd_done(dc, hb, old);
}
TEST(bd_sdbd_user_matches_syscall_result) {
    BD_NEED(); REQUIRE(U(SetDIBitsToDevice));
    HDC dc; HBITMAP hb; DWORD *m; HGDIOBJ old;
    if (!sdbd_setup(&dc, &hb, &m, &old)) { t_skip("setup"); return; }
    DWORD src[W * H]; fill32(src, W, H, 1, 17);
    struct bd_bmi bi; bmi32(&bi, H);
    int r = U(SetDIBitsToDevice)(dc, 0, 0, W, H, 0, 0, 0, H, src, (BITMAPINFO *)&bi, DIB_RGB_COLORS);
    bd_flush();
    CHECKF(r == H, "returned %d", r);
    CHECK(diff32(m, src, W * H) == 0);
    sdbd_done(dc, hb, old);
}
TEST(bd_sdbd_user_source_rect) {
    BD_NEED(); REQUIRE(U(SetDIBitsToDevice));
    HDC dc; HBITMAP hb; DWORD *m; HGDIOBJ old;
    if (!sdbd_setup(&dc, &hb, &m, &old)) { t_skip("setup"); return; }
    DWORD src[W * H]; fill32(src, W, H, 1, 18);
    struct bd_bmi bi; bmi32(&bi, H);
    /* ySrc is the lower-left corner of the source rectangle, counted up from the bottom of a bottom-up DIB:
     * source rows 0..2 (memory order), columns 2..5, land in the dest top-left 4x3 with the top source row first */
    int r = U(SetDIBitsToDevice)(dc, 0, 0, 4, 3, 2, 0, 0, H, src, (BITMAPINFO *)&bi, DIB_RGB_COLORS);
    bd_flush();
    t_note("SetDIBitsToDevice(source rect) returned %d", r);
    int bad = 0;
    for (int py = 0; py < H; py++) for (int x = 0; x < W; x++) {
        DWORD want = 0x005a5a5a;
        if (x < 4 && py < 3) want = src[(3 - 1 - py) * W + (x + 2)];
        if (!same_rgb(m[(H - 1 - py) * W + x], want)) bad++;
    }
    CHECKF(bad == 0, "%d pixels differ", bad);
    sdbd_done(dc, hb, old);
}

/* ---- StretchDIBits ------------------------------------------------------------------------------------------------------ */
TEST(bd_stretch_one_to_one_copy) {
    BD_NEED(); REQUIRE(U(StretchDIBits));
    HDC dc; HBITMAP hb; DWORD *m; HGDIOBJ old;
    if (!sdbd_setup(&dc, &hb, &m, &old)) { t_skip("setup"); return; }
    DWORD src[W * H]; fill32(src, W, H, 1, 19);
    struct bd_bmi bi; bmi32(&bi, H);
    int r = U(StretchDIBits)(dc, 0, 0, W, H, 0, 0, W, H, src, (BITMAPINFO *)&bi, DIB_RGB_COLORS, SRCCOPY);
    bd_flush();
    CHECKF(r == H, "returned %d", r);
    CHECKF(diff32(m, src, W * H) == 0, "%d pixels differ", diff32(m, src, W * H));
    sdbd_done(dc, hb, old);
}
TEST(bd_stretch_top_down_source) {
    BD_NEED(); REQUIRE(U(StretchDIBits));
    HDC dc; HBITMAP hb; DWORD *m; HGDIOBJ old;
    if (!sdbd_setup(&dc, &hb, &m, &old)) { t_skip("setup"); return; }
    DWORD src[W * H], want[W * H]; fill32(src, W, H, 0, 20); fill32(want, W, H, 1, 20);
    struct bd_bmi bi; bmi32(&bi, -H);
    int r = U(StretchDIBits)(dc, 0, 0, W, H, 0, 0, W, H, src, (BITMAPINFO *)&bi, DIB_RGB_COLORS, SRCCOPY);
    bd_flush();
    CHECK(r == H);
    CHECKF(diff32(m, want, W * H) == 0, "%d pixels differ", diff32(m, want, W * H));
    sdbd_done(dc, hb, old);
}
TEST(bd_stretch_dest_offset) {
    BD_NEED(); REQUIRE(U(StretchDIBits));
    HDC dc; HBITMAP hb; DWORD *m; HGDIOBJ old;
    if (!sdbd_setup(&dc, &hb, &m, &old)) { t_skip("setup"); return; }
    enum { SW = 3, SH = 2 };
    DWORD src[SW * SH]; fill32(src, SW, SH, 1, 21);
    struct bd_bmi bi; bd_bmi_init(&bi, SW, SH, 32);
    U(StretchDIBits)(dc, 4, 3, SW, SH, 0, 0, SW, SH, src, (BITMAPINFO *)&bi, DIB_RGB_COLORS, SRCCOPY);
    bd_flush();
    int bad = 0;
    for (int py = 0; py < H; py++) for (int x = 0; x < W; x++) {
        DWORD want = 0x005a5a5a;
        if (x >= 4 && x < 4 + SW && py >= 3 && py < 3 + SH) want = px32(x - 4, py - 3, 21);
        if (!same_rgb(m[(H - 1 - py) * W + x], want)) bad++;
    }
    CHECKF(bad == 0, "%d pixels differ", bad);
    sdbd_done(dc, hb, old);
}
TEST(bd_stretch_double_size_replicates) {
    BD_NEED(); REQUIRE(U(StretchDIBits));
    HDC dc; HBITMAP hb; DWORD *m; HGDIOBJ old;
    if (!sdbd_setup(&dc, &hb, &m, &old)) { t_skip("setup"); return; }
    enum { SW = 4, SH = 3 };
    DWORD src[SW * SH]; fill32(src, SW, SH, 1, 22);
    struct bd_bmi bi; bd_bmi_init(&bi, SW, SH, 32);
    U(StretchDIBits)(dc, 0, 0, 2 * SW, 2 * SH, 0, 0, SW, SH, src, (BITMAPINFO *)&bi, DIB_RGB_COLORS, SRCCOPY);
    bd_flush();
    int bad = 0;
    for (int py = 0; py < 2 * SH; py++) for (int x = 0; x < 2 * SW; x++)
        if (!same_rgb(m[(H - 1 - py) * W + x], px32(x / 2, py / 2, 22))) bad++;
    CHECKF(bad == 0, "%d pixels differ from 2x pixel replication", bad);
    sdbd_done(dc, hb, old);
}
TEST(bd_stretch_rop_blackness) {
    BD_NEED(); REQUIRE(U(StretchDIBits));
    HDC dc; HBITMAP hb; DWORD *m; HGDIOBJ old;
    if (!sdbd_setup(&dc, &hb, &m, &old)) { t_skip("setup"); return; }
    DWORD src[W * H]; fill32(src, W, H, 1, 23);
    struct bd_bmi bi; bmi32(&bi, H);
    U(StretchDIBits)(dc, 0, 0, W, H, 0, 0, W, H, src, (BITMAPINFO *)&bi, DIB_RGB_COLORS, BLACKNESS);
    bd_flush();
    int bad = 0; for (int i = 0; i < W * H; i++) if (m[i] & 0xffffff) bad++;
    CHECKF(bad == 0, "%d pixels not black", bad);
    sdbd_done(dc, hb, old);
}
TEST(bd_stretch_rop_whiteness) {
    BD_NEED(); REQUIRE(U(StretchDIBits));
    HDC dc; HBITMAP hb; DWORD *m; HGDIOBJ old;
    if (!sdbd_setup(&dc, &hb, &m, &old)) { t_skip("setup"); return; }
    DWORD src[W * H]; fill32(src, W, H, 1, 24);
    struct bd_bmi bi; bmi32(&bi, H);
    U(StretchDIBits)(dc, 0, 0, W, H, 0, 0, W, H, src, (BITMAPINFO *)&bi, DIB_RGB_COLORS, WHITENESS);
    bd_flush();
    int bad = 0; for (int i = 0; i < W * H; i++) if ((m[i] & 0xffffff) != 0xffffff) bad++;
    CHECKF(bad == 0, "%d pixels not white", bad);
    sdbd_done(dc, hb, old);
}
TEST(bd_stretch_null_bits_or_bmi_fails) {
    BD_NEED(); REQUIRE(U(StretchDIBits));
    HDC dc; HBITMAP hb; DWORD *m; HGDIOBJ old;
    if (!sdbd_setup(&dc, &hb, &m, &old)) { t_skip("setup"); return; }
    DWORD src[W * H]; fill32(src, W, H, 1, 25);
    struct bd_bmi bi; bmi32(&bi, H);
    /* NtGdiStretchDIBitsInternal requires pjInit, pbmi and cjMaxInfo to be non-zero */
    int r1 = U(StretchDIBits)(dc, 0, 0, W, H, 0, 0, W, H, NULL, (BITMAPINFO *)&bi, DIB_RGB_COLORS, SRCCOPY);
    int r2 = U(StretchDIBits)(dc, 0, 0, W, H, 0, 0, W, H, src, NULL, DIB_RGB_COLORS, SRCCOPY);
    t_note("StretchDIBits(NULL bits) -> %d, (NULL bmi) -> %d", r1, r2);
    CHECK(r1 == 0 || r1 == (int)GDI_ERROR);
    CHECK(r2 == 0 || r2 == (int)GDI_ERROR);
    int touched = 0; for (int i = 0; i < W * H; i++) touched |= m[i] != 0x005a5a5a;
    CHECK(!touched);
    sdbd_done(dc, hb, old);
}
TEST(bd_stretch_invalid_dc) {
    BD_NEED(); REQUIRE(U(StretchDIBits));
    DWORD src[W * H]; fill32(src, W, H, 1, 26);
    struct bd_bmi bi; bmi32(&bi, H);
    int r = U(StretchDIBits)(NULL, 0, 0, W, H, 0, 0, W, H, src, (BITMAPINFO *)&bi, DIB_RGB_COLORS, SRCCOPY);
    CHECK(r == 0 || r == (int)GDI_ERROR);
}

/* ---- CreateDIBitmap / NtGdiCreateDIBitmapInternal --------------------------------------------------------------------------- */
static int cdb(HDC dc, int cx, int cy, DWORD fInit, const void *bits, const struct bd_bmi *bi, UINT usage, UINT cjInfo, UINT cjBits, HBITMAP *out) {
    *out = UN(NtGdiCreateDIBitmapInternal)(dc, cx, cy, fInit, bits, (const BITMAPINFO *)bi, usage, cjInfo, cjBits, 0, NULL);
    return *out != NULL;
}
TEST(bd_cdb_sys_dimensions_come_from_arguments) {
    BD_NEED(); BD_NEED_NT(NtGdiCreateDIBitmapInternal);
    HDC dc = bd_screen_dc();
    if (!dc) { t_skip("no display DC"); return; }
    HBITMAP hb;
    /* creates a compatible bitmap of (cx, abs(cy)) */
    CHECK(cdb(dc, 5, 3, 0, NULL, NULL, 0, 0, 0, &hb));
    if (hb) { BITMAP bm; bd_getobj(hb, &bm); CHECK(bm.bmWidth == 5 && bm.bmHeight == 3); U(DeleteObject)(hb); }
    CHECK(cdb(dc, 5, -3, 0, NULL, NULL, 0, 0, 0, &hb));
    if (hb) { BITMAP bm; bd_getobj(hb, &bm); CHECK(bm.bmWidth == 5 && bm.bmHeight == 3); U(DeleteObject)(hb); }
    U(DeleteDC)(dc);
}
TEST(bd_cdb_sys_zero_width_fails) {
    BD_NEED(); BD_NEED_NT(NtGdiCreateDIBitmapInternal);
    HDC dc = bd_screen_dc();
    if (!dc) { t_skip("no display DC"); return; }
    HBITMAP hb;
    SET_ERR(0);
    CHECK(!cdb(dc, 0, 3, 0, NULL, NULL, 0, 0, 0, &hb));
    EXPECT_ERR(BD_ERR_INVALID_PARAM); /* cx < 1 */
    if (hb) U(DeleteObject)(hb);
    U(DeleteDC)(dc);
}
TEST(bd_cdb_sys_bad_usage_fails) {
    BD_NEED(); BD_NEED_NT(NtGdiCreateDIBitmapInternal);
    HDC dc = bd_screen_dc();
    if (!dc) { t_skip("no display DC"); return; }
    HBITMAP hb;
    SET_ERR(0);
    CHECK(!cdb(dc, 4, 4, 0, NULL, NULL, 3, 0, 0, &hb)); /* usage > 2 */
    EXPECT_ERR(BD_ERR_INVALID_PARAM);
    if (hb) U(DeleteObject)(hb);
    U(DeleteDC)(dc);
}
TEST(bd_cdb_sys_jpeg_png_header_fails) {
    BD_NEED(); BD_NEED_NT(NtGdiCreateDIBitmapInternal);
    HDC dc = bd_screen_dc();
    if (!dc) { t_skip("no display DC"); return; }
    for (DWORD comp = BI_JPEG; comp <= BI_PNG; comp++) {
        struct bd_bmi bi; bd_bmi_init(&bi, 4, 4, 0); bi.h.biCompression = comp; bi.h.biSizeImage = 16;
        HBITMAP hb; SET_ERR(0);
        CHECKF(!cdb(dc, 4, 4, 0, NULL, &bi, 0, 0x28, 0, &hb), "comp %lu", comp);
        EXPECT_ERR(BD_ERR_INVALID_PARAM);
        if (hb) U(DeleteObject)(hb);
    }
    U(DeleteDC)(dc);
}
TEST(bd_cdb_sys_null_dc_makes_mono) {
    BD_NEED(); BD_NEED_NT(NtGdiCreateDIBitmapInternal);
    HBITMAP hb;
    CHECK(cdb(NULL, 6, 4, 0, NULL, NULL, 0, 0, 0, &hb));
    if (hb) { BITMAP bm; bd_getobj(hb, &bm); CHECK(bm.bmWidth == 6 && bm.bmHeight == 4 && bm.bmBitsPixel == 1); U(DeleteObject)(hb); }
}
TEST(bd_cdb_user_header_only) {
    BD_NEED(); REQUIRE(U(CreateDIBitmap));
    HDC dc = bd_screen_dc();
    if (!dc) { t_skip("no display DC"); return; }
    BITMAPINFOHEADER bh; memset(&bh, 0, sizeof bh); bh.biSize = sizeof bh; bh.biWidth = 7; bh.biHeight = 5; bh.biPlanes = 1; bh.biBitCount = 24;
    HBITMAP hb = U(CreateDIBitmap)(dc, &bh, 0, NULL, NULL, DIB_RGB_COLORS);
    CHECK(hb != NULL);
    if (hb) {
        BITMAP bm; CHECK(bd_getobj(hb, &bm) == sizeof bm);
        CHECK(bm.bmWidth == 7 && bm.bmHeight == 5 && bm.bmBits == NULL);
        CHECK(bm.bmPlanes * bm.bmBitsPixel == U(GetDeviceCaps)(dc, PLANES) * U(GetDeviceCaps)(dc, BITSPIXEL));
        U(DeleteObject)(hb);
    }
    U(DeleteDC)(dc);
}
TEST(bd_cdb_user_top_down_header_height_positive) {
    BD_NEED(); REQUIRE(U(CreateDIBitmap));
    HDC dc = bd_screen_dc();
    if (!dc) { t_skip("no display DC"); return; }
    BITMAPINFOHEADER bh; memset(&bh, 0, sizeof bh); bh.biSize = sizeof bh; bh.biWidth = 7; bh.biHeight = -5; bh.biPlanes = 1; bh.biBitCount = 24;
    HBITMAP hb = U(CreateDIBitmap)(dc, &bh, 0, NULL, NULL, DIB_RGB_COLORS);
    CHECK(hb != NULL);
    if (hb) { BITMAP bm; bd_getobj(hb, &bm); CHECK(bm.bmWidth == 7 && bm.bmHeight == 5); U(DeleteObject)(hb); }
    U(DeleteDC)(dc);
}
TEST(bd_cdb_user_cbm_init_roundtrip_24bpp) {
    BD_NEED(); REQUIRE(U(CreateDIBitmap)); REQUIRE(U(GetDIBits));
    HDC dc = bd_screen_dc();
    if (!dc) { t_skip("no display DC"); return; }
    if (U(GetDeviceCaps)(dc, BITSPIXEL) < 24) { U(DeleteDC)(dc); t_skip("display is not true colour"); return; }
    enum { SW = 5, SH = 4 };
    unsigned st = bd_stride(SW, 24); BYTE src[20 * SH], out[20 * SH];
    memset(src, 0, sizeof src);
    for (int r = 0; r < SH; r++) for (int x = 0; x < SW; x++) { BYTE *p = src + r * st + x * 3; p[0] = (BYTE)(10 * r + x); p[1] = (BYTE)(100 + r); p[2] = (BYTE)(200 + x); }
    struct bd_bmi bi; bd_bmi_init(&bi, SW, SH, 24);
    HBITMAP hb = U(CreateDIBitmap)(dc, &bi.h, CBM_INIT, src, (BITMAPINFO *)&bi, DIB_RGB_COLORS);
    CHECK(hb != NULL);
    if (hb) {
        memset(out, 0xee, sizeof out);
        struct bd_bmi q; bd_bmi_init(&q, SW, SH, 24);
        CHECK(U(GetDIBits)(dc, hb, 0, SH, out, (BITMAPINFO *)&q, DIB_RGB_COLORS) == SH);
        int bad = 0; for (int r = 0; r < SH; r++) if (memcmp(out + r * st, src + r * st, SW * 3)) bad++;
        CHECKF(bad == 0, "%d rows differ", bad);
        U(DeleteObject)(hb);
    }
    U(DeleteDC)(dc);
}

/* ---- DIB colour table -------------------------------------------------------------------------------------------------------- */
TEST(bd_ctab_get_8bpp_selected) {
    BD_NEED(); REQUIRE(U(GetDIBColorTable)); REQUIRE(U(SelectObject));
    HBITMAP hb = bd_dib(4, 4, 8, NULL); HDC dc = bd_mem_dc();
    if (!hb || !dc) { if (hb) U(DeleteObject)(hb); if (dc) U(DeleteDC)(dc); t_skip("setup"); return; }
    HGDIOBJ old = U(SelectObject)(dc, hb);
    RGBQUAD q[256]; memset(q, 0x99, sizeof q);
    UINT n = U(GetDIBColorTable)(dc, 0, 256, q);
    CHECKF(n == 256, "returned %u", n);
    int bad = 0; for (int i = 0; i < 256; i++) if (q[i].rgbRed != i || q[i].rgbGreen != i || q[i].rgbBlue != i) bad++;
    CHECKF(bad == 0, "%d entries differ", bad);
    n = U(GetDIBColorTable)(dc, 10, 5, q);
    CHECK(n == 5 && q[0].rgbRed == 10 && q[4].rgbRed == 14);
    U(SelectObject)(dc, old); U(DeleteDC)(dc); U(DeleteObject)(hb);
}
TEST(bd_ctab_get_zero_entries_returns_zero) {
    BD_NEED(); REQUIRE(U(GetDIBColorTable)); REQUIRE(U(SelectObject));
    HBITMAP hb = bd_dib(4, 4, 8, NULL); HDC dc = bd_mem_dc();
    if (!hb || !dc) { if (hb) U(DeleteObject)(hb); if (dc) U(DeleteDC)(dc); t_skip("setup"); return; }
    HGDIOBJ old = U(SelectObject)(dc, hb);
    RGBQUAD q[4];
    CHECK(U(GetDIBColorTable)(dc, 0, 0, q) == 0); /* gdi32full!GetDIBColorTable: cEntries == 0 never reaches the kernel */
    U(SelectObject)(dc, old); U(DeleteDC)(dc); U(DeleteObject)(hb);
}
TEST(bd_ctab_set_then_get_8bpp) {
    BD_NEED(); REQUIRE(U(GetDIBColorTable)); REQUIRE(U(SetDIBColorTable)); REQUIRE(U(SelectObject));
    HBITMAP hb = bd_dib(4, 4, 8, NULL); HDC dc = bd_mem_dc();
    if (!hb || !dc) { if (hb) U(DeleteObject)(hb); if (dc) U(DeleteDC)(dc); t_skip("setup"); return; }
    HGDIOBJ old = U(SelectObject)(dc, hb);
    RGBQUAD in[3] = { { 1, 2, 3, 0 }, { 4, 5, 6, 0 }, { 7, 8, 9, 0 } }, out[3];
    UINT n = U(SetDIBColorTable)(dc, 20, 3, in);
    CHECKF(n == 3, "SetDIBColorTable returned %u", n);
    memset(out, 0, sizeof out);
    n = U(GetDIBColorTable)(dc, 20, 3, out);
    CHECK(n == 3);
    CHECK(out[0].rgbBlue == 1 && out[0].rgbGreen == 2 && out[0].rgbRed == 3);
    CHECK(out[2].rgbBlue == 7 && out[2].rgbGreen == 8 && out[2].rgbRed == 9);
    RGBQUAD nb; U(GetDIBColorTable)(dc, 19, 1, &nb);
    CHECK(nb.rgbRed == 19 && nb.rgbGreen == 19 && nb.rgbBlue == 19); /* neighbours untouched */
    U(SelectObject)(dc, old); U(DeleteDC)(dc); U(DeleteObject)(hb);
}
TEST(bd_ctab_set_changes_pixel_color) {
    BD_NEED(); REQUIRE(U(SetDIBColorTable)); REQUIRE(U(SelectObject)); REQUIRE(U(GetPixel));
    void *bits = NULL; HBITMAP hb = bd_dib(4, 4, 8, &bits); HDC dc = bd_mem_dc();
    if (!hb || !dc) { if (hb) U(DeleteObject)(hb); if (dc) U(DeleteDC)(dc); t_skip("setup"); return; }
    HGDIOBJ old = U(SelectObject)(dc, hb);
    ((BYTE *)bits)[0] = 77;
    RGBQUAD c = { 0x10, 0x20, 0x30, 0 }; /* B, G, R */
    CHECK(U(SetDIBColorTable)(dc, 77, 1, &c) == 1);
    bd_flush();
    COLORREF px = U(GetPixel)(dc, 0, 3); /* memory row 0 of a bottom-up DIB is the bottom scan line */
    CHECKF(px == RGB(0x30, 0x20, 0x10), "pixel 0x%08lx", px);
    U(SelectObject)(dc, old); U(DeleteDC)(dc); U(DeleteObject)(hb);
}
TEST(bd_ctab_clamped_at_table_end) {
    BD_NEED(); REQUIRE(U(GetDIBColorTable)); REQUIRE(U(SelectObject));
    HBITMAP hb = bd_dib(4, 4, 8, NULL); HDC dc = bd_mem_dc();
    if (!hb || !dc) { if (hb) U(DeleteObject)(hb); if (dc) U(DeleteDC)(dc); t_skip("setup"); return; }
    HGDIOBJ old = U(SelectObject)(dc, hb);
    RGBQUAD q[32];
    UINT n = U(GetDIBColorTable)(dc, 250, 32, q);
    t_note("GetDIBColorTable(250, 32) on a 256 entry table -> %u", n);
    CHECK(n <= 6);
    n = U(GetDIBColorTable)(dc, 256, 4, q);
    t_note("GetDIBColorTable(256, 4) -> %u", n);
    CHECK(n == 0);
    U(SelectObject)(dc, old); U(DeleteDC)(dc); U(DeleteObject)(hb);
}
#define CT_SIZES(X) X(b1, 1, 2) X(b4, 4, 16)
#define X(nm, bpp, ent) \
TEST(bd_ctab_table_size_##nm) { \
    BD_NEED(); REQUIRE(U(GetDIBColorTable)); REQUIRE(U(SelectObject)); \
    HBITMAP hb = bd_dib(8, 4, bpp, NULL); HDC dc = bd_mem_dc(); \
    if (!hb || !dc) { if (hb) U(DeleteObject)(hb); if (dc) U(DeleteDC)(dc); t_skip("setup"); return; } \
    HGDIOBJ old = U(SelectObject)(dc, hb); \
    RGBQUAD q[256]; \
    UINT n = U(GetDIBColorTable)(dc, 0, ent, q); \
    CHECKF(n == (ent), "returned %u", n); \
    if (n == (ent)) { CHECK(q[0].rgbRed == 0); CHECK(q[(ent) - 1].rgbRed == 255); } \
    U(SelectObject)(dc, old); U(DeleteDC)(dc); U(DeleteObject)(hb); \
}
CT_SIZES(X)
#undef X
#define CT_NONE(X) X(b16, 16) X(b24, 24) X(b32, 32)
#define X(nm, bpp) \
TEST(bd_ctab_true_color_has_no_table_##nm) { \
    BD_NEED(); REQUIRE(U(GetDIBColorTable)); REQUIRE(U(SetDIBColorTable)); REQUIRE(U(SelectObject)); \
    HBITMAP hb = bd_dib(8, 4, bpp, NULL); HDC dc = bd_mem_dc(); \
    if (!hb || !dc) { if (hb) U(DeleteObject)(hb); if (dc) U(DeleteDC)(dc); t_skip("setup"); return; } \
    HGDIOBJ old = U(SelectObject)(dc, hb); \
    RGBQUAD q[4] = { { 1, 2, 3, 0 } }; \
    CHECK(U(GetDIBColorTable)(dc, 0, 4, q) == 0); \
    CHECK(U(SetDIBColorTable)(dc, 0, 4, q) == 0); \
    U(SelectObject)(dc, old); U(DeleteDC)(dc); U(DeleteObject)(hb); \
}
CT_NONE(X)
#undef X
TEST(bd_ctab_needs_dib_selected) {
    BD_NEED(); REQUIRE(U(GetDIBColorTable)); REQUIRE(U(SetDIBColorTable));
    HDC dc = bd_mem_dc(); REQUIRE(dc);
    RGBQUAD q[4] = { { 1, 2, 3, 0 } };
    /* The default 1x1 monochrome bitmap is a DDB. Windows answers GetDIBColorTable for a monochrome surface with the
     * fixed two-entry black/white table (it returns the entry count) and refuses SetDIBColorTable. */
    SetLastError(0);
    CHECK(U(GetDIBColorTable)(dc, 0, 2, q) == 2);
    CHECK(q[0].rgbRed == 0 && q[0].rgbGreen == 0 && q[0].rgbBlue == 0);
    CHECK(q[1].rgbRed == 255 && q[1].rgbGreen == 255 && q[1].rgbBlue == 255);
    SetLastError(0);
    CHECK(U(SetDIBColorTable)(dc, 0, 2, q) == 0);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    HBITMAP mono = U(CreateBitmap)(4, 4, 1, 1, NULL);
    if (mono) {
        HGDIOBJ old = U(SelectObject)(dc, mono);
        memset(q, 0xee, sizeof q);
        CHECK(U(GetDIBColorTable)(dc, 0, 2, q) == 2);
        CHECK(q[0].rgbRed == 0 && q[1].rgbRed == 255 && q[1].rgbBlue == 255);
        CHECK(U(SetDIBColorTable)(dc, 0, 2, q) == 0);
        U(SelectObject)(dc, old); U(DeleteObject)(mono);
    }
    /* a bitmap in the display format (32 bpp here) has no table at all */
    HDC scr = U(GetDC)(NULL);
    HBITMAP cb = scr ? U(CreateCompatibleBitmap)(scr, 4, 4) : NULL;
    if (cb && U(GetDeviceCaps)(scr, BITSPIXEL) > 8) {
        HGDIOBJ old = U(SelectObject)(dc, cb);
        SetLastError(0);
        CHECK(U(GetDIBColorTable)(dc, 0, 2, q) == 0);
        EXPECT_ERR(ERROR_INVALID_HANDLE);
        CHECK(U(SetDIBColorTable)(dc, 0, 2, q) == 0);
        U(SelectObject)(dc, old);
    }
    if (cb) U(DeleteObject)(cb);
    if (scr) U(ReleaseDC)(NULL, scr);
    U(DeleteDC)(dc);
}
TEST(bd_ctab_invalid_dc) {
    BD_NEED(); REQUIRE(U(GetDIBColorTable)); REQUIRE(U(SetDIBColorTable));
    RGBQUAD q[4] = { { 1, 2, 3, 0 } };
    CHECK(U(GetDIBColorTable)(NULL, 0, 2, q) == 0);
    CHECK(U(SetDIBColorTable)(NULL, 0, 2, q) == 0);
    HBITMAP hb = bd_dib(4, 4, 8, NULL);
    if (hb) { CHECK(U(GetDIBColorTable)((HDC)hb, 0, 2, q) == 0); U(DeleteObject)(hb); }
}
TEST(bd_ctab_not_visible_until_selected_dib_is_deselected_ok) {
    BD_NEED(); REQUIRE(U(GetDIBColorTable)); REQUIRE(U(SelectObject));
    HBITMAP hb = bd_dib(4, 4, 8, NULL); HDC dc = bd_mem_dc();
    if (!hb || !dc) { if (hb) U(DeleteObject)(hb); if (dc) U(DeleteDC)(dc); t_skip("setup"); return; }
    HGDIOBJ old = U(SelectObject)(dc, hb);
    U(SelectObject)(dc, old);
    RGBQUAD q[2];
    CHECK(U(GetDIBColorTable)(dc, 0, 2, q) == 2); /* deselected: back to the monochrome default bitmap, whose table is black/white */
    CHECK(q[0].rgbRed == 0 && q[1].rgbRed == 255);
    U(DeleteDC)(dc); U(DeleteObject)(hb);
}
