/* World transforms, graphics mode, LPtoDP/DPtoLP and the kernel transform syscalls.
 *
 * gdi32full!ModifyWorldTransform refuses (returns 0, no error) unless DC_ATTR+0xd0 (graphics mode) == GM_ADVANCED;
 * win32kfull!NtGdiModifyWorldTransform handles modes 1 (identity), 2/3 (left/right multiply), 4 (set) and returns 0 for
 * everything else; NULL XFORM is only legal for MWT_IDENTITY. gdi32full!SetGraphicsMode stores 1/2 straight into
 * DC_ATTR+0xd0 and returns the previous mode, any other value gives ERROR_INVALID_PARAMETER and 0.
 * gdi32full!LPtoDP returns TRUE immediately for c < 1, fails with 0x57 for an invalid DC, and for DCs with a non-trivial
 * world transform (DC_ATTR+0x154 & 0xe000) forwards to NtGdiTransformPoints(hdc, in, out, c, 1).
 */
#include "xd.h"

#define MEMDC(dc) HDC dc = U(CreateCompatibleDC)(NULL); if (!dc) { t_skip("CreateCompatibleDC failed"); return; }
#define MEMDC_FREE(dc) U(DeleteDC)(dc)
#define NEED_XF() do { REQUIRE(U(SetGraphicsMode)); REQUIRE(U(GetGraphicsMode)); REQUIRE(U(SetWorldTransform)); \
    REQUIRE(U(GetWorldTransform)); REQUIRE(U(ModifyWorldTransform)); REQUIRE(U(CreateCompatibleDC)); } while (0)
#define ADV(dc) do { U(SetGraphicsMode)((dc), GM_ADVANCED); } while (0)

static const XFORM XID = { 1, 0, 0, 1, 0, 0 };

/* ---- graphics mode ----------------------------------------------------------------------------------------- */
XT(gm_default_is_compatible) {
    long v;
    NEED_XF();
    MEMDC(dc);
    EXPECT_EQ(U(GetGraphicsMode)(dc), GM_COMPATIBLE);
    if (xd_attr_i32(dc, DCATTR_GfxMode, &v)) EXPECT_INT(v, GM_COMPATIBLE);
    MEMDC_FREE(dc);
}

XT(gm_set_returns_previous) {
    NEED_XF();
    MEMDC(dc);
    EXPECT_EQ(U(SetGraphicsMode)(dc, GM_ADVANCED), GM_COMPATIBLE);
    EXPECT_EQ(U(GetGraphicsMode)(dc), GM_ADVANCED);
    EXPECT_EQ(U(SetGraphicsMode)(dc, GM_ADVANCED), GM_ADVANCED);   /* same mode: previous == requested */
    EXPECT_EQ(U(SetGraphicsMode)(dc, GM_COMPATIBLE), GM_ADVANCED); /* no transform yet, so going back is allowed */
    EXPECT_EQ(U(GetGraphicsMode)(dc), GM_COMPATIBLE);
    MEMDC_FREE(dc);
}

XT(gm_dcattr_mirrors_mode) {
    long v;
    NEED_XF();
    MEMDC(dc);
    if (!xd_attr_i32(dc, DCATTR_GfxMode, &v)) { t_skip("DC_ATTR not reachable"); MEMDC_FREE(dc); return; }
    ADV(dc);
    CHECK(xd_attr_i32(dc, DCATTR_GfxMode, &v)); EXPECT_INT(v, GM_ADVANCED);   /* SetGraphicsMode writes DC_ATTR+0xd0 directly */
    U(SetGraphicsMode)(dc, GM_COMPATIBLE);
    CHECK(xd_attr_i32(dc, DCATTR_GfxMode, &v)); EXPECT_INT(v, GM_COMPATIBLE);
    MEMDC_FREE(dc);
}

XT(gm_invalid_modes_rejected) {
    static const int bad[] = { 0, 3, 4, -1, 0x100, 0x10001, (int)0x80000000 };
    size_t i;
    NEED_XF();
    MEMDC(dc);
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SET_ERR(0);
        EXPECT_EQ(U(SetGraphicsMode)(dc, bad[i]), 0);     /* iMode-1U >= 2 -> GdiSetLastError(0x57), 0 */
        EXPECT_ERR(ERR_INVALID_PARAMETER);
        EXPECT_EQ(U(GetGraphicsMode)(dc), GM_COMPATIBLE);
    }
    MEMDC_FREE(dc);
}

XT(gm_invalid_hdc) {
    HDC bad[3];
    int i;
    NEED_XF();
    bad[0] = NULL; bad[1] = (HDC)(ULONG_PTR)0x01010fff; bad[2] = (HDC)U(GetStockObject)(BLACK_PEN);
    for (i = 0; i < 3; i++) {
        SET_ERR(0);
        EXPECT_EQ(U(SetGraphicsMode)(bad[i], GM_ADVANCED), 0);
        EXPECT_ERR(ERR_INVALID_PARAMETER);
    }
}

XT(gm_deleted_dc) {
    HDC dc;
    NEED_XF();
    dc = U(CreateCompatibleDC)(NULL);
    if (!dc) { t_skip("no DC"); return; }
    U(DeleteDC)(dc);
    SET_ERR(0);
    EXPECT_EQ(U(SetGraphicsMode)(dc, GM_ADVANCED), 0);
    EXPECT_ERR(ERR_INVALID_PARAMETER);
}

XT(gm_independent_per_dc) {
    NEED_XF();
    MEMDC(a); MEMDC(b);
    ADV(a);
    EXPECT_EQ(U(GetGraphicsMode)(a), GM_ADVANCED);
    EXPECT_EQ(U(GetGraphicsMode)(b), GM_COMPATIBLE);
    MEMDC_FREE(a); MEMDC_FREE(b);
}

XT(gm_restored_by_restoredc) {
    int sv;
    NEED_XF(); REQUIRE(U(SaveDC)); REQUIRE(U(RestoreDC));
    MEMDC(dc);
    sv = U(SaveDC)(dc);
    ADV(dc);
    EXPECT_EQ(U(GetGraphicsMode)(dc), GM_ADVANCED);
    CHECK(U(RestoreDC)(dc, sv));
    EXPECT_EQ(U(GetGraphicsMode)(dc), GM_COMPATIBLE);   /* graphics mode is part of the saved DC state */
    MEMDC_FREE(dc);
}

XT(gm_back_to_compat_with_transform) {
    XFORM got; long gm;
    NEED_XF();
    MEMDC(dc);
    ADV(dc);
    CHECK(U(SetWorldTransform)(dc, &(XFORM){ 2, 0, 0, 2, 5, 5 }));
    gm = U(SetGraphicsMode)(dc, GM_COMPATIBLE);
    t_note("SetGraphicsMode(COMPATIBLE) with a world transform returned %ld, now mode %d", gm, U(GetGraphicsMode)(dc));
    CHECK(gm == GM_ADVANCED || gm == 0);
    if (U(GetWorldTransform)(dc, &got))
        t_note("world transform after switching back: {%g %g %g %g %g %g}", got.eM11, got.eM12, got.eM21, got.eM22, got.eDx, got.eDy);
    MEMDC_FREE(dc);
}

/* ---- Set/Get/ModifyWorldTransform ------------------------------------------------------------------------ */
XT(world_get_identity_advanced) {
    XFORM x;
    NEED_XF();
    MEMDC(dc);
    ADV(dc);
    memset(&x, 0x77, sizeof x);
    CHECK(U(GetWorldTransform)(dc, &x));
    CHECK_XF(x, XID);
    MEMDC_FREE(dc);
}

XT(world_get_in_compat_mode) {
    XFORM x = xf_make(9, 9, 9, 9, 9, 9);
    BOOL r;
    NEED_XF();
    MEMDC(dc);
    r = U(GetWorldTransform)(dc, &x);
    t_note("GetWorldTransform in GM_COMPATIBLE: %d {%g %g %g %g %g %g}", (int)r, x.eM11, x.eM12, x.eM21, x.eM22, x.eDx, x.eDy);
    if (r) CHECK_XF(x, XID);
    MEMDC_FREE(dc);
}

XT(world_set_requires_advanced) {
    XFORM x = xf_make(2, 0, 0, 2, 1, 1);
    NEED_XF();
    MEMDC(dc);
    CHECK(!U(SetWorldTransform)(dc, &x));                    /* DC_ATTR graphics mode != 2 -> ModifyWorldTransform returns 0 */
    CHECK(!U(ModifyWorldTransform)(dc, &x, MWT_LEFTMULTIPLY));
    CHECK(!U(ModifyWorldTransform)(dc, &x, MWT_RIGHTMULTIPLY));
    CHECK(!U(ModifyWorldTransform)(dc, NULL, MWT_IDENTITY));
    MEMDC_FREE(dc);
}

XT(world_set_get_exact_floats) {
    static const XFORM t[] = {
        { 1, 0, 0, 1, 0, 0 }, { 2, 0, 0, 3, 10, 20 }, { 0.5f, 0, 0, 0.25f, -7.5f, 100.0f },
        { 0, 1, -1, 0, 0, 0 }, { -1, 0, 0, 1, 200, 0 }, { 1.5f, 0.75f, -0.5f, 2.25f, 3.0f, -4.0f },
        { 1, 0, 0, 1, 1234567, -7654321 },
    };
    size_t i; XFORM got;
    NEED_XF();
    MEMDC(dc);
    ADV(dc);
    for (i = 0; i < sizeof t / sizeof t[0]; i++) {
        memset(&got, 0, sizeof got);
        CHECKF(U(SetWorldTransform)(dc, &t[i]), "set #%u", (unsigned)i);
        CHECK(U(GetWorldTransform)(dc, &got));
        CHECK_XF(got, t[i]);                                 /* vConvertXformToMatrix stores single-precision floats */
    }
    MEMDC_FREE(dc);
}

XT(world_identity_set_matches_reset) {
    XFORM got;
    NEED_XF();
    MEMDC(dc);
    ADV(dc);
    CHECK(U(SetWorldTransform)(dc, &(XFORM){ 3, 0, 0, 3, 1, 2 }));
    CHECK(U(SetWorldTransform)(dc, &XID));
    CHECK(U(GetWorldTransform)(dc, &got));
    CHECK_XF(got, XID);
    MEMDC_FREE(dc);
}

XT(world_modify_identity_resets) {
    XFORM got;
    NEED_XF();
    MEMDC(dc);
    ADV(dc);
    CHECK(U(SetWorldTransform)(dc, &(XFORM){ 3, 1, 2, 3, 1, 2 }));
    CHECK(U(ModifyWorldTransform)(dc, NULL, MWT_IDENTITY));  /* NULL XFORM is accepted only for MWT_IDENTITY */
    CHECK(U(GetWorldTransform)(dc, &got));
    CHECK_XF(got, XID);
    CHECK(U(SetWorldTransform)(dc, &(XFORM){ 3, 1, 2, 3, 1, 2 }));
    CHECK(U(ModifyWorldTransform)(dc, &(XFORM){ 9, 9, 9, 9, 9, 9 }, MWT_IDENTITY));   /* a supplied matrix is ignored */
    CHECK(U(GetWorldTransform)(dc, &got));
    CHECK_XF(got, XID);
    MEMDC_FREE(dc);
}

typedef struct { const char *name; XFORM base; XFORM op; DWORD mode; } modrow_t;

XT(world_modify_left_right_multiply) {
    static const modrow_t t[] = {
        { "scale then translate-left", { 2, 0, 0, 2, 0, 0 }, { 1, 0, 0, 1, 10, 20 }, MWT_LEFTMULTIPLY },
        { "scale then translate-right", { 2, 0, 0, 2, 0, 0 }, { 1, 0, 0, 1, 10, 20 }, MWT_RIGHTMULTIPLY },
        { "rot left", { 1, 0, 0, 1, 4, 8 }, { 0, 1, -1, 0, 0, 0 }, MWT_LEFTMULTIPLY },
        { "rot right", { 1, 0, 0, 1, 4, 8 }, { 0, 1, -1, 0, 0, 0 }, MWT_RIGHTMULTIPLY },
        { "general left", { 1.5f, 0.5f, -0.25f, 2, 3, -1 }, { 0.5f, 2, 4, 1, 7, 9 }, MWT_LEFTMULTIPLY },
        { "general right", { 1.5f, 0.5f, -0.25f, 2, 3, -1 }, { 0.5f, 2, 4, 1, 7, 9 }, MWT_RIGHTMULTIPLY },
    };
    size_t i; XFORM got, want;
    NEED_XF();
    for (i = 0; i < sizeof t / sizeof t[0]; i++) {
        MEMDC(dc);
        ADV(dc);
        CHECK(U(SetWorldTransform)(dc, &t[i].base));
        CHECKF(U(ModifyWorldTransform)(dc, &t[i].op, t[i].mode), "%s modify", t[i].name);
        /* MWT_LEFTMULTIPLY: new = op x current; MWT_RIGHTMULTIPLY: new = current x op (row vectors, left operand first) */
        want = t[i].mode == MWT_LEFTMULTIPLY ? xf_mul(t[i].op, t[i].base) : xf_mul(t[i].base, t[i].op);
        CHECK(U(GetWorldTransform)(dc, &got));
        CHECKF(xf_eq(&got, &want), "%s: got {%g %g %g %g %g %g} want {%g %g %g %g %g %g}", t[i].name,
               got.eM11, got.eM12, got.eM21, got.eM22, got.eDx, got.eDy, want.eM11, want.eM12, want.eM21, want.eM22, want.eDx, want.eDy);
        MEMDC_FREE(dc);
    }
}

XT(world_modify_left_vs_right_translation_order) {
    XFORM got;
    NEED_XF();
    MEMDC(dc);
    ADV(dc);
    CHECK(U(SetWorldTransform)(dc, &(XFORM){ 2, 0, 0, 2, 0, 0 }));
    CHECK(U(ModifyWorldTransform)(dc, &(XFORM){ 1, 0, 0, 1, 10, 20 }, MWT_LEFTMULTIPLY));
    CHECK(U(GetWorldTransform)(dc, &got));
    EXPECT_EQ((int)got.eDx, 20); EXPECT_EQ((int)got.eDy, 40);    /* translate first, then scale */
    CHECK(U(ModifyWorldTransform)(dc, NULL, MWT_IDENTITY));
    CHECK(U(SetWorldTransform)(dc, &(XFORM){ 2, 0, 0, 2, 0, 0 }));
    CHECK(U(ModifyWorldTransform)(dc, &(XFORM){ 1, 0, 0, 1, 10, 20 }, MWT_RIGHTMULTIPLY));
    CHECK(U(GetWorldTransform)(dc, &got));
    EXPECT_EQ((int)got.eDx, 10); EXPECT_EQ((int)got.eDy, 20);    /* scale first, then translate */
    MEMDC_FREE(dc);
}

XT(world_modify_chain_of_translations) {
    XFORM got;
    int i;
    NEED_XF();
    MEMDC(dc);
    ADV(dc);
    for (i = 1; i <= 8; i++) CHECK(U(ModifyWorldTransform)(dc, &(XFORM){ 1, 0, 0, 1, (float)i, (float)(2 * i) }, MWT_RIGHTMULTIPLY));
    CHECK(U(GetWorldTransform)(dc, &got));
    CHECK_XF(got, xf_make(1, 0, 0, 1, 36, 72));
    MEMDC_FREE(dc);
}

XT(world_modify_invalid_modes) {
    static const DWORD bad[] = { 0, 5, 6, 7, 100, 0xffffffff, 0x80000001 };
    size_t i; XFORM got;
    NEED_XF();
    MEMDC(dc);
    ADV(dc);
    CHECK(U(SetWorldTransform)(dc, &(XFORM){ 2, 0, 0, 2, 3, 4 }));
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        CHECKF(!U(ModifyWorldTransform)(dc, &(XFORM){ 1, 0, 0, 1, 50, 50 }, bad[i]), "mode %lu accepted", (unsigned long)bad[i]);
    }
    CHECK(U(GetWorldTransform)(dc, &got));
    CHECK_XF(got, xf_make(2, 0, 0, 2, 3, 4));                /* nothing changed */
    MEMDC_FREE(dc);
}

XT(world_modify_null_xform_only_for_identity) {
    XFORM got;
    NEED_XF();
    MEMDC(dc);
    ADV(dc);
    CHECK(U(SetWorldTransform)(dc, &(XFORM){ 2, 0, 0, 2, 3, 4 }));
    CHECK(!U(ModifyWorldTransform)(dc, NULL, MWT_SET));
    CHECK(!U(ModifyWorldTransform)(dc, NULL, MWT_LEFTMULTIPLY));
    CHECK(!U(ModifyWorldTransform)(dc, NULL, MWT_RIGHTMULTIPLY));
    CHECK(!U(SetWorldTransform)(dc, NULL));
    CHECK(U(GetWorldTransform)(dc, &got));
    CHECK_XF(got, xf_make(2, 0, 0, 2, 3, 4));
    MEMDC_FREE(dc);
}

XT(world_invalid_hdc) {
    XFORM x = xf_make(1, 0, 0, 1, 0, 0), got = xf_make(7, 7, 7, 7, 7, 7);
    NEED_XF();
    CHECK(!U(SetWorldTransform)(NULL, &x));
    SET_ERR(0);
    CHECK(!U(ModifyWorldTransform)(NULL, &x, MWT_SET));
    EXPECT_ERR(ERR_INVALID_HANDLE);               /* type bits != DC: pldcGet() fails -> GdiSetLastError(6) */
    SET_ERR(0);
    CHECK(!U(ModifyWorldTransform)((HDC)(ULONG_PTR)0x01010fff, &x, MWT_SET));
    EXPECT_ERR(ERR_INVALID_PARAMETER);            /* DC-typed handle that fails GdiGetEntry validation -> 0x57 */
    CHECK(!U(GetWorldTransform)(NULL, &got));
    CHECK_XF(got, xf_make(7, 7, 7, 7, 7, 7));     /* output untouched on failure */
}

XT(world_singular_matrix) {
    XFORM z = xf_make(0, 0, 0, 0, 5, 6);
    POINT p = { 3, 4 }, q;
    BOOL set;
    NEED_XF(); REQUIRE(U(DPtoLP)); REQUIRE(U(LPtoDP));
    MEMDC(dc);
    ADV(dc);
    set = U(SetWorldTransform)(dc, &z);
    t_note("SetWorldTransform(singular) = %d", (int)set);
    if (set) {
        q = p;
        SET_ERR(0);
        CHECK(!U(DPtoLP)(dc, &q, 1));                /* the inverse does not exist */
        q = p;
        if (U(LPtoDP)(dc, &q, 1)) t_note("LPtoDP under singular matrix: (%ld,%ld)", q.x, q.y);
        else t_note("LPtoDP under singular matrix failed (err %lu)", GetLastError());
    }
    MEMDC_FREE(dc);
}

XT(world_restored_by_restoredc) {
    XFORM got; int sv;
    NEED_XF(); REQUIRE(U(SaveDC)); REQUIRE(U(RestoreDC));
    MEMDC(dc);
    ADV(dc);
    CHECK(U(SetWorldTransform)(dc, &(XFORM){ 2, 0, 0, 2, 3, 4 }));
    sv = U(SaveDC)(dc);
    CHECK(U(SetWorldTransform)(dc, &(XFORM){ 4, 0, 0, 4, 9, 9 }));
    CHECK(U(RestoreDC)(dc, sv));
    CHECK(U(GetWorldTransform)(dc, &got));
    CHECK_XF(got, xf_make(2, 0, 0, 2, 3, 4));
    MEMDC_FREE(dc);
}

XT(world_independent_per_dc) {
    XFORM got;
    NEED_XF();
    MEMDC(a); MEMDC(b);
    ADV(a); ADV(b);
    CHECK(U(SetWorldTransform)(a, &(XFORM){ 2, 0, 0, 2, 3, 4 }));
    CHECK(U(GetWorldTransform)(b, &got));
    CHECK_XF(got, XID);
    MEMDC_FREE(a); MEMDC_FREE(b);
}

/* ---- LPtoDP / DPtoLP with a world transform ------------------------------------------------------------------ */
XT(lptodp_world_scale_translate) {
    POINT p[3] = { {3, 4}, {0, 0}, {-5, 100} };
    NEED_XF(); REQUIRE(U(LPtoDP)); REQUIRE(U(DPtoLP));
    MEMDC(dc);
    ADV(dc);
    CHECK(U(SetWorldTransform)(dc, &(XFORM){ 2, 0, 0, 3, 10, 20 }));
    CHECK(U(LPtoDP)(dc, p, 3));
    EXPECT_EQ(p[0].x, 16); EXPECT_EQ(p[0].y, 32);
    EXPECT_EQ(p[1].x, 10); EXPECT_EQ(p[1].y, 20);
    EXPECT_EQ(p[2].x, 0);  EXPECT_EQ(p[2].y, 320);
    CHECK(U(DPtoLP)(dc, p, 3));
    EXPECT_EQ(p[0].x, 3); EXPECT_EQ(p[0].y, 4);
    EXPECT_EQ(p[1].x, 0); EXPECT_EQ(p[1].y, 0);
    EXPECT_EQ(p[2].x, -5); EXPECT_EQ(p[2].y, 100);
    MEMDC_FREE(dc);
}

XT(lptodp_world_rotation_90) {
    POINT p = { 3, 4 };
    NEED_XF(); REQUIRE(U(LPtoDP)); REQUIRE(U(DPtoLP));
    MEMDC(dc);
    ADV(dc);
    CHECK(U(SetWorldTransform)(dc, &(XFORM){ 0, 1, -1, 0, 0, 0 }));   /* (x,y) -> (-y, x) */
    CHECK(U(LPtoDP)(dc, &p, 1));
    EXPECT_EQ(p.x, -4); EXPECT_EQ(p.y, 3);
    CHECK(U(DPtoLP)(dc, &p, 1));
    EXPECT_EQ(p.x, 3); EXPECT_EQ(p.y, 4);
    MEMDC_FREE(dc);
}

XT(lptodp_world_shear_and_flip) {
    POINT p = { 10, 20 };
    NEED_XF(); REQUIRE(U(LPtoDP));
    MEMDC(dc);
    ADV(dc);
    CHECK(U(SetWorldTransform)(dc, &(XFORM){ -1, 0, 1, 1, 100, 0 }));  /* x' = -x + y + 100 ; y' = y */
    CHECK(U(LPtoDP)(dc, &p, 1));
    EXPECT_EQ(p.x, 110); EXPECT_EQ(p.y, 20);
    MEMDC_FREE(dc);
}

XT(lptodp_world_plus_viewport_origin) {
    POINT p = { 3, 4 };
    NEED_XF(); REQUIRE(U(LPtoDP)); REQUIRE(U(SetViewportOrgEx));
    MEMDC(dc);
    ADV(dc);
    CHECK(U(SetWorldTransform)(dc, &(XFORM){ 2, 0, 0, 2, 10, 10 }));
    U(SetViewportOrgEx)(dc, 100, 200, NULL);
    CHECK(U(LPtoDP)(dc, &p, 1));
    EXPECT_EQ(p.x, 116); EXPECT_EQ(p.y, 218);       /* world transform first, then page->device */
    MEMDC_FREE(dc);
}

XT(lptodp_count_edge_cases) {
    POINT p = { 7, 9 }, orig = { 7, 9 };
    NEED_XF(); REQUIRE(U(LPtoDP)); REQUIRE(U(DPtoLP));
    MEMDC(dc);
    ADV(dc);
    CHECK(U(SetWorldTransform)(dc, &(XFORM){ 2, 0, 0, 2, 0, 0 }));
    CHECK(U(LPtoDP)(dc, &p, 0));          /* c < 1 returns TRUE before looking at the DC */
    CHECK(U(LPtoDP)(dc, &p, -1));
    CHECK(U(DPtoLP)(dc, &p, 0));
    EXPECT_EQ(p.x, orig.x); EXPECT_EQ(p.y, orig.y);
    CHECK(U(LPtoDP)(NULL, &p, 0));        /* ... even for an invalid DC */
    MEMDC_FREE(dc);
}

XT(lptodp_invalid_hdc) {
    POINT p = { 7, 9 };
    NEED_XF(); REQUIRE(U(LPtoDP)); REQUIRE(U(DPtoLP));
    SET_ERR(0);
    CHECK(!U(LPtoDP)(NULL, &p, 1));
    EXPECT_ERR(ERR_INVALID_PARAMETER);    /* GdiSetLastError(0x57) in the validation-failure arm */
    SET_ERR(0);
    CHECK(!U(DPtoLP)(NULL, &p, 1));
    EXPECT_EQ(p.x, 7); EXPECT_EQ(p.y, 9);
}

XT(lptodp_many_points_alloc_path) {
    POINT in[40], out[40], dp[40];
    int i;
    NEED_XF(); REQUIRE(U(LPtoDP));
    MEMDC(dc);
    ADV(dc);
    CHECK(U(SetWorldTransform)(dc, &(XFORM){ 3, 0, 0, 2, 1, -1 }));
    for (i = 0; i < 40; i++) { in[i].x = i * 7 - 50; in[i].y = 300 - i * 11; dp[i] = in[i]; }
    CHECK(U(LPtoDP)(dc, dp, 40));        /* > 10 points: the kernel switches to a pool temp buffer */
    for (i = 0; i < 40; i++) { out[i].x = in[i].x * 3 + 1; out[i].y = in[i].y * 2 - 1; }
    CHECK(!memcmp(dp, out, sizeof dp));
    MEMDC_FREE(dc);
}

XT(combine_transform_order) {
    XFORM a = xf_make(2, 0, 0, 2, 0, 0), b = xf_make(1, 0, 0, 1, 10, 20), r;
    NEED_XF(); REQUIRE(U(CombineTransform));
    CHECK(U(CombineTransform)(&r, &a, &b));
    CHECK_XF(r, xf_mul(a, b));              /* a applied first, then b */
    CHECK_XF(r, xf_make(2, 0, 0, 2, 10, 20));
    CHECK(U(CombineTransform)(&r, &b, &a));
    CHECK_XF(r, xf_make(2, 0, 0, 2, 20, 40));
}

XT(combine_transform_identity_and_inplace) {
    XFORM a = xf_make(1.5f, 0.5f, -0.25f, 2, 3, -1), r, i = XID;
    NEED_XF(); REQUIRE(U(CombineTransform));
    CHECK(U(CombineTransform)(&r, &a, &i)); CHECK_XF(r, a);
    CHECK(U(CombineTransform)(&r, &i, &a)); CHECK_XF(r, a);
    r = a;
    CHECK(U(CombineTransform)(&r, &r, &a));             /* result may alias an operand */
    CHECK_XF(r, xf_mul(a, a));
}

XT(combine_transform_needs_no_dc) {
    XFORM a = xf_make(2, 0, 0, 2, 0, 0), b = xf_make(3, 0, 0, 3, 0, 0), r;
    NEED_XF(); REQUIRE(U(CombineTransform));
    CHECK(U(CombineTransform)(&r, &a, &b));
    CHECK_XF(r, xf_make(6, 0, 0, 6, 0, 0));
}

/* ---- world transform affects rendering ---------------------------------------------------------------------- */
XT(render_world_translate_line) {
    gdi_sel_t s = {0}; int x;
    NEED_XF(); REQUIRE(U(MoveToEx)); REQUIRE(U(LineTo));
    FX(f, 64, 64);
    ADV(f.dc);
    sel_pen(f.dc, &s, COL_RED, 1);
    CHECK(U(SetWorldTransform)(f.dc, &(XFORM){ 1, 0, 0, 1, 20, 30 }));
    U(MoveToEx)(f.dc, 0, 0, NULL); U(LineTo)(f.dc, 10, 0);
    for (x = 20; x < 30; x++) EXPECT_EQ(fx_px(&f, x, 30), PX_RED);
    EXPECT_EQ(fx_px(&f, 19, 30), PX_WHITE);
    EXPECT_EQ(fx_px(&f, 0, 0), PX_WHITE);
    EXPECT_INT(fx_count(&f, PX_RED), 10);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(render_world_scale_rectangle) {
    gdi_sel_t s = {0};
    int l, t, r, b;
    NEED_XF(); REQUIRE(U(Rectangle));
    FX(f, 64, 64);
    ADV(f.dc);
    sel_pen(f.dc, &s, COL_BLUE, 1); sel_brush(f.dc, &s, COL_BLUE);
    CHECK(U(SetWorldTransform)(f.dc, &(XFORM){ 2, 0, 0, 2, 0, 0 }));
    CHECK(U(Rectangle)(f.dc, 5, 5, 15, 15));
    EXPECT_EQ(fx_px(&f, 15, 15), PX_BLUE);
    EXPECT_EQ(fx_px(&f, 25, 25), PX_BLUE);
    EXPECT_EQ(fx_px(&f, 5, 5), PX_WHITE);
    EXPECT_EQ(fx_px(&f, 40, 40), PX_WHITE);
    fx_bbox(&f, PX_BLUE, &l, &t, &r, &b);
    t_note("scaled rectangle bbox (%d,%d)-(%d,%d)", l, t, r, b);
    CHECK(l >= 9 && l <= 10 && t >= 9 && t <= 10 && r >= 28 && r <= 30 && b >= 28 && b <= 30);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(render_world_identity_after_reset) {
    gdi_sel_t s = {0}; int x;
    NEED_XF(); REQUIRE(U(MoveToEx)); REQUIRE(U(LineTo));
    FX(f, 64, 64);
    ADV(f.dc);
    sel_pen(f.dc, &s, COL_RED, 1);
    CHECK(U(SetWorldTransform)(f.dc, &(XFORM){ 1, 0, 0, 1, 20, 30 }));
    CHECK(U(ModifyWorldTransform)(f.dc, NULL, MWT_IDENTITY));
    U(MoveToEx)(f.dc, 5, 5, NULL); U(LineTo)(f.dc, 15, 5);
    for (x = 5; x < 15; x++) EXPECT_EQ(fx_px(&f, x, 5), PX_RED);
    EXPECT_INT(fx_count(&f, PX_RED), 10);
    sel_done(f.dc, &s);
    fx_close(&f);
}

XT(render_world_flip_x) {
    gdi_sel_t s = {0}; int x;
    NEED_XF(); REQUIRE(U(MoveToEx)); REQUIRE(U(LineTo));
    FX(f, 64, 64);
    ADV(f.dc);
    sel_pen(f.dc, &s, COL_RED, 1);
    CHECK(U(SetWorldTransform)(f.dc, &(XFORM){ -1, 0, 0, 1, 40, 0 }));   /* x' = 40 - x */
    U(MoveToEx)(f.dc, 10, 12, NULL); U(LineTo)(f.dc, 20, 12);          /* device (30,12) -> (20,12), end excluded */
    for (x = 21; x <= 30; x++) EXPECT_EQ(fx_px(&f, x, 12), PX_RED);
    EXPECT_EQ(fx_px(&f, 20, 12), PX_WHITE);
    EXPECT_EQ(fx_px(&f, 31, 12), PX_WHITE);
    sel_done(f.dc, &s);
    fx_close(&f);
}

/* ---- direct syscalls ---------------------------------------------------------------------------------------- */
XT(direct_gettransform_world_matches_api) {
    XFORM a, b = xf_make(5, 5, 5, 5, 5, 5);
    NEED_XF();
    REQUIRE_NT(NtGdiGetTransform);
    MEMDC(dc);
    ADV(dc);
    CHECK(U(SetWorldTransform)(dc, &(XFORM){ 2, 0, 0, 0.5f, 7, -3 }));
    CHECK(U(GetWorldTransform)(dc, &a));
    CHECK(UN(NtGdiGetTransform)(dc, 0x203, &b));   /* the exact call GetWorldTransform makes */
    CHECK_XF(b, a);
    MEMDC_FREE(dc);
}

XT(direct_gettransform_other_selectors) {
    XFORM x;
    static const DWORD sel[] = { 0x202, 0x204, 0x302, 0x402, 0x401, 0x203 };
    size_t i;
    NEED_XF(); REQUIRE_NT(NtGdiGetTransform);
    MEMDC(dc);
    for (i = 0; i < sizeof sel / sizeof sel[0]; i++) {
        BOOL r;
        x = xf_make(7, 7, 7, 7, 7, 7);
        r = UN(NtGdiGetTransform)(dc, sel[i], &x);
        t_note("NtGdiGetTransform(0x%lx) on a default memory DC = %d {%g %g %g %g %g %g}", (unsigned long)sel[i], (int)r,
               x.eM11, x.eM12, x.eM21, x.eM22, x.eDx, x.eDy);
    }
    /* identity page mapping: world->device (0x203/0x204 combos) is the identity for a fresh DC */
    x = xf_make(7, 7, 7, 7, 7, 7);
    if (UN(NtGdiGetTransform)(dc, 0x203, &x) && x.eM11 != 7) CHECK_XF(x, XID);
    MEMDC_FREE(dc);
}

XT(direct_gettransform_invalid_hdc_leaves_output) {
    XFORM x = xf_make(7, 7, 7, 7, 7, 7);
    NEED_XF(); REQUIRE_NT(NtGdiGetTransform);
    UN(NtGdiGetTransform)(NULL, 0x203, &x);
    CHECK_XF(x, xf_make(7, 7, 7, 7, 7, 7));        /* the kernel transform query failed, NtGdiGetTransform copies nothing out */
    UN(NtGdiGetTransform)((HDC)(ULONG_PTR)0x01010fff, 0x203, &x);
    CHECK_XF(x, xf_make(7, 7, 7, 7, 7, 7));
}

XT(direct_modifyworldtransform_roundtrip) {
    XFORM x = xf_make(2, 0, 0, 3, 4, 5), got = xf_make(0, 0, 0, 0, 0, 0);
    BOOL r;
    NEED_XF();
    REQUIRE_NT(NtGdiModifyWorldTransform); REQUIRE_NT(NtGdiGetTransform);
    MEMDC(dc);
    /* The kernel entry has no graphics-mode test of its own (the gm==ADVANCED gate lives in gdi32full). */
    r = UN(NtGdiModifyWorldTransform)(dc, &x, 4);
    t_note("direct NtGdiModifyWorldTransform(MWT_SET) in GM_COMPATIBLE = %d", (int)r);
    if (r) {
        CHECK(UN(NtGdiGetTransform)(dc, 0x203, &got));
        CHECK_XF(got, x);
        CHECK(UN(NtGdiModifyWorldTransform)(dc, NULL, 1));
        CHECK(UN(NtGdiGetTransform)(dc, 0x203, &got));
        CHECK_XF(got, XID);
    }
    MEMDC_FREE(dc);
}

XT(direct_modifyworldtransform_modes) {
    XFORM x = xf_make(1, 0, 0, 1, 3, 3);
    static const DWORD bad[] = { 0, 5, 9, 0xffffffff };
    size_t i;
    NEED_XF(); REQUIRE_NT(NtGdiModifyWorldTransform);
    MEMDC(dc);
    ADV(dc);
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) CHECKF(!UN(NtGdiModifyWorldTransform)(dc, &x, bad[i]), "mode %lx", (unsigned long)bad[i]);
    CHECK(UN(NtGdiModifyWorldTransform)(dc, &x, 2));
    CHECK(UN(NtGdiModifyWorldTransform)(dc, &x, 3));
    CHECK(UN(NtGdiModifyWorldTransform)(dc, &x, 4));
    CHECK(!UN(NtGdiModifyWorldTransform)(dc, NULL, 4));
    CHECK(UN(NtGdiModifyWorldTransform)(dc, NULL, 1));
    CHECK(!UN(NtGdiModifyWorldTransform)(NULL, &x, 4));
    CHECK(!UN(NtGdiModifyWorldTransform)((HDC)U(GetStockObject)(BLACK_PEN), &x, 4));   /* wrong object type */
    MEMDC_FREE(dc);
}

XT(direct_transformpoints_modes) {
    POINT in[3] = { {3, 4}, {0, 0}, {10, -10} }, out[3], back[3];
    NEED_XF(); REQUIRE_NT(NtGdiTransformPoints); REQUIRE(U(LPtoDP));
    MEMDC(dc);
    ADV(dc);
    CHECK(U(SetWorldTransform)(dc, &(XFORM){ 2, 0, 0, 3, 10, 20 }));
    memset(out, 0x55, sizeof out);
    CHECK(UN(NtGdiTransformPoints)(dc, in, out, 3, 1));        /* iMode 1 = LP -> DP (what gdi32full!LPtoDP passes) */
    EXPECT_EQ(out[0].x, 16); EXPECT_EQ(out[0].y, 32);
    EXPECT_EQ(out[1].x, 10); EXPECT_EQ(out[1].y, 20);
    EXPECT_EQ(out[2].x, 30); EXPECT_EQ(out[2].y, -10);
    EXPECT_EQ(in[0].x, 3);                                       /* separate output leaves the input alone */
    memset(back, 0x55, sizeof back);
    CHECK(UN(NtGdiTransformPoints)(dc, out, back, 3, 0));      /* iMode 0 = DP -> LP (DPToLPInternal passes 0) */
    CHECK(!memcmp(back, in, sizeof in));
    MEMDC_FREE(dc);
}

XT(direct_transformpoints_zero_count) {
    POINT in = { 3, 4 }, out = { 0x5a, 0x5a };
    NEED_XF(); REQUIRE_NT(NtGdiTransformPoints);
    MEMDC(dc);
    ADV(dc);
    CHECK(UN(NtGdiTransformPoints)(dc, &in, &out, 0, 1));      /* (int)c < 1 returns 1 immediately */
    CHECK(UN(NtGdiTransformPoints)(dc, &in, &out, -5, 1));
    EXPECT_EQ(out.x, 0x5a);
    CHECK(UN(NtGdiTransformPoints)(NULL, &in, &out, 0, 1));
    MEMDC_FREE(dc);
}

XT(direct_transformpoints_invalid_hdc) {
    POINT in = { 3, 4 }, out = { 0x5a, 0x5a };
    NEED_XF(); REQUIRE_NT(NtGdiTransformPoints);
    CHECK(!UN(NtGdiTransformPoints)(NULL, &in, &out, 1, 1));
    CHECK(!UN(NtGdiTransformPoints)((HDC)(ULONG_PTR)0x01010fff, &in, &out, 1, 1));
    CHECK(!UN(NtGdiTransformPoints)((HDC)U(GetStockObject)(BLACK_BRUSH), &in, &out, 1, 1));
    EXPECT_EQ(out.x, 0x5a);
}

XT(direct_transformpoints_in_place_many) {
    POINT p[64], ref[64];
    int i;
    NEED_XF(); REQUIRE_NT(NtGdiTransformPoints); REQUIRE(U(SetMapMode));
    MEMDC(dc);
    U(SetMapMode)(dc, MM_ANISOTROPIC);
    U(SetWindowExtEx)(dc, 10, 10, NULL);
    U(SetViewportExtEx)(dc, 30, -20, NULL);
    U(SetViewportOrgEx)(dc, 5, 6, NULL);
    for (i = 0; i < 64; i++) { p[i].x = i * 10; p[i].y = 640 - i * 5; ref[i] = p[i]; }
    CHECK(UN(NtGdiTransformPoints)(dc, p, p, 64, 1));          /* in == out is supported (single probe, memcpy in/out) */
    {
        int bad = 0;
        for (i = 0; i < 64; i++) if (p[i].x != ref[i].x * 3 + 5 || p[i].y != -(ref[i].y * 2) + 6) bad++;
        CHECKF(bad == 0, "%d of 64 points transformed wrongly (first: (%ld,%ld) from (%ld,%ld))", bad, p[0].x, p[0].y, ref[0].x, ref[0].y);
    }
    MEMDC_FREE(dc);
}
