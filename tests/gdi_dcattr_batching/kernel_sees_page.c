/* The page is only a mailbox: the kernel consumes it when a drawing or query syscall runs. These tests
 * change DC_ATTR state through the user-mode setters, run a kernel-consuming call, and validate the
 * result either as pixels of a 32bpp DIB section (exact on this format) or through a kernel getter
 * (NtGdiGetDCPoint, NtGdiGetDCDword, NtGdiGetDCObject via GetCurrentObject). Each drawing scenario is
 * run with the default batch limit and with batching defeated (limit 1): the results must agree. */
#include "dcattr.h"
#include <stdlib.h>

UDECL(BOOL, NtGdiGetDCPoint, (HDC, UINT, POINT *));
UDECL(int, NtGdiGetDCDword, (HDC, int, ULONG *));

static const DWORD g_limits[] = { 20, 1 };

/* ---- ROP2 ------------------------------------------------------------------------------------ */

static void rop2_row(struct fx *f, HPEN pen, int rop, int row, DWORD pre, DWORD want, const char *what) {
    for (int x = 0; x < f->w; x++) f->bits[row * f->w + x] = pre;
    HGDIOBJ op = U(SelectObject)(f->dc, pen);
    U(SetROP2)(f->dc, rop);
    U(MoveToEx)(f->dc, 0, row, NULL);
    U(LineTo)(f->dc, f->w, row);
    fx_sync();
    DWORD got = fx_px(f, f->w / 2, row);
    CHECKF(got == want, "%s rop2 %d: pixel 0x%06lx want 0x%06lx", what, rop, (unsigned long)got, (unsigned long)want);
    U(SelectObject)(f->dc, op);
}

TEST(gdi_dcattr_batching_kernel_applies_rop2_to_lineto) {
    FXT(f, 32, 16);
    HPEN pen = U(CreatePen)(PS_SOLID, 0, RGB(255, 0, 0));
    REQUIRE(pen);
    for (unsigned li = 0; li < 2; li++) {
        DWORD old = U(GdiSetBatchLimit)(g_limits[li]);
        /* LineTo -> NtGdiLineTo consumes ulDirty_ (jROP2 byte) when it draws. Sources are exact on 32bpp. */
        rop2_row(&f, pen, R2_BLACK,    1, 0x00ffffff, 0x000000, "black");
        rop2_row(&f, pen, R2_WHITE,    3, 0x00000000, 0xffffff, "white");
        rop2_row(&f, pen, R2_NOP,      5, 0x00123456, 0x123456, "nop");
        rop2_row(&f, pen, R2_COPYPEN,  7, 0x00ffffff, RGB2PX(255, 0, 0), "copy");
        rop2_row(&f, pen, R2_NOT,      9, 0x00ffffff, 0x000000, "not");
        rop2_row(&f, pen, R2_XORPEN,  11, 0x00ff0000, 0x000000, "xor");
        rop2_row(&f, pen, R2_MERGEPEN, 13, 0x0000ff00, 0xffff00, "or");
        rop2_row(&f, pen, R2_MASKPEN,  15, 0x00ffff00, 0xff0000, "and");
        U(GdiSetBatchLimit)(old);
    }
    U(SetROP2)(f.dc, R2_COPYPEN);
    U(DeleteObject)(pen);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_kernel_applies_rop2_changed_between_two_lines) {
    FXT(f, 32, 8);
    HPEN pen = U(CreatePen)(PS_SOLID, 0, RGB(0, 0, 255));
    REQUIRE(pen);
    HGDIOBJ op = U(SelectObject)(f.dc, pen);
    fx_fill(&f, 0x00ffffff);
    U(SetROP2)(f.dc, R2_COPYPEN);
    U(MoveToEx)(f.dc, 0, 1, NULL); U(LineTo)(f.dc, 32, 1);
    U(SetROP2)(f.dc, R2_BLACK);
    U(MoveToEx)(f.dc, 0, 3, NULL); U(LineTo)(f.dc, 32, 3);
    fx_sync();
    EXPECT_EQ(fx_px(&f, 16, 1), RGB2PX(0, 0, 255));
    EXPECT_EQ(fx_px(&f, 16, 3), 0);
    EXPECT_EQ(fx_px(&f, 16, 2), 0xffffff);
    U(SelectObject)(f.dc, op);
    U(SetROP2)(f.dc, R2_COPYPEN);
    U(DeleteObject)(pen);
    fx_close(&f);
}

/* ---- background colour / mode -------------------------------------------------------------------- */

static int opaque_rect(struct fx *f, int l, int t, int r, int b) {
    RECT rc = { l, t, r, b };
    return U(ExtTextOutW)(f->dc, 0, 0, ETO_OPAQUE, &rc, NULL, 0, NULL);
}

TEST(gdi_dcattr_batching_kernel_fills_opaque_rect_with_background_colour) {
    FXT(f, 32, 16);
    for (unsigned li = 0; li < 2; li++) {
        DWORD old = U(GdiSetBatchLimit)(g_limits[li]);
        fx_fill(&f, 0x00ffffff);
        U(SetBkColor)(f.dc, RGB(12, 34, 56));
        CHECK(opaque_rect(&f, 2, 2, 10, 6));
        fx_sync();
        /* ETO_OPAQUE with an empty string fills the rectangle with the DC's background colour. */
        EXPECT_EQ(fx_px(&f, 5, 4), RGB2PX(12, 34, 56));
        EXPECT_EQ(fx_px(&f, 1, 4), 0xffffff);
        EXPECT_EQ(fx_px(&f, 10, 4), 0xffffff);     /* right edge exclusive */
        EXPECT_EQ(fx_px(&f, 5, 6), 0xffffff);      /* bottom edge exclusive */
        U(GdiSetBatchLimit)(old);
    }
    fx_close(&f);
}

TEST(gdi_dcattr_batching_each_opaque_rect_keeps_its_own_background_colour) {
    FXT(f, 64, 8);
    for (unsigned li = 0; li < 2; li++) {
        DWORD old = U(GdiSetBatchLimit)(g_limits[li]);
        fx_fill(&f, 0x00ffffff);
        /* Queued ExtTextOut(rect) records capture the background colour at call time (ExtTextOutWImpl
         * copies DC_ATTR+0xb0 into the record), so a later SetBkColor must not recolour earlier rects. */
        static const COLORREF c[4] = { RGB(255, 0, 0), RGB(0, 255, 0), RGB(0, 0, 255), RGB(9, 8, 7) };
        for (int i = 0; i < 4; i++) { U(SetBkColor)(f.dc, c[i]); CHECK(opaque_rect(&f, i * 16, 0, i * 16 + 8, 8)); }
        fx_sync();
        for (int i = 0; i < 4; i++) EXPECT_EQ(fx_px(&f, i * 16 + 3, 3), RGB2PX(GetRValue(c[i]), GetGValue(c[i]), GetBValue(c[i])));
        for (int i = 0; i < 4; i++) EXPECT_EQ(fx_px(&f, i * 16 + 12, 3), 0xffffff);
        U(GdiSetBatchLimit)(old);
    }
    fx_close(&f);
}

static int count_px(struct fx *f, int row, DWORD v) {
    int n = 0;
    for (int x = 0; x < f->w; x++) n += (fx_px(f, x, row) == v);
    return n;
}

TEST(gdi_dcattr_batching_kernel_uses_bkmode_for_dash_gaps) {
    FXT(f, 128, 8);
    HPEN pen = U(CreatePen)(PS_DASH, 0, RGB(0, 0, 0));
    REQUIRE(pen);
    HGDIOBJ op = U(SelectObject)(f.dc, pen);
    U(SetBkColor)(f.dc, RGB(0, 200, 0));
    U(SetROP2)(f.dc, R2_COPYPEN);
    /* A dashed pen leaves gaps; OPAQUE paints them with the background colour, TRANSPARENT leaves them. */
    fx_fill(&f, 0x00ffffff);
    U(SetBkMode)(f.dc, OPAQUE);
    U(MoveToEx)(f.dc, 0, 2, NULL); U(LineTo)(f.dc, 128, 2);
    fx_sync();
    int green_opaque = count_px(&f, 2, RGB2PX(0, 200, 0)), black_opaque = count_px(&f, 2, 0);
    fx_fill(&f, 0x00ffffff);
    U(SetBkMode)(f.dc, TRANSPARENT);
    U(MoveToEx)(f.dc, 0, 2, NULL); U(LineTo)(f.dc, 128, 2);
    fx_sync();
    int green_transp = count_px(&f, 2, RGB2PX(0, 200, 0)), black_transp = count_px(&f, 2, 0);
    CHECKF(black_opaque > 0 && black_transp > 0, "dash segments missing: %d %d", black_opaque, black_transp);
    CHECKF(green_opaque > 0, "OPAQUE gaps not painted with bk colour (green=%d)", green_opaque);
    EXPECT_EQ(green_transp, 0);
    EXPECT_EQ(black_opaque, black_transp);          /* segments themselves do not depend on the mode */
    U(SetBkMode)(f.dc, OPAQUE);
    U(SelectObject)(f.dc, op);
    U(DeleteObject)(pen);
    fx_close(&f);
}

/* ---- pen and brush selection reach the kernel ------------------------------------------------- */

static const struct { DWORD rop; DWORD pre; DWORD want_solid; const char *n; } g_pat[] = {
    { PATCOPY,   0x00ffffff, 0, "patcopy" },     /* want_solid==0 -> "equals brush colour", filled below */
    { BLACKNESS, 0x00abcdef, 0x000000, "blackness" },
    { WHITENESS, 0x00abcdef, 0xffffff, "whiteness" },
    { DSTINVERT, 0x00f0f0f0, 0x0f0f0f, "dstinvert" },
    { PATINVERT, 0x00ffffff, 0xefdfcf, "patinvert" },   /* white ^ (0x10,0x20,0x30) */
};

TEST(gdi_dcattr_batching_kernel_uses_selected_brush_for_patblt) {
    FXT(f, 16, 16);
    HBRUSH br = U(CreateSolidBrush)(RGB(0x10, 0x20, 0x30));
    REQUIRE(br);
    HGDIOBJ ob = U(SelectObject)(f.dc, br);
    for (unsigned li = 0; li < 2; li++) {
        DWORD old = U(GdiSetBatchLimit)(g_limits[li]);
        for (unsigned i = 0; i < sizeof g_pat / sizeof *g_pat; i++) {
            fx_fill(&f, g_pat[i].pre);
            CHECK(U(PatBlt)(f.dc, 0, 0, 8, 8, g_pat[i].rop));
            fx_sync();
            DWORD want = i == 0 ? RGB2PX(0x10, 0x20, 0x30) : g_pat[i].want_solid;
            CHECKF(fx_px(&f, 4, 4) == want, "%s limit %lu: 0x%06lx want 0x%06lx", g_pat[i].n, (unsigned long)g_limits[li], (unsigned long)fx_px(&f, 4, 4), (unsigned long)want);
            EXPECT_EQ(fx_px(&f, 12, 12), g_pat[i].pre & 0xffffff);      /* outside the rectangle untouched */
        }
        U(GdiSetBatchLimit)(old);
    }
    U(SelectObject)(f.dc, ob);
    U(DeleteObject)(br);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_kernel_follows_brush_reselection_between_patblts) {
    FXT(f, 32, 8);
    HBRUSH a = U(CreateSolidBrush)(RGB(1, 2, 3)), b = U(CreateSolidBrush)(RGB(200, 100, 50));
    REQUIRE(a); REQUIRE(b);
    HGDIOBJ ob = U(SelectObject)(f.dc, a);
    fx_fill(&f, 0x00ffffff);
    U(PatBlt)(f.dc, 0, 0, 8, 8, PATCOPY);
    U(SelectObject)(f.dc, b);
    U(PatBlt)(f.dc, 8, 0, 8, 8, PATCOPY);
    U(SelectObject)(f.dc, a);
    U(PatBlt)(f.dc, 16, 0, 8, 8, PATCOPY);
    fx_sync();
    EXPECT_EQ(fx_px(&f, 4, 4), RGB2PX(1, 2, 3));
    EXPECT_EQ(fx_px(&f, 12, 4), RGB2PX(200, 100, 50));
    EXPECT_EQ(fx_px(&f, 20, 4), RGB2PX(1, 2, 3));
    EXPECT_EQ(fx_px(&f, 28, 4), 0xffffff);
    U(SelectObject)(f.dc, ob);
    U(DeleteObject)(a); U(DeleteObject)(b);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_getcurrentobject_sees_page_selection) {
    FXT(f, 8, 8);
    HBRUSH br = U(CreateSolidBrush)(RGB(5, 6, 7));
    HPEN pen = U(CreatePen)(PS_SOLID, 3, RGB(7, 6, 5));
    REQUIRE(br); REQUIRE(pen);
    HGDIOBJ ob = U(SelectObject)(f.dc, br), op = U(SelectObject)(f.dc, pen);
    /* GetCurrentObject asks the kernel (NtGdiGetDCObject), which must first pick up the dirty brush/pen. */
    EXPECT_EQ(U(GetCurrentObject)(f.dc, OBJ_BRUSH), br);
    EXPECT_EQ(U(GetCurrentObject)(f.dc, OBJ_PEN), pen);
    EXPECT_EQ(U(GetCurrentObject)(f.dc, OBJ_BITMAP), f.bmp);
    if (f.a) {
        EXPECT_EQ(dca_ptr(f.a, DCA_HBRUSH), br);
        EXPECT_EQ(dca_ptr(f.a, DCA_HPEN), pen);
    }
    U(SelectObject)(f.dc, ob); U(SelectObject)(f.dc, op);
    U(DeleteObject)(br); U(DeleteObject)(pen);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_selecting_deleted_brush_leaves_page_unchanged) {
    FXT(f, 8, 8); NEEDA(f);
    HBRUSH br = U(CreateSolidBrush)(RGB(1, 1, 1));
    REQUIRE(br);
    U(DeleteObject)(br);
    batch_reset();                  /* brush deletion may be deferred into the batch; run it */
    void *before = dca_ptr(f.a, DCA_HBRUSH);
    /* SelectObjectImpl validates the object's cell (GdiGetEntry) before writing the page. */
    HGDIOBJ r = U(SelectObject)(f.dc, br);
    EXPECT_EQ(r, NULL);
    EXPECT_EQ(dca_ptr(f.a, DCA_HBRUSH), before);
    fx_close(&f);
}

/* ---- kernel getters read what the user-mode setters wrote ---------------------------------------- */

TEST(gdi_dcattr_batching_kernel_getdcpoint_sees_window_org_written_in_page) {
    FXT(f, 8, 8);
    REQUIRE(UN(NtGdiGetDCPoint));
    CHECK(U(SetWindowOrgEx)(f.dc, 7, -9, NULL));
    POINT p = { 1, 1 };
    /* NtGdiGetDCPoint index 8: window origin. */
    CHECK(UN(NtGdiGetDCPoint)(f.dc, 8, &p));
    EXPECT_EQ(p.x, 7); EXPECT_EQ(p.y, -9);
    POINT q = { 1, 1 };
    CHECK(UN(NtGdiGetDCPoint)(f.dc, 8 | 0x40, &q));   /* NtGdiGetDCPoint masks the index with ~0x40 */
    EXPECT_EQ(q.x, 7); EXPECT_EQ(q.y, -9);
    U(SetWindowOrgEx)(f.dc, 0, 0, NULL);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_kernel_getdcpoint_extents_in_page) {
    FXT(f, 8, 8);
    REQUIRE(UN(NtGdiGetDCPoint));
    POINT w = { 0, 0 }, v = { 0, 0 };
    /* MM_TEXT default extents are 1,1 on both sides. */
    CHECK(UN(NtGdiGetDCPoint)(f.dc, 2, &w)); CHECK(UN(NtGdiGetDCPoint)(f.dc, 1, &v));
    EXPECT_EQ(w.x, 1); EXPECT_EQ(w.y, 1); EXPECT_EQ(v.x, 1); EXPECT_EQ(v.y, 1);
    U(SetMapMode)(f.dc, MM_ANISOTROPIC);
    CHECK(U(SetWindowExtEx)(f.dc, 100, 200, NULL));
    CHECK(U(SetViewportExtEx)(f.dc, 300, 400, NULL));
    CHECK(UN(NtGdiGetDCPoint)(f.dc, 2, &w)); CHECK(UN(NtGdiGetDCPoint)(f.dc, 1, &v));
    EXPECT_EQ(w.x, 100); EXPECT_EQ(w.y, 200); EXPECT_EQ(v.x, 300); EXPECT_EQ(v.y, 400);
    SIZE s = { 0, 0 };
    CHECK(U(GetWindowExtEx)(f.dc, &s)); EXPECT_EQ(s.cx, 100); EXPECT_EQ(s.cy, 200);
    CHECK(U(GetViewportExtEx)(f.dc, &s)); EXPECT_EQ(s.cx, 300); EXPECT_EQ(s.cy, 400);
    if (f.a) {
        POINT pw = dca_pt(f.a, 0x13c), pv = dca_pt(f.a, 0x14c);    /* GetWindowExtEx / GetViewportExtEx read these */
        EXPECT_EQ(pw.x, 100); EXPECT_EQ(pw.y, 200); EXPECT_EQ(pv.x, 300); EXPECT_EQ(pv.y, 400);
    }
    U(SetMapMode)(f.dc, MM_TEXT);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_kernel_getdcpoint_rejects_unknown_index_and_dc) {
    FXT(f, 8, 8);
    REQUIRE(UN(NtGdiGetDCPoint));
    POINT p = { 0x1111, 0x2222 };
    /* NtGdiGetDCPoint handles 1, 2, 4, 8, 0x10, 0x20 (after masking 0x40); anything else returns 0 and writes nothing. */
    static const UINT bad[] = { 0, 3, 5, 6, 7, 9, 0x100, 0x1000 };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        p.x = 0x1111; p.y = 0x2222;
        CHECKF(!UN(NtGdiGetDCPoint)(f.dc, bad[i], &p), "index 0x%x unexpectedly accepted", bad[i]);
        EXPECT_EQ(p.x, 0x1111); EXPECT_EQ(p.y, 0x2222);
    }
    HDC stale = U(CreateCompatibleDC)(NULL);
    if (stale) {
        U(DeleteDC)(stale);
        CHECK(!UN(NtGdiGetDCPoint)(stale, 8, &p));
    }
    CHECK(!UN(NtGdiGetDCPoint)(NULL, 8, &p));
    fx_close(&f);
}

TEST(gdi_dcattr_batching_kernel_getdcdword_rejects_unknown_index_and_dc) {
    FXT(f, 8, 8);
    REQUIRE(UN(NtGdiGetDCDword));
    ULONG v = 0xA5A5A5A5;
 /* Indices 0..8 are handled; others fall through with == 0 and the output untouched. */
    static const int bad[] = { 9, 10, 100, -1, 0x7fffffff };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        v = 0xA5A5A5A5;
        CHECKF(!UN(NtGdiGetDCDword)(f.dc, bad[i], &v), "index %d unexpectedly accepted", bad[i]);
        EXPECT_EQ(v, 0xA5A5A5A5);
    }
    CHECK(!UN(NtGdiGetDCDword)(NULL, 8, &v));
    EXPECT_EQ(v, 0xA5A5A5A5);
    CHECK(UN(NtGdiGetDCDword)(f.dc, 8, &v));
    EXPECT_EQ(v, MM_TEXT);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_kernel_getdcdword_roundtrip_keeps_page_fields) {
    FXT(f, 8, 8); NEEDA(f);
    REQUIRE(UN(NtGdiGetDCDword));
    U(SetBkColor)(f.dc, RGB(1, 2, 3)); U(SetTextColor)(f.dc, RGB(4, 5, 6)); U(SetROP2)(f.dc, R2_XORPEN);
    U(SetBkMode)(f.dc, TRANSPARENT); U(SetTextAlign)(f.dc, TA_CENTER); U(SetPolyFillMode)(f.dc, WINDING);
    unsigned char before[DCA_MIN_READ], after[DCA_MIN_READ];
    CHECK(k_try_read(f.a, before, sizeof before));
    ULONG v;
    for (int idx = 0; idx <= 8; idx++) UN(NtGdiGetDCDword)(f.dc, idx, &v);
    CHECK(k_try_read(f.a, after, sizeof after));
    /* DC::SaveAttributes/RestoreAttributes bracket the query; the user-visible state must come back unchanged. */
    static const struct { unsigned off, len; } keep[] = { {DCA_BKCLR_XL, 16}, {DCA_ROP2, 4}, {DCA_BKMODE, 12}, {DCA_TA, 8}, {DCA_MAPMODE, 4} };
    for (unsigned i = 0; i < sizeof keep / sizeof *keep; i++)
        CHECKF(!memcmp(before + keep[i].off, after + keep[i].off, keep[i].len), "page range +0x%x..+0x%x changed", keep[i].off, keep[i].off + keep[i].len);
    unsigned db = *(unsigned *)(before + DCA_DIRTY), da = *(unsigned *)(after + DCA_DIRTY);
    if (db != da) t_note("ulDirty_ 0x%08x -> 0x%08x across NtGdiGetDCDword", db, da);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_isotropic_extents_resolved_by_kernel) {
    FXT(f, 8, 8);
    REQUIRE(UN(NtGdiGetDCPoint));
    CHECK(U(SetMapMode)(f.dc, MM_ISOTROPIC) != 0);
    CHECK(U(SetWindowExtEx)(f.dc, 100, 100, NULL));
    CHECK(U(SetViewportExtEx)(f.dc, 400, 200, NULL));
    SIZE s = { 0, 0 };
    CHECK(U(GetViewportExtEx)(f.dc, &s));
    /* With a 1:1 logical window the kernel (DC::vMakeIso, reached through GetViewportExtEx ->
     * NtGdiGetDCPoint(1)) equalises the device magnitudes. */
    CHECKF(abs(s.cx) == abs(s.cy), "viewport %ld x %ld", s.cx, s.cy);
    t_note("isotropic viewport after SetViewportExtEx(400,200): %ld x %ld", s.cx, s.cy);
    POINT p = { 0, 0 };
    CHECK(UN(NtGdiGetDCPoint)(f.dc, 1, &p));
    EXPECT_EQ(p.x, s.cx); EXPECT_EQ(p.y, s.cy);
    U(SetMapMode)(f.dc, MM_TEXT);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_maptext_origin_translates_drawing) {
    FXT(f, 32, 16);
    fx_fill(&f, 0x00ffffff);
    HBRUSH br = U(CreateSolidBrush)(RGB(0x20, 0x40, 0x60));
    REQUIRE(br);
    HGDIOBJ ob = U(SelectObject)(f.dc, br);
    /* Window and viewport origins live in the page; the kernel applies them to the next PatBlt. */
    U(SetViewportOrgEx)(f.dc, 8, 4, NULL);
    U(PatBlt)(f.dc, 0, 0, 4, 4, PATCOPY);
    U(SetViewportOrgEx)(f.dc, 0, 0, NULL);
    U(SetWindowOrgEx)(f.dc, -16, -8, NULL);
    U(PatBlt)(f.dc, 0, 0, 4, 4, PATCOPY);
    U(SetWindowOrgEx)(f.dc, 0, 0, NULL);
    fx_sync();
    EXPECT_EQ(fx_px(&f, 9, 5), RGB2PX(0x20, 0x40, 0x60));      /* viewport origin (8,4) */
    EXPECT_EQ(fx_px(&f, 7, 5), 0xffffff);
    EXPECT_EQ(fx_px(&f, 17, 9), RGB2PX(0x20, 0x40, 0x60));     /* window origin (-16,-8) maps logical 0 to device 16,8 */
    EXPECT_EQ(fx_px(&f, 15, 9), 0xffffff);
    U(SelectObject)(f.dc, ob);
    U(DeleteObject)(br);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_loenglish_mapmode_flips_y_axis) {
    FXT(f, 32, 32);
    fx_fill(&f, 0x00ffffff);
    HGDIOBJ ob = U(SelectObject)(f.dc, U(GetStockObject)(BLACK_BRUSH));
    U(SetMapMode)(f.dc, MM_LOENGLISH);
    U(SetViewportOrgEx)(f.dc, 0, 31, NULL);
    /* MM_LOENGLISH has the y axis pointing up: the window extent y is non-zero and LPtoDP inverts y. */
    SIZE w = { 0, 0 };
    CHECK(U(GetWindowExtEx)(f.dc, &w));
    CHECKF(w.cy != 0, "window extent y zero");
    POINT dp[2] = { {0, 0}, {10, 10} };
    CHECK(U(LPtoDP)(f.dc, dp, 2));
    CHECKF(dp[1].y < dp[0].y, "logical +y must go up on screen: %ld -> %ld", dp[0].y, dp[1].y);
    U(SetViewportOrgEx)(f.dc, 0, 0, NULL);
    U(SetMapMode)(f.dc, MM_TEXT);
    U(SelectObject)(f.dc, ob);
    fx_close(&f);
}
