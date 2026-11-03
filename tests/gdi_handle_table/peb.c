/* PEB->GdiSharedHandleTable and the GDI-related PEB/TEB fields.
 * win32kbase!GdiProcessCallout (process create): ZwMapViewOfSection(gpHmgrSharedHandleSection, PAGE_READONLY) into the
 * process, PEB+0xf8 = the view base, PEB+0x108 = 0x14, PEB+0x140 (0xf0 bytes) zeroed. gdi32full!GdiProcessSetup then
 * points pGdiSharedHandleTable and pGdiSharedMemory at that base and pGdiDevCaps at base + 0x180000 (0x10000 cells
 * of 0x18 bytes first); GdiBatchLimit starts as the byte at PEB+0x108. */
#include "gh.h"

TEST(gdi_handle_table_peb_table_pointer_user_range) {
    GH_NEED();
    void *b = gh_base();
    CHECK(b != NULL);
    CHECKF((ULONG_PTR)b < 0x00007FFFFFFF0000ull, "base %p", b);
    CHECK(((ULONG_PTR)b & 0xfff) == 0);
    t_note("GdiSharedHandleTable = %p", b);
}
TEST(gdi_handle_table_peb_table_is_readonly_section_view) {
    GH_NEED();
    MEMORY_BASIC_INFORMATION mbi;
    void *b = gh_base();
    REQUIRE(b);
    CHECK(VirtualQuery(b, &mbi, sizeof mbi) == sizeof mbi);
    CHECKF(mbi.Type == MEM_MAPPED, "type 0x%lx", mbi.Type);
    CHECKF(mbi.State == MEM_COMMIT, "state 0x%lx", mbi.State);
    CHECKF(mbi.Protect == PAGE_READONLY, "protect 0x%lx", mbi.Protect);
    CHECK(mbi.AllocationBase == b);
    /* the cell array alone is 0x10000 * 0x18 bytes and gdi32full reads shared data right behind it */
    CHECKF(mbi.RegionSize >= 0x180000, "region size 0x%llx", (unsigned long long)mbi.RegionSize);
}
TEST(gdi_handle_table_peb_last_cell_readable) {
    GH_NEED();
    struct gh_cell c;
    CHECK(gh_read(GH_CELLS - 1, &c));
    CHECK(gh_read(0, &c));
}
TEST(gdi_handle_table_peb_cell_zero_is_not_a_private_object) {
    GH_NEED();
    struct gh_cell c;
    REQUIRE(gh_read(0, &c));
    t_note("cell0 q0=0x%llx owner=0x%x upper=0x%04x objt=0x%02x", c.q0, c.owner, c.upper, c.objt);
    if (c.objt != 0) CHECK((c.owner & ~1u) != GH_PID());
}
TEST(gdi_handle_table_peb_shared_data_behind_cells_readable) {
    GH_NEED();
    char *b = gh_base();
    unsigned long long v;
    REQUIRE(b);
    /* devcaps block and the stock-object slots (+0x1800b0 .. +0x180210 .. ) read by gdi32full!GetStockObject / GetDeviceCaps */
    CHECK(k_try_read(b + 0x180000, &v, 8));
    CHECK(k_try_read(b + 0x1800b0, &v, 8));
    CHECK(k_try_read(b + 0x180160, &v, 8));
    CHECK(k_try_read(b + 0x180210, &v, 8));
}
TEST(gdi_handle_table_peb_stock_slots_hold_stock_handles) {
    GH_NEED();
    char *b = gh_base();
    REQUIRE(b);
    int stock_ok[] = { WHITE_BRUSH, LTGRAY_BRUSH, GRAY_BRUSH, DKGRAY_BRUSH, BLACK_BRUSH, NULL_BRUSH, WHITE_PEN, BLACK_PEN, NULL_PEN,
                       ANSI_FIXED_FONT, ANSI_VAR_FONT, DEVICE_DEFAULT_FONT, DEFAULT_PALETTE, DC_BRUSH, DC_PEN };
    for (size_t i = 0; i < sizeof stock_ok / sizeof stock_ok[0]; i++) {
        unsigned long long v = 0;
        CHECK(k_try_read(b + 0x1800b0 + (ULONG_PTR)stock_ok[i] * 8, &v, 8));
        CHECKF((v & GH_STOCK) != 0, "slot %d = 0x%llx has no stock bit", stock_ok[i], v);
        CHECK((HGDIOBJ)(ULONG_PTR)v == U(GetStockObject)(stock_ok[i]));
    }
}
TEST(gdi_handle_table_peb_table_pointer_stable) {
    GH_NEED();
    void *a = gh_base();
    HRGN r = U(CreateRectRgn)(0, 0, 1, 1);
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(r && dc);
    CHECK(gh_base() == a);
    U(DeleteDC)(dc); U(DeleteObject)(r);
    CHECK(gh_base() == a);
}
TEST(gdi_handle_table_peb_dc_attribute_list_is_batch_limit) {
    GH_NEED();
    unsigned long v = 0;
    REQUIRE(k_try_read_u32((char *)k_peb() + PEB_GdiDCAttributeList, &v));
    /* GdiProcessCallout stores 0x14 here; gdi32full!GdiProcessSetup copies the byte into GdiBatchLimit */
    CHECKF(v == 0x14, "PEB.GdiDCAttributeList = 0x%lx", v);
}
TEST(gdi_handle_table_peb_batch_limit_roundtrip) {
    GH_NEED();
    REQUIRE(U(GdiSetBatchLimit));
    DWORD orig = U(GdiGetBatchLimit)();
    t_note("initial GdiGetBatchLimit = %lu", orig);
    /* GdiSetBatchLimit(dw): dw == 0 means 0x14; accepted when 1 <= value <= 0x14; returns the previous limit, else 0 */
    DWORD prev = U(GdiSetBatchLimit)(5);
    CHECK(prev == orig);
    CHECK(U(GdiGetBatchLimit)() == 5);
    CHECK(U(GdiSetBatchLimit)(0) == 5);
    CHECK(U(GdiGetBatchLimit)() == 0x14);
    CHECK(U(GdiSetBatchLimit)(0x15) == 0);
    CHECK(U(GdiGetBatchLimit)() == 0x14);
    CHECK(U(GdiSetBatchLimit)(0xffffffff) == 0);
    CHECK(U(GdiGetBatchLimit)() == 0x14);
    CHECK(U(GdiSetBatchLimit)(1) == 0x14);
    CHECK(U(GdiGetBatchLimit)() == 1);
    U(GdiSetBatchLimit)(orig ? orig : 0x14);
    CHECK(U(GdiGetBatchLimit)() == (orig ? orig : 0x14));
}
TEST(gdi_handle_table_peb_gdiflush_returns_true) {
    GH_NEED();
    REQUIRE(U(GdiFlush));
    HDC dc = U(CreateCompatibleDC)(NULL);
    HBITMAP bm = U(CreateBitmap)(8, 8, 1, 32, NULL);
    REQUIRE(dc && bm);
    HGDIOBJ old = U(SelectObject)(dc, bm);
    U(PatBlt)(dc, 0, 0, 4, 4, BLACKNESS);
    unsigned long before = 0, after = 0;
    k_try_read_u32((char *)k_teb() + TEB_GdiBatchCount, &before);
    CHECK(U(GdiFlush)());
    k_try_read_u32((char *)k_teb() + TEB_GdiBatchCount, &after);
    t_note("TEB GdiBatchCount %lu -> %lu around GdiFlush", before, after);
    U(SelectObject)(dc, old);
    U(DeleteDC)(dc); U(DeleteObject)(bm);
}
TEST(gdi_handle_table_peb_w32pid_is_client_pid) {
    GH_NEED();
    /* gdi32full!GdiProcessSetup: gW32PID = TEB.ClientId.UniqueProcess & ~3; every cell owner of this process equals it */
    HRGN r = U(CreateRectRgn)(0, 0, 1, 1);
    struct gh_cell c;
    REQUIRE(r);
    CHECK(gh_cell_of(r, &c));
    CHECK((c.owner & ~1u) == (unsigned)(GetCurrentProcessId() & ~3u));
    CHECK((GetCurrentProcessId() & 3) == 0);
    U(DeleteObject)(r);
}
TEST(gdi_handle_table_peb_teb_gdi_client_ids_note) {
    GH_NEED();
    unsigned long pid = 0, tid = 0;
    k_try_read_u32((char *)k_teb() + TEB_GdiClientPID, &pid);
    k_try_read_u32((char *)k_teb() + TEB_GdiClientTID, &tid);
    t_info("TEB GdiClientPID=%lu GdiClientTID=%lu (process %lu thread %lu)", pid, tid, GetCurrentProcessId(), GetCurrentThreadId());
}
TEST(gdi_handle_table_peb_gdi_handle_buffer_note) {
    GH_NEED();
    unsigned char buf[0xf0];
    int nz = 0;
    REQUIRE(k_try_read((char *)k_peb() + PEB_GdiHandleBuffer, buf, sizeof buf));
    for (int i = 0; i < 0xf0; i++) nz += buf[i] != 0;
    t_info("PEB.GdiHandleBuffer has %d non-zero bytes", nz);
}
TEST(gdi_handle_table_peb_getdc_cache_cells_owned) {
    GH_NEED();
    REQUIRE(U(GetDC));
    HDC dc = U(GetDC)(NULL);
    struct gh_cell c;
    REQUIRE(dc);
    CHECK(gh_cell_of(dc, &c));
    CHECK(c.objt == 1);
    CHECK((c.owner & ~1u) == GH_PID());
    U(ReleaseDC)(NULL, dc);
}
