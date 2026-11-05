/* Adapter enumeration/open/close through NtGdiDdDDIEnumAdapters(2), OpenAdapterFromHdc/Luid/DeviceName and CloseAdapter.
 * The dxgkrnl-side result codes are only hard-checked when the outcome is dictated by the documented protocol
 * (probe with a NULL array, STATUS_BUFFER_TOO_SMALL, invalid handle => STATUS_INVALID_PARAMETER) or by a
 * analysed gdi32 wrapper; everything else is recorded with t_note. A machine without any WDDM adapter
 * (no display adapter in the session) skips the tests that need one.
 */
#include "d.h"
#include <stdlib.h>

static int probe_count(void) {
    dk_fn1 f = dk_nt("NtGdiDdDDIEnumAdapters2");
    if (!f) return -1;
    dk_enum2 e = { 0, NULL };
    LONG st = f(&e);
    return NTOK(st) ? (int)e.NumAdapters : -1;
}

/* The sizing call (NULL array) reports an upper bound for the array capacity (32 on Windows 10), not the number of
 * adapters present; the real number comes back from the filling call. */
static int actual_count(void) {
    dk_adapterinfo *a; int n = dk_enum(&a);
    if (a) dk_enum_close(a, n);
    return n;
}

TEST(gdi_d3dkmt_dd_enum2_probe_count_is_upper_bound) {
    DKREQ(f, "NtGdiDdDDIEnumAdapters2");
    int bound = probe_count(), n = actual_count();
    REQUIRE(bound > 0);
    CHECKF(n >= 0 && n <= bound, "adapters %d, sizing bound %d", n, bound);
    t_note("sizing bound %d, adapters present %d", bound, n);
}
TEST(gdi_d3dkmt_dd_enum2_probe_count_succeeds) {
    DKREQ(f, "NtGdiDdDDIEnumAdapters2");
    dk_enum2 e = { 0xCCCCCCCC, NULL };
    LONG st = f(&e);
    /* pAdapters == NULL is the sizing call: success with the adapter count in NumAdapters */
    CHECKF(NTOK(st), "status 0x%lx", (unsigned long)st);
    CHECKF(e.NumAdapters != 0xCCCCCCCC, "NumAdapters not written");
    t_note("EnumAdapters2 count = %lu", (unsigned long)e.NumAdapters);
    CHECK(e.pAdapters == NULL);
}
TEST(gdi_d3dkmt_dd_enum2_probe_repeatable) {
    int a = probe_count();
    REQUIRE(a >= 0);
    for (int i = 0; i < 10; i++) CHECKF(probe_count() == a, "count changed on pass %d", i);
}
TEST(gdi_d3dkmt_dd_enum2_probe_opens_no_handle_in_gdi_tables) {
    DKREQ(f, "NtGdiDdDDIEnumAdapters2");
    REQUIRE(U(GetGuiResources));
    unsigned g = dk_gui_count(GR_GDIOBJECTS), u = dk_gui_count(GR_USEROBJECTS);
    dk_enum2 e = { 0, NULL };
    f(&e);
    CHECK(dk_gui_count(GR_GDIOBJECTS) == g);
    CHECK(dk_gui_count(GR_USEROBJECTS) == u);
}
TEST(gdi_d3dkmt_dd_enum2_probe_with_large_numadapters_input) {
    DKREQ(f, "NtGdiDdDDIEnumAdapters2");
    int base = probe_count();
    REQUIRE(base >= 0);
    dk_enum2 e = { 1000, NULL };
    LONG st = f(&e);
    NOTE_ST("EnumAdapters2(NULL array, NumAdapters=1000)", st);
    if (NTOK(st)) t_note("NumAdapters after = %lu (probe count %d)", (unsigned long)e.NumAdapters, base);
}
TEST(gdi_d3dkmt_dd_enum2_fill_full_buffer) {
    DKREQ(f, "NtGdiDdDDIEnumAdapters2");
    int n = probe_count();
    REQUIRE(n > 0);
    dk_adapterinfo *a = calloc(n + 1, sizeof *a);
    dk_enum2 e = { (ULONG)n, a };
    LONG st = f(&e);
    CHECKF(NTOK(st), "status 0x%lx", (unsigned long)st);
    if (NTOK(st)) {
        CHECKF(e.NumAdapters <= (ULONG)n && e.NumAdapters > 0, "NumAdapters %lu", (unsigned long)e.NumAdapters);
        for (ULONG i = 0; i < e.NumAdapters; i++) {
            CHECKF(a[i].hAdapter != 0, "adapter %lu handle 0", (unsigned long)i);
            t_info("adapter[%lu] h=0x%lx luid=%lx:%lx sources=%lu", (unsigned long)i, (unsigned long)a[i].hAdapter, (unsigned long)a[i].luid.HighPart, (unsigned long)a[i].luid.LowPart, (unsigned long)a[i].NumOfSources);
        }
        for (ULONG i = 0; i < e.NumAdapters; i++) dk_close_ok(a[i].hAdapter);
    }
    free(a);
}
TEST(gdi_d3dkmt_dd_enum2_fill_handles_distinct) {
    dk_adapterinfo *a; int n = dk_enum(&a);
    if (n < 2) { if (a) dk_enum_close(a, n); t_skip("fewer than two adapters"); return; }
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) CHECKF(a[i].hAdapter != a[j].hAdapter, "adapters %d and %d share handle", i, j);
    dk_enum_close(a, n);
}
TEST(gdi_d3dkmt_dd_enum2_fill_luids_distinct) {
    dk_adapterinfo *a; int n = dk_enum(&a);
    if (n < 2) { if (a) dk_enum_close(a, n); t_skip("fewer than two adapters"); return; }
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) CHECKF(!dk_luid_eq(a[i].luid, a[j].luid), "adapters %d and %d share a LUID", i, j);
    dk_enum_close(a, n);
}
TEST(gdi_d3dkmt_dd_enum2_fill_handles_are_not_gdi_objects) {
    REQUIRE(U(GetObjectType));
    dk_adapterinfo *a; int n = dk_enum(&a);
    if (n <= 0) { if (a) dk_enum_close(a, n); t_skip("no adapters"); return; }
    for (int i = 0; i < n; i++) CHECKF(U(GetObjectType)((HGDIOBJ)(ULONG_PTR)a[i].hAdapter) == 0, "D3DKMT handle 0x%lx is a GDI object", (unsigned long)a[i].hAdapter);
    dk_enum_close(a, n);
}
TEST(gdi_d3dkmt_dd_enum2_fill_leaves_gdi_counts_alone) {
    REQUIRE(U(GetGuiResources));
    unsigned g = dk_gui_count(GR_GDIOBJECTS), u = dk_gui_count(GR_USEROBJECTS);
    dk_adapterinfo *a; int n = dk_enum(&a);
    if (n <= 0) { if (a) dk_enum_close(a, n); t_skip("no adapters"); return; }
    CHECK(dk_gui_count(GR_GDIOBJECTS) == g);
    CHECK(dk_gui_count(GR_USEROBJECTS) == u);
    dk_enum_close(a, n);
}
TEST(gdi_d3dkmt_dd_enum2_buffer_too_small) {
    DKREQ(f, "NtGdiDdDDIEnumAdapters2");
    int n = actual_count();
    REQUIRE(n > 0);
    dk_adapterinfo *a = calloc(n + 1, sizeof *a);
    dk_enum2 e = { (ULONG)(n - 1), a };
    LONG st = f(&e);
    CHECKF(st == ST_BUFFER_TOO_SMALL, "status 0x%lx (%s)", (unsigned long)st, dk_stn(st));
    t_note("NumAdapters after too-small call = %lu (adapters present %d)", (unsigned long)e.NumAdapters, n);
    if (NTOK(st)) for (ULONG i = 0; i < e.NumAdapters && i < (ULONG)n + 1; i++) if (a[i].hAdapter) dk_close(a[i].hAdapter);
    free(a);
}
TEST(gdi_d3dkmt_dd_enum2_too_small_does_not_report_required_count) {
    DKREQ(f, "NtGdiDdDDIEnumAdapters2");
    int n = actual_count();
    REQUIRE(n > 0);
    dk_adapterinfo *a = calloc(n + 2, sizeof *a);
    /* A zero capacity with a real array fails with STATUS_BUFFER_TOO_SMALL and leaves NumAdapters alone: the caller
     * learns the count only from the NULL-array sizing call (an upper bound) or from a call that succeeds. */
    dk_enum2 e = { 0, a };
    LONG st = f(&e);
    CHECKF(st == ST_BUFFER_TOO_SMALL, "status 0x%lx (%s)", (unsigned long)st, dk_stn(st));
    CHECKF(e.NumAdapters == 0, "NumAdapters became %lu", (unsigned long)e.NumAdapters);
    if (NTOK(st)) for (int i = 0; i < n; i++) if (a[i].hAdapter) dk_close(a[i].hAdapter);
    free(a);
}
TEST(gdi_d3dkmt_dd_enum2_too_small_leaves_array_untouched) {
    DKREQ(f, "NtGdiDdDDIEnumAdapters2");
    int n = actual_count();
    REQUIRE(n > 0);
    dk_adapterinfo *a = calloc(n + 1, sizeof *a);
    memset(a, 0x5A, (n + 1) * sizeof *a);
    dk_enum2 e = { (ULONG)(n - 1), a };
    LONG st = f(&e);
    if (st != ST_BUFFER_TOO_SMALL) { t_note("status 0x%lx; skipping untouched check", (unsigned long)st); if (NTOK(st)) for (int i = 0; i < n; i++) if (a[i].hAdapter && a[i].hAdapter != 0x5A5A5A5A) dk_close(a[i].hAdapter); free(a); return; }
    int touched = 0;
    for (int i = 0; i <= n; i++) { unsigned char *p = (unsigned char *)&a[i]; for (size_t k = 0; k < sizeof *a; k++) if (p[k] != 0x5A) touched = 1; }
    /* a buffer-size failure must not hand out adapter handles (they would leak until process exit) */
    CHECK(!touched);
    free(a);
}
TEST(gdi_d3dkmt_dd_enum2_oversized_buffer) {
    DKREQ(f, "NtGdiDdDDIEnumAdapters2");
    int n = actual_count();
    REQUIRE(n > 0);
    dk_adapterinfo *a = calloc(n + 5, sizeof *a);
    dk_enum2 e = { (ULONG)(n + 4), a };
    LONG st = f(&e);
    CHECKF(NTOK(st), "status 0x%lx", (unsigned long)st);
    if (NTOK(st)) {
        CHECKF(e.NumAdapters == (ULONG)n, "NumAdapters %lu, adapters present %d", (unsigned long)e.NumAdapters, n);
        for (ULONG i = 0; i < e.NumAdapters; i++) dk_close_ok(a[i].hAdapter);
    }
    free(a);
}
TEST(gdi_d3dkmt_dd_enum2_twice_both_sets_closable) {
    dk_adapterinfo *a, *b; int n = dk_enum(&a), m = dk_enum(&b);
    if (n <= 0 || m <= 0) { if (a) dk_enum_close(a, n); if (b) dk_enum_close(b, m); t_skip("no adapters"); return; }
    CHECK(n == m);
    for (int i = 0; i < n && i < m; i++) t_info("adapter[%d] first=0x%lx second=0x%lx", i, (unsigned long)a[i].hAdapter, (unsigned long)b[i].hAdapter);
    for (int i = 0; i < n; i++) dk_close_ok(a[i].hAdapter);
    for (int i = 0; i < m; i++) dk_close_ok(b[i].hAdapter);
    free(a); free(b);
}
TEST(gdi_d3dkmt_dd_enum2_has_a_source_bearing_adapter) {
    dk_adapterinfo *a; int n = dk_enum(&a);
    if (n <= 0) { if (a) dk_enum_close(a, n); t_skip("no adapters"); return; }
    int srcs = 0;
    for (int i = 0; i < n; i++) srcs += a[i].NumOfSources > 0;
    if (!srcs) t_note("no adapter reports VidPn sources (headless session?)");
    dk_enum_close(a, n);
}
TEST(gdi_d3dkmt_dd_enum_v1_matches_v2_count) {
    DKREQ(f1, "NtGdiDdDDIEnumAdapters");
    int n2 = actual_count();
    REQUIRE(n2 >= 0);
    /* D3DKMT_ENUMADAPTERS = { UINT NumAdapters; D3DKMT_ADAPTERINFO Adapters[16]; } */
    unsigned char buf[4 + 20 * 16 + 64];
    memset(buf, 0, sizeof buf);
    LONG st = f1(buf);
    NOTE_ST("EnumAdapters (v1)", st);
    if (!NTOK(st)) return;
    ULONG n1 = *(ULONG *)buf;
    dk_adapterinfo *ad = (dk_adapterinfo *)(buf + 4);
    if (n2 <= 16) CHECKF(n1 == (ULONG)n2, "v1 %lu v2 %d", (unsigned long)n1, n2);
    for (ULONG i = 0; i < n1 && i < 16; i++) { CHECK(ad[i].hAdapter != 0); if (ad[i].hAdapter) dk_close_ok(ad[i].hAdapter); }
}

/* ---- OpenAdapterFromHdc -------------------------------------------------------------------------- */
TEST(gdi_d3dkmt_dd_openhdc_screen_dc_roundtrip) {
    dk_openhdc o; HDC dc;
    if (!dk_open_screen(&o, &dc)) { t_skip("no display adapter"); return; }
    CHECK(o.hAdapter != 0);
    CHECK(o.hDc == dc);
    t_info("screen adapter h=0x%lx luid=%lx:%lx source=%lu", (unsigned long)o.hAdapter, (unsigned long)o.luid.HighPart, (unsigned long)o.luid.LowPart, (unsigned long)o.VidPnSourceId);
    dk_close_ok(o.hAdapter);
    U(ReleaseDC)(NULL, dc);
}
TEST(gdi_d3dkmt_dd_openhdc_handle_is_not_dc_or_gdi_object) {
    dk_openhdc o; HDC dc;
    if (!dk_open_screen(&o, &dc)) { t_skip("no display adapter"); return; }
    REQUIRE(U(GetObjectType));
    CHECK((ULONG_PTR)o.hAdapter != (ULONG_PTR)dc);
    CHECK(U(GetObjectType)((HGDIOBJ)(ULONG_PTR)o.hAdapter) == 0);
    CHECK(U(GetObjectType)(dc) == OBJ_DC);
    dk_close_ok(o.hAdapter);
    U(ReleaseDC)(NULL, dc);
}
TEST(gdi_d3dkmt_dd_openhdc_dc_cell_unlocked_after_call) {
    dk_openhdc o; HDC dc;
    if (!dk_open_screen(&o, &dc)) { t_skip("no display adapter"); return; }
    struct k_gdicell c;
    if (k_gdi_cell(dc, &c)) {
        /* the DC was locked (XDCOBJ) for the duration of the syscall and released on return: owner word has bit0 clear
         * and still names this process (see gdi_handle_table cell layout: owner = pid with bit0 = lock) */
        unsigned owner = (unsigned)(c.field8 & 0xffffffffu);
        CHECKF((owner & 1) == 0, "owner word 0x%x still locked", owner);
        CHECKF((owner & ~1u) == (GetCurrentProcessId() & ~1u), "owner 0x%x vs pid %lu", owner, (unsigned long)GetCurrentProcessId());
    } else t_note("GDI shared table not readable");
    dk_close_ok(o.hAdapter);
    U(ReleaseDC)(NULL, dc);
}
TEST(gdi_d3dkmt_dd_openhdc_leaves_gdi_counts_alone) {
    REQUIRE(U(GetGuiResources));
    HDC dc = U(GetDC)(NULL);
    REQUIRE(dc);
    unsigned g = dk_gui_count(GR_GDIOBJECTS), u = dk_gui_count(GR_USEROBJECTS);
    dk_fn1 f = dk_nt("NtGdiDdDDIOpenAdapterFromHdc");
    if (!f) { U(ReleaseDC)(NULL, dc); t_skip("missing export"); return; }
    dk_openhdc o = { dc };
    LONG st = f(&o);
    CHECK(dk_gui_count(GR_GDIOBJECTS) == g);
    CHECK(dk_gui_count(GR_USEROBJECTS) == u);
    if (NTOK(st) && o.hAdapter) dk_close_ok(o.hAdapter);
    U(ReleaseDC)(NULL, dc);
}
TEST(gdi_d3dkmt_dd_openhdc_luid_listed_by_enum) {
    dk_openhdc o; HDC dc;
    if (!dk_open_screen(&o, &dc)) { t_skip("no display adapter"); return; }
    dk_adapterinfo *a; int n = dk_enum(&a);
    int found = 0;
    for (int i = 0; i < n; i++) found |= dk_luid_eq(a[i].luid, o.luid);
    if (n > 0) CHECKF(found, "LUID %lx:%lx of the screen DC's adapter missing from EnumAdapters2", (unsigned long)o.luid.HighPart, (unsigned long)o.luid.LowPart);
    if (a) dk_enum_close(a, n);
    dk_close_ok(o.hAdapter);
    U(ReleaseDC)(NULL, dc);
}
TEST(gdi_d3dkmt_dd_openhdc_twice_both_closable) {
    dk_openhdc a, b; HDC d1, d2;
    if (!dk_open_screen(&a, &d1)) { t_skip("no display adapter"); return; }
    if (!dk_open_screen(&b, &d2)) { dk_close_ok(a.hAdapter); U(ReleaseDC)(NULL, d1); t_skip("second open failed"); return; }
    CHECK(dk_luid_eq(a.luid, b.luid));
    t_note("two opens: 0x%lx and 0x%lx", (unsigned long)a.hAdapter, (unsigned long)b.hAdapter);
    dk_close_ok(a.hAdapter);
    if (b.hAdapter != a.hAdapter) dk_close_ok(b.hAdapter);
    U(ReleaseDC)(NULL, d1); U(ReleaseDC)(NULL, d2);
}
TEST(gdi_d3dkmt_dd_openhdc_double_close_fails) {
    dk_openhdc o; HDC dc;
    if (!dk_open_screen(&o, &dc)) { t_skip("no display adapter"); return; }
    dk_close_ok(o.hAdapter);
    LONG st = dk_close(o.hAdapter);
    CHECKF(!NTOK(st), "second close succeeded");
    NOTE_ST("second CloseAdapter", st);
    U(ReleaseDC)(NULL, dc);
}
TEST(gdi_d3dkmt_dd_openhdc_double_close_status_invalid_parameter) {
    dk_openhdc o; HDC dc;
    if (!dk_open_screen(&o, &dc)) { t_skip("no display adapter"); return; }
    dk_close_ok(o.hAdapter);
    CHECKF(dk_close(o.hAdapter) == ST_INVALID_PARAMETER, "closed handle must read as an invalid handle parameter");
    U(ReleaseDC)(NULL, dc);
}
TEST(gdi_d3dkmt_dd_openhdc_memory_dc_opens_primary_adapter) {
    DKREQ(f, "NtGdiDdDDIOpenAdapterFromHdc");
    REQUIRE(U(CreateCompatibleDC)); REQUIRE(U(DeleteDC)); REQUIRE(U(GetObjectType));
    dk_openhdc os; HDC scr;
    if (!dk_open_screen(&os, &scr)) { t_skip("no display adapter"); return; }
    HDC mem = U(CreateCompatibleDC)(NULL);
    REQUIRE(mem);
    dk_openhdc o = { mem, 0, { 0, 0 }, 0 };
    LONG st = f(&o);
    /* Windows accepts a memory DC and opens the adapter that drives the primary display: same LUID as the screen DC. */
    CHECKF(NTOK(st), "memory DC rejected, status 0x%lx", (unsigned long)st);
    NOTE_ST("OpenAdapterFromHdc(memory DC)", st);
    if (NTOK(st)) {
        CHECK(o.hAdapter != 0);
        CHECK(dk_luid_eq(o.luid, os.luid));
        if (o.hAdapter) dk_close_ok(o.hAdapter);
    }
    CHECK(U(GetObjectType)(mem) == OBJ_MEMDC);   /* the open must not release or lock the DC */
    CHECK(U(DeleteDC)(mem));
    dk_close_ok(os.hAdapter);
    U(ReleaseDC)(NULL, scr);
}
TEST(gdi_d3dkmt_dd_openhdc_null_and_bogus_dc_fail) {
    DKREQ(f, "NtGdiDdDDIOpenAdapterFromHdc");
    static const ULONG_PTR v[] = { 0, 1, 0x1234, 0xFFFFFFFF, 0x00010001, 0xDEADBEEF };
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        dk_openhdc o = { (HDC)v[i], 0, { 0, 0 }, 0 };
        LONG st = f(&o);
        CHECKF(!NTOK(st), "hdc %p accepted", (void *)v[i]);
        CHECKF(o.hAdapter == 0, "hdc %p produced adapter handle 0x%lx", (void *)v[i], (unsigned long)o.hAdapter);
        if (NTOK(st) && o.hAdapter) dk_close(o.hAdapter);
    }
}
TEST(gdi_d3dkmt_dd_openhdc_wrong_object_type_fails) {
    DKREQ(f, "NtGdiDdDDIOpenAdapterFromHdc");
    REQUIRE(U(CreateSolidBrush)); REQUIRE(U(CreateBitmap)); REQUIRE(U(DeleteObject));
    HGDIOBJ obj[2] = { U(CreateSolidBrush)(RGB(1, 2, 3)), U(CreateBitmap)(2, 2, 1, 1, NULL) };
    for (int i = 0; i < 2; i++) {
        if (!obj[i]) continue;
        dk_openhdc o = { (HDC)obj[i], 0, { 0, 0 }, 0 };
        LONG st = f(&o);
        CHECKF(!NTOK(st), "GDI object type %d accepted as DC", i);
        if (NTOK(st) && o.hAdapter) dk_close(o.hAdapter);
        U(DeleteObject)(obj[i]);
    }
}
TEST(gdi_d3dkmt_dd_openhdc_stale_dc_fails) {
    DKREQ(f, "NtGdiDdDDIOpenAdapterFromHdc");
    REQUIRE(U(CreateCompatibleDC)); REQUIRE(U(DeleteDC));
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    U(DeleteDC)(dc);
    dk_openhdc o = { dc, 0, { 0, 0 }, 0 };
    LONG st = f(&o);
    CHECK(!NTOK(st));
    CHECK(o.hAdapter == 0);
}
TEST(gdi_d3dkmt_dd_openhdc_gdi32_wrapper_preserves_hdc_on_success) {
    dk_openhdc probe; HDC dc;
    if (!dk_open_screen(&probe, &dc)) { t_skip("no display adapter"); return; }
    dk_close_ok(probe.hAdapter);
    typedef LONG (WINAPI *wr_fn)(void *);
    wr_fn w = (wr_fn)t_proc_in("gdi32.dll", "D3DKMTOpenAdapterFromHdc");
    if (!w || t_is_wine()) { U(ReleaseDC)(NULL, dc); t_skip("wrapper unavailable"); return; }
    dk_openhdc o = { dc, 0, { 0, 0 }, 0 };
    LONG st = w(&o);
 /* gdi32!D3DKMTOpenAdapterFromHdc saves *argument 1 and writes it back after the syscall */
    CHECK(o.hDc == dc);
    if (NTOK(st) && o.hAdapter) dk_close_ok(o.hAdapter);
    U(ReleaseDC)(NULL, dc);
}
TEST(gdi_d3dkmt_dd_openhdc_gdi32_wrapper_preserves_hdc_on_failure) {
    typedef LONG (WINAPI *wr_fn)(void *);
    wr_fn w = (wr_fn)t_proc_in("gdi32.dll", "D3DKMTOpenAdapterFromHdc");
    REQUIRE(w);
    if (t_is_wine()) { t_skip("Wine"); return; }
    dk_openhdc o = { (HDC)(ULONG_PTR)0x4242, 0, { 0, 0 }, 0 };
    LONG st = w(&o);
    CHECK(!NTOK(st));
    CHECK(o.hDc == (HDC)(ULONG_PTR)0x4242);
    CHECK(o.hAdapter == 0);
}
TEST(gdi_d3dkmt_dd_openhdc_createdc_display_same_luid) {
    dk_openhdc s; HDC sdc;
    if (!dk_open_screen(&s, &sdc)) { t_skip("no display adapter"); return; }
    REQUIRE(U(CreateDCW)); REQUIRE(U(DeleteDC));
    HDC d = U(CreateDCW)(L"DISPLAY", NULL, NULL, NULL);
    if (!d) { dk_close_ok(s.hAdapter); U(ReleaseDC)(NULL, sdc); t_skip("CreateDC(DISPLAY) failed"); return; }
    dk_fn1 f = dk_nt("NtGdiDdDDIOpenAdapterFromHdc");
    dk_openhdc o = { d };
    LONG st = f(&o);
    NOTE_ST("OpenAdapterFromHdc(CreateDC DISPLAY)", st);
    if (NTOK(st)) { CHECK(dk_luid_eq(o.luid, s.luid)); dk_close_ok(o.hAdapter); }
    U(DeleteDC)(d);
    dk_close_ok(s.hAdapter);
    U(ReleaseDC)(NULL, sdc);
}
TEST(gdi_d3dkmt_dd_openhdc_many_open_close) {
    DKREQ(f, "NtGdiDdDDIOpenAdapterFromHdc");
    REQUIRE(U(GetDC));
    HDC dc = U(GetDC)(NULL);
    REQUIRE(dc);
    enum { N = 48 };
    ULONG h[N]; int got = 0;
    for (int i = 0; i < N; i++) {
        dk_openhdc o = { dc };
        if (!NTOK(f(&o)) || !o.hAdapter) break;
        h[got++] = o.hAdapter;
    }
    if (got == 0) { U(ReleaseDC)(NULL, dc); t_skip("no display adapter"); return; }
    CHECKF(got == N, "only %d of %d opens succeeded", got, N);
    for (int i = got - 1; i >= 0; i--) dk_close_ok(h[i]);
    U(ReleaseDC)(NULL, dc);
}
TEST(gdi_d3dkmt_dd_openhdc_nt_handle_count_roundtrip) {
    /* Opening/closing an adapter is a dxgkrnl-handle-table operation; record whether it moves the NT handle count. */
    dk_openhdc o; HDC dc;
    DWORD c0 = 0, c1 = 0, c2 = 0;
    GetProcessHandleCount(GetCurrentProcess(), &c0);
    if (!dk_open_screen(&o, &dc)) { t_skip("no display adapter"); return; }
    GetProcessHandleCount(GetCurrentProcess(), &c1);
    dk_close_ok(o.hAdapter);
    U(ReleaseDC)(NULL, dc);
    GetProcessHandleCount(GetCurrentProcess(), &c2);
    t_info("NT handle count before=%lu open=%lu closed=%lu", (unsigned long)c0, (unsigned long)c1, (unsigned long)c2);
}

/* ---- CloseAdapter validation ---------------------------------------------------------------------- */
TEST(gdi_d3dkmt_dd_close_zero_handle_invalid_parameter) {
    DKREQ(f, "NtGdiDdDDICloseAdapter");
    dk_closeargs c = { 0 };
    CHECKF(f(&c) == ST_INVALID_PARAMETER, "got 0x%lx", (unsigned long)f(&c));
}
TEST(gdi_d3dkmt_dd_close_bogus_handles_invalid_parameter) {
    DKREQ(f, "NtGdiDdDDICloseAdapter");
    static const ULONG v[] = { 0xDEADBEEF, 0xFFFFFFFF, 0x12345678, 0x7FFFFFF0, 0x80000000 };
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        dk_closeargs c = { v[i] };
        LONG st = f(&c);
        CHECKF(st == ST_INVALID_PARAMETER, "handle 0x%lx -> 0x%lx (%s)", (unsigned long)v[i], (unsigned long)st, dk_stn(st));
    }
}
TEST(gdi_d3dkmt_dd_close_gdi32_export_same_status) {
    typedef LONG (WINAPI *wr_fn)(void *);
    DKREQ(f, "NtGdiDdDDICloseAdapter");
    wr_fn w = (wr_fn)t_proc_in("gdi32.dll", "D3DKMTCloseAdapter");
    REQUIRE(w);
    dk_closeargs c = { 0xDEADBEEF };
    CHECK(w(&c) == f(&c));
    c.hAdapter = 0;
    CHECK(w(&c) == f(&c));
}
TEST(gdi_d3dkmt_dd_close_does_not_modify_argument) {
    DKREQ(f, "NtGdiDdDDICloseAdapter");
    dk_closeargs c = { 0xDEADBEEF };
    f(&c);
    CHECK(c.hAdapter == 0xDEADBEEF);
}
TEST(gdi_d3dkmt_dd_close_other_adapters_handle_unaffected) {
    dk_openhdc a, b; HDC d1, d2;
    if (!dk_open_screen(&a, &d1)) { t_skip("no display adapter"); return; }
    if (!dk_open_screen(&b, &d2)) { dk_close_ok(a.hAdapter); U(ReleaseDC)(NULL, d1); t_skip("second open failed"); return; }
    if (a.hAdapter == b.hAdapter) { dk_close_ok(a.hAdapter); U(ReleaseDC)(NULL, d1); U(ReleaseDC)(NULL, d2); t_skip("opens share a handle"); return; }
    dk_close_ok(a.hAdapter);
    /* b is still a live handle: a query through it still passes handle validation */
    ULONG type = 0xFFFFFFFF; dk_qai q = { b.hAdapter, QAI_ADAPTERTYPE, &type, 4 };
    dk_fn1 qf = dk_nt("NtGdiDdDDIQueryAdapterInfo");
    if (qf) { LONG st = qf(&q); NOTE_ST("QueryAdapterInfo via the surviving handle", st); if (NTOK(st)) CHECK(type != 0xFFFFFFFF); }
    dk_close_ok(b.hAdapter);
    U(ReleaseDC)(NULL, d1); U(ReleaseDC)(NULL, d2);
}
static DWORD WINAPI close_thread(void *p) { return (DWORD)dk_close(*(ULONG *)p); }
TEST(gdi_d3dkmt_dd_close_from_other_thread) {
    dk_openhdc o; HDC dc;
    if (!dk_open_screen(&o, &dc)) { t_skip("no display adapter"); return; }
    ULONG h = o.hAdapter;
    HANDLE t = CreateThread(NULL, 0, close_thread, &h, 0, NULL);
    REQUIRE(t);
    CHECK(WaitForSingleObject(t, 5000) == WAIT_OBJECT_0);
    DWORD st = 0xFFFFFFFF; GetExitCodeThread(t, &st); CloseHandle(t);
    /* adapter handles belong to the process, not the opening thread */
    CHECKF(st == 0, "close on another thread: 0x%lx", (unsigned long)st);
    U(ReleaseDC)(NULL, dc);
}
static DWORD WINAPI open_thread(void *p) {
    dk_openhdc o; HDC dc;
    if (!dk_open_screen(&o, &dc)) return 0;
    *(ULONG *)p = o.hAdapter;
    U(ReleaseDC)(NULL, dc);
    return 1;
}
TEST(gdi_d3dkmt_dd_open_on_thread_close_on_main_after_thread_exit) {
    ULONG h = 0;
    HANDLE t = CreateThread(NULL, 0, open_thread, &h, 0, NULL);
    REQUIRE(t);
    WaitForSingleObject(t, 5000);
    DWORD ok = 0; GetExitCodeThread(t, &ok); CloseHandle(t);
    if (!ok) { t_skip("no display adapter"); return; }
    /* the opening thread is gone (its W32THREAD torn down), the process-level adapter handle survives */
    dk_close_ok(h);
}

/* ---- OpenAdapterFromLuid ------------------------------------------------------------------------- */
TEST(gdi_d3dkmt_dd_openluid_roundtrip) {
    DKREQ(f, "NtGdiDdDDIOpenAdapterFromLuid");
    dk_adapterinfo *a; int n = dk_enum(&a);
    if (n <= 0) { if (a) dk_enum_close(a, n); t_skip("no adapters"); return; }
    for (int i = 0; i < n; i++) {
        dk_openluid o = { a[i].luid, 0 };
        LONG st = f(&o);
        CHECKF(NTOK(st) && o.hAdapter, "OpenAdapterFromLuid(%lx:%lx) = 0x%lx", (unsigned long)a[i].luid.HighPart, (unsigned long)a[i].luid.LowPart, (unsigned long)st);
        if (NTOK(st) && o.hAdapter) dk_close_ok(o.hAdapter);
    }
    dk_enum_close(a, n);
}
TEST(gdi_d3dkmt_dd_openluid_unknown_luid_fails) {
    DKREQ(f, "NtGdiDdDDIOpenAdapterFromLuid");
    static const LUID v[] = { { 0, 0 }, { 0xDEADBEEF, 0x1234 }, { 0xFFFFFFFF, 0x7FFFFFFF } };
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        dk_openluid o = { v[i], 0 };
        LONG st = f(&o);
        CHECKF(!NTOK(st), "LUID %lx:%lx accepted", (unsigned long)v[i].HighPart, (unsigned long)v[i].LowPart);
        CHECK(o.hAdapter == 0);
        NOTE_ST("OpenAdapterFromLuid(bogus)", st);
        if (NTOK(st) && o.hAdapter) dk_close(o.hAdapter);
    }
}
TEST(gdi_d3dkmt_dd_openluid_matches_hdc_luid) {
    DKREQ(f, "NtGdiDdDDIOpenAdapterFromLuid");
    dk_openhdc h; HDC dc;
    if (!dk_open_screen(&h, &dc)) { t_skip("no display adapter"); return; }
    dk_openluid o = { h.luid, 0 };
    LONG st = f(&o);
    CHECK(NTOK(st) && o.hAdapter);
    t_note("hdc-opened 0x%lx luid-opened 0x%lx", (unsigned long)h.hAdapter, (unsigned long)o.hAdapter);
    if (NTOK(st) && o.hAdapter && o.hAdapter != h.hAdapter) dk_close_ok(o.hAdapter);
    dk_close_ok(h.hAdapter);
    U(ReleaseDC)(NULL, dc);
}
TEST(gdi_d3dkmt_dd_openluid_luid_adapter_queryable) {
    DKREQ(f, "NtGdiDdDDIOpenAdapterFromLuid");
    DKREQ(qf, "NtGdiDdDDIQueryAdapterInfo");
    dk_adapterinfo *a; int n = dk_enum(&a);
    if (n <= 0) { if (a) dk_enum_close(a, n); t_skip("no adapters"); return; }
    dk_openluid o = { a[0].luid, 0 };
    if (NTOK(f(&o)) && o.hAdapter) {
        ULONG type = 0xFFFFFFFF; dk_qai q = { o.hAdapter, QAI_ADAPTERTYPE, &type, 4 };
        LONG st = qf(&q);
        NOTE_ST("QueryAdapterInfo(ADAPTERTYPE) on luid-opened handle", st);
        if (NTOK(st)) CHECK(type != 0xFFFFFFFF);
        dk_close_ok(o.hAdapter);
    }
    dk_enum_close(a, n);
}

/* ---- OpenAdapterFromDeviceName / GdiDisplayName ---------------------------------------------------- */
typedef struct { const WCHAR *pDeviceName; ULONG hAdapter; LUID luid; } dk_opendevname;
TEST(gdi_d3dkmt_dd_opendevicename_bogus_names_fail) {
    DKREQ(f, "NtGdiDdDDIOpenAdapterFromDeviceName");
    static const WCHAR *const v[] = { L"", L"\\\\?\\win32kt-no-such-device", L"DISPLAY", L"\\\\.\\DISPLAY1", L"x" };
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        dk_opendevname o = { v[i], 0, { 0, 0 } };
        LONG st = f(&o);
        /* the name must be a device interface path of a graphics adapter; display names and junk are rejected */
        CHECKF(!NTOK(st), "name %u accepted", (unsigned)i);
        CHECK(o.hAdapter == 0);
        if (NTOK(st) && o.hAdapter) dk_close(o.hAdapter);
    }
}
TEST(gdi_d3dkmt_dd_opengdidisplayname_bogus_name_unsuccessful) {
    typedef LONG (WINAPI *wr_fn)(void *);
    wr_fn w = (wr_fn)t_proc_in("gdi32.dll", "D3DKMTOpenAdapterFromGdiDisplayName");
    REQUIRE(w);
    if (t_is_wine()) { t_skip("Wine"); return; }
    dk_opengdiname o; memset(&o, 0, sizeof o);
    wcscpy(o.Name, L"\\\\.\\WIN32KTNOSUCHDISPLAY");
    /* gdi32 does CreateDCW(name,name,NULL,NULL); failure maps to STATUS_UNSUCCESSFUL without calling the kernel */
    LONG st = w(&o);
    CHECKF(st == ST_UNSUCCESSFUL, "status 0x%lx", (unsigned long)st);
    CHECK(o.hAdapter == 0);
}
TEST(gdi_d3dkmt_dd_opengdidisplayname_empty_name) {
    typedef LONG (WINAPI *wr_fn)(void *);
    wr_fn w = (wr_fn)t_proc_in("gdi32.dll", "D3DKMTOpenAdapterFromGdiDisplayName");
    REQUIRE(w);
    if (t_is_wine()) { t_skip("Wine"); return; }
    dk_opengdiname o; memset(&o, 0, sizeof o);
    LONG st = w(&o);
    CHECK(!NTOK(st));
    CHECK(o.hAdapter == 0);
    NOTE_ST("OpenAdapterFromGdiDisplayName(empty)", st);
}
TEST(gdi_d3dkmt_dd_opengdidisplayname_primary_display_roundtrip) {
    typedef LONG (WINAPI *wr_fn)(void *);
    wr_fn w = (wr_fn)t_proc_in("gdi32.dll", "D3DKMTOpenAdapterFromGdiDisplayName");
    REQUIRE(w); REQUIRE(U(EnumDisplayDevicesW));
    if (t_is_wine()) { t_skip("Wine"); return; }
    DISPLAY_DEVICEW dd; memset(&dd, 0, sizeof dd); dd.cb = sizeof dd;
    int found = 0;
    for (DWORD i = 0; U(EnumDisplayDevicesW)(NULL, i, &dd, 0); i++) if (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) { found = 1; break; }
    if (!found) { t_skip("no primary display device"); return; }
    REQUIRE(U(GetGuiResources));
    unsigned g0 = dk_gui_count(GR_GDIOBJECTS);
    dk_opengdiname o; memset(&o, 0, sizeof o);
    wcsncpy(o.Name, dd.DeviceName, 31);
    LONG st = w(&o);
    NOTE_ST("OpenAdapterFromGdiDisplayName(primary)", st);
    /* the wrapper's temporary DC is always deleted again */
    CHECK(dk_gui_count(GR_GDIOBJECTS) == g0);
    if (NTOK(st)) {
        CHECK(o.hAdapter != 0);
        dk_adapterinfo *a; int n = dk_enum(&a);
        int in = 0; for (int i = 0; i < n; i++) in |= dk_luid_eq(a[i].luid, o.luid);
        if (n > 0) CHECK(in);
        if (a) dk_enum_close(a, n);
        t_note("primary %ls: source %lu", dd.DeviceName, (unsigned long)o.VidPnSourceId);
        dk_close_ok(o.hAdapter);
    }
}
TEST(gdi_d3dkmt_dd_opengdidisplayname_failure_keeps_gdi_count) {
    typedef LONG (WINAPI *wr_fn)(void *);
    wr_fn w = (wr_fn)t_proc_in("gdi32.dll", "D3DKMTOpenAdapterFromGdiDisplayName");
    REQUIRE(w); REQUIRE(U(GetGuiResources));
    if (t_is_wine()) { t_skip("Wine"); return; }
    unsigned g0 = dk_gui_count(GR_GDIOBJECTS);
    for (int i = 0; i < 10; i++) { dk_opengdiname o; memset(&o, 0, sizeof o); wcscpy(o.Name, L"\\\\.\\WIN32KTNOSUCH"); w(&o); }
    CHECK(dk_gui_count(GR_GDIOBJECTS) == g0);
}

/* ---- pristine process: the first win32k syscall happens to be a D3DKMT one ----------------------- */
/* In a process that never loaded user32 the first win32k syscall converts the thread to a GUI thread, which needs the
 * user32 callback table; Windows 10 then terminates the process with STATUS_FATAL_USER_CALLBACK_EXCEPTION. */
CHILD(d3dkmt_pristine) {
    (void)argc; (void)argv;
    int fail = 0;
#define CC(c) do { if (!(c)) { printf("  FAIL child %s:%d %s\n", __FILE__, __LINE__, #c); fail++; } } while (0)
    void *ti = (void *)1;
    CC(k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &ti));
    CC(ti == NULL);                                         /* no win32k syscall yet */
    CC(GetModuleHandleA("user32.dll") == NULL);
    CC(GetModuleHandleA("gdi32.dll") == NULL);
    if (fail) return 100 + fail;                            /* precondition not met: not a statement about Windows */
    dk_fn1 f = dk_nt("NtGdiDdDDICloseAdapter");
    if (!f) { printf("  NOTE child: no export\n"); return 99; }
    dk_closeargs c = { 0xDEADBEEF };
    LONG st = f(&c);
    printf("  NOTE child: syscall returned 0x%lx, the process was expected to be terminated\n", (unsigned long)st);
    return 0;
}
TEST(gdi_d3dkmt_dd_pristine_first_syscall_without_user32_is_fatal) {
    if (t_is_wine()) { t_skip("Wine has no GUI-thread conversion"); return; }
    struct k_child_result r = k_child_run("d3dkmt_pristine", "", 30000);
    CHECKF(r.exit_code == 0xC000041D, "child exit 0x%lx", (unsigned long)r.exit_code);
}
/* With user32 loaded the same syscall just returns the status. A new thread is converted by its first win32k syscall. */
static DWORD WINAPI pristine_thread(LPVOID arg) {
    void *before = (void *)1, *after = NULL;
    k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &before);
    dk_fn1 f = dk_nt("NtGdiDdDDICloseAdapter");
    dk_closeargs c = { 0xDEADBEEF };
    LONG st = f ? f(&c) : 0x7FFFFFFF;
    k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &after);
    printf("  NOTE child thread: Win32ThreadInfo before=%p after=%p status 0x%lx\n", before, after, (unsigned long)st);
    *(LONG *)arg = st;
    return (before == NULL && after != NULL) ? 0 : 1;   /* converted by this very syscall */
}
CHILD(d3dkmt_loaded) {
    (void)argc; (void)argv;
    int fail = 0;
    HMODULE u = LoadLibraryA("user32.dll");
    CC(u != NULL);
    LONG st = 0; DWORD tid;
    HANDLE th = CreateThread(NULL, 0, pristine_thread, &st, 0, &tid);
    CC(th != NULL);
    if (th) { WaitForSingleObject(th, 20000); DWORD rc = 99; GetExitCodeThread(th, &rc); CC(rc == 0); CloseHandle(th); }
    CC(st == ST_INVALID_PARAMETER);
    return fail;
}
TEST(gdi_d3dkmt_dd_first_syscall_on_new_thread_converts_it) {
    if (t_is_wine()) { t_skip("Wine has no GUI-thread conversion"); return; }
    struct k_child_result r = k_child_run("d3dkmt_loaded", "", 30000);
    CHECKF(r.exit_code == 0, "child exit 0x%lx", (unsigned long)r.exit_code);
}
/* A child that opens an adapter and exits without closing it: process teardown must reclaim it without disturbing us. */
CHILD(d3dkmt_leak) {
    (void)argc; (void)argv;
    dk_openhdc o; HDC dc;
    if (!dk_open_screen(&o, &dc)) { printf("  NOTE child: no adapter\n"); return 0; }
    printf("  NOTE child leaked adapter handle 0x%lx\n", (unsigned long)o.hAdapter);
    return 0;
}
TEST(gdi_d3dkmt_dd_child_exit_without_close_is_clean) {
    if (t_is_wine()) { t_skip("Wine: a GUI child spawned after win32u syscalls in the parent can hang headless"); return; }
    int before = actual_count();
    REQUIRE(before >= 0);
    struct k_child_result r = k_child_run("d3dkmt_leak", "", 30000);
    CHECKF(r.exit_code == 0, "child exit 0x%lx", (unsigned long)r.exit_code);
    CHECK(actual_count() == before);
    dk_adapterinfo *a; int n = dk_enum(&a);
    if (a) dk_enum_close(a, n);
    CHECK(n == before);
}
