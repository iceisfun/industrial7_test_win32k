/* gap_07 GDI direct pinpoint tests: NtGdiPtVisible, NtGdiRectVisible, NtGdiCombineTransform, NtGdiAngleArc,
 * NtGdiExtTextOutW, NtGdiDrawEscape, NtGdiColorCorrectPalette, NtGdiCreateHalftonePalette, NtGdiGetColorAdjustment,
 * NtGdiUpdateColors, NtGdiDeleteColorSpace, NtGdiMirrorWindowOrg, NtGdiEnsureDpiDepDefaultGuiFontForPlateau,
 * NtUserSelectPalette. */
#include "g7.h"
#include <math.h>

static HDC bogus_dc(void) { return (HDC)(ULONG_PTR)0x00010123; }
static int near_f(float a, float b) { return fabsf(a - b) < 1e-3f; }

/* ---- NtGdiPtVisible ------------------------------------------------------------------------------------------ */
TEST(gap7_ptvisible_invalid_dc_returns_minus_one_with_6) {
    NtGdiPtVisible_pfn f = G7N(NtGdiPtVisible); REQUIRE(f);
    SetLastError(0);
    EXPECT_EQ(f(NULL, 0, 0), -1);                 /* DCOBJ null -> EngSetLastError(ERROR_INVALID_HANDLE), -1 */
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    SetLastError(0);
    EXPECT_EQ(f(bogus_dc(), 1, 1), -1);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    SetLastError(0);
    EXPECT_EQ(f((HDC)(ULONG_PTR)0xffffffff, 1, 1), -1);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
}
TEST(gap7_ptvisible_deleted_dc_returns_minus_one) {
    NtGdiPtVisible_pfn f = G7N(NtGdiPtVisible); REQUIRE(f);
    HDC dc = U(CreateCompatibleDC)(NULL); REQUIRE(dc);
    U(DeleteDC)(dc);
    SetLastError(0);
    EXPECT_EQ(f(dc, 0, 0), -1);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
}
TEST(gap7_ptvisible_bitmap_bounds) {
    NtGdiPtVisible_pfn f = G7N(NtGdiPtVisible); REQUIRE(f);
    struct g7_dib d; REQUIRE(g7_dib_open(&d, 8, 8));
    EXPECT_EQ(f(d.dc, 0, 0), 1);
    EXPECT_EQ(f(d.dc, 7, 7), 1);
    EXPECT_EQ(f(d.dc, 3, 4), 1);
    EXPECT_EQ(f(d.dc, 8, 0), 0);
    EXPECT_EQ(f(d.dc, 0, 8), 0);
    EXPECT_EQ(f(d.dc, -1, 0), 0);
    EXPECT_EQ(f(d.dc, 0, -1), 0);
    EXPECT_EQ(f(d.dc, 100, 100), 0);
    g7_dib_close(&d);
}
TEST(gap7_ptvisible_follows_clip_region) {
    NtGdiPtVisible_pfn f = G7N(NtGdiPtVisible); REQUIRE(f);
    struct g7_dib d; REQUIRE(g7_dib_open(&d, 16, 16));
    U(IntersectClipRect)(d.dc, 0, 0, 4, 4);
    EXPECT_EQ(f(d.dc, 3, 3), 1);
    EXPECT_EQ(f(d.dc, 4, 4), 0);
    EXPECT_EQ(f(d.dc, 10, 10), 0);
    U(SelectClipRgn)(d.dc, NULL);
    EXPECT_EQ(f(d.dc, 10, 10), 1);
    g7_dib_close(&d);
}
TEST(gap7_ptvisible_applies_viewport_origin) {
    NtGdiPtVisible_pfn f = G7N(NtGdiPtVisible); REQUIRE(f);
    struct g7_dib d; REQUIRE(g7_dib_open(&d, 8, 8));
    U(SetViewportOrgEx)(d.dc, 2, 2, NULL);        /* logical coordinates are transformed before the region test */
    EXPECT_EQ(f(d.dc, 0, 0), 1);
    EXPECT_EQ(f(d.dc, 5, 5), 1);
    EXPECT_EQ(f(d.dc, 6, 6), 0);
    EXPECT_EQ(f(d.dc, -2, -2), 1);
    EXPECT_EQ(f(d.dc, -3, -3), 0);
    g7_dib_close(&d);
}
TEST(gap7_ptvisible_route_matches_direct) {
    NtGdiPtVisible_pfn f = G7N(NtGdiPtVisible); REQUIRE(f);
    struct g7_dib d; REQUIRE(g7_dib_open(&d, 8, 8));
    for (int x = -2; x < 11; x += 3)
        for (int y = -2; y < 11; y += 3)
            EXPECT_EQ(U(PtVisible)(d.dc, x, y), f(d.dc, x, y));
    g7_dib_close(&d);
}
TEST(gap7_ptvisible_empty_clip_hides_everything) {
    NtGdiPtVisible_pfn f = G7N(NtGdiPtVisible); REQUIRE(f);
    struct g7_dib d; REQUIRE(g7_dib_open(&d, 8, 8));
    HRGN empty = U(CreateRectRgn)(0, 0, 0, 0);
    U(SelectClipRgn)(d.dc, empty);
    EXPECT_EQ(f(d.dc, 1, 1), 0);
    EXPECT_EQ(f(d.dc, 0, 0), 0);
    U(SelectClipRgn)(d.dc, NULL);
    U(DeleteObject)(empty);
    g7_dib_close(&d);
}

/* ---- NtGdiRectVisible ---------------------------------------------------------------------------------------- */
TEST(gap7_rectvisible_inside_outside_partial) {
    NtGdiRectVisible_pfn f = G7N(NtGdiRectVisible); REQUIRE(f);
    struct g7_dib d; REQUIRE(g7_dib_open(&d, 8, 8));
    RECT in_ = { 1, 1, 5, 5 }, out_ = { 20, 20, 30, 30 }, part = { 6, 6, 20, 20 }, big = { -50, -50, 50, 50 };
    CHECK(f(d.dc, &in_) != 0);
    EXPECT_EQ(f(d.dc, &out_), 0);
    CHECK(f(d.dc, &part) != 0);
    CHECK(f(d.dc, &big) != 0);
    g7_dib_close(&d);
}
TEST(gap7_rectvisible_clip_region) {
    NtGdiRectVisible_pfn f = G7N(NtGdiRectVisible); REQUIRE(f);
    struct g7_dib d; REQUIRE(g7_dib_open(&d, 16, 16));
    U(IntersectClipRect)(d.dc, 0, 0, 4, 4);
    RECT a = { 1, 1, 3, 3 }, b = { 8, 8, 12, 12 }, c = { 3, 3, 9, 9 };
    CHECK(f(d.dc, &a) != 0);
    EXPECT_EQ(f(d.dc, &b), 0);
    CHECK(f(d.dc, &c) != 0);
    g7_dib_close(&d);
}
TEST(gap7_rectvisible_route_matches_direct) {
    NtGdiRectVisible_pfn f = G7N(NtGdiRectVisible); REQUIRE(f);
    struct g7_dib d; REQUIRE(g7_dib_open(&d, 8, 8));
    RECT rs[] = { {0,0,8,8}, {8,8,9,9}, {-4,-4,1,1}, {3,3,3,3}, {4,0,2,7} };
    for (unsigned i = 0; i < sizeof rs / sizeof *rs; i++)
        EXPECT_EQ(U(RectVisible)(d.dc, &rs[i]) != 0, f(d.dc, &rs[i]) != 0);
    g7_dib_close(&d);
}
TEST(gap7_rectvisible_invalid_dc) {
    NtGdiRectVisible_pfn f = G7N(NtGdiRectVisible); REQUIRE(f);
    RECT r = { 0, 0, 4, 4 };
    int a = f(bogus_dc(), &r);
    t_note("RectVisible(bogus dc) = %d", a);
    CHECK(a == 0 || a == -1);
}

/* ---- NtGdiCombineTransform ----------------------------------------------------------------------------------- */
TEST(gap7_combine_identity) {
    NtGdiCombineTransform_pfn f = G7N(NtGdiCombineTransform); REQUIRE(f);
    XFORM id = { 1, 0, 0, 1, 0, 0 }, out; memset(&out, 0, sizeof out);
    CHECK(f(&out, &id, &id) != 0);
    CHECK(near_f(out.eM11, 1) && near_f(out.eM22, 1) && near_f(out.eM12, 0) && near_f(out.eM21, 0));
    CHECK(near_f(out.eDx, 0) && near_f(out.eDy, 0));
}
TEST(gap7_combine_translate_then_scale) {
    NtGdiCombineTransform_pfn f = G7N(NtGdiCombineTransform); REQUIRE(f);
    XFORM a = { 1, 0, 0, 1, 10, 20 }, b = { 2, 0, 0, 3, 5, 6 }, out; memset(&out, 0, sizeof out);
    CHECK(f(&out, &a, &b) != 0);          /* a applied first, then b: dx = 10*2+5, dy = 20*3+6 */
    CHECK(near_f(out.eM11, 2) && near_f(out.eM22, 3) && near_f(out.eM12, 0) && near_f(out.eM21, 0));
    CHECK(near_f(out.eDx, 25));
    CHECK(near_f(out.eDy, 66));
}
TEST(gap7_combine_is_not_commutative) {
    NtGdiCombineTransform_pfn f = G7N(NtGdiCombineTransform); REQUIRE(f);
    XFORM a = { 1, 0, 0, 1, 10, 20 }, b = { 2, 0, 0, 3, 5, 6 }, o1, o2;
    memset(&o1, 0, sizeof o1); memset(&o2, 0, sizeof o2);
    CHECK(f(&o1, &a, &b) != 0);
    CHECK(f(&o2, &b, &a) != 0);           /* b first: dx = 5+10, dy = 6+20, scale kept */
    CHECK(near_f(o2.eM11, 2) && near_f(o2.eM22, 3));
    CHECK(near_f(o2.eDx, 15));
    CHECK(near_f(o2.eDy, 26));
    CHECK(!near_f(o1.eDx, o2.eDx));
}
TEST(gap7_combine_rotations_add) {
    NtGdiCombineTransform_pfn f = G7N(NtGdiCombineTransform); REQUIRE(f);
    XFORM r90 = { 0, 1, -1, 0, 0, 0 }, out; memset(&out, 0, sizeof out);
    CHECK(f(&out, &r90, &r90) != 0);      /* 90 + 90 degrees */
    CHECK(near_f(out.eM11, -1) && near_f(out.eM22, -1) && near_f(out.eM12, 0) && near_f(out.eM21, 0));
    XFORM out4 = out;
    CHECK(f(&out4, &out, &out) != 0);     /* 180 + 180 = identity; out4 aliases an input source */
    CHECK(near_f(out4.eM11, 1) && near_f(out4.eM22, 1));
}
TEST(gap7_combine_output_aliasing_inputs) {
    NtGdiCombineTransform_pfn f = G7N(NtGdiCombineTransform); REQUIRE(f);
    XFORM a = { 1, 0, 0, 1, 10, 20 }, b = { 2, 0, 0, 3, 5, 6 }, ref;
    memset(&ref, 0, sizeof ref);
    CHECK(f(&ref, &a, &b) != 0);
    XFORM x = a;
    CHECK(f(&x, &x, &b) != 0);            /* the kernel copies both inputs before writing the result */
    CHECK(near_f(x.eDx, ref.eDx) && near_f(x.eDy, ref.eDy) && near_f(x.eM11, ref.eM11));
    XFORM y = b;
    CHECK(f(&y, &a, &y) != 0);
    CHECK(near_f(y.eDx, ref.eDx) && near_f(y.eDy, ref.eDy) && near_f(y.eM22, ref.eM22));
}
TEST(gap7_combine_route_matches_direct) {
    NtGdiCombineTransform_pfn f = G7N(NtGdiCombineTransform); REQUIRE(f);
    XFORM a = { 0.5f, 0.25f, -0.25f, 2.0f, 3.0f, -4.0f }, b = { 1.5f, -1.0f, 0.5f, 0.75f, -2.0f, 8.0f }, o1, o2;
    memset(&o1, 0, sizeof o1); memset(&o2, 0, sizeof o2);
    BOOL r1 = f(&o1, &a, &b);
    BOOL r2 = U(CombineTransform)(&o2, &a, &b);
    EXPECT_EQ(r1 != 0, r2 != 0);
    CHECK(near_f(o1.eM11, o2.eM11) && near_f(o1.eM12, o2.eM12) && near_f(o1.eM21, o2.eM21) && near_f(o1.eM22, o2.eM22));
    CHECK(near_f(o1.eDx, o2.eDx) && near_f(o1.eDy, o2.eDy));
    /* general product, row-vector convention: out = a x b */
    CHECK(near_f(o1.eM11, a.eM11 * b.eM11 + a.eM12 * b.eM21));
    CHECK(near_f(o1.eM12, a.eM11 * b.eM12 + a.eM12 * b.eM22));
    CHECK(near_f(o1.eM21, a.eM21 * b.eM11 + a.eM22 * b.eM21));
    CHECK(near_f(o1.eM22, a.eM21 * b.eM12 + a.eM22 * b.eM22));
    CHECK(near_f(o1.eDx, a.eDx * b.eM11 + a.eDy * b.eM21 + b.eDx));
    CHECK(near_f(o1.eDy, a.eDx * b.eM12 + a.eDy * b.eM22 + b.eDy));
}

/* ---- NtGdiAngleArc ------------------------------------------------------------------------------------------- */
TEST(gap7_anglearc_invalid_dc_fails) {
    NtGdiAngleArc_pfn f = G7N(NtGdiAngleArc); REQUIRE(f);
    EXPECT_EQ(f(NULL, 10, 10, 5, g7_f2d(0), g7_f2d(90)), FALSE);
    EXPECT_EQ(f(bogus_dc(), 10, 10, 5, g7_f2d(0), g7_f2d(90)), FALSE);
}
TEST(gap7_anglearc_moves_current_position_to_arc_end) {
    NtGdiAngleArc_pfn f = G7N(NtGdiAngleArc); REQUIRE(f);
    struct g7_dib d; REQUIRE(g7_dib_open(&d, 64, 64));
    U(MoveToEx)(d.dc, 0, 0, NULL);
    BOOL r = f(d.dc, 20, 20, 10, g7_f2d(0.0f), g7_f2d(90.0f));
    EXPECT_EQ(r != 0, 1);
    POINT cp = { -1, -1 };
    U(GetCurrentPositionEx)(d.dc, &cp);
    /* end = (x + r*cos(start+sweep), y - r*sin(start+sweep)) = (20, 10) */
    CHECKF(abs(cp.x - 20) <= 1 && abs(cp.y - 10) <= 1, "current position (%ld,%ld)", cp.x, cp.y);
    g7_dib_close(&d);
}
TEST(gap7_anglearc_draws_start_and_end_points) {
    NtGdiAngleArc_pfn f = G7N(NtGdiAngleArc); REQUIRE(f);
    struct g7_dib d; REQUIRE(g7_dib_open(&d, 64, 64));
    for (int i = 0; i < 64 * 64; i++) d.bits[i] = 0xffffffff;
    U(MoveToEx)(d.dc, 20, 20, NULL);
    EXPECT_EQ(f(d.dc, 20, 20, 10, g7_f2d(0.0f), g7_f2d(90.0f)) != 0, 1);
    U(GdiFlush)();
    int hit_start = 0, hit_end = 0;
    for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
        if ((d.bits[(20 + dy) * 64 + 30 + dx] & 0xffffff) != 0xffffff) hit_start = 1;
        if ((d.bits[(10 + dy) * 64 + 20 + dx] & 0xffffff) != 0xffffff) hit_end = 1;
    }
    CHECK(hit_start);    /* connecting line ends / arc begins at (x + r, y) */
    CHECK(hit_end);
    g7_dib_close(&d);
}
TEST(gap7_anglearc_route_matches_direct) {
    NtGdiAngleArc_pfn f = G7N(NtGdiAngleArc); REQUIRE(f);
    struct g7_dib a, b; REQUIRE(g7_dib_open(&a, 48, 48)); REQUIRE(g7_dib_open(&b, 48, 48));
    for (int i = 0; i < 48 * 48; i++) a.bits[i] = b.bits[i] = 0xffffffff;
    U(MoveToEx)(a.dc, 5, 5, NULL); U(MoveToEx)(b.dc, 5, 5, NULL);
    f(a.dc, 24, 24, 12, g7_f2d(30.0f), g7_f2d(120.0f));
    U(AngleArc)(b.dc, 24, 24, 12, 30.0f, 120.0f);
    U(GdiFlush)();
    CHECK(memcmp(a.bits, b.bits, 48 * 48 * 4) == 0);
    POINT pa, pb; U(GetCurrentPositionEx)(a.dc, &pa); U(GetCurrentPositionEx)(b.dc, &pb);
    EXPECT_EQ(pa.x, pb.x); EXPECT_EQ(pa.y, pb.y);
    g7_dib_close(&a); g7_dib_close(&b);
}
TEST(gap7_anglearc_nonfinite_angle_bits_note) {
    NtGdiAngleArc_pfn f = G7N(NtGdiAngleArc); REQUIRE(f);
    struct g7_dib d; REQUIRE(g7_dib_open(&d, 32, 32));
    DWORD nan_ = 0x7fc00000, inf_ = 0x7f800000;
    BOOL a = f(d.dc, 10, 10, 5, nan_, g7_f2d(10));
    BOOL b = f(d.dc, 10, 10, 5, g7_f2d(10), inf_);
    t_note("AngleArc NaN start -> %d, Inf sweep -> %d", a, b);
    g7_dib_close(&d);
}

/* ---- NtGdiExtTextOutW ---------------------------------------------------------------------------------------- */
static int count_dark(const struct g7_dib *d, int *maxx)
{
    int n = 0; if (maxx) *maxx = -1;
    for (int y = 0; y < d->h; y++) for (int x = 0; x < d->w; x++)
        if ((d->bits[y * d->w + x] & 0xffffff) != 0xffffff) { n++; if (maxx && x > *maxx) *maxx = x; }
    return n;
}
static void white(struct g7_dib *d) { for (int i = 0; i < d->w * d->h; i++) d->bits[i] = 0xffffffff; }
TEST(gap7_exttextout_invalid_dc_fails) {
    NtGdiExtTextOutW_pfn f = G7N(NtGdiExtTextOutW); REQUIRE(f);
    EXPECT_EQ(f(NULL, 0, 0, 0, NULL, L"A", 1, NULL, 0), FALSE);
    EXPECT_EQ(f(bogus_dc(), 0, 0, 0, NULL, L"A", 1, NULL, 0), FALSE);
}
TEST(gap7_exttextout_zero_length_succeeds) {
    NtGdiExtTextOutW_pfn f = G7N(NtGdiExtTextOutW); REQUIRE(f);
    struct g7_dib d; REQUIRE(g7_dib_open(&d, 32, 32)); white(&d);
    EXPECT_EQ(f(d.dc, 0, 0, 0, NULL, L"A", 0, NULL, 0), TRUE);     /* cwc == 0 -> returns 1 without drawing */
    EXPECT_EQ(f(d.dc, 0, 0, 0, NULL, NULL, 0, NULL, 0), TRUE);
    U(GdiFlush)();
    EXPECT_EQ(count_dark(&d, NULL), 0);
    g7_dib_close(&d);
}
TEST(gap7_exttextout_null_string_with_count_fails) {
    NtGdiExtTextOutW_pfn f = G7N(NtGdiExtTextOutW); REQUIRE(f);
    if (t_is_wine()) { t_skip("wine's ExtTextOut hangs on a NULL string"); return; }
    struct g7_dib d; REQUIRE(g7_dib_open(&d, 32, 32));
    EXPECT_EQ(f(d.dc, 0, 0, 0, NULL, NULL, 3, NULL, 0), FALSE);    /* string required when cwc > 0 */
    g7_dib_close(&d);
}
TEST(gap7_exttextout_option_bits_need_a_rect) {
    NtGdiExtTextOutW_pfn f = G7N(NtGdiExtTextOutW); REQUIRE(f);
    struct g7_dib d; REQUIRE(g7_dib_open(&d, 32, 32));
    EXPECT_EQ(f(d.dc, 0, 0, ETO_OPAQUE, NULL, L"A", 1, NULL, 0), FALSE);
    EXPECT_EQ(f(d.dc, 0, 0, ETO_CLIPPED, NULL, L"A", 1, NULL, 0), FALSE);
    g7_dib_close(&d);
}
TEST(gap7_exttextout_count_limit_is_64k) {
    NtGdiExtTextOutW_pfn f = G7N(NtGdiExtTextOutW); REQUIRE(f);
    struct g7_dib d; REQUIRE(g7_dib_open(&d, 32, 32));
    static WCHAR big[1];
    EXPECT_EQ(f(d.dc, 0, 0, 0, NULL, big, 0x10000, NULL, 0), FALSE);
    EXPECT_EQ(f(d.dc, 0, 0, 0, NULL, big, 0xffffffffu, NULL, 0), FALSE);
    g7_dib_close(&d);
}
TEST(gap7_exttextout_draws_text) {
    NtGdiExtTextOutW_pfn f = G7N(NtGdiExtTextOutW); REQUIRE(f);
    struct g7_dib d; REQUIRE(g7_dib_open(&d, 64, 32)); white(&d);
    U(SetBkMode)(d.dc, TRANSPARENT); U(SetTextColor)(d.dc, RGB(0, 0, 0));
    EXPECT_EQ(f(d.dc, 2, 2, 0, NULL, L"MM", 2, NULL, 0) != 0, 1);
    U(GdiFlush)();
    CHECK(count_dark(&d, NULL) > 0);
    g7_dib_close(&d);
}
TEST(gap7_exttextout_dx_array_spreads_glyphs) {
    NtGdiExtTextOutW_pfn f = G7N(NtGdiExtTextOutW); REQUIRE(f);
    struct g7_dib d; REQUIRE(g7_dib_open(&d, 128, 32)); white(&d);
    U(SetBkMode)(d.dc, TRANSPARENT);
    INT dx[2] = { 40, 40 };
    EXPECT_EQ(f(d.dc, 2, 2, 0, NULL, L"II", 2, dx, 0) != 0, 1);
    U(GdiFlush)();
    int maxx; CHECK(count_dark(&d, &maxx) > 0);
    CHECKF(maxx >= 42, "second glyph should start at x=42, rightmost dark pixel %d", maxx);
    g7_dib_close(&d);
}
TEST(gap7_exttextout_opaque_rect_fills_background) {
    NtGdiExtTextOutW_pfn f = G7N(NtGdiExtTextOutW); REQUIRE(f);
    struct g7_dib d; REQUIRE(g7_dib_open(&d, 32, 32)); white(&d);
    U(SetBkColor)(d.dc, RGB(255, 0, 0));
    RECT rc = { 4, 4, 20, 20 };
    EXPECT_EQ(f(d.dc, 4, 4, ETO_OPAQUE, &rc, NULL, 0, NULL, 0) != 0, 1);
    U(GdiFlush)();
    EXPECT_EQ(d.bits[10 * 32 + 10] & 0xffffff, 0xff0000);
    EXPECT_EQ(d.bits[4 * 32 + 4] & 0xffffff, 0xff0000);
    EXPECT_EQ(d.bits[19 * 32 + 19] & 0xffffff, 0xff0000);
    EXPECT_EQ(d.bits[20 * 32 + 20] & 0xffffff, 0xffffff);          /* right/bottom edges are exclusive */
    EXPECT_EQ(d.bits[3 * 32 + 3] & 0xffffff, 0xffffff);
    g7_dib_close(&d);
}
TEST(gap7_exttextout_route_textoutw_matches_direct) {
    NtGdiExtTextOutW_pfn f = G7N(NtGdiExtTextOutW); REQUIRE(f);
    struct g7_dib a, b; REQUIRE(g7_dib_open(&a, 64, 32)); REQUIRE(g7_dib_open(&b, 64, 32));
    white(&a); white(&b);
    U(SetBkMode)(a.dc, TRANSPARENT); U(SetBkMode)(b.dc, TRANSPARENT);
    f(a.dc, 3, 3, 0, NULL, L"Wg", 2, NULL, 0);
    U(TextOutW)(b.dc, 3, 3, L"Wg", 2);
    U(GdiFlush)();
    CHECK(memcmp(a.bits, b.bits, 64 * 32 * 4) == 0);
    g7_dib_close(&a); g7_dib_close(&b);
}

/* ---- NtGdiDrawEscape ----------------------------------------------------------------------------------------- */
TEST(gap7_drawescape_negative_length_is_minus_one) {
    NtGdiDrawEscape_pfn f = G7N(NtGdiDrawEscape); REQUIRE(f);
    struct g7_dib d; REQUIRE(g7_dib_open(&d, 8, 8));
    char buf[8] = { 0 };
    EXPECT_EQ(f(d.dc, 1, -1, buf), -1);
    EXPECT_EQ(f(d.dc, 1, (int)0x80000000, buf), -1);
    EXPECT_EQ(f(d.dc, 1, -1, NULL), -1);
    g7_dib_close(&d);
}
TEST(gap7_drawescape_null_buffer_with_length_is_minus_one) {
    NtGdiDrawEscape_pfn f = G7N(NtGdiDrawEscape); REQUIRE(f);
    struct g7_dib d; REQUIRE(g7_dib_open(&d, 8, 8));
    EXPECT_EQ(f(d.dc, 1, 4, NULL), -1);
    EXPECT_EQ(f(d.dc, 1, 1, NULL), -1);
    EXPECT_EQ(f(d.dc, 1, 0x7fffffff, NULL), -1);
    g7_dib_close(&d);
}
TEST(gap7_drawescape_unsupported_escape_on_memory_dc) {
    NtGdiDrawEscape_pfn f = G7N(NtGdiDrawEscape); REQUIRE(f);
    struct g7_dib d; REQUIRE(g7_dib_open(&d, 8, 8));
    char buf[16] = { 0 };
    int a = f(d.dc, 0x7777, 0, NULL);
    int b = f(d.dc, 0x7777, 16, buf);      /* <= 0x40 bytes go through the on-stack copy */
    char big[0x100] = { 0 };
    int c = f(d.dc, 0x7777, sizeof big, big);   /* > 0x40 bytes go through MmSecureVirtualMemory */
    t_note("DrawEscape(unsupported): %d %d %d", a, b, c);
    CHECK(a <= 0 && b <= 0 && c <= 0);
    g7_dib_close(&d);
}
TEST(gap7_drawescape_user32_invalid_dc_sets_6) {
    char buf[4] = { 0 };
    SetLastError(0);
    EXPECT_EQ(U(DrawEscape)(NULL, 1, 4, buf), 0);   /* pldcGet fails -> GdiSetLastError(ERROR_INVALID_HANDLE) */
    EXPECT_ERR(ERROR_INVALID_HANDLE);
}

/* ---- NtGdiColorCorrectPalette -------------------------------------------------------------------------------- */
static HPALETTE pal4(void)
{
    struct { WORD ver, n; PALETTEENTRY e[4]; } lp = { 0x300, 4, { {1,2,3,0}, {4,5,6,0}, {7,8,9,0}, {10,11,12,0} } };
    return U(CreatePalette)((LOGPALETTE *)&lp);
}
TEST(gap7_colorcorrect_bad_arguments_set_57) {
    NtGdiColorCorrectPalette_pfn f = G7N(NtGdiColorCorrectPalette); REQUIRE(f);
    HDC dc = U(GetDC)(NULL); HPALETTE pal = pal4(); REQUIRE(dc && pal);
    PALETTEENTRY buf[8];
    struct { HDC dc; HPALETTE p; ULONG first, n; const char *why; } c[] = {
        { NULL,      pal,  0, 2, "null dc" },
        { bogus_dc(), pal, 0, 2, "bogus dc" },
        { dc,        NULL, 0, 2, "null palette" },
        { dc, (HPALETTE)(ULONG_PTR)0x1234, 0, 2, "bogus palette" },
        { dc,        pal,  0, 0, "zero entries" },
        { dc,        pal,  0, 5, "more entries than the palette holds" },
        { dc,        pal,  4, 1, "first == count" },
        { dc,        pal,  3, 2, "first + n > count" },
        { dc,        pal,  0, 0x40000000, "n >= 0x40000000" },
    };
    for (unsigned i = 0; i < sizeof c / sizeof *c; i++) {
        SetLastError(0);
        ULONG r = f(c[i].dc, c[i].p, c[i].first, c[i].n, buf, 0);
        EXPECT_EQ(r, 0);
        CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "%s: err %lu", c[i].why, GetLastError());
    }
    U(DeleteObject)(pal); U(ReleaseDC)(NULL, dc);
}
TEST(gap7_colorcorrect_valid_range_note) {
    NtGdiColorCorrectPalette_pfn f = G7N(NtGdiColorCorrectPalette); REQUIRE(f);
    HDC dc = U(GetDC)(NULL); HPALETTE pal = pal4(); REQUIRE(dc && pal);
    PALETTEENTRY buf[4]; memset(buf, 0, sizeof buf);
    SetLastError(0);
    ULONG r = f(dc, pal, 0, 4, buf, 0);
    t_note("ColorCorrectPalette(read 4) = %lu err %lu", r, GetLastError());
    CHECK(r <= 4);
    U(DeleteObject)(pal); U(ReleaseDC)(NULL, dc);
}

/* ---- NtGdiCreateHalftonePalette ------------------------------------------------------------------------------ */
TEST(gap7_halftone_palette_object_properties) {
    NtGdiCreateHalftonePalette_pfn f = G7N(NtGdiCreateHalftonePalette); REQUIRE(f);
    HDC dc = U(GetDC)(NULL); REQUIRE(dc);
    HPALETTE p = f(dc);
    REQUIRE(p);
    EXPECT_EQ(U(GetObjectType)(p), OBJ_PAL);
    UINT n = U(GetPaletteEntries)(p, 0, 0, NULL);
    t_note("halftone palette entries: %u", n);
    CHECK(n > 0 && n <= 256);
    WORD cnt = 0;
    EXPECT_EQ(U(GetObjectW)(p, sizeof cnt, &cnt), (int)sizeof cnt);
    EXPECT_EQ(cnt, n);
    struct k_gdicell c;
    if (!t_is_wine() && k_gdi_cell(p, &c)) CHECK(c.object != NULL);
    EXPECT_EQ(U(DeleteObject)(p), TRUE);
    /* Windows 10 gdi32 keeps answering GetObjectType for a deleted handle; the handle is dead when a second delete fails and the size query is empty */
    EXPECT_EQ(U(DeleteObject)(p), FALSE);
    EXPECT_EQ(U(GetObjectW)(p, sizeof cnt, &cnt), 0);
    U(ReleaseDC)(NULL, dc);
}
TEST(gap7_halftone_palette_distinct_handles) {
    NtGdiCreateHalftonePalette_pfn f = G7N(NtGdiCreateHalftonePalette); REQUIRE(f);
    HDC dc = U(GetDC)(NULL); REQUIRE(dc);
    HPALETTE a = f(dc), b = f(dc);
    REQUIRE(a && b);
    CHECK(a != b);
    EXPECT_EQ(U(DeleteObject)(a), TRUE);
    EXPECT_EQ(U(GetObjectType)(b), OBJ_PAL);
    EXPECT_EQ(U(DeleteObject)(b), TRUE);
    U(ReleaseDC)(NULL, dc);
}
TEST(gap7_halftone_palette_on_memory_dc_and_route) {
    NtGdiCreateHalftonePalette_pfn f = G7N(NtGdiCreateHalftonePalette); REQUIRE(f);
    struct g7_dib d; REQUIRE(g7_dib_open(&d, 4, 4));
    HPALETTE a = f(d.dc), b = U(CreateHalftonePalette)(d.dc);
    REQUIRE(a && b);
    EXPECT_EQ(U(GetPaletteEntries)(a, 0, 0, NULL), U(GetPaletteEntries)(b, 0, 0, NULL));
    U(DeleteObject)(a); U(DeleteObject)(b);
    g7_dib_close(&d);
}
TEST(gap7_halftone_palette_invalid_dc_note) {
    NtGdiCreateHalftonePalette_pfn f = G7N(NtGdiCreateHalftonePalette); REQUIRE(f);
    HPALETTE p = f(bogus_dc());
    t_note("halftone(bogus dc) = %p err %lu", (void *)p, GetLastError());
    if (p) U(DeleteObject)(p);
}

/* ---- NtGdiGetColorAdjustment --------------------------------------------------------------------------------- */
TEST(gap7_getcoloradjustment_is_deterministic_and_sized) {
    HDC dc = U(GetDC)(NULL); REQUIRE(dc);
    COLORADJUSTMENT a, b; memset(&a, 0xcc, sizeof a); memset(&b, 0x33, sizeof b);
    BOOL r1 = U(GetColorAdjustment)(dc, &a);
    BOOL r2 = U(GetColorAdjustment)(dc, &b);
    EXPECT_EQ(r1 != 0, r2 != 0);
    if (r1) {
        CHECK(memcmp(&a, &b, sizeof a) == 0);
        t_note("COLORADJUSTMENT size=%u flags=0x%x gamma=%u/%u/%u black=%u white=%u contrast=%d bright=%d color=%d tint=%d",
               a.caSize, a.caFlags, a.caRedGamma, a.caGreenGamma, a.caBlueGamma, a.caReferenceBlack, a.caReferenceWhite,
               a.caContrast, a.caBrightness, a.caColorfulness, a.caRedGreenTint);
        EXPECT_EQ(sizeof a, 24);
    }
    U(ReleaseDC)(NULL, dc);
}
TEST(gap7_getcoloradjustment_direct_stub_writes_24_bytes) {
    NtGdiGetColorAdjustment_pfn f = G7N(NtGdiGetColorAdjustment); REQUIRE(f);
    HDC dc = U(GetDC)(NULL); REQUIRE(dc);
    unsigned char raw[40]; memset(raw, 0xab, sizeof raw);
    f(dc, raw + 8);
    for (int i = 0; i < 8; i++) EXPECT_EQ(raw[i], 0xab);          /* nothing before */
    for (int i = 32; i < 40; i++) EXPECT_EQ(raw[i], 0xab);        /* nothing past the 24-byte structure */
    U(ReleaseDC)(NULL, dc);
}

/* ---- NtGdiUpdateColors --------------------------------------------------------------------------------------- */
TEST(gap7_updatecolors_invalid_dc_returns_zero) {
    NtGdiUpdateColors_pfn f = G7N(NtGdiUpdateColors); REQUIRE(f);
    EXPECT_EQ(f(NULL), 0);
    EXPECT_EQ(f(bogus_dc()), 0);
    HDC dc = U(CreateCompatibleDC)(NULL); REQUIRE(dc);
    U(DeleteDC)(dc);
    EXPECT_EQ(f(dc), 0);
}
TEST(gap7_updatecolors_non_palette_surfaces_note) {
    NtGdiUpdateColors_pfn f = G7N(NtGdiUpdateColors); REQUIRE(f);
    struct g7_dib d; REQUIRE(g7_dib_open(&d, 4, 4));
    HDC scr = U(GetDC)(NULL);
    int a = f(d.dc), b = f(scr);
    t_note("UpdateColors(32bpp mem)=%d, UpdateColors(screen)=%d, route=%d", a, b, U(UpdateColors)(scr));
    CHECK(a == 0 || a == 1);
    U(ReleaseDC)(NULL, scr);
    g7_dib_close(&d);
}

/* ---- NtGdiDeleteColorSpace ----------------------------------------------------------------------------------- */
static HCOLORSPACE make_cs(void)
{
    LOGCOLORSPACEW lcs; memset(&lcs, 0, sizeof lcs);
    lcs.lcsSignature = 0x50534F43; /* "PSOC" */ lcs.lcsVersion = 0x400; lcs.lcsSize = sizeof lcs;
    lcs.lcsCSType = LCS_CALIBRATED_RGB; lcs.lcsIntent = LCS_GM_IMAGES;
    lcs.lcsEndpoints.ciexyzRed.ciexyzX = 0x1a3d70a; lcs.lcsEndpoints.ciexyzRed.ciexyzY = 0x0d9c28f; lcs.lcsEndpoints.ciexyzRed.ciexyzZ = 0x00ccccc;
    lcs.lcsEndpoints.ciexyzGreen.ciexyzX = 0x0a3d70a; lcs.lcsEndpoints.ciexyzGreen.ciexyzY = 0x1999999; lcs.lcsEndpoints.ciexyzGreen.ciexyzZ = 0x0147ae1;
    lcs.lcsEndpoints.ciexyzBlue.ciexyzX = 0x0733333; lcs.lcsEndpoints.ciexyzBlue.ciexyzY = 0x03a3d70; lcs.lcsEndpoints.ciexyzBlue.ciexyzZ = 0x2d3d70a;
    lcs.lcsGammaRed = lcs.lcsGammaGreen = lcs.lcsGammaBlue = 0x00023333;
    return U(CreateColorSpaceW)(&lcs);
}
TEST(gap7_deletecolorspace_lifetime) {
    NtGdiDeleteColorSpace_pfn f = G7N(NtGdiDeleteColorSpace); REQUIRE(f);
    HCOLORSPACE cs = make_cs();
    if (!cs) { t_skip("CreateColorSpaceW failed err %lu", GetLastError()); return; }
    EXPECT_EQ(U(GetObjectType)(cs), OBJ_COLORSPACE);
    struct k_gdicell c;
    if (!t_is_wine() && k_gdi_cell(cs, &c)) CHECK(c.object != NULL);
    EXPECT_EQ(f(cs) != 0, 1);                          /* GetObjectType keeps answering for a deleted handle on Windows 10; death shows as the failed second delete */
    SetLastError(0);
    EXPECT_EQ(f(cs), FALSE);                           /* double delete fails */
    t_note("double delete color space err %lu", GetLastError());
}
TEST(gap7_deletecolorspace_invalid_handles) {
    NtGdiDeleteColorSpace_pfn f = G7N(NtGdiDeleteColorSpace); REQUIRE(f);
    EXPECT_EQ(f(NULL), FALSE);
    EXPECT_EQ(f((HANDLE)(ULONG_PTR)0x1234), FALSE);
    EXPECT_EQ(f((HANDLE)(ULONG_PTR)0xffffffff), FALSE);
}
TEST(gap7_deletecolorspace_wrong_object_type_rejected) {
    NtGdiDeleteColorSpace_pfn f = G7N(NtGdiDeleteColorSpace); REQUIRE(f);
    HGDIOBJ brush = U(GetStockObject)(BLACK_BRUSH);
    HGDIOBJ pen = U(GetStockObject)(BLACK_PEN);
    EXPECT_EQ(f(brush), FALSE);
    EXPECT_EQ(f(pen), FALSE);
    EXPECT_EQ(U(GetObjectType)(brush), OBJ_BRUSH);     /* the stock objects are untouched */
    EXPECT_EQ(U(GetObjectType)(pen), OBJ_PEN);
    HBITMAP bm = U(CreateBitmap)(2, 2, 1, 1, NULL);
    EXPECT_EQ(f(bm), FALSE);
    EXPECT_EQ(U(GetObjectType)(bm), OBJ_BITMAP);
    U(DeleteObject)(bm);
}
TEST(gap7_deletecolorspace_user32_route_matches) {
    HCOLORSPACE cs = make_cs();
    if (!cs) { t_skip("CreateColorSpaceW failed"); return; }
    EXPECT_EQ(U(DeleteColorSpace)(cs) != 0, 1);
    EXPECT_EQ(U(DeleteColorSpace)(cs) != 0, 0);       /* the handle is dead (GetObjectType keeps answering for it on Windows 10) */
}

/* ---- NtGdiMirrorWindowOrg ------------------------------------------------------------------------------------ */
TEST(gap7_mirrorwindoworg_invalid_dc_false) {
    NtGdiMirrorWindowOrg_pfn f = G7N(NtGdiMirrorWindowOrg); REQUIRE(f);
    EXPECT_EQ(f(NULL), FALSE);
    EXPECT_EQ(f(bogus_dc()), FALSE);
}
TEST(gap7_mirrorwindoworg_valid_dc_true) {
    NtGdiMirrorWindowOrg_pfn f = G7N(NtGdiMirrorWindowOrg); REQUIRE(f);
    struct g7_dib d; REQUIRE(g7_dib_open(&d, 16, 16));
    POINT before, after;
    U(SetWindowOrgEx)(d.dc, 3, 5, NULL);
    U(GetWindowOrgEx)(d.dc, &before);
    EXPECT_EQ(f(d.dc) != 0, 1);
    U(GetWindowOrgEx)(d.dc, &after);
    t_note("window org (%ld,%ld) -> (%ld,%ld) after MirrorWindowOrg (layout 0x%lx)", before.x, before.y, after.x, after.y, (unsigned long)U(GetLayout)(d.dc));
    EXPECT_EQ(after.y, before.y);     /* mirroring is horizontal only */
    g7_dib_close(&d);
}
TEST(gap7_mirrorwindoworg_deleted_dc_false) {
    NtGdiMirrorWindowOrg_pfn f = G7N(NtGdiMirrorWindowOrg); REQUIRE(f);
    HDC dc = U(CreateCompatibleDC)(NULL); REQUIRE(dc);
    U(DeleteDC)(dc);
    EXPECT_EQ(f(dc), FALSE);
}

/* ---- NtGdiEnsureDpiDepDefaultGuiFontForPlateau --------------------------------------------------------------- */
TEST(gap7_ensuredpifont_returns_zero_and_is_idempotent) {
    NtGdiEnsureDpiDepDefaultGuiFontForPlateau_pfn f = G7N(NtGdiEnsureDpiDepDefaultGuiFontForPlateau); REQUIRE(f);
    for (int i = 0; i < 3; i++) EXPECT_EQ(f(), 0);        /* the thunk always returns 0 */
}
TEST(gap7_ensuredpifont_default_gui_font_remains_valid) {
    NtGdiEnsureDpiDepDefaultGuiFontForPlateau_pfn f = G7N(NtGdiEnsureDpiDepDefaultGuiFontForPlateau); REQUIRE(f);
    HGDIOBJ before = U(GetStockObject)(DEFAULT_GUI_FONT);
    REQUIRE(before);
    f();
    HGDIOBJ after = U(GetStockObject)(DEFAULT_GUI_FONT);
    REQUIRE(after);
    EXPECT_EQ(U(GetObjectType)(after), OBJ_FONT);
    LOGFONTW lf; memset(&lf, 0, sizeof lf);
    EXPECT_EQ(U(GetObjectW)(after, sizeof lf, &lf), (int)sizeof lf);
    CHECK(lf.lfFaceName[0] != 0);
    CHECK(lf.lfHeight != 0);
    t_note("DEFAULT_GUI_FONT handle %s after Ensure, height %ld", before == after ? "unchanged" : "changed", lf.lfHeight);
}
TEST(gap7_ensuredpifont_stock_fonts_all_valid) {
    NtGdiEnsureDpiDepDefaultGuiFontForPlateau_pfn f = G7N(NtGdiEnsureDpiDepDefaultGuiFontForPlateau); REQUIRE(f);
    f();
    static const int fonts[] = { OEM_FIXED_FONT, ANSI_FIXED_FONT, ANSI_VAR_FONT, SYSTEM_FONT, DEVICE_DEFAULT_FONT, SYSTEM_FIXED_FONT, DEFAULT_GUI_FONT };
    for (unsigned i = 0; i < sizeof fonts / sizeof *fonts; i++) {
        HGDIOBJ h = U(GetStockObject)(fonts[i]);
        CHECKF(h != NULL && U(GetObjectType)(h) == OBJ_FONT, "stock font %d", fonts[i]);
    }
}

/* ---- NtUserSelectPalette ------------------------------------------------------------------------------------- */
TEST(gap7_selectpalette_returns_previous_palette) {
    NtUserSelectPalette_pfn f = G7N(NtUserSelectPalette); REQUIRE(f);
    struct g7_dib d; REQUIRE(g7_dib_open(&d, 4, 4));
    HPALETTE pal = pal4(); REQUIRE(pal);
    HPALETTE def = (HPALETTE)U(GetStockObject)(DEFAULT_PALETTE);
    EXPECT_EQ(f(d.dc, pal, FALSE), def);          /* a fresh DC has the default palette selected */
    EXPECT_EQ(U(GetCurrentObject)(d.dc, OBJ_PAL), pal);
    EXPECT_EQ(f(d.dc, def, TRUE), pal);
    EXPECT_EQ(U(GetCurrentObject)(d.dc, OBJ_PAL), def);
    g7_dib_close(&d);
    U(DeleteObject)(pal);
}
TEST(gap7_selectpalette_invalid_dc_returns_null) {
    NtUserSelectPalette_pfn f = G7N(NtUserSelectPalette); REQUIRE(f);
    HPALETTE pal = pal4(); REQUIRE(pal);
    EXPECT_EQ(f(NULL, pal, FALSE), NULL);
    EXPECT_EQ(f(bogus_dc(), pal, FALSE), NULL);
    U(DeleteObject)(pal);
}
TEST(gap7_selectpalette_invalid_palette_returns_null_and_keeps_selection) {
    NtUserSelectPalette_pfn f = G7N(NtUserSelectPalette); REQUIRE(f);
    struct g7_dib d; REQUIRE(g7_dib_open(&d, 4, 4));
    HPALETTE pal = pal4(); REQUIRE(pal);
    f(d.dc, pal, FALSE);
    EXPECT_EQ(f(d.dc, NULL, FALSE), NULL);
    EXPECT_EQ(f(d.dc, (HPALETTE)(ULONG_PTR)0x1234, FALSE), NULL);
    HBRUSH wrongtype = (HBRUSH)U(GetStockObject)(BLACK_BRUSH);
    EXPECT_EQ(f(d.dc, (HPALETTE)wrongtype, FALSE), NULL);       /* an HBRUSH is not an HPALETTE */
    EXPECT_EQ(U(GetCurrentObject)(d.dc, OBJ_PAL), pal);
    f(d.dc, (HPALETTE)U(GetStockObject)(DEFAULT_PALETTE), FALSE);
    g7_dib_close(&d);
    U(DeleteObject)(pal);
}
TEST(gap7_selectpalette_route_matches_direct) {
    NtUserSelectPalette_pfn f = G7N(NtUserSelectPalette); REQUIRE(f);
    struct g7_dib d; REQUIRE(g7_dib_open(&d, 4, 4));
    HPALETTE p1 = pal4(), p2 = pal4(); REQUIRE(p1 && p2);
    HPALETTE def = (HPALETTE)U(GetStockObject)(DEFAULT_PALETTE);
    EXPECT_EQ(U(SelectPalette)(d.dc, p1, FALSE), def);
    EXPECT_EQ(f(d.dc, p2, FALSE), p1);
    EXPECT_EQ(U(SelectPalette)(d.dc, def, FALSE), p2);
    g7_dib_close(&d);
    U(DeleteObject)(p1); U(DeleteObject)(p2);
}
TEST(gap7_selectpalette_palette_is_gdi_object_type_pal) {
    HPALETTE pal = pal4(); REQUIRE(pal);
    EXPECT_EQ(U(GetObjectType)(pal), OBJ_PAL);
    EXPECT_EQ(U(GetObjectType)(U(GetStockObject)(DEFAULT_PALETTE)), OBJ_PAL);
    U(DeleteObject)(pal);
}
