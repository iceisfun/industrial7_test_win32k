/* NtGdiSetBoundsRect / NtGdiCreateDIBBrush. */
#include "g10.h"

/* ---- SetBoundsRect state machine (win32kfull SetBoundsRect): the return value is the state BEFORE the call:
 *   bit0 DCB_RESET (rect is empty), bit1 DCB_ACCUMULATE (rect is non-empty), bit2 DCB_ENABLE / bit3 DCB_DISABLE.
 * Fresh DC: empty + disabled = 9. Bits 0x4000/0x8000 select two alternate rects and are echoed in the result. ---- */
#define DCB_RESET_  0x1
#define DCB_ACCUM_  0x2
#define DCB_ENABLE_ 0x4
#define DCB_DISABLE_ 0x8
static UINT sbr(HDC dc, const RECT *r, UINT f) { return UN(NtGdiSetBoundsRect)(dc, r, f); }

/* Bounds are clipped to the selected bitmap, so rectangles up to 64x64 need a bitmap that large (the default 1x1 would clip them to
 * the origin). *old and *bm are released by big_dc_free. */
static HDC big_dc(HBITMAP *bm, HGDIOBJ *old) {
    HDC dc = g10_mem_dc();
    HDC scr = U(GetDC)(NULL);
    *bm = NULL; *old = NULL;
    if (dc && scr) *bm = U(CreateCompatibleBitmap)(scr, 64, 64);
    if (scr) U(ReleaseDC)(NULL, scr);
    if (!dc || !*bm) { if (*bm) U(DeleteObject)(*bm); g10_mem_dc_free(dc); return NULL; }
    *old = U(SelectObject)(dc, *bm);
    return dc;
}
static void big_dc_free(HDC dc, HBITMAP bm, HGDIOBJ old) { U(SelectObject)(dc, old); U(DeleteObject)(bm); g10_mem_dc_free(dc); }

TEST(gap10_bounds_fresh_dc_state_is_reset_disabled) {
    REQUIRE(UN(NtGdiSetBoundsRect));
    HDC dc = g10_mem_dc(); REQUIRE(dc);
    G10_EQ(sbr(dc, NULL, 0), DCB_RESET_ | DCB_DISABLE_);
    G10_EQ(sbr(dc, NULL, 0), 9);
    G10_EQ(U(SetBoundsRect)(dc, NULL, 0), 9);
    g10_mem_dc_free(dc);
}
TEST(gap10_bounds_enable_disable_flags_one_at_a_time) {
    REQUIRE(UN(NtGdiSetBoundsRect));
    HDC dc = g10_mem_dc(); REQUIRE(dc);
    G10_EQ(sbr(dc, NULL, DCB_ENABLE_), 9);    /* returns the previous state */
    G10_EQ(sbr(dc, NULL, 0), 5);              /* reset + enabled */
    G10_EQ(sbr(dc, NULL, DCB_DISABLE_), 5);
    G10_EQ(sbr(dc, NULL, 0), 9);
    g10_mem_dc_free(dc);
}
TEST(gap10_bounds_enable_then_disable_in_one_call_leaves_disabled) {
    REQUIRE(UN(NtGdiSetBoundsRect));
    HDC dc = g10_mem_dc(); REQUIRE(dc);
    sbr(dc, NULL, DCB_ENABLE_ | DCB_DISABLE_);   /* SetBoundsRect applies ENABLE then DISABLE */
    G10_EQ(sbr(dc, NULL, 0), 9);
    g10_mem_dc_free(dc);
}
TEST(gap10_bounds_first_accumulate_on_fresh_dc_includes_origin) {
    REQUIRE(UN(NtGdiSetBoundsRect));
    HBITMAP bm; HGDIOBJ old; HDC dc = big_dc(&bm, &old); REQUIRE(dc);
    /* Observed on Windows 10: although a fresh DC reports "empty", its first accumulate behaves as a union with the zero rectangle */
    RECT r = { 10, 20, 30, 40 }, out = {0};
    G10_EQ(sbr(dc, &r, DCB_ACCUM_), 9);
    UINT st = U(GetBoundsRect)(dc, &out, 0);
    G10_EQ(st, 3);
    CHECKF(out.left == 0 && out.top == 0 && out.right == 30 && out.bottom == 40, "rect %ld,%ld,%ld,%ld", out.left, out.top, out.right, out.bottom);
    big_dc_free(dc, bm, old);
}
TEST(gap10_bounds_accumulate_sets_nonempty_state) {
    REQUIRE(UN(NtGdiSetBoundsRect));
    HBITMAP bm; HGDIOBJ old; HDC dc = big_dc(&bm, &old); REQUIRE(dc);
    RECT r = { 10, 20, 30, 40 };
    sbr(dc, NULL, DCB_RESET_);                /* an explicit reset makes the first accumulate exact */
    G10_EQ(sbr(dc, &r, DCB_ACCUM_), 9);
    G10_EQ(sbr(dc, NULL, 0), 0xb);            /* non-empty + disabled */
    RECT out = {0};
    UINT st = U(GetBoundsRect)(dc, &out, 0);
    G10_EQ(st, 3);                            /* GetBoundsRect reports only the set/reset bits, not enable or disable */
    CHECKF(out.left == 10 && out.top == 20 && out.right == 30 && out.bottom == 40, "rect %ld,%ld,%ld,%ld", out.left, out.top, out.right, out.bottom);
    big_dc_free(dc, bm, old);
}
TEST(gap10_bounds_accumulate_is_a_union) {
    REQUIRE(UN(NtGdiSetBoundsRect));
    HBITMAP bm; HGDIOBJ old; HDC dc = big_dc(&bm, &old); REQUIRE(dc);
    RECT a = { 10, 20, 30, 40 }, b = { 5, 25, 15, 60 };
    sbr(dc, NULL, DCB_RESET_);
    sbr(dc, &a, DCB_ACCUM_);
    sbr(dc, &b, DCB_ACCUM_);
    RECT out = {0};
    U(GetBoundsRect)(dc, &out, 0);
    CHECKF(out.left == 5 && out.top == 20 && out.right == 30 && out.bottom == 60, "union %ld,%ld,%ld,%ld", out.left, out.top, out.right, out.bottom);
    big_dc_free(dc, bm, old);
}
TEST(gap10_bounds_reset_clears_the_rectangle) {
    REQUIRE(UN(NtGdiSetBoundsRect));
    HDC dc = g10_mem_dc(); REQUIRE(dc);
    RECT r = { 1, 2, 3, 4 };
    sbr(dc, &r, DCB_ACCUM_);
    G10_EQ(sbr(dc, NULL, DCB_RESET_), 0xb);
    G10_EQ(sbr(dc, NULL, 0), 9);
    g10_mem_dc_free(dc);
}
TEST(gap10_bounds_reset_and_accumulate_together_leaves_only_the_new_rect) {
    REQUIRE(UN(NtGdiSetBoundsRect));
    HBITMAP bm; HGDIOBJ old; HDC dc = big_dc(&bm, &old); REQUIRE(dc);
    RECT a = { 0, 0, 60, 60 }, b = { 40, 40, 50, 50 };
    sbr(dc, &a, DCB_ACCUM_);
    sbr(dc, &b, DCB_RESET_ | DCB_ACCUM_);       /* reset runs before the accumulate in SetBoundsRect */
    RECT out = {0};
    U(GetBoundsRect)(dc, &out, 0);
    CHECKF(out.left == 40 && out.top == 40 && out.right == 50 && out.bottom == 50, "rect %ld,%ld,%ld,%ld", out.left, out.top, out.right, out.bottom);
    big_dc_free(dc, bm, old);
}
TEST(gap10_bounds_null_rect_ignores_accumulate_bit) {
    REQUIRE(UN(NtGdiSetBoundsRect));
    HDC dc = g10_mem_dc(); REQUIRE(dc);
    /* NtGdiSetBoundsRect clears bit 1 when the RECT pointer is NULL */
    G10_EQ(sbr(dc, NULL, DCB_ACCUM_), 9);
    G10_EQ(sbr(dc, NULL, 0), 9);
    G10_EQ(U(SetBoundsRect)(dc, NULL, DCB_ACCUMULATE), 9);
    G10_EQ(U(SetBoundsRect)(dc, NULL, 0), 9);
    g10_mem_dc_free(dc);
}
TEST(gap10_bounds_flags_hi_bits_select_alternate_rects) {
    REQUIRE(UN(NtGdiSetBoundsRect));
    HDC dc = g10_mem_dc(); REQUIRE(dc);
    /* bits 14/15 pick separate rect+enable slots and are echoed in the result; the primary slot is unaffected */
    G10_EQ(sbr(dc, NULL, 0x4000 | DCB_ENABLE_), 0x4009);
    G10_EQ(sbr(dc, NULL, 0x4000), 0x4005);
    G10_EQ(sbr(dc, NULL, 0x8000), 0x8009);
    G10_EQ(sbr(dc, NULL, 0), 9);
    RECT r = { 7, 8, 9, 10 };
    sbr(dc, &r, 0x8000 | DCB_ACCUM_);
    G10_EQ(sbr(dc, NULL, 0x8000), 0x800b);
    G10_EQ(sbr(dc, NULL, 0), 9);
    g10_mem_dc_free(dc);
}
TEST(gap10_bounds_user_wrapper_strips_hi_bits) {
    HDC dc = g10_mem_dc(); REQUIRE(dc);
    /* gdi32full!SetBoundsRect masks flags with 0xffff3fff, so the alternate slots are unreachable from user mode */
    G10_EQ(U(SetBoundsRect)(dc, NULL, 0x4000), 9);
    G10_EQ(U(SetBoundsRect)(dc, NULL, 0xC000 | DCB_ENABLE), 9);
    G10_EQ(U(SetBoundsRect)(dc, NULL, 0), 5);
    g10_mem_dc_free(dc);
}
TEST(gap10_bounds_invalid_dc_returns_zero) {
    REQUIRE(UN(NtGdiSetBoundsRect));
    RECT r = { 1, 2, 3, 4 };
    HDC bad[] = { NULL, (HDC)(ULONG_PTR)0x1234 };
    for (unsigned i = 0; i < 2; i++) {
        G10_EQ(sbr(bad[i], NULL, 0), 0);
        G10_EQ(sbr(bad[i], &r, DCB_ACCUM_), 0);
    }
}
TEST(gap10_bounds_state_is_per_dc) {
    REQUIRE(UN(NtGdiSetBoundsRect));
    HDC a = g10_mem_dc(), b = g10_mem_dc(); REQUIRE(a && b);
    RECT r = { 1, 1, 2, 2 };
    sbr(a, &r, DCB_ACCUM_ | DCB_ENABLE_);
    G10_EQ(sbr(b, NULL, 0), 9);
    G10_EQ(sbr(a, NULL, 0), 7);
    g10_mem_dc_free(a); g10_mem_dc_free(b);
}
TEST(gap10_bounds_drawing_accumulates_when_enabled) {
    REQUIRE(UN(NtGdiSetBoundsRect));
    HDC dc = g10_mem_dc(); REQUIRE(dc);
    HDC scr = U(GetDC)(NULL);
    HBITMAP bm = U(CreateCompatibleBitmap)(scr, 64, 64);
    if (scr) U(ReleaseDC)(NULL, scr);
    if (!bm) { g10_mem_dc_free(dc); t_skip("no bitmap"); return; }
    HGDIOBJ old = U(SelectObject)(dc, bm);
    sbr(dc, NULL, DCB_RESET_ | DCB_ENABLE_);
    U(Rectangle)(dc, 10, 12, 30, 40);
    RECT out = {0};
    UINT st = U(GetBoundsRect)(dc, &out, 0);
    t_note("after Rectangle: state 0x%x rect %ld,%ld,%ld,%ld", st, out.left, out.top, out.right, out.bottom);
    /* GetBoundsRect reports only the set/reset bits (3); the enable state is read through the syscall */
    G10_EQ(st, 3);
    CHECK(sbr(dc, NULL, 0) & DCB_ENABLE_);
    CHECK(out.left <= 10 && out.top <= 12 && out.right >= 30 && out.bottom >= 40);   /* bounds cover at least the drawn rectangle */
    U(SelectObject)(dc, old); U(DeleteObject)(bm);
    g10_mem_dc_free(dc);
}

/* ---- DIB pattern brushes (gdi32full!CreateBrush type 6 -> NtGdiCreateDIBBrush) ---- */
typedef struct { BITMAPINFOHEADER h; RGBQUAD pal[256]; } PBI;
static int ncolors(int bpp) { return bpp == 1 ? 2 : bpp == 4 ? 16 : bpp == 8 ? 16 : 0; }
static int stride(int w, int bpp) { return ((w * bpp + 31) / 32) * 4; }
/* Builds a packed 8x8 DIB (bottom-up) in `out` and returns total size; pixel (x,y) colour is fn(x,y). */
static RGBQUAD pix_color(int x, int y) { RGBQUAD q = { (BYTE)(10 + x * 20), (BYTE)(10 + y * 20), (BYTE)(200 - x * 10 - y * 10), 0 }; return q; }
static size_t build_dib(BYTE *out, int bpp, RGBQUAD *expect /*[64]*/) {
    int nc = ncolors(bpp), pb = nc * 4, st = stride(8, bpp);
    BITMAPINFOHEADER *h = (BITMAPINFOHEADER *)out;
    memset(out, 0, sizeof(BITMAPINFOHEADER) + pb + st * 8);
    h->biSize = sizeof *h; h->biWidth = 8; h->biHeight = 8; h->biPlanes = 1; h->biBitCount = (WORD)bpp;
    h->biCompression = BI_RGB; h->biClrUsed = nc;
    RGBQUAD *pal = (RGBQUAD *)(out + sizeof *h);
    for (int i = 0; i < nc; i++) { pal[i].rgbBlue = (BYTE)(i * 15 + 3); pal[i].rgbGreen = (BYTE)(255 - i * 12); pal[i].rgbRed = (BYTE)(i * 7 + 100); }
    BYTE *bits = out + sizeof *h + pb;
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {
        BYTE *row = bits + (7 - y) * st;          /* bottom-up: memory row 0 is the bottom scan line y == 7 */
        RGBQUAD c;
        if (bpp >= 24) {
            c = pix_color(x, y);
            if (bpp == 24) { row[x * 3] = c.rgbBlue; row[x * 3 + 1] = c.rgbGreen; row[x * 3 + 2] = c.rgbRed; }
            else { row[x * 4] = c.rgbBlue; row[x * 4 + 1] = c.rgbGreen; row[x * 4 + 2] = c.rgbRed; }
        } else {
            int idx = (x + 2 * y) % nc;
            c = pal[idx];
            if (bpp == 8) row[x] = (BYTE)idx;
            else if (bpp == 4) row[x / 2] |= (BYTE)((x & 1) ? idx : idx << 4);
            else row[x / 8] |= (BYTE)(idx << (7 - (x & 7)));
        }
        if (expect) expect[y * 8 + x] = c;
    }
    return sizeof *h + pb + st * 8;
}
static const int g_bpp[] = { 1, 4, 8, 24, 32 };
TEST(gap10_dibbrush_handle_shape_for_every_depth) {
    for (unsigned i = 0; i < 5; i++) {
        BYTE dib[sizeof(PBI) + 8 * 64]; RGBQUAD ex[64];
        build_dib(dib, g_bpp[i], ex);
        HBRUSH b = U(CreateDIBPatternBrushPt)(dib, DIB_RGB_COLORS);
        CHECKF(b != NULL, "%d bpp: CreateDIBPatternBrushPt", g_bpp[i]);
        if (!b) continue;
        G10_EQ((((ULONG_PTR)b >> 16) & 0x7f), 0x10);       /* brush handle type */
        G10_EQ(U(GetObjectType)(b), OBJ_BRUSH);
        LOGBRUSH lb; memset(&lb, 0xCC, sizeof lb);
        G10_EQ(U(GetObjectW)(b, sizeof lb, &lb), sizeof lb);
        t_note("%d bpp: lbStyle %u", g_bpp[i], lb.lbStyle);
        struct k_gdicell c; CHECK(k_gdi_cell(b, &c));
        CHECK(U(DeleteObject)(b));
        /* Windows 10 gdi32 keeps answering GetObjectType for a deleted handle, so death shows in the failing second delete and size query */
        CHECK(!U(DeleteObject)(b));
        G10_EQ(U(GetObjectW)(b, sizeof lb, &lb), 0);
    }
}
TEST(gap10_dibbrush_pattern_is_the_dib_for_every_depth) {
    for (unsigned i = 0; i < 5; i++) {
        BYTE dib[sizeof(PBI) + 8 * 64]; RGBQUAD ex[64];
        build_dib(dib, g_bpp[i], ex);
        HBRUSH b = U(CreateDIBPatternBrushPt)(dib, DIB_RGB_COLORS);
        if (!b) { CHECKF(0, "%d bpp: no brush", g_bpp[i]); continue; }
        BITMAPINFO bi; memset(&bi, 0, sizeof bi);
        bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = 8; bi.bmiHeader.biHeight = 8;
        bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
        BYTE *bits = NULL;
        HDC dc = g10_mem_dc();
        HBITMAP ds = U(CreateDIBSection)(dc, &bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
        if (!ds || !bits) { CHECK(0); U(DeleteObject)(b); g10_mem_dc_free(dc); continue; }
        HGDIOBJ ob = U(SelectObject)(dc, ds), obr = U(SelectObject)(dc, b);
        U(SetBrushOrgEx)(dc, 0, 0, NULL);
        CHECK(U(PatBlt)(dc, 0, 0, 8, 8, PATCOPY));
        U(GdiFlush)();
        int bad = 0;
        for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {
            const BYTE *px = bits + (7 - y) * 32 + x * 4;     /* 32-bit bottom-up scan lines are 32 bytes */
            if (px[0] != ex[y * 8 + x].rgbBlue || px[1] != ex[y * 8 + x].rgbGreen || px[2] != ex[y * 8 + x].rgbRed) bad++;
        }
        CHECKF(bad == 0, "%d bpp: %d of 64 pixels differ", g_bpp[i], bad);
        U(SelectObject)(dc, obr); U(SelectObject)(dc, ob);
        U(DeleteObject)(ds); U(DeleteObject)(b); g10_mem_dc_free(dc);
    }
}
TEST(gap10_dibbrush_kernel_copy_survives_caller_buffer_change) {
    BYTE dib[sizeof(PBI) + 8 * 64]; RGBQUAD ex[64];
    build_dib(dib, 24, ex);
    HBRUSH b = U(CreateDIBPatternBrushPt)(dib, DIB_RGB_COLORS);
    REQUIRE(b);
    memset(dib + sizeof(BITMAPINFOHEADER), 0xEE, 8 * stride(8, 24));   /* the kernel keeps its own bitmap */
    BITMAPINFO bi; memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = 8; bi.bmiHeader.biHeight = 8;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    BYTE *bits = NULL; HDC dc = g10_mem_dc();
    HBITMAP ds = U(CreateDIBSection)(dc, &bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    REQUIRE(ds && bits);
    HGDIOBJ ob = U(SelectObject)(dc, ds), obr = U(SelectObject)(dc, b);
    U(PatBlt)(dc, 0, 0, 8, 8, PATCOPY); U(GdiFlush)();
    int bad = 0;
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {
        const BYTE *px = bits + (7 - y) * 32 + x * 4;
        if (px[0] != ex[y * 8 + x].rgbBlue || px[2] != ex[y * 8 + x].rgbRed) bad++;
    }
    CHECKF(bad == 0, "%d pixels changed after the caller overwrote its DIB", bad);
    U(SelectObject)(dc, obr); U(SelectObject)(dc, ob); U(DeleteObject)(ds); U(DeleteObject)(b); g10_mem_dc_free(dc);
}
TEST(gap10_dibbrush_size_limit_returns_null) {
    REQUIRE(UN(NtGdiCreateDIBBrush));
    BYTE dib[sizeof(PBI) + 8 * 64]; build_dib(dib, 24, NULL);
    /* NtGdiCreateDIBBrush allocates a temp copy only for cb < 0x2710001; larger -> NULL before any read of the DIB */
    G10_EQ(UN(NtGdiCreateDIBBrush)(dib, DIB_RGB_COLORS, 0x2710001, FALSE, FALSE, dib), NULL);
    G10_EQ(UN(NtGdiCreateDIBBrush)(dib, DIB_RGB_COLORS, 0xFFFFFFFFu, FALSE, FALSE, dib), NULL);
}
TEST(gap10_dibbrush_null_packed_dib_in_wrapper) {
    G10_EQ(U(CreateDIBPatternBrushPt)(NULL, DIB_RGB_COLORS), NULL);   /* gdi32full returns NULL without a syscall */
}
TEST(gap10_dibbrush_corrupt_header_variants) {
    struct { const char *n; int field; LONG v; } t[] = {
        { "biSize 0", 0, 0 }, { "biPlanes 0 ok?", 1, 0 }, { "biBitCount 3", 2, 3 }, { "biCompression 9", 3, 9 },
    };
    for (unsigned i = 0; i < 4; i++) {
        BYTE dib[sizeof(PBI) + 8 * 64]; build_dib(dib, 24, NULL);
        BITMAPINFOHEADER *h = (BITMAPINFOHEADER *)dib;
        switch (t[i].field) { case 0: h->biSize = t[i].v; break; case 1: h->biPlanes = (WORD)t[i].v; break; case 2: h->biBitCount = (WORD)t[i].v; break; default: h->biCompression = t[i].v; }
        HBRUSH b = U(CreateDIBPatternBrushPt)(dib, DIB_RGB_COLORS);
        t_note("%s -> %p", t[i].n, (void *)b);
        if (t[i].field == 0 || t[i].field == 2) CHECKF(b == NULL, "%s accepted", t[i].n);
        if (b) U(DeleteObject)(b);
    }
}
TEST(gap10_dibbrush_usage_values) {
    BYTE dib[sizeof(PBI) + 8 * 64]; build_dib(dib, 24, NULL);
    UINT use[] = { DIB_RGB_COLORS, DIB_PAL_COLORS, 2 + 1, 0x10, 0xFFFFFFFF };
    for (unsigned i = 0; i < 5; i++) {
        HBRUSH b = U(CreateDIBPatternBrushPt)(dib, use[i]);
        t_note("iUsage %u -> %p", use[i], (void *)b);
        if (i == 0) CHECK(b != NULL);
        if (b) U(DeleteObject)(b);
    }
}
TEST(gap10_dibbrush_many_brushes_do_not_leak_handles) {
    BYTE dib[sizeof(PBI) + 8 * 64]; build_dib(dib, 24, NULL);
    DWORD before = U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS);
    for (int i = 0; i < 200; i++) { HBRUSH b = U(CreateDIBPatternBrushPt)(dib, DIB_RGB_COLORS); if (!b) { CHECK(0); break; } U(DeleteObject)(b); }
    DWORD after = U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS);
    CHECKF(after <= before + 2, "GDI objects %lu -> %lu", before, after);
}
