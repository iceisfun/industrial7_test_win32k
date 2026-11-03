/* gap6_umpd: the user-mode-printer-driver DDI callback syscalls (NtGdiEng.., NtGdi..OBJ_.., NtGdiHT_..).
 *
 * These syscalls exist so that a user-mode printer driver (UMPD) can call back into GDI while the
 * engine is dispatching a driver DDI call on that thread. Every NtGdiEng.. and NtGdi..OBJ_.. body in
 * win32kfull.sys begins with UMPDOBJ::GetThreadCurrentObj(W32GetThreadWin32Thread(...)): the
 * UMPDOBJ exists only on a thread that is inside such a driver call. An ordinary test thread has
 * none, so each of these returns the function's own "no UMPD" constant before looking at any
 * argument, which makes calling them with all-zero arguments side-effect free. The constant is
 * quoted per row (comment = the analysis branch). The win32k.sys thunk in front of each one
 * (a __guard_dispatch_icall through a feature slot, 0xc000001c STATUS_ILLEGAL_FUNCTION when the
 * slot is empty) is populated on a desktop SKU, so the body is reached.
 */
#include "gap6.h"

struct umpd_row { const char *name; long num; unsigned long exp; char mode; };

#define UMPD_LIST(X) \
X(BRUSH_DeleteRbrush, NtGdiBRUSHOBJ_DeleteRbrush, 4485, 0x0, 'd', "returns 0 when GetThreadCurrentObj == NULL") \
X(BRUSH_hGetColorTransform, NtGdiBRUSHOBJ_hGetColorTransform, 4486, 0x0, 'q', "returns 0 when there is no UMPD object") \
X(BRUSH_pvAllocRbrush, NtGdiBRUSHOBJ_pvAllocRbrush, 4487, 0x0, 'q', "returns NULL when there is no UMPD object") \
X(BRUSH_pvGetRbrush, NtGdiBRUSHOBJ_pvGetRbrush, 4488, 0x0, 'q', "returns NULL when there is no UMPD object") \
X(BRUSH_ulGetBrushColor, NtGdiBRUSHOBJ_ulGetBrushColor, 4489, 0x0, 'd', "returns 0 when there is no UMPD object") \
X(CLIP_bEnum, NtGdiCLIPOBJ_bEnum, 4491, 0x0, 'd', "returns 0 on the this == NULL branch (it is -1 only after a UMPD object exists)") \
X(CLIP_cEnumStart, NtGdiCLIPOBJ_cEnumStart, 4492, 0xFFFFFFFF, 'd', "returns 0xffffffff when GetThreadCurrentObj == NULL") \
X(CLIP_ppoGetPath, NtGdiCLIPOBJ_ppoGetPath, 4493, 0x0, 'q', "returns NULL on the this == NULL branch") \
X(FONT_cGetAllGlyphHandles, NtGdiFONTOBJ_cGetAllGlyphHandles, 4760, 0x0, 'd', "early return 0 when GetThreadCurrentObj == NULL") \
X(FONT_cGetGlyphs, NtGdiFONTOBJ_cGetGlyphs, 4761, 0x0, 'd', "final return 0 when there is no UMPD object") \
X(FONT_pfdg, NtGdiFONTOBJ_pfdg, 4763, 0x0, 'q', "returns NULL") \
X(FONT_pifi, NtGdiFONTOBJ_pifi, 4764, 0x0, 'q', "_Dst = NULL on the this == NULL branch") \
X(FONT_pQueryGlyphAttrs, NtGdiFONTOBJ_pQueryGlyphAttrs, 4762, 0x0, 'q', "final return NULL when there is no UMPD object") \
X(FONT_pvTrueTypeFontFile, NtGdiFONTOBJ_pvTrueTypeFontFile, 4765, 0x0, 'q', "returns NULL on the this == NULL branch") \
X(FONT_pxoGetXform, NtGdiFONTOBJ_pxoGetXform, 4766, 0x0, 'q', "returns NULL on the this == NULL branch") \
X(FONT_vGetInfo, NtGdiFONTOBJ_vGetInfo, 4767, 0xC000000D, 'd', "returns STATUS_INVALID_PARAMETER when there is no UMPD object") \
X(PATH_bEnum, NtGdiPATHOBJ_bEnum, 4844, 0x0, 'd', "returns 0 on the this == NULL branch") \
X(PATH_bEnumClipLines, NtGdiPATHOBJ_bEnumClipLines, 4845, 0x0, 'd', "final return 0 when there is no UMPD object") \
X(PATH_vEnumStart, NtGdiPATHOBJ_vEnumStart, 4846, 0xC000000D, 'd', "returns STATUS_INVALID_PARAMETER when there is no UMPD object") \
X(PATH_vEnumStartClipLines, NtGdiPATHOBJ_vEnumStartClipLines, 4847, 0xC000000D, 'd', "returns STATUS_INVALID_PARAMETER when there is no UMPD object") \
X(PATH_vGetBounds, NtGdiPATHOBJ_vGetBounds, 4848, 0xC000000D, 'd', "returns STATUS_INVALID_PARAMETER when there is no UMPD object") \
X(STR_bEnum, NtGdiSTROBJ_bEnum, 4861, 0xFFFFFFFF, 'n', "NtGdiSTROBJ_bEnumInternal returns -1 without a UMPD object; the wrapper has no documented result so the leftover rax is only noted") \
X(STR_bEnumPositionsOnly, NtGdiSTROBJ_bEnumPositionsOnly, 4862, 0xFFFFFFFF, 'n', "same as bEnum: bEnumInternal(..., 1) returns -1 without a UMPD object") \
X(STR_bGetAdvanceWidths, NtGdiSTROBJ_bGetAdvanceWidths, 4863, 0x0, 'd', "final return 0 when there is no UMPD object") \
X(STR_dwGetCodePage, NtGdiSTROBJ_dwGetCodePage, 4864, 0x0, 'd', "returns 0 when there is no UMPD object") \
X(STR_vEnumStart, NtGdiSTROBJ_vEnumStart, 4865, 0xC000000D, 'd', "returns STATUS_INVALID_PARAMETER when there is no UMPD object") \
X(XFORM_bApplyXform, NtGdiXFORMOBJ_bApplyXform, 4904, 0x0, 'd', "early return 0 when GetThreadCurrentObj == NULL") \
X(XFORM_iGetXform, NtGdiXFORMOBJ_iGetXform, 4905, 0xFFFFFFFF, 'd', "returns -1 when there is no UMPD object") \
X(XLATE_cGetPalette, NtGdiXLATEOBJ_cGetPalette, 4906, 0x0, 'd', "early return 0 when GetThreadCurrentObj == NULL") \
X(XLATE_hGetColorTransform, NtGdiXLATEOBJ_hGetColorTransform, 4907, 0x0, 'q', "returns 0 when there is no UMPD object") \
X(XLATE_iXlate, NtGdiXLATEOBJ_iXlate, 4908, 0xFFFFFFFF, 'd', "returns 0xffffffff when there is no UMPD object") \
X(EngAlphaBlend, NtGdiEngAlphaBlend, 4725, 0x0, 'd', "early return 0 when GetThreadCurrentObj == NULL") \
X(EngBitBlt, NtGdiEngBitBlt, 4727, 0x0, 'q', "early return NULL when GetThreadCurrentObj == NULL") \
X(EngCheckAbort, NtGdiEngCheckAbort, 4728, 0x1, 'd', "returns 1 (not aborted) when there is no UMPD object") \
X(EngComputeGlyphSet, NtGdiEngComputeGlyphSet, 4729, 0x0, 'q', "returns 0 when there is no UMPD object") \
X(EngCopyBits, NtGdiEngCopyBits, 4730, 0x0, 'd', "early return 0 when GetThreadCurrentObj == NULL") \
X(EngCreateClip, NtGdiEngCreateClip, 4732, 0x0, 'q', "returns NULL on the this == NULL branch") \
X(EngDeleteClip, NtGdiEngDeleteClip, 4736, 0xC000000D, 'd', "returns STATUS_INVALID_PARAMETER when there is no UMPD object") \
X(EngDeletePath, NtGdiEngDeletePath, 4738, 0xC000000D, 'd', "returns STATUS_INVALID_PARAMETER when there is no UMPD object") \
X(EngEraseSurface, NtGdiEngEraseSurface, 4740, 0x0, 'd', "returns 0 on the no-UMPD branch") \
X(EngFillPath, NtGdiEngFillPath, 4741, 0x0, 'd', "returns 0 on the this == NULL branch") \
X(EngGradientFill, NtGdiEngGradientFill, 4742, 0x0, 'd', "early return 0 when GetThreadCurrentObj == NULL") \
X(EngLineTo, NtGdiEngLineTo, 4743, 0x0, 'd', "returns 0 on the this == NULL branch") \
X(EngLockSurface, NtGdiEngLockSurface, 4744, 0x0, 'q', "returns NULL on the no-UMPD branch") \
X(EngPaint, NtGdiEngPaint, 4746, 0x0, 'd', "returns 0 on the this == NULL branch") \
X(EngPlgBlt, NtGdiEngPlgBlt, 4747, 0x0, 'd', "early return 0 when GetThreadCurrentObj == NULL") \
X(EngStretchBlt, NtGdiEngStretchBlt, 4748, 0x0, 'd', "early return 0 when GetThreadCurrentObj == NULL") \
X(EngStretchBltROP, NtGdiEngStretchBltROP, 4749, 0x0, 'q', "early return 0 when GetThreadCurrentObj == NULL") \
X(EngStrokeAndFillPath, NtGdiEngStrokeAndFillPath, 4750, 0x0, 'd', "returns 0 on the this == NULL branch") \
X(EngStrokePath, NtGdiEngStrokePath, 4751, 0x0, 'd', "returns 0 on the this == NULL branch") \
X(EngTextOut, NtGdiEngTextOut, 4752, 0x0, 'd', "returns NULL (low dword used as the BOOL) on the this == NULL branch") \
X(EngTransparentBlt, NtGdiEngTransparentBlt, 4753, 0x0, 'd', "early return 0 when GetThreadCurrentObj == NULL") \
X(EngUnlockSurface, NtGdiEngUnlockSurface, 4754, 0xC000000D, 'd', "returns STATUS_INVALID_PARAMETER when there is no UMPD object") \

static void umpd_run(const struct umpd_row *r, const char *why) {
    g6_fn14 f = g6_stub(r->name);
    REQUIRE(f);
    g6_check_stub_number(r->name, r->num);
    for (int pass = 0; pass < 2; pass++) {       /* the no-UMPD path leaves no state: stable on repeat */
        ULONG_PTR v = g6_call0(f);
        if (r->mode == 'n') {
            t_note("%s() with no UMPD object returned 0x%llx (%s)", r->name, (unsigned long long)v, why);
        } else if (r->mode == 'd') {
            t_check((unsigned long)v == r->exp, __FILE__, __LINE__, r->name, "pass %d: (%s) got 0x%lx want 0x%lx",
                    pass, why, (unsigned long)v, r->exp);
        } else {
            t_check((unsigned long long)v == (unsigned long long)r->exp, __FILE__, __LINE__, r->name,
                    "pass %d: (%s) got 0x%llx want 0x%lx", pass, why, (unsigned long long)v, r->exp);
        }
    }
}

#define X(tag, sym, num, exp, mode, why) \
    TEST(gap6_umpd_##tag) { static const struct umpd_row r = { #sym, num, exp, mode }; umpd_run(&r, why); }
UMPD_LIST(X)
#undef X

/* The same table on a second, freshly created thread: the UMPDOBJ lookup is per thread, so a
 * new thread must see exactly the same "no UMPD" answers. */
static const struct umpd_row umpd_table[] = {
#define X(tag, sym, num, exp, mode, why) { #sym, num, exp, mode },
    UMPD_LIST(X)
#undef X
};
static DWORD WINAPI umpd_thread(LPVOID arg) {
    int bad = 0;
    for (size_t i = 0; i < sizeof umpd_table / sizeof umpd_table[0]; i++) {
        const struct umpd_row *r = &umpd_table[i];
        g6_fn14 f = g6_stub(r->name);
        if (!f || r->mode == 'n') continue;
        ULONG_PTR v = g6_call0(f);
        unsigned long long got = r->mode == 'd' ? (unsigned long)v : (unsigned long long)v;
        if (got != r->exp) { bad++; printf("  umpd second thread: %s got 0x%llx want 0x%lx\n", r->name, got, r->exp); }
    }
    return bad;
}
TEST(gap6_umpd_second_thread_all_rows) {
    REQUIRE(g6_stub("NtGdiEngBitBlt"));
    HANDLE h = CreateThread(NULL, 0, umpd_thread, NULL, 0, NULL);
    REQUIRE(h);
    DWORD w = WaitForSingleObject(h, 10000), code = 99;
    CHECK(w == WAIT_OBJECT_0);
    if (w == WAIT_OBJECT_0) { GetExitCodeThread(h, &code); CHECK(code == 0); }
    CloseHandle(h);
}
TEST(gap6_umpd_table_size) {
    CHECK(sizeof umpd_table / sizeof umpd_table[0] == 53);
}

/* ---- DDI callbacks that do not consult the UMPD object ------------------------------------- */

/* NtGdiEngAssociateSurface(HSURF, HDEV, flags): the handle lookup fails for
 * a NULL handle and the function falls out returning 0. */
TEST(gap6_eng_associate_surface_null) {
    g6_fn14 f = g6_stub("NtGdiEngAssociateSurface"); REQUIRE(f);
    CHECK((unsigned long)g6_call0(f) == 0);
    CHECK((unsigned long)g6_call(f, 0, 0, 0x1, 0) == 0);

}
/* NtGdiEngDeleteSurface(HSURF): the handle lookup of NULL fails, so returns 0. */
TEST(gap6_eng_delete_surface_null) {
    g6_fn14 f = g6_stub("NtGdiEngDeleteSurface"); REQUIRE(f);
    CHECK((unsigned long)g6_call0(f) == 0);
}
/* NtGdiEngMarkBandingSurface(HSURF): the surface is not found, result 0. */
TEST(gap6_eng_mark_banding_surface_null) {
    g6_fn14 f = g6_stub("NtGdiEngMarkBandingSurface"); REQUIRE(f);
    CHECK((unsigned long)g6_call0(f) == 0);
}
/* NtGdiEngCreateDeviceBitmap(DHSURF, SIZEL, ULONG iFormat): format 0 makes (iFormat - 1) > 7, so
 * the function returns 0 whether or not ValidUmpdSizl accepts the size. */
TEST(gap6_eng_create_device_bitmap_bad_format) {
    g6_fn14 f = g6_stub("NtGdiEngCreateDeviceBitmap"); REQUIRE(f);
    CHECK(g6_call0(f) == 0);
    CHECK(g6_call(f, 0, 0x0010000100ULL, 0, 0) == 0);
    CHECK(g6_call(f, 0, 0x0010000100ULL, 9, 0) == 0);      /* 9 - 1 = 8, still > 7 */
}
/* NtGdiEngCreateDeviceSurface(DHSURF, SIZEL, ULONG iFormat): same iFormat window (1..8). */
TEST(gap6_eng_create_device_surface_bad_format) {
    g6_fn14 f = g6_stub("NtGdiEngCreateDeviceSurface"); REQUIRE(f);
    CHECK(g6_call0(f) == 0);
    CHECK(g6_call(f, 0, 0x0010000100ULL, 0, 0) == 0);
    CHECK(g6_call(f, 0, 0x0010000100ULL, 9, 0) == 0);
}
/* NtGdiEngCreatePalette(mode, count, colors, ...): count above 0x10000 returns 0 before the
 * colors pointer is touched. */
TEST(gap6_eng_create_palette_count_too_large) {
    g6_fn14 f = g6_stub("NtGdiEngCreatePalette"); REQUIRE(f);
    CHECK(g6_call(f, 1, 0x10001, 0, 0) == 0);
    CHECK(g6_call(f, 4, 0xFFFFFFFF, 0, 0) == 0);
}

/* ---- NtGdiHT_* halftone palette helpers ------------------------------------------------------ */

/* NtGdiHT_Get8BPPFormatPalette(void *pOut, USHORT r, USHORT g, USHORT b): a NULL output buffer
 * returns 0xd8 (216, the 6x6x6 cube size) without allocating anything. */
TEST(gap6_ht_get8bpp_format_palette_null_buffer) {
    g6_fn14 f = g6_stub("NtGdiHT_Get8BPPFormatPalette"); REQUIRE(f);
    CHECK((unsigned long)g6_call0(f) == 0xD8);
    CHECK((unsigned long)g6_call(f, 0, 1, 2, 3) == 0xD8);
}
/* NtGdiHT_Get8BPPMaskPalette(void *pal, BOOL use8bppMask, ...): with a NULL pal the call is the entry-count
 * query. Observed on Windows 10: the count follows the second argument only, 216 (the 6x6x6 cube) when it is
 * zero and 256 when it is non-zero, whatever the colour mask arguments are. */
TEST(gap6_ht_get8bpp_mask_palette_count_query) {
    g6_fn14 f = g6_stub("NtGdiHT_Get8BPPMaskPalette"); REQUIRE(f);
    EXPECT_EQ((unsigned long)g6_call0(f), 0xD8);
    EXPECT_EQ((unsigned long)g6_call(f, 0, 0, 1, 1), 0xD8);
    EXPECT_EQ((unsigned long)f(0, 1, 0, 0, 0,0,0,0,0,0,0,0,0,0), 0x100);
    EXPECT_EQ((unsigned long)f(0, 1, 1, 1, 0,0,0,0,0,0,0,0,0,0), 0x100);
    EXPECT_EQ((unsigned long)f(0, 1, 0xff, 0x80, 0,0,0,0,0,0,0,0,0,0), 0x100);
}
