/* NtGdiColorCorrectPalette / ColorCorrectPalette: argument matrix (DC and palette handle types, first/count ranges
 * against the palette size) and the no-adjustment case: with colour management off, correcting a palette leaves its
 * entries unchanged. */
#include "xb.h"

static HPALETTE pal4(void) {
    struct { WORD ver, n; PALETTEENTRY e[4]; } lp = { 0x300, 4, { {1,2,3,0}, {40,50,60,0}, {70,80,90,0}, {250,251,252,0} } };
    return U(CreatePalette)((LOGPALETTE *)&lp);
}
/* name, dc kind (0 screen, 1 null, 2 junk, 3 screen with ICM on, 4 memory DC with ICM on), palette kind (0 pal4, 1 null, 2 junk, 3 stock, 4 brush), first, count */
#define CROWS(X) \
 X(ok_all,        0, 0, 0, 4, 0,2018) \
 X(ok_first1,     0, 0, 1, 3, 0,2018) \
 X(ok_last,       0, 0, 3, 1, 0,2018) \
 X(ok_one,        0, 0, 2, 1, 0,2018) \
 X(zero_count,    0, 0, 0, 0, 0,87) \
 X(zero_count_at_end, 0, 0, 4, 0, 0,87) \
 X(count_over,    0, 0, 0, 5, 0,2018) \
 X(first_eq_n,    0, 0, 4, 1, 0,2018) \
 X(first_over,    0, 0, 9, 1, 0,2018) \
 X(sum_over,      0, 0, 3, 2, 0,2018) \
 X(count_huge,    0, 0, 0, 0x40000000, 0,87) \
 X(count_max,     0, 0, 0, 0xFFFFFFFF, 0,87) \
 X(first_max,     0, 0, 0xFFFFFFFF, 1, 0,87) \
 X(first_max_count_max, 0, 0, 0xFFFFFFFF, 0xFFFFFFFF, 0,87) \
 X(null_dc,       1, 0, 0, 4, 0,87) \
 X(junk_dc,       2, 0, 0, 4, 0,87) \
 X(null_pal,      0, 1, 0, 4, 0,87) \
 X(junk_pal,      0, 2, 0, 4, 0,2018) \
 X(stock_pal,     0, 3, 0, 20, 0,87) \
 X(stock_pal_over, 0, 3, 0, 21, 0,87) \
 X(brush_as_pal,  0, 4, 0, 4, 0,2018) \
 X(icm_ok_all,    3, 0, 0, 4, 1,0) \
 X(icm_ok_first1, 3, 0, 1, 3, 1,0) \
 X(icm_ok_last,   3, 0, 3, 1, 1,0) \
 X(icm_ok_one,    3, 0, 2, 1, 1,0) \
 X(icm_zero_count, 3, 0, 0, 0, 0,87) \
 X(icm_count_over, 3, 0, 0, 5, 1,0) \
 X(icm_first_eq_n, 3, 0, 4, 1, 1,0) \
 X(icm_first_over, 3, 0, 9, 1, 1,0) \
 X(icm_sum_over,  3, 0, 3, 2, 1,0) \
 X(icm_count_huge, 3, 0, 0, 0x40000000, 0,87) \
 X(icm_null_pal,  3, 1, 0, 4, 0,87) \
 X(icm_junk_pal,  3, 2, 0, 4, 1,0) \
 X(icm_stock_pal, 3, 3, 0, 20, 0,87) \
 X(icm_stock_pal_over, 3, 3, 0, 21, 0,87) \
 X(icm_brush_as_pal, 3, 4, 0, 4, 1,0) \
 X(icm_memdc_ok_all, 4, 0, 0, 4, 1,0) \
 X(icm_memdc_ok_one, 4, 0, 1, 1, 1,0)

struct crow { const char *nm; int dk, pk; ULONG first, n; int nz, err; };
#define CS_(nm, d, p, f, n, enz, ee) { #nm, d, p, f, n, enz, ee },
static const struct crow crows[] = { CROWS(CS_) };
static void cc_run(const struct crow *r) {
    REQUIRE(U(ColorCorrectPalette)); REQUIRE(UN(NtGdiColorCorrectPalette));
    HDC scr = U(GetDC)(NULL); HPALETTE own = pal4(); HBRUSH br = U(CreateSolidBrush)(5);
    HDC mdc = U(CreateCompatibleDC)(NULL);
    HDC dc = (r->dk == 0 || r->dk == 3) ? scr : r->dk == 4 ? mdc : r->dk == 1 ? NULL : (HDC)(ULONG_PTR)0x00010123;
    if (r->dk >= 3) { if (!U(SetICMMode)(dc, ICM_ON)) { t_skip("ICM cannot be enabled on this DC"); goto out; } }
    HPALETTE pl = r->pk == 0 ? own : r->pk == 1 ? NULL : r->pk == 2 ? (HPALETTE)(ULONG_PTR)0x1234 : r->pk == 3 ? (HPALETTE)U(GetStockObject)(DEFAULT_PALETTE) : (HPALETTE)br;
    PALETTEENTRY before[4] = {{0}}, after[4] = {{0}};
    if (own) U(GetPaletteEntries)(own, 0, 4, before);
    SetLastError(0);
    BOOL a = U(ColorCorrectPalette)(dc, pl, r->first, r->n); DWORD e = GetLastError();
    xb_row(r->nm, a != 0, e, r->nz, r->err);
    if (own) { U(GetPaletteEntries)(own, 0, 4, after); CHECKF(!memcmp(before, after, sizeof before), "%s: palette entries changed", r->nm); }
    if (r->dk >= 3) U(SetICMMode)(dc, ICM_OFF);
out:
    U(DeleteObject)(br); if (own) U(DeleteObject)(own); U(DeleteDC)(mdc); U(ReleaseDC)(NULL, scr);
}
XB_GROUP(colorcorrect_range_without_icm, crows, cc_run, "ok|zero|count|first|sum")
XB_GROUP(colorcorrect_handles_without_icm, crows, cc_run, "null|junk|stock|brush")
XB_GROUP(colorcorrect_icm_valid, crows, cc_run, "icm_ok|icm_memdc")
XB_GROUP(colorcorrect_icm_ranges, crows, cc_run, "icm_zero|icm_count|icm_first|icm_sum")
XB_GROUP(colorcorrect_icm_handles, crows, cc_run, "icm_null|icm_junk|icm_stock|icm_brush")
/* The Nt stub rejects a missing DC, a missing palette, a zero count and counts of 0x40000000 and above with
 * ERROR_INVALID_PARAMETER, exactly as the gdi32 export does. */
XT(colorcorrect_nt_stub_bad_arguments_87) {
    REQUIRE(UN(NtGdiColorCorrectPalette));
    HDC dc = U(GetDC)(NULL); HPALETTE pal = pal4(); REQUIRE(dc && pal); PALETTEENTRY buf[8];
    struct { HDC dc; HPALETTE p; ULONG first, n; const char *why; } c[] = {
        { NULL, pal, 0, 2, "null dc" }, { dc, NULL, 0, 2, "null palette" }, { dc, pal, 0, 0, "zero entries" },
        { dc, pal, 0, 0x40000000, "count 0x40000000" }, { dc, pal, 0, 0xFFFFFFFF, "count max" },
    };
    for (unsigned i = 0; i < sizeof c / sizeof *c; i++) {
        SetLastError(0);
        ULONG r = UN(NtGdiColorCorrectPalette)(c[i].dc, c[i].p, c[i].first, c[i].n, buf, 0);
        CHECKF(r == 0, "%s: returned %lu", c[i].why, (unsigned long)r);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
    U(DeleteObject)(pal); U(ReleaseDC)(NULL, dc);
}
XT(colorcorrect_without_icm_reports_icm_not_enabled) {
    REQUIRE(U(ColorCorrectPalette)); HDC dc = U(GetDC)(NULL); HPALETTE pal = pal4(); REQUIRE(dc && pal);
    CHECK(U(SetICMMode)(dc, ICM_QUERY) == ICM_OFF);
    SetLastError(0); CHECK(!U(ColorCorrectPalette)(dc, pal, 0, 4)); EXPECT_ERR(2018);
    U(DeleteObject)(pal); U(ReleaseDC)(NULL, dc);
}
XT(colorcorrect_icm_leaves_entries_without_profile) {
    REQUIRE(U(ColorCorrectPalette)); HDC dc = U(GetDC)(NULL); HPALETTE pal = pal4(); REQUIRE(dc && pal);
    PALETTEENTRY b[4], a[4]; U(GetPaletteEntries)(pal, 0, 4, b);
    CHECK(U(SetICMMode)(dc, ICM_ON));
    SetLastError(0); BOOL r = U(ColorCorrectPalette)(dc, pal, 0, 4);
    CHECK(r); EXPECT_ERR(0);
    U(GetPaletteEntries)(pal, 0, 4, a);
    CHECKF(!memcmp(a, b, sizeof a), "entries changed although no colour profile is associated with the DC");
    U(SetICMMode)(dc, ICM_OFF); U(DeleteObject)(pal); U(ReleaseDC)(NULL, dc);
}
