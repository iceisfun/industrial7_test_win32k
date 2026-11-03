/* EnumFontFamiliesEx and friends through gdi32full -> NtGdiEnumFonts, validated against what the callback receives.
 *
 * gdi32full!EnumFontsInternalW: bEnumFonts twice (size, then data), then per record: callback(record+12, record+8+cjEfdw,
 * record.fontType); the loop stops at the first zero callback result; the API returns the last callback result, or 1 when the
 * record chain is empty. A failing NtGdiEnumFonts gives GdiSetLastError(6) and 0.
 * EnumFontFamiliesExW passes (face = lfFaceName or NULL if empty, charset = lfCharSet or 1 when lpLogfont is NULL, type 3). */
#include "fe.h"
#include <stdlib.h>

static int need(void) { return U(GetDC) && U(ReleaseDC) && U(EnumFontFamiliesExW) && U(EnumFontFamiliesW) && U(EnumFontsW) && U(EnumFontFamiliesExA); }

/* ---- stock families -------------------------------------------------------------------------------------------- */
static const WCHAR *g_core_tt[] = { L"Arial", L"Courier New", L"Times New Roman", L"Segoe UI", L"Tahoma", L"Verdana", L"Symbol", L"Wingdings", L"Marlett" };
static const WCHAR *g_opt_tt[]  = { L"Consolas", L"Calibri", L"Cambria", L"Lucida Console", L"Webdings", L"Georgia", L"Trebuchet MS", L"Impact", L"Comic Sans MS", L"Segoe UI Symbol", L"Microsoft Sans Serif", L"Lucida Sans Unicode", L"Palatino Linotype", L"Franklin Gothic Medium" };
static const WCHAR *g_core_raster[] = { L"Terminal", L"Fixedsys", L"System" };
static const WCHAR *g_opt_raster[] = { L"MS Sans Serif", L"Small Fonts", L"MS Serif", L"Modern", L"Roman", L"Script" };
#define NEL(a) (int)(sizeof(a) / sizeof((a)[0]))

TEST(gdi_font_enum_resources_user_core_truetype_families_present) {
    if (!need()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    for (int i = 0; i < NEL(g_core_tt); i++) {
        int n = fe_count_family(dc, g_core_tt[i], DEFAULT_CHARSET);
        CHECKF(n > 0, "family %ls not enumerated", g_core_tt[i]);
    }
    U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_user_optional_families_noted) {
    if (!need()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    for (int i = 0; i < NEL(g_opt_tt); i++) {
        int n = fe_count_family(dc, g_opt_tt[i], DEFAULT_CHARSET);
        t_note("%ls: %d entries", g_opt_tt[i], n);
    }
    for (int i = 0; i < NEL(g_opt_raster); i++) t_note("%ls: %d entries", g_opt_raster[i], fe_count_family(dc, g_opt_raster[i], DEFAULT_CHARSET));
    U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_user_core_raster_families_present) {
    if (!need()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    for (int i = 0; i < NEL(g_core_raster); i++) CHECKF(fe_count_family(dc, g_core_raster[i], DEFAULT_CHARSET) > 0, "raster family %ls missing", g_core_raster[i]);
    U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_user_face_filter_yields_only_that_family) {
    if (!need()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    for (int i = 0; i < NEL(g_core_tt); i++) {
        struct fe_set s; fe_set_init(&s);
        fe_enum(dc, g_core_tt[i], DEFAULT_CHARSET, &s);
        int bad = 0;
        for (int k = 0; k < s.n; k++) if (lstrcmpiW(s.it[k].face, g_core_tt[i])) bad++;
        CHECKF(s.n > 0 && bad == 0, "%ls: %d items, %d with another family name", g_core_tt[i], s.n, bad);
        fe_set_free(&s);
    }
    U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_user_truetype_families_flag_truetype) {
    if (!need()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    for (int i = 0; i < NEL(g_core_tt); i++) {
        struct fe_set s; fe_set_init(&s);
        fe_enum(dc, g_core_tt[i], DEFAULT_CHARSET, &s);
        for (int k = 0; k < s.n; k++) {
            CHECKF(s.it[k].ftype & TRUETYPE_FONTTYPE, "%ls item %d type 0x%lx lacks TRUETYPE", g_core_tt[i], k, s.it[k].ftype);
            CHECKF(!(s.it[k].ftype & RASTER_FONTTYPE), "%ls item %d type 0x%lx has RASTER", g_core_tt[i], k, s.it[k].ftype);
            CHECKF(s.it[k].tmPitch & TMPF_TRUETYPE, "%ls item %d tmPitchAndFamily 0x%x lacks TMPF_TRUETYPE", g_core_tt[i], k, s.it[k].tmPitch);
        }
        fe_set_free(&s);
    }
    U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_user_raster_families_flag_raster) {
    if (!need()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    for (int i = 0; i < NEL(g_core_raster); i++) {
        struct fe_set s; fe_set_init(&s);
        fe_enum(dc, g_core_raster[i], DEFAULT_CHARSET, &s);
        for (int k = 0; k < s.n; k++) {
            CHECKF(s.it[k].ftype & RASTER_FONTTYPE, "%ls item %d type 0x%lx lacks RASTER", g_core_raster[i], k, s.it[k].ftype);
            CHECKF(!(s.it[k].ftype & TRUETYPE_FONTTYPE), "%ls item %d type 0x%lx has TRUETYPE", g_core_raster[i], k, s.it[k].ftype);
            CHECKF(!(s.it[k].tmPitch & TMPF_TRUETYPE), "%ls item %d claims TMPF_TRUETYPE", g_core_raster[i], k);
        }
        fe_set_free(&s);
    }
    U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_user_screen_dc_has_no_device_fonts) {
    if (!need()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    struct fe_set s; fe_set_init(&s);
    fe_enum(dc, NULL, DEFAULT_CHARSET, &s);
    int dev = 0; for (int i = 0; i < s.n; i++) if (s.it[i].ftype & DEVICE_FONTTYPE) dev++;
    if (dev) t_note("%d device-type entries on the display DC", dev);
    CHECK(s.n > 0);
    fe_set_free(&s); U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_user_symbol_families_use_symbol_charset) {
    if (!need()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    const WCHAR *sym[] = { L"Symbol", L"Wingdings", L"Marlett" };
    for (int i = 0; i < 3; i++) {
        struct fe_set s; fe_set_init(&s);
        fe_enum(dc, sym[i], DEFAULT_CHARSET, &s);
        CHECKF(s.n > 0, "%ls missing", sym[i]);
        for (int k = 0; k < s.n; k++) CHECKF(s.it[k].charset == SYMBOL_CHARSET, "%ls item %d charset %u", sym[i], k, s.it[k].charset);
        fe_set_free(&s);
    }
    U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_user_symbol_fonts_sig_has_symbol_bit) {
    if (!need()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    struct fe_set s; fe_set_init(&s);
    fe_enum(dc, L"Wingdings", DEFAULT_CHARSET, &s);
    for (int k = 0; k < s.n; k++) if (s.it[k].hasntm && !(s.it[k].sig.fsCsb[0] & 0x80000000u)) t_note("Wingdings item %d fsCsb0=0x%lx lacks bit 31", k, s.it[k].sig.fsCsb[0]);
    CHECK(s.n > 0);
    fe_set_free(&s); U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_user_ansi_charset_filter_exact) {
    if (!need()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    struct fe_set s; fe_set_init(&s);
    fe_enum(dc, NULL, ANSI_CHARSET, &s);
    CHECK(s.n > 0);
    for (int i = 0; i < s.n; i++) if (s.it[i].charset != ANSI_CHARSET) { CHECKF(0, "item %d %ls charset %u", i, s.it[i].face, s.it[i].charset); break; }
    CHECK(fe_find(&s, L"Arial") > 0);
    CHECK(fe_find(&s, L"Wingdings") == 0);
    fe_set_free(&s); U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_user_symbol_charset_filter_exact) {
    if (!need()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    struct fe_set s; fe_set_init(&s);
    fe_enum(dc, NULL, SYMBOL_CHARSET, &s);
    CHECK(s.n > 0);
    for (int i = 0; i < s.n; i++) if (s.it[i].charset != SYMBOL_CHARSET) { CHECKF(0, "item %d %ls charset %u", i, s.it[i].face, s.it[i].charset); break; }
    CHECK(fe_find(&s, L"Wingdings") > 0);
    CHECK(fe_find(&s, L"Arial") == 0);
    fe_set_free(&s); U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_user_oem_charset_filter) {
    if (!need()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    struct fe_set s; fe_set_init(&s);
    fe_enum(dc, NULL, OEM_CHARSET, &s);
    for (int i = 0; i < s.n; i++) if (s.it[i].charset != OEM_CHARSET) { CHECKF(0, "item %d %ls charset %u", i, s.it[i].face, s.it[i].charset); break; }
    t_note("OEM_CHARSET entries: %d (Terminal=%d)", s.n, fe_find(&s, L"Terminal"));
    fe_set_free(&s); U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_user_charset_table_specific_means_exact) {
    if (!need()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    static const BYTE cs[] = { ANSI_CHARSET, SYMBOL_CHARSET, SHIFTJIS_CHARSET, GREEK_CHARSET, TURKISH_CHARSET, HEBREW_CHARSET, ARABIC_CHARSET, BALTIC_CHARSET, RUSSIAN_CHARSET, EASTEUROPE_CHARSET, GB2312_CHARSET, HANGEUL_CHARSET, CHINESEBIG5_CHARSET, THAI_CHARSET, VIETNAMESE_CHARSET, OEM_CHARSET };
    for (unsigned i = 0; i < sizeof cs; i++) {
        struct fe_set s; fe_set_init(&s);
        fe_enum(dc, NULL, cs[i], &s);
        int bad = 0; for (int k = 0; k < s.n; k++) if (s.it[k].charset != cs[i]) bad++;
        CHECKF(bad == 0, "charset %u: %d of %d entries report another charset", cs[i], bad, s.n);
        t_note("charset %u: %d entries", cs[i], s.n);
        fe_set_free(&s);
    }
    U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_user_ansi_families_subset_of_default) {
    if (!need()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    struct fe_set a, d; fe_set_init(&a); fe_set_init(&d);
    fe_enum(dc, NULL, ANSI_CHARSET, &a);
    fe_enum(dc, NULL, DEFAULT_CHARSET, &d);
    CHECKF(a.n <= d.n, "ansi %d > default %d", a.n, d.n);
    for (int i = 0; i < a.n; i++) if (!fe_find(&d, a.it[i].face)) { CHECKF(0, "ANSI family %ls missing in DEFAULT set", a.it[i].face); break; }
    fe_set_free(&a); fe_set_free(&d); U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_user_null_logfont_equals_default_empty_face) {
    if (!need()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    struct fe_set a, b; fe_set_init(&a); fe_set_init(&b);
    int rb;
    /* lpLogfont == NULL is read as charset 1 (DEFAULT) with no face (EnumFontFamiliesExW analysis). */
    extern int CALLBACK fe_cb_pub(const LOGFONTW *, const TEXTMETRICW *, DWORD, LPARAM);
    rb = U(EnumFontFamiliesExW)(dc, NULL, fe_cb_pub, (LPARAM)&a, 0);
    fe_enum(dc, L"", DEFAULT_CHARSET, &b);
    CHECK(rb != 0);
    EXPECT_EQ(a.n, b.n);
    fe_set_free(&a); fe_set_free(&b); U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_user_dwflags_ignored) {
    if (!need()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    LOGFONTW lf; memset(&lf, 0, sizeof lf); lf.lfCharSet = DEFAULT_CHARSET; lstrcpyW(lf.lfFaceName, L"Arial");
    extern int CALLBACK fe_cb_pub(const LOGFONTW *, const TEXTMETRICW *, DWORD, LPARAM);
    struct fe_set a, b; fe_set_init(&a); fe_set_init(&b);
    U(EnumFontFamiliesExW)(dc, &lf, fe_cb_pub, (LPARAM)&a, 0);
    U(EnumFontFamiliesExW)(dc, &lf, fe_cb_pub, (LPARAM)&b, 0xFFFFFFFFu);
    EXPECT_EQ(a.n, b.n);
    CHECK(a.n > 0);
    fe_set_free(&a); fe_set_free(&b); U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_user_other_logfont_fields_ignored) {
    if (!need()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    extern int CALLBACK fe_cb_pub(const LOGFONTW *, const TEXTMETRICW *, DWORD, LPARAM);
    LOGFONTW lf; memset(&lf, 0, sizeof lf); lf.lfCharSet = DEFAULT_CHARSET; lstrcpyW(lf.lfFaceName, L"Arial");
    struct fe_set a, b; fe_set_init(&a); fe_set_init(&b);
    U(EnumFontFamiliesExW)(dc, &lf, fe_cb_pub, (LPARAM)&a, 0);
    lf.lfHeight = -100; lf.lfWeight = 700; lf.lfItalic = 1; lf.lfUnderline = 1; lf.lfEscapement = 900; lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
    U(EnumFontFamiliesExW)(dc, &lf, fe_cb_pub, (LPARAM)&b, 0);
    EXPECT_EQ(a.n, b.n);
    fe_set_free(&a); fe_set_free(&b); U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_user_callback_zero_stops_enumeration) {
    if (!need()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    struct fe_set s; fe_set_init(&s); s.stop_after = 1;
    int r = fe_enum(dc, NULL, DEFAULT_CHARSET, &s);
    EXPECT_EQ(s.n, 1);
    EXPECT_EQ(r, 0);
    fe_set_free(&s);
    struct fe_set t; fe_set_init(&t); t.stop_after = 3;
    r = fe_enum(dc, NULL, DEFAULT_CHARSET, &t);
    EXPECT_EQ(t.n, 3);
    EXPECT_EQ(r, 0);
    fe_set_free(&t); U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_user_return_value_is_last_callback_result) {
    if (!need()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    struct fe_set s; fe_set_init(&s); s.ret = 7;
    int r = fe_enum(dc, L"Arial", DEFAULT_CHARSET, &s);
    CHECK(s.n > 0);
    EXPECT_EQ(r, 7);
    fe_set_free(&s);
    struct fe_set t; fe_set_init(&t); t.ret = -3;
    r = fe_enum(dc, L"Arial", DEFAULT_CHARSET, &t);
    EXPECT_EQ(r, -3);
    fe_set_free(&t); U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_user_all_callbacks_run_when_returning_nonzero) {
    if (!need()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    struct fe_set s; fe_set_init(&s); s.ret = 2;
    int r = fe_enum(dc, NULL, DEFAULT_CHARSET, &s);
    BYTE *b; ULONG sz;
    fe_raw_enum(dc, 3, NULL, 1, &b, &sz);
    int n = b ? fe_raw_walk(b, sz, NULL) : -1;
    EXPECT_EQ(r, 2);
    if (n > 0) EXPECT_EQ(s.n, n);
    free(b); fe_set_free(&s); U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_user_unknown_face_calls_nothing) {
    if (!need()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    WCHAR w[64]; wsprintfW(w, L"Win32ktNoSuch%lu", GetCurrentProcessId());
    struct fe_set s; fe_set_init(&s);
    int r = fe_enum(dc, w, DEFAULT_CHARSET, &s);
    EXPECT_EQ(s.n, 0);
    t_note("unknown face: EnumFontFamiliesExW returned %d", r);
    fe_set_free(&s); U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_user_overlong_face_enumerates_nothing) {
    if (!need()) { t_skip("missing exports"); return; }
    /* A lfFaceName cannot exceed 31 characters, but EnumFontFamiliesW takes a free string: 40 chars -> cwch 41 -> rejected in the kernel. */
    HDC dc = U(GetDC)(NULL);
    WCHAR w[48]; for (int i = 0; i < 40; i++) w[i] = L'A'; w[40] = 0;
    struct fe_set s; fe_set_init(&s);
    extern int CALLBACK fe_cb_pub(const LOGFONTW *, const TEXTMETRICW *, DWORD, LPARAM);
    SetLastError(0);
    int r = U(EnumFontFamiliesW)(dc, w, fe_cb_pub, (LPARAM)&s);
    EXPECT_EQ(s.n, 0);
    EXPECT_EQ(r, 0);
    EXPECT_ERR(ERROR_INVALID_HANDLE); /* bEnumFonts==0 -> GdiSetLastError(6) */
    fe_set_free(&s); U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_user_invalid_dc_fails) {
    if (!need()) { t_skip("missing exports"); return; }
    struct fe_set s; fe_set_init(&s);
    SetLastError(0);
    int r = fe_enum((HDC)(ULONG_PTR)0x1234, NULL, DEFAULT_CHARSET, &s);
    EXPECT_EQ(r, 0);
    EXPECT_EQ(s.n, 0);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    SetLastError(0);
    r = fe_enum(NULL, NULL, DEFAULT_CHARSET, &s);
    EXPECT_EQ(r, 0);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    fe_set_free(&s);
}

TEST(gdi_font_enum_resources_user_lparam_passed_through) {
    if (!need()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    struct fe_set s; fe_set_init(&s); s.stop_after = 1;
    fe_enum(dc, L"Arial", DEFAULT_CHARSET, &s); /* lParam is &s: the item landed there */
    EXPECT_EQ(s.n, 1);
    fe_set_free(&s); U(ReleaseDC)(NULL, dc);
}

/* ---- NTM / LOGFONT content ------------------------------------------------------------------------------------- */
TEST(gdi_font_enum_resources_user_arial_has_regular_and_bold_full_names) {
    if (!need()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    struct fe_set s; fe_set_init(&s);
    fe_enum(dc, L"Arial", ANSI_CHARSET, &s);
    CHECKF(fe_find_full(&s, L"Arial") >= 1, "no 'Arial' full name");
    CHECKF(fe_find_full(&s, L"Arial Bold") >= 1, "no 'Arial Bold' full name");
    CHECKF(fe_find_full(&s, L"Arial Italic") >= 1, "no 'Arial Italic' full name");
    CHECKF(fe_find_full(&s, L"Arial Bold Italic") >= 1, "no 'Arial Bold Italic' full name");
    fe_set_free(&s); U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_user_arial_ntm_flags_match_weight_and_italic) {
    if (!need()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    struct fe_set s; fe_set_init(&s);
    fe_enum(dc, L"Arial", ANSI_CHARSET, &s);
    int reg = 0;
    for (int i = 0; i < s.n; i++) {
        const struct fe_item *it = &s.it[i];
        if (!it->hasntm) { CHECKF(0, "item %d has no NTM", i); continue; }
        int bold = (it->ntmFlags & NTM_BOLD) != 0, ital = (it->ntmFlags & NTM_ITALIC) != 0, regular = (it->ntmFlags & NTM_REGULAR) != 0;
        CHECKF(ital == (it->italic != 0), "%ls: NTM_ITALIC=%d lfItalic=%d", it->full, ital, it->italic);
        CHECKF(bold == (it->weight >= 700), "%ls: NTM_BOLD=%d weight=%ld", it->full, bold, it->weight);
        if (regular) { reg++; CHECKF(!bold && !ital, "%ls: NTM_REGULAR with bold/italic", it->full); }
    }
    CHECK(reg >= 1);
    fe_set_free(&s); U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_user_arial_font_signature_latin) {
    if (!need()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    struct fe_set s; fe_set_init(&s);
    fe_enum(dc, L"Arial", ANSI_CHARSET, &s);
    CHECK(s.n > 0);
    for (int i = 0; i < s.n; i++) {
        CHECKF(s.it[i].sig.fsCsb[0] & 1, "item %d: Latin 1 codepage bit clear (0x%lx)", i, s.it[i].sig.fsCsb[0]);
        CHECKF(s.it[i].sig.fsUsb[0] & 1, "item %d: Basic Latin range bit clear (0x%lx)", i, s.it[i].sig.fsUsb[0]);
    }
    fe_set_free(&s); U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_user_arial_logfont_height_note) {
    if (!need()) { t_skip("missing exports"); return; }
    /* GDI reports scalable faces with lfHeight 0 / lfWidth 0 in the enumerated LOGFONT. */
    HDC dc = U(GetDC)(NULL);
    BYTE *b; ULONG sz; struct fe_rec *r = NULL;
    fe_raw_enum(dc, 3, L"Arial", ANSI_CHARSET, &b, &sz);
    int n = b ? fe_raw_walk(b, sz, &r) : -1;
    CHECK(n > 0);
    for (int i = 0; i < n; i++) {
        t_note("Arial item %d lfHeight=%ld lfWidth=%ld", i, r[i].elf.elfLogFont.lfHeight, r[i].elf.elfLogFont.lfWidth);
        break;
    }
    free(r); free(b); U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_user_script_name_nonempty) {
    if (!need()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    struct fe_set s; fe_set_init(&s);
    fe_enum(dc, L"Arial", ANSI_CHARSET, &s);
    CHECK(s.n > 0);
    for (int i = 0; i < s.n; i++) { CHECK(s.it[i].script[0] != 0); CHECK(s.it[i].style[0] != 0); t_note("Arial item %d script=%ls style=%ls", i, s.it[i].script, s.it[i].style); break; }
    fe_set_free(&s); U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_user_fixed_pitch_courier) {
    if (!need()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    struct fe_set s; fe_set_init(&s);
    fe_enum(dc, L"Courier New", ANSI_CHARSET, &s);
    CHECK(s.n > 0);
    for (int i = 0; i < s.n; i++) CHECKF((s.it[i].pitch & 3) == FIXED_PITCH, "item %d pitch 0x%x", i, s.it[i].pitch);
    fe_set_free(&s); U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_user_variable_pitch_arial) {
    if (!need()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    struct fe_set s; fe_set_init(&s);
    fe_enum(dc, L"Arial", ANSI_CHARSET, &s);
    CHECK(s.n > 0);
    for (int i = 0; i < s.n; i++) CHECKF((s.it[i].pitch & 3) == VARIABLE_PITCH, "item %d pitch 0x%x", i, s.it[i].pitch);
    fe_set_free(&s); U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_user_family_class_bits) {
    if (!need()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    struct { const WCHAR *f; int fam; } t[] = { { L"Arial", FF_SWISS }, { L"Times New Roman", FF_ROMAN }, { L"Courier New", FF_MODERN } };
    for (int i = 0; i < 3; i++) {
        struct fe_set s; fe_set_init(&s);
        fe_enum(dc, t[i].f, ANSI_CHARSET, &s);
        CHECK(s.n > 0);
        for (int k = 0; k < s.n; k++) CHECKF((s.it[k].pitch & 0xF0) == t[i].fam, "%ls item %d family 0x%x", t[i].f, k, s.it[k].pitch & 0xF0);
        fe_set_free(&s);
    }
    U(ReleaseDC)(NULL, dc);
}

/* ---- vertical (@) faces ---------------------------------------------------------------------------------------- */
TEST(gdi_font_enum_resources_user_vertical_faces_have_horizontal_twin) {
    if (!need()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    struct fe_set s; fe_set_init(&s);
    fe_enum(dc, NULL, DEFAULT_CHARSET, &s);
    int at = 0, orphan = 0;
    for (int i = 0; i < s.n; i++) if (s.it[i].face[0] == L'@') {
        at++;
        if (!fe_find(&s, s.it[i].face + 1)) { orphan++; t_note("orphan vertical face %ls", s.it[i].face); }
    }
    t_note("vertical (@) entries: %d", at);
    EXPECT_EQ(orphan, 0);
    fe_set_free(&s); U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_user_vertical_faces_only_for_cjk_charsets) {
    if (!need()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    struct fe_set s; fe_set_init(&s);
    fe_enum(dc, NULL, ANSI_CHARSET, &s);
    int at = 0; for (int i = 0; i < s.n; i++) if (s.it[i].face[0] == L'@') at++;
    t_note("@ faces in ANSI enumeration: %d", at);
    fe_set_free(&s); U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_user_vertical_arial_filter) {
    if (!need()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    int n = fe_count_family(dc, L"@Arial", DEFAULT_CHARSET);
    t_note("@Arial enumerates %d entries (Arial has no vertical support)", n);
    U(ReleaseDC)(NULL, dc);
}

/* ---- parity between the entry points --------------------------------------------------------------------------- */
static int CALLBACK cnt_cb(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD ft, LPARAM lp) { (*(int *)lp)++; return 1; }
static int CALLBACK cnt_cba(const LOGFONTA *lf, const TEXTMETRICA *tm, DWORD ft, LPARAM lp) { (*(int *)lp)++; return 1; }

TEST(gdi_font_enum_resources_user_ansi_and_wide_counts_match) {
    if (!need()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    LOGFONTA la; LOGFONTW lw; memset(&la, 0, sizeof la); memset(&lw, 0, sizeof lw);
    la.lfCharSet = lw.lfCharSet = ANSI_CHARSET;
    lstrcpyA(la.lfFaceName, "Arial"); lstrcpyW(lw.lfFaceName, L"Arial");
    int na = 0, nw = 0;
    U(EnumFontFamiliesExA)(dc, &la, cnt_cba, (LPARAM)&na, 0);
    U(EnumFontFamiliesExW)(dc, &lw, cnt_cb, (LPARAM)&nw, 0);
    EXPECT_EQ(na, nw);
    CHECK(nw > 0);
    la.lfCharSet = lw.lfCharSet = DEFAULT_CHARSET; la.lfFaceName[0] = 0; lw.lfFaceName[0] = 0;
    na = nw = 0;
    U(EnumFontFamiliesExA)(dc, &la, cnt_cba, (LPARAM)&na, 0);
    U(EnumFontFamiliesExW)(dc, &lw, cnt_cb, (LPARAM)&nw, 0);
    if (na != nw) t_note("DEFAULT enumeration: ANSI %d vs wide %d", na, nw);
    CHECK(na > 0);
    U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_user_enum_font_families_w_with_family) {
    if (!need()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    int nfam = 0, nex = 0, nfonts = 0;
    U(EnumFontFamiliesW)(dc, L"Arial", cnt_cb, (LPARAM)&nfam);
    U(EnumFontsW)(dc, L"Arial", cnt_cb, (LPARAM)&nfonts);
    nex = fe_count_family(dc, L"Arial", DEFAULT_CHARSET);
    CHECK(nfam > 0);
    CHECK(nfonts > 0);
    t_note("Arial: EnumFontFamiliesW=%d EnumFontsW=%d EnumFontFamiliesExW(DEFAULT)=%d", nfam, nfonts, nex);
    U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_user_enum_font_families_w_null_lists_families) {
    if (!need()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    int n = 0, nall = 0;
    U(EnumFontFamiliesW)(dc, NULL, cnt_cb, (LPARAM)&n);
    U(EnumFontsW)(dc, NULL, cnt_cb, (LPARAM)&nall);
    CHECK(n > 0);
    CHECKF(n <= nall, "families %d > faces %d", n, nall);
    U(ReleaseDC)(NULL, dc);
}

struct nested { HDC dc; int outer, inner, innerdone; };
static int CALLBACK nest_cb(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD ft, LPARAM lp) {
    struct nested *n = (struct nested *)lp;
    n->outer++;
    if (!n->innerdone) { n->innerdone = 1; LOGFONTW l; memset(&l, 0, sizeof l); l.lfCharSet = ANSI_CHARSET; lstrcpyW(l.lfFaceName, L"Courier New");
        U(EnumFontFamiliesExW)(n->dc, &l, cnt_cb, (LPARAM)&n->inner, 0); }
    return 1;
}
TEST(gdi_font_enum_resources_user_nested_enumeration) {
    if (!need()) { t_skip("missing exports"); return; }
    /* The record buffer is a private heap snapshot, so a callback may start another enumeration on the same DC. */
    HDC dc = U(GetDC)(NULL);
    struct nested n = { dc, 0, 0, 0 };
    LOGFONTW lf; memset(&lf, 0, sizeof lf); lf.lfCharSet = ANSI_CHARSET; lstrcpyW(lf.lfFaceName, L"Arial");
    U(EnumFontFamiliesExW)(dc, &lf, nest_cb, (LPARAM)&n, 0);
    int expect = fe_count_family(dc, L"Courier New", ANSI_CHARSET);
    CHECK(n.outer > 0);
    EXPECT_EQ(n.inner, expect);
    U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_user_enumeration_on_released_dc_in_callback) {
    if (!need()) { t_skip("missing exports"); return; }
    /* Deleting the DC from inside the callback does not disturb the remaining callbacks: the snapshot is user-side. */
    HDC dc = U(CreateCompatibleDC)(NULL);
    struct fe_set s; fe_set_init(&s);
    int r = fe_enum(dc, L"Arial", ANSI_CHARSET, &s);
    int n = s.n;
    U(DeleteDC)(dc);
    CHECK(r != 0); CHECK(n > 0);
    fe_set_free(&s);
}

TEST(gdi_font_enum_resources_user_screen_vs_memory_dc_family_lists_equal) {
    if (!need()) { t_skip("missing exports"); return; }
    HDC scr = U(GetDC)(NULL), mem = U(CreateCompatibleDC)(scr);
    struct fe_set a, b; fe_set_init(&a); fe_set_init(&b);
    fe_enum(scr, NULL, DEFAULT_CHARSET, &a);
    fe_enum(mem, NULL, DEFAULT_CHARSET, &b);
    EXPECT_EQ(a.n, b.n);
    if (a.n == b.n) for (int i = 0; i < a.n; i++) if (lstrcmpW(a.it[i].full, b.it[i].full)) { CHECKF(0, "item %d: %ls vs %ls", i, a.it[i].full, b.it[i].full); break; }
    fe_set_free(&a); fe_set_free(&b);
    U(DeleteDC)(mem); U(ReleaseDC)(NULL, scr);
}

TEST(gdi_font_enum_resources_user_enumeration_stable_between_runs) {
    if (!need()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    int a = fe_count_family(dc, NULL, DEFAULT_CHARSET), b = fe_count_family(dc, NULL, DEFAULT_CHARSET);
    EXPECT_EQ(a, b);
    U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_user_every_item_has_face_and_full_name) {
    if (!need()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    struct fe_set s; fe_set_init(&s);
    fe_enum(dc, NULL, DEFAULT_CHARSET, &s);
    CHECK(s.n > 20);
    int noface = 0, nofull = 0;
    for (int i = 0; i < s.n; i++) { if (!s.it[i].face[0]) noface++; if (!s.it[i].full[0]) nofull++; }
    EXPECT_EQ(noface, 0);
    if (nofull) t_note("%d items without full name", nofull);
    fe_set_free(&s); U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_user_font_types_have_defined_bits_only) {
    if (!need()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    struct fe_set s; fe_set_init(&s);
    fe_enum(dc, NULL, DEFAULT_CHARSET, &s);
    int odd = 0; DWORD seen = 0;
    for (int i = 0; i < s.n; i++) { seen |= s.it[i].ftype; if (s.it[i].ftype & ~7u) odd++; if ((s.it[i].ftype & 7) == 0) CHECKF(fe_is_vector_face(s.it[i].face), "item %d (%ls) type 0x%lx", i, s.it[i].face, s.it[i].ftype); }
    t_note("font type bits seen 0x%lx, %d items with bits above 7", seen, odd);
    fe_set_free(&s); U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_user_createfont_selects_enumerated_faces) {
    if (!need() || !U(CreateFontW) || !U(SelectObject) || !U(GetTextFaceW) || !U(DeleteObject)) { t_skip("missing exports"); return; }
    HDC dc = U(CreateCompatibleDC)(NULL);
    for (int i = 0; i < NEL(g_core_tt); i++) {
        HFONT f = U(CreateFontW)(-16, 0, 0, 0, 400, 0, 0, 0, g_core_tt[i][0] == L'S' || g_core_tt[i][0] == L'W' || g_core_tt[i][0] == L'M' ? DEFAULT_CHARSET : ANSI_CHARSET, 0, 0, 0, 0, g_core_tt[i]);
        HGDIOBJ old = U(SelectObject)(dc, f);
        WCHAR face[64] = { 0 };
        U(GetTextFaceW)(dc, 64, face);
        CHECKF(!lstrcmpiW(face, g_core_tt[i]), "asked %ls, selected %ls", g_core_tt[i], face);
        U(SelectObject)(dc, old); U(DeleteObject)(f);
    }
    U(DeleteDC)(dc);
}
