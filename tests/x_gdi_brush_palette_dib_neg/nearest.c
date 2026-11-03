/* NtGdiGetNearestPaletteIndex and GetNearestPaletteIndex: the handle-type matrix, the colour forms (plain RGB, the
 * palette-relative and palette-index flag bytes), exact and approximate matches on a known palette, agreement between
 * the Nt stub and the gdi32 export, and consistency with GetPaletteEntries (the entry at the returned index is at least
 * as close to the colour as every other entry) including after SetPaletteEntries / ResizePalette edits. */
#include "xb.h"

#define BAD ((unsigned long long)0xFFFFFFFFu)
static HPALETTE mkpal(const PALETTEENTRY *e, unsigned n) {
    LOGPALETTE *lp = (LOGPALETTE *)calloc(1, sizeof(LOGPALETTE) + n * sizeof(PALETTEENTRY));
    lp->palVersion = 0x300; lp->palNumEntries = (WORD)n; memcpy(lp->palPalEntry, e, n * sizeof(PALETTEENTRY));
    HPALETTE p = U(CreatePalette)(lp); free(lp); return p;
}
static const PALETTEENTRY p5[5] = { {255,0,0,0}, {0,255,0,0}, {0,0,255,0}, {255,255,255,0}, {0,0,0,0} };
#define C(r, g, b) ((ULONG)RGB(r, g, b))

/* name, colour, expected index  */
#define NROWS(X) \
 X(exact_red,    C(255,0,0),     0u) \
 X(exact_green,  C(0,255,0),     1u) \
 X(exact_blue,   C(0,0,255),     2u) \
 X(exact_white,  C(255,255,255), 3u) \
 X(exact_black,  C(0,0,0),       4u) \
 X(near_red,     C(250,10,10),   0u) \
 X(near_green,   C(10,240,10),   1u) \
 X(near_blue,    C(5,5,250),     2u) \
 X(near_white,   C(240,240,250), 3u) \
 X(near_black,   C(10,10,10),    4u) \
 X(mid_gray,     C(128,128,128), 3u) \
 X(dark_gray,    C(100,100,100), 4u) \
 X(yellow,       C(255,255,0),   0u) \
 X(flag_pal_rgb_red, 0x02000000u | C(255,0,0), 0u) \
 X(flag_pal_rgb_blue, 0x02000000u | C(0,0,255), 2u) \
 X(flag_explicit_green, 0x01000000u | C(0,255,0), 0u) \
 X(flag_03_white, 0x03000000u | C(255,255,255), 0u) \
 X(flag_ff_black, 0xFF000000u | C(0,0,0), 0u) \
 X(flag_ff_red,   0xFF000000u | C(255,0,0), 0u) \
 X(index_0,      PALETTEINDEX(0), 0u) \
 X(index_2,      PALETTEINDEX(2), 2u) \
 X(index_4,      PALETTEINDEX(4), 4u) \
 X(index_5,      PALETTEINDEX(5), 0u) \
 X(index_99,     PALETTEINDEX(99), 0u) \
 X(index_max16,  PALETTEINDEX(0xFFFF), 0u) \
 X(all_ones,     0xFFFFFFFFu, 0u) \
 X(clr_invalid_like, 0xFFFFFFFFu & ~1u, 0u)

struct nrow { const char *nm; ULONG cr; unsigned exp; };
#define NS_(nm, cr, e) { #nm, cr, e },
static const struct nrow nrows[] = { NROWS(NS_) };
static void near_run(const struct nrow *r) {
    REQUIRE(UN(NtGdiGetNearestPaletteIndex)); REQUIRE(U(GetNearestPaletteIndex));
    HPALETTE p = mkpal(p5, 5); REQUIRE(p);
    SetLastError(0); ULONG a = UN(NtGdiGetNearestPaletteIndex)(p, r->cr); DWORD e = GetLastError();
    ULONG b = U(GetNearestPaletteIndex)(p, r->cr);
    xb_val(r->nm, a, e, r->exp, 0);
    CHECKF(a == b, "%s: Nt stub %lu, gdi32 %lu", r->nm, (unsigned long)a, (unsigned long)b);
    U(DeleteObject)(p);
}
XB_GROUP(nearest_plain_colors, nrows, near_run, "exact|near|mid|dark|yellow")
XB_GROUP(nearest_colorref_flag_bytes, nrows, near_run, "flag|all_ones|clr_invalid")
XB_GROUP(nearest_palette_index_form, nrows, near_run, "index")

/* The handle matrix: only a palette is accepted. Every other value yields CLR_INVALID (0xFFFFFFFF) on both routes. */
XT(nearest_handle_types) {
    REQUIRE(UN(NtGdiGetNearestPaletteIndex)); REQUIRE(U(GetNearestPaletteIndex));
    XHS(x);
    for (int k = 0; k < XH_COUNT; k++) {
        SetLastError(0); ULONG a = UN(NtGdiGetNearestPaletteIndex)(x.h[k], C(1,2,3));
        ULONG b = U(GetNearestPaletteIndex)((HPALETTE)x.h[k], C(1,2,3));
        if (k == XH_PALETTE) { CHECKF(a == 0 && b == 0, "one-entry palette: nt %lu gdi %lu", (unsigned long)a, (unsigned long)b); }
        else { CHECKF(a == BAD && b == BAD, "%s: nt 0x%lx gdi 0x%lx", xh_nm[k], (unsigned long)a, (unsigned long)b); }
    }
    xhs_close(&x);
}
XT(nearest_deleted_palette) {
    REQUIRE(UN(NtGdiGetNearestPaletteIndex)); HPALETTE p = mkpal(p5, 5); REQUIRE(p);
    CHECK(UN(NtGdiGetNearestPaletteIndex)(p, C(0,255,0)) == 1);
    U(DeleteObject)(p);
    CHECK(UN(NtGdiGetNearestPaletteIndex)(p, C(0,255,0)) == BAD);
    CHECK(U(GetNearestPaletteIndex)(p, C(0,255,0)) == BAD);
}
XT(nearest_stock_default_palette) {
    REQUIRE(UN(NtGdiGetNearestPaletteIndex)); REQUIRE(U(GetPaletteEntries));
    HPALETTE p = (HPALETTE)U(GetStockObject)(DEFAULT_PALETTE); REQUIRE(p);
    UINT n = U(GetPaletteEntries)(p, 0, 0, NULL);
    CHECKF(n == 20, "default palette has %u entries", n);
    PALETTEENTRY e[20]; CHECK(U(GetPaletteEntries)(p, 0, 20, e) == 20);
    CHECK(UN(NtGdiGetNearestPaletteIndex)(p, C(0,0,0)) == 0);
    CHECK(UN(NtGdiGetNearestPaletteIndex)(p, C(255,255,255)) == 19);
}

/* Relation over a 256-entry palette: the returned index minimises the distance among the palette entries. */
static unsigned dist(PALETTEENTRY e, ULONG cr) {
    int dr = e.peRed - GetRValue(cr), dg = e.peGreen - GetGValue(cr), db = e.peBlue - GetBValue(cr);
    return (unsigned)(dr * dr + dg * dg + db * db);
}
static void minimal_distance(unsigned n, unsigned seed, int viaNt) {
    REQUIRE(UN(NtGdiGetNearestPaletteIndex)); REQUIRE(U(GetPaletteEntries));
    PALETTEENTRY e[256]; unsigned s = seed;
    for (unsigned i = 0; i < n; i++) { s = s * 1664525u + 1013904223u; e[i].peRed = (BYTE)(s >> 24); e[i].peGreen = (BYTE)(s >> 16); e[i].peBlue = (BYTE)(s >> 8); e[i].peFlags = 0; }
    HPALETTE p = mkpal(e, n); REQUIRE(p);
    PALETTEENTRY back[256]; CHECK(U(GetPaletteEntries)(p, 0, n, back) == n);
    int bad = 0, oob = 0;
    for (int t = 0; t < 400; t++) {
        s = s * 1664525u + 1013904223u; ULONG cr = s & 0xFFFFFF;
        ULONG i = viaNt ? UN(NtGdiGetNearestPaletteIndex)(p, cr) : U(GetNearestPaletteIndex)(p, cr);
        if (i >= n) { oob++; continue; }
        unsigned best = ~0u; for (unsigned j = 0; j < n; j++) { unsigned d = dist(back[j], cr); if (d < best) best = d; }
        if (dist(back[i], cr) != best) bad++;
    }
    CHECKF(oob == 0, "%d indices outside the palette", oob);
    CHECKF(bad == 0, "%d of 400 colours: returned entry is not a closest one", bad);
    U(DeleteObject)(p);
}
XT(nearest_minimal_distance_256_nt)  { minimal_distance(256, 1, 1); }
XT(nearest_minimal_distance_256_gdi) { minimal_distance(256, 2, 0); }
XT(nearest_minimal_distance_17_nt)   { minimal_distance(17, 3, 1); }
XT(nearest_minimal_distance_2_nt)    { minimal_distance(2, 4, 1); }
XT(nearest_minimal_distance_1_nt)    { minimal_distance(1, 5, 1); }

/* An exact palette colour is found at its own index when the palette holds no duplicate of it. */
XT(nearest_exact_entry_found_at_own_index) {
    REQUIRE(UN(NtGdiGetNearestPaletteIndex));
    PALETTEENTRY e[64]; for (unsigned i = 0; i < 64; i++) { e[i].peRed = (BYTE)(i * 4); e[i].peGreen = (BYTE)(255 - i * 3); e[i].peBlue = (BYTE)(i * 2 + 7); e[i].peFlags = 0; }
    HPALETTE p = mkpal(e, 64); REQUIRE(p); int bad = 0;
    for (unsigned i = 0; i < 64; i++) if (UN(NtGdiGetNearestPaletteIndex)(p, RGB(e[i].peRed, e[i].peGreen, e[i].peBlue)) != i) bad++;
    CHECKF(bad == 0, "%d of 64 exact colours not found at their index", bad);
    U(DeleteObject)(p);
}
XT(nearest_follows_set_palette_entries) {
    REQUIRE(UN(NtGdiGetNearestPaletteIndex)); REQUIRE(U(SetPaletteEntries));
    HPALETTE p = mkpal(p5, 5); REQUIRE(p);
    PALETTEENTRY n = { 17, 99, 201, 0 };
    CHECK(U(SetPaletteEntries)(p, 3, 1, &n) == 1);
    CHECK(UN(NtGdiGetNearestPaletteIndex)(p, RGB(17, 99, 201)) == 3);
    CHECK(UN(NtGdiGetNearestPaletteIndex)(p, RGB(17, 99, 200)) == 3);
    PALETTEENTRY w = { 255, 255, 255, 0 };
    CHECK(U(SetPaletteEntries)(p, 3, 1, &w) == 1);
    CHECK(UN(NtGdiGetNearestPaletteIndex)(p, RGB(255, 255, 255)) == 3);
    CHECK(UN(NtGdiGetNearestPaletteIndex)(p, RGB(17, 99, 201)) != 3);
    PALETTEENTRY m = { 40, 40, 40, 0 };
    CHECK(U(SetPaletteEntries)(p, 0, 1, &m) == 1);
    CHECK(UN(NtGdiGetNearestPaletteIndex)(p, RGB(40, 40, 40)) == 0);
    U(DeleteObject)(p);
}
XT(nearest_follows_resize_palette) {
    REQUIRE(UN(NtGdiGetNearestPaletteIndex)); REQUIRE(U(ResizePalette)); REQUIRE(U(SetPaletteEntries));
    HPALETTE p = mkpal(p5, 5); REQUIRE(p);
    CHECK(U(ResizePalette)(p, 8));
    PALETTEENTRY n = { 50, 120, 230, 0 };
    CHECK(U(SetPaletteEntries)(p, 7, 1, &n) == 1);
    CHECK(UN(NtGdiGetNearestPaletteIndex)(p, RGB(50, 120, 230)) == 7);
    CHECK(U(ResizePalette)(p, 3));
    ULONG i = UN(NtGdiGetNearestPaletteIndex)(p, RGB(50, 120, 230));
    CHECKF(i < 3, "index %lu after shrinking to 3 entries", (unsigned long)i);
    U(DeleteObject)(p);
}
XT(nearest_nt_equals_gdi_on_random_colors) {
    REQUIRE(UN(NtGdiGetNearestPaletteIndex));
    HPALETTE p = mkpal(p5, 5); REQUIRE(p); unsigned s = 77; int bad = 0;
    for (int t = 0; t < 300; t++) { s = s * 1664525u + 1013904223u; ULONG cr = s; if (UN(NtGdiGetNearestPaletteIndex)(p, cr) != U(GetNearestPaletteIndex)(p, cr)) bad++; }
    CHECKF(bad == 0, "%d of 300 colours differ between the routes", bad);
    U(DeleteObject)(p);
}
