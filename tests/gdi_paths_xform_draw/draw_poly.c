/* Poly* primitives, polygon fill modes, binary raster ops, PolyPatBlt and bounds rectangles.
 *
 * gdi32full!Polygon / PolyPolyline forward to NtGdiPolyPolyDraw(hdc, apt, counts, nPolys, iFunc) with iFunc 1 = polygon
 * and 2 = polyline (analysis of both callers). gdi32full!PolyPatBlt batches POLYPATBLT entries of 0x18 bytes
 * {x, y, cx, cy, hBrush} when Count < 0xaaa and Mode == 0, otherwise calls NtGdiPolyPatBlt directly. SetBoundsRect /
 * GetBoundsRect mask the flags with 0xffff3fff before calling the kernel; the kernel returns the previous
 * ENABLE/DISABLE (4/8) and emptiness (bit 2 = non-empty) state.
 */
#include "xd.h"

#define NEED_POLY() do { REQUIRE(U(MoveToEx)); REQUIRE(U(LineTo)); REQUIRE(U(GetCurrentPositionEx)); REQUIRE(U(CreateCompatibleDC)); \
    REQUIRE(U(CreateDIBSection)); REQUIRE(U(GdiFlush)); } while (0)
#define EXPECT_PT(p, X, Y) do { EXPECT_INT((p).x, X); EXPECT_INT((p).y, Y); } while (0)

/* ---- Polygon / Polyline -------------------------------------------------------------------------------------- */
XT(poly_polygon_rect_fill) {
    gdi_sel_t s = {0}; static const POINT pt[4] = { {10,10}, {30,10}, {30,24}, {10,24} };
    int x, y, ok = 1;
    NEED_POLY(); REQUIRE(U(Polygon));
    FX(f, 48, 40);
    sel_pen(f.dc, &s, COL_RED, 1); sel_brush(f.dc, &s, COL_BLUE);
    CHECK(U(Polygon)(f.dc, pt, 4));
    for (y = 11; y <= 22; y++) for (x = 11; x <= 28; x++) if (fx_px(&f, x, y) != PX_BLUE) ok = 0;
    CHECK(ok);
    EXPECT_EQ(fx_px(&f, 10, 10), PX_RED);                   /* the first vertex is where the pen starts */
    EXPECT_EQ(fx_px(&f, 9, 17), PX_WHITE); EXPECT_EQ(fx_px(&f, 17, 9), PX_WHITE);
    EXPECT_EQ(fx_px(&f, 32, 17), PX_WHITE); EXPECT_EQ(fx_px(&f, 17, 26), PX_WHITE);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(poly_polygon_closing_edge_drawn) {
    gdi_sel_t s = {0}; static const POINT pt[3] = { {10,10}, {40,10}, {40,40} };
    NEED_POLY(); REQUIRE(U(Polygon));
    FX(f, 64, 64);
    sel_pen(f.dc, &s, COL_RED, 1); sel_nullbrush(f.dc, &s);
    CHECK(U(Polygon)(f.dc, pt, 3));
    EXPECT_EQ(fx_px(&f, 25, 25), PX_RED);                   /* implicit closing edge (40,40) -> (10,10) */
    EXPECT_EQ(fx_px(&f, 25, 10), PX_RED);
    EXPECT_EQ(fx_px(&f, 40, 25), PX_RED);
    EXPECT_EQ(fx_px(&f, 25, 15), PX_WHITE);                 /* NULL brush: interior untouched */
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(poly_polygon_degenerate_counts) {
    gdi_sel_t s = {0}; static const POINT pt[4] = { {10,10}, {30,10}, {30,24}, {10,24} };
    int n;
    NEED_POLY(); REQUIRE(U(Polygon));
    FX(f, 48, 40);
    sel_pen(f.dc, &s, COL_RED, 1); sel_brush(f.dc, &s, COL_BLUE);
    for (n = 0; n <= 2; n++) {
        SET_ERR(0);
        t_note("Polygon with %d point(s): %d (err %lu)", n, (int)U(Polygon)(f.dc, pt, n), GetLastError());
    }
    SET_ERR(0);
    t_note("Polygon with -1 points: %d", (int)U(Polygon)(f.dc, pt, -1));
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(poly_polygon_does_not_move_position) {
    gdi_sel_t s = {0}; static const POINT pt[3] = { {10,10}, {40,10}, {40,40} }; POINT cur;
    NEED_POLY(); REQUIRE(U(Polygon)); REQUIRE(U(Polyline)); REQUIRE(U(PolyPolygon)); REQUIRE(U(PolyPolyline)); REQUIRE(U(PolyBezier));
    FX(f, 64, 64);
    sel_pen(f.dc, &s, COL_RED, 1);
    U(MoveToEx)(f.dc, 3, 4, NULL);
    CHECK(U(Polygon)(f.dc, pt, 3));
    CHECK(U(Polyline)(f.dc, pt, 3));
    { static const INT c[1] = { 3 }; static const DWORD d[1] = { 3 };
      CHECK(U(PolyPolygon)(f.dc, pt, c, 1)); CHECK(U(PolyPolyline)(f.dc, pt, d, 1)); }
    { static const POINT bz[4] = { {10,10},{20,0},{30,20},{40,10} }; CHECK(U(PolyBezier)(f.dc, bz, 4)); }
    CHECK(U(GetCurrentPositionEx)(f.dc, &cur));
    EXPECT_PT(cur, 3, 4);                                   /* none of these use or update the current position */
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(poly_polyline_open_chain_exact) {
    gdi_sel_t s = {0}; static const POINT pt[3] = { {10,10}, {20,10}, {20,20} };
    int i;
    NEED_POLY(); REQUIRE(U(Polyline));
    FX(f, 48, 40);
    sel_pen(f.dc, &s, COL_RED, 1);
    CHECK(U(Polyline)(f.dc, pt, 3));
    for (i = 10; i < 20; i++) EXPECT_EQ(fx_px(&f, i, 10), PX_RED);
    for (i = 10; i < 20; i++) EXPECT_EQ(fx_px(&f, 20, i), PX_RED);
    EXPECT_EQ(fx_px(&f, 20, 20), PX_WHITE);                 /* only the final vertex is excluded */
    EXPECT_INT(fx_count(&f, PX_RED), 20);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(poly_polyline_two_points_equals_lineto) {
    gdi_sel_t s = {0}; static const POINT pt[2] = { {5,7}, {35,22} };
    fx_t a;
    NEED_POLY(); REQUIRE(U(Polyline));
    FX(f, 48, 40);
    if (!fx_open(&a, 48, 40)) { t_skip("fixture"); fx_close(&f); return; }
    sel_pen(f.dc, &s, COL_RED, 1);
    CHECK(U(Polyline)(f.dc, pt, 2));
    { gdi_sel_t s2 = {0}; sel_pen(a.dc, &s2, COL_RED, 1); U(MoveToEx)(a.dc, 5, 7, NULL); U(LineTo)(a.dc, 35, 22); sel_done(a.dc, &s2); }
    EXPECT_INT(fx_diff(&f, &a), 0);
    CHECK(fx_count(&f, PX_RED) > 20);
    sel_done(f.dc, &s);
    fx_close(&f); fx_close(&a);
}

XT(poly_polyline_degenerate_counts) {
    static const POINT pt[2] = { {5,7}, {35,22} };
    int n;
    NEED_POLY(); REQUIRE(U(Polyline));
    FX(f, 48, 40);
    for (n = 0; n <= 1; n++) { SET_ERR(0); t_note("Polyline with %d point(s): %d (err %lu)", n, (int)U(Polyline)(f.dc, pt, n), GetLastError()); }
    SET_ERR(0);
    t_note("Polyline with -1 points: %d", (int)U(Polyline)(f.dc, pt, -1));
    fx_close(&f);
}

XT(poly_polylineto_moves_position) {
    gdi_sel_t s = {0}; static const POINT pt[2] = { {30,10}, {30,30} }; POINT cur;
    NEED_POLY(); REQUIRE(U(PolylineTo));
    FX(f, 48, 48);
    sel_pen(f.dc, &s, COL_RED, 1);
    U(MoveToEx)(f.dc, 10, 10, NULL);
    CHECK(U(PolylineTo)(f.dc, pt, 2));
    CHECK(U(GetCurrentPositionEx)(f.dc, &cur)); EXPECT_PT(cur, 30, 30);
    EXPECT_EQ(fx_px(&f, 10, 10), PX_RED); EXPECT_EQ(fx_px(&f, 29, 10), PX_RED);
    EXPECT_EQ(fx_px(&f, 30, 10), PX_RED); EXPECT_EQ(fx_px(&f, 30, 29), PX_RED);
    EXPECT_EQ(fx_px(&f, 30, 30), PX_WHITE);
    EXPECT_INT(fx_count(&f, PX_RED), 40);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(poly_polyline_to_invalid_hdc) {
    static const POINT pt[2] = { {30,10}, {30,30} };
    NEED_POLY(); REQUIRE(U(PolylineTo)); REQUIRE(U(Polyline)); REQUIRE(U(Polygon)); REQUIRE(U(PolyBezier)); REQUIRE(U(PolyBezierTo));
    CHECK(!U(PolylineTo)(NULL, pt, 2));
    CHECK(!U(Polyline)(NULL, pt, 2));
    CHECK(!U(Polygon)(NULL, pt, 2));
    CHECK(!U(PolyBezierTo)(NULL, pt, 3));
    CHECK(!U(PolyBezier)(NULL, pt, 4));
}

XT(poly_polybezier_collinear_is_straight) {
    gdi_sel_t s = {0}; static const POINT pt[4] = { {10,10}, {20,10}, {30,10}, {40,10} };
    int x, l, t, r, b;
    NEED_POLY(); REQUIRE(U(PolyBezier));
    FX(f, 64, 32);
    sel_pen(f.dc, &s, COL_RED, 1);
    CHECK(U(PolyBezier)(f.dc, pt, 4));
    fx_bbox(&f, PX_RED, &l, &t, &r, &b);
    EXPECT_INT(t, 10); EXPECT_INT(b, 10);                   /* never leaves the row of the control points */
    EXPECT_INT(l, 10);
    CHECK(r >= 38 && r <= 40);
    for (x = 10; x < 38; x++) EXPECT_EQ(fx_px(&f, x, 10), PX_RED);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(poly_polybezier_curve_stays_in_hull) {
    gdi_sel_t s = {0}; static const POINT pt[4] = { {10,40}, {20,0}, {50,0}, {60,40} };
    int l, t, r, b, n;
    NEED_POLY(); REQUIRE(U(PolyBezier));
    FX(f, 80, 64);
    sel_pen(f.dc, &s, COL_RED, 1);
    CHECK(U(PolyBezier)(f.dc, pt, 4));
    n = fx_bbox(&f, PX_RED, &l, &t, &r, &b);
    t_note("bezier: %d px bbox (%d,%d)-(%d,%d)", n, l, t, r, b);
    CHECK(n > 40);
    CHECK(l >= 10 && r <= 60 && t >= 0 && b <= 40);        /* inside the control polygon's bounding box */
    CHECK(t >= 5 && t <= 14);                               /* apex of this curve is at y = 40 - 0.75*40 = 10 */
    CHECK(fx_near(&f, 10, 40, 1, PX_RED));
    CHECK(fx_near(&f, 35, 10, 2, PX_RED));
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(poly_polybezierto_moves_position) {
    gdi_sel_t s = {0}; static const POINT pt[3] = { {20,0}, {30,20}, {40,10} }; POINT cur;
    NEED_POLY(); REQUIRE(U(PolyBezierTo));
    FX(f, 64, 32);
    sel_pen(f.dc, &s, COL_RED, 1);
    U(MoveToEx)(f.dc, 10, 10, NULL);
    CHECK(U(PolyBezierTo)(f.dc, pt, 3));
    CHECK(U(GetCurrentPositionEx)(f.dc, &cur)); EXPECT_PT(cur, 40, 10);
    EXPECT_EQ(fx_px(&f, 10, 10), PX_RED);
    CHECK(fx_count(&f, PX_RED) > 20);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(poly_polybezierto_bad_counts) {
    static const POINT pt[6] = { {20,0}, {30,20}, {40,10}, {50,0}, {60,20}, {70,10} };
    int n;
    NEED_POLY(); REQUIRE(U(PolyBezierTo));
    FX(f, 80, 32);
    for (n = 0; n <= 6; n++) {
        BOOL r = U(PolyBezierTo)(f.dc, pt, n);
        if (n == 3 || n == 6) CHECKF(r, "count %d rejected", n);
        else if (n == 0) t_note("PolyBezierTo with 0 points: %d", (int)r);
        else CHECKF(!r, "count %d accepted", n);              /* needs a multiple of three */
    }
    fx_close(&f);
}

XT(poly_polydraw_lines_and_position) {
    gdi_sel_t s = {0}; static const POINT pt[3] = { {5,5}, {25,5}, {25,20} };
    static const BYTE ty[3] = { PT_MOVETO, PT_LINETO, PT_LINETO };
    POINT cur; int i;
    NEED_POLY(); REQUIRE(U(PolyDraw));
    FX(f, 48, 40);
    sel_pen(f.dc, &s, COL_RED, 1);
    CHECK(U(PolyDraw)(f.dc, pt, ty, 3));
    for (i = 5; i < 25; i++) EXPECT_EQ(fx_px(&f, i, 5), PX_RED);
    for (i = 5; i < 20; i++) EXPECT_EQ(fx_px(&f, 25, i), PX_RED);
    CHECK(U(GetCurrentPositionEx)(f.dc, &cur)); EXPECT_PT(cur, 25, 20);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(poly_polydraw_closefigure_returns_to_start) {
    gdi_sel_t s = {0}; static const POINT pt[3] = { {10,10}, {30,10}, {30,30} };
    static const BYTE ty[3] = { PT_MOVETO, PT_LINETO, PT_LINETO | PT_CLOSEFIGURE };
    POINT cur;
    NEED_POLY(); REQUIRE(U(PolyDraw));
    FX(f, 48, 48);
    sel_pen(f.dc, &s, COL_RED, 1);
    CHECK(U(PolyDraw)(f.dc, pt, ty, 3));
    EXPECT_EQ(fx_px(&f, 20, 20), PX_RED);                   /* the closing segment back to (10,10) */
    CHECK(U(GetCurrentPositionEx)(f.dc, &cur));
    t_note("PolyDraw with CLOSEFIGURE: current position (%ld,%ld)", cur.x, cur.y);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(poly_polydraw_invalid_type) {
    static const POINT pt[2] = { {5,5}, {25,5} };
    static const BYTE bad1[2] = { PT_MOVETO, 0x55 }, bad2[2] = { PT_LINETO, PT_LINETO }, bad3[2] = { 0, 0 };
    NEED_POLY(); REQUIRE(U(PolyDraw));
    FX(f, 48, 40);
    SET_ERR(0); t_note("PolyDraw bad type 0x55: %d err %lu", (int)U(PolyDraw)(f.dc, pt, bad1, 2), GetLastError());
    SET_ERR(0); t_note("PolyDraw without MOVETO: %d err %lu", (int)U(PolyDraw)(f.dc, pt, bad2, 2), GetLastError());
    SET_ERR(0); t_note("PolyDraw zero types: %d err %lu", (int)U(PolyDraw)(f.dc, pt, bad3, 2), GetLastError());
    fx_close(&f);
}

XT(poly_polypolygon_disjoint_squares) {
    gdi_sel_t s = {0}; static const POINT pt[8] = { {5,5},{15,5},{15,15},{5,15}, {30,20},{44,20},{44,34},{30,34} };
    static const INT cnt[2] = { 4, 4 };
    NEED_POLY(); REQUIRE(U(PolyPolygon));
    FX(f, 64, 48);
    sel_pen(f.dc, &s, COL_BLUE, 1); sel_brush(f.dc, &s, COL_BLUE);
    CHECK(U(PolyPolygon)(f.dc, pt, cnt, 2));
    EXPECT_EQ(fx_px(&f, 10, 10), PX_BLUE);
    EXPECT_EQ(fx_px(&f, 37, 27), PX_BLUE);
    EXPECT_EQ(fx_px(&f, 22, 12), PX_WHITE);                 /* between the two polygons */
    EXPECT_EQ(fx_px(&f, 20, 30), PX_WHITE);
    CHECK(fx_count(&f, PX_BLUE) > 100 + 190);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(poly_polypolygon_fill_modes) {
    static const POINT pt[8] = { {10,10},{90,10},{90,90},{10,90}, {30,30},{70,30},{70,70},{30,70} };
    static const INT cnt[2] = { 4, 4 };
    int mode;
    NEED_POLY(); REQUIRE(U(PolyPolygon)); REQUIRE(U(SetPolyFillMode));
    for (mode = ALTERNATE; mode <= WINDING; mode++) {
        gdi_sel_t s = {0};
        FX(f, 100, 100);
        U(SetPolyFillMode)(f.dc, mode);
        sel_pen(f.dc, &s, COL_BLUE, 1); sel_brush(f.dc, &s, COL_BLUE);
        CHECK(U(PolyPolygon)(f.dc, pt, cnt, 2));
        EXPECT_EQ(fx_px(&f, 20, 50), PX_BLUE);
        EXPECT_EQ(fx_px(&f, 50, 50), mode == WINDING ? PX_BLUE : PX_WHITE);   /* same orientation: winding 2 inside */
        sel_done(f.dc, &s);
        fx_close(&f);
    }
}

XT(poly_polygon_fill_mode_opposite_orientation) {
    static const POINT pt[8] = { {10,10},{90,10},{90,90},{10,90}, {30,30},{30,70},{70,70},{70,30} };
    static const INT cnt[2] = { 4, 4 };
    int mode;
    NEED_POLY(); REQUIRE(U(PolyPolygon)); REQUIRE(U(SetPolyFillMode));
    for (mode = ALTERNATE; mode <= WINDING; mode++) {
        gdi_sel_t s = {0};
        FX(f, 100, 100);
        U(SetPolyFillMode)(f.dc, mode);
        sel_pen(f.dc, &s, COL_BLUE, 1); sel_brush(f.dc, &s, COL_BLUE);
        CHECK(U(PolyPolygon)(f.dc, pt, cnt, 2));
        EXPECT_EQ(fx_px(&f, 20, 50), PX_BLUE);
        EXPECT_EQ(fx_px(&f, 50, 50), PX_WHITE);             /* opposite windings cancel in both modes */
        sel_done(f.dc, &s);
        fx_close(&f);
    }
}

XT(poly_polygon_self_intersecting_star) {
    static const POINT star[5] = { {50,5}, {80,95}, {5,38}, {95,38}, {20,95} };
    int mode;
    NEED_POLY(); REQUIRE(U(Polygon)); REQUIRE(U(SetPolyFillMode));
    for (mode = ALTERNATE; mode <= WINDING; mode++) {
        gdi_sel_t s = {0};
        FX(f, 100, 100);
        U(SetPolyFillMode)(f.dc, mode);
        sel_pen(f.dc, &s, COL_BLUE, 1); sel_brush(f.dc, &s, COL_BLUE);
        CHECK(U(Polygon)(f.dc, star, 5));
        EXPECT_EQ(fx_px(&f, 50, 20), PX_BLUE);              /* a tip */
        EXPECT_EQ(fx_px(&f, 50, 55), mode == WINDING ? PX_BLUE : PX_WHITE);   /* the pentagon in the middle */
        sel_done(f.dc, &s);
        fx_close(&f);
    }
}

XT(poly_polypolyline_two_runs) {
    gdi_sel_t s = {0}; static const POINT pt[5] = { {5,5},{25,5}, {5,10},{25,10},{25,20} };
    static const DWORD cnt[2] = { 2, 3 };
    int i;
    NEED_POLY(); REQUIRE(U(PolyPolyline));
    FX(f, 48, 40);
    sel_pen(f.dc, &s, COL_RED, 1);
    CHECK(U(PolyPolyline)(f.dc, pt, cnt, 2));
    for (i = 5; i < 25; i++) { EXPECT_EQ(fx_px(&f, i, 5), PX_RED); EXPECT_EQ(fx_px(&f, i, 10), PX_RED); }
    for (i = 10; i < 20; i++) EXPECT_EQ(fx_px(&f, 25, i), PX_RED);
    EXPECT_INT(fx_count(&f, PX_RED), 20 + 20 + 10);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(poly_polypolyline_zero_runs) {
    static const POINT pt[2] = { {5,5},{25,5} };
    static const DWORD cnt[1] = { 2 };
    NEED_POLY(); REQUIRE(U(PolyPolyline));
    FX(f, 48, 40);
    SET_ERR(0); t_note("PolyPolyline with 0 runs: %d err %lu", (int)U(PolyPolyline)(f.dc, pt, cnt, 0), GetLastError());
    fx_close(&f);
}

XT(poly_fillmode_set_get) {
    NEED_POLY(); REQUIRE(U(SetPolyFillMode)); REQUIRE(U(GetPolyFillMode));
    FX(f, 16, 16);
    EXPECT_EQ(U(GetPolyFillMode)(f.dc), ALTERNATE);
    EXPECT_EQ(U(SetPolyFillMode)(f.dc, WINDING), ALTERNATE);
    EXPECT_EQ(U(GetPolyFillMode)(f.dc), WINDING);
    { long v; if (xd_attr_i32(f.dc, 0xe4, &v)) EXPECT_INT(v, WINDING); }   /* gdi32full!GetPolyFillMode reads DC_ATTR+0xe4 */
    EXPECT_EQ(U(SetPolyFillMode)(f.dc, WINDING), WINDING);
    EXPECT_EQ(U(SetPolyFillMode)(f.dc, ALTERNATE), WINDING);
    SET_ERR(0);
    { static const int bad[] = { 0, 3, -1, 100 }; size_t i;
      for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
          int r = U(SetPolyFillMode)(f.dc, bad[i]);
          t_note("SetPolyFillMode(%d) = %d err %lu", bad[i], r, GetLastError());
          EXPECT_EQ(U(GetPolyFillMode)(f.dc), bad[i]);      /* Windows 10 stores the mode without validating it */
          U(SetPolyFillMode)(f.dc, ALTERNATE);
      } }
    CHECK(U(GetPolyFillMode)(NULL) == 0);
    fx_close(&f);
}

XT(poly_fillmode_restored_by_restoredc) {
    int sv;
    NEED_POLY(); REQUIRE(U(SetPolyFillMode)); REQUIRE(U(GetPolyFillMode)); REQUIRE(U(SaveDC)); REQUIRE(U(RestoreDC));
    FX(f, 16, 16);
    sv = U(SaveDC)(f.dc);
    U(SetPolyFillMode)(f.dc, WINDING);
    CHECK(U(RestoreDC)(f.dc, sv));
    EXPECT_EQ(U(GetPolyFillMode)(f.dc), ALTERNATE);
    fx_close(&f);
}

/* ---- NtGdiPolyPolyDraw direct ------------------------------------------------------------------------------- */
XT(direct_polypolydraw_polygon_matches) {
    gdi_sel_t s1 = {0}, s2 = {0}; fx_t a, b; static const POINT pt[4] = { {10,10}, {50,12}, {40,40}, {8,30} };
    static const ULONG cnt[1] = { 4 };
    NEED_POLY(); REQUIRE(U(Polygon)); REQUIRE_NT(NtGdiPolyPolyDraw);
    if (!fx_open(&a, 64, 64) || !fx_open(&b, 64, 64)) { t_skip("fixtures"); return; }
    sel_pen(a.dc, &s1, COL_RED, 1); sel_brush(a.dc, &s1, COL_BLUE);
    sel_pen(b.dc, &s2, COL_RED, 1); sel_brush(b.dc, &s2, COL_BLUE);
    CHECK(U(Polygon)(a.dc, pt, 4));
    U(GdiFlush)();
    CHECK(UN(NtGdiPolyPolyDraw)(b.dc, pt, cnt, 1, 1));
    EXPECT_INT(fx_diff(&a, &b), 0);
    CHECK(fx_count(&a, PX_BLUE) > 300);
    sel_done(a.dc, &s1); sel_done(b.dc, &s2);
    fx_close(&a); fx_close(&b);
}

XT(direct_polypolydraw_polyline_matches) {
    gdi_sel_t s1 = {0}, s2 = {0}; fx_t a, b; static const POINT pt[5] = { {5,5},{25,5}, {5,10},{25,10},{25,20} };
    static const DWORD dcnt[2] = { 2, 3 };
    static const ULONG ucnt[2] = { 2, 3 };
    NEED_POLY(); REQUIRE(U(PolyPolyline)); REQUIRE_NT(NtGdiPolyPolyDraw);
    if (!fx_open(&a, 48, 40) || !fx_open(&b, 48, 40)) { t_skip("fixtures"); return; }
    sel_pen(a.dc, &s1, COL_RED, 1); sel_pen(b.dc, &s2, COL_RED, 1);
    CHECK(U(PolyPolyline)(a.dc, pt, dcnt, 2));
    U(GdiFlush)();
    CHECK(UN(NtGdiPolyPolyDraw)(b.dc, pt, ucnt, 2, 2));
    EXPECT_INT(fx_diff(&a, &b), 0);
    EXPECT_INT(fx_count(&b, PX_RED), 50);
    sel_done(a.dc, &s1); sel_done(b.dc, &s2);
    fx_close(&a); fx_close(&b);
}

XT(direct_polypolydraw_invalid) {
    static const POINT pt[3] = { {5,5},{25,5},{25,20} };
    static const ULONG cnt[1] = { 3 };
    NEED_POLY(); REQUIRE_NT(NtGdiPolyPolyDraw);
    CHECK(!UN(NtGdiPolyPolyDraw)(NULL, pt, cnt, 1, 1));
    CHECK(!UN(NtGdiPolyPolyDraw)((HDC)(ULONG_PTR)0x01010fff, pt, cnt, 1, 2));
    CHECK(!UN(NtGdiPolyPolyDraw)((HDC)U(GetStockObject)(BLACK_PEN), pt, cnt, 1, 1));
}

/* ---- SetROP2 ------------------------------------------------------------------------------------------------- */
XT(rop2_default_and_set_get) {
    int r;
    NEED_POLY(); REQUIRE(U(SetROP2)); REQUIRE(U(GetROP2));
    FX(f, 16, 16);
    EXPECT_EQ(U(GetROP2)(f.dc), R2_COPYPEN);
    for (r = R2_BLACK; r <= R2_LAST; r++) {
        int prev = U(GetROP2)(f.dc);
        unsigned long raw = 0;
        EXPECT_EQ(U(SetROP2)(f.dc, r), prev);
        EXPECT_EQ(U(GetROP2)(f.dc), r);
        { void *a = xd_dcattr(f.dc);   /* gdi32full!GetROP2 reads the byte at DC_ATTR+0xd4 */
          unsigned char b = 0;
          if (a && k_try_read_u8((char *)a + 0xd4, &b)) { raw = b; EXPECT_EQ(raw, r); } }
    }
    fx_close(&f);
}

XT(rop2_invalid_values_rejected) {
    static const int bad[] = { 0, 17, 18, -1, 0x100 };
    size_t i;
    NEED_POLY(); REQUIRE(U(SetROP2)); REQUIRE(U(GetROP2));
    FX(f, 16, 16);
    U(SetROP2)(f.dc, R2_XORPEN);
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        int rr;
        SET_ERR(0);
        rr = U(SetROP2)(f.dc, bad[i]);
        t_note("SetROP2(%d) = %d err %lu", bad[i], rr, GetLastError());
        /* Windows 10 does not validate the value: its low byte is stored and read back. */
        EXPECT_EQ(U(GetROP2)(f.dc), bad[i] & 0xff);
        U(SetROP2)(f.dc, R2_XORPEN);
    }
    CHECK(U(SetROP2)(NULL, R2_COPYPEN) == 0);
    fx_close(&f);
}

XT(rop2_restored_by_restoredc) {
    int sv;
    NEED_POLY(); REQUIRE(U(SetROP2)); REQUIRE(U(GetROP2)); REQUIRE(U(SaveDC)); REQUIRE(U(RestoreDC));
    FX(f, 16, 16);
    sv = U(SaveDC)(f.dc);
    U(SetROP2)(f.dc, R2_XORPEN);
    CHECK(U(RestoreDC)(f.dc, sv));
    EXPECT_EQ(U(GetROP2)(f.dc), R2_COPYPEN);
    fx_close(&f);
}

static uint32_t rop2_apply(int rop, uint32_t p, uint32_t d) {
    switch (rop) {
    case R2_BLACK:       return 0;
    case R2_NOTMERGEPEN: return ~(d | p);
    case R2_MASKNOTPEN:  return d & ~p;
    case R2_NOTCOPYPEN:  return ~p;
    case R2_MASKPENNOT:  return p & ~d;
    case R2_NOT:         return ~d;
    case R2_XORPEN:      return p ^ d;
    case R2_NOTMASKPEN:  return ~(p & d);
    case R2_MASKPEN:     return p & d;
    case R2_NOTXORPEN:   return ~(p ^ d);
    case R2_NOP:         return d;
    case R2_MERGENOTPEN: return d | ~p;
    case R2_COPYPEN:     return p;
    case R2_MERGEPENNOT: return p | ~d;
    case R2_MERGEPEN:    return p | d;
    default:             return 0xffffff;   /* R2_WHITE */
    }
}

XT(rop2_all_modes_on_lines) {
    static const COLORREF pens[] = { RGB(0xAA, 0x55, 0xF0), RGB(0x0F, 0xF0, 0x33) };
    static const uint32_t dsts[] = { 0x336699, 0xC0FFEE, 0x000000, 0xFFFFFF };
    int rop; size_t pi, di;
    NEED_POLY(); REQUIRE(U(SetROP2));
    FX(f, 32, 8);
    for (pi = 0; pi < sizeof pens / sizeof pens[0]; pi++) for (di = 0; di < sizeof dsts / sizeof dsts[0]; di++) {
        for (rop = R2_BLACK; rop <= R2_LAST; rop++) {
            gdi_sel_t s = {0}; uint32_t want, got;
            fx_clear(&f, dsts[di]);
            sel_pen(f.dc, &s, pens[pi], 1);
            U(SetROP2)(f.dc, rop);
            U(MoveToEx)(f.dc, 4, 3, NULL); U(LineTo)(f.dc, 24, 3);
            want = rop2_apply(rop, col2px(pens[pi]), dsts[di]) & 0xffffff;
            got = fx_px(&f, 12, 3);
            CHECKF(got == want, "rop2 %d pen %06x dst %06x: got %06x want %06x", rop, col2px(pens[pi]), dsts[di], got, want);
            CHECKF(fx_px(&f, 12, 2) == dsts[di], "rop2 %d neighbour row untouched", rop);
            CHECKF(fx_px(&f, 24, 3) == dsts[di], "rop2 %d line end excluded", rop);
            U(SetROP2)(f.dc, R2_COPYPEN);
            sel_done(f.dc, &s);
        }
    }
    fx_close(&f);
}

XT(rop2_applies_to_brush_fill) {
    static const int rops[] = { R2_COPYPEN, R2_XORPEN, R2_MASKPEN, R2_MERGEPEN, R2_NOTCOPYPEN, R2_NOP, R2_BLACK, R2_WHITE };
    size_t i;
    NEED_POLY(); REQUIRE(U(SetROP2)); REQUIRE(U(Rectangle));
    FX(f, 32, 32);
    for (i = 0; i < sizeof rops / sizeof rops[0]; i++) {
        gdi_sel_t s = {0}; uint32_t want;
        fx_clear(&f, 0x336699);
        sel_nullpen(f.dc, &s); sel_brush(f.dc, &s, RGB(0xAA, 0x55, 0xF0));
        U(SetROP2)(f.dc, rops[i]);
        U(Rectangle)(f.dc, 4, 4, 20, 20);
        want = rop2_apply(rops[i], 0xAA55F0, 0x336699) & 0xffffff;
        CHECKF(fx_px(&f, 10, 10) == want, "rop2 %d brush fill got %06x want %06x", rops[i], fx_px(&f, 10, 10), want);
        EXPECT_EQ(fx_px(&f, 25, 25), 0x336699);
        U(SetROP2)(f.dc, R2_COPYPEN);
        sel_done(f.dc, &s);
    }
    fx_close(&f);
}

/* ---- PolyPatBlt --------------------------------------------------------------------------------------------- */
XT(polypatblt_two_rects) {
    POLYPATBLT_K pp[2]; HBRUSH b1, b2;
    NEED_POLY(); REQUIRE(UN(PolyPatBlt));
    FX(f, 64, 64);
    b1 = U(CreateSolidBrush)(COL_RED); b2 = U(CreateSolidBrush)(COL_BLUE);
    pp[0].x = 5; pp[0].y = 6; pp[0].cx = 20; pp[0].cy = 10; pp[0].hBrush = b1;
    pp[1].x = 30; pp[1].y = 20; pp[1].cx = 15; pp[1].cy = 12; pp[1].hBrush = b2;
    CHECK(UN(PolyPatBlt)(f.dc, PATCOPY, pp, 2, 0));
    EXPECT_INT(fx_count(&f, PX_RED), 20 * 10);
    EXPECT_INT(fx_count(&f, PX_BLUE), 15 * 12);
    EXPECT_INT(fx_count_box(&f, 5, 6, 24, 15, PX_RED), 200);
    EXPECT_INT(fx_count_box(&f, 30, 20, 44, 31, PX_BLUE), 180);
    U(DeleteObject)(b1); U(DeleteObject)(b2);
    fx_close(&f);
}

XT(polypatblt_matches_patblt) {
    POLYPATBLT_K pp; HBRUSH br; HGDIOBJ old; fx_t a;
    NEED_POLY(); REQUIRE(UN(PolyPatBlt)); REQUIRE(U(PatBlt));
    FX(f, 64, 64);
    if (!fx_open(&a, 64, 64)) { t_skip("fixture"); fx_close(&f); return; }
    br = U(CreateSolidBrush)(RGB(10, 200, 77));
    pp.x = 7; pp.y = 9; pp.cx = 33; pp.cy = 21; pp.hBrush = br;
    CHECK(UN(PolyPatBlt)(f.dc, PATCOPY, &pp, 1, 0));
    old = U(SelectObject)(a.dc, br);
    CHECK(U(PatBlt)(a.dc, 7, 9, 33, 21, PATCOPY));
    U(SelectObject)(a.dc, old);
    EXPECT_INT(fx_diff(&f, &a), 0);
    U(DeleteObject)(br);
    fx_close(&f); fx_close(&a);
}

typedef struct { DWORD rop; const char *name; } ppb_rop_t;

XT(polypatblt_rops) {
    static const ppb_rop_t t[] = { { PATCOPY, "PATCOPY" }, { PATINVERT, "PATINVERT" }, { BLACKNESS, "BLACKNESS" },
                                    { WHITENESS, "WHITENESS" }, { DSTINVERT, "DSTINVERT" }, { 0x00A000C9, "DPa" } };
    size_t i;
    NEED_POLY(); REQUIRE(UN(PolyPatBlt));
    for (i = 0; i < sizeof t / sizeof t[0]; i++) {
        POLYPATBLT_K pp; HBRUSH br; uint32_t d = 0x336699, p = 0xAA55F0, want;
        FX(f, 32, 32);
        br = U(CreateSolidBrush)(RGB(0xAA, 0x55, 0xF0));
        fx_clear(&f, d);
        pp.x = 4; pp.y = 4; pp.cx = 10; pp.cy = 10; pp.hBrush = br;
        CHECKF(UN(PolyPatBlt)(f.dc, t[i].rop, &pp, 1, 0), "%s", t[i].name);
        switch (t[i].rop) {
        case PATCOPY:   want = p; break;
        case PATINVERT: want = p ^ d; break;
        case BLACKNESS: want = 0; break;
        case WHITENESS: want = 0xffffff; break;
        case DSTINVERT: want = ~d & 0xffffff; break;
        default:        want = d & p; break;                                           /* 0xA000C9 = DPa */
        }
        CHECKF(fx_px(&f, 8, 8) == want, "%s: got %06x want %06x", t[i].name, fx_px(&f, 8, 8), want);
        EXPECT_EQ(fx_px(&f, 20, 20), d);
        U(DeleteObject)(br);
        fx_close(&f);
    }
}

XT(polypatblt_many_rects_beyond_batch_limit) {
    POLYPATBLT_K *pp; HBRUSH br; int i, n = 3000;
    NEED_POLY(); REQUIRE(UN(PolyPatBlt));
    FX(f, 64, 64);
    pp = (POLYPATBLT_K *)calloc(n, sizeof *pp);
    if (!pp) { t_skip("alloc"); fx_close(&f); return; }
    br = U(CreateSolidBrush)(COL_BLUE);
    for (i = 0; i < n; i++) { pp[i].x = i % 64; pp[i].y = i / 64; pp[i].cx = 1; pp[i].cy = 1; pp[i].hBrush = br; }
    CHECK(UN(PolyPatBlt)(f.dc, PATCOPY, pp, n, 0));        /* > 0xaa9 entries: gdi32full falls back to the direct syscall */
    EXPECT_INT(fx_count(&f, PX_BLUE), n);
    EXPECT_EQ(fx_px(&f, 0, 0), PX_BLUE);
    EXPECT_EQ(fx_px(&f, (n - 1) % 64, (n - 1) / 64), PX_BLUE);
    EXPECT_EQ(fx_px(&f, n % 64, n / 64), PX_WHITE);
    U(DeleteObject)(br);
    free(pp);
    fx_close(&f);
}

XT(polypatblt_degenerate_arguments) {
    POLYPATBLT_K pp; HBRUSH br;
    NEED_POLY(); REQUIRE(UN(PolyPatBlt));
    FX(f, 32, 32);
    br = U(CreateSolidBrush)(COL_RED);
    pp.x = 4; pp.y = 4; pp.cx = 10; pp.cy = 10; pp.hBrush = br;
    SET_ERR(0); t_note("PolyPatBlt count 0: %d err %lu", (int)UN(PolyPatBlt)(f.dc, PATCOPY, &pp, 0, 0), GetLastError());
    pp.cx = -5;
    SET_ERR(0); t_note("PolyPatBlt negative width: %d err %lu", (int)UN(PolyPatBlt)(f.dc, PATCOPY, &pp, 1, 0), GetLastError());
    t_note("negative-width rect painted %d px", fx_count(&f, PX_RED));
    pp.cx = 0;
    fx_clear(&f, 0x00FFFFFF);
    UN(PolyPatBlt)(f.dc, PATCOPY, &pp, 1, 0);
    EXPECT_INT(fx_count(&f, PX_RED), 0);                    /* zero width paints nothing */
    CHECK(!UN(PolyPatBlt)(NULL, PATCOPY, &pp, 1, 0));
    U(DeleteObject)(br);
    fx_close(&f);
}

XT(polypatblt_clipped_to_surface) {
    POLYPATBLT_K pp; HBRUSH br;
    NEED_POLY(); REQUIRE(UN(PolyPatBlt));
    FX(f, 32, 32);
    br = U(CreateSolidBrush)(COL_RED);
    pp.x = -10; pp.y = -10; pp.cx = 20; pp.cy = 20; pp.hBrush = br;
    CHECK(UN(PolyPatBlt)(f.dc, PATCOPY, &pp, 1, 0));
    EXPECT_INT(fx_count(&f, PX_RED), 10 * 10);
    pp.x = 25; pp.y = 25; pp.cx = 100; pp.cy = 100;
    CHECK(UN(PolyPatBlt)(f.dc, PATCOPY, &pp, 1, 0));
    EXPECT_INT(fx_count(&f, PX_RED), 100 + 49);
    U(DeleteObject)(br);
    fx_close(&f);
}

XT(polypatblt_follows_mapping) {
    POLYPATBLT_K pp; HBRUSH br;
    NEED_POLY(); REQUIRE(UN(PolyPatBlt)); REQUIRE(U(SetViewportOrgEx));
    FX(f, 64, 64);
    br = U(CreateSolidBrush)(COL_RED);
    U(SetViewportOrgEx)(f.dc, 20, 30, NULL);
    pp.x = 0; pp.y = 0; pp.cx = 5; pp.cy = 4; pp.hBrush = br;
    CHECK(UN(PolyPatBlt)(f.dc, PATCOPY, &pp, 1, 0));
    EXPECT_INT(fx_count_box(&f, 20, 30, 24, 33, PX_RED), 20);
    EXPECT_INT(fx_count(&f, PX_RED), 20);
    U(DeleteObject)(br);
    fx_close(&f);
}

XT(direct_polypatblt_matches_userland) {
    POLYPATBLT_K pp[2]; HBRUSH b1, b2; fx_t a;
    NEED_POLY(); REQUIRE(UN(PolyPatBlt)); REQUIRE_NT(NtGdiPolyPatBlt);
    FX(f, 64, 64);
    if (!fx_open(&a, 64, 64)) { t_skip("fixture"); fx_close(&f); return; }
    b1 = U(CreateSolidBrush)(COL_RED); b2 = U(CreateSolidBrush)(COL_BLUE);
    pp[0].x = 5; pp[0].y = 6; pp[0].cx = 20; pp[0].cy = 10; pp[0].hBrush = b1;
    pp[1].x = 15; pp[1].y = 12; pp[1].cx = 25; pp[1].cy = 12; pp[1].hBrush = b2;
    CHECK(UN(PolyPatBlt)(f.dc, PATCOPY, pp, 2, 0));
    U(GdiFlush)();
    CHECK(UN(NtGdiPolyPatBlt)(a.dc, PATCOPY, pp, 2, 0));    /* later entries overwrite earlier ones */
    EXPECT_INT(fx_diff(&f, &a), 0);
    EXPECT_EQ(fx_px(&a, 20, 14), PX_BLUE);                  /* overlap belongs to the second rect */
    CHECK(!UN(NtGdiPolyPatBlt)(NULL, PATCOPY, pp, 2, 0));
    CHECK(!UN(NtGdiPolyPatBlt)((HDC)U(GetStockObject)(BLACK_PEN), PATCOPY, pp, 2, 0));
    U(DeleteObject)(b1); U(DeleteObject)(b2);
    fx_close(&f); fx_close(&a);
}

XT(direct_polypatblt_colorref_mode) {
    POLYPATBLT_K pp;
    NEED_POLY(); REQUIRE_NT(NtGdiPolyPatBlt);
    FX(f, 32, 32);
    pp.x = 4; pp.y = 4; pp.cx = 10; pp.cy = 10; pp.hBrush = (HBRUSH)(ULONG_PTR)RGB(0x12, 0x34, 0x56);
    SET_ERR(0);
    t_note("NtGdiPolyPatBlt Mode=1 (hBrush as COLORREF): %d err %lu, pixel %06x", (int)UN(NtGdiPolyPatBlt)(f.dc, PATCOPY, &pp, 1, 1),
           GetLastError(), fx_px(&f, 8, 8));
    fx_close(&f);
}

/* ---- bounds rectangle ------------------------------------------------------------------------------------- */
#define NEED_BOUNDS() do { NEED_POLY(); REQUIRE(U(SetBoundsRect)); REQUIRE(U(GetBoundsRect)); REQUIRE(U(Rectangle)); } while (0)

XT(bounds_initial_state_disabled_and_empty) {
    RECT r = { 7, 7, 7, 7 }; UINT st;
    NEED_BOUNDS();
    FX(f, 64, 64);
    st = U(GetBoundsRect)(f.dc, &r, 0);
    t_note("initial GetBoundsRect state 0x%x rect (%ld,%ld,%ld,%ld)", st, r.left, r.top, r.right, r.bottom);
    CHECK(st != 0);
    CHECK(st & DCB_RESET);                                  /* the rectangle is empty; GetBoundsRect never reports ENABLE/DISABLE */
    CHECK(!(st & (DCB_ENABLE | DCB_DISABLE)));
    CHECK(!(st & DCB_ACCUMULATE));                          /* nothing accumulated yet */
    fx_close(&f);
}

XT(bounds_disabled_does_not_accumulate) {
    gdi_sel_t s = {0}; RECT r; UINT st;
    NEED_BOUNDS();
    FX(f, 64, 64);
    sel_pen(f.dc, &s, COL_RED, 1);
    U(Rectangle)(f.dc, 10, 10, 30, 25);
    st = U(GetBoundsRect)(f.dc, &r, 0);
    CHECK(!(st & DCB_ACCUMULATE));
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(bounds_enable_accumulates_drawing) {
    gdi_sel_t s = {0}; RECT r; UINT st;
    NEED_BOUNDS();
    FX(f, 64, 64);
    sel_pen(f.dc, &s, COL_RED, 1); sel_nullbrush(f.dc, &s);
    U(SetBoundsRect)(f.dc, NULL, DCB_ENABLE | DCB_RESET);
    U(Rectangle)(f.dc, 10, 12, 30, 25);
    st = U(GetBoundsRect)(f.dc, &r, 0);
    t_note("bounds after Rectangle(10,12,30,25): state 0x%x (%ld,%ld,%ld,%ld)", st, r.left, r.top, r.right, r.bottom);
    CHECK(!(st & (DCB_ENABLE | DCB_DISABLE)));              /* the enable state is not reported back by GetBoundsRect */
    CHECK(st & DCB_ACCUMULATE);
    CHECK(r.left <= 10 && r.left >= 8 && r.top <= 12 && r.top >= 10);
    CHECK(r.right >= 29 && r.right <= 32 && r.bottom >= 24 && r.bottom <= 27);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(bounds_union_of_two_draws) {
    gdi_sel_t s = {0}; RECT r;
    NEED_BOUNDS();
    FX(f, 100, 100);
    sel_pen(f.dc, &s, COL_RED, 1); sel_nullbrush(f.dc, &s);
    U(SetBoundsRect)(f.dc, NULL, DCB_ENABLE | DCB_RESET);
    U(Rectangle)(f.dc, 10, 10, 20, 20);
    U(Rectangle)(f.dc, 60, 50, 80, 70);
    U(GetBoundsRect)(f.dc, &r, 0);
    t_note("union bounds (%ld,%ld,%ld,%ld)", r.left, r.top, r.right, r.bottom);
    CHECK(r.left <= 10 && r.left >= 8 && r.top <= 10 && r.top >= 8);
    CHECK(r.right >= 79 && r.right <= 82 && r.bottom >= 69 && r.bottom <= 72);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(bounds_reset_flag_clears) {
    gdi_sel_t s = {0}; RECT r, r2; UINT st;
    NEED_BOUNDS();
    FX(f, 64, 64);
    sel_pen(f.dc, &s, COL_RED, 1);
    U(SetBoundsRect)(f.dc, NULL, DCB_ENABLE | DCB_RESET);
    U(Rectangle)(f.dc, 10, 10, 30, 25);
    st = U(GetBoundsRect)(f.dc, &r, DCB_RESET);              /* reads, then resets */
    CHECK(st & DCB_ACCUMULATE);
    CHECK(r.right > r.left);
    memset(&r2, 0x55, sizeof r2);
    st = U(GetBoundsRect)(f.dc, &r2, 0);
    t_note("after reset: state 0x%x rect (%ld,%ld,%ld,%ld)", st, r2.left, r2.top, r2.right, r2.bottom);
    CHECK(!(st & DCB_ACCUMULATE));
    CHECK(st & DCB_RESET);                                   /* the rectangle is empty again */
    U(Rectangle)(f.dc, 10, 10, 30, 25);
    st = U(GetBoundsRect)(f.dc, &r2, 0);
    CHECK(st & DCB_ACCUMULATE);                              /* RESET only clears the rectangle, not the enable state: drawing accumulates again */
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(bounds_set_returns_previous_state) {
    UINT p1, p2, p3;
    NEED_BOUNDS();
    FX(f, 64, 64);
    p1 = U(SetBoundsRect)(f.dc, NULL, DCB_ENABLE);
    t_note("SetBoundsRect #1 returned 0x%x", p1);
    CHECK(p1 & DCB_DISABLE); CHECK(!(p1 & DCB_ENABLE));      /* previous: disabled (the default) */
    p2 = U(SetBoundsRect)(f.dc, NULL, DCB_DISABLE);
    t_note("SetBoundsRect #2 returned 0x%x", p2);
    CHECK(p2 & DCB_ENABLE); CHECK(!(p2 & DCB_DISABLE));
    p3 = U(SetBoundsRect)(f.dc, NULL, 0);
    CHECK(p3 & DCB_DISABLE);
    fx_close(&f);
}

XT(bounds_set_with_rect_accumulates) {
    RECT in = { 20, 30, 40, 60 }, in2 = { 50, 10, 70, 20 }, r; UINT st;
    NEED_BOUNDS();
    FX(f, 100, 100);
    U(SetBoundsRect)(f.dc, NULL, DCB_ENABLE | DCB_RESET);
    U(SetBoundsRect)(f.dc, &in, DCB_ACCUMULATE);
    st = U(GetBoundsRect)(f.dc, &r, 0);
    CHECK(st & DCB_ACCUMULATE);
    EXPECT_INT(r.left, 20); EXPECT_INT(r.top, 30); EXPECT_INT(r.right, 40); EXPECT_INT(r.bottom, 60);
    U(SetBoundsRect)(f.dc, &in2, DCB_ACCUMULATE);
    U(GetBoundsRect)(f.dc, &r, 0);
    EXPECT_INT(r.left, 20); EXPECT_INT(r.top, 10); EXPECT_INT(r.right, 70); EXPECT_INT(r.bottom, 60);   /* union of both */
    fx_close(&f);
}

XT(bounds_set_rect_resets_when_reset_flag) {
    RECT in = { 20, 30, 40, 60 }, r;
    NEED_BOUNDS();
    FX(f, 100, 100);
    U(SetBoundsRect)(f.dc, NULL, DCB_ENABLE | DCB_RESET);
    U(SetBoundsRect)(f.dc, &in, DCB_ACCUMULATE);
    U(SetBoundsRect)(f.dc, NULL, DCB_RESET);
    memset(&r, 0x55, sizeof r);
    { UINT st = U(GetBoundsRect)(f.dc, &r, 0);
      CHECK(!(st & DCB_ACCUMULATE)); t_note("bounds after RESET: (%ld,%ld,%ld,%ld)", r.left, r.top, r.right, r.bottom); }
    fx_close(&f);
}

XT(bounds_logical_coordinates) {
    gdi_sel_t s = {0}; RECT r;
    NEED_BOUNDS(); REQUIRE(U(SetViewportOrgEx));
    FX(f, 128, 64);
    sel_pen(f.dc, &s, COL_RED, 1); sel_nullbrush(f.dc, &s);
    U(SetBoundsRect)(f.dc, NULL, DCB_ENABLE | DCB_RESET);
    U(SetViewportOrgEx)(f.dc, 40, 5, NULL);
    U(Rectangle)(f.dc, 10, 10, 20, 20);
    U(GetBoundsRect)(f.dc, &r, 0);
    t_note("bounds with viewport origin (%ld,%ld,%ld,%ld)", r.left, r.top, r.right, r.bottom);
    CHECK(r.left >= 8 && r.left <= 10 && r.right >= 19 && r.right <= 22);   /* reported in logical, not device, coordinates */
    CHECK(r.top >= 8 && r.top <= 10);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(bounds_clipped_drawing_not_counted) {
    gdi_sel_t s = {0}; RECT r;
    NEED_BOUNDS();
    FX(f, 64, 64);
    sel_pen(f.dc, &s, COL_RED, 1); sel_nullbrush(f.dc, &s);
    U(SetBoundsRect)(f.dc, NULL, DCB_ENABLE | DCB_RESET);
    U(Rectangle)(f.dc, 100, 100, 120, 120);                  /* entirely off the 64x64 surface */
    { UINT st = U(GetBoundsRect)(f.dc, &r, 0); t_note("bounds after off-surface rect: state 0x%x (%ld,%ld,%ld,%ld)", st, r.left, r.top, r.right, r.bottom); }
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(bounds_invalid_hdc) {
    RECT r = { 1, 2, 3, 4 };
    NEED_BOUNDS();
    EXPECT_EQ(U(SetBoundsRect)(NULL, &r, DCB_ENABLE), 0);
    EXPECT_EQ(U(GetBoundsRect)(NULL, &r, 0), 0);
    EXPECT_EQ(U(SetBoundsRect)((HDC)(ULONG_PTR)0x01010fff, &r, DCB_ENABLE), 0);
    EXPECT_EQ(U(GetBoundsRect)((HDC)U(GetStockObject)(BLACK_PEN), &r, 0), 0);
    EXPECT_INT(r.left, 1); EXPECT_INT(r.bottom, 4);
}

XT(bounds_independent_per_dc) {
    UINT st;
    RECT r;
    NEED_BOUNDS();
    gdi_sel_t sa = {0}, sb = {0};
    FX(a, 32, 32);
    FX(b, 32, 32);
    sel_pen(a.dc, &sa, COL_RED, 1); sel_pen(b.dc, &sb, COL_RED, 1);
    U(SetBoundsRect)(a.dc, NULL, DCB_ENABLE | DCB_RESET);   /* only the first DC collects bounds */
    U(Rectangle)(a.dc, 4, 4, 20, 20);
    U(Rectangle)(b.dc, 4, 4, 20, 20);
    st = U(GetBoundsRect)(b.dc, &r, 0);
    CHECK(!(st & DCB_ACCUMULATE));
    st = U(GetBoundsRect)(a.dc, &r, 0);
    CHECK(st & DCB_ACCUMULATE);
    sel_done(a.dc, &sa); sel_done(b.dc, &sb);
    fx_close(&a); fx_close(&b);
}

XT(direct_boundsrect_matches_api) {
    RECT in = { 20, 30, 40, 60 }, r1, r2; UINT s1, s2;
    NEED_BOUNDS(); REQUIRE_NT(NtGdiSetBoundsRect); REQUIRE_NT(NtGdiGetBoundsRect);
    FX(f, 100, 100);
    UN(NtGdiSetBoundsRect)(f.dc, NULL, DCB_ENABLE | DCB_RESET);
    UN(NtGdiSetBoundsRect)(f.dc, &in, DCB_ACCUMULATE);
    s1 = UN(NtGdiGetBoundsRect)(f.dc, &r1, 0);
    s2 = U(GetBoundsRect)(f.dc, &r2, 0);
    EXPECT_EQ(s1, s2);
    CHECK(!memcmp(&r1, &r2, sizeof r1));
    EXPECT_INT(r1.left, 20); EXPECT_INT(r1.bottom, 60);
    EXPECT_EQ(UN(NtGdiSetBoundsRect)(NULL, &in, DCB_ENABLE), 0);
    EXPECT_EQ(UN(NtGdiGetBoundsRect)(NULL, &r1, 0), 0);
    fx_close(&f);
}
