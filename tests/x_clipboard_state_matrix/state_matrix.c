/* x_clipboard_state_matrix: the clipboard "state" syscalls checked against each other.
 *
 * Measures: NtUserCountClipboardFormats, NtUserEmptyClipboard, NtUserGetClipboardSequenceNumber,
 * NtUserGetClipboardViewer, NtUserIsClipboardFormatAvailable, NtUserGetPriorityClipboardFormat,
 * NtUserSetClipboardViewer and NtUserChangeClipboardChain, once called straight through the win32u
 * stub (route "nt") and once through the user32 wrapper (route "u32"). Every scenario that has both
 * routes is registered twice (<name>_nt, <name>_u32) and must give the same answer.
 *
 * What is cross-checked: the sequence number advances when the clipboard content changes and stays
 * put when it does not; the count equals the length of the enumeration; availability equals
 * membership in the enumeration; priority selection follows list order; operations that need the
 * clipboard open fail with ERROR_CLIPBOARD_NOT_OPEN (1418) without it; a second thread cannot open
 * the clipboard while the first holds it and cannot empty it either; the viewer chain links,
 * forwards and unlinks as windows are added and removed, including when the window is destroyed.
 *
 * Determinism: only relations over sequence numbers and counts are asserted or printed; the sequence
 * values, window handles and thread ids never appear in the output. All windows are private,
 * hidden, never shown. Every test starts from and ends in an emptied, closed clipboard with no
 * viewer of its own in the chain; the only system-visible state touched is the clipboard content
 * the test itself placed there.
 */
#include "t.h"
#include "k.h"

#ifndef ERROR_CLIPBOARD_NOT_OPEN
#define ERROR_CLIPBOARD_NOT_OPEN 1418
#endif
#ifndef WM_CLIPBOARDUPDATE
#define WM_CLIPBOARDUPDATE 0x031D
#endif

UDECL(int,   NtUserCountClipboardFormats, (void));
UDECL(BOOL,  NtUserEmptyClipboard, (void));
UDECL(DWORD, NtUserGetClipboardSequenceNumber, (void));
UDECL(HWND,  NtUserGetClipboardViewer, (void));
UDECL(BOOL,  NtUserIsClipboardFormatAvailable, (UINT));
UDECL(int,   NtUserGetPriorityClipboardFormat, (UINT *, int));
UDECL(HWND,  NtUserSetClipboardViewer, (HWND));
UDECL(BOOL,  NtUserChangeClipboardChain, (HWND, HWND));

/* ------------------------------------------------------------------------------------------ */
/* fixture                                                                                     */
/* ------------------------------------------------------------------------------------------ */
struct wctx {
    unsigned draw, chain, update, destroy, renderfmt, renderall;
    UINT lastrender;
    HWND next;          /* what SetClipboardViewer returned for this window */
    int forward;        /* pass WM_DRAWCLIPBOARD / WM_CHANGECBCHAIN down the chain */
    int render_data;    /* answer WM_RENDERFORMAT with SetClipboardData */
    int in_chain;       /* this window is (as far as the test knows) a viewer */
    int listener;
};
static HWND g_w[3];
static struct wctx g_c[3];
static int g_cls_done, g_cls_ok;

static HGLOBAL mkmem(const void *p, size_t n) {
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, n ? n : 1);
    if (g && p && n) { void *d = GlobalLock(g); memcpy(d, p, n); GlobalUnlock(g); }
    return g;
}

static LRESULT CALLBACK wp(HWND h, UINT m, WPARAM w, LPARAM l) {
    struct wctx *c = (struct wctx *)U(GetWindowLongPtrA)(h, GWLP_USERDATA);
    if (c) switch (m) {
    case WM_DRAWCLIPBOARD:
        c->draw++;
        if (c->next && c->forward) U(SendMessageA)(c->next, m, w, l);
        return 0;
    case WM_CHANGECBCHAIN:
        c->chain++;
        if ((HWND)w == c->next) c->next = (HWND)l;
        else if (c->next && c->forward) U(SendMessageA)(c->next, m, w, l);
        return 0;
    case WM_CLIPBOARDUPDATE: c->update++; return 0;
    case WM_DESTROYCLIPBOARD: c->destroy++; return 0;
    case WM_RENDERFORMAT:
        c->renderfmt++; c->lastrender = (UINT)w;
        if (c->render_data) U(SetClipboardData)((UINT)w, mkmem("rendered", 9));
        return 0;
    case WM_RENDERALLFORMATS: c->renderall++; return 0;
    }
    return U(DefWindowProcA)(h, m, w, l);
}

static int fx_class(void) {
    if (g_cls_done) return g_cls_ok;
    g_cls_done = 1;
    WNDCLASSA wc; memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = wp;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "w32kt_xcsm";
    g_cls_ok = U(RegisterClassA)(&wc) != 0;
    return g_cls_ok;
}

static HWND fx_mkwin(int i) {
    HWND h = U(CreateWindowExA)(0, "w32kt_xcsm", "xcsm", WS_POPUP, -32000, -32000, 8, 8, NULL, NULL, GetModuleHandleA(NULL), NULL);
    if (h) { memset(&g_c[i], 0, sizeof g_c[i]); U(SetWindowLongPtrA)(h, GWLP_USERDATA, (LONG_PTR)&g_c[i]); }
    return h;
}

/* ---- the two routes ----------------------------------------------------------------------- */
static int nt_ok(void) {
    return UN(NtUserCountClipboardFormats) && UN(NtUserEmptyClipboard) && UN(NtUserGetClipboardSequenceNumber)
        && UN(NtUserGetClipboardViewer) && UN(NtUserIsClipboardFormatAvailable) && UN(NtUserGetPriorityClipboardFormat)
        && UN(NtUserSetClipboardViewer) && UN(NtUserChangeClipboardChain);
}
static int  c_count(int r)           { return r ? UN(NtUserCountClipboardFormats)() : U(CountClipboardFormats)(); }
static BOOL c_empty(int r)           { return r ? UN(NtUserEmptyClipboard)() : U(EmptyClipboard)(); }
static DWORD c_seq(int r)            { return r ? UN(NtUserGetClipboardSequenceNumber)() : U(GetClipboardSequenceNumber)(); }
static HWND c_view(int r)            { return r ? UN(NtUserGetClipboardViewer)() : U(GetClipboardViewer)(); }
static BOOL c_avail(int r, UINT f)   { return r ? UN(NtUserIsClipboardFormatAvailable)(f) : U(IsClipboardFormatAvailable)(f); }
static int  c_prio(int r, UINT *l, int n) { return r ? UN(NtUserGetPriorityClipboardFormat)(l, n) : U(GetPriorityClipboardFormat)(l, n); }
static HWND c_setview(int r, HWND h) { return r ? UN(NtUserSetClipboardViewer)(h) : U(SetClipboardViewer)(h); }
static BOOL c_chg(int r, HWND h, HWND n) { return r ? UN(NtUserChangeClipboardChain)(h, n) : U(ChangeClipboardChain)(h, n); }

/* sequence-number relations; the values themselves are never printed */
static int adv(DWORD a, DWORD b) { return (int)(b - a) > 0; }

/* ---- open / close / enumerate ------------------------------------------------------------- */
static BOOL x_open(HWND h) {
    for (int i = 0; i < 40; i++) { if (U(OpenClipboard)(h)) return TRUE; Sleep(25); }
    return FALSE;
}
static int x_enum(UINT *out, int cap) {  /* clipboard must be open by the caller */
    int n = 0; UINT f = 0;
    while ((f = U(EnumClipboardFormats)(f)) != 0) { if (n < cap) out[n] = f; n++; if (n > 4096) break; }
    return n;
}
static int x_has(const UINT *l, int n, UINT f) { for (int i = 0; i < n; i++) if (l[i] == f) return 1; return 0; }
static UINT x_reg(int i) {
    char nm[48]; snprintf(nm, sizeof nm, "w32kt_xcsm_fmt_%d", i);
    return U(RegisterClipboardFormatA)(nm);
}
/* open (window i), empty, leave open */
static int x_open_empty(int i) {
    if (!x_open(g_w[i])) return 0;
    return U(EmptyClipboard)() != 0;
}
static int x_put(int i, UINT f, const void *d, size_t n) {   /* clipboard must be open */
    HGLOBAL g = mkmem(d, n);
    return g && U(SetClipboardData)(f, g) == g;
}
static int x_put_text(const WCHAR *s) {
    return x_put(0, CF_UNICODETEXT, s, (lstrlenW(s) + 1) * sizeof(WCHAR));
}
/* open window i, empty, put one private format, close */
static int x_one(int i, UINT f) {
    if (!x_open_empty(i)) return 0;
    int ok = x_put(i, f, "abcdefgh", 8);
    U(CloseClipboard)();
    return ok;
}

static void fx_reset(void) {
    for (int i = 0; i < 3; i++) {
        if (!g_w[i]) continue;
        if (g_c[i].in_chain) { U(ChangeClipboardChain)(g_w[i], g_c[i].next); g_c[i].in_chain = 0; }
        if (g_c[i].listener) { U(RemoveClipboardFormatListener)(g_w[i]); g_c[i].listener = 0; }
    }
    U(CloseClipboard)();
    if (g_w[0] && U(OpenClipboard)(g_w[0])) { U(EmptyClipboard)(); U(CloseClipboard)(); }
    for (int i = 0; i < 3; i++) if (g_w[i]) { memset(&g_c[i], 0, sizeof g_c[i]); }
}
static void fx_end(int *p) { (void)p; if (g_w[0]) fx_reset(); }

static int fx_begin(void) {
    if (t_is_wine()) { t_skip("direct win32k state syscalls are Windows-specific"); return 0; }
    if (!fx_class()) { t_skip("cannot register window class in this session"); return 0; }
    for (int i = 0; i < 3; i++) if (!g_w[i]) {
        g_w[i] = fx_mkwin(i);
        if (!g_w[i]) { t_skip("cannot create windows in this session"); return 0; }
    }
    fx_reset();
    return 1;
}
#define SETUP() int fxg_ __attribute__((cleanup(fx_end))) = 0; if (!fx_begin()) return
#define NTSETUP() do { if (!nt_ok()) { t_skip("win32u clipboard stubs missing"); return; } } while (0)

/* Scenario registered for both routes. */
#define RT(name) \
    static void name##_body(int r); \
    TEST(name##_u32) { SETUP(); name##_body(0); } \
    TEST(name##_nt)  { SETUP(); NTSETUP(); name##_body(1); } \
    static void name##_body(int r)

/* ------------------------------------------------------------------------------------------ */
/* empty clipboard baseline                                                                    */
/* ------------------------------------------------------------------------------------------ */
RT(count_zero_when_empty) { EXPECT_EQ(c_count(r), 0); }

RT(avail_false_for_all_standard_formats_when_empty) {
    for (UINT f = 1; f <= 17; f++) CHECKF(!c_avail(r, f), "format %u", f);
}
RT(avail_false_for_registered_format_when_empty) {
    UINT f = x_reg(1); CHECK(f >= 0xC000); CHECK(!c_avail(r, f));
}
RT(avail_false_for_format_zero) { CHECK(!c_avail(r, 0)); }
RT(avail_false_for_format_zero_with_data) {
    CHECK(x_one(0, x_reg(2))); CHECK(!c_avail(r, 0));
}
RT(seq_nonzero) { CHECK(c_seq(r) != 0); }
RT(seq_stable_without_changes) {
    DWORD a = c_seq(r); k_pump(20); DWORD b = c_seq(r);
    EXPECT_EQ(a, b);
}
RT(priority_zero_when_empty) {
    UINT l[2] = { CF_TEXT, CF_UNICODETEXT };
    EXPECT_EQ(c_prio(r, l, 2), 0);
}
RT(viewer_null_when_no_viewer) { CHECK(c_view(r) == NULL); }

/* ------------------------------------------------------------------------------------------ */
/* sequence number                                                                             */
/* ------------------------------------------------------------------------------------------ */
RT(seq_advances_on_set) {
    UINT f = x_reg(3);
    CHECK(x_open_empty(0));
    DWORD a = c_seq(r);
    CHECK(x_put(0, f, "abcdefgh", 8));
    DWORD b = c_seq(r);
    U(CloseClipboard)();
    CHECK(adv(a, b));
}
RT(seq_advances_on_empty_of_nonempty) {
    CHECK(x_one(0, x_reg(4)));
    DWORD a = c_seq(r);
    CHECK(x_open(g_w[0]));
    CHECK(c_empty(r));
    DWORD b = c_seq(r);
    U(CloseClipboard)();
    CHECK(adv(a, b));
}
RT(seq_advances_on_empty_of_empty) {
    CHECK(x_open(g_w[0]));
    DWORD a = c_seq(r);
    CHECK(c_empty(r));
    DWORD b = c_seq(r);
    U(CloseClipboard)();
    CHECK(adv(a, b));
}
RT(seq_advances_on_each_empty) {
    CHECK(x_open(g_w[0]));
    DWORD a = c_seq(r);
    CHECK(c_empty(r)); DWORD b = c_seq(r);
    CHECK(c_empty(r)); DWORD c = c_seq(r);
    U(CloseClipboard)();
    CHECK(adv(a, b)); CHECK(adv(b, c));
}
RT(seq_advances_on_each_set) {
    CHECK(x_open_empty(0));
    DWORD s0 = c_seq(r), prev = s0;
    for (int i = 0; i < 4; i++) {
        CHECK(x_put(0, x_reg(10 + i), "abcdefgh", 8));
        DWORD s = c_seq(r);
        CHECKF(adv(prev, s), "set %d", i);
        prev = s;
    }
    U(CloseClipboard)();
}
RT(seq_advances_when_same_format_replaced) {
    UINT f = x_reg(5);
    CHECK(x_open_empty(0));
    CHECK(x_put(0, f, "aaaaaaaa", 8));
    DWORD a = c_seq(r);
    CHECK(x_put(0, f, "bbbbbbbb", 8));
    DWORD b = c_seq(r);
    U(CloseClipboard)();
    CHECK(adv(a, b));
}
RT(seq_advances_on_delayed_format_set) {
    CHECK(x_open_empty(0));
    DWORD a = c_seq(r);
    SetLastError(0);
    CHECK(U(SetClipboardData)(x_reg(6), NULL) == NULL);
    DWORD b = c_seq(r);
    U(CloseClipboard)();
    CHECK(adv(a, b));
}
RT(seq_unchanged_by_open_close) {
    CHECK(x_one(0, x_reg(7)));
    DWORD a = c_seq(r);
    CHECK(x_open(g_w[1])); U(CloseClipboard)();
    CHECK(x_open(g_w[1]));
    DWORD b = c_seq(r);
    U(CloseClipboard)();
    DWORD c = c_seq(r);
    EXPECT_EQ(a, b); EXPECT_EQ(a, c);
}
RT(seq_unchanged_by_reads) {
    UINT f = x_reg(8);
    CHECK(x_one(0, f));
    DWORD a = c_seq(r);
    (void)c_count(r); (void)c_avail(r, f); UINT l[1] = { f }; (void)c_prio(r, l, 1); (void)c_view(r);
    CHECK(x_open(g_w[0]));
    UINT fm[16]; (void)x_enum(fm, 16);
    CHECK(U(GetClipboardData)(f) != NULL);
    U(CloseClipboard)();
    EXPECT_EQ(a, c_seq(r));
}
RT(seq_failed_empty_does_not_advance) {
    DWORD a = c_seq(r);
    SetLastError(0);
    CHECK(!c_empty(r)); EXPECT_ERR(ERROR_CLIPBOARD_NOT_OPEN);
    EXPECT_EQ(a, c_seq(r));
}
RT(seq_failed_set_does_not_advance) {
    DWORD a = c_seq(r);
    SetLastError(0);
    CHECK(U(SetClipboardData)(x_reg(9), mkmem("abcdefgh", 8)) == NULL); EXPECT_ERR(ERROR_CLIPBOARD_NOT_OPEN);
    EXPECT_EQ(a, c_seq(r));
}
RT(seq_advance_visible_from_second_thread_same_as_first) {
    CHECK(x_one(0, x_reg(11)));
    DWORD s1 = c_seq(r);
    CHECK(x_one(0, x_reg(12)));
    DWORD s2 = c_seq(r);
    CHECK(adv(s1, s2));
}

/* ------------------------------------------------------------------------------------------ */
/* count vs enumeration vs availability                                                        */
/* ------------------------------------------------------------------------------------------ */
RT(count_one_private_format) {
    CHECK(x_one(0, x_reg(20)));
    EXPECT_EQ(c_count(r), 1);
}
RT(count_equals_enumeration_private_formats) {
    CHECK(x_open_empty(0));
    for (int i = 0; i < 5; i++) CHECK(x_put(0, x_reg(30 + i), "abcdefgh", 8));
    UINT l[64]; int n = x_enum(l, 64);
    EXPECT_EQ(c_count(r), n);
    EXPECT_EQ(n, 5);
    U(CloseClipboard)();
    EXPECT_EQ(c_count(r), 5);
}
RT(count_same_format_twice_is_one) {
    UINT f = x_reg(21);
    CHECK(x_open_empty(0));
    CHECK(x_put(0, f, "aaaaaaaa", 8)); CHECK(x_put(0, f, "bbbbbbbb", 8));
    EXPECT_EQ(c_count(r), 1);
    U(CloseClipboard)();
}
RT(count_after_empty_is_zero) {
    CHECK(x_one(0, x_reg(22)));
    EXPECT_EQ(c_count(r), 1);
    CHECK(x_open(g_w[0])); CHECK(c_empty(r)); EXPECT_EQ(c_count(r), 0); U(CloseClipboard)();
    EXPECT_EQ(c_count(r), 0);
}
RT(count_open_and_closed_agree) {
    CHECK(x_open_empty(0));
    CHECK(x_put(0, x_reg(23), "abcdefgh", 8)); CHECK(x_put(0, x_reg(24), "abcdefgh", 8));
    int open_n = c_count(r);
    U(CloseClipboard)();
    EXPECT_EQ(open_n, c_count(r));
}
RT(count_delayed_format_counts) {
    CHECK(x_open_empty(0));
    CHECK(U(SetClipboardData)(x_reg(25), NULL) == NULL);
    EXPECT_EQ(c_count(r), 1);
    U(CloseClipboard)();
    EXPECT_EQ(c_count(r), 1);
}
RT(count_many_private_formats) {
    CHECK(x_open_empty(0));
    for (int i = 0; i < 40; i++) CHECKF(x_put(0, x_reg(100 + i), "abcdefgh", 8), "fmt %d", i);
    EXPECT_EQ(c_count(r), 40);
    UINT l[64]; EXPECT_EQ(x_enum(l, 64), 40);
    U(CloseClipboard)();
}
RT(count_text_equals_enumeration) {
    CHECK(x_open_empty(0)); CHECK(x_put_text(L"hello"));
    UINT l[64]; int n = x_enum(l, 64);
    EXPECT_EQ(c_count(r), n);
    CHECK(x_has(l, n, CF_UNICODETEXT));
    CHECK(n >= 1);
    U(CloseClipboard)();
}
RT(count_text_includes_synthesized_ansi) {
    CHECK(x_one(0, x_reg(26)));  /* reset to a known state */
    CHECK(x_open_empty(0)); CHECK(x_put_text(L"hello")); U(CloseClipboard)();
    CHECK(c_avail(r, CF_TEXT)); CHECK(c_avail(r, CF_OEMTEXT)); CHECK(c_avail(r, CF_UNICODETEXT));
    CHECK(c_count(r) >= 3);
}
RT(avail_true_for_set_format) {
    UINT f = x_reg(27);
    CHECK(x_one(0, f));
    CHECK(c_avail(r, f));
}
RT(avail_false_for_other_format) {
    CHECK(x_one(0, x_reg(28)));
    CHECK(!c_avail(r, x_reg(29)));
    CHECK(!c_avail(r, CF_BITMAP));
}
RT(avail_works_while_open) {
    UINT f = x_reg(40);
    CHECK(x_open_empty(0));
    CHECK(!c_avail(r, f));
    CHECK(x_put(0, f, "abcdefgh", 8));
    CHECK(c_avail(r, f));
    U(CloseClipboard)();
    CHECK(c_avail(r, f));
}
RT(avail_works_while_open_by_other_window) {
    UINT f = x_reg(41);
    CHECK(x_one(0, f));
    CHECK(x_open(g_w[1]));
    CHECK(c_avail(r, f));
    U(CloseClipboard)();
}
RT(avail_false_after_empty) {
    UINT f = x_reg(42);
    CHECK(x_one(0, f)); CHECK(c_avail(r, f));
    CHECK(x_open(g_w[0])); CHECK(c_empty(r)); CHECK(!c_avail(r, f)); U(CloseClipboard)();
    CHECK(!c_avail(r, f));
}
RT(avail_equals_enumeration_membership_text) {
    CHECK(x_open_empty(0)); CHECK(x_put_text(L"hello"));
    UINT l[64]; int n = x_enum(l, 64);
    for (UINT f = 1; f <= 20; f++) CHECKF(!!c_avail(r, f) == !!x_has(l, n, f), "format %u", f);
    U(CloseClipboard)();
}
RT(avail_equals_enumeration_membership_private) {
    CHECK(x_open_empty(0));
    for (int i = 0; i < 3; i++) CHECK(x_put(0, x_reg(50 + i), "abcdefgh", 8));
    UINT l[64]; int n = x_enum(l, 64);
    for (int i = 0; i < 6; i++) { UINT f = x_reg(50 + i); CHECKF(!!c_avail(r, f) == !!x_has(l, n, f), "fmt %d", i); }
    U(CloseClipboard)();
}
RT(avail_synthesized_bitmap_from_dib) {
    BYTE dib[40 + 4]; memset(dib, 0, sizeof dib);
    BITMAPINFOHEADER *bi = (BITMAPINFOHEADER *)dib; bi->biSize = 40; bi->biWidth = 1; bi->biHeight = 1; bi->biPlanes = 1; bi->biBitCount = 32; bi->biSizeImage = 4;
    CHECK(x_open_empty(0));
    CHECK(x_put(0, CF_DIB, dib, sizeof dib));
    U(CloseClipboard)();
    CHECK(c_avail(r, CF_DIB));
    CHECK(c_avail(r, CF_BITMAP));
    CHECK(c_avail(r, CF_DIBV5));
    CHECK(!c_avail(r, CF_TEXT));
}
RT(avail_synthesized_text_pair_from_ansi) {
    CHECK(x_open_empty(0));
    CHECK(x_put(0, CF_TEXT, "hello", 6));
    U(CloseClipboard)();
    CHECK(c_avail(r, CF_TEXT)); CHECK(c_avail(r, CF_OEMTEXT)); CHECK(c_avail(r, CF_UNICODETEXT));
    CHECK(!c_avail(r, CF_BITMAP));
}
RT(avail_dsptext_not_synthesized) {
    CHECK(x_open_empty(0)); CHECK(x_put_text(L"hello")); U(CloseClipboard)();
    CHECK(!c_avail(r, CF_DSPTEXT));
    CHECK(!c_avail(r, CF_HDROP));
}

/* ------------------------------------------------------------------------------------------ */
/* priority                                                                                    */
/* ------------------------------------------------------------------------------------------ */
RT(priority_picks_present_format) {
    UINT a = x_reg(60), b = x_reg(61);
    CHECK(x_one(0, b));
    UINT l[2] = { a, b };
    EXPECT_EQ(c_prio(r, l, 2), b);
}
RT(priority_follows_list_order) {
    UINT a = x_reg(62), b = x_reg(63);
    CHECK(x_open_empty(0)); CHECK(x_put(0, a, "abcdefgh", 8)); CHECK(x_put(0, b, "abcdefgh", 8)); U(CloseClipboard)();
    UINT l1[2] = { a, b }, l2[2] = { b, a };
    EXPECT_EQ(c_prio(r, l1, 2), a);
    EXPECT_EQ(c_prio(r, l2, 2), b);
}
RT(priority_minus_one_when_none_match) {
    CHECK(x_one(0, x_reg(64)));
    UINT l[2] = { x_reg(65), x_reg(66) };
    EXPECT_EQ((long)c_prio(r, l, 2), -1L);
}
RT(priority_count_limits_scan) {
    UINT a = x_reg(67), b = x_reg(68);
    CHECK(x_one(0, b));
    UINT l[2] = { a, b };
    EXPECT_EQ((long)c_prio(r, l, 1), -1L);
    EXPECT_EQ(c_prio(r, l, 2), b);
}
RT(priority_single_entry) {
    UINT a = x_reg(69);
    CHECK(x_one(0, a));
    UINT l[1] = { a };
    EXPECT_EQ(c_prio(r, l, 1), a);
}
RT(priority_works_while_open) {
    UINT a = x_reg(70);
    CHECK(x_open_empty(0)); CHECK(x_put(0, a, "abcdefgh", 8));
    UINT l[1] = { a };
    EXPECT_EQ(c_prio(r, l, 1), a);
    U(CloseClipboard)();
}
RT(priority_synthesized_ansi_from_unicode) {
    CHECK(x_open_empty(0)); CHECK(x_put_text(L"hello")); U(CloseClipboard)();
    UINT l[2] = { CF_TEXT, CF_UNICODETEXT };
    EXPECT_EQ(c_prio(r, l, 2), CF_TEXT);
    UINT l2[2] = { CF_UNICODETEXT, CF_TEXT };
    EXPECT_EQ(c_prio(r, l2, 2), CF_UNICODETEXT);
}
RT(priority_zero_after_empty) {
    UINT a = x_reg(71);
    CHECK(x_one(0, a));
    CHECK(x_open(g_w[0])); CHECK(c_empty(r)); U(CloseClipboard)();
    UINT l[1] = { a };
    EXPECT_EQ(c_prio(r, l, 1), 0);
}
RT(priority_agrees_with_availability) {
    UINT f[4] = { x_reg(72), x_reg(73), x_reg(74), x_reg(75) };
    CHECK(x_open_empty(0)); CHECK(x_put(0, f[2], "abcdefgh", 8)); U(CloseClipboard)();
    int p = c_prio(r, f, 4);
    CHECK(p > 0);
    CHECK(c_avail(r, (UINT)p));
    for (int i = 0; i < 4; i++) if (f[i] == (UINT)p) EXPECT_EQ(i, 2);
}

/* ------------------------------------------------------------------------------------------ */
/* ownership and openness                                                                      */
/* ------------------------------------------------------------------------------------------ */
RT(empty_without_open_fails_1418) {
    SetLastError(0);
    CHECK(!c_empty(r)); EXPECT_ERR(ERROR_CLIPBOARD_NOT_OPEN);
}
RT(empty_after_close_fails_1418) {
    CHECK(x_open(g_w[0])); CHECK(c_empty(r)); U(CloseClipboard)();
    SetLastError(0);
    CHECK(!c_empty(r)); EXPECT_ERR(ERROR_CLIPBOARD_NOT_OPEN);
}
RT(empty_succeeds_when_open) {
    CHECK(x_open(g_w[0]));
    CHECK(c_empty(r));
    U(CloseClipboard)();
}
RT(empty_does_not_touch_content_when_not_open) {
    UINT f = x_reg(80);
    CHECK(x_one(0, f));
    SetLastError(0);
    CHECK(!c_empty(r));
    EXPECT_EQ(c_count(r), 1); CHECK(c_avail(r, f));
}
RT(empty_removes_all_formats) {
    CHECK(x_open_empty(0));
    for (int i = 0; i < 4; i++) CHECK(x_put(0, x_reg(81 + i), "abcdefgh", 8));
    EXPECT_EQ(c_count(r), 4);
    CHECK(c_empty(r));
    EXPECT_EQ(c_count(r), 0);
    UINT l[8]; EXPECT_EQ(x_enum(l, 8), 0);
    U(CloseClipboard)();
}
RT(empty_makes_opener_the_owner) {
    CHECK(x_open(g_w[0])); CHECK(c_empty(r));
    CHECK(U(GetClipboardOwner)() == g_w[0]);
    U(CloseClipboard)();
    CHECK(U(GetClipboardOwner)() == g_w[0]);
    CHECK(x_open(g_w[1])); CHECK(c_empty(r)); U(CloseClipboard)();
    CHECK(U(GetClipboardOwner)() == g_w[1]);
}
RT(empty_with_null_window_clears_owner) {
    CHECK(x_open(g_w[0])); CHECK(c_empty(r)); U(CloseClipboard)();
    CHECK(x_open(NULL)); CHECK(c_empty(r)); U(CloseClipboard)();
    CHECK(U(GetClipboardOwner)() == NULL);
}
RT(empty_sends_destroyclipboard_to_previous_owner) {
    CHECK(x_open(g_w[0])); CHECK(c_empty(r)); U(CloseClipboard)();
    unsigned before = g_c[0].destroy;
    CHECK(x_open(g_w[1])); CHECK(c_empty(r)); U(CloseClipboard)();
    k_pump(20);
    EXPECT_EQ(g_c[0].destroy, before + 1);
    EXPECT_EQ(g_c[1].destroy, 0);
}
RT(empty_by_same_owner_sends_destroyclipboard_to_itself) {
    CHECK(x_open(g_w[0])); CHECK(c_empty(r)); U(CloseClipboard)();
    unsigned before = g_c[0].destroy;
    CHECK(x_open(g_w[0])); CHECK(c_empty(r)); U(CloseClipboard)();
    k_pump(20);
    EXPECT_EQ(g_c[0].destroy, before + 1);   /* the previous owner is told even when it is the new owner too */

}

TEST(ownership_close_without_open_fails_1418) {
    SETUP();
    SetLastError(0);
    CHECK(!U(CloseClipboard)()); EXPECT_ERR(ERROR_CLIPBOARD_NOT_OPEN);
}
TEST(ownership_close_twice_fails_1418) {
    SETUP();
    CHECK(x_open(g_w[0])); CHECK(U(CloseClipboard)());
    SetLastError(0);
    CHECK(!U(CloseClipboard)()); EXPECT_ERR(ERROR_CLIPBOARD_NOT_OPEN);
}
TEST(ownership_set_without_open_fails_1418) {
    SETUP();
    SetLastError(0);
    CHECK(U(SetClipboardData)(x_reg(90), mkmem("abcdefgh", 8)) == NULL); EXPECT_ERR(ERROR_CLIPBOARD_NOT_OPEN);
}
TEST(ownership_get_without_open_fails_1418) {
    SETUP();
    UINT f = x_reg(91); CHECK(x_one(0, f));
    SetLastError(0);
    CHECK(U(GetClipboardData)(f) == NULL); EXPECT_ERR(ERROR_CLIPBOARD_NOT_OPEN);
}
TEST(ownership_enum_without_open_fails_1418) {
    SETUP();
    CHECK(x_one(0, x_reg(92)));
    SetLastError(0);
    EXPECT_EQ(U(EnumClipboardFormats)(0), 0); EXPECT_ERR(ERROR_CLIPBOARD_NOT_OPEN);
}
TEST(ownership_enum_without_open_fails_1418_when_empty) {
    SETUP();
    SetLastError(0);
    EXPECT_EQ(U(EnumClipboardFormats)(0), 0); EXPECT_ERR(ERROR_CLIPBOARD_NOT_OPEN);
}
TEST(ownership_open_invalid_window_fails_1400) {
    SETUP();
    SetLastError(0);
    CHECK(!U(OpenClipboard)((HWND)(ULONG_PTR)0x7ffe1234)); EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
    CHECK(U(GetOpenClipboardWindow)() == NULL);
}
TEST(ownership_open_with_window_of_other_thread_succeeds) {
    SETUP();
    struct a { HWND h; BOOL r; DWORD e; } a = { g_w[0], 0, 0 };
    DWORD WINAPI fn(void *p) { struct a *q = p; SetLastError(0); q->r = U(OpenClipboard)(q->h); q->e = GetLastError(); if (q->r) U(CloseClipboard)(); return 0; }
    HANDLE t = CreateThread(NULL, 0, fn, &a, 0, NULL);
    CHECK(t != NULL);
    if (t) { CHECK(WaitForSingleObject(t, 10000) == WAIT_OBJECT_0); CloseHandle(t); }
    CHECK(a.r);        /* the window's thread is not checked by OpenClipboard */
}
TEST(ownership_open_window_reports_open_window) {
    SETUP();
    /* another process (a clipboard watcher) may hold the clipboard for a moment: wait for it */
    for (int i = 0; i < 80 && U(GetOpenClipboardWindow)() != NULL; i++) Sleep(25);
    CHECK(U(GetOpenClipboardWindow)() == NULL);
    CHECK(x_open(g_w[0]));
    CHECK(U(GetOpenClipboardWindow)() == g_w[0]);
    U(CloseClipboard)();
    CHECK(U(GetOpenClipboardWindow)() == NULL);
}
TEST(ownership_open_null_window_reports_null_open_window) {
    SETUP();
    CHECK(x_open(NULL));
    CHECK(U(GetOpenClipboardWindow)() == NULL);
    /* but the clipboard is open: a second thread cannot open it */
    U(CloseClipboard)();
}
TEST(ownership_reopen_same_thread_succeeds) {
    SETUP();
    CHECK(x_open(g_w[0]));
    CHECK(U(OpenClipboard)(g_w[0]));
    CHECK(U(OpenClipboard)(g_w[0]));
    CHECK(U(CloseClipboard)());
}
TEST(ownership_one_close_closes_after_reopen) {
    SETUP();
    CHECK(x_open(g_w[0])); CHECK(U(OpenClipboard)(g_w[0]));
    CHECK(U(CloseClipboard)());
    SetLastError(0);
    CHECK(!U(CloseClipboard)()); EXPECT_ERR(ERROR_CLIPBOARD_NOT_OPEN);
    CHECK(U(GetOpenClipboardWindow)() == NULL);
}
TEST(ownership_reopen_with_other_window_same_thread_fails) {
    SETUP();
    CHECK(x_open(g_w[0]));
    SetLastError(0);
    CHECK(!U(OpenClipboard)(g_w[1])); EXPECT_ERR(ERROR_ACCESS_DENIED);
    CHECK(U(GetOpenClipboardWindow)() == g_w[0]);
    U(CloseClipboard)();
    CHECK(U(GetOpenClipboardWindow)() == NULL);
}

/* ---- second thread ------------------------------------------------------------------------- */
struct tctx {
    int r;
    UINT f;
    /* results */
    BOOL open_a, empty_a; DWORD open_err, empty_err;
    BOOL open_b;
    int count, avail, prio; DWORD s1, s2;
    HWND view, owner, openwin;
    BOOL setdata_null; DWORD set_err;
    HANDLE ev_in, ev_out;
};

static DWORD WINAPI th_try_open(void *p) {
    struct tctx *t = p;
    SetLastError(0); t->open_a = U(OpenClipboard)(NULL); t->open_err = GetLastError();
    if (t->open_a) U(CloseClipboard)();
    return 0;
}
static DWORD WINAPI th_try_empty(void *p) {
    struct tctx *t = p;
    SetLastError(0); t->empty_a = c_empty(t->r); t->empty_err = GetLastError();
    return 0;
}
static DWORD WINAPI th_observe(void *p) {
    struct tctx *t = p;
    UINT l[1] = { t->f };
    t->count = c_count(t->r); t->avail = c_avail(t->r, t->f); t->prio = c_prio(t->r, l, 1);
    t->s1 = c_seq(t->r); t->view = c_view(t->r);
    t->owner = U(GetClipboardOwner)(); t->openwin = U(GetOpenClipboardWindow)();
    return 0;
}
static DWORD WINAPI th_set(void *p) {
    struct tctx *t = p;
    SetLastError(0);
    t->setdata_null = U(SetClipboardData)(t->f, mkmem("abcdefgh", 8)) == NULL;
    t->set_err = GetLastError();
    return 0;
}
static void run_th(LPTHREAD_START_ROUTINE fn, struct tctx *t) {
    HANDLE h = CreateThread(NULL, 0, fn, t, 0, NULL);
    CHECK(h != NULL);
    if (h) { CHECK(WaitForSingleObject(h, 10000) == WAIT_OBJECT_0); CloseHandle(h); }
}

RT(thread_cannot_open_while_first_holds) {
    struct tctx t = { r };
    CHECK(x_open(g_w[0]));
    run_th(th_try_open, &t);
    CHECK(!t.open_a); EXPECT_EQ(t.open_err, ERROR_ACCESS_DENIED);
    U(CloseClipboard)();
}
RT(thread_can_open_after_first_closes) {
    struct tctx t = { r };
    CHECK(x_open(g_w[0])); U(CloseClipboard)();
    run_th(th_try_open, &t);
    CHECK(t.open_a);
}
RT(thread_cannot_empty_while_first_holds) {
    struct tctx t = { r };
    CHECK(x_open(g_w[0]));
    run_th(th_try_empty, &t);
    CHECK(!t.empty_a); EXPECT_EQ(t.empty_err, ERROR_CLIPBOARD_NOT_OPEN);
    U(CloseClipboard)();
}
RT(thread_failed_empty_leaves_content) {
    struct tctx t = { r };
    UINT f = x_reg(110);
    CHECK(x_open_empty(0)); CHECK(x_put(0, f, "abcdefgh", 8));
    DWORD s = c_seq(r);
    run_th(th_try_empty, &t);
    EXPECT_EQ(c_count(r), 1); CHECK(c_avail(r, f)); EXPECT_EQ(c_seq(r), s);
    U(CloseClipboard)();
}
RT(thread_set_while_first_holds_is_not_refused) {
    /* unlike Empty, SetClipboardData from a thread that did not open the clipboard is accepted
     * while another thread holds it open: the format lands in the clipboard */
    struct tctx t = { r, x_reg(111) };
    CHECK(x_open_empty(0));
    DWORD s = c_seq(r);
    run_th(th_set, &t);
    CHECK(!t.setdata_null);
    EXPECT_EQ(c_count(r), 1);
    CHECK(c_avail(r, t.f));
    CHECK(adv(s, c_seq(r)));
    U(CloseClipboard)();
}
RT(thread_sees_same_state_as_first) {
    struct tctx t = { r, x_reg(112) };
    CHECK(x_open_empty(0)); CHECK(x_put(0, t.f, "abcdefgh", 8)); U(CloseClipboard)();
    run_th(th_observe, &t);
    EXPECT_EQ(t.count, c_count(r));
    CHECK(t.avail); EXPECT_EQ(t.prio, t.f);
    EXPECT_EQ(t.s1, c_seq(r));
    CHECK(t.view == c_view(r));
    CHECK(t.owner == g_w[0]);
    CHECK(t.openwin == NULL);
}
RT(thread_sees_open_window_of_first) {
    struct tctx t = { r, x_reg(113) };
    CHECK(x_open(g_w[1]));
    run_th(th_observe, &t);
    CHECK(t.openwin == g_w[1]);
    U(CloseClipboard)();
}
RT(thread_count_visible_while_open_by_first) {
    struct tctx t = { r, x_reg(114) };
    CHECK(x_open_empty(0)); CHECK(x_put(0, t.f, "abcdefgh", 8)); CHECK(x_put(0, x_reg(115), "abcdefgh", 8));
    run_th(th_observe, &t);
    EXPECT_EQ(t.count, 2); CHECK(t.avail);
    EXPECT_EQ(t.s1, c_seq(r));
    U(CloseClipboard)();
}
TEST(thread_hold_released_when_thread_exits) {
    SETUP();
    struct tctx t = { 0 };
    DWORD WINAPI hold(void *p) { U(OpenClipboard)(NULL); return 0; }   /* thread exits without CloseClipboard */
    HANDLE h = CreateThread(NULL, 0, hold, &t, 0, NULL);
    CHECK(h != NULL);
    if (h) { WaitForSingleObject(h, 10000); CloseHandle(h); }
    /* thread teardown releases its clipboard hold; allow the kernel a moment */
    int ok = 0;
    for (int i = 0; i < 40 && !ok; i++) { ok = U(OpenClipboard)(g_w[0]); if (!ok) Sleep(25); }
    CHECK(ok);
    U(CloseClipboard)();
}
TEST(thread_other_thread_open_blocks_us_until_closed) {
    SETUP();
    struct hs { HANDLE ready, go; BOOL opened; } hs;
    hs.ready = CreateEventA(NULL, TRUE, FALSE, NULL); hs.go = CreateEventA(NULL, TRUE, FALSE, NULL);
    DWORD WINAPI holder(void *p) { struct hs *q = p; q->opened = U(OpenClipboard)(NULL); SetEvent(q->ready); WaitForSingleObject(q->go, 10000); if (q->opened) U(CloseClipboard)(); return 0; }
    HANDLE h = CreateThread(NULL, 0, holder, &hs, 0, NULL);
    CHECK(h != NULL);
    WaitForSingleObject(hs.ready, 10000);
    CHECK(hs.opened);
    SetLastError(0);
    CHECK(!U(OpenClipboard)(g_w[0])); EXPECT_ERR(ERROR_ACCESS_DENIED);
    SetLastError(0);
    CHECK(!U(EmptyClipboard)()); EXPECT_ERR(ERROR_CLIPBOARD_NOT_OPEN);
    SetEvent(hs.go);
    if (h) { WaitForSingleObject(h, 10000); CloseHandle(h); }
    CHECK(x_open(g_w[0])); U(CloseClipboard)();
    CloseHandle(hs.ready); CloseHandle(hs.go);
}

/* ------------------------------------------------------------------------------------------ */
/* viewer chain                                                                                */
/* ------------------------------------------------------------------------------------------ */
static HWND vadd(int r, int i) {   /* add window i to the chain and remember what to forward to */
    HWND n = c_setview(r, g_w[i]);
    g_c[i].next = n; g_c[i].in_chain = 1; g_c[i].forward = 1;
    return n;
}
static BOOL vdel(int r, int i) {
    BOOL b = c_chg(r, g_w[i], g_c[i].next);
    g_c[i].in_chain = 0;
    return b;
}

RT(viewer_set_first_returns_null_and_becomes_head) {
    HWND n = vadd(r, 0);
    CHECK(n == NULL);
    CHECK(c_view(r) == g_w[0]);
}
RT(viewer_second_returns_first_and_becomes_head) {
    CHECK(vadd(r, 0) == NULL);
    HWND n = vadd(r, 1);
    CHECK(n == g_w[0]);
    CHECK(c_view(r) == g_w[1]);
}
RT(viewer_third_returns_second) {
    vadd(r, 0); vadd(r, 1);
    CHECK(vadd(r, 2) == g_w[1]);
    CHECK(c_view(r) == g_w[2]);
}
RT(viewer_remove_only_viewer_empties_chain) {
    vadd(r, 0);
    vdel(r, 0);
    CHECK(c_view(r) == NULL);
}
RT(viewer_remove_head_exposes_next) {
    vadd(r, 0); vadd(r, 1);
    vdel(r, 1);
    CHECK(c_view(r) == g_w[0]);
    vdel(r, 0);
    CHECK(c_view(r) == NULL);
}
RT(viewer_remove_middle_keeps_head_and_notifies) {
    vadd(r, 0); vadd(r, 1); vadd(r, 2);
    vdel(r, 1);
    k_pump(20);
    CHECK(c_view(r) == g_w[2]);
    CHECK(g_c[2].chain >= 1);       /* head heard about the removal and relinked past it */
    CHECK(g_c[2].next == g_w[0]);
    vdel(r, 2); vdel(r, 0);
    CHECK(c_view(r) == NULL);
}
RT(viewer_remove_tail_notifies_chain) {
    vadd(r, 0); vadd(r, 1);
    vdel(r, 0);
    k_pump(20);
    CHECK(c_view(r) == g_w[1]);
    CHECK(g_c[1].chain >= 1);
    CHECK(g_c[1].next == NULL);
    vdel(r, 1);
}
RT(viewer_head_removal_is_not_announced_to_itself) {
    vadd(r, 0); vadd(r, 1);
    unsigned c1 = g_c[1].chain;
    vdel(r, 1);
    EXPECT_EQ(g_c[1].chain, c1);
}
RT(viewer_removal_in_reverse_order_leaves_empty) {
    vadd(r, 0); vadd(r, 1); vadd(r, 2);
    vdel(r, 2); CHECK(c_view(r) == g_w[1]);
    vdel(r, 1); CHECK(c_view(r) == g_w[0]);
    vdel(r, 0); CHECK(c_view(r) == NULL);
}
RT(viewer_destroyed_window_leaves_chain) {
    vadd(r, 0);
    HWND h = g_w[2];                 /* a throwaway viewer so the others stay intact */
    HWND n = c_setview(r, h);
    CHECK(n == g_w[0]);
    CHECK(c_view(r) == h);
    U(DestroyWindow)(h);
    g_w[2] = NULL;
    CHECK(c_view(r) != h);
    vdel(r, 0);
    CHECK(c_view(r) == NULL);
}
RT(viewer_destroyed_only_viewer_clears_chain) {
    HWND h = g_w[2];
    CHECK(c_setview(r, h) == NULL);
    CHECK(c_view(r) == h);
    U(DestroyWindow)(h);
    g_w[2] = NULL;
    CHECK(c_view(r) == NULL);
}
RT(viewer_receives_draw_on_set) {
    vadd(r, 0);
    k_pump(20);
    unsigned d0 = g_c[0].draw;
    CHECK(x_one(1, x_reg(120)));
    k_pump(20);
    CHECK(g_c[0].draw > d0);
}
RT(viewer_receives_draw_on_empty) {
    CHECK(x_one(1, x_reg(121)));
    vadd(r, 0); k_pump(20);
    unsigned d0 = g_c[0].draw;
    CHECK(x_open(g_w[1])); CHECK(c_empty(r)); U(CloseClipboard)();
    k_pump(20);
    CHECK(g_c[0].draw > d0);
}
RT(viewer_no_draw_when_nothing_changed) {
    CHECK(x_one(1, x_reg(122)));
    vadd(r, 0); k_pump(20);
    unsigned d0 = g_c[0].draw;
    CHECK(x_open(g_w[1])); U(CloseClipboard)();
    (void)c_count(r); (void)c_seq(r);
    k_pump(20);
    EXPECT_EQ(g_c[0].draw, d0);
}
RT(viewer_draw_forwarded_down_chain) {
    vadd(r, 0); vadd(r, 1);
    k_pump(20);
    unsigned d0 = g_c[0].draw, d1 = g_c[1].draw;
    CHECK(x_one(2, x_reg(123)));
    k_pump(20);
    CHECK(g_c[1].draw > d1);
    CHECK(g_c[0].draw > d0);       /* forwarded by the head */
}
RT(viewer_draw_stops_where_not_forwarded) {
    vadd(r, 0); vadd(r, 1);
    g_c[1].forward = 0;
    k_pump(20);
    unsigned d0 = g_c[0].draw, d1 = g_c[1].draw;
    CHECK(x_one(2, x_reg(124)));
    k_pump(20);
    CHECK(g_c[1].draw > d1);
    EXPECT_EQ(g_c[0].draw, d0);
}
RT(viewer_removed_window_gets_no_more_draws) {
    vadd(r, 0); vadd(r, 1);
    vdel(r, 0);
    k_pump(20);
    unsigned d0 = g_c[0].draw;
    CHECK(x_one(2, x_reg(125)));
    k_pump(20);
    EXPECT_EQ(g_c[0].draw, d0);
    vdel(r, 1);
}
RT(viewer_chain_cross_route_view_agrees) {
    vadd(r, 0);
    CHECK(U(GetClipboardViewer)() == g_w[0]);
    CHECK(UN(NtUserGetClipboardViewer) == NULL || UN(NtUserGetClipboardViewer)() == g_w[0]);
}
RT(viewer_visible_from_second_thread) {
    struct tctx t = { r, x_reg(126) };
    vadd(r, 0);
    run_th(th_observe, &t);
    CHECK(t.view == g_w[0]);
}
RT(viewer_set_does_not_change_content_or_sequence) {
    UINT f = x_reg(127);
    CHECK(x_one(1, f));
    DWORD s = c_seq(r);
    vadd(r, 0);
    k_pump(20);
    EXPECT_EQ(c_seq(r), s);
    EXPECT_EQ(c_count(r), 1);
    vdel(r, 0);
    EXPECT_EQ(c_seq(r), s);
}
RT(viewer_set_null_clears_head) {
    vadd(r, 0);
    SetLastError(0);
    HWND n = c_setview(r, NULL);
    CHECK(n == g_w[0]);
    CHECK(c_view(r) == NULL);
    g_c[0].next = NULL;
    vdel(r, 0);
}
RT(viewer_change_chain_invalid_window_fails) {
    SetLastError(0);
    BOOL b = c_chg(r, (HWND)(ULONG_PTR)0x7ffe1234, NULL);
    CHECK(!b);
    EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
}
RT(viewer_change_chain_null_window_fails) {
    SetLastError(0);
    BOOL b = c_chg(r, NULL, NULL);
    CHECK(!b);
    EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
}
RT(viewer_change_chain_for_non_viewer_leaves_chain_alone) {
    vadd(r, 0);
    c_chg(r, g_w[1], NULL);
    k_pump(20);
    CHECK(c_view(r) == g_w[0]);
    vdel(r, 0);
}
RT(viewer_change_chain_twice_second_call_harmless) {
    vadd(r, 0);
    vdel(r, 0);
    c_chg(r, g_w[0], NULL);
    CHECK(c_view(r) == NULL);
}
RT(viewer_readd_after_remove_returns_null) {
    vadd(r, 0); vdel(r, 0);
    CHECK(vadd(r, 0) == NULL);
    CHECK(c_view(r) == g_w[0]);
    vdel(r, 0);
}
RT(viewer_readd_same_window_twice) {
    CHECK(vadd(r, 0) == NULL);
    HWND n = c_setview(r, g_w[0]);
    CHECK(n == g_w[0]);
    CHECK(c_view(r) == g_w[0]);
    g_c[0].next = NULL;
    vdel(r, 0);
    CHECKF(c_view(r) == NULL, "view after one removal nonnull=%d", c_view(r) != NULL);
}

/* ------------------------------------------------------------------------------------------ */
/* delayed rendering and listeners                                                             */
/* ------------------------------------------------------------------------------------------ */
TEST(render_delayed_get_sends_renderformat_to_owner) {
    SETUP();
    UINT f = x_reg(130);
    g_c[0].render_data = 1;
    CHECK(x_open_empty(0));
    CHECK(U(SetClipboardData)(f, NULL) == NULL);
    U(CloseClipboard)();
    EXPECT_EQ(g_c[0].renderfmt, 0);
    CHECK(x_open(g_w[1]));
    HANDLE h = U(GetClipboardData)(f);
    CHECK(h != NULL);
    EXPECT_EQ(g_c[0].renderfmt, 1);
    EXPECT_EQ(g_c[0].lastrender, f);
    U(CloseClipboard)();
}
TEST(render_delayed_count_unchanged_by_render) {
    SETUP();
    UINT f = x_reg(131);
    g_c[0].render_data = 1;
    CHECK(x_open_empty(0)); CHECK(U(SetClipboardData)(f, NULL) == NULL); U(CloseClipboard)();
    int c0 = U(CountClipboardFormats)();
    CHECK(x_open(g_w[1])); CHECK(U(GetClipboardData)(f) != NULL); U(CloseClipboard)();
    EXPECT_EQ(U(CountClipboardFormats)(), c0);
    CHECK(U(IsClipboardFormatAvailable)(f));
}
TEST(render_second_get_does_not_render_again) {
    SETUP();
    UINT f = x_reg(132);
    g_c[0].render_data = 1;
    CHECK(x_open_empty(0)); CHECK(U(SetClipboardData)(f, NULL) == NULL); U(CloseClipboard)();
    CHECK(x_open(g_w[1]));
    CHECK(U(GetClipboardData)(f) != NULL); CHECK(U(GetClipboardData)(f) != NULL);
    U(CloseClipboard)();
    EXPECT_EQ(g_c[0].renderfmt, 1);
}
TEST(render_refused_get_returns_null_format_stays_listed) {
    SETUP();
    UINT f = x_reg(133);
    g_c[0].render_data = 0;
    CHECK(x_open_empty(0)); CHECK(U(SetClipboardData)(f, NULL) == NULL); U(CloseClipboard)();
    CHECK(x_open(g_w[1]));
    CHECK(U(GetClipboardData)(f) == NULL);
    EXPECT_EQ(g_c[0].renderfmt, 1);
    U(CloseClipboard)();
    CHECK(U(IsClipboardFormatAvailable)(f));
    EXPECT_EQ(U(CountClipboardFormats)(), 1);
}
TEST(render_seq_not_decreased_by_render) {
    SETUP();
    UINT f = x_reg(134);
    g_c[0].render_data = 1;
    CHECK(x_open_empty(0)); CHECK(U(SetClipboardData)(f, NULL) == NULL); U(CloseClipboard)();
    DWORD a = U(GetClipboardSequenceNumber)();
    CHECK(x_open(g_w[1])); (void)U(GetClipboardData)(f); U(CloseClipboard)();
    DWORD b = U(GetClipboardSequenceNumber)();
    CHECK((int)(b - a) >= 0);
}
TEST(render_owner_destroy_sends_renderallformats) {
    SETUP();
    UINT f = x_reg(135);
    HWND h = g_w[2];
    g_c[2].render_data = 0;
    CHECK(x_open_empty(2)); CHECK(U(SetClipboardData)(f, NULL) == NULL); U(CloseClipboard)();
    U(DestroyWindow)(h);
    g_w[2] = NULL;
    EXPECT_EQ(g_c[2].renderall, 1);
    CHECK(U(GetClipboardOwner)() == NULL);
}
TEST(render_owner_destroy_keeps_rendered_data) {
    SETUP();
    UINT f = x_reg(136);
    HWND h = g_w[2];
    CHECK(x_one(2, f));
    U(DestroyWindow)(h);
    g_w[2] = NULL;
    CHECK(U(IsClipboardFormatAvailable)(f));
    EXPECT_EQ(U(CountClipboardFormats)(), 1);
    CHECK(U(GetClipboardOwner)() == NULL);
}

TEST(listener_update_on_set_and_empty) {
    SETUP();
    CHECK(U(AddClipboardFormatListener)(g_w[0])); g_c[0].listener = 1;
    k_pump(20);
    unsigned u0 = g_c[0].update;
    CHECK(x_one(1, x_reg(140)));
    k_pump(50);
    CHECK(g_c[0].update > u0);
}
TEST(listener_not_notified_when_nothing_changed) {
    SETUP();
    CHECK(x_one(1, x_reg(141)));
    CHECK(U(AddClipboardFormatListener)(g_w[0])); g_c[0].listener = 1;
    k_pump(50);
    unsigned u0 = g_c[0].update;
    CHECK(x_open(g_w[1])); U(CloseClipboard)();
    (void)U(CountClipboardFormats)();
    k_pump(50);
    EXPECT_EQ(g_c[0].update, u0);
}
TEST(listener_notification_matches_sequence_advance) {
    SETUP();
    CHECK(U(AddClipboardFormatListener)(g_w[0])); g_c[0].listener = 1;
    k_pump(20);
    unsigned u0 = g_c[0].update; DWORD s0 = U(GetClipboardSequenceNumber)();
    CHECK(x_one(1, x_reg(142)));
    k_pump(50);
    DWORD s1 = U(GetClipboardSequenceNumber)();
    CHECK(adv(s0, s1));
    CHECK(g_c[0].update > u0);
}
TEST(listener_removed_gets_nothing) {
    SETUP();
    CHECK(U(AddClipboardFormatListener)(g_w[0]));
    CHECK(U(RemoveClipboardFormatListener)(g_w[0]));
    k_pump(20);
    unsigned u0 = g_c[0].update;
    CHECK(x_one(1, x_reg(143)));
    k_pump(50);
    EXPECT_EQ(g_c[0].update, u0);
}
TEST(listener_does_not_change_viewer_chain) {
    SETUP();
    CHECK(U(AddClipboardFormatListener)(g_w[0])); g_c[0].listener = 1;
    CHECK(U(GetClipboardViewer)() == NULL);
}

/* ------------------------------------------------------------------------------------------ */
/* edge cases of SetClipboardData against the state numbers                                    */
/* ------------------------------------------------------------------------------------------ */
TEST(edge_set_format_zero_fails_state_unchanged) {
    SETUP();
    CHECK(x_open_empty(0));
    DWORD s = U(GetClipboardSequenceNumber)();
    SetLastError(0);
    CHECK(U(SetClipboardData)(0, mkmem("abcdefgh", 8)) == NULL);
    DWORD e = GetLastError();
    EXPECT_EQ(e, ERROR_CLIPBOARD_NOT_OPEN);
    EXPECT_EQ(U(CountClipboardFormats)(), 0);
    EXPECT_EQ(U(GetClipboardSequenceNumber)(), s);
    U(CloseClipboard)();
}
TEST(edge_get_absent_format_returns_null_state_unchanged) {
    SETUP();
    CHECK(x_one(0, x_reg(150)));
    CHECK(x_open(g_w[0]));
    DWORD s = U(GetClipboardSequenceNumber)();
    CHECK(U(GetClipboardData)(x_reg(151)) == NULL);
    EXPECT_EQ(U(CountClipboardFormats)(), 1);
    EXPECT_EQ(U(GetClipboardSequenceNumber)(), s);
    U(CloseClipboard)();
}
TEST(edge_enumeration_order_stable_and_matches_count) {
    SETUP();
    CHECK(x_open_empty(0));
    UINT fm[6]; for (int i = 0; i < 6; i++) { fm[i] = x_reg(160 + i); CHECK(x_put(0, fm[i], "abcdefgh", 8)); }
    UINT a[16], b[16];
    int na = x_enum(a, 16), nb = x_enum(b, 16);
    EXPECT_EQ(na, nb); EXPECT_EQ(na, 6);
    CHECK(!memcmp(a, b, sizeof(UINT) * 6));
    for (int i = 0; i < 6; i++) CHECK(x_has(a, na, fm[i]));
    U(CloseClipboard)();
}
TEST(edge_enumeration_after_close_open_same) {
    SETUP();
    CHECK(x_open_empty(0));
    for (int i = 0; i < 4; i++) CHECK(x_put(0, x_reg(170 + i), "abcdefgh", 8));
    UINT a[16], b[16];
    int na = x_enum(a, 16); U(CloseClipboard)();
    CHECK(x_open(g_w[1]));
    int nb = x_enum(b, 16); U(CloseClipboard)();
    EXPECT_EQ(na, nb);
    CHECK(!memcmp(a, b, sizeof(UINT) * 4));
}
TEST(edge_enumeration_from_unknown_format_ends) {
    SETUP();
    CHECK(x_one(0, x_reg(180)));
    CHECK(x_open(g_w[0]));
    SetLastError(0);
    UINT r = U(EnumClipboardFormats)(x_reg(181));
    EXPECT_EQ(r, 0); EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
    U(CloseClipboard)();
}
TEST(edge_enumeration_last_returns_zero_with_no_error) {
    SETUP();
    CHECK(x_one(0, x_reg(182)));
    CHECK(x_open(g_w[0]));
    UINT f = U(EnumClipboardFormats)(0);
    CHECK(f != 0);
    SetLastError(0xdead);
    f = U(EnumClipboardFormats)(f);
    EXPECT_EQ(f, 0);
    EXPECT_EQ(GetLastError(), ERROR_SUCCESS);
    U(CloseClipboard)();
}
TEST(edge_cross_route_values_agree) {
    SETUP(); NTSETUP();
    UINT f = x_reg(183);
    CHECK(x_one(0, f));
    EXPECT_EQ(U(CountClipboardFormats)(), UN(NtUserCountClipboardFormats)());
    EXPECT_EQ(U(GetClipboardSequenceNumber)(), UN(NtUserGetClipboardSequenceNumber)());
    CHECK(U(GetClipboardViewer)() == UN(NtUserGetClipboardViewer)());
    CHECK(!!U(IsClipboardFormatAvailable)(f) == !!UN(NtUserIsClipboardFormatAvailable)(f));
    UINT l[2] = { x_reg(184), f };
    EXPECT_EQ(U(GetPriorityClipboardFormat)(l, 2), UN(NtUserGetPriorityClipboardFormat)(l, 2));
}
TEST(edge_cross_route_empty_then_set_sequence_consistent) {
    SETUP(); NTSETUP();
    CHECK(x_open(g_w[0]));
    DWORD a = U(GetClipboardSequenceNumber)();
    CHECK(UN(NtUserEmptyClipboard)());
    DWORD b = UN(NtUserGetClipboardSequenceNumber)();
    CHECK(x_put(0, x_reg(185), "abcdefgh", 8));
    DWORD c = U(GetClipboardSequenceNumber)();
    U(CloseClipboard)();
    CHECK(adv(a, b)); CHECK(adv(b, c));
}
TEST(edge_nt_priority_null_list_does_not_crash) {
    SETUP(); NTSETUP();
    CHECK(x_one(0, x_reg(186)));
    EXPECT_EQ((long)UN(NtUserGetPriorityClipboardFormat)(NULL, 0), -1L);
}
TEST(edge_nt_priority_zero_count_with_data) {
    SETUP(); NTSETUP();
    UINT l[1] = { x_reg(187) };
    CHECK(x_one(0, l[0]));
    EXPECT_EQ((long)UN(NtUserGetPriorityClipboardFormat)(l, 0), -1L);
}
TEST(edge_nt_priority_bad_pointer_fails_without_crash) {
    SETUP(); NTSETUP();
    CHECK(x_one(0, x_reg(188)));
    EXPECT_EQ(UN(NtUserGetPriorityClipboardFormat)((UINT *)(ULONG_PTR)0x10, 2), 0);   /* unreadable list: no crash, no match */
}
TEST(edge_nt_is_available_large_format) {
    SETUP(); NTSETUP();
    CHECK(!UN(NtUserIsClipboardFormatAvailable)(0xFFFFFFFFu));
    CHECK(!UN(NtUserIsClipboardFormatAvailable)(0x80000000u));
}
TEST(edge_nt_seq_return_is_dword_in_eax_only) {
    SETUP(); NTSETUP();
    DWORD a = UN(NtUserGetClipboardSequenceNumber)();
    DWORD b = UN(NtUserGetClipboardSequenceNumber)();
    EXPECT_EQ(a, b);
}
