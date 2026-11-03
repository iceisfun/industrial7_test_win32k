/* SaveDC / RestoreDC through gdi32full!SaveDCImpl/RestoreDCImpl and the syscalls NtGdiSaveDC / NtGdiRestoreDC
 * (SaveDC / RestoreDC).
 *
 * The DC keeps a save level (1 when nothing is saved). SaveDC returns the level
 * BEFORE incrementing it (so the first SaveDC returns 1) and NtGdiGetDCDword(hdc, 5) reads the level. RestoreDC(n):
 * n < 0 is relative (n += level); valid when 0 < n < level, else ERROR_INVALID_PARAMETER and nothing changes;
 * on success every level above n is popped, ending with level == n. */
#include "dc.h"
#include <limits.h>

static int dc_level(HDC dc) {
    ULONG v = 0xdeadbeef;
    if (!UN(NtGdiGetDCDword) || !UN(NtGdiGetDCDword)(dc, 5, &v)) return -1;
    return (int)v;
}

#define NEED_LEVEL() DCWANT(UN(NtGdiGetDCDword))

TEST(gdi_dc_objects_save_initial_level_is_one) {
    NEED_LEVEL();
    FX(f);
    EXPECT_EQ(dc_level(f.mem), 1);
    dcf_close(&f);
}

TEST(gdi_dc_objects_save_returns_incrementing_levels) {
    NEED_LEVEL();
    FX(f);
    for (int i = 1; i <= 6; i++) {
        int r = U(SaveDC)(f.mem);
        EXPECT_EQ(r, i);
        EXPECT_EQ(dc_level(f.mem), i + 1);
    }
    dcf_close(&f);
}

TEST(gdi_dc_objects_restore_minus_one_pops_one_level) {
    NEED_LEVEL();
    FX(f);
    U(SaveDC)(f.mem); U(SaveDC)(f.mem); U(SaveDC)(f.mem);
    EXPECT_EQ(dc_level(f.mem), 4);
    CHECK(U(RestoreDC)(f.mem, -1));
    EXPECT_EQ(dc_level(f.mem), 3);
    CHECK(U(RestoreDC)(f.mem, -2));
    EXPECT_EQ(dc_level(f.mem), 1);
    dcf_close(&f);
}

TEST(gdi_dc_objects_restore_absolute_pops_to_that_level) {
    NEED_LEVEL();
    FX(f);
    for (int i = 0; i < 5; i++) U(SaveDC)(f.mem);
    EXPECT_EQ(dc_level(f.mem), 6);
    CHECK(U(RestoreDC)(f.mem, 3));
    EXPECT_EQ(dc_level(f.mem), 3);
    CHECK(U(RestoreDC)(f.mem, 1));
    EXPECT_EQ(dc_level(f.mem), 1);
    dcf_close(&f);
}

TEST(gdi_dc_objects_restore_invalid_levels_fail_and_keep_level) {
    NEED_LEVEL();
    FX(f);
    U(SaveDC)(f.mem); U(SaveDC)(f.mem);   /* level 3 */
    static const int bad[] = { 0, 3, 4, 100, -3, -4, -100, INT_MAX, INT_MIN, INT_MIN + 1 };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        SET_ERR(0);
        BOOL r = U(RestoreDC)(f.mem, bad[i]);
        CHECKF(!r, "RestoreDC(%d) accepted at level 3", bad[i]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
        EXPECT_EQ(dc_level(f.mem), 3);
    }
    dcf_close(&f);
}

TEST(gdi_dc_objects_restore_with_nothing_saved_fails) {
    NEED_LEVEL();
    FX(f);
    static const int vals[] = { -1, 0, 1, 2 };
    for (unsigned i = 0; i < sizeof vals / sizeof *vals; i++) {
        SET_ERR(0);
        CHECKF(!U(RestoreDC)(f.mem, vals[i]), "RestoreDC(%d) with no save", vals[i]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
        EXPECT_EQ(dc_level(f.mem), 1);
    }
    dcf_close(&f);
}

TEST(gdi_dc_objects_restore_restores_dc_attributes) {
    FX(f);
    COLORREF tc0 = U(SetTextColor)(f.mem, RGB(1, 2, 3));
    int bk0 = U(SetBkMode)(f.mem, OPAQUE);
    int rop0 = U(SetROP2)(f.mem, R2_COPYPEN);
    U(SaveDC)(f.mem);
    U(SetTextColor)(f.mem, RGB(10, 20, 30));
    U(SetBkMode)(f.mem, TRANSPARENT);
    U(SetROP2)(f.mem, R2_XORPEN);
    U(SetMapMode)(f.mem, MM_HIMETRIC);
    EXPECT_EQ(U(GetTextColor)(f.mem), RGB(10, 20, 30));
    CHECK(U(RestoreDC)(f.mem, -1));
    /* The kernel copies the saved DC back and marks the DC_ATTR page dirty; the user-mode getters read the page. */
    EXPECT_EQ(U(GetTextColor)(f.mem), RGB(1, 2, 3));
    EXPECT_EQ(U(GetBkMode)(f.mem), OPAQUE);
    EXPECT_EQ(U(GetROP2)(f.mem), R2_COPYPEN);
    EXPECT_EQ(U(GetMapMode)(f.mem), MM_TEXT);
    void *a = dc_attr(f.mem);
    if (a) EXPECT_EQ(da_u32(a, DA_TXCLR), RGB(1, 2, 3));
    (void)tc0; (void)bk0; (void)rop0;
    dcf_close(&f);
}

TEST(gdi_dc_objects_restore_restores_origins_and_extents) {
    FX(f);
    U(SetMapMode)(f.mem, MM_ANISOTROPIC);
    U(SetViewportOrgEx)(f.mem, 3, 4, NULL);
    U(SetWindowOrgEx)(f.mem, 5, 6, NULL);
    U(SaveDC)(f.mem);
    U(SetViewportOrgEx)(f.mem, 30, 40, NULL);
    U(SetWindowOrgEx)(f.mem, 50, 60, NULL);
    U(SetMapMode)(f.mem, MM_TEXT);
    CHECK(U(RestoreDC)(f.mem, -1));
    POINT p = { -1, -1 };
    EXPECT_EQ(U(GetMapMode)(f.mem), MM_ANISOTROPIC);
    CHECK(U(GetViewportOrgEx)(f.mem, &p)); EXPECT_EQ(p.x, 3); EXPECT_EQ(p.y, 4);
    CHECK(U(GetWindowOrgEx)(f.mem, &p));   EXPECT_EQ(p.x, 5); EXPECT_EQ(p.y, 6);
    dcf_close(&f);
}

TEST(gdi_dc_objects_restore_restores_selected_pen_and_brush) {
    FX(f);
    HGDIOBJ pen0 = U(GetCurrentObject)(f.mem, OBJ_PEN), br0 = U(GetCurrentObject)(f.mem, OBJ_BRUSH);
    HPEN pen = U(CreatePen)(PS_SOLID, 1, RGB(5, 6, 7));
    HBRUSH br = U(CreateSolidBrush)(RGB(7, 6, 5));
    U(SaveDC)(f.mem);
    /* The select goes to the DC_ATTR page only (dirty bit); RestoreDC first syncs it and then restores the saved handles. */
    U(SelectObject)(f.mem, pen);
    U(SelectObject)(f.mem, br);
    CHECK(U(RestoreDC)(f.mem, -1));
    EXPECT_EQ(U(GetCurrentObject)(f.mem, OBJ_PEN), pen0);
    EXPECT_EQ(U(GetCurrentObject)(f.mem, OBJ_BRUSH), br0);
    U(GdiFlush)();
    CHECK(U(DeleteObject)(pen));
    CHECK(U(DeleteObject)(br));
    dcf_close(&f);
}

TEST(gdi_dc_objects_restore_restores_selected_bitmap) {
    FX(f);
    HGDIOBJ def = U(GetCurrentObject)(f.mem, OBJ_BITMAP);
    HBITMAP bm = U(CreateCompatibleBitmap)(f.scr, 8, 8);
    CHECK(bm != NULL);
    U(SaveDC)(f.mem);
    EXPECT_EQ(U(SelectObject)(f.mem, bm), def);
    EXPECT_EQ(U(GetCurrentObject)(f.mem, OBJ_BITMAP), bm);
    CHECK(U(RestoreDC)(f.mem, -1));
    /* RestoreDC re-selects the stock bitmap and copies the saved surface back. */
    EXPECT_EQ(U(GetCurrentObject)(f.mem, OBJ_BITMAP), def);
    CHECK(U(DeleteObject)(bm));    /* no longer selected anywhere */
    dcf_close(&f);
}

TEST(gdi_dc_objects_restore_restores_clip_region) {
    FX(f);
    HBITMAP bm = U(CreateCompatibleBitmap)(f.scr, 64, 64);
    U(SelectObject)(f.mem, bm);
    RECT r0, r1, r2;
    U(GetClipBox)(f.mem, &r0);
    U(SaveDC)(f.mem);
    U(IntersectClipRect)(f.mem, 2, 3, 10, 12);
    U(GetClipBox)(f.mem, &r1);
    EXPECT_EQ(r1.left, 2); EXPECT_EQ(r1.top, 3); EXPECT_EQ(r1.right, 10); EXPECT_EQ(r1.bottom, 12);
    CHECK(U(RestoreDC)(f.mem, -1));
    U(GetClipBox)(f.mem, &r2);
    CHECK(!memcmp(&r0, &r2, sizeof r0));
    U(SelectObject)(f.mem, U(GetStockObject)(21));
    U(DeleteObject)(bm);
    dcf_close(&f);
}

TEST(gdi_dc_objects_restore_restores_dc_brush_color) {
    FX(f);
    COLORREF c0 = U(GetDCBrushColor)(f.mem);
    U(SaveDC)(f.mem);
    U(SetDCBrushColor)(f.mem, RGB(11, 22, 33));
    EXPECT_EQ(U(GetDCBrushColor)(f.mem), RGB(11, 22, 33));
    CHECK(U(RestoreDC)(f.mem, -1));
    EXPECT_EQ(U(GetDCBrushColor)(f.mem), c0);
    dcf_close(&f);
}

TEST(gdi_dc_objects_failed_restore_keeps_state) {
    NEED_LEVEL();
    FX(f);
    U(SetTextColor)(f.mem, RGB(3, 3, 3));
    U(SaveDC)(f.mem);
    U(SetTextColor)(f.mem, RGB(4, 4, 4));
    SET_ERR(0);
    CHECK(!U(RestoreDC)(f.mem, 7));
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    EXPECT_EQ(U(GetTextColor)(f.mem), RGB(4, 4, 4));
    EXPECT_EQ(dc_level(f.mem), 2);
    CHECK(U(RestoreDC)(f.mem, 1));
    EXPECT_EQ(U(GetTextColor)(f.mem), RGB(3, 3, 3));
    dcf_close(&f);
}

TEST(gdi_dc_objects_restore_pops_all_intermediate_states) {
    FX(f);
    for (int i = 0; i < 20; i++) { U(SetTextColor)(f.mem, RGB(i, 0, 0)); U(SaveDC)(f.mem); }
    U(SetTextColor)(f.mem, RGB(250, 0, 0));
    CHECK(U(RestoreDC)(f.mem, 10));
    /* state saved by the 10th SaveDC (made when the colour was i == 9) */
    EXPECT_EQ(U(GetTextColor)(f.mem), RGB(9, 0, 0));
    CHECK(U(RestoreDC)(f.mem, 1));
    EXPECT_EQ(U(GetTextColor)(f.mem), RGB(0, 0, 0));
    dcf_close(&f);
}

TEST(gdi_dc_objects_deep_save_stack_restores_in_one_call) {
    NEED_LEVEL();
    FX(f);
    for (int i = 0; i < 200; i++) EXPECT_EQ(U(SaveDC)(f.mem), i + 1);
    EXPECT_EQ(dc_level(f.mem), 201);
    CHECK(U(RestoreDC)(f.mem, 1));
    EXPECT_EQ(dc_level(f.mem), 1);
    dcf_close(&f);
}

TEST(gdi_dc_objects_save_restore_null_dc) {
    SET_ERR(0);
    EXPECT_EQ(U(SaveDC)(NULL), 0);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    SET_ERR(0);
    CHECK(!U(RestoreDC)(NULL, -1));
    EXPECT_ERR(ERROR_INVALID_HANDLE);
}

TEST(gdi_dc_objects_save_restore_dead_dc) {
    REQUIRE(U(CreateCompatibleDC));
    HDC dc = U(CreateCompatibleDC)(NULL);
    U(DeleteDC)(dc);
    /* SaveDCImpl/RestoreDCImpl: a DC-typed handle whose table entry is gone just returns 0. */
    EXPECT_EQ(U(SaveDC)(dc), 0);
    CHECK(!U(RestoreDC)(dc, -1));
}

TEST(gdi_dc_objects_save_on_pen_handle_fails) {
    REQUIRE(U(CreatePen));
    HPEN pen = U(CreatePen)(PS_SOLID, 1, 0);
    SET_ERR(0);
    /* pldcGet() of a non-DC handle yields no local DC info: ERROR_INVALID_HANDLE, result 0. */
    EXPECT_EQ(U(SaveDC)((HDC)pen), 0);
    U(DeleteObject)(pen);
}

TEST(gdi_dc_objects_save_restore_on_created_dc) {
    NEED_LEVEL();
    REQUIRE(U(CreateDCW));
    HDC dc = U(CreateDCW)(L"DISPLAY", NULL, NULL, NULL);
    if (!dc) { t_skip("CreateDCW(DISPLAY) failed"); return; }
    EXPECT_EQ(dc_level(dc), 1);
    EXPECT_EQ(U(SaveDC)(dc), 1);
    EXPECT_EQ(U(SaveDC)(dc), 2);
    CHECK(U(RestoreDC)(dc, 1));
    EXPECT_EQ(dc_level(dc), 1);
    U(DeleteDC)(dc);
}

/* ---- direct syscalls ---- */

TEST(gdi_dc_objects_syscall_save_restore_roundtrip) {
    DCWANT(UN(NtGdiSaveDC)); DCWANT(UN(NtGdiRestoreDC)); NEED_LEVEL();
    FX(f);
    EXPECT_EQ(UN(NtGdiSaveDC)(f.mem), 1);
    EXPECT_EQ(UN(NtGdiSaveDC)(f.mem), 2);
    EXPECT_EQ(dc_level(f.mem), 3);
    CHECK(UN(NtGdiRestoreDC)(f.mem, -1));
    EXPECT_EQ(dc_level(f.mem), 2);
    CHECK(UN(NtGdiRestoreDC)(f.mem, 1));
    EXPECT_EQ(dc_level(f.mem), 1);
    dcf_close(&f);
}

TEST(gdi_dc_objects_syscall_save_restore_invalid_dc_sets_invalid_handle) {
    DCWANT(UN(NtGdiSaveDC)); DCWANT(UN(NtGdiRestoreDC));
    HDC stale = U(CreateCompatibleDC)(NULL);
    U(DeleteDC)(stale);
    HDC bad[3] = { NULL, stale, (HDC)U(GetStockObject)(BLACK_PEN) };
    for (int i = 0; i < 3; i++) {
        /* SaveDC / RestoreDC: object lookup fails -> EngSetLastError(ERROR_INVALID_HANDLE), 0. */
        SET_ERR(0);
        EXPECT_EQ(UN(NtGdiSaveDC)(bad[i]), 0);
        EXPECT_ERR(ERROR_INVALID_HANDLE);
        SET_ERR(0);
        CHECK(!UN(NtGdiRestoreDC)(bad[i], -1));
        EXPECT_ERR(ERROR_INVALID_HANDLE);
    }
}

TEST(gdi_dc_objects_syscall_restore_bad_level_sets_invalid_parameter) {
    DCWANT(UN(NtGdiSaveDC)); DCWANT(UN(NtGdiRestoreDC)); NEED_LEVEL();
    FX(f);
    UN(NtGdiSaveDC)(f.mem);
    static const int bad[] = { 0, 2, 3, -2, -50, INT_MAX, INT_MIN };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        SET_ERR(0);
        CHECKF(!UN(NtGdiRestoreDC)(f.mem, bad[i]), "level %d", bad[i]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
        EXPECT_EQ(dc_level(f.mem), 2);
    }
    dcf_close(&f);
}

TEST(gdi_dc_objects_syscall_restore_syncs_pending_pen_brush_selection) {
    DCWANT(UN(NtGdiSaveDC)); DCWANT(UN(NtGdiRestoreDC)); DCWANT(UN(NtGdiGetDCObject));
    FX(f);
    HBRUSH br = U(CreateSolidBrush)(RGB(9, 9, 9));
    HGDIOBJ br0 = U(GetCurrentObject)(f.mem, OBJ_BRUSH);
    UN(NtGdiSaveDC)(f.mem);
    U(SelectObject)(f.mem, br);
    CHECK(UN(NtGdiRestoreDC)(f.mem, -1));
    EXPECT_EQ(UN(NtGdiGetDCObject)(f.mem, K_DCOBJ_BRUSH), br0);
    U(GdiFlush)();
    CHECK(U(DeleteObject)(br));
    dcf_close(&f);
}
