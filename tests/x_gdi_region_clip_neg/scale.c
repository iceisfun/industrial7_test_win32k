/* ScaleWindowExtEx / NtGdiScaleWindowExtEx: new extent = old * num / den per axis (truncating, 32-bit wrap on overflow);
 * a zero numerator or denominator, or a result of zero, fails and leaves both the extent and the output size untouched;
 * negative factors flip the extent sign; fixed map modes accept the call without changing their extent; the previous
 * extent is returned through the optional output. Display-dependent extents are never printed, only compared. */
#include "xn.h"

#define NEED_S() do { if (!U(ScaleWindowExtEx) || !UN(NtGdiScaleWindowExtEx) || !U(SetMapMode) || !U(SetWindowExtEx) || !U(SetViewportExtEx) || !U(GetWindowExtEx) || !U(GetViewportExtEx)) { t_skip("exports missing"); return; } } while (0)
static void aniso(HDC dc) { U(SetMapMode)(dc, MM_ANISOTROPIC); U(SetWindowExtEx)(dc, 100, 50, NULL); U(SetViewportExtEx)(dc, 200, 100, NULL); }

struct srow { int xn, xd, yn, yd; int ok; int nx, ny; };
static void srow_run(const struct srow *w) {
    NEED_S(); FX(f, 16, 16);
    for (int route = 0; route < 3; route++) {
        aniso(f.dc);
        SIZE old = { 7, 7 }, cur;
        DWORD lim = xn_limit_set(route);
        BOOL r = route == ROUTE_NT ? UN(NtGdiScaleWindowExtEx)(f.dc, w->xn, w->xd, w->yn, w->yd, &old) : U(ScaleWindowExtEx)(f.dc, w->xn, w->xd, w->yn, w->yd, &old);
        xn_limit_restore(route, lim);
        U(GetWindowExtEx)(f.dc, &cur);
        CHECKF((r != 0) == w->ok, "route %d result %d want %d", route, r, w->ok);
        if (w->ok) { CHECKF(old.cx == 100 && old.cy == 50, "route %d old (%ld,%ld)", route, old.cx, old.cy); CHECKF(cur.cx == w->nx && cur.cy == w->ny, "route %d new (%ld,%ld) want (%d,%d)", route, cur.cx, cur.cy, w->nx, w->ny); }
        else { CHECKF(old.cx == 7 && old.cy == 7, "route %d output touched (%ld,%ld)", route, old.cx, old.cy); CHECKF(cur.cx == 100 && cur.cy == 50, "route %d extent changed (%ld,%ld)", route, cur.cx, cur.cy); }
    }
    fx_close(&f);
}
#define SR(name, xn, xd, yn, yd, ok, nx, ny) static const struct srow s_##name = { xn, xd, yn, yd, ok, nx, ny }; XT(scale_##name) { srow_run(&s_##name); }
SR(identity,         1, 1, 1, 1,  1, 100, 50)
SR(double_both,      2, 1, 2, 1,  1, 200, 100)
SR(half_x_three_quarter_y, 1, 2, 3, 4, 1, 50, 37)
SR(ratio_three_two_five_seven, 3, 2, 5, 7, 1, 150, 35)
SR(thousand_times,   1000, 1, 1000, 1, 1, 100000, 50000)
SR(zero_x_numerator, 0, 1, 1, 1,  0, 0, 0)
SR(zero_x_denominator, 1, 0, 1, 1, 0, 0, 0)
SR(zero_y_numerator, 1, 1, 0, 1,  0, 0, 0)
SR(zero_y_denominator, 1, 1, 1, 0, 0, 0, 0)
SR(negative_x,      -1, 1, 1, 1,  1, -100, 50)
SR(negative_x_denominator, 1, -1, 1, 1, 1, -100, 50)
SR(negative_y,       1, 1, -1, 1, 1, 100, -50)
SR(negative_y_denominator, 1, 1, 1, -1, 1, 100, -50)
SR(both_axes_negated, -1, -1, -1, -1, 1, 100, 50)
SR(x_overflow_wraps, 0x7fffffff, 1, 1, 1, 1, -100, 50)
SR(result_truncates_to_zero_x, 1, 1000, 1, 1000, 0, 0, 0)
SR(huge_x_denominator, 1, 0x7fffffff, 1, 1, 0, 0, 0)
SR(int_min_x_numerator, -0x7fffffff - 1, 1, 1, 1, 0, 0, 0)

XT(scale_null_output_is_allowed) {
    NEED_S(); FX(f, 16, 16); SIZE cur;
    aniso(f.dc);
    CHECK(U(ScaleWindowExtEx)(f.dc, 2, 1, 2, 1, NULL));
    U(GetWindowExtEx)(f.dc, &cur); CHECK(cur.cx == 200 && cur.cy == 100);
    fx_close(&f);
}
XT(scale_nt_null_output_is_allowed) {
    NEED_S(); FX(f, 16, 16); SIZE cur;
    aniso(f.dc);
    CHECK(UN(NtGdiScaleWindowExtEx)(f.dc, 2, 1, 2, 1, NULL));
    U(GetWindowExtEx)(f.dc, &cur); CHECK(cur.cx == 200 && cur.cy == 100);
    fx_close(&f);
}
XT(scale_nt_unreadable_output_fails) {
    NEED_S(); FX(f, 16, 16);
    aniso(f.dc);
    CHECK(!UN(NtGdiScaleWindowExtEx)(f.dc, 2, 1, 2, 1, (LPSIZE)(ULONG_PTR)0x10));
    fx_close(&f);
}
XT(scale_does_not_touch_viewport_extent) {
    NEED_S(); FX(f, 16, 16); SIZE v;
    aniso(f.dc);
    CHECK(U(ScaleWindowExtEx)(f.dc, 3, 1, 3, 1, NULL));
    U(GetViewportExtEx)(f.dc, &v); CHECK(v.cx == 200 && v.cy == 100);
    fx_close(&f);
}
XT(scale_applies_cumulatively) {
    NEED_S(); FX(f, 16, 16); SIZE cur, old;
    aniso(f.dc);
    CHECK(U(ScaleWindowExtEx)(f.dc, 2, 1, 2, 1, &old)); CHECK(old.cx == 100 && old.cy == 50);
    CHECK(U(ScaleWindowExtEx)(f.dc, 3, 1, 3, 1, &old)); CHECK(old.cx == 200 && old.cy == 100);
    U(GetWindowExtEx)(f.dc, &cur); CHECK(cur.cx == 600 && cur.cy == 300);
    fx_close(&f);
}
XT(scale_changes_point_mapping) {
    NEED_S(); if (!U(DPtoLP)) { t_skip("exports missing"); return; }
    FX(f, 16, 16); POINT p = { 40, 40 };
    aniso(f.dc);
    U(DPtoLP)(f.dc, &p, 1); CHECK(p.x == 20 && p.y == 20);
    U(ScaleWindowExtEx)(f.dc, 2, 1, 2, 1, NULL);
    p.x = 40; p.y = 40; U(DPtoLP)(f.dc, &p, 1); CHECK(p.x == 40 && p.y == 40);
    fx_close(&f);
}
static void fixed_mode(int mm) {
    NEED_S(); FX(f, 16, 16); SIZE a, b, old = { 7, 7 };
    U(SetMapMode)(f.dc, mm); U(GetWindowExtEx)(f.dc, &a);
    CHECK(U(ScaleWindowExtEx)(f.dc, 2, 1, 3, 1, &old));
    CHECKF(old.cx == a.cx && old.cy == a.cy, "old extent is the current extent");
    U(GetWindowExtEx)(f.dc, &b); CHECKF(b.cx == a.cx && b.cy == a.cy, "extent unchanged in a fixed mode");
    fx_close(&f);
}
XT(scale_fixed_mode_lometric)   { fixed_mode(MM_LOMETRIC); }
XT(scale_fixed_mode_himetric)   { fixed_mode(MM_HIMETRIC); }
XT(scale_fixed_mode_loenglish)  { fixed_mode(MM_LOENGLISH); }
XT(scale_fixed_mode_hienglish)  { fixed_mode(MM_HIENGLISH); }
XT(scale_fixed_mode_twips)      { fixed_mode(MM_TWIPS); }
XT(scale_isotropic_mode_scales_the_extent) {
    NEED_S(); FX(f, 16, 16); SIZE a, b;
    U(SetMapMode)(f.dc, MM_ISOTROPIC); U(GetWindowExtEx)(f.dc, &a);
    CHECK(U(ScaleWindowExtEx)(f.dc, 2, 1, 3, 1, NULL));
    U(GetWindowExtEx)(f.dc, &b); CHECKF(b.cx == a.cx * 2 && b.cy == a.cy * 3, "extent (%ld,%ld) from (%ld,%ld)", b.cx, b.cy, a.cx, a.cy);
    fx_close(&f);
}
XT(scale_zero_factor_in_fixed_mode_succeeds) {
    NEED_S(); FX(f, 16, 16);
    U(SetMapMode)(f.dc, MM_LOMETRIC);
    SIZE a, b;
    U(GetWindowExtEx)(f.dc, &a);
    CHECK(U(ScaleWindowExtEx)(f.dc, 0, 1, 1, 1, NULL));      /* the fixed modes never look at the factors */
    CHECK(U(ScaleWindowExtEx)(f.dc, 1, 1, 1, 0, NULL));
    U(GetWindowExtEx)(f.dc, &b); CHECK(a.cx == b.cx && a.cy == b.cy);
    fx_close(&f);
}
XT(scale_batch_limit_one_matches_default) {
    NEED_S(); if (!U(GdiSetBatchLimit)) { t_skip("GdiSetBatchLimit missing"); return; }
    FX(f, 16, 16); SIZE a, b;
    aniso(f.dc); U(GdiFlush)(); U(ScaleWindowExtEx)(f.dc, 3, 2, 5, 4, NULL); U(GdiFlush)(); U(GetWindowExtEx)(f.dc, &a);
    aniso(f.dc); DWORD o = U(GdiSetBatchLimit)(1); U(ScaleWindowExtEx)(f.dc, 3, 2, 5, 4, NULL); U(GdiSetBatchLimit)(o); U(GdiFlush)(); U(GetWindowExtEx)(f.dc, &b);
    CHECK(a.cx == b.cx && a.cy == b.cy); CHECK(a.cx == 150 && a.cy == 62);
    fx_close(&f);
}
XT(scale_after_set_map_mode_text_resets_to_fixed_extent) {
    NEED_S(); FX(f, 16, 16); SIZE a;
    aniso(f.dc); U(ScaleWindowExtEx)(f.dc, 2, 1, 2, 1, NULL);
    U(SetMapMode)(f.dc, MM_TEXT); U(GetWindowExtEx)(f.dc, &a);
    CHECK(a.cx == 1 && a.cy == 1);
    fx_close(&f);
}
