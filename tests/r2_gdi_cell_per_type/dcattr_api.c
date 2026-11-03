/* DC_ATTR (cell+0x10 of a DC cell) against the gdi32 setters, for every kind of DC rather than only a memory DC.
 * Offsets and rules come from gdi32full!SetBkColor, SetTextColorImpl, SetDCBrushColor, SetROP2:
 *   +0x98 ulDirty_      bits ORed in when a translated colour changes (bk: 0xb, text: 0x7, DC brush: 0x1)
 *   +0xa0 hbrush
 *   +0xb0/+0xb4 bk colour translated/raw, +0xb8/+0xbc text colour translated/raw, +0xc0/+0xc4 DC brush colour,
 *   +0xcc DC pen colour, +0xd4 jROP2 (byte)
 * With ICM off the translated copy is the raw colour & 0x13ffffff. */
#include "r2.h"

typedef HGDIOBJ (*dk_mk)(void);
typedef void (*dk_rm)(HGDIOBJ);
struct dkind { const char *name; dk_mk mk; dk_rm rm; };

static HGDIOBJ mk_getdc2(void) { return U(GetDC)(NULL); }
static void rm_getdc2(HGDIOBJ h) { U(ReleaseDC)(NULL, (HDC)h); }
static const struct dkind dk_mem  = { "memory",  mk_dc_memory,      rm_dc };
static const struct dkind dk_disp = { "display", mk_dc_display,     rm_dc };
static const struct dkind dk_info = { "info",    mk_dc_info,        rm_dc };
static const struct dkind dk_comp = { "compat_of_display", mk_dc_compat_disp, rm_dc };
static const struct dkind dk_get  = { "getdc",   mk_getdc2,         rm_getdc2 };

static int da_read(HDC dc, unsigned off, void *out, size_t n) {
    struct gh_cell c;
    return gh_cell_of(dc, &c) && c.puser && k_try_read((void *)(ULONG_PTR)(c.puser + off), out, n);
}
static unsigned da32(HDC dc, unsigned off) { unsigned v = 0xdeadbeef; da_read(dc, off, &v, 4); return v; }

#define DK_BEGIN(k) \
    GH_NEED(); \
    HDC dc = (HDC)(k)->mk(); \
    if (!dc) { t_skip("cannot create a %s DC", (k)->name); return; } \
    struct gh_cell cell_; \
    if (!gh_cell_of(dc, &cell_) || !cell_.puser) { t_skip("no DC_ATTR for the %s DC", (k)->name); (k)->rm(dc); return; }
#define DK_END(k) (k)->rm(dc)

static void dk_rop2(const struct dkind *k) {
    DK_BEGIN(k);
    unsigned char b = 0xee;
    int prev = U(GetROP2)(dc);
    for (int r = R2_BLACK; r <= R2_WHITE; r++) {
        int old = U(SetROP2)(dc, r);
        CHECKF(old == prev, "%s: SetROP2(%d) returned %d, previous was %d", k->name, r, old, prev);
        CHECK(da_read(dc, 0xd4, &b, 1));
        CHECKF(b == r, "%s: jROP2=%u after SetROP2(%d)", k->name, b, r);
        CHECK(U(GetROP2)(dc) == r);
        prev = r;
    }
    U(SetROP2)(dc, R2_COPYPEN);
    DK_END(k);
}
static void dk_bkcolor(const struct dkind *k) {
    DK_BEGIN(k);
    COLORREF c1 = RGB(1, 2, 3), c2 = RGB(250, 100, 7);
    COLORREF old = U(GetBkColor)(dc);
    CHECK(U(SetBkColor)(dc, c1) == old);
    CHECKF(da32(dc, 0xb4) == c1, "%s: raw +0xb4 = 0x%x", k->name, da32(dc, 0xb4));
    CHECKF(da32(dc, 0xb0) == (c1 & 0x13ffffff), "%s: translated +0xb0 = 0x%x", k->name, da32(dc, 0xb0));
    /* a changed translated colour ORs fill|line|background into ulDirty_ */
    CHECKF((da32(dc, 0x98) & 0xb) == 0xb, "%s: ulDirty_=0x%x after SetBkColor", k->name, da32(dc, 0x98));
    CHECK(U(SetBkColor)(dc, c2) == c1);
    CHECKF(da32(dc, 0xb4) == c2 && da32(dc, 0xb0) == (c2 & 0x13ffffff), "%s: +0xb4=0x%x +0xb0=0x%x", k->name, da32(dc, 0xb4), da32(dc, 0xb0));
    CHECK(U(GetBkColor)(dc) == c2);
    U(SetBkColor)(dc, old);
    DK_END(k);
}
static void dk_textcolor(const struct dkind *k) {
    DK_BEGIN(k);
    COLORREF c1 = RGB(9, 8, 7), c2 = RGB(1, 255, 128);
    COLORREF old = U(GetTextColor)(dc);
    CHECK(U(SetTextColor)(dc, c1) == old);
    CHECKF(da32(dc, 0xbc) == c1 && da32(dc, 0xb8) == (c1 & 0x13ffffff), "%s: +0xbc=0x%x +0xb8=0x%x", k->name, da32(dc, 0xbc), da32(dc, 0xb8));
    CHECKF((da32(dc, 0x98) & 7) == 7, "%s: ulDirty_=0x%x after SetTextColor", k->name, da32(dc, 0x98));
    CHECK(U(SetTextColor)(dc, c2) == c1);
    CHECKF(da32(dc, 0xbc) == c2 && da32(dc, 0xb8) == (c2 & 0x13ffffff), "%s: +0xbc=0x%x +0xb8=0x%x", k->name, da32(dc, 0xbc), da32(dc, 0xb8));
    U(SetTextColor)(dc, old);
    DK_END(k);
}
static void dk_dcbrush(const struct dkind *k) {
    DK_BEGIN(k);
    COLORREF c1 = RGB(10, 20, 30), c2 = RGB(200, 100, 50);
    COLORREF old = U(GetDCBrushColor)(dc);
    CHECKF(old == RGB(255, 255, 255), "%s: default DC brush colour 0x%lx", k->name, old);
    CHECK(U(SetDCBrushColor)(dc, c1) == old);
    CHECKF(da32(dc, 0xc4) == c1 && da32(dc, 0xc0) == (c1 & 0x13ffffff), "%s: +0xc4=0x%x +0xc0=0x%x", k->name, da32(dc, 0xc4), da32(dc, 0xc0));
    CHECKF((da32(dc, 0x98) & 1) == 1, "%s: ulDirty_=0x%x after SetDCBrushColor", k->name, da32(dc, 0x98));
    CHECK(U(SetDCBrushColor)(dc, c2) == c1);
    CHECK(U(GetDCBrushColor)(dc) == c2 && da32(dc, 0xc4) == c2);
    /* an invalid HDC reports CLR_INVALID and ERROR_INVALID_PARAMETER and writes nothing */
    SetLastError(0);
    CHECK(U(SetDCBrushColor)((HDC)(ULONG_PTR)0x1234, c1) == CLR_INVALID);
    U(SetDCBrushColor)(dc, old);
    DK_END(k);
}
static void dk_dcpen(const struct dkind *k) {
    DK_BEGIN(k);
    COLORREF old = U(GetDCPenColor)(dc);
    CHECKF(old == RGB(0, 0, 0), "%s: default DC pen colour 0x%lx", k->name, old);
    CHECK(U(SetDCPenColor)(dc, RGB(11, 22, 33)) == old);
    CHECKF(da32(dc, 0xcc) == RGB(11, 22, 33), "%s: +0xcc=0x%x", k->name, da32(dc, 0xcc));
    CHECK(U(GetDCPenColor)(dc) == RGB(11, 22, 33));
    U(SetDCPenColor)(dc, old);
    DK_END(k);
}
static void dk_selected_stock(const struct dkind *k) {
    DK_BEGIN(k);
    /* a fresh DC has the stock WHITE_BRUSH / BLACK_PEN selected; their handles are stored at +0xa0 / +0xa8 */
    unsigned long long hb = 0, hp = 0;
    CHECK(da_read(dc, 0xa0, &hb, 8) && da_read(dc, 0xa8, &hp, 8));
    CHECKF((unsigned)hb == r2_lo32(U(GetStockObject)(WHITE_BRUSH)), "%s: hbrush 0x%llx", k->name, hb);
    CHECKF((unsigned)hp == r2_lo32(U(GetStockObject)(BLACK_PEN)), "%s: hpen 0x%llx", k->name, hp);
    CHECK(U(GetCurrentObject)(dc, OBJ_BRUSH) == U(GetStockObject)(WHITE_BRUSH));
    DK_END(k);
}
#define GENDK(kn) \
    TEST(r2_gdi_cell_per_type_dcattr_##kn##_rop2)       { dk_rop2(&dk_##kn); } \
    TEST(r2_gdi_cell_per_type_dcattr_##kn##_bkcolor)    { dk_bkcolor(&dk_##kn); } \
    TEST(r2_gdi_cell_per_type_dcattr_##kn##_textcolor)  { dk_textcolor(&dk_##kn); } \
    TEST(r2_gdi_cell_per_type_dcattr_##kn##_dcbrush)    { dk_dcbrush(&dk_##kn); } \
    TEST(r2_gdi_cell_per_type_dcattr_##kn##_dcpen)      { dk_dcpen(&dk_##kn); } \
    TEST(r2_gdi_cell_per_type_dcattr_##kn##_selected_stock) { dk_selected_stock(&dk_##kn); }
GENDK(mem) GENDK(disp) GENDK(info) GENDK(comp) GENDK(get)

/* ---- DC brush colour reaches the drawing (kernel reads +0xc4 via the dirty flags) and the stock BRUSHATTR stays put ---- */
TEST(r2_gdi_cell_per_type_dcattr_dcbrush_color_draws) {
    GH_NEED();
    BITMAPINFO bi; void *bits = NULL;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = 2; bi.bmiHeader.biHeight = -2;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    HBITMAP dib = U(CreateDIBSection)(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    HDC dc = U(CreateCompatibleDC)(NULL);
    if (!dib || !bits || !dc) { if (dc) U(DeleteDC)(dc); if (dib) U(DeleteObject)(dib); t_skip("no DIB"); return; }
    HGDIOBJ ob = U(SelectObject)(dc, dib);
    HGDIOBJ oldb = U(SelectObject)(dc, U(GetStockObject)(DC_BRUSH));
    static const COLORREF cols[] = { RGB(255, 0, 0), RGB(10, 200, 30), RGB(0, 0, 1) };
    for (size_t i = 0; i < 3; i++) {
        U(SetDCBrushColor)(dc, cols[i]);
        CHECK(U(PatBlt)(dc, 0, 0, 2, 2, PATCOPY));
        U(GdiFlush)();
        unsigned want = GetRValue(cols[i]) << 16 | GetGValue(cols[i]) << 8 | GetBValue(cols[i]);
        CHECKF((((unsigned *)bits)[0] & 0xffffff) == want && (((unsigned *)bits)[3] & 0xffffff) == want, "DC brush 0x%lx: pixels 0x%08x 0x%08x want 0x%06x", cols[i], ((unsigned *)bits)[0], ((unsigned *)bits)[3], want);
    }
    U(SelectObject)(dc, oldb);
    U(SelectObject)(dc, ob);
    U(DeleteDC)(dc);
    U(DeleteObject)(dib);
}
TEST(r2_gdi_cell_per_type_dcattr_dcbrush_leaves_stock_brushattr_alone) {
    GH_NEED();
    HGDIOBJ st = U(GetStockObject)(DC_BRUSH);
    HDC dc = U(CreateCompatibleDC)(NULL);
    struct gh_cell a, b;
    unsigned a0 = 0, a1 = 0, b0 = 0, b1 = 0;
    REQUIRE(st && dc);
    CHECK(gh_cell_of(st, &a));
    if (a.puser) { r2_attr(&a, 0, &a0); r2_attr(&a, 1, &a1); }
    HGDIOBJ old = U(SelectObject)(dc, st);
    U(SetDCBrushColor)(dc, RGB(77, 88, 99));
    U(SelectObject)(dc, old);
    CHECK(gh_cell_of(st, &b));
    CHECKF(a.puser == b.puser && a.q0 == b.q0 && a.upper == b.upper && a.objt == b.objt, "stock DC_BRUSH cell changed");
    if (b.puser) { r2_attr(&b, 0, &b0); r2_attr(&b, 1, &b1); }
    /* SetDCBrushColor writes only the DC's own DC_ATTR; the shared stock brush object keeps its attribute dwords */
    if (!a.puser) t_note("the stock DC_BRUSH has no attribute block on this system; only the cell is compared");
    else {
        CHECKF(a0 == b0 && a1 == b1, "stock DC_BRUSH attr %x/%x -> %x/%x", a0, a1, b0, b1);
        CHECK((a0 & 1) == 0);
    }
    U(DeleteDC)(dc);
}
TEST(r2_gdi_cell_per_type_dcattr_two_dcs_keep_separate_dcbrush_colours) {
    GH_NEED();
    HDC a = U(CreateCompatibleDC)(NULL), b = U(CreateCompatibleDC)(NULL);
    REQUIRE(a && b);
    U(SetDCBrushColor)(a, RGB(1, 1, 1));
    U(SetDCBrushColor)(b, RGB(2, 2, 2));
    CHECK(da32(a, 0xc4) == RGB(1, 1, 1) && da32(b, 0xc4) == RGB(2, 2, 2));
    CHECK(U(GetDCBrushColor)(a) == RGB(1, 1, 1) && U(GetDCBrushColor)(b) == RGB(2, 2, 2));
    U(DeleteDC)(a);
    U(DeleteDC)(b);
}
