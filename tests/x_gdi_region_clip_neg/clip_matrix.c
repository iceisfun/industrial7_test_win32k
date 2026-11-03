/* Return-code matrix for IntersectClipRect and ExcludeClipRect on a 64x48 memory DC, generated from the
 * values observed on Windows 10 22H2: the call result, the last error of a failed call, GetClipBox, and the
 * clip region read back through GetRandomRgn. States: none = no clip region, rect = (10,10,40,30),
 * complex = that rectangle united with (30,25,55,45), empty = a NULLREGION clip. Each row runs through the
 * Nt stub, the gdi32 export, and the gdi32 export with GdiSetBatchLimit(1); all three must agree. */
#include "xn.h"

struct crow { int st, op, rc[4], ret, err, cbret, cb[4], rrret, rr[4]; };
static void run_row(const struct crow *w) {
    struct fx f;
    if (!U(IntersectClipRect) || !U(ExcludeClipRect) || !U(GetClipBox) || !U(SelectClipRgn) || !U(CombineRgn) || !U(SetRectRgn) ||
        !UN(NtGdiIntersectClipRect) || !UN(NtGdiExcludeClipRect) || !UN(NtGdiGetRandomRgn) || !U(GdiFlush)) { t_skip("exports missing"); return; }
    if (!fx_open(&f, 64, 48)) { t_skip("cannot create memory DC + DIB section"); return; }
    for (int route = 0; route < 3; route++) {
        U(SelectClipRgn)(f.dc, NULL);
        if (w->st) {
            HRGN s = U(CreateRectRgn)(10, 10, 40, 30);
            if (w->st == 2) { HRGN s2 = U(CreateRectRgn)(30, 25, 55, 45); U(CombineRgn)(s, s, s2, RGN_OR); U(DeleteObject)(s2); }
            if (w->st == 3) U(SetRectRgn)(s, 0, 0, 0, 0);
            U(SelectClipRgn)(f.dc, s); U(DeleteObject)(s);
        }
        DWORD old = xn_limit_set(route);
        SetLastError(0);
        int r;
        if (route == ROUTE_NT) r = w->op ? UN(NtGdiExcludeClipRect)(f.dc, w->rc[0], w->rc[1], w->rc[2], w->rc[3]) : UN(NtGdiIntersectClipRect)(f.dc, w->rc[0], w->rc[1], w->rc[2], w->rc[3]);
        else r = w->op ? U(ExcludeClipRect)(f.dc, w->rc[0], w->rc[1], w->rc[2], w->rc[3]) : U(IntersectClipRect)(f.dc, w->rc[0], w->rc[1], w->rc[2], w->rc[3]);
        DWORD e = GetLastError();
        xn_limit_restore(route, old);
        CHECKF(r == w->ret, "route %d result %d want %d", route, r, w->ret);
        if (!w->ret) CHECKF(e == (DWORD)w->err, "route %d error %lu want %d", route, e, w->err);
        RECT cb; int g = U(GetClipBox)(f.dc, &cb);
        CHECKF(g == w->cbret && box_is(&cb, w->cb[0], w->cb[1], w->cb[2], w->cb[3]), "route %d GetClipBox %d (%ld,%ld,%ld,%ld) want %d (%d,%d,%d,%d)", route, g, cb.left, cb.top, cb.right, cb.bottom, w->cbret, w->cb[0], w->cb[1], w->cb[2], w->cb[3]);
        HRGN t = U(CreateRectRgn)(-7, -7, -3, -3);
        int rr = UN(NtGdiGetRandomRgn)(f.dc, t, GRR_CLIP), c = -1; RECT bx = { 0, 0, 0, 0 };
        if (rr == 1) c = kbox(t, &bx);
        U(DeleteObject)(t);
        int got_ret = rr == 1 ? c : -10 - rr;
        CHECKF(got_ret == w->rrret && box_is(&bx, w->rr[0], w->rr[1], w->rr[2], w->rr[3]), "route %d clip region %d (%ld,%ld,%ld,%ld) want %d (%d,%d,%d,%d)", route, got_ret, bx.left, bx.top, bx.right, bx.bottom, w->rrret, w->rr[0], w->rr[1], w->rr[2], w->rr[3]);
    }
    fx_close(&f);
}

static const struct crow r_clip_intersect_none_zero = { 0, 0, {0,0,0,0}, 3, 0, 1, {0,0,0,0}, 1, {0,0,0,0} };
XT(clip_intersect_none_zero) { run_row(&r_clip_intersect_none_zero); }

static const struct crow r_clip_intersect_none_inverted_origin = { 0, 0, {10,10,0,0}, 3, 0, 2, {0,0,10,10}, 2, {0,0,10,10} };
XT(clip_intersect_none_inverted_origin) { run_row(&r_clip_intersect_none_inverted_origin); }

static const struct crow r_clip_intersect_none_overlap = { 0, 0, {5,5,20,20}, 3, 0, 2, {5,5,20,20}, 2, {5,5,20,20} };
XT(clip_intersect_none_overlap) { run_row(&r_clip_intersect_none_overlap); }

static const struct crow r_clip_intersect_none_outside = { 0, 0, {100,100,200,200}, 3, 0, 1, {0,0,0,0}, 2, {100,100,200,200} };
XT(clip_intersect_none_outside) { run_row(&r_clip_intersect_none_outside); }

static const struct crow r_clip_intersect_none_full_surface = { 0, 0, {0,0,64,48}, 3, 0, 2, {0,0,64,48}, 2, {0,0,64,48} };
XT(clip_intersect_none_full_surface) { run_row(&r_clip_intersect_none_full_surface); }

static const struct crow r_clip_intersect_none_max_range = { 0, 0, {-134217728,-134217728,134217727,134217727}, 3, 0, 2, {0,0,64,48}, 2, {-134217728,-134217728,134217727,134217727} };
XT(clip_intersect_none_max_range) { run_row(&r_clip_intersect_none_max_range); }

static const struct crow r_clip_intersect_none_int_max = { 0, 0, {0,0,2147483647,2147483647}, 0, 87, 2, {0,0,64,48}, -10, {0,0,0,0} };
XT(clip_intersect_none_int_max) { run_row(&r_clip_intersect_none_int_max); }

static const struct crow r_clip_intersect_none_over_range = { 0, 0, {0,0,134217728,10}, 0, 87, 2, {0,0,64,48}, -10, {0,0,0,0} };
XT(clip_intersect_none_over_range) { run_row(&r_clip_intersect_none_over_range); }

static const struct crow r_clip_intersect_none_inverted_overlap = { 0, 0, {20,20,5,5}, 3, 0, 2, {5,5,20,20}, 2, {5,5,20,20} };
XT(clip_intersect_none_inverted_overlap) { run_row(&r_clip_intersect_none_inverted_overlap); }

static const struct crow r_clip_intersect_none_same_as_clip = { 0, 0, {10,10,40,30}, 3, 0, 2, {10,10,40,30}, 2, {10,10,40,30} };
XT(clip_intersect_none_same_as_clip) { run_row(&r_clip_intersect_none_same_as_clip); }

static const struct crow r_clip_intersect_rect_zero = { 1, 0, {0,0,0,0}, 1, 0, 1, {0,0,0,0}, 1, {0,0,0,0} };
XT(clip_intersect_rect_zero) { run_row(&r_clip_intersect_rect_zero); }

static const struct crow r_clip_intersect_rect_inverted_origin = { 1, 0, {10,10,0,0}, 3, 0, 1, {0,0,0,0}, 1, {0,0,0,0} };
XT(clip_intersect_rect_inverted_origin) { run_row(&r_clip_intersect_rect_inverted_origin); }

static const struct crow r_clip_intersect_rect_overlap = { 1, 0, {5,5,20,20}, 3, 0, 2, {10,10,20,20}, 2, {10,10,20,20} };
XT(clip_intersect_rect_overlap) { run_row(&r_clip_intersect_rect_overlap); }

static const struct crow r_clip_intersect_rect_outside = { 1, 0, {100,100,200,200}, 3, 0, 1, {0,0,0,0}, 1, {0,0,0,0} };
XT(clip_intersect_rect_outside) { run_row(&r_clip_intersect_rect_outside); }

static const struct crow r_clip_intersect_rect_full_surface = { 1, 0, {0,0,64,48}, 3, 0, 2, {10,10,40,30}, 2, {10,10,40,30} };
XT(clip_intersect_rect_full_surface) { run_row(&r_clip_intersect_rect_full_surface); }

static const struct crow r_clip_intersect_rect_max_range = { 1, 0, {-134217728,-134217728,134217727,134217727}, 3, 0, 2, {10,10,40,30}, 2, {10,10,40,30} };
XT(clip_intersect_rect_max_range) { run_row(&r_clip_intersect_rect_max_range); }

static const struct crow r_clip_intersect_rect_int_max = { 1, 0, {0,0,2147483647,2147483647}, 0, 87, 2, {10,10,40,30}, 2, {10,10,40,30} };
XT(clip_intersect_rect_int_max) { run_row(&r_clip_intersect_rect_int_max); }

static const struct crow r_clip_intersect_rect_over_range = { 1, 0, {0,0,134217728,10}, 0, 87, 2, {10,10,40,30}, 2, {10,10,40,30} };
XT(clip_intersect_rect_over_range) { run_row(&r_clip_intersect_rect_over_range); }

static const struct crow r_clip_intersect_rect_inverted_overlap = { 1, 0, {20,20,5,5}, 3, 0, 2, {10,10,20,20}, 2, {10,10,20,20} };
XT(clip_intersect_rect_inverted_overlap) { run_row(&r_clip_intersect_rect_inverted_overlap); }

static const struct crow r_clip_intersect_rect_same_as_clip = { 1, 0, {10,10,40,30}, 3, 0, 2, {10,10,40,30}, 2, {10,10,40,30} };
XT(clip_intersect_rect_same_as_clip) { run_row(&r_clip_intersect_rect_same_as_clip); }

static const struct crow r_clip_intersect_complex_zero = { 2, 0, {0,0,0,0}, 1, 0, 1, {0,0,0,0}, 1, {0,0,0,0} };
XT(clip_intersect_complex_zero) { run_row(&r_clip_intersect_complex_zero); }

static const struct crow r_clip_intersect_complex_inverted_origin = { 2, 0, {10,10,0,0}, 1, 0, 1, {0,0,0,0}, 1, {0,0,0,0} };
XT(clip_intersect_complex_inverted_origin) { run_row(&r_clip_intersect_complex_inverted_origin); }

static const struct crow r_clip_intersect_complex_overlap = { 2, 0, {5,5,20,20}, 3, 0, 2, {10,10,20,20}, 2, {10,10,20,20} };
XT(clip_intersect_complex_overlap) { run_row(&r_clip_intersect_complex_overlap); }

static const struct crow r_clip_intersect_complex_outside = { 2, 0, {100,100,200,200}, 1, 0, 1, {0,0,0,0}, 1, {0,0,0,0} };
XT(clip_intersect_complex_outside) { run_row(&r_clip_intersect_complex_outside); }

static const struct crow r_clip_intersect_complex_full_surface = { 2, 0, {0,0,64,48}, 3, 0, 3, {10,10,55,45}, 3, {10,10,55,45} };
XT(clip_intersect_complex_full_surface) { run_row(&r_clip_intersect_complex_full_surface); }

static const struct crow r_clip_intersect_complex_max_range = { 2, 0, {-134217728,-134217728,134217727,134217727}, 3, 0, 3, {10,10,55,45}, 3, {10,10,55,45} };
XT(clip_intersect_complex_max_range) { run_row(&r_clip_intersect_complex_max_range); }

static const struct crow r_clip_intersect_complex_int_max = { 2, 0, {0,0,2147483647,2147483647}, 0, 87, 3, {10,10,55,45}, 3, {10,10,55,45} };
XT(clip_intersect_complex_int_max) { run_row(&r_clip_intersect_complex_int_max); }

static const struct crow r_clip_intersect_complex_over_range = { 2, 0, {0,0,134217728,10}, 0, 87, 3, {10,10,55,45}, 3, {10,10,55,45} };
XT(clip_intersect_complex_over_range) { run_row(&r_clip_intersect_complex_over_range); }

static const struct crow r_clip_intersect_complex_inverted_overlap = { 2, 0, {20,20,5,5}, 3, 0, 2, {10,10,20,20}, 2, {10,10,20,20} };
XT(clip_intersect_complex_inverted_overlap) { run_row(&r_clip_intersect_complex_inverted_overlap); }

static const struct crow r_clip_intersect_complex_same_as_clip = { 2, 0, {10,10,40,30}, 3, 0, 2, {10,10,40,30}, 2, {10,10,40,30} };
XT(clip_intersect_complex_same_as_clip) { run_row(&r_clip_intersect_complex_same_as_clip); }

static const struct crow r_clip_intersect_empty_zero = { 3, 0, {0,0,0,0}, 1, 0, 1, {0,0,0,0}, 1, {0,0,0,0} };
XT(clip_intersect_empty_zero) { run_row(&r_clip_intersect_empty_zero); }

static const struct crow r_clip_intersect_empty_overlap = { 3, 0, {5,5,20,20}, 1, 0, 1, {0,0,0,0}, 1, {0,0,0,0} };
XT(clip_intersect_empty_overlap) { run_row(&r_clip_intersect_empty_overlap); }

static const struct crow r_clip_intersect_empty_full_surface = { 3, 0, {0,0,64,48}, 1, 0, 1, {0,0,0,0}, 1, {0,0,0,0} };
XT(clip_intersect_empty_full_surface) { run_row(&r_clip_intersect_empty_full_surface); }

static const struct crow r_clip_intersect_empty_int_max = { 3, 0, {0,0,2147483647,2147483647}, 0, 87, 1, {0,0,0,0}, 1, {0,0,0,0} };
XT(clip_intersect_empty_int_max) { run_row(&r_clip_intersect_empty_int_max); }

static const struct crow r_clip_exclude_none_zero = { 0, 1, {0,0,0,0}, 3, 0, 2, {0,0,64,48}, 2, {0,0,64,48} };
XT(clip_exclude_none_zero) { run_row(&r_clip_exclude_none_zero); }

static const struct crow r_clip_exclude_none_inverted_origin = { 0, 1, {10,10,0,0}, 3, 0, 3, {0,0,64,48}, 3, {0,0,64,48} };
XT(clip_exclude_none_inverted_origin) { run_row(&r_clip_exclude_none_inverted_origin); }

static const struct crow r_clip_exclude_none_overlap = { 0, 1, {5,5,20,20}, 3, 0, 3, {0,0,64,48}, 3, {0,0,64,48} };
XT(clip_exclude_none_overlap) { run_row(&r_clip_exclude_none_overlap); }

static const struct crow r_clip_exclude_none_outside = { 0, 1, {100,100,200,200}, 3, 0, 2, {0,0,64,48}, 2, {0,0,64,48} };
XT(clip_exclude_none_outside) { run_row(&r_clip_exclude_none_outside); }

static const struct crow r_clip_exclude_none_full_surface = { 0, 1, {0,0,64,48}, 1, 0, 1, {0,0,0,0}, 1, {0,0,0,0} };
XT(clip_exclude_none_full_surface) { run_row(&r_clip_exclude_none_full_surface); }

static const struct crow r_clip_exclude_none_max_range = { 0, 1, {-134217728,-134217728,134217727,134217727}, 1, 0, 1, {0,0,0,0}, 1, {0,0,0,0} };
XT(clip_exclude_none_max_range) { run_row(&r_clip_exclude_none_max_range); }

static const struct crow r_clip_exclude_none_int_max = { 0, 1, {0,0,2147483647,2147483647}, 0, 87, 2, {0,0,64,48}, -10, {0,0,0,0} };
XT(clip_exclude_none_int_max) { run_row(&r_clip_exclude_none_int_max); }

static const struct crow r_clip_exclude_none_over_range = { 0, 1, {0,0,134217728,10}, 0, 87, 2, {0,0,64,48}, -10, {0,0,0,0} };
XT(clip_exclude_none_over_range) { run_row(&r_clip_exclude_none_over_range); }

static const struct crow r_clip_exclude_none_inverted_overlap = { 0, 1, {20,20,5,5}, 3, 0, 3, {0,0,64,48}, 3, {0,0,64,48} };
XT(clip_exclude_none_inverted_overlap) { run_row(&r_clip_exclude_none_inverted_overlap); }

static const struct crow r_clip_exclude_none_same_as_clip = { 0, 1, {10,10,40,30}, 3, 0, 3, {0,0,64,48}, 3, {0,0,64,48} };
XT(clip_exclude_none_same_as_clip) { run_row(&r_clip_exclude_none_same_as_clip); }

static const struct crow r_clip_exclude_rect_zero = { 1, 1, {0,0,0,0}, 3, 0, 2, {10,10,40,30}, 2, {10,10,40,30} };
XT(clip_exclude_rect_zero) { run_row(&r_clip_exclude_rect_zero); }

static const struct crow r_clip_exclude_rect_inverted_origin = { 1, 1, {10,10,0,0}, 3, 0, 2, {10,10,40,30}, 2, {10,10,40,30} };
XT(clip_exclude_rect_inverted_origin) { run_row(&r_clip_exclude_rect_inverted_origin); }

static const struct crow r_clip_exclude_rect_overlap = { 1, 1, {5,5,20,20}, 3, 0, 3, {10,10,40,30}, 3, {10,10,40,30} };
XT(clip_exclude_rect_overlap) { run_row(&r_clip_exclude_rect_overlap); }

static const struct crow r_clip_exclude_rect_outside = { 1, 1, {100,100,200,200}, 3, 0, 2, {10,10,40,30}, 2, {10,10,40,30} };
XT(clip_exclude_rect_outside) { run_row(&r_clip_exclude_rect_outside); }

static const struct crow r_clip_exclude_rect_full_surface = { 1, 1, {0,0,64,48}, 1, 0, 1, {0,0,0,0}, 1, {0,0,0,0} };
XT(clip_exclude_rect_full_surface) { run_row(&r_clip_exclude_rect_full_surface); }

static const struct crow r_clip_exclude_rect_max_range = { 1, 1, {-134217728,-134217728,134217727,134217727}, 1, 0, 1, {0,0,0,0}, 1, {0,0,0,0} };
XT(clip_exclude_rect_max_range) { run_row(&r_clip_exclude_rect_max_range); }

static const struct crow r_clip_exclude_rect_int_max = { 1, 1, {0,0,2147483647,2147483647}, 0, 87, 2, {10,10,40,30}, 2, {10,10,40,30} };
XT(clip_exclude_rect_int_max) { run_row(&r_clip_exclude_rect_int_max); }

static const struct crow r_clip_exclude_rect_over_range = { 1, 1, {0,0,134217728,10}, 0, 87, 2, {10,10,40,30}, 2, {10,10,40,30} };
XT(clip_exclude_rect_over_range) { run_row(&r_clip_exclude_rect_over_range); }

static const struct crow r_clip_exclude_rect_inverted_overlap = { 1, 1, {20,20,5,5}, 3, 0, 3, {10,10,40,30}, 3, {10,10,40,30} };
XT(clip_exclude_rect_inverted_overlap) { run_row(&r_clip_exclude_rect_inverted_overlap); }

static const struct crow r_clip_exclude_rect_same_as_clip = { 1, 1, {10,10,40,30}, 1, 0, 1, {0,0,0,0}, 1, {0,0,0,0} };
XT(clip_exclude_rect_same_as_clip) { run_row(&r_clip_exclude_rect_same_as_clip); }

static const struct crow r_clip_exclude_complex_zero = { 2, 1, {0,0,0,0}, 3, 0, 3, {10,10,55,45}, 3, {10,10,55,45} };
XT(clip_exclude_complex_zero) { run_row(&r_clip_exclude_complex_zero); }

static const struct crow r_clip_exclude_complex_inverted_origin = { 2, 1, {10,10,0,0}, 3, 0, 3, {10,10,55,45}, 3, {10,10,55,45} };
XT(clip_exclude_complex_inverted_origin) { run_row(&r_clip_exclude_complex_inverted_origin); }

static const struct crow r_clip_exclude_complex_overlap = { 2, 1, {5,5,20,20}, 3, 0, 3, {10,10,55,45}, 3, {10,10,55,45} };
XT(clip_exclude_complex_overlap) { run_row(&r_clip_exclude_complex_overlap); }

static const struct crow r_clip_exclude_complex_outside = { 2, 1, {100,100,200,200}, 3, 0, 3, {10,10,55,45}, 3, {10,10,55,45} };
XT(clip_exclude_complex_outside) { run_row(&r_clip_exclude_complex_outside); }

static const struct crow r_clip_exclude_complex_full_surface = { 2, 1, {0,0,64,48}, 1, 0, 1, {0,0,0,0}, 1, {0,0,0,0} };
XT(clip_exclude_complex_full_surface) { run_row(&r_clip_exclude_complex_full_surface); }

static const struct crow r_clip_exclude_complex_max_range = { 2, 1, {-134217728,-134217728,134217727,134217727}, 1, 0, 1, {0,0,0,0}, 1, {0,0,0,0} };
XT(clip_exclude_complex_max_range) { run_row(&r_clip_exclude_complex_max_range); }

static const struct crow r_clip_exclude_complex_int_max = { 2, 1, {0,0,2147483647,2147483647}, 0, 87, 3, {10,10,55,45}, 3, {10,10,55,45} };
XT(clip_exclude_complex_int_max) { run_row(&r_clip_exclude_complex_int_max); }

static const struct crow r_clip_exclude_complex_over_range = { 2, 1, {0,0,134217728,10}, 0, 87, 3, {10,10,55,45}, 3, {10,10,55,45} };
XT(clip_exclude_complex_over_range) { run_row(&r_clip_exclude_complex_over_range); }

static const struct crow r_clip_exclude_complex_inverted_overlap = { 2, 1, {20,20,5,5}, 3, 0, 3, {10,10,55,45}, 3, {10,10,55,45} };
XT(clip_exclude_complex_inverted_overlap) { run_row(&r_clip_exclude_complex_inverted_overlap); }

static const struct crow r_clip_exclude_complex_same_as_clip = { 2, 1, {10,10,40,30}, 3, 0, 3, {30,25,55,45}, 3, {30,25,55,45} };
XT(clip_exclude_complex_same_as_clip) { run_row(&r_clip_exclude_complex_same_as_clip); }

static const struct crow r_clip_exclude_empty_zero = { 3, 1, {0,0,0,0}, 1, 0, 1, {0,0,0,0}, 1, {0,0,0,0} };
XT(clip_exclude_empty_zero) { run_row(&r_clip_exclude_empty_zero); }

static const struct crow r_clip_exclude_empty_overlap = { 3, 1, {5,5,20,20}, 1, 0, 1, {0,0,0,0}, 1, {0,0,0,0} };
XT(clip_exclude_empty_overlap) { run_row(&r_clip_exclude_empty_overlap); }

static const struct crow r_clip_exclude_empty_full_surface = { 3, 1, {0,0,64,48}, 1, 0, 1, {0,0,0,0}, 1, {0,0,0,0} };
XT(clip_exclude_empty_full_surface) { run_row(&r_clip_exclude_empty_full_surface); }

static const struct crow r_clip_exclude_empty_int_max = { 3, 1, {0,0,2147483647,2147483647}, 0, 87, 1, {0,0,0,0}, 1, {0,0,0,0} };
XT(clip_exclude_empty_int_max) { run_row(&r_clip_exclude_empty_int_max); }
