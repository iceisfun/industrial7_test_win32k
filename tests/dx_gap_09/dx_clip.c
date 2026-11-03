/* dx_gap_09 (run with /destructive only): clipboard-content and viewer-chain cases of the gap_09 set.
 * These change the shared window-station clipboard (EmptyClipboard/SetClipboardData) and the viewer chain,
 * so they live outside the default run.  Every test removes the viewers/listeners it added and leaves the
 * clipboard empty.
 *
 *  - CloseClipboard after a change posts WM_CLIPBOARDUPDATE to each registered listener (clipboard change notification).
 *  - NtUserGetUpdatedClipboardFormats lists the formats stored by SetClipboardData.
 *  - NtUserChangeClipboardChain(hwndRemove, hwndNext): removing the chain head just relinks; removing a
 *    later link sends WM_CHANGECBCHAIN (wParam = leaving window, lParam = its successor) to the head. */
#include "t.h"
#include "k.h"

UDECL(BOOL, NtUserGetUpdatedClipboardFormats, (UINT *, UINT, UINT *));
UDECL(BOOL, NtUserAddClipboardFormatListener, (HWND));
UDECL(BOOL, NtUserRemoveClipboardFormatListener, (HWND));
UDECL(BOOL, NtUserChangeClipboardChain, (HWND, HWND));

#define N(a) ((int)(sizeof(a) / sizeof((a)[0])))

struct rec { int update, draw, chain; WPARAM chain_w; LPARAM chain_l; LRESULT chain_ret; };
static struct rec g_rec[4];

static LRESULT CALLBACK dx_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    int i = (int)(LONG_PTR)U(GetWindowLongPtrA)(h, GWLP_USERDATA);
    if (i >= 0 && i < 4) {
        if (m == WM_CLIPBOARDUPDATE) { g_rec[i].update++; return 0; }
        if (m == WM_DRAWCLIPBOARD) { g_rec[i].draw++; return 0; }
        if (m == WM_CHANGECBCHAIN) { g_rec[i].chain++; g_rec[i].chain_w = w; g_rec[i].chain_l = l; return g_rec[i].chain_ret; }
    }
    return U(DefWindowProcA)(h, m, w, l);
}
static HWND dx_win(int idx) {
    static char cls[48];
    if (!U(RegisterClassA) || !U(CreateWindowExA) || !U(DefWindowProcA) || !U(SetWindowLongPtrA)) return NULL;
    if (!cls[0]) {
        WNDCLASSA wc; memset(&wc, 0, sizeof wc);
        wsprintfA(cls, "win32kt_dxgap9_%lu", GetCurrentProcessId());
        wc.lpfnWndProc = dx_proc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = cls;
        if (!U(RegisterClassA)(&wc)) { cls[0] = 0; return NULL; }
    }
    HWND h = U(CreateWindowExA)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls, "dx", WS_POPUP, 100, 100, 20, 20, NULL, NULL, GetModuleHandleA(NULL), NULL);
    if (h) U(SetWindowLongPtrA)(h, GWLP_USERDATA, idx);
    memset(&g_rec[idx], 0, sizeof g_rec[idx]);
    return h;
}
static int set_text(HWND owner, const char *txt) {
    if (!U(OpenClipboard)(owner)) return 0;
    U(EmptyClipboard)();
    size_t n = strlen(txt) + 1;
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, n);
    if (g) { memcpy(GlobalLock(g), txt, n); GlobalUnlock(g); if (!U(SetClipboardData)(CF_TEXT, g)) GlobalFree(g); }
    return U(CloseClipboard)();
}
static void clear_cb(HWND owner) { if (U(OpenClipboard)(owner)) { U(EmptyClipboard)(); U(CloseClipboard)(); } }
#define DXREQ() do { if (!U(OpenClipboard) || !U(EmptyClipboard) || !U(SetClipboardData) || !U(SetClipboardViewer) || !UN(NtUserChangeClipboardChain)) { t_skip("missing export"); return; } } while (0)

TEST(gap9_dx_clipl_update_message_per_change) {
    DXREQ(); REQUIRE(UN(NtUserAddClipboardFormatListener)); REQUIRE(UN(NtUserRemoveClipboardFormatListener));
    HWND h = dx_win(0);
    if (!h) { t_skip("no window"); return; }
    CHECK(UN(NtUserAddClipboardFormatListener)(h));
    CHECK(set_text(h, "gap9 one"));
    k_pump(300);
    EXPECT_EQ(g_rec[0].update, 1);
    CHECK(set_text(h, "gap9 two"));
    k_pump(300);
    EXPECT_EQ(g_rec[0].update, 2);
    CHECK(UN(NtUserRemoveClipboardFormatListener)(h));
    CHECK(set_text(h, "gap9 three"));
    k_pump(300);
    EXPECT_EQ(g_rec[0].update, 2);                          /* unregistered: nothing more is posted */
    clear_cb(h);
    U(DestroyWindow)(h);
}

TEST(gap9_dx_clipl_destroyed_listener_not_notified) {
    DXREQ(); REQUIRE(UN(NtUserAddClipboardFormatListener));
    HWND a = dx_win(0), b = dx_win(1);
    if (!a || !b) { t_skip("no window"); return; }
    CHECK(UN(NtUserAddClipboardFormatListener)(a));
    CHECK(UN(NtUserAddClipboardFormatListener)(b));
    U(DestroyWindow)(a);
    CHECK(set_text(b, "gap9 destroyed"));
    k_pump(300);
    EXPECT_EQ(g_rec[1].update, 1);
    UN(NtUserRemoveClipboardFormatListener)(b);
    clear_cb(b);
    U(DestroyWindow)(b);
}

TEST(gap9_dx_clipfmt_lists_stored_formats) {
    DXREQ(); REQUIRE(UN(NtUserGetUpdatedClipboardFormats));
    HWND h = dx_win(0);
    if (!h) { t_skip("no window"); return; }
    CHECK(set_text(h, "gap9 formats"));
    UINT f[64] = {0}, n = 0;
    CHECK(UN(NtUserGetUpdatedClipboardFormats)(f, 64, &n));
    int has_text = 0;
    for (UINT i = 0; i < n; i++) if (f[i] == CF_TEXT) has_text = 1;
    CHECK(has_text);
    CHECK(n >= 1 && n <= 64);
    t_note("%u formats after SetClipboardData(CF_TEXT)", n);
    if (U(CountClipboardFormats)) t_note("CountClipboardFormats = %d", U(CountClipboardFormats)());
    clear_cb(h);
    UINT m = 99;
    BOOL r = UN(NtUserGetUpdatedClipboardFormats)(f, 64, &m);
    CHECK(r);
    EXPECT_EQ(m, 0);                                         /* EmptyClipboard leaves no formats */
    U(DestroyWindow)(h);
}

TEST(gap9_dx_chain_head_removal_relinks) {
    DXREQ();
    HWND a = dx_win(0), b = dx_win(1);
    if (!a || !b) { t_skip("no window"); return; }
    HWND prev_a = U(SetClipboardViewer)(a);
    HWND prev_b = U(SetClipboardViewer)(b);                  /* chain: b -> a -> prev_a */
    CHECK(prev_b == a);
    int a_chain_before = g_rec[0].chain;
    CHECK(UN(NtUserChangeClipboardChain)(b, a));             /* b is the head: relink only, returns 1 */
    EXPECT_EQ(g_rec[0].chain, a_chain_before);               /* the new head receives no WM_CHANGECBCHAIN */
    UN(NtUserChangeClipboardChain)(a, prev_a);
    U(DestroyWindow)(a); U(DestroyWindow)(b);
}

TEST(gap9_dx_chain_inner_removal_notifies_head) {
    DXREQ();
    HWND a = dx_win(0), b = dx_win(1);
    if (!a || !b) { t_skip("no window"); return; }
    HWND prev_a = U(SetClipboardViewer)(a);
    HWND prev_b = U(SetClipboardViewer)(b);                  /* b -> a -> prev_a */
    CHECK(prev_b == a);
    g_rec[1].chain_ret = 0;
    BOOL r = UN(NtUserChangeClipboardChain)(a, prev_a);      /* a is behind the head: head gets the message */
    EXPECT_EQ(g_rec[1].chain, 1);
    EXPECT_EQ(g_rec[1].chain_w, (WPARAM)a);
    EXPECT_EQ(g_rec[1].chain_l, (LPARAM)prev_a);
    CHECK(!r);                                               /* the call returns the head's result (0 here) */
    UN(NtUserChangeClipboardChain)(b, prev_a);
    U(DestroyWindow)(a); U(DestroyWindow)(b);
}

TEST(gap9_dx_chain_invalid_windows) {
    DXREQ();
    HWND a = dx_win(0);
    if (!a) { t_skip("no window"); return; }
    SetLastError(0);
    CHECK(!UN(NtUserChangeClipboardChain)((HWND)(ULONG_PTR)0x5555, NULL));
    t_note("ChangeClipboardChain(bad hwnd, NULL) error %lu", GetLastError());
    SetLastError(0);
    CHECK(!UN(NtUserChangeClipboardChain)(a, (HWND)(ULONG_PTR)0x5555));      /* bad hwndNext fails in ValidateHwnd */
    EXPECT_EQ(GetLastError(), ERROR_INVALID_WINDOW_HANDLE);
    U(DestroyWindow)(a);
}
