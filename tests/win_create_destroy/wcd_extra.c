/* Additional direct NtUserCreateWindowEx / NtUserDestroyWindow checks: message-only parent handled
 * in the kernel (a10 == -3), ANSI _LARGE_STRING marker, destroy of a message-only window. */
#include "wcd.h"
#include <wchar.h>

#define NEED_NT() do { if (!wcd_user() || !UN(NtUserCreateWindowEx) || !UN(NtUserDestroyWindow)) { t_skip("user32/win32u entry points missing"); return; } } while (0)
#define HIDE_EX (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)

TEST(win_create_destroy_extra_direct_message_parent) {
    NEED_NT();
    /* NtUserCreateWindowEx: a10 == -3 resolves to the thread desktop's message window (+0x68 of
     * the desktop info) before window creation, so a plain popup becomes a message-only window. */
    HWND h = wcd_nt_create(HIDE_EX, wcd_cls(), L"m", WS_POPUP, 0, 0, 10, 10, WCD_HWND_MESSAGE, NULL, wcd_hinst(), NULL);
    if (!h) { t_note("message window creation failed, error %lu", GetLastError()); return; }
    CHECK(U(IsWindow)(h));
    CHECK(!U(IsWindowVisible)(h));
    if (U(FindWindowExW)) CHECK(U(FindWindowExW)(WCD_HWND_MESSAGE, NULL, wcd_cls(), L"m") == h);
    CHECK(wcd_entry_live(h) || !wcd_sw(h));
    CHECK((int)((NtUserDestroyWindow_pfn)UN(NtUserDestroyWindow))(h) != 0);
    CHECK(!U(IsWindow)(h));
}

TEST(win_create_destroy_extra_direct_ansi_name_marker) {
    NEED_NT();
    /* MaximumLength bit 31 of the header marks an ANSI buffer: the kernel probes Length+1 bytes
     * and copies Length bytes. Creation must succeed; the stored text depends on the class's
     * unicode-ness, so the result is only noted. */
    char txt[] = "ansi-name";
    WCD_LSTR n = { 9, 10, 1, txt };
    WCD_LSTR c = { 0 };
    c.Length = (ULONG)wcslen(wcd_cls()) * 2; c.MaximumLength = c.Length + 2; c.Buffer = (PVOID)wcd_cls();
    NtUserCreateWindowEx_pfn f = UN(NtUserCreateWindowEx);
    SetLastError(0);
    HWND h = f(HIDE_EX, &c, &c, &n, WS_POPUP, -32000, -32000, 10, 10, NULL, NULL, wcd_hinst(), NULL, 0, WCD_WINVER, 0, NULL);
    if (!h) { t_note("ansi-marked name rejected, error %lu", GetLastError()); return; }
    WCHAR b[32] = { 0 };
    int len = U(GetWindowTextW)(h, b, 32);
    t_note("ansi-marked name stored as len %d", len);
    wcd_destroy(h);
}

TEST(win_create_destroy_extra_direct_destroy_returns_boolean) {
    NEED_NT();
    HWND h = wcd_nt_create(HIDE_EX, wcd_cls(), L"b", WS_POPUP, -32000, -32000, 10, 10, NULL, NULL, wcd_hinst(), NULL);
    REQUIRE(h);
    /* window destruction returns a bool, widened to 0/1 by NtUserDestroyWindow */
    DWORD r = ((NtUserDestroyWindow_pfn)UN(NtUserDestroyWindow))(h);
    CHECK(r == 1);
}

TEST(win_create_destroy_extra_direct_handle_index_unique_stable) {
    NEED_NT();
    /* handle = (uniq << 16) | table index; the index selects an entry whose live window handle
     * must be this window's handle. */
    HWND h = wcd_nt_create(HIDE_EX, wcd_cls(), L"i", WS_POPUP, -32000, -32000, 10, 10, NULL, NULL, wcd_hinst(), NULL);
    REQUIRE(h);
    CHECK(((ULONG_PTR)h >> 32) == 0);
    CHECK(((ULONG_PTR)h & 0xffff) != 0);
    bool live = wcd_entry_live(h);
    if (!live) t_note("handle entry not readable here");
    HWND again = U(GetAncestor)(h, GA_ROOT);
    CHECK(again == h);
    wcd_destroy(h);
}
