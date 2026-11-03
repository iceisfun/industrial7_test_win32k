/* gap_03 / gdi: GDI syscalls no other area reaches directly.
 *
 * NtGdiGetMiterLimit(hdc, float*)        GetMiterLimit: DC lock failure -> EngSetLastError(0x57), FALSE.
 *                                        Success copies DC+0xe0 (the miter limit, default 10.0f) to *out.
 * NtGdiGetRasterizerCaps(buf, cb)        returns 0 for NULL buf or cb==0; else writes min(cb,6) bytes of
 *                                        { nSize=6, wFlags=(gcTrueTypeFonts!=0)+2, nLanguageID } and returns 1.
 * NtGdiGetDhpdev(hdev)                   ValidUmpdHdev walks gppdevList comparing the pointer; unknown -> 0.
 * NtGdiScaleRgn(hdc, hrgn)               ScaleRgn: both objects must lock; TRUE; scales only if bDpiScaleTransform.
 * NtGdiScaleValues(hdc, long*, n)        n<<2 >= 2^32 -> 0 before any access; bad DC -> 0 with buffer not written back.
 * NtGdiPolyDraw(hdc, pts, types, n)      n >= 0x20000000 -> 0 before any access; bad DC -> FALSE.
 * NtGdiPolyTextOutW(hdc, POLYTEXTW*, n)  0x38-byte POLYTEXTW entries.
 * NtGdiGetCharacterPlacementW            NULL string / negative count / nMaxExtent < -1 -> 0; NULL results -> packed extent.
 * NtGdiMakeFontDir                       cjFontDir must be > 0xfa and cjPath <= 0x20a else 0, output untouched.
 * NtGdiCheckBitmapBits                   param4 == 2 && param6 == 1 && cbBuf == ((n*3+3)&~3) else 0/ERROR_INVALID_PARAMETER;
 *                                        n*3 overflowing 32 bits -> ERROR_ARITHMETIC_OVERFLOW (0x216).
 */
#include "gap3.h"

static HDC fresh_dc(void) { HDC scr = U(GetDC)(NULL); HDC d = U(CreateCompatibleDC)(scr); U(ReleaseDC)(NULL, scr); return d; }
static DWORD fbits(float f) { DWORD d; memcpy(&d, &f, 4); return d; }

/* ---- stubs ------------------------------------------------------------- */

static const struct { const char *name; long number; } STUBS[] = {
    {"NtGdiGetCharacterPlacementW", 4782}, {"NtGdiGetDhpdev", 4791}, {"NtGdiMakeFontDir", 4836},
    {"NtGdiPolyTextOutW", 4852}, {"NtDCompositionGetFrameStatistics", 4404}, {"NtUserCreateDesktopEx", 5051},
    {"NtUserSetWindowCompositionTransition", 5365}, {"NtUserTrackMouseEvent", 4302}, {"NtGdiCheckBitmapBits", 4496},
    {"NtGdiDdDDISetProcessDeviceRemovalSupport", 4676}, {"NtGdiGetMiterLimit", 4807}, {"NtGdiGetRasterizerCaps", 4314},
    {"NtGdiPolyDraw", 4851}, {"NtGdiScaleRgn", 4866}, {"NtGdiScaleValues", 4867}, {"NtUserGetAltTabInfo", 4326},
    {"NtUserGetWindowCompositionAttribute", 5189}, {"NtUserSBGetParms", 4174}, {"NtUserSetWindowCompositionAttribute", 5364},
    {"NtUserTranslateAccelerator", 4113}, {"NtUserCreateWindowStation", 5056}, {"NtUserFlashWindowEx", 5103},
    {"NtUserInheritWindowMonitor", 5206}, {"NtUserInitializePointerDeviceInjection", 5212}, {"NtUserInjectPointerInput", 5220},
};

TEST(gap3_stub_exports_present_and_numbered) {
    if (t_is_wine()) { t_skip("win32u stubs are Wine's"); return; }
    for (unsigned i = 0; i < sizeof STUBS / sizeof *STUBS; i++) {
        FARPROC p = t_proc_in("win32u.dll", STUBS[i].name);
        CHECKF(p != NULL, "%s exported", STUBS[i].name);
        if (!p) continue;
        CHECKF(k_stub_valid(p), "%s prologue", STUBS[i].name);
        CHECKF(k_stub_number(p) == STUBS[i].number, "%s number %ld want %ld", STUBS[i].name, k_stub_number(p), STUBS[i].number);
    }
}

TEST(gap3_stub_numbers_in_win32k_range) {
    if (t_is_wine()) { t_skip("win32u stubs are Wine's"); return; }
    for (unsigned i = 0; i < sizeof STUBS / sizeof *STUBS; i++) {
        long n = k_stub_number(t_proc_in("win32u.dll", STUBS[i].name));
        CHECKF(n >= 0x1000 && n < 0x1600, "%s service number 0x%lx outside win32k table", STUBS[i].name, n);
    }
}

/* ---- NtGdiGetMiterLimit ------------------------------------------------ */

TEST(gap3_miter_invalid_hdc_fails_with_invalid_parameter) {
    BOOL (WINAPI *get)(HDC, DWORD *) = (void *)UNX(NtGdiGetMiterLimit);
    REQUIRE(get);
    static const ULONG_PTR bad[] = { 0, 1, 0x1234, 0xdeadbeef, 0x00010000, (ULONG_PTR)-1, 0x7fffffff };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        DWORD out = 0xCCCCCCCC;
        PLANT();
        BOOL r = get((HDC)bad[i], &out);
        CHECKF(r == 0, "hdc 0x%llx r=%d", (unsigned long long)bad[i], r);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
        EXPECT_EQ(out, 0xCCCCCCCC);
    }
}

TEST(gap3_miter_wrong_object_types_rejected) {
    BOOL (WINAPI *get)(HDC, DWORD *) = (void *)UNX(NtGdiGetMiterLimit);
    REQUIRE(get);
    HRGN rgn = U(CreateRectRgn)(0, 0, 4, 4);
    HBRUSH br = U(CreateSolidBrush)(RGB(1, 2, 3));
    HPEN pen = U(CreatePen)(PS_SOLID, 1, 0);
    HGDIOBJ objs[] = { rgn, br, pen, U(GetStockObject)(BLACK_PEN) };
    for (unsigned i = 0; i < 4; i++) {
        DWORD out = 0xCCCCCCCC;
        PLANT();
        /* DCOBJ::DCOBJ fails for a handle whose type is not DC. */
        EXPECT_EQ(get((HDC)objs[i], &out), 0);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
        EXPECT_EQ(out, 0xCCCCCCCC);
    }
    U(DeleteObject)(rgn); U(DeleteObject)(br); U(DeleteObject)(pen);
}

TEST(gap3_miter_default_is_ten) {
    BOOL (WINAPI *get)(HDC, DWORD *) = (void *)UNX(NtGdiGetMiterLimit);
    REQUIRE(get);
    HDC dc = fresh_dc();
    CHECK(dc != NULL);
    DWORD out = 0;
    EXPECT_EQ(get(dc, &out), 1);          /* returns TRUE once the DC locks */
    EXPECT_EQ(out, fbits(10.0f));         /* DC+0xe0 initialised to the documented default of 10.0 */
    U(DeleteDC)(dc);
}

TEST(gap3_miter_roundtrip_values) {
    BOOL (WINAPI *get)(HDC, DWORD *) = (void *)UNX(NtGdiGetMiterLimit);
    REQUIRE(get);
    static const float vals[] = { 1.0f, 1.5f, 2.5f, 7.25f, 10.0f, 33.0f, 100.0f, 1000.5f };
    HDC dc = fresh_dc();
    for (unsigned i = 0; i < sizeof vals / sizeof *vals; i++) {
        float old = 0;
        CHECK(U(SetMiterLimit)(dc, vals[i], &old));
        DWORD out = 0;
        EXPECT_EQ(get(dc, &out), 1);
        EXPECT_EQ(out, fbits(vals[i]));
    }
    U(DeleteDC)(dc);
}

TEST(gap3_miter_set_returns_previous) {
    BOOL (WINAPI *get)(HDC, DWORD *) = (void *)UNX(NtGdiGetMiterLimit);
    REQUIRE(get);
    HDC dc = fresh_dc();
    float old = 0;
    CHECK(U(SetMiterLimit)(dc, 4.0f, &old));
    EXPECT_EQ(fbits(old), fbits(10.0f));
    CHECK(U(SetMiterLimit)(dc, 6.0f, &old));
    EXPECT_EQ(fbits(old), fbits(4.0f));
    DWORD out = 0; get(dc, &out);
    EXPECT_EQ(out, fbits(6.0f));
    U(DeleteDC)(dc);
}

TEST(gap3_miter_is_per_dc) {
    BOOL (WINAPI *get)(HDC, DWORD *) = (void *)UNX(NtGdiGetMiterLimit);
    REQUIRE(get);
    HDC a = fresh_dc(), b = fresh_dc();
    float old;
    U(SetMiterLimit)(a, 3.0f, &old);
    DWORD oa = 0, ob = 0;
    get(a, &oa); get(b, &ob);
    EXPECT_EQ(oa, fbits(3.0f));
    EXPECT_EQ(ob, fbits(10.0f));
    U(DeleteDC)(a); U(DeleteDC)(b);
}

TEST(gap3_miter_gdi32_path_agrees_with_syscall) {
    BOOL (WINAPI *get)(HDC, DWORD *) = (void *)UNX(NtGdiGetMiterLimit);
    REQUIRE(get);
    HDC dc = fresh_dc();
    float old, viaapi = 0;
    U(SetMiterLimit)(dc, 12.5f, &old);
    /* gdi32full!GetMiterLimit is a bare tail jump into the stub: same arguments. */
    CHECK(U(GetMiterLimit)(dc, &viaapi));
    DWORD out = 0; get(dc, &out);
    EXPECT_EQ(fbits(viaapi), out);
    EXPECT_EQ(out, fbits(12.5f));
    U(DeleteDC)(dc);
}

TEST(gap3_miter_deleted_dc_fails) {
    BOOL (WINAPI *get)(HDC, DWORD *) = (void *)UNX(NtGdiGetMiterLimit);
    REQUIRE(get);
    HDC dc = fresh_dc();
    U(DeleteDC)(dc);
    DWORD out = 0xCCCCCCCC;
    PLANT();
    EXPECT_EQ(get(dc, &out), 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    EXPECT_EQ(out, 0xCCCCCCCC);
}

TEST(gap3_miter_stale_uniqueness_rejected) {
    BOOL (WINAPI *get)(HDC, DWORD *) = (void *)UNX(NtGdiGetMiterLimit);
    REQUIRE(get);
    HDC dc = fresh_dc();
    /* Handle = index | (uniqueness << 16): flipping the upper word must not resolve to the DC. */
    HDC forged = (HDC)((ULONG_PTR)dc ^ 0x00100000);
    DWORD out = 0xCCCCCCCC;
    PLANT();
    EXPECT_EQ(get(forged, &out), 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    EXPECT_EQ(out, 0xCCCCCCCC);
    EXPECT_EQ(get(dc, &out), 1);
    U(DeleteDC)(dc);
}

TEST(gap3_miter_dc_handle_cell_is_dc_type) {
    if (t_is_wine()) { t_skip("no win32k GDI table"); return; }
    HDC dc = fresh_dc();
    /* The cell's object-type byte (cell+0x0e) is 1 for a DC; cell+0x0d holds the stale-handle check byte, the top byte of the handle. */
    char *base = NULL; unsigned char ty = 0xff, chk = 0xff;
    k_try_read_ptr((char *)k_peb() + PEB_GdiSharedHandleTable, (void **)&base);
    if (!base) { t_skip("GDI shared handle table not readable"); U(DeleteDC)(dc); return; }
    char *cell = base + (ULONG_PTR)((ULONG_PTR)dc & 0xffff) * GDI_CELL_SIZE;
    CHECK(k_try_read_u8(cell + 0x0e, &ty));
    EXPECT_EQ(ty, 1);
    EXPECT_EQ(k_gdi_type_byte(dc), (unsigned char)((ULONG_PTR)dc >> 24));
    U(DeleteDC)(dc);
}

TEST(gap3_miter_screen_dc) {
    BOOL (WINAPI *get)(HDC, DWORD *) = (void *)UNX(NtGdiGetMiterLimit);
    REQUIRE(get);
    HDC scr = U(GetDC)(NULL);
    DWORD out = 0;
    EXPECT_EQ(get(scr, &out), 1);
    t_note("screen DC miter limit bits 0x%08lx", (unsigned long)out);
    U(ReleaseDC)(NULL, scr);
}

/* ---- NtGdiGetRasterizerCaps -------------------------------------------- */

TEST(gap3_rastcaps_null_or_zero_size_returns_zero) {
    BOOL (WINAPI *rc)(void *, UINT) = (void *)UNX(NtGdiGetRasterizerCaps);
    REQUIRE(rc);
    BYTE buf[16]; memset(buf, 0xCC, sizeof buf);
    PLANT();
    EXPECT_EQ(rc(NULL, 6), 0);
    EXPECT_EQ(rc(NULL, 0), 0);
    EXPECT_EQ(rc(buf, 0), 0);
    EXPECT_EQ(GetLastError(), SENTINEL_ERR); /* none of these paths sets an error */
    for (int i = 0; i < 16; i++) CHECKF(buf[i] == 0xCC, "buf[%d] touched", i);
}

TEST(gap3_rastcaps_full_struct_contents) {
    BOOL (WINAPI *rc)(void *, UINT) = (void *)UNX(NtGdiGetRasterizerCaps);
    REQUIRE(rc);
    RASTERIZER_STATUS st __attribute__((aligned(4))); memset(&st, 0xCC, sizeof st);   /* the kernel rejects a buffer that is not 4-byte aligned */
    EXPECT_EQ(rc(&st, sizeof st), 1);
    EXPECT_EQ(st.nSize, 6);                       /* local_res8 = 6 */
    CHECKF((st.wFlags & ~3) == 0, "wFlags 0x%x", st.wFlags);
    CHECKF(st.wFlags & TT_ENABLED, "TT_ENABLED always set: (gcTrueTypeFonts!=0)+2 is 2 or 3, got 0x%x", st.wFlags);
    t_note("rasterizer wFlags=0x%x nLanguageID=0x%04x", st.wFlags, (unsigned)st.nLanguageID);
}

TEST(gap3_rastcaps_sizes_copy_prefix_only) {
    BOOL (WINAPI *rc)(void *, UINT) = (void *)UNX(NtGdiGetRasterizerCaps);
    REQUIRE(rc);
    /* Reference image from the full-size call. */
    BYTE ref[8] = { 0 };
    DWORD refbuf[4]; memset(refbuf, 0, sizeof refbuf);
    EXPECT_EQ(rc(refbuf, 6), 1);
    memcpy(ref, refbuf, 6);
    for (UINT cb = 1; cb <= 12; cb++) {
        DWORD b[4]; memset(b, 0xCC, sizeof b);   /* DWORD array: 4-byte aligned as the kernel requires */
        BYTE *p = (BYTE *)b;
        EXPECT_EQ(rc(b, cb), 1);
        UINT n = cb > 6 ? 6 : cb;                /* "if (5 < cb) cb = 6" */
        for (UINT i = 0; i < n; i++) CHECKF(p[i] == ref[i], "cb=%u byte %u 0x%02x want 0x%02x", cb, i, p[i], ref[i]);
        for (UINT i = n; i < 16; i++) CHECKF(p[i] == 0xCC, "cb=%u byte %u past copy limit written", cb, i);
    }
}

TEST(gap3_rastcaps_gdi32_wrapper_matches) {
    BOOL (WINAPI *rc)(void *, UINT) = (void *)UNX(NtGdiGetRasterizerCaps);
    REQUIRE(rc);
    RASTERIZER_STATUS a __attribute__((aligned(4))), b __attribute__((aligned(4))); memset(&a, 0, sizeof a); memset(&b, 0xEE, sizeof b);
    EXPECT_EQ(rc(&a, sizeof a), 1);
    CHECK(U(GetRasterizerCaps)(&b, sizeof b));
    CHECK(!memcmp(&a, &b, sizeof a));
}

TEST(gap3_rastcaps_stable_across_calls) {
    BOOL (WINAPI *rc)(void *, UINT) = (void *)UNX(NtGdiGetRasterizerCaps);
    REQUIRE(rc);
    RASTERIZER_STATUS a __attribute__((aligned(4))), b __attribute__((aligned(4)));
    rc(&a, 6); rc(&b, 6);
    CHECK(!memcmp(&a, &b, 6));
}

/* ---- NtGdiGetDhpdev ----------------------------------------------------- */

TEST(gap3_dhpdev_unknown_hdev_is_zero) {
    ULONG_PTR (WINAPI *g)(HANDLE) = (void *)UNX(NtGdiGetDhpdev);
    REQUIRE(g);
    /* ValidUmpdHdev only compares the value against the PDEV list entries; it never dereferences it. */
    static const ULONG_PTR bad[] = { 0, 1, 2, 0x1000, 0x12345678, 0xdeadbeef, 0xfffff80000001000ULL, (ULONG_PTR)-1 };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        PLANT();
        ULONG_PTR r = g((HANDLE)bad[i]);
        CHECKF(r == 0, "hdev 0x%llx -> 0x%llx", (unsigned long long)bad[i], (unsigned long long)r);
        EXPECT_EQ(GetLastError(), SENTINEL_ERR);
    }
}

TEST(gap3_dhpdev_gdi_handles_are_not_hdevs) {
    ULONG_PTR (WINAPI *g)(HANDLE) = (void *)UNX(NtGdiGetDhpdev);
    REQUIRE(g);
    HDC dc = fresh_dc();
    HDC scr = U(GetDC)(NULL);
    HBITMAP bm = U(CreateBitmap)(2, 2, 1, 1, NULL);
    EXPECT_EQ(g((HANDLE)dc), 0);
    EXPECT_EQ(g((HANDLE)scr), 0);
    EXPECT_EQ(g((HANDLE)bm), 0);
    U(DeleteObject)(bm); U(ReleaseDC)(NULL, scr); U(DeleteDC)(dc);
}

TEST(gap3_dhpdev_engdrivername_path_without_umpd_returns_null) {
    /* gdi32full!EngGetDriverName -> NtGdiGetDhpdev(); without a user-mode printer driver context the call yields 0
     * and EngGetDriverName returns NULL. */
    if (t_is_wine()) { t_skip("Wine"); return; }
    PVOID (WINAPI *egd)(void) = (void *)t_proc_in("gdi32.dll", "EngGetDriverName");
    REQUIRE(egd);
    t_note("EngGetDriverName() exported by gdi32; not invoked with a real HDEV from this process");
}

/* ---- NtGdiScaleRgn ------------------------------------------------------ */

TEST(gap3_scalergn_invalid_dc_or_region_is_false) {
    BOOL (WINAPI *sr)(HDC, HRGN) = (void *)UNX(NtGdiScaleRgn);
    REQUIRE(sr);
    HDC dc = fresh_dc();
    HRGN rg = U(CreateRectRgn)(1, 2, 30, 40);
    EXPECT_EQ(sr(NULL, rg), 0);              /* DCOBJ fails */
    EXPECT_EQ(sr((HDC)0xdeadbeef, rg), 0);
    EXPECT_EQ(sr(dc, NULL), 0);              /* RGNOBJAPI fails */
    EXPECT_EQ(sr(dc, (HRGN)0xdeadbeef), 0);
    EXPECT_EQ(sr(NULL, NULL), 0);
    EXPECT_EQ(sr((HDC)rg, (HRGN)dc), 0);     /* swapped types */
    U(DeleteObject)(rg); U(DeleteDC)(dc);
}

TEST(gap3_scalergn_valid_pair_succeeds) {
    BOOL (WINAPI *sr)(HDC, HRGN) = (void *)UNX(NtGdiScaleRgn);
    REQUIRE(sr);
    HDC dc = fresh_dc();
    static const RECT rects[] = { {0,0,0,0}, {1,2,30,40}, {-5,-5,5,5}, {100,100,101,101}, {0,0,4000,3000} };
    for (unsigned i = 0; i < sizeof rects / sizeof *rects; i++) {
        HRGN rg = U(CreateRectRgnIndirect)(&rects[i]);
        RECT before, after;
        U(GetRgnBox)(rg, &before);
        EXPECT_EQ(sr(dc, rg), 1);            /* both objects locked -> TRUE */
        U(GetRgnBox)(rg, &after);
        if (memcmp(&before, &after, sizeof before)) t_note("region %u box changed: DC has a DPI scale transform", i);
        else CHECK(1);
        U(DeleteObject)(rg);
    }
    U(DeleteDC)(dc);
}

TEST(gap3_scalergn_gdi32_wrapper_agrees) {
    BOOL (WINAPI *sr)(HDC, HRGN) = (void *)UNX(NtGdiScaleRgn);
    REQUIRE(sr);
    HDC dc = fresh_dc();
    HRGN rg = U(CreateRectRgn)(3, 4, 50, 60);
    BOOL (WINAPI *api)(HDC, HRGN) = (void *)t_proc_in("gdi32.dll", "ScaleRgn");
    REQUIRE(api);
    EXPECT_EQ(api(dc, rg), sr(dc, rg));
    EXPECT_EQ(api(NULL, rg), 0);
    U(DeleteObject)(rg); U(DeleteDC)(dc);
}

TEST(gap3_scalergn_complex_region) {
    BOOL (WINAPI *sr)(HDC, HRGN) = (void *)UNX(NtGdiScaleRgn);
    REQUIRE(sr);
    HDC dc = fresh_dc();
    HRGN a = U(CreateRectRgn)(0, 0, 10, 10), b = U(CreateRectRgn)(20, 20, 30, 30), c = U(CreateRectRgn)(0, 0, 0, 0);
    EXPECT_EQ(U(CombineRgn)(c, a, b, RGN_OR), COMPLEXREGION);
    EXPECT_EQ(sr(dc, c), 1);
    RECT bx; U(GetRgnBox)(c, &bx);
    CHECK(bx.right >= 30 && bx.bottom >= 30);
    U(DeleteObject)(a); U(DeleteObject)(b); U(DeleteObject)(c); U(DeleteDC)(dc);
}

/* ---- NtGdiScaleValues --------------------------------------------------- */

TEST(gap3_scalevalues_invalid_dc_is_false_and_buffer_untouched) {
    BOOL (WINAPI *sv)(HDC, LONG *, UINT) = (void *)UNX(NtGdiScaleValues);
    REQUIRE(sv);
    LONG v[4] = { 11, 22, 33, 44 };
    /* returns false when the DC does not lock; the copy-back is skipped on false. */
    EXPECT_EQ(sv(NULL, v, 4), 0);
    EXPECT_EQ(sv((HDC)0xdeadbeef, v, 4), 0);
    EXPECT_EQ(v[0], 11); EXPECT_EQ(v[1], 22); EXPECT_EQ(v[2], 33); EXPECT_EQ(v[3], 44);
}

TEST(gap3_scalevalues_huge_count_rejected_before_access) {
    BOOL (WINAPI *sv)(HDC, LONG *, UINT) = (void *)UNX(NtGdiScaleValues);
    REQUIRE(sv);
    HDC dc = fresh_dc();
    /* (ULONGLONG)n << 2 >= 2^32 skips the whole body -> 0; the pointer is never probed, so NULL is safe. */
    static const UINT counts[] = { 0x40000000u, 0x40000001u, 0x7fffffffu, 0x80000000u, 0xffffffffu };
    for (unsigned i = 0; i < sizeof counts / sizeof *counts; i++) {
        EXPECT_EQ(sv(dc, NULL, counts[i]), 0);
        EXPECT_EQ(sv(NULL, NULL, counts[i]), 0);
    }
    U(DeleteDC)(dc);
}

TEST(gap3_scalevalues_valid_dc_identity_scale) {
    BOOL (WINAPI *sv)(HDC, LONG *, UINT) = (void *)UNX(NtGdiScaleValues);
    REQUIRE(sv);
    HDC dc = fresh_dc();
    LONG v[6] = { 1, -2, 300, 4000, -50000, 7 };
    LONG orig[6]; memcpy(orig, v, sizeof v);
    EXPECT_EQ(sv(dc, v, 6), 1);              /* returns 1 when nothing needs scaling */
    if (memcmp(v, orig, sizeof v)) t_note("values scaled: DC has a DPI scale transform (%ld %ld ...)", v[0], v[1]);
    else CHECK(1);
    U(DeleteDC)(dc);
}

TEST(gap3_scalevalues_gdi32_wrapper_agrees) {
    HDC dc = fresh_dc();
    BOOL (WINAPI *api)(HDC, LONG *, UINT) = (void *)t_proc_in("gdi32.dll", "ScaleValues");
    BOOL (WINAPI *sv)(HDC, LONG *, UINT) = (void *)UNX(NtGdiScaleValues);
    REQUIRE(api); REQUIRE(sv);
    LONG a[3] = { 5, 6, 7 }, b[3] = { 5, 6, 7 };
    EXPECT_EQ(api(dc, a, 3), sv(dc, b, 3));
    CHECK(!memcmp(a, b, sizeof a));
    U(DeleteDC)(dc);
}

/* ---- NtGdiPolyDraw ------------------------------------------------------ */

TEST(gap3_polydraw_huge_count_rejected_without_access) {
    BOOL (WINAPI *pd)(HDC, const POINT *, const BYTE *, DWORD) = (void *)UNX(NtGdiPolyDraw);
    REQUIRE(pd);
    HDC dc = fresh_dc();
    /* (uint)count >= 0x20000000 leaves both secure handles NULL -> returns 0 before touching either pointer. */
    static const DWORD counts[] = { 0x20000000u, 0x20000001u, 0x7fffffffu, 0x80000000u, 0xffffffffu };
    for (unsigned i = 0; i < sizeof counts / sizeof *counts; i++) {
        EXPECT_EQ(pd(dc, NULL, NULL, counts[i]), 0);
        EXPECT_EQ(pd(NULL, NULL, NULL, counts[i]), 0);
    }
    U(DeleteDC)(dc);
}

TEST(gap3_polydraw_invalid_hdc_fails) {
    BOOL (WINAPI *pd)(HDC, const POINT *, const BYTE *, DWORD) = (void *)UNX(NtGdiPolyDraw);
    REQUIRE(pd);
    POINT pts[2] = { {1, 1}, {10, 10} };
    BYTE ty[2] = { PT_MOVETO, PT_LINETO };
    EXPECT_EQ(pd(NULL, pts, ty, 2), 0);
    EXPECT_EQ(pd((HDC)0xdeadbeef, pts, ty, 2), 0);
    HRGN rg = U(CreateRectRgn)(0, 0, 1, 1);
    EXPECT_EQ(pd((HDC)rg, pts, ty, 2), 0);
    U(DeleteObject)(rg);
}

TEST(gap3_polydraw_draws_line_into_dib_memory) {
    BOOL (WINAPI *pd)(HDC, const POINT *, const BYTE *, DWORD) = (void *)UNX(NtGdiPolyDraw);
    REQUIRE(pd);
    gap3_dib d; DIB_OR_SKIP(d, 32, 32);
    POINT pts[2] = { {2, 5}, {20, 5} };
    BYTE ty[2] = { PT_MOVETO, PT_LINETO };
    EXPECT_EQ(pd(d.dc, pts, ty, 2), 1);
    U(GdiFlush)();
    /* The default black 1px pen draws the horizontal run on row 5; rows far away stay white. */
    EXPECT_EQ(d.px[5 * 32 + 10] & 0xFFFFFF, 0);
    EXPECT_EQ(d.px[5 * 32 + 3] & 0xFFFFFF, 0);
    EXPECT_EQ(d.px[15 * 32 + 10] & 0xFFFFFF, 0xFFFFFF);
    EXPECT_EQ(d.px[0] & 0xFFFFFF, 0xFFFFFF);
    CHECK(gap3_dib_nonwhite(&d) >= 15);
    gap3_dib_close(&d);
}

TEST(gap3_polydraw_gdi32_path_draws_same_pixels) {
    BOOL (WINAPI *pd)(HDC, const POINT *, const BYTE *, DWORD) = (void *)UNX(NtGdiPolyDraw);
    REQUIRE(pd);
    gap3_dib a, b; DIB_OR_SKIP(a, 32, 32);
    if (!gap3_dib_open(&b, 32, 32)) { gap3_dib_close(&a); t_skip("dib"); return; }
    POINT pts[3] = { {1, 1}, {25, 3}, {10, 28} };
    BYTE ty[3] = { PT_MOVETO, PT_LINETO, PT_LINETO };
    EXPECT_EQ(pd(a.dc, pts, ty, 3), 1);
    CHECK(U(PolyDraw)(b.dc, pts, ty, 3));
    U(GdiFlush)();
    CHECK(!memcmp(a.px, b.px, 32 * 32 * 4));
    CHECK(gap3_dib_nonwhite(&a) > 30);
    gap3_dib_close(&a); gap3_dib_close(&b);
}

TEST(gap3_polydraw_moveto_only_draws_nothing) {
    BOOL (WINAPI *pd)(HDC, const POINT *, const BYTE *, DWORD) = (void *)UNX(NtGdiPolyDraw);
    REQUIRE(pd);
    gap3_dib d; DIB_OR_SKIP(d, 16, 16);
    POINT pts[2] = { {1, 1}, {9, 9} };
    BYTE ty[2] = { PT_MOVETO, PT_MOVETO };
    BOOL r = pd(d.dc, pts, ty, 2);
    U(GdiFlush)();
    t_note("PolyDraw of two MOVETOs returned %d", r);
    EXPECT_EQ(gap3_dib_nonwhite(&d), 0);
    gap3_dib_close(&d);
}

TEST(gap3_polydraw_bezier) {
    BOOL (WINAPI *pd)(HDC, const POINT *, const BYTE *, DWORD) = (void *)UNX(NtGdiPolyDraw);
    REQUIRE(pd);
    gap3_dib d; DIB_OR_SKIP(d, 32, 32);
    POINT pts[4] = { {2, 28}, {8, 2}, {22, 2}, {29, 28} };
    BYTE ty[4] = { PT_MOVETO, PT_BEZIERTO, PT_BEZIERTO, PT_BEZIERTO };
    EXPECT_EQ(pd(d.dc, pts, ty, 4), 1);
    U(GdiFlush)();
    CHECK(gap3_dib_nonwhite(&d) > 20);
    EXPECT_EQ(d.px[28 * 32 + 2] & 0xFFFFFF, 0);        /* curve starts at the MOVETO point */
    gap3_dib_close(&d);
}

TEST(gap3_polydraw_current_position_after_lines) {
    BOOL (WINAPI *pd)(HDC, const POINT *, const BYTE *, DWORD) = (void *)UNX(NtGdiPolyDraw);
    REQUIRE(pd);
    gap3_dib d; DIB_OR_SKIP(d, 32, 32);
    POINT pts[3] = { {3, 3}, {12, 3}, {12, 14} };
    BYTE ty[3] = { PT_MOVETO, PT_LINETO, PT_LINETO };
    EXPECT_EQ(pd(d.dc, pts, ty, 3), 1);
    POINT cp = { -1, -1 };
    U(GetCurrentPositionEx)(d.dc, &cp);
    t_note("current position after PolyDraw: (%ld,%ld)", cp.x, cp.y);
    gap3_dib_close(&d);
}

/* ---- NtGdiPolyTextOutW -------------------------------------------------- */

TEST(gap3_polytextout_draws_text_into_dib_memory) {
    gap3_dib d; DIB_OR_SKIP(d, 64, 32);
    static const WCHAR s[] = L"MMMM";
    POLYTEXTW pt; memset(&pt, 0, sizeof pt);
    pt.x = 2; pt.y = 2; pt.n = 4; pt.lpstr = s; pt.uiFlags = 0;
    U(SetBkMode)(d.dc, TRANSPARENT);
    U(SetTextColor)(d.dc, RGB(0, 0, 0));
    BOOL r = U(PolyTextOutW)(d.dc, &pt, 1);
    U(GdiFlush)();
    EXPECT_EQ(r, 1);
    CHECK(gap3_dib_nonwhite(&d) > 20);       /* four capital Ms leave ink on a white background */
    gap3_dib_close(&d);
}

TEST(gap3_polytextout_direct_stub_two_entries) {
    BOOL (WINAPI *pt)(HDC, const POLYTEXTW *, UINT) = (void *)UNX(NtGdiPolyTextOutW);
    REQUIRE(pt);
    gap3_dib d; DIB_OR_SKIP(d, 96, 40);
    static const WCHAR a[] = L"AB", b[] = L"CD";
    POLYTEXTW e[2]; memset(e, 0, sizeof e);
    CHECK(sizeof(POLYTEXTW) == 0x38);        /* the kernel indexes entries at 0x38 bytes */
    e[0].x = 1; e[0].y = 1; e[0].n = 2; e[0].lpstr = a;
    e[1].x = 40; e[1].y = 18; e[1].n = 2; e[1].lpstr = b;
    U(SetBkMode)(d.dc, TRANSPARENT);
    BOOL r = pt(d.dc, e, 2);
    U(GdiFlush)();
    EXPECT_EQ(r, 1);
    int top = 0, bottom = 0;
    for (int y = 0; y < 40; y++) for (int x = 0; x < 96; x++)
        if ((d.px[y * 96 + x] & 0xFFFFFF) != 0xFFFFFF) { if (x < 38 && y < 17) top++; if (x >= 38 && y >= 17) bottom++; }
    CHECK(top > 5);
    CHECK(bottom > 5);
    gap3_dib_close(&d);
}

TEST(gap3_polytextout_opaque_clip_rect_fills_background) {
    gap3_dib d; DIB_OR_SKIP(d, 64, 32);
    static const WCHAR s[] = L"I";
    POLYTEXTW pt; memset(&pt, 0, sizeof pt);
    pt.x = 4; pt.y = 4; pt.n = 1; pt.lpstr = s; pt.uiFlags = ETO_OPAQUE;
    pt.rcl.left = 0; pt.rcl.top = 0; pt.rcl.right = 20; pt.rcl.bottom = 20;
    U(SetBkColor)(d.dc, RGB(255, 0, 0));
    BOOL r = U(PolyTextOutW)(d.dc, &pt, 1);
    U(GdiFlush)();
    EXPECT_EQ(r, 1);
    /* ETO_OPAQUE paints the rectangle with the background colour (BGRA DIB: red = 0x00FF0000). */
    EXPECT_EQ(d.px[0] & 0xFFFFFF, 0xFF0000);
    EXPECT_EQ(d.px[19 * 64 + 19] & 0xFFFFFF, 0xFF0000);
    EXPECT_EQ(d.px[25 * 64 + 40] & 0xFFFFFF, 0xFFFFFF);
    gap3_dib_close(&d);
}

TEST(gap3_polytextout_invalid_hdc_fails) {
    static const WCHAR s[] = L"x";
    POLYTEXTW pt; memset(&pt, 0, sizeof pt);
    pt.n = 1; pt.lpstr = s;
    EXPECT_EQ(U(PolyTextOutW)((HDC)0xdeadbeef, &pt, 1), 0);
    EXPECT_EQ(U(PolyTextOutW)(NULL, &pt, 1), 0);
}

/* ---- NtGdiGetCharacterPlacementW ---------------------------------------- */

TEST(gap3_gcp_early_exits_return_zero) {
    DWORD (WINAPI *g)(HDC, LPCWSTR, int, int, GCP_RESULTSW *, DWORD) = (void *)UNX(NtGdiGetCharacterPlacementW);
    REQUIRE(g);
    HDC dc = fresh_dc();
    static const WCHAR s[] = L"hello";
    PLANT();
    EXPECT_EQ(g(dc, NULL, 5, -1, NULL, 0), 0);
    EXPECT_EQ(g(dc, s, -1, -1, NULL, 0), 0);          /* count < 0 */
    EXPECT_EQ(g(dc, s, 0x80000000, -1, NULL, 0), 0);
    EXPECT_EQ(g(dc, s, 5, -2, NULL, 0), 0);           /* nMaxExtent <= -2 */
    EXPECT_EQ(g(dc, s, 5, -100, NULL, 0), 0);
    EXPECT_EQ(g(dc, s, 5, (int)0x80000000, NULL, 0), 0);
    EXPECT_EQ(GetLastError(), SENTINEL_ERR);
    U(DeleteDC)(dc);
}

TEST(gap3_gcp_too_many_chars_rejected) {
    DWORD (WINAPI *g)(HDC, LPCWSTR, int, int, GCP_RESULTSW *, DWORD) = (void *)UNX(NtGdiGetCharacterPlacementW);
    REQUIRE(g);
    HDC dc = fresh_dc();
    static const WCHAR s[] = L"hello";
 /* count > 0x9c4000 fails the gate before the string is copied. */
    EXPECT_EQ(g(dc, s, 0x9c4001, -1, NULL, 0), 0);
    EXPECT_EQ(g(dc, s, 0x7fffffff, -1, NULL, 0), 0);
    U(DeleteDC)(dc);
}

TEST(gap3_gcp_invalid_hdc_fails) {
    DWORD (WINAPI *g)(HDC, LPCWSTR, int, int, GCP_RESULTSW *, DWORD) = (void *)UNX(NtGdiGetCharacterPlacementW);
    REQUIRE(g);
    static const WCHAR s[] = L"hello";
    EXPECT_EQ(g(NULL, s, 5, -1, NULL, 0), 0);
    EXPECT_EQ(g((HDC)0xdeadbeef, s, 5, -1, NULL, 0), 0);
}

TEST(gap3_gcp_null_results_matches_text_extent) {
    DWORD (WINAPI *g)(HDC, LPCWSTR, int, int, GCP_RESULTSW *, DWORD) = (void *)UNX(NtGdiGetCharacterPlacementW);
    REQUIRE(g);
    gap3_dib d; DIB_OR_SKIP(d, 8, 8);
    static const WCHAR *strs[] = { L"M", L"hello", L"Hello, world", L"iiiiiiii", L"WWWWWWWW 0123456789" };
    for (unsigned i = 0; i < sizeof strs / sizeof *strs; i++) {
        int n = lstrlenW(strs[i]);
        SIZE sz = { 0, 0 };
        CHECK(U(GetTextExtentPoint32W)(d.dc, strs[i], n, &sz));
        /* lpResults == NULL: text extent result packed as (cy << 16) | cx. */
        DWORD r = g(d.dc, strs[i], n, -1, NULL, 0);
        EXPECT_EQ(LOWORD(r), sz.cx & 0xFFFF);
        EXPECT_EQ(HIWORD(r), sz.cy & 0xFFFF);
        CHECK(r != 0);
        /* The gdi32 API on the same arguments agrees with the raw syscall. */
        DWORD api = U(GetCharacterPlacementW)(d.dc, strs[i], n, -1, NULL, 0);
        EXPECT_EQ(api, r);
    }
    gap3_dib_close(&d);
}

TEST(gap3_gcp_extent_grows_with_length) {
    DWORD (WINAPI *g)(HDC, LPCWSTR, int, int, GCP_RESULTSW *, DWORD) = (void *)UNX(NtGdiGetCharacterPlacementW);
    REQUIRE(g);
    gap3_dib d; DIB_OR_SKIP(d, 8, 8);
    static const WCHAR s[] = L"abcdefghij";
    unsigned prev = 0;
    for (int n = 1; n <= 10; n++) {
        DWORD r = g(d.dc, s, n, -1, NULL, 0);
        unsigned cx = LOWORD(r);
        CHECKF(cx > prev, "n=%d cx=%u prev=%u", n, cx, prev);
        prev = cx;
    }
    gap3_dib_close(&d);
}

TEST(gap3_gcp_with_results_fills_dx_and_counts) {
    DWORD (WINAPI *g)(HDC, LPCWSTR, int, int, GCP_RESULTSW *, DWORD) = (void *)UNX(NtGdiGetCharacterPlacementW);
    REQUIRE(g);
    gap3_dib d; DIB_OR_SKIP(d, 8, 8);
    static const WCHAR s[] = L"MMiiWW";
    int n = 6;
    int dx[8]; memset(dx, 0xCC, sizeof dx);
    GCP_RESULTSW res; memset(&res, 0, sizeof res);
    res.lStructSize = sizeof res;
    res.lpDx = dx;
    res.nGlyphs = n;
    SIZE sz = { 0, 0 };
    U(GetTextExtentPoint32W)(d.dc, s, n, &sz);
    DWORD r = g(d.dc, s, n, -1, &res, 0);
    CHECK(r != 0);
    EXPECT_EQ(LOWORD(r), sz.cx & 0xFFFF);
    long sum = 0;
    for (int i = 0; i < n; i++) { CHECKF(dx[i] > 0, "dx[%d]=%d", i, dx[i]); sum += dx[i]; }
    CHECKF(dx[6] == (int)0xCCCCCCCC && dx[7] == (int)0xCCCCCCCC, "dx beyond nGlyphs written");
    t_note("sum(dx)=%ld cx=%ld nGlyphs=%u nMaxFit=%d", sum, (long)sz.cx, res.nGlyphs, res.nMaxFit);
    CHECK(res.nGlyphs <= (UINT)n);
    gap3_dib_close(&d);
}

TEST(gap3_gcp_results_nglyphs_clamps_copy_count) {
    DWORD (WINAPI *g)(HDC, LPCWSTR, int, int, GCP_RESULTSW *, DWORD) = (void *)UNX(NtGdiGetCharacterPlacementW);
    REQUIRE(g);
    gap3_dib d; DIB_OR_SKIP(d, 8, 8);
    static const WCHAR s[] = L"abcdefgh";
    int dx[8]; memset(dx, 0xCC, sizeof dx);
    GCP_RESULTSW res; memset(&res, 0, sizeof res);
    res.lStructSize = sizeof res; res.lpDx = dx;
    res.nGlyphs = 3;                 /* kernel: count = min(count, nGlyphs) */
    DWORD r = g(d.dc, s, 8, -1, &res, 0);
    CHECK(r != 0);
    for (int i = 0; i < 3; i++) CHECKF(dx[i] > 0, "dx[%d]", i);
    for (int i = 3; i < 8; i++) CHECKF(dx[i] == (int)0xCCCCCCCC, "dx[%d] written past nGlyphs", i);
    gap3_dib_close(&d);
}

TEST(gap3_gcp_out_string_copied) {
    DWORD (WINAPI *g)(HDC, LPCWSTR, int, int, GCP_RESULTSW *, DWORD) = (void *)UNX(NtGdiGetCharacterPlacementW);
    REQUIRE(g);
    gap3_dib d; DIB_OR_SKIP(d, 8, 8);
    static const WCHAR s[] = L"Test";
    WCHAR out[8]; memset(out, 0xCC, sizeof out);
    GCP_RESULTSW res; memset(&res, 0, sizeof res);
    res.lStructSize = sizeof res; res.lpOutString = out; res.nGlyphs = 4;
    DWORD r = g(d.dc, s, 4, -1, &res, 0);
    CHECK(r != 0);
    if (memcmp(out, s, 8)) t_note("lpOutString differs from input without GCP_REORDER");
    else CHECK(1);
    gap3_dib_close(&d);
}

/* ---- NtGdiMakeFontDir --------------------------------------------------- */

TEST(gap3_makefontdir_size_gates) {
    int (WINAPI *mf)(DWORD, void *, UINT, void *, UINT) = (void *)UNX(NtGdiMakeFontDir);
    REQUIRE(mf);
    /* Early exits: cjFontDir must be > 0xfa and cjPath <= 0x20a; otherwise 0 and the output block is untouched. */
    static const struct { UINT cjDir, cjPath; } v[] = {
        { 0, 4 }, { 0xf9, 4 }, { 0xfa, 4 },            /* cjFontDir <= 0xfa */
        { 0x100, 0x20b }, { 0x100, 0x20c }, { 0x100, 0x400 }, { 0x100, 0xffffffffu }, { 0xfb, 0x20b }, /* cjPath > 0x20a */
        { 0, 0x20b }, { 0xfa, 0xffff },
    };
    for (unsigned flags = 0; flags < 2; flags++)
        for (unsigned i = 0; i < sizeof v / sizeof *v; i++) {
            BYTE out[0x110]; memset(out, 0xCC, sizeof out);
            WCHAR path[8] = L"x.ttf";
            int r = mf(flags, out, v[i].cjDir, path, v[i].cjPath);
            CHECKF(r == 0, "flags %u cjDir 0x%x cjPath 0x%x -> %d", flags, v[i].cjDir, v[i].cjPath, r);
            for (unsigned k = 0; k < sizeof out; k++) if (out[k] != 0xCC) { CHECKF(0, "output byte %u modified", k); break; }
        }
}

TEST(gap3_makefontdir_gates_do_not_read_path_buffer) {
    int (WINAPI *mf)(DWORD, void *, UINT, void *, UINT) = (void *)UNX(NtGdiMakeFontDir);
    REQUIRE(mf);
    BYTE out[0x100];
    /* The path pointer is only copied after both gates pass, so an oversize cjPath with a NULL path is a clean failure. */
    EXPECT_EQ(mf(0, out, 0x100, NULL, 0x20b), 0);
    EXPECT_EQ(mf(0, out, 0x10, NULL, 4), 0);
}

/* ---- NtGdiCheckBitmapBits ----------------------------------------------- */

TEST(gap3_checkbitmapbits_bad_format_or_type_invalid_parameter) {
    int (WINAPI *cb)(HDC, void *, void *, int, UINT, int, UINT, BYTE *) = (void *)UNX(NtGdiCheckBitmapBits);
    REQUIRE(cb);
    BYTE in[16] = {0}, res[8];
 /* else EngSetLastError(0x57). Nothing is probed on this path. */
    static const struct { int fmt; int type; } v[] = { {0,1},{1,1},{3,1},{5,1},{-1,1},{2,0},{2,2},{2,-1},{0,0},{3,3} };
    for (unsigned i = 0; i < sizeof v / sizeof *v; i++) {
        PLANT();
        int r = cb((HDC)0xdeadbeef, NULL, in, v[i].fmt, 1, v[i].type, 4, res);
        CHECKF(r == 0, "fmt %d type %d r=%d", v[i].fmt, v[i].type, r);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
}

TEST(gap3_checkbitmapbits_buffer_size_must_match_count) {
    int (WINAPI *cb)(HDC, void *, void *, int, UINT, int, UINT, BYTE *) = (void *)UNX(NtGdiCheckBitmapBits);
    REQUIRE(cb);
    BYTE in[64] = {0}, res[16];
    for (UINT n = 1; n <= 12; n++) {
        UINT want = (n * 3 + 3) & ~3u;
        static const int deltas[] = { -4, -1, 1, 4, 8 };
        for (unsigned j = 0; j < 5; j++) {
            UINT sz = want + deltas[j];
            if (sz == want) continue;
            PLANT();
            int r = cb((HDC)0xdeadbeef, NULL, in, 2, n, 1, sz, res);
            CHECKF(r == 0, "n %u cb %u want %u r=%d", n, sz, want, r);
            EXPECT_ERR(ERROR_INVALID_PARAMETER);
        }
    }
}

TEST(gap3_checkbitmapbits_count_overflow_is_arithmetic_overflow) {
    int (WINAPI *cb)(HDC, void *, void *, int, UINT, int, UINT, BYTE *) = (void *)UNX(NtGdiCheckBitmapBits);
    REQUIRE(cb);
    static const UINT ns[] = { 0x55555556u, 0x60000000u, 0x80000000u, 0xfffffffeu, 0xffffffffu };
    for (unsigned i = 0; i < sizeof ns / sizeof *ns; i++) {
        PLANT();
        /* n*3 as 64-bit exceeds 0xffffffff -> status 0x216 (ERROR_ARITHMETIC_OVERFLOW), checked before cbBuf. */
        int r = cb((HDC)0xdeadbeef, NULL, NULL, 2, ns[i], 1, 0, NULL);
        CHECKF(r == 0, "n 0x%x r=%d", ns[i], r);
        EXPECT_ERR(0x216);
    }
}

TEST(gap3_checkbitmapbits_overflow_precedes_size_match_only_when_formats_valid) {
    int (WINAPI *cb)(HDC, void *, void *, int, UINT, int, UINT, BYTE *) = (void *)UNX(NtGdiCheckBitmapBits);
    REQUIRE(cb);
    PLANT();
    /* With fmt != 2 the overflow test is never reached: ERROR_INVALID_PARAMETER wins. */
    EXPECT_EQ(cb((HDC)0xdeadbeef, NULL, NULL, 1, 0xffffffffu, 1, 0, NULL), 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}

TEST(gap3_checkcolorsingamut_null_args_invalid_parameter) {
    if (t_is_wine()) { t_skip("CheckColorsInGamut is a Wine stub"); return; }
    HDC dc = fresh_dc();
    RGBTRIPLE rgb[2] = { {1,2,3}, {4,5,6} };
    BYTE out[4] = {0};
    PLANT(); EXPECT_EQ(U(CheckColorsInGamut)(dc, NULL, out, 2), 0);   EXPECT_ERR(ERROR_INVALID_PARAMETER);
    PLANT(); EXPECT_EQ(U(CheckColorsInGamut)(dc, rgb, NULL, 2), 0);   EXPECT_ERR(ERROR_INVALID_PARAMETER);
    PLANT(); EXPECT_EQ(U(CheckColorsInGamut)(dc, rgb, out, 0), 0);    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    U(DeleteDC)(dc);
}

TEST(gap3_checkcolorsingamut_bad_dc_invalid_parameter) {
    if (t_is_wine()) { t_skip("CheckColorsInGamut is a Wine stub"); return; }
    RGBTRIPLE rgb[2] = { {1,2,3}, {4,5,6} };
    BYTE out[4] = {0};
    PLANT(); EXPECT_EQ(U(CheckColorsInGamut)(NULL, rgb, out, 2), 0);          EXPECT_ERR(ERROR_INVALID_PARAMETER);
    PLANT(); EXPECT_EQ(U(CheckColorsInGamut)((HDC)0xdeadbeef, rgb, out, 2), 0); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    HDC dc = fresh_dc(); U(DeleteDC)(dc);
    PLANT(); EXPECT_EQ(U(CheckColorsInGamut)(dc, rgb, out, 2), 0);            EXPECT_ERR(ERROR_INVALID_PARAMETER);
}

TEST(gap3_checkcolorsingamut_dc_without_icm_transform) {
    if (t_is_wine()) { t_skip("CheckColorsInGamut is a Wine stub"); return; }
    HDC dc = fresh_dc();
    RGBTRIPLE rgb[2] = { {1,2,3}, {4,5,6} };
    BYTE out[4] = { 0xCC, 0xCC, 0xCC, 0xCC };
    PLANT();
    /* gdi32full!CheckColorsInGamut: a valid DC whose ICM flags (DC_ATTR+0xf0 & 3) are clear -> error 0x7e2. */
    EXPECT_EQ(U(CheckColorsInGamut)(dc, rgb, out, 2), 0);
    EXPECT_ERR(0x7e2);
    EXPECT_EQ(out[0], 0xCC);
    U(DeleteDC)(dc);
}
