/* r2_gdi_internal_eng_glue: the DC-taking internal syscalls with small, fully analysed bodies.
 *
 *  NtGdiSetTextJustification(HDC, int extra, int breakCount)  win32kfull 0x2bb2d0
 *  NtGdiSetFontXform(HDC, DWORD fltBitsX, DWORD fltBitsY)     win32kfull 0x1413d0
 *  NtGdiComputeXformCoefficients(HDC)                         win32kfull 0x146820
 *  NtGdiForceUFIMapping(HDC, UNIVERSAL_FONT_ID *)             win32kfull 0x144fd0
 *  NtGdiSetLinkedUFIs(HDC, UNIVERSAL_FONT_ID *, ULONG count)  win32kfull 0x27c950
 *  NtGdiSetMagicColors(HDC, ULONG color, ULONG index)         win32kbase
 */
#include "r2e.h"

#define DCA_BREAKEXTRA 0x11c
#define DCA_BREAKCOUNT 0x120
#define DCA_CHAREXTRA  0x118

static HDC mk_dc(void) { return U(CreateCompatibleDC)(NULL); }
/* A handle that was a valid DC and is not any more. */
static HDC stale_dc(void) {
    HDC d = mk_dc();
    if (d) U(DeleteDC)(d);
    return d;
}

/* ---- NtGdiSetTextJustification ------------------------------------------------------------ */

/* DCOBJ::DCOBJ fails for a handle that is not a live DC, the body returns 0 before it touches
 * the DC_ATTR. */
TEST(r2_gdi_internal_eng_glue_settextjust_null_dc) {
    r2e_fn f = r2e_stub("NtGdiSetTextJustification"); REQUIRE(f);
    r2e_check_number("NtGdiSetTextJustification", 4890);
    CHECK(R2E_LO(r2e_call(f, 0, 10, 2, 0, 0)) == 0);
    CHECK(R2E_LO(r2e_call(f, 0, 0, 0, 0, 0)) == 0);
}
TEST(r2_gdi_internal_eng_glue_settextjust_stale_dc) {
    r2e_fn f = r2e_stub("NtGdiSetTextJustification"); REQUIRE(f);
    HDC d = stale_dc(); REQUIRE(d);
    CHECK(R2E_LO(r2e_call(f, (ULONG_PTR)d, 10, 2, 0, 0)) == 0);
}
/* A bitmap handle is the wrong object type for DCOBJ's lock. */
TEST(r2_gdi_internal_eng_glue_settextjust_wrong_type) {
    r2e_fn f = r2e_stub("NtGdiSetTextJustification"); REQUIRE(f);
    HBITMAP bm = U(CreateBitmap)(2, 2, 1, 1, NULL); REQUIRE(bm);
    unsigned long gdi0 = r2e_gdi_count();
    CHECK(R2E_LO(r2e_call(f, (ULONG_PTR)bm, 10, 2, 0, 0)) == 0);
    CHECK(U(GetObjectType)(bm) == OBJ_BITMAP);
    CHECK(r2e_gdi_count() == gdi0);
    U(DeleteObject)(bm);
}
/* On a live DC the body stores both arguments into the DC_ATTR the process shares with the
 * kernel (*(DC+0x3d0)+0x11c / +0x120) and returns 1; no other DC_ATTR byte below 0x168 moves. */
TEST(r2_gdi_internal_eng_glue_settextjust_writes_dcattr) {
    r2e_fn f = r2e_stub("NtGdiSetTextJustification"); REQUIRE(f);
    HDC d = mk_dc(); REQUIRE(d);
    unsigned char *a = r2e_dcattr(d);
    static const struct { int extra, cnt; } v[] = { {17, 4}, {-9, 2}, {0, 0}, {0x7fffffff, 1}, {(-2147483647 - 1), -1} };
    unsigned char b0[R2E_DCA_SNAP], b1[R2E_DCA_SNAP];
    if (!a || !r2e_snap(d, b0)) { t_skip("DC_ATTR not decodable"); U(DeleteDC)(d); return; }
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) {
        CHECK(R2E_LO(r2e_call(f, (ULONG_PTR)d, (ULONG_PTR)(unsigned)v[i].extra, (ULONG_PTR)(unsigned)v[i].cnt, 0, 0)) == 1);
        unsigned long e = 0, c = 0;
        CHECK(k_try_read(a + DCA_BREAKEXTRA, &e, 4)); CHECK(k_try_read(a + DCA_BREAKCOUNT, &c, 4));
        CHECKF((int)e == v[i].extra, "break extra %d want %d", (int)e, v[i].extra);
        CHECKF((int)c == v[i].cnt, "break count %d want %d", (int)c, v[i].cnt);
    }
    CHECK(r2e_snap(d, b1));
    /* everything outside the two written dwords is what it was before the first call */
    memset(b0 + DCA_BREAKEXTRA, 0, 8); memset(b1 + DCA_BREAKEXTRA, 0, 8);
    CHECK(!memcmp(b0, b1, sizeof b0));
    U(DeleteDC)(d);
}
/* A second DC is unaffected by a justification set on the first (each DC has its own DC_ATTR). */
TEST(r2_gdi_internal_eng_glue_settextjust_per_dc) {
    r2e_fn f = r2e_stub("NtGdiSetTextJustification"); REQUIRE(f);
    HDC d1 = mk_dc(), d2 = mk_dc();
    if (!d1 || !d2) { t_skip("no DCs"); return; }
    unsigned char *a1 = r2e_dcattr(d1), *a2 = r2e_dcattr(d2);
    if (!a1 || !a2) { t_skip("DC_ATTR not decodable"); return; }
    CHECK(a1 != a2);
    CHECK(R2E_LO(r2e_call(f, (ULONG_PTR)d1, 33, 5, 0, 0)) == 1);
    unsigned long e2 = 99, c2 = 99;
    CHECK(k_try_read(a2 + DCA_BREAKEXTRA, &e2, 4)); CHECK(k_try_read(a2 + DCA_BREAKCOUNT, &c2, 4));
    CHECK(e2 == 0 && c2 == 0);
    U(DeleteDC)(d1); U(DeleteDC)(d2);
}
/* The call does not leak or create GDI objects. */
TEST(r2_gdi_internal_eng_glue_settextjust_no_object_growth) {
    r2e_fn f = r2e_stub("NtGdiSetTextJustification"); REQUIRE(f);
    HDC d = mk_dc(); REQUIRE(d);
    unsigned long gdi0 = r2e_gdi_count(), usr0 = r2e_user_count();
    for (int i = 0; i < 50; i++) r2e_call(f, (ULONG_PTR)d, i, i & 3, 0, 0);
    CHECK(r2e_gdi_count() == gdi0);
    CHECK(r2e_user_count() == usr0);
    U(DeleteDC)(d);
}

/* ---- NtGdiSetFontXform ---------------------------------------------------------------------- */

/* bConvertDwordToFloat always succeeds ((float)param), so the result is decided by the DC lock:
 * 0 for anything that is not a live DC, 1 (after storing the two floats into the DC) for a DC. */
TEST(r2_gdi_internal_eng_glue_setfontxform_null_dc) {
    r2e_fn f = r2e_stub("NtGdiSetFontXform"); REQUIRE(f);
    r2e_check_number("NtGdiSetFontXform", 4878);
    CHECK(R2E_LO(r2e_call(f, 0, 1, 1, 0, 0)) == 0);
    CHECK(R2E_LO(r2e_call(f, 0, 0, 0, 0, 0)) == 0);
}
TEST(r2_gdi_internal_eng_glue_setfontxform_stale_dc) {
    r2e_fn f = r2e_stub("NtGdiSetFontXform"); REQUIRE(f);
    HDC d = stale_dc(); REQUIRE(d);
    CHECK(R2E_LO(r2e_call(f, (ULONG_PTR)d, 1, 1, 0, 0)) == 0);
}
TEST(r2_gdi_internal_eng_glue_setfontxform_wrong_type) {
    r2e_fn f = r2e_stub("NtGdiSetFontXform"); REQUIRE(f);
    HPEN p = U(CreatePen)(PS_SOLID, 1, 0); REQUIRE(p);
    CHECK(R2E_LO(r2e_call(f, (ULONG_PTR)p, 1, 1, 0, 0)) == 0);
    CHECK(U(GetObjectType)(p) == OBJ_PEN);
    U(DeleteObject)(p);
}
TEST(r2_gdi_internal_eng_glue_setfontxform_live_dc) {
    r2e_fn f = r2e_stub("NtGdiSetFontXform"); REQUIRE(f);
    HDC d = mk_dc(); REQUIRE(d);
    unsigned char b0[R2E_DCA_SNAP], b1[R2E_DCA_SNAP];
    int have = r2e_snap(d, b0);
    static const unsigned long v[][2] = { {0, 0}, {1, 1}, {100, 200}, {0xFFFFFFFFul, 0x80000000ul} };
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++)
        CHECK(R2E_LO(r2e_call(f, (ULONG_PTR)d, v[i][0], v[i][1], 0, 0)) == 1);
    /* the floats go into the kernel DC (+0x1c4/+0x1c8), not into the shared DC_ATTR */
    if (have) { CHECK(r2e_snap(d, b1)); CHECK(!memcmp(b0, b1, sizeof b0)); }
    CHECK(U(GetObjectType)(d) == OBJ_MEMDC);
    unsigned long gdi0 = r2e_gdi_count();
    U(DeleteDC)(d);
    CHECK(r2e_gdi_count() == gdi0 - 1);
}

/* ---- NtGdiComputeXformCoefficients ---------------------------------------------------------- */

TEST(r2_gdi_internal_eng_glue_computexform_invalid_dc) {
    r2e_fn f = r2e_stub("NtGdiComputeXformCoefficients"); REQUIRE(f);
    r2e_check_number("NtGdiComputeXformCoefficients", 4232);
    CHECK(R2E_LO(r2e_call(f, 0, 0, 0, 0, 0)) == 0);        /* nothing is locked, so the result stays 0 */
    HDC d = stale_dc(); REQUIRE(d);
    CHECK(R2E_LO(r2e_call(f, (ULONG_PTR)d, 0, 0, 0, 0)) == 0);
}
TEST(r2_gdi_internal_eng_glue_computexform_wrong_type) {
    r2e_fn f = r2e_stub("NtGdiComputeXformCoefficients"); REQUIRE(f);
    HBRUSH b = U(CreateSolidBrush)(0); REQUIRE(b);
    CHECK(R2E_LO(r2e_call(f, (ULONG_PTR)b, 0, 0, 0, 0)) == 0);
    U(DeleteObject)(b);
}
/* The result is the single bit `flags & 1` of the DC's transform, so it is 0 or 1, and it is
 * stable for an unchanged DC. */
TEST(r2_gdi_internal_eng_glue_computexform_is_a_bit) {
    r2e_fn f = r2e_stub("NtGdiComputeXformCoefficients"); REQUIRE(f);
    HDC d = mk_dc(); REQUIRE(d);
    unsigned long a = R2E_LO(r2e_call(f, (ULONG_PTR)d, 0, 0, 0, 0));
    unsigned long b = R2E_LO(r2e_call(f, (ULONG_PTR)d, 0, 0, 0, 0));
    CHECK(a <= 1);
    CHECK(a == b);
    t_note("ComputeXformCoefficients(identity memory DC) = %lu", a);
    U(DeleteDC)(d);
}
/* A scaling world transform does not change the answer's range. */
TEST(r2_gdi_internal_eng_glue_computexform_after_world_transform) {
    r2e_fn f = r2e_stub("NtGdiComputeXformCoefficients"); REQUIRE(f);
    HDC d = mk_dc(); REQUIRE(d);
    U(SetGraphicsMode)(d, GM_ADVANCED);
    XFORM x = { 2.0f, 0.0f, 0.0f, 2.0f, 3.0f, 4.0f };
    if (!U(SetWorldTransform)(d, &x)) { t_skip("SetWorldTransform unavailable"); U(DeleteDC)(d); return; }
    CHECK(R2E_LO(r2e_call(f, (ULONG_PTR)d, 0, 0, 0, 0)) <= 1);
    U(DeleteDC)(d);
}

/* ---- NtGdiForceUFIMapping ------------------------------------------------------------------- */

/* The body is void; with a NULL UFI pointer it does nothing at all, and with a bad HDC the
 * lock fails so the UFI (read first, through a clamped probe) is never stored. */
TEST(r2_gdi_internal_eng_glue_forceufi_null_ufi_is_noop) {
    r2e_fn f = r2e_stub("NtGdiForceUFIMapping"); REQUIRE(f);
    r2e_check_number("NtGdiForceUFIMapping", 4770);
    HDC d = mk_dc(); REQUIRE(d);
    unsigned char b0[R2E_DCA_SNAP], b1[R2E_DCA_SNAP];
    int have = r2e_snap(d, b0);
    unsigned long gdi0 = r2e_gdi_count();
    r2e_call(f, (ULONG_PTR)d, 0, 0, 0, 0);
    r2e_call(f, 0, 0, 0, 0, 0);
    if (have) { CHECK(r2e_snap(d, b1)); CHECK(!memcmp(b0, b1, sizeof b0)); }
    CHECK(r2e_gdi_count() == gdi0);
    CHECK(U(GetObjectType)(d) == OBJ_MEMDC);
    U(DeleteDC)(d);
}
TEST(r2_gdi_internal_eng_glue_forceufi_invalid_dc_with_ufi) {
    r2e_fn f = r2e_stub("NtGdiForceUFIMapping"); REQUIRE(f);
    unsigned long long ufi[2] = { 0x1111, 0x2222 };    /* UNIVERSAL_FONT_ID is 8 bytes; the extra qword must stay untouched */
    unsigned long gdi0 = r2e_gdi_count();
    r2e_call(f, 0, (ULONG_PTR)ufi, 0, 0, 0);
    HDC s = stale_dc(); REQUIRE(s);
    gdi0 = r2e_gdi_count();
    r2e_call(f, (ULONG_PTR)s, (ULONG_PTR)ufi, 0, 0, 0);
    CHECK(ufi[0] == 0x1111 && ufi[1] == 0x2222);
    CHECK(r2e_gdi_count() == gdi0);
}
/* A live DC takes the (read-only) UFI: the caller's copy is never written. */
TEST(r2_gdi_internal_eng_glue_forceufi_live_dc_reads_only) {
    r2e_fn f = r2e_stub("NtGdiForceUFIMapping"); REQUIRE(f);
    HDC d = mk_dc(); REQUIRE(d);
    unsigned long long ufi = 0; /* the all-zero UFI selects no font; DeleteDC below drops the DC */
    unsigned char b0[R2E_DCA_SNAP], b1[R2E_DCA_SNAP];
    int have = r2e_snap(d, b0);
    r2e_call(f, (ULONG_PTR)d, (ULONG_PTR)&ufi, 0, 0, 0);
    CHECK(ufi == 0);
    if (have) { CHECK(r2e_snap(d, b1)); CHECK(!memcmp(b0, b1, sizeof b0)); }   /* +0xfc/+0x100 are kernel DC fields */
    unsigned long gdi0 = r2e_gdi_count();
    CHECK(U(DeleteDC)(d));
    CHECK(r2e_gdi_count() == gdi0 - 1);
}

/* ---- NtGdiSetLinkedUFIs --------------------------------------------------------------------- */

/* the result starts as 1 and only the successful lock + bSetLinkedUFIs can change it, so a handle
 * that locks no DC still gets 1 once the (count, pointer) pair passed the up-front checks. */
TEST(r2_gdi_internal_eng_glue_setlinkedufis_invalid_dc_empty_list) {
    r2e_fn f = r2e_stub("NtGdiSetLinkedUFIs"); REQUIRE(f);
    r2e_check_number("NtGdiSetLinkedUFIs", 4881);
    CHECK(R2E_LO(r2e_call(f, 0, 0, 0, 0, 0)) == 1);
    HDC s = stale_dc(); REQUIRE(s);
    CHECK(R2E_LO(r2e_call(f, (ULONG_PTR)s, 0, 0, 0, 0)) == 1);
}
/* A NULL list with a non-zero count is rejected first, whatever the HDC is. */
TEST(r2_gdi_internal_eng_glue_setlinkedufis_null_list_nonzero_count) {
    r2e_fn f = r2e_stub("NtGdiSetLinkedUFIs"); REQUIRE(f);
    HDC d = mk_dc(); REQUIRE(d);
    static const unsigned long counts[] = { 1, 2, 4, 5, 100, 0x4e2000, 0x4e2001, 0xFFFFFFFFul };
    for (unsigned i = 0; i < sizeof counts / sizeof counts[0]; i++) {
        CHECKF(R2E_LO(r2e_call(f, (ULONG_PTR)d, 0, counts[i], 0, 0)) == 0, "count %lu live dc", counts[i]);
        CHECKF(R2E_LO(r2e_call(f, 0, 0, counts[i], 0, 0)) == 0, "count %lu null dc", counts[i]);
    }
    U(DeleteDC)(d);
}
/* Above 0x4e2000 entries no pool allocation is attempted and the call fails before the list is read,
 * so a pointer to a single entry is enough. */
TEST(r2_gdi_internal_eng_glue_setlinkedufis_count_over_limit) {
    r2e_fn f = r2e_stub("NtGdiSetLinkedUFIs"); REQUIRE(f);
    static unsigned long long one[2];
    HDC d = mk_dc(); REQUIRE(d);
    static const unsigned long counts[] = { 0x4e2001, 0x4e2002, 0x1000000, 0x7FFFFFFF, 0x80000000ul, 0xFFFFFFFFul };
    for (unsigned i = 0; i < sizeof counts / sizeof counts[0]; i++) {
        CHECKF(R2E_LO(r2e_call(f, (ULONG_PTR)d, (ULONG_PTR)one, counts[i], 0, 0)) == 0, "count 0x%lx live dc", counts[i]);
        CHECKF(R2E_LO(r2e_call(f, 0, (ULONG_PTR)one, counts[i], 0, 0)) == 0, "count 0x%lx null dc", counts[i]);
    }
    U(DeleteDC)(d);
}
/* Counts below five use the on-stack list, five and above a pool copy; both copy the caller's list
 * with memcpy and, for a handle that locks no DC, return 1 and free the copy. */
TEST(r2_gdi_internal_eng_glue_setlinkedufis_copy_paths_invalid_dc) {
    r2e_fn f = r2e_stub("NtGdiSetLinkedUFIs"); REQUIRE(f);
    static unsigned long long list[256];
    for (unsigned i = 0; i < 256; i++) list[i] = 0x0101010101010101ull * (i & 0xff);
    static const unsigned long counts[] = { 1, 2, 3, 4, 5, 6, 16, 100, 256 };
    unsigned long gdi0 = r2e_gdi_count();
    for (unsigned i = 0; i < sizeof counts / sizeof counts[0]; i++) {
        CHECKF(R2E_LO(r2e_call(f, 0, (ULONG_PTR)list, counts[i], 0, 0)) == 1, "count %lu", counts[i]);
    }
    for (unsigned i = 0; i < 256; i++) CHECK(list[i] == 0x0101010101010101ull * (i & 0xff));   /* input only */
    CHECK(r2e_gdi_count() == gdi0);
}
/* Wrong-type handles lock no DC, they behave like a null handle. */
TEST(r2_gdi_internal_eng_glue_setlinkedufis_wrong_type_handle) {
    r2e_fn f = r2e_stub("NtGdiSetLinkedUFIs"); REQUIRE(f);
    HBITMAP bm = U(CreateBitmap)(2, 2, 1, 1, NULL); REQUIRE(bm);
    static unsigned long long list[4];
    CHECK(R2E_LO(r2e_call(f, (ULONG_PTR)bm, (ULONG_PTR)list, 2, 0, 0)) == 1);
    CHECK(R2E_LO(r2e_call(f, (ULONG_PTR)bm, 0, 1, 0, 0)) == 0);
    CHECK(U(GetObjectType)(bm) == OBJ_BITMAP);
    U(DeleteObject)(bm);
}

/* ---- NtGdiSetMagicColors -------------------------------------------------------------------- */

/* SetMagicColors: no DC, or an index that is not one of 8, 9, 0xf6, 0xf7 (`1 < index - 8 &&
 * 1 < index - 0xf6`), takes the exit with a zero result. Only those rejected cases are driven. */
TEST(r2_gdi_internal_eng_glue_setmagiccolors_invalid_dc) {
    r2e_fn f = r2e_stub("NtGdiSetMagicColors"); REQUIRE(f);
    r2e_check_number("NtGdiSetMagicColors", 4882);
    static const unsigned long idx[] = { 8, 9, 0xf6, 0xf7, 0, 1 };
    HDC s = stale_dc(); REQUIRE(s);
    for (unsigned i = 0; i < sizeof idx / sizeof idx[0]; i++) {
        CHECKF(R2E_LO(r2e_call(f, 0, 0, idx[i], 0, 0)) == 0, "null dc index %lu", idx[i]);
        CHECKF(R2E_LO(r2e_call(f, (ULONG_PTR)s, 0, idx[i], 0, 0)) == 0, "stale dc index %lu", idx[i]);
    }
}
TEST(r2_gdi_internal_eng_glue_setmagiccolors_bad_index_live_dc) {
    r2e_fn f = r2e_stub("NtGdiSetMagicColors"); REQUIRE(f);
    HDC d = mk_dc(); REQUIRE(d);
    static const unsigned long idx[] = { 0, 1, 7, 10, 11, 0xf4, 0xf5, 0xf8, 0xf9, 0x100, 0xFFFFFFFFul };
    unsigned char b0[R2E_DCA_SNAP], b1[R2E_DCA_SNAP];
    int have = r2e_snap(d, b0);
    for (unsigned i = 0; i < sizeof idx / sizeof idx[0]; i++)
        CHECKF(R2E_LO(r2e_call(f, (ULONG_PTR)d, 0x00112233, idx[i], 0, 0)) == 0, "index 0x%lx", idx[i]);
    if (have) { CHECK(r2e_snap(d, b1)); CHECK(!memcmp(b0, b1, sizeof b0)); }
    U(DeleteDC)(d);
}
