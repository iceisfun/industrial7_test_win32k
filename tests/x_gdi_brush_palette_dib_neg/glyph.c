/* NtGdiGetGlyphIndicesWInternal: parameter matrix (DC handle types, character count, flag bits, NULL and inaccessible
 * string / output buffers) and consistency with the public GetGlyphIndicesW/A and the character-width APIs. Returns the
 * number of characters converted, or GDI_ERROR. With flag 1 (GGI_MARK_NONEXISTING_GLYPHS) a character the font lacks
 * maps to 0xFFFF; flag bits above bit 0 are ignored. */
#include "xb.h"

#define GE 0xFFFFFFFFu
struct gp { int dk; WCHAR s[320]; int cwc; DWORD fl, fl2; int nullpwc, nullpgi, junkpwc, junkpgi, gpgi, gpwc; };
static void gp_base(struct gp *p) { memset(p, 0, sizeof *p); p->dk = -1; p->s[0] = 'A'; p->s[1] = 'B'; p->s[2] = ' '; p->s[3] = 'C'; p->cwc = 4; }

/* name, expected return, expected error, mutation */
#define GROWS(X) \
 X(ok4,         4u,0, ;) \
 X(ok1,         1u,0, p->cwc = 1;) \
 X(zero_count,  4294967295u,0, p->cwc = 0;) \
 X(zero_count_null_out, 4294967295u,0, p->cwc = 0; p->nullpgi = 1;) \
 X(zero_count_null_in, 4294967295u,0, p->cwc = 0; p->nullpwc = 1;) \
 X(neg1,        4294967295u,0, p->cwc = -1;) \
 X(neg_min,     4294967295u,0, p->cwc = (int)0x80000000;) \
 X(huge,        4294967295u,0, p->cwc = 0x7fffffff;) \
 X(count_65536, 4294967295u,0, p->cwc = 65536;) \
 X(count_320,   320u,0, for (int i = 0; i < 320; i++) p->s[i] = 'a' + i % 26; p->cwc = 320;) \
 X(count_16,    16u,0, for (int i = 0; i < 16; i++) p->s[i] = 'a' + i; p->cwc = 16;) \
 X(count_17,    17u,0, for (int i = 0; i < 17; i++) p->s[i] = 'a' + i; p->cwc = 17;) \
 X(count_18,    18u,0, for (int i = 0; i < 18; i++) p->s[i] = 'a' + i; p->cwc = 18;) \
 X(flag1,       4u,0, p->fl = 1;) \
 X(flag2,       4u,0, p->fl = 2;) \
 X(flag3,       4u,0, p->fl = 3;) \
 X(flag_hi,     4u,0, p->fl = 0x80000000;) \
 X(flag_all,    4u,0, p->fl = 0xFFFFFFFF;) \
 X(fl2_1,       4u,0, p->fl2 = 1;) \
 X(fl2_2,       4u,0, p->fl2 = 2;) \
 X(fl2_all,     4u,0, p->fl2 = 0xFFFFFFFF;) \
 X(null_in,     4294967295u,0, p->nullpwc = 1;) \
 X(null_out,    4294967295u,0, p->nullpgi = 1;) \
 X(junk_in,     4294967295u,0, p->junkpwc = 1;) \
 X(junk_out,    4294967295u,0, p->junkpgi = 1;) \
 X(guard_out_exact, 4u,0, p->gpgi = 4;) \
 X(guard_out_short, 4294967295u,0, p->gpgi = 3;) \
 X(guard_out_one,   1u,0, p->gpgi = 1; p->cwc = 1;) \
 X(guard_in_exact, 4u,0, p->gpwc = 4;) \
 X(guard_in_short, 4294967295u,0, p->gpwc = 3;) \
 X(guard_in_claims_long, 4294967295u,0, p->gpwc = 4; p->cwc = 40;) \
 X(null_dc,     4294967295u,0, p->dk = XH_NULL;) \
 X(junk_dc,     4294967295u,0, p->dk = XH_JUNK;) \
 X(minus1_dc,   4294967295u,0, p->dk = XH_MINUS1;) \
 X(bitmap_dc,   4294967295u,0, p->dk = XH_BITMAP;) \
 X(pen_dc,      4294967295u,0, p->dk = XH_PEN;) \
 X(brush_dc,    4294967295u,0, p->dk = XH_BRUSH;) \
 X(region_dc,   4294967295u,0, p->dk = XH_REGION;) \
 X(palette_dc,  4294967295u,0, p->dk = XH_PALETTE;) \
 X(stockpen_dc, 4294967295u,0, p->dk = XH_STOCKPEN;) \
 X(hwnd_dc,     4294967295u,0, p->dk = XH_HWND;) \
 X(font_dc,     4294967295u,0, p->dk = XH_FONT;) \
 X(unmapped_flag0, 4u,0, p->s[1] = 0x0378;) \
 X(unmapped_flag1, 4u,0, p->s[1] = 0x0378; p->fl = 1;) \
 X(ffff_flag1,  4u,0, p->s[1] = 0xFFFF; p->fl = 1;) \
 X(nul_char,    4u,0, p->s[1] = 0;) \
 X(lone_surrogate, 4u,0, p->s[1] = 0xD800;) \
 X(surrogate_pair, 4u,0, p->s[1] = 0xD83D; p->s[2] = 0xDE00;)

static void gl_run(const char *nm, void (*mut)(struct gp *), unsigned long long expv, int experr) {
    REQUIRE(UN(NtGdiGetGlyphIndicesWInternal));
    XHS(x);
    struct gp gp; gp_base(&gp); mut(&gp);
    HDC mdc = U(CreateCompatibleDC)(NULL); HFONT ft = U(CreateFontW)(-16, 0, 0, 0, 400, 0, 0, 0, ANSI_CHARSET, 0, 0, 0, 0, L"Arial");
    HGDIOBJ oldf = U(SelectObject)(mdc, ft);
    HDC dc = gp.dk < 0 ? mdc : (HDC)x.h[gp.dk];
    struct xb_guard gi = {0}, gw = {0}; WORD out[400]; memset(out, 0xCC, sizeof out);
    WORD *pgi = out; const WCHAR *pwc = gp.s;
    if (gp.gpgi > 0) { if (!xb_guard_open(&gi, gp.gpgi * 2)) { t_skip("guard"); goto done; } pgi = (WORD *)gi.p; }
    if (gp.gpwc) { if (!xb_guard_open(&gw, gp.gpwc * 2)) { t_skip("guard"); goto done; } memcpy(gw.p, gp.s, gp.gpwc * 2); pwc = (WCHAR *)gw.p; }
    if (gp.nullpwc) pwc = NULL;
    if (gp.nullpgi) pgi = NULL;
    if (gp.junkpwc) pwc = (const WCHAR *)(ULONG_PTR)0x1000;
    if (gp.junkpgi) pgi = (WORD *)(ULONG_PTR)0x1000;
    SetLastError(0);
    int r = UN(NtGdiGetGlyphIndicesWInternal)(dc, pwc, gp.cwc, pgi, gp.fl, gp.fl2);
    DWORD e = GetLastError();
    xb_val(nm, (unsigned)r, e, expv, experr);
done:
    xb_guard_close(&gi); xb_guard_close(&gw);
    U(SelectObject)(mdc, oldf); U(DeleteObject)(ft); U(DeleteDC)(mdc); xhs_close(&x);
}
#define GM(nm, e, ee, ...) static void gm_##nm(struct gp *p) { __VA_ARGS__ }
GROWS(GM)
struct grow { const char *nm; void (*mut)(struct gp *); unsigned long long ev; int ee; };
#define GS(nm, e, ee, ...) { #nm, gm_##nm, e, ee },
static const struct grow growsT[] = { GROWS(GS) };
static void gl_row(const struct grow *g) { gl_run(g->nm, g->mut, g->ev, g->ee); }
XB_GROUP(glyph_count_matrix, growsT, gl_row, "ok|zero_count|neg|huge|count")
XB_GROUP(glyph_flag_bits, growsT, gl_row, "flag|fl2")
XB_GROUP(glyph_buffer_pointers, growsT, gl_row, "null_in|null_out|junk_in|junk_out|guard")
XB_GROUP(glyph_dc_handle_types, growsT, gl_row, "*_dc")
XB_GROUP(glyph_unusual_characters, growsT, gl_row, "unmapped|ffff|nul_char|lone|surrogate")

/* ---- consistency ------------------------------------------------------------------------------------------------ */
struct gf { HDC dc; HFONT f; HGDIOBJ old; };
static int gf_open(struct gf *g, const WCHAR *face, int h) {
    g->dc = U(CreateCompatibleDC)(NULL); g->f = U(CreateFontW)(h, 0, 0, 0, 400, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, face);
    if (!g->dc || !g->f) return 0;
    g->old = U(SelectObject)(g->dc, g->f);
    return 1;
}
static void gf_close(struct gf *g) { if (g->dc && g->old) U(SelectObject)(g->dc, g->old); if (g->f) U(DeleteObject)(g->f); if (g->dc) U(DeleteDC)(g->dc); }
#define GF(g) struct gf g; if (!gf_open(&g, L"Arial", -16)) { gf_close(&g); t_skip("font"); return; } REQUIRE(UN(NtGdiGetGlyphIndicesWInternal))

XT(glyph_nt_matches_public) {
    GF(g); WCHAR s[] = L"Glyph indices 123 xyz"; int n = lstrlenW(s); WORD a[32], b[32];
    CHECK(UN(NtGdiGetGlyphIndicesWInternal)(g.dc, s, n, a, 0, 0) == (DWORD)n);
    CHECK(U(GetGlyphIndicesW)(g.dc, s, n, b, 0) == (DWORD)n);
    CHECK(!memcmp(a, b, n * 2));
    gf_close(&g);
}
XT(glyph_ansi_matches_wide) {
    GF(g); WORD a[16], w[16];
    CHECK(U(GetGlyphIndicesA)(g.dc, "Hello World", 11, a, 0) == 11);
    CHECK(UN(NtGdiGetGlyphIndicesWInternal)(g.dc, L"Hello World", 11, w, 0, 0) == 11);
    CHECK(!memcmp(a, w, 22));
    gf_close(&g);
}
XT(glyph_same_char_same_glyph) {
    GF(g); WORD a[6]; WCHAR s[] = L"aXaXaX";
    CHECK(UN(NtGdiGetGlyphIndicesWInternal)(g.dc, s, 6, a, 0, 0) == 6);
    CHECK(a[0] == a[2] && a[2] == a[4] && a[1] == a[3] && a[3] == a[5] && a[0] != a[1]);
    gf_close(&g);
}
XT(glyph_distinct_letters_distinct_glyphs) {
    GF(g); WCHAR s[26]; WORD a[26]; for (int i = 0; i < 26; i++) s[i] = 'a' + i;
    CHECK(UN(NtGdiGetGlyphIndicesWInternal)(g.dc, s, 26, a, 0, 0) == 26);
    int dup = 0; for (int i = 0; i < 26; i++) for (int j = 0; j < i; j++) if (a[i] == a[j]) dup++;
    CHECKF(dup == 0, "%d duplicate glyph indices among the 26 lower-case letters", dup);
    gf_close(&g);
}
XT(glyph_mark_flag_marks_only_missing) {
    GF(g); WCHAR s[3] = { 'A', 0x0378, 'B' }; WORD m[3], u[3];
    CHECK(UN(NtGdiGetGlyphIndicesWInternal)(g.dc, s, 3, m, 1, 0) == 3);
    CHECK(UN(NtGdiGetGlyphIndicesWInternal)(g.dc, s, 3, u, 0, 0) == 3);
    CHECK(m[1] == 0xFFFF && m[0] != 0xFFFF && m[2] != 0xFFFF);
    CHECK(u[1] != 0xFFFF && u[0] == m[0] && u[2] == m[2]);
    gf_close(&g);
}
XT(glyph_flags_above_bit0_ignored) {
    GF(g); WCHAR s[2] = { 0x0378, 'A' }; WORD a[2], b[2], c[2], d[2];
    CHECK(UN(NtGdiGetGlyphIndicesWInternal)(g.dc, s, 2, a, 0, 0) == 2);
    CHECK(UN(NtGdiGetGlyphIndicesWInternal)(g.dc, s, 2, b, 2, 0) == 2);
    CHECK(UN(NtGdiGetGlyphIndicesWInternal)(g.dc, s, 2, c, 0xFFFFFFFE, 0) == 2);
    CHECK(UN(NtGdiGetGlyphIndicesWInternal)(g.dc, s, 2, d, 0x100, 0) == 2);
    CHECK(!memcmp(a, b, 4) && !memcmp(a, c, 4) && !memcmp(a, d, 4));
    gf_close(&g);
}
XT(glyph_widths_agree_with_char_widths) {
    GF(g); REQUIRE(U(GetCharWidthI)); REQUIRE(U(GetCharWidth32W));
    WCHAR s[] = L"AaWi. m"; int n = 7; WORD gi[7]; int wi[7], wc[7];
    CHECK(UN(NtGdiGetGlyphIndicesWInternal)(g.dc, s, n, gi, 0, 0) == (DWORD)n);
    int bad = 0;
    for (int i = 0; i < n; i++) {
        if (!U(GetCharWidthI)(g.dc, gi[i], 1, NULL, &wi[i])) bad++;
        if (!U(GetCharWidth32W)(g.dc, s[i], s[i], &wc[i])) bad++;
        else if (wi[i] != wc[i]) bad++;
    }
    CHECKF(bad == 0, "%d width disagreements between glyph-index and character width queries", bad);
    gf_close(&g);
}
XT(glyph_different_fonts_same_call_succeeds) {
    REQUIRE(UN(NtGdiGetGlyphIndicesWInternal));
    const WCHAR *faces[] = { L"Arial", L"Courier New", L"Times New Roman", L"Segoe UI" };
    for (int i = 0; i < 4; i++) {
        struct gf g; if (!gf_open(&g, faces[i], -14)) { gf_close(&g); continue; }
        WORD a[3]; CHECK(UN(NtGdiGetGlyphIndicesWInternal)(g.dc, L"A b", 3, a, 0, 0) == 3);
        CHECK(a[0] != a[1] || a[0] != a[2]);
        gf_close(&g);
    }
}
XT(glyph_default_font_dc) {
    REQUIRE(UN(NtGdiGetGlyphIndicesWInternal)); HDC dc = U(CreateCompatibleDC)(NULL); REQUIRE(dc); WORD a[3];
    CHECK(UN(NtGdiGetGlyphIndicesWInternal)(dc, L"A b", 3, a, 0, 0) == 3);
    U(DeleteDC)(dc);
}
XT(glyph_info_dc) {
    REQUIRE(UN(NtGdiGetGlyphIndicesWInternal)); HDC dc = U(CreateICW)(L"DISPLAY", NULL, NULL, NULL); REQUIRE(dc); WORD a[3];
    SetLastError(0); DWORD r = UN(NtGdiGetGlyphIndicesWInternal)(dc, L"A b", 3, a, 0, 0);
    CHECK(r == 3); EXPECT_ERR(0);
    U(DeleteDC)(dc);
}
XT(glyph_deleted_dc) {
    REQUIRE(UN(NtGdiGetGlyphIndicesWInternal)); HDC dc = U(CreateCompatibleDC)(NULL); REQUIRE(dc); WORD a[3];
    U(DeleteDC)(dc);
    CHECK(UN(NtGdiGetGlyphIndicesWInternal)(dc, L"A b", 3, a, 0, 0) == GE);
}
XT(glyph_failed_call_leaves_output_untouched) {
    GF(g); WORD a[4] = { 0x1111, 0x2222, 0x3333, 0x4444 };
    CHECK(UN(NtGdiGetGlyphIndicesWInternal)(g.dc, L"ABCD", -1, a, 0, 0) == GE);
    CHECK(a[0] == 0x1111 && a[1] == 0x2222 && a[2] == 0x3333 && a[3] == 0x4444);
    gf_close(&g);
}
XT(glyph_repeatable) {
    GF(g); WORD a[4], b[4]; int bad = 0;
    UN(NtGdiGetGlyphIndicesWInternal)(g.dc, L"Test", 4, a, 0, 0);
    for (int i = 0; i < 50; i++) { UN(NtGdiGetGlyphIndicesWInternal)(g.dc, L"Test", 4, b, 0, 0); if (memcmp(a, b, 8)) bad++; }
    CHECKF(bad == 0, "%d repeats differed", bad);
    gf_close(&g);
}
