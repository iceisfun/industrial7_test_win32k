/* MoveToEx / NtGdiMoveTo: the previous current position is returned through the optional output in logical
 * coordinates, the new position is stored without clipping or range checks, a failed call leaves the output
 * untouched, and a metafile DC tracks the position as well. Route (b) drains the batch before and after and
 * runs the same sequence with GdiSetBatchLimit(1). */
#include "xn.h"

#define NEED_M() do { if (!U(MoveToEx) || !UN(NtGdiMoveTo) || !U(GetCurrentPositionEx) || !U(GdiFlush)) { t_skip("exports missing"); return; } } while (0)
static BOOL mv(int route, HDC dc, int x, int y, POINT *old) { return route == ROUTE_NT ? UN(NtGdiMoveTo)(dc, x, y, old) : U(MoveToEx)(dc, x, y, old); }

static void seq(int route) {
    NEED_M(); FX(f, 32, 32); POINT p = { 7, 7 }, c;
    DWORD lim = xn_limit_set(route);
    CHECK(mv(route, f.dc, 10, 20, &p)); CHECKF(p.x == 0 && p.y == 0, "initial old point (%ld,%ld)", p.x, p.y);
    CHECK(mv(route, f.dc, 1, 2, &p));   CHECKF(p.x == 10 && p.y == 20, "second old point (%ld,%ld)", p.x, p.y);
    CHECK(mv(route, f.dc, -5, 100000, &p)); CHECKF(p.x == 1 && p.y == 2, "third old point (%ld,%ld)", p.x, p.y);
    xn_limit_restore(route, lim);
    CHECK(U(GetCurrentPositionEx)(f.dc, &c)); CHECKF(c.x == -5 && c.y == 100000, "position (%ld,%ld)", c.x, c.y);
    fx_close(&f);
}
XT(moveto_sequence_nt)           { seq(ROUTE_NT); }
XT(moveto_sequence_gdi32)        { seq(ROUTE_GDI); }
XT(moveto_sequence_gdi32_batch_limit_one) { seq(ROUTE_GDI_B1); }

static void null_out(int route) {
    NEED_M(); FX(f, 32, 32); POINT c;
    DWORD lim = xn_limit_set(route);
    CHECK(mv(route, f.dc, 12, 13, NULL));
    xn_limit_restore(route, lim);
    CHECK(U(GetCurrentPositionEx)(f.dc, &c)); CHECK(c.x == 12 && c.y == 13);
    fx_close(&f);
}
XT(moveto_null_output_nt)        { null_out(ROUTE_NT); }
XT(moveto_null_output_gdi32)     { null_out(ROUTE_GDI); }
XT(moveto_null_output_gdi32_batch_limit_one) { null_out(ROUTE_GDI_B1); }

static void extremes(int route) {
    NEED_M(); FX(f, 32, 32); POINT p = { 7, 7 }, c;
    DWORD lim = xn_limit_set(route);
    CHECK(mv(route, f.dc, 0x7fffffff, -0x7fffffff - 1, &p));
    CHECK(mv(route, f.dc, 3, 4, &p));
    xn_limit_restore(route, lim);
    CHECKF(p.x == 0x7fffffff && p.y == -0x7fffffff - 1, "extreme position round-trips (%ld,%ld)", p.x, p.y);
    CHECK(U(GetCurrentPositionEx)(f.dc, &c)); CHECK(c.x == 3 && c.y == 4);
    fx_close(&f);
}
XT(moveto_extreme_coordinates_nt)    { extremes(ROUTE_NT); }
XT(moveto_extreme_coordinates_gdi32) { extremes(ROUTE_GDI); }
XT(moveto_extreme_coordinates_gdi32_batch_limit_one) { extremes(ROUTE_GDI_B1); }

XT(moveto_mixed_routes_share_one_position) {
    NEED_M(); FX(f, 32, 32); POINT p;
    CHECK(U(MoveToEx)(f.dc, 1, 2, NULL));
    CHECK(UN(NtGdiMoveTo)(f.dc, 3, 4, &p)); CHECK(p.x == 1 && p.y == 2);
    CHECK(U(MoveToEx)(f.dc, 5, 6, &p));     CHECK(p.x == 3 && p.y == 4);
    fx_close(&f);
}
XT(moveto_old_point_is_logical_under_scaled_mapping) {
    NEED_M(); if (!U(SetMapMode) || !U(SetWindowExtEx) || !U(SetViewportExtEx) || !U(SetWindowOrgEx)) { t_skip("exports missing"); return; }
    FX(f, 32, 32); POINT p = { 7, 7 }, c;
    U(SetMapMode)(f.dc, MM_ANISOTROPIC); U(SetWindowExtEx)(f.dc, 1, 1, NULL); U(SetViewportExtEx)(f.dc, 3, 3, NULL); U(SetWindowOrgEx)(f.dc, 2, 2, NULL);
    U(MoveToEx)(f.dc, 10, 20, NULL);
    CHECK(U(MoveToEx)(f.dc, 1, 2, &p)); CHECK(p.x == 10 && p.y == 20);
    CHECK(UN(NtGdiMoveTo)(f.dc, 11, 12, &p)); CHECK(p.x == 1 && p.y == 2);
    CHECK(U(GetCurrentPositionEx)(f.dc, &c)); CHECK(c.x == 11 && c.y == 12);
    fx_close(&f);
}
XT(moveto_unreadable_output_fails_nt) {
    NEED_M(); FX(f, 32, 32);
    CHECK(!UN(NtGdiMoveTo)(f.dc, 1, 1, (LPPOINT)(ULONG_PTR)0x10));
    fx_close(&f);
}
XT(moveto_position_is_per_dc) {
    NEED_M(); FX(a, 32, 32); FX(b, 32, 32); POINT p = { 7, 7 };
    U(MoveToEx)(a.dc, 9, 9, NULL);
    CHECK(U(MoveToEx)(b.dc, 1, 1, &p)); CHECK(p.x == 0 && p.y == 0);
    fx_close(&b); fx_close(&a);
}
XT(moveto_does_not_draw) {
    NEED_M(); FX(f, 32, 32);
    U(MoveToEx)(f.dc, 2, 2, NULL); U(MoveToEx)(f.dc, 20, 20, NULL); U(GdiFlush)();
    int black = 0; for (int i = 0; i < 32 * 32; i++) if ((f.bits[i] & 0xffffff) != 0xffffff) black++;
    CHECK(black == 0);
    fx_close(&f);
}
XT(moveto_ok_after_failed_call_on_null_dc) {
    NEED_M(); FX(f, 32, 32); POINT p = { 7, 7 };
    CHECK(!UN(NtGdiMoveTo)(NULL, 1, 1, &p));
    CHECK(UN(NtGdiMoveTo)(f.dc, 4, 5, &p)); CHECK(p.x == 0 && p.y == 0);
    fx_close(&f);
}
