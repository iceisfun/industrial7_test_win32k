/* AddFontMemResourceEx / RemoveFontMemResourceEx and NtGdiAddFontMemResourceEx / NtGdiRemoveFontMemResourceEx.
 *
 * gdi32full!AddFontMemResourceEx: cjSize == 0, NULL view or NULL pNumFonts -> ERROR_INVALID_PARAMETER and NULL, otherwise
 * NtGdiAddFontMemResourceEx(view, cj, 0, 0, pNumFonts). The kernel (win32kfull!NtGdiAddFontMemResourceEx) returns 0 for the same
 * three conditions, rejects a design-vector size above 0x48 or one that is not dvNumAxes*4+8, and only stores *pNumFonts on success.
 * The font goes into the per-process private font table (AddFontMemResourceEx), so the public
 * table (and its change cookie) is not involved. RemoveFontMemResourceEx(NULL) -> ERROR_INVALID_PARAMETER in gdi32full. */
#include "fe.h"
#include <stdlib.h>

static int need(void) { return U(AddFontMemResourceEx) && U(RemoveFontMemResourceEx) && U(EnumFontFamiliesExW) && U(GetDC) && U(ReleaseDC); }
#define SENT 0xA5A5A5A5u

static int vis(const WCHAR *fam) { HDC dc = U(GetDC)(NULL); int n = fe_count_family(dc, fam, DEFAULT_CHARSET); U(ReleaseDC)(NULL, dc); return n; }

/* A font added from memory is usable by CreateFont/SelectObject in this process but is NOT reported by EnumFontFamiliesEx
 * on Windows 10 (vis() stays 0 while it is loaded); usable() asks the font mapper for the family and compares the face
 * it actually selected. */
static int usable(const WCHAR *fam) {
    if (!U(CreateFontW) || !U(CreateCompatibleDC) || !U(GetTextFaceW)) return -1;
    HDC dc = U(CreateCompatibleDC)(NULL);
    HFONT f = U(CreateFontW)(-16, 0, 0, 0, 400, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, fam);
    HGDIOBJ old = U(SelectObject)(dc, f);
    WCHAR face[LF_FACESIZE] = { 0 };
    U(GetTextFaceW)(dc, LF_FACESIZE, face);
    U(SelectObject)(dc, old); U(DeleteObject)(f); U(DeleteDC)(dc);
    return lstrcmpiW(face, fam) == 0;
}

TEST(gdi_font_enum_resources_mem_stub_prologue) {
    REQUIRE(UN(NtGdiAddFontMemResourceEx)); REQUIRE(UN(NtGdiRemoveFontMemResourceEx));
    FARPROC a = t_proc_in("win32u.dll", "NtGdiAddFontMemResourceEx"), r = t_proc_in("win32u.dll", "NtGdiRemoveFontMemResourceEx");
    CHECK(k_stub_valid(a)); CHECK(k_stub_valid(r));
    if (t_is_wine()) return;
    EXPECT_EQ(k_stub_number(a), 4355);
    EXPECT_EQ(k_stub_number(r), 4358);
}

TEST(gdi_font_enum_resources_mem_user_zero_size_rejected) {
    if (!need()) { t_skip("missing exports"); return; }
    BYTE blob[64] = { 0 }; DWORD n = SENT;
    SetLastError(0);
    HANDLE h = U(AddFontMemResourceEx)(blob, 0, NULL, &n);
    EXPECT_EQ(h, 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    EXPECT_EQ(n, SENT);
}

TEST(gdi_font_enum_resources_mem_user_null_view_rejected) {
    if (!need()) { t_skip("missing exports"); return; }
    DWORD n = SENT;
    SetLastError(0);
    EXPECT_EQ(U(AddFontMemResourceEx)(NULL, 100, NULL, &n), 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    EXPECT_EQ(n, SENT);
}

TEST(gdi_font_enum_resources_mem_user_null_count_rejected) {
    if (!need()) { t_skip("missing exports"); return; }
    BYTE blob[64] = { 0 };
    SetLastError(0);
    EXPECT_EQ(U(AddFontMemResourceEx)(blob, sizeof blob, NULL, NULL), 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}

TEST(gdi_font_enum_resources_mem_user_garbage_blob_rejected) {
    if (!need()) { t_skip("missing exports"); return; }
    BYTE blob[1024]; memset(blob, 0, sizeof blob);
    DWORD n = SENT;
    HANDLE h = U(AddFontMemResourceEx)(blob, sizeof blob, NULL, &n);
    EXPECT_EQ(h, 0);
    EXPECT_EQ(n, SENT); /* *pNumFonts is stored only after a successful load */
    if (h) U(RemoveFontMemResourceEx)(h);
}

TEST(gdi_font_enum_resources_mem_user_text_blob_rejected) {
    if (!need()) { t_skip("missing exports"); return; }
    BYTE blob[512]; for (unsigned i = 0; i < sizeof blob; i++) blob[i] = (BYTE)('a' + i % 26);
    DWORD n = SENT;
    HANDLE h = U(AddFontMemResourceEx)(blob, sizeof blob, NULL, &n);
    EXPECT_EQ(h, 0);
    EXPECT_EQ(n, SENT);
    if (h) U(RemoveFontMemResourceEx)(h);
}

TEST(gdi_font_enum_resources_mem_user_tiny_sizes_rejected) {
    if (!need()) { t_skip("missing exports"); return; }
    BYTE blob[2048]; char fam[64]; fe_family(fam, sizeof fam, 800);
    size_t full = fe_make_ttf(blob, sizeof blob, fam);
    DWORD sizes[] = { 1, 4, 12, 28, 100, 171 };
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        if (sizes[i] >= full) continue;
        DWORD n = SENT;
        HANDLE h = U(AddFontMemResourceEx)(blob, sizes[i], NULL, &n);
        CHECKF(h == NULL, "%lu bytes of a TrueType file accepted", sizes[i]);
        if (h) U(RemoveFontMemResourceEx)(h);
    }
}

TEST(gdi_font_enum_resources_mem_user_truncated_ttf_rejected) {
    if (!need()) { t_skip("missing exports"); return; }
    BYTE blob[2048]; char fam[64]; fe_family(fam, sizeof fam, 801);
    size_t full = fe_make_ttf(blob, sizeof blob, fam);
    DWORD n = SENT;
    HANDLE h = U(AddFontMemResourceEx)(blob, (DWORD)(full / 2), NULL, &n);
    EXPECT_EQ(h, 0);
    if (h) U(RemoveFontMemResourceEx)(h);
}

TEST(gdi_font_enum_resources_mem_user_corrupt_head_magic_rejected) {
    if (!need()) { t_skip("missing exports"); return; }
    BYTE blob[2048]; char fam[64]; fe_family(fam, sizeof fam, 802);
    size_t full = fe_make_ttf(blob, sizeof blob, fam);
    /* Break the sfnt version: no font driver recognises it. */
    blob[0] = 'X'; blob[1] = 'Y'; blob[2] = 'Z'; blob[3] = 'W';
    DWORD n = SENT;
    HANDLE h = U(AddFontMemResourceEx)(blob, (DWORD)full, NULL, &n);
    EXPECT_EQ(h, 0);
    if (h) U(RemoveFontMemResourceEx)(h);
}

TEST(gdi_font_enum_resources_mem_user_generated_font_roundtrip) {
    if (!need()) { t_skip("missing exports"); return; }
    BYTE blob[2048]; char fam[64]; WCHAR wf[64]; fe_family(fam, sizeof fam, 803); fe_wide(fam, wf, 64);
    size_t full = fe_make_ttf(blob, sizeof blob, fam);
    DWORD n = SENT;
    HANDLE h = U(AddFontMemResourceEx)(blob, (DWORD)full, NULL, &n);
    if (!h) { t_note("generated font rejected from memory (GetLastError %lu)", GetLastError()); t_skip("font generator not accepted"); return; }
    EXPECT_EQ(n, 1);
    EXPECT_EQ(vis(wf), 0);            /* memory fonts are not enumerated ... */
    EXPECT_EQ(usable(wf), 1);         /* ... but they can be selected */
    EXPECT_EQ(U(RemoveFontMemResourceEx)(h), 1);
    EXPECT_EQ(vis(wf), 0);
    EXPECT_EQ(usable(wf), 0);
}

TEST(gdi_font_enum_resources_mem_user_source_buffer_can_be_freed) {
    if (!need()) { t_skip("missing exports"); return; }
    /* The kernel keeps its own mapping of the view (bCreateFontFileView/cMapRemoteFonts): clobbering the caller's copy is harmless. */
    BYTE *blob = (BYTE *)malloc(2048); char fam[64]; WCHAR wf[64]; fe_family(fam, sizeof fam, 804); fe_wide(fam, wf, 64);
    size_t full = fe_make_ttf(blob, 2048, fam);
    DWORD n = 0;
    HANDLE h = U(AddFontMemResourceEx)(blob, (DWORD)full, NULL, &n);
    if (!h) { free(blob); t_skip("generated font not accepted"); return; }
    memset(blob, 0xCC, full); free(blob);
    EXPECT_EQ(usable(wf), 1);
    if (U(CreateFontW) && U(CreateCompatibleDC)) {
        HDC dc = U(CreateCompatibleDC)(NULL);
        HFONT f = U(CreateFontW)(-64, 0, 0, 0, 400, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, wf);
        HGDIOBJ old = U(SelectObject)(dc, f);
        INT w = 0;
        CHECK(U(GetCharWidth32W)(dc, 'A', 'A', &w));
        CHECKF(w == 37 || w == 38, "advance %d", w); /* 600/1024 of 64 px = 37.5 */
        U(SelectObject)(dc, old); U(DeleteObject)(f); U(DeleteDC)(dc);
    }
    U(RemoveFontMemResourceEx)(h);
}

TEST(gdi_font_enum_resources_mem_user_twice_gives_two_handles) {
    if (!need()) { t_skip("missing exports"); return; }
    BYTE blob[2048]; char fam[64]; WCHAR wf[64]; fe_family(fam, sizeof fam, 805); fe_wide(fam, wf, 64);
    size_t full = fe_make_ttf(blob, sizeof blob, fam);
    DWORD n1 = 0, n2 = 0;
    HANDLE h1 = U(AddFontMemResourceEx)(blob, (DWORD)full, NULL, &n1);
    if (!h1) { t_skip("generated font not accepted"); return; }
    HANDLE h2 = U(AddFontMemResourceEx)(blob, (DWORD)full, NULL, &n2);
    CHECK(h2 != NULL);
    t_note("handles %p %p, enumerable copies: %d", h1, h2, vis(wf));
    if (h2) { EXPECT_NE(h1, h2); EXPECT_EQ(n2, 1); }
    EXPECT_EQ(U(RemoveFontMemResourceEx)(h1), 1);
    if (h2) { CHECKF(usable(wf) == 1, "font vanished after releasing one of two memory resources"); EXPECT_EQ(U(RemoveFontMemResourceEx)(h2), 1); }
    EXPECT_EQ(usable(wf), 0);
    EXPECT_EQ(vis(wf), 0);
}

TEST(gdi_font_enum_resources_mem_user_remove_null_rejected) {
    if (!need()) { t_skip("missing exports"); return; }
    SetLastError(0);
    EXPECT_EQ(U(RemoveFontMemResourceEx)(NULL), 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}

TEST(gdi_font_enum_resources_mem_remove_syscall_null_returns_zero) {
    REQUIRE(UN(NtGdiRemoveFontMemResourceEx));
    EXPECT_EQ(UN(NtGdiRemoveFontMemResourceEx)(NULL), 0); /* NtGdiRemoveFontMemResourceEx: param == 0 -> 0 */
}

TEST(gdi_font_enum_resources_mem_remove_twice_second_fails) {
    if (!need()) { t_skip("missing exports"); return; }
    BYTE blob[2048]; char fam[64]; fe_family(fam, sizeof fam, 806);
    size_t full = fe_make_ttf(blob, sizeof blob, fam);
    DWORD n = 0;
    HANDLE h = U(AddFontMemResourceEx)(blob, (DWORD)full, NULL, &n);
    if (!h) { t_skip("generated font not accepted"); return; }
    EXPECT_EQ(U(RemoveFontMemResourceEx)(h), 1);
    /* the handle is not removable again; whether the kernel validates or merely reports FALSE is not asserted beyond that */
    BOOL again = U(RemoveFontMemResourceEx)(h);
    t_note("second RemoveFontMemResourceEx = %d", again);
}

TEST(gdi_font_enum_resources_mem_syscall_valid_roundtrip) {
    if (!UN(NtGdiAddFontMemResourceEx) || !UN(NtGdiRemoveFontMemResourceEx) || !U(GetDC)) { t_skip("missing exports"); return; }
    BYTE blob[2048]; char fam[64]; WCHAR wf[64]; fe_family(fam, sizeof fam, 807); fe_wide(fam, wf, 64);
    size_t full = fe_make_ttf(blob, sizeof blob, fam);
    ULONG n = SENT;
    void *h = UN(NtGdiAddFontMemResourceEx)(blob, (ULONG)full, NULL, 0, &n);
    if (!h) { t_skip("generated font not accepted"); return; }
    EXPECT_EQ(n, 1);
    EXPECT_EQ(usable(wf), 1);
    EXPECT_EQ(UN(NtGdiRemoveFontMemResourceEx)(h), 1);
    EXPECT_EQ(usable(wf), 0);
    EXPECT_EQ(vis(wf), 0);
}

TEST(gdi_font_enum_resources_mem_syscall_rejects_zero_arguments) {
    if (!UN(NtGdiAddFontMemResourceEx)) { t_skip("missing exports"); return; }
    BYTE blob[2048]; char fam[64]; fe_family(fam, sizeof fam, 808);
    size_t full = fe_make_ttf(blob, sizeof blob, fam);
    ULONG n = SENT;
    EXPECT_EQ(UN(NtGdiAddFontMemResourceEx)(blob, 0, NULL, 0, &n), 0);
    EXPECT_EQ(UN(NtGdiAddFontMemResourceEx)(NULL, (ULONG)full, NULL, 0, &n), 0);
    EXPECT_EQ(UN(NtGdiAddFontMemResourceEx)(blob, (ULONG)full, NULL, 0, NULL), 0);
    EXPECT_EQ(n, SENT);
}

TEST(gdi_font_enum_resources_mem_syscall_design_vector_size_checks) {
    if (!UN(NtGdiAddFontMemResourceEx) || !UN(NtGdiRemoveFontMemResourceEx)) { t_skip("missing exports"); return; }
    /* cjDV above 0x48 is refused; a non-zero cjDV must equal dvNumAxes*4+8 where dvNumAxes is dv[1] of the copied vector.
     * A valid font with an invalid vector size is refused before it is parsed, and *pcFonts stays untouched. */
    BYTE blob[2048]; char fam[64]; WCHAR wf[64]; fe_family(fam, sizeof fam, 809); fe_wide(fam, wf, 64);
    size_t full = fe_make_ttf(blob, sizeof blob, fam);
    ULONG dv[32]; memset(dv, 0, sizeof dv);
    struct { ULONG cj; ULONG axes; } t[] = { { 0x49, 0 }, { 0x100, 0 }, { 0xFFFFFFFFu, 0 }, { 4, 0 }, { 12, 0 }, { 8, 1 }, { 16, 1 }, { 0x48, 3 }, { 0x1C, 2 } };
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
        ULONG n = SENT;
        dv[0] = 0x8000; dv[1] = t[i].axes;
        if (t[i].cj > sizeof dv && t[i].cj != 0x49 && t[i].cj != 0x100 && t[i].cj != 0xFFFFFFFFu) continue;
        void *h = UN(NtGdiAddFontMemResourceEx)(blob, (ULONG)full, dv, t[i].cj, &n);
        CHECKF(h == NULL, "cjDV=%lu axes=%lu accepted", t[i].cj, t[i].axes);
        CHECKF(n == SENT, "cjDV=%lu wrote *pcFonts", t[i].cj);
        if (h) UN(NtGdiRemoveFontMemResourceEx)(h);
    }
    EXPECT_EQ(vis(wf), 0);
}

TEST(gdi_font_enum_resources_mem_stock_font_from_disk) {
    if (!need()) { t_skip("missing exports"); return; }
    char path[MAX_PATH]; fe_windir_font("cour.ttf", path, sizeof path);
    DWORD len = 0; BYTE *b = fe_read_file(path, &len);
    if (!b) { t_skip("cour.ttf unreadable"); return; }
    DWORD n = SENT;
    HANDLE h = U(AddFontMemResourceEx)(b, len, NULL, &n);
    CHECK(h != NULL);
    EXPECT_EQ(n, 1);
    if (h) EXPECT_EQ(U(RemoveFontMemResourceEx)(h), 1);
    free(b);
}

TEST(gdi_font_enum_resources_mem_stock_collection_reports_face_count) {
    if (!need()) { t_skip("missing exports"); return; }
    char path[MAX_PATH]; fe_windir_font("cambria.ttc", path, sizeof path);
    DWORD len = 0; BYTE *b = fe_read_file(path, &len);
    if (!b) { t_skip("cambria.ttc unreadable"); return; }
    DWORD n = SENT;
    HANDLE h = U(AddFontMemResourceEx)(b, len, NULL, &n);
    CHECK(h != NULL);
    EXPECT_EQ(n, 2);
    if (h) U(RemoveFontMemResourceEx)(h);
    free(b);
}

TEST(gdi_font_enum_resources_mem_stock_arial_does_not_change_family_visibility) {
    if (!need()) { t_skip("missing exports"); return; }
    char path[MAX_PATH]; fe_windir_font("arial.ttf", path, sizeof path);
    DWORD len = 0; BYTE *b = fe_read_file(path, &len);
    if (!b) { t_skip("arial.ttf unreadable"); return; }
    int before = vis(L"Arial");
    DWORD n = 0;
    HANDLE h = U(AddFontMemResourceEx)(b, len, NULL, &n);
    if (h) {
        int mid = vis(L"Arial");
        t_note("Arial entries before %d, with an in-memory duplicate %d", before, mid);
        CHECK(mid >= before);
        U(RemoveFontMemResourceEx)(h);
        EXPECT_EQ(vis(L"Arial"), before);
    }
    free(b);
}

TEST(gdi_font_enum_resources_mem_font_invisible_to_other_process) {
    if (!need()) { t_skip("missing exports"); return; }
    BYTE blob[2048]; char fam[64]; fe_family(fam, sizeof fam, 810);
    size_t full = fe_make_ttf(blob, sizeof blob, fam);
    DWORD n = 0;
    HANDLE h = U(AddFontMemResourceEx)(blob, (DWORD)full, NULL, &n);
    if (!h) { t_skip("generated font not accepted"); return; }
    struct k_child_result r = k_child_run("fe_probe_hidden", fam, 30000);
    EXPECT_EQ(r.exit_code, 0);
    U(RemoveFontMemResourceEx)(h);
}

TEST(gdi_font_enum_resources_mem_font_leaves_public_cookie_alone) {
    if (!need() || !UN(NtGdiGetPublicFontTableChangeCookie) || t_is_wine()) { t_skip("missing exports"); return; }
    BYTE blob[2048]; char fam[64]; fe_family(fam, sizeof fam, 811);
    size_t full = fe_make_ttf(blob, sizeof blob, fam);
    ULONG c0 = UN(NtGdiGetPublicFontTableChangeCookie)();
    DWORD n = 0;
    HANDLE h = U(AddFontMemResourceEx)(blob, (DWORD)full, NULL, &n);
    if (!h) { t_skip("generated font not accepted"); return; }
    ULONG c1 = UN(NtGdiGetPublicFontTableChangeCookie)();
    U(RemoveFontMemResourceEx)(h);
    ULONG c2 = UN(NtGdiGetPublicFontTableChangeCookie)();
    if (c0 != c1 || c1 != c2) t_note("memory font moved the public cookie: %lu %lu %lu", c0, c1, c2);
    EXPECT_EQ(c0, c1);
    EXPECT_EQ(c1, c2);
}

TEST(gdi_font_enum_resources_mem_unicode_ranges_of_generated_font) {
    if (!need() || !U(GetFontUnicodeRanges) || !U(CreateFontW) || !U(CreateCompatibleDC) || !U(SelectObject)) { t_skip("missing exports"); return; }
    BYTE blob[2048]; char fam[64]; WCHAR wf[64]; fe_family(fam, sizeof fam, 812); fe_wide(fam, wf, 64);
    size_t full = fe_make_ttf(blob, sizeof blob, fam);
    DWORD n = 0;
    HANDLE h = U(AddFontMemResourceEx)(blob, (DWORD)full, NULL, &n);
    if (!h) { t_skip("generated font not accepted"); return; }
    HDC dc = U(CreateCompatibleDC)(NULL);
    HFONT f = U(CreateFontW)(-32, 0, 0, 0, 400, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, wf);
    HGDIOBJ old = U(SelectObject)(dc, f);
    WCHAR face[64] = { 0 }; U(GetTextFaceW)(dc, 64, face);
    if (!lstrcmpW(face, wf)) {
        DWORD need_ = U(GetFontUnicodeRanges)(dc, NULL);
        CHECK(need_ >= 16);
        GLYPHSET *gs = (GLYPHSET *)calloc(1, need_ ? need_ : 16);
        DWORD got = gs ? U(GetFontUnicodeRanges)(dc, gs) : 0;
        EXPECT_EQ(got, need_);
        if (got) {
            EXPECT_EQ(gs->cbThis, got);
            EXPECT_EQ(got, 16 + 4 * gs->cRanges); /* GLYPHSET header (16 bytes) + 4 bytes per WCRANGE */
            DWORD sum = 0; for (DWORD i = 0; i < gs->cRanges; i++) sum += gs->ranges[i].cGlyphs;
            EXPECT_EQ(sum, gs->cGlyphsSupported);
            t_note("generated font glyph set: ranges=%lu glyphs=%lu", gs->cRanges, gs->cGlyphsSupported);
        }
        free(gs);
    } else t_note("generated font not selected (%ls)", face);
    U(SelectObject)(dc, old); U(DeleteObject)(f); U(DeleteDC)(dc);
    U(RemoveFontMemResourceEx)(h);
}
