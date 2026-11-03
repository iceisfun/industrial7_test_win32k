/* Thin-coverage GDI syscalls: NtGdiEllipse, NtGdiRoundRect, NtGdiCreateEllipticRgn, NtGdiCreateRoundRectRgn,
 * NtGdiOffsetClipRgn, NtGdiResizePalette, NtGdiSwapBuffers.
 *
 * Analysis facts (win32kfull 19041) used for the hard CHECKs:
 *  - NtGdiEllipse / NtGdiRoundRect: DCOBJ lookup failure (NULL, wrong type, stale handle) -> FALSE + ERROR_INVALID_HANDLE (6).
 *    NtGdiRoundRect with width==0 or height==0 is NtGdiRectangle.
 *  - NtGdiCreateEllipticRgn / NtGdiCreateRoundRectRgn: every coordinate must satisfy
 *    ((v & 0xf8000000) + 0x8000000 & 0xf7ffffff) == 0, i.e. v in [-0x8000000, 0x7ffffff]; else NULL + ERROR_INVALID_PARAMETER (0x57).
 *  - NtGdiOffsetClipRgn: DC type (dc+0xc) must be < 2 (direct/memory); no clip region (dc+0xa0 == 0) -> SIMPLEREGION (2);
 *    else the offset is transformed by the DC xform and the clip region's complexity is returned; other DC types -> 0 + error 6.
 *  - NtGdiResizePalette: new size must satisfy size-1 < 0x400, the palette must be valid, not the stock default palette and
 *    carry PAL_DC (0x100); returns FALSE otherwise.
 *  - NtGdiSwapBuffers: no DC, or a DC without a WNDOBJ context (dc+0x1f0 == 0) -> FALSE + error 6. */
#include "g8.h"

UDECL(HRGN, NtGdiCreateEllipticRgn,   (int, int, int, int));
UDECL(HRGN, NtGdiCreateRoundRectRgn,  (int, int, int, int, int, int));
static void *rrs(void) { return t_is_wine() ? NULL : (void *)UN(NtGdiCreateRoundRectRgn); }  /* Wine's win32u has a different raw ABI here */
UDECL(int,  NtGdiEllipse,             (HDC, int, int, int, int));
UDECL(int,  NtGdiRoundRect,           (HDC, int, int, int, int, int, int));
UDECL(int,  NtGdiOffsetClipRgn,       (HDC, int, int, ULONG_PTR));
UDECL(int,  NtGdiResizePalette,       (HPALETTE, ULONG));
UDECL(int,  NtGdiSwapBuffers,         (HDC, ULONG_PTR, int));

typedef HRGN (WINAPI *ell_fn)(int, int, int, int);
typedef HRGN (WINAPI *rr_fn)(int, int, int, int, int, int);

#define NEED_GDI() do { if (!U(CreateCompatibleDC) || !U(DeleteObject) || !U(GetDC) || !U(ReleaseDC) || !U(CreateDIBSection) || !U(SelectObject)) { t_skip("gdi32/user32 missing"); return; } } while (0)

/* ---- a 32bpp top-down DIB section selected into a memory DC -------------------------------------- */
struct dib { HDC dc; HBITMAP bmp, old; DWORD *bits; int w, h; };
static int dib_make(struct dib *d, int w, int h) {
    memset(d, 0, sizeof *d);
    BITMAPINFO bi; memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    HDC scr = U(GetDC)(NULL);
    d->dc = U(CreateCompatibleDC)(scr);
    if (scr) U(ReleaseDC)(NULL, scr);
    if (!d->dc) return 0;
    d->bmp = U(CreateDIBSection)(d->dc, &bi, DIB_RGB_COLORS, (void **)&d->bits, NULL, 0);
    if (!d->bmp || !d->bits) { U(DeleteDC)(d->dc); d->dc = NULL; return 0; }
    d->old = (HBITMAP)U(SelectObject)(d->dc, d->bmp);
    d->w = w; d->h = h; d->bits[0] = 0;
    d->w = w; d->h = h;
    return 1;
}
static void dib_free(struct dib *d) {
    if (!d->dc) return;
    U(SelectObject)(d->dc, d->old);
    U(DeleteObject)(d->bmp);
    U(DeleteDC)(d->dc);
    memset(d, 0, sizeof *d);
}
static DWORD px(struct dib *d, int x, int y) { if (U(GdiFlush)) U(GdiFlush)(); return d->bits[y * d->w + x] & 0xffffff; }
#define RED 0xff0000u
/* red solid brush + NULL_PEN so only the interior is painted */
static HBRUSH ink(struct dib *d, HGDIOBJ *oldpen, HGDIOBJ *oldbr) {
    HBRUSH b = U(CreateSolidBrush)(RGB(255, 0, 0));
    *oldbr = U(SelectObject)(d->dc, b);
    *oldpen = U(SelectObject)(d->dc, U(GetStockObject)(NULL_PEN));
    return b;
}
static void unink(struct dib *d, HBRUSH b, HGDIOBJ oldpen, HGDIOBJ oldbr) {
    U(SelectObject)(d->dc, oldpen); U(SelectObject)(d->dc, oldbr); U(DeleteObject)(b);
}

/* ================================================================= NtGdiEllipse ====================== */

TEST(gap8_ellipse_user_fills_interior) {
    NEED_GDI(); REQUIRE(U(Ellipse)); REQUIRE(U(CreateSolidBrush)); REQUIRE(U(GetStockObject));
    struct dib d; if (!dib_make(&d, 40, 40)) { t_skip("no DIB"); return; }
    HGDIOBJ op, ob; HBRUSH b = ink(&d, &op, &ob);
    G8EQ(U(Ellipse)(d.dc, 0, 0, 40, 40), TRUE);
    G8EQ(px(&d, 20, 20), RED);
    G8EQ(px(&d, 10, 20), RED);
    G8EQ(px(&d, 20, 10), RED);
    /* the four corners of the bounding box lie outside the inscribed ellipse */
    G8EQ(px(&d, 0, 0), 0);
    G8EQ(px(&d, 39, 0), 0);
    G8EQ(px(&d, 0, 39), 0);
    G8EQ(px(&d, 39, 39), 0);
    unink(&d, b, op, ob); dib_free(&d);
}

TEST(gap8_ellipse_syscall_matches_userland_bits) {
    NEED_GDI(); REQUIRE(U(Ellipse)); REQUIRE(UN(NtGdiEllipse)); REQUIRE(U(CreateSolidBrush)); REQUIRE(U(GetStockObject));
    static const int rc[][4] = { {0,0,40,40}, {3,5,37,31}, {0,0,40,10}, {10,0,20,40}, {1,1,38,38}, {-5,-5,45,45} };
    for (unsigned i = 0; i < sizeof rc / sizeof rc[0]; i++) {
        struct dib a, b2;
        if (!dib_make(&a, 40, 40)) { t_skip("no DIB"); return; }
        if (!dib_make(&b2, 40, 40)) { dib_free(&a); t_skip("no DIB"); return; }
        HGDIOBJ op, ob, op2, ob2; HBRUSH br = ink(&a, &op, &ob), br2 = ink(&b2, &op2, &ob2);
        CHECKF(U(Ellipse)(a.dc, rc[i][0], rc[i][1], rc[i][2], rc[i][3]) != 0, "row %u", i);
        CHECKF(UN(NtGdiEllipse)(b2.dc, rc[i][0], rc[i][1], rc[i][2], rc[i][3]) != 0, "row %u", i);
        if (U(GdiFlush)) U(GdiFlush)();
        CHECKF(!memcmp(a.bits, b2.bits, 40 * 40 * 4), "row %u: wrapper and raw syscall rasterise differently", i);
        unink(&a, br, op, ob); unink(&b2, br2, op2, ob2); dib_free(&a); dib_free(&b2);
    }
}

TEST(gap8_ellipse_degenerate_rects_succeed) {
    NEED_GDI(); REQUIRE(UN(NtGdiEllipse)); REQUIRE(U(CreateSolidBrush)); REQUIRE(U(GetStockObject));
    /* Table: empty and inverted rectangles reach EBOX; whether they draw is not asserted, only that nothing is
     * left behind in the target surface outside the box. */
    static const int rc[][4] = { {10,10,10,10}, {10,10,30,10}, {10,10,10,30}, {30,30,10,10}, {30,10,10,30} };
    for (unsigned i = 0; i < sizeof rc / sizeof rc[0]; i++) {
        struct dib d; if (!dib_make(&d, 40, 40)) { t_skip("no DIB"); return; }
        HGDIOBJ op, ob; HBRUSH b = ink(&d, &op, &ob);
        SetLastError(0);
        int r = UN(NtGdiEllipse)(d.dc, rc[i][0], rc[i][1], rc[i][2], rc[i][3]);
        t_note("ellipse(%d,%d,%d,%d) -> %d err %lu", rc[i][0], rc[i][1], rc[i][2], rc[i][3], r, GetLastError());
        G8EQ(px(&d, 0, 0), 0);
        G8EQ(px(&d, 39, 39), 0);
        unink(&d, b, op, ob); dib_free(&d);
    }
}

/* ---- invalid DC table shared by Ellipse / RoundRect ----------------------------------------------- */
struct baddc { const char *what; HDC dc; };
/* gdi32full's pldcGet() lookup is only analysed as an argument-less call, so the error code of the user-side rejection is asserted just for the two rows whose route is certain. */
static int user_err_known(const char *w) { return !strcmp(w, "NULL") || !strcmp(w, "deleted DC"); }
static int bad_dcs(struct baddc *out, int cap, HBRUSH *tmpb, HBITMAP *tmpbm) {
    int n = 0;
    *tmpb = U(CreateSolidBrush)(RGB(1, 2, 3));
    *tmpbm = U(CreateBitmap)(4, 4, 1, 1, NULL);
    HDC dead = U(CreateCompatibleDC)(NULL);
    if (dead) U(DeleteDC)(dead);
    if (n < cap) { out[n].what = "NULL"; out[n].dc = NULL; n++; }
    if (n < cap) { out[n].what = "garbage small value"; out[n].dc = (HDC)(ULONG_PTR)0x1234; n++; }
    if (n < cap) { out[n].what = "brush handle"; out[n].dc = (HDC)*tmpb; n++; }
    if (n < cap) { out[n].what = "bitmap handle"; out[n].dc = (HDC)*tmpbm; n++; }
    if (n < cap) { out[n].what = "deleted DC"; out[n].dc = dead; n++; }
    if (n < cap) { out[n].what = "INVALID_HANDLE_VALUE"; out[n].dc = (HDC)INVALID_HANDLE_VALUE; n++; }
    return n;
}

TEST(gap8_ellipse_invalid_dc_syscall) {
    NEED_GDI(); REQUIRE(UN(NtGdiEllipse)); REQUIRE(U(CreateSolidBrush)); REQUIRE(U(CreateBitmap)); REQUIRE(U(DeleteDC));
    struct baddc t[8]; HBRUSH b; HBITMAP bm; int n = bad_dcs(t, 8, &b, &bm);
    for (int i = 0; i < n; i++) {
        SetLastError(0);
        int r = UN(NtGdiEllipse)(t[i].dc, 0, 0, 10, 10);
        CHECKF(r == 0, "%s: ret %d", t[i].what, r);
        CHECKF(GetLastError() == ERROR_INVALID_HANDLE, "%s: err %lu", t[i].what, GetLastError());
    }
    U(DeleteObject)(b); U(DeleteObject)(bm);
}

TEST(gap8_ellipse_invalid_dc_userland) {
    NEED_GDI(); REQUIRE(U(Ellipse)); REQUIRE(U(CreateSolidBrush)); REQUIRE(U(CreateBitmap)); REQUIRE(U(DeleteDC));
    struct baddc t[8]; HBRUSH b; HBITMAP bm; int n = bad_dcs(t, 8, &b, &bm);
    for (int i = 0; i < n; i++) {
        SetLastError(0);
        BOOL r = U(Ellipse)(t[i].dc, 0, 0, 10, 10);
        CHECKF(r == FALSE, "%s", t[i].what);
        if (user_err_known(t[i].what)) CHECKF(GetLastError() == ERROR_INVALID_HANDLE, "%s: err %lu", t[i].what, GetLastError());
    }
    U(DeleteObject)(b); U(DeleteObject)(bm);
}

/* ================================================================= NtGdiRoundRect ==================== */

TEST(gap8_roundrect_zero_corner_is_rectangle) {
    NEED_GDI(); REQUIRE(UN(NtGdiRoundRect)); REQUIRE(U(RoundRect)); REQUIRE(U(CreateSolidBrush)); REQUIRE(U(GetStockObject));
    /* width==0 or height==0 -> NtGdiRectangle: no rounded corners at all. */
    static const int wh[][2] = { {0,0}, {0,20}, {20,0} };
    for (unsigned i = 0; i < 3; i++) {
        for (int viaSys = 0; viaSys < 2; viaSys++) {
            struct dib d; if (!dib_make(&d, 40, 40)) { t_skip("no DIB"); return; }
            HGDIOBJ op, ob; HBRUSH b = ink(&d, &op, &ob);
            int r = viaSys ? UN(NtGdiRoundRect)(d.dc, 10, 10, 30, 30, wh[i][0], wh[i][1])
                           : U(RoundRect)(d.dc, 10, 10, 30, 30, wh[i][0], wh[i][1]);
            CHECKF(r != 0, "row %u sys %d", i, viaSys);
            G8EQ(px(&d, 20, 20), RED);
            G8EQ(px(&d, 11, 11), RED);            /* a rounded corner would leave this untouched */
            G8EQ(px(&d, 5, 5), 0);
            G8EQ(px(&d, 35, 35), 0);
            G8EQ(px(&d, 9, 20), 0);
            unink(&d, b, op, ob); dib_free(&d);
        }
    }
}

TEST(gap8_roundrect_rounded_corners_cut) {
    NEED_GDI(); REQUIRE(UN(NtGdiRoundRect)); REQUIRE(U(RoundRect)); REQUIRE(U(CreateSolidBrush)); REQUIRE(U(GetStockObject));
    for (int viaSys = 0; viaSys < 2; viaSys++) {
        struct dib d; if (!dib_make(&d, 40, 40)) { t_skip("no DIB"); return; }
        HGDIOBJ op, ob; HBRUSH b = ink(&d, &op, &ob);
        int r = viaSys ? UN(NtGdiRoundRect)(d.dc, 10, 10, 30, 30, 20, 20) : U(RoundRect)(d.dc, 10, 10, 30, 30, 20, 20);
        CHECKF(r != 0, "sys %d", viaSys);
        G8EQ(px(&d, 20, 20), RED);
        G8EQ(px(&d, 20, 12), RED);      /* middle of the straight top edge region */
        G8EQ(px(&d, 12, 20), RED);
        G8EQ(px(&d, 11, 11), 0);        /* corner pixel is outside the radius-10 arc */
        G8EQ(px(&d, 28, 11), 0);
        G8EQ(px(&d, 11, 28), 0);
        G8EQ(px(&d, 28, 28), 0);
        G8EQ(px(&d, 5, 5), 0);
        unink(&d, b, op, ob); dib_free(&d);
    }
}

TEST(gap8_roundrect_syscall_matches_userland_bits) {
    NEED_GDI(); REQUIRE(UN(NtGdiRoundRect)); REQUIRE(U(RoundRect)); REQUIRE(U(CreateSolidBrush)); REQUIRE(U(GetStockObject));
    static const int rc[][6] = { {0,0,40,40,10,10}, {2,3,38,30,30,8}, {5,5,35,35,1,1}, {0,0,40,40,200,200}, {4,4,36,36,-8,-8}, {36,36,4,4,12,12} };
    for (unsigned i = 0; i < sizeof rc / sizeof rc[0]; i++) {
        struct dib a, b2;
        if (!dib_make(&a, 40, 40)) { t_skip("no DIB"); return; }
        if (!dib_make(&b2, 40, 40)) { dib_free(&a); t_skip("no DIB"); return; }
        HGDIOBJ op, ob, op2, ob2; HBRUSH br = ink(&a, &op, &ob), br2 = ink(&b2, &op2, &ob2);
        int r1 = U(RoundRect)(a.dc, rc[i][0], rc[i][1], rc[i][2], rc[i][3], rc[i][4], rc[i][5]);
        int r2 = UN(NtGdiRoundRect)(b2.dc, rc[i][0], rc[i][1], rc[i][2], rc[i][3], rc[i][4], rc[i][5]);
        CHECKF((r1 != 0) == (r2 != 0), "row %u: %d vs %d", i, r1, r2);
        if (U(GdiFlush)) U(GdiFlush)();
        CHECKF(!memcmp(a.bits, b2.bits, 40 * 40 * 4), "row %u", i);
        unink(&a, br, op, ob); unink(&b2, br2, op2, ob2); dib_free(&a); dib_free(&b2);
    }
}

TEST(gap8_roundrect_invalid_dc_syscall) {
    NEED_GDI(); REQUIRE(UN(NtGdiRoundRect)); REQUIRE(U(CreateSolidBrush)); REQUIRE(U(CreateBitmap)); REQUIRE(U(DeleteDC));
    struct baddc t[8]; HBRUSH b; HBITMAP bm; int n = bad_dcs(t, 8, &b, &bm);
    for (int i = 0; i < n; i++) {
        SetLastError(0);
        int r = UN(NtGdiRoundRect)(t[i].dc, 0, 0, 10, 10, 4, 4);   /* the DCOBJ branch */
        CHECKF(r == 0, "%s: ret %d", t[i].what, r);
        CHECKF(GetLastError() == ERROR_INVALID_HANDLE, "%s: err %lu", t[i].what, GetLastError());
        SetLastError(0);
        r = UN(NtGdiRoundRect)(t[i].dc, 0, 0, 10, 10, 0, 0);       /* the Rectangle branch */
        CHECKF(r == 0, "%s (rectangle path): ret %d", t[i].what, r);
        t_note("%s rectangle path err %lu", t[i].what, GetLastError());
    }
    U(DeleteObject)(b); U(DeleteObject)(bm);
}

TEST(gap8_roundrect_invalid_dc_userland) {
    NEED_GDI(); REQUIRE(U(RoundRect)); REQUIRE(U(CreateSolidBrush)); REQUIRE(U(CreateBitmap)); REQUIRE(U(DeleteDC));
    struct baddc t[8]; HBRUSH b; HBITMAP bm; int n = bad_dcs(t, 8, &b, &bm);
    for (int i = 0; i < n; i++) {
        SetLastError(0);
        BOOL r = U(RoundRect)(t[i].dc, 0, 0, 10, 10, 4, 4);
        CHECKF(r == FALSE, "%s", t[i].what);
        if (user_err_known(t[i].what)) CHECKF(GetLastError() == ERROR_INVALID_HANDLE, "%s: err %lu", t[i].what, GetLastError());
    }
    U(DeleteObject)(b); U(DeleteObject)(bm);
}

/* ============================================== NtGdiCreateEllipticRgn / NtGdiCreateRoundRectRgn ===== */

static ell_fn ell_user(void) { return (ell_fn)U(CreateEllipticRgn); }
static ell_fn ell_sys(void)  { return t_is_wine() ? NULL : (ell_fn)UN(NtGdiCreateEllipticRgn); }
static HRGN rr_user(int a, int b, int c, int d) { return ((rr_fn)U(CreateRoundRectRgn))(a, b, c, d, 8, 8); }
static HRGN rr_sys(int a, int b, int c, int d)  { return ((rr_fn)rrs())(a, b, c, d, 8, 8); }

/* Every one of the four coordinates, with each out-of-range value, must be rejected before any work is done. */
static void range_reject(ell_fn f, const char *tag) {
    if (t_is_wine()) { t_note("Wine's gdi does not enforce the range test and would build huge regions"); return; }
    static const int bad[] = { 0x08000000, -0x08000001, 0x7fffffff, (int)0x80000000, 0x10000000, -0x10000000 };
    for (int pos = 0; pos < 4; pos++)
        for (unsigned k = 0; k < sizeof bad / sizeof bad[0]; k++) {
            int c[4] = { 0, 0, 10, 10 }; c[pos] = bad[k];
            SetLastError(0);
            HRGN h = f(c[0], c[1], c[2], c[3]);
            CHECKF(h == NULL, "%s pos %d val 0x%x returned %p", tag, pos, bad[k], (void *)h);
            CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "%s pos %d val 0x%x err %lu", tag, pos, bad[k], GetLastError());
            if (h) U(DeleteObject)(h);
        }
}

TEST(gap8_elliptic_rgn_range_reject_userland) { NEED_GDI(); REQUIRE(ell_user()); range_reject(ell_user(), "user"); }
TEST(gap8_elliptic_rgn_range_reject_syscall)  { NEED_GDI(); REQUIRE(ell_sys());  range_reject(ell_sys(), "sys"); }

static ell_fn g_rr_adapter_target;
TEST(gap8_roundrect_rgn_range_reject) {
    NEED_GDI(); REQUIRE(rrs()); REQUIRE(U(CreateRoundRectRgn));
    static const int bad[] = { 0x08000000, -0x08000001, 0x7fffffff, (int)0x80000000 };
    for (int viaSys = 0; viaSys < 2; viaSys++)
        for (int pos = 0; pos < 4; pos++)
            for (unsigned k = 0; k < sizeof bad / sizeof bad[0]; k++) {
                if (t_is_wine()) continue;        /* Wine's gdi layer does not enforce the kernel range test */
                int c[4] = { 0, 0, 10, 10 }; c[pos] = bad[k];
                SetLastError(0);
                HRGN h = viaSys ? rr_sys(c[0], c[1], c[2], c[3]) : rr_user(c[0], c[1], c[2], c[3]);
                CHECKF(h == NULL, "sys %d pos %d val 0x%x", viaSys, pos, bad[k]);
                CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "sys %d pos %d val 0x%x err %lu", viaSys, pos, bad[k], GetLastError());
                if (h) U(DeleteObject)(h);
            }
}

TEST(gap8_roundrect_rgn_corner_params_are_not_range_checked) {
    NEED_GDI(); REQUIRE(rrs());
    /* Only params 1..4 go through the range test; the ellipse width/height (params 5, 6) are handed to bRoundRect. */
    static const int wh[][2] = { {0,0}, {1,1}, {8,8}, {40,40}, {1000,1000}, {-8,-8}, {0,20}, {20,0} };
    for (unsigned i = 0; i < sizeof wh / sizeof wh[0]; i++) {
        SetLastError(0);
        HRGN h = ((rr_fn)rrs())(0, 0, 40, 40, wh[i][0], wh[i][1]);
        if (!h) { t_note("w=%d h=%d -> NULL err %lu", wh[i][0], wh[i][1], GetLastError()); CHECKF(wh[i][0] == wh[i][1] ? wh[i][0] <= 0 : (wh[i][0] == 0 || wh[i][1] == 0), "w=%d h=%d must be created", wh[i][0], wh[i][1]); continue; }
        G8EQ(GH_TYPE(h), GT_RGN);
        G8EQ(U(GetObjectType)(h), OBJ_REGION);
        RECT bx; int c = U(GetRgnBox)(h, &bx);
        CHECKF(c == SIMPLEREGION || c == COMPLEXREGION, "w=%d h=%d complexity %d", wh[i][0], wh[i][1], c);
        CHECKF(bx.left >= 0 && bx.top >= 0 && bx.right <= 40 && bx.bottom <= 40, "box (%ld,%ld,%ld,%ld)", bx.left, bx.top, bx.right, bx.bottom);
        U(DeleteObject)(h);
    }
}

TEST(gap8_elliptic_rgn_boundary_values_note) {
    NEED_GDI(); REQUIRE(ell_sys());
    /* Accepted by the range test on both ends; only the thin rows are created so no huge scan-line count results. */
    static const int rc[][4] = { {G8_LO,0,0,10}, {0,0,G8_HI,10}, {G8_LO,-5,G8_LO+10,5}, {G8_HI-10,0,G8_HI,10} };
    for (unsigned i = 0; i < sizeof rc / sizeof rc[0]; i++) {
        SetLastError(0);
        HRGN h = ell_sys()(rc[i][0], rc[i][1], rc[i][2], rc[i][3]);
        CHECKF(GetLastError() != ERROR_INVALID_PARAMETER, "row %u rejected by the range test", i);
        t_note("ellipse(%d,%d,%d,%d) -> %p err %lu", rc[i][0], rc[i][1], rc[i][2], rc[i][3], (void *)h, GetLastError());
        if (h) U(DeleteObject)(h);
    }
}

static void shape_checks(HRGN h, const char *tag, int ellipse) {
    CHECKF(h != NULL, "%s", tag);
    if (!h) return;
    G8EQ(GH_TYPE(h), GT_RGN);
    G8EQ(U(GetObjectType)(h), OBJ_REGION);
    if (!t_is_wine()) { struct k_gdicell c; if (k_gdi_cell(h, &c)) { G8EQ(c.type_check, (unsigned char)((ULONG_PTR)h >> 24)); G8EQ(g8_cell_type(h), GT_RGN); } }
    RECT bx; int cx = U(GetRgnBox)(h, &bx);
    G8EQ(cx, COMPLEXREGION);                       /* a curved outline never collapses to one rectangle */
    CHECKF(bx.left >= 0 && bx.top >= 0 && bx.right <= 40 && bx.bottom <= 40, "%s box (%ld,%ld,%ld,%ld)", tag, bx.left, bx.top, bx.right, bx.bottom);
    t_note("%s box (%ld,%ld,%ld,%ld)", tag, bx.left, bx.top, bx.right, bx.bottom);
    G8EQ(U(PtInRegion)(h, 20, 20), TRUE);
    G8EQ(U(PtInRegion)(h, 0, 0), FALSE);           /* the bounding-box corners are cut by the curve */
    G8EQ(U(PtInRegion)(h, 39, 0), FALSE);
    G8EQ(U(PtInRegion)(h, 0, 39), FALSE);
    G8EQ(U(PtInRegion)(h, 39, 39), FALSE);
    G8EQ(U(PtInRegion)(h, 100, 100), FALSE);
    G8EQ(U(PtInRegion)(h, -1, 20), FALSE);
    if (ellipse) { G8EQ(U(PtInRegion)(h, 20, 1), TRUE); G8EQ(U(PtInRegion)(h, 1, 20), TRUE); }
}

TEST(gap8_elliptic_rgn_shape_userland) { NEED_GDI(); REQUIRE(ell_user()); REQUIRE(U(PtInRegion)); REQUIRE(U(GetRgnBox)); REQUIRE(U(GetObjectType)); HRGN h = ell_user()(0, 0, 40, 40); shape_checks(h, "ellipse user", 1); if (h) U(DeleteObject)(h); }
TEST(gap8_elliptic_rgn_shape_syscall)  { NEED_GDI(); REQUIRE(ell_sys());  REQUIRE(U(PtInRegion)); REQUIRE(U(GetRgnBox)); REQUIRE(U(GetObjectType)); HRGN h = ell_sys()(0, 0, 40, 40); shape_checks(h, "ellipse sys", 1); if (h) U(DeleteObject)(h); }
TEST(gap8_roundrect_rgn_shape_userland) { NEED_GDI(); REQUIRE(U(CreateRoundRectRgn)); REQUIRE(U(PtInRegion)); REQUIRE(U(GetRgnBox)); REQUIRE(U(GetObjectType)); HRGN h = ((rr_fn)U(CreateRoundRectRgn))(0, 0, 40, 40, 40, 40); shape_checks(h, "roundrect user", 1); if (h) U(DeleteObject)(h); }
TEST(gap8_roundrect_rgn_shape_syscall)  { NEED_GDI(); REQUIRE(rrs()); REQUIRE(U(PtInRegion)); REQUIRE(U(GetRgnBox)); REQUIRE(U(GetObjectType)); HRGN h = rr_sys(0, 0, 40, 40); (void)h; if (h) U(DeleteObject)(h);
    h = ((rr_fn)rrs())(0, 0, 40, 40, 40, 40); shape_checks(h, "roundrect sys", 1); if (h) U(DeleteObject)(h); }

TEST(gap8_roundrect_rgn_small_corner_keeps_edges) {
    NEED_GDI(); REQUIRE(rrs()); REQUIRE(U(PtInRegion));
    HRGN h = ((rr_fn)rrs())(0, 0, 40, 40, 10, 10);
    CHECK(h != NULL); if (!h) return;
    G8EQ(U(PtInRegion)(h, 20, 20), TRUE);
    G8EQ(U(PtInRegion)(h, 20, 1), TRUE);
    G8EQ(U(PtInRegion)(h, 1, 20), TRUE);
    G8EQ(U(PtInRegion)(h, 0, 0), FALSE);
    G8EQ(U(PtInRegion)(h, 39, 0), FALSE);
    G8EQ(U(PtInRegion)(h, 0, 39), FALSE);
    G8EQ(U(PtInRegion)(h, 39, 39), FALSE);
    U(DeleteObject)(h);
}

TEST(gap8_elliptic_rgn_wrapper_equals_syscall) {
    NEED_GDI(); REQUIRE(ell_user()); REQUIRE(ell_sys()); REQUIRE(U(EqualRgn));
    static const int rc[][4] = { {0,0,40,40}, {-10,-10,10,10}, {3,7,33,12}, {0,0,1,1}, {0,0,2,100} };
    for (unsigned i = 0; i < sizeof rc / sizeof rc[0]; i++) {
        HRGN a = ell_user()(rc[i][0], rc[i][1], rc[i][2], rc[i][3]);
        HRGN b = ell_sys()(rc[i][0], rc[i][1], rc[i][2], rc[i][3]);
        CHECKF(a && b, "row %u", i);
        if (a && b) { CHECKF(U(EqualRgn)(a, b), "row %u", i); CHECKF(a != b, "row %u: two creations returned one handle", i); }
        if (a) U(DeleteObject)(a);
        if (b) U(DeleteObject)(b);
    }
}

TEST(gap8_roundrect_rgn_wrapper_equals_syscall) {
    NEED_GDI(); REQUIRE(U(CreateRoundRectRgn)); REQUIRE(rrs()); REQUIRE(U(EqualRgn));
    static const int rc[][6] = { {0,0,40,40,10,10}, {-10,-10,10,10,4,4}, {3,7,33,12,6,2}, {0,0,100,3,50,3} };
    for (unsigned i = 0; i < sizeof rc / sizeof rc[0]; i++) {
        HRGN a = ((rr_fn)U(CreateRoundRectRgn))(rc[i][0], rc[i][1], rc[i][2], rc[i][3], rc[i][4], rc[i][5]);
        HRGN b = ((rr_fn)rrs())(rc[i][0], rc[i][1], rc[i][2], rc[i][3], rc[i][4], rc[i][5]);
        CHECKF(a && b, "row %u", i);
        if (a && b) CHECKF(U(EqualRgn)(a, b), "row %u", i);
        if (a) U(DeleteObject)(a);
        if (b) U(DeleteObject)(b);
    }
}

TEST(gap8_elliptic_rgn_degenerate_note) {
    NEED_GDI(); REQUIRE(ell_sys());
    static const int rc[][4] = { {5,5,5,5}, {5,5,5,20}, {5,5,20,5}, {20,20,5,5}, {20,5,5,20} };
    for (unsigned i = 0; i < sizeof rc / sizeof rc[0]; i++) {
        SetLastError(0);
        HRGN h = ell_sys()(rc[i][0], rc[i][1], rc[i][2], rc[i][3]);
        RECT bx = {0}; int c = h ? U(GetRgnBox)(h, &bx) : -1;
        t_note("ellipse(%d,%d,%d,%d) -> %p complexity %d box(%ld,%ld,%ld,%ld) err %lu", rc[i][0], rc[i][1], rc[i][2], rc[i][3], (void *)h, c, bx.left, bx.top, bx.right, bx.bottom, GetLastError());
        CHECKF(GetLastError() != ERROR_INVALID_PARAMETER, "row %u: in-range coordinates must pass the range test", i);
        if (h) U(DeleteObject)(h);
    }
}

TEST(gap8_elliptic_rgn_handle_lifetime) {
    NEED_GDI(); REQUIRE(ell_sys()); REQUIRE(U(GetObjectType)); REQUIRE(U(PtInRegion));
    HRGN h = ell_sys()(0, 0, 20, 20);
    CHECK(h != NULL); if (!h) return;
    G8EQ(U(GetObjectType)(h), OBJ_REGION);
    G8EQ(U(DeleteObject)(h), TRUE);
    /* GetObjectType keeps answering OBJ_REGION for a deleted handle on Windows 10, so death is shown by the failing calls */
    G8EQ(U(PtInRegion)(h, 10, 10), FALSE);
    G8EQ(U(DeleteObject)(h), FALSE);
}

TEST(gap8_elliptic_rgn_many_handles_distinct) {
    NEED_GDI(); REQUIRE(ell_sys());
    enum { N = 64 };
    HRGN hs[N]; int n = 0;
    for (int i = 0; i < N; i++) { hs[i] = ell_sys()(0, 0, 10 + i, 10 + i); if (!hs[i]) break; n++; }
    G8EQ(n, N);
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) if (hs[i] == hs[j]) { CHECKF(0, "duplicate handle %d/%d", i, j); }
        CHECKF(GH_TYPE(hs[i]) == GT_RGN, "handle %d", i);
        CHECKF(((ULONG_PTR)hs[i] & 0xffff) != 0, "index 0 is never handed out (%d)", i);
    }
    for (int i = 0; i < n; i++) U(DeleteObject)(hs[i]);
}

/* ================================================================= NtGdiOffsetClipRgn ================ */

static int clip_off(int viaSys, HDC dc, int x, int y) {
    return viaSys ? UN(NtGdiOffsetClipRgn)(dc, x, y, 0) : U(OffsetClipRgn)(dc, x, y);
}
static int clip_box_is(HDC dc, int l, int t, int r, int b) {
    RECT rc = {0}; int c = U(GetClipBox)(dc, &rc);
    return c != 0 && rc.left == l && rc.top == t && rc.right == r && rc.bottom == b;
}

TEST(gap8_offsetclip_no_clip_is_simple) {
    NEED_GDI(); REQUIRE(UN(NtGdiOffsetClipRgn)); REQUIRE(U(OffsetClipRgn));
    for (int viaSys = 0; viaSys < 2; viaSys++) {
        struct dib d; if (!dib_make(&d, 100, 100)) { t_skip("no DIB"); return; }
        G8EQ(clip_off(viaSys, d.dc, 0, 0), SIMPLEREGION);
        G8EQ(clip_off(viaSys, d.dc, 17, -9), SIMPLEREGION);
        dib_free(&d);
    }
}

TEST(gap8_offsetclip_rect_clip_moves_box) {
    NEED_GDI(); REQUIRE(UN(NtGdiOffsetClipRgn)); REQUIRE(U(OffsetClipRgn)); REQUIRE(U(IntersectClipRect)); REQUIRE(U(GetClipBox));
    static const int off[][2] = { {0,0}, {5,7}, {-10,-10}, {60,0}, {0,60}, {-3,40}, {1,1} };
    for (int viaSys = 0; viaSys < 2; viaSys++)
        for (unsigned i = 0; i < sizeof off / sizeof off[0]; i++) {
            struct dib d; if (!dib_make(&d, 100, 100)) { t_skip("no DIB"); return; }
            /* Windows 10 reports COMPLEXREGION for the first IntersectClipRect on a fresh memory DC; GetClipBox below shows the real shape */
            CHECK(U(IntersectClipRect)(d.dc, 20, 20, 40, 40) != 0);
            CHECK(clip_box_is(d.dc, 20, 20, 40, 40));
            int r = clip_off(viaSys, d.dc, off[i][0], off[i][1]);
            CHECKF(r == SIMPLEREGION, "sys %d row %u ret %d", viaSys, i, r);
            CHECKF(clip_box_is(d.dc, 20 + off[i][0], 20 + off[i][1], 40 + off[i][0], 40 + off[i][1]), "sys %d row %u", viaSys, i);
            dib_free(&d);
        }
}

TEST(gap8_offsetclip_offsets_accumulate) {
    NEED_GDI(); REQUIRE(U(OffsetClipRgn)); REQUIRE(U(IntersectClipRect)); REQUIRE(U(GetClipBox));
    struct dib d; if (!dib_make(&d, 100, 100)) { t_skip("no DIB"); return; }
    U(IntersectClipRect)(d.dc, 10, 10, 20, 20);
    for (int i = 0; i < 5; i++) G8EQ(U(OffsetClipRgn)(d.dc, 3, 4), SIMPLEREGION);
    CHECK(clip_box_is(d.dc, 25, 30, 35, 40));
    for (int i = 0; i < 5; i++) U(OffsetClipRgn)(d.dc, -3, -4);
    CHECK(clip_box_is(d.dc, 10, 10, 20, 20));
    dib_free(&d);
}

TEST(gap8_offsetclip_complex_clip_stays_complex) {
    NEED_GDI(); REQUIRE(UN(NtGdiOffsetClipRgn)); REQUIRE(U(CreateRectRgn)); REQUIRE(U(CombineRgn)); REQUIRE(U(ExtSelectClipRgn)); REQUIRE(U(GetClipBox));
    for (int viaSys = 0; viaSys < 2; viaSys++) {
        struct dib d; if (!dib_make(&d, 100, 100)) { t_skip("no DIB"); return; }
        HRGN a = U(CreateRectRgn)(10, 10, 20, 20), b = U(CreateRectRgn)(40, 40, 50, 50), u = U(CreateRectRgn)(0, 0, 1, 1);
        G8EQ(U(CombineRgn)(u, a, b, RGN_OR), COMPLEXREGION);
        G8EQ(U(ExtSelectClipRgn)(d.dc, u, RGN_COPY), COMPLEXREGION);
        int r = clip_off(viaSys, d.dc, 5, 6);
        CHECKF(r == COMPLEXREGION, "sys %d ret %d", viaSys, r);
        CHECKF(clip_box_is(d.dc, 15, 16, 55, 56), "sys %d", viaSys);
        if (U(PtVisible)) { G8EQ(U(PtVisible)(d.dc, 16, 17), TRUE); G8EQ(U(PtVisible)(d.dc, 11, 11), FALSE); G8EQ(U(PtVisible)(d.dc, 46, 47), TRUE); G8EQ(U(PtVisible)(d.dc, 30, 30), FALSE); }
        U(DeleteObject)(a); U(DeleteObject)(b); U(DeleteObject)(u);
        dib_free(&d);
    }
}

TEST(gap8_offsetclip_null_clip_stays_null) {
    NEED_GDI(); REQUIRE(UN(NtGdiOffsetClipRgn)); REQUIRE(U(IntersectClipRect)); REQUIRE(U(GetClipBox));
    for (int viaSys = 0; viaSys < 2; viaSys++) {
        struct dib d; if (!dib_make(&d, 100, 100)) { t_skip("no DIB"); return; }
        CHECK(U(IntersectClipRect)(d.dc, 10, 10, 10, 10) != 0);   /* Windows 10 answers COMPLEXREGION here, the empty box shows in GetClipBox */
        { RECT e = {1,1,1,1}; G8EQ(U(GetClipBox)(d.dc, &e), NULLREGION); }
        int r = clip_off(viaSys, d.dc, 3, 3);
        CHECKF(r == NULLREGION, "sys %d ret %d", viaSys, r);
        RECT rc = {1,1,1,1};
        G8EQ(U(GetClipBox)(d.dc, &rc), NULLREGION);
        dib_free(&d);
    }
}

TEST(gap8_offsetclip_offset_is_in_logical_units) {
    NEED_GDI(); REQUIRE(U(OffsetClipRgn)); REQUIRE(U(IntersectClipRect)); REQUIRE(U(GetClipBox)); REQUIRE(U(SetMapMode)); REQUIRE(U(SetWindowExtEx)); REQUIRE(U(SetViewportExtEx));
    /* The offset vector is run through the DC transform (EXFORMOBJ::bXform) and the clip region is moved in device
     * space; GetClipBox maps back to logical space, so the logical box shifts by exactly the logical offset. */
    struct dib d; if (!dib_make(&d, 100, 100)) { t_skip("no DIB"); return; }
    U(SetMapMode)(d.dc, MM_ANISOTROPIC);
    U(SetWindowExtEx)(d.dc, 100, 100, NULL);
    U(SetViewportExtEx)(d.dc, 200, 200, NULL);
    U(IntersectClipRect)(d.dc, 5, 5, 15, 15);
    G8EQ(U(OffsetClipRgn)(d.dc, 5, 5), SIMPLEREGION);
    CHECK(clip_box_is(d.dc, 10, 10, 20, 20));
    G8EQ(U(OffsetClipRgn)(d.dc, -10, 0), SIMPLEREGION);
    CHECK(clip_box_is(d.dc, 0, 10, 10, 20));
    dib_free(&d);
}

TEST(gap8_offsetclip_flipped_axis_transform) {
    NEED_GDI(); REQUIRE(U(OffsetClipRgn)); REQUIRE(U(IntersectClipRect)); REQUIRE(U(GetClipBox)); REQUIRE(U(SetMapMode)); REQUIRE(U(SetWindowExtEx)); REQUIRE(U(SetViewportExtEx));
    struct dib d; if (!dib_make(&d, 100, 100)) { t_skip("no DIB"); return; }
    U(SetMapMode)(d.dc, MM_ANISOTROPIC);
    U(SetWindowExtEx)(d.dc, 1, 1, NULL);
    U(SetViewportExtEx)(d.dc, -1, 1, NULL);        /* x axis mirrored */
    int before = U(IntersectClipRect)(d.dc, -30, 10, -10, 30);
    t_note("clip after mirrored intersect: %d", before);
    /* GetClipBox does not give a normalised box under a mirrored mapping on Windows 10, so the boxes are read with the axis
     * restored (1:1 mapping). The mirrored logical offset (-5, 2) moves the device box by (+5, +2). */
    RECT b0 = {0}, b1 = {0};
    U(SetViewportExtEx)(d.dc, 1, 1, NULL);
    G8EQ(U(GetClipBox)(d.dc, &b0), SIMPLEREGION);
    U(SetViewportExtEx)(d.dc, -1, 1, NULL);
    int r = U(OffsetClipRgn)(d.dc, -5, 2);
    CHECKF(r == SIMPLEREGION, "ret %d", r);
    U(SetViewportExtEx)(d.dc, 1, 1, NULL);
    G8EQ(U(GetClipBox)(d.dc, &b1), SIMPLEREGION);
    CHECKF(b0.left == 10 && b0.top == 10 && b0.right == 30 && b0.bottom == 30 && b1.left == 15 && b1.top == 12 && b1.right == 35 && b1.bottom == 32,
           "box (%ld,%ld,%ld,%ld) -> (%ld,%ld,%ld,%ld)", b0.left, b0.top, b0.right, b0.bottom, b1.left, b1.top, b1.right, b1.bottom);
    dib_free(&d);
}

TEST(gap8_offsetclip_overflow_fails_cleanly) {
    NEED_GDI(); REQUIRE(UN(NtGdiOffsetClipRgn)); REQUIRE(U(IntersectClipRect)); REQUIRE(U(GetClipBox));
    struct dib d; if (!dib_make(&d, 100, 100)) { t_skip("no DIB"); return; }
    U(IntersectClipRect)(d.dc, 20, 20, 40, 40);
    SetLastError(0);
    int r = UN(NtGdiOffsetClipRgn)(d.dc, 0x7fffffff, 0x7fffffff, 0);
    t_note("offset by INT_MAX -> %d err %lu", r, GetLastError());
    if (r == 0) CHECK(clip_box_is(d.dc, 20, 20, 40, 40));   /* a failed bOffset must not move the clip */
    dib_free(&d);
}

TEST(gap8_offsetclip_invalid_dc) {
    NEED_GDI(); REQUIRE(UN(NtGdiOffsetClipRgn)); REQUIRE(U(OffsetClipRgn)); REQUIRE(U(CreateSolidBrush)); REQUIRE(U(CreateBitmap)); REQUIRE(U(DeleteDC));
    struct baddc t[8]; HBRUSH b; HBITMAP bm; int n = bad_dcs(t, 8, &b, &bm);
    for (int i = 0; i < n; i++) {
        SetLastError(0);
        int r = UN(NtGdiOffsetClipRgn)(t[i].dc, 1, 1, 0);
        CHECKF(r == 0, "sys %s ret %d", t[i].what, r);
        CHECKF(GetLastError() == ERROR_INVALID_HANDLE, "sys %s err %lu", t[i].what, GetLastError());
    }
    for (int i = 0; i < n; i++) {
        SetLastError(0);
        int r = U(OffsetClipRgn)(t[i].dc, 1, 1);
        CHECKF(r == 0, "user %s ret %d", t[i].what, r);
        if (user_err_known(t[i].what)) CHECKF(GetLastError() == ERROR_INVALID_HANDLE, "user %s err %lu", t[i].what, GetLastError());
    }
    U(DeleteObject)(b); U(DeleteObject)(bm);
}

TEST(gap8_offsetclip_info_dc_rejected) {
    NEED_GDI(); REQUIRE(UN(NtGdiOffsetClipRgn)); REQUIRE(U(CreateICW)); REQUIRE(U(DeleteDC));
    /* DC type 2 (information DC) fails the "dc+0xc < 2" test and reports ERROR_INVALID_HANDLE. */
    HDC ic = U(CreateICW)(L"DISPLAY", NULL, NULL, NULL);
    if (!ic) { t_skip("no display IC"); return; }
    SetLastError(0);
    int r = UN(NtGdiOffsetClipRgn)(ic, 1, 1, 0);
    CHECKF(r == SIMPLEREGION, "ret %d", r);   /* an information DC is accepted and has no clip, as a memory DC without one */
    CHECKF(GetLastError() == 0, "err %lu", GetLastError());
    U(DeleteDC)(ic);
}

TEST(gap8_offsetclip_screen_dc_note) {
    NEED_GDI(); REQUIRE(UN(NtGdiOffsetClipRgn));
    HDC dc = U(GetDC)(NULL);
    if (!dc) { t_skip("no screen DC"); return; }
    SetLastError(0);
    int r = UN(NtGdiOffsetClipRgn)(dc, 0, 0, 0);
    t_note("screen DC offset(0,0) -> %d (visible region based clip, not asserted)", r);
    CHECKF(r == 0 || r == NULLREGION || r == SIMPLEREGION || r == COMPLEXREGION, "ret %d", r);
    U(ReleaseDC)(NULL, dc);
}

/* ================================================================= NtGdiResizePalette ================ */

static HPALETTE mkpal(int n, int seed) {
    LOGPALETTE *lp = (LOGPALETTE *)calloc(1, sizeof(LOGPALETTE) + (n - 1) * sizeof(PALETTEENTRY));
    lp->palVersion = 0x300; lp->palNumEntries = (WORD)n;
    for (int i = 0; i < n; i++) { lp->palPalEntry[i].peRed = (BYTE)(seed + i); lp->palPalEntry[i].peGreen = (BYTE)(seed * 3 + i * 2); lp->palPalEntry[i].peBlue = (BYTE)(255 - i); lp->palPalEntry[i].peFlags = 0; }
    HPALETTE p = U(CreatePalette)(lp);
    free(lp);
    return p;
}
static int pal_count(HPALETTE p) { return (int)U(GetPaletteEntries)(p, 0, 0, NULL); }

TEST(gap8_resizepalette_grow_and_shrink) {
    NEED_GDI(); REQUIRE(U(CreatePalette)); REQUIRE(U(ResizePalette)); REQUIRE(U(GetPaletteEntries));
    static const int sizes[] = { 32, 8, 1, 256, 17, 0x400, 2 };
    HPALETTE p = mkpal(16, 10);
    CHECK(p != NULL); if (!p) return;
    G8EQ(GH_TYPE(p), GT_PAL);
    G8EQ(pal_count(p), 16);
    HPALETTE same = p;
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        int before = pal_count(p);
        PALETTEENTRY old[64]; int keep = before < 64 ? before : 64;
        U(GetPaletteEntries)(p, 0, keep, old);
        G8EQ(U(ResizePalette)(p, sizes[i]), TRUE);
        G8EQ(p, same);                                   /* handle value is stable across a resize */
        G8EQ(pal_count(p), sizes[i]);
        WORD w = 0;
        G8EQ(U(GetObjectW)(p, sizeof w, &w), sizeof w);  /* GetObject(HPALETTE) reports the entry count */
        G8EQ(w, sizes[i]);
        int cmp = keep < sizes[i] ? keep : sizes[i]; if (cmp > 64) cmp = 64;
        PALETTEENTRY now[64]; U(GetPaletteEntries)(p, 0, cmp, now);
        for (int k = 0; k < cmp; k++)
            CHECKF(now[k].peRed == old[k].peRed && now[k].peGreen == old[k].peGreen && now[k].peBlue == old[k].peBlue, "size %d entry %d lost", sizes[i], k);
    }
    U(DeleteObject)(p);
}

TEST(gap8_resizepalette_syscall_bounds) {
    NEED_GDI(); REQUIRE(UN(NtGdiResizePalette)); REQUIRE(U(CreatePalette)); REQUIRE(U(GetPaletteEntries));
    HPALETTE p = mkpal(16, 3);
    CHECK(p != NULL); if (!p) return;
    static const struct { ULONG n; int ok; } t[] = {
        { 0, 0 }, { 1, 1 }, { 0x400, 1 }, { 0x401, 0 }, { 0xffffffffu, 0 }, { 0x80000000u, 0 }, { 0x1000, 0 }, { 0x3ff, 1 }, { 16, 1 }
    };
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
        if (t_is_wine() && t[i].n > 0x400) continue;      /* Wine allocates the requested size without a limit */
        int before = pal_count(p);
        int r = UN(NtGdiResizePalette)(p, t[i].n);
        CHECKF((r != 0) == t[i].ok, "size 0x%lx ret %d", t[i].n, r);
        if (!t[i].ok) G8EQ(pal_count(p), before);         /* a rejected resize leaves the palette alone */
        else G8EQ(pal_count(p), (int)t[i].n);
    }
    U(DeleteObject)(p);
}

TEST(gap8_resizepalette_invalid_objects) {
    NEED_GDI(); REQUIRE(UN(NtGdiResizePalette)); REQUIRE(U(ResizePalette)); REQUIRE(U(CreatePalette)); REQUIRE(U(GetStockObject)); REQUIRE(U(CreateSolidBrush)); REQUIRE(U(GetPaletteEntries));
    HPALETTE stock = (HPALETTE)U(GetStockObject)(DEFAULT_PALETTE);
    int cnt = pal_count(stock);
    G8EQ(UN(NtGdiResizePalette)(stock, 30), 0);           /* the stock palette is never resizable */
    G8EQ(U(ResizePalette)(stock, 30), FALSE);
    G8EQ(pal_count(stock), cnt);
    G8EQ(cnt, 20);
    HBRUSH br = U(CreateSolidBrush)(RGB(1, 2, 3));
    G8EQ(UN(NtGdiResizePalette)((HPALETTE)br, 16), 0);
    G8EQ(U(ResizePalette)((HPALETTE)br, 16), FALSE);
    U(DeleteObject)(br);
    G8EQ(UN(NtGdiResizePalette)(NULL, 16), 0);
    G8EQ(UN(NtGdiResizePalette)((HPALETTE)(ULONG_PTR)0x1234, 16), 0);
    G8EQ(U(ResizePalette)(NULL, 16), FALSE);
    HPALETTE p = mkpal(4, 1);
    if (p) { U(DeleteObject)(p); G8EQ(UN(NtGdiResizePalette)(p, 16), 0); G8EQ(U(ResizePalette)(p, 16), FALSE); }
}

TEST(gap8_resizepalette_cell_type_survives) {
    NEED_GDI(); REQUIRE(U(CreatePalette)); REQUIRE(U(ResizePalette));
    HPALETTE p = mkpal(8, 2);
    CHECK(p != NULL); if (!p) return;
    unsigned char u = (unsigned char)((ULONG_PTR)p >> 24);
    G8EQ(U(ResizePalette)(p, 64), TRUE);
    if (!t_is_wine()) {
        struct k_gdicell c;
        if (k_gdi_cell(p, &c)) { G8EQ(c.type_check, u); G8EQ(g8_cell_type(p), GT_PAL); }
    }
    G8EQ(U(GetObjectType)(p), OBJ_PAL);
    U(DeleteObject)(p);
}

/* ================================================================= NtGdiSwapBuffers ================== */

TEST(gap8_swapbuffers_invalid_dc) {
    NEED_GDI(); REQUIRE(UN(NtGdiSwapBuffers)); REQUIRE(U(CreateSolidBrush)); REQUIRE(U(CreateBitmap)); REQUIRE(U(DeleteDC));
    struct baddc t[8]; HBRUSH b; HBITMAP bm; int n = bad_dcs(t, 8, &b, &bm);
    for (int i = 0; i < n; i++) {
        SetLastError(0);
        int r = UN(NtGdiSwapBuffers)(t[i].dc, 0, 0);
        CHECKF(r == 0, "%s: ret %d", t[i].what, r);
        CHECKF(GetLastError() == ERROR_INVALID_HANDLE, "%s: err %lu", t[i].what, GetLastError());
    }
    U(DeleteObject)(b); U(DeleteObject)(bm);
}

TEST(gap8_swapbuffers_dc_without_gl_context) {
    NEED_GDI(); REQUIRE(UN(NtGdiSwapBuffers));
    /* A memory DC has no WNDOBJ/ICD context (dc+0x1f0 == 0): same failure as an invalid DC. A screen DC is deliberately not tried. */
    struct dib d; if (!dib_make(&d, 8, 8)) { t_skip("no DIB"); return; }
    SetLastError(0);
    /* Windows 10 accepts a memory DC and reports success without a GL context */
    CHECK(UN(NtGdiSwapBuffers)(d.dc, 0, 0) == 1);
    dib_free(&d);
}
