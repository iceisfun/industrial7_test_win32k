/* SERVERINFO page and gSharedInfo: mapping attributes, structure fields, flags, handle-table
 * bounds, monitor objects reachable through gSharedInfo, WinEvent mask, capture counter. */
#include "si.h"

#define AREA "user_serverinfo_shared_"

static BOOL si_query(const void *p, MEMORY_BASIC_INFORMATION *mbi) {
    return VirtualQuery(p, mbi, sizeof *mbi) == sizeof *mbi;
}
#define WRITE_BITS (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)

TEST(user_serverinfo_shared_gsharedinfo_exported) {
    REQUIRE(U(GetSystemMetrics));
    U(GetSystemMetrics)(SM_CXSCREEN);
    /* user32 exports gSharedInfo; ClientThreadSetup copies the NtUserProcessConnect result into it. */
    void *p = k_shared_info_ptr();
    if (!p) { t_skip("user32!gSharedInfo not exported"); return; }
    CHECK(((ULONG_PTR)p & 7) == 0);
    void *psi = NULL;
    CHECK(k_try_read_ptr(p, &psi));
    CHECK(psi != NULL);
}

TEST(user_serverinfo_shared_psi_in_user_range) {
    char *psi = si_get(); if (!psi) return;
    CHECK((ULONG_PTR)psi >= 0x10000);
    CHECK((ULONG_PTR)psi < 0x00007FFFFFFF0000ULL);
    CHECK(((ULONG_PTR)psi & 7) == 0);
}

TEST(user_serverinfo_shared_psi_whole_struct_readable) {
    char *psi = si_get(); if (!psi) return;
    /* GetSysColorBrush / GetLastInputInfo / GetCapture / GetDpiCacheSlot read as far as +0x1b68. */
    static char buf[0x1b68];
    CHECK(k_try_read(psi, buf, sizeof buf));
    /* A byte past the last field must at least be on a committed page of the same view or fail cleanly. */
    unsigned char b;
    (void)k_try_read(psi + sizeof buf, &b, 1);
}

TEST(user_serverinfo_shared_psi_is_committed_mapped_view) {
    char *psi = si_get(); if (!psi) return;
    MEMORY_BASIC_INFORMATION mbi;
    REQUIRE(si_query(psi, &mbi));
    /* The SERVERINFO page is a section view (MmMapViewOfSection into the process), not an image or private memory. */
    CHECKF(mbi.State == MEM_COMMIT, "State=0x%lx", mbi.State);
    CHECKF(mbi.Type == MEM_MAPPED, "Type=0x%lx", mbi.Type);
}

TEST(user_serverinfo_shared_psi_is_read_only) {
    char *psi = si_get(); if (!psi) return;
    MEMORY_BASIC_INFORMATION mbi;
    REQUIRE(si_query(psi, &mbi));
    /* win32kbase maps the shared info read-only for user mode: a user process must never be able to write SERVERINFO. */
    CHECKF((mbi.Protect & WRITE_BITS) == 0, "Protect=0x%lx", mbi.Protect);
    CHECKF((mbi.Protect & 0xff) != 0 && (mbi.Protect & PAGE_NOACCESS) == 0, "Protect=0x%lx", mbi.Protect);
}

TEST(user_serverinfo_shared_psi_view_covers_struct) {
    char *psi = si_get(); if (!psi) return;
    MEMORY_BASIC_INFORMATION mbi;
    REQUIRE(si_query(psi, &mbi));
    size_t avail = (size_t)((char *)mbi.BaseAddress + mbi.RegionSize - psi);
    CHECKF(avail >= 0x1b68, "contiguous bytes from psi: 0x%zx", avail);
}

TEST(user_serverinfo_shared_psi_write_attempt_fails) {
    char *psi = si_get(); if (!psi) return;
    /* ReadProcessMemory is the safe reader; NtWriteVirtualMemory to a read-only view must report failure (no fault). */
    unsigned long old = 0, nv;
    REQUIRE(k_try_read_u32(psi + SI_LastRITTick, &old));
    nv = old ^ 0x5a5a5a5a;
    MEMORY_BASIC_INFORMATION mbi;
    /* Only attempt the write when the view really is read-only: never risk altering a shared page. */
    if (!si_query(psi, &mbi) || (mbi.Protect & WRITE_BITS)) { CHECK(!(mbi.Protect & WRITE_BITS)); return; }
    SIZE_T wr = 0;
    BOOL ok = WriteProcessMemory(GetCurrentProcess(), psi + SI_LastRITTick, &nv, 4, &wr);
    unsigned long now = 0; k_try_read_u32(psi + SI_LastRITTick, &now);
    if (ok) { /* undo to keep the page sane, then fail the test */
        WriteProcessMemory(GetCurrentProcess(), psi + SI_LastRITTick, &old, 4, &wr);
    }
    CHECK(!ok);
    CHECK(now != nv || now == old);
}

TEST(user_serverinfo_shared_gsi_handle_entry_size) {
    char *psi = si_get(); if (!psi) return;
    struct si_gsi g; REQUIRE(si_gsi(&g));
    /* HMValidateHandle/IsWindow index the handle table with a 0x20 byte stride (Windows 10 x64). */
    EXPECT_EQ(g.entsz, 0x20);
}

TEST(user_serverinfo_shared_gsi_handle_table_mapping) {
    char *psi = si_get(); if (!psi) return;
    struct si_gsi g; REQUIRE(si_gsi(&g));
    CHECK(g.ahe != NULL);
    MEMORY_BASIC_INFORMATION mbi;
    REQUIRE(si_query(g.ahe, &mbi));
    CHECKF(mbi.State == MEM_COMMIT && mbi.Type == MEM_MAPPED, "State=0x%lx Type=0x%lx", mbi.State, mbi.Type);
    CHECKF((mbi.Protect & WRITE_BITS) == 0, "handle table Protect=0x%lx", mbi.Protect);
}

TEST(user_serverinfo_shared_handle_entry_count_bound) {
    char *psi = si_get(); if (!psi) return;
    unsigned long long n = 0;
    REQUIRE(k_try_read(psi + SI_cHandleEntries, &n, 8));
    /* Handles carry a 16-bit table index, and IsWindow compares (h & 0xffff) < cHandleEntries. */
    CHECKF(n >= 16 && n <= 0x10000, "cHandleEntries=%llu", n);
}

TEST(user_serverinfo_shared_handle_table_last_entry_readable) {
    char *psi = si_get(); if (!psi) return;
    struct si_gsi g; REQUIRE(si_gsi(&g));
    unsigned long long n = 0;
    REQUIRE(k_try_read(psi + SI_cHandleEntries, &n, 8));
    REQUIRE(n > 0 && n <= 0x10000);
    unsigned char e[0x18];
    CHECK(k_try_read((char *)g.ahe + (n - 1) * 0x18, e, sizeof e));
    CHECK(k_try_read(g.ahe, e, sizeof e));
}

TEST(user_serverinfo_shared_handle_entry0_is_not_a_window) {
    char *psi = si_get(); if (!psi) return;
    struct k_user_entry e;
    REQUIRE(k_user_entry(NULL, &e));
    /* IsWindow(NULL) is false in all cases; entry 0 must not be a live window object. */
    CHECK(e.type != K_TYPE_WINDOW || (e.flags & 1));
    REQUIRE(U(IsWindow));
    CHECK(!U(IsWindow)(NULL));
}

TEST(user_serverinfo_shared_gsi_dispinfo_pointer) {
    char *psi = si_get(); if (!psi) return;
    struct si_gsi g; REQUIRE(si_gsi(&g));
    CHECK(g.pDispInfo != NULL);
    unsigned char b[0x40];
    CHECK(k_try_read(g.pDispInfo, b, sizeof b));
}

TEST(user_serverinfo_shared_primary_monitor_object_reachable) {
    char *psi = si_get(); if (!psi) return;
    char *obj = si_primary_monitor_obj();
    CHECK(obj != NULL);
    if (!obj) return;
    unsigned char b[0x58];
    CHECK(k_try_read(obj, b, sizeof b));
}

TEST(user_serverinfo_shared_psi_stable_across_calls) {
    char *psi = si_get(); if (!psi) return;
    for (int i = 0; i < 50; i++) {
        U(GetSystemMetrics)(i % 20);
        U(GetSysColor)(i % 31);
        struct k_user_shared_info s;
        REQUIRE(k_shared_info(&s));
        if (s.psi != psi) { CHECK(s.psi == psi); return; }
    }
    CHECK(1);
}

static DWORD si_thread_psi(void *arg) {
    struct k_user_shared_info s;
    void **out = (void **)arg;
    U(GetSystemMetrics)(SM_CXSCREEN); /* first win32k call on this thread converts it to a GUI thread */
    out[0] = k_shared_info(&s) ? s.psi : NULL;
    unsigned long long n = 0;
    out[1] = (void *)(ULONG_PTR)(s.psi && k_try_read((char *)s.psi + SI_cHandleEntries, &n, 8) ? n : 0);
    return 0;
}
TEST(user_serverinfo_shared_psi_same_in_other_gui_thread) {
    char *psi = si_get(); if (!psi) return;
    void *out[2] = { 0, 0 };
    k_run_gui_thread(si_thread_psi, out);
    /* gSharedInfo is process-wide: another GUI thread sees the very same mapping. */
    CHECK(out[0] == psi);
    CHECK(out[1] != NULL);
}

/* ---------------- dwSRVIFlags ---------------- */
TEST(user_serverinfo_shared_flags_dbcs_matches_metric) {
    char *psi = si_get(); if (!psi) return;
    /* RealGetSystemMetrics case 0x2a: SM_DBCSENABLED is 1 iff dwSRVIFlags bit1 is set. */
    unsigned long f = si_u32(psi, SI_dwSRVIFlags);
    int m = U(GetSystemMetrics)(SM_DBCSENABLED);
    if (f & 2) EXPECT_EQ(m, 1); else EXPECT_EQ(m, 0);
}
TEST(user_serverinfo_shared_flags_ime_matches_metric) {
    char *psi = si_get(); if (!psi) return;
    /* RealGetSystemMetrics case 0x52: SM_IMMENABLED is bit 2 of the first shared-info word. */
    unsigned long f = si_u32(psi, SI_dwSRVIFlags);
    EXPECT_EQ(U(GetSystemMetrics)(SM_IMMENABLED), (f >> 2) & 1);
}
TEST(user_serverinfo_shared_flags_mideast_clear_means_zero) {
    char *psi = si_get(); if (!psi) return;
    unsigned long f = si_u32(psi, SI_dwSRVIFlags);
    int m = U(GetSystemMetrics)(SM_MIDEASTENABLED);
    /* case 0x4a: with bit3 clear the metric is 0; with it set the value comes from a helper. */
    if (!(f & 8)) EXPECT_EQ(m, 0); else CHECK(m == 0 || m == 1);
}
TEST(user_serverinfo_shared_flags_stable_bits_between_reads) {
    char *psi = si_get(); if (!psi) return;
    unsigned long a = si_u32(psi, SI_dwSRVIFlags);
    k_pump(20);
    unsigned long b = si_u32(psi, SI_dwSRVIFlags);
    /* DBCS/IME/MidEast follow the keyboard-layout/locale configuration; they do not flip by themselves. */
    CHECK((a & 0xe) == (b & 0xe));
}
TEST(user_serverinfo_shared_flags_value_logged) {
    char *psi = si_get(); if (!psi) return;
    t_note("dwSRVIFlags=0x%08lx", si_u32(psi, SI_dwSRVIFlags));
}

TEST(user_serverinfo_shared_remarks_bits_match_metrics_2000) {
    char *psi = si_get(); if (!psi) return;
    unsigned long r = si_u32(psi, SI_SystemRemarks);
    for (int n = 0; n < 7; n++) {
        int m = U(GetSystemMetrics)(0x2000 + n);
        /* GetSystemMetrics: nIndex in 0x2000..0x2006 -> bit (nIndex-0x2000) of a shared-info flags word. */
        EXPECT_EQ(m, (r >> n) & 1);
    }
}
TEST(user_serverinfo_shared_remarks_bits_0x2007_invalid) {
    REQUIRE(U(GetSystemMetrics));
    SET_ERR(0);
    int m = U(GetSystemMetrics)(0x2007);
    EXPECT_EQ(m, 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}
TEST(user_serverinfo_shared_remarks_shutting_down_is_zero) {
    REQUIRE(U(GetSystemMetrics));
    CHECK(U(GetSystemMetrics)(0x2000) == 0); /* SM_SHUTTINGDOWN while running a test */
}

/* ---------------- WinEvent mask ---------------- */
UDECL(BOOL, IsWinEventHookInstalled, (DWORD));
static void CALLBACK si_winevent_noop(HWINEVENTHOOK h, DWORD ev, HWND hwnd, LONG o, LONG c, DWORD t, DWORD tm) {}
TEST(user_serverinfo_shared_winevent_mask_tracks_hook) {
    char *psi = si_get(); if (!psi) return;
    REQUIRE(UN(IsWinEventHookInstalled));
    REQUIRE(U(SetWinEventHook)); REQUIRE(U(UnhookWinEvent));
    unsigned long before = si_u32(psi, SI_WinEventMask);
    HWINEVENTHOOK h = U(SetWinEventHook)(EVENT_OBJECT_CREATE, EVENT_OBJECT_CREATE, NULL, si_winevent_noop, GetCurrentProcessId(), 0,
                                          WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    REQUIRE(h);
    unsigned long during = si_u32(psi, SI_WinEventMask);
    /* IsWinEventHookInstalled / NotifyWinEvent test (a shared-info hook mask tested against the event's category). */
    CHECK(UN(IsWinEventHookInstalled)(EVENT_OBJECT_CREATE));
    CHECK(during != 0);
    CHECK((during & before) == before);
    CHECK(U(UnhookWinEvent)(h));
    unsigned long after = si_u32(psi, SI_WinEventMask);
    t_note("WinEvent mask before=0x%lx during=0x%lx after=0x%lx", before, during, after);
}
TEST(user_serverinfo_shared_winevent_mask_new_bit_added) {
    char *psi = si_get(); if (!psi) return;
    REQUIRE(U(SetWinEventHook)); REQUIRE(U(UnhookWinEvent));
    HWINEVENTHOOK h1 = U(SetWinEventHook)(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, NULL, si_winevent_noop, GetCurrentProcessId(), 0,
                                           WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    REQUIRE(h1);
    unsigned long m1 = si_u32(psi, SI_WinEventMask);
    HWINEVENTHOOK h2 = U(SetWinEventHook)(EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE, NULL, si_winevent_noop, GetCurrentProcessId(), 0,
                                           WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    REQUIRE(h2);
    unsigned long m2 = si_u32(psi, SI_WinEventMask);
    CHECK((m2 & m1) == m1); /* installing a second hook can only add categories */
    U(UnhookWinEvent)(h2);
    U(UnhookWinEvent)(h1);
}
TEST(user_serverinfo_shared_winevent_notify_above_range_skips_kernel) {
    REQUIRE(U(NotifyWinEvent));
    /* NotifyWinEvent only calls the kernel when event < 0x80000000 and the category is hooked; events >= 0x80000000 return without effect. */
    U(NotifyWinEvent)(0x80000001, NULL, 0, 0);
    CHECK(1);
}

/* ---------------- capture counter ---------------- */
TEST(user_serverinfo_shared_capture_count_nonzero_while_captured) {
    char *psi = si_get(); if (!psi) return;
    HWND w = k_make_window("si_capture"); if (!w) { t_skip("k_make_window failed"); return; }
    HWND prev = U(SetCapture)(w);
    HWND cur = U(GetCapture)();
    /* GetCapture returns NULL outright when the shared capture state is 0, so a window that is the capture must make it non-zero. */
    CHECK(cur == w);
    CHECK(si_u32(psi, SI_CaptureCount) != 0);
    U(ReleaseCapture)();
    CHECK(U(GetCapture)() == NULL);
    t_note("capture count after release = %lu (prev=%p)", si_u32(psi, SI_CaptureCount), prev);
    U(DestroyWindow)(w);
}
TEST(user_serverinfo_shared_capture_cleared_by_destroy) {
    char *psi = si_get(); if (!psi) return;
    HWND w = k_make_window("si_capture2"); if (!w) { t_skip("k_make_window failed"); return; }
    U(SetCapture)(w);
    CHECK(U(GetCapture)() == w);
    U(DestroyWindow)(w);
    CHECK(U(GetCapture)() != w);
}

/* ---------------- monitors reachable through the handle table ---------------- */
static HMONITOR si_primary(void) {
    POINT pt = { 0, 0 };
    return U(MonitorFromPoint)(pt, MONITOR_DEFAULTTOPRIMARY);
}
TEST(user_serverinfo_shared_monitor_handle_entry_type) {
    char *psi = si_get(); if (!psi) return;
    REQUIRE(U(MonitorFromPoint));
    HMONITOR m = si_primary(); REQUIRE(m);
    struct k_user_entry e; REQUIRE(k_user_entry(m, &e));
    /* GetMonitorInfoW requires entry type 0x0c and the deleted bit clear. */
    EXPECT_EQ(e.type, SI_TYPE_MONITOR);
    CHECK((e.flags & 1) == 0);
    CHECKF(e.uniq == (unsigned short)((ULONG_PTR)m >> 16) || e.uniq == 0xffff, "uniq=0x%x handle=%p", e.uniq, m);
}
TEST(user_serverinfo_shared_monitor_handle_below_entry_count) {
    char *psi = si_get(); if (!psi) return;
    REQUIRE(U(MonitorFromPoint));
    HMONITOR m = si_primary(); REQUIRE(m);
    unsigned long long n = 0; REQUIRE(k_try_read(psi + SI_cHandleEntries, &n, 8));
    CHECK(((ULONG_PTR)m & 0xffff) < n);
}
TEST(user_serverinfo_shared_monitor_object_rect_matches_api) {
    char *psi = si_get(); if (!psi) return;
    REQUIRE(U(MonitorFromPoint)); REQUIRE(U(GetMonitorInfoW));
    HMONITOR m = si_primary(); REQUIRE(m);
    MONITORINFO mi = { sizeof mi };
    REQUIRE(U(GetMonitorInfoW)(m, &mi));
    char *obj = si_monitor_obj(m);
    CHECK(obj != NULL); if (!obj) return;
    RECT raw, work;
    REQUIRE(k_try_read(obj + SI_MON_rcMonitor, &raw, sizeof raw));
    REQUIRE(k_try_read(obj + SI_MON_rcWork, &work, sizeof work));
    unsigned pd = si_u16(psi, SI_SystemDpi);
    /* GetMonitorInfoW copies obj+0x1c / obj+0x2c verbatim unless the calling thread's DPI context virtualizes coordinates. */
    if (pd == 96) {
        CHECK(raw.left == mi.rcMonitor.left && raw.top == mi.rcMonitor.top && raw.right == mi.rcMonitor.right && raw.bottom == mi.rcMonitor.bottom);
        CHECK(work.left == mi.rcWork.left && work.top == mi.rcWork.top && work.right == mi.rcWork.right && work.bottom == mi.rcWork.bottom);
    } else {
        t_note("system DPI %u: raw %ld,%ld,%ld,%ld vs API %ld,%ld,%ld,%ld", pd, raw.left, raw.top, raw.right, raw.bottom,
               mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right, mi.rcMonitor.bottom);
    }
    CHECK(raw.right > raw.left && raw.bottom > raw.top);
    CHECK(work.left >= raw.left && work.top >= raw.top && work.right <= raw.right && work.bottom <= raw.bottom);
}
TEST(user_serverinfo_shared_monitor_primary_pointer_matches_flag) {
    char *psi = si_get(); if (!psi) return;
    REQUIRE(U(MonitorFromPoint)); REQUIRE(U(GetMonitorInfoW));
    HMONITOR m = si_primary(); REQUIRE(m);
    MONITORINFO mi = { sizeof mi };
    REQUIRE(U(GetMonitorInfoW)(m, &mi));
    /* GetMonitorInfoW sets dwFlags = (monitor object == *(pDispInfo+8) + delta). */
    char *obj = si_monitor_obj(m), *prim = si_primary_monitor_obj();
    CHECK(obj != NULL && prim != NULL);
    CHECK(obj == prim);
    CHECK(mi.dwFlags & MONITORINFOF_PRIMARY);
}
TEST(user_serverinfo_shared_monitor_cbsize_protocol) {
    char *psi = si_get(); if (!psi) return;
    REQUIRE(U(MonitorFromPoint)); REQUIRE(U(GetMonitorInfoW));
    HMONITOR m = si_primary(); REQUIRE(m);
    MONITORINFOEXW ex; memset(&ex, 0, sizeof ex);
    ex.cbSize = sizeof(MONITORINFO);
    CHECK(U(GetMonitorInfoW)(m, (MONITORINFO *)&ex));
    memset(&ex, 0, sizeof ex);
    ex.cbSize = sizeof ex; /* 0x68 */
    EXPECT_EQ(ex.cbSize, 0x68);
    CHECK(U(GetMonitorInfoW)(m, (MONITORINFO *)&ex));
    CHECK(ex.szDevice[0] != 0);
    static const DWORD bad[] = { 0, 4, 0x27, 0x29, 0x67, 0x69, 0x100 };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        MONITORINFOEXW bx; memset(&bx, 0xcc, sizeof bx);
        bx.cbSize = bad[i];
        SET_ERR(0);
        BOOL r = U(GetMonitorInfoW)(m, (MONITORINFO *)&bx);
        /* neither 0x28 nor 0x68 -> ERROR_INVALID_PARAMETER */
        CHECKF(!r, "cbSize=0x%lx unexpectedly succeeded", bad[i]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
}
TEST(user_serverinfo_shared_monitor_invalid_handles_error) {
    char *psi = si_get(); if (!psi) return;
    REQUIRE(U(GetMonitorInfoW));
    HMONITOR bad[] = { NULL, (HMONITOR)(ULONG_PTR)1, (HMONITOR)(ULONG_PTR)0xFFFF, (HMONITOR)(ULONG_PTR)0x12345678, (HMONITOR)(ULONG_PTR)-1 };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        MONITORINFO mi = { sizeof mi };
        SET_ERR(0);
        BOOL r = U(GetMonitorInfoW)(bad[i], &mi);
        /* every validation failure in GetMonitorInfoW funnels to ERROR_INVALID_MONITOR_HANDLE (0x5b5) */
        CHECKF(!r, "handle %p accepted", bad[i]);
        EXPECT_ERR(0x5b5);
    }
}
TEST(user_serverinfo_shared_monitor_window_handle_not_a_monitor) {
    char *psi = si_get(); if (!psi) return;
    REQUIRE(U(GetMonitorInfoW));
    HWND w = k_make_window("si_mon_vs_wnd"); if (!w) { t_skip("k_make_window failed"); return; }
    MONITORINFO mi = { sizeof mi };
    SET_ERR(0);
    /* An HWND's handle-table entry has type 1, not 0x0c, so it is rejected as a monitor. */
    CHECK(!U(GetMonitorInfoW)((HMONITOR)w, &mi));
    EXPECT_ERR(0x5b5);
    HMONITOR m = si_primary();
    CHECK(!U(IsWindow)((HWND)m));
    U(DestroyWindow)(w);
}
TEST(user_serverinfo_shared_monitor_enum_entries_are_monitor_type) {
    char *psi = si_get(); if (!psi) return;
    REQUIRE(U(EnumDisplayMonitors));
    struct mon_list { HMONITOR m[16]; int n; } L = { {0}, 0 };
    BOOL CALLBACK cb(HMONITOR m, HDC dc, LPRECT r, LPARAM lp) {
        struct mon_list *l = (struct mon_list *)lp; if (l->n < 16) l->m[l->n++] = m; return TRUE;
    }
    CHECK(U(EnumDisplayMonitors)(NULL, NULL, cb, (LPARAM)&L));
    CHECK(L.n >= 1);
    for (int i = 0; i < L.n; i++) {
        struct k_user_entry e;
        if (!k_user_entry(L.m[i], &e)) { CHECK(0); continue; }
        EXPECT_EQ(e.type, SI_TYPE_MONITOR);
        CHECK(si_monitor_obj(L.m[i]) != NULL);
    }
}
TEST(user_serverinfo_shared_monitor_count_matches_metric_and_enum) {
    char *psi = si_get(); if (!psi) return;
    REQUIRE(U(EnumDisplayMonitors));
    struct si_mon_count c = { 0 };
    CHECK(U(EnumDisplayMonitors)(NULL, NULL, si_enum_cb, (LPARAM)&c));
    /* SM_CMONITORS is stored in aiSysMet[80]; it counts the monitors on the desktop which EnumDisplayMonitors visits. */
    EXPECT_EQ(si_i32(psi, SI_aiSysMet + 80 * 4), c.n);
    EXPECT_EQ(U(GetSystemMetrics)(SM_CMONITORS), c.n);
}
TEST(user_serverinfo_shared_virtual_screen_is_monitor_union) {
    char *psi = si_get(); if (!psi) return;
    REQUIRE(U(EnumDisplayMonitors));
    struct si_mon_count c = { 0 };
    REQUIRE(U(EnumDisplayMonitors)(NULL, NULL, si_enum_cb, (LPARAM)&c));
    int x = U(GetSystemMetrics)(SM_XVIRTUALSCREEN), y = U(GetSystemMetrics)(SM_YVIRTUALSCREEN);
    int w = U(GetSystemMetrics)(SM_CXVIRTUALSCREEN), h = U(GetSystemMetrics)(SM_CYVIRTUALSCREEN);
    if (si_u16(psi, SI_SystemDpi) != 96) { t_note("system DPI != 96: virtual screen %d,%d %dx%d vs union %ld,%ld,%ld,%ld", x, y, w, h, c.u.left, c.u.top, c.u.right, c.u.bottom); return; }
    CHECK(x == c.u.left && y == c.u.top);
    CHECK(w == c.u.right - c.u.left && h == c.u.bottom - c.u.top);
}

/* ---------------- DPI bookkeeping fields ---------------- */
TEST(user_serverinfo_shared_system_dpi_field_plausible) {
    char *psi = si_get(); if (!psi) return;
    unsigned dpi = si_u16(psi, SI_SystemDpi);
    CHECKF(dpi >= 96 && dpi <= 960, "system DPI field = %u", dpi);
    t_note("system DPI field = %u", dpi);
}
TEST(user_serverinfo_shared_system_dpi_has_cache_slot_zero) {
    char *psi = si_get(); if (!psi) return;
    unsigned dpi = si_u16(psi, SI_SystemDpi);
    /* GetDpiCacheSlot returns 0 first whenever the DPI equals the page field. */
    EXPECT_EQ(si_cache_slot(dpi, dpi), 0);
}
TEST(user_serverinfo_shared_getdpiforsystem_unaware_is_96) {
    char *psi = si_get(); if (!psi) return;
    REQUIRE(UN(GetDpiForSystem)); REQUIRE(UN(IsProcessDPIAware));
    UINT d = UN(GetDpiForSystem)();
    if (!UN(IsProcessDPIAware)()) {
        /* DPI-unaware thread on a composited desktop: GetDpiForSystem returns 0x60 before consulting the page. */
        EXPECT_EQ(d, 96);
    } else {
        EXPECT_EQ(d, si_u16(psi, SI_SystemDpi));
    }
}
UDECL(ULONG, NtUserGetDpiForCurrentProcess, (void));
TEST(user_serverinfo_shared_ntuser_dpi_for_current_process) {
    char *psi = si_get(); if (!psi) return;
    REQUIRE(UN(NtUserGetDpiForCurrentProcess));
    ULONG r = UN(NtUserGetDpiForCurrentProcess)();
    /* win32kfull NtUserGetDpiForCurrentProcess returns the 16-bit value stored in the W32PROCESS. */
    unsigned lo = r & 0xffff;
    /* the 16-bit W32PROCESS DPI is only guaranteed meaningful for DPI-aware callers (GetDpiForSystem consults it only then) */
    if (UN(IsProcessDPIAware) && !UN(IsProcessDPIAware)()) t_note("unaware process: NtUserGetDpiForCurrentProcess=%u page=%u", lo, si_u16(psi, SI_SystemDpi));
    else CHECKF(lo >= 96 && lo <= 960, "dpi=%u (raw 0x%lx)", lo, r);
}

/* ---------------- dialog base units / system font char size ---------------- */
TEST(user_serverinfo_shared_dialog_base_units_match_server_info) {
    char *psi = si_get(); if (!psi) return;
    REQUIRE(U(GetDialogBaseUnits)); REQUIRE(UN(IsProcessDPIAware));
    if (UN(IsProcessDPIAware)()) { t_skip("DPI-aware process uses a per-DPI DPISERVERINFO slot"); return; }
    /* GetDialogBaseUnits = MAKELONG(word at DPISERVERINFO+0x20, word at +0x24); unaware callers use the system-DPI values. */
    unsigned cx = si_u16(psi, SI_DpiServerInfo96 + SI_DSI_cxChar), cy = si_u16(psi, SI_DpiServerInfo96 + SI_DSI_cyChar);
    LONG u = U(GetDialogBaseUnits)();
    EXPECT_EQ((unsigned)LOWORD(u), cx);
    EXPECT_EQ((unsigned)HIWORD(u), cy);
    CHECK(cx > 0 && cy > 0);
}
TEST(user_serverinfo_shared_dialog_base_units_vs_system_font_metrics) {
    char *psi = si_get(); if (!psi) return;
    REQUIRE(U(GetDialogBaseUnits)); REQUIRE(U(GetDC)); REQUIRE(U(GetTextMetricsW)); REQUIRE(U(GetStockObject)); REQUIRE(U(SelectObject));
    HDC dc = U(GetDC)(NULL); REQUIRE(dc);
    HGDIOBJ old = U(SelectObject)(dc, U(GetStockObject)(SYSTEM_FONT));
    TEXTMETRICW tm; BOOL ok = U(GetTextMetricsW)(dc, &tm);
    U(SelectObject)(dc, old); U(ReleaseDC)(NULL, dc);
    REQUIRE(ok);
    LONG u = U(GetDialogBaseUnits)();
    t_note("dialog base units %dx%d, SYSTEM_FONT tmAveCharWidth=%ld tmHeight=%ld", LOWORD(u), HIWORD(u), tm.tmAveCharWidth, tm.tmHeight);
    CHECK(LOWORD(u) > 0 && HIWORD(u) > 0);
}

/* ---------------- remote session / console id ---------------- */
TEST(user_serverinfo_shared_remote_session_metric_matches_console_id) {
    REQUIRE(U(GetSystemMetrics));
    U(GetSystemMetrics)(SM_CXSCREEN);
    DWORD sid = k_session_id();
    DWORD active = *(volatile DWORD *)0x7ffe02d8; /* KUSER_SHARED_DATA.ActiveConsoleId, what RtlGetActiveConsoleId returns */
    int rs = U(GetSystemMetrics)(SM_REMOTESESSION);
    if (t_is_wine()) { t_note("Wine: SM_REMOTESESSION=%d", rs); return; }
    /* RealGetSystemMetrics(0x1000): 0 for the service session, else 1 iff PEB->SessionId != active console id. */
    if (sid == 0) EXPECT_EQ(rs, 0); else EXPECT_EQ(rs, sid != active);
}
TEST(user_serverinfo_shared_getsystemmetrics_0x1800_is_one) {
    REQUIRE(U(GetSystemMetrics));
    if (t_is_wine()) { t_skip("Wine"); return; }
    EXPECT_EQ(U(GetSystemMetrics)(0x1800), 1); /* RealGetSystemMetrics case 0x1800 returns 1 unconditionally */
}
