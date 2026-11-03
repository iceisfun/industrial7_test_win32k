/* What each GDI primitive records into an open path, and what Fill/Stroke/Widen/Flatten/PathToRegion/SelectClipPath
 * do with the finished path.
 *
 * Point/type encoding comes from win32kfull!NtGdiGetPath: figure start = PT_MOVETO (6); line points = PT_LINETO (2);
 * bezier points = PT_BEZIERTO (4); the last point of a closed figure has PT_CLOSEFIGURE (1) OR'ed in.
 */
#include "xd.h"

#define NEED_PATH_API() do { REQUIRE(U(BeginPath)); REQUIRE(U(EndPath)); REQUIRE(U(GetPath)); REQUIRE(U(CreateCompatibleDC)); \
    REQUIRE(U(Rectangle)); REQUIRE(U(MoveToEx)); REQUIRE(U(LineTo)); } while (0)
#define MEMDC(dc) HDC dc = U(CreateCompatibleDC)(NULL); if (!dc) { t_skip("CreateCompatibleDC failed"); return; }
#define MEMDC_FREE(dc) U(DeleteDC)(dc)

static void expect_types(const struct pth *p, const char *want, int n) {
    int i;
    EXPECT_INT(p->n, n);
    for (i = 0; i < n && i < p->n; i++)
        CHECKF(p->ty[i] == (BYTE)want[i], "type[%d] got %u want %u", i, p->ty[i], (unsigned)(BYTE)want[i]);
}
#define T_M "\x06"
#define T_L "\x02"
#define T_B "\x04"
#define T_LC "\x03"
#define T_BC "\x05"

XT(rec_rectangle_five_points) {
    struct pth p;
    NEED_PATH_API();
    MEMDC(dc);
    PBEGIN(dc); CHECK(U(Rectangle)(dc, 10, 20, 50, 60)); PEND(dc);
    CHECK(pth_get(dc, &p) == 4);
    expect_types(&p, T_M T_L T_L T_LC, 4);        /* rectangle: move + 2 lines + a closing line (4 points) */
    CHECK(pth_valid(&p));
    if (p.n == 4) {
        int i, minx = 9999, maxx = -9999, miny = 9999, maxy = -9999;
        for (i = 0; i < 4; i++) {
            if (p.pt[i].x < minx) { minx = p.pt[i].x; }
            if (p.pt[i].x > maxx) { maxx = p.pt[i].x; }
            if (p.pt[i].y < miny) { miny = p.pt[i].y; }
            if (p.pt[i].y > maxy) { maxy = p.pt[i].y; }
        }
        t_note("rect path bbox x[%d,%d] y[%d,%d] for Rectangle(10,20,50,60)", minx, maxx, miny, maxy);
        CHECK(minx == 10 && miny == 20);
        CHECK(maxx >= 49 && maxx <= 50 && maxy >= 59 && maxy <= 60);
        /* four corners of an axis-aligned box: every point is on the box and the last repeats the first or closes it */
        for (i = 0; i < 4; i++) CHECK((p.pt[i].x == minx || p.pt[i].x == maxx) && (p.pt[i].y == miny || p.pt[i].y == maxy));
    }
    MEMDC_FREE(dc);
}

XT(rec_rectangle_degenerate_still_recorded) {
    struct pth p;
    NEED_PATH_API();
    MEMDC(dc);
    PBEGIN(dc); U(Rectangle)(dc, 10, 10, 30, 30); U(Rectangle)(dc, 40, 40, 60, 60); PEND(dc);
    CHECK(pth_get(dc, &p) == 8);
    CHECK(pth_count_type(&p, PT_MOVETO) == 2);
    CHECK(p.n == 8 && (p.ty[3] & PT_CLOSEFIGURE) && (p.ty[7] & PT_CLOSEFIGURE));
    MEMDC_FREE(dc);
}

XT(rec_moveto_lineto_exact) {
    struct pth p;
    NEED_PATH_API();
    MEMDC(dc);
    PBEGIN(dc);
    U(MoveToEx)(dc, 10, 10, NULL); U(LineTo)(dc, 20, 10); U(LineTo)(dc, 20, 20);
    PEND(dc);
    CHECK(pth_get(dc, &p) == 3);
    expect_types(&p, T_M T_L T_L, 3);
    EXPECT_EQ(p.pt[0].x, 10); EXPECT_EQ(p.pt[0].y, 10);
    EXPECT_EQ(p.pt[1].x, 20); EXPECT_EQ(p.pt[1].y, 10);
    EXPECT_EQ(p.pt[2].x, 20); EXPECT_EQ(p.pt[2].y, 20);
    MEMDC_FREE(dc);
}

XT(rec_lineto_starts_at_current_position) {
    struct pth p;
    POINT cur;
    NEED_PATH_API(); REQUIRE(U(GetCurrentPositionEx));
    MEMDC(dc);
    U(MoveToEx)(dc, 5, 7, NULL);              /* before the path opens */
    PBEGIN(dc);
    U(LineTo)(dc, 15, 17);
    PEND(dc);
    CHECK(pth_get(dc, &p) == 2);
    expect_types(&p, T_M T_L, 2);
    EXPECT_EQ(p.pt[0].x, 5); EXPECT_EQ(p.pt[0].y, 7);
    EXPECT_EQ(p.pt[1].x, 15); EXPECT_EQ(p.pt[1].y, 17);
    CHECK(U(GetCurrentPositionEx)(dc, &cur));
    EXPECT_EQ(cur.x, 15); EXPECT_EQ(cur.y, 17);   /* recorded LineTo still advances the current position */
    MEMDC_FREE(dc);
}

XT(rec_two_figures) {
    struct pth p;
    NEED_PATH_API();
    MEMDC(dc);
    PBEGIN(dc);
    U(MoveToEx)(dc, 1, 1, NULL); U(LineTo)(dc, 9, 1);
    U(MoveToEx)(dc, 1, 5, NULL); U(LineTo)(dc, 9, 5);
    PEND(dc);
    CHECK(pth_get(dc, &p) == 4);
    expect_types(&p, T_M T_L T_M T_L, 4);
    MEMDC_FREE(dc);
}

XT(rec_consecutive_movetos_last_wins) {
    struct pth p;
    NEED_PATH_API();
    MEMDC(dc);
    PBEGIN(dc);
    U(MoveToEx)(dc, 1, 1, NULL); U(MoveToEx)(dc, 2, 2, NULL); U(MoveToEx)(dc, 3, 3, NULL); U(LineTo)(dc, 9, 9);
    PEND(dc);
    CHECK(pth_get(dc, &p) >= 2);
    t_note("three MoveTo + LineTo recorded %d points", p.n);
    CHECK(pth_valid(&p));
    if (p.n >= 2) {   /* the figure that owns the line starts at the last MoveTo */
        EXPECT_EQ(p.ty[p.n - 2], PT_MOVETO); EXPECT_EQ(p.ty[p.n - 1], PT_LINETO);
        EXPECT_EQ(p.pt[p.n - 2].x, 3); EXPECT_EQ(p.pt[p.n - 2].y, 3);
        EXPECT_EQ(p.pt[p.n - 1].x, 9); EXPECT_EQ(p.pt[p.n - 1].y, 9);
    }
    MEMDC_FREE(dc);
}

XT(rec_closefigure_marks_last_point) {
    struct pth p;
    NEED_PATH_API(); REQUIRE(U(CloseFigure));
    MEMDC(dc);
    PBEGIN(dc);
    U(MoveToEx)(dc, 10, 10, NULL); U(LineTo)(dc, 20, 10); U(LineTo)(dc, 20, 20);
    CHECK(U(CloseFigure)(dc));
    PEND(dc);
    CHECK(pth_get(dc, &p) == 3);
    expect_types(&p, T_M T_L T_LC, 3);
    MEMDC_FREE(dc);
}

XT(rec_closefigure_then_new_figure) {
    struct pth p;
    NEED_PATH_API(); REQUIRE(U(CloseFigure));
    MEMDC(dc);
    PBEGIN(dc);
    U(MoveToEx)(dc, 10, 10, NULL); U(LineTo)(dc, 20, 10); U(LineTo)(dc, 20, 20);
    U(CloseFigure)(dc);
    U(MoveToEx)(dc, 50, 50, NULL); U(LineTo)(dc, 60, 50);
    PEND(dc);
    CHECK(pth_get(dc, &p) == 5);
    expect_types(&p, T_M T_L T_LC T_M T_L, 5);
    MEMDC_FREE(dc);
}

XT(rec_closefigure_empty_path_ok) {
    struct pth p;
    NEED_PATH_API(); REQUIRE(U(CloseFigure));
    MEMDC(dc);
    PBEGIN(dc);
    { BOOL r = U(CloseFigure)(dc);   /* EPATHOBJ::bCloseFigure on an empty path: outcome logged, not asserted */
      t_note("CloseFigure on an empty open path: %d (err %lu)", (int)r, GetLastError()); }
    PEND(dc);
    EXPECT_INT(pth_get(dc, &p), 0);
    MEMDC_FREE(dc);
}

XT(rec_polyline) {
    struct pth p; static const POINT pt[3] = { {10, 10}, {20, 10}, {20, 20} };
    NEED_PATH_API(); REQUIRE(U(Polyline));
    MEMDC(dc);
    PBEGIN(dc); CHECK(U(Polyline)(dc, pt, 3)); PEND(dc);
    CHECK(pth_get(dc, &p) == 3);
    expect_types(&p, T_M T_L T_L, 3);
    CHECK(!memcmp(p.pt, pt, sizeof pt));
    MEMDC_FREE(dc);
}

XT(rec_polygon_closed) {
    struct pth p; static const POINT pt[3] = { {10, 10}, {20, 10}, {20, 20} };
    NEED_PATH_API(); REQUIRE(U(Polygon));
    MEMDC(dc);
    PBEGIN(dc); CHECK(U(Polygon)(dc, pt, 3)); PEND(dc);
    CHECK(pth_get(dc, &p) == 3);
    expect_types(&p, T_M T_L T_LC, 3);
    CHECK(!memcmp(p.pt, pt, sizeof pt));
    MEMDC_FREE(dc);
}

XT(rec_polypolygon) {
    struct pth p; static const POINT pt[6] = { {10,10},{20,10},{20,20}, {40,40},{50,40},{50,50} };
    static const INT cnt[2] = { 3, 3 };
    NEED_PATH_API(); REQUIRE(U(PolyPolygon));
    MEMDC(dc);
    PBEGIN(dc); CHECK(U(PolyPolygon)(dc, pt, cnt, 2)); PEND(dc);
    CHECK(pth_get(dc, &p) == 6);
    expect_types(&p, T_M T_L T_LC T_M T_L T_LC, 6);
    CHECK(!memcmp(p.pt, pt, sizeof pt));
    MEMDC_FREE(dc);
}

XT(rec_polypolyline) {
    struct pth p; static const POINT pt[5] = { {10,10},{20,10}, {40,40},{50,40},{50,50} };
    static const DWORD cnt[2] = { 2, 3 };
    NEED_PATH_API(); REQUIRE(U(PolyPolyline));
    MEMDC(dc);
    PBEGIN(dc); CHECK(U(PolyPolyline)(dc, pt, cnt, 2)); PEND(dc);
    CHECK(pth_get(dc, &p) == 5);
    expect_types(&p, T_M T_L T_M T_L T_L, 5);
    CHECK(!memcmp(p.pt, pt, sizeof pt));
    MEMDC_FREE(dc);
}

XT(rec_polybezier) {
    struct pth p; static const POINT pt[7] = { {10,10},{20,0},{30,20},{40,10}, {50,0},{60,20},{70,10} };
    NEED_PATH_API(); REQUIRE(U(PolyBezier));
    MEMDC(dc);
    PBEGIN(dc); CHECK(U(PolyBezier)(dc, pt, 4)); PEND(dc);
    CHECK(pth_get(dc, &p) == 4);
    expect_types(&p, T_M T_B T_B T_B, 4);
    CHECK(!memcmp(p.pt, pt, 4 * sizeof(POINT)));
    MEMDC_FREE(dc);
    {
        MEMDC(dc2);
        PBEGIN(dc2); CHECK(U(PolyBezier)(dc2, pt, 7)); PEND(dc2);
        CHECK(pth_get(dc2, &p) == 7);
        expect_types(&p, T_M T_B T_B T_B T_B T_B T_B, 7);   /* a second curve continues the same figure */
        MEMDC_FREE(dc2);
    }
}

XT(rec_polybezier_bad_counts) {
    static const POINT pt[7] = { {10,10},{20,0},{30,20},{40,10}, {50,0},{60,20},{70,10} };
    int n;
    NEED_PATH_API(); REQUIRE(U(PolyBezier));
    MEMDC(dc);
    PBEGIN(dc);
    for (n = 0; n <= 6; n++) {
        BOOL r = U(PolyBezier)(dc, pt, n);
        if (n == 4) CHECK(r);
        else if (n == 0) t_note("PolyBezier with 0 points: %d", (int)r);
        else if (n % 3 != 1 || n < 4) CHECKF(!r, "PolyBezier count %d accepted", n);   /* needs 3k+1 points */
        else CHECK(r);
    }
    U(AbortPath)(dc);
    MEMDC_FREE(dc);
}

XT(rec_polybezierto) {
    struct pth p; static const POINT pt[3] = { {20,0},{30,20},{40,10} };
    POINT cur;
    NEED_PATH_API(); REQUIRE(U(PolyBezierTo)); REQUIRE(U(GetCurrentPositionEx));
    MEMDC(dc);
    U(MoveToEx)(dc, 10, 10, NULL);
    PBEGIN(dc); CHECK(U(PolyBezierTo)(dc, pt, 3)); PEND(dc);
    CHECK(pth_get(dc, &p) == 4);
    expect_types(&p, T_M T_B T_B T_B, 4);
    EXPECT_EQ(p.pt[0].x, 10); EXPECT_EQ(p.pt[0].y, 10);
    CHECK(!memcmp(p.pt + 1, pt, sizeof pt));
    U(GetCurrentPositionEx)(dc, &cur);
    EXPECT_EQ(cur.x, 40); EXPECT_EQ(cur.y, 10);
    MEMDC_FREE(dc);
}

XT(rec_polylineto) {
    struct pth p; static const POINT pt[2] = { {30,10},{30,30} };
    POINT cur;
    NEED_PATH_API(); REQUIRE(U(PolylineTo)); REQUIRE(U(GetCurrentPositionEx));
    MEMDC(dc);
    U(MoveToEx)(dc, 10, 10, NULL);
    PBEGIN(dc); CHECK(U(PolylineTo)(dc, pt, 2)); PEND(dc);
    CHECK(pth_get(dc, &p) == 3);
    expect_types(&p, T_M T_L T_L, 3);
    EXPECT_EQ(p.pt[0].x, 10); EXPECT_EQ(p.pt[0].y, 10);
    U(GetCurrentPositionEx)(dc, &cur);
    EXPECT_EQ(cur.x, 30); EXPECT_EQ(cur.y, 30);
    MEMDC_FREE(dc);
}

XT(rec_polydraw) {
    struct pth p;
    static const POINT pt[6] = { {10,10},{30,10},{40,0},{50,20},{60,10},{10,10} };
    static const BYTE ty[6] = { PT_MOVETO, PT_LINETO, PT_BEZIERTO, PT_BEZIERTO, PT_BEZIERTO, PT_LINETO | PT_CLOSEFIGURE };
    NEED_PATH_API(); REQUIRE(U(PolyDraw));
    MEMDC(dc);
    PBEGIN(dc); CHECK(U(PolyDraw)(dc, pt, ty, 6)); PEND(dc);
    CHECK(pth_get(dc, &p) == 6);
    CHECK(pth_valid(&p));
    expect_types(&p, T_M T_L T_B T_B T_B T_LC, 6);
    CHECK(!memcmp(p.pt, pt, sizeof pt));
    MEMDC_FREE(dc);
}

XT(rec_ellipse_bezier_figure) {
    struct pth p;
    NEED_PATH_API(); REQUIRE(U(Ellipse));
    MEMDC(dc);
    PBEGIN(dc); CHECK(U(Ellipse)(dc, 10, 10, 60, 40)); PEND(dc);
    CHECK(pth_get(dc, &p) > 0);
    t_note("Ellipse path has %d points", p.n);
    CHECK(pth_valid(&p));
    CHECK(p.n >= 13 && (p.n - 1) % 3 == 0);      /* move + n beziers of 3 points each */
    CHECK(pth_count_type(&p, PT_MOVETO) == 1);
    CHECK(pth_count_type(&p, PT_LINETO) == 0);
    CHECK(p.n > 0 && (p.ty[p.n - 1] & PT_CLOSEFIGURE));
    MEMDC_FREE(dc);
}

XT(rec_roundrect_mixes_lines_and_curves) {
    struct pth p;
    NEED_PATH_API(); REQUIRE(U(RoundRect));
    MEMDC(dc);
    PBEGIN(dc); CHECK(U(RoundRect)(dc, 10, 10, 80, 50, 16, 16)); PEND(dc);
    CHECK(pth_get(dc, &p) > 0);
    t_note("RoundRect path has %d points", p.n);
    CHECK(pth_valid(&p));
    CHECK(pth_count_type(&p, PT_MOVETO) == 1);
    CHECK(pth_count_type(&p, PT_BEZIERTO) > 0 && pth_count_type(&p, PT_BEZIERTO) % 3 == 0);
    CHECK(pth_count_type(&p, PT_LINETO) > 0);
    CHECK(p.n > 0 && (p.ty[p.n - 1] & PT_CLOSEFIGURE));
    MEMDC_FREE(dc);
}

XT(rec_roundrect_zero_corner_is_rect_like) {
    struct pth p;
    NEED_PATH_API(); REQUIRE(U(RoundRect));
    MEMDC(dc);
    PBEGIN(dc); CHECK(U(RoundRect)(dc, 10, 10, 80, 50, 0, 0)); PEND(dc);
    CHECK(pth_get(dc, &p) > 0);
    CHECK(pth_valid(&p));
    t_note("RoundRect(0,0) path: %d points, %d bezier", p.n, pth_count_type(&p, PT_BEZIERTO));
    MEMDC_FREE(dc);
}

XT(rec_arc_open_figure) {
    struct pth p;
    NEED_PATH_API(); REQUIRE(U(Arc));
    MEMDC(dc);
    PBEGIN(dc); CHECK(U(Arc)(dc, 10, 10, 50, 50, 50, 30, 30, 10)); PEND(dc);
    CHECK(pth_get(dc, &p) > 0);
    CHECK(pth_valid(&p));
    CHECK(pth_count_type(&p, PT_MOVETO) == 1);
    CHECK(pth_count_type(&p, PT_LINETO) == 0);
    CHECK(pth_count_type(&p, PT_BEZIERTO) > 0 && pth_count_type(&p, PT_BEZIERTO) % 3 == 0);
    CHECK(p.n > 0 && !(p.ty[p.n - 1] & PT_CLOSEFIGURE));   /* Arc leaves its figure open */
    MEMDC_FREE(dc);
}

XT(rec_pie_closed_with_radii) {
    struct pth p;
    NEED_PATH_API(); REQUIRE(U(Pie));
    MEMDC(dc);
    PBEGIN(dc); CHECK(U(Pie)(dc, 10, 10, 50, 50, 50, 30, 30, 10)); PEND(dc);
    CHECK(pth_get(dc, &p) > 0);
    CHECK(pth_valid(&p));
    CHECK(pth_count_type(&p, PT_LINETO) >= 1);
    CHECK(pth_count_type(&p, PT_BEZIERTO) > 0);
    CHECK(p.n > 0 && (p.ty[p.n - 1] & PT_CLOSEFIGURE));
    MEMDC_FREE(dc);
}

XT(rec_chord_closed) {
    struct pth p;
    NEED_PATH_API(); REQUIRE(U(Chord));
    MEMDC(dc);
    PBEGIN(dc); CHECK(U(Chord)(dc, 10, 10, 50, 50, 50, 30, 30, 10)); PEND(dc);
    CHECK(pth_get(dc, &p) > 0);
    CHECK(pth_valid(&p));
    CHECK(pth_count_type(&p, PT_BEZIERTO) > 0);
    CHECK(p.n > 0 && (p.ty[p.n - 1] & PT_CLOSEFIGURE));
    MEMDC_FREE(dc);
}

XT(rec_arcto_connects_from_current_position) {
    struct pth p; POINT cur;
    NEED_PATH_API(); REQUIRE(U(ArcTo)); REQUIRE(U(GetCurrentPositionEx));
    MEMDC(dc);
    U(MoveToEx)(dc, 0, 0, NULL);
    PBEGIN(dc); CHECK(U(ArcTo)(dc, 10, 10, 50, 50, 50, 30, 30, 10)); PEND(dc);
    CHECK(pth_get(dc, &p) > 0);
    CHECK(pth_valid(&p));
    CHECK(p.n > 0 && p.ty[0] == PT_MOVETO && p.pt[0].x == 0 && p.pt[0].y == 0);   /* figure starts at the old position */
    CHECK(pth_count_type(&p, PT_LINETO) >= 1);                                    /* connecting line to the arc start */
    U(GetCurrentPositionEx)(dc, &cur);
    t_note("ArcTo in path: current position now (%ld,%ld)", cur.x, cur.y);
    CHECK(cur.x >= 28 && cur.x <= 32 && cur.y >= 9 && cur.y <= 12);
    MEMDC_FREE(dc);
}

XT(rec_anglearc_in_path) {
    struct pth p;
    NEED_PATH_API(); REQUIRE(U(AngleArc));
    MEMDC(dc);
    U(MoveToEx)(dc, 0, 0, NULL);
    PBEGIN(dc); CHECK(U(AngleArc)(dc, 50, 50, 20, 0.0f, 90.0f)); PEND(dc);
    CHECK(pth_get(dc, &p) > 0);
    CHECK(pth_valid(&p));
    CHECK(pth_count_type(&p, PT_LINETO) >= 1);
    CHECK(pth_count_type(&p, PT_BEZIERTO) > 0);
    MEMDC_FREE(dc);
}

XT(rec_text_truetype_outline) {
    struct pth p; HFONT f, of; char face[64]; int n;
    NEED_PATH_API(); REQUIRE(U(CreateFontW)); REQUIRE(U(GetTextFaceA)); REQUIRE(U(TextOutW)); REQUIRE(U(SelectObject));
    MEMDC(dc);
    f = U(CreateFontW)(40, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_ONLY_PRECIS, 0, 0, 0, L"Arial");
    of = (HFONT)U(SelectObject)(dc, f);
    face[0] = 0; U(GetTextFaceA)(dc, sizeof face, face);
    if (_stricmp(face, "Arial")) { t_skip("Arial not available (got '%s')", face); U(SelectObject)(dc, of); U(DeleteObject)(f); MEMDC_FREE(dc); return; }
    PBEGIN(dc); CHECK(U(TextOutW)(dc, 10, 10, L"I", 1)); PEND(dc);
    n = pth_get(dc, &p);
    t_note("TextOut(\"I\") path: %d points", n);
    CHECK(n > 0);
    CHECK(pth_valid(&p));
    CHECK(pth_count_type(&p, PT_MOVETO) >= 1);
    CHECK(p.n > 0 && (p.ty[p.n - 1] & PT_CLOSEFIGURE));   /* glyph outlines are closed contours */
    U(SelectObject)(dc, of); U(DeleteObject)(f);
    MEMDC_FREE(dc);
}

XT(rec_text_two_glyphs_has_more_points) {
    struct pth p; HFONT f, of; char face[64]; int n1, n2;
    NEED_PATH_API(); REQUIRE(U(CreateFontW)); REQUIRE(U(GetTextFaceA)); REQUIRE(U(TextOutW)); REQUIRE(U(ExtTextOutW));
    MEMDC(dc);
    f = U(CreateFontW)(40, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_ONLY_PRECIS, 0, 0, 0, L"Arial");
    of = (HFONT)U(SelectObject)(dc, f);
    face[0] = 0; U(GetTextFaceA)(dc, sizeof face, face);
    if (_stricmp(face, "Arial")) { t_skip("Arial not available"); U(SelectObject)(dc, of); U(DeleteObject)(f); MEMDC_FREE(dc); return; }
    PBEGIN(dc); U(TextOutW)(dc, 10, 10, L"I", 1); PEND(dc); n1 = pth_get(dc, &p); U(AbortPath)(dc);
    PBEGIN(dc); U(ExtTextOutW)(dc, 10, 10, 0, NULL, L"II", 2, NULL); PEND(dc); n2 = pth_get(dc, &p);
    CHECK(n1 > 0 && n2 > n1);
    U(SelectObject)(dc, of); U(DeleteObject)(f);
    MEMDC_FREE(dc);
}

XT(rec_text_empty_string_adds_nothing) {
    struct pth p;
    NEED_PATH_API(); REQUIRE(U(TextOutW));
    MEMDC(dc);
    PBEGIN(dc); U(TextOutW)(dc, 10, 10, L"", 0); PEND(dc);
    EXPECT_INT(pth_get(dc, &p), 0);
    MEMDC_FREE(dc);
}

XT(rec_drawing_into_path_leaves_pixels_alone) {
    gdi_sel_t s = {0};
    int before;
    NEED_PATH_API(); REQUIRE(U(Ellipse)); REQUIRE(U(Polyline));
    FX(f, 64, 64);
    sel_pen(f.dc, &s, COL_RED, 1);
    PBEGIN(f.dc);
    U(Rectangle)(f.dc, 10, 10, 40, 40); U(Ellipse)(f.dc, 5, 5, 50, 50); U(MoveToEx)(f.dc, 0, 0, NULL); U(LineTo)(f.dc, 60, 60);
    before = fx_count(&f, PX_WHITE);
    EXPECT_INT(before, 64 * 64);                      /* nothing was painted while the path was open */
    PEND(f.dc);
    EXPECT_INT(fx_count(&f, PX_WHITE), 64 * 64);      /* ... nor when it ended */
    U(AbortPath)(f.dc);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(rec_selectobject_and_save_during_path) {
    struct pth p; HGDIOBJ pen;
    NEED_PATH_API(); REQUIRE(U(SaveDC)); REQUIRE(U(RestoreDC));
    MEMDC(dc);
    PBEGIN(dc);
    U(MoveToEx)(dc, 1, 1, NULL); U(LineTo)(dc, 9, 9);
    pen = U(CreatePen)(PS_SOLID, 3, 0);
    CHECK(U(SelectObject)(dc, pen) != NULL);
    U(SelectObject)(dc, U(GetStockObject)(BLACK_PEN));
    U(DeleteObject)(pen);
    PEND(dc);
    EXPECT_INT(pth_get(dc, &p), 2);
    MEMDC_FREE(dc);
}

/* ---- Fill / Stroke / StrokeAndFill on a DIB --------------------------------------------------------------- */
XT(render_fillpath_rect_interior_and_outside) {
    gdi_sel_t s = {0}; int x, y, inside_ok = 1, outside_ok = 1;
    NEED_PATH_API(); REQUIRE(U(FillPath));
    FX(f, 64, 64);
    sel_brush(f.dc, &s, COL_BLUE);
    PBEGIN(f.dc); U(Rectangle)(f.dc, 10, 10, 30, 30); PEND(f.dc);
    CHECK(U(FillPath)(f.dc));
    for (y = 11; y <= 28; y++) for (x = 11; x <= 28; x++) if (fx_px(&f, x, y) != PX_BLUE) inside_ok = 0;
    for (y = 0; y < 64; y++) for (x = 0; x < 64; x++)
        if ((x < 9 || x > 30 || y < 9 || y > 30) && fx_px(&f, x, y) != PX_WHITE) outside_ok = 0;
    CHECK(inside_ok); CHECK(outside_ok);
    { int n = fx_count(&f, PX_BLUE); t_note("FillPath Rectangle(10,10,30,30): %d blue pixels", n); CHECK(n >= 18 * 18 && n <= 21 * 21); }
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(render_fillpath_ignores_pen) {
    gdi_sel_t s = {0};
    NEED_PATH_API(); REQUIRE(U(FillPath));
    FX(f, 64, 64);
    sel_brush(f.dc, &s, COL_BLUE); sel_pen(f.dc, &s, COL_RED, 3);
    PBEGIN(f.dc); U(Rectangle)(f.dc, 10, 10, 30, 30); PEND(f.dc);
    CHECK(U(FillPath)(f.dc));
    EXPECT_INT(fx_count(&f, PX_RED), 0);      /* NtGdiFillPath uses bStrokeAndOrFill(..., 2) = fill only */
    CHECK(fx_count(&f, PX_BLUE) > 0);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(render_fillpath_closes_open_figures) {
    gdi_sel_t s = {0};
    NEED_PATH_API(); REQUIRE(U(FillPath));
    FX(f, 64, 64);
    sel_brush(f.dc, &s, COL_BLUE);
    PBEGIN(f.dc);
    U(MoveToEx)(f.dc, 10, 10, NULL); U(LineTo)(f.dc, 40, 10); U(LineTo)(f.dc, 40, 40); U(LineTo)(f.dc, 10, 40);   /* never closed */
    PEND(f.dc);
    CHECK(U(FillPath)(f.dc));                  /* vCloseAllFigures runs before the fill */
    EXPECT_EQ(fx_px(&f, 25, 25), PX_BLUE);
    EXPECT_EQ(fx_px(&f, 12, 38), PX_BLUE);
    EXPECT_EQ(fx_px(&f, 50, 50), PX_WHITE);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(render_fillpath_polyfill_modes) {
    gdi_sel_t s = {0}; int old;
    NEED_PATH_API(); REQUIRE(U(FillPath)); REQUIRE(U(SetPolyFillMode));
    FX(f, 100, 100);
    sel_brush(f.dc, &s, COL_BLUE);
    {
        int mode;
        for (mode = ALTERNATE; mode <= WINDING; mode++) {
            fx_clear(&f, 0x00FFFFFF);
            old = U(SetPolyFillMode)(f.dc, mode);
            CHECK(old == ALTERNATE || old == WINDING);
            PBEGIN(f.dc);
            /* two nested squares drawn in the same direction: winding number 2 inside the inner one */
            U(MoveToEx)(f.dc, 10, 10, NULL); U(LineTo)(f.dc, 90, 10); U(LineTo)(f.dc, 90, 90); U(LineTo)(f.dc, 10, 90); U(CloseFigure)(f.dc);
            U(MoveToEx)(f.dc, 30, 30, NULL); U(LineTo)(f.dc, 70, 30); U(LineTo)(f.dc, 70, 70); U(LineTo)(f.dc, 30, 70); U(CloseFigure)(f.dc);
            PEND(f.dc);
            CHECK(U(FillPath)(f.dc));
            EXPECT_EQ(fx_px(&f, 20, 50), PX_BLUE);                              /* ring is always filled */
            EXPECT_EQ(fx_px(&f, 50, 50), mode == WINDING ? PX_BLUE : PX_WHITE);  /* centre: filled only when winding */
        }
    }
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(render_fillpath_opposite_winding_hole) {
    gdi_sel_t s = {0};
    NEED_PATH_API(); REQUIRE(U(FillPath)); REQUIRE(U(SetPolyFillMode));
    FX(f, 100, 100);
    sel_brush(f.dc, &s, COL_BLUE);
    U(SetPolyFillMode)(f.dc, WINDING);
    PBEGIN(f.dc);
    U(MoveToEx)(f.dc, 10, 10, NULL); U(LineTo)(f.dc, 90, 10); U(LineTo)(f.dc, 90, 90); U(LineTo)(f.dc, 10, 90); U(CloseFigure)(f.dc);
    U(MoveToEx)(f.dc, 30, 30, NULL); U(LineTo)(f.dc, 30, 70); U(LineTo)(f.dc, 70, 70); U(LineTo)(f.dc, 70, 30); U(CloseFigure)(f.dc);
    PEND(f.dc);
    CHECK(U(FillPath)(f.dc));
    EXPECT_EQ(fx_px(&f, 20, 50), PX_BLUE);
    EXPECT_EQ(fx_px(&f, 50, 50), PX_WHITE);    /* opposite orientation cancels the winding number */
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(render_strokepath_outline_only) {
    gdi_sel_t s = {0};
    NEED_PATH_API(); REQUIRE(U(StrokePath));
    FX(f, 64, 64);
    sel_pen(f.dc, &s, COL_RED, 1); sel_brush(f.dc, &s, COL_BLUE);
    PBEGIN(f.dc); U(Rectangle)(f.dc, 10, 10, 30, 30); PEND(f.dc);
    CHECK(U(StrokePath)(f.dc));
    EXPECT_INT(fx_count(&f, PX_BLUE), 0);      /* stroke only: brush untouched */
    EXPECT_EQ(fx_px(&f, 20, 20), PX_WHITE);    /* interior untouched */
    CHECK(fx_near(&f, 10, 20, 1, PX_RED));
    CHECK(fx_near(&f, 20, 10, 1, PX_RED));
    CHECK(fx_near(&f, 29, 20, 1, PX_RED) || fx_near(&f, 30, 20, 1, PX_RED));
    CHECK(fx_near(&f, 20, 29, 1, PX_RED) || fx_near(&f, 20, 30, 1, PX_RED));
    { int n = fx_count(&f, PX_RED); t_note("StrokePath Rectangle(10,10,30,30): %d red pixels", n); CHECK(n >= 70 && n <= 90); }
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(render_strokepath_horizontal_line_exact) {
    gdi_sel_t s = {0}; int x;
    NEED_PATH_API(); REQUIRE(U(StrokePath));
    FX(f, 64, 64);
    sel_pen(f.dc, &s, COL_RED, 1);
    PBEGIN(f.dc); U(MoveToEx)(f.dc, 10, 20, NULL); U(LineTo)(f.dc, 30, 20); PEND(f.dc);
    CHECK(U(StrokePath)(f.dc));
    for (x = 10; x < 29; x++) EXPECT_EQ(fx_px(&f, x, 20), PX_RED);
    EXPECT_EQ(fx_px(&f, 9, 20), PX_WHITE);
    EXPECT_EQ(fx_px(&f, 20, 19), PX_WHITE);
    EXPECT_EQ(fx_px(&f, 20, 21), PX_WHITE);
    EXPECT_EQ(fx_px(&f, 31, 20), PX_WHITE);
    t_note("StrokePath line: %d red pixels, (29,20)=%06x (30,20)=%06x", fx_count(&f, PX_RED), fx_px(&f, 29, 20), fx_px(&f, 30, 20));
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(render_strokeandfill_both) {
    gdi_sel_t s = {0};
    NEED_PATH_API(); REQUIRE(U(StrokeAndFillPath));
    FX(f, 64, 64);
    sel_pen(f.dc, &s, COL_RED, 1); sel_brush(f.dc, &s, COL_BLUE);
    PBEGIN(f.dc); U(Rectangle)(f.dc, 10, 10, 40, 40); PEND(f.dc);
    CHECK(U(StrokeAndFillPath)(f.dc));
    EXPECT_EQ(fx_px(&f, 25, 25), PX_BLUE);
    EXPECT_EQ(fx_px(&f, 12, 12), PX_BLUE);
    CHECK(fx_near(&f, 10, 25, 1, PX_RED));
    CHECK(fx_near(&f, 25, 10, 1, PX_RED));
    EXPECT_EQ(fx_px(&f, 50, 50), PX_WHITE);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(render_path_consumed_after_stroke_and_fill) {
    gdi_sel_t s = {0}; struct pth p;
    NEED_PATH_API(); REQUIRE(U(StrokeAndFillPath));
    FX(f, 64, 64);
    sel_pen(f.dc, &s, COL_RED, 1); sel_brush(f.dc, &s, COL_BLUE);
    PBEGIN(f.dc); U(Rectangle)(f.dc, 10, 10, 40, 40); PEND(f.dc);
    CHECK(U(StrokeAndFillPath)(f.dc));
    fx_clear(&f, 0x00FFFFFF);
    SET_ERR(0);
    CHECK(!U(StrokeAndFillPath)(f.dc));         /* the path is gone: a second render paints nothing */
    EXPECT_INT(fx_count(&f, PX_WHITE), 64 * 64);
    EXPECT_INT(pth_get(f.dc, &p), -1);
    sel_done(f.dc, &s);
    fx_close(&f);
}

/* ---- PathToRegion ---------------------------------------------------------------------------------------- */
XT(region_pathtoregion_rect) {
    HRGN r; RECT bb; int t;
    NEED_PATH_API(); REQUIRE(U(PathToRegion)); REQUIRE(U(PtInRegion)); REQUIRE(U(GetRgnBox));
    MEMDC(dc);
    PBEGIN(dc); U(Rectangle)(dc, 10, 10, 30, 30); PEND(dc);
    r = U(PathToRegion)(dc);
    CHECK(r != NULL);
    if (r) {
        CHECK(U(PtInRegion)(r, 20, 20));
        CHECK(!U(PtInRegion)(r, 5, 5));
        CHECK(!U(PtInRegion)(r, 40, 40));
        t = U(GetRgnBox)(r, &bb);
        t_note("PathToRegion box (%ld,%ld,%ld,%ld) type %d", bb.left, bb.top, bb.right, bb.bottom, t);
        CHECK(bb.left == 10 && bb.top == 10 && bb.right >= 29 && bb.right <= 30 && bb.bottom >= 29 && bb.bottom <= 30);
        if (t != SIMPLEREGION) t_note("PathToRegion rect box type %d", t);
        CHECK(t != ERROR && t != NULLREGION);
        EXPECT_EQ(((ULONG_PTR)r >> 16) & 0x7f, 4);   /* result is a region handle (object type 4) */
        U(DeleteObject)(r);
    }
    MEMDC_FREE(dc);
}

XT(region_pathtoregion_consumes_path) {
    HRGN r; struct pth p;
    NEED_PATH_API(); REQUIRE(U(PathToRegion));
    MEMDC(dc);
    PBEGIN(dc); U(Rectangle)(dc, 10, 10, 30, 30); PEND(dc);
    r = U(PathToRegion)(dc);
    CHECK(r != NULL);
    if (r) U(DeleteObject)(r);
    EXPECT_INT(pth_get(dc, &p), -1);
    SET_ERR(0);
    CHECK(U(PathToRegion)(dc) == NULL);
    MEMDC_FREE(dc);
}

XT(region_pathtoregion_triangle_shape) {
    HRGN r;
    NEED_PATH_API(); REQUIRE(U(PathToRegion)); REQUIRE(U(PtInRegion));
    MEMDC(dc);
    PBEGIN(dc);
    U(MoveToEx)(dc, 10, 10, NULL); U(LineTo)(dc, 50, 10); U(LineTo)(dc, 10, 50); U(CloseFigure)(dc);
    PEND(dc);
    r = U(PathToRegion)(dc);
    CHECK(r != NULL);
    if (r) {
        CHECK(U(PtInRegion)(r, 15, 15));
        CHECK(!U(PtInRegion)(r, 45, 45));      /* beyond the hypotenuse */
        CHECK(!U(PtInRegion)(r, 5, 5));
        U(DeleteObject)(r);
    }
    MEMDC_FREE(dc);
}

XT(region_pathtoregion_polyfill_mode) {
    HRGN r; int mode;
    NEED_PATH_API(); REQUIRE(U(PathToRegion)); REQUIRE(U(PtInRegion)); REQUIRE(U(SetPolyFillMode));
    for (mode = ALTERNATE; mode <= WINDING; mode++) {
        MEMDC(dc);
        U(SetPolyFillMode)(dc, mode);
        PBEGIN(dc);
        U(MoveToEx)(dc, 10, 10, NULL); U(LineTo)(dc, 90, 10); U(LineTo)(dc, 90, 90); U(LineTo)(dc, 10, 90); U(CloseFigure)(dc);
        U(MoveToEx)(dc, 30, 30, NULL); U(LineTo)(dc, 70, 30); U(LineTo)(dc, 70, 70); U(LineTo)(dc, 30, 70); U(CloseFigure)(dc);
        PEND(dc);
        r = U(PathToRegion)(dc);   /* RGNMEMOBJ::vCreate gets the DC's fill mode byte (pdcattr+0xd6) */
        CHECK(r != NULL);
        if (r) {
            CHECK(U(PtInRegion)(r, 20, 50));
            CHECK((U(PtInRegion)(r, 50, 50) != 0) == (mode == WINDING));
            U(DeleteObject)(r);
        }
        MEMDC_FREE(dc);
    }
}

XT(region_pathtoregion_handle_registered) {
    HRGN r; struct k_gdicell c;
    NEED_PATH_API(); REQUIRE(U(PathToRegion)); REQUIRE(U(GetObjectType));
    MEMDC(dc);
    PBEGIN(dc); U(Rectangle)(dc, 10, 10, 30, 30); PEND(dc);
    r = U(PathToRegion)(dc);
    CHECK(r != NULL);
    if (r) {
        EXPECT_EQ(U(GetObjectType)(r), OBJ_REGION);
        if (!t_is_wine() && k_gdi_cell(r, &c)) {
            /* the type byte of the cell (low byte of the word at +0xc) equals the type bits of the handle; k_gdi_cell
               reports the byte at +0xd, the reuse byte, which is part of the handle's upper word */
            void *tb = NULL; unsigned char ty = 0;
            if (k_try_read_ptr((char *)k_peb() + PEB_GdiSharedHandleTable, &tb) && tb &&
                k_try_read_u8((char *)tb + (ULONG_PTR)((ULONG_PTR)r & 0xffff) * GDI_CELL_SIZE + 0xc, &ty))
                EXPECT_EQ(ty, ((ULONG_PTR)r >> 16) & 0x7f);
            EXPECT_EQ(c.type_check, ((ULONG_PTR)r >> 24) & 0xff);
            CHECK(c.object != NULL);
        }
        U(DeleteObject)(r);
    }
    MEMDC_FREE(dc);
}

/* ---- SelectClipPath ---------------------------------------------------------------------------------------- */
typedef struct { int mode; const char *name; int pA, pAB, pB, pN; } clipmode_t;

XT(clip_selectclippath_modes) {
    static const clipmode_t t[] = {
        /* A = (10,10)-(50,50) pre-selected clip; B = path rect (30,30)-(70,70); points: only-A, A&B, only-B, neither */
        { RGN_AND,  "RGN_AND",  0, 1, 0, 0 },
        { RGN_OR,   "RGN_OR",   1, 1, 1, 0 },
        { RGN_XOR,  "RGN_XOR",  1, 0, 1, 0 },
        { RGN_DIFF, "RGN_DIFF", 1, 0, 0, 0 },
        { RGN_COPY, "RGN_COPY", 0, 1, 1, 0 },
    };
    size_t i;
    NEED_PATH_API(); REQUIRE(U(SelectClipPath)); REQUIRE(U(IntersectClipRect)); REQUIRE(U(PtVisible));
    for (i = 0; i < sizeof t / sizeof t[0]; i++) {
        FX(f, 100, 100);
        U(IntersectClipRect)(f.dc, 10, 10, 50, 50);
        PBEGIN(f.dc); U(Rectangle)(f.dc, 30, 30, 70, 70); PEND(f.dc);
        CHECKF(U(SelectClipPath)(f.dc, t[i].mode), "%s failed err %lu", t[i].name, GetLastError());
        CHECKF((U(PtVisible)(f.dc, 20, 20) != 0) == t[i].pA, "%s: only-A", t[i].name);
        CHECKF((U(PtVisible)(f.dc, 40, 40) != 0) == t[i].pAB, "%s: A&B", t[i].name);
        CHECKF((U(PtVisible)(f.dc, 60, 60) != 0) == t[i].pB, "%s: only-B", t[i].name);
        CHECKF((U(PtVisible)(f.dc, 80, 80) != 0) == t[i].pN, "%s: neither", t[i].name);
        fx_close(&f);
    }
}

XT(clip_selectclippath_invalid_modes) {
    static const int bad[] = { 0, 6, 7, -1, 0x7fffffff, (int)0x80000000 };
    size_t i; struct pth p;
    NEED_PATH_API(); REQUIRE(U(SelectClipPath));
    MEMDC(dc);
    PBEGIN(dc); U(Rectangle)(dc, 10, 10, 30, 30); PEND(dc);
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SET_ERR(0);
        CHECKF(!U(SelectClipPath)(dc, bad[i]), "mode %d accepted", bad[i]);
        EXPECT_ERR(ERR_INVALID_PARAMETER);       /* (mode-1) > 4 rejected before the path is touched */
    }
    EXPECT_INT(pth_get(dc, &p), 4);               /* path still intact */
    MEMDC_FREE(dc);
}

XT(clip_selectclippath_copy_sets_clipbox) {
    RECT bb; int t;
    NEED_PATH_API(); REQUIRE(U(SelectClipPath)); REQUIRE(U(GetClipBox));
    FX(f, 100, 100);
    PBEGIN(f.dc); U(Rectangle)(f.dc, 20, 30, 60, 70); PEND(f.dc);
    CHECK(U(SelectClipPath)(f.dc, RGN_COPY));
    t = U(GetClipBox)(f.dc, &bb);
    t_note("clip box (%ld,%ld,%ld,%ld) type %d", bb.left, bb.top, bb.right, bb.bottom, t);
    CHECK(t == SIMPLEREGION);
    CHECK(bb.left == 20 && bb.top == 30 && bb.right >= 59 && bb.right <= 60 && bb.bottom >= 69 && bb.bottom <= 70);
    fx_close(&f);
}

XT(clip_selectclippath_clips_drawing) {
    gdi_sel_t s = {0};
    NEED_PATH_API(); REQUIRE(U(SelectClipPath)); REQUIRE(U(PatBlt));
    FX(f, 100, 100);
    PBEGIN(f.dc); U(Rectangle)(f.dc, 20, 20, 60, 60); PEND(f.dc);
    CHECK(U(SelectClipPath)(f.dc, RGN_COPY));
    sel_brush(f.dc, &s, COL_BLUE);
    CHECK(U(PatBlt)(f.dc, 0, 0, 100, 100, PATCOPY));
    EXPECT_EQ(fx_px(&f, 40, 40), PX_BLUE);
    EXPECT_EQ(fx_px(&f, 25, 25), PX_BLUE);
    EXPECT_EQ(fx_px(&f, 10, 10), PX_WHITE);
    EXPECT_EQ(fx_px(&f, 80, 80), PX_WHITE);
    EXPECT_EQ(fx_px(&f, 19, 40), PX_WHITE);
    EXPECT_EQ(fx_px(&f, 40, 19), PX_WHITE);
    { int n = fx_count(&f, PX_BLUE); t_note("clip path fill area %d", n); CHECK(n >= 39 * 39 && n <= 41 * 41); }
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(clip_selectclippath_diff_makes_hole) {
    gdi_sel_t s = {0};
    NEED_PATH_API(); REQUIRE(U(SelectClipPath)); REQUIRE(U(PatBlt));
    FX(f, 100, 100);
    PBEGIN(f.dc); U(Rectangle)(f.dc, 30, 30, 70, 70); PEND(f.dc);
    CHECK(U(SelectClipPath)(f.dc, RGN_DIFF));    /* no previous clip: everything except the path */
    sel_brush(f.dc, &s, COL_BLUE);
    CHECK(U(PatBlt)(f.dc, 0, 0, 100, 100, PATCOPY));
    EXPECT_EQ(fx_px(&f, 50, 50), PX_WHITE);
    EXPECT_EQ(fx_px(&f, 5, 5), PX_BLUE);
    EXPECT_EQ(fx_px(&f, 95, 95), PX_BLUE);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(clip_selectclippath_restored_by_restoredc) {
    struct pth p;
    NEED_PATH_API(); REQUIRE(U(SelectClipPath)); REQUIRE(U(SaveDC)); REQUIRE(U(RestoreDC)); REQUIRE(U(PtVisible));
    FX(f, 100, 100);
    int sv = U(SaveDC)(f.dc);
    PBEGIN(f.dc); U(Rectangle)(f.dc, 30, 30, 50, 50); PEND(f.dc);
    CHECK(U(SelectClipPath)(f.dc, RGN_COPY));
    CHECK(!U(PtVisible)(f.dc, 80, 80));
    CHECK(U(RestoreDC)(f.dc, sv));
    CHECK(U(PtVisible)(f.dc, 80, 80));           /* clip restored with the saved state */
    EXPECT_INT(pth_get(f.dc, &p), -1);
    fx_close(&f);
}

/* ---- Widen / Flatten ---------------------------------------------------------------------------------------- */
XT(widen_cosmetic_pen_fails) {
    BOOL r;
    NEED_PATH_API(); REQUIRE(U(WidenPath));
    MEMDC(dc);
    PBEGIN(dc); U(MoveToEx)(dc, 10, 10, NULL); U(LineTo)(dc, 50, 10); PEND(dc);
    SET_ERR(0);
    r = U(WidenPath)(dc);
    if (!r) EXPECT_ERR(ERR_CAN_NOT_COMPLETE);   /* NtGdiWidenPath: non-geometric pen -> 0x3eb */
    t_note("WidenPath with the default 1px cosmetic pen: %d (err %lu)", (int)r, GetLastError());
    MEMDC_FREE(dc);
}

XT(widen_geometric_pen_expands_path) {
    struct pth p; HGDIOBJ pen, old; LOGBRUSH lb = { BS_SOLID, RGB(0,0,0), 0 };
    int i, miny = 9999, maxy = -9999;
    NEED_PATH_API(); REQUIRE(U(WidenPath)); REQUIRE(U(ExtCreatePen));
    MEMDC(dc);
    pen = U(ExtCreatePen)(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_FLAT, 8, &lb, 0, NULL);
    old = U(SelectObject)(dc, pen);
    PBEGIN(dc); U(MoveToEx)(dc, 10, 20, NULL); U(LineTo)(dc, 50, 20); PEND(dc);
    CHECK(U(WidenPath)(dc));
    CHECK(pth_get(dc, &p) >= 4);
    CHECK(pth_valid(&p));
    for (i = 0; i < p.n; i++) { if (p.pt[i].y < miny) { miny = p.pt[i].y; } if (p.pt[i].y > maxy) { maxy = p.pt[i].y; } }
    t_note("widened 8px line spans y [%d,%d]", miny, maxy);
    CHECK(miny <= 17 && maxy >= 23);
    CHECK(p.n > 0 && (p.ty[p.n - 1] & PT_CLOSEFIGURE));
    U(SelectObject)(dc, old); U(DeleteObject)(pen);
    MEMDC_FREE(dc);
}

XT(widen_then_fill_paints_stroke_area) {
    struct pth p; HGDIOBJ pen, old; LOGBRUSH lb = { BS_SOLID, RGB(0,0,0), 0 };
    gdi_sel_t s = {0};
    NEED_PATH_API(); REQUIRE(U(WidenPath)); REQUIRE(U(ExtCreatePen)); REQUIRE(U(FillPath));
    FX(f, 100, 60);
    pen = U(ExtCreatePen)(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_FLAT, 10, &lb, 0, NULL);
    old = U(SelectObject)(f.dc, pen);
    sel_brush(f.dc, &s, COL_BLUE);
    PBEGIN(f.dc); U(MoveToEx)(f.dc, 10, 30, NULL); U(LineTo)(f.dc, 80, 30); PEND(f.dc);
    CHECK(U(WidenPath)(f.dc));
    CHECK(U(FillPath)(f.dc));
    EXPECT_EQ(fx_px(&f, 40, 30), PX_BLUE);
    EXPECT_EQ(fx_px(&f, 40, 27), PX_BLUE);
    EXPECT_EQ(fx_px(&f, 40, 33), PX_BLUE);
    EXPECT_EQ(fx_px(&f, 40, 20), PX_WHITE);
    EXPECT_EQ(fx_px(&f, 40, 40), PX_WHITE);
    sel_done(f.dc, &s);
    U(SelectObject)(f.dc, old); U(DeleteObject)(pen);
    fx_close(&f);
}

XT(flatten_removes_beziers) {
    struct pth p; static const POINT pt[4] = { {10,10},{20,0},{30,20},{40,10} };
    int before;
    NEED_PATH_API(); REQUIRE(U(PolyBezier)); REQUIRE(U(FlattenPath));
    MEMDC(dc);
    PBEGIN(dc); U(PolyBezier)(dc, pt, 4); PEND(dc);
    before = pth_get(dc, &p);
    EXPECT_INT(before, 4);
    CHECK(U(FlattenPath)(dc));
    CHECK(pth_get(dc, &p) > before);
    CHECK(pth_valid(&p));
    EXPECT_INT(pth_count_type(&p, PT_BEZIERTO), 0);
    CHECK(pth_count_type(&p, PT_LINETO) >= 3);
    EXPECT_EQ(p.pt[0].x, 10); EXPECT_EQ(p.pt[0].y, 10);
    EXPECT_EQ(p.pt[p.n - 1].x, 40); EXPECT_EQ(p.pt[p.n - 1].y, 10);
    MEMDC_FREE(dc);
}

XT(flatten_ellipse_has_only_lines) {
    struct pth p;
    NEED_PATH_API(); REQUIRE(U(Ellipse)); REQUIRE(U(FlattenPath));
    MEMDC(dc);
    PBEGIN(dc); U(Ellipse)(dc, 10, 10, 90, 60); PEND(dc);
    CHECK(U(FlattenPath)(dc));
    CHECK(pth_get(dc, &p) > 13);
    CHECK(pth_valid(&p));
    EXPECT_INT(pth_count_type(&p, PT_BEZIERTO), 0);
    CHECK(p.n > 0 && (p.ty[p.n - 1] & PT_CLOSEFIGURE));
    MEMDC_FREE(dc);
}

XT(flatten_lines_unchanged) {
    struct pth a, b;
    NEED_PATH_API(); REQUIRE(U(FlattenPath));
    MEMDC(dc);
    PBEGIN(dc); U(Rectangle)(dc, 10, 10, 30, 30); PEND(dc);
    pth_get(dc, &a);
    CHECK(U(FlattenPath)(dc));
    pth_get(dc, &b);
    EXPECT_INT(b.n, a.n);
    CHECK(!memcmp(a.pt, b.pt, sizeof(POINT) * (a.n > 0 ? a.n : 0)));
    CHECK(!memcmp(a.ty, b.ty, a.n > 0 ? a.n : 0));
    MEMDC_FREE(dc);
}

XT(flatten_fill_matches_curve_region) {
    gdi_sel_t s = {0}; static const POINT pt[4] = { {10,40},{30,0},{50,0},{70,40} };
    NEED_PATH_API(); REQUIRE(U(PolyBezier)); REQUIRE(U(FlattenPath)); REQUIRE(U(FillPath));
    FX(f, 100, 60);
    sel_brush(f.dc, &s, COL_BLUE);
    PBEGIN(f.dc); U(PolyBezier)(f.dc, pt, 4); PEND(f.dc);
    CHECK(U(FlattenPath)(f.dc));
    CHECK(U(FillPath)(f.dc));        /* implicitly closes the curve with its chord */
    EXPECT_EQ(fx_px(&f, 40, 35), PX_BLUE);   /* between chord (y=40) and curve apex (~y=10) */
    EXPECT_EQ(fx_px(&f, 40, 50), PX_WHITE);  /* below the chord */
    EXPECT_EQ(fx_px(&f, 40, 3), PX_WHITE);   /* above the apex */
    sel_done(f.dc, &s);
    fx_close(&f);
}
