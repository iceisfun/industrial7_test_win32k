/* NtGdiCreateDIBSection  and the surface it builds:
 *  - the kernel copies cjHeader bytes of the BITMAPINFO and requires cjHeader == the size computed from the header, usage and biSize
 *    and biSize >= 0x28; anything else returns NULL without touching *ppvBits.
 *  - without a section the pixels are committed with ZwAllocateVirtualMemory(-1, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE); with a
 *    section handle (needs SECTION_MAP_READ|SECTION_MAP_WRITE) the view is mapped at offset & 0xffff0000 and *ppvBits = view + (offset & 0xffff).
 *  - the kernel rejects: width < 1, height == 0, biCompression JPEG/PNG (4/5) or any other than RGB/BITFIELDS, usage 2/3/>=4,
 *    BITFIELDS for anything but 16/32bpp, BITFIELDS without room for three masks.
 *  - GetObject fills the DIBSECTION (bmBits = the user pointer, masks from the palette). */
#include "bd.h"

/* ---- formats ----------------------------------------------------------------------------------------------------------- */
/* name, w, h, bpp, compression, masks r/g/b, expected reported compression, expected reported masks r/g/b */
#define DS_ROWS(X) \
    X(1bpp_8x8,        8, 8,   1, BI_RGB,       0, 0, 0,                    BI_RGB,       0, 0, 0) \
    X(1bpp_33x5,       33, 5,  1, BI_RGB,       0, 0, 0,                    BI_RGB,       0, 0, 0) \
    X(4bpp_16x4,       16, 4,  4, BI_RGB,       0, 0, 0,                    BI_RGB,       0, 0, 0) \
    X(4bpp_5x7,        5, 7,   4, BI_RGB,       0, 0, 0,                    BI_RGB,       0, 0, 0) \
    X(8bpp_4x4,        4, 4,   8, BI_RGB,       0, 0, 0,                    BI_RGB,       0, 0, 0) \
    X(8bpp_7x3,        7, 3,   8, BI_RGB,       0, 0, 0,                    BI_RGB,       0, 0, 0) \
    X(8bpp_64x64,      64, 64, 8, BI_RGB,       0, 0, 0,                    BI_RGB,       0, 0, 0) \
    X(16bpp_555_5x3,   5, 3,  16, BI_RGB,       0, 0, 0,                    BI_BITFIELDS, 0x7c00, 0x03e0, 0x001f) \
    X(16bpp_565_5x3,   5, 3,  16, BI_BITFIELDS, 0xf800, 0x07e0, 0x001f,     BI_BITFIELDS, 0xf800, 0x07e0, 0x001f) \
    X(16bpp_555bf_4x4, 4, 4,  16, BI_BITFIELDS, 0x7c00, 0x03e0, 0x001f,     BI_BITFIELDS, 0x7c00, 0x03e0, 0x001f) \
    X(24bpp_1x1,       1, 1,  24, BI_RGB,       0, 0, 0,                    BI_RGB,       0, 0, 0) \
    X(24bpp_3x3,       3, 3,  24, BI_RGB,       0, 0, 0,                    BI_RGB,       0, 0, 0) \
    X(24bpp_10x5,      10, 5, 24, BI_RGB,       0, 0, 0,                    BI_RGB,       0, 0, 0) \
    X(32bpp_1x1,       1, 1,  32, BI_RGB,       0, 0, 0,                    BI_RGB,       0, 0, 0) \
    X(32bpp_17x9,      17, 9, 32, BI_RGB,       0, 0, 0,                    BI_RGB,       0, 0, 0) \
    X(32bpp_bf888_4x4, 4, 4,  32, BI_BITFIELDS, 0xff0000, 0xff00, 0xff,     BI_BITFIELDS, 0xff0000, 0xff00, 0xff) \
    X(32bpp_bf_rgba,   4, 4,  32, BI_BITFIELDS, 0xff, 0xff00, 0xff0000,     BI_BITFIELDS, 0xff, 0xff00, 0xff0000)

static HBITMAP ds_make(int w, int h, int bpp, DWORD comp, DWORD r, DWORD g, DWORD b, void **bits, struct bd_bmi *out) {
    struct bd_bmi bi; void *p = NULL;
    bd_bmi_init(&bi, w, h, bpp);
    if (comp == BI_BITFIELDS) bd_bmi_masks(&bi, r, g, b);
    else if (bpp <= 8) bd_bmi_gray(&bi, (bpp) <= 8 ? 1 << (bpp) : 0);
    if (out) *out = bi;
    HBITMAP hb = U(CreateDIBSection)(NULL, (BITMAPINFO *)&bi, DIB_RGB_COLORS, &p, NULL, 0);
    if (bits) *bits = p;
    return hb;
}

#define X(nm, w, h, bpp, comp, r, g, b, rcomp, rr, rg, rb) \
TEST(bd_dib_format_##nm##_bottom_up) { \
    BD_NEED(); void *bits = NULL; \
    HBITMAP hb = ds_make(w, h, bpp, comp, r, g, b, &bits, NULL); \
    CHECK(hb != NULL && bits != NULL); if (!hb) return; \
    DIBSECTION ds; int n = bd_getdib(hb, &ds); \
    CHECKF(n == (int)sizeof(DIBSECTION), "GetObject returned %d", n); \
    CHECK(ds.dsBm.bmType == 0 && ds.dsBm.bmPlanes == 1); \
    CHECK(ds.dsBm.bmWidth == (w) && ds.dsBm.bmHeight == (h) && ds.dsBm.bmBitsPixel == (bpp)); \
    CHECKF(ds.dsBm.bmWidthBytes == (int)bd_stride(w, bpp), "stride %d want %u", ds.dsBm.bmWidthBytes, bd_stride(w, bpp)); \
    CHECK(ds.dsBm.bmBits == bits); \
    CHECK(ds.dsBmih.biSize == sizeof(BITMAPINFOHEADER) && ds.dsBmih.biWidth == (w) && ds.dsBmih.biHeight == (h)); \
    CHECK(ds.dsBmih.biPlanes == 1 && ds.dsBmih.biBitCount == (bpp)); \
    CHECKF(ds.dsBmih.biCompression == (rcomp), "reported compression %lu want %lu", ds.dsBmih.biCompression, (DWORD)(rcomp)); \
    CHECKF(ds.dsBitfields[0] == (rr) && ds.dsBitfields[1] == (rg) && ds.dsBitfields[2] == (rb), "masks %08lx %08lx %08lx", ds.dsBitfields[0], ds.dsBitfields[1], ds.dsBitfields[2]); \
    CHECK(ds.dshSection == NULL && ds.dsOffset == 0); \
    /* the committed range covers stride * height and is plain read/write committed memory */ \
    MEMORY_BASIC_INFORMATION mbi; \
    if (bd_query(bits, &mbi)) { \
        CHECK(mbi.State == MEM_COMMIT); CHECK(mbi.Protect == PAGE_READWRITE); \
        CHECKF(mbi.RegionSize >= (SIZE_T)bd_stride(w, bpp) * (h), "region %zu", (size_t)mbi.RegionSize); \
        CHECK(mbi.BaseAddress == bits); } \
    CHECK(((ULONG_PTR)bits & 0xfff) == 0); \
    U(DeleteObject)(hb); \
}
DS_ROWS(X)
#undef X

#define X(nm, w, h, bpp, comp, r, g, b, rcomp, rr, rg, rb) \
TEST(bd_dib_format_##nm##_top_down) { \
    BD_NEED(); void *bits = NULL; \
    HBITMAP hb = ds_make(w, -(h), bpp, comp, r, g, b, &bits, NULL); \
    CHECK(hb != NULL && bits != NULL); if (!hb) return; \
    DIBSECTION ds; int n = bd_getdib(hb, &ds); \
    CHECK(n == (int)sizeof(DIBSECTION)); \
    /* BITMAP.bmHeight is the surface height: always positive */ \
    CHECK(ds.dsBm.bmWidth == (w) && ds.dsBm.bmHeight == (h) && ds.dsBm.bmBitsPixel == (bpp)); \
    CHECK(ds.dsBm.bmBits == bits); \
    CHECK(ds.dsBmih.biWidth == (w) && bd_abs(ds.dsBmih.biHeight) == (h)); \
    if (ds.dsBmih.biHeight > 0) t_note("top-down DIB reports positive biHeight %ld", ds.dsBmih.biHeight); \
    U(DeleteObject)(hb); \
}
DS_ROWS(X)
#undef X

/* the same bitmaps through the raw syscall with the cjHeader the real caller computes */
#define X(nm, w, h, bpp, comp, r, g, b, rcomp, rr, rg, rb) \
TEST(bd_dib_sys_format_##nm) { \
    BD_NEED(); BD_NEED_NT(NtGdiCreateDIBSection); \
    struct bd_bmi bi; void *bits = NULL; \
    bd_bmi_init(&bi, w, h, bpp); if (comp == BI_BITFIELDS) bd_bmi_masks(&bi, r, g, b); else if (bpp <= 8) bd_bmi_gray(&bi, (bpp) <= 8 ? 1 << (bpp) : 0); \
    HBITMAP hb = bd_nt_dib(NULL, NULL, 0, &bi, DIB_RGB_COLORS, &bits); \
    CHECK(hb != NULL && bits != NULL); if (!hb) return; \
    CHECK(BD_TYPE7(hb) == 5 && U(GetObjectType)(hb) == OBJ_BITMAP); \
    DIBSECTION ds; CHECK(bd_getdib(hb, &ds) == (int)sizeof ds); \
    CHECK(ds.dsBm.bmBits == bits && ds.dsBm.bmWidth == (w) && ds.dsBm.bmHeight == (h)); \
    U(DeleteObject)(hb); \
}
DS_ROWS(X)
#undef X

/* ---- fresh memory ------------------------------------------------------------------------------------------------------- */
TEST(bd_dib_memory_starts_zeroed) {
    BD_NEED();
    void *bits = NULL;
    HBITMAP hb = bd_dib(32, 32, 32, &bits);
    REQUIRE(hb && bits);
    /* ZwAllocateVirtualMemory returns demand-zero pages and the surface constructor does not touch them */
    const BYTE *p = bits; int nz = 0;
    for (int i = 0; i < 32 * 32 * 4; i++) nz |= p[i];
    CHECK(nz == 0);
    U(DeleteObject)(hb);
}
TEST(bd_dib_memory_writable_and_readable) {
    BD_NEED();
    void *bits = NULL;
    HBITMAP hb = bd_dib(20, 10, 24, &bits);
    REQUIRE(hb && bits);
    BYTE *p = bits; size_t n = (size_t)bd_stride(20, 24) * 10;
    for (size_t i = 0; i < n; i++) p[i] = (BYTE)(i * 7 + 3);
    int bad = 0; for (size_t i = 0; i < n; i++) if (p[i] != (BYTE)(i * 7 + 3)) bad++;
    CHECK(bad == 0);
    U(DeleteObject)(hb);
}
TEST(bd_dib_memory_is_private_or_mapped_view) {
    BD_NEED();
    void *bits = NULL;
    HBITMAP hb = bd_dib(16, 16, 32, &bits);
    REQUIRE(hb && bits);
    MEMORY_BASIC_INFORMATION mbi;
    CHECK(bd_query(bits, &mbi));
    /* NtGdiCreateDIBSection commits the pixels with ZwAllocateVirtualMemory: expected MEM_PRIVATE; record what this build does */
    CHECK(mbi.Type == MEM_PRIVATE || mbi.Type == MEM_MAPPED);
    t_note("DIB bits type 0x%lx base %p alloc base %p protect 0x%lx", mbi.Type, mbi.BaseAddress, mbi.AllocationBase, mbi.Protect);
    U(DeleteObject)(hb);
}
TEST(bd_dib_bits_inside_user_range) {
    BD_NEED();
    void *bits = NULL;
    HBITMAP hb = bd_dib(8, 8, 32, &bits);
    REQUIRE(hb && bits);
    SYSTEM_INFO si; GetSystemInfo(&si);
    CHECK((ULONG_PTR)bits >= (ULONG_PTR)si.lpMinimumApplicationAddress && (ULONG_PTR)bits < (ULONG_PTR)si.lpMaximumApplicationAddress);
    CHECK(((ULONG_PTR)bits & 3) == 0);
    U(DeleteObject)(hb);
}
TEST(bd_dib_two_sections_do_not_overlap) {
    BD_NEED();
    void *b1 = NULL, *b2 = NULL;
    HBITMAP h1 = bd_dib(64, 64, 32, &b1), h2 = bd_dib(64, 64, 32, &b2);
    REQUIRE(h1 && h2);
    CHECK(b1 && b2 && b1 != b2);
    size_t n = 64 * 64 * 4;
    CHECK((char *)b1 + n <= (char *)b2 || (char *)b2 + n <= (char *)b1);
    memset(b1, 0x11, n); memset(b2, 0x22, n);
    CHECK(((BYTE *)b1)[n - 1] == 0x11 && ((BYTE *)b2)[0] == 0x22);
    U(DeleteObject)(h1); U(DeleteObject)(h2);
}
TEST(bd_dib_bits_stable_across_select) {
    BD_NEED(); REQUIRE(U(SelectObject));
    void *bits = NULL;
    HBITMAP hb = bd_dib(8, 8, 32, &bits);
    HDC dc = bd_mem_dc();
    if (!hb || !dc) { if (hb) U(DeleteObject)(hb); if (dc) U(DeleteDC)(dc); t_skip("no DIB/DC"); return; }
    ((DWORD *)bits)[5] = 0xdeadbeef;
    HGDIOBJ old = U(SelectObject)(dc, hb);
    CHECK(old != NULL);
    DIBSECTION ds; bd_getdib(hb, &ds);
    CHECK(ds.dsBm.bmBits == bits);
    CHECK(((DWORD *)bits)[5] == 0xdeadbeef);
    U(SelectObject)(dc, old);
    bd_getdib(hb, &ds);
    CHECK(ds.dsBm.bmBits == bits && ((DWORD *)bits)[5] == 0xdeadbeef);
    U(DeleteDC)(dc); U(DeleteObject)(hb);
}
TEST(bd_dib_getobject_bits_equal_returned_pointer_many) {
    BD_NEED();
    for (int i = 1; i <= 12; i++) {
        void *bits = NULL;
        HBITMAP hb = bd_dib(i * 3, i, 32, &bits);
        DIBSECTION ds;
        CHECK(hb != NULL); if (!hb) continue;
        CHECK(bd_getdib(hb, &ds) == (int)sizeof ds && ds.dsBm.bmBits == bits);
        U(DeleteObject)(hb);
    }
}
TEST(bd_dib_object_not_a_ddb_has_bits_pointer) {
    BD_NEED(); BD_NEED_NT(NtGdiCreateBitmap);
    /* a DDB never reports a bits pointer, a DIB section always does  */
    void *bits = NULL;
    HBITMAP d = bd_dib(4, 4, 32, &bits), s = UN(NtGdiCreateBitmap)(4, 4, 1, 32, NULL);
    BITMAP a, b;
    REQUIRE(d && s);
    bd_getobj(d, &a); bd_getobj(s, &b);
    CHECK(a.bmBits == bits && b.bmBits == NULL);
    CHECK(U(GetObjectW)(d, sizeof(DIBSECTION), &(DIBSECTION){0}) == (int)sizeof(DIBSECTION));
    CHECK(U(GetObjectW)(s, sizeof(DIBSECTION), &(DIBSECTION){0}) == (int)sizeof(BITMAP));
    U(DeleteObject)(d); U(DeleteObject)(s);
}
TEST(bd_dib_without_ppvbits_pointer_succeeds) {
    BD_NEED();
    struct bd_bmi bi; bd_bmi_init(&bi, 4, 4, 32);
    HBITMAP hb = U(CreateDIBSection)(NULL, (BITMAPINFO *)&bi, DIB_RGB_COLORS, NULL, NULL, 0);
    CHECK(hb != NULL);
    if (hb) { DIBSECTION ds; CHECK(bd_getdib(hb, &ds) == (int)sizeof ds && ds.dsBm.bmBits != NULL); U(DeleteObject)(hb); }
}
TEST(bd_dib_with_screen_dc_ok) {
    BD_NEED();
    HDC dc = bd_screen_dc();
    if (!dc) { t_skip("no display DC"); return; }
    struct bd_bmi bi; void *bits = NULL; bd_bmi_init(&bi, 6, 6, 24);
    HBITMAP hb = U(CreateDIBSection)(dc, (BITMAPINFO *)&bi, DIB_RGB_COLORS, &bits, NULL, 0);
    CHECK(hb != NULL && bits != NULL);
    if (hb) U(DeleteObject)(hb);
    U(DeleteDC)(dc);
}
TEST(bd_dib_with_memory_dc_ok) {
    BD_NEED();
    HDC dc = bd_mem_dc(); REQUIRE(dc);
    struct bd_bmi bi; void *bits = NULL; bd_bmi_init(&bi, 6, 6, 24);
    HBITMAP hb = U(CreateDIBSection)(dc, (BITMAPINFO *)&bi, DIB_RGB_COLORS, &bits, NULL, 0);
    CHECK(hb != NULL && bits != NULL);
    if (hb) U(DeleteObject)(hb);
    U(DeleteDC)(dc);
}
/* ---- rejection by the kernel -------------------------------------------------------------------------------------------- */
TEST(bd_dib_reject_null_bmi) {
    BD_NEED_NT(NtGdiCreateDIBSection);
    void *bits = (void *)0x1; /* untouched on failure */
    HBITMAP hb = UN(NtGdiCreateDIBSection)(NULL, NULL, 0, NULL, 0, 0x28, 0, 0, &bits);
    CHECK(hb == NULL); CHECK(bits == (void *)0x1);
    if (hb) U(DeleteObject)(hb);
}
#define DS_CJ(X) X(zero, 0) X(four, 4) X(core, 0xc) X(short24, 0x24) X(plus4, 0x2c) X(plus8, 0x30) X(big, 0x400) X(huge, 0x2710001)
#define X(nm, cj) \
TEST(bd_dib_reject_cjheader_##nm) { \
    BD_NEED_NT(NtGdiCreateDIBSection); \
    struct bd_bmi bi; void *bits = (void *)0x1; bd_bmi_init(&bi, 4, 4, 24); \
    /* cjHeader must equal the computed header size (0x28 for a 24bpp BI_RGB header), else the capture fails */ \
    HBITMAP hb = UN(NtGdiCreateDIBSection)(NULL, NULL, 0, (BITMAPINFO *)&bi, 0, cj, 0, 0, &bits); \
    CHECK(hb == NULL); CHECK(bits == (void *)0x1); if (hb) U(DeleteObject)(hb); \
}
DS_CJ(X)
#undef X
TEST(bd_dib_reject_cjheader_8bpp_needs_color_table) {
    BD_NEED_NT(NtGdiCreateDIBSection);
    struct bd_bmi bi; void *bits = (void *)0x1; bd_bmi_init(&bi, 4, 4, 8); bd_bmi_gray(&bi, 256);
    /* an 8bpp BI_RGB header with biClrUsed == 0 is 0x28 + 256 * 4 bytes; 0x28 alone mismatches */
    HBITMAP hb = UN(NtGdiCreateDIBSection)(NULL, NULL, 0, (BITMAPINFO *)&bi, DIB_RGB_COLORS, 0x28, 0, 0, &bits);
    CHECK(hb == NULL); CHECK(bits == (void *)0x1); if (hb) U(DeleteObject)(hb);
    hb = UN(NtGdiCreateDIBSection)(NULL, NULL, 0, (BITMAPINFO *)&bi, DIB_RGB_COLORS, 0x28 + 256 * 4, 0, 0, &bits);
    CHECK(hb != NULL && bits != (void *)0x1); if (hb) U(DeleteObject)(hb);
}
TEST(bd_dib_clrused_shrinks_required_header) {
    BD_NEED_NT(NtGdiCreateDIBSection);
    struct bd_bmi bi; void *bits = NULL; bd_bmi_init(&bi, 4, 4, 8); bd_bmi_gray(&bi, 16); bi.h.biClrUsed = 16;
    /* the table is min(biClrUsed, 256) entries long */
    CHECK(bd_hdr_size((BITMAPINFO *)&bi, 0) == 0x28 + 16 * 4);
    HBITMAP hb = UN(NtGdiCreateDIBSection)(NULL, NULL, 0, (BITMAPINFO *)&bi, 0, 0x28 + 16 * 4, 0, 0, &bits);
    CHECK(hb != NULL && bits != NULL); if (hb) U(DeleteObject)(hb);
    bits = NULL;
    hb = UN(NtGdiCreateDIBSection)(NULL, NULL, 0, (BITMAPINFO *)&bi, 0, 0x28 + 256 * 4, 0, 0, &bits);
    CHECK(hb == NULL);
    if (hb) U(DeleteObject)(hb);
}
TEST(bd_dib_clrused_above_table_clamped) {
    BD_NEED_NT(NtGdiCreateDIBSection);
    struct bd_bmi bi; void *bits = NULL; bd_bmi_init(&bi, 4, 4, 4); bd_bmi_gray(&bi, 16); bi.h.biClrUsed = 200;
    /* 4bpp: min(200, 16) entries */
    HBITMAP hb = UN(NtGdiCreateDIBSection)(NULL, NULL, 0, (BITMAPINFO *)&bi, 0, 0x28 + 16 * 4, 0, 0, &bits);
    CHECK(hb != NULL); if (hb) U(DeleteObject)(hb);
}
TEST(bd_dib_bitfields_header_needs_masks) {
    BD_NEED_NT(NtGdiCreateDIBSection);
    struct bd_bmi bi; void *bits = NULL; bd_bmi_init(&bi, 4, 4, 32); bd_bmi_masks(&bi, 0xff0000, 0xff00, 0xff);
    /* BI_BITFIELDS with a 0x28 header: three DWORD masks follow, so the header is 0x34 bytes */
    CHECK(bd_hdr_size((BITMAPINFO *)&bi, 0) == 0x34);
    HBITMAP hb = UN(NtGdiCreateDIBSection)(NULL, NULL, 0, (BITMAPINFO *)&bi, 0, 0x28, 0, 0, &bits);
    CHECK(hb == NULL);
    hb = UN(NtGdiCreateDIBSection)(NULL, NULL, 0, (BITMAPINFO *)&bi, 0, 0x34, 0, 0, &bits);
    CHECK(hb != NULL); if (hb) U(DeleteObject)(hb);
}
#define DS_BADBPP(X) X(0) X(2) X(3) X(5) X(7) X(12) X(15) X(48) X(64) X(128)
#define X(bp) \
TEST(bd_dib_reject_bitcount_##bp) { \
    BD_NEED(); BD_NEED_NT(NtGdiCreateDIBSection); \
    struct bd_bmi bi; void *bits = (void *)0x1; bd_bmi_init(&bi, 4, 4, bp); \
    /* a BI_RGB header with any other depth has no valid size, so the capture fails */ \
    HBITMAP hb = UN(NtGdiCreateDIBSection)(NULL, NULL, 0, (BITMAPINFO *)&bi, 0, 0x28, 0, 0, &bits); \
    CHECK(hb == NULL && bits == (void *)0x1); if (hb) U(DeleteObject)(hb); \
    HBITMAP h2 = U(CreateDIBSection)(NULL, (BITMAPINFO *)&bi, DIB_RGB_COLORS, &bits, NULL, 0); \
    CHECK(h2 == NULL); if (h2) U(DeleteObject)(h2); \
}
DS_BADBPP(X)
#undef X
#define DS_BADDIM(X) X(w0, 0, 4) X(h0, 4, 0) X(w_neg, -4, 4) X(w_min, (int)0x80000000, 4) X(both0, 0, 0)
#define X(nm, w, h) \
TEST(bd_dib_reject_dim_##nm) { \
    BD_NEED(); BD_NEED_NT(NtGdiCreateDIBSection); \
    struct bd_bmi bi; void *bits = (void *)0x1; bd_bmi_init(&bi, w, h, 24); \
    HBITMAP hb = UN(NtGdiCreateDIBSection)(NULL, NULL, 0, (BITMAPINFO *)&bi, 0, 0x28, 0, 0, &bits); \
    CHECK(hb == NULL && bits == (void *)0x1); if (hb) U(DeleteObject)(hb); \
    HBITMAP h2 = U(CreateDIBSection)(NULL, (BITMAPINFO *)&bi, DIB_RGB_COLORS, &bits, NULL, 0); \
    CHECK(h2 == NULL); if (h2) U(DeleteObject)(h2); \
}
DS_BADDIM(X)
#undef X
TEST(bd_dib_reject_height_min_int) {
    BD_NEED(); BD_NEED_NT(NtGdiCreateDIBSection);
    /* abs(INT_MIN) overflows the size computation; no bitmap may come out */
    struct bd_bmi bi; void *bits = (void *)0x1; bd_bmi_init(&bi, 4, (int)0x80000000, 24);
    HBITMAP hb = UN(NtGdiCreateDIBSection)(NULL, NULL, 0, (BITMAPINFO *)&bi, 0, 0x28, 0, 0, &bits);
    CHECK(hb == NULL); if (hb) U(DeleteObject)(hb);
}
TEST(bd_dib_reject_size_overflow) {
    BD_NEED(); BD_NEED_NT(NtGdiCreateDIBSection);
    /* stride * height > 2^32: the bits-size computation yields 0 and the kernel returns NULL before allocating */
    struct bd_bmi bi; void *bits = (void *)0x1; bd_bmi_init(&bi, 0x40000000, 0x10, 32);
    HBITMAP hb = UN(NtGdiCreateDIBSection)(NULL, NULL, 0, (BITMAPINFO *)&bi, 0, 0x28, 0, 0, &bits);
    CHECK(hb == NULL && bits == (void *)0x1); if (hb) U(DeleteObject)(hb);
}
#define DS_BADCOMP(X) X(rle8_8bpp, BI_RLE8, 8) X(rle4_4bpp, BI_RLE4, 4) X(jpeg, BI_JPEG, 0) X(png, BI_PNG, 0) X(jpeg_24, BI_JPEG, 24) X(png_32, BI_PNG, 32) X(rle8_24, BI_RLE8, 24)
#define X(nm, comp, bp) \
TEST(bd_dib_reject_compression_##nm) { \
    BD_NEED(); BD_NEED_NT(NtGdiCreateDIBSection); \
    struct bd_bmi bi; void *bits = (void *)0x1; bd_bmi_init(&bi, 4, 4, bp); bi.h.biCompression = comp; bi.h.biSizeImage = 64; \
    if (bp > 0 && bp <= 8) bd_bmi_gray(&bi, (bp) <= 8 ? 1 << (bp) : 0); \
    /* the kernel accepts only BI_RGB, BI_BITFIELDS (and CMYK variants for CMYK DCs) for a section: ERROR_INVALID_PARAMETER */ \
    HBITMAP hb = U(CreateDIBSection)(NULL, (BITMAPINFO *)&bi, DIB_RGB_COLORS, &bits, NULL, 0); \
    CHECK(hb == NULL); if (hb) U(DeleteObject)(hb); \
    hb = UN(NtGdiCreateDIBSection)(NULL, NULL, 0, (BITMAPINFO *)&bi, 0, bd_hdr_size((BITMAPINFO *)&bi, 0), 0, 0, &bits); \
    CHECK(hb == NULL); if (hb) U(DeleteObject)(hb); \
}
DS_BADCOMP(X)
#undef X
TEST(bd_dib_reject_jpeg_png_last_error) {
    BD_NEED();
    struct bd_bmi bi; void *bits = NULL; bd_bmi_init(&bi, 4, 4, 0); bi.h.biCompression = BI_PNG; bi.h.biSizeImage = 100;
    SET_ERR(0);
    /* gdi32full!CreateDIBSectionInternal: biCompression 4/5 -> GdiSetLastError(ERROR_INVALID_PARAMETER) */
    HBITMAP hb = U(CreateDIBSection)(NULL, (BITMAPINFO *)&bi, DIB_RGB_COLORS, &bits, NULL, 0);
    CHECK(hb == NULL); if (hb) { U(DeleteObject)(hb); return; }
    EXPECT_ERR(BD_ERR_INVALID_PARAM);
}
TEST(bd_dib_bitfields_on_24bpp_rejected) {
    BD_NEED(); BD_NEED_NT(NtGdiCreateDIBSection);
    /* the kernel accepts BI_BITFIELDS only for 16 and 32 bits per pixel */
    struct bd_bmi bi; void *bits = (void *)0x1; bd_bmi_init(&bi, 4, 4, 24); bd_bmi_masks(&bi, 0xff0000, 0xff00, 0xff);
    HBITMAP hb = UN(NtGdiCreateDIBSection)(NULL, NULL, 0, (BITMAPINFO *)&bi, 0, 0x34, 0, 0, &bits);
    CHECK(hb == NULL); if (hb) U(DeleteObject)(hb);
    bd_bmi_init(&bi, 4, 4, 8); bd_bmi_masks(&bi, 0xff0000, 0xff00, 0xff);
    hb = UN(NtGdiCreateDIBSection)(NULL, NULL, 0, (BITMAPINFO *)&bi, 0, 0x34, 0, 0, &bits);
    CHECK(hb == NULL); if (hb) U(DeleteObject)(hb);
}
#define DS_USAGE_BAD(X) X(2) X(3) X(4) X(7) X(0x80000000) X(0xffffffff)
#define X(u) \
TEST(bd_dib_reject_usage_##u) { \
    BD_NEED(); BD_NEED_NT(NtGdiCreateDIBSection); \
    struct bd_bmi bi; void *bits = (void *)0x1; bd_bmi_init(&bi, 4, 4, 24); \
    SET_ERR(0); \
    /* (usage & ~3) != 0, usage == 2, or usage == 3 without the palette flag -> ERROR_INVALID_PARAMETER */ \
    HBITMAP hb = UN(NtGdiCreateDIBSection)(NULL, NULL, 0, (BITMAPINFO *)&bi, (DWORD)(u), bd_hdr_size((BITMAPINFO *)&bi, (UINT)(u)), 0, 0, &bits); \
    CHECK(hb == NULL); if (hb) { U(DeleteObject)(hb); return; } \
    CHECK(bits == (void *)0x1); \
}
DS_USAGE_BAD(X)
#undef X
TEST(bd_dib_pal_colors_24bpp_treated_as_rgb) {
    BD_NEED(); BD_NEED_NT(NtGdiCreateDIBSection);
    /* gdi32full!CreateDIBSectionInternal maps DIB_PAL_COLORS to 0 for depths above 8 before the syscall */
    HDC dc = bd_mem_dc(); REQUIRE(dc);
    struct bd_bmi bi; void *bits = NULL; bd_bmi_init(&bi, 4, 4, 24);
    HBITMAP hb = U(CreateDIBSection)(dc, (BITMAPINFO *)&bi, DIB_PAL_COLORS, &bits, NULL, 0);
    CHECK(hb != NULL && bits != NULL); if (hb) U(DeleteObject)(hb);
    U(DeleteDC)(dc);
}
TEST(bd_dib_pal_colors_8bpp_with_memory_dc) {
    BD_NEED();
    /* DIB_PAL_COLORS: the colour table is an array of WORD indices into the DC's palette */
    HDC dc = bd_mem_dc(); REQUIRE(dc);
    struct { BITMAPINFOHEADER h; WORD idx[256]; } bi;
    memset(&bi, 0, sizeof bi);
    bi.h.biSize = sizeof bi.h; bi.h.biWidth = 4; bi.h.biHeight = 4; bi.h.biPlanes = 1; bi.h.biBitCount = 8; bi.h.biClrUsed = 256;
    for (int i = 0; i < 256; i++) bi.idx[i] = (WORD)i;
    void *bits = NULL;
    HBITMAP hb = U(CreateDIBSection)(dc, (BITMAPINFO *)&bi, DIB_PAL_COLORS, &bits, NULL, 0);
    t_note("8bpp DIB_PAL_COLORS with memory DC -> %p", hb);
    if (hb) { DIBSECTION ds; CHECK(bd_getdib(hb, &ds) == (int)sizeof ds && ds.dsBm.bmBitsPixel == 8 && ds.dsBm.bmBits == bits); U(DeleteObject)(hb); }
    U(DeleteDC)(dc);
}
TEST(bd_dib_pal_colors_8bpp_null_dc_fails) {
    BD_NEED();
    /* with DIB_PAL_COLORS the kernel needs a palette source: the kernel locks hdc and fails when there is none */
    struct { BITMAPINFOHEADER h; WORD idx[256]; } bi;
    memset(&bi, 0, sizeof bi);
    bi.h.biSize = sizeof bi.h; bi.h.biWidth = 4; bi.h.biHeight = 4; bi.h.biPlanes = 1; bi.h.biBitCount = 8; bi.h.biClrUsed = 256;
    void *bits = NULL;
    HBITMAP hb = U(CreateDIBSection)(NULL, (BITMAPINFO *)&bi, DIB_PAL_COLORS, &bits, NULL, 0);
    CHECK(hb == NULL); if (hb) U(DeleteObject)(hb);
}

/* ---- section-backed DIBs -------------------------------------------------------------------------------------------------- */
static HANDLE ds_section(DWORD size) { return CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, size, NULL); }

TEST(bd_dib_section_view_is_mapped) {
    BD_NEED(); BD_NEED_NT(NtGdiCreateDIBSection);
    HANDLE sec = ds_section(0x40000); REQUIRE(sec);
    struct bd_bmi bi; void *bits = NULL; bd_bmi_init(&bi, 16, 16, 32);
    HBITMAP hb = U(CreateDIBSection)(NULL, (BITMAPINFO *)&bi, DIB_RGB_COLORS, &bits, sec, 0);
    CHECK(hb != NULL && bits != NULL);
    if (hb && bits) {
        MEMORY_BASIC_INFORMATION mbi;
        CHECK(bd_query(bits, &mbi));
        /* MmMapViewOfSection of a pagefile-backed data section */
        CHECKF(mbi.Type == MEM_MAPPED, "type 0x%lx", mbi.Type);
        CHECK(mbi.State == MEM_COMMIT);
        CHECK(mbi.AllocationBase == bits);
        DIBSECTION ds; CHECK(bd_getdib(hb, &ds) == (int)sizeof ds);
        CHECK(ds.dshSection == sec); CHECK(ds.dsOffset == 0);
        CHECK(ds.dsBm.bmBits == bits);
        U(DeleteObject)(hb);
    }
    CloseHandle(sec);
}
TEST(bd_dib_section_shares_memory_with_second_view) {
    BD_NEED();
    HANDLE sec = ds_section(0x40000); REQUIRE(sec);
    BYTE *view = MapViewOfFile(sec, FILE_MAP_ALL_ACCESS, 0, 0, 0x40000);
    struct bd_bmi bi; void *bits = NULL; bd_bmi_init(&bi, 16, 16, 32);
    HBITMAP hb = U(CreateDIBSection)(NULL, (BITMAPINFO *)&bi, DIB_RGB_COLORS, &bits, sec, 0);
    if (!view || !hb || !bits) { if (hb) U(DeleteObject)(hb); if (view) UnmapViewOfFile(view); CloseHandle(sec); t_skip("no section DIB"); return; }
    CHECK((void *)view != bits);
    for (int i = 0; i < 1024; i++) view[i] = (BYTE)(i ^ 0x5a);
    int bad = 0; for (int i = 0; i < 1024; i++) if (((BYTE *)bits)[i] != (BYTE)(i ^ 0x5a)) bad++;
    CHECK(bad == 0);
    ((BYTE *)bits)[1023] = 0x77;
    CHECK(view[1023] == 0x77);
    U(DeleteObject)(hb);
    CHECK(view[1023] == 0x77); /* the section outlives the bitmap */
    UnmapViewOfFile(view); CloseHandle(sec);
}
TEST(bd_dib_section_offset_low_bits_displace_pointer) {
    BD_NEED();
    HANDLE sec = ds_section(0x40000); REQUIRE(sec);
    BYTE *view = MapViewOfFile(sec, FILE_MAP_ALL_ACCESS, 0, 0, 0x40000);
    REQUIRE(view);
    for (int i = 0; i < 4096; i++) view[0x1000 + i] = (BYTE)(i + 1);
    struct bd_bmi bi; void *bits = NULL; bd_bmi_init(&bi, 16, 16, 32);
    /* offset 0x1000: view mapped at 0, *ppvBits = view + 0x1000 (NtGdiCreateDIBSection: (offset & 0xffff) + base) */
    HBITMAP hb = U(CreateDIBSection)(NULL, (BITMAPINFO *)&bi, DIB_RGB_COLORS, &bits, sec, 0x1000);
    CHECK(hb != NULL && bits != NULL);
    if (hb && bits) {
        MEMORY_BASIC_INFORMATION mbi; CHECK(bd_query(bits, &mbi));
        CHECK(mbi.AllocationBase == (BYTE *)bits - 0x1000);
        CHECK(((BYTE *)bits)[0] == 1 && ((BYTE *)bits)[255] == 0);
        DIBSECTION ds; bd_getdib(hb, &ds);
        CHECK(ds.dsOffset == 0x1000 && ds.dshSection == sec);
        U(DeleteObject)(hb);
    }
    UnmapViewOfFile(view); CloseHandle(sec);
}
TEST(bd_dib_section_offset_above_64k) {
    BD_NEED();
    HANDLE sec = ds_section(0x40000); REQUIRE(sec);
    BYTE *view = MapViewOfFile(sec, FILE_MAP_ALL_ACCESS, 0, 0, 0x40000);
    REQUIRE(view);
    for (int i = 0; i < 64; i++) view[0x10010 + i] = (BYTE)(0xa0 + i);
    struct bd_bmi bi; void *bits = NULL; bd_bmi_init(&bi, 16, 16, 32);
    /* the kernel maps section offset (offset & 0xffff0000) and adds (offset & 0xffff) to the returned pointer */
    HBITMAP hb = U(CreateDIBSection)(NULL, (BITMAPINFO *)&bi, DIB_RGB_COLORS, &bits, sec, 0x10010);
    CHECK(hb != NULL && bits != NULL);
    if (hb && bits) {
        MEMORY_BASIC_INFORMATION mbi; CHECK(bd_query(bits, &mbi));
        CHECK(mbi.AllocationBase == (BYTE *)bits - 0x10);
        CHECK(((BYTE *)bits)[0] == 0xa0 && ((BYTE *)bits)[63] == 0xa0 + 63);
        DIBSECTION ds; bd_getdib(hb, &ds);
        CHECK(ds.dsOffset == 0x10010);
        U(DeleteObject)(hb);
    }
    UnmapViewOfFile(view); CloseHandle(sec);
}
TEST(bd_dib_section_misaligned_offset_rejected) {
    BD_NEED();
    HANDLE sec = ds_section(0x40000); REQUIRE(sec);
    struct bd_bmi bi; bd_bmi_init(&bi, 4, 4, 32);
    DWORD offs[] = { 1, 2, 3, 6, 0x1001 };
    for (int i = 0; i < 5; i++) {
        void *bits = NULL;
        /* gdi32full!CreateDIBSectionInternal: with a section, (offset & 3) != 0 never reaches the kernel */
        HBITMAP hb = U(CreateDIBSection)(NULL, (BITMAPINFO *)&bi, DIB_RGB_COLORS, &bits, sec, offs[i]);
        CHECKF(hb == NULL, "offset %lu accepted", offs[i]);
        if (hb) U(DeleteObject)(hb);
    }
    CloseHandle(sec);
}
TEST(bd_dib_section_stale_handle_rejected) {
    BD_NEED(); BD_NEED_NT(NtGdiCreateDIBSection);
    HANDLE sec = ds_section(0x10000); REQUIRE(sec);
    CloseHandle(sec);
    struct bd_bmi bi; void *bits = (void *)0x1; bd_bmi_init(&bi, 4, 4, 32);
    SET_ERR(0);
    /* ObReferenceObjectByHandle fails -> EngSetLastError(ERROR_INVALID_PARAMETER) */
    HBITMAP hb = UN(NtGdiCreateDIBSection)(NULL, sec, 0, (BITMAPINFO *)&bi, 0, 0x28, 0, 0, &bits);
    CHECK(hb == NULL); if (hb) { U(DeleteObject)(hb); return; }
    EXPECT_ERR(BD_ERR_INVALID_PARAM);
    CHECK(bits == (void *)0x1);
}
TEST(bd_dib_section_wrong_object_type_rejected) {
    BD_NEED(); BD_NEED_NT(NtGdiCreateDIBSection);
    HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL); REQUIRE(ev);
    struct bd_bmi bi; void *bits = (void *)0x1; bd_bmi_init(&bi, 4, 4, 32);
    SET_ERR(0);
    /* the object type check against MmSectionObjectType rejects an event handle */
    HBITMAP hb = UN(NtGdiCreateDIBSection)(NULL, ev, 0, (BITMAPINFO *)&bi, 0, 0x28, 0, 0, &bits);
    CHECK(hb == NULL); if (hb) U(DeleteObject)(hb); else EXPECT_ERR(BD_ERR_INVALID_PARAM);
    CloseHandle(ev);
}
TEST(bd_dib_section_read_only_handle_rejected) {
    BD_NEED(); BD_NEED_NT(NtGdiCreateDIBSection);
    HANDLE sec = ds_section(0x10000), ro = NULL; REQUIRE(sec);
    /* SECTION_MAP_READ only: the kernel asks for 6 (MAP_WRITE | MAP_READ) */
    if (!DuplicateHandle(GetCurrentProcess(), sec, GetCurrentProcess(), &ro, SECTION_MAP_READ, FALSE, 0)) { CloseHandle(sec); t_skip("DuplicateHandle failed"); return; }
    struct bd_bmi bi; void *bits = (void *)0x1; bd_bmi_init(&bi, 4, 4, 32);
    SET_ERR(0);
    HBITMAP hb = UN(NtGdiCreateDIBSection)(NULL, ro, 0, (BITMAPINFO *)&bi, 0, 0x28, 0, 0, &bits);
    CHECK(hb == NULL); if (hb) U(DeleteObject)(hb); else EXPECT_ERR(BD_ERR_INVALID_PARAM);
    CloseHandle(ro); CloseHandle(sec);
}
TEST(bd_dib_section_too_small_rejected) {
    BD_NEED();
    HANDLE sec = ds_section(0x1000); REQUIRE(sec);
    struct bd_bmi bi; void *bits = NULL; bd_bmi_init(&bi, 64, 64, 32); /* 16 KiB > 4 KiB section */
    HBITMAP hb = U(CreateDIBSection)(NULL, (BITMAPINFO *)&bi, DIB_RGB_COLORS, &bits, sec, 0);
    t_note("DIB larger than its 4 KiB section -> %p", hb);
    CHECK(hb == NULL); if (hb) U(DeleteObject)(hb);
    CloseHandle(sec);
}
TEST(bd_dib_section_exact_fit) {
    BD_NEED();
    HANDLE sec = ds_section(0x1000); REQUIRE(sec);
    struct bd_bmi bi; void *bits = NULL; bd_bmi_init(&bi, 32, 32, 32); /* exactly 4 KiB */
    HBITMAP hb = U(CreateDIBSection)(NULL, (BITMAPINFO *)&bi, DIB_RGB_COLORS, &bits, sec, 0);
    CHECK(hb != NULL && bits != NULL); if (hb) U(DeleteObject)(hb);
    CloseHandle(sec);
}
TEST(bd_dib_section_two_dibs_one_section_alias) {
    BD_NEED();
    HANDLE sec = ds_section(0x10000); REQUIRE(sec);
    struct bd_bmi bi; void *b1 = NULL, *b2 = NULL; bd_bmi_init(&bi, 8, 8, 32);
    HBITMAP h1 = U(CreateDIBSection)(NULL, (BITMAPINFO *)&bi, DIB_RGB_COLORS, &b1, sec, 0);
    HBITMAP h2 = U(CreateDIBSection)(NULL, (BITMAPINFO *)&bi, DIB_RGB_COLORS, &b2, sec, 0);
    CHECK(h1 && h2 && b1 && b2);
    if (h1 && h2 && b1 && b2) {
        CHECK(b1 != b2); /* two views of one section: distinct addresses, shared pages */
        ((DWORD *)b1)[3] = 0xCAFEF00D;
        CHECK(((DWORD *)b2)[3] == 0xCAFEF00D);
    }
    if (h1) U(DeleteObject)(h1);
    if (h2) U(DeleteObject)(h2);
    CloseHandle(sec);
}
TEST(bd_dib_section_close_handle_after_create_keeps_bits) {
    BD_NEED();
    HANDLE sec = ds_section(0x10000); REQUIRE(sec);
    struct bd_bmi bi; void *bits = NULL; bd_bmi_init(&bi, 8, 8, 32);
    HBITMAP hb = U(CreateDIBSection)(NULL, (BITMAPINFO *)&bi, DIB_RGB_COLORS, &bits, sec, 0);
    CloseHandle(sec); /* the view holds its own reference on the section object */
    CHECK(hb && bits);
    if (hb && bits) {
        ((DWORD *)bits)[0] = 0x01020304;
        CHECK(((DWORD *)bits)[0] == 0x01020304);
        MEMORY_BASIC_INFORMATION mbi; CHECK(bd_query(bits, &mbi) && mbi.Type == MEM_MAPPED && mbi.State == MEM_COMMIT);
        U(DeleteObject)(hb);
    }
}
TEST(bd_dib_section_bitmap_selected_in_dc_reads_view) {
    BD_NEED();
    HANDLE sec = ds_section(0x10000); REQUIRE(sec);
    BYTE *view = MapViewOfFile(sec, FILE_MAP_ALL_ACCESS, 0, 0, 0x10000);
    HDC dc = bd_mem_dc();
    struct bd_bmi bi; void *bits = NULL; bd_bmi_init(&bi, 4, 4, 32);
    HBITMAP hb = U(CreateDIBSection)(NULL, (BITMAPINFO *)&bi, DIB_RGB_COLORS, &bits, sec, 0);
    if (!view || !dc || !hb) { if (hb) U(DeleteObject)(hb); if (dc) U(DeleteDC)(dc); if (view) UnmapViewOfFile(view); CloseHandle(sec); t_skip("setup"); return; }
    for (int i = 0; i < 16; i++) ((DWORD *)view)[i] = 0x00112233 + i;
    HGDIOBJ old = U(SelectObject)(dc, hb);
    COLORREF c = U(GetPixel)(dc, 0, 3); /* bottom-up: memory row 0 is the bottom scan line y == 3 */
    CHECKF(c == RGB(0x11, 0x22, 0x33) || c == CLR_INVALID, "pixel 0x%08lx", c);
    U(SelectObject)(dc, old);
    U(DeleteDC)(dc); U(DeleteObject)(hb); UnmapViewOfFile(view); CloseHandle(sec);
}

/* ---- orientation -------------------------------------------------------------------------------------------------------- */
TEST(bd_dib_top_down_memory_row0_is_top) {
    BD_NEED(); REQUIRE(U(GetPixel)); REQUIRE(U(SelectObject));
    void *bits = NULL; HBITMAP hb = bd_dib(4, -4, 32, &bits);
    HDC dc = bd_mem_dc();
    if (!hb || !dc) { if (hb) U(DeleteObject)(hb); if (dc) U(DeleteDC)(dc); t_skip("setup"); return; }
    DWORD *p = bits;
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) p[y * 4 + x] = (DWORD)(0x00100000 * (y + 1) + x);
    HGDIOBJ old = U(SelectObject)(dc, hb);
    bd_flush();
    /* a negative biHeight makes memory row 0 the TOP scan line */
    CHECK(U(GetPixel)(dc, 2, 0) == RGB(0x10, 0, 2));
    CHECK(U(GetPixel)(dc, 1, 3) == RGB(0x40, 0, 1));
    U(SelectObject)(dc, old); U(DeleteDC)(dc); U(DeleteObject)(hb);
}
TEST(bd_dib_bottom_up_memory_row0_is_bottom) {
    BD_NEED(); REQUIRE(U(GetPixel)); REQUIRE(U(SelectObject));
    void *bits = NULL; HBITMAP hb = bd_dib(4, 4, 32, &bits);
    HDC dc = bd_mem_dc();
    if (!hb || !dc) { if (hb) U(DeleteObject)(hb); if (dc) U(DeleteDC)(dc); t_skip("setup"); return; }
    DWORD *p = bits;
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) p[y * 4 + x] = (DWORD)(0x00100000 * (y + 1) + x);
    HGDIOBJ old = U(SelectObject)(dc, hb);
    bd_flush();
    CHECK(U(GetPixel)(dc, 2, 3) == RGB(0x10, 0, 2));
    CHECK(U(GetPixel)(dc, 1, 0) == RGB(0x40, 0, 1));
    U(SelectObject)(dc, old); U(DeleteDC)(dc); U(DeleteObject)(hb);
}
TEST(bd_dib_pixel_format_24bpp_bgr_order) {
    BD_NEED(); REQUIRE(U(GetPixel)); REQUIRE(U(SelectObject));
    void *bits = NULL; HBITMAP hb = bd_dib(2, -2, 24, &bits);
    HDC dc = bd_mem_dc();
    if (!hb || !dc) { if (hb) U(DeleteObject)(hb); if (dc) U(DeleteDC)(dc); t_skip("setup"); return; }
    BYTE *p = bits; p[0] = 0x11; p[1] = 0x22; p[2] = 0x33; /* memory order B, G, R */
    HGDIOBJ old = U(SelectObject)(dc, hb);
    bd_flush();
    CHECK(U(GetPixel)(dc, 0, 0) == RGB(0x33, 0x22, 0x11));
    U(SelectObject)(dc, old); U(DeleteDC)(dc); U(DeleteObject)(hb);
}
TEST(bd_dib_pixel_format_565) {
    BD_NEED(); REQUIRE(U(GetPixel)); REQUIRE(U(SelectObject));
    void *bits = NULL; HBITMAP hb = ds_make(2, -2, 16, BI_BITFIELDS, 0xf800, 0x07e0, 0x001f, &bits, NULL);
    HDC dc = bd_mem_dc();
    if (!hb || !dc) { if (hb) U(DeleteObject)(hb); if (dc) U(DeleteDC)(dc); t_skip("setup"); return; }
    ((WORD *)bits)[0] = 0xf800; /* pure red */
    ((WORD *)bits)[1] = 0x001f; /* pure blue */
    HGDIOBJ old = U(SelectObject)(dc, hb);
    bd_flush();
    COLORREF a = U(GetPixel)(dc, 0, 0), b = U(GetPixel)(dc, 1, 0);
    CHECKF(a == RGB(0xff, 0, 0), "red 0x%08lx", a);
    CHECKF(b == RGB(0, 0, 0xff), "blue 0x%08lx", b);
    U(SelectObject)(dc, old); U(DeleteDC)(dc); U(DeleteObject)(hb);
}
TEST(bd_dib_pixel_format_555) {
    BD_NEED(); REQUIRE(U(GetPixel)); REQUIRE(U(SelectObject));
    void *bits = NULL; HBITMAP hb = bd_dib(2, -2, 16, &bits);
    HDC dc = bd_mem_dc();
    if (!hb || !dc) { if (hb) U(DeleteObject)(hb); if (dc) U(DeleteDC)(dc); t_skip("setup"); return; }
    ((WORD *)bits)[0] = 0x7c00; ((WORD *)bits)[1] = 0x03e0;
    HGDIOBJ old = U(SelectObject)(dc, hb);
    bd_flush();
    COLORREF a = U(GetPixel)(dc, 0, 0), b = U(GetPixel)(dc, 1, 0);
    CHECKF(a == RGB(0xff, 0, 0), "red 0x%08lx", a);
    CHECKF(b == RGB(0, 0xff, 0), "green 0x%08lx", b);
    U(SelectObject)(dc, old); U(DeleteDC)(dc); U(DeleteObject)(hb);
}
