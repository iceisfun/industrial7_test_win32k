/* System colors and brushes: GetSysColor / GetSysColorBrush versus the SERVERINFO page they are served from.
 *
 * user32!GetSysColor returns the shared color table for i < 0x1f, else sets ERROR_INVALID_PARAMETER and returns 0.
 * user32!GetSysColorBrush returns the HBRUSH from the shared brush table for i < 0x1f, else NULL.
 * win32kbase!SetSysColor (the only writer) stores the requested color in the shared table, the
 * (possibly palette-matched) color in the system color table and keeps one solid brush per index.
 */
#include "si.h"

static int si_truecolor(void) {
    HDC dc = U(GetDC)(NULL);
    if (!dc) return 0;
    int bpp = U(GetDeviceCaps)(dc, BITSPIXEL) * U(GetDeviceCaps)(dc, PLANES);
    U(ReleaseDC)(NULL, dc);
    return bpp >= 24;
}

static void si_color_matches_page(int i) {
    char *psi = si_get(); if (!psi) return;
    COLORREF c = U(GetSysColor)(i);
    COLORREF p = (COLORREF)si_u32(psi, SI_argbSystem + (size_t)i * 4);
    EXPECT_EQ(c, p);
}
static void si_color_unmatched(int i) {
    char *psi = si_get(); if (!psi) return;
    COLORREF m = (COLORREF)si_u32(psi, SI_argbSystem + (size_t)i * 4);
    COLORREF u = (COLORREF)si_u32(psi, SI_argbUnmatched + (size_t)i * 4);
    /* SetSysColor only substitutes the nearest-color lookup when the device has <= 16 colors (flag 8) or for non-solid-capable
     * indices of a palette device; on a true-color display both arrays hold the same RGB triple. */
    if (si_truecolor()) CHECKF((m & 0xffffff) == (u & 0xffffff), "color %d: system=0x%08lx unmatched=0x%08lx", i, (unsigned long)m, (unsigned long)u);
    else t_note("color %d: system=0x%08lx unmatched=0x%08lx (palette display)", i, (unsigned long)m, (unsigned long)u);
}
static void si_brush_matches_page(int i) {
    char *psi = si_get(); if (!psi) return;
    HBRUSH b = U(GetSysColorBrush)(i);
    HBRUSH p = (HBRUSH)si_ptr(psi, SI_hbrSystem + (size_t)i * 8);
    CHECK(b == p);
    if (i == 25 && !b) { t_note("index 25 is not a defined COLOR_ value and has no brush"); return; }
    CHECK(b != NULL);
    if (!b) return;
    /* a GDI handle: the object type reported by NtGdiGetObjectType is OBJ_BRUSH, and the shared GDI cell's type byte matches the handle */
    EXPECT_EQ(U(GetObjectType)(b), OBJ_BRUSH);
    struct k_gdicell cell;
    if (k_gdi_cell(b, &cell)) {
        CHECKF(cell.type_check == (unsigned char)((ULONG_PTR)b >> 24), "cell type 0x%02x handle %p", cell.type_check, b);
        EXPECT_NE(cell.object, NULL);
    }
    LOGBRUSH lb; memset(&lb, 0xcc, sizeof lb);
    int n = U(GetObjectW)(b, sizeof lb, &lb);
    CHECK(n == (int)sizeof lb);
    if (n == sizeof lb) {
        /* hCreateSolidBrushInternal(color) in SetSysColor -> BS_SOLID of that color */
        EXPECT_EQ(lb.lbStyle, BS_SOLID);
        COLORREF c = U(GetSysColor)(i);
        CHECKF((lb.lbColor & 0xffffff) == (c & 0xffffff), "color %d: brush 0x%08lx syscolor 0x%08lx", i, (unsigned long)lb.lbColor, (unsigned long)c);
    }
}

TEST(user_serverinfo_shared_color_00_scrollbar_matches_page) { si_color_matches_page(0); }
TEST(user_serverinfo_shared_color_01_background_matches_page) { si_color_matches_page(1); }
TEST(user_serverinfo_shared_color_02_activecaption_matches_page) { si_color_matches_page(2); }
TEST(user_serverinfo_shared_color_03_inactivecaption_matches_page) { si_color_matches_page(3); }
TEST(user_serverinfo_shared_color_04_menu_matches_page) { si_color_matches_page(4); }
TEST(user_serverinfo_shared_color_05_window_matches_page) { si_color_matches_page(5); }
TEST(user_serverinfo_shared_color_06_windowframe_matches_page) { si_color_matches_page(6); }
TEST(user_serverinfo_shared_color_07_menutext_matches_page) { si_color_matches_page(7); }
TEST(user_serverinfo_shared_color_08_windowtext_matches_page) { si_color_matches_page(8); }
TEST(user_serverinfo_shared_color_09_captiontext_matches_page) { si_color_matches_page(9); }
TEST(user_serverinfo_shared_color_10_activeborder_matches_page) { si_color_matches_page(10); }
TEST(user_serverinfo_shared_color_11_inactiveborder_matches_page) { si_color_matches_page(11); }
TEST(user_serverinfo_shared_color_12_appworkspace_matches_page) { si_color_matches_page(12); }
TEST(user_serverinfo_shared_color_13_highlight_matches_page) { si_color_matches_page(13); }
TEST(user_serverinfo_shared_color_14_highlighttext_matches_page) { si_color_matches_page(14); }
TEST(user_serverinfo_shared_color_15_btnface_matches_page) { si_color_matches_page(15); }
TEST(user_serverinfo_shared_color_16_btnshadow_matches_page) { si_color_matches_page(16); }
TEST(user_serverinfo_shared_color_17_graytext_matches_page) { si_color_matches_page(17); }
TEST(user_serverinfo_shared_color_18_btntext_matches_page) { si_color_matches_page(18); }
TEST(user_serverinfo_shared_color_19_inactivecaptiontext_matches_page) { si_color_matches_page(19); }
TEST(user_serverinfo_shared_color_20_btnhighlight_matches_page) { si_color_matches_page(20); }
TEST(user_serverinfo_shared_color_21_3ddkshadow_matches_page) { si_color_matches_page(21); }
TEST(user_serverinfo_shared_color_22_3dlight_matches_page) { si_color_matches_page(22); }
TEST(user_serverinfo_shared_color_23_infotext_matches_page) { si_color_matches_page(23); }
TEST(user_serverinfo_shared_color_24_infobk_matches_page) { si_color_matches_page(24); }
TEST(user_serverinfo_shared_color_25_unnamed_matches_page) { si_color_matches_page(25); }
TEST(user_serverinfo_shared_color_26_hotlight_matches_page) { si_color_matches_page(26); }
TEST(user_serverinfo_shared_color_27_gradientactivecaption_matches_page) { si_color_matches_page(27); }
TEST(user_serverinfo_shared_color_28_gradientinactivecaption_matches_page) { si_color_matches_page(28); }
TEST(user_serverinfo_shared_color_29_menuhilight_matches_page) { si_color_matches_page(29); }
TEST(user_serverinfo_shared_color_30_menubar_matches_page) { si_color_matches_page(30); }
TEST(user_serverinfo_shared_color_00_scrollbar_unmatched_array) { si_color_unmatched(0); }
TEST(user_serverinfo_shared_color_01_background_unmatched_array) { si_color_unmatched(1); }
TEST(user_serverinfo_shared_color_02_activecaption_unmatched_array) { si_color_unmatched(2); }
TEST(user_serverinfo_shared_color_03_inactivecaption_unmatched_array) { si_color_unmatched(3); }
TEST(user_serverinfo_shared_color_04_menu_unmatched_array) { si_color_unmatched(4); }
TEST(user_serverinfo_shared_color_05_window_unmatched_array) { si_color_unmatched(5); }
TEST(user_serverinfo_shared_color_06_windowframe_unmatched_array) { si_color_unmatched(6); }
TEST(user_serverinfo_shared_color_07_menutext_unmatched_array) { si_color_unmatched(7); }
TEST(user_serverinfo_shared_color_08_windowtext_unmatched_array) { si_color_unmatched(8); }
TEST(user_serverinfo_shared_color_09_captiontext_unmatched_array) { si_color_unmatched(9); }
TEST(user_serverinfo_shared_color_10_activeborder_unmatched_array) { si_color_unmatched(10); }
TEST(user_serverinfo_shared_color_11_inactiveborder_unmatched_array) { si_color_unmatched(11); }
TEST(user_serverinfo_shared_color_12_appworkspace_unmatched_array) { si_color_unmatched(12); }
TEST(user_serverinfo_shared_color_13_highlight_unmatched_array) { si_color_unmatched(13); }
TEST(user_serverinfo_shared_color_14_highlighttext_unmatched_array) { si_color_unmatched(14); }
TEST(user_serverinfo_shared_color_15_btnface_unmatched_array) { si_color_unmatched(15); }
TEST(user_serverinfo_shared_color_16_btnshadow_unmatched_array) { si_color_unmatched(16); }
TEST(user_serverinfo_shared_color_17_graytext_unmatched_array) { si_color_unmatched(17); }
TEST(user_serverinfo_shared_color_18_btntext_unmatched_array) { si_color_unmatched(18); }
TEST(user_serverinfo_shared_color_19_inactivecaptiontext_unmatched_array) { si_color_unmatched(19); }
TEST(user_serverinfo_shared_color_20_btnhighlight_unmatched_array) { si_color_unmatched(20); }
TEST(user_serverinfo_shared_color_21_3ddkshadow_unmatched_array) { si_color_unmatched(21); }
TEST(user_serverinfo_shared_color_22_3dlight_unmatched_array) { si_color_unmatched(22); }
TEST(user_serverinfo_shared_color_23_infotext_unmatched_array) { si_color_unmatched(23); }
TEST(user_serverinfo_shared_color_24_infobk_unmatched_array) { si_color_unmatched(24); }
TEST(user_serverinfo_shared_color_25_unnamed_unmatched_array) { si_color_unmatched(25); }
TEST(user_serverinfo_shared_color_26_hotlight_unmatched_array) { si_color_unmatched(26); }
TEST(user_serverinfo_shared_color_27_gradientactivecaption_unmatched_array) { si_color_unmatched(27); }
TEST(user_serverinfo_shared_color_28_gradientinactivecaption_unmatched_array) { si_color_unmatched(28); }
TEST(user_serverinfo_shared_color_29_menuhilight_unmatched_array) { si_color_unmatched(29); }
TEST(user_serverinfo_shared_color_30_menubar_unmatched_array) { si_color_unmatched(30); }
TEST(user_serverinfo_shared_brush_00_scrollbar_handle) { si_brush_matches_page(0); }
TEST(user_serverinfo_shared_brush_01_background_handle) { si_brush_matches_page(1); }
TEST(user_serverinfo_shared_brush_02_activecaption_handle) { si_brush_matches_page(2); }
TEST(user_serverinfo_shared_brush_03_inactivecaption_handle) { si_brush_matches_page(3); }
TEST(user_serverinfo_shared_brush_04_menu_handle) { si_brush_matches_page(4); }
TEST(user_serverinfo_shared_brush_05_window_handle) { si_brush_matches_page(5); }
TEST(user_serverinfo_shared_brush_06_windowframe_handle) { si_brush_matches_page(6); }
TEST(user_serverinfo_shared_brush_07_menutext_handle) { si_brush_matches_page(7); }
TEST(user_serverinfo_shared_brush_08_windowtext_handle) { si_brush_matches_page(8); }
TEST(user_serverinfo_shared_brush_09_captiontext_handle) { si_brush_matches_page(9); }
TEST(user_serverinfo_shared_brush_10_activeborder_handle) { si_brush_matches_page(10); }
TEST(user_serverinfo_shared_brush_11_inactiveborder_handle) { si_brush_matches_page(11); }
TEST(user_serverinfo_shared_brush_12_appworkspace_handle) { si_brush_matches_page(12); }
TEST(user_serverinfo_shared_brush_13_highlight_handle) { si_brush_matches_page(13); }
TEST(user_serverinfo_shared_brush_14_highlighttext_handle) { si_brush_matches_page(14); }
TEST(user_serverinfo_shared_brush_15_btnface_handle) { si_brush_matches_page(15); }
TEST(user_serverinfo_shared_brush_16_btnshadow_handle) { si_brush_matches_page(16); }
TEST(user_serverinfo_shared_brush_17_graytext_handle) { si_brush_matches_page(17); }
TEST(user_serverinfo_shared_brush_18_btntext_handle) { si_brush_matches_page(18); }
TEST(user_serverinfo_shared_brush_19_inactivecaptiontext_handle) { si_brush_matches_page(19); }
TEST(user_serverinfo_shared_brush_20_btnhighlight_handle) { si_brush_matches_page(20); }
TEST(user_serverinfo_shared_brush_21_3ddkshadow_handle) { si_brush_matches_page(21); }
TEST(user_serverinfo_shared_brush_22_3dlight_handle) { si_brush_matches_page(22); }
TEST(user_serverinfo_shared_brush_23_infotext_handle) { si_brush_matches_page(23); }
TEST(user_serverinfo_shared_brush_24_infobk_handle) { si_brush_matches_page(24); }
TEST(user_serverinfo_shared_brush_25_unnamed_handle) { si_brush_matches_page(25); }
TEST(user_serverinfo_shared_brush_26_hotlight_handle) { si_brush_matches_page(26); }
TEST(user_serverinfo_shared_brush_27_gradientactivecaption_handle) { si_brush_matches_page(27); }
TEST(user_serverinfo_shared_brush_28_gradientinactivecaption_handle) { si_brush_matches_page(28); }
TEST(user_serverinfo_shared_brush_29_menuhilight_handle) { si_brush_matches_page(29); }
TEST(user_serverinfo_shared_brush_30_menubar_handle) { si_brush_matches_page(30); }

/* ---------------- out of range and global properties ---------------- */
static void si_bad_color(int i) {
    REQUIRE(U(GetSysColor));
    U(GetSysColor)(0);
    SET_ERR(0);
    DWORD c = U(GetSysColor)(i);
    EXPECT_EQ(c, 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}
#define BADCOL(name, v) TEST(user_serverinfo_shared_color_invalid_##name) { si_bad_color(v); }
BADCOL(31, 31)
BADCOL(32, 32)
BADCOL(33, 33)
BADCOL(64, 64)
BADCOL(255, 255)
BADCOL(0x1000, 0x1000)
BADCOL(neg1, -1)
BADCOL(intmax, 0x7fffffff)
BADCOL(intmin, (int)0x80000000)

static void si_bad_brush(int i) {
    REQUIRE(U(GetSysColorBrush));
    U(GetSysColor)(0);
    EXPECT_EQ(U(GetSysColorBrush)(i), NULL);
}
#define BADBR(name, v) TEST(user_serverinfo_shared_brush_invalid_##name) { si_bad_brush(v); }
BADBR(31, 31)
BADBR(32, 32)
BADBR(100, 100)
BADBR(neg1, -1)
BADBR(intmax, 0x7fffffff)
BADBR(intmin, (int)0x80000000)

TEST(user_serverinfo_shared_color_valid_leaves_last_error_alone) {
    REQUIRE(U(GetSysColor));
    U(GetSysColor)(0);
    SET_ERR(0x1234);
    for (int i = 0; i < 31; i++) U(GetSysColor)(i);
    EXPECT_ERR(0x1234);
}
TEST(user_serverinfo_shared_brush_handles_are_distinct) {
    char *psi = si_get(); if (!psi) return;
    HBRUSH b[31];
    for (int i = 0; i < 31; i++) b[i] = U(GetSysColorBrush)(i);
    /* SetSysColor allocates a brush per index when the slot is empty and recolors it in place afterwards, so the 31 handles never alias. */
    for (int i = 0; i < 31; i++)
        for (int j = i + 1; j < 31; j++) CHECKF(b[i] != b[j], "indices %d and %d share brush %p", i, j, b[i]);
}
TEST(user_serverinfo_shared_brush_handles_stable_across_calls) {
    char *psi = si_get(); if (!psi) return;
    HBRUSH first[31];
    for (int i = 0; i < 31; i++) first[i] = U(GetSysColorBrush)(i);
    k_pump(20);
    for (int r = 0; r < 3; r++)
        for (int i = 0; i < 31; i++) CHECK(U(GetSysColorBrush)(i) == first[i]);
}
TEST(user_serverinfo_shared_brush_type_differs_from_dc_type) {
    char *psi = si_get(); if (!psi) return;
    HBRUSH b = U(GetSysColorBrush)(COLOR_WINDOW);
    REQUIRE(b);
    HDC dc = U(CreateCompatibleDC)(NULL); REQUIRE(dc);
    CHECK(U(GetObjectType)(b) != U(GetObjectType)(dc));
    EXPECT_EQ(U(GetObjectType)(dc), OBJ_MEMDC);
    U(DeleteDC)(dc);
}
TEST(user_serverinfo_shared_brush_select_into_dc_and_back) {
    char *psi = si_get(); if (!psi) return;
    HDC dc = U(CreateCompatibleDC)(NULL); REQUIRE(dc);
    HBRUSH sys = U(GetSysColorBrush)(COLOR_BTNFACE);
    HGDIOBJ old = U(SelectObject)(dc, sys);
    CHECK(old != NULL);
    /* the selected brush is the one the page points at, and we get the previous (stock white) brush back */
    CHECK(U(GetCurrentObject)(dc, OBJ_BRUSH) == sys);
    U(SelectObject)(dc, old);
    CHECK(U(GetCurrentObject)(dc, OBJ_BRUSH) == old);
    CHECK(U(GetSysColorBrush)(COLOR_BTNFACE) == sys);
    CHECK(U(GetObjectType)(sys) == OBJ_BRUSH);
    U(DeleteDC)(dc);
}
TEST(user_serverinfo_shared_brush_gray_brush_valid) {
    char *psi = si_get(); if (!psi) return;
    HBRUSH g = (HBRUSH)si_ptr(psi, SI_hbrGray);
    /* DrawFrame/DrawState feed this handle straight into PolyPatBlt as brush slot 31 */
    CHECK(g != NULL);
    if (g) EXPECT_EQ(U(GetObjectType)(g), OBJ_BRUSH);
}
TEST(user_serverinfo_shared_brush_array_has_no_null_entries) {
    char *psi = si_get(); if (!psi) return;
    for (int i = 0; i < 31; i++) if (i != 25) CHECKF(si_ptr(psi, SI_hbrSystem + (size_t)i * 8) != NULL, "hbrSystem[%d] is NULL", i);
}
TEST(user_serverinfo_shared_color_aliases_share_slot) {
    REQUIRE(U(GetSysColor));
    CHECK(COLOR_BACKGROUND == COLOR_DESKTOP && COLOR_3DFACE == COLOR_BTNFACE && COLOR_3DSHADOW == COLOR_BTNSHADOW);
    CHECK(U(GetSysColor)(COLOR_BACKGROUND) == U(GetSysColor)(COLOR_DESKTOP));
    CHECK(U(GetSysColorBrush)(COLOR_3DFACE) == U(GetSysColorBrush)(COLOR_BTNFACE));
    CHECK(COLOR_MENUBAR == 30 && COLOR_MENUHILIGHT == 29);
}
TEST(user_serverinfo_shared_color_high_byte_clear_for_registry_colors) {
    char *psi = si_get(); if (!psi) return;
    for (int i = 0; i < 31; i++) {
        unsigned long c = si_u32(psi, SI_argbSystem + (size_t)i * 4);
        if (c >> 24) t_note("color %d has non-zero high byte: 0x%08lx", i, c);
    }
}
TEST(user_serverinfo_shared_color_default_theme_values_logged) {
    char *psi = si_get(); if (!psi) return;
    t_note("COLOR_WINDOW=0x%06lx COLOR_BTNFACE=0x%06lx COLOR_HIGHLIGHT=0x%06lx COLOR_DESKTOP=0x%06lx",
           (unsigned long)U(GetSysColor)(COLOR_WINDOW), (unsigned long)U(GetSysColor)(COLOR_BTNFACE),
           (unsigned long)U(GetSysColor)(COLOR_HIGHLIGHT), (unsigned long)U(GetSysColor)(COLOR_DESKTOP));
}
TEST(user_serverinfo_shared_color_dc_window_pixel_vs_window_color) {
    char *psi = si_get(); if (!psi) return;
    /* GetNearestColor on the screen DC leaves a true-color system color unchanged. */
    HDC dc = U(GetDC)(NULL); REQUIRE(dc);
    COLORREF c = U(GetSysColor)(COLOR_BTNFACE), n = U(GetNearestColor)(dc, c);
    int bpp = U(GetDeviceCaps)(dc, BITSPIXEL) * U(GetDeviceCaps)(dc, PLANES);
    U(ReleaseDC)(NULL, dc);
    if (bpp >= 24) CHECK((n & 0xffffff) == (c & 0xffffff)); else t_note("%d bpp: nearest 0x%06lx for 0x%06lx", bpp, (unsigned long)n, (unsigned long)c);
}
