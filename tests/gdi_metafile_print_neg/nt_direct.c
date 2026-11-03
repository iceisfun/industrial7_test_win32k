/* Direct win32u syscall checks for the metafile/enumeration/device-caps/server-metafile/spool entry points.
 *
 *   HDC  NtGdiCreateMetafileDC(HDC hdcRef)                      win32kbase: NULL -> UserGetDesktopDC(2), else DCOBJ +
 *                                                                display DC creation; failure -> 0
 *   UINT NtGdiEnumObjects(HDC, int type, UINT cb, PVOID buf)    win32kfull: type 1 pens (5*n entries), 2 brushes (7*n), 16 bytes
 *                                                                each; n = 1/2/8/16/20 from pdev+0x878; both (cb==0) and (buf==NULL)
 *                                                                must agree or 0; cb < count*16 with cb>=16 -> 0; 0<cb<16 -> count
 *   int  NtGdiGetDeviceCaps(HDC, int index)                     GetDeviceCaps: DC lock failure -> EngSetLastError(6), 0
 *   BOOL NtGdiGetDeviceCapsAll(HDC, DEVCAPS *)                  same lock, 0 + error 6 on a bad DC
 *   int  NtGdiConvertMetafileRect(HDC, RECTL *)                 2 ok, 1 degenerate, 0 + error 87 when the DC is invalid
 *   int  NtGdiSetMetaRgn(HDC)                                   vLock failure -> EngSetLastError(0x57), 0
 *   HANDLE NtGdiCreateServerMetaFile(DWORD type, ULONG cb, PBYTE bits, DWORD mm, DWORD xExt, DWORD yExt)
 *   ULONG  NtGdiGetServerMetaFileBits(HANDLE, ULONG cb, PBYTE, DWORD *type, DWORD *mm, DWORD *x, DWORD *y)
 *   NtGdiInitSpool() -> EngSetLastError(ERROR_NOT_SUPPORTED=0x32), 0 ; NtGdiGetSpoolMessage / NtGdiUnloadPrinterDriver -> 0
 */
#include "mf.h"

UDECL(HDC, NtGdiCreateMetafileDC, (HDC));
UDECL(UINT, NtGdiEnumObjects, (HDC, int, UINT, PVOID));
UDECL(int, NtGdiGetDeviceCaps, (HDC, int));
UDECL(BOOL, NtGdiGetDeviceCapsAll, (HDC, PVOID));
UDECL(int, NtGdiConvertMetafileRect, (HDC, RECTL *));
UDECL(int, NtGdiSetMetaRgn, (HDC));
UDECL(HANDLE, NtGdiCreateServerMetaFile, (DWORD, ULONG, PBYTE, DWORD, DWORD, DWORD));
UDECL(ULONG, NtGdiGetServerMetaFileBits, (HANDLE, ULONG, PBYTE, DWORD *, DWORD *, DWORD *, DWORD *));
UDECL(ULONG_PTR, NtGdiInitSpool, (void));
UDECL(ULONG_PTR, NtGdiGetSpoolMessage, (ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR));
UDECL(ULONG_PTR, NtGdiUnloadPrinterDriver, (ULONG_PTR, ULONG_PTR));
UDECL(BOOL, NtGdiDeleteObjectApp, (HANDLE));
UDECL(UINT, NtGdiQueryFontAssocInfo, (HDC));
UDECL(HANDLE, GdiConvertEnhMetaFile, (HENHMETAFILE));

#define AL8 __attribute__((aligned(8)))
/* Wine's win32u aborts the process on a syscall stub it does not implement, so direct Nt* calls are Windows-only. */
#define NEED_NT(f) do { if (t_is_wine()) { t_skip("direct win32k syscalls are not available under Wine"); return; } \
    if (!UN(f) || !U(GetDC)) { t_skip("missing export: " #f); return; } } while (0)

/* ---- NtGdiCreateMetafileDC --------------------------------------------------------------------------------------- */
TEST(gdi_metafile_print_neg_nt_createmetafiledc_screen_dc) {
    NEED_NT(NtGdiCreateMetafileDC); MF_SCREEN(ref);
    HDC m = UN(NtGdiCreateMetafileDC)(ref);
    CHECK(m != NULL);
    if (m) {
        EXPECT_EQ(MF_TYPE7(m), MF_T7_ALTDC);
        CHECK(!MF_STOCKED(m));
        struct mf_cell c;
        if (!t_is_wine() && mf_cell_of(m, &c)) {
            EXPECT_EQ(c.objt, 1);
            EXPECT_EQ(c.owner & ~1u, GetCurrentProcessId());
        }
        if (UN(NtGdiDeleteObjectApp)) CHECK(UN(NtGdiDeleteObjectApp)(m));
    }
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_nt_createmetafiledc_null_reference) {
    NEED_NT(NtGdiCreateMetafileDC);
    HDC m = UN(NtGdiCreateMetafileDC)(NULL);                 /* UserGetDesktopDC(2) */
    CHECK(m != NULL);
    if (m) {
        EXPECT_EQ(MF_TYPE7(m), MF_T7_ALTDC);
        if (UN(NtGdiDeleteObjectApp)) CHECK(UN(NtGdiDeleteObjectApp)(m));
    }
}

TEST(gdi_metafile_print_neg_nt_createmetafiledc_memory_reference) {
    NEED_NT(NtGdiCreateMetafileDC); MF_SCREEN(ref);
    HDC mem = U(CreateCompatibleDC)(ref);
    REQUIRE(mem);
    HDC m = UN(NtGdiCreateMetafileDC)(mem);
    CHECK(m != NULL);
    if (m) {
        EXPECT_EQ(MF_TYPE7(m), MF_T7_ALTDC);
        if (UN(NtGdiDeleteObjectApp)) CHECK(UN(NtGdiDeleteObjectApp)(m));
    }
    U(DeleteDC)(mem);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_nt_createmetafiledc_bad_reference_returns_null) {
    NEED_NT(NtGdiCreateMetafileDC); MF_SCREEN(ref);
    HGDIOBJ br = U(CreateSolidBrush)(1);
    HDC mem = U(CreateCompatibleDC)(ref);
    HDC gone = mem;
    U(DeleteDC)(mem);
    HDC bad[] = { (HDC)(ULONG_PTR)0x1234, (HDC)(ULONG_PTR)0xdeadbeef, (HDC)br, gone, (HDC)U(GetStockObject)(BLACK_PEN) };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        HDC m = UN(NtGdiCreateMetafileDC)(bad[i]);
        CHECKF(m == NULL, "reference %u", i);
        if (m && UN(NtGdiDeleteObjectApp)) UN(NtGdiDeleteObjectApp)(m);
    }
    U(DeleteObject)(br);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_nt_createmetafiledc_raw_dc_has_no_client_ldc) {
    NEED_NT(NtGdiCreateMetafileDC); MF_SCREEN(ref);
    HDC m = UN(NtGdiCreateMetafileDC)(ref);
    REQUIRE(m);
    /* gdi32full!GetObjectType: 0x210000 needs pldcGet(); the raw kernel DC has none -> GdiSetLastError(6), 0 */
    SetLastError(0);
    EXPECT_EQ(U(GetObjectType)(m), 0);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    /* ... but the kernel DC itself is alive and answers device caps */
    if (UN(NtGdiGetDeviceCaps)) EXPECT_EQ(UN(NtGdiGetDeviceCaps)(m, HORZRES), U(GetDeviceCaps)(ref, HORZRES));
    if (UN(NtGdiDeleteObjectApp)) CHECK(UN(NtGdiDeleteObjectApp)(m));
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_nt_createmetafiledc_many_distinct_and_freed) {
    NEED_NT(NtGdiCreateMetafileDC); NEED_NT(NtGdiDeleteObjectApp); MF_SCREEN(ref);
    enum { N = 64 };
    HDC h[N]; int made = 0;
    DWORD before = U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS);
    for (int i = 0; i < N; i++) { h[i] = UN(NtGdiCreateMetafileDC)(ref); if (!h[i]) break; made++; }
    EXPECT_EQ(made, N);
    DWORD during = U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS);
    if (!t_is_wine()) EXPECT_EQ(during, before + made);
    for (int i = 0; i < made; i++) for (int j = i + 1; j < made; j++) if (h[i] == h[j]) CHECKF(0, "duplicate handle %d/%d", i, j);
    for (int i = 0; i < made; i++) CHECK(UN(NtGdiDeleteObjectApp)(h[i]));
    EXPECT_EQ(U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS), before);
    mf_screen_free(ref);
}

/* ---- NtGdiEnumObjects ------------------------------------------------------------------------------------------ */
static UINT enum_count(HDC dc, int type) { return UN(NtGdiEnumObjects)(dc, type, 0, NULL); }

TEST(gdi_metafile_print_neg_nt_enumobjects_counts_are_5n_and_7n) {
    NEED_NT(NtGdiEnumObjects); MF_SCREEN(ref);
    UINT p = enum_count(ref, OBJ_PEN), b = enum_count(ref, OBJ_BRUSH);
    CHECK(p != 0 && b != 0);
    if (p && b) {
        EXPECT_EQ(p % 5, 0);                                     /* 5 pen styles * n colours */
        EXPECT_EQ(b % 7, 0);                                     /* n solid + 6 hatches * n */
        EXPECT_EQ(p / 5, b / 7);
        UINT n = p / 5;
        CHECK(n == 1 || n == 2 || n == 8 || n == 16 || n == 20); /* clamp ladder on pdev+0x878 */
        t_note("EnumObjects: n=%u pens=%u brushes=%u", n, p, b);
    }
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_nt_enumobjects_same_for_memory_and_metafile_dc) {
    NEED_NT(NtGdiEnumObjects); MF_SCREEN(ref);
    HDC mem = U(CreateCompatibleDC)(ref);
    HDC m = U(CreateEnhMetaFileW)(ref, NULL, NULL, NULL);
    REQUIRE(mem && m);
    UINT p = enum_count(ref, OBJ_PEN), b = enum_count(ref, OBJ_BRUSH);
    EXPECT_EQ(enum_count(mem, OBJ_PEN), p);                      /* all three DCs share the display pdev */
    EXPECT_EQ(enum_count(mem, OBJ_BRUSH), b);
    EXPECT_EQ(enum_count(m, OBJ_PEN), p);
    EXPECT_EQ(enum_count(m, OBJ_BRUSH), b);
    HENHMETAFILE e = U(CloseEnhMetaFile)(m);
    if (e) U(DeleteEnhMetaFile)(e);
    U(DeleteDC)(mem);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_nt_enumobjects_pen_entries_layout) {
    NEED_NT(NtGdiEnumObjects); MF_SCREEN(ref);
    UINT n = enum_count(ref, OBJ_PEN);
    REQUIRE(n);
    LOGPEN *lp = (LOGPEN *)calloc(n + 2, sizeof(LOGPEN));
    REQUIRE(lp);
    memset(lp, 0xaa, (n + 2) * sizeof(LOGPEN));
    EXPECT_EQ(UN(NtGdiEnumObjects)(ref, OBJ_PEN, n * 16, lp), n);
    unsigned bad = 0, highbyte = 0, styles[5] = { 0 };
    for (UINT i = 0; i < n; i++) {
        if (lp[i].lopnStyle > PS_DASHDOTDOT) bad++; else styles[lp[i].lopnStyle]++;
        if (lp[i].lopnWidth.x != 0 || lp[i].lopnWidth.y != 0) bad++;     /* zero-width cosmetic pens */
        if (lp[i].lopnColor & 0xff000000) highbyte++;
    }
    if (highbyte) t_note("%u pen colours carry flag bits in the high byte (palette-style COLORREF)", highbyte);
    EXPECT_EQ(bad, 0);
    for (int s = 0; s < 5; s++) EXPECT_EQ(styles[s], n / 5);             /* each style appears once per colour */
    EXPECT_EQ(*(DWORD *)&lp[n], 0xaaaaaaaa);                              /* nothing past the last entry */
    free(lp);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_nt_enumobjects_brush_entries_layout) {
    NEED_NT(NtGdiEnumObjects); MF_SCREEN(ref);
    UINT n = enum_count(ref, OBJ_BRUSH);
    REQUIRE(n);
    LOGBRUSH *lb = (LOGBRUSH *)calloc(n + 2, sizeof(LOGBRUSH));
    REQUIRE(lb);
    memset(lb, 0xaa, (n + 2) * sizeof(LOGBRUSH));
    EXPECT_EQ(UN(NtGdiEnumObjects)(ref, OBJ_BRUSH, n * 16, lb), n);
    UINT solid = n / 7, i = 0, bad = 0;
    for (; i < solid; i++) { if (lb[i].lbStyle != BS_SOLID || lb[i].lbHatch != 0) bad++; }
    for (; i < n; i++) { if (lb[i].lbStyle != BS_HATCHED || lb[i].lbHatch > HS_DIAGCROSS) bad++; }
    EXPECT_EQ(bad, 0);
    EXPECT_EQ(*(DWORD *)&lb[n], 0xaaaaaaaa);
    free(lb);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_nt_enumobjects_buffer_size_protocol) {
    NEED_NT(NtGdiEnumObjects); MF_SCREEN(ref);
    UINT n = enum_count(ref, OBJ_PEN);
    REQUIRE(n);
    BYTE *buf = (BYTE *)malloc(n * 16 + 256);
    REQUIRE(buf);
    memset(buf, 0xcc, n * 16 + 256);
    EXPECT_EQ(UN(NtGdiEnumObjects)(ref, OBJ_PEN, (n - 1) * 16, buf), 0);   /* too small: 0 */
    CHECK(buf[0] == 0xcc);
    EXPECT_EQ(UN(NtGdiEnumObjects)(ref, OBJ_PEN, 15, buf), n);              /* 0 < cb < 16: acts as a size query */
    CHECK(buf[0] == 0xcc);
    EXPECT_EQ(UN(NtGdiEnumObjects)(ref, OBJ_PEN, 16, buf), n >= 2 ? 0 : n); /* one entry buffer: only valid when n == 1 */
    memset(buf, 0xcc, n * 16 + 256);
    EXPECT_EQ(UN(NtGdiEnumObjects)(ref, OBJ_PEN, n * 16 + 200, buf), n);    /* larger buffer is fine */
    for (int k = 4; k < 12; k++) EXPECT_EQ(buf[(n - 1) * 16 + k], 0);        /* last LOGPEN's lopnWidth is {0,0} */
    EXPECT_EQ(buf[n * 16 + 100], 0xcc);
    free(buf);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_nt_enumobjects_inconsistent_size_and_buffer) {
    NEED_NT(NtGdiEnumObjects); MF_SCREEN(ref);
    BYTE buf[64];
    EXPECT_EQ(UN(NtGdiEnumObjects)(ref, OBJ_PEN, 0, buf), 0);               /* (cb==0) != (buf==NULL) */
    EXPECT_EQ(UN(NtGdiEnumObjects)(ref, OBJ_PEN, 64, NULL), 0);
    EXPECT_EQ(UN(NtGdiEnumObjects)(ref, OBJ_BRUSH, 0, buf), 0);
    EXPECT_EQ(UN(NtGdiEnumObjects)(ref, OBJ_BRUSH, 64, NULL), 0);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_nt_enumobjects_bad_type_returns_zero) {
    NEED_NT(NtGdiEnumObjects); MF_SCREEN(ref);
    static const int types[] = { 0, 3, 4, 5, 6, 7, 11, 12, 13, -1, 0x10000, 0x7fffffff };
    for (unsigned i = 0; i < sizeof types / sizeof types[0]; i++) {
        EXPECT_EQ(UN(NtGdiEnumObjects)(ref, types[i], 0, NULL), 0);
    }
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_nt_enumobjects_bad_dc_returns_zero) {
    NEED_NT(NtGdiEnumObjects); MF_SCREEN(ref);
    HGDIOBJ br = U(CreateSolidBrush)(1);
    HDC bad[] = { NULL, (HDC)(ULONG_PTR)0x1234, (HDC)br, (HDC)U(GetStockObject)(WHITE_BRUSH), (HDC)(ULONG_PTR)0xffffffff };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        EXPECT_EQ(enum_count(bad[i], OBJ_PEN), 0);
        EXPECT_EQ(enum_count(bad[i], OBJ_BRUSH), 0);
    }
    U(DeleteObject)(br);
    mf_screen_free(ref);
}

static int g_enum_calls, g_enum_stop_at, g_enum_ret, g_enum_bad;
static int CALLBACK enum_obj_cb(LPVOID obj, LPARAM lp) {
    g_enum_calls++;
    if (!obj) g_enum_bad++;
    if (g_enum_stop_at && g_enum_calls >= g_enum_stop_at) return 0;
    return g_enum_ret;
}
TEST(gdi_metafile_print_neg_enumobjects_user_pen_and_brush_counts_match_kernel) {
    NEED_NT(NtGdiEnumObjects); MF_SCREEN(ref);
    REQUIRE(U(EnumObjects));
    UINT p = enum_count(ref, OBJ_PEN), b = enum_count(ref, OBJ_BRUSH);
    g_enum_calls = g_enum_stop_at = g_enum_bad = 0; g_enum_ret = 1;
    EXPECT_EQ(U(EnumObjects)(ref, OBJ_PEN, (GOBJENUMPROC)enum_obj_cb, 0), 1);
    EXPECT_EQ(g_enum_calls, p);
    g_enum_calls = 0;
    EXPECT_EQ(U(EnumObjects)(ref, OBJ_BRUSH, (GOBJENUMPROC)enum_obj_cb, 0), 1);
    EXPECT_EQ(g_enum_calls, b);
    EXPECT_EQ(g_enum_bad, 0);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_enumobjects_returns_last_callback_value) {
    NEED_NT(NtGdiEnumObjects); MF_SCREEN(ref);
    REQUIRE(U(EnumObjects));
    g_enum_calls = g_enum_stop_at = 0; g_enum_ret = 7;
    EXPECT_EQ(U(EnumObjects)(ref, OBJ_PEN, (GOBJENUMPROC)enum_obj_cb, 0), 7);    /* gdi32full!EnumObjects returns the last callback result */
    g_enum_ret = 1; g_enum_stop_at = 3; g_enum_calls = 0;
    EXPECT_EQ(U(EnumObjects)(ref, OBJ_BRUSH, (GOBJENUMPROC)enum_obj_cb, 0), 0);  /* stops at the first FALSE */
    EXPECT_EQ(g_enum_calls, 3);
    g_enum_stop_at = 1; g_enum_calls = 0;
    EXPECT_EQ(U(EnumObjects)(ref, OBJ_PEN, (GOBJENUMPROC)enum_obj_cb, 0), 0);
    EXPECT_EQ(g_enum_calls, 1);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_enumobjects_user_bad_type_fails_87) {
    MF_SCREEN(ref);
    REQUIRE(U(EnumObjects));
    static const int types[] = { 0, 3, 4, 5, 6, 7, 8, 11, 12, 13, -1, 100 };
    for (unsigned i = 0; i < sizeof types / sizeof types[0]; i++) {
        g_enum_calls = g_enum_stop_at = 0; g_enum_ret = 1;
        SetLastError(0);
        EXPECT_EQ(U(EnumObjects)(ref, types[i], (GOBJENUMPROC)enum_obj_cb, 0), 0);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);                      /* nType not 1 or 2 -> GdiSetLastError(0x57) */
        EXPECT_EQ(g_enum_calls, 0);
    }
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_enumobjects_user_invalid_dc_no_callbacks) {
    NEED_NT(NtGdiEnumObjects);
    REQUIRE(U(EnumObjects));
    g_enum_calls = g_enum_stop_at = 0; g_enum_ret = 1;
    EXPECT_EQ(U(EnumObjects)(NULL, OBJ_PEN, (GOBJENUMPROC)enum_obj_cb, 0), 0);
    EXPECT_EQ(U(EnumObjects)((HDC)(ULONG_PTR)0x4321, OBJ_BRUSH, (GOBJENUMPROC)enum_obj_cb, 0), 0);
    EXPECT_EQ(g_enum_calls, 0);
}

TEST(gdi_metafile_print_neg_enumobjects_on_metafile_dc_records_nothing) {
    NEED_NT(NtGdiEnumObjects); MF_SCREEN(ref);
    REQUIRE(U(EnumObjects));
    HDC m = U(CreateEnhMetaFileW)(ref, NULL, NULL, NULL);
    struct mf_enum en;
    REQUIRE(m);
    g_enum_calls = g_enum_stop_at = 0; g_enum_ret = 1;
    CHECK(U(EnumObjects)(m, OBJ_PEN, (GOBJENUMPROC)enum_obj_cb, 0) == 1);
    EXPECT_EQ(g_enum_calls, enum_count(ref, OBJ_PEN));
    HENHMETAFILE e = U(CloseEnhMetaFile)(m), e0 = mf_make(ref, NULL, NULL, 0);
    struct mf_enum en0;
    REQUIRE(e && e0);
    mf_enum(e, &en); mf_enum(e0, &en0);
    EXPECT_EQ(en.total, en0.total);                               /* an enumeration is not a drawing call: same as an empty metafile */
    U(DeleteEnhMetaFile)(e0);
    U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

/* ---- device caps -------------------------------------------------------------------------------------------------- */
TEST(gdi_metafile_print_neg_nt_getdevicecaps_bad_dc_fails_6) {
    NEED_NT(NtGdiGetDeviceCaps); MF_SCREEN(ref);
    HGDIOBJ br = U(CreateSolidBrush)(1);
    HDC bad[] = { NULL, (HDC)(ULONG_PTR)0x1234, (HDC)br };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(0);
        EXPECT_EQ(UN(NtGdiGetDeviceCaps)(bad[i], HORZRES), 0);
        EXPECT_ERR(ERROR_INVALID_HANDLE);                         /* DC lock failure -> EngSetLastError(6) */
    }
    U(DeleteObject)(br);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_nt_getdevicecapsall_bad_dc_fails_6) {
    NEED_NT(NtGdiGetDeviceCapsAll); MF_SCREEN(ref);
    BYTE caps[1024]; memset(caps, 0xcc, sizeof caps);
    HDC bad[] = { NULL, (HDC)(ULONG_PTR)0x1234 };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(0);
        CHECK(!UN(NtGdiGetDeviceCapsAll)(bad[i], caps));
        EXPECT_ERR(ERROR_INVALID_HANDLE);
    }
    CHECK(caps[0] == 0xcc);                                       /* untouched */
    mf_screen_free(ref);
}

/* gdi32!GetDeviceCaps reads these slots of the DEVCAPS block cached in the LDC (ldc+0x90): each GetDeviceCaps index below
 * maps to a fixed dword of what NtGdiGetDeviceCapsAll returned. */
static const struct { int idx; int slot; const char *name; } CAPSLOT[] = {
    { 0, 0, "DRIVERVERSION" }, { TECHNOLOGY, 1, "TECHNOLOGY" }, { BITSPIXEL, 8, "BITSPIXEL" }, { PLANES, 9, "PLANES" },
    { NUMPENS, 10, "NUMPENS" }, { NUMFONTS, 11, "NUMFONTS" }, { NUMCOLORS, 12, "NUMCOLORS" }, { RASTERCAPS, 13, "RASTERCAPS" },
    { TEXTCAPS, 0x19, "TEXTCAPS" }, { SIZEPALETTE, 0x13, "SIZEPALETTE" }, { COLORRES, 0x14, "COLORRES" },
    { PHYSICALWIDTH, 0x15, "PHYSICALWIDTH" }, { PHYSICALHEIGHT, 0x16, "PHYSICALHEIGHT" }, { PHYSICALOFFSETX, 0x17, "PHYSICALOFFSETX" },
    { PHYSICALOFFSETY, 0x18, "PHYSICALOFFSETY" }, { VREFRESH, 0x1a, "VREFRESH" }, { DESKTOPHORZRES, 0x1b, "DESKTOPHORZRES" },
    { DESKTOPVERTRES, 0x1c, "DESKTOPVERTRES" }, { BLTALIGNMENT, 0x1d, "BLTALIGNMENT" }, { SHADEBLENDCAPS, 0x22, "SHADEBLENDCAPS" },
    { COLORMGMTCAPS, 0x23, "COLORMGMTCAPS" },
};

TEST(gdi_metafile_print_neg_nt_getdevicecapsall_matches_cached_getdevicecaps_slots) {
    NEED_NT(NtGdiGetDeviceCapsAll); MF_SCREEN(ref);
    HDC m = U(CreateEnhMetaFileW)(ref, NULL, NULL, NULL);       /* LDC path: gdi32!GetDeviceCaps serves from the cached block */
    DWORD caps[256]; memset(caps, 0, sizeof caps);
    REQUIRE(m);
    CHECK(UN(NtGdiGetDeviceCapsAll)(m, caps));
    for (unsigned i = 0; i < sizeof CAPSLOT / sizeof CAPSLOT[0]; i++) {
        int viaApi = U(GetDeviceCaps)(m, CAPSLOT[i].idx);
        CHECKF((int)caps[CAPSLOT[i].slot] == viaApi, "%s: slot %d = %d, GetDeviceCaps = %d", CAPSLOT[i].name, CAPSLOT[i].slot, (int)caps[CAPSLOT[i].slot], viaApi);
    }
    HENHMETAFILE e = U(CloseEnhMetaFile)(m);
    if (e) U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_nt_getdevicecaps_kernel_matches_api_for_direct_indices) {
    NEED_NT(NtGdiGetDeviceCaps); MF_SCREEN(ref);
    static const int idx[] = { HORZSIZE, VERTSIZE, HORZRES, VERTRES, ASPECTX, ASPECTY, ASPECTXY, LOGPIXELSX, LOGPIXELSY };
    HDC mem = U(CreateCompatibleDC)(ref);
    REQUIRE(mem);
    for (unsigned i = 0; i < sizeof idx / sizeof idx[0]; i++) {
        EXPECT_EQ(UN(NtGdiGetDeviceCaps)(ref, idx[i]), U(GetDeviceCaps)(ref, idx[i]));    /* these indices always go to the kernel */
        EXPECT_EQ(UN(NtGdiGetDeviceCaps)(mem, idx[i]), U(GetDeviceCaps)(mem, idx[i]));
    }
    U(DeleteDC)(mem);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_nt_getdevicecaps_kernel_matches_cache_for_other_indices) {
    NEED_NT(NtGdiGetDeviceCaps); MF_SCREEN(ref);
    static const int idx[] = { 0, TECHNOLOGY, BITSPIXEL, PLANES, NUMPENS, NUMFONTS, RASTERCAPS, TEXTCAPS, SIZEPALETTE, COLORRES };
    HDC m = U(CreateEnhMetaFileW)(ref, NULL, NULL, NULL);
    REQUIRE(m);
    for (unsigned i = 0; i < sizeof idx / sizeof idx[0]; i++) {
        int k = UN(NtGdiGetDeviceCaps)(m, idx[i]), c = U(GetDeviceCaps)(m, idx[i]);
        if (k != c) t_note("index %d: kernel %d, cached %d", idx[i], k, c);
    }
    HENHMETAFILE e = U(CloseEnhMetaFile)(m);
    if (e) U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_getdevicecaps_hardcoded_indices_on_ldc_dc) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    HDC m = U(CreateEnhMetaFileW)(ref, NULL, NULL, NULL);
    REQUIRE(m);
    EXPECT_EQ(U(GetDeviceCaps)(m, NUMBRUSHES), -1);              /* gdi32!GetDeviceCaps case 0x10 */
    EXPECT_EQ(U(GetDeviceCaps)(m, CLIPCAPS), 1);                 /* case 0x24 */
    EXPECT_EQ(U(GetDeviceCaps)(m, CURVECAPS), 0x1ff);            /* case 0x1c */
    EXPECT_EQ(U(GetDeviceCaps)(m, LINECAPS), 0xfe);              /* case 0x1e */
    EXPECT_EQ(U(GetDeviceCaps)(m, POLYGONALCAPS), 0xff);         /* case 0x20 */
    EXPECT_EQ(U(GetDeviceCaps)(m, NUMRESERVED), 20);             /* case 0x6a */
    HENHMETAFILE e = U(CloseEnhMetaFile)(m);
    if (e) U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_getdevicecaps_unlisted_indices_are_zero_on_ldc_dc) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    HDC m = U(CreateEnhMetaFileW)(ref, NULL, NULL, NULL);
    /* indices with no case in the switch (and not forwarded to the kernel) fall to the default arm: 0 */
    static const int idx[] = { 1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 20, 21, 23, 25, 26, 27, 29, 31, 33, 35, 37, 39, 41, 43, 45,
                               89, 91, 100, 101, 102, 103, 105, 107, 109, 115, 122, 123, 200, 1000, 0x7fffffff, -1 };
    REQUIRE(m);
    for (unsigned i = 0; i < sizeof idx / sizeof idx[0]; i++) {
        int v = U(GetDeviceCaps)(m, idx[i]);
        CHECKF(v == 0, "GetDeviceCaps(meta, %d) = %d", idx[i], v);
    }
    HENHMETAFILE e = U(CloseEnhMetaFile)(m);
    if (e) U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_getdevicecaps_bad_handle_fails) {
    MF_SCREEN(ref);
    HGDIOBJ br = U(CreateSolidBrush)(1);
    SetLastError(0);
    EXPECT_EQ(U(GetDeviceCaps)(NULL, HORZRES), 0);
    EXPECT_ERR(ERROR_INVALID_HANDLE);                             /* pldcGet == 0 -> error 6 */
    SetLastError(0);
    EXPECT_EQ(U(GetDeviceCaps)((HDC)br, HORZRES), 0);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    SetLastError(0);
    EXPECT_EQ(U(GetDeviceCaps)((HDC)(ULONG_PTR)0x00210007, HORZRES), 0);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    U(DeleteObject)(br);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_getdevicecaps_metadc16_type_special_cases) {
    MF_NEED_EMF();
    /* gdi32!GetDeviceCaps: a 0x66 (16-bit metafile DC) handle answers TECHNOLOGY with 5 (DT_METAFILE), everything else 0 */
    HDC fake = (HDC)(ULONG_PTR)0x00660001;
    EXPECT_EQ(U(GetDeviceCaps)(fake, TECHNOLOGY), DT_METAFILE);
    EXPECT_EQ(U(GetDeviceCaps)(fake, HORZRES), 0);
    EXPECT_EQ(U(GetDeviceCaps)(fake, BITSPIXEL), 0);
    EXPECT_EQ(U(GetDeviceCaps)(fake, 9999), 0);
}

/* ---- NtGdiConvertMetafileRect / NtGdiSetMetaRgn ------------------------------------------------------------------- */
TEST(gdi_metafile_print_neg_nt_convertmetafilerect_identity_memory_dc) {
    NEED_NT(NtGdiConvertMetafileRect); MF_SCREEN(ref);
    HDC mem = U(CreateCompatibleDC)(ref);
    RECTL r = { 10, 10, 50, 40 };
    REQUIRE(mem);
    int rc = UN(NtGdiConvertMetafileRect)(mem, &r);
    EXPECT_EQ(rc, 2);
    EXPECT_EQ(r.left, 10);
    EXPECT_EQ(r.top, 10);
    CHECK(r.right == 49 || r.right == 50);
    CHECK(r.bottom == 39 || r.bottom == 40);
    t_note("ConvertMetafileRect {10,10,50,40} -> {%ld,%ld,%ld,%ld}", r.left, r.top, r.right, r.bottom);
    U(DeleteDC)(mem);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_nt_convertmetafilerect_degenerate_returns_1) {
    NEED_NT(NtGdiConvertMetafileRect); MF_SCREEN(ref);
    HDC mem = U(CreateCompatibleDC)(ref);
    static const RECTL in[] = { {10, 10, 10, 10}, {10, 10, 10, 40}, {10, 10, 50, 10} };
    REQUIRE(mem);
    for (unsigned i = 0; i < sizeof in / sizeof in[0]; i++) {
        RECTL r = in[i];
        EXPECT_EQ(UN(NtGdiConvertMetafileRect)(mem, &r), 1);      /* right-1 < left or bottom-1 < top */
    }
    U(DeleteDC)(mem);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_nt_convertmetafilerect_reversed_corners_are_sorted) {
    NEED_NT(NtGdiConvertMetafileRect); MF_SCREEN(ref);
    HDC mem = U(CreateCompatibleDC)(ref);
    RECTL a = { 10, 10, 50, 40 }, b = { 50, 40, 10, 10 };
    REQUIRE(mem);
    EXPECT_EQ(UN(NtGdiConvertMetafileRect)(mem, &a), 2);
    EXPECT_EQ(UN(NtGdiConvertMetafileRect)(mem, &b), 2);
    EXPECT_EQ(a.left, b.left); EXPECT_EQ(a.top, b.top); EXPECT_EQ(a.right, b.right); EXPECT_EQ(a.bottom, b.bottom);
    U(DeleteDC)(mem);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_nt_convertmetafilerect_bad_dc_fails_87) {
    NEED_NT(NtGdiConvertMetafileRect); MF_SCREEN(ref);
    HDC bad[] = { NULL, (HDC)(ULONG_PTR)0x1234 };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        RECTL r = { 10, 10, 50, 40 };
        SetLastError(0);
        EXPECT_EQ(UN(NtGdiConvertMetafileRect)(bad[i], &r), 0);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
        EXPECT_EQ(r.right, 50);                                   /* rectangle untouched on failure */
    }
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_nt_setmetargn_bad_dc_fails_87) {
    NEED_NT(NtGdiSetMetaRgn); MF_SCREEN(ref);
    HDC bad[] = { NULL, (HDC)(ULONG_PTR)0x1234 };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(0);
        EXPECT_EQ(UN(NtGdiSetMetaRgn)(bad[i]), 0);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);                      /* XDCOBJ::vLock fails -> EngSetLastError(0x57) */
    }
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_setmetargn_clip_cannot_grow_beyond_meta_region) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    REQUIRE(U(SetMetaRgn) && U(IntersectClipRect) && U(GetClipBox));
    MF_NEED_DIB(d, 64, 64, 0);
    RECT box;
    /* IntersectClipRect reports COMPLEXREGION on Windows 10 even though the resulting clip is one rectangle (GetClipBox says SIMPLEREGION) */
    EXPECT_EQ(U(IntersectClipRect)(d.dc, 0, 0, 10, 10), COMPLEXREGION);
    EXPECT_EQ(U(SetMetaRgn)(d.dc), SIMPLEREGION);                 /* the current clip becomes the meta region */
    U(SelectClipRgn)(d.dc, NULL);                                  /* drops only the app clip */
    EXPECT_EQ(U(GetClipBox)(d.dc, &box), SIMPLEREGION);
    EXPECT_EQ(box.left, 0); EXPECT_EQ(box.top, 0); EXPECT_EQ(box.right, 10); EXPECT_EQ(box.bottom, 10);
    EXPECT_EQ(U(IntersectClipRect)(d.dc, 0, 0, 64, 64), COMPLEXREGION);
    U(GetClipBox)(d.dc, &box);
    EXPECT_EQ(box.right, 10); EXPECT_EQ(box.bottom, 10);          /* cannot be widened */
    mf_dib_close(&d);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_setmetargn_on_metafile_dc_is_recorded) {
    MF_NEED_EMF(); MF_SCREEN(ref);
    REQUIRE(U(SetMetaRgn) && U(IntersectClipRect));
    HDC m = U(CreateEnhMetaFileW)(ref, NULL, NULL, NULL);
    struct mf_enum en;
    REQUIRE(m);
    U(IntersectClipRect)(m, 0, 0, 20, 20);
    U(SetMetaRgn)(m);
    HENHMETAFILE e = U(CloseEnhMetaFile)(m);
    REQUIRE(e);
    mf_walk(e, &en);
    EXPECT_EQ(en.counts[EMR_SETMETARGN], 1);
    EXPECT_EQ(en.counts[EMR_INTERSECTCLIPRECT], 1);
    U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

/* ---- server metafile objects ------------------------------------------------------------------------------------- */
#define MF_EX 0x5845464du   /* 'MFEX' */
#define MF_PP 0x5f50464du   /* 'MFP_' */

static HANDLE srv_make(DWORD type, ULONG cb, const BYTE *src, DWORD mm, DWORD x, DWORD y) {
    BYTE *buf = (BYTE *)_aligned_malloc(cb ? cb : 4, 8);
    HANDLE h;
    if (!buf) return NULL;
    memcpy(buf, src, cb);
    h = UN(NtGdiCreateServerMetaFile)(type, cb, buf, mm, x, y);
    _aligned_free(buf);
    return h;
}
static void pattern(BYTE *p, size_t n) { for (size_t i = 0; i < n; i++) p[i] = (BYTE)(i * 7 + 3); }

TEST(gdi_metafile_print_neg_nt_servermetafile_roundtrip) {
    NEED_NT(NtGdiCreateServerMetaFile); NEED_NT(NtGdiGetServerMetaFileBits);
    BYTE src[100] AL8, out[100] AL8;
    DWORD ty = 0, mm = 0, x = 0, y = 0;
    pattern(src, sizeof src);
    HANDLE h = srv_make(MF_EX, sizeof src, src, 8, 1234, 5678);
    CHECK(h != NULL);
    if (!h) return;
    t_note("server metafile handle %p type7=0x%x", h, MF_TYPE7(h));
    EXPECT_EQ(UN(NtGdiGetServerMetaFileBits)(h, 0, NULL, NULL, NULL, NULL, NULL), sizeof src);   /* size query touches no pointer */
    memset(out, 0, sizeof out);
    EXPECT_EQ(UN(NtGdiGetServerMetaFileBits)(h, sizeof src, out, &ty, &mm, &x, &y), sizeof src);
    CHECK(memcmp(src, out, sizeof src) == 0);
    EXPECT_EQ(ty, MF_EX);
    EXPECT_EQ(mm, 8);
    EXPECT_EQ(x, 1234);
    EXPECT_EQ(y, 5678);
    if (UN(NtGdiDeleteObjectApp)) t_note("delete server metafile via NtGdiDeleteObjectApp: %d", (int)UN(NtGdiDeleteObjectApp)(h));
}

TEST(gdi_metafile_print_neg_nt_servermetafile_mfp_type_accepted) {
    NEED_NT(NtGdiCreateServerMetaFile); NEED_NT(NtGdiGetServerMetaFileBits);
    BYTE src[24] AL8, out[24] AL8;
    DWORD ty = 0, mm = 0, x = 0, y = 0;
    pattern(src, sizeof src);
    HANDLE h = srv_make(MF_PP, sizeof src, src, 1, 2, 3);
    CHECK(h != NULL);
    if (!h) return;
    EXPECT_EQ(UN(NtGdiGetServerMetaFileBits)(h, sizeof src, out, &ty, &mm, &x, &y), sizeof src);
    EXPECT_EQ(ty, MF_PP);
    EXPECT_EQ(mm, 1); EXPECT_EQ(x, 2); EXPECT_EQ(y, 3);
    if (UN(NtGdiDeleteObjectApp)) UN(NtGdiDeleteObjectApp)(h);
}

TEST(gdi_metafile_print_neg_nt_servermetafile_create_rejects_bad_args) {
    NEED_NT(NtGdiCreateServerMetaFile);
    BYTE src[32]; pattern(src, sizeof src);
    static const DWORD badty[] = { 0, 1, MF_EX + 1, MF_PP - 1, 0x46464d45, 0xffffffff };
    for (unsigned i = 0; i < sizeof badty / sizeof badty[0]; i++)
        CHECK(srv_make(badty[i], sizeof src, src, 0, 0, 0) == NULL);            /* only 'MFEX' / 'MFP_' */
    CHECK(UN(NtGdiCreateServerMetaFile)(MF_EX, sizeof src, NULL, 0, 0, 0) == NULL);          /* bits == NULL */
    CHECK(UN(NtGdiCreateServerMetaFile)(MF_EX, 0x2710000, src, 0, 0, 0) == NULL);            /* cb + 0x30 >= 0x2710001 */
    CHECK(UN(NtGdiCreateServerMetaFile)(MF_EX, 0xffffffd0, src, 0, 0, 0) == NULL);           /* cb >= 0xffffffd0 */
    CHECK(UN(NtGdiCreateServerMetaFile)(MF_EX, 0xffffffff, src, 0, 0, 0) == NULL);
}

TEST(gdi_metafile_print_neg_nt_servermetafile_getbits_size_must_match_exactly) {
    NEED_NT(NtGdiCreateServerMetaFile); NEED_NT(NtGdiGetServerMetaFileBits);
    BYTE src[64] AL8, out[128] AL8;
    DWORD ty = 0xabc, mm = 0xabc, x = 0xabc, y = 0xabc;
    pattern(src, sizeof src);
    HANDLE h = srv_make(MF_EX, sizeof src, src, 4, 5, 6);
    REQUIRE(h);
    memset(out, 0xcc, sizeof out);
    EXPECT_EQ(UN(NtGdiGetServerMetaFileBits)(h, sizeof src - 4, out, &ty, &mm, &x, &y), 0);   /* cb != stored size */
    EXPECT_EQ(UN(NtGdiGetServerMetaFileBits)(h, sizeof src + 4, out, &ty, &mm, &x, &y), 0);
    CHECK(out[0] == 0xcc);
    EXPECT_EQ(ty, 0xabc); EXPECT_EQ(mm, 0xabc); EXPECT_EQ(x, 0xabc); EXPECT_EQ(y, 0xabc);   /* nothing written on mismatch */
    if (UN(NtGdiDeleteObjectApp)) UN(NtGdiDeleteObjectApp)(h);
}

TEST(gdi_metafile_print_neg_nt_servermetafile_getbits_bad_handles_return_zero) {
    NEED_NT(NtGdiGetServerMetaFileBits); MF_SCREEN(ref);
    BYTE out[16] AL8; DWORD ty = 0xabc;
    HGDIOBJ br = U(CreateSolidBrush)(1);
    HANDLE bad[] = { NULL, (HANDLE)(ULONG_PTR)0x1234, (HANDLE)ref, (HANDLE)br, (HANDLE)U(GetStockObject)(BLACK_PEN) };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        EXPECT_EQ(UN(NtGdiGetServerMetaFileBits)(bad[i], 0, NULL, NULL, NULL, NULL, NULL), 0);   /* locking the handle as a metafile object fails */
        EXPECT_EQ(UN(NtGdiGetServerMetaFileBits)(bad[i], 16, out, &ty, &ty, &ty, &ty), 0);
    }
    EXPECT_EQ(ty, 0xabc);
    U(DeleteObject)(br);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_nt_servermetafile_from_user_emf_carries_same_bits) {
    NEED_NT(NtGdiGetServerMetaFileBits); MF_SCREEN(ref);
    if (!UN(GdiConvertEnhMetaFile)) { t_skip("GdiConvertEnhMetaFile not exported"); mf_screen_free(ref); return; }
    HENHMETAFILE e = mf_make(ref, NULL, NULL, 2);
    REQUIRE(e);
    UINT n = U(GetEnhMetaFileBits)(e, 0, NULL);
    HANDLE s = UN(GdiConvertEnhMetaFile)(e);                      /* NtGdiCreateServerMetaFile('MFEX', nBytes, bits, 0, 0, 0) */
    CHECK(s != NULL);
    if (s) {
        DWORD ty = 0, mm = 9, x = 9, y = 9;
        BYTE *a = (BYTE *)malloc(n), *b = (BYTE *)malloc(n);
        EXPECT_EQ(UN(NtGdiGetServerMetaFileBits)(s, 0, NULL, NULL, NULL, NULL, NULL), n);
        if (a && b) {
            EXPECT_EQ(U(GetEnhMetaFileBits)(e, n, a), n);
            EXPECT_EQ(UN(NtGdiGetServerMetaFileBits)(s, n, b, &ty, &mm, &x, &y), n);
            CHECK(memcmp(a, b, n) == 0);
        }
        EXPECT_EQ(ty, MF_EX); EXPECT_EQ(mm, 0); EXPECT_EQ(x, 0); EXPECT_EQ(y, 0);
        free(a); free(b);
        if (UN(NtGdiDeleteObjectApp)) UN(NtGdiDeleteObjectApp)(s);
    }
    U(DeleteEnhMetaFile)(e);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_nt_servermetafile_handle_in_gdi_table) {
    NEED_NT(NtGdiCreateServerMetaFile);
    BYTE src[16]; pattern(src, sizeof src);
    HANDLE h = srv_make(MF_EX, sizeof src, src, 0, 0, 0);
    REQUIRE(h);
    struct mf_cell c;
    if (!t_is_wine() && mf_cell_of(h, &c)) {
        t_info("server metafile cell: objt=%u upper=0x%x owner=0x%x", c.objt, c.upper, c.owner);
        EXPECT_EQ(c.upper, (unsigned)(((ULONG_PTR)h >> 16) & 0xffff));
        EXPECT_EQ(c.owner & ~1u, 0);                         /* a server metafile cell has no owning process */
        CHECK(!MF_STOCKED(h));
    }
    if (UN(NtGdiDeleteObjectApp)) UN(NtGdiDeleteObjectApp)(h);
}

/* ---- spool stubs --------------------------------------------------------------------------------------------------- */
TEST(gdi_metafile_print_neg_nt_initspool_not_supported) {
    NEED_NT(NtGdiInitSpool);
    SetLastError(0);
    EXPECT_EQ(UN(NtGdiInitSpool)(), 0);
    EXPECT_ERR(ERROR_NOT_SUPPORTED);          /* shared body with EngEnumForms/EngGetForm/...: EngSetLastError(0x32); return 0 */
}

TEST(gdi_metafile_print_neg_nt_getspoolmessage_returns_zero) {
    NEED_NT(NtGdiGetSpoolMessage);
    SetLastError(0xbeef);
    EXPECT_EQ(UN(NtGdiGetSpoolMessage)(0, 0, 0, 0), 0);          /* body is `xor eax,eax; ret` */
    EXPECT_EQ(GetLastError(), 0xbeef);
}

TEST(gdi_metafile_print_neg_nt_unloadprinterdriver_returns_zero) {
    NEED_NT(NtGdiUnloadPrinterDriver);
    SetLastError(0xbeef);
    EXPECT_EQ(UN(NtGdiUnloadPrinterDriver)(0, 0), 0);
    EXPECT_EQ(GetLastError(), 0xbeef);
}

TEST(gdi_metafile_print_neg_nt_queryfontassocinfo_global_status) {
    NEED_NT(NtGdiQueryFontAssocInfo);
    UINT a = UN(NtGdiQueryFontAssocInfo)(NULL), b = UN(NtGdiQueryFontAssocInfo)(NULL);    /* hdc == 0 -> returns fFontAssocStatus */
    EXPECT_EQ(a, b);
    t_note("fFontAssocStatus = 0x%x", a);
    CHECK((a & ~7u) == 0);
}

TEST(gdi_metafile_print_neg_nt_queryfontassocinfo_dc_result_range) {
    NEED_NT(NtGdiQueryFontAssocInfo); MF_SCREEN(ref);
    HDC mem = U(CreateCompatibleDC)(ref);
    REQUIRE(mem);
    UINT r = UN(NtGdiQueryFontAssocInfo)(mem);
    CHECK(r <= 2);                                                /* 0 / 1 / 2 from the RFONT probe */
    r = UN(NtGdiQueryFontAssocInfo)(ref);
    CHECK(r <= 2);
    U(DeleteDC)(mem);
    mf_screen_free(ref);
}

TEST(gdi_metafile_print_neg_nt_queryfontassocinfo_bad_dc_fails_6) {
    NEED_NT(NtGdiQueryFontAssocInfo);
    SetLastError(0);
    EXPECT_EQ(UN(NtGdiQueryFontAssocInfo)((HDC)(ULONG_PTR)0x1234), 0);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
}

/* ---- stub presence and service numbers ----------------------------------------------------------------------------- */
static const struct { const char *name; long num; } SCOPE[] = {
    { "NtGdiSetMetaRgn", 4311 }, { "NtGdiQueryFontAssocInfo", 4328 }, { "NtGdiExtEscape", 4361 }, { "NtGdiAbortDoc", 4475 },
    { "NtGdiConvertMetafileRect", 4501 }, { "NtGdiCreateMetafileDC", 4507 }, { "NtGdiCreateServerMetaFile", 4511 },
    { "NtGdiDescribePixelFormat", 4714 }, { "NtGdiDrawEscape", 4718 }, { "NtGdiEndDoc", 4722 }, { "NtGdiEndPage", 4724 },
    { "NtGdiEnumObjects", 4757 }, { "NtGdiGetDeviceCaps", 4787 }, { "NtGdiGetDeviceCapsAll", 4788 },
    { "NtGdiGetServerMetaFileBits", 4820 }, { "NtGdiGetSpoolMessage", 4821 }, { "NtGdiInitSpool", 4835 },
    { "NtGdiSetPixelFormat", 4885 }, { "NtGdiStartDoc", 4892 }, { "NtGdiStartPage", 4893 }, { "NtGdiSwapBuffers", 4896 },
    { "NtGdiUnloadPrinterDriver", 4899 },
};

TEST(gdi_metafile_print_neg_stub_service_numbers_and_shape) {
    HMODULE w = t_module("win32u.dll");
    REQUIRE(w);
    for (unsigned i = 0; i < sizeof SCOPE / sizeof SCOPE[0]; i++) {
        FARPROC p = GetProcAddress(w, SCOPE[i].name);
        if (!p) { CHECKF(t_is_wine(), "%s not exported by win32u.dll", SCOPE[i].name); continue; }
        if (t_is_wine()) { t_note("%s present (Wine numbering differs)", SCOPE[i].name); continue; }
        CHECKF(k_stub_valid(p), "%s: prologue is not the standard syscall stub", SCOPE[i].name);
        EXPECT_EQ(k_stub_number(p), SCOPE[i].num);
        CHECK(k_stub_number(p) >= 0x1000);
    }
}

TEST(gdi_metafile_print_neg_stub_service_numbers_unique) {
    HMODULE w = t_module("win32u.dll");
    REQUIRE(w);
    long nums[sizeof SCOPE / sizeof SCOPE[0]]; int n = 0;
    if (t_is_wine()) { t_skip("Wine numbering"); return; }
    for (unsigned i = 0; i < sizeof SCOPE / sizeof SCOPE[0]; i++) { FARPROC p = GetProcAddress(w, SCOPE[i].name); if (p) nums[n++] = k_stub_number(p); }
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) CHECKF(nums[i] != nums[j], "duplicate service number %ld", nums[i]);
}
