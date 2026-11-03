/* gap9_glyph* / gap9_otm*: NtGdiGetGlyphOutline and NtGdiGetOutlineTextMetricsInternalW (win32kfull).
 *  GetGlyphOutline: cbBuffer in 1..0x2710000 allocates a temp buffer, 0 is the size query, anything bigger
 *    fails (-1) before the font is touched; the GLYPHMETRICS is stored only on success.
 *  GetOutlineTextMetrics: cb==0 or pbuf==NULL is the size query; cb above 0x2710000 fails with 0. */
#include "g9.h"

UDECL(DWORD, NtGdiGetGlyphOutline, (HDC, WCHAR, UINT, GLYPHMETRICS *, ULONG, PVOID, const MAT2 *, BOOL));
UDECL(UINT, NtGdiGetOutlineTextMetricsInternalW, (HDC, ULONG, OUTLINETEXTMETRICW *, void *tmdiff));

static const MAT2 ident = { {0, 1}, {0, 0}, {0, 0}, {0, 1} };

struct fctx { struct g9_dib d; HFONT f, old; int tt; };
static int font_open(struct fctx *c, const WCHAR *face, int h) {
    memset(c, 0, sizeof *c);
    if (!U(CreateFontW) || !U(GetTextMetricsW) || !g9_dib_new(&c->d, 4, 4)) return 0;
    c->f = U(CreateFontW)(h, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_ONLY_PRECIS, 0, ANTIALIASED_QUALITY, 0, face);
    if (!c->f) return 0;
    c->old = (HFONT)U(SelectObject)(c->d.dc, c->f);
    TEXTMETRICW tm;
    c->tt = U(GetTextMetricsW)(c->d.dc, &tm) && (tm.tmPitchAndFamily & TMPF_TRUETYPE);
    return 1;
}
static void font_close(struct fctx *c) {
    if (c->d.dc && c->old) U(SelectObject)(c->d.dc, c->old);
    if (c->f) U(DeleteObject)(c->f);
    g9_dib_free(&c->d);
}
#define OPEN_TT(c) struct fctx c; if (!font_open(&c, L"Arial", -32) || !c.tt) { t_skip("no TrueType Arial"); font_close(&c); return; }

TEST(gap9_glyph_metrics_basic) {
    REQUIRE(U(GetGlyphOutlineW)); OPEN_TT(c);
    GLYPHMETRICS gm; memset(&gm, 0, sizeof gm);
    DWORD r = U(GetGlyphOutlineW)(c.d.dc, 'A', GGO_METRICS, &gm, 0, NULL, &ident);
    CHECK(r != GDI_ERROR);
    CHECK(gm.gmBlackBoxX > 0 && gm.gmBlackBoxY > 0 && gm.gmCellIncX > 0);
    CHECK(gm.gmptGlyphOrigin.y > 0);                        /* 'A' sits above the baseline */
    CHECK(gm.gmCellIncY == 0);
    int w = 0;
    if (U(GetCharWidth32W) && U(GetCharWidth32W)(c.d.dc, 'A', 'A', &w)) t_note("cellIncX %d GetCharWidth32 %d", gm.gmCellIncX, w);
    font_close(&c);
}

TEST(gap9_glyph_native_size_protocol) {
    REQUIRE(U(GetGlyphOutlineW)); OPEN_TT(c);
    GLYPHMETRICS gm;
    DWORD sz = U(GetGlyphOutlineW)(c.d.dc, 'A', GGO_NATIVE, &gm, 0, NULL, &ident);
    CHECK(sz != GDI_ERROR && sz >= 2 * sizeof(TTPOLYGONHEADER));    /* two contours for 'A' */
    BYTE *buf = (BYTE *)calloc(1, sz + 16);
    if (!buf || sz == GDI_ERROR) { free(buf); font_close(&c); return; }
    memset(buf + sz, 0xCD, 16);
    DWORD r = U(GetGlyphOutlineW)(c.d.dc, 'A', GGO_NATIVE, &gm, sz, buf, &ident);
    EXPECT_EQ(r, sz);
    for (int i = 0; i < 16; i++) if (buf[sz + i] != 0xCD) { CHECKF(0, "byte %d past the buffer overwritten", i); break; }
    /* the buffer is a sequence of TTPOLYGONHEADER records whose cb fields tile the whole result */
    DWORD pos = 0; int polys = 0, bad = 0;
    while (pos + sizeof(TTPOLYGONHEADER) <= sz) {
        TTPOLYGONHEADER *h = (TTPOLYGONHEADER *)(buf + pos);
        if (h->dwType != TT_POLYGON_TYPE || h->cb < sizeof *h || pos + h->cb > sz) { bad = 1; break; }
        pos += h->cb; polys++;
    }
    CHECK(!bad); EXPECT_EQ(pos, sz); CHECK(polys >= 2);
    free(buf); font_close(&c);
}

TEST(gap9_glyph_native_too_small_buffer) {
    REQUIRE(U(GetGlyphOutlineW)); OPEN_TT(c);
    GLYPHMETRICS gm;
    DWORD sz = U(GetGlyphOutlineW)(c.d.dc, 'A', GGO_NATIVE, &gm, 0, NULL, &ident);
    if (sz == GDI_ERROR || sz < 8) { t_skip("no outline"); font_close(&c); return; }
    BYTE *buf = (BYTE *)malloc(sz);
    memset(buf, 0x77, sz);
    GLYPHMETRICS g2; memset(&g2, 0x5A, sizeof g2);
    DWORD r = U(GetGlyphOutlineW)(c.d.dc, 'A', GGO_NATIVE, &g2, sz - 1, buf, &ident);
    CHECK(r == GDI_ERROR);
    free(buf); font_close(&c);
}

TEST(gap9_glyph_bitmap_sizes_follow_pitch_rule) {
    REQUIRE(U(GetGlyphOutlineW)); OPEN_TT(c);
    static const struct { UINT fmt; int dword_pitch; const char *n; } t[] = {
        { GGO_BITMAP, 1, "GGO_BITMAP" }, { GGO_GRAY2_BITMAP, 0, "GRAY2" }, { GGO_GRAY4_BITMAP, 0, "GRAY4" }, { GGO_GRAY8_BITMAP, 0, "GRAY8" },
    };
    for (int i = 0; i < G9_N(t); i++) {
        GLYPHMETRICS gm; memset(&gm, 0, sizeof gm);
        DWORD sz = U(GetGlyphOutlineW)(c.d.dc, 'B', t[i].fmt, &gm, 0, NULL, &ident);
        if (sz == GDI_ERROR) { CHECKF(0, "%s: size query failed", t[i].n); continue; }
        UINT bw = gm.gmBlackBoxX, bh = gm.gmBlackBoxY;
        UINT pitch = t[i].dword_pitch ? (((bw + 31) / 32) * 4) : ((bw + 3) & ~3u);   /* rows are DWORD aligned */
        CHECKF(sz == pitch * bh, "%s: size %lu, box %ux%u pitch %u", t[i].n, sz, bw, bh, pitch);
        BYTE *buf = (BYTE *)calloc(1, sz ? sz : 1);
        GLYPHMETRICS g2;
        DWORD r = U(GetGlyphOutlineW)(c.d.dc, 'B', t[i].fmt, &g2, sz, buf, &ident);
        EXPECT_EQ(r, sz);
        int nz = 0; for (DWORD k = 0; k < sz; k++) if (buf[k]) nz++;
        CHECKF(nz > 0, "%s: bitmap is empty", t[i].n);
        CHECK(g2.gmBlackBoxX == gm.gmBlackBoxX && g2.gmBlackBoxY == gm.gmBlackBoxY);
        free(buf);
    }
    font_close(&c);
}

TEST(gap9_glyph_gray_levels_in_range) {
    REQUIRE(U(GetGlyphOutlineW)); OPEN_TT(c);
    static const struct { UINT fmt; int max; } t[] = { { GGO_GRAY2_BITMAP, 4 }, { GGO_GRAY4_BITMAP, 16 }, { GGO_GRAY8_BITMAP, 64 } };
    for (int i = 0; i < G9_N(t); i++) {
        GLYPHMETRICS gm;
        DWORD sz = U(GetGlyphOutlineW)(c.d.dc, 'O', t[i].fmt, &gm, 0, NULL, &ident);
        if (sz == GDI_ERROR || !sz) continue;
        BYTE *buf = (BYTE *)calloc(1, sz);
        U(GetGlyphOutlineW)(c.d.dc, 'O', t[i].fmt, &gm, sz, buf, &ident);
        int mx = 0; for (DWORD k = 0; k < sz; k++) if (buf[k] > mx) mx = buf[k];
        CHECKF(mx <= t[i].max, "fmt %u: level %d exceeds %d", t[i].fmt, mx, t[i].max);   /* 5, 17 and 65 level ramps */
        free(buf);
    }
    font_close(&c);
}

TEST(gap9_glyph_index_equals_char) {
    REQUIRE(U(GetGlyphOutlineW)); REQUIRE(U(GetGlyphIndicesW)); OPEN_TT(c);
    WORD gi = 0; WCHAR ch = 'A';
    if (U(GetGlyphIndicesW)(c.d.dc, &ch, 1, &gi, GGI_MARK_NONEXISTING_GLYPHS) != 1 || gi == 0xFFFF) { t_skip("no glyph index"); font_close(&c); return; }
    GLYPHMETRICS a, b; memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
    DWORD ra = U(GetGlyphOutlineW)(c.d.dc, 'A', GGO_METRICS, &a, 0, NULL, &ident);
    DWORD rb = U(GetGlyphOutlineW)(c.d.dc, gi, GGO_METRICS | GGO_GLYPH_INDEX, &b, 0, NULL, &ident);
    CHECK(ra != GDI_ERROR && rb != GDI_ERROR);
    CHECK(!memcmp(&a, &b, sizeof a));
    font_close(&c);
}

TEST(gap9_glyph_blank_char_has_no_outline) {
    REQUIRE(U(GetGlyphOutlineW)); OPEN_TT(c);
    GLYPHMETRICS gm;
    DWORD sz = U(GetGlyphOutlineW)(c.d.dc, ' ', GGO_NATIVE, &gm, 0, NULL, &ident);
    EXPECT_EQ(sz, 0);                                       /* an empty glyph is size 0, not GDI_ERROR */
    CHECK(gm.gmCellIncX > 0);
    font_close(&c);
}

TEST(gap9_glyph_invalid_dc) {
    REQUIRE(U(GetGlyphOutlineW));
    GLYPHMETRICS gm;
    CHECK(U(GetGlyphOutlineW)(NULL, 'A', GGO_METRICS, &gm, 0, NULL, &ident) == GDI_ERROR);
    CHECK(U(GetGlyphOutlineW)((HDC)(ULONG_PTR)0x1111, 'A', GGO_METRICS, &gm, 0, NULL, &ident) == GDI_ERROR);
}

TEST(gap9_glyph_direct_size_matches_user_path) {
    REQUIRE(UN(NtGdiGetGlyphOutline)); REQUIRE(U(GetGlyphOutlineW)); OPEN_TT(c);
    GLYPHMETRICS a, b;
    DWORD ru = U(GetGlyphOutlineW)(c.d.dc, 'g', GGO_NATIVE, &a, 0, NULL, &ident);
    DWORD rk = UN(NtGdiGetGlyphOutline)(c.d.dc, 'g', GGO_NATIVE, &b, 0, NULL, &ident, 0);
    CHECK(ru != GDI_ERROR); EXPECT_EQ(ru, rk);
    CHECK(!memcmp(&a, &b, sizeof a));
    font_close(&c);
}

TEST(gap9_glyph_direct_oversized_buffer_rejected) {
    REQUIRE(UN(NtGdiGetGlyphOutline)); OPEN_TT(c);
    GLYPHMETRICS gm; memset(&gm, 0x5A, sizeof gm);
    BYTE small[64]; memset(small, 0x11, sizeof small);
    /* cbBuffer - 1 must be < 0x2710000: the temp buffer is not allocated and the call fails with -1.  The
     * user buffer pointer is never dereferenced on this path. */
    DWORD r = UN(NtGdiGetGlyphOutline)(c.d.dc, 'A', GGO_NATIVE, &gm, 0x2710001, small, &ident, 0);
    CHECK(r == GDI_ERROR);
    CHECK(((BYTE *)&gm)[0] == 0x5A && ((BYTE *)&gm)[sizeof gm - 1] == 0x5A);
    CHECK(small[0] == 0x11 && small[63] == 0x11);
    r = UN(NtGdiGetGlyphOutline)(c.d.dc, 'A', GGO_NATIVE, &gm, 0xFFFFFFFF, small, &ident, 0);
    CHECK(r == GDI_ERROR);
    font_close(&c);
}

/* ---- outline text metrics --------------------------------------------------------------------- */
TEST(gap9_otm_size_query) {
    REQUIRE(U(GetOutlineTextMetricsW)); OPEN_TT(c);
    UINT need = U(GetOutlineTextMetricsW)(c.d.dc, 0, NULL);
    CHECK(need >= sizeof(OUTLINETEXTMETRICW));              /* structure plus the four name strings */
    UINT need2 = U(GetOutlineTextMetricsW)(c.d.dc, 5, NULL);
    EXPECT_EQ(need2, need);                                 /* pbuf==NULL forces the query regardless of cb */
    BYTE tmp[8] = {0};
    UINT need3 = U(GetOutlineTextMetricsW)(c.d.dc, 0, (OUTLINETEXTMETRICW *)tmp);
    EXPECT_EQ(need3, need);                                 /* cb==0 forces it too */
    font_close(&c);
}

TEST(gap9_otm_full_fetch_consistent_with_textmetrics) {
    REQUIRE(U(GetOutlineTextMetricsW)); OPEN_TT(c);
    UINT need = U(GetOutlineTextMetricsW)(c.d.dc, 0, NULL);
    if (!need) { t_skip("no OTM"); font_close(&c); return; }
    BYTE *buf = (BYTE *)malloc(need + 32);
    memset(buf, 0xCD, need + 32);
    OUTLINETEXTMETRICW *o = (OUTLINETEXTMETRICW *)buf;
    UINT r = U(GetOutlineTextMetricsW)(c.d.dc, need, o);
    CHECK(r != 0);
    EXPECT_EQ(o->otmSize, need);
    for (int i = 0; i < 32; i++) if (buf[need + i] != 0xCD) { CHECKF(0, "byte %d past the buffer written", i); break; }
    TEXTMETRICW tm; CHECK(U(GetTextMetricsW)(c.d.dc, &tm));
    CHECK(!memcmp(&o->otmTextMetrics, &tm, sizeof tm));
    CHECK(o->otmEMSquare > 0);
    CHECK(o->otmAscent > 0);
    CHECK(o->otmDescent <= 0);
    t_note("EMSquare %u asc %d desc %d lineGap %u capHeight %u", o->otmEMSquare, o->otmAscent, o->otmDescent, o->otmLineGap, o->otmsCapEmHeight);
    /* name strings are stored behind the structure and referenced by byte offset */
    ULONG_PTR offs[4] = { (ULONG_PTR)o->otmpFamilyName, (ULONG_PTR)o->otmpFaceName, (ULONG_PTR)o->otmpStyleName, (ULONG_PTR)o->otmpFullName };
    for (int i = 0; i < 4; i++) {
        CHECKF(offs[i] >= sizeof *o && offs[i] < need, "name %d offset %llu of %u", i, (unsigned long long)offs[i], need);
        if (offs[i] >= sizeof *o && offs[i] < need) {
            WCHAR *s = (WCHAR *)(buf + offs[i]);
            size_t maxw = (need - offs[i]) / 2, n = 0;
            while (n < maxw && s[n]) n++;
            CHECKF(n < maxw && n > 0, "name %d not terminated inside the buffer", i);
        }
    }
    WCHAR face[LF_FACESIZE] = {0};
    if (U(GetTextFaceW)(c.d.dc, LF_FACESIZE, face) && offs[1] < need) t_note("face \"%ls\" otm face \"%ls\"", face, (WCHAR *)(buf + offs[1]));
    free(buf); font_close(&c);
}

TEST(gap9_otm_buffer_too_small_note) {
    REQUIRE(U(GetOutlineTextMetricsW)); OPEN_TT(c);
    UINT need = U(GetOutlineTextMetricsW)(c.d.dc, 0, NULL);
    if (!need) { t_skip("no OTM"); font_close(&c); return; }
    BYTE *buf = (BYTE *)malloc(need);
    memset(buf, 0xAB, need);
    UINT r = U(GetOutlineTextMetricsW)(c.d.dc, need - 1, (OUTLINETEXTMETRICW *)buf);
    t_note("cb = need-1 (%u) returned %u", need - 1, r);
    r = U(GetOutlineTextMetricsW)(c.d.dc, sizeof(OUTLINETEXTMETRICW) - 1, (OUTLINETEXTMETRICW *)buf);
    t_note("cb = sizeof(OTM)-1 returned %u", r);
    free(buf); font_close(&c);
}

TEST(gap9_otm_raster_system_font_reports_metrics) {
    REQUIRE(U(GetOutlineTextMetricsW)); REQUIRE(U(GetStockObject));
    struct g9_dib d;
    if (!g9_dib_new(&d, 4, 4)) { t_skip("no dib"); return; }
    HGDIOBJ old = U(SelectObject)(d.dc, U(GetStockObject)(SYSTEM_FONT));
    TEXTMETRICW tm;
    if (U(GetTextMetricsW)(d.dc, &tm)) {
        UINT r = U(GetOutlineTextMetricsW)(d.dc, 0, NULL);
        /* Observed on Windows 10: even the raster System stock font reports outline metrics (a size, not 0) */
        t_note("SYSTEM_FONT pitch 0x%x, size query %u", tm.tmPitchAndFamily, r);
        CHECK(r != 0);
    }
    U(SelectObject)(d.dc, old); g9_dib_free(&d);
}

TEST(gap9_otm_invalid_dc) {
    REQUIRE(U(GetOutlineTextMetricsW));
    BYTE buf[512];
    CHECK(U(GetOutlineTextMetricsW)(NULL, 0, NULL) == 0);
    CHECK(U(GetOutlineTextMetricsW)((HDC)(ULONG_PTR)0x5151, sizeof buf, (OUTLINETEXTMETRICW *)buf) == 0);
}

TEST(gap9_otm_direct_oversized_cb_rejected) {
    if (t_is_wine()) { t_skip("Wine's 4th parameter is an option flag, not a TMDIFF pointer"); return; }
    REQUIRE(UN(NtGdiGetOutlineTextMetricsInternalW)); OPEN_TT(c);
    BYTE buf[64]; memset(buf, 0x66, sizeof buf);
    BYTE tmdiff[8]; memset(tmdiff, 0x77, sizeof tmdiff);
    /* cb > 0x2710000 never reaches the font: the temp allocation is skipped and the call returns 0 */
    UINT r = UN(NtGdiGetOutlineTextMetricsInternalW)(c.d.dc, 0x2710001, (OUTLINETEXTMETRICW *)buf, tmdiff);
    EXPECT_EQ(r, 0);
    CHECK(buf[0] == 0x66 && buf[63] == 0x66 && tmdiff[0] == 0x77 && tmdiff[7] == 0x77);
    font_close(&c);
}

TEST(gap9_otm_direct_query_matches_user_path) {
    REQUIRE(UN(NtGdiGetOutlineTextMetricsInternalW)); REQUIRE(U(GetOutlineTextMetricsW)); OPEN_TT(c);
    BYTE tmdiff[8] = {0};
    UINT k = UN(NtGdiGetOutlineTextMetricsInternalW)(c.d.dc, 0, NULL, tmdiff);
    UINT u = U(GetOutlineTextMetricsW)(c.d.dc, 0, NULL);
    CHECK(u != 0); EXPECT_EQ(k, u);
    font_close(&c);
}
