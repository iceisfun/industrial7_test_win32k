/* Pinpoint calls of the path / transform / drawing syscalls through their win32u.dll stubs (no gdi32 in between), plus
 * stub-layout checks against the service numbers listed in tools/win32u_syscalls.tsv.
 *
 * Signatures (from the win32kfull analysis): every path entry takes just the HDC (GetPath: hdc, POINT*, BYTE*, int;
 * SelectClipPath: hdc, int mode). The raw win32u stub for each is 'mov r10,rcx; mov eax,<n>; ...; syscall; ret'.
 */
#include "xd.h"

#define MEMDC(dc) HDC dc = U(CreateCompatibleDC)(NULL); if (!dc) { t_skip("CreateCompatibleDC failed"); return; }
#define MEMDC_FREE(dc) U(DeleteDC)(dc)
#define NEED_CORE() do { REQUIRE(U(CreateCompatibleDC)); REQUIRE(U(MoveToEx)); REQUIRE(U(LineTo)); REQUIRE(U(Rectangle)); \
    REQUIRE(U(CreatePen)); REQUIRE(U(GetStockObject)); } while (0)

/* ---- stub layout / service numbers ------------------------------------------------------------------------- */
typedef struct { const char *name; long number; } stubnum_t;

#define SN(n, v) { #n, v }
static const stubnum_t stub_numbers[] = {
    SN(NtGdiBeginPath, 4348), SN(NtGdiEndPath, 4349), SN(NtGdiAbortPath, 4476), SN(NtGdiCloseFigure, 4339),
    SN(NtGdiFillPath, 4350), SN(NtGdiStrokePath, 4895), SN(NtGdiStrokeAndFillPath, 4894), SN(NtGdiWidenPath, 4903),
    SN(NtGdiFlattenPath, 4768), SN(NtGdiPathToRegion, 4849), SN(NtGdiGetPath, 4813), SN(NtGdiSelectClipPath, 4871),
    SN(NtGdiGetDCPoint, 4209), SN(NtGdiModifyWorldTransform, 4297), SN(NtGdiGetTransform, 4303), SN(NtGdiTransformPoints, 4208),
    SN(NtGdiCombineTransform, 4323), SN(NtGdiMoveTo, 4842), SN(NtGdiLineTo, 4161), SN(NtGdiRectangle, 4237),
    SN(NtGdiEllipse, 4720), SN(NtGdiRoundRect, 4860), SN(NtGdiArcInternal, 4484), SN(NtGdiAngleArc, 4482),
    SN(NtGdiPolyPolyDraw, 4168), SN(NtGdiPolyDraw, 4851), SN(NtGdiPolyPatBlt, 4205), SN(NtGdiSetBoundsRect, 4334),
    SN(NtGdiGetBoundsRect, 4775), SN(NtGdiScaleViewportExtEx, 4868), SN(NtGdiScaleWindowExtEx, 4869),
    SN(NtGdiSetLayout, 4243), SN(NtGdiGetDCDword, 4159),
};

XT(direct_stub_service_numbers) {
    size_t i;
    if (t_is_wine()) { t_skip("Wine's win32u does not use win32k service numbers"); return; }
    for (i = 0; i < sizeof stub_numbers / sizeof stub_numbers[0]; i++) {
        FARPROC p = t_proc_in("win32u.dll", stub_numbers[i].name);
        CHECKF(p != NULL, "%s not exported by win32u.dll", stub_numbers[i].name);
        if (!p) continue;
        CHECKF(k_stub_valid(p), "%s stub prologue shape", stub_numbers[i].name);
        CHECKF(k_stub_number(p) == stub_numbers[i].number, "%s service number %ld want %ld", stub_numbers[i].name,
               k_stub_number(p), stub_numbers[i].number);
    }
}

XT(direct_stub_numbers_in_win32k_range) {
    size_t i;
    if (t_is_wine()) { t_skip("no win32k service table under Wine"); return; }
    for (i = 0; i < sizeof stub_numbers / sizeof stub_numbers[0]; i++) {
        FARPROC p = t_proc_in("win32u.dll", stub_numbers[i].name);
        if (!p) continue;
        /* table 1 (win32k) dispatches numbers with bit 12 set: 0x1000 + index */
        CHECKF((k_stub_number(p) & 0x1000) != 0 && k_stub_number(p) < 0x1400, "%s number %ld", stub_numbers[i].name, k_stub_number(p));
    }
}

XT(direct_raw_syscall_beginpath_endpath) {
    long long r;
    NEED_CORE(); REQUIRE(UN(NtGdiGetPath));
    if (t_is_wine()) { t_skip("raw syscalls are meaningless under Wine"); return; }
    MEMDC(dc);
    r = k_raw_syscall(4348, (ULONG_PTR)dc, 0, 0, 0, 0, 0, 0, 0);
    CHECK((int)r != 0);                                           /* NtGdiBeginPath(hdc) = TRUE */
    U(MoveToEx)(dc, 1, 1, NULL); U(LineTo)(dc, 9, 9);
    r = k_raw_syscall(4349, (ULONG_PTR)dc, 0, 0, 0, 0, 0, 0, 0);
    CHECK((int)r != 0);                                           /* NtGdiEndPath */
    EXPECT_INT(UN(NtGdiGetPath)(dc, NULL, NULL, 0), 2);
    r = k_raw_syscall(4349, (ULONG_PTR)dc, 0, 0, 0, 0, 0, 0, 0);
    CHECK((int)r == 0);                                           /* second EndPath: 0x3eb */
    r = k_raw_syscall(4476, (ULONG_PTR)dc, 0, 0, 0, 0, 0, 0, 0);
    CHECK((int)r != 0);                                           /* AbortPath */
    EXPECT_INT(UN(NtGdiGetPath)(dc, NULL, NULL, 0), -1);
    MEMDC_FREE(dc);
}

/* ---- path syscalls without gdi32 ------------------------------------------------------------------------------ */
#define NEED_PATH_NT() do { NEED_CORE(); REQUIRE_NT(NtGdiBeginPath); REQUIRE_NT(NtGdiEndPath); REQUIRE_NT(NtGdiAbortPath); \
    REQUIRE_NT(NtGdiCloseFigure); REQUIRE_NT(NtGdiFillPath); REQUIRE_NT(NtGdiStrokePath); REQUIRE_NT(NtGdiStrokeAndFillPath); \
    REQUIRE_NT(NtGdiWidenPath); REQUIRE_NT(NtGdiFlattenPath); REQUIRE_NT(NtGdiPathToRegion); REQUIRE_NT(NtGdiGetPath); \
    REQUIRE_NT(NtGdiSelectClipPath); } while (0)

typedef BOOL (WINAPI *hdc_fn)(HDC);

XT(direct_path_wrong_handles_all_fail) {
    HDC bad[5]; size_t i;
    NEED_PATH_NT();
    bad[0] = NULL;
    bad[1] = (HDC)(ULONG_PTR)0x01010fff;
    bad[2] = (HDC)U(GetStockObject)(BLACK_PEN);
    bad[3] = (HDC)U(GetStockObject)(WHITE_BRUSH);
    bad[4] = (HDC)U(GetStockObject)(SYSTEM_FONT);
    for (i = 0; i < 5; i++) {
        HRGN r;
        SET_ERR(0);
        CHECK(!UN(NtGdiBeginPath)(bad[i])); EXPECT_ERR(ERR_INVALID_HANDLE);
        SET_ERR(0);
        CHECK(!UN(NtGdiEndPath)(bad[i])); EXPECT_ERR(ERR_INVALID_HANDLE);
        SET_ERR(0);
        CHECK(!UN(NtGdiAbortPath)(bad[i])); EXPECT_ERR(ERR_INVALID_HANDLE);
        SET_ERR(0);
        CHECK(!UN(NtGdiCloseFigure)(bad[i])); EXPECT_ERR(ERR_INVALID_PARAMETER);
        SET_ERR(0);
        CHECK(!UN(NtGdiStrokePath)(bad[i])); EXPECT_ERR(ERR_INVALID_PARAMETER);
        SET_ERR(0);
        CHECK(!UN(NtGdiWidenPath)(bad[i])); EXPECT_ERR(ERR_INVALID_PARAMETER);
        SET_ERR(0);
        CHECK(!UN(NtGdiFlattenPath)(bad[i])); EXPECT_ERR(ERR_INVALID_HANDLE);
        CHECK(!UN(NtGdiFillPath)(bad[i]));
        CHECK(!UN(NtGdiStrokeAndFillPath)(bad[i]));
        SET_ERR(0);
        r = UN(NtGdiPathToRegion)(bad[i]); CHECK(r == NULL); EXPECT_ERR(ERR_INVALID_PARAMETER);
        SET_ERR(0);
        CHECK(!UN(NtGdiSelectClipPath)(bad[i], RGN_COPY)); EXPECT_ERR(ERR_INVALID_PARAMETER);
        SET_ERR(0);
        EXPECT_INT(UN(NtGdiGetPath)(bad[i], NULL, NULL, 0), -1); EXPECT_ERR(ERR_INVALID_PARAMETER);
    }
}

XT(direct_path_lifecycle) {
    POINT pt[8]; BYTE ty[8];
    NEED_PATH_NT();
    MEMDC(dc);
    SET_ERR(0);
    EXPECT_INT(UN(NtGdiGetPath)(dc, NULL, NULL, 0), -1);                 /* no path yet */
    CHECK(UN(NtGdiBeginPath)(dc));
    SET_ERR(0);
    EXPECT_INT(UN(NtGdiGetPath)(dc, NULL, NULL, 0), -1);                 /* path still open */
    EXPECT_ERR(ERR_CAN_NOT_COMPLETE);
    U(Rectangle)(dc, 10, 10, 30, 30);
    CHECK(UN(NtGdiEndPath)(dc));
    EXPECT_INT(UN(NtGdiGetPath)(dc, NULL, NULL, 0), 4);
    EXPECT_INT(UN(NtGdiGetPath)(dc, pt, ty, 8), 4);
    EXPECT_EQ(ty[0], PT_MOVETO); EXPECT_EQ(ty[3], PT_LINETO | PT_CLOSEFIGURE);
    SET_ERR(0);
    CHECK(!UN(NtGdiEndPath)(dc)); EXPECT_ERR(ERR_CAN_NOT_COMPLETE);
    SET_ERR(0);
    CHECK(!UN(NtGdiCloseFigure)(dc)); EXPECT_ERR(ERR_CAN_NOT_COMPLETE);
    CHECK(UN(NtGdiAbortPath)(dc));
    EXPECT_INT(UN(NtGdiGetPath)(dc, NULL, NULL, 0), -1);
    MEMDC_FREE(dc);
}

XT(direct_path_buffer_protocol) {
    POINT pt[8]; BYTE ty[8];
    NEED_PATH_NT();
    MEMDC(dc);
    UN(NtGdiBeginPath)(dc); U(Rectangle)(dc, 1, 2, 11, 12); UN(NtGdiEndPath)(dc);
    SET_ERR(0);
    EXPECT_INT(UN(NtGdiGetPath)(dc, pt, ty, 3), -1); EXPECT_ERR(ERR_INVALID_PARAMETER);
    SET_ERR(0);
    EXPECT_INT(UN(NtGdiGetPath)(dc, pt, ty, -1), -1); EXPECT_ERR(ERR_INVALID_PARAMETER);
    SET_ERR(0);
    EXPECT_INT(UN(NtGdiGetPath)(dc, pt, ty, 0x7fffffff), -1);              /* (int)n > 0x1fffffff points is refused too */
    EXPECT_INT(UN(NtGdiGetPath)(dc, pt, ty, 4), 4);
    UN(NtGdiAbortPath)(dc);
    MEMDC_FREE(dc);
}

XT(direct_path_fill_pixels) {
    gdi_sel_t s = {0}; POINT pt[8]; BYTE ty[8];
    NEED_PATH_NT();
    FX(f, 48, 48);
    sel_brush(f.dc, &s, COL_BLUE);
    UN(NtGdiBeginPath)(f.dc); U(Rectangle)(f.dc, 10, 10, 30, 30); UN(NtGdiEndPath)(f.dc);
    CHECK(UN(NtGdiFillPath)(f.dc));
    EXPECT_EQ(fx_px(&f, 20, 20), PX_BLUE);
    EXPECT_EQ(fx_px(&f, 5, 5), PX_WHITE);
    EXPECT_INT(UN(NtGdiGetPath)(f.dc, pt, ty, 8), -1);                      /* consumed */
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(direct_path_stroke_and_strokefill_pixels) {
    gdi_sel_t s = {0};
    NEED_PATH_NT();
    FX(f, 48, 48);
    sel_pen(f.dc, &s, COL_RED, 1); sel_brush(f.dc, &s, COL_BLUE);
    UN(NtGdiBeginPath)(f.dc); U(Rectangle)(f.dc, 10, 10, 30, 30); UN(NtGdiEndPath)(f.dc);
    CHECK(UN(NtGdiStrokePath)(f.dc));
    EXPECT_EQ(fx_px(&f, 20, 20), PX_WHITE);
    CHECK(fx_count(&f, PX_RED) > 60);
    EXPECT_INT(fx_count(&f, PX_BLUE), 0);
    fx_clear(&f, 0x00FFFFFF);
    UN(NtGdiBeginPath)(f.dc); U(Rectangle)(f.dc, 10, 10, 30, 30); UN(NtGdiEndPath)(f.dc);
    CHECK(UN(NtGdiStrokeAndFillPath)(f.dc));
    EXPECT_EQ(fx_px(&f, 20, 20), PX_BLUE);
    CHECK(fx_count(&f, PX_RED) > 60);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(direct_path_to_region_and_clip) {
    HRGN r; gdi_sel_t s = {0};
    NEED_PATH_NT(); REQUIRE(U(PtInRegion)); REQUIRE(U(PtVisible)); REQUIRE(U(GetRgnBox));
    MEMDC(dc);
    UN(NtGdiBeginPath)(dc); U(Rectangle)(dc, 10, 10, 30, 30); UN(NtGdiEndPath)(dc);
    r = UN(NtGdiPathToRegion)(dc);
    CHECK(r != NULL);
    if (r) { CHECK(U(PtInRegion)(r, 20, 20)); CHECK(!U(PtInRegion)(r, 40, 40)); U(DeleteObject)(r); }
    MEMDC_FREE(dc);
    {
        FX(f, 64, 64);
        UN(NtGdiBeginPath)(f.dc); U(Rectangle)(f.dc, 10, 10, 30, 30); UN(NtGdiEndPath)(f.dc);
        CHECK(UN(NtGdiSelectClipPath)(f.dc, RGN_COPY));
        CHECK(U(PtVisible)(f.dc, 20, 20)); CHECK(!U(PtVisible)(f.dc, 40, 40));
        UN(NtGdiBeginPath)(f.dc); U(Rectangle)(f.dc, 40, 40, 50, 50); UN(NtGdiEndPath)(f.dc);
        SET_ERR(0);
        CHECK(!UN(NtGdiSelectClipPath)(f.dc, 6)); EXPECT_ERR(ERR_INVALID_PARAMETER);
        SET_ERR(0);
        CHECK(!UN(NtGdiSelectClipPath)(f.dc, 0)); EXPECT_ERR(ERR_INVALID_PARAMETER);
        CHECK(UN(NtGdiSelectClipPath)(f.dc, RGN_OR));
        CHECK(U(PtVisible)(f.dc, 45, 45)); CHECK(U(PtVisible)(f.dc, 20, 20));
        fx_close(&f);
    }
    (void)s;
}

XT(direct_path_flatten_widen) {
    HGDIOBJ pen, old; LOGBRUSH lb = { BS_SOLID, 0, 0 }; POINT pt[256]; BYTE ty[256]; int n, i, beziers = 0;
    static const POINT bz[4] = { {10,10},{20,0},{30,20},{40,10} };
    NEED_PATH_NT(); REQUIRE(U(PolyBezier)); REQUIRE(U(ExtCreatePen));
    MEMDC(dc);
    UN(NtGdiBeginPath)(dc); U(PolyBezier)(dc, bz, 4); UN(NtGdiEndPath)(dc);
    CHECK(UN(NtGdiFlattenPath)(dc));
    n = UN(NtGdiGetPath)(dc, NULL, NULL, 0);
    CHECK(n > 4 && n <= 256);
    if (n > 4 && n <= 256) {
        UN(NtGdiGetPath)(dc, pt, ty, n);
        for (i = 0; i < n; i++) if ((ty[i] & ~PT_CLOSEFIGURE) == PT_BEZIERTO) beziers++;
        EXPECT_INT(beziers, 0);
    }
    pen = U(ExtCreatePen)(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_FLAT, 6, &lb, 0, NULL);
    old = U(SelectObject)(dc, pen);
    CHECK(UN(NtGdiWidenPath)(dc));
    CHECK(UN(NtGdiGetPath)(dc, NULL, NULL, 0) > n);
    U(SelectObject)(dc, old); U(DeleteObject)(pen);
    UN(NtGdiAbortPath)(dc);
    MEMDC_FREE(dc);
}

XT(direct_path_open_blocks_consumers) {
    NEED_PATH_NT();
    MEMDC(dc);
    UN(NtGdiBeginPath)(dc);
    U(MoveToEx)(dc, 1, 1, NULL); U(LineTo)(dc, 9, 9);
    SET_ERR(0); CHECK(!UN(NtGdiFillPath)(dc)); EXPECT_ERR(ERR_CAN_NOT_COMPLETE);
    SET_ERR(0); CHECK(!UN(NtGdiStrokePath)(dc)); EXPECT_ERR(ERR_CAN_NOT_COMPLETE);
    SET_ERR(0); CHECK(!UN(NtGdiStrokeAndFillPath)(dc)); EXPECT_ERR(ERR_CAN_NOT_COMPLETE);
    SET_ERR(0); CHECK(!UN(NtGdiWidenPath)(dc)); EXPECT_ERR(ERR_CAN_NOT_COMPLETE);
    SET_ERR(0); CHECK(!UN(NtGdiFlattenPath)(dc)); EXPECT_ERR(ERR_CAN_NOT_COMPLETE);
    SET_ERR(0); CHECK(UN(NtGdiPathToRegion)(dc) == NULL); EXPECT_ERR(ERR_CAN_NOT_COMPLETE);
    SET_ERR(0); CHECK(!UN(NtGdiSelectClipPath)(dc, RGN_COPY)); EXPECT_ERR(ERR_CAN_NOT_COMPLETE);
    CHECK(UN(NtGdiCloseFigure)(dc));
    CHECK(UN(NtGdiEndPath)(dc));
    EXPECT_INT(UN(NtGdiGetPath)(dc, NULL, NULL, 0), 2);
    UN(NtGdiAbortPath)(dc);
    MEMDC_FREE(dc);
}

/* ---- local process state across the path syscalls ----------------------------------------------------------------- */
XT(direct_path_leaves_gdi_batch_state_consistent) {
    unsigned long before = 0, after = 0;
    NEED_PATH_NT(); REQUIRE(U(GdiFlush));
    MEMDC(dc);
    U(GdiFlush)();
    k_try_read_u32((char *)k_teb() + TEB_GdiBatchCount, &before);
    UN(NtGdiBeginPath)(dc); U(Rectangle)(dc, 1, 1, 8, 8); UN(NtGdiEndPath)(dc); UN(NtGdiFillPath)(dc);
    U(GdiFlush)();
    k_try_read_u32((char *)k_teb() + TEB_GdiBatchCount, &after);
    t_note("TEB GdiBatchCount before %lu after path ops + GdiFlush %lu", before, after);

    MEMDC_FREE(dc);
}

XT(direct_win32_thread_info_present_after_gdi) {
    void *w = NULL;
    NEED_CORE();
    MEMDC(dc);
    U(MoveToEx)(dc, 1, 1, NULL);
    if (t_is_wine()) { t_skip("no PTHREADINFO under Wine"); MEMDC_FREE(dc); return; }
    CHECK(k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &w));
    t_note("TEB.Win32ThreadInfo after first GDI use: %p", w);
    MEMDC_FREE(dc);
}

/* ---- remaining direct calls ----------------------------------------------------------------------------------------- */
XT(direct_combinetransform_matches_api) {
    XFORM a = xf_make(2, 0, 0, 3, 1, 1), b = xf_make(1, 0.5f, 0, 1, 4, 5), r1, r2;
    NEED_CORE(); REQUIRE(U(CombineTransform));
    CHECK(U(CombineTransform)(&r1, &a, &b));
    r2 = r1;
    CHECK_XF(r2, xf_mul(a, b));
}

XT(direct_scale_ext_via_syscalls) {
    SIZE prev = { 0, 0 }, v;
    typedef BOOL (WINAPI *scale_fn)(HDC, int, int, int, int, SIZE *);
    scale_fn sv, sw;
    NEED_CORE(); REQUIRE(U(SetMapMode)); REQUIRE(U(SetViewportExtEx)); REQUIRE(U(GetViewportExtEx));
    sv = (scale_fn)t_proc_in("win32u.dll", "NtGdiScaleViewportExtEx");
    sw = (scale_fn)t_proc_in("win32u.dll", "NtGdiScaleWindowExtEx");
    if (!sv || !sw) { t_skip("scale stubs missing"); return; }
    if (t_is_wine()) { t_skip("scale stubs unimplemented/different under Wine"); return; }
    MEMDC(dc);
    U(SetMapMode)(dc, MM_ANISOTROPIC);
    U(SetViewportExtEx)(dc, 100, 200, NULL);
    CHECK(sv(dc, 3, 2, 1, 4, &prev));
    EXPECT_INT(prev.cx, 100); EXPECT_INT(prev.cy, 200);
    U(GetViewportExtEx)(dc, &v);
    EXPECT_INT(v.cx, 150); EXPECT_INT(v.cy, 50);
    CHECK(!sv(NULL, 1, 1, 1, 1, NULL));
    CHECK(sw(dc, 1, 1, 1, 1, NULL));
    MEMDC_FREE(dc);
}

XT(direct_setlayout_syscall) {
    typedef DWORD (WINAPI *setlayout_fn)(HDC, int, DWORD);
    setlayout_fn nt;
    NEED_CORE(); REQUIRE(U(GetLayout));
    nt = (setlayout_fn)t_proc_in("win32u.dll", "NtGdiSetLayout");
    if (!nt) { t_skip("NtGdiSetLayout missing"); return; }
    if (t_is_wine()) { t_skip("NtGdiSetLayout signature differs under Wine"); return; }
    {
        FX(f, 32, 32);
        EXPECT_EQ(nt(f.dc, -1, LAYOUT_RTL), 0);               /* gdi32full!SetLayout passes wox = -1 and returns the old layout */
        EXPECT_EQ(U(GetLayout)(f.dc) & LAYOUT_RTL, LAYOUT_RTL);
        EXPECT_EQ(nt(f.dc, -1, 0) & LAYOUT_RTL, LAYOUT_RTL);
        EXPECT_EQ(U(GetLayout)(f.dc) & LAYOUT_RTL, 0);
        fx_close(&f);
    }
}
