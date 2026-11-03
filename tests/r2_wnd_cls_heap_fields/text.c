/* r2_wnd_cls_heap_fields: window caption (tagWND.strName) as the client view of the desktop heap shows it.
 *
 * user32 RealDefWindowProcWorker (WM_GETTEXT / WM_GETTEXTLENGTH) reads strName as a LARGE_UNICODE_STRING:
 * Length (bytes) at +0xb8, MaximumLength|bAnsi at +0xbc, Buffer (kernel pointer, rebased with the same
 * pSelf fixup as the window links) at +0xc0; the character count is Length >> 1 and an empty name has
 * Length 0. The kernel stores the text as UTF-16 in every case (WCSToMBEx converts on the way out). */
#define R2H_TAG L"txt"
#include "r2h.h"

/* Reads strName of w; returns false if the buffer cannot be followed. */
static bool txt_raw(const struct wv *w, wchar_t *out, size_t cap, uint32_t *len_bytes) {
    uint32_t len = W32(w, W_STRLEN);
    *len_bytes = len;
    out[0] = 0;
    if (!len) return true;
    if (len / 2 >= cap) return false;
    uint64_t buf = W64(w, W_STRBUF);
    if (!buf) return false;
    if (!wv_read_at(wv_fix(w, buf), out, len)) return false;
    out[len / 2] = 0;
    return true;
}
/* Fresh view of h and its raw text. */
static bool txt_of(HWND h, wchar_t *out, size_t cap, uint32_t *len, struct wv *w) {
    return wv_get(h, w) && txt_raw(w, out, cap, len);
}

static HWND mk_titled(const wchar_t *title) {
    if (!plain_class()) return NULL;
    return wk_add(U(CreateWindowExW)(EX_GUEST, plain_class(), title, WS_POPUP, -30000, -30000, 60, 40, NULL, NULL, g_hinst(), NULL));
}

static void title_case(const wchar_t *t) {
    T_BEGIN();
    HWND h = mk_titled(t);
    NEED_WND(h, w);
    wchar_t raw[1024], api[1024];
    uint32_t len;
    CHECK(txt_raw(&w, raw, 1024, &len));
    size_t n = wcslen(t);
    EXPECT_EQ(len, n * 2);                       /* Length counts bytes of UTF-16 without the terminator */
    CHECK(!wmemcmp(raw, t, n));
    CHECK((W32(&w, W_STRMAX) & 0x7fffffff) >= len);   /* MaximumLength is a 31-bit field, bit 31 is bAnsi */
    EXPECT_EQ((size_t)U(GetWindowTextLengthW)(h), n);
    SetLastError(0xdead);
    int got = U(GetWindowTextW)(h, api, 1024);
    EXPECT_EQ((size_t)got, n);
    CHECK(!wcscmp(api, t));
    END();
}
#define TT(id, str) TEST(r2_wnd_cls_heap_fields_text_create_##id) { title_case(str); }
TT(empty, L"")
TT(one, L"a")
TT(word, L"hello")
TT(spaces, L"Window Title With  Spaces ")
TT(latin1, L"caf\x00e9 \x00fc\x00df")
TT(cjk, L"\x4e2d\x6587\x6807\x9898")
TT(surrogate_pair, L"x\xD83D\xDE00y")
TT(backslash_quote, L"C:\\dir\\\"q\" & <t>")
TT(digits, L"0123456789")
TT(long64, L"0123456789012345678901234567890123456789012345678901234567890123")
TT(long300, L"0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
            L"0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789"
            L"0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789")

TEST(r2_wnd_cls_heap_fields_text_settext_updates_raw) {
    T_BEGIN();
    HWND h = mk_titled(L"first");
    NEED_WND(h, w0);
    static const wchar_t *seq[] = { L"second title", L"x", L"", L"a much longer replacement caption than before, forcing growth", L"short" };
    for (size_t i = 0; i < sizeof seq / sizeof *seq; i++) {
        CHECK(U(SetWindowTextW)(h, seq[i]));
        struct wv w; wchar_t raw[512]; uint32_t len;
        CHECK(txt_of(h, raw, 512, &len, &w));
        EXPECT_EQ(len, wcslen(seq[i]) * 2);
        CHECK(!wcscmp(raw, seq[i]));
        EXPECT_EQ((size_t)U(GetWindowTextLengthW)(h), wcslen(seq[i]));
    }
    END();
}
TEST(r2_wnd_cls_heap_fields_text_settext_null_clears) {
    T_BEGIN();
    HWND h = mk_titled(L"to be cleared");
    NEED_WND(h, w0);
    EXPECT_EQ(W32(&w0, W_STRLEN), 26);
    CHECK(U(SetWindowTextW)(h, NULL));
    struct wv w; wchar_t raw[64]; uint32_t len;
    CHECK(txt_of(h, raw, 64, &len, &w));
    EXPECT_EQ(len, 0);                          /* Length 0 is the empty name for WM_GETTEXT */
    EXPECT_EQ(U(GetWindowTextLengthW)(h), 0);
    wchar_t b[8] = L"zzzz";
    EXPECT_EQ(U(GetWindowTextW)(h, b, 8), 0);
    EXPECT_EQ(b[0], 0);
    END();
}
TEST(r2_wnd_cls_heap_fields_text_settexta_stored_as_utf16) {
    T_BEGIN();
    HWND h = mk_titled(L"");
    NEED_WND(h, w0);
    CHECK(U(SetWindowTextA)(h, "ansi text"));
    struct wv w; wchar_t raw[64]; uint32_t len;
    CHECK(txt_of(h, raw, 64, &len, &w));
    EXPECT_EQ(len, 18);
    CHECK(!wcscmp(raw, L"ansi text"));
    END();
}
TEST(r2_wnd_cls_heap_fields_text_createwindowexa_title) {
    T_BEGIN();
    if (!plain_class()) { t_skip("class"); return; }
    HWND h = wk_add(U(CreateWindowExA)(EX_GUEST, "win32kt_r2h_nonexistent_zz", "x", WS_POPUP, 0, 0, 5, 5, NULL, NULL, g_hinst(), NULL));
    CHECK(h == NULL);                           /* unknown class: nothing is created */
    EXPECT_ERR(ERROR_CANNOT_FIND_WND_CLASS);
    char cn[80];
    WideCharToMultiByte(CP_ACP, 0, plain_class(), -1, cn, sizeof cn, NULL, NULL);
    h = wk_add(U(CreateWindowExA)(EX_GUEST, cn, "created by A", WS_POPUP, -30000, -30000, 20, 20, NULL, NULL, g_hinst(), NULL));
    NEED_WND(h, w);
    wchar_t raw[64]; uint32_t len;
    CHECK(txt_raw(&w, raw, 64, &len));
    EXPECT_EQ(len, 24);
    CHECK(!wcscmp(raw, L"created by A"));
    END();
}
TEST(r2_wnd_cls_heap_fields_text_wm_settext_defproc_stores) {
    T_BEGIN();
    HWND h = mk_titled(L"a");
    NEED_WND(h, w0);
    EXPECT_EQ(U(SendMessageW)(h, WM_SETTEXT, 0, (LPARAM)L"sent"), TRUE);
    struct wv w; wchar_t raw[16]; uint32_t len;
    CHECK(txt_of(h, raw, 16, &len, &w));
    EXPECT_EQ(len, 8);
    CHECK(!wcscmp(raw, L"sent"));
    wchar_t b[16];
    EXPECT_EQ(U(SendMessageW)(h, WM_GETTEXTLENGTH, 0, 0), 4);
    EXPECT_EQ(U(SendMessageW)(h, WM_GETTEXT, 16, (LPARAM)b), 4);
    CHECK(!wcscmp(b, L"sent"));
    END();
}

/* A class procedure that swallows WM_SETTEXT never reaches DefWindowProc, which is the code that hands the
 * text to the kernel, so the stored name does not change. */
static LRESULT CALLBACK swallow_wp(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_SETTEXT) return TRUE;
    return DefWindowProcW(h, m, wp, lp);
}
TEST(r2_wnd_cls_heap_fields_text_swallowed_settext_keeps_raw) {
    T_BEGIN();
    wchar_t cn[64];
    if (!reg_class(cn, 0, 0, 0, swallow_wp, FALSE, NULL, NULL)) { t_skip("register failed"); return; }
    HWND h = mk_top(cn, WS_POPUP, 0, -30000, -30000, 20, 20, NULL);
    NEED_WND(h, w0);
    CHECK(U(SetWindowTextW)(h, L"dropped"));
    struct wv w; wchar_t raw[16]; uint32_t len;
    CHECK(txt_of(h, raw, 16, &len, &w));
    EXPECT_EQ(len, 6);                          /* still the creation title "r2h" */
    CHECK(!wcscmp(raw, L"r2h"));
    END();
}
TEST(r2_wnd_cls_heap_fields_text_gettext_truncates) {
    T_BEGIN();
    HWND h = mk_titled(L"abcdef");
    NEED_WND(h, w);
    wchar_t b[16];
    wmemset(b, 0x7777, 16);
    /* WM_GETTEXT copies min(chars, nMax - 1) characters and a terminator */
    EXPECT_EQ(U(GetWindowTextW)(h, b, 4), 3);
    CHECK(!wcscmp(b, L"abc"));
    EXPECT_EQ(b[4], 0x7777);
    wmemset(b, 0x7777, 16);
    EXPECT_EQ(U(GetWindowTextW)(h, b, 1), 0);
    EXPECT_EQ(b[0], 0);
    EXPECT_EQ(U(GetWindowTextW)(h, b, 7), 6);
    CHECK(!wcscmp(b, L"abcdef"));
    wmemset(b, 0x7777, 16);
    EXPECT_EQ(U(GetWindowTextW)(h, b, 0), 0);   /* nMaxCount 0: nothing is written */
    EXPECT_EQ(b[0], 0x7777);
    END();
}
TEST(r2_wnd_cls_heap_fields_text_gettexta_converts) {
    T_BEGIN();
    HWND h = mk_titled(L"abcdez");
    NEED_WND(h, w);
    char b[16];
    memset(b, 0x55, sizeof b);
    int n = U(GetWindowTextA)(h, b, 16);
    EXPECT_EQ(n, 6);                            /* ASCII converts one byte per character in every ANSI code page */
    CHECK(!strcmp(b, "abcdez"));
    EXPECT_EQ(U(GetWindowTextLengthA)(h), 6);
    END();
}
TEST(r2_wnd_cls_heap_fields_text_buffers_are_distinct) {
    T_BEGIN();
    HWND a = mk_titled(L"same");
    HWND b = mk_titled(L"same");
    NEED_WND(a, wa);
    NEED_WND(b, wb);
    uint64_t pa = wv_fix(&wa, W64(&wa, W_STRBUF)), pb = wv_fix(&wb, W64(&wb, W_STRBUF));
    CHECK(pa && pb);
    CHECK(pa != pb);
    /* changing one name leaves the other alone */
    CHECK(U(SetWindowTextW)(a, L"changed"));
    struct wv w2; wchar_t raw[16]; uint32_t len;
    CHECK(txt_of(b, raw, 16, &len, &w2));
    CHECK(!wcscmp(raw, L"same"));
    END();
}
TEST(r2_wnd_cls_heap_fields_text_buffer_in_desktop_heap) {
    T_BEGIN();
    HWND h = mk_titled(L"inside the heap");
    NEED_WND(h, w);
    uint64_t buf = wv_fix(&w, W64(&w, W_STRBUF));
    CHECK(buf != 0);
    CHECK(same_view(w.pc, buf));                /* same mapping as the window itself */
    MEMORY_BASIC_INFORMATION m;
    CHECK(vq(buf, &m));
    CHECK(m.Protect == PAGE_READONLY || m.Protect == PAGE_READWRITE);
    END();
}
TEST(r2_wnd_cls_heap_fields_text_empty_title_len_zero) {
    T_BEGIN();
    HWND h = mk_titled(L"");
    NEED_WND(h, w);
    EXPECT_EQ(W32(&w, W_STRLEN), 0);
    EXPECT_EQ(U(GetWindowTextLengthW)(h), 0);
    END();
}
TEST(r2_wnd_cls_heap_fields_text_null_title_len_zero) {
    T_BEGIN();
    if (!plain_class()) { t_skip("class"); return; }
    HWND h = wk_add(U(CreateWindowExW)(EX_GUEST, plain_class(), NULL, WS_POPUP, -30000, -30000, 10, 10, NULL, NULL, g_hinst(), NULL));
    NEED_WND(h, w);
    EXPECT_EQ(W32(&w, W_STRLEN), 0);
    END();
}
TEST(r2_wnd_cls_heap_fields_text_child_window) {
    T_BEGIN();
    HWND host = mk_host();
    HWND c = U(CreateWindowExW) ? wk_add(U(CreateWindowExW)(0, plain_class(), L"child cap", WS_CHILD, 1, 1, 20, 20, host, (HMENU)5, g_hinst(), NULL)) : NULL;
    NEED_WND(c, w);
    wchar_t raw[32]; uint32_t len;
    CHECK(txt_raw(&w, raw, 32, &len));
    EXPECT_EQ(len, 18);
    CHECK(!wcscmp(raw, L"child cap"));
    END();
}
static void sys_text_case(const wchar_t *cls, const wchar_t *cap, DWORD style, bool must) {
    T_BEGIN();
    HWND host = mk_host();
    HWND c = host ? wk_add(U(CreateWindowExW)(0, cls, cap, style | WS_CHILD, 1, 1, 60, 30, host, (HMENU)7, g_hinst(), NULL)) : NULL;
    NEED_WND(c, w);
    wchar_t raw[64]; uint32_t len;
    CHECK(txt_raw(&w, raw, 64, &len));
    if (must) {
        EXPECT_EQ(len, wcslen(cap) * 2);
        CHECK(!wcscmp(raw, cap));
    } else if (len != wcslen(cap) * 2) {
        t_note("%ls: strName length %lu, caption has %zu bytes", cls, (unsigned long)len, wcslen(cap) * 2);
    }
    END();
}
/* Static and Button hand WM_SETTEXT to DefWindowProc, so the caption lives in strName. */
TEST(r2_wnd_cls_heap_fields_text_sysclass_static) { sys_text_case(L"Static", L"static caption", SS_LEFT, true); }
TEST(r2_wnd_cls_heap_fields_text_sysclass_button) { sys_text_case(L"Button", L"push me", BS_PUSHBUTTON, true); }
TEST(r2_wnd_cls_heap_fields_text_sysclass_edit_note) { sys_text_case(L"Edit", L"edit text", ES_LEFT, false); }
TEST(r2_wnd_cls_heap_fields_text_sysclass_combobox_note) { sys_text_case(L"ComboBox", L"combo", CBS_DROPDOWN, false); }

TEST(r2_wnd_cls_heap_fields_text_cross_thread_settext) {
    T_BEGIN();
    if (!plain_class()) { t_skip("class"); return; }
    struct xt x;
    if (!xt_start(&x, plain_class())) { xt_stop(&x); t_skip("helper thread window failed"); return; }
    NEED_WND(x.hwnd, w0);
    CHECK(U(SetWindowTextW)(x.hwnd, L"set from other thread"));   /* SendMessage to the owner thread, which pumps */
    struct wv w; wchar_t raw[64]; uint32_t len;
    CHECK(txt_of(x.hwnd, raw, 64, &len, &w));
    EXPECT_EQ(len, 42);
    CHECK(!wcscmp(raw, L"set from other thread"));
    xt_stop(&x);
    END();
}
TEST(r2_wnd_cls_heap_fields_text_owner_destroy_text_unreadable) {
    T_BEGIN();
    HWND h = mk_titled(L"will die");
    NEED_WND(h, w);
    CHECK(U(DestroyWindow)(h));
    CHECK(!U(IsWindow)(h));
    SetLastError(0);
    EXPECT_EQ(U(GetWindowTextLengthW)(h), 0);
    wchar_t b[8] = L"q";
    EXPECT_EQ(U(GetWindowTextW)(h, b, 8), 0);
    END();
}
TEST(r2_wnd_cls_heap_fields_text_desktop_window_note) {
    T_BEGIN();
    HWND d = U(GetDesktopWindow)();
    NEED_WND(d, w);
    wchar_t raw[64]; uint32_t len;
    if (txt_raw(&w, raw, 64, &len)) t_note("desktop window strName length %lu", (unsigned long)len);
    CHECK(len % 2 == 0);                        /* UTF-16 bytes */
    END();
}
