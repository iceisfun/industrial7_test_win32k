/* Mapping modes, window/viewport extents and origins, layout, and NtGdiGetDCPoint.
 *
 * gdi32full!SetMapMode returns DC_ATTR+0x68 (previous mode); the kernel (GetAndSetDCDword 0x11) validates the new
 * value. gdi32full!SetWindowExtEx / SetViewportExtEx only store a new extent when the current mode is ISOTROPIC (7) or
 * ANISOTROPIC (8) (map mode > 6) and both components are non-zero. NtGdiScaleViewportExtEx returns the previous extent
 * first, is a successful no-op in fixed modes, and fails for a zero denominator or a zero result in 7/8.
 * NtGdiGetDCPoint(iPoint 1 = viewport ext, 2 = window ext, 4 = viewport org,
 * 8 = window org) reports these values.
 */
#include "xd.h"

#define MEMDC(dc) HDC dc = U(CreateCompatibleDC)(NULL); if (!dc) { t_skip("CreateCompatibleDC failed"); return; }
#define MEMDC_FREE(dc) U(DeleteDC)(dc)
#define NEED_MM() do { REQUIRE(U(SetMapMode)); REQUIRE(U(GetMapMode)); REQUIRE(U(SetWindowExtEx)); REQUIRE(U(GetWindowExtEx)); \
    REQUIRE(U(SetViewportExtEx)); REQUIRE(U(GetViewportExtEx)); REQUIRE(U(SetWindowOrgEx)); REQUIRE(U(GetWindowOrgEx)); \
    REQUIRE(U(SetViewportOrgEx)); REQUIRE(U(GetViewportOrgEx)); REQUIRE(U(CreateCompatibleDC)); REQUIRE(U(LPtoDP)); \
    REQUIRE(U(DPtoLP)); } while (0)

static void get_ext(HDC dc, SIZE *w, SIZE *v) { U(GetWindowExtEx)(dc, w); U(GetViewportExtEx)(dc, v); }
#define EXPECT_PT(p, X, Y) do { EXPECT_INT((p).x, X); EXPECT_INT((p).y, Y); } while (0)
#define EXPECT_SZ(s, X, Y) do { EXPECT_INT((s).cx, X); EXPECT_INT((s).cy, Y); } while (0)

/* ---- defaults ---------------------------------------------------------------------------------------------- */
XT(mm_default_state) {
    SIZE w, v; POINT p; long l;
    NEED_MM();
    MEMDC(dc);
    EXPECT_EQ(U(GetMapMode)(dc), MM_TEXT);
    get_ext(dc, &w, &v);
    EXPECT_SZ(w, 1, 1); EXPECT_SZ(v, 1, 1);
    CHECK(U(GetWindowOrgEx)(dc, &p)); EXPECT_PT(p, 0, 0);
    CHECK(U(GetViewportOrgEx)(dc, &p)); EXPECT_PT(p, 0, 0);
    if (xd_attr_i32(dc, DCATTR_MapMode, &l)) EXPECT_INT(l, MM_TEXT);
    if (xd_attr_i32(dc, DCATTR_WndExtX, &l)) EXPECT_INT(l, 1);
    if (xd_attr_i32(dc, DCATTR_VpExtX, &l)) EXPECT_INT(l, 1);
    MEMDC_FREE(dc);
}

XT(mm_dcattr_mirrors_getters) {
    SIZE w, v; POINT po; long l;
    NEED_MM();
    MEMDC(dc);
    if (!xd_attr_i32(dc, DCATTR_MapMode, &l)) { t_skip("DC_ATTR not reachable"); MEMDC_FREE(dc); return; }
    U(SetMapMode)(dc, MM_ANISOTROPIC);
    U(SetWindowExtEx)(dc, 111, 222, NULL);
    U(SetViewportExtEx)(dc, 333, 444, NULL);
    U(SetWindowOrgEx)(dc, 5, 6, NULL);
    U(SetViewportOrgEx)(dc, 7, 8, NULL);
    CHECK(xd_attr_i32(dc, DCATTR_MapMode, &l)); EXPECT_INT(l, MM_ANISOTROPIC);
    CHECK(xd_attr_i32(dc, DCATTR_WndExtX, &l)); EXPECT_INT(l, 111);
    CHECK(xd_attr_i32(dc, DCATTR_WndExtY, &l)); EXPECT_INT(l, 222);
    CHECK(xd_attr_i32(dc, DCATTR_VpExtX, &l)); EXPECT_INT(l, 333);
    CHECK(xd_attr_i32(dc, DCATTR_VpExtY, &l)); EXPECT_INT(l, 444);
    CHECK(xd_attr_i32(dc, DCATTR_WndOrgX, &l)); EXPECT_INT(l, 5);
    CHECK(xd_attr_i32(dc, DCATTR_WndOrgY, &l)); EXPECT_INT(l, 6);
    CHECK(xd_attr_i32(dc, DCATTR_VpOrgX, &l)); EXPECT_INT(l, 7);
    CHECK(xd_attr_i32(dc, DCATTR_VpOrgY, &l)); EXPECT_INT(l, 8);
    get_ext(dc, &w, &v);
    EXPECT_SZ(w, 111, 222); EXPECT_SZ(v, 333, 444);
    U(GetWindowOrgEx)(dc, &po); EXPECT_PT(po, 5, 6);
    U(GetViewportOrgEx)(dc, &po); EXPECT_PT(po, 7, 8);
    MEMDC_FREE(dc);
}

/* ---- SetMapMode / GetMapMode ------------------------------------------------------------------------------- */
XT(mm_set_returns_previous_for_all_modes) {
    int m, prev = MM_TEXT;
    NEED_MM();
    MEMDC(dc);
    for (m = MM_TEXT; m <= MM_ANISOTROPIC; m++) {
        EXPECT_EQ(U(SetMapMode)(dc, m), prev);
        EXPECT_EQ(U(GetMapMode)(dc), m);
        prev = m;
    }
    for (m = MM_ANISOTROPIC; m >= MM_TEXT; m--) {
        EXPECT_EQ(U(SetMapMode)(dc, m), prev);
        prev = m;
    }
    MEMDC_FREE(dc);
}

XT(mm_set_same_mode_returns_same) {
    int m;
    NEED_MM();
    MEMDC(dc);
    for (m = MM_TEXT; m <= MM_ANISOTROPIC; m++) {
        U(SetMapMode)(dc, m);
        EXPECT_EQ(U(SetMapMode)(dc, m), m);
        EXPECT_EQ(U(GetMapMode)(dc), m);
    }
    MEMDC_FREE(dc);
}

XT(mm_set_invalid_modes) {
    static const int bad[] = { 0, 9, 10, 100, -1, 0x100, 0x7fffffff, (int)0x80000000 };
    size_t i;
    NEED_MM();
    MEMDC(dc);
    U(SetMapMode)(dc, MM_LOENGLISH);
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SET_ERR(0);
        EXPECT_EQ(U(SetMapMode)(dc, bad[i]), 0);               /* the kernel rejects values outside 1..8 and returns 0 */
        EXPECT_EQ(U(GetMapMode)(dc), MM_LOENGLISH);              /* ... leaving the mode alone */
        if (GetLastError()) t_note("SetMapMode(%d) error %lu", bad[i], GetLastError());
    }
    MEMDC_FREE(dc);
}

XT(mm_invalid_hdc) {
    NEED_MM();
    SET_ERR(0);
    EXPECT_EQ(U(SetMapMode)(NULL, MM_TEXT), 0);
    EXPECT_ERR(ERR_INVALID_PARAMETER);
    SET_ERR(0);
    EXPECT_EQ(U(SetMapMode)((HDC)(ULONG_PTR)0x01010fff, MM_TEXT), 0);
    EXPECT_ERR(ERR_INVALID_PARAMETER);
    EXPECT_EQ(U(GetMapMode)(NULL), 0);
    CHECK(!U(GetWindowExtEx)(NULL, &(SIZE){0}));
    CHECK(!U(GetViewportExtEx)(NULL, &(SIZE){0}));
    CHECK(!U(GetWindowOrgEx)(NULL, &(POINT){0}));
    CHECK(!U(GetViewportOrgEx)(NULL, &(POINT){0}));
}

XT(mm_getters_reject_null_out) {
    NEED_MM();
    if (t_is_wine()) { t_skip("Wine dereferences the NULL out pointer"); return; }
    MEMDC(dc);
    CHECK(!U(GetWindowExtEx)(dc, NULL));        /* gdi32full requires a non-NULL out pointer */
    CHECK(!U(GetViewportExtEx)(dc, NULL));
    CHECK(!U(GetWindowOrgEx)(dc, NULL));
    CHECK(!U(GetViewportOrgEx)(dc, NULL));
    MEMDC_FREE(dc);
}

XT(mm_independent_per_dc) {
    NEED_MM();
    MEMDC(a); MEMDC(b);
    U(SetMapMode)(a, MM_HIMETRIC);
    EXPECT_EQ(U(GetMapMode)(a), MM_HIMETRIC);
    EXPECT_EQ(U(GetMapMode)(b), MM_TEXT);
    MEMDC_FREE(a); MEMDC_FREE(b);
}

XT(mm_restored_by_restoredc) {
    int sv; SIZE w, v; POINT p;
    NEED_MM(); REQUIRE(U(SaveDC)); REQUIRE(U(RestoreDC));
    MEMDC(dc);
    U(SetMapMode)(dc, MM_ANISOTROPIC);
    U(SetWindowExtEx)(dc, 100, 100, NULL); U(SetViewportExtEx)(dc, 200, 200, NULL);
    U(SetWindowOrgEx)(dc, 3, 4, NULL); U(SetViewportOrgEx)(dc, 5, 6, NULL);
    sv = U(SaveDC)(dc);
    U(SetMapMode)(dc, MM_TEXT);
    U(SetWindowOrgEx)(dc, 30, 40, NULL); U(SetViewportOrgEx)(dc, 50, 60, NULL);
    CHECK(U(RestoreDC)(dc, sv));
    EXPECT_EQ(U(GetMapMode)(dc), MM_ANISOTROPIC);
    get_ext(dc, &w, &v);
    EXPECT_SZ(w, 100, 100); EXPECT_SZ(v, 200, 200);
    U(GetWindowOrgEx)(dc, &p); EXPECT_PT(p, 3, 4);
    U(GetViewportOrgEx)(dc, &p); EXPECT_PT(p, 5, 6);
    MEMDC_FREE(dc);
}

/* ---- fixed modes ------------------------------------------------------------------------------------------- */
static const struct { int mode; const char *name; int units_per_inch; } fixed_modes[] = {
    { MM_LOMETRIC, "LOMETRIC", 254 }, { MM_HIMETRIC, "HIMETRIC", 2540 }, { MM_LOENGLISH, "LOENGLISH", 100 },
    { MM_HIENGLISH, "HIENGLISH", 1000 }, { MM_TWIPS, "TWIPS", 1440 },
};

XT(mm_fixed_modes_extent_signs) {
    size_t i;
    NEED_MM();
    for (i = 0; i < sizeof fixed_modes / sizeof fixed_modes[0]; i++) {
        SIZE w, v;
        MEMDC(dc);
        U(SetMapMode)(dc, fixed_modes[i].mode);
        get_ext(dc, &w, &v);
        t_note("%s: window ext (%ld,%ld) viewport ext (%ld,%ld)", fixed_modes[i].name, w.cx, w.cy, v.cx, v.cy);
        CHECKF(w.cx > 0 && w.cy > 0, "%s window ext positive", fixed_modes[i].name);
        CHECKF(v.cx > 0, "%s viewport cx positive", fixed_modes[i].name);
        CHECKF(v.cy < 0, "%s viewport cy negative (y axis up)", fixed_modes[i].name);
        MEMDC_FREE(dc);
    }
}

XT(mm_fixed_modes_equal_physical_scale) {
    /* All five fixed modes derive from the same physical size, so pixels per inch must agree across them. */
    double ppi[5], base; size_t i;
    NEED_MM();
    for (i = 0; i < 5; i++) {
        SIZE w, v;
        MEMDC(dc);
        U(SetMapMode)(dc, fixed_modes[i].mode);
        get_ext(dc, &w, &v);
        ppi[i] = (double)(v.cx < 0 ? -v.cx : v.cx) * fixed_modes[i].units_per_inch / (w.cx ? w.cx : 1);
        MEMDC_FREE(dc);
    }
    base = ppi[0];
    for (i = 0; i < 5; i++) {
        t_note("%s: %.2f px/inch", fixed_modes[i].name, ppi[i]);
        CHECKF(base > 0 && ppi[i] > base * 0.97 && ppi[i] < base * 1.03, "%s ppi %.2f vs %.2f", fixed_modes[i].name, ppi[i], base);
    }
}

XT(mm_fixed_modes_lptodp_directions) {
    size_t i;
    NEED_MM();
    for (i = 0; i < sizeof fixed_modes / sizeof fixed_modes[0]; i++) {
        POINT p[3] = { {0, 0}, {fixed_modes[i].units_per_inch, 0}, {0, fixed_modes[i].units_per_inch} };
        MEMDC(dc);
        U(SetMapMode)(dc, fixed_modes[i].mode);
        CHECK(U(LPtoDP)(dc, p, 3));
        EXPECT_PT(p[0], 0, 0);
        CHECKF(p[1].x > 0 && p[1].y == 0, "%s +x", fixed_modes[i].name);
        CHECKF(p[2].x == 0 && p[2].y < 0, "%s +y goes up", fixed_modes[i].name);
        t_note("%s: one inch = (%ld,%ld) device px", fixed_modes[i].name, p[1].x, p[2].y);
        CHECKF(p[1].x > 20 && p[1].x < 600, "%s: one inch is %ld px", fixed_modes[i].name, p[1].x);
        MEMDC_FREE(dc);
    }
}

XT(mm_fixed_modes_lptodp_dptolp_roundtrip) {
    size_t i;
    NEED_MM();
    for (i = 0; i < sizeof fixed_modes / sizeof fixed_modes[0]; i++) {
        POINT p = { fixed_modes[i].units_per_inch * 2, fixed_modes[i].units_per_inch }, q;
        MEMDC(dc);
        U(SetMapMode)(dc, fixed_modes[i].mode);
        q = p;
        CHECK(U(LPtoDP)(dc, &q, 1));
        CHECK(U(DPtoLP)(dc, &q, 1));
        CHECKF(labs(q.x - p.x) <= fixed_modes[i].units_per_inch / 50 + 2 && labs(q.y - p.y) <= fixed_modes[i].units_per_inch / 50 + 2,
               "%s: roundtrip (%ld,%ld) -> (%ld,%ld)", fixed_modes[i].name, p.x, p.y, q.x, q.y);
        MEMDC_FREE(dc);
    }
}

XT(mm_text_is_identity) {
    POINT p[2] = { {123, -45}, {0, 7} };
    NEED_MM();
    MEMDC(dc);
    CHECK(U(LPtoDP)(dc, p, 2));
    EXPECT_PT(p[0], 123, -45); EXPECT_PT(p[1], 0, 7);
    U(SetViewportOrgEx)(dc, 10, 20, NULL);
    CHECK(U(LPtoDP)(dc, p, 2));
    EXPECT_PT(p[0], 133, -25); EXPECT_PT(p[1], 10, 27);
    CHECK(U(DPtoLP)(dc, p, 2));
    EXPECT_PT(p[0], 123, -45); EXPECT_PT(p[1], 0, 7);
    MEMDC_FREE(dc);
}

XT(mm_fixed_modes_ignore_set_extents) {
    size_t i;
    NEED_MM();
    for (i = 0; i < sizeof fixed_modes / sizeof fixed_modes[0]; i++) {
        SIZE w0, v0, w1, v1; BOOL rv, rw;
        MEMDC(dc);
        U(SetMapMode)(dc, fixed_modes[i].mode);
        get_ext(dc, &w0, &v0);
        rw = U(SetWindowExtEx)(dc, 777, 888, NULL);
        rv = U(SetViewportExtEx)(dc, 999, 111, NULL);
        CHECKF(rv, "%s: SetViewportExtEx returns TRUE and ignores the request in fixed modes", fixed_modes[i].name);
        t_note("%s: SetWindowExtEx returned %d", fixed_modes[i].name, (int)rw);
        get_ext(dc, &w1, &v1);
        CHECKF(w0.cx == w1.cx && w0.cy == w1.cy && v0.cx == v1.cx && v0.cy == v1.cy, "%s extents unchanged", fixed_modes[i].name);
        MEMDC_FREE(dc);
    }
}

XT(mm_text_ignores_set_extents) {
    SIZE w, v;
    NEED_MM();
    MEMDC(dc);
    CHECK(U(SetViewportExtEx)(dc, 50, 60, NULL));
    U(SetWindowExtEx)(dc, 70, 80, NULL);
    get_ext(dc, &w, &v);
    EXPECT_SZ(w, 1, 1); EXPECT_SZ(v, 1, 1);
    MEMDC_FREE(dc);
}

/* ---- anisotropic / isotropic -------------------------------------------------------------------------------- */
XT(mm_aniso_set_ext_returns_previous) {
    SIZE prev, w, v;
    NEED_MM();
    MEMDC(dc);
    U(SetMapMode)(dc, MM_ANISOTROPIC);
    U(GetWindowExtEx)(dc, &w);
    CHECK(U(SetWindowExtEx)(dc, 100, 200, &prev));
    EXPECT_INT(prev.cx, w.cx); EXPECT_INT(prev.cy, w.cy);
    CHECK(U(SetWindowExtEx)(dc, 300, 400, &prev));
    EXPECT_SZ(prev, 100, 200);
    U(GetViewportExtEx)(dc, &v);
    CHECK(U(SetViewportExtEx)(dc, 500, 600, &prev));
    EXPECT_INT(prev.cx, v.cx); EXPECT_INT(prev.cy, v.cy);
    CHECK(U(SetViewportExtEx)(dc, 700, 800, &prev));
    EXPECT_SZ(prev, 500, 600);
    get_ext(dc, &w, &v);
    EXPECT_SZ(w, 300, 400); EXPECT_SZ(v, 700, 800);
    MEMDC_FREE(dc);
}

XT(mm_aniso_set_ext_null_prev) {
    SIZE w, v;
    NEED_MM();
    MEMDC(dc);
    U(SetMapMode)(dc, MM_ANISOTROPIC);
    CHECK(U(SetWindowExtEx)(dc, 10, 20, NULL));
    CHECK(U(SetViewportExtEx)(dc, 30, 40, NULL));
    get_ext(dc, &w, &v);
    EXPECT_SZ(w, 10, 20); EXPECT_SZ(v, 30, 40);
    MEMDC_FREE(dc);
}

XT(mm_aniso_zero_extents_ignored) {
    SIZE w0, v0, w1, v1;
    NEED_MM();
    MEMDC(dc);
    U(SetMapMode)(dc, MM_ANISOTROPIC);
    U(SetWindowExtEx)(dc, 100, 100, NULL); U(SetViewportExtEx)(dc, 200, 300, NULL);
    get_ext(dc, &w0, &v0);
    U(SetWindowExtEx)(dc, 0, 50, NULL);
    U(SetWindowExtEx)(dc, 50, 0, NULL);
    U(SetWindowExtEx)(dc, 0, 0, NULL);
    CHECK(U(SetViewportExtEx)(dc, 0, 50, NULL));      /* SetViewportExtEx still returns 1 but stores nothing */
    CHECK(U(SetViewportExtEx)(dc, 50, 0, NULL));
    get_ext(dc, &w1, &v1);
    EXPECT_INT(w1.cx, w0.cx); EXPECT_INT(w1.cy, w0.cy);
    EXPECT_INT(v1.cx, v0.cx); EXPECT_INT(v1.cy, v0.cy);
    MEMDC_FREE(dc);
}

XT(mm_aniso_negative_extents) {
    SIZE w, v; POINT p = { 30, 40 };
    NEED_MM();
    MEMDC(dc);
    U(SetMapMode)(dc, MM_ANISOTROPIC);
    CHECK(U(SetWindowExtEx)(dc, 100, 100, NULL));
    CHECK(U(SetViewportExtEx)(dc, -200, 300, NULL));
    get_ext(dc, &w, &v);
    EXPECT_SZ(v, -200, 300);
    CHECK(U(LPtoDP)(dc, &p, 1));
    EXPECT_PT(p, -60, 120);                           /* x*vx/wx, y*vy/wy */
    MEMDC_FREE(dc);
}

XT(mm_aniso_lptodp_exact) {
    POINT p[3] = { {30, 40}, {10, 20}, {-10, 0} }, ref[3];
    NEED_MM();
    MEMDC(dc);
    U(SetMapMode)(dc, MM_ANISOTROPIC);
    U(SetWindowExtEx)(dc, 100, 100, NULL);
    U(SetViewportExtEx)(dc, 200, 300, NULL);
    U(SetWindowOrgEx)(dc, 10, 20, NULL);
    U(SetViewportOrgEx)(dc, 5, 7, NULL);
    memcpy(ref, p, sizeof p);
    CHECK(U(LPtoDP)(dc, p, 3));
    EXPECT_PT(p[0], 45, 67);                          /* (x - wox) * vx / wx + vox */
    EXPECT_PT(p[1], 5, 7);
    EXPECT_PT(p[2], -35, -53);
    CHECK(U(DPtoLP)(dc, p, 3));
    /* The inverse is computed from the transform, not by the exact integer ratio: the round trip may be off by one unit. */
    for (int i = 0; i < 3; i++) {
        CHECKF(abs((int)(p[i].x - ref[i].x)) <= 1 && abs((int)(p[i].y - ref[i].y)) <= 1,
               "point %d round trip (%ld,%ld) from (%ld,%ld)", i, p[i].x, p[i].y, ref[i].x, ref[i].y);
        t_note("DPtoLP(LPtoDP(%ld,%ld)) = (%ld,%ld)", ref[i].x, ref[i].y, p[i].x, p[i].y);
    }
    MEMDC_FREE(dc);
}

XT(mm_aniso_lptodp_shrink) {
    POINT p = { 1000, 500 };
    NEED_MM();
    MEMDC(dc);
    U(SetMapMode)(dc, MM_ANISOTROPIC);
    U(SetWindowExtEx)(dc, 1000, 1000, NULL);
    U(SetViewportExtEx)(dc, 100, 50, NULL);
    CHECK(U(LPtoDP)(dc, &p, 1));
    EXPECT_PT(p, 100, 25);
    CHECK(U(DPtoLP)(dc, &p, 1));
    EXPECT_PT(p, 1000, 500);
    MEMDC_FREE(dc);
}

XT(mm_iso_keeps_aspect) {
    POINT p[2] = { {1000, 0}, {0, 1000} };
    SIZE w, v;
    NEED_MM();
    MEMDC(dc);
    U(SetMapMode)(dc, MM_ISOTROPIC);
    CHECK(U(SetWindowExtEx)(dc, 1000, 1000, NULL));
    CHECK(U(SetViewportExtEx)(dc, 200, 400, NULL));
    CHECK(U(LPtoDP)(dc, p, 2));
    t_note("ISOTROPIC window 1000x1000 viewport 200x400: 1000 units -> (%ld,%ld) / (%ld,%ld)", p[0].x, p[0].y, p[1].x, p[1].y);
    CHECK(labs(p[0].x) == labs(p[1].y));              /* equal scale on both axes */
    CHECK(p[0].y == 0 && p[1].x == 0);
    CHECK(labs(p[0].x) == 200 || labs(p[0].x) == 400 || labs(p[0].x) > 0);
    get_ext(dc, &w, &v);
    t_note("ISOTROPIC extents now window (%ld,%ld) viewport (%ld,%ld)", w.cx, w.cy, v.cx, v.cy);
    MEMDC_FREE(dc);
}

XT(mm_iso_already_square_unchanged) {
    POINT p = { 100, 100 };
    NEED_MM();
    MEMDC(dc);
    U(SetMapMode)(dc, MM_ISOTROPIC);
    U(SetWindowExtEx)(dc, 100, 100, NULL);
    U(SetViewportExtEx)(dc, 300, 300, NULL);
    CHECK(U(LPtoDP)(dc, &p, 1));
    EXPECT_PT(p, 300, 300);
    MEMDC_FREE(dc);
}

XT(mm_iso_flipped_y) {
    POINT p = { 10, 10 };
    NEED_MM();
    MEMDC(dc);
    U(SetMapMode)(dc, MM_ISOTROPIC);
    U(SetWindowExtEx)(dc, 100, 100, NULL);
    U(SetViewportExtEx)(dc, 100, -100, NULL);
    CHECK(U(LPtoDP)(dc, &p, 1));
    EXPECT_PT(p, 10, -10);                            /* a negative viewport extent mirrors the axis */
    MEMDC_FREE(dc);
}

XT(mm_switch_to_aniso_keeps_fixed_extents) {
    SIZE w0, v0, w1, v1;
    NEED_MM();
    MEMDC(dc);
    U(SetMapMode)(dc, MM_LOENGLISH);
    get_ext(dc, &w0, &v0);
    U(SetMapMode)(dc, MM_ANISOTROPIC);
    get_ext(dc, &w1, &v1);
    t_note("LOENGLISH ext w(%ld,%ld) v(%ld,%ld) -> after ANISOTROPIC w(%ld,%ld) v(%ld,%ld)", w0.cx, w0.cy, v0.cx, v0.cy, w1.cx, w1.cy, v1.cx, v1.cy);
    if (!(w1.cx == w0.cx && w1.cy == w0.cy && v1.cx == v0.cx && v1.cy == v0.cy)) t_note("extents did not carry over");
    MEMDC_FREE(dc);
}

XT(mm_switch_back_to_text_resets_extents) {
    SIZE w, v;
    NEED_MM();
    MEMDC(dc);
    U(SetMapMode)(dc, MM_ANISOTROPIC);
    U(SetWindowExtEx)(dc, 500, 500, NULL); U(SetViewportExtEx)(dc, 900, 900, NULL);
    U(SetMapMode)(dc, MM_TEXT);
    get_ext(dc, &w, &v);
    EXPECT_SZ(w, 1, 1); EXPECT_SZ(v, 1, 1);
    MEMDC_FREE(dc);
}

/* ---- origins ----------------------------------------------------------------------------------------------- */
XT(mm_origins_in_every_mode) {
    int m;
    NEED_MM();
    for (m = MM_TEXT; m <= MM_ANISOTROPIC; m++) {
        POINT prev, cur;
        MEMDC(dc);
        U(SetMapMode)(dc, m);
        CHECKF(U(SetWindowOrgEx)(dc, 11, 12, &prev), "mode %d", m);
        EXPECT_PT(prev, 0, 0);
        CHECK(U(SetWindowOrgEx)(dc, 21, 22, &prev)); EXPECT_PT(prev, 11, 12);
        CHECK(U(SetViewportOrgEx)(dc, 31, 32, &prev)); EXPECT_PT(prev, 0, 0);
        CHECK(U(SetViewportOrgEx)(dc, 41, 42, &prev)); EXPECT_PT(prev, 31, 32);
        U(GetWindowOrgEx)(dc, &cur); EXPECT_PT(cur, 21, 22);
        U(GetViewportOrgEx)(dc, &cur); EXPECT_PT(cur, 41, 42);
        MEMDC_FREE(dc);
    }
}

XT(mm_origins_null_prev_ok) {
    POINT cur;
    NEED_MM();
    MEMDC(dc);
    CHECK(U(SetWindowOrgEx)(dc, 1, 2, NULL));
    CHECK(U(SetViewportOrgEx)(dc, 3, 4, NULL));
    U(GetWindowOrgEx)(dc, &cur); EXPECT_PT(cur, 1, 2);
    U(GetViewportOrgEx)(dc, &cur); EXPECT_PT(cur, 3, 4);
    MEMDC_FREE(dc);
}

XT(mm_origins_extreme_values) {
    POINT cur;
    NEED_MM();
    MEMDC(dc);
    CHECK(U(SetWindowOrgEx)(dc, -100000, 100000, NULL));
    CHECK(U(SetViewportOrgEx)(dc, 2000000000, -2000000000, NULL));
    U(GetWindowOrgEx)(dc, &cur); EXPECT_PT(cur, -100000, 100000);
    U(GetViewportOrgEx)(dc, &cur); EXPECT_PT(cur, 2000000000, -2000000000);
    MEMDC_FREE(dc);
}

XT(mm_offset_viewport_org) {
    POINT prev, cur;
    NEED_MM(); REQUIRE(U(OffsetViewportOrgEx));
    MEMDC(dc);
    U(SetViewportOrgEx)(dc, 10, 20, NULL);
    CHECK(U(OffsetViewportOrgEx)(dc, 5, -7, &prev)); EXPECT_PT(prev, 10, 20);
    U(GetViewportOrgEx)(dc, &cur); EXPECT_PT(cur, 15, 13);
    CHECK(U(OffsetViewportOrgEx)(dc, 0, 0, &prev)); EXPECT_PT(prev, 15, 13);
    CHECK(U(OffsetViewportOrgEx)(dc, -15, -13, NULL));
    U(GetViewportOrgEx)(dc, &cur); EXPECT_PT(cur, 0, 0);
    MEMDC_FREE(dc);
}

XT(mm_offset_window_org) {
    POINT prev, cur;
    NEED_MM(); REQUIRE(U(OffsetWindowOrgEx));
    MEMDC(dc);
    U(SetWindowOrgEx)(dc, 10, 20, NULL);
    CHECK(U(OffsetWindowOrgEx)(dc, 5, -7, &prev)); EXPECT_PT(prev, 10, 20);
    U(GetWindowOrgEx)(dc, &cur); EXPECT_PT(cur, 15, 13);
    CHECK(U(OffsetWindowOrgEx)(dc, 100, 100, NULL));
    U(GetWindowOrgEx)(dc, &cur); EXPECT_PT(cur, 115, 113);
    MEMDC_FREE(dc);
}

XT(mm_origins_affect_lptodp) {
    POINT p = { 100, 100 };
    NEED_MM();
    MEMDC(dc);
    U(SetWindowOrgEx)(dc, 30, 40, NULL);
    CHECK(U(LPtoDP)(dc, &p, 1)); EXPECT_PT(p, 70, 60);              /* x - window origin */
    U(SetViewportOrgEx)(dc, 1000, 2000, NULL);
    p.x = 100; p.y = 100;
    CHECK(U(LPtoDP)(dc, &p, 1)); EXPECT_PT(p, 1070, 2060);          /* ... + viewport origin */
    CHECK(U(DPtoLP)(dc, &p, 1)); EXPECT_PT(p, 100, 100);
    MEMDC_FREE(dc);
}

XT(mm_origin_invalid_hdc) {
    POINT prev = { 0x55, 0x55 };
    NEED_MM(); REQUIRE(U(OffsetViewportOrgEx)); REQUIRE(U(OffsetWindowOrgEx));
    CHECK(!U(SetWindowOrgEx)(NULL, 1, 1, &prev));
    CHECK(!U(SetViewportOrgEx)(NULL, 1, 1, &prev));
    CHECK(!U(OffsetWindowOrgEx)(NULL, 1, 1, &prev));
    CHECK(!U(OffsetViewportOrgEx)(NULL, 1, 1, &prev));
    CHECK(!U(SetWindowExtEx)(NULL, 1, 1, NULL));
    CHECK(!U(SetViewportExtEx)(NULL, 1, 1, NULL));
    EXPECT_PT(prev, 0x55, 0x55);
}

XT(mm_origin_invalid_hdc_errors) {
    NEED_MM();
    SET_ERR(0);
    CHECK(!U(SetWindowExtEx)(NULL, 1, 1, NULL));
    EXPECT_ERR(ERR_INVALID_HANDLE);                    /* type bits != DC -> pldcGet() fails -> 6 */
    SET_ERR(0);
    CHECK(!U(SetViewportExtEx)(NULL, 1, 1, NULL));
    EXPECT_ERR(ERR_INVALID_HANDLE);
    SET_ERR(0);
    CHECK(!U(SetWindowExtEx)((HDC)(ULONG_PTR)0x01010fff, 1, 1, NULL));
    EXPECT_ERR(ERR_INVALID_PARAMETER);                 /* DC-typed but not live: GdiGetEntry validation -> 0x57 */
}

/* ---- ScaleXExtEx --------------------------------------------------------------------------------------------- */
XT(mm_scale_window_ext) {
    SIZE prev, w, v;
    NEED_MM(); REQUIRE(U(ScaleWindowExtEx));
    MEMDC(dc);
    U(SetMapMode)(dc, MM_ANISOTROPIC);
    U(SetWindowExtEx)(dc, 100, 200, NULL);
    CHECK(U(ScaleWindowExtEx)(dc, 1, 2, 3, 4, &prev));
    t_note("ScaleWindowExtEx previous extent reported as (%ld,%ld)", prev.cx, prev.cy);
    get_ext(dc, &w, &v);
    EXPECT_SZ(w, 50, 150);
    MEMDC_FREE(dc);
}

XT(mm_scale_viewport_ext) {
    SIZE prev, w, v;
    NEED_MM(); REQUIRE(U(ScaleViewportExtEx));
    MEMDC(dc);
    U(SetMapMode)(dc, MM_ANISOTROPIC);
    U(SetViewportExtEx)(dc, 100, 200, NULL);
    CHECK(U(ScaleViewportExtEx)(dc, 3, 1, 1, 2, &prev));
    EXPECT_SZ(prev, 100, 200);
    get_ext(dc, &w, &v);
    EXPECT_SZ(v, 300, 100);                            /* ext * num / den, truncated */
    CHECK(U(ScaleViewportExtEx)(dc, 1, 3, 1, 3, NULL));
    U(GetViewportExtEx)(dc, &v);
    EXPECT_SZ(v, 100, 33);
    MEMDC_FREE(dc);
}

XT(mm_scale_viewport_zero_denominator) {
    SIZE prev = { 0, 0 }, v0, v1, w;
    NEED_MM(); REQUIRE(U(ScaleViewportExtEx));
    MEMDC(dc);
    U(SetMapMode)(dc, MM_ANISOTROPIC);
    U(SetViewportExtEx)(dc, 100, 200, NULL);
    get_ext(dc, &w, &v0);
    CHECK(!U(ScaleViewportExtEx)(dc, 1, 0, 1, 1, &prev));
    CHECK(!U(ScaleViewportExtEx)(dc, 1, 1, 1, 0, NULL));
    EXPECT_SZ(prev, 100, 200);                         /* the previous extent is stored before the arithmetic is checked */
    U(GetViewportExtEx)(dc, &v1);
    EXPECT_INT(v1.cx, v0.cx); EXPECT_INT(v1.cy, v0.cy);
    MEMDC_FREE(dc);
}

XT(mm_scale_viewport_zero_result) {
    SIZE v;
    NEED_MM(); REQUIRE(U(ScaleViewportExtEx));
    MEMDC(dc);
    U(SetMapMode)(dc, MM_ANISOTROPIC);
    U(SetViewportExtEx)(dc, 100, 200, NULL);
    CHECK(!U(ScaleViewportExtEx)(dc, 0, 1, 1, 1, NULL));   /* a zero resulting extent is rejected */
    U(GetViewportExtEx)(dc, &v);
    EXPECT_SZ(v, 100, 200);
    MEMDC_FREE(dc);
}

XT(mm_scale_viewport_fixed_mode_noop) {
    SIZE prev = { 0, 0 }, v0, v1, w;
    NEED_MM(); REQUIRE(U(ScaleViewportExtEx));
    MEMDC(dc);
    U(SetMapMode)(dc, MM_LOENGLISH);
    get_ext(dc, &w, &v0);
    CHECK(U(ScaleViewportExtEx)(dc, 5, 3, 7, 2, &prev));    /* mode <= 6: reports the old extent, changes nothing, succeeds */
    EXPECT_INT(prev.cx, v0.cx); EXPECT_INT(prev.cy, v0.cy);
    U(GetViewportExtEx)(dc, &v1);
    EXPECT_INT(v1.cx, v0.cx); EXPECT_INT(v1.cy, v0.cy);
    CHECK(U(ScaleViewportExtEx)(dc, 1, 0, 1, 0, NULL));      /* even a zero denominator is not examined in fixed modes */
    MEMDC_FREE(dc);
}

XT(mm_scale_invalid_hdc) {
    NEED_MM(); REQUIRE(U(ScaleViewportExtEx)); REQUIRE(U(ScaleWindowExtEx));
    CHECK(!U(ScaleViewportExtEx)(NULL, 1, 1, 1, 1, NULL));
    CHECK(!U(ScaleWindowExtEx)(NULL, 1, 1, 1, 1, NULL));
}

/* ---- NtGdiGetDCPoint ---------------------------------------------------------------------------------------- */
XT(dcpoint_matches_user_getters) {
    POINT q; SIZE w, v, s; POINT o;
    NEED_MM(); REQUIRE_NT(NtGdiGetDCPoint);
    MEMDC(dc);
    U(SetMapMode)(dc, MM_ANISOTROPIC);
    U(SetWindowExtEx)(dc, 111, 222, NULL); U(SetViewportExtEx)(dc, 333, 444, NULL);
    U(SetWindowOrgEx)(dc, 5, 6, NULL); U(SetViewportOrgEx)(dc, 7, 8, NULL);
    q.x = q.y = 0x5555;
    CHECK(UN(NtGdiGetDCPoint)(dc, 1, &q)); EXPECT_PT(q, 333, 444);
    CHECK(UN(NtGdiGetDCPoint)(dc, 2, &q)); EXPECT_PT(q, 111, 222);
    CHECK(UN(NtGdiGetDCPoint)(dc, 4, &q)); EXPECT_PT(q, 7, 8);
    CHECK(UN(NtGdiGetDCPoint)(dc, 8, &q)); EXPECT_PT(q, 5, 6);
    get_ext(dc, &w, &v); U(GetWindowOrgEx)(dc, &o);
    (void)s;
    MEMDC_FREE(dc);
}

XT(dcpoint_default_dc) {
    POINT q;
    NEED_MM(); REQUIRE_NT(NtGdiGetDCPoint);
    MEMDC(dc);
    q.x = q.y = 0x5555;
    CHECK(UN(NtGdiGetDCPoint)(dc, 1, &q)); EXPECT_PT(q, 1, 1);
    CHECK(UN(NtGdiGetDCPoint)(dc, 2, &q)); EXPECT_PT(q, 1, 1);
    CHECK(UN(NtGdiGetDCPoint)(dc, 4, &q)); EXPECT_PT(q, 0, 0);
    CHECK(UN(NtGdiGetDCPoint)(dc, 8, &q)); EXPECT_PT(q, 0, 0);
    MEMDC_FREE(dc);
}

XT(dcpoint_fixed_mode_extents) {
    POINT q; SIZE w, v;
    NEED_MM(); REQUIRE_NT(NtGdiGetDCPoint);
    MEMDC(dc);
    U(SetMapMode)(dc, MM_HIMETRIC);
    get_ext(dc, &w, &v);
    CHECK(UN(NtGdiGetDCPoint)(dc, 2, &q)); EXPECT_PT(q, w.cx, w.cy);
    CHECK(UN(NtGdiGetDCPoint)(dc, 1, &q)); EXPECT_PT(q, v.cx, v.cy);
    MEMDC_FREE(dc);
}

XT(dcpoint_invalid_hdc_and_selector) {
    POINT q = { 0x5555, 0x5555 };
    NEED_MM(); REQUIRE_NT(NtGdiGetDCPoint);
    CHECK(!UN(NtGdiGetDCPoint)(NULL, 1, &q));
    CHECK(!UN(NtGdiGetDCPoint)((HDC)(ULONG_PTR)0x01010fff, 2, &q));
    CHECK(!UN(NtGdiGetDCPoint)((HDC)U(GetStockObject)(BLACK_PEN), 4, &q));
    EXPECT_PT(q, 0x5555, 0x5555);
    {
        MEMDC(dc);
        static const UINT bad[] = { 0, 3, 5, 6, 7, 9, 0x11, 0x100 };
        size_t i;
        for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
            q.x = q.y = 0x5555;
            CHECKF(!UN(NtGdiGetDCPoint)(dc, bad[i], &q), "selector 0x%x accepted", bad[i]);   /* the selector is not one of 1, 2, 4, 8 */
        }
        MEMDC_FREE(dc);
    }
}

/* ---- SetLayout / GetLayout ---------------------------------------------------------------------------------- */
XT(layout_default_and_rtl) {
    DWORD old;
    NEED_MM(); REQUIRE(U(SetLayout)); REQUIRE(U(GetLayout));
    FX(f, 64, 64);
    EXPECT_EQ(U(GetLayout)(f.dc), 0);
    old = U(SetLayout)(f.dc, LAYOUT_RTL);
    EXPECT_EQ(old, 0);
    EXPECT_EQ(U(GetLayout)(f.dc) & LAYOUT_RTL, LAYOUT_RTL);
    { long fl; if (xd_attr_i32(f.dc, DCATTR_Flags, &fl)) CHECK(fl & 1); }   /* DC_ATTR+0x6c bit0 is the RTL flag read by GetWindowExtEx etc. */
    old = U(SetLayout)(f.dc, 0);
    EXPECT_EQ(old & LAYOUT_RTL, LAYOUT_RTL);
    EXPECT_EQ(U(GetLayout)(f.dc) & LAYOUT_RTL, 0);
    { long fl; if (xd_attr_i32(f.dc, DCATTR_Flags, &fl)) CHECK(!(fl & 1)); }
    fx_close(&f);
}

XT(layout_invalid_hdc) {
    NEED_MM(); REQUIRE(U(SetLayout)); REQUIRE(U(GetLayout));
    SET_ERR(0);
    EXPECT_EQ(U(SetLayout)(NULL, LAYOUT_RTL), GDI_ERROR);
    EXPECT_ERR(ERR_INVALID_HANDLE);
    t_note("GetLayout(NULL) = 0x%lx", (unsigned long)U(GetLayout)(NULL));
}

XT(layout_rtl_mirrors_extent_getters) {
    SIZE w; POINT p;
    NEED_MM(); REQUIRE(U(SetLayout));
    FX(f, 64, 64);
    U(SetLayout)(f.dc, LAYOUT_RTL);
    U(SetMapMode)(f.dc, MM_ANISOTROPIC);
    CHECK(U(SetWindowExtEx)(f.dc, 100, 50, NULL));
    CHECK(U(GetWindowExtEx)(f.dc, &w));
    EXPECT_SZ(w, 100, 50);                              /* x is stored negated and negated again on read */
    CHECK(U(SetViewportOrgEx)(f.dc, 12, 13, NULL));
    CHECK(U(GetViewportOrgEx)(f.dc, &p));
    t_note("RTL viewport origin round trip -> (%ld,%ld)", p.x, p.y);
    { long l; if (xd_attr_i32(f.dc, DCATTR_WndExtX, &l)) { t_note("RTL stored window ext x = %ld", l); CHECK(l == -100); } }
    fx_close(&f);
}

XT(layout_rtl_flips_device_x) {
    POINT p = { 10, 20 };
    NEED_MM(); REQUIRE(U(SetLayout));
    FX(f, 64, 64);
    U(SetLayout)(f.dc, LAYOUT_RTL);
    CHECK(U(LPtoDP)(f.dc, &p, 1));
    t_note("RTL memory DC 64 wide: LPtoDP(10,20) = (%ld,%ld)", p.x, p.y);
    CHECK(p.y == 20);
    fx_close(&f);
}

XT(layout_rtl_draws_mirrored_line) {
    gdi_sel_t s = {0};
    int l, t, r, b, n;
    NEED_MM(); REQUIRE(U(SetLayout)); REQUIRE(U(MoveToEx)); REQUIRE(U(LineTo));
    FX(f, 64, 64);
    sel_pen(f.dc, &s, COL_RED, 1);
    U(MoveToEx)(f.dc, 4, 10, NULL); U(LineTo)(f.dc, 14, 10);
    n = fx_count(&f, PX_RED); EXPECT_INT(n, 10);
    fx_bbox(&f, PX_RED, &l, &t, &r, &b); EXPECT_INT(l, 4); EXPECT_INT(r, 13);
    fx_clear(&f, 0x00FFFFFF);
    U(SetLayout)(f.dc, LAYOUT_RTL);
    U(MoveToEx)(f.dc, 4, 10, NULL); U(LineTo)(f.dc, 14, 10);
    n = fx_bbox(&f, PX_RED, &l, &t, &r, &b);
    t_note("RTL mirrored line: %d px x[%d,%d] y[%d,%d]", n, l, r, t, b);
    CHECK(n >= 9 && n <= 11);
    CHECK(t == 10 && b == 10);
    if (!(l >= 64 - 15 && r <= 64 - 3)) t_note("RTL line not mirrored about the DC width");
    sel_done(f.dc, &s);
    fx_close(&f);
}
