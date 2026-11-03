/* RGNATTR: the 0x18-byte OBJECTATTR behind a rectangular region's cell (cell+0x10).
 *   dword0 Flags      0x10 box valid, 0x20 newer than the kernel's REGION, bit 0 parked in the PEB cache
 *   dword1 complexity 1 = NULLREGION, 2 = SIMPLEREGION (anything else makes gdi32 ask the kernel)
 *   dword2..5         left, top, right, bottom
 * Written by gdi32!CreateRectRgn / OffsetRgn, read by gdi32!GetRgnBox / OffsetRgn, consumed and the 0x20 bit
 * cleared by win32kbase!RGNOBJAPI::RGNOBJAPI (NtGdiEqualRgn builds one for each operand). */
#include "r2.h"

struct rattr { unsigned flags, cx; int box[4]; };
static int rg_attr(HRGN r, struct rattr *a) {
    struct gh_cell c;
    unsigned v[6];
    if (!gh_cell_of(r, &c) || !c.puser) return 0;
    if (!k_try_read((void *)(ULONG_PTR)c.puser, v, sizeof v)) return 0;
    a->flags = v[0]; a->cx = v[1];
    for (int i = 0; i < 4; i++) a->box[i] = (int)v[2 + i];
    return 1;
}

struct rvec { const char *name; int x1, y1, x2, y2; unsigned cx; int box[4]; };
static void rv_check(const struct rvec *v) {
    GH_NEED();
    HRGN r = U(CreateRectRgn)(v->x1, v->y1, v->x2, v->y2);
    struct rattr a;
    RECT rc = { 77, 77, 77, 77 };
    REQUIRE(r);
    CHECKF(rg_attr(r, &a), "%s: no RGNATTR", v->name);
    /* gdi32!CreateRectRgn: swaps reversed corners, empty box -> {1, 0,0,0,0}, otherwise {2, l,t,r,b}; Flags = 0x30 */
    CHECKF(a.flags == 0x30, "%s: Flags 0x%x", v->name, a.flags);
    CHECKF(a.cx == v->cx, "%s: complexity %u want %u", v->name, a.cx, v->cx);
    CHECKF(a.box[0] == v->box[0] && a.box[1] == v->box[1] && a.box[2] == v->box[2] && a.box[3] == v->box[3],
           "%s: box %d,%d,%d,%d want %d,%d,%d,%d", v->name, a.box[0], a.box[1], a.box[2], a.box[3], v->box[0], v->box[1], v->box[2], v->box[3]);
    CHECKF(U(GetRgnBox)(r, &rc) == (int)v->cx, "%s: GetRgnBox type", v->name);
    CHECK(rc.left == v->box[0] && rc.top == v->box[1] && rc.right == v->box[2] && rc.bottom == v->box[3]);
    CHECK(U(DeleteObject)(r));
}
#define RV(id, x1, y1, x2, y2, cx, l, t, rr, b) \
    static const struct rvec rv_##id = { #id, x1, y1, x2, y2, cx, { l, t, rr, b } }; \
    TEST(r2_gdi_cell_per_type_rgnattr_##id) { rv_check(&rv_##id); }
RV(plain,         0, 0, 10, 10,   2, 0, 0, 10, 10)
RV(offset_origin, 1, 2, 30, 40,   2, 1, 2, 30, 40)
RV(reversed_both, 10, 10, 0, 0,   2, 0, 0, 10, 10)
RV(reversed_x,    10, 0, 0, 10,   2, 0, 0, 10, 10)
RV(reversed_y,    0, 10, 10, 0,   2, 0, 0, 10, 10)
RV(negative,      -100, -50, 100, 50, 2, -100, -50, 100, 50)
RV(unit,          0, 0, 1, 1,     2, 0, 0, 1, 1)
RV(extreme,       -0x8000000, -0x8000000, 0x7ffffff, 0x7ffffff, 2, -0x8000000, -0x8000000, 0x7ffffff, 0x7ffffff)
RV(empty_width,   5, 5, 5, 9,     1, 0, 0, 0, 0)
RV(empty_height,  5, 5, 9, 5,     1, 0, 0, 0, 0)
RV(empty_point,   3, 4, 3, 4,     1, 0, 0, 0, 0)
RV(empty_origin,  0, 0, 0, 0,     1, 0, 0, 0, 0)

struct rbad { const char *name; int x1, y1, x2, y2; };
static void rb_check(const struct rbad *b) {
    GH_NEED();
    int g0 = r2_gr(0);
    SetLastError(0);
    HRGN r = U(CreateRectRgn)(b->x1, b->y1, b->x2, b->y2);
    DWORD e = GetLastError();
    /* gdi32!CreateRectRgn rejects a box outside +-0x8000000 before any kernel call: NULL + ERROR_INVALID_PARAMETER */
    CHECKF(r == NULL, "%s: got %p", b->name, r);
    CHECKF(e == ERROR_INVALID_PARAMETER, "%s: last error %lu", b->name, e);
    CHECKF(r2_gr(0) == g0, "%s: GR_GDIOBJECTS %d -> %d", b->name, g0, r2_gr(0));
    if (r) U(DeleteObject)(r);
}
#define RB(id, x1, y1, x2, y2) \
    static const struct rbad rb_##id = { #id, x1, y1, x2, y2 }; \
    TEST(r2_gdi_cell_per_type_rgnattr_reject_##id) { rb_check(&rb_##id); }
RB(left_low,  -0x8000001, 0, 5, 5)
RB(top_low,   0, -0x8000001, 5, 5)
RB(right_high, 0, 0, 0x8000000, 5)
RB(bottom_high, 0, 0, 5, 0x8000000)
RB(reversed_left_low, 5, 5, -0x8000001, 0)
RB(reversed_bottom_high, 5, 0x8000000, 0, 0)

TEST(r2_gdi_cell_per_type_rgnattr_kernel_sync_clears_dirty_bit) {
    GH_NEED();
    REQUIRE(U(EqualRgn));
    HRGN a = U(CreateRectRgn)(1, 2, 20, 30), b = U(CreateRectRgn)(1, 2, 20, 30);
    struct rattr x, y;
    REQUIRE(a && b);
    CHECK(rg_attr(a, &x) && x.flags == 0x30);
    CHECK(U(EqualRgn)(a, b));
    /* both operands were locked through RGNOBJAPI, which applies the box and clears bit 0x20 */
    CHECK(rg_attr(a, &x) && rg_attr(b, &y));
    CHECKF(x.flags == 0x10 && y.flags == 0x10, "Flags after EqualRgn: 0x%x / 0x%x", x.flags, y.flags);
    CHECK(x.cx == 2 && x.box[0] == 1 && x.box[1] == 2 && x.box[2] == 20 && x.box[3] == 30);
    U(DeleteObject)(a);
    U(DeleteObject)(b);
}
TEST(r2_gdi_cell_per_type_rgnattr_offset_marks_dirty_and_moves_box) {
    GH_NEED();
    REQUIRE(U(EqualRgn));
    HRGN a = U(CreateRectRgn)(1, 2, 20, 30), same = U(CreateRectRgn)(1, 2, 20, 30), moved = U(CreateRectRgn)(11, -8, 30, 20);
    struct rattr x;
    REQUIRE(a && same && moved);
    U(EqualRgn)(a, same);
    CHECK(rg_attr(a, &x) && x.flags == 0x10);
    /* gdi32!OffsetRgn edits the box in the attribute page itself and ORs in 0x20 */
    CHECK(U(OffsetRgn)(a, 10, -10) == SIMPLEREGION);
    CHECK(rg_attr(a, &x));
    CHECKF(x.flags == 0x30, "Flags 0x%x", x.flags);
    CHECKF(x.cx == 2 && x.box[0] == 11 && x.box[1] == -8 && x.box[2] == 30 && x.box[3] == 20, "box %d,%d,%d,%d", x.box[0], x.box[1], x.box[2], x.box[3]);
    CHECK(U(EqualRgn)(a, moved));
    CHECK(!U(EqualRgn)(a, same));
    CHECK(rg_attr(a, &x) && x.flags == 0x10);
    U(DeleteObject)(a); U(DeleteObject)(same); U(DeleteObject)(moved);
}
TEST(r2_gdi_cell_per_type_rgnattr_offset_empty_region_untouched) {
    GH_NEED();
    HRGN a = U(CreateRectRgn)(4, 4, 4, 9);
    struct rattr x, y;
    REQUIRE(a);
    CHECK(rg_attr(a, &x));
    CHECK(U(OffsetRgn)(a, 100, 100) == NULLREGION);
    CHECK(rg_attr(a, &y));
    CHECKF(x.flags == y.flags && x.cx == y.cx && !memcmp(x.box, y.box, sizeof x.box), "empty region attr changed by OffsetRgn");
    RECT rc = { 1, 1, 1, 1 };
    CHECK(U(GetRgnBox)(a, &rc) == NULLREGION);
    CHECK(rc.left == 0 && rc.top == 0 && rc.right == 0 && rc.bottom == 0);
    U(DeleteObject)(a);
}
TEST(r2_gdi_cell_per_type_rgnattr_offset_out_of_range_fails_locally) {
    GH_NEED();
    HRGN a = U(CreateRectRgn)(0, 0, 10, 10);
    struct rattr x, y;
    REQUIRE(a);
    CHECK(rg_attr(a, &x));
    /* the shifted right edge would leave +-0x8000000: OffsetRgn returns ERROR (0) without touching the box */
    CHECK(U(OffsetRgn)(a, 0x7ffffff, 0) == 0);
    CHECK(U(OffsetRgn)(a, 0, 0x7ffffff) == 0);
    CHECK(U(OffsetRgn)(a, -0x7fffffff, 0) == 0);
    CHECK(rg_attr(a, &y));
    CHECKF(x.flags == y.flags && x.cx == y.cx && !memcmp(x.box, y.box, sizeof x.box), "attr changed by a rejected OffsetRgn");
    U(DeleteObject)(a);
}
TEST(r2_gdi_cell_per_type_rgnattr_offset_zero_keeps_box) {
    GH_NEED();
    HRGN a = U(CreateRectRgn)(3, 4, 13, 14);
    struct rattr x;
    REQUIRE(a);
    CHECK(U(OffsetRgn)(a, 0, 0) == SIMPLEREGION);
    CHECK(rg_attr(a, &x) && x.cx == 2 && x.box[0] == 3 && x.box[1] == 4 && x.box[2] == 13 && x.box[3] == 14);
    U(DeleteObject)(a);
}
TEST(r2_gdi_cell_per_type_rgnattr_getrgnbox_trusts_attr) {
    GH_NEED();
    HRGN a = U(CreateRectRgn)(0, 0, 10, 10);
    struct gh_cell c;
    unsigned saved;
    RECT rc;
    REQUIRE(a);
    CHECK(gh_cell_of(a, &c) && c.puser);
    if (!c.puser) { U(DeleteObject)(a); return; }
    /* gdi32!GetRgnBox answers from the page when complexity is 2: the page is the source of truth in user mode.
     * The page is writable by the process; restore it before the region is used by the kernel. */
    unsigned *p = (unsigned *)(ULONG_PTR)c.puser;
    if (!k_try_read(&p[4], &saved, 4)) { U(DeleteObject)(a); t_skip("attr page unreadable"); return; }
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof mbi) || mbi.Protect != PAGE_READWRITE) { U(DeleteObject)(a); t_skip("attr page not writable"); return; }
    p[4] = 4242;
    memset(&rc, 0, sizeof rc);
    CHECK(U(GetRgnBox)(a, &rc) == SIMPLEREGION);
    CHECKF(rc.right == 4242, "GetRgnBox right=%ld, the attr page says 4242", rc.right);
    p[4] = saved;
    CHECK(U(GetRgnBox)(a, &rc) == SIMPLEREGION && rc.right == 10);
    U(DeleteObject)(a);
}
TEST(r2_gdi_cell_per_type_rgnattr_complexity_other_than_1_2_asks_kernel) {
    GH_NEED();
    /* gdi32!CreateRectRgn only writes the box into the attribute page (Flags 0x20, newer than the kernel): the kernel's REGION keeps
     * the placeholder it was created with (0,0,1,1), or the box of the object that used the cell before when the handle came out of
     * the PEB cache. Empty the cache first so the region below is a fresh kernel object with the placeholder. */
    HGDIOBJ held[40];
    int nheld = r2_drain(R2_T_REGION, held, 40);
    HRGN a = U(CreateRectRgn)(0, 0, 10, 10);
    struct gh_cell c;
    RECT rc;
    REQUIRE(a);
    CHECK(gh_cell_of(a, &c) && c.puser);
    if (!c.puser) { U(DeleteObject)(a); r2_release(held, nheld); return; }
    unsigned *p = (unsigned *)(ULONG_PTR)c.puser, saved, savedr;
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof mbi) || mbi.Protect != PAGE_READWRITE) { U(DeleteObject)(a); r2_release(held, nheld); t_skip("attr page not writable"); return; }
    if (!k_try_read(&p[1], &saved, 4) || !k_try_read(&p[4], &savedr, 4)) { U(DeleteObject)(a); r2_release(held, nheld); t_skip("attr page unreadable"); return; }
    /* complexity 0 is neither 1 nor 2: gdi32!GetRgnBox falls through to NtGdiGetRgnBox, which reads the kernel's REGION and so
     * ignores the (poisoned) right edge in the page */
    p[1] = 0; p[4] = 999;
    memset(&rc, 0, sizeof rc);
    int t = U(GetRgnBox)(a, &rc);
    CHECKF(t == SIMPLEREGION && rc.left == 0 && rc.top == 0 && rc.right == 1 && rc.bottom == 1, "type %d kernel box %ld,%ld,%ld,%ld", t, rc.left, rc.top, rc.right, rc.bottom);
    p[1] = saved; p[4] = savedr;
    U(DeleteObject)(a);
    r2_release(held, nheld);
}

/* whatever the operation, if the page claims complexity 1/2 it agrees with the answer the API gives */
static void rg_consistent(HRGN r, const char *what) {
    struct rattr a;
    RECT rc = { 5, 5, 5, 5 };
    int t = U(GetRgnBox)(r, &rc);
    if (!rg_attr(r, &a)) { t_note("%s: region has no attr", what); return; }
    if (a.cx == 1) CHECKF(t == NULLREGION, "%s: attr says null, GetRgnBox=%d", what, t);
    else if (a.cx == 2) CHECKF(t == SIMPLEREGION && rc.left == a.box[0] && rc.top == a.box[1] && rc.right == a.box[2] && rc.bottom == a.box[3], "%s: attr box vs GetRgnBox", what);
    else t_note("%s: complexity dword %u flags 0x%x", what, a.cx, a.flags);
}
TEST(r2_gdi_cell_per_type_rgnattr_consistent_after_setrectrgn) {
    GH_NEED();
    HRGN a = U(CreateRectRgn)(0, 0, 10, 10);
    REQUIRE(a);
    CHECK(U(SetRectRgn)(a, 7, 8, 70, 80));
    rg_consistent(a, "SetRectRgn");
    RECT rc;
    CHECK(U(GetRgnBox)(a, &rc) == SIMPLEREGION && rc.left == 7 && rc.top == 8 && rc.right == 70 && rc.bottom == 80);
    CHECK(U(SetRectRgn)(a, 7, 8, 7, 80));
    rg_consistent(a, "SetRectRgn empty");
    CHECK(U(GetRgnBox)(a, &rc) == NULLREGION);
    U(DeleteObject)(a);
}
TEST(r2_gdi_cell_per_type_rgnattr_consistent_after_combine) {
    GH_NEED();
    HRGN a = U(CreateRectRgn)(0, 0, 10, 10), b = U(CreateRectRgn)(20, 0, 30, 10), d = U(CreateRectRgn)(0, 0, 1, 1);
    REQUIRE(a && b && d);
    CHECK(U(CombineRgn)(d, a, b, RGN_OR) == COMPLEXREGION);
    rg_consistent(d, "CombineRgn OR");
    RECT rc;
    CHECK(U(GetRgnBox)(d, &rc) == COMPLEXREGION && rc.left == 0 && rc.right == 30 && rc.bottom == 10);
    CHECK(U(CombineRgn)(d, a, b, RGN_AND) == NULLREGION);
    rg_consistent(d, "CombineRgn AND");
    CHECK(U(CombineRgn)(d, a, a, RGN_COPY) == SIMPLEREGION);
    rg_consistent(d, "CombineRgn COPY");
    U(DeleteObject)(a); U(DeleteObject)(b); U(DeleteObject)(d);
}
TEST(r2_gdi_cell_per_type_rgnattr_other_creators_consistent) {
    GH_NEED();
    HRGN e = U(CreateEllipticRgn)(0, 0, 20, 20), rr = U(CreateRoundRectRgn)(0, 0, 20, 20, 6, 6);
    POINT pt[3] = { { 0, 0 }, { 10, 0 }, { 5, 10 } };
    HRGN p = U(CreatePolygonRgn)(pt, 3, WINDING);
    REQUIRE(e && rr && p);
    rg_consistent(e, "CreateEllipticRgn");
    rg_consistent(rr, "CreateRoundRectRgn");
    rg_consistent(p, "CreatePolygonRgn");
    RECT rc;
    int er = U(GetRgnBox)(e, &rc);
    /* Windows 10: the elliptic region of (0,0,20,20) is a complex region whose box ends one unit inside the requested rectangle */
    CHECKF(er == COMPLEXREGION && rc.left == 0 && rc.top == 0 && rc.right == 19 && rc.bottom == 19, "ellipse type %d box %ld,%ld,%ld,%ld", er, rc.left, rc.top, rc.right, rc.bottom);
    U(DeleteObject)(e); U(DeleteObject)(rr); U(DeleteObject)(p);
}
TEST(r2_gdi_cell_per_type_rgnattr_distinct_boxes_per_region) {
    GH_NEED();
    HRGN rs[10];
    for (int i = 0; i < 10; i++) { rs[i] = U(CreateRectRgn)(i, 2 * i, i + 20, 2 * i + 30); REQUIRE(rs[i]); }
    for (int i = 0; i < 10; i++) {
        struct rattr a;
        CHECK(rg_attr(rs[i], &a));
        CHECKF(a.box[0] == i && a.box[1] == 2 * i && a.box[2] == i + 20 && a.box[3] == 2 * i + 30, "region %d attr box %d,%d,%d,%d", i, a.box[0], a.box[1], a.box[2], a.box[3]);
    }
    for (int i = 0; i < 10; i++) U(DeleteObject)(rs[i]);
}
