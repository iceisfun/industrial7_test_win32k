/* Ownership of a layered window's content between SetLayeredWindowAttributes (attribute driven) and UpdateLayeredWindow
 * (image driven), plus the way the stored attributes follow the WS_EX_LAYERED style bit and ordinary window operations.
 * Measures on Windows 10: UpdateLayeredWindow fails with ERROR_INVALID_PARAMETER once attributes were set (even with flags
 * 0 or with no image arguments); SetLayeredWindowAttributes still succeeds on a window that was image driven and turns it
 * attribute driven; GetLayeredWindowAttributes fails on an image driven window without touching the last error or the
 * outputs; removing the style bit drops the stored attributes (Get/Set fail with 87 while the bit is clear, and after the
 * bit is added back Get fails again until a new Set); unrelated style changes, SetWindowPos and ShowWindow keep them.
 * Style change messages are counted with sent-message counters only. */
#include "xl.h"

#define W_IMG(w, im) XL_NEED(); struct xl_img im; if (!xl_img_make(&im, 8, 8)) { t_skip("image"); return; } HWND w = xl_lay(); \
    if (!w) { xl_img_free(&im); t_skip("window"); return; }
#define END(w, im) do { xl_kill(w); xl_img_free(&im); } while (0)
static void expect_get_fails_untouched(HWND w, DWORD err) {
    struct xl_attr a; xl_attr_clear(&a); SET_ERR(XL_ERR_SENT);
    BOOL r = xl_getall(w, &a);
    CHECK(!r); EXPECT_ERR(err);
    EXPECT_EQ(a.key, 0xdeadbeef); EXPECT_EQ(a.alpha, 0xa5); EXPECT_EQ(a.flags, 0xfeedf00d);
}
static void expect_get(HWND w, COLORREF k, BYTE a, DWORD f) {
    struct xl_attr o; xl_attr_clear(&o); SET_ERR(XL_ERR_SENT);
    BOOL r = xl_getall(w, &o);
    CHECK(r); if (r) { EXPECT_EQ(o.key, k); EXPECT_EQ(o.alpha, a); EXPECT_EQ(o.flags, f); EXPECT_EQ(GetLastError(), XL_ERR_SENT); }
}

/* ---- attribute first, image second --------------------------------------------------------------------------- */
TEST(excl_set_then_ulw_fails_87) {
    W_IMG(w, im); CHECK(xl_set(w, 0x112233, 100, 3));
    SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_ulw(w, &im, ULW_ALPHA), ERROR_INVALID_PARAMETER); END(w, im);
}
TEST(excl_set_alpha_only_then_ulw_fails_87) {
    W_IMG(w, im); CHECK(xl_set(w, 0, 100, LWA_ALPHA));
    SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_ulw(w, &im, ULW_ALPHA), ERROR_INVALID_PARAMETER); END(w, im);
}
TEST(excl_set_colorkey_only_then_ulw_fails_87) {
    W_IMG(w, im); CHECK(xl_set(w, 0x00ff00ff, 0, LWA_COLORKEY));
    SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_ulw(w, &im, ULW_COLORKEY), ERROR_INVALID_PARAMETER); END(w, im);
}
TEST(excl_set_flags_zero_then_ulw_fails_87) {
    W_IMG(w, im); CHECK(xl_set(w, 0, 0, 0));
    SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_ulw(w, &im, ULW_ALPHA), ERROR_INVALID_PARAMETER); END(w, im);
}
TEST(excl_set_then_ulw_flags_zero_fails_87) {
    W_IMG(w, im); CHECK(xl_set(w, 1, 2, 3));
    SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_ulw(w, &im, 0), ERROR_INVALID_PARAMETER); END(w, im);
}
TEST(excl_set_then_ulw_without_arguments_fails_87) {
    W_IMG(w, im); CHECK(xl_set(w, 1, 2, 3));
    SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!U(UpdateLayeredWindow)(w, NULL, NULL, NULL, NULL, NULL, 0, NULL, 0), ERROR_INVALID_PARAMETER); END(w, im);
}
TEST(excl_failed_ulw_leaves_attributes) {
    W_IMG(w, im); CHECK(xl_set(w, 0x112233, 100, 3));
    CHECK(!xl_ulw(w, &im, ULW_ALPHA)); expect_get(w, 0x112233, 100, 3); END(w, im);
}
TEST(excl_failed_ulw_repeats_identically) {
    W_IMG(w, im); CHECK(xl_set(w, 1, 2, 3));
    for (int i = 0; i < 3; i++) { SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_ulw(w, &im, ULW_ALPHA), ERROR_INVALID_PARAMETER); }
    expect_get(w, 1, 2, 3); END(w, im);
}
TEST(excl_set_then_ulw_with_invalid_flags_is_still_87) {
    W_IMG(w, im); CHECK(xl_set(w, 1, 2, 3));
    SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_ulw(w, &im, 0x10), ERROR_INVALID_PARAMETER); END(w, im);
}

/* ---- image first, attributes second -------------------------------------------------------------------------- */
TEST(excl_ulw_then_get_fails_untouched) {
    W_IMG(w, im); CHECK(xl_ulw(w, &im, ULW_ALPHA)); expect_get_fails_untouched(w, XL_ERR_SENT); END(w, im);
}
TEST(excl_ulw_then_get_all_null_pointers_fails) {
    W_IMG(w, im); CHECK(xl_ulw(w, &im, ULW_ALPHA)); SET_ERR(XL_ERR_SENT); CHECK(!xl_get(w, NULL, NULL, NULL)); EXPECT_ERR(XL_ERR_SENT); END(w, im);
}
TEST(excl_ulw_then_set_succeeds) {
    W_IMG(w, im); CHECK(xl_ulw(w, &im, ULW_ALPHA));
    SET_ERR(XL_ERR_SENT); CHECK(xl_set(w, 0x112233, 100, 3)); EXPECT_EQ(GetLastError(), XL_ERR_SENT); END(w, im);
}
TEST(excl_ulw_then_set_flags_zero_succeeds) {
    W_IMG(w, im); CHECK(xl_ulw(w, &im, ULW_ALPHA)); CHECK(xl_set(w, 0, 0, 0)); END(w, im);
}
TEST(excl_ulw_then_set_makes_get_work) {
    W_IMG(w, im); CHECK(xl_ulw(w, &im, ULW_ALPHA)); CHECK(xl_set(w, 0x112233, 100, 3)); expect_get(w, 0x112233, 100, 3); END(w, im);
}
TEST(excl_ulw_then_set_bad_flags_87) {
    W_IMG(w, im); CHECK(xl_ulw(w, &im, ULW_ALPHA)); SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_set(w, 1, 2, 4), ERROR_INVALID_PARAMETER);
    expect_get_fails_untouched(w, XL_ERR_SENT); END(w, im);
}
TEST(excl_ulw_set_then_ulw_fails_87) {
    W_IMG(w, im); CHECK(xl_ulw(w, &im, ULW_ALPHA)); CHECK(xl_set(w, 1, 2, 3));
    SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_ulw(w, &im, ULW_ALPHA), ERROR_INVALID_PARAMETER); END(w, im);
}
TEST(excl_ulw_twice_both_succeed) {
    W_IMG(w, im); CHECK(xl_ulw(w, &im, ULW_ALPHA)); SET_ERR(XL_ERR_SENT); CHECK(xl_ulw(w, &im, ULW_ALPHA)); EXPECT_EQ(GetLastError(), XL_ERR_SENT); END(w, im);
}

/* ---- the style bit -------------------------------------------------------------------------------------------- */
TEST(excl_clear_bit_get_set_ulw_all_87) {
    W_IMG(w, im); CHECK(xl_set(w, 1, 2, 3)); U(SetWindowLongA)(w, GWL_EXSTYLE, xl_exstyle(w) & ~XL_EXL);
    CHECK(!xl_is_lay(w)); expect_get_fails_untouched(w, ERROR_INVALID_PARAMETER);
    SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_set(w, 1, 2, 3), ERROR_INVALID_PARAMETER);
    SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_ulw(w, &im, ULW_ALPHA), ERROR_INVALID_PARAMETER); END(w, im);
}
TEST(excl_readd_bit_drops_attributes) {
    W_IMG(w, im); CHECK(xl_set(w, 0x123456, 77, 3)); LONG ex = xl_exstyle(w);
    U(SetWindowLongA)(w, GWL_EXSTYLE, ex & ~XL_EXL); U(SetWindowLongA)(w, GWL_EXSTYLE, ex);
    CHECK(xl_is_lay(w)); expect_get_fails_untouched(w, XL_ERR_SENT); END(w, im);
}
TEST(excl_readd_bit_then_set_then_get) {
    W_IMG(w, im); CHECK(xl_set(w, 0x123456, 77, 3)); LONG ex = xl_exstyle(w);
    U(SetWindowLongA)(w, GWL_EXSTYLE, ex & ~XL_EXL); U(SetWindowLongA)(w, GWL_EXSTYLE, ex);
    CHECK(xl_set(w, 9, 8, 3)); expect_get(w, 9, 8, 3); END(w, im);
}
TEST(excl_readd_bit_after_set_allows_ulw_again) {
    W_IMG(w, im); CHECK(xl_set(w, 0x123456, 77, 3)); LONG ex = xl_exstyle(w);
    U(SetWindowLongA)(w, GWL_EXSTYLE, ex & ~XL_EXL); U(SetWindowLongA)(w, GWL_EXSTYLE, ex);
    SET_ERR(XL_ERR_SENT); CHECK(xl_ulw(w, &im, ULW_ALPHA)); END(w, im);
}
TEST(excl_readd_bit_after_ulw_then_set_then_ulw_fails) {
    W_IMG(w, im); CHECK(xl_ulw(w, &im, ULW_ALPHA)); LONG ex = xl_exstyle(w);
    U(SetWindowLongA)(w, GWL_EXSTYLE, ex & ~XL_EXL); U(SetWindowLongA)(w, GWL_EXSTYLE, ex);
    expect_get_fails_untouched(w, XL_ERR_SENT); CHECK(xl_set(w, 1, 2, 3));
    SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_ulw(w, &im, ULW_ALPHA), ERROR_INVALID_PARAMETER); END(w, im);
}
TEST(excl_bit_added_by_setwindowlong_behaves_like_created) {
    XL_NEED(); struct xl_img im; xl_img_make(&im, 8, 8); HWND w = xl_plain();
    expect_get_fails_untouched(w, ERROR_INVALID_PARAMETER);
    U(SetWindowLongA)(w, GWL_EXSTYLE, xl_exstyle(w) | XL_EXL); CHECK(xl_is_lay(w));
    expect_get_fails_untouched(w, XL_ERR_SENT); CHECK(xl_set(w, 1, 2, 3)); expect_get(w, 1, 2, 3); END(w, im);
}
TEST(excl_bit_added_by_setwindowlong_allows_ulw) {
    XL_NEED(); struct xl_img im; xl_img_make(&im, 8, 8); HWND w = xl_plain();
    U(SetWindowLongA)(w, GWL_EXSTYLE, xl_exstyle(w) | XL_EXL); SET_ERR(XL_ERR_SENT); CHECK(xl_ulw(w, &im, ULW_ALPHA)); END(w, im);
}
TEST(excl_setwindowlongw_and_ptr_toggle_alike) {
    W_IMG(w, im); CHECK(xl_set(w, 1, 2, 3));
    U(SetWindowLongW)(w, GWL_EXSTYLE, xl_exstyle(w) & ~XL_EXL); CHECK(!xl_is_lay(w));
    U(SetWindowLongPtrA)(w, GWL_EXSTYLE, xl_exstyle(w) | XL_EXL); CHECK(xl_is_lay(w));
    expect_get_fails_untouched(w, XL_ERR_SENT); END(w, im);
}
TEST(excl_set_does_not_change_exstyle) {
    W_IMG(w, im); LONG a = xl_exstyle(w); CHECK(xl_set(w, 1, 2, 3)); EXPECT_EQ(xl_exstyle(w), a); END(w, im);
}
TEST(excl_ulw_does_not_change_exstyle) {
    W_IMG(w, im); LONG a = xl_exstyle(w); CHECK(xl_ulw(w, &im, ULW_ALPHA)); EXPECT_EQ(xl_exstyle(w), a); END(w, im);
}

/* ---- operations that keep the stored attributes --------------------------------------------------------------- */
TEST(excl_attrs_survive_setwindowpos_and_show) {
    W_IMG(w, im); CHECK(xl_set(w, 0x010203, 66, 3));
    U(SetWindowPos)(w, NULL, -31000, -31000, 40, 40, SWP_NOZORDER | SWP_NOACTIVATE); expect_get(w, 0x010203, 66, 3);
    U(SetWindowPos)(w, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED); expect_get(w, 0x010203, 66, 3);
    U(SetWindowPos)(w, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW); expect_get(w, 0x010203, 66, 3);
    U(ShowWindow)(w, SW_HIDE); expect_get(w, 0x010203, 66, 3); END(w, im);
}
TEST(excl_attrs_survive_other_style_changes) {
    W_IMG(w, im); CHECK(xl_set(w, 0x010203, 66, 3));
    U(SetWindowLongA)(w, GWL_EXSTYLE, xl_exstyle(w) | WS_EX_TRANSPARENT); expect_get(w, 0x010203, 66, 3);
    U(SetWindowLongA)(w, GWL_STYLE, U(GetWindowLongA)(w, GWL_STYLE) | WS_BORDER); expect_get(w, 0x010203, 66, 3); END(w, im);
}
TEST(excl_set_returns_old_exstyle_unchanged_by_attribute_calls) {
    W_IMG(w, im); LONG a = xl_exstyle(w); xl_set(w, 1, 2, 3); LONG r = U(SetWindowLongA)(w, GWL_EXSTYLE, a); EXPECT_EQ(r, a); END(w, im);
}

/* ---- messages (sent-message counters; no timing involved) -------------------------------------------------------- */
TEST(excl_set_sends_no_style_or_position_messages) {
    W_IMG(w, im); k_pump(30); xl_msgs_reset();
    CHECK(xl_set(w, 1, 2, 3)); k_pump(30);
    EXPECT_EQ(xl_msgs[XM_STYLECHANGING] + xl_msgs[XM_STYLECHANGED], 0);
    EXPECT_EQ(xl_msgs[XM_POSCHANGING] + xl_msgs[XM_POSCHANGED] + xl_msgs[XM_SIZE] + xl_msgs[XM_MOVE], 0);
    END(w, im);
}
TEST(excl_set_again_sends_nothing) {
    W_IMG(w, im); CHECK(xl_set(w, 1, 2, 3)); k_pump(30); xl_msgs_reset();
    CHECK(xl_set(w, 1, 9, 2)); k_pump(30);
    for (int i = 0; i < XM_N; i++) CHECKF(xl_msgs[i] == 0, "message slot %d count %ld", i, xl_msgs[i]);
    END(w, im);
}
TEST(excl_style_bit_toggle_sends_one_pair_each_way) {
    W_IMG(w, im); k_pump(30);
    xl_msgs_reset(); U(SetWindowLongA)(w, GWL_EXSTYLE, xl_exstyle(w) & ~XL_EXL);
    EXPECT_EQ(xl_msgs[XM_STYLECHANGING], 1); EXPECT_EQ(xl_msgs[XM_STYLECHANGED], 1);
    xl_msgs_reset(); U(SetWindowLongA)(w, GWL_EXSTYLE, xl_exstyle(w) | XL_EXL);
    EXPECT_EQ(xl_msgs[XM_STYLECHANGING], 1); EXPECT_EQ(xl_msgs[XM_STYLECHANGED], 1);
    END(w, im);
}
TEST(excl_first_set_paint_effect_is_observed) {
    W_IMG(w, im); k_pump(30); xl_msgs_reset(); CHECK(xl_set(w, 1, 2, 3)); k_pump(50);
    t_note("first Set on a fresh layered window: paint=%ld ncpaint=%ld erase=%ld", xl_msgs[XM_PAINT], xl_msgs[XM_NCPAINT], xl_msgs[XM_ERASE]);
    END(w, im);
}

/* ---- Set/Get contract that interacts with the above ---------------------------------------------------------------- */
TEST(excl_user32_set_visible_through_service_get) {
    W_IMG(w, im); SET_ERR(XL_ERR_SENT); CHECK(U(SetLayeredWindowAttributes)(w, 0x10, 20, 3)); expect_get(w, 0x10, 20, 3);
    COLORREF k = 0; BYTE a = 0; DWORD f = 0; CHECK(U(GetLayeredWindowAttributes)(w, &k, &a, &f)); EXPECT_EQ(k, 0x10); EXPECT_EQ(a, 20); EXPECT_EQ(f, 3); END(w, im);
}
TEST(excl_user32_get_fresh_layered_fails_untouched) {
    W_IMG(w, im); COLORREF k = 1; BYTE a = 2; DWORD f = 3; SET_ERR(XL_ERR_SENT);
    CHECK(!U(GetLayeredWindowAttributes)(w, &k, &a, &f)); EXPECT_ERR(XL_ERR_SENT); EXPECT_EQ(k, 1); EXPECT_EQ(a, 2); EXPECT_EQ(f, 3); END(w, im);
}
TEST(excl_set_after_set_replaces_fields_selected_by_flags) {
    /* semantics of repeated Set calls: see the read-back table test below */
    W_IMG(w, im); CHECK(xl_set(w, 1000, 10, 3)); CHECK(xl_set(w, 2000, 20, 3)); expect_get(w, 2000, 20, 3); END(w, im);
}

/* ---- what a Set call stores: only the fields selected by the flags are replaced; the flags value is always replaced ---------------- */
TEST(excl_set_flags_zero_on_fresh_window_stores_nothing) {
    W_IMG(w, im); CHECK(xl_set(w, 1000, 10, 0)); expect_get(w, 0, 0, 0); END(w, im);
}
TEST(excl_set_colorkey_only_keeps_alpha) {
    W_IMG(w, im); CHECK(xl_set(w, 4000, 40, 3)); CHECK(xl_set(w, 5000, 50, LWA_COLORKEY)); expect_get(w, 5000, 40, 1); END(w, im);
}
TEST(excl_set_alpha_only_keeps_key) {
    W_IMG(w, im); CHECK(xl_set(w, 4000, 40, 3)); CHECK(xl_set(w, 5000, 50, LWA_ALPHA)); expect_get(w, 4000, 50, 2); END(w, im);
}
TEST(excl_set_flags_zero_keeps_fields_but_clears_flags) {
    W_IMG(w, im); CHECK(xl_set(w, 4000, 40, 3)); CHECK(xl_set(w, 7000, 70, 0)); expect_get(w, 4000, 40, 0); END(w, im);
}
TEST(excl_set_colorkey_all_bits_kept) {
    W_IMG(w, im); static const COLORREF keys[] = { 0, 0x00ffffff, 0x01000000, 0x01020304, 0x7fffffff, 0xffffffff, 0x12345678 };
    for (unsigned i = 0; i < sizeof keys / sizeof keys[0]; i++) { CHECK(xl_set(w, keys[i], 1, LWA_COLORKEY)); struct xl_attr a; CHECK(xl_getall(w, &a)); EXPECT_EQ(a.key, keys[i]); }
    END(w, im);
}
TEST(excl_set_alpha_all_byte_values_kept) {
    W_IMG(w, im);
    for (unsigned v = 0; v < 256; v += 51) { CHECK(xl_set(w, 0, (BYTE)v, LWA_ALPHA)); struct xl_attr a; CHECK(xl_getall(w, &a)); EXPECT_EQ(a.alpha, v); }
    CHECK(xl_set(w, 0, 255, LWA_ALPHA)); expect_get(w, 0, 255, 2); END(w, im);
}
TEST(excl_set_sequence_table) {
    W_IMG(w, im);
    static const struct { COLORREF k; BYTE a; DWORD f; COLORREF rk; BYTE ra; DWORD rf; } seq[] = {
        {1000,10,0, 0,0,0}, {2000,20,1, 2000,0,1}, {3000,30,2, 2000,30,2}, {4000,40,3, 4000,40,3}, {5000,50,1, 5000,40,1}, {6000,60,2, 5000,60,2},
        {0,0,3, 0,0,3}, {0x01020304,255,3, 0x01020304,255,3}, {0x7fffffff,128,1, 0x7fffffff,255,1}, {0xffffffff,1,3, 0xffffffff,1,3},
        {7000,70,0, 0xffffffff,1,0}, {77,77,2, 0xffffffff,77,2} };
    for (unsigned i = 0; i < sizeof seq / sizeof seq[0]; i++) { CHECKF(xl_set(w, seq[i].k, seq[i].a, seq[i].f), "step %u", i); struct xl_attr a; CHECK(xl_getall(w, &a));
        CHECKF(a.key == seq[i].rk && a.alpha == seq[i].ra && a.flags == seq[i].rf, "step %u got key 0x%lx alpha %u flags %lu", i, (unsigned long)a.key, a.alpha, a.flags); }
    END(w, im);
}
TEST(excl_ulw_then_set_colorkey_only_defaults_alpha_255) {
    W_IMG(w, im); CHECK(xl_ulw(w, &im, ULW_ALPHA)); CHECK(xl_set(w, 1000, 20, LWA_COLORKEY)); expect_get(w, 1000, 255, 1); END(w, im);
}
TEST(excl_ulw_then_set_flags_zero_defaults_alpha_255_then_ulw_fails) {
    W_IMG(w, im); CHECK(xl_ulw(w, &im, ULW_ALPHA)); CHECK(xl_set(w, 1000, 20, 0)); expect_get(w, 0, 255, 0);
    SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_ulw(w, &im, ULW_ALPHA), ERROR_INVALID_PARAMETER); END(w, im);
}
TEST(excl_exstyle_value_stable_across_all_calls) {
    W_IMG(w, im); LONG a = xl_exstyle(w); CHECK(xl_set(w, 1, 2, 3)); (void)xl_set(w, 1, 2, 4); (void)xl_ulw(w, &im, ULW_ALPHA); EXPECT_EQ(xl_exstyle(w), a); END(w, im);
}
TEST(excl_get_with_each_single_pointer) {
    W_IMG(w, im); CHECK(xl_set(w, 0x0a0b0c, 44, 3)); COLORREF k = 1; BYTE a = 2; DWORD f = 3;
    CHECK(xl_get(w, NULL, NULL, NULL)); CHECK(xl_get(w, &k, NULL, NULL)); EXPECT_EQ(k, 0x0a0b0c);
    CHECK(xl_get(w, NULL, &a, NULL)); EXPECT_EQ(a, 44); CHECK(xl_get(w, NULL, NULL, &f)); EXPECT_EQ(f, 3); END(w, im);
}
TEST(excl_get_null_pointers_on_failing_windows_still_fail) {
    W_IMG(w, im); SET_ERR(XL_ERR_SENT); CHECK(!xl_get(w, NULL, NULL, NULL)); EXPECT_ERR(XL_ERR_SENT);
    HWND p = xl_plain(); SET_ERR(XL_ERR_SENT); CHECK(!xl_get(p, NULL, NULL, NULL)); EXPECT_ERR(ERROR_INVALID_PARAMETER); xl_kill(p);
    SET_ERR(XL_ERR_SENT); CHECK(!xl_get(xl_bogus(), NULL, NULL, NULL)); EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE); END(w, im);
}
TEST(excl_set_get_on_desktop_window_87) {
    XL_NEED(); HWND d = U(GetDesktopWindow)(); COLORREF k; BYTE a; DWORD f;
    SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_set(d, 0, 1, 2), ERROR_INVALID_PARAMETER); SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_get(d, &k, &a, &f), ERROR_INVALID_PARAMETER);
}
TEST(excl_null_and_bogus_handles_1400_even_with_bad_flags) {
    XL_NEED(); SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_set(NULL, 0, 0, 0), ERROR_INVALID_WINDOW_HANDLE);
    SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_set(NULL, 0, 0, 0x10), ERROR_INVALID_WINDOW_HANDLE);
    SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_set(xl_bogus(), 0, 0, 0x10), ERROR_INVALID_WINDOW_HANDLE);
}
TEST(excl_set_on_other_window_shapes) {
    XL_NEED(); HWND w = xl_mk_at(WS_OVERLAPPEDWINDOW, XL_EXL, NULL, -32000, -32000, 100, 100); CHECK(w != NULL);
    if (w) { CHECK(xl_set(w, 0, 128, 2)); xl_kill(w); }
    HWND o = xl_lay(); w = xl_mk_at(WS_POPUP, XL_EXL, o, -32000, -32000, 10, 10); CHECK(w != NULL);
    if (w) { CHECK(xl_set(w, 0, 128, 2)); xl_kill(w); } xl_kill(o);
    w = xl_lay(); U(ShowWindow)(w, SW_SHOWNOACTIVATE); CHECK(xl_set(w, 0, 128, 2)); xl_kill(w);
}
TEST(excl_ulw_on_shown_window_then_set) {
    W_IMG(w, im); U(ShowWindow)(w, SW_SHOWNOACTIVATE); CHECK(xl_ulw(w, &im, ULW_ALPHA)); CHECK(xl_set(w, 0, 128, 2)); END(w, im);
}
