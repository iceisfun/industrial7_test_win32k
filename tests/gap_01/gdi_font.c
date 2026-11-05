/* Direct coverage of the font/text syscalls: NtGdiGetWidthTable, NtGdiGetTextExtentExW, NtGdiGetTextExtent,
 * NtGdiGetGlyphIndicesW(+Internal), NtGdiGetRealizationInfo, NtGdiGetUFI, NtGdiGetEmbUFI, NtGdiGetLinkedUFIs. */
#include "gap.h"

#define ARIAL_BEGIN(d) \
    GAP_BEGIN(); struct gap_dc d; if (!gap_dc_open(&d, L"Arial", -16)) { t_skip("no DC"); return; } \
    if (!d.font) { gap_dc_close(&d); t_skip("font creation failed"); return; }

static int is_arial(const struct gap_dc *d) { return !lstrcmpiW(d->face, L"Arial"); }

/* ---- NtGdiGetTextExtent(HDC, WCHAR *, int cwc, SIZE *, flags) -------------------------------------
 * win32kfull!NtGdiGetTextExtent: cwc < 0 returns 0 untouched; cwc == 0 returns 1 and stores a zero SIZE; a NULL string
 * (cwc > 0) or an oversize cwc (>= 0x1388001) never reaches the kernel text-extent path and returns 0. */
TEST(gap1_gettextextent_zero_length) {
    ARIAL_BEGIN(d);
    NtGdiGetTextExtent_pfn gte = UN(NtGdiGetTextExtent); REQUIRE(gte);
    SIZE sz = { 0x7777, 0x7777 };
    EXPECT_EQ(gte(d.dc, L"x", 0, &sz, 0), 1);
    EXPECT_EQ(sz.cx, 0); EXPECT_EQ(sz.cy, 0);
    gap_dc_close(&d);
}

TEST(gap1_gettextextent_negative_length) {
    ARIAL_BEGIN(d);
    NtGdiGetTextExtent_pfn gte = UN(NtGdiGetTextExtent); REQUIRE(gte);
    int negs[] = { -1, -2, -16, -0x10000, 0x80000000 };
    for (unsigned i = 0; i < sizeof negs / sizeof negs[0]; i++) {
        SIZE sz = { 0x7777, 0x7777 };
        EXPECT_EQ(gte(d.dc, L"abc", negs[i], &sz, 0), 0);
        EXPECT_EQ(sz.cx, 0x7777); EXPECT_EQ(sz.cy, 0x7777);
    }
    gap_dc_close(&d);
}

TEST(gap1_gettextextent_null_string) {
    ARIAL_BEGIN(d);
    NtGdiGetTextExtent_pfn gte = UN(NtGdiGetTextExtent); REQUIRE(gte);
    SIZE sz = { 0x7777, 0x7777 };
    EXPECT_EQ(gte(d.dc, NULL, 3, &sz, 0), 0);
    EXPECT_EQ(sz.cx, 0x7777);
    gap_dc_close(&d);
}

TEST(gap1_gettextextent_oversize_count) {
    ARIAL_BEGIN(d);
    NtGdiGetTextExtent_pfn gte = UN(NtGdiGetTextExtent); REQUIRE(gte);
    SIZE sz = { 0x7777, 0x7777 };
    WCHAR s[4] = L"abc";
    EXPECT_EQ(gte(d.dc, s, 0x1388001, &sz, 0), 0);          /* no temp buffer is allocated, the string is never read */
    EXPECT_EQ(sz.cx, 0x7777);
    gap_dc_close(&d);
}

TEST(gap1_gettextextent_matches_user_api) {
    ARIAL_BEGIN(d);
    NtGdiGetTextExtent_pfn gte = UN(NtGdiGetTextExtent); REQUIRE(gte);
    static const WCHAR *strs[] = { L"A", L"Hello", L"The quick brown fox", L"iiiiiiiiii", L"WWWWWWWWWW", L"0123456789abcdefg" };
    for (unsigned i = 0; i < sizeof strs / sizeof strs[0]; i++) {
        int n = lstrlenW(strs[i]);
        SIZE a = {0}, b = {0};
        EXPECT_EQ(gte(d.dc, strs[i], n, &a, 0), 1);
        CHECK(U(GetTextExtentPoint32W)(d.dc, strs[i], n, &b));
        CHECK(a.cx > 0 && a.cy > 0);
        EXPECT_EQ(a.cx, b.cx);
        EXPECT_EQ(a.cy, b.cy);
    }
    gap_dc_close(&d);
}

TEST(gap1_gettextextent_height_is_font_height) {
    ARIAL_BEGIN(d);
    NtGdiGetTextExtent_pfn gte = UN(NtGdiGetTextExtent); REQUIRE(gte);
    TEXTMETRICW tm; SIZE a = {0};
    CHECK(U(GetTextMetricsW)(d.dc, &tm));
    EXPECT_EQ(gte(d.dc, L"Hg", 2, &a, 0), 1);
    EXPECT_EQ(a.cy, tm.tmHeight);
    gap_dc_close(&d);
}

TEST(gap1_gettextextent_length_scaling) {
    ARIAL_BEGIN(d);
    NtGdiGetTextExtent_pfn gte = UN(NtGdiGetTextExtent); REQUIRE(gte);
    /* lengths straddling the 16-character on-stack buffer limit (cwc < 0x11 uses the stack copy) */
    int lens[] = { 1, 2, 8, 15, 16, 17, 18, 32, 100, 1000 };
    WCHAR buf[1001]; for (int i = 0; i < 1000; i++) buf[i] = L'M'; buf[1000] = 0;
    SIZE prev = {0};
    for (unsigned i = 0; i < sizeof lens / sizeof lens[0]; i++) {
        SIZE s = {0};
        EXPECT_EQ(gte(d.dc, buf, lens[i], &s, 0), 1);
        CHECK(s.cx > prev.cx);
        if (i) CHECK(s.cx >= prev.cx + (lens[i] - lens[i-1]));   /* every M is at least 1 pixel wide */
        prev = s;
    }
    gap_dc_close(&d);
}

TEST(gap1_gettextextent_same_char_additive) {
    ARIAL_BEGIN(d);
    NtGdiGetTextExtent_pfn gte = UN(NtGdiGetTextExtent); REQUIRE(gte);
    SIZE one = {0}, ten = {0};
    EXPECT_EQ(gte(d.dc, L"I", 1, &one, 0), 1);
    EXPECT_EQ(gte(d.dc, L"IIIIIIIIII", 10, &ten, 0), 1);
    /* integer advance widths under GM_COMPATIBLE accumulate exactly */
    t_note("width(I)=%ld width(10 I)=%ld", one.cx, ten.cx);
    CHECK(ten.cx >= 10 * one.cx - 10 && ten.cx <= 10 * one.cx + 10);
    gap_dc_close(&d);
}

TEST(gap1_gettextextent_invalid_dc) {
    GAP_BEGIN(); NtGdiGetTextExtent_pfn gte = UN(NtGdiGetTextExtent); REQUIRE(gte);
    HDC bad[] = { NULL, (HDC)(ULONG_PTR)1, (HDC)(ULONG_PTR)0xdeadbeef };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SIZE sz = { 0x7777, 0x7777 };
        EXPECT_EQ(gte(bad[i], L"abc", 3, &sz, 0), 0);
    }
}

TEST(gap1_gettextextent_deleted_dc) {
    GAP_BEGIN(); NtGdiGetTextExtent_pfn gte = UN(NtGdiGetTextExtent); REQUIRE(gte);
    HDC dc = U(CreateCompatibleDC)(NULL);
    if (!dc) { t_skip("no DC"); return; }
    SIZE sz = {0};
    EXPECT_EQ(gte(dc, L"abc", 3, &sz, 0), 1);
    U(DeleteDC)(dc);
    sz.cx = 0x7777;
    EXPECT_EQ(gte(dc, L"abc", 3, &sz, 0), 0);
}

/* ---- NtGdiGetTextExtentExW(HDC, WCHAR *, cwc, dxMax, ULONG *pcFit, ULONG *pdx, SIZE *) -------------
 * win32kfull!NtGdiGetTextExtentExW: psize NULL returns 0 at once; cwc == 0 stores SIZE = 0 and *pcFit = 0 and returns 1;
 * a NULL string (cwc > 0) or cwc >= 0x682aab (no temp buffer) fails with 0. Otherwise it fills the optional partial
 * extents (one ULONG per character) and the fit count. */
static BOOL tex(NtGdiGetTextExtentExW_pfn f, HDC dc, const WCHAR *s, int n, ULONG dxmax, ULONG *fit, ULONG *dx, SIZE *sz) {
    return f(dc, s, (ULONG)n, dxmax, fit, dx, sz);
}

TEST(gap1_textextentex_zero_length) {
    ARIAL_BEGIN(d);
    NtGdiGetTextExtentExW_pfn f = UN(NtGdiGetTextExtentExW); REQUIRE(f);
    SIZE sz = { 0x7777, 0x7777 }; ULONG fit = 0x7777;
    EXPECT_EQ(tex(f, d.dc, L"x", 0, 100, &fit, NULL, &sz), 1);
    EXPECT_EQ(sz.cx, 0); EXPECT_EQ(sz.cy, 0); EXPECT_EQ(fit, 0);
    sz.cx = 0x7777;
    EXPECT_EQ(tex(f, d.dc, L"x", 0, 100, NULL, NULL, &sz), 1);   /* pcFit is optional */
    EXPECT_EQ(sz.cx, 0);
    gap_dc_close(&d);
}

TEST(gap1_textextentex_null_size_pointer) {
    ARIAL_BEGIN(d);
    NtGdiGetTextExtentExW_pfn f = UN(NtGdiGetTextExtentExW); REQUIRE(f);
    ULONG fit = 0x7777;
    EXPECT_EQ(tex(f, d.dc, L"abc", 3, 100, &fit, NULL, NULL), 0);
    EXPECT_EQ(tex(f, d.dc, L"abc", 0, 100, &fit, NULL, NULL), 0);
    EXPECT_EQ(fit, 0x7777);
    gap_dc_close(&d);
}

TEST(gap1_textextentex_null_string) {
    ARIAL_BEGIN(d);
    NtGdiGetTextExtentExW_pfn f = UN(NtGdiGetTextExtentExW); REQUIRE(f);
    if (t_is_wine()) { gap_dc_close(&d); t_skip("Wine dereferences the NULL string"); return; }
    SIZE sz = {0}; ULONG fit = 0;
    EXPECT_EQ(tex(f, d.dc, NULL, 3, 100, &fit, NULL, &sz), 0);
    gap_dc_close(&d);
}

TEST(gap1_textextentex_oversize_count) {
    ARIAL_BEGIN(d);
    NtGdiGetTextExtentExW_pfn f = UN(NtGdiGetTextExtentExW); REQUIRE(f);
    if (t_is_wine()) { gap_dc_close(&d); t_skip("Wine would read cwc characters from the 4-character buffer"); return; }
    SIZE sz = {0}; ULONG fit = 0; WCHAR s[4] = L"abc";
    EXPECT_EQ(tex(f, d.dc, s, 0x682aab, 100, &fit, NULL, &sz), 0);
    gap_dc_close(&d);
}

TEST(gap1_textextentex_matches_gettextextent) {
    ARIAL_BEGIN(d);
    NtGdiGetTextExtentExW_pfn f = UN(NtGdiGetTextExtentExW); NtGdiGetTextExtent_pfn g = UN(NtGdiGetTextExtent);
    REQUIRE(f); REQUIRE(g);
    static const WCHAR *strs[] = { L"A", L"Hello", L"The quick brown fox jumps" };
    for (unsigned i = 0; i < 3; i++) {
        int n = lstrlenW(strs[i]);
        SIZE a = {0}, b = {0};
        EXPECT_EQ(tex(f, d.dc, strs[i], n, 100000, NULL, NULL, &a), 1);
        EXPECT_EQ(g(d.dc, strs[i], n, &b, 0), 1);
        EXPECT_EQ(a.cx, b.cx); EXPECT_EQ(a.cy, b.cy);
    }
    gap_dc_close(&d);
}

TEST(gap1_textextentex_partial_extents_monotonic) {
    ARIAL_BEGIN(d);
    NtGdiGetTextExtentExW_pfn f = UN(NtGdiGetTextExtentExW); REQUIRE(f);
    static const int lens[] = { 1, 2, 5, 15, 16, 17, 18, 64, 300 };
    WCHAR buf[301];
    for (int i = 0; i < 300; i++) buf[i] = (i % 3) ? L'i' : L'W';
    buf[300] = 0;
    for (unsigned k = 0; k < sizeof lens / sizeof lens[0]; k++) {
        int n = lens[k];
        ULONG dx[301]; for (int i = 0; i < 301; i++) dx[i] = GAP_SENT;
        ULONG fit = GAP_SENT; SIZE sz = {0};
        EXPECT_EQ(tex(f, d.dc, buf, n, 1000000, &fit, dx, &sz), 1);
        EXPECT_EQ(fit, n);
        int mono = 1;
        for (int i = 1; i < n; i++) if (dx[i] < dx[i-1]) mono = 0;
        CHECK(mono);
        CHECK(dx[0] > 0);
        EXPECT_EQ(dx[n-1], sz.cx);                           /* the last partial extent is the whole string's width */
        EXPECT_EQ(dx[n], GAP_SENT);                          /* nothing written past the count */
    }
    gap_dc_close(&d);
}

TEST(gap1_textextentex_fit_count) {
    ARIAL_BEGIN(d);
    NtGdiGetTextExtentExW_pfn f = UN(NtGdiGetTextExtentExW); REQUIRE(f);
    const WCHAR *s = L"Hello";
    ULONG dx[5]; ULONG fit = 0; SIZE sz = {0};
    EXPECT_EQ(tex(f, d.dc, s, 5, 100000, &fit, dx, &sz), 1);
    EXPECT_EQ(fit, 5);
    ULONG total = sz.cx;
    ULONG lim[] = { total, total - 1, dx[3], dx[3] - 1, dx[0], dx[0] - 1, 0 };
    ULONG want[] = { 5, 4, 4, 3, 1, 0, 0 };
    for (unsigned i = 0; i < sizeof lim / sizeof lim[0]; i++) {
        ULONG fit2 = GAP_SENT, dx2[5] = {0}; SIZE s2 = {0};
        EXPECT_EQ(tex(f, d.dc, s, 5, lim[i], &fit2, dx2, &s2), 1);
        EXPECT_EQ(fit2, want[i]);
    }
    gap_dc_close(&d);
}

TEST(gap1_textextentex_matches_user_api) {
    ARIAL_BEGIN(d);
    NtGdiGetTextExtentExW_pfn f = UN(NtGdiGetTextExtentExW); REQUIRE(f);
    const WCHAR *s = L"Partial extents";
    int n = lstrlenW(s);
    ULONG dx[32]; ULONG fit = 0; SIZE a = {0};
    EXPECT_EQ(tex(f, d.dc, s, n, 100000, &fit, dx, &a), 1);
    INT udx[32]; INT ufit = 0; SIZE b = {0};
    CHECK(U(GetTextExtentExPointW)(d.dc, s, n, 100000, &ufit, udx, &b));
    EXPECT_EQ(a.cx, b.cx); EXPECT_EQ(a.cy, b.cy); EXPECT_EQ(fit, ufit);
    int same = 1; for (int i = 0; i < n; i++) if ((INT)dx[i] != udx[i]) same = 0;
    CHECK(same);
    gap_dc_close(&d);
}

TEST(gap1_textextentex_invalid_dc) {
    GAP_BEGIN(); NtGdiGetTextExtentExW_pfn f = UN(NtGdiGetTextExtentExW); REQUIRE(f);
    HDC bad[] = { NULL, (HDC)(ULONG_PTR)1, (HDC)(ULONG_PTR)0xdeadbeef };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SIZE sz = {0}; ULONG fit = 0;
        EXPECT_EQ(tex(f, bad[i], L"abc", 3, 100, &fit, NULL, &sz), 0);
    }
}

/* ---- NtGdiGetGlyphIndicesW / NtGdiGetGlyphIndicesWInternal ----------------------------------------
 * NtGdiGetGlyphIndicesW is a tail jump to the Internal variant (extra trailing parameter). win32kfull: cwc < 0 returns
 * GDI_ERROR; mode values >= 2 are clamped to 0 (only GGI_MARK_NONEXISTING_GLYPHS == 1 is meaningful); the output is
 * one WORD per input character; cwc above 0x9c4000 never allocates and fails with GDI_ERROR. */
TEST(gap1_glyphindices_negative_count) {
    ARIAL_BEGIN(d);
    NtGdiGetGlyphIndicesW_pfn f = UN(NtGdiGetGlyphIndicesW); REQUIRE(f);
    WORD out[4] = { 0x7777, 0x7777, 0x7777, 0x7777 };
    EXPECT_EQ(f(d.dc, L"ab", -1, out, 0), -1);
    EXPECT_EQ(f(d.dc, L"ab", 0x80000000, out, 0), -1);
    EXPECT_EQ(out[0], 0x7777);
    gap_dc_close(&d);
}

TEST(gap1_glyphindices_oversize_count) {
    ARIAL_BEGIN(d);
    NtGdiGetGlyphIndicesW_pfn f = UN(NtGdiGetGlyphIndicesW); REQUIRE(f);
    WORD out[4] = { 0x7777 };
    EXPECT_EQ(f(d.dc, L"ab", 0x9c4001, out, 0), -1);
    EXPECT_EQ(out[0], 0x7777);
    gap_dc_close(&d);
}

TEST(gap1_glyphindices_basic) {
    ARIAL_BEGIN(d);
    NtGdiGetGlyphIndicesW_pfn f = UN(NtGdiGetGlyphIndicesW); REQUIRE(f);
    WORD out[5] = { GAP_SENT & 0xffff, 0x7777, 0x7777, 0x7777, 0x7777 };
    EXPECT_EQ(f(d.dc, L"AB ", 3, out, 0), 3);
    if (is_arial(&d)) {
        CHECK(out[0] != 0xffff && out[1] != 0xffff && out[2] != 0xffff);
        CHECK(out[0] != out[1]);
    } else t_note("face is %ls, not Arial", d.face);
    EXPECT_EQ(out[3], 0x7777);                               /* only cwc entries are written */
    gap_dc_close(&d);
}

TEST(gap1_glyphindices_mark_nonexisting) {
    ARIAL_BEGIN(d);
    NtGdiGetGlyphIndicesW_pfn f = UN(NtGdiGetGlyphIndicesW); REQUIRE(f);
    WCHAR s[3] = { L'A', 0xFFFF, L'B' };                     /* U+FFFF is a noncharacter: no glyph in any font */
    WORD m[3], z[3];
    EXPECT_EQ(f(d.dc, s, 3, m, 1), 3);
    EXPECT_EQ(m[1], 0xffff);
    EXPECT_NE(m[0], 0xffff); EXPECT_NE(m[2], 0xffff);
    EXPECT_EQ(f(d.dc, s, 3, z, 0), 3);
    EXPECT_EQ(z[0], m[0]); EXPECT_EQ(z[2], m[2]);            /* existing glyphs are the same in both modes */
    t_note("default glyph for U+FFFF without GGI_MARK: 0x%04x", z[1]);
    gap_dc_close(&d);
}

TEST(gap1_glyphindices_mode_clamped) {
    ARIAL_BEGIN(d);
    NtGdiGetGlyphIndicesW_pfn f = UN(NtGdiGetGlyphIndicesW); REQUIRE(f);
    WCHAR s[2] = { 0xFFFF, L'A' };
    WORD z[2], o[2];
    EXPECT_EQ(f(d.dc, s, 2, z, 0), 2);
    DWORD modes[] = { 2, 3, 0x80, 0xffffffff };
    for (unsigned i = 0; i < 4; i++) {
        EXPECT_EQ(f(d.dc, s, 2, o, modes[i]), 2);
        EXPECT_EQ(o[0], z[0]); EXPECT_EQ(o[1], z[1]);        /* mode >= 2 behaves as mode 0 */
    }
    gap_dc_close(&d);
}

TEST(gap1_glyphindices_internal_matches_public) {
    ARIAL_BEGIN(d);
    NtGdiGetGlyphIndicesW_pfn f = UN(NtGdiGetGlyphIndicesW);
    NtGdiGetGlyphIndicesWInternal_pfn g = UN(NtGdiGetGlyphIndicesWInternal);
    REQUIRE(f); REQUIRE(g);
    WCHAR s[] = L"Glyph indices 123";
    int n = lstrlenW(s);
    WORD a[32], b[32], c[32];
    EXPECT_EQ(f(d.dc, s, n, a, 0), n);
    EXPECT_EQ(g(d.dc, s, n, b, 0, 0), n);
    CHECK(!memcmp(a, b, n * 2));
    EXPECT_EQ(U(GetGlyphIndicesW)(d.dc, s, n, c, 0), n);
    CHECK(!memcmp(a, c, n * 2));
    gap_dc_close(&d);
}

TEST(gap1_glyphindices_ansi_wrapper_agrees) {
    ARIAL_BEGIN(d);
    NtGdiGetGlyphIndicesW_pfn f = UN(NtGdiGetGlyphIndicesW); REQUIRE(f);
    WORD a[16], w[16];
    EXPECT_EQ(U(GetGlyphIndicesA)(d.dc, "Hello World", 11, a, 0), 11);
    EXPECT_EQ(f(d.dc, L"Hello World", 11, w, 0), 11);
    CHECK(!memcmp(a, w, 22));
    gap_dc_close(&d);
}

TEST(gap1_glyphindices_same_char_same_glyph) {
    ARIAL_BEGIN(d);
    NtGdiGetGlyphIndicesW_pfn f = UN(NtGdiGetGlyphIndicesW); REQUIRE(f);
    WORD o[20];
    EXPECT_EQ(f(d.dc, L"aaaaaaaaaaaaaaaaaaaa", 20, o, 0), 20);
    for (int i = 1; i < 20; i++) EXPECT_EQ(o[i], o[0]);
    gap_dc_close(&d);
}

TEST(gap1_glyphindices_lengths_around_stack_limit) {
    ARIAL_BEGIN(d);
    NtGdiGetGlyphIndicesW_pfn f = UN(NtGdiGetGlyphIndicesW); REQUIRE(f);
    WCHAR buf[300]; WORD o[300];
    for (int i = 0; i < 300; i++) buf[i] = L'A' + (i % 26);
    int lens[] = { 1, 15, 16, 17, 18, 64, 299 };           /* <= 16 uses the on-stack buffer, larger a tmp allocation */
    for (unsigned k = 0; k < sizeof lens / sizeof lens[0]; k++) {
        memset(o, 0x77, sizeof o);
        EXPECT_EQ(f(d.dc, buf, lens[k], o, 0), lens[k]);
        EXPECT_EQ(o[lens[k]], 0x7777);
        if (lens[k] > 27) EXPECT_EQ(o[0], o[26]);            /* period-26 input -> period-26 glyphs */
    }
    gap_dc_close(&d);
}

TEST(gap1_glyphindices_invalid_dc) {
    GAP_BEGIN(); NtGdiGetGlyphIndicesW_pfn f = UN(NtGdiGetGlyphIndicesW); REQUIRE(f);
    HDC bad[] = { NULL, (HDC)(ULONG_PTR)1, (HDC)(ULONG_PTR)0xdeadbeef };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        WORD o[4] = { 0x7777 };
        EXPECT_EQ(f(bad[i], L"abc", 3, o, 0), -1);           /* GDI_ERROR */
    }
}

/* ---- NtGdiGetRealizationInfo(HDC, DWORD *info) ----------------------------------------------------
 * win32kfull!NtGdiGetRealizationInfo reads info[0] (the cbSize) and only accepts 0x10 or 0x32c; any other size returns 0
 * without writing back. On success the (re)filled structure of that size is copied out. */
TEST(gap1_realizationinfo_bad_sizes) {
    ARIAL_BEGIN(d);
    NtGdiGetRealizationInfo_pfn f = UN(NtGdiGetRealizationInfo); REQUIRE(f);
    DWORD sizes[] = { 0, 1, 4, 0xf, 0x11, 0x20, 0x32b, 0x32d, 0x400, 0xffffffff };
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        DWORD buf[0x32c / 4 + 4]; memset(buf, 0x5a, sizeof buf);
        buf[0] = sizes[i];
        EXPECT_EQ(f(d.dc, buf), 0);
        EXPECT_EQ(buf[0], sizes[i]);
        EXPECT_EQ(buf[1], 0x5a5a5a5au);
    }
    gap_dc_close(&d);
}

TEST(gap1_realizationinfo_small_struct) {
    ARIAL_BEGIN(d);
    NtGdiGetRealizationInfo_pfn f = UN(NtGdiGetRealizationInfo); REQUIRE(f);
    DWORD buf[8]; memset(buf, 0x5a, sizeof buf);
    buf[0] = 0x10;
    int r = f(d.dc, buf);
    t_note("GetRealizationInfo(0x10) -> %d, size field %08lx", r, (unsigned long)buf[0]);
    t_info("GetRealizationInfo words: %08lx %08lx %08lx", (unsigned long)buf[1], (unsigned long)buf[2], (unsigned long)buf[3]);
    CHECK(r != 0);
    EXPECT_EQ(buf[0], 0x10);
    EXPECT_EQ(buf[4], 0x5a5a5a5au);                          /* exactly cbSize bytes are copied back */
    gap_dc_close(&d);
}

TEST(gap1_realizationinfo_large_struct) {
    ARIAL_BEGIN(d);
    NtGdiGetRealizationInfo_pfn f = UN(NtGdiGetRealizationInfo); REQUIRE(f);
    DWORD buf[0x32c / 4 + 8]; memset(buf, 0x5a, sizeof buf);
    buf[0] = 0x32c;
    int r = f(d.dc, buf);
    CHECK(r != 0);
    EXPECT_EQ(buf[0], 0x32c);
    EXPECT_EQ(buf[0x32c / 4], 0x5a5a5a5au);
    gap_dc_close(&d);
}

TEST(gap1_realizationinfo_large_extends_small) {
    ARIAL_BEGIN(d);
    NtGdiGetRealizationInfo_pfn f = UN(NtGdiGetRealizationInfo); REQUIRE(f);
    DWORD s[8] = {0}, l[0x32c / 4 + 4] = {0};
    s[0] = 0x10; l[0] = 0x32c;
    if (!f(d.dc, s) || !f(d.dc, l)) { t_skip("realization info unavailable for this font"); gap_dc_close(&d); return; }
    t_note("head of 0x10 form %s the 0x32c form", !memcmp(&s[1], &l[1], 0xc) ? "equals" : "differs from");
    gap_dc_close(&d);
}

TEST(gap1_realizationinfo_differs_by_font) {
    GAP_BEGIN();
    NtGdiGetRealizationInfo_pfn f = UN(NtGdiGetRealizationInfo); REQUIRE(f);
    struct gap_dc a, b;
    if (!gap_dc_open(&a, L"Arial", -16) || !gap_dc_open(&b, L"Courier New", -16)) { t_skip("setup"); return; }
    DWORD ia[4] = { 0x10 }, ib[4] = { 0x10 };
    int ra = f(a.dc, ia), rb = f(b.dc, ib);
    t_note("Arial: %d / Courier: %d", ra, rb);
    t_info("Arial: %08lx %08lx %08lx / Courier: %08lx %08lx %08lx", (unsigned long)ia[1], (unsigned long)ia[2], (unsigned long)ia[3], (unsigned long)ib[1], (unsigned long)ib[2], (unsigned long)ib[3]);
    CHECK(ra != 0 && rb != 0);
    gap_dc_close(&a); gap_dc_close(&b);
}

TEST(gap1_realizationinfo_user_path) {
    ARIAL_BEGIN(d);
    NtGdiGetRealizationInfo_pfn f = UN(NtGdiGetRealizationInfo);
    GetFontRealizationInfo_pfn u = UN(GetFontRealizationInfo);
    REQUIRE(f); REQUIRE(u);
    DWORD a[4] = { 0x10 }, b[4] = { 0x10 };
    int ra = f(d.dc, a), rb = u(d.dc, b);
    t_note("kernel %d / user %d", ra, rb);
    CHECK(ra != 0);
    CHECK(rb != 0);
    gap_dc_close(&d);
}

TEST(gap1_realizationinfo_invalid_dc) {
    GAP_BEGIN(); NtGdiGetRealizationInfo_pfn f = UN(NtGdiGetRealizationInfo); REQUIRE(f);
    HDC bad[] = { NULL, (HDC)(ULONG_PTR)1, (HDC)(ULONG_PTR)0xdeadbeef };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        DWORD buf[8]; memset(buf, 0x5a, sizeof buf); buf[0] = 0x10;
        EXPECT_EQ(f(bad[i], buf), 0);
        EXPECT_EQ(buf[1], 0x5a5a5a5au);                      /* nothing is copied out on failure */
    }
}

/* ---- NtGdiGetWidthTable(HDC, cSpecial, WCHAR *pwc, cwc, USHORT *widths, WIDTHDATA *, FLONG *) ------
 * win32kfull!NtGdiGetWidthTable: cwc == 0 or an unlockable DC returns -1 untouched; the character list is copied in, a
 * width (28.4 fixed, so pixels << 4 for fixed-pitch faces) is produced per character and the WIDTHDATA (16 bytes)
 * and flags are written. gdi32full!bFillWidthTableForGCW passes cSpecial = 0x100. */
TEST(gap1_widthtable_zero_count) {
    ARIAL_BEGIN(d);
    NtGdiGetWidthTable_pfn f = UN(NtGdiGetWidthTable); REQUIRE(f);
    WCHAR c = L'A'; USHORT w = 0x7777; BYTE wd[16]; memset(wd, 0x5a, 16); ULONG fl = GAP_SENT;
    EXPECT_EQ(f(d.dc, 0x100, &c, 0, &w, wd, &fl), -1);
    EXPECT_EQ(w, 0x7777); EXPECT_EQ(wd[0], 0x5a); EXPECT_EQ(fl, GAP_SENT);
    gap_dc_close(&d);
}

TEST(gap1_widthtable_invalid_dc) {
    GAP_BEGIN(); NtGdiGetWidthTable_pfn f = UN(NtGdiGetWidthTable); REQUIRE(f);
    HDC bad[] = { NULL, (HDC)(ULONG_PTR)1, (HDC)(ULONG_PTR)0xdeadbeef };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        WCHAR c = L'A'; USHORT w = 0x7777; BYTE wd[16] = {0}; ULONG fl = GAP_SENT;
        EXPECT_EQ(f(bad[i], 0x100, &c, 1, &w, wd, &fl), -1);
        EXPECT_EQ(w, 0x7777); EXPECT_EQ(fl, GAP_SENT);
    }
}

TEST(gap1_widthtable_oversize_count) {
    ARIAL_BEGIN(d);
    NtGdiGetWidthTable_pfn f = UN(NtGdiGetWidthTable); REQUIRE(f);
    WCHAR c = L'A'; USHORT w = 0x7777; BYTE wd[16] = {0}; ULONG fl = GAP_SENT;
    EXPECT_EQ(f(d.dc, 0x100, &c, 0x9c4001, &w, wd, &fl), -1);   /* AllocFreeTmpBuffer is skipped, buffer stays NULL */
    EXPECT_EQ(w, 0x7777);
    gap_dc_close(&d);
}

TEST(gap1_widthtable_matches_charwidth) {
    ARIAL_BEGIN(d);
    NtGdiGetWidthTable_pfn f = UN(NtGdiGetWidthTable); REQUIRE(f);
    WCHAR chars[] = { L'A', L'i', L'W', L' ', L'0', L'm', L'.' };
    USHORT w[7]; BYTE wd[16] = {0}; ULONG fl = GAP_SENT;
    int r = f(d.dc, 0x100, chars, 7, w, wd, &fl);
    t_note("GetWidthTable -> %d flags %08lx", r, (unsigned long)fl);
    CHECK(r != -1);
    if (r == -1) { gap_dc_close(&d); return; }
    CHECK(fl != GAP_SENT);                                    /* the flags dword is always written on success */
    for (int i = 0; i < 7; i++) {
        int cw = 0;
        CHECK(U(GetCharWidth32W)(d.dc, chars[i], chars[i], &cw));
        int px = (w[i] + 8) >> 4;
        t_note("U+%04x: table 0x%x (%d px) GetCharWidth32 %d", chars[i], w[i], px, cw);
        CHECK(px >= cw - 1 && px <= cw + 1);
    }
    gap_dc_close(&d);
}

TEST(gap1_widthtable_fixed_pitch_uniform) {
    GAP_BEGIN();
    struct gap_dc d;
    if (!gap_dc_open(&d, L"Courier New", -16) || !d.font) { t_skip("no Courier New"); return; }
    NtGdiGetWidthTable_pfn f = UN(NtGdiGetWidthTable); REQUIRE(f);
    WCHAR chars[8] = { L'i', L'W', L'm', L'.', L'1', L'@', L'l', L'M' };
    USHORT w[8]; BYTE wd[16] = {0}; ULONG fl = 0;
    int r = f(d.dc, 0x100, chars, 8, w, wd, &fl);
    CHECK(r != -1);
    if (r != -1 && !lstrcmpiW(d.face, L"Courier New")) {
        for (int i = 1; i < 8; i++) EXPECT_EQ(w[i], w[0]);    /* monospaced: every width equal */
        CHECK(w[0] != 0);
    }
    gap_dc_close(&d);
}

/* ---- NtGdiGetUFI / NtGdiGetEmbUFI / NtGdiGetLinkedUFIs -------------------------------------------
 * win32kfull!NtGdiGetUFI fills (ufi, DESIGNVECTOR, cjDV, baseChecksum, flags) from the kernel UFI query; the output pointers are
 * written only when the kernel UFI query succeeds, and the DESIGNVECTOR/cjDV/baseChecksum triple only when (flags & 2). NtGdiGetEmbUFI
 * calls the same the kernel UFI query and additionally returns a file identifier qword. */
struct ufi_out { gap_ufi ufi; BYTE dv[0x48]; ULONG cjdv, base, flags; ULONGLONG fid; };
static void ufi_init(struct ufi_out *o) {
    memset(o, 0x5a, sizeof *o);
}

TEST(gap1_getufi_basic) {
    ARIAL_BEGIN(d);
    NtGdiGetUFI_pfn f = UN(NtGdiGetUFI); REQUIRE(f);
    struct ufi_out o; ufi_init(&o);
    int r = f(d.dc, &o.ufi, o.dv, &o.cjdv, &o.base, &o.flags);
    t_note("GetUFI -> %d checksum %08lx index %lu flags %lx", r, (unsigned long)o.ufi.CheckSum, (unsigned long)o.ufi.Index, (unsigned long)o.flags);
    CHECK(r != 0);
    if (r && is_arial(&d)) CHECK(o.ufi.CheckSum != 0x5a5a5a5au);
    if (r && !(o.flags & 2)) {
        EXPECT_EQ(o.cjdv, 0x5a5a5a5au);                       /* the DV triple is untouched without the design-vector flag */
        EXPECT_EQ(o.base, 0x5a5a5a5au);
    }
    gap_dc_close(&d);
}

TEST(gap1_getufi_deterministic) {
    ARIAL_BEGIN(d);
    NtGdiGetUFI_pfn f = UN(NtGdiGetUFI); REQUIRE(f);
    struct ufi_out a, b; ufi_init(&a); ufi_init(&b);
    int ra = f(d.dc, &a.ufi, a.dv, &a.cjdv, &a.base, &a.flags);
    int rb = f(d.dc, &b.ufi, b.dv, &b.cjdv, &b.base, &b.flags);
    EXPECT_EQ(ra, rb);
    if (ra) { EXPECT_EQ(a.ufi.CheckSum, b.ufi.CheckSum); EXPECT_EQ(a.ufi.Index, b.ufi.Index); EXPECT_EQ(a.flags, b.flags); }
    gap_dc_close(&d);
}

TEST(gap1_getufi_same_font_other_dc) {
    GAP_BEGIN();
    NtGdiGetUFI_pfn f = UN(NtGdiGetUFI); REQUIRE(f);
    struct gap_dc d1, d2;
    if (!gap_dc_open(&d1, L"Arial", -16) || !gap_dc_open(&d2, L"Arial", -24)) { t_skip("setup"); return; }
    struct ufi_out a, b; ufi_init(&a); ufi_init(&b);
    int ra = f(d1.dc, &a.ufi, a.dv, &a.cjdv, &a.base, &a.flags);
    int rb = f(d2.dc, &b.ufi, b.dv, &b.cjdv, &b.base, &b.flags);
    CHECK(ra && rb);
    if (ra && rb) { EXPECT_EQ(a.ufi.CheckSum, b.ufi.CheckSum); EXPECT_EQ(a.ufi.Index, b.ufi.Index); }   /* identifies the font file, not the size */
    gap_dc_close(&d1); gap_dc_close(&d2);
}

TEST(gap1_getufi_different_fonts) {
    GAP_BEGIN();
    NtGdiGetUFI_pfn f = UN(NtGdiGetUFI); REQUIRE(f);
    struct gap_dc d1, d2;
    if (!gap_dc_open(&d1, L"Arial", -16) || !gap_dc_open(&d2, L"Courier New", -16)) { t_skip("setup"); return; }
    if (lstrcmpiW(d1.face, L"Arial") || lstrcmpiW(d2.face, L"Courier New")) { t_note("faces %ls / %ls", d1.face, d2.face); t_skip("faces not installed"); gap_dc_close(&d1); gap_dc_close(&d2); return; }
    struct ufi_out a, b; ufi_init(&a); ufi_init(&b);
    int ra = f(d1.dc, &a.ufi, a.dv, &a.cjdv, &a.base, &a.flags);
    int rb = f(d2.dc, &b.ufi, b.dv, &b.cjdv, &b.base, &b.flags);
    CHECK(ra && rb);
    if (ra && rb) CHECK(a.ufi.CheckSum != b.ufi.CheckSum || a.ufi.Index != b.ufi.Index);
    gap_dc_close(&d1); gap_dc_close(&d2);
}

TEST(gap1_getufi_invalid_dc_leaves_outputs) {
    GAP_BEGIN();
    NtGdiGetUFI_pfn f = UN(NtGdiGetUFI); REQUIRE(f);
    HDC bad[] = { NULL, (HDC)(ULONG_PTR)1, (HDC)(ULONG_PTR)0xdeadbeef };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        struct ufi_out o; ufi_init(&o);
        EXPECT_EQ(f(bad[i], &o.ufi, o.dv, &o.cjdv, &o.base, &o.flags), 0);
        EXPECT_EQ(o.ufi.CheckSum, 0x5a5a5a5au); EXPECT_EQ(o.flags, 0x5a5a5a5au); EXPECT_EQ(o.dv[0], 0x5a);
    }
}

TEST(gap1_getufi_stock_fonts_stable) {
    ARIAL_BEGIN(d);
    NtGdiGetUFI_pfn f = UN(NtGdiGetUFI); REQUIRE(f);
    int ids[] = { SYSTEM_FONT, ANSI_VAR_FONT, ANSI_FIXED_FONT, DEVICE_DEFAULT_FONT, OEM_FIXED_FONT, SYSTEM_FIXED_FONT, DEFAULT_GUI_FONT };
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        HGDIOBJ sf = U(GetStockObject)(ids[i]);
        HGDIOBJ old = U(SelectObject)(d.dc, sf);
        struct ufi_out a, b; ufi_init(&a); ufi_init(&b);
        int ra = f(d.dc, &a.ufi, a.dv, &a.cjdv, &a.base, &a.flags);
        int rb = f(d.dc, &b.ufi, b.dv, &b.cjdv, &b.base, &b.flags);
        t_note("stock font %d: ret %d checksum %08lx flags %lx", ids[i], ra, (unsigned long)a.ufi.CheckSum, (unsigned long)a.flags);
        EXPECT_EQ(ra, rb);
        if (ra && rb) EXPECT_EQ(a.ufi.CheckSum, b.ufi.CheckSum);
        U(SelectObject)(d.dc, old);
    }
    gap_dc_close(&d);
}

TEST(gap1_getembufi_matches_getufi) {
    ARIAL_BEGIN(d);
    NtGdiGetUFI_pfn f = UN(NtGdiGetUFI); NtGdiGetEmbUFI_pfn g = UN(NtGdiGetEmbUFI);
    REQUIRE(f); REQUIRE(g);
    struct ufi_out a, b; ufi_init(&a); ufi_init(&b);
    int ra = f(d.dc, &a.ufi, a.dv, &a.cjdv, &a.base, &a.flags);
    int rb = g(d.dc, &b.ufi, b.dv, &b.cjdv, &b.base, &b.flags, &b.fid);
    EXPECT_EQ(ra, rb);
    if (ra && rb) {
        EXPECT_EQ(a.ufi.CheckSum, b.ufi.CheckSum); EXPECT_EQ(a.ufi.Index, b.ufi.Index); EXPECT_EQ(a.flags, b.flags);
        t_note("EmbUFI file id %016llx", (unsigned long long)b.fid);
        CHECK(b.fid != 0x5a5a5a5a5a5a5a5aULL);              /* the file-id qword is always written on success */
    }
    gap_dc_close(&d);
}

TEST(gap1_getembufi_invalid_dc) {
    GAP_BEGIN(); NtGdiGetEmbUFI_pfn g = UN(NtGdiGetEmbUFI); REQUIRE(g);
    struct ufi_out o; ufi_init(&o);
    EXPECT_EQ(g(NULL, &o.ufi, o.dv, &o.cjdv, &o.base, &o.flags, &o.fid), 0);
    EXPECT_EQ(o.ufi.CheckSum, 0x5a5a5a5au);
    EXPECT_EQ(o.fid, 0x5a5a5a5a5a5a5a5aULL);
}

TEST(gap1_linkedufis_count_query) {
    ARIAL_BEGIN(d);
    NtGdiGetLinkedUFIs_pfn f = UN(NtGdiGetLinkedUFIs); REQUIRE(f);
    int n = f(d.dc, NULL, 0);                                 /* cj == 0 and no buffer: a pure count query */
    t_note("linked fonts for %ls: %d", d.face, n);
    CHECK(n >= 0);
    if (n > 0 && n < 1000) {
        gap_ufi *u = (gap_ufi *)calloc(n + 2, sizeof *u);
        if (u) {
            u[n].CheckSum = 0x1234; u[n].Index = 0x5678;
            EXPECT_EQ(f(d.dc, u, n), n);
            EXPECT_EQ(u[n].CheckSum, 0x1234);                 /* only n entries are written */
            free(u);
        }
    }
    gap_dc_close(&d);
}

TEST(gap1_linkedufis_buffer_without_count) {
    ARIAL_BEGIN(d);
    NtGdiGetLinkedUFIs_pfn f = UN(NtGdiGetLinkedUFIs); REQUIRE(f);
    gap_ufi u[2] = {{0}};
    GAP_POISON();
    EXPECT_EQ(f(d.dc, NULL, 3), -1);                          /* count but no buffer */
    EXPECT_ERR(8);
    GAP_POISON();
    EXPECT_EQ(f(d.dc, u, -1), -1);                            /* negative count with a buffer */
    EXPECT_ERR(8);
    gap_dc_close(&d);
}

TEST(gap1_linkedufis_invalid_dc) {
    GAP_BEGIN(); NtGdiGetLinkedUFIs_pfn f = UN(NtGdiGetLinkedUFIs); REQUIRE(f);
    HDC bad[] = { NULL, (HDC)(ULONG_PTR)1, (HDC)(ULONG_PTR)0xdeadbeef };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        gap_ufi u[2];
        GAP_POISON();
        EXPECT_EQ(f(bad[i], u, 2), -1);
        EXPECT_ERR(8);                                        /* EngSetLastError(ERROR_NOT_ENOUGH_MEMORY) on every failure */
        GAP_POISON();
        EXPECT_EQ(f(bad[i], NULL, 0), -1);
        EXPECT_ERR(8);
    }
}

TEST(gap1_linkedufis_oversize_count) {
    ARIAL_BEGIN(d);
    NtGdiGetLinkedUFIs_pfn f = UN(NtGdiGetLinkedUFIs); REQUIRE(f);
    gap_ufi u[2];
    GAP_POISON();
    EXPECT_EQ(f(d.dc, u, 0x4e2001), -1);                      /* above 0x4e2000 entries no allocation is attempted */
    EXPECT_ERR(8);
    gap_dc_close(&d);
}
