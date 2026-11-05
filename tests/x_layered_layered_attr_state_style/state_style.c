/* x_layered_layered_attr_state_style: state, style and read-back semantics of layered-window attributes.
 *
 * Measures what NtUserSetLayeredWindowAttributes (win32u, and the user32 SetLayeredWindowAttributes that jumps
 * to it) leaves behind and how NtUserGetLayeredWindowAttributes reads it back:
 *  - a layered window that was never configured has nothing to read (Get fails, last error untouched);
 *  - the stored state is the pair (colour key, alpha) plus the flags of the latest call; the colour key is only
 *    replaced by a call carrying LWA_COLORKEY and the alpha only by one carrying LWA_ALPHA, so flags are
 *    replaced but values are kept across calls that omit them; flags 0 is a valid call; a failed call changes
 *    nothing;
 *  - dropping WS_EX_LAYERED discards the state (Get fails with 87), re-adding the bit starts unconfigured;
 *  - move, size, z-order, show, hide, minimise, maximise, frame change, ownership/parenting and changes to other
 *    extended style bits leave the state alone; the attribute calls leave all style bits alone;
 *  - destroying a window invalidates it (1400) and a new window starts clean; windows are independent;
 *  - interplay with UpdateLayeredWindow, windows of other threads, converted children;
 *  - deterministic per-window message effects counted by a window procedure and drained with PeekMessage.
 * Expectations come from observation on Windows 10 22H2 x64. Values depending on the build, hardware or what is
 * on screen are printed with t_note, never checked. */
#include "t.h"

UDECL(BOOL, NtUserSetLayeredWindowAttributes, (HWND, COLORREF, BYTE, DWORD));
UDECL(BOOL, NtUserGetLayeredWindowAttributes, (HWND, COLORREF *, BYTE *, DWORD *));

#define N(a) ((int)(sizeof(a) / sizeof((a)[0])))
#define LWA_K LWA_COLORKEY
#define LWA_A LWA_ALPHA
#define STALE 0x1234u

/* ---- message counting window procedure ---------------------------------------------------------- */
static HWND g_watch;
static int cnt[0x400];
static LRESULT CALLBACK wp(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (h == g_watch && m < 0x400) cnt[m]++;
    if (m == WM_PAINT) { PAINTSTRUCT ps; U(BeginPaint)(h, &ps); U(EndPaint)(h, &ps); return 0; }
    return U(DefWindowProcA)(h, m, w, l);
}
static int registered;
static void reg(void) {
    if (registered) return;
    WNDCLASSA c; memset(&c, 0, sizeof c);
    c.lpfnWndProc = wp; c.hInstance = GetModuleHandleA(NULL); c.lpszClassName = "xlsq_cls";
    U(RegisterClassA)(&c);
    c.lpszClassName = "xlsq_shadow"; c.style = CS_DROPSHADOW; U(RegisterClassA)(&c);
    registered = 1;
}
static HWND mk(DWORD ex, DWORD st, const char *cls, HWND parent) {
    reg();
    return U(CreateWindowExA)(ex, cls, "t", st, -20000, -20000, 50, 40, parent, NULL, GetModuleHandleA(NULL), NULL);
}
static HWND top(DWORD ex) { return mk(ex, WS_POPUP, "xlsq_cls", NULL); }
static HWND lay(void) { return top(WS_EX_LAYERED); }
static void kill(HWND h) { if (h) U(DestroyWindow)(h); }
static void pump(void) { MSG m; while (U(PeekMessageA)(&m, NULL, 0, 0, PM_REMOVE)) U(DispatchMessageA)(&m); }
static void watch(HWND h) { pump(); memset(cnt, 0, sizeof cnt); g_watch = h; }
static LONG xs(HWND h) { return (LONG)U(GetWindowLongA)(h, GWL_EXSTYLE); }
static LONG ss(HWND h) { return (LONG)U(GetWindowLongA)(h, GWL_STYLE); }
static void set_xs(HWND h, LONG v) { U(SetWindowLongA)(h, GWL_EXSTYLE, v); }
static void add_layered(HWND h) { set_xs(h, xs(h) | WS_EX_LAYERED); }
static void del_layered(HWND h) { set_xs(h, xs(h) & ~WS_EX_LAYERED); }

/* ---- model ----------------------------------------------------------------------------------------- */
struct st { COLORREF k; BYTE a; DWORD f; };
static void model_set(struct st *s, COLORREF k, BYTE a, DWORD f) {
    if (f & LWA_K) s->k = k;
    if (f & LWA_A) s->a = a;
    s->f = f;
}
static BOOL nset(HWND h, COLORREF k, BYTE a, DWORD f) { return UN(NtUserSetLayeredWindowAttributes)(h, k, a, f); }
/* Get must succeed and match the model. */
static void expect_state(HWND h, const struct st *s) {
    COLORREF k = 0xAAAAAAAA; BYTE a = 0xBB; DWORD f = 0xCCCCCCCC;
    BOOL r = UN(NtUserGetLayeredWindowAttributes)(h, &k, &a, &f);
    CHECKF(r, "get failed, err %lu", GetLastError());
    if (!r) return;
    CHECKF(k == s->k, "key got %08lx want %08lx", (unsigned long)k, (unsigned long)s->k);
    CHECKF(a == s->a, "alpha got %02x want %02x", a, s->a);
    CHECKF(f == s->f, "flags got %lx want %lx", f, s->f);
}
/* Get must fail with last error err (0 = left at the sentinel value) and write nothing. */
static void expect_noget(HWND h, DWORD err) {
    COLORREF k = 0xAAAAAAAA; BYTE a = 0xBB; DWORD f = 0xCCCCCCCC;
    SetLastError(STALE);
    BOOL r = UN(NtUserGetLayeredWindowAttributes)(h, &k, &a, &f);
    DWORD e = GetLastError();
    CHECK(!r);
    CHECKF(e == (err ? err : STALE), "err got %lu want %lu", e, err ? err : STALE);
    CHECK(k == 0xAAAAAAAA && a == 0xBB && f == 0xCCCCCCCC);
}
static void op_show(HWND h);
static struct st zero_st = {0, 0, 0};
/* A shown window that gains WS_EX_LAYERED starts with an opaque alpha state instead of an unconfigured one. */
static struct st vis_default = { 0, 255, LWA_ALPHA };
static struct st conf(HWND h, COLORREF k, BYTE a, DWORD f) {   /* set on a fresh window and return the model */
    struct st s = zero_st;
    CHECK(nset(h, k, a, f));
    model_set(&s, k, a, f);
    return s;
}
#define NEED() do { REQUIRE(UN(NtUserSetLayeredWindowAttributes)); REQUIRE(UN(NtUserGetLayeredWindowAttributes)); \
    REQUIRE(U(CreateWindowExA)); } while (0)
#define MKLAY(h) HWND h = lay(); if (!h) { t_skip("cannot create layered window"); return; }

/* ===== never configured =========================================================================== */
TEST(fresh_popup_get_fails) { NEED(); MKLAY(h); expect_noget(h, 0); kill(h); }
TEST(fresh_overlapped_get_fails) {
    NEED(); HWND h = mk(WS_EX_LAYERED, WS_OVERLAPPEDWINDOW, "xlsq_cls", NULL);
    if (!h) { t_skip("no window"); return; } expect_noget(h, 0); kill(h);
}
TEST(fresh_style_added_get_fails) {
    NEED(); HWND h = top(0); if (!h) { t_skip("no window"); return; }
    expect_noget(h, 87); add_layered(h); expect_noget(h, 0); kill(h);
}
TEST(fresh_dropshadow_class_get_fails) {
    NEED(); HWND h = mk(WS_EX_LAYERED, WS_POPUP, "xlsq_shadow", NULL); if (!h) { t_skip("no window"); return; }
    expect_noget(h, 0); kill(h);
}
TEST(fresh_all_null_pointers_get_fails) {
    NEED(); MKLAY(h);
    SetLastError(STALE); CHECK(!UN(NtUserGetLayeredWindowAttributes)(h, NULL, NULL, NULL)); EXPECT_ERR(STALE);
    kill(h);
}
TEST(fresh_single_pointer_get_fails) {
    NEED(); MKLAY(h); COLORREF k = 1; BYTE a = 2; DWORD f = 3;
    CHECK(!UN(NtUserGetLayeredWindowAttributes)(h, &k, NULL, NULL));
    CHECK(!UN(NtUserGetLayeredWindowAttributes)(h, NULL, &a, NULL));
    CHECK(!UN(NtUserGetLayeredWindowAttributes)(h, NULL, NULL, &f));
    CHECK(k == 1 && a == 2 && f == 3);
    kill(h);
}
TEST(fresh_ex_style_bit_visible) {
    NEED(); MKLAY(h); CHECK(xs(h) & WS_EX_LAYERED); EXPECT_EQ(xs(h) & ~WS_EX_LAYERED, 0); kill(h);
}
TEST(fresh_ex_style_combinations_get_fails) {
    NEED();
    static const DWORD extra[] = { 0, WS_EX_TOOLWINDOW, WS_EX_TOPMOST, WS_EX_NOACTIVATE, WS_EX_TRANSPARENT,
                                   WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, WS_EX_APPWINDOW };
    for (int i = 0; i < N(extra); i++) {
        HWND h = top(WS_EX_LAYERED | extra[i]); if (!h) { t_skip("no window"); return; }
        CHECKF(xs(h) & WS_EX_LAYERED, "case %d", i);
        expect_noget(h, 0); kill(h);
    }
}
TEST(fresh_get_does_not_configure) {
    NEED(); MKLAY(h);
    for (int i = 0; i < 5; i++) expect_noget(h, 0);
    struct st s = conf(h, 5, 6, 3); expect_state(h, &s); kill(h);
}

/* ===== first Set, per flag value ================================================================== */
TEST(first_set_flags0_zeroes) {
    NEED(); MKLAY(h); struct st s = conf(h, 0x123456, 0x80, 0);
    EXPECT_EQ(s.k, 0); EXPECT_EQ(s.a, 0); expect_state(h, &s); kill(h);
}
TEST(first_set_flags1_key_only) {
    NEED(); MKLAY(h); struct st s = conf(h, 0x123456, 0x80, LWA_K);
    EXPECT_EQ(s.a, 0); expect_state(h, &s); kill(h);
}
TEST(first_set_flags2_alpha_only) {
    NEED(); MKLAY(h); struct st s = conf(h, 0x123456, 0x80, LWA_A);
    EXPECT_EQ(s.k, 0); expect_state(h, &s); kill(h);
}
TEST(first_set_flags3_both) {
    NEED(); MKLAY(h); struct st s = conf(h, 0x123456, 0x80, 3);
    EXPECT_EQ(s.k, 0x123456); EXPECT_EQ(s.a, 0x80); expect_state(h, &s); kill(h);
}
/* from each flag value to each flag value: values omitted by the second call are kept from before */
#define TRANS(n, from) TEST(transition_from_##n) { \
    NEED(); \
    for (int to = 0; to < 4; to++) { \
        MKLAY(h); struct st s = conf(h, 0x112233, 0x41, from); \
        CHECKF(nset(h, 0x445566, 0x82, to), "to %d", to); model_set(&s, 0x445566, 0x82, to); \
        CHECKF(1, ""); expect_state(h, &s); kill(h); \
    } }
TRANS(0, 0)
TRANS(1, 1)
TRANS(2, 2)
TRANS(3, 3)
TEST(flags_replaced_not_merged) {
    NEED(); MKLAY(h); struct st s = conf(h, 1, 2, 3);
    CHECK(nset(h, 9, 9, LWA_A)); model_set(&s, 9, 9, LWA_A); EXPECT_EQ(s.f, LWA_A); expect_state(h, &s);
    CHECK(nset(h, 7, 7, LWA_K)); model_set(&s, 7, 7, LWA_K); EXPECT_EQ(s.f, LWA_K); expect_state(h, &s);
    kill(h);
}
TEST(key_and_alpha_independent) {
    NEED(); MKLAY(h); struct st s = conf(h, 0x10, 0x20, 3);
    CHECK(nset(h, 0x30, 0, LWA_K)); model_set(&s, 0x30, 0, LWA_K); EXPECT_EQ(s.a, 0x20); expect_state(h, &s);
    CHECK(nset(h, 0, 0x40, LWA_A)); model_set(&s, 0, 0x40, LWA_A); EXPECT_EQ(s.k, 0x30); expect_state(h, &s);
    kill(h);
}
TEST(last_set_wins) {
    NEED(); MKLAY(h); struct st s = zero_st;
    for (int i = 1; i <= 20; i++) { CHECK(nset(h, i * 0x010101, i * 10, 3)); model_set(&s, i * 0x010101, i * 10, 3); }
    expect_state(h, &s); kill(h);
}
TEST(repeated_identical_set_idempotent) {
    NEED(); MKLAY(h); struct st s = zero_st;
    for (int i = 0; i < 8; i++) { CHECKF(nset(h, 0x0A0B0C, 0x5A, 3), "iter %d", i); }
    model_set(&s, 0x0A0B0C, 0x5A, 3); expect_state(h, &s);
    for (int i = 0; i < 3; i++) expect_state(h, &s);   /* reading does not change anything */
    kill(h);
}
TEST(model_random_sequence) {
    NEED(); MKLAY(h); struct st s = zero_st; unsigned x = 12345;
    CHECK(nset(h, 0, 0, 0));
    for (int i = 0; i < 300; i++) {
        x = x * 1103515245u + 12345u;
        COLORREF k = (x >> 8) & 0xFFFFFF; BYTE a = (x >> 3) & 0xFF; DWORD f = (x >> 28) & 3;
        CHECKF(nset(h, k, a, f), "step %d", i); model_set(&s, k, a, f);
        if (i % 7 == 0) expect_state(h, &s);
    }
    expect_state(h, &s); kill(h);
}
TEST(alpha_all_256_values_roundtrip) {
    NEED(); MKLAY(h); int bad = 0;
    for (int a = 0; a < 256; a++) {
        BYTE g = 0xEE; DWORD f = 0;
        if (!nset(h, 0, (BYTE)a, LWA_A) || !UN(NtUserGetLayeredWindowAttributes)(h, NULL, &g, &f) || g != a || f != LWA_A) bad++;
    }
    EXPECT_EQ(bad, 0); kill(h);
}
TEST(colorkey_values_roundtrip_unmasked) {
    NEED(); MKLAY(h);
    static const COLORREF ks[] = { 0, 1, 0xFF, 0xFF00, 0xFF0000, 0x00FFFFFF, 0x01000000, 0xFF000000, 0x80808080, 0xFFFFFFFF, 0x02000000 | 0x123456 };
    for (int i = 0; i < N(ks); i++) {
        COLORREF g = 5; DWORD f = 0;
        CHECKF(nset(h, ks[i], 0, LWA_K), "set %d", i);
        CHECKF(UN(NtUserGetLayeredWindowAttributes)(h, &g, NULL, &f), "get %d", i);
        CHECKF(g == ks[i], "got %08lx want %08lx", (unsigned long)g, (unsigned long)ks[i]);
    }
    kill(h);
}
TEST(flags_output_masked_to_two_bits) {
    NEED(); MKLAY(h);
    for (DWORD f = 0; f < 4; f++) { DWORD g = 0xFFFFFFFF; CHECK(nset(h, 1, 2, f)); CHECK(UN(NtUserGetLayeredWindowAttributes)(h, NULL, NULL, &g)); EXPECT_EQ(g, f); }
    kill(h);
}
TEST(get_null_pointer_combinations) {
    NEED(); MKLAY(h); struct st s = conf(h, 0x8899AA, 0x77, 3);
    COLORREF k = 0; BYTE a = 0; DWORD f = 0;
    CHECK(UN(NtUserGetLayeredWindowAttributes)(h, NULL, NULL, NULL));
    CHECK(UN(NtUserGetLayeredWindowAttributes)(h, &k, NULL, NULL)); EXPECT_EQ(k, s.k);
    CHECK(UN(NtUserGetLayeredWindowAttributes)(h, NULL, &a, NULL)); EXPECT_EQ(a, s.a);
    CHECK(UN(NtUserGetLayeredWindowAttributes)(h, NULL, NULL, &f)); EXPECT_EQ(f, s.f);
    k = 0; a = 0; f = 0;
    CHECK(UN(NtUserGetLayeredWindowAttributes)(h, &k, &a, NULL)); EXPECT_EQ(k, s.k); EXPECT_EQ(a, s.a);
    CHECK(UN(NtUserGetLayeredWindowAttributes)(h, NULL, &a, &f)); EXPECT_EQ(f, s.f);
    kill(h);
}

/* ===== failed calls change nothing ================================================================= */
TEST(invalid_flags_fail_87_state_kept) {
    NEED(); MKLAY(h); struct st s = conf(h, 0x010203, 0x44, 3);
    static const DWORD bad[] = { 4, 5, 6, 7, 8, 0x10, 0x100, 0x10000, 0x80000000, 0xFFFFFFFC, 0xFFFFFFFF };
    for (int i = 0; i < N(bad); i++) {
        SetLastError(STALE); CHECKF(!nset(h, 0x999999, 0x99, bad[i]), "flags %lx", bad[i]); EXPECT_ERR(87);
        expect_state(h, &s);
    }
    kill(h);
}
TEST(invalid_flags_on_fresh_layered_stay_unconfigured) {
    NEED(); MKLAY(h);
    SetLastError(STALE); CHECK(!nset(h, 1, 2, 4)); EXPECT_ERR(87);
    SetLastError(STALE); CHECK(!nset(h, 1, 2, 0xFFFFFFFF)); EXPECT_ERR(87);
    expect_noget(h, 0); kill(h);
}
TEST(nonlayered_set_fails_87_for_each_flag_value) {
    NEED(); HWND h = top(0); if (!h) { t_skip("no window"); return; }
    static const DWORD fl[] = { 0, 1, 2, 3, 4, 8, 0xFFFFFFFF };
    for (int i = 0; i < N(fl); i++) { SetLastError(STALE); CHECKF(!nset(h, 1, 2, fl[i]), "flags %lx", fl[i]); EXPECT_ERR(87); }
    kill(h);
}
TEST(nonlayered_failed_set_leaves_nothing_behind) {
    NEED(); HWND h = top(0); if (!h) { t_skip("no window"); return; }
    CHECK(!nset(h, 1, 2, 3)); add_layered(h); expect_noget(h, 0); kill(h);
}
TEST(nonlayered_get_fails_87) {
    NEED(); HWND h = top(0); if (!h) { t_skip("no window"); return; }
    expect_noget(h, 87); kill(h);
}
TEST(nonlayered_variants_get_set_87) {
    NEED();
    static const DWORD sts[] = { WS_POPUP, WS_OVERLAPPEDWINDOW, WS_POPUP | WS_BORDER, WS_POPUP | WS_CAPTION };
    for (int i = 0; i < N(sts); i++) {
        HWND h = mk(0, sts[i], "xlsq_cls", NULL); if (!h) { t_skip("no window"); return; }
        SetLastError(STALE); CHECKF(!nset(h, 1, 2, 3), "case %d", i); EXPECT_ERR(87); expect_noget(h, 87); kill(h);
    }
}
TEST(failed_set_does_not_touch_ex_style) {
    NEED(); MKLAY(h); LONG before = xs(h); nset(h, 0, 0, 0x20); EXPECT_EQ(xs(h), before); kill(h);
}

/* ===== style bit: add, clear, re-add ================================================================ */
TEST(clear_style_get_fails_87) {
    NEED(); MKLAY(h); conf(h, 1, 2, 3); del_layered(h); expect_noget(h, 87); kill(h);
}
TEST(clear_style_set_fails_87) {
    NEED(); MKLAY(h); conf(h, 1, 2, 3); del_layered(h);
    SetLastError(STALE); CHECK(!nset(h, 4, 5, 3)); EXPECT_ERR(87); kill(h);
}
TEST(readd_style_starts_unconfigured) {
    NEED(); MKLAY(h); conf(h, 0x112233, 0x66, 3); del_layered(h); add_layered(h); expect_noget(h, 0); kill(h);
}
TEST(readd_style_then_set_works) {
    NEED(); MKLAY(h); conf(h, 0x112233, 0x66, 3); del_layered(h); add_layered(h);
    struct st s = conf(h, 5, 6, LWA_A); expect_state(h, &s);
    EXPECT_EQ(s.k, 0); kill(h);
}
TEST(readd_style_old_values_not_resurrected) {
    NEED(); MKLAY(h); conf(h, 0x112233, 0x66, 3); del_layered(h); add_layered(h);
    CHECK(nset(h, 0, 0, 0)); struct st z = zero_st; expect_state(h, &z); kill(h);
}
TEST(toggle_style_ten_times) {
    NEED(); MKLAY(h);
    for (int i = 0; i < 10; i++) {
        struct st s = conf(h, i, i + 1, 3); expect_state(h, &s);
        del_layered(h); expect_noget(h, 87); add_layered(h); expect_noget(h, 0);
    }
    kill(h);
}
TEST(clear_style_before_any_set) {
    NEED(); MKLAY(h); del_layered(h); expect_noget(h, 87); add_layered(h); expect_noget(h, 0); kill(h);
}
TEST(plain_window_gains_layered_by_setwindowlong) {
    NEED(); HWND h = top(0); if (!h) { t_skip("no window"); return; }
    add_layered(h); CHECK(xs(h) & WS_EX_LAYERED); expect_noget(h, 0);
    struct st s = conf(h, 0x10, 0x20, 3); expect_state(h, &s); kill(h);
}
TEST(setwindowlong_returns_old_ex_style) {
    NEED(); HWND h = top(0); if (!h) { t_skip("no window"); return; }
    LONG o = xs(h);
    EXPECT_EQ((LONG)U(SetWindowLongA)(h, GWL_EXSTYLE, o | WS_EX_LAYERED), o);
    EXPECT_EQ((LONG)U(SetWindowLongA)(h, GWL_EXSTYLE, o | WS_EX_LAYERED), o | WS_EX_LAYERED);
    kill(h);
}
TEST(setwindowlong_same_value_keeps_state) {
    NEED(); MKLAY(h); struct st s = conf(h, 3, 4, 3);
    for (int i = 0; i < 3; i++) set_xs(h, xs(h));
    expect_state(h, &s); kill(h);
}
TEST(setwindowlong_zero_clears_layered) {
    NEED(); MKLAY(h); conf(h, 3, 4, 3); set_xs(h, 0); EXPECT_EQ(xs(h) & WS_EX_LAYERED, 0); expect_noget(h, 87); kill(h);
}
TEST(setwindowlongptr_toggles_layered) {
    NEED(); REQUIRE(U(SetWindowLongPtrA)); HWND h = top(0); if (!h) { t_skip("no window"); return; }
    U(SetWindowLongPtrA)(h, GWL_EXSTYLE, WS_EX_LAYERED); CHECK(xs(h) & WS_EX_LAYERED);
    struct st s = conf(h, 1, 2, 3); expect_state(h, &s);
    U(SetWindowLongPtrA)(h, GWL_EXSTYLE, 0); expect_noget(h, 87); kill(h);
}
TEST(other_ex_bits_change_keeps_state) {
    NEED(); static const LONG bits[] = { WS_EX_TOOLWINDOW, WS_EX_NOACTIVATE, WS_EX_TRANSPARENT, WS_EX_APPWINDOW, WS_EX_CLIENTEDGE };
    MKLAY(h); struct st s = conf(h, 0x778899, 0x55, 3);
    for (int i = 0; i < N(bits); i++) {
        set_xs(h, xs(h) | bits[i]); CHECKF(xs(h) & bits[i], "bit %d on", i); expect_state(h, &s);
        set_xs(h, xs(h) & ~bits[i]); CHECKF(!(xs(h) & bits[i]), "bit %d off", i); expect_state(h, &s);
    }
    kill(h);
}
TEST(set_does_not_change_any_style_bit) {
    NEED(); MKLAY(h); LONG x0 = xs(h), s0 = ss(h);
    static const DWORD fl[] = { 0, 1, 2, 3 };
    for (int i = 0; i < N(fl); i++) { CHECK(nset(h, 0x123, 0x45, fl[i])); EXPECT_EQ(xs(h), x0); EXPECT_EQ(ss(h), s0); }
    kill(h);
}
TEST(transparent_bit_unaffected_by_set) {
    NEED(); MKLAY(h); CHECK(!(xs(h) & WS_EX_TRANSPARENT));
    conf(h, 1, 0, 3); CHECK(!(xs(h) & WS_EX_TRANSPARENT)); kill(h);
    HWND t = top(WS_EX_LAYERED | WS_EX_TRANSPARENT); if (!t) { t_skip("no window"); return; }
    struct st s = conf(t, 1, 2, 3); CHECK(xs(t) & WS_EX_TRANSPARENT); CHECK(xs(t) & WS_EX_LAYERED); expect_state(t, &s); kill(t);
}
TEST(transparent_toggle_keeps_values) {
    NEED(); MKLAY(h); struct st s = conf(h, 9, 8, 3);
    set_xs(h, xs(h) | WS_EX_TRANSPARENT); expect_state(h, &s);
    CHECK(nset(h, 7, 6, 3)); model_set(&s, 7, 6, 3); set_xs(h, xs(h) & ~WS_EX_TRANSPARENT); expect_state(h, &s); kill(h);
}
TEST(ex_style_exact_value_after_set) {
    NEED();
    static const DWORD cre[] = { WS_EX_LAYERED, WS_EX_LAYERED | WS_EX_TOOLWINDOW, WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_NOACTIVATE, WS_EX_LAYERED | WS_EX_TRANSPARENT };
    for (int i = 0; i < N(cre); i++) {
        HWND h = top(cre[i]); if (!h) { t_skip("no window"); return; }
        LONG before = xs(h); conf(h, 1, 2, 3); EXPECT_EQ(xs(h), before); CHECK(xs(h) & WS_EX_LAYERED); kill(h);
    }
}

/* A shown window keeps its colour key across a style cycle and comes back opaque: alpha 255, alpha flag only. */
TEST(readd_style_on_shown_window_keeps_key_comes_back_opaque) {
    NEED(); MKLAY(h); op_show(h); conf(h, 0x112233, 0x66, 3); del_layered(h); expect_noget(h, 87);
    add_layered(h); struct st want = { 0x112233, 255, LWA_ALPHA }; expect_state(h, &want); kill(h);
}
TEST(readd_style_on_shown_window_then_set_key_keeps_opaque_alpha) {
    NEED(); MKLAY(h); op_show(h); del_layered(h); add_layered(h);
    struct st s = vis_default; CHECK(nset(h, 0x445566, 7, LWA_K)); model_set(&s, 0x445566, 7, LWA_K);
    expect_state(h, &s); kill(h);
}
TEST(shown_plain_window_gaining_style_note) {
    NEED(); HWND h = top(0); if (!h) { t_skip("no window"); return; }
    op_show(h); add_layered(h);
    /* a shown window that never had attributes reads back as all zero, flags 0 */
    struct st z = zero_st; expect_state(h, &z);
    kill(h);
}
TEST(hidden_then_shown_after_readd_keeps_unconfigured) {
    NEED(); MKLAY(h); del_layered(h); add_layered(h); expect_noget(h, 0); op_show(h); pump(); expect_noget(h, 0); kill(h);
}

/* ===== window operations leave the state alone ===================================================== */
typedef void (*opfn)(HWND);
static void op_move(HWND h) { U(SetWindowPos)(h, NULL, -19000, -19000, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE); }
static void op_size(HWND h) { U(SetWindowPos)(h, NULL, 0, 0, 90, 70, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE); }
static void op_movesize(HWND h) { U(SetWindowPos)(h, NULL, -18000, -18000, 33, 22, SWP_NOZORDER | SWP_NOACTIVATE); }
static void op_zbottom(HWND h) { U(SetWindowPos)(h, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE); }
static void op_ztop(HWND h) { U(SetWindowPos)(h, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE); }
static void op_topmost(HWND h) { U(SetWindowPos)(h, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE); }
static void op_notopmost(HWND h) { U(SetWindowPos)(h, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE); }
static void op_frame(HWND h) { U(SetWindowPos)(h, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED); }
static void op_swp_show(HWND h) { U(SetWindowPos)(h, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW); }
static void op_swp_hide(HWND h) { U(SetWindowPos)(h, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_HIDEWINDOW); }
static void op_show(HWND h) { U(ShowWindow)(h, SW_SHOWNOACTIVATE); }
static void op_hide(HWND h) { U(ShowWindow)(h, SW_HIDE); }
static void op_min(HWND h) { U(ShowWindow)(h, SW_SHOWMINNOACTIVE); }
static void op_restore(HWND h) { U(ShowWindow)(h, SW_SHOWNOACTIVATE); }
static void op_enable(HWND h) { U(EnableWindow)(h, FALSE); U(EnableWindow)(h, TRUE); }
static void op_settext(HWND h) { U(SetWindowTextA)(h, "renamed"); }
static void op_invalidate(HWND h) { U(InvalidateRect)(h, NULL, TRUE); pump(); }
static void op_redraw(HWND h) { U(RedrawWindow)(h, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE | RDW_FRAME); }
static void op_setuserdata(HWND h) { U(SetWindowLongA)(h, GWLP_USERDATA, 77); }
static const struct { const char *n; opfn f; } ops[] = {
    { "move", op_move }, { "size", op_size }, { "movesize", op_movesize }, { "zbottom", op_zbottom }, { "ztop", op_ztop },
    { "topmost", op_topmost }, { "notopmost", op_notopmost }, { "frame", op_frame }, { "swp_show", op_swp_show },
    { "swp_hide", op_swp_hide }, { "show", op_show }, { "hide", op_hide }, { "min", op_min }, { "restore", op_restore },
    { "enable", op_enable }, { "settext", op_settext }, { "invalidate", op_invalidate }, { "redraw", op_redraw },
    { "userdata", op_setuserdata },
};
TEST(ops_keep_state_each_op) {
    NEED(); REQUIRE(U(SetWindowPos));
    for (int i = 0; i < N(ops); i++) {
        MKLAY(h); struct st s = conf(h, 0x0A141E, 0x6B, 3);
        ops[i].f(h); pump(); CHECKF(1, "%s", ops[i].n);
        COLORREF k = 0; BYTE a = 0; DWORD f = 0;
        BOOL r = UN(NtUserGetLayeredWindowAttributes)(h, &k, &a, &f);
        CHECKF(r && k == s.k && a == s.a && f == s.f, "op %s r=%d k=%lx a=%x f=%lx", ops[i].n, r, (unsigned long)k, a, f);
        CHECKF(xs(h) & WS_EX_LAYERED, "op %s lost style", ops[i].n);
        kill(h);
    }
}
TEST(ops_keep_state_all_in_sequence) {
    NEED(); MKLAY(h); struct st s = conf(h, 0xC0FFEE & 0xFFFFFF, 0x31, 3);
    for (int r = 0; r < 3; r++) for (int i = 0; i < N(ops); i++) { ops[i].f(h); pump(); }
    expect_state(h, &s); CHECK(xs(h) & WS_EX_LAYERED); kill(h);
}
TEST(ops_do_not_configure_unset_window) {
    NEED();
    for (int i = 0; i < N(ops); i++) {
        MKLAY(h); ops[i].f(h); pump();
        COLORREF k = 1; BYTE a = 2; DWORD f = 3;
        CHECKF(!UN(NtUserGetLayeredWindowAttributes)(h, &k, &a, &f), "op %s configured the window", ops[i].n);
        kill(h);
    }
}
TEST(set_after_ops_on_visible_window) {
    NEED(); MKLAY(h); op_show(h); op_movesize(h); pump();
    struct st s = conf(h, 0x70, 0x71, 3); expect_state(h, &s); op_hide(h); expect_state(h, &s); kill(h);
}
TEST(minimized_created_window_accepts_set) {
    NEED(); HWND h = mk(WS_EX_LAYERED, WS_POPUP | WS_MINIMIZE, "xlsq_cls", NULL); if (!h) { t_skip("no window"); return; }
    struct st s = conf(h, 1, 2, 3); expect_state(h, &s); kill(h);
}
TEST(maximize_restore_keeps_state) {
    NEED(); HWND h = mk(WS_EX_LAYERED, WS_OVERLAPPEDWINDOW, "xlsq_cls", NULL); if (!h) { t_skip("no window"); return; }
    struct st s = conf(h, 4, 5, 3);
    U(ShowWindow)(h, SW_SHOWMAXIMIZED); expect_state(h, &s); U(ShowWindow)(h, SW_RESTORE); expect_state(h, &s); U(ShowWindow)(h, SW_HIDE);
    EXPECT_EQ(xs(h) & WS_EX_LAYERED, WS_EX_LAYERED); kill(h);
}
TEST(owner_change_keeps_state) {
    NEED(); MKLAY(h); HWND p = top(0); if (!p) { t_skip("no window"); return; }
    struct st s = conf(h, 6, 7, 3);
    U(SetParent)(h, p); expect_state(h, &s); U(SetParent)(h, NULL); expect_state(h, &s); kill(h); kill(p);
}
TEST(owned_window_destroy_with_owner) {
    NEED(); HWND p = top(0); if (!p) { t_skip("no window"); return; }
    HWND c = mk(WS_EX_LAYERED, WS_POPUP, "xlsq_cls", p); if (!c) { kill(p); t_skip("no window"); return; }
    struct st s = conf(c, 6, 7, 3); expect_state(c, &s);
    kill(p); SetLastError(STALE); CHECK(!nset(c, 1, 2, 3)); EXPECT_ERR(1400);
}
TEST(toplevel_converted_to_child_keeps_state) {
    NEED(); MKLAY(h); HWND p = top(0); if (!p) { t_skip("no window"); return; }
    struct st s = conf(h, 1, 2, 3);
    LONG st = ss(h); U(SetWindowLongA)(h, GWL_STYLE, (st & ~WS_POPUP) | WS_CHILD); U(SetParent)(h, p);
    CHECK(U(GetParent)(h) == p); CHECK(xs(h) & WS_EX_LAYERED);
    expect_state(h, &s);
    struct st s2 = conf(h, 9, 8, 3); expect_state(h, &s2);
    U(SetParent)(h, NULL); kill(h); kill(p);
}

/* ===== child windows ================================================================================ */
TEST(layered_child_creation_coherent) {
    NEED(); HWND p = top(0); if (!p) { t_skip("no window"); return; }
    HWND c = mk(WS_EX_LAYERED, WS_CHILD, "xlsq_cls", p);
    t_note("layered child created without a compatibility manifest: %d", c != NULL);
    if (c) {
        CHECK(xs(c) & WS_EX_LAYERED);
        struct st s = conf(c, 1, 2, 3); expect_state(c, &s);
        kill(c);
    }
    kill(p);
}
TEST(plain_child_cannot_be_set) {
    NEED(); HWND p = top(0); if (!p) { t_skip("no window"); return; }
    HWND c = mk(0, WS_CHILD, "xlsq_cls", p); if (!c) { kill(p); t_skip("no child"); return; }
    SetLastError(STALE); CHECK(!nset(c, 1, 2, 3)); EXPECT_ERR(87);
    add_layered(c);
    t_note("child gains layered bit via SetWindowLong: %d", !!(xs(c) & WS_EX_LAYERED));
    if (xs(c) & WS_EX_LAYERED) { struct st s = conf(c, 1, 2, 3); expect_state(c, &s); }
    else { SetLastError(STALE); CHECK(!nset(c, 1, 2, 3)); EXPECT_ERR(87); expect_noget(c, 87); }
    kill(c); kill(p);
}

/* ===== destruction and independence ================================================================ */
TEST(destroyed_window_get_set_1400) {
    NEED(); MKLAY(h); conf(h, 1, 2, 3); kill(h);
    expect_noget(h, 1400);
    SetLastError(STALE); CHECK(!nset(h, 1, 2, 3)); EXPECT_ERR(1400);
}
TEST(destroyed_unconfigured_window_1400) {
    NEED(); MKLAY(h); kill(h); expect_noget(h, 1400); SetLastError(STALE); CHECK(!nset(h, 1, 2, 3)); EXPECT_ERR(1400);
}
TEST(destroyed_window_invalid_flags_note) {
    NEED(); MKLAY(h); kill(h); SetLastError(STALE); CHECK(!nset(h, 1, 2, 0xFF)); t_note("destroyed handle with invalid flags: error %lu", GetLastError());
}
TEST(recreated_window_starts_clean) {
    NEED();
    for (int i = 0; i < 50; i++) {
        MKLAY(h); expect_noget(h, 0); conf(h, 0xFFFFFF - i, 255 - i, 3); kill(h);
    }
}
TEST(recreate_same_class_clean_state) {
    NEED(); MKLAY(h); conf(h, 0x111111, 0x11, 3); kill(h); MKLAY(g); expect_noget(g, 0); kill(g);
}
TEST(recreate_after_style_cycle_clean) {
    NEED(); MKLAY(h); conf(h, 0x111111, 0x11, 3); del_layered(h); kill(h); MKLAY(g); expect_noget(g, 0); kill(g);
}
TEST(windows_are_independent) {
    NEED(); HWND w[8]; struct st s[8];
    for (int i = 0; i < 8; i++) { w[i] = lay(); if (!w[i]) { t_skip("no window"); return; } }
    for (int i = 0; i < 8; i++) s[i] = conf(w[i], (i + 1) * 0x101010, (i + 1) * 30, 3);
    for (int i = 0; i < 8; i++) expect_state(w[i], &s[i]);
    CHECK(nset(w[3], 0, 0, 0)); model_set(&s[3], 0, 0, 0);
    del_layered(w[5]); add_layered(w[5]);
    for (int i = 0; i < 8; i++) { if (i == 5) expect_noget(w[i], 0); else expect_state(w[i], &s[i]); }
    for (int i = 0; i < 8; i++) kill(w[i]);
}
TEST(configuring_one_does_not_configure_another) {
    NEED(); MKLAY(a); MKLAY(b); conf(a, 1, 2, 3); expect_noget(b, 0); kill(a); kill(b);
}
TEST(destroying_one_keeps_other) {
    NEED(); MKLAY(a); MKLAY(b); struct st s = conf(b, 4, 5, 3); conf(a, 1, 2, 3); kill(a); expect_state(b, &s); kill(b);
}
TEST(many_windows_distinct_alpha) {
    NEED(); HWND w[16];
    for (int i = 0; i < 16; i++) { w[i] = lay(); if (!w[i]) { t_skip("no window"); return; } CHECK(nset(w[i], 0, i * 16, LWA_A)); }
    for (int i = 15; i >= 0; i--) { BYTE a = 0xEE; CHECK(UN(NtUserGetLayeredWindowAttributes)(w[i], NULL, &a, NULL)); EXPECT_EQ(a, i * 16); }
    for (int i = 0; i < 16; i++) kill(w[i]);
}
TEST(invalid_handles_1400_with_valid_flags) {
    NEED(); HWND bad[] = { NULL, (HWND)(ULONG_PTR)1, (HWND)(ULONG_PTR)0x777777, (HWND)(ULONG_PTR)-1 };
    for (int i = 0; i < N(bad); i++) {
        SetLastError(STALE); CHECKF(!nset(bad[i], 1, 2, 3), "hwnd %p", bad[i]); EXPECT_ERR(1400);
        expect_noget(bad[i], 1400);
    }
}

/* ===== other thread ================================================================================== */
struct thr { HWND h; BOOL set_r, get_r; COLORREF k; BYTE a; DWORD f; };
static DWORD WINAPI thr_main(void *p) {
    struct thr *t = p;
    t->set_r = UN(NtUserSetLayeredWindowAttributes)(t->h, 0x0C0D0E, 0x99, 3);
    t->get_r = UN(NtUserGetLayeredWindowAttributes)(t->h, &t->k, &t->a, &t->f);
    return 0;
}
TEST(other_thread_can_set_and_get) {
    NEED(); REQUIRE(U(CreateThread)); MKLAY(h); struct thr t; memset(&t, 0, sizeof t); t.h = h;
    HANDLE th = U(CreateThread)(NULL, 0, thr_main, &t, 0, NULL);
    if (!th) { kill(h); t_skip("no thread"); return; }
    WaitForSingleObject(th, 10000); CloseHandle(th);
    CHECK(t.set_r); CHECK(t.get_r); EXPECT_EQ(t.k, 0x0C0D0E); EXPECT_EQ(t.a, 0x99); EXPECT_EQ(t.f, 3);
    struct st s = zero_st; model_set(&s, 0x0C0D0E, 0x99, 3); expect_state(h, &s); kill(h);
}
static DWORD WINAPI thr_unconf(void *p) {
    struct thr *t = p; t->get_r = UN(NtUserGetLayeredWindowAttributes)(t->h, &t->k, &t->a, &t->f); return 0;
}
TEST(other_thread_sees_unconfigured) {
    NEED(); REQUIRE(U(CreateThread)); MKLAY(h); struct thr t; memset(&t, 0, sizeof t); t.h = h;
    HANDLE th = U(CreateThread)(NULL, 0, thr_unconf, &t, 0, NULL);
    if (!th) { kill(h); t_skip("no thread"); return; }
    WaitForSingleObject(th, 10000); CloseHandle(th); CHECK(!t.get_r); kill(h);
}

/* ===== user32 front doors =========================================================================== */
TEST(user32_set_get_match_syscall) {
    NEED(); REQUIRE(U(SetLayeredWindowAttributes)); REQUIRE(U(GetLayeredWindowAttributes)); MKLAY(h);
    COLORREF k = 0; BYTE a = 0; DWORD f = 0;
    CHECK(U(SetLayeredWindowAttributes)(h, 0x010203, 0x44, 3));
    CHECK(U(GetLayeredWindowAttributes)(h, &k, &a, &f)); EXPECT_EQ(k, 0x010203); EXPECT_EQ(a, 0x44); EXPECT_EQ(f, 3);
    CHECK(UN(NtUserSetLayeredWindowAttributes)(h, 0x040506, 0x77, 2));
    CHECK(U(GetLayeredWindowAttributes)(h, &k, &a, &f)); EXPECT_EQ(k, 0x010203); EXPECT_EQ(a, 0x77); EXPECT_EQ(f, 2);
    kill(h);
}
TEST(user32_errors_match_syscall) {
    NEED(); REQUIRE(U(SetLayeredWindowAttributes)); REQUIRE(U(GetLayeredWindowAttributes));
    HWND p = top(0); MKLAY(h); if (!p) { t_skip("no window"); return; }
    SetLastError(STALE); CHECK(!U(SetLayeredWindowAttributes)(p, 1, 2, 3)); EXPECT_ERR(87);
    SetLastError(STALE); CHECK(!U(SetLayeredWindowAttributes)(h, 1, 2, 4)); EXPECT_ERR(87);
    SetLastError(STALE); CHECK(!U(SetLayeredWindowAttributes)(NULL, 1, 2, 3)); EXPECT_ERR(1400);
    COLORREF k; BYTE a; DWORD f;
    SetLastError(STALE); CHECK(!U(GetLayeredWindowAttributes)(p, &k, &a, &f)); EXPECT_ERR(87);
    SetLastError(STALE); CHECK(!U(GetLayeredWindowAttributes)(NULL, &k, &a, &f)); EXPECT_ERR(1400);
    SetLastError(STALE); CHECK(!U(GetLayeredWindowAttributes)(h, &k, &a, &f)); EXPECT_ERR(STALE);
    kill(p); kill(h);
}
TEST(user32_get_null_pointers) {
    NEED(); REQUIRE(U(SetLayeredWindowAttributes)); REQUIRE(U(GetLayeredWindowAttributes)); MKLAY(h);
    CHECK(U(SetLayeredWindowAttributes)(h, 1, 2, 3));
    CHECK(U(GetLayeredWindowAttributes)(h, NULL, NULL, NULL));
    DWORD f = 0; CHECK(U(GetLayeredWindowAttributes)(h, NULL, NULL, &f)); EXPECT_EQ(f, 3);
    kill(h);
}

/* ===== UpdateLayeredWindow interplay ================================================================ */
struct ulw { HDC sd, md; HBITMAP bm; HGDIOBJ old; };
static int ulw_open(struct ulw *u) {
    if (!U(GetDC) || !U(CreateCompatibleDC) || !U(CreateCompatibleBitmap) || !U(SelectObject) || !U(UpdateLayeredWindow)) return 0;
    u->sd = U(GetDC)(NULL); if (!u->sd) return 0;
    u->md = U(CreateCompatibleDC)(u->sd); u->bm = U(CreateCompatibleBitmap)(u->sd, 4, 4);
    if (!u->md || !u->bm) return 0;
    u->old = U(SelectObject)(u->md, u->bm); return 1;
}
static void ulw_close(struct ulw *u) {
    if (u->md && u->old) U(SelectObject)(u->md, u->old);
    if (u->bm) U(DeleteObject)(u->bm);
    if (u->md) U(DeleteDC)(u->md);
    if (u->sd) U(ReleaseDC)(NULL, u->sd);
}
static BOOL do_ulw(HWND h, struct ulw *u) {
    POINT src = {0, 0}; SIZE sz = {4, 4}; BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, 0 };
    return U(UpdateLayeredWindow)(h, u->sd, NULL, &sz, u->md, &src, 0, &bf, ULW_ALPHA);
}
TEST(ulw_first_then_get_fails_unconfigured) {
    NEED(); struct ulw u = {0}; if (!ulw_open(&u)) { ulw_close(&u); t_skip("no dc"); return; }
    MKLAY(h); CHECK(do_ulw(h, &u)); expect_noget(h, 0); kill(h); ulw_close(&u);
}
TEST(ulw_first_then_set_succeeds) {
    NEED(); struct ulw u = {0}; if (!ulw_open(&u)) { ulw_close(&u); t_skip("no dc"); return; }
    MKLAY(h); CHECK(do_ulw(h, &u)); struct st s = conf(h, 1, 2, 3); expect_state(h, &s); kill(h); ulw_close(&u);
}
TEST(set_first_then_ulw_fails_87_state_kept) {
    NEED(); struct ulw u = {0}; if (!ulw_open(&u)) { ulw_close(&u); t_skip("no dc"); return; }
    MKLAY(h); struct st s = conf(h, 1, 2, 3);
    SetLastError(STALE); CHECK(!do_ulw(h, &u)); EXPECT_ERR(87); expect_state(h, &s);
    kill(h); ulw_close(&u);
}
TEST(ulw_after_ulw_and_set_fails_87) {
    NEED(); struct ulw u = {0}; if (!ulw_open(&u)) { ulw_close(&u); t_skip("no dc"); return; }
    MKLAY(h); CHECK(do_ulw(h, &u)); struct st s = conf(h, 3, 4, 3);
    SetLastError(STALE); CHECK(!do_ulw(h, &u)); EXPECT_ERR(87); expect_state(h, &s); kill(h); ulw_close(&u);
}
TEST(ulw_on_nonlayered_fails_87) {
    NEED(); struct ulw u = {0}; if (!ulw_open(&u)) { ulw_close(&u); t_skip("no dc"); return; }
    HWND h = top(0); if (!h) { t_skip("no window"); ulw_close(&u); return; }
    SetLastError(STALE); CHECK(!do_ulw(h, &u)); EXPECT_ERR(87); kill(h); ulw_close(&u);
}
TEST(ulw_after_style_cycle_note) {
    NEED(); struct ulw u = {0}; if (!ulw_open(&u)) { ulw_close(&u); t_skip("no dc"); return; }
    MKLAY(h); conf(h, 1, 2, 3); del_layered(h); add_layered(h);
    BOOL r = do_ulw(h, &u); t_note("ulw after Set, style cleared and re-added: %d", r);
    kill(h); ulw_close(&u);
}

/* ===== deterministic messages (counted per window) =================================================== */
static void msgs_expect(const char *tag, int paint_min, int styl) {
    CHECKF(cnt[WM_PAINT] >= paint_min, "%s: WM_PAINT %d", tag, cnt[WM_PAINT]);
    CHECKF(cnt[WM_STYLECHANGED] == styl && cnt[WM_STYLECHANGING] == styl, "%s: STYLECHANGED %d/%d", tag, cnt[WM_STYLECHANGING], cnt[WM_STYLECHANGED]);
}
TEST(msg_set_posts_nothing_synchronously) {
    NEED(); MKLAY(h); op_show(h); watch(h); CHECK(nset(h, 0, 100, LWA_A));
    EXPECT_EQ(cnt[WM_PAINT] + cnt[WM_ERASEBKGND] + cnt[WM_NCPAINT] + cnt[WM_STYLECHANGED] + cnt[WM_WINDOWPOSCHANGED] + cnt[WM_MOVE] + cnt[WM_SIZE], 0);
    kill(h);
}
TEST(msg_first_set_on_shown_window_paints) {
    NEED(); MKLAY(h); op_show(h); watch(h); CHECK(nset(h, 0, 100, LWA_A)); pump();
    msgs_expect("first set", 1, 0); CHECK(cnt[WM_ERASEBKGND] >= 1); CHECK(cnt[WM_NCPAINT] >= 1);
    EXPECT_EQ(cnt[WM_WINDOWPOSCHANGED], 0); kill(h);
}
TEST(msg_repeated_set_does_not_repaint) {
    NEED(); MKLAY(h); op_show(h); CHECK(nset(h, 0, 100, LWA_A)); watch(h);
    CHECK(nset(h, 0, 100, LWA_A)); pump(); EXPECT_EQ(cnt[WM_PAINT], 0);
    CHECK(nset(h, 0, 101, LWA_A)); pump(); EXPECT_EQ(cnt[WM_PAINT], 0);
    CHECK(nset(h, 0x11, 101, LWA_K)); pump(); EXPECT_EQ(cnt[WM_PAINT], 0);
    EXPECT_EQ(cnt[WM_STYLECHANGED], 0); EXPECT_EQ(cnt[WM_ERASEBKGND], 0); kill(h);
}
TEST(msg_set_sends_no_style_or_pos_messages) {
    NEED(); MKLAY(h); op_show(h); watch(h);
    for (int f = 0; f < 4; f++) { CHECK(nset(h, 1, 2, f)); }
    pump(); EXPECT_EQ(cnt[WM_STYLECHANGED], 0); EXPECT_EQ(cnt[WM_STYLECHANGING], 0);
    EXPECT_EQ(cnt[WM_WINDOWPOSCHANGED], 0); EXPECT_EQ(cnt[WM_WINDOWPOSCHANGING], 0); EXPECT_EQ(cnt[WM_MOVE], 0); EXPECT_EQ(cnt[WM_SIZE], 0);
    kill(h);
}
TEST(msg_style_clear_and_readd_send_one_pair_each) {
    NEED(); MKLAY(h); op_show(h); conf(h, 0, 100, LWA_A); watch(h);
    del_layered(h); pump(); msgs_expect("clear", 0, 1);
    memset(cnt, 0, sizeof cnt); add_layered(h); pump(); msgs_expect("readd", 0, 1);
    kill(h);
}
TEST(msg_set_after_readd_sends_no_style_message) {
    NEED(); MKLAY(h); op_show(h); conf(h, 0, 100, LWA_A); del_layered(h); add_layered(h); watch(h);
    CHECK(nset(h, 0, 100, LWA_A)); pump(); EXPECT_EQ(cnt[WM_STYLECHANGED], 0); kill(h);
}
TEST(msg_other_ex_bit_change_sends_style_pair) {
    NEED(); MKLAY(h); op_show(h); conf(h, 0, 100, LWA_A); watch(h);
    set_xs(h, xs(h) | WS_EX_TRANSPARENT); pump(); msgs_expect("transparent on", 0, 1);
    memset(cnt, 0, sizeof cnt); set_xs(h, xs(h) & ~WS_EX_TRANSPARENT); pump(); msgs_expect("transparent off", 0, 1);
    kill(h);
}
TEST(msg_move_and_size_one_poschanged_each) {
    NEED(); MKLAY(h); op_show(h); conf(h, 0, 100, 3); watch(h);
    op_movesize(h); pump(); EXPECT_EQ(cnt[WM_WINDOWPOSCHANGED], 1); EXPECT_EQ(cnt[WM_MOVE], 1); EXPECT_EQ(cnt[WM_SIZE], 1);
    memset(cnt, 0, sizeof cnt); U(SetWindowPos)(h, NULL, -17000, -17000, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE); pump();
    EXPECT_EQ(cnt[WM_WINDOWPOSCHANGED], 1); EXPECT_EQ(cnt[WM_MOVE], 1); EXPECT_EQ(cnt[WM_SIZE], 0);
    kill(h);
}
TEST(msg_hide_show_repaints_configured_window) {
    NEED(); MKLAY(h); op_show(h); conf(h, 0, 100, 3); pump(); watch(h);
    op_hide(h); op_show(h); pump(); CHECK(cnt[WM_PAINT] >= 1); EXPECT_EQ(cnt[WM_WINDOWPOSCHANGED], 2); kill(h);
}
TEST(msg_invalidate_paints_configured_window) {
    NEED(); MKLAY(h); op_show(h); conf(h, 0, 100, 3); pump(); watch(h);
    U(InvalidateRect)(h, NULL, TRUE); pump(); EXPECT_EQ(cnt[WM_PAINT], 1); CHECK(cnt[WM_ERASEBKGND] >= 1); kill(h);
}
TEST(msg_hidden_window_set_notes) {
    NEED(); MKLAY(h); watch(h); CHECK(nset(h, 0, 100, LWA_A)); pump();
    t_note("hidden window first Set: paint>0 %d, style messages %d", cnt[WM_PAINT] > 0, cnt[WM_STYLECHANGED]);
    EXPECT_EQ(cnt[WM_STYLECHANGED], 0); EXPECT_EQ(cnt[WM_WINDOWPOSCHANGED], 0); kill(h);
}
TEST(msg_plain_window_made_layered_then_set) {
    NEED(); HWND h = top(0); if (!h) { t_skip("no window"); return; }
    op_show(h); pump(); watch(h); add_layered(h); pump(); msgs_expect("gain", 0, 1);
    memset(cnt, 0, sizeof cnt); CHECK(nset(h, 0, 128, LWA_A)); pump(); EXPECT_EQ(cnt[WM_STYLECHANGED], 0);
    kill(h);
}
TEST(msg_unconfigured_layered_show_then_set) {
    NEED(); MKLAY(h); watch(h); op_show(h); pump();
    EXPECT_EQ(cnt[WM_WINDOWPOSCHANGED], 1); EXPECT_EQ(cnt[WM_STYLECHANGED], 0);
    memset(cnt, 0, sizeof cnt); CHECK(nset(h, 0, 128, LWA_A)); pump(); CHECK(cnt[WM_PAINT] >= 1); kill(h);
}

/* ===== hit testing and shadow class ================================================================== */
TEST(hittest_alpha_zero_and_full_conditional) {
    NEED(); REQUIRE(U(WindowFromPoint)); MKLAY(h);
    U(SetWindowPos)(h, HWND_TOPMOST, 100, 100, 50, 50, SWP_NOACTIVATE | SWP_SHOWWINDOW); pump();
    POINT pt = { 110, 110 };
    CHECK(nset(h, 0, 255, LWA_A)); BOOL full = U(WindowFromPoint)(pt) == h;
    CHECK(nset(h, 0, 0, LWA_A)); BOOL zero = U(WindowFromPoint)(pt) == h;
    t_note("opaque window found at point: %d; alpha 0 window found at point: %d", full, zero);
    if (!full) t_skip("the probe point is not reachable on this display");
    else CHECK(!zero);
    kill(h);
}
TEST(hittest_message_unaffected_by_attributes) {
    NEED(); MKLAY(h); conf(h, 0, 0, LWA_A);
    LRESULT r = U(SendMessageA)(h, WM_NCHITTEST, 0, MAKELPARAM(-19990, -19990));
    t_note("WM_NCHITTEST at an inside point with alpha 0: %ld", (long)r);
    EXPECT_EQ(r, HTCLIENT); kill(h);
}
TEST(dropshadow_class_set_get) {
    NEED(); HWND h = mk(WS_EX_LAYERED, WS_POPUP, "xlsq_shadow", NULL); if (!h) { t_skip("no window"); return; }
    struct st s = conf(h, 0, 0x80, LWA_A); expect_state(h, &s);
    CHECK(nset(h, 0, 0, LWA_A)); model_set(&s, 0, 0, LWA_A); expect_state(h, &s);
    CHECK(nset(h, 0, 255, LWA_A)); model_set(&s, 0, 255, LWA_A); expect_state(h, &s);
    op_show(h); pump(); expect_state(h, &s);
    del_layered(h); expect_noget(h, 87); add_layered(h); expect_state(h, &vis_default);
    kill(h);
}
TEST(dropshadow_class_colorkey_only) {
    NEED(); HWND h = mk(WS_EX_LAYERED, WS_POPUP, "xlsq_shadow", NULL); if (!h) { t_skip("no window"); return; }
    struct st s = conf(h, 0x00FF00, 0x40, LWA_K); expect_state(h, &s); kill(h);
}

/* ===== additional window property changes that must leave the stored state alone ====================== */
static LRESULT CALLBACK wp2(HWND h, UINT m, WPARAM w, LPARAM l) { return U(DefWindowProcA)(h, m, w, l); }
TEST(window_region_keeps_state) {
    NEED(); MKLAY(h); struct st s = conf(h, 0x112233, 0x66, LWA_K | LWA_A);
    HRGN r = UIN("gdi32.dll", CreateRectRgn) ? UIN("gdi32.dll", CreateRectRgn)(0, 0, 10, 10) : NULL;
    if (r) { U(SetWindowRgn)(h, r, FALSE); expect_state(h, &s); U(SetWindowRgn)(h, NULL, FALSE); expect_state(h, &s); }
    else t_skip("no region");
    kill(h);
}
TEST(style_visible_bit_via_setwindowlong_keeps_state) {
    NEED(); MKLAY(h); struct st s = conf(h, 0, 0x33, LWA_A);
    U(SetWindowLongA)(h, GWL_STYLE, ss(h) | WS_VISIBLE); pump(); expect_state(h, &s);
    U(SetWindowLongA)(h, GWL_STYLE, ss(h) & ~WS_VISIBLE); pump(); expect_state(h, &s);
    kill(h);
}
TEST(style_disabled_bit_keeps_state) {
    NEED(); MKLAY(h); struct st s = conf(h, 0x010203, 0x44, LWA_K | LWA_A);
    U(SetWindowLongA)(h, GWL_STYLE, ss(h) | WS_DISABLED); expect_state(h, &s);
    CHECK(nset(h, 0, 0x45, LWA_A)); model_set(&s, 0, 0x45, LWA_A); expect_state(h, &s);
    U(SetWindowLongA)(h, GWL_STYLE, ss(h) & ~WS_DISABLED); expect_state(h, &s);
    kill(h);
}
TEST(subclassing_keeps_state) {
    NEED(); MKLAY(h); struct st s = conf(h, 0x0A0B0C, 0x99, LWA_K | LWA_A);
    LONG_PTR old = U(SetWindowLongPtrA)(h, GWLP_WNDPROC, (LONG_PTR)wp2);
    CHECK(old != 0); expect_state(h, &s);
    U(SetWindowLongPtrA)(h, GWLP_WNDPROC, old); expect_state(h, &s);
    kill(h);
}
TEST(layout_rtl_toggle_keeps_state) {
    NEED(); MKLAY(h); struct st s = conf(h, 0, 0x21, LWA_A);
    set_xs(h, xs(h) | WS_EX_LAYOUTRTL); CHECK(xs(h) & WS_EX_LAYOUTRTL); expect_state(h, &s);
    set_xs(h, xs(h) & ~WS_EX_LAYOUTRTL); CHECK(!(xs(h) & WS_EX_LAYOUTRTL)); expect_state(h, &s);
    kill(h);
}
TEST(composited_bit_keeps_state) {
    NEED(); MKLAY(h); struct st s = conf(h, 0, 0x22, LWA_A);
    set_xs(h, xs(h) | WS_EX_COMPOSITED); expect_state(h, &s);
    set_xs(h, xs(h) & ~WS_EX_COMPOSITED); expect_state(h, &s);
    kill(h);
}
TEST(created_with_layered_and_transparent_set_get) {
    NEED(); HWND h = top(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE);
    if (!h) { t_skip("no window"); return; }
    struct st s = conf(h, 0x7F7F7F, 0x7F, LWA_K | LWA_A); expect_state(h, &s);
    EXPECT_EQ(xs(h) & (WS_EX_LAYERED | WS_EX_TRANSPARENT), (LONG)(WS_EX_LAYERED | WS_EX_TRANSPARENT));
    kill(h);
}
TEST(display_affinity_keeps_state_when_supported) {
    NEED(); MKLAY(h); struct st s = conf(h, 0, 0x55, LWA_A);
    BOOL r = U(SetWindowDisplayAffinity) ? U(SetWindowDisplayAffinity)(h, 0) : FALSE;
    t_note("SetWindowDisplayAffinity(NONE) result %d", r != 0);
    expect_state(h, &s); kill(h);
}
TEST(class_style_change_keeps_state) {
    NEED(); MKLAY(h); struct st s = conf(h, 0x00FFFF, 0x77, LWA_K | LWA_A);
    ULONG_PTR old = U(GetClassLongPtrA)(h, GCL_STYLE);
    U(SetClassLongPtrA)(h, GCL_STYLE, old | CS_HREDRAW | CS_VREDRAW);
    expect_state(h, &s);
    U(SetClassLongPtrA)(h, GCL_STYLE, old); expect_state(h, &s);
    kill(h);
}
