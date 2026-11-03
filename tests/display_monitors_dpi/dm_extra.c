/* Handle-leak and SystemParametersInfoForDpi coverage for the display/DPI area. */
#include "dm.h"

static BOOL CALLBACK count_cb(HMONITOR h, HDC dc, LPRECT rc, LPARAM lp) { (*(int *)lp)++; return TRUE; }

TEST(display_monitors_dpi_extra_enum_with_dc_does_not_leak_gdi_objects) {
    NEED_GUI(); REQUIRE(U(GetGuiResources) && U(GetDC) && U(ReleaseDC) && U(EnumDisplayMonitors));
    /* EnumDisplayMonitors with an hdc creates a per-monitor DC for the callback and deletes it again (NtUserEnumDisplayMonitors) */
    HDC dc = U(GetDC)(NULL); REQUIRE(dc);
    int n = 0;
    U(EnumDisplayMonitors)(dc, NULL, count_cb, (LPARAM)&n);   /* warm up caches */
    DWORD before = U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS);
    for (int i = 0; i < 50; i++) U(EnumDisplayMonitors)(dc, NULL, count_cb, (LPARAM)&n);
    DWORD after = U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS);
    U(ReleaseDC)(NULL, dc);
    CHECKF(after <= before + 1, "GDI objects %lu -> %lu", before, after);
}

TEST(display_monitors_dpi_extra_get_dc_release_loop_keeps_gdi_count) {
    NEED_GUI(); REQUIRE(U(GetGuiResources) && U(GetDC) && U(ReleaseDC) && U(GetDeviceCaps));
    HDC d0 = U(GetDC)(NULL); U(ReleaseDC)(NULL, d0);
    DWORD before = U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS);
    for (int i = 0; i < 200; i++) { HDC dc = U(GetDC)(NULL); if (dc) { U(GetDeviceCaps)(dc, HORZRES); U(ReleaseDC)(NULL, dc); } }
    DWORD after = U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS);
    CHECKF(after <= before + 1, "GDI objects %lu -> %lu", before, after);
}

TEST(display_monitors_dpi_extra_spi_for_dpi_nonclient_scales_with_dpi) {
    NEED_GUI(); REQUIRE(U(SystemParametersInfoForDpi));
    NONCLIENTMETRICSW a, b;
    memset(&a, 0, sizeof a); a.cbSize = sizeof a; b = a;
    BOOL r1 = U(SystemParametersInfoForDpi)(SPI_GETNONCLIENTMETRICS, sizeof a, &a, 0, 96);
    BOOL r2 = U(SystemParametersInfoForDpi)(SPI_GETNONCLIENTMETRICS, sizeof b, &b, 0, 192);
    CHECK(r1 && r2);
    if (!(r1 && r2)) return;
    CHECKF(a.cbSize == sizeof a && b.cbSize == sizeof b, "cbSize %u %u", a.cbSize, b.cbSize);
    /* the font height is rescaled from 96 to the requested dpi, so 192 dpi is at least as tall as 96 dpi (magnitude) */
    int ha = a.lfCaptionFont.lfHeight < 0 ? -a.lfCaptionFont.lfHeight : a.lfCaptionFont.lfHeight;
    int hb = b.lfCaptionFont.lfHeight < 0 ? -b.lfCaptionFont.lfHeight : b.lfCaptionFont.lfHeight;
    CHECKF(hb >= ha && ha > 0, "caption font heights %d (96) %d (192)", ha, hb);
    CHECKF(b.iBorderWidth >= a.iBorderWidth && b.iCaptionHeight >= a.iCaptionHeight, "border %d/%d caption %d/%d", a.iBorderWidth, b.iBorderWidth, a.iCaptionHeight, b.iCaptionHeight);
}

TEST(display_monitors_dpi_extra_spi_for_dpi_does_not_touch_thread_context) {
    NEED_GUI(); REQUIRE(U(SystemParametersInfoForDpi) && U(GetThreadDpiAwarenessContext));
    unsigned before = dm_cur_ctx(); unsigned long raw1 = 0, raw2 = 0;
    dm_ci(DM_CI_CTX, &raw1);
    LOGFONTW lf; memset(&lf, 0, sizeof lf);
    U(SystemParametersInfoForDpi)(SPI_GETICONTITLELOGFONT, sizeof lf, &lf, 0, 144);
    dm_ci(DM_CI_CTX, &raw2);
    CHECK(dm_cur_ctx() == before);
    CHECKF(raw1 == raw2, "TEB client info context 0x%lx -> 0x%lx", raw1, raw2);
}

TEST(display_monitors_dpi_extra_thread_context_roundtrip_leaves_no_override) {
    NEED_GUI(); REQUIRE(U(SetThreadDpiAwarenessContext) && U(GetThreadDpiAwarenessContext));
    unsigned start = dm_cur_ctx();
    {
        DM_CTX_GUARD();
        dm_set_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);
        CHECK((dm_cur_ctx() & 0xf) == 2);
    }
    CHECKF(dm_cur_ctx() == start, "context 0x%x -> 0x%x", start, dm_cur_ctx());
}
