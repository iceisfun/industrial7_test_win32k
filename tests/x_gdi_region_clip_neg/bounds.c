/* GetBoundsRect / SetBoundsRect on a memory DC: the returned flag word (DCB_RESET 1 = rectangle empty, DCB_ACCUMULATE 2
 * bit = rectangle non-empty, so 1 and 3), which flag bits the getter honours, the previous-state word returned by the
 * setter (DCB_ENABLE 4 / DCB_DISABLE 8 included), accumulation by drawing, and reset-on-read. */
#include "xn.h"

#define NEED_B() do { if (!U(GetBoundsRect) || !U(SetBoundsRect) || !U(PatBlt) || !U(GdiFlush) || !UN(NtGdiGetBoundsRect)) { t_skip("exports missing"); return; } } while (0)
static void dirty(HDC dc, int x, int y, int w, int h) { U(PatBlt)(dc, x, y, w, h, BLACKNESS); U(GdiFlush)(); }
static UINT gb(HDC dc, RECT *r, UINT fl) { memset(r, 0x55, sizeof *r); return U(GetBoundsRect)(dc, r, fl); }

XT(bounds_fresh_dc_is_reset_and_zero) { NEED_B(); FX(f, 64, 48); RECT r; EXPECT_EQ(gb(f.dc, &r, 0), 1); CHECK(box_is(&r, 0, 0, 0, 0)); fx_close(&f); }
XT(bounds_reset_flag_on_empty_returns_reset) { NEED_B(); FX(f, 64, 48); RECT r; EXPECT_EQ(gb(f.dc, &r, DCB_RESET), 1); CHECK(box_is(&r, 0, 0, 0, 0)); fx_close(&f); }
XT(bounds_first_setter_returns_reset_and_disabled) {
    NEED_B(); FX(f, 64, 48);
    EXPECT_EQ(U(SetBoundsRect)(f.dc, NULL, DCB_ENABLE), DCB_RESET | DCB_DISABLE);
    EXPECT_EQ(U(SetBoundsRect)(f.dc, NULL, DCB_DISABLE), DCB_RESET | DCB_ENABLE);
    fx_close(&f);
}
XT(bounds_getter_ignores_enable_accumulate_disable_flags) {
    NEED_B(); FX(f, 64, 48); RECT r;
    U(SetBoundsRect)(f.dc, NULL, DCB_ENABLE);
    static const UINT fl[] = { DCB_ACCUMULATE, DCB_ENABLE, DCB_DISABLE, 0xffffffff & ~DCB_RESET, 0x100 };
    for (unsigned i = 0; i < sizeof fl / sizeof fl[0]; i++) { EXPECT_EQ(gb(f.dc, &r, fl[i]), 1); CHECK(box_is(&r, 0, 0, 0, 0)); }
    fx_close(&f);
}
XT(bounds_getter_null_rect_fails) {
    NEED_B(); FX(f, 64, 48);
    EXPECT_EQ(U(GetBoundsRect)(f.dc, NULL, 0), 0);
    EXPECT_EQ(U(GetBoundsRect)(f.dc, NULL, DCB_RESET), 0);
    fx_close(&f);
}
XT(bounds_draw_while_enabled_accumulates) {
    NEED_B(); FX(f, 64, 48); RECT r;
    U(SetBoundsRect)(f.dc, NULL, DCB_ENABLE | DCB_RESET);
    dirty(f.dc, 5, 6, 10, 8);
    EXPECT_EQ(gb(f.dc, &r, 0), 3); CHECK(box_is(&r, 5, 6, 15, 14));
    dirty(f.dc, 20, 20, 5, 5);
    EXPECT_EQ(gb(f.dc, &r, 0), 3); CHECK(box_is(&r, 5, 6, 25, 25));
    fx_close(&f);
}
XT(bounds_reset_on_read_returns_old_then_empties) {
    NEED_B(); FX(f, 64, 48); RECT r;
    U(SetBoundsRect)(f.dc, NULL, DCB_ENABLE | DCB_RESET);
    dirty(f.dc, 5, 6, 10, 8);
    EXPECT_EQ(gb(f.dc, &r, DCB_RESET), 3); CHECK(box_is(&r, 5, 6, 15, 14));
    EXPECT_EQ(gb(f.dc, &r, 0), 1); CHECK(box_is(&r, 0, 0, 0, 0));
    fx_close(&f);
}
XT(bounds_draw_while_disabled_does_not_accumulate) {
    NEED_B(); FX(f, 64, 48); RECT r;
    U(SetBoundsRect)(f.dc, NULL, DCB_DISABLE | DCB_RESET);
    dirty(f.dc, 1, 1, 3, 3);
    EXPECT_EQ(gb(f.dc, &r, 0), 1); CHECK(box_is(&r, 0, 0, 0, 0));
    fx_close(&f);
}
XT(bounds_reenable_then_draw_accumulates) {
    NEED_B(); FX(f, 64, 48); RECT r;
    U(SetBoundsRect)(f.dc, NULL, DCB_DISABLE | DCB_RESET);
    dirty(f.dc, 1, 1, 3, 3);
    U(SetBoundsRect)(f.dc, NULL, DCB_ENABLE);
    dirty(f.dc, 1, 1, 3, 3);
    EXPECT_EQ(gb(f.dc, &r, 0), 3); CHECK(box_is(&r, 1, 1, 4, 4));
    fx_close(&f);
}
XT(bounds_nt_getter_ignores_all_flags) {
    NEED_B(); FX(f, 64, 48); RECT r;
    U(SetBoundsRect)(f.dc, NULL, DCB_RESET | DCB_ENABLE);
    static const UINT fl[] = { 0, 8, 0xff, 0x10000 };
    for (unsigned i = 0; i < 4; i++) { memset(&r, 0x55, sizeof r); EXPECT_EQ(UN(NtGdiGetBoundsRect)(f.dc, &r, fl[i]), 1); CHECK(box_is(&r, 0, 0, 0, 0)); }
    fx_close(&f);
}
XT(bounds_nt_getter_reset_flag_empties_after_read) {
    NEED_B(); FX(f, 64, 48); RECT r;
    U(SetBoundsRect)(f.dc, NULL, DCB_RESET | DCB_ENABLE);
    dirty(f.dc, 2, 3, 4, 5);
    EXPECT_EQ(UN(NtGdiGetBoundsRect)(f.dc, &r, DCB_RESET), 3); CHECK(box_is(&r, 2, 3, 6, 8));
    EXPECT_EQ(UN(NtGdiGetBoundsRect)(f.dc, &r, 0), 1);
    fx_close(&f);
}
XT(bounds_nt_getter_null_dc_fails) { NEED_B(); RECT r; EXPECT_EQ(UN(NtGdiGetBoundsRect)(NULL, &r, 0), 0); }
XT(bounds_setter_accumulate_rect_into_empty) {
    NEED_B(); FX(f, 64, 48); RECT r = { 1, 2, 30, 40 };
    U(SetBoundsRect)(f.dc, NULL, DCB_RESET | DCB_ENABLE);
    EXPECT_EQ(U(SetBoundsRect)(f.dc, &r, DCB_ACCUMULATE), DCB_RESET | DCB_ENABLE);
    EXPECT_EQ(gb(f.dc, &r, 0), 3); CHECK(box_is(&r, 1, 2, 30, 40));
    fx_close(&f);
}
XT(bounds_setter_accumulate_unions) {
    NEED_B(); FX(f, 64, 48); RECT r = { 1, 2, 30, 40 }, s = { 20, 30, 50, 45 };
    U(SetBoundsRect)(f.dc, NULL, DCB_RESET | DCB_ENABLE);
    U(SetBoundsRect)(f.dc, &r, DCB_ACCUMULATE);
    U(SetBoundsRect)(f.dc, &s, DCB_ACCUMULATE);
    EXPECT_EQ(gb(f.dc, &r, 0), 3); CHECK(box_is(&r, 1, 2, 50, 45));
    fx_close(&f);
}
XT(bounds_setter_inverted_accumulate_rect_is_ignored) {
    NEED_B(); FX(f, 64, 48); RECT r = { 1, 2, 30, 40 }, inv = { 50, 40, 10, 5 };
    U(SetBoundsRect)(f.dc, NULL, DCB_RESET | DCB_ENABLE);
    U(SetBoundsRect)(f.dc, &r, DCB_ACCUMULATE);
    EXPECT_EQ(U(SetBoundsRect)(f.dc, &inv, DCB_ACCUMULATE), DCB_RESET | DCB_ACCUMULATE | DCB_ENABLE);
    EXPECT_EQ(gb(f.dc, &r, 0), 3); CHECK(box_is(&r, 1, 2, 30, 40));
    fx_close(&f);
}
XT(bounds_setter_reset_ignores_rect) {
    NEED_B(); FX(f, 64, 48); RECT r = { 1, 2, 30, 40 };
    U(SetBoundsRect)(f.dc, NULL, DCB_RESET | DCB_ENABLE);
    U(SetBoundsRect)(f.dc, &r, DCB_ACCUMULATE);
    EXPECT_EQ(U(SetBoundsRect)(f.dc, &r, DCB_RESET), DCB_RESET | DCB_ACCUMULATE | DCB_ENABLE);
    EXPECT_EQ(gb(f.dc, &r, 0), 1); CHECK(box_is(&r, 0, 0, 0, 0));
    fx_close(&f);
}
XT(bounds_setter_zero_flags_returns_state_and_changes_nothing) {
    NEED_B(); FX(f, 64, 48); RECT r = { 1, 2, 30, 40 };
    U(SetBoundsRect)(f.dc, NULL, DCB_RESET | DCB_ENABLE);
    EXPECT_EQ(U(SetBoundsRect)(f.dc, &r, 0), DCB_RESET | DCB_ENABLE);
    EXPECT_EQ(gb(f.dc, &r, 0), 1);
    fx_close(&f);
}
XT(bounds_batched_draw_visible_after_flush_bracket_and_limit_one) {
    NEED_B(); if (!U(GdiSetBatchLimit)) { t_skip("GdiSetBatchLimit missing"); return; }
    FX(f, 64, 48);
    RECT a, b;
    U(SetBoundsRect)(f.dc, NULL, DCB_RESET | DCB_ENABLE);
    dirty(f.dc, 7, 8, 9, 10);
    UINT ua = gb(f.dc, &a, DCB_RESET);
    DWORD old = U(GdiSetBatchLimit)(1);
    U(PatBlt)(f.dc, 7, 8, 9, 10, BLACKNESS);
    UINT ub = gb(f.dc, &b, DCB_RESET);
    U(GdiSetBatchLimit)(old); U(GdiFlush)();
    CHECK(ua == 3 && ub == 3); CHECK(box_is(&a, 7, 8, 16, 18)); CHECK(box_is(&b, 7, 8, 16, 18));
    fx_close(&f);
}
XT(bounds_two_dcs_are_independent) {
    NEED_B(); FX(f, 64, 48); FX(g, 64, 48); RECT r;
    U(SetBoundsRect)(f.dc, NULL, DCB_RESET | DCB_ENABLE);
    U(SetBoundsRect)(g.dc, NULL, DCB_RESET | DCB_ENABLE);
    dirty(f.dc, 2, 2, 4, 4);
    EXPECT_EQ(gb(g.dc, &r, 0), 1);
    EXPECT_EQ(gb(f.dc, &r, 0), 3);
    fx_close(&g); fx_close(&f);
}
