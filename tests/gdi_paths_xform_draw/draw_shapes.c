/* Curved primitives on a DIB: Ellipse, RoundRect, Arc/ArcTo/Chord/Pie/AngleArc and the NtGdiArcInternal syscall.
 *
 * gdi32full!Arc/ArcTo/Chord/Pie are thin callers of NtGdiArcInternal(type, hdc, l, t, r, b, xs, ys, xe, ye) with
 * type 0 = Arc, 1 = ArcTo, 2 = Chord, 3 = Pie (analysis of each caller). Only coarse geometry is asserted (bounding
 * boxes, which quadrants are touched, symmetry notes) because the rasterizer is the kernel's own.
 */
#include "xd.h"

#define NEED_SHAPES() do { REQUIRE(U(MoveToEx)); REQUIRE(U(Ellipse)); REQUIRE(U(RoundRect)); REQUIRE(U(Arc)); REQUIRE(U(Pie)); \
    REQUIRE(U(Chord)); REQUIRE(U(ArcTo)); REQUIRE(U(AngleArc)); REQUIRE(U(CreateCompatibleDC)); REQUIRE(U(CreateDIBSection)); \
    REQUIRE(U(GdiFlush)); REQUIRE(U(GetCurrentPositionEx)); } while (0)

/* ---- Ellipse ------------------------------------------------------------------------------------------------ */
XT(ellipse_outline_bounds) {
    gdi_sel_t s = {0}; int l, t, r, b, n;
    NEED_SHAPES();
    FX(f, 64, 64);
    sel_pen(f.dc, &s, COL_RED, 1); sel_nullbrush(f.dc, &s);
    CHECK(U(Ellipse)(f.dc, 10, 10, 50, 40));
    n = fx_bbox(&f, PX_RED, &l, &t, &r, &b);
    t_note("Ellipse(10,10,50,40): %d px bbox (%d,%d)-(%d,%d)", n, l, t, r, b);
    CHECK(n > 60);
    CHECK(l == 10 && t == 10 && r >= 48 && r <= 49 && b >= 38 && b <= 39);   /* touches the box on all four sides, never beyond */
    EXPECT_EQ(fx_px(&f, 30, 25), PX_WHITE);                                  /* NULL brush */
    EXPECT_EQ(fx_px(&f, 10, 10), PX_WHITE);                                  /* corners of the box are not on the curve */
    EXPECT_EQ(fx_px(&f, 49, 39), PX_WHITE);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(ellipse_axis_points_present) {
    gdi_sel_t s = {0};
    NEED_SHAPES();
    FX(f, 64, 64);
    sel_pen(f.dc, &s, COL_RED, 1); sel_nullbrush(f.dc, &s);
    CHECK(U(Ellipse)(f.dc, 10, 10, 30, 30));
    CHECK(fx_near(&f, 10, 20, 0, PX_RED) || fx_near(&f, 10, 19, 0, PX_RED));
    CHECK(fx_near(&f, 29, 20, 0, PX_RED) || fx_near(&f, 29, 19, 0, PX_RED));
    CHECK(fx_near(&f, 20, 10, 0, PX_RED) || fx_near(&f, 19, 10, 0, PX_RED));
    CHECK(fx_near(&f, 20, 29, 0, PX_RED) || fx_near(&f, 19, 29, 0, PX_RED));
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(ellipse_filled_area) {
    gdi_sel_t s = {0}; int n;
    NEED_SHAPES();
    FX(f, 64, 64);
    sel_pen(f.dc, &s, COL_BLUE, 1); sel_brush(f.dc, &s, COL_BLUE);
    CHECK(U(Ellipse)(f.dc, 10, 10, 50, 50));        /* circle, diameter 40 */
    n = fx_count(&f, PX_BLUE);
    t_note("filled circle d=40: %d px (pi r^2 = 1257)", n);
    CHECK(n > 1150 && n < 1360);
    EXPECT_EQ(fx_px(&f, 30, 30), PX_BLUE);
    EXPECT_EQ(fx_px(&f, 12, 12), PX_WHITE);
    EXPECT_EQ(fx_px(&f, 47, 47), PX_WHITE);
    EXPECT_EQ(fx_px(&f, 30, 11), PX_BLUE);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(ellipse_symmetry_report) {
    gdi_sel_t s = {0}; int x, y, asym_h = 0, asym_v = 0;
    NEED_SHAPES();
    FX(f, 64, 64);
    sel_pen(f.dc, &s, COL_BLUE, 1); sel_brush(f.dc, &s, COL_BLUE);
    CHECK(U(Ellipse)(f.dc, 10, 10, 50, 40));
    for (y = 0; y < 64; y++) for (x = 0; x < 64; x++) {
        if (fx_px(&f, x, y) != fx_px(&f, 59 - x, y)) asym_h++;   /* mirror about the box centre x = 29.5 */
        if (fx_px(&f, x, y) != fx_px(&f, x, 49 - y)) asym_v++;   /* mirror about y = 24.5 */
    }
    t_note("ellipse asymmetry: horizontal %d vertical %d", asym_h, asym_v);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(ellipse_degenerate_boxes) {
    gdi_sel_t s = {0};
    NEED_SHAPES();
    FX(f, 32, 32);
    sel_pen(f.dc, &s, COL_RED, 1); sel_brush(f.dc, &s, COL_BLUE);
    t_note("empty ellipse returned %d", (int)U(Ellipse)(f.dc, 10, 10, 10, 10));
    t_note("empty ellipse painted %d px", 32 * 32 - fx_count(&f, PX_WHITE));
    fx_clear(&f, 0x00FFFFFF);
    t_note("1x1 ellipse returned %d", (int)U(Ellipse)(f.dc, 10, 10, 11, 11));
    t_note("1x1 ellipse painted %d px", 32 * 32 - fx_count(&f, PX_WHITE));
    fx_clear(&f, 0x00FFFFFF);
    t_note("flat ellipse returned %d", (int)U(Ellipse)(f.dc, 10, 10, 40, 11));
    t_note("30x1 ellipse painted %d px", 32 * 32 - fx_count(&f, PX_WHITE));
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(ellipse_clipped_by_surface) {
    gdi_sel_t s = {0}; int l, t, r, b;
    NEED_SHAPES();
    FX(f, 32, 32);
    sel_pen(f.dc, &s, COL_RED, 1); sel_nullbrush(f.dc, &s);
    CHECK(U(Ellipse)(f.dc, 16, 16, 80, 80));
    fx_bbox(&f, PX_RED, &l, &t, &r, &b);
    /* the ellipse is centred at (48,48) with radius 32: only the arc from about (31,21) to (21,31) falls on the 32x32 surface */
    t_note("clipped ellipse bbox (%d,%d)-(%d,%d), %d px", l, t, r, b, fx_count(&f, PX_RED));
    CHECK(l >= 19 && l <= 23 && t >= 19 && t <= 23);
    CHECK(r == 31 && b == 31);
    CHECK(fx_count(&f, PX_RED) >= 10 && fx_count(&f, PX_RED) <= 16);   /* a one pixel arc of about 14 pixels */
    sel_done(f.dc, &s);
    fx_close(&f);
}

/* ---- RoundRect ---------------------------------------------------------------------------------------------- */
XT(roundrect_corners_cut_edges_kept) {
    gdi_sel_t s = {0}; int l, t, r, b;
    NEED_SHAPES();
    FX(f, 64, 64);
    sel_pen(f.dc, &s, COL_RED, 1); sel_nullbrush(f.dc, &s);
    CHECK(U(RoundRect)(f.dc, 10, 10, 50, 40, 16, 16));
    fx_bbox(&f, PX_RED, &l, &t, &r, &b);
    CHECK(l == 10 && t == 10 && r >= 48 && r <= 49 && b >= 38 && b <= 39);
    EXPECT_EQ(fx_px(&f, 10, 10), PX_WHITE);                 /* rounded corners: the box corners are not painted */
    EXPECT_EQ(fx_px(&f, 49, 10), PX_WHITE);
    EXPECT_EQ(fx_px(&f, 10, 39), PX_WHITE);
    EXPECT_EQ(fx_px(&f, 49, 39), PX_WHITE);
    EXPECT_EQ(fx_px(&f, 30, 10), PX_RED);                   /* straight middle portions of each edge */
    EXPECT_EQ(fx_px(&f, 30, 39), PX_RED);
    EXPECT_EQ(fx_px(&f, 10, 25), PX_RED);
    EXPECT_EQ(fx_px(&f, 49, 25), PX_RED);
    EXPECT_EQ(fx_px(&f, 30, 25), PX_WHITE);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(roundrect_zero_radius_like_rectangle) {
    gdi_sel_t s = {0}; int n1, n2;
    NEED_SHAPES();
    FX(f, 64, 64);
    sel_pen(f.dc, &s, COL_RED, 1); sel_nullbrush(f.dc, &s);
    CHECK(U(RoundRect)(f.dc, 10, 10, 50, 40, 0, 0));
    n1 = fx_count(&f, PX_RED);
    fx_clear(&f, 0x00FFFFFF);
    CHECK(U(Rectangle)(f.dc, 10, 10, 50, 40));
    n2 = fx_count(&f, PX_RED);
    t_note("RoundRect(0,0) %d px vs Rectangle %d px", n1, n2);
    EXPECT_INT(n2, 2 * 40 + 2 * 30 - 4);
    CHECK(n1 > 0);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(roundrect_filled_interior) {
    gdi_sel_t s = {0};
    NEED_SHAPES();
    FX(f, 64, 64);
    sel_pen(f.dc, &s, COL_RED, 1); sel_brush(f.dc, &s, COL_BLUE);
    CHECK(U(RoundRect)(f.dc, 10, 10, 50, 40, 12, 12));
    EXPECT_EQ(fx_px(&f, 30, 25), PX_BLUE);
    EXPECT_EQ(fx_px(&f, 14, 14), PX_BLUE);
    EXPECT_EQ(fx_px(&f, 10, 10), PX_WHITE);
    EXPECT_EQ(fx_px(&f, 30, 10), PX_RED);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(roundrect_huge_radius_is_ellipse_like) {
    gdi_sel_t s = {0}; int n;
    NEED_SHAPES();
    FX(f, 64, 64);
    sel_pen(f.dc, &s, COL_RED, 1); sel_nullbrush(f.dc, &s);
    CHECK(U(RoundRect)(f.dc, 10, 10, 50, 40, 1000, 1000));  /* radii are clamped to the box: an ellipse */
    n = fx_count(&f, PX_RED);
    t_note("RoundRect with oversized radii painted %d px", n);
    EXPECT_EQ(fx_px(&f, 10, 10), PX_WHITE);
    EXPECT_EQ(fx_px(&f, 30, 25), PX_WHITE);
    CHECK(n > 60);
    sel_done(f.dc, &s);
    fx_close(&f);
}

/* ---- Arc / ArcTo / Chord / Pie / AngleArc ------------------------------------------------------------------- */
/* Circle box (10,10,50,50): centre (30,30), radius 20.  Radial endpoints: east (50,30) and north (30,10). */
#define ARC_ARGS 10, 10, 50, 50, 50, 30, 30, 10

XT(arc_default_counterclockwise_quadrant) {
    gdi_sel_t s = {0};
    NEED_SHAPES(); REQUIRE(U(GetArcDirection));
    FX(f, 64, 64);
    EXPECT_EQ(U(GetArcDirection)(f.dc), AD_COUNTERCLOCKWISE);
    sel_pen(f.dc, &s, COL_RED, 1); sel_nullbrush(f.dc, &s);
    CHECK(U(Arc)(f.dc, ARC_ARGS));
    CHECK(fx_near(&f, 44, 16, 2, PX_RED));                    /* north-east on the circle */
    CHECK(fx_near(&f, 49, 30, 1, PX_RED));                    /* start (east) */
    CHECK(fx_near(&f, 30, 10, 1, PX_RED));                    /* end (north) */
    CHECK(!fx_near(&f, 16, 44, 3, PX_RED));                   /* south-west quadrant untouched */
    CHECK(!fx_near(&f, 44, 44, 3, PX_RED));
    CHECK(!fx_near(&f, 16, 16, 3, PX_RED));
    EXPECT_EQ(fx_px(&f, 30, 30), PX_WHITE);                   /* no radii for Arc */
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(arc_clockwise_takes_long_way) {
    gdi_sel_t s = {0};
    NEED_SHAPES(); REQUIRE(U(SetArcDirection)); REQUIRE(U(GetArcDirection));
    FX(f, 64, 64);
    EXPECT_EQ(U(SetArcDirection)(f.dc, AD_CLOCKWISE), AD_COUNTERCLOCKWISE);
    EXPECT_EQ(U(GetArcDirection)(f.dc), AD_CLOCKWISE);
    sel_pen(f.dc, &s, COL_RED, 1); sel_nullbrush(f.dc, &s);
    CHECK(U(Arc)(f.dc, ARC_ARGS));
    CHECK(fx_near(&f, 16, 44, 2, PX_RED));                    /* south-west now covered */
    CHECK(fx_near(&f, 44, 44, 2, PX_RED));
    CHECK(fx_near(&f, 16, 16, 2, PX_RED));
    CHECK(!fx_near(&f, 44, 16, 3, PX_RED));                   /* the short NE quadrant is the part left out */
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(arc_direction_set_get) {
    NEED_SHAPES(); REQUIRE(U(SetArcDirection)); REQUIRE(U(GetArcDirection));
    FX(f, 16, 16);
    EXPECT_EQ(U(SetArcDirection)(f.dc, AD_CLOCKWISE), AD_COUNTERCLOCKWISE);
    EXPECT_EQ(U(SetArcDirection)(f.dc, AD_CLOCKWISE), AD_CLOCKWISE);
    EXPECT_EQ(U(SetArcDirection)(f.dc, AD_COUNTERCLOCKWISE), AD_CLOCKWISE);
    EXPECT_EQ(U(GetArcDirection)(f.dc), AD_COUNTERCLOCKWISE);
    if (UN(NtGdiGetDCDword) && !t_is_wine()) {    /* GetArcDirection is NtGdiGetDCDword(hdc, 4, &out) */
        DWORD v = 0xffff;
        CHECK(UN(NtGdiGetDCDword)(f.dc, 4, &v));
        EXPECT_EQ(v, AD_COUNTERCLOCKWISE);
        U(SetArcDirection)(f.dc, AD_CLOCKWISE);
        CHECK(UN(NtGdiGetDCDword)(f.dc, 4, &v));
        EXPECT_EQ(v, AD_CLOCKWISE);
        U(SetArcDirection)(f.dc, AD_COUNTERCLOCKWISE);
    }
    SET_ERR(0);
    { int r = U(SetArcDirection)(f.dc, 0); t_note("SetArcDirection(0) = %d err %lu", r, GetLastError());
      r = U(SetArcDirection)(f.dc, 3); t_note("SetArcDirection(3) = %d err %lu", r, GetLastError());
      t_note("direction afterwards %d", U(GetArcDirection)(f.dc)); }
    CHECK(U(GetArcDirection)(NULL) == 0);
    fx_close(&f);
}

XT(arc_direction_restored_by_restoredc) {
    int sv;
    NEED_SHAPES(); REQUIRE(U(SetArcDirection)); REQUIRE(U(SaveDC)); REQUIRE(U(RestoreDC));
    FX(f, 16, 16);
    sv = U(SaveDC)(f.dc);
    U(SetArcDirection)(f.dc, AD_CLOCKWISE);
    CHECK(U(RestoreDC)(f.dc, sv));
    EXPECT_EQ(U(GetArcDirection)(f.dc), AD_COUNTERCLOCKWISE);
    fx_close(&f);
}

XT(arc_full_circle_when_endpoints_equal) {
    gdi_sel_t s = {0}; int l, t, r, b;
    NEED_SHAPES();
    FX(f, 64, 64);
    sel_pen(f.dc, &s, COL_RED, 1); sel_nullbrush(f.dc, &s);
    CHECK(U(Arc)(f.dc, 10, 10, 50, 50, 50, 30, 50, 30));     /* start == end radial: the whole ellipse */
    fx_bbox(&f, PX_RED, &l, &t, &r, &b);
    CHECK(l == 10 && t == 10 && r >= 48 && b >= 48);
    CHECK(fx_near(&f, 16, 16, 2, PX_RED) && fx_near(&f, 44, 44, 2, PX_RED));
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(pie_has_radii_and_fill) {
    gdi_sel_t s = {0};
    NEED_SHAPES();
    FX(f, 64, 64);
    sel_pen(f.dc, &s, COL_RED, 1); sel_brush(f.dc, &s, COL_BLUE);
    CHECK(U(Pie)(f.dc, ARC_ARGS));
    CHECK(fx_near(&f, 30, 30, 1, PX_RED));                    /* the two radii meet at the centre */
    CHECK(fx_near(&f, 40, 30, 1, PX_RED));                    /* east radius */
    CHECK(fx_near(&f, 30, 20, 1, PX_RED));                    /* north radius */
    EXPECT_EQ(fx_px(&f, 38, 22), PX_BLUE);                    /* inside the NE sector */
    EXPECT_EQ(fx_px(&f, 22, 38), PX_WHITE);                   /* outside it */
    EXPECT_EQ(fx_px(&f, 22, 22), PX_WHITE);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(chord_has_secant_and_fill) {
    gdi_sel_t s = {0};
    NEED_SHAPES();
    FX(f, 64, 64);
    sel_pen(f.dc, &s, COL_RED, 1); sel_brush(f.dc, &s, COL_BLUE);
    CHECK(U(Chord)(f.dc, ARC_ARGS));
    CHECK(fx_near(&f, 40, 20, 1, PX_RED));                    /* midpoint of the secant (50,30)-(30,10) */
    EXPECT_EQ(fx_px(&f, 43, 20), PX_BLUE);                    /* between the secant and the arc */
    EXPECT_EQ(fx_px(&f, 30, 30), PX_WHITE);                   /* the centre is on the far side of the secant */
    EXPECT_EQ(fx_px(&f, 36, 24), PX_WHITE);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(arcto_draws_connecting_line_and_moves) {
    gdi_sel_t s = {0}; POINT cur;
    NEED_SHAPES();
    FX(f, 64, 64);
    sel_pen(f.dc, &s, COL_RED, 1); sel_nullbrush(f.dc, &s);
    U(MoveToEx)(f.dc, 5, 5, NULL);
    CHECK(U(ArcTo)(f.dc, ARC_ARGS));
    CHECK(fx_near(&f, 27, 17, 2, PX_RED));                    /* the line from (5,5) towards the arc start (50,30) */
    CHECK(fx_near(&f, 44, 16, 2, PX_RED));                    /* the arc itself */
    CHECK(U(GetCurrentPositionEx)(f.dc, &cur));
    t_note("ArcTo current position (%ld,%ld)", cur.x, cur.y);
    CHECK(cur.x >= 28 && cur.x <= 32 && cur.y >= 9 && cur.y <= 12);   /* the arc's end point on the circle */
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(arc_does_not_move_current_position) {
    gdi_sel_t s = {0}; POINT cur;
    NEED_SHAPES();
    FX(f, 64, 64);
    sel_pen(f.dc, &s, COL_RED, 1); sel_nullbrush(f.dc, &s);
    U(MoveToEx)(f.dc, 3, 4, NULL);
    CHECK(U(Arc)(f.dc, ARC_ARGS));
    CHECK(U(Pie)(f.dc, ARC_ARGS));
    CHECK(U(Chord)(f.dc, ARC_ARGS));
    CHECK(U(Ellipse)(f.dc, 10, 10, 20, 20));
    CHECK(U(Rectangle)(f.dc, 10, 10, 20, 20));
    CHECK(U(RoundRect)(f.dc, 10, 10, 30, 30, 4, 4));
    CHECK(U(GetCurrentPositionEx)(f.dc, &cur));
    EXPECT_INT(cur.x, 3); EXPECT_INT(cur.y, 4);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(anglearc_line_then_arc) {
    gdi_sel_t s = {0}; POINT cur;
    NEED_SHAPES();
    FX(f, 100, 100);
    sel_pen(f.dc, &s, COL_RED, 1);
    U(MoveToEx)(f.dc, 10, 50, NULL);
    CHECK(U(AngleArc)(f.dc, 50, 50, 20, 0.0f, 90.0f));
    CHECK(fx_near(&f, 30, 50, 0, PX_RED));                    /* leading line from (10,50) to the arc start (70,50) */
    CHECK(fx_near(&f, 64, 36, 2, PX_RED));                    /* arc between east and north */
    CHECK(U(GetCurrentPositionEx)(f.dc, &cur));
    t_note("AngleArc current position (%ld,%ld)", cur.x, cur.y);
    CHECK(cur.x >= 49 && cur.x <= 51 && cur.y >= 29 && cur.y <= 31);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(anglearc_negative_sweep_clockwise) {
    gdi_sel_t s = {0}; POINT cur;
    NEED_SHAPES();
    FX(f, 100, 100);
    sel_pen(f.dc, &s, COL_RED, 1);
    U(MoveToEx)(f.dc, 70, 50, NULL);
    CHECK(U(AngleArc)(f.dc, 50, 50, 20, 0.0f, -90.0f));
    CHECK(U(GetCurrentPositionEx)(f.dc, &cur));
    t_note("AngleArc(-90) current position (%ld,%ld)", cur.x, cur.y);
    CHECK(cur.x >= 49 && cur.x <= 51 && cur.y >= 69 && cur.y <= 71);   /* swept towards the bottom of the screen */
    CHECK(fx_near(&f, 64, 64, 2, PX_RED));
    CHECK(!fx_near(&f, 64, 36, 3, PX_RED));
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(anglearc_with_clockwise_arc_direction) {
    gdi_sel_t s = {0}; POINT cur;
    NEED_SHAPES(); REQUIRE(U(SetArcDirection));
    FX(f, 100, 100);
    sel_pen(f.dc, &s, COL_RED, 1);
    U(SetArcDirection)(f.dc, AD_CLOCKWISE);
    U(MoveToEx)(f.dc, 70, 50, NULL);
    CHECK(U(AngleArc)(f.dc, 50, 50, 20, 0.0f, 90.0f));
    CHECK(U(GetCurrentPositionEx)(f.dc, &cur));
    t_note("AngleArc under AD_CLOCKWISE ends at (%ld,%ld)", cur.x, cur.y);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(anglearc_invalid_hdc) {
    NEED_SHAPES();
    CHECK(!U(AngleArc)(NULL, 50, 50, 20, 0.0f, 90.0f));
    CHECK(!U(Arc)(NULL, ARC_ARGS));
    CHECK(!U(ArcTo)(NULL, ARC_ARGS));
    CHECK(!U(Pie)(NULL, ARC_ARGS));
    CHECK(!U(Chord)(NULL, ARC_ARGS));
    CHECK(!U(Ellipse)(NULL, 1, 1, 5, 5));
    CHECK(!U(RoundRect)(NULL, 1, 1, 5, 5, 2, 2));
}

/* ---- NtGdiArcInternal direct: same pixels as the gdi32 callers --------------------------------------------- */
typedef struct { int type; const char *name; } arctype_t;

XT(direct_arcinternal_matches_gdi32) {
    static const arctype_t t[] = { { 0, "Arc" }, { 1, "ArcTo" }, { 2, "Chord" }, { 3, "Pie" } };
    size_t i;
    NEED_SHAPES(); REQUIRE_NT(NtGdiArcInternal); REQUIRE(U(GdiFlush));
    for (i = 0; i < sizeof t / sizeof t[0]; i++) {
        gdi_sel_t s1 = {0}, s2 = {0}; fx_t a, b; POINT ca, cb;
        if (!fx_open(&a, 64, 64) || !fx_open(&b, 64, 64)) { t_skip("fixtures"); return; }
        sel_pen(a.dc, &s1, COL_RED, 1); sel_brush(a.dc, &s1, COL_BLUE);
        sel_pen(b.dc, &s2, COL_RED, 1); sel_brush(b.dc, &s2, COL_BLUE);
        U(MoveToEx)(a.dc, 5, 5, NULL); U(MoveToEx)(b.dc, 5, 5, NULL);
        switch (t[i].type) {
        case 0: CHECK(U(Arc)(a.dc, ARC_ARGS)); break;
        case 1: CHECK(U(ArcTo)(a.dc, ARC_ARGS)); break;
        case 2: CHECK(U(Chord)(a.dc, ARC_ARGS)); break;
        default: CHECK(U(Pie)(a.dc, ARC_ARGS)); break;
        }
        U(GdiFlush)();
        CHECKF(UN(NtGdiArcInternal)(t[i].type, b.dc, ARC_ARGS), "%s direct", t[i].name);
        EXPECT_INT(fx_diff(&a, &b), 0);
        U(GetCurrentPositionEx)(a.dc, &ca); U(GetCurrentPositionEx)(b.dc, &cb);
        EXPECT_INT(ca.x, cb.x); EXPECT_INT(ca.y, cb.y);
        CHECKF(fx_count(&a, PX_WHITE) < 64 * 64, "%s painted something", t[i].name);
        sel_done(a.dc, &s1); sel_done(b.dc, &s2);
        fx_close(&a); fx_close(&b);
    }
}

XT(direct_arcinternal_invalid) {
    NEED_SHAPES(); REQUIRE_NT(NtGdiArcInternal);
    CHECK(!UN(NtGdiArcInternal)(0, NULL, ARC_ARGS));
    CHECK(!UN(NtGdiArcInternal)(3, (HDC)(ULONG_PTR)0x01010fff, ARC_ARGS));
    CHECK(!UN(NtGdiArcInternal)(0, (HDC)U(GetStockObject)(BLACK_PEN), ARC_ARGS));
    {
        FX(f, 32, 32);
        SET_ERR(0);
        t_note("NtGdiArcInternal(type 9) = %d", (int)UN(NtGdiArcInternal)(9, f.dc, ARC_ARGS));
        fx_close(&f);
    }
}

XT(direct_shapes_match_gdi32) {
    gdi_sel_t s1 = {0}, s2 = {0}; fx_t a, b;
    NEED_SHAPES(); REQUIRE_NT(NtGdiEllipse); REQUIRE_NT(NtGdiRoundRect); REQUIRE_NT(NtGdiRectangle);
    if (!fx_open(&a, 64, 64) || !fx_open(&b, 64, 64)) { t_skip("fixtures"); return; }
    sel_pen(a.dc, &s1, COL_RED, 1); sel_brush(a.dc, &s1, COL_BLUE);
    sel_pen(b.dc, &s2, COL_RED, 1); sel_brush(b.dc, &s2, COL_BLUE);
    CHECK(U(Ellipse)(a.dc, 5, 6, 40, 30)); CHECK(U(RoundRect)(a.dc, 20, 20, 60, 58, 10, 12)); CHECK(U(Rectangle)(a.dc, 2, 40, 18, 60));
    U(GdiFlush)();
    CHECK(UN(NtGdiEllipse)(b.dc, 5, 6, 40, 30)); CHECK(UN(NtGdiRoundRect)(b.dc, 20, 20, 60, 58, 10, 12)); CHECK(UN(NtGdiRectangle)(b.dc, 2, 40, 18, 60));
    EXPECT_INT(fx_diff(&a, &b), 0);
    CHECK(!UN(NtGdiEllipse)(NULL, 5, 6, 40, 30));
    CHECK(!UN(NtGdiRoundRect)(NULL, 20, 20, 60, 58, 10, 12));
    CHECK(!UN(NtGdiRectangle)(NULL, 2, 40, 18, 60));
    sel_done(a.dc, &s1); sel_done(b.dc, &s2);
    fx_close(&a); fx_close(&b);
}

XT(direct_moveto_lineto_pixels) {
    gdi_sel_t s = {0}; POINT prev = { 0x55, 0x55 }; int x;
    NEED_SHAPES(); REQUIRE_NT(NtGdiMoveTo); REQUIRE_NT(NtGdiLineTo);
    FX(f, 48, 48);
    sel_pen(f.dc, &s, COL_RED, 1);
    CHECK(UN(NtGdiMoveTo)(f.dc, 8, 9, &prev));
    EXPECT_INT(prev.x, 0); EXPECT_INT(prev.y, 0);
    CHECK(UN(NtGdiLineTo)(f.dc, 18, 9));
    for (x = 8; x < 18; x++) EXPECT_EQ(fx_px(&f, x, 9), PX_RED);
    EXPECT_EQ(fx_px(&f, 18, 9), PX_WHITE);
    CHECK(UN(NtGdiMoveTo)(f.dc, 1, 1, &prev));
    EXPECT_INT(prev.x, 18); EXPECT_INT(prev.y, 9);           /* previous position as the kernel tracked it */
    CHECK(!UN(NtGdiMoveTo)(NULL, 1, 1, &prev));
    CHECK(!UN(NtGdiLineTo)(NULL, 1, 1));
    sel_done(f.dc, &s);
    fx_close(&f);
}
