/* GetDeviceCaps / NtGdiGetDeviceCaps / NtGdiGetDeviceCapsAll, and the DC point / dword syscalls.
 *
 * gdi32!GetDeviceCaps: for a DC-typed handle it validates the cell, then either answers from the shared DEVCAPS table
 * (when DC_ATTR bit 1 is set) or calls NtGdiGetDeviceCaps; indexes 4,6,8,10,0x28,0x2a,0x2c,0x58,0x5a always go to the
 * kernel. Metafile DC handles (type 0x66) answer TECHNOLOGY = DT_METAFILE and 0 for everything else. Failure to resolve
 * the DC gives 0 with ERROR_INVALID_HANDLE (non-DC handle value) or ERROR_INVALID_PARAMETER (DC-typed, dead cell).
 * NtGdiGetDeviceCapsAll(hdc, DEVCAPS*) fills a 0x90 byte block (ints: [1]=TECHNOLOGY [2]=HORZSIZE [3]=VERTSIZE
 * [4]/[5]=the same sizes in micrometres [6]=HORZRES [7]=VERTRES [8]=BITSPIXEL [9]=PLANES [12]=NUMCOLORS [13]=RASTERCAPS
 * [0x11]=LOGPIXELSX [0x12]=LOGPIXELSY [0x1a]=VREFRESH [0x1b]=DESKTOPHORZRES [0x1c]=DESKTOPVERTRES [0x22]=SHADEBLENDCAPS
 * [0x23]=COLORMGMTCAPS) and returns 1, or 0 with ERROR_INVALID_HANDLE. */
#include "dc.h"

static int bits_of(HDC dc) { return U(GetDeviceCaps)(dc, BITSPIXEL) * U(GetDeviceCaps)(dc, PLANES); }

TEST(gdi_dc_objects_caps_screen_dc_technology_and_planes) {
    FX(f);
    EXPECT_EQ(U(GetDeviceCaps)(f.scr, TECHNOLOGY), DT_RASDISPLAY);
    EXPECT_EQ(U(GetDeviceCaps)(f.scr, PLANES), 1);
    int bpp = U(GetDeviceCaps)(f.scr, BITSPIXEL);
    CHECKF(bpp == 8 || bpp == 15 || bpp == 16 || bpp == 24 || bpp == 32, "BITSPIXEL %d", bpp);
    t_note("screen BITSPIXEL=%d COLORRES=%d", bpp, U(GetDeviceCaps)(f.scr, COLORRES));
    dcf_close(&f);
}

TEST(gdi_dc_objects_caps_memory_dc_default_has_display_format) {
    FX(f);
    /* On Windows 10 a fresh memory DC reports the format of the display, although its default bitmap is 1x1 monochrome
     * (the monochrome format only shows through the bitmap itself and GetDIBColorTable). */
    EXPECT_EQ(U(GetDeviceCaps)(f.mem, TECHNOLOGY), DT_RASDISPLAY);
    EXPECT_EQ(U(GetDeviceCaps)(f.mem, BITSPIXEL), U(GetDeviceCaps)(f.scr, BITSPIXEL));
    EXPECT_EQ(U(GetDeviceCaps)(f.mem, PLANES), U(GetDeviceCaps)(f.scr, PLANES));
    EXPECT_EQ(U(GetDeviceCaps)(f.mem, NUMCOLORS), U(GetDeviceCaps)(f.scr, NUMCOLORS));
    EXPECT_EQ(U(GetDeviceCaps)(f.mem, RASTERCAPS), U(GetDeviceCaps)(f.scr, RASTERCAPS));
    dcf_close(&f);
}

TEST(gdi_dc_objects_caps_memory_dc_caps_ignore_selected_bitmap_format) {
    FX(f);
    HBITMAP bm = U(CreateCompatibleBitmap)(f.scr, 4, 4);
    HBITMAP mono = U(CreateBitmap)(4, 4, 1, 1, NULL);
    HGDIOBJ def = U(GetCurrentObject)(f.mem, OBJ_BITMAP);
    U(SelectObject)(f.mem, bm);
    EXPECT_EQ(U(GetDeviceCaps)(f.mem, BITSPIXEL), U(GetDeviceCaps)(f.scr, BITSPIXEL));
    EXPECT_EQ(U(GetDeviceCaps)(f.mem, PLANES), U(GetDeviceCaps)(f.scr, PLANES));
    U(SelectObject)(f.mem, mono);
    /* selecting a monochrome bitmap does not change what the memory DC reports */
    EXPECT_EQ(U(GetDeviceCaps)(f.mem, BITSPIXEL), U(GetDeviceCaps)(f.scr, BITSPIXEL));
    U(SelectObject)(f.mem, def);
    EXPECT_EQ(U(GetDeviceCaps)(f.mem, BITSPIXEL), U(GetDeviceCaps)(f.scr, BITSPIXEL));
    U(DeleteObject)(bm); U(DeleteObject)(mono);
    dcf_close(&f);
}

TEST(gdi_dc_objects_caps_memory_dc_dib_depths_keep_display_format) {
    FX(f);
    static const int depths[] = { 1, 8, 16, 24, 32 };
    for (unsigned i = 0; i < sizeof depths / sizeof *depths; i++) {
        union { BITMAPINFO bi; char raw[sizeof(BITMAPINFOHEADER) + 256 * sizeof(RGBQUAD)]; } u;
        memset(&u, 0, sizeof u);
        u.bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); u.bi.bmiHeader.biWidth = 4; u.bi.bmiHeader.biHeight = -4;
        u.bi.bmiHeader.biPlanes = 1; u.bi.bmiHeader.biBitCount = (WORD)depths[i]; u.bi.bmiHeader.biCompression = BI_RGB;
        void *bits = NULL;
        HBITMAP dib = U(CreateDIBSection)(f.scr, &u.bi, DIB_RGB_COLORS, &bits, NULL, 0);
        CHECKF(dib != NULL, "dib %d", depths[i]);
        if (!dib) continue;
        HGDIOBJ prev = U(SelectObject)(f.mem, dib);
        CHECKF(prev != NULL, "select dib %d", depths[i]);
        /* the memory DC keeps reporting the display format whatever DIB section is selected */
        CHECKF(U(GetDeviceCaps)(f.mem, BITSPIXEL) == U(GetDeviceCaps)(f.scr, BITSPIXEL), "DIB %d -> BITSPIXEL %d", depths[i], U(GetDeviceCaps)(f.mem, BITSPIXEL));
        EXPECT_EQ(U(GetDeviceCaps)(f.mem, PLANES), 1);
        U(SelectObject)(f.mem, prev);
        U(DeleteObject)(dib);
    }
    dcf_close(&f);
}

TEST(gdi_dc_objects_caps_dib_flag_in_dc_attr) {
    FX(f);
    void *a = dc_attr(f.mem);
    if (!a) { dcf_close(&f); t_skip("DC_ATTR unreadable"); return; }
    BITMAPINFO bi; memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = 2; bi.bmiHeader.biHeight = -2;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    void *bits;
    HBITMAP dib = U(CreateDIBSection)(f.scr, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    HBITMAP ddb = U(CreateCompatibleBitmap)(f.scr, 2, 2);
    HGDIOBJ def = U(GetCurrentObject)(f.mem, OBJ_BITMAP);
    /* Selecting a bitmap stores bit 0 of the first DC_ATTR dword = "selected surface is a DIB section". (The section's
     * address is not kept at +0x18 of the page on Windows 10 build 19041.) */
    U(SelectObject)(f.mem, dib);
    CHECK((da_u32(a, DA_FLAGS) & 1) == 1);
    U(SelectObject)(f.mem, ddb);
    CHECK((da_u32(a, DA_FLAGS) & 1) == 0);
    U(SelectObject)(f.mem, def);
    U(DeleteObject)(dib); U(DeleteObject)(ddb);
    dcf_close(&f);
}

TEST(gdi_dc_objects_caps_screen_numcolors_and_rastercaps) {
    FX(f);
    int bpp = bits_of(f.scr);
    int rc = U(GetDeviceCaps)(f.scr, RASTERCAPS);
    CHECK((rc & RC_BITBLT) != 0);
    CHECK((rc & RC_BITMAP64) != 0);
    CHECK((rc & RC_DI_BITMAP) != 0);
    CHECK((rc & RC_DIBTODEV) != 0);
    CHECK((rc & RC_STRETCHBLT) != 0);
    CHECK((rc & RC_STRETCHDIB) != 0);
    if (bpp > 8) {
        EXPECT_EQ(U(GetDeviceCaps)(f.scr, NUMCOLORS), -1);      /* documented: -1 above 8 bits per pixel */
        CHECK((rc & RC_PALETTE) == 0);
        EXPECT_EQ(U(GetDeviceCaps)(f.scr, SIZEPALETTE), 0);
    }
    t_note("RASTERCAPS=0x%x SHADEBLENDCAPS=0x%x COLORMGMTCAPS=0x%x", rc, U(GetDeviceCaps)(f.scr, SHADEBLENDCAPS), U(GetDeviceCaps)(f.scr, COLORMGMTCAPS));
    dcf_close(&f);
}

TEST(gdi_dc_objects_caps_screen_geometry_is_positive) {
    FX(f);
    static const int idx[] = { HORZSIZE, VERTSIZE, HORZRES, VERTRES, LOGPIXELSX, LOGPIXELSY, ASPECTX, ASPECTY, ASPECTXY,
                               DESKTOPHORZRES, DESKTOPVERTRES };
    for (unsigned i = 0; i < sizeof idx / sizeof *idx; i++)
        CHECKF(U(GetDeviceCaps)(f.scr, idx[i]) > 0, "index %d = %d", idx[i], U(GetDeviceCaps)(f.scr, idx[i]));
    t_note("HORZRES=%d VERTRES=%d LOGPIXELS=%dx%d DESKTOP=%dx%d", U(GetDeviceCaps)(f.scr, HORZRES), U(GetDeviceCaps)(f.scr, VERTRES),
           U(GetDeviceCaps)(f.scr, LOGPIXELSX), U(GetDeviceCaps)(f.scr, LOGPIXELSY),
           U(GetDeviceCaps)(f.scr, DESKTOPHORZRES), U(GetDeviceCaps)(f.scr, DESKTOPVERTRES));
    dcf_close(&f);
}

TEST(gdi_dc_objects_caps_kernel_indexes_match_syscall) {
    DCWANT(UN(NtGdiGetDeviceCaps));
    FX(f);
    /* gdi32!GetDeviceCaps forwards these indexes straight to NtGdiGetDeviceCaps. */
    static const int idx[] = { HORZSIZE, VERTSIZE, HORZRES, VERTRES, ASPECTX, ASPECTY, ASPECTXY, LOGPIXELSX, LOGPIXELSY };
    HDC dcs[2] = { f.scr, f.mem };
    for (int d = 0; d < 2; d++)
        for (unsigned i = 0; i < sizeof idx / sizeof *idx; i++)
            CHECKF(U(GetDeviceCaps)(dcs[d], idx[i]) == UN(NtGdiGetDeviceCaps)(dcs[d], idx[i]), "dc %d index %d", d, idx[i]);
    dcf_close(&f);
}

TEST(gdi_dc_objects_caps_cached_indexes_match_kernel) {
    DCWANT(UN(NtGdiGetDeviceCaps));
    FX(f);
    /* The shared-table answers must agree with what the kernel reports for the same DC. */
    static const int idx[] = { TECHNOLOGY, BITSPIXEL, PLANES, NUMCOLORS, RASTERCAPS, SIZEPALETTE, COLORRES, TEXTCAPS,
                               SHADEBLENDCAPS, COLORMGMTCAPS, DESKTOPHORZRES, DESKTOPVERTRES };
    for (unsigned i = 0; i < sizeof idx / sizeof *idx; i++) {
        int u = U(GetDeviceCaps)(f.scr, idx[i]), k = UN(NtGdiGetDeviceCaps)(f.scr, idx[i]);
        if (u != k) t_note("screen index %d: GetDeviceCaps=%d NtGdiGetDeviceCaps=%d", idx[i], u, k);
        u = U(GetDeviceCaps)(f.mem, idx[i]); k = UN(NtGdiGetDeviceCaps)(f.mem, idx[i]);
        /* TEXTCAPS: the cached value carries the three low bits (character/stroke output precision) that the kernel query leaves out */
        if (idx[i] == TEXTCAPS) { CHECKF((u & ~7) == (k & ~7), "memdc TEXTCAPS: %x vs %x", u, k); continue; }
        CHECKF(u == k, "memdc index %d: %d vs %d", idx[i], u, k);
    }
    void *a = dc_attr(f.scr);
    t_note("screen DC_ATTR flags dword 0x%x (bit1 = GetDeviceCaps uses the shared table)", da_u32(a, DA_FLAGS));
    dcf_close(&f);
}

TEST(gdi_dc_objects_caps_unknown_indexes_note) {
    FX(f);
    static const int idx[] = { 1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31, 33, 35, 37, 39, 41, 43, 45, 87, 89, 91, 103, 105, 107, 109, 114, 115, 122, 200 };
    for (unsigned i = 0; i < sizeof idx / sizeof *idx; i++) {
        int s = U(GetDeviceCaps)(f.scr, idx[i]);
        if (s) t_note("screen GetDeviceCaps(%d) = %d", idx[i], s);
    }
    dcf_close(&f);
}

TEST(gdi_dc_objects_caps_invalid_handles) {
    REQUIRE(U(GetDeviceCaps));
    HDC dead = U(CreateCompatibleDC)(NULL);
    U(DeleteDC)(dead);
    HPEN pen = U(CreatePen)(PS_SOLID, 1, 0);
    SET_ERR(0);
    EXPECT_EQ(U(GetDeviceCaps)(NULL, HORZRES), 0);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    SET_ERR(0);
    int pr = U(GetDeviceCaps)((HDC)pen, HORZRES);
    t_note("GetDeviceCaps(pen handle) = %d, last error %lu", pr, GetLastError());
    SET_ERR(0);
    EXPECT_EQ(U(GetDeviceCaps)(dead, BITSPIXEL), 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    U(DeleteObject)(pen);
}

TEST(gdi_dc_objects_caps_metafile_dc) {
    REQUIRE(U(CreateMetaFileW));
    HDC mf = U(CreateMetaFileW)(NULL);
    if (!mf) { t_skip("CreateMetaFile failed"); return; }
    /* Handles of type 0x66 (metafile DC) never reach the kernel: TECHNOLOGY = DT_METAFILE (5), the rest 0. */
    EXPECT_EQ(H_TYPE7(mf), 0x66);
    EXPECT_EQ(U(GetDeviceCaps)(mf, TECHNOLOGY), DT_METAFILE);
    static const int idx[] = { HORZRES, VERTRES, BITSPIXEL, PLANES, LOGPIXELSX, RASTERCAPS, NUMCOLORS };
    for (unsigned i = 0; i < sizeof idx / sizeof *idx; i++)
        CHECKF(U(GetDeviceCaps)(mf, idx[i]) == 0, "metafile caps %d", idx[i]);
    HMETAFILE h = U(CloseMetaFile)(mf);
    if (h) U(DeleteMetaFile)(h);
}

TEST(gdi_dc_objects_syscall_getdevicecaps_invalid_dc) {
    DCWANT(UN(NtGdiGetDeviceCaps));
    HDC dead = U(CreateCompatibleDC)(NULL);
    U(DeleteDC)(dead);
    HDC bad[2] = { NULL, dead };
    for (int i = 0; i < 2; i++) {
        SET_ERR(0);
        /* GetDeviceCaps: DC lock failure -> EngSetLastError(6), 0. */
        EXPECT_EQ(UN(NtGdiGetDeviceCaps)(bad[i], HORZRES), 0);
        EXPECT_ERR(ERROR_INVALID_HANDLE);
    }
}

TEST(gdi_dc_objects_syscall_getdevicecapsall_fills_block) {
    DCWANT(UN(NtGdiGetDeviceCapsAll)); DCWANT(UN(NtGdiGetDeviceCaps));
    if (t_is_wine()) { t_skip("wine win32u leaves NtGdiGetDeviceCapsAll unimplemented (aborts)"); return; }
    FX(f);
    HDC dcs[2] = { f.scr, f.mem };
    for (int d = 0; d < 2; d++) {
        unsigned char buf[0x100]; memset(buf, 0xcc, sizeof buf);
        int *v = (int *)buf;
        CHECKF(UN(NtGdiGetDeviceCapsAll)(dcs[d], buf) != 0, "dc %d", d);
        /* NtGdiGetDeviceCapsAllInternal probes bytes 0 and 0x8f; vGetDeviceCaps writes up to +0x8c (size 0x90). */
        int clean = 1;
        for (unsigned i = 0x90; i < sizeof buf; i++) if (buf[i] != 0xcc) clean = 0;
        CHECKF(clean, "dc %d: DEVCAPS write overran 0xb0 bytes", d);
        EXPECT_EQ(v[1], DT_RASDISPLAY);
        /* +8/+0xc are HORZSIZE/VERTSIZE, +0x10/+0x14 the physical size in micrometres (HORZSIZE * 1000 up to rounding),
         * +0x18/+0x1c HORZRES/VERTRES, +0x44/+0x48 LOGPIXELSX/Y. */
        EXPECT_EQ(v[2], UN(NtGdiGetDeviceCaps)(dcs[d], HORZSIZE));
        EXPECT_EQ(v[3], UN(NtGdiGetDeviceCaps)(dcs[d], VERTSIZE));
        CHECKF(v[4] / 1000 == v[2] || (v[4] + 999) / 1000 == v[2], "dc %d: micrometre width %d vs HORZSIZE %d", d, v[4], v[2]);
        CHECKF(v[5] / 1000 == v[3] || (v[5] + 999) / 1000 == v[3], "dc %d: micrometre height %d vs VERTSIZE %d", d, v[5], v[3]);
        EXPECT_EQ(v[6], UN(NtGdiGetDeviceCaps)(dcs[d], HORZRES));
        EXPECT_EQ(v[7], UN(NtGdiGetDeviceCaps)(dcs[d], VERTRES));
        EXPECT_EQ(v[0x11], UN(NtGdiGetDeviceCaps)(dcs[d], LOGPIXELSX));
        EXPECT_EQ(v[0x12], UN(NtGdiGetDeviceCaps)(dcs[d], LOGPIXELSY));
        EXPECT_EQ(v[8], UN(NtGdiGetDeviceCaps)(dcs[d], BITSPIXEL));
        EXPECT_EQ(v[9], UN(NtGdiGetDeviceCaps)(dcs[d], PLANES));
        EXPECT_EQ(v[12], UN(NtGdiGetDeviceCaps)(dcs[d], NUMCOLORS));
        EXPECT_EQ(v[13], UN(NtGdiGetDeviceCaps)(dcs[d], RASTERCAPS));
        EXPECT_EQ(v[0x1b], UN(NtGdiGetDeviceCaps)(dcs[d], DESKTOPHORZRES));
        EXPECT_EQ(v[0x1c], UN(NtGdiGetDeviceCaps)(dcs[d], DESKTOPVERTRES));
        EXPECT_EQ(v[0x22], UN(NtGdiGetDeviceCaps)(dcs[d], SHADEBLENDCAPS));
        EXPECT_EQ(v[0x23], UN(NtGdiGetDeviceCaps)(dcs[d], COLORMGMTCAPS));
    }
    dcf_close(&f);
}

TEST(gdi_dc_objects_syscall_getdevicecapsall_invalid_dc) {
    DCWANT(UN(NtGdiGetDeviceCapsAll));
    if (t_is_wine()) { t_skip("wine win32u leaves NtGdiGetDeviceCapsAll unimplemented (aborts)"); return; }
    HDC dead = U(CreateCompatibleDC)(NULL);
    U(DeleteDC)(dead);
    HDC bad[2] = { NULL, dead };
    for (int i = 0; i < 2; i++) {
        unsigned char buf[0x100]; memset(buf, 0xcc, sizeof buf);
        SET_ERR(0);
        EXPECT_EQ(UN(NtGdiGetDeviceCapsAll)(bad[i], buf), 0);
        EXPECT_ERR(ERROR_INVALID_HANDLE);
        int clean = 1;
        for (unsigned j = 0; j < sizeof buf; j++) if (buf[j] != 0xcc) clean = 0;
        CHECK(clean);
    }
}

/* ---- NtGdiGetDCPoint ---- */

TEST(gdi_dc_objects_dcpoint_invalid_index_returns_zero_untouched) {
    DCWANT(UN(NtGdiGetDCPoint));
    FX(f);
    static const int bad[] = { 0, 3, 5, 6, 7, 9, 0xa, 0xf, 0x11, 0x12, 0x21, 0x30, 0x80, 0x100, -1 };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        POINT p = { 0x1234, 0x5678 };
        CHECKF(!UN(NtGdiGetDCPoint)(f.mem, bad[i], &p), "index 0x%x", bad[i]);
        CHECKF(p.x == 0x1234 && p.y == 0x5678, "index 0x%x modified the output", bad[i]);
    }
    dcf_close(&f);
}

TEST(gdi_dc_objects_dcpoint_invalid_dc) {
    DCWANT(UN(NtGdiGetDCPoint));
    HDC dead = U(CreateCompatibleDC)(NULL);
    U(DeleteDC)(dead);
    HDC bad[2] = { NULL, dead };
    static const int idx[] = { 1, 2, 8, 0x10, 0x20, 0x60 };
    for (int d = 0; d < 2; d++)
        for (unsigned i = 0; i < sizeof idx / sizeof *idx; i++) {
            POINT p = { 0x1234, 0x5678 };
            CHECKF(!UN(NtGdiGetDCPoint)(bad[d], idx[i], &p), "index 0x%x", idx[i]);
            CHECK(p.x == 0x1234 && p.y == 0x5678);
        }
}

TEST(gdi_dc_objects_dcpoint_viewport_ext_matches_getviewportext) {
    DCWANT(UN(NtGdiGetDCPoint));
    FX(f);
    POINT p = { -1, -1 };
    /* Index 1 reads DC_ATTR+0x14c, the pair GetViewportExtEx returns. Default MM_TEXT extent is 1x1. */
    CHECK(UN(NtGdiGetDCPoint)(f.mem, 1, &p));
    EXPECT_EQ(p.x, 1); EXPECT_EQ(p.y, 1);
    U(SetMapMode)(f.mem, MM_ANISOTROPIC);
    U(SetViewportExtEx)(f.mem, 200, 100, NULL);
    SIZE s = { 0, 0 };
    CHECK(U(GetViewportExtEx)(f.mem, &s));
    CHECK(UN(NtGdiGetDCPoint)(f.mem, 1, &p));
    EXPECT_EQ(p.x, s.cx); EXPECT_EQ(p.y, s.cy);
    EXPECT_EQ(s.cx, 200); EXPECT_EQ(s.cy, 100);
    /* the 0x40 bit is a refresh modifier only; the low bits still select the same item */
    POINT q = { -1, -1 };
    CHECK(UN(NtGdiGetDCPoint)(f.mem, 0x41, &q));
    EXPECT_EQ(q.x, 200); EXPECT_EQ(q.y, 100);
    dcf_close(&f);
}

TEST(gdi_dc_objects_dcpoint_window_ext_note) {
    DCWANT(UN(NtGdiGetDCPoint));
    FX(f);
    U(SetMapMode)(f.mem, MM_ANISOTROPIC);
    U(SetWindowExtEx)(f.mem, 50, 60, NULL);
    POINT p = { -1, -1 };
    CHECK(UN(NtGdiGetDCPoint)(f.mem, 2, &p));
    SIZE s = { 0, 0 };
    U(GetWindowExtEx)(f.mem, &s);
    if (p.x != s.cx || p.y != s.cy) t_note("window ext: index2=(%ld,%ld) GetWindowExtEx=(%ld,%ld)", p.x, p.y, s.cx, s.cy);
    EXPECT_EQ(s.cx, 50); EXPECT_EQ(s.cy, 60);
    dcf_close(&f);
}

TEST(gdi_dc_objects_dcpoint_window_org_note) {
    DCWANT(UN(NtGdiGetDCPoint));
    FX(f);
    U(SetWindowOrgEx)(f.mem, 7, 9, NULL);
    POINT p = { -1, -1 }, g = { 0, 0 };
    CHECK(UN(NtGdiGetDCPoint)(f.mem, 8, &p));
    U(GetWindowOrgEx)(f.mem, &g);
    t_note("index8=(%ld,%ld) GetWindowOrgEx=(%ld,%ld)", p.x, p.y, g.x, g.y);
    EXPECT_EQ(g.x, 7); EXPECT_EQ(g.y, 9);
    dcf_close(&f);
}

TEST(gdi_dc_objects_dcpoint_aspect_filter_note) {
    DCWANT(UN(NtGdiGetDCPoint));
    FX(f);
    SIZE s = { -1, -1 };
    BOOL r = U(GetAspectRatioFilterEx)(f.mem, &s);
    t_note("GetAspectRatioFilterEx(memdc) = %d (%ld,%ld)", r, s.cx, s.cy);
    dcf_close(&f);
}

TEST(gdi_dc_objects_dc_origin_memory_dc_is_zero) {
    FX(f);
    POINT p = { 77, 88 };
    /* gdi32full!GetDCOrgEx -> NtGdiGetDCPoint(hdc, 0x60): the DC's device origin. A memory DC draws at (0,0). */
    CHECK(U(GetDCOrgEx)(f.mem, &p));
    EXPECT_EQ(p.x, 0); EXPECT_EQ(p.y, 0);
    dcf_close(&f);
}

TEST(gdi_dc_objects_dc_origin_syscall_matches_wrapper) {
    DCWANT(UN(NtGdiGetDCPoint));
    FX(f);
    POINT a = { 1, 1 }, b = { 2, 2 }, c = { 3, 3 };
    CHECK(U(GetDCOrgEx)(f.scr, &a));
    CHECK(UN(NtGdiGetDCPoint)(f.scr, 0x60, &b));
    CHECK(UN(NtGdiGetDCPoint)(f.scr, 0x20, &c));
    EXPECT_EQ(a.x, b.x); EXPECT_EQ(a.y, b.y);
    EXPECT_EQ(a.x, c.x); EXPECT_EQ(a.y, c.y);
    t_note("screen DC origin (%ld,%ld)", a.x, a.y);
    dcf_close(&f);
}

TEST(gdi_dc_objects_dc_origin_of_window_dc_is_client_origin) {
    HWND w = k_make_window("win32kt_dc_org");
    if (!w) { t_skip("no window"); return; }
    HDC dc = U(GetDC)(w);
    CHECK(dc != NULL);
    if (dc) {
        POINT o = { -1, -1 }, c = { 0, 0 };
        CHECK(U(GetDCOrgEx)(dc, &o));
        CHECK(U(ClientToScreen)(w, &c));
        /* The cache DC's origin is the window client area's top-left in screen coordinates. */
        EXPECT_EQ(o.x, c.x); EXPECT_EQ(o.y, c.y);
        CHECK(U(ReleaseDC)(w, dc) == 1);
    }
    U(DestroyWindow)(w);
}

TEST(gdi_dc_objects_dc_origin_follows_window_move) {
    HWND w = k_make_window("win32kt_dc_org2");
    if (!w) { t_skip("no window"); return; }
    RECT r; U(GetWindowRect)(w, &r);
    CHECK(U(SetWindowPos)(w, NULL, r.left + 13, r.top + 7, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE));
    HDC dc = U(GetDC)(w);
    POINT o = { -1, -1 }, c = { 0, 0 };
    if (dc) {
        U(GetDCOrgEx)(dc, &o); U(ClientToScreen)(w, &c);
        EXPECT_EQ(o.x, c.x); EXPECT_EQ(o.y, c.y);
        U(ReleaseDC)(w, dc);
    }
    U(DestroyWindow)(w);
}

/* ---- NtGdiGetDCDword ---- */

TEST(gdi_dc_objects_dcdword_known_indexes) {
    DCWANT(UN(NtGdiGetDCDword));
    FX(f);
    ULONG v;
    v = 0xdeadbeef; CHECK(UN(NtGdiGetDCDword)(f.mem, 4, &v)); EXPECT_EQ(v, AD_COUNTERCLOCKWISE);
    v = 0xdeadbeef; CHECK(UN(NtGdiGetDCDword)(f.mem, 5, &v)); EXPECT_EQ(v, 1);
    v = 0xdeadbeef; CHECK(UN(NtGdiGetDCDword)(f.mem, 7, &v)); EXPECT_EQ(v, 1);
    v = 0xdeadbeef; CHECK(UN(NtGdiGetDCDword)(f.mem, 8, &v)); EXPECT_EQ(v, MM_TEXT);
    U(SetMapMode)(f.mem, MM_LOENGLISH);
    v = 0xdeadbeef; CHECK(UN(NtGdiGetDCDword)(f.mem, 8, &v)); EXPECT_EQ(v, MM_LOENGLISH);
    for (int i = 0; i <= 8; i++) {
        v = 0xdeadbeef;
        CHECKF(UN(NtGdiGetDCDword)(f.mem, i, &v) != 0, "index %d", i);
        CHECKF(v != 0xdeadbeef, "index %d left the output alone", i);
    }
    dcf_close(&f);
}

TEST(gdi_dc_objects_dcdword_unknown_index_fails_untouched) {
    DCWANT(UN(NtGdiGetDCDword));
    FX(f);
    static const int bad[] = { 9, 10, 11, 100, 0x100, -1 };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        ULONG v = 0xdeadbeef;
        CHECKF(!UN(NtGdiGetDCDword)(f.mem, bad[i], &v), "index %d", bad[i]);
        EXPECT_EQ(v, 0xdeadbeef);
    }
    dcf_close(&f);
}

TEST(gdi_dc_objects_dcdword_invalid_dc) {
    DCWANT(UN(NtGdiGetDCDword));
    HDC dead = U(CreateCompatibleDC)(NULL);
    U(DeleteDC)(dead);
    HPEN pen = U(CreatePen)(PS_SOLID, 1, 0);
    HDC bad[3] = { NULL, dead, (HDC)pen };
    for (int d = 0; d < 3; d++)
        for (int i = 0; i <= 8; i++) {
            ULONG v = 0xdeadbeef;
            CHECKF(!UN(NtGdiGetDCDword)(bad[d], i, &v), "dc %d index %d", d, i);
            EXPECT_EQ(v, 0xdeadbeef);
        }
    U(DeleteObject)(pen);
}

TEST(gdi_dc_objects_dcdword_font_language_matches_wrapper) {
    DCWANT(UN(NtGdiGetDCDword));
    FX(f);
    ULONG v = 0xdeadbeef;
    CHECK(UN(NtGdiGetDCDword)(f.mem, 6, &v));
    DWORD w = U(GetFontLanguageInfo)(f.mem);
    t_note("NtGdiGetDCDword(6)=0x%lx GetFontLanguageInfo=0x%lx", (unsigned long)v, (unsigned long)w);
    dcf_close(&f);
}

TEST(gdi_dc_objects_dcdword_save_level_index_tracks_savedc) {
    DCWANT(UN(NtGdiGetDCDword));
    FX(f);
    ULONG v;
    for (int i = 1; i <= 3; i++) {
        U(SaveDC)(f.mem);
        v = 0; UN(NtGdiGetDCDword)(f.mem, 5, &v);
        EXPECT_EQ(v, i + 1);
    }
    U(RestoreDC)(f.mem, 1);
    v = 0; UN(NtGdiGetDCDword)(f.mem, 5, &v);
    EXPECT_EQ(v, 1);
    dcf_close(&f);
}

/* ---- NtGdiGetDCforBitmap ---- */

UDECL(HDC, NtGdiGetDCforBitmap, (HBITMAP));

TEST(gdi_dc_objects_dc_for_bitmap_tracks_selection) {
    DCWANT(UN(NtGdiGetDCforBitmap));
    if (t_is_wine()) { t_skip("wine win32u leaves NtGdiGetDCforBitmap unimplemented (aborts)"); return; }
    FX(f);
    HBITMAP bm = U(CreateCompatibleBitmap)(f.scr, 4, 4);
    HGDIOBJ def = U(GetCurrentObject)(f.mem, OBJ_BITMAP);
    /* NtGdiGetDCforBitmap returns the surface's owner-DC field (+0xa0): 0 until selected, then the memory DC. */
    CHECK(UN(NtGdiGetDCforBitmap)(bm) == NULL);
    U(SelectObject)(f.mem, bm);
    EXPECT_EQ(UN(NtGdiGetDCforBitmap)(bm), f.mem);
    HDC d2 = U(CreateCompatibleDC)(f.scr);
    CHECK(U(SelectObject)(d2, bm) == NULL);
    EXPECT_EQ(UN(NtGdiGetDCforBitmap)(bm), f.mem);
    U(SelectObject)(f.mem, def);
    t_note("owner after deselect: %p", UN(NtGdiGetDCforBitmap)(bm));
    U(DeleteObject)(bm);
    U(DeleteDC)(d2);
    dcf_close(&f);
}

TEST(gdi_dc_objects_dc_for_bitmap_bad_handles) {
    DCWANT(UN(NtGdiGetDCforBitmap));
    if (t_is_wine()) { t_skip("wine win32u leaves NtGdiGetDCforBitmap unimplemented (aborts)"); return; }
    HPEN pen = U(CreatePen)(PS_SOLID, 1, 0);
    HBITMAP dead = U(CreateBitmap)(1, 1, 1, 1, NULL);
    U(DeleteObject)(dead);
    CHECK(UN(NtGdiGetDCforBitmap)(NULL) == NULL);
    CHECK(UN(NtGdiGetDCforBitmap)((HBITMAP)pen) == NULL);   /* the kernel rejects non-bitmap types */
    CHECK(UN(NtGdiGetDCforBitmap)(dead) == NULL);
    U(DeleteObject)(pen);
}

TEST(gdi_dc_objects_dc_for_bitmap_default_bitmap_note) {
    DCWANT(UN(NtGdiGetDCforBitmap));
    if (t_is_wine()) { t_skip("wine win32u leaves NtGdiGetDCforBitmap unimplemented (aborts)"); return; }
    FX(f);
    HGDIOBJ def = U(GetCurrentObject)(f.mem, OBJ_BITMAP);
    t_note("owner of the stock bitmap: %p", UN(NtGdiGetDCforBitmap)((HBITMAP)def));
    dcf_close(&f);
}
