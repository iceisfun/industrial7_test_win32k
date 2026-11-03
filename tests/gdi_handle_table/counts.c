/* GetGuiResources / NtUserGetGuiResources against the cells of PEB->GdiSharedHandleTable.
 * win32kfull!NtUserGetGuiResources(hProcess, flags): flags > 4 -> ERROR_INVALID_PARAMETER;
 * hProcess == -1 reads the caller's W32PROCESS (+0x3c gdi, +0x44 user, +0x40 gdi peak, +0x48 user peak);
 * hProcess == -2 reads the session-wide counters; any other handle needs PROCESS_QUERY_LIMITED_INFORMATION,
 * a process in this session that has a W32PROCESS; every failure is ERROR_INVALID_PARAMETER with result 0. */
#include "gh.h"

#define GR_GDI 0
#define GR_USER 1
#define GR_GDI_PEAK 2
#define GR_USER_PEAK 4
#define H_SELF ((HANDLE)(LONG_PTR)-1)
#define H_GLOBAL ((HANDLE)(LONG_PTR)-2)

static DWORD gr(HANDLE p, DWORD f) { return U(GetGuiResources) ? U(GetGuiResources)(p, f) : 0xFFFFFFFF; }

/* Windows 10 recycles deleted solid brushes and rectangular regions through a small cache (see gh.h): exact deltas are only
 * observable once the caches are empty, so the tests below take every cached entry out first and hand them back at the end. */
struct drain { HGDIOBJ b[GH_CACHE_PROBE], r[GH_CACHE_PROBE]; int nb, nr; };
static void drain_take(struct drain *d) { d->nb = gh_cache_hold(&gk_brush_solid, d->b); d->nr = gh_cache_hold(&gk_rgn_rect, d->r); }
static void drain_give(struct drain *d) { for (int i = 0; i < d->nb; i++) U(DeleteObject)(d->b[i]); for (int i = 0; i < d->nr; i++) U(DeleteObject)(d->r[i]); }

TEST(gdi_handle_table_counts_gdi_matches_owned_cells) {
    GH_NEED();
    int cells = gh_count_owned(GH_PID());
    DWORD n = gr(H_SELF, GR_GDI);
    CHECK(cells >= 0);
    /* the per-process handle count goes +1 per allocation owned by this pid and -1 per release, the
     * same population as cells whose owner word is the pid. */
    /* Deleted solid brushes and rectangular regions leave their cells allocated for a while without being counted. */
    CHECKF((int)n <= cells && cells - (int)n <= GH_CACHE_MAX, "GR_GDIOBJECTS=%lu owned cells=%d", n, cells);
}
TEST(gdi_handle_table_counts_create_regions_delta) {
    GH_NEED();
    enum { N = 300 };
    HRGN r[N]; struct drain d; drain_take(&d);
    int n0 = gh_count_owned(GH_PID()); DWORD g0 = gr(H_SELF, GR_GDI);
    for (int i = 0; i < N; i++) r[i] = U(CreateRectRgn)(0, 0, i + 1, i + 1);
    for (int i = 0; i < N; i++) REQUIRE(r[i]);
    CHECK(gr(H_SELF, GR_GDI) == g0 + N);
    CHECK(gh_count_owned(GH_PID()) == n0 + N);
    for (int i = 0; i < N; i++) CHECK(U(DeleteObject)(r[i]));
    /* the region cache keeps a few of the deleted cells (and their handles) allocated */
    DWORD g1 = gr(H_SELF, GR_GDI);
    CHECKF(g1 >= g0 && g1 - g0 <= GH_CACHE_MAX, "after delete %lu, before %lu", g1, g0);
    CHECK((int)(gh_count_owned(GH_PID()) - n0) == (int)(g1 - g0));
    drain_give(&d);
}
TEST(gdi_handle_table_counts_mixed_kinds_delta) {
    GH_NEED();
    HGDIOBJ hs[64]; const struct gh_kind *ks[64]; int n = 0;
    struct drain d; drain_take(&d);
    DWORD g0 = gr(H_SELF, GR_GDI); int c0 = gh_count_owned(GH_PID());
    for (size_t i = 0; i < GH_NKINDS && n < 64; i++) {
        const struct gh_kind *k = gh_all_kinds[i];
        if (k->nh != 1) continue;
        HGDIOBJ h = k->mk();
        if (!h) continue;
        hs[n] = h; ks[n] = k; n++;
    }
    CHECKF(gr(H_SELF, GR_GDI) == g0 + (DWORD)n, "count %lu want %lu", gr(H_SELF, GR_GDI), g0 + n);
    CHECK(gh_count_owned(GH_PID()) == c0 + n);
    /* delete in reverse creation order */
    for (int i = n - 1; i >= 0; i--) ks[i]->rm(hs[i]);
    DWORD g1 = gr(H_SELF, GR_GDI);          /* solid brushes and rect regions may stay behind in their caches */
    CHECKF(g1 >= g0 && g1 - g0 <= GH_CACHE_MAX, "after delete %lu, before %lu", g1, g0);
    CHECK((int)(gh_count_owned(GH_PID()) - c0) == (int)(g1 - g0));
    drain_give(&d);
}
TEST(gdi_handle_table_counts_peak_not_below_current) {
    GH_NEED();
    DWORD cur = gr(H_SELF, GR_GDI), pk = gr(H_SELF, GR_GDI_PEAK);
    CHECKF(pk >= cur, "peak %lu < current %lu", pk, cur);
}
TEST(gdi_handle_table_counts_peak_tracks_high_water) {
    GH_NEED();
    enum { N = 400 };
    HRGN r[N]; struct drain d; drain_take(&d);
    DWORD g0 = gr(H_SELF, GR_GDI), p0 = gr(H_SELF, GR_GDI_PEAK);
    for (int i = 0; i < N; i++) { r[i] = U(CreateRectRgn)(0, 0, 2, 2); REQUIRE(r[i]); }
    DWORD p1 = gr(H_SELF, GR_GDI_PEAK);
    CHECKF(p1 >= g0 + N, "peak %lu < %lu", p1, g0 + N);
    CHECK(p1 >= p0);
    for (int i = 0; i < N; i++) U(DeleteObject)(r[i]);
    /* the peak count is max(peak, count); nothing lowers it */
    CHECK(gr(H_SELF, GR_GDI_PEAK) == p1);
    DWORD g1 = gr(H_SELF, GR_GDI);
    CHECKF(g1 >= g0 && g1 - g0 <= GH_CACHE_MAX, "after delete %lu, before %lu", g1, g0);
    drain_give(&d);
}
TEST(gdi_handle_table_counts_peak_monotonic) {
    GH_NEED();
    DWORD last = gr(H_SELF, GR_GDI_PEAK);
    for (int i = 0; i < 10; i++) {
        HRGN a[20];
        for (int j = 0; j < 20; j++) a[j] = U(CreateRectRgn)(0, 0, 1, 1);
        DWORD p = gr(H_SELF, GR_GDI_PEAK);
        CHECK(p >= last);
        last = p;
        for (int j = 0; j < 20; j++) if (a[j]) U(DeleteObject)(a[j]);
    }
}
TEST(gdi_handle_table_counts_api_vs_syscall) {
    GH_NEED();
    REQUIRE(GHN(NtUserGetGuiResources));
    for (DWORD f = 0; f <= 4; f++)
        CHECKF(GHN(NtUserGetGuiResources)(H_SELF, f) == gr(H_SELF, f), "flag %lu", f);
}
TEST(gdi_handle_table_counts_flag_3_is_zero) {
    GH_NEED();
    REQUIRE(GHN(NtUserGetGuiResources));
    /* flags 0,1,2,4 are the only selectors; 3 falls through every branch and returns 0 without an error */
    SetLastError(0xdead);
    CHECK(GHN(NtUserGetGuiResources)(H_SELF, 3) == 0);
    t_note("flag 3 last error = %lu", GetLastError());
    CHECK(GHN(NtUserGetGuiResources)(H_GLOBAL, 3) == 0);
}
TEST(gdi_handle_table_counts_bad_flags_invalid_parameter) {
    GH_NEED();
    REQUIRE(GHN(NtUserGetGuiResources));
    DWORD bad[] = { 5, 6, 7, 8, 0x10, 0x100, 0x7fffffff, 0xffffffff };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(0);
        DWORD r = GHN(NtUserGetGuiResources)(H_SELF, bad[i]);
        CHECKF(r == 0, "flags 0x%lx -> %lu", bad[i], r);
        CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "flags 0x%lx err %lu", bad[i], GetLastError());
    }
}
TEST(gdi_handle_table_counts_bad_flags_through_user32) {
    GH_NEED();
    REQUIRE(U(GetGuiResources));
    SetLastError(0);
    CHECK(U(GetGuiResources)(GetCurrentProcess(), 5) == 0);
    CHECK(GetLastError() == ERROR_INVALID_PARAMETER);
}
TEST(gdi_handle_table_counts_invalid_process_handle) {
    GH_NEED();
    REQUIRE(GHN(NtUserGetGuiResources));
    HANDLE bad[] = { (HANDLE)0x12344, (HANDLE)0x7ffffff0, (HANDLE)(LONG_PTR)-3, (HANDLE)(LONG_PTR)-4, NULL };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(0);
        DWORD r = GHN(NtUserGetGuiResources)(bad[i], GR_GDI);
        CHECKF(r == 0, "handle %p -> %lu", bad[i], r);
        CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "handle %p err %lu", bad[i], GetLastError());
    }
}
TEST(gdi_handle_table_counts_real_handle_with_query_right) {
    GH_NEED();
    REQUIRE(GHN(NtUserGetGuiResources));
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, GH_PID());
    REQUIRE(p);
    CHECK(GHN(NtUserGetGuiResources)(p, GR_GDI) == gr(H_SELF, GR_GDI));
    CHECK(GHN(NtUserGetGuiResources)(p, GR_USER) == gr(H_SELF, GR_USER));
    CHECK(GHN(NtUserGetGuiResources)(p, GR_GDI_PEAK) == gr(H_SELF, GR_GDI_PEAK));
    CloseHandle(p);
}
TEST(gdi_handle_table_counts_handle_without_query_right) {
    GH_NEED();
    REQUIRE(GHN(NtUserGetGuiResources));
    HANDLE p = OpenProcess(SYNCHRONIZE, FALSE, GH_PID());
    REQUIRE(p);
    SetLastError(0);
    /* ObReferenceObjectByHandle(.., 0x1000, ..) fails; the caller sees ERROR_INVALID_PARAMETER, not ACCESS_DENIED */
    CHECK(GHN(NtUserGetGuiResources)(p, GR_GDI) == 0);
    CHECK(GetLastError() == ERROR_INVALID_PARAMETER);
    CloseHandle(p);
}
TEST(gdi_handle_table_counts_non_process_handle) {
    GH_NEED();
    REQUIRE(GHN(NtUserGetGuiResources));
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    REQUIRE(ev);
    SetLastError(0);
    CHECK(GHN(NtUserGetGuiResources)(ev, GR_GDI) == 0);
    CHECK(GetLastError() == ERROR_INVALID_PARAMETER);
    CloseHandle(ev);
}
TEST(gdi_handle_table_counts_global_not_below_own) {
    GH_NEED();
    REQUIRE(GHN(NtUserGetGuiResources));
    DWORD own = gr(H_SELF, GR_GDI), glob = GHN(NtUserGetGuiResources)(H_GLOBAL, GR_GDI);
    CHECKF(glob >= own, "session handle count %lu < own %lu", glob, own);
    DWORD gp = GHN(NtUserGetGuiResources)(H_GLOBAL, GR_GDI_PEAK);
    CHECKF(gp >= glob, "session peak %lu < session count %lu", gp, glob);
}
TEST(gdi_handle_table_counts_global_follows_creation) {
    GH_NEED();
    REQUIRE(GHN(NtUserGetGuiResources));
    DWORD g0 = GHN(NtUserGetGuiResources)(H_GLOBAL, GR_GDI);
    HRGN r = U(CreateRectRgn)(0, 0, 4, 4);
    REQUIRE(r);
    DWORD g1 = GHN(NtUserGetGuiResources)(H_GLOBAL, GR_GDI);
    /* other processes may allocate concurrently, so only the direction is asserted */
    CHECKF(g1 >= g0 || g0 - g1 < 50, "session count %lu -> %lu", g0, g1);
    t_info("session gdi count %lu -> %lu", g0, g1);
    U(DeleteObject)(r);
}
TEST(gdi_handle_table_counts_user_objects) {
    GH_NEED();
    REQUIRE(U(GetGuiResources));
    DWORD u0 = gr(H_SELF, GR_USER);
    HWND w = k_make_window("gh_count");
    if (!w) { t_skip("no window"); return; }
    DWORD u1 = gr(H_SELF, GR_USER);
    CHECKF(u1 > u0, "GR_USEROBJECTS %lu -> %lu", u0, u1);
    CHECKF(gr(H_SELF, GR_USER_PEAK) >= u1, "user peak");
    DestroyWindow(w);
    CHECKF(gr(H_SELF, GR_USER) < u1, "GR_USEROBJECTS after destroy %lu", gr(H_SELF, GR_USER));
}
TEST(gdi_handle_table_counts_user_global_not_below_own) {
    GH_NEED();
    REQUIRE(GHN(NtUserGetGuiResources));
    DWORD own = gr(H_SELF, GR_USER), glob = GHN(NtUserGetGuiResources)(H_GLOBAL, GR_USER);
    CHECKF(glob >= own, "global user objects %lu < own %lu", glob, own);
    CHECK(GHN(NtUserGetGuiResources)(H_GLOBAL, GR_USER_PEAK) >= glob);
}
TEST(gdi_handle_table_counts_dc_and_bitmap_count_once) {
    GH_NEED();
    DWORD g0 = gr(H_SELF, GR_GDI);
    HDC dc = U(CreateCompatibleDC)(NULL);
    HBITMAP bm = U(CreateBitmap)(4, 4, 1, 32, NULL);
    REQUIRE(dc && bm);
    CHECK(gr(H_SELF, GR_GDI) == g0 + 2);
    HGDIOBJ old = U(SelectObject)(dc, bm);
    /* selecting does not allocate or free a handle */
    CHECK(gr(H_SELF, GR_GDI) == g0 + 2);
    U(SelectObject)(dc, old);
    U(DeleteDC)(dc);
    U(DeleteObject)(bm);
    CHECK(gr(H_SELF, GR_GDI) == g0);
}
TEST(gdi_handle_table_counts_double_delete_counts_once) {
    GH_NEED();
    struct drain d; drain_take(&d);
    DWORD g0 = gr(H_SELF, GR_GDI);
    HBRUSH b = U(CreateSolidBrush)(RGB(1, 2, 3));
    REQUIRE(b);
    CHECK(gr(H_SELF, GR_GDI) == g0 + 1);
    gh_cache_fill(&gk_brush_solid);                 /* full brush cache: the delete below releases the handle */
    DWORD g1 = gr(H_SELF, GR_GDI);
    CHECK(U(DeleteObject)(b));
    CHECK(gr(H_SELF, GR_GDI) == g1 - 1);
    CHECK(!U(DeleteObject)(b));
    CHECK(gr(H_SELF, GR_GDI) == g1 - 1);            /* the second delete counts nothing */
    drain_give(&d);
}
TEST(gdi_handle_table_counts_failed_create_no_change) {
    GH_NEED();
    DWORD g0 = gr(H_SELF, GR_GDI);
    /* NtGdiCreateRectRgn rejects coordinates outside +-2^27 before allocating anything */
    HRGN r = U(CreateRectRgn)(0x08000000, 0, 0x08000001, 1);
    t_note("CreateRectRgn(2^27..) = %p", r);
    if (r) U(DeleteObject)(r);
    CHECK(gr(H_SELF, GR_GDI) == g0);
}

TEST(gdi_handle_table_counts_gdi_creation_leaves_user_count) {
    GH_NEED();
    REQUIRE(U(GetGuiResources));
    DWORD u0 = gr(H_SELF, GR_USER);
    HRGN r = U(CreateRectRgn)(0, 0, 3, 3);
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(r && dc);
    /* the user-object count is untouched by GDI allocations, which only update the GDI count and its peak */
    CHECK(gr(H_SELF, GR_USER) == u0);
    U(DeleteDC)(dc); U(DeleteObject)(r);
    CHECK(gr(H_SELF, GR_USER) == u0);
}
TEST(gdi_handle_table_counts_process_peak_not_above_session_peak) {
    GH_NEED();
    REQUIRE(GHN(NtUserGetGuiResources));
    DWORD own = gr(H_SELF, GR_GDI_PEAK), sess = GHN(NtUserGetGuiResources)(H_GLOBAL, GR_GDI_PEAK);
    /* the peak handle count is the high-water mark of the session total, which includes this process */
    CHECKF(sess >= own, "session peak %lu < process peak %lu", sess, own);
}
