/* CreateEnhMetaFileW / CloseEnhMetaFile / DeleteEnhMetaFile and what the kernel hands back.
 *
 * gdi32full!CreateEnhMetaFileW calls NtGdiCreateMetafileDC(hdcRef) (NULL -> UserGetDesktopDC(2); otherwise
 * an alternate-type display DC, so the DC handle carries the alt-DC type 0x21), then
 * builds the client-side LDC (pldcCreate(hdc, 2)) and metafile state (pmdcAllocMDC). CloseEnhMetaFile frees the DC
 * (InternalDeleteDC) and returns a CLIENT object handle created by hCreateClientObjLink -> NtGdiCreateClientObj(0x460000):
 * NtGdiCreateClientObj allocates an OBJECT of kernel type 6 and the handle's type bits are the requested 0x46.
 * GetObjectType: 0x210000 -> alPublicTypes[ldc.type==2] == 12 (OBJ_ENHMETADC); 0x460000 -> 13 (OBJ_ENHMETAFILE).
 * A bad frame rectangle (ERECTL::bEmpty) fails with GdiSetLastError(0x57) before the DC is deleted again.
 */
#include "mf.h"

#define P "gdi_metafile_print_neg_"

TEST(gdi_metafile_print_neg_emf_dc_handle_is_alt_dc_type) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    HDC m = U(CreateEnhMetaFileW)(ref, NULL, NULL, NULL);
    CHECK(m != NULL);
    if (m) {
        EXPECT_EQ(MF_TYPE7(m), MF_T7_ALTDC);        /* alternate-type display DC */
        CHECK(!MF_STOCKED(m));
        EXPECT_EQ(U(GetObjectType)(m), OBJ_ENHMETADC);
        HENHMETAFILE e = U(CloseEnhMetaFile)(m);
        CHECK(e != NULL);
        if (e) U(DeleteEnhMetaFile)(e);
    }
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_emf_dc_not_memdc_not_display) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    HDC m = U(CreateEnhMetaFileW)(ref, NULL, NULL, NULL);
    CHECK(m != NULL);
    if (m) {
        DWORD t = U(GetObjectType)(m);
        CHECK(t != OBJ_DC);
        CHECK(t != OBJ_MEMDC);
        CHECK(t != OBJ_METADC);
        HENHMETAFILE e = U(CloseEnhMetaFile)(m);
        if (e) U(DeleteEnhMetaFile)(e);
    }
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_emf_handle_is_client_object_type_46) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    HENHMETAFILE e = mf_make(ref, NULL, NULL, 1);
    CHECK(e != NULL);
    if (e) {
        EXPECT_EQ(MF_TYPE7(e), MF_T7_EMF);
        CHECK(!MF_STOCKED(e));
        EXPECT_EQ(U(GetObjectType)(e), OBJ_ENHMETAFILE);
        CHECK(U(DeleteEnhMetaFile)(e));
    }
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_emf_handle_cell_is_client_obj) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    HENHMETAFILE e = mf_make(ref, NULL, NULL, 1);
    struct mf_cell c;
    REQUIRE(e);
    if (t_is_wine() || !mf_cell_of(e, &c)) { t_skip("GDI shared table not readable"); U(DeleteEnhMetaFile)(e); mf_screen_free(ref); return; }
    EXPECT_EQ(c.objt, 6);                        /* NtGdiCreateClientObj: AllocateObject(0x18, 6) */
    EXPECT_EQ(c.upper, (unsigned)(((ULONG_PTR)e >> 16) & 0xffff)); /* the cell stores HIWORD(handle) */
    EXPECT_EQ(c.owner & ~1u, GetCurrentProcessId());
    EXPECT_EQ((unsigned)(c.upper >> 8), MF_UNIQ(e));
    U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_emf_dc_cell_is_dc_object) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    HDC m = U(CreateEnhMetaFileW)(ref, NULL, NULL, NULL);
    struct mf_cell c;
    REQUIRE(m);
    if (t_is_wine() || !mf_cell_of(m, &c)) { t_skip("GDI shared table not readable"); HENHMETAFILE e = U(CloseEnhMetaFile)(m); if (e) U(DeleteEnhMetaFile)(e); mf_screen_free(ref); return; }
    EXPECT_EQ(c.objt, 1);                        /* alt DC type 0x21 still uses object type 1 (gdi32!GetDeviceCaps checks cell+0xe == 1) */
    EXPECT_EQ(c.owner & ~1u, GetCurrentProcessId());
    EXPECT_EQ(c.upper, (unsigned)(((ULONG_PTR)m >> 16) & 0xffff));
    CHECK(c.puser != 0);                         /* DC_ATTR page pointer */
    HENHMETAFILE e = U(CloseEnhMetaFile)(m);
    if (e) U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_emf_dc_cell_freed_after_close) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    HDC m = U(CreateEnhMetaFileW)(ref, NULL, NULL, NULL);
    REQUIRE(m);
    HENHMETAFILE e = U(CloseEnhMetaFile)(m);
    CHECK(e != NULL);
    /* the closed DC handle is dead: GetObjectType validates the cell (upper bits) and the owner */
    SetLastError(0);
    DWORD t = U(GetObjectType)(m);
    CHECK(t == 0);
    if (e) U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_close_regular_dc_fails_6) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    HDC mem = U(CreateCompatibleDC)(ref);
    REQUIRE(mem);
    SetLastError(0);
    HENHMETAFILE e = U(CloseEnhMetaFile)(mem);      /* pldcGet == 0 for an ordinary DC */
    CHECK(e == NULL);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    CHECK(U(GetObjectType)(mem) == OBJ_MEMDC);      /* untouched */
    U(DeleteDC)(mem);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_close_display_dc_fails_6) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    SetLastError(0);
    CHECK(U(CloseEnhMetaFile)(ref) == NULL);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    CHECK(U(GetObjectType)(ref) == OBJ_DC);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_close_null_fails_6) {
    MF_NEED_EMF();
    SetLastError(0);
    CHECK(U(CloseEnhMetaFile)(NULL) == NULL);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
}

TEST(gdi_metafile_print_neg_close_garbage_handles_fail) {
    MF_NEED_EMF();
    HDC bad[] = { (HDC)(ULONG_PTR)0x1, (HDC)(ULONG_PTR)0x00210001, (HDC)(ULONG_PTR)0x00660000, (HDC)(ULONG_PTR)0xffffffff };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(0);
        CHECK(U(CloseEnhMetaFile)(bad[i]) == NULL);
        EXPECT_ERR(ERROR_INVALID_HANDLE);                /* 0x660000 type is rejected up front, others fail pldcGet */
    }
}

TEST(gdi_metafile_print_neg_close_twice_second_fails) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    HDC m = U(CreateEnhMetaFileW)(ref, NULL, NULL, NULL);
    REQUIRE(m);
    HENHMETAFILE e = U(CloseEnhMetaFile)(m);
    CHECK(e != NULL);
    SetLastError(0);
    CHECK(U(CloseEnhMetaFile)(m) == NULL);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    if (e) U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_delete_emf_twice_second_fails_6) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    HENHMETAFILE e = mf_make(ref, NULL, NULL, 1);
    REQUIRE(e);
    CHECK(U(DeleteEnhMetaFile)(e));
    SetLastError(0);
    CHECK(!U(DeleteEnhMetaFile)(e));                /* pvClientObjGet: link no longer found -> GdiSetLastError(6) */
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    CHECK(U(GetObjectType)(e) == OBJ_ENHMETAFILE);   /* the freed cell keeps its upper word, so the handle's type is still reported */
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_delete_null_fails_6) {
    MF_NEED_EMF();
    SetLastError(0);
    CHECK(!U(DeleteEnhMetaFile)(NULL));
    EXPECT_ERR(ERROR_INVALID_HANDLE);
}

TEST(gdi_metafile_print_neg_delete_wrong_type_handles_fail_6) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    HGDIOBJ objs[3]; int n = 0;
    objs[n++] = U(CreateSolidBrush)(RGB(1, 2, 3));
    objs[n++] = U(CreatePen)(PS_SOLID, 1, RGB(1, 2, 3));
    objs[n++] = U(CreateCompatibleBitmap)(ref, 4, 4);
    for (int i = 0; i < n; i++) {
        REQUIRE(objs[i]);
        SetLastError(0);
        CHECK(!U(DeleteEnhMetaFile)((HENHMETAFILE)objs[i]));   /* type bits != 0x46 */
        EXPECT_ERR(ERROR_INVALID_HANDLE);
        CHECK(U(GetObjectType)(objs[i]) != 0);                  /* object survived */
        U(DeleteObject)(objs[i]);
    }
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_delete_dc_handle_as_emf_fails) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    HDC mem = U(CreateCompatibleDC)(ref);
    REQUIRE(mem);
    CHECK(!U(DeleteEnhMetaFile)((HENHMETAFILE)mem));
    CHECK(U(GetObjectType)(mem) == OBJ_MEMDC);
    U(DeleteDC)(mem);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_create_empty_frame_rect_fails_87) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    /* only a frame with right < left or bottom < top is refused; a zero-width or zero-height frame is accepted */
    static const RECT bad[] = { {10, 0, 5, 100}, {0, 10, 100, 5}, {100, 0, 0, 100}, {0, 100, 100, 0} };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(0);
        HDC m = U(CreateEnhMetaFileW)(ref, NULL, &bad[i], NULL);
        CHECK(m == NULL);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
        mf_discard(m);
    }
    static const RECT ok[] = { {5, 5, 5, 5}, {0, 0, 0, 100}, {0, 0, 100, 0} };
    for (unsigned i = 0; i < sizeof ok / sizeof ok[0]; i++) {
        HDC m = U(CreateEnhMetaFileW)(ref, NULL, &ok[i], NULL);
        CHECK(m != NULL);
        mf_discard(m);
    }
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_create_bad_frame_does_not_leak_dc) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    DWORD before = U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS);
    RECT bad = { 10, 1, 5, 20 };
    for (int i = 0; i < 20; i++) CHECK(U(CreateEnhMetaFileW)(ref, NULL, &bad, NULL) == NULL);
    DWORD after = U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS);
    EXPECT_EQ(after, before);                       /* InternalDeleteDC(hdc, 2) on the failure path */
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_create_invalid_reference_dc_fails) {
    MF_NEED_EMF();
    /* NtGdiCreateMetafileDC: DC lookup fails -> returns 0 */
    HDC bad[] = { (HDC)(ULONG_PTR)0x1234, (HDC)(ULONG_PTR)0xdeadbeef, (HDC)(ULONG_PTR)0x00010001 };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        HDC m = U(CreateEnhMetaFileW)(bad[i], NULL, NULL, NULL);
        CHECK(m == NULL);
        mf_discard(m);
    }
}

TEST(gdi_metafile_print_neg_create_reference_is_non_dc_object_fails) {
    MF_NEED_EMF();
    HGDIOBJ b = U(CreateSolidBrush)(RGB(9, 9, 9));
    REQUIRE(b);
    HDC m = U(CreateEnhMetaFileW)((HDC)b, NULL, NULL, NULL);    /* DC lookup on a brush handle fails */
    CHECK(m == NULL);
    mf_discard(m);
    U(DeleteObject)(b);
}

TEST(gdi_metafile_print_neg_create_reference_deleted_dc_fails) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    HDC mem = U(CreateCompatibleDC)(ref);
    REQUIRE(mem);
    U(DeleteDC)(mem);
    HDC m = U(CreateEnhMetaFileW)(mem, NULL, NULL, NULL);
    CHECK(m == NULL);
    mf_discard(m);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_create_reference_null_uses_desktop_dc) {
    MF_NEED_EMF();
    HDC m = U(CreateEnhMetaFileW)(NULL, NULL, NULL, NULL);      /* NtGdiCreateMetafileDC(NULL) -> UserGetDesktopDC(2) */
    CHECK(m != NULL);
    if (m) {
        EXPECT_EQ(MF_TYPE7(m), MF_T7_ALTDC);
        HENHMETAFILE e = U(CloseEnhMetaFile)(m);
        CHECK(e != NULL);
        if (e) {
            ENHMETAHEADER h; memset(&h, 0, sizeof h);
            CHECK(U(GetEnhMetaFileHeader)(e, sizeof h, &h) != 0);
            HDC s = mf_screen();
            if (s) {
                EXPECT_EQ(h.szlDevice.cx, U(GetDeviceCaps)(s, HORZRES));
                EXPECT_EQ(h.szlDevice.cy, U(GetDeviceCaps)(s, VERTRES));
                mf_screen_free(s);
            }
            U(DeleteEnhMetaFile)(e);
        }
    }
}

TEST(gdi_metafile_print_neg_create_reference_memory_dc_ok) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    HDC mem = U(CreateCompatibleDC)(ref);
    REQUIRE(mem);
    HDC m = U(CreateEnhMetaFileW)(mem, NULL, NULL, NULL);
    CHECK(m != NULL);
    if (m) {
        EXPECT_EQ(MF_TYPE7(m), MF_T7_ALTDC);
        HENHMETAFILE e = U(CloseEnhMetaFile)(m);
        if (e) U(DeleteEnhMetaFile)(e);
    }
    CHECK(U(GetObjectType)(mem) == OBJ_MEMDC);
    U(DeleteDC)(mem);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_two_open_metafile_dcs_distinct) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    HDC a = U(CreateEnhMetaFileW)(ref, NULL, NULL, NULL), b = U(CreateEnhMetaFileW)(ref, NULL, NULL, NULL);
    CHECK(a && b);
    if (a && b) {
        CHECK(a != b);
        CHECK(MF_IDX(a) != MF_IDX(b));
        U(Rectangle)(a, 1, 1, 20, 20);
        U(Rectangle)(b, 2, 2, 30, 30); U(Rectangle)(b, 3, 3, 31, 31);
        HENHMETAFILE eb = U(CloseEnhMetaFile)(b), ea = U(CloseEnhMetaFile)(a);   /* close in reverse order */
        CHECK(ea && eb && ea != eb);
        struct mf_enum x, y;
        if (ea && eb) {
            mf_enum(ea, &x); mf_enum(eb, &y);
            EXPECT_EQ(x.counts[EMR_RECTANGLE], 1);
            EXPECT_EQ(y.counts[EMR_RECTANGLE], 2);
        }
        if (ea) U(DeleteEnhMetaFile)(ea);
        if (eb) U(DeleteEnhMetaFile)(eb);
    }
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_create_close_loop_gdi_count_balanced) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    DWORD before = U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS);
    for (int i = 0; i < 60; i++) {
        HDC m = U(CreateEnhMetaFileW)(ref, NULL, NULL, NULL);
        if (!m) { CHECK(m != NULL); break; }
        U(Rectangle)(m, 1, 1, 10 + i, 10 + i);
        HENHMETAFILE e = U(CloseEnhMetaFile)(m);
        CHECK(e != NULL);
        if (e) CHECK(U(DeleteEnhMetaFile)(e));
    }
    DWORD after = U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS);
    EXPECT_EQ(after, before);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_open_metafile_dc_counts_as_gdi_object) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    DWORD before = U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS);
    HDC m = U(CreateEnhMetaFileW)(ref, NULL, NULL, NULL);
    REQUIRE(m);
    DWORD during = U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS);
    CHECK(during > before);
    HENHMETAFILE e = U(CloseEnhMetaFile)(m);
    DWORD closed = U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS);
    t_note("GDI objects: before=%lu open=%lu closed(EMF alive)=%lu", before, during, closed);
    if (e) U(DeleteEnhMetaFile)(e);
    DWORD after = U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS);
    EXPECT_EQ(after, before);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_emf_handle_survives_reference_dc_release) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    HENHMETAFILE e = mf_make(ref, NULL, NULL, 2);
    mf_screen_free(ref);                            /* the metafile no longer depends on the reference DC */
    REQUIRE(e);
    ENHMETAHEADER h;
    CHECK(U(GetEnhMetaFileHeader)(e, sizeof h, &h) != 0);
    EXPECT_EQ(h.iType, EMR_HEADER);
    CHECK(U(DeleteEnhMetaFile)(e));
}

TEST(gdi_metafile_print_neg_metafile_dc_getdevicecaps_matches_reference) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    HDC m = U(CreateEnhMetaFileW)(ref, NULL, NULL, NULL);
    REQUIRE(m);
    static const int idx[] = { HORZRES, VERTRES, HORZSIZE, VERTSIZE, BITSPIXEL, PLANES, LOGPIXELSX, LOGPIXELSY,
                               TECHNOLOGY, NUMBRUSHES, NUMPENS, CLIPCAPS, CURVECAPS, LINECAPS, POLYGONALCAPS, RASTERCAPS };
    for (unsigned i = 0; i < sizeof idx / sizeof idx[0]; i++) {
        int a = U(GetDeviceCaps)(m, idx[i]), b = U(GetDeviceCaps)(ref, idx[i]);
        CHECKF(a == b, "index %d: metafile DC %d, reference %d", idx[i], a, b);   /* NtGdiGetDeviceCapsAll reads the shared pdev */
    }
    HENHMETAFILE e = U(CloseEnhMetaFile)(m);
    if (e) U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_file_metafile_roundtrip_on_disk) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    char a[MAX_PATH]; wchar_t w[MAX_PATH];
    t_tmpfile(a, sizeof a, "a.emf");
    MultiByteToWideChar(CP_ACP, 0, a, -1, w, MAX_PATH);
    HDC m = U(CreateEnhMetaFileW)(ref, w, NULL, NULL);
    CHECK(m != NULL);
    if (m) {
        U(Rectangle)(m, 5, 5, 40, 40);
        HENHMETAFILE e = U(CloseEnhMetaFile)(m);
        CHECK(e != NULL);
        if (e) {
            ENHMETAHEADER h; memset(&h, 0, sizeof h);
            CHECK(U(GetEnhMetaFileHeader)(e, sizeof h, &h) != 0);
            WIN32_FILE_ATTRIBUTE_DATA fa;
            CHECK(GetFileAttributesExA(a, GetFileExInfoStandard, &fa));
            EXPECT_EQ(fa.nFileSizeLow, h.nBytes);           /* MDC::bFlush + header rewrite */
            UINT n = U(GetEnhMetaFileBits)(e, 0, NULL);
            EXPECT_EQ(n, h.nBytes);
            CHECK(U(DeleteEnhMetaFile)(e));
            CHECK(GetFileAttributesA(a) != INVALID_FILE_ATTRIBUTES);  /* deleting the handle never deletes the file */
        }
    }
    DeleteFileA(a);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_file_metafile_bits_equal_file_content) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    char a[MAX_PATH]; wchar_t w[MAX_PATH];
    t_tmpfile(a, sizeof a, "b.emf");
    MultiByteToWideChar(CP_ACP, 0, a, -1, w, MAX_PATH);
    HDC m = U(CreateEnhMetaFileW)(ref, w, NULL, L"Test\0Doc\0");
    REQUIRE(m);
    U(Ellipse)(m, 3, 3, 33, 33); U(Rectangle)(m, 1, 1, 9, 9);
    HENHMETAFILE e = U(CloseEnhMetaFile)(m);
    REQUIRE(e);
    UINT n = U(GetEnhMetaFileBits)(e, 0, NULL);
    CHECK(n >= 88);
    if (n >= 88 && n < (1u << 24)) {
        BYTE *buf = (BYTE *)malloc(n), *fb = (BYTE *)malloc(n);
        HANDLE f = CreateFileA(a, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        DWORD got = 0;
        CHECK(f != INVALID_HANDLE_VALUE);
        if (f != INVALID_HANDLE_VALUE && buf && fb) {
            CHECK(ReadFile(f, fb, n, &got, NULL) && got == n);
            EXPECT_EQ(U(GetEnhMetaFileBits)(e, n, buf), n);
            CHECK(memcmp(buf, fb, n) == 0);
        }
        if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
        free(buf); free(fb);
    }
    U(DeleteEnhMetaFile)(e);
    DeleteFileA(a);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_file_metafile_unwritable_path_fails) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    char a[MAX_PATH]; wchar_t w[MAX_PATH];
    t_tmpfile(a, sizeof a, "nodir\\x\\y.emf");          /* parent directory does not exist */
    MultiByteToWideChar(CP_ACP, 0, a, -1, w, MAX_PATH);
    HDC m = U(CreateEnhMetaFileW)(ref, w, NULL, NULL);
    CHECK(m == NULL);
    mf_discard(m);
    CHECK(GetFileAttributesA(a) == INVALID_FILE_ATTRIBUTES);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_getenhmetafile_from_disk_loads_same_bits) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    char a[MAX_PATH]; wchar_t w[MAX_PATH];
    if (!U(GetEnhMetaFileW)) { t_skip("GetEnhMetaFileW missing"); return; }
    t_tmpfile(a, sizeof a, "c.emf");
    MultiByteToWideChar(CP_ACP, 0, a, -1, w, MAX_PATH);
    HDC m = U(CreateEnhMetaFileW)(ref, w, NULL, NULL);
    REQUIRE(m);
    U(Rectangle)(m, 2, 2, 20, 20);
    HENHMETAFILE e = U(CloseEnhMetaFile)(m), e2 = U(GetEnhMetaFileW)(w);
    CHECK(e && e2 && e != e2);
    if (e && e2) {
        EXPECT_EQ(MF_TYPE7(e2), MF_T7_EMF);
        EXPECT_EQ(U(GetEnhMetaFileBits)(e, 0, NULL), U(GetEnhMetaFileBits)(e2, 0, NULL));
    }
    if (e) U(DeleteEnhMetaFile)(e);
    if (e2) U(DeleteEnhMetaFile)(e2);
    DeleteFileA(a);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_getenhmetafile_missing_file_fails) {
    MF_NEED_EMF();
    if (!U(GetEnhMetaFileW)) { t_skip("GetEnhMetaFileW missing"); return; }
    char a[MAX_PATH]; wchar_t w[MAX_PATH];
    t_tmpfile(a, sizeof a, "missing.emf");
    MultiByteToWideChar(CP_ACP, 0, a, -1, w, MAX_PATH);
    HENHMETAFILE e = U(GetEnhMetaFileW)(w);
    CHECK(e == NULL);
    if (e) U(DeleteEnhMetaFile)(e);
}

TEST(gdi_metafile_print_neg_getenhmetafile_garbage_file_fails) {
    MF_NEED_EMF();
    if (!U(GetEnhMetaFileW)) { t_skip("GetEnhMetaFileW missing"); return; }
    char a[MAX_PATH]; wchar_t w[MAX_PATH];
    t_tmpfile(a, sizeof a, "garbage.emf");
    MultiByteToWideChar(CP_ACP, 0, a, -1, w, MAX_PATH);
    HANDLE f = CreateFileA(a, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    REQUIRE(f != INVALID_HANDLE_VALUE);
    BYTE junk[256]; memset(junk, 0x5a, sizeof junk);
    DWORD wr; WriteFile(f, junk, sizeof junk, &wr, NULL); CloseHandle(f);
    HENHMETAFILE e = U(GetEnhMetaFileW)(w);               /* header iType/signature validation */
    CHECK(e == NULL);
    if (e) U(DeleteEnhMetaFile)(e);
    DeleteFileA(a);
}

TEST(gdi_metafile_print_neg_metafile_dc_drawing_no_gdi_leak_per_call) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    HDC m = U(CreateEnhMetaFileW)(ref, NULL, NULL, NULL);
    REQUIRE(m);
    DWORD b = U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS);
    for (int i = 0; i < 100; i++) { U(MoveToEx)(m, i, 0, NULL); U(LineTo)(m, i, 50); U(Rectangle)(m, i, i, i + 5, i + 5); }
    DWORD a = U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS);
    EXPECT_EQ(a, b);                                 /* drawing with stock objects creates no GDI objects */
    HENHMETAFILE e = U(CloseEnhMetaFile)(m);
    if (e) U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

