/* The PEB handle cache (PEB+0x140..0x230) and the OBJECTATTR behind solid-brush and rectangular-region cells.
 *   delete  : win32kbase!NtGdiDeleteObjectApp -> bDeleteBrush / RGNOBJAPI::bDeleteRGNOBJAPI -> bPEBCacheHandle
 *   create  : gdi32!CreateSolidBrush -> hGetPEBHandle(0, color) ; gdi32!CreateRectRgn pops PEB+0x1e8..0x228
 * Brush OBJECTATTR: dword0 AttrFlags (bit0 parked, bit2 colour changed since the kernel last looked), dword1 lbColor.
 * DeleteObject of both is queued in the GDI batch: every test below flushes (r2_del) before it looks at the PEB cache.
 * Region OBJECTATTR (RGNATTR): dword0 Flags (0x10 valid, 0x20 newer than the kernel), dword1 complexity (1 null,
 * 2 simple), dword2..5 the box. */
#include "r2.h"

static int rc_need(void) {
    struct r2_cache c;
    if (!gh_connect()) { t_skip("PEB->GdiSharedHandleTable unavailable"); return 0; }
    if (!r2_cache_read(&c)) { t_skip("PEB cache area unreadable"); return 0; }
    return 1;
}
#define RC_NEED() do { if (!rc_need()) return; } while (0)

TEST(r2_gdi_cell_per_type_cache_lock_idle) {
    RC_NEED();
    struct r2_cache c;
    r2_cache_read(&c);
    /* both bPEBCacheHandle and hGetPEBHandle clear the lock qword before returning */
    CHECKF(c.lock == 0, "cache lock qword 0x%llx", c.lock);
}
TEST(r2_gdi_cell_per_type_cache_counts_match_slots) {
    RC_NEED();
    struct r2_cache c;
    r2_cache_read(&c);
    CHECKF((int)(c.count[0] + c.count[1] + c.count[2] + c.pad) == r2_cache_nonzero(&c), "counts %u/%u/%u, non-zero slots %d", c.count[0], c.count[1], c.count[2], r2_cache_nonzero(&c));
}
TEST(r2_gdi_cell_per_type_cache_entries_are_valid_cells) {
    RC_NEED();
    struct r2_cache c;
    r2_cache_read(&c);
    for (int i = 0; i < R2_CACHE_SLOTS; i++) {
        struct gh_cell cell;
        if (!c.slot[i]) continue;
        unsigned v = (unsigned)c.slot[i];
        CHECKF(gh_read(v & 0xffff, &cell), "slot %d value 0x%llx", i, c.slot[i]);
        /* hGetPEBHandle accepts an entry only if Objt is 0x10 (brush) or 4 (region), HIWORD matches, owner is the pid and AttrFlags&0xb == 1 */
        CHECKF(cell.objt == 0x10 || cell.objt == 4, "slot %d cell objt 0x%x", i, cell.objt);
        CHECKF(cell.upper == (v >> 16), "slot %d value 0x%x cell upper 0x%04x", i, v, cell.upper);
        CHECKF((cell.owner & ~1u) == GH_PID(), "slot %d owner 0x%x", i, cell.owner);
        unsigned af = 0;
        CHECKF(r2_attr(&cell, 0, &af) && (af & 0xb) == 1, "slot %d AttrFlags 0x%x", i, af);
    }
}
TEST(r2_gdi_cell_per_type_cache_slots_hold_distinct_cells) {
    RC_NEED();
    struct r2_cache c;
    r2_cache_read(&c);
    for (int i = 0; i < R2_CACHE_SLOTS; i++)
        for (int j = i + 1; j < R2_CACHE_SLOTS; j++)
            if (c.slot[i] && c.slot[j]) CHECKF((c.slot[i] & 0xffff) != (c.slot[j] & 0xffff), "slots %d and %d cache cell 0x%llx twice", i, j, c.slot[i] & 0xffff);
}
TEST(r2_gdi_cell_per_type_cache_invariants_after_churn) {
    RC_NEED();
    HGDIOBJ b[30], r[30];
    for (int i = 0; i < 30; i++) { b[i] = (HGDIOBJ)U(CreateSolidBrush)(RGB(i, 2 * i, 3 * i)); r[i] = (HGDIOBJ)U(CreateRectRgn)(i, i, i + 5, i + 9); }
    for (int i = 0; i < 30; i += 2) { r2_del(b[i]); r2_del(r[i]); }
    struct r2_cache c;
    r2_cache_read(&c);
    CHECK(c.lock == 0);
    CHECKF((int)(c.count[0] + c.count[1] + c.count[2] + c.pad) == r2_cache_nonzero(&c), "counts %u/%u/%u, slots %d", c.count[0], c.count[1], c.count[2], r2_cache_nonzero(&c));
    CHECKF(c.count[2] <= 9, "region cache holds %u (window PEB+0x1e8..0x230 has 9 slots)", c.count[2]);
    for (int i = 1; i < 30; i += 2) { r2_del(b[i]); r2_del(r[i]); }
    r2_cache_read(&c);
    CHECK(c.lock == 0);
    CHECKF((int)(c.count[0] + c.count[1] + c.count[2] + c.pad) == r2_cache_nonzero(&c), "counts %u/%u/%u, slots %d", c.count[0], c.count[1], c.count[2], r2_cache_nonzero(&c));
    CHECKF(c.count[2] <= 9, "region cache holds %u", c.count[2]);
}

/* ---- solid brushes ------------------------------------------------------------------------------ */
static HBRUSH sb(COLORREF c) { return U(CreateSolidBrush)(c); }

TEST(r2_gdi_cell_per_type_brush_delete_is_parked) {
    RC_NEED();
    HGDIOBJ held[40];
    int n = r2_drain(R2_T_BRUSH, held, 40);
    HBRUSH b = sb(RGB(1, 2, 3));
    REQUIRE(b);
    struct gh_cell before, after;
    CHECK(gh_cell_of(b, &before));
    unsigned idx = GH_IDX(b);
    int g0 = r2_gr(0);
    CHECK(r2_del(b));
    struct r2_cache c;
    r2_cache_read(&c);
    CHECKF(c.count[R2_T_BRUSH] == 1, "brush cache count %u", c.count[R2_T_BRUSH]);
    CHECKF(r2_cache_find(&c, idx) >= 0, "deleted solid brush (cell 0x%x) not in the PEB cache", idx);
    CHECK(gh_read(idx, &after));
    /* the cell stays live: the process still owns it, so GetGuiResources does not drop */
    CHECKF(after.objt == 0x10 && (after.owner & ~1u) == GH_PID(), "cell objt 0x%x owner 0x%x", after.objt, after.owner);
    CHECKF(r2_gr(0) == g0, "GR_GDIOBJECTS %d -> %d across a parked delete", g0, r2_gr(0));
    r2_release(held, n);
}
TEST(r2_gdi_cell_per_type_brush_parked_handle_is_dead) {
    RC_NEED();
    HGDIOBJ held[40];
    int n = r2_drain(R2_T_BRUSH, held, 40);
    HBRUSH b = sb(RGB(4, 5, 6));
    REQUIRE(b);
    struct gh_cell p1, p2;
    CHECK(gh_cell_of(b, &p1));
    r2_del(b);
    CHECK(gh_read(GH_IDX(b), &p1));
    /* HIWORD of the old handle no longer matches the (bumped) cell, so every gdi32 validator refuses it */
    CHECK(U(GetObjectType)(b) == 0);
    CHECKF(U(DeleteObject)(b) == 0, "second DeleteObject on a parked handle succeeded");
    CHECK(gh_read(GH_IDX(b), &p2));
    CHECKF(p1.upper == p2.upper && p1.objt == p2.objt && p1.puser == p2.puser, "parked cell changed by operations on the dead handle");
    LOGBRUSH lb;
    SetLastError(0);
    CHECKF(U(GetObjectW)(b, sizeof lb, &lb) == 0, "GetObject on a parked handle succeeded");
    r2_release(held, n);
}
TEST(r2_gdi_cell_per_type_brush_cached_handle_is_reused) {
    RC_NEED();
    HGDIOBJ held[40];
    int n = r2_drain(R2_T_BRUSH, held, 40);
    HBRUSH a = sb(RGB(10, 20, 30));
    REQUIRE(a);
    unsigned idx = GH_IDX(a);
    r2_del(a);
    struct r2_cache c0, c1;
    r2_cache_read(&c0);
    int slot = r2_cache_find(&c0, idx);
    REQUIRE(slot >= 0);
    unsigned cached = (unsigned)c0.slot[slot];
    HBRUSH b = sb(RGB(10, 20, 30));
    REQUIRE(b);
    CHECKF(r2_lo32(b) == cached, "CreateSolidBrush returned 0x%x, cache held 0x%x", r2_lo32(b), cached);
    CHECKF(r2_lo32(b) != r2_lo32(a), "reused handle identical to the deleted one");
    CHECKF(GH_IDX(b) == idx && GH_UNIQ(b) == ((GH_UNIQ(a) + 1) & 0xff), "idx 0x%x uniq 0x%x (old uniq 0x%x)", GH_IDX(b), GH_UNIQ(b), GH_UNIQ(a));
    if ((ULONG_PTR)b != (ULONG_PTR)(LONG_PTR)(int)r2_lo32(b)) t_info("reused brush handle %p is zero-extended, not sign-extended (cache stores a 32-bit value)", b);
    r2_cache_read(&c1);
    CHECKF(c1.count[R2_T_BRUSH] == 0 && r2_cache_find(&c1, idx) < 0, "cache still lists cell 0x%x (count %u)", idx, c1.count[R2_T_BRUSH]);
    CHECK(U(GetObjectType)(b) == OBJ_BRUSH);
    CHECK(U(GetObjectType)(a) == 0);
    r2_del(b);
    r2_release(held, n);
}
TEST(r2_gdi_cell_per_type_brush_reuse_same_colour_leaves_attr_clean) {
    RC_NEED();
    HGDIOBJ held[40];
    int n = r2_drain(R2_T_BRUSH, held, 40);
    COLORREF col = RGB(40, 50, 60);
    HBRUSH a = sb(col);
    REQUIRE(a);
    r2_del(a);
    HBRUSH b = sb(col);
    struct gh_cell c;
    unsigned af = 0xdead, lb = 0xdead;
    REQUIRE(b);
    CHECK(gh_cell_of(b, &c));
    /* bPEBCacheHandle clears bits 1 and 3 and sets 0; hGetPEBHandle clears 0 and sets 2 only when lbColor differs */
    CHECK(r2_attr(&c, 0, &af) && r2_attr(&c, 1, &lb));
    CHECKF((af & 0xb) == 0, "AttrFlags 0x%x after reusing a parked brush with the same colour", af);
    CHECKF(lb == col, "lbColor 0x%x want 0x%lx", lb, col);
    r2_del(b);
    r2_release(held, n);
}
TEST(r2_gdi_cell_per_type_brush_reuse_new_colour_marks_attr) {
    RC_NEED();
    HGDIOBJ held[40];
    int n = r2_drain(R2_T_BRUSH, held, 40);
    HBRUSH a = sb(RGB(40, 50, 60));
    REQUIRE(a);
    r2_del(a);
    COLORREF col = RGB(200, 100, 50);
    HBRUSH b = sb(col);
    struct gh_cell c;
    unsigned af = 0xdead, lb = 0xdead;
    REQUIRE(b);
    CHECK(gh_cell_of(b, &c));
    CHECK(r2_attr(&c, 0, &af) && r2_attr(&c, 1, &lb));
    CHECKF((af & 0xf) == 4, "AttrFlags 0x%x, want bit 2 only among 0..3 (colour changed, kernel must re-read)", af);
    CHECKF(lb == col, "lbColor 0x%x want 0x%lx", lb, col);
    /* the kernel notices the flag: the colour the API reports is the new one */
    LOGBRUSH l;
    memset(&l, 0, sizeof l);
    CHECK(U(GetObjectW)(b, sizeof l, &l) == sizeof l);
    CHECKF(l.lbColor == col, "GetObject lbColor 0x%lx want 0x%lx", l.lbColor, col);
    CHECK(l.lbStyle == BS_SOLID);
    r2_attr(&c, 0, &af);
    t_note("AttrFlags after the kernel looked: 0x%x", af);
    r2_del(b);
    r2_release(held, n);
}
TEST(r2_gdi_cell_per_type_brush_reuse_new_colour_draws_new_colour) {
    RC_NEED();
    REQUIRE(U(CreateDIBSection));
    HGDIOBJ held[40];
    int n = r2_drain(R2_T_BRUSH, held, 40);
    BITMAPINFO bi; void *bits = NULL;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = 2; bi.bmiHeader.biHeight = -2;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    HBITMAP dib = U(CreateDIBSection)(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    HDC dc = U(CreateCompatibleDC)(NULL);
    if (!dib || !bits || !dc) { if (dc) U(DeleteDC)(dc); if (dib) r2_del(dib); r2_release(held, n); t_skip("no DIB"); return; }
    HGDIOBJ old = U(SelectObject)(dc, dib);
    HBRUSH a = sb(RGB(255, 0, 0));
    r2_del(a);
    HBRUSH b = sb(RGB(10, 200, 30));
    HGDIOBJ ob = U(SelectObject)(dc, b);
    CHECK(U(PatBlt)(dc, 0, 0, 2, 2, PATCOPY));
    U(GdiFlush)();
    /* the pending colour (AttrFlags bit 2) is honoured when the brush is realized */
    CHECKF((((unsigned *)bits)[0] & 0xffffff) == 0x0ac81e, "pixel 0x%08x want 0x000ac81e", ((unsigned *)bits)[0]);
    U(SelectObject)(dc, ob);
    U(SelectObject)(dc, old);
    r2_del(b);
    U(DeleteDC)(dc);
    r2_del(dib);
    r2_release(held, n);
}
TEST(r2_gdi_cell_per_type_brush_fresh_colour_in_attr) {
    RC_NEED();
    static const COLORREF cols[] = { 0x000000, 0xffffff, RGB(1, 2, 3), RGB(255, 0, 0), RGB(0, 255, 0), RGB(0, 0, 255), RGB(128, 64, 32) };
    HGDIOBJ held[40];
    int n = r2_drain(R2_T_BRUSH, held, 40);
    for (size_t i = 0; i < sizeof cols / sizeof cols[0]; i++) {
        HBRUSH b = sb(cols[i]);
        struct gh_cell c;
        unsigned af = 0xdead, lb = 0xdead;
        if (!b) { CHECK(0); continue; }
        CHECK(gh_cell_of(b, &c));
        CHECKF(r2_attr(&c, 1, &lb) && lb == cols[i], "colour 0x%lx: BRUSHATTR.lbColor 0x%x", cols[i], lb);
        CHECKF(r2_attr(&c, 0, &af) && (af & 1) == 0, "colour 0x%lx: live brush has the parked bit, AttrFlags 0x%x", cols[i], af);
        r2_del(b);
        /* each iteration pops the handle just parked, so lbColor must be rewritten with the new colour */
    }
    r2_release(held, n);
}
TEST(r2_gdi_cell_per_type_brush_cache_capacity_then_release) {
    RC_NEED();
    HGDIOBJ held[40], bs[40];
    int n = r2_drain(R2_T_BRUSH, held, 40);
    int g0 = r2_scan(GH_PID(), (int[32]){0});
    unsigned idx[40];
    for (int i = 0; i < 40; i++) { bs[i] = sb(RGB(i, 0, 0)); REQUIRE(bs[i]); idx[i] = GH_IDX(bs[i]); }
    for (int i = 0; i < 40; i++) r2_del(bs[i]);
    struct r2_cache c;
    r2_cache_read(&c);
    int parked = 0, freed = 0;
    for (int i = 0; i < 40; i++) {
        struct gh_cell cell;
        gh_read(idx[i], &cell);
        if (r2_cache_find(&c, idx[i]) >= 0) { parked++; CHECK(cell.objt == 0x10); }
        else { freed++; CHECKF(cell.objt == 0, "uncached brush cell 0x%x still Objt 0x%x", idx[i], cell.objt); }
    }
    CHECKF((unsigned)parked == c.count[R2_T_BRUSH], "parked %d, cache count %u", parked, c.count[R2_T_BRUSH]);
    /* brush and pen caches share PEB+0x158..0x1e8: 18 qwords */
    CHECKF(parked >= 1 && parked <= 18, "parked %d", parked);
    CHECK(parked + freed == 40);
    t_note("brush cache capacity observed: %d parked, %d released", parked, freed);
    int g1 = r2_scan(GH_PID(), (int[32]){0});
    CHECKF(g1 - g0 == parked, "population grew by %d, parked %d", g1 - g0, parked);
    r2_release(held, n);
}

/* ---- rectangular regions ------------------------------------------------------------------------ */
TEST(r2_gdi_cell_per_type_region_delete_is_parked) {
    RC_NEED();
    HGDIOBJ held[40];
    int n = r2_drain(R2_T_REGION, held, 40);
    HRGN r = U(CreateRectRgn)(1, 2, 30, 40);
    REQUIRE(r);
    unsigned idx = GH_IDX(r);
    struct gh_cell before, after;
    CHECK(gh_cell_of(r, &before));
    int g0 = r2_gr(0);
    CHECK(r2_del(r));
    struct r2_cache c;
    r2_cache_read(&c);
    CHECKF(c.count[R2_T_REGION] == 1 && r2_cache_find(&c, idx) >= 0, "region cache count %u, cell 0x%x found %d", c.count[R2_T_REGION], idx, r2_cache_find(&c, idx));
    CHECK(gh_read(idx, &after));
    CHECKF(after.objt == 4 && (after.owner & ~1u) == GH_PID(), "cell objt 0x%x owner 0x%x", after.objt, after.owner);
    CHECKF(((after.upper >> 8) & 0xff) == ((GH_UNIQ(r) + 1) & 0xff), "reuse byte 0x%x old handle 0x%x", after.upper >> 8, GH_UNIQ(r));
    CHECKF(r2_gr(0) == g0, "GR_GDIOBJECTS %d -> %d across a parked region delete", g0, r2_gr(0));
    CHECK(U(GetObjectType)(r) == 0);
    unsigned af = 0;
    /* NtGdiDeleteObjectApp builds an RGNOBJAPI first, which applies the pending box and clears 0x20; then the parked bit is set */
    CHECKF(r2_attr(&after, 0, &af) && (af & 0x31) == 0x11, "RGNATTR.Flags 0x%x, want 0x11 (valid | parked, kernel up to date)", af);
    r2_release(held, n);
}
TEST(r2_gdi_cell_per_type_region_cached_handle_is_reused) {
    RC_NEED();
    HGDIOBJ held[40];
    int n = r2_drain(R2_T_REGION, held, 40);
    HRGN a = U(CreateRectRgn)(0, 0, 5, 5);
    REQUIRE(a);
    unsigned idx = GH_IDX(a);
    r2_del(a);
    struct r2_cache c0, c1;
    r2_cache_read(&c0);
    int slot = r2_cache_find(&c0, idx);
    REQUIRE(slot >= 0);
    HRGN b = U(CreateRectRgn)(5, 6, 50, 60);
    REQUIRE(b);
    CHECKF(r2_lo32(b) == (unsigned)c0.slot[slot], "CreateRectRgn returned 0x%x, cache held 0x%x", r2_lo32(b), (unsigned)c0.slot[slot]);
    r2_cache_read(&c1);
    CHECKF(c1.count[R2_T_REGION] == 0 && r2_cache_find(&c1, idx) < 0, "region cache still lists the popped handle");
    struct gh_cell cell;
    unsigned fl = 0, cx = 0, l, t, r, bt;
    CHECK(gh_cell_of(b, &cell));
    CHECK(r2_attr(&cell, 0, &fl));
    CHECKF(fl == 0x30, "RGNATTR.Flags 0x%x", fl);
    CHECK(r2_attr(&cell, 1, &cx) && cx == 2);
    CHECK(r2_attr(&cell, 2, &l) && r2_attr(&cell, 3, &t) && r2_attr(&cell, 4, &r) && r2_attr(&cell, 5, &bt));
    CHECKF(l == 5 && t == 6 && r == 50 && bt == 60, "attr box %d,%d,%d,%d", l, t, r, bt);
    RECT rc = { 9, 9, 9, 9 };
    CHECK(U(GetRgnBox)(b, &rc) == SIMPLEREGION);
    CHECK(rc.left == 5 && rc.top == 6 && rc.right == 50 && rc.bottom == 60);
    r2_del(b);
    r2_release(held, n);
}
TEST(r2_gdi_cell_per_type_region_cache_never_exceeds_window) {
    RC_NEED();
    HGDIOBJ held[40], rs[24];
    int n = r2_drain(R2_T_REGION, held, 40);
    unsigned idx[24];
    for (int i = 0; i < 24; i++) { rs[i] = (HGDIOBJ)U(CreateRectRgn)(0, 0, i + 1, i + 1); REQUIRE(rs[i]); idx[i] = GH_IDX(rs[i]); }
    for (int i = 0; i < 24; i++) r2_del(rs[i]);
    struct r2_cache c;
    r2_cache_read(&c);
    int parked = 0;
    for (int i = 0; i < 24; i++) {
        struct gh_cell cell;
        gh_read(idx[i], &cell);
        if (r2_cache_find(&c, idx[i]) >= 0) { parked++; CHECK(cell.objt == 4); }
        else CHECKF(cell.objt == 0, "uncached region cell 0x%x still Objt 0x%x", idx[i], cell.objt);
    }
    /* CreateRectRgn drains PEB+0x1e8..0x230: at most nine qwords */
    CHECKF(parked >= 1 && parked <= 9 && (unsigned)parked == c.count[R2_T_REGION], "parked %d cache count %u", parked, c.count[R2_T_REGION]);
    r2_release(held, n);
}
