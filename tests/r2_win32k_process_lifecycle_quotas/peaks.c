/* peaks: GetGuiResources current/peak counters and the per-process handle quotas, each scenario in its own child.
 * win32kbase analysis: GDI handle allocation fails when the process handle quota is reached; the
 * peak is tracked separately. USER object allocation fails with last error 0x486 when the user handle quota is reached
 * (+0x44); the USER peak is +0x48. Defaults are 10000 for both (registry GDIProcessHandleQuota / USERProcessHandleQuota
 * under HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Windows; GDI value must be 0..65536, USER 200..18000).
 */
#include "r2.h"

#define MAXOBJ 12000
static void *g_h[MAXOBJ];
/* Churn objects are 1x1 bitmaps: win32k keeps a few deleted brushes (and regions) in a per-process cache that stays
 * counted by GetGuiResources and is reused by the next brush, so brushes do not give exact count relations. Bitmaps are
 * freed at once. */
static void *mkobj(int i) { (void)i; return U(CreateBitmap)(1, 1, 1, 1, NULL); }

static unsigned reg_q(const char *name, int *found) {
    HKEY k; DWORD v = 0, sz = sizeof v, ty = 0; *found = 0;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Windows", 0, KEY_READ, &k)) return 0;
    LONG r = RegQueryValueExA(k, name, NULL, &ty, (BYTE *)&v, &sz);
    RegCloseKey(k);
    if (r || ty != REG_DWORD) return 0;
    *found = 1; return v;
}
/* Effective quota when the configuration is the stock one; 0 otherwise (exact-count checks are then skipped). */
static unsigned std_quota(const char *name) {
    int f; unsigned v = reg_q(name, &f);
    if (!f || v == 10000) return 10000;
    t_note("%s configured as %u: exact-quota checks skipped", name, v);
    return 0;
}

R2_SCN(peaks_gdi_high_water_after_delete) {
    R2_CHILD_NEED_WIN();
    DWORD g0 = r2_gur(0), p0 = r2_gur(2);
    RC(p0 >= g0);
    for (int i = 0; i < 300; i++) g_h[i] = mkobj(RGB(i & 255, 0, 0));
    DWORD g1 = r2_gur(0), p1 = r2_gur(2);
    REQ(g1, g0 + 300);
    RC(p1 >= g1);
    for (int i = 0; i < 300; i++) U(DeleteObject)(g_h[i]);
    REQ(r2_gur(0), g0);
    REQ(r2_gur(2), p1);                                  /* deleting never lowers the peak */
    for (int i = 0; i < 100; i++) g_h[i] = mkobj(0);
    REQ(r2_gur(2), p1);                                  /* below the old mark the peak stays put */
    for (int i = 0; i < 100; i++) U(DeleteObject)(g_h[i]);
}
R2_SCN(peaks_user_high_water_after_delete) {
    R2_CHILD_NEED_WIN();
    DWORD u0 = r2_gur(1), p0 = r2_gur(4);
    RC(p0 >= u0);
    for (int i = 0; i < 200; i++) g_h[i] = U(CreateMenu)();
    DWORD u1 = r2_gur(1), p1 = r2_gur(4);
    REQ(u1, u0 + 200);
    RC(p1 >= u1);
    for (int i = 0; i < 200; i++) U(DestroyMenu)(g_h[i]);
    REQ(r2_gur(1), u0);
    REQ(r2_gur(4), p1);
}
R2_SCN(peaks_never_below_current_while_growing) {
    R2_CHILD_NEED_WIN();
    int bad = 0;
    for (int i = 0; i < 400; i++) {
        g_h[i] = mkobj(i);
        if (i % 7 == 0) { DWORD c = r2_gur(0), p = r2_gur(2); if (p < c) bad++; }
        if (i % 5 == 0) { g_h[2000 + i / 5] = U(CreateMenu)(); DWORD c = r2_gur(1), p = r2_gur(4); if (p < c) bad++; }
    }
    RCF(bad == 0, "%d samples with peak below current", bad);
    for (int i = 0; i < 400; i++) U(DeleteObject)(g_h[i]);
    for (int i = 0; i < 80; i++) U(DestroyMenu)(g_h[2000 + i]);
}
R2_SCN(peaks_gdi_and_user_counters_independent) {
    R2_CHILD_NEED_WIN();
    DWORD g = r2_gur(0), gp = r2_gur(2), u = r2_gur(1), up = r2_gur(4);
    for (int i = 0; i < 60; i++) g_h[i] = U(CreateMenu)();
    REQ(r2_gur(0), g); REQ(r2_gur(2), gp);               /* menus are USER objects only */
    for (int i = 0; i < 60; i++) U(DestroyMenu)(g_h[i]);
    up = r2_gur(4);                                      /* the menus raised the USER peak; the GDI objects below must not move it */
    RC(up >= u + 60);
    for (int i = 0; i < 60; i++) g_h[i] = mkobj(0);
    REQ(r2_gur(1), u); REQ(r2_gur(4), up);
    for (int i = 0; i < 60; i++) U(DeleteObject)(g_h[i]);
}
R2_SCN(peaks_stock_objects_do_not_count) {
    R2_CHILD_NEED_WIN();
    DWORD g = r2_gur(0), p = r2_gur(2);
    for (int i = 0; i < 20; i++) U(GetStockObject)(i % 17);
    REQ(r2_gur(0), g); REQ(r2_gur(2), p);
}
R2_SCN(peaks_dc_counts_one_gdi_handle) {
    R2_CHILD_NEED_WIN();
    DWORD g = r2_gur(0);
    HDC dc = U(CreateCompatibleDC)(NULL); RC(dc != NULL);
    REQ(r2_gur(0), g + 1);
    U(DeleteDC)(dc);
    REQ(r2_gur(0), g);
}
R2_SCN(peaks_second_process_handle_sees_same_peak) {
    R2_CHILD_NEED_WIN();
    HANDLE h = NULL;
    RC(DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(), &h, PROCESS_QUERY_INFORMATION, FALSE, 0));
    for (int i = 0; i < 50; i++) g_h[i] = mkobj(0);
    for (int i = 0; i < 50; i++) U(DeleteObject)(g_h[i]);
    for (DWORD f = 0; f <= 4; f++) if (f != 3) REQ(U(GetGuiResources)(h, f), U(GetGuiResources)(GetCurrentProcess(), f));
    CloseHandle(h);
}

R2_SCN(quota_gdi_fill_to_limit_exact) {
    R2_CHILD_NEED_WIN();
    unsigned q = std_quota("GDIProcessHandleQuota"); if (!q) return;
    DWORD g0 = r2_gur(0); int n = 0;
    SetLastError(0x7777);
    for (; n < MAXOBJ; n++) { g_h[n] = mkobj(n); if (!g_h[n]) break; }
    DWORD err = GetLastError();
    RCF(n == (int)(q - g0), "created %d brushes, expected %u", n, q - g0);
    REQ(r2_gur(0), q);
    REQ(r2_gur(2), q);
    t_note("GDI quota failure leaves last error %lu (sentinel 0x7777)", err);   /* not proven, so not a hard check */
    for (int i = 0; i < n; i++) U(DeleteObject)(g_h[i]);
    REQ(r2_gur(0), g0);
    REQ(r2_gur(2), q);                                   /* the peak stays at the quota */
}
R2_SCN(quota_gdi_release_k_allows_k) {
    R2_CHILD_NEED_WIN();
    unsigned q = std_quota("GDIProcessHandleQuota"); if (!q) return;
    DWORD g0 = r2_gur(0); int n = 0;
    for (; n < MAXOBJ; n++) { g_h[n] = mkobj(n); if (!g_h[n]) break; }
    RC(!mkobj(1));                          /* still full */
    for (int i = 0; i < 7; i++) U(DeleteObject)(g_h[i]);
    int again = 0; void *a[16];
    for (; again < 16; again++) { a[again] = mkobj(again); if (!a[again]) break; }
    REQ(again, 7);
    for (int i = 0; i < again; i++) U(DeleteObject)(a[i]);
    for (int i = 7; i < n; i++) U(DeleteObject)(g_h[i]);
    REQ(r2_gur(0), g0);
}
R2_SCN(quota_gdi_full_blocks_dc_and_bitmap_but_not_menu_or_stock) {
    R2_CHILD_NEED_WIN();
    unsigned q = std_quota("GDIProcessHandleQuota"); if (!q) return;
    int n = 0;
    for (; n < MAXOBJ; n++) { g_h[n] = mkobj(n); if (!g_h[n]) break; }
    RC(U(CreateCompatibleDC)(NULL) == NULL);
    RC(U(CreateCompatibleBitmap)(U(GetDC)(NULL), 4, 4) == NULL);
    RC(U(CreatePen)(PS_SOLID, 1, 0) == NULL);
    RC(U(CreateRectRgn)(0, 0, 4, 4) == NULL);
    RC(U(GetStockObject)(BLACK_BRUSH) != NULL);           /* public/stock objects skip the process quota */
    HMENU m = U(CreateMenu)(); RC(m != NULL); if (m) U(DestroyMenu)(m);   /* USER objects use the other counter */
    for (int i = 0; i < n; i++) U(DeleteObject)(g_h[i]);
}
R2_SCN(quota_user_fill_to_limit_exact) {
    R2_CHILD_NEED_WIN();
    unsigned q = std_quota("USERProcessHandleQuota"); if (!q) return;
    DWORD u0 = r2_gur(1); int n = 0;
    SetLastError(0);
    for (; n < MAXOBJ; n++) { g_h[n] = U(CreateMenu)(); if (!g_h[n]) break; }
    DWORD err = GetLastError();
    RCF(n == (int)(q - u0), "created %d menus, expected %u", n, q - u0);
    RCF(err == 1158, "ERROR_NO_MORE_USER_HANDLES expected, got %lu", err);
    REQ(r2_gur(1), q); REQ(r2_gur(4), q);
    for (int i = 0; i < n; i++) U(DestroyMenu)(g_h[i]);
    REQ(r2_gur(1), u0); REQ(r2_gur(4), q);
}
R2_SCN(quota_user_full_blocks_window_not_brush) {
    R2_CHILD_NEED_WIN();
    unsigned q = std_quota("USERProcessHandleQuota"); if (!q) return;
    int n = 0;
    for (; n < MAXOBJ; n++) { g_h[n] = U(CreateMenu)(); if (!g_h[n]) break; }
    SetLastError(0);
    HWND w = r2_window();
    RC(w == NULL);
    RCF(GetLastError() == 1158, "CreateWindow error %lu", GetLastError());
    HBRUSH b = U(CreateSolidBrush)(5); RC(b != NULL); if (b) U(DeleteObject)(b);
    for (int i = 0; i < n; i++) U(DestroyMenu)(g_h[i]);
    w = r2_window(); RC(w != NULL); if (w) U(DestroyWindow)(w);
}
R2_SCN(quota_user_release_k_allows_k) {
    R2_CHILD_NEED_WIN();
    unsigned q = std_quota("USERProcessHandleQuota"); if (!q) return;
    DWORD u0 = r2_gur(1); int n = 0;
    for (; n < MAXOBJ; n++) { g_h[n] = U(CreateMenu)(); if (!g_h[n]) break; }
    for (int i = 0; i < 5; i++) U(DestroyMenu)(g_h[i]);
    int again = 0; void *a[12];
    for (; again < 12; again++) { a[again] = U(CreateMenu)(); if (!a[again]) break; }
    REQ(again, 5);
    for (int i = 0; i < again; i++) U(DestroyMenu)(a[i]);
    for (int i = 5; i < n; i++) U(DestroyMenu)(g_h[i]);
    REQ(r2_gur(1), u0);
}
R2_SCN(quota_gdi_limit_parked) {
    R2_CHILD_NEED_WIN();
    unsigned q = std_quota("GDIProcessHandleQuota");
    r2_publish("q", q);
    int n = 0;
    if (q) for (; n < MAXOBJ; n++) { g_h[n] = mkobj(n); if (!g_h[n]) break; }
    r2_publish("n", n);
    r2_park();
}
TEST(r2_win32k_process_lifecycle_quotas_quota_gdi_limit_seen_through_process_handle) {
    R2_NEED_WIN();
    struct r2_proc p;
    if (!r2_start(&p, "quota_gdi_limit_parked")) { t_skip("cannot start child"); return; }
    if (!r2_wait_ready(&p, 60000)) { CHECK(0); r2_finish(&p, 100); return; }
    ULONGLONG q = 0, n = 0; CHECK(r2_fetch(&p, "q", &q)); CHECK(r2_fetch(&p, "n", &n));
    HANDLE hp = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, p.pid); REQUIRE(hp);
    DWORD cur = U(GetGuiResources)(hp, 0), pk = U(GetGuiResources)(hp, 2);
    if (q) { CHECK(cur == q); CHECK(pk == q); }
    else { CHECK(cur >= n); CHECK(pk >= cur); }
    /* the quota is per process: this process is not limited by the child's usage */
    HBRUSH b = U(CreateSolidBrush)(1); CHECK(b != NULL); if (b) U(DeleteObject)(b);
    r2_go(&p);
    CHECK(r2_finish(&p, 30000) == 0);
    CHECK(U(GetGuiResources)(hp, 0) == 0);
    CloseHandle(hp);
}
