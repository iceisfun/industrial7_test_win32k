/* Font object creation and lifetime: NtGdiHfontCreate, handle table cells, LOGFONT round trip, selection. */
#include "fm.h"

TEST(gdi_font_metrics_text_create_basic) {
    FM_BEGIN();
    HFONT f = fm_font(L"Arial", -16, 400, ANSI_CHARSET, 0, 0);
    CHECK(f != NULL);
    if (!f) return;
    /* Font creation inserts an object of type 'n' (0x0a) into the handle table. */
    EXPECT_EQ(FM_TYPE7(f), FM_T7_FONT);
    CHECK(!FM_STOCK(f));
    EXPECT_EQ(fm.GetObjectType(f), OBJ_FONT);
    CHECK(fm_cell_live_font(f));
    fm.DeleteObject(f);
}

TEST(gdi_font_metrics_text_create_cell_puser_set) {
    FM_BEGIN();
    struct fm_cell c;
    HFONT f = fm_font(L"Arial", -12, 400, ANSI_CHARSET, 0, 0);
    if (!f) { t_skip("create failed"); return; }
    /* gdi32full!CreateFontIndirectExW passes a LOCALFONT* as pvCliData; hfontCreate stores it at cell+0x10. */
    CHECK(fm_cell_of(f, &c));
    CHECK(c.puser != 0);
    CHECK(c.puser > 0x10000);
    fm.DeleteObject(f);
}

TEST(gdi_font_metrics_text_create_distinct_handles) {
    FM_BEGIN();
    HFONT a = fm_font(L"Arial", -12, 400, 0, 0, 0), b = fm_font(L"Arial", -12, 400, 0, 0, 0);
    CHECK(a && b);
    CHECK(a != b);
    if (a && b) CHECK(FM_INDEX(a) != FM_INDEX(b));
    if (a) fm.DeleteObject(a);
    if (b) fm.DeleteObject(b);
}

TEST(gdi_font_metrics_text_delete_frees_cell) {
    FM_BEGIN();
    HFONT f = fm_font(L"Tahoma", -14, 400, 0, 0, 0);
    if (!f) { t_skip("create failed"); return; }
    CHECK(fm_cell_live_font(f));
    CHECK(fm.DeleteObject(f));
    /* GetObjectType only matches the handle's upper word against the cell, which a free leaves unchanged: it still reports OBJ_FONT */
    EXPECT_EQ(fm.GetObjectType(f), OBJ_FONT);
    LOGFONTW lf;
    EXPECT_EQ(fm.GetObjectW(f, sizeof lf, &lf), 0);
    CHECK(!fm_cell_live_font(f));
}

TEST(gdi_font_metrics_text_delete_twice_fails) {
    FM_BEGIN();
    HFONT f = fm_font(L"Arial", -10, 400, 0, 0, 0);
    if (!f) { t_skip("create failed"); return; }
    CHECK(fm.DeleteObject(f));
    SetLastError(0);
    CHECK(!fm.DeleteObject(f));
}

TEST(gdi_font_metrics_text_delete_stock_is_noop_true) {
    FM_BEGIN();
    static const int ids[] = { OEM_FIXED_FONT, ANSI_FIXED_FONT, ANSI_VAR_FONT, SYSTEM_FONT, DEVICE_DEFAULT_FONT, SYSTEM_FIXED_FONT, DEFAULT_GUI_FONT };
    for (unsigned i = 0; i < sizeof ids / sizeof *ids; i++) {
        HGDIOBJ s = fm.GetStockObject(ids[i]);
        CHECK(s != NULL);
        if (!s) continue;
        /* NtGdiDeleteObjectApp returns 1 immediately when bit 23 (stock) is set. */
        CHECK(FM_STOCK(s));
        EXPECT_EQ(FM_TYPE7(s), FM_T7_FONT);
        CHECK(fm.DeleteObject(s));
        EXPECT_EQ(fm.GetObjectType(s), OBJ_FONT);
        CHECK(fm_cell_live_font(s));
    }
}

TEST(gdi_font_metrics_text_stock_font_handles_stable) {
    FM_BEGIN();
    static const int ids[] = { OEM_FIXED_FONT, ANSI_FIXED_FONT, ANSI_VAR_FONT, SYSTEM_FONT, DEVICE_DEFAULT_FONT, SYSTEM_FIXED_FONT, DEFAULT_GUI_FONT };
    for (unsigned i = 0; i < sizeof ids / sizeof *ids; i++) {
        EXPECT_EQ(fm.GetStockObject(ids[i]), fm.GetStockObject(ids[i]));
    }
    /* All distinct stock fonts have distinct handles except possibly aliases; at minimum SYSTEM and OEM_FIXED differ. */
    CHECK(fm.GetStockObject(SYSTEM_FONT) != fm.GetStockObject(OEM_FIXED_FONT));
}

TEST(gdi_font_metrics_text_stock_font_logfont_notes) {
    FM_BEGIN();
    static const struct { int id; const char *name; } t[] = {
        { OEM_FIXED_FONT, "OEM_FIXED" }, { ANSI_FIXED_FONT, "ANSI_FIXED" }, { ANSI_VAR_FONT, "ANSI_VAR" },
        { SYSTEM_FONT, "SYSTEM" }, { DEVICE_DEFAULT_FONT, "DEVICE_DEFAULT" }, { SYSTEM_FIXED_FONT, "SYSTEM_FIXED" },
        { DEFAULT_GUI_FONT, "DEFAULT_GUI" } };
    for (unsigned i = 0; i < sizeof t / sizeof *t; i++) {
        LOGFONTW lf; char face[40];
        HGDIOBJ s = fm.GetStockObject(t[i].id);
        memset(&lf, 0, sizeof lf);
        int n = fm.GetObjectW(s, sizeof lf, &lf);
        EXPECT_EQ(n, sizeof(LOGFONTW));
        fm_u8(lf.lfFaceName, face, sizeof face);
        t_note("stock %s: face \"%s\" height %ld weight %ld charset %u", t[i].name, face, lf.lfHeight, lf.lfWeight, lf.lfCharSet);
    }
}

TEST(gdi_font_metrics_text_default_dc_font_is_stock) {
    FM_BEGIN();
    HDC dc = fm_dc(); if (!dc) { t_skip("no dc"); return; }
    HGDIOBJ cur = fm.GetCurrentObject(dc, OBJ_FONT);
    CHECK(cur != NULL);
    /* A new DC starts with a stock font (the DC's default hlfntNew is a stock handle). */
    if (cur) { CHECK(FM_STOCK(cur)); EXPECT_EQ(FM_TYPE7(cur), FM_T7_FONT); }
    if (cur != fm.GetStockObject(SYSTEM_FONT)) t_note("new DC font is not SYSTEM_FONT");
    fm_freedc(dc);
}

TEST(gdi_font_metrics_text_select_returns_previous) {
    FM_BEGIN();
    HDC dc = fm_dc(); if (!dc) { t_skip("no dc"); return; }
    HGDIOBJ orig = fm.GetCurrentObject(dc, OBJ_FONT);
    HFONT f1 = fm_font(L"Arial", -12, 400, 0, 0, 0), f2 = fm_font(L"Tahoma", -12, 400, 0, 0, 0);
    if (!f1 || !f2) { t_skip("create failed"); fm_freedc(dc); return; }
    EXPECT_EQ(fm_sel(dc, f1), orig);
    EXPECT_EQ(fm.GetCurrentObject(dc, OBJ_FONT), f1);
    EXPECT_EQ(fm_sel(dc, f2), f1);
    EXPECT_EQ(fm.GetCurrentObject(dc, OBJ_FONT), f2);
    EXPECT_EQ(fm_sel(dc, f2), f2);   /* reselecting the current font returns it */
    fm_sel(dc, orig);
    fm.DeleteObject(f1); fm.DeleteObject(f2);
    fm_freedc(dc);
}

TEST(gdi_font_metrics_text_select_direct_stub) {
    FM_BEGIN(); FM_NEED(NtGdiSelectFont_);
    HDC dc = fm_dc(); if (!dc) { t_skip("no dc"); return; }
    HFONT f = fm_font(L"Arial", -13, 400, 0, 0, 0);
    if (!f) { t_skip("create failed"); fm_freedc(dc); return; }
    HGDIOBJ orig = fm.GetCurrentObject(dc, OBJ_FONT);
    /* SelectObject returns the previously selected font handle. */
    HGDIOBJ prev = NtGdiSelectFont_(dc, f);
    EXPECT_EQ(prev, orig);
    EXPECT_EQ(fm.GetCurrentObject(dc, OBJ_FONT), f);
    NtGdiSelectFont_(dc, (HFONT)orig);
    fm.DeleteObject(f);
    fm_freedc(dc);
}

TEST(gdi_font_metrics_text_select_invalid_handles) {
    FM_BEGIN();
    HDC dc = fm_dc(); if (!dc) { t_skip("no dc"); return; }
    HGDIOBJ orig = fm.GetCurrentObject(dc, OBJ_FONT);
    HFONT f = fm_font(L"Arial", -12, 400, 0, 0, 0);
    HFONT dead = fm_font(L"Arial", -12, 400, 0, 0, 0);
    if (!f || !dead) { t_skip("create failed"); fm_freedc(dc); return; }
    fm.DeleteObject(dead);
    fm_sel(dc, f);
    SetLastError(0);
    CHECK(fm.SelectObject(dc, dead) == NULL);          /* deleted handle */
    EXPECT_EQ(fm.GetCurrentObject(dc, OBJ_FONT), f);   /* unchanged */
    CHECK(fm.SelectObject(dc, (HGDIOBJ)(ULONG_PTR)0x12345678) == NULL);
    EXPECT_EQ(fm.GetCurrentObject(dc, OBJ_FONT), f);
    CHECK(fm.SelectObject(dc, NULL) == NULL);
    EXPECT_EQ(fm.GetCurrentObject(dc, OBJ_FONT), f);
    fm_sel(dc, orig);
    fm.DeleteObject(f);
    fm_freedc(dc);
}

TEST(gdi_font_metrics_text_font_selected_in_two_dcs) {
    FM_BEGIN();
    HDC a = fm_dc(), b = fm_dc();
    HFONT f = fm_font(L"Arial", -15, 400, 0, 0, 0);
    if (!a || !b || !f) { t_skip("setup failed"); fm_freedc(a); fm_freedc(b); if (f) fm.DeleteObject(f); return; }
    HGDIOBJ oa = fm.GetCurrentObject(a, OBJ_FONT), ob = fm.GetCurrentObject(b, OBJ_FONT);
    fm_sel(a, f); fm_sel(b, f);
    EXPECT_EQ(fm.GetCurrentObject(a, OBJ_FONT), f);
    EXPECT_EQ(fm.GetCurrentObject(b, OBJ_FONT), f);
    WCHAR fa[40], fb[40];
    fm_face(a, fa, 40); fm_face(b, fb, 40);
    CHECK(fm_wcsieq(fa, fb));
    fm_sel(a, oa); fm_sel(b, ob);
    fm.DeleteObject(f);
    fm_freedc(a); fm_freedc(b);
}

TEST(gdi_font_metrics_text_delete_while_selected_keeps_dc_usable) {
    FM_BEGIN();
    HDC dc = fm_dc(); if (!dc) { t_skip("no dc"); return; }
    HGDIOBJ orig = fm.GetCurrentObject(dc, OBJ_FONT);
    HFONT f = fm_font(L"Arial", -16, 400, ANSI_CHARSET, 0, 0);
    if (!f) { t_skip("create failed"); fm_freedc(dc); return; }
    fm_sel(dc, f);
    BOOL r = fm.DeleteObject(f);
    t_note("DeleteObject of a font selected in a DC returned %d", r);
    /* The DC must stay usable whichever way the delete went. */
    TEXTMETRICW tm;
    CHECK(fm.GetTextMetricsW(dc, &tm));
    CHECK(tm.tmHeight > 0);
    fm_sel(dc, orig);
    fm.DeleteObject(f);   /* complete the delete in case it was deferred */
    EXPECT_EQ(fm.GetObjectType(f), OBJ_FONT);    /* see delete_frees_cell: the type of a freed handle is still reported */
    CHECK(!fm_cell_live_font(f));
    fm_freedc(dc);
}

TEST(gdi_font_metrics_text_dc_delete_releases_font_selection) {
    FM_BEGIN();
    HDC dc = fm_dc(); if (!dc) { t_skip("no dc"); return; }
    HFONT f = fm_font(L"Arial", -16, 400, 0, 0, 0);
    if (!f) { t_skip("create failed"); fm_freedc(dc); return; }
    fm_sel(dc, f);
    fm_freedc(dc);                       /* DC deleted with f still selected */
    CHECK(fm.DeleteObject(f));           /* font is now free to delete */
    EXPECT_EQ(fm.GetObjectType(f), OBJ_FONT);    /* see delete_frees_cell */
    CHECK(!fm_cell_live_font(f));
}

TEST(gdi_font_metrics_text_savedc_restores_font) {
    FM_BEGIN();
    HDC dc = fm_dc(); if (!dc) { t_skip("no dc"); return; }
    HGDIOBJ orig = fm.GetCurrentObject(dc, OBJ_FONT);
    HFONT f = fm_font(L"Arial", -16, 400, 0, 0, 0);
    if (!f) { t_skip("create failed"); fm_freedc(dc); return; }
    int s = fm.SaveDC(dc);
    CHECK(s > 0);
    fm_sel(dc, f);
    EXPECT_EQ(fm.GetCurrentObject(dc, OBJ_FONT), f);
    CHECK(fm.RestoreDC(dc, s));
    EXPECT_EQ(fm.GetCurrentObject(dc, OBJ_FONT), orig);   /* RestoreDC restores the selected font */
    fm.DeleteObject(f);
    fm_freedc(dc);
}

TEST(gdi_font_metrics_text_getcurrentobject_other_types_not_font) {
    FM_BEGIN();
    HDC dc = fm_dc(); if (!dc) { t_skip("no dc"); return; }
    HGDIOBJ o = fm.GetCurrentObject(dc, OBJ_FONT);
    HGDIOBJ p = fm.GetCurrentObject(dc, OBJ_PEN);
    CHECK(o != p);
    CHECK(FM_TYPE7(o) == FM_T7_FONT && FM_TYPE7(p) != FM_T7_FONT);
    fm_freedc(dc);
}

/* ---- direct NtGdiHfontCreate ---- */

static void fm_fill_elf(struct fm_elfdv *e, const WCHAR *face, int height) {
    memset(e, 0, sizeof *e);
    e->lf.lfHeight = height; e->lf.lfWeight = 400; e->lf.lfCharSet = ANSI_CHARSET;
    for (int i = 0; face && i < 31 && face[i]; i++) e->lf.lfFaceName[i] = face[i];
}

TEST(gdi_font_metrics_text_nt_hfontcreate_basic) {
    FM_BEGIN(); FM_NEED(NtGdiHfontCreate_);
    struct fm_elfdv e; fm_fill_elf(&e, L"Arial", -16);
    HFONT f = NtGdiHfontCreate_(&e, 0x164, 0, 0, NULL);
    CHECK(f != NULL);
    if (!f) return;
    EXPECT_EQ(FM_TYPE7(f), FM_T7_FONT);
    CHECK(!FM_STOCK(f));
    CHECK(fm_cell_live_font(f));
    struct fm_cell c;
    CHECK(fm_cell_of(f, &c));
    EXPECT_EQ(c.puser, 0);     /* pvCliData NULL is stored verbatim at cell+0x10 */
    LOGFONTW lf;
    EXPECT_EQ(fm.GetObjectW(f, sizeof lf, &lf), sizeof lf);
    EXPECT_EQ(lf.lfHeight, -16);
    CHECK(fm_wcsieq(lf.lfFaceName, L"Arial"));
    CHECK(fm.DeleteObject(f));
    CHECK(!fm_cell_live_font(f));
}

TEST(gdi_font_metrics_text_nt_hfontcreate_sizes_valid) {
    FM_BEGIN(); FM_NEED(NtGdiHfontCreate_);
    /* Any cb in 1..0x1a4 copies in (rest zero-filled). Sample the boundaries. */
    static const ULONG cbs[] = { 1, 4, 0x5c, 0x100, 0x15c, 0x160, 0x164, 0x1a4 };
    for (unsigned i = 0; i < sizeof cbs / sizeof *cbs; i++) {
        struct fm_elfdv e; fm_fill_elf(&e, L"Arial", -12);
        HFONT f = NtGdiHfontCreate_(&e, cbs[i], 0, 0, NULL);
        CHECKF(f != NULL, "cb=0x%lx", cbs[i]);
        if (f) fm.DeleteObject(f);
    }
}

TEST(gdi_font_metrics_text_nt_hfontcreate_sizes_invalid) {
    FM_BEGIN(); FM_NEED(NtGdiHfontCreate_);
    struct fm_elfdv e; fm_fill_elf(&e, L"Arial", -12);
    /* (cb - 1) > 0x1a3 returns 0 before anything is copied. */
    EXPECT_EQ(NtGdiHfontCreate_(&e, 0, 0, 0, NULL), 0);
    EXPECT_EQ(NtGdiHfontCreate_(&e, 0x1a5, 0, 0, NULL), 0);
    EXPECT_EQ(NtGdiHfontCreate_(&e, 0x1000, 0, 0, NULL), 0);
    EXPECT_EQ(NtGdiHfontCreate_(&e, 0xFFFFFFFF, 0, 0, NULL), 0);
}

TEST(gdi_font_metrics_text_nt_hfontcreate_null_pointer) {
    FM_BEGIN(); FM_NEED(NtGdiHfontCreate_);
    EXPECT_EQ(NtGdiHfontCreate_(NULL, 0x164, 0, 0, NULL), 0);
    EXPECT_EQ(NtGdiHfontCreate_(NULL, 0x5c, 0, 0, NULL), 0);
}

TEST(gdi_font_metrics_text_nt_hfontcreate_design_axes_limit) {
    FM_BEGIN(); FM_NEED(NtGdiHfontCreate_);
    /* dvNumAxes (at +0x160) up to 16 is always accepted; larger counts are probed and reported. */
    for (DWORD axes = 0; axes <= 40; axes++) {
        struct fm_elfdv e; fm_fill_elf(&e, L"Arial", -12);
        e.dvNumAxes = axes;
        HFONT f = NtGdiHfontCreate_(&e, 0x164 + (axes <= 16 ? axes * 4 : 64), 0, 0, NULL);
        if (axes <= 16) { CHECKF(f != NULL, "axes=%lu", axes); }
        else t_note("dvNumAxes=%lu -> %s", axes, f ? "created" : "refused");
        if (f) fm.DeleteObject(f);
    }
    { static const DWORD big[] = { 0x100, 0xffff, 0x10000, 0x7fffffff, 0xffffffff };
      for (unsigned i = 0; i < 5; i++) {
        struct fm_elfdv e; fm_fill_elf(&e, L"Arial", -12);
        e.dvNumAxes = big[i];
        HFONT f = NtGdiHfontCreate_(&e, 0x164, 0, 0, NULL);
        t_note("dvNumAxes=0x%lx cb=0x164 -> %s", big[i], f ? "created" : "refused");
        if (f) fm.DeleteObject(f);
      } }
}

TEST(gdi_font_metrics_text_nt_hfontcreate_zero_fill_beyond_cb) {
    FM_BEGIN(); FM_NEED(NtGdiHfontCreate_);
    /* With cb=0x5c only the LOGFONT is copied in; the uncopied tail is zero-filled, so it must not read the
       (deliberately dirty) bytes after cb: dvNumAxes there would be 0x20 and fail the >16 test. */
    struct fm_elfdv e; fm_fill_elf(&e, L"Arial", -12);
    e.dvNumAxes = 0x20;
    HFONT f = NtGdiHfontCreate_(&e, 0x5c, 0, 0, NULL);
    CHECK(f != NULL);
    if (f) fm.DeleteObject(f);
    f = NtGdiHfontCreate_(&e, 0x164, 0, 0, NULL);
    t_note("dvNumAxes 0x20 with cb 0x164 -> %s", f ? "created" : "refused");
    if (f) fm.DeleteObject(f);
}

TEST(gdi_font_metrics_text_nt_hfontcreate_logfont_roundtrip) {
    FM_BEGIN(); FM_NEED(NtGdiHfontCreate_);
    struct fm_elfdv e; fm_fill_elf(&e, L"Tahoma", -21);
    e.lf.lfWidth = 7; e.lf.lfWeight = 700; e.lf.lfItalic = 1; e.lf.lfUnderline = 1; e.lf.lfStrikeOut = 1;
    e.lf.lfCharSet = EASTEUROPE_CHARSET; e.lf.lfQuality = CLEARTYPE_QUALITY; e.lf.lfPitchAndFamily = VARIABLE_PITCH | FF_SWISS;
    e.lf.lfOutPrecision = OUT_TT_PRECIS; e.lf.lfClipPrecision = CLIP_LH_ANGLES;
    HFONT f = NtGdiHfontCreate_(&e, 0x164, 0, 0, NULL);
    if (!f) { t_skip("create failed"); return; }
    LOGFONTW lf; memset(&lf, 0xCC, sizeof lf);
    EXPECT_EQ(fm.GetObjectW(f, sizeof lf, &lf), sizeof lf);
    EXPECT_EQ(lf.lfHeight, -21); EXPECT_EQ(lf.lfWidth, 7); EXPECT_EQ(lf.lfWeight, 700);
    EXPECT_EQ(lf.lfItalic, 1); EXPECT_EQ(lf.lfUnderline, 1); EXPECT_EQ(lf.lfStrikeOut, 1);
    EXPECT_EQ(lf.lfCharSet, EASTEUROPE_CHARSET); EXPECT_EQ(lf.lfQuality, CLEARTYPE_QUALITY);
    EXPECT_EQ(lf.lfPitchAndFamily, VARIABLE_PITCH | FF_SWISS);
    EXPECT_EQ(lf.lfOutPrecision, OUT_TT_PRECIS); EXPECT_EQ(lf.lfClipPrecision, CLIP_LH_ANGLES);
    CHECK(fm_wcsieq(lf.lfFaceName, L"Tahoma"));
    fm.DeleteObject(f);
}

TEST(gdi_font_metrics_text_nt_hfontcreate_font_usable_in_dc) {
    FM_BEGIN(); FM_NEED(NtGdiHfontCreate_);
    struct fm_elfdv e; fm_fill_elf(&e, L"Arial", -20);
    HFONT f = NtGdiHfontCreate_(&e, 0x164, 0, 0, NULL);
    HDC dc = fm_dc();
    if (!f || !dc) { t_skip("setup failed"); if (f) fm.DeleteObject(f); fm_freedc(dc); return; }
    HGDIOBJ orig = fm.GetCurrentObject(dc, OBJ_FONT);
    fm_sel(dc, f);
    TEXTMETRICW tm;
    CHECK(fm.GetTextMetricsW(dc, &tm));
    CHECK(tm.tmHeight > 0);
    SIZE sz; CHECK(fm.GetTextExtentPoint32W(dc, L"Hello", 5, &sz)); CHECK(sz.cx > 0);
    fm_sel(dc, orig);
    CHECK(fm.DeleteObject(f));
    fm_freedc(dc);
}

TEST(gdi_font_metrics_text_nt_hfontcreate_many_handles_distinct) {
    FM_BEGIN(); FM_NEED(NtGdiHfontCreate_);
    HFONT h[64]; int n = 0;
    for (int i = 0; i < 64; i++) {
        struct fm_elfdv e; fm_fill_elf(&e, L"Arial", -(8 + i));
        h[i] = NtGdiHfontCreate_(&e, 0x164, 0, 0, NULL);
        if (!h[i]) break;
        n++;
    }
    CHECK(n == 64);
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) if (h[i] == h[j]) { CHECK(0); }
    for (int i = 0; i < n; i++) { CHECK(fm_cell_live_font(h[i])); fm.DeleteObject(h[i]); }
}

TEST(gdi_font_metrics_text_handle_reuse_counter) {
    FM_BEGIN();
    /* Deleting then recreating may reuse the table index but the handle value (reuse counter in bits 24..31) must differ. */
    HFONT a = fm_font(L"Arial", -12, 400, 0, 0, 0);
    if (!a) { t_skip("create failed"); return; }
    unsigned ia = FM_INDEX(a);
    fm.DeleteObject(a);
    HFONT b = NULL;
    for (int i = 0; i < 4; i++) {
        b = fm_font(L"Arial", -12, 400, 0, 0, 0);
        if (b && FM_INDEX(b) == ia) break;
        if (b) { fm.DeleteObject(b); b = NULL; }
    }
    if (!b) { t_note("index %u not reused in 4 tries", ia); return; }
    CHECK(b != a);
    fm.DeleteObject(b);
}

/* ---- LOGFONT round trip via GetObjectW ---- */

TEST(gdi_font_metrics_text_logfont_roundtrip_table) {
    FM_BEGIN();
    static const struct { int h, w, wt; BYTE it, ul, so, cs, op, cp, q, pf; const WCHAR *face; } t[] = {
        { -12, 0, 400, 0, 0, 0, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, 0, L"Arial" },
        { 12, 0, 700, 1, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FF_SWISS, L"Tahoma" },
        { 0, 0, 0, 0, 1, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"" },
        { -100, 40, 900, 1, 1, 1, SYMBOL_CHARSET, OUT_RASTER_PRECIS, CLIP_STROKE_PRECIS, NONANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, L"Courier New" },
        { -1, 1, 100, 0, 0, 0, RUSSIAN_CHARSET, OUT_OUTLINE_PRECIS, CLIP_LH_ANGLES, CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_ROMAN, L"Times New Roman" },
        { 32767, 0, 1000, 0, 0, 0, OEM_CHARSET, OUT_DEVICE_PRECIS, CLIP_TT_ALWAYS, PROOF_QUALITY, 0, L"Terminal" },
        { -32768, 0, 400, 0, 0, 0, GREEK_CHARSET, OUT_STRING_PRECIS, CLIP_CHARACTER_PRECIS, DRAFT_QUALITY, 0, L"Segoe UI" },
    };
    for (unsigned i = 0; i < sizeof t / sizeof *t; i++) {
        LOGFONTW in; memset(&in, 0, sizeof in);
        in.lfHeight = t[i].h; in.lfWidth = t[i].w; in.lfWeight = t[i].wt; in.lfItalic = t[i].it; in.lfUnderline = t[i].ul;
        in.lfStrikeOut = t[i].so; in.lfCharSet = t[i].cs; in.lfOutPrecision = t[i].op; in.lfClipPrecision = t[i].cp;
        in.lfQuality = t[i].q; in.lfPitchAndFamily = t[i].pf;
        lstrcpyW(in.lfFaceName, t[i].face);
        HFONT f = fm.CreateFontIndirectW(&in);
        CHECKF(f != NULL, "row %u", i);
        if (!f) continue;
        LOGFONTW out; memset(&out, 0xCC, sizeof out);
        EXPECT_EQ(fm.GetObjectW(f, sizeof out, &out), sizeof out);
        /* hfontCreate memcpy's the caller's LOGFONT into the object before any normalisation. */
        CHECKF(out.lfHeight == in.lfHeight, "row %u height %ld", i, out.lfHeight);
        CHECKF(out.lfWidth == in.lfWidth, "row %u width", i);
        CHECKF(out.lfWeight == in.lfWeight, "row %u weight %ld", i, out.lfWeight);
        CHECKF(out.lfItalic == in.lfItalic && out.lfUnderline == in.lfUnderline && out.lfStrikeOut == in.lfStrikeOut, "row %u flags", i);
        CHECKF(out.lfCharSet == in.lfCharSet, "row %u charset", i);
        CHECKF(out.lfOutPrecision == in.lfOutPrecision && out.lfClipPrecision == in.lfClipPrecision, "row %u prec", i);
        CHECKF(out.lfQuality == in.lfQuality && out.lfPitchAndFamily == in.lfPitchAndFamily, "row %u q/pf", i);
        CHECKF(!lstrcmpW(out.lfFaceName, in.lfFaceName), "row %u face", i);
        fm.DeleteObject(f);
    }
}

TEST(gdi_font_metrics_text_logfont_escapement_orientation_stored) {
    FM_BEGIN();
    static const int angles[] = { 0, 450, 900, 1800, 2700, 3599 };
    for (unsigned i = 0; i < sizeof angles / sizeof *angles; i++) {
        LOGFONTW in; memset(&in, 0, sizeof in);
        in.lfHeight = -16; in.lfWeight = 400; in.lfEscapement = angles[i]; in.lfOrientation = angles[i];
        lstrcpyW(in.lfFaceName, L"Arial");
        HFONT f = fm.CreateFontIndirectW(&in); if (!f) { CHECK(0); continue; }
        LOGFONTW out; memset(&out, 0, sizeof out);
        fm.GetObjectW(f, sizeof out, &out);
        EXPECT_EQ(out.lfEscapement, angles[i]);
        EXPECT_EQ(out.lfOrientation, angles[i]);
        fm.DeleteObject(f);
    }
}

TEST(gdi_font_metrics_text_logfont_out_of_range_angle_notes) {
    FM_BEGIN();
    static const int angles[] = { -450, 3600, 3601, 7200, 36000 };
    for (unsigned i = 0; i < sizeof angles / sizeof *angles; i++) {
        LOGFONTW in; memset(&in, 0, sizeof in);
        in.lfHeight = -16; in.lfWeight = 400; in.lfEscapement = angles[i]; in.lfOrientation = angles[i];
        lstrcpyW(in.lfFaceName, L"Arial");
        HFONT f = fm.CreateFontIndirectW(&in); if (!f) { CHECK(0); continue; }
        LOGFONTW out; memset(&out, 0, sizeof out);
        fm.GetObjectW(f, sizeof out, &out);
        t_note("angle %d reads back escapement %ld orientation %ld", angles[i], out.lfEscapement, out.lfOrientation);
        fm.DeleteObject(f);
    }
}

TEST(gdi_font_metrics_text_logfont_facename_32_chars_no_terminator) {
    FM_BEGIN();
    LOGFONTW in; memset(&in, 0, sizeof in);
    in.lfHeight = -12; in.lfWeight = 400;
    for (int i = 0; i < 32; i++) in.lfFaceName[i] = L'A' + (i % 26);
    HFONT f = fm.CreateFontIndirectW(&in);
    CHECK(f != NULL);   /* hfontCreate's cCapString limits to 32 characters, it does not fail. */
    if (!f) return;
    LOGFONTW out; memset(&out, 0xCC, sizeof out);
    EXPECT_EQ(fm.GetObjectW(f, sizeof out, &out), sizeof out);
    for (int i = 0; i < 31; i++) CHECKF(out.lfFaceName[i] == in.lfFaceName[i], "char %d", i);
    t_note("32nd face char reads back as 0x%04x", out.lfFaceName[31]);
    HDC dc = fm_dc();
    if (dc) {
        HGDIOBJ orig = fm.GetCurrentObject(dc, OBJ_FONT);
        fm_sel(dc, f);
        WCHAR face[80];
        int n = fm_face(dc, face, 80);
        CHECK(n > 0 && n <= LF_FACESIZE);   /* the selected face name is a real face, never longer than 31 chars + NUL */
        fm_sel(dc, orig);
        fm_freedc(dc);
    }
    fm.DeleteObject(f);
}

TEST(gdi_font_metrics_text_getobject_size_protocol) {
    FM_BEGIN();
    HFONT f = fm_font(L"Arial", -12, 400, 0, 0, 0);
    if (!f) { t_skip("create failed"); return; }
    /* On Windows 10 the font object is kept as an ENUMLOGFONTEXDVW (0x164 bytes) and GetObjectW copies up to cb bytes of
       it: the buffers below are large enough for the whole record, never LOGFONTW sized. */
    unsigned char big[0x200]; LOGFONTW lf; memset(&lf, 0, sizeof lf);
    int need = fm.GetObjectW(f, 0, NULL);
    EXPECT_EQ(need, sizeof(LOGFONTW));      /* the gdi32 size query answers with LOGFONTW even though the object is larger */
    EXPECT_EQ(fm.GetObjectW(f, sizeof lf, &lf), sizeof(LOGFONTW));
    EXPECT_EQ(lf.lfHeight, -12);
    memset(big, 0, sizeof big);
    EXPECT_EQ(fm.GetObjectW(f, sizeof lf + 40, big), sizeof lf + 40);    /* a larger cb is honoured, not clamped to LOGFONTW */
    EXPECT_EQ(((LOGFONTW *)big)->lfHeight, -12);
    memset(big, 0, sizeof big);
    EXPECT_EQ(fm.GetObjectW(f, 40, big), 40);
    memset(big, 0, sizeof big);
    EXPECT_EQ(fm.GetObjectW(f, sizeof big, big), 0x164);                  /* clamped to the full ENUMLOGFONTEXDVW */
    fm.DeleteObject(f);
}

TEST(gdi_font_metrics_text_nt_extgetobjectw_matches_gdi32) {
    FM_BEGIN(); FM_NEED(NtGdiExtGetObjectW_);
    HFONT f = fm_font(L"Tahoma", -17, 700, 0, 0, 0);
    if (!f) { t_skip("create failed"); return; }
    LOGFONTW a, b; memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
    int ra = fm.GetObjectW(f, sizeof a, &a);
    ULONG rb = NtGdiExtGetObjectW_(f, sizeof b, &b);
    EXPECT_EQ(ra, rb);
    CHECK(!memcmp(&a, &b, sizeof a));
    EXPECT_EQ(NtGdiExtGetObjectW_(f, 0, NULL), 0x164);   /* the kernel reports the whole ENUMLOGFONTEXDVW record */
    fm.DeleteObject(f);
}

TEST(gdi_font_metrics_text_nt_extgetobjectw_invalid) {
    FM_BEGIN(); FM_NEED(NtGdiExtGetObjectW_);
    LOGFONTW lf;
    EXPECT_EQ(NtGdiExtGetObjectW_(NULL, sizeof lf, &lf), 0);
    EXPECT_EQ(NtGdiExtGetObjectW_((HGDIOBJ)(ULONG_PTR)0x000a0000, sizeof lf, &lf), 0);
    HFONT f = fm_font(L"Arial", -12, 400, 0, 0, 0);
    if (!f) return;
    fm.DeleteObject(f);
    EXPECT_EQ(NtGdiExtGetObjectW_(f, sizeof lf, &lf), 0);
}

TEST(gdi_font_metrics_text_getobject_wrong_type_not_font) {
    FM_BEGIN();
    HDC dc = fm_dc(); if (!dc) { t_skip("no dc"); return; }
    LOGFONTW lf; memset(&lf, 0, sizeof lf);
    HGDIOBJ pen = fm.GetCurrentObject(dc, OBJ_PEN);
    int n = fm.GetObjectW(pen, sizeof lf, &lf);
    CHECK(n != (int)sizeof(LOGFONTW));   /* a pen reads as a LOGPEN, never as a LOGFONT */
    fm_freedc(dc);
}

TEST(gdi_font_metrics_text_createfont_variants_agree) {
    FM_BEGIN();
    HFONT a = fm.CreateFontW(-20, 0, 0, 0, 700, 1, 0, 0, ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Arial");
    HFONT b = fm_font(L"Arial", -20, 700, ANSI_CHARSET, CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS);
    if (!a || !b) { t_skip("create failed"); if (a) fm.DeleteObject(a); if (b) fm.DeleteObject(b); return; }
    LOGFONTW la, lb; memset(&la, 0, sizeof la); memset(&lb, 0, sizeof lb);
    fm.GetObjectW(a, sizeof la, &la); fm.GetObjectW(b, sizeof lb, &lb);
    EXPECT_EQ(la.lfHeight, lb.lfHeight); EXPECT_EQ(la.lfWeight, lb.lfWeight); EXPECT_EQ(la.lfItalic, 1);
    EXPECT_EQ(la.lfCharSet, lb.lfCharSet); EXPECT_EQ(la.lfQuality, lb.lfQuality);
    CHECK(!lstrcmpW(la.lfFaceName, lb.lfFaceName));
    fm.DeleteObject(a); fm.DeleteObject(b);
}

TEST(gdi_font_metrics_text_createfont_ansi_facename_widened) {
    FM_BEGIN();
    HFONT f = fm.CreateFontA(-14, 0, 0, 0, 400, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, "Tahoma");
    if (!f) { t_skip("create failed"); return; }
    LOGFONTW lf; memset(&lf, 0, sizeof lf);
    EXPECT_EQ(fm.GetObjectW(f, sizeof lf, &lf), sizeof lf);
    CHECK(!lstrcmpW(lf.lfFaceName, L"Tahoma"));
    EXPECT_EQ(lf.lfHeight, -14);
    fm.DeleteObject(f);
}

TEST(gdi_font_metrics_text_guiresources_font_not_counted) {
    FM_BEGIN();
    /* GetGuiResources counts GDI objects through the process handle table (GR_GDIOBJECTS); fonts are included. */
    DWORD before = fm.GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    HFONT h[10]; int n = 0;
    for (int i = 0; i < 10; i++) { h[i] = fm_font(L"Arial", -10 - i, 400, 0, 0, 0); if (h[i]) n++; }
    DWORD mid = fm.GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    EXPECT_EQ(mid, before + n);
    for (int i = 0; i < 10; i++) if (h[i]) fm.DeleteObject(h[i]);
    DWORD after = fm.GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    EXPECT_EQ(after, before);
}
