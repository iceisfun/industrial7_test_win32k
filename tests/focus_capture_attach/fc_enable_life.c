/* EnableWindow lifecycle fixture: the BOOL result (previous state, not success), IsWindowEnabled and the exact
 * notification log for disable/disable/enable/enable, with and without capture, and for parent/child.
 * Observed on Windows 10: every disable sends WM_CANCELMODE first (also a repeated one), WM_ENABLE(0) only on a
 * change; WM_CAPTURECHANGED sits between them when the window has capture; IsWindowEnabled reads only the window's
 * own WS_DISABLED bit, so a child of a disabled parent still reports enabled and gets no WM_ENABLE. */
#include "fc.h"

/* Compact text of the log: M=WM_CANCELMODE E0/E1=WM_ENABLE(0/1) K=WM_KILLFOCUS C=WM_CAPTURECHANGED, others by number.
 * "w" = the window under test, "c" = its child, "?" = another window. */
static void seq_text(HWND w, HWND c, char *out, size_t n) {
    out[0] = 0; size_t l = 0; int cnt = fc_total();
    for (int i = 0; i < cnt && l + 12 < n; i++) {
        struct fc_msg *m = &fc_log[i];
        const char *who = m->hwnd == w ? "w" : (c && m->hwnd == c) ? "c" : "?";
        char t[24];
        switch (m->msg) {
        case WM_CANCELMODE: _snprintf(t, sizeof t, "M"); break;
        case WM_ENABLE: _snprintf(t, sizeof t, "E%d", (int)m->wp); break;
        case WM_KILLFOCUS: _snprintf(t, sizeof t, "K"); break;
        case WM_CAPTURECHANGED: _snprintf(t, sizeof t, "C"); break;
        default: _snprintf(t, sizeof t, "m%x", m->msg); break;
        }
        l += _snprintf(out + l, n - l, "%s%s%s", l ? " " : "", t, who);
    }
}


static void expect_seq(HWND w, HWND c, const char *want, const char *what) {
    char s[256]; seq_text(w, c, s, sizeof s);
    CHECKF(strcmp(s, want) == 0, "%s: log [%s] want [%s]", what, s, want);
    fc_reset();
}

TEST(fca_enablelife_four_step_results_and_log) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    BOOL r = U(EnableWindow)(a, FALSE);
    EXPECT_EQ(r, 0);  CHECK(!U(IsWindowEnabled)(a));  expect_seq(a, NULL, "Mw E0w", "disable #1");
    r = U(EnableWindow)(a, FALSE);
    CHECK(r != 0);    CHECK(!U(IsWindowEnabled)(a));  expect_seq(a, NULL, "Mw", "disable #2 (no WM_ENABLE)");
    r = U(EnableWindow)(a, TRUE);
    CHECK(r != 0);    CHECK(U(IsWindowEnabled)(a));   expect_seq(a, NULL, "E1w", "enable #1");
    r = U(EnableWindow)(a, TRUE);
    EXPECT_EQ(r, 0);  CHECK(U(IsWindowEnabled)(a));   expect_seq(a, NULL, "", "enable #2 (nothing)");
    fc_destroy(a);
}

TEST(fca_enablelife_disable_with_capture) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    U(SetCapture)(a);
    if (U(GetCapture)() != a) { fc_destroy(a); FC_SKIP("capture not taken"); }
    fc_reset();
    BOOL r = U(EnableWindow)(a, FALSE);
    EXPECT_EQ(r, 0);
    EXPECT_EQ(U(GetCapture)(), NULL);                       /* capture is released by the disable */
    expect_seq(a, NULL, "Mw Cw E0w", "disable with capture");
    r = U(EnableWindow)(a, FALSE);
    CHECK(r != 0);
    expect_seq(a, NULL, "Mw", "repeated disable, capture already gone");
    r = U(EnableWindow)(a, TRUE);
    CHECK(r != 0);
    expect_seq(a, NULL, "E1w", "enable after capture disable");
    EXPECT_EQ(U(GetCapture)(), NULL);
    fc_destroy(a);
}

TEST(fca_enablelife_capture_is_in_the_middle) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    U(SetCapture)(a);
    if (U(GetCapture)() != a) { fc_destroy(a); FC_SKIP("capture not taken"); }
    fc_reset();
    U(EnableWindow)(a, FALSE);
    int m = fc_find(WM_CANCELMODE, a, 0), c = fc_find(WM_CAPTURECHANGED, a, 0), e = fc_find(WM_ENABLE, a, 0);
    CHECK(m >= 0 && c >= 0 && e >= 0);
    if (m >= 0 && c >= 0) CHECK(m < c);
    if (c >= 0 && e >= 0) CHECK(c < e);
    if (m >= 0) { CHECK(!(fc_log[m].style_now & WS_DISABLED)); EXPECT_EQ(fc_log[m].capture_now, a); }  /* still captured, still enabled */
    if (e >= 0) { CHECK(fc_log[e].style_now & WS_DISABLED); EXPECT_EQ(fc_log[e].capture_now, NULL); EXPECT_EQ(fc_log[e].wp, 0); }
    U(EnableWindow)(a, TRUE);
    fc_destroy(a);
}

TEST(fca_enablelife_parent_child_no_per_child_message) {
    FC_NEED();
    HWND p = fc_top(); REQUIRE(p);
    HWND c = fc_child(p); REQUIRE(c);
    fc_quiesce();
    BOOL r = U(EnableWindow)(p, FALSE);
    EXPECT_EQ(r, 0);
    CHECK(!U(IsWindowEnabled)(p));
    CHECK(U(IsWindowEnabled)(c));                           /* only the window's own bit is read */
    CHECK(!(U(GetWindowLongW)(c, GWL_STYLE) & WS_DISABLED));
    expect_seq(p, c, "Mw E0w", "parent disable");
    r = U(EnableWindow)(p, TRUE);
    CHECK(r != 0);
    CHECK(U(IsWindowEnabled)(p)); CHECK(U(IsWindowEnabled)(c));
    expect_seq(p, c, "E1w", "parent enable");
    fc_destroy(p);
}

TEST(fca_enablelife_child_state_independent_of_parent) {
    FC_NEED();
    HWND p = fc_top(); REQUIRE(p);
    HWND c = fc_child(p); REQUIRE(c);
    fc_quiesce();
    BOOL r = U(EnableWindow)(c, FALSE);
    EXPECT_EQ(r, 0);
    CHECK(!U(IsWindowEnabled)(c)); CHECK(U(IsWindowEnabled)(p));
    expect_seq(p, c, "Mc E0c", "child disable");
    r = U(EnableWindow)(p, FALSE);                          /* parent disable does not touch the disabled child */
    EXPECT_EQ(r, 0);
    expect_seq(p, c, "Mw E0w", "parent disable, child already disabled");
    r = U(EnableWindow)(p, TRUE);
    CHECK(r != 0);
    CHECK(U(IsWindowEnabled)(p)); CHECK(!U(IsWindowEnabled)(c));   /* enabling the parent does not enable the child */
    expect_seq(p, c, "E1w", "parent enable, child stays disabled");
    r = U(EnableWindow)(c, TRUE);
    CHECK(r != 0);
    CHECK(U(IsWindowEnabled)(c));
    expect_seq(p, c, "E1c", "child enable");
    fc_destroy(p);
}
