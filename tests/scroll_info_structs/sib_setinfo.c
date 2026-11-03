/* SetScrollInfo / GetScrollInfo / SetScrollPos / SetScrollRange and the kernel scroll state.
 * Kernel: NtUserSetScrollInfo -> SetScrollInfo -> scroll-parameter code (clamping), NtUserSBGetParms (reader).
 * user32 client side: RealGetScrollInfo validates cbSize (0x18/0x1c) and fMask & 0xffff8fe0 itself. */
#include "sib.h"

#define SI_ALL_BITS (SIF_RANGE | SIF_PAGE | SIF_POS)

static int gsi(HWND h, int bar, UINT mask, SCROLLINFO *si) {
    memset(si, 0, sizeof *si);
    si->cbSize = sizeof *si;
    si->fMask = mask;
    return U(GetScrollInfo)(h, bar, si);
}

/* Reference model of the kernel scroll-parameter code (clamping rules). */
static int model_set(struct sb_data *d, UINT mask, int mn, int mx, UINT pg, int pos) {
    int prev = d->pos;
    if (mask & SIF_RANGE) {
        if (mx < mn) mx = mn;                      /* scroll-parameter code: max < min => max = min */
        if (d->min != mn || d->max != mx) {
            d->min = mn; d->max = mx;
            if (!(mask & SIF_PAGE)) { mask |= SIF_PAGE; pg = d->page; }   /* range change re-clamps page and pos */
            if (!(mask & SIF_POS))  { mask |= SIF_POS;  pos = d->pos; }
        }
    }
    if (mask & SIF_PAGE) {
        UINT range = (UINT)(d->max - d->min < 0 ? d->min - d->max : d->max - d->min) + 1;
        if (pg > range) pg = range;
        if ((UINT)d->page != pg) {
            d->page = (int)pg;
            if (!(mask & SIF_POS)) { mask |= SIF_POS; pos = d->pos; }
        }
    }
    if (mask & SIF_POS) {
        int hi = d->max - (d->page ? d->page - 1 : 0);
        if (pos < d->min) pos = d->min; else if (hi < pos) pos = hi;
        d->pos = pos;
    }
    return (mask & 0x1000) ? prev : d->pos;     /* SIF_RETURNPREV */
}

struct vec { UINT mask; int mn, mx; UINT pg; int pos; };

static void run_seq(const char *name, const struct vec *v, int n, int bar, DWORD hostStyle) {
    HWND h = sib_mk(hostStyle, 0, 200, 200, 0);
    if (!h) { t_skip("window"); return; }
    struct sb_data d = {0, 0, 0, 0};
    int started = 0;
    for (int i = 0; i < n; i++) {
        SCROLLINFO si = { sizeof si, v[i].mask, v[i].mn, v[i].mx, v[i].pg, v[i].pos, 0 };
        if (!started && (v[i].mask & SIF_RANGE) == 0) { /* first op without range on a window without scroll info */ }
        int want = model_set(&d, v[i].mask, v[i].mn, v[i].mx, v[i].pg, v[i].pos);
        int got = U(SetScrollInfo)(h, bar, &si, FALSE);
        started = 1;
        CHECKF(got == want, "%s bar %d step %d: SetScrollInfo returned %d want %d", name, bar, i, got, want);
        SCROLLINFO g;
        CHECK(gsi(h, bar, SIF_ALL, &g));
        CHECKF(g.nMin == d.min && g.nMax == d.max, "%s bar %d step %d range %d..%d want %d..%d", name, bar, i, g.nMin, g.nMax, d.min, d.max);
        CHECKF((int)g.nPage == d.page, "%s bar %d step %d page %u want %d", name, bar, i, g.nPage, d.page);
        CHECKF(g.nPos == d.pos, "%s bar %d step %d pos %d want %d", name, bar, i, g.nPos, d.pos);
        /* NtUserSBGetParms: nTrackPos mirrors nPos when no thumb drag is in progress. */
        CHECKF(g.nTrackPos == d.pos, "%s bar %d step %d trackpos %d want %d", name, bar, i, g.nTrackPos, d.pos);
        CHECK(U(GetScrollPos)(h, bar) == d.pos);
        int mn = -1, mx = -1;
        CHECK(U(GetScrollRange)(h, bar, &mn, &mx));
        CHECK(mn == d.min && mx == d.max);
        struct sb_info sb; int pres = 0;
        if (sib_read_sbinfo(h, &sb, &pres) && pres) {
            struct sb_data *s = bar == SB_HORZ ? &sb.h : &sb.v;
            CHECKF(s->min == d.min && s->max == d.max && s->page == d.page && s->pos == d.pos,
                   "%s bar %d step %d SBINFO %d %d %d %d want %d %d %d %d", name, bar, i, s->min, s->max, s->page, s->pos, d.min, d.max, d.page, d.pos);
        }
    }
    U(DestroyWindow)(h);
}

#define SEQ(nm, ...) \
    TEST(si_seq_##nm) { \
        REQUIRE(U(SetScrollInfo)); REQUIRE(U(GetScrollInfo)); REQUIRE(U(CreateWindowExW)); \
        static const struct vec v[] = { __VA_ARGS__ }; \
        run_seq(#nm, v, sizeof v / sizeof *v, SB_HORZ, WS_OVERLAPPEDWINDOW); \
        run_seq(#nm, v, sizeof v / sizeof *v, SB_VERT, WS_OVERLAPPEDWINDOW); \
    }

SEQ(basic,       { SI_ALL_BITS, 0, 100, 10, 50 })
SEQ(pos_hi,      { SI_ALL_BITS, 0, 100, 10, 500 })              /* pos clamps to max - page + 1 = 91 */
SEQ(pos_lo,      { SI_ALL_BITS, 0, 100, 10, -500 })
SEQ(page_zero,   { SI_ALL_BITS, 0, 100, 0, 1000 })              /* page 0 => pos max is max itself */
SEQ(page_full,   { SI_ALL_BITS, 0, 100, 101, 40 })              /* page == range size */
SEQ(page_over,   { SI_ALL_BITS, 0, 100, 5000, 40 })             /* page clamps to range size 101 */
SEQ(page_huge,   { SI_ALL_BITS, 0, 100, 0xFFFFFFF0u, 3 })       /* unsigned compare */
SEQ(range_swap,  { SI_ALL_BITS, 0, 10, 2, 5 }, { SIF_RANGE, 10, 5, 0, 0 })   /* max < min => max = min */
SEQ(range_one,   { SI_ALL_BITS, 0, 100, 10, 50 }, { SIF_RANGE, 7, 7, 0, 0 })
SEQ(range_neg,   { SI_ALL_BITS, -100, -50, 4, -70 })
SEQ(range_shrink,{ SI_ALL_BITS, 0, 100, 20, 80 }, { SIF_RANGE, 0, 40, 0, 0 })   /* page and pos re-clamped */
SEQ(range_grow,  { SI_ALL_BITS, 0, 10, 3, 7 }, { SIF_RANGE, 0, 1000, 0, 0 })
SEQ(range_shift, { SI_ALL_BITS, 0, 100, 10, 50 }, { SIF_RANGE, 200, 300, 0, 0 })
SEQ(page_shrink, { SI_ALL_BITS, 0, 100, 50, 51 }, { SIF_PAGE, 0, 0, 10, 0 })
SEQ(page_grow,   { SI_ALL_BITS, 0, 100, 10, 90 }, { SIF_PAGE, 0, 0, 60, 0 })   /* pos clamps to 41 */
SEQ(pos_only,    { SI_ALL_BITS, 0, 100, 10, 0 }, { SIF_POS, 0, 0, 0, 30 }, { SIF_POS, 0, 0, 0, 91 }, { SIF_POS, 0, 0, 0, 92 })
SEQ(returnprev,  { SI_ALL_BITS, 0, 100, 10, 20 }, { SIF_POS | 0x1000, 0, 0, 0, 60 }, { SIF_POS | 0x1000, 0, 0, 0, 70 })
SEQ(returnprev_range, { SI_ALL_BITS, 0, 100, 10, 80 }, { SIF_RANGE | 0x1000, 0, 50, 0, 0 })
SEQ(extremes,    { SI_ALL_BITS, -2147483647 - 1, -2147483647 - 1 + 1000, 10, 0 })
SEQ(near_max,    { SI_ALL_BITS, 2147483000, 2147483647, 100, 2147483647 })
SEQ(many_steps,  { SI_ALL_BITS, 0, 50, 5, 25 }, { SIF_POS, 0, 0, 0, 100 }, { SIF_PAGE, 0, 0, 51, 0 }, { SIF_RANGE, 0, 20, 0, 0 },
                 { SIF_PAGE | SIF_POS, 0, 0, 7, 3 }, { SIF_RANGE | SIF_POS, 5, 15, 0, 0 })

/* ---- direct stub path (win32u) ----------------------------------------------------------- */

TEST(si_stub_basic) {
    FARPROC p = sib_stub("NtUserSetScrollInfo");
    REQUIRE(p); REQUIRE(U(CreateWindowExW)); REQUIRE(U(GetScrollInfo));
    NtUserSetScrollInfo_pfn f = (NtUserSetScrollInfo_pfn)p;
    HWND h = sib_mk(WS_OVERLAPPEDWINDOW, 0, 200, 200, 0); SIB_NEED_WND(h);
    SCROLLINFO si = { sizeof si, SI_ALL_BITS, 0, 100, 10, 55, 0 };
    /* Returns the resulting position (scroll-parameter code writes it to the return slot). */
    EQ(f(h, SB_VERT, &si, FALSE), 55);
    SCROLLINFO g; CHECK(gsi(h, SB_VERT, SIF_ALL, &g));
    CHECK(g.nMin == 0 && g.nMax == 100 && g.nPage == 10 && g.nPos == 55);
    U(DestroyWindow)(h);
}

TEST(si_stub_cbsize_not_validated) {
    FARPROC p = sib_stub("NtUserSetScrollInfo");
    REQUIRE(p); REQUIRE(U(CreateWindowExW)); REQUIRE(U(GetScrollInfo));
    NtUserSetScrollInfo_pfn f = (NtUserSetScrollInfo_pfn)p;
    HWND h = sib_mk(WS_OVERLAPPEDWINDOW, 0, 200, 200, 0); SIB_NEED_WND(h);
    /* NtUserSetScrollInfo copies 0x1c bytes and never looks at cbSize. */
    static const UINT sizes[] = { 0, 1, 0x14, 0x18, 0x1c, 0x20, 0x7fffffff, 0xffffffff };
    for (unsigned i = 0; i < sizeof sizes / sizeof *sizes; i++) {
        SCROLLINFO si = { sizes[i], SI_ALL_BITS, 0, 100, 10, (int)(i + 1), 0 };
        SET_ERR(0);
        EQ(f(h, SB_HORZ, &si, FALSE), (int)(i + 1));
    }
    U(DestroyWindow)(h);
}

TEST(si_stub_bad_bar) {
    FARPROC p = sib_stub("NtUserSetScrollInfo");
    REQUIRE(p); REQUIRE(U(CreateWindowExW));
    NtUserSetScrollInfo_pfn f = (NtUserSetScrollInfo_pfn)p;
    HWND h = sib_mk(WS_OVERLAPPEDWINDOW, 0, 200, 200, 0); SIB_NEED_WND(h);
    static const int bars[] = { 4, 5, 100, -1, -4, 0x7fffffff, (int)0x80000000 };
    for (unsigned i = 0; i < sizeof bars / sizeof *bars; i++) {
        SCROLLINFO si = { sizeof si, SI_ALL_BITS, 0, 100, 10, 5, 0 };
        SET_ERR(0);
        /* NtUserSetScrollInfo: nBar is compared as unsigned < 4, else UserSetLastError(ERROR_INVALID_PARAMETER). */
        EQ(f(h, bars[i], &si, FALSE), 0);
        EXPECT_ERR(E_INVALID_PARAM);
    }
    U(DestroyWindow)(h);
}

TEST(si_stub_invalid_hwnd) {
    FARPROC p = sib_stub("NtUserSetScrollInfo");
    REQUIRE(p); REQUIRE(U(CreateWindowExW));
    NtUserSetScrollInfo_pfn f = (NtUserSetScrollInfo_pfn)p;
    HWND gone = sib_mk(WS_OVERLAPPEDWINDOW, 0, 100, 100, 0); SIB_NEED_WND(gone);
    U(DestroyWindow)(gone);
    HWND bad[] = { NULL, (HWND)(ULONG_PTR)0x10, (HWND)(ULONG_PTR)-1, (HWND)(ULONG_PTR)0xdeadbeef, gone };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        SCROLLINFO si = { sizeof si, SI_ALL_BITS, 0, 100, 10, 5, 0 };
        SET_ERR(0);
        /* ValidateHwnd fails first: result 0, ERROR_INVALID_WINDOW_HANDLE. */
        EQ(f(bad[i], SB_HORZ, &si, FALSE), 0);
        EXPECT_ERR(E_INVALID_HWND);
    }
}

TEST(si_stub_sb_both_acts_vertical) {
    FARPROC p = sib_stub("NtUserSetScrollInfo");
    REQUIRE(p); REQUIRE(U(CreateWindowExW)); REQUIRE(U(GetScrollInfo));
    NtUserSetScrollInfo_pfn f = (NtUserSetScrollInfo_pfn)p;
    HWND h = sib_mk(WS_OVERLAPPEDWINDOW, 0, 200, 200, 0); SIB_NEED_WND(h);
    SCROLLINFO si = { sizeof si, SI_ALL_BITS, 0, 100, 10, 33, 0 };
    /* SetScrollInfo only distinguishes nBar==0 (horizontal) from nonzero: SB_BOTH lands on the vertical SBDATA. */
    EQ(f(h, SB_BOTH, &si, FALSE), 33);
    SCROLLINFO g;
    CHECK(gsi(h, SB_VERT, SIF_ALL, &g) && g.nPos == 33 && g.nMax == 100);
    SET_ERR(0);
    int mn = 1, mx = 1;
    CHECK(U(GetScrollRange)(h, SB_HORZ, &mn, &mx));
    CHECK(mn == 0 && mx == 100);       /* the untouched horizontal bar keeps the default range of a new scroll info */
    U(DestroyWindow)(h);
}

TEST(si_stub_desktop_window_ignored) {
    FARPROC p = sib_stub("NtUserSetScrollInfo");
    REQUIRE(p); REQUIRE(U(GetDesktopWindow));
    NtUserSetScrollInfo_pfn f = (NtUserSetScrollInfo_pfn)p;
    SCROLLINFO si = { sizeof si, SI_ALL_BITS, 0, 100, 10, 5, 0 };
    /* fnid FNID_DESKTOP (0x29d) is filtered before the bar is looked at: 0 and no SetLastError. */
    SET_ERR(0);
    EQ(f(U(GetDesktopWindow)(), SB_VERT, &si, FALSE), 0);
    EXPECT_ERR(0);
}

/* ---- error behaviour -------------------------------------------------------------------- */

TEST(si_no_scrollbars_without_range) {
    REQUIRE(U(SetScrollInfo)); REQUIRE(U(CreateWindowExW));
    HWND h = sib_mk(WS_OVERLAPPED, 0, 100, 100, 0); SIB_NEED_WND(h);
    static const UINT masks[] = { SIF_POS, SIF_PAGE, SIF_POS | SIF_PAGE, SIF_DISABLENOSCROLL, SIF_TRACKPOS, 0 };
    for (unsigned i = 0; i < sizeof masks / sizeof *masks; i++) {
        SCROLLINFO si = { sizeof si, masks[i], 0, 0, 5, 5, 0 };
        SET_ERR(0);
        /* SetScrollInfo: no SIF_RANGE, no WS_xSCROLL style and no scroll info yet => ERROR_NO_SCROLLBARS, return 0. */
        EQ(U(SetScrollInfo)(h, SB_HORZ, &si, FALSE), 0);
        EXPECT_ERR(E_NO_SCROLLBARS);
        SET_ERR(0);
        EQ(U(SetScrollInfo)(h, SB_VERT, &si, FALSE), 0);
        EXPECT_ERR(E_NO_SCROLLBARS);
    }
    U(DestroyWindow)(h);
}

TEST(si_get_without_sbinfo) {
    REQUIRE(U(GetScrollInfo)); REQUIRE(U(GetScrollPos)); REQUIRE(U(GetScrollRange)); REQUIRE(U(CreateWindowExW));
    HWND h = sib_mk(WS_OVERLAPPED, 0, 100, 100, 0); SIB_NEED_WND(h);
    for (int bar = SB_HORZ; bar <= SB_VERT; bar++) {
        SCROLLINFO si;
        SET_ERR(0);
        /* RealGetScrollInfo: pSBInfo == NULL => ERROR_NO_SCROLLBARS, 0. */
        CHECK(!gsi(h, bar, SIF_ALL, &si));
        EXPECT_ERR(E_NO_SCROLLBARS);
        SET_ERR(0);
        EQ(U(GetScrollPos)(h, bar), 0);
        EXPECT_ERR(E_NO_SCROLLBARS);
        int mn = 77, mx = 77;
        SET_ERR(0);
        /* GetScrollRange still reports TRUE, with zeroed outputs, after recording the error. */
        CHECK(U(GetScrollRange)(h, bar, &mn, &mx));
        CHECK(mn == 0 && mx == 0);
        EXPECT_ERR(E_NO_SCROLLBARS);
    }
    U(DestroyWindow)(h);
}

TEST(si_get_bad_cbsize) {
    REQUIRE(U(GetScrollInfo)); REQUIRE(U(SetScrollInfo)); REQUIRE(U(CreateWindowExW));
    HWND h = sib_mk(WS_OVERLAPPEDWINDOW, 0, 200, 200, 0); SIB_NEED_WND(h);
    SCROLLINFO set = { sizeof set, SI_ALL_BITS, 0, 100, 10, 20, 0 };
    U(SetScrollInfo)(h, SB_VERT, &set, FALSE);
    /* GetScrollInfo does not validate cbSize at all on Windows 10: every value, 0 and 0xffffffff included, returns TRUE
     * and fills the fields selected by fMask. */
    static const UINT cbs[] = { 0, 4, 0x14, 0x17, 0x18, 0x19, 0x1b, 0x1c, 0x1d, 0x20, 0x38, 0xffffffffu };
    for (unsigned i = 0; i < sizeof cbs / sizeof *cbs; i++) {
        SCROLLINFO si; memset(&si, 0, sizeof si);
        si.cbSize = cbs[i]; si.fMask = SIF_POS;
        int r = U(GetScrollInfo)(h, SB_VERT, &si);
        CHECKF(r, "cbSize %#x -> %d", cbs[i], r);
        CHECKF(si.nPos == 20, "cbSize %#x nPos %d", cbs[i], si.nPos);
    }
    /* A 0x18-byte SCROLLINFO has no nTrackPos: SIF_TRACKPOS on it is accepted (the field is not written back to the caller). */
    SCROLLINFO s18; memset(&s18, 0xAB, sizeof s18);
    s18.cbSize = 0x18; s18.fMask = SIF_POS;
    CHECK(U(GetScrollInfo)(h, SB_VERT, &s18) && s18.nPos == 20);
    U(DestroyWindow)(h);
}

TEST(si_get_bad_mask) {
    REQUIRE(U(GetScrollInfo)); REQUIRE(U(SetScrollInfo)); REQUIRE(U(CreateWindowExW));
    HWND h = sib_mk(WS_OVERLAPPEDWINDOW, 0, 200, 200, 0); SIB_NEED_WND(h);
    SCROLLINFO set = { sizeof set, SI_ALL_BITS, 0, 100, 10, 20, 0 };
    U(SetScrollInfo)(h, SB_VERT, &set, FALSE);
    /* Undefined fMask bits are not rejected on Windows 10: every single-bit addition to SIF_POS succeeds and the
     * position is returned. */
    for (int b = 0; b < 32; b++) {
        SCROLLINFO si; memset(&si, 0, sizeof si);
        si.cbSize = sizeof si; si.fMask = SIF_POS | (1u << b);
        int r = U(GetScrollInfo)(h, SB_VERT, &si);
        CHECKF(r, "fMask bit %d -> %d", b, r);
        CHECKF(si.nPos == 20, "fMask bit %d nPos %d", b, si.nPos);
    }
    U(DestroyWindow)(h);
}

TEST(si_get_mask_without_data_bits) {
    REQUIRE(U(GetScrollInfo)); REQUIRE(U(SetScrollInfo)); REQUIRE(U(CreateWindowExW));
    HWND h = sib_mk(WS_OVERLAPPEDWINDOW, 0, 200, 200, 0); SIB_NEED_WND(h);
    SCROLLINFO set = { sizeof set, SI_ALL_BITS, 0, 100, 10, 20, 0 };
    U(SetScrollInfo)(h, SB_VERT, &set, FALSE);
    static const UINT masks[] = { 0, SIF_DISABLENOSCROLL, 0x1000, 0x2000, 0x4000, 0x7000 | SIF_DISABLENOSCROLL };
    for (unsigned i = 0; i < sizeof masks / sizeof *masks; i++) {
        SCROLLINFO si; memset(&si, 0x5A, sizeof si);
        si.cbSize = sizeof si; si.fMask = masks[i];
        /* NtUserSBGetParms returns (fMask & 0x17) != 0; none of the data bits requested => FALSE and nothing written. */
        CHECKF(!U(GetScrollInfo)(h, SB_VERT, &si), "mask %#x", masks[i]);
        CHECK(si.nMin == 0x5A5A5A5A && si.nMax == 0x5A5A5A5A && si.nPage == 0x5A5A5A5A && si.nPos == 0x5A5A5A5A);
    }
    U(DestroyWindow)(h);
}

TEST(si_get_partial_masks) {
    REQUIRE(U(GetScrollInfo)); REQUIRE(U(SetScrollInfo)); REQUIRE(U(CreateWindowExW));
    HWND h = sib_mk(WS_OVERLAPPEDWINDOW, 0, 200, 200, 0); SIB_NEED_WND(h);
    SCROLLINFO set = { sizeof set, SI_ALL_BITS, -10, 90, 12, 33, 0 };
    U(SetScrollInfo)(h, SB_HORZ, &set, FALSE);
    for (UINT m = 1; m < 0x20; m++) {
        SCROLLINFO si; memset(&si, 0x77, sizeof si);
        si.cbSize = sizeof si; si.fMask = m;
        CHECK(U(GetScrollInfo)(h, SB_HORZ, &si) == !!(m & 0x17));
        /* Only the fields selected by the mask are written; the others keep the caller's sentinel. */
        int rng = !!(m & SIF_RANGE);
        CHECKF(rng ? (si.nMin == -10 && si.nMax == 90) : (si.nMin == 0x77777777 && si.nMax == 0x77777777), "mask %#x range", m);
        CHECKF((m & SIF_PAGE) ? si.nPage == 12 : si.nPage == 0x77777777, "mask %#x page", m);
        CHECKF((m & SIF_POS) ? si.nPos == 33 : si.nPos == 0x77777777, "mask %#x pos", m);
        CHECKF((m & SIF_TRACKPOS) ? si.nTrackPos == 33 : si.nTrackPos == 0x77777777, "mask %#x track", m);
    }
    U(DestroyWindow)(h);
}

TEST(si_get_bad_bar) {
    REQUIRE(U(GetScrollInfo)); REQUIRE(U(SetScrollInfo)); REQUIRE(U(CreateWindowExW));
    HWND h = sib_mk(WS_OVERLAPPEDWINDOW, 0, 200, 200, 0); SIB_NEED_WND(h);
    static const int bars[] = { -1, 3, 4, 100, (int)0x80000000 };
    for (unsigned i = 0; i < sizeof bars / sizeof *bars; i++) {
        SCROLLINFO si; SET_ERR(0);
        CHECK(!gsi(h, bars[i], SIF_ALL, &si));
        EXPECT_ERR(E_INVALID_PARAM);
        SET_ERR(0);
        EQ(U(GetScrollPos)(h, bars[i]), 0);
        EXPECT_ERR(E_INVALID_PARAM);
        int a = 1, b = 1; SET_ERR(0);
        CHECK(!U(GetScrollRange)(h, bars[i], &a, &b));
        EXPECT_ERR(E_INVALID_PARAM);
    }
    U(DestroyWindow)(h);
}

TEST(si_get_invalid_hwnd) {
    REQUIRE(U(GetScrollInfo)); REQUIRE(U(CreateWindowExW));
    HWND gone = sib_mk(WS_OVERLAPPEDWINDOW, 0, 100, 100, 0); SIB_NEED_WND(gone);
    U(DestroyWindow)(gone);
    HWND bad[] = { NULL, (HWND)(ULONG_PTR)0x12345, (HWND)(ULONG_PTR)-1, gone };
    for (unsigned i = 0; i < 4; i++) {
        SCROLLINFO si; SET_ERR(0);
        CHECK(!gsi(bad[i], SB_VERT, SIF_ALL, &si));
        EXPECT_ERR(E_INVALID_HWND);
        SET_ERR(0);
        EQ(U(GetScrollPos)(bad[i], SB_VERT), 0);
        EXPECT_ERR(E_INVALID_HWND);
        int a = 1, b = 1; SET_ERR(0);
        CHECK(!U(GetScrollRange)(bad[i], SB_VERT, &a, &b));
        EXPECT_ERR(E_INVALID_HWND);
        SET_ERR(0);
        CHECK(!U(SetScrollRange)(bad[i], SB_VERT, 0, 10, FALSE));
        EXPECT_ERR(E_INVALID_HWND);
    }
}

/* ---- SetScrollPos / SetScrollRange wrappers ----------------------------------------------- */

TEST(si_setscrollrange_basic) {
    REQUIRE(U(SetScrollRange)); REQUIRE(U(GetScrollRange)); REQUIRE(U(CreateWindowExW));
    HWND h = sib_mk(WS_OVERLAPPEDWINDOW, 0, 200, 200, 0); SIB_NEED_WND(h);
    for (int bar = SB_HORZ; bar <= SB_VERT; bar++) {
        CHECK(U(SetScrollRange)(h, bar, 5, 55, FALSE));
        int mn = 0, mx = 0;
        CHECK(U(GetScrollRange)(h, bar, &mn, &mx));
        CHECK(mn == 5 && mx == 55);
        /* max < min is refused with ERROR_INVALID_SCROLLBAR_RANGE and the range is left as it was */
        SET_ERR(0);
        CHECK(!U(SetScrollRange)(h, bar, 9, 3, FALSE));
        EXPECT_ERR(E_BAD_SB_RANGE);
        CHECK(U(GetScrollRange)(h, bar, &mn, &mx));
        CHECKF(mn == 5 && mx == 55, "%d %d", mn, mx);
    }
    U(DestroyWindow)(h);
}

TEST(si_setscrollrange_too_wide) {
    REQUIRE(U(SetScrollRange)); REQUIRE(U(GetScrollRange)); REQUIRE(U(CreateWindowExW));
    HWND h = sib_mk(WS_OVERLAPPEDWINDOW, 0, 200, 200, 0); SIB_NEED_WND(h);
    U(SetScrollRange)(h, SB_VERT, 1, 2, FALSE);
    static const struct { int mn, mx; } r[] = { { -2000000000, 2000000000 }, { (int)0x80000000, 0 }, { 0, (int)0x80000000 }, { (int)0x80000000, 0x7fffffff } };
    for (unsigned i = 0; i < sizeof r / sizeof *r; i++) {
        SET_ERR(0);
        /* user32!SetScrollRange: (UINT)(max - min) >= 0x80000000 => ERROR_INVALID_SCROLLBAR_RANGE, FALSE, no kernel call. */
        CHECKF(!U(SetScrollRange)(h, SB_VERT, r[i].mn, r[i].mx, FALSE), "range %d..%d", r[i].mn, r[i].mx);
        EXPECT_ERR(E_BAD_SB_RANGE);
    }
    int mn = 0, mx = 0;
    CHECK(U(GetScrollRange)(h, SB_VERT, &mn, &mx) && mn == 1 && mx == 2);
    /* The largest representable span is accepted. */
    CHECK(U(SetScrollRange)(h, SB_VERT, -1, 0x7ffffffe, FALSE));
    CHECK(U(SetScrollRange)(h, SB_VERT, 0, 0x7fffffff, FALSE));
    U(DestroyWindow)(h);
}

TEST(si_setscrollrange_clamps_pos) {
    REQUIRE(U(SetScrollRange)); REQUIRE(U(SetScrollPos)); REQUIRE(U(GetScrollPos)); REQUIRE(U(CreateWindowExW));
    HWND h = sib_mk(WS_OVERLAPPEDWINDOW, 0, 200, 200, 0); SIB_NEED_WND(h);
    U(SetScrollRange)(h, SB_HORZ, 0, 100, FALSE);
    U(SetScrollPos)(h, SB_HORZ, 80, FALSE);
    CHECK(U(GetScrollPos)(h, SB_HORZ) == 80);
    U(SetScrollRange)(h, SB_HORZ, 0, 50, FALSE);
    CHECKF(U(GetScrollPos)(h, SB_HORZ) == 50, "pos %d", U(GetScrollPos)(h, SB_HORZ));
    U(SetScrollRange)(h, SB_HORZ, 60, 70, FALSE);
    CHECK(U(GetScrollPos)(h, SB_HORZ) == 60);
    U(DestroyWindow)(h);
}

TEST(si_setscrollpos_returns_previous) {
    REQUIRE(U(SetScrollRange)); REQUIRE(U(SetScrollPos)); REQUIRE(U(GetScrollPos)); REQUIRE(U(CreateWindowExW));
    HWND h = sib_mk(WS_OVERLAPPEDWINDOW, 0, 200, 200, 0); SIB_NEED_WND(h);
    for (int bar = SB_HORZ; bar <= SB_VERT; bar++) {
        U(SetScrollRange)(h, bar, 0, 100, FALSE);
        /* SetScrollPos builds SIF_POS|SIF_RETURNPREV (0x1004): returns the old position, then stores the clamped new one. */
        EQ(U(SetScrollPos)(h, bar, 30, FALSE), 0);
        EQ(U(SetScrollPos)(h, bar, 70, FALSE), 30);
        EQ(U(SetScrollPos)(h, bar, 500, FALSE), 70);
        EQ(U(GetScrollPos)(h, bar), 100);
        EQ(U(SetScrollPos)(h, bar, -5, FALSE), 100);
        EQ(U(GetScrollPos)(h, bar), 0);
    }
    U(DestroyWindow)(h);
}

TEST(si_setscrollpos_without_range_errors) {
    REQUIRE(U(SetScrollPos)); REQUIRE(U(CreateWindowExW));
    HWND h = sib_mk(WS_OVERLAPPED, 0, 100, 100, 0); SIB_NEED_WND(h);
    SET_ERR(0);
    EQ(U(SetScrollPos)(h, SB_VERT, 5, FALSE), 0);
    EXPECT_ERR(E_NO_SCROLLBARS);
    U(DestroyWindow)(h);
}

TEST(si_redraw_flag_irrelevant_to_state) {
    REQUIRE(U(SetScrollInfo)); REQUIRE(U(GetScrollInfo)); REQUIRE(U(CreateWindowExW));
    for (int redraw = 0; redraw < 2; redraw++) {
        HWND h = sib_mk(WS_OVERLAPPEDWINDOW, 0, 200, 200, 0); SIB_NEED_WND(h);
        SCROLLINFO si = { sizeof si, SI_ALL_BITS, 0, 100, 10, 42, 0 };
        EQ(U(SetScrollInfo)(h, SB_VERT, &si, redraw), 42);
        SCROLLINFO g; CHECK(gsi(h, SB_VERT, SIF_ALL, &g));
        CHECK(g.nPos == 42 && g.nPage == 10 && g.nMax == 100);
        U(DestroyWindow)(h);
    }
}

/* ---- style bits driven by the kernel ------------------------------------------------------- */

static DWORD style_of(HWND h) { return (DWORD)U(GetWindowLongW)(h, GWL_STYLE); }

TEST(si_style_set_when_range_needs_bar) {
    REQUIRE(U(SetScrollInfo)); REQUIRE(U(GetWindowLongW)); REQUIRE(U(CreateWindowExW));
    HWND h = sib_mk(WS_OVERLAPPED, 0, 200, 200, 0); SIB_NEED_WND(h);
    CHECK(!(style_of(h) & (WS_HSCROLL | WS_VSCROLL)));
    SCROLLINFO v = { sizeof v, SI_ALL_BITS, 0, 100, 10, 0, 0 };
    U(SetScrollInfo)(h, SB_VERT, &v, FALSE);
    /* SetScrollInfo: page (10) <= max-min (100) => bar needed => WS_VSCROLL set; horizontal untouched. */
    CHECK(style_of(h) & WS_VSCROLL);
    CHECK(!(style_of(h) & WS_HSCROLL));
    SCROLLINFO hh = { sizeof hh, SI_ALL_BITS, 0, 100, 10, 0, 0 };
    U(SetScrollInfo)(h, SB_HORZ, &hh, FALSE);
    CHECK((style_of(h) & (WS_HSCROLL | WS_VSCROLL)) == (WS_HSCROLL | WS_VSCROLL));
    U(DestroyWindow)(h);
}

TEST(si_style_cleared_when_page_covers_range) {
    REQUIRE(U(SetScrollInfo)); REQUIRE(U(GetWindowLongW)); REQUIRE(U(CreateWindowExW));
    HWND h = sib_mk(WS_OVERLAPPED, 0, 200, 200, 0); SIB_NEED_WND(h);
    SCROLLINFO v = { sizeof v, SI_ALL_BITS, 0, 100, 10, 0, 0 };
    U(SetScrollInfo)(h, SB_VERT, &v, FALSE);
    CHECK(style_of(h) & WS_VSCROLL);
    SCROLLINFO p = { sizeof p, SIF_PAGE, 0, 0, 101, 0, 0 };
    U(SetScrollInfo)(h, SB_VERT, &p, FALSE);
    /* page 101 > max-min (100): scroll-parameter code reports "no bar needed" and the style flag is dropped. */
    CHECK(!(style_of(h) & WS_VSCROLL));
    U(DestroyWindow)(h);
}

TEST(si_style_cleared_when_range_empty) {
    REQUIRE(U(SetScrollInfo)); REQUIRE(U(GetWindowLongW)); REQUIRE(U(CreateWindowExW));
    HWND h = sib_mk(WS_OVERLAPPED, 0, 200, 200, 0); SIB_NEED_WND(h);
    SCROLLINFO v = { sizeof v, SI_ALL_BITS, 0, 100, 10, 0, 0 };
    U(SetScrollInfo)(h, SB_HORZ, &v, FALSE);
    CHECK(style_of(h) & WS_HSCROLL);
    SCROLLINFO r = { sizeof r, SIF_RANGE, 5, 5, 0, 0, 0 };
    U(SetScrollInfo)(h, SB_HORZ, &r, FALSE);
    CHECK(!(style_of(h) & WS_HSCROLL));
    U(DestroyWindow)(h);
}

TEST(si_style_page_boundary) {
    REQUIRE(U(SetScrollInfo)); REQUIRE(U(GetWindowLongW)); REQUIRE(U(CreateWindowExW));
    HWND h = sib_mk(WS_OVERLAPPED, 0, 200, 200, 0); SIB_NEED_WND(h);
    /* Bar needed iff page <= max - min (not max - min + 1). */
    SCROLLINFO a = { sizeof a, SI_ALL_BITS, 0, 100, 100, 0, 0 };
    U(SetScrollInfo)(h, SB_VERT, &a, FALSE);
    CHECK(style_of(h) & WS_VSCROLL);
    SCROLLINFO b = { sizeof b, SIF_PAGE, 0, 0, 101, 0, 0 };
    U(SetScrollInfo)(h, SB_VERT, &b, FALSE);
    CHECK(!(style_of(h) & WS_VSCROLL));
    SCROLLINFO c = { sizeof c, SIF_PAGE, 0, 0, 100, 0, 0 };
    U(SetScrollInfo)(h, SB_VERT, &c, FALSE);
    CHECK(style_of(h) & WS_VSCROLL);
    U(DestroyWindow)(h);
}

TEST(si_disablenoscroll_keeps_style) {
    REQUIRE(U(SetScrollInfo)); REQUIRE(U(GetWindowLongW)); REQUIRE(U(CreateWindowExW));
    HWND h = sib_mk(WS_OVERLAPPED, 0, 200, 200, 0); SIB_NEED_WND(h);
    SCROLLINFO v = { sizeof v, SI_ALL_BITS, 0, 100, 10, 0, 0 };
    U(SetScrollInfo)(h, SB_VERT, &v, FALSE);
    CHECK(style_of(h) & WS_VSCROLL);
    SCROLLINFO p = { sizeof p, SIF_PAGE | SIF_DISABLENOSCROLL, 0, 0, 101, 0, 0 };
    U(SetScrollInfo)(h, SB_VERT, &p, FALSE);
    /* SIF_DISABLENOSCROLL: a bar that exists is kept (SetOrClrWF) and its arrows are disabled instead of the bar being removed. */
    CHECK(style_of(h) & WS_VSCROLL);
    if (!t_is_wine()) {
        struct sb_info sb; int pres = 0;
        if (sib_read_sbinfo(h, &sb, &pres) && pres) CHECKF((sb.flags & 0xC) == 0xC, "WSBflags %#x", sb.flags);
    }
    U(DestroyWindow)(h);
}

TEST(si_disablenoscroll_without_bar_stays_hidden) {
    REQUIRE(U(SetScrollInfo)); REQUIRE(U(GetWindowLongW)); REQUIRE(U(CreateWindowExW));
    HWND h = sib_mk(WS_OVERLAPPED, 0, 200, 200, 0); SIB_NEED_WND(h);
    SCROLLINFO v = { sizeof v, SI_ALL_BITS | SIF_DISABLENOSCROLL, 0, 5, 100, 0, 0 };
    U(SetScrollInfo)(h, SB_VERT, &v, FALSE);
 /* No bar existed, so there is nothing to keep: the style flag stays clear. */
    CHECK(!(style_of(h) & WS_VSCROLL));
    U(DestroyWindow)(h);
}

TEST(si_preexisting_style_range_only) {
    REQUIRE(U(SetScrollInfo)); REQUIRE(U(GetScrollInfo)); REQUIRE(U(CreateWindowExW));
    HWND h = sib_mk(WS_OVERLAPPED | WS_VSCROLL | WS_HSCROLL, 0, 200, 200, 0); SIB_NEED_WND(h);
    /* With the style bits present the "no scroll bars" gate (needs SIF_RANGE or style or SBINFO) is passed even for SIF_POS. */
    SCROLLINFO si = { sizeof si, SIF_POS, 0, 0, 0, 3, 0 };
    SET_ERR(0);
    int r = U(SetScrollInfo)(h, SB_VERT, &si, FALSE);
    CHECKF(GetLastError() != E_NO_SCROLLBARS, "ret %d err %lu", r, GetLastError());
    U(DestroyWindow)(h);
}

/* ---- scroll info client view ------------------------------------------------------------------ */

TEST(si_sbinfo_view_matches_api) {
    REQUIRE(U(SetScrollInfo)); REQUIRE(U(CreateWindowExW));
    if (t_is_wine()) { t_skip("no desktop-heap scroll info under Wine"); return; }
    HWND h = sib_mk(WS_OVERLAPPEDWINDOW, 0, 200, 200, 0); SIB_NEED_WND(h);
    struct sb_info sb; int pres = 1;
    if (!sib_read_sbinfo(h, &sb, &pres)) { t_skip("tagWND client view unavailable"); U(DestroyWindow)(h); return; }
    /* Fresh window: tagWND.pSBInfo (+0x90) is NULL until the first range set / ShowScrollBar. */
    CHECK(!pres);
    SCROLLINFO a = { sizeof a, SI_ALL_BITS, 3, 203, 20, 40, 0 };
    SCROLLINFO b = { sizeof b, SI_ALL_BITS, -7, 17, 4, 9, 0 };
    U(SetScrollInfo)(h, SB_HORZ, &a, FALSE);
    U(SetScrollInfo)(h, SB_VERT, &b, FALSE);
    CHECK(sib_read_sbinfo(h, &sb, &pres) && pres);
    CHECK(sb.h.min == 3 && sb.h.max == 203 && sb.h.page == 20 && sb.h.pos == 40);
    CHECK(sb.v.min == -7 && sb.v.max == 17 && sb.v.page == 4 && sb.v.pos == 9);
    CHECK((sb.flags & 0xF) == 0);
    U(DestroyWindow)(h);
}

TEST(si_sbinfo_pointer_in_heap_range) {
    REQUIRE(U(SetScrollInfo)); REQUIRE(U(CreateWindowExW));
    if (t_is_wine()) { t_skip("no desktop-heap scroll info under Wine"); return; }
    HWND h = sib_mk(WS_OVERLAPPEDWINDOW, 0, 200, 200, 0); SIB_NEED_WND(h);
    SCROLLINFO a = { sizeof a, SI_ALL_BITS, 0, 10, 2, 1, 0 };
    U(SetScrollInfo)(h, SB_VERT, &a, FALSE);
    char *w = (char *)k_wnd_ptr(h);
    if (!w) { t_skip("tagWND client view unavailable"); U(DestroyWindow)(h); return; }
    void *self = NULL, *sb = NULL;
    CHECK(k_try_read_ptr(w + SIB_W_PSELF, &self) && k_try_read_ptr(w + SIB_W_SBINFO, &sb));
    /* scroll info lives in the same desktop heap as the window: pSBInfo - pSelf is a small signed distance, and the 0x24-byte block is readable. */
    CHECK(sb != NULL);
    LONG_PTR dist = (LONG_PTR)sb - (LONG_PTR)self;
    CHECKF(dist > -(1LL << 31) && dist < (1LL << 31), "distance %lld", (long long)dist);
    unsigned char buf[0x24];
    CHECK(k_try_read(w + dist, buf, sizeof buf));
    U(DestroyWindow)(h);
}

TEST(si_sbinfo_independent_per_window) {
    REQUIRE(U(SetScrollInfo)); REQUIRE(U(GetScrollPos)); REQUIRE(U(CreateWindowExW));
    HWND a = sib_mk(WS_OVERLAPPEDWINDOW, 0, 100, 100, 0), b = sib_mk(WS_OVERLAPPEDWINDOW, 0, 100, 100, 0);
    if (!a || !b) { t_skip("window"); return; }
    SCROLLINFO s1 = { sizeof s1, SI_ALL_BITS, 0, 100, 10, 10, 0 }, s2 = { sizeof s2, SI_ALL_BITS, 0, 100, 10, 60, 0 };
    U(SetScrollInfo)(a, SB_VERT, &s1, FALSE);
    U(SetScrollInfo)(b, SB_VERT, &s2, FALSE);
    CHECK(U(GetScrollPos)(a, SB_VERT) == 10);
    CHECK(U(GetScrollPos)(b, SB_VERT) == 60);
    U(DestroyWindow)(a); U(DestroyWindow)(b);
}

/* ---- SB_CTL (scroll bar control) ---------------------------------------------------------- */

TEST(si_ctl_roundtrip) {
    REQUIRE(U(SetScrollInfo)); REQUIRE(U(GetScrollInfo)); REQUIRE(U(CreateWindowExW));
    HWND p = sib_mk(WS_OVERLAPPEDWINDOW, 0, 300, 300, 0); SIB_NEED_WND(p);
    HWND c = sib_child(p, L"ScrollBar", SBS_VERT, 0, 0, 20, 100);
    if (!c) { t_skip("ScrollBar control"); U(DestroyWindow)(p); return; }
    SCROLLINFO si = { sizeof si, SI_ALL_BITS, 0, 100, 10, 55, 0 };
    /* nBar == SB_CTL => SetScrollInfo sends SBM_SETSCROLLINFO to the control; the control clamps like a window bar. */
    int r = U(SetScrollInfo)(c, SB_CTL, &si, FALSE);
    CHECKF(r == 55, "ret %d", r);
    SCROLLINFO g;
    CHECK(gsi(c, SB_CTL, SIF_ALL, &g));
    CHECK(g.nMin == 0 && g.nMax == 100 && g.nPage == 10 && g.nPos == 55);
    CHECK(U(GetScrollPos)(c, SB_CTL) == 55);
    CHECK((int)SendMessageW(c, SBM_GETPOS, 0, 0) == 55);
    int mn = -1, mx = -1;
    CHECK(U(GetScrollRange)(c, SB_CTL, &mn, &mx) && mn == 0 && mx == 100);
    SCROLLINFO big = { sizeof big, SIF_POS, 0, 0, 0, 5000, 0 };
    r = U(SetScrollInfo)(c, SB_CTL, &big, FALSE);
    CHECKF(r == 91, "clamped pos %d", r);
    U(DestroyWindow)(p);
}

TEST(si_ctl_on_non_scrollbar) {
    REQUIRE(U(SetScrollInfo)); REQUIRE(U(GetScrollInfo)); REQUIRE(U(CreateWindowExW));
    HWND p = sib_mk(WS_OVERLAPPEDWINDOW, 0, 300, 300, 0); SIB_NEED_WND(p);
    static const wchar_t *const cls[] = { L"Static", L"Button", L"Edit" };
    for (unsigned i = 0; i < 3; i++) {
        HWND c = sib_child(p, cls[i], 0, 0, 0, 50, 20);
        if (!c) continue;
        SCROLLINFO si = { sizeof si, SI_ALL_BITS, 0, 100, 10, 55, 0 };
        /* SB_CTL is forwarded as a message (SBM_SETSCROLLINFO); a control that does not implement it
         * returns 0 from DefWindowProc, so the "set" reports 0 and the window gains no scroll state. */
        EQ(U(SetScrollInfo)(c, SB_CTL, &si, FALSE), 0);
        CHECK(!(U(GetWindowLongW)(c, GWL_STYLE) & (WS_HSCROLL | WS_VSCROLL)));
        SCROLLINFO g; memset(&g, 0, sizeof g); g.cbSize = sizeof g; g.fMask = SIF_ALL;
        /* RealGetScrollInfo: for SB_CTL it sends SBM_GETSCROLLINFO and returns TRUE unconditionally. */
        CHECK(U(GetScrollInfo)(c, SB_CTL, &g));
        EQ(U(GetScrollPos)(c, SB_CTL), 0);
    }
    U(DestroyWindow)(p);
}

TEST(si_ctl_default_range) {
    REQUIRE(U(GetScrollRange)); REQUIRE(U(CreateWindowExW));
    HWND p = sib_mk(WS_OVERLAPPEDWINDOW, 0, 300, 300, 0); SIB_NEED_WND(p);
    HWND c = sib_child(p, L"ScrollBar", SBS_HORZ, 0, 0, 100, 20);
    if (!c) { t_skip("ScrollBar control"); U(DestroyWindow)(p); return; }
    int mn = -5, mx = -5;
    CHECK(U(GetScrollRange)(c, SB_CTL, &mn, &mx));
    t_note("fresh ScrollBar control range %d..%d", mn, mx);
    CHECK(mn <= mx);
    U(DestroyWindow)(p);
}
