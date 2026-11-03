/* PolyTextOutW / NtGdiPolyTextOutW: a POLYTEXTW array draws exactly the pixels the same sequence of ExtTextOutW calls draws
 * (checked on 1 bpp bitmaps for plain, opaque, clipped, explicit spacing, alignment and current-position modes), count 0
 * succeeds without touching the array, and malformed arrays (NULL array, bad string pointer, negative or huge lengths,
 * unreadable spacing array, wrong handle) return FALSE. Nothing is drawn on a screen DC; every surface is a private bitmap. */
#include "xf.h"

#define W 120
#define H 60
#define STRIDE 16
struct surf { HDC dc; HBITMAP bm, old; HFONT f, oldf; };
static int surf_open(struct surf *s, int cs, int height, int esc) {
    memset(s, 0, sizeof *s);
    s->dc = U(CreateCompatibleDC)(NULL);
    s->bm = U(CreateBitmap)(W, H, 1, 1, NULL);
    if (!s->dc || !s->bm) return 0;
    s->old = (HBITMAP)U(SelectObject)(s->dc, s->bm);
    s->f = U(CreateFontW)(height, 0, esc, esc, 400, 0, 0, 0, cs, 0, 0, 0, 0, L"Arial");
    s->oldf = (HFONT)U(SelectObject)(s->dc, s->f);
    U(PatBlt)(s->dc, 0, 0, W, H, WHITENESS);
    U(SetBkMode)(s->dc, OPAQUE);
    U(SetTextColor)(s->dc, 0); U(SetBkColor)(s->dc, 0xFFFFFF);
    return 1;
}
static void surf_close(struct surf *s) {
    if (s->oldf) U(SelectObject)(s->dc, s->oldf);
    if (s->old) U(SelectObject)(s->dc, s->old);
    if (s->f) U(DeleteObject)(s->f);
    if (s->bm) U(DeleteObject)(s->bm);
    if (s->dc) U(DeleteDC)(s->dc);
}
static void surf_bits(struct surf *s, unsigned char *out) {
    struct { BITMAPINFOHEADER h; RGBQUAD c[2]; } bi; memset(&bi, 0, sizeof bi);
    bi.h.biSize = sizeof bi.h; bi.h.biWidth = W; bi.h.biHeight = H; bi.h.biPlanes = 1; bi.h.biBitCount = 1;
    memset(out, 0, STRIDE * H);
    U(SelectObject)(s->dc, s->old);
    U(GetDIBits)(s->dc, s->bm, 0, H, out, (BITMAPINFO *)&bi, DIB_RGB_COLORS);
    U(SelectObject)(s->dc, s->bm);
}
static int count_set(const unsigned char *b) { int n = 0; for (int i = 0; i < STRIDE * H; i++) if ((i % STRIDE) < 15 && b[i] != 0xff) n++; return n; }
static void ext_all(HDC dc, POLYTEXTW *p, UINT n) {
    for (UINT i = 0; i < n; i++)
        U(ExtTextOutW)(dc, p[i].x, p[i].y, p[i].uiFlags, (p[i].uiFlags & (ETO_OPAQUE | ETO_CLIPPED)) ? &p[i].rcl : NULL, p[i].lpstr, p[i].n, p[i].pdx);
}
/* Renders with PolyTextOutW (route 0), NtGdiPolyTextOutW (route 1) and ExtTextOutW; compares. */
static void compare(POLYTEXTW *p, UINT n, int cs, int height, int esc, int align, int expect_ink) {
    XF_GUI(); XF_NEED(U(PolyTextOutW)); XF_NEED(XF_NT(NtGdiPolyTextOutW));
    struct surf a, b, c;
    if (!surf_open(&a, cs, height, esc) || !surf_open(&b, cs, height, esc) || !surf_open(&c, cs, height, esc)) { t_skip("cannot create surfaces"); return; }
    U(SetTextAlign)(a.dc, align); U(SetTextAlign)(b.dc, align); U(SetTextAlign)(c.dc, align);
    if (align & TA_UPDATECP) { U(MoveToEx)(a.dc, 5, 9, NULL); U(MoveToEx)(b.dc, 5, 9, NULL); U(MoveToEx)(c.dc, 5, 9, NULL); }
    BOOL r0 = U(PolyTextOutW)(a.dc, p, n);
    BOOL r1 = XF_NT(NtGdiPolyTextOutW)(b.dc, p, n, 0);
    ext_all(c.dc, p, n);
    CHECK(r0 == TRUE); CHECK(r1 == TRUE);
    unsigned char ba[STRIDE * H], bb[STRIDE * H], bc[STRIDE * H];
    surf_bits(&a, ba); surf_bits(&b, bb); surf_bits(&c, bc);
    CHECK(!memcmp(ba, bc, sizeof ba));
    CHECK(!memcmp(bb, bc, sizeof bb));
    if (expect_ink) CHECK(count_set(bc) > 0); else CHECK(count_set(bc) == 0);
    if (align & TA_UPDATECP) {
        POINT pa, pb, pc; U(GetCurrentPositionEx)(a.dc, &pa); U(GetCurrentPositionEx)(b.dc, &pb); U(GetCurrentPositionEx)(c.dc, &pc);
        CHECK(pa.x == pc.x && pa.y == pc.y); CHECK(pb.x == pc.x && pb.y == pc.y);
        CHECK(pc.x > 5);
    }
    surf_close(&a); surf_close(&b); surf_close(&c);
}
static POLYTEXTW mk(int x, int y, UINT n, const WCHAR *s, UINT fl) { POLYTEXTW p; memset(&p, 0, sizeof p); p.x = x; p.y = y; p.n = (int)n; p.lpstr = s; p.uiFlags = fl; return p; }

XT(poly_equals_exttextout_plain) { POLYTEXTW p[2] = { mk(2, 2, 5, L"Hello", 0), mk(2, 30, 5, L"World", 0) }; compare(p, 2, 0, -14, 0, TA_TOP | TA_LEFT, 1); }
XT(poly_equals_exttextout_single) { POLYTEXTW p[1] = { mk(3, 5, 3, L"abc", 0) }; compare(p, 1, 0, -16, 0, TA_TOP | TA_LEFT, 1); }
XT(poly_equals_exttextout_opaque) {
    POLYTEXTW p[1] = { mk(4, 4, 4, L"Text", ETO_OPAQUE) }; p[0].rcl.left = 0; p[0].rcl.top = 0; p[0].rcl.right = 60; p[0].rcl.bottom = 25;
    compare(p, 1, 0, -14, 0, TA_TOP | TA_LEFT, 1);
}
XT(poly_equals_exttextout_clipped) {
    POLYTEXTW p[1] = { mk(4, 4, 10, L"ClippedTxt", ETO_CLIPPED) }; p[0].rcl.left = 0; p[0].rcl.top = 0; p[0].rcl.right = 30; p[0].rcl.bottom = 14;
    compare(p, 1, 0, -14, 0, TA_TOP | TA_LEFT, 1);
}
XT(poly_equals_exttextout_opaque_clipped) {
    POLYTEXTW p[2] = { mk(4, 4, 10, L"ClippedTxt", ETO_OPAQUE | ETO_CLIPPED), mk(4, 34, 4, L"Abcd", ETO_CLIPPED) };
    p[0].rcl.left = 2; p[0].rcl.top = 2; p[0].rcl.right = 50; p[0].rcl.bottom = 22; p[1].rcl.left = 0; p[1].rcl.top = 30; p[1].rcl.right = 20; p[1].rcl.bottom = 50;
    compare(p, 2, 0, -14, 0, TA_TOP | TA_LEFT, 1);
}
XT(poly_equals_exttextout_explicit_spacing) {
    static int dx[5] = { 12, 12, 12, 12, 12 };
    POLYTEXTW p[1] = { mk(2, 2, 5, L"Space", 0) }; p[0].pdx = dx;
    compare(p, 1, 0, -14, 0, TA_TOP | TA_LEFT, 1);
}
XT(poly_equals_exttextout_pdy) {
    static int dx[10] = { 12, 2, 12, 2, 12, 2, 12, 2, 12, 2 };
    POLYTEXTW p[1] = { mk(2, 20, 5, L"Space", ETO_PDY) }; p[0].pdx = dx;
    compare(p, 1, 0, -14, 0, TA_TOP | TA_LEFT, 1);
}
XT(poly_equals_exttextout_align_right_bottom) { POLYTEXTW p[2] = { mk(100, 25, 5, L"Right", 0), mk(100, 55, 5, L"Under", 0) }; compare(p, 2, 0, -14, 0, TA_RIGHT | TA_BOTTOM, 1); }
XT(poly_equals_exttextout_align_center_baseline) { POLYTEXTW p[1] = { mk(60, 30, 6, L"Center", 0) }; compare(p, 1, 0, -14, 0, TA_CENTER | TA_BASELINE, 1); }
XT(poly_equals_exttextout_update_cp) { POLYTEXTW p[2] = { mk(0, 0, 5, L"First", 0), mk(0, 0, 3, L"Two", 0) }; compare(p, 2, 0, -12, 0, TA_UPDATECP | TA_LEFT | TA_TOP, 1); }
XT(poly_equals_exttextout_rotated_font) { POLYTEXTW p[1] = { mk(10, 55, 5, L"Slant", 0) }; compare(p, 1, 0, -14, 450, TA_TOP | TA_LEFT, 1); }
XT(poly_equals_exttextout_greek_charset) { POLYTEXTW p[1] = { mk(2, 2, 5, L"\x03b1\x03b2\x03b3\x03b4\x03b5", 0) }; compare(p, 1, 161, -16, 0, TA_TOP | TA_LEFT, 1); }
XT(poly_equals_exttextout_cyrillic_charset) { POLYTEXTW p[1] = { mk(2, 2, 4, L"\x0430\x0431\x0432\x0433", 0) }; compare(p, 1, 204, -16, 0, TA_TOP | TA_LEFT, 1); }
XT(poly_equals_exttextout_empty_string_in_middle) {
    POLYTEXTW p[3] = { mk(2, 2, 3, L"One", 0), mk(2, 20, 0, L"skip", 0), mk(2, 38, 3, L"Two", 0) };
    compare(p, 3, 0, -14, 0, TA_TOP | TA_LEFT, 1);
}
XT(poly_empty_string_draws_nothing) { POLYTEXTW p[1] = { mk(2, 2, 0, L"x", 0) }; compare(p, 1, 0, -14, 0, TA_TOP | TA_LEFT, 0); }
XT(poly_empty_string_opaque_fills_rect) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiPolyTextOutW));
    struct surf s; if (!surf_open(&s, 0, -14, 0)) { t_skip("cannot create surface"); return; }
    U(SetBkColor)(s.dc, 0);
    POLYTEXTW p = mk(2, 2, 0, L"", ETO_OPAQUE); p.rcl.left = 10; p.rcl.top = 10; p.rcl.right = 30; p.rcl.bottom = 20;
    CHECK(XF_NT(NtGdiPolyTextOutW)(s.dc, &p, 1, 0) == TRUE);
    CHECK(U(GetPixel)(s.dc, 15, 15) == 0);
    CHECK(U(GetPixel)(s.dc, 5, 15) == 0xFFFFFF);
    CHECK(U(GetPixel)(s.dc, 30, 15) == 0xFFFFFF);          /* right edge exclusive */
    surf_close(&s);
}
XT(poly_transparent_mode_matches_exttextout) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiPolyTextOutW));
    struct surf a, c; if (!surf_open(&a, 0, -14, 0) || !surf_open(&c, 0, -14, 0)) { t_skip("cannot create surfaces"); return; }
    U(SetBkMode)(a.dc, TRANSPARENT); U(SetBkMode)(c.dc, TRANSPARENT);
    U(PatBlt)(a.dc, 0, 0, W, 30, BLACKNESS); U(PatBlt)(c.dc, 0, 0, W, 30, BLACKNESS);
    U(SetTextColor)(a.dc, 0xFFFFFF); U(SetTextColor)(c.dc, 0xFFFFFF);
    POLYTEXTW p = mk(2, 2, 6, L"Mixed!", 0);
    CHECK(XF_NT(NtGdiPolyTextOutW)(a.dc, &p, 1, 0) == TRUE);
    ext_all(c.dc, &p, 1);
    unsigned char ba[STRIDE * H], bc[STRIDE * H]; surf_bits(&a, ba); surf_bits(&c, bc);
    CHECK(!memcmp(ba, bc, sizeof ba));
    surf_close(&a); surf_close(&c);
}
XT(poly_does_not_touch_current_position_without_updatecp) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiPolyTextOutW));
    struct surf s; if (!surf_open(&s, 0, -14, 0)) { t_skip("cannot create surface"); return; }
    U(MoveToEx)(s.dc, 11, 13, NULL);
    POLYTEXTW p = mk(2, 2, 5, L"Hello", 0);
    CHECK(XF_NT(NtGdiPolyTextOutW)(s.dc, &p, 1, 0) == TRUE);
    POINT pt; U(GetCurrentPositionEx)(s.dc, &pt);
    CHECK(pt.x == 11 && pt.y == 13);
    surf_close(&s);
}
XT(poly_nt_flags_argument_ignored) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiPolyTextOutW));
    static const DWORD fl[] = { 0, 1, 2, 0xff, 0xffffffffu };
    unsigned char ref[STRIDE * H];
    for (unsigned i = 0; i < sizeof fl / sizeof *fl; i++) {
        struct surf s; if (!surf_open(&s, 0, -14, 0)) { t_skip("cannot create surface"); return; }
        POLYTEXTW p = mk(2, 2, 5, L"Hello", 0);
        CHECKF(XF_NT(NtGdiPolyTextOutW)(s.dc, &p, 1, fl[i]) == TRUE, "flags %u", i);
        unsigned char cur[STRIDE * H]; surf_bits(&s, cur);
        if (i == 0) memcpy(ref, cur, sizeof ref); else CHECKF(!memcmp(ref, cur, sizeof ref), "flags %u", i);
        surf_close(&s);
    }
}

/* ---- failure matrix (NtGdiPolyTextOutW) ---- */
XT(poly_count_zero_succeeds_anywhere) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiPolyTextOutW)); XH(x);
    CHECK(XF_NT(NtGdiPolyTextOutW)(x.mdc, NULL, 0, 0) == TRUE);
    POLYTEXTW p = mk(0, 0, 1, L"a", 0);
    for (int k = 0; k < H_COUNT; k++) CHECKF(XF_NT(NtGdiPolyTextOutW)((HDC)x.h[k], &p, 0, 0) == TRUE, "%s", xh_name[k]);
    CHECK(XF_NT(NtGdiPolyTextOutW)(NULL, NULL, 0, 0) == TRUE);
    xh_close(&x);
}
XT(poly_gdi32_count_zero_succeeds) {
    XF_GUI(); XF_NEED(U(PolyTextOutW));
    HDC dc = U(CreateCompatibleDC)(NULL);
    CHECK(U(PolyTextOutW)(dc, NULL, 0) == TRUE);
    POLYTEXTW p = mk(0, 0, 1, L"a", 0);
    CHECK(U(PolyTextOutW)(dc, &p, 0) == TRUE);
    U(DeleteDC)(dc);
}
XT(poly_null_array_with_count_fails) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiPolyTextOutW));
    HDC dc = U(CreateCompatibleDC)(NULL);
    CHECK(XF_NT(NtGdiPolyTextOutW)(dc, NULL, 1, 0) == FALSE);
    CHECK(XF_NT(NtGdiPolyTextOutW)(dc, NULL, 3, 0) == FALSE);
    U(DeleteDC)(dc);
}
XT(poly_huge_counts_fail) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiPolyTextOutW));
    HDC dc = U(CreateCompatibleDC)(NULL); POLYTEXTW p = mk(0, 0, 1, L"a", 0);
    static const UINT c[] = { 0x10000000u, 0x7fffffffu, 0x80000000u, 0xffffffffu };
    for (unsigned i = 0; i < 4; i++) CHECKF(XF_NT(NtGdiPolyTextOutW)(dc, &p, c[i], 0) == FALSE, "count 0x%x", c[i]);
    U(DeleteDC)(dc);
}
XT(poly_gdi32_huge_count_fails) {
    XF_GUI(); XF_NEED(U(PolyTextOutW));
    HDC dc = U(CreateCompatibleDC)(NULL); POLYTEXTW p = mk(0, 0, 1, L"a", 0);
    CHECK(U(PolyTextOutW)(dc, &p, 0xffffffffu) == FALSE);
    U(DeleteDC)(dc);
}
XT(poly_negative_length_fails) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiPolyTextOutW));
    HDC dc = U(CreateCompatibleDC)(NULL);
    static const int n[] = { -1, -2, -100, (int)0x80000000 };
    for (unsigned i = 0; i < 4; i++) { POLYTEXTW p = mk(0, 0, 1, L"abc", 0); p.n = n[i]; CHECKF(XF_NT(NtGdiPolyTextOutW)(dc, &p, 1, 0) == FALSE, "n %d", n[i]); }
    U(DeleteDC)(dc);
}
XT(poly_huge_length_fails) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiPolyTextOutW));
    HDC dc = U(CreateCompatibleDC)(NULL);
    static const int n[] = { 0x7fffffff, 0x10000000, 0x01000000 };
    for (unsigned i = 0; i < 3; i++) { POLYTEXTW p = mk(0, 0, 1, L"abc", 0); p.n = n[i]; CHECKF(XF_NT(NtGdiPolyTextOutW)(dc, &p, 1, 0) == FALSE, "n 0x%x", n[i]); }
    U(DeleteDC)(dc);
}
XT(poly_null_string_fails) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiPolyTextOutW));
    HDC dc = U(CreateCompatibleDC)(NULL);
    POLYTEXTW p = mk(0, 0, 3, NULL, 0);
    CHECK(XF_NT(NtGdiPolyTextOutW)(dc, &p, 1, 0) == FALSE);
    U(DeleteDC)(dc);
}
XT(poly_null_string_with_zero_length_succeeds) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiPolyTextOutW));
    HDC dc = U(CreateCompatibleDC)(NULL);
    POLYTEXTW p = mk(0, 0, 0, NULL, 0);
    CHECK(XF_NT(NtGdiPolyTextOutW)(dc, &p, 1, 0) == TRUE);
    U(DeleteDC)(dc);
}
XT(poly_kernel_string_pointer_fails) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiPolyTextOutW));
    HDC dc = U(CreateCompatibleDC)(NULL);
    POLYTEXTW p = mk(0, 0, 3, (const WCHAR *)0xFFFF800000000000ull, 0);
    CHECK(XF_NT(NtGdiPolyTextOutW)(dc, &p, 1, 0) == FALSE);
    p.lpstr = (const WCHAR *)(ULONG_PTR)0x10;
    CHECK(XF_NT(NtGdiPolyTextOutW)(dc, &p, 1, 0) == FALSE);
    U(DeleteDC)(dc);
}
XT(poly_unreadable_spacing_array_fails) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiPolyTextOutW));
    HDC dc = U(CreateCompatibleDC)(NULL);
    POLYTEXTW p = mk(0, 0, 2, L"ab", 0); p.pdx = (int *)(ULONG_PTR)0x10;
    CHECK(XF_NT(NtGdiPolyTextOutW)(dc, &p, 1, 0) == FALSE);
    U(DeleteDC)(dc);
}
XT(poly_second_entry_bad_fails) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiPolyTextOutW));
    HDC dc = U(CreateCompatibleDC)(NULL);
    POLYTEXTW p[2] = { mk(0, 0, 2, L"ab", 0), mk(0, 0, 2, NULL, 0) };
    CHECK(XF_NT(NtGdiPolyTextOutW)(dc, p, 2, 0) == FALSE);
    p[1] = mk(0, 0, -5, L"ab", 0);
    CHECK(XF_NT(NtGdiPolyTextOutW)(dc, p, 2, 0) == FALSE);
    U(DeleteDC)(dc);
}
XT(poly_invalid_flag_bits_fail) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiPolyTextOutW));
    HDC dc = U(CreateCompatibleDC)(NULL);
    POLYTEXTW p = mk(0, 0, 2, L"ab", 0x80000000u);
    CHECK(XF_NT(NtGdiPolyTextOutW)(dc, &p, 1, 0) == FALSE);
    U(DeleteDC)(dc);
}
XT(poly_inverted_rect_with_opaque_succeeds) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiPolyTextOutW));
    HDC dc = U(CreateCompatibleDC)(NULL);
    POLYTEXTW p = mk(0, 0, 2, L"ab", ETO_OPAQUE); p.rcl.left = 5; p.rcl.top = 5; p.rcl.right = 1; p.rcl.bottom = 1;
    CHECK(XF_NT(NtGdiPolyTextOutW)(dc, &p, 1, 0) == TRUE);
    U(DeleteDC)(dc);
}
XT(poly_glyph_index_flag_succeeds) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiPolyTextOutW));
    HDC dc = U(CreateCompatibleDC)(NULL);
    POLYTEXTW p = mk(0, 0, 2, L"\x0001\x0002", ETO_GLYPH_INDEX);
    CHECK(XF_NT(NtGdiPolyTextOutW)(dc, &p, 1, 0) == TRUE);
    U(DeleteDC)(dc);
}
static void poly_bad_dc(int k) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiPolyTextOutW)); XF_NEED(U(PolyTextOutW)); XH(x);
    POLYTEXTW p = mk(0, 0, 2, L"ab", 0);
    SetLastError(0); CHECKF(XF_NT(NtGdiPolyTextOutW)((HDC)x.h[k], &p, 1, 0) == FALSE, "%s", xh_name[k]); EXPECT_ERR(6);
    SetLastError(0); CHECKF(U(PolyTextOutW)((HDC)x.h[k], &p, 1) == FALSE, "%s", xh_name[k]); EXPECT_ERR(6);
    xh_close(&x);
}
#define PBD(nm, k) XT(poly_bad_dc_##nm) { poly_bad_dc(k); }
PBD(null, H_NULL) PBD(bitmap, H_BITMAP) PBD(region, H_REGION) PBD(junk, H_JUNK) PBD(hwnd, H_HWND) PBD(font, H_FONT) PBD(deleted_dc, H_DEADDC)

XT(poly_failure_leaves_dc_usable) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiPolyTextOutW));
    struct surf s; if (!surf_open(&s, 0, -14, 0)) { t_skip("cannot create surface"); return; }
    POLYTEXTW bad = mk(0, 0, 3, NULL, 0);
    CHECK(XF_NT(NtGdiPolyTextOutW)(s.dc, &bad, 1, 0) == FALSE);
    POLYTEXTW good = mk(2, 2, 5, L"Hello", 0);
    CHECK(XF_NT(NtGdiPolyTextOutW)(s.dc, &good, 1, 0) == TRUE);
    unsigned char b[STRIDE * H]; surf_bits(&s, b);
    CHECK(count_set(b) > 0);
    surf_close(&s);
}
XT(poly_charset_font_does_not_change_result_code) {
    XF_GUI(); XF_NEED(XF_NT(NtGdiPolyTextOutW));
    static const int cs[] = { 0, 2, 128, 161, 177, 204, 222, 238, 255 };
    for (unsigned i = 0; i < sizeof cs / sizeof *cs; i++) {
        struct surf s; if (!surf_open(&s, cs[i], -14, 0)) continue;
        POLYTEXTW p = mk(2, 2, 4, L"Test", 0);
        CHECKF(XF_NT(NtGdiPolyTextOutW)(s.dc, &p, 1, 0) == TRUE, "charset %d", cs[i]);
        surf_close(&s);
    }
}
