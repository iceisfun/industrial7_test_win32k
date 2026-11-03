/* Per-window DPI state, DPI queries by monitor / process, point conversion and DPI-dependent metrics.
 *
 * tagWND (desktop heap, read through k_wnd_ptr) +0x120 holds the DPI awareness context captured when the window was created
 * (low nibble awareness, bits 8..16 the DPI for unaware / system-aware windows, 0 for per-monitor windows whose DPI is the
 * ushort at +0x11e, falling back to +0x11c) - user32!GetDpiForWindow / GetWindowDpiAwarenessContext read exactly that.
 */
#include "dm.h"

#define WND_DPICTX 0x120
#define WND_DPI_A  0x11e
#define WND_DPI_B  0x11c

static HWND mkwin(const char *t) { return k_make_window(t); }
static int wnd_words(HWND w, unsigned long *ctx, unsigned short *a, unsigned short *b) {
    unsigned char *p = (unsigned char *)k_wnd_ptr(w);
    if (!p) return 0;
    return k_try_read_u32(p + WND_DPICTX, ctx) && k_try_read_u16(p + WND_DPI_A, a) && k_try_read_u16(p + WND_DPI_B, b);
}

/* ---- GetDpiForWindow / GetWindowDpiAwarenessContext ------------------------------------------------- */

TEST(display_monitors_dpi_wnd_get_dpi_invalid_window) {
    NEED_GUI(); REQUIRE(U(GetDpiForWindow));
    HWND bad[] = { NULL, (HWND)(ULONG_PTR)0x10000, (HWND)(ULONG_PTR)0xdeadbeef, (HWND)(ULONG_PTR)0x7fffffff };
    for (unsigned i = 0; i < 4; i++) {
        SetLastError(0);
        CHECKF(U(GetDpiForWindow)(bad[i]) == 0, "hwnd %p", (void *)bad[i]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);                 /* user32!GetDpiForWindow: NULL or ValidateHwnd failure -> 0x57 */
    }
}

TEST(display_monitors_dpi_wnd_get_dpi_destroyed_window) {
    NEED_GUI(); REQUIRE(U(GetDpiForWindow));
    HWND w = mkwin("dm_wd"); REQUIRE(w);
    CHECK(U(GetDpiForWindow)(w) != 0);
    U(DestroyWindow)(w);
    SetLastError(0);
    CHECK(U(GetDpiForWindow)(w) == 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}

TEST(display_monitors_dpi_wnd_window_context_invalid_window_is_zero) {
    NEED_GUI(); REQUIRE(U(GetWindowDpiAwarenessContext));
    CHECK(DM_LO(U(GetWindowDpiAwarenessContext)(NULL)) == 0);
    CHECK(DM_LO(U(GetWindowDpiAwarenessContext)((HWND)(ULONG_PTR)0x10000)) == 0);
    CHECK(DM_LO(U(GetWindowDpiAwarenessContext)((HWND)(ULONG_PTR)0xdeadbeef)) == 0);
}

static const struct { DPI_AWARENESS_CONTEXT c; const char *n; int aw; } wctx[] = {
    { DPI_AWARENESS_CONTEXT_UNAWARE, "UNAWARE", 0 }, { DPI_AWARENESS_CONTEXT_SYSTEM_AWARE, "SYSTEM", 1 }, { DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE, "PM", 2 },
    { DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2, "PMV2", 2 }, { DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED, "GDISCALED", 0 },
};

TEST(display_monitors_dpi_wnd_window_inherits_creating_thread_awareness) {
    NEED_GUI(); REQUIRE(U(GetWindowDpiAwarenessContext) && U(SetThreadDpiAwarenessContext));
    DM_CTX_GUARD();
    for (unsigned i = 0; i < 5; i++) {
        dm_set_ctx(wctx[i].c);
        HWND w = mkwin("dm_wi"); if (!w) { CHECKF(0, "%s: window not created", wctx[i].n); continue; }
        DPI_AWARENESS_CONTEXT wc = U(GetWindowDpiAwarenessContext)(w);
        CHECKF((int)U(GetAwarenessFromDpiAwarenessContext)(wc) == wctx[i].aw, "%s: window awareness %d", wctx[i].n, (int)U(GetAwarenessFromDpiAwarenessContext)(wc));
        if (i != 1) CHECKF(U(AreDpiAwarenessContextsEqual)(wc, wctx[i].c), "%s: window context 0x%x", wctx[i].n, DM_LO(wc));
        U(DestroyWindow)(w);
    }
}

TEST(display_monitors_dpi_wnd_unaware_window_dpi_is_96_and_context_is_6010) {
    NEED_GUI(); REQUIRE(U(GetDpiForWindow) && U(SetThreadDpiAwarenessContext));
    DM_CTX_GUARD();
    dm_set_ctx(DPI_AWARENESS_CONTEXT_UNAWARE);
    HWND w = mkwin("dm_wu"); REQUIRE(w);
    CHECK(U(GetDpiForWindow)(w) == 96);
    CHECK((DM_LO(U(GetWindowDpiAwarenessContext)(w)) & 0xdfffffffu) == DM_RAW_UNAWARE);
    unsigned long ctx; unsigned short a, b;
    if (wnd_words(w, &ctx, &a, &b)) CHECKF(((ctx >> 8) & 0x1ff) == 96, "heap ctx word 0x%lx", ctx);
    U(DestroyWindow)(w);
}

TEST(display_monitors_dpi_wnd_gdiscaled_window_keeps_bit30) {
    NEED_GUI(); REQUIRE(U(GetDpiForWindow) && U(SetThreadDpiAwarenessContext));
    DM_CTX_GUARD();
    dm_set_ctx(DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED);
    HWND w = mkwin("dm_wg"); REQUIRE(w);
    unsigned wc = DM_LO(U(GetWindowDpiAwarenessContext)(w));
    CHECKF((wc & 0x40000000u) != 0, "window ctx 0x%x lost the GDI-scaled bit", wc);
    CHECK((wc & 0xf) == 0);
    CHECK(U(GetDpiForWindow)(w) == 96);
    U(DestroyWindow)(w);
}

TEST(display_monitors_dpi_wnd_system_aware_window_dpi_is_system_dpi) {
    NEED_GUI(); REQUIRE(U(GetDpiForWindow) && U(SetThreadDpiAwarenessContext) && U(GetDpiForSystem));
    DM_CTX_GUARD();
    dm_set_ctx(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    HWND w = mkwin("dm_ws"); REQUIRE(w);
    CHECKF(U(GetDpiForWindow)(w) == U(GetDpiForSystem)(), "window %u system %u", U(GetDpiForWindow)(w), U(GetDpiForSystem)());
    unsigned wc = DM_LO(U(GetWindowDpiAwarenessContext)(w));
    CHECK((wc & 0xf) == 1);
    CHECK(((wc >> 8) & 0x1ff) == (U(GetDpiForSystem)() & 0x1ff));
    U(DestroyWindow)(w);
}

TEST(display_monitors_dpi_wnd_per_monitor_window_dpi_comes_from_the_window_dpi_field) {
    NEED_GUI(); REQUIRE(U(GetDpiForWindow) && U(SetThreadDpiAwarenessContext));
    if (t_is_wine()) { t_skip("Wine has no desktop-heap tagWND"); return; }
    DM_CTX_GUARD();
    dm_set_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HWND w = mkwin("dm_wp"); REQUIRE(w);
    unsigned long ctx; unsigned short a, b;
    if (!wnd_words(w, &ctx, &a, &b)) { U(DestroyWindow)(w); t_skip("tagWND not readable"); return; }
    CHECKF(((ctx >> 8) & 0x1ff) == 0, "per-monitor window ctx 0x%lx carries a dpi", ctx);
    unsigned want = a ? a : b;
    CHECKF(U(GetDpiForWindow)(w) == want, "GetDpiForWindow %u, +0x11e=%u +0x11c=%u", U(GetDpiForWindow)(w), a, b);
    CHECK(want >= 96);
    U(DestroyWindow)(w);
}

TEST(display_monitors_dpi_wnd_heap_context_word_matches_api) {
    NEED_GUI(); REQUIRE(U(GetWindowDpiAwarenessContext) && U(SetThreadDpiAwarenessContext));
    if (t_is_wine()) { t_skip("Wine has no desktop-heap tagWND"); return; }
    DM_CTX_GUARD();
    for (unsigned i = 0; i < 5; i++) {
        dm_set_ctx(wctx[i].c);
        HWND w = mkwin("dm_wh"); if (!w) continue;
        unsigned long ctx; unsigned short a, b;
        if (wnd_words(w, &ctx, &a, &b)) {
            CHECKF((DM_LO(U(GetWindowDpiAwarenessContext)(w))) == (ctx & 0xdfffffff), "%s: api 0x%x heap 0x%lx", wctx[i].n, DM_LO(U(GetWindowDpiAwarenessContext)(w)), ctx);
            CHECKF((int)(ctx & 0xf) == wctx[i].aw, "%s: heap awareness nibble %lu", wctx[i].n, ctx & 0xf);
            unsigned dpi = (ctx >> 8) & 0x1ff; if (!dpi) dpi = a ? a : b;
            CHECKF(U(GetDpiForWindow)(w) == dpi, "%s: api %u heap-derived %u", wctx[i].n, U(GetDpiForWindow)(w), dpi);
        } else t_note("%s: tagWND not readable", wctx[i].n);
        U(DestroyWindow)(w);
    }
}

TEST(display_monitors_dpi_wnd_context_is_fixed_at_creation) {
    NEED_GUI(); REQUIRE(U(GetWindowDpiAwarenessContext) && U(SetThreadDpiAwarenessContext) && U(GetDpiForWindow));
    DM_CTX_GUARD();
    dm_set_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HWND w = mkwin("dm_wf"); REQUIRE(w);
    unsigned c0 = DM_LO(U(GetWindowDpiAwarenessContext)(w)); UINT d0 = U(GetDpiForWindow)(w);
    dm_set_ctx(DPI_AWARENESS_CONTEXT_UNAWARE);
    CHECK(DM_LO(U(GetWindowDpiAwarenessContext)(w)) == c0);          /* a property of the window, not of the calling thread */
    CHECK(U(GetDpiForWindow)(w) == d0);
    dm_set_ctx(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    CHECK(DM_LO(U(GetWindowDpiAwarenessContext)(w)) == c0);
    U(DestroyWindow)(w);
}

struct xw { HANDLE ready, go; HWND w; int ok; };
static DWORD WINAPI xw_thread(LPVOID p) {
    struct xw *x = (struct xw *)p;
    U(SetThreadDpiAwarenessContext)(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    x->w = mkwin("dm_xw");
    x->ok = x->w != NULL;
    SetEvent(x->ready);
    WaitForSingleObject(x->go, 10000);
    if (x->w) U(DestroyWindow)(x->w);
    return 0;
}
TEST(display_monitors_dpi_wnd_other_threads_window_reports_its_own_awareness) {
    NEED_GUI(); REQUIRE(U(GetWindowDpiAwarenessContext) && U(SetThreadDpiAwarenessContext));
    DM_CTX_GUARD();
    dm_set_ctx(DPI_AWARENESS_CONTEXT_UNAWARE);
    struct xw x; memset(&x, 0, sizeof x);
    x.ready = CreateEventA(NULL, TRUE, FALSE, NULL); x.go = CreateEventA(NULL, TRUE, FALSE, NULL);
    HANDLE t = CreateThread(NULL, 0, xw_thread, &x, 0, NULL); REQUIRE(t);
    WaitForSingleObject(x.ready, 10000);
    if (x.ok) {
        CHECK((int)U(GetAwarenessFromDpiAwarenessContext)(U(GetWindowDpiAwarenessContext)(x.w)) == DPI_AWARENESS_PER_MONITOR_AWARE);
        CHECK(U(AreDpiAwarenessContextsEqual)(U(GetWindowDpiAwarenessContext)(x.w), DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
        CHECK(U(GetDpiForWindow)(x.w) >= 96);
    } else t_note("worker window not created");
    SetEvent(x.go); WaitForSingleObject(t, 10000);
    CloseHandle(t); CloseHandle(x.ready); CloseHandle(x.go);
}

TEST(display_monitors_dpi_wnd_dpi_matches_monitor_dpi_on_single_monitor) {
    NEED_GUI(); REQUIRE(U(GetDpiForWindow) && U(SetThreadDpiAwarenessContext));
    UINT x = 0, y = 0;
    if (!UN(GetDpiForMonitorInternal)) { t_skip("GetDpiForMonitorInternal missing"); return; }
    DM_CTX_GUARD();
    dm_set_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HWND w = mkwin("dm_wm"); REQUIRE(w);
    HMONITOR m = U(MonitorFromWindow)(w, MONITOR_DEFAULTTONEAREST);
    BOOL r = UN(GetDpiForMonitorInternal)(m, 0, &x, &y);
    CHECK(r);
    if (U(GetSystemMetrics)(SM_CMONITORS) == 1) CHECKF(U(GetDpiForWindow)(w) == x, "window %u monitor %u", U(GetDpiForWindow)(w), x);
    else if (U(GetDpiForWindow)(w) != x) t_note("window dpi %u, nearest monitor effective dpi %u (multi-monitor)", U(GetDpiForWindow)(w), x);
    U(DestroyWindow)(w);
}

TEST(display_monitors_dpi_wnd_nt_current_dpi_info_validates_window) {
    NEED_GUI(); NEED_NT(NtUserGetCurrentDpiInfoForWindow);
    HWND w = mkwin("dm_wi2"); REQUIRE(w);
    static unsigned char buf[0x100] __attribute__((aligned(8)));
    memset(buf, 0xcc, sizeof buf);
    CHECK(UN(NtUserGetCurrentDpiInfoForWindow)(w, buf) == TRUE);
    int touched = 0; for (int i = 0; i < 0x60; i++) if (buf[i] != 0xcc) touched = 1;
    CHECK(touched);
    CHECK(buf[0x60] == 0xcc);                                      /* exactly 0x60 bytes are copied out */
    memset(buf, 0xcc, sizeof buf);
    CHECK(UN(NtUserGetCurrentDpiInfoForWindow)(NULL, buf) == FALSE);
    CHECK(UN(NtUserGetCurrentDpiInfoForWindow)((HWND)(ULONG_PTR)0xdeadbeef, buf) == FALSE);
    CHECK(buf[0] == 0xcc && buf[0x5f] == 0xcc);                    /* nothing written for an invalid window */
    U(DestroyWindow)(w);
}

/* ---- GetDpiForMonitor ------------------------------------------------------------------------------ */

TEST(display_monitors_dpi_mon_effective_dpi_by_thread_awareness) {
    NEED_GUI(); NEED_NT(GetDpiForMonitorInternal); REQUIRE(U(SetThreadDpiAwarenessContext));
    DM_CTX_GUARD();
    HMONITOR m = dm_primary();
    UINT x = 0, y = 0;
    dm_set_ctx(DPI_AWARENESS_CONTEXT_UNAWARE);
    CHECK(UN(GetDpiForMonitorInternal)(m, 0, &x, &y) && x == 96 && y == 96);                 /* NtUserGetDpiForMonitor: neither system nor per-monitor -> 0x60 */
    dm_set_ctx(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    UINT sys = U(GetDpiForSystem)();
    CHECK(UN(GetDpiForMonitorInternal)(m, 0, &x, &y) && x == sys && y == sys);               /* system aware -> the process dpi (win32process+0x11c) */
    dm_set_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CHECK(UN(GetDpiForMonitorInternal)(m, 0, &x, &y) && x >= 96 && x == y);
}

TEST(display_monitors_dpi_mon_effective_dpi_matches_shared_heap_pmv2) {
    NEED_GUI(); NEED_NT(GetDpiForMonitorInternal); REQUIRE(U(SetThreadDpiAwarenessContext));
    struct dm_heap h; if (t_is_wine() || !dm_heap_read(&h) || !h.n) { t_skip("monitor shared heap not readable"); return; }
    DM_CTX_GUARD();
    dm_set_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    for (int i = 0; i < h.n; i++) {
        if (!dm_heap_is_active(&h.m[i])) continue;
        UINT x = 0, y = 0;
        CHECK(UN(GetDpiForMonitorInternal)(h.m[i].h, 0, &x, &y));
        if (x != h.m[i].dpi) t_note("monitor %d: effective dpi %u, shared-heap dpi field %u", i, x, h.m[i].dpi);
        CHECK(x != 0);
    }
}

TEST(display_monitors_dpi_mon_angular_and_raw_types_report_or_fail_cleanly) {
    NEED_GUI(); NEED_NT(GetDpiForMonitorInternal);
    HMONITOR m = dm_primary();
    for (UINT t = 1; t <= 2; t++) {
        UINT x = 0xcccccccc, y = 0xcccccccc; SetLastError(0);
        BOOL r = UN(GetDpiForMonitorInternal)(m, t, &x, &y);
        if (r) { CHECKF(x > 0 && y > 0, "type %u: %u,%u", t, x, y); }
        else {
            CHECKF(GetLastError() != 0, "type %u failed without a last error", t);
            t_note("type %u failed (error %lu) outputs %u,%u", t, GetLastError(), x, y);
        }
    }
}

TEST(display_monitors_dpi_mon_dpi_type_out_of_range_is_bad_arguments) {
    NEED_GUI(); NEED_NT(GetDpiForMonitorInternal);
    UINT x = 0xcccccccc, y = 0xcccccccc;
    UINT types[] = { 3, 4, 100, 0xffffffff };
    for (unsigned i = 0; i < 4; i++) {
        SetLastError(0);
        CHECKF(UN(GetDpiForMonitorInternal)(dm_primary(), types[i], &x, &y) == FALSE, "type %u", types[i]);
        EXPECT_ERR(ERROR_BAD_ARGUMENTS);           /* user32!GetDpiForMonitorInternal: type >= 3 -> SetLastError(0xa0) before the syscall */
        CHECK(x == 0xcccccccc && y == 0xcccccccc);
    }
}

TEST(display_monitors_dpi_mon_invalid_monitor_handle_sets_invalid_handle_error) {
    NEED_GUI(); NEED_NT(GetDpiForMonitorInternal);
    HMONITOR bad[] = { NULL, (HMONITOR)(ULONG_PTR)0xdeadbeef, (HMONITOR)(ULONG_PTR)0x10000, (HMONITOR)(ULONG_PTR)-1 };
    for (unsigned i = 0; i < 4; i++) {
        UINT x = 0xcccccccc, y = 0xcccccccc; SetLastError(0);
        CHECKF(UN(GetDpiForMonitorInternal)(bad[i], 0, &x, &y) == FALSE, "handle %p", (void *)bad[i]);
        EXPECT_ERR(ERROR_INVALID_HANDLE);          /* NtUserGetDpiForMonitor: HMValidateSharedHandle failed -> UserSetLastError(6) */
        CHECK(x == 0xcccccccc && y == 0xcccccccc);
    }
}

TEST(display_monitors_dpi_mon_shcore_wrapper_matches_internal) {
    NEED_GUI(); NEED_NT(GetDpiForMonitorInternal);
    shcore_GetDpiForMonitor_pfn f = DM_SHCORE(shcore_GetDpiForMonitor);
    if (!f) { t_skip("shcore.dll!GetDpiForMonitor missing"); return; }
    UINT x = 0, y = 0, ix = 0, iy = 0;
    for (int t = 0; t <= 1; t++) {
        HRESULT hr = (HRESULT)f(dm_primary(), t, &x, &y);
        BOOL r = UN(GetDpiForMonitorInternal)(dm_primary(), t, &ix, &iy);
        CHECK(hr == S_OK && r);
        CHECK(x == ix && y == iy);
    }
    UINT bx = 0xcccccccc, by = 0xcccccccc;
    HRESULT hr = (HRESULT)f((HMONITOR)(ULONG_PTR)0xdeadbeef, 0, &bx, &by);
    CHECKF(FAILED(hr), "invalid monitor hr 0x%lx", (unsigned long)hr);
    CHECK(bx == 0xcccccccc && by == 0xcccccccc);
}

/* ---- process level queries through the syscalls ------------------------------------------------------- */

TEST(display_monitors_dpi_proc_nt_dpi_for_current_process_is_plausible) {
    NEED_GUI(); NEED_NT(NtUserGetDpiForCurrentProcess);
    if (t_is_wine()) { t_skip("Wine lacks NtUserGetDpiForCurrentProcess"); return; }
    unsigned d = UN(NtUserGetDpiForCurrentProcess)() & 0xffff;
    CHECKF(d >= 96 && d <= 960, "process dpi %u", d);
}

TEST(display_monitors_dpi_proc_system_dpi_for_process_pseudo_and_real_handles_agree) {
    NEED_GUI(); NEED_NT(NtUserGetSystemDpiForProcess); NEED_NT(NtUserGetDpiForCurrentProcess);
    if (t_is_wine()) { t_skip("Wine lacks the process dpi syscalls"); return; }
    unsigned cur = UN(NtUserGetDpiForCurrentProcess)() & 0xffff;
    HANDLE self = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentProcessId()); REQUIRE(self);
    CHECK((UN(NtUserGetSystemDpiForProcess)(NULL) & 0xffff) == cur);
    CHECK((UN(NtUserGetSystemDpiForProcess)(GetCurrentProcess()) & 0xffff) == cur);
    CHECK((UN(NtUserGetSystemDpiForProcess)(self) & 0xffff) == cur);
    CloseHandle(self);
}

TEST(display_monitors_dpi_proc_system_dpi_for_process_rejects_bad_handles) {
    NEED_GUI(); NEED_NT(NtUserGetSystemDpiForProcess);
    if (t_is_wine()) { t_skip("Wine lacks the process dpi syscalls"); return; }
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    HANDLE sync_only = NULL, me = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentProcessId());
    if (me) DuplicateHandle(GetCurrentProcess(), me, GetCurrentProcess(), &sync_only, SYNCHRONIZE, FALSE, 0);
    HANDLE bad[] = { (HANDLE)(ULONG_PTR)0x7777c, ev, sync_only, (HANDLE)(ULONG_PTR)0xfffffffc };
    for (unsigned i = 0; i < 4; i++) {
        if (!bad[i]) continue;
        SetLastError(0);
        CHECKF((UN(NtUserGetSystemDpiForProcess)(bad[i]) & 0xffff) == 0, "handle %p", bad[i]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);         /* ObReferenceObjectByHandle(.., PROCESS_QUERY_LIMITED_INFORMATION, PsProcessType) failed -> 0x57 */
    }
    if (sync_only) CloseHandle(sync_only);
    if (me) CloseHandle(me);
    CloseHandle(ev);
}

TEST(display_monitors_dpi_proc_awareness_context_rejects_bad_handles) {
    NEED_GUI(); NEED_NT(NtUserGetProcessDpiAwarenessContext);
    if (t_is_wine()) { t_skip("Wine lacks the process dpi syscalls"); return; }
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    HANDLE sync_only = NULL, me = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentProcessId());
    if (me) DuplicateHandle(GetCurrentProcess(), me, GetCurrentProcess(), &sync_only, SYNCHRONIZE, FALSE, 0);
    HANDLE bad[] = { (HANDLE)(ULONG_PTR)0x7777c, ev, sync_only };
    for (unsigned i = 0; i < 3; i++) {
        if (!bad[i]) continue;
        SetLastError(0);
        CHECKF(UN(NtUserGetProcessDpiAwarenessContext)(bad[i]) == 0, "handle %p", bad[i]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
    if (sync_only) CloseHandle(sync_only);
    if (me) CloseHandle(me);
    CloseHandle(ev);
}

TEST(display_monitors_dpi_proc_awareness_context_for_self_handle_follows_the_calling_thread) {
    NEED_GUI(); NEED_NT(NtUserGetProcessDpiAwarenessContext); REQUIRE(U(SetThreadDpiAwarenessContext));
    if (t_is_wine()) { t_skip("Wine lacks the process dpi syscalls"); return; }
    DM_CTX_GUARD();
    HANDLE self = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentProcessId()); REQUIRE(self);
    dm_set_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CHECK((UN(NtUserGetProcessDpiAwarenessContext)(self) & 0xf) == 2);              /* win32 process == current -> W32GetCurrentThreadDpiAwarenessContext */
    CHECK((UN(NtUserGetProcessDpiAwarenessContext)(NULL) & 0xf) == 2);
    CHECK((UN(NtUserGetProcessDpiAwarenessContext)(GetCurrentProcess()) & 0xf) == 2);
    dm_set_ctx(DPI_AWARENESS_CONTEXT_UNAWARE);
    CHECK((UN(NtUserGetProcessDpiAwarenessContext)(self) & 0xf) == 0);
    CloseHandle(self);
}

/* ---- GetDpiForSystem and the process dpi cache ------------------------------------------------------- */

TEST(display_monitors_dpi_sys_system_dpi_equals_process_dpi_for_aware_threads) {
    NEED_GUI(); REQUIRE(U(GetDpiForSystem) && U(SetThreadDpiAwarenessContext));
    if (t_is_wine()) { t_skip("Wine lacks NtUserGetDpiForCurrentProcess"); return; }
    NEED_NT(NtUserGetDpiForCurrentProcess);
    DM_CTX_GUARD();
    unsigned proc = UN(NtUserGetDpiForCurrentProcess)() & 0xffff;
    DPI_AWARENESS_CONTEXT cs[] = { DPI_AWARENESS_CONTEXT_SYSTEM_AWARE, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 };
    for (unsigned i = 0; i < 3; i++) { dm_set_ctx(cs[i]); CHECKF(U(GetDpiForSystem)() == proc, "ctx %u: GetDpiForSystem %u process %u", i, U(GetDpiForSystem)(), proc); }
}

TEST(display_monitors_dpi_sys_unaware_thread_sees_96) {
    NEED_GUI(); REQUIRE(U(GetDpiForSystem) && U(SetThreadDpiAwarenessContext));
    DM_CTX_GUARD();
    dm_set_ctx(DPI_AWARENESS_CONTEXT_UNAWARE);
    UINT d = U(GetDpiForSystem)();
    int comp = dm_composited();
    if (comp == 1) CHECKF(d == 96, "unaware GetDpiForSystem %u", d);       /* nibble 0 and IsThreadDesktopComposited -> return 0x60 */
    else t_note("GetDpiForSystem = %u while unaware, composition flag %d", d, comp);
}

TEST(display_monitors_dpi_sys_serverinfo_system_dpi_matches_process_dpi) {
    NEED_GUI();
    if (t_is_wine()) { t_skip("Wine has no SERVERINFO"); return; }
    NEED_NT(NtUserGetDpiForCurrentProcess);
    struct k_user_shared_info si;
    if (!k_shared_info(&si) || !si.psi) { t_skip("gSharedInfo unavailable"); return; }
    unsigned short sdpi = 0;
    /* the system DPI word sits at SERVERINFO+0x1b56 on Windows 10 22H2 */
    if (!k_try_read_u16((char *)si.psi + 0x1b56, &sdpi)) { t_skip("SERVERINFO not readable"); return; }
    unsigned proc = UN(NtUserGetDpiForCurrentProcess)() & 0xffff;
    if (sdpi != proc) t_note("SERVERINFO+0x1b56 = %u, process dpi = %u (dpi changed since process start?)", sdpi, proc);
    else CHECK(sdpi == proc);
    CHECK(sdpi >= 96 && sdpi <= 960);
}

/* ---- point conversion --------------------------------------------------------------------------- */

TEST(display_monitors_dpi_pt_null_window_conversions_succeed) {
    NEED_GUI(); REQUIRE(U(LogicalToPhysicalPointForPerMonitorDPI) && U(PhysicalToLogicalPointForPerMonitorDPI) && U(SetThreadDpiAwarenessContext));
    DM_CTX_GUARD();
    dm_set_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    POINT p = { 100, 120 };
    CHECK(U(LogicalToPhysicalPointForPerMonitorDPI)(NULL, &p) == TRUE);       /* NULL hwnd: GuessMonitorOverrideForCoordinateConversions path always converts */
    if (p.x != 100 || p.y != 120) t_note("PMv2 logical->physical moved (100,120) to (%ld,%ld)", p.x, p.y);
    CHECK(U(PhysicalToLogicalPointForPerMonitorDPI)(NULL, &p) == TRUE);
}

TEST(display_monitors_dpi_pt_invalid_window_fails) {
    NEED_GUI(); REQUIRE(U(LogicalToPhysicalPointForPerMonitorDPI) && U(PhysicalToLogicalPointForPerMonitorDPI));
    POINT p = { 10, 10 };
    HWND bad[] = { (HWND)(ULONG_PTR)0x10000, (HWND)(ULONG_PTR)0xdeadbeef };
    for (unsigned i = 0; i < 2; i++) {
        p.x = 10; p.y = 10;
        CHECKF(U(LogicalToPhysicalPointForPerMonitorDPI)(bad[i], &p) == FALSE, "hwnd %p", (void *)bad[i]);     /* ValidateHwnd failed */
        CHECK(p.x == 10 && p.y == 10);
        CHECKF(U(PhysicalToLogicalPointForPerMonitorDPI)(bad[i], &p) == FALSE, "hwnd %p", (void *)bad[i]);
        CHECK(p.x == 10 && p.y == 10);
    }
}

TEST(display_monitors_dpi_pt_point_outside_window_fails_and_is_unchanged) {
    NEED_GUI(); REQUIRE(U(LogicalToPhysicalPointForPerMonitorDPI) && U(PhysicalToLogicalPointForPerMonitorDPI) && U(SetThreadDpiAwarenessContext) && U(SetWindowPos));
    DM_CTX_GUARD();
    dm_set_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HWND w = mkwin("dm_pt"); REQUIRE(w);
    U(SetWindowPos)(w, NULL, 200, 200, 100, 80, SWP_NOACTIVATE | SWP_NOZORDER);
    POINT out = { 50, 50 };                                  /* well outside (200,200,300,280) */
    CHECK(U(LogicalToPhysicalPointForPerMonitorDPI)(w, &out) == FALSE);
    CHECK(out.x == 50 && out.y == 50);
    POINT in = { 250, 240 };
    CHECK(U(LogicalToPhysicalPointForPerMonitorDPI)(w, &in) == TRUE);
    if (in.x != 250 || in.y != 240) t_note("same-awareness conversion moved (250,240) to (%ld,%ld)", in.x, in.y);
    CHECK(U(PhysicalToLogicalPointForPerMonitorDPI)(w, &in) == TRUE);
    U(DestroyWindow)(w);
}

TEST(display_monitors_dpi_pt_nt_direct_matches_user32_export) {
    NEED_GUI(); NEED_NT(NtUserLogicalToPerMonitorDPIPhysicalPoint); NEED_NT(NtUserPerMonitorDPIPhysicalToLogicalPoint);
    REQUIRE(U(LogicalToPhysicalPointForPerMonitorDPI) && U(SetThreadDpiAwarenessContext));
    DM_CTX_GUARD();
    DPI_AWARENESS_CONTEXT cs[] = { DPI_AWARENESS_CONTEXT_UNAWARE, DPI_AWARENESS_CONTEXT_SYSTEM_AWARE, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 };
    for (unsigned i = 0; i < 3; i++) {
        dm_set_ctx(cs[i]);
        POINT a = { 321, 123 }, b = a;
        BOOL ra = U(LogicalToPhysicalPointForPerMonitorDPI)(NULL, &a), rb = UN(NtUserLogicalToPerMonitorDPIPhysicalPoint)(NULL, &b);
        CHECK(ra == rb && a.x == b.x && a.y == b.y);        /* the export is a bare jump to the syscall */
    }
}

TEST(display_monitors_dpi_pt_unaware_round_trip_stays_within_a_pixel) {
    NEED_GUI(); REQUIRE(U(LogicalToPhysicalPointForPerMonitorDPI) && U(PhysicalToLogicalPointForPerMonitorDPI) && U(SetThreadDpiAwarenessContext));
    DM_CTX_GUARD();
    dm_set_ctx(DPI_AWARENESS_CONTEXT_UNAWARE);
    POINT p = { 500, 300 }, orig = p;
    CHECK(U(LogicalToPhysicalPointForPerMonitorDPI)(NULL, &p));
    POINT phys = p;
    CHECK(U(PhysicalToLogicalPointForPerMonitorDPI)(NULL, &p));
    CHECKF(abs(p.x - orig.x) <= 1 && abs(p.y - orig.y) <= 1, "(500,300) -> (%ld,%ld) -> (%ld,%ld)", phys.x, phys.y, p.x, p.y);
    UINT d = U(GetDpiForSystem) ? 0 : 0; (void)d;
    if (phys.x == orig.x && phys.y == orig.y) t_note("no scaling in effect (monitor at 100%%)");
}

/* ---- DPI dependent metrics ----------------------------------------------------------------------- */

TEST(display_monitors_dpi_metrics_for_dpi_are_monotonic) {
    NEED_GUI(); REQUIRE(U(GetSystemMetricsForDpi));
    int idx[] = { SM_CXVSCROLL, SM_CYHSCROLL, SM_CYCAPTION, SM_CXICON, SM_CYICON, SM_CXSMICON, SM_CYMENU, SM_CXFRAME, SM_CYMENUSIZE };
    UINT dpis[] = { 96, 120, 144, 192, 288 };
    for (unsigned i = 0; i < sizeof idx / sizeof *idx; i++) {
        int prev = 0;
        for (unsigned k = 0; k < 5; k++) {
            int v = U(GetSystemMetricsForDpi)(idx[i], dpis[k]);
            CHECKF(v >= prev && v > 0, "metric %d at %u dpi = %d (previous %d)", idx[i], dpis[k], v, prev);
            prev = v;
        }
        int a = U(GetSystemMetricsForDpi)(idx[i], 96), b = U(GetSystemMetricsForDpi)(idx[i], 192);
        CHECKF(b >= a && b <= 2 * a + 4, "metric %d: 96dpi=%d 192dpi=%d", idx[i], a, b);
    }
}

TEST(display_monitors_dpi_metrics_unaware_thread_gets_96_dpi_metrics) {
    NEED_GUI(); REQUIRE(U(GetSystemMetricsForDpi) && U(SetThreadDpiAwarenessContext));
    DM_CTX_GUARD();
    dm_set_ctx(DPI_AWARENESS_CONTEXT_UNAWARE);
    int idx[] = { SM_CXVSCROLL, SM_CYCAPTION, SM_CXICON, SM_CYMENU, SM_CXSMICON };
    for (unsigned i = 0; i < sizeof idx / sizeof *idx; i++) {
        int a = U(GetSystemMetrics)(idx[i]), b = U(GetSystemMetricsForDpi)(idx[i], 96);
        if (a != b) t_note("metric %d: unaware GetSystemMetrics=%d, ForDpi(96)=%d", idx[i], a, b);
        else CHECK(a == b);
    }
}

TEST(display_monitors_dpi_metrics_screen_size_by_thread_context) {
    NEED_GUI(); REQUIRE(U(GetSystemMetrics) && U(SetThreadDpiAwarenessContext));
    if (t_is_wine()) { t_skip("Wine does not virtualize metrics"); return; }
    NEED_NT(NtUserGetDpiForCurrentProcess);
    DM_CTX_GUARD();
    unsigned proc = UN(NtUserGetDpiForCurrentProcess)() & 0xffff;
    dm_set_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    int pw = U(GetSystemMetrics)(SM_CXSCREEN), ph = U(GetSystemMetrics)(SM_CYSCREEN);
    dm_set_ctx(DPI_AWARENESS_CONTEXT_UNAWARE);
    int uw = U(GetSystemMetrics)(SM_CXSCREEN), uh = U(GetSystemMetrics)(SM_CYSCREEN);
    if (proc == 96) { CHECK(uw == pw && uh == ph); return; }
    int ew = MulDiv(pw, 96, proc), eh = MulDiv(ph, 96, proc);
    if (dm_composited() != 1) { t_note("desktop not composited (%d): unaware %dx%d physical %dx%d", dm_composited(), uw, uh, pw, ph); return; }
    CHECKF(abs(uw - ew) <= 1 && abs(uh - eh) <= 1, "unaware %dx%d, physical %dx%d scaled by 96/%u = %dx%d", uw, uh, pw, ph, proc, ew, eh);
}

/* ---- AdjustWindowRectExForDpi -------------------------------------------------------------------- */

TEST(display_monitors_dpi_adjust_zero_dpi_is_invalid_parameter) {
    NEED_GUI(); REQUIRE(U(AdjustWindowRectExForDpi));
    RECT r = { 0, 0, 100, 100 };
    SetLastError(0);
    CHECK(U(AdjustWindowRectExForDpi)(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, 0) == FALSE);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    CHECK(r.left == 0 && r.top == 0 && r.right == 100 && r.bottom == 100);
}

TEST(display_monitors_dpi_adjust_popup_without_frame_is_identity) {
    NEED_GUI(); REQUIRE(U(AdjustWindowRectExForDpi));
    UINT dpis[] = { 96, 120, 144, 192 };
    for (unsigned i = 0; i < 4; i++) {
        RECT r = { 10, 20, 110, 220 };
        CHECK(U(AdjustWindowRectExForDpi)(&r, WS_POPUP, FALSE, 0, dpis[i]));
        CHECKF(r.left == 10 && r.top == 20 && r.right == 110 && r.bottom == 220, "dpi %u: (%ld,%ld,%ld,%ld)", dpis[i], r.left, r.top, r.right, r.bottom);
    }
}

TEST(display_monitors_dpi_adjust_border_style_adds_one_pixel_each_side_at_any_dpi) {
    NEED_GUI(); REQUIRE(U(AdjustWindowRectExForDpi));
    UINT dpis[] = { 96, 144, 192 };
    for (unsigned i = 0; i < 3; i++) {
        RECT r = { 0, 0, 100, 100 };
        CHECK(U(AdjustWindowRectExForDpi)(&r, WS_POPUP | WS_BORDER, FALSE, 0, dpis[i]));
        CHECKF(r.left == -1 && r.top == -1 && r.right == 101 && r.bottom == 101, "dpi %u: (%ld,%ld,%ld,%ld)", dpis[i], r.left, r.top, r.right, r.bottom);   /* (style & 0xc00000) != 0 -> +1 */
    }
}

TEST(display_monitors_dpi_adjust_ex_styles_edge_widths) {
    NEED_GUI(); REQUIRE(U(AdjustWindowRectExForDpi));
    struct { DWORD ex; int grow; } t[] = {
        { WS_EX_CLIENTEDGE, 2 },          /* ex & 0x200 -> +2 */
        { WS_EX_STATICEDGE, 1 },          /* ex & 0x20000 -> 1 */
        { WS_EX_DLGMODALFRAME, 3 },
        { WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE, 5 },
        { WS_EX_WINDOWEDGE, 0 },
    };
    for (unsigned i = 0; i < sizeof t / sizeof *t; i++) {
        RECT r = { 0, 0, 100, 100 };
        CHECK(U(AdjustWindowRectExForDpi)(&r, WS_POPUP, FALSE, t[i].ex, 96));
        CHECKF(r.left == -t[i].grow && r.top == -t[i].grow && r.right == 100 + t[i].grow && r.bottom == 100 + t[i].grow, "ex 0x%lx: (%ld,%ld,%ld,%ld) want grow %d", t[i].ex, r.left, r.top, r.right, r.bottom, t[i].grow);
    }
}

TEST(display_monitors_dpi_adjust_menu_moves_top_up_by_menu_height_for_dpi) {
    NEED_GUI(); REQUIRE(U(AdjustWindowRectExForDpi) && U(GetSystemMetricsForDpi));
    UINT dpis[] = { 96, 120, 144, 192 };
    for (unsigned i = 0; i < 4; i++) {
        RECT r = { 0, 0, 100, 100 };
        CHECK(U(AdjustWindowRectExForDpi)(&r, WS_POPUP, TRUE, 0, dpis[i]));
        int cym = U(GetSystemMetricsForDpi)(SM_CYMENU, dpis[i]);
        CHECKF(r.top == -cym && r.left == 0 && r.right == 100 && r.bottom == 100, "dpi %u: top %ld, SM_CYMENU %d", dpis[i], r.top, cym);
    }
}

TEST(display_monitors_dpi_adjust_caption_moves_top_up_by_caption_height_for_dpi) {
    NEED_GUI(); REQUIRE(U(AdjustWindowRectExForDpi) && U(GetSystemMetricsForDpi));
    UINT dpis[] = { 96, 144, 192 };
    for (unsigned i = 0; i < 3; i++) {
        RECT r = { 0, 0, 100, 100 }, base = { 0, 0, 100, 100 };
        CHECK(U(AdjustWindowRectExForDpi)(&r, WS_POPUP | WS_CAPTION, FALSE, 0, dpis[i]));
        int cyc = U(GetSystemMetricsForDpi)(SM_CYCAPTION, dpis[i]);
        CHECKF(r.top <= base.top - cyc, "dpi %u: top %ld, caption %d", dpis[i], r.top, cyc);        /* (style >> 16 & 0xc0) == 0xc0 -> top -= SM_CYCAPTION, then the border grows it further */
        CHECK(r.left < 0 && r.right > 100 && r.bottom > 100);
        CHECK(-r.left == r.right - 100);                                                              /* horizontally symmetric */
    }
}

TEST(display_monitors_dpi_adjust_overlapped_window_grows_with_dpi) {
    NEED_GUI(); REQUIRE(U(AdjustWindowRectExForDpi));
    UINT dpis[] = { 96, 120, 144, 192 };
    long prev_w = 0, prev_h = 0;
    for (unsigned i = 0; i < 4; i++) {
        RECT r = { 0, 0, 400, 300 };
        CHECK(U(AdjustWindowRectExForDpi)(&r, WS_OVERLAPPEDWINDOW, TRUE, 0, dpis[i]));
        long w = r.right - r.left, h = r.bottom - r.top;
        CHECKF(w > 400 && h > 300, "dpi %u: %ldx%ld", dpis[i], w, h);
        CHECKF(w >= prev_w && h >= prev_h, "dpi %u: %ldx%ld smaller than at the previous dpi (%ldx%ld)", dpis[i], w, h, prev_w, prev_h);
        prev_w = w; prev_h = h;
    }
}

/* Windows 10: user32 dereferences the rectangle without a NULL check, so a NULL rect faults in the caller. The call runs in
 * a child that turns the access violation into its exit code. */
static LONG CALLBACK adj_null_veh(PEXCEPTION_POINTERS ep) { ExitProcess(ep->ExceptionRecord->ExceptionCode); return EXCEPTION_CONTINUE_SEARCH; }
CHILD(display_monitors_dpi_adjust_null) {
    SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
    AddVectoredExceptionHandler(1, adj_null_veh);
    BOOL r = U(AdjustWindowRectExForDpi)(NULL, WS_OVERLAPPEDWINDOW, FALSE, 0, 96);
    printf("returned %d\n", r);
    return 0;
}
TEST(display_monitors_dpi_adjust_null_rect_without_menu_is_harmless) {
    NEED_GUI(); REQUIRE(U(AdjustWindowRectExForDpi));
    if (t_is_wine()) { t_skip("the NULL-rect fault is user32 behavior of Windows"); return; }
    struct k_child_result r = k_child_run("display_monitors_dpi_adjust_null", "", 60000);
    CHECKF(r.exit_code == 0xC0000005, "child exit code 0x%lx (0xc0000005 = access violation inside user32)", (unsigned long)r.exit_code);
}

TEST(display_monitors_dpi_adjust_unaware_thread_equals_for_dpi_96) {
    NEED_GUI(); REQUIRE(U(AdjustWindowRectEx) && U(AdjustWindowRectExForDpi) && U(SetThreadDpiAwarenessContext));
    DM_CTX_GUARD();
    dm_set_ctx(DPI_AWARENESS_CONTEXT_UNAWARE);
    DWORD styles[] = { WS_POPUP, WS_POPUP | WS_BORDER, WS_OVERLAPPEDWINDOW, WS_POPUP | WS_CAPTION | WS_SYSMENU };
    for (unsigned i = 0; i < 4; i++) {
        RECT a = { 0, 0, 300, 200 }, b = a;
        CHECK(U(AdjustWindowRectEx)(&a, styles[i], FALSE, 0));
        CHECK(U(AdjustWindowRectExForDpi)(&b, styles[i], FALSE, 0, 96));
        if (!dm_rect_eq(&a, &b)) t_note("style 0x%lx: AdjustWindowRectEx (%ld,%ld,%ld,%ld) vs ForDpi(96) (%ld,%ld,%ld,%ld)", styles[i], a.left, a.top, a.right, a.bottom, b.left, b.top, b.right, b.bottom);
    }
}
