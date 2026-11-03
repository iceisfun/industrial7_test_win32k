/* CreateEllipticRgn / CreateRoundRectRgn and their Nt syscalls: result matrix for degenerate, inverted, unit, negative
 * and out-of-range rectangles. A failing call returns NULL and sets the last error; a successful one returns a region
 * of the stated complexity and bounding box (the box of an elliptic region ends one short of the right and bottom
 * edge). Both the Nt stub and the gdi32 export must agree on every row. */
#include "xn.h"

struct erow { int l, t, r, b; int cplx; int box[4]; DWORD err; };

static void rgn_row(const struct erow *w, int round, int ew, int eh) {
    if (!U(CreateEllipticRgn) || !U(CreateRoundRectRgn) || !U(DeleteObject) || !UN(NtGdiCreateEllipticRgn) || !UN(NtGdiCreateRoundRectRgn) || !UN(NtGdiGetRgnBox)) { t_skip("exports missing"); return; }
    for (int route = 0; route < 2; route++) {
        SetLastError(0);
        HRGN h;
        if (round) h = route ? U(CreateRoundRectRgn)(w->l, w->t, w->r, w->b, ew, eh) : UN(NtGdiCreateRoundRectRgn)(w->l, w->t, w->r, w->b, ew, eh);
        else h = route ? U(CreateEllipticRgn)(w->l, w->t, w->r, w->b) : UN(NtGdiCreateEllipticRgn)(w->l, w->t, w->r, w->b);
        DWORD e = GetLastError();
        if (!w->cplx) {
            CHECKF(h == NULL, "route %d returned a region for a rejected rectangle", route);
            CHECKF(e == w->err, "route %d error %lu want %lu", route, e, w->err);
            if (h) U(DeleteObject)(h);
            continue;
        }
        if (!h) { CHECKF(0, "route %d returned NULL", route); continue; }
        RECT bx; int c = kbox(h, &bx);
        CHECKF(c == w->cplx, "route %d complexity %d want %d", route, c, w->cplx);
        CHECKF(box_is(&bx, w->box[0], w->box[1], w->box[2], w->box[3]), "route %d box (%ld,%ld,%ld,%ld) want (%d,%d,%d,%d)", route, bx.left, bx.top, bx.right, bx.bottom, w->box[0], w->box[1], w->box[2], w->box[3]);
        U(DeleteObject)(h);
    }
}

#define ER(name, l, t, r, b, cplx, bl, bt, br, bb, err) \
    static const struct erow e_##name = { l, t, r, b, cplx, { bl, bt, br, bb }, err }; \
    XT(ellipse_##name) { rgn_row(&e_##name, 0, 0, 0); } \
    XT(roundrect_##name) { rgn_row(&e_##name, 1, 4, 4); }

ER(zero_size,        0, 0, 0, 0,     RC_SIMPLE, -1, -1, 0, 0, 0)
ER(zero_height,      0, 0, 10, 0,    RC_SIMPLE, 0, -1, 9, 0, 0)
ER(zero_width,       0, 0, 0, 10,    RC_SIMPLE, -1, 0, 0, 9, 0)
ER(inverted_both,    10, 10, 0, 0,   RC_COMPLEX, 0, 0, 9, 9, 0)
ER(inverted_x,       10, 0, 0, 10,   RC_COMPLEX, 0, 0, 9, 9, 0)
ER(inverted_y,       0, 10, 10, 0,   RC_COMPLEX, 0, 0, 9, 9, 0)
ER(ten_square,       0, 0, 10, 10,   RC_COMPLEX, 0, 0, 9, 9, 0)
ER(unit,             0, 0, 1, 1,     RC_NULL, 0, 0, 0, 0, 0)
ER(two_square,       0, 0, 2, 2,     RC_SIMPLE, 0, 0, 1, 1, 0)
ER(three_square,     0, 0, 3, 3,     RC_NULL, 0, 0, 0, 0, 0)
ER(negative_origin, -5, -5, 5, 5,    RC_COMPLEX, -5, -5, 4, 4, 0)
ER(right_over_max,   0, 0, 0x8000000, 10, 0, 0, 0, 0, 0, 87)
ER(left_under_min,  -0x8000001, 0, 10, 10, 0, 0, 0, 0, 0, 87)
ER(right_int_max,    0, 0, 0x7fffffff, 10, 0, 0, 0, 0, 0, 87)
ER(left_int_min,    -0x7fffffff - 1, 0, 10, 10, 0, 0, 0, 0, 0, 87)
ER(bottom_over_max,  0, 0, 10, 0x8000000, 0, 0, 0, 0, 0, 87)
ER(top_under_min,    0, -0x8000001, 10, 10, 0, 0, 0, 0, 0, 87)

/* The extreme legal range behaves differently for the two shapes. */
XT(ellipse_full_legal_range) {
    struct erow w = { -0x8000000, -0x8000000, 0x7ffffff, 0x7ffffff, RC_SIMPLE, { -0x8000000, 0x7ffffff - 1, 0x7ffffff - 1, 0x7ffffff }, 0 };
    rgn_row(&w, 0, 0, 0);
}
XT(roundrect_full_legal_range) {
    struct erow w = { -0x8000000, -0x8000000, 0x7ffffff, 0x7ffffff, RC_NULL, { 0, 0, 0, 0 }, 0 };
    rgn_row(&w, 1, 4, 4);
}

/* CreateRoundRectRgn corner-size matrix on a 20x12 rectangle: the box is always (0,0,19,11); the complexity and the
 * number of rectangles depend on the corner size. */
static void rr_corner(int ew, int eh, int cplx, int nrects) {
    if (!U(CreateRoundRectRgn) || !U(DeleteObject) || !UN(NtGdiCreateRoundRectRgn) || !UN(NtGdiGetRegionData) || !UN(NtGdiGetRgnBox)) { t_skip("exports missing"); return; }
    for (int route = 0; route < 2; route++) {
        HRGN h = route ? U(CreateRoundRectRgn)(0, 0, 20, 12, ew, eh) : UN(NtGdiCreateRoundRectRgn)(0, 0, 20, 12, ew, eh);
        if (!h) { CHECKF(0, "route %d returned NULL for corner %d,%d", route, ew, eh); continue; }
        RECT bx, rc[64]; int c = kbox(h, &bx);
        CHECKF(c == cplx, "route %d complexity %d want %d", route, c, cplx);
        CHECK(box_is(&bx, 0, 0, 19, 11));
        int n = rgn_rects(h, rc, 64);
        CHECKF(n == nrects, "route %d rectangles %d want %d", route, n, nrects);
        U(DeleteObject)(h);
    }
}
XT(roundrect_corner_zero)        { rr_corner(0, 0, RC_SIMPLE, 1); }
XT(roundrect_corner_one)         { rr_corner(1, 1, RC_SIMPLE, 1); }
XT(roundrect_corner_negative)    { rr_corner(-4, -4, RC_COMPLEX, 3); }
XT(roundrect_corner_mixed_sign)  { rr_corner(4, -4, RC_COMPLEX, 3); }
XT(roundrect_corner_ten)         { rr_corner(10, 10, RC_COMPLEX, 7); }
XT(roundrect_corner_twenty)      { rr_corner(20, 20, RC_COMPLEX, 9); }
XT(roundrect_corner_larger_than_rect) { rr_corner(32, 16, RC_COMPLEX, 9); }
XT(roundrect_corner_thousand)    { rr_corner(1000, 1000, RC_COMPLEX, 9); }
XT(roundrect_corner_int_max_width) { rr_corner(0x7fffffff, 3, RC_COMPLEX, 3); }

/* The rectangles of a 10x10 ellipse, in band order. */
XT(ellipse_ten_square_rectangles) {
    static const RECT want[] = { { 4, 0, 5, 1 }, { 1, 1, 7, 2 }, { 1, 2, 8, 4 }, { 0, 4, 9, 5 }, { 1, 5, 8, 7 }, { 1, 7, 7, 8 }, { 4, 8, 5, 9 } };
    if (!U(CreateEllipticRgn) || !U(DeleteObject) || !UN(NtGdiGetRegionData)) { t_skip("exports missing"); return; }
    HRGN h = U(CreateEllipticRgn)(0, 0, 10, 10);
    RECT rc[32]; int n = h ? rgn_rects(h, rc, 32) : -1;
    EXPECT_EQ(n, 7);
    for (int i = 0; i < n && i < 7; i++) CHECKF(!memcmp(&rc[i], &want[i], sizeof(RECT)), "rectangle %d (%ld,%ld,%ld,%ld)", i, rc[i].left, rc[i].top, rc[i].right, rc[i].bottom);
    if (h) U(DeleteObject)(h);
}
XT(ellipse_inverted_equals_normal) {
    if (!U(CreateEllipticRgn) || !U(DeleteObject) || !UN(NtGdiEqualRgn)) { t_skip("exports missing"); return; }
    HRGN a = U(CreateEllipticRgn)(0, 0, 10, 10), b = U(CreateEllipticRgn)(10, 10, 0, 0);
    CHECK(a && b && UN(NtGdiEqualRgn)(a, b));
    if (a) { U(DeleteObject)(a); } if (b) { U(DeleteObject)(b); }
}
XT(roundrect_inverted_equals_normal) {
    if (!U(CreateRoundRectRgn) || !U(DeleteObject) || !UN(NtGdiEqualRgn)) { t_skip("exports missing"); return; }
    HRGN a = U(CreateRoundRectRgn)(0, 0, 20, 12, 6, 6), b = U(CreateRoundRectRgn)(20, 12, 0, 0, 6, 6);
    CHECK(a && b && UN(NtGdiEqualRgn)(a, b));
    if (a) { U(DeleteObject)(a); } if (b) { U(DeleteObject)(b); }
}
XT(created_regions_have_region_type_and_delete_once) {
    if (!U(CreateEllipticRgn) || !U(CreateRoundRectRgn) || !U(DeleteObject) || !U(GetObjectType)) { t_skip("exports missing"); return; }
    HRGN h[2] = { U(CreateEllipticRgn)(0, 0, 8, 8), U(CreateRoundRectRgn)(0, 0, 8, 8, 2, 2) };
    for (int i = 0; i < 2; i++) {
        CHECK(h[i] != NULL);
        EXPECT_EQ(U(GetObjectType)(h[i]), OBJ_REGION);
        CHECK(U(DeleteObject)(h[i]));
        CHECK(!U(DeleteObject)(h[i]));          /* the second delete of the same handle fails */
    }
}
XT(created_regions_are_distinct_handles) {
    if (!U(CreateEllipticRgn) || !U(DeleteObject)) { t_skip("exports missing"); return; }
    HRGN a = U(CreateEllipticRgn)(0, 0, 8, 8), b = U(CreateEllipticRgn)(0, 0, 8, 8);
    CHECK(a && b && a != b);
    if (a) { U(DeleteObject)(a); } if (b) { U(DeleteObject)(b); }
}
XT(failed_create_does_not_leak_a_handle) {
    if (!U(CreateEllipticRgn) || !U(CreateRoundRectRgn) || !U(DeleteObject) || !U(GetGuiResources)) { t_skip("exports missing"); return; }
    DWORD before = U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS);
    for (int i = 0; i < 20; i++) {
        HRGN a = U(CreateEllipticRgn)(0, 0, 0x8000000, 10), b = U(CreateRoundRectRgn)(-0x8000001, 0, 10, 10, 2, 2);
        CHECK(!a && !b);
        if (a) { U(DeleteObject)(a); } if (b) { U(DeleteObject)(b); }
    }
    DWORD after = U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS);
    CHECKF(after <= before, "GDI object count grew %lu -> %lu", before, after);
}
