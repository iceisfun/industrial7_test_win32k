/* What the metafile DC records, what playback does to a DIB section, and the DC state the kernel keeps for the DC.
 *
 * A metafile DC created by NtGdiCreateMetafileDC is a real kernel DC on the reference device (display DC): its
 * DC_ATTR page is live, so Get* calls return what was Set*. gdi32full records each call (MF_*), and
 * bInternalPlayEMF/PlayEnhMetaFile replays the records through the normal user-mode path into the target DC.
 * PlayEnhMetaFile(NULL, ...) fails up front with GdiSetLastError(0x57).
 */
#include "mf.h"

/* ---- record counts per call ------------------------------------------------------------------------------------ */
typedef void (*op_fn)(HDC m);
static void op_rect(HDC m)      { U(Rectangle)(m, 10, 10, 50, 40); }
static void op_ellipse(HDC m)   { U(Ellipse)(m, 10, 10, 50, 40); }
static void op_moveto(HDC m)    { U(MoveToEx)(m, 7, 9, NULL); }
static void op_lineto(HDC m)    { U(LineTo)(m, 30, 30); }
static void op_roundrect(HDC m) { U(RoundRect)(m, 10, 10, 50, 40, 6, 6); }
static void op_arc(HDC m)       { U(Arc)(m, 10, 10, 50, 40, 10, 10, 50, 10); }
static void op_pie(HDC m)       { U(Pie)(m, 10, 10, 50, 40, 10, 10, 50, 10); }
static void op_chord(HDC m)     { U(Chord)(m, 10, 10, 50, 40, 10, 10, 50, 10); }
static void op_savedc(HDC m)    { U(SaveDC)(m); }
static void op_bkcolor(HDC m)   { U(SetBkColor)(m, RGB(1, 2, 3)); }
static void op_textcolor(HDC m) { U(SetTextColor)(m, RGB(10, 20, 30)); }
static void op_bkmode(HDC m)    { U(SetBkMode)(m, TRANSPARENT); }
static void op_rop2(HDC m)      { U(SetROP2)(m, R2_XORPEN); }
static void op_mapmode(HDC m)   { U(SetMapMode)(m, MM_LOMETRIC); }
static void op_polyfill(HDC m)  { U(SetPolyFillMode)(m, WINDING); }
static void op_stretch(HDC m)   { U(SetStretchBltMode)(m, HALFTONE); }
static void op_textalign(HDC m) { U(SetTextAlign)(m, TA_BASELINE); }
static void op_vporg(HDC m)     { U(SetViewportOrgEx)(m, 5, 6, NULL); }
static void op_wndorg(HDC m)    { U(SetWindowOrgEx)(m, 5, 6, NULL); }
static void op_comment(HDC m)   { static const char c[] = "kernel conformance"; U(GdiComment)(m, sizeof c, (const BYTE *)c); }
static void op_isectclip(HDC m) { U(IntersectClipRect)(m, 0, 0, 100, 100); }
static void op_exclclip(HDC m)  { U(ExcludeClipRect)(m, 20, 20, 30, 30); }
static void op_textout(HDC m)   { U(TextOutW)(m, 5, 5, L"abc", 3); }

static const struct { const char *name; op_fn fn; int type; } OPS[] = {
    { "Rectangle", op_rect, EMR_RECTANGLE }, { "Ellipse", op_ellipse, EMR_ELLIPSE }, { "MoveToEx", op_moveto, EMR_MOVETOEX },
    { "LineTo", op_lineto, EMR_LINETO }, { "RoundRect", op_roundrect, EMR_ROUNDRECT }, { "Arc", op_arc, EMR_ARC },
    { "Pie", op_pie, EMR_PIE }, { "Chord", op_chord, EMR_CHORD }, { "SaveDC", op_savedc, EMR_SAVEDC },
    { "SetBkColor", op_bkcolor, EMR_SETBKCOLOR }, { "SetTextColor", op_textcolor, EMR_SETTEXTCOLOR },
    { "SetBkMode", op_bkmode, EMR_SETBKMODE }, { "SetROP2", op_rop2, EMR_SETROP2 }, { "SetMapMode", op_mapmode, EMR_SETMAPMODE },
    { "SetPolyFillMode", op_polyfill, EMR_SETPOLYFILLMODE }, { "SetStretchBltMode", op_stretch, EMR_SETSTRETCHBLTMODE },
    { "SetTextAlign", op_textalign, EMR_SETTEXTALIGN }, { "SetViewportOrgEx", op_vporg, EMR_SETVIEWPORTORGEX },
    { "SetWindowOrgEx", op_wndorg, EMR_SETWINDOWORGEX }, { "GdiComment", op_comment, EMR_GDICOMMENT },
    { "IntersectClipRect", op_isectclip, EMR_INTERSECTCLIPRECT }, { "ExcludeClipRect", op_exclclip, EMR_EXCLUDECLIPRECT },
    { "TextOutW", op_textout, EMR_EXTTEXTOUTW },
};

static void run_op_table(int exact_only_first_n) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    REQUIRE(U(EnumEnhMetaFile)); REQUIRE(U(SaveDC)); REQUIRE(U(GdiComment));
    for (unsigned i = 0; i < sizeof OPS / sizeof OPS[0]; i++) {
        HDC m = U(CreateEnhMetaFileW)(ref, NULL, NULL, NULL);
        struct mf_enum e; ENHMETAHEADER h;
        CHECK(m != NULL);
        if (!m) continue;
        OPS[i].fn(m);
        HENHMETAFILE hm = U(CloseEnhMetaFile)(m);
        CHECK(hm != NULL);
        if (!hm) continue;
        CHECK(U(GetEnhMetaFileHeader)(hm, sizeof h, &h) != 0);
        CHECK(mf_walk(hm, &e) > 0);
        CHECKF(e.counts[OPS[i].type] == 1, "%s: %d records of type %d", OPS[i].name, e.counts[OPS[i].type], OPS[i].type);
        EXPECT_EQ(e.first, EMR_HEADER);
        EXPECT_EQ(e.last, EMR_EOF);
        EXPECT_EQ(e.counts[EMR_HEADER], 1);
        EXPECT_EQ(e.counts[EMR_EOF], 1);
        EXPECT_EQ((unsigned)e.total, (unsigned)h.nRecords);
        if (e.total != 3) t_note("%s: %d records in total (header+EOF+%d)", OPS[i].name, e.total, e.total - 2);
        U(DeleteEnhMetaFile)(hm);
    }
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_rec_one_call_one_record_each_op) { run_op_table(0); }

TEST(gdi_metafile_print_neg_rec_rectangle_n_calls_n_records) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    static const int ns[] = { 1, 2, 5, 17, 50 };
    for (unsigned k = 0; k < sizeof ns / sizeof ns[0]; k++) {
        HENHMETAFILE e = mf_make(ref, NULL, NULL, ns[k]);
        struct mf_enum en;
        ENHMETAHEADER h;
        CHECK(e != NULL);
        if (!e) continue;
        CHECK(mf_enum(e, &en) != 0);
        EXPECT_EQ(en.counts[EMR_RECTANGLE], ns[k]);
        CHECK(U(GetEnhMetaFileHeader)(e, sizeof h, &h) != 0);
        EXPECT_EQ((unsigned)en.total, (unsigned)h.nRecords);
        EXPECT_EQ(en.dsize_sum, h.nBytes);                       /* record sizes tile the whole stream */
        EXPECT_EQ(en.bad_dsize, 0);
        U(DeleteEnhMetaFile)(e);
    }
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_rec_order_is_call_order) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    HDC m = U(CreateEnhMetaFileW)(ref, NULL, NULL, NULL);
    struct mf_enum e;
    REQUIRE(m);
    U(MoveToEx)(m, 1, 1, NULL); U(LineTo)(m, 40, 40); U(Rectangle)(m, 2, 2, 20, 20); U(Ellipse)(m, 3, 3, 30, 30);
    HENHMETAFILE hm = U(CloseEnhMetaFile)(m);
    REQUIRE(hm);
    mf_enum(hm, &e);
    static const int want[] = { EMR_MOVETOEX, EMR_LINETO, EMR_RECTANGLE, EMR_ELLIPSE };
    unsigned w = 0;
    for (int i = 0; i < e.nord && w < 4; i++) if (e.order[i] == want[w]) w++;
    EXPECT_EQ(w, 4);                                            /* subsequence in call order */
    U(DeleteEnhMetaFile)(hm);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_rec_rectangle_coordinates_exact) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    HENHMETAFILE e = mf_make(ref, NULL, NULL, 1);
    UINT n; BYTE *b;
    REQUIRE(e);
    b = mf_getbits(e, &n);
    REQUIRE(b);
    const EMRRECTANGLE *r = (const EMRRECTANGLE *)mf_find(b, n, EMR_RECTANGLE, 0);
    CHECK(r != NULL);
    if (r) {
        EXPECT_EQ(r->emr.nSize, sizeof *r);                      /* record + RECTL */
        EXPECT_EQ(r->rclBox.left, 10);
        EXPECT_EQ(r->rclBox.top, 10);
        /* right/bottom are stored either as passed (exclusive) or as the last covered coordinate (inclusive) */
        CHECK(r->rclBox.right == 50 || r->rclBox.right == 49);
        CHECK(r->rclBox.bottom == 40 || r->rclBox.bottom == 39);
        t_note("EMR_RECTANGLE rclBox for Rectangle(10,10,50,40): {%ld,%ld,%ld,%ld}", r->rclBox.left, r->rclBox.top, r->rclBox.right, r->rclBox.bottom);
    }
    free(b);
    U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_rec_ellipse_lineto_moveto_coordinates_exact) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    HDC m = U(CreateEnhMetaFileW)(ref, NULL, NULL, NULL);
    UINT n; BYTE *b;
    REQUIRE(m);
    U(MoveToEx)(m, 7, 9, NULL); U(LineTo)(m, 31, 33); U(Ellipse)(m, 2, 3, 22, 43);
    HENHMETAFILE e = U(CloseEnhMetaFile)(m);
    REQUIRE(e);
    b = mf_getbits(e, &n);
    REQUIRE(b);
    const EMRMOVETOEX *mv = (const EMRMOVETOEX *)mf_find(b, n, EMR_MOVETOEX, 0);
    const EMRLINETO *ln = (const EMRLINETO *)mf_find(b, n, EMR_LINETO, 0);
    const EMRELLIPSE *el = (const EMRELLIPSE *)mf_find(b, n, EMR_ELLIPSE, 0);
    CHECK(mv && ln && el);
    if (mv) { EXPECT_EQ(mv->ptl.x, 7); EXPECT_EQ(mv->ptl.y, 9); }
    if (ln) { EXPECT_EQ(ln->ptl.x, 31); EXPECT_EQ(ln->ptl.y, 33); }
    if (el) { EXPECT_EQ(el->rclBox.left, 2); EXPECT_EQ(el->rclBox.top, 3); CHECK(el->rclBox.right == 22 || el->rclBox.right == 21); CHECK(el->rclBox.bottom == 43 || el->rclBox.bottom == 42); }
    free(b);
    U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_rec_stock_object_selection_encoding) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    HDC m = U(CreateEnhMetaFileW)(ref, NULL, NULL, NULL);
    UINT n; BYTE *b;
    REQUIRE(m);
    U(SelectObject)(m, U(GetStockObject)(GRAY_BRUSH));
    U(SelectObject)(m, U(GetStockObject)(NULL_PEN));
    HENHMETAFILE e = U(CloseEnhMetaFile)(m);
    REQUIRE(e);
    b = mf_getbits(e, &n);
    REQUIRE(b);
    const EMRSELECTOBJECT *s0 = (const EMRSELECTOBJECT *)mf_find(b, n, EMR_SELECTOBJECT, 0);
    const EMRSELECTOBJECT *s1 = (const EMRSELECTOBJECT *)mf_find(b, n, EMR_SELECTOBJECT, 1);
    CHECK(s0 && s1);
    if (s0 && s1) {
        EXPECT_EQ(s0->ihObject, 0x80000000u | GRAY_BRUSH);       /* ENHMETA_STOCK_OBJECT | stock index */
        EXPECT_EQ(s1->ihObject, 0x80000000u | NULL_PEN);
    }
    free(b);
    U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_rec_solid_brush_creation_record) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    HDC m = U(CreateEnhMetaFileW)(ref, NULL, NULL, NULL);
    HGDIOBJ br = U(CreateSolidBrush)(RGB(11, 22, 33));
    UINT n; BYTE *b;
    REQUIRE(m && br);
    U(SelectObject)(m, br);
    HENHMETAFILE e = U(CloseEnhMetaFile)(m);
    REQUIRE(e);
    b = mf_getbits(e, &n);
    REQUIRE(b);
    const EMRCREATEBRUSHINDIRECT *cb = (const EMRCREATEBRUSHINDIRECT *)mf_find(b, n, EMR_CREATEBRUSHINDIRECT, 0);
    const EMRSELECTOBJECT *so = (const EMRSELECTOBJECT *)mf_find(b, n, EMR_SELECTOBJECT, 0);
    CHECK(cb && so);
    if (cb && so) {
        EXPECT_EQ(cb->ihBrush, 1);                               /* first free slot after the reserved index 0 */
        EXPECT_EQ(cb->lb.lbStyle, BS_SOLID);
        EXPECT_EQ(cb->lb.lbColor, RGB(11, 22, 33));
        EXPECT_EQ(so->ihObject, 1);
    }
    free(b);
    U(DeleteEnhMetaFile)(e);
    U(DeleteObject)(br);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_rec_pen_creation_record) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    HDC m = U(CreateEnhMetaFileW)(ref, NULL, NULL, NULL);
    HGDIOBJ pen = U(CreatePen)(PS_DASH, 1, RGB(200, 100, 50));
    UINT n; BYTE *b;
    REQUIRE(m && pen);
    U(SelectObject)(m, pen);
    HENHMETAFILE e = U(CloseEnhMetaFile)(m);
    REQUIRE(e);
    b = mf_getbits(e, &n);
    REQUIRE(b);
    const EMRCREATEPEN *cp = (const EMRCREATEPEN *)mf_find(b, n, EMR_CREATEPEN, 0);
    CHECK(cp != NULL);
    if (cp) {
        EXPECT_EQ(cp->ihPen, 1);
        EXPECT_EQ(cp->lopn.lopnStyle, PS_DASH);
        EXPECT_EQ(cp->lopn.lopnColor, RGB(200, 100, 50));
        EXPECT_EQ(cp->lopn.lopnWidth.x, 1);
    }
    free(b);
    U(DeleteEnhMetaFile)(e);
    U(DeleteObject)(pen);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_rec_two_brushes_get_two_handle_slots) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    HDC m = U(CreateEnhMetaFileW)(ref, NULL, NULL, NULL);
    HGDIOBJ b1 = U(CreateSolidBrush)(RGB(1, 1, 1)), b2 = U(CreateSolidBrush)(RGB(2, 2, 2));
    UINT n; BYTE *b;
    REQUIRE(m && b1 && b2);
    U(SelectObject)(m, b1); U(SelectObject)(m, b2); U(SelectObject)(m, b1);   /* b1 already has a slot: no second create */
    HENHMETAFILE e = U(CloseEnhMetaFile)(m);
    ENHMETAHEADER h;
    REQUIRE(e);
    CHECK(U(GetEnhMetaFileHeader)(e, sizeof h, &h) != 0);
    EXPECT_EQ(h.nHandles, 3);
    b = mf_getbits(e, &n);
    REQUIRE(b);
    const EMRCREATEBRUSHINDIRECT *c0 = (const EMRCREATEBRUSHINDIRECT *)mf_find(b, n, EMR_CREATEBRUSHINDIRECT, 0);
    const EMRCREATEBRUSHINDIRECT *c1 = (const EMRCREATEBRUSHINDIRECT *)mf_find(b, n, EMR_CREATEBRUSHINDIRECT, 1);
    CHECK(c0 && c1 && !mf_find(b, n, EMR_CREATEBRUSHINDIRECT, 2));
    if (c0 && c1) { EXPECT_EQ(c0->ihBrush, 1); EXPECT_EQ(c1->ihBrush, 2); }
    const EMRSELECTOBJECT *s2 = (const EMRSELECTOBJECT *)mf_find(b, n, EMR_SELECTOBJECT, 2);
    CHECK(s2 && s2->ihObject == 1);
    free(b);
    U(DeleteEnhMetaFile)(e);
    U(DeleteObject)(b1); U(DeleteObject)(b2);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_rec_font_creation_record) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    HDC m = U(CreateEnhMetaFileW)(ref, NULL, NULL, NULL);
    LOGFONTW lf; memset(&lf, 0, sizeof lf);
    lf.lfHeight = -12; lf.lfWeight = FW_BOLD; wcscpy(lf.lfFaceName, L"Arial");
    HGDIOBJ f = U(CreateFontIndirectW)(&lf);
    struct mf_enum e;
    REQUIRE(m && f);
    U(SelectObject)(m, f);
    HENHMETAFILE hm = U(CloseEnhMetaFile)(m);
    REQUIRE(hm);
    mf_walk(hm, &e);
    EXPECT_EQ(e.counts[EMR_EXTCREATEFONTINDIRECTW], 1);
    U(DeleteEnhMetaFile)(hm);
    U(DeleteObject)(f);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_rec_gdicomment_payload_roundtrip) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    HDC m = U(CreateEnhMetaFileW)(ref, NULL, NULL, NULL);
    static const BYTE data[13] = { 'k', '3', 'r', 'n', 'e', 'l', 0, 1, 2, 3, 4, 5, 6 };
    UINT n; BYTE *b;
    REQUIRE(m);
    CHECK(U(GdiComment)(m, sizeof data, data));
    HENHMETAFILE e = U(CloseEnhMetaFile)(m);
    REQUIRE(e);
    b = mf_getbits(e, &n);
    REQUIRE(b);
    const EMRGDICOMMENT *c = (const EMRGDICOMMENT *)mf_find(b, n, EMR_GDICOMMENT, 0);
    CHECK(c != NULL);
    if (c) {
        EXPECT_EQ(c->cbData, sizeof data);
        CHECK(memcmp(c->Data, data, sizeof data) == 0);
        EXPECT_EQ(c->emr.nSize, (12 + sizeof data + 3) & ~3u);  /* header(8) + cbData(4) + payload, DWORD-padded */
    }
    free(b);
    U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_rec_savedc_returns_levels_on_metafile_dc) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    HDC m = U(CreateEnhMetaFileW)(ref, NULL, NULL, NULL);
    REQUIRE(m);
    int a = U(SaveDC)(m), b = U(SaveDC)(m);
    CHECK(a >= 1 && b == a + 1);                                  /* kernel save level */
    CHECK(U(RestoreDC)(m, -1));
    CHECK(U(RestoreDC)(m, -1));
    HENHMETAFILE e = U(CloseEnhMetaFile)(m);
    struct mf_enum en;
    REQUIRE(e);
    mf_walk(e, &en);
    EXPECT_EQ(en.counts[EMR_SAVEDC], 2);
    CHECK(en.counts[EMR_RESTOREDC] >= 2);
    U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_enum_callback_false_stops_enumeration) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    HENHMETAFILE e = mf_make(ref, NULL, NULL, 5);
    struct mf_enum en;
    REQUIRE(e && U(EnumEnhMetaFile));
    memset(&en, 0, sizeof en); en.stop_at = 2;
    RECT rc = { 0, 0, 100, 100 };
    HDC dc = mf_screen();
    REQUIRE(dc);
    int r = U(EnumEnhMetaFile)(dc, e, mf_enum_cb, &en, &rc);
    EXPECT_EQ(r, 0);                                              /* callback returned 0 */
    EXPECT_EQ(en.total, 2);
    EXPECT_EQ(en.calls_after_stop, 0);
    memset(&en, 0, sizeof en);
    r = U(EnumEnhMetaFile)(dc, e, mf_enum_cb, &en, &rc);
    CHECK(r != 0);                                                /* every call returned TRUE */
    mf_screen_free(dc);
    U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_enum_invalid_handle_fails) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    struct mf_enum en;
    RECT rc = { 0, 0, 10, 10 };
    REQUIRE(U(EnumEnhMetaFile));
    memset(&en, 0, sizeof en);
    CHECK(U(EnumEnhMetaFile)(ref, NULL, mf_enum_cb, &en, &rc) == 0);
    CHECK(U(EnumEnhMetaFile)(ref, (HENHMETAFILE)(ULONG_PTR)0x460033, mf_enum_cb, &en, &rc) == 0);
    EXPECT_EQ(en.total, 0);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_enum_handle_table_length_matches_header) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    HDC m = U(CreateEnhMetaFileW)(ref, NULL, NULL, NULL);
    struct mf_enum en; ENHMETAHEADER h;
    HGDIOBJ o[3];
    REQUIRE(m);
    o[0] = U(CreateSolidBrush)(RGB(1, 2, 3)); o[1] = U(CreatePen)(PS_SOLID, 1, 0); o[2] = U(CreateSolidBrush)(RGB(4, 5, 6));
    for (int i = 0; i < 3; i++) U(SelectObject)(m, o[i]);
    U(Rectangle)(m, 1, 1, 5, 5);
    HENHMETAFILE e = U(CloseEnhMetaFile)(m);
    REQUIRE(e);
    CHECK(U(GetEnhMetaFileHeader)(e, sizeof h, &h) != 0);
    mf_enum(e, &en);
    EXPECT_EQ(en.nobj, (int)h.nHandles);
    EXPECT_EQ(h.nHandles, 4);
    U(DeleteEnhMetaFile)(e);
    for (int i = 0; i < 3; i++) U(DeleteObject)(o[i]);
    mf_screen_free(ref);
}

/* ---- the metafile DC leaves the reference bitmap alone ------------------------------------------------------------ */
TEST(gdi_metafile_print_neg_dc_drawing_does_not_touch_reference_bitmap) {
    MF_NEED_EMF(); MF_SCREEN(scr);
    MF_NEED_DIB(d, 64, 64, 0x00123456);
    uint32_t *snap = (uint32_t *)malloc(64 * 64 * 4);
    REQUIRE(snap);
    memcpy(snap, d.bits, 64 * 64 * 4);
    HDC m = U(CreateEnhMetaFileW)(d.dc, NULL, NULL, NULL);       /* reference DC is the DIB memory DC */
    if (!m) { CHECK(m != NULL); free(snap); mf_dib_close(&d); mf_screen_free(scr); return; }
    HGDIOBJ br = U(CreateSolidBrush)(RGB(255, 0, 0));
    U(SelectObject)(m, br);
    U(Rectangle)(m, 4, 4, 40, 40);
    U(Ellipse)(m, 10, 10, 60, 60);
    U(PatBlt)(m, 0, 0, 64, 64, PATCOPY);
    U(MoveToEx)(m, 0, 0, NULL); U(LineTo)(m, 63, 63);
    U(GdiFlush)();
    CHECK(memcmp(snap, d.bits, 64 * 64 * 4) == 0);                /* nothing rendered: a metafile DC has no bitmap */
    HENHMETAFILE e = U(CloseEnhMetaFile)(m);
    U(GdiFlush)();
    CHECK(memcmp(snap, d.bits, 64 * 64 * 4) == 0);
    if (e) U(DeleteEnhMetaFile)(e);
    U(DeleteObject)(br);
    free(snap);
    mf_dib_close(&d);
    mf_screen_free(scr);
}

TEST(gdi_metafile_print_neg_control_direct_drawing_does_change_the_dib) {
    MF_NEED_DIB(d, 32, 32, 0x00123456);
    HGDIOBJ br = U(CreateSolidBrush)(RGB(255, 0, 0)), old = U(SelectObject)(d.dc, br);
    U(PatBlt)(d.dc, 4, 4, 8, 8, PATCOPY);
    U(GdiFlush)();
    EXPECT_EQ(mf_px(&d, 6, 6), 0xff0000);                         /* sanity: the kernel writes straight into the section */
    EXPECT_EQ(mf_px(&d, 20, 20), 0x123456);
    U(SelectObject)(d.dc, old);
    U(DeleteObject)(br);
    mf_dib_close(&d);
}

/* ---- playback onto a DIB ------------------------------------------------------------------------------------------ */
/* red box without outline, so the filled area is exactly the shape */
static HENHMETAFILE make_red_box(HDC ref, int l, int t, int r, int b) {
    HDC m = U(CreateEnhMetaFileW)(ref, NULL, NULL, NULL);
    HGDIOBJ br;
    if (!m) return NULL;
    br = U(CreateSolidBrush)(RGB(255, 0, 0));
    U(SelectObject)(m, U(GetStockObject)(NULL_PEN));
    U(SelectObject)(m, br);
    U(Rectangle)(m, l, t, r, b);
    HENHMETAFILE e = U(CloseEnhMetaFile)(m);
    U(DeleteObject)(br);
    return e;
}
static RECT bounds_rect(HENHMETAFILE e) {
    ENHMETAHEADER h; RECT r = { 0, 0, 0, 0 };
    memset(&h, 0, sizeof h);
    if (U(GetEnhMetaFileHeader)(e, sizeof h, &h)) { r.left = h.rclBounds.left; r.top = h.rclBounds.top; r.right = h.rclBounds.right + 1; r.bottom = h.rclBounds.bottom + 1; }
    return r;
}
static int count_px(const struct mf_dib *d, uint32_t v) { int n = 0; for (int i = 0; i < d->w * d->h; i++) if ((d->bits[i] & 0xffffff) == v) n++; return n; }

TEST(gdi_metafile_print_neg_play_rect_onto_dib_interior_and_exterior) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    MF_NEED_DIB(d, 96, 96, 0x00123456);
    HENHMETAFILE e = make_red_box(ref, 16, 16, 64, 48);
    REQUIRE(e && U(PlayEnhMetaFile));
    RECT rc = bounds_rect(e);
    t_note("playback rect {%ld,%ld,%ld,%ld}", rc.left, rc.top, rc.right, rc.bottom);
    CHECK(U(PlayEnhMetaFile)(d.dc, e, &rc));
    U(GdiFlush)();
    for (int y = 20; y < 44; y += 3) for (int x = 20; x < 60; x += 3) EXPECT_EQ(mf_px(&d, x, y), 0xff0000);   /* well inside */
    for (int x = 0; x < 96; x += 5) { EXPECT_EQ(mf_px(&d, x, 4), 0x123456); EXPECT_EQ(mf_px(&d, x, 90), 0x123456); }
    for (int y = 0; y < 96; y += 5) { EXPECT_EQ(mf_px(&d, 4, y), 0x123456); EXPECT_EQ(mf_px(&d, 90, y), 0x123456); }
    int red = count_px(&d, 0xff0000);
    t_note("red pixels: %d (box 48x32 = %d)", red, 48 * 32);
    CHECK(red >= (48 * 32) * 85 / 100 && red <= (48 * 32) * 115 / 100);
    U(DeleteEnhMetaFile)(e);
    mf_dib_close(&d);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_play_only_touches_destination_rectangle) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    MF_NEED_DIB(d, 128, 96, 0x00123456);
    HENHMETAFILE e = make_red_box(ref, 8, 8, 40, 40);
    REQUIRE(e);
    RECT rc = { 60, 20, 60 + 32, 20 + 32 };
    CHECK(U(PlayEnhMetaFile)(d.dc, e, &rc));
    U(GdiFlush)();
    int outside = 0;
    for (int y = 0; y < d.h; y++) for (int x = 0; x < d.w; x++) {
        uint32_t v = mf_px(&d, x, y);
        /* the box is scaled from the metafile's frame, which can put its edge one pixel beyond the destination rectangle */
        if (v != 0x123456 && (x < rc.left - 1 || x > rc.right + 1 || y < rc.top - 1 || y > rc.bottom + 1)) outside++;
    }
    EXPECT_EQ(outside, 0);
    EXPECT_EQ(mf_px(&d, 75, 35), 0xff0000);
    EXPECT_EQ(mf_px(&d, 20, 20), 0x123456);                       /* the source coordinates are NOT where it lands */
    U(DeleteEnhMetaFile)(e);
    mf_dib_close(&d);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_play_scales_to_destination_rect) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    MF_NEED_DIB(d, 160, 128, 0x00123456);
    HENHMETAFILE e = make_red_box(ref, 8, 8, 40, 40);
    REQUIRE(e);
    RECT rc = bounds_rect(e);
    RECT big = { 10, 10, 10 + (rc.right - rc.left) * 3, 10 + (rc.bottom - rc.top) * 3 };
    CHECK(U(PlayEnhMetaFile)(d.dc, e, &big));
    U(GdiFlush)();
    int red = count_px(&d, 0xff0000), want = 32 * 32 * 9;
    t_note("3x playback: %d red pixels, ideal %d", red, want);
    CHECK(red >= want * 85 / 100 && red <= want * 115 / 100);
    EXPECT_EQ(mf_px(&d, 10 + 48, 10 + 48), 0xff0000);
    U(DeleteEnhMetaFile)(e);
    mf_dib_close(&d);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_play_twice_is_idempotent) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    MF_NEED_DIB(d, 96, 96, 0x00123456);
    HENHMETAFILE e = make_red_box(ref, 16, 16, 64, 48);
    REQUIRE(e);
    RECT rc = bounds_rect(e);
    uint32_t *snap = (uint32_t *)malloc(96 * 96 * 4);
    REQUIRE(snap);
    CHECK(U(PlayEnhMetaFile)(d.dc, e, &rc)); U(GdiFlush)();
    memcpy(snap, d.bits, 96 * 96 * 4);
    CHECK(U(PlayEnhMetaFile)(d.dc, e, &rc)); U(GdiFlush)();
    CHECK(memcmp(snap, d.bits, 96 * 96 * 4) == 0);
    free(snap);
    U(DeleteEnhMetaFile)(e);
    mf_dib_close(&d);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_play_later_records_paint_over_earlier) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    MF_NEED_DIB(d, 96, 96, 0x00123456);
    HDC m = U(CreateEnhMetaFileW)(ref, NULL, NULL, NULL);
    HGDIOBJ red = U(CreateSolidBrush)(RGB(255, 0, 0)), grn = U(CreateSolidBrush)(RGB(0, 255, 0));
    REQUIRE(m && red && grn);
    U(SelectObject)(m, U(GetStockObject)(NULL_PEN));
    U(SelectObject)(m, red); U(Rectangle)(m, 10, 10, 60, 60);
    U(SelectObject)(m, grn); U(Rectangle)(m, 30, 30, 80, 80);
    HENHMETAFILE e = U(CloseEnhMetaFile)(m);
    REQUIRE(e);
    RECT rc = bounds_rect(e);
    CHECK(U(PlayEnhMetaFile)(d.dc, e, &rc)); U(GdiFlush)();
    EXPECT_EQ(mf_px(&d, 20, 20), 0xff0000);
    EXPECT_EQ(mf_px(&d, 45, 45), 0x00ff00);                       /* overlap: second record wins */
    EXPECT_EQ(mf_px(&d, 70, 70), 0x00ff00);
    EXPECT_EQ(mf_px(&d, 70, 20), 0x123456);
    U(DeleteEnhMetaFile)(e);
    U(DeleteObject)(red); U(DeleteObject)(grn);
    mf_dib_close(&d);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_play_survives_deleting_source_brush) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    MF_NEED_DIB(d, 96, 96, 0x00123456);
    HDC m = U(CreateEnhMetaFileW)(ref, NULL, NULL, NULL);
    HGDIOBJ br = U(CreateSolidBrush)(RGB(0, 0, 255));
    REQUIRE(m && br);
    U(SelectObject)(m, U(GetStockObject)(NULL_PEN));
    U(SelectObject)(m, br);
    U(Rectangle)(m, 10, 10, 60, 60);
    HENHMETAFILE e = U(CloseEnhMetaFile)(m);
    REQUIRE(e);
    CHECK(U(DeleteObject)(br));                                   /* the record embeds the LOGBRUSH, not the handle */
    RECT rc = bounds_rect(e);
    CHECK(U(PlayEnhMetaFile)(d.dc, e, &rc)); U(GdiFlush)();
    EXPECT_EQ(mf_px(&d, 30, 30), 0x0000ff);
    U(DeleteEnhMetaFile)(e);
    mf_dib_close(&d);
    mf_screen_free(ref);
}

static int CALLBACK play_cb(HDC hdc, HANDLETABLE *ht, const ENHMETARECORD *r, int n, LPARAM lp) {
    (*(int *)lp)++;
    return U(PlayEnhMetaFileRecord)(hdc, ht, r, n);
}

TEST(gdi_metafile_print_neg_play_record_by_record_matches_whole_playback) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    MF_NEED_DIB(a, 96, 96, 0x00123456);
    MF_NEED_DIB(b, 96, 96, 0x00123456);
    REQUIRE(U(PlayEnhMetaFileRecord) && U(EnumEnhMetaFile));
    HENHMETAFILE e = make_red_box(ref, 16, 16, 64, 48);
    REQUIRE(e);
    RECT rc = bounds_rect(e);
    int calls = 0;
    CHECK(U(PlayEnhMetaFile)(a.dc, e, &rc));
    CHECK(U(EnumEnhMetaFile)(b.dc, e, play_cb, &calls, &rc));      /* NtGdiPlayEnhMetaFileRecord per record */
    U(GdiFlush)();
    CHECK(memcmp(a.bits, b.bits, 96 * 96 * 4) == 0);
    ENHMETAHEADER h;
    CHECK(U(GetEnhMetaFileHeader)(e, sizeof h, &h) != 0);
    EXPECT_EQ(calls, (int)h.nRecords);
    U(DeleteEnhMetaFile)(e);
    mf_dib_close(&a); mf_dib_close(&b);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_play_into_memory_dc_leaves_dc_state_restored) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    MF_NEED_DIB(d, 96, 96, 0x00123456);
    HENHMETAFILE e = make_red_box(ref, 16, 16, 64, 48);
    REQUIRE(e);
    HGDIOBJ pen0 = U(GetCurrentObject)(d.dc, OBJ_PEN), br0 = U(GetCurrentObject)(d.dc, OBJ_BRUSH);
    COLORREF bk0 = U(GetBkColor)(d.dc);
    int map0 = U(GetMapMode)(d.dc);
    RECT rc = bounds_rect(e);
    CHECK(U(PlayEnhMetaFile)(d.dc, e, &rc));
    EXPECT_EQ(U(GetCurrentObject)(d.dc, OBJ_PEN), pen0);          /* bInternalPlayEMF saves/restores the DC */
    EXPECT_EQ(U(GetCurrentObject)(d.dc, OBJ_BRUSH), br0);
    EXPECT_EQ(U(GetBkColor)(d.dc), bk0);
    EXPECT_EQ(U(GetMapMode)(d.dc), map0);
    U(DeleteEnhMetaFile)(e);
    mf_dib_close(&d);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_play_null_dc_fails_87) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    HENHMETAFILE e = mf_make(ref, NULL, NULL, 1);
    RECT rc = { 0, 0, 100, 100 };
    REQUIRE(e && U(PlayEnhMetaFile));
    SetLastError(0);
    CHECK(!U(PlayEnhMetaFile)(NULL, e, &rc));                     /* hdc == NULL -> GdiSetLastError(0x57) */
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_play_invalid_metafile_handles_fail) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    MF_NEED_DIB(d, 32, 32, 0x00123456);
    RECT rc = { 0, 0, 32, 32 };
    REQUIRE(U(PlayEnhMetaFile));
    HENHMETAFILE e = mf_make(ref, NULL, NULL, 1);
    REQUIRE(e);
    U(DeleteEnhMetaFile)(e);
    HGDIOBJ br = U(CreateSolidBrush)(0);
    HENHMETAFILE bad[] = { NULL, e, (HENHMETAFILE)br, (HENHMETAFILE)(ULONG_PTR)0x460777, (HENHMETAFILE)d.dc };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) CHECK(!U(PlayEnhMetaFile)(d.dc, bad[i], &rc));
    U(GdiFlush)();
    EXPECT_EQ(count_px(&d, 0x123456), 32 * 32);
    U(DeleteObject)(br);
    mf_dib_close(&d);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_play_invalid_target_dc_fails) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    HENHMETAFILE e = mf_make(ref, NULL, NULL, 1);
    RECT rc = { 0, 0, 100, 100 };
    REQUIRE(e && U(PlayEnhMetaFile));
    HDC bad[] = { (HDC)(ULONG_PTR)0x1234, (HDC)U(GetStockObject)(BLACK_BRUSH) };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) CHECK(!U(PlayEnhMetaFile)(bad[i], e, &rc));
    U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_play_into_another_metafile_dc_is_recorded) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    HENHMETAFILE inner = mf_make(ref, NULL, NULL, 2);
    REQUIRE(inner && U(PlayEnhMetaFile));
    RECT rc = bounds_rect(inner);
    HDC m = U(CreateEnhMetaFileW)(ref, NULL, NULL, NULL);
    REQUIRE(m);
    CHECK(U(PlayEnhMetaFile)(m, inner, &rc));
    HENHMETAFILE outer = U(CloseEnhMetaFile)(m);
    struct mf_enum en;
    REQUIRE(outer);
    mf_enum(outer, &en);
    t_note("nested playback: outer has %d records, %d rectangles, %d comments", en.total, en.counts[EMR_RECTANGLE], en.counts[EMR_GDICOMMENT]);
    CHECK(en.total > 2);                                          /* something was recorded */
    U(DeleteEnhMetaFile)(outer);
    U(DeleteEnhMetaFile)(inner);
    mf_screen_free(ref);
}

/* ---- DC state the kernel keeps for a metafile DC (shared DC_ATTR page) --------------------------------------------- */
TEST(gdi_metafile_print_neg_dc_defaults_match_a_fresh_display_dc) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    HDC m = U(CreateEnhMetaFileW)(ref, NULL, NULL, NULL);
    REQUIRE(m);
    EXPECT_EQ(U(GetROP2)(m), R2_COPYPEN);
    EXPECT_EQ(U(GetBkMode)(m), OPAQUE);
    EXPECT_EQ(U(GetMapMode)(m), MM_TEXT);
    EXPECT_EQ(U(GetPolyFillMode)(m), ALTERNATE);
    EXPECT_EQ(U(GetTextColor)(m), RGB(0, 0, 0));
    EXPECT_EQ(U(GetBkColor)(m), RGB(255, 255, 255));
    EXPECT_EQ(U(GetTextAlign)(m), TA_TOP | TA_LEFT);
    EXPECT_EQ(U(GetStretchBltMode)(m), BLACKONWHITE);
    EXPECT_EQ(U(GetGraphicsMode)(m), GM_COMPATIBLE);
    EXPECT_EQ(U(GetCurrentObject)(m, OBJ_PEN), U(GetStockObject)(BLACK_PEN));
    EXPECT_EQ(U(GetCurrentObject)(m, OBJ_BRUSH), U(GetStockObject)(WHITE_BRUSH));
    HENHMETAFILE e = U(CloseEnhMetaFile)(m);
    if (e) U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_dc_set_get_roundtrip) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    HDC m = U(CreateEnhMetaFileW)(ref, NULL, NULL, NULL);
    REQUIRE(m);
    U(SetBkColor)(m, RGB(1, 2, 3)); EXPECT_EQ(U(GetBkColor)(m), RGB(1, 2, 3));
    U(SetTextColor)(m, RGB(9, 8, 7)); EXPECT_EQ(U(GetTextColor)(m), RGB(9, 8, 7));
    U(SetBkMode)(m, TRANSPARENT); EXPECT_EQ(U(GetBkMode)(m), TRANSPARENT);
    U(SetROP2)(m, R2_XORPEN); EXPECT_EQ(U(GetROP2)(m), R2_XORPEN);
    U(SetPolyFillMode)(m, WINDING); EXPECT_EQ(U(GetPolyFillMode)(m), WINDING);
    U(SetTextAlign)(m, TA_BASELINE | TA_CENTER); EXPECT_EQ(U(GetTextAlign)(m), TA_BASELINE | TA_CENTER);
    U(SetStretchBltMode)(m, HALFTONE); EXPECT_EQ(U(GetStretchBltMode)(m), HALFTONE);
    U(SetMapMode)(m, MM_LOMETRIC); EXPECT_EQ(U(GetMapMode)(m), MM_LOMETRIC);
    HENHMETAFILE e = U(CloseEnhMetaFile)(m);
    if (e) U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_dc_select_returns_previous_objects) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    HDC m = U(CreateEnhMetaFileW)(ref, NULL, NULL, NULL);
    HGDIOBJ br = U(CreateSolidBrush)(RGB(5, 5, 5)), pen = U(CreatePen)(PS_SOLID, 2, 0);
    REQUIRE(m && br && pen);
    EXPECT_EQ(U(SelectObject)(m, br), U(GetStockObject)(WHITE_BRUSH));
    EXPECT_EQ(U(SelectObject)(m, pen), U(GetStockObject)(BLACK_PEN));
    EXPECT_EQ(U(GetCurrentObject)(m, OBJ_BRUSH), br);
    EXPECT_EQ(U(GetCurrentObject)(m, OBJ_PEN), pen);
    EXPECT_EQ(U(SelectObject)(m, U(GetStockObject)(BLACK_BRUSH)), br);
    HENHMETAFILE e = U(CloseEnhMetaFile)(m);
    if (e) U(DeleteEnhMetaFile)(e);
    U(DeleteObject)(br); U(DeleteObject)(pen);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_dc_viewport_window_origin_roundtrip) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    HDC m = U(CreateEnhMetaFileW)(ref, NULL, NULL, NULL);
    POINT p; SIZE s;
    REQUIRE(m);
    CHECK(U(SetViewportOrgEx)(m, 5, 6, NULL)); CHECK(U(GetViewportOrgEx)(m, &p)); EXPECT_EQ(p.x, 5); EXPECT_EQ(p.y, 6);
    CHECK(U(SetWindowOrgEx)(m, -7, 8, NULL)); CHECK(U(GetWindowOrgEx)(m, &p)); EXPECT_EQ(p.x, -7); EXPECT_EQ(p.y, 8);
    U(SetMapMode)(m, MM_ANISOTROPIC);
    CHECK(U(SetWindowExtEx)(m, 100, 200, NULL)); CHECK(U(GetWindowExtEx)(m, &s)); EXPECT_EQ(s.cx, 100); EXPECT_EQ(s.cy, 200);
    CHECK(U(SetViewportExtEx)(m, 300, 400, NULL)); CHECK(U(GetViewportExtEx)(m, &s)); EXPECT_EQ(s.cx, 300); EXPECT_EQ(s.cy, 400);
    HENHMETAFILE e = U(CloseEnhMetaFile)(m);
    if (e) U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_dc_attr_page_is_present_for_metafile_dc) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    HDC m = U(CreateEnhMetaFileW)(ref, NULL, NULL, NULL);
    struct mf_cell c;
    REQUIRE(m);
    if (t_is_wine() || !mf_cell_of(m, &c)) { t_skip("GDI shared table not readable"); HENHMETAFILE e = U(CloseEnhMetaFile)(m); if (e) U(DeleteEnhMetaFile)(e); mf_screen_free(ref); return; }
    CHECK(c.puser != 0);
    /* pUser points at the DC_ATTR the kernel allocated in the process's GDI attribute section: user-mode address range */
    CHECK(c.puser < 0x00007fffffffffffull);
    unsigned char probe[8];
    CHECK(k_try_read((void *)(ULONG_PTR)c.puser, probe, sizeof probe));
    HENHMETAFILE e = U(CloseEnhMetaFile)(m);
    if (e) U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_abort_proc_on_metafile_dc) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    HDC m = U(CreateEnhMetaFileW)(ref, NULL, NULL, NULL);
    REQUIRE(m && U(SetAbortProc));
    EXPECT_EQ(U(SetAbortProc)(m, (ABORTPROC)(ULONG_PTR)0x1000), 1);   /* ldc present, not in a document: only stores the pointer */
    EXPECT_EQ(U(SetAbortProc)(m, NULL), 1);
    HENHMETAFILE e = U(CloseEnhMetaFile)(m);
    if (e) U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}
