/* Brushes: gdi32full!CreateSolidBrush/CreateHatchBrush/CreatePatternBrush/CreateDIBPatternBrushPt/CreateBrushIndirect
 * -> NtGdiCreateSolidBrush / NtGdiCreateHatchBrushInternal / NtGdiCreatePatternBrushInternal / NtGdiCreateDIBBrush.
 * Read-back is win32kfull!cjGetBrushOrPen (non-pen branch): LOGBRUSH written as {style, color, hatch}, always 0x10 bytes. */
#include "pb.h"

static int getlb(HGDIOBJ h, LOGBRUSH *lb) { memset(lb, 0xCC, sizeof *lb); return pb_getobj(h, sizeof *lb, lb); }

TEST(brushes_solid_roundtrip) {
    PB_GDI(); REQUIRE(U(CreateSolidBrush));
    static const COLORREF cs[] = { 0, RGB(255, 255, 255), RGB(255, 0, 0), RGB(0, 255, 0), RGB(0, 0, 255), RGB(1, 2, 3), RGB(127, 128, 129), RGB(254, 254, 254) };
    for (size_t i = 0; i < sizeof cs / sizeof cs[0]; i++) {
        HBRUSH b = U(CreateSolidBrush)(cs[i]);
        CHECKF(b != NULL, "color 0x%lx", cs[i]);
        if (!b) continue;
        LOGBRUSH lb;
        CHECK(getlb(b, &lb) == (int)sizeof lb);
        CHECK(lb.lbStyle == BS_SOLID);
        CHECKF(lb.lbColor == cs[i], "0x%lx -> 0x%lx", cs[i], lb.lbColor);
        CHECK(lb.lbHatch == 0);
        CHECK(U(GetObjectType)(b) == OBJ_BRUSH);
        CHECK(pb_del(b));
    }
}
TEST(brushes_solid_handle_bits) {
    PB_GDI(); REQUIRE(U(CreateSolidBrush));
    HBRUSH b = U(CreateSolidBrush)(RGB(9, 9, 9));
    REQUIRE(b);
    CHECKF(PB_T7(b) == T7_BRUSH, "type7 0x%x", PB_T7(b));
    CHECK(!PB_STOCKED(b));
    pb_del(b);
}
TEST(brushes_solid_cell_and_attr) {
    PB_GDI(); PB_TABLE(); REQUIRE(U(CreateSolidBrush));
    HBRUSH b = U(CreateSolidBrush)(RGB(11, 22, 33));
    REQUIRE(b);
    struct pb_cell c;
    CHECK(pb_cell_of(b, &c));
    CHECK(c.objt == 0x10);
    CHECK((c.owner & ~1u) == PB_PID());
    CHECK(c.upper == (unsigned short)((ULONG_PTR)b >> 16));
    DWORD a[2] = { 0, 0 };
    if (c.puser && k_try_read((void *)(ULONG_PTR)c.puser, a, sizeof a)) {
        CHECKF(a[1] == RGB(11, 22, 33) || (a[0] & 4), "BRUSHATTR {0x%lx, 0x%lx}", a[0], a[1]);
    } else t_note("solid brush cell pUser unreadable (0x%llx)", c.puser);
    pb_del(b);
}
TEST(brushes_solid_color_high_byte) {
    PB_GDI(); REQUIRE(U(CreateSolidBrush));
    static const COLORREF cs[] = { 0x01102030, 0x02102030, 0x80102030, 0xFF102030 };
    for (size_t i = 0; i < sizeof cs / sizeof cs[0]; i++) {
        HBRUSH b = U(CreateSolidBrush)(cs[i]);
        LOGBRUSH lb;
        if (!b || getlb(b, &lb) != (int)sizeof lb) { CHECK(b != NULL); continue; }
        CHECK((lb.lbColor & 0xFFFFFF) == (cs[i] & 0xFFFFFF));
        t_note("solid brush 0x%08lx reads back 0x%08lx", cs[i], lb.lbColor);
        pb_del(b);
    }
}
TEST(brushes_attr_dirty_flag_sync) {
    PB_GDI(); PB_TABLE(); REQUIRE(U(CreateSolidBrush));
    HBRUSH b = U(CreateSolidBrush)(RGB(1, 2, 3));
    REQUIRE(b);
    DWORD *attr = (DWORD *)pb_attr(b);
    DWORD a[2];
    if (!attr || !k_try_read(attr, a, sizeof a)) { t_note("no BRUSHATTR"); pb_del(b); return; }
    /* gdi32full!CreatePen updates a cached pen exactly like this: attr[1] = color; attr[0] |= 4. The kernel picks it up
     * in the brush-sync path and clears bit 4. */
    attr[1] = RGB(200, 100, 50);
    attr[0] |= 4;
    LOGBRUSH lb;
    CHECK(getlb(b, &lb) == (int)sizeof lb);
    CHECKF(lb.lbColor == RGB(200, 100, 50), "color 0x%lx", lb.lbColor);
    CHECK(lb.lbStyle == BS_SOLID);
    CHECKF((attr[0] & 4) == 0, "flags 0x%lx", attr[0]);
    CHECK(attr[1] == RGB(200, 100, 50));
    pb_del(b);
}
TEST(brushes_attr_clean_flag_no_sync) {
    PB_GDI(); PB_TABLE(); REQUIRE(U(CreateSolidBrush));
    HBRUSH b = U(CreateSolidBrush)(RGB(5, 6, 7));
    REQUIRE(b);
    DWORD *attr = (DWORD *)pb_attr(b);
    DWORD a[2];
    if (!attr || !k_try_read(attr, a, sizeof a)) { t_note("no BRUSHATTR"); pb_del(b); return; }
    LOGBRUSH lb;
    getlb(b, &lb); /* make sure any pending bit is consumed */
    DWORD saved = attr[1];
    attr[0] &= ~4u;
    attr[1] = RGB(99, 99, 99); /* without the dirty bit the kernel must ignore this word */
    CHECK(getlb(b, &lb) == (int)sizeof lb);
    CHECK(lb.lbColor == RGB(5, 6, 7));
    attr[1] = saved;
    pb_del(b);
}
TEST(brushes_nt_solid_matches_userland) {
    REQUIRE(UN(NtGdiCreateSolidBrush)); PB_GDI();
    HANDLE a = UN(NtGdiCreateSolidBrush)(RGB(12, 34, 56), NULL);
    CHECK(a != NULL);
    if (!a) return;
    LOGBRUSH lb;
    CHECK(getlb(a, &lb) == 0x10);
    CHECK(lb.lbStyle == BS_SOLID && lb.lbColor == RGB(12, 34, 56));
    CHECK(PB_T7(a) == T7_BRUSH);
    pb_del(a);
}
TEST(brushes_nt_solid_reset_existing) {
    REQUIRE(UN(NtGdiCreateSolidBrush)); PB_GDI();
    HANDLE a = UN(NtGdiCreateSolidBrush)(RGB(1, 1, 1), NULL);
    REQUIRE(a);
    /* the internal solid-brush creation with a non-NULL hbr re-colours the brush and returns hbr */
    HANDLE r = UN(NtGdiCreateSolidBrush)(RGB(77, 88, 99), a);
    CHECK(r == a);
    LOGBRUSH lb;
    CHECK(getlb(a, &lb) == 0x10);
    CHECKF(lb.lbColor == RGB(77, 88, 99), "color 0x%lx", lb.lbColor);
    pb_del(a);
}
TEST(brushes_nt_solid_reset_wrong_object) {
    REQUIRE(UN(NtGdiCreateSolidBrush)); PB_GDI();
    LOGBRUSH hl = { BS_HATCHED, RGB(1, 2, 3), HS_CROSS };
    HBRUSH h = U(CreateBrushIndirect)(&hl);
    REQUIRE(h);
    /* re-colouring requires the brush's solid flag (0x10): a hatch brush is refused -> NULL */
    HANDLE r = UN(NtGdiCreateSolidBrush)(RGB(9, 9, 9), h);
    CHECK(r == NULL);
    LOGBRUSH lb;
    CHECK(getlb(h, &lb) == 0x10 && lb.lbStyle == BS_HATCHED && lb.lbColor == RGB(1, 2, 3));
    HGDIOBJ pen = U(CreatePen)(PS_SOLID, 3, 0);
    r = UN(NtGdiCreateSolidBrush)(RGB(9, 9, 9), pen); /* pen-type brush objects have flag 0x400, parameter 0 does not match */
    CHECK(r == NULL);
    pb_del(pen); pb_del(h);
}
TEST(brushes_nt_solid_reset_stock_refused) {
    REQUIRE(UN(NtGdiCreateSolidBrush)); PB_GDI();
    HGDIOBJ w = U(GetStockObject)(WHITE_BRUSH);
    REQUIRE(w);
 /* stock brushes carry flag 0x200 which re-colouring refuses when argument 4 == 0 */
    HANDLE r = UN(NtGdiCreateSolidBrush)(RGB(1, 2, 3), w);
    CHECK(r == NULL);
    LOGBRUSH lb; CHECK(getlb(w, &lb) == 0x10 && lb.lbColor == RGB(255, 255, 255));
}
TEST(brushes_nt_solid_reset_invalid_handle) {
    REQUIRE(UN(NtGdiCreateSolidBrush)); PB_GDI();
    CHECK(UN(NtGdiCreateSolidBrush)(0, (HANDLE)(ULONG_PTR)0x1234) == NULL);
    CHECK(UN(NtGdiCreateSolidBrush)(0, (HANDLE)(ULONG_PTR)0xFFFFFFFFFFFFull) == NULL);
}
TEST(brushes_hatch_all_styles) {
    PB_GDI(); REQUIRE(U(CreateHatchBrush));
    for (int h = HS_HORIZONTAL; h <= HS_DIAGCROSS; h++) {
        COLORREF c = RGB(10 * h + 1, 20, 30);
        HBRUSH b = U(CreateHatchBrush)(h, c);
        CHECKF(b != NULL, "hatch %d", h);
        if (!b) continue;
        LOGBRUSH lb;
        CHECK(getlb(b, &lb) == 0x10);
        CHECK(lb.lbStyle == BS_HATCHED);
        CHECKF(lb.lbColor == c, "hatch %d color 0x%lx", h, lb.lbColor);
        CHECKF(lb.lbHatch == (ULONG_PTR)h, "hatch %d -> %llu", h, (unsigned long long)lb.lbHatch);
        CHECK(PB_T7(b) == T7_BRUSH);
        CHECK(U(GetObjectType)(b) == OBJ_BRUSH);
        pb_del(b);
    }
}
TEST(brushes_hatch_out_of_range) {
    PB_GDI(); REQUIRE(U(CreateHatchBrush));
    /* hCreateHatchBrushInternal, otherwise NULL (no last error is set) */
    static const int bad[] = { 12, 13, 100, 0x10000, 0x7fffffff, -1, INT_MIN };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        HBRUSH b = U(CreateHatchBrush)(bad[i], RGB(1, 2, 3));
        CHECKF(b == NULL, "hatch %d", bad[i]);
        if (b) pb_del(b);
    }
}
TEST(brushes_hatch_documented_range_edge) {
    PB_GDI(); REQUIRE(U(CreateHatchBrush));
    for (int h = 6; h < 12; h++) {
        HBRUSH b = U(CreateHatchBrush)(h, 0);
        t_note("CreateHatchBrush(%d) -> %p", h, (void *)b);
        if (b) pb_del(b);
    }
}
TEST(brushes_nt_hatch_direct) {
    REQUIRE(UN(NtGdiCreateHatchBrushInternal)); PB_GDI();
    for (ULONG h = 0; h < 6; h++) {
        HANDLE b = UN(NtGdiCreateHatchBrushInternal)(h, RGB(4, 5, 6), FALSE);
        CHECKF(b != NULL, "hatch %lu", h);
        if (!b) continue;
        LOGBRUSH lb; CHECK(getlb(b, &lb) == 0x10 && lb.lbStyle == BS_HATCHED && lb.lbHatch == h && lb.lbColor == RGB(4, 5, 6));
        pb_del(b);
    }
    CHECK(UN(NtGdiCreateHatchBrushInternal)(12, 0, FALSE) == NULL);
    CHECK(UN(NtGdiCreateHatchBrushInternal)(0xFFFFFFFF, 0, FALSE) == NULL);
}
TEST(brushes_nt_hatch_pen_flag) {
    REQUIRE(UN(NtGdiCreateHatchBrushInternal)); PB_GDI();
    /* bPen = 1 is what ExtCreatePen passes; the resulting object is a brush-type handle (type 0x10) */
    HANDLE b = UN(NtGdiCreateHatchBrushInternal)(HS_CROSS, RGB(1, 1, 1), TRUE);
    if (!b) { t_note("hatch with bPen=1 returned NULL"); return; }
    CHECK(PB_T7(b) == T7_BRUSH);
    pb_del(b);
}
TEST(brushes_pattern_from_bitmap) {
    PB_GDI(); REQUIRE(U(CreatePatternBrush)); REQUIRE(U(CreateBitmap));
    static const struct { int w, h, planes, bpp; } v[] = { {8, 8, 1, 1}, {8, 8, 1, 32}, {16, 16, 1, 1}, {4, 4, 1, 32}, {1, 1, 1, 1}, {3, 5, 1, 32}, {32, 32, 1, 1} };
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        HBITMAP bm = U(CreateBitmap)(v[i].w, v[i].h, v[i].planes, v[i].bpp, NULL);
        REQUIRE(bm);
        HBRUSH b = U(CreatePatternBrush)(bm);
        CHECKF(b != NULL, "%dx%dx%d err %lu", v[i].w, v[i].h, v[i].bpp, GetLastError());
        if (b) {
            LOGBRUSH lb;
            CHECK(getlb(b, &lb) == 0x10);
            CHECK(lb.lbStyle == BS_PATTERN); /* cjGetBrushOrPen: flag 0x40 -> style 3 */
            CHECK(lb.lbHatch != 0);
            CHECK(PB_T7(b) == T7_BRUSH);
            pb_del(b);
        }
        pb_del(bm);
    }
}
TEST(brushes_pattern_survives_bitmap_delete) {
    PB_GDI(); REQUIRE(U(CreatePatternBrush)); REQUIRE(U(CreateBitmap));
    HBITMAP bm = U(CreateBitmap)(8, 8, 1, 1, NULL);
    REQUIRE(bm);
    HBRUSH b = U(CreatePatternBrush)(bm);
    REQUIRE(b);
    LOGBRUSH l1, l2;
    CHECK(getlb(b, &l1) == 0x10);
    /* pattern-brush creation clones the surface: the brush owns a private copy */
    CHECK(l1.lbHatch != (ULONG_PTR)bm || (t_note("pattern brush references the source bitmap handle"), 1));
    CHECK(pb_del(bm));
    CHECK(getlb(b, &l2) == 0x10);
    CHECK(l2.lbStyle == BS_PATTERN);
    CHECK(l2.lbHatch == l1.lbHatch);
    CHECK(pb_del(b));
}
TEST(brushes_pattern_invalid_bitmap) {
    PB_GDI(); REQUIRE(U(CreatePatternBrush));
    static const ULONG_PTR bad[] = { 0, 1, 0x12345, 0xFFFFFFFF };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(0);
        HBRUSH b = U(CreatePatternBrush)((HBITMAP)bad[i]);
        CHECKF(b == NULL, "handle 0x%llx", (unsigned long long)bad[i]);
        EXPECT_ERR(ERROR_INVALID_HANDLE); /* pattern-brush creation: handle check fails -> EngSetLastError(6) */
        if (b) pb_del(b);
    }
}
TEST(brushes_pattern_wrong_object_type) {
    PB_GDI(); REQUIRE(U(CreatePatternBrush));
    HGDIOBJ other[3];
    other[0] = U(GetStockObject)(WHITE_BRUSH);
    other[1] = U(CreatePen)(PS_SOLID, 3, 0);
    other[2] = U(CreateRectRgn) ? U(CreateRectRgn)(0, 0, 4, 4) : NULL;
    for (int i = 0; i < 3; i++) {
        if (!other[i]) continue;
        SetLastError(0);
        HBRUSH b = U(CreatePatternBrush)((HBITMAP)other[i]);
        CHECKF(b == NULL, "object %d", i);
        if (b) pb_del(b);
    }
    pb_del(other[1]); pb_del(other[2]);
}
TEST(brushes_nt_pattern_direct) {
    REQUIRE(UN(NtGdiCreatePatternBrushInternal)); PB_GDI(); REQUIRE(U(CreateBitmap));
    HBITMAP bm = U(CreateBitmap)(8, 8, 1, 1, NULL);
    REQUIRE(bm);
    HANDLE b = UN(NtGdiCreatePatternBrushInternal)(bm, FALSE, FALSE);
    CHECK(b != NULL);
    if (b) { LOGBRUSH lb; CHECK(getlb(b, &lb) == 0x10 && lb.lbStyle == BS_PATTERN); pb_del(b); }
    SetLastError(0);
    CHECK(UN(NtGdiCreatePatternBrushInternal)(NULL, FALSE, FALSE) == NULL);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    pb_del(bm);
}
TEST(brushes_pattern_monochrome_via_indirect) {
    PB_GDI(); REQUIRE(U(CreateBrushIndirect)); REQUIRE(U(CreateBitmap));
    HBITMAP bm = U(CreateBitmap)(8, 8, 1, 1, NULL);
    REQUIRE(bm);
    LOGBRUSH in = { BS_PATTERN, 0, (ULONG_PTR)bm }, out;
    HBRUSH b = U(CreateBrushIndirect)(&in);
    CHECK(b != NULL);
    if (b) { CHECK(getlb(b, &out) == 0x10 && out.lbStyle == BS_PATTERN); pb_del(b); }
    in.lbStyle = BS_MONOPATTERN; /* CreateBrush(7): NtGdiCreatePatternBrushInternal(hbm, 0, 1) */
    b = U(CreateBrushIndirect)(&in);
    t_note("BS_MONOPATTERN brush -> %p", (void *)b);
    if (b) pb_del(b);
    pb_del(bm);
}
TEST(brushes_dib_pattern_packed) {
    PB_GDI(); REQUIRE(U(CreateDIBPatternBrushPt));
    struct { BITMAPINFOHEADER h; DWORD px[64]; } d;
    memset(&d, 0, sizeof d);
    d.h.biSize = sizeof d.h; d.h.biWidth = 8; d.h.biHeight = 8; d.h.biPlanes = 1; d.h.biBitCount = 32;
    for (int i = 0; i < 64; i++) d.px[i] = 0x010101 * i;
    HBRUSH b = U(CreateDIBPatternBrushPt)(&d, DIB_RGB_COLORS);
    CHECKF(b != NULL, "err %lu", GetLastError());
    if (!b) return;
    LOGBRUSH lb;
    CHECK(getlb(b, &lb) == 0x10);
    CHECKF(lb.lbStyle == BS_DIBPATTERN || lb.lbStyle == BS_PATTERN, "style %u", lb.lbStyle);
    if (lb.lbStyle != BS_DIBPATTERN) t_note("DIB pattern brush reports style %u", lb.lbStyle);
    CHECK(PB_T7(b) == T7_BRUSH);
    pb_del(b);
}
TEST(brushes_dib_pattern_null) {
    PB_GDI(); REQUIRE(U(CreateDIBPatternBrushPt));
    CHECK(U(CreateDIBPatternBrushPt)(NULL, DIB_RGB_COLORS) == NULL); /* gdi32full returns 0 before any syscall */
}
TEST(brushes_dib_pattern_usage_variants) {
    PB_GDI(); REQUIRE(U(CreateDIBPatternBrushPt));
    struct { BITMAPINFOHEADER h; RGBQUAD pal[2]; BYTE bits[8 * 4]; } d;
    memset(&d, 0, sizeof d);
    d.h.biSize = sizeof d.h; d.h.biWidth = 8; d.h.biHeight = 8; d.h.biPlanes = 1; d.h.biBitCount = 1; d.h.biClrUsed = 2;
    d.pal[1].rgbRed = d.pal[1].rgbGreen = d.pal[1].rgbBlue = 255;
    for (int i = 0; i < 8; i++) d.bits[i * 4] = (i & 1) ? 0xAA : 0x55;
    static const UINT usage[] = { DIB_RGB_COLORS, DIB_PAL_COLORS };
    for (size_t i = 0; i < 2; i++) {
        HBRUSH b = U(CreateDIBPatternBrushPt)(&d, usage[i]);
        t_note("1bpp DIB brush usage %u -> %p", usage[i], (void *)b);
        if (b) { LOGBRUSH lb; CHECK(getlb(b, &lb) == 0x10); pb_del(b); }
    }
}
TEST(brushes_nt_dib_brush_direct) {
    REQUIRE(UN(NtGdiCreateDIBBrush)); PB_GDI();
    struct { BITMAPINFOHEADER h; DWORD px[64]; } d;
    memset(&d, 0, sizeof d);
    d.h.biSize = sizeof d.h; d.h.biWidth = 8; d.h.biHeight = 8; d.h.biPlanes = 1; d.h.biBitCount = 32;
    HANDLE b = UN(NtGdiCreateDIBBrush)(&d, DIB_RGB_COLORS, sizeof d, FALSE, FALSE, &d);
    if (!b) { t_note("NtGdiCreateDIBBrush(cj=%zu) returned NULL err %lu", sizeof d, GetLastError()); return; }
    LOGBRUSH lb; CHECK(getlb(b, &lb) == 0x10);
    CHECK(lb.lbStyle == BS_DIBPATTERN || lb.lbStyle == BS_PATTERN);
    CHECK(PB_T7(b) == T7_BRUSH);
    pb_del(b);
}
TEST(brushes_indirect_styles) {
    PB_GDI(); REQUIRE(U(CreateBrushIndirect));
    LOGBRUSH lb, out;
    lb.lbStyle = BS_SOLID; lb.lbColor = RGB(1, 2, 3); lb.lbHatch = 0x55;
    HBRUSH b = U(CreateBrushIndirect)(&lb);
    REQUIRE(b);
    CHECK(getlb(b, &out) == 0x10 && out.lbStyle == BS_SOLID && out.lbColor == lb.lbColor && out.lbHatch == 0);
    pb_del(b);
    lb.lbStyle = BS_HATCHED; lb.lbHatch = HS_VERTICAL;
    b = U(CreateBrushIndirect)(&lb);
    REQUIRE(b);
    CHECK(getlb(b, &out) == 0x10 && out.lbStyle == BS_HATCHED && out.lbHatch == HS_VERTICAL && out.lbColor == lb.lbColor);
    pb_del(b);
}
TEST(brushes_indirect_hollow_is_stock_null) {
    PB_GDI(); REQUIRE(U(CreateBrushIndirect));
    LOGBRUSH lb = { BS_HOLLOW, 0, 0 }, out;
    /* gdi32full!CreateBrush(1) returns GetStockObject(NULL_BRUSH) */
    HBRUSH b = U(CreateBrushIndirect)(&lb);
    CHECK(b == (HBRUSH)U(GetStockObject)(NULL_BRUSH));
    CHECK(b != NULL && PB_STOCKED(b));
    CHECK(getlb(b, &out) == 0x10 && out.lbStyle == BS_NULL && out.lbHatch == 0);
}
TEST(brushes_indirect_invalid_styles) {
    PB_GDI(); REQUIRE(U(CreateBrushIndirect));
    static const UINT bad[] = { 4, 9, 10, 100, 0xFFFF, 0xFFFFFFFF };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        LOGBRUSH lb = { bad[i], 0, 0 };
        HBRUSH b = U(CreateBrushIndirect)(&lb);
        CHECKF(b == NULL, "style %u", bad[i]); /* CreateBrushIndirect/CreateBrush return NULL for styles 4 and >= 9 */
        if (b) pb_del(b);
    }
}
TEST(brushes_getobject_protocol) {
    PB_GDI(); REQUIRE(U(CreateSolidBrush));
    HBRUSH b = U(CreateSolidBrush)(RGB(1, 2, 3));
    REQUIRE(b);
    unsigned char buf[0x40];
    CHECK(pb_getobj(b, 0, NULL) == 0x10);
    CHECK(pb_getobj(b, 100, NULL) == 0x10);
    memset(buf, 0xAB, sizeof buf);
    CHECK(pb_getobj(b, 0x40, buf) == 0x10);
    CHECK(buf[0x10] == 0xAB);
    /* NtGdiExtGetObjectW forces the Gre worker's buffer size to 0x10 for brush handles and then copies only
     * min(result, caller count) bytes, reporting the full 0x10 */
    memset(buf, 0xAB, sizeof buf);
    CHECK(pb_getobj(b, 4, buf) == 0x10);
    CHECK(buf[4] == 0xAB && buf[8] == 0xAB);
    CHECK(*(DWORD *)buf == BS_SOLID);
    memset(buf, 0xAB, sizeof buf);
    CHECK(pb_getobj(b, 8, buf) == 0x10);
    CHECK(*(DWORD *)(buf + 4) == RGB(1, 2, 3) && buf[8] == 0xAB);
    pb_del(b);
}
TEST(brushes_nt_getobject_direct) {
    REQUIRE(UN(NtGdiExtGetObjectW)); PB_GDI(); REQUIRE(U(CreateHatchBrush));
    HBRUSH b = U(CreateHatchBrush)(HS_BDIAGONAL, RGB(9, 8, 7));
    REQUIRE(b);
    LOGBRUSH lb;
    CHECK(UN(NtGdiExtGetObjectW)(b, sizeof lb, &lb) == 0x10);
    CHECK(lb.lbStyle == BS_HATCHED && lb.lbHatch == HS_BDIAGONAL && lb.lbColor == RGB(9, 8, 7));
    CHECK(UN(NtGdiExtGetObjectW)(b, 0, NULL) == 0x10);
    CHECK(UN(NtGdiExtGetObjectW)(NULL, sizeof lb, &lb) == 0);
    pb_del(b);
}
TEST(brushes_getobject_on_deleted) {
    PB_GDI(); REQUIRE(U(CreateSolidBrush));
    HBRUSH b = U(CreateSolidBrush)(RGB(1, 2, 3));
    REQUIRE(b);
    CHECK(pb_del(b));
    LOGBRUSH lb;
    SetLastError(0);
    CHECK(getlb(b, &lb) == 0);
    CHECK(!pb_del(b));
    CHECK(U(GetObjectType)(b) == 0);
}
TEST(brushes_stock_brushes) {
    PB_GDI();
    static const struct { int id; COLORREF c; UINT style; } v[] = {
        { WHITE_BRUSH, RGB(255, 255, 255), BS_SOLID }, { LTGRAY_BRUSH, RGB(192, 192, 192), BS_SOLID },
        { GRAY_BRUSH, RGB(128, 128, 128), BS_SOLID }, { DKGRAY_BRUSH, RGB(64, 64, 64), BS_SOLID },
        { BLACK_BRUSH, 0, BS_SOLID }, { NULL_BRUSH, 0, BS_NULL },
    };
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        HGDIOBJ h = U(GetStockObject)(v[i].id);
        CHECKF(h != NULL, "stock %d", v[i].id);
        if (!h) continue;
        CHECK(h == U(GetStockObject)(v[i].id));
        CHECK(PB_STOCKED(h));
        CHECK(PB_T7(h) == (T7_BRUSH | 0x00)); /* type7 masks the stock bit away: bits 16..22 */
        CHECK(U(GetObjectType)(h) == OBJ_BRUSH);
        LOGBRUSH lb;
        CHECK(getlb(h, &lb) == 0x10);
        CHECKF(lb.lbStyle == v[i].style, "stock %d style %u", v[i].id, lb.lbStyle);
        if (v[i].style == BS_SOLID) CHECKF(lb.lbColor == v[i].c, "stock %d color 0x%lx", v[i].id, lb.lbColor);
    }
}
TEST(brushes_stock_brush_handles_distinct) {
    PB_GDI();
    HGDIOBJ h[6];
    for (int i = 0; i < 6; i++) h[i] = U(GetStockObject)(i);
    for (int i = 0; i < 6; i++) for (int j = 0; j < i; j++) CHECK(h[i] != h[j]);
}
TEST(brushes_stock_cell_state) {
    PB_GDI(); PB_TABLE();
    HGDIOBJ h = U(GetStockObject)(WHITE_BRUSH);
    REQUIRE(h);
    struct pb_cell c;
    CHECK(pb_cell_of(h, &c));
    CHECK(c.objt == 0x10);
    t_note("stock WHITE_BRUSH 0x%llx: owner 0x%x flags 0x%x pUser 0x%llx", (unsigned long long)(ULONG_PTR)h, c.owner, c.flags, c.puser);
}
TEST(brushes_stock_delete_keeps_object) {
    PB_GDI();
    HGDIOBJ h = U(GetStockObject)(BLACK_BRUSH);
    REQUIRE(h);
    SetLastError(0);
    BOOL r = pb_del(h);
    t_note("DeleteObject(stock BLACK_BRUSH) = %d", r);
    LOGBRUSH lb;
    CHECK(getlb(h, &lb) == 0x10 && lb.lbColor == 0);
    CHECK(U(GetStockObject)(BLACK_BRUSH) == h);
}
TEST(brushes_dc_brush_defaults_and_set) {
    PB_GDI(); REQUIRE(U(SetDCBrushColor)); REQUIRE(U(GetDCBrushColor));
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    CHECK(U(GetDCBrushColor)(dc) == RGB(255, 255, 255)); /* documented default of DC_BRUSH */
    CHECK(U(SetDCBrushColor)(dc, RGB(1, 2, 3)) == RGB(255, 255, 255));
    CHECK(U(GetDCBrushColor)(dc) == RGB(1, 2, 3));
    struct pb_cell c; DWORD v = 0;
    if (pb_connect() && pb_cell_of(dc, &c) && c.puser && k_try_read_u32((char *)(ULONG_PTR)c.puser + 0xc4, &v))
        CHECKF(v == RGB(1, 2, 3), "DC_ATTR+0xc4 = 0x%lx", v); /* GetDCBrushColor */
    U(DeleteDC)(dc);
}
TEST(brushes_dc_brush_invalid_dc) {
    PB_GDI(); REQUIRE(U(GetDCBrushColor)); REQUIRE(U(SetDCBrushColor));
    SetLastError(0);
    CHECK(U(GetDCBrushColor)(NULL) == CLR_INVALID);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SetLastError(0);
    CHECK(U(SetDCBrushColor)(NULL, 0) == CLR_INVALID);
    HBRUSH b = U(CreateSolidBrush)(0);
    REQUIRE(b);
    SetLastError(0);
    CHECK(U(GetDCBrushColor)((HDC)b) == CLR_INVALID);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    pb_del(b);
}
TEST(brushes_dc_brush_selected_tracks_color) {
    PB_GDI(); REQUIRE(U(SelectObject));
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    HGDIOBJ dcb = U(GetStockObject)(DC_BRUSH);
    REQUIRE(dcb);
    HGDIOBJ old = U(SelectObject)(dc, dcb);
    CHECK(old == U(GetStockObject)(WHITE_BRUSH));
    CHECK(U(GetCurrentObject)(dc, OBJ_BRUSH) == dcb);
    U(SetDCBrushColor)(dc, RGB(10, 20, 30));
    CHECK(U(GetDCBrushColor)(dc) == RGB(10, 20, 30));
    CHECK(U(GetCurrentObject)(dc, OBJ_BRUSH) == dcb);
    U(SelectObject)(dc, old);
    U(DeleteDC)(dc);
}
TEST(brushes_select_returns_previous_and_current) {
    PB_GDI(); REQUIRE(U(SelectObject)); REQUIRE(U(CreateSolidBrush));
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    HBRUSH a = U(CreateSolidBrush)(RGB(1, 2, 3)), b = U(CreateSolidBrush)(RGB(4, 5, 6));
    if (!a || !b) { U(DeleteDC)(dc); REQUIRE(a); REQUIRE(b); }
    HGDIOBJ def = U(GetCurrentObject)(dc, OBJ_BRUSH);
    CHECK(def == U(GetStockObject)(WHITE_BRUSH));
    CHECK(U(SelectObject)(dc, a) == def);
    CHECK(U(SelectObject)(dc, b) == a);
    CHECK(U(GetCurrentObject)(dc, OBJ_BRUSH) == b);
    CHECK(U(SelectObject)(dc, def) == b);
    pb_del(a); pb_del(b);
    U(DeleteDC)(dc);
}
TEST(brushes_nt_solid_reset_selected_brush_note) {
    REQUIRE(UN(NtGdiCreateSolidBrush)); PB_GDI(); REQUIRE(U(SelectObject));
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    HANDLE a = UN(NtGdiCreateSolidBrush)(RGB(1, 2, 3), NULL);
    if (!a) { U(DeleteDC)(dc); REQUIRE(a); }
    HGDIOBJ old = U(SelectObject)(dc, a);
    HANDLE r = UN(NtGdiCreateSolidBrush)(RGB(9, 9, 9), a);
    t_note("reset of a brush selected into a DC -> %p", r);
    LOGBRUSH lb; getlb(a, &lb);
    t_note("color after reset attempt 0x%lx", lb.lbColor);
    U(SelectObject)(dc, old);
    pb_del(a);
    U(DeleteDC)(dc);
}
TEST(brushes_org_set_get_userland) {
    PB_GDI(); REQUIRE(U(SetBrushOrgEx)); REQUIRE(U(GetBrushOrgEx));
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    POINT pt = { 99, 99 };
    CHECK(U(GetBrushOrgEx)(dc, &pt)); CHECK(pt.x == 0 && pt.y == 0);
    CHECK(U(SetBrushOrgEx)(dc, 5, 7, &pt)); CHECK(pt.x == 0 && pt.y == 0);
    CHECK(U(GetBrushOrgEx)(dc, &pt)); CHECK(pt.x == 5 && pt.y == 7);
    CHECK(U(SetBrushOrgEx)(dc, -3, 100000, &pt)); CHECK(pt.x == 5 && pt.y == 7);
    CHECK(U(GetBrushOrgEx)(dc, &pt)); CHECK(pt.x == -3 && pt.y == 100000);
    CHECK(U(SetBrushOrgEx)(dc, 1, 2, NULL));
    CHECK(U(GetBrushOrgEx)(dc, &pt)); CHECK(pt.x == 1 && pt.y == 2);
    U(DeleteDC)(dc);
}
TEST(brushes_org_dc_attr_fields) {
    PB_GDI(); PB_TABLE();
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    struct pb_cell c;
    CHECK(pb_cell_of(dc, &c));
    CHECK(c.puser != 0);
    if (!c.puser) { U(DeleteDC)(dc); return; }
    char *attr = (char *)(ULONG_PTR)c.puser;
    U(SetBrushOrgEx)(dc, 11, 22, NULL);
    /* gdi32full!SetBrushOrgEx and GetBrushOrgEx read/write DC_ATTR+0x158 (x) and +0x15c (y) directly */
    LONG x = 0, y = 0;
    CHECK(k_try_read(attr + 0x158, &x, 4) && k_try_read(attr + 0x15c, &y, 4));
    CHECKF(x == 11 && y == 22, "DC_ATTR org (%ld,%ld)", x, y);
    U(DeleteDC)(dc);
}
TEST(brushes_org_kernel_sees_batched_value) {
    PB_GDI(); REQUIRE(UN(NtGdiSetBrushOrg)); REQUIRE(U(GdiFlush));
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    U(SetBrushOrgEx)(dc, 33, 44, NULL);   /* may be batched in the TEB */
    U(GdiFlush)();
    POINT old = { -1, -1 };
    /* SetBrushOrgEx returns the previous origin through the 4th argument */
    CHECK(UN(NtGdiSetBrushOrg)(dc, 55, 66, &old));
    CHECKF(old.x == 33 && old.y == 44, "kernel old org (%ld,%ld)", old.x, old.y);
    POINT now;
    CHECK(U(GetBrushOrgEx)(dc, &now));
    CHECKF(now.x == 55 && now.y == 66, "client org (%ld,%ld)", now.x, now.y); /* kernel also writes DC_ATTR+0x158/0x15c */
    U(DeleteDC)(dc);
}
TEST(brushes_org_invalid_dc) {
    PB_GDI(); REQUIRE(U(SetBrushOrgEx)); REQUIRE(U(GetBrushOrgEx)); REQUIRE(UN(NtGdiSetBrushOrg));
    POINT pt;
    SetLastError(0);
    CHECK(!U(SetBrushOrgEx)(NULL, 1, 2, &pt));
    EXPECT_ERR(ERROR_INVALID_HANDLE); /* pldcGet() == 0 -> GdiSetLastError(6) */
    SetLastError(0);
    CHECK(!U(GetBrushOrgEx)(NULL, &pt));
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SetLastError(0);
    CHECK(!UN(NtGdiSetBrushOrg)(NULL, 1, 2, &pt));
    EXPECT_ERR(ERROR_INVALID_HANDLE); /* SetBrushOrgEx: DC lock fails -> EngSetLastError(6) */
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    SetLastError(0);
    if (!t_is_wine()) { /* an emulator that writes through lppt unchecked would fault */
        CHECK(!U(GetBrushOrgEx)(dc, NULL)); /* lppt == NULL fails the GetBrushOrgEx validation chain */
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
    U(DeleteDC)(dc);
}
TEST(brushes_org_wrong_object_type) {
    PB_GDI(); REQUIRE(U(GetBrushOrgEx));
    HBRUSH b = U(CreateSolidBrush)(0);
    REQUIRE(b);
    POINT pt;
    SetLastError(0);
    CHECK(!U(GetBrushOrgEx)((HDC)b, &pt)); /* cell Objt must be 1 (DC) */
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    pb_del(b);
}
TEST(brushes_org_independent_per_dc) {
    PB_GDI(); REQUIRE(U(SetBrushOrgEx));
    HDC a = U(CreateCompatibleDC)(NULL), b = U(CreateCompatibleDC)(NULL);
    if (!a || !b) { if (a) U(DeleteDC)(a); if (b) U(DeleteDC)(b); REQUIRE(a); REQUIRE(b); }
    U(SetBrushOrgEx)(a, 3, 4, NULL);
    POINT pa, pb2;
    U(GetBrushOrgEx)(a, &pa); U(GetBrushOrgEx)(b, &pb2);
    CHECK(pa.x == 3 && pa.y == 4);
    CHECK(pb2.x == 0 && pb2.y == 0);
    U(DeleteDC)(a); U(DeleteDC)(b);
}
TEST(brushes_unrealize_brush_is_noop_true) {
    PB_GDI(); REQUIRE(U(UnrealizeObject)); REQUIRE(U(CreateSolidBrush));
    HBRUSH b = U(CreateSolidBrush)(RGB(1, 2, 3));
    REQUIRE(b);
    CHECK(U(UnrealizeObject)(b)); /* gdi32full!UnrealizeObject: type 0x100000 returns 1 without a syscall */
    CHECK(U(UnrealizeObject)(U(GetStockObject)(WHITE_BRUSH)));
    pb_del(b);
}
TEST(brushes_unrealize_non_palette_fails) {
    PB_GDI(); REQUIRE(U(UnrealizeObject)); REQUIRE(UN(NtGdiUnrealizeObject));
    HGDIOBJ pen = U(CreatePen)(PS_SOLID, 3, 0);
    HGDIOBJ rgn = U(CreateRectRgn) ? U(CreateRectRgn)(0, 0, 3, 3) : NULL;
    /* UnrealizeObject only accepts a palette */
    if (pen) CHECK(!U(UnrealizeObject)(pen));
    if (rgn) CHECK(!U(UnrealizeObject)(rgn));
    CHECK(!U(UnrealizeObject)(NULL));
    CHECK(!UN(NtGdiUnrealizeObject)((HANDLE)(ULONG_PTR)0x1234));
    pb_del(pen); pb_del(rgn);
}
TEST(brushes_gdi_count_delta) {
    PB_GDI(); REQUIRE(U(GetGuiResources)); REQUIRE(U(CreateHatchBrush));
    enum { N = 30 };
    HBRUSH h[N];
    DWORD g0 = U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS);
    for (int i = 0; i < N; i++) { h[i] = U(CreateHatchBrush)(i % 6, i); REQUIRE(h[i]); }
    CHECK(U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS) == g0 + N);
    for (int i = 0; i < N; i++) pb_del(h[i]);
    CHECK(U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS) == g0);
}
TEST(brushes_solid_many_distinct_handles) {
    PB_GDI(); REQUIRE(U(CreateSolidBrush));
    enum { N = 100 };
    HBRUSH h[N];
    for (int i = 0; i < N; i++) { h[i] = U(CreateSolidBrush)(RGB(i, 255 - i, i / 2)); REQUIRE(h[i]); }
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < i; j++) CHECK(h[i] != h[j]);
        LOGBRUSH lb; CHECK(getlb(h[i], &lb) == 0x10 && lb.lbColor == RGB(i, 255 - i, i / 2));
    }
    for (int i = 0; i < N; i++) pb_del(h[i]);
}
