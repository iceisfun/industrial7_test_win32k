/* Direct coverage of NtGdiMonoBitmap, NtGdiSetMiterLimit, NtGdiSetSizeDevice, NtGdiStartPage, NtGdiStretchDIBitsInternal. */
#include "gap.h"

/* ---- NtGdiMonoBitmap(HBITMAP) ------------------------------------------------------------------
 * win32kfull!NtGdiMonoBitmap: a surface handle check (type 5) (object type 5 = surface); on success returns
 * XEPALOBJ::bIsMonochrome(surface palette), otherwise 0. */
#define MONO_BEGIN() \
    GAP_BEGIN(); NtGdiMonoBitmap_pfn mono = UN(NtGdiMonoBitmap); REQUIRE(mono)

TEST(gap1_monobitmap_mono_ddb) {
    MONO_BEGIN();
    HBITMAP b = U(CreateBitmap)(8, 8, 1, 1, NULL);
    CHECK(b != NULL);
    if (!b) return;
    EXPECT_EQ(mono(b), 1);
    U(DeleteObject)(b);
}

TEST(gap1_monobitmap_color_ddb) {
    MONO_BEGIN();
    HBITMAP b = U(CreateBitmap)(8, 8, 1, 32, NULL);
    CHECK(b != NULL);
    if (!b) return;
    EXPECT_EQ(mono(b), 0);   /* a 32bpp surface carries the RGB palette, not the mono one */
    U(DeleteObject)(b);
}

TEST(gap1_monobitmap_compatible_with_screen) {
    MONO_BEGIN();
    HDC scr = U(GetDC)(NULL);
    if (!scr) { t_skip("no screen DC"); return; }
    int bpp = U(GetDeviceCaps)(scr, BITSPIXEL) * U(GetDeviceCaps)(scr, PLANES);
    HBITMAP b = U(CreateCompatibleBitmap)(scr, 8, 8);
    CHECK(b != NULL);
    if (b) {
        if (bpp > 1) EXPECT_EQ(mono(b), 0); else t_note("screen is %d bpp", bpp);
        U(DeleteObject)(b);
    }
    U(ReleaseDC)(NULL, scr);
}

TEST(gap1_monobitmap_default_memdc_bitmap) {
    MONO_BEGIN();
    /* A fresh memory DC owns the 1x1 monochrome stock bitmap; SelectObject hands it back as the previous object. */
    HDC dc = U(CreateCompatibleDC)(NULL);
    HBITMAP b = U(CreateBitmap)(4, 4, 1, 32, NULL);
    CHECK(dc && b);
    if (dc && b) {
        HBITMAP def = (HBITMAP)U(SelectObject)(dc, b);
        CHECK(def != NULL);
        EXPECT_EQ(U(GetObjectType)(def), OBJ_BITMAP);
        EXPECT_EQ(mono(def), 1);
        EXPECT_EQ(mono(b), 0);      /* still lockable while selected into a DC */
        U(SelectObject)(dc, def);
    }
    if (b) U(DeleteObject)(b);
    if (dc) U(DeleteDC)(dc);
}

TEST(gap1_monobitmap_handle_type_bits) {
    MONO_BEGIN();
    HBITMAP b = U(CreateBitmap)(2, 2, 1, 1, NULL);
    if (!b) { t_skip("create"); return; }
    /* bits 16..22 of a GDI handle hold the object type; the kernel locks type 5 (surface) for HBITMAPs. */
    EXPECT_EQ(((ULONG_PTR)b >> 16) & 0x7f, 5);
    U(DeleteObject)(b);
}

TEST(gap1_monobitmap_invalid_handles) {
    MONO_BEGIN();
    HBITMAP bad[] = { NULL, (HBITMAP)(ULONG_PTR)1, (HBITMAP)(ULONG_PTR)0x00050000, (HBITMAP)(ULONG_PTR)0xdeadbeef, (HBITMAP)(ULONG_PTR)0x7fffffff };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) EXPECT_EQ(mono(bad[i]), 0);
}

TEST(gap1_monobitmap_wrong_object_types) {
    MONO_BEGIN();
    HDC dc = U(CreateCompatibleDC)(NULL);
    HPEN pen = U(CreatePen)(PS_SOLID, 1, 0);
    HBRUSH br = U(CreateSolidBrush)(0x112233);
    HRGN rgn = U(CreateRectRgn)(0, 0, 4, 4);
    HFONT f = U(CreateFontW)(-12, 0, 0, 0, 400, 0, 0, 0, 0, 0, 0, 0, 0, L"Arial");
    HGDIOBJ stock[] = { U(GetStockObject)(BLACK_PEN), U(GetStockObject)(WHITE_BRUSH), U(GetStockObject)(SYSTEM_FONT) };
    HGDIOBJ all[] = { dc, pen, br, rgn, f, stock[0], stock[1], stock[2] };
    for (unsigned i = 0; i < sizeof all / sizeof all[0]; i++)
        if (all[i]) EXPECT_EQ(mono((HBITMAP)all[i]), 0);   /* handle type mismatch */
    if (pen) U(DeleteObject)(pen);
    if (br) U(DeleteObject)(br);
    if (rgn) U(DeleteObject)(rgn);
    if (f) U(DeleteObject)(f);
    if (dc) U(DeleteDC)(dc);
}

TEST(gap1_monobitmap_deleted_handle) {
    MONO_BEGIN();
    HBITMAP b = U(CreateBitmap)(2, 2, 1, 1, NULL);
    if (!b) { t_skip("create"); return; }
    EXPECT_EQ(mono(b), 1);
    CHECK(U(DeleteObject)(b));
    EXPECT_EQ(mono(b), 0);
}

TEST(gap1_monobitmap_repeatable) {
    MONO_BEGIN();
    HBITMAP m = U(CreateBitmap)(3, 3, 1, 1, NULL), c = U(CreateBitmap)(3, 3, 1, 32, NULL);
    CHECK(m && c);
    for (int i = 0; i < 50 && m && c; i++) { EXPECT_EQ(mono(m), 1); EXPECT_EQ(mono(c), 0); }
    if (m) U(DeleteObject)(m);
    if (c) U(DeleteObject)(c);
}

/* ---- NtGdiSetMiterLimit(HDC, DWORD floatbits, DWORD *old) ---------------------------------------
 * win32kfull!NtGdiSetMiterLimit -> the miter-limit setter: DCOBJ lock failure or a limit < 1.0
 * is EngSetLastError(0x57) and returns 0; otherwise old limit stored to *peOld (only when non-NULL and the call
 * succeeded) and the new limit stored; success returns 1. The DC default is 10.0 (documented). */
#define MITER_BEGIN() \
    GAP_BEGIN(); NtGdiSetMiterLimit_pfn setml = UN(NtGdiSetMiterLimit); REQUIRE(setml); \
    HDC dc = U(CreateCompatibleDC)(NULL); if (!dc) { t_skip("no DC"); return; }

TEST(gap1_miterlimit_default_is_ten) {
    MITER_BEGIN();
    DWORD old = GAP_SENT;
    EXPECT_EQ(setml(dc, gap_fbits(10.0f), &old), 1);
    EXPECT_EQ(old, gap_fbits(10.0f));
    U(DeleteDC)(dc);
}

TEST(gap1_miterlimit_returns_previous) {
    MITER_BEGIN();
    DWORD old = 0;
    EXPECT_EQ(setml(dc, gap_fbits(3.5f), &old), 1);
    EXPECT_EQ(old, gap_fbits(10.0f));
    EXPECT_EQ(setml(dc, gap_fbits(7.25f), &old), 1);
    EXPECT_EQ(old, gap_fbits(3.5f));
    EXPECT_EQ(setml(dc, gap_fbits(1.0f), &old), 1);
    EXPECT_EQ(old, gap_fbits(7.25f));
    U(DeleteDC)(dc);
}

TEST(gap1_miterlimit_visible_to_getmiterlimit) {
    MITER_BEGIN();
    DWORD old = 0; float got = 0;
    EXPECT_EQ(setml(dc, gap_fbits(4.0f), &old), 1);
    CHECK(U(GetMiterLimit)(dc, &got));
    CHECK(got == 4.0f);
    U(DeleteDC)(dc);
}

TEST(gap1_miterlimit_user_path_same_kernel_state) {
    MITER_BEGIN();
    float old = 0, got = 0;
    CHECK(U(SetMiterLimit)(dc, 6.0f, &old));            /* SetMiterLimitImpl -> NtGdiSetMiterLimit for a plain DC */
    CHECK(old == 10.0f);
    DWORD o2 = 0;
    EXPECT_EQ(setml(dc, gap_fbits(2.0f), &o2), 1);
    EXPECT_EQ(o2, gap_fbits(6.0f));
    CHECK(U(GetMiterLimit)(dc, &got));
    CHECK(got == 2.0f);
    U(DeleteDC)(dc);
}

TEST(gap1_miterlimit_null_old_pointer) {
    MITER_BEGIN();
    EXPECT_EQ(setml(dc, gap_fbits(5.0f), NULL), 1);      /* peOld is optional */
    DWORD o = 0;
    EXPECT_EQ(setml(dc, gap_fbits(5.0f), &o), 1);
    EXPECT_EQ(o, gap_fbits(5.0f));
    U(DeleteDC)(dc);
}

TEST(gap1_miterlimit_below_one_rejected) {
    MITER_BEGIN();
    static const float bad[] = { 0.0f, 0.5f, 0.999999f, -1.0f, -10.0f, -0.0f };
    DWORD o0 = 0;
    EXPECT_EQ(setml(dc, gap_fbits(8.0f), &o0), 1);
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        DWORD old = GAP_SENT;
        GAP_POISON();
        EXPECT_EQ(setml(dc, gap_fbits(bad[i]), &old), 0);
        EXPECT_ERR(0x57);
        EXPECT_EQ(old, GAP_SENT);                           /* *peOld only written on success */
    }
    DWORD o = 0;
    EXPECT_EQ(setml(dc, gap_fbits(8.0f), &o), 1);
    EXPECT_EQ(o, gap_fbits(8.0f));                          /* rejected values left the limit untouched */
    U(DeleteDC)(dc);
}

TEST(gap1_miterlimit_boundaries_accepted) {
    MITER_BEGIN();
    static const float good[] = { 1.0f, 1.0000001f, 2.0f, 10.0f, 100.0f, 1e6f, 3.4e38f };
    for (unsigned i = 0; i < sizeof good / sizeof good[0]; i++) {
        DWORD old;
        EXPECT_EQ(setml(dc, gap_fbits(good[i]), &old), 1);
        float got = 0;
        CHECK(U(GetMiterLimit)(dc, &got));
        CHECK(got == good[i]);
    }
    U(DeleteDC)(dc);
}

TEST(gap1_miterlimit_nonfinite_note) {
    MITER_BEGIN();
    /* bConvertDwordToFloat is not in the corpus symbols; record whether Inf/NaN bit patterns are rejected. */
    static const DWORD pats[] = { 0x7f800000u, 0xff800000u, 0x7fc00000u, 0x7f800001u };
    for (unsigned i = 0; i < 4; i++) {
        DWORD old = GAP_SENT;
        int r = setml(dc, pats[i], &old);
        t_note("miter limit bits %08lx -> ret %d", (unsigned long)pats[i], r);
    }
    U(DeleteDC)(dc);
}

TEST(gap1_miterlimit_invalid_dc) {
    GAP_BEGIN(); NtGdiSetMiterLimit_pfn setml = UN(NtGdiSetMiterLimit); REQUIRE(setml);
    HDC bad[] = { NULL, (HDC)(ULONG_PTR)1, (HDC)(ULONG_PTR)0xdeadbeef, (HDC)(ULONG_PTR)0x00010000 };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        DWORD old = GAP_SENT; GAP_POISON();
        EXPECT_EQ(setml(bad[i], gap_fbits(5.0f), &old), 0);
        EXPECT_ERR(0x57);
        EXPECT_EQ(old, GAP_SENT);
    }
}

TEST(gap1_miterlimit_deleted_dc_and_wrong_type) {
    MITER_BEGIN();
    HBITMAP b = U(CreateBitmap)(2, 2, 1, 1, NULL);
    DWORD old = 0; GAP_POISON();
    if (b) { EXPECT_EQ(setml((HDC)b, gap_fbits(5.0f), &old), 0); EXPECT_ERR(0x57); U(DeleteObject)(b); }
    U(DeleteDC)(dc);
    GAP_POISON();
    EXPECT_EQ(setml(dc, gap_fbits(5.0f), &old), 0);
    EXPECT_ERR(0x57);
}

TEST(gap1_miterlimit_per_dc_state) {
    MITER_BEGIN();
    HDC dc2 = U(CreateCompatibleDC)(NULL);
    if (!dc2) { U(DeleteDC)(dc); t_skip("no 2nd DC"); return; }
    DWORD o;
    EXPECT_EQ(setml(dc, gap_fbits(3.0f), &o), 1);
    EXPECT_EQ(setml(dc2, gap_fbits(9.0f), &o), 1);
    EXPECT_EQ(o, gap_fbits(10.0f));                          /* the second DC still had the default */
    float g1 = 0, g2 = 0;
    U(GetMiterLimit)(dc, &g1); U(GetMiterLimit)(dc2, &g2);
    CHECK(g1 == 3.0f); CHECK(g2 == 9.0f);
    U(DeleteDC)(dc2); U(DeleteDC)(dc);
}

TEST(gap1_miterlimit_survives_save_restore) {
    MITER_BEGIN();
    DWORD o; float g = 0;
    setml(dc, gap_fbits(5.0f), &o);
    int sv = U(SaveDC)(dc);
    setml(dc, gap_fbits(2.0f), &o);
    CHECK(U(RestoreDC)(dc, sv));
    CHECK(U(GetMiterLimit)(dc, &g));
    t_note("miter limit after RestoreDC: %g (saved 5, changed to 2)", g);
    CHECK(g == 5.0f || g == 2.0f);
    U(DeleteDC)(dc);
}

/* ---- NtGdiSetSizeDevice(HDC, cx, cy) ------------------------------------------------------------
 * win32kfull!NtGdiSetSizeDevice: returns 0 when either extent is 0 or the DC cannot be locked, otherwise stores both
 * extents in the DC's extension block and returns 1. */
#define SIZEDEV_BEGIN() \
    GAP_BEGIN(); NtGdiSetSizeDevice_pfn ssd = UN(NtGdiSetSizeDevice); REQUIRE(ssd); \
    HDC dc = U(CreateCompatibleDC)(NULL); if (!dc) { t_skip("no DC"); return; }

TEST(gap1_setsizedevice_valid) {
    SIZEDEV_BEGIN();
    EXPECT_EQ(ssd(dc, 100, 200), 1);
    EXPECT_EQ(ssd(dc, 1, 1), 1);
    EXPECT_EQ(ssd(dc, 20000, 30000), 1);
    U(DeleteDC)(dc);
}

TEST(gap1_setsizedevice_negative_extents_accepted) {
    SIZEDEV_BEGIN();
    EXPECT_EQ(ssd(dc, -5, 7), 1);    /* only == 0 is rejected */
    EXPECT_EQ(ssd(dc, 5, -7), 1);
    EXPECT_EQ(ssd(dc, -1, -1), 1);
    U(DeleteDC)(dc);
}

TEST(gap1_setsizedevice_zero_extents_rejected) {
    SIZEDEV_BEGIN();
    EXPECT_EQ(ssd(dc, 0, 5), 0);
    EXPECT_EQ(ssd(dc, 5, 0), 0);
    EXPECT_EQ(ssd(dc, 0, 0), 0);
    U(DeleteDC)(dc);
}

TEST(gap1_setsizedevice_invalid_dc) {
    GAP_BEGIN(); NtGdiSetSizeDevice_pfn ssd = UN(NtGdiSetSizeDevice); REQUIRE(ssd);
    HDC bad[] = { NULL, (HDC)(ULONG_PTR)1, (HDC)(ULONG_PTR)0xdeadbeef, (HDC)(ULONG_PTR)0x7fffffff };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) EXPECT_EQ(ssd(bad[i], 10, 10), 0);
}

TEST(gap1_setsizedevice_deleted_dc) {
    SIZEDEV_BEGIN();
    CHECK(U(DeleteDC)(dc));
    EXPECT_EQ(ssd(dc, 10, 10), 0);
}

TEST(gap1_setsizedevice_does_not_touch_devicecaps) {
    SIZEDEV_BEGIN();
    int h0 = U(GetDeviceCaps)(dc, HORZRES), v0 = U(GetDeviceCaps)(dc, VERTRES);
    int hs0 = U(GetDeviceCaps)(dc, HORZSIZE), vs0 = U(GetDeviceCaps)(dc, VERTSIZE);
    EXPECT_EQ(ssd(dc, 300, 400), 1);
    EXPECT_EQ(U(GetDeviceCaps)(dc, HORZRES), h0);
    EXPECT_EQ(U(GetDeviceCaps)(dc, VERTRES), v0);
    EXPECT_EQ(U(GetDeviceCaps)(dc, HORZSIZE), hs0);   /* the extents are EMF-playback bookkeeping, not PDEV data */
    EXPECT_EQ(U(GetDeviceCaps)(dc, VERTSIZE), vs0);
    U(DeleteDC)(dc);
}

TEST(gap1_setsizedevice_wrong_object_type) {
    SIZEDEV_BEGIN();
    HBITMAP b = U(CreateBitmap)(2, 2, 1, 1, NULL);
    HBRUSH br = U(CreateSolidBrush)(0);
    if (b) { EXPECT_EQ(ssd((HDC)b, 4, 4), 0); U(DeleteObject)(b); }
    if (br) { EXPECT_EQ(ssd((HDC)br, 4, 4), 0); U(DeleteObject)(br); }
    U(DeleteDC)(dc);
}

/* ---- NtGdiStartPage(HDC) ------------------------------------------------------------------------
 * win32kfull!NtGdiStartPage: when the DC has no valid surface, or the handle's type field is 0x01 (a plain DC:
 * LO_DC_TYPE) or 0x66 (a 16-bit metafile DC), it does EngSetLastError(6) and returns 0. Only printer-class DCs
 * (alternate DC type) continue to the driver. */
#define SP_BEGIN() \
    GAP_BEGIN(); NtGdiStartPage_pfn sp = UN(NtGdiStartPage); REQUIRE(sp)

TEST(gap1_startpage_memory_dc) {
    SP_BEGIN();
    HDC dc = U(CreateCompatibleDC)(NULL);
    if (!dc) { t_skip("no DC"); return; }
    GAP_POISON();
    EXPECT_EQ(sp(dc), 0);
    EXPECT_ERR(6);
    U(DeleteDC)(dc);
}

TEST(gap1_startpage_screen_dc) {
    SP_BEGIN();
    HDC dc = U(GetDC)(NULL);
    if (!dc) { t_skip("no screen DC"); return; }
    GAP_POISON();
    EXPECT_EQ(sp(dc), 0);
    EXPECT_ERR(6);
    U(ReleaseDC)(NULL, dc);
}

TEST(gap1_startpage_invalid_handles) {
    SP_BEGIN();
    HDC bad[] = { NULL, (HDC)(ULONG_PTR)1, (HDC)(ULONG_PTR)0xdeadbeef, (HDC)(ULONG_PTR)0x00010000, (HDC)(ULONG_PTR)0x00660001 };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        GAP_POISON();
        EXPECT_EQ(sp(bad[i]), 0);
        EXPECT_ERR(6);
    }
}

TEST(gap1_startpage_non_dc_object) {
    SP_BEGIN();
    HBITMAP b = U(CreateBitmap)(2, 2, 1, 1, NULL);
    HBRUSH br = U(CreateSolidBrush)(0);
    if (b) { GAP_POISON(); EXPECT_EQ(sp((HDC)b), 0); EXPECT_ERR(6); U(DeleteObject)(b); }
    if (br) { GAP_POISON(); EXPECT_EQ(sp((HDC)br), 0); EXPECT_ERR(6); U(DeleteObject)(br); }
}

TEST(gap1_startpage_deleted_dc) {
    SP_BEGIN();
    HDC dc = U(CreateCompatibleDC)(NULL);
    if (!dc) { t_skip("no DC"); return; }
    U(DeleteDC)(dc);
    GAP_POISON();
    EXPECT_EQ(sp(dc), 0);
    EXPECT_ERR(6);
}

TEST(gap1_startpage_leaves_dc_usable) {
    SP_BEGIN();
    HDC dc = U(CreateCompatibleDC)(NULL);
    HBITMAP b = U(CreateBitmap)(4, 4, 1, 32, NULL);
    if (!dc || !b) { t_skip("setup"); return; }
    HGDIOBJ old = U(SelectObject)(dc, b);
    sp(dc);
    CHECK(U(SetPixel)(dc, 1, 1, 0x00112233) != (COLORREF)-1);
    EXPECT_EQ(U(GetPixel)(dc, 1, 1) & 0xffffff, 0x112233);
    U(SelectObject)(dc, old);
    U(DeleteObject)(b);
    U(DeleteDC)(dc);
}

/* ---- NtGdiStretchDIBitsInternal ------------------------------------------------------------------
 * Arguments (gdi32full!StretchDIBitsImpl call site and the stack offsets used by win32kfull): hdc, xDest, yDest,
 * cxDest, cyDest, xSrc, ySrc, cxSrc, cySrc, pjBits, pbmi, iUsage, rop, cjMaxInfo, cjMaxBits, hcmXform.
 * The routine returns 0 without drawing when pjBits or pbmi is NULL or cjMaxInfo is 0, and (after copying the header)
 * when biSize < 0x28, biSize > cjMaxInfo or the computed bitmap size != cjMaxInfo. */
struct sd_fix { HDC dc; HBITMAP dib, old; DWORD *px; };
static int sd_open(struct sd_fix *f) {
    BITMAPINFO bi; memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = 2; bi.bmiHeader.biHeight = -2;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    f->dc = U(CreateCompatibleDC)(NULL);
    f->dib = U(CreateDIBSection)(f->dc, &bi, DIB_RGB_COLORS, (void **)&f->px, NULL, 0);
    if (!f->dc || !f->dib || !f->px) return 0;
    f->old = (HBITMAP)U(SelectObject)(f->dc, f->dib);
    for (int i = 0; i < 4; i++) f->px[i] = 0xAAAAAAAAu;
    return 1;
}
static void sd_close(struct sd_fix *f) {
    if (f->dc && f->old) U(SelectObject)(f->dc, f->old);
    if (f->dib) U(DeleteObject)(f->dib);
    if (f->dc) U(DeleteDC)(f->dc);
}
static const BYTE sd_src[4] = { 0x33, 0x22, 0x11, 0x00 };           /* one 24bpp pixel (B,G,R) + row padding */
static void sd_bmi(BITMAPINFO *b) {
    memset(b, 0, sizeof *b);
    b->bmiHeader.biSize = sizeof b->bmiHeader; b->bmiHeader.biWidth = 1; b->bmiHeader.biHeight = 1;
    b->bmiHeader.biPlanes = 1; b->bmiHeader.biBitCount = 24; b->bmiHeader.biCompression = BI_RGB;
}
#define SD_BEGIN() \
    GAP_BEGIN(); NtGdiStretchDIBitsInternal_pfn sdb = UN(NtGdiStretchDIBitsInternal); REQUIRE(sdb); \
    struct sd_fix fx; if (!sd_open(&fx)) { sd_close(&fx); t_skip("DIB fixture"); return; } \
    BITMAPINFO bmi; sd_bmi(&bmi)
static int sd_unchanged(const struct sd_fix *f) { for (int i = 0; i < 4; i++) if (f->px[i] != 0xAAAAAAAAu) return 0; return 1; }

TEST(gap1_stretchdibits_direct_writes_pixel) {
    SD_BEGIN();
    int r = sdb(fx.dc, 1, 1, 1, 1, 0, 0, 1, 1, sd_src, &bmi, DIB_RGB_COLORS, SRCCOPY, sizeof bmi.bmiHeader, sizeof sd_src, NULL);
    U(GdiFlush)();
    t_note("NtGdiStretchDIBitsInternal returned %d", r);
    EXPECT_EQ(r, 1);                                         /* scan lines copied */
    EXPECT_EQ(fx.px[3] & 0xffffff, 0x112233);                /* the section memory we mapped was written by the kernel */
    EXPECT_EQ(fx.px[0], 0xAAAAAAAAu);
    EXPECT_EQ(fx.px[1], 0xAAAAAAAAu);
    EXPECT_EQ(fx.px[2], 0xAAAAAAAAu);
    sd_close(&fx);
}

TEST(gap1_stretchdibits_user_path_same_result) {
    SD_BEGIN();
    int r = U(StretchDIBits)(fx.dc, 0, 1, 1, 1, 0, 0, 1, 1, sd_src, &bmi, DIB_RGB_COLORS, SRCCOPY);
    U(GdiFlush)();
    EXPECT_EQ(r, 1);
    EXPECT_EQ(fx.px[2] & 0xffffff, 0x112233);
    sd_close(&fx);
}

TEST(gap1_stretchdibits_stretch_to_all_pixels) {
    SD_BEGIN();
    int r = sdb(fx.dc, 0, 0, 2, 2, 0, 0, 1, 1, sd_src, &bmi, DIB_RGB_COLORS, SRCCOPY, sizeof bmi.bmiHeader, sizeof sd_src, NULL);
    U(GdiFlush)();
    CHECK(r != 0 && r != -1);
    for (int i = 0; i < 4; i++) EXPECT_EQ(fx.px[i] & 0xffffff, 0x112233);
    sd_close(&fx);
}

TEST(gap1_stretchdibits_null_bits) {
    SD_BEGIN();
    EXPECT_EQ(sdb(fx.dc, 0, 0, 1, 1, 0, 0, 1, 1, NULL, &bmi, DIB_RGB_COLORS, SRCCOPY, sizeof bmi.bmiHeader, 4, NULL), 0);
    CHECK(sd_unchanged(&fx));
    sd_close(&fx);
}

TEST(gap1_stretchdibits_null_bitmapinfo) {
    SD_BEGIN();
    EXPECT_EQ(sdb(fx.dc, 0, 0, 1, 1, 0, 0, 1, 1, sd_src, NULL, DIB_RGB_COLORS, SRCCOPY, sizeof bmi.bmiHeader, 4, NULL), 0);
    CHECK(sd_unchanged(&fx));
    sd_close(&fx);
}

TEST(gap1_stretchdibits_zero_info_size) {
    SD_BEGIN();
    EXPECT_EQ(sdb(fx.dc, 0, 0, 1, 1, 0, 0, 1, 1, sd_src, &bmi, DIB_RGB_COLORS, SRCCOPY, 0, 4, NULL), 0);
    CHECK(sd_unchanged(&fx));
    sd_close(&fx);
}

TEST(gap1_stretchdibits_info_size_mismatch) {
    SD_BEGIN();
    /* the computed bitmap size is 40 for a 24bpp BI_RGB header; any other cjMaxInfo is refused. */
    UINT sizes[] = { 1, 12, 39, 41, 44, 52, 0x1000 };
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        BITMAPINFO big; memset(&big, 0, sizeof big); memcpy(&big, &bmi, sizeof bmi);
        EXPECT_EQ(sdb(fx.dc, 1, 1, 1, 1, 0, 0, 1, 1, sd_src, &bmi, DIB_RGB_COLORS, SRCCOPY, sizes[i], 4, NULL), 0);
        CHECK(sd_unchanged(&fx));
    }
    sd_close(&fx);
}

TEST(gap1_stretchdibits_short_header) {
    SD_BEGIN();
    bmi.bmiHeader.biSize = 12;                               /* below 0x28: rejected after the copy-in */
    EXPECT_EQ(sdb(fx.dc, 1, 1, 1, 1, 0, 0, 1, 1, sd_src, &bmi, DIB_RGB_COLORS, SRCCOPY, sizeof bmi.bmiHeader, 4, NULL), 0);
    CHECK(sd_unchanged(&fx));
    bmi.bmiHeader.biSize = 0x100;                            /* larger than cjMaxInfo */
    EXPECT_EQ(sdb(fx.dc, 1, 1, 1, 1, 0, 0, 1, 1, sd_src, &bmi, DIB_RGB_COLORS, SRCCOPY, sizeof bmi.bmiHeader, 4, NULL), 0);
    CHECK(sd_unchanged(&fx));
    sd_close(&fx);
}

TEST(gap1_stretchdibits_invalid_dc) {
    SD_BEGIN();
    HDC bad[] = { NULL, (HDC)(ULONG_PTR)1, (HDC)(ULONG_PTR)0xdeadbeef };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        int r = sdb(bad[i], 0, 0, 1, 1, 0, 0, 1, 1, sd_src, &bmi, DIB_RGB_COLORS, SRCCOPY, sizeof bmi.bmiHeader, 4, NULL);
        EXPECT_EQ(r, 0);
    }
    CHECK(sd_unchanged(&fx));
    sd_close(&fx);
}

TEST(gap1_stretchdibits_zero_size_rects) {
    SD_BEGIN();
    int r = sdb(fx.dc, 0, 0, 0, 0, 0, 0, 1, 1, sd_src, &bmi, DIB_RGB_COLORS, SRCCOPY, sizeof bmi.bmiHeader, 4, NULL);
    t_note("zero dest extents returned %d", r);
    CHECK(sd_unchanged(&fx));
    sd_close(&fx);
}

TEST(gap1_stretchdibits_clipped_dest) {
    SD_BEGIN();
    int r = sdb(fx.dc, 5, 5, 1, 1, 0, 0, 1, 1, sd_src, &bmi, DIB_RGB_COLORS, SRCCOPY, sizeof bmi.bmiHeader, 4, NULL);
    U(GdiFlush)();
    t_note("fully clipped destination returned %d", r);
    CHECK(sd_unchanged(&fx));
    sd_close(&fx);
}

TEST(gap1_stretchdibits_rop_blackness) {
    SD_BEGIN();
    int r = sdb(fx.dc, 0, 0, 1, 1, 0, 0, 1, 1, sd_src, &bmi, DIB_RGB_COLORS, BLACKNESS, sizeof bmi.bmiHeader, 4, NULL);
    U(GdiFlush)();
    CHECK(r != -1);
    EXPECT_EQ(fx.px[0] & 0xffffff, 0);                       /* BLACKNESS ignores the source */
    EXPECT_EQ(fx.px[1], 0xAAAAAAAAu);
    sd_close(&fx);
}
