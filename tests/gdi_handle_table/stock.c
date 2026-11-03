/* Stock objects: handles come out of the shared GDI section (gdi32full!GetStockObject reads
 * base + 0x1800b0 + i*8), carry the stock bit (the kernel ORs 0x800000 and
 * retypes the handle), and are public: GetObjectType works for them in every process. */
#include "gh.h"

struct gh_stock { int idx; unsigned type7; DWORD objtype; const char *name; };
#define STOCK_BRUSH 0x10
#define STOCK_PEN   0x30
#define STOCK_FONT  0x0a
#define STOCK_PAL   0x08

static const struct gh_stock gh_stocks[] = {
    { WHITE_BRUSH,  STOCK_BRUSH, OBJ_BRUSH, "white_brush" },
    { LTGRAY_BRUSH, STOCK_BRUSH, OBJ_BRUSH, "ltgray_brush" },
    { GRAY_BRUSH,   STOCK_BRUSH, OBJ_BRUSH, "gray_brush" },
    { DKGRAY_BRUSH, STOCK_BRUSH, OBJ_BRUSH, "dkgray_brush" },
    { BLACK_BRUSH,  STOCK_BRUSH, OBJ_BRUSH, "black_brush" },
    { NULL_BRUSH,   STOCK_BRUSH, OBJ_BRUSH, "null_brush" },
    { WHITE_PEN,    STOCK_PEN,   OBJ_PEN,   "white_pen" },
    { BLACK_PEN,    STOCK_PEN,   OBJ_PEN,   "black_pen" },
    { NULL_PEN,     STOCK_PEN,   OBJ_PEN,   "null_pen" },
    { OEM_FIXED_FONT,    STOCK_FONT, OBJ_FONT, "oem_fixed_font" },
    { ANSI_FIXED_FONT,   STOCK_FONT, OBJ_FONT, "ansi_fixed_font" },
    { ANSI_VAR_FONT,     STOCK_FONT, OBJ_FONT, "ansi_var_font" },
    { SYSTEM_FONT,       STOCK_FONT, OBJ_FONT, "system_font" },
    { DEVICE_DEFAULT_FONT, STOCK_FONT, OBJ_FONT, "device_default_font" },
    { DEFAULT_PALETTE,   STOCK_PAL,  OBJ_PAL,  "default_palette" },
    { SYSTEM_FIXED_FONT, STOCK_FONT, OBJ_FONT, "system_fixed_font" },
    { DEFAULT_GUI_FONT,  STOCK_FONT, OBJ_FONT, "default_gui_font" },
    { DC_BRUSH,     STOCK_BRUSH, OBJ_BRUSH, "dc_brush" },
    { DC_PEN,       STOCK_PEN,   OBJ_PEN,   "dc_pen" },
};
#define NSTOCK (sizeof gh_stocks / sizeof gh_stocks[0])

/* gdi32full!GetStockObject: slot i of the section after the cell array. */
static int gh_stock_slot(int i, HGDIOBJ *out, int alt) {
    void *b = gh_base();
    unsigned long long v = 0;
    if (!b) return 0;
    if (!k_try_read((char *)b + 0x1800b0 + (ULONG_PTR)i * 8 + (alt ? 0xb0 : 0), &v, 8)) return 0;
    *out = (HGDIOBJ)(ULONG_PTR)v;
    return 1;
}

static void st_handle(const struct gh_stock *s) {
    GH_NEED();
    HGDIOBJ h = U(GetStockObject)(s->idx);
    CHECKF(h != NULL, "GetStockObject(%d)", s->idx);
    if (!h) return;
    CHECKF(GH_STOCKED(h), "%s handle %p has no stock bit", s->name, h);
    CHECKF(GH_TYPE7(h) == s->type7, "%s type bits 0x%x want 0x%x", s->name, GH_TYPE7(h), s->type7);
    CHECK((ULONG_PTR)h == (ULONG_PTR)(LONG_PTR)(int)(unsigned)(ULONG_PTR)h);
    CHECKF(U(GetStockObject)(s->idx) == h, "%s unstable across calls", s->name);
    CHECKF(U(GetObjectType)(h) == s->objtype, "%s GetObjectType=%lu want %lu", s->name, U(GetObjectType)(h), s->objtype);
}
static void st_cell(const struct gh_stock *s) {
    GH_NEED();
    HGDIOBJ h = U(GetStockObject)(s->idx);
    struct gh_cell c;
    REQUIRE(h);
    if (!gh_cell_of(h, &c)) { CHECK(0); return; }
    CHECKF(c.upper == GH_HIWORD(h), "%s upper=0x%04x HIWORD=0x%04x", s->name, c.upper, GH_HIWORD(h));
    CHECKF(c.objt == (s->type7 & 0x1f), "%s objt=0x%02x", s->name, c.objt);
    CHECKF(c.q0 >= GH_KERNEL_FLOOR, "%s q0=0x%llx", s->name, c.q0);
    /* public objects: GetObjectType succeeds in every process, which requires owner 0 (gdi32full!GetObjectType) */
    CHECKF((c.owner & ~1u) == 0, "%s owner=0x%x", s->name, c.owner);
    if (c.puser) CHECKF(c.puser < GH_KERNEL_FLOOR, "%s pUser 0x%llx", s->name, c.puser);
    t_note("%s handle=%p flags=0x%02x pUser=0x%llx", s->name, h, c.flags, c.puser);
}
static void st_slot(const struct gh_stock *s) {
    GH_NEED();
    HGDIOBJ h = U(GetStockObject)(s->idx), a = NULL, b = NULL;
    REQUIRE(h);
    if (!gh_stock_slot(s->idx, &a, 0)) { t_skip("shared section slot unreadable"); return; }
    if (s->idx == DEFAULT_GUI_FONT) { t_note("DEFAULT_GUI_FONT is DPI dependent: slot=%p handle=%p", a, h); return; }
    if (s->idx == OEM_FIXED_FONT || s->idx == SYSTEM_FONT || s->idx == SYSTEM_FIXED_FONT) {
        /* GetStockObject takes the +0x180160 slot when the thread's DPI awareness is unaware and that slot is filled */
        gh_stock_slot(s->idx, &b, 1);
        CHECKF(h == a || h == b, "%s handle %p slots %p/%p", s->name, h, a, b);
    } else CHECKF(h == a, "%s handle %p shared slot %p", s->name, h, a);
}
static void st_delete(const struct gh_stock *s) {
    GH_NEED();
    REQUIRE(UN(NtGdiDeleteObjectApp));
    HGDIOBJ h = U(GetStockObject)(s->idx);
    struct gh_cell before, after;
    REQUIRE(h);
    if (!gh_cell_of(h, &before)) { CHECK(0); return; }
    /* NtGdiDeleteObjectApp: bit 23 set -> return 1 immediately, nothing is freed */
    CHECK(UN(NtGdiDeleteObjectApp)((HANDLE)h) == 1);
    BOOL r = U(DeleteObject)(h);
    t_note("%s DeleteObject(stock)=%d err=%lu", s->name, r, GetLastError());
    CHECK(gh_cell_of(h, &after));
    CHECK(after.objt == before.objt);
    CHECK(after.upper == before.upper);
    CHECK(after.q0 == before.q0);
    CHECK(U(GetStockObject)(s->idx) == h);
    CHECK(U(GetObjectType)(h) == s->objtype);
}

#define GENS(n, i) \
    TEST(gdi_handle_table_stock_##n##_handle)     { st_handle(&gh_stocks[i]); } \
    TEST(gdi_handle_table_stock_##n##_cell)       { st_cell(&gh_stocks[i]); } \
    TEST(gdi_handle_table_stock_##n##_shared_slot) { st_slot(&gh_stocks[i]); } \
    TEST(gdi_handle_table_stock_##n##_delete_noop) { st_delete(&gh_stocks[i]); }
GENS(white_brush, 0) GENS(ltgray_brush, 1) GENS(gray_brush, 2) GENS(dkgray_brush, 3) GENS(black_brush, 4)
GENS(null_brush, 5) GENS(white_pen, 6) GENS(black_pen, 7) GENS(null_pen, 8) GENS(oem_fixed_font, 9)
GENS(ansi_fixed_font, 10) GENS(ansi_var_font, 11) GENS(system_font, 12) GENS(device_default_font, 13)
GENS(default_palette, 14) GENS(system_fixed_font, 15) GENS(default_gui_font, 16) GENS(dc_brush, 17) GENS(dc_pen, 18)

TEST(gdi_handle_table_stock_distinct_handles) {
    GH_NEED();
    /* all stock objects other than the DPI-dependent fonts are separate table entries */
    for (size_t i = 0; i < NSTOCK; i++)
        for (size_t j = i + 1; j < NSTOCK; j++) {
            HGDIOBJ a = U(GetStockObject)(gh_stocks[i].idx), b = U(GetStockObject)(gh_stocks[j].idx);
            if (!a || !b) continue;
            if (gh_stocks[i].type7 == STOCK_FONT && gh_stocks[j].type7 == STOCK_FONT) { if (a == b) t_note("%s and %s are the same font object", gh_stocks[i].name, gh_stocks[j].name); }
            else CHECKF(a != b, "%s and %s share handle %p", gh_stocks[i].name, gh_stocks[j].name, a);
        }
}
TEST(gdi_handle_table_stock_out_of_range_is_null) {
    GH_NEED();
    /* gdi32full!GetStockObject */
    int bad[] = { 22, 23, 100, 0xffff, -1, -2, 0x7fffffff, (int)0x80000000 };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) CHECKF(U(GetStockObject)(bad[i]) == NULL, "GetStockObject(%d)", bad[i]);
}
TEST(gdi_handle_table_stock_undefined_indices) {
    GH_NEED();
    /* indices 9, 20, 21 are in the accepted 0..0x15 window but name no object */
    int idx[] = { 9, 20, 21 };
    for (int i = 0; i < 3; i++) {
        HGDIOBJ h = U(GetStockObject)(idx[i]);
        t_note("GetStockObject(%d)=%p", idx[i], h);
        if (h) CHECK(GH_STOCKED(h));
    }
}
TEST(gdi_handle_table_stock_default_dc_selection) {
    GH_NEED();
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    /* A new DC starts with the stock white brush and black pen selected (DC_ATTR hbrush/hpen, read by GetCurrentObject) */
    CHECK(U(GetCurrentObject)(dc, OBJ_BRUSH) == U(GetStockObject)(WHITE_BRUSH));
    CHECK(U(GetCurrentObject)(dc, OBJ_PEN) == U(GetStockObject)(BLACK_PEN));
    HGDIOBJ bmp = U(GetCurrentObject)(dc, OBJ_BITMAP);
    CHECK(bmp != NULL);
    if (bmp) CHECK(GH_TYPE7(bmp) == 0x05);
    HGDIOBJ pal = U(GetCurrentObject)(dc, OBJ_PAL);
    t_note("default palette %p stock default %p", pal, U(GetStockObject)(DEFAULT_PALETTE));
    U(DeleteDC)(dc);
}
TEST(gdi_handle_table_stock_select_keeps_handle) {
    GH_NEED();
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    HGDIOBJ nb = U(GetStockObject)(NULL_BRUSH), np = U(GetStockObject)(NULL_PEN);
    HGDIOBJ ob = U(SelectObject)(dc, nb), op = U(SelectObject)(dc, np);
    CHECK(ob == U(GetStockObject)(WHITE_BRUSH));
    CHECK(op == U(GetStockObject)(BLACK_PEN));
    CHECK(U(GetCurrentObject)(dc, OBJ_BRUSH) == nb);
    CHECK(U(GetCurrentObject)(dc, OBJ_PEN) == np);
    U(SelectObject)(dc, ob); U(SelectObject)(dc, op);
    U(DeleteDC)(dc);
}
TEST(gdi_handle_table_stock_gui_count_unchanged) {
    GH_NEED();
    REQUIRE(U(GetGuiResources));
    DWORD a = U(GetGuiResources)(GetCurrentProcess(), 0);
    for (int i = 0; i < 20; i++) U(GetStockObject)(i);
    CHECK(U(GetGuiResources)(GetCurrentProcess(), 0) == a);
}
