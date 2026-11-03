/* DC creation / deletion through NtGdiCreateCompatibleDC, NtGdiOpenDCW and NtGdiDeleteObjectApp, and the
 * handle-table fingerprints a live DC leaves in this process. */
#include "dc.h"

/* The cell of a live DC: type 1, owner = this pid, upper bits = handle bits 16..31, pUser = DC_ATTR in user space. */
static void check_dc_cell(HDC dc, const char *what) {
    struct dc_cell c;
    if (!dc_cell_of((HGDIOBJ)dc, &c)) { t_skip("GdiSharedHandleTable unreadable"); return; }
    CHECKF(H_TYPE7(dc) == T7_DC, "%s: type bits 0x%x", what, H_TYPE7(dc));
    CHECKF(c.objt == 1, "%s: cell objt 0x%x", what, c.objt);
    CHECKF(c.upper == (unsigned short)((ULONG_PTR)dc >> 16), "%s: cell upper 0x%x handle %p", what, c.upper, dc);
    /* gdi32full compares (owner & 0xfffffffe) == gW32PID for every DC access. */
    CHECKF((c.owner & 0xfffffffeu) == (GetCurrentProcessId() & 0xfffffffeu), "%s: owner 0x%x", what, c.owner);
    CHECKF(c.puser != 0 && c.puser < 0xFFFF800000000000ull, "%s: pUser 0x%llx", what, c.puser);
    CHECKF(c.q0 >= 0xFFFF800000000000ull, "%s: kernel object pointer 0x%llx", what, c.q0);
}

TEST(gdi_dc_objects_compat_dc_null_is_memdc) {
    REQUIRE(U(CreateCompatibleDC)); REQUIRE(U(GetObjectType)); REQUIRE(U(DeleteDC));
    /* CreateCompatibleDC(NULL) builds a memory DC compatible with the primary display. */
    HDC dc = U(CreateCompatibleDC)(NULL);
    CHECK(dc != NULL);
    if (!dc) return;
    EXPECT_EQ(H_TYPE7(dc), T7_DC);
    EXPECT_EQ(U(GetObjectType)(dc), OBJ_MEMDC);
    CHECK(U(DeleteDC)(dc));
}

TEST(gdi_dc_objects_compat_dc_of_screen_is_memdc) {
    REQUIRE(U(CreateCompatibleDC)); REQUIRE(U(GetDC)); REQUIRE(U(ReleaseDC));
    HDC scr = U(GetDC)(NULL);
    HDC dc = U(CreateCompatibleDC)(scr);
    CHECK(dc != NULL);
    if (dc) {
        EXPECT_EQ(U(GetObjectType)(dc), OBJ_MEMDC);
        EXPECT_EQ(U(GetObjectType)(scr), OBJ_DC);
        EXPECT_NE(dc, scr);
        U(DeleteDC)(dc);
    }
    U(ReleaseDC)(NULL, scr);
}

TEST(gdi_dc_objects_compat_dc_of_memdc) {
    FX(f);
    /* CreateCompatibleDC accepts any DC handle (memory DCs too) and clones its device. */
    HDC dc2 = U(CreateCompatibleDC)(f.mem);
    CHECK(dc2 != NULL);
    if (dc2) {
        EXPECT_EQ(U(GetObjectType)(dc2), OBJ_MEMDC);
        EXPECT_NE(dc2, f.mem);
        U(DeleteDC)(dc2);
    }
    dcf_close(&f);
}

TEST(gdi_dc_objects_compat_dc_cell_fingerprint) {
    FX(f);
    check_dc_cell(f.mem, "memdc");
    dcf_close(&f);
}

TEST(gdi_dc_objects_screen_dc_cell_fingerprint) {
    REQUIRE(U(GetDC)); REQUIRE(U(ReleaseDC));
    HDC scr = U(GetDC)(NULL);
    CHECK(scr != NULL);
    if (scr) check_dc_cell(scr, "screen dc");
    U(ReleaseDC)(NULL, scr);
}

TEST(gdi_dc_objects_two_compat_dcs_have_distinct_cells_and_attrs) {
    FX(f);
    HDC b = U(CreateCompatibleDC)(f.scr);
    CHECK(b != NULL);
    if (b) {
        EXPECT_NE(H_INDEX(b), H_INDEX(f.mem));
        void *a1 = dc_attr(f.mem), *a2 = dc_attr(b);
        if (a1 && a2) EXPECT_NE(a1, a2);
        U(DeleteDC)(b);
    }
    dcf_close(&f);
}

TEST(gdi_dc_objects_dc_dword_kind_flag) {
    DCWANT(UN(NtGdiGetDCDword));
    FX(f);
    /* NtGdiGetDCDword index 7 is (dc type == memory); GetObjectType calls it to tell OBJ_MEMDC from OBJ_DC. */
    ULONG v = 0xdeadbeef;
    CHECK(UN(NtGdiGetDCDword)(f.mem, 7, &v));
    EXPECT_EQ(v, 1);
    v = 0xdeadbeef;
    CHECK(UN(NtGdiGetDCDword)(f.scr, 7, &v));
    EXPECT_EQ(v, 0);
    dcf_close(&f);
}

TEST(gdi_dc_objects_syscall_create_compatible_dc) {
    DCWANT(UN(NtGdiCreateCompatibleDC)); DCWANT(UN(NtGdiDeleteObjectApp));
    HDC scr = U(GetDC)(NULL);
    HDC a = UN(NtGdiCreateCompatibleDC)(NULL);
    HDC b = UN(NtGdiCreateCompatibleDC)(scr);
    CHECK(a != NULL); CHECK(b != NULL);
    if (a) { EXPECT_EQ(H_TYPE7(a), T7_DC); check_dc_cell(a, "NtGdiCreateCompatibleDC(NULL)"); EXPECT_EQ(U(GetObjectType)(a), OBJ_MEMDC); }
    if (b) { EXPECT_EQ(H_TYPE7(b), T7_DC); check_dc_cell(b, "NtGdiCreateCompatibleDC(scr)"); EXPECT_EQ(U(GetObjectType)(b), OBJ_MEMDC); }
    /* NtGdiDeleteObjectApp on a DC handle goes to bDeleteDCInternalEx and returns nonzero. */
    if (a) CHECK(UN(NtGdiDeleteObjectApp)(a));
    if (b) CHECK(UN(NtGdiDeleteObjectApp)(b));
    if (a) { EXPECT_DEAD(a); CHECK(!dc_cell_live((HGDIOBJ)a, T7_DC)); }
    U(ReleaseDC)(NULL, scr);
}

TEST(gdi_dc_objects_syscall_create_compatible_dc_bad_handles) {
    DCWANT(UN(NtGdiCreateCompatibleDC));
    FX(f);
    HPEN pen = U(CreatePen)(PS_SOLID, 1, RGB(1, 2, 3));
    HDC stale = U(CreateCompatibleDC)(NULL);
    U(DeleteDC)(stale);
    /* CreateCompatibleDC requires the argument to be a DC; a non-DC handle or a freed one gives NULL. */
    HDC r = UN(NtGdiCreateCompatibleDC)((HDC)pen);
    CHECK(r == NULL); if (r) U(DeleteDC)(r);
    r = UN(NtGdiCreateCompatibleDC)(stale);
    CHECK(r == NULL); if (r) U(DeleteDC)(r);
    U(DeleteObject)(pen);
    dcf_close(&f);
}

TEST(gdi_dc_objects_delete_dc_twice) {
    REQUIRE(U(CreateCompatibleDC));
    HDC dc = U(CreateCompatibleDC)(NULL);
    CHECK(dc != NULL);
    CHECK(U(DeleteDC)(dc));
    SET_ERR(0);
    CHECK(!U(DeleteDC)(dc));
}

TEST(gdi_dc_objects_deleted_dc_handle_is_dead) {
    REQUIRE(U(CreateCompatibleDC));
    HDC dc = U(CreateCompatibleDC)(NULL);
    CHECK(dc != NULL);
    U(DeleteDC)(dc);
    /* The table slot is released: the cell no longer holds a type 1 entry for this handle. */
    CHECK(!dc_cell_live((HGDIOBJ)dc, T7_DC));
    EXPECT_DEAD(dc);
    /* gdi32!GetDeviceCaps: a DC-typed handle with no live entry raises ERROR_INVALID_PARAMETER and returns 0. */
    SET_ERR(0);
    EXPECT_EQ(U(GetDeviceCaps)(dc, HORZRES), 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    /* SelectObject on a dead DC handle: entry validation fails in SelectObjectImpl -> 0x57. */
    HGDIOBJ pen = U(GetStockObject)(BLACK_PEN);
    SET_ERR(0);
    CHECK(U(SelectObject)(dc, pen) == NULL);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}

TEST(gdi_dc_objects_delete_dc_null_and_junk) {
    REQUIRE(U(DeleteDC));
    SET_ERR(0);
    CHECK(!U(DeleteDC)(NULL));
    /* InternalDeleteDC: a non-DC-typed handle goes through pldcGet(), which fails for NULL: ERROR_INVALID_HANDLE. */
    EXPECT_ERR(ERROR_INVALID_HANDLE);
}

TEST(gdi_dc_objects_delete_dc_on_pen_keeps_pen) {
    REQUIRE(U(CreatePen)); REQUIRE(U(DeleteDC));
    HPEN pen = U(CreatePen)(PS_SOLID, 1, RGB(9, 8, 7));
    CHECK(pen != NULL);
    /* InternalDeleteDC bails out (cell type is not 1) before NtGdiDeleteObjectApp. */
    CHECK(!U(DeleteDC)((HDC)pen));
    EXPECT_EQ(U(GetObjectType)(pen), OBJ_PEN);
    CHECK(U(DeleteObject)(pen));
}

TEST(gdi_dc_objects_deleteobject_on_dc_deletes_it) {
    REQUIRE(U(CreateCompatibleDC)); REQUIRE(U(DeleteObject));
    HDC dc = U(CreateCompatibleDC)(NULL);
    CHECK(dc != NULL);
    /* InternalDeleteObject: cell type 1 -> InternalDeleteDC(h). The DC is gone afterwards. */
    BOOL r = U(DeleteObject)(dc);
    t_note("DeleteObject(hdc) returned %d", r);
    EXPECT_DEAD(dc);
    CHECK(!dc_cell_live((HGDIOBJ)dc, T7_DC));
}

TEST(gdi_dc_objects_delete_syscall_null_handle) {
    DCWANT(UN(NtGdiDeleteObjectApp));
    /* NULL has object type 0, which matches none of the deletable kinds in NtGdiDeleteObjectApp: FALSE. */
    CHECK(!UN(NtGdiDeleteObjectApp)(NULL));
}

TEST(gdi_dc_objects_delete_syscall_pen_then_stale) {
    DCWANT(UN(NtGdiDeleteObjectApp)); REQUIRE(U(CreatePen));
    HPEN pen = U(CreatePen)(PS_SOLID, 1, RGB(1, 1, 1));
    CHECK(pen != NULL);
    /* The syscall deletes at once (no TEB batching, which only gdi32full!InternalDeleteObject does). */
    CHECK(UN(NtGdiDeleteObjectApp)(pen));
    /* bDeleteBrush may park an unreferenced pen in the PEB handle cache instead of freeing the cell, so only
     * record what became of the handle. */
    t_note("pen after NtGdiDeleteObjectApp: GetObjectType=%lu live=%d", U(GetObjectType)(pen), dc_cell_live((HGDIOBJ)pen, T7_PEN));
}

TEST(gdi_dc_objects_delete_syscall_brush_region_bitmap) {
    DCWANT(UN(NtGdiDeleteObjectApp));
    HBRUSH br = U(CreateSolidBrush)(RGB(1, 2, 3));
    HRGN rg = U(CreateRectRgn)(0, 0, 4, 4);
    HBITMAP bm = U(CreateBitmap)(4, 4, 1, 1, NULL);
    CHECK(br && rg && bm);
    /* NtGdiDeleteObjectApp dispatches on the type bits: brush -> bDeleteBrush, region -> RGNOBJAPI, bitmap -> bDeleteSurface. */
    CHECK(UN(NtGdiDeleteObjectApp)(br));
    CHECK(UN(NtGdiDeleteObjectApp)(rg));
    CHECK(UN(NtGdiDeleteObjectApp)(bm));
    t_note("brush after NtGdiDeleteObjectApp: GetObjectType=%lu", U(GetObjectType)(br));
    EXPECT_EQ(U(GetObjectType)(rg), 0);
    EXPECT_DEAD(bm);
}
