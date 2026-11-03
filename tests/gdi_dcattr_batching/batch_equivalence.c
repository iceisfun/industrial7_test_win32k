/* Batched and unbatched execution must produce the same result. Each scene is rendered once with the
 * default batch limit (calls are queued in TEB.GdiTebBatch and replayed by the kernel) and once with
 * GdiSetBatchLimit(1) (every queued call is flushed at once); the DIB sections are then compared, and
 * a few absolute pixels are checked where the expected value is exact. */
#include "dcattr.h"

static DWORD g_entry;
static DWORD limit_set(DWORD n) { g_entry = U(GdiGetBatchLimit)(); return U(GdiSetBatchLimit)(n); }
static void limit_back(void) { U(GdiSetBatchLimit)(g_entry >= 1 && g_entry <= 20 ? g_entry : 20); }

/* ---- pattern brush origin ------------------------------------------------------------------------ */

static HBRUSH make_dot_brush(void) {
    BITMAPINFO bi; memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = 8; bi.bmiHeader.biHeight = -8;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    HBITMAP bm = U(CreateDIBSection)(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bm || !bits) return NULL;
    DWORD *p = (DWORD *)bits;
    for (int i = 0; i < 64; i++) p[i] = 0x00000000;
    p[0] = 0x00ffffff;                     /* single white texel at (0,0) */
    U(GdiFlush)();
    HBRUSH b = U(CreatePatternBrush)(bm);
    U(DeleteObject)(bm);
    return b;
}

static int dot_expected(int x, int y, int ox, int oy) { return (((x - ox) % 8) + 8) % 8 == 0 && (((y - oy) % 8) + 8) % 8 == 0; }

static void check_dot_image(struct fx *f, int ox, int oy, const char *what) {
    int bad = 0;
    for (int y = 0; y < f->h; y++)
        for (int x = 0; x < f->w; x++) {
            DWORD want = dot_expected(x, y, ox, oy) ? 0xffffff : 0;
            if (fx_px(f, x, y) != want && bad++ < 3) t_check(0, __FILE__, __LINE__, "pattern pixel", "%s origin (%d,%d): (%d,%d) got 0x%06lx want 0x%06lx", what, ox, oy, x, y, (unsigned long)fx_px(f, x, y), (unsigned long)want);
        }
    CHECKF(bad == 0, "%s origin (%d,%d): %d wrong pixels", what, ox, oy, bad);
}

static const struct { int x, y; } g_orgs[] = { {0,0}, {1,0}, {0,1}, {3,2}, {7,5}, {7,7}, {2,6} };

TEST(gdi_dcattr_batching_pattern_brush_origin_follows_setbrushorgex_batched) {
    FXT(f, 32, 24);
    HBRUSH br = make_dot_brush();
    REQUIRE(br);
    HGDIOBJ ob = U(SelectObject)(f.dc, br);
    for (unsigned li = 0; li < 2; li++) {
        limit_set(li == 0 ? 20 : 1);
        for (unsigned i = 0; i < sizeof g_orgs / sizeof *g_orgs; i++) {
            fx_fill(&f, 0x00808080);
            CHECK(U(SetBrushOrgEx)(f.dc, g_orgs[i].x, g_orgs[i].y, NULL));
            CHECK(U(PatBlt)(f.dc, 0, 0, 32, 24, PATCOPY));
            fx_sync();
            check_dot_image(&f, g_orgs[i].x, g_orgs[i].y, li == 0 ? "batched" : "limit1");
        }
        limit_back();
    }
    U(SetBrushOrgEx)(f.dc, 0, 0, NULL);
    U(SelectObject)(f.dc, ob);
    batch_reset();
    U(DeleteObject)(br);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_two_dcs_keep_their_own_brush_origin) {
    FXT(f1, 32, 16); FXT(f2, 32, 16);
    HBRUSH br = make_dot_brush();
    REQUIRE(br);
    HGDIOBJ o1 = U(SelectObject)(f1.dc, br), o2 = U(SelectObject)(f2.dc, br);
    /* Alternating DCs forces the batch to drain on every switch; each DC's origin must still stick. */
    U(SetBrushOrgEx)(f1.dc, 1, 2, NULL);
    U(SetBrushOrgEx)(f2.dc, 5, 3, NULL);
    U(PatBlt)(f1.dc, 0, 0, 32, 16, PATCOPY);
    U(PatBlt)(f2.dc, 0, 0, 32, 16, PATCOPY);
    fx_sync();
    check_dot_image(&f1, 1, 2, "dc1");
    check_dot_image(&f2, 5, 3, "dc2");
    U(SelectObject)(f1.dc, o1); U(SelectObject)(f2.dc, o2);
    batch_reset();
    U(DeleteObject)(br);
    fx_close(&f2); fx_close(&f1);
}

TEST(gdi_dcattr_batching_origin_set_before_brush_selection_applies) {
    FXT(f, 32, 16);
    HBRUSH br = make_dot_brush();
    REQUIRE(br);
    U(SetBrushOrgEx)(f.dc, 4, 4, NULL);              /* queued */
    HGDIOBJ ob = U(SelectObject)(f.dc, br);          /* page write; the queue is still pending */
    fx_fill(&f, 0x00808080);
    U(PatBlt)(f.dc, 0, 0, 32, 16, PATCOPY);
    fx_sync();
    check_dot_image(&f, 4, 4, "late selection");
    U(SelectObject)(f.dc, ob);
    batch_reset();
    U(DeleteObject)(br);
    fx_close(&f);
}

/* ---- whole scenes ------------------------------------------------------------------------------ */

static void scene_fills(struct fx *f) {
    fx_fill(f, 0x00ffffff);
    HBRUSH b1 = U(CreateSolidBrush)(RGB(200, 30, 40)), b2 = U(CreateSolidBrush)(RGB(10, 150, 60));
    HGDIOBJ ob = U(SelectObject)(f->dc, b1);
    U(PatBlt)(f->dc, 0, 0, 20, 10, PATCOPY);
    U(SetBkColor)(f->dc, RGB(1, 2, 250));
    RECT r1 = { 24, 0, 44, 10 };
    U(ExtTextOutW)(f->dc, 0, 0, ETO_OPAQUE, &r1, NULL, 0, NULL);
    U(SelectObject)(f->dc, b2);
    U(PatBlt)(f->dc, 0, 12, 20, 10, PATCOPY);
    U(SetBkColor)(f->dc, RGB(250, 250, 1));
    RECT r2 = { 24, 12, 44, 22 };
    U(ExtTextOutW)(f->dc, 0, 0, ETO_OPAQUE, &r2, NULL, 0, NULL);
    U(PatBlt)(f->dc, 4, 4, 6, 6, PATINVERT);
    U(PatBlt)(f->dc, 28, 4, 4, 4, DSTINVERT);
    U(SelectObject)(f->dc, ob);
    fx_sync();
    batch_reset();
    U(DeleteObject)(b1); U(DeleteObject)(b2);
    batch_reset();
}

TEST(gdi_dcattr_batching_fill_scene_batched_equals_unbatched) {
    FXT(fa, 48, 24); FXT(fb, 48, 24);
    limit_set(1);  scene_fills(&fa);  limit_back();
    limit_set(20); scene_fills(&fb);  limit_back();
    CHECK(!memcmp(fa.bits, fb.bits, 48 * 24 * 4));
    /* absolute anchors (32bpp DIB: exact) */
    EXPECT_EQ(fx_px(&fb, 2, 2), RGB2PX(200, 30, 40));
    EXPECT_EQ(fx_px(&fb, 40, 8), RGB2PX(1, 2, 250));
    EXPECT_EQ(fx_px(&fb, 30, 5), RGB2PX(1, 2, 250) ^ 0xffffff);          /* DSTINVERT'd square inside the blue rect */
    EXPECT_EQ(fx_px(&fb, 40, 16), RGB2PX(250, 250, 1));
    EXPECT_EQ(fx_px(&fb, 2, 16), RGB2PX(10, 150, 60));
    EXPECT_EQ(fx_px(&fb, 6, 6), RGB2PX(200 ^ 10, 30 ^ 150, 40 ^ 60));    /* PATINVERT with the green brush over the red fill */
    EXPECT_EQ(fx_px(&fb, 46, 20), 0xffffff);
    fx_close(&fb); fx_close(&fa);
}

static void scene_text(struct fx *f) {
    fx_fill(f, 0x00f0f0f0);
    HGDIOBJ of = U(SelectObject)(f->dc, U(GetStockObject)(ANSI_VAR_FONT));
    U(SetBkMode)(f->dc, OPAQUE);
    U(SetBkColor)(f->dc, RGB(255, 220, 0));
    U(SetTextColor)(f->dc, RGB(0, 0, 160));
    U(TextOutW)(f->dc, 2, 2, L"Batch", 5);
    U(SetBkColor)(f->dc, RGB(0, 220, 255));
    U(SetTextColor)(f->dc, RGB(160, 0, 0));
    U(TextOutW)(f->dc, 2, 20, L"Batch", 5);
    U(SetBkMode)(f->dc, TRANSPARENT);
    U(SelectObject)(f->dc, U(GetStockObject)(SYSTEM_FIXED_FONT));
    U(TextOutW)(f->dc, 2, 38, L"Fixed", 5);
    U(SelectObject)(f->dc, of);
    fx_sync();
    batch_reset();
}

TEST(gdi_dcattr_batching_text_scene_batched_equals_unbatched) {
    FXT(fa, 96, 60); FXT(fb, 96, 60);
    limit_set(1);  scene_text(&fa);  limit_back();
    limit_set(20); scene_text(&fb);  limit_back();
    int diff = 0, drawn = 0;
    for (int i = 0; i < 96 * 60; i++) { diff += (fa.bits[i] != fb.bits[i]); drawn += ((fb.bits[i] & 0xffffff) != 0xf0f0f0); }
    CHECKF(diff == 0, "%d pixels differ between batched and unbatched text", diff);
    CHECKF(drawn > 100, "scene drew only %d pixels", drawn);
    /* The first string was queued with the first colours; the later SetBkColor must not recolour it. */
    int a = 0, b = 0;
    for (int y = 2; y < 18; y++) for (int x = 2; x < 40; x++) { DWORD p = fx_px(&fb, x, y); a += (p == RGB2PX(255, 220, 0)); b += (p == RGB2PX(0, 220, 255)); }
    CHECKF(a > 0, "opaque background of the first string missing");
    EXPECT_EQ(b, 0);
    fx_close(&fb); fx_close(&fa);
}

TEST(gdi_dcattr_batching_text_background_colour_captured_at_call_time) {
    FXT(f, 64, 40);
    fx_fill(&f, 0x00ffffff);
    U(SetBkMode)(f.dc, OPAQUE);
    U(SetBkColor)(f.dc, RGB(255, 0, 255));
    CHECK(U(TextOutW)(f.dc, 2, 2, L"MM", 2));
    U(SetBkColor)(f.dc, RGB(0, 255, 0));
    CHECK(U(TextOutW)(f.dc, 2, 20, L"MM", 2));
    U(SetBkColor)(f.dc, RGB(0, 0, 0));        /* after both calls, before the flush */
    fx_sync();
    int m = 0, g = 0;
    for (int y = 0; y < 40; y++) for (int x = 0; x < 64; x++) {
        DWORD p = fx_px(&f, x, y);
        m += (p == RGB2PX(255, 0, 255)); g += (p == RGB2PX(0, 255, 0));
    }
    CHECK(m > 0); CHECK(g > 0);
    fx_close(&f);
}

/* ---- fonts ------------------------------------------------------------------------------------ */

static LONG text_width(struct fx *f, HGDIOBJ font) {
    HGDIOBJ of = U(SelectObject)(f->dc, font);
    SIZE sz = { 0, 0 };
    U(GetTextExtentPoint32W)(f->dc, L"iiiiiiiiWWWW", 12, &sz);
    U(SelectObject)(f->dc, of);
    return sz.cx;
}

TEST(gdi_dcattr_batching_batched_font_selection_seen_by_text_extent) {
    FXT(f, 8, 8);
    HGDIOBJ fa = U(GetStockObject)(SYSTEM_FIXED_FONT), fb = U(GetStockObject)(ANSI_VAR_FONT);
    LONG w1[2];
    for (int li = 0; li < 2; li++) {
        limit_set(li == 0 ? 20 : 1);
        U(SelectObject)(f.dc, fa);
        batch_reset();
        U(SelectObject)(f.dc, fb);                 /* queued as a type-6 record when batching */
        SIZE sz = { 0, 0 };
        CHECK(U(GetTextExtentPoint32W)(f.dc, L"iiiiiiiiWWWW", 12, &sz));   /* kernel call: drains, then measures */
        w1[li] = sz.cx;
        limit_back();
    }
    EXPECT_EQ(w1[0], w1[1]);
    LONG wa = text_width(&f, fa), wb = text_width(&f, fb);
    EXPECT_EQ(w1[0], wb);
    if (wa == wb) t_note("fixed and variable stock fonts measure the same (%ld)", wa);
    batch_reset();
    fx_close(&f);
}

TEST(gdi_dcattr_batching_getcurrentobject_font_after_batched_selection) {
    FXT(f, 8, 8);
    static const int fonts[] = { SYSTEM_FONT, SYSTEM_FIXED_FONT, ANSI_FIXED_FONT, ANSI_VAR_FONT, DEVICE_DEFAULT_FONT, DEFAULT_GUI_FONT };
    for (unsigned i = 0; i < sizeof fonts / sizeof *fonts; i++) {
        HGDIOBJ h = U(GetStockObject)(fonts[i]);
        if (!h) continue;
        U(SelectObject)(f.dc, h);
        EXPECT_EQ(U(GetCurrentObject)(f.dc, OBJ_FONT), h);
    }
    batch_reset();
    fx_close(&f);
}

/* ---- deferred deletes ------------------------------------------------------------------------- */

TEST(gdi_dcattr_batching_deferred_delete_records_by_object_type) {
    FXT(f, 8, 8);
    /* after: what GetObjectType says once the queued delete has run. A solid brush and a region are parked by gdi32 and
     * read as 0; a pen's cell is freed, yet GetObjectType still decodes the handle bits (OBJ_PEN). */
    struct { const char *n; HGDIOBJ h; unsigned type; unsigned after; } v[3] = {
        { "brush",  U(CreateSolidBrush)(RGB(3, 3, 3)), REC_DELETE7, 0 },
        { "pen",    U(CreatePen)(PS_SOLID, 1, RGB(3, 3, 3)), REC_DELETE7, OBJ_PEN },
        { "region", U(CreateRectRgn)(0, 0, 4, 4), REC_DELETE8, 0 },
    };
    for (int i = 0; i < 3; i++) {
        if (!v[i].h) { t_skip("cannot create %s", v[i].n); continue; }
        batch_reset();
        CHECK(U(DeleteObject)(v[i].h));
        /* InternalDeleteObject: 0x70010 for brush/pen, 0x80010 for a region, when queueing is possible. */
        if (teb_count() == 1) {
            EXPECT_EQ(rec_len(0), 0x10);
            EXPECT_EQ(rec_type(0), v[i].type);
            EXPECT_EQ((void *)rdp(teb_rec(0) + 8), (void *)v[i].h);
            EXPECT_EQ(teb_offset(), 0x10);
        } else t_note("%s delete was not deferred (count %u)", v[i].n, teb_count());
        batch_reset();
        EXPECT_EQ(U(GetObjectType)(v[i].h), v[i].after);
    }
    fx_close(&f);
}

TEST(gdi_dcattr_batching_deferred_delete_of_selected_brush_kills_handle_but_dc_keeps_it) {
    FXT(f, 8, 8);
    HBRUSH b = U(CreateSolidBrush)(RGB(4, 5, 6));
    REQUIRE(b);
    HGDIOBJ ob = U(SelectObject)(f.dc, b);
    batch_reset();
    CHECK(U(DeleteObject)(b));           /* accepted although the brush is selected */
    batch_reset();
    /* once the queued delete has run, user mode no longer knows the handle, while the DC still reports it as current */
    EXPECT_EQ(U(GetObjectType)(b), 0);
    LOGBRUSH lb;
    CHECK(U(GetObjectW)(b, sizeof lb, &lb) == 0);
    EXPECT_EQ(U(GetCurrentObject)(f.dc, OBJ_BRUSH), b);
    U(SelectObject)(f.dc, ob);
    EXPECT_NE(U(GetCurrentObject)(f.dc, OBJ_BRUSH), b);
    CHECK(!U(DeleteObject)(b));
    batch_reset();
    EXPECT_EQ(U(GetObjectType)(b), 0);
    fx_close(&f);
}

/* ---- HasRenderingCommand and stale flags --------------------------------------------------------- */

TEST(gdi_dcattr_batching_rendering_flag_observed_after_text_flush) {
    FXT(f, 64, 32);
    CHECK(U(TextOutW)(f.dc, 0, 0, L"z", 1));
    NEEDBATCH(f, "TextOutW did not batch");
    EXPECT_EQ(teb_hrc(), 1);
    U(GdiFlush)();
    EXPECT_EQ(teb_offset(), 0);
    /* The kernel flush routines clear HasRenderingCommand differently (NtGdiFlushUserBatchInternal drops
     * it, the entry callout keeps it): record what this build does. */
    t_note("HasRenderingCommand after GdiFlush: %u", teb_hrc());
    fx_close(&f);
}

TEST(gdi_dcattr_batching_hdc_field_after_flush_is_recorded) {
    FXT(f, 8, 8);
    U(SetBrushOrgEx)(f.dc, 9, 9, NULL);
    NEEDBATCH(f, "SetBrushOrgEx did not queue");
    EXPECT_EQ(teb_hdc(), f.dc);
    U(GdiFlush)();
    t_note("TEB.GdiTebBatch.HDC after GdiFlush: %p (queued for %p)", teb_hdc(), f.dc);
    fx_close(&f);
}
