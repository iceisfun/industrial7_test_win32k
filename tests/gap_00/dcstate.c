/* NtGdiGetDCDpiScaleValue, NtGdiGetDeviceWidth, NtGdiSetVirtualResolution, NtGdiSetIcmMode, NtGdiMakeInfoDC.
 *
 * All five lock a DC by handle by locking it and fail soft on anything that is not a live DC of this process.
 * SetIcmMode stores into the client-visible DC page, mapped into the process as GDICELL.pUser, so its effect
 * is checked by reading that page back. SetVirtualResolution leaves no client-visible trace on Windows 10, so only its
 * return values are checked. */
#include "gap0.h"

/* ---- NtGdiGetDCDpiScaleValue(hdc): returns 1 when the DC cannot be locked ------ */
TEST(gap0_dpiscale_bad_handles_return_one) {
    REQUIRE(UN(NtGdiGetDCDpiScaleValue));
    for (unsigned i = 0; i < G0_NBAD; i++) {
        LONG r = UN(NtGdiGetDCDpiScaleValue)((HDC)g0_bad_handles[i]);
        CHECKF(r == 1, "handle 0x%llx -> %ld (returns 1 when the DC cannot be locked)", (unsigned long long)g0_bad_handles[i], r);
    }
}
TEST(gap0_dpiscale_stale_dc_returns_one) {
    REQUIRE(UN(NtGdiGetDCDpiScaleValue)); REQUIRE(U(CreateCompatibleDC));
    HDC d = g0_stale_dc();
    REQUIRE(d);
    EXPECT_EQ((LONG)UN(NtGdiGetDCDpiScaleValue)(d), 1);
}
TEST(gap0_dpiscale_non_dc_object_returns_one) {
    REQUIRE(UN(NtGdiGetDCDpiScaleValue)); REQUIRE(U(CreateSolidBrush)); REQUIRE(U(CreateBitmap)); REQUIRE(U(CreatePen));
    HGDIOBJ o[4] = { U(CreateSolidBrush)(RGB(1, 2, 3)), U(CreateBitmap)(2, 2, 1, 1, NULL), U(CreatePen)(PS_SOLID, 1, 0), U(GetStockObject)(WHITE_BRUSH) };
    for (int i = 0; i < 4; i++) {
        CHECKF(o[i] != NULL, "object %d", i);
        if (o[i]) EXPECT_EQ((LONG)UN(NtGdiGetDCDpiScaleValue)((HDC)o[i]), 1);
    }
    for (int i = 0; i < 3; i++) if (o[i]) U(DeleteObject)(o[i]);
}
TEST(gap0_dpiscale_memdc_equals_dcattr_cached_value) {
    REQUIRE(UN(NtGdiGetDCDpiScaleValue));
    G0FX(f, 8, 8);
    LONG r = UN(NtGdiGetDCDpiScaleValue)(f.dc);
    void *a = g0_dca(f.dc);
    if (a) EXPECT_EQ(r, (LONG)g0_dca_i32(a, DCA_DPISCALE));   /* the cached DPI scale value */
    else t_note("DC_ATTR page not reachable (not the Windows layout)");
    t_note("memory DC DPI scale value %ld", r);
    g0fx_close(&f);
}
TEST(gap0_dpiscale_screen_dc_equals_dcattr_cached_value) {
    REQUIRE(UN(NtGdiGetDCDpiScaleValue)); REQUIRE(U(GetDC)); REQUIRE(U(ReleaseDC));
    HDC dc = U(GetDC)(NULL);
    REQUIRE(dc);
    LONG r = UN(NtGdiGetDCDpiScaleValue)(dc);
    void *a = g0_dca(dc);
    if (a) EXPECT_EQ(r, (LONG)g0_dca_i32(a, DCA_DPISCALE));
    t_note("screen DC DPI scale value %ld", r);
    U(ReleaseDC)(NULL, dc);
}
TEST(gap0_dpiscale_user_wrapper_agrees_with_syscall) {
    /* GetDCDpiScaleValue answers from the client-visible DC page on its fast path and falls back to the syscall otherwise */
    REQUIRE(UN(NtGdiGetDCDpiScaleValue)); REQUIRE(UN(GetDCDpiScaleValue));
    G0FX(f, 4, 4);
    EXPECT_EQ((LONG)UN(GetDCDpiScaleValue)(f.dc), (LONG)UN(NtGdiGetDCDpiScaleValue)(f.dc));
    g0fx_close(&f);
}
TEST(gap0_dpiscale_user_wrapper_null_returns_one) {
    REQUIRE(UN(GetDCDpiScaleValue));
    EXPECT_EQ((LONG)UN(GetDCDpiScaleValue)(NULL), 1);   /* gdi32full!GetDCDpiScaleValue: if (hdc == 0) return 1 */
}
TEST(gap0_dpiscale_stable_across_calls_and_bitmap_selection) {
    REQUIRE(UN(NtGdiGetDCDpiScaleValue));
    G0FX(f, 8, 8);
    LONG a = UN(NtGdiGetDCDpiScaleValue)(f.dc);
    HBITMAP b2 = g0_dib(f.dc, 16, 16, 24);
    REQUIRE(b2);
    HGDIOBJ prev = U(SelectObject)(f.dc, b2);
    LONG b = UN(NtGdiGetDCDpiScaleValue)(f.dc);
    U(SelectObject)(f.dc, prev);
    U(DeleteObject)(b2);
    EXPECT_EQ(a, b);
    EXPECT_EQ(a, (LONG)UN(NtGdiGetDCDpiScaleValue)(f.dc));
    g0fx_close(&f);
}
TEST(gap0_dpiscale_two_dcs_same_value) {
    REQUIRE(UN(NtGdiGetDCDpiScaleValue));
    G0FX(f, 2, 2);
    HDC d2 = U(CreateCompatibleDC)(NULL);
    REQUIRE(d2);
    EXPECT_EQ((LONG)UN(NtGdiGetDCDpiScaleValue)(d2), (LONG)UN(NtGdiGetDCDpiScaleValue)(f.dc));
    U(DeleteDC)(d2);
    g0fx_close(&f);
}

/* ---- NtGdiGetDeviceWidth(hdc): -1 when the DC does not lock, else DC+0x410 - DC+0x408 ------------------------- */
TEST(gap0_devwidth_bad_handles_return_minus_one) {
    REQUIRE(UN(NtGdiGetDeviceWidth));
    for (unsigned i = 0; i < G0_NBAD; i++) {
        INT r = UN(NtGdiGetDeviceWidth)((HDC)g0_bad_handles[i]);
        CHECKF(r == -1, "handle 0x%llx -> %d", (unsigned long long)g0_bad_handles[i], r);
    }
}
TEST(gap0_devwidth_stale_dc_minus_one) {
    REQUIRE(UN(NtGdiGetDeviceWidth)); REQUIRE(U(CreateCompatibleDC));
    HDC d = g0_stale_dc();
    REQUIRE(d);
    EXPECT_EQ(UN(NtGdiGetDeviceWidth)(d), -1);
}
TEST(gap0_devwidth_memdc_valid_and_stable) {
    REQUIRE(UN(NtGdiGetDeviceWidth));
    G0FX(f, 37, 23);
    INT w = UN(NtGdiGetDeviceWidth)(f.dc);
    CHECKF(w >= 0, "width %d (-1 is reserved for a DC that does not lock)", w);
    EXPECT_EQ(UN(NtGdiGetDeviceWidth)(f.dc), w);
    t_note("device width of a 37x23 memory DC = %d", w);
    g0fx_close(&f);
}
TEST(gap0_devwidth_follows_selected_bitmap_note) {
    REQUIRE(UN(NtGdiGetDeviceWidth));
    G0FX(f, 37, 23);
    INT w1 = UN(NtGdiGetDeviceWidth)(f.dc);
    HBITMAP big = g0_dib(f.dc, 101, 9, 32);
    REQUIRE(big);
    HGDIOBJ prev = U(SelectObject)(f.dc, big);
    INT w2 = UN(NtGdiGetDeviceWidth)(f.dc);
    U(SelectObject)(f.dc, prev);
    U(DeleteObject)(big);
    CHECK(w1 >= 0 && w2 >= 0);
    t_note("device width 37-wide bitmap=%d, 101-wide bitmap=%d (rect right-left of the DC)", w1, w2);
    g0fx_close(&f);
}
TEST(gap0_devwidth_screen_dc_nonnegative) {
    REQUIRE(UN(NtGdiGetDeviceWidth)); REQUIRE(U(GetDC));
    HDC dc = U(GetDC)(NULL);
    REQUIRE(dc);
    INT w = UN(NtGdiGetDeviceWidth)(dc);
    CHECKF(w >= 0, "width %d", w);
    t_note("screen DC device width %d, GetSystemMetrics(SM_CXSCREEN)=%d", w, U(GetSystemMetrics) ? U(GetSystemMetrics)(SM_CXSCREEN) : -1);
    U(ReleaseDC)(NULL, dc);
}

/* ---- NtGdiSetVirtualResolution(hdc, cxPx, cyPx, cxMm, cyMm) ------------------------------------------------------
 * Either all four values are zero (reset) or all four are non-zero; anything mixed returns 0 before the DC is even
 * locked. Valid vectors return 1. */
struct vres { int a, b, c, d; int ok; };
static const struct vres vres_vec[] = {
    { 0, 0, 0, 0, 1 },
    { 800, 600, 200, 150, 1 },
    { 1, 1, 1, 1, 1 },
    { 0x7fffffff, 0x7fffffff, 0x7fffffff, 0x7fffffff, 1 },
    { -1, -1, -1, -1, 1 },          /* only zero-ness is tested, there is no sign check */
    { 1280, 1024, -5, 7, 1 },
    { 0, 1, 1, 1, 0 }, { 1, 0, 1, 1, 0 }, { 1, 1, 0, 1, 0 }, { 1, 1, 1, 0, 0 },
    { 5, 0, 0, 0, 0 }, { 0, 5, 0, 0, 0 }, { 0, 0, 5, 0, 0 }, { 0, 0, 0, 5, 0 },
    { 0, 0, 1, 1, 0 }, { 1, 1, 0, 0, 0 }, { 0, 1, 0, 1, 0 }, { 1, 0, 1, 0, 0 },
};
TEST(gap0_virtres_vectors_return_value) {
    /* On Windows 10 the four values are not mirrored into the client-visible DC_ATTR page, so only the return value of each
     * vector is observable (a rejected vector must also not disturb the DC: the next valid vector still succeeds). */
    REQUIRE(UN(NtGdiSetVirtualResolution));
    G0FX(f, 8, 8);
    EXPECT_EQ(UN(NtGdiSetVirtualResolution)(f.dc, 11, 22, 33, 44), 1);
    for (unsigned i = 0; i < sizeof vres_vec / sizeof *vres_vec; i++) {
        const struct vres *v = &vres_vec[i];
        INT r = UN(NtGdiSetVirtualResolution)(f.dc, v->a, v->b, v->c, v->d);
        CHECKF(r == v->ok, "vector %u (%d,%d,%d,%d) -> %d want %d", i, v->a, v->b, v->c, v->d, r, v->ok);
    }
    EXPECT_EQ(UN(NtGdiSetVirtualResolution)(f.dc, 0, 0, 0, 0), 1);
    g0fx_close(&f);
}
TEST(gap0_virtres_does_not_change_device_caps_of_memory_dc) {
    REQUIRE(UN(NtGdiSetVirtualResolution)); REQUIRE(U(GetDeviceCaps));
    G0FX(f, 8, 8);
    static const int caps[] = { HORZSIZE, VERTSIZE, HORZRES, VERTRES, LOGPIXELSX, LOGPIXELSY };
    int before[6], after[6];
    for (int i = 0; i < 6; i++) before[i] = U(GetDeviceCaps)(f.dc, caps[i]);
    EXPECT_EQ(UN(NtGdiSetVirtualResolution)(f.dc, 800, 600, 200, 150), 1);
    for (int i = 0; i < 6; i++) after[i] = U(GetDeviceCaps)(f.dc, caps[i]);
    for (int i = 0; i < 6; i++) CHECKF(before[i] == after[i], "cap %d changed %d -> %d", caps[i], before[i], after[i]);
    EXPECT_EQ(UN(NtGdiSetVirtualResolution)(f.dc, 0, 0, 0, 0), 1);
    for (int i = 0; i < 6; i++) CHECKF(U(GetDeviceCaps)(f.dc, caps[i]) == before[i], "cap %d after reset", caps[i]);
    g0fx_close(&f);
}
TEST(gap0_virtres_reset_succeeds_repeatedly) {
    REQUIRE(UN(NtGdiSetVirtualResolution));
    G0FX(f, 8, 8);
    EXPECT_EQ(UN(NtGdiSetVirtualResolution)(f.dc, 640, 480, 160, 120), 1);
    EXPECT_EQ(UN(NtGdiSetVirtualResolution)(f.dc, 0, 0, 0, 0), 1);
    EXPECT_EQ(UN(NtGdiSetVirtualResolution)(f.dc, 0, 0, 0, 0), 1);   /* resetting an already reset DC is not an error */
    g0fx_close(&f);
}
TEST(gap0_virtres_bad_dc_returns_zero) {
    REQUIRE(UN(NtGdiSetVirtualResolution));
    for (unsigned i = 0; i < G0_NBAD; i++) {
        HDC h = (HDC)g0_bad_handles[i];
        CHECKF(UN(NtGdiSetVirtualResolution)(h, 100, 100, 10, 10) == 0, "valid vector, handle 0x%llx", (unsigned long long)g0_bad_handles[i]);
        CHECKF(UN(NtGdiSetVirtualResolution)(h, 0, 0, 0, 0) == 0, "reset vector, handle 0x%llx", (unsigned long long)g0_bad_handles[i]);
        CHECKF(UN(NtGdiSetVirtualResolution)(h, 0, 1, 1, 1) == 0, "invalid vector, handle 0x%llx", (unsigned long long)g0_bad_handles[i]);
    }
}
TEST(gap0_virtres_stale_dc_returns_zero) {
    REQUIRE(UN(NtGdiSetVirtualResolution)); REQUIRE(U(CreateCompatibleDC));
    HDC d = g0_stale_dc();
    REQUIRE(d);
    EXPECT_EQ(UN(NtGdiSetVirtualResolution)(d, 100, 100, 10, 10), 0);
}
TEST(gap0_virtres_user_path_gdi32_export) {
    /* gdi32!SetVirtualResolution is a plain jump to the syscall: same return values */
    REQUIRE(UN(SetVirtualResolution));
    G0FX(f, 8, 8);
    EXPECT_EQ(UN(SetVirtualResolution)(f.dc, 1920, 1080, 508, 286), 1);
    EXPECT_EQ(UN(SetVirtualResolution)(f.dc, 0, 5, 0, 0), 0);
    EXPECT_EQ(UN(SetVirtualResolution)(NULL, 1920, 1080, 508, 286), 0);
    EXPECT_EQ(UN(SetVirtualResolution)(f.dc, 0, 0, 0, 0), 1);
    g0fx_close(&f);
}
TEST(gap0_virtres_is_per_dc) {
    /* each DC is locked and updated on its own: a stale or rejected call on one DC does not affect the other */
    REQUIRE(UN(NtGdiSetVirtualResolution));
    G0FX(f, 4, 4);
    HDC d2 = U(CreateCompatibleDC)(NULL);
    REQUIRE(d2);
    EXPECT_EQ(UN(NtGdiSetVirtualResolution)(f.dc, 300, 200, 30, 20), 1);
    EXPECT_EQ(UN(NtGdiSetVirtualResolution)(d2, 0, 1, 1, 1), 0);
    EXPECT_EQ(UN(NtGdiSetVirtualResolution)(d2, 5, 5, 5, 5), 1);
    U(DeleteDC)(d2);
    EXPECT_EQ(UN(NtGdiSetVirtualResolution)(d2, 5, 5, 5, 5), 0);   /* the deleted DC no longer locks */
    EXPECT_EQ(UN(NtGdiSetVirtualResolution)(f.dc, 0, 0, 0, 0), 1);
    g0fx_close(&f);
}
TEST(gap0_virtres_screen_dc_roundtrip) {
    REQUIRE(UN(NtGdiSetVirtualResolution)); REQUIRE(U(GetDC));
    HDC dc = U(GetDC)(NULL);
    REQUIRE(dc);
    EXPECT_EQ(UN(NtGdiSetVirtualResolution)(dc, 77, 66, 55, 44), 1);
    EXPECT_EQ(UN(NtGdiSetVirtualResolution)(dc, 0, 0, 0, 0), 1);   /* leave the shared screen DC state as found */
    U(ReleaseDC)(NULL, dc);
}

/* ---- NtGdiSetIcmMode(hdc, mode, flags) (ICM mode) ------------------------------------------------------
 * mode 1 = ICM_OFF-style set (flags 0 or 0x100/0x200/0x400), 2 toggles bit 0x20, 3/4 take flags 0x10/8 (or 0x20/0x305 on
 * ICM capable pdevs). Anything outside returns 0 after the DC lock; a non-locking DC returns 0 at once. */
TEST(gap0_icmmode_bad_dc_returns_zero) {
    REQUIRE(UN(NtGdiSetIcmMode));
    for (unsigned i = 0; i < G0_NBAD; i++) {
        HDC h = (HDC)g0_bad_handles[i];
        CHECKF(UN(NtGdiSetIcmMode)(h, 1, 0) == 0, "mode 1 handle 0x%llx", (unsigned long long)g0_bad_handles[i]);
        CHECKF(UN(NtGdiSetIcmMode)(h, 2, 0) == 0, "mode 2 handle 0x%llx", (unsigned long long)g0_bad_handles[i]);
        CHECKF(UN(NtGdiSetIcmMode)(h, 4, 8) == 0, "mode 4 handle 0x%llx", (unsigned long long)g0_bad_handles[i]);
    }
}
TEST(gap0_icmmode_stale_dc_returns_zero) {
    REQUIRE(UN(NtGdiSetIcmMode)); REQUIRE(U(CreateCompatibleDC));
    HDC d = g0_stale_dc();
    REQUIRE(d);
    EXPECT_EQ(UN(NtGdiSetIcmMode)(d, 1, 0), 0);
}
TEST(gap0_icmmode_invalid_mode_values_return_zero) {
    REQUIRE(UN(NtGdiSetIcmMode));
    G0FX(f, 4, 4);
    static const ULONG modes[] = { 0, 5, 6, 7, 0x10, 0x100, 0x7fffffff, 0xffffffff };
    for (unsigned i = 0; i < sizeof modes / sizeof *modes; i++)
        CHECKF(UN(NtGdiSetIcmMode)(f.dc, modes[i], 0) == 0, "mode 0x%lx flags 0 -> nonzero (only 1..4 are modes)", (unsigned long)modes[i]);
    g0fx_close(&f);
}
TEST(gap0_icmmode_invalid_flag_values_return_zero) {
    REQUIRE(UN(NtGdiSetIcmMode));
    G0FX(f, 4, 4);
    /* mode 1 accepts flags 0, 0x100, 0x200, 0x400 only; mode 3/4 accept 0x10, 8 (and 0x20/0x305 on an ICM capable pdev) */
    static const ULONG flags1[] = { 0x1, 0x2, 0x80, 0x101, 0x300, 0x500, 0x800, 0x1000, 0xffffffff };
    for (unsigned i = 0; i < sizeof flags1 / sizeof *flags1; i++)
        CHECKF(UN(NtGdiSetIcmMode)(f.dc, 1, flags1[i]) == 0, "mode 1 flags 0x%lx", (unsigned long)flags1[i]);
    static const ULONG flags4[] = { 0, 1, 2, 4, 0x18, 0x40, 0x100, 0xffffffff };
    for (unsigned i = 0; i < sizeof flags4 / sizeof *flags4; i++) {
        CHECKF(UN(NtGdiSetIcmMode)(f.dc, 4, flags4[i]) == 0, "mode 4 flags 0x%lx", (unsigned long)flags4[i]);
        CHECKF(UN(NtGdiSetIcmMode)(f.dc, 3, flags4[i]) == 0, "mode 3 flags 0x%lx", (unsigned long)flags4[i]);
    }
    g0fx_close(&f);
}
TEST(gap0_icmmode_off_clears_mode_bits_in_dcattr) {
    REQUIRE(UN(NtGdiSetIcmMode));
    G0FX(f, 4, 4);
    void *a = g0_dca(f.dc);
    EXPECT_EQ(UN(NtGdiSetIcmMode)(f.dc, 1, 0), 1);   /* flags 0: the 0xf00 field is cleared */
    if (a) {
        unsigned long w = g0_rd32((char *)a + DCA_ICMFLAGS);
        CHECKF((w & 0xf00) == 0, "DC_ATTR+0xf0 = 0x%lx", w);
        EXPECT_EQ(UN(NtGdiSetIcmMode)(f.dc, 1, 0), 1);   /* idempotent */
        EXPECT_EQ(g0_rd32((char *)a + DCA_ICMFLAGS), w);
    }
    g0fx_close(&f);
}
TEST(gap0_icmmode_done_outside_dc_flag8_accepted) {
    REQUIRE(UN(NtGdiSetIcmMode));
    G0FX(f, 4, 4);
    /* mode 4 with flags 8 or 0x10 passes validation, and mode 4 itself never changes state: returns 1 */
    EXPECT_EQ(UN(NtGdiSetIcmMode)(f.dc, 4, 8), 1);
    EXPECT_EQ(UN(NtGdiSetIcmMode)(f.dc, 4, 0x10), 1);
    g0fx_close(&f);
}
TEST(gap0_icmmode_toggle_off_roundtrip) {
    REQUIRE(UN(NtGdiSetIcmMode));
    G0FX(f, 4, 4);
    void *a = g0_dca(f.dc);
    unsigned long before = a ? g0_rd32((char *)a + DCA_ICMFLAGS) : 0;
    EXPECT_EQ(UN(NtGdiSetIcmMode)(f.dc, 2, 0), 1);   /* mode 2 with flags 0 clears bit 0x20 and keeps the 0xf00 field */
    if (a) {
        unsigned long after = g0_rd32((char *)a + DCA_ICMFLAGS);
        CHECKF((after & 0x20) == 0, "bit 0x20 still set: 0x%lx (before 0x%lx)", after, before);
    }
    g0fx_close(&f);
}

/* ---- NtGdiMakeInfoDC(hdc, bSet): DC::bMakeInfoDC ---------------------------------------------------------------
 * bSet == 0 only succeeds on a DC that is currently an info DC (flag 0x800), so on an ordinary DC it returns 0; a DC that
 * does not lock returns 0. The bSet != 0 direction is not exercised on display or memory DCs: it replaces the DC's surface. */
TEST(gap0_makeinfodc_bad_dc_zero) {
    REQUIRE(UN(NtGdiMakeInfoDC));
    for (unsigned i = 0; i < G0_NBAD; i++)
        CHECKF(UN(NtGdiMakeInfoDC)((HDC)g0_bad_handles[i], 0) == 0, "handle 0x%llx", (unsigned long long)g0_bad_handles[i]);
}
TEST(gap0_makeinfodc_unset_on_plain_memdc_zero) {
    REQUIRE(UN(NtGdiMakeInfoDC));
    G0FX(f, 4, 4);
    EXPECT_EQ(UN(NtGdiMakeInfoDC)(f.dc, 0), 0);
    EXPECT_EQ(UN(NtGdiMakeInfoDC)(f.dc, 0), 0);
    /* the DC is still fully usable afterwards */
    CHECK(U(SetPixel)(f.dc, 1, 1, RGB(10, 20, 30)) != (COLORREF)-1);
    g0fx_close(&f);
}
TEST(gap0_makeinfodc_unset_on_screen_dc_zero) {
    REQUIRE(UN(NtGdiMakeInfoDC)); REQUIRE(U(GetDC));
    HDC dc = U(GetDC)(NULL);
    REQUIRE(dc);
    EXPECT_EQ(UN(NtGdiMakeInfoDC)(dc, 0), 0);
    U(ReleaseDC)(NULL, dc);
}
TEST(gap0_makeinfodc_stale_dc_zero) {
    REQUIRE(UN(NtGdiMakeInfoDC)); REQUIRE(U(CreateCompatibleDC));
    HDC d = g0_stale_dc();
    REQUIRE(d);
    EXPECT_EQ(UN(NtGdiMakeInfoDC)(d, 0), 0);
}
