/* Object type matrix, CreateDC/CreateIC/NtGdiOpenDCW combinations, layout, graphics mode, CancelDC/ResetDC,
 * ReleaseDC of non-cache DCs and GdiFlush. */
#include "dc.h"

/* ---------------- GetObjectType matrix ---------------- */

struct objcase { const char *name; unsigned t7; DWORD objtype; };

TEST(gdi_dc_objects_type_matrix_gdi_objects) {
    REQUIRE(U(GetObjectType));
    HDC scr = U(GetDC)(NULL);
    HPEN pen = U(CreatePen)(PS_SOLID, 1, 0);
    LOGBRUSH lb = { BS_SOLID, RGB(1, 2, 3), 0 };
    HPEN ext = U(ExtCreatePen)(PS_GEOMETRIC | PS_SOLID, 3, &lb, 0, NULL);
    HBRUSH br = U(CreateSolidBrush)(RGB(1, 2, 3));
    HBITMAP bm = U(CreateBitmap)(2, 2, 1, 1, NULL);
    HRGN rg = U(CreateRectRgn)(0, 0, 3, 3);
    LOGFONTW lf; memset(&lf, 0, sizeof lf); lf.lfHeight = -10; lstrcpyW(lf.lfFaceName, L"Arial");
    HFONT fn = U(CreateFontIndirectW)(&lf);
    union { LOGPALETTE lp; char raw[sizeof(LOGPALETTE) + 3 * sizeof(PALETTEENTRY)]; } pl;
    memset(&pl, 0, sizeof pl); pl.lp.palVersion = 0x300; pl.lp.palNumEntries = 4;
    HPALETTE pal = U(CreatePalette)(&pl.lp);
    struct { const char *n; HGDIOBJ h; unsigned t7; DWORD ot; } t[] = {
        { "pen", pen, T7_PEN, OBJ_PEN }, { "extpen", ext, 0x50, OBJ_EXTPEN }, { "brush", br, T7_BRUSH, OBJ_BRUSH },
        { "bitmap", bm, T7_BITMAP, OBJ_BITMAP }, { "region", rg, T7_REGION, OBJ_REGION }, { "font", fn, T7_FONT, OBJ_FONT },
        { "palette", pal, T7_PALETTE, OBJ_PAL }, { "screen dc", scr, T7_DC, OBJ_DC },
    };
    for (unsigned i = 0; i < sizeof t / sizeof *t; i++) {
        CHECKF(t[i].h != NULL, "create %s", t[i].n);
        if (!t[i].h) continue;
        /* gdi32full!GetObjectType dispatches on bits 16..22 of the handle after validating the cell. */
        CHECKF(H_TYPE7(t[i].h) == t[i].t7, "%s: type bits 0x%x", t[i].n, H_TYPE7(t[i].h));
        CHECKF(U(GetObjectType)(t[i].h) == t[i].ot, "%s: GetObjectType %lu want %lu", t[i].n, U(GetObjectType)(t[i].h), t[i].ot);
        CHECKF(!H_STOCKED(t[i].h), "%s has the stock bit", t[i].n);
    }
    /* bitmap, region and palette handles die immediately on delete (no PEB cache for them) */
    CHECK(U(DeleteObject)(bm)); CHECK(U(DeleteObject)(rg)); CHECK(U(DeleteObject)(fn)); CHECK(U(DeleteObject)(pal));
    EXPECT_DEAD(bm); EXPECT_EQ(U(GetObjectType)(rg), 0);
    EXPECT_DEAD(pal);
    t_note("font after DeleteObject: GetObjectType=%lu", U(GetObjectType)(fn));
    U(DeleteObject)(pen); U(DeleteObject)(ext); U(DeleteObject)(br);
    U(ReleaseDC)(NULL, scr);
}

TEST(gdi_dc_objects_type_matrix_dcs) {
    FX(f);
    HDC ic = U(CreateICW)(L"DISPLAY", NULL, NULL, NULL);
    HDC cdc = U(CreateDCW)(L"DISPLAY", NULL, NULL, NULL);
    EXPECT_EQ(U(GetObjectType)(f.scr), OBJ_DC);
    EXPECT_EQ(U(GetObjectType)(f.mem), OBJ_MEMDC);
    if (ic) { EXPECT_EQ(U(GetObjectType)(ic), OBJ_DC); U(DeleteDC)(ic); } else t_note("CreateICW(DISPLAY) failed");
    if (cdc) { EXPECT_EQ(U(GetObjectType)(cdc), OBJ_DC); U(DeleteDC)(cdc); } else t_note("CreateDCW(DISPLAY) failed");
    dcf_close(&f);
}

TEST(gdi_dc_objects_type_matrix_metafiles) {
    REQUIRE(U(CreateMetaFileW)); REQUIRE(U(CreateEnhMetaFileW));
    HDC m = U(CreateMetaFileW)(NULL);
    if (m) {
        EXPECT_EQ(U(GetObjectType)(m), OBJ_METADC);
        HMETAFILE h = U(CloseMetaFile)(m);
        CHECK(h != NULL);
        if (h) { EXPECT_EQ(U(GetObjectType)(h), OBJ_METAFILE); U(DeleteMetaFile)(h); }
    } else t_note("CreateMetaFileW failed");
    HDC e = U(CreateEnhMetaFileW)(NULL, NULL, NULL, NULL);
    if (e) {
        EXPECT_EQ(U(GetObjectType)(e), OBJ_ENHMETADC);
        HENHMETAFILE h = U(CloseEnhMetaFile)(e);
        CHECK(h != NULL);
        if (h) { EXPECT_EQ(U(GetObjectType)(h), OBJ_ENHMETAFILE); U(DeleteEnhMetaFile)(h); }
    } else t_note("CreateEnhMetaFileW failed");
}

TEST(gdi_dc_objects_type_zero_for_invalid_handles) {
    REQUIRE(U(GetObjectType));
    HDC dead = U(CreateCompatibleDC)(NULL);
    U(DeleteDC)(dead);
    HBITMAP bm = U(CreateBitmap)(1, 1, 1, 1, NULL);
    U(DeleteObject)(bm);
    EXPECT_EQ(U(GetObjectType)(NULL), 0);
    /* deleted handles: the cell is free, but GetObjectType still decodes the handle bits (see h_cell_dead) unless the slot
     * was taken over by another object, so only the cell state is asserted */
    EXPECT_DEAD(dead);
    EXPECT_DEAD(bm);
    EXPECT_EQ(U(GetObjectType)((HGDIOBJ)(ULONG_PTR)0xffffffffull), 0);
    EXPECT_EQ(U(GetObjectType)((HGDIOBJ)(ULONG_PTR)0x12345678ull), 0);
}

TEST(gdi_dc_objects_type_dc_handle_cell_matches_kernel_kind) {
    DCWANT(UN(NtGdiGetDCDword));
    FX(f);
    /* OBJ_MEMDC iff NtGdiGetDCDword(7) is nonzero (gdi32full!GetObjectType). */
    ULONG m = 9, s = 9;
    UN(NtGdiGetDCDword)(f.mem, 7, &m); UN(NtGdiGetDCDword)(f.scr, 7, &s);
    EXPECT_EQ(m != 0, U(GetObjectType)(f.mem) == OBJ_MEMDC);
    EXPECT_EQ(s != 0, U(GetObjectType)(f.scr) == OBJ_MEMDC);
    dcf_close(&f);
}

/* ---------------- DC creation combinations ---------------- */

static int primary_device(wchar_t *out, size_t cap) {
    DISPLAY_DEVICEW dd; 
    for (DWORD i = 0; i < 16; i++) {
        memset(&dd, 0, sizeof dd); dd.cb = sizeof dd;
        if (!U(EnumDisplayDevicesW)(NULL, i, &dd, 0)) return 0;
        if (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) { lstrcpynW(out, dd.DeviceName, (int)cap); return 1; }
    }
    return 0;
}

TEST(gdi_dc_objects_createdc_display_driver) {
    REQUIRE(U(CreateDCW));
    HDC dc = U(CreateDCW)(L"DISPLAY", NULL, NULL, NULL);
    CHECK(dc != NULL);
    if (!dc) return;
    EXPECT_EQ(H_TYPE7(dc), T7_DC);
    EXPECT_EQ(U(GetObjectType)(dc), OBJ_DC);
    EXPECT_EQ(U(GetDeviceCaps)(dc, TECHNOLOGY), DT_RASDISPLAY);
    EXPECT_EQ(U(GetDeviceCaps)(dc, PLANES), 1);
    struct dc_cell c;
    if (dc_cell_of((HGDIOBJ)dc, &c)) EXPECT_EQ(c.objt, 1);
    CHECK(U(DeleteDC)(dc));
}

TEST(gdi_dc_objects_createdc_display_matches_screen_dc_format) {
    FX(f);
    HDC dc = U(CreateDCW)(L"DISPLAY", NULL, NULL, NULL);
    if (!dc) { dcf_close(&f); t_skip("CreateDCW failed"); return; }
    EXPECT_EQ(U(GetDeviceCaps)(dc, BITSPIXEL), U(GetDeviceCaps)(f.scr, BITSPIXEL));
    EXPECT_EQ(U(GetDeviceCaps)(dc, PLANES), U(GetDeviceCaps)(f.scr, PLANES));
    EXPECT_EQ(U(GetDeviceCaps)(dc, RASTERCAPS), U(GetDeviceCaps)(f.scr, RASTERCAPS));
    t_note("CreateDC HORZRES=%d, GetDC(NULL) HORZRES=%d", U(GetDeviceCaps)(dc, HORZRES), U(GetDeviceCaps)(f.scr, HORZRES));
    U(DeleteDC)(dc);
    dcf_close(&f);
}

TEST(gdi_dc_objects_createdc_ansi_display) {
    REQUIRE(U(CreateDCA));
    HDC dc = U(CreateDCA)("DISPLAY", NULL, NULL, NULL);
    CHECK(dc != NULL);
    if (dc) { EXPECT_EQ(U(GetObjectType)(dc), OBJ_DC); CHECK(U(DeleteDC)(dc)); }
}

TEST(gdi_dc_objects_createdc_by_device_name) {
    wchar_t dev[64];
    if (!U(EnumDisplayDevicesW) || !primary_device(dev, 64)) { t_skip("no primary display device"); return; }
    /* bCreateDCW: a device name starting with \\.\DISPLAY selects the display path (bDisplay = 1). */
    HDC dc = U(CreateDCW)(NULL, dev, NULL, NULL);
    if (!dc) { t_note("CreateDCW(NULL, primary device) failed, error %lu", GetLastError()); return; }
    EXPECT_EQ(U(GetObjectType)(dc), OBJ_DC);
    EXPECT_EQ(U(GetDeviceCaps)(dc, TECHNOLOGY), DT_RASDISPLAY);
    HDC dc2 = U(CreateDCW)(L"DISPLAY", dev, NULL, NULL);
    if (dc2) { EXPECT_EQ(U(GetObjectType)(dc2), OBJ_DC); U(DeleteDC)(dc2); }
    U(DeleteDC)(dc);
}

TEST(gdi_dc_objects_createic_display_is_info_dc) {
    DCWANT(UN(NtGdiGetDCDword));
    REQUIRE(U(CreateICW));
    HDC ic = U(CreateICW)(L"DISPLAY", NULL, NULL, NULL);
    CHECK(ic != NULL);
    if (!ic) return;
    EXPECT_EQ(H_TYPE7(ic), T7_DC);
    EXPECT_EQ(U(GetObjectType)(ic), OBJ_DC);
    ULONG v = 9;
    CHECK(UN(NtGdiGetDCDword)(ic, 7, &v));
    EXPECT_EQ(v, 0);                       /* not a memory DC */
    EXPECT_EQ(U(GetDeviceCaps)(ic, TECHNOLOGY), DT_RASDISPLAY);
    CHECK(U(DeleteDC)(ic));
}

TEST(gdi_dc_objects_createdc_without_device_fails) {
    REQUIRE(U(CreateDCW));
    /* hdcCreateDCW returns NULL when no device name was derived and the driver is not DISPLAY. */
    HDC dc = U(CreateDCW)(NULL, NULL, NULL, NULL);
    CHECK(dc == NULL); if (dc) U(DeleteDC)(dc);
    dc = U(CreateDCW)(L"NOSUCHDRIVER", NULL, NULL, NULL);
    CHECK(dc == NULL); if (dc) U(DeleteDC)(dc);
    HDC ic = U(CreateICW)(NULL, NULL, NULL, NULL);
    CHECK(ic == NULL); if (ic) U(DeleteDC)(ic);
}

TEST(gdi_dc_objects_createdc_nonexistent_printer_fails) {
    REQUIRE(U(CreateDCW));
    if (t_is_wine()) { t_skip("spooler path is not meaningful under wine"); return; }
    wchar_t name[96];
    wsprintfW(name, L"win32kt_no_such_printer_%lu", GetCurrentProcessId());
    HDC dc = U(CreateDCW)(L"WINSPOOL", name, NULL, NULL);
    CHECK(dc == NULL);
    if (dc) U(DeleteDC)(dc);
}

TEST(gdi_dc_objects_createdc_unknown_display_device_note) {
    REQUIRE(U(CreateDCW));
    HDC dc = U(CreateDCW)(L"DISPLAY", L"\\\\.\\DISPLAY999", NULL, NULL);
    t_note("CreateDCW(DISPLAY, \\\\.\\DISPLAY999) = %p error %lu", dc, GetLastError());
    if (dc) U(DeleteDC)(dc);
}

TEST(gdi_dc_objects_createdc_many_and_distinct) {
    REQUIRE(U(CreateDCW));
    HDC dc[16]; int n = 0;
    for (int i = 0; i < 16; i++) { dc[i] = U(CreateDCW)(L"DISPLAY", NULL, NULL, NULL); if (!dc[i]) break; n++; }
    CHECK(n == 16);
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++) CHECKF(dc[i] != dc[j], "duplicate handle %p", dc[i]);
    for (int i = 0; i < n; i++) CHECK(U(DeleteDC)(dc[i]));
    for (int i = 0; i < n; i++) EXPECT_DEAD(dc[i]);
}

struct us { USHORT Length, MaximumLength; ULONG pad; PWSTR Buffer; };

TEST(gdi_dc_objects_syscall_opendcw_null_device_is_display_dc) {
    DCWANT(UN(NtGdiOpenDCW)); DCWANT(UN(NtGdiGetDCDword));
    if (t_is_wine()) { t_skip("NtGdiOpenDCW argument list differs under wine"); return; }
    /* NtGdiOpenDCW with no device name: UserGetDesktopDC(iType, 0, 1) - what CreateDC("DISPLAY") passes. */
    HDC dc = UN(NtGdiOpenDCW)(NULL, NULL, NULL, 0, 1, 0, NULL, NULL, NULL);
    CHECK(dc != NULL);
    if (!dc) return;
    EXPECT_EQ(H_TYPE7(dc), T7_DC);
    EXPECT_EQ(U(GetObjectType)(dc), OBJ_DC);
    EXPECT_EQ(U(GetDeviceCaps)(dc, TECHNOLOGY), DT_RASDISPLAY);
    ULONG v = 9; UN(NtGdiGetDCDword)(dc, 7, &v); EXPECT_EQ(v, 0);
    CHECK(U(DeleteDC)(dc));
}

TEST(gdi_dc_objects_syscall_opendcw_info_type) {
    DCWANT(UN(NtGdiOpenDCW)); DCWANT(UN(NtGdiGetDCDword));
    if (t_is_wine()) { t_skip("NtGdiOpenDCW argument list differs under wine"); return; }
    /* gdi32full!hdcCreateDCW passes iType 2 for CreateIC. */
    HDC dc = UN(NtGdiOpenDCW)(NULL, NULL, NULL, 2, 1, 0, NULL, NULL, NULL);
    CHECK(dc != NULL);
    if (!dc) return;
    EXPECT_EQ(U(GetObjectType)(dc), OBJ_DC);
    ULONG v = 9; UN(NtGdiGetDCDword)(dc, 7, &v); EXPECT_EQ(v, 0);
    EXPECT_EQ(U(GetDeviceCaps)(dc, TECHNOLOGY), DT_RASDISPLAY);
    CHECK(U(DeleteDC)(dc));
}

TEST(gdi_dc_objects_syscall_opendcw_named_display) {
    DCWANT(UN(NtGdiOpenDCW));
    wchar_t dev[64];
    if (t_is_wine()) { t_skip("NtGdiOpenDCW argument list differs under wine"); return; }
    if (!U(EnumDisplayDevicesW) || !primary_device(dev, 64)) { t_skip("no primary display device"); return; }
    struct us u; memset(&u, 0, sizeof u);
    u.Length = (USHORT)(lstrlenW(dev) * 2); u.MaximumLength = u.Length + 2; u.Buffer = dev;
    HDC dc = UN(NtGdiOpenDCW)(&u, NULL, NULL, 0, 1, 0, NULL, NULL, NULL);
    if (!dc) { t_note("NtGdiOpenDCW(named primary) failed, error %lu", GetLastError()); return; }
    EXPECT_EQ(U(GetObjectType)(dc), OBJ_DC);
    EXPECT_EQ(U(GetDeviceCaps)(dc, TECHNOLOGY), DT_RASDISPLAY);
    CHECK(U(DeleteDC)(dc));
}

TEST(gdi_dc_objects_syscall_opendcw_unknown_display_note) {
    DCWANT(UN(NtGdiOpenDCW));
    if (t_is_wine()) { t_skip("NtGdiOpenDCW argument list differs under wine"); return; }
    static WCHAR nm[] = L"\\\\.\\DISPLAY999";
    struct us u; memset(&u, 0, sizeof u);
    u.Length = (USHORT)(lstrlenW(nm) * 2); u.MaximumLength = u.Length + 2; u.Buffer = nm;
    HDC dc = UN(NtGdiOpenDCW)(&u, NULL, NULL, 0, 1, 0, NULL, NULL, NULL);
    t_note("NtGdiOpenDCW(\\\\.\\DISPLAY999) = %p, error %lu", dc, GetLastError());
    if (dc) U(DeleteDC)(dc);
}

/* ---------------- ReleaseDC / CancelDC / ResetDC ---------------- */

TEST(gdi_dc_objects_releasedc_on_created_dc_destroys_it) {
    REQUIRE(U(CreateDCW));
    HDC dc = U(CreateDCW)(L"DISPLAY", NULL, NULL, NULL);
    if (!dc) { t_skip("CreateDCW failed"); return; }
    int r = U(ReleaseDC)(NULL, dc);
    t_note("ReleaseDC(NULL, CreateDC dc) = %d", r);
    /* Windows 10 destroys a CreateDC device context when it is handed to ReleaseDC: the call reports 1, the cell is
     * freed and a following DeleteDC fails. (A memory DC survives ReleaseDC: it returns 0.) */
    EXPECT_EQ(r, 1);
    EXPECT_DEAD(dc);
    CHECK(!U(DeleteDC)(dc));
}

TEST(gdi_dc_objects_releasedc_on_memory_dc_keeps_it) {
    FX(f);
    int r = U(ReleaseDC)(NULL, f.mem);
    t_note("ReleaseDC(NULL, memdc) = %d", r);
    EXPECT_EQ(U(GetObjectType)(f.mem), OBJ_MEMDC);
    dcf_close(&f);
}

TEST(gdi_dc_objects_getdc_release_returns_one_and_recycles) {
    HWND w = k_make_window("win32kt_dc_rel");
    if (!w) { t_skip("no window"); return; }
    HDC a = U(GetDC)(w);
    CHECK(a != NULL);
    if (a) {
        EXPECT_EQ(U(GetObjectType)(a), OBJ_DC);
        EXPECT_EQ(U(ReleaseDC)(w, a), 1);
        EXPECT_EQ(U(ReleaseDC)(w, a), 0);       /* already released */
    }
    HDC b = U(GetDC)(w);
    CHECK(b != NULL);
    /* cache DCs are recycled: the same handle value comes back for an unchanged window */
    t_note("first GetDC %p, second %p", a, b);
    if (b) U(ReleaseDC)(w, b);
    U(DestroyWindow)(w);
}

TEST(gdi_dc_objects_cancel_dc_clears_dirty_bit_in_page) {
    FX(f);
    void *a = dc_attr(f.mem);
    if (!a) { dcf_close(&f); t_skip("DC_ATTR unreadable"); return; }
    /* gdi32full!CancelDC does not call the kernel for DC-typed handles: it clears ulDirty bit 0x800 in the page. */
    uint32_t before = da_u32(a, DA_DIRTY);
    SIZE_T wr;
    uint32_t forced = before | 0x800;
    BOOL ok = WriteProcessMemory(GetCurrentProcess(), (char *)a + DA_DIRTY, &forced, 4, &wr);
    if (!ok) { dcf_close(&f); t_skip("DC_ATTR page not writable"); return; }
    CHECK(U(CancelDC)(f.mem));
    CHECK((da_u32(a, DA_DIRTY) & 0x800) == 0);
    dcf_close(&f);
}

TEST(gdi_dc_objects_cancel_dc_invalid_handles) {
    REQUIRE(U(CancelDC));
    HDC dead = U(CreateCompatibleDC)(NULL);
    U(DeleteDC)(dead);
    SET_ERR(0);
    CHECK(!U(CancelDC)(NULL));
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    CHECK(!U(CancelDC)(dead));
}

TEST(gdi_dc_objects_syscall_cancel_dc_invalid) {
    DCWANT(UN(NtGdiCancelDC));
    HDC dead = U(CreateCompatibleDC)(NULL);
    U(DeleteDC)(dead);
    /* CancelDC: handle validation fails -> 0. (A live DC would get a cancel flag on its surface, so it
     * is not exercised here.) */
    CHECK(!UN(NtGdiCancelDC)(NULL));
    CHECK(!UN(NtGdiCancelDC)(dead));
    CHECK(!UN(NtGdiCancelDC)((HDC)U(GetStockObject)(BLACK_PEN)));
}

TEST(gdi_dc_objects_resetdc_invalid) {
    REQUIRE(U(ResetDCW));
    SET_ERR(0);
    CHECK(U(ResetDCW)(NULL, NULL) == NULL);
    HDC dead = U(CreateCompatibleDC)(NULL);
    U(DeleteDC)(dead);
    CHECK(U(ResetDCW)(dead, NULL) == NULL);
}

TEST(gdi_dc_objects_resetdc_on_memory_dc_note) {
    FX(f);
    HDC r = U(ResetDCW)(f.mem, NULL);
    t_note("ResetDCW(memdc, NULL) = %p (dc %p) error %lu", r, f.mem, GetLastError());
    /* ResetDC rejects memory DCs (type != direct) with ERROR_INVALID_HANDLE; whatever the result, the DC survives. */
    EXPECT_EQ(U(GetObjectType)(f.mem), OBJ_MEMDC);
    dcf_close(&f);
}

/* ---------------- layout ---------------- */

TEST(gdi_dc_objects_layout_default_and_roundtrip) {
    FX(f);
    EXPECT_EQ(U(GetLayout)(f.mem), 0);
    EXPECT_EQ(U(SetLayout)(f.mem, LAYOUT_RTL), 0);          /* returns the previous layout */
    EXPECT_EQ(U(GetLayout)(f.mem), LAYOUT_RTL);
    void *a = dc_attr(f.mem);
    if (a) EXPECT_EQ(da_u32(a, DA_LAYOUT) & 7, LAYOUT_RTL);  /* GetLayout reads the page the kernel updated */
    EXPECT_EQ(U(SetLayout)(f.mem, LAYOUT_RTL), LAYOUT_RTL);
    EXPECT_EQ(U(SetLayout)(f.mem, 0), LAYOUT_RTL);
    EXPECT_EQ(U(GetLayout)(f.mem), 0);
    dcf_close(&f);
}

TEST(gdi_dc_objects_layout_bitmap_orientation_preserved) {
    FX(f);
    EXPECT_EQ(U(SetLayout)(f.mem, LAYOUT_BITMAPORIENTATIONPRESERVED), 0);
    EXPECT_EQ(U(GetLayout)(f.mem), LAYOUT_BITMAPORIENTATIONPRESERVED);
    EXPECT_EQ(U(SetLayout)(f.mem, LAYOUT_RTL | LAYOUT_BITMAPORIENTATIONPRESERVED), LAYOUT_BITMAPORIENTATIONPRESERVED);
    EXPECT_EQ(U(GetLayout)(f.mem), LAYOUT_RTL | LAYOUT_BITMAPORIENTATIONPRESERVED);
    U(SetLayout)(f.mem, 0);
    dcf_close(&f);
}

TEST(gdi_dc_objects_layout_invalid_dc) {
    REQUIRE(U(SetLayout));
    HDC dead = U(CreateCompatibleDC)(NULL);
    U(DeleteDC)(dead);
    SET_ERR(0);
    EXPECT_EQ(U(SetLayout)(NULL, LAYOUT_RTL), GDI_ERROR);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    SET_ERR(0);
    EXPECT_EQ(U(SetLayout)(dead, LAYOUT_RTL), GDI_ERROR);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SET_ERR(0);
    U(GetLayout)(dead);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}

TEST(gdi_dc_objects_layout_metafile_dc) {
    REQUIRE(U(CreateMetaFileW));
    HDC m = U(CreateMetaFileW)(NULL);
    if (!m) { t_skip("no metafile"); return; }
    EXPECT_EQ(U(GetLayout)(m), GDI_ERROR);          /* GetLayout answers 0xffffffff for type-0x66 handles */
    HMETAFILE h = U(CloseMetaFile)(m);
    if (h) U(DeleteMetaFile)(h);
}

TEST(gdi_dc_objects_syscall_setlayout) {
    DCWANT(UN(NtGdiSetLayout));
    FX(f);
    /* gdi32full!SetLayout calls NtGdiSetLayout(hdc, -1, flags); the result is the previous layout. */
    EXPECT_EQ(UN(NtGdiSetLayout)(f.mem, -1, LAYOUT_RTL), 0);
    EXPECT_EQ(U(GetLayout)(f.mem), LAYOUT_RTL);
    EXPECT_EQ(UN(NtGdiSetLayout)(f.mem, -1, 0), LAYOUT_RTL);
    EXPECT_EQ(U(GetLayout)(f.mem), 0);
    dcf_close(&f);
}

TEST(gdi_dc_objects_syscall_setlayout_invalid_dc) {
    DCWANT(UN(NtGdiSetLayout));
    HDC dead = U(CreateCompatibleDC)(NULL);
    U(DeleteDC)(dead);
    EXPECT_EQ(UN(NtGdiSetLayout)(NULL, -1, LAYOUT_RTL), GDI_ERROR);
    EXPECT_EQ(UN(NtGdiSetLayout)(dead, -1, LAYOUT_RTL), GDI_ERROR);
}

TEST(gdi_dc_objects_layout_survives_save_restore) {
    FX(f);
    U(SetLayout)(f.mem, LAYOUT_RTL);
    U(SaveDC)(f.mem);
    U(SetLayout)(f.mem, 0);
    EXPECT_EQ(U(GetLayout)(f.mem), 0);
    CHECK(U(RestoreDC)(f.mem, -1));
    EXPECT_EQ(U(GetLayout)(f.mem), LAYOUT_RTL);
    U(SetLayout)(f.mem, 0);
    dcf_close(&f);
}

TEST(gdi_dc_objects_layout_of_compatible_dc_of_rtl_dc_note) {
    FX(f);
    HDC rtl = U(CreateDCW)(L"DISPLAY", NULL, NULL, NULL);
    if (!rtl) { dcf_close(&f); t_skip("CreateDCW failed"); return; }
    U(SetLayout)(rtl, LAYOUT_RTL);
    HDC c = U(CreateCompatibleDC)(rtl);
    if (c) { t_note("layout of CreateCompatibleDC(rtl dc) = 0x%lx", (unsigned long)U(GetLayout)(c)); U(DeleteDC)(c); }
    U(DeleteDC)(rtl);
    dcf_close(&f);
}

/* ---------------- graphics mode / world transform ---------------- */

TEST(gdi_dc_objects_graphics_mode_default_and_switch) {
    FX(f);
    EXPECT_EQ(U(GetGraphicsMode)(f.mem), GM_COMPATIBLE);
    EXPECT_EQ(U(SetGraphicsMode)(f.mem, GM_ADVANCED), GM_COMPATIBLE);   /* returns the previous mode */
    EXPECT_EQ(U(GetGraphicsMode)(f.mem), GM_ADVANCED);
    void *a = dc_attr(f.mem);
    if (a) EXPECT_EQ(da_u32(a, DA_GFXMODE), GM_ADVANCED);
    EXPECT_EQ(U(SetGraphicsMode)(f.mem, GM_ADVANCED), GM_ADVANCED);
    EXPECT_EQ(U(SetGraphicsMode)(f.mem, GM_COMPATIBLE), GM_ADVANCED);
    EXPECT_EQ(U(GetGraphicsMode)(f.mem), GM_COMPATIBLE);
    dcf_close(&f);
}

TEST(gdi_dc_objects_graphics_mode_invalid_values) {
    FX(f);
    static const int bad[] = { 0, 3, 4, 0x100, -1, (int)0x80000000 };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        SET_ERR(0);
        /* SetGraphicsMode accepts only 1 and 2 (iMode - 1U < 2); otherwise ERROR_INVALID_PARAMETER and 0. */
        CHECKF(U(SetGraphicsMode)(f.mem, bad[i]) == 0, "mode %d", bad[i]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
        EXPECT_EQ(U(GetGraphicsMode)(f.mem), GM_COMPATIBLE);
    }
    dcf_close(&f);
}

TEST(gdi_dc_objects_graphics_mode_invalid_dc) {
    HDC dead = U(CreateCompatibleDC)(NULL);
    U(DeleteDC)(dead);
    SET_ERR(0);
    EXPECT_EQ(U(SetGraphicsMode)(dead, GM_ADVANCED), 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SET_ERR(0);
    EXPECT_EQ(U(SetGraphicsMode)(NULL, GM_ADVANCED), 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}

TEST(gdi_dc_objects_world_transform_requires_advanced_mode) {
    FX(f);
    XFORM x = { 2.0f, 0.0f, 0.0f, 3.0f, 10.0f, 20.0f };
    CHECK(!U(SetWorldTransform)(f.mem, &x));                 /* GM_COMPATIBLE: refused */
    CHECK(U(SetGraphicsMode)(f.mem, GM_ADVANCED) == GM_COMPATIBLE);
    CHECK(U(SetWorldTransform)(f.mem, &x));
    XFORM y; memset(&y, 0, sizeof y);
    CHECK(U(GetWorldTransform)(f.mem, &y));
    EXPECT_EQ(y.eM11 == 2.0f && y.eM12 == 0.0f && y.eM21 == 0.0f && y.eM22 == 3.0f && y.eDx == 10.0f && y.eDy == 20.0f, 1);
    /* the world transform applies to LPtoDP */
    POINT p = { 5, 5 };
    CHECK(U(LPtoDP)(f.mem, &p, 1));
    EXPECT_EQ(p.x, 20); EXPECT_EQ(p.y, 35);
    CHECK(U(ModifyWorldTransform)(f.mem, NULL, MWT_IDENTITY));
    memset(&y, 0xcc, sizeof y);
    CHECK(U(GetWorldTransform)(f.mem, &y));
    EXPECT_EQ(y.eM11 == 1.0f && y.eM22 == 1.0f && y.eDx == 0.0f && y.eDy == 0.0f, 1);
    dcf_close(&f);
}

TEST(gdi_dc_objects_world_transform_survives_save_restore) {
    FX(f);
    U(SetGraphicsMode)(f.mem, GM_ADVANCED);
    XFORM x = { 1.0f, 0.0f, 0.0f, 1.0f, 4.0f, 6.0f }, z = { 1.0f, 0.0f, 0.0f, 1.0f, 40.0f, 60.0f };
    U(SetWorldTransform)(f.mem, &x);
    U(SaveDC)(f.mem);
    U(SetWorldTransform)(f.mem, &z);
    CHECK(U(RestoreDC)(f.mem, -1));
    XFORM y; memset(&y, 0, sizeof y);
    CHECK(U(GetWorldTransform)(f.mem, &y));
    EXPECT_EQ(y.eDx == 4.0f && y.eDy == 6.0f, 1);
    EXPECT_EQ(U(GetGraphicsMode)(f.mem), GM_ADVANCED);
    dcf_close(&f);
}

TEST(gdi_dc_objects_graphics_mode_reset_to_compatible_note) {
    FX(f);
    U(SetGraphicsMode)(f.mem, GM_ADVANCED);
    XFORM x = { 1.0f, 0.0f, 0.0f, 1.0f, 4.0f, 6.0f };
    U(SetWorldTransform)(f.mem, &x);
    U(SetGraphicsMode)(f.mem, GM_COMPATIBLE);
    XFORM y; memset(&y, 0, sizeof y);
    BOOL r = U(GetWorldTransform)(f.mem, &y);
    t_note("GetWorldTransform after returning to GM_COMPATIBLE: %d dx=%g dy=%g", r, y.eDx, y.eDy);
    dcf_close(&f);
}

/* ---------------- GdiFlush ---------------- */

TEST(gdi_dc_objects_gdiflush_empties_teb_batch) {
    FX(f);
    HBITMAP bm = U(CreateCompatibleBitmap)(f.scr, 8, 8);
    HGDIOBJ def = U(SelectObject)(f.mem, bm);
    unsigned long off = 0xdeadbeef;
    U(PatBlt)(f.mem, 0, 0, 4, 4, BLACKNESS);
    k_try_read_u32((char *)k_teb() + TEB_GdiTebBatch, &off);
    t_note("TEB batch offset after PatBlt: 0x%lx", off);
    CHECK(U(GdiFlush)());
    off = 0xdeadbeef;
    CHECK(k_try_read_u32((char *)k_teb() + TEB_GdiTebBatch, &off));
    EXPECT_EQ(off & 0x7fffffff, 0);
    U(SelectObject)(f.mem, def);
    U(DeleteObject)(bm);
    dcf_close(&f);
}

TEST(gdi_dc_objects_syscall_flush_empties_teb_batch) {
    DCWANT(UN(NtGdiFlush));
    FX(f);
    HBITMAP bm = U(CreateCompatibleBitmap)(f.scr, 8, 8);
    HGDIOBJ def = U(SelectObject)(f.mem, bm);
    U(PatBlt)(f.mem, 0, 0, 4, 4, WHITENESS);
    UN(NtGdiFlush)();
    unsigned long off = 0xdeadbeef;
    CHECK(k_try_read_u32((char *)k_teb() + TEB_GdiTebBatch, &off));
    EXPECT_EQ(off & 0x7fffffff, 0);
    U(SelectObject)(f.mem, def);
    U(DeleteObject)(bm);
    dcf_close(&f);
}

TEST(gdi_dc_objects_flush_with_empty_batch_is_true) {
    REQUIRE(U(GdiFlush));
    U(GdiFlush)();
    CHECK(U(GdiFlush)());
    CHECK(U(GdiFlush)());
}
