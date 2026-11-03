/* State-changing checks of the SERVERINFO page (run only with /destructive): system colors, SPI-backed
 * metrics and the last-input tick. Every test saves what it changes and restores it before returning;
 * an atexit backstop restores the colors if a test is aborted by an exception. */
#include "../user_serverinfo_shared/si.h"

typedef LONG_PTR (WINAPI *setcol_t)(UINT, const INT *, const COLORREF *, UINT);
static setcol_t dx_ntsetcol(void) { return (setcol_t)t_proc_in("win32u.dll", "NtUserSetSysColors"); }

static COLORREF g_saved[SI_COLOR_COUNT];
static int g_have_saved;
static void dx_restore_all(void) {
    setcol_t nt = dx_ntsetcol();
    if (!g_have_saved || !nt) return;
    INT idx[SI_COLOR_COUNT];
    for (int i = 0; i < SI_COLOR_COUNT; i++) idx[i] = i;
    nt(SI_COLOR_COUNT, idx, g_saved, 0);
    g_have_saved = 0;
}
static void dx_save_all(void) {
    if (g_have_saved) return;
    for (int i = 0; i < SI_COLOR_COUNT; i++) g_saved[i] = U(GetSysColor)(i);
    g_have_saved = 1;
    static int reg;
    if (!reg) { atexit(dx_restore_all); reg = 1; }
}

/* NtUserSetSysColors(count, indices, colors, flags): flags 0 means no broadcast and no nearest-color matching
 * (SetSysColors with (flags & 1) == 0 skips the WM_SYSCOLORCHANGE notification). */
static void dx_color_roundtrip(int i, COLORREF nc, UINT flags) {
    char *psi = si_get(); if (!psi) return;
    setcol_t nt = dx_ntsetcol(); REQUIRE(nt); REQUIRE(U(GetSysColor)); REQUIRE(U(GetSysColorBrush));
    dx_save_all();
    COLORREF old = U(GetSysColor)(i);
    HBRUSH hb = U(GetSysColorBrush)(i);
    INT idx = i; COLORREF val = nc;
    LONG_PTR r = nt(1, &idx, &val, flags);
    CHECK((int)r != 0);
    /* SetSysColor writes the color to argbSystemUnmatched and recolors the existing brush in place. */
    EXPECT_EQ(U(GetSysColor)(i), nc);
    EXPECT_EQ(si_u32(psi, SI_argbSystem + (size_t)i * 4), nc);
    EXPECT_EQ(si_u32(psi, SI_argbUnmatched + (size_t)i * 4), nc);
    CHECK(U(GetSysColorBrush)(i) == hb);
    LOGBRUSH lb; memset(&lb, 0, sizeof lb);
    CHECK(U(GetObjectW)(hb, sizeof lb, &lb) == sizeof lb);
    CHECK((lb.lbColor & 0xffffff) == (nc & 0xffffff));
    /* other indices are untouched */
    for (int j = 0; j < SI_COLOR_COUNT; j++)
        if (j != i) CHECKF(U(GetSysColor)(j) == g_saved[j], "color %d changed unexpectedly", j);
    val = old; r = nt(1, &idx, &val, flags);
    CHECK((int)r != 0);
    EXPECT_EQ(U(GetSysColor)(i), old);
    EXPECT_EQ(si_u32(psi, SI_argbSystem + (size_t)i * 4), old);
    CHECK(U(GetSysColorBrush)(i) == hb);
    dx_restore_all();
}
#define DXCOL(name, idx, col) TEST(dx_user_serverinfo_shared_setcolor_##name) { dx_color_roundtrip(idx, col, 0); }
DXCOL(infobk,           COLOR_INFOBK,           0x00123456)
DXCOL(infotext,         COLOR_INFOTEXT,         0x00654321)
DXCOL(3ddkshadow,       COLOR_3DDKSHADOW,       0x00102030)
DXCOL(hotlight,         COLOR_HOTLIGHT,         0x00A0B0C0)
DXCOL(gradactive,       COLOR_GRADIENTACTIVECAPTION,   0x00010203)
DXCOL(gradinactive,     COLOR_GRADIENTINACTIVECAPTION, 0x00FEDCBA)
DXCOL(menubar,          COLOR_MENUBAR,          0x00222222)
DXCOL(menuhilight,      COLOR_MENUHILIGHT,      0x00333333)
DXCOL(appworkspace,     COLOR_APPWORKSPACE,     0x00444444)
DXCOL(inactiveborder,   COLOR_INACTIVEBORDER,   0x00555555)
DXCOL(activeborder,     COLOR_ACTIVEBORDER,     0x00666666)
DXCOL(graytext,         COLOR_GRAYTEXT,         0x00777777)
DXCOL(btnshadow,        COLOR_BTNSHADOW,        0x00888888)
DXCOL(btnhighlight,     COLOR_BTNHIGHLIGHT,     0x00999999)
DXCOL(3dlight,          COLOR_3DLIGHT,          0x00AAAAAA)

TEST(dx_user_serverinfo_shared_setcolor_flag2_true_color_keeps_value) {
    HDC dc = NULL;
    REQUIRE(U(GetDC));
    dc = U(GetDC)(NULL); REQUIRE(dc);
    int bpp = U(GetDeviceCaps)(dc, BITSPIXEL) * U(GetDeviceCaps)(dc, PLANES);
    U(ReleaseDC)(NULL, dc);
    if (bpp < 24) { t_skip("palette display"); return; }
    /* flags 2 requests nearest-color matching, which on a true-color display returns the same RGB */
    dx_color_roundtrip(COLOR_INFOBK, 0x00ABCDEF, 2);
}
TEST(dx_user_serverinfo_shared_setcolor_user32_path_flags7_roundtrip) {
    char *psi = si_get(); if (!psi) return;
    REQUIRE(U(SetSysColors));
    dx_save_all();
    int idx = COLOR_HOTLIGHT; COLORREF nv = 0x00112233, old = U(GetSysColor)(idx);
    /* user32!SetSysColors loads r9 = 7 (broadcast | nearest | match) and tail-jumps to the stub */
    CHECK(U(SetSysColors)(1, &idx, &nv));
    EXPECT_EQ(si_u32(psi, SI_argbSystem + (size_t)idx * 4), nv & 0xffffff);
    EXPECT_EQ(U(GetSysColor)(idx) & 0xffffff, nv & 0xffffff);
    k_pump(50);
    CHECK(U(SetSysColors)(1, &idx, &old));
    EXPECT_EQ(U(GetSysColor)(idx), old);
    dx_restore_all();
}
TEST(dx_user_serverinfo_shared_setcolor_multiple_in_one_call) {
    char *psi = si_get(); if (!psi) return;
    setcol_t nt = dx_ntsetcol(); REQUIRE(nt);
    dx_save_all();
    INT idx[4] = { COLOR_INFOBK, COLOR_HOTLIGHT, COLOR_MENUBAR, COLOR_3DDKSHADOW };
    COLORREF val[4] = { 0x00010101, 0x00020202, 0x00030303, 0x00040404 };
    CHECK((int)nt(4, idx, val, 0) != 0);
    for (int k = 0; k < 4; k++) EXPECT_EQ(U(GetSysColor)(idx[k]), val[k]);
    for (int k = 0; k < 4; k++) EXPECT_EQ(si_u32(psi, SI_argbSystem + (size_t)idx[k] * 4), val[k]);
    dx_restore_all();
    for (int k = 0; k < 4; k++) EXPECT_EQ(U(GetSysColor)(idx[k]), g_saved[idx[k]]);
}
TEST(dx_user_serverinfo_shared_setcolor_duplicate_index_last_wins) {
    char *psi = si_get(); if (!psi) return;
    setcol_t nt = dx_ntsetcol(); REQUIRE(nt);
    dx_save_all();
    INT idx[3] = { COLOR_INFOBK, COLOR_INFOBK, COLOR_INFOBK };
    COLORREF val[3] = { 0x00111111, 0x00222222, 0x00333333 };
    CHECK((int)nt(3, idx, val, 0) != 0);
    /* the kernel walks the arrays in order */
    EXPECT_EQ(U(GetSysColor)(COLOR_INFOBK), 0x00333333);
    dx_restore_all();
}
TEST(dx_user_serverinfo_shared_setcolor_out_of_range_index_ignored) {
    char *psi = si_get(); if (!psi) return;
    setcol_t nt = dx_ntsetcol(); REQUIRE(nt);
    dx_save_all();
    INT idx[4] = { 31, 32, 0x7fffffff, -1 };
    COLORREF val[4] = { 0x00ABABAB, 0x00ABABAB, 0x00ABABAB, 0x00ABABAB };
    /* only indices < 0x1f; the others are skipped and the call still reports success */
    CHECK((int)nt(4, idx, val, 0) != 0);
    for (int i = 0; i < SI_COLOR_COUNT; i++) CHECKF(U(GetSysColor)(i) == g_saved[i], "color %d changed", i);
    dx_restore_all();
}
TEST(dx_user_serverinfo_shared_setcolor_zero_count_is_noop) {
    char *psi = si_get(); if (!psi) return;
    setcol_t nt = dx_ntsetcol(); REQUIRE(nt);
    dx_save_all();
    CHECK((int)nt(0, NULL, NULL, 0) != 0);
    for (int i = 0; i < SI_COLOR_COUNT; i++) CHECK(U(GetSysColor)(i) == g_saved[i]);
    dx_restore_all();
}
TEST(dx_user_serverinfo_shared_setcolor_brush_handles_survive_recolor) {
    char *psi = si_get(); if (!psi) return;
    setcol_t nt = dx_ntsetcol(); REQUIRE(nt);
    dx_save_all();
    HBRUSH before[SI_COLOR_COUNT];
    for (int i = 0; i < SI_COLOR_COUNT; i++) before[i] = U(GetSysColorBrush)(i);
    INT idx[3] = { COLOR_INFOBK, COLOR_HOTLIGHT, COLOR_MENUBAR };
    COLORREF val[3] = { 0x00131313, 0x00232323, 0x00333333 };
    nt(3, idx, val, 0);
    /* the kernel recolors in place: brush handles never change on recolor */
    for (int i = 0; i < SI_COLOR_COUNT; i++) CHECK(U(GetSysColorBrush)(i) == before[i]);
    dx_restore_all();
}
TEST(dx_user_serverinfo_shared_setcolor_dc_selected_brush_sees_new_color) {
    char *psi = si_get(); if (!psi) return;
    setcol_t nt = dx_ntsetcol(); REQUIRE(nt);
    dx_save_all();
    HDC dc = U(CreateCompatibleDC)(NULL); REQUIRE(dc);
    HDC sdc = U(GetDC)(NULL);
    HBITMAP bm = U(CreateCompatibleBitmap)(sdc, 4, 4);
    U(ReleaseDC)(NULL, sdc);
    HGDIOBJ obm = U(SelectObject)(dc, bm);
    INT idx = COLOR_INFOBK; COLORREF val = 0x00F0A050;
    nt(1, &idx, &val, 0);
    RECT r = { 0, 0, 4, 4 };
    U(FillRect)(dc, &r, U(GetSysColorBrush)(COLOR_INFOBK));
    COLORREF px = U(GetPixel)(dc, 1, 1);
    /* the recolored shared brush paints the new color (true-color bitmap) */
    CHECKF((px & 0xffffff) == val, "pixel 0x%06lx", (unsigned long)px);
    U(SelectObject)(dc, obm);
    U(DeleteObject)(bm); U(DeleteDC)(dc);
    dx_restore_all();
}

/* ---------------- SPI-backed metrics ---------------- */
static void dx_spi_u32_roundtrip(UINT set, UINT get_sm_off, UINT val, size_t page_off, int sm) {
    char *psi = si_get(); if (!psi) return;
    REQUIRE(U(SystemParametersInfoW));
    int old = U(GetSystemMetrics)(sm);
    long oldp = si_i32(psi, page_off);
    CHECK(old == oldp);
    /* fWinIni = 0: change the live value only (no profile write, no broadcast) */
    CHECK(U(SystemParametersInfoW)(set, val, NULL, 0));
    EXPECT_EQ(si_i32(psi, page_off), val);
    EXPECT_EQ(U(GetSystemMetrics)(sm), val);
    CHECK(U(SystemParametersInfoW)(set, old, NULL, 0));
    EXPECT_EQ(si_i32(psi, page_off), old);
    EXPECT_EQ(U(GetSystemMetrics)(sm), old);
}
TEST(dx_user_serverinfo_shared_spi_set_doubleclick_width) {
    /* SPI 0x1d stores uiParam in the shared server info */
    dx_spi_u32_roundtrip(SPI_SETDOUBLECLKWIDTH, 0, 7, 0x7f8, SM_CXDOUBLECLK);
}
TEST(dx_user_serverinfo_shared_spi_set_doubleclick_height) {
    dx_spi_u32_roundtrip(SPI_SETDOUBLECLKHEIGHT, 0, 9, 0x7fc, SM_CYDOUBLECLK);
}
TEST(dx_user_serverinfo_shared_spi_set_drag_width) {
    dx_spi_u32_roundtrip(SPI_SETDRAGWIDTH, 0, 6, 0x878, SM_CXDRAG);
}
TEST(dx_user_serverinfo_shared_spi_set_drag_height) {
    dx_spi_u32_roundtrip(SPI_SETDRAGHEIGHT, 0, 8, 0x87c, SM_CYDRAG);
}
TEST(dx_user_serverinfo_shared_spi_set_menu_drop_alignment) {
    char *psi = si_get(); if (!psi) return;
    BOOL old = 0;
    REQUIRE(U(SystemParametersInfoW)(SPI_GETMENUDROPALIGNMENT, 0, &old, 0));
    BOOL want = !old;
    /* SPI 0x1c stores (param != 0) in the shared server info */
    CHECK(U(SystemParametersInfoW)(SPI_SETMENUDROPALIGNMENT, want, NULL, 0));
    EXPECT_EQ(si_i32(psi, 0x808), want);
    EXPECT_EQ(U(GetSystemMetrics)(SM_MENUDROPALIGNMENT), want);
    BOOL now = 99; CHECK(U(SystemParametersInfoW)(SPI_GETMENUDROPALIGNMENT, 0, &now, 0));
    EXPECT_EQ(now, want);
    CHECK(U(SystemParametersInfoW)(SPI_SETMENUDROPALIGNMENT, old, NULL, 0));
    EXPECT_EQ(si_i32(psi, 0x808), old);
    CHECK(U(SystemParametersInfoW)(SPI_SETMENUDROPALIGNMENT, 5, NULL, 0)); /* any non-zero value normalizes to 1 */
    EXPECT_EQ(si_i32(psi, 0x808), 1);
    CHECK(U(SystemParametersInfoW)(SPI_SETMENUDROPALIGNMENT, old, NULL, 0));
}
TEST(dx_user_serverinfo_shared_spi_set_focus_border_width) {
    char *psi = si_get(); if (!psi) return;
    UINT old = 0;
    REQUIRE(U(SystemParametersInfoW)(SPI_GETFOCUSBORDERWIDTH, 0, &old, 0));
    /* SPI 0x200f mirrors a non-zero value into the shared server info (the pvParam is the value itself) */
    CHECK(U(SystemParametersInfoW)(SPI_SETFOCUSBORDERWIDTH, 0, (PVOID)(ULONG_PTR)3, 0));
    EXPECT_EQ(si_i32(psi, 0x8b4), 3);
    EXPECT_EQ(U(GetSystemMetrics)(SM_CXFOCUSBORDER), 3);
    UINT now = 0; CHECK(U(SystemParametersInfoW)(SPI_GETFOCUSBORDERWIDTH, 0, &now, 0));
    EXPECT_EQ(now, 3);
    CHECK(U(SystemParametersInfoW)(SPI_SETFOCUSBORDERWIDTH, 0, (PVOID)(ULONG_PTR)old, 0));
    EXPECT_EQ(si_i32(psi, 0x8b4), old);
}
TEST(dx_user_serverinfo_shared_spi_set_focus_border_height) {
    char *psi = si_get(); if (!psi) return;
    UINT old = 0;
    REQUIRE(U(SystemParametersInfoW)(SPI_GETFOCUSBORDERHEIGHT, 0, &old, 0));
    CHECK(U(SystemParametersInfoW)(SPI_SETFOCUSBORDERHEIGHT, 0, (PVOID)(ULONG_PTR)4, 0));
    EXPECT_EQ(si_i32(psi, 0x8b8), 4);
    EXPECT_EQ(U(GetSystemMetrics)(SM_CYFOCUSBORDER), 4);
    CHECK(U(SystemParametersInfoW)(SPI_SETFOCUSBORDERHEIGHT, 0, (PVOID)(ULONG_PTR)old, 0));
    EXPECT_EQ(si_i32(psi, 0x8b8), old);
}
TEST(dx_user_serverinfo_shared_set_double_click_time_roundtrip) {
    char *psi = si_get(); if (!psi) return;
    REQUIRE(U(SetDoubleClickTime)); REQUIRE(U(GetDoubleClickTime));
    UINT old = U(GetDoubleClickTime)();
    CHECK(U(SetDoubleClickTime)(321));
    EXPECT_EQ(U(GetDoubleClickTime)(), 321);
    CHECK(U(SetDoubleClickTime)(old));
    EXPECT_EQ(U(GetDoubleClickTime)(), old);
}
TEST(dx_user_serverinfo_shared_set_double_click_time_zero_means_default) {
    char *psi = si_get(); if (!psi) return;
    UINT old = U(GetDoubleClickTime)();
    CHECK(U(SetDoubleClickTime)(0));
    /* zero selects the system default (500 ms) */
    EXPECT_EQ(U(GetDoubleClickTime)(), 500);
    CHECK(U(SetDoubleClickTime)(old));
    EXPECT_EQ(U(GetDoubleClickTime)(), old);
}
TEST(dx_user_serverinfo_shared_set_double_click_time_clamped) {
    char *psi = si_get(); if (!psi) return;
    UINT old = U(GetDoubleClickTime)();
    U(SetDoubleClickTime)(60000);
    UINT big = U(GetDoubleClickTime)();
    U(SetDoubleClickTime)(old);
    t_note("SetDoubleClickTime(60000) -> GetDoubleClickTime %u", big);
}

/* ---------------- last input tick ---------------- */
TEST(dx_user_serverinfo_shared_sendinput_keeps_last_input_monotonic) {
    char *psi = si_get(); if (!psi) return;
    REQUIRE(U(SendInput)); REQUIRE(U(GetLastInputInfo));
    LASTINPUTINFO a = { sizeof a, 0 }, b = { sizeof b, 0 };
    CHECK(U(GetLastInputInfo)(&a));
    Sleep(60);
    INPUT in; memset(&in, 0, sizeof in);
    in.type = INPUT_KEYBOARD; in.ki.wVk = VK_F24; in.ki.dwFlags = KEYEVENTF_KEYUP; /* an unpaired key-up of an unbound key is inert */
    UINT n = U(SendInput)(1, &in, sizeof in);
    Sleep(30);
    CHECK(U(GetLastInputInfo)(&b));
    CHECKF((int)(b.dwTime - a.dwTime) >= 0, "tick went backwards %lu -> %lu", (unsigned long)a.dwTime, (unsigned long)b.dwTime);
    CHECK((int)(si_u32(psi, SI_LastRITTick) - b.dwTime) >= 0);
    t_note("SendInput accepted %u; last-input tick %lu -> %lu", n, (unsigned long)a.dwTime, (unsigned long)b.dwTime);
}
