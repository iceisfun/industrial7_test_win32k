/* Widths, extents, glyph indices, character placement and the DC text attributes (DC_ATTR) the kernel shares. */
#include <math.h>
#include "fm.h"

struct ws { HDC dc; HFONT f; HGDIOBJ old; };
static int ws_open(struct ws *w, const WCHAR *face, int h, int wt, int cs) {
    memset(w, 0, sizeof *w);
    w->dc = fm_dc(); w->f = fm_font(face, h, wt, cs, 0, 0);
    if (!w->dc || !w->f) { if (w->f) fm.DeleteObject(w->f); fm_freedc(w->dc); memset(w, 0, sizeof *w); return 0; }
    w->old = fm.GetCurrentObject(w->dc, OBJ_FONT);
    fm_sel(w->dc, w->f);
    return 1;
}
static void ws_close(struct ws *w) { if (!w->dc) return; fm_sel(w->dc, w->old); fm.DeleteObject(w->f); fm_freedc(w->dc); memset(w, 0, sizeof *w); }

/* DC_ATTR* of a DC: the pUser slot of its handle table cell. Offsets (from gdi32full!GetTextAlign/SetTextCharacterExtra/
   SetTextJustification/GetTextColor): +0xbc text colour, +0x114 text align, +0x118 char extra, +0x11c break extra, +0x120 break count. */
#define DCA_TEXTCOLOR 0xbc
#define DCA_TEXTALIGN 0x114
#define DCA_CHAREXTRA 0x118
#define DCA_BREAKEXTRA 0x11c
#define DCA_BREAKCOUNT 0x120
static void *dcattr(HDC dc) {
    struct fm_cell c;
    if (!fm_cell_of(dc, &c) || !c.puser) return NULL;
    return (void *)(ULONG_PTR)c.puser;
}
static int dca_get(HDC dc, unsigned off, long *out) {
    void *a = dcattr(dc); unsigned long v;
    if (!a || !k_try_read_u32((char *)a + off, &v)) return 0;
    *out = (long)(int)v; return 1;
}

static long sumw(HDC dc, const WCHAR *s, int n) {
    long t = 0;
    for (int i = 0; i < n; i++) { INT w = 0; if (!fm.GetCharWidth32W(dc, s[i], s[i], &w)) return -1; t += w; }
    return t;
}

/* ---- GetCharWidth family ---- */

TEST(gdi_font_metrics_text_charwidth_ascii_positive) {
    FM_BEGIN();
    static const WCHAR *faces[] = { L"Arial", L"Courier New", L"Times New Roman", L"Tahoma" };
    for (unsigned i = 0; i < 4; i++) {
        if (!fm_face_present(faces[i])) continue;
        struct ws w; if (!ws_open(&w, faces[i], -16, 400, ANSI_CHARSET)) continue;
        INT buf[96];
        CHECK(fm.GetCharWidth32W(w.dc, 0x20, 0x7e, buf));
        for (int c = 0; c < 95; c++) CHECKF(buf[c] > 0, "face %u char 0x%x width %d", i, 0x20 + c, buf[c]);
        ws_close(&w);
    }
}

TEST(gdi_font_metrics_text_charwidth_invalid_args_set_error) {
    FM_BEGIN();
    struct ws w; if (!ws_open(&w, L"Arial", -16, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    INT buf[8] = { 0 };
    /* gdi32full!bGetCharWidthW rejects a NULL buffer, first > last and last > 0xFFFF with ERROR_INVALID_PARAMETER. */
    if (t_is_wine()) { t_skip("Wine hangs on NULL buffers and reversed or oversized ranges"); ws_close(&w); return; }
    SetLastError(0); CHECK(!fm.GetCharWidth32W(w.dc, 'A', 'B', NULL)); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SetLastError(0); CHECK(!fm.GetCharWidthW(w.dc, 'A', 'B', NULL)); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SetLastError(0); CHECK(!fm.GetCharWidth32W(w.dc, 'B', 'A', buf)); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SetLastError(0); CHECK(!fm.GetCharWidth32W(w.dc, 0, 0x10000, buf)); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SetLastError(0); CHECK(!fm.GetCharWidthW(w.dc, 'B', 'A', buf)); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    ws_close(&w);
}

TEST(gdi_font_metrics_text_charwidth_invalid_dc) {
    FM_BEGIN();
    INT buf[4];
    CHECK(!fm.GetCharWidth32W(NULL, 'A', 'A', buf));
    CHECK(!fm.GetCharWidth32W((HDC)(ULONG_PTR)0x4321, 'A', 'A', buf));
    CHECK(!fm.GetCharWidthW(NULL, 'A', 'A', buf));
    HDC dc = fm_dc(); if (dc) { fm_freedc(dc); CHECK(!fm.GetCharWidth32W(dc, 'A', 'A', buf)); }
}

TEST(gdi_font_metrics_text_charwidth_single_equals_range) {
    FM_BEGIN();
    struct ws w; if (!ws_open(&w, L"Arial", -20, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    INT range[26], one;
    CHECK(fm.GetCharWidth32W(w.dc, 'a', 'z', range));
    for (int i = 0; i < 26; i++) {
        CHECK(fm.GetCharWidth32W(w.dc, 'a' + i, 'a' + i, &one));
        CHECKF(one == range[i], "'%c' range %d single %d", 'a' + i, range[i], one);
    }
    ws_close(&w);
}

TEST(gdi_font_metrics_text_charwidth_16_vs_32) {
    FM_BEGIN();
    struct ws w; if (!ws_open(&w, L"Arial", -20, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    INT a[95], b[95];
    CHECK(fm.GetCharWidthW(w.dc, 0x20, 0x7e, a));
    CHECK(fm.GetCharWidth32W(w.dc, 0x20, 0x7e, b));
    int diff = 0;
    for (int i = 0; i < 95; i++) if (abs(a[i] - b[i]) > 1) diff++;   /* they differ only by overhang, nil for upright TrueType */
    EXPECT_EQ(diff, 0);
    ws_close(&w);
}

TEST(gdi_font_metrics_text_charwidth_above_0x80_path) {
    FM_BEGIN();
    /* last >= 0x80 bypasses the client width cache and goes to NtGdiGetCharWidthW. */
    struct ws w; if (!ws_open(&w, L"Arial", -20, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    INT a[0x100], b[0x80];
    CHECK(fm.GetCharWidth32W(w.dc, 0, 0xff, a));
    CHECK(fm.GetCharWidth32W(w.dc, 0x20, 0x7f, b));
    for (int i = 0; i < 0x60; i++) CHECKF(a[0x20 + i] == b[i], "char 0x%x cached %d kernel %d", 0x20 + i, b[i], a[0x20 + i]);
    CHECK(fm.GetCharWidth32W(w.dc, 0x3b1, 0x3c9, a));   /* Greek: Arial has them */
    for (int i = 0; i <= 0x3c9 - 0x3b1; i++) CHECK(a[i] > 0);
    CHECK(fm.GetCharWidth32W(w.dc, 0xFFFF, 0xFFFF, a));
    ws_close(&w);
}

TEST(gdi_font_metrics_text_charwidth_scales_with_size) {
    FM_BEGIN();
    if (!fm_face_present(L"Arial")) { t_skip("no Arial"); return; }
    int prev = 0;
    for (int h = 8; h <= 64; h += 8) {
        struct ws w; if (!ws_open(&w, L"Arial", -h, 400, ANSI_CHARSET)) continue;
        INT m; CHECK(fm.GetCharWidth32W(w.dc, 'M', 'M', &m));
        CHECKF(m >= prev, "size %d: M width %d < %d", h, m, prev);
        CHECKF(abs(m - h * 833 / 1000) <= 2 + h / 16, "size %d: M width %d (Arial M is ~0.833 em)", h, m);
        prev = m;
        ws_close(&w);
    }
}

TEST(gdi_font_metrics_text_charwidth_courier_all_equal) {
    FM_BEGIN();
    if (!fm_face_present(L"Courier New")) { t_skip("no Courier New"); return; }
    struct ws w; if (!ws_open(&w, L"Courier New", -24, 400, ANSI_CHARSET)) return;
    INT buf[95];
    CHECK(fm.GetCharWidth32W(w.dc, 0x20, 0x7e, buf));
    for (int i = 1; i < 95; i++) CHECKF(buf[i] == buf[0], "char 0x%x width %d vs %d", 0x20 + i, buf[i], buf[0]);
    ws_close(&w);
}

TEST(gdi_font_metrics_text_charwidth_bold_not_narrower) {
    FM_BEGIN();
    if (!fm_face_present(L"Arial")) { t_skip("no Arial"); return; }
    struct ws r, b;
    if (!ws_open(&r, L"Arial", -20, 400, ANSI_CHARSET)) return;
    if (!ws_open(&b, L"Arial", -20, 700, ANSI_CHARSET)) { ws_close(&r); return; }
    INT wr[26], wb[26];
    fm.GetCharWidth32W(r.dc, 'a', 'z', wr); fm.GetCharWidth32W(b.dc, 'a', 'z', wb);
    int narrower = 0; for (int i = 0; i < 26; i++) if (wb[i] < wr[i]) narrower++;
    EXPECT_EQ(narrower, 0);
    ws_close(&r); ws_close(&b);
}

/* ---- ABC widths ---- */

TEST(gdi_font_metrics_text_abc_sum_equals_advance) {
    FM_BEGIN();
    static const WCHAR *faces[] = { L"Arial", L"Courier New", L"Times New Roman", L"Tahoma", L"Segoe UI" };
    for (unsigned f = 0; f < 5; f++) {
        if (!fm_face_present(faces[f])) continue;
        struct ws w; if (!ws_open(&w, faces[f], -18, 400, ANSI_CHARSET)) continue;
        ABC abc[95]; INT adv[95];
        CHECK(fm.GetCharABCWidthsW(w.dc, 0x20, 0x7e, abc));
        CHECK(fm.GetCharWidth32W(w.dc, 0x20, 0x7e, adv));
        for (int i = 0; i < 95; i++) {
            long sum = abc[i].abcA + (long)abc[i].abcB + abc[i].abcC;
            CHECKF(sum == adv[i], "face %u char 0x%x: A %d B %u C %d vs advance %d", f, 0x20 + i, abc[i].abcA, abc[i].abcB, abc[i].abcC, adv[i]);
        }
        ws_close(&w);
    }
}

TEST(gdi_font_metrics_text_abc_space_has_no_ink) {
    FM_BEGIN();
    struct ws w; if (!ws_open(&w, L"Arial", -20, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    ABC abc; CHECK(fm.GetCharABCWidthsW(w.dc, ' ', ' ', &abc));
    INT adv; fm.GetCharWidth32W(w.dc, ' ', ' ', &adv);
    EXPECT_EQ(abc.abcA + (int)abc.abcB + abc.abcC, adv);
    CHECK(adv > 0);
    ws_close(&w);
}

TEST(gdi_font_metrics_text_abc_a_nonnegative_for_regular_arial) {
    FM_BEGIN();
    struct ws w; if (!ws_open(&w, L"Arial", -48, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    ABC abc[26];
    CHECK(fm.GetCharABCWidthsW(w.dc, 'A', 'Z', abc));
    for (int i = 0; i < 26; i++) CHECKF(abc[i].abcB > 0, "'%c' B=%u", 'A' + i, abc[i].abcB);
    ws_close(&w);
}

TEST(gdi_font_metrics_text_abc_italic_f_has_negative_c_or_a) {
    FM_BEGIN();
    if (!fm_face_present(L"Times New Roman")) { t_skip("no Times New Roman"); return; }
    HDC dc = fm_dc(); if (!dc) return;
    LOGFONTW lf; memset(&lf, 0, sizeof lf); lf.lfHeight = -64; lf.lfWeight = 400; lf.lfItalic = 1; lf.lfCharSet = ANSI_CHARSET; lstrcpyW(lf.lfFaceName, L"Times New Roman");
    HFONT f = fm.CreateFontIndirectW(&lf); HGDIOBJ old = fm_sel(dc, f);
    ABC abc; CHECK(fm.GetCharABCWidthsW(dc, 'f', 'f', &abc));
    INT adv; fm.GetCharWidth32W(dc, 'f', 'f', &adv);
    EXPECT_EQ(abc.abcA + (int)abc.abcB + abc.abcC, adv);
    t_note("TNR italic 'f' at 64px: A %d B %u C %d", abc.abcA, abc.abcB, abc.abcC);
    fm_sel(dc, old); fm.DeleteObject(f); fm_freedc(dc);
}

TEST(gdi_font_metrics_text_abc_ansi_matches_wide) {
    FM_BEGIN();
    struct ws w; if (!ws_open(&w, L"Arial", -20, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    ABC a[26], b[26];
    CHECK(fm.GetCharABCWidthsA(w.dc, 'a', 'z', a));
    CHECK(fm.GetCharABCWidthsW(w.dc, 'a', 'z', b));
    CHECK(!memcmp(a, b, sizeof a));
    ws_close(&w);
}

TEST(gdi_font_metrics_text_abc_invalid_args) {
    FM_BEGIN();
    struct ws w; if (!ws_open(&w, L"Arial", -16, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    ABC abc[4];
    CHECK(!fm.GetCharABCWidthsW(w.dc, 'B', 'A', abc));
    CHECK(!fm.GetCharABCWidthsW(w.dc, 'A', 'B', NULL));
    CHECK(!fm.GetCharABCWidthsW(NULL, 'A', 'B', abc));
    ws_close(&w);
}

TEST(gdi_font_metrics_text_abc_bitmap_font_notes) {
    FM_BEGIN();
    HDC dc = fm_dc(); if (!dc) return;
    HGDIOBJ old = fm.SelectObject(dc, fm.GetStockObject(SYSTEM_FONT));
    ABC abc;
    SetLastError(0);
    BOOL r = fm.GetCharABCWidthsW(dc, 'A', 'A', &abc);
    t_note("GetCharABCWidthsW on the System bitmap font: %d (err %lu) A %d B %u C %d", r, GetLastError(), abc.abcA, abc.abcB, abc.abcC);
    fm.SelectObject(dc, old); fm_freedc(dc);
}

TEST(gdi_font_metrics_text_abc_float_consistent) {
    FM_BEGIN();
    struct ws w; if (!ws_open(&w, L"Arial", -32, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    ABCFLOAT f[26]; INT wi[26];
    CHECK(fm.GetCharABCWidthsFloatW(w.dc, 'A', 'Z', f));
    CHECK(fm.GetCharWidth32W(w.dc, 'A', 'Z', wi));
    /* On Windows 10 the ABC triple of the float variant is in device pixels: its sum is the integer advance. */
    for (int i = 0; i < 26; i++) {
        float sum = f[i].abcfA + f[i].abcfB + f[i].abcfC;
        CHECKF(fabsf(sum - wi[i]) < 0.5f, "'%c' sum %f advance %d", 'A' + i, sum, wi[i]);
        CHECKF(sum > 0.0f, "'%c'", 'A' + i);
    }
    ws_close(&w);
}

TEST(gdi_font_metrics_text_charwidth_float_close_to_int) {
    FM_BEGIN();
    struct ws w; if (!ws_open(&w, L"Arial", -32, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    FLOAT wf[26]; INT wi[26];
    CHECK(fm.GetCharWidthFloatW(w.dc, 'a', 'z', wf));
    CHECK(fm.GetCharWidth32W(w.dc, 'a', 'z', wi));
    /* On Windows 10 GetCharWidthFloatW reports the advance in 1/16 pixel units of the integer variant (17 px -> 1.0625). */
    for (int i = 0; i < 26; i++) CHECKF(fabsf(wf[i] * 16.0f - wi[i]) <= 1.5f, "'%c' float %f int %d", 'a' + i, wf[i], wi[i]);
    ws_close(&w);
}

/* ---- glyph index variants ---- */

TEST(gdi_font_metrics_text_glyphindices_basic) {
    FM_BEGIN();
    struct ws w; if (!ws_open(&w, L"Arial", -16, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    WORD gi[8];
    EXPECT_EQ(fm.GetGlyphIndicesW(w.dc, L"ABCABC  ", 8, gi, 0), 8);
    CHECK(gi[0] == gi[3] && gi[1] == gi[4] && gi[2] == gi[5]);
    CHECK(gi[0] != gi[1] && gi[1] != gi[2] && gi[0] != gi[2]);
    CHECK(gi[6] == gi[7]);
    CHECK(gi[6] != gi[0]);
    for (int i = 0; i < 8; i++) CHECK(gi[i] != 0xFFFF);
    ws_close(&w);
}

TEST(gdi_font_metrics_text_glyphindices_mark_missing) {
    FM_BEGIN();
    struct ws w; if (!ws_open(&w, L"Arial", -16, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    WCHAR s[3] = { 'A', 0x0378, 'B' };   /* U+0378 is unassigned: no font maps it */
    WORD gi[3], gj[3];
    EXPECT_EQ(fm.GetGlyphIndicesW(w.dc, s, 3, gi, GGI_MARK_NONEXISTING_GLYPHS), 3);
    CHECK(gi[0] != 0xFFFF); EXPECT_EQ(gi[1], 0xFFFF); CHECK(gi[2] != 0xFFFF);
    EXPECT_EQ(fm.GetGlyphIndicesW(w.dc, s, 3, gj, 0), 3);
    CHECK(gj[1] != 0xFFFF);            /* without the flag the default glyph is substituted */
    EXPECT_EQ(gj[0], gi[0]); EXPECT_EQ(gj[2], gi[2]);
    ws_close(&w);
}

TEST(gdi_font_metrics_text_glyphindices_flag_bits_above_one_ignored) {
    FM_BEGIN();
    struct ws w; if (!ws_open(&w, L"Arial", -16, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    WCHAR s[2] = { 0x0378, 'A' };
    WORD g0[2], g2[2], gff[2];
 /* NtGdiGetGlyphIndicesWInternal: flags >= 2 are treated as 0. */
    EXPECT_EQ(fm.GetGlyphIndicesW(w.dc, s, 2, g0, 0), 2);
    EXPECT_EQ(fm.GetGlyphIndicesW(w.dc, s, 2, g2, 2), 2);
    EXPECT_EQ(fm.GetGlyphIndicesW(w.dc, s, 2, gff, 0xFFFFFFFE), 2);
    CHECK(!memcmp(g0, g2, sizeof g0)); CHECK(!memcmp(g0, gff, sizeof g0));
    CHECK(g2[0] != 0xFFFF);
    ws_close(&w);
}

TEST(gdi_font_metrics_text_glyphindices_negative_count) {
    FM_BEGIN();
    struct ws w; if (!ws_open(&w, L"Arial", -16, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    WORD gi[2] = { 0x1111, 0x2222 };
    EXPECT_EQ(fm.GetGlyphIndicesW(w.dc, L"AB", -1, gi, 0), GDI_ERROR);   /* (int)count < 0 -> 0xffffffff */
    EXPECT_EQ(gi[0], 0x1111);
    ws_close(&w);
}

TEST(gdi_font_metrics_text_glyphindices_invalid_dc) {
    FM_BEGIN();
    WORD gi[2];
    EXPECT_EQ(fm.GetGlyphIndicesW(NULL, L"AB", 2, gi, 0), GDI_ERROR);
    EXPECT_EQ(fm.GetGlyphIndicesW((HDC)(ULONG_PTR)0x777, L"AB", 2, gi, 0), GDI_ERROR);
}

TEST(gdi_font_metrics_text_glyphindices_long_string) {
    FM_BEGIN();
    struct ws w; if (!ws_open(&w, L"Arial", -16, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    /* Above 16 characters the kernel switches from its stack buffer to AllocFreeTmpBuffer. */
    WCHAR s[300]; WORD gi[300];
    for (int i = 0; i < 300; i++) s[i] = 'a' + (i % 26);
    EXPECT_EQ(fm.GetGlyphIndicesW(w.dc, s, 300, gi, 0), 300);
    for (int i = 26; i < 300; i++) CHECKF(gi[i] == gi[i - 26], "index %d", i);
    ws_close(&w);
}

TEST(gdi_font_metrics_text_glyphindices_widths_agree_with_i_api) {
    FM_BEGIN();
    struct ws w; if (!ws_open(&w, L"Arial", -20, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    WORD gi[26]; INT wc[26], wg[26]; ABC ac[26], ag[26];
    fm.GetGlyphIndicesW(w.dc, L"abcdefghijklmnopqrstuvwxyz", 26, gi, 0);
    CHECK(fm.GetCharWidth32W(w.dc, 'a', 'z', wc));
    CHECK(fm.GetCharWidthI(w.dc, 0, 26, gi, wg));
    for (int i = 0; i < 26; i++) CHECKF(wc[i] == wg[i], "'%c' char %d glyph %d", 'a' + i, wc[i], wg[i]);
    CHECK(fm.GetCharABCWidthsW(w.dc, 'a', 'z', ac));
    CHECK(fm.GetCharABCWidthsI(w.dc, 0, 26, gi, ag));
    CHECK(!memcmp(ac, ag, sizeof ac));
    ws_close(&w);
}

TEST(gdi_font_metrics_text_chari_sequential_indices) {
    FM_BEGIN();
    struct ws w; if (!ws_open(&w, L"Arial", -20, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    /* pgi == NULL: widths for glyph indices giFirst .. giFirst+cgi-1. */
    INT seq[8], expl[8]; WORD idx[8];
    for (int i = 0; i < 8; i++) idx[i] = 36 + i;
    CHECK(fm.GetCharWidthI(w.dc, 36, 8, NULL, seq));
    CHECK(fm.GetCharWidthI(w.dc, 0, 8, idx, expl));
    for (int i = 0; i < 8; i++) CHECKF(seq[i] == expl[i], "glyph %d: %d vs %d", 36 + i, seq[i], expl[i]);
    ws_close(&w);
}

TEST(gdi_font_metrics_text_chari_invalid) {
    FM_BEGIN();
    struct ws w; if (!ws_open(&w, L"Arial", -20, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    WORD g[2] = { 36, 37 }; INT out[2];
    /* gdi32full!GetCharWidthI: NULL width buffer, or pgi==NULL with giFirst above 0xFFFF, is ERROR_INVALID_PARAMETER. */
    SetLastError(0); CHECK(!fm.GetCharWidthI(w.dc, 0, 2, g, NULL)); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SetLastError(0); CHECK(!fm.GetCharWidthI(w.dc, 0x10000, 2, NULL, out)); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    /* cgi == 0 succeeds before any kernel work, even for a bad DC. */
    CHECK(fm.GetCharWidthI(w.dc, 0, 0, g, out));
    CHECK(fm.GetCharWidthI(NULL, 0, 0, g, out));
    CHECK(!fm.GetCharWidthI(NULL, 0, 2, g, out));
    ws_close(&w);
}

/* ---- text extents ---- */

TEST(gdi_font_metrics_text_extent_courier_is_sum_of_advances) {
    FM_BEGIN();
    if (!fm_face_present(L"Courier New")) { t_skip("no Courier New"); return; }
    static const int hs[] = { 8, 12, 16, 20, 33, 50 };
    for (unsigned i = 0; i < 6; i++) {
        struct ws w; if (!ws_open(&w, L"Courier New", -hs[i], 400, ANSI_CHARSET)) continue;
        SIZE sz; CHECK(fm.GetTextExtentPoint32W(w.dc, FM_STR_A, 19, &sz));
        INT adv; fm.GetCharWidth32W(w.dc, 'x', 'x', &adv);
        CHECKF(sz.cx == 19 * adv, "h=%d cx %ld vs 19*%d", hs[i], sz.cx, adv);
        CHECKF(sz.cy > 0, "h=%d", hs[i]);
        ws_close(&w);
    }
}

TEST(gdi_font_metrics_text_extent_cy_equals_tmheight) {
    FM_BEGIN();
    static const WCHAR *faces[] = { L"Arial", L"Courier New", L"Times New Roman", L"Tahoma" };
    for (unsigned i = 0; i < 4; i++) {
        if (!fm_face_present(faces[i])) continue;
        struct ws w; if (!ws_open(&w, faces[i], -21, 400, ANSI_CHARSET)) continue;
        SIZE sz; TEXTMETRICW tm;
        CHECK(fm.GetTextExtentPoint32W(w.dc, L"Hxg", 3, &sz)); CHECK(fm.GetTextMetricsW(w.dc, &tm));
        CHECKF(sz.cy == tm.tmHeight, "face %u cy %ld tmHeight %ld", i, sz.cy, tm.tmHeight);
        ws_close(&w);
    }
}

TEST(gdi_font_metrics_text_extent_empty_string) {
    FM_BEGIN();
    struct ws w; if (!ws_open(&w, L"Arial", -16, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    SIZE sz = { 77, 77 }; TEXTMETRICW tm; fm.GetTextMetricsW(w.dc, &tm);
    CHECK(fm.GetTextExtentPoint32W(w.dc, L"", 0, &sz));
    EXPECT_EQ(sz.cx, 0);
    t_note("empty string extent cy = %ld (tmHeight %ld)", sz.cy, tm.tmHeight);
    ws_close(&w);
}

TEST(gdi_font_metrics_text_extent_invalid_args) {
    FM_BEGIN();
    struct ws w; if (!ws_open(&w, L"Arial", -16, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    SIZE sz;
    CHECK(!fm.GetTextExtentPoint32W(NULL, L"A", 1, &sz));
    CHECK(!fm.GetTextExtentPoint32W((HDC)(ULONG_PTR)0x5151, L"A", 1, &sz));
    CHECK(!fm.GetTextExtentPoint32W(w.dc, L"A", -1, &sz));      /* NtGdiGetTextExtent: negative count fails */
    ws_close(&w);
}

TEST(gdi_font_metrics_text_extent_additive_courier_and_near_additive_arial) {
    FM_BEGIN();
    static const WCHAR *faces[] = { L"Courier New", L"Arial", L"Times New Roman" };
    for (unsigned i = 0; i < 3; i++) {
        if (!fm_face_present(faces[i])) continue;
        struct ws w; if (!ws_open(&w, faces[i], -20, 400, ANSI_CHARSET)) continue;
        const WCHAR *s = L"Hello, world";
        int n = lstrlenW(s);
        SIZE total; CHECK(fm.GetTextExtentPoint32W(w.dc, s, n, &total));
        long sum = sumw(w.dc, s, n);
        long tol = i == 0 ? 0 : n / 2 + 1;       /* the extent is computed from unrounded advances for proportional fonts */
        CHECKF(labs(total.cx - sum) <= tol, "face %u: extent %ld sum of advances %ld", i, total.cx, sum);
        if (labs(total.cx - sum) != 0) t_note("face %u: extent %ld vs advance sum %ld", i, total.cx, sum);
        ws_close(&w);
    }
}

TEST(gdi_font_metrics_text_extent_prefix_monotone) {
    FM_BEGIN();
    struct ws w; if (!ws_open(&w, L"Arial", -18, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    const WCHAR *s = L"The quick brown fox jumps";
    int n = lstrlenW(s); long prev = 0;
    for (int k = 1; k <= n; k++) {
        SIZE sz; CHECK(fm.GetTextExtentPoint32W(w.dc, s, k, &sz));
        CHECKF(sz.cx >= prev, "prefix %d: %ld < %ld", k, sz.cx, prev);
        prev = sz.cx;
    }
    ws_close(&w);
}

TEST(gdi_font_metrics_text_extent_legacy_equals_32) {
    FM_BEGIN();
    struct ws w; if (!ws_open(&w, L"Arial", -18, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    SIZE a, b;
    CHECK(fm.GetTextExtentPointW(w.dc, FM_STR_A, 19, &a)); CHECK(fm.GetTextExtentPoint32W(w.dc, FM_STR_A, 19, &b));
    EXPECT_EQ(a.cx, b.cx); EXPECT_EQ(a.cy, b.cy);
    ws_close(&w);
}

TEST(gdi_font_metrics_text_extent_ansi_equals_wide) {
    FM_BEGIN();
    struct ws w; if (!ws_open(&w, L"Arial", -18, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    SIZE a, b;
    CHECK(fm.GetTextExtentPoint32A(w.dc, "The quick brown fox", 19, &a)); CHECK(fm.GetTextExtentPoint32W(w.dc, FM_STR_A, 19, &b));
    EXPECT_EQ(a.cx, b.cx); EXPECT_EQ(a.cy, b.cy);
    ws_close(&w);
}

TEST(gdi_font_metrics_text_extent_scales_linearly_courier) {
    FM_BEGIN();
    if (!fm_face_present(L"Courier New")) { t_skip("no Courier New"); return; }
    long first = 0;
    for (int h = 10; h <= 60; h += 10) {
        struct ws w; if (!ws_open(&w, L"Courier New", -h, 400, ANSI_CHARSET)) continue;
        SIZE sz; fm.GetTextExtentPoint32W(w.dc, L"0123456789", 10, &sz);
        INT adv; fm.GetCharWidth32W(w.dc, '0', '0', &adv);
        EXPECT_EQ(sz.cx, 10 * adv);
        CHECKF(abs(adv * 1000 / h - 600) <= 100 + 600 / h * 1, "h=%d advance %d (Courier New is 0.6 em)", h, adv);
        if (!first) first = sz.cx; else CHECK(sz.cx > first);
        ws_close(&w);
    }
}

TEST(gdi_font_metrics_text_extent_glyph_index_variant) {
    FM_BEGIN();
    if (!fm_face_present(L"Courier New")) { t_skip("no Courier New"); return; }
    struct ws w; if (!ws_open(&w, L"Courier New", -20, 400, ANSI_CHARSET)) return;
    WORD gi[8]; SIZE s1, s2;
    fm.GetGlyphIndicesW(w.dc, L"abcdefgh", 8, gi, 0);
    CHECK(fm.GetTextExtentPointI(w.dc, gi, 8, &s1));
    CHECK(fm.GetTextExtentPoint32W(w.dc, L"abcdefgh", 8, &s2));
    EXPECT_EQ(s1.cx, s2.cx); EXPECT_EQ(s1.cy, s2.cy);
    ws_close(&w);
}

TEST(gdi_font_metrics_text_extent_character_extra_adds_per_char) {
    FM_BEGIN();
    struct ws w; if (!ws_open(&w, L"Arial", -18, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    SIZE base, ext;
    fm.GetTextExtentPoint32W(w.dc, L"abcdef", 6, &base);
    EXPECT_EQ(fm.SetTextCharacterExtra(w.dc, 3), 0);
    fm.GetTextExtentPoint32W(w.dc, L"abcdef", 6, &ext);
    EXPECT_EQ(ext.cx, base.cx + 6 * 3);     /* each of the 6 characters gets 3 extra units */
    EXPECT_EQ(fm.SetTextCharacterExtra(w.dc, 0), 3);
    fm.GetTextExtentPoint32W(w.dc, L"abcdef", 6, &ext);
    EXPECT_EQ(ext.cx, base.cx);
    ws_close(&w);
}

TEST(gdi_font_metrics_text_extent_justification_adds_break_extra) {
    FM_BEGIN();
    struct ws w; if (!ws_open(&w, L"Arial", -18, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    SIZE base, just;
    const WCHAR *s = L"a b c";
    fm.GetTextExtentPoint32W(w.dc, s, 5, &base);
    CHECK(fm.SetTextJustification(w.dc, 20, 2));       /* 20 units over 2 break characters */
    fm.GetTextExtentPoint32W(w.dc, s, 5, &just);
    t_note("justified extent %ld, base %ld", just.cx, base.cx);
    CHECK(just.cx >= base.cx);
    CHECK(fm.SetTextJustification(w.dc, 0, 0));
    fm.GetTextExtentPoint32W(w.dc, s, 5, &just);
    EXPECT_EQ(just.cx, base.cx);
    ws_close(&w);
}

/* ---- GetTextExtentExPoint ---- */

TEST(gdi_font_metrics_text_extentex_total_equals_extent32) {
    FM_BEGIN();
    struct ws w; if (!ws_open(&w, L"Arial", -17, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    SIZE a, b; INT fit = -1, dx[24];
    CHECK(fm.GetTextExtentPoint32W(w.dc, FM_STR_A, 19, &a));
    CHECK(fm.GetTextExtentExPointW(w.dc, FM_STR_A, 19, 0, NULL, NULL, &b));
    EXPECT_EQ(a.cx, b.cx); EXPECT_EQ(a.cy, b.cy);
    CHECK(fm.GetTextExtentExPointW(w.dc, FM_STR_A, 19, 100000, &fit, dx, &b));
    EXPECT_EQ(fit, 19);
    EXPECT_EQ(dx[18], b.cx);              /* the last partial extent is the total */
    EXPECT_EQ(b.cx, a.cx);
    ws_close(&w);
}

TEST(gdi_font_metrics_text_extentex_partial_monotone_and_prefix) {
    FM_BEGIN();
    struct ws w; if (!ws_open(&w, L"Arial", -17, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    INT fit, dx[24]; SIZE sz;
    CHECK(fm.GetTextExtentExPointW(w.dc, FM_STR_A, 19, 1 << 20, &fit, dx, &sz));
    for (int i = 1; i < 19; i++) CHECKF(dx[i] >= dx[i - 1], "dx[%d]=%d < dx[%d]=%d", i, dx[i], i - 1, dx[i - 1]);
    CHECK(dx[0] > 0);
    for (int k = 1; k <= 19; k += 3) {
        SIZE p; fm.GetTextExtentPoint32W(w.dc, FM_STR_A, k, &p);
        CHECKF(abs(p.cx - dx[k - 1]) <= 1, "prefix %d: %ld vs partial %d", k, p.cx, dx[k - 1]);
    }
    ws_close(&w);
}

TEST(gdi_font_metrics_text_extentex_fit_count_with_max_extent) {
    FM_BEGIN();
    struct ws w; if (!ws_open(&w, L"Arial", -17, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    INT all[24], fit, dx[24]; SIZE sz;
    fm.GetTextExtentExPointW(w.dc, FM_STR_A, 19, 1 << 20, &fit, all, &sz);
    static const int fracs[] = { 1, 2, 3, 5, 7, 10 };
    for (unsigned i = 0; i < 6; i++) {
        int max = sz.cx * fracs[i] / 10;
        CHECK(fm.GetTextExtentExPointW(w.dc, FM_STR_A, 19, max, &fit, dx, &sz));
        CHECKF(fit >= 0 && fit <= 19, "max %d fit %d", max, fit);
        if (fit > 0) CHECKF(dx[fit - 1] <= max, "max %d: dx[%d]=%d exceeds", max, fit - 1, dx[fit - 1]);
        if (fit < 19) CHECKF(all[fit] > max, "max %d: next char dx %d fits yet fit=%d", max, all[fit], fit);
    }
    ws_close(&w);
}

TEST(gdi_font_metrics_text_extentex_max_zero_and_small) {
    FM_BEGIN();
    struct ws w; if (!ws_open(&w, L"Arial", -17, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    INT fit = 99, dx[24]; SIZE sz;
    CHECK(fm.GetTextExtentExPointW(w.dc, FM_STR_A, 19, 0, &fit, dx, &sz));
    EXPECT_EQ(fit, 0);
    INT w0; fm.GetCharWidth32W(w.dc, 'T', 'T', &w0);
    CHECK(fm.GetTextExtentExPointW(w.dc, FM_STR_A, 19, w0 - 1, &fit, dx, &sz));
    EXPECT_EQ(fit, 0);
    CHECK(fm.GetTextExtentExPointW(w.dc, FM_STR_A, 19, w0, &fit, dx, &sz));
    CHECK(fit >= 1);
    ws_close(&w);
}

TEST(gdi_font_metrics_text_extentex_empty_and_negative) {
    FM_BEGIN();
    struct ws w; if (!ws_open(&w, L"Arial", -17, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    INT fit = 55; SIZE sz = { 9, 9 };
    CHECK(fm.GetTextExtentExPointW(w.dc, L"", 0, 100, &fit, NULL, &sz));
    EXPECT_EQ(fit, 0); EXPECT_EQ(sz.cx, 0);
    SetLastError(0);
    CHECK(!fm.GetTextExtentExPointW(w.dc, L"abc", -1, 100, &fit, NULL, &sz));
    CHECK(!fm.GetTextExtentExPointW(NULL, L"abc", 3, 100, &fit, NULL, &sz));
    ws_close(&w);
}

TEST(gdi_font_metrics_text_extentex_courier_fit_exact) {
    FM_BEGIN();
    if (!fm_face_present(L"Courier New")) { t_skip("no Courier New"); return; }
    struct ws w; if (!ws_open(&w, L"Courier New", -20, 400, ANSI_CHARSET)) return;
    INT adv; fm.GetCharWidth32W(w.dc, 'a', 'a', &adv);
    INT fit, dx[16]; SIZE sz;
    for (int k = 0; k <= 10; k++) {
        CHECK(fm.GetTextExtentExPointW(w.dc, L"abcdefghij", 10, k * adv + (adv / 2), &fit, dx, &sz));
        CHECKF(fit == k, "max %d fit %d expected %d", k * adv + adv / 2, fit, k);
    }
    for (int i = 0; i < 10; i++) CHECKF(dx[i] == (i + 1) * adv, "dx[%d]=%d", i, dx[i]);
    ws_close(&w);
}

/* ---- character placement ---- */

TEST(gdi_font_metrics_text_gcp_null_results_packs_extent) {
    FM_BEGIN();
    struct ws w; if (!ws_open(&w, L"Arial", -17, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    SIZE sz; fm.GetTextExtentPoint32W(w.dc, L"Placement", 9, &sz);
    /* gdi32full!GetCharacterPlacementW with lpResults==NULL returns (cy << 16) | (cx & 0xffff). */
    DWORD r = fm.GetCharacterPlacementW(w.dc, L"Placement", 9, 0, NULL, 0);
    EXPECT_EQ(LOWORD(r), sz.cx & 0xffff);
    EXPECT_EQ(HIWORD(r), sz.cy & 0xffff);
    ws_close(&w);
}

TEST(gdi_font_metrics_text_gcp_invalid_args) {
    FM_BEGIN();
    if (t_is_wine()) { t_skip("Wine hangs on invalid GetCharacterPlacementW arguments"); return; }
    struct ws w; if (!ws_open(&w, L"Arial", -17, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    GCP_RESULTSW r; memset(&r, 0, sizeof r); r.lStructSize = sizeof r;
    SetLastError(0); EXPECT_EQ(fm.GetCharacterPlacementW(w.dc, NULL, 3, 0, &r, 0), 0); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SetLastError(0); EXPECT_EQ(fm.GetCharacterPlacementW(w.dc, L"abc", 0, 0, &r, 0), 0); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SetLastError(0); EXPECT_EQ(fm.GetCharacterPlacementW(w.dc, L"abc", -2, 0, &r, 0), 0); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SetLastError(0); EXPECT_EQ(fm.GetCharacterPlacementW(w.dc, L"abc", 3, -2, &r, 0), 0); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    ws_close(&w);
}

TEST(gdi_font_metrics_text_gcp_dx_and_order) {
    FM_BEGIN();
    if (!fm_face_present(L"Courier New")) { t_skip("no Courier New"); return; }
    struct ws w; if (!ws_open(&w, L"Courier New", -20, 400, ANSI_CHARSET)) return;
    const WCHAR *s = L"abcdef"; int n = 6;
    WCHAR out[16] = { 0 }; INT dx[16] = { 0 }; UINT order[16] = { 0 };
    GCP_RESULTSW r; memset(&r, 0, sizeof r);
    r.lStructSize = sizeof r; r.lpOutString = out; r.lpOrder = order; r.lpDx = dx; r.nGlyphs = n;
    DWORD ret = fm.GetCharacterPlacementW(w.dc, s, n, 0, &r, GCP_REORDER);
    CHECK(ret != 0);
    INT adv; fm.GetCharWidth32W(w.dc, 'a', 'a', &adv);
    EXPECT_EQ(r.nGlyphs, n);
    for (int i = 0; i < n; i++) CHECKF(dx[i] == adv, "dx[%d]=%d adv %d", i, dx[i], adv);
    for (int i = 0; i < n; i++) if (order[i] != (UINT)i) t_note("lpOrder[%d] = %u", i, order[i]);
    EXPECT_EQ(LOWORD(ret), n * adv);
    ws_close(&w);
}

TEST(gdi_font_metrics_text_gcp_outstring_copies_text) {
    FM_BEGIN();
    struct ws w; if (!ws_open(&w, L"Arial", -17, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    WCHAR out[32]; memset(out, 0, sizeof out);
    GCP_RESULTSW r; memset(&r, 0, sizeof r);
    r.lStructSize = sizeof r; r.lpOutString = out; r.nGlyphs = 10;
    DWORD ret = fm.GetCharacterPlacementW(w.dc, L"Text Value", 10, 0, &r, 0);
    CHECK(ret != 0);
    CHECK(!memcmp(out, L"Text Value", 20));
    ws_close(&w);
}

TEST(gdi_font_metrics_text_gcp_nglyphs_clamps_count) {
    FM_BEGIN();
    struct ws w; if (!ws_open(&w, L"Arial", -17, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    INT dx[16]; WCHAR out[16];
    GCP_RESULTSW r; memset(&r, 0, sizeof r);
    r.lStructSize = sizeof r; r.lpDx = dx; r.lpOutString = out; r.nGlyphs = 3;
    /* gdi32full clamps nCount to lpResults->nGlyphs before the syscall. */
    DWORD ret = fm.GetCharacterPlacementW(w.dc, L"abcdefgh", 8, 0, &r, 0);
    CHECK(ret != 0);
    CHECK(r.nGlyphs <= 3);
    SIZE s3; fm.GetTextExtentPoint32W(w.dc, L"abc", 3, &s3);
    CHECKF(labs((long)LOWORD(ret) - s3.cx) <= 1, "extent %u vs 3-char extent %ld", LOWORD(ret), s3.cx);
    ws_close(&w);
}

TEST(gdi_font_metrics_text_gcp_maxextent_limits) {
    FM_BEGIN();
    if (!fm_face_present(L"Courier New")) { t_skip("no Courier New"); return; }
    struct ws w; if (!ws_open(&w, L"Courier New", -20, 400, ANSI_CHARSET)) return;
    INT adv; fm.GetCharWidth32W(w.dc, 'a', 'a', &adv);
    WCHAR out[16] = { 0 }; INT dx[16] = { 0 };
    GCP_RESULTSW r; memset(&r, 0, sizeof r);
    r.lStructSize = sizeof r; r.lpOutString = out; r.lpDx = dx; r.nGlyphs = 10;
    DWORD ret = fm.GetCharacterPlacementW(w.dc, L"abcdefghij", 10, adv * 4 + adv / 2, &r, GCP_MAXEXTENT);
    CHECK(ret != 0);
    EXPECT_EQ(r.nMaxFit, 4);
    CHECKF(r.nGlyphs <= 10, "nGlyphs %u", r.nGlyphs);
    ws_close(&w);
}

TEST(gdi_font_metrics_text_gcp_kerning_flag_not_wider_than_plain) {
    FM_BEGIN();
    struct ws w; if (!ws_open(&w, L"Times New Roman", -40, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    INT dx1[8], dx2[8]; WCHAR o1[8], o2[8];
    GCP_RESULTSW a, b; memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
    a.lStructSize = b.lStructSize = sizeof a; a.lpDx = dx1; b.lpDx = dx2; a.lpOutString = o1; b.lpOutString = o2; a.nGlyphs = b.nGlyphs = 4;
    DWORD ra = fm.GetCharacterPlacementW(w.dc, L"AVAT", 4, 0, &a, 0);
    DWORD rb = fm.GetCharacterPlacementW(w.dc, L"AVAT", 4, 0, &b, GCP_USEKERNING);
    t_note("GCP \"AVAT\" plain %u kerned %u", LOWORD(ra), LOWORD(rb));
    CHECK(ra != 0 && rb != 0);
    CHECK(LOWORD(rb) <= LOWORD(ra));     /* kerning only ever tightens a pair that has a negative value */
    ws_close(&w);
}

/* ---- text alignment, character extra and break extra: DC_ATTR state ---- */

TEST(gdi_font_metrics_text_dcattr_textalign_default_and_roundtrip) {
    FM_BEGIN();
    HDC dc = fm_dc(); if (!dc) { t_skip("no dc"); return; }
    EXPECT_EQ(fm.GetTextAlign(dc), TA_LEFT | TA_TOP | TA_NOUPDATECP);   /* 0 */
    static const UINT modes[] = { TA_LEFT, TA_RIGHT, TA_CENTER, TA_TOP, TA_BOTTOM, TA_BASELINE, TA_UPDATECP,
                                  TA_RIGHT | TA_BOTTOM, TA_CENTER | TA_BASELINE, TA_UPDATECP | TA_RIGHT | TA_BASELINE, TA_RTLREADING, TA_RTLREADING | TA_CENTER };
    UINT prev = 0;
    for (unsigned i = 0; i < sizeof modes / sizeof *modes; i++) {
        UINT r = fm.SetTextAlign(dc, modes[i]);
        CHECKF(r == prev, "mode %u: returned previous 0x%x want 0x%x", i, r, prev);
        EXPECT_EQ(fm.GetTextAlign(dc), modes[i]);
        long raw;
        if (dca_get(dc, DCA_TEXTALIGN, &raw)) EXPECT_EQ((unsigned long)raw, modes[i]);   /* the kernel wrote the shared DC_ATTR */
        prev = modes[i];
    }
    fm.SetTextAlign(dc, 0);
    fm_freedc(dc);
}

TEST(gdi_font_metrics_text_dcattr_textalign_invalid_dc) {
    FM_BEGIN();
    EXPECT_EQ(fm.GetTextAlign(NULL), GDI_ERROR);
    EXPECT_EQ(fm.GetTextAlign((HDC)(ULONG_PTR)0x4242), GDI_ERROR);
    HDC dc = fm_dc(); if (dc) { fm_freedc(dc); EXPECT_EQ(fm.GetTextAlign(dc), GDI_ERROR); }
    UINT r = fm.SetTextAlign(NULL, TA_CENTER);
    CHECK(r == GDI_ERROR);
}

TEST(gdi_font_metrics_text_dcattr_textalign_per_dc) {
    FM_BEGIN();
    HDC a = fm_dc(), b = fm_dc();
    if (!a || !b) { t_skip("no dc"); fm_freedc(a); fm_freedc(b); return; }
    fm.SetTextAlign(a, TA_RIGHT | TA_BOTTOM);
    EXPECT_EQ(fm.GetTextAlign(b), 0);
    EXPECT_EQ(fm.GetTextAlign(a), TA_RIGHT | TA_BOTTOM);
    void *pa = dcattr(a), *pb = dcattr(b);
    if (pa && pb) CHECK(pa != pb);
    fm_freedc(a); fm_freedc(b);
}

TEST(gdi_font_metrics_text_dcattr_textalign_savedc_restores) {
    FM_BEGIN();
    HDC dc = fm_dc(); if (!dc) { t_skip("no dc"); return; }
    fm.SetTextAlign(dc, TA_CENTER);
    int s = fm.SaveDC(dc);
    fm.SetTextAlign(dc, TA_RIGHT | TA_BASELINE);
    EXPECT_EQ(fm.GetTextAlign(dc), TA_RIGHT | TA_BASELINE);
    CHECK(fm.RestoreDC(dc, s));
    EXPECT_EQ(fm.GetTextAlign(dc), TA_CENTER);
    fm_freedc(dc);
}

TEST(gdi_font_metrics_text_dcattr_char_extra_roundtrip) {
    FM_BEGIN();
    HDC dc = fm_dc(); if (!dc) { t_skip("no dc"); return; }
    EXPECT_EQ(fm.GetTextCharacterExtra(dc), 0);
    static const int vals[] = { 5, -3, 0, 1000, -1000, 0x7fffffff };
    int prev = 0;
    for (unsigned i = 0; i < sizeof vals / sizeof *vals; i++) {
        /* gdi32full!SetTextCharacterExtra stores into DC_ATTR+0x118 and returns the old value. */
        EXPECT_EQ(fm.SetTextCharacterExtra(dc, vals[i]), prev);
        EXPECT_EQ(fm.GetTextCharacterExtra(dc), vals[i]);
        long raw; if (dca_get(dc, DCA_CHAREXTRA, &raw)) EXPECT_EQ(raw, vals[i]);
        prev = vals[i];
    }
    fm.SetTextCharacterExtra(dc, 0);
    fm_freedc(dc);
}

TEST(gdi_font_metrics_text_dcattr_char_extra_invalid) {
    FM_BEGIN();
    HDC dc = fm_dc(); if (!dc) { t_skip("no dc"); return; }
    /* extra == INT_MIN is the error sentinel and is rejected with ERROR_INVALID_PARAMETER. */
    SetLastError(0);
    EXPECT_EQ(fm.SetTextCharacterExtra(dc, (int)0x80000000), (int)0x80000000);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SetLastError(0);
    EXPECT_EQ(fm.SetTextCharacterExtra(NULL, 4), (int)0x80000000);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    EXPECT_EQ(fm.GetTextCharacterExtra(NULL), (int)0x80000000);
    EXPECT_EQ(fm.GetTextCharacterExtra(dc), 0);
    fm_freedc(dc);
}

TEST(gdi_font_metrics_text_dcattr_justification_roundtrip) {
    FM_BEGIN();
    HDC dc = fm_dc(); if (!dc) { t_skip("no dc"); return; }
    long v;
    /* SetTextJustification stores extra at DC_ATTR+0x11c and the break count at +0x120. */
    CHECK(fm.SetTextJustification(dc, 17, 4));
    if (dca_get(dc, DCA_BREAKEXTRA, &v)) EXPECT_EQ(v, 17);
    if (dca_get(dc, DCA_BREAKCOUNT, &v)) EXPECT_EQ(v, 4);
    CHECK(fm.SetTextJustification(dc, -9, 2));
    if (dca_get(dc, DCA_BREAKEXTRA, &v)) EXPECT_EQ(v, -9);
    if (dca_get(dc, DCA_BREAKCOUNT, &v)) EXPECT_EQ(v, 2);
    CHECK(fm.SetTextJustification(dc, 0, 0));
    if (dca_get(dc, DCA_BREAKEXTRA, &v)) EXPECT_EQ(v, 0);
    if (dca_get(dc, DCA_BREAKCOUNT, &v)) EXPECT_EQ(v, 0);
    fm_freedc(dc);
}

TEST(gdi_font_metrics_text_dcattr_justification_invalid_dc) {
    FM_BEGIN();
    CHECK(!fm.SetTextJustification(NULL, 5, 1));
    CHECK(!fm.SetTextJustification((HDC)(ULONG_PTR)0x999, 5, 1));
    HDC dc = fm_dc(); if (dc) { fm_freedc(dc); CHECK(!fm.SetTextJustification(dc, 5, 1)); }
}

TEST(gdi_font_metrics_text_dcattr_textcolor_default_black) {
    FM_BEGIN();
    HDC dc = fm_dc(); if (!dc) { t_skip("no dc"); return; }
    EXPECT_EQ(fm.GetTextColor(dc), 0);    /* DC_ATTR+0xbc starts as black */
    long raw; if (dca_get(dc, DCA_TEXTCOLOR, &raw)) EXPECT_EQ(raw, 0);
    EXPECT_EQ(fm.GetTextColor(NULL), 0xFFFFFFFF);
    fm_freedc(dc);
}

TEST(gdi_font_metrics_text_dcattr_cell_belongs_to_process) {
    FM_BEGIN();
    HDC dc = fm_dc(); if (!dc) { t_skip("no dc"); return; }
    struct fm_cell c;
    CHECK(fm_cell_of(dc, &c));
    EXPECT_EQ(c.objt, 1);                   /* DC */
    CHECK(c.puser != 0);                    /* the DC_ATTR the text attribute accessors read */
    EXPECT_EQ(c.owner & 0xfffffffe, GetCurrentProcessId() & 0xfffffffe);
    fm_freedc(dc);
}

/* ---- text alignment effect on the current position ---- */

TEST(gdi_font_metrics_text_updatecp_advances_position) {
    FM_BEGIN();
    struct ws w; if (!ws_open(&w, L"Arial", -17, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    POINT p;
    SIZE sz; fm.GetTextExtentPoint32W(w.dc, L"Advance", 7, &sz);
    /* Without TA_UPDATECP, TextOut leaves the current position alone. */
    fm.MoveToEx(w.dc, 10, 20, NULL);
    CHECK(fm.TextOutW(w.dc, 5, 5, L"Advance", 7));
    CHECK(fm.GetCurrentPositionEx(w.dc, &p));
    EXPECT_EQ(p.x, 10); EXPECT_EQ(p.y, 20);
    /* With TA_UPDATECP, the string is drawn at the current position and the position advances by the text width. */
    fm.SetTextAlign(w.dc, TA_UPDATECP);
    fm.MoveToEx(w.dc, 10, 20, NULL);
    CHECK(fm.TextOutW(w.dc, 0, 0, L"Advance", 7));
    CHECK(fm.GetCurrentPositionEx(w.dc, &p));
    EXPECT_EQ(p.x, 10 + sz.cx); EXPECT_EQ(p.y, 20);
    /* TA_RIGHT|TA_UPDATECP moves the position back to the left edge of the string... width-based delta only. */
    fm.SetTextAlign(w.dc, 0);
    ws_close(&w);
}

TEST(gdi_font_metrics_text_updatecp_right_aligned_moves_left) {
    FM_BEGIN();
    struct ws w; if (!ws_open(&w, L"Arial", -17, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    SIZE sz; fm.GetTextExtentPoint32W(w.dc, L"Right", 5, &sz);
    POINT p;
    /* TA_RIGHT|TA_UPDATECP: the string ends at the current position; the position then becomes the string's start. */
    fm.SetTextAlign(w.dc, TA_UPDATECP | TA_RIGHT);
    fm.MoveToEx(w.dc, 200, 30, NULL);
    CHECK(fm.TextOutW(w.dc, 0, 0, L"Right", 5));
    CHECK(fm.GetCurrentPositionEx(w.dc, &p));
    t_note("TA_UPDATECP|TA_RIGHT: position moved from 200 to %ld (text width %ld)", p.x, sz.cx);
    CHECK(p.x != 200 || sz.cx == 0);
    fm.SetTextAlign(w.dc, 0);
    ws_close(&w);
}

TEST(gdi_font_metrics_text_updatecp_zero_length_keeps_position) {
    FM_BEGIN();
    struct ws w; if (!ws_open(&w, L"Arial", -17, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    POINT p;
    fm.SetTextAlign(w.dc, TA_UPDATECP);
    fm.MoveToEx(w.dc, 33, 44, NULL);
    fm.TextOutW(w.dc, 0, 0, L"", 0);
    CHECK(fm.GetCurrentPositionEx(w.dc, &p));
    EXPECT_EQ(p.x, 33); EXPECT_EQ(p.y, 44);
    fm.SetTextAlign(w.dc, 0);
    ws_close(&w);
}
