/* Pixel-exact line / rectangle drawing on a 32bpp DIB section, and the current-position bookkeeping.
 *
 * The DIB section is shared memory the kernel draws into; after GdiFlush the test reads the pixels straight out of the
 * mapped section. Exactness is only asserted for axis-aligned geometry and 45 degree lines: the kernel's line engine
 * excludes the final endpoint (NtGdiLineTo -> EngLineTo semantics) and a one pixel solid pen on Rectangle paints the
 * rectangle outline inside [left,right) x [top,bottom).
 */
#include "xd.h"

#define NEED_DRAW() do { REQUIRE(U(MoveToEx)); REQUIRE(U(LineTo)); REQUIRE(U(GetCurrentPositionEx)); REQUIRE(U(Rectangle)); \
    REQUIRE(U(CreateCompatibleDC)); REQUIRE(U(CreateDIBSection)); REQUIRE(U(GdiFlush)); } while (0)
#define EXPECT_PT(p, X, Y) do { EXPECT_INT((p).x, X); EXPECT_INT((p).y, Y); } while (0)

/* ---- DIB section state -------------------------------------------------------------------------------------- */
XT(dib_object_describes_mapped_bits) {
    DIBSECTION ds; int n;
    NEED_DRAW(); REQUIRE(U(GetObjectW));
    FX(f, 40, 30);
    memset(&ds, 0, sizeof ds);
    n = U(GetObjectW)(f.bmp, sizeof ds, &ds);
    EXPECT_INT(n, sizeof ds);
    EXPECT_EQ(ds.dsBm.bmBits, f.bits);                   /* the pointer CreateDIBSection returned is the kernel-mapped section */
    EXPECT_INT(ds.dsBm.bmWidth, 40); EXPECT_INT(ds.dsBm.bmHeight, 30);
    EXPECT_INT(ds.dsBm.bmBitsPixel, 32); EXPECT_INT(ds.dsBm.bmWidthBytes, 160);
    EXPECT_INT(ds.dsBmih.biCompression, BI_RGB);
    fx_close(&f);
}

XT(dib_direct_writes_visible_to_getpixel) {
    NEED_DRAW(); REQUIRE(U(GetPixel));
    FX(f, 32, 32);
    f.bits[5 * 32 + 7] = 0x00123456;
    f.bits[0] = 0x00FF0000;
    f.bits[31 * 32 + 31] = 0x0000FF00;
    EXPECT_EQ(U(GetPixel)(f.dc, 7, 5), RGB(0x12, 0x34, 0x56));
    EXPECT_EQ(U(GetPixel)(f.dc, 0, 0), RGB(0xFF, 0, 0));
    EXPECT_EQ(U(GetPixel)(f.dc, 31, 31), RGB(0, 0xFF, 0));
    EXPECT_EQ(U(GetPixel)(f.dc, 32, 0), CLR_INVALID);
    EXPECT_EQ(U(GetPixel)(f.dc, -1, 0), CLR_INVALID);
    fx_close(&f);
}

XT(dib_setpixel_lands_in_section) {
    NEED_DRAW(); REQUIRE(U(SetPixel));
    FX(f, 32, 32);
    EXPECT_EQ(U(SetPixel)(f.dc, 3, 4, RGB(1, 2, 3)), RGB(1, 2, 3));
    EXPECT_EQ(fx_px(&f, 3, 4), 0x010203);
    EXPECT_EQ(fx_px(&f, 4, 4), PX_WHITE);
    EXPECT_EQ(U(SetPixel)(f.dc, 99, 99, RGB(1, 2, 3)), (COLORREF)-1);   /* outside the bitmap */
    fx_close(&f);
}

XT(dib_getpixel_matches_bits_after_line) {
    gdi_sel_t s = {0}; int x;
    NEED_DRAW(); REQUIRE(U(GetPixel));
    FX(f, 48, 48);
    sel_pen(f.dc, &s, RGB(10, 200, 90), 1);
    U(MoveToEx)(f.dc, 5, 9, NULL); U(LineTo)(f.dc, 30, 9);
    for (x = 3; x < 34; x++) {
        COLORREF c = U(GetPixel)(f.dc, x, 9);
        EXPECT_EQ(col2px(c), fx_px(&f, x, 9));
    }
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(dib_two_dcs_see_same_section) {
    HDC dc2; HGDIOBJ old2; gdi_sel_t s = {0};
    NEED_DRAW();
    FX(f, 32, 32);
    dc2 = U(CreateCompatibleDC)(NULL);
    if (!dc2) { t_skip("no second DC"); fx_close(&f); return; }
    /* a bitmap can be selected into one DC at a time: release it, draw through the second DC, read the shared bits */
    U(SelectObject)(f.dc, f.old); f.old = NULL;
    old2 = U(SelectObject)(dc2, f.bmp);
    sel_pen(dc2, &s, COL_RED, 1);
    U(MoveToEx)(dc2, 2, 2, NULL); U(LineTo)(dc2, 12, 2);
    EXPECT_INT(fx_count(&f, PX_RED), 10);
    sel_done(dc2, &s);
    U(SelectObject)(dc2, old2);
    U(DeleteDC)(dc2);
    fx_close(&f);
}

/* ---- MoveToEx / LineTo / current position ------------------------------------------------------------------- */
XT(line_current_position_initial) {
    POINT p = { 0x55, 0x55 };
    NEED_DRAW();
    FX(f, 32, 32);
    CHECK(U(GetCurrentPositionEx)(f.dc, &p));
    EXPECT_PT(p, 0, 0);
    fx_close(&f);
}

XT(line_moveto_returns_previous) {
    POINT prev = { 0x55, 0x55 }, cur;
    NEED_DRAW();
    FX(f, 32, 32);
    CHECK(U(MoveToEx)(f.dc, 7, 9, &prev)); EXPECT_PT(prev, 0, 0);
    CHECK(U(MoveToEx)(f.dc, 11, 13, &prev)); EXPECT_PT(prev, 7, 9);
    CHECK(U(GetCurrentPositionEx)(f.dc, &cur)); EXPECT_PT(cur, 11, 13);
    CHECK(U(MoveToEx)(f.dc, -5, 100000, NULL));
    CHECK(U(GetCurrentPositionEx)(f.dc, &cur)); EXPECT_PT(cur, -5, 100000);   /* not clipped to the surface */
    fx_close(&f);
}

XT(line_lineto_updates_position) {
    gdi_sel_t s = {0}; POINT cur;
    NEED_DRAW();
    FX(f, 32, 32);
    sel_pen(f.dc, &s, COL_RED, 1);
    U(MoveToEx)(f.dc, 2, 2, NULL);
    CHECK(U(LineTo)(f.dc, 20, 5));
    CHECK(U(GetCurrentPositionEx)(f.dc, &cur)); EXPECT_PT(cur, 20, 5);
    CHECK(U(LineTo)(f.dc, 20, 25));
    CHECK(U(GetCurrentPositionEx)(f.dc, &cur)); EXPECT_PT(cur, 20, 25);
    CHECK(U(LineTo)(f.dc, 1000, -1000));                            /* far outside: still succeeds and moves */
    CHECK(U(GetCurrentPositionEx)(f.dc, &cur)); EXPECT_PT(cur, 1000, -1000);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(line_position_follows_mapping) {
    POINT cur;
    NEED_DRAW(); REQUIRE(U(SetViewportOrgEx)); REQUIRE(U(SetWindowOrgEx));
    FX(f, 32, 32);
    U(SetViewportOrgEx)(f.dc, 10, 10, NULL);
    U(MoveToEx)(f.dc, 3, 4, NULL);
    CHECK(U(GetCurrentPositionEx)(f.dc, &cur)); EXPECT_PT(cur, 3, 4);     /* reported in logical coordinates */
    U(SetWindowOrgEx)(f.dc, 1, 1, NULL);
    CHECK(U(GetCurrentPositionEx)(f.dc, &cur));
    t_note("current position after changing the window origin: (%ld,%ld)", cur.x, cur.y);
    fx_close(&f);
}

XT(line_horizontal_left_to_right) {
    gdi_sel_t s = {0}; int x;
    NEED_DRAW();
    FX(f, 40, 40);
    sel_pen(f.dc, &s, COL_RED, 1);
    U(MoveToEx)(f.dc, 10, 10, NULL); U(LineTo)(f.dc, 20, 10);
    for (x = 10; x < 20; x++) EXPECT_EQ(fx_px(&f, x, 10), PX_RED);
    EXPECT_EQ(fx_px(&f, 20, 10), PX_WHITE);          /* the endpoint is excluded */
    EXPECT_EQ(fx_px(&f, 9, 10), PX_WHITE);
    EXPECT_INT(fx_count(&f, PX_RED), 10);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(line_horizontal_right_to_left) {
    gdi_sel_t s = {0}; int x;
    NEED_DRAW();
    FX(f, 40, 40);
    sel_pen(f.dc, &s, COL_RED, 1);
    U(MoveToEx)(f.dc, 20, 10, NULL); U(LineTo)(f.dc, 10, 10);
    for (x = 11; x <= 20; x++) EXPECT_EQ(fx_px(&f, x, 10), PX_RED);
    EXPECT_EQ(fx_px(&f, 10, 10), PX_WHITE);          /* the start is drawn, the end (10) is not */
    EXPECT_INT(fx_count(&f, PX_RED), 10);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(line_vertical_both_directions) {
    gdi_sel_t s = {0}; int y;
    NEED_DRAW();
    FX(f, 40, 40);
    sel_pen(f.dc, &s, COL_RED, 1);
    U(MoveToEx)(f.dc, 7, 5, NULL); U(LineTo)(f.dc, 7, 15);
    for (y = 5; y < 15; y++) EXPECT_EQ(fx_px(&f, 7, y), PX_RED);
    EXPECT_EQ(fx_px(&f, 7, 15), PX_WHITE);
    fx_clear(&f, 0x00FFFFFF);
    U(MoveToEx)(f.dc, 7, 15, NULL); U(LineTo)(f.dc, 7, 5);
    for (y = 6; y <= 15; y++) EXPECT_EQ(fx_px(&f, 7, y), PX_RED);
    EXPECT_EQ(fx_px(&f, 7, 5), PX_WHITE);
    EXPECT_INT(fx_count(&f, PX_RED), 10);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(line_diagonal_45_degrees) {
    gdi_sel_t s = {0}; int i; static const struct { int x0, y0, x1, y1, dx, dy; } t[] = {
        { 10, 10, 20, 20, 1, 1 }, { 20, 20, 10, 10, -1, -1 }, { 10, 20, 20, 10, 1, -1 }, { 20, 10, 10, 20, -1, 1 },
    };
    size_t k;
    NEED_DRAW();
    FX(f, 40, 40);
    sel_pen(f.dc, &s, COL_RED, 1);
    for (k = 0; k < sizeof t / sizeof t[0]; k++) {
        fx_clear(&f, 0x00FFFFFF);
        U(MoveToEx)(f.dc, t[k].x0, t[k].y0, NULL); U(LineTo)(f.dc, t[k].x1, t[k].y1);
        for (i = 0; i < 10; i++) EXPECT_EQ(fx_px(&f, t[k].x0 + i * t[k].dx, t[k].y0 + i * t[k].dy), PX_RED);
        EXPECT_EQ(fx_px(&f, t[k].x1, t[k].y1), PX_WHITE);
        EXPECT_INT(fx_count(&f, PX_RED), 10);
    }
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(line_shallow_slope_pixel_count) {
    gdi_sel_t s = {0}; int l, t, r, b, n;
    NEED_DRAW();
    FX(f, 64, 40);
    sel_pen(f.dc, &s, COL_RED, 1);
    U(MoveToEx)(f.dc, 4, 4, NULL); U(LineTo)(f.dc, 44, 24);   /* 40 columns, 20 rows */
    n = fx_bbox(&f, PX_RED, &l, &t, &r, &b);
    EXPECT_INT(n, 40);                                         /* one pixel per major-axis step, end excluded */
    EXPECT_INT(l, 4); EXPECT_INT(t, 4); EXPECT_INT(r, 43); EXPECT_INT(b, 23);
    EXPECT_EQ(fx_px(&f, 4, 4), PX_RED);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(line_clipped_at_surface_edge) {
    gdi_sel_t s = {0}; int x;
    NEED_DRAW();
    FX(f, 32, 32);
    sel_pen(f.dc, &s, COL_RED, 1);
    U(MoveToEx)(f.dc, -10, 5, NULL); U(LineTo)(f.dc, 10, 5);
    for (x = 0; x < 10; x++) EXPECT_EQ(fx_px(&f, x, 5), PX_RED);
    EXPECT_EQ(fx_px(&f, 10, 5), PX_WHITE);
    EXPECT_INT(fx_count(&f, PX_RED), 10);
    fx_clear(&f, 0x00FFFFFF);
    U(MoveToEx)(f.dc, 25, 7, NULL); U(LineTo)(f.dc, 100, 7);
    for (x = 25; x < 32; x++) EXPECT_EQ(fx_px(&f, x, 7), PX_RED);
    EXPECT_INT(fx_count(&f, PX_RED), 7);
    fx_clear(&f, 0x00FFFFFF);
    U(MoveToEx)(f.dc, 3, -50, NULL); U(LineTo)(f.dc, 3, 500);       /* vertical across the whole surface */
    EXPECT_INT(fx_count(&f, PX_RED), 32);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(line_completely_outside_draws_nothing) {
    gdi_sel_t s = {0};
    NEED_DRAW();
    FX(f, 32, 32);
    sel_pen(f.dc, &s, COL_RED, 1);
    U(MoveToEx)(f.dc, 100, 100, NULL); U(LineTo)(f.dc, 200, 200);
    U(MoveToEx)(f.dc, -100, 5, NULL); U(LineTo)(f.dc, -10, 5);
    U(MoveToEx)(f.dc, 5, 40, NULL); U(LineTo)(f.dc, 25, 40);
    EXPECT_INT(fx_count(&f, PX_WHITE), 32 * 32);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(line_pen_width_three_horizontal) {
    gdi_sel_t s = {0}; int y, n = 0;
    NEED_DRAW();
    FX(f, 64, 40);
    sel_pen(f.dc, &s, COL_RED, 3);
    U(MoveToEx)(f.dc, 10, 20, NULL); U(LineTo)(f.dc, 50, 20);
    for (y = 0; y < 40; y++) if (fx_px(&f, 30, y) == PX_RED) n++;
    EXPECT_INT(n, 3);                                       /* column in the middle of a 3px pen */
    EXPECT_EQ(fx_px(&f, 30, 19), PX_RED); EXPECT_EQ(fx_px(&f, 30, 20), PX_RED); EXPECT_EQ(fx_px(&f, 30, 21), PX_RED);
    EXPECT_EQ(fx_px(&f, 30, 18), PX_WHITE); EXPECT_EQ(fx_px(&f, 30, 22), PX_WHITE);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(line_null_pen_draws_nothing_but_moves) {
    gdi_sel_t s = {0}; POINT cur;
    NEED_DRAW();
    FX(f, 32, 32);
    sel_nullpen(f.dc, &s);
    U(MoveToEx)(f.dc, 2, 2, NULL);
    CHECK(U(LineTo)(f.dc, 20, 20));
    EXPECT_INT(fx_count(&f, PX_WHITE), 32 * 32);
    U(GetCurrentPositionEx)(f.dc, &cur); EXPECT_PT(cur, 20, 20);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(line_dashed_pen_has_gaps) {
    gdi_sel_t s = {0}; int n, full;
    NEED_DRAW();
    FX(f, 128, 16);
    s.pen = U(CreatePen)(PS_DASH, 1, COL_RED);
    s.oldpen = U(SelectObject)(f.dc, s.pen); s.oldbrush = NULL; s.brush = NULL;
    U(MoveToEx)(f.dc, 2, 8, NULL); U(LineTo)(f.dc, 102, 8);
    n = fx_count(&f, PX_RED); full = 100;
    t_note("PS_DASH line of length 100 painted %d pixels", n);
    CHECK(n > 20 && n < full);
    EXPECT_EQ(fx_px(&f, 2, 8), PX_RED);                    /* the pattern starts with a dash */
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(line_exact_pen_colors) {
    static const COLORREF cols[] = { RGB(0,0,0), RGB(255,255,255), RGB(255,0,0), RGB(0,255,0), RGB(0,0,255), RGB(18,52,86), RGB(250,1,128) };
    size_t i; gdi_sel_t s = {0};
    NEED_DRAW();
    FX(f, 32, 8);
    for (i = 0; i < sizeof cols / sizeof cols[0]; i++) {
        fx_clear(&f, 0x00808080);
        sel_pen(f.dc, &s, cols[i], 1);
        U(MoveToEx)(f.dc, 1, 3, NULL); U(LineTo)(f.dc, 11, 3);
        EXPECT_EQ(fx_px(&f, 5, 3), col2px(cols[i]));      /* solid pens are exact on a 32bpp surface (RGB -> BGRX) */
        EXPECT_EQ(fx_px(&f, 5, 2), 0x808080);
        sel_done(f.dc, &s);
    }
    fx_close(&f);
}

XT(line_zero_length) {
    gdi_sel_t s = {0};
    NEED_DRAW();
    FX(f, 32, 32);
    sel_pen(f.dc, &s, COL_RED, 1);
    U(MoveToEx)(f.dc, 10, 10, NULL);
    CHECK(U(LineTo)(f.dc, 10, 10));
    t_note("zero-length LineTo painted %d pixels", fx_count(&f, PX_RED));
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(line_invalid_hdc) {
    NEED_DRAW();
    CHECK(!U(MoveToEx)(NULL, 1, 1, NULL));
    CHECK(!U(LineTo)(NULL, 1, 1));
    CHECK(!U(Rectangle)(NULL, 1, 1, 5, 5));
    CHECK(!U(MoveToEx)((HDC)(ULONG_PTR)0x01010fff, 1, 1, NULL));
    CHECK(!U(LineTo)((HDC)U(GetStockObject)(BLACK_PEN), 1, 1));
    CHECK(!U(Rectangle)((HDC)U(GetStockObject)(BLACK_BRUSH), 1, 1, 5, 5));
}

XT(line_survives_viewport_origin) {
    gdi_sel_t s = {0}; int x;
    NEED_DRAW(); REQUIRE(U(SetViewportOrgEx));
    FX(f, 64, 64);
    sel_pen(f.dc, &s, COL_RED, 1);
    U(SetViewportOrgEx)(f.dc, 20, 30, NULL);
    U(MoveToEx)(f.dc, 0, 0, NULL); U(LineTo)(f.dc, 10, 0);
    for (x = 20; x < 30; x++) EXPECT_EQ(fx_px(&f, x, 30), PX_RED);
    EXPECT_INT(fx_count(&f, PX_RED), 10);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(line_mapped_scale) {
    gdi_sel_t s = {0}; int x;
    NEED_DRAW(); REQUIRE(U(SetMapMode)); REQUIRE(U(SetWindowExtEx)); REQUIRE(U(SetViewportExtEx));
    FX(f, 64, 64);
    sel_pen(f.dc, &s, COL_RED, 1);
    U(SetMapMode)(f.dc, MM_ANISOTROPIC);
    U(SetWindowExtEx)(f.dc, 10, 10, NULL); U(SetViewportExtEx)(f.dc, 20, 20, NULL);   /* 2x */
    U(MoveToEx)(f.dc, 3, 4, NULL); U(LineTo)(f.dc, 13, 4);
    /* a width 1 pen is scaled with the mapping, so the line is a 2 pixel thick geometric line whose end pixel is drawn */
    { int l, t, r, b, n = fx_bbox(&f, PX_RED, &l, &t, &r, &b);
      t_note("scaled line: %d red px, bbox (%d,%d)-(%d,%d)", n, l, t, r, b);
      EXPECT_INT(l, 6); EXPECT_INT(r, 26); EXPECT_INT(t, 7); EXPECT_INT(b, 8);
      EXPECT_INT(n, 41); }
    for (x = 6; x <= 26; x++) EXPECT_EQ(fx_px(&f, x, 8), PX_RED);
    EXPECT_EQ(fx_px(&f, 27, 8), PX_WHITE);
    sel_done(f.dc, &s);
    fx_close(&f);
}

/* ---- Rectangle ---------------------------------------------------------------------------------------------- */
XT(rect_outline_exact) {
    gdi_sel_t s = {0}; int l = 10, t = 12, r = 30, b = 24, x, y;
    NEED_DRAW();
    FX(f, 48, 40);
    sel_pen(f.dc, &s, COL_RED, 1); sel_nullbrush(f.dc, &s);
    CHECK(U(Rectangle)(f.dc, l, t, r, b));
    for (x = l; x < r; x++) { EXPECT_EQ(fx_px(&f, x, t), PX_RED); EXPECT_EQ(fx_px(&f, x, b - 1), PX_RED); }
    for (y = t; y < b; y++) { EXPECT_EQ(fx_px(&f, l, y), PX_RED); EXPECT_EQ(fx_px(&f, r - 1, y), PX_RED); }
    EXPECT_EQ(fx_px(&f, r, t), PX_WHITE); EXPECT_EQ(fx_px(&f, l, b), PX_WHITE);
    EXPECT_EQ(fx_px(&f, l - 1, t), PX_WHITE); EXPECT_EQ(fx_px(&f, l, t - 1), PX_WHITE);
    EXPECT_EQ(fx_px(&f, (l + r) / 2, (t + b) / 2), PX_WHITE);   /* NULL brush leaves the interior alone */
    EXPECT_INT(fx_count(&f, PX_RED), 2 * (r - l) + 2 * (b - t) - 4);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(rect_filled_with_border) {
    gdi_sel_t s = {0}; int l = 10, t = 12, r = 30, b = 24;
    NEED_DRAW();
    FX(f, 48, 40);
    sel_pen(f.dc, &s, COL_RED, 1); sel_brush(f.dc, &s, COL_BLUE);
    CHECK(U(Rectangle)(f.dc, l, t, r, b));
    EXPECT_INT(fx_count(&f, PX_RED), 2 * (r - l) + 2 * (b - t) - 4);
    EXPECT_INT(fx_count(&f, PX_BLUE), (r - l - 2) * (b - t - 2));
    EXPECT_INT(fx_count(&f, PX_WHITE), 48 * 40 - (r - l) * (b - t));
    EXPECT_EQ(fx_px(&f, l, t), PX_RED); EXPECT_EQ(fx_px(&f, l + 1, t + 1), PX_BLUE);
    EXPECT_EQ(fx_px(&f, r - 1, b - 1), PX_RED); EXPECT_EQ(fx_px(&f, r - 2, b - 2), PX_BLUE);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(rect_same_pen_and_brush_is_solid_block) {
    gdi_sel_t s = {0};
    NEED_DRAW();
    FX(f, 48, 40);
    sel_pen(f.dc, &s, COL_BLUE, 1); sel_brush(f.dc, &s, COL_BLUE);
    CHECK(U(Rectangle)(f.dc, 5, 6, 25, 20));
    EXPECT_INT(fx_count(&f, PX_BLUE), 20 * 14);
    EXPECT_INT(fx_count_box(&f, 5, 6, 24, 19, PX_BLUE), 20 * 14);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(rect_null_pen_fill) {
    gdi_sel_t s = {0}; int l, t, r, b, n;
    NEED_DRAW();
    FX(f, 48, 40);
    sel_nullpen(f.dc, &s); sel_brush(f.dc, &s, COL_BLUE);
    CHECK(U(Rectangle)(f.dc, 10, 10, 30, 24));
    n = fx_bbox(&f, PX_BLUE, &l, &t, &r, &b);
    t_note("NULL pen Rectangle(10,10,30,24): %d px, bbox (%d,%d)-(%d,%d)", n, l, t, r, b);
    EXPECT_EQ(fx_px(&f, 20, 17), PX_BLUE);
    EXPECT_EQ(fx_px(&f, 10, 10), PX_BLUE);
    /* with a NULL pen the fill is one pixel narrower and shorter than the rectangle: 19x13 pixels */
    CHECK(l == 10 && t == 10 && r == 28 && b == 22);
    EXPECT_INT(n, 19 * 13);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(rect_one_pixel_pen_small_sizes) {
    gdi_sel_t s = {0};
    NEED_DRAW();
    FX(f, 32, 32);
    sel_pen(f.dc, &s, COL_RED, 1); sel_nullbrush(f.dc, &s);
    CHECK(U(Rectangle)(f.dc, 10, 10, 12, 12));
    t_note("Rectangle 2x2 painted %d px", fx_count(&f, PX_RED));
    EXPECT_INT(fx_count(&f, PX_RED), 4);
    fx_clear(&f, 0x00FFFFFF);
    CHECK(U(Rectangle)(f.dc, 10, 10, 13, 13));
    EXPECT_INT(fx_count(&f, PX_RED), 8);                   /* 3x3 ring */
    EXPECT_EQ(fx_px(&f, 11, 11), PX_WHITE);
    fx_clear(&f, 0x00FFFFFF);
    CHECK(U(Rectangle)(f.dc, 10, 10, 11, 11));
    t_note("Rectangle 1x1 painted %d px", fx_count(&f, PX_RED));
    fx_clear(&f, 0x00FFFFFF);
    CHECK(U(Rectangle)(f.dc, 10, 10, 10, 10));
    t_note("Rectangle empty painted %d px", fx_count(&f, PX_RED));
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(rect_partially_clipped) {
    gdi_sel_t s = {0};
    NEED_DRAW();
    FX(f, 32, 32);
    sel_pen(f.dc, &s, COL_RED, 1); sel_nullbrush(f.dc, &s);
    CHECK(U(Rectangle)(f.dc, -5, -5, 10, 10));
    EXPECT_EQ(fx_px(&f, 9, 5), PX_RED);                    /* right edge is inside */
    EXPECT_EQ(fx_px(&f, 5, 9), PX_RED);                    /* bottom edge */
    EXPECT_EQ(fx_px(&f, 9, 0), PX_RED);
    EXPECT_EQ(fx_px(&f, 0, 9), PX_RED);
    EXPECT_EQ(fx_px(&f, 0, 0), PX_WHITE);                  /* left/top edges were clipped away */
    EXPECT_EQ(fx_px(&f, 5, 5), PX_WHITE);
    EXPECT_INT(fx_count(&f, PX_RED), 19);                  /* 10 + 10 edge pixels sharing the (9,9) corner */
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(rect_huge_coordinates_clipped) {
    gdi_sel_t s = {0};
    NEED_DRAW();
    FX(f, 32, 32);
    sel_pen(f.dc, &s, COL_RED, 1); sel_brush(f.dc, &s, COL_BLUE);
    CHECK(U(Rectangle)(f.dc, -100000, -100000, 100000, 100000));
    EXPECT_EQ(fx_px(&f, 16, 16), PX_BLUE);                 /* the outline is far outside: all brush */
    EXPECT_INT(fx_count(&f, PX_BLUE), 32 * 32);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(rect_follows_viewport_and_window_origin) {
    gdi_sel_t s = {0};
    NEED_DRAW(); REQUIRE(U(SetViewportOrgEx)); REQUIRE(U(SetWindowOrgEx));
    FX(f, 64, 64);
    sel_pen(f.dc, &s, COL_RED, 1); sel_nullbrush(f.dc, &s);
    U(SetViewportOrgEx)(f.dc, 20, 30, NULL);
    CHECK(U(Rectangle)(f.dc, 0, 0, 10, 8));
    EXPECT_EQ(fx_px(&f, 20, 30), PX_RED); EXPECT_EQ(fx_px(&f, 29, 37), PX_RED);
    EXPECT_EQ(fx_px(&f, 0, 0), PX_WHITE);
    fx_clear(&f, 0x00FFFFFF);
    U(SetViewportOrgEx)(f.dc, 0, 0, NULL);
    U(SetWindowOrgEx)(f.dc, -15, -16, NULL);               /* logical (0,0) is device (15,16) */
    CHECK(U(Rectangle)(f.dc, 0, 0, 10, 8));
    EXPECT_EQ(fx_px(&f, 15, 16), PX_RED); EXPECT_EQ(fx_px(&f, 24, 23), PX_RED);
    EXPECT_INT(fx_count(&f, PX_RED), 2 * 10 + 2 * 8 - 4);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(rect_flipped_axes_by_negative_viewport_extent) {
    gdi_sel_t s = {0}; int l, t, r, b, n;
    NEED_DRAW(); REQUIRE(U(SetMapMode)); REQUIRE(U(SetViewportOrgEx)); REQUIRE(U(SetViewportExtEx)); REQUIRE(U(SetWindowExtEx));
    FX(f, 64, 64);
    sel_pen(f.dc, &s, COL_RED, 1); sel_nullbrush(f.dc, &s);
    U(SetMapMode)(f.dc, MM_ANISOTROPIC);
    U(SetWindowExtEx)(f.dc, 1, 1, NULL); U(SetViewportExtEx)(f.dc, 1, -1, NULL);
    U(SetViewportOrgEx)(f.dc, 0, 63, NULL);                /* y up with origin at the bottom row */
    CHECK(U(Rectangle)(f.dc, 10, 10, 30, 20));
    n = fx_bbox(&f, PX_RED, &l, &t, &r, &b);
    t_note("flipped rectangle: %d px bbox (%d,%d)-(%d,%d)", n, l, t, r, b);
    CHECK(n > 0);
    CHECK(l >= 9 && l <= 10 && r >= 29 && r <= 30);
    CHECK(t >= 43 && t <= 44 && b >= 52 && b <= 53);        /* y: 63-20 .. 63-10 */
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(rect_inverted_coordinates) {
    gdi_sel_t s = {0}; int n1, n2;
    NEED_DRAW();
    FX(f, 48, 48);
    sel_pen(f.dc, &s, COL_RED, 1); sel_nullbrush(f.dc, &s);
    CHECK(U(Rectangle)(f.dc, 10, 10, 30, 25));
    n1 = fx_count(&f, PX_RED);
    fx_clear(&f, 0x00FFFFFF);
    CHECK(U(Rectangle)(f.dc, 30, 25, 10, 10));
    n2 = fx_count(&f, PX_RED);
    t_note("Rectangle with swapped corners painted %d px (ordered: %d)", n2, n1);
    CHECK(n2 > 0);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(rect_pen_width_five_outline_inside) {
    gdi_sel_t s = {0}; int l, t, r, b;
    NEED_DRAW();
    FX(f, 64, 64);
    sel_pen(f.dc, &s, COL_RED, 5); sel_nullbrush(f.dc, &s);
    CHECK(U(Rectangle)(f.dc, 10, 10, 50, 40));
    fx_bbox(&f, PX_RED, &l, &t, &r, &b);
    t_note("5px pen Rectangle(10,10,50,40) bbox (%d,%d)-(%d,%d)", l, t, r, b);
    CHECK(l >= 6 && l <= 10 && t >= 6 && t <= 10 && r >= 47 && r <= 52 && b >= 37 && b <= 42);
    EXPECT_EQ(fx_px(&f, 30, 25), PX_WHITE);
    CHECK(fx_near(&f, 10, 25, 1, PX_RED)); CHECK(fx_near(&f, 30, 10, 1, PX_RED));
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(rect_rop2_xor_twice_restores) {
    gdi_sel_t s = {0}; int i; uint32_t before[48 * 48];
    NEED_DRAW(); REQUIRE(U(SetROP2));
    FX(f, 48, 48);
    for (i = 0; i < 48 * 48; i++) f.bits[i] = (uint32_t)(i * 2654435761u) & 0xffffff;
    memcpy(before, f.bits, sizeof before);
    sel_pen(f.dc, &s, RGB(0x12, 0x34, 0x56), 1); sel_brush(f.dc, &s, RGB(0xaa, 0xbb, 0xcc));
    U(SetROP2)(f.dc, R2_XORPEN);
    U(Rectangle)(f.dc, 5, 5, 40, 40);
    fx_sync();
    CHECK(memcmp(before, f.bits, sizeof before) != 0);
    U(Rectangle)(f.dc, 5, 5, 40, 40);
    fx_sync();
    for (i = 0; i < 48 * 48; i++) if ((f.bits[i] & 0xffffff) != before[i]) break;
    EXPECT_INT(i, 48 * 48);                               /* XOR with the same pen and brush is an involution */
    sel_done(f.dc, &s);
    fx_close(&f);
}
