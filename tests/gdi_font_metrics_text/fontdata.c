/* Raw font data, unicode ranges, kerning and glyph outlines through the font engine syscalls. */
#include "fm.h"

#define TAG(a, b, c, d) ((DWORD)(a) | ((DWORD)(b) << 8) | ((DWORD)(c) << 16) | ((DWORD)(d) << 24))
#define TG_HEAD TAG('h','e','a','d')
#define TG_OS2  TAG('O','S','/','2')
#define TG_CMAP TAG('c','m','a','p')
#define TG_NAME TAG('n','a','m','e')
#define TG_MAXP TAG('m','a','x','p')
#define TG_HHEA TAG('h','h','e','a')
#define TG_HMTX TAG('h','m','t','x')

static unsigned be16(const unsigned char *p) { return (p[0] << 8) | p[1]; }
static unsigned long be32(const unsigned char *p) { return ((unsigned long)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

struct fd { HDC dc; HFONT f; HGDIOBJ old; };
static int fd_open(struct fd *w, const WCHAR *face, int h, int wt, int cs) {
    memset(w, 0, sizeof *w);
    w->dc = fm_dc(); w->f = fm_font(face, h, wt, cs, 0, 0);
    if (!w->dc || !w->f) { if (w->f) fm.DeleteObject(w->f); fm_freedc(w->dc); memset(w, 0, sizeof *w); return 0; }
    w->old = fm.GetCurrentObject(w->dc, OBJ_FONT);
    fm_sel(w->dc, w->f);
    return 1;
}
static void fd_close(struct fd *w) { if (!w->dc) return; fm_sel(w->dc, w->old); fm.DeleteObject(w->f); fm_freedc(w->dc); memset(w, 0, sizeof *w); }

/* Fetch a whole table (tag 0 = whole file). Returns malloc'd buffer and size, or NULL. */
static unsigned char *table(HDC dc, DWORD tag, DWORD *size) {
    DWORD n = fm.GetFontData(dc, tag, 0, NULL, 0);
    *size = 0;
    if (n == GDI_ERROR || n == 0) return NULL;
    unsigned char *b = malloc(n + 8);
    if (!b) return NULL;
    DWORD g = fm.GetFontData(dc, tag, 0, b, n);
    if (g != n) { free(b); return NULL; }
    *size = n;
    return b;
}

static const WCHAR *k_ttf[] = { L"Arial", L"Courier New", L"Tahoma", L"Times New Roman", L"Segoe UI", L"Verdana", L"Consolas" };

/* ---- NtGdiGetFontData ---- */

TEST(gdi_font_metrics_text_fontdata_head_table) {
    FM_BEGIN();
    for (unsigned i = 0; i < sizeof k_ttf / sizeof *k_ttf; i++) {
        if (!fm_face_present(k_ttf[i])) continue;
        struct fd w; if (!fd_open(&w, k_ttf[i], -16, 400, ANSI_CHARSET)) continue;
        DWORD sz; unsigned char *h = table(w.dc, TG_HEAD, &sz);
        CHECKF(h != NULL, "face %u: no head table", i);
        if (h) {
            EXPECT_EQ(sz, 54);                                    /* the head table is a fixed 54 bytes */
            CHECKF(be32(h + 12) == 0x5F0F3CF5, "face %u magic 0x%lx", i, be32(h + 12));
            CHECK(be32(h) == 0x00010000);                          /* head version 1.0 */
            unsigned upem = be16(h + 18);
            CHECKF(upem >= 16 && upem <= 16384, "unitsPerEm %u", upem);
            free(h);
        }
        fd_close(&w);
    }
}

TEST(gdi_font_metrics_text_fontdata_size_query_matches_read) {
    FM_BEGIN();
    struct fd w; if (!fd_open(&w, L"Arial", -16, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    static const DWORD tags[] = { TG_HEAD, TG_OS2, TG_CMAP, TG_NAME, TG_MAXP, TG_HHEA, TG_HMTX };
    for (unsigned i = 0; i < 7; i++) {
        DWORD q = fm.GetFontData(w.dc, tags[i], 0, NULL, 0);
        CHECKF(q != GDI_ERROR && q > 0, "tag %u", i);
        if (q == GDI_ERROR) continue;
        unsigned char *b = malloc(q + 64); memset(b, 0xEE, q + 64);
        EXPECT_EQ(fm.GetFontData(w.dc, tags[i], 0, b, q), q);
        for (DWORD j = q; j < q + 64; j++) if (b[j] != 0xEE) { CHECKF(0, "tag %u byte past the table clobbered", i); break; }
        /* A buffer larger than the table still returns just the table size. */
        EXPECT_EQ(fm.GetFontData(w.dc, tags[i], 0, b, q + 32), q);
        free(b);
    }
    fd_close(&w);
}

TEST(gdi_font_metrics_text_fontdata_partial_reads) {
    FM_BEGIN();
    struct fd w; if (!fd_open(&w, L"Arial", -16, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    DWORD sz; unsigned char *all = table(w.dc, TG_HEAD, &sz);
    if (!all) { t_skip("no head"); fd_close(&w); return; }
    unsigned char part[16];
    EXPECT_EQ(fm.GetFontData(w.dc, TG_HEAD, 12, part, 4), 4);
    CHECK(!memcmp(part, all + 12, 4));
    EXPECT_EQ(fm.GetFontData(w.dc, TG_HEAD, 0, part, 10), 10);
    CHECK(!memcmp(part, all, 10));
    EXPECT_EQ(fm.GetFontData(w.dc, TG_HEAD, sz - 2, part, 2), 2);
    CHECK(!memcmp(part, all + sz - 2, 2));
    DWORD clip = fm.GetFontData(w.dc, TG_HEAD, sz - 2, part, 16);      /* the kernel never returns more than cb */
    t_note("GetFontData read across the end of the table returned %lu", clip);
    CHECK(clip <= 16);
    free(all); fd_close(&w);
}

TEST(gdi_font_metrics_text_fontdata_offset_beyond_table) {
    FM_BEGIN();
    struct fd w; if (!fd_open(&w, L"Arial", -16, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    unsigned char b[8];
    DWORD r = fm.GetFontData(w.dc, TG_HEAD, 54 + 100, b, 4);
    t_note("GetFontData head offset 154 (table is 54 bytes) returned 0x%lx", r);
    CHECK(r == GDI_ERROR || r == 0);
    r = fm.GetFontData(w.dc, TG_HEAD, 0xFFFFFFF0, b, 4);
    CHECK(r == GDI_ERROR || r == 0);
    fd_close(&w);
}

TEST(gdi_font_metrics_text_fontdata_unknown_tag_fails) {
    FM_BEGIN();
    struct fd w; if (!fd_open(&w, L"Arial", -16, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    unsigned char b[16];
    EXPECT_EQ(fm.GetFontData(w.dc, TAG('Z','Z','Z','Z'), 0, NULL, 0), GDI_ERROR);
    EXPECT_EQ(fm.GetFontData(w.dc, TAG('Z','Z','Z','Z'), 0, b, sizeof b), GDI_ERROR);
    EXPECT_EQ(fm.GetFontData(w.dc, TAG('g','l','y','x'), 0, b, sizeof b), GDI_ERROR);
    fd_close(&w);
}

TEST(gdi_font_metrics_text_fontdata_invalid_dc) {
    FM_BEGIN();
    unsigned char b[16];
    EXPECT_EQ(fm.GetFontData(NULL, TG_HEAD, 0, NULL, 0), GDI_ERROR);
    EXPECT_EQ(fm.GetFontData(NULL, TG_HEAD, 0, b, sizeof b), GDI_ERROR);
    EXPECT_EQ(fm.GetFontData((HDC)(ULONG_PTR)0x3030, TG_HEAD, 0, b, sizeof b), GDI_ERROR);
}

TEST(gdi_font_metrics_text_fontdata_bitmap_font_fails) {
    FM_BEGIN();
    HDC dc = fm_dc(); if (!dc) return;
    HGDIOBJ old = fm.SelectObject(dc, fm.GetStockObject(SYSTEM_FONT));
    TEXTMETRICW tm; fm.GetTextMetricsW(dc, &tm);
    if (tm.tmPitchAndFamily & TMPF_TRUETYPE) { t_skip("System font mapped to TrueType"); }
    else EXPECT_EQ(fm.GetFontData(dc, TG_HEAD, 0, NULL, 0), GDI_ERROR);   /* tables exist only for TrueType/OpenType fonts */
    fm.SelectObject(dc, old); fm_freedc(dc);
}

TEST(gdi_font_metrics_text_fontdata_cb_above_limit) {
    FM_BEGIN(); FM_NEED(NtGdiGetFontData_);
    struct fd w; if (!fd_open(&w, L"Arial", -16, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    unsigned char b[16];
    if (t_is_wine()) { t_skip("Wine honours the oversized cb and would overrun the buffer"); fd_close(&w); return; }
    /* NtGdiGetFontData: cb > 0x2710000 never allocates its temp buffer and returns 0xFFFFFFFF without touching b. */
    EXPECT_EQ(NtGdiGetFontData_(w.dc, TG_HEAD, 0, b, 0x2710001), GDI_ERROR);
    EXPECT_EQ(NtGdiGetFontData_(w.dc, TG_HEAD, 0, b, 0xFFFFFFFF), GDI_ERROR);
    fd_close(&w);
}

TEST(gdi_font_metrics_text_fontdata_direct_stub_matches) {
    FM_BEGIN(); FM_NEED(NtGdiGetFontData_);
    struct fd w; if (!fd_open(&w, L"Arial", -16, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    unsigned char a[64], b[64];
    EXPECT_EQ(NtGdiGetFontData_(w.dc, TG_HEAD, 0, NULL, 0), 54);
    EXPECT_EQ(NtGdiGetFontData_(w.dc, TG_HEAD, 0, a, 54), 54);
    EXPECT_EQ(fm.GetFontData(w.dc, TG_HEAD, 0, b, 54), 54);
    CHECK(!memcmp(a, b, 54));
    EXPECT_EQ(NtGdiGetFontData_(w.dc, TAG('Z','Z','Z','Z'), 0, NULL, 0), GDI_ERROR);
    fd_close(&w);
}

TEST(gdi_font_metrics_text_fontdata_whole_file_directory) {
    FM_BEGIN();
    static const WCHAR *faces[] = { L"Arial", L"Courier New", L"Tahoma", L"Times New Roman" };
    for (unsigned i = 0; i < 4; i++) {
        if (!fm_face_present(faces[i])) continue;
        struct fd w; if (!fd_open(&w, faces[i], -16, 400, ANSI_CHARSET)) continue;
        DWORD total; unsigned char *f = table(w.dc, 0, &total);       /* tag 0: the whole file */
        if (!f) { t_note("face %u: whole-file read unavailable", i); fd_close(&w); continue; }
        unsigned long ver = be32(f);
        CHECKF(ver == 0x00010000 || ver == 0x74727565 /* 'true' */ || ver == 0x4F54544F /* 'OTTO' */ || ver == 0x74746366 /* 'ttcf' */, "face %u version 0x%lx", i, ver);
        if (ver == 0x00010000 || ver == 0x74727565) {
            unsigned nt = be16(f + 4);
            CHECK(nt >= 8 && nt < 64);
            CHECK(total >= 12 + 16 * nt);
            for (unsigned t = 0; t < nt && 12 + 16 * (t + 1) <= total; t++) {
                const unsigned char *e = f + 12 + 16 * t;
                unsigned long off = be32(e + 8), len = be32(e + 12);
                CHECKF(off + len <= total + 3, "face %u table %u runs past file (off %lu len %lu total %lu)", i, t, off, len, (unsigned long)total);
                /* Cross-check: the per-table size reported by the kernel equals the directory length. */
                DWORD tag = (DWORD)e[0] | ((DWORD)e[1] << 8) | ((DWORD)e[2] << 16) | ((DWORD)e[3] << 24);
                static const DWORD chk[] = { TG_HEAD, TG_OS2, TG_CMAP, TG_NAME, TG_MAXP, TG_HHEA, TG_HMTX };
                for (unsigned c = 0; c < 7; c++) if (tag == chk[c]) {
                    CHECKF(fm.GetFontData(w.dc, tag, 0, NULL, 0) == len, "face %u table %.4s: kernel size vs directory %lu", i, e, len);
                }
            }
        }
        free(f); fd_close(&w);
    }
}

TEST(gdi_font_metrics_text_fontdata_cmap_name_os2_structure) {
    FM_BEGIN();
    static const WCHAR *faces[] = { L"Arial", L"Courier New", L"Tahoma", L"Times New Roman" };
    for (unsigned i = 0; i < 4; i++) {
        if (!fm_face_present(faces[i])) continue;
        struct fd w; if (!fd_open(&w, faces[i], -16, 400, ANSI_CHARSET)) continue;
        DWORD sz; unsigned char *t;
        if ((t = table(w.dc, TG_CMAP, &sz)) != NULL) {
            CHECK(be16(t) == 0);                     /* cmap version 0 */
            unsigned n = be16(t + 2);
            CHECK(n >= 1 && n < 32 && 4 + 8 * n <= sz);
            free(t);
        } else CHECK(0);
        if ((t = table(w.dc, TG_NAME, &sz)) != NULL) {
            unsigned fmt = be16(t), count = be16(t + 2), so = be16(t + 4);
            CHECK(fmt == 0 || fmt == 1);
            CHECK(count >= 1 && 6 + 12 * count <= sz && so <= sz);
            int found = 0;
            for (unsigned r = 0; r < count && 6 + 12 * (r + 1) <= sz; r++) {
                const unsigned char *rec = t + 6 + 12 * r;
                if (be16(rec) == 3 && be16(rec + 6) == 1) {              /* Windows platform, family name (ID 1) */
                    unsigned len = be16(rec + 8), off = be16(rec + 10);
                    if (so + off + len <= sz && len / 2 < 64) {
                        WCHAR nm[64]; for (unsigned k = 0; k < len / 2; k++) nm[k] = (WCHAR)be16(t + so + off + 2 * k);
                        nm[len / 2] = 0;
                        if (fm_wcsieq(nm, faces[i])) found = 1;
                    }
                }
            }
            CHECKF(found, "face %u: name table family does not match the selected face", i);
            free(t);
        } else CHECK(0);
        if ((t = table(w.dc, TG_OS2, &sz)) != NULL) {
            CHECK(sz >= 68);
            unsigned wc = be16(t + 4);
            EXPECT_EQ(wc, 400);                      /* usWeightClass of the regular face */
            free(t);
        } else CHECK(0);
        fd_close(&w);
    }
}

TEST(gdi_font_metrics_text_fontdata_bold_os2_weight) {
    FM_BEGIN();
    static const WCHAR *faces[] = { L"Arial", L"Tahoma", L"Times New Roman", L"Courier New" };
    for (unsigned i = 0; i < 4; i++) {
        if (!fm_face_present(faces[i])) continue;
        struct fd w; if (!fd_open(&w, faces[i], -16, 700, ANSI_CHARSET)) continue;
        DWORD sz; unsigned char *t = table(w.dc, TG_OS2, &sz);
        if (t && sz >= 6) EXPECT_EQ(be16(t + 4), 700);   /* the mapper picked the real bold file */
        free(t); fd_close(&w);
    }
}

TEST(gdi_font_metrics_text_fontdata_unitsperem_matches_otm) {
    FM_BEGIN();
    for (unsigned i = 0; i < sizeof k_ttf / sizeof *k_ttf; i++) {
        if (!fm_face_present(k_ttf[i])) continue;
        struct fd w; if (!fd_open(&w, k_ttf[i], -20, 400, ANSI_CHARSET)) continue;
        DWORD sz; unsigned char *h = table(w.dc, TG_HEAD, &sz);
        UINT n = fm.GetOutlineTextMetricsW(w.dc, 0, NULL);
        if (h && n) {
            OUTLINETEXTMETRICW *p = calloc(1, n + 8);
            if (fm.GetOutlineTextMetricsW(w.dc, n, p) == n) EXPECT_EQ(p->otmEMSquare, be16(h + 18));   /* head.unitsPerEm */
            free(p);
        }
        free(h); fd_close(&w);
    }
}

TEST(gdi_font_metrics_text_fontdata_maxp_numglyphs_vs_unicode_ranges) {
    FM_BEGIN();
    struct fd w; if (!fd_open(&w, L"Arial", -16, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    DWORD sz; unsigned char *m = table(w.dc, TG_MAXP, &sz);
    DWORD gsz = fm.GetFontUnicodeRanges(w.dc, NULL);
    if (m && sz >= 6 && gsz && gsz != GDI_ERROR) {
        GLYPHSET *gs = calloc(1, gsz + 8);
        gs->cbThis = gsz;
        if (fm.GetFontUnicodeRanges(w.dc, gs) == gsz) CHECK(gs->cGlyphsSupported <= be16(m + 4));   /* cannot support more Unicode glyphs than the font has */
        free(gs);
    }
    free(m); fd_close(&w);
}

/* ---- GetFontUnicodeRanges ---- */

TEST(gdi_font_metrics_text_ranges_size_query) {
    FM_BEGIN();
    struct fd w; if (!fd_open(&w, L"Arial", -16, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    DWORD n = fm.GetFontUnicodeRanges(w.dc, NULL);
    CHECK(n >= sizeof(GLYPHSET));
    CHECK(n < 1 << 20);
    EXPECT_EQ((n - 16) % 4, 0);                    /* 16 byte header + 4 bytes per WCRANGE */
    GLYPHSET *gs = calloc(1, n + 16);
    EXPECT_EQ(fm.GetFontUnicodeRanges(w.dc, gs), n);
    EXPECT_EQ(gs->cbThis, n);
    EXPECT_EQ(gs->cRanges, (n - 16) / 4);
    free(gs); fd_close(&w);
}

TEST(gdi_font_metrics_text_ranges_content_sorted_and_counted) {
    FM_BEGIN();
    for (unsigned i = 0; i < sizeof k_ttf / sizeof *k_ttf; i++) {
        if (!fm_face_present(k_ttf[i])) continue;
        struct fd w; if (!fd_open(&w, k_ttf[i], -16, 400, ANSI_CHARSET)) continue;
        DWORD n = fm.GetFontUnicodeRanges(w.dc, NULL);
        if (!n || n == GDI_ERROR) { CHECK(0); fd_close(&w); continue; }
        GLYPHSET *gs = calloc(1, n + 16);
        DWORD r = fm.GetFontUnicodeRanges(w.dc, gs);
        EXPECT_EQ(r, n);
        CHECK(gs->cRanges >= 1);
        CHECK(gs->cGlyphsSupported >= 90);
        unsigned long sum = 0; int sorted = 1; unsigned prev_end = 0;
        for (DWORD k = 0; k < gs->cRanges; k++) {
            WCRANGE *rg = &gs->ranges[k];
            sum += rg->cGlyphs;
            if (rg->cGlyphs == 0) sorted = 0;
            if (k && (unsigned)rg->wcLow < prev_end) sorted = 0;
            prev_end = (unsigned)rg->wcLow + rg->cGlyphs;
            if (prev_end > 0x10000) sorted = 0;
        }
        CHECKF(sorted, "face %u: ranges unsorted, overlapping or empty", i);
        CHECKF(sum == gs->cGlyphsSupported, "face %u: ranges sum %lu, cGlyphsSupported %lu", i, sum, (unsigned long)gs->cGlyphsSupported);
        /* Basic Latin lies inside some range. */
        int hasA = 0;
        for (DWORD k = 0; k < gs->cRanges; k++) if ('A' >= gs->ranges[k].wcLow && 'A' < gs->ranges[k].wcLow + gs->ranges[k].cGlyphs) hasA = 1;
        CHECKF(hasA, "face %u does not report 'A'", i);
        int has378 = 0;
        for (DWORD k = 0; k < gs->cRanges; k++) if (0x378 >= gs->ranges[k].wcLow && 0x378 < gs->ranges[k].wcLow + gs->ranges[k].cGlyphs) has378 = 1;
        CHECK(!has378);
        free(gs); fd_close(&w);
    }
}

TEST(gdi_font_metrics_text_ranges_agree_with_glyph_indices) {
    FM_BEGIN();
    struct fd w; if (!fd_open(&w, L"Arial", -16, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    DWORD n = fm.GetFontUnicodeRanges(w.dc, NULL);
    GLYPHSET *gs = calloc(1, n + 16);
    if (fm.GetFontUnicodeRanges(w.dc, gs) != n) { t_skip("ranges"); free(gs); fd_close(&w); return; }
    int bad = 0, probed = 0;
    for (DWORD k = 0; k < gs->cRanges && probed < 400; k++) {
        WCRANGE *rg = &gs->ranges[k];
        WCHAR c[2] = { rg->wcLow, (WCHAR)(rg->wcLow + rg->cGlyphs - 1) };
        WORD gi[2];
        if (fm.GetGlyphIndicesW(w.dc, c, 2, gi, GGI_MARK_NONEXISTING_GLYPHS) != 2) { bad++; continue; }
        if (gi[0] == 0xFFFF || gi[1] == 0xFFFF) bad++;
        probed++;
    }
    CHECKF(bad == 0, "%d range edges reported missing by GetGlyphIndicesW", bad);
    CHECK(probed > 0);
    free(gs); fd_close(&w);
}

TEST(gdi_font_metrics_text_ranges_invalid_and_bitmap) {
    FM_BEGIN();
    EXPECT_EQ(fm.GetFontUnicodeRanges(NULL, NULL), 0);
    EXPECT_EQ(fm.GetFontUnicodeRanges((HDC)(ULONG_PTR)0x2020, NULL), 0);
    HDC dc = fm_dc(); if (!dc) return;
    HGDIOBJ old = fm.SelectObject(dc, fm.GetStockObject(SYSTEM_FONT));
    DWORD n = fm.GetFontUnicodeRanges(dc, NULL);
    t_note("GetFontUnicodeRanges on the System bitmap font -> %lu", n);
    fm.SelectObject(dc, old); fm_freedc(dc);
}

TEST(gdi_font_metrics_text_ranges_direct_stub_matches) {
    FM_BEGIN(); FM_NEED(NtGdiGetFontUnicodeRanges_);
    struct fd w; if (!fd_open(&w, L"Tahoma", -16, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    ULONG n = NtGdiGetFontUnicodeRanges_(w.dc, NULL);
    EXPECT_EQ(n, fm.GetFontUnicodeRanges(w.dc, NULL));
    CHECK(n >= sizeof(GLYPHSET));
    GLYPHSET *gs = calloc(1, n + 16);
    EXPECT_EQ(NtGdiGetFontUnicodeRanges_(w.dc, gs), n);
    EXPECT_EQ(gs->cbThis, n);
    EXPECT_EQ(NtGdiGetFontUnicodeRanges_(NULL, gs), 0);
    free(gs); fd_close(&w);
}

TEST(gdi_font_metrics_text_ranges_symbol_font_marlett) {
    FM_BEGIN();
    struct fd w; if (!fd_open(&w, L"Marlett", -16, 400, SYMBOL_CHARSET)) { t_skip("setup"); return; }
    WCHAR face[40]; fm_face(w.dc, face, 40);
    if (!fm_wcsieq(face, L"Marlett")) { t_skip("no Marlett"); fd_close(&w); return; }
    DWORD n = fm.GetFontUnicodeRanges(w.dc, NULL);
    CHECK(n >= sizeof(GLYPHSET) && n != GDI_ERROR);
    GLYPHSET *gs = calloc(1, n + 16);
    if (fm.GetFontUnicodeRanges(w.dc, gs) == n) {
        t_note("Marlett: %lu glyphs in %lu ranges", (unsigned long)gs->cGlyphsSupported, (unsigned long)gs->cRanges);
        CHECK(gs->cGlyphsSupported > 0);
    }
    free(gs); fd_close(&w);
}

/* ---- Marlett symbols ---- */

TEST(gdi_font_metrics_text_marlett_caption_glyphs_present) {
    FM_BEGIN();
    struct fd w; if (!fd_open(&w, L"Marlett", -16, 400, SYMBOL_CHARSET)) { t_skip("setup"); return; }
    WCHAR face[40]; fm_face(w.dc, face, 40);
    if (!fm_wcsieq(face, L"Marlett")) { t_skip("no Marlett"); fd_close(&w); return; }
    /* The caption button glyphs: '0' minimise, '1' maximise, '2' restore, 'r' close. A symbol font is addressed either at
       the plain code or in the U+F0xx private range. */
    static const WCHAR sym[] = { '0', '1', '2', 'r' };
    for (unsigned i = 0; i < 4; i++) {
        WCHAR c[2] = { sym[i], (WCHAR)(0xF000 + sym[i]) };
        WORD gi[2];
        EXPECT_EQ(fm.GetGlyphIndicesW(w.dc, c, 2, gi, GGI_MARK_NONEXISTING_GLYPHS), 2);
        CHECKF(gi[0] != 0xFFFF || gi[1] != 0xFFFF, "Marlett symbol '%c' missing", sym[i]);
        INT adv = 0; fm.GetCharWidth32W(w.dc, sym[i], sym[i], &adv);
        CHECKF(adv > 0, "Marlett '%c' advance %d", sym[i], adv);
    }
    fd_close(&w);
}

TEST(gdi_font_metrics_text_marlett_glyphs_are_square_ish) {
    FM_BEGIN();
    struct fd w; if (!fd_open(&w, L"Marlett", -32, 400, SYMBOL_CHARSET)) { t_skip("setup"); return; }
    WCHAR face[40]; fm_face(w.dc, face, 40);
    if (!fm_wcsieq(face, L"Marlett")) { t_skip("no Marlett"); fd_close(&w); return; }
    GLYPHMETRICS gm; MAT2 id = { {0, 1}, {0, 0}, {0, 0}, {0, 1} };
    DWORD r = fm.GetGlyphOutlineW(w.dc, 'r', GGO_METRICS, &gm, 0, NULL, &id);
    if (r == GDI_ERROR) r = fm.GetGlyphOutlineW(w.dc, 0xF072, GGO_METRICS, &gm, 0, NULL, &id);
    CHECK(r != GDI_ERROR);
    if (r != GDI_ERROR) { CHECK(gm.gmBlackBoxX > 0 && gm.gmBlackBoxY > 0); CHECK(gm.gmCellIncX > 0); t_note("Marlett 'r' box %ux%u adv %d", gm.gmBlackBoxX, gm.gmBlackBoxY, gm.gmCellIncX); }
    fd_close(&w);
}

/* ---- kerning pairs ---- */

TEST(gdi_font_metrics_text_kerning_count_query_and_fill) {
    FM_BEGIN();
    struct fd w; if (!fd_open(&w, L"Times New Roman", -20, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    DWORD n = fm.GetKerningPairsW(w.dc, 0, NULL);
    t_note("Times New Roman kerning pair count: %lu", n);
    if (n == 0) { t_skip("font reports no kerning pairs"); fd_close(&w); return; }
    KERNINGPAIR *kp = calloc(n + 4, sizeof *kp);
    EXPECT_EQ(fm.GetKerningPairsW(w.dc, n, kp), n);
    EXPECT_EQ(fm.GetKerningPairsW(w.dc, n + 3, kp), n);          /* larger buffer: still n */
    int zero = 0; for (DWORD i = 0; i < n; i++) if (kp[i].iKernAmount == 0) zero++;
    t_note("%d of %lu pairs have a zero kern amount", zero, n);
    for (DWORD i = 0; i < n; i++) CHECK(abs(kp[i].iKernAmount) < 200);
    free(kp); fd_close(&w);
}

TEST(gdi_font_metrics_text_kerning_buffer_too_small) {
    FM_BEGIN();
    struct fd w; if (!fd_open(&w, L"Times New Roman", -20, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    DWORD n = fm.GetKerningPairsW(w.dc, 0, NULL);
    if (n < 2) { t_skip("font has fewer than two pairs"); fd_close(&w); return; }
    KERNINGPAIR kp[2];
    memset(kp, 0x66, sizeof kp);
    /* A buffer smaller than the pair count is filled as far as it goes: one pair is written and one is reported. */
    EXPECT_EQ(fm.GetKerningPairsW(w.dc, 1, kp), 1);
    CHECK(memcmp(&kp[0], (const char[sizeof kp[0]]){ 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66 }, sizeof kp[0]) != 0);
    EXPECT_EQ(((unsigned char *)&kp[1])[0], 0x66);       /* the second slot is untouched */
    fd_close(&w);
}

TEST(gdi_font_metrics_text_kerning_invalid_args) {
    FM_BEGIN();
    struct fd w; if (!fd_open(&w, L"Times New Roman", -20, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    KERNINGPAIR kp[2];
    SetLastError(0);
    EXPECT_EQ(fm.GetKerningPairsW(w.dc, 0, kp), 0);          /* gdi32full: nPairs==0 with a buffer -> ERROR_INVALID_PARAMETER */
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    EXPECT_EQ(fm.GetKerningPairsW(NULL, 0, NULL), 0);
    EXPECT_EQ(fm.GetKerningPairsW((HDC)(ULONG_PTR)0x1717, 0, NULL), 0);
    fd_close(&w);
}

TEST(gdi_font_metrics_text_kerning_huge_count_rejected) {
    FM_BEGIN(); FM_NEED(NtGdiGetKerningPairs_);
    struct fd w; if (!fd_open(&w, L"Times New Roman", -20, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    KERNINGPAIR kp[2];
    if (t_is_wine()) { t_skip("Wine honours the oversized count and would overrun the buffer"); fd_close(&w); return; }
    /* nPairs >= 0x4e2001 skips the temp-buffer allocation, so with a non-NULL buffer the call yields 0 and never touches it. */
    EXPECT_EQ(NtGdiGetKerningPairs_(w.dc, 0x4e2001, kp), 0);
    EXPECT_EQ(NtGdiGetKerningPairs_(w.dc, 0xFFFFFFFF, kp), 0);
    DWORD n = NtGdiGetKerningPairs_(w.dc, 0, NULL);
    EXPECT_EQ(n, fm.GetKerningPairsW(w.dc, 0, NULL));
    fd_close(&w);
}

TEST(gdi_font_metrics_text_kerning_bitmap_font_none) {
    FM_BEGIN();
    HDC dc = fm_dc(); if (!dc) return;
    HGDIOBJ old = fm.SelectObject(dc, fm.GetStockObject(OEM_FIXED_FONT));
    EXPECT_EQ(fm.GetKerningPairsW(dc, 0, NULL), 0);          /* Terminal has no kerning table */
    fm.SelectObject(dc, old); fm_freedc(dc);
}

TEST(gdi_font_metrics_text_kerning_consolas_and_courier_note) {
    FM_BEGIN();
    static const WCHAR *faces[] = { L"Courier New", L"Consolas", L"Arial", L"Tahoma" };
    for (unsigned i = 0; i < 4; i++) {
        struct fd w; if (!fm_face_present(faces[i]) || !fd_open(&w, faces[i], -20, 400, ANSI_CHARSET)) continue;
        char u[40]; fm_u8(faces[i], u, sizeof u);
        t_note("%s: %lu kerning pairs", u, fm.GetKerningPairsW(w.dc, 0, NULL));
        fd_close(&w);
    }
}

/* ---- glyph outlines ---- */

static const MAT2 k_id = { {0, 1}, {0, 0}, {0, 0}, {0, 1} };

TEST(gdi_font_metrics_text_outline_requires_gm_and_mat2) {
    FM_BEGIN();
    struct fd w; if (!fd_open(&w, L"Arial", -48, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    GLYPHMETRICS gm; MAT2 m = k_id;
    /* gdi32full!GetGlyphOutlineInternalW returns -1 before any syscall unless both gm and mat2 are non-NULL. */
    EXPECT_EQ(fm.GetGlyphOutlineW(w.dc, 'A', GGO_METRICS, NULL, 0, NULL, &m), GDI_ERROR);
    EXPECT_EQ(fm.GetGlyphOutlineW(w.dc, 'A', GGO_METRICS, &gm, 0, NULL, NULL), GDI_ERROR);
    EXPECT_EQ(fm.GetGlyphOutlineW(w.dc, 'A', GGO_NATIVE, NULL, 0, NULL, NULL), GDI_ERROR);
    fd_close(&w);
}

TEST(gdi_font_metrics_text_outline_invalid_dc) {
    FM_BEGIN();
    GLYPHMETRICS gm; MAT2 m = k_id;
    EXPECT_EQ(fm.GetGlyphOutlineW(NULL, 'A', GGO_METRICS, &gm, 0, NULL, &m), GDI_ERROR);
    EXPECT_EQ(fm.GetGlyphOutlineW((HDC)(ULONG_PTR)0x2468, 'A', GGO_NATIVE, &gm, 0, NULL, &m), GDI_ERROR);
}

TEST(gdi_font_metrics_text_outline_metrics_relations_for_A) {
    FM_BEGIN();
    static const WCHAR *faces[] = { L"Arial", L"Times New Roman", L"Tahoma", L"Courier New" };
    for (unsigned i = 0; i < 4; i++) {
        if (!fm_face_present(faces[i])) continue;
        struct fd w; if (!fd_open(&w, faces[i], -100, 400, ANSI_CHARSET)) continue;
        GLYPHMETRICS gm; memset(&gm, 0xCC, sizeof gm); MAT2 m = k_id;
        DWORD r = fm.GetGlyphOutlineW(w.dc, 'A', GGO_METRICS, &gm, 0, NULL, &m);
        CHECKF(r != GDI_ERROR, "face %u", i);
        if (r == GDI_ERROR) { fd_close(&w); continue; }
        t_note("face %u 'A' @100px: box %ux%u origin (%ld,%ld) cell (%d,%d) ret %lu", i, gm.gmBlackBoxX, gm.gmBlackBoxY, gm.gmptGlyphOrigin.x, gm.gmptGlyphOrigin.y, gm.gmCellIncX, gm.gmCellIncY, r);
        CHECK(gm.gmBlackBoxX > 20 && gm.gmBlackBoxX < 120);
        CHECK(gm.gmBlackBoxY > 40 && gm.gmBlackBoxY < 110);
        CHECK(gm.gmCellIncX > 0);
        EXPECT_EQ(gm.gmCellIncY, 0);                                   /* horizontal text */
        CHECKF(labs(gm.gmptGlyphOrigin.y - (long)gm.gmBlackBoxY) <= 2, "face %u: origin.y %ld box height %u (A sits on the baseline)", i, gm.gmptGlyphOrigin.y, gm.gmBlackBoxY);
        INT adv; fm.GetCharWidth32W(w.dc, 'A', 'A', &adv);
        CHECKF(abs(gm.gmCellIncX - adv) <= 1, "face %u: cell %d vs advance %d", i, gm.gmCellIncX, adv);
        ABC abc; fm.GetCharABCWidthsW(w.dc, 'A', 'A', &abc);
        CHECKF(labs((long)gm.gmBlackBoxX - (long)abc.abcB) <= 1, "face %u: box %u vs abcB %u", i, gm.gmBlackBoxX, abc.abcB);
        CHECKF(labs(gm.gmptGlyphOrigin.x - abc.abcA) <= 1, "face %u: origin.x %ld vs abcA %d", i, gm.gmptGlyphOrigin.x, abc.abcA);
        fd_close(&w);
    }
}

TEST(gdi_font_metrics_text_outline_glyph_index_equals_char) {
    FM_BEGIN();
    struct fd w; if (!fd_open(&w, L"Arial", -80, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    WORD gi; fm.GetGlyphIndicesW(w.dc, L"g", 1, &gi, 0);
    GLYPHMETRICS a, b; MAT2 m = k_id;
    DWORD ra = fm.GetGlyphOutlineW(w.dc, 'g', GGO_METRICS, &a, 0, NULL, &m);
    DWORD rb = fm.GetGlyphOutlineW(w.dc, gi, GGO_METRICS | GGO_GLYPH_INDEX, &b, 0, NULL, &m);
    CHECK(ra != GDI_ERROR && rb != GDI_ERROR);
    CHECK(!memcmp(&a, &b, sizeof a));
    DWORD sa = fm.GetGlyphOutlineW(w.dc, 'g', GGO_NATIVE, &a, 0, NULL, &m);
    DWORD sb = fm.GetGlyphOutlineW(w.dc, gi, GGO_NATIVE | GGO_GLYPH_INDEX, &b, 0, NULL, &m);
    EXPECT_EQ(sa, sb);
    fd_close(&w);
}

/* Walks a GGO_NATIVE/BEZIER buffer; returns the number of polygons, or -1 when malformed. */
static int walk_outline(const unsigned char *b, DWORD n, int bezier, int *prims_seen) {
    DWORD pos = 0; int polys = 0;
    *prims_seen = 0;
    while (pos < n) {
        if (n - pos < sizeof(TTPOLYGONHEADER)) return -1;
        const TTPOLYGONHEADER *h = (const TTPOLYGONHEADER *)(b + pos);
        if (h->dwType != TT_POLYGON_TYPE) return -1;
        if (h->cb < sizeof *h || pos + h->cb > n) return -1;
        DWORD cpos = pos + sizeof *h, end = pos + h->cb;
        while (cpos < end) {
            if (end - cpos < 4) return -1;
            const TTPOLYCURVE *c = (const TTPOLYCURVE *)(b + cpos);
            if (bezier) { if (c->wType != TT_PRIM_LINE && c->wType != TT_PRIM_CSPLINE) return -1; }
            else if (c->wType != TT_PRIM_LINE && c->wType != TT_PRIM_QSPLINE) return -1;
            if (c->cpfx == 0) return -1;
            if (c->wType == TT_PRIM_CSPLINE && (c->cpfx % 3)) return -1;
            DWORD sz = 4 + c->cpfx * 8;
            if (cpos + sz > end) return -1;
            *prims_seen |= 1 << c->wType;
            cpos += sz;
        }
        if (cpos != end) return -1;
        pos = end; polys++;
    }
    return polys;
}

TEST(gdi_font_metrics_text_outline_native_structure) {
    FM_BEGIN();
    static const WCHAR chars[] = { 'A', 'B', 'O', 'g', 'e', '8', '@', 'i' };
    static const WCHAR *faces[] = { L"Arial", L"Times New Roman" };
    for (unsigned f = 0; f < 2; f++) {
        if (!fm_face_present(faces[f])) continue;
        struct fd w; if (!fd_open(&w, faces[f], -64, 400, ANSI_CHARSET)) continue;
        for (unsigned i = 0; i < sizeof chars / sizeof *chars; i++) {
            GLYPHMETRICS gm; MAT2 m = k_id;
            DWORD n = fm.GetGlyphOutlineW(w.dc, chars[i], GGO_NATIVE, &gm, 0, NULL, &m);   /* size query */
            CHECKF(n != GDI_ERROR && n >= sizeof(TTPOLYGONHEADER) + 4, "face %u char %c size 0x%lx", f, chars[i], n);
            if (n == GDI_ERROR || n > 100000) continue;
            unsigned char *b = calloc(1, n + 32); memset(b, 0xEE, n + 32);
            GLYPHMETRICS gm2;
            DWORD r = fm.GetGlyphOutlineW(w.dc, chars[i], GGO_NATIVE, &gm2, n, b, &m);
            EXPECT_EQ(r, n);
            CHECK(!memcmp(&gm, &gm2, sizeof gm));
            for (DWORD j = n; j < n + 32; j++) if (b[j] != 0xEE) { CHECKF(0, "face %u char %c wrote past the buffer", f, chars[i]); break; }
            int seen; int polys = walk_outline(b, n, 0, &seen);
            CHECKF(polys > 0, "face %u char %c: malformed TTPOLYGONHEADER chain", f, chars[i]);
            if (chars[i] == 'O' || chars[i] == '8') CHECK(polys >= 2);           /* counters */
            if (chars[i] == 'i') CHECK(polys >= 2);                              /* stem and dot */
            free(b);
        }
        fd_close(&w);
    }
}

TEST(gdi_font_metrics_text_outline_bezier_structure) {
    FM_BEGIN();
    struct fd w; if (!fd_open(&w, L"Arial", -64, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    static const WCHAR chars[] = { 'A', 'O', 'S', 'g' };
    for (unsigned i = 0; i < 4; i++) {
        GLYPHMETRICS gm; MAT2 m = k_id;
        DWORD n = fm.GetGlyphOutlineW(w.dc, chars[i], GGO_BEZIER, &gm, 0, NULL, &m);
        CHECKF(n != GDI_ERROR && n > sizeof(TTPOLYGONHEADER), "char %c", chars[i]);
        if (n == GDI_ERROR || n > 100000) continue;
        unsigned char *b = calloc(1, n + 8);
        EXPECT_EQ(fm.GetGlyphOutlineW(w.dc, chars[i], GGO_BEZIER, &gm, n, b, &m), n);
        int seen; int polys = walk_outline(b, n, 1, &seen);
        CHECKF(polys > 0, "char %c: malformed bezier chain (only TT_PRIM_LINE/CSPLINE allowed)", chars[i]);
        if (chars[i] == 'O' || chars[i] == 'S') CHECK(seen & (1 << TT_PRIM_CSPLINE));
        free(b);
    }
    fd_close(&w);
}

TEST(gdi_font_metrics_text_outline_native_first_point_is_start) {
    FM_BEGIN();
    struct fd w; if (!fd_open(&w, L"Arial", -100, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    GLYPHMETRICS gm; MAT2 m = k_id;
    DWORD n = fm.GetGlyphOutlineW(w.dc, 'A', GGO_NATIVE, &gm, 0, NULL, &m);
    if (n == GDI_ERROR) { CHECK(0); fd_close(&w); return; }
    unsigned char *b = calloc(1, n + 8);
    fm.GetGlyphOutlineW(w.dc, 'A', GGO_NATIVE, &gm, n, b, &m);
    const TTPOLYGONHEADER *h = (const TTPOLYGONHEADER *)b;
    EXPECT_EQ(h->dwType, TT_POLYGON_TYPE);
    /* POINTFX values are 16.16 fixed; the start point is inside the glyph box (x within box, y up to the cap height). */
    long sx = h->pfxStart.x.value, sy = h->pfxStart.y.value;
    CHECK(sx >= -2 && sx <= (long)gm.gmCellIncX + 2);
    CHECK(sy >= -2 && sy <= 110);
    free(b); fd_close(&w);
}

TEST(gdi_font_metrics_text_outline_native_requires_truetype) {
    FM_BEGIN();
    HDC dc = fm_dc(); if (!dc) return;
    HGDIOBJ old = fm.SelectObject(dc, fm.GetStockObject(SYSTEM_FONT));
    GLYPHMETRICS gm; MAT2 m = k_id;
    TEXTMETRICW tm; fm.GetTextMetricsW(dc, &tm);
    DWORD r = fm.GetGlyphOutlineW(dc, 'A', GGO_NATIVE, &gm, 0, NULL, &m);
    if (tm.tmPitchAndFamily & TMPF_TRUETYPE) t_skip("System font is TrueType");
    else EXPECT_EQ(r, GDI_ERROR);                                       /* native outlines exist only for TrueType fonts */
    fm.SelectObject(dc, old); fm_freedc(dc);
}

TEST(gdi_font_metrics_text_outline_bitmap_size_formulas) {
    FM_BEGIN();
    struct fd w; if (!fd_open(&w, L"Arial", -40, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    static const struct { UINT fmt; int bits; unsigned maxv; const char *n; } t[] = {
        { GGO_BITMAP, 1, 1, "BITMAP" }, { GGO_GRAY2_BITMAP, 8, 4, "GRAY2" }, { GGO_GRAY4_BITMAP, 8, 16, "GRAY4" }, { GGO_GRAY8_BITMAP, 8, 64, "GRAY8" } };
    static const WCHAR chars[] = { 'A', 'W', 'g', '.', 'M' };
    for (unsigned c = 0; c < sizeof chars / sizeof *chars; c++) for (unsigned i = 0; i < 4; i++) {
        GLYPHMETRICS gm; MAT2 m = k_id;
        DWORD n = fm.GetGlyphOutlineW(w.dc, chars[c], t[i].fmt, &gm, 0, NULL, &m);
        CHECKF(n != GDI_ERROR, "%s char %c", t[i].n, chars[c]);
        if (n == GDI_ERROR) continue;
        /* Rows are DWORD aligned: 1 bit per pixel for BITMAP, 1 byte per pixel for the gray formats. */
        DWORD pitch = t[i].bits == 1 ? ((gm.gmBlackBoxX + 31) / 32) * 4 : ((gm.gmBlackBoxX + 3) / 4) * 4;
        EXPECT_EQ(n, pitch * gm.gmBlackBoxY);
        if (n == 0 || n > 100000) continue;
        unsigned char *b = calloc(1, n + 16); memset(b, 0xEE, n + 16);
        GLYPHMETRICS g2;
        EXPECT_EQ(fm.GetGlyphOutlineW(w.dc, chars[c], t[i].fmt, &g2, n, b, &m), n);
        for (DWORD j = n; j < n + 16; j++) if (b[j] != 0xEE) { CHECKF(0, "%s overran its buffer", t[i].n); break; }
        if (t[i].bits == 8) {
            int over = 0, nonzero = 0;
            for (DWORD y = 0; y < gm.gmBlackBoxY; y++) for (DWORD x = 0; x < gm.gmBlackBoxX; x++) { unsigned v = b[y * pitch + x]; if (v > t[i].maxv) over++; if (v) nonzero++; }
            CHECKF(over == 0, "%s char %c: %d samples above %u", t[i].n, chars[c], over, t[i].maxv);
            CHECKF(nonzero > 0, "%s char %c: empty bitmap", t[i].n, chars[c]);
        } else {
            int nonzero = 0; for (DWORD j = 0; j < n; j++) if (b[j]) nonzero++;
            CHECKF(nonzero > 0, "BITMAP char %c: empty", chars[c]);
        }
        free(b);
    }
    fd_close(&w);
}

TEST(gdi_font_metrics_text_outline_bitmap_gray_levels_used) {
    FM_BEGIN();
    struct fd w; if (!fd_open(&w, L"Arial", -40, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    GLYPHMETRICS gm; MAT2 m = k_id;
    DWORD n = fm.GetGlyphOutlineW(w.dc, 'O', GGO_GRAY8_BITMAP, &gm, 0, NULL, &m);
    if (n == GDI_ERROR || n == 0) { CHECK(0); fd_close(&w); return; }
    unsigned char *b = calloc(1, n);
    fm.GetGlyphOutlineW(w.dc, 'O', GGO_GRAY8_BITMAP, &gm, n, b, &m);
    int hist[65] = { 0 };
    for (DWORD j = 0; j < n; j++) if (b[j] <= 64) hist[b[j]]++;
    int distinct = 0; for (int k = 1; k <= 64; k++) if (hist[k]) distinct++;
    CHECKF(distinct >= 4, "an anti-aliased 'O' shows %d distinct gray levels", distinct);
    CHECK(hist[64] > 0);                 /* fully covered pixels exist at 40 px */
    free(b); fd_close(&w);
}

TEST(gdi_font_metrics_text_outline_buffer_too_small) {
    FM_BEGIN();
    struct fd w; if (!fd_open(&w, L"Arial", -48, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    GLYPHMETRICS gm; MAT2 m = k_id;
    DWORD n = fm.GetGlyphOutlineW(w.dc, 'B', GGO_NATIVE, &gm, 0, NULL, &m);
    if (n == GDI_ERROR || n < 8) { t_skip("no outline"); fd_close(&w); return; }
    unsigned char *b = malloc(n + 16); memset(b, 0xEE, n + 16);
    DWORD r = fm.GetGlyphOutlineW(w.dc, 'B', GGO_NATIVE, &gm, n - 1, b, &m);
    t_note("GetGlyphOutline with a %lu byte buffer for a %lu byte outline -> 0x%lx", n - 1, n, r);
    for (DWORD j = n - 1; j < n + 16; j++) CHECKF(b[j] == 0xEE, "byte %lu past the declared buffer written", j);
    free(b); fd_close(&w);
}

TEST(gdi_font_metrics_text_outline_space_has_empty_outline) {
    FM_BEGIN();
    struct fd w; if (!fd_open(&w, L"Arial", -48, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    GLYPHMETRICS gm; MAT2 m = k_id;
    DWORD n = fm.GetGlyphOutlineW(w.dc, ' ', GGO_NATIVE, &gm, 0, NULL, &m);
    t_note("space GGO_NATIVE size query -> 0x%lx; cell %d", n, gm.gmCellIncX);
    CHECK(n != GDI_ERROR);
    if (n != 0) t_note("space GGO_NATIVE returned %lu bytes", n);
    INT adv; fm.GetCharWidth32W(w.dc, ' ', ' ', &adv);
    CHECK(abs(gm.gmCellIncX - adv) <= 1);
    fd_close(&w);
}

TEST(gdi_font_metrics_text_outline_mat2_scales_box) {
    FM_BEGIN();
    struct fd w; if (!fd_open(&w, L"Arial", -60, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    GLYPHMETRICS g1, g2, g3; MAT2 id = k_id;
    MAT2 dbl = { {0, 2}, {0, 0}, {0, 0}, {0, 2} };
    MAT2 xonly = { {0, 2}, {0, 0}, {0, 0}, {0, 1} };
    CHECK(fm.GetGlyphOutlineW(w.dc, 'H', GGO_METRICS, &g1, 0, NULL, &id) != GDI_ERROR);
    CHECK(fm.GetGlyphOutlineW(w.dc, 'H', GGO_METRICS, &g2, 0, NULL, &dbl) != GDI_ERROR);
    CHECK(fm.GetGlyphOutlineW(w.dc, 'H', GGO_METRICS, &g3, 0, NULL, &xonly) != GDI_ERROR);
    CHECKF(labs((long)g2.gmBlackBoxX - 2L * g1.gmBlackBoxX) <= 3, "x: %u vs 2*%u", g2.gmBlackBoxX, g1.gmBlackBoxX);
    CHECKF(labs((long)g2.gmBlackBoxY - 2L * g1.gmBlackBoxY) <= 3, "y: %u vs 2*%u", g2.gmBlackBoxY, g1.gmBlackBoxY);
    CHECKF(labs((long)g3.gmBlackBoxX - 2L * g1.gmBlackBoxX) <= 3, "xonly x: %u", g3.gmBlackBoxX);
    CHECKF(labs((long)g3.gmBlackBoxY - (long)g1.gmBlackBoxY) <= 2, "xonly y: %u vs %u", g3.gmBlackBoxY, g1.gmBlackBoxY);
    CHECKF(labs(g2.gmCellIncX - 2 * g1.gmCellIncX) <= 3, "cell %d vs 2*%d", g2.gmCellIncX, g1.gmCellIncX);
    fd_close(&w);
}

TEST(gdi_font_metrics_text_outline_scales_with_font_size) {
    FM_BEGIN();
    LONG prev = 0;
    for (int h = 16; h <= 128; h *= 2) {
        struct fd w; if (!fd_open(&w, L"Arial", -h, 400, ANSI_CHARSET)) continue;
        GLYPHMETRICS gm; MAT2 m = k_id;
        CHECK(fm.GetGlyphOutlineW(w.dc, 'H', GGO_METRICS, &gm, 0, NULL, &m) != GDI_ERROR);
        CHECKF(gm.gmBlackBoxY > prev, "size %d box height %u after %ld", h, gm.gmBlackBoxY, prev);
        CHECKF(labs((long)gm.gmBlackBoxY - h * 716 / 1000) <= 2 + h / 32, "size %d: 'H' height %u (Arial cap height is about 0.716 em)", h, gm.gmBlackBoxY);
        prev = gm.gmBlackBoxY;
        fd_close(&w);
    }
}

TEST(gdi_font_metrics_text_outline_direct_stub_matches) {
    FM_BEGIN(); FM_NEED(NtGdiGetGlyphOutline_);
    struct fd w; if (!fd_open(&w, L"Arial", -50, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    GLYPHMETRICS a, b; MAT2 m = k_id;
    int ra = NtGdiGetGlyphOutline_(w.dc, 'R', GGO_NATIVE, &a, 0, NULL, &m, FALSE);
    DWORD rb = fm.GetGlyphOutlineW(w.dc, 'R', GGO_NATIVE, &b, 0, NULL, &m);
    EXPECT_EQ((DWORD)ra, rb);
    CHECK(!memcmp(&a, &b, sizeof a));
    CHECK(ra > 0);
    /* cb above 0x2710000 never allocates the kernel buffer: the call fails with -1 and does not touch the buffer. */
    unsigned char tiny[8];
    if (t_is_wine()) { t_skip("Wine honours the oversized cb and would overrun the buffer"); fd_close(&w); return; }
    EXPECT_EQ((DWORD)NtGdiGetGlyphOutline_(w.dc, 'R', GGO_NATIVE, &a, 0x2710001, tiny, &m, FALSE), GDI_ERROR);
    EXPECT_EQ((DWORD)NtGdiGetGlyphOutline_(NULL, 'R', GGO_NATIVE, &a, 0, NULL, &m, FALSE), GDI_ERROR);
    fd_close(&w);
}

TEST(gdi_font_metrics_text_outline_bad_format_bits) {
    FM_BEGIN();
    struct fd w; if (!fd_open(&w, L"Arial", -50, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    GLYPHMETRICS gm; MAT2 m = k_id;
    static const UINT bad[] = { 7, 8, 9, 0x10, 0x7f };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        DWORD r = fm.GetGlyphOutlineW(w.dc, 'A', bad[i], &gm, 0, NULL, &m);
        t_note("GetGlyphOutline format 0x%x -> 0x%lx", bad[i], r);
    }
    fd_close(&w);
}

TEST(gdi_font_metrics_text_outline_unmapped_char_gives_default_glyph) {
    FM_BEGIN();
    struct fd w; if (!fd_open(&w, L"Arial", -50, 400, ANSI_CHARSET)) { t_skip("setup"); return; }
    GLYPHMETRICS a, b; MAT2 m = k_id;
    DWORD ra = fm.GetGlyphOutlineW(w.dc, 0x0378, GGO_METRICS, &a, 0, NULL, &m);
    t_note("unassigned U+0378 GGO_METRICS -> 0x%lx, box %ux%u", ra, a.gmBlackBoxX, a.gmBlackBoxY);
    /* Index 0 is the font's .notdef glyph; asking for it by index must succeed. */
    DWORD rb = fm.GetGlyphOutlineW(w.dc, 0, GGO_METRICS | GGO_GLYPH_INDEX, &b, 0, NULL, &m);
    CHECK(rb != GDI_ERROR);
    fd_close(&w);
}
