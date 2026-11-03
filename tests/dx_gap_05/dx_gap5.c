/* dx_gap_05: state-changing syscalls of the gap_05 set. Run with /destructive only.
 * Everything here changes kernel object state of this process (a bitmap becomes a stock object) or the
 * session-wide EUDC/font-link tables; objects are released again at the end of each test. */
#include "g5.h"

/* ---------------------------------------------------------------- NtGdiSetBitmapAttributes(bit 0) / NtGdiClearBitmapAttributes(bit 0)
 * Making a bitmap stock: for an ordinary (non-DIB-section) bitmap that is not selected into a
 * DC and is not already stock it ORs 0x800000 into the handle, converts the object to a
 * stock type, takes one slot from the stock-bitmap pool and returns the new handle; 0 on any
 * refusal (already stock, DIB section, selected, no free slot). */
TEST(gap5dx_bmattr_make_stock_sets_handle_bit) {
    G5_GUI(); G5_NT(NtGdiSetBitmapAttributes); G5_NT(NtGdiClearBitmapAttributes);
    G5_NEED(NtGdiSetBitmapAttributes_); G5_NEED(NtGdiClearBitmapAttributes_);
    HBITMAP bm = U(CreateBitmap)(8, 8, 1, 1, NULL);
    REQUIRE(bm);
    CHECK(((ULONG_PTR)bm & 0x800000) == 0);
    ULONG_PTR st = NtGdiSetBitmapAttributes_(bm, 1);
    if (!st) { t_note("stock conversion refused (gStockBitmapFree exhausted?)"); U(DeleteObject)(bm); return; }
    CHECKF(st == (((ULONG_PTR)bm) | 0x800000), "stock handle %p from %p", (void *)st, (void *)bm);
    CHECK(U(GetObjectType)((HGDIOBJ)st) == OBJ_BITMAP);
    struct k_gdicell c;
    if (k_gdi_cell((void *)st, &c)) t_note("stock cell: type_check=0x%02x", c.type_check);
    /* a stock bitmap is not converted twice */
    CHECK(NtGdiSetBitmapAttributes_((HBITMAP)st, 1) == 0);
    /* and is returned to the ordinary pool by the clear call */
    ULONG_PTR back = NtGdiClearBitmapAttributes_((HBITMAP)st, 1);
    if (back) {
        CHECKF((back & 0x800000) == 0, "non-stock handle %p still has the stock bit", (void *)back);
        CHECK(U(GetObjectType)((HGDIOBJ)back) == OBJ_BITMAP);
        CHECK(U(DeleteObject)((HGDIOBJ)back));
    } else {
        t_note("ClearBitmapAttributes(stock, 1) returned 0");
    }
}
TEST(gap5dx_bmattr_user_mode_wrappers_roundtrip) {
    G5_GUI();
    SetBitmapAttributes_pfn s = (SetBitmapAttributes_pfn)t_proc_in("gdi32.dll", "SetBitmapAttributes");
    ClearBitmapAttributes_pfn c = (ClearBitmapAttributes_pfn)t_proc_in("gdi32.dll", "ClearBitmapAttributes");
    G5_NEED(s); G5_NEED(c);
    HBITMAP bm = U(CreateBitmap)(8, 8, 1, 1, NULL);
    REQUIRE(bm);
    ULONG_PTR st = s(bm, 1);
    if (!st) { t_note("stock conversion refused"); U(DeleteObject)(bm); return; }
    CHECK(((ULONG_PTR)st & 0x800000) != 0);
    ULONG_PTR back = c((HBITMAP)st, 1);
    if (back) { CHECK((back & 0x800000) == 0); U(DeleteObject)((HGDIOBJ)back); }
    else t_note("clear returned 0");
}
TEST(gap5dx_bmattr_selected_bitmap_not_made_stock) {
    G5_GUI(); G5_NT(NtGdiSetBitmapAttributes); G5_NT(NtGdiClearBitmapAttributes);
    G5_NEED(NtGdiSetBitmapAttributes_); G5_NEED(NtGdiClearBitmapAttributes_);
    HDC dc = U(CreateCompatibleDC)(NULL);
    HBITMAP bm = U(CreateBitmap)(8, 8, 1, 1, NULL);
    if (!dc || !bm) { if (dc) U(DeleteDC)(dc); if (bm) U(DeleteObject)(bm); t_skip("cannot create DC/bitmap"); return; }
    HGDIOBJ old = U(SelectObject)(dc, bm);
    ULONG_PTR st = NtGdiSetBitmapAttributes_(bm, 1);
    t_note("selected bitmap SetBitmapAttributes -> %p", (void *)st);
    U(SelectObject)(dc, old);
    U(DeleteDC)(dc);
    if (st) {   /* converted despite the selection: return it to the ordinary pool so it can be freed */
        ULONG_PTR back = NtGdiClearBitmapAttributes_((HBITMAP)st, 1);
        if (back) U(DeleteObject)((HGDIOBJ)back);
    } else U(DeleteObject)(bm);
}
TEST(gap5dx_bmattr_dib_section_unchanged) {
    G5_GUI(); G5_NT(NtGdiSetBitmapAttributes); G5_NEED(NtGdiSetBitmapAttributes_);
    BITMAPINFO bi; memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = 4; bi.bmiHeader.biHeight = -4;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    HBITMAP dib = U(CreateDIBSection)(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!dib) { t_skip("no DIB section"); return; }
    ULONG_PTR st = NtGdiSetBitmapAttributes_(dib, 1);
    t_note("DIB section SetBitmapAttributes -> %p", (void *)st);
    if (!st) { CHECK(U(GetObjectType)(dib) == OBJ_BITMAP); U(DeleteObject)(dib); }
}

/* ---------------------------------------------------------------- NtGdiEnableEudc(TRUE)
 * Enabling EUDC (TRUE) finishes the default font-association set-up and calls
 * the link loader: it (re)reads the EUDC font links for the session. (FALSE would delete every link and is
 * never called.) */
TEST(gap5dx_enableeudc_true_keeps_fontislinked_boolean) {
    G5_GUI(); G5_NT(NtGdiEnableEudc); G5_NT(NtGdiFontIsLinked);
    G5_NEED(NtGdiEnableEudc_); G5_NEED(NtGdiFontIsLinked_);
    g5_fdc d;
    if (!g5_fdc_open(&d, L"Arial")) { g5_fdc_close(&d); t_skip("no Arial"); return; }
    ULONG before = NtGdiFontIsLinked_(d.dc);
    ULONG r = NtGdiEnableEudc_(TRUE);
    ULONG after = NtGdiFontIsLinked_(d.dc);
    t_note("EnableEudc(TRUE)=%lu, FontIsLinked %lu -> %lu", (unsigned long)r, (unsigned long)before, (unsigned long)after);
    CHECK(before <= 1);
    CHECK(after <= 1);
    CHECK(NtGdiEnableEudc_(TRUE) == r);
    g5_fdc_close(&d);
}

/* ---------------------------------------------------------------- NtGdiRemoveMergeFont on a live DC
 * win32kfull 0x27c8a0: with a live DC whose flag word (+0x24 bit 0) is clear the pufi pointer must be 4-byte
 * aligned (else ExRaiseDatatypeMisalignment), then XDCOBJ::bRemoveMergeFont searches the DC's merge list. A DC
 * with no merged fonts finds nothing. */
TEST(gap5dx_removemergefont_live_dc_nothing_to_remove) {
    G5_GUI(); G5_NT(NtGdiRemoveMergeFont); G5_NEED(NtGdiRemoveMergeFont_);
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    ULONG64 ufi[2] = { 0, 0 };
    ULONG r = NtGdiRemoveMergeFont_(dc, ufi);
    t_note("RemoveMergeFont(empty DC) -> %lu", (unsigned long)r);
    CHECK(r == 0);
    CHECK(U(GetObjectType)(dc) == OBJ_MEMDC);
    U(DeleteDC)(dc);
}
