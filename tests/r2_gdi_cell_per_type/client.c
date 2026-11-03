/* Client-side GDI objects (Objt 6: metafiles and metafile DCs) and the transfer flag of a DC cell.
 *   NtGdiCreateClientObj : accepts a type word iff (t & 0x600000) != 0, (t & 0xff00ffff) == 0 and
 *                                     (t & 0x1f0000) == 0x60000, i.e. type7 in {0x26, 0x46, 0x66} below the stock bit
 *   NtGdiMakeObjectXferable / NtGdiMakeObjectUnXferable : stock-bit handle -> 1, otherwise only a DC
 *                                     (Objt 1) is eligible; making a handle non-transferable additionally needs the cell's Objt == 1,
 *                                     the HIWORD to match and the owner to be the caller, then clears Flags bit 0x20 */
#include "r2.h"

static int accepted_t7(unsigned t) { return (t & 0x60) != 0 && (t & 0x1f) == 6; }

TEST(r2_gdi_cell_per_type_client_type_sweep) {
    GH_NEED();
    REQUIRE(UN(NtGdiCreateClientObj));
    REQUIRE(UN(NtGdiDeleteClientObj));
    if (t_is_wine()) { t_skip("Wine's NtGdiCreateClientObj asserts on a zero object type"); return; }
    int made = 0;
    for (unsigned t = 0; t < 0x80; t++) {
        HANDLE h = UN(NtGdiCreateClientObj)(t << 16);
        if (accepted_t7(t)) {
            CHECKF(h != NULL, "type7 0x%02x refused", t);
            if (h) {
                struct gh_cell c;
                made++;
                CHECKF(GH_TYPE7(h) == t, "type7 0x%02x gave handle %p", t, h);
                CHECK(gh_cell_of(h, &c) && c.objt == 6 && c.puser == 0 && (c.owner & ~1u) == GH_PID());
                UN(NtGdiDeleteClientObj)(h);
            }
        } else {
            CHECKF(h == NULL, "type7 0x%02x produced %p", t, h);
            if (h) UN(NtGdiDeleteClientObj)(h);
        }
    }
    CHECKF(made == 3, "%d accepted types", made);
}
TEST(r2_gdi_cell_per_type_client_umpd_and_dcls_types_not_user_creatable) {
    GH_NEED();
    REQUIRE(UN(NtGdiCreateClientObj));
    if (t_is_wine()) { t_skip("Wine's NtGdiCreateClientObj asserts on a zero object type"); return; }
    /* type7 0x11 (UMPD), 0x21 (DC local storage / enhanced metafile DC) and 0x01 (DC) cannot be made by a client object call:
     * their low five bits are not 6 */
    static const unsigned t7[] = { 0x11, 0x21, 0x01, 0x31, 0x41, 0x61, 0x09, 0x0a };
    for (size_t i = 0; i < sizeof t7 / sizeof t7[0]; i++) {
        HANDLE h = UN(NtGdiCreateClientObj)(t7[i] << 16);
        CHECKF(h == NULL, "type7 0x%02x produced %p", t7[i], h);
        if (h) UN(NtGdiDeleteClientObj)(h);
    }
}
TEST(r2_gdi_cell_per_type_client_objects_survive_each_other_delete) {
    GH_NEED();
    REQUIRE(UN(NtGdiCreateClientObj));
    REQUIRE(UN(NtGdiDeleteClientObj));
    if (t_is_wine()) { t_skip("Wine's NtGdiCreateClientObj asserts on a zero object type"); return; }
    HANDLE a = UN(NtGdiCreateClientObj)(0x260000), b = UN(NtGdiCreateClientObj)(0x460000), c = UN(NtGdiCreateClientObj)(0x660000);
    struct gh_cell cb, cc;
    REQUIRE(a && b && c);
    CHECK(GH_IDX(a) != GH_IDX(b) && GH_IDX(b) != GH_IDX(c) && GH_IDX(a) != GH_IDX(c));
    UN(NtGdiDeleteClientObj)(a);
    CHECK(gh_cell_of(b, &cb) && cb.objt == 6 && cb.upper == GH_HIWORD(b));
    CHECK(gh_cell_of(c, &cc) && cc.objt == 6 && cc.upper == GH_HIWORD(c));
    CHECK(U(GetObjectType)((HGDIOBJ)b) == OBJ_ENHMETAFILE);
    CHECK(U(GetObjectType)((HGDIOBJ)c) == OBJ_METADC);
    UN(NtGdiDeleteClientObj)(b);
    UN(NtGdiDeleteClientObj)(c);
}
TEST(r2_gdi_cell_per_type_client_double_delete_is_harmless) {
    GH_NEED();
    REQUIRE(UN(NtGdiCreateClientObj));
    REQUIRE(UN(NtGdiDeleteClientObj));
    if (t_is_wine()) { t_skip("Wine's NtGdiCreateClientObj asserts on a zero object type"); return; }
    HANDLE a = UN(NtGdiCreateClientObj)(0x460000);
    REQUIRE(a);
    int base = r2_gr(0);
    UN(NtGdiDeleteClientObj)(a);
    int once = r2_gr(0);
    ULONG_PTR r = UN(NtGdiDeleteClientObj)(a);
    CHECKF((r & 0xff) == 0, "second NtGdiDeleteClientObj returned 0x%llx", (unsigned long long)r);
    CHECKF(r2_gr(0) == once && once <= base, "GR_GDIOBJECTS %d -> %d -> %d", base, once, r2_gr(0));
}

/* ---- transfer flag ---------------------------------------------------------------------------------- */
#define XF_NEED() do { GH_NEED(); if (t_is_wine()) { t_skip("Wine has no NtGdiMakeObject*Xferable"); return; } REQUIRE(UN(NtGdiMakeObjectUnXferable)); REQUIRE(UN(NtGdiMakeObjectXferable)); } while (0)

TEST(r2_gdi_cell_per_type_xfer_unxferable_own_dc_succeeds_and_keeps_flag_clear) {
    XF_NEED();
    static const struct gh_kind *const kinds[] = { &gk_dc_memory, &gk_dc_display, &gk_dc_info, &gk_dc_compat_disp };
    for (size_t i = 0; i < sizeof kinds / sizeof kinds[0]; i++) {
        HGDIOBJ h = kinds[i]->mk();
        struct gh_cell a, b;
        if (!h) { t_note("cannot create %s", kinds[i]->name); continue; }
        CHECK(gh_cell_of(h, &a));
        /* Objt 1, HIWORD and owner match -> Flags &= ~0x20, return 1 */
        CHECKF(UN(NtGdiMakeObjectUnXferable)((HANDLE)h) == 1, "%s: NtGdiMakeObjectUnXferable != 1", kinds[i]->name);
        CHECK(gh_cell_of(h, &b));
        CHECKF(b.flags == a.flags && (b.flags & 0x20) == 0, "%s: flags 0x%02x -> 0x%02x", kinds[i]->name, a.flags, b.flags);
        CHECK(b.q0 == a.q0 && b.upper == a.upper && b.puser == a.puser && b.owner == a.owner);
        kinds[i]->rm(h);
    }
}
TEST(r2_gdi_cell_per_type_xfer_unxferable_stock_bit_returns_one) {
    XF_NEED();
    for (int i = 0; i <= DC_PEN; i++) {
        HGDIOBJ h = U(GetStockObject)(i);
        if (!h) continue;
        CHECKF(UN(NtGdiMakeObjectUnXferable)((HANDLE)h) == 1, "stock %d: UnXferable != 1", i);
        CHECKF(UN(NtGdiMakeObjectXferable)((HANDLE)h, 0) == 1, "stock %d: Xferable != 1", i);
    }
}
TEST(r2_gdi_cell_per_type_xfer_non_dc_objects_are_refused) {
    XF_NEED();
    static const struct gh_kind *const kinds[] = { &gk_rgn_rect, &gk_bmp_mono, &gk_pal_logical, &gk_font_indirect, &gk_brush_hatch, &gk_pen_solid, &gk_extpen_geom, &gk_colorspace, &gk_emf, &gk_wmf };
    for (size_t i = 0; i < sizeof kinds / sizeof kinds[0]; i++) {
        HGDIOBJ h = kinds[i]->mk();
        struct gh_cell a, b;
        if (!h) { t_note("cannot create %s", kinds[i]->name); continue; }
        CHECK(gh_cell_of(h, &a));
        /* the type test is on the handle's low five type bits: only 1 (DC) passes */
        CHECKF(UN(NtGdiMakeObjectUnXferable)((HANDLE)h) == 0, "%s: UnXferable != 0", kinds[i]->name);
        CHECKF(UN(NtGdiMakeObjectXferable)((HANDLE)h, 0) == 0, "%s: Xferable != 0", kinds[i]->name);
        CHECK(gh_cell_of(h, &b));
        CHECKF(a.flags == b.flags && a.upper == b.upper && a.objt == b.objt, "%s: cell changed", kinds[i]->name);
        kinds[i]->rm(h);
    }
}
TEST(r2_gdi_cell_per_type_xfer_null_and_garbage_handles) {
    XF_NEED();
    CHECK(UN(NtGdiMakeObjectUnXferable)(NULL) == 0);
    CHECK(UN(NtGdiMakeObjectXferable)(NULL, 0) == 0);
    CHECK(UN(NtGdiMakeObjectUnXferable)((HANDLE)(ULONG_PTR)0x00010000) == 0);      /* DC type bits, index 0 */
    CHECK(UN(NtGdiMakeObjectUnXferable)((HANDLE)(ULONG_PTR)0x0001ffff) == 0);      /* DC type bits, last cell */
    CHECK(UN(NtGdiMakeObjectUnXferable)((HANDLE)(ULONG_PTR)0x00040001) == 0);      /* region type bits */
}
TEST(r2_gdi_cell_per_type_xfer_stale_and_wrong_reuse_byte) {
    XF_NEED();
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    ULONG_PTR good = (ULONG_PTR)dc;
    HANDLE wrong = (HANDLE)(good ^ 0x01000000);   /* other reuse byte */
    CHECKF(UN(NtGdiMakeObjectUnXferable)(wrong) == 0, "wrong reuse byte accepted");
    HANDLE wrongtype = (HANDLE)(good ^ 0x00050000); /* DC index with brush-ish type bits */
    CHECKF(UN(NtGdiMakeObjectUnXferable)(wrongtype) == 0, "wrong type bits accepted");
    CHECK(UN(NtGdiMakeObjectUnXferable)((HANDLE)dc) == 1);
    U(DeleteDC)(dc);
    /* released cell: Objt 0 */
    CHECKF(UN(NtGdiMakeObjectUnXferable)((HANDLE)dc) == 0, "deleted DC still unxferable-able");
    CHECKF(UN(NtGdiMakeObjectXferable)((HANDLE)dc, 0) == 0, "deleted DC xferable-able");
}
TEST(r2_gdi_cell_per_type_xfer_getdc_cached_dc_unxferable) {
    XF_NEED();
    HDC dc = U(GetDC)(NULL);
    struct gh_cell a, b;
    REQUIRE(dc);
    CHECK(gh_cell_of(dc, &a));
    /* NtGdiMakeObjectUnXferable succeeds only when the cell is owned by this process; whether USER's cached DC is owned or public
     * while checked out is not established, so only the outcome-dependent invariants are asserted */
    ULONG r = UN(NtGdiMakeObjectUnXferable)((HANDLE)dc);
    CHECKF(r == 0 || r == 1, "UnXferable returned %lu", r);
    if (r == 0) t_note("GetDC cell owner 0x%x flags 0x%02x: UnXferable refused", a.owner, a.flags);
    CHECK(gh_cell_of(dc, &b));
    CHECK(a.flags == b.flags || (r == 1 && b.flags == (a.flags & 0xdf)));
    CHECK((b.flags & 0x20) == 0 || r == 0);
    U(ReleaseDC)(NULL, dc);
}
