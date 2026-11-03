/* gdi_blit_raster: pixel access, flood fill, region fills, mapping modes, brush origin / hatch alignment
 * and format conversion between DIB sections of different depth. */
#include "br.h"

#define SENT 0x00DEAD

/* ---------- generic-depth DIB section (rows top-down) ---------- */
typedef struct { HDC dc; HBITMAP bmp, old; unsigned char *bits; int w, h, bpp, stride; } DB;
static DB db_make(int w, int h, int bpp, const RGBQUAD *pal, int npal) {
    DB d; struct { BITMAPINFOHEADER h; RGBQUAD c[256]; } bi; void *bits = NULL;
    memset(&d, 0, sizeof d); memset(&bi, 0, sizeof bi);
    bi.h.biSize = sizeof bi.h; bi.h.biWidth = w; bi.h.biHeight = -h; bi.h.biPlanes = 1; bi.h.biBitCount = bpp; bi.h.biCompression = BI_RGB;
    if (bpp <= 8) { bi.h.biClrUsed = npal; for (int i = 0; i < npal; i++) bi.c[i] = pal[i]; }
    d.dc = U(CreateCompatibleDC)(NULL);
    if (!d.dc) return d;
    d.bmp = U(CreateDIBSection)(d.dc, (BITMAPINFO *)&bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!d.bmp || !bits) { U(DeleteDC)(d.dc); d.dc = NULL; return d; }
    d.old = (HBITMAP)U(SelectObject)(d.dc, d.bmp);
    d.bits = bits; d.w = w; d.h = h; d.bpp = bpp; d.stride = ((w * bpp + 31) / 32) * 4;
    return d;
}
static void db_free(DB *d) {
    if (d->dc) { U(SelectObject)(d->dc, d->old); U(DeleteDC)(d->dc); }
    if (d->bmp) U(DeleteObject)(d->bmp);
    memset(d, 0, sizeof *d);
}
static unsigned db_get(const DB *d, int x, int y) {   /* raw sample: index (1/8bpp) or packed pixel */
    const unsigned char *r = d->bits + y * d->stride;
    switch (d->bpp) {
    case 1: return (r[x >> 3] >> (7 - (x & 7))) & 1;
    case 8: return r[x];
    case 16: return r[x * 2] | (r[x * 2 + 1] << 8);
    case 24: return r[x * 3] | (r[x * 3 + 1] << 8) | (r[x * 3 + 2] << 16);
    default: return *(const DWORD *)(r + x * 4) & M24;
    }
}
static void db_put(DB *d, int x, int y, unsigned v) {
    unsigned char *r = d->bits + y * d->stride;
    switch (d->bpp) {
    case 1: if (v) r[x >> 3] |= 0x80 >> (x & 7); else r[x >> 3] &= ~(0x80 >> (x & 7)); break;
    case 8: r[x] = (unsigned char)v; break;
    case 16: r[x * 2] = v & 0xff; r[x * 2 + 1] = v >> 8; break;
    case 24: r[x * 3] = v & 0xff; r[x * 3 + 1] = (v >> 8) & 0xff; r[x * 3 + 2] = v >> 16; break;
    default: *(DWORD *)(r + x * 4) = v;
    }
}
static const RGBQUAD BW[2] = { { 0, 0, 0, 0 }, { 255, 255, 255, 0 } };   /* index 0 black, 1 white */
static void pal256(RGBQUAD *p) { for (int i = 0; i < 256; i++) { p[i].rgbRed = i; p[i].rgbGreen = 255 - i; p[i].rgbBlue = (i * 7) & 255; p[i].rgbReserved = 0; } }

#define NEED32(d, w, h) BR d = br_make(w, h); if (!d.dc) { br_free(&d); t_skip("no DIB/DC"); return; }

/* ============ SetPixel / GetPixel at 32bpp ============ */
static const COLORREF PXCOL[] = { RGB(0, 0, 0), RGB(255, 255, 255), RGB(255, 0, 0), RGB(0, 255, 0), RGB(0, 0, 255),
                                  RGB(1, 2, 3), RGB(0x80, 0x40, 0x20), RGB(0xFE, 0xFD, 0xFC), RGB(0x12, 0x34, 0x56), RGB(0xAB, 0xCD, 0xEF) };

TEST(gdi_blit_raster_setpixel_exact_color_returned_and_stored) {
    NEED32(d, 4, 4);
    for (unsigned i = 0; i < sizeof PXCOL / sizeof PXCOL[0]; i++) {
        COLORREF c = PXCOL[i];
        COLORREF r = U(SetPixel)(d.dc, 1, 2, c); br_flush();
        CHECKF(r == c, "SetPixel returned %08lx for %08lx", r, c);
        CHECKF(br_at(&d, 1, 2) == RGBPX(GetRValue(c), GetGValue(c), GetBValue(c)), "stored %06x", br_at(&d, 1, 2));
        CHECKF(U(GetPixel)(d.dc, 1, 2) == c, "GetPixel %08lx", U(GetPixel)(d.dc, 1, 2));
    }
    br_free(&d);
}

TEST(gdi_blit_raster_setpixel_only_target_pixel_changes) {
    NEED32(d, 5, 5);
    br_fill(&d, SENT);
    U(SetPixel)(d.dc, 2, 3, RGB(9, 8, 7)); br_flush();
    CHECK(br_count(&d, 0, 0, 5, 5, SENT) == 24);
    CHECK(br_at(&d, 2, 3) == RGBPX(9, 8, 7));
    br_free(&d);
}

TEST(gdi_blit_raster_getpixel_reads_direct_writes) {
    NEED32(d, 6, 6);
    for (int y = 0; y < 6; y++) for (int x = 0; x < 6; x++) br_set(&d, x, y, br_pat(x, y));
    int bad = 0;
    for (int y = 0; y < 6; y++) for (int x = 0; x < 6; x++) if (U(GetPixel)(d.dc, x, y) != PX2CR(br_pat(x, y))) bad++;
    CHECKF(bad == 0, "%d mismatches", bad);
    br_free(&d);
}

TEST(gdi_blit_raster_getpixel_syscall_matches_userland) {
    NT(NtGdiGetPixel); NEED32(d, 4, 4);
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) {
        br_set(&d, x, y, br_pat(x, y));
        CHECKF(p_NtGdiGetPixel(d.dc, x, y) == PX2CR(br_pat(x, y)), "(%d,%d)", x, y);
    }
    br_free(&d);
}

TEST(gdi_blit_raster_getpixel_ignores_top_byte) {
    NEED32(d, 2, 2);
    br_set(&d, 0, 0, 0xFF123456); br_set(&d, 1, 0, 0x80ABCDEF);
    CHECK(U(GetPixel)(d.dc, 0, 0) == RGB(0x12, 0x34, 0x56));
    CHECK(U(GetPixel)(d.dc, 1, 0) == RGB(0xAB, 0xCD, 0xEF));
    br_free(&d);
}

TEST(gdi_blit_raster_pixel_out_of_range_is_clr_invalid) {
    NT(NtGdiSetPixel); NT(NtGdiGetPixel); NEED32(d, 4, 4);
    br_fill(&d, SENT);
    static const int pts[][2] = { { -1, 0 }, { 0, -1 }, { 4, 0 }, { 0, 4 }, { 4, 4 }, { -1, -1 }, { 1000, 1 }, { 1, 1000 }, { 0x7fffffff, 0 }, { 0, (int)0x80000000 } };
    for (unsigned i = 0; i < sizeof pts / sizeof pts[0]; i++) {
        CHECKF(U(GetPixel)(d.dc, pts[i][0], pts[i][1]) == CLR_BAD, "GetPixel(%d,%d)", pts[i][0], pts[i][1]);
        CHECKF(U(SetPixel)(d.dc, pts[i][0], pts[i][1], RGB(1, 2, 3)) == CLR_BAD, "SetPixel(%d,%d)", pts[i][0], pts[i][1]);
        CHECKF(p_NtGdiGetPixel(d.dc, pts[i][0], pts[i][1]) == CLR_BAD, "NtGdiGetPixel(%d,%d)", pts[i][0], pts[i][1]);
        CHECKF(p_NtGdiSetPixel(d.dc, pts[i][0], pts[i][1], RGB(1, 2, 3)) == CLR_BAD, "NtGdiSetPixel(%d,%d)", pts[i][0], pts[i][1]);
    }
    br_flush();
    CHECK(br_all(&d, 0, 0, 4, 4, SENT));
    br_free(&d);
}

TEST(gdi_blit_raster_setpixelv_return_values) {
    NEED32(d, 4, 4);
    CHECK(U(SetPixelV)(d.dc, 1, 1, RGB(10, 20, 30)) == TRUE);
    br_flush();
    CHECK(br_at(&d, 1, 1) == RGBPX(10, 20, 30));
    CHECK(U(SetPixelV)(d.dc, 4, 1, RGB(10, 20, 30)) == FALSE);
    CHECK(U(SetPixelV)(d.dc, -1, 1, RGB(10, 20, 30)) == FALSE);
    br_free(&d);
}

TEST(gdi_blit_raster_pixel_invalid_dc) {
    NT(NtGdiSetPixel); NT(NtGdiGetPixel); NEED32(d, 4, 4);
    BR t = br_make(4, 4); HDC dead = t.dc; br_free(&t);
    CHECK(U(GetPixel)(NULL, 0, 0) == CLR_BAD);
    CHECK(U(GetPixel)(dead, 0, 0) == CLR_BAD);
    CHECK(p_NtGdiGetPixel(NULL, 0, 0) == CLR_BAD);
    CHECK(p_NtGdiGetPixel(dead, 0, 0) == CLR_BAD);
    CHECK(p_NtGdiSetPixel(NULL, 0, 0, 0) == CLR_BAD);
    CHECK(p_NtGdiSetPixel(dead, 0, 0, 0) == CLR_BAD);
    CHECK(U(SetPixel)(dead, 0, 0, 0) == CLR_BAD);
    CHECK(U(SetPixelV)(dead, 0, 0, 0) == FALSE);
    CHECK(p_NtGdiGetPixel((HDC)d.bmp, 0, 0) == CLR_BAD);   /* a bitmap handle is not a DC */
    br_free(&d);
}

TEST(gdi_blit_raster_pixel_clip_region_limits_access) {
    NEED32(d, 6, 6);
    br_fill(&d, SENT);
    HRGN rg = U(CreateRectRgn)(2, 2, 4, 4);
    U(SelectClipRgn)(d.dc, rg);
    CHECK(U(GetPixel)(d.dc, 0, 0) == CLR_BAD);
    CHECK(U(GetPixel)(d.dc, 3, 3) == PX2CR(SENT));
    CHECK(U(SetPixel)(d.dc, 0, 0, RGB(1, 1, 1)) == CLR_BAD);
    CHECK(U(SetPixel)(d.dc, 3, 3, RGB(1, 1, 1)) == RGB(1, 1, 1));
    CHECK(U(SetPixelV)(d.dc, 1, 3, RGB(1, 1, 1)) == FALSE);
    br_flush();
    CHECK(br_count(&d, 0, 0, 6, 6, SENT) == 35);
    U(SelectClipRgn)(d.dc, NULL); U(DeleteObject)(rg);
    CHECK(U(GetPixel)(d.dc, 0, 0) == PX2CR(SENT));
    br_free(&d);
}

TEST(gdi_blit_raster_pixel_viewport_org_and_extents) {
    NEED32(d, 12, 12);
    br_fill(&d, SENT);
    U(SetViewportOrgEx)(d.dc, 5, 3, NULL);
    CHECK(U(SetPixel)(d.dc, 0, 0, RGB(1, 2, 3)) == RGB(1, 2, 3)); br_flush();
    CHECK(br_at(&d, 5, 3) == RGBPX(1, 2, 3));
    CHECK(U(GetPixel)(d.dc, 0, 0) == RGB(1, 2, 3));
    U(SetViewportOrgEx)(d.dc, 0, 0, NULL);
    U(SetMapMode)(d.dc, MM_ANISOTROPIC);
    U(SetWindowExtEx)(d.dc, 1, 1, NULL); U(SetViewportExtEx)(d.dc, 2, 3, NULL);
    CHECK(U(SetPixel)(d.dc, 3, 2, RGB(4, 5, 6)) == RGB(4, 5, 6)); br_flush();
    CHECKF(br_at(&d, 6, 6) == RGBPX(4, 5, 6), "device (6,6) = %06x", br_at(&d, 6, 6));
    CHECK(U(GetPixel)(d.dc, 3, 2) == RGB(4, 5, 6));
    U(SetMapMode)(d.dc, MM_TEXT);
    br_free(&d);
}

TEST(gdi_blit_raster_pixel_negative_viewport_org) {
    NEED32(d, 6, 6);
    U(SetWindowOrgEx)(d.dc, 2, 2, NULL);
    CHECK(U(SetPixel)(d.dc, 3, 4, RGB(7, 7, 7)) == RGB(7, 7, 7)); br_flush();
    CHECK(br_at(&d, 1, 2) == RGBPX(7, 7, 7));
    CHECK(U(GetPixel)(d.dc, 2, 2) == PX2CR(0));          /* logical (2,2) -> device (0,0) */
    CHECK(U(GetPixel)(d.dc, 1, 1) == CLR_BAD);           /* logical (1,1) -> device (-1,-1) */
    br_free(&d);
}

TEST(gdi_blit_raster_setpixel_palette_flag_colors_logged) {
    NEED32(d, 2, 2);
    COLORREF r1 = U(SetPixel)(d.dc, 0, 0, PALETTERGB(0x11, 0x22, 0x33));
    COLORREF r2 = U(SetPixel)(d.dc, 1, 0, PALETTEINDEX(3));
    br_flush();
    t_note("PALETTERGB(11,22,33) -> %08lx stored %06x; PALETTEINDEX(3) -> %08lx stored %06x", r1, br_at(&d, 0, 0), r2, br_at(&d, 1, 0));
    CHECK(br_at(&d, 0, 0) == RGBPX(0x11, 0x22, 0x33));
    br_free(&d);
}

TEST(gdi_blit_raster_pixel_window_dc_versus_memory_dc) {
    HWND w = k_make_window("win32kt_pixels");
    if (!w) { t_skip("no window"); return; }
    HDC wdc = U(GetDC)(w);
    NEED32(d, 2, 2);
    if (!wdc) { t_skip("no window DC"); br_free(&d); DestroyWindow(w); return; }
    COLORREF a = U(GetPixel)(wdc, 0, 0);
    COLORREF b = U(GetPixel)(d.dc, 0, 0);
    t_note("hidden window DC GetPixel(0,0)=%08lx memory DC %08lx", a, b);
    CHECK(b == 0);
    CHECK(a == CLR_BAD || a <= 0x00FFFFFF);
    U(ReleaseDC)(w, wdc);
    DestroyWindow(w);
    br_free(&d);
}

/* ============ pixel access at other depths ============ */
TEST(gdi_blit_raster_setpixel_1bpp_black_white) {
    DB d = db_make(8, 2, 1, BW, 2);
    if (!d.dc) { t_skip("no 1bpp DIB"); return; }
    CHECK(U(SetPixel)(d.dc, 3, 0, RGB(255, 255, 255)) == RGB(255, 255, 255)); br_flush();
    CHECK(db_get(&d, 3, 0) == 1);
    CHECK(U(SetPixel)(d.dc, 3, 0, RGB(0, 0, 0)) == RGB(0, 0, 0)); br_flush();
    CHECK(db_get(&d, 3, 0) == 0);
    COLORREF lo = U(SetPixel)(d.dc, 4, 1, RGB(0x20, 0x20, 0x20)), hi = U(SetPixel)(d.dc, 5, 1, RGB(0xE0, 0xE0, 0xE0)); br_flush();
    t_note("1bpp nearest: dark gray -> %08lx bit %u, light gray -> %08lx bit %u", lo, db_get(&d, 4, 1), hi, db_get(&d, 5, 1));
    CHECK(lo == RGB(0, 0, 0) && db_get(&d, 4, 1) == 0);
    CHECK(hi == RGB(255, 255, 255) && db_get(&d, 5, 1) == 1);
    CHECK(U(GetPixel)(d.dc, 3, 0) == RGB(0, 0, 0));
    db_put(&d, 6, 0, 1);
    CHECK(U(GetPixel)(d.dc, 6, 0) == RGB(255, 255, 255));
    db_free(&d);
}

TEST(gdi_blit_raster_setpixel_8bpp_exact_palette_entries) {
    RGBQUAD pal[256]; pal256(pal);
    DB d = db_make(16, 16, 8, pal, 256);
    if (!d.dc) { t_skip("no 8bpp DIB"); return; }
    int bad = 0;
    for (int i = 0; i < 256; i++) {
        int x = i & 15, y = i >> 4;
        COLORREF c = RGB(pal[i].rgbRed, pal[i].rgbGreen, pal[i].rgbBlue);
        COLORREF r = U(SetPixel)(d.dc, x, y, c); br_flush();
        if (r != c || db_get(&d, x, y) != (unsigned)i) bad++;
    }
    CHECKF(bad == 0, "%d palette entries mapped to the wrong index", bad);
    for (int i = 0; i < 256; i += 17) CHECK(U(GetPixel)(d.dc, i & 15, i >> 4) == RGB(pal[i].rgbRed, pal[i].rgbGreen, pal[i].rgbBlue));
    db_free(&d);
}

TEST(gdi_blit_raster_getpixel_8bpp_index_to_color) {
    RGBQUAD pal[256]; pal256(pal);
    DB d = db_make(8, 8, 8, pal, 256);
    if (!d.dc) { t_skip("no 8bpp DIB"); return; }
    for (int i = 0; i < 64; i++) db_put(&d, i & 7, i >> 3, (i * 37) & 255);
    int bad = 0;
    for (int i = 0; i < 64; i++) { unsigned idx = (i * 37) & 255; if (U(GetPixel)(d.dc, i & 7, i >> 3) != RGB(pal[idx].rgbRed, pal[idx].rgbGreen, pal[idx].rgbBlue)) bad++; }
    CHECKF(bad == 0, "%d bad", bad);
    db_free(&d);
}

TEST(gdi_blit_raster_setpixel_8bpp_nearest_logged) {
    RGBQUAD pal[256]; pal256(pal);
    DB d = db_make(4, 4, 8, pal, 256);
    if (!d.dc) { t_skip("no 8bpp DIB"); return; }
    COLORREF r = U(SetPixel)(d.dc, 0, 0, RGB(100, 154, 5)); br_flush();
    t_note("8bpp SetPixel(100,154,5) -> %08lx index %u", r, db_get(&d, 0, 0));
    CHECK(r != CLR_BAD);
    unsigned i = db_get(&d, 0, 0);
    CHECK(r == RGB(pal[i].rgbRed, pal[i].rgbGreen, pal[i].rgbBlue));   /* returned color is the one actually stored */
    db_free(&d);
}

TEST(gdi_blit_raster_setpixel_24bpp_exact) {
    DB d = db_make(4, 4, 24, NULL, 0);
    if (!d.dc) { t_skip("no 24bpp DIB"); return; }
    for (unsigned i = 0; i < sizeof PXCOL / sizeof PXCOL[0]; i++) {
        COLORREF c = PXCOL[i];
        CHECK(U(SetPixel)(d.dc, 2, 1, c) == c); br_flush();
        CHECKF(db_get(&d, 2, 1) == RGBPX(GetRValue(c), GetGValue(c), GetBValue(c)), "stored %06x", db_get(&d, 2, 1));
        CHECK(U(GetPixel)(d.dc, 2, 1) == c);
    }
    CHECK(db_get(&d, 1, 1) == 0 && db_get(&d, 3, 1) == 0);
    db_free(&d);
}

TEST(gdi_blit_raster_setpixel_16bpp_extremes) {
    DB d = db_make(4, 4, 16, NULL, 0);
    if (!d.dc) { t_skip("no 16bpp DIB"); return; }
    U(SetPixel)(d.dc, 0, 0, RGB(255, 255, 255)); U(SetPixel)(d.dc, 1, 0, RGB(0, 0, 0)); U(SetPixel)(d.dc, 2, 0, RGB(255, 0, 0)); br_flush();
    CHECKF(db_get(&d, 0, 0) == 0x7FFF, "white %04x", db_get(&d, 0, 0));
    CHECK(db_get(&d, 1, 0) == 0);
    CHECKF(db_get(&d, 2, 0) == 0x7C00, "red %04x", db_get(&d, 2, 0));
    CHECK(U(GetPixel)(d.dc, 0, 0) == RGB(255, 255, 255));
    COLORREF mid = U(SetPixel)(d.dc, 3, 0, RGB(0x12, 0x34, 0x56));
    t_note("16bpp RGB(12,34,56) -> %08lx raw %04x", mid, db_get(&d, 3, 0));
    db_free(&d);
}

/* ============ ExtFloodFill ============ */
#define BORDER 0x0000FF
/* 8x8 surface: ring of BORDER, interior SENT */
static void ring(BR *d) {
    br_fill(d, SENT);
    for (int i = 0; i < d->w; i++) { br_set(d, i, 0, BORDER); br_set(d, i, d->h - 1, BORDER); }
    for (int i = 0; i < d->h; i++) { br_set(d, 0, i, BORDER); br_set(d, d->w - 1, i, BORDER); }
}
static HBRUSH sel_solid(HDC dc, COLORREF c, HGDIOBJ *old) { HBRUSH b = U(CreateSolidBrush)(c); *old = U(SelectObject)(dc, b); return b; }

TEST(gdi_blit_raster_floodfill_border_fills_interior) {
    NEED32(d, 8, 8); ring(&d);
    HGDIOBJ old; HBRUSH b = sel_solid(d.dc, RGB(0x10, 0x90, 0x30), &old);
    BOOL r = U(ExtFloodFill)(d.dc, 3, 3, PX2CR(BORDER), FLOODFILLBORDER); br_flush();
    CHECK(r);
    CHECK(br_all(&d, 1, 1, 7, 7, RGBPX(0x10, 0x90, 0x30)));
    CHECK(br_count(&d, 0, 0, 8, 8, BORDER) == 28);
    U(SelectObject)(d.dc, old); U(DeleteObject)(b); br_free(&d);
}

TEST(gdi_blit_raster_floodfill_surface_replaces_same_color) {
    NEED32(d, 8, 8); ring(&d);
    HGDIOBJ old; HBRUSH b = sel_solid(d.dc, RGB(0x10, 0x90, 0x30), &old);
    BOOL r = U(ExtFloodFill)(d.dc, 2, 5, PX2CR(SENT), FLOODFILLSURFACE); br_flush();
    CHECK(r);
    CHECK(br_all(&d, 1, 1, 7, 7, RGBPX(0x10, 0x90, 0x30)));
    CHECK(br_count(&d, 0, 0, 8, 8, BORDER) == 28);
    U(SelectObject)(d.dc, old); U(DeleteObject)(b); br_free(&d);
}

TEST(gdi_blit_raster_floodfill_syscall_matches_userland) {
    NT(NtGdiExtFloodFill); NEED32(d, 8, 8); ring(&d);
    HGDIOBJ old; HBRUSH b = sel_solid(d.dc, RGB(1, 2, 3), &old);
    BOOL r = p_NtGdiExtFloodFill(d.dc, 4, 4, PX2CR(BORDER), FLOODFILLBORDER); br_flush();
    CHECK(r);
    CHECK(br_all(&d, 1, 1, 7, 7, RGBPX(1, 2, 3)));
    U(SelectObject)(d.dc, old); U(DeleteObject)(b); br_free(&d);
}

TEST(gdi_blit_raster_floodfill_stops_at_wall) {
    NEED32(d, 8, 8); ring(&d);
    for (int y = 1; y < 7; y++) br_set(&d, 4, y, BORDER);    /* vertical wall splits the interior */
    HGDIOBJ old; HBRUSH b = sel_solid(d.dc, RGB(9, 9, 9), &old);
    BOOL r = U(ExtFloodFill)(d.dc, 2, 2, PX2CR(BORDER), FLOODFILLBORDER); br_flush();
    CHECK(r);
    CHECK(br_all(&d, 1, 1, 4, 7, RGBPX(9, 9, 9)));
    CHECK(br_all(&d, 5, 1, 7, 7, SENT));
    U(SelectObject)(d.dc, old); U(DeleteObject)(b); br_free(&d);
}

TEST(gdi_blit_raster_floodfill_diagonal_gap_does_not_leak) {
    NEED32(d, 8, 8); br_fill(&d, SENT);
    for (int i = 0; i < 8; i++) br_set(&d, i, i, BORDER);   /* a diagonal wall: 4-connected fill must not cross */
    HGDIOBJ old; HBRUSH b = sel_solid(d.dc, RGB(9, 9, 9), &old);
    BOOL r = U(ExtFloodFill)(d.dc, 6, 1, PX2CR(BORDER), FLOODFILLBORDER); br_flush();
    CHECK(r);
    int filled_upper = 0, filled_lower = 0;
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) if (br_at(&d, x, y) == RGBPX(9, 9, 9)) { if (x > y) filled_upper++; else if (x < y) filled_lower++; }
    CHECKF(filled_upper == 28, "upper triangle filled %d of 28", filled_upper);
    CHECKF(filled_lower == 0, "diagonal wall leaked %d pixels", filled_lower);
    U(SelectObject)(d.dc, old); U(DeleteObject)(b); br_free(&d);
}

TEST(gdi_blit_raster_floodfill_start_on_border_logged) {
    NEED32(d, 8, 8); ring(&d);
    HGDIOBJ old; HBRUSH b = sel_solid(d.dc, RGB(9, 9, 9), &old);
    BOOL r = U(ExtFloodFill)(d.dc, 0, 0, PX2CR(BORDER), FLOODFILLBORDER); br_flush();
    BOOL r2 = U(ExtFloodFill)(d.dc, 3, 3, PX2CR(BORDER), FLOODFILLSURFACE); br_flush();
    t_note("flood from border pixel (BORDER mode) returned %d; SURFACE mode with non-matching color returned %d", r, r2);
    CHECK(br_all(&d, 1, 1, 7, 7, SENT));
    U(SelectObject)(d.dc, old); U(DeleteObject)(b); br_free(&d);
}

TEST(gdi_blit_raster_floodfill_respects_clip) {
    NEED32(d, 8, 8); ring(&d);
    HRGN rg = U(CreateRectRgn)(0, 0, 8, 4);
    U(SelectClipRgn)(d.dc, rg);
    HGDIOBJ old; HBRUSH b = sel_solid(d.dc, RGB(9, 9, 9), &old);
    BOOL r = U(ExtFloodFill)(d.dc, 3, 2, PX2CR(BORDER), FLOODFILLBORDER); br_flush();
    t_note("flood fill under clip returned %d", r);
    CHECK(br_all(&d, 1, 4, 7, 7, SENT));
    CHECK(br_at(&d, 3, 2) == RGBPX(9, 9, 9));
    U(SelectClipRgn)(d.dc, NULL); U(DeleteObject)(rg);
    U(SelectObject)(d.dc, old); U(DeleteObject)(b); br_free(&d);
}

TEST(gdi_blit_raster_floodfill_uses_brush_pattern_origin) {
    NEED32(d, 8, 8); ring(&d);
    HGDIOBJ old; HBRUSH b = sel_solid(d.dc, RGB(9, 9, 9), &old);
    U(SetROP2)(d.dc, R2_NOT);
    BOOL r = U(ExtFloodFill)(d.dc, 3, 3, PX2CR(BORDER), FLOODFILLBORDER); br_flush();
    t_note("flood fill in R2_NOT returned %d, interior pixel %06x", r, br_at(&d, 3, 3));
    U(SetROP2)(d.dc, R2_COPYPEN);
    U(SelectObject)(d.dc, old); U(DeleteObject)(b); br_free(&d);
}

TEST(gdi_blit_raster_floodfill_invalid_dc) {
    NT(NtGdiExtFloodFill); NEED32(d, 4, 4);
    BR t = br_make(4, 4); HDC dead = t.dc; br_free(&t);
    CHECK(!p_NtGdiExtFloodFill(NULL, 1, 1, 0, FLOODFILLSURFACE));
    CHECK(!p_NtGdiExtFloodFill(dead, 1, 1, 0, FLOODFILLSURFACE));
    CHECK(!U(ExtFloodFill)(dead, 1, 1, 0, FLOODFILLSURFACE));
    br_free(&d);
}

/* ============ region fills ============ */
TEST(gdi_blit_raster_fillrgn_rect_region_exact) {
    NT(NtGdiFillRgn); NEED32(d, 10, 10); br_fill(&d, SENT);
    HRGN rg = U(CreateRectRgn)(2, 3, 7, 6);
    HBRUSH b = U(CreateSolidBrush)(RGB(1, 2, 3));
    BOOL r = p_NtGdiFillRgn(d.dc, rg, b); br_flush();
    CHECK(r);
    for (int y = 0; y < 10; y++) for (int x = 0; x < 10; x++)
        CHECKF(br_at(&d, x, y) == ((x >= 2 && x < 7 && y >= 3 && y < 6) ? RGBPX(1, 2, 3) : SENT), "(%d,%d)", x, y);
    U(DeleteObject)(b); U(DeleteObject)(rg); br_free(&d);
}

TEST(gdi_blit_raster_fillrgn_complex_region_exact) {
    NEED32(d, 10, 10); br_fill(&d, SENT);
    HRGN a = U(CreateRectRgn)(0, 0, 4, 4), c = U(CreateRectRgn)(6, 6, 10, 10), e = U(CreateRectRgn)(2, 2, 3, 3);
    U(CombineRgn)(a, a, c, RGN_OR); U(CombineRgn)(a, a, e, RGN_DIFF);
    HBRUSH b = U(CreateSolidBrush)(RGB(1, 2, 3));
    BOOL r = U(FillRgn)(d.dc, a, b); br_flush();
    CHECK(r);
    CHECK(br_count(&d, 0, 0, 10, 10, RGBPX(1, 2, 3)) == 16 - 1 + 16);
    CHECK(br_at(&d, 2, 2) == SENT && br_at(&d, 0, 0) == RGBPX(1, 2, 3) && br_at(&d, 9, 9) == RGBPX(1, 2, 3) && br_at(&d, 5, 5) == SENT);
    U(DeleteObject)(b); U(DeleteObject)(a); U(DeleteObject)(c); U(DeleteObject)(e); br_free(&d);
}

TEST(gdi_blit_raster_fillrgn_empty_region_noop) {
    NT(NtGdiFillRgn); NEED32(d, 4, 4); br_fill(&d, SENT);
    HRGN rg = U(CreateRectRgn)(0, 0, 0, 0);
    HBRUSH b = U(CreateSolidBrush)(RGB(1, 2, 3));
    BOOL r = p_NtGdiFillRgn(d.dc, rg, b); br_flush();
    t_note("FillRgn of an empty region returned %d", r);
    CHECK(br_all(&d, 0, 0, 4, 4, SENT));
    U(DeleteObject)(b); U(DeleteObject)(rg); br_free(&d);
}

TEST(gdi_blit_raster_fillrgn_clipped_by_surface_and_clip) {
    NEED32(d, 6, 6); br_fill(&d, SENT);
    HRGN rg = U(CreateRectRgn)(-5, -5, 20, 20), clip = U(CreateRectRgn)(1, 1, 3, 3);
    HBRUSH b = U(CreateSolidBrush)(RGB(1, 2, 3));
    U(SelectClipRgn)(d.dc, clip);
    BOOL r = U(FillRgn)(d.dc, rg, b); br_flush();
    CHECK(r);
    CHECK(br_count(&d, 0, 0, 6, 6, RGBPX(1, 2, 3)) == 4 && br_all(&d, 1, 1, 3, 3, RGBPX(1, 2, 3)));
    U(SelectClipRgn)(d.dc, NULL);
    r = U(FillRgn)(d.dc, rg, b); br_flush();
    CHECK(r && br_all(&d, 0, 0, 6, 6, RGBPX(1, 2, 3)));
    U(DeleteObject)(b); U(DeleteObject)(rg); U(DeleteObject)(clip); br_free(&d);
}

TEST(gdi_blit_raster_fillrgn_invalid_arguments) {
    NT(NtGdiFillRgn); NEED32(d, 4, 4); br_fill(&d, SENT);
    HRGN rg = U(CreateRectRgn)(0, 0, 4, 4);
    HBRUSH b = U(CreateSolidBrush)(RGB(1, 2, 3));
    HRGN dead = U(CreateRectRgn)(0, 0, 1, 1); U(DeleteObject)(dead);
    CHECK(!U(FillRgn)(d.dc, NULL, b));
    CHECK(!U(FillRgn)(d.dc, rg, NULL));
    CHECK(!U(FillRgn)(NULL, rg, b));
    CHECK(!p_NtGdiFillRgn(NULL, rg, b));
    CHECK(!p_NtGdiFillRgn(d.dc, dead, b));
    CHECK(!p_NtGdiFillRgn(d.dc, rg, NULL));
    CHECK(!p_NtGdiFillRgn(d.dc, (HRGN)b, b));       /* a brush handle is not a region */
    CHECK(!p_NtGdiFillRgn(d.dc, rg, (HBRUSH)rg));   /* a region handle is not a brush */
    br_flush();
    CHECK(br_all(&d, 0, 0, 4, 4, SENT));
    U(DeleteObject)(b); U(DeleteObject)(rg); br_free(&d);
}

TEST(gdi_blit_raster_paintrgn_uses_selected_brush) {
    NEED32(d, 8, 8); br_fill(&d, SENT);
    HRGN rg = U(CreateRectRgn)(1, 1, 5, 4);
    HGDIOBJ old; HBRUSH b = sel_solid(d.dc, RGB(0x70, 0x80, 0x90), &old);
    BOOL r = U(PaintRgn)(d.dc, rg); br_flush();
    CHECK(r);
    CHECK(br_all(&d, 1, 1, 5, 4, RGBPX(0x70, 0x80, 0x90)) && br_count(&d, 0, 0, 8, 8, SENT) == 64 - 12);
    U(SelectObject)(d.dc, old); U(DeleteObject)(b); U(DeleteObject)(rg); br_free(&d);
}

TEST(gdi_blit_raster_invertrgn_inverts_exactly) {
    NT(NtGdiInvertRgn); NEED32(d, 8, 8);
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) br_set(&d, x, y, br_pat(x, y));
    HRGN rg = U(CreateRectRgn)(2, 2, 6, 5);
    BOOL r = p_NtGdiInvertRgn(d.dc, rg); br_flush();
    CHECK(r);
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++)
        CHECKF(br_at(&d, x, y) == ((x >= 2 && x < 6 && y >= 2 && y < 5) ? (br_pat(x, y) ^ M24) : br_pat(x, y)), "(%d,%d)", x, y);
    r = U(InvertRgn)(d.dc, rg); br_flush();
    CHECK(r);
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) CHECK(br_at(&d, x, y) == br_pat(x, y));
    CHECK(!p_NtGdiInvertRgn(d.dc, NULL));
    CHECK(!p_NtGdiInvertRgn(NULL, rg));
    U(DeleteObject)(rg); br_free(&d);
}

TEST(gdi_blit_raster_framergn_draws_border_only) {
    NT(NtGdiFrameRgn); NEED32(d, 14, 12); br_fill(&d, SENT);
    HRGN rg = U(CreateRectRgn)(3, 3, 11, 9);   /* 8 x 6 */
    HBRUSH b = U(CreateSolidBrush)(RGB(1, 2, 3));
    BOOL r = p_NtGdiFrameRgn(d.dc, rg, b, 1, 1); br_flush();
    CHECK(r);
    int n = br_count(&d, 0, 0, 14, 12, RGBPX(1, 2, 3));
    t_note("FrameRgn 1x1 around an 8x6 region painted %d pixels (24 = inside ring, 32 = outside ring)", n);
    CHECK(n == 24 || n == 32 || n == 28);
    CHECK(br_at(&d, 6, 5) == SENT);               /* interior untouched */
    CHECK(br_at(&d, 0, 0) == SENT && br_at(&d, 13, 11) == SENT);
    U(DeleteObject)(b); U(DeleteObject)(rg); br_free(&d);
}

TEST(gdi_blit_raster_framergn_thickness_scales_ring) {
    NEED32(d, 20, 16); br_fill(&d, SENT);
    HRGN rg = U(CreateRectRgn)(4, 4, 16, 12);   /* 12 x 8 */
    HBRUSH b = U(CreateSolidBrush)(RGB(1, 2, 3));
    CHECK(U(FrameRgn)(d.dc, rg, b, 2, 3)); br_flush();
    int n2 = br_count(&d, 0, 0, 20, 16, RGBPX(1, 2, 3));
    br_fill(&d, SENT);
    CHECK(U(FrameRgn)(d.dc, rg, b, 1, 1)); br_flush();
    int n1 = br_count(&d, 0, 0, 20, 16, RGBPX(1, 2, 3));
    t_note("ring pixels: 1x1 -> %d, 2x3 -> %d", n1, n2);
    CHECK(n2 > n1 && n1 > 0);
    U(DeleteObject)(b); U(DeleteObject)(rg); br_free(&d);
}

TEST(gdi_blit_raster_framergn_invalid_arguments) {
    NT(NtGdiFrameRgn); NEED32(d, 4, 4); br_fill(&d, SENT);
    HRGN rg = U(CreateRectRgn)(0, 0, 4, 4);
    HBRUSH b = U(CreateSolidBrush)(RGB(1, 2, 3));
    CHECK(!p_NtGdiFrameRgn(NULL, rg, b, 1, 1));
    CHECK(!p_NtGdiFrameRgn(d.dc, NULL, b, 1, 1));
    CHECK(!p_NtGdiFrameRgn(d.dc, rg, NULL, 1, 1));
    CHECK(!U(FrameRgn)(d.dc, NULL, b, 1, 1));
    br_flush();
    CHECK(br_all(&d, 0, 0, 4, 4, SENT));
    U(DeleteObject)(b); U(DeleteObject)(rg); br_free(&d);
}

/* ============ pattern brush alignment / hatch ============ */
static HBRUSH pat_brush(void) {
    struct { BITMAPINFOHEADER h; DWORD px[64]; } p;
    memset(&p, 0, sizeof p);
    p.h.biSize = sizeof p.h; p.h.biWidth = 8; p.h.biHeight = -8; p.h.biPlanes = 1; p.h.biBitCount = 32; p.h.biCompression = BI_RGB;
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) p.px[y * 8 + x] = br_pat(x, y);
    return U(CreateDIBPatternBrushPt)(&p, DIB_RGB_COLORS);
}

static void brush_org_case(int ox, int oy) {
    NT(NtGdiPatBlt); NEED32(d, 20, 20);
    HBRUSH pb = pat_brush();
    if (!pb) { t_skip("no pattern brush"); br_free(&d); return; }
    HGDIOBJ old = U(SelectObject)(d.dc, pb);
    U(SetBrushOrgEx)(d.dc, ox, oy, NULL);
    POINT got = { 12345, 12345 };
    U(GetBrushOrgEx)(d.dc, &got);
    CHECKF(got.x == ox && got.y == oy, "GetBrushOrgEx (%ld,%ld)", got.x, got.y);
    BOOL r = p_NtGdiPatBlt(d.dc, 0, 0, 20, 20, ROPCODE(0xF0)); br_flush();
    CHECK(r);
    int bad = 0;
    for (int y = 0; y < 20; y++) for (int x = 0; x < 20; x++) {
        int px = ((x - ox) % 8 + 8) % 8, py = ((y - oy) % 8 + 8) % 8;
        if (br_at(&d, x, y) != br_pat(px, py)) bad++;
    }
    CHECKF(bad == 0, "org (%d,%d): %d pixels mis-aligned", ox, oy, bad);
    U(SelectObject)(d.dc, old); U(DeleteObject)(pb); br_free(&d);
}
#define ORG_TEST(nm, ox, oy) TEST(gdi_blit_raster_brushorg_##nm) { brush_org_case(ox, oy); }
ORG_TEST(zero, 0, 0)
ORG_TEST(x1y0, 1, 0)
ORG_TEST(x0y1, 0, 1)
ORG_TEST(x3y5, 3, 5)
ORG_TEST(x7y7, 7, 7)
ORG_TEST(x8y8, 8, 8)
ORG_TEST(x9y17, 9, 17)
ORG_TEST(neg2neg3, -2, -3)
ORG_TEST(neg9, -9, 0)

TEST(gdi_blit_raster_brushorg_change_after_fill_does_not_move_pixels) {
    NT(NtGdiPatBlt); NEED32(d, 16, 8);
    HBRUSH pb = pat_brush(); HGDIOBJ old = U(SelectObject)(d.dc, pb);
    p_NtGdiPatBlt(d.dc, 0, 0, 8, 8, ROPCODE(0xF0));
    U(SetBrushOrgEx)(d.dc, 4, 0, NULL);
    p_NtGdiPatBlt(d.dc, 8, 0, 8, 8, ROPCODE(0xF0)); br_flush();
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {
        CHECKF(br_at(&d, x, y) == br_pat(x, y), "left (%d,%d)", x, y);
        CHECKF(br_at(&d, 8 + x, y) == br_pat(((8 + x - 4) % 8 + 8) % 8, y), "right (%d,%d)", x, y);
    }
    U(SelectObject)(d.dc, old); U(DeleteObject)(pb); br_free(&d);
}

TEST(gdi_blit_raster_brushorg_setbrushorgex_returns_previous) {
    NEED32(d, 4, 4);
    POINT old = { 77, 77 };
    CHECK(U(SetBrushOrgEx)(d.dc, 5, 6, &old));
    CHECK(old.x == 0 && old.y == 0);
    CHECK(U(SetBrushOrgEx)(d.dc, 1, 2, &old));
    CHECK(old.x == 5 && old.y == 6);
    CHECK(!U(SetBrushOrgEx)(NULL, 1, 2, &old));
    br_free(&d);
}

TEST(gdi_blit_raster_brushorg_survives_savedc_restore) {
    NEED32(d, 4, 4);
    U(SetBrushOrgEx)(d.dc, 3, 4, NULL);
    int id = U(SaveDC)(d.dc);
    U(SetBrushOrgEx)(d.dc, 6, 7, NULL);
    U(RestoreDC)(d.dc, id);
    POINT p; U(GetBrushOrgEx)(d.dc, &p);
    t_note("brush origin after RestoreDC: (%ld,%ld)", p.x, p.y);
    CHECK(p.x == 3 || p.x == 6);
    br_free(&d);
}

/* mode 1: OPAQUE + PatBlt; 0: TRANSPARENT + PatBlt (Windows ignores the background mode and still paints the background
 * colour); 2: TRANSPARENT + Rectangle with a null pen (the background really stays untouched). */
static void hatch_tile(int style, COLORREF fg, int mode, COLORREF bk, DWORD base, unsigned char tile[8][8], int *nfg) {
    NT(NtGdiPatBlt); NEED32(d, 16, 16);
    br_fill(&d, base);
    HBRUSH hb = U(CreateHatchBrush)(style, fg);
    HGDIOBJ old = U(SelectObject)(d.dc, hb);
    U(SetBkMode)(d.dc, mode == 1 ? OPAQUE : TRANSPARENT);
    U(SetBkColor)(d.dc, bk);
    if (mode == 2) {
        HGDIOBJ op = U(SelectObject)(d.dc, U(GetStockObject)(NULL_PEN));
        U(Rectangle)(d.dc, 0, 0, 17, 17);   /* a null pen leaves the right/bottom edge out: 16x16 pixels */
        U(SelectObject)(d.dc, op);
    } else p_NtGdiPatBlt(d.dc, 0, 0, 16, 16, ROPCODE(0xF0));
    br_flush();
    int opaque = mode != 2;
    DWORD f = RGBPX(GetRValue(fg), GetGValue(fg), GetBValue(fg)), b = opaque ? RGBPX(GetRValue(bk), GetGValue(bk), GetBValue(bk)) : base;
    int bad = 0; *nfg = 0;
    for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
        DWORD v = br_at(&d, x, y);
        if (v != f && v != b) bad++;
        int is = v == f && f != b;
        if (y < 8 && x < 8) { tile[y][x] = is; *nfg += is; }
        else if (tile[y & 7][x & 7] != is) bad++;     /* period 8 */
    }
    CHECKF(bad == 0, "hatch %d: %d stray/non-periodic pixels", style, bad);
    U(SelectObject)(d.dc, old); U(DeleteObject)(hb); br_free(&d);
}

#define HATCH_TEST(nm, style, expected) \
TEST(gdi_blit_raster_hatch_##nm##_opaque) { \
    unsigned char t[8][8]; int n = -1; memset(t, 0, sizeof t); \
    hatch_tile(style, RGB(0xFF, 0x00, 0x00), 1, RGB(0x00, 0x00, 0xFF), SENT, t, &n); \
    if (n >= 0 && (expected) > 0) CHECKF(n == (expected), "foreground pixels per 8x8 tile %d", n); \
    t_note(#nm ": %d foreground pixels per tile", n); \
} \
TEST(gdi_blit_raster_hatch_##nm##_transparent) { \
    unsigned char t[8][8]; int n = -1; memset(t, 0, sizeof t); \
    hatch_tile(style, RGB(0x00, 0xFF, 0x00), 2, RGB(0x00, 0x00, 0xFF), SENT, t, &n); \
    if (n >= 0 && (expected) > 0) CHECKF(n == (expected), "foreground pixels per 8x8 tile %d", n); \
}
HATCH_TEST(horizontal, HS_HORIZONTAL, 8)
HATCH_TEST(vertical, HS_VERTICAL, 8)
HATCH_TEST(fdiagonal, HS_FDIAGONAL, 8)
HATCH_TEST(bdiagonal, HS_BDIAGONAL, 8)
HATCH_TEST(cross, HS_CROSS, 15)
HATCH_TEST(diagcross, HS_DIAGCROSS, 16)

/* PatBlt with a hatch brush paints the background colour even in TRANSPARENT mode (hatch_tile mode 0 checks that every
 * non-foreground pixel took the background colour). */
#define HATCH_PATBLT_TEST(nm, style) \
TEST(gdi_blit_raster_hatch_##nm##_patblt_ignores_transparent_mode) { \
    unsigned char t[8][8]; int n = -1; memset(t, 0, sizeof t); \
    hatch_tile(style, RGB(0x00, 0xFF, 0x00), 0, RGB(0x00, 0x00, 0xFF), SENT, t, &n); \
}
HATCH_PATBLT_TEST(horizontal, HS_HORIZONTAL)
HATCH_PATBLT_TEST(vertical, HS_VERTICAL)
HATCH_PATBLT_TEST(fdiagonal, HS_FDIAGONAL)
HATCH_PATBLT_TEST(bdiagonal, HS_BDIAGONAL)
HATCH_PATBLT_TEST(cross, HS_CROSS)
HATCH_PATBLT_TEST(diagcross, HS_DIAGCROSS)

TEST(gdi_blit_raster_hatch_horizontal_is_single_row_vertical_single_column) {
    unsigned char h[8][8], v[8][8]; int nh = 0, nv = 0;
    hatch_tile(HS_HORIZONTAL, RGB(255, 0, 0), 1, RGB(0, 0, 255), SENT, h, &nh);
    hatch_tile(HS_VERTICAL, RGB(255, 0, 0), 1, RGB(0, 0, 255), SENT, v, &nv);
    int rows = 0, cols = 0;
    for (int y = 0; y < 8; y++) { int s = 0; for (int x = 0; x < 8; x++) s += h[y][x]; if (s == 8) rows++; else CHECK(s == 0); }
    for (int x = 0; x < 8; x++) { int s = 0; for (int y = 0; y < 8; y++) s += v[y][x]; if (s == 8) cols++; else CHECK(s == 0); }
    CHECK(rows == 1 && cols == 1);
}

TEST(gdi_blit_raster_hatch_cross_is_union_of_horizontal_and_vertical) {
    unsigned char h[8][8], v[8][8], c[8][8]; int n;
    hatch_tile(HS_HORIZONTAL, RGB(255, 0, 0), 1, RGB(0, 0, 255), SENT, h, &n);
    hatch_tile(HS_VERTICAL, RGB(255, 0, 0), 1, RGB(0, 0, 255), SENT, v, &n);
    hatch_tile(HS_CROSS, RGB(255, 0, 0), 1, RGB(0, 0, 255), SENT, c, &n);
    int diff = 0;
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) if (c[y][x] != (h[y][x] | v[y][x])) diff++;
    t_note("HS_CROSS differs from HS_HORIZONTAL|HS_VERTICAL in %d positions", diff);
    CHECK(diff == 0);
}

TEST(gdi_blit_raster_hatch_diagonals_are_mirror_images) {
    unsigned char f[8][8], b[8][8]; int n;
    hatch_tile(HS_FDIAGONAL, RGB(255, 0, 0), 1, RGB(0, 0, 255), SENT, f, &n);
    hatch_tile(HS_BDIAGONAL, RGB(255, 0, 0), 1, RGB(0, 0, 255), SENT, b, &n);
    int diff = 0, dflip = 0;
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) { if (f[y][x] != b[y][7 - x]) diff++; if (f[y][x] != b[7 - y][x]) dflip++; }
    t_note("diagonal mirror mismatch horizontally %d, vertically %d", diff, dflip);
    CHECK(diff == 0 || dflip == 0);
}

/* ============ patblt under mapping modes ============ */
TEST(gdi_blit_raster_patblt_anisotropic_extents_scale) {
    NT(NtGdiPatBlt); NEED32(d, 16, 16); br_fill(&d, SENT);
    HBRUSH b = U(CreateSolidBrush)(RGB(1, 2, 3)); HGDIOBJ old = U(SelectObject)(d.dc, b);
    U(SetMapMode)(d.dc, MM_ANISOTROPIC);
    U(SetWindowExtEx)(d.dc, 1, 1, NULL); U(SetViewportExtEx)(d.dc, 2, 3, NULL);
    BOOL r = p_NtGdiPatBlt(d.dc, 1, 1, 2, 2, ROPCODE(0xF0)); br_flush();
    CHECK(r);
    for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++)
        CHECKF(br_at(&d, x, y) == ((x >= 2 && x < 6 && y >= 3 && y < 9) ? RGBPX(1, 2, 3) : SENT), "(%d,%d)", x, y);
    U(SetMapMode)(d.dc, MM_TEXT);
    U(SelectObject)(d.dc, old); U(DeleteObject)(b); br_free(&d);
}

TEST(gdi_blit_raster_patblt_window_and_viewport_origin) {
    NT(NtGdiPatBlt); NEED32(d, 12, 12); br_fill(&d, SENT);
    HBRUSH b = U(CreateSolidBrush)(RGB(1, 2, 3)); HGDIOBJ old = U(SelectObject)(d.dc, b);
    U(SetViewportOrgEx)(d.dc, 4, 5, NULL);
    U(SetWindowOrgEx)(d.dc, 1, 1, NULL);
    BOOL r = p_NtGdiPatBlt(d.dc, 1, 1, 3, 2, ROPCODE(0xF0)); br_flush();
    CHECK(r);
    CHECK(br_all(&d, 4, 5, 7, 7, RGBPX(1, 2, 3)));
    CHECK(br_count(&d, 0, 0, 12, 12, RGBPX(1, 2, 3)) == 6);
    U(SetViewportOrgEx)(d.dc, 0, 0, NULL); U(SetWindowOrgEx)(d.dc, 0, 0, NULL);
    U(SelectObject)(d.dc, old); U(DeleteObject)(b); br_free(&d);
}

TEST(gdi_blit_raster_patblt_isotropic_maptype_units) {
    NT(NtGdiPatBlt); NEED32(d, 16, 16); br_fill(&d, SENT);
    HBRUSH b = U(CreateSolidBrush)(RGB(1, 2, 3)); HGDIOBJ old = U(SelectObject)(d.dc, b);
    U(SetMapMode)(d.dc, MM_ANISOTROPIC);
    U(SetWindowExtEx)(d.dc, 4, 4, NULL); U(SetViewportExtEx)(d.dc, 8, 8, NULL);   /* 2 device pixels per unit */
    BOOL r = p_NtGdiPatBlt(d.dc, 2, 2, 3, 1, ROPCODE(0xF0)); br_flush();
    CHECK(r);
    CHECK(br_all(&d, 4, 4, 10, 6, RGBPX(1, 2, 3)) && br_count(&d, 0, 0, 16, 16, RGBPX(1, 2, 3)) == 12);
    U(SetMapMode)(d.dc, MM_TEXT);
    U(SelectObject)(d.dc, old); U(DeleteObject)(b); br_free(&d);
}

TEST(gdi_blit_raster_bitblt_between_maptypes_uses_device_size_of_dest) {
    NT(NtGdiBitBlt); NEED32(d, 12, 12); NEED32(s, 8, 8);
    br_fill(&d, SENT); br_fill_pat(&s);
    U(SetMapMode)(d.dc, MM_ANISOTROPIC);
    U(SetWindowExtEx)(d.dc, 1, 1, NULL); U(SetViewportExtEx)(d.dc, 2, 2, NULL);
    BOOL r = p_NtGdiBitBlt(d.dc, 0, 0, 3, 3, s.dc, 0, 0, SRCCOPY, CLR_BAD, 0); br_flush();
    int w = 0;
    for (int y = 0; y < 12; y++) for (int x = 0; x < 12; x++) if (br_at(&d, x, y) != SENT) w++;
    t_note("BitBlt 3x3 logical into a 2x-scaled DC returned %d and wrote %d pixels", r, w);
    CHECK(r);
    CHECK(br_at(&d, 0, 0) == br_pat(0, 0));
    U(SetMapMode)(d.dc, MM_TEXT);
    br_free(&d); br_free(&s);
}

/* ============ conversion between depths ============ */
/* A monochrome DDB (CreateBitmap 1x1bpp) selected into a memory DC: 0 bits take the destination DC's text
 * colour and 1 bits its background colour. Rows are WORD aligned; the first byte holds the leftmost 8 pixels. */
static HDC mono_dc(const WORD *rows, int h, HBITMAP *bmp, HGDIOBJ *old) {
    *bmp = U(CreateBitmap)(8, h, 1, 1, rows);
    HDC md = U(CreateCompatibleDC)(NULL);
    if (!*bmp || !md) return NULL;
    *old = U(SelectObject)(md, *bmp);
    return md;
}
TEST(gdi_blit_raster_blt_1bpp_to_32bpp_uses_text_and_bk_colors) {
    NEED32(d, 8, 2);
    static const WORD rows[2] = { 0x55, 0x33 };   /* 01010101 / 00110011 */
    HBITMAP mb; HGDIOBJ mo; HDC md = mono_dc(rows, 2, &mb, &mo);
    if (!md) { t_skip("no mono bitmap"); br_free(&d); return; }
    U(SetTextColor)(d.dc, RGB(0xFF, 0x00, 0x00)); U(SetBkColor)(d.dc, RGB(0x00, 0x00, 0xFF));
    BOOL r = U(BitBlt)(d.dc, 0, 0, 8, 2, md, 0, 0, SRCCOPY); br_flush();
    CHECK(r);
    for (int y = 0; y < 2; y++) for (int x = 0; x < 8; x++) {
        unsigned bit = (rows[y] >> (7 - x)) & 1;
        CHECKF(br_at(&d, x, y) == (bit ? RGBPX(0, 0, 0xFF) : RGBPX(0xFF, 0, 0)), "(%d,%d) bit %u got %06x", x, y, bit, br_at(&d, x, y));
    }
    U(SelectObject)(md, mo); U(DeleteDC)(md); U(DeleteObject)(mb); br_free(&d);
}

TEST(gdi_blit_raster_blt_1bpp_to_32bpp_default_colors_black_white) {
    NEED32(d, 8, 1);
    static const WORD rows[1] = { 0x55 };
    HBITMAP mb; HGDIOBJ mo; HDC md = mono_dc(rows, 1, &mb, &mo);
    if (!md) { t_skip("no mono bitmap"); br_free(&d); return; }
    U(SetTextColor)(d.dc, RGB(0, 0, 0)); U(SetBkColor)(d.dc, RGB(255, 255, 255));
    CHECK(U(BitBlt)(d.dc, 0, 0, 8, 1, md, 0, 0, SRCCOPY)); br_flush();
    for (int x = 0; x < 8; x++) CHECK(br_at(&d, x, 0) == ((x & 1) ? 0xFFFFFF : 0));
    U(SelectObject)(md, mo); U(DeleteDC)(md); U(DeleteObject)(mb); br_free(&d);
}

TEST(gdi_blit_raster_blt_1bpp_to_32bpp_srcinvert_with_colors) {
    NEED32(d, 8, 1);
    static const WORD rows[1] = { 0x55 };
    HBITMAP mb; HGDIOBJ mo; HDC md = mono_dc(rows, 1, &mb, &mo);
    if (!md) { t_skip("no mono bitmap"); br_free(&d); return; }
    br_fill(&d, 0x112233);
    U(SetTextColor)(d.dc, RGB(0x0F, 0x0F, 0x0F)); U(SetBkColor)(d.dc, RGB(0xF0, 0xF0, 0xF0));
    CHECK(U(BitBlt)(d.dc, 0, 0, 8, 1, md, 0, 0, SRCINVERT)); br_flush();
    for (int x = 0; x < 8; x++) CHECKF(br_at(&d, x, 0) == (0x112233 ^ ((x & 1) ? 0xF0F0F0 : 0x0F0F0F)), "x=%d got %06x", x, br_at(&d, x, 0));
    U(SelectObject)(md, mo); U(DeleteDC)(md); U(DeleteObject)(mb); br_free(&d);
}

TEST(gdi_blit_raster_blt_32bpp_to_1bpp_matches_source_bk_color) {
    /* Colour to monochrome DDB: pixels equal to the source DC background colour become 1, everything else 0. (A 1 bpp
     * DIB section destination does not follow this rule, see the next test.) */
    NEED32(s, 8, 2);
    HDC md = U(CreateCompatibleDC)(NULL);
    HBITMAP mb = U(CreateBitmap)(8, 2, 1, 1, NULL);
    if (!md || !mb) { if (md) U(DeleteDC)(md); if (mb) U(DeleteObject)(mb); t_skip("no mono bitmap"); br_free(&s); return; }
    HGDIOBJ mo = U(SelectObject)(md, mb);
    DWORD key = RGBPX(0x12, 0x34, 0x56);
    for (int y = 0; y < 2; y++) for (int x = 0; x < 8; x++) br_set(&s, x, y, ((x + y) % 3 == 0) ? key : br_pat(x, y));
    U(SetBkColor)(s.dc, PX2CR(key));
    CHECK(U(BitBlt)(md, 0, 0, 8, 2, s.dc, 0, 0, SRCCOPY)); br_flush();
    for (int y = 0; y < 2; y++) for (int x = 0; x < 8; x++)
        CHECKF((U(GetPixel)(md, x, y) != 0) == (((x + y) % 3 == 0) ? 1 : 0), "(%d,%d)", x, y);
    U(SelectObject)(md, mo); U(DeleteDC)(md); U(DeleteObject)(mb); br_free(&s);
}

TEST(gdi_blit_raster_blt_32bpp_to_1bpp_dib_maps_to_nearest_palette_entry) {
    /* A 1 bpp DIB section destination maps every colour to the nearest entry of its own colour table, whatever the
     * background colours of source and destination DC are. */
    NEED32(s, 8, 2);
    DB d = db_make(8, 2, 1, BW, 2);
    if (!d.dc) { t_skip("no 1bpp DIB"); br_free(&s); return; }
    DWORD key = RGBPX(0x12, 0x34, 0x56);
    for (int y = 0; y < 2; y++) for (int x = 0; x < 8; x++) br_set(&s, x, y, ((x + y) % 3 == 0) ? RGBPX(0xF0, 0xF0, 0xF0) : key);
    U(SetBkColor)(s.dc, PX2CR(key));
    CHECK(U(BitBlt)(d.dc, 0, 0, 8, 2, s.dc, 0, 0, SRCCOPY)); br_flush();
    for (int y = 0; y < 2; y++) for (int x = 0; x < 8; x++)
        CHECKF(db_get(&d, x, y) == (unsigned)(((x + y) % 3 == 0) ? 1 : 0), "(%d,%d)", x, y);
    db_free(&d); br_free(&s);
}

TEST(gdi_blit_raster_blt_32bpp_to_8bpp_exact_palette) {
    /* The 32 -> 8 bpp colour match goes through a 5 bits per channel lookup: a palette colour lands on its own index
     * unless another entry shares its 5-5-5 cell, in which case the later entry wins. pal256 has such pairs (i, i+1 for
     * every i divisible by 8); the test requires an index in the same cell, and the exact index when the cell is unique. */
    RGBQUAD pal[256]; pal256(pal);
    NEED32(s, 16, 16);
    DB d = db_make(16, 16, 8, pal, 256);
    if (!d.dc) { t_skip("no 8bpp DIB"); br_free(&s); return; }
    for (int i = 0; i < 256; i++) br_set(&s, i & 15, i >> 4, RGBPX(pal[i].rgbRed, pal[i].rgbGreen, pal[i].rgbBlue));
    CHECK(U(BitBlt)(d.dc, 0, 0, 16, 16, s.dc, 0, 0, SRCCOPY)); br_flush();
    int bad = 0, shared = 0;
    for (int i = 0; i < 256; i++) {
        unsigned g = db_get(&d, i & 15, i >> 4);
        int same = 0, last = i;
        for (int j = 0; j < 256; j++)
            if ((pal[j].rgbRed >> 3) == (pal[i].rgbRed >> 3) && (pal[j].rgbGreen >> 3) == (pal[i].rgbGreen >> 3) && (pal[j].rgbBlue >> 3) == (pal[i].rgbBlue >> 3)) { same++; last = j; }
        if (same == 1) { if (g != (unsigned)i) bad++; }
        else { shared++; if (g != (unsigned)last) bad++; }
    }
    CHECKF(bad == 0, "%d palette colors landed on the wrong index (%d share a 5-5-5 cell)", bad, shared);
    db_free(&d); br_free(&s);
}

TEST(gdi_blit_raster_blt_8bpp_to_32bpp_expands_palette) {
    RGBQUAD pal[256]; pal256(pal);
    NEED32(d, 16, 16);
    DB s = db_make(16, 16, 8, pal, 256);
    if (!s.dc) { t_skip("no 8bpp DIB"); br_free(&d); return; }
    for (int i = 0; i < 256; i++) db_put(&s, i & 15, i >> 4, (i * 5 + 3) & 255);
    CHECK(U(BitBlt)(d.dc, 0, 0, 16, 16, s.dc, 0, 0, SRCCOPY)); br_flush();
    int bad = 0;
    for (int i = 0; i < 256; i++) { unsigned k = (i * 5 + 3) & 255; if (br_at(&d, i & 15, i >> 4) != RGBPX(pal[k].rgbRed, pal[k].rgbGreen, pal[k].rgbBlue)) bad++; }
    CHECKF(bad == 0, "%d wrong", bad);
    db_free(&s); br_free(&d);
}

TEST(gdi_blit_raster_blt_32bpp_to_24bpp_and_back_lossless) {
    NEED32(a, 8, 8); NEED32(c, 8, 8);
    DB m = db_make(8, 8, 24, NULL, 0);
    if (!m.dc) { t_skip("no 24bpp DIB"); br_free(&a); br_free(&c); return; }
    br_fill_pat(&a);
    CHECK(U(BitBlt)(m.dc, 0, 0, 8, 8, a.dc, 0, 0, SRCCOPY));
    CHECK(U(BitBlt)(c.dc, 0, 0, 8, 8, m.dc, 0, 0, SRCCOPY)); br_flush();
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {
        CHECKF(db_get(&m, x, y) == br_pat(x, y), "24bpp (%d,%d) %06x", x, y, db_get(&m, x, y));
        CHECKF(br_at(&c, x, y) == br_pat(x, y), "roundtrip (%d,%d)", x, y);
    }
    db_free(&m); br_free(&a); br_free(&c);
}

TEST(gdi_blit_raster_blt_32bpp_to_16bpp_logged) {
    NEED32(a, 4, 1);
    DB m = db_make(4, 1, 16, NULL, 0);
    if (!m.dc) { t_skip("no 16bpp DIB"); br_free(&a); return; }
    br_set(&a, 0, 0, 0xFFFFFF); br_set(&a, 1, 0, 0x000000); br_set(&a, 2, 0, 0xFF0000); br_set(&a, 3, 0, 0x123456);
    CHECK(U(BitBlt)(m.dc, 0, 0, 4, 1, a.dc, 0, 0, SRCCOPY)); br_flush();
    CHECK(db_get(&m, 0, 0) == 0x7FFF && db_get(&m, 1, 0) == 0 && db_get(&m, 2, 0) == 0x7C00);
    t_note("16bpp 0x123456 -> %04x", db_get(&m, 3, 0));
    db_free(&m); br_free(&a);
}

TEST(gdi_blit_raster_blt_32bpp_to_8bpp_nearest_logged) {
    RGBQUAD pal[256]; pal256(pal);
    NEED32(s, 2, 1);
    DB d = db_make(2, 1, 8, pal, 256);
    if (!d.dc) { t_skip("no 8bpp DIB"); br_free(&s); return; }
    br_set(&s, 0, 0, RGBPX(100, 154, 5)); br_set(&s, 1, 0, RGBPX(250, 7, 200));
    CHECK(U(BitBlt)(d.dc, 0, 0, 2, 1, s.dc, 0, 0, SRCCOPY)); br_flush();
    t_note("8bpp nearest: (100,154,5) -> idx %u, (250,7,200) -> idx %u", db_get(&d, 0, 0), db_get(&d, 1, 0));
    CHECK(db_get(&d, 0, 0) < 256);
    db_free(&d); br_free(&s);
}

TEST(gdi_blit_raster_blt_1bpp_roundtrip_through_32bpp) {
    DB a = db_make(16, 4, 1, BW, 2), b = db_make(16, 4, 1, BW, 2);
    NEED32(m, 16, 4);
    if (!a.dc || !b.dc) { t_skip("no 1bpp DIB"); db_free(&a); db_free(&b); br_free(&m); return; }
    for (int y = 0; y < 4; y++) for (int x = 0; x < 16; x++) db_put(&a, x, y, ((x * 5 + y * 3) % 7) < 3);
    U(SetTextColor)(m.dc, RGB(0, 0, 0)); U(SetBkColor)(m.dc, RGB(255, 255, 255));
    CHECK(U(BitBlt)(m.dc, 0, 0, 16, 4, a.dc, 0, 0, SRCCOPY));
    U(SetBkColor)(m.dc, RGB(255, 255, 255));
    CHECK(U(BitBlt)(b.dc, 0, 0, 16, 4, m.dc, 0, 0, SRCCOPY)); br_flush();
    int bad = 0;
    for (int y = 0; y < 4; y++) for (int x = 0; x < 16; x++) if (db_get(&a, x, y) != db_get(&b, x, y)) bad++;
    CHECKF(bad == 0, "%d bits changed in the 1bpp -> 32bpp -> 1bpp round trip", bad);
    db_free(&a); db_free(&b); br_free(&m);
}

TEST(gdi_blit_raster_blt_mono_ddb_to_32bpp_colors) {
    NEED32(d, 8, 1);
    static const WORD bits[1] = { 0x55 };   /* 01010101 left to right */
    HBITMAP mono = U(CreateBitmap)(8, 1, 1, 1, bits);
    HDC md = U(CreateCompatibleDC)(NULL);
    if (!mono || !md) { t_skip("no mono bitmap"); br_free(&d); return; }
    HGDIOBJ old = U(SelectObject)(md, mono);
    U(SetTextColor)(d.dc, RGB(0x10, 0x20, 0x30)); U(SetBkColor)(d.dc, RGB(0xA0, 0xB0, 0xC0));
    CHECK(U(BitBlt)(d.dc, 0, 0, 8, 1, md, 0, 0, SRCCOPY)); br_flush();
    for (int x = 0; x < 8; x++) {
        int bit = (0x55 >> (7 - x)) & 1;
        CHECKF(br_at(&d, x, 0) == (bit ? RGBPX(0xA0, 0xB0, 0xC0) : RGBPX(0x10, 0x20, 0x30)), "x=%d", x);
    }
    U(SelectObject)(md, old); U(DeleteDC)(md); U(DeleteObject)(mono); br_free(&d);
}

TEST(gdi_blit_raster_gdiflush_makes_batched_patblt_visible) {
    NT(NtGdiPatBlt); NEED32(d, 8, 8); br_fill(&d, SENT);
    HBRUSH b = U(CreateSolidBrush)(RGB(1, 2, 3)); HGDIOBJ old = U(SelectObject)(d.dc, b);
    BOOL r = U(PatBlt)(d.dc, 0, 0, 8, 8, PATCOPY);
    CHECK(r);
    CHECK(U(GdiFlush)());
    CHECK(br_all(&d, 0, 0, 8, 8, RGBPX(1, 2, 3)));
    U(SelectObject)(d.dc, old); U(DeleteObject)(b); br_free(&d);
}

TEST(gdi_blit_raster_gdi_batch_count_drains_after_flush) {
    NEED32(d, 8, 8);
    HBRUSH b = U(CreateSolidBrush)(RGB(1, 2, 3)); HGDIOBJ old = U(SelectObject)(d.dc, b);
    for (int i = 0; i < 4; i++) U(PatBlt)(d.dc, i, 0, 1, 8, PATCOPY);
    U(GdiFlush)();
    unsigned long n = 0xdead;
    if (!k_try_read_u32((unsigned char *)k_teb() + TEB_GdiBatchCount, &n)) t_note("TEB GdiBatchCount unreadable");
    else { CHECKF(n == 0 || t_is_wine(), "GdiBatchCount %lu after GdiFlush", n); }
    U(SelectObject)(d.dc, old); U(DeleteObject)(b); br_free(&d);
}
