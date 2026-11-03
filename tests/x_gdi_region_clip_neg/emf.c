/* Metafile and information DCs against the thin clip, bounds, extent and MoveTo calls. On an enhanced-metafile DC the
 * clip calls return COMPLEXREGION for a non-empty rectangle on a fresh DC and NULLREGION for an empty one or once the
 * same rectangle is excluded again; the position and extent calls succeed and track state, and GetBoundsRect reports
 * an empty rectangle. */
#include "xn.h"

#define NEED_E() do { if (!U(CreateEnhMetaFileW) || !U(CloseEnhMetaFile) || !U(DeleteEnhMetaFile) || !U(IntersectClipRect) || !U(ExcludeClipRect) || !UN(NtGdiIntersectClipRect) || \
    !UN(NtGdiExcludeClipRect) || !U(GetBoundsRect) || !U(ScaleWindowExtEx) || !U(MoveToEx) || !UN(NtGdiMoveTo) || !U(GetWindowExtEx)) { t_skip("exports missing"); return; } } while (0)
static void emf_done(HDC mf) { HENHMETAFILE e = U(CloseEnhMetaFile)(mf); if (e) U(DeleteEnhMetaFile)(e); }

XT(emf_intersect_returns_complex) { NEED_E(); HDC mf = U(CreateEnhMetaFileW)(NULL, NULL, NULL, NULL); EXPECT_EQ(U(IntersectClipRect)(mf, 0, 0, 10, 10), RC_COMPLEX); emf_done(mf); }
XT(emf_exclude_on_fresh_dc_returns_complex) { NEED_E(); HDC mf = U(CreateEnhMetaFileW)(NULL, NULL, NULL, NULL); EXPECT_EQ(U(ExcludeClipRect)(mf, 0, 0, 10, 10), RC_COMPLEX); emf_done(mf); }
XT(emf_exclude_after_intersect_of_same_rect_returns_null) {
    NEED_E(); HDC mf = U(CreateEnhMetaFileW)(NULL, NULL, NULL, NULL);
    EXPECT_EQ(U(IntersectClipRect)(mf, 0, 0, 10, 10), RC_COMPLEX);
    EXPECT_EQ(U(ExcludeClipRect)(mf, 0, 0, 10, 10), RC_NULL);
    emf_done(mf);
}
XT(emf_intersect_empty_rect_returns_null) { NEED_E(); HDC mf = U(CreateEnhMetaFileW)(NULL, NULL, NULL, NULL); EXPECT_EQ(U(IntersectClipRect)(mf, 0, 0, 0, 0), RC_NULL); emf_done(mf); }
XT(emf_intersect_inverted_rect_returns_complex) { NEED_E(); HDC mf = U(CreateEnhMetaFileW)(NULL, NULL, NULL, NULL); EXPECT_EQ(U(IntersectClipRect)(mf, 10, 10, 0, 0), RC_COMPLEX); emf_done(mf); }
XT(emf_nt_intersect_returns_complex) { NEED_E(); HDC mf = U(CreateEnhMetaFileW)(NULL, NULL, NULL, NULL); EXPECT_EQ(UN(NtGdiIntersectClipRect)(mf, 0, 0, 10, 10), RC_COMPLEX); emf_done(mf); }
XT(emf_bounds_empty) { NEED_E(); HDC mf = U(CreateEnhMetaFileW)(NULL, NULL, NULL, NULL); RECT r; EXPECT_EQ(U(GetBoundsRect)(mf, &r, 0), 1); emf_done(mf); }
XT(emf_scale_window_ext_succeeds_and_reports_extent) {
    NEED_E(); HDC mf = U(CreateEnhMetaFileW)(NULL, NULL, NULL, NULL); SIZE sz = { 7, 7 };
    CHECK(U(ScaleWindowExtEx)(mf, 1, 2, 3, 4, &sz)); CHECK(sz.cx == 1 && sz.cy == 1);
    emf_done(mf);
}
XT(emf_moveto_tracks_old_point) {
    NEED_E(); HDC mf = U(CreateEnhMetaFileW)(NULL, NULL, NULL, NULL); POINT p = { 7, 7 };
    CHECK(U(MoveToEx)(mf, 3, 4, &p)); CHECK(p.x == 0 && p.y == 0);
    CHECK(U(MoveToEx)(mf, 5, 6, &p)); CHECK(p.x == 3 && p.y == 4);
    CHECK(UN(NtGdiMoveTo)(mf, 8, 9, &p)); CHECK(p.x == 5 && p.y == 6);
    emf_done(mf);
}
XT(info_dc_clip_calls) {
    NEED_E(); if (!U(CreateICW) || !U(DeleteDC)) { t_skip("exports missing"); return; }
    HDC ic = U(CreateICW)(L"DISPLAY", NULL, NULL, NULL); if (!ic) { t_skip("no information DC"); return; }
    RECT r;
    EXPECT_EQ(U(IntersectClipRect)(ic, 0, 0, 10, 10), RC_COMPLEX);
    EXPECT_EQ(U(GetBoundsRect)(ic, &r, 0), 1);
    U(DeleteDC)(ic);
}
