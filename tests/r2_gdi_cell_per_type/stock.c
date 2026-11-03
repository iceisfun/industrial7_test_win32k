/* Stock object cells (GetStockObject 0..19, except the undefined index 9): public objects owned by nobody.
 * win32kbase!bSetStockObject ORs 0x800000 into the handle (cell type byte | 0x80) and the owner word is 0 forever,
 * so no lock bit, no pid, and NtGdiDeleteObjectApp's stock-bit early-out returns TRUE without releasing or caching. */
#include "r2.h"

struct stk { int idx; unsigned t7; const char *name; };
static const struct stk stks[] = {
    { WHITE_BRUSH, 0x10, "white_brush" }, { LTGRAY_BRUSH, 0x10, "ltgray_brush" }, { GRAY_BRUSH, 0x10, "gray_brush" },
    { DKGRAY_BRUSH, 0x10, "dkgray_brush" }, { BLACK_BRUSH, 0x10, "black_brush" }, { NULL_BRUSH, 0x10, "null_brush" },
    { WHITE_PEN, 0x30, "white_pen" }, { BLACK_PEN, 0x30, "black_pen" }, { NULL_PEN, 0x30, "null_pen" },
    { OEM_FIXED_FONT, 0x0a, "oem_fixed_font" }, { ANSI_FIXED_FONT, 0x0a, "ansi_fixed_font" }, { ANSI_VAR_FONT, 0x0a, "ansi_var_font" },
    { SYSTEM_FONT, 0x0a, "system_font" }, { DEVICE_DEFAULT_FONT, 0x0a, "device_default_font" }, { DEFAULT_PALETTE, 0x08, "default_palette" },
    { SYSTEM_FIXED_FONT, 0x0a, "system_fixed_font" }, { DEFAULT_GUI_FONT, 0x0a, "default_gui_font" },
    { DC_BRUSH, 0x10, "dc_brush" }, { DC_PEN, 0x30, "dc_pen" },
};

static void st_public(const struct stk *s) {
    GH_NEED();
    HGDIOBJ h = U(GetStockObject)(s->idx);
    struct gh_cell c;
    REQUIRE(h);
    CHECK(gh_cell_of(h, &c));
    /* NtGdiDeleteObjectApp treats (owner & ~1) == 0 as public; only the lock bit may be transiently set */
    CHECKF((c.owner & ~1u) == 0, "%s: owner word 0x%x (public objects have no pid)", s->name, c.owner);
    CHECKF((c.flags & 0x20) == 0, "%s: flags 0x%02x", s->name, c.flags);
    CHECKF((c.upper & 0xff) == (s->t7 | 0x80), "%s: cell type byte 0x%02x want 0x%02x", s->name, c.upper & 0xff, s->t7 | 0x80);
    CHECKF(GH_TYPE7(h) == s->t7 && GH_STOCKED(h), "%s: handle %p", s->name, h);
    CHECKF(c.objt == (s->t7 & 0x1f), "%s: objt 0x%x", s->name, c.objt);
    CHECKF(c.q0 == (0xffffffffff000000ull | ((unsigned long long)GH_UNIQ(h) << 16) | GH_IDX(h)), "%s: q0 0x%llx", s->name, c.q0);
    if (c.puser) {
        CHECKF(c.puser < GH_KERNEL_FLOOR, "%s: pUser 0x%llx", s->name, c.puser);
        CHECKF((c.puser & 3) == 0, "%s: pUser 0x%llx misaligned", s->name, c.puser);
    }
}
static void st_not_cached(const struct stk *s) {
    GH_NEED();
    HGDIOBJ h = U(GetStockObject)(s->idx);
    struct gh_cell a, b;
    struct r2_cache c;
    REQUIRE(h);
    CHECK(gh_cell_of(h, &a));
    int g0 = r2_gr(0);
    U(DeleteObject)(h);
    CHECK(gh_cell_of(h, &b));
    CHECKF(a.q0 == b.q0 && a.upper == b.upper && a.objt == b.objt && a.puser == b.puser && (a.owner & ~1u) == (b.owner & ~1u), "%s: cell changed by DeleteObject", s->name);
    CHECK(U(GetStockObject)(s->idx) == h);
    CHECKF(r2_gr(0) == g0, "%s: GR_GDIOBJECTS %d -> %d", s->name, g0, r2_gr(0));
    if (r2_cache_read(&c)) CHECKF(r2_cache_find(&c, GH_IDX(h)) < 0, "%s: stock cell 0x%x appeared in the PEB handle cache", s->name, GH_IDX(h));
    unsigned af;
    if (a.puser && (a.objt == 0x10) && r2_attr(&a, 0, &af)) CHECKF((af & 1) == 0, "%s: AttrFlags 0x%x has the parked bit", s->name, af);
}
#define GENS(n, i) \
    TEST(r2_gdi_cell_per_type_stock_##n##_public_cell)  { st_public(&stks[i]); } \
    TEST(r2_gdi_cell_per_type_stock_##n##_never_cached) { st_not_cached(&stks[i]); }
GENS(white_brush, 0) GENS(ltgray_brush, 1) GENS(gray_brush, 2) GENS(dkgray_brush, 3) GENS(black_brush, 4)
GENS(null_brush, 5) GENS(white_pen, 6) GENS(black_pen, 7) GENS(null_pen, 8) GENS(oem_fixed_font, 9)
GENS(ansi_fixed_font, 10) GENS(ansi_var_font, 11) GENS(system_font, 12) GENS(device_default_font, 13)
GENS(default_palette, 14) GENS(system_fixed_font, 15) GENS(default_gui_font, 16) GENS(dc_brush, 17) GENS(dc_pen, 18)

TEST(r2_gdi_cell_per_type_stock_attr_slots_unique) {
    GH_NEED();
    unsigned long long pu[sizeof stks / sizeof stks[0]];
    size_t n = sizeof stks / sizeof stks[0];
    for (size_t i = 0; i < n; i++) {
        struct gh_cell c;
        HGDIOBJ h = U(GetStockObject)(stks[i].idx);
        pu[i] = (h && gh_cell_of(h, &c)) ? c.puser : 0;
    }
    for (size_t i = 0; i < n; i++)
        for (size_t j = i + 1; j < n; j++)
            if (pu[i] && pu[j]) CHECKF(pu[i] != pu[j], "%s and %s share pUser 0x%llx", stks[i].name, stks[j].name, pu[i]);
}
TEST(r2_gdi_cell_per_type_stock_owned_population_excludes_stock) {
    GH_NEED();
    int cnt[32];
    int before = r2_scan(GH_PID(), cnt);
    for (size_t i = 0; i < sizeof stks / sizeof stks[0]; i++) U(GetStockObject)(stks[i].idx);
    int after = r2_scan(GH_PID(), cnt);
    /* stock cells have owner 0, so they are in nobody's population and GetGuiResources never counts them */
    CHECKF(before == after, "population %d -> %d after touching every stock object", before, after);
    CHECK(r2_gr(0) == after);
}
TEST(r2_gdi_cell_per_type_stock_window_edges) {
    GH_NEED();
    /* gdi32full!GetStockObject accepts 0..0x15 and answers NULL above; indices that name no object give NULL or a stock-bit handle */
    CHECK(U(GetStockObject)(0x16) == NULL);
    CHECK(U(GetStockObject)(0x100) == NULL);
    CHECK(U(GetStockObject)(-1) == NULL);
    for (int i = 9; i <= 21; i += (i == 9 ? 11 : 1)) {
        HGDIOBJ h = U(GetStockObject)(i);
        if (h) CHECKF(GH_STOCKED(h), "GetStockObject(%d)=%p lacks the stock bit", i, h);
    }
}
