/* DC lifetime, ownership and counting: GetGuiResources deltas, handle reuse, DC_ATTR defaults, and a DC handle used
 * from a process that does not own it (every user-mode accessor compares the cell owner with its own pid, every
 * kernel entry locks the object with an owner check). */
#include "dc.h"

#define CC(c) do { if (!(c)) { fails++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CCF(c, ...) do { if (!(c)) { fails++; printf("  FAIL %s:%d: %s (", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf(")\n"); } } while (0)

static DWORD gdi_objs(void) { return U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS); }

TEST(gdi_dc_objects_guiresources_counts_dcs) {
    REQUIRE(U(GetGuiResources)); REQUIRE(U(CreateCompatibleDC));
    HDC w = U(CreateCompatibleDC)(NULL); U(DeleteDC)(w);      /* warm up lazily created per-process state */
    DWORD base = gdi_objs();
    HDC a = U(CreateCompatibleDC)(NULL), b = U(CreateCompatibleDC)(a);
    EXPECT_EQ(gdi_objs(), base + 2);
    U(DeleteDC)(a);
    EXPECT_EQ(gdi_objs(), base + 1);
    U(DeleteDC)(b);
    EXPECT_EQ(gdi_objs(), base);
}

TEST(gdi_dc_objects_guiresources_screen_dc_not_leaked_by_release) {
    REQUIRE(U(GetGuiResources)); REQUIRE(U(GetDC));
    HDC w = U(GetDC)(NULL); U(ReleaseDC)(NULL, w);
    DWORD base = gdi_objs();
    for (int i = 0; i < 20; i++) { HDC d = U(GetDC)(NULL); U(ReleaseDC)(NULL, d); }
    EXPECT_EQ(gdi_objs(), base);
}

TEST(gdi_dc_objects_guiresources_selected_bitmap_survives_deferred_delete) {
    FX(f);
    DWORD base = gdi_objs();
    HBITMAP bm = U(CreateCompatibleBitmap)(f.scr, 4, 4);
    EXPECT_EQ(gdi_objs(), base + 1);
    HGDIOBJ def = U(SelectObject)(f.mem, bm);
    CHECK(U(DeleteObject)(bm));                        /* accepted but deferred while selected: the object still counts */
    EXPECT_EQ(gdi_objs(), base + 1);
    U(SelectObject)(f.mem, def);                       /* deselecting completes the deletion */
    EXPECT_EQ(gdi_objs(), base);
    CHECK(!U(DeleteObject)(bm));
    dcf_close(&f);
}

TEST(gdi_dc_objects_guiresources_delete_dc_with_bitmap_selected) {
    REQUIRE(U(GetGuiResources));
    FX(f);
    DWORD base = gdi_objs();
    HBITMAP bm = U(CreateCompatibleBitmap)(f.scr, 4, 4);
    U(SelectObject)(f.mem, bm);
    EXPECT_EQ(gdi_objs(), base + 1);
    U(DeleteDC)(f.mem); f.mem = NULL;
    /* the bitmap is not owned by the DC: it keeps counting until it is deleted itself */
    EXPECT_EQ(gdi_objs(), base);
    CHECK(U(DeleteObject)(bm));
    EXPECT_EQ(gdi_objs(), base - 1);   /* base still counted the memory DC that is now gone */
    dcf_close(&f);
}

TEST(gdi_dc_objects_handle_reuse_never_revives_old_handle) {
    REQUIRE(U(CreateCompatibleDC));
    HDC prev = NULL;
    int same_index = 0;
    for (int i = 0; i < 64; i++) {
        HDC d = U(CreateCompatibleDC)(NULL);
        CHECK(d != NULL);
        if (!d) break;
        if (prev) {
            /* the old handle is dead even when the new DC took the same table slot (the uniqueness byte differs) */
            if (H_INDEX(prev) == H_INDEX(d)) { same_index++; CHECKF(prev != d, "handle %p reissued", d); EXPECT_EQ(U(GetObjectType)(prev), 0); }
            else EXPECT_DEAD(prev);
        }
        EXPECT_EQ(U(GetObjectType)(d), OBJ_MEMDC);
        U(DeleteDC)(d);
        prev = d;
    }
    t_note("slot reused %d times in 64 create/delete rounds", same_index);
}

TEST(gdi_dc_objects_attr_defaults_of_new_dc) {
    FX(f);
    void *a = dc_attr(f.mem);
    if (!a) { dcf_close(&f); t_skip("DC_ATTR unreadable"); return; }
    /* A new DC starts with the stock pen/brush in the page, a font handle, the TEXT map mode and graphics mode 1. */
    EXPECT_EQ(da_ptr(a, DA_HBRUSH), U(GetStockObject)(WHITE_BRUSH));
    EXPECT_EQ(da_ptr(a, DA_HPEN), U(GetStockObject)(BLACK_PEN));
    CHECK(da_ptr(a, DA_HFONT) != NULL);
    EXPECT_EQ(da_u32(a, DA_GFXMODE), GM_COMPATIBLE);
    EXPECT_EQ(da_u32(a, DA_LAYOUT) & 7, 0);
    EXPECT_EQ(da_u32(a, DA_BKCLR), RGB(255, 255, 255));
    EXPECT_EQ(da_u32(a, DA_TXCLR), RGB(0, 0, 0));
    EXPECT_EQ(da_u32(a, DA_DCBRUSH), RGB(255, 255, 255));
    EXPECT_EQ(da_u32(a, DA_DCPEN), RGB(0, 0, 0));
    EXPECT_EQ(U(GetBkColor)(f.mem), RGB(255, 255, 255));
    EXPECT_EQ(U(GetTextColor)(f.mem), RGB(0, 0, 0));
    EXPECT_EQ(U(GetBkMode)(f.mem), OPAQUE);
    EXPECT_EQ(U(GetROP2)(f.mem), R2_COPYPEN);
    EXPECT_EQ(U(GetMapMode)(f.mem), MM_TEXT);
    EXPECT_EQ(U(GetPolyFillMode)(f.mem), ALTERNATE);
    EXPECT_EQ(U(GetStretchBltMode)(f.mem), BLACKONWHITE);
    EXPECT_EQ(U(GetTextAlign)(f.mem), TA_LEFT | TA_TOP | TA_NOUPDATECP);
    dcf_close(&f);
}

TEST(gdi_dc_objects_attr_page_is_per_dc_and_in_user_space) {
    FX(f);
    HDC d = U(CreateCompatibleDC)(f.scr);
    void *a = dc_attr(f.mem), *b = dc_attr(d);
    if (!a || !b) { U(DeleteDC)(d); dcf_close(&f); t_skip("DC_ATTR unreadable"); return; }
    CHECK((ULONG_PTR)a < 0x800000000000ull); CHECK((ULONG_PTR)b < 0x800000000000ull);
    EXPECT_NE(a, b);
    U(SetTextColor)(f.mem, RGB(9, 9, 9));
    EXPECT_EQ(da_u32(a, DA_TXCLR), RGB(9, 9, 9));
    EXPECT_EQ(da_u32(b, DA_TXCLR), RGB(0, 0, 0));
    U(DeleteDC)(d);
    dcf_close(&f);
}

TEST(gdi_dc_objects_compat_dc_does_not_inherit_state) {
    FX(f);
    HDC src = U(CreateDCW)(L"DISPLAY", NULL, NULL, NULL);
    if (!src) { dcf_close(&f); t_skip("CreateDCW failed"); return; }
    U(SetTextColor)(src, RGB(1, 2, 3));
    U(SetBkMode)(src, TRANSPARENT);
    U(SetMapMode)(src, MM_HIMETRIC);
    HDC c = U(CreateCompatibleDC)(src);
    CHECK(c != NULL);
    if (c) {
        /* CreateCompatibleDC makes a fresh DC from the device only. */
        EXPECT_EQ(U(GetTextColor)(c), RGB(0, 0, 0));
        EXPECT_EQ(U(GetBkMode)(c), OPAQUE);
        EXPECT_EQ(U(GetMapMode)(c), MM_TEXT);
        U(DeleteDC)(c);
    }
    U(DeleteDC)(src);
    dcf_close(&f);
}

/* ---- a DC owned by another process ---- */

CHILD(gdi_dc_objects_foreign_dc) {
    int fails = 0;
    HDC h = (HDC)(ULONG_PTR)strtoull(argv[1], NULL, 16);
    if (!U(GetObjectType) || !U(GetDeviceCaps) || !U(SaveDC) || !U(CreatePen)) { printf("  SKIP missing exports\n"); return 0; }
    HGDIOBJ pen = U(GetStockObject)(BLACK_PEN);
    /* user-mode accessors: the cell's owner (the parent's pid) differs from gW32PID */
    CCF(U(GetObjectType)(h) == 0, "GetObjectType %lu", U(GetObjectType)(h));
    SetLastError(0);
    CC(U(GetDeviceCaps)(h, HORZRES) == 0);
    CC(GetLastError() == ERROR_INVALID_PARAMETER);
    CC(U(SaveDC)(h) == 0);
    CC(!U(RestoreDC)(h, -1));
    CC(U(SelectObject)(h, pen) == NULL);
    CC(U(GetCurrentObject)(h, OBJ_PEN) == NULL);
    CC(U(GetCurrentObject)(h, OBJ_BITMAP) == NULL);
    CC(!U(DeleteDC)(h));
    SetLastError(0);
    CC(U(SetDCBrushColor)(h, RGB(1, 2, 3)) == CLR_INVALID);
    CC(GetLastError() == ERROR_INVALID_PARAMETER);
    /* kernel entries lock the DC with an owner check */
    if (UN(NtGdiSaveDC)) {
        SetLastError(0);
        CC(UN(NtGdiSaveDC)(h) == 0);
        CC(GetLastError() == ERROR_INVALID_HANDLE);
    }
    if (UN(NtGdiRestoreDC)) CC(!UN(NtGdiRestoreDC)(h, -1));
    if (UN(NtGdiGetDCObject)) {
        CC(UN(NtGdiGetDCObject)(h, K_DCOBJ_PEN) == NULL);
        CC(UN(NtGdiGetDCObject)(h, K_DCOBJ_BITMAP) == NULL);
    }
    if (UN(NtGdiSelectBitmap)) CC(UN(NtGdiSelectBitmap)(h, U(GetStockObject)(21)) == NULL);
    if (UN(NtGdiGetDCDword)) { ULONG v = 0xdeadbeef; CC(!UN(NtGdiGetDCDword)(h, 5, &v)); CC(v == 0xdeadbeef); }
    if (UN(NtGdiGetDeviceCaps)) {
        SetLastError(0);
        CC(UN(NtGdiGetDeviceCaps)(h, HORZRES) == 0);
        CC(GetLastError() == ERROR_INVALID_HANDLE);
    }
    if (UN(NtGdiDeleteObjectApp)) CC(!UN(NtGdiDeleteObjectApp)(h));
    if (UN(NtGdiCreateCompatibleDC)) {
        /* compatible DC of a foreign DC: the lock fails, nothing is created */
        HDC c = UN(NtGdiCreateCompatibleDC)(h);
        CC(c == NULL);
        if (c) U(DeleteDC)(c);
    }
    return fails;
}

TEST(gdi_dc_objects_foreign_dc_is_untouchable_from_another_process) {
    if (t_is_wine()) { t_skip("wine's gdi has no per-process ownership"); return; }
    FX(f);
    HBITMAP bm = U(CreateCompatibleBitmap)(f.scr, 4, 4);
    HGDIOBJ def = U(SelectObject)(f.mem, bm);
    char arg[32];
    snprintf(arg, sizeof arg, "%llx", (unsigned long long)(ULONG_PTR)f.mem);
    struct k_child_result r = k_child_run("gdi_dc_objects_foreign_dc", arg, 30000);
    EXPECT_EQ(r.exit_code, 0);
    /* nothing the child tried changed the DC */
    EXPECT_EQ(U(GetObjectType)(f.mem), OBJ_MEMDC);
    EXPECT_EQ(U(GetCurrentObject)(f.mem, OBJ_BITMAP), bm);
    ULONG lv = 0;
    if (UN(NtGdiGetDCDword)) { UN(NtGdiGetDCDword)(f.mem, 5, &lv); EXPECT_EQ(lv, 1); }
    U(SelectObject)(f.mem, def);
    U(DeleteObject)(bm);
    dcf_close(&f);
}

CHILD(gdi_dc_objects_first_dc) {
    int fails = 0;
    if (!U(CreateCompatibleDC) || !U(GetObjectType) || !U(GetGuiResources)) { printf("  SKIP missing exports\n"); return 0; }
    /* This process has made no GDI call yet other than loading gdi32/user32; the very first object is a memory DC. */
    HDC dc = U(CreateCompatibleDC)(NULL);
    CC(dc != NULL);
    if (!dc) return 100;
    void *table = NULL;
    CC(k_try_read_ptr((char *)k_peb() + PEB_GdiSharedHandleTable, &table) && table != NULL);
    void *ti = NULL;
    CC(k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &ti) && ti != NULL);    /* GUI thread after the first win32k call */
    struct dc_cell c;
    CC(dc_cell_of((HGDIOBJ)dc, &c));
    CC(c.objt == 1);
    CC((c.owner & 0xfffffffeu) == (GetCurrentProcessId() & 0xfffffffeu));
    CC(c.upper == (unsigned short)((ULONG_PTR)dc >> 16));
    CC(c.puser != 0 && c.puser < 0xFFFF800000000000ull);
    CC(U(GetObjectType)(dc) == OBJ_MEMDC);
    CC(U(GetCurrentObject)(dc, OBJ_PEN) == U(GetStockObject)(BLACK_PEN));
    CC(U(GetCurrentObject)(dc, OBJ_BRUSH) == U(GetStockObject)(WHITE_BRUSH));
    CC(U(GetCurrentObject)(dc, OBJ_BITMAP) == U(GetStockObject)(21));
    CC(U(SaveDC)(dc) == 1);
    CC(U(RestoreDC)(dc, -1));
    CC(U(DeleteDC)(dc));
    CC(!dc_cell_live((HGDIOBJ)dc, T7_DC));
    return fails;
}

TEST(gdi_dc_objects_first_gdi_call_in_fresh_process) {
    if (t_is_wine()) { t_skip("wine's gdi tables differ"); return; }
    struct k_child_result r = k_child_run("gdi_dc_objects_first_dc", NULL, 30000);
    EXPECT_EQ(r.exit_code, 0);
}
