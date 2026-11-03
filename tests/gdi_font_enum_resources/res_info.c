/* GetFontResourceInfoW / NtGdiGetFontResourceInfoInternalW, CreateScalableFontResource, GetFontUnicodeRanges, misc font-table queries.
 *
 * win32kfull!GetFontResourceInfoInternalW looks the file up in the private table first, then the public one (pPFFGet by NT path).
 * With the file loaded, iType selects: 0 -> face count (4 bytes), 1 -> description string (QueryFontFile), 2 -> count * 0x5c bytes of
 * LOGFONTW, 3 -> 4-byte "is TrueType" flag, 5 -> 4 bytes of 0, anything else -> ERROR_INVALID_PARAMETER (0x57) and 0.
 * With the file NOT loaded only iType 5 succeeds (4 bytes = 1); every other type returns 0. cjIn == 0 only reports the size through
 * *pcjOut. The syscall wrapper (NtGdiGetFontResourceInfoInternalW) bounces the result through a zeroed kernel buffer, then copies cjIn bytes. */
#include "fe.h"
#include <stdlib.h>

#define SENT 0xA5A5A5A5u
static int needk(void) { return UN(NtGdiGetFontResourceInfoInternalW) && U(GetDC) && !t_is_wine(); /* wine aborts on the unimplemented syscall */ }

static int arial(char *path, WCHAR *nt, ULONG *cwc, WCHAR *dos) {
    fe_windir_font("arial.ttf", path, MAX_PATH);
    if (!fe_file_exists(path)) return 0;
    fe_nt_path(path, nt, MAX_PATH + 8, cwc);
    fe_wide(path, dos, MAX_PATH);
    return 1;
}
#define ARIAL() char ap[MAX_PATH]; WCHAR ant[MAX_PATH + 8], adr[MAX_PATH]; ULONG acwc; \
    if (!needk()) { t_skip("missing exports"); return; } if (!arial(ap, ant, &acwc, adr)) { t_skip("arial.ttf absent"); return; }

TEST(gdi_font_enum_resources_resinfo_stub_prologue) {
    REQUIRE(UN(NtGdiGetFontResourceInfoInternalW));
    FARPROC p = t_proc_in("win32u.dll", "NtGdiGetFontResourceInfoInternalW");
    CHECK(k_stub_valid(p));
    if (!t_is_wine()) EXPECT_EQ(k_stub_number(p), 4799);
}

TEST(gdi_font_enum_resources_resinfo_count_size_query) {
    ARIAL();
    ULONG out = SENT;
    int r = UN(NtGdiGetFontResourceInfoInternalW)(ant, acwc, 1, 0, &out, NULL, 0);
    CHECK(r != 0);
    EXPECT_EQ(out, 4);
}

TEST(gdi_font_enum_resources_resinfo_count_value) {
    ARIAL();
    ULONG out = SENT; ULONG cnt = SENT;
    int r = UN(NtGdiGetFontResourceInfoInternalW)(ant, acwc, 1, 4, &out, &cnt, 0);
    CHECK(r != 0);
    EXPECT_EQ(out, 4);
    EXPECT_EQ(cnt, 1); /* arial.ttf holds one face */
}

TEST(gdi_font_enum_resources_resinfo_small_buffer_zero_filled_tail) {
    ARIAL();
    /* The kernel copies cjIn bytes back from a zeroed 0x5c byte buffer, so bytes after the 4-byte result are cleared. */
    BYTE b[16]; memset(b, 0xCC, sizeof b);
    ULONG out = 0;
    int r = UN(NtGdiGetFontResourceInfoInternalW)(ant, acwc, 1, 16, &out, b, 0);
    CHECK(r != 0);
    ULONG cnt; memcpy(&cnt, b, 4);
    EXPECT_EQ(cnt, 1);
    for (int i = 4; i < 16; i++) if (b[i] != 0) { CHECKF(0, "byte %d is 0x%02x, expected zero", i, b[i]); break; }
}

TEST(gdi_font_enum_resources_resinfo_buffer_too_small_for_count) {
    ARIAL();
    BYTE b[8]; memset(b, 0xCC, sizeof b);
    ULONG out = SENT;
    for (ULONG cj = 1; cj < 4; cj++) {
        out = SENT;
        EXPECT_EQ(UN(NtGdiGetFontResourceInfoInternalW)(ant, acwc, 1, cj, &out, b, 0), 0); /* cjIn < 4 -> goto failure */
        EXPECT_EQ(out, SENT);
    }
}

TEST(gdi_font_enum_resources_resinfo_logfont_size_and_content) {
    ARIAL();
    ULONG out = SENT;
    int r = UN(NtGdiGetFontResourceInfoInternalW)(ant, acwc, 1, 0, &out, NULL, 2);
    CHECK(r != 0);
    EXPECT_EQ(out, 0x5c);
    LOGFONTW lf[2]; memset(lf, 0, sizeof lf);
    out = SENT;
    r = UN(NtGdiGetFontResourceInfoInternalW)(ant, acwc, 1, 0x5c, &out, lf, 2);
    CHECK(r != 0);
    EXPECT_EQ(out, 0x5c);
    EXPECT_WSTR(lf[0].lfFaceName, L"Arial");
    EXPECT_EQ(lf[0].lfCharSet == 0 || lf[0].lfCharSet == 1, 1);
    EXPECT_EQ(lf[0].lfWeight, 400);
    EXPECT_EQ(lf[0].lfItalic, 0);
}

TEST(gdi_font_enum_resources_resinfo_logfont_buffer_too_small) {
    ARIAL();
    BYTE b[0x5c]; memset(b, 0xCC, sizeof b);
    ULONG out = SENT;
    EXPECT_EQ(UN(NtGdiGetFontResourceInfoInternalW)(ant, acwc, 1, 0x5b, &out, b, 2), 0); /* cjIn < count*0x5c */
    EXPECT_EQ(UN(NtGdiGetFontResourceInfoInternalW)(ant, acwc, 1, 4, &out, b, 2), 0);
    EXPECT_EQ(out, SENT);
}

TEST(gdi_font_enum_resources_resinfo_description_string) {
    ARIAL();
    ULONG need_ = SENT;
    int r = UN(NtGdiGetFontResourceInfoInternalW)(ant, acwc, 1, 0, &need_, NULL, 1);
    CHECK(r != 0);
    CHECK(need_ >= 2 && need_ != SENT);
    if (need_ >= 2 && need_ < 1024) {
        WCHAR d[512]; memset(d, 0, sizeof d);
        ULONG out = SENT;
        r = UN(NtGdiGetFontResourceInfoInternalW)(ant, acwc, 1, need_, &out, d, 1);
        CHECK(r != 0);
        CHECK(wcsstr(d, L"Arial") != NULL);
        t_note("description (%lu bytes): %ls", need_, d);
    }
}

TEST(gdi_font_enum_resources_resinfo_description_buffer_too_small) {
    ARIAL();
    ULONG need_ = 0;
    UN(NtGdiGetFontResourceInfoInternalW)(ant, acwc, 1, 0, &need_, NULL, 1);
    if (need_ > 2) {
        WCHAR d[8]; ULONG out = SENT;
        EXPECT_EQ(UN(NtGdiGetFontResourceInfoInternalW)(ant, acwc, 1, need_ - 1 > 8 * 2 ? 8 * 2 : need_ - 1, &out, d, 1), 0);
    }
}

TEST(gdi_font_enum_resources_resinfo_truetype_flag) {
    ARIAL();
    ULONG out = SENT, v = SENT;
    int r = UN(NtGdiGetFontResourceInfoInternalW)(ant, acwc, 1, 4, &out, &v, 3);
    CHECK(r != 0);
    EXPECT_EQ(out, 4);
    CHECK(v == 0 || v == 1);
    t_note("iType 3 value for arial.ttf: %lu", v);
}

TEST(gdi_font_enum_resources_resinfo_type5_loaded_file_value_zero) {
    ARIAL();
    ULONG out = SENT, v = SENT;
    int r = UN(NtGdiGetFontResourceInfoInternalW)(ant, acwc, 1, 4, &out, &v, 5);
    CHECK(r != 0);
    EXPECT_EQ(out, 4);
    EXPECT_EQ(v, 0); /* loaded font: the 'not installed' flag is 0 */
}

TEST(gdi_font_enum_resources_resinfo_unknown_types_invalid_parameter) {
    ARIAL();
    ULONG types[] = { 4, 6, 7, 8, 0x10, 0xFF, 0x100, 0xFFFFFFFFu };
    for (unsigned i = 0; i < sizeof types / sizeof types[0]; i++) {
        ULONG out = SENT, v = SENT;
        SetLastError(0);
        int r = UN(NtGdiGetFontResourceInfoInternalW)(ant, acwc, 1, 4, &out, &v, types[i]);
        CHECKF(r == 0, "iType %lu accepted", types[i]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER); /* EngSetLastError(0x57) */
        EXPECT_EQ(out, SENT);
        EXPECT_EQ(v, SENT);
    }
}

TEST(gdi_font_enum_resources_resinfo_syscall_rejects_bad_path_args) {
    ARIAL();
    ULONG out = SENT, v = SENT;
    EXPECT_EQ(UN(NtGdiGetFontResourceInfoInternalW)(ant, 0, 1, 4, &out, &v, 0), 0);
    EXPECT_EQ(UN(NtGdiGetFontResourceInfoInternalW)(ant, 1, 1, 4, &out, &v, 0), 0);
    EXPECT_EQ(UN(NtGdiGetFontResourceInfoInternalW)(ant, acwc - 1, 1, 4, &out, &v, 0), 0); /* not NUL terminated */
    EXPECT_EQ(UN(NtGdiGetFontResourceInfoInternalW)(ant, acwc, 2, 4, &out, &v, 0), 0);     /* file count mismatch */
    EXPECT_EQ(out, SENT);
    EXPECT_EQ(v, SENT);
}

TEST(gdi_font_enum_resources_resinfo_syscall_huge_output_size_rejected) {
    ARIAL();
    /* cjIn above 0x2710000 is refused before any allocation. */
    ULONG out = SENT; ULONG v = 0;
    EXPECT_EQ(UN(NtGdiGetFontResourceInfoInternalW)(ant, acwc, 1, 0x2710001, &out, &v, 0), 0);
    EXPECT_EQ(UN(NtGdiGetFontResourceInfoInternalW)(ant, acwc, 1, 0xFFFFFFFFu, &out, &v, 0), 0);
    EXPECT_EQ(out, SENT);
}

TEST(gdi_font_enum_resources_resinfo_not_loaded_file_fails_except_type5) {
    if (!needk()) { t_skip("missing exports"); return; }
    char a[MAX_PATH]; WCHAR p[MAX_PATH + 8]; ULONG cwc;
    t_tmpfile(a, sizeof a, "not_loaded_font.ttf"); fe_nt_path(a, p, MAX_PATH + 8, &cwc);
    for (ULONG t = 0; t <= 4; t++) {
        ULONG out = SENT, v = SENT; BYTE b[0x5c];
        int r = UN(NtGdiGetFontResourceInfoInternalW)(p, cwc, 1, 0x5c, &out, b, t);
        CHECKF(r == 0, "iType %lu succeeded for a font that is not loaded", t);
        (void)v;
    }
    ULONG out = SENT, v = SENT;
    int r = UN(NtGdiGetFontResourceInfoInternalW)(p, cwc, 1, 4, &out, &v, 5);
    CHECK(r != 0);
    EXPECT_EQ(out, 4);
    EXPECT_EQ(v, 1); /* "not installed" */
}

TEST(gdi_font_enum_resources_resinfo_not_loaded_type5_size_query) {
    if (!needk()) { t_skip("missing exports"); return; }
    char a[MAX_PATH]; WCHAR p[MAX_PATH + 8]; ULONG cwc;
    t_tmpfile(a, sizeof a, "not_loaded_font2.ttf"); fe_nt_path(a, p, MAX_PATH + 8, &cwc);
    ULONG out = SENT;
    EXPECT_EQ(UN(NtGdiGetFontResourceInfoInternalW)(p, cwc, 1, 0, &out, NULL, 5), 1);
    EXPECT_EQ(out, 4);
    out = SENT; ULONG v = SENT;
    EXPECT_EQ(UN(NtGdiGetFontResourceInfoInternalW)(p, cwc, 1, 3, &out, &v, 5), 0); /* cjIn < 4 */
}

/* ---- user-mode GetFontResourceInfoW ----------------------------------------------------------------------------- */
TEST(gdi_font_enum_resources_resinfo_user_null_name_zero) {
    if (!UN(GetFontResourceInfoW) || t_is_wine()) { t_skip("missing GetFontResourceInfoW"); return; }
    DWORD cb = 4; DWORD v;
    EXPECT_EQ(UN(GetFontResourceInfoW)(NULL, &cb, &v, 0), 0);
}

TEST(gdi_font_enum_resources_resinfo_user_null_buffer_with_size_zero) {
    if (!UN(GetFontResourceInfoW) || t_is_wine()) { t_skip("missing GetFontResourceInfoW"); return; }
    WCHAR w[MAX_PATH]; char p[MAX_PATH]; fe_windir_font("arial.ttf", p, sizeof p); fe_wide(p, w, MAX_PATH);
    DWORD cb = 4;
    EXPECT_EQ(UN(GetFontResourceInfoW)(w, &cb, NULL, 0), 0); /* size != 0 with no buffer */
}

TEST(gdi_font_enum_resources_resinfo_user_count_for_arial) {
    if (!UN(GetFontResourceInfoW) || t_is_wine()) { t_skip("missing GetFontResourceInfoW"); return; }
    char p[MAX_PATH]; WCHAR w[MAX_PATH]; fe_windir_font("arial.ttf", p, sizeof p);
    if (!fe_file_exists(p)) { t_skip("arial.ttf absent"); return; }
    fe_wide(p, w, MAX_PATH);
    DWORD cb = 4, v = SENT;
    DWORD r = UN(GetFontResourceInfoW)(w, &cb, &v, 0);
    CHECK(r != 0);
    EXPECT_EQ(v, 1);
    DWORD cb0 = 0;
    r = UN(GetFontResourceInfoW)(w, &cb0, NULL, 0);
    CHECK(r != 0);
    EXPECT_EQ(cb0, 4); /* size query */
}

TEST(gdi_font_enum_resources_resinfo_user_logfont_for_arial) {
    if (!UN(GetFontResourceInfoW) || t_is_wine()) { t_skip("missing GetFontResourceInfoW"); return; }
    char p[MAX_PATH]; WCHAR w[MAX_PATH]; fe_windir_font("arial.ttf", p, sizeof p);
    if (!fe_file_exists(p)) { t_skip("arial.ttf absent"); return; }
    fe_wide(p, w, MAX_PATH);
    LOGFONTW lf; memset(&lf, 0, sizeof lf);
    DWORD cb = sizeof lf;
    DWORD r = UN(GetFontResourceInfoW)(w, &cb, &lf, 2);
    CHECK(r != 0);
    EXPECT_EQ(cb, sizeof(LOGFONTW));
    EXPECT_WSTR(lf.lfFaceName, L"Arial");
}

TEST(gdi_font_enum_resources_resinfo_user_added_font_logfont) {
    if (!UN(GetFontResourceInfoW) || t_is_wine() || !U(AddFontResourceExW) || !U(RemoveFontResourceExW)) { t_skip("missing exports"); return; }
    char fam[64], path[MAX_PATH]; WCHAR wf[64], wp[MAX_PATH];
    fe_family(fam, sizeof fam, 700); fe_wide(fam, wf, 64);
    if (!fe_write_ttf(fam, path, sizeof path)) { t_skip("cannot write font"); return; }
    fe_wide(path, wp, MAX_PATH);
    if (!U(AddFontResourceExW)(wp, FR_PRIVATE, NULL)) { t_skip("generated font not accepted"); return; }
    DWORD cb = 4, cnt = SENT;
    EXPECT_NE(UN(GetFontResourceInfoW)(wp, &cb, &cnt, 0), 0);
    EXPECT_EQ(cnt, 1);
    LOGFONTW lf; memset(&lf, 0, sizeof lf); cb = sizeof lf;
    EXPECT_NE(UN(GetFontResourceInfoW)(wp, &cb, &lf, 2), 0);
    EXPECT_WSTR(lf.lfFaceName, wf);
    WCHAR desc[256] = { 0 }; cb = sizeof desc;
    if (UN(GetFontResourceInfoW)(wp, &cb, desc, 1)) { CHECK(wcsstr(desc, wf) != NULL); t_info("description: %ls", desc); }
    /* after removal the file is no longer loaded: counts fail */
    U(RemoveFontResourceExW)(wp, FR_PRIVATE, NULL);
    cb = 4; cnt = SENT;
    EXPECT_EQ(UN(GetFontResourceInfoW)(wp, &cb, &cnt, 0), 0);
}

TEST(gdi_font_enum_resources_resinfo_user_unloaded_file_fails) {
    if (!UN(GetFontResourceInfoW) || t_is_wine()) { t_skip("missing GetFontResourceInfoW"); return; }
    char fam[64], path[MAX_PATH]; WCHAR wp[MAX_PATH];
    fe_family(fam, sizeof fam, 701);
    if (!fe_write_ttf(fam, path, sizeof path)) { t_skip("cannot write font"); return; }
    fe_wide(path, wp, MAX_PATH);
    DWORD cb = 4, cnt = SENT;
    EXPECT_EQ(UN(GetFontResourceInfoW)(wp, &cb, &cnt, 0), 0); /* GetFontResourceInfoInternalW only knows loaded files */
}

/* ---- CreateScalableFontResource ---------------------------------------------------------------------------------- */
TEST(gdi_font_enum_resources_scalable_null_arguments_rejected) {
    if (!U(CreateScalableFontResourceW)) { t_skip("missing export"); return; }
    WCHAR f[] = L"x.fot", t[] = L"arial.ttf";
    SetLastError(0);
    EXPECT_EQ(U(CreateScalableFontResourceW)(0, NULL, t, NULL), 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SetLastError(0);
    EXPECT_EQ(U(CreateScalableFontResourceW)(0, f, NULL, NULL), 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SetLastError(0);
    EXPECT_EQ(U(CreateScalableFontResourceW)(0, NULL, NULL, NULL), 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}

TEST(gdi_font_enum_resources_scalable_missing_font_file_fails) {
    if (!U(CreateScalableFontResourceW)) { t_skip("missing export"); return; }
    char fa[MAX_PATH], ta[MAX_PATH]; WCHAR fot[MAX_PATH], ttf[MAX_PATH];
    t_tmpfile(fa, sizeof fa, "missing.fot"); t_tmpfile(ta, sizeof ta, "missing_font.ttf");
    fe_wide(fa, fot, MAX_PATH); fe_wide(ta, ttf, MAX_PATH);
    EXPECT_EQ(U(CreateScalableFontResourceW)(0, fot, ttf, NULL), 0);
    CHECK(!fe_file_exists(fa));
}

TEST(gdi_font_enum_resources_scalable_existing_resource_file_fails) {
    if (!U(CreateScalableFontResourceW)) { t_skip("missing export"); return; }
    /* Documented: the function fails if the .fot already exists. */
    char fam[64], path[MAX_PATH], fa[MAX_PATH]; WCHAR fot[MAX_PATH], ttf[MAX_PATH];
    fe_family(fam, sizeof fam, 702);
    if (!fe_write_ttf(fam, path, sizeof path)) { t_skip("cannot write font"); return; }
    t_tmpfile(fa, sizeof fa, "exists.fot");
    HANDLE h = CreateFileA(fa, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL); CloseHandle(h);
    fe_wide(fa, fot, MAX_PATH); fe_wide(path, ttf, MAX_PATH);
    EXPECT_EQ(U(CreateScalableFontResourceW)(0, fot, ttf, NULL), 0);
}

TEST(gdi_font_enum_resources_scalable_creates_resource_for_ttf) {
    if (!U(CreateScalableFontResourceW)) { t_skip("missing export"); return; }
    char fam[64], path[MAX_PATH], fa[MAX_PATH]; WCHAR fot[MAX_PATH], ttf[MAX_PATH];
    fe_family(fam, sizeof fam, 703);
    if (!fe_write_ttf(fam, path, sizeof path)) { t_skip("cannot write font"); return; }
    t_tmpfile(fa, sizeof fa, "made.fot");
    fe_wide(fa, fot, MAX_PATH); fe_wide(path, ttf, MAX_PATH);
    BOOL r = U(CreateScalableFontResourceW)(0, fot, ttf, NULL);
    t_note("CreateScalableFontResourceW on a TrueType file: %d (error %lu), .fot exists: %d", r, GetLastError(), fe_file_exists(fa));
    if (r) CHECK(fe_file_exists(fa));
}

/* ---- GetFontUnicodeRanges (NtGdiGetFontUnicodeRanges) -------------------------------------------------------------- */
TEST(gdi_font_enum_resources_ranges_stub_prologue) {
    REQUIRE(UN(NtGdiGetFontUnicodeRanges));
    FARPROC p = t_proc_in("win32u.dll", "NtGdiGetFontUnicodeRanges");
    CHECK(k_stub_valid(p));
    if (!t_is_wine()) EXPECT_EQ(k_stub_number(p), 4800);
}

static HFONT arial_font(HDC dc, HGDIOBJ *old) {
    HFONT f = U(CreateFontW)(-16, 0, 0, 0, 400, 0, 0, 0, ANSI_CHARSET, 0, 0, 0, 0, L"Arial");
    *old = U(SelectObject)(dc, f);
    return f;
}

TEST(gdi_font_enum_resources_ranges_size_query_and_layout) {
    if (!U(GetFontUnicodeRanges) || !U(CreateFontW) || !U(CreateCompatibleDC) || !U(SelectObject)) { t_skip("missing exports"); return; }
    HDC dc = U(CreateCompatibleDC)(NULL); HGDIOBJ old; HFONT f = arial_font(dc, &old);
    DWORD sz = U(GetFontUnicodeRanges)(dc, NULL);
    CHECK(sz >= 16);
    GLYPHSET *gs = (GLYPHSET *)calloc(1, sz ? sz : 16);
    DWORD got = U(GetFontUnicodeRanges)(dc, gs);
    EXPECT_EQ(got, sz);
    if (got) {
        EXPECT_EQ(gs->cbThis, sz);
        EXPECT_EQ(sz, 16 + 4 * gs->cRanges);
        DWORD sum = 0; for (DWORD i = 0; i < gs->cRanges; i++) sum += gs->ranges[i].cGlyphs;
        EXPECT_EQ(sum, gs->cGlyphsSupported);
        CHECK(gs->cGlyphsSupported > 100);
        int latin = 0; for (DWORD i = 0; i < gs->cRanges; i++) if (gs->ranges[i].wcLow <= 0x41 && 0x41 < gs->ranges[i].wcLow + gs->ranges[i].cGlyphs) latin = 1;
        CHECK(latin);
        for (DWORD i = 1; i < gs->cRanges; i++) if (gs->ranges[i].wcLow <= gs->ranges[i - 1].wcLow) { CHECKF(0, "ranges not ascending at %lu", i); break; }
    }
    free(gs);
    U(SelectObject)(dc, old); U(DeleteObject)(f); U(DeleteDC)(dc);
}

TEST(gdi_font_enum_resources_ranges_small_buffer_semantics) {
    if (!UN(NtGdiGetFontUnicodeRanges) || !U(CreateFontW) || !U(CreateCompatibleDC) || !U(SelectObject)) { t_skip("missing exports"); return; }
    HDC dc = U(CreateCompatibleDC)(NULL); HGDIOBJ old; HFONT f = arial_font(dc, &old);
    ULONG sz = UN(NtGdiGetFontUnicodeRanges)(dc, NULL);
    CHECK(sz >= 16);
    /* the kernel sizes its own copy from the first call, so a user buffer of that size is all it ever writes */
    BYTE *b = (BYTE *)calloc(1, sz + 16);
    memset(b + sz, 0x77, 16);
    ULONG got = UN(NtGdiGetFontUnicodeRanges)(dc, b);
    EXPECT_EQ(got, sz);
    for (int i = 0; i < 16; i++) if (b[sz + i] != 0x77) { CHECKF(0, "wrote past the buffer"); break; }
    free(b);
    U(SelectObject)(dc, old); U(DeleteObject)(f); U(DeleteDC)(dc);
}

TEST(gdi_font_enum_resources_ranges_invalid_dc_zero) {
    if (!UN(NtGdiGetFontUnicodeRanges)) { t_skip("missing export"); return; }
    EXPECT_EQ(UN(NtGdiGetFontUnicodeRanges)(NULL, NULL), 0);
    EXPECT_EQ(UN(NtGdiGetFontUnicodeRanges)((HDC)(ULONG_PTR)0x1234, NULL), 0);
}

TEST(gdi_font_enum_resources_ranges_symbol_font_nonempty) {
    if (!U(GetFontUnicodeRanges) || !U(CreateFontW) || !U(CreateCompatibleDC) || !U(SelectObject)) { t_skip("missing exports"); return; }
    HDC dc = U(CreateCompatibleDC)(NULL);
    HFONT f = U(CreateFontW)(-16, 0, 0, 0, 400, 0, 0, 0, SYMBOL_CHARSET, 0, 0, 0, 0, L"Wingdings");
    HGDIOBJ old = U(SelectObject)(dc, f);
    WCHAR face[64] = { 0 }; U(GetTextFaceW)(dc, 64, face);
    if (!lstrcmpiW(face, L"Wingdings")) {
        DWORD sz = U(GetFontUnicodeRanges)(dc, NULL);
        CHECK(sz >= 16);
        t_note("Wingdings glyph-set size %lu", sz);
    } else t_skip("Wingdings not selected");
    U(SelectObject)(dc, old); U(DeleteObject)(f); U(DeleteDC)(dc);
}

/* ---- misc font-table syscalls -------------------------------------------------------------------------------- */
TEST(gdi_font_enum_resources_misc_embed_fonts_zero_without_embedded) {
    if (t_is_wine()) { t_skip("wine aborts on unimplemented NtGdiGetEmbedFonts"); return; }
    typedef ULONG (WINAPI *pfn)(void);
    pfn p = (pfn)t_proc("NtGdiGetEmbedFonts");
    if (!p) { t_skip("missing NtGdiGetEmbedFonts"); return; }
    ULONG r = p();
    t_note("NtGdiGetEmbedFonts = %lu", r);
}
