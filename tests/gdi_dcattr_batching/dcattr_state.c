/* DC_ATTR page (GDICELL.pUser of a DC): the user-mode setters/getters of gdi32full mutate and read it
 * directly, with no kernel round trip. Each test goes through the normal gdi32 API on a memory DC and
 * then inspects the page the kernel mapped into this process. */
#include "dcattr.h"

#define P(x) "gdi_dcattr_batching_" #x

/* ---- the cell and the page ------------------------------------------------------------------ */

TEST(gdi_dcattr_batching_cell_describes_memory_dc) {
    NOTWINE("Windows GDICELL layout");
    FXT(f, 8, 8);
    struct dca_cell c;
    if (!dca_cell(f.dc, &c)) { t_skip("GdiSharedHandleTable not available"); fx_close(&f); return; }
    /* GdiGetEntry callers (e.g. SetBkColor) require wType byte == 1 for a DC, wUpper == HIWORD(handle),
     * owner & ~1 == gW32PID and a non-NULL pUser before they touch the page. */
    CHECK(c.type == 1);
    CHECKF(c.upper == (unsigned)(((ULONG_PTR)f.dc >> 16) & 0xffff), "upper 0x%x handle 0x%llx", c.upper, (unsigned long long)(ULONG_PTR)f.dc);
    CHECKF((c.owner & ~1ul) == GetCurrentProcessId(), "owner 0x%lx pid %lu", c.owner, GetCurrentProcessId());
    CHECK(c.user != NULL);
    CHECK(c.kern != NULL);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_handle_type_bits_of_dc) {
    NOTWINE("Windows GDI handle encoding");
    FXT(f, 8, 8);
    /* gdi32full tests (hdc & 0x7f0000) == 0x10000 for "plain DC"; 0x660000 is a 16-bit metafile DC. */
    CHECKF(((ULONG_PTR)f.dc & 0x7f0000) == 0x10000, "handle 0x%llx", (unsigned long long)(ULONG_PTR)f.dc);
    CHECK(((ULONG_PTR)f.dc & 0xffff) != 0);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_attr_page_readable_to_known_size) {
    FXT(f, 8, 8); NEEDA(f);
    unsigned char buf[DCA_MIN_READ];
    CHECK(k_try_read(f.a, buf, sizeof buf));
    /* The page is 8-byte aligned (qword fields at +0xa0/+0xa8/+0x128 are read with plain loads). */
    CHECK(((ULONG_PTR)f.a & 7) == 0);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_attr_pages_distinct_per_dc) {
    FXT(f1, 8, 8); NEEDA(f1);
    FXT(f2, 8, 8); NEEDA(f2);
    CHECK(f1.a != f2.a);
    long long d = (long long)((char *)f1.a - (char *)f2.a);
    if (d < 0) d = -d;
    /* The pages are carved out of one block at a 0x160 byte stride (the size of the client DC_ATTR on build 19041). */
    CHECKF(d >= 0x160, "pages overlap: distance 0x%llx", (unsigned long long)d);
    fx_close(&f2); fx_close(&f1);
}

TEST(gdi_dcattr_batching_attr_pointer_stable_while_dc_lives) {
    FXT(f, 8, 8); NEEDA(f);
    void *a0 = f.a;
    U(SetBkColor)(f.dc, RGB(1, 2, 3));
    U(SetMapMode)(f.dc, MM_HIMETRIC);
    U(SelectObject)(f.dc, U(GetStockObject)(BLACK_BRUSH));
    CHECK(dca_of(f.dc) == a0);
    HBITMAP b2 = U(CreateBitmap)(4, 4, 1, 32, NULL);
    if (b2) { HGDIOBJ o = U(SelectObject)(f.dc, b2); CHECK(dca_of(f.dc) == a0); U(SelectObject)(f.dc, o); U(DeleteObject)(b2); }
    fx_close(&f);
}

TEST(gdi_dcattr_batching_deleted_dc_no_longer_validates) {
    FXT(f, 8, 8); NEEDA(f);
    HDC dc = f.dc; void *a = f.a;
    U(SelectObject)(f.dc, f.old); f.old = NULL;
    U(DeleteObject)(f.bmp); f.bmp = NULL;
    CHECK(U(DeleteDC)(dc));
    f.dc = NULL;
    /* GdiGetEntry callers reject the old handle (stale uniqueness / wType), so the getter yields its failure value. */
    EXPECT_EQ(U(GetBkColor)(dc), CLR_INVALID);
    void *again = dca_of(dc);
    if (again) t_note("deleted handle still validates through the cell: page %p (was %p)", again, a);
    CHECK(!U(DeleteDC)(dc));
    fx_close(&f);
}

/* ---- defaults of a fresh CreateCompatibleDC ------------------------------------------------------ */

TEST(gdi_dcattr_batching_defaults_colors_and_modes) {
    FXT(f, 8, 8);
    /* Documented CreateCompatibleDC defaults, read back through the cached getters. */
    EXPECT_EQ(U(GetBkColor)(f.dc), 0x00ffffff);
    EXPECT_EQ(U(GetTextColor)(f.dc), 0);
    EXPECT_EQ(U(GetBkMode)(f.dc), OPAQUE);
    EXPECT_EQ(U(GetROP2)(f.dc), R2_COPYPEN);
    EXPECT_EQ(U(GetStretchBltMode)(f.dc), BLACKONWHITE);
    EXPECT_EQ(U(GetPolyFillMode)(f.dc), ALTERNATE);
    EXPECT_EQ(U(GetMapMode)(f.dc), MM_TEXT);
    EXPECT_EQ(U(GetTextAlign)(f.dc), TA_LEFT | TA_TOP | TA_NOUPDATECP);
    EXPECT_EQ(U(GetGraphicsMode)(f.dc), GM_COMPATIBLE);
    EXPECT_EQ(U(GetTextCharacterExtra)(f.dc), 0);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_defaults_in_the_page) {
    FXT(f, 8, 8); NEEDA(f);
    EXPECT_EQ(dca_u32(f.a, DCA_BKCLR), 0x00ffffff);
    EXPECT_EQ(dca_u32(f.a, DCA_BKCLR_XL), 0x00ffffff);
    EXPECT_EQ(dca_u32(f.a, DCA_TXCLR), 0);
    EXPECT_EQ(dca_u32(f.a, DCA_TXCLR_XL), 0);
    EXPECT_EQ(dca_u8(f.a, DCA_ROP2), R2_COPYPEN);
    EXPECT_EQ(dca_i32(f.a, DCA_BKMODE), OPAQUE);
    EXPECT_EQ(dca_u8(f.a, DCA_JBKMODE), 2);              /* SetBkModeImpl: jBkMode = (mode == 2) + 1 */
    EXPECT_EQ(dca_i32(f.a, DCA_FILLMODE), ALTERNATE);
    EXPECT_EQ(dca_u8(f.a, DCA_JFILLMODE), 1);            /* SetPolyFillModeImpl: (mode == 2) + 1 */
    EXPECT_EQ(dca_i32(f.a, DCA_STRETCH), BLACKONWHITE);
    EXPECT_EQ(dca_i32(f.a, DCA_MAPMODE), MM_TEXT);
    EXPECT_EQ(dca_i32(f.a, DCA_TA), 0);
    EXPECT_EQ(dca_i32(f.a, DCA_CHAREXTRA), 0);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_defaults_geometry) {
    FXT(f, 8, 8); NEEDA(f);
    POINT p = dca_pt(f.a, DCA_PTLCURRENT);
    EXPECT_EQ(p.x, 0); EXPECT_EQ(p.y, 0);
    p = dca_pt(f.a, DCA_WNDORG); EXPECT_EQ(p.x, 0); EXPECT_EQ(p.y, 0);
    p = dca_pt(f.a, DCA_VIEWORG); EXPECT_EQ(p.x, 0); EXPECT_EQ(p.y, 0);
    p = dca_pt(f.a, DCA_BRUSHORG); EXPECT_EQ(p.x, 0); EXPECT_EQ(p.y, 0);
    POINT g = { -1, -1 };
    CHECK(U(GetCurrentPositionEx)(f.dc, &g)); EXPECT_EQ(g.x, 0); EXPECT_EQ(g.y, 0);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_defaults_selected_stock_objects) {
    FXT(f, 8, 8);
    /* Documented default pen/brush of a new DC; GetCurrentObject asks the kernel (NtGdiGetDCObject). */
    EXPECT_EQ(U(GetCurrentObject)(f.dc, OBJ_BRUSH), U(GetStockObject)(WHITE_BRUSH));
    EXPECT_EQ(U(GetCurrentObject)(f.dc, OBJ_PEN), U(GetStockObject)(BLACK_PEN));
    if (f.a) {
        void *hb = dca_ptr(f.a, DCA_HBRUSH), *hp = dca_ptr(f.a, DCA_HPEN);
        if (hb != U(GetStockObject)(WHITE_BRUSH)) t_note("page hbrush %p differs from WHITE_BRUSH %p", hb, U(GetStockObject)(WHITE_BRUSH));
        if (hp != U(GetStockObject)(BLACK_PEN)) t_note("page hpen %p differs from BLACK_PEN %p", hp, U(GetStockObject)(BLACK_PEN));
    }
    fx_close(&f);
}

/* ---- colours --------------------------------------------------------------------------------- */

static const COLORREF g_colors[] = { 0x00000000, 0x00ffffff, 0x00010203, 0x00fedcba, 0x00808080, 0x000000ff, 0x00ff0000 };

TEST(gdi_dcattr_batching_bkcolor_written_into_page) {
    FXT(f, 8, 8); NEEDA(f);
    for (unsigned i = 0; i < sizeof g_colors / sizeof *g_colors; i++) {
        U(SetBkColor)(f.dc, g_colors[i]);
        /* SetBkColor stores the caller's value at +0xb4 and the (ICM-translated, flag-masked) value at +0xb0. */
        EXPECT_EQ(dca_u32(f.a, DCA_BKCLR), g_colors[i]);
        EXPECT_EQ(dca_u32(f.a, DCA_BKCLR_XL), g_colors[i]);
        EXPECT_EQ(U(GetBkColor)(f.dc), g_colors[i]);
    }
    fx_close(&f);
}

TEST(gdi_dcattr_batching_textcolor_written_into_page) {
    FXT(f, 8, 8); NEEDA(f);
    for (unsigned i = 0; i < sizeof g_colors / sizeof *g_colors; i++) {
        U(SetTextColor)(f.dc, g_colors[i]);
        EXPECT_EQ(dca_u32(f.a, DCA_TXCLR), g_colors[i]);
        EXPECT_EQ(dca_u32(f.a, DCA_TXCLR_XL), g_colors[i]);
        EXPECT_EQ(U(GetTextColor)(f.dc), g_colors[i]);
    }
    fx_close(&f);
}

TEST(gdi_dcattr_batching_colour_flag_bits_masked_in_translated_copy) {
    FXT(f, 8, 8); NEEDA(f);
    /* The translated copy keeps only bits 0x13ffffff of the COLORREF; the raw copy is what GetBkColor
     * returns. 0x02 = PALETTERGB flag and 0x01 = PALETTEINDEX flag survive, 0x40 and 0x20 bits are cleared. */
    static const struct { COLORREF in, xl; } v[] = {
        { 0x02112233, 0x02112233 }, { 0x01000005, 0x01000005 }, { 0x40112233, 0x00112233 },
        { 0x10445566, 0x10445566 }, { 0x20112233, 0x00112233 }, { 0x80000001, 0x00000001 },
    };
    for (unsigned i = 0; i < sizeof v / sizeof *v; i++) {
        U(SetBkColor)(f.dc, v[i].in);
        EXPECT_EQ(dca_u32(f.a, DCA_BKCLR), v[i].in);
        EXPECT_EQ(dca_u32(f.a, DCA_BKCLR_XL), v[i].xl);
        EXPECT_EQ(U(GetBkColor)(f.dc), v[i].in);
        U(SetTextColor)(f.dc, v[i].in);
        EXPECT_EQ(dca_u32(f.a, DCA_TXCLR), v[i].in);
        EXPECT_EQ(dca_u32(f.a, DCA_TXCLR_XL), v[i].xl);
        EXPECT_EQ(U(GetTextColor)(f.dc), v[i].in);
    }
    fx_close(&f);
}

TEST(gdi_dcattr_batching_colour_setters_return_previous_raw_value) {
    FXT(f, 8, 8);
    U(SetBkColor)(f.dc, 0x00123456);
    EXPECT_EQ(U(SetBkColor)(f.dc, 0x00654321), 0x00123456);
    EXPECT_EQ(U(SetBkColor)(f.dc, 0x02abcdef), 0x00654321);
    EXPECT_EQ(U(SetBkColor)(f.dc, 0), 0x02abcdef);          /* previous is the raw value, flag bits included */
    U(SetTextColor)(f.dc, 0x00123456);
    EXPECT_EQ(U(SetTextColor)(f.dc, 0x00654321), 0x00123456);
    EXPECT_EQ(U(SetTextColor)(f.dc, 0x02abcdef), 0x00654321);
    EXPECT_EQ(U(SetTextColor)(f.dc, 0), 0x02abcdef);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_bkcolor_change_sets_fill_line_background_dirty) {
    FXT(f, 8, 8); NEEDA(f);
    U(SetBkColor)(f.dc, 0x00111111);
    unsigned before = dca_u32(f.a, DCA_DIRTY);
    dca_poke32(f.a, DCA_DIRTY, before & ~0xbu);       /* scratch DC: drop the three bits SetBkColor owns */
    U(SetBkColor)(f.dc, 0x00222222);
    /* A different translated colour ORs 0xb (fill|line|background) into ulDirty_. */
    EXPECT_EQ(dca_u32(f.a, DCA_DIRTY) & 0xb, 0xb);
    /* Setting the same colour again leaves cleared bits cleared (the page is only dirtied on a change). */
    dca_poke32(f.a, DCA_DIRTY, dca_u32(f.a, DCA_DIRTY) & ~0xbu);
    U(SetBkColor)(f.dc, 0x00222222);
    EXPECT_EQ(dca_u32(f.a, DCA_DIRTY) & 0xb, 0);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_textcolor_change_sets_fill_line_text_dirty) {
    FXT(f, 8, 8); NEEDA(f);
    U(SetTextColor)(f.dc, 0x00111111);
    dca_poke32(f.a, DCA_DIRTY, dca_u32(f.a, DCA_DIRTY) & ~0x7u);
    U(SetTextColor)(f.dc, 0x00222222);
    EXPECT_EQ(dca_u32(f.a, DCA_DIRTY) & 0x7, 0x7);       /* SetTextColorImpl ORs 7 (fill|line|text) */
    dca_poke32(f.a, DCA_DIRTY, dca_u32(f.a, DCA_DIRTY) & ~0x7u);
    U(SetTextColor)(f.dc, 0x00222222);
    EXPECT_EQ(dca_u32(f.a, DCA_DIRTY) & 0x7, 0);
    fx_close(&f);
}

/* ---- modes ----------------------------------------------------------------------------------- */

TEST(gdi_dcattr_batching_bkmode_values_and_jbkmode) {
    FXT(f, 8, 8); NEEDA(f);
    /* SetBkModeImpl does no range check: lBkMode takes the raw int and jBkMode = (mode == 2) + 1. */
    static const int modes[] = { TRANSPARENT, OPAQUE, 0, 3, 5, -1 };
    for (unsigned i = 0; i < sizeof modes / sizeof *modes; i++) {
        U(SetBkMode)(f.dc, modes[i]);
        EXPECT_EQ(dca_i32(f.a, DCA_BKMODE), modes[i]);
        EXPECT_EQ(dca_u8(f.a, DCA_JBKMODE), modes[i] == 2 ? 2 : 1);
        EXPECT_EQ(U(GetBkMode)(f.dc), modes[i]);
    }
    fx_close(&f);
}

TEST(gdi_dcattr_batching_bkmode_returns_previous) {
    FXT(f, 8, 8);
    EXPECT_EQ(U(SetBkMode)(f.dc, TRANSPARENT), OPAQUE);
    EXPECT_EQ(U(SetBkMode)(f.dc, OPAQUE), TRANSPARENT);
    EXPECT_EQ(U(SetBkMode)(f.dc, OPAQUE), OPAQUE);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_rop2_all_valid_values) {
    FXT(f, 8, 8); NEEDA(f);
    for (int r = R2_BLACK; r <= R2_LAST; r++) {
        int prev = U(GetROP2)(f.dc);
        EXPECT_EQ(U(SetROP2)(f.dc, r), prev);       /* returns the previous byte */
        EXPECT_EQ(dca_u8(f.a, DCA_ROP2), r);
        EXPECT_EQ(U(GetROP2)(f.dc), r);
    }
    fx_close(&f);
}

TEST(gdi_dcattr_batching_rop2_stored_as_single_byte_without_validation) {
    FXT(f, 8, 8); NEEDA(f);
    /* SetROP2 stores (char)rop2 with no range check, GetROP2 returns the byte zero-extended. */
    U(SetROP2)(f.dc, 0x1ff);
    EXPECT_EQ(dca_u8(f.a, DCA_ROP2), 0xff);
    EXPECT_EQ(U(GetROP2)(f.dc), 0xff);
    U(SetROP2)(f.dc, 0x100);
    EXPECT_EQ(U(GetROP2)(f.dc), 0);
    U(SetROP2)(f.dc, R2_COPYPEN);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_stretchblt_mode_and_jstretch_mapping) {
    FXT(f, 8, 8); NEEDA(f);
    /* SetStretchBltModeImpl: lStretchBltMode = mode; jStretchBltMode = mode for 1..4, otherwise 2. */
    static const struct { int mode, j; } v[] = { {1,1}, {2,2}, {3,3}, {4,4}, {0,2}, {5,2}, {0x10,2}, {-1,2} };
    for (unsigned i = 0; i < sizeof v / sizeof *v; i++) {
        U(SetStretchBltMode)(f.dc, v[i].mode);
        EXPECT_EQ(dca_i32(f.a, DCA_STRETCH), v[i].mode);
        EXPECT_EQ(dca_u8(f.a, DCA_JSTRETCH), v[i].j);
        EXPECT_EQ(U(GetStretchBltMode)(f.dc), v[i].mode);
    }
    fx_close(&f);
}

TEST(gdi_dcattr_batching_stretchblt_returns_previous) {
    FXT(f, 8, 8);
    EXPECT_EQ(U(SetStretchBltMode)(f.dc, HALFTONE), BLACKONWHITE);
    EXPECT_EQ(U(SetStretchBltMode)(f.dc, COLORONCOLOR), HALFTONE);
    EXPECT_EQ(U(SetStretchBltMode)(f.dc, WHITEONBLACK), COLORONCOLOR);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_polyfill_mode_and_jfill_mapping) {
    FXT(f, 8, 8); NEEDA(f);
    static const int modes[] = { ALTERNATE, WINDING, 0, 3, 9 };
    for (unsigned i = 0; i < sizeof modes / sizeof *modes; i++) {
        U(SetPolyFillMode)(f.dc, modes[i]);
        EXPECT_EQ(dca_i32(f.a, DCA_FILLMODE), modes[i]);
        EXPECT_EQ(dca_u8(f.a, DCA_JFILLMODE), modes[i] == 2 ? 2 : 1);
        EXPECT_EQ(U(GetPolyFillMode)(f.dc), modes[i]);
    }
    fx_close(&f);
}

TEST(gdi_dcattr_batching_polyfill_returns_previous) {
    FXT(f, 8, 8);
    EXPECT_EQ(U(SetPolyFillMode)(f.dc, WINDING), ALTERNATE);
    EXPECT_EQ(U(SetPolyFillMode)(f.dc, ALTERNATE), WINDING);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_textalign_raw_and_masked_copies) {
    FXT(f, 8, 8); NEEDA(f);
    static const UINT v[] = { TA_LEFT, TA_CENTER, TA_RIGHT, TA_BOTTOM, TA_BASELINE, TA_CENTER | TA_BOTTOM,
                              TA_RIGHT | TA_BASELINE, TA_UPDATECP, 0x7f, 0x1234 };
    for (unsigned i = 0; i < sizeof v / sizeof *v; i++) {
        U(SetTextAlign)(f.dc, v[i]);
        EXPECT_EQ(dca_u32(f.a, DCA_TA), v[i]);                   /* GetTextAlign returns this word */
        EXPECT_EQ(U(GetTextAlign)(f.dc), v[i]);
        /* SetTextAlignImpl: the masked copy is (align & 0x1f), pre-adjusted by ^2 for RTL layout only. */
        if ((dca_u8(f.a, DCA_LAYOUTFLAGS) & 1) == 0) EXPECT_EQ(dca_u32(f.a, DCA_TA_MASKED), v[i] & 0x1f);
    }
    U(SetTextAlign)(f.dc, TA_LEFT);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_textalign_returns_previous) {
    FXT(f, 8, 8);
    EXPECT_EQ(U(SetTextAlign)(f.dc, TA_CENTER), TA_LEFT);
    EXPECT_EQ(U(SetTextAlign)(f.dc, TA_RIGHT | TA_BOTTOM), TA_CENTER);
    EXPECT_EQ(U(SetTextAlign)(f.dc, 0), TA_RIGHT | TA_BOTTOM);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_mapmode_all_modes_visible_in_page_and_kernel) {
    FXT(f, 8, 8); NEEDA(f);
    UDECL(int, NtGdiGetDCDword, (HDC, int, ULONG *));
    for (int m = MM_TEXT; m <= MM_ANISOTROPIC; m++) {
        int prev = U(GetMapMode)(f.dc);
        EXPECT_EQ(U(SetMapMode)(f.dc, m), prev);        /* SetMapMode returns the previous mode */
        EXPECT_EQ(dca_i32(f.a, DCA_MAPMODE), m);          /* the kernel half (NtGdiGetAndSetDCDword idx 8) writes +0x68 */
        EXPECT_EQ(U(GetMapMode)(f.dc), m);
        ULONG kv = 0xdeadbeef;
        if (UN(NtGdiGetDCDword)) {
            /* NtGdiGetDCDword index 8: kernel DC attr +0x68 (DC::iSetMapMode result). */
            CHECK(UN(NtGdiGetDCDword)(f.dc, 8, &kv));
            EXPECT_EQ(kv, (ULONG)m);
        }
    }
    U(SetMapMode)(f.dc, MM_TEXT);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_mapmode_same_mode_no_change) {
    FXT(f, 8, 8);
    EXPECT_EQ(U(SetMapMode)(f.dc, MM_TEXT), MM_TEXT);
    EXPECT_EQ(U(SetMapMode)(f.dc, MM_LOENGLISH), MM_TEXT);
    EXPECT_EQ(U(SetMapMode)(f.dc, MM_LOENGLISH), MM_LOENGLISH);
    EXPECT_EQ(U(GetMapMode)(f.dc), MM_LOENGLISH);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_chars_extra_and_graphics_mode) {
    FXT(f, 8, 8); NEEDA(f);
    EXPECT_EQ(U(SetTextCharacterExtra)(f.dc, 7), 0);
    EXPECT_EQ(dca_i32(f.a, DCA_CHAREXTRA), 7);
    EXPECT_EQ(U(GetTextCharacterExtra)(f.dc), 7);
    EXPECT_EQ(U(SetTextCharacterExtra)(f.dc, -3), 7);
    EXPECT_EQ(dca_i32(f.a, DCA_CHAREXTRA), -3);
    /* extra == INT_MIN is the failure sentinel: SetTextCharacterExtra refuses it, sets error 0x57. */
    SetLastError(0);
    EXPECT_EQ(U(SetTextCharacterExtra)(f.dc, (int)0x80000000), (int)0x80000000);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    EXPECT_EQ(dca_i32(f.a, DCA_CHAREXTRA), -3);
    U(SetTextCharacterExtra)(f.dc, 0);
    CHECK(U(SetGraphicsMode)(f.dc, GM_ADVANCED) != 0);
    EXPECT_EQ(dca_i32(f.a, DCA_GFXMODE), GM_ADVANCED);
    EXPECT_EQ(U(GetGraphicsMode)(f.dc), GM_ADVANCED);
    U(SetGraphicsMode)(f.dc, GM_COMPATIBLE);
    EXPECT_EQ(dca_i32(f.a, DCA_GFXMODE), GM_COMPATIBLE);
    fx_close(&f);
}

/* ---- pen position and origins ---------------------------------------------------------------- */

TEST(gdi_dcattr_batching_moveto_updates_ptlcurrent_and_dirty) {
    FXT(f, 64, 64); NEEDA(f);
    POINT old = { -7, -7 };
    CHECK(U(MoveToEx)(f.dc, 10, 20, &old));
    EXPECT_EQ(old.x, 0); EXPECT_EQ(old.y, 0);
    POINT p = dca_pt(f.a, DCA_PTLCURRENT);
    EXPECT_EQ(p.x, 10); EXPECT_EQ(p.y, 20);
    /* MoveToExImpl clears 0x100 and sets 0x600 (ptfx + style state dirty). */
    unsigned d = dca_u32(f.a, DCA_DIRTY);
    EXPECT_EQ(d & DIRTY_PTLCURRENT, 0);
    EXPECT_EQ(d & (DIRTY_PTFX | DIRTY_STYLESTATE), DIRTY_PTFX | DIRTY_STYLESTATE);
    POINT g = { 0, 0 };
    CHECK(U(GetCurrentPositionEx)(f.dc, &g));
    EXPECT_EQ(g.x, 10); EXPECT_EQ(g.y, 20);
    CHECK(U(MoveToEx)(f.dc, -5, 6, &old));
    EXPECT_EQ(old.x, 10); EXPECT_EQ(old.y, 20);
    p = dca_pt(f.a, DCA_PTLCURRENT);
    EXPECT_EQ(p.x, -5); EXPECT_EQ(p.y, 6);
    CHECK(U(MoveToEx)(f.dc, 1, 1, NULL));
    fx_close(&f);
}

TEST(gdi_dcattr_batching_lineto_moves_current_position) {
    FXT(f, 64, 64); NEEDA(f);
    U(MoveToEx)(f.dc, 3, 4, NULL);
    CHECK(U(LineTo)(f.dc, 30, 4));
    /* LineTo is a kernel call; afterwards the kernel's position reaches the page, either as an
     * updated ptlCurrent or via the 0x100 "ptfxCurrent is newer" marker that GetCurrentPositionEx
     * resolves (and clears) on the next query. */
    unsigned d = dca_u32(f.a, DCA_DIRTY);
    if (!(d & DIRTY_PTLCURRENT)) t_note("LineTo left ptlCurrent clean (dirty 0x%x)", d);
    POINT g = { 0, 0 };
    CHECK(U(GetCurrentPositionEx)(f.dc, &g));
    EXPECT_EQ(g.x, 30); EXPECT_EQ(g.y, 4);
    EXPECT_EQ(dca_u32(f.a, DCA_DIRTY) & DIRTY_PTLCURRENT, 0);   /* GetCurrentPositionEx clears 0x100 */
    POINT p = dca_pt(f.a, DCA_PTLCURRENT);
    EXPECT_EQ(p.x, 30); EXPECT_EQ(p.y, 4);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_window_org_in_page_and_returns_previous) {
    FXT(f, 8, 8); NEEDA(f);
    POINT old = { 99, 99 };
    CHECK(U(SetWindowOrgEx)(f.dc, 11, 22, &old));
    EXPECT_EQ(old.x, 0); EXPECT_EQ(old.y, 0);
    POINT p = dca_pt(f.a, DCA_WNDORG);
    EXPECT_EQ(p.x, 11); EXPECT_EQ(p.y, 22);
    EXPECT_EQ(dca_i32(f.a, DCA_WNDORG_X), 11);
    EXPECT_EQ(dca_u32(f.a, DCA_XFORMDIRTY) & 0x2010, 0x2010);   /* an origin change marks the transform dirty */
    POINT g = { 0, 0 };
    CHECK(U(GetWindowOrgEx)(f.dc, &g));
    EXPECT_EQ(g.x, 11); EXPECT_EQ(g.y, 22);
    CHECK(U(SetWindowOrgEx)(f.dc, -1, -2, &old));
    EXPECT_EQ(old.x, 11); EXPECT_EQ(old.y, 22);
    U(SetWindowOrgEx)(f.dc, 0, 0, NULL);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_viewport_org_in_page_and_returns_previous) {
    FXT(f, 8, 8); NEEDA(f);
    POINT old = { 99, 99 };
    CHECK(U(SetViewportOrgEx)(f.dc, 5, 6, &old));
    EXPECT_EQ(old.x, 0); EXPECT_EQ(old.y, 0);
    POINT p = dca_pt(f.a, DCA_VIEWORG);
    EXPECT_EQ(p.x, 5); EXPECT_EQ(p.y, 6);
    POINT g = { 0, 0 };
    CHECK(U(GetViewportOrgEx)(f.dc, &g));
    EXPECT_EQ(g.x, 5); EXPECT_EQ(g.y, 6);
    CHECK(U(SetViewportOrgEx)(f.dc, 8, 9, &old));
    EXPECT_EQ(old.x, 5); EXPECT_EQ(old.y, 6);
    U(SetViewportOrgEx)(f.dc, 0, 0, NULL);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_origin_setters_unchanged_value_leaves_xform_clean) {
    FXT(f, 8, 8); NEEDA(f);
    U(SetWindowOrgEx)(f.dc, 3, 3, NULL);
    U(SetViewportOrgEx)(f.dc, 4, 4, NULL);
    dca_poke32(f.a, DCA_XFORMDIRTY, dca_u32(f.a, DCA_XFORMDIRTY) & ~0x2010u);
    /* Both setters only touch the page when the value changes (compared against +0x134 / +0x144). */
    U(SetWindowOrgEx)(f.dc, 3, 3, NULL);
    U(SetViewportOrgEx)(f.dc, 4, 4, NULL);
    EXPECT_EQ(dca_u32(f.a, DCA_XFORMDIRTY) & 0x2010, 0);
    U(SetViewportOrgEx)(f.dc, 5, 4, NULL);
    EXPECT_EQ(dca_u32(f.a, DCA_XFORMDIRTY) & 0x2010, 0x2010);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_brush_org_written_immediately) {
    FXT(f, 8, 8); NEEDA(f);
    POINT old = { 9, 9 };
    CHECK(U(SetBrushOrgEx)(f.dc, 3, 5, &old));
    EXPECT_EQ(old.x, 0); EXPECT_EQ(old.y, 0);
    /* Whether the kernel hears about it now or at the next flush, the page is updated right away. */
    POINT p = dca_pt(f.a, DCA_BRUSHORG);
    EXPECT_EQ(p.x, 3); EXPECT_EQ(p.y, 5);
    POINT g = { 0, 0 };
    CHECK(U(GetBrushOrgEx)(f.dc, &g));
    EXPECT_EQ(g.x, 3); EXPECT_EQ(g.y, 5);
    CHECK(U(SetBrushOrgEx)(f.dc, 6, 7, &old));
    EXPECT_EQ(old.x, 3); EXPECT_EQ(old.y, 5);
    batch_reset();
    fx_close(&f);
}

/* ---- selecting objects ----------------------------------------------------------------------- */

TEST(gdi_dcattr_batching_select_brush_updates_page_without_batch) {
    FXT(f, 8, 8); NEEDA(f);
    HGDIOBJ blk = U(GetStockObject)(BLACK_BRUSH), grey = U(GetStockObject)(GRAY_BRUSH);
    HGDIOBJ prev0 = dca_ptr(f.a, DCA_HBRUSH);
    HGDIOBJ r = U(SelectObject)(f.dc, blk);
    EXPECT_EQ(r, prev0);                                  /* returns what the page held */
    EXPECT_EQ(dca_ptr(f.a, DCA_HBRUSH), blk);
    CHECK((dca_u32(f.a, DCA_DIRTY) & DIRTY_BRUSH) != 0);  /* SelectObjectImpl brush case ORs 0x1000 */
    EXPECT_EQ(teb_count(), 0);                            /* brush selection is not batched */
    r = U(SelectObject)(f.dc, grey);
    EXPECT_EQ(r, blk);
    EXPECT_EQ(dca_ptr(f.a, DCA_HBRUSH), grey);
    /* The kernel resolves the pending page change when asked: GetCurrentObject -> NtGdiGetDCObject. */
    EXPECT_EQ(U(GetCurrentObject)(f.dc, OBJ_BRUSH), grey);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_select_pen_updates_page_without_batch) {
    FXT(f, 8, 8); NEEDA(f);
    HGDIOBJ nul = U(GetStockObject)(NULL_PEN), wht = U(GetStockObject)(WHITE_PEN);
    HGDIOBJ prev0 = dca_ptr(f.a, DCA_HPEN);
    EXPECT_EQ(U(SelectObject)(f.dc, nul), prev0);
    EXPECT_EQ(dca_ptr(f.a, DCA_HPEN), nul);
    CHECK((dca_u32(f.a, DCA_DIRTY) & DIRTY_PEN) != 0);    /* pen case ORs 0x2000 */
    EXPECT_EQ(teb_count(), 0);
    EXPECT_EQ(U(SelectObject)(f.dc, wht), nul);
    EXPECT_EQ(U(GetCurrentObject)(f.dc, OBJ_PEN), wht);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_select_same_brush_returns_itself) {
    FXT(f, 8, 8); NEEDA(f);
    HGDIOBJ b = U(GetStockObject)(DKGRAY_BRUSH);
    U(SelectObject)(f.dc, b);
    EXPECT_EQ(U(SelectObject)(f.dc, b), b);
    EXPECT_EQ(dca_ptr(f.a, DCA_HBRUSH), b);
    fx_close(&f);
}

/* ---- getters are served from the page ------------------------------------------------------------ */

TEST(gdi_dcattr_batching_getters_read_cached_page_not_kernel) {
    FXT(f, 8, 8); NEEDA(f);
    /* The gdi32full getters only index the page. Rewriting a field behind their back therefore changes
     * what they return, which cannot happen if they asked the kernel. Everything is restored. */
    #define POKE(off, v, getter_expr, want) do { unsigned sv = dca_u32(f.a, off); \
        CHECK(dca_poke32(f.a, off, (v))); CHECKF((getter_expr) == (want), "%s -> got %lld", #getter_expr, (long long)(getter_expr)); \
        dca_poke32(f.a, off, sv); } while (0)
    POKE(DCA_BKCLR, 0x00abcdef, U(GetBkColor)(f.dc), 0x00abcdef);
    POKE(DCA_TXCLR, 0x00fedcba, U(GetTextColor)(f.dc), 0x00fedcba);
    POKE(DCA_BKMODE, 77, U(GetBkMode)(f.dc), 77);
    POKE(DCA_FILLMODE, 78, U(GetPolyFillMode)(f.dc), 78);
    POKE(DCA_STRETCH, 79, U(GetStretchBltMode)(f.dc), 79);
    POKE(DCA_TA, 0x55, U(GetTextAlign)(f.dc), 0x55);
    POKE(DCA_MAPMODE, 6, U(GetMapMode)(f.dc), 6);
    POKE(DCA_CHAREXTRA, 12, U(GetTextCharacterExtra)(f.dc), 12);
    POKE(DCA_GFXMODE, 2, U(GetGraphicsMode)(f.dc), 2);
    #undef POKE
    unsigned char sv = (unsigned char)dca_u8(f.a, DCA_ROP2);
    dca_poke8(f.a, DCA_ROP2, R2_NOT);
    EXPECT_EQ(U(GetROP2)(f.dc), R2_NOT);
    dca_poke8(f.a, DCA_ROP2, sv);
    EXPECT_EQ(U(GetROP2)(f.dc), sv);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_point_getters_read_cached_page) {
    FXT(f, 8, 8); NEEDA(f);
    POINT save = dca_pt(f.a, DCA_VIEWORG), g;
    dca_poke32(f.a, DCA_VIEWORG, 123); dca_poke32(f.a, DCA_VIEWORG + 4, 456);
    CHECK(U(GetViewportOrgEx)(f.dc, &g)); EXPECT_EQ(g.x, 123); EXPECT_EQ(g.y, 456);
    dca_poke32(f.a, DCA_VIEWORG, save.x); dca_poke32(f.a, DCA_VIEWORG + 4, save.y);
    save = dca_pt(f.a, DCA_BRUSHORG);
    dca_poke32(f.a, DCA_BRUSHORG, 31); dca_poke32(f.a, DCA_BRUSHORG + 4, 32);
    CHECK(U(GetBrushOrgEx)(f.dc, &g)); EXPECT_EQ(g.x, 31); EXPECT_EQ(g.y, 32);
    dca_poke32(f.a, DCA_BRUSHORG, save.x); dca_poke32(f.a, DCA_BRUSHORG + 4, save.y);
    /* ptlCurrent is read from +0xd8 while 0x100 is clear. */
    unsigned d = dca_u32(f.a, DCA_DIRTY);
    dca_poke32(f.a, DCA_DIRTY, d & ~DIRTY_PTLCURRENT);
    save = dca_pt(f.a, DCA_PTLCURRENT);
    dca_poke32(f.a, DCA_PTLCURRENT, 17); dca_poke32(f.a, DCA_PTLCURRENT + 4, 18);
    CHECK(U(GetCurrentPositionEx)(f.dc, &g)); EXPECT_EQ(g.x, 17); EXPECT_EQ(g.y, 18);
    dca_poke32(f.a, DCA_PTLCURRENT, save.x); dca_poke32(f.a, DCA_PTLCURRENT + 4, save.y);
    dca_poke32(f.a, DCA_DIRTY, d);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_dcs_do_not_share_state) {
    FXT(f1, 8, 8); FXT(f2, 8, 8);
    U(SetBkColor)(f1.dc, 0x00111111); U(SetTextColor)(f1.dc, 0x00222222); U(SetROP2)(f1.dc, R2_XORPEN);
    U(SetBkMode)(f1.dc, TRANSPARENT); U(SetMapMode)(f1.dc, MM_HIMETRIC);
    EXPECT_EQ(U(GetBkColor)(f2.dc), 0x00ffffff);
    EXPECT_EQ(U(GetTextColor)(f2.dc), 0);
    EXPECT_EQ(U(GetROP2)(f2.dc), R2_COPYPEN);
    EXPECT_EQ(U(GetBkMode)(f2.dc), OPAQUE);
    EXPECT_EQ(U(GetMapMode)(f2.dc), MM_TEXT);
    fx_close(&f2); fx_close(&f1);
}

TEST(gdi_dcattr_batching_save_restore_dc_restores_page_values) {
    FXT(f, 8, 8);
    U(SetBkColor)(f.dc, RGB(10, 20, 30)); U(SetTextColor)(f.dc, RGB(40, 50, 60)); U(SetROP2)(f.dc, R2_XORPEN);
    U(SetBkMode)(f.dc, TRANSPARENT); U(SetMapMode)(f.dc, MM_HIMETRIC); U(SetTextAlign)(f.dc, TA_CENTER | TA_BOTTOM);
    int lvl = U(SaveDC)(f.dc);
    CHECK(lvl > 0);
    U(SetBkColor)(f.dc, 0); U(SetTextColor)(f.dc, 0x00ffffff); U(SetROP2)(f.dc, R2_NOT);
    U(SetBkMode)(f.dc, OPAQUE); U(SetMapMode)(f.dc, MM_TEXT); U(SetTextAlign)(f.dc, TA_LEFT);
    CHECK(U(RestoreDC)(f.dc, -1));
    EXPECT_EQ(U(GetBkColor)(f.dc), RGB(10, 20, 30));
    EXPECT_EQ(U(GetTextColor)(f.dc), RGB(40, 50, 60));
    EXPECT_EQ(U(GetROP2)(f.dc), R2_XORPEN);
    EXPECT_EQ(U(GetBkMode)(f.dc), TRANSPARENT);
    EXPECT_EQ(U(GetMapMode)(f.dc), MM_HIMETRIC);
    EXPECT_EQ(U(GetTextAlign)(f.dc), TA_CENTER | TA_BOTTOM);
    if (f.a) { EXPECT_EQ(dca_u32(f.a, DCA_BKCLR), RGB(10, 20, 30)); EXPECT_EQ(dca_u8(f.a, DCA_ROP2), R2_XORPEN); }
    fx_close(&f);
}

TEST(gdi_dcattr_batching_restore_dc_with_bad_level_fails) {
    FXT(f, 8, 8);
    int lvl = U(SaveDC)(f.dc);
    CHECK(lvl > 0);
    CHECK(!U(RestoreDC)(f.dc, lvl + 40));
    CHECK(U(RestoreDC)(f.dc, lvl));
    fx_close(&f);
}

/* ---- invalid and stale handles ------------------------------------------------------------------ */

struct stale_ctx { HDC dc; };
static HDC make_stale_dc(void) {
    HDC d = U(CreateCompatibleDC)(NULL);
    if (d) U(DeleteDC)(d);
    return d;
}

TEST(gdi_dcattr_batching_stale_dc_setters_fail_with_invalid_parameter) {
    REQUIRE(U(CreateCompatibleDC)); REQUIRE(U(SetBkColor));
    HDC d = make_stale_dc();
    REQUIRE(d);
    /* A deleted DC still has type bits 1, so the setters take the GdiGetEntry path: the stale-uniqueness
     * check fails and GdiSetLastError(0x57) is raised with the function's documented failure value. */
    #define STALE(call, want) do { SetLastError(0); CHECKF((call) == (want), "%s", #call); EXPECT_ERR(ERROR_INVALID_PARAMETER); } while (0)
    STALE(U(SetBkColor)(d, 1), CLR_INVALID);
    STALE(U(SetTextColor)(d, 1), CLR_INVALID);
    STALE(U(SetBkMode)(d, 1), 0);
    STALE(U(SetROP2)(d, R2_NOT), 0);
    STALE(U(SetMapMode)(d, MM_HIMETRIC), 0);
    STALE(U(SetTextAlign)(d, TA_CENTER), (UINT)GDI_ERROR);
    STALE(U(SetStretchBltMode)(d, HALFTONE), 0);
    STALE(U(SetPolyFillMode)(d, WINDING), 0);
    STALE(U(MoveToEx)(d, 1, 2, NULL), 0);
    STALE(U(SetTextCharacterExtra)(d, 3), (int)0x80000000);
    #undef STALE
}

TEST(gdi_dcattr_batching_stale_dc_getters_return_failure_values) {
    REQUIRE(U(CreateCompatibleDC));
    HDC d = make_stale_dc();
    REQUIRE(d);
    EXPECT_EQ(U(GetBkColor)(d), CLR_INVALID);
    EXPECT_EQ(U(GetTextColor)(d), CLR_INVALID);
    EXPECT_EQ(U(GetBkMode)(d), 0);
    EXPECT_EQ(U(GetROP2)(d), 0);
    EXPECT_EQ(U(GetStretchBltMode)(d), 0);
    EXPECT_EQ(U(GetPolyFillMode)(d), 0);
    EXPECT_EQ(U(GetTextAlign)(d), GDI_ERROR);
    EXPECT_EQ(U(GetTextCharacterExtra)(d), (int)0x80000000);
    POINT p = { 5, 5 };
    SetLastError(0);
    CHECK(!U(GetCurrentPositionEx)(d, &p)); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SetLastError(0);
    EXPECT_EQ(U(GetMapMode)(d), 0); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SetLastError(0);
    CHECK(!U(GetBrushOrgEx)(d, &p)); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    CHECK(!U(GetWindowOrgEx)(d, &p));
    CHECK(!U(GetViewportOrgEx)(d, &p));
}

TEST(gdi_dcattr_batching_null_dc_getters_and_setters) {
    REQUIRE(U(SetBkColor)); REQUIRE(U(GetBkColor));
    /* A NULL HDC fails the cell lookup in every one of these paths. */
    EXPECT_EQ(U(GetBkColor)(NULL), CLR_INVALID);
    EXPECT_EQ(U(GetTextColor)(NULL), CLR_INVALID);
    EXPECT_EQ(U(GetBkMode)(NULL), 0);
    EXPECT_EQ(U(GetROP2)(NULL), 0);
    EXPECT_EQ(U(GetStretchBltMode)(NULL), 0);
    EXPECT_EQ(U(GetPolyFillMode)(NULL), 0);
    EXPECT_EQ(U(GetTextAlign)(NULL), GDI_ERROR);
    EXPECT_EQ(U(SetBkColor)(NULL, 1), CLR_INVALID);
    EXPECT_EQ(U(SetTextColor)(NULL, 1), CLR_INVALID);
    EXPECT_EQ(U(SetBkMode)(NULL, 1), 0);
    EXPECT_EQ(U(SetROP2)(NULL, 1), 0);
    EXPECT_EQ(U(SetMapMode)(NULL, MM_TEXT), 0);
    EXPECT_EQ(U(SetStretchBltMode)(NULL, 1), 0);
    EXPECT_EQ(U(SetPolyFillMode)(NULL, 1), 0);
    EXPECT_EQ(U(SetTextAlign)(NULL, 0), GDI_ERROR);
    SetLastError(0);
    CHECK(!U(MoveToEx)(NULL, 1, 1, NULL));
    DWORD e = GetLastError();
    t_note("MoveToEx(NULL) last error %lu (pldcGet path uses 6, GdiGetEntry path 87)", e);
    POINT p;
    CHECK(!U(GetCurrentPositionEx)(NULL, &p));
}

TEST(gdi_dcattr_batching_getcurrentpositionex_null_point_fails) {
    NOTWINE("Wine faults on a NULL out-pointer where win32k validates it");
    FXT(f, 8, 8);
    SetLastError(0);
    CHECK(!U(GetCurrentPositionEx)(f.dc, NULL));      /* lppt == NULL is part of the same rejection condition */
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SetLastError(0);
    CHECK(!U(GetBrushOrgEx)(f.dc, NULL));
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    CHECK(!U(GetWindowOrgEx)(f.dc, NULL));
    CHECK(!U(GetViewportOrgEx)(f.dc, NULL));
    fx_close(&f);
}

TEST(gdi_dcattr_batching_non_dc_handle_rejected_by_dc_setters) {
    FXT(f, 8, 8);
    /* A bitmap handle has object type 5, so GdiGetEntry's wType == 1 requirement fails. */
    HGDIOBJ hb = f.bmp;
    SetLastError(0);
    EXPECT_EQ(U(SetBkColor)((HDC)hb, 1), CLR_INVALID);
    EXPECT_EQ(U(GetBkColor)((HDC)hb), CLR_INVALID);
    EXPECT_EQ(U(GetROP2)((HDC)hb), 0);
    EXPECT_EQ(U(SetROP2)((HDC)hb, R2_NOT), 0);
    HGDIOBJ pen = U(GetStockObject)(BLACK_PEN);
    EXPECT_EQ(U(GetBkMode)((HDC)pen), 0);
    EXPECT_EQ(U(GetTextAlign)((HDC)pen), GDI_ERROR);
    fx_close(&f);
}
