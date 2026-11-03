/* Color mapping and ICM: GetNearestColor , COLORADJUSTMENT validation
 * (NtGdiSetColorAdjustment range checks), PALETTEINDEX/PALETTERGB on a memory DC holding an 8bpp DIB,
 * color spaces and ICM mode. */
#include "pb.h"

static HDC scr(void) { return U(CreateDCW) ? U(CreateDCW)(L"DISPLAY", NULL, NULL, NULL) : NULL; }

TEST(colors_nearest_color_nonpalette_display) {
    PB_GDI(); REQUIRE(U(GetNearestColor));
    if (pb_screen_has_palette()) { t_skip("palette-managed display"); return; }
    HDC dc = scr();
    REQUIRE(dc);
    static const COLORREF cs[] = { 0, RGB(255, 255, 255), RGB(1, 2, 3), RGB(255, 0, 128), RGB(17, 99, 201), RGB(254, 253, 252) };
    /* bit 24 clear and the device palette has no entries -> `cr & 0xffffff` */
    for (size_t i = 0; i < sizeof cs / sizeof cs[0]; i++) CHECKF(U(GetNearestColor)(dc, cs[i]) == cs[i], "0x%lx -> 0x%lx", cs[i], U(GetNearestColor)(dc, cs[i]));
    CHECK(U(GetNearestColor)(dc, 0x02123456) == 0x00123456);
    CHECK(U(GetNearestColor)(dc, 0xFE654321) == 0x00654321);
    CHECK(U(GetNearestColor)(dc, 0x00000000) == 0);
    U(DeleteDC)(dc);
}
TEST(colors_nearest_color_invalid_dc) {
    PB_GDI(); REQUIRE(U(GetNearestColor));
    CHECK(U(GetNearestColor)(NULL, RGB(1, 2, 3)) == CLR_INVALID);
    CHECK(U(GetNearestColor)((HDC)(ULONG_PTR)0x1234, RGB(1, 2, 3)) == CLR_INVALID);
    if (UN(NtGdiGetNearestColor)) CHECK(UN(NtGdiGetNearestColor)(NULL, 5) == 0xFFFFFFFF);
}
TEST(colors_nearest_color_nt_matches_user) {
    REQUIRE(UN(NtGdiGetNearestColor)); PB_GDI();
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    static const COLORREF cs[] = { 0, RGB(255, 255, 255), RGB(10, 10, 10), RGB(240, 240, 240), RGB(100, 150, 200) };
    for (size_t i = 0; i < sizeof cs / sizeof cs[0]; i++) CHECK(UN(NtGdiGetNearestColor)(dc, cs[i]) == U(GetNearestColor)(dc, cs[i]));
    U(DeleteDC)(dc);
}
TEST(colors_nearest_color_monochrome_memdc) {
    PB_GDI(); REQUIRE(U(GetNearestColor));
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    /* the default 1x1 monochrome bitmap carries a two entry black/white palette: extremes map to themselves */
    CHECK(U(GetNearestColor)(dc, RGB(0, 0, 0)) == RGB(0, 0, 0));
    CHECK(U(GetNearestColor)(dc, RGB(255, 255, 255)) == RGB(255, 255, 255));
    CHECK(U(GetNearestColor)(dc, RGB(5, 5, 5)) == RGB(0, 0, 0));
    CHECK(U(GetNearestColor)(dc, RGB(250, 250, 250)) == RGB(255, 255, 255));
    U(DeleteDC)(dc);
}

/* 8bpp DIB section with a known color table, selected into a memory DC */
struct dib8 { HDC dc; HBITMAP bmp, old; void *bits; };
static int dib8_open(struct dib8 *d, const RGBQUAD *tab, int n) {
    struct { BITMAPINFOHEADER h; RGBQUAD pal[256]; } bi;
    memset(&bi, 0, sizeof bi);
    bi.h.biSize = sizeof bi.h; bi.h.biWidth = 4; bi.h.biHeight = 4; bi.h.biPlanes = 1; bi.h.biBitCount = 8; bi.h.biClrUsed = 256;
    for (int i = 0; i < n; i++) bi.pal[i] = tab[i];
    memset(d, 0, sizeof *d);
    d->dc = U(CreateCompatibleDC)(NULL);
    if (!d->dc) return 0;
    d->bmp = U(CreateDIBSection)(d->dc, (BITMAPINFO *)&bi, DIB_RGB_COLORS, &d->bits, NULL, 0);
    if (!d->bmp) { U(DeleteDC)(d->dc); return 0; }
    d->old = U(SelectObject)(d->dc, d->bmp);
    return 1;
}
static void dib8_close(struct dib8 *d) { U(SelectObject)(d->dc, d->old); pb_del(d->bmp); U(DeleteDC)(d->dc); }
static const RGBQUAD tab5[5] = { {0,0,0,0}, {255,255,255,0}, {0,0,255,0}, {0,255,0,0}, {255,0,0,0} }; /* B,G,R order: black, white, red, green, blue */

TEST(colors_nearest_color_dib8_table) {
    PB_GDI(); REQUIRE(U(GetNearestColor)); REQUIRE(U(CreateDIBSection));
    struct dib8 d;
    if (!dib8_open(&d, tab5, 5)) { t_skip("8bpp DIB section unavailable"); return; }
    /* the surface palette is the DIB color table: a color maps to the nearest table entry */
    CHECK(U(GetNearestColor)(d.dc, RGB(250, 5, 5)) == RGB(255, 0, 0));
    CHECK(U(GetNearestColor)(d.dc, RGB(5, 250, 5)) == RGB(0, 255, 0));
    CHECK(U(GetNearestColor)(d.dc, RGB(5, 5, 250)) == RGB(0, 0, 255));
    CHECK(U(GetNearestColor)(d.dc, RGB(250, 250, 250)) == RGB(255, 255, 255));
    CHECK(U(GetNearestColor)(d.dc, RGB(3, 3, 3)) == RGB(0, 0, 0));
    CHECK(U(GetNearestColor)(d.dc, RGB(255, 0, 0)) == RGB(255, 0, 0));
    dib8_close(&d);
}
TEST(colors_paletteindex_on_dib8_with_logical_palette) {
    PB_GDI(); REQUIRE(U(GetNearestColor)); REQUIRE(U(CreateDIBSection)); REQUIRE(U(SelectPalette));
    struct dib8 d;
    if (!dib8_open(&d, tab5, 5)) { t_skip("8bpp DIB section unavailable"); return; }
    LOGPALETTE *lp = pb_mkpal(3, 0x300);
    REQUIRE(lp);
    /* logical palette entries are exact members of the DIB color table */
    lp->palPalEntry[0].peRed = 0;   lp->palPalEntry[0].peGreen = 255; lp->palPalEntry[0].peBlue = 0;
    lp->palPalEntry[1].peRed = 255; lp->palPalEntry[1].peGreen = 0;   lp->palPalEntry[1].peBlue = 0;
    lp->palPalEntry[2].peRed = 0;   lp->palPalEntry[2].peGreen = 0;   lp->palPalEntry[2].peBlue = 255;
    HPALETTE p = U(CreatePalette)(lp);
    REQUIRE(p);
    HPALETTE oldp = U(SelectPalette)(d.dc, p, FALSE);
    CHECK(oldp != NULL);
    COLORREF r0 = U(GetNearestColor)(d.dc, PALETTEINDEX(0));
    COLORREF r1 = U(GetNearestColor)(d.dc, PALETTEINDEX(1));
    COLORREF r2 = U(GetNearestColor)(d.dc, PALETTEINDEX(2));
    CHECKF(r0 == RGB(0, 255, 0), "PALETTEINDEX(0) -> 0x%lx", r0);
    CHECKF(r1 == RGB(255, 0, 0), "PALETTEINDEX(1) -> 0x%lx", r1);
    CHECKF(r2 == RGB(0, 0, 255), "PALETTEINDEX(2) -> 0x%lx", r2);
    COLORREF q = U(GetNearestColor)(d.dc, PALETTERGB(0, 250, 5));
    CHECKF(q == RGB(0, 255, 0), "PALETTERGB nearest -> 0x%lx", q);
    t_note("PALETTEINDEX(7) (out of range) -> 0x%lx", U(GetNearestColor)(d.dc, PALETTEINDEX(7)));
    if (oldp) U(SelectPalette)(d.dc, oldp, FALSE);
    pb_del(p); free(lp);
    dib8_close(&d);
}
TEST(colors_nearest_index_palette_matches_dib_table) {
    PB_GDI(); REQUIRE(U(GetNearestPaletteIndex)); REQUIRE(U(GetDIBColorTable)); REQUIRE(U(CreateDIBSection));
    struct dib8 d;
    if (!dib8_open(&d, tab5, 5)) { t_skip("8bpp DIB section unavailable"); return; }
    RGBQUAD got[5];
    CHECK(U(GetDIBColorTable)(d.dc, 0, 5, got) == 5);
    CHECK(!memcmp(got, tab5, sizeof tab5));
    dib8_close(&d);
}
TEST(colors_dib8_gdi_state_pointer) {
    PB_GDI(); PB_TABLE(); REQUIRE(U(CreateDIBSection));
    struct dib8 d;
    if (!dib8_open(&d, tab5, 5)) { t_skip("8bpp DIB section unavailable"); return; }
    /* the DIB's pixel memory is a user-mode section view: writable local memory the kernel also reads */
    unsigned char *px = (unsigned char *)d.bits;
    CHECK(px != NULL);
    if (px) {
        memset(px, 2, 16);
        COLORREF c = U(GetPixel)(d.dc, 0, 0);
        CHECKF(c == RGB(255, 0, 0), "GetPixel of index 2 -> 0x%lx", c);
        px[0] = 4;
        c = U(GetPixel)(d.dc, 0, 3); /* bottom-up DIB: row 0 of the memory is the bottom scanline */
        CHECKF(c == RGB(0, 0, 255), "GetPixel of index 4 -> 0x%lx", c);
    }
    struct pb_cell cell;
    CHECK(pb_cell_of(d.bmp, &cell) && cell.objt == 5);
    dib8_close(&d);
}

/* ---- color adjustment ---- */
struct ca { WORD size, flags, illum, rg, gg, bg, rb, rw; SHORT contrast, bright, color, tint; };
typedef char ca_size[(sizeof(struct ca) == 24) ? 1 : -1];
static struct ca ca_def(void) { struct ca c = { 24, 0, 0, 10000, 10000, 10000, 0, 10000, 0, 0, 0, 0 }; return c; }

TEST(colors_adjustment_get_default) {
    PB_GDI(); REQUIRE(U(GetColorAdjustment));
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    struct ca c; memset(&c, 0xCC, sizeof c);
    BOOL r = U(GetColorAdjustment)(dc, (COLORADJUSTMENT *)&c);
    t_note("GetColorAdjustment -> %d size %u illum %u gammas %u/%u/%u black %u white %u", r, c.size, c.illum, c.rg, c.gg, c.bg, c.rb, c.rw);
    CHECK(r);
    CHECK(c.size == 24);
    U(DeleteDC)(dc);
}
TEST(colors_adjustment_set_valid_roundtrip) {
    PB_GDI(); REQUIRE(U(SetColorAdjustment)); REQUIRE(U(GetColorAdjustment));
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    struct ca in = { 24, 0, 3, 5000, 6000, 7000, 100, 9000, 10, -20, 30, -40 }, out;
    CHECK(U(SetColorAdjustment)(dc, (COLORADJUSTMENT *)&in));
    memset(&out, 0xCC, sizeof out);
    CHECK(U(GetColorAdjustment)(dc, (COLORADJUSTMENT *)&out));
    if (memcmp(&in, &out, sizeof in)) t_note("color adjustment round trip differs");
    CHECK(out.size == 24);
    struct ca d = ca_def();
    U(SetColorAdjustment)(dc, (COLORADJUSTMENT *)&d);
    U(DeleteDC)(dc);
}
TEST(colors_adjustment_valid_boundaries) {
    PB_GDI(); REQUIRE(U(SetColorAdjustment));
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    /* NtGdiSetColorAdjustment ranges: illuminant <= 8, gammas 1344..65000, black 0..4000, white 6000..10000, +-100 */
    struct ca v[] = {
        { 24, 0, 8, 1344, 1344, 1344, 0, 6000, -100, -100, -100, -100 },
        { 24, 0, 0, 65000, 65000, 65000, 4000, 10000, 100, 100, 100, 100 },
        { 24, 0, 0, 10000, 10000, 10000, 0, 10000, 0, 0, 0, 0 },
        { 24, 0xFFFF, 4, 2500, 2500, 2500, 2000, 8000, 1, -1, 50, -50 },
    };
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) CHECKF(U(SetColorAdjustment)(dc, (COLORADJUSTMENT *)&v[i]), "vector %zu", i);
    struct ca d = ca_def();
    U(SetColorAdjustment)(dc, (COLORADJUSTMENT *)&d);
    U(DeleteDC)(dc);
}
TEST(colors_adjustment_invalid_boundaries) {
    PB_GDI(); REQUIRE(U(SetColorAdjustment));
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    struct ca base = ca_def();
    struct { const char *why; struct ca c; } v[16];
    int n = 0;
#define BAD(w, field, val) do { v[n].why = w; v[n].c = base; v[n].c.field = (val); n++; } while (0)
    BAD("size 0", size, 0); BAD("size 23", size, 23); BAD("size 25", size, 25); BAD("size 48", size, 48);
    BAD("illuminant 9", illum, 9); BAD("illuminant 0xffff", illum, 0xFFFF);
    BAD("red gamma 1343", rg, 1343); BAD("red gamma 65001", rg, 65001);
    BAD("green gamma 1343", gg, 1343); BAD("blue gamma 65001", bg, 65001);
    BAD("ref black 4001", rb, 4001); BAD("ref white 5999", rw, 5999);
    BAD("ref white 10001", rw, 10001);
    BAD("contrast 101", contrast, 101); BAD("brightness -101", bright, -101);
    BAD("colorfulness 101", color, 101);
    for (int i = 0; i < n; i++) CHECKF(!U(SetColorAdjustment)(dc, (COLORADJUSTMENT *)&v[i].c), "%s accepted", v[i].why);
    struct ca t = base; t.tint = -101;
    CHECK(!U(SetColorAdjustment)(dc, (COLORADJUSTMENT *)&t));
    t = base; t.tint = 101;
    CHECK(!U(SetColorAdjustment)(dc, (COLORADJUSTMENT *)&t));
    U(DeleteDC)(dc);
}
TEST(colors_adjustment_invalid_does_not_change) {
    PB_GDI(); REQUIRE(U(SetColorAdjustment)); REQUIRE(U(GetColorAdjustment));
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    struct ca good = { 24, 0, 1, 4000, 4000, 4000, 10, 9000, 5, 6, 7, 8 }, bad = good, got;
    CHECK(U(SetColorAdjustment)(dc, (COLORADJUSTMENT *)&good));
    bad.rw = 100;
    CHECK(!U(SetColorAdjustment)(dc, (COLORADJUSTMENT *)&bad));
    CHECK(U(GetColorAdjustment)(dc, (COLORADJUSTMENT *)&got));
    CHECK(got.rw != 100);
    struct ca d = ca_def();
    U(SetColorAdjustment)(dc, (COLORADJUSTMENT *)&d);
    U(DeleteDC)(dc);
}
TEST(colors_adjustment_invalid_dc) {
    PB_GDI(); REQUIRE(U(SetColorAdjustment));
    struct ca d = ca_def();
    SetLastError(0);
    CHECK(!U(SetColorAdjustment)(NULL, (COLORADJUSTMENT *)&d));
    EXPECT_ERR(ERROR_INVALID_HANDLE); /* gdi32full!SetColorAdjustment: pldcGet == 0 -> GdiSetLastError(6) */
}
TEST(colors_adjustment_nt_set_direct) {
    REQUIRE(UN(NtGdiSetColorAdjustment)); PB_GDI();
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    struct ca ok = ca_def(), bad = ca_def();
    CHECK(UN(NtGdiSetColorAdjustment)(dc, &ok));
    bad.size = 8;
    CHECK(!UN(NtGdiSetColorAdjustment)(dc, &bad));
    bad = ok; bad.illum = 9;
    CHECK(!UN(NtGdiSetColorAdjustment)(dc, &bad));
    CHECK(!UN(NtGdiSetColorAdjustment)(NULL, &ok));
    U(DeleteDC)(dc);
}

/* ---- ICM / color spaces ---- */
static LOGCOLORSPACEW mklcs(void) {
    LOGCOLORSPACEW l; memset(&l, 0, sizeof l);
    l.lcsSignature = 0x50534F43; l.lcsVersion = 0x400; l.lcsSize = sizeof l;
    l.lcsCSType = LCS_CALIBRATED_RGB; l.lcsIntent = LCS_GM_IMAGES;
    l.lcsGammaRed = l.lcsGammaGreen = l.lcsGammaBlue = 0x10000;
    return l;
}
TEST(colors_colorspace_create_roundtrip) {
    PB_GDI(); REQUIRE(U(CreateColorSpaceW)); REQUIRE(U(DeleteColorSpace));
    LOGCOLORSPACEW l = mklcs(), o;
    HCOLORSPACE cs = U(CreateColorSpaceW)(&l);
    CHECKF(cs != NULL, "err %lu", GetLastError());
    if (!cs) return;
    CHECK(PB_T7(cs) == 9);
    CHECK(U(GetObjectType)(cs) == OBJ_COLORSPACE);
    memset(&o, 0, sizeof o);
    int r = pb_getobj(cs, sizeof o, &o);
    CHECKF(r == 0x24c, "GetObject -> %d", r); /* gdi32full!GetObjectW: type 9 returns 0x24c after GetLogColorSpaceW */
    CHECK(o.lcsSignature == 0x50534F43);
    CHECK(o.lcsIntent == LCS_GM_IMAGES);
    CHECK(o.lcsCSType == LCS_CALIBRATED_RGB);
    CHECK(U(DeleteColorSpace)(cs));
    CHECK(!U(DeleteColorSpace)(cs));
}
TEST(colors_colorspace_create_invalid) {
    PB_GDI(); REQUIRE(U(CreateColorSpaceW));
    HCOLORSPACE cs;
    SetLastError(0);
    CHECK(U(CreateColorSpaceW)(NULL) == NULL);
    EXPECT_ERR(ERROR_INVALID_PARAMETER); /* CreateColorSpaceInternalW: NULL -> 0x57 */
    struct { const char *why; int off; DWORD val; } v[] = {
        { "signature", 0, 0x12345678 }, { "version 0x401", 4, 0x401 }, { "version 0", 4, 0 },
        { "size 0x24b", 8, 0x24b }, { "size 0x250", 8, 0x250 }, { "size 4", 8, 4 },
        { "intent 0", 0x10, 0 }, { "intent 3", 0x10, 3 }, { "intent 5", 0x10, 5 }, { "intent 6", 0x10, 6 },
        { "intent 7", 0x10, 7 }, { "intent 9", 0x10, 9 }, { "intent 16", 0x10, 16 }, { "intent 0xFFFFFFFF", 0x10, 0xFFFFFFFF },
    };
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        LOGCOLORSPACEW l = mklcs();
        *(DWORD *)((char *)&l + v[i].off) = v[i].val;
        SetLastError(0);
        cs = U(CreateColorSpaceW)(&l);
        CHECKF(cs == NULL, "%s accepted", v[i].why); /* signature 'PSOC', version 0x400, size 0x24c, intent in {1,2,4,8} */
        EXPECT_ERR(ERROR_INVALID_COLORSPACE);
        if (cs) U(DeleteColorSpace)(cs);
    }
}
TEST(colors_colorspace_valid_intents) {
    PB_GDI(); REQUIRE(U(CreateColorSpaceW)); REQUIRE(U(DeleteColorSpace));
    static const DWORD intents[] = { LCS_GM_BUSINESS, LCS_GM_GRAPHICS, LCS_GM_IMAGES, LCS_GM_ABS_COLORIMETRIC };
    for (size_t i = 0; i < 4; i++) {
        LOGCOLORSPACEW l = mklcs(), o;
        l.lcsIntent = intents[i];
        HCOLORSPACE cs = U(CreateColorSpaceW)(&l);
        CHECKF(cs != NULL, "intent %lu", intents[i]);
        if (!cs) continue;
        memset(&o, 0, sizeof o);
        CHECK(pb_getobj(cs, sizeof o, &o) == 0x24c);
        CHECK(o.lcsIntent == intents[i]);
        U(DeleteColorSpace)(cs);
    }
}
TEST(colors_colorspace_getobject_small_buffer) {
    PB_GDI(); REQUIRE(U(CreateColorSpaceW)); REQUIRE(U(DeleteColorSpace));
    LOGCOLORSPACEW l = mklcs();
    HCOLORSPACE cs = U(CreateColorSpaceW)(&l);
    if (!cs) { t_skip("no color space"); return; }
    unsigned char buf[0x300];
    /* gdi32full!GetLogColorSpaceW: buffer NULL or nSize < 0x24c -> ERROR_INSUFFICIENT_BUFFER (0x7a), result 0 */
    SetLastError(0);
    CHECK(pb_getobj(cs, 0x24b, buf) == 0);
    EXPECT_ERR(ERROR_INSUFFICIENT_BUFFER);
    SetLastError(0);
    CHECK(pb_getobj(cs, 0x24c, NULL) == 0);
    EXPECT_ERR(ERROR_INSUFFICIENT_BUFFER);
    CHECK(pb_getobj(cs, 0x300, buf) == 0x24c);
    U(DeleteColorSpace)(cs);
}
TEST(colors_colorspace_delete_invalid) {
    PB_GDI(); REQUIRE(U(DeleteColorSpace));
    CHECK(!U(DeleteColorSpace)(NULL));
    CHECK(!U(DeleteColorSpace)((HCOLORSPACE)(ULONG_PTR)0x1234));
    HBRUSH b = U(CreateSolidBrush)(0);
    REQUIRE(b);
    CHECK(!U(DeleteColorSpace)((HCOLORSPACE)b));
    LOGBRUSH lb;
    CHECK(pb_getobj(b, sizeof lb, &lb) == 0x10); /* the brush must survive the wrong-type delete */
    pb_del(b);
}
TEST(colors_colorspace_cell_state) {
    PB_GDI(); PB_TABLE(); REQUIRE(U(CreateColorSpaceW));
    LOGCOLORSPACEW l = mklcs();
    HCOLORSPACE cs = U(CreateColorSpaceW)(&l);
    if (!cs) { t_skip("color space not created"); return; }
    struct pb_cell c;
    CHECK(pb_cell_of(cs, &c));
    CHECK(c.objt == 9);
    CHECK((c.owner & ~1u) == PB_PID());
    U(DeleteColorSpace)(cs);
}
TEST(colors_icm_mode_query) {
    PB_GDI(); REQUIRE(U(SetICMMode));
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    int m = U(SetICMMode)(dc, ICM_QUERY);
    t_note("ICM_QUERY on a fresh memory DC -> %d", m);
    CHECK(m != 0);
    t_note("SetICMMode(dc, 0) -> %d, (dc, 5) -> %d, (NULL, QUERY) -> %d", U(SetICMMode)(dc, 0), U(SetICMMode)(dc, 5), U(SetICMMode)(NULL, ICM_QUERY));
    U(DeleteDC)(dc);
}
TEST(colors_icm_profile_errors) {
    PB_GDI(); REQUIRE(U(GetICMProfileW));
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    DWORD n = 0;
    /* GetICMProfileW: pBufSize == NULL or a DC that fails the GdiGetEntry checks -> ERROR_INVALID_PARAMETER */
    SetLastError(0);
    CHECK(!U(GetICMProfileW)(dc, NULL, NULL));
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SetLastError(0);
    CHECK(!U(GetICMProfileW)(NULL, &n, NULL));
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    HBRUSH b = U(CreateSolidBrush)(0);
    if (b) { SetLastError(0); CHECK(!U(GetICMProfileW)((HDC)b, &n, NULL)); EXPECT_ERR(ERROR_INVALID_PARAMETER); pb_del(b); }
    SetLastError(0);
    BOOL r = U(GetICMProfileW)(dc, &n, NULL);
    t_note("GetICMProfileW(memory DC, size query) -> %d size %lu err %lu", r, n, GetLastError());
    U(DeleteDC)(dc);
}
TEST(colors_color_correct_palette_invalid) {
    PB_GDI(); REQUIRE(U(ColorCorrectPalette));
    if (t_is_wine()) { t_skip("Wine's ColorCorrectPalette dereferences its arguments"); return; }
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    HPALETTE p = pb_mkpal(4, 0x300) ? U(CreatePalette)(pb_mkpal(4, 0x300)) : NULL;
    REQUIRE(p);
    /* gdi32full!ColorCorrectPalette: hdc/hPal NULL, num outside 1..0x10000, deFirst > 0xffff or deFirst + num > 0x10000 -> 0x57 */
    struct { HDC dc; HPALETTE p; DWORD first, num; const char *why; } v[] = {
        { NULL, p, 0, 4, "null dc" }, { dc, NULL, 0, 4, "null palette" }, { dc, p, 0, 0, "num 0" },
        { dc, p, 0, 0x10001, "num 0x10001" }, { dc, p, 0x10000, 1, "first 0x10000" },
        { dc, p, 0xFFFF, 2, "first + num overflow" }, { dc, p, 0xFFFFFFFF, 1, "first -1" }, { dc, p, 0, 0xFFFFFFFF, "num -1" },
    };
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        SetLastError(0);
        CHECKF(!U(ColorCorrectPalette)(v[i].dc, v[i].p, v[i].first, v[i].num), "%s", v[i].why);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
    pb_del(p);
    U(DeleteDC)(dc);
}
TEST(colors_colorref_macros) {
    /* COLORREF layout facts the kernel code above depends on */
    CHECK(PALETTEINDEX(5) == 0x01000005);
    CHECK(PALETTERGB(1, 2, 3) == 0x02030201);
    CHECK(RGB(1, 2, 3) == 0x00030201);
    CHECK((PALETTEINDEX(5) >> 24 & 1) == 1);
    CHECK((PALETTERGB(1, 2, 3) >> 24 & 1) == 0);
}
TEST(colors_screen_bpp_note) {
    PB_GDI(); REQUIRE(U(GetDeviceCaps)); REQUIRE(U(CreateDCW));
    HDC dc = scr();
    REQUIRE(dc);
    t_note("BITSPIXEL %d PLANES %d NUMCOLORS %d SIZEPALETTE %d NUMRESERVED %d COLORRES %d RASTERCAPS 0x%x",
           U(GetDeviceCaps)(dc, BITSPIXEL), U(GetDeviceCaps)(dc, PLANES), U(GetDeviceCaps)(dc, NUMCOLORS), U(GetDeviceCaps)(dc, SIZEPALETTE),
           U(GetDeviceCaps)(dc, NUMRESERVED), U(GetDeviceCaps)(dc, COLORRES), U(GetDeviceCaps)(dc, RASTERCAPS));
    int bpp = U(GetDeviceCaps)(dc, BITSPIXEL) * U(GetDeviceCaps)(dc, PLANES);
    if (bpp > 8) CHECK((U(GetDeviceCaps)(dc, RASTERCAPS) & RC_PALETTE) == 0); /* RC_PALETTE is a palette-managed device property */
    U(DeleteDC)(dc);
}
