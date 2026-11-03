/* Wrong handle types for the thin DC syscalls: every DC-taking call (IntersectClipRect, ExcludeClipRect, GetBoundsRect,
 * ScaleWindowExtEx, MoveToEx and the Nt stubs) rejects a NULL handle, a GDI object that is not a DC, a junk handle and
 * a window handle, returning 0 with ERROR_INVALID_HANDLE (6). A deleted DC is rejected too. */
#include "xn.h"

#define NEED_ALL() do { if (!U(IntersectClipRect) || !U(ExcludeClipRect) || !U(GetBoundsRect) || !U(ScaleWindowExtEx) || !U(MoveToEx) || \
    !UN(NtGdiIntersectClipRect) || !UN(NtGdiExcludeClipRect) || !UN(NtGdiGetBoundsRect) || !UN(NtGdiScaleWindowExtEx) || !UN(NtGdiMoveTo)) { t_skip("exports missing"); return; } } while (0)

static void bad_clip(int k) {
    XH(x); NEED_ALL();
    HDC d = (HDC)x.h[k];
    SetLastError(0); CHECK(U(IntersectClipRect)(d, 0, 0, 5, 5) == 0); EXPECT_ERR(6);
    SetLastError(0); CHECK(U(ExcludeClipRect)(d, 0, 0, 5, 5) == 0); EXPECT_ERR(6);
    SetLastError(0); CHECK(UN(NtGdiIntersectClipRect)(d, 0, 0, 5, 5) == 0); EXPECT_ERR(6);
    SetLastError(0); CHECK(UN(NtGdiExcludeClipRect)(d, 0, 0, 5, 5) == 0); EXPECT_ERR(6);
    xh_close(&x);
}
static void bad_bounds(int k) {
    XH(x); NEED_ALL();
    HDC d = (HDC)x.h[k]; RECT r = { 1, 2, 3, 4 };
    CHECK(U(GetBoundsRect)(d, &r, 0) == 0);
    CHECK(UN(NtGdiGetBoundsRect)(d, &r, 0) == 0);
    CHECK(U(GetBoundsRect)(d, &r, DCB_RESET) == 0);
    xh_close(&x);
}
static void bad_scale(int k) {
    XH(x); NEED_ALL();
    HDC d = (HDC)x.h[k]; SIZE sz = { 7, 7 };
    SetLastError(0); CHECK(!U(ScaleWindowExtEx)(d, 1, 2, 3, 4, &sz));
    CHECK(sz.cx == 7 && sz.cy == 7);              /* a failed call leaves the output untouched */
    CHECK(!UN(NtGdiScaleWindowExtEx)(d, 1, 2, 3, 4, &sz));
    CHECK(sz.cx == 7 && sz.cy == 7);
    xh_close(&x);
}
static void bad_moveto(int k) {
    XH(x); NEED_ALL();
    HDC d = (HDC)x.h[k]; POINT p = { 7, 7 };
    SetLastError(0); CHECK(!U(MoveToEx)(d, 1, 2, &p));
    CHECK(p.x == 7 && p.y == 7);
    SetLastError(0); CHECK(!UN(NtGdiMoveTo)(d, 1, 2, &p)); EXPECT_ERR(6);
    CHECK(p.x == 7 && p.y == 7);
    xh_close(&x);
}
/* gdi32 reports ERROR_INVALID_HANDLE for non-DC GDI objects and ERROR_INVALID_PARAMETER for an HWND passed as a DC. */
#define BADT(nm, k) \
    XT(bad_clip_##nm)    { bad_clip(k); } \
    XT(bad_scale_##nm)   { bad_scale(k); } \
    XT(bad_moveto_##nm)  { bad_moveto(k); }
BADT(null, H_NULL)
BADT(bitmap, H_BITMAP)
BADT(pen, H_PEN)
BADT(brush, H_BRUSH)
BADT(region, H_REGION)
BADT(palette, H_PALETTE)
BADT(stock_pen, H_STOCKPEN)
BADT(junk, H_JUNK)
BADT(minus1, H_MINUS1)
BADT(hwnd, H_HWND)

XT(bad_bounds_every_type) { XH(x); NEED_ALL(); for (int k = H_NULL; k <= H_HWND; k++) { RECT r = { 1, 2, 3, 4 }; CHECKF(U(GetBoundsRect)((HDC)x.h[k], &r, 0) == 0 && UN(NtGdiGetBoundsRect)((HDC)x.h[k], &r, 0) == 0, "%s", xh_name[k]); } xh_close(&x); }
XT(gdi_scale_moveto_report_invalid_handle) {
    XH(x); NEED_ALL();
    for (int k = H_NULL; k <= H_MINUS1; k++) {
        SIZE sz = { 7, 7 }; POINT p = { 7, 7 };
        SetLastError(0); CHECKF(!U(ScaleWindowExtEx)((HDC)x.h[k], 1, 2, 3, 4, &sz), "%s", xh_name[k]); EXPECT_ERR(6);
        SetLastError(0); CHECKF(!U(MoveToEx)((HDC)x.h[k], 1, 2, &p), "%s", xh_name[k]); EXPECT_ERR(6);
    }
    xh_close(&x);
}
XT(deleted_dc_is_rejected) {
    NEED_ALL(); if (!U(CreateCompatibleDC) || !U(DeleteDC)) { t_skip("exports missing"); return; }
    HDC d = U(CreateCompatibleDC)(NULL); RECT r; SIZE sz; POINT p;
    CHECK(d != NULL); CHECK(U(DeleteDC)(d));
    CHECK(U(IntersectClipRect)(d, 0, 0, 5, 5) == 0);
    CHECK(U(ExcludeClipRect)(d, 0, 0, 5, 5) == 0);
    CHECK(UN(NtGdiIntersectClipRect)(d, 0, 0, 5, 5) == 0);
    CHECK(UN(NtGdiExcludeClipRect)(d, 0, 0, 5, 5) == 0);
    CHECK(U(GetBoundsRect)(d, &r, 0) == 0);
    CHECK(!U(ScaleWindowExtEx)(d, 1, 1, 1, 1, &sz));
    CHECK(!U(MoveToEx)(d, 1, 1, &p));
}
XT(window_dc_accepts_clip_calls) {
    NEED_ALL(); if (!U(GetDC) || !U(ReleaseDC) || !U(SelectClipRgn)) { t_skip("exports missing"); return; }
    HDC d = U(GetDC)(NULL); if (!d) { t_skip("no screen DC"); return; }
    RECT cb; U(SelectClipRgn)(d, NULL);
    EXPECT_EQ(U(IntersectClipRect)(d, 0, 0, 10, 10), RC_COMPLEX);
    CHECK(U(GetClipBox)(d, &cb) == RC_SIMPLE && box_is(&cb, 0, 0, 10, 10));
    EXPECT_EQ(U(ExcludeClipRect)(d, 0, 0, 10, 10), RC_NULL);
    U(SelectClipRgn)(d, NULL); U(ReleaseDC)(NULL, d);
}
