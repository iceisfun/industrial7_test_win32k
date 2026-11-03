/* AddFontResourceEx / RemoveFontResourceEx and the NtGdiAddFontResourceW / NtGdiRemoveFontResourceW syscalls behind them.
 *
 * gdi32full!AddFontResourceExW rejects (fl & 0xffffffcf) with ERROR_INVALID_PARAMETER before any syscall; only FR_PRIVATE (0x10)
 * and FR_NOT_ENUM (0x20) pass. RemoveFontResourceExW has the same gate, then a NULL-name check. The path is converted to an NT path
 * by pwszAllocNtMultiplePath and handed to NtGdiAddFontResourceW(path, cwc, cFiles, flags, dvSize, pdv), where bCheckAndCapThePath
 * demands cwc >= 2, a terminating NUL and cFiles == (number of '|' separators + 1). Flags 0x80000000 are only honoured for csrss.
 * Every test removes whatever it added, also after a failed check. */
#include "fe.h"
#include <stdlib.h>

struct gen { char family[64]; WCHAR wfam[64]; char path[MAX_PATH]; WCHAR wpath[MAX_PATH]; WCHAR ntpath[MAX_PATH + 8]; ULONG ntcwc; };
static int gen_n;
static int gen_make(struct gen *g) {
    memset(g, 0, sizeof *g);
    fe_family(g->family, sizeof g->family, ++gen_n);
    fe_wide(g->family, g->wfam, 64);
    if (!fe_write_ttf(g->family, g->path, sizeof g->path)) return 0;
    fe_wide(g->path, g->wpath, MAX_PATH);
    fe_nt_path(g->path, g->ntpath, MAX_PATH + 8, &g->ntcwc);
    return 1;
}
static int need(void) { return U(AddFontResourceExW) && U(RemoveFontResourceExW) && U(EnumFontFamiliesExW) && U(GetDC) && U(ReleaseDC); }
static int visible(const struct gen *g) { HDC dc = U(GetDC)(NULL); int n = fe_count_family(dc, g->wfam, DEFAULT_CHARSET); U(ReleaseDC)(NULL, dc); return n; }

#define GEN(g) struct gen g; if (!need()) { t_skip("missing exports"); return; } if (!gen_make(&g)) { t_skip("cannot write temp font"); return; }

TEST(gdi_font_enum_resources_add_stub_prologue) {
    REQUIRE(UN(NtGdiAddFontResourceW)); REQUIRE(UN(NtGdiRemoveFontResourceW));
    FARPROC a = t_proc_in("win32u.dll", "NtGdiAddFontResourceW"), r = t_proc_in("win32u.dll", "NtGdiRemoveFontResourceW");
    CHECK(k_stub_valid(a)); CHECK(k_stub_valid(r));
    if (t_is_wine()) return;
    EXPECT_EQ(k_stub_number(a), 4478);
    EXPECT_EQ(k_stub_number(r), 4856);
}

TEST(gdi_font_enum_resources_add_ex_invalid_flags_rejected) {
    GEN(g);
    DWORD bad[] = { 1, 2, 4, 8, 0x40, 0x80, 0x100, 0x200, 0x8000, 0x10000, 0x40000000, 0x80000000, 0xFFFFFFFF, 0x11, 0x31, 0x50 };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(0);
        int r = U(AddFontResourceExW)(g.wpath, bad[i], NULL);
        CHECKF(r == 0, "flags 0x%lx accepted", bad[i]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
        if (r) U(RemoveFontResourceExW)(g.wpath, bad[i], NULL);
    }
    EXPECT_EQ(visible(&g), 0);
}

TEST(gdi_font_enum_resources_remove_ex_invalid_flags_rejected) {
    GEN(g);
    DWORD bad[] = { 1, 2, 4, 8, 0x40, 0x80, 0x100, 0x8000, 0x80000000, 0xFFFFFFFF, 0x11, 0x31 };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(0);
        BOOL r = U(RemoveFontResourceExW)(g.wpath, bad[i], NULL);
        EXPECT_EQ(r, 0);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
}

TEST(gdi_font_enum_resources_add_ex_invalid_flags_checked_before_name) {
    if (!need()) { t_skip("missing exports"); return; }
    SetLastError(0);
    EXPECT_EQ(U(AddFontResourceExW)(NULL, 0x40, NULL), 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SetLastError(0);
    EXPECT_EQ(U(RemoveFontResourceExW)(NULL, 0x40, NULL), 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}

TEST(gdi_font_enum_resources_remove_ex_null_name_false) {
    if (!need()) { t_skip("missing exports"); return; }
    EXPECT_EQ(U(RemoveFontResourceExW)(NULL, 0, NULL), 0);
    EXPECT_EQ(U(RemoveFontResourceExW)(NULL, FR_PRIVATE, NULL), 0);
    EXPECT_EQ(U(RemoveFontResourceExW)(NULL, FR_PRIVATE | FR_NOT_ENUM, NULL), 0);
}

TEST(gdi_font_enum_resources_add_nonexistent_file_zero) {
    if (!need()) { t_skip("missing exports"); return; }
    WCHAR p[MAX_PATH]; char a[MAX_PATH]; t_tmpfile(a, sizeof a, "no_such_font_file.ttf"); fe_wide(a, p, MAX_PATH);
    EXPECT_EQ(U(AddFontResourceExW)(p, FR_PRIVATE, NULL), 0);
    EXPECT_EQ(U(AddFontResourceExW)(p, 0, NULL), 0);
    EXPECT_EQ(U(AddFontResourceExW)(p, FR_PRIVATE | FR_NOT_ENUM, NULL), 0);
}

TEST(gdi_font_enum_resources_add_nonexistent_directory_zero) {
    if (!need()) { t_skip("missing exports"); return; }
    WCHAR p[MAX_PATH]; fe_wide("C:\\win32kt_no_such_dir\\deeper\\f.ttf", p, MAX_PATH);
    EXPECT_EQ(U(AddFontResourceExW)(p, FR_PRIVATE, NULL), 0);
}

TEST(gdi_font_enum_resources_add_text_file_zero) {
    if (!need()) { t_skip("missing exports"); return; }
    char a[MAX_PATH]; WCHAR w[MAX_PATH]; t_tmpfile(a, sizeof a, "notafont.ttf");
    HANDLE h = CreateFileA(a, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    DWORD wr; const char txt[] = "this is definitely not a font file, only text\r\n";
    WriteFile(h, txt, sizeof txt, &wr, NULL); CloseHandle(h);
    fe_wide(a, w, MAX_PATH);
    EXPECT_EQ(U(AddFontResourceExW)(w, FR_PRIVATE, NULL), 0);
    EXPECT_EQ(U(RemoveFontResourceExW)(w, FR_PRIVATE, NULL), 0);
}

TEST(gdi_font_enum_resources_add_empty_file_zero) {
    if (!need()) { t_skip("missing exports"); return; }
    char a[MAX_PATH]; WCHAR w[MAX_PATH]; t_tmpfile(a, sizeof a, "empty.ttf");
    HANDLE h = CreateFileA(a, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL); CloseHandle(h);
    fe_wide(a, w, MAX_PATH);
    EXPECT_EQ(U(AddFontResourceExW)(w, FR_PRIVATE, NULL), 0);
}

TEST(gdi_font_enum_resources_add_truncated_ttf_zero) {
    if (!need()) { t_skip("missing exports"); return; }
    BYTE buf[2048]; char fam[64]; fe_family(fam, sizeof fam, 900);
    size_t n = fe_make_ttf(buf, sizeof buf, fam);
    char a[MAX_PATH]; WCHAR w[MAX_PATH]; t_tmpfile(a, sizeof a, "trunc.ttf");
    HANDLE h = CreateFileA(a, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL); DWORD wr;
    WriteFile(h, buf, (DWORD)(n / 3), &wr, NULL); CloseHandle(h);
    fe_wide(a, w, MAX_PATH);
    EXPECT_EQ(U(AddFontResourceExW)(w, FR_PRIVATE, NULL), 0);
}

TEST(gdi_font_enum_resources_add_directory_path_zero) {
    if (!need()) { t_skip("missing exports"); return; }
    WCHAR w[MAX_PATH]; fe_wide(t_tmpdir(), w, MAX_PATH);
    EXPECT_EQ(U(AddFontResourceExW)(w, FR_PRIVATE, NULL), 0);
}

TEST(gdi_font_enum_resources_add_private_returns_one_and_enumerates) {
    GEN(g);
    int r = U(AddFontResourceExW)(g.wpath, FR_PRIVATE, NULL);
    if (r == 0) { t_note("generated TrueType font rejected (GetLastError %lu)", GetLastError()); t_skip("font generator not accepted"); return; }
    EXPECT_EQ(r, 1); /* one face in the file */
    EXPECT_EQ(visible(&g), 1);
    EXPECT_EQ(U(RemoveFontResourceExW)(g.wpath, FR_PRIVATE, NULL), 1);
    EXPECT_EQ(visible(&g), 0);
}

TEST(gdi_font_enum_resources_private_font_item_content) {
    GEN(g);
    if (!U(AddFontResourceExW)(g.wpath, FR_PRIVATE, NULL)) { t_skip("generated font not accepted"); return; }
    struct fe_set s; fe_set_init(&s);
    HDC dc = U(GetDC)(NULL);
    fe_enum(dc, g.wfam, DEFAULT_CHARSET, &s);
    CHECK(s.n >= 1);
    if (s.n) {
        CHECK(s.it[0].ftype & TRUETYPE_FONTTYPE);
        EXPECT_WSTR(s.it[0].face, g.wfam);
        EXPECT_WSTR(s.it[0].full, g.wfam); /* name id 4 */
        CHECK(s.it[0].weight == 400);
        t_note("generated font: charset=%u script=%ls style=%ls sizeEM=%lu ntmFlags=0x%lx", s.it[0].charset, s.it[0].script, s.it[0].style, s.it[0].sizeEM, s.it[0].ntmFlags);
        EXPECT_EQ(s.it[0].sizeEM, 1024);
    }
    fe_set_free(&s); U(ReleaseDC)(NULL, dc);
    U(RemoveFontResourceExW)(g.wpath, FR_PRIVATE, NULL);
}

TEST(gdi_font_enum_resources_add_private_twice_needs_two_removes) {
    GEN(g);
    int a = U(AddFontResourceExW)(g.wpath, FR_PRIVATE, NULL);
    if (!a) { t_skip("generated font not accepted"); return; }
    int b = U(AddFontResourceExW)(g.wpath, FR_PRIVATE, NULL);
    EXPECT_EQ(b, 1);
    EXPECT_EQ(visible(&g), 1); /* the second add bumps a reference count, it does not list the face twice */
    EXPECT_EQ(U(RemoveFontResourceExW)(g.wpath, FR_PRIVATE, NULL), 1);
    EXPECT_EQ(visible(&g), 1);
    EXPECT_EQ(U(RemoveFontResourceExW)(g.wpath, FR_PRIVATE, NULL), 1);
    EXPECT_EQ(visible(&g), 0);
    EXPECT_EQ(U(RemoveFontResourceExW)(g.wpath, FR_PRIVATE, NULL), 0);
}

TEST(gdi_font_enum_resources_remove_never_added_false) {
    GEN(g);
    SetLastError(0);
    EXPECT_EQ(U(RemoveFontResourceExW)(g.wpath, FR_PRIVATE, NULL), 0);
    EXPECT_EQ(U(RemoveFontResourceExW)(g.wpath, 0, NULL), 0);
    EXPECT_EQ(U(RemoveFontResourceExW)(g.wpath, FR_NOT_ENUM, NULL), 0);
}

TEST(gdi_font_enum_resources_remove_after_remove_false) {
    GEN(g);
    if (!U(AddFontResourceExW)(g.wpath, FR_PRIVATE, NULL)) { t_skip("generated font not accepted"); return; }
    EXPECT_EQ(U(RemoveFontResourceExW)(g.wpath, FR_PRIVATE, NULL), 1);
    EXPECT_EQ(U(RemoveFontResourceExW)(g.wpath, FR_PRIVATE, NULL), 0);
}

TEST(gdi_font_enum_resources_remove_name_case_insensitive) {
    GEN(g);
    if (!U(AddFontResourceExW)(g.wpath, FR_PRIVATE, NULL)) { t_skip("generated font not accepted"); return; }
    WCHAR up[MAX_PATH]; lstrcpyW(up, g.wpath); CharUpperW(up);
    BOOL r = U(RemoveFontResourceExW)(up, FR_PRIVATE, NULL);
    t_note("remove with upper-cased path: %d", r);
    if (!r) U(RemoveFontResourceExW)(g.wpath, FR_PRIVATE, NULL);
}

TEST(gdi_font_enum_resources_remove_private_with_wrong_flags) {
    GEN(g);
    if (!U(AddFontResourceExW)(g.wpath, FR_PRIVATE, NULL)) { t_skip("generated font not accepted"); return; }
    BOOL r0 = U(RemoveFontResourceExW)(g.wpath, 0, NULL);
    t_note("RemoveFontResourceExW(private font, flags 0) = %d", r0);
    int vis = visible(&g);
    if (r0) EXPECT_EQ(vis, 0);
    else { EXPECT_EQ(vis, 1); EXPECT_EQ(U(RemoveFontResourceExW)(g.wpath, FR_PRIVATE, NULL), 1); }
    EXPECT_EQ(visible(&g), 0);
}

TEST(gdi_font_enum_resources_not_enum_hidden_but_selectable) {
    GEN(g);
    if (!U(AddFontResourceExW)(g.wpath, FR_PRIVATE | FR_NOT_ENUM, NULL)) { t_skip("generated font not accepted"); return; }
    EXPECT_EQ(visible(&g), 0); /* FR_NOT_ENUM keeps the face out of the enumeration */
    if (U(CreateFontW) && U(CreateCompatibleDC) && U(SelectObject) && U(GetTextFaceW)) {
        HDC dc = U(CreateCompatibleDC)(NULL);
        HFONT f = U(CreateFontW)(-32, 0, 0, 0, 400, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, g.wfam);
        HGDIOBJ old = U(SelectObject)(dc, f);
        WCHAR face[64] = { 0 };
        U(GetTextFaceW)(dc, 64, face);
        EXPECT_WSTR(face, g.wfam); /* but the mapper still finds it by name */
        U(SelectObject)(dc, old); U(DeleteObject)(f); U(DeleteDC)(dc);
    }
    EXPECT_EQ(U(RemoveFontResourceExW)(g.wpath, FR_PRIVATE | FR_NOT_ENUM, NULL), 1);
}

TEST(gdi_font_enum_resources_private_font_selectable_then_gone_after_remove) {
    GEN(g);
    if (!U(AddFontResourceExW)(g.wpath, FR_PRIVATE, NULL)) { t_skip("generated font not accepted"); return; }
    HDC dc = U(CreateCompatibleDC)(NULL);
    HFONT f = U(CreateFontW)(-32, 0, 0, 0, 400, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, g.wfam);
    HGDIOBJ old = U(SelectObject)(dc, f);
    WCHAR face[64] = { 0 };
    U(GetTextFaceW)(dc, 64, face);
    EXPECT_WSTR(face, g.wfam);
    U(SelectObject)(dc, old); U(DeleteObject)(f);
    U(RemoveFontResourceExW)(g.wpath, FR_PRIVATE, NULL);
    f = U(CreateFontW)(-32, 0, 0, 0, 400, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, g.wfam);
    old = U(SelectObject)(dc, f);
    face[0] = 0; U(GetTextFaceW)(dc, 64, face);
    CHECKF(lstrcmpW(face, g.wfam) != 0, "mapper still returns the removed font %ls", face);
    U(SelectObject)(dc, old); U(DeleteObject)(f); U(DeleteDC)(dc);
}

TEST(gdi_font_enum_resources_generated_font_metrics_via_textmetric) {
    GEN(g);
    if (!U(AddFontResourceExW)(g.wpath, FR_PRIVATE, NULL)) { t_skip("generated font not accepted"); return; }
    HDC dc = U(CreateCompatibleDC)(NULL);
    HFONT f = U(CreateFontW)(-1024, 0, 0, 0, 400, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, g.wfam);
    HGDIOBJ old = U(SelectObject)(dc, f);
    WCHAR face[64] = { 0 }; U(GetTextFaceW)(dc, 64, face);
    if (!lstrcmpW(face, g.wfam)) {
        TEXTMETRICW tm; INT w = 0;
        CHECK(U(GetTextMetricsW)(dc, &tm));
        /* hhea ascender 800 / descender -200 at 1024 ppem (1 unit == 1 pixel) -> OS/2 winAscent 800, winDescent 200 */
        t_note("tmAscent=%ld tmDescent=%ld tmHeight=%ld", tm.tmAscent, tm.tmDescent, tm.tmHeight);
        EXPECT_EQ(tm.tmAscent, 800);
        EXPECT_EQ(tm.tmDescent, 200);
        CHECK(U(GetCharWidth32W)(dc, 'A', 'A', &w));
        EXPECT_EQ(w, 600);
        CHECK(U(GetCharWidth32W)(dc, ' ', ' ', &w));
        EXPECT_EQ(w, 600);
    } else t_note("generated font was not selected (%ls)", face);
    U(SelectObject)(dc, old); U(DeleteObject)(f); U(DeleteDC)(dc);
    U(RemoveFontResourceExW)(g.wpath, FR_PRIVATE, NULL);
}

static int child_probe(int argc, char **argv, int expect_visible) {
    if (argc < 2) return 90;
    WCHAR fam[64]; fe_wide(argv[1], fam, 64);
    if (!U(GetDC) || !U(EnumFontFamiliesExW)) return 91;
    HDC dc = U(GetDC)(NULL);
    int n = fe_count_family(dc, fam, DEFAULT_CHARSET);
    U(ReleaseDC)(NULL, dc);
    printf("child sees %d faces\n", n);
    return expect_visible ? (n > 0 ? 0 : 1) : (n == 0 ? 0 : 1);
}
CHILD(fe_probe_hidden) { return child_probe(argc, argv, 0); }
CHILD(fe_probe_visible) { return child_probe(argc, argv, 1); }

TEST(gdi_font_enum_resources_private_font_invisible_to_other_process) {
    GEN(g);
    if (!U(AddFontResourceExW)(g.wpath, FR_PRIVATE, NULL)) { t_skip("generated font not accepted"); return; }
    EXPECT_EQ(visible(&g), 1);
    struct k_child_result r = k_child_run("fe_probe_hidden", g.family, 30000);
    EXPECT_EQ(r.exit_code, 0);
    U(RemoveFontResourceExW)(g.wpath, FR_PRIVATE, NULL);
}

TEST(gdi_font_enum_resources_not_enum_private_invisible_to_other_process) {
    GEN(g);
    if (!U(AddFontResourceExW)(g.wpath, FR_PRIVATE | FR_NOT_ENUM, NULL)) { t_skip("generated font not accepted"); return; }
    struct k_child_result r = k_child_run("fe_probe_hidden", g.family, 30000);
    EXPECT_EQ(r.exit_code, 0);
    U(RemoveFontResourceExW)(g.wpath, FR_PRIVATE | FR_NOT_ENUM, NULL);
}

TEST(gdi_font_enum_resources_public_font_visible_to_other_process) {
    GEN(g);
    int a = U(AddFontResourceExW)(g.wpath, 0, NULL);
    if (!a) { t_skip("generated font not accepted"); return; }
    EXPECT_EQ(visible(&g), 1);
    struct k_child_result r = k_child_run("fe_probe_visible", g.family, 30000);
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_EQ(U(RemoveFontResourceExW)(g.wpath, 0, NULL), 1);
    EXPECT_EQ(visible(&g), 0);
}

TEST(gdi_font_enum_resources_public_font_gone_for_other_process_after_remove) {
    GEN(g);
    if (!U(AddFontResourceExW)(g.wpath, 0, NULL)) { t_skip("generated font not accepted"); return; }
    U(RemoveFontResourceExW)(g.wpath, 0, NULL);
    struct k_child_result r = k_child_run("fe_probe_hidden", g.family, 30000);
    EXPECT_EQ(r.exit_code, 0);
}

TEST(gdi_font_enum_resources_cookie_stub_and_value) {
    if (!UN(NtGdiGetPublicFontTableChangeCookie) || t_is_wine()) { t_skip("cookie syscall unavailable (wine aborts on it)"); return; }
    FARPROC p = t_proc_in("win32u.dll", "NtGdiGetPublicFontTableChangeCookie");
    CHECK(k_stub_valid(p));
    if (!t_is_wine()) EXPECT_EQ(k_stub_number(p), 4818);
    ULONG a = UN(NtGdiGetPublicFontTableChangeCookie)();
    ULONG b = UN(NtGdiGetPublicFontTableChangeCookie)();
    EXPECT_EQ(a, b); /* a plain read of gpPFTPublic+0x20 under the PFT semaphore */
}

TEST(gdi_font_enum_resources_cookie_unchanged_by_private_add_remove) {
    GEN(g);
    if (!UN(NtGdiGetPublicFontTableChangeCookie) || t_is_wine()) { t_skip("cookie syscall unavailable (wine aborts on it)"); return; }
    ULONG c0 = UN(NtGdiGetPublicFontTableChangeCookie)();
    if (!U(AddFontResourceExW)(g.wpath, FR_PRIVATE, NULL)) { t_skip("generated font not accepted"); return; }
    ULONG c1 = UN(NtGdiGetPublicFontTableChangeCookie)();
    U(RemoveFontResourceExW)(g.wpath, FR_PRIVATE, NULL);
    ULONG c2 = UN(NtGdiGetPublicFontTableChangeCookie)();
    if (c0 != c1 || c1 != c2) t_note("private add/remove moved the public cookie: %lu -> %lu -> %lu", c0, c1, c2);
    /* Another process may legitimately change the public table meanwhile, so only a note when it moves. */
    EXPECT_EQ(c0, c1);
    EXPECT_EQ(c1, c2);
}

TEST(gdi_font_enum_resources_cookie_changes_on_public_add_and_remove) {
    GEN(g);
    if (!UN(NtGdiGetPublicFontTableChangeCookie) || t_is_wine()) { t_skip("cookie syscall unavailable (wine aborts on it)"); return; }
    ULONG c0 = UN(NtGdiGetPublicFontTableChangeCookie)();
    if (!U(AddFontResourceExW)(g.wpath, 0, NULL)) { t_skip("generated font not accepted"); return; }
    ULONG c1 = UN(NtGdiGetPublicFontTableChangeCookie)();
    U(RemoveFontResourceExW)(g.wpath, 0, NULL);
    ULONG c2 = UN(NtGdiGetPublicFontTableChangeCookie)();
    EXPECT_NE(c0, c1);
    EXPECT_NE(c1, c2);
}

struct fc { HWND w; int count; };
static struct fc g_fc;
static LRESULT CALLBACK fc_proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_FONTCHANGE) g_fc.count++;
    return DefWindowProcW(h, m, wp, lp);
}
TEST(gdi_font_enum_resources_wm_fontchange_after_public_add_note) {
    GEN(g);
    HWND w = k_make_window("fe_fontchange");
    if (!w) { t_skip("no window"); return; }
    WNDPROC old = (WNDPROC)SetWindowLongPtrW(w, GWLP_WNDPROC, (LONG_PTR)fc_proc);
    g_fc.count = 0;
    if (!U(AddFontResourceExW)(g.wpath, 0, NULL)) { SetWindowLongPtrW(w, GWLP_WNDPROC, (LONG_PTR)old); U(DestroyWindow)(w); t_skip("generated font not accepted"); return; }
    k_pump(500);
    int after_add = g_fc.count;
    U(RemoveFontResourceExW)(g.wpath, 0, NULL);
    k_pump(500);
    t_note("WM_FONTCHANGE received after add: %d, after remove: %d (the API itself does not broadcast; observation only)", after_add, g_fc.count - after_add);
    SetWindowLongPtrW(w, GWLP_WNDPROC, (LONG_PTR)old);
    U(DestroyWindow)(w);
}

TEST(gdi_font_enum_resources_add_a_and_w_equivalent) {
    GEN(g);
    if (!U(AddFontResourceExA) || !U(RemoveFontResourceExA)) { t_skip("missing A exports"); return; }
    int r = U(AddFontResourceExA)(g.path, FR_PRIVATE, NULL);
    if (!r) { t_skip("generated font not accepted"); return; }
    EXPECT_EQ(visible(&g), 1);
    EXPECT_EQ(U(RemoveFontResourceExW)(g.wpath, FR_PRIVATE, NULL), 1); /* same file, same reference */
    EXPECT_EQ(visible(&g), 0);
}

TEST(gdi_font_enum_resources_add_font_resource_w_is_public) {
    GEN(g);
    if (!U(AddFontResourceW) || !U(RemoveFontResourceW)) { t_skip("missing exports"); return; }
    int r = U(AddFontResourceW)(g.wpath);
    if (!r) { t_skip("generated font not accepted"); return; }
    EXPECT_EQ(r, 1);
    struct k_child_result c = k_child_run("fe_probe_visible", g.family, 30000);
    EXPECT_EQ(c.exit_code, 0);
    EXPECT_EQ(U(RemoveFontResourceW)(g.wpath), 1);
}

TEST(gdi_font_enum_resources_add_relative_path_resolves) {
    GEN(g);
    char old[MAX_PATH]; GetCurrentDirectoryA(sizeof old, old);
    char leaf[80]; snprintf(leaf, sizeof leaf, "%s.ttf", g.family);
    WCHAR wl[80]; fe_wide(leaf, wl, 80);
    SetCurrentDirectoryA(t_tmpdir());
    int r = U(AddFontResourceExW)(wl, FR_PRIVATE, NULL);
    BOOL rm = r ? U(RemoveFontResourceExW)(wl, FR_PRIVATE, NULL) : FALSE;
    SetCurrentDirectoryA(old);
    if (!r) { t_note("relative path add failed (%lu)", GetLastError()); return; }
    EXPECT_EQ(rm, 1);
}

TEST(gdi_font_enum_resources_add_stock_font_copy) {
    if (!need()) { t_skip("missing exports"); return; }
    char src[MAX_PATH], dst[MAX_PATH]; fe_windir_font("cour.ttf", src, sizeof src);
    if (!fe_file_exists(src)) { t_skip("cour.ttf absent"); return; }
    t_tmpfile(dst, sizeof dst, "cour_copy.ttf");
    CHECK(CopyFileA(src, dst, FALSE));
    WCHAR w[MAX_PATH]; fe_wide(dst, w, MAX_PATH);
    int r = U(AddFontResourceExW)(w, FR_PRIVATE, NULL);
    EXPECT_EQ(r, 1);
    if (r) EXPECT_EQ(U(RemoveFontResourceExW)(w, FR_PRIVATE, NULL), 1);
}

TEST(gdi_font_enum_resources_add_stock_collection_counts_faces) {
    if (!need()) { t_skip("missing exports"); return; }
    char src[MAX_PATH], dst[MAX_PATH]; fe_windir_font("cambria.ttc", src, sizeof src);
    if (!fe_file_exists(src)) { t_skip("cambria.ttc absent"); return; }
    t_tmpfile(dst, sizeof dst, "cambria_copy.ttc");
    CHECK(CopyFileA(src, dst, FALSE));
    WCHAR w[MAX_PATH]; fe_wide(dst, w, MAX_PATH);
    int r = U(AddFontResourceExW)(w, FR_PRIVATE, NULL);
    EXPECT_EQ(r, 2); /* Cambria and Cambria Math */
    if (r) U(RemoveFontResourceExW)(w, FR_PRIVATE, NULL);
}

struct snap { struct gen *g; int added; };
static void snap_hook(struct fe_set *s, const struct fe_item *it) {
    struct snap *sn = (struct snap *)s->hookarg;
    if (!sn->added) { sn->added = U(AddFontResourceExW)(sn->g->wpath, FR_PRIVATE, NULL); }
}
TEST(gdi_font_enum_resources_enumeration_is_snapshot) {
    GEN(g);
    HDC dc = U(GetDC)(NULL);
    int before = fe_count_family(dc, NULL, DEFAULT_CHARSET);
    struct snap sn = { &g, 0 };
    struct fe_set s; fe_set_init(&s); s.hook = snap_hook; s.hookarg = &sn;
    fe_enum(dc, NULL, DEFAULT_CHARSET, &s);
    if (!sn.added) { fe_set_free(&s); U(ReleaseDC)(NULL, dc); t_skip("generated font not accepted"); return; }
    /* The font appeared during the walk, but the record buffer was taken before the first callback. */
    EXPECT_EQ(s.n, before);
    CHECK(fe_find(&s, g.wfam) == 0);
    int after = fe_count_family(dc, NULL, DEFAULT_CHARSET);
    EXPECT_EQ(after, before + 1);
    U(RemoveFontResourceExW)(g.wpath, FR_PRIVATE, NULL);
    fe_set_free(&s); U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_enum_count_grows_by_one_for_private_font) {
    GEN(g);
    HDC dc = U(GetDC)(NULL);
    int before = fe_count_family(dc, NULL, DEFAULT_CHARSET);
    if (!U(AddFontResourceExW)(g.wpath, FR_PRIVATE, NULL)) { U(ReleaseDC)(NULL, dc); t_skip("generated font not accepted"); return; }
    int mid = fe_count_family(dc, NULL, DEFAULT_CHARSET);
    U(RemoveFontResourceExW)(g.wpath, FR_PRIVATE, NULL);
    int end = fe_count_family(dc, NULL, DEFAULT_CHARSET);
    EXPECT_EQ(mid, before + 1);
    EXPECT_EQ(end, before);
    U(ReleaseDC)(NULL, dc);
}

TEST(gdi_font_enum_resources_enum_charset_filter_for_generated_font) {
    GEN(g);
    if (!U(AddFontResourceExW)(g.wpath, FR_PRIVATE, NULL)) { t_skip("generated font not accepted"); return; }
    HDC dc = U(GetDC)(NULL);
    struct fe_set s; fe_set_init(&s);
    fe_enum(dc, g.wfam, DEFAULT_CHARSET, &s);
    if (s.n) {
        BYTE cs = s.it[0].charset;
        t_note("generated font enumerated with charset %u", cs);
        EXPECT_EQ(fe_count_family(dc, g.wfam, cs), s.n);
        EXPECT_EQ(fe_count_family(dc, g.wfam, SYMBOL_CHARSET), cs == SYMBOL_CHARSET ? s.n : 0);
    }
    fe_set_free(&s); U(ReleaseDC)(NULL, dc);
    U(RemoveFontResourceExW)(g.wpath, FR_PRIVATE, NULL);
}

/* ---- direct syscall validation --------------------------------------------------------------------------------- */
#define NEEDK() do { if (!UN(NtGdiAddFontResourceW) || !UN(NtGdiRemoveFontResourceW) || !U(GetDC)) { t_skip("missing exports"); return; } } while (0)

TEST(gdi_font_enum_resources_add_syscall_valid_path_roundtrip) {
    GEN(g); NEEDK();
    int r = UN(NtGdiAddFontResourceW)(g.ntpath, g.ntcwc, 1, FR_PRIVATE, 0, NULL);
    if (!r) { t_skip("generated font not accepted"); return; }
    EXPECT_EQ(r, 1);
    EXPECT_EQ(visible(&g), 1);
    EXPECT_EQ(UN(NtGdiRemoveFontResourceW)(g.ntpath, g.ntcwc, 1, FR_PRIVATE, 0, NULL), 1);
    EXPECT_EQ(visible(&g), 0);
}

TEST(gdi_font_enum_resources_add_syscall_matches_user_path_reference) {
    GEN(g); NEEDK();
    int r = UN(NtGdiAddFontResourceW)(g.ntpath, g.ntcwc, 1, FR_PRIVATE, 0, NULL);
    if (!r) { t_skip("generated font not accepted"); return; }
    /* the same private entry is released through the user-mode wrapper, which builds the same NT path */
    EXPECT_EQ(U(RemoveFontResourceExW)(g.wpath, FR_PRIVATE, NULL), 1);
    EXPECT_EQ(visible(&g), 0);
}

TEST(gdi_font_enum_resources_add_syscall_short_length_rejected) {
    GEN(g); NEEDK();
    ULONG lens[] = { 0, 1 };
    for (int i = 0; i < 2; i++) {
        EXPECT_EQ(UN(NtGdiAddFontResourceW)(g.ntpath, lens[i], 1, FR_PRIVATE, 0, NULL), 0); /* cwc < 2: bCheckAndCapThePath is never reached */
        EXPECT_EQ(UN(NtGdiRemoveFontResourceW)(g.ntpath, lens[i], 1, FR_PRIVATE, 0, NULL), 0);
    }
    EXPECT_EQ(visible(&g), 0);
}

TEST(gdi_font_enum_resources_add_syscall_unterminated_path_rejected) {
    GEN(g); NEEDK();
    /* bCheckAndCapThePath requires path[cwc-1] == 0: cut the count one short so the last counted character is non-NUL. */
    EXPECT_EQ(UN(NtGdiAddFontResourceW)(g.ntpath, g.ntcwc - 1, 1, FR_PRIVATE, 0, NULL), 0);
    EXPECT_EQ(UN(NtGdiRemoveFontResourceW)(g.ntpath, g.ntcwc - 1, 1, FR_PRIVATE, 0, NULL), 0);
    EXPECT_EQ(visible(&g), 0);
}

TEST(gdi_font_enum_resources_add_syscall_file_count_must_match) {
    GEN(g); NEEDK();
    /* The file count must equal the number of '|' separated names (here 1). */
    ULONG counts[] = { 0, 2, 3, 100 };
    for (unsigned i = 0; i < 4; i++) {
        int r = UN(NtGdiAddFontResourceW)(g.ntpath, g.ntcwc, counts[i], FR_PRIVATE, 0, NULL);
        CHECKF(r == 0, "cFiles=%lu accepted for a single name", counts[i]);
        if (r) UN(NtGdiRemoveFontResourceW)(g.ntpath, g.ntcwc, counts[i], FR_PRIVATE, 0, NULL);
        EXPECT_EQ(UN(NtGdiRemoveFontResourceW)(g.ntpath, g.ntcwc, counts[i], FR_PRIVATE, 0, NULL), 0);
    }
    EXPECT_EQ(visible(&g), 0);
}

TEST(gdi_font_enum_resources_add_syscall_pipe_count_two) {
    GEN(g); NEEDK();
    /* "a|b" with cFiles == 2 passes the path gate (the bar becomes a NUL); neither file exists so the add itself fails. */
    WCHAR p[MAX_PATH * 2 + 8]; lstrcpyW(p, g.ntpath); lstrcatW(p, L"|"); lstrcatW(p, g.ntpath);
    ULONG cwc = (ULONG)lstrlenW(p) + 1;
    int r = UN(NtGdiAddFontResourceW)(p, cwc, 2, FR_PRIVATE, 0, NULL);
    t_note("two-file add of a file pair that is not a font pair: %d", r);
    if (r) UN(NtGdiRemoveFontResourceW)(p, cwc, 2, FR_PRIVATE, 0, NULL);
    EXPECT_EQ(UN(NtGdiAddFontResourceW)(p, cwc, 1, FR_PRIVATE, 0, NULL), 0); /* cFiles 1 but 2 names */
    U(RemoveFontResourceExW)(g.wpath, FR_PRIVATE, NULL);
    EXPECT_EQ(visible(&g), 0);
}

TEST(gdi_font_enum_resources_add_syscall_csrss_flag_refused_for_normal_process) {
    GEN(g); NEEDK();
    /* flags == 0x80000000 is only accepted when the caller is csrss (NtGdiAddFontResourceW checks PsGetCurrentProcess() == gpepCSRSS). */
    EXPECT_EQ(UN(NtGdiAddFontResourceW)(g.ntpath, g.ntcwc, 1, 0x80000000u, 0, NULL), 0);
    EXPECT_EQ(visible(&g), 0);
}

TEST(gdi_font_enum_resources_add_syscall_nonexistent_nt_path) {
    NEEDK();
    WCHAR p[MAX_PATH]; char a[MAX_PATH]; ULONG cwc;
    t_tmpfile(a, sizeof a, "nt_missing.ttf"); fe_nt_path(a, p, MAX_PATH, &cwc);
    EXPECT_EQ(UN(NtGdiAddFontResourceW)(p, cwc, 1, FR_PRIVATE, 0, NULL), 0);
    EXPECT_EQ(UN(NtGdiRemoveFontResourceW)(p, cwc, 1, FR_PRIVATE, 0, NULL), 0);
}

TEST(gdi_font_enum_resources_add_syscall_dos_path_not_accepted) {
    GEN(g); NEEDK();
    /* The kernel opens the name as given: a DOS path without the \??\ prefix is not an object path. */
    WCHAR w[MAX_PATH]; lstrcpyW(w, g.wpath);
    int r = UN(NtGdiAddFontResourceW)(w, (ULONG)lstrlenW(w) + 1, 1, FR_PRIVATE, 0, NULL);
    t_note("NtGdiAddFontResourceW with a DOS path: %d", r);
    if (r) UN(NtGdiRemoveFontResourceW)(w, (ULONG)lstrlenW(w) + 1, 1, FR_PRIVATE, 0, NULL);
}

TEST(gdi_font_enum_resources_add_syscall_refcount_via_stub) {
    GEN(g); NEEDK();
    if (!UN(NtGdiAddFontResourceW)(g.ntpath, g.ntcwc, 1, FR_PRIVATE, 0, NULL)) { t_skip("generated font not accepted"); return; }
    EXPECT_EQ(UN(NtGdiAddFontResourceW)(g.ntpath, g.ntcwc, 1, FR_PRIVATE, 0, NULL), 1);
    EXPECT_EQ(UN(NtGdiRemoveFontResourceW)(g.ntpath, g.ntcwc, 1, FR_PRIVATE, 0, NULL), 1);
    EXPECT_EQ(visible(&g), 1);
    EXPECT_EQ(UN(NtGdiRemoveFontResourceW)(g.ntpath, g.ntcwc, 1, FR_PRIVATE, 0, NULL), 1);
    EXPECT_EQ(visible(&g), 0);
    EXPECT_EQ(UN(NtGdiRemoveFontResourceW)(g.ntpath, g.ntcwc, 1, FR_PRIVATE, 0, NULL), 0);
}

TEST(gdi_font_enum_resources_cleanup_leaves_nothing_behind) {
    if (!need()) { t_skip("missing exports"); return; }
    /* Every earlier test removed its fonts: no Win32kt family is left enumerable in this process. */
    HDC dc = U(GetDC)(NULL);
    struct fe_set s; fe_set_init(&s);
    fe_enum(dc, NULL, DEFAULT_CHARSET, &s);
    char pfx[40]; snprintf(pfx, sizeof pfx, "Win32ktT%lux", GetCurrentProcessId());
    WCHAR wp[40]; fe_wide(pfx, wp, 40);
    int left = 0;
    for (int i = 0; i < s.n; i++) if (!wcsncmp(s.it[i].face, wp, lstrlenW(wp))) left++;
    EXPECT_EQ(left, 0);
    fe_set_free(&s); U(ReleaseDC)(NULL, dc);
}
