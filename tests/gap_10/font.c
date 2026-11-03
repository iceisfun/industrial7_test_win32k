/* NtGdiGetFontData / NtGdiGetKerningPairs / NtGdiGetTextFaceW / NtGdiGetTextCharsetInfo / NtGdiHfontCreate. */
#include "g10.h"

/* ---- NtGdiGetFontData (win32kfull: cb==0 -> size query; cb > 0x2710000 -> no temp buffer -> GDI_ERROR;
 *      result larger than cb -> GDI_ERROR; otherwise memcpy of the returned count) ---- */
static const struct { const char *name; DWORD tag; DWORD len; } g_tab[] = {
    { "head", G10_TAG('h','e','a','d'), 54 },   /* head is a fixed 54-byte table */
    { "hhea", G10_TAG('h','h','e','a'), 36 },   /* hhea is a fixed 36-byte table */
    { "maxp", G10_TAG('m','a','x','p'), 0 }, { "cmap", G10_TAG('c','m','a','p'), 0 },
    { "name", G10_TAG('n','a','m','e'), 0 }, { "post", G10_TAG('p','o','s','t'), 0 },
    { "OS/2", G10_TAG('O','S','/','2'), 0 }, { "hmtx", G10_TAG('h','m','t','x'), 0 },
    { "glyf", G10_TAG('g','l','y','f'), 0 }, { "loca", G10_TAG('l','o','c','a'), 0 },
};
TEST(gap10_fontdata_size_probe_per_table) {
    REQUIRE(UN(NtGdiGetFontData));
    G10_NEED_ARIAL(x, -16);
    for (unsigned i = 0; i < sizeof g_tab / sizeof g_tab[0]; i++) {
        DWORD n = UN(NtGdiGetFontData)(x.dc, g_tab[i].tag, 0, NULL, 0);
        CHECKF(n != G10_GDI_ERROR && n != 0, "%s size probe %lu", g_tab[i].name, n);
        if (g_tab[i].len) G10_EQ(n, g_tab[i].len);
        /* the user-mode wrapper is a straight pass-through */
        G10_EQ(U(GetFontData)(x.dc, g_tab[i].tag, 0, NULL, 0), n);
    }
    g10_font_close(&x);
}
TEST(gap10_fontdata_head_contents) {
    REQUIRE(UN(NtGdiGetFontData));
    G10_NEED_ARIAL(x, -16);
    unsigned char b[54]; memset(b, 0xCC, sizeof b);
    DWORD n = UN(NtGdiGetFontData)(x.dc, G10_TAG('h','e','a','d'), 0, b, sizeof b);
    G10_EQ(n, 54);
    /* head: version 1.0 (16.16) at +0, magicNumber 0x5F0F3CF5 (big endian) at +12 */
    CHECK(b[0] == 0 && b[1] == 1 && b[2] == 0 && b[3] == 0);
    CHECK(b[12] == 0x5F && b[13] == 0x0F && b[14] == 0x3C && b[15] == 0xF5);
    g10_font_close(&x);
}
TEST(gap10_fontdata_partial_and_offset_reads_are_consistent) {
    REQUIRE(UN(NtGdiGetFontData));
    G10_NEED_ARIAL(x, -16);
    unsigned char all[54], part[8];
    CHECK(UN(NtGdiGetFontData)(x.dc, G10_TAG('h','e','a','d'), 0, all, 54) == 54);
    memset(part, 0xCC, sizeof part);
    G10_EQ(UN(NtGdiGetFontData)(x.dc, G10_TAG('h','e','a','d'), 0, part, 4), 4);
    CHECK(!memcmp(part, all, 4));
    CHECK(part[4] == 0xCC);                      /* only the returned count is copied out */
    memset(part, 0xCC, sizeof part);
    G10_EQ(UN(NtGdiGetFontData)(x.dc, G10_TAG('h','e','a','d'), 12, part, 4), 4);
    CHECK(!memcmp(part, all + 12, 4));
    g10_font_close(&x);
}
TEST(gap10_fontdata_whole_file_table_zero) {
    REQUIRE(UN(NtGdiGetFontData));
    G10_NEED_ARIAL(x, -16);
    DWORD sz = UN(NtGdiGetFontData)(x.dc, 0, 0, NULL, 0);
    CHECKF(sz != G10_GDI_ERROR && sz > 54, "file size %lu", sz);
    unsigned char b[4] = {0};
    G10_EQ(UN(NtGdiGetFontData)(x.dc, 0, 0, b, 4), 4);
    /* sfnt version: 0x00010000, 'true', 'OTTO' or 'ttcf' */
    DWORD v = ((DWORD)b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3];
    CHECKF(v == 0x00010000 || v == 0x74727565 || v == 0x4F54544F || v == 0x74746366, "sfnt tag 0x%08lx", v);
    t_note("Arial whole-file size %lu", sz);
    g10_font_close(&x);
}
TEST(gap10_fontdata_unknown_table_is_gdi_error) {
    REQUIRE(UN(NtGdiGetFontData));
    G10_NEED_ARIAL(x, -16);
    unsigned char b[8];
    G10_EQ(UN(NtGdiGetFontData)(x.dc, G10_TAG('Z','Z','Z','Z'), 0, NULL, 0), G10_GDI_ERROR);
    G10_EQ(UN(NtGdiGetFontData)(x.dc, G10_TAG('Z','Z','Z','Z'), 0, b, sizeof b), G10_GDI_ERROR);
    /* Arial is a glyf font: no CFF table */
    G10_EQ(UN(NtGdiGetFontData)(x.dc, G10_TAG('C','F','F',' '), 0, NULL, 0), G10_GDI_ERROR);
    g10_font_close(&x);
}
TEST(gap10_fontdata_cb_over_limit_is_gdi_error_without_touching_buffer) {
    REQUIRE(UN(NtGdiGetFontData));
    G10_NEED_ARIAL(x, -16);
    unsigned char b[16]; memset(b, 0xCC, sizeof b);
    /* NtGdiGetFontData only allocates a temp buffer for cb < 0x2710001; larger requests return GDI_ERROR */
    G10_EQ(UN(NtGdiGetFontData)(x.dc, G10_TAG('h','e','a','d'), 0, b, 0x2710001), G10_GDI_ERROR);
    G10_EQ(UN(NtGdiGetFontData)(x.dc, 0, 0, b, 0xFFFFFFF0u), G10_GDI_ERROR);
    for (int i = 0; i < 16; i++) CHECK(b[i] == 0xCC);
    g10_font_close(&x);
}
TEST(gap10_fontdata_invalid_dc_is_gdi_error) {
    REQUIRE(UN(NtGdiGetFontData));
    unsigned char b[8];
    HDC bad[] = { NULL, (HDC)(ULONG_PTR)0x1234, (HDC)(ULONG_PTR)0xFFFFFFFFFFFFFFFFull };
    for (unsigned i = 0; i < 3; i++) {
        G10_EQ(UN(NtGdiGetFontData)(bad[i], G10_TAG('h','e','a','d'), 0, NULL, 0), G10_GDI_ERROR);
        G10_EQ(UN(NtGdiGetFontData)(bad[i], G10_TAG('h','e','a','d'), 0, b, sizeof b), G10_GDI_ERROR);
    }
}
TEST(gap10_fontdata_deleted_dc_is_gdi_error) {
    REQUIRE(UN(NtGdiGetFontData));
    G10_NEED_ARIAL(x, -16);
    HDC dc = x.dc;
    g10_font_close(&x);   /* deletes the DC */
    G10_EQ(UN(NtGdiGetFontData)(dc, G10_TAG('h','e','a','d'), 0, NULL, 0), G10_GDI_ERROR);
}
TEST(gap10_fontdata_raster_font_has_no_tables) {
    REQUIRE(UN(NtGdiGetFontData));
    HDC dc = g10_mem_dc(); REQUIRE(dc);
    HGDIOBJ old = U(SelectObject)(dc, U(GetStockObject)(SYSTEM_FIXED_FONT));
    TEXTMETRICW tm; U(GetTextMetricsW)(dc, &tm);
    if (tm.tmPitchAndFamily & TMPF_TRUETYPE) t_note("SYSTEM_FIXED_FONT is TrueType here; skipping table check");
    else G10_EQ(UN(NtGdiGetFontData)(dc, G10_TAG('h','e','a','d'), 0, NULL, 0), G10_GDI_ERROR);
    U(SelectObject)(dc, old);
    g10_mem_dc_free(dc);
}
TEST(gap10_fontdata_size_independent_of_font_height) {
    REQUIRE(UN(NtGdiGetFontData));
    G10_NEED_ARIAL(a, -12);
    G10F b;
    if (!g10_font_open_tt(&b, L"Arial", -48, DEFAULT_CHARSET)) { g10_font_close(&a); t_skip("no second Arial"); return; }
    /* the tables come from the font file, not from the realized size */
    G10_EQ(UN(NtGdiGetFontData)(a.dc, G10_TAG('h','e','a','d'), 0, NULL, 0), UN(NtGdiGetFontData)(b.dc, G10_TAG('h','e','a','d'), 0, NULL, 0));
    G10_EQ(UN(NtGdiGetFontData)(a.dc, G10_TAG('c','m','a','p'), 0, NULL, 0), UN(NtGdiGetFontData)(b.dc, G10_TAG('c','m','a','p'), 0, NULL, 0));
    g10_font_close(&a); g10_font_close(&b);
}
TEST(gap10_fontdata_offset_past_table_end) {
    REQUIRE(UN(NtGdiGetFontData));
    G10_NEED_ARIAL(x, -16);
    unsigned char b[4];
    DWORD r = UN(NtGdiGetFontData)(x.dc, G10_TAG('h','e','a','d'), 54, b, 4);
    t_note("offset == table length, cb 4 -> 0x%lx", r);
    r = UN(NtGdiGetFontData)(x.dc, G10_TAG('h','e','a','d'), 0, b, 4096);
    t_note("cb 4096 over a 54-byte table -> 0x%lx", r);
    CHECK(r == G10_GDI_ERROR || r <= 54);
    g10_font_close(&x);
}

/* ---- NtGdiGetKerningPairs (win32kfull: n and buffer both required to copy; result clamped to n; n > 0x4e2000 with a buffer -> 0) ---- */
static BOOL kern_font(G10F *x, int h) {
    static const WCHAR *faces[] = { L"Arial", L"Times New Roman", L"Segoe UI" };
    for (int i = 0; i < 3; i++) {
        if (!g10_font_open_tt(x, faces[i], h, DEFAULT_CHARSET)) continue;
        if (UN(NtGdiGetKerningPairs)(x->dc, 0, NULL) > 0) return TRUE;
        g10_font_close(x);
    }
    return FALSE;
}
#define NEED_KERN(x, h) G10F x; REQUIRE(UN(NtGdiGetKerningPairs)); \
    if (!kern_font(&x, (h))) { t_skip("no TrueType font with a kern table"); return; }
TEST(gap10_kerning_count_probe_matches_user_wrapper) {
    NEED_KERN(x, -16);
    DWORD n = UN(NtGdiGetKerningPairs)(x.dc, 0, NULL);
    G10_EQ(U(GetKerningPairsW)(x.dc, 0, NULL), n);
    CHECK(n > 0);
    /* a non-NULL buffer with n == 0 still takes the count path inside the kerning-pair query */
    g10_font_close(&x);
}
TEST(gap10_kerning_full_read_returns_count_and_sane_pairs) {
    NEED_KERN(x, -16);
    DWORD n = UN(NtGdiGetKerningPairs)(x.dc, 0, NULL);
    KERNINGPAIR *p = (KERNINGPAIR *)calloc(n + 2, sizeof *p);
    REQUIRE(p);
    memset(p, 0xCC, (n + 2) * sizeof *p);
    DWORD got = UN(NtGdiGetKerningPairs)(x.dc, n, p);
    G10_EQ(got, n);
    int bad = 0;
    for (DWORD i = 0; i < got; i++) {
        if (!p[i].wFirst || !p[i].wSecond) bad++;
        if (p[i].iKernAmount < -64 || p[i].iKernAmount > 64) bad++;   /* 16px font: amounts are a few pixels at most */
    }
    CHECKF(bad == 0, "%d implausible pairs", bad);
    unsigned char *tail = (unsigned char *)&p[n];
    for (unsigned i = 0; i < 2 * sizeof *p; i++) CHECK(tail[i] == 0xCC);   /* nothing written past the returned count */
    free(p);
    g10_font_close(&x);
}
TEST(gap10_kerning_short_buffer_never_overflows) {
    NEED_KERN(x, -16);
    DWORD n = UN(NtGdiGetKerningPairs)(x.dc, 0, NULL);
    DWORD want = n > 8 ? 4 : 1;
    KERNINGPAIR p[8]; memset(p, 0xCC, sizeof p);
    DWORD got = UN(NtGdiGetKerningPairs)(x.dc, want, p);
    t_note("short buffer: asked %lu of %lu, got %lu", want, n, got);
    CHECK(got <= want);
    unsigned char *tail = (unsigned char *)&p[want];
    for (unsigned i = 0; i < sizeof p - want * sizeof p[0]; i++) CHECK(tail[i] == 0xCC);
    g10_font_close(&x);
}
TEST(gap10_kerning_count_independent_of_height) {
    NEED_KERN(a, -12);
    DWORD na = UN(NtGdiGetKerningPairs)(a.dc, 0, NULL);
    G10F b; memset(&b, 0, sizeof b);
    if (!g10_font_open_tt(&b, a.face, -40, DEFAULT_CHARSET)) { g10_font_close(&a); t_skip("no second size"); return; }
    G10_EQ(UN(NtGdiGetKerningPairs)(b.dc, 0, NULL), na);
    g10_font_close(&a); g10_font_close(&b);
}
TEST(gap10_kerning_amounts_scale_with_height) {
    NEED_KERN(a, -16);
    DWORD n = UN(NtGdiGetKerningPairs)(a.dc, 0, NULL);
    G10F b;
    if (!g10_font_open_tt(&b, a.face, -64, DEFAULT_CHARSET)) { g10_font_close(&a); t_skip("no second size"); return; }
    KERNINGPAIR *pa = (KERNINGPAIR *)calloc(n, sizeof *pa), *pb = (KERNINGPAIR *)calloc(n, sizeof *pb);
    REQUIRE(pa && pb);
    CHECK(UN(NtGdiGetKerningPairs)(a.dc, n, pa) == n);
    CHECK(UN(NtGdiGetKerningPairs)(b.dc, n, pb) == n);
    long sa = 0, sb = 0; int same = 1;
    for (DWORD i = 0; i < n; i++) {
        if (pa[i].wFirst != pb[i].wFirst || pa[i].wSecond != pb[i].wSecond) same = 0;
        sa += labs(pa[i].iKernAmount); sb += labs(pb[i].iKernAmount);
    }
    CHECK(same);                       /* same pair order at any size */
    CHECKF(sb >= sa, "sum |kern| 16px=%ld 64px=%ld", sa, sb);
    free(pa); free(pb);
    g10_font_close(&a); g10_font_close(&b);
}
TEST(gap10_kerning_oversized_n_with_buffer_returns_zero) {
    NEED_KERN(x, -16);
    KERNINGPAIR p[2]; memset(p, 0xCC, sizeof p);
    /* n >= 0x4e2001 with a buffer: no temp buffer is allocated and the result stays 0 */
    G10_EQ(UN(NtGdiGetKerningPairs)(x.dc, 0x4e2001, p), 0);
    G10_EQ(UN(NtGdiGetKerningPairs)(x.dc, 0xFFFFFFFFu, p), 0);
    for (unsigned i = 0; i < sizeof p; i++) CHECK(((unsigned char *)p)[i] == 0xCC);
    g10_font_close(&x);
}
TEST(gap10_kerning_invalid_dc_returns_zero) {
    REQUIRE(UN(NtGdiGetKerningPairs));
    KERNINGPAIR p[2];
    HDC bad[] = { NULL, (HDC)(ULONG_PTR)0x1234 };
    for (unsigned i = 0; i < 2; i++) {
        G10_EQ(UN(NtGdiGetKerningPairs)(bad[i], 0, NULL), 0);
        G10_EQ(UN(NtGdiGetKerningPairs)(bad[i], 2, p), 0);
    }
}
TEST(gap10_kerning_raster_font_has_none) {
    REQUIRE(UN(NtGdiGetKerningPairs));
    HDC dc = g10_mem_dc(); REQUIRE(dc);
    HGDIOBJ old = U(SelectObject)(dc, U(GetStockObject)(SYSTEM_FONT));
    TEXTMETRICW tm; U(GetTextMetricsW)(dc, &tm);
    if (!(tm.tmPitchAndFamily & TMPF_TRUETYPE)) G10_EQ(UN(NtGdiGetKerningPairs)(dc, 0, NULL), 0);
    else t_note("SYSTEM_FONT is TrueType here");
    U(SelectObject)(dc, old);
    g10_mem_dc_free(dc);
}
TEST(gap10_kerning_user_wrapper_rejects_zero_n_with_buffer) {
    NEED_KERN(x, -16);
    KERNINGPAIR p[1];
    SET_ERR(0);
    /* gdi32full!GetKerningPairsW: nPairs == 0 with a buffer -> ERROR_INVALID_PARAMETER before any syscall */
    G10_EQ(U(GetKerningPairsW)(x.dc, 0, p), 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    g10_font_close(&x);
}

/* ---- NtGdiGetTextFaceW(hdc, cwc, buf, bAlias) ---- */
static int face_len(HDC dc, BOOL alias) { return UN(NtGdiGetTextFaceW)(dc, 0, NULL, alias); }
TEST(gap10_textface_length_probe_counts_the_nul) {
    REQUIRE(UN(NtGdiGetTextFaceW));
    G10_NEED_ARIAL(x, -16);
    G10_EQ(face_len(x.dc, FALSE), (int)wcslen(x.face) + 1);
    G10_EQ(face_len(x.dc, FALSE), 6);   /* "Arial" + NUL */
    /* NULL buffer with any positive cwc still reports the full required length */
    G10_EQ(UN(NtGdiGetTextFaceW)(x.dc, 5, NULL, FALSE), 6);
    G10_EQ(UN(NtGdiGetTextFaceW)(x.dc, 1000, NULL, FALSE), 6);
    g10_font_close(&x);
}
TEST(gap10_textface_exact_buffer_copy) {
    REQUIRE(UN(NtGdiGetTextFaceW));
    G10_NEED_ARIAL(x, -16);
    WCHAR b[16]; wmemset(b, 0xCCCC, 16);
    int r = UN(NtGdiGetTextFaceW)(x.dc, 6, b, FALSE);
    G10_EQ(r, 6);
    CHECK(!wcscmp(b, L"Arial"));
    CHECK(b[6] == 0xCCCC);
    wmemset(b, 0xCCCC, 16);
    r = UN(NtGdiGetTextFaceW)(x.dc, 16, b, FALSE);
    G10_EQ(r, 6);                       /* returns copied chars including the NUL, not the buffer size */
    CHECK(!wcscmp(b, L"Arial"));
    CHECK(b[6] == 0xCCCC);
    g10_font_close(&x);
}
TEST(gap10_textface_short_buffer_stays_inside_cwc) {
    REQUIRE(UN(NtGdiGetTextFaceW));
    G10_NEED_ARIAL(x, -16);
    for (int cwc = 1; cwc <= 5; cwc++) {
        WCHAR b[16]; wmemset(b, 0xCCCC, 16);
        int r = UN(NtGdiGetTextFaceW)(x.dc, cwc, b, FALSE);
        CHECKF(r >= 0 && r <= cwc, "cwc %d -> %d", cwc, r);
        for (int i = cwc; i < 16; i++) CHECKF(b[i] == 0xCCCC, "cwc %d wrote b[%d]", cwc, i);
        t_note("cwc %d -> %d", cwc, r);
    }
    g10_font_close(&x);
}
TEST(gap10_textface_nonpositive_cwc_with_buffer_copies_nothing) {
    REQUIRE(UN(NtGdiGetTextFaceW));
    G10_NEED_ARIAL(x, -16);
    WCHAR b[8]; wmemset(b, 0xCCCC, 8);
    int bad[] = { 0, -1, (int)0x80000000 };
    for (unsigned i = 0; i < 3; i++) {
 /* no temp buffer for cwc <= 0, so the memcpy branch is skipped */
        G10_EQ(UN(NtGdiGetTextFaceW)(x.dc, bad[i], b, FALSE), 0);
        for (int k = 0; k < 8; k++) CHECK(b[k] == 0xCCCC);
    }
    g10_font_close(&x);
}
TEST(gap10_textface_cwc_over_limit_with_buffer) {
    REQUIRE(UN(NtGdiGetTextFaceW));
    G10_NEED_ARIAL(x, -16);
    WCHAR b[8]; wmemset(b, 0xCCCC, 8);
    G10_EQ(UN(NtGdiGetTextFaceW)(x.dc, 0x1388001, b, FALSE), 0);   /* temp buffer limit is 0x1388000 chars */
    for (int k = 0; k < 8; k++) CHECK(b[k] == 0xCCCC);
    g10_font_close(&x);
}
TEST(gap10_textface_user_wrapper_rejects_buffer_with_nonpositive_c) {
    G10_NEED_ARIAL(x, -16);
    WCHAR b[8] = {0};
    SET_ERR(0);
    G10_EQ(U(GetTextFaceW)(x.dc, 0, b), 0);     /* gdi32full!GetTextFaceW: lpName != NULL && c < 1 */
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SET_ERR(0);
    G10_EQ(U(GetTextFaceW)(x.dc, -3, b), 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    G10_EQ(U(GetTextFaceW)(x.dc, 0, NULL), 6);   /* NULL buffer and c == 0 is the length query */
    g10_font_close(&x);
}
TEST(gap10_textface_invalid_dc_returns_zero) {
    REQUIRE(UN(NtGdiGetTextFaceW));
    WCHAR b[8];
    HDC bad[] = { NULL, (HDC)(ULONG_PTR)0x1234 };
    for (unsigned i = 0; i < 2; i++) {
        G10_EQ(UN(NtGdiGetTextFaceW)(bad[i], 0, NULL, FALSE), 0);
        G10_EQ(UN(NtGdiGetTextFaceW)(bad[i], 8, b, FALSE), 0);
    }
}
TEST(gap10_textface_alias_protocol_is_self_consistent) {
    REQUIRE(UN(NtGdiGetTextFaceW));
    G10F x;
    if (!g10_font_open(&x, L"Win32kt No Such Face", -16, DEFAULT_CHARSET)) { t_skip("font create failed"); return; }
    int na = face_len(x.dc, TRUE), nr = face_len(x.dc, FALSE);
    CHECK(na > 0); CHECK(nr > 0);
    WCHAR a[LF_FACESIZE + 1], r[LF_FACESIZE + 1];
    G10_EQ(UN(NtGdiGetTextFaceW)(x.dc, LF_FACESIZE, a, TRUE), na);
    G10_EQ(UN(NtGdiGetTextFaceW)(x.dc, LF_FACESIZE, r, FALSE), nr);
    CHECK((int)wcslen(a) + 1 == na);
    CHECK((int)wcslen(r) + 1 == nr);
    CHECK(!wcscmp(r, x.face));
    t_note("requested face with no match: alias \"%ls\" real \"%ls\"", a, r);
    g10_font_close(&x);
}

/* ---- GetTextCharsetInfo (GetTextCharsetInfo; FONTSIGNATURE is 3 qwords) ---- */
static const struct { int cs; const char *n; int csbit; } g_cs[] = {
    { ANSI_CHARSET, "ansi", 0 }, { EASTEUROPE_CHARSET, "easteurope", 1 }, { RUSSIAN_CHARSET, "russian", 2 },
    { GREEK_CHARSET, "greek", 3 }, { TURKISH_CHARSET, "turkish", 4 },
};
TEST(gap10_charset_matches_text_metrics_table) {
    for (unsigned i = 0; i < sizeof g_cs / sizeof g_cs[0]; i++) {
        G10F x;
        if (!g10_font_open_tt(&x, L"Arial", -16, g_cs[i].cs)) { t_note("%s: no Arial", g_cs[i].n); continue; }
        G10_EQ(U(GetTextCharsetInfo)(x.dc, NULL, 0), x.tm.tmCharSet);
        FONTSIGNATURE fs; memset(&fs, 0xAA, sizeof fs);
        G10_EQ(U(GetTextCharsetInfo)(x.dc, &fs, 0), x.tm.tmCharSet);
        if (x.tm.tmCharSet == g_cs[i].cs) {
            /* the signature describes the font file's code page coverage; Arial covers every code page of this table */
            CHECKF(fs.fsCsb[0] & (1u << g_cs[i].csbit), "%s fsCsb[0]=0x%08lx", g_cs[i].n, fs.fsCsb[0]);
        } else t_note("%s mapped to charset %u", g_cs[i].n, x.tm.tmCharSet);
        g10_font_close(&x);
    }
}
TEST(gap10_charset_signature_fully_overwritten) {
    G10_NEED_ARIAL(x, -16);
    FONTSIGNATURE fs; memset(&fs, 0xAA, sizeof fs);
    U(GetTextCharsetInfo)(x.dc, &fs, 0);
    int untouched = 1;
    for (unsigned i = 0; i < sizeof fs; i++) if (((unsigned char *)&fs)[i] != 0xAA) untouched = 0;
    CHECK(!untouched);
    CHECK(fs.fsUsb[0] & 1);       /* Basic Latin */
    g10_font_close(&x);
}
TEST(gap10_charset_null_signature_is_allowed) {
    G10_NEED_ARIAL(x, -16);
    int r = U(GetTextCharsetInfo)(x.dc, NULL, 0);
    CHECK(r >= 0 && r <= 255);
    g10_font_close(&x);
}
TEST(gap10_charset_invalid_dc_is_default_charset_and_zero_signature) {
    FONTSIGNATURE fs; memset(&fs, 0xAA, sizeof fs);
    SET_ERR(0);
    /* GetTextCharsetInfo: invalid DC -> last error 6 and DEFAULT_CHARSET; the zeroed temp is still copied out */
    G10_EQ(U(GetTextCharsetInfo)((HDC)(ULONG_PTR)0x1234, &fs, 0), DEFAULT_CHARSET);
    for (unsigned i = 0; i < sizeof fs; i++) CHECK(((unsigned char *)&fs)[i] == 0);
    t_note("last error after invalid DC: %lu", GetLastError());
}
TEST(gap10_charset_symbol_font) {
    G10F x;
    if (!g10_font_open(&x, L"Symbol", -16, SYMBOL_CHARSET)) { t_skip("no Symbol"); return; }
    if (x.tm.tmCharSet == SYMBOL_CHARSET) {
        G10_EQ(U(GetTextCharsetInfo)(x.dc, NULL, 0), SYMBOL_CHARSET);
    } else t_note("Symbol mapped to charset %u", x.tm.tmCharSet);
    g10_font_close(&x);
}

/* ---- NtGdiHfontCreate (win32kfull: cb in 1..0x1a4, pointer non-NULL, else 0 before anything is allocated) ---- */
TEST(gap10_hfontcreate_size_and_pointer_validation) {
    REQUIRE(UN(NtGdiHfontCreate));
    BYTE buf[0x1a8]; memset(buf, 0, sizeof buf);
    ULONG bad[] = { 0, 0x1a5, 0x1a8, 0x7FFFFFFF, 0xFFFFFFFF };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++)
        CHECKF(UN(NtGdiHfontCreate)(buf, bad[i], 0, 0, NULL) == NULL, "cb 0x%lx", bad[i]);
    G10_EQ(UN(NtGdiHfontCreate)(NULL, 0x164, 0, 0, NULL), NULL);
    G10_EQ(UN(NtGdiHfontCreate)(NULL, 1, 0, 0, NULL), NULL);
}
typedef struct { const char *n; LONG h, w, esc, orient, weight; BYTE it, ul, so, cs; const WCHAR *face; } LFCASE;
static const LFCASE g_lf[] = {
    { "plain", -16, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, L"Arial" },
    { "bold_it", -20, 0, 0, 0, FW_BOLD, 1, 0, 0, DEFAULT_CHARSET, L"Arial" },
    { "ul_so", 12, 7, 0, 0, FW_LIGHT, 0, 1, 1, ANSI_CHARSET, L"Courier New" },
    { "rot", -14, 0, 900, 900, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, L"Arial" },
    { "zero", 0, 0, 0, 0, 0, 0, 0, 0, DEFAULT_CHARSET, L"" },
    { "nosuch", -10, 0, 0, 0, FW_HEAVY, 0, 0, 0, DEFAULT_CHARSET, L"Win32kt No Such Face" },
    { "large", -300, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, L"Times New Roman" },
};
TEST(gap10_hfontcreate_roundtrips_logfont_through_kernel) {
    for (unsigned i = 0; i < sizeof g_lf / sizeof g_lf[0]; i++) {
        const LFCASE *c = &g_lf[i];
        LOGFONTW in; memset(&in, 0, sizeof in);
        in.lfHeight = c->h; in.lfWidth = c->w; in.lfEscapement = c->esc; in.lfOrientation = c->orient;
        in.lfWeight = c->weight; in.lfItalic = c->it; in.lfUnderline = c->ul; in.lfStrikeOut = c->so;
        in.lfCharSet = c->cs; lstrcpyW(in.lfFaceName, c->face);
        HFONT f = U(CreateFontIndirectW)(&in);
        CHECKF(f != NULL, "%s: CreateFontIndirectW", c->n);
        if (!f) continue;
        G10_EQ((((ULONG_PTR)f >> 16) & 0x7f), 0x0a);     /* LFONT handle type */
        G10_EQ(U(GetObjectType)(f), OBJ_FONT);
        LOGFONTW out; memset(&out, 0xCC, sizeof out);
        G10_EQ(U(GetObjectW)(f, sizeof out, &out), sizeof out);
        CHECKF(out.lfHeight == in.lfHeight && out.lfWidth == in.lfWidth, "%s: size %ld/%ld", c->n, out.lfHeight, out.lfWidth);
        CHECKF(out.lfEscapement == in.lfEscapement && out.lfOrientation == in.lfOrientation, "%s: angles", c->n);
        CHECKF(out.lfWeight == in.lfWeight, "%s: weight %ld", c->n, out.lfWeight);
        CHECKF(out.lfItalic == in.lfItalic && out.lfUnderline == in.lfUnderline && out.lfStrikeOut == in.lfStrikeOut, "%s: attrs", c->n);
        CHECKF(!lstrcmpW(out.lfFaceName, in.lfFaceName), "%s: face \"%ls\"", c->n, out.lfFaceName);
        struct k_gdicell cell;
        CHECK(k_gdi_cell(f, &cell));
        CHECK(U(DeleteObject)(f));
        CHECK(!U(DeleteObject)(f));                      /* handle lifetime: gone after DeleteObject (GetObjectType keeps answering for it on Windows 10) */
        G10_EQ(U(GetObjectW)(f, sizeof out, &out), 0);
    }
}
TEST(gap10_hfontcreate_every_font_is_a_distinct_handle) {
    LOGFONTW in; memset(&in, 0, sizeof in); in.lfHeight = -16; lstrcpyW(in.lfFaceName, L"Arial");
    HFONT f[4];
    for (int i = 0; i < 4; i++) { f[i] = U(CreateFontIndirectW)(&in); CHECK(f[i]); }
    for (int i = 0; i < 4; i++) for (int j = i + 1; j < 4; j++) CHECK(f[i] != f[j]);
    for (int i = 0; i < 4; i++) if (f[i]) U(DeleteObject)(f[i]);
}
TEST(gap10_hfontcreate_extended_logfont_axes_limit) {
    /* gdi32full!CreateFontIndirectExW passes cb = nAxes*4 + 0x164 and refuses nAxes > 16 itself */
    ENUMLOGFONTEXDVW e; memset(&e, 0, sizeof e);
    e.elfEnumLogfontEx.elfLogFont.lfHeight = -16;
    lstrcpyW(e.elfEnumLogfontEx.elfLogFont.lfFaceName, L"Arial");
    e.elfDesignVector.dvNumAxes = 17;
    CHECK(U(CreateFontIndirectExW)(&e) == NULL);
    e.elfDesignVector.dvNumAxes = 0;
    HFONT f = U(CreateFontIndirectExW)(&e);
    CHECK(f != NULL);
    if (f) U(DeleteObject)(f);
}
