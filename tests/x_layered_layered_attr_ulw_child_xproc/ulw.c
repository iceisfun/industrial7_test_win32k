/* UpdateLayeredWindow argument validation, measured through the user32 front doors UpdateLayeredWindow and
 * UpdateLayeredWindowIndirect and the win32u service they end in. Each case runs on a freshly created, hidden,
 * layered popup so that no earlier call can influence the outcome. Measures: which dwFlags values are accepted
 * (only the bits ULW_COLORKEY, ULW_ALPHA, ULW_OPAQUE and one further accepted bit; 8 and the high bits are
 * rejected), the order handle-check-before-flag-check, the effect of missing pointer arguments, negative or empty
 * sizes, negative dirty rectangles, the UPDATELAYEREDWINDOWINFO size field, bad handles, and the fact that a
 * successful or failed call leaves the Set/Get attribute state alone. Also the position/size effect of a call. */
#include "xl.h"
volatile LONG xl_msgs[XM_N];

#define FRESH(w, im) XL_NEED(); struct xl_img im; if (!xl_img_make(&im, 8, 8)) { t_skip("cannot build image DC"); return; } \
    HWND w = xl_lay(); if (!w) { xl_img_free(&im); t_skip("cannot create layered window"); return; }
#define DONE(w, im) do { xl_kill(w); xl_img_free(&im); } while (0)

/* ---- dwFlags --------------------------------------------------------------------------------------- */
#define FLAG_CASE(nm, fl, ok) \
    TEST(ulw_flags_##nm) { FRESH(w, im); SET_ERR(XL_ERR_SENT); BOOL r = xl_ulw(w, &im, (fl)); \
        if (ok) { CHECK(r); EXPECT_EQ(GetLastError(), XL_ERR_SENT); } else { EXPECT_FAIL(!r, ERROR_INVALID_PARAMETER); } \
        struct xl_attr a; xl_attr_clear(&a); SET_ERR(XL_ERR_SENT); \
        CHECK(!xl_getall(w, &a));            /* an image driven window has no Set attributes in any case */ \
        EXPECT_EQ(a.flags, 0xfeedf00d); DONE(w, im); }
FLAG_CASE(0x00, 0x00, 1) FLAG_CASE(colorkey, ULW_COLORKEY, 1) FLAG_CASE(alpha, ULW_ALPHA, 1) FLAG_CASE(ck_alpha, 3, 1)
FLAG_CASE(opaque, ULW_OPAQUE, 1) FLAG_CASE(opaque_ck, 5, 1) FLAG_CASE(opaque_alpha, 6, 1) FLAG_CASE(all_three, 7, 1)
FLAG_CASE(bit3, 0x08, 0) FLAG_CASE(bit3_ck, 0x09, 0) FLAG_CASE(bit3_alpha, 0x0a, 0) FLAG_CASE(low_nibble, 0x0f, 0)
FLAG_CASE(bit4, 0x10, 0) FLAG_CASE(bit4_ck, 0x11, 0) FLAG_CASE(bit5, 0x20, 1) FLAG_CASE(bit5_alpha, 0x22, 1)
FLAG_CASE(bit6, 0x40, 0) FLAG_CASE(bit7, 0x80, 0) FLAG_CASE(bit8, 0x100, 0) FLAG_CASE(bit31, 0x80000000u, 0)
FLAG_CASE(mask_ffffffd0, 0xffffffd0u, 0) FLAG_CASE(all_ones, 0xffffffffu, 0)

/* ---- argument combinations (flags passed along with the pointers) -------------------------------------- */
struct uc { const char *name; int dc, src, size, blend; DWORD flags; BOOL ok; DWORD err; };
/* dc/src/size/blend: 1 = pass a valid argument, 0 = pass NULL.  src applies to both hdcSrc and pptSrc below. */
static BOOL ulw_run(const struct uc *c, HWND w, struct xl_img *im, int hdcsrc, int pptsrc) {
    POINT s = { 0, 0 }; SIZE sz = { 8, 8 }; BLENDFUNCTION b = xl_blend(255);
    return U(UpdateLayeredWindow)(w, NULL, NULL, c->size ? &sz : NULL, hdcsrc ? im->dc : NULL, pptsrc ? &s : NULL, 0, c->blend ? &b : NULL, c->flags);
}
#define ARG_CASE(nm, hs, ps, size, blend, fl, okv, errv) \
    TEST(ulw_args_##nm) { FRESH(w, im); struct uc c = { #nm, 0, 0, size, blend, fl, okv, errv }; SET_ERR(XL_ERR_SENT); \
        BOOL r = ulw_run(&c, w, &im, hs, ps); \
        if (okv) { CHECK(r); EXPECT_EQ(GetLastError(), XL_ERR_SENT); } else { EXPECT_FAIL(!r, errv); } DONE(w, im); }
ARG_CASE(all_given_alpha,        1, 1, 1, 1, ULW_ALPHA, 1, 0)
ARG_CASE(blend_null_alpha,       1, 1, 1, 0, ULW_ALPHA, 0, ERROR_GEN_FAILURE)
ARG_CASE(blend_null_flags0,      1, 1, 1, 0, 0,         1, 0)
ARG_CASE(blend_null_opaque,      1, 1, 1, 0, ULW_OPAQUE, 1, 0)
ARG_CASE(src_point_null_alpha,   1, 0, 1, 1, ULW_ALPHA, 1, 0)
ARG_CASE(src_point_null_flags0,  1, 0, 1, 1, 0,         1, 0)
ARG_CASE(src_dc_null_point_given, 0, 1, 1, 1, ULW_ALPHA, 0, ERROR_GEN_FAILURE)
ARG_CASE(src_dc_null_point_null, 0, 0, 1, 1, ULW_ALPHA, 1, 0)
ARG_CASE(src_dc_null_flags0,     0, 0, 1, 0, 0,         1, 0)
ARG_CASE(nothing_flags0,         0, 0, 0, 0, 0,         1, 0)
ARG_CASE(nothing_alpha,          0, 0, 0, 0, ULW_ALPHA, 0, ERROR_GEN_FAILURE)
ARG_CASE(size_null_image_given,  1, 1, 0, 1, ULW_ALPHA, 1, 0)

/* ---- size ------------------------------------------------------------------------------------------- */
#define SIZE_CASE(nm, cx, cy, okv, errv) \
    TEST(ulw_size_##nm) { FRESH(w, im); POINT s = { 0, 0 }; SIZE sz = { cx, cy }; BLENDFUNCTION b = xl_blend(255); SET_ERR(XL_ERR_SENT); \
        BOOL r = U(UpdateLayeredWindow)(w, NULL, NULL, &sz, im.dc, &s, 0, &b, ULW_ALPHA); \
        if (okv) { CHECK(r); } else { EXPECT_FAIL(!r, errv); } DONE(w, im); }
SIZE_CASE(neg_cx, -1, 8, 0, ERROR_INVALID_PARAMETER) SIZE_CASE(neg_cy, 8, -1, 0, ERROR_INVALID_PARAMETER)
SIZE_CASE(neg_both, -1, -1, 0, ERROR_INVALID_PARAMETER) SIZE_CASE(zero_both, 0, 0, 0, ERROR_GEN_FAILURE)
SIZE_CASE(zero_cx, 0, 8, 0, ERROR_GEN_FAILURE) SIZE_CASE(zero_cy, 8, 0, 0, ERROR_GEN_FAILURE) SIZE_CASE(one_by_one, 1, 1, 1, 0)

/* ---- blend function contents (accepted as given for a plain image) -------------------------------------- */
TEST(ulw_blend_contents_accepted) {
    FRESH(w, im); (void)w; xl_kill(w); xl_img_free(&im);
    static const BLENDFUNCTION bb[] = { {1,0,255,0}, {0,1,255,0}, {0,0,255,1}, {0,0,255,2}, {0,0,0,0}, {0,0x80,255,0} };
    for (unsigned i = 0; i < sizeof bb / sizeof bb[0]; i++) {
        struct xl_img im2; if (!xl_img_make(&im2, 8, 8)) { t_skip("image"); return; }
        HWND w2 = xl_lay(); POINT s = { 0, 0 }; SIZE sz = { 8, 8 };
        SET_ERR(XL_ERR_SENT); BOOL r = U(UpdateLayeredWindow)(w2, NULL, NULL, &sz, im2.dc, &s, 0, (BLENDFUNCTION *)&bb[i], ULW_ALPHA);
        CHECKF(r, "blend row %u", i); xl_kill(w2); xl_img_free(&im2);
    }
}

/* ---- device contexts ------------------------------------------------------------------------------------ */
TEST(ulw_dst_dc_screen_and_memory) {
    FRESH(w, im); HDC scr = U(GetDC)(NULL); POINT s = { 0, 0 }; SIZE sz = { 8, 8 }; BLENDFUNCTION b = xl_blend(255);
    CHECK(U(UpdateLayeredWindow)(w, scr, NULL, &sz, im.dc, &s, 0, &b, ULW_ALPHA));
    CHECK(U(UpdateLayeredWindow)(w, im.dc, NULL, &sz, im.dc, &s, 0, &b, ULW_ALPHA));
    U(ReleaseDC)(NULL, scr); DONE(w, im);
}
TEST(ulw_dst_dc_garbage_handle_ignored) {
    FRESH(w, im); POINT s = { 0, 0 }; SIZE sz = { 8, 8 }; BLENDFUNCTION b = xl_blend(255);
    CHECK(U(UpdateLayeredWindow)(w, (HDC)(ULONG_PTR)0x1234, NULL, &sz, im.dc, &s, 0, &b, ULW_ALPHA)); DONE(w, im);
}
TEST(ulw_src_dc_screen_accepted) {
    FRESH(w, im); HDC scr = U(GetDC)(NULL); POINT s = { 0, 0 }; SIZE sz = { 8, 8 }; BLENDFUNCTION b = xl_blend(255);
    CHECK(U(UpdateLayeredWindow)(w, NULL, NULL, &sz, scr, &s, 0, &b, ULW_ALPHA)); U(ReleaseDC)(NULL, scr); DONE(w, im);
}
TEST(ulw_src_dc_garbage_handle_fails) {
    FRESH(w, im); POINT s = { 0, 0 }; SIZE sz = { 8, 8 }; BLENDFUNCTION b = xl_blend(255); SET_ERR(XL_ERR_SENT);
    EXPECT_FAIL(!U(UpdateLayeredWindow)(w, NULL, NULL, &sz, (HDC)(ULONG_PTR)0x1234, &s, 0, &b, ULW_ALPHA), ERROR_GEN_FAILURE); DONE(w, im);
}

/* ---- dirty rectangle (UpdateLayeredWindowIndirect and the service) ------------------------------------------- */
#define DIRTY_CASE(nm, l, t, r_, b_, okv) \
    TEST(ulw_dirty_##nm) { FRESH(w, im); RECT rc = { l, t, r_, b_ }; POINT s = { 0, 0 }; SIZE sz = { 8, 8 }; BLENDFUNCTION b = xl_blend(255); \
        UPDATELAYEREDWINDOWINFO u; memset(&u, 0, sizeof u); u.cbSize = sizeof u; u.hdcSrc = im.dc; u.pptSrc = &s; u.psize = &sz; u.pblend = &b; u.dwFlags = ULW_ALPHA; u.prcDirty = &rc; \
        SET_ERR(XL_ERR_SENT); BOOL r1 = U(UpdateLayeredWindowIndirect)(w, &u); DWORD e1 = GetLastError(); \
        xl_kill(w); w = xl_lay(); SET_ERR(XL_ERR_SENT); BOOL r2 = UN(NtUserUpdateLayeredWindow)(w, NULL, NULL, &sz, im.dc, &s, 0, &b, ULW_ALPHA, &rc); DWORD e2 = GetLastError(); \
        if (okv) { CHECK(r1); CHECK(r2); EXPECT_EQ(e1, XL_ERR_SENT); EXPECT_EQ(e2, XL_ERR_SENT); } \
        else { CHECK(!r1); CHECK(!r2); EXPECT_EQ(e1, ERROR_INVALID_PARAMETER); EXPECT_EQ(e2, ERROR_INVALID_PARAMETER); } DONE(w, im); }
DIRTY_CASE(inside, 0, 0, 4, 4, 1) DIRTY_CASE(neg_left, -1, 0, 4, 4, 0) DIRTY_CASE(neg_top, 0, -1, 4, 4, 0)
DIRTY_CASE(neg_right, 0, 0, -1, 4, 1) DIRTY_CASE(neg_bottom, 0, 0, 4, -1, 1) DIRTY_CASE(inverted, 4, 4, 0, 0, 1)
DIRTY_CASE(empty, 0, 0, 0, 0, 1) DIRTY_CASE(larger_than_image, 0, 0, 100, 100, 1)

TEST(ulw_indirect_cbsize_must_match) {
    FRESH(w, im); POINT s = { 0, 0 }; SIZE sz = { 8, 8 }; BLENDFUNCTION b = xl_blend(255);
    UPDATELAYEREDWINDOWINFO u; memset(&u, 0, sizeof u); u.hdcSrc = im.dc; u.pptSrc = &s; u.psize = &sz; u.pblend = &b; u.dwFlags = ULW_ALPHA;
    DWORD sizes[] = { 0, sizeof u + 8, sizeof u - 1, 1 };
    for (unsigned i = 0; i < 4; i++) { u.cbSize = sizes[i]; SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!U(UpdateLayeredWindowIndirect)(w, &u), ERROR_INVALID_PARAMETER); }
    u.cbSize = sizeof u; CHECK(U(UpdateLayeredWindowIndirect)(w, &u));
    DONE(w, im);
}
TEST(ulw_indirect_null_info) {
    FRESH(w, im); SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!U(UpdateLayeredWindowIndirect)(w, NULL), ERROR_INVALID_PARAMETER); DONE(w, im);
}

/* ---- window handle ------------------------------------------------------------------------------------------ */
TEST(ulw_nonlayered_window_87) {
    XL_NEED(); struct xl_img im; xl_img_make(&im, 8, 8); HWND w = xl_plain();
    SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_ulw(w, &im, ULW_ALPHA), ERROR_INVALID_PARAMETER);
    SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_ulw(w, &im, 0), ERROR_INVALID_PARAMETER);
    DONE(w, im);
}
TEST(ulw_nonlayered_with_bad_flags_still_87) {
    XL_NEED(); struct xl_img im; xl_img_make(&im, 8, 8); HWND w = xl_plain();
    SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_ulw(w, &im, 0x10), ERROR_INVALID_PARAMETER); DONE(w, im);
}
TEST(ulw_invalid_handle_1400) {
    XL_NEED(); struct xl_img im; xl_img_make(&im, 8, 8);
    SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_ulw(xl_bogus(), &im, ULW_ALPHA), ERROR_INVALID_WINDOW_HANDLE);
    SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_ulw(NULL, &im, ULW_ALPHA), ERROR_INVALID_WINDOW_HANDLE);
    xl_img_free(&im);
}
TEST(ulw_invalid_handle_checked_before_flags) {
    XL_NEED(); struct xl_img im; xl_img_make(&im, 8, 8);
    SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_ulw(xl_bogus(), &im, 0x10), ERROR_INVALID_WINDOW_HANDLE); xl_img_free(&im);
}
TEST(ulw_destroyed_window_1400) {
    FRESH(w, im); xl_kill(w); SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_ulw(w, &im, ULW_ALPHA), ERROR_INVALID_WINDOW_HANDLE); xl_img_free(&im);
}
TEST(ulw_desktop_window_87) {
    XL_NEED(); struct xl_img im; xl_img_make(&im, 8, 8); SET_ERR(XL_ERR_SENT);
    EXPECT_FAIL(!xl_ulw(U(GetDesktopWindow)(), &im, ULW_ALPHA), ERROR_INVALID_PARAMETER); xl_img_free(&im);
}

/* ---- position and size ---------------------------------------------------------------------------------------- */
TEST(ulw_moves_and_sizes_hidden_window) {
    FRESH(w, im); POINT d = { -30000, -30000 }, s = { 0, 0 }; SIZE sz = { 20, 12 }; BLENDFUNCTION b = xl_blend(255); RECT rc;
    CHECK(U(UpdateLayeredWindow)(w, NULL, &d, &sz, im.dc, &s, 0, &b, ULW_ALPHA));
    U(GetWindowRect)(w, &rc);
    EXPECT_EQ(rc.left, (ULONG_PTR)-30000); EXPECT_EQ(rc.top, (ULONG_PTR)-30000);
    EXPECT_EQ(rc.right - rc.left, 20); EXPECT_EQ(rc.bottom - rc.top, 12);
    DONE(w, im);
}
TEST(ulw_dst_null_keeps_position_resizes) {
    FRESH(w, im); POINT s = { 0, 0 }; SIZE sz = { 30, 31 }; BLENDFUNCTION b = xl_blend(255); RECT r0, r1;
    U(GetWindowRect)(w, &r0);
    CHECK(U(UpdateLayeredWindow)(w, NULL, NULL, &sz, im.dc, &s, 0, &b, ULW_ALPHA));
    U(GetWindowRect)(w, &r1);
    EXPECT_EQ(r1.left, r0.left); EXPECT_EQ(r1.top, r0.top);
    EXPECT_EQ(r1.right - r1.left, 30); EXPECT_EQ(r1.bottom - r1.top, 31);
    DONE(w, im);
}
TEST(ulw_move_only_without_image) {
    FRESH(w, im); POINT d = { -29000, -29001 }; RECT rc;
    CHECK(U(UpdateLayeredWindow)(w, NULL, &d, NULL, NULL, NULL, 0, NULL, 0));
    U(GetWindowRect)(w, &rc); EXPECT_EQ(rc.left, (ULONG_PTR)-29000); EXPECT_EQ(rc.top, (ULONG_PTR)-29001);
    EXPECT_EQ(rc.right - rc.left, 16); EXPECT_EQ(rc.bottom - rc.top, 16);   /* creation size kept */
    DONE(w, im);
}
TEST(ulw_sends_no_position_or_style_messages) {
    FRESH(w, im); k_pump(30); xl_msgs_reset();
    CHECK(xl_ulw(w, &im, ULW_ALPHA));
    EXPECT_EQ(xl_msgs[XM_STYLECHANGING] + xl_msgs[XM_STYLECHANGED], 0);
    EXPECT_EQ(xl_msgs[XM_POSCHANGING] + xl_msgs[XM_POSCHANGED], 0);
    DONE(w, im);
}

/* ---- ULW_EX_NORESIZE: only the UpdateLayeredWindowIndirect structure and the service itself accept it; the
 *      9-argument user32 entry point rejects the bit (see the flags table above). With the bit, a size different from the
 *      window's current size makes the call fail with a general failure (31). -------------------------------------------- */
static BOOL indirect(HWND w, struct xl_img *im, int cx, int cy, DWORD flags) {
    POINT s = { 0, 0 }; SIZE sz = { cx, cy }; BLENDFUNCTION b = xl_blend(255);
    UPDATELAYEREDWINDOWINFO u; memset(&u, 0, sizeof u); u.cbSize = sizeof u; u.hdcSrc = im->dc; u.pptSrc = &s; u.psize = &sz; u.pblend = &b; u.dwFlags = flags;
    return U(UpdateLayeredWindowIndirect)(w, &u);
}
TEST(ulw_noresize_indirect_same_size_ok) {
    FRESH(w, im); SET_ERR(XL_ERR_SENT); CHECK(indirect(w, &im, 16, 16, ULW_ALPHA | ULW_EX_NORESIZE)); EXPECT_EQ(GetLastError(), XL_ERR_SENT); DONE(w, im);
}
TEST(ulw_noresize_indirect_different_size_fails_31) {
    FRESH(w, im); RECT rc;
    SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!indirect(w, &im, 20, 16, ULW_ALPHA | ULW_EX_NORESIZE), ERROR_GEN_FAILURE);
    U(GetWindowRect)(w, &rc); EXPECT_EQ(rc.right - rc.left, 16);     /* window untouched */
    DONE(w, im);
}
TEST(ulw_noresize_absent_resizes) {
    FRESH(w, im); RECT rc; CHECK(indirect(w, &im, 20, 16, ULW_ALPHA)); U(GetWindowRect)(w, &rc); EXPECT_EQ(rc.right - rc.left, 20); EXPECT_EQ(rc.bottom - rc.top, 16);
    CHECK(indirect(w, &im, 20, 16, ULW_ALPHA | ULW_EX_NORESIZE));      /* now the size matches */
    SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!indirect(w, &im, 16, 16, ULW_ALPHA | ULW_EX_NORESIZE), ERROR_GEN_FAILURE);
    DONE(w, im);
}
TEST(ulw_noresize_service_same_size_ok) {
    FRESH(w, im); POINT s = { 0, 0 }; SIZE sz = { 16, 16 }; BLENDFUNCTION b = xl_blend(255);
    SET_ERR(XL_ERR_SENT); CHECK(UN(NtUserUpdateLayeredWindow)(w, NULL, NULL, &sz, im.dc, &s, 0, &b, ULW_ALPHA | ULW_EX_NORESIZE, NULL)); DONE(w, im);
}
TEST(ulw_noresize_user32_entry_rejects_bit) {
    FRESH(w, im); SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_ulw(w, &im, ULW_ALPHA | ULW_EX_NORESIZE), ERROR_INVALID_PARAMETER); DONE(w, im);
}
