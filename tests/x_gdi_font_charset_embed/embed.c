/* NtGdiGetEmbedFonts: the number of private (embedded) fonts the calling process holds. A fresh process holds none;
 * every AddFontMemResourceEx adds one and RemoveFontMemResourceEx removes it; FR_PRIVATE file fonts count once per file
 * with reference counting on repeated adds; a rejected font adds nothing; the arguments are ignored. Process-wide state is
 * tested in child processes so the count starts from zero, and every test removes what it adds. Only system fonts that
 * ship with Windows are read; nothing is installed system-wide. */
#include "xf.h"

#define CC(c) do { if (!(c)) { fails++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CCF(c, ...) do { if (!(c)) { fails++; printf("  FAIL %s:%d: %s (", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf(")\n"); } } while (0)

static WCHAR g_arial[MAX_PATH], g_cour[MAX_PATH];
static int font_paths(void) {
    WCHAR w[MAX_PATH];
    if (!GetWindowsDirectoryW(w, MAX_PATH)) return 0;
    lstrcpyW(g_arial, w); lstrcatW(g_arial, L"\\Fonts\\arial.ttf");
    lstrcpyW(g_cour, w); lstrcatW(g_cour, L"\\Fonts\\cour.ttf");
    return GetFileAttributesW(g_arial) != INVALID_FILE_ATTRIBUTES && GetFileAttributesW(g_cour) != INVALID_FILE_ATTRIBUTES;
}
static void *slurp(const WCHAR *p, DWORD *sz) {
    HANDLE h = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    *sz = GetFileSize(h, NULL); void *b = HeapAlloc(GetProcessHeap(), 0, *sz); DWORD r = 0;
    if (!b || !ReadFile(h, b, *sz, &r, NULL) || r != *sz) { CloseHandle(h); return NULL; }
    CloseHandle(h); return b;
}
static ULONG emb(void) { return XF_NT(NtGdiGetEmbedFonts)(); }

CHILD(x_embed_fresh) {
    int fails = 0;
    U(GetDesktopWindow)();                           /* a process must be connected to user32 before any win32k call */
    CC(emb() == 0);
    HDC dc = U(CreateCompatibleDC)(NULL); HFONT f = U(CreateFontW)(-16, 0, 0, 0, 400, 0, 0, 0, 0, 0, 0, 0, 0, L"Arial");
    HGDIOBJ o = U(SelectObject)(dc, f); (void)o;
    CC(emb() == 0);                                  /* creating and selecting fonts embeds nothing */
    U(DeleteDC)(dc); U(DeleteObject)(f);
    CC(emb() == 0);
    return fails;
}
CHILD(x_embed_mem_counts) {
    int fails = 0; DWORD sz = 0, n = 0;
    if (!U(GetDesktopWindow) || !U(AddFontMemResourceEx) || !font_paths()) { printf("  SKIP no font data\n"); return 0; }
    U(GetDesktopWindow)();
    void *b = slurp(g_arial, &sz); CC(b != NULL); if (!b) return fails;
    HANDLE h1 = U(AddFontMemResourceEx)(b, sz, NULL, &n); CC(h1 != NULL); CC(n == 1); CC(emb() == 1);
    HANDLE h2 = U(AddFontMemResourceEx)(b, sz, NULL, &n); CC(h2 != NULL); CC(emb() == 2);
    HANDLE h3 = U(AddFontMemResourceEx)(b, sz, NULL, &n); CC(h3 != NULL); CC(emb() == 3);
    CC(U(RemoveFontMemResourceEx)(h2)); CC(emb() == 2);
    CC(U(RemoveFontMemResourceEx)(h1)); CC(emb() == 1);
    CC(U(RemoveFontMemResourceEx)(h3)); CC(emb() == 0);
    return fails;
}
CHILD(x_embed_mem_double_remove) {
    int fails = 0; DWORD sz = 0, n = 0;
    if (!U(GetDesktopWindow) || !U(AddFontMemResourceEx) || !font_paths()) { printf("  SKIP no font data\n"); return 0; }
    U(GetDesktopWindow)();
    void *b = slurp(g_arial, &sz); CC(b != NULL); if (!b) return fails;
    HANDLE h = U(AddFontMemResourceEx)(b, sz, NULL, &n); CC(h != NULL);
    CC(U(RemoveFontMemResourceEx)(h)); CC(emb() == 0);
    CC(!U(RemoveFontMemResourceEx)(h)); CC(emb() == 0);
    return fails;
}
CHILD(x_embed_mem_junk) {
    int fails = 0; DWORD n = 77;
    if (!U(GetDesktopWindow) || !U(AddFontMemResourceEx)) { printf("  SKIP missing exports\n"); return 0; }
    U(GetDesktopWindow)();
    unsigned char junk[256]; memset(junk, 1, sizeof junk);
    HANDLE h = U(AddFontMemResourceEx)(junk, sizeof junk, NULL, &n);
    CC(h == NULL); CC(emb() == 0);
    h = U(AddFontMemResourceEx)(junk, 0, NULL, &n);
    CC(h == NULL); CC(emb() == 0);
    h = U(AddFontMemResourceEx)(NULL, 100, NULL, &n);
    CC(h == NULL); CC(emb() == 0);
    return fails;
}
CHILD(x_embed_private_file_counts) {
    int fails = 0;
    if (!U(GetDesktopWindow) || !U(AddFontResourceExW) || !font_paths()) { printf("  SKIP no font files\n"); return 0; }
    U(GetDesktopWindow)();
    CC(U(AddFontResourceExW)(g_arial, FR_PRIVATE, 0) == 1); CC(emb() == 1);
    CC(U(AddFontResourceExW)(g_arial, FR_PRIVATE, 0) == 1); CC(emb() == 1);       /* same file: reference counted, still one */
    CC(U(RemoveFontResourceExW)(g_arial, FR_PRIVATE, 0)); CC(emb() == 1);
    CC(U(RemoveFontResourceExW)(g_arial, FR_PRIVATE, 0)); CC(emb() == 0);
    CC(!U(RemoveFontResourceExW)(g_arial, FR_PRIVATE, 0)); CC(emb() == 0);
    return fails;
}
CHILD(x_embed_private_two_files) {
    int fails = 0;
    if (!U(GetDesktopWindow) || !U(AddFontResourceExW) || !font_paths()) { printf("  SKIP no font files\n"); return 0; }
    U(GetDesktopWindow)();
    CC(U(AddFontResourceExW)(g_cour, FR_PRIVATE, 0) == 1); CC(emb() == 1);
    CC(U(AddFontResourceExW)(g_arial, FR_PRIVATE, 0) == 1); CC(emb() == 2);
    CC(U(RemoveFontResourceExW)(g_cour, FR_PRIVATE, 0)); CC(emb() == 1);
    CC(U(RemoveFontResourceExW)(g_arial, FR_PRIVATE, 0)); CC(emb() == 0);
    return fails;
}
CHILD(x_embed_private_not_enum) {
    int fails = 0;
    if (!U(GetDesktopWindow) || !U(AddFontResourceExW) || !font_paths()) { printf("  SKIP no font files\n"); return 0; }
    U(GetDesktopWindow)();
    CC(U(AddFontResourceExW)(g_arial, FR_PRIVATE | FR_NOT_ENUM, 0) == 1); CC(emb() == 1);
    CC(U(RemoveFontResourceExW)(g_arial, FR_PRIVATE | FR_NOT_ENUM, 0)); CC(emb() == 0);
    return fails;
}
CHILD(x_embed_mixed_sources) {
    int fails = 0; DWORD sz = 0, n = 0;
    if (!U(GetDesktopWindow) || !U(AddFontMemResourceEx) || !U(AddFontResourceExW) || !font_paths()) { printf("  SKIP no font data\n"); return 0; }
    U(GetDesktopWindow)();
    void *b = slurp(g_cour, &sz); CC(b != NULL); if (!b) return fails;
    HANDLE h = U(AddFontMemResourceEx)(b, sz, NULL, &n); CC(h != NULL); CC(emb() == 1);
    CC(U(AddFontResourceExW)(g_arial, FR_PRIVATE, 0) == 1); CC(emb() == 2);
    CC(U(RemoveFontMemResourceEx)(h)); CC(emb() == 1);
    CC(U(RemoveFontResourceExW)(g_arial, FR_PRIVATE, 0)); CC(emb() == 0);
    return fails;
}
CHILD(x_embed_args_ignored) {
    int fails = 0; DWORD sz = 0, n = 0;
    if (!U(GetDesktopWindow) || !U(AddFontMemResourceEx) || !font_paths()) { printf("  SKIP no font data\n"); return 0; }
    U(GetDesktopWindow)();
    ULONG (WINAPI *f)(ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR) = (void *)XF_NT(NtGdiGetEmbedFonts);
    CC(f(1, 2, 3, 4) == 0); CC(f(~0ull, ~0ull, ~0ull, ~0ull) == 0);
    void *b = slurp(g_arial, &sz); CC(b != NULL); if (!b) return fails;
    HANDLE h = U(AddFontMemResourceEx)(b, sz, NULL, &n);
    CC(f(9, 9, 9, 9) == 1); CC(f(0, 0, 0, 0) == 1);
    U(RemoveFontMemResourceEx)(h);
    CC(f(5, 6, 7, 8) == 0);
    return fails;
}
CHILD(x_embed_count_independent_of_dc_use) {
    int fails = 0; DWORD sz = 0, n = 0;
    if (!U(GetDesktopWindow) || !U(AddFontMemResourceEx) || !font_paths()) { printf("  SKIP no font data\n"); return 0; }
    U(GetDesktopWindow)();
    void *b = slurp(g_arial, &sz); CC(b != NULL); if (!b) return fails;
    HANDLE h = U(AddFontMemResourceEx)(b, sz, NULL, &n); CC(h != NULL);
    HDC dc = U(CreateCompatibleDC)(NULL); HFONT f = U(CreateFontW)(-16, 0, 0, 0, 400, 0, 0, 0, 0, 0, 0, 0, 0, L"Arial");
    HGDIOBJ o = U(SelectObject)(dc, f);
    CC(emb() == 1);
    U(SelectObject)(dc, o); U(DeleteObject)(f); U(DeleteDC)(dc);
    CC(emb() == 1);
    U(RemoveFontMemResourceEx)(h);
    CC(emb() == 0);
    return fails;
}
static void run_child(const char *nm) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiGetEmbedFonts));
    if (t_is_wine()) { t_skip("no win32k"); return; }
    DWORD rc = t_run_child(nm, "", 60000);
    CHECKF(rc == 0, "child %s exit %lu", nm, (unsigned long)rc);
}
#define EMT(n) XT(embed_##n) { run_child("x_embed_" #n); }
EMT(fresh) EMT(mem_counts) EMT(mem_double_remove) EMT(mem_junk) EMT(private_file_counts) EMT(private_two_files)
EMT(private_not_enum) EMT(mixed_sources) EMT(args_ignored) EMT(count_independent_of_dc_use)

XT(embed_parent_returns_to_baseline) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiGetEmbedFonts));
    if (!font_paths()) { t_skip("system font files not found"); return; }
    ULONG base = emb(); DWORD sz = 0, n = 0;
    void *b = slurp(g_arial, &sz); XF_NEED(b); XF_NEED(U(AddFontMemResourceEx));
    HANDLE h = U(AddFontMemResourceEx)(b, sz, NULL, &n);
    CHECK(h != NULL); CHECK(emb() == base + 1);
    U(RemoveFontMemResourceEx)(h);
    CHECK(emb() == base);
    HeapFree(GetProcessHeap(), 0, b);
}
XT(embed_stub_shape) {
    XF_NEED(XF_NT(NtGdiGetEmbedFonts));
    FARPROC p = t_proc_in("win32u.dll", "NtGdiGetEmbedFonts");
    if (t_is_wine()) { t_skip("no win32k"); return; }
    CHECK(k_stub_valid(p));
    CHECK(k_stub_number(p) >= 0x1000);
}
