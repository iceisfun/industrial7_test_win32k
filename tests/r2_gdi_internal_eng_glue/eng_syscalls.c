/* r2_gdi_internal_eng_glue: surface/palette/UMPD-management syscalls with up-front rejection paths.
 *
 *  NtGdiEngCreateBitmap(SIZEL sizl, LONG lWidth, ULONG iFormat, ULONG fl, PVOID pvBits)  win32kfull 0x1520c0
 *  NtGdiEngDeletePalette(HPALETTE)                                                       win32kfull 0x12d2c0
 *  NtGdiUMPDEngFreeUserMem(PVOID *ppv)                                                   win32kfull 0x2b4460
 *  NtGdiSetPUMPDOBJ(HUMPD, BOOL bPush, HUMPD *phumpd, BOOL *pbWasPushed)                 win32kfull 0x71780
 */
#include "r2e.h"

#define SIZL(cx, cy) ((((ULONG_PTR)(unsigned)(cy)) << 32) | (ULONG_PTR)(unsigned)(cx))

/* ---- NtGdiEngCreateBitmap ------------------------------------------------------------------- */

/* ValidUmpdSizl(sizl, 1) wants cx > 0, cy > 0 and cx*cy < 2^32; cy*lWidth must fit 32 bits as
 * well. Everything else returns 0 before any allocation or probe. */
static const struct { int cx, cy; unsigned long width; const char *why; } cb_bad[] = {
    { 0, 0, 0, "cx = cy = 0" },
    { 0, 16, 0, "cx = 0" },
    { 16, 0, 0, "cy = 0" },
    { -1, 16, 0, "cx < 0" },
    { 16, -1, 0, "cy < 0" },
    { -16, -16, 0, "both negative" },
    { (-2147483647 - 1), 16, 0, "cx = INT_MIN" },
    { 16, (-2147483647 - 1), 0, "cy = INT_MIN" },
    { 0x10000, 0x10000, 0, "cx*cy == 2^32 is not < 2^32" },
    { 0x7fffffff, 0x7fffffff, 0, "product far above 2^32" },
    { 0x7fffffff, 3, 0, "product above 2^32" },
    { 0x100000, 0x1001, 0, "cx*cy = 2^32 + 2^20" },
    { 16, 16, 0x10000000, "valid size, cy*lWidth = 2^32 overflows the 32-bit byte count" },
    { 2, 0x7fffffff, 0x80000000ul, "valid size (2*0x7fffffff < 2^32), cy*lWidth > 0xffffffff" },
};
TEST(r2_gdi_internal_eng_glue_engcreatebitmap_bad_size_returns_null) {
    r2e_fn f = r2e_stub("NtGdiEngCreateBitmap"); REQUIRE(f);
    r2e_check_number("NtGdiEngCreateBitmap", 4731);
    unsigned long g0 = r2e_gdi_count();
    unsigned n = sizeof cb_bad / sizeof cb_bad[0];
    for (unsigned i = 0; i < n; i++) {
        ULONG_PTR v = f(SIZL(cb_bad[i].cx, cb_bad[i].cy), cb_bad[i].width, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        CHECKF(v == 0, "%s: got 0x%llx", cb_bad[i].why, (unsigned long long)v);
    }
    CHECK(r2e_gdi_count() == g0);
}
/* The SIZEL is read from the register, so the format and flag arguments do not rescue a bad size. */
TEST(r2_gdi_internal_eng_glue_engcreatebitmap_bad_size_any_format) {
    r2e_fn f = r2e_stub("NtGdiEngCreateBitmap"); REQUIRE(f);
    unsigned long g0 = r2e_gdi_count();
    for (unsigned fmt = 0; fmt <= 9; fmt++)
        for (unsigned fl = 0; fl <= 0x80; fl += 0x40)
            CHECK(f(SIZL(0, 8), 4, fmt, fl, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) == 0);
    CHECK(r2e_gdi_count() == g0);
}
/* The pvBits pointer is not looked at on a rejected size (MmSecureVirtualMemory is after the checks). */
TEST(r2_gdi_internal_eng_glue_engcreatebitmap_bad_size_ignores_bits_pointer) {
    r2e_fn f = r2e_stub("NtGdiEngCreateBitmap"); REQUIRE(f);
    unsigned char *page = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE); REQUIRE(page);
    memset(page, 0x3C, 4096);
    CHECK(f(SIZL(0, 0), 64, 1, 0, (ULONG_PTR)page, 0, 0, 0, 0, 0, 0, 0, 0, 0) == 0);
    CHECK(f(SIZL(0x10000, 0x10000), 64, 1, 0, (ULONG_PTR)page, 0, 0, 0, 0, 0, 0, 0, 0, 0) == 0);
    /* page protection and contents unchanged: nothing was secured/locked on this path */
    MEMORY_BASIC_INFORMATION mbi;
    CHECK(VirtualQuery(page, &mbi, sizeof mbi) == sizeof mbi && mbi.Protect == PAGE_READWRITE);
    for (int i = 0; i < 4096; i++) if (page[i] != 0x3C) { CHECK(0); break; }
    CHECK(VirtualFree(page, 0, MEM_RELEASE));
}
TEST(r2_gdi_internal_eng_glue_engcreatebitmap_bad_size_fresh_thread) {
    r2e_fn f = r2e_stub("NtGdiEngCreateBitmap"); REQUIRE(f);
    static r2e_fn g; g = f;
    DWORD WINAPI th(LPVOID a) { (void)a; return g(SIZL(-5, 5), 8, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0) == 0 ? 0 : 1; }
    HANDLE h = CreateThread(NULL, 0, th, NULL, 0, NULL); REQUIRE(h);
    DWORD w = WaitForSingleObject(h, 10000), code = 9;
    CHECK(w == WAIT_OBJECT_0);
    if (w == WAIT_OBJECT_0) { GetExitCodeThread(h, &code); CHECK(code == 0); }
    CloseHandle(h);
}

/* ---- NtGdiEngDeletePalette ------------------------------------------------------------------ */

/* EngDeletePaletteInternal(h, 0x8000): EPALOBJ's lock must find a palette. Null, stale, and
 * wrong-type handles find none, the result is dropped (the syscall is void) and nothing else moves. */
TEST(r2_gdi_internal_eng_glue_engdeletepalette_null_is_noop) {
    r2e_fn f = r2e_stub("NtGdiEngDeletePalette"); REQUIRE(f);
    r2e_check_number("NtGdiEngDeletePalette", 4737);
    unsigned long g0 = r2e_gdi_count();
    f(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    CHECK(r2e_gdi_count() == g0);
}
TEST(r2_gdi_internal_eng_glue_engdeletepalette_wrong_type_handles_survive) {
    r2e_fn f = r2e_stub("NtGdiEngDeletePalette"); REQUIRE(f);
    HDC d = U(CreateCompatibleDC)(NULL);
    HBITMAP bm = U(CreateBitmap)(4, 4, 1, 1, NULL);
    HPEN pen = U(CreatePen)(PS_SOLID, 1, 0);
    HBRUSH br = U(CreateSolidBrush)(0x112233);
    HRGN rg = U(CreateRectRgn)(0, 0, 4, 4);
    if (!d || !bm || !pen || !br || !rg) { t_skip("cannot create objects"); return; }
    unsigned long g0 = r2e_gdi_count();
    ULONG_PTR hs[] = { (ULONG_PTR)d, (ULONG_PTR)bm, (ULONG_PTR)pen, (ULONG_PTR)br, (ULONG_PTR)rg };
    for (unsigned i = 0; i < 5; i++) f(hs[i], 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    CHECK(U(GetObjectType)(d) == OBJ_MEMDC);
    CHECK(U(GetObjectType)(bm) == OBJ_BITMAP);
    CHECK(U(GetObjectType)(pen) == OBJ_PEN);
    CHECK(U(GetObjectType)(br) == OBJ_BRUSH);
    CHECK(U(GetObjectType)(rg) == OBJ_REGION);
    CHECK(r2e_gdi_count() == g0);
    U(DeleteObject)(rg); U(DeleteObject)(br); U(DeleteObject)(pen); U(DeleteObject)(bm); U(DeleteDC)(d);
}
TEST(r2_gdi_internal_eng_glue_engdeletepalette_stale_handle) {
    r2e_fn f = r2e_stub("NtGdiEngDeletePalette"); REQUIRE(f);
    LOGPALETTE lp; memset(&lp, 0, sizeof lp); lp.palVersion = 0x300; lp.palNumEntries = 1;
    HPALETTE p = U(CreatePalette)(&lp); REQUIRE(p);
    CHECK(U(DeleteObject)(p));
    unsigned long g0 = r2e_gdi_count();
    f((ULONG_PTR)p, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);       /* the handle is dead: a second delete finds nothing */
    CHECK(r2e_gdi_count() == g0);
    CHECK(U(GetObjectType)(p) == OBJ_PAL);   /* a freed cell keeps its upper word until it is reallocated: the type is still reported */
}
/* A palette from CreatePalette is an ordinary GDI object. Whether the engine-delete accepts it
 * depends on the "created by the engine" bit (palette flags 0x2000000) that EngDeletePaletteInternal
 * demands when called with 0x8000; the outcome is recorded and the handle is then released in
 * whichever way keeps the count balanced. */
TEST(r2_gdi_internal_eng_glue_engdeletepalette_user_palette_recorded) {
    r2e_fn f = r2e_stub("NtGdiEngDeletePalette"); REQUIRE(f);
    LOGPALETTE lp; memset(&lp, 0, sizeof lp); lp.palVersion = 0x300; lp.palNumEntries = 1;
    HPALETTE p = U(CreatePalette)(&lp); REQUIRE(p);
    unsigned long g0 = r2e_gdi_count();
    f((ULONG_PTR)p, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    DWORD t = U(GetObjectType)(p);
    t_note("EngDeletePalette(CreatePalette handle): GetObjectType afterwards = %lu, gdi count %lu -> %lu", (unsigned long)t, g0, r2e_gdi_count());
    CHECK(t == OBJ_PAL || t == 0);
    if (t == OBJ_PAL) { CHECK(U(DeleteObject)(p)); CHECK(r2e_gdi_count() == g0 - 1); }
}

/* ---- NtGdiUMPDEngFreeUserMem ----------------------------------------------------------------- */

/* Observed on Windows 10 22H2: a NULL argument frees nothing and answers 1. A non-NULL pointer (here to a slot holding NULL)
 * answers 0: the call refuses it rather than treating the NULL slot as "nothing to free". The memory it points to is not
 * modified either way. */
TEST(r2_gdi_internal_eng_glue_umpdfreeusermem_null_returns_one) {
    r2e_fn f = r2e_stub("NtGdiUMPDEngFreeUserMem"); REQUIRE(f);
    r2e_check_number("NtGdiUMPDEngFreeUserMem", 4898);
    CHECK(R2E_LO(f(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)) == 1);
}
TEST(r2_gdi_internal_eng_glue_umpdfreeusermem_null_slot_returns_zero) {
    r2e_fn f = r2e_stub("NtGdiUMPDEngFreeUserMem"); REQUIRE(f);
    ULONG_PTR slot = 0;
    CHECK(R2E_LO(f((ULONG_PTR)&slot, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)) == 0);
    CHECK(slot == 0);
    /* a slot inside a larger heap block, 8-aligned, holding NULL */
    ULONG_PTR *blk = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 64); REQUIRE(blk);
    CHECK(R2E_LO(f((ULONG_PTR)(blk + 3), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)) == 0);
    for (int i = 0; i < 8; i++) CHECK(blk[i] == 0);
    HeapFree(GetProcessHeap(), 0, blk);
}
TEST(r2_gdi_internal_eng_glue_umpdfreeusermem_repeatable) {
    r2e_fn f = r2e_stub("NtGdiUMPDEngFreeUserMem"); REQUIRE(f);
    ULONG_PTR slot = 0;
    for (int i = 0; i < 100; i++) CHECK(R2E_LO(f((ULONG_PTR)&slot, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)) == 0);
    CHECK(slot == 0);
}

/* ---- NtGdiSetPUMPDOBJ ------------------------------------------------------------------------- */

/* hUMPD == 0 fails (both the push and pop forms) before the HUMPD slot or the "was pushed" flag are
 * touched; the result is the low dword of a 64-bit 0. */
TEST(r2_gdi_internal_eng_glue_setpumpdobj_null_handle_push) {
    r2e_fn f = r2e_stub("NtGdiSetPUMPDOBJ"); REQUIRE(f);
    r2e_check_number("NtGdiSetPUMPDOBJ", 4884);
    ULONG_PTR slot = 0xA5A5A5A5A5A5A5A5ull; int was = 0x5A5A5A5A;
    CHECK(R2E_LO(f(0, 1, (ULONG_PTR)&slot, (ULONG_PTR)&was, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)) == 0);   /* first argument 0, second non-zero */
    CHECK(slot == 0xA5A5A5A5A5A5A5A5ull);
    CHECK(was == 0x5A5A5A5A);
}
TEST(r2_gdi_internal_eng_glue_setpumpdobj_null_handle_null_slot) {
    r2e_fn f = r2e_stub("NtGdiSetPUMPDOBJ"); REQUIRE(f);
    CHECK(R2E_LO(f(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)) == 0);   /* pop form with a NULL slot pointer */
    CHECK(R2E_LO(f(0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)) == 0);
}
/* A HDC (type 1), not a HUMPD (type 0x11): the handle lookup finds no object, `this` stays NULL, push fails. */
TEST(r2_gdi_internal_eng_glue_setpumpdobj_wrong_type_push) {
    r2e_fn f = r2e_stub("NtGdiSetPUMPDOBJ"); REQUIRE(f);
    HDC d = U(CreateCompatibleDC)(NULL); REQUIRE(d);
    HBITMAP bm = U(CreateBitmap)(2, 2, 1, 1, NULL); REQUIRE(bm);
    ULONG_PTR slot = 0xA5A5A5A5A5A5A5A5ull; int was = 0x5A5A5A5A;
    unsigned long g0 = r2e_gdi_count();
    CHECK(R2E_LO(f((ULONG_PTR)d, 1, (ULONG_PTR)&slot, (ULONG_PTR)&was, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)) == 0);
    CHECK(R2E_LO(f((ULONG_PTR)bm, 1, (ULONG_PTR)&slot, (ULONG_PTR)&was, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)) == 0);
    CHECK(slot == 0xA5A5A5A5A5A5A5A5ull);
    CHECK(was == 0x5A5A5A5A);
    CHECK(U(GetObjectType)(d) == OBJ_MEMDC);
    CHECK(U(GetObjectType)(bm) == OBJ_BITMAP);
    CHECK(r2e_gdi_count() == g0);
    U(DeleteObject)(bm); U(DeleteDC)(d);
}
/* Stale and never-allocated handle values. */
TEST(r2_gdi_internal_eng_glue_setpumpdobj_stale_and_garbage_handles) {
    r2e_fn f = r2e_stub("NtGdiSetPUMPDOBJ"); REQUIRE(f);
    HDC d = U(CreateCompatibleDC)(NULL); REQUIRE(d);
    U(DeleteDC)(d);
    ULONG_PTR slot = 1; int was = 7;
    ULONG_PTR hs[] = { (ULONG_PTR)d, 0x00110000ull, 0xFFFFull, 0x7FFFFFFFull };
    for (unsigned i = 0; i < 4; i++) {
        CHECKF(R2E_LO(f(hs[i], 1, (ULONG_PTR)&slot, (ULONG_PTR)&was, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)) == 0, "handle 0x%llx", (unsigned long long)hs[i]);
    }
    CHECK(slot == 1 && was == 7);
}
/* Pop form (bPush == 0) with an empty per-thread UMPD list: the lookup finds nothing, so the call
 * fails and *phumpd is not written (it is only compared when a UMPDOBJ exists). */
TEST(r2_gdi_internal_eng_glue_setpumpdobj_pop_without_push) {
    r2e_fn f = r2e_stub("NtGdiSetPUMPDOBJ"); REQUIRE(f);
    ULONG_PTR slot = 0x1122334455667788ull; int was = 0x77;
    CHECK(R2E_LO(f(0, 0, (ULONG_PTR)&slot, (ULONG_PTR)&was, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)) == 0);
    CHECK(slot == 0x1122334455667788ull);
    CHECK(was == 0x77);
}
TEST(r2_gdi_internal_eng_glue_setpumpdobj_no_object_growth) {
    r2e_fn f = r2e_stub("NtGdiSetPUMPDOBJ"); REQUIRE(f);
    ULONG_PTR slot = 0; int was = 0;
    unsigned long g0 = r2e_gdi_count(), u0 = r2e_user_count();
    for (int i = 0; i < 20; i++) {
        f(0, i & 1, (ULONG_PTR)&slot, (ULONG_PTR)&was, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        f(0xdead0000ull + i, 1, (ULONG_PTR)&slot, (ULONG_PTR)&was, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    }
    CHECK(r2e_gdi_count() == g0);
    CHECK(r2e_user_count() == u0);
    CHECK(slot == 0 && was == 0);
}
