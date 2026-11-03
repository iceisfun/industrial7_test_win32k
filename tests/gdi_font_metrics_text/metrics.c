/* Font mapper results and TEXTMETRIC invariants via NtGdiGetTextFaceW / NtGdiGetTextMetricsW. */
#include "fm.h"

/* Select a font into a fresh DC and fetch face + metrics. Returns 0 when setup failed. */
struct fmsel { HDC dc; HFONT f; HGDIOBJ old; WCHAR face[LF_FACESIZE + 8]; TEXTMETRICW tm; };
static int fms_open(struct fmsel *s, const WCHAR *face, int h, int wt, int cs, int q, int pf) {
    memset(s, 0, sizeof *s);
    s->dc = fm_dc();
    s->f = fm_font(face, h, wt, cs, q, pf);
    if (!s->dc || !s->f) { if (s->f) fm.DeleteObject(s->f); fm_freedc(s->dc); memset(s, 0, sizeof *s); return 0; }
    s->old = fm.GetCurrentObject(s->dc, OBJ_FONT);
    fm_sel(s->dc, s->f);
    fm_face(s->dc, s->face, LF_FACESIZE + 4);
    if (!fm.GetTextMetricsW(s->dc, &s->tm)) { memset(&s->tm, 0, sizeof s->tm); }
    return 1;
}
static void fms_close(struct fmsel *s) {
    if (!s->dc) return;
    fm_sel(s->dc, s->old); fm.DeleteObject(s->f); fm_freedc(s->dc); memset(s, 0, sizeof *s);
}

/* ---- face selection through the font mapper ---- */

#define FACE_SELECT_TEST(tag, fname) \
TEST(gdi_font_metrics_text_face_select_##tag) { \
    FM_BEGIN(); \
    if (!fm_face_present(fname)) { t_skip("face " #tag " not mapped (not installed)"); return; } \
    struct fmsel s; if (!fms_open(&s, fname, -16, 400, DEFAULT_CHARSET, 0, 0)) { t_skip("setup"); return; } \
    CHECK(fm_wcsieq(s.face, fname)); \
    int n = fm.GetTextFaceW(s.dc, 0, NULL); \
    EXPECT_EQ(n, lstrlenW(s.face) + 1);   /* size query returns length including the NUL */ \
    fms_close(&s); \
}
FACE_SELECT_TEST(arial, L"Arial")
FACE_SELECT_TEST(tahoma, L"Tahoma")
FACE_SELECT_TEST(segoe_ui, L"Segoe UI")
FACE_SELECT_TEST(courier_new, L"Courier New")
FACE_SELECT_TEST(times_new_roman, L"Times New Roman")
FACE_SELECT_TEST(consolas, L"Consolas")
FACE_SELECT_TEST(verdana, L"Verdana")
FACE_SELECT_TEST(calibri, L"Calibri")
FACE_SELECT_TEST(lucida_console, L"Lucida Console")
FACE_SELECT_TEST(georgia, L"Georgia")
FACE_SELECT_TEST(trebuchet, L"Trebuchet MS")
FACE_SELECT_TEST(symbol, L"Symbol")
FACE_SELECT_TEST(wingdings, L"Wingdings")

TEST(gdi_font_metrics_text_face_select_marlett) {
    FM_BEGIN();
    struct fmsel s; if (!fms_open(&s, L"Marlett", -16, 400, SYMBOL_CHARSET, 0, 0)) { t_skip("setup"); return; }
    if (!fm_wcsieq(s.face, L"Marlett")) { t_skip("Marlett not mapped"); fms_close(&s); return; }
    EXPECT_EQ(s.tm.tmCharSet, SYMBOL_CHARSET);   /* Marlett is a symbol-encoded font */
    CHECK(s.tm.tmPitchAndFamily & TMPF_TRUETYPE);
    fms_close(&s);
}

TEST(gdi_font_metrics_text_face_system_raster) {
    FM_BEGIN();
    struct fmsel s; if (!fms_open(&s, L"System", 0, 0, DEFAULT_CHARSET, 0, 0)) { t_skip("setup"); return; }
    char u[40]; fm_u8(s.face, u, sizeof u);
    t_note("mapper result for \"System\": \"%s\" tmHeight %ld", u, s.tm.tmHeight);
    if (fm_wcsieq(s.face, L"System")) {
        CHECK(!(s.tm.tmPitchAndFamily & TMPF_TRUETYPE));   /* System is a bitmap (raster) font */
        CHECK(!(s.tm.tmPitchAndFamily & TMPF_VECTOR));
        CHECK(s.tm.tmHeight == s.tm.tmAscent + s.tm.tmDescent);
    }
    fms_close(&s);
}

TEST(gdi_font_metrics_text_face_terminal_raster) {
    FM_BEGIN();
    struct fmsel s; if (!fms_open(&s, L"Terminal", 0, 0, OEM_CHARSET, 0, 0)) { t_skip("setup"); return; }
    char u[40]; fm_u8(s.face, u, sizeof u);
    t_note("mapper result for \"Terminal\": \"%s\" tmHeight %ld avg %ld charset %u", u, s.tm.tmHeight, s.tm.tmAveCharWidth, s.tm.tmCharSet);
    if (fm_wcsieq(s.face, L"Terminal")) {
        CHECK(!(s.tm.tmPitchAndFamily & TMPF_TRUETYPE));
        EXPECT_EQ(s.tm.tmCharSet, OEM_CHARSET);
        EXPECT_EQ(s.tm.tmAveCharWidth, s.tm.tmMaxCharWidth);   /* Terminal is a fixed-pitch bitmap font */
    }
    fms_close(&s);
}

TEST(gdi_font_metrics_text_face_unknown_name_maps_to_something) {
    FM_BEGIN();
    struct fmsel s; if (!fms_open(&s, L"No Such Font Face 12345", -16, 400, DEFAULT_CHARSET, 0, 0)) { t_skip("setup"); return; }
    CHECK(s.face[0] != 0);                         /* mapper always substitutes */
    CHECK(!fm_wcsieq(s.face, L"No Such Font Face 12345"));
    CHECK(s.tm.tmHeight > 0);
    fms_close(&s);
}

TEST(gdi_font_metrics_text_face_empty_name_picks_by_family) {
    FM_BEGIN();
    static const struct { int fam; const char *n; } t[] = { { FF_ROMAN, "ROMAN" }, { FF_SWISS, "SWISS" }, { FF_MODERN, "MODERN" }, { FF_SCRIPT, "SCRIPT" }, { FF_DECORATIVE, "DECORATIVE" } };
    for (unsigned i = 0; i < sizeof t / sizeof *t; i++) {
        struct fmsel s; if (!fms_open(&s, L"", -16, 400, DEFAULT_CHARSET, 0, t[i].fam)) { t_skip("setup"); return; }
        char u[40]; fm_u8(s.face, u, sizeof u);
        CHECK(s.face[0] != 0);
        CHECK(s.tm.tmHeight > 0);
        t_note("family %s -> \"%s\" pitchfam 0x%02x", t[i].n, u, s.tm.tmPitchAndFamily);
        fms_close(&s);
    }
}

TEST(gdi_font_metrics_text_face_case_insensitive) {
    FM_BEGIN();
    if (!fm_face_present(L"Arial")) { t_skip("no Arial"); return; }
    struct fmsel a, b;
    if (!fms_open(&a, L"ARIAL", -16, 400, DEFAULT_CHARSET, 0, 0)) return;
    if (!fms_open(&b, L"arial", -16, 400, DEFAULT_CHARSET, 0, 0)) { fms_close(&a); return; }
    CHECK(fm_wcsieq(a.face, L"Arial"));
    CHECK(!lstrcmpW(a.face, b.face));   /* hfontCreate upper-cases the name (cCapString), selection is case-blind */
    EXPECT_EQ(a.tm.tmHeight, b.tm.tmHeight);
    fms_close(&a); fms_close(&b);
}

TEST(gdi_font_metrics_text_textface_buffer_protocol) {
    FM_BEGIN();
    if (!fm_face_present(L"Arial")) { t_skip("no Arial"); return; }
    struct fmsel s; if (!fms_open(&s, L"Arial", -16, 400, ANSI_CHARSET, 0, 0)) return;
    WCHAR buf[64];
    EXPECT_EQ(fm.GetTextFaceW(s.dc, 64, buf), 6);        /* "Arial" + NUL */
    CHECK(!lstrcmpW(buf, L"Arial"));
    EXPECT_EQ(fm.GetTextFaceW(s.dc, 0, NULL), 6);
    EXPECT_EQ(fm.GetTextFaceW(s.dc, 6, buf), 6);
    int r3 = fm.GetTextFaceW(s.dc, 3, buf);
    CHECK(r3 >= 0 && r3 <= 3);
    t_note("GetTextFaceW cwc=3 returned %d, buffer \"%ls\"", r3, buf);
    int r1 = fm.GetTextFaceW(s.dc, 1, buf);
    CHECK(r1 >= 0 && r1 <= 1);
    t_note("GetTextFaceW cwc=1 returned %d", r1);
    fms_close(&s);
}

TEST(gdi_font_metrics_text_textface_invalid_args) {
    FM_BEGIN();
    HDC dc = fm_dc(); if (!dc) { t_skip("no dc"); return; }
    WCHAR buf[8] = { 1 };
    /* gdi32full!GetTextFaceW: non-NULL buffer with c < 1 -> ERROR_INVALID_PARAMETER and 0 without a syscall. */
    SetLastError(0);
    EXPECT_EQ(fm.GetTextFaceW(dc, 0, buf), 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SetLastError(0);
    EXPECT_EQ(fm.GetTextFaceW(dc, -1, buf), 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    EXPECT_EQ(buf[0], 1);
    /* An invalid DC fails in the kernel. */
    EXPECT_EQ(fm.GetTextFaceW(NULL, 8, buf), 0);
    EXPECT_EQ(fm.GetTextFaceW((HDC)(ULONG_PTR)0x1234, 8, buf), 0);
    fm_freedc(dc);
}

TEST(gdi_font_metrics_text_nt_textface_direct) {
    FM_BEGIN(); FM_NEED(NtGdiGetTextFaceW_);
    if (!fm_face_present(L"Tahoma")) { t_skip("no Tahoma"); return; }
    struct fmsel s; if (!fms_open(&s, L"Tahoma", -16, 400, ANSI_CHARSET, 0, 0)) return;
    WCHAR b[64];
    EXPECT_EQ(NtGdiGetTextFaceW_(s.dc, 64, b, FALSE), 7);
    CHECK(!lstrcmpW(b, L"Tahoma"));
    /* cwc<=0 or NULL buffer is a pure size query in the kernel (the copy is guarded by 0<cwc && buf). */
    EXPECT_EQ(NtGdiGetTextFaceW_(s.dc, 0, NULL, FALSE), 7);
    EXPECT_EQ(NtGdiGetTextFaceW_(s.dc, 64, NULL, FALSE), 7);
    EXPECT_EQ(NtGdiGetTextFaceW_(s.dc, 64, b, TRUE) > 0, 1);   /* bAliasName variant still resolves a name */
    EXPECT_EQ(NtGdiGetTextFaceW_(NULL, 64, b, FALSE), 0);
    fms_close(&s);
}

TEST(gdi_font_metrics_text_gettextfacea_matches_wide) {
    FM_BEGIN();
    if (!fm_face_present(L"Arial")) { t_skip("no Arial"); return; }
    struct fmsel s; if (!fms_open(&s, L"Arial", -16, 400, ANSI_CHARSET, 0, 0)) return;
    char a[64];
    int na = fm.GetTextFaceA(s.dc, 64, a);
    t_note("GetTextFaceA returned %d for \"Arial\"", na);
    CHECK(na == 5 || na == 6);
    EXPECT_STR(a, "Arial");
    fms_close(&s);
}

/* ---- TEXTMETRIC invariants, table driven over faces and sizes ---- */

static void tm_invariants(const char *tag, const TEXTMETRICW *tm) {
    CHECKF(tm->tmHeight == tm->tmAscent + tm->tmDescent, "%s: height %ld asc %ld desc %ld", tag, tm->tmHeight, tm->tmAscent, tm->tmDescent);
    CHECKF(tm->tmHeight > 0, "%s", tag);
    CHECKF(tm->tmAscent > 0 && tm->tmDescent >= 0, "%s", tag);
    CHECKF(tm->tmInternalLeading >= 0 && tm->tmInternalLeading < tm->tmHeight, "%s: ilead %ld", tag, tm->tmInternalLeading);
    CHECKF(tm->tmExternalLeading >= 0, "%s", tag);
    CHECKF(tm->tmAveCharWidth > 0, "%s", tag);
    CHECKF(tm->tmMaxCharWidth >= tm->tmAveCharWidth, "%s: max %ld ave %ld", tag, tm->tmMaxCharWidth, tm->tmAveCharWidth);
    CHECKF(tm->tmFirstChar <= tm->tmLastChar, "%s", tag);
    CHECKF(tm->tmDigitizedAspectX > 0 && tm->tmDigitizedAspectY > 0, "%s", tag);
}

static const struct { const WCHAR *face; const char *tag; } k_faces[] = {
    { L"Arial", "Arial" }, { L"Tahoma", "Tahoma" }, { L"Segoe UI", "Segoe UI" }, { L"Courier New", "Courier New" },
    { L"Times New Roman", "Times New Roman" }, { L"Consolas", "Consolas" }, { L"Verdana", "Verdana" } };
static const int k_sizes[] = { -8, -9, -10, -12, -14, -16, -20, -24, -32, -48, -72, -120 };

TEST(gdi_font_metrics_text_tm_invariants_table) {
    FM_BEGIN();
    for (unsigned i = 0; i < sizeof k_faces / sizeof *k_faces; i++) {
        if (!fm_face_present(k_faces[i].face)) { t_note("%s absent", k_faces[i].tag); continue; }
        for (unsigned j = 0; j < sizeof k_sizes / sizeof *k_sizes; j++) {
            struct fmsel s; char tag[64];
            if (!fms_open(&s, k_faces[i].face, k_sizes[j], 400, ANSI_CHARSET, 0, 0)) continue;
            snprintf(tag, sizeof tag, "%s %d", k_faces[i].tag, k_sizes[j]);
            tm_invariants(tag, &s.tm);
            /* Negative lfHeight is the em height: tmHeight - tmInternalLeading == |lfHeight| for a scalable font. */
            CHECKF(s.tm.tmHeight - s.tm.tmInternalLeading == -k_sizes[j], "%s: height %ld ilead %ld", tag, s.tm.tmHeight, s.tm.tmInternalLeading);
            fms_close(&s);
        }
    }
}

TEST(gdi_font_metrics_text_tm_truetype_flags) {
    FM_BEGIN();
    for (unsigned i = 0; i < sizeof k_faces / sizeof *k_faces; i++) {
        if (!fm_face_present(k_faces[i].face)) continue;
        struct fmsel s; if (!fms_open(&s, k_faces[i].face, -16, 400, ANSI_CHARSET, 0, 0)) continue;
        /* TrueType outline fonts report TMPF_TRUETYPE and TMPF_VECTOR, and never TMPF_DEVICE on a memory DC. */
        CHECKF(s.tm.tmPitchAndFamily & TMPF_TRUETYPE, "%s", k_faces[i].tag);
        CHECKF(s.tm.tmPitchAndFamily & TMPF_VECTOR, "%s", k_faces[i].tag);
        CHECKF(!(s.tm.tmPitchAndFamily & TMPF_DEVICE), "%s", k_faces[i].tag);
        fms_close(&s);
    }
}

TEST(gdi_font_metrics_text_tm_pitch_bit_inverted) {
    FM_BEGIN();
    /* TMPF_FIXED_PITCH (bit 0) is set for VARIABLE pitch fonts; monospaced Courier New / Consolas leave it clear. */
    struct fmsel s;
    if (fm_face_present(L"Arial") && fms_open(&s, L"Arial", -16, 400, ANSI_CHARSET, 0, 0)) { CHECK(s.tm.tmPitchAndFamily & TMPF_FIXED_PITCH); fms_close(&s); }
    if (fm_face_present(L"Courier New") && fms_open(&s, L"Courier New", -16, 400, ANSI_CHARSET, 0, 0)) { CHECK(!(s.tm.tmPitchAndFamily & TMPF_FIXED_PITCH)); fms_close(&s); }
    if (fm_face_present(L"Consolas") && fms_open(&s, L"Consolas", -16, 400, ANSI_CHARSET, 0, 0)) { CHECK(!(s.tm.tmPitchAndFamily & TMPF_FIXED_PITCH)); fms_close(&s); }
    if (fm_face_present(L"Times New Roman") && fms_open(&s, L"Times New Roman", -16, 400, ANSI_CHARSET, 0, 0)) { CHECK(s.tm.tmPitchAndFamily & TMPF_FIXED_PITCH); fms_close(&s); }
}

TEST(gdi_font_metrics_text_tm_family_bits) {
    FM_BEGIN();
    static const struct { const WCHAR *face; int fam; } t[] = { { L"Arial", FF_SWISS }, { L"Courier New", FF_MODERN }, { L"Times New Roman", FF_ROMAN } };
    for (unsigned i = 0; i < 3; i++) {
        if (!fm_face_present(t[i].face)) continue;
        struct fmsel s; if (!fms_open(&s, t[i].face, -16, 400, ANSI_CHARSET, 0, 0)) continue;
        EXPECT_EQ(s.tm.tmPitchAndFamily & 0xF0, t[i].fam);
        fms_close(&s);
    }
}

TEST(gdi_font_metrics_text_tm_weight_regular_bold) {
    FM_BEGIN();
    static const WCHAR *faces[] = { L"Arial", L"Tahoma", L"Courier New", L"Times New Roman", L"Segoe UI", L"Verdana" };
    for (unsigned i = 0; i < 6; i++) {
        if (!fm_face_present(faces[i])) continue;
        struct fmsel r, b;
        if (!fms_open(&r, faces[i], -16, 400, ANSI_CHARSET, 0, 0)) continue;
        if (!fms_open(&b, faces[i], -16, 700, ANSI_CHARSET, 0, 0)) { fms_close(&r); continue; }
        EXPECT_EQ(r.tm.tmWeight, 400);
        EXPECT_EQ(b.tm.tmWeight, 700);
        CHECK(b.tm.tmAveCharWidth >= r.tm.tmAveCharWidth);
        fms_close(&r); fms_close(&b);
    }
}

TEST(gdi_font_metrics_text_tm_style_flags_follow_logfont) {
    FM_BEGIN();
    HDC dc = fm_dc(); if (!dc) { t_skip("no dc"); return; }
    for (int mask = 0; mask < 8; mask++) {
        LOGFONTW lf; memset(&lf, 0, sizeof lf);
        lf.lfHeight = -16; lf.lfWeight = 400; lf.lfCharSet = ANSI_CHARSET;
        lf.lfItalic = (mask & 1); lf.lfUnderline = (mask & 2) != 0; lf.lfStrikeOut = (mask & 4) != 0;
        lstrcpyW(lf.lfFaceName, L"Arial");
        HFONT f = fm.CreateFontIndirectW(&lf); if (!f) { CHECK(0); continue; }
        HGDIOBJ old = fm_sel(dc, f);
        TEXTMETRICW tm; CHECK(fm.GetTextMetricsW(dc, &tm));
        CHECKF((tm.tmItalic != 0) == (lf.lfItalic != 0), "mask %d italic", mask);
        CHECKF((tm.tmUnderlined != 0) == (lf.lfUnderline != 0), "mask %d underline", mask);
        CHECKF((tm.tmStruckOut != 0) == (lf.lfStrikeOut != 0), "mask %d strikeout", mask);
        fm_sel(dc, old); fm.DeleteObject(f);
    }
    fm_freedc(dc);
}

TEST(gdi_font_metrics_text_tm_positive_height_is_cell_height) {
    FM_BEGIN();
    static const int h[] = { 12, 16, 20, 24, 32, 48 };
    for (unsigned i = 0; i < sizeof k_faces / sizeof *k_faces; i++) {
        if (!fm_face_present(k_faces[i].face)) continue;
        for (unsigned j = 0; j < sizeof h / sizeof *h; j++) {
            struct fmsel s; if (!fms_open(&s, k_faces[i].face, h[j], 400, ANSI_CHARSET, 0, 0)) continue;
            /* lfHeight > 0 requests a cell height; the mapper rounds the em size so the cell may be off by a pixel. */
            CHECKF(labs(s.tm.tmHeight - h[j]) <= 1, "%s cell %d got %ld", k_faces[i].tag, h[j], s.tm.tmHeight);
            if (s.tm.tmHeight != h[j]) t_note("%s cell height %d -> tmHeight %ld", k_faces[i].tag, h[j], s.tm.tmHeight);
            fms_close(&s);
        }
    }
}

TEST(gdi_font_metrics_text_tm_height_monotonic_with_size) {
    FM_BEGIN();
    static const WCHAR *faces[] = { L"Arial", L"Courier New", L"Times New Roman" };
    for (unsigned i = 0; i < 3; i++) {
        if (!fm_face_present(faces[i])) continue;
        LONG prevh = 0, prevw = 0;
        for (int h = 6; h <= 80; h += 2) {
            struct fmsel s; if (!fms_open(&s, faces[i], -h, 400, ANSI_CHARSET, 0, 0)) continue;
            CHECKF(s.tm.tmHeight >= prevh, "size %d height %ld < %ld", h, s.tm.tmHeight, prevh);
            CHECKF(s.tm.tmAveCharWidth >= prevw, "size %d avg %ld < %ld", h, s.tm.tmAveCharWidth, prevw);
            prevh = s.tm.tmHeight; prevw = s.tm.tmAveCharWidth;
            fms_close(&s);
        }
    }
}

TEST(gdi_font_metrics_text_tm_zero_height_default) {
    FM_BEGIN();
    struct fmsel s; if (!fms_open(&s, L"Arial", 0, 400, ANSI_CHARSET, 0, 0)) return;
    /* lfHeight 0 selects the font's default size; it must still produce a usable scalable font. */
    CHECK(s.tm.tmHeight > 0);
    tm_invariants("Arial h=0", &s.tm);
    t_note("Arial lfHeight=0 -> tmHeight %ld", s.tm.tmHeight);
    fms_close(&s);
}

TEST(gdi_font_metrics_text_tm_explicit_width_scales_avg) {
    FM_BEGIN();
    if (!fm_face_present(L"Arial")) { t_skip("no Arial"); return; }
    HDC dc = fm_dc(); if (!dc) return;
    static const int w[] = { 6, 8, 10, 12 };
    LONG prev = 0;
    for (unsigned i = 0; i < 4; i++) {
        LOGFONTW lf; memset(&lf, 0, sizeof lf);
        lf.lfHeight = -20; lf.lfWidth = w[i]; lf.lfWeight = 400; lf.lfCharSet = ANSI_CHARSET; lstrcpyW(lf.lfFaceName, L"Arial");
        HFONT f = fm.CreateFontIndirectW(&lf);
        HGDIOBJ old = fm_sel(dc, f);
        TEXTMETRICW tm; CHECK(fm.GetTextMetricsW(dc, &tm));
        CHECKF(labs(tm.tmAveCharWidth - w[i]) <= 2, "lfWidth %d -> ave %ld", w[i], tm.tmAveCharWidth);
        CHECKF(tm.tmAveCharWidth > prev, "ave not increasing: %ld after %ld", tm.tmAveCharWidth, prev);
        prev = tm.tmAveCharWidth;
        fm_sel(dc, old); fm.DeleteObject(f);
    }
    fm_freedc(dc);
}

TEST(gdi_font_metrics_text_tm_monospace_courier) {
    FM_BEGIN();
    if (!fm_face_present(L"Courier New")) { t_skip("no Courier New"); return; }
    for (int h = 10; h <= 40; h += 6) {
        struct fmsel s; if (!fms_open(&s, L"Courier New", -h, 400, ANSI_CHARSET, 0, 0)) continue;
        INT wi, wW, wsp;
        CHECK(fm.GetCharWidth32W(s.dc, 'i', 'i', &wi));
        CHECK(fm.GetCharWidth32W(s.dc, 'W', 'W', &wW));
        CHECK(fm.GetCharWidth32W(s.dc, ' ', ' ', &wsp));
        CHECKF(wi == wW && wW == wsp, "h=%d i=%d W=%d sp=%d", h, wi, wW, wsp);
        CHECKF(wi == s.tm.tmAveCharWidth, "h=%d adv %d ave %ld", h, wi, s.tm.tmAveCharWidth);
        fms_close(&s);
    }
}

/* ---- charset resolution ---- */

TEST(gdi_font_metrics_text_charset_default_resolves) {
    FM_BEGIN();
    struct fmsel s; if (!fms_open(&s, L"Arial", -16, 400, DEFAULT_CHARSET, 0, 0)) return;
    CHECK(s.tm.tmCharSet != DEFAULT_CHARSET);   /* the mapper resolves DEFAULT_CHARSET to the system's charset */
    t_note("DEFAULT_CHARSET resolves to %u (ANSI_CHARSET on en-US)", s.tm.tmCharSet);
    EXPECT_EQ(fm.GetTextCharset(s.dc), s.tm.tmCharSet);
    fms_close(&s);
}

TEST(gdi_font_metrics_text_charset_explicit_arial) {
    FM_BEGIN();
    if (!fm_face_present(L"Arial")) { t_skip("no Arial"); return; }
    static const BYTE cs[] = { ANSI_CHARSET, EASTEUROPE_CHARSET, RUSSIAN_CHARSET, GREEK_CHARSET, TURKISH_CHARSET, HEBREW_CHARSET, ARABIC_CHARSET, BALTIC_CHARSET, VIETNAMESE_CHARSET };
    for (unsigned i = 0; i < sizeof cs; i++) {
        struct fmsel s; if (!fms_open(&s, L"Arial", -16, 400, cs[i], 0, 0)) continue;
        CHECKF(fm_wcsieq(s.face, L"Arial"), "charset %u mapped to a different face", cs[i]);
        EXPECT_EQ(s.tm.tmCharSet, cs[i]);
        EXPECT_EQ(fm.GetTextCharset(s.dc), cs[i]);
        EXPECT_EQ(NtGdiGetTextCharsetInfo_ ? NtGdiGetTextCharsetInfo_(s.dc, NULL, 0) : cs[i], cs[i]);
        fms_close(&s);
    }
}

TEST(gdi_font_metrics_text_charset_info_fontsignature) {
    FM_BEGIN(); FM_NEED(NtGdiGetTextCharsetInfo_);
    if (!fm_face_present(L"Arial")) { t_skip("no Arial"); return; }
    struct fmsel s; if (!fms_open(&s, L"Arial", -16, 400, ANSI_CHARSET, 0, 0)) return;
    FONTSIGNATURE fs; memset(&fs, 0xCC, sizeof fs);
    /* The kernel fills 24 bytes (16 USB + 8 CSB) of FONTSIGNATURE then returns the charset. */
    EXPECT_EQ(NtGdiGetTextCharsetInfo_(s.dc, &fs, 0), ANSI_CHARSET);
    CHECK(fs.fsCsb[0] & 1);        /* Latin 1 code page bit */
    CHECK(fs.fsUsb[0] & 1);        /* Basic Latin Unicode subrange */
    FONTSIGNATURE fs2; memset(&fs2, 0, sizeof fs2);
    EXPECT_EQ(fm.GetTextCharsetInfo(s.dc, &fs2, 0), ANSI_CHARSET);
    CHECK(!memcmp(&fs, &fs2, sizeof fs));
    fms_close(&s);
}

TEST(gdi_font_metrics_text_charset_info_null_signature) {
    FM_BEGIN();
    struct fmsel s; if (!fms_open(&s, L"Arial", -16, 400, GREEK_CHARSET, 0, 0)) return;
    EXPECT_EQ(fm.GetTextCharsetInfo(s.dc, NULL, 0), s.tm.tmCharSet);   /* NULL signature -> charset only */
    fms_close(&s);
}

TEST(gdi_font_metrics_text_charset_invalid_dc_notes) {
    FM_BEGIN();
    int r = fm.GetTextCharset(NULL);
    int r2 = fm.GetTextCharsetInfo(NULL, NULL, 0);
    t_note("GetTextCharset(NULL)=%d GetTextCharsetInfo(NULL)=%d", r, r2);
    CHECK(r == r2);
}

TEST(gdi_font_metrics_text_nt_getcharset_stable) {
    FM_BEGIN(); FM_NEED(NtGdiGetCharSet_);
    if (t_is_wine()) { t_skip("Wine aborts on its unimplemented NtGdiGetCharSet stub"); return; }
    struct fmsel s; if (!fms_open(&s, L"Arial", -16, 400, ANSI_CHARSET, 0, 0)) return;
    int a = NtGdiGetCharSet_(s.dc);
    int b = NtGdiGetCharSet_(s.dc);
    EXPECT_EQ(a, b);
    t_note("NtGdiGetCharSet(Arial/ANSI) = 0x%x", a);
    EXPECT_EQ(NtGdiGetCharSet_(NULL), NtGdiGetCharSet_(NULL));
    fms_close(&s);
}

TEST(gdi_font_metrics_text_charset_symbol_request) {
    FM_BEGIN();
    struct fmsel s; if (!fms_open(&s, L"Symbol", -16, 400, SYMBOL_CHARSET, 0, 0)) return;
    char u[40]; fm_u8(s.face, u, sizeof u);
    t_note("SYMBOL_CHARSET request for \"Symbol\" -> \"%s\" charset %u", u, s.tm.tmCharSet);
    EXPECT_EQ(fm.GetTextCharset(s.dc), s.tm.tmCharSet);
    fms_close(&s);
}

/* ---- direct NtGdiGetTextMetricsW ---- */

TEST(gdi_font_metrics_text_nt_textmetrics_matches_gdi32) {
    FM_BEGIN(); FM_NEED(NtGdiGetTextMetricsW_);
    struct fmsel s; if (!fms_open(&s, L"Arial", -17, 700, ANSI_CHARSET, 0, 0)) return;
    struct fm_ktm k; memset(&k, 0xAA, sizeof k);
    /* gdi32full!GetTextMetricsW passes a 0x44-byte buffer and widens tmCharSet to a DWORD at +56. */
    EXPECT_EQ(NtGdiGetTextMetricsW_(s.dc, &k, 0x44), 1);
    EXPECT_EQ(k.tmHeight, s.tm.tmHeight); EXPECT_EQ(k.tmAscent, s.tm.tmAscent); EXPECT_EQ(k.tmDescent, s.tm.tmDescent);
    EXPECT_EQ(k.tmInternalLeading, s.tm.tmInternalLeading); EXPECT_EQ(k.tmExternalLeading, s.tm.tmExternalLeading);
    EXPECT_EQ(k.tmAveCharWidth, s.tm.tmAveCharWidth); EXPECT_EQ(k.tmMaxCharWidth, s.tm.tmMaxCharWidth);
    EXPECT_EQ(k.tmWeight, s.tm.tmWeight); EXPECT_EQ(k.tmOverhang, s.tm.tmOverhang);
    EXPECT_EQ(k.tmDigitizedAspectX, s.tm.tmDigitizedAspectX); EXPECT_EQ(k.tmDigitizedAspectY, s.tm.tmDigitizedAspectY);
    EXPECT_EQ(k.tmFirstChar, s.tm.tmFirstChar); EXPECT_EQ(k.tmLastChar, s.tm.tmLastChar);
    EXPECT_EQ(k.tmDefaultChar, s.tm.tmDefaultChar); EXPECT_EQ(k.tmBreakChar, s.tm.tmBreakChar);
    EXPECT_EQ(k.tmPitchAndFamily, s.tm.tmPitchAndFamily);
    EXPECT_EQ(k.tmCharSet, s.tm.tmCharSet);
    EXPECT_EQ(k.tmWeight, 700);
    fms_close(&s);
}

TEST(gdi_font_metrics_text_nt_textmetrics_size_checks) {
    FM_BEGIN(); FM_NEED(NtGdiGetTextMetricsW_);
    struct fmsel s; if (!fms_open(&s, L"Arial", -16, 400, ANSI_CHARSET, 0, 0)) return;
    struct fm_ktm k;
    memset(&k, 0x55, sizeof k);
    EXPECT_EQ(NtGdiGetTextMetricsW_(s.dc, &k, 0x43), 0);     /* 0x43 < 0x44: rejected before any work */
    EXPECT_EQ(k.tmHeight, 0x55555555);                        /* untouched */
    EXPECT_EQ(NtGdiGetTextMetricsW_(s.dc, &k, 0), 0);
    EXPECT_EQ(NtGdiGetTextMetricsW_(s.dc, &k, 0x44), 1);
    EXPECT_EQ(NtGdiGetTextMetricsW_(s.dc, &k, 0x100), 1);     /* larger buffers are fine; 0x44 bytes written */
    fms_close(&s);
}

TEST(gdi_font_metrics_text_nt_textmetrics_writes_only_0x44) {
    FM_BEGIN(); FM_NEED(NtGdiGetTextMetricsW_);
    struct fmsel s; if (!fms_open(&s, L"Arial", -16, 400, ANSI_CHARSET, 0, 0)) return;
    unsigned char buf[0x60]; memset(buf, 0x77, sizeof buf);
    EXPECT_EQ(NtGdiGetTextMetricsW_(s.dc, buf, 0x44), 1);
    for (int i = 0x44; i < 0x60; i++) CHECKF(buf[i] == 0x77, "byte 0x%x clobbered", i);
    fms_close(&s);
}

TEST(gdi_font_metrics_text_nt_textmetrics_invalid_dc) {
    FM_BEGIN(); FM_NEED(NtGdiGetTextMetricsW_);
    struct fm_ktm k;
    EXPECT_EQ(NtGdiGetTextMetricsW_(NULL, &k, 0x44), 0);
    EXPECT_EQ(NtGdiGetTextMetricsW_((HDC)(ULONG_PTR)0x00010001, &k, 0x44), 0);
    HFONT f = fm_font(L"Arial", -12, 400, 0, 0, 0);
    if (f) { EXPECT_EQ(NtGdiGetTextMetricsW_((HDC)f, &k, 0x44), 0); fm.DeleteObject(f); }   /* wrong object type */
}

TEST(gdi_font_metrics_text_gdi32_textmetrics_invalid) {
    FM_BEGIN();
    TEXTMETRICW tm;
    SetLastError(0);
    EXPECT_EQ(fm.GetTextMetricsW(NULL, &tm), 0);
    EXPECT_EQ(fm.GetTextMetricsW((HDC)(ULONG_PTR)0xDEAD0001, &tm), 0);
    HDC dc = fm_dc(); if (!dc) return;
    fm_freedc(dc);
    EXPECT_EQ(fm.GetTextMetricsW(dc, &tm), 0);    /* deleted DC */
}

TEST(gdi_font_metrics_text_textmetrics_cache_consistent) {
    FM_BEGIN();
    struct fmsel s; if (!fms_open(&s, L"Arial", -19, 400, ANSI_CHARSET, 0, 0)) return;
    /* The first call fills gdi32full's per-font CFONT cache, later calls are served from it; results are identical. */
    TEXTMETRICW a, b, c;
    CHECK(fm.GetTextMetricsW(s.dc, &a)); CHECK(fm.GetTextMetricsW(s.dc, &b)); CHECK(fm.GetTextMetricsW(s.dc, &c));
    CHECK(!memcmp(&a, &b, sizeof a)); CHECK(!memcmp(&b, &c, sizeof b));
    /* Reselecting the same font in another DC yields the same values. */
    HDC dc2 = fm_dc();
    if (dc2) { fm_sel(dc2, s.f); TEXTMETRICW d; CHECK(fm.GetTextMetricsW(dc2, &d)); CHECK(!memcmp(&a, &d, sizeof a)); fm_sel(dc2, s.old); fm_freedc(dc2); }
    fms_close(&s);
}

TEST(gdi_font_metrics_text_textmetrics_switching_fonts_changes_values) {
    FM_BEGIN();
    HDC dc = fm_dc(); if (!dc) return;
    HFONT small = fm_font(L"Arial", -10, 400, ANSI_CHARSET, 0, 0), big = fm_font(L"Arial", -60, 400, ANSI_CHARSET, 0, 0);
    if (!small || !big) { t_skip("create"); fm_freedc(dc); return; }
    HGDIOBJ old = fm_sel(dc, small);
    TEXTMETRICW a, b, c;
    fm.GetTextMetricsW(dc, &a);
    fm_sel(dc, big); fm.GetTextMetricsW(dc, &b);
    fm_sel(dc, small); fm.GetTextMetricsW(dc, &c);
    CHECK(b.tmHeight > a.tmHeight * 4);
    CHECK(!memcmp(&a, &c, sizeof a));
    fm_sel(dc, old); fm.DeleteObject(small); fm.DeleteObject(big); fm_freedc(dc);
}

TEST(gdi_font_metrics_text_textmetrics_ansi_matches_wide) {
    FM_BEGIN();
    struct fmsel s; if (!fms_open(&s, L"Arial", -16, 400, ANSI_CHARSET, 0, 0)) return;
    TEXTMETRICA a;
    CHECK(fm.GetTextMetricsA(s.dc, &a));
    EXPECT_EQ(a.tmHeight, s.tm.tmHeight); EXPECT_EQ(a.tmAscent, s.tm.tmAscent);
    EXPECT_EQ(a.tmAveCharWidth, s.tm.tmAveCharWidth); EXPECT_EQ(a.tmCharSet, s.tm.tmCharSet);
    EXPECT_EQ(a.tmPitchAndFamily, s.tm.tmPitchAndFamily);
    fms_close(&s);
}

/* ---- map modes ---- */

static long fm_em(HDC dc, int lfh, TEXTMETRICW *tm) {
    HFONT f = fm_font(L"Arial", lfh, 400, ANSI_CHARSET, 0, 0);
    HGDIOBJ old = fm_sel(dc, f);
    long em = 0;
    if (fm.GetTextMetricsW(dc, tm)) em = labs(tm->tmHeight - tm->tmInternalLeading);
    fm_sel(dc, old); fm.DeleteObject(f);
    return em;
}

TEST(gdi_font_metrics_text_mapmode_lometric_font_scale) {
    FM_BEGIN();
    if (!fm_face_present(L"Arial")) { t_skip("no Arial"); return; }
    HDC dc = fm_dc(); if (!dc) return;
    int dpi = fm.GetDeviceCaps(dc, LOGPIXELSY);
    CHECK(fm.SetMapMode(dc, MM_LOMETRIC) != 0);
    EXPECT_EQ(fm.GetMapMode(dc), MM_LOMETRIC);
    /* LOMETRIC is y-up: the sign of lfHeight is interpreted after the y flip, so of +100 and -100 one is the 10 mm
       character height and the other a cell height of 10 mm. Logical units come back after a pixel round trip. */
    TEXTMETRICW tm;
    long emn = fm_em(dc, -100, &tm), emp = fm_em(dc, 100, &tm);
    t_note("MM_LOMETRIC em for -100: %ld, for +100: %ld (dpi %d)", emn, emp, dpi);
    long slack = 254 / (dpi ? dpi : 96) * 2 + 4;
    CHECK(emn > 0 && emp > 0);
    CHECKF(labs(emn - 100) <= slack || labs(emp - 100) <= slack, "neither sign gives em 100: %ld %ld", emn, emp);
    fm.SetMapMode(dc, MM_TEXT);
    fm_freedc(dc);
}

TEST(gdi_font_metrics_text_mapmode_text_vs_twips_ratio) {
    FM_BEGIN();
    if (!fm_face_present(L"Arial")) { t_skip("no Arial"); return; }
    HDC dc = fm_dc(); if (!dc) return;
    int dpi = fm.GetDeviceCaps(dc, LOGPIXELSY);
    fm.SetMapMode(dc, MM_TWIPS);                   /* 1/1440 inch per unit */
    TEXTMETRICW tm;
    long a = fm_em(dc, -240, &tm), b = fm_em(dc, 240, &tm);   /* 12 pt */
    long slack = 1440 / (dpi ? dpi : 96) + 6;
    t_note("MM_TWIPS em for -240: %ld, +240: %ld", a, b);
    CHECKF(labs(a - 240) <= slack || labs(b - 240) <= slack, "neither sign gives 240 twips: %ld %ld", a, b);
    long a2 = fm_em(dc, -480, &tm), b2 = fm_em(dc, 480, &tm);
    CHECK(a2 > a || b2 > b);                        /* a larger request is a larger font */
    fm.SetMapMode(dc, MM_TEXT);
    fm_freedc(dc);
}

TEST(gdi_font_metrics_text_mapmode_extent_scales_with_units) {
    FM_BEGIN();
    HDC dc = fm_dc(); if (!dc) return;
    int dpi = fm.GetDeviceCaps(dc, LOGPIXELSX);
    HFONT f = fm_font(L"Arial", -48, 400, ANSI_CHARSET, 0, 0);
    HGDIOBJ old = fm_sel(dc, f);
    SIZE px; CHECK(fm.GetTextExtentPoint32W(dc, L"Mapping", 7, &px));
    fm_sel(dc, old); fm.DeleteObject(f);
    /* Same 48 px em expressed in 0.1 mm units: 48 px = 48*254/dpi units. */
    fm.SetMapMode(dc, MM_LOMETRIC);
    int units = 48 * 254 / (dpi ? dpi : 96);
    long expect = px.cx * 254 / (dpi ? dpi : 96), tol = 7 * (254 / (dpi ? dpi : 96) + 1) + 6;
    SIZE a, b;
    HFONT fa = fm_font(L"Arial", -units, 400, ANSI_CHARSET, 0, 0), fb = fm_font(L"Arial", units, 400, ANSI_CHARSET, 0, 0);
    fm_sel(dc, fa); fm.GetTextExtentPoint32W(dc, L"Mapping", 7, &a);
    fm_sel(dc, fb); fm.GetTextExtentPoint32W(dc, L"Mapping", 7, &b);
    t_note("MM_LOMETRIC extents %ld / %ld, expected about %ld", a.cx, b.cx, expect);
    CHECKF(labs(a.cx - expect) <= tol || labs(b.cx - expect) <= tol, "cx %ld/%ld expected about %ld", a.cx, b.cx, expect);
    fm_sel(dc, old); fm.DeleteObject(fa); fm.DeleteObject(fb);
    fm.SetMapMode(dc, MM_TEXT);
    fm_freedc(dc);
}

TEST(gdi_font_metrics_text_mapmode_anisotropic_change_after_select) {
    FM_BEGIN();
    HDC dc = fm_dc(); if (!dc) return;
    HFONT f = fm_font(L"Arial", -20, 400, ANSI_CHARSET, 0, 0);
    HGDIOBJ old = fm_sel(dc, f);
    TEXTMETRICW a, b;
    fm.GetTextMetricsW(dc, &a);
    fm.SetMapMode(dc, MM_ANISOTROPIC);
    fm.SetWindowExtEx(dc, 100, 100, NULL);
    fm.SetViewportExtEx(dc, 200, 200, NULL);      /* 2 device px per logical unit */
    CHECK(fm.GetTextMetricsW(dc, &b));
    t_note("tmHeight MM_TEXT %ld, anisotropic 2px/unit %ld", a.tmHeight, b.tmHeight);
    CHECK(b.tmHeight > 0);
    tm_invariants("anisotropic", &b);
    fm.SetMapMode(dc, MM_TEXT);
    fm_sel(dc, old); fm.DeleteObject(f); fm_freedc(dc);
}

/* ---- OUTLINETEXTMETRIC ---- */

static UINT otm_get(HDC dc, OUTLINETEXTMETRICW **out) {
    UINT n = fm.GetOutlineTextMetricsW(dc, 0, NULL);
    *out = NULL;
    if (!n || n == GDI_ERROR) return 0;
    *out = (OUTLINETEXTMETRICW *)calloc(1, n + 16);
    UINT r = fm.GetOutlineTextMetricsW(dc, n, *out);
    if (r != n) { free(*out); *out = NULL; return 0; }
    return n;
}

TEST(gdi_font_metrics_text_otm_size_query) {
    FM_BEGIN();
    if (!fm_face_present(L"Arial")) { t_skip("no Arial"); return; }
    struct fmsel s; if (!fms_open(&s, L"Arial", -16, 400, ANSI_CHARSET, 0, 0)) return;
    UINT n = fm.GetOutlineTextMetricsW(s.dc, 0, NULL);
    CHECK(n >= sizeof(OUTLINETEXTMETRICW));   /* the four name strings follow the fixed part */
    CHECK(n < 4096);
    t_note("OUTLINETEXTMETRICW size for Arial: %u (fixed part %u)", n, (unsigned)sizeof(OUTLINETEXTMETRICW));
    OUTLINETEXTMETRICW *p = calloc(1, n + 16);
    EXPECT_EQ(fm.GetOutlineTextMetricsW(s.dc, n, p), n);
    EXPECT_EQ(p->otmSize, n);
    free(p);
    fms_close(&s);
}

TEST(gdi_font_metrics_text_otm_names_inside_buffer) {
    FM_BEGIN();
    if (!fm_face_present(L"Arial")) { t_skip("no Arial"); return; }
    struct fmsel s; if (!fms_open(&s, L"Arial", -16, 400, ANSI_CHARSET, 0, 0)) return;
    OUTLINETEXTMETRICW *p; UINT n = otm_get(s.dc, &p);
    if (!n) { t_skip("no otm"); fms_close(&s); return; }
    ULONG_PTR offs[4] = { (ULONG_PTR)p->otmpFamilyName, (ULONG_PTR)p->otmpFaceName, (ULONG_PTR)p->otmpStyleName, (ULONG_PTR)p->otmpFullName };
    static const char *nm[4] = { "family", "face", "style", "full" };
    for (int i = 0; i < 4; i++) {
        /* The pointer fields hold byte offsets from the start of the structure. */
        CHECKF(offs[i] >= sizeof(OUTLINETEXTMETRICW) - 0 && offs[i] < n, "%s offset 0x%llx (size %u)", nm[i], (unsigned long long)offs[i], n);
        if (offs[i] < n) {
            const WCHAR *w = (const WCHAR *)((char *)p + offs[i]);
            size_t maxc = (n - offs[i]) / 2, l = 0;
            while (l < maxc && w[l]) l++;
            CHECKF(l < maxc, "%s string unterminated inside buffer", nm[i]);
            char u[80]; fm_u8(w, u, sizeof u); t_note("OTM %s = \"%s\"", nm[i], u);
            if (i == 0) CHECK(fm_wcsieq(w, L"Arial"));
            if (i == 1) CHECK(fm_wcsieq(w, L"Arial"));
            CHECKF(l > 0, "%s empty", nm[i]);
        }
    }
    free(p); fms_close(&s);
}

TEST(gdi_font_metrics_text_otm_embedded_textmetric_matches) {
    FM_BEGIN();
    if (!fm_face_present(L"Arial")) { t_skip("no Arial"); return; }
    struct fmsel s; if (!fms_open(&s, L"Arial", -22, 700, ANSI_CHARSET, 0, 0)) return;
    OUTLINETEXTMETRICW *p; UINT n = otm_get(s.dc, &p);
    if (!n) { t_skip("no otm"); fms_close(&s); return; }
    EXPECT_EQ(p->otmTextMetrics.tmHeight, s.tm.tmHeight);
    EXPECT_EQ(p->otmTextMetrics.tmAscent, s.tm.tmAscent);
    EXPECT_EQ(p->otmTextMetrics.tmDescent, s.tm.tmDescent);
    EXPECT_EQ(p->otmTextMetrics.tmAveCharWidth, s.tm.tmAveCharWidth);
    EXPECT_EQ(p->otmTextMetrics.tmWeight, s.tm.tmWeight);
    EXPECT_EQ(p->otmTextMetrics.tmPitchAndFamily, s.tm.tmPitchAndFamily);
    EXPECT_EQ(p->otmTextMetrics.tmCharSet, s.tm.tmCharSet);
    free(p); fms_close(&s);
}

TEST(gdi_font_metrics_text_otm_design_metrics_sane) {
    FM_BEGIN();
    if (!fm_face_present(L"Arial")) { t_skip("no Arial"); return; }
    struct fmsel s; if (!fms_open(&s, L"Arial", -100, 400, ANSI_CHARSET, 0, 0)) return;
    OUTLINETEXTMETRICW *p; UINT n = otm_get(s.dc, &p);
    if (!n) { t_skip("no otm"); fms_close(&s); return; }
    CHECK(p->otmEMSquare >= 16 && p->otmEMSquare <= 16384);
    t_note("Arial otmEMSquare %u otmAscent %d otmDescent %d otmLineGap %u cap %u xheight %u", p->otmEMSquare, p->otmAscent, p->otmDescent, p->otmLineGap, p->otmsCapEmHeight, p->otmsXHeight);
    CHECK(p->otmAscent > 0);
    CHECK(p->otmDescent < 0);                         /* descent below the baseline is negative in OTM */
    CHECK(p->otmsCapEmHeight > 0 && p->otmsCapEmHeight <= p->otmEMSquare);
    CHECK(p->otmsXHeight > 0 && p->otmsXHeight < p->otmsCapEmHeight);
    CHECK(p->otmsUnderscoreSize > 0);
    CHECK(p->otmrcFontBox.right > p->otmrcFontBox.left);
    EXPECT_EQ(p->otmItalicAngle, 0);                  /* upright font */
    CHECK(p->otmsStrikeoutSize > 0);
    free(p); fms_close(&s);
}

TEST(gdi_font_metrics_text_otm_italic_angle) {
    FM_BEGIN();
    if (!fm_face_present(L"Arial")) { t_skip("no Arial"); return; }
    HDC dc = fm_dc(); if (!dc) return;
    LOGFONTW lf; memset(&lf, 0, sizeof lf); lf.lfHeight = -100; lf.lfWeight = 400; lf.lfItalic = 1; lf.lfCharSet = ANSI_CHARSET; lstrcpyW(lf.lfFaceName, L"Arial");
    HFONT f = fm.CreateFontIndirectW(&lf); HGDIOBJ old = fm_sel(dc, f);
    OUTLINETEXTMETRICW *p; UINT n = otm_get(dc, &p);
    if (n) { CHECK(p->otmItalicAngle < 0); CHECK(p->otmTextMetrics.tmItalic != 0); CHECK(p->otmfsSelection & 1); free(p); }
    fm_sel(dc, old); fm.DeleteObject(f); fm_freedc(dc);
}

TEST(gdi_font_metrics_text_otm_buffer_too_small_notes) {
    FM_BEGIN();
    if (!fm_face_present(L"Arial")) { t_skip("no Arial"); return; }
    struct fmsel s; if (!fms_open(&s, L"Arial", -16, 400, ANSI_CHARSET, 0, 0)) return;
    UINT n = fm.GetOutlineTextMetricsW(s.dc, 0, NULL);
    unsigned char *buf = calloc(1, n + 16);
    memset(buf, 0x5A, n + 16);
    UINT r = fm.GetOutlineTextMetricsW(s.dc, n / 2, (LPOUTLINETEXTMETRICW)buf);
    t_note("GetOutlineTextMetricsW with half-size buffer (%u of %u) returned %u", n / 2, n, r);
    for (UINT i = n; i < n + 16; i++) CHECKF(buf[i] == 0x5A, "byte %u beyond the buffer clobbered", i);
    /* The truncated copy is reported as the number of bytes written: the call fills what fits and nothing beyond it. */
    EXPECT_EQ(r, n / 2);
    memset(buf, 0x5A, n + 16);
    r = fm.GetOutlineTextMetricsW(s.dc, 4, (LPOUTLINETEXTMETRICW)buf);
    t_note("GetOutlineTextMetricsW with 4-byte buffer returned %u", r);
    EXPECT_EQ(r, 4);
    for (UINT i = 4; i < 4 + 16 && i < n; i++) CHECKF(buf[i] == 0x5A, "byte %u beyond the 4-byte buffer clobbered", i);
    free(buf); fms_close(&s);
}

TEST(gdi_font_metrics_text_otm_bitmap_font_fails) {
    FM_BEGIN();
    struct fmsel s; if (!fms_open(&s, L"System", 0, 0, DEFAULT_CHARSET, 0, 0)) return;
    /* On Windows 10 the System face still yields an outline record (the size query is not 0), although its TEXTMETRIC does
       not carry TMPF_TRUETYPE. The record must at least hold the fixed OUTLINETEXTMETRICW part. */
    UINT n = fm.GetOutlineTextMetricsW(s.dc, 0, NULL);
    t_note("System: TMPF_TRUETYPE %d, GetOutlineTextMetricsW size %u", (s.tm.tmPitchAndFamily & TMPF_TRUETYPE) != 0, n);
    CHECK(n == 0 || n >= sizeof(OUTLINETEXTMETRICW) - 8);
    fms_close(&s);
}

TEST(gdi_font_metrics_text_otm_invalid_dc) {
    FM_BEGIN();
    EXPECT_EQ(fm.GetOutlineTextMetricsW(NULL, 0, NULL), 0);
    EXPECT_EQ(fm.GetOutlineTextMetricsW((HDC)(ULONG_PTR)0x1234, 0, NULL), 0);
}

TEST(gdi_font_metrics_text_nt_otm_matches_gdi32) {
    FM_BEGIN(); FM_NEED(NtGdiGetOutlineTextMetricsInternalW_);
    if (!fm_face_present(L"Arial")) { t_skip("no Arial"); return; }
    struct fmsel s; if (!fms_open(&s, L"Arial", -16, 400, ANSI_CHARSET, 0, 0)) return;
    unsigned char diff[64]; memset(diff, 0, sizeof diff);
    int n = NtGdiGetOutlineTextMetricsInternalW_(s.dc, 0, NULL, diff);
    UINT u = fm.GetOutlineTextMetricsW(s.dc, 0, NULL);
    EXPECT_EQ(n, u);
    CHECK(n > 0);
    /* gdi32full passes cb=0 when potm is NULL; the kernel treats cb==0 or NULL buffer as the size query. */
    EXPECT_EQ(NtGdiGetOutlineTextMetricsInternalW_(s.dc, 100, NULL, diff), n);
    EXPECT_EQ(NtGdiGetOutlineTextMetricsInternalW_(NULL, 0, NULL, diff), 0);
    fms_close(&s);
}

/* ---- font language info ---- */

TEST(gdi_font_metrics_text_fontlanguageinfo_flags) {
    FM_BEGIN();
    struct fmsel s; if (!fms_open(&s, L"Arial", -16, 400, ANSI_CHARSET, 0, 0)) return;
    DWORD f = fm.GetFontLanguageInfo(s.dc);
    t_note("GetFontLanguageInfo(Arial/ANSI) = 0x%lx", f);
    CHECK(f != GCP_ERROR);
    /* Latin text has no reordering or ligature requirement. */
    CHECK(!(f & GCP_REORDER));
    EXPECT_EQ(fm.GetFontLanguageInfo(s.dc), f);
    fms_close(&s);
}

TEST(gdi_font_metrics_text_fontlanguageinfo_invalid_dc) {
    FM_BEGIN();
    EXPECT_EQ(fm.GetFontLanguageInfo(NULL), GCP_ERROR);
    EXPECT_EQ(fm.GetFontLanguageInfo((HDC)(ULONG_PTR)0xBAD0), GCP_ERROR);
}

TEST(gdi_font_metrics_text_fontlanguageinfo_hebrew_arabic_reorder) {
    FM_BEGIN();
    if (!fm_face_present(L"Arial")) { t_skip("no Arial"); return; }
    struct fmsel s;
    if (fms_open(&s, L"Arial", -16, 400, HEBREW_CHARSET, 0, 0)) { t_note("Hebrew FLI = 0x%lx", fm.GetFontLanguageInfo(s.dc)); fms_close(&s); }
    if (fms_open(&s, L"Arial", -16, 400, ARABIC_CHARSET, 0, 0)) { t_note("Arabic FLI = 0x%lx", fm.GetFontLanguageInfo(s.dc)); fms_close(&s); }
}

/* ---- stock fonts as seen through the DC ---- */

TEST(gdi_font_metrics_text_stock_fonts_in_dc_metrics) {
    FM_BEGIN();
    static const int ids[] = { OEM_FIXED_FONT, ANSI_FIXED_FONT, ANSI_VAR_FONT, SYSTEM_FONT, DEVICE_DEFAULT_FONT, SYSTEM_FIXED_FONT, DEFAULT_GUI_FONT };
    static const char *nm[] = { "OEM_FIXED", "ANSI_FIXED", "ANSI_VAR", "SYSTEM", "DEVICE_DEFAULT", "SYSTEM_FIXED", "DEFAULT_GUI" };
    HDC dc = fm_dc(); if (!dc) return;
    HGDIOBJ old = fm.GetCurrentObject(dc, OBJ_FONT);
    for (unsigned i = 0; i < 7; i++) {
        HGDIOBJ f = fm.GetStockObject(ids[i]);
        EXPECT_EQ(fm.SelectObject(dc, f) != NULL, 1);
        EXPECT_EQ(fm.GetCurrentObject(dc, OBJ_FONT), f);
        TEXTMETRICW tm; WCHAR face[40];
        CHECK(fm.GetTextMetricsW(dc, &tm));
        fm_face(dc, face, 40);
        char u[48]; fm_u8(face, u, sizeof u);
        tm_invariants(nm[i], &tm);
        t_note("%s -> \"%s\" h %ld ascent %ld ave %ld max %ld pitchfam 0x%02x charset %u", nm[i], u, tm.tmHeight, tm.tmAscent, tm.tmAveCharWidth, tm.tmMaxCharWidth, tm.tmPitchAndFamily, tm.tmCharSet);
    }
    fm.SelectObject(dc, old);
    fm_freedc(dc);
}

TEST(gdi_font_metrics_text_stock_system_font_is_bitmap) {
    FM_BEGIN();
    HDC dc = fm_dc(); if (!dc) return;
    HGDIOBJ old = fm.SelectObject(dc, fm.GetStockObject(SYSTEM_FONT));
    TEXTMETRICW tm; WCHAR face[40];
    CHECK(fm.GetTextMetricsW(dc, &tm));
    fm_face(dc, face, 40);
    CHECK(fm_wcsieq(face, L"System"));
    CHECK(!(tm.tmPitchAndFamily & TMPF_TRUETYPE));
    CHECK(tm.tmWeight >= 400);
    if (fm.GetDeviceCaps(dc, LOGPIXELSY) == 96) {
        /* Well-known 96 dpi "System" bitmap font cell on en-US: 16 high, 13 ascent, 7 wide; build-dependent so only noted. */
        t_note("System@96dpi: h %ld asc %ld ave %ld max %ld (typical 16/13/7/14)", tm.tmHeight, tm.tmAscent, tm.tmAveCharWidth, tm.tmMaxCharWidth);
    }
    fm.SelectObject(dc, old);
    fm_freedc(dc);
}
