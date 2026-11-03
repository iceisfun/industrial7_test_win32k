/* gdi_blit_raster: lifetime, handle-type and involution checks that validate the local process state around raster syscalls. */
#include "br.h"

/* GDI handle type field: bits 16..22 of the handle value (the same field gdi32full tests: DC == 0x10000). */
typedef DWORD (WINAPI *GdiSetBatchLimit_pfn)(DWORD);
#define HTYPE(h) (((ULONG_PTR)(h)) & 0x7f0000)

TEST(gdi_blit_raster_handle_type_bits) {
    if (t_is_wine()) { t_skip("Wine handle encoding differs"); return; }
    BR d = br_make(4, 4); REQUIRE(d.dc);
    HRGN rg = U(CreateRectRgn)(0, 0, 2, 2); HBRUSH br = U(CreateSolidBrush)(RGB(1, 2, 3));
    CHECK(HTYPE(d.dc) == 0x010000);    /* LO_DC_TYPE: gdi32full!GdiTransparentBlt takes the direct-syscall path on this value */
    CHECK(HTYPE(d.bmp) == 0x050000);   /* LO_BITMAP_TYPE */
    CHECK(HTYPE(rg) == 0x040000);      /* LO_REGION_TYPE */
    CHECK(HTYPE(br) == 0x100000);      /* LO_BRUSH_TYPE */
    U(DeleteObject)(rg); U(DeleteObject)(br); br_free(&d);
}

TEST(gdi_blit_raster_dib_bits_pointer_matches_getobject) {
    BR d = br_make(8, 4); REQUIRE(d.dc);
    DIBSECTION ds; memset(&ds, 0, sizeof ds);
    CHECK(U(GetObjectW)(d.bmp, sizeof ds, &ds) == (int)sizeof ds);
    CHECK(ds.dsBm.bmBits == (void *)d.px);          /* the section view is the memory we read pixels from */
    CHECK(ds.dsBm.bmBitsPixel == 32 && ds.dsBm.bmWidth == 8 && ds.dsBm.bmHeight == 4);
    CHECK(U(GetCurrentObject)(d.dc, OBJ_BITMAP) == (HGDIOBJ)d.bmp);
    br_free(&d);
}

TEST(gdi_blit_raster_dib_contents_survive_dc_deletion) {
    NT(NtGdiPatBlt);
    BR d = br_make(4, 4); REQUIRE(d.dc);
    HBRUSH b = U(CreateSolidBrush)(RGB(0x12, 0x34, 0x56)), ob = (HBRUSH)U(SelectObject)(d.dc, b);
    CHECK(p_NtGdiPatBlt(d.dc, 0, 0, 4, 4, PATCOPY)); br_flush();
    U(SelectObject)(d.dc, ob); U(DeleteObject)(b);
    HBITMAP keep = d.bmp; DWORD *px = d.px;
    U(SelectObject)(d.dc, d.old); U(DeleteDC)(d.dc); d.dc = NULL;
    /* The section is owned by the bitmap, not by the DC: the bits stay mapped and unchanged. */
    for (int i = 0; i < 16; i++) CHECKF((px[i] & M24) == RGBPX(0x12, 0x34, 0x56), "px %d", i);
    U(DeleteObject)(keep);
}

TEST(gdi_blit_raster_bitmap_cannot_be_selected_into_two_dcs) {
    BR d = br_make(4, 4); REQUIRE(d.dc);
    HDC o = U(CreateCompatibleDC)(NULL); REQUIRE(o);
    SetLastError(0);
    HGDIOBJ r = U(SelectObject)(o, d.bmp);   /* documented: a bitmap lives in one DC at a time; the second select fails */
    CHECK(r == NULL);
    CHECK(U(GetCurrentObject)(d.dc, OBJ_BITMAP) == (HGDIOBJ)d.bmp);
    U(DeleteDC)(o); br_free(&d);
}

TEST(gdi_blit_raster_default_bitmap_is_1x1) {
    HDC dc = U(CreateCompatibleDC)(NULL); REQUIRE(dc);
    HGDIOBJ bm = U(GetCurrentObject)(dc, OBJ_BITMAP); CHECK(bm != NULL);
    BITMAP b; memset(&b, 0, sizeof b);
    CHECK(U(GetObjectW)(bm, sizeof b, &b) == (int)sizeof b);
    CHECK(b.bmWidth == 1 && b.bmHeight == 1 && b.bmBitsPixel == 1 && b.bmPlanes == 1);   /* stock 1x1 monochrome surface of a new memory DC */
    /* The default surface is not readable or writable pixel by pixel: even (0,0) is CLR_BAD and SetPixel fails. */
    CHECK(U(GetPixel)(dc, 0, 0) == CLR_BAD);
    CHECK(U(GetPixel)(dc, 1, 0) == CLR_BAD);
    CHECK(U(GetPixel)(dc, 0, 1) == CLR_BAD);
    CHECK(U(SetPixel)(dc, 0, 0, RGB(255, 255, 255)) == CLR_BAD);
    /* A bitmap of the same format created explicitly is addressable. */
    HBITMAP mb = U(CreateBitmap)(1, 1, 1, 1, NULL);
    HGDIOBJ old = mb ? U(SelectObject)(dc, mb) : NULL;
    if (old) { CHECK(U(GetPixel)(dc, 0, 0) == 0); U(SelectObject)(dc, old); }
    if (mb) U(DeleteObject)(mb);
    U(DeleteDC)(dc);
}

TEST(gdi_blit_raster_no_gdi_object_leak_across_blits) {
    NT(NtGdiBitBlt); NT(NtGdiPatBlt); NT(NtGdiStretchBlt); REQUIRE(U(GetGuiResources));
    BR s = br_make(8, 8), d = br_make(8, 8); REQUIRE(s.dc && d.dc);
    br_fill_pat(&s);
    DWORD a = U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS);
    for (int i = 0; i < 200; i++) {
        HBRUSH b = U(CreateSolidBrush)(RGB(i, i, i)), ob = (HBRUSH)U(SelectObject)(d.dc, b);
        p_NtGdiPatBlt(d.dc, 0, 0, 8, 8, PATCOPY);
        p_NtGdiBitBlt(d.dc, 0, 0, 8, 8, s.dc, 0, 0, SRCINVERT, CLR_BAD, 0);
        p_NtGdiStretchBlt(d.dc, 0, 0, 16, 16, s.dc, 0, 0, 8, 8, SRCCOPY, CLR_BAD);
        U(SelectObject)(d.dc, ob); U(DeleteObject)(b);
    }
    br_flush();
    CHECK(U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS) == a);   /* blits allocate no persistent GDI objects */
    br_free(&s); br_free(&d);
}

/* Involutions: applying the same invert-type ROP twice restores the destination bit-exactly. */
TEST(gdi_blit_raster_dstinvert_twice_restores) {
    NT(NtGdiPatBlt); BR d = br_make(8, 8); REQUIRE(d.dc);
    br_fill_pat(&d);
    CHECK(p_NtGdiPatBlt(d.dc, 0, 0, 8, 8, DSTINVERT)); br_flush();
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) CHECKF(br_at(&d, x, y) == (br_pat(x, y) ^ M24), "inv (%d,%d)", x, y);
    CHECK(p_NtGdiPatBlt(d.dc, 0, 0, 8, 8, DSTINVERT)); br_flush();
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) CHECKF(br_at(&d, x, y) == br_pat(x, y), "restore (%d,%d)", x, y);
    br_free(&d);
}

TEST(gdi_blit_raster_srcinvert_twice_restores) {
    NT(NtGdiBitBlt); BR s = br_make(8, 8), d = br_make(8, 8); REQUIRE(s.dc && d.dc);
    br_fill_pat(&s); br_fill(&d, RGBPX(0xA5, 0x5A, 0x3C));
    for (int pass = 0; pass < 2; pass++) { CHECK(p_NtGdiBitBlt(d.dc, 0, 0, 8, 8, s.dc, 0, 0, SRCINVERT, CLR_BAD, 0)); br_flush(); }
    CHECK(br_all(&d, 0, 0, 8, 8, RGBPX(0xA5, 0x5A, 0x3C)));
    br_free(&s); br_free(&d);
}

TEST(gdi_blit_raster_notsrccopy_twice_through_scratch_restores) {
    NT(NtGdiBitBlt); BR s = br_make(8, 8), t = br_make(8, 8), d = br_make(8, 8); REQUIRE(s.dc && t.dc && d.dc);
    br_fill_pat(&s);
    CHECK(p_NtGdiBitBlt(t.dc, 0, 0, 8, 8, s.dc, 0, 0, NOTSRCCOPY, CLR_BAD, 0));
    CHECK(p_NtGdiBitBlt(d.dc, 0, 0, 8, 8, t.dc, 0, 0, NOTSRCCOPY, CLR_BAD, 0)); br_flush();
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) CHECKF(br_at(&d, x, y) == br_pat(x, y), "(%d,%d)", x, y);
    br_free(&s); br_free(&t); br_free(&d);
}

TEST(gdi_blit_raster_self_copy_same_rect_is_noop) {
    NT(NtGdiBitBlt); BR d = br_make(8, 8); REQUIRE(d.dc);
    br_fill_pat(&d);
    CHECK(p_NtGdiBitBlt(d.dc, 0, 0, 8, 8, d.dc, 0, 0, SRCCOPY, CLR_BAD, 0)); br_flush();
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) CHECKF(br_at(&d, x, y) == br_pat(x, y), "(%d,%d)", x, y);
    br_free(&d);
}

TEST(gdi_blit_raster_patblt_invalid_dc_returns_false) {
    NT(NtGdiPatBlt); BR d = br_make(4, 4); REQUIRE(d.dc); br_fill(&d, 0x112233);
    HDC dead = d.dc; U(SelectObject)(dead, d.old); U(DeleteDC)(dead); d.dc = NULL;
    CHECK(!p_NtGdiPatBlt(NULL, 0, 0, 4, 4, BLACKNESS));
    CHECK(!p_NtGdiPatBlt(dead, 0, 0, 4, 4, BLACKNESS));        /* stale handle: handle lookup fails, nothing drawn */
    CHECK(!p_NtGdiPatBlt((HDC)(ULONG_PTR)0x1234, 0, 0, 4, 4, BLACKNESS));
    br_flush();
    CHECK(br_all(&d, 0, 0, 4, 4, 0x112233));
    U(DeleteObject)(d.bmp);
}

TEST(gdi_blit_raster_batch_limit_roundtrip) {
    GDIF(GdiSetBatchLimit);
    DWORD prev = p_GdiSetBatchLimit(1);
    DWORD one = p_GdiSetBatchLimit(prev);
    CHECK(one == 1);                          /* GdiSetBatchLimit returns the limit it replaced */
    CHECK(p_GdiSetBatchLimit(prev) == prev);
}
