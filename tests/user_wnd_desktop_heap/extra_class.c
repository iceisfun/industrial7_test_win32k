/* user_wnd_desktop_heap: window extra bytes (cbWndExtra), the class structure the window points at
 * (tagCLS, cls = window + (pcls - *(window+8))), class extra bytes, and the SetWindowLong/SetClassLong
 * syscalls that change them. */
#define WDH_TAG L"x"
#include "wdh.h"
#include <limits.h>

UDECL(ULONG_PTR, NtUserSetWindowLongPtr, (HWND, int, ULONG_PTR, int));
UDECL(DWORD, NtUserSetWindowLong, (HWND, int, DWORD, int));

/* address of extra byte `idx` as _GetWindowLongPtr computes it: *(wnd+0x128) + (idx - *(wnd+0xfc)) */
static uint64_t extra_addr(const struct wv *w, int idx) { return W64(w, W_EXTRAPTR) + (int64_t)(idx - (int32_t)W32(w, W_EXTRABASE)); }
static bool extra_q(const struct wv *w, int idx, uint64_t *v) { return wv_read_at(extra_addr(w, idx), v, 8); }

/* ---- window extra bytes ---------------------------------------------------------------------------- */

TEST(user_wnd_desktop_heap_extra_pointer_present) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    /* _GetWindowLongPtr dereferences *(wnd+0x128) for every non-negative index */
    CHECK(W64(&w, W_EXTRAPTR) != 0);
    uint64_t v;
    CHECK(extra_q(&w, 0, &v));
    t_note("cbWndExtra word=%lu extra base=%lu extra ptr=0x%llx (same view as heap: %d)", (unsigned long)W32(&w, W_CBEXTRA),
           (unsigned long)W32(&w, W_EXTRABASE), (unsigned long long)W64(&w, W_EXTRAPTR), same_view(w.pc, W64(&w, W_EXTRAPTR)));
    END();
}
TEST(user_wnd_desktop_heap_extra_zero_initialised) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    unsigned char buf[PLAIN_CBWND];
    CHECK(wv_read_at(extra_addr(&w, 0), buf, sizeof buf));
    for (int i = 0; i < PLAIN_CBWND; i++) if (buf[i]) { CHECKF(0, "extra byte %d = 0x%02x", i, buf[i]); break; }
    for (int i = 0; i < PLAIN_CBWND; i += 8) EXPECT_EQ((ULONG_PTR)U(GetWindowLongPtrW)(h, i), 0);
    END();
}
TEST(user_wnd_desktop_heap_extra_cbwndextra_matches_class) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    uint32_t cb = W32(&w, W_CBEXTRA), ib = W32(&w, W_EXTRABASE);
    /* the class-level value is what RegisterClass asked for; the window words are only observed */
    t_note("window cbWndExtra word=%lu, first valid index=%lu (class value %d)", (unsigned long)cb, (unsigned long)ib, PLAIN_CBWND);
    EXPECT_EQ((ULONG)U(GetClassLongW)(h, GCL_CBWNDEXTRA), PLAIN_CBWND);
    END();
}
static const int k_qidx[] = { 0, 8, 16, 24, 32, 40, 48, 56 };
TEST(user_wnd_desktop_heap_extra_qword_roundtrip_table) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    for (size_t i = 0; i < sizeof k_qidx / sizeof *k_qidx; i++) {
        int idx = k_qidx[i];
        ULONG_PTR val = (ULONG_PTR)0xA5A5000000000000ULL + 0x1111 * (i + 1);
        SET_ERR(0);
        EXPECT_EQ((ULONG_PTR)U(SetWindowLongPtrW)(h, idx, (LONG_PTR)val), 0);
        EXPECT_ERR(0);
        EXPECT_EQ((ULONG_PTR)U(GetWindowLongPtrW)(h, idx), val);
        uint64_t raw = 0;
        CHECK(extra_q(&w, idx, &raw));   /* same-thread read: *(i64*)(*(wnd+0x128) + idx - base) */
        EXPECT_EQ(raw, val);
    }
    END();
}
TEST(user_wnd_desktop_heap_extra_set_returns_previous_value) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    U(SetWindowLongPtrW)(h, 8, 0x1111);
    EXPECT_EQ((ULONG_PTR)U(SetWindowLongPtrW)(h, 8, 0x2222), 0x1111);
    EXPECT_EQ((ULONG_PTR)U(SetWindowLongPtrW)(h, 8, 0x3333), 0x2222);
    uint64_t raw = 0;
    CHECK(extra_q(&w, 8, &raw));
    EXPECT_EQ(raw, 0x3333);
    END();
}
TEST(user_wnd_desktop_heap_extra_unaligned_indices) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    int idx[] = { 1, 3, 5, 13, 41 };
    for (size_t i = 0; i < sizeof idx / sizeof *idx; i++) {
        SET_ERR(0);
        U(SetWindowLongPtrW)(h, idx[i], 0x0102030405060708LL);
        EXPECT_ERR(0);
        EXPECT_EQ((ULONG_PTR)U(GetWindowLongPtrW)(h, idx[i]), 0x0102030405060708ULL);
        unsigned char b[8];
        CHECK(wv_read_at(extra_addr(&w, idx[i]), b, 8));
        EXPECT_EQ(b[0], 0x08);
        EXPECT_EQ(b[7], 0x01);
        U(SetWindowLongPtrW)(h, idx[i], 0);
    }
    END();
}
TEST(user_wnd_desktop_heap_extra_dword_halves) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    U(SetWindowLongPtrW)(h, 16, (LONG_PTR)0x1111222233334444ULL);
    SET_ERR(0);
    EXPECT_EQ((uint32_t)U(SetWindowLongW)(h, 20, 0x55556666), 0x11112222u);   /* previous value of the high dword */
    EXPECT_ERR(0);
    uint64_t raw = 0;
    CHECK(extra_q(&w, 16, &raw));
    EXPECT_EQ(raw, 0x5555666633334444ULL);   /* little endian: index 20 is the high half */
    EXPECT_EQ((uint32_t)U(GetWindowLongW)(h, 16), 0x33334444u);
    EXPECT_EQ((uint32_t)U(GetWindowLongW)(h, 20), 0x55556666u);
    END();
}
TEST(user_wnd_desktop_heap_extra_bounds_ptr_version) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    SET_ERR(0);
    U(SetWindowLongPtrW)(h, PLAIN_CBWND - 8, 0x99);
    EXPECT_ERR(0);
    EXPECT_EQ((ULONG_PTR)U(GetWindowLongPtrW)(h, PLAIN_CBWND - 8), 0x99);
    int bad[] = { PLAIN_CBWND - 7, PLAIN_CBWND - 4, PLAIN_CBWND, PLAIN_CBWND + 8, 1000, 0x10000 };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        SET_ERR(0);
        CHECKF(U(GetWindowLongPtrW)(h, bad[i]) == 0, "get %d", bad[i]);
        EXPECT_ERR(ERROR_INVALID_INDEX);
        SET_ERR(0);
        CHECKF(U(SetWindowLongPtrW)(h, bad[i], 5) == 0, "set %d", bad[i]);
        EXPECT_ERR(ERROR_INVALID_INDEX);
    }
    END();
}
TEST(user_wnd_desktop_heap_extra_bounds_dword_version) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    SET_ERR(0);
    U(SetWindowLongW)(h, PLAIN_CBWND - 4, 0x77);
    EXPECT_ERR(0);
    EXPECT_EQ((uint32_t)U(GetWindowLongW)(h, PLAIN_CBWND - 4), 0x77);
    int bad[] = { PLAIN_CBWND - 3, PLAIN_CBWND, PLAIN_CBWND + 4, 4096 };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        SET_ERR(0);
        CHECKF(U(GetWindowLongW)(h, bad[i]) == 0, "get %d", bad[i]);
        EXPECT_ERR(ERROR_INVALID_INDEX);
    }
    END();
}
TEST(user_wnd_desktop_heap_extra_invalid_negative_indices) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    /* GetWindowData has no case for these: default -> ERROR_INVALID_INDEX */
    int bad[] = { -3, -5, -7, -9, -10, -11, -13, -14, -15, -17, -18, -19, -22, -23, -100, INT_MIN };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        SET_ERR(0);
        CHECKF(U(GetWindowLongPtrW)(h, bad[i]) == 0, "index %d", bad[i]);
        EXPECT_ERR(ERROR_INVALID_INDEX);
    }
    END();
}
TEST(user_wnd_desktop_heap_extra_dword_getter_rejects_pointer_indices) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    /* GWL_WNDPROC / GWL_HINSTANCE / GWL_HWNDPARENT are not in the 32-bit getter's accepted set (0x223 mask) */
    int bad[] = { GWLP_WNDPROC, GWLP_HINSTANCE, GWLP_HWNDPARENT };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        SET_ERR(0);
        CHECKF(U(GetWindowLongW)(h, bad[i]) == 0, "index %d", bad[i]);
        EXPECT_ERR(ERROR_INVALID_INDEX);
    }
    int ok[] = { GWL_STYLE, GWL_EXSTYLE, GWLP_USERDATA, GWLP_ID };
    for (size_t i = 0; i < sizeof ok / sizeof *ok; i++) {
        SET_ERR(0);
        U(GetWindowLongW)(h, ok[i]);
        EXPECT_ERR(0);
    }
    END();
}
TEST(user_wnd_desktop_heap_extra_windows_do_not_share_bytes) {
    T_BEGIN();
    HWND a = mk_plain();
    HWND b = mk_plain();
    NEED_WND(a, wa);
    struct wv wb;
    CHECK(wv_get(b, &wb));
    U(SetWindowLongPtrW)(a, 0, 0xAAAA);
    EXPECT_EQ((ULONG_PTR)U(GetWindowLongPtrW)(b, 0), 0);
    U(SetWindowLongPtrW)(b, 0, 0xBBBB);
    EXPECT_EQ((ULONG_PTR)U(GetWindowLongPtrW)(a, 0), 0xAAAA);
    uint64_t pa = W64(&wa, W_EXTRAPTR), pb = W64(&wb, W_EXTRAPTR);
    CHECK(pa != pb);
    CHECK(pa + PLAIN_CBWND <= pb || pb + PLAIN_CBWND <= pa);   /* the two blocks do not overlap */
    END();
}
TEST(user_wnd_desktop_heap_extra_invalid_window_error) {
    T_BEGIN();
    HWND h = mk_plain();
    if (!h) { t_skip("window creation failed"); return; }
    HWND s = h;
    U(DestroyWindow)(h);
    SET_ERR(0);
    CHECK(U(SetWindowLongPtrW)(s, 0, 1) == 0);
    EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
    SET_ERR(0);
    CHECK(U(GetWindowLongPtrW)(s, 0) == 0);
    EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
}
TEST(user_wnd_desktop_heap_extra_window_without_extra_bytes) {
    T_BEGIN();
    wchar_t cn[64];
    if (!reg_class(cn, 0, 0, 0, wdh_wp2, FALSE, NULL, NULL)) { t_skip("register failed"); return; }
    HWND h = mk_top(cn, WS_POPUP, 0, -30000, -30000, 10, 10, NULL);
    NEED_WND(h, w);
    SET_ERR(0);
    CHECK(U(GetWindowLongPtrW)(h, 0) == 0);
    EXPECT_ERR(ERROR_INVALID_INDEX);   /* cbWndExtra == 0: no valid index */
    SET_ERR(0);
    CHECK(U(SetWindowLongPtrW)(h, 0, 3) == 0);
    EXPECT_ERR(ERROR_INVALID_INDEX);
    t_note("cbWndExtra word=%lu base=%lu", (unsigned long)W32(&w, W_CBEXTRA), (unsigned long)W32(&w, W_EXTRABASE));
    END();
}
TEST(user_wnd_desktop_heap_extra_cross_thread_set_get) {
    T_BEGIN();
    if (!plain_class()) { t_skip("class registration failed"); return; }
    struct xt x;
    if (!xt_start(&x, plain_class())) { xt_stop(&x); t_skip("helper thread window failed"); return; }
    NEED_WND(x.hwnd, w);
    /* the owner is another thread, so _GetWindowLongPtr goes through NtUserMessageCall(0x5a) */
    SET_ERR(0);
    EXPECT_EQ((ULONG_PTR)U(SetWindowLongPtrW)(x.hwnd, 8, 0xC0FFEE), 0);
    EXPECT_EQ((ULONG_PTR)U(GetWindowLongPtrW)(x.hwnd, 8), 0xC0FFEE);
    uint64_t raw = 0;
    if (extra_q(&w, 8, &raw)) EXPECT_EQ(raw, 0xC0FFEE);
    else t_note("extra bytes of a foreign-thread window are not readable here");
    EXPECT_EQ((ULONG_PTR)U(SetWindowLongPtrW)(x.hwnd, 8, 0xBEEF), 0xC0FFEE);
    xt_stop(&x);
    END();
}
TEST(user_wnd_desktop_heap_extra_cross_thread_userdata_visible_in_raw) {
    T_BEGIN();
    if (!plain_class()) { t_skip("class registration failed"); return; }
    struct xt x;
    if (!xt_start(&x, plain_class())) { xt_stop(&x); t_skip("helper thread window failed"); return; }
    NEED_WND(x.hwnd, w);
    U(SetWindowLongPtrW)(x.hwnd, GWLP_USERDATA, 0x1357);
    struct wv w2;
    CHECK(wv_get(x.hwnd, &w2));
    EXPECT_EQ(W64(&w2, W_USERDATA), 0x1357);   /* the kernel updated the shared structure, no message pump needed */
    xt_stop(&x);
    END();
}

/* ---- direct NtUserSetWindowLong* -------------------------------------------------------------------------- */

TEST(user_wnd_desktop_heap_sys_setwindowlongptr_userdata) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    REQUIRE(UN(NtUserSetWindowLongPtr));
    /* user32!_SetWindowLongPtr forwards (hwnd, index, value, ansiFlag) */
    EXPECT_EQ(UN(NtUserSetWindowLongPtr)(h, GWLP_USERDATA, 0x42, 0), 0);
    EXPECT_EQ(UN(NtUserSetWindowLongPtr)(h, GWLP_USERDATA, 0x43, 0), 0x42);
    CHECK(wv_get(h, &w));
    EXPECT_EQ(W64(&w, W_USERDATA), 0x43);
    END();
}
TEST(user_wnd_desktop_heap_sys_setwindowlongptr_extra_bytes) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    REQUIRE(UN(NtUserSetWindowLongPtr));
    EXPECT_EQ(UN(NtUserSetWindowLongPtr)(h, 24, 0xD00D, 0), 0);
    uint64_t raw = 0;
    CHECK(extra_q(&w, 24, &raw));
    EXPECT_EQ(raw, 0xD00D);
    EXPECT_EQ((ULONG_PTR)U(GetWindowLongPtrW)(h, 24), 0xD00D);
    END();
}
TEST(user_wnd_desktop_heap_sys_setwindowlongptr_invalid_handle) {
    T_BEGIN();
    REQUIRE(UN(NtUserSetWindowLongPtr));
    HWND h = mk_plain();
    if (!h) { t_skip("window creation failed"); return; }
    HWND s = h;
    U(DestroyWindow)(h);
    HWND bad[] = { NULL, s, (HWND)(ULONG_PTR)0xFFFFFFFF, (HWND)(ULONG_PTR)0x7FFE };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        SET_ERR(0);
        CHECKF(UN(NtUserSetWindowLongPtr)(bad[i], GWLP_USERDATA, 1, 0) == 0, "handle %p", bad[i]);
        if (GetLastError() != ERROR_INVALID_WINDOW_HANDLE) t_note("handle %p: last error %lu", bad[i], GetLastError());
    }
}
TEST(user_wnd_desktop_heap_sys_setwindowlongptr_bad_index) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    REQUIRE(UN(NtUserSetWindowLongPtr));
    int bad[] = { PLAIN_CBWND, PLAIN_CBWND - 4, -3, -5, -7, 0x7fffffff };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        SET_ERR(0);
        CHECKF(UN(NtUserSetWindowLongPtr)(h, bad[i], 1, 0) == 0, "index %d", bad[i]);
        if (GetLastError() != ERROR_INVALID_INDEX) t_note("index %d: last error %lu", bad[i], GetLastError());
    }
    END();
}
TEST(user_wnd_desktop_heap_sys_setwindowlong_dword) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    REQUIRE(UN(NtUserSetWindowLong));
    EXPECT_EQ(UN(NtUserSetWindowLong)(h, 32, 0xABCD1234, 0), 0);
    EXPECT_EQ(UN(NtUserSetWindowLong)(h, 32, 0x1, 0), 0xABCD1234u);
    uint64_t raw = 0;
    CHECK(extra_q(&w, 32, &raw));
    EXPECT_EQ((uint32_t)raw, 1u);
    END();
}
TEST(user_wnd_desktop_heap_sys_setwindowlong_style_returns_old) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    REQUIRE(UN(NtUserSetWindowLong));
    DWORD old = (DWORD)U(GetWindowLongW)(h, GWL_STYLE);
    EXPECT_EQ(UN(NtUserSetWindowLong)(h, GWL_STYLE, old | WS_TABSTOP, 0), old);
    CHECK(wv_get(h, &w));
    CHECK((wv_style(&w) & WS_TABSTOP) != 0);
    UN(NtUserSetWindowLong)(h, GWL_STYLE, old, 0);
    END();
}

/* ---- class structure -------------------------------------------------------------------------------------- */

static LRESULT CALLBACK cls_wp(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }
static LRESULT CALLBACK cls_wpa(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcA(h, m, w, l); }

TEST(user_wnd_desktop_heap_cls_fields_match_registration) {
    T_BEGIN();
    wchar_t cn[64];
    HBRUSH hbr = (HBRUSH)(ULONG_PTR)(COLOR_BTNFACE + 1);
    UINT st = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    ATOM a = reg_class(cn, st, 24, 40, cls_wp, FALSE, hbr, U(LoadCursorW)(NULL, MAKEINTRESOURCEW(32512)));
    if (!a) { t_skip("register failed"); return; }
    HWND h = mk_top(cn, WS_POPUP, 0, -30000, -30000, 10, 10, NULL);
    NEED_WND(h, w);
    struct cv c;
    CHECK(cv_get(&w, &c));
    EXPECT_EQ(C16(&c, C_ATOM), a);             /* GCW_ATOM = word at cls+2 */
    EXPECT_EQ((ULONG_PTR)U(GetClassWord)(h, GCW_ATOM), a);
    EXPECT_EQ(C32(&c, C_CBCLS), 24);           /* GCL_CBCLSEXTRA = int at cls+0xc */
    EXPECT_EQ((ULONG)U(GetClassLongW)(h, GCL_CBCLSEXTRA), 24);
    EXPECT_EQ(C32(&c, C_CBWND0) + C32(&c, C_CBWND1), 40);   /* GCL_CBWNDEXTRA = [0x50] + [0x54] */
    EXPECT_EQ((ULONG)U(GetClassLongW)(h, GCL_CBWNDEXTRA), 40);
    EXPECT_EQ(C64(&c, C_HMODULE), (ULONG_PTR)g_hinst());
    EXPECT_EQ((ULONG_PTR)U(GetClassLongPtrW)(h, GCLP_HMODULE), (ULONG_PTR)g_hinst());
    EXPECT_EQ(C64(&c, C_HBR), (ULONG_PTR)hbr);
    EXPECT_EQ((ULONG_PTR)U(GetClassLongPtrW)(h, GCLP_HBRBACKGROUND), (ULONG_PTR)hbr);
    EXPECT_EQ(C64(&c, C_WNDPROC), (ULONG_PTR)cls_wp);
    EXPECT_EQ((ULONG_PTR)U(GetClassLongPtrW)(h, GCLP_WNDPROC), (ULONG_PTR)cls_wp);
    EXPECT_EQ((C16(&c, C_FLAGS) & 2), 0);      /* Unicode class: no CSF_ANSIPROC bit */
    END();
}
TEST(user_wnd_desktop_heap_cls_ansi_flag) {
    T_BEGIN();
    wchar_t cn[64];
    ATOM a = reg_class(cn, 0, 0, 0, cls_wpa, TRUE, NULL, NULL);
    if (!a) { t_skip("register failed"); return; }
    HWND h = mk_top(cn, WS_POPUP, 0, -30000, -30000, 10, 10, NULL);
    NEED_WND(h, w);
    struct cv c;
    CHECK(cv_get(&w, &c));
    CHECK((C16(&c, C_FLAGS) & 2) != 0);        /* _GetClassData(-0x18) compares the caller's ANSI-ness with bit 1 */
    EXPECT_EQ(C64(&c, C_WNDPROC), (ULONG_PTR)cls_wpa);
    EXPECT_EQ((ULONG_PTR)U(GetClassLongPtrA)(h, GCLP_WNDPROC), (ULONG_PTR)cls_wpa);
    EXPECT_EQ(C16(&c, C_ATOM), a);
    END();
}
TEST(user_wnd_desktop_heap_cls_cursor_api) {
    T_BEGIN();
    wchar_t cn[64];
    HCURSOR cur = U(LoadCursorW)(NULL, MAKEINTRESOURCEW(32513));   /* IDC_IBEAM */
    if (!reg_class(cn, 0, 0, 0, cls_wp, FALSE, NULL, cur)) { t_skip("register failed"); return; }
    HWND h = mk_top(cn, WS_POPUP, 0, -30000, -30000, 10, 10, NULL);
    NEED_WND(h, w);
    /* GCLP_HCURSOR goes through NtUserCallHwndParam(.., 0x5e): the kernel returns the cursor handle */
    EXPECT_EQ((ULONG_PTR)U(GetClassLongPtrW)(h, GCLP_HCURSOR), (ULONG_PTR)cur);
    CHECK(cur != NULL);
    END();
}
static void class_style_case(UINT st) {
    T_BEGIN();
    wchar_t cn[64];
    if (!reg_class(cn, st, 0, 0, cls_wp, FALSE, NULL, NULL)) { t_skip("register failed"); return; }
    HWND h = mk_top(cn, WS_POPUP, 0, -30000, -30000, 10, 10, NULL);
    NEED_WND(h, w);
    struct cv c;
    CHECK(cv_get(&w, &c));
    uint32_t raw = C32(&c, C_STYLE);
    uint32_t api = (uint32_t)U(GetClassLongW)(h, GCL_STYLE);
    EXPECT_EQ(api, raw & 0x37bffu);              /* _GetClassData(-0x1a) masks with 0x37bff */
    CHECKF((api & st) == (st & 0x37bffu), "registered 0x%lx api 0x%lx", (unsigned long)st, (unsigned long)api);
    if (raw & ~0x37bffu) t_note("class style bits hidden from the API: 0x%lx", (unsigned long)(raw & ~0x37bffu));
    END();
}
#define CS(n, v) TEST(user_wnd_desktop_heap_cls_style_##n) { class_style_case(v); }
CS(none, 0)
CS(hv_redraw, CS_HREDRAW | CS_VREDRAW)
CS(dblclks, CS_DBLCLKS)
CS(owndc, CS_OWNDC)
CS(classdc, CS_CLASSDC)
CS(parentdc, CS_PARENTDC)
CS(noclose, CS_NOCLOSE)
CS(savebits, CS_SAVEBITS)
CS(bytealign, CS_BYTEALIGNCLIENT | CS_BYTEALIGNWINDOW)
CS(global, CS_GLOBALCLASS)
CS(combo, CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS | CS_OWNDC | CS_NOCLOSE)

TEST(user_wnd_desktop_heap_cls_same_class_same_object) {
    T_BEGIN();
    HWND a = mk_plain();
    HWND b = mk_plain();
    NEED_WND(a, wa);
    struct wv wb;
    CHECK(wv_get(b, &wb));
    struct cv ca, cb;
    CHECK(cv_get(&wa, &ca) && cv_get(&wb, &cb));
    EXPECT_EQ(W64(&wa, W_PCLS), W64(&wb, W_PCLS));
    EXPECT_EQ(ca.pc, cb.pc);
    END();
}
TEST(user_wnd_desktop_heap_cls_different_class_different_object) {
    T_BEGIN();
    wchar_t cn[64];
    if (!reg_class(cn, 0, 0, 0, cls_wp, FALSE, NULL, NULL)) { t_skip("register failed"); return; }
    HWND a = mk_plain();
    HWND b = mk_top(cn, WS_POPUP, 0, -30000, -30000, 10, 10, NULL);
    NEED_WND(a, wa);
    struct wv wb;
    CHECK(wv_get(b, &wb));
    CHECK(W64(&wa, W_PCLS) != W64(&wb, W_PCLS));
    struct cv ca, cb;
    CHECK(cv_get(&wa, &ca) && cv_get(&wb, &cb));
    CHECK(C16(&ca, C_ATOM) != C16(&cb, C_ATOM));
    END();
}
TEST(user_wnd_desktop_heap_cls_pointer_is_rebased_in_heap) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    struct cv c;
    CHECK(cv_get(&w, &c));
    /* the class object lives in the desktop heap too: rebased with the window's own (pSelf, pc) pair */
    CHECK(same_view(w.pc, c.pc));
    CHECK(c.pc != w.pc);
    END();
}
TEST(user_wnd_desktop_heap_cls_extra_bytes_raw) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    struct cv c;
    CHECK(cv_get(&w, &c));
    SET_ERR(0);
    EXPECT_EQ((ULONG_PTR)U(SetClassLongPtrW)(h, 0, 0xCAFE), 0);
    EXPECT_ERR(0);
    EXPECT_EQ((ULONG_PTR)U(GetClassLongPtrW)(h, 0), 0xCAFE);
    /* _GetClassLongPtr: non-negative index -> *(cls + index + 0x58) */
    uint64_t raw = 0;
    CHECK(wv_read_at(c.pc + C_EXTRA, &raw, 8));
    EXPECT_EQ(raw, 0xCAFE);
    U(SetClassLongPtrW)(h, 8, 0xF00D);
    CHECK(wv_read_at(c.pc + C_EXTRA + 8, &raw, 8));
    EXPECT_EQ(raw, 0xF00D);
    END();
}
TEST(user_wnd_desktop_heap_cls_extra_bytes_shared_between_windows) {
    T_BEGIN();
    HWND a = mk_plain();
    HWND b = mk_plain();
    NEED_WND(a, wa);
    U(SetClassLongPtrW)(a, 16, 0x1234);
    EXPECT_EQ((ULONG_PTR)U(GetClassLongPtrW)(b, 16), 0x1234);
    struct wv wb;
    CHECK(wv_get(b, &wb));
    struct cv cb;
    CHECK(cv_get(&wb, &cb));
    uint64_t raw = 0;
    CHECK(wv_read_at(cb.pc + C_EXTRA + 16, &raw, 8));
    EXPECT_EQ(raw, 0x1234);
    END();
}
TEST(user_wnd_desktop_heap_cls_extra_bounds) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    SET_ERR(0);
    U(SetClassLongPtrW)(h, PLAIN_CBCLS - 8, 7);
    EXPECT_ERR(0);
    EXPECT_EQ((ULONG_PTR)U(GetClassLongPtrW)(h, PLAIN_CBCLS - 8), 7);
    int bad[] = { PLAIN_CBCLS - 7, PLAIN_CBCLS - 4, PLAIN_CBCLS, PLAIN_CBCLS + 1, 100000 };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        SET_ERR(0);
        CHECKF(U(GetClassLongPtrW)(h, bad[i]) == 0, "index %d", bad[i]);
        EXPECT_ERR(ERROR_INVALID_INDEX);
    }
    END();
}
TEST(user_wnd_desktop_heap_cls_extra_dword_getter_last_slot) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    U(SetClassLongW)(h, PLAIN_CBCLS - 4, 0x55);
    EXPECT_EQ((ULONG)U(GetClassLongW)(h, PLAIN_CBCLS - 4), 0x55);
    SET_ERR(0);
    CHECK(U(GetClassLongW)(h, PLAIN_CBCLS) == 0);
    EXPECT_ERR(ERROR_INVALID_INDEX);
    END();
}
TEST(user_wnd_desktop_heap_cls_invalid_negative_index) {
    T_BEGIN();
    HWND h = mk_plain();
    NEED_WND(h, w);
    /* _GetClassData: idx + 0x22 < 0 -> error 0x585 */
    int bad[] = { -35, -36, -100, INT_MIN };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        SET_ERR(0);
        CHECKF(U(GetClassLongPtrW)(h, bad[i]) == 0, "index %d", bad[i]);
        EXPECT_ERR(ERROR_INVALID_INDEX);
    }
    END();
}
TEST(user_wnd_desktop_heap_cls_class_name_and_atom) {
    T_BEGIN();
    wchar_t cn[64];
    ATOM a = reg_class(cn, 0, 0, 0, cls_wp, FALSE, NULL, NULL);
    if (!a) { t_skip("register failed"); return; }
    HWND h = mk_top(cn, WS_POPUP, 0, -30000, -30000, 10, 10, NULL);
    NEED_WND(h, w);
    wchar_t got[64] = L"";
    int n = U(GetClassNameW)(h, got, 64);
    EXPECT_EQ(n, (int)wcslen(cn));
    EXPECT_WSTR(got, cn);
    struct cv c;
    CHECK(cv_get(&w, &c));
    EXPECT_EQ(C16(&c, C_ATOM), a);
    END();
}
TEST(user_wnd_desktop_heap_cls_getclassinfo_agrees_with_raw) {
    T_BEGIN();
    wchar_t cn[64];
    if (!reg_class(cn, CS_DBLCLKS, 8, 16, cls_wp, FALSE, (HBRUSH)(ULONG_PTR)(COLOR_WINDOW + 1), NULL)) { t_skip("register failed"); return; }
    HWND h = mk_top(cn, WS_POPUP, 0, -30000, -30000, 10, 10, NULL);
    NEED_WND(h, w);
    WNDCLASSEXW wc; memset(&wc, 0, sizeof wc); wc.cbSize = sizeof wc;
    CHECK(U(GetClassInfoExW)(g_hinst(), cn, &wc) != 0);
    struct cv c;
    CHECK(cv_get(&w, &c));
    EXPECT_EQ(wc.cbClsExtra, C32(&c, C_CBCLS));
    EXPECT_EQ(wc.cbWndExtra, C32(&c, C_CBWND0) + C32(&c, C_CBWND1));
    EXPECT_EQ((ULONG_PTR)wc.lpfnWndProc, C64(&c, C_WNDPROC));
    EXPECT_EQ((ULONG_PTR)wc.hbrBackground, C64(&c, C_HBR));
    EXPECT_EQ(wc.style & 0x37bff, C32(&c, C_STYLE) & 0x37bff);
    END();
}
TEST(user_wnd_desktop_heap_cls_system_button_fields) {
    T_BEGIN();
    HWND p = mk_host();
    HWND b = mk_child(p, L"Button", WS_CHILD | BS_PUSHBUTTON, 0, 0, 30, 20, 1);
    NEED_WND(b, w);
    struct cv c;
    CHECK(cv_get(&w, &c));
    wchar_t nm[32] = L"";
    U(GetClassNameW)(b, nm, 32);
    EXPECT_WSTR(nm, L"Button");
    EXPECT_EQ(C16(&c, C_ATOM), (ULONG_PTR)U(GetClassWord)(b, GCW_ATOM));
    EXPECT_EQ((uint32_t)U(GetClassLongW)(b, GCL_STYLE), C32(&c, C_STYLE) & 0x37bffu);
    EXPECT_EQ((uint32_t)U(GetClassLongW)(b, GCL_CBCLSEXTRA), C32(&c, C_CBCLS));
    EXPECT_EQ((uint32_t)U(GetClassLongW)(b, GCL_CBWNDEXTRA), C32(&c, C_CBWND0) + C32(&c, C_CBWND1));
    t_note("Button class: atom=0x%04x style=0x%08lx flags=0x%04x cbwnd=%lu+%lu", C16(&c, C_ATOM), (unsigned long)C32(&c, C_STYLE), C16(&c, C_FLAGS),
           (unsigned long)C32(&c, C_CBWND0), (unsigned long)C32(&c, C_CBWND1));
    END();
}
TEST(user_wnd_desktop_heap_cls_system_classes_share_atoms_per_class) {
    T_BEGIN();
    HWND p = mk_host();
    HWND b1 = mk_child(p, L"Button", WS_CHILD, 0, 0, 30, 20, 1);
    HWND b2 = mk_child(p, L"Button", WS_CHILD, 0, 0, 30, 20, 2);
    HWND s = mk_child(p, L"Static", WS_CHILD, 0, 0, 30, 20, 3);
    NEED_WND(b1, w1);
    struct wv w2, w3;
    CHECK(wv_get(b2, &w2) && wv_get(s, &w3));
    struct cv c1, c2, c3;
    CHECK(cv_get(&w1, &c1) && cv_get(&w2, &c2) && cv_get(&w3, &c3));
    EXPECT_EQ(c1.pc, c2.pc);
    CHECK(c1.pc != c3.pc);
    CHECK(C16(&c1, C_ATOM) != C16(&c3, C_ATOM));
    END();
}
TEST(user_wnd_desktop_heap_cls_desktop_window_class) {
    T_BEGIN();
    HWND d = U(GetDesktopWindow)();
    NEED_WND(d, w);
    struct cv c;
    CHECK(cv_get(&w, &c));
    EXPECT_EQ(C16(&c, C_ATOM), (ULONG_PTR)U(GetClassWord)(d, GCW_ATOM));
    EXPECT_EQ((uint32_t)U(GetClassLongW)(d, GCL_STYLE), C32(&c, C_STYLE) & 0x37bffu);
    END();
}

/* ---- class lifetime visible through the window ----------------------------------------------------------------- */

TEST(user_wnd_desktop_heap_cls_unregister_while_window_exists) {
    T_BEGIN();
    wchar_t cn[64];
    if (!reg_class(cn, 0, 0, 0, cls_wp, FALSE, NULL, NULL)) { t_skip("register failed"); return; }
    HWND h = mk_top(cn, WS_POPUP, 0, -30000, -30000, 10, 10, NULL);
    NEED_WND(h, w);
    SET_ERR(0);
    CHECK(!U(UnregisterClassW)(cn, g_hinst()));
    EXPECT_ERR(ERROR_CLASS_HAS_WINDOWS);
    CHECK(U(DestroyWindow)(h));
    CHECK(U(UnregisterClassW)(cn, g_hinst()));
    g_ntrkcls = 0;
    SET_ERR(0);
    HWND again = U(CreateWindowExW)(0, cn, L"x", WS_POPUP, 0, 0, 1, 1, NULL, NULL, g_hinst(), NULL);
    CHECK(again == NULL);
    EXPECT_ERR(ERROR_CANNOT_FIND_WND_CLASS);
    if (again) U(DestroyWindow)(again);
    END();
}
TEST(user_wnd_desktop_heap_cls_small_extra_sizes) {
    T_BEGIN();
    wchar_t cn[64];
    ATOM a1 = reg_class(cn, 0, 4, 4, cls_wp, FALSE, NULL, NULL);
    if (!a1) { t_skip("register failed"); return; }
    HWND h = mk_top(cn, WS_POPUP, 0, -30000, -30000, 10, 10, NULL);
    NEED_WND(h, w);
    struct cv c;
    CHECK(cv_get(&w, &c));
    EXPECT_EQ(C16(&c, C_ATOM), a1);
    EXPECT_EQ(C32(&c, C_CBCLS), 4);
    END();
}
