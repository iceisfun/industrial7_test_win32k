/* NtGdiEndDoc, NtGdiAbortDoc, NtGdiEndPage, NtGdiDoBanding, NtGdiGetPerBandInfo (print path on non-printer DCs),
 * NtGdiExtEscape, NtGdiGetUFIPathname, NtGdiCreateSessionMappedDIBSection, NtGdiMakeObjectXferable / UnXferable,
 * NtGdiAnyLinkedFonts, NtGdiGetETM, NtGdiGetMonitorID.
 *
 * No printer is involved. The print syscalls reject a plain DC (handle type 1, LO_DC_TYPE) before reaching the driver, so
 * they are exercised with invalid handles and with screen/memory DCs only. Valid-DC calls to EndDoc/AbortDoc are guarded by
 * a handle-type check because a CreateDC-style (type 0x21) display DC would pass bEndDocInternal's type test. */
#include "gap0.h"

static int g0_is_plain_dc(HDC dc) { return (((ULONG_PTR)dc >> 16) & 0x7f) == 0x01; }

/* ---- NtGdiEndDoc / NtGdiAbortDoc -> bEndDocInternal(hdc, fAbort, 1): every rejection is ERROR_CAN_NOT_COMPLETE (0x3eb) ---- */
TEST(gap0_enddoc_bad_handles_fail_with_1003) {
    REQUIRE(UN(NtGdiEndDoc));
    for (unsigned i = 0; i < G0_NBAD; i++) {
        SET_ERR(0);
        CHECKF(UN(NtGdiEndDoc)((HDC)g0_bad_handles[i]) == 0, "handle 0x%llx", (unsigned long long)g0_bad_handles[i]);
        EXPECT_ERR(G0_ERR_CANNOT_COMPLETE);
    }
}
TEST(gap0_abortdoc_bad_handles_fail_with_1003) {
    REQUIRE(UN(NtGdiAbortDoc));
    for (unsigned i = 0; i < G0_NBAD; i++) {
        SET_ERR(0);
        CHECKF(UN(NtGdiAbortDoc)((HDC)g0_bad_handles[i]) == 0, "handle 0x%llx", (unsigned long long)g0_bad_handles[i]);
        EXPECT_ERR(G0_ERR_CANNOT_COMPLETE);
    }
}
TEST(gap0_enddoc_abortdoc_stale_dc_fail_with_1003) {
    REQUIRE(UN(NtGdiEndDoc)); REQUIRE(UN(NtGdiAbortDoc)); REQUIRE(U(CreateCompatibleDC));
    HDC d = g0_stale_dc();
    REQUIRE(d);
    SET_ERR(0); EXPECT_EQ(UN(NtGdiEndDoc)(d), 0);   EXPECT_ERR(G0_ERR_CANNOT_COMPLETE);
    SET_ERR(0); EXPECT_EQ(UN(NtGdiAbortDoc)(d), 0); EXPECT_ERR(G0_ERR_CANNOT_COMPLETE);
}
TEST(gap0_enddoc_abortdoc_memory_dc_rejected_and_dc_stays_usable) {
    REQUIRE(UN(NtGdiEndDoc)); REQUIRE(UN(NtGdiAbortDoc));
    G0FX(f, 4, 4);
    if (!g0_is_plain_dc(f.dc)) { t_note("memory DC handle 0x%llx is not LO_DC_TYPE; skipped", (unsigned long long)(ULONG_PTR)f.dc); g0fx_close(&f); return; }
    SET_ERR(0); EXPECT_EQ(UN(NtGdiEndDoc)(f.dc), 0);   EXPECT_ERR(G0_ERR_CANNOT_COMPLETE);
    SET_ERR(0); EXPECT_EQ(UN(NtGdiAbortDoc)(f.dc), 0); EXPECT_ERR(G0_ERR_CANNOT_COMPLETE);
    CHECK(U(SetPixel)(f.dc, 1, 1, RGB(1, 2, 3)) != (COLORREF)-1);
    EXPECT_EQ(U(GetPixel)(f.dc, 1, 1) & 0xffffff, RGB(1, 2, 3));
    g0fx_close(&f);
}
TEST(gap0_enddoc_abortdoc_screen_dc_rejected) {
    REQUIRE(UN(NtGdiEndDoc)); REQUIRE(UN(NtGdiAbortDoc)); REQUIRE(U(GetDC));
    HDC dc = U(GetDC)(NULL);
    REQUIRE(dc);
    if (!g0_is_plain_dc(dc)) { t_note("screen DC handle 0x%llx is not LO_DC_TYPE; skipped", (unsigned long long)(ULONG_PTR)dc); U(ReleaseDC)(NULL, dc); return; }
    SET_ERR(0); EXPECT_EQ(UN(NtGdiEndDoc)(dc), 0);   EXPECT_ERR(G0_ERR_CANNOT_COMPLETE);
    SET_ERR(0); EXPECT_EQ(UN(NtGdiAbortDoc)(dc), 0); EXPECT_ERR(G0_ERR_CANNOT_COMPLETE);
    U(ReleaseDC)(NULL, dc);
}
TEST(gap0_enddoc_user_path_gdi32_enddoc_on_memory_dc) {
    /* gdi32!EndDoc / AbortDoc on a DC that never had StartDoc: fail, never crash */
    REQUIRE(U(EndDoc)); REQUIRE(U(AbortDoc));
    G0FX(f, 4, 4);
    SET_ERR(0);
    INT r1 = U(EndDoc)(f.dc), r2 = U(AbortDoc)(f.dc);
    CHECKF(r1 <= 0, "EndDoc on a memory DC returned %d", r1);
    CHECKF(r2 <= 0, "AbortDoc on a memory DC returned %d", r2);
    g0fx_close(&f);
}

/* ---- NtGdiEndPage(hdc): returns 0 for an invalid DC or any DC that is not a printer surface; sets no error ------------- */
TEST(gap0_endpage_bad_handles_zero) {
    REQUIRE(UN(NtGdiEndPage));
    for (unsigned i = 0; i < G0_NBAD; i++)
        CHECKF(UN(NtGdiEndPage)((HDC)g0_bad_handles[i]) == 0, "handle 0x%llx", (unsigned long long)g0_bad_handles[i]);
}
TEST(gap0_endpage_stale_dc_zero) {
    REQUIRE(UN(NtGdiEndPage)); REQUIRE(U(CreateCompatibleDC));
    HDC d = g0_stale_dc();
    REQUIRE(d);
    EXPECT_EQ(UN(NtGdiEndPage)(d), 0);
}
TEST(gap0_endpage_memory_dc_zero_and_dc_unchanged) {
    REQUIRE(UN(NtGdiEndPage));
    G0FX(f, 4, 4);
    if (!g0_is_plain_dc(f.dc)) { t_note("memory DC is not LO_DC_TYPE; skipped"); g0fx_close(&f); return; }
    EXPECT_EQ(UN(NtGdiEndPage)(f.dc), 0);
    CHECK(U(SetPixel)(f.dc, 0, 0, RGB(7, 8, 9)) != (COLORREF)-1);
    g0fx_close(&f);
}
TEST(gap0_endpage_screen_dc_zero) {
    REQUIRE(UN(NtGdiEndPage)); REQUIRE(U(GetDC));
    HDC dc = U(GetDC)(NULL);
    REQUIRE(dc);
    if (g0_is_plain_dc(dc)) EXPECT_EQ(UN(NtGdiEndPage)(dc), 0);
    else t_note("screen DC handle is not LO_DC_TYPE; skipped");
    U(ReleaseDC)(NULL, dc);
}

/* ---- NtGdiDoBanding(hdc, bStart, POINTL*, SIZE*) / NtGdiGetPerBandInfo(hdc, PERBANDINFO*) -------------------------------
 * the banding calls need a printer band surface (surface flag 0x2000000); everything else gives 0 /
 * 0xffffffff and writes nothing back to the caller's structures. */
TEST(gap0_doband_non_printer_dcs_zero_and_no_output) {
    REQUIRE(UN(NtGdiDoBanding)); REQUIRE(U(GetDC));
    G0FX(f, 4, 4);
    HDC scr = U(GetDC)(NULL);
    HDC dcs[2] = { f.dc, scr };
    for (int i = 0; i < 2; i++) {
        if (!dcs[i]) continue;
        for (int start = 0; start < 2; start++) {
            POINT pt; SIZE sz; memset(&pt, 0x5a, sizeof pt); memset(&sz, 0x5a, sizeof sz);
            CHECKF(UN(NtGdiDoBanding)(dcs[i], start, &pt, &sz) == 0, "dc %d bStart %d", i, start);
            CHECK(g0_all(&pt, sizeof pt, 0x5a)); CHECK(g0_all(&sz, sizeof sz, 0x5a));
        }
    }
    if (scr) U(ReleaseDC)(NULL, scr);
    g0fx_close(&f);
}
TEST(gap0_doband_bad_dc_zero) {
    REQUIRE(UN(NtGdiDoBanding));
    POINT pt; SIZE sz; memset(&pt, 0x5a, sizeof pt); memset(&sz, 0x5a, sizeof sz);
    for (unsigned i = 0; i < G0_NBAD; i++)
        CHECKF(UN(NtGdiDoBanding)((HDC)g0_bad_handles[i], 1, &pt, &sz) == 0 && UN(NtGdiDoBanding)((HDC)g0_bad_handles[i], 0, &pt, &sz) == 0, "handle 0x%llx", (unsigned long long)g0_bad_handles[i]);
    CHECK(g0_all(&pt, sizeof pt, 0x5a)); CHECK(g0_all(&sz, sizeof sz, 0x5a));
}
TEST(gap0_perbandinfo_non_printer_dcs_minus_one_buffer_untouched) {
    REQUIRE(UN(NtGdiGetPerBandInfo)); REQUIRE(U(GetDC));
    G0FX(f, 4, 4);
    HDC scr = U(GetDC)(NULL);
    HDC dcs[2] = { f.dc, scr };
    for (int i = 0; i < 2; i++) {
        if (!dcs[i]) continue;
        DWORD pbi[5]; memset(pbi, 0x5a, sizeof pbi);
        ULONG r = UN(NtGdiGetPerBandInfo)(dcs[i], pbi);
        CHECKF(r == 0xffffffff, "dc %d -> 0x%lx", i, (unsigned long)r);
        CHECK(g0_all(pbi, sizeof pbi, 0x5a));    /* only a result in 1..0xfffffffe is copied out */
    }
    if (scr) U(ReleaseDC)(NULL, scr);
    g0fx_close(&f);
}
TEST(gap0_perbandinfo_bad_dc_and_null_buffer) {
    REQUIRE(UN(NtGdiGetPerBandInfo));
    for (unsigned i = 0; i < G0_NBAD; i++) {
        DWORD pbi[5]; memset(pbi, 0x5a, sizeof pbi);
        CHECKF(UN(NtGdiGetPerBandInfo)((HDC)g0_bad_handles[i], pbi) == 0xffffffff, "handle 0x%llx", (unsigned long long)g0_bad_handles[i]);
        CHECK(g0_all(pbi, sizeof pbi, 0x5a));
        CHECKF(UN(NtGdiGetPerBandInfo)((HDC)g0_bad_handles[i], NULL) == 0xffffffff, "NULL buffer, handle 0x%llx", (unsigned long long)g0_bad_handles[i]);
    }
}

/* ---- NtGdiExtEscape(hdc, driver, cwcDriver, iEsc, cjIn, pjIn, cjOut, pjOut) ------------------------------------------------
 * The first test in the body returns 0xffffffff for a negative cjIn / cwcDriver / cjOut, or for hdc != 0 together with a
 * driver name, before any pointer is touched. */
TEST(gap0_extescape_negative_sizes_minus_one) {
    REQUIRE(UN(NtGdiExtEscape));
    G0FX(f, 2, 2);
    static const INT neg[] = { -1, -2, 0x80000000, -0x1000 };
    for (unsigned i = 0; i < sizeof neg / sizeof *neg; i++) {
        INT n = neg[i];
        CHECKF((INT)UN(NtGdiExtEscape)(f.dc, NULL, 0, 1, n, NULL, 0, NULL) == -1, "cjIn %d", n);
        CHECKF((INT)UN(NtGdiExtEscape)(f.dc, NULL, 0, 1, 0, NULL, n, NULL) == -1, "cjOut %d", n);
        CHECKF((INT)UN(NtGdiExtEscape)(f.dc, NULL, n, 1, 0, NULL, 0, NULL) == -1, "cwcDriver %d", n);
        CHECKF((INT)UN(NtGdiExtEscape)(NULL, NULL, 0, 1, n, NULL, 0, NULL) == -1, "NULL dc, cjIn %d", n);
        CHECKF((INT)UN(NtGdiExtEscape)(NULL, NULL, n, 1, 0, NULL, 0, NULL) == -1, "NULL dc, cwcDriver %d", n);
    }
    g0fx_close(&f);
}
TEST(gap0_extescape_dc_with_driver_name_minus_one) {
    REQUIRE(UN(NtGdiExtEscape));
    G0FX(f, 2, 2);
    static const WCHAR drv[] = L"GSESC";
    for (INT esc = 0; esc < 4; esc++) {
        CHECKF((INT)UN(NtGdiExtEscape)(f.dc, drv, 5, esc, 0, NULL, 0, NULL) == -1, "esc %d", esc);
        CHECKF((INT)UN(NtGdiExtEscape)(f.dc, drv, 0, esc, 0, NULL, 0, NULL) == -1, "cwc 0, esc %d", esc);
    }
    g0fx_close(&f);
}
TEST(gap0_extescape_unsupported_escape_on_memory_dc_note) {
    REQUIRE(UN(NtGdiExtEscape));
    G0FX(f, 2, 2);
    INT code = 4107;   /* arbitrary unsupported escape code */
    INT r = (INT)UN(NtGdiExtEscape)(f.dc, NULL, 0, 8 /*QUERYESCSUPPORT*/, sizeof code, &code, 0, NULL);
    t_note("ExtEscape(QUERYESCSUPPORT %d) on a memory DC -> %d", code, r);
    CHECKF(r <= 0, "memory DC claims to support escape %d: %d", code, r);
    g0fx_close(&f);
}
TEST(gap0_extescape_user_path_scaled_compatible_bitmap_note) {
    /* gdi32!CreateScaledCompatibleBitmap sends the GSESC escape through NtGdiExtEscape */
    HMODULE g = t_module("gdi32.dll");
    HBITMAP (WINAPI *CSCB)(HDC, int, int, int, int) = g ? (void *)GetProcAddress(g, "CreateScaledCompatibleBitmap") : NULL;
    REQUIRE(CSCB); REQUIRE(U(GetDC));
    HDC dc = U(GetDC)(NULL);
    REQUIRE(dc);
    HBITMAP bm = CSCB(dc, 16, 16, 100, 100);
    t_note("CreateScaledCompatibleBitmap -> %p", (void *)bm);
    if (bm) { BITMAP b; CHECK(U(GetObjectW)(bm, sizeof b, &b) == sizeof b); U(DeleteObject)(bm); }
    U(ReleaseDC)(NULL, dc);
}

/* ---- NtGdiGetUFIPathname: a UFI that matches no loaded font gives 0 and writes no output ------------------------------- */
TEST(gap0_ufipath_unknown_ufi_zero_no_output) {
    REQUIRE(UN(NtGdiGetUFIPathname));
    static const DWORD ufis[][2] = { { 0x5a5a5a5a, 0x5a5a5a5a }, { 0x13572468, 0 }, { 0xfedcba98, 0x1234 } };
    for (unsigned i = 0; i < 3; i++) {
        DWORD ufi[2] = { ufis[i][0], ufis[i][1] };
        ULONG cwc = 0x11111111, nfiles = 0x22222222, cjfile = 0x33333333, a1 = 0x44444444, a2 = 0x55555555;
        INT memfont = 0x66666666;
        WCHAR path[64]; memset(path, 0x5a, sizeof path);
        ULONG r = UN(NtGdiGetUFIPathname)(ufi, &cwc, path, &nfiles, 0, &memfont, &cjfile, NULL, &a1, &a2);
        CHECKF(r == 0, "ufi %u: 0x%lx", i, (unsigned long)r);
        EXPECT_EQ(cwc, 0x11111111); EXPECT_EQ(nfiles, 0x22222222); EXPECT_EQ(cjfile, 0x33333333);
        EXPECT_EQ(a1, 0x44444444); EXPECT_EQ(a2, 0x55555555); EXPECT_EQ((ULONG)memfont, 0x66666666);
        CHECK(g0_all(path, sizeof path, 0x5a));
    }
}
TEST(gap0_ufipath_all_optional_pointers_null) {
    REQUIRE(UN(NtGdiGetUFIPathname));
    DWORD ufi[2] = { 0x5a5a5a5a, 0x0badf00d };
    EXPECT_EQ(UN(NtGdiGetUFIPathname)(ufi, NULL, NULL, NULL, 0, NULL, NULL, NULL, NULL, NULL), 0);
}

/* ---- NtGdiCreateSessionMappedDIBSection(hdc, hSection, off, pbmi, usage, cjHeader, fl, color) --------------------------- */
TEST(gap0_sessiondib_requires_section_and_bitmapinfo_87) {
    REQUIRE(UN(NtGdiCreateSessionMappedDIBSection));
    G0FX(f, 2, 2);
    BITMAPINFO bi; memset(&bi, 0, sizeof bi); bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = 2; bi.bmiHeader.biHeight = 2;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    SET_ERR(0);
    CHECK(UN(NtGdiCreateSessionMappedDIBSection)(f.dc, NULL, 0, &bi, DIB_RGB_COLORS, sizeof bi, 0, 0) == NULL);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);                         /* no section handle */
    SET_ERR(0);
    CHECK(UN(NtGdiCreateSessionMappedDIBSection)(f.dc, (HANDLE)4, 0, NULL, DIB_RGB_COLORS, sizeof bi, 0, 0) == NULL);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);                         /* no BITMAPINFO */
    SET_ERR(0);
    CHECK(UN(NtGdiCreateSessionMappedDIBSection)(f.dc, NULL, 0, NULL, 0, 0, 0, 0) == NULL);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    g0fx_close(&f);
}
TEST(gap0_sessiondib_only_winlogon_may_call_access_denied) {
    /* both parameters present: the caller must be the logon process (gpidLogon); an ordinary process gets ERROR_ACCESS_DENIED
     * before the section handle is even looked at */
    REQUIRE(UN(NtGdiCreateSessionMappedDIBSection));
    G0FX(f, 2, 2);
    BITMAPINFO bi; memset(&bi, 0, sizeof bi); bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = 2; bi.bmiHeader.biHeight = 2;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    static const ULONG_PTR sec[] = { 4, 8, 0x1234, 0xdeadbeef };
    HDC dcs[3] = { f.dc, NULL, (HDC)0x12345678 };
    for (unsigned d = 0; d < 3; d++)
        for (unsigned i = 0; i < sizeof sec / sizeof *sec; i++) {
            SET_ERR(0);
            HBITMAP r = UN(NtGdiCreateSessionMappedDIBSection)(dcs[d], (HANDLE)sec[i], 0, &bi, DIB_RGB_COLORS, sizeof bi, 0, 0);
            CHECKF(r == NULL, "dc %u section 0x%llx returned a bitmap", d, (unsigned long long)sec[i]);
            EXPECT_ERR(ERROR_ACCESS_DENIED);
        }
    g0fx_close(&f);
}
TEST(gap0_sessiondib_user_path_gdi32_export) {
    HMODULE g = t_module("gdi32.dll");
    /* gdi32!CreateSessionMappedDIBSection(hdc, pbmi, usage, hSection, offset): the BITMAPINFO is the second parameter and is
     * dereferenced by the caller side, so a section handle value in that slot faults. */
    HBITMAP (WINAPI *CSMD)(HDC, const BITMAPINFO *, UINT, HANDLE, DWORD) = g ? (void *)GetProcAddress(g, "CreateSessionMappedDIBSection") : NULL;
    REQUIRE(CSMD);
    G0FX(f, 2, 2);
    BITMAPINFO bi; memset(&bi, 0, sizeof bi); bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = 2; bi.bmiHeader.biHeight = 2;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    SET_ERR(0);
    HBITMAP r = CSMD(f.dc, &bi, DIB_RGB_COLORS, (HANDLE)4, 0);
    CHECK(r == NULL);                                            /* only the logon process may create session mapped DIBs */
    EXPECT_ERR(ERROR_ACCESS_DENIED);
    SET_ERR(0);
    r = CSMD(f.dc, NULL, DIB_RGB_COLORS, (HANDLE)4, 0);
    CHECK(r == NULL);
    g0fx_close(&f);
}

/* ---- NtGdiMakeObjectXferable(h, pid) / NtGdiMakeObjectUnXferable(h) -------------------------------------------------------
 * Both return 1 at once when handle bit 23 (stock object) is set; otherwise only a DC-type handle (type bits & 0x1f == 1)
 * goes on to mark the object transferable / untransferable, everything else is 0. UnXferable on an own DC clears cell flag 0x20;
 * Xferable towards the caller's own pid is refused by XFERDCOBJ::bPrepareDCForXfer. */
TEST(gap0_xfer_stock_bit_handles_succeed_for_any_value) {
    REQUIRE(UN(NtGdiMakeObjectXferable)); REQUIRE(UN(NtGdiMakeObjectUnXferable));
    static const ULONG_PTR hs[] = { 0x00800000, 0x00800001, 0x00810001, 0x00ffffff, 0x01800000, 0x12345678 | 0x00800000, 0xffffffff };
    for (unsigned i = 0; i < sizeof hs / sizeof *hs; i++) {
        CHECKF(UN(NtGdiMakeObjectXferable)((HGDIOBJ)hs[i], 0) == 1, "xferable 0x%llx", (unsigned long long)hs[i]);
        CHECKF(UN(NtGdiMakeObjectXferable)((HGDIOBJ)hs[i], 0x1234) == 1, "xferable pid 0x1234 0x%llx", (unsigned long long)hs[i]);
        CHECKF(UN(NtGdiMakeObjectUnXferable)((HGDIOBJ)hs[i]) == 1, "unxferable 0x%llx", (unsigned long long)hs[i]);
    }
}
TEST(gap0_xfer_real_stock_objects) {
    REQUIRE(UN(NtGdiMakeObjectXferable)); REQUIRE(UN(NtGdiMakeObjectUnXferable)); REQUIRE(U(GetStockObject));
    static const int stock[] = { WHITE_BRUSH, LTGRAY_BRUSH, GRAY_BRUSH, DKGRAY_BRUSH, BLACK_BRUSH, NULL_BRUSH, WHITE_PEN, BLACK_PEN, NULL_PEN, SYSTEM_FONT, DEFAULT_GUI_FONT, DEFAULT_PALETTE };
    for (unsigned i = 0; i < sizeof stock / sizeof *stock; i++) {
        HGDIOBJ o = U(GetStockObject)(stock[i]);
        CHECKF(o != NULL, "stock %d", stock[i]);
        if (!o) continue;
        if (!((ULONG_PTR)o & 0x800000)) { t_note("stock object %d handle %p does not carry bit 23", stock[i], (void *)o); continue; }
        EXPECT_EQ(UN(NtGdiMakeObjectXferable)(o, 0), 1);
        EXPECT_EQ(UN(NtGdiMakeObjectUnXferable)(o), 1);
    }
}
TEST(gap0_xfer_non_dc_handles_zero) {
    REQUIRE(UN(NtGdiMakeObjectXferable)); REQUIRE(UN(NtGdiMakeObjectUnXferable)); REQUIRE(U(CreateSolidBrush));
    G0FX(f, 2, 2);
    HGDIOBJ o[5] = { f.bmp, U(CreateSolidBrush)(RGB(1, 2, 3)), U(CreatePen)(PS_SOLID, 1, 0), U(CreateRectRgn)(0, 0, 2, 2), (HGDIOBJ)(ULONG_PTR)0x00020000 };
    for (int i = 0; i < 5; i++) {
        CHECKF(o[i] != NULL, "object %d", i);
        if (!o[i] || ((ULONG_PTR)o[i] & 0x800000)) continue;
        CHECKF(UN(NtGdiMakeObjectXferable)(o[i], 0x1234) == 0, "xferable object %d", i);
        CHECKF(UN(NtGdiMakeObjectUnXferable)(o[i]) == 0, "unxferable object %d", i);
    }
    for (int i = 1; i < 4; i++) if (o[i]) U(DeleteObject)(o[i]);
    g0fx_close(&f);
}
TEST(gap0_xfer_null_handle_zero) {
    REQUIRE(UN(NtGdiMakeObjectXferable)); REQUIRE(UN(NtGdiMakeObjectUnXferable));
    EXPECT_EQ(UN(NtGdiMakeObjectXferable)(NULL, 0x1234), 0);
    EXPECT_EQ(UN(NtGdiMakeObjectXferable)(NULL, 0), 0);
    EXPECT_EQ(UN(NtGdiMakeObjectUnXferable)(NULL), 0);
}
TEST(gap0_xfer_stale_dc_unxferable_zero) {
    REQUIRE(UN(NtGdiMakeObjectUnXferable)); REQUIRE(U(CreateCompatibleDC));
    HDC d = g0_stale_dc();
    REQUIRE(d);
    EXPECT_EQ(UN(NtGdiMakeObjectUnXferable)(d), 0);
}
TEST(gap0_xfer_own_dc_unxferable_clears_cell_flag) {
    REQUIRE(UN(NtGdiMakeObjectUnXferable));
    G0FX(f, 2, 2);
    int fl0 = g0_cell_u8(f.dc, CELL_FLAGS);
    EXPECT_EQ(UN(NtGdiMakeObjectUnXferable)(f.dc), 1);   /* cell type/unique match and owner == this pid */
    int fl1 = g0_cell_u8(f.dc, CELL_FLAGS);
    if (fl1 >= 0) CHECKF((fl1 & 0x20) == 0, "cell flags 0x%x (were 0x%x)", fl1, fl0);
    g0fx_close(&f);
}
TEST(gap0_xfer_own_pid_target_refused) {
    REQUIRE(UN(NtGdiMakeObjectXferable));
    G0FX(f, 2, 2);
    int fl0 = g0_cell_u8(f.dc, CELL_FLAGS);
    EXPECT_EQ(UN(NtGdiMakeObjectXferable)(f.dc, GetCurrentProcessId()), 0);
    int fl1 = g0_cell_u8(f.dc, CELL_FLAGS);
    if (fl0 >= 0) EXPECT_EQ(fl1, fl0);
    g0fx_close(&f);
}
TEST(gap0_xfer_bad_dc_with_foreign_pid_zero) {
    REQUIRE(UN(NtGdiMakeObjectXferable));
    /* DC-typed values (type bits & 0x1f == 1, bit 23 clear) reach XFERDCOBJ::bPrepareDCForXfer, whose DC lock fails */
    static const ULONG_PTR hs[] = { 0x00010fff, 0x00210fff, 0x00010001, 0x00110fff, 0x7f010123 & ~0x00800000ul };
    for (unsigned i = 0; i < sizeof hs / sizeof *hs; i++)
        CHECKF(UN(NtGdiMakeObjectXferable)((HGDIOBJ)hs[i], 0x1234) == 0, "handle 0x%llx", (unsigned long long)hs[i]);
}

/* ---- NtGdiAnyLinkedFonts() ---------------------------------------------------------------------------------------------------- */
TEST(gap0_linkedfonts_returns_boolean_and_is_stable) {
    REQUIRE(UN(NtGdiAnyLinkedFonts));
    INT a = UN(NtGdiAnyLinkedFonts)();
    CHECKF(a == 0 || a == 1, "returned %d (the body returns 0 or 1)", a);
    for (int i = 0; i < 20; i++) EXPECT_EQ(UN(NtGdiAnyLinkedFonts)(), a);
    t_note("AnyLinkedFonts = %d", a);
}
TEST(gap0_linkedfonts_gdi32_export_matches_syscall) {
    REQUIRE(UN(NtGdiAnyLinkedFonts)); REQUIRE(UN(AnyLinkedFonts));
    EXPECT_EQ((INT)(UN(AnyLinkedFonts)() != 0), UN(NtGdiAnyLinkedFonts)());   /* gdi32full!AnyLinkedFonts is a plain jump */
}

/* ---- NtGdiGetETM(hdc, EXTTEXTMETRIC*) -------------------------------------------------------------------------------------------- */
TEST(gap0_getetm_null_output_zero) {
    REQUIRE(UN(NtGdiGetETM));
    G0FX(f, 2, 2);
    EXPECT_EQ(UN(NtGdiGetETM)(f.dc, NULL), 0);
    EXPECT_EQ(UN(NtGdiGetETM)(NULL, NULL), 0);
    g0fx_close(&f);
}
TEST(gap0_getetm_bad_dc_zero_buffer_untouched) {
    REQUIRE(UN(NtGdiGetETM));
    for (unsigned i = 0; i < G0_NBAD; i++) {
        DWORD etm[16]; memset(etm, 0x5a, sizeof etm);
        CHECKF(UN(NtGdiGetETM)((HDC)g0_bad_handles[i], etm) == 0, "handle 0x%llx", (unsigned long long)g0_bad_handles[i]);
        CHECK(g0_all(etm, sizeof etm, 0x5a));
    }
}
static void etm_checks(const unsigned char *b) {
    unsigned short w[26]; memcpy(w, b, sizeof w);
    /* vIFIMetricsToETM */
    EXPECT_EQ(w[0], 0x34);                    /* etmSize */
    CHECKF(w[1] % 20 == 0, "etmPointSize %u is a multiple of 20 (twips)", w[1]);
    EXPECT_EQ(w[2], 0);                       /* etmOrientation */
    EXPECT_EQ(w[6], w[3]);                    /* etmMasterUnits == etmMasterHeight */
    EXPECT_EQ(w[5], 0x4000);                  /* etmMaxScale */
    EXPECT_EQ((short)w[0x12], (short)w[0x13] >> 1);   /* etmDoubleUpperUnderlineOffset == lower >> 1 */
    EXPECT_EQ(w[0x14], w[0x15]);              /* etmDoubleUpper/LowerUnderlineWidth */
    EXPECT_EQ((short)w[0x14], (short)w[0x11] >> 1);   /* each is etmUnderlineWidth >> 1 */
    EXPECT_EQ(w[0x19], 0);                    /* etmNKernTracks */
}
static HFONT mk_font(const WCHAR *face, int h) {
    LOGFONTW lf; memset(&lf, 0, sizeof lf);
    lf.lfHeight = h; lf.lfWeight = FW_NORMAL; lf.lfCharSet = DEFAULT_CHARSET; lf.lfQuality = DEFAULT_QUALITY;
    lstrcpyW(lf.lfFaceName, face);
    return U(CreateFontIndirectW)(&lf);
}
TEST(gap0_getetm_truetype_font_fields) {
    REQUIRE(UN(NtGdiGetETM)); REQUIRE(U(CreateFontIndirectW));
    G0FX(f, 8, 8);
    static const WCHAR *faces[] = { L"Arial", L"Times New Roman", L"Courier New", L"Segoe UI" };
    int got = 0;
    for (unsigned i = 0; i < 4; i++) {
        HFONT hf = mk_font(faces[i], -24);
        if (!hf) continue;
        HGDIOBJ old = U(SelectObject)(f.dc, hf);
        union { unsigned char b[0x40]; DWORD align; } etm; memset(&etm, 0x5a, sizeof etm);
        INT r = UN(NtGdiGetETM)(f.dc, etm.b);
        t_note("GetETM(%ls) -> %d", faces[i], r);
        if (r) {
            got++;
            EXPECT_EQ(r, 1);
            etm_checks(etm.b);
            CHECK(g0_all(etm.b + 0x34, 0xc, 0x5a));       /* nothing written beyond the 0x34-byte structure */
        } else {
            CHECK(g0_all(etm.b, 0x40, 0x5a));
        }
        U(SelectObject)(f.dc, old);
        U(DeleteObject)(hf);
    }
    if (!got) t_note("no TrueType face produced an ETM");
    g0fx_close(&f);
}
TEST(gap0_getetm_point_size_scales_with_height) {
    REQUIRE(UN(NtGdiGetETM)); REQUIRE(U(CreateFontIndirectW));
    G0FX(f, 8, 8);
    unsigned short ps[2] = { 0, 0 }; int ok = 0;
    int hs[2] = { -16, -48 };
    for (int i = 0; i < 2; i++) {
        HFONT hf = mk_font(L"Arial", hs[i]);
        if (!hf) continue;
        HGDIOBJ old = U(SelectObject)(f.dc, hf);
        union { unsigned char b[0x40]; DWORD align; } etm; memset(&etm, 0, sizeof etm);
        if (UN(NtGdiGetETM)(f.dc, etm.b)) { memcpy(&ps[i], etm.b + 2, 2); ok++; }
        U(SelectObject)(f.dc, old); U(DeleteObject)(hf);
    }
    if (ok == 2) { CHECKF(ps[1] > ps[0], "point size for 48 px (%u) not larger than for 16 px (%u)", ps[1], ps[0]); t_note("etmPointSize %u / %u twips", ps[0], ps[1]); }
    else t_skip("Arial did not produce an ETM here");
    g0fx_close(&f);
}
TEST(gap0_getetm_user_path_adds_kerning_pair_count) {
    /* gdi32full!GetETM calls the syscall and then stores GetKerningPairsA(hdc, 0, NULL) */
    REQUIRE(UN(GetETM)); REQUIRE(U(CreateFontIndirectW)); REQUIRE(U(GetKerningPairsA));
    G0FX(f, 8, 8);
    HFONT hf = mk_font(L"Arial", -24);
    REQUIRE(hf);
    HGDIOBJ old = U(SelectObject)(f.dc, hf);
    union { unsigned char b[0x40]; DWORD align; } etm; memset(&etm, 0, sizeof etm);
    INT r = UN(GetETM)(f.dc, etm.b);
    if (r) {
        unsigned short n; memcpy(&n, etm.b + 0x30, 2);
        EXPECT_EQ(n, (unsigned short)U(GetKerningPairsA)(f.dc, 0, NULL));
        etm_checks(etm.b);
    } else t_note("gdi32!GetETM returned 0 for Arial");
    U(SelectObject)(f.dc, old); U(DeleteObject)(hf);
    g0fx_close(&f);
}

/* ---- NtGdiGetMonitorID(hdc, cbBuffer, buffer): copies the monitor DeviceID (a NUL-terminated WCHAR string) only if cb fits it --- */
TEST(gap0_monitorid_bad_dc_zero_buffer_untouched) {
    REQUIRE(UN(NtGdiGetMonitorID));
    for (unsigned i = 0; i < G0_NBAD; i++) {
        WCHAR buf[160]; memset(buf, 0x5a, sizeof buf);
        CHECKF(UN(NtGdiGetMonitorID)((HDC)g0_bad_handles[i], sizeof buf, buf) == 0, "handle 0x%llx", (unsigned long long)g0_bad_handles[i]);
        CHECK(g0_all(buf, sizeof buf, 0x5a));
    }
}
TEST(gap0_monitorid_zero_or_one_byte_buffer_fails) {
    REQUIRE(UN(NtGdiGetMonitorID)); REQUIRE(U(GetDC));
    HDC dc = U(GetDC)(NULL);
    REQUIRE(dc);
    WCHAR buf[160]; memset(buf, 0x5a, sizeof buf);
    EXPECT_EQ(UN(NtGdiGetMonitorID)(dc, 0, buf), 0);      /* the shortest possible string needs 2 bytes (the NUL) */
    EXPECT_EQ(UN(NtGdiGetMonitorID)(dc, 1, buf), 0);
    CHECK(g0_all(buf, sizeof buf, 0x5a));
    U(ReleaseDC)(NULL, dc);
}
TEST(gap0_monitorid_screen_dc_string_exact_size_protocol) {
    REQUIRE(UN(NtGdiGetMonitorID)); REQUIRE(U(GetDC));
    HDC dc = U(GetDC)(NULL);
    REQUIRE(dc);
    WCHAR buf[300]; memset(buf, 0x5a, sizeof buf);
    ULONG_PTR r = UN(NtGdiGetMonitorID)(dc, sizeof buf, buf);
    if (!r) { t_note("no monitor device id for the screen DC here"); CHECK(g0_all(buf, sizeof buf, 0x5a)); U(ReleaseDC)(NULL, dc); return; }
    size_t n = 0; while (n < 300 && buf[n]) n++;
    CHECKF(n < 128, "DeviceID length %zu (DISPLAY_DEVICE.DeviceID is 128 WCHARs)", n);
    t_note("monitor id: \"%ls\"", buf);
    size_t bytes = (n + 1) * 2;
    CHECK(g0_all((unsigned char *)buf + bytes, sizeof buf - bytes, 0x5a));   /* copies exactly the string and its NUL */
    /* the buffer must hold the string plus its terminator: one byte less fails and writes nothing */
    WCHAR b2[300]; memset(b2, 0x5a, sizeof b2);
    EXPECT_EQ(UN(NtGdiGetMonitorID)(dc, (ULONG)bytes - 1, b2), 0);
    CHECK(g0_all(b2, sizeof b2, 0x5a));
    memset(b2, 0x5a, sizeof b2);
    CHECK(UN(NtGdiGetMonitorID)(dc, (ULONG)bytes, b2) != 0);
    CHECK(memcmp(b2, buf, bytes) == 0);
    U(ReleaseDC)(NULL, dc);
}
TEST(gap0_monitorid_memory_dc_note) {
    REQUIRE(UN(NtGdiGetMonitorID));
    G0FX(f, 4, 4);
    WCHAR buf[300]; memset(buf, 0x5a, sizeof buf);
    ULONG_PTR r = UN(NtGdiGetMonitorID)(f.dc, sizeof buf, buf);
    t_note("memory DC monitor id -> %llu \"%ls\"", (unsigned long long)r, r ? buf : L"");
    g0fx_close(&f);
}
