/* Logical palettes: gdi32full!CreatePalette -> NtGdiCreatePaletteInternal (win32kfull: version must be 0x300, entry
 * count 1..0x10000), Get/SetPaletteEntries and AnimatePalette -> NtGdiDoPalette (iFunc dispatch table),
 * ResizePalette -> NtGdiResizePalette (count 1..0x400), object read-back -> the kernel object query palette branch (a WORD
 * holding the entry count, needs a >= 2 byte buffer). */
#include "pb.h"

static HPALETTE mkpal(unsigned n) {
    LOGPALETTE *lp = pb_mkpal(n, 0x300);
    HPALETTE h = lp ? U(CreatePalette)(lp) : NULL;
    free(lp);
    return h;
}
static int palcount(HGDIOBJ h) { WORD w = 0xCCCC; int r = pb_getobj(h, sizeof w, &w); return r == 2 ? (int)w : -1; }

TEST(palettes_create_counts) {
    PB_GDI(); REQUIRE(U(CreatePalette));
    static const unsigned n[] = { 1, 2, 3, 16, 20, 255, 256, 257, 512, 1000, 1024, 4096 };
    for (size_t i = 0; i < sizeof n / sizeof n[0]; i++) {
        HPALETTE p = mkpal(n[i]);
        CHECKF(p != NULL, "n=%u err %lu", n[i], GetLastError());
        if (!p) continue;
        CHECKF(palcount(p) == (int)n[i], "n=%u count %d", n[i], palcount(p)); /* *(WORD*)buf = entry count (+0x1c) */
        CHECK(PB_T7(p) == T7_PAL);
        CHECK(U(GetObjectType)(p) == OBJ_PAL);
        CHECK(pb_del(p));
    }
}
TEST(palettes_create_null) {
    PB_GDI(); REQUIRE(U(CreatePalette));
    SetLastError(0);
    CHECK(U(CreatePalette)(NULL) == NULL);
    EXPECT_ERR(ERROR_INVALID_PARAMETER); /* gdi32full!CreatePalette: SetLastError(0x57) */
}
TEST(palettes_create_bad_version) {
    PB_GDI(); REQUIRE(U(CreatePalette));
    static const WORD vers[] = { 0, 1, 0x2ff, 0x301, 0x300 ^ 0x100, 0x3000, 0xFFFF };
    for (size_t i = 0; i < sizeof vers / sizeof vers[0]; i++) {
        LOGPALETTE *lp = pb_mkpal(4, vers[i]);
        REQUIRE(lp);
        HPALETTE p = U(CreatePalette)(lp);
        CHECKF(p == NULL, "version 0x%x", vers[i]); /* NtGdiCreatePaletteInternal: `*plp != 0x300` -> NULL */
        if (p) pb_del(p);
        free(lp);
    }
}
TEST(palettes_create_zero_entries) {
    PB_GDI(); REQUIRE(U(CreatePalette));
    LOGPALETTE lp = { 0x300, 0, { { 1, 2, 3, 0 } } };
    CHECK(U(CreatePalette)(&lp) == NULL); /* palNumEntries == 0 is rejected before bCreatePalette */
}
TEST(palettes_nt_create_count_limit) {
    REQUIRE(UN(NtGdiCreatePaletteInternal)); PB_GDI();
    LOGPALETTE *lp = pb_mkpal(8, 0x300);
    REQUIRE(lp);
    /* the entry count is a separate argument: values > 0x10000 are refused before the probe */
    CHECK(UN(NtGdiCreatePaletteInternal)(lp, 0x10001) == NULL);
    CHECK(UN(NtGdiCreatePaletteInternal)(lp, 0x7fffffff) == NULL);
    CHECK(UN(NtGdiCreatePaletteInternal)(lp, 0xffffffff) == NULL);
    CHECK(UN(NtGdiCreatePaletteInternal)(lp, 0) == NULL);
    HANDLE p = UN(NtGdiCreatePaletteInternal)(lp, 8);
    CHECK(p != NULL);
    if (p) { CHECK(palcount(p) == 8); pb_del(p); }
    free(lp);
}
TEST(palettes_nt_create_max_entries) {
    REQUIRE(UN(NtGdiCreatePaletteInternal)); PB_GDI();
    LOGPALETTE *lp = pb_mkpal(0x10000, 0x300);
    REQUIRE(lp);
    HANDLE p = UN(NtGdiCreatePaletteInternal)(lp, 0x10000);
    t_note("palette with 0x10000 entries -> %p", p);
    if (p) { t_note("count %d", palcount(p)); pb_del(p); }
    free(lp);
}
TEST(palettes_create_entries_roundtrip) {
    PB_GDI(); REQUIRE(U(CreatePalette)); REQUIRE(U(GetPaletteEntries));
    enum { N = 40 };
    LOGPALETTE *lp = pb_mkpal(N, 0x300);
    REQUIRE(lp);
    static const BYTE flags[] = { 0, PC_RESERVED, PC_EXPLICIT, PC_NOCOLLAPSE };
    for (int i = 0; i < N; i++) lp->palPalEntry[i].peFlags = flags[i & 3];
    HPALETTE p = U(CreatePalette)(lp);
    REQUIRE(p);
    PALETTEENTRY out[N + 2];
    memset(out, 0xCC, sizeof out);
    CHECK(U(GetPaletteEntries)(p, 0, N, out) == (UINT)N);
    for (int i = 0; i < N; i++) {
        CHECKF(out[i].peRed == lp->palPalEntry[i].peRed && out[i].peGreen == lp->palPalEntry[i].peGreen && out[i].peBlue == lp->palPalEntry[i].peBlue, "entry %d", i);
        CHECKF(out[i].peFlags == lp->palPalEntry[i].peFlags, "entry %d flags %u", i, out[i].peFlags);
    }
    CHECK(out[N].peRed == 0xCC); /* nothing written past the requested range */
    pb_del(p); free(lp);
}
TEST(palettes_cell_state) {
    PB_GDI(); PB_TABLE(); REQUIRE(U(CreatePalette));
    HPALETTE p = mkpal(8);
    REQUIRE(p);
    struct pb_cell c;
    CHECK(pb_cell_of(p, &c));
    CHECK(c.objt == 8);
    CHECK((c.owner & ~1u) == PB_PID());
    CHECK(c.upper == (unsigned short)((ULONG_PTR)p >> 16));
    CHECK(!PB_STOCKED(p));
    pb_del(p);
}
TEST(palettes_getobject_protocol) {
    PB_GDI(); REQUIRE(U(CreatePalette));
    HPALETTE p = mkpal(37);
    REQUIRE(p);
    WORD w; unsigned char buf[8];
    CHECK(pb_getobj(p, 2, NULL) == 2);
    CHECK(pb_getobj(p, 0, NULL) == 2);
    CHECK(pb_getobj(p, 0, &w) == 0);
    CHECK(pb_getobj(p, 1, &w) == 0); /* the kernel object query: count < 2 -> 0 */
    memset(buf, 0xAB, sizeof buf);
    CHECK(pb_getobj(p, 2, buf) == 2);
    CHECK(*(WORD *)buf == 37 && buf[2] == 0xAB);
    memset(buf, 0xAB, sizeof buf);
    CHECK(pb_getobj(p, 8, buf) == 2);
    CHECK(*(WORD *)buf == 37 && buf[2] == 0xAB);
    pb_del(p);
}
TEST(palettes_nt_getobject_direct) {
    REQUIRE(UN(NtGdiExtGetObjectW)); PB_GDI();
    HPALETTE p = mkpal(12);
    REQUIRE(p);
    WORD w = 0;
    CHECK(UN(NtGdiExtGetObjectW)(p, 2, &w) == 2 && w == 12);
    CHECK(UN(NtGdiExtGetObjectW)(p, 1, &w) == 0);
    CHECK(UN(NtGdiExtGetObjectW)(p, 0, NULL) == 2);
    pb_del(p);
}
TEST(palettes_delete_lifetime) {
    PB_GDI();
    HPALETTE p = mkpal(4);
    REQUIRE(p);
    CHECK(pb_del(p));
    WORD w;
    CHECK(pb_getobj(p, 2, &w) == 0);
    CHECK(U(GetObjectType)(p) == OBJ_PAL);       /* a freed cell keeps its upper word: the handle's type is still reported */
    CHECK(!pb_del(p));
}
TEST(palettes_gdi_count_delta) {
    PB_GDI(); REQUIRE(U(GetGuiResources));
    enum { N = 25 };
    HPALETTE h[N];
    DWORD g0 = U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS);
    for (int i = 0; i < N; i++) { h[i] = mkpal(2 + i); REQUIRE(h[i]); }
    CHECK(U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS) == g0 + N);
    for (int i = 0; i < N; i++) pb_del(h[i]);
    CHECK(U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS) == g0);
}
TEST(palettes_get_entries_null_buffer_returns_size) {
    PB_GDI(); REQUIRE(U(GetPaletteEntries));
    HPALETTE p = mkpal(23);
    REQUIRE(p);
    /* documented: with lppe == NULL the return value is the number of entries in the palette */
    CHECK(U(GetPaletteEntries)(p, 0, 0, NULL) == 23);
    CHECK(U(GetPaletteEntries)(p, 0, 5, NULL) == 23);
    pb_del(p);
}
TEST(palettes_get_entries_ranges) {
    PB_GDI(); REQUIRE(U(GetPaletteEntries));
    HPALETTE p = mkpal(10);
    REQUIRE(p);
    PALETTEENTRY e[16];
    memset(e, 0xCC, sizeof e);
    CHECK(U(GetPaletteEntries)(p, 3, 4, e) == 4);
    CHECK(e[0].peRed == (BYTE)(3 * 7 + 1) && e[3].peRed == (BYTE)(6 * 7 + 1));
    CHECK(e[4].peRed == 0xCC);
    memset(e, 0xCC, sizeof e);
    UINT r = U(GetPaletteEntries)(p, 8, 8, e); /* clamps to the palette end */
    t_note("GetPaletteEntries(start 8, count 8) of a 10 entry palette -> %u", r);
    r = U(GetPaletteEntries)(p, 10, 1, e);
    t_note("GetPaletteEntries(start == size) -> %u", r);
    r = U(GetPaletteEntries)(p, 100, 1, e);
    CHECK(r == 0);
    pb_del(p);
}
TEST(palettes_get_entries_invalid_handle) {
    PB_GDI(); REQUIRE(U(GetPaletteEntries));
    PALETTEENTRY e[2];
    CHECK(U(GetPaletteEntries)(NULL, 0, 2, e) == 0);
    CHECK(U(GetPaletteEntries)((HPALETTE)(ULONG_PTR)0x1234, 0, 2, e) == 0);
    HBRUSH b = U(CreateSolidBrush)(0);
    REQUIRE(b);
    CHECK(U(GetPaletteEntries)((HPALETTE)b, 0, 2, e) == 0);
    pb_del(b);
}
TEST(palettes_set_entries_roundtrip) {
    PB_GDI(); REQUIRE(U(SetPaletteEntries)); REQUIRE(U(GetPaletteEntries));
    HPALETTE p = mkpal(8);
    REQUIRE(p);
    PALETTEENTRY in[3] = { {250, 1, 2, PC_RESERVED}, {3, 240, 4, 0}, {5, 6, 230, PC_NOCOLLAPSE} }, out[8];
    CHECK(U(SetPaletteEntries)(p, 2, 3, in) == 3);
    CHECK(U(GetPaletteEntries)(p, 0, 8, out) == 8);
    for (int i = 0; i < 3; i++) {
        CHECKF(out[2 + i].peRed == in[i].peRed && out[2 + i].peGreen == in[i].peGreen && out[2 + i].peBlue == in[i].peBlue, "entry %d", 2 + i);
        CHECKF(out[2 + i].peFlags == in[i].peFlags, "flags %d = %u", 2 + i, out[2 + i].peFlags);
    }
    CHECK(out[0].peRed == 1 && out[1].peRed == 8 && out[5].peRed == (BYTE)(5 * 7 + 1)); /* untouched entries keep CreatePalette's values */
    pb_del(p);
}
TEST(palettes_set_entries_start_out_of_range) {
    PB_GDI(); REQUIRE(U(SetPaletteEntries));
    HPALETTE p = mkpal(4);
    REQUIRE(p);
    PALETTEENTRY in = { 1, 2, 3, 0 };
    CHECK(U(SetPaletteEntries)(p, 50, 1, &in) == 0);
    CHECK(U(SetPaletteEntries)(p, 0x10000, 1, &in) <= 1); /* gdi32full masks iStart with 0xffff, so this means index 0 */
    pb_del(p);
}
TEST(palettes_set_entries_clamped_to_size) {
    PB_GDI(); REQUIRE(U(SetPaletteEntries));
    HPALETTE p = mkpal(4);
    REQUIRE(p);
    PALETTEENTRY in[8]; memset(in, 7, sizeof in);
    UINT r = U(SetPaletteEntries)(p, 2, 8, in);
    t_note("SetPaletteEntries(start 2, count 8) on a 4 entry palette -> %u", r);
    CHECK(r <= 4);
    CHECK(palcount(p) == 4);
    pb_del(p);
}
TEST(palettes_set_entries_invalid_handle) {
    PB_GDI(); REQUIRE(U(SetPaletteEntries));
    PALETTEENTRY in = { 1, 2, 3, 0 };
    CHECK(U(SetPaletteEntries)(NULL, 0, 1, &in) == 0);
    CHECK(U(SetPaletteEntries)((HPALETTE)(ULONG_PTR)0x4321, 0, 1, &in) == 0);
}
TEST(palettes_set_entries_default_palette_refused) {
    PB_GDI(); REQUIRE(U(SetPaletteEntries));
    HGDIOBJ dp = U(GetStockObject)(DEFAULT_PALETTE);
    REQUIRE(dp);
    PALETTEENTRY in = { 1, 2, 3, 0 }, before, after;
    U(GetPaletteEntries)(dp, 0, 1, &before);
    UINT r = U(SetPaletteEntries)(dp, 0, 1, &in);
    U(GetPaletteEntries)(dp, 0, 1, &after);
    t_note("SetPaletteEntries(default palette) -> %u", r);
    if (r != 0) U(SetPaletteEntries)(dp, 0, 1, &before);
}
TEST(palettes_nt_dopalette_get_set) {
    REQUIRE(UN(NtGdiDoPalette)); PB_GDI();
    HPALETTE p = mkpal(6);
    REQUIRE(p);
    PALETTEENTRY in[2] = { {10, 20, 30, 0}, {40, 50, 60, 0} }, out[6];
    CHECK(UN(NtGdiDoPalette)(p, 1, 2, in, PAL_SET, TRUE) == 2);
    memset(out, 0, sizeof out);
    CHECK(UN(NtGdiDoPalette)(p, 0, 6, out, PAL_GET, FALSE) == 6);
    CHECK(out[1].peRed == 10 && out[2].peBlue == 60);
    CHECK(UN(NtGdiDoPalette)(p, 0, 6, NULL, PAL_GET, FALSE) == 6);
    pb_del(p);
}
TEST(palettes_nt_dopalette_bad_function) {
    REQUIRE(UN(NtGdiDoPalette)); PB_GDI();
    HPALETTE p = mkpal(4);
    REQUIRE(p);
    PALETTEENTRY e[4];
    /* NtGdiDoPalette before the dispatch table is read */
    for (ULONG f = 6; f < 12; f++) CHECKF(UN(NtGdiDoPalette)(p, 0, 4, e, f, FALSE) == 0, "iFunc %lu", f);
    CHECK(UN(NtGdiDoPalette)(p, 0, 4, e, 0xFFFFFFFF, FALSE) == 0);
    CHECK(UN(NtGdiDoPalette)(p, 0, 4, e, 0x80000000, FALSE) == 0);
    pb_del(p);
}
TEST(palettes_nt_dopalette_invalid_handle) {
    REQUIRE(UN(NtGdiDoPalette)); PB_GDI();
    PALETTEENTRY e[2];
    CHECK(UN(NtGdiDoPalette)(NULL, 0, 2, e, PAL_GET, FALSE) == 0);
    CHECK(UN(NtGdiDoPalette)((HANDLE)(ULONG_PTR)0x1234, 0, 2, e, PAL_GET, FALSE) == 0);
    CHECK(UN(NtGdiDoPalette)(NULL, 0, 2, e, PAL_SET, TRUE) == 0);
}
TEST(palettes_nt_dopalette_cap_16bit) {
    REQUIRE(UN(NtGdiDoPalette)); PB_GDI();
    HPALETTE p = mkpal(8);
    REQUIRE(p);
    PALETTEENTRY e[8];
 /* the syscall takes 16-bit iStart/cEntries */
    CHECK(UN(NtGdiDoPalette)(p, 0, 8, e, PAL_GET, FALSE) == 8);
    CHECK(UN(NtGdiDoPalette)(p, 0, 0x10008, e, PAL_GET, FALSE) == 8);
    pb_del(p);
}
TEST(palettes_get_system_entries_requires_dc) {
    PB_GDI(); REQUIRE(U(GetSystemPaletteEntries));
    PALETTEENTRY e[4];
    SetLastError(0);
    UINT r = U(GetSystemPaletteEntries)(NULL, 0, 4, e);
    t_note("GetSystemPaletteEntries(NULL dc) -> %u err %lu", r, GetLastError());
}
TEST(palettes_get_system_entries_nonpalette_device) {
    PB_GDI(); REQUIRE(U(GetSystemPaletteEntries)); REQUIRE(U(CreateDCW));
    if (pb_screen_has_palette()) { t_skip("palette-managed display"); return; }
    HDC dc = U(CreateDCW)(L"DISPLAY", NULL, NULL, NULL);
    REQUIRE(dc);
    PALETTEENTRY e[256];
    memset(e, 0xCC, sizeof e);
    /* gdi32full!GetSystemPaletteEntries without RC_PALETTE: fills the buffer from a built-in 256 entry table
     * (static colors at both ends, zeros between) but its return value stays 0 */
    UINT r = U(GetSystemPaletteEntries)(dc, 0, 256, e);
    CHECK(r == 0);
    CHECK(e[0].peRed == 0 && e[0].peGreen == 0 && e[0].peBlue == 0);
    CHECK(e[255].peRed == 255 && e[255].peGreen == 255 && e[255].peBlue == 255);
    CHECK(e[7].peRed == 192 && e[7].peGreen == 192 && e[7].peBlue == 192);
    CHECK(e[248].peRed == 128 && e[248].peGreen == 128 && e[248].peBlue == 128);
    for (int i = 10; i < 246; i++) { if (e[i].peRed || e[i].peGreen || e[i].peBlue) { CHECKF(0, "entry %d not zero", i); break; } }
    U(DeleteDC)(dc);
}
TEST(palettes_get_system_entries_ranges_nonpalette) {
    PB_GDI(); REQUIRE(U(GetSystemPaletteEntries)); REQUIRE(U(CreateDCW));
    if (pb_screen_has_palette()) { t_skip("palette-managed display"); return; }
    HDC dc = U(CreateDCW)(L"DISPLAY", NULL, NULL, NULL);
    REQUIRE(dc);
    PALETTEENTRY e[16];
    memset(e, 0xCC, sizeof e);
    CHECK(U(GetSystemPaletteEntries)(dc, 250, 100, e) == 0); /* copies min(256 - 250, 100) = 6 entries */
    CHECK(e[5].peRed == 255 && e[6].peRed == 0xCC);
    memset(e, 0xCC, sizeof e);
    CHECK(U(GetSystemPaletteEntries)(dc, 256, 4, e) == 0);
    CHECK(e[0].peRed == 0xCC); /* iStart >= 0x100: nothing copied */
    CHECK(U(GetSystemPaletteEntries)(dc, 0, 4, NULL) == 0);
    /* negative count short-circuits before the buffer is touched (an emulator without that check would overrun it) */
    if (!t_is_wine()) CHECK(U(GetSystemPaletteEntries)(dc, 0, (UINT)-1, e) == 0);
    U(DeleteDC)(dc);
}
TEST(palettes_system_palette_use_nonpalette) {
    PB_GDI(); REQUIRE(U(GetSystemPaletteUse)); REQUIRE(U(CreateDCW));
    if (pb_screen_has_palette()) { t_skip("palette-managed display"); return; }
    HDC dc = U(CreateDCW)(L"DISPLAY", NULL, NULL, NULL);
    REQUIRE(dc);
    /* NtGdiGetSystemPaletteUse: the device palette-managed bit is clear -> returns 0 (SYSPAL_ERROR) */
    CHECK(U(GetSystemPaletteUse)(dc) == SYSPAL_ERROR);
    if (UN(NtGdiGetSystemPaletteUse)) CHECK(UN(NtGdiGetSystemPaletteUse)(dc) == 0);
    U(DeleteDC)(dc);
}
TEST(palettes_system_palette_use_invalid_dc) {
    PB_GDI(); REQUIRE(U(GetSystemPaletteUse));
    CHECK(U(GetSystemPaletteUse)(NULL) == SYSPAL_ERROR);
    CHECK(U(GetSystemPaletteUse)((HDC)(ULONG_PTR)0x1234) == SYSPAL_ERROR);
}
TEST(palettes_set_system_palette_use_nonpalette) {
    PB_GDI(); REQUIRE(U(SetSystemPaletteUse)); REQUIRE(U(CreateDCW));
    if (pb_screen_has_palette()) { t_skip("palette-managed display"); return; }
    HDC dc = U(CreateDCW)(L"DISPLAY", NULL, NULL, NULL);
    REQUIRE(dc);
    UINT r = U(SetSystemPaletteUse)(dc, SYSPAL_STATIC);
    t_note("SetSystemPaletteUse on non-palette device -> %u", r);
    CHECK(U(GetSystemPaletteUse)(dc) == SYSPAL_ERROR);
    CHECK(U(SetSystemPaletteUse)(NULL, SYSPAL_STATIC) == SYSPAL_ERROR);
    U(DeleteDC)(dc);
}
TEST(palettes_realize_invalid_dc) {
    PB_GDI(); REQUIRE(U(RealizePalette));
    SetLastError(0);
    CHECK(U(RealizePalette)(NULL) == GDI_ERROR);
    EXPECT_ERR(ERROR_INVALID_HANDLE); /* pldcGet() == 0 -> GdiSetLastError(6); return 0xffffffff */
}
TEST(palettes_realize_nonpalette_dc) {
    PB_GDI(); REQUIRE(U(RealizePalette)); REQUIRE(U(CreateDCW));
    if (pb_screen_has_palette()) { t_skip("palette-managed display"); return; }
    HDC dc = U(CreateDCW)(L"DISPLAY", NULL, NULL, NULL);
    REQUIRE(dc);
    HPALETTE p = mkpal(16);
    REQUIRE(p);
    HPALETTE old = U(SelectPalette)(dc, p, FALSE);
    CHECK(old != NULL);
    CHECK(U(RealizePalette)(dc) == 0);
    if (old) U(SelectPalette)(dc, old, FALSE);
    pb_del(p);
    U(DeleteDC)(dc);
}
TEST(palettes_select_invalid_dc) {
    PB_GDI(); REQUIRE(U(SelectPalette));
    HPALETTE p = mkpal(4);
    REQUIRE(p);
    SetLastError(0);
    CHECK(U(SelectPalette)(NULL, p, FALSE) == NULL);
    EXPECT_ERR(ERROR_INVALID_HANDLE); /* gdi32full!SelectPalette: pldcGet == 0 -> GdiSetLastError(6) */
    pb_del(p);
}
TEST(palettes_select_returns_previous) {
    PB_GDI(); REQUIRE(U(SelectPalette)); REQUIRE(U(GetCurrentObject));
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    HPALETTE a = mkpal(4), b = mkpal(6);
    if (!a || !b) { U(DeleteDC)(dc); REQUIRE(a); REQUIRE(b); }
    HGDIOBJ def = U(GetStockObject)(DEFAULT_PALETTE);
    CHECK(U(GetCurrentObject)(dc, OBJ_PAL) == def);
    CHECK(U(SelectPalette)(dc, a, FALSE) == (HPALETTE)def);
    CHECK(U(GetCurrentObject)(dc, OBJ_PAL) == a);
    CHECK(U(SelectPalette)(dc, b, TRUE) == a);
    CHECK(U(SelectPalette)(dc, (HPALETTE)def, FALSE) == b);
    CHECK(U(GetCurrentObject)(dc, OBJ_PAL) == def);
    pb_del(a); pb_del(b);
    U(DeleteDC)(dc);
}
TEST(palettes_select_non_palette_handle) {
    PB_GDI(); REQUIRE(U(SelectPalette));
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    HBRUSH b = U(CreateSolidBrush)(0);
    if (!b) { U(DeleteDC)(dc); REQUIRE(b); }
    HPALETTE r = U(SelectPalette)(dc, (HPALETTE)b, FALSE);
    t_note("SelectPalette(dc, brush) -> %p", (void *)r);
    CHECK(U(GetCurrentObject)(dc, OBJ_PAL) == U(GetStockObject)(DEFAULT_PALETTE));
    pb_del(b);
    U(DeleteDC)(dc);
}
TEST(palettes_delete_selected_palette_behaviour) {
    PB_GDI(); REQUIRE(U(SelectPalette));
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    HPALETTE p = mkpal(4);
    if (!p) { U(DeleteDC)(dc); REQUIRE(p); }
    HPALETTE old = U(SelectPalette)(dc, p, FALSE);
    BOOL r = pb_del(p);
    t_note("DeleteObject(palette selected in a DC) = %d", r);
    U(SelectPalette)(dc, old, FALSE);
    if (!r) CHECK(pb_del(p));
    U(DeleteDC)(dc);
}
TEST(palettes_stock_default_palette) {
    PB_GDI(); REQUIRE(U(GetPaletteEntries));
    HGDIOBJ dp = U(GetStockObject)(DEFAULT_PALETTE);
    CHECK(dp != NULL);
    if (!dp) return;
    CHECK(PB_STOCKED(dp));
    CHECK(PB_T7(dp) == T7_PAL);
    CHECK(U(GetObjectType)(dp) == OBJ_PAL);
    CHECK(palcount(dp) == 20);
    CHECK(dp == U(GetStockObject)(DEFAULT_PALETTE));
}
TEST(palettes_stock_default_palette_entries) {
    PB_GDI(); REQUIRE(U(GetPaletteEntries));
    HGDIOBJ dp = U(GetStockObject)(DEFAULT_PALETTE);
    REQUIRE(dp);
    /* the 20 static system colors */
    static const BYTE exp[20][3] = {
        {0,0,0}, {128,0,0}, {0,128,0}, {128,128,0}, {0,0,128}, {128,0,128}, {0,128,128}, {192,192,192},
        {192,220,192}, {166,202,240}, {255,251,240}, {160,160,164}, {128,128,128}, {255,0,0}, {0,255,0},
        {255,255,0}, {0,0,255}, {255,0,255}, {0,255,255}, {255,255,255} };
    PALETTEENTRY e[20];
    CHECK(U(GetPaletteEntries)((HPALETTE)dp, 0, 20, e) == 20);
    for (int i = 0; i < 20; i++) CHECKF(e[i].peRed == exp[i][0] && e[i].peGreen == exp[i][1] && e[i].peBlue == exp[i][2], "static color %d = (%u,%u,%u)", i, e[i].peRed, e[i].peGreen, e[i].peBlue);
    CHECK(U(GetPaletteEntries)((HPALETTE)dp, 0, 0, NULL) == 20);
}
TEST(palettes_resize_grow_shrink) {
    PB_GDI(); REQUIRE(U(ResizePalette)); REQUIRE(U(GetPaletteEntries));
    HPALETTE p = mkpal(8);
    REQUIRE(p);
    PALETTEENTRY before[8], after[32];
    U(GetPaletteEntries)(p, 0, 8, before);
    CHECK(U(ResizePalette)(p, 32));
    CHECK(palcount(p) == 32);
    CHECK(U(GetPaletteEntries)(p, 0, 32, after) == 32);
    CHECK(!memcmp(before, after, sizeof before)); /* NtGdiResizePalette copies min(old, new) entries */
    CHECK(U(ResizePalette)(p, 3));
    CHECK(palcount(p) == 3);
    PALETTEENTRY small[3];
    CHECK(U(GetPaletteEntries)(p, 0, 3, small) == 3);
    CHECK(!memcmp(before, small, sizeof small));
    CHECK(U(ResizePalette)(p, 3));
    CHECK(palcount(p) == 3);
    pb_del(p);
}
TEST(palettes_resize_limits) {
    PB_GDI(); REQUIRE(U(ResizePalette));
    if (t_is_wine()) { t_skip("emulator ResizePalette frees the palette entry array twice"); return; }
    HPALETTE p = mkpal(8);
    REQUIRE(p);
    /* NtGdiResizePalette: `n - 1 < 0x400` */
    CHECK(!U(ResizePalette)(p, 0));
    CHECK(!U(ResizePalette)(p, 0x401));
    if (!t_is_wine()) { /* an emulator without the 0x400 cap would try to allocate gigabytes */
        CHECK(!U(ResizePalette)(p, 0x10000));
        CHECK(!U(ResizePalette)(p, 0xFFFFFFFF));
    }
    CHECK(palcount(p) == 8);
    CHECK(U(ResizePalette)(p, 0x400));
    CHECK(palcount(p) == 0x400);
    CHECK(U(ResizePalette)(p, 1));
    CHECK(palcount(p) == 1);
    pb_del(p);
}
TEST(palettes_resize_invalid_and_stock) {
    PB_GDI(); REQUIRE(U(ResizePalette));
    if (t_is_wine()) { t_skip("emulator ResizePalette lacks the stock-palette and 0x400 rules"); return; }
    CHECK(!U(ResizePalette)(NULL, 4));
    CHECK(!U(ResizePalette)((HPALETTE)(ULONG_PTR)0x1234, 4));
    HGDIOBJ dp = U(GetStockObject)(DEFAULT_PALETTE);
    REQUIRE(dp);
    CHECK(!U(ResizePalette)((HPALETTE)dp, 30)); /* `local_res18 != ppalDefault` */
    CHECK(palcount(dp) == 20);
    HBRUSH b = U(CreateSolidBrush)(0);
    REQUIRE(b);
    CHECK(!U(ResizePalette)((HPALETTE)b, 4));
    pb_del(b);
}
TEST(palettes_nt_resize_direct) {
    REQUIRE(UN(NtGdiResizePalette)); PB_GDI();
    if (t_is_wine()) { t_skip("emulator resize rules differ"); return; }
    HPALETTE p = mkpal(5);
    REQUIRE(p);
    CHECK(UN(NtGdiResizePalette)(p, 10));
    CHECK(palcount(p) == 10);
    CHECK(!UN(NtGdiResizePalette)(p, 0));
    CHECK(!UN(NtGdiResizePalette)(p, 0x401));
    pb_del(p);
}
TEST(palettes_animate_only_reserved) {
    PB_GDI(); REQUIRE(U(AnimatePalette)); REQUIRE(U(GetPaletteEntries));
    LOGPALETTE *lp = pb_mkpal(4, 0x300);
    REQUIRE(lp);
    lp->palPalEntry[1].peFlags = PC_RESERVED;
    lp->palPalEntry[2].peFlags = PC_RESERVED;
    HPALETTE p = U(CreatePalette)(lp);
    REQUIRE(p);
    PALETTEENTRY nw[4], got[4];
    for (int i = 0; i < 4; i++) { nw[i].peRed = 200 + i; nw[i].peGreen = 100 + i; nw[i].peBlue = 50 + i; nw[i].peFlags = PC_RESERVED; }
    BOOL r = U(AnimatePalette)(p, 0, 4, nw);
    t_note("AnimatePalette -> %d", r);
    CHECK(U(GetPaletteEntries)(p, 0, 4, got) == 4);
    /* documented: only entries created with PC_RESERVED are changed */
    CHECK(got[0].peRed == lp->palPalEntry[0].peRed);
    CHECK(got[3].peRed == lp->palPalEntry[3].peRed);
    CHECK(got[1].peRed == 201 && got[1].peGreen == 101 && got[1].peBlue == 51);
    CHECK(got[2].peRed == 202 && got[2].peGreen == 102 && got[2].peBlue == 52);
    pb_del(p); free(lp);
}
TEST(palettes_animate_invalid_handle) {
    PB_GDI(); REQUIRE(U(AnimatePalette));
    PALETTEENTRY e = { 1, 2, 3, PC_RESERVED };
    CHECK(!U(AnimatePalette)(NULL, 0, 1, &e));
    CHECK(!U(AnimatePalette)((HPALETTE)(ULONG_PTR)0x1234, 0, 1, &e));
}
TEST(palettes_nearest_index_exact) {
    PB_GDI(); REQUIRE(U(GetNearestPaletteIndex));
    LOGPALETTE *lp = pb_mkpal(5, 0x300);
    REQUIRE(lp);
    static const BYTE c[5][3] = { {255,0,0}, {0,255,0}, {0,0,255}, {255,255,255}, {0,0,0} };
    for (int i = 0; i < 5; i++) { lp->palPalEntry[i].peRed = c[i][0]; lp->palPalEntry[i].peGreen = c[i][1]; lp->palPalEntry[i].peBlue = c[i][2]; lp->palPalEntry[i].peFlags = 0; }
    HPALETTE p = U(CreatePalette)(lp);
    REQUIRE(p);
    for (int i = 0; i < 5; i++) CHECKF(U(GetNearestPaletteIndex)(p, RGB(c[i][0], c[i][1], c[i][2])) == (UINT)i, "exact %d", i);
    CHECK(U(GetNearestPaletteIndex)(p, RGB(250, 10, 10)) == 0);
    CHECK(U(GetNearestPaletteIndex)(p, RGB(10, 240, 10)) == 1);
    CHECK(U(GetNearestPaletteIndex)(p, RGB(5, 5, 250)) == 2);
    CHECK(U(GetNearestPaletteIndex)(p, RGB(240, 240, 250)) == 3);
    CHECK(U(GetNearestPaletteIndex)(p, RGB(3, 2, 1)) == 4);
    /* only bit 24 selects PALETTEINDEX handling; the other high bits (PALETTERGB's 0x02) do not */
    CHECK(U(GetNearestPaletteIndex)(p, PALETTERGB(0, 255, 0)) == 1);
    pb_del(p); free(lp);
}
TEST(palettes_nearest_index_paletteindex) {
    PB_GDI(); REQUIRE(U(GetNearestPaletteIndex));
    HPALETTE p = mkpal(6);
    REQUIRE(p);
    for (UINT i = 0; i < 6; i++) CHECKF(U(GetNearestPaletteIndex)(p, PALETTEINDEX(i)) == i, "PALETTEINDEX(%u)", i);
    /* `-(idx < count) & (cr & 0xffff)`: an index outside the palette collapses to 0 */
    CHECK(U(GetNearestPaletteIndex)(p, PALETTEINDEX(6)) == 0);
    CHECK(U(GetNearestPaletteIndex)(p, PALETTEINDEX(500)) == 0);
    CHECK(U(GetNearestPaletteIndex)(p, 0x01FF0003) == 3); /* only the low 16 bits are the index */
    pb_del(p);
}
TEST(palettes_nearest_index_invalid_handle) {
    PB_GDI(); REQUIRE(U(GetNearestPaletteIndex));
    SetLastError(0);
    CHECK(U(GetNearestPaletteIndex)(NULL, RGB(1, 2, 3)) == CLR_INVALID);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    SetLastError(0);
    CHECK(U(GetNearestPaletteIndex)((HPALETTE)(ULONG_PTR)0x1234, 0) == CLR_INVALID);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    if (UN(NtGdiGetNearestPaletteIndex)) {
        SetLastError(0);
        CHECK(UN(NtGdiGetNearestPaletteIndex)(NULL, 0) == 0xFFFFFFFF);
        EXPECT_ERR(ERROR_INVALID_HANDLE); /* NtGdiGetNearestPaletteIndex: the palette lookup fails -> EngSetLastError(6) */
    }
}
TEST(palettes_nearest_index_default_palette) {
    PB_GDI(); REQUIRE(U(GetNearestPaletteIndex));
    HGDIOBJ dp = U(GetStockObject)(DEFAULT_PALETTE);
    REQUIRE(dp);
    CHECK(U(GetNearestPaletteIndex)((HPALETTE)dp, RGB(0, 0, 0)) == 0);
    CHECK(U(GetNearestPaletteIndex)((HPALETTE)dp, RGB(255, 255, 255)) == 19);
    CHECK(U(GetNearestPaletteIndex)((HPALETTE)dp, RGB(255, 0, 0)) == 13);
    CHECK(U(GetNearestPaletteIndex)((HPALETTE)dp, RGB(0, 0, 255)) == 16);
}
TEST(palettes_halftone_palette) {
    PB_GDI(); REQUIRE(U(CreateHalftonePalette)); REQUIRE(U(CreateDCW));
    HDC dc = U(CreateDCW)(L"DISPLAY", NULL, NULL, NULL);
    REQUIRE(dc);
    HPALETTE p = U(CreateHalftonePalette)(dc);
    CHECK(p != NULL);
    if (p) {
        CHECK(U(GetObjectType)(p) == OBJ_PAL);
        CHECK(PB_T7(p) == T7_PAL);
        t_note("halftone palette entries: %d", palcount(p));
        pb_del(p);
    }
    U(DeleteDC)(dc);
}
TEST(palettes_halftone_palette_entry_count_note) {
    PB_GDI(); REQUIRE(U(CreateHalftonePalette)); REQUIRE(U(CreateDCW));
    HDC dc = U(CreateDCW)(L"DISPLAY", NULL, NULL, NULL);
    REQUIRE(dc);
    HPALETTE p = U(CreateHalftonePalette)(dc);
    REQUIRE(p);
    int n = palcount(p);
    CHECK(n > 0);
    t_note("halftone entries %d", n);
    pb_del(p); U(DeleteDC)(dc);
}
TEST(palettes_update_colors_nonpalette) {
    PB_GDI(); REQUIRE(U(UpdateColors)); REQUIRE(U(CreateDCW));
    HDC dc = U(CreateDCW)(L"DISPLAY", NULL, NULL, NULL);
    REQUIRE(dc);
    BOOL r = U(UpdateColors)(dc);
    t_note("UpdateColors on display DC -> %d", r);
    t_note("UpdateColors(NULL) -> %d", U(UpdateColors)(NULL));
    U(DeleteDC)(dc);
}
