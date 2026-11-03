/* gap9_gdi*: NtGdiRectangle, NtGdiLineTo, NtGdiScaleViewportExtEx, NtGdiSetColorAdjustment (win32kfull).
 * Direct syscalls are issued after GdiFlush() so no user-mode batched command is pending, and the effect
 * is read back from the DIB section bits (kernel-written, mapped into this process) and from the DC_ATTR
 * state gdi32 reads (current position).
 *  - Rectangle(l,t,r,b) with a 1px solid pen paints the border on columns l..r-1 / rows t..b-1; the
 *    interior (l+1..r-2 / t+1..b-2) is painted with the selected brush.
 *  - LineTo excludes the end point and leaves the current position at it.
 *  - ScaleViewportExtEx: viewport extent *= num/denom per axis, only when the map mode is > 6
 *    (MM_ISOTROPIC/MM_ANISOTROPIC; fixed modes skip the block and still return TRUE); old extent is stored first; zero denominators/results fail with no change.
 *  - SetColorAdjustment validates every field before applying them in the kernel (ranges in the table below). */
#include "g9.h"

UDECL(BOOL, NtGdiRectangle, (HDC, int, int, int, int));
UDECL(BOOL, NtGdiLineTo, (HDC, int, int));
UDECL(BOOL, NtGdiScaleViewportExtEx, (HDC, int, int, int, int, SIZE *));
UDECL(BOOL, NtGdiSetColorAdjustment, (HDC, const COLORADJUSTMENT *));

#define RED   G9_BGR(255, 0, 0)
#define BLUE  G9_BGR(0, 0, 255)
#define WHITE G9_BGR(255, 255, 255)

static void flush(void) { if (U(GdiFlush)) U(GdiFlush)(); }

struct pen_ctx { HPEN pen, oldpen; HBRUSH brush, oldbrush; };
static void pen_on(struct g9_dib *d, struct pen_ctx *c, COLORREF pen, COLORREF brush, int nullbrush) {
    c->pen = U(CreatePen)(PS_SOLID, 1, pen);
    c->brush = nullbrush ? (HBRUSH)U(GetStockObject)(NULL_BRUSH) : U(CreateSolidBrush)(brush);
    c->oldpen = (HPEN)U(SelectObject)(d->dc, c->pen);
    c->oldbrush = (HBRUSH)U(SelectObject)(d->dc, c->brush);
}
static void pen_off(struct g9_dib *d, struct pen_ctx *c, int nullbrush) {
    U(SelectObject)(d->dc, c->oldpen); U(SelectObject)(d->dc, c->oldbrush);
    U(DeleteObject)(c->pen); if (!nullbrush) U(DeleteObject)(c->brush);
}
static int need_gdi(void) { return U(CreatePen) && U(CreateSolidBrush) && U(GetStockObject) && U(SelectObject) && U(MoveToEx) && U(GetCurrentPositionEx); }
#define NEED() do { if (!need_gdi()) { t_skip("gdi32 missing"); return; } } while (0)

/* ---- NtGdiRectangle ------------------------------------------------------------------------- */
TEST(gap9_gdi_rectangle_border_and_fill) {
    REQUIRE(UN(NtGdiRectangle)); NEED();
    struct g9_dib d; struct pen_ctx c;
    if (!g9_dib_new(&d, 32, 32)) { t_skip("no dib"); return; }
    pen_on(&d, &c, RGB(255, 0, 0), RGB(0, 0, 255), 0);
    flush();
    CHECK(UN(NtGdiRectangle)(d.dc, 4, 6, 12, 14));
    flush();
    for (int x = 4; x < 12; x++) { EXPECT_EQ(g9_px(&d, x, 6), RED); EXPECT_EQ(g9_px(&d, x, 13), RED); }
    for (int y = 6; y < 14; y++) { EXPECT_EQ(g9_px(&d, 4, y), RED); EXPECT_EQ(g9_px(&d, 11, y), RED); }
    for (int y = 7; y < 13; y++) for (int x = 5; x < 11; x++) EXPECT_EQ(g9_px(&d, x, y), BLUE);
    /* right and bottom edges are exclusive; nothing outside */
    EXPECT_EQ(g9_px(&d, 12, 8), WHITE); EXPECT_EQ(g9_px(&d, 8, 14), WHITE);
    EXPECT_EQ(g9_px(&d, 3, 8), WHITE);  EXPECT_EQ(g9_px(&d, 8, 5), WHITE);
    EXPECT_EQ(g9_px(&d, 0, 0), WHITE);  EXPECT_EQ(g9_px(&d, 31, 31), WHITE);
    pen_off(&d, &c, 0); g9_dib_free(&d);
}

TEST(gap9_gdi_rectangle_null_brush_leaves_interior) {
    REQUIRE(UN(NtGdiRectangle)); NEED();
    struct g9_dib d; struct pen_ctx c;
    if (!g9_dib_new(&d, 32, 32)) { t_skip("no dib"); return; }
    pen_on(&d, &c, RGB(255, 0, 0), 0, 1);
    flush();
    CHECK(UN(NtGdiRectangle)(d.dc, 2, 2, 10, 10));
    flush();
    EXPECT_EQ(g9_px(&d, 2, 2), RED); EXPECT_EQ(g9_px(&d, 9, 9), RED);
    EXPECT_EQ(g9_px(&d, 5, 5), WHITE);
    pen_off(&d, &c, 1); g9_dib_free(&d);
}

TEST(gap9_gdi_rectangle_null_pen_and_null_brush_draws_nothing) {
    REQUIRE(UN(NtGdiRectangle)); NEED();
    struct g9_dib d;
    if (!g9_dib_new(&d, 16, 16)) { t_skip("no dib"); return; }
    HGDIOBJ op = U(SelectObject)(d.dc, U(GetStockObject)(NULL_PEN));
    HGDIOBJ ob = U(SelectObject)(d.dc, U(GetStockObject)(NULL_BRUSH));
    flush();
    CHECK(UN(NtGdiRectangle)(d.dc, 1, 1, 12, 12));
    flush();
    int changed = 0; for (int i = 0; i < 256; i++) if ((d.bits[i] & 0xFFFFFF) != 0xFFFFFF) changed++;
    EXPECT_EQ(changed, 0);
    U(SelectObject)(d.dc, op); U(SelectObject)(d.dc, ob); g9_dib_free(&d);
}

TEST(gap9_gdi_rectangle_clipped_by_bitmap) {
    REQUIRE(UN(NtGdiRectangle)); NEED();
    struct g9_dib d; struct pen_ctx c;
    if (!g9_dib_new(&d, 16, 16)) { t_skip("no dib"); return; }
    pen_on(&d, &c, RGB(255, 0, 0), RGB(0, 0, 255), 0);
    flush();
    CHECK(UN(NtGdiRectangle)(d.dc, -10, -10, 8, 8));       /* left/top borders fall outside the surface */
    flush();
    EXPECT_EQ(g9_px(&d, 7, 0), RED);   /* right border column 7 */
    EXPECT_EQ(g9_px(&d, 0, 7), RED);   /* bottom border row 7 */
    EXPECT_EQ(g9_px(&d, 0, 0), BLUE);
    EXPECT_EQ(g9_px(&d, 8, 0), WHITE);
    pen_off(&d, &c, 0); g9_dib_free(&d);
}

TEST(gap9_gdi_rectangle_user_path_matches_syscall) {
    REQUIRE(UN(NtGdiRectangle)); REQUIRE(U(Rectangle)); NEED();
    struct g9_dib a, b; struct pen_ctx ca, cb;
    if (!g9_dib_new(&a, 24, 24) || !g9_dib_new(&b, 24, 24)) { t_skip("no dib"); return; }
    pen_on(&a, &ca, RGB(255, 0, 0), RGB(0, 0, 255), 0);
    pen_on(&b, &cb, RGB(255, 0, 0), RGB(0, 0, 255), 0);
    CHECK(U(Rectangle)(a.dc, 3, 5, 17, 19));              /* may be batched in GdiTebBatch */
    flush();
    CHECK(UN(NtGdiRectangle)(b.dc, 3, 5, 17, 19));
    flush();
    int diff = 0; for (int i = 0; i < 24 * 24; i++) if ((a.bits[i] & 0xFFFFFF) != (b.bits[i] & 0xFFFFFF)) diff++;
    EXPECT_EQ(diff, 0);
    pen_off(&a, &ca, 0); pen_off(&b, &cb, 0); g9_dib_free(&a); g9_dib_free(&b);
}

TEST(gap9_gdi_rectangle_rop2_nop) {
    REQUIRE(UN(NtGdiRectangle)); REQUIRE(U(SetROP2)); NEED();
    struct g9_dib d; struct pen_ctx c;
    if (!g9_dib_new(&d, 16, 16)) { t_skip("no dib"); return; }
    pen_on(&d, &c, RGB(255, 0, 0), RGB(0, 0, 255), 0);
    int old = U(SetROP2)(d.dc, R2_NOP);
    flush();
    CHECK(UN(NtGdiRectangle)(d.dc, 2, 2, 10, 10));
    flush();
    EXPECT_EQ(g9_px(&d, 2, 2), WHITE);                     /* R2_NOP leaves both pen and brush output untouched */
    EXPECT_EQ(g9_px(&d, 5, 5), WHITE);
    U(SetROP2)(d.dc, old);
    pen_off(&d, &c, 0); g9_dib_free(&d);
}

TEST(gap9_gdi_rectangle_invalid_dcs) {
    REQUIRE(UN(NtGdiRectangle)); NEED();
    CHECK(!UN(NtGdiRectangle)(NULL, 0, 0, 5, 5));
    CHECK(!UN(NtGdiRectangle)((HDC)(ULONG_PTR)0x1234, 0, 0, 5, 5));
    CHECK(!UN(NtGdiRectangle)((HDC)U(GetStockObject)(BLACK_PEN), 0, 0, 5, 5));    /* a pen is not a DC */
    CHECK(!UN(NtGdiRectangle)((HDC)U(GetStockObject)(WHITE_BRUSH), 0, 0, 5, 5));
    struct g9_dib d;
    if (!g9_dib_new(&d, 8, 8)) { t_skip("no dib"); return; }
    HDC dead = d.dc;
    g9_dib_free(&d);
    CHECK(!UN(NtGdiRectangle)(dead, 0, 0, 5, 5));          /* handle outlives the DC object */
}

/* ---- NtGdiLineTo ---------------------------------------------------------------------------- */
TEST(gap9_gdi_lineto_horizontal_excludes_endpoint) {
    REQUIRE(UN(NtGdiLineTo)); NEED();
    struct g9_dib d; struct pen_ctx c;
    if (!g9_dib_new(&d, 32, 16)) { t_skip("no dib"); return; }
    pen_on(&d, &c, RGB(255, 0, 0), 0, 1);
    U(MoveToEx)(d.dc, 5, 5, NULL);
    flush();
    CHECK(UN(NtGdiLineTo)(d.dc, 15, 5));
    flush();
    for (int x = 5; x < 15; x++) EXPECT_EQ(g9_px(&d, x, 5), RED);
    EXPECT_EQ(g9_px(&d, 15, 5), WHITE);                    /* end point not drawn */
    EXPECT_EQ(g9_px(&d, 4, 5), WHITE);
    EXPECT_EQ(g9_px(&d, 10, 4), WHITE); EXPECT_EQ(g9_px(&d, 10, 6), WHITE);
    pen_off(&d, &c, 1); g9_dib_free(&d);
}

TEST(gap9_gdi_lineto_updates_current_position) {
    REQUIRE(UN(NtGdiLineTo)); NEED();
    struct g9_dib d; struct pen_ctx c;
    if (!g9_dib_new(&d, 32, 32)) { t_skip("no dib"); return; }
    pen_on(&d, &c, RGB(255, 0, 0), 0, 1);
    U(MoveToEx)(d.dc, 2, 3, NULL);
    flush();
    CHECK(UN(NtGdiLineTo)(d.dc, 20, 11));
    flush();
    POINT p = { -1, -1 };
    CHECK(U(GetCurrentPositionEx)(d.dc, &p));
    EXPECT_EQ(p.x, 20); EXPECT_EQ(p.y, 11);                /* the kernel stores the end point in the shared DC_ATTR */
    pen_off(&d, &c, 1); g9_dib_free(&d);
}

TEST(gap9_gdi_lineto_vertical_and_diagonal) {
    REQUIRE(UN(NtGdiLineTo)); NEED();
    struct g9_dib d; struct pen_ctx c;
    if (!g9_dib_new(&d, 32, 32)) { t_skip("no dib"); return; }
    pen_on(&d, &c, RGB(255, 0, 0), 0, 1);
    U(MoveToEx)(d.dc, 3, 3, NULL);
    flush();
    CHECK(UN(NtGdiLineTo)(d.dc, 3, 13));
    flush();
    for (int y = 3; y < 13; y++) EXPECT_EQ(g9_px(&d, 3, y), RED);
    EXPECT_EQ(g9_px(&d, 3, 13), WHITE);
    U(MoveToEx)(d.dc, 10, 10, NULL);
    flush();
    CHECK(UN(NtGdiLineTo)(d.dc, 20, 20));
    flush();
    for (int i = 0; i < 10; i++) EXPECT_EQ(g9_px(&d, 10 + i, 10 + i), RED);   /* exact 45 degree line */
    EXPECT_EQ(g9_px(&d, 20, 20), WHITE);
    pen_off(&d, &c, 1); g9_dib_free(&d);
}

TEST(gap9_gdi_lineto_zero_length_draws_nothing) {
    REQUIRE(UN(NtGdiLineTo)); NEED();
    struct g9_dib d; struct pen_ctx c;
    if (!g9_dib_new(&d, 16, 16)) { t_skip("no dib"); return; }
    pen_on(&d, &c, RGB(255, 0, 0), 0, 1);
    U(MoveToEx)(d.dc, 7, 7, NULL);
    flush();
    CHECK(UN(NtGdiLineTo)(d.dc, 7, 7));
    flush();
    EXPECT_EQ(g9_px(&d, 7, 7), WHITE);
    pen_off(&d, &c, 1); g9_dib_free(&d);
}

TEST(gap9_gdi_lineto_clipped_line_still_succeeds) {
    REQUIRE(UN(NtGdiLineTo)); NEED();
    struct g9_dib d; struct pen_ctx c;
    if (!g9_dib_new(&d, 16, 16)) { t_skip("no dib"); return; }
    pen_on(&d, &c, RGB(255, 0, 0), 0, 1);
    U(MoveToEx)(d.dc, -50, 4, NULL);
    flush();
    CHECK(UN(NtGdiLineTo)(d.dc, 50, 4));
    flush();
    for (int x = 0; x < 16; x++) EXPECT_EQ(g9_px(&d, x, 4), RED);
    pen_off(&d, &c, 1); g9_dib_free(&d);
}

TEST(gap9_gdi_lineto_null_pen_moves_only) {
    REQUIRE(UN(NtGdiLineTo)); NEED();
    struct g9_dib d;
    if (!g9_dib_new(&d, 16, 16)) { t_skip("no dib"); return; }
    HGDIOBJ op = U(SelectObject)(d.dc, U(GetStockObject)(NULL_PEN));
    U(MoveToEx)(d.dc, 1, 1, NULL);
    flush();
    CHECK(UN(NtGdiLineTo)(d.dc, 12, 1));
    flush();
    int changed = 0; for (int i = 0; i < 256; i++) if ((d.bits[i] & 0xFFFFFF) != 0xFFFFFF) changed++;
    EXPECT_EQ(changed, 0);
    POINT p = { 0, 0 }; U(GetCurrentPositionEx)(d.dc, &p);
    EXPECT_EQ(p.x, 12); EXPECT_EQ(p.y, 1);
    U(SelectObject)(d.dc, op); g9_dib_free(&d);
}

TEST(gap9_gdi_lineto_invalid_dcs) {
    REQUIRE(UN(NtGdiLineTo)); NEED();
    CHECK(!UN(NtGdiLineTo)(NULL, 1, 1));
    CHECK(!UN(NtGdiLineTo)((HDC)(ULONG_PTR)0x4321, 1, 1));
    CHECK(!UN(NtGdiLineTo)((HDC)U(GetStockObject)(BLACK_BRUSH), 1, 1));
    struct g9_dib d;
    if (!g9_dib_new(&d, 8, 8)) { t_skip("no dib"); return; }
    HDC dead = d.dc;
    g9_dib_free(&d);
    CHECK(!UN(NtGdiLineTo)(dead, 1, 1));
}

TEST(gap9_gdi_lineto_user_path_matches_syscall) {
    REQUIRE(UN(NtGdiLineTo)); REQUIRE(U(LineTo)); NEED();
    struct g9_dib a, b; struct pen_ctx ca, cb;
    if (!g9_dib_new(&a, 32, 32) || !g9_dib_new(&b, 32, 32)) { t_skip("no dib"); return; }
    pen_on(&a, &ca, RGB(255, 0, 0), 0, 1); pen_on(&b, &cb, RGB(255, 0, 0), 0, 1);
    U(MoveToEx)(a.dc, 2, 30, NULL); U(MoveToEx)(b.dc, 2, 30, NULL);
    CHECK(U(LineTo)(a.dc, 29, 3));
    flush();
    CHECK(UN(NtGdiLineTo)(b.dc, 29, 3));
    flush();
    int diff = 0; for (int i = 0; i < 32 * 32; i++) if ((a.bits[i] & 0xFFFFFF) != (b.bits[i] & 0xFFFFFF)) diff++;
    EXPECT_EQ(diff, 0);
    pen_off(&a, &ca, 1); pen_off(&b, &cb, 1); g9_dib_free(&a); g9_dib_free(&b);
}

/* ---- NtGdiScaleViewportExtEx ------------------------------------------------------------------ */
static int aniso_dc(struct g9_dib *d, int ex, int ey) {
    if (!g9_dib_new(d, 8, 8)) return 0;
    U(SetMapMode)(d->dc, MM_ANISOTROPIC);
    U(SetViewportExtEx)(d->dc, ex, ey, NULL);
    flush();
    return 1;
}
static SIZE vext(HDC dc) { SIZE s = { -1, -1 }; U(GetViewportExtEx)(dc, &s); return s; }
#define NEEDV() do { if (!U(SetMapMode) || !U(SetViewportExtEx) || !U(GetViewportExtEx)) { t_skip("gdi32 missing"); return; } } while (0)

TEST(gap9_gdi_scalevp_anisotropic_ratio) {
    REQUIRE(UN(NtGdiScaleViewportExtEx)); NEEDV();
    struct g9_dib d;
    if (!aniso_dc(&d, 100, 200)) { t_skip("no dib"); return; }
    SIZE old = { -7, -7 };
    CHECK(UN(NtGdiScaleViewportExtEx)(d.dc, 1, 2, 3, 4, &old));
    EXPECT_EQ(old.cx, 100); EXPECT_EQ(old.cy, 200);        /* previous extent comes back first */
    SIZE now = vext(d.dc);
    EXPECT_EQ(now.cx, 50);  EXPECT_EQ(now.cy, 150);        /* ext = ext * num / denom per axis */
    g9_dib_free(&d);
}

TEST(gap9_gdi_scalevp_compounds) {
    REQUIRE(UN(NtGdiScaleViewportExtEx)); NEEDV();
    struct g9_dib d;
    if (!aniso_dc(&d, 64, 64)) { t_skip("no dib"); return; }
    CHECK(UN(NtGdiScaleViewportExtEx)(d.dc, 2, 1, 2, 1, NULL));
    CHECK(UN(NtGdiScaleViewportExtEx)(d.dc, 3, 2, 1, 4, NULL));
    SIZE now = vext(d.dc);
    EXPECT_EQ(now.cx, 192); EXPECT_EQ(now.cy, 32);        /* 64*2=128 *3/2=192 ; 64*2=128 /4=32 */
    g9_dib_free(&d);
}

TEST(gap9_gdi_scalevp_null_old_extent_pointer) {
    REQUIRE(UN(NtGdiScaleViewportExtEx)); NEEDV();
    struct g9_dib d;
    if (!aniso_dc(&d, 40, 40)) { t_skip("no dib"); return; }
    CHECK(UN(NtGdiScaleViewportExtEx)(d.dc, 1, 1, 1, 1, NULL));
    SIZE now = vext(d.dc);
    EXPECT_EQ(now.cx, 40); EXPECT_EQ(now.cy, 40);
    g9_dib_free(&d);
}

TEST(gap9_gdi_scalevp_zero_denominator_fails_unchanged) {
    REQUIRE(UN(NtGdiScaleViewportExtEx)); NEEDV();
    struct g9_dib d;
    if (!aniso_dc(&d, 100, 200)) { t_skip("no dib"); return; }
    static const int a[][4] = { {1, 0, 1, 1}, {1, 1, 1, 0}, {0, 1, 1, 1}, {1, 1, 0, 1}, {1, 0, 1, 0} };
    for (int i = 0; i < G9_N(a); i++) {
        SIZE old = { -7, -7 };
        CHECKF(!UN(NtGdiScaleViewportExtEx)(d.dc, a[i][0], a[i][1], a[i][2], a[i][3], &old), "args %d", i);
        EXPECT_EQ(old.cx, 100); EXPECT_EQ(old.cy, 200);    /* *lpSize is stored before the arithmetic */
        SIZE now = vext(d.dc);
        EXPECT_EQ(now.cx, 100); EXPECT_EQ(now.cy, 200);
    }
    g9_dib_free(&d);
}

TEST(gap9_gdi_scalevp_result_rounds_to_zero_fails) {
    REQUIRE(UN(NtGdiScaleViewportExtEx)); NEEDV();
    struct g9_dib d;
    if (!aniso_dc(&d, 1, 1)) { t_skip("no dib"); return; }
    CHECK(!UN(NtGdiScaleViewportExtEx)(d.dc, 1, 2, 1, 1, NULL));    /* 1*1/2 == 0 */
    SIZE now = vext(d.dc);
    EXPECT_EQ(now.cx, 1); EXPECT_EQ(now.cy, 1);
    g9_dib_free(&d);
}

TEST(gap9_gdi_scalevp_negative_ratio_mirrors) {
    REQUIRE(UN(NtGdiScaleViewportExtEx)); NEEDV();
    struct g9_dib d;
    if (!aniso_dc(&d, 100, 100)) { t_skip("no dib"); return; }
    CHECK(UN(NtGdiScaleViewportExtEx)(d.dc, -1, 1, 1, 1, NULL));
    SIZE now = vext(d.dc);
    EXPECT_EQ(now.cx, -100); EXPECT_EQ(now.cy, 100);
    g9_dib_free(&d);
}

TEST(gap9_gdi_scalevp_fixed_map_modes_untouched) {
    REQUIRE(UN(NtGdiScaleViewportExtEx)); NEEDV();
    static const int modes[] = { MM_TEXT, MM_LOMETRIC, MM_HIMETRIC, MM_LOENGLISH, MM_HIENGLISH, MM_TWIPS };
    struct g9_dib d;
    if (!g9_dib_new(&d, 8, 8)) { t_skip("no dib"); return; }
    for (int i = 0; i < G9_N(modes); i++) {
        U(SetMapMode)(d.dc, modes[i]);
        flush();
        SIZE before = vext(d.dc), old = { -9, -9 };
        BOOL r = UN(NtGdiScaleViewportExtEx)(d.dc, 2, 1, 2, 1, &old);   /* map mode <= 6: the scaling block is skipped, result is still TRUE */
        SIZE after = vext(d.dc);
        CHECKF(after.cx == before.cx && after.cy == before.cy, "mode %d: ext %ld,%ld -> %ld,%ld", modes[i], before.cx, before.cy, after.cx, after.cy);
        CHECKF(r, "mode %d returned FALSE", modes[i]);
        t_note("mode %d: viewport ext %ld,%ld old %ld,%ld ret %d", modes[i], before.cx, before.cy, old.cx, old.cy, r);
    }
    g9_dib_free(&d);
}

TEST(gap9_gdi_scalevp_invalid_dc) {
    REQUIRE(UN(NtGdiScaleViewportExtEx));
    SIZE old = { 5, 6 };
    CHECK(!UN(NtGdiScaleViewportExtEx)(NULL, 1, 1, 1, 1, &old));
    CHECK(!UN(NtGdiScaleViewportExtEx)((HDC)(ULONG_PTR)0x2222, 1, 1, 1, 1, &old));
    EXPECT_EQ(old.cx, 5); EXPECT_EQ(old.cy, 6);            /* dc lookup precedes the store */
}

TEST(gap9_gdi_scalevp_user32_wrapper) {
    REQUIRE(U(ScaleViewportExtEx)); NEEDV();
    struct g9_dib d;
    if (!aniso_dc(&d, 90, 30)) { t_skip("no dib"); return; }
    SIZE old = { 0, 0 };
    CHECK(U(ScaleViewportExtEx)(d.dc, 1, 3, 1, 3, &old));
    EXPECT_EQ(old.cx, 90); EXPECT_EQ(old.cy, 30);
    SIZE now = vext(d.dc);
    EXPECT_EQ(now.cx, 30); EXPECT_EQ(now.cy, 10);
    g9_dib_free(&d);
}

/* ---- NtGdiSetColorAdjustment ------------------------------------------------------------------ */
static COLORADJUSTMENT base_ca(void) {
    COLORADJUSTMENT c; memset(&c, 0, sizeof c);
    c.caSize = sizeof c; c.caIlluminantIndex = ILLUMINANT_DEVICE_DEFAULT;
    c.caRedGamma = c.caGreenGamma = c.caBlueGamma = 10000;
    c.caReferenceBlack = 0; c.caReferenceWhite = 10000;
    return c;
}

TEST(gap9_gdi_setca_default_and_roundtrip) {
    REQUIRE(UN(NtGdiSetColorAdjustment)); REQUIRE(U(GetColorAdjustment)); NEED();
    struct g9_dib d;
    if (!g9_dib_new(&d, 4, 4)) { t_skip("no dib"); return; }
    COLORADJUSTMENT c = base_ca(), g;
    CHECK(UN(NtGdiSetColorAdjustment)(d.dc, &c));
    c.caIlluminantIndex = ILLUMINANT_D65; c.caRedGamma = 20000; c.caGreenGamma = 15000; c.caBlueGamma = 30000;
    c.caReferenceBlack = 500; c.caReferenceWhite = 9500;
    c.caContrast = 10; c.caBrightness = -20; c.caColorfulness = 30; c.caRedGreenTint = -40;
    CHECK(UN(NtGdiSetColorAdjustment)(d.dc, &c));
    memset(&g, 0xEE, sizeof g);
    CHECK(U(GetColorAdjustment)(d.dc, &g));
    EXPECT_EQ(g.caSize, sizeof g);
    EXPECT_EQ(g.caIlluminantIndex, ILLUMINANT_D65);
    EXPECT_EQ(g.caRedGamma, 20000); EXPECT_EQ(g.caGreenGamma, 15000); EXPECT_EQ(g.caBlueGamma, 30000);
    EXPECT_EQ(g.caReferenceBlack, 500); EXPECT_EQ(g.caReferenceWhite, 9500);
    EXPECT_EQ(g.caContrast, 10); EXPECT_EQ(g.caBrightness, -20); EXPECT_EQ(g.caColorfulness, 30); EXPECT_EQ(g.caRedGreenTint, -40);
    g9_dib_free(&d);
}

TEST(gap9_gdi_setca_field_ranges) {
    REQUIRE(UN(NtGdiSetColorAdjustment)); NEED();
    struct g9_dib d;
    if (!g9_dib_new(&d, 4, 4)) { t_skip("no dib"); return; }
#define ROW(field, v, ok) { offsetof(COLORADJUSTMENT, field), sizeof(((COLORADJUSTMENT *)0)->field), (v), (ok), #field "=" #v }
    static const struct { size_t off, sz; int v; int ok; const char *n; } t[] = {
        ROW(caSize, 0x18, 1), ROW(caSize, 0x17, 0), ROW(caSize, 0x19, 0), ROW(caSize, 0, 0), ROW(caSize, 0x1800, 0),
        ROW(caIlluminantIndex, 0, 1), ROW(caIlluminantIndex, 8, 1), ROW(caIlluminantIndex, 9, 0), ROW(caIlluminantIndex, 0xFFFF, 0),
        ROW(caRedGamma, 1343, 0), ROW(caRedGamma, 1344, 1), ROW(caRedGamma, 65000, 1), ROW(caRedGamma, 65001, 0), ROW(caRedGamma, 0, 0),
        ROW(caGreenGamma, 1343, 0), ROW(caGreenGamma, 1344, 1), ROW(caGreenGamma, 65000, 1), ROW(caGreenGamma, 65001, 0),
        ROW(caBlueGamma, 1343, 0), ROW(caBlueGamma, 1344, 1), ROW(caBlueGamma, 65000, 1), ROW(caBlueGamma, 65001, 0),
        ROW(caReferenceBlack, 0, 1), ROW(caReferenceBlack, 4000, 1), ROW(caReferenceBlack, 4001, 0),
        ROW(caReferenceWhite, 5999, 0), ROW(caReferenceWhite, 6000, 1), ROW(caReferenceWhite, 10000, 1), ROW(caReferenceWhite, 10001, 0), ROW(caReferenceWhite, 0, 0),
        ROW(caContrast, -100, 1), ROW(caContrast, 100, 1), ROW(caContrast, -101, 0), ROW(caContrast, 101, 0),
        ROW(caBrightness, -100, 1), ROW(caBrightness, 100, 1), ROW(caBrightness, -101, 0), ROW(caBrightness, 101, 0),
        ROW(caColorfulness, -100, 1), ROW(caColorfulness, 100, 1), ROW(caColorfulness, -101, 0), ROW(caColorfulness, 101, 0),
        ROW(caRedGreenTint, -100, 1), ROW(caRedGreenTint, 100, 1), ROW(caRedGreenTint, -101, 0), ROW(caRedGreenTint, 101, 0),
    };
    for (int i = 0; i < G9_N(t); i++) {
        COLORADJUSTMENT c = base_ca();
        if (t[i].sz == 2) { WORD w = (WORD)t[i].v; memcpy((BYTE *)&c + t[i].off, &w, 2); }
        else { SHORT s = (SHORT)t[i].v; memcpy((BYTE *)&c + t[i].off, &s, 2); }
        BOOL r = UN(NtGdiSetColorAdjustment)(d.dc, &c);
        CHECKF(!!r == t[i].ok, "%s: got %d want %d", t[i].n, r, t[i].ok);
    }
    g9_dib_free(&d);
}

TEST(gap9_gdi_setca_rejected_set_keeps_previous) {
    REQUIRE(UN(NtGdiSetColorAdjustment)); REQUIRE(U(GetColorAdjustment)); NEED();
    struct g9_dib d;
    if (!g9_dib_new(&d, 4, 4)) { t_skip("no dib"); return; }
    COLORADJUSTMENT c = base_ca(), bad = base_ca(), g;
    c.caRedGamma = 12345; c.caContrast = 7;
    CHECK(UN(NtGdiSetColorAdjustment)(d.dc, &c));
    bad.caRedGamma = 22222; bad.caContrast = 101;           /* one bad field rejects the whole structure */
    CHECK(!UN(NtGdiSetColorAdjustment)(d.dc, &bad));
    memset(&g, 0, sizeof g);
    CHECK(U(GetColorAdjustment)(d.dc, &g));
    EXPECT_EQ(g.caRedGamma, 12345); EXPECT_EQ(g.caContrast, 7);
    g9_dib_free(&d);
}

TEST(gap9_gdi_setca_flags_not_validated) {
    REQUIRE(UN(NtGdiSetColorAdjustment)); NEED();
    struct g9_dib d;
    if (!g9_dib_new(&d, 4, 4)) { t_skip("no dib"); return; }
    for (WORD fl = 0; fl <= 3; fl++) {
        COLORADJUSTMENT c = base_ca(); c.caFlags = fl;
        BOOL r = UN(NtGdiSetColorAdjustment)(d.dc, &c);
        CHECKF(r, "caFlags %u (CA_NEGATIVE|CA_LOG_FILTER combinations) rejected", fl);
    }
    g9_dib_free(&d);
}

TEST(gap9_gdi_setca_invalid_dc) {
    REQUIRE(UN(NtGdiSetColorAdjustment)); NEED();
    COLORADJUSTMENT c = base_ca();
    CHECK(!UN(NtGdiSetColorAdjustment)(NULL, &c));
    CHECK(!UN(NtGdiSetColorAdjustment)((HDC)(ULONG_PTR)0x3333, &c));
    CHECK(!UN(NtGdiSetColorAdjustment)((HDC)U(GetStockObject)(BLACK_PEN), &c));
}

TEST(gap9_gdi_setca_user_path) {
    REQUIRE(U(SetColorAdjustment)); REQUIRE(U(GetColorAdjustment)); NEED();
    struct g9_dib d;
    if (!g9_dib_new(&d, 4, 4)) { t_skip("no dib"); return; }
    COLORADJUSTMENT c = base_ca(), g;
    c.caBrightness = 55;
    CHECK(U(SetColorAdjustment)(d.dc, &c));
    CHECK(U(GetColorAdjustment)(d.dc, &g));
    EXPECT_EQ(g.caBrightness, 55);
    c.caSize = 12;
    CHECK(!U(SetColorAdjustment)(d.dc, &c));
    SetLastError(0);
    CHECK(!U(SetColorAdjustment)(NULL, &c));
    g9_dib_free(&d);
}
