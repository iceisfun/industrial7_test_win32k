/* NtGdiDoPalette reached through its userland routes (GetPaletteEntries / SetPaletteEntries / AnimatePalette /
 * GetNearestPaletteIndex), plus the rule that every win32k syscall drains a non-empty TEB batch on entry
 * (ntoskrnl KiSystemServiceStart tests TEB.GdiBatchCount for the win32k service table). */
#include "dcattr.h"

static HPALETTE mkpal(int n) {
    struct { WORD ver, num; PALETTEENTRY e[8]; } lp;
    lp.ver = 0x300; lp.num = (WORD)n;
    for (int i = 0; i < n; i++) { lp.e[i].peRed = (BYTE)(10 * (i + 1)); lp.e[i].peGreen = (BYTE)(20 * (i + 1)); lp.e[i].peBlue = (BYTE)(5 * (i + 1)); lp.e[i].peFlags = PC_RESERVED; }
    return U(CreatePalette)((LOGPALETTE *)&lp);
}

TEST(gdi_dcattr_batching_palette_getentries_roundtrip) {
    REQUIRE(U(CreatePalette)); REQUIRE(U(GetPaletteEntries)); REQUIRE(U(DeleteObject));
    HPALETTE p = mkpal(4);
    REQUIRE(p);
    PALETTEENTRY e[4]; memset(e, 0xcc, sizeof e);
    EXPECT_EQ(U(GetPaletteEntries)(p, 0, 4, e), 4);
    for (int i = 0; i < 4; i++) {
        EXPECT_EQ(e[i].peRed, 10 * (i + 1)); EXPECT_EQ(e[i].peGreen, 20 * (i + 1)); EXPECT_EQ(e[i].peBlue, 5 * (i + 1));
    }
    /* count query: NULL buffer returns the number of entries in the palette */
    EXPECT_EQ(U(GetPaletteEntries)(p, 0, 0, NULL), 4);
    CHECK(U(DeleteObject)(p));
}

TEST(gdi_dcattr_batching_palette_setentries_visible_to_get) {
    REQUIRE(U(CreatePalette)); REQUIRE(U(SetPaletteEntries)); REQUIRE(U(GetPaletteEntries));
    HPALETTE p = mkpal(4);
    REQUIRE(p);
    PALETTEENTRY n[2] = { { 1, 2, 3, 0 }, { 4, 5, 6, 0 } }, g[2];
    EXPECT_EQ(U(SetPaletteEntries)(p, 1, 2, n), 2);
    EXPECT_EQ(U(GetPaletteEntries)(p, 1, 2, g), 2);
    EXPECT_EQ(g[0].peRed, 1); EXPECT_EQ(g[0].peBlue, 3); EXPECT_EQ(g[1].peGreen, 5);
    U(DeleteObject)(p);
}

TEST(gdi_dcattr_batching_palette_get_on_stale_handle_fails) {
    REQUIRE(U(CreatePalette)); REQUIRE(U(GetPaletteEntries)); REQUIRE(U(DeleteObject));
    HPALETTE p = mkpal(2);
    REQUIRE(p);
    CHECK(U(DeleteObject)(p));
    PALETTEENTRY e[2];
    EXPECT_EQ(U(GetPaletteEntries)(p, 0, 2, e), 0);
    EXPECT_EQ(U(GetPaletteEntries)(NULL, 0, 2, e), 0);
}

TEST(gdi_dcattr_batching_palette_syscall_drains_pending_batch) {
    REQUIRE(U(CreatePalette)); REQUIRE(U(GetPaletteEntries));
    FXT(f, 8, 8);
    HPALETTE p = mkpal(2);
    REQUIRE(p);
    U(SetBrushOrgEx)(f.dc, 3, 4, NULL); U(SetBrushOrgEx)(f.dc, 5, 6, NULL);
    if (!f.a || teb_count() == 0) { t_note("batch not observed: SetBrushOrgEx did not queue"); U(DeleteObject)(p); batch_reset(); fx_close(&f); return; }
    PALETTEENTRY e[2];
    /* GetPaletteEntries -> NtGdiDoPalette is a win32k syscall: the entry hook replays the queue first. */
    EXPECT_EQ(U(GetPaletteEntries)(p, 0, 2, e), 2);
    EXPECT_EQ(teb_count(), 0);
    EXPECT_EQ(teb_offset(), 0);
    POINT o = dca_pt(f.a, DCA_BRUSHORG);
    EXPECT_EQ(o.x, 5); EXPECT_EQ(o.y, 6);
    U(DeleteObject)(p);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_palette_nearest_index_on_selected_palette) {
    REQUIRE(U(CreatePalette)); REQUIRE(U(SelectPalette)); REQUIRE(U(GetNearestPaletteIndex));
    FXT(f, 8, 8);
    HPALETTE p = mkpal(4), old;
    REQUIRE(p);
    old = U(SelectPalette)(f.dc, p, FALSE);
    CHECK(old != NULL);
    /* entry 2 is (30,60,15): an exact colour maps to its own index */
    UINT idx = U(GetNearestPaletteIndex)(p, RGB(30, 60, 15));
    EXPECT_EQ(idx, 2);
    if (old) U(SelectPalette)(f.dc, old, FALSE);
    U(DeleteObject)(p);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_ordinary_ntdll_syscall_leaves_batch_alone) {
    FXT(f, 8, 8);
    U(SetBrushOrgEx)(f.dc, 7, 8, NULL);
    if (!f.a || teb_count() == 0) { t_note("batch not observed: SetBrushOrgEx did not queue"); batch_reset(); fx_close(&f); return; }
    unsigned c = teb_count();
    /* GetTickCount / GetCurrentThreadId use no syscall; QueryPerformanceCounter neither; NtYieldExecution is a plain ntoskrnl service. */
    SwitchToThread();
    EXPECT_EQ(teb_count(), c);
    batch_reset();
    EXPECT_EQ(teb_count(), 0);
    fx_close(&f);
}
