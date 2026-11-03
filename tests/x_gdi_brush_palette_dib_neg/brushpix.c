/* Consistency of DIB and pattern brushes: the bits painted by PatBlt follow the pattern with the brush origin applied
 * modulo the pattern size, SetBrushOrgEx round-trips through GetBrushOrgEx, a monochrome pattern takes its two colours
 * from the DC's text and background colours, the brush owns a copy of its pattern, and LOGBRUSH describes the result.
 * Painting goes to a 32bpp DIB section so the expected value of every pixel is exact. */
#include "xb.h"

UDECL(BOOL, NtGdiSetBrushOrg, (HDC hdc, LONG x, LONG y, POINT *pOld));

static const int depths[] = { 1, 4, 8, 24, 32 };
static const POINT origins[] = { {0,0}, {1,0}, {0,1}, {3,5}, {7,7}, {8,8}, {-1,-1}, {13,-9} };

static HBRUSH dibbrush(int bpp, BYTE *dib, size_t *len) {
    *len = xb_dib8(dib, bpp);
    return (HBRUSH)UN(NtGdiCreateDIBBrush)(dib, DIB_RGB_COLORS, (ULONG)*len, FALSE, FALSE, dib);
}
static int posmod8(int v) { return ((v % 8) + 8) % 8; }

static void pix_origin(int di, int oi) {
    REQUIRE(UN(NtGdiCreateDIBBrush)); REQUIRE(U(PatBlt)); REQUIRE(U(SetBrushOrgEx));
    int bpp = depths[di]; BYTE dib[40 + 1024 + 1024]; size_t len;
    HBRUSH b = dibbrush(bpp, dib, &len);
    struct xb_surf s;
    if (!b || !xb_surf_open(&s, 24, 24)) { CHECK(b != NULL); if (b) U(DeleteObject)(b); t_skip("fixture"); return; }
    HGDIOBJ ob = U(SelectObject)(s.dc, b);
    POINT o = origins[oi];
    CHECK(U(SetBrushOrgEx)(s.dc, o.x, o.y, NULL));
    CHECK(U(PatBlt)(s.dc, 0, 0, 24, 24, PATCOPY));
    int bad = 0, fx = -1, fy = -1;
    for (int y = 0; y < 24; y++) for (int x = 0; x < 24; x++) {
        BYTE r, g, bl; xb_pix(bpp, posmod8(x - o.x), posmod8(y - o.y), &r, &g, &bl);
        if (xb_px(&s, x, y) != (DWORD)((r << 16) | (g << 8) | bl)) { if (!bad) { fx = x; fy = y; } bad++; }
    }
    CHECKF(bad == 0, "%d bpp origin (%ld,%ld): %d of 576 pixels differ, first at (%d,%d)", bpp, o.x, o.y, bad, fx, fy);
    U(SelectObject)(s.dc, ob); xb_surf_close(&s); U(DeleteObject)(b);
}
#define PO(d, nm) XT(dibbrush_origin_pixels_##nm) { for (int o = 0; o < 8; o++) pix_origin(d, o); }
PO(0, 1bpp) PO(1, 4bpp) PO(2, 8bpp) PO(3, 24bpp) PO(4, 32bpp)

/* The origin is stored as given (not reduced) and SetBrushOrgEx returns the previous one. */
static void org_roundtrip(int oi, int nt) {
    REQUIRE(U(SetBrushOrgEx)); REQUIRE(U(GetBrushOrgEx)); REQUIRE(U(CreateCompatibleDC)); if (nt) REQUIRE(UN(NtGdiSetBrushOrg));
    HDC dc = U(CreateCompatibleDC)(NULL); POINT prev = { 99, 99 }, got = { 77, 77 };
    CHECK(U(GetBrushOrgEx)(dc, &got)); CHECKF(got.x == 0 && got.y == 0, "initial origin (%ld,%ld)", got.x, got.y);
    POINT o = origins[oi];
    BOOL r = nt ? UN(NtGdiSetBrushOrg)(dc, o.x, o.y, &prev) : U(SetBrushOrgEx)(dc, o.x, o.y, &prev);
    CHECK(r); CHECKF(prev.x == 0 && prev.y == 0, "previous (%ld,%ld)", prev.x, prev.y);
    CHECK(U(GetBrushOrgEx)(dc, &got));
    CHECKF(got.x == o.x && got.y == o.y, "readback (%ld,%ld) want (%ld,%ld)", got.x, got.y, o.x, o.y);
    POINT p2 = { 5, 5 }; CHECK(U(SetBrushOrgEx)(dc, 0, 0, &p2));
    CHECKF(p2.x == o.x && p2.y == o.y, "second previous (%ld,%ld)", p2.x, p2.y);
    U(DeleteDC)(dc);
}
XT(brush_org_roundtrip_gdi) { for (int o = 0; o < 8; o++) org_roundtrip(o, 0); }
XT(brush_org_roundtrip_nt) { for (int o = 0; o < 8; o++) org_roundtrip(o, 1); }

XT(brush_org_is_per_dc) {
    REQUIRE(U(SetBrushOrgEx)); REQUIRE(U(GetBrushOrgEx));
    HDC a = U(CreateCompatibleDC)(NULL), b = U(CreateCompatibleDC)(NULL); POINT p;
    U(SetBrushOrgEx)(a, 3, 4, NULL);
    U(GetBrushOrgEx)(b, &p); CHECKF(p.x == 0 && p.y == 0, "other DC origin (%ld,%ld)", p.x, p.y);
    U(GetBrushOrgEx)(a, &p); CHECKF(p.x == 3 && p.y == 4, "own origin (%ld,%ld)", p.x, p.y);
    U(DeleteDC)(a); U(DeleteDC)(b);
}
XT(brush_org_bad_dc) {
    REQUIRE(U(SetBrushOrgEx)); REQUIRE(UN(NtGdiSetBrushOrg));
    XHS(x);
    for (int k = XH_NULL; k <= XH_MINUS1; k++) {
        if (k == XH_DC) continue;
        POINT p = { 7, 7 };
        SetLastError(0); CHECKF(!U(SetBrushOrgEx)((HDC)x.h[k], 1, 2, &p), "%s", xh_nm[k]);
        CHECKF(p.x == 7 && p.y == 7, "%s: output touched on failure", xh_nm[k]);
        SetLastError(0); CHECKF(!UN(NtGdiSetBrushOrg)((HDC)x.h[k], 1, 2, &p), "%s nt", xh_nm[k]);
    }
    xhs_close(&x);
}

/* A monochrome pattern: 0 bits take the text colour, 1 bits the background colour. */
static void mono_colors(int nt, int b8) {
    REQUIRE(UN(NtGdiCreatePatternBrushInternal)); REQUIRE(U(CreateBitmap)); REQUIRE(U(CreatePatternBrush));
    static const BYTE rows[16] = { 0xAA,0, 0x55,0, 0xF0,0, 0x0F,0, 0xAA,0, 0x55,0, 0xFF,0, 0x00,0 };
    HBITMAP bm = U(CreateBitmap)(8, 8, 1, 1, rows);
    struct xb_surf s; if (!bm || !xb_surf_open(&s, 16, 16)) { t_skip("fixture"); return; }
    HBRUSH b = nt ? (HBRUSH)UN(NtGdiCreatePatternBrushInternal)(bm, b8, FALSE) : U(CreatePatternBrush)(bm);
    CHECK(b != NULL);
    if (b) {
        HGDIOBJ ob = U(SelectObject)(s.dc, b);
        U(SetTextColor)(s.dc, RGB(255, 0, 0)); U(SetBkColor)(s.dc, RGB(0, 0, 255));
        CHECK(U(PatBlt)(s.dc, 0, 0, 16, 16, PATCOPY));
        int bad = 0;
        for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
            int bit = (rows[(y & 7) * 2] >> (7 - (x & 7))) & 1;
            DWORD want = bit ? 0x0000FF : 0xFF0000;
            if (xb_px(&s, x, y) != want) bad++;
        }
        CHECKF(bad == 0, "%d of 256 pixels differ from the text/background colour mapping", bad);
        U(SelectObject)(s.dc, ob); U(DeleteObject)(b);
    }
    xb_surf_close(&s); U(DeleteObject)(bm);
}
XT(pattern_mono_colors_nt) { mono_colors(1, 0); }
XT(pattern_mono_colors_nt_8x8) { mono_colors(1, 1); }
XT(pattern_mono_colors_gdi) { mono_colors(0, 0); }

/* The brush owns a copy: deleting or overwriting the source leaves the pattern unchanged. */
static void owns_copy(int kind) {
    REQUIRE(UN(NtGdiCreatePatternBrushInternal));
    BYTE dib[40 + 1024 + 1024]; size_t len = xb_dib8(dib, kind);
    void *bits = NULL; BITMAPINFO *bi = (BITMAPINFO *)dib;
    HDC mdc = U(CreateCompatibleDC)(NULL);
    HBITMAP src = U(CreateDIBSection)(mdc, bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!src || !bits) { t_skip("fixture"); U(DeleteDC)(mdc); return; }
    memcpy(bits, dib + 40 + ((kind <= 8) ? (1u << kind) * 4 : 0), xb_stride(8, kind) * 8);
    HBRUSH b = (HBRUSH)UN(NtGdiCreatePatternBrushInternal)(src, FALSE, FALSE);
    CHECK(b != NULL);
    memset(bits, 0xEE, xb_stride(8, kind) * 8);              /* overwrite the source pixels */
    U(DeleteObject)(src); U(DeleteDC)(mdc);                   /* and then delete it */
    struct xb_surf s;
    if (b && xb_surf_open(&s, 8, 8)) {
        HGDIOBJ ob = U(SelectObject)(s.dc, b); U(SetBrushOrgEx)(s.dc, 0, 0, NULL);
        CHECK(U(PatBlt)(s.dc, 0, 0, 8, 8, PATCOPY));
        int bad = 0;
        for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {
            BYTE r, g, bl; xb_pix(kind, x, y, &r, &g, &bl);
            if (xb_px(&s, x, y) != (DWORD)((r << 16) | (g << 8) | bl)) bad++;
        }
        CHECKF(bad == 0, "%d bpp: %d of 64 pixels differ from the original pattern", kind, bad);
        U(SelectObject)(s.dc, ob); xb_surf_close(&s);
    }
    if (b) U(DeleteObject)(b);
}
#define OC(d) XT(pattern_owns_copy_##d) { owns_copy(d); }
OC(8) OC(24) OC(32) OC(4)

/* A 16x16 pattern: with the 8x8 flag clear the pattern period is 16, with it set only the 8x8 corner is used. */
static void big_pattern(int b8) {
    REQUIRE(UN(NtGdiCreateDIBBrush));
    BYTE dib[40 + 16 * 16 * 3 + 64]; memset(dib, 0, sizeof dib);
    BITMAPINFOHEADER *h = (BITMAPINFOHEADER *)dib; h->biSize = 40; h->biWidth = 16; h->biHeight = -16; h->biPlanes = 1; h->biBitCount = 24;
    BYTE *p = dib + 40;
    for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) { BYTE *q = p + y * 48 + x * 3; q[0] = (BYTE)(x * 16 + 1); q[1] = (BYTE)(y * 16 + 2); q[2] = (BYTE)(x + y); }
    HBRUSH br = (HBRUSH)UN(NtGdiCreateDIBBrush)(dib, DIB_RGB_COLORS, 40 + 768, b8, FALSE, dib);
    struct xb_surf s;
    if (!br || !xb_surf_open(&s, 32, 32)) { CHECK(br != NULL); if (br) U(DeleteObject)(br); t_skip("fixture"); return; }
    HGDIOBJ ob = U(SelectObject)(s.dc, br); U(SetBrushOrgEx)(s.dc, 0, 0, NULL);
    CHECK(U(PatBlt)(s.dc, 0, 0, 32, 32, PATCOPY));
    int period = b8 ? 8 : 16, bad = 0;
    for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) {
        int sx = x % period, sy = y % period;
        DWORD want = (DWORD)((sx + sy) << 16 | (sy * 16 + 2) << 8 | (sx * 16 + 1));
        if (xb_px(&s, x, y) != want) bad++;
    }
    CHECKF(bad == 0, "8x8 flag %d: %d of 1024 pixels differ from period %d", b8, bad, period);
    U(SelectObject)(s.dc, ob); xb_surf_close(&s); U(DeleteObject)(br);
}
XT(dibbrush_16x16_flag_clear_period_16) { big_pattern(0); }
XT(dibbrush_16x16_flag_set_period_8) { big_pattern(1); }

/* LOGBRUSH of the created brushes. */
XT(dibbrush_logbrush_style) {
    REQUIRE(UN(NtGdiCreateDIBBrush)); REQUIRE(U(GetObjectW));
    for (unsigned i = 0; i < 5; i++) {
        BYTE dib[40 + 1024 + 1024]; size_t len; HBRUSH b = dibbrush(depths[i], dib, &len);
        if (!b) { CHECKF(0, "%d bpp: no brush", depths[i]); continue; }
        LOGBRUSH lb; memset(&lb, 0xCC, sizeof lb);
        CHECK(U(GetObjectW)(b, sizeof lb, &lb) == sizeof lb);
        CHECKF(lb.lbStyle == BS_DIBPATTERN, "%d bpp: lbStyle %u", depths[i], lb.lbStyle);
        U(DeleteObject)(b);
    }
}
XT(pattern_logbrush_style) {
    REQUIRE(UN(NtGdiCreatePatternBrushInternal)); REQUIRE(U(GetObjectW));
    HBITMAP bm = U(CreateBitmap)(8, 8, 1, 1, NULL); REQUIRE(bm);
    HBRUSH b = (HBRUSH)UN(NtGdiCreatePatternBrushInternal)(bm, FALSE, FALSE); CHECK(b != NULL);
    if (b) { LOGBRUSH lb; memset(&lb, 0xCC, sizeof lb); CHECK(U(GetObjectW)(b, sizeof lb, &lb) == sizeof lb);
             CHECKF(lb.lbStyle == BS_PATTERN, "lbStyle %u", lb.lbStyle); U(DeleteObject)(b); }
    U(DeleteObject)(bm);
}
XT(pattern_brush_not_stock_and_deletable) {
    REQUIRE(UN(NtGdiCreatePatternBrushInternal));
    HBITMAP bm = U(CreateBitmap)(8, 8, 1, 1, NULL); REQUIRE(bm);
    HBRUSH b = (HBRUSH)UN(NtGdiCreatePatternBrushInternal)(bm, FALSE, FALSE); CHECK(b != NULL);
    if (b) {
        CHECK((((ULONG_PTR)b >> 16) & 0xFF) == 0x10);          /* brush type, not a stock object (bit 23 clear) */
        CHECK(U(DeleteObject)(b));
        SetLastError(0); CHECK(!U(DeleteObject)(b));
    }
    U(DeleteObject)(bm);
}
XT(pattern_pen_flag_brush_type) {
    REQUIRE(UN(NtGdiCreatePatternBrushInternal));
    HBITMAP bm = U(CreateBitmap)(8, 8, 1, 1, NULL); REQUIRE(bm);
    HBRUSH b = (HBRUSH)UN(NtGdiCreatePatternBrushInternal)(bm, FALSE, TRUE); CHECK(b != NULL);
    if (b) { CHECKF(((ULONG_PTR)b >> 16 & 0x7f) == 0x10 || ((ULONG_PTR)b >> 16 & 0x7f) == 0x30, "type7 %lx", (unsigned long)((ULONG_PTR)b >> 16 & 0x7f)); U(DeleteObject)(b); }
    U(DeleteObject)(bm);
}
