/* r2_gdi_internal_eng_glue: the NtGdi..OBJ_ / NtGdiEng.. user-mode-printer-driver (UMPD) callbacks.
 *
 * Each body in win32kfull.sys starts with UMPDOBJ::GetThreadCurrentObj(W32GetThreadWin32Thread(..)).
 * That object only exists on a thread that is inside a UMPD driver call, which an ordinary test
 * thread never is, so every entry returns its own "no UMPD" constant before it looks at any
 * argument. The tests here drive that guard with arguments that WOULD be dereferenced or
 * released if the guard did not hold (pointers into canary buffers, live HDC/HBITMAP/HPEN/HBRUSH
 * handles) and then prove that nothing changed: same constant back, canaries intact, handles still
 * valid, GDI object count unchanged. The constants are the ones quoted per row (analysis branch).
 */
#include "r2e.h"

struct row { const char *name; long num; unsigned long exp; char mode; };

#define UMPD_LIST(X) \
X(BRUSH_DeleteRbrush, NtGdiBRUSHOBJ_DeleteRbrush, 4485, 0x0, 'd', "result is 0 when GetThreadCurrentObj == NULL") \
X(BRUSH_hGetColorTransform, NtGdiBRUSHOBJ_hGetColorTransform, 4486, 0x0, 'q', "returns 0 when there is no UMPD object (result is 0 fallthrough)") \
X(BRUSH_pvAllocRbrush, NtGdiBRUSHOBJ_pvAllocRbrush, 4487, 0x0, 'q', "result is NULL when there is no UMPD object") \
X(BRUSH_pvGetRbrush, NtGdiBRUSHOBJ_pvGetRbrush, 4488, 0x0, 'q', "result is NULL when there is no UMPD object") \
X(BRUSH_ulGetBrushColor, NtGdiBRUSHOBJ_ulGetBrushColor, 4489, 0x0, 'd', "result is 0 when there is no UMPD object") \
X(CLIP_bEnum, NtGdiCLIPOBJ_bEnum, 4491, 0x0, 'd', "result is 0 on the this == NULL branch (it is -1 only after a UMPD object exists)") \
X(CLIP_cEnumStart, NtGdiCLIPOBJ_cEnumStart, 4492, 0xFFFFFFFF, 'd', "result is 0xffffffff is the fallthrough when GetThreadCurrentObj == NULL") \
X(CLIP_ppoGetPath, NtGdiCLIPOBJ_ppoGetPath, 4493, 0x0, 'q', "result is NULL on the this == NULL branch") \
X(FONT_cGetAllGlyphHandles, NtGdiFONTOBJ_cGetAllGlyphHandles, 4760, 0x0, 'd', "early return 0 when GetThreadCurrentObj == NULL") \
X(FONT_cGetGlyphs, NtGdiFONTOBJ_cGetGlyphs, 4761, 0x0, 'd', "final return 0 when there is no UMPD object") \
X(FONT_pfdg, NtGdiFONTOBJ_pfdg, 4763, 0x0, 'q', "result is NULL initial value is returned") \
X(FONT_pifi, NtGdiFONTOBJ_pifi, 4764, 0x0, 'q', "_Dst = NULL on the this == NULL branch") \
X(FONT_pQueryGlyphAttrs, NtGdiFONTOBJ_pQueryGlyphAttrs, 4762, 0x0, 'q', "final return NULL when there is no UMPD object") \
X(FONT_pvTrueTypeFontFile, NtGdiFONTOBJ_pvTrueTypeFontFile, 4765, 0x0, 'q', "result is NULL on the this == NULL branch") \
X(FONT_pxoGetXform, NtGdiFONTOBJ_pxoGetXform, 4766, 0x0, 'q', "result is NULL on the this == NULL branch") \
X(FONT_vGetInfo, NtGdiFONTOBJ_vGetInfo, 4767, 0xC000000D, 'd', "result is STATUS_INVALID_PARAMETER when there is no UMPD object") \
X(PATH_bEnum, NtGdiPATHOBJ_bEnum, 4844, 0x0, 'd', "result is 0 on the this == NULL branch") \
X(PATH_bEnumClipLines, NtGdiPATHOBJ_bEnumClipLines, 4845, 0x0, 'd', "final return 0 when there is no UMPD object") \
X(PATH_vEnumStart, NtGdiPATHOBJ_vEnumStart, 4846, 0xC000000D, 'd', "result is STATUS_INVALID_PARAMETER when there is no UMPD object") \
X(PATH_vEnumStartClipLines, NtGdiPATHOBJ_vEnumStartClipLines, 4847, 0xC000000D, 'd', "result is STATUS_INVALID_PARAMETER when there is no UMPD object") \
X(PATH_vGetBounds, NtGdiPATHOBJ_vGetBounds, 4848, 0xC000000D, 'd', "result is STATUS_INVALID_PARAMETER when there is no UMPD object") \
X(STR_bEnum, NtGdiSTROBJ_bEnum, 4861, 0xFFFFFFFF, 'n', "NtGdiSTROBJ_bEnumInternal returns -1 without a UMPD object; the wrapper is void in the analysis so the leftover rax is only noted") \
X(STR_bEnumPositionsOnly, NtGdiSTROBJ_bEnumPositionsOnly, 4862, 0xFFFFFFFF, 'n', "same as bEnum: bEnumInternal(..., 1) returns -1 without a UMPD object") \
X(STR_bGetAdvanceWidths, NtGdiSTROBJ_bGetAdvanceWidths, 4863, 0x0, 'd', "final return 0 when there is no UMPD object") \
X(STR_dwGetCodePage, NtGdiSTROBJ_dwGetCodePage, 4864, 0x0, 'd', "result is 0 fallthrough when there is no UMPD object") \
X(STR_vEnumStart, NtGdiSTROBJ_vEnumStart, 4865, 0xC000000D, 'd', "result is STATUS_INVALID_PARAMETER when there is no UMPD object") \
X(XFORM_bApplyXform, NtGdiXFORMOBJ_bApplyXform, 4904, 0x0, 'd', "early return 0 when GetThreadCurrentObj == NULL") \
X(XFORM_iGetXform, NtGdiXFORMOBJ_iGetXform, 4905, 0xFFFFFFFF, 'd', "result is -1 when there is no UMPD object") \
X(XLATE_cGetPalette, NtGdiXLATEOBJ_cGetPalette, 4906, 0x0, 'd', "early return 0 when GetThreadCurrentObj == NULL") \
X(XLATE_hGetColorTransform, NtGdiXLATEOBJ_hGetColorTransform, 4907, 0x0, 'q', "result is 0 fallthrough when there is no UMPD object") \
X(XLATE_iXlate, NtGdiXLATEOBJ_iXlate, 4908, 0xFFFFFFFF, 'd', "result is 0xffffffff when there is no UMPD object") \
X(EngAlphaBlend, NtGdiEngAlphaBlend, 4725, 0x0, 'd', "early return 0 when GetThreadCurrentObj == NULL") \
X(EngBitBlt, NtGdiEngBitBlt, 4727, 0x0, 'q', "early return NULL when GetThreadCurrentObj == NULL") \
X(EngCheckAbort, NtGdiEngCheckAbort, 4728, 0x1, 'd', "result is 1 (not aborted) when there is no UMPD object") \
X(EngComputeGlyphSet, NtGdiEngComputeGlyphSet, 4729, 0x0, 'q', "the initial value 0 is returned when there is no UMPD object") \
X(EngCopyBits, NtGdiEngCopyBits, 4730, 0x0, 'd', "early return 0 when GetThreadCurrentObj == NULL") \
X(EngCreateClip, NtGdiEngCreateClip, 4732, 0x0, 'q', "result is NULL on the this == NULL branch") \
X(EngDeleteClip, NtGdiEngDeleteClip, 4736, 0xC000000D, 'd', "result is STATUS_INVALID_PARAMETER when there is no UMPD object") \
X(EngDeletePath, NtGdiEngDeletePath, 4738, 0xC000000D, 'd', "result is STATUS_INVALID_PARAMETER when there is no UMPD object") \
X(EngEraseSurface, NtGdiEngEraseSurface, 4740, 0x0, 'd', "result is 0 on the no-UMPD branch") \
X(EngFillPath, NtGdiEngFillPath, 4741, 0x0, 'd', "result is 0 on the this == NULL branch") \
X(EngGradientFill, NtGdiEngGradientFill, 4742, 0x0, 'd', "early return 0 when GetThreadCurrentObj == NULL") \
X(EngLineTo, NtGdiEngLineTo, 4743, 0x0, 'd', "result is 0 on the this == NULL branch") \
X(EngLockSurface, NtGdiEngLockSurface, 4744, 0x0, 'q', "result is NULL on the no-UMPD branch") \
X(EngPaint, NtGdiEngPaint, 4746, 0x0, 'd', "result is 0 on the this == NULL branch") \
X(EngPlgBlt, NtGdiEngPlgBlt, 4747, 0x0, 'd', "early return 0 when GetThreadCurrentObj == NULL") \
X(EngStretchBlt, NtGdiEngStretchBlt, 4748, 0x0, 'd', "early return 0 when GetThreadCurrentObj == NULL") \
X(EngStretchBltROP, NtGdiEngStretchBltROP, 4749, 0x0, 'q', "early return 0 when GetThreadCurrentObj == NULL") \
X(EngStrokeAndFillPath, NtGdiEngStrokeAndFillPath, 4750, 0x0, 'd', "result is 0 on the this == NULL branch") \
X(EngStrokePath, NtGdiEngStrokePath, 4751, 0x0, 'd', "result is 0 on the this == NULL branch") \
X(EngTextOut, NtGdiEngTextOut, 4752, 0x0, 'd', "result is NULL (low dword used as the BOOL) on the this == NULL branch") \
X(EngTransparentBlt, NtGdiEngTransparentBlt, 4753, 0x0, 'd', "early return 0 when GetThreadCurrentObj == NULL") \
X(EngUnlockSurface, NtGdiEngUnlockSurface, 4754, 0xC000000D, 'd', "result is STATUS_INVALID_PARAMETER when there is no UMPD object")

static const struct row g_rows[] = {
#define X(tag, sym, num, exp, mode, why) { #sym, num, exp, mode },
    UMPD_LIST(X)
#undef X
};
#define NROWS (sizeof g_rows / sizeof g_rows[0])

static int result_ok(const struct row *r, ULONG_PTR v) {
    if (r->mode == 'n') return 1;
    if (r->mode == 'd') return R2E_LO(v) == r->exp;
    return (unsigned long long)v == (unsigned long long)r->exp;
}

/* ---- canary buffers ------------------------------------------------------------------------ */
#define NCAN 14
static unsigned char g_can[NCAN][64] __attribute__((aligned(16)));
static void can_fill(void) { for (int i = 0; i < NCAN; i++) memset(g_can[i], 0xA0 + i, sizeof g_can[i]); }
static int can_intact(void) {
    for (int i = 0; i < NCAN; i++)
        for (unsigned j = 0; j < sizeof g_can[i]; j++) if (g_can[i][j] != (unsigned char)(0xA0 + i)) return 0;
    return 1;
}

/* Table-wide worker for the pointer-argument pass. Returns the number of mismatches. */
static int run_ptr_args(const struct row *r) {
    r2e_fn f = r2e_stub(r->name);
    if (!f) return -1;
    can_fill();
    unsigned long gdi0 = r2e_gdi_count();
    ULONG_PTR p[NCAN];
    for (int i = 0; i < NCAN; i++) p[i] = (ULONG_PTR)g_can[i];
    ULONG_PTR v = f(p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12], p[13]);
    int bad = 0;
    if (!result_ok(r, v)) { bad++; printf("  FAIL %s ptr args: got 0x%llx want 0x%lx\n", r->name, (unsigned long long)v, r->exp); }
    if (!can_intact()) { bad++; printf("  FAIL %s ptr args: a canary buffer was written\n", r->name); }
    if (r2e_gdi_count() != gdi0) { bad++; printf("  FAIL %s ptr args: GDI object count changed\n", r->name); }
    return bad;
}

#define X(tag, sym, num, exp, mode, why) \
TEST(r2_gdi_internal_eng_glue_##tag##_stub) { \
    r2e_fn f = r2e_stub(#sym); REQUIRE(f); \
    CHECK(k_stub_valid((FARPROC)f)); \
    r2e_check_number(#sym, num); \
} \
TEST(r2_gdi_internal_eng_glue_##tag##_ptr_args_untouched) { \
    static const struct row r = { #sym, num, exp, mode }; \
    r2e_fn f = r2e_stub(#sym); REQUIRE(f); \
    CHECKF(run_ptr_args(&r) == 0, "%s: " why, #sym); \
} \
TEST(r2_gdi_internal_eng_glue_##tag##_live_handles_survive) { \
    static const struct row r = { #sym, num, exp, mode }; \
    r2e_fn f = r2e_stub(#sym); REQUIRE(f); \
    HDC dc = U(CreateCompatibleDC)(NULL); \
    HBITMAP bm = U(CreateBitmap)(4, 4, 1, 1, NULL); \
    HPEN pen = U(CreatePen)(PS_SOLID, 1, RGB(1, 2, 3)); \
    HBRUSH br = U(CreateHatchBrush)(HS_CROSS, RGB(4, 5, 6)); /* not a solid brush: those stay parked in a cache after delete */ \
    if (!dc || !bm || !pen || !br) { t_skip("cannot create GDI objects"); return; } \
    unsigned long gdi0 = r2e_gdi_count(); \
    ULONG_PTR v = f((ULONG_PTR)dc, (ULONG_PTR)bm, (ULONG_PTR)pen, (ULONG_PTR)br, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10); \
    CHECKF(result_ok(&r, v), "%s: " why " (got 0x%llx)", #sym, (unsigned long long)v); \
    CHECK(U(GetObjectType)(dc) == OBJ_MEMDC); \
    CHECK(U(GetObjectType)(bm) == OBJ_BITMAP); \
    CHECK(U(GetObjectType)(pen) == OBJ_PEN); \
    CHECK(U(GetObjectType)(br) == OBJ_BRUSH); \
    CHECK(r2e_gdi_count() == gdi0); \
    U(DeleteObject)(br); U(DeleteObject)(pen); U(DeleteObject)(bm); U(DeleteDC)(dc); \
    CHECK(r2e_gdi_count() == gdi0 - 4); \
}
UMPD_LIST(X)
#undef X

/* The table has exactly the entries this file expects. */
TEST(r2_gdi_internal_eng_glue_table_size) { CHECK(NROWS == 53); }

/* Every row once more, all pointers, on a freshly created thread: the UMPDOBJ lookup is
 * per-thread (W32GetThreadWin32Thread), a new thread has none either. */
static DWORD WINAPI worker(LPVOID arg) {
    int bad = 0;
    for (unsigned i = 0; i < NROWS; i++) {
        int b = run_ptr_args(&g_rows[i]);
        if (b > 0) bad += b;
    }
    return (DWORD)bad;
}
TEST(r2_gdi_internal_eng_glue_all_rows_fresh_thread) {
    REQUIRE(r2e_stub("NtGdiEngBitBlt"));
    HANDLE h = CreateThread(NULL, 0, worker, NULL, 0, NULL);
    REQUIRE(h);
    DWORD w = WaitForSingleObject(h, 10000), code = 99;
    CHECK(w == WAIT_OBJECT_0);
    if (w == WAIT_OBJECT_0) { GetExitCodeThread(h, &code); CHECK(code == 0); }
    CloseHandle(h);
}

/* Five repetitions of the whole table: the no-UMPD path keeps no state, so the answers must
 * not drift (the UMPDOBJ reference counter at +0x1a4 is only touched after the guard). */
TEST(r2_gdi_internal_eng_glue_all_rows_repeat) {
    REQUIRE(r2e_stub("NtGdiEngBitBlt"));
    int bad = 0;
    for (int rep = 0; rep < 5; rep++)
        for (unsigned i = 0; i < NROWS; i++) { int b = run_ptr_args(&g_rows[i]); if (b > 0) bad += b; }
    CHECK(bad == 0);
}

/* ---- pristine process: the first win32k syscall of a fresh process is one of these ------------ */
static const char *const pristine_rows[] = {
    "NtGdiEngCheckAbort", "NtGdiXFORMOBJ_iGetXform", "NtGdiCLIPOBJ_cEnumStart", "NtGdiFONTOBJ_vGetInfo",
    "NtGdiPATHOBJ_vEnumStart", "NtGdiEngBitBlt", "NtGdiBRUSHOBJ_ulGetBrushColor", "NtGdiEngUnlockSurface",
};
CHILD(r2e_pristine_umpd) {
    int bad = 0;
    const char *name = argc > 1 ? argv[1] : "";
    const struct row *r = NULL;
    for (unsigned i = 0; i < NROWS; i++) if (!strcmp(g_rows[i].name, name)) r = &g_rows[i];
    if (!r) { printf("  FAIL unknown row %s\n", name); return 1; }
    r2e_fn f = (r2e_fn)t_proc_in("win32u.dll", name);
    if (!f) { printf("  FAIL cannot resolve %s\n", name); return 1; }
    /* No user32/gdi32 has been loaded in this process: the thread is not a GUI thread yet. */
    r2e_stub_callback_table();
    unsigned long long before = 0, after = 0;
    k_try_read((char *)k_teb() + TEB_Win32ThreadInfo, &before, 8);
    if (before != 0) { bad++; printf("  FAIL Win32ThreadInfo already 0x%llx before the first win32k call\n", before); }
    ULONG_PTR v = f(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    if (!result_ok(r, v)) { bad++; printf("  FAIL %s first call got 0x%llx want 0x%lx\n", name, (unsigned long long)v, r->exp); }
    /* The win32k thunk converted the thread before the body ran, whatever the body returned. */
    k_try_read((char *)k_teb() + TEB_Win32ThreadInfo, &after, 8);
    if (after == 0) { bad++; printf("  FAIL Win32ThreadInfo still NULL after %s\n", name); }
    /* Second call: same answer, no re-conversion. */
    v = f(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    if (!result_ok(r, v)) { bad++; printf("  FAIL %s second call got 0x%llx\n", name, (unsigned long long)v); }
    unsigned long long again = 0;
    k_try_read((char *)k_teb() + TEB_Win32ThreadInfo, &again, 8);
    if (again != after) { bad++; printf("  FAIL Win32ThreadInfo changed 0x%llx -> 0x%llx\n", after, again); }
    return bad;
}
TEST(r2_gdi_internal_eng_glue_pristine_first_syscall_children) {
    REQUIRE(r2e_stub("NtGdiEngBitBlt"));
    for (unsigned i = 0; i < sizeof pristine_rows / sizeof pristine_rows[0]; i++) {
        struct k_child_result r = k_child_run("r2e_pristine_umpd", pristine_rows[i], 15000);
        CHECKF(r.exit_code == 0, "%s as first win32k syscall: child exit 0x%lx", pristine_rows[i], (unsigned long)r.exit_code);
    }
}
