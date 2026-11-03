/* NtGdiCreateColorSpace, NtGdiSetColorSpace, NtGdiCreateColorTransform, NtGdiDeleteColorTransform, NtGdiIcmBrushInfo,
 * NtGdiGetObjectBitmapHandle.
 *
 * Colour spaces are GDI objects of type 9
 * (type 9 in the handle). NtGdiSetColorSpace stores the handle at DC_ATTR+0x10, which gdi32full!GetColorSpace reads
 * from GDICELL.pUser, so the effect is visible from user mode without another syscall. */
#include "gap0.h"

#define LCS_SIG 0x50534f43u   /* 'PSOC' */
struct lcsx { union { unsigned char raw[0x250]; DWORD d[0x94]; } u; };   /* LOGCOLORSPACEEXW = LOGCOLORSPACEW (0x24c) + dwFlags */
static void lcs_fill(struct lcsx *x, const WCHAR *file) {
    memset(x, 0, sizeof *x);
    x->u.d[0] = LCS_SIG; x->u.d[1] = 0x400; x->u.d[2] = 0x24c;
    x->u.d[3] = 0x73524742;   /* LCS_sRGB */
    x->u.d[4] = 4;            /* LCS_GM_IMAGES */
    if (file) lstrcpynW((WCHAR *)(x->u.raw + 0x44), file, 260);
}

TEST(gap0_createcs_valid_makes_type9_object) {
    REQUIRE(UN(NtGdiCreateColorSpace)); REQUIRE(U(DeleteColorSpace));
    struct lcsx x; lcs_fill(&x, NULL);
    SET_ERR(0);
    HANDLE h = UN(NtGdiCreateColorSpace)(&x);
    CHECKF(h != NULL, "NtGdiCreateColorSpace failed, err %lu", GetLastError());
    if (!h) return;
    EXPECT_EQ(((ULONG_PTR)h >> 16) & 0x7f, 9);   /* object type 9 is encoded in the handle */
    int ty = g0_cell_u8(h, CELL_TYPE);
    if (ty >= 0) EXPECT_EQ(ty, 9);
    EXPECT_EQ(U(DeleteColorSpace)(h), TRUE);
}
TEST(gap0_createcs_distinct_handles_and_delete_all) {
    REQUIRE(UN(NtGdiCreateColorSpace)); REQUIRE(U(DeleteColorSpace));
    HANDLE hs[8]; struct lcsx x; lcs_fill(&x, NULL);
    for (int i = 0; i < 8; i++) { x.u.d[4] = 1 + (i & 3); hs[i] = UN(NtGdiCreateColorSpace)(&x); CHECKF(hs[i] != NULL, "create %d", i); }
    for (int i = 0; i < 8; i++) for (int j = i + 1; j < 8; j++) CHECKF(!hs[i] || !hs[j] || hs[i] != hs[j], "duplicate handle %d/%d", i, j);
    for (int i = 0; i < 8; i++) if (hs[i]) EXPECT_EQ(U(DeleteColorSpace)(hs[i]), TRUE);
}
TEST(gap0_createcs_deleted_handle_is_dead) {
    REQUIRE(UN(NtGdiCreateColorSpace)); REQUIRE(U(DeleteColorSpace));
    struct lcsx x; lcs_fill(&x, NULL);
    HANDLE h = UN(NtGdiCreateColorSpace)(&x);
    REQUIRE(h);
    EXPECT_EQ(U(DeleteColorSpace)(h), TRUE);
    SET_ERR(0);
    EXPECT_EQ(U(DeleteColorSpace)(h), FALSE);
    /* gdi32!GetObjectType does not validate against the kernel table (a freed handle still reports the type encoded in it),
     * so liveness is observed through the second delete above and through NtGdiSetColorSpace below. */
    G0FX(f, 2, 2);
    REQUIRE(UN(NtGdiSetColorSpace));
    EXPECT_EQ(UN(NtGdiSetColorSpace)(f.dc, h), 0);   /* a freed cell fails the type-9 lookup */
    g0fx_close(&f);
}
/* each of the three header words is validated at creation; failure is ERROR_INVALID_PARAMETER and NULL */
TEST(gap0_createcs_bad_header_words) {
    REQUIRE(UN(NtGdiCreateColorSpace));
    static const struct { unsigned w; DWORD v; } bad[] = {
        { 0, 0 }, { 0, 0x50534f44 }, { 0, 0xffffffff }, { 0, 0x434f5350 },    /* signature */
        { 1, 0 }, { 1, 0x300 }, { 1, 0x401 }, { 1, 0x40000 },                 /* version */
        { 2, 0 }, { 2, 0x24b }, { 2, 0x24d }, { 2, 0x250 }, { 2, 0x1000 },    /* size */
    };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        struct lcsx x; lcs_fill(&x, NULL);
        x.u.d[bad[i].w] = bad[i].v;
        SET_ERR(0);
        HANDLE h = UN(NtGdiCreateColorSpace)(&x);
        CHECKF(h == NULL, "word %u = 0x%lx accepted", bad[i].w, (unsigned long)bad[i].v);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
        if (h && U(DeleteColorSpace)) U(DeleteColorSpace)(h);
    }
}
TEST(gap0_createcs_ignores_type_and_intent_values) {
    /* the kernel copies csType/intent/endpoints/gamma verbatim without validating them */
    REQUIRE(UN(NtGdiCreateColorSpace)); REQUIRE(U(DeleteColorSpace));
    static const DWORD types[] = { 0, 1, 0x73524742, 0x57696e20, 0x50524f46, 0x4d424544, 0xffffffff };
    for (unsigned i = 0; i < sizeof types / sizeof *types; i++) {
        struct lcsx x; lcs_fill(&x, NULL);
        x.u.d[3] = types[i];
        HANDLE h = UN(NtGdiCreateColorSpace)(&x);
        CHECKF(h != NULL, "csType 0x%lx rejected", (unsigned long)types[i]);
        if (h) U(DeleteColorSpace)(h);
    }
}
TEST(gap0_createcs_long_filename_is_truncated_not_rejected) {
    /* RtlStringCchCopyW(dst+0x5c, 0x104, src+0x44) copies at most 260 WCHARs; a full non-terminated field must not fail the call */
    REQUIRE(UN(NtGdiCreateColorSpace)); REQUIRE(U(DeleteColorSpace));
    struct lcsx x; lcs_fill(&x, NULL);
    WCHAR *fn = (WCHAR *)(x.u.raw + 0x44);
    for (int i = 0; i < 260; i++) fn[i] = L'a';
    HANDLE h = UN(NtGdiCreateColorSpace)(&x);
    CHECKF(h != NULL, "260 non-terminated characters");
    if (h) U(DeleteColorSpace)(h);
}
TEST(gap0_createcs_roundtrip_through_getlogcolorspace) {
    REQUIRE(UN(NtGdiCreateColorSpace)); REQUIRE(U(DeleteColorSpace));
    HMODULE g = t_module("gdi32.dll");
    BOOL (WINAPI *GetLogCS)(HCOLORSPACE, LPLOGCOLORSPACEW, DWORD) = g ? (void *)GetProcAddress(g, "GetLogColorSpaceW") : NULL;
    REQUIRE(GetLogCS);
    struct lcsx x; lcs_fill(&x, L"win32kt-gap0.icm");
    x.u.d[4] = 2;
    HANDLE h = UN(NtGdiCreateColorSpace)(&x);
    REQUIRE(h);
    LOGCOLORSPACEW out; memset(&out, 0xAB, sizeof out);
    BOOL ok = GetLogCS((HCOLORSPACE)h, &out, sizeof out);
    CHECKF(ok, "GetLogColorSpaceW err %lu", GetLastError());
    if (ok) {
        t_note("colorspace round trip: type 0x%lx intent %ld sig 0x%lx ver 0x%lx size %lu file \"%ls\"",
               (unsigned long)out.lcsCSType, (long)out.lcsIntent, (unsigned long)out.lcsSignature, (unsigned long)out.lcsVersion, (unsigned long)out.lcsSize, out.lcsFilename);
        CHECK(out.lcsCSType == 0x73524742);
        CHECK(out.lcsIntent == 2);
    }
    U(DeleteColorSpace)(h);
}
TEST(gap0_createcs_user_path_createcolorspacew) {
    REQUIRE(U(CreateColorSpaceW)); REQUIRE(U(DeleteColorSpace));
    LOGCOLORSPACEW l; memset(&l, 0, sizeof l);
    l.lcsSignature = 0x50534f43; l.lcsVersion = 0x400; l.lcsSize = sizeof l; l.lcsCSType = 0x73524742; l.lcsIntent = LCS_GM_IMAGES;
    HCOLORSPACE h = U(CreateColorSpaceW)(&l);
    CHECKF(h != NULL, "CreateColorSpaceW err %lu", GetLastError());
    if (!h) return;
    EXPECT_EQ(((ULONG_PTR)h >> 16) & 0x7f, 9);
    int ty = g0_cell_u8(h, CELL_TYPE);
    if (ty >= 0) EXPECT_EQ(ty, 9);
    EXPECT_EQ(U(DeleteColorSpace)(h), TRUE);
}

/* ---- NtGdiSetColorSpace(hdc, hcs) ----------------------------------------------------------------------- */
TEST(gap0_setcs_bad_dc_returns_zero) {
    REQUIRE(UN(NtGdiSetColorSpace));
    for (unsigned i = 0; i < G0_NBAD; i++)
        CHECKF(UN(NtGdiSetColorSpace)((HDC)g0_bad_handles[i], (HANDLE)0x1234) == 0, "handle 0x%llx", (unsigned long long)g0_bad_handles[i]);
}
TEST(gap0_setcs_non_colorspace_handle_returns_zero) {
    REQUIRE(UN(NtGdiSetColorSpace)); REQUIRE(U(GetColorSpace)); REQUIRE(U(CreateSolidBrush));
    G0FX(f, 2, 2);
    HGDIOBJ cur = (HGDIOBJ)U(GetColorSpace)(f.dc);
    HGDIOBJ objs[5] = { f.bmp, U(CreateSolidBrush)(RGB(9, 9, 9)), U(GetStockObject)(BLACK_PEN), (HGDIOBJ)f.dc, (HGDIOBJ)(ULONG_PTR)0xdeadbeef };
    for (int i = 0; i < 5; i++) {
        if (objs[i] == cur) continue;   /* storing the current value is the documented no-op success */
        CHECKF(UN(NtGdiSetColorSpace)(f.dc, objs[i]) == 0, "object %d accepted as a colour space", i);
    }
    EXPECT_EQ((ULONG_PTR)U(GetColorSpace)(f.dc), (ULONG_PTR)cur);
    U(DeleteObject)(objs[1]);
    g0fx_close(&f);
}
TEST(gap0_setcs_same_as_current_is_success_noop) {
    REQUIRE(UN(NtGdiSetColorSpace)); REQUIRE(U(GetColorSpace));
    G0FX(f, 2, 2);
    HCOLORSPACE cur = U(GetColorSpace)(f.dc);
 /* argument 2 == DC_ATTR.hColorSpace returns 1 without touching ref counts, even when the value is NULL */
    EXPECT_EQ(UN(NtGdiSetColorSpace)(f.dc, cur), 1);
    EXPECT_EQ((ULONG_PTR)U(GetColorSpace)(f.dc), (ULONG_PTR)cur);
    g0fx_close(&f);
}
TEST(gap0_setcs_stores_handle_in_dcattr) {
    REQUIRE(UN(NtGdiSetColorSpace)); REQUIRE(UN(NtGdiCreateColorSpace)); REQUIRE(U(GetColorSpace)); REQUIRE(U(DeleteColorSpace));
    G0FX(f, 2, 2);
    struct lcsx x; lcs_fill(&x, NULL);
    HANDLE c1 = UN(NtGdiCreateColorSpace)(&x), c2 = UN(NtGdiCreateColorSpace)(&x);
    REQUIRE(c1 && c2);
    void *a = g0_dca(f.dc);
    EXPECT_EQ(UN(NtGdiSetColorSpace)(f.dc, c1), 1);
    EXPECT_EQ((ULONG_PTR)U(GetColorSpace)(f.dc), (ULONG_PTR)c1);
    if (a) EXPECT_EQ((ULONG_PTR)g0_rdp((char *)a + DCA_COLORSPACE), (ULONG_PTR)c1);
    EXPECT_EQ(UN(NtGdiSetColorSpace)(f.dc, c1), 1);    /* again: same as current */
    EXPECT_EQ(UN(NtGdiSetColorSpace)(f.dc, c2), 1);
    EXPECT_EQ((ULONG_PTR)U(GetColorSpace)(f.dc), (ULONG_PTR)c2);
    if (a) EXPECT_EQ((ULONG_PTR)g0_rdp((char *)a + DCA_COLORSPACE), (ULONG_PTR)c2);
    EXPECT_EQ(UN(NtGdiSetColorSpace)(f.dc, c1), 1);
    EXPECT_EQ((ULONG_PTR)U(GetColorSpace)(f.dc), (ULONG_PTR)c1);
    g0fx_close(&f);                                    /* the DC releases its share reference before the objects go */
    EXPECT_EQ(U(DeleteColorSpace)(c1), TRUE);
    EXPECT_EQ(U(DeleteColorSpace)(c2), TRUE);
}
TEST(gap0_setcs_same_colorspace_on_two_dcs) {
    REQUIRE(UN(NtGdiSetColorSpace)); REQUIRE(UN(NtGdiCreateColorSpace)); REQUIRE(U(GetColorSpace)); REQUIRE(U(DeleteColorSpace));
    G0FX(f, 2, 2);
    HDC d2 = U(CreateCompatibleDC)(NULL);
    REQUIRE(d2);
    struct lcsx x; lcs_fill(&x, NULL);
    HANDLE c = UN(NtGdiCreateColorSpace)(&x);
    REQUIRE(c);
    EXPECT_EQ(UN(NtGdiSetColorSpace)(f.dc, c), 1);
    EXPECT_EQ(UN(NtGdiSetColorSpace)(d2, c), 1);
    EXPECT_EQ((ULONG_PTR)U(GetColorSpace)(f.dc), (ULONG_PTR)c);
    EXPECT_EQ((ULONG_PTR)U(GetColorSpace)(d2), (ULONG_PTR)c);
    U(DeleteDC)(d2);
    EXPECT_EQ((ULONG_PTR)U(GetColorSpace)(f.dc), (ULONG_PTR)c);   /* the first DC still holds its own reference */
    g0fx_close(&f);
    EXPECT_EQ(U(DeleteColorSpace)(c), TRUE);
}
TEST(gap0_setcs_user_path_setcolorspace) {
    REQUIRE(U(SetColorSpace)); REQUIRE(UN(NtGdiCreateColorSpace)); REQUIRE(U(GetColorSpace)); REQUIRE(U(DeleteColorSpace));
    G0FX(f, 2, 2);
    struct lcsx x; lcs_fill(&x, NULL);
    HANDLE c = UN(NtGdiCreateColorSpace)(&x);
    REQUIRE(c);
    HCOLORSPACE prev = U(GetColorSpace)(f.dc);
    HCOLORSPACE r = U(SetColorSpace)(f.dc, (HCOLORSPACE)c);
    t_note("SetColorSpace returned %p (previous %p)", (void *)r, (void *)prev);
    CHECKF(r != NULL || prev == NULL || GetLastError() != 0, "SetColorSpace failed without error");
    EXPECT_EQ((ULONG_PTR)U(GetColorSpace)(f.dc), (ULONG_PTR)c);
    void *a = g0_dca(f.dc);
    if (a) EXPECT_EQ((ULONG_PTR)g0_rdp((char *)a + DCA_COLORSPACE), (ULONG_PTR)c);
    g0fx_close(&f);
    U(DeleteColorSpace)(c);
}

/* ---- NtGdiCreateColorTransform(hdc, LOGCOLORSPACEW*, src, cbSrc, dst, cbDst, target, cbTarget) --------------------- */
TEST(gap0_createxform_null_logcolorspace_returns_null) {
    REQUIRE(UN(NtGdiCreateColorTransform));
    G0FX(f, 2, 2);
    EXPECT_EQ((ULONG_PTR)UN(NtGdiCreateColorTransform)(f.dc, NULL, NULL, 0, NULL, 0, NULL, 0), 0);
    EXPECT_EQ((ULONG_PTR)UN(NtGdiCreateColorTransform)(NULL, NULL, NULL, 0, NULL, 0, NULL, 0), 0);
    g0fx_close(&f);
}
TEST(gap0_createxform_bad_header_rejected_with_87) {
    REQUIRE(UN(NtGdiCreateColorTransform));
    G0FX(f, 2, 2);
    static const struct { unsigned w; DWORD v; } bad[] = { {0, 0}, {0, 0x50534f44}, {1, 0}, {1, 0x401}, {2, 0}, {2, 0x250} };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        struct lcsx x; lcs_fill(&x, NULL);
        x.u.d[bad[i].w] = bad[i].v;
        SET_ERR(0);
        HANDLE h = UN(NtGdiCreateColorTransform)(f.dc, &x, NULL, 0, NULL, 0, NULL, 0);
        CHECKF(h == NULL, "word %u = 0x%lx accepted", bad[i].w, (unsigned long)bad[i].v);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
    g0fx_close(&f);
}
TEST(gap0_createxform_valid_header_bad_dc_rejected_with_87) {
    REQUIRE(UN(NtGdiCreateColorTransform));
    struct lcsx x; lcs_fill(&x, NULL);
    for (unsigned i = 0; i < G0_NBAD; i++) {
        SET_ERR(0);
        HANDLE h = UN(NtGdiCreateColorTransform)((HDC)g0_bad_handles[i], &x, NULL, 0, NULL, 0, NULL, 0);
        CHECKF(h == NULL, "handle 0x%llx", (unsigned long long)g0_bad_handles[i]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);   /* XDCOBJ::vLock failed -> EngSetLastError(0x57) */
    }
}

/* ---- NtGdiDeleteColorTransform(hdc, hxform) -------------------------------------------------------------------- */
TEST(gap0_deletexform_bad_dc_zero) {
    REQUIRE(UN(NtGdiDeleteColorTransform));
    for (unsigned i = 0; i < G0_NBAD; i++)
        CHECKF(UN(NtGdiDeleteColorTransform)((HDC)g0_bad_handles[i], (HANDLE)0x1234) == 0, "handle 0x%llx", (unsigned long long)g0_bad_handles[i]);
}
TEST(gap0_deletexform_bogus_transform_zero) {
    REQUIRE(UN(NtGdiDeleteColorTransform));
    G0FX(f, 2, 2);
    static const ULONG_PTR hs[] = { 0, 1, 0x1234, 0xdeadbeef, 0xffffffff };
    for (unsigned i = 0; i < sizeof hs / sizeof *hs; i++)
        CHECKF(UN(NtGdiDeleteColorTransform)(f.dc, (HANDLE)hs[i]) == 0, "transform 0x%llx", (unsigned long long)hs[i]);
    /* a GDI object of another type (type mismatch) */
    CHECK(UN(NtGdiDeleteColorTransform)(f.dc, f.bmp) == 0);
    CHECK(UN(NtGdiDeleteColorTransform)(f.dc, (HANDLE)f.dc) == 0);
    g0fx_close(&f);
}

/* ---- NtGdiIcmBrushInfo(hdc, hbr, pbmi, pbits, pcbBits, pcolors, pvalid, bSet) -------------------------------------- */
TEST(gap0_icmbrush_bad_mode_is_87) {
    REQUIRE(UN(NtGdiIcmBrushInfo)); REQUIRE(U(CreateSolidBrush));
    G0FX(f, 2, 2);
    HBRUSH b = U(CreateSolidBrush)(RGB(1, 2, 3));
    static const INT modes[] = { 2, 3, -1, 0x100, 0x7fffffff, (INT)0x80000000 };
    for (unsigned i = 0; i < sizeof modes / sizeof *modes; i++) {
        SET_ERR(0);
        CHECKF(UN(NtGdiIcmBrushInfo)(f.dc, b, NULL, NULL, NULL, NULL, NULL, modes[i]) == 0, "bSet %d", modes[i]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
    U(DeleteObject)(b);
    g0fx_close(&f);
}
TEST(gap0_icmbrush_query_without_bitmapinfo_returns_zero) {
    /* the query returns 0 at once when pbmi, pulColors, pbValid or pcbBits is NULL; the wrapper copies nothing out */
    REQUIRE(UN(NtGdiIcmBrushInfo)); REQUIRE(U(CreateSolidBrush));
    G0FX(f, 2, 2);
    HBRUSH b = U(CreateSolidBrush)(RGB(1, 2, 3));
    ULONG cb = 0x77777777, col = 0x66666666; INT valid = 0x55555555;
    EXPECT_EQ(UN(NtGdiIcmBrushInfo)(f.dc, b, NULL, NULL, &cb, &col, &valid, 0), 0);
    EXPECT_EQ(cb, 0x77777777); EXPECT_EQ(col, 0x66666666); EXPECT_EQ((ULONG)valid, 0x55555555);
    EXPECT_EQ(UN(NtGdiIcmBrushInfo)(f.dc, b, NULL, NULL, NULL, NULL, NULL, 0), 0);
    U(DeleteObject)(b);
    g0fx_close(&f);
}
TEST(gap0_icmbrush_query_plain_dc_solid_brush_returns_zero_and_writes_nothing) {
    /* the DC has ICM off (DC+0x78 bit 0 clear), so the query leaves everything zero and the wrapper copies nothing out */
    REQUIRE(UN(NtGdiIcmBrushInfo)); REQUIRE(U(CreateSolidBrush));
    G0FX(f, 2, 2);
    HBRUSH b = U(CreateSolidBrush)(RGB(1, 2, 3));
    DWORD bmi[0x430 / 4]; memset(bmi, 0x5a, sizeof bmi);
    ULONG cb = 0x77777777, col = 0x66666666; INT valid = 0x55555555;
    EXPECT_EQ(UN(NtGdiIcmBrushInfo)(f.dc, b, bmi, NULL, &cb, &col, &valid, 0), 0);
    CHECK(g0_all(bmi, sizeof bmi, 0x5a));
    EXPECT_EQ(cb, 0x77777777); EXPECT_EQ(col, 0x66666666); EXPECT_EQ((ULONG)valid, 0x55555555);
    U(DeleteObject)(b);
    g0fx_close(&f);
}
TEST(gap0_icmbrush_query_bad_dc_returns_zero) {
    REQUIRE(UN(NtGdiIcmBrushInfo)); REQUIRE(U(CreateSolidBrush));
    HBRUSH b = U(CreateSolidBrush)(RGB(1, 2, 3));
    DWORD bmi[0x430 / 4]; memset(bmi, 0x5a, sizeof bmi);
    ULONG cb = 0x77777777, col = 0x66666666; INT valid = 0x55555555;
    for (unsigned i = 0; i < G0_NBAD; i++)
        CHECKF(UN(NtGdiIcmBrushInfo)((HDC)g0_bad_handles[i], b, bmi, NULL, &cb, &col, &valid, 0) == 0, "handle 0x%llx", (unsigned long long)g0_bad_handles[i]);
    CHECK(g0_all(bmi, sizeof bmi, 0x5a));
    U(DeleteObject)(b);
}
TEST(gap0_icmbrush_set_with_empty_bitmapinfo_returns_zero) {
    /* bSet == 1 needs a nonzero required bitmap size that is <= *pcbBits; with *pcbBits == 0 nothing can qualify */
    REQUIRE(UN(NtGdiIcmBrushInfo)); REQUIRE(U(CreateSolidBrush));
    G0FX(f, 2, 2);
    HBRUSH b = U(CreateSolidBrush)(RGB(1, 2, 3));
    DWORD bmi[0x430 / 4]; memset(bmi, 0, sizeof bmi);
    ULONG cb = 0;
    EXPECT_EQ(UN(NtGdiIcmBrushInfo)(f.dc, b, bmi, bmi, &cb, NULL, NULL, 1), 0);
    U(DeleteObject)(b);
    g0fx_close(&f);
}

/* ---- NtGdiGetObjectBitmapHandle(hbrush, &usage) ---------------------------------------------------------------------
 * Only handles whose type bits ((h & 0x7f0000) - 0x100000 with bit 22 ignored) are brush/ext-pen reach the bitmap-handle lookup.
 * Every other handle value is returned unchanged and *pusage is not written. */
TEST(gap0_objbitmaphandle_other_types_echo_the_handle) {
    REQUIRE(UN(NtGdiGetObjectBitmapHandle)); REQUIRE(U(CreateBitmap)); REQUIRE(U(CreateFontIndirectW)); REQUIRE(U(CreateRectRgn)); REQUIRE(U(CreatePen));
    G0FX(f, 2, 2);
    LOGFONTW lf; memset(&lf, 0, sizeof lf); lf.lfHeight = -12; lstrcpyW(lf.lfFaceName, L"Arial");
    HGDIOBJ o[6] = { f.bmp, U(CreateBitmap)(2, 2, 1, 1, NULL), U(CreateFontIndirectW)(&lf), U(CreateRectRgn)(0, 0, 3, 3), U(CreatePen)(PS_SOLID, 1, 0), (HGDIOBJ)f.dc };
    for (int i = 0; i < 6; i++) {
        CHECKF(o[i] != NULL, "object %d", i);
        if (!o[i]) continue;
        ULONG u = 0x12345678;
        ULONG_PTR r = UN(NtGdiGetObjectBitmapHandle)(o[i], &u);
        if (((ULONG_PTR)o[i] & 0x7f0000) == 0x100000 || ((ULONG_PTR)o[i] & 0x7f0000) == 0x500000) { t_note("object %d has brush-like type bits", i); continue; }
        CHECKF(r == (ULONG_PTR)o[i], "object %d: returned 0x%llx want the handle 0x%llx", i, (unsigned long long)r, (unsigned long long)(ULONG_PTR)o[i]);
        EXPECT_EQ(u, 0x12345678);
    }
    for (int i = 1; i < 5; i++) if (o[i]) U(DeleteObject)(o[i]);
    g0fx_close(&f);
}
TEST(gap0_objbitmaphandle_null_returns_zero) {
    REQUIRE(UN(NtGdiGetObjectBitmapHandle));
    ULONG u = 0x12345678;
    EXPECT_EQ(UN(NtGdiGetObjectBitmapHandle)(NULL, &u), 0);
    EXPECT_EQ(u, 0x12345678);
}
TEST(gap0_objbitmaphandle_pattern_brush_returns_bitmap_handle) {
    REQUIRE(UN(NtGdiGetObjectBitmapHandle)); REQUIRE(U(CreatePatternBrush));
    G0FX(f, 8, 8);
    HBITMAP pat = g0_dib(f.dc, 8, 8, 32);
    REQUIRE(pat);
    HBRUSH b = U(CreatePatternBrush)(pat);
    REQUIRE(b);
    ULONG u = 0x12345678;
    ULONG_PTR r = UN(NtGdiGetObjectBitmapHandle)(b, &u);
    CHECKF(r != 0, "pattern brush returned no bitmap");
    if (r) {
        CHECK(r != (ULONG_PTR)b);
        CHECKF(u <= 2, "usage %lu (0, 1 or 2 are the only values the lookup stores)", u);
        t_note("pattern brush %p: bitmap %p (source %p), usage %lu, type bits 0x%llx", (void *)b, (void *)r, (void *)pat, u, (unsigned long long)((r >> 16) & 0x7f));
    }
    U(DeleteObject)(b); U(DeleteObject)(pat);
    g0fx_close(&f);
}
TEST(gap0_objbitmaphandle_dib_pattern_brush_usage_values) {
    REQUIRE(UN(NtGdiGetObjectBitmapHandle)); REQUIRE(U(CreateDIBPatternBrushPt));
    union { BITMAPINFO bi; char raw[sizeof(BITMAPINFOHEADER) + 8 * 4 * 8]; } u; memset(&u, 0, sizeof u);
    u.bi.bmiHeader.biSize = sizeof u.bi.bmiHeader; u.bi.bmiHeader.biWidth = 8; u.bi.bmiHeader.biHeight = 8;
    u.bi.bmiHeader.biPlanes = 1; u.bi.bmiHeader.biBitCount = 32; u.bi.bmiHeader.biCompression = BI_RGB;
    HBRUSH b = U(CreateDIBPatternBrushPt)(&u.bi, DIB_RGB_COLORS);
    REQUIRE(b);
    ULONG us = 0x12345678;
    ULONG_PTR r = UN(NtGdiGetObjectBitmapHandle)(b, &us);
    CHECKF(r != 0, "DIB pattern brush returned no bitmap");
    if (r) {
        CHECKF(us <= 2, "usage %lu", us);
        t_note("DIB pattern brush (DIB_RGB_COLORS): usage %lu", us);
    }
    U(DeleteObject)(b);
}
TEST(gap0_objbitmaphandle_user_path_pattern_brush_survives_select) {
    /* SelectObject of a pattern brush goes through gdi32full!SelectObjectImpl, which calls this syscall for brushes */
    REQUIRE(UN(NtGdiGetObjectBitmapHandle)); REQUIRE(U(CreatePatternBrush));
    G0FX(f, 8, 8);
    HBITMAP pat = g0_dib(f.dc, 8, 8, 32);
    REQUIRE(pat);
    HBRUSH b = U(CreatePatternBrush)(pat);
    REQUIRE(b);
    HGDIOBJ old = U(SelectObject)(f.dc, b);
    CHECK(old != NULL);
    EXPECT_EQ((ULONG_PTR)U(GetCurrentObject)(f.dc, OBJ_BRUSH), (ULONG_PTR)b);
    U(SelectObject)(f.dc, old);
    U(DeleteObject)(b); U(DeleteObject)(pat);
    g0fx_close(&f);
}
