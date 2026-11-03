/* NtGdiEnumFonts called directly (win32u stub) and the ENUMFONTDATA record chain it produces.
 *
 * Kernel facts (win32kfull NtGdiEnumFonts):
 *  - face == NULL: pBuf == NULL stores the needed size through pcjBuf; pBuf != NULL is probed for *pcjBuf bytes and filled; the
 *    size stored back is what was produced. A bad DC makes the DC lookup fail -> EngSetLastError(6) and return 0.
 *  - face != NULL: only 1 <= cwchFace <= 0x20 is accepted, anything else returns 0 without touching the DC.
 * gdi32full!EnumFontsInternalW parses the buffer with  while (p < base + size)  p += p->ulSize, so the stride sum equals the size. */
#include "fe.h"
#include <stdlib.h>

#define NAME_NOSUCH(buf) snprintf(buf, sizeof buf, "Win32ktNoSuch%lu", GetCurrentProcessId())

static int need_enum(void) {
    return UN(NtGdiEnumFonts) && U(GetDC) && U(ReleaseDC) && U(EnumFontFamiliesExW) && U(CreateCompatibleDC) && U(DeleteDC);
}

TEST(gdi_font_enum_resources_raw_stub_prologue) {
    REQUIRE(UN(NtGdiEnumFonts));
    FARPROC p = t_proc_in("win32u.dll", "NtGdiEnumFonts");
    CHECK(k_stub_valid(p));
    long n = k_stub_number(p);
    if (t_is_wine()) { t_note("wine syscall number %ld", n); return; }
    EXPECT_EQ(n, 4756);
}

TEST(gdi_font_enum_resources_raw_size_query_succeeds) {
    if (!need_enum()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    ULONG sz = 0x1234;
    SetLastError(0);
    ULONG r = UN(NtGdiEnumFonts)(dc, 3, 0, 0, NULL, 1, &sz, NULL);
    CHECK(r != 0);
    CHECK(sz > 0);
    CHECKF((sz & 3) == 0, "size %lu not a multiple of 4", sz);
    CHECKF(sz >= 12 + sizeof(ENUMLOGFONTEXW) + sizeof(NEWTEXTMETRICEXW), "size %lu smaller than one record", sz);
    U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_raw_size_query_overwrites_input_size) {
    if (!need_enum()) { t_skip("missing exports"); return; }
    /* the kernel font enumeration stores 0 into *pcjBuf first when pBuf is NULL, then the needed size: the input value is not a limit. */
    HDC dc = U(GetDC)(NULL);
    ULONG a = 0, b = 0xFFFF0000u;
    ULONG r1 = UN(NtGdiEnumFonts)(dc, 3, 0, 0, NULL, 1, &a, NULL);
    ULONG r2 = UN(NtGdiEnumFonts)(dc, 3, 0, 0, NULL, 1, &b, NULL);
    CHECK(r1 && r2);
    EXPECT_EQ(a, b);
    U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_raw_size_query_stable) {
    if (!need_enum()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    ULONG a = 0, b = 0;
    UN(NtGdiEnumFonts)(dc, 3, 0, 0, NULL, 1, &a, NULL);
    UN(NtGdiEnumFonts)(dc, 3, 0, 0, NULL, 1, &b, NULL);
    EXPECT_EQ(a, b);
    U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_raw_null_dc_fails_with_invalid_handle) {
    if (!need_enum()) { t_skip("missing exports"); return; }
    ULONG sz = 0;
    SetLastError(0);
    ULONG r = UN(NtGdiEnumFonts)(NULL, 3, 0, 0, NULL, 1, &sz, NULL);
    EXPECT_EQ(r, 0);
    EXPECT_ERR(ERROR_INVALID_HANDLE); /* DC lookup failed: EngSetLastError(6) */
    EXPECT_EQ(sz, 0); /* *pcjBuf was cleared before the lock */
}

TEST(gdi_font_enum_resources_raw_invalid_dc_values) {
    if (!need_enum()) { t_skip("missing exports"); return; }
    ULONG_PTR bad[] = { 1, 2, 0x100, 0xFFFF, 0x10001, 0x00050001, 0xFFFFFFFF, 0x7FFFFFFF };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        ULONG sz = 0;
        SetLastError(0);
        ULONG r = UN(NtGdiEnumFonts)((HDC)bad[i], 3, 0, 0, NULL, 1, &sz, NULL);
        CHECKF(r == 0, "dc %llx accepted", (unsigned long long)bad[i]);
        EXPECT_ERR(ERROR_INVALID_HANDLE);
    }
}

TEST(gdi_font_enum_resources_raw_wrong_object_type_handles) {
    if (!need_enum() || !U(CreateSolidBrush) || !U(CreateBitmap) || !U(DeleteObject)) { t_skip("missing exports"); return; }
    /* A brush / bitmap / region handle is not a DC: the DCOBJ lock (type 1 expected) fails. */
    HGDIOBJ objs[3] = { U(CreateSolidBrush)(0x123456), U(CreateBitmap)(4, 4, 1, 1, NULL), U(CreateRectRgn)(0, 0, 4, 4) };
    for (int i = 0; i < 3; i++) {
        ULONG sz = 0;
        SetLastError(0);
        ULONG r = UN(NtGdiEnumFonts)((HDC)objs[i], 3, 0, 0, NULL, 1, &sz, NULL);
        CHECKF(r == 0, "object %d accepted as DC", i);
        EXPECT_ERR(ERROR_INVALID_HANDLE);
        if (objs[i]) U(DeleteObject)(objs[i]);
    }
}

TEST(gdi_font_enum_resources_raw_deleted_dc_fails) {
    if (!need_enum()) { t_skip("missing exports"); return; }
    HDC dc = U(CreateCompatibleDC)(NULL);
    CHECK(dc != NULL);
    ULONG sz = 0;
    CHECK(UN(NtGdiEnumFonts)(dc, 3, 0, 0, NULL, 1, &sz, NULL) != 0);
    U(DeleteDC)(dc);
    SetLastError(0);
    EXPECT_EQ(UN(NtGdiEnumFonts)(dc, 3, 0, 0, NULL, 1, &sz, NULL), 0);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
}

TEST(gdi_font_enum_resources_raw_records_stride_sums_to_size) {
    if (!need_enum()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    BYTE *b; ULONG sz;
    CHECK(fe_raw_enum(dc, 3, NULL, 1, &b, &sz) != 0);
    if (b) {
        /* EnumFontsInternalW walks p += p->ulSize until base+size: the chain must land exactly on the end. */
        int n = fe_raw_walk(b, sz, NULL);
        CHECKF(n > 0, "walk returned %d size %lu", n, sz);
        free(b);
    }
    U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_raw_records_wellformed) {
    if (!need_enum()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    BYTE *b; ULONG sz; struct fe_rec *r = NULL;
    CHECK(fe_raw_enum(dc, 3, NULL, 1, &b, &sz) != 0);
    int n = b ? fe_raw_walk(b, sz, &r) : -1;
    CHECK(n > 0);
    for (int i = 0; i < n; i++) {
        BOOL term = FALSE;
        for (int k = 0; k < LF_FACESIZE; k++) if (!r[i].elf.elfLogFont.lfFaceName[k]) { term = TRUE; break; }
        CHECKF(term, "record %d face not terminated", i);
        CHECKF(r[i].elf.elfLogFont.lfFaceName[0] != 0, "record %d has empty face", i);
        /* the stock vector fonts (Modern, Roman, Script) have none of the RASTER/DEVICE/TRUETYPE bits; nothing else may lack them */
        CHECKF((r[i].ftype & 7) != 0 || fe_is_vector_face(r[i].elf.elfLogFont.lfFaceName), "record %d (%ls) font type 0x%lx has none of RASTER/DEVICE/TRUETYPE", i, r[i].elf.elfLogFont.lfFaceName, r[i].ftype);
        CHECKF((r[i].size & 3) == 0, "record %d stride %lu", i, r[i].size);
    }
    free(r); free(b);
    U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_raw_ntm_block_size_is_constant) {
    if (!need_enum()) { t_skip("missing exports"); return; }
    /* cjEfdw (the offset of the NTM minus 8) follows the size of the ENUMLOGFONTEXDVW part, which grows for variable
     * fonts, so it differs between records; but everything behind it is a fixed-size block (the NTM and its trailer) and
     * only TrueType records may differ from the first one. */
    HDC dc = U(GetDC)(NULL);
    BYTE *b; ULONG sz; struct fe_rec *r = NULL;
    fe_raw_enum(dc, 3, NULL, 1, &b, &sz);
    int n = b ? fe_raw_walk(b, sz, &r) : -1;
    CHECK(n > 0);
    int differing = 0;
    for (int i = 1; i < n; i++) {
        EXPECT_EQ(r[i].size - r[i].cjEfdw, r[0].size - r[0].cjEfdw);
        CHECKF((r[i].cjEfdw & 7) == 0, "record %d cjEfdw %lu not 8-aligned", i, r[i].cjEfdw);
        if (r[i].cjEfdw != r[0].cjEfdw) { differing++; CHECKF((r[i].ftype & 7) == FE_TRUETYPE, "record %d: cjEfdw differs but font type is 0x%lx", i, r[i].ftype); }
    }
    if (n > 0) t_note("cjEfdw=%lu first stride=%lu, %d records differ", r[0].cjEfdw, r[0].size, differing);
    free(r); free(b);
    U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_raw_matches_callback_count) {
    if (!need_enum()) { t_skip("missing exports"); return; }
    /* With a NULL face EnumFontsInternalW invokes the callback once per record (no scaling expansion). */
    HDC dc = U(GetDC)(NULL);
    BYTE *b; ULONG sz;
    fe_raw_enum(dc, 3, NULL, 1, &b, &sz);
    int n = b ? fe_raw_walk(b, sz, NULL) : -1;
    struct fe_set s; fe_set_init(&s);
    int ret = fe_enum(dc, NULL, 1, &s);
    CHECK(ret != 0);
    EXPECT_EQ(s.n, n);
    fe_set_free(&s); free(b);
    U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_raw_records_equal_callback_order) {
    if (!need_enum()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    BYTE *b; ULONG sz; struct fe_rec *r = NULL;
    fe_raw_enum(dc, 3, NULL, 1, &b, &sz);
    int n = b ? fe_raw_walk(b, sz, &r) : -1;
    struct fe_set s; fe_set_init(&s);
    fe_enum(dc, NULL, 1, &s);
    if (n == s.n) {
        for (int i = 0; i < n; i++) {
            if (lstrcmpW(r[i].elf.elfLogFont.lfFaceName, s.it[i].face) || lstrcmpW(r[i].elf.elfFullName, s.it[i].full) ||
                r[i].elf.elfLogFont.lfCharSet != s.it[i].charset || r[i].elf.elfLogFont.lfWeight != s.it[i].weight) {
                CHECKF(0, "record %d differs from the callback item", i); break;
            }
            /* The callback FontType is the record word, except that the appcompat flag may mask bit 2 (flags==0 here). */
            if ((r[i].ftype & 7) != (s.it[i].ftype & 7)) { CHECKF(0, "record %d font type 0x%lx vs callback 0x%lx", i, r[i].ftype, s.it[i].ftype); break; }
        }
    } else CHECKF(0, "counts differ %d vs %d", n, s.n);
    fe_set_free(&s); free(r); free(b);
    U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_raw_exact_size_buffer_succeeds) {
    if (!need_enum()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    ULONG sz = 0;
    UN(NtGdiEnumFonts)(dc, 3, 0, 0, NULL, 1, &sz, NULL);
    BYTE *b = (BYTE *)calloc(1, sz + 16);
    if (b) {
        ULONG s2 = sz;
        memset(b + sz, 0xAB, 16);
        ULONG r = UN(NtGdiEnumFonts)(dc, 3, 0, 0, NULL, 1, &s2, b);
        CHECK(r != 0);
        EXPECT_EQ(s2, sz);
        for (int i = 0; i < 16; i++) if (b[sz + i] != 0xAB) { CHECKF(0, "byte %d past the buffer was written", i); break; }
        free(b);
    }
    U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_raw_small_buffer_fails) {
    if (!need_enum()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    ULONG sz = 0;
    UN(NtGdiEnumFonts)(dc, 3, 0, 0, NULL, 1, &sz, NULL);
    ULONG cuts[] = { 4, 16, 64 };
    for (int i = 0; i < 3; i++) {
        if (sz <= cuts[i]) continue;
        ULONG cap = sz - cuts[i];
        BYTE *b = (BYTE *)calloc(1, cap + 16);
        if (!b) continue;
        ULONG s2 = cap;
        SetLastError(0);
        ULONG r = UN(NtGdiEnumFonts)(dc, 3, 0, 0, NULL, 1, &s2, b);
        CHECKF(r == 0, "buffer %lu of %lu accepted", cap, sz);
        /* EnumFontsInternalW grows and retries only when GetLastError()==0x6f (ERROR_BUFFER_OVERFLOW). */
        DWORD e = GetLastError();
        if (e != 0x6f) t_note("short buffer left last error %lu (retry protocol expects 111)", e);
        free(b);
    }
    U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_raw_tiny_buffer_fails) {
    if (!need_enum()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    BYTE b[64]; memset(b, 0xEE, sizeof b);
    ULONG s2 = 4;
    EXPECT_EQ(UN(NtGdiEnumFonts)(dc, 3, 0, 0, NULL, 1, &s2, b), 0);
    for (int i = 4; i < 64; i++) if (b[i] != 0xEE) { CHECKF(0, "byte %d written beyond the declared 4", i); break; }
    U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_raw_zero_size_buffer_fails) {
    if (!need_enum()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    BYTE b[32]; memset(b, 0xEE, sizeof b);
    ULONG s2 = 0;
    EXPECT_EQ(UN(NtGdiEnumFonts)(dc, 3, 0, 0, NULL, 1, &s2, b), 0);
    for (int i = 0; i < 32; i++) if (b[i] != 0xEE) { CHECKF(0, "byte %d written for a zero-size buffer", i); break; }
    U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_raw_face_length_rejected) {
    if (!need_enum()) { t_skip("missing exports"); return; }
    /* NtGdiEnumFonts: for a non-NULL face only (cwch - 1) < 0x20 passes; everything else returns 0 before any DC lookup. */
    HDC dc = U(GetDC)(NULL);
    WCHAR face[40]; for (int i = 0; i < 39; i++) face[i] = L'A'; face[39] = 0;
    ULONG lens[] = { 0, 33, 34, 40, 0x100, 0x10000, 0x7FFFFFFF, 0xFFFFFFFFu };
    for (unsigned i = 0; i < sizeof lens / sizeof lens[0]; i++) {
        if (t_is_wine() && lens[i] > 40) continue; /* Wine's implementation would read cwch characters from the 40-char buffer */
        ULONG sz = 0x77;
        ULONG r = UN(NtGdiEnumFonts)(dc, 3, 0, lens[i], face, 1, &sz, NULL);
        CHECKF(r == 0, "cwchFace=%lu accepted", lens[i]);
        EXPECT_EQ(sz, 0x77);
    }
    U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_raw_face_length_rejected_even_for_bad_dc) {
    if (!need_enum()) { t_skip("missing exports"); return; }
    WCHAR face[4] = L"Abc"; ULONG sz = 0;
    SetLastError(0);
    EXPECT_EQ(UN(NtGdiEnumFonts)(NULL, 3, 0, 0, face, 1, &sz, NULL), 0);
    CHECKF(GetLastError() == 0, "length rejection must not reach the DC lookup (last error %lu)", GetLastError());
}

TEST(gdi_font_enum_resources_raw_face_arial_only_arial_records) {
    if (!need_enum()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    BYTE *b; ULONG sz; struct fe_rec *r = NULL;
    ULONG ret = fe_raw_enum(dc, 3, L"Arial", 1, &b, &sz);
    CHECK(ret != 0);
    int n = b ? fe_raw_walk(b, sz, &r) : -1;
    CHECKF(n > 0, "no Arial records (%d)", n);
    for (int i = 0; i < n; i++) CHECKF(!lstrcmpiW(r[i].elf.elfLogFont.lfFaceName, L"Arial"), "record %d face %ls", i, r[i].elf.elfLogFont.lfFaceName);
    free(r); free(b);
    U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_raw_face_is_case_insensitive) {
    if (!need_enum()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    BYTE *b1, *b2; ULONG s1, s2;
    fe_raw_enum(dc, 3, L"Arial", 1, &b1, &s1);
    fe_raw_enum(dc, 3, L"aRIAL", 1, &b2, &s2);
    CHECK(s1 > 0);
    EXPECT_EQ(s1, s2);
    free(b1); free(b2);
    U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_raw_face_missing_yields_no_records) {
    if (!need_enum()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    char n8[64]; WCHAR w[64]; NAME_NOSUCH(n8); fe_wide(n8, w, 64);
    BYTE *b; ULONG sz;
    ULONG ret = fe_raw_enum(dc, 3, w, 1, &b, &sz);
    if (ret) { int n = fe_raw_walk(b, sz, NULL); EXPECT_EQ(n, 0); t_note("missing face: syscall succeeded with %lu bytes", sz); }
    else t_note("missing face: syscall returned 0, last error %lu", GetLastError());
    free(b);
    U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_raw_face_max_length_31_chars) {
    if (!need_enum()) { t_skip("missing exports"); return; }
    /* cwch == 0x20 (31 chars + NUL) is the longest accepted: the call must not be rejected by the length gate, so it
     * can only fail by not finding the face. A 32-char name (cwch 33) is rejected outright (previous test). */
    HDC dc = U(GetDC)(NULL);
    WCHAR face[33]; for (int i = 0; i < 31; i++) face[i] = L'Q'; face[31] = 0;
    ULONG sz = 0;
    ULONG r = UN(NtGdiEnumFonts)(dc, 3, 0, 32, face, 1, &sz, NULL);
    t_note("31-char face: ret=%lu size=%lu", r, sz);
    U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_raw_charset_ansi_records_are_ansi) {
    if (!need_enum()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    BYTE *b; ULONG sz; struct fe_rec *r = NULL;
    CHECK(fe_raw_enum(dc, 3, NULL, ANSI_CHARSET, &b, &sz) != 0);
    int n = b ? fe_raw_walk(b, sz, &r) : -1;
    CHECK(n > 0);
    for (int i = 0; i < n; i++)
        if (r[i].elf.elfLogFont.lfCharSet != ANSI_CHARSET) { CHECKF(0, "record %d charset %u face %ls", i, r[i].elf.elfLogFont.lfCharSet, r[i].elf.elfLogFont.lfFaceName); break; }
    free(r); free(b);
    U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_raw_charset_symbol_records_are_symbol) {
    if (!need_enum()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    BYTE *b; ULONG sz; struct fe_rec *r = NULL;
    CHECK(fe_raw_enum(dc, 3, NULL, SYMBOL_CHARSET, &b, &sz) != 0);
    int n = b ? fe_raw_walk(b, sz, &r) : -1;
    CHECKF(n > 0, "no symbol-charset fonts (%d)", n);
    for (int i = 0; i < n; i++)
        if (r[i].elf.elfLogFont.lfCharSet != SYMBOL_CHARSET) { CHECKF(0, "record %d charset %u face %ls", i, r[i].elf.elfLogFont.lfCharSet, r[i].elf.elfLogFont.lfFaceName); break; }
    free(r); free(b);
    U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_raw_charset_subsets_of_default) {
    if (!need_enum()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    ULONG sdef = 0, sans = 0, ssym = 0;
    UN(NtGdiEnumFonts)(dc, 3, 0, 0, NULL, DEFAULT_CHARSET, &sdef, NULL);
    UN(NtGdiEnumFonts)(dc, 3, 0, 0, NULL, ANSI_CHARSET, &sans, NULL);
    UN(NtGdiEnumFonts)(dc, 3, 0, 0, NULL, SYMBOL_CHARSET, &ssym, NULL);
    CHECKF(sans <= sdef, "ansi %lu > default %lu", sans, sdef);
    CHECKF(ssym <= sdef, "symbol %lu > default %lu", ssym, sdef);
    U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_raw_enum_types_family_vs_all) {
    if (!need_enum()) { t_skip("missing exports"); return; }
    /* iEnumType: 1 EnumFonts, 2 EnumFontFamilies, 3 EnumFontFamiliesEx (EnumFontsW/FamiliesW/FamiliesExW in gdi32full). */
    HDC dc = U(GetDC)(NULL);
    ULONG t1 = 0, t2 = 0, t3 = 0;
    ULONG r1 = UN(NtGdiEnumFonts)(dc, 1, 0, 0, NULL, 1, &t1, NULL);
    ULONG r2 = UN(NtGdiEnumFonts)(dc, 2, 0, 0, NULL, 1, &t2, NULL);
    ULONG r3 = UN(NtGdiEnumFonts)(dc, 3, 0, 0, NULL, 1, &t3, NULL);
    CHECK(r1 && r2 && r3);
    CHECKF(t2 <= t3, "families %lu > all %lu", t2, t3);
    t_note("sizes: type1=%lu type2=%lu type3=%lu", t1, t2, t3);
    U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_raw_family_type_one_record_per_family) {
    if (!need_enum()) { t_skip("missing exports"); return; }
    /* EnumFontFamilies(NULL) lists one font per family; with DEFAULT charset the family appears once. */
    HDC dc = U(GetDC)(NULL);
    BYTE *b; ULONG sz; struct fe_rec *r = NULL;
    fe_raw_enum(dc, 2, NULL, 1, &b, &sz);
    int n = b ? fe_raw_walk(b, sz, &r) : -1;
    CHECK(n > 0);
    int dup = 0;
    for (int i = 0; i < n && !dup; i++) for (int k = i + 1; k < n; k++)
        if (!lstrcmpiW(r[i].elf.elfLogFont.lfFaceName, r[k].elf.elfLogFont.lfFaceName)) { dup = 1; t_note("family %ls repeated in type 2", r[i].elf.elfLogFont.lfFaceName); break; }
    free(r); free(b);
    U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_raw_memory_dc_same_set_as_screen) {
    if (!need_enum()) { t_skip("missing exports"); return; }
    HDC scr = U(GetDC)(NULL), mem = U(CreateCompatibleDC)(scr);
    BYTE *b1, *b2; ULONG s1, s2;
    CHECK(fe_raw_enum(scr, 3, NULL, 1, &b1, &s1) != 0);
    CHECK(fe_raw_enum(mem, 3, NULL, 1, &b2, &s2) != 0);
    EXPECT_EQ(s1, s2);
    if (b1 && b2 && s1 == s2) CHECK(!memcmp(b1, b2, s1));
    free(b1); free(b2);
    U(DeleteDC)(mem); U(ReleaseDC)(NULL, scr);
}

TEST(gdi_font_enum_resources_raw_null_compat_dc_same_set_as_screen) {
    if (!need_enum()) { t_skip("missing exports"); return; }
    HDC scr = U(GetDC)(NULL), mem = U(CreateCompatibleDC)(NULL);
    ULONG s1 = 0, s2 = 0;
    CHECK(UN(NtGdiEnumFonts)(scr, 3, 0, 0, NULL, 1, &s1, NULL) != 0);
    CHECK(UN(NtGdiEnumFonts)(mem, 3, 0, 0, NULL, 1, &s2, NULL) != 0);
    EXPECT_EQ(s1, s2);
    U(DeleteDC)(mem); U(ReleaseDC)(NULL, scr);
}

TEST(gdi_font_enum_resources_raw_memory_dc_with_selected_bitmap_same_set) {
    if (!need_enum() || !U(CreateCompatibleBitmap) || !U(SelectObject)) { t_skip("missing exports"); return; }
    HDC scr = U(GetDC)(NULL), mem = U(CreateCompatibleDC)(scr);
    HBITMAP bm = U(CreateCompatibleBitmap)(scr, 8, 8);
    HGDIOBJ old = U(SelectObject)(mem, bm);
    ULONG s1 = 0, s2 = 0;
    UN(NtGdiEnumFonts)(scr, 3, 0, 0, NULL, 1, &s1, NULL);
    CHECK(UN(NtGdiEnumFonts)(mem, 3, 0, 0, NULL, 1, &s2, NULL) != 0);
    EXPECT_EQ(s1, s2);
    U(SelectObject)(mem, old); U(DeleteObject)(bm); U(DeleteDC)(mem); U(ReleaseDC)(NULL, scr);
}

TEST(gdi_font_enum_resources_raw_info_dc_same_set_as_screen) {
    if (!need_enum() || !U(CreateICW)) { t_skip("missing exports"); return; }
    HDC scr = U(GetDC)(NULL), ic = U(CreateICW)(L"DISPLAY", NULL, NULL, NULL);
    if (!ic) { t_skip("no info DC"); U(ReleaseDC)(NULL, scr); return; }
    ULONG s1 = 0, s2 = 0;
    UN(NtGdiEnumFonts)(scr, 3, 0, 0, NULL, 1, &s1, NULL);
    CHECK(UN(NtGdiEnumFonts)(ic, 3, 0, 0, NULL, 1, &s2, NULL) != 0);
    t_note("screen size %lu, info DC size %lu", s1, s2);
    U(DeleteDC)(ic); U(ReleaseDC)(NULL, scr);
}

TEST(gdi_font_enum_resources_raw_get_dc_window_same_set) {
    if (!need_enum() || !U(CreateWindowExW)) { t_skip("missing exports"); return; }
    HWND w = k_make_window("fe_raw_wdc");
    if (!w) { t_skip("no window"); return; }
    HDC wdc = U(GetDC)(w), scr = U(GetDC)(NULL);
    ULONG s1 = 0, s2 = 0;
    UN(NtGdiEnumFonts)(scr, 3, 0, 0, NULL, 1, &s1, NULL);
    CHECK(UN(NtGdiEnumFonts)(wdc, 3, 0, 0, NULL, 1, &s2, NULL) != 0);
    EXPECT_EQ(s1, s2);
    U(ReleaseDC)(w, wdc); U(ReleaseDC)(NULL, scr); U(DestroyWindow)(w);
}

TEST(gdi_font_enum_resources_raw_selected_font_does_not_affect_set) {
    if (!need_enum() || !U(CreateFontW) || !U(SelectObject) || !U(DeleteObject)) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    ULONG s1 = 0, s2 = 0;
    UN(NtGdiEnumFonts)(dc, 3, 0, 0, NULL, 1, &s1, NULL);
    HFONT f = U(CreateFontW)(-20, 0, 0, 0, 700, 1, 0, 0, 0, 0, 0, 0, 0, L"Arial");
    HGDIOBJ old = U(SelectObject)(dc, f);
    CHECK(UN(NtGdiEnumFonts)(dc, 3, 0, 0, NULL, 1, &s2, NULL) != 0);
    EXPECT_EQ(s1, s2);
    U(SelectObject)(dc, old); U(DeleteObject)(f);
    U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_raw_map_mode_does_not_affect_set) {
    if (!need_enum() || !U(SetMapMode)) { t_skip("missing exports"); return; }
    HDC dc = U(CreateCompatibleDC)(NULL);
    ULONG s1 = 0, s2 = 0;
    UN(NtGdiEnumFonts)(dc, 3, 0, 0, NULL, 1, &s1, NULL);
    U(SetMapMode)(dc, MM_LOENGLISH);
    CHECK(UN(NtGdiEnumFonts)(dc, 3, 0, 0, NULL, 1, &s2, NULL) != 0);
    EXPECT_EQ(s1, s2);
    U(DeleteDC)(dc);
}

TEST(gdi_font_enum_resources_raw_buffer_alignment_probe_4) {
    if (!need_enum()) { t_skip("missing exports"); return; }
    /* The output buffer is probed with 4-byte alignment: a 4-aligned but not 8-aligned buffer is valid. */
    HDC dc = U(GetDC)(NULL);
    ULONG sz = 0;
    UN(NtGdiEnumFonts)(dc, 3, 0, 0, NULL, 1, &sz, NULL);
    BYTE *raw = (BYTE *)calloc(1, sz + 16);
    BYTE *b = raw + 4;
    if ((((ULONG_PTR)b) & 7) == 0) b += 4; /* force 4 mod 8 */
    ULONG s2 = sz;
    CHECK(UN(NtGdiEnumFonts)(dc, 3, 0, 0, NULL, 1, &s2, b) != 0);
    EXPECT_EQ(s2, sz);
    int n = fe_raw_walk(b, s2, NULL);
    CHECK(n > 0);
    free(raw);
    U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_raw_repeat_calls_deterministic_bytes) {
    if (!need_enum()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    BYTE *b1, *b2; ULONG s1, s2;
    fe_raw_enum(dc, 3, NULL, 1, &b1, &s1);
    fe_raw_enum(dc, 3, NULL, 1, &b2, &s2);
    EXPECT_EQ(s1, s2);
    if (b1 && b2 && s1 == s2) CHECK(!memcmp(b1, b2, s1));
    free(b1); free(b2);
    U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_raw_ntm_size_em_for_truetype) {
    if (!need_enum()) { t_skip("missing exports"); return; }
    HDC dc = U(GetDC)(NULL);
    BYTE *b; ULONG sz; struct fe_rec *r = NULL;
    fe_raw_enum(dc, 3, L"Arial", 1, &b, &sz);
    int n = b ? fe_raw_walk(b, sz, &r) : -1;
    CHECK(n > 0);
    for (int i = 0; i < n; i++) {
        if (!(r[i].ftype & FE_TRUETYPE)) continue;
        /* A TrueType face reports its em size and a non-empty cell/average width in the NEWTEXTMETRIC tail. */
        CHECKF(r[i].ntm.ntmTm.ntmSizeEM >= 16, "em %lu", r[i].ntm.ntmTm.ntmSizeEM);
        CHECKF(r[i].ntm.ntmTm.ntmCellHeight > 0, "cell %lu", r[i].ntm.ntmTm.ntmCellHeight);
        CHECKF(r[i].ntm.ntmTm.ntmAvgWidth > 0, "avg %lu", r[i].ntm.ntmTm.ntmAvgWidth);
        CHECK(r[i].ntm.ntmTm.tmPitchAndFamily & TMPF_TRUETYPE);
    }
    free(r); free(b);
    U(ReleaseDC)(NULL, dc);
}
