/* Remote/merge/embedded font attach calls on a DC: NtGdiAddRemoteFontToDC, NtGdiRemoveMergeFont and NtGdiAddEmbFontToDC.
 * For a live DC with ordinary data they return 0 and leave the output id untouched and the DC's font state unchanged;
 * for every wrong handle type they return 0 as well. No font is installed: the data fed to the calls is garbage or a
 * read-only copy of a system font file, and nothing is left behind (the embedded font counter is checked). */
#include "xf.h"

static void *slurp_arial(DWORD *sz) {
    WCHAR p[MAX_PATH]; if (!GetWindowsDirectoryW(p, MAX_PATH)) return NULL; lstrcatW(p, L"\\Fonts\\arial.ttf");
    HANDLE h = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    *sz = GetFileSize(h, NULL); void *b = HeapAlloc(GetProcessHeap(), 0, *sz); DWORD r = 0;
    if (!b || !ReadFile(h, b, *sz, &r, NULL) || r != *sz) { CloseHandle(h); return NULL; }
    CloseHandle(h); return b;
}

XT(remote_add_junk_data_returns_zero) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiAddRemoteFontToDC));
    HDC dc = U(CreateCompatibleDC)(NULL);
    unsigned char junk[64]; memset(junk, 0, sizeof junk);
    ULONG64 ufi = 0x55;
    CHECK(XF_NT(NtGdiAddRemoteFontToDC)(dc, junk, sizeof junk, &ufi) == 0);
    CHECK(ufi == 0x55);
    memset(junk, 0xEE, sizeof junk);
    CHECK(XF_NT(NtGdiAddRemoteFontToDC)(dc, junk, sizeof junk, &ufi) == 0);
    CHECK(ufi == 0x55);
    U(DeleteDC)(dc);
}
XT(remote_add_null_buffer_returns_zero) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiAddRemoteFontToDC));
    HDC dc = U(CreateCompatibleDC)(NULL); ULONG64 ufi = 0x55;
    CHECK(XF_NT(NtGdiAddRemoteFontToDC)(dc, NULL, 0, NULL) == 0);
    CHECK(XF_NT(NtGdiAddRemoteFontToDC)(dc, NULL, 0, &ufi) == 0);
    CHECK(XF_NT(NtGdiAddRemoteFontToDC)(dc, NULL, 64, &ufi) == 0);
    CHECK(ufi == 0x55);
    U(DeleteDC)(dc);
}
XT(remote_add_zero_length_returns_zero) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiAddRemoteFontToDC));
    HDC dc = U(CreateCompatibleDC)(NULL); ULONG64 ufi = 0x55; char b[16] = { 0 };
    CHECK(XF_NT(NtGdiAddRemoteFontToDC)(dc, b, 0, &ufi) == 0);
    CHECK(XF_NT(NtGdiAddRemoteFontToDC)(dc, b, 1, &ufi) == 0);
    CHECK(ufi == 0x55);
    U(DeleteDC)(dc);
}
XT(remote_add_without_id_pointer) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiAddRemoteFontToDC));
    HDC dc = U(CreateCompatibleDC)(NULL); char b[64] = { 0 };
    CHECK(XF_NT(NtGdiAddRemoteFontToDC)(dc, b, sizeof b, NULL) == 0);
    U(DeleteDC)(dc);
}
XT(remote_add_real_font_file_does_not_attach) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiAddRemoteFontToDC)); XF_NEED(XF_NT(NtGdiGetEmbedFonts));
    DWORD sz = 0; void *b = slurp_arial(&sz);
    if (!b) { t_skip("system font file not readable"); return; }
    HDC dc = U(CreateCompatibleDC)(NULL); ULONG64 ufi = 0x55; ULONG base = XF_NT(NtGdiGetEmbedFonts)();
    ULONG r = XF_NT(NtGdiAddRemoteFontToDC)(dc, b, sz, &ufi);
    CHECK(r == 0);                      /* a user process cannot hand a merge font to a DC */
    CHECK(ufi == 0x55);
    CHECK(XF_NT(NtGdiGetEmbedFonts)() == base);
    CHECK(XF_NT(NtGdiRemoveMergeFont)(dc, &ufi) == 0);
    U(DeleteDC)(dc); HeapFree(GetProcessHeap(), 0, b);
}
XT(remote_add_truncated_font_returns_zero) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiAddRemoteFontToDC));
    DWORD sz = 0; void *b = slurp_arial(&sz);
    if (!b) { t_skip("system font file not readable"); return; }
    HDC dc = U(CreateCompatibleDC)(NULL); ULONG64 ufi = 0x55;
    CHECK(XF_NT(NtGdiAddRemoteFontToDC)(dc, b, 100, &ufi) == 0);
    CHECK(XF_NT(NtGdiAddRemoteFontToDC)(dc, b, 12, &ufi) == 0);
    CHECK(ufi == 0x55);
    U(DeleteDC)(dc); HeapFree(GetProcessHeap(), 0, b);
}
XT(remote_add_does_not_change_font_state) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiAddRemoteFontToDC));
    XD(d, L"Arial", 161, 0);
    char b[64] = { 0 }; ULONG64 ufi = 0;
    WCHAR before[64]; wcscpy(before, d.face);
    XF_NT(NtGdiAddRemoteFontToDC)(d.dc, b, sizeof b, &ufi);
    WCHAR after[64]; after[0] = 0; U(GetTextFaceW)(d.dc, 64, after);
    CHECK(xf_wcseq(before, after));
    CHECK(U(GetTextCharset)(d.dc) == d.tm.tmCharSet);
    TEXTMETRICW tm; U(GetTextMetricsW)(d.dc, &tm);
    CHECK(tm.tmHeight == d.tm.tmHeight && tm.tmAveCharWidth == d.tm.tmAveCharWidth);
    xd_close(&d);
}
XT(remote_add_bad_handles_all_zero) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiAddRemoteFontToDC)); XH(x);
    char b[64] = { 0 };
    for (int k = 0; k < H_COUNT; k++) {
        ULONG64 ufi = 0x55;
        CHECKF(XF_NT(NtGdiAddRemoteFontToDC)((HDC)x.h[k], b, sizeof b, &ufi) == 0, "%s", xh_name[k]);
        CHECKF(ufi == 0x55, "%s", xh_name[k]);
        CHECKF(XF_NT(NtGdiAddRemoteFontToDC)((HDC)x.h[k], NULL, 0, NULL) == 0, "%s null", xh_name[k]);
    }
    xh_close(&x);
}
XT(merge_remove_unknown_id_returns_zero) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiRemoveMergeFont));
    HDC dc = U(CreateCompatibleDC)(NULL);
    static const ULONG64 ids[] = { 0, 1, 0x1234, 0xffffffffull, 0xffffffffffffffffull, 0x8000000000000000ull };
    for (unsigned i = 0; i < sizeof ids / sizeof *ids; i++) {
        ULONG64 id = ids[i];
        CHECKF(XF_NT(NtGdiRemoveMergeFont)(dc, &id) == 0, "id %u", i);
        CHECKF(id == ids[i], "id %u untouched", i);
    }
    U(DeleteDC)(dc);
}
XT(merge_remove_null_id_returns_zero) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiRemoveMergeFont));
    HDC dc = U(CreateCompatibleDC)(NULL);
    CHECK(XF_NT(NtGdiRemoveMergeFont)(dc, NULL) == 0);
    U(DeleteDC)(dc);
}
XT(merge_remove_twice_is_harmless) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiRemoveMergeFont));
    XD(d, L"Arial", 0, 0); ULONG64 id = 7;
    for (int i = 0; i < 3; i++) CHECK(XF_NT(NtGdiRemoveMergeFont)(d.dc, &id) == 0);
    CHECK(U(GetTextCharset)(d.dc) == d.tm.tmCharSet);
    xd_close(&d);
}
XT(merge_remove_bad_handles_all_zero) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiRemoveMergeFont)); XH(x);
    for (int k = 0; k < H_COUNT; k++) { ULONG64 id = 3; CHECKF(XF_NT(NtGdiRemoveMergeFont)((HDC)x.h[k], &id) == 0, "%s", xh_name[k]); CHECKF(id == 3, "%s", xh_name[k]); }
    xh_close(&x);
}
XT(embfont_add_unknown_id_returns_zero) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiAddEmbFontToDC));
    HDC dc = U(CreateCompatibleDC)(NULL);
    static const ULONG64 ids[] = { 0, 1, 0x1234, 0xffffffffull, 0xffffffffffffffffull };
    for (unsigned i = 0; i < sizeof ids / sizeof *ids; i++) {
        ULONG64 id = ids[i];
        CHECKF(XF_NT(NtGdiAddEmbFontToDC)(dc, &id) == 0, "id %u", i);
        CHECKF(id == ids[i], "id %u untouched", i);
    }
    U(DeleteDC)(dc);
}
XT(embfont_add_bad_handles_all_zero) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiAddEmbFontToDC)); XH(x);
    for (int k = 0; k < H_COUNT; k++) { ULONG64 id = 3; CHECKF(XF_NT(NtGdiAddEmbFontToDC)((HDC)x.h[k], &id) == 0, "%s", xh_name[k]); CHECKF(id == 3, "%s", xh_name[k]); }
    xh_close(&x);
}
XT(embfont_add_leaves_embed_count_alone) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiAddEmbFontToDC)); XF_NEED(XF_NT(NtGdiGetEmbedFonts));
    HDC dc = U(CreateCompatibleDC)(NULL); ULONG base = XF_NT(NtGdiGetEmbedFonts)(); ULONG64 id = 9;
    XF_NT(NtGdiAddEmbFontToDC)(dc, &id);
    CHECK(XF_NT(NtGdiGetEmbedFonts)() == base);
    U(DeleteDC)(dc);
}
