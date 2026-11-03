/* kprobe_user_pointers/kprobe_getpath.c
 *
 * NtGdiGetPath(hdc, pts, types, cpt) (win32kfull 0x2ac9e0): when cpt != 0 it first rejects
 * cpt < 0 or cpt < cTotalPts or cpt > 0x1fffffff with EngSetLastError(0x57) and a -1 result, then
 * calls ProbeForWrite(pts, cpt*8, 4) and ProbeForWrite(types, cpt, 4) BEFORE any enumeration
 * writes, so a failing probe leaves both buffers untouched. Both probes use alignment 4, hence an
 * unaligned types pointer fails even though it is a byte array. Also NtGdiGetTextMetricsW's
 * `0x43 < cb` gate (no probe, no write below 0x44) and NtGdiGetBitmapBits' min(cb, real size).
 */
#include "kprobe.h"

typedef int (WINAPI *kp_getpath_t)(HDC, POINT *, BYTE *, int);

/* memory DC with a 4-point open path (MoveTo + 3 LineTo), so cTotalPts == 4 */
static HDC kp_path4_dc(void) {
    if (!U(CreateCompatibleDC) || !U(BeginPath) || !U(MoveToEx) || !U(LineTo) || !U(EndPath)) return NULL;
    HDC dc = U(CreateCompatibleDC)(NULL);
    if (!dc) return NULL;
    U(BeginPath)(dc);
    U(MoveToEx)(dc, 0, 0, NULL);
    U(LineTo)(dc, 10, 0);
    U(LineTo)(dc, 10, 10);
    U(LineTo)(dc, 0, 10);
    if (!U(EndPath)(dc)) { U(DeleteDC)(dc); return NULL; }
    return dc;
}

TEST(kprobe_user_pointers_getpath_exact_fit_writes_points_and_types) {
    kp_getpath_t p = (kp_getpath_t)t_proc_in("win32u.dll", "NtGdiGetPath");
    REQUIRE(p);
    HDC dc = kp_path4_dc();
    REQUIRE(dc);
    int n = p(dc, NULL, NULL, 0); /* cpt == 0 is the "count points" query */
    if (n != 4) { t_note("path count %d (expected 4)", n); }
    if (n != 4) { U(DeleteDC)(dc); t_skip("path fixture did not produce 4 points"); return; }
    struct kp_guard gp, gt;
    if (!kp_guard_alloc(32, &gp)) { U(DeleteDC)(dc); t_skip("guard alloc failed"); return; }
    if (!kp_guard_alloc(4, &gt)) { kp_guard_free(&gp); U(DeleteDC)(dc); t_skip("guard alloc failed"); return; }
    kp_guard_fill(&gp, 0xCC); kp_guard_fill(&gt, 0xCC);
    kp_veh_reset();
    int r = p(dc, (POINT *)gp.buf, (BYTE *)gt.buf, 4);
    CHECKF(r == 4, "exact-fit buffers must succeed with the point count, got %d", r);
    CHECK(kp_veh_hits() == 0);
    CHECKF(!kp_guard_untouched(&gp, 0, 32, 0xCC), "points buffer must have been written");
    if (r == 4) {
        const BYTE *ty = (const BYTE *)gt.buf;
        CHECKF(ty[0] == PT_MOVETO, "first type must be PT_MOVETO, got 0x%x", ty[0]);
        for (int i = 1; i < 4; i++) CHECKF(ty[i] == PT_LINETO, "type[%d]=0x%x must be PT_LINETO", i, ty[i]);
    }
    kp_guard_free(&gp); kp_guard_free(&gt);
    U(DeleteDC)(dc);
}

TEST(kprobe_user_pointers_getpath_types_one_byte_short_fails_without_writes) {
    kp_getpath_t p = (kp_getpath_t)t_proc_in("win32u.dll", "NtGdiGetPath");
    REQUIRE(p);
    HDC dc = kp_path4_dc();
    REQUIRE(dc);
    if (p(dc, NULL, NULL, 0) != 4) { U(DeleteDC)(dc); t_skip("path fixture did not produce 4 points"); return; }
    /* cpt 5 >= cTotalPts passes the count gate; types has room for 4 bytes only, so the second
     * ProbeForWrite(types, 5, 4) crosses into the reserved page. Both probes precede any write. */
    struct kp_guard gp, gt;
    if (!kp_guard_alloc(40, &gp)) { U(DeleteDC)(dc); t_skip("guard alloc failed"); return; }
    if (!kp_guard_alloc(4, &gt)) { kp_guard_free(&gp); U(DeleteDC)(dc); t_skip("guard alloc failed"); return; }
    kp_guard_fill(&gp, 0xCC); kp_guard_fill(&gt, 0xCC);
    kp_veh_reset();
    int r = p(dc, (POINT *)gp.buf, (BYTE *)gt.buf, 5);
    CHECKF(r != 4 && r != 5, "types buffer crossing the guard page must fail, got %d", r);
    CHECKF(kp_veh_hits() == 0, "hits=%u", kp_veh_hits());
    CHECK(kp_guard_untouched(&gp, 0, 40, 0xCC));
    CHECK(kp_guard_untouched(&gt, 0, 4, 0xCC));
    kp_guard_free(&gp); kp_guard_free(&gt);
    U(DeleteDC)(dc);
}

TEST(kprobe_user_pointers_getpath_points_one_entry_short_fails_without_writes) {
    kp_getpath_t p = (kp_getpath_t)t_proc_in("win32u.dll", "NtGdiGetPath");
    REQUIRE(p);
    HDC dc = kp_path4_dc();
    REQUIRE(dc);
    if (p(dc, NULL, NULL, 0) != 4) { U(DeleteDC)(dc); t_skip("path fixture did not produce 4 points"); return; }
    struct kp_guard gp, gt;
    if (!kp_guard_alloc(32, &gp)) { U(DeleteDC)(dc); t_skip("guard alloc failed"); return; }
    if (!kp_guard_alloc(8, &gt)) { kp_guard_free(&gp); U(DeleteDC)(dc); t_skip("guard alloc failed"); return; }
    kp_guard_fill(&gp, 0xCC); kp_guard_fill(&gt, 0xCC);
    kp_veh_reset();
    int r = p(dc, (POINT *)gp.buf, (BYTE *)gt.buf, 5); /* 5*8 = 40 bytes wanted, 32 available */
    CHECKF(r != 4 && r != 5, "points buffer crossing the guard page must fail, got %d", r);
    CHECKF(kp_veh_hits() == 0, "hits=%u", kp_veh_hits());
    CHECK(kp_guard_untouched(&gp, 0, 32, 0xCC));
    CHECK(kp_guard_untouched(&gt, 0, 8, 0xCC));
    kp_guard_free(&gp); kp_guard_free(&gt);
    U(DeleteDC)(dc);
}

TEST(kprobe_user_pointers_getpath_unaligned_types_pointer_fails) {
    kp_getpath_t p = (kp_getpath_t)t_proc_in("win32u.dll", "NtGdiGetPath");
    REQUIRE(p);
    HDC dc = kp_path4_dc();
    REQUIRE(dc);
    if (p(dc, NULL, NULL, 0) != 4) { U(DeleteDC)(dc); t_skip("path fixture did not produce 4 points"); return; }
    /* ProbeForWrite(types, cpt, 4): a misaligned pointer raises DATATYPE_MISALIGNMENT inside the
     * guarded region even though types is a byte array. */
    struct kp_guard gp, gt;
    if (!kp_guard_alloc(32, &gp)) { U(DeleteDC)(dc); t_skip("guard alloc failed"); return; }
    if (!kp_guard_alloc(16, &gt)) { kp_guard_free(&gp); U(DeleteDC)(dc); t_skip("guard alloc failed"); return; }
    kp_guard_fill(&gp, 0xCC); kp_guard_fill(&gt, 0xCC);
    kp_veh_reset();
    int r = p(dc, (POINT *)gp.buf, (BYTE *)gt.buf + 1, 4);
    CHECKF(r != 4, "misaligned types pointer must fail, got %d", r);
    CHECKF(kp_veh_hits() == 0, "hits=%u", kp_veh_hits());
    CHECK(kp_guard_untouched(&gt, 0, 16, 0xCC));
    kp_guard_free(&gp); kp_guard_free(&gt);
    U(DeleteDC)(dc);
}

TEST(kprobe_user_pointers_getpath_count_gate_sets_invalid_parameter) {
    kp_getpath_t p = (kp_getpath_t)t_proc_in("win32u.dll", "NtGdiGetPath");
    REQUIRE(p);
    HDC dc = kp_path4_dc();
    REQUIRE(dc);
    if (p(dc, NULL, NULL, 0) != 4) { U(DeleteDC)(dc); t_skip("path fixture did not produce 4 points"); return; }
    POINT pts[8]; BYTE types[8];
    /* cpt (3) < cTotalPts (4): EngSetLastError(0x57), result -1, before any probe. */
    SetLastError(0);
    int r = p(dc, pts, types, 3);
    CHECKF(r == -1, "cpt smaller than the point count must return -1, got %d", r);
    CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "last error %lu, want 87", GetLastError());
 /* negative cpt: rejected by the (int)argument 4 < 0 test with 0x57. */
    SetLastError(0);
    r = p(dc, pts, types, -1);
    CHECKF(r == -1, "negative cpt must return -1, got %d", r);
    CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "last error %lu, want 87", GetLastError());
    /* cpt above 0x1fffffff is rejected before probing */
    SetLastError(0);
    r = p(dc, pts, types, 0x20000000);
    CHECKF(r == -1, "oversized cpt must return -1, got %d", r);
    CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "last error %lu, want 87", GetLastError());
    /* NULL HDC: DCOBJ is null -> 0x57 */
    SetLastError(0);
    r = p(NULL, pts, types, 4);
    CHECKF(r == -1, "NULL HDC must return -1, got %d", r);
    CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "last error %lu, want 87", GetLastError());
    U(DeleteDC)(dc);
}

/* NtGdiGetTextMetricsW: `0x43 < cb` gate; below it nothing is probed or written, result 0. */
TEST(kprobe_user_pointers_gettextmetricsw_cb_0x43_is_rejected_without_write) {
    typedef int (WINAPI *fn_t)(HDC, void *, UINT);
    fn_t p = (fn_t)t_proc_in("win32u.dll", "NtGdiGetTextMetricsW");
    REQUIRE(p);
    if (t_is_wine()) { t_skip("Wine GDI lock behaviour, see kprobe_nullreq.c"); return; }
    HDC dc = U(CreateCompatibleDC) ? U(CreateCompatibleDC)(NULL) : NULL;
    REQUIRE(dc);
    struct kp_guard g;
    if (!kp_guard_alloc(0x44, &g)) { U(DeleteDC)(dc); t_skip("guard alloc failed"); return; }
    kp_guard_fill(&g, 0xCC);
    kp_veh_reset();
    int r = p(dc, g.buf, 0x43);
    CHECKF(r == 0, "cb 0x43 must return 0, got %d", r);
    CHECK(kp_veh_hits() == 0);
    CHECK(kp_guard_untouched(&g, 0, 0x44, 0xCC));
    /* cb larger than 0x44 still copies exactly 0x44 bytes: the destination is probed for 0x44 */
    r = p(dc, g.buf, 0x1000);
    CHECKF(r != 0, "cb above 0x44 with a 0x44-byte buffer must succeed, got %d", r);
    CHECK(kp_veh_hits() == 0);
    kp_guard_free(&g);
    U(DeleteDC)(dc);
}

/* NtGdiGetBitmapBits: copy length is min(cb, real size); the probe covers that length only. */
TEST(kprobe_user_pointers_getbitmapbits_short_cb_writes_no_more_than_cb) {
    typedef DWORD (WINAPI *fn_t)(HBITMAP, DWORD, LPVOID);
    fn_t p = (fn_t)t_proc_in("win32u.dll", "NtGdiGetBitmapBits");
    REQUIRE(p);
    HBITMAP bmp = kp_fixture_bitmap(32, 4);
    REQUIRE(bmp);
    DWORD want = kp_fixture_bitmap_bits_size(bmp);
    if (want < 4) { U(DeleteObject)(bmp); t_skip("unexpected bitmap size"); return; }
    struct kp_guard g;
    if (!kp_guard_alloc(want, &g)) { U(DeleteObject)(bmp); t_skip("guard alloc failed"); return; }
    kp_guard_fill(&g, 0xCC);
    kp_veh_reset();
    DWORD r = p(bmp, want - 1, g.buf);
    CHECKF(r <= want - 1, "must not report more than cb bytes, got %lu of cb %lu", r, want - 1);
    CHECK(kp_veh_hits() == 0);
    CHECKF(kp_guard_untouched(&g, want - 1, 1, 0xCC), "byte beyond cb must stay untouched");
    /* a cb far above the real size is clamped to it: exact-size buffer still succeeds */
    kp_guard_fill(&g, 0xCC);
    r = p(bmp, want + 4096, g.buf);
    CHECKF(r == want, "oversized cb is clamped to the real size %lu, got %lu", want, r);
    CHECK(kp_veh_hits() == 0);
    kp_guard_free(&g);
    U(DeleteObject)(bmp);
}
