/* NtGdiGetSystemPaletteUse and GetSystemPaletteUse: DC-kind and wrong-handle matrix on both routes. On a device that is
 * not palette-managed the answer is SYSPAL_ERROR (0); on a palette-managed one it is one of the three use values. The
 * kernel answer must agree with the gdi32 answer and with RC_PALETTE in GetDeviceCaps of the same DC. */
#include "xb.h"

static int is_pal(HDC dc) { return (U(GetDeviceCaps)(dc, RASTERCAPS) & RC_PALETTE) != 0; }
static int valid_use(ULONG v) { return v == SYSPAL_STATIC || v == SYSPAL_NOSTATIC || v == SYSPAL_NOSTATIC256; }

enum { D_SCREEN, D_WINDOW, D_CREATEDC, D_MEM, D_MEM_DIB, D_IC, D_MEM_PAL };
static void dc_kind(int kind) {
    REQUIRE(UN(NtGdiGetSystemPaletteUse)); REQUIRE(U(GetSystemPaletteUse)); REQUIRE(U(GetDeviceCaps));
    HDC dc = NULL; HBITMAP bm = NULL, ob = NULL; HWND w = NULL; HPALETTE pal = NULL;
    switch (kind) {
    case D_SCREEN: dc = U(GetDC)(NULL); break;
    case D_WINDOW: w = U(GetDesktopWindow)(); dc = U(GetDC)(w); break;
    case D_CREATEDC: dc = U(CreateDCW)(L"DISPLAY", NULL, NULL, NULL); break;
    case D_IC: dc = U(CreateICW)(L"DISPLAY", NULL, NULL, NULL); break;
    case D_MEM: dc = U(CreateCompatibleDC)(NULL); break;
    case D_MEM_DIB: {
        BITMAPINFO bi; memset(&bi, 0, sizeof bi); bi.bmiHeader.biSize = 40; bi.bmiHeader.biWidth = 2; bi.bmiHeader.biHeight = 2; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 24;
        dc = U(CreateCompatibleDC)(NULL); void *p; bm = U(CreateDIBSection)(dc, &bi, DIB_RGB_COLORS, &p, NULL, 0); if (bm) ob = (HBITMAP)U(SelectObject)(dc, bm); break; }
    case D_MEM_PAL: {
        LOGPALETTE lp = { 0x300, 1, { { 1, 2, 3, 0 } } }; dc = U(CreateCompatibleDC)(NULL); pal = U(CreatePalette)(&lp); if (pal) U(SelectPalette)(dc, pal, FALSE); break; }
    }
    if (!dc) { t_skip("cannot create the DC"); return; }
    SetLastError(0);
    ULONG a = UN(NtGdiGetSystemPaletteUse)(dc), b = U(GetSystemPaletteUse)(dc);
    CHECKF(a == b, "Nt stub %lu, gdi32 %lu", (unsigned long)a, (unsigned long)b);
    int pm = is_pal(dc);
    if (pm) CHECKF(valid_use(a), "palette-managed device answered %lu", (unsigned long)a);
    else CHECKF(a == SYSPAL_ERROR, "device without a palette answered %lu", (unsigned long)a);
    /* repeatable */
    CHECK(U(GetSystemPaletteUse)(dc) == b);
    if (pal) { U(SelectPalette)(dc, (HPALETTE)U(GetStockObject)(DEFAULT_PALETTE), FALSE); U(DeleteObject)(pal); }
    if (bm) { U(SelectObject)(dc, ob); U(DeleteObject)(bm); }
    if (kind == D_SCREEN || kind == D_WINDOW) U(ReleaseDC)(w, dc); else U(DeleteDC)(dc);
}
#define DK(k, n) XT(syspal_dc_##n) { dc_kind(k); }
DK(D_SCREEN, screen) DK(D_WINDOW, desktop_window) DK(D_CREATEDC, createdc_display) DK(D_MEM, memory) DK(D_MEM_DIB, memory_dib)
DK(D_IC, info_dc) DK(D_MEM_PAL, memory_with_palette)

XT(syspal_handle_types) {
    REQUIRE(UN(NtGdiGetSystemPaletteUse)); REQUIRE(U(GetSystemPaletteUse));
    XHS(x);
    for (int k = 0; k < XH_COUNT; k++) {
        if (k == XH_DC) continue;                       /* a real memory DC is covered by the DC-kind tests */
        SetLastError(0); ULONG a = UN(NtGdiGetSystemPaletteUse)((HDC)x.h[k]); DWORD ea = GetLastError();
        ULONG b = U(GetSystemPaletteUse)((HDC)x.h[k]);
        CHECKF(a == SYSPAL_ERROR && b == SYSPAL_ERROR, "%s: nt %lu gdi %lu", xh_nm[k], (unsigned long)a, (unsigned long)b);
        CHECKF(ea == 0, "%s: error %lu", xh_nm[k], (unsigned long)ea);
    }
    xhs_close(&x);
}
XT(syspal_deleted_dc) {
    REQUIRE(UN(NtGdiGetSystemPaletteUse)); HDC dc = U(CreateCompatibleDC)(NULL); REQUIRE(dc);
    U(DeleteDC)(dc);
    CHECK(UN(NtGdiGetSystemPaletteUse)(dc) == SYSPAL_ERROR);
    CHECK(U(GetSystemPaletteUse)(dc) == SYSPAL_ERROR);
}
XT(syspal_released_window_dc) {
    REQUIRE(UN(NtGdiGetSystemPaletteUse)); HDC dc = U(GetDC)(NULL); REQUIRE(dc);
    U(ReleaseDC)(NULL, dc);
    CHECK(UN(NtGdiGetSystemPaletteUse)(dc) == SYSPAL_ERROR);
}
/* No palette-managed display: GetSystemPaletteEntries has nothing to return either. */
XT(syspal_entries_agree_with_use) {
    REQUIRE(U(GetSystemPaletteEntries)); REQUIRE(U(GetSystemPaletteUse));
    HDC dc = U(GetDC)(NULL); REQUIRE(dc);
    PALETTEENTRY e[256];
    UINT n = U(GetSystemPaletteEntries)(dc, 0, 256, e);
    if (is_pal(dc)) CHECKF(n > 0, "palette device returned no system entries");
    else CHECKF(n == 0, "device without a palette returned %u entries", n);
    { UINT u = U(GetSystemPaletteUse)(dc); CHECKF(is_pal(dc) ? valid_use(u) : u == SYSPAL_ERROR, "use %u disagrees with RC_PALETTE", u); }
    U(ReleaseDC)(NULL, dc);
}
XT(syspal_memory_dc_shares_device_answer) {
    REQUIRE(U(GetSystemPaletteUse)); HDC s = U(GetDC)(NULL), m = U(CreateCompatibleDC)(NULL); REQUIRE(s && m);
    CHECK(U(GetSystemPaletteUse)(s) == U(GetSystemPaletteUse)(m));
    U(DeleteDC)(m); U(ReleaseDC)(NULL, s);
}
