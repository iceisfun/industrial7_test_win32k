/* CombineRgn geometry fixture: every combine mode over simple, complex and empty sources, with the destination
 * separate from, equal to the first source, or equal to the second source. The result is judged against a
 * point-membership model (PtInRegion on a grid that covers every edge, the cells just inside and just outside it, and
 * points far beyond the bounds), against GetRgnBox, against the GetRegionData rectangle count, and against the
 * documented return value (NULLREGION / SIMPLEREGION / COMPLEXREGION). Both the gdi32 entry point and the raw
 * NtGdiCombineRgn syscall are run. */
#include "rgn.h"

#define READY() do { if (!U(CreateRectRgn) || !U(DeleteObject) || !U(CombineRgn) || !U(PtInRegion) || !UN(NtGdiCombineRgn) || !UN(NtGdiPtInRegion) || !UN(NtGdiGetRgnBox) || !UN(NtGdiGetRegionData) || !UN(NtGdiCreateRectRgn)) { t_skip("exports missing"); return; } } while (0)

static void del(HRGN h) { if (h) U(DeleteObject)(h); }
static const char *mname(int m) { static const char *n[] = { "?", "AND", "OR", "XOR", "DIFF", "COPY" }; return (m >= 1 && m <= 5) ? n[m] : "?"; }

struct shape { const char *name; int n; RECT r[3]; };
/* A complex (two disjoint rectangles), B complex (overlaps both), C simple, E empty */
static const struct shape g_sh[] = {
    { "A", 2, { { 0, 0, 10, 10 }, { 20, 5, 30, 15 } } },
    { "B", 2, { { 5, -5, 25, 8 }, { 12, 8, 18, 20 } } },
    { "C", 1, { { 5, 5, 15, 15 } } },
    { "E", 0, { { 0, 0, 0, 0 } } },
};
#define NSH ((int)(sizeof g_sh / sizeof g_sh[0]))

static int in_shape(const struct shape *s, int x, int y) {
    for (int i = 0; i < s->n; i++)
        if (x >= s->r[i].left && x < s->r[i].right && y >= s->r[i].top && y < s->r[i].bottom) return 1;
    return 0;
}
static int model(int mode, int a, int b) {
    switch (mode) {
    case RGN_AND: return a && b;
    case RGN_OR: return a || b;
    case RGN_XOR: return a != b;
    case RGN_DIFF: return a && !b;
    default: return a;   /* RGN_COPY */
    }
}

/* grid coordinates: every rectangle edge, the coordinate before it, and two far points */
static int g_xs[64], g_ys[64], g_nx, g_ny;
static void add_unique(int *a, int *n, int v) { for (int i = 0; i < *n; i++) if (a[i] == v) return; a[(*n)++] = v; }
static void build_grid(void) {
    g_nx = g_ny = 0;
    for (int s = 0; s < NSH; s++) for (int i = 0; i < g_sh[s].n; i++) {
        const RECT *r = &g_sh[s].r[i];
        add_unique(g_xs, &g_nx, r->left - 1); add_unique(g_xs, &g_nx, r->left);
        add_unique(g_xs, &g_nx, r->right - 1); add_unique(g_xs, &g_nx, r->right);
        add_unique(g_ys, &g_ny, r->top - 1); add_unique(g_ys, &g_ny, r->top);
        add_unique(g_ys, &g_ny, r->bottom - 1); add_unique(g_ys, &g_ny, r->bottom);
    }
    add_unique(g_xs, &g_nx, -1000); add_unique(g_xs, &g_nx, 1000); add_unique(g_xs, &g_nx, 0x7ffffff);
    add_unique(g_ys, &g_ny, -1000); add_unique(g_ys, &g_ny, 1000); add_unique(g_ys, &g_ny, 0x7ffffff);
}

/* creates region for a shape; kernel != 0 builds with the raw syscalls (rectangles via kcreate) */
static HRGN make(const struct shape *s, int kernel) {
    HRGN h = kernel ? kcreate(s->r[0].left, s->r[0].top, s->r[0].right, s->r[0].bottom)
                    : U(CreateRectRgn)(s->r[0].left, s->r[0].top, s->r[0].right, s->r[0].bottom);
    for (int i = 1; h && i < s->n; i++) {
        HRGN t = kernel ? kcreate(s->r[i].left, s->r[i].top, s->r[i].right, s->r[i].bottom)
                        : U(CreateRectRgn)(s->r[i].left, s->r[i].top, s->r[i].right, s->r[i].bottom);
        if (kernel) UN(NtGdiCombineRgn)(h, h, t, RGN_OR); else U(CombineRgn)(h, h, t, RGN_OR);
        del(t);
    }
    return h;
}
static int comb(int kernel, HRGN d, HRGN a, HRGN b, int mode) {
    return kernel ? UN(NtGdiCombineRgn)(d, a, b, mode) : U(CombineRgn)(d, a, b, mode);
}
static int ptin(int kernel, HRGN h, int x, int y) {
    return (kernel ? UN(NtGdiPtInRegion)(h, x, y) : U(PtInRegion)(h, x, y)) != 0;
}

/* Compares region h with membership function f(x,y). Returns the bad-point count (0 = same) and fills the model's
 * bounding box / area into *want_box / *any. The membership is read with PtInRegion over the grid. */
struct rep { int bad, any; RECT box; };

static void check_region(int kernel, HRGN h, int ret, const struct shape *s1, const struct shape *s2, int mode, const char *tag) {
    int bad = 0, any = 0;
    RECT mb = { 0x7fffffff, 0x7fffffff, -0x7fffffff, -0x7fffffff };
    for (int yi = 0; yi < g_ny; yi++) for (int xi = 0; xi < g_nx; xi++) {
        int x = g_xs[xi], y = g_ys[yi];
        int want = model(mode, in_shape(s1, x, y), s2 ? in_shape(s2, x, y) : 0);
        if (ptin(kernel, h, x, y) != want) bad++;
        if (want) {
            any = 1;
            if (x < mb.left) mb.left = x;
            if (y < mb.top) mb.top = y;
            if (x + 1 > mb.right) mb.right = x + 1;
            if (y + 1 > mb.bottom) mb.bottom = y + 1;
        }
    }
    CHECKF(bad == 0, "%s: %d grid points differ from the model", tag, bad);
    /* bounding box: every rectangle edge is a grid coordinate, so the model box is exact */
    RECT bx; int c = kbox(h, &bx);
    RECT rr[16]; int n = rgn_rects(h, rr, 16);
    if (!any) {
        CHECKF(ret == RC_NULL, "%s: ret %d want NULLREGION", tag, ret);
        CHECKF(c == RC_NULL, "%s: GetRgnBox %d want NULLREGION", tag, c);
        CHECKF(box_is(&bx, 0, 0, 0, 0), "%s: empty box (%ld,%ld,%ld,%ld)", tag, bx.left, bx.top, bx.right, bx.bottom);
    } else {
        int want_c = (n == 1) ? RC_SIMPLE : RC_COMPLEX;
        CHECKF(n >= 1, "%s: rect count %d", tag, n);
        CHECKF(ret == want_c, "%s: ret %d want %d for %d rects", tag, ret, want_c, n);
        CHECKF(c == want_c, "%s: GetRgnBox %d want %d", tag, c, want_c);
        CHECKF(box_is(&bx, mb.left, mb.top, mb.right, mb.bottom), "%s: box (%ld,%ld,%ld,%ld) want (%ld,%ld,%ld,%ld)", tag,
               bx.left, bx.top, bx.right, bx.bottom, mb.left, mb.top, mb.right, mb.bottom);
    }
}

/* mode: aliasing 0 separate, 1 dst == src1, 2 dst == src2 */
static void run_matrix(int kernel) {
    build_grid();
    static const int modes[] = { RGN_AND, RGN_OR, RGN_XOR, RGN_DIFF, RGN_COPY };
    static const char *al[] = { "separate", "dst=src1", "dst=src2" };
    for (int i = 0; i < NSH; i++) for (int j = 0; j < NSH; j++) for (int mi = 0; mi < 5; mi++) for (int a = 0; a < 3; a++) {
        const struct shape *s1 = &g_sh[i], *s2 = &g_sh[j];
        int mode = modes[mi];
        HRGN r1 = make(s1, kernel), r2 = make(s2, kernel);
        HRGN dst = (a == 0) ? make(&g_sh[2], kernel) : (a == 1) ? r1 : r2;
        char tag[96];
        snprintf(tag, sizeof tag, "%s %s %s %s %s", kernel ? "nt" : "user", s1->name, mname(mode), s2->name, al[a]);
        int ret = comb(kernel, dst, r1, r2, mode);
        check_region(kernel, dst, ret, s1, s2, mode, tag);
        /* operands that are not the destination are unchanged */
        if (a != 1) { int bad = 0; for (int yi = 0; yi < g_ny; yi++) for (int xi = 0; xi < g_nx; xi++) if (ptin(kernel, r1, g_xs[xi], g_ys[yi]) != in_shape(s1, g_xs[xi], g_ys[yi])) bad++; CHECKF(bad == 0, "%s: source 1 changed", tag); }
        if (a != 2) { int bad = 0; for (int yi = 0; yi < g_ny; yi++) for (int xi = 0; xi < g_nx; xi++) if (ptin(kernel, r2, g_xs[xi], g_ys[yi]) != in_shape(s2, g_xs[xi], g_ys[yi])) bad++; CHECKF(bad == 0, "%s: source 2 changed", tag); }
        if (a == 0) del(dst);
        del(r1);
        if (r2 != r1) del(r2);
    }
}

TEST(gdi_regions_clip_combine_geometry_matrix_userland) { READY(); run_matrix(0); }
TEST(gdi_regions_clip_combine_geometry_matrix_syscall) { READY(); run_matrix(1); }

/* identical sources: separate destination, then the same region object as both sources */
TEST(gdi_regions_clip_combine_identical_sources_all_modes) {
    READY();
    for (int kernel = 0; kernel < 2; kernel++) {
        HRGN a = make(&g_sh[0], kernel), b = make(&g_sh[0], kernel), d = make(&g_sh[2], kernel);
        /* two distinct regions with identical contents */
        CHECK(comb(kernel, d, a, b, RGN_XOR) == RC_NULL);
        RECT bx; CHECK(kbox(d, &bx) == RC_NULL);
        CHECK(comb(kernel, d, a, b, RGN_DIFF) == RC_NULL);
        CHECK(comb(kernel, d, a, b, RGN_AND) == RC_COMPLEX);
        CHECK(comb(kernel, d, a, b, RGN_OR) == RC_COMPLEX);
        CHECK(comb(kernel, d, a, b, RGN_COPY) == RC_COMPLEX);
        /* the same object as both operands, destination separate */
        CHECK(comb(kernel, d, a, a, RGN_XOR) == RC_NULL);
        CHECK(comb(kernel, d, a, a, RGN_DIFF) == RC_NULL);
        CHECK(comb(kernel, d, a, a, RGN_AND) == RC_COMPLEX);
        CHECK(ptin(kernel, d, 5, 5));
        CHECK(comb(kernel, d, a, a, RGN_OR) == RC_COMPLEX);
        /* in place: destination and both operands the same object */
        CHECK(comb(kernel, a, a, a, RGN_XOR) == RC_NULL);
        CHECK(!ptin(kernel, a, 5, 5));
        del(a); del(b); del(d);
    }
}

/* DIFF is not commutative: A - B and B - A with explicit band decomposition */
TEST(gdi_regions_clip_combine_diff_reversed_operands) {
    READY();
    for (int kernel = 0; kernel < 2; kernel++) {
        HRGN a = kernel ? kcreate(0, 0, 10, 10) : U(CreateRectRgn)(0, 0, 10, 10);
        HRGN b = kernel ? kcreate(5, 5, 15, 15) : U(CreateRectRgn)(5, 5, 15, 15);
        HRGN d = kernel ? kcreate(100, 100, 101, 101) : U(CreateRectRgn)(100, 100, 101, 101);
        RECT o[8]; int n;
        CHECK(comb(kernel, d, a, b, RGN_DIFF) == RC_COMPLEX);
        n = rgn_rects(d, o, 8); CHECK(n == 2);
        if (n == 2) { CHECK(box_is(&o[0], 0, 0, 10, 5)); CHECK(box_is(&o[1], 0, 5, 5, 10)); }
        CHECK(comb(kernel, d, b, a, RGN_DIFF) == RC_COMPLEX);
        n = rgn_rects(d, o, 8); CHECK(n == 2);
        if (n == 2) { CHECK(box_is(&o[0], 10, 5, 15, 10)); CHECK(box_is(&o[1], 5, 10, 15, 15)); }
        RECT bx; CHECK(kbox(d, &bx) == RC_COMPLEX); CHECK_BOX(bx, 5, 5, 15, 15);
        /* in place, reversed: dst == src2 */
        HRGN b2 = kernel ? kcreate(5, 5, 15, 15) : U(CreateRectRgn)(5, 5, 15, 15);
        CHECK(comb(kernel, b2, a, b2, RGN_DIFF) == RC_COMPLEX);
        n = rgn_rects(b2, o, 8); CHECK(n == 2);
        if (n == 2) { CHECK(box_is(&o[0], 0, 0, 10, 5)); CHECK(box_is(&o[1], 0, 5, 5, 10)); }
        /* in place, dst == src1 */
        HRGN a2 = kernel ? kcreate(0, 0, 10, 10) : U(CreateRectRgn)(0, 0, 10, 10);
        CHECK(comb(kernel, a2, a2, b, RGN_DIFF) == RC_COMPLEX);
        n = rgn_rects(a2, o, 8); CHECK(n == 2);
        if (n == 2) { CHECK(box_is(&o[0], 0, 0, 10, 5)); CHECK(box_is(&o[1], 0, 5, 5, 10)); }
        del(a); del(b); del(d); del(b2); del(a2);
    }
}

/* PtInRegion over a complex region: exact edges (right and bottom exclusive), gap between the rectangles, points beyond the bounds */
TEST(gdi_regions_clip_ptinregion_complex_region_grid) {
    READY();
    for (int kernel = 0; kernel < 2; kernel++) {
        HRGN h = make(&g_sh[0], kernel);   /* (0,0,10,10) + (20,5,30,15) */
        static const struct { int x, y, in; } t[] = {
            { 0, 0, 1 }, { 9, 9, 1 }, { 10, 9, 0 }, { 9, 10, 0 }, { 10, 10, 0 }, { -1, 0, 0 }, { 0, -1, 0 },
            { 15, 5, 0 }, { 19, 7, 0 }, { 20, 5, 1 }, { 20, 4, 0 }, { 29, 14, 1 }, { 30, 14, 0 }, { 29, 15, 0 },
            { 30, 15, 0 }, { 25, 10, 1 }, { 9, 14, 0 }, { 20, 9, 1 }, { 19, 9, 0 }, { 5, 14, 0 }, { 25, 4, 0 },
            { -100000, 5, 0 }, { 100000, 5, 0 }, { 5, -100000, 0 }, { 5, 100000, 0 }, { 0x7ffffff, 0x7ffffff, 0 },
            { -0x8000000, -0x8000000, 0 },
        };
        for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++)
            CHECKF(ptin(kernel, h, t[i].x, t[i].y) == t[i].in, "%s (%d,%d)", kernel ? "nt" : "user", t[i].x, t[i].y);
        del(h);
    }
}
