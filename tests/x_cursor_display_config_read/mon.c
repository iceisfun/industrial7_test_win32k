/* Monitor services: NtUserEnumDisplayMonitors (behind user32!EnumDisplayMonitors, a bare jump), NtUserPaintMonitor,
 * NtUserSetDisplayMapping, and the stubs of NtUserSetMonitorWorkArea and NtUserFunctionalizeDisplayConfig, which are
 * identified by shape and service number but never called (they change monitor geometry or display state).
 *
 * Measured on Windows 10 22H2: the callback gets (HMONITOR, a DC of its own when a DC was passed else NULL, the
 * monitor rectangle clipped by the clip rectangle) on the calling thread; a clip rectangle that excludes every
 * monitor, an empty clip and an inverted clip call back nothing and still return TRUE; an invalid or memory DC
 * returns FALSE with no callback. The number of callbacks equals GetSystemMetrics(SM_CMONITORS).
 */
#include "x.h"

#define NEEDENUM() do { NEED(NtUserEnumDisplayMonitors); } while (0)
static BOOL enum_user(HDC dc, const RECT *clip, struct xc_enum *e) { memset(e, 0, sizeof *e); return U(EnumDisplayMonitors)(dc, clip, xc_enum_cb, (LPARAM)e); }
static BOOL enum_raw(HDC dc, const RECT *clip, struct xc_enum *e) { memset(e, 0, sizeof *e); return UN(NtUserEnumDisplayMonitors)(dc, clip, xc_enum_cb, (LPARAM)e); }

XT(enum_returns_true_and_one_callback_per_monitor) {
    struct xc_enum e;
    EXPECT_EQ(enum_user(NULL, NULL, &e), TRUE);
    EXPECT_EQ(e.total, U(GetSystemMetrics)(SM_CMONITORS));
    CHECK(e.total >= 1);
}
XT(enum_raw_syscall_matches_user32) {
    NEEDENUM();
    struct xc_enum a, b;
    EXPECT_EQ(enum_user(NULL, NULL, &a), TRUE);
    EXPECT_EQ(enum_raw(NULL, NULL, &b), TRUE);
    EXPECT_EQ(a.total, b.total);
    CHECK(!memcmp(a.m, b.m, sizeof a.m));
    CHECK(!memcmp(a.rc, b.rc, sizeof a.rc));
}
XT(enum_callback_runs_on_the_calling_thread) {
    struct xc_enum e;
    enum_user(NULL, NULL, &e);
    EXPECT_EQ(e.tid, GetCurrentThreadId());
}
XT(enum_null_dc_gives_null_callback_dc) {
    struct xc_enum e;
    enum_user(NULL, NULL, &e);
    for (int i = 0; i < e.n; i++) CHECKF(e.dc[i] == NULL, "monitor %d got a DC", i);
}
XT(enum_callback_rects_are_the_monitor_rects) {
    struct xc_enum e;
    enum_user(NULL, NULL, &e);
    for (int i = 0; i < e.n; i++) {
        MONITORINFO mi; memset(&mi, 0, sizeof mi); mi.cbSize = sizeof mi;
        CHECK(U(GetMonitorInfoW)(e.m[i], &mi));
        CHECKF(EqualRect(&mi.rcMonitor, &e.rc[i]), "monitor %d rectangle differs from GetMonitorInfo", i);
    }
}
XT(enum_monitors_are_distinct_and_valid) {
    struct xc_enum e;
    enum_user(NULL, NULL, &e);
    for (int i = 0; i < e.n; i++) {
        CHECK(e.m[i] != NULL);
        for (int j = i + 1; j < e.n; j++) CHECK(e.m[i] != e.m[j]);
        MONITORINFO mi; memset(&mi, 0, sizeof mi); mi.cbSize = sizeof mi;
        CHECK(U(GetMonitorInfoW)(e.m[i], &mi));
    }
}
XT(enum_includes_the_primary_monitor) {
    struct xc_enum e;
    enum_user(NULL, NULL, &e);
    POINT o = { 0, 0 };
    HMONITOR p = U(MonitorFromPoint)(o, MONITOR_DEFAULTTOPRIMARY);
    int found = 0, primaries = 0;
    for (int i = 0; i < e.n; i++) {
        if (e.m[i] == p) found = 1;
        MONITORINFO mi; memset(&mi, 0, sizeof mi); mi.cbSize = sizeof mi;
        if (U(GetMonitorInfoW)(e.m[i], &mi) && (mi.dwFlags & MONITORINFOF_PRIMARY)) primaries++;
    }
    CHECK(found);
    EXPECT_EQ(primaries, 1);
}
XT(enum_union_of_rects_is_the_virtual_screen) {
    struct xc_enum e;
    enum_user(NULL, NULL, &e);
    RECT u = e.rc[0];
    for (int i = 1; i < e.n; i++) UnionRect(&u, &u, &e.rc[i]);
    EXPECT_EQ(u.left, U(GetSystemMetrics)(SM_XVIRTUALSCREEN));
    EXPECT_EQ(u.top, U(GetSystemMetrics)(SM_YVIRTUALSCREEN));
    EXPECT_EQ(u.right - u.left, U(GetSystemMetrics)(SM_CXVIRTUALSCREEN));
    EXPECT_EQ(u.bottom - u.top, U(GetSystemMetrics)(SM_CYVIRTUALSCREEN));
}
XT(enum_primary_rect_is_the_screen_size) {
    struct xc_enum e;
    enum_user(NULL, NULL, &e);
    POINT o = { 0, 0 };
    HMONITOR p = U(MonitorFromPoint)(o, MONITOR_DEFAULTTOPRIMARY);
    for (int i = 0; i < e.n; i++) if (e.m[i] == p) {
        EXPECT_EQ(e.rc[i].right - e.rc[i].left, U(GetSystemMetrics)(SM_CXSCREEN));
        EXPECT_EQ(e.rc[i].bottom - e.rc[i].top, U(GetSystemMetrics)(SM_CYSCREEN));
    }
}
XT(enum_passes_lparam_through) {
    struct xc_enum e;
    enum_user(NULL, NULL, &e);
    CHECK(e.total >= 1);
    EXPECT_EQ(e.total, e.n);
}
static BOOL CALLBACK lp_cb(HMONITOR m, HDC dc, LPRECT rc, LPARAM lp) { *(LPARAM *)lp = lp; return TRUE; }
XT(enum_lparam_value_is_delivered_unchanged) {
    LPARAM cell = 0;
    EXPECT_EQ(U(EnumDisplayMonitors)(NULL, NULL, lp_cb, (LPARAM)&cell), TRUE);
    EXPECT_EQ(cell, (LPARAM)&cell);
}
XT(enum_callback_returning_false_stops_and_returns_false) {
    struct xc_enum e;
    memset(&e, 0, sizeof e); e.stop_after = 1;
    BOOL r = U(EnumDisplayMonitors)(NULL, NULL, xc_enum_cb, (LPARAM)&e);
    EXPECT_EQ(r, FALSE);
    EXPECT_EQ(e.total, 1);
}
static BOOL CALLBACK nested_cb(HMONITOR m, HDC dc, LPRECT rc, LPARAM lp) {
    struct xc_enum *e = (struct xc_enum *)lp;
    struct xc_enum inner; memset(&inner, 0, sizeof inner);
    if (U(EnumDisplayMonitors)(NULL, NULL, xc_enum_cb, (LPARAM)&inner) && inner.total == U(GetSystemMetrics)(SM_CMONITORS)) e->nested++;
    e->total++;
    return TRUE;
}
XT(enum_nested_enumeration_from_a_callback_works) {
    struct xc_enum e; memset(&e, 0, sizeof e);
    EXPECT_EQ(U(EnumDisplayMonitors)(NULL, NULL, nested_cb, (LPARAM)&e), TRUE);
    EXPECT_EQ(e.nested, e.total);
    CHECK(e.total >= 1);
}
XT(enum_is_repeatable) {
    struct xc_enum a, b;
    enum_user(NULL, NULL, &a); enum_user(NULL, NULL, &b);
    EXPECT_EQ(a.total, b.total);
    CHECK(!memcmp(a.m, b.m, sizeof a.m));
    CHECK(!memcmp(a.rc, b.rc, sizeof a.rc));
}
XT(enum_does_not_set_last_error) {
    struct xc_enum e;
    SET_ERR(XSENT);
    enum_user(NULL, NULL, &e);
    EXPECT_EQ(GetLastError(), XSENT);
}

/* ---- clip rectangle ------------------------------------------------------------------------------------- */
XT(enum_clip_inside_the_primary_monitor_is_returned_as_is) {
    struct xc_enum e;
    RECT clip = { 10, 10, 50, 60 };
    EXPECT_EQ(enum_user(NULL, &clip, &e), TRUE);
    EXPECT_EQ(e.total, 1);
    CHECK(EqualRect(&e.rc[0], &clip));
}
XT(enum_clip_is_intersected_with_the_monitor) {
    struct xc_enum e, all;
    enum_user(NULL, NULL, &all);
    RECT clip = { all.rc[0].left - 100, all.rc[0].top - 100, all.rc[0].left + 40, all.rc[0].top + 30 };
    EXPECT_EQ(enum_user(NULL, &clip, &e), TRUE);
    CHECK(e.total >= 1);
    RECT want = { all.rc[0].left, all.rc[0].top, all.rc[0].left + 40, all.rc[0].top + 30 };
    CHECK(EqualRect(&e.rc[0], &want));
}
XT(enum_clip_covering_everything_gives_the_monitor_rects) {
    struct xc_enum e, all;
    enum_user(NULL, NULL, &all);
    RECT clip = { -100000, -100000, 100000, 100000 };
    enum_user(NULL, &clip, &e);
    EXPECT_EQ(e.total, all.total);
    CHECK(!memcmp(e.rc, all.rc, sizeof e.rc));
}
XT(enum_clip_outside_every_monitor_calls_nothing) {
    struct xc_enum e;
    RECT clip = { -50000, -50000, -49000, -49000 };
    EXPECT_EQ(enum_user(NULL, &clip, &e), TRUE);
    EXPECT_EQ(e.total, 0);
}
XT(enum_empty_clip_calls_nothing) {
    struct xc_enum e;
    RECT clip = { 0, 0, 0, 0 };
    EXPECT_EQ(enum_user(NULL, &clip, &e), TRUE);
    EXPECT_EQ(e.total, 0);
    RECT line = { 10, 10, 10, 50 };
    EXPECT_EQ(enum_user(NULL, &line, &e), TRUE);
    EXPECT_EQ(e.total, 0);
}
XT(enum_inverted_clip_calls_nothing) {
    struct xc_enum e;
    RECT clip = { 50, 50, 10, 10 };
    EXPECT_EQ(enum_user(NULL, &clip, &e), TRUE);
    EXPECT_EQ(e.total, 0);
}
XT(enum_one_pixel_clip_at_the_origin_gives_one_pixel) {
    struct xc_enum e;
    RECT clip = { 0, 0, 1, 1 };
    EXPECT_EQ(enum_user(NULL, &clip, &e), TRUE);
    CHECK(e.total >= 1);
    CHECK(EqualRect(&e.rc[0], &clip));
}
XT(enum_clip_raw_matches_user32) {
    NEEDENUM();
    struct xc_enum a, b;
    RECT clip = { 5, 5, 200, 100 };
    enum_user(NULL, &clip, &a); enum_raw(NULL, &clip, &b);
    EXPECT_EQ(a.total, b.total);
    CHECK(!memcmp(a.rc, b.rc, sizeof a.rc));
}
XT(enum_clip_is_not_modified) {
    struct xc_enum e;
    RECT clip = { -100, -100, 40, 30 };
    enum_user(NULL, &clip, &e);
    CHECK(clip.left == -100 && clip.top == -100 && clip.right == 40 && clip.bottom == 30);
}
XT(enum_unreadable_clip_does_not_crash_and_calls_nothing) {
    NEEDENUM();
    struct xc_enum e; memset(&e, 0, sizeof e);
    BOOL r = UN(NtUserEnumDisplayMonitors)(NULL, (const RECT *)(ULONG_PTR)1, xc_enum_cb, (LPARAM)&e);
    CHECKF(r == 0 || r == 1, "result %d", r);
    if (r == 0) EXPECT_EQ(e.total, 0);
}

/* ---- device contexts ------------------------------------------------------------------------------------- */
XT(enum_screen_dc_enumerates_every_monitor) {
    struct xc_enum e;
    HDC dc = U(GetDC)(NULL);
    CHECK(dc != NULL);
    EXPECT_EQ(enum_user(dc, NULL, &e), TRUE);
    EXPECT_EQ(e.total, U(GetSystemMetrics)(SM_CMONITORS));
    U(ReleaseDC)(NULL, dc);
}
XT(enum_screen_dc_callback_gets_a_dc) {
    struct xc_enum e;
    HDC dc = U(GetDC)(NULL);
    enum_user(dc, NULL, &e);
    for (int i = 0; i < e.n; i++) CHECKF(e.dc[i] != NULL, "monitor %d got no DC", i);
    U(ReleaseDC)(NULL, dc);
}
XT(enum_screen_dc_callback_dc_is_not_the_passed_dc) {
    struct xc_enum e;
    HDC dc = U(GetDC)(NULL);
    enum_user(dc, NULL, &e);
    for (int i = 0; i < e.n; i++) CHECKF(e.dc[i] != dc, "monitor %d got the caller's DC", i);
    U(ReleaseDC)(NULL, dc);
}
XT(enum_screen_dc_rects_match_the_null_dc_rects) {
    struct xc_enum a, b;
    HDC dc = U(GetDC)(NULL);
    enum_user(NULL, NULL, &a); enum_user(dc, NULL, &b);
    EXPECT_EQ(a.total, b.total);
    CHECK(!memcmp(a.rc, b.rc, sizeof a.rc));
    U(ReleaseDC)(NULL, dc);
}
XT(enum_screen_dc_with_clip_intersects) {
    struct xc_enum e;
    HDC dc = U(GetDC)(NULL);
    RECT clip = { 10, 10, 50, 60 };
    EXPECT_EQ(enum_user(dc, &clip, &e), TRUE);
    EXPECT_EQ(e.total, 1);
    CHECK(EqualRect(&e.rc[0], &clip));
    U(ReleaseDC)(NULL, dc);
}
XT(enum_screen_dc_with_clip_outside_calls_nothing) {
    struct xc_enum e;
    HDC dc = U(GetDC)(NULL);
    RECT clip = { -50000, -50000, -49000, -49000 };
    EXPECT_EQ(enum_user(dc, &clip, &e), TRUE);
    EXPECT_EQ(e.total, 0);
    U(ReleaseDC)(NULL, dc);
}
XT(enum_invalid_dc_returns_false_without_callbacks) {
    struct xc_enum e;
    EXPECT_EQ(enum_user((HDC)(ULONG_PTR)0x1234, NULL, &e), FALSE);
    EXPECT_EQ(e.total, 0);
    EXPECT_EQ(enum_user((HDC)(ULONG_PTR)1, NULL, &e), FALSE);
    EXPECT_EQ(e.total, 0);
}
XT(enum_deleted_dc_returns_false_without_callbacks) {
    struct xc_enum e;
    HDC dc = U(GetDC)(NULL);
    HDC mem = U(CreateCompatibleDC)(dc);
    U(DeleteDC)(mem);
    EXPECT_EQ(enum_user(mem, NULL, &e), FALSE);
    EXPECT_EQ(e.total, 0);
    U(ReleaseDC)(NULL, dc);
}
XT(enum_memory_dc_returns_false_without_callbacks) {
    struct xc_enum e;
    HDC dc = U(GetDC)(NULL);
    HDC mem = U(CreateCompatibleDC)(dc);
    EXPECT_EQ(enum_user(mem, NULL, &e), FALSE);
    EXPECT_EQ(e.total, 0);
    U(DeleteDC)(mem);
    U(ReleaseDC)(NULL, dc);
}
XT(enum_memory_dc_with_bitmap_returns_false_without_callbacks) {
    struct xc_enum e;
    HDC dc = U(GetDC)(NULL);
    HDC mem = U(CreateCompatibleDC)(dc);
    HBITMAP bm = U(CreateCompatibleBitmap)(dc, 16, 16);
    HGDIOBJ old = U(SelectObject)(mem, bm);
    EXPECT_EQ(enum_user(mem, NULL, &e), FALSE);
    EXPECT_EQ(e.total, 0);
    U(SelectObject)(mem, old); U(DeleteObject)(bm); U(DeleteDC)(mem);
    U(ReleaseDC)(NULL, dc);
}
XT(enum_hidden_window_dc_calls_nothing) {
    struct xc_enum e;
    HWND w = xc_hidden_window();
    HDC dc = U(GetDC)(w);
    CHECK(dc != NULL);
    BOOL r = enum_user(dc, NULL, &e);
    CHECKF(r == TRUE, "result %d", r);
    EXPECT_EQ(e.total, 0);
    U(ReleaseDC)(w, dc); U(DestroyWindow)(w);
}
XT(enum_invalid_dc_raw_matches_user32) {
    NEEDENUM();
    struct xc_enum e;
    EXPECT_EQ(enum_raw((HDC)(ULONG_PTR)0x1234, NULL, &e), FALSE);
    EXPECT_EQ(e.total, 0);
}

/* ---- NtUserPaintMonitor(HMONITOR, HDC, RECT *) ----------------------------------------------------------- */
#define NEEDPAINT() NEED(NtUserPaintMonitor)
static HMONITOR primary_mon(void) { POINT o = { 0, 0 }; return U(MonitorFromPoint)(o, MONITOR_DEFAULTTOPRIMARY); }
XT(paint_null_dc_fails_with_invalid_handle) {
    NEEDPAINT();
    RECT rc = { 0, 0, 16, 16 };
    XFAIL(UN(NtUserPaintMonitor)(primary_mon(), NULL, &rc), XE_INVALID_HANDLE);
}
XT(paint_garbage_dc_fails_with_invalid_handle) {
    NEEDPAINT();
    RECT rc = { 0, 0, 16, 16 };
    XFAIL(UN(NtUserPaintMonitor)(primary_mon(), (HDC)(ULONG_PTR)0x1234, &rc), XE_INVALID_HANDLE);
}
XT(paint_deleted_dc_fails_with_invalid_handle) {
    NEEDPAINT();
    HDC dc = U(GetDC)(NULL), mem = U(CreateCompatibleDC)(dc);
    U(DeleteDC)(mem);
    RECT rc = { 0, 0, 16, 16 };
    XFAIL(UN(NtUserPaintMonitor)(primary_mon(), mem, &rc), XE_INVALID_HANDLE);
    U(ReleaseDC)(NULL, dc);
}
XT(paint_null_rect_returns_false) {
    NEEDPAINT();
    HDC dc = U(GetDC)(NULL), mem = U(CreateCompatibleDC)(dc);
    HBITMAP bm = U(CreateCompatibleBitmap)(dc, 16, 16);
    HGDIOBJ old = U(SelectObject)(mem, bm);
    EXPECT_EQ(UN(NtUserPaintMonitor)(primary_mon(), mem, NULL), 0);
    U(SelectObject)(mem, old); U(DeleteObject)(bm); U(DeleteDC)(mem); U(ReleaseDC)(NULL, dc);
}
XT(paint_invalid_monitor_returns_false_and_leaves_the_rect) {
    NEEDPAINT();
    HDC dc = U(GetDC)(NULL);
    static const ULONG_PTR bad[] = { 0, 1, 0x1234, 0xFFFFFFFF };
    for (unsigned i = 0; i < 4; i++) {
        RECT rc = { 1, 2, 17, 18 };
        EXPECT_EQ(UN(NtUserPaintMonitor)((HMONITOR)bad[i], dc, &rc), 0);
        CHECK(rc.left == 1 && rc.top == 2 && rc.right == 17 && rc.bottom == 18);
    }
    U(ReleaseDC)(NULL, dc);
}
XT(paint_window_handle_as_monitor_returns_false) {
    NEEDPAINT();
    HWND w = xc_hidden_window();
    HDC dc = U(GetDC)(NULL);
    RECT rc = { 0, 0, 16, 16 };
    EXPECT_EQ(UN(NtUserPaintMonitor)((HMONITOR)w, dc, &rc), 0);
    U(ReleaseDC)(NULL, dc); U(DestroyWindow)(w);
}
XT(paint_valid_monitor_into_a_memory_dc_succeeds) {
    NEEDPAINT();
    HDC dc = U(GetDC)(NULL), mem = U(CreateCompatibleDC)(dc);
    HBITMAP bm = U(CreateCompatibleBitmap)(dc, 16, 16);
    HGDIOBJ old = U(SelectObject)(mem, bm);
    RECT rc = { 0, 0, 16, 16 };
    EXPECT_EQ(UN(NtUserPaintMonitor)(primary_mon(), mem, &rc), 1);
    CHECK(rc.left == 0 && rc.top == 0 && rc.right == 16 && rc.bottom == 16);
    U(SelectObject)(mem, old); U(DeleteObject)(bm); U(DeleteDC)(mem); U(ReleaseDC)(NULL, dc);
}

/* ---- NtUserSetDisplayMapping(HANDLE device, HMONITOR) ---------------------------------------------------- */
#define NEEDMAP() NEED(NtUserSetDisplayMapping)
XT(mapping_null_device_fails_with_invalid_handle) {
    NEEDMAP();
    XFAIL(UN(NtUserSetDisplayMapping)(NULL, primary_mon()), XE_INVALID_HANDLE);
}
XT(mapping_null_device_and_null_monitor_fails_with_invalid_handle) {
    NEEDMAP();
    XFAIL(UN(NtUserSetDisplayMapping)(NULL, NULL), XE_INVALID_HANDLE);
}
XT(mapping_monitor_as_device_fails_with_invalid_handle) {
    NEEDMAP();
    HMONITOR m = primary_mon();
    XFAIL(UN(NtUserSetDisplayMapping)((HANDLE)m, m), XE_INVALID_HANDLE);
}
XT(mapping_window_as_device_fails_with_invalid_handle) {
    NEEDMAP();
    HWND w = xc_hidden_window();
    XFAIL(UN(NtUserSetDisplayMapping)((HANDLE)w, primary_mon()), XE_INVALID_HANDLE);
    U(DestroyWindow)(w);
}
XT(mapping_cursor_as_device_fails_with_invalid_handle) {
    NEEDMAP();
    HCURSOR c = xc_cursor(32, 32, 0, 0, 0);
    XFAIL(UN(NtUserSetDisplayMapping)((HANDLE)c, primary_mon()), XE_INVALID_HANDLE);
    U(DestroyCursor)(c);
}
XT(mapping_garbage_device_fails_with_invalid_handle) {
    NEEDMAP();
    XFAIL(UN(NtUserSetDisplayMapping)((HANDLE)(ULONG_PTR)0x1234, primary_mon()), XE_INVALID_HANDLE);
    XFAIL(UN(NtUserSetDisplayMapping)((HANDLE)(ULONG_PTR)0xFFFFFFFF, primary_mon()), XE_INVALID_HANDLE);
}
XT(mapping_rejections_leave_the_monitor_list_alone) {
    NEEDMAP();
    struct xc_enum a, b;
    enum_user(NULL, NULL, &a);
    UN(NtUserSetDisplayMapping)(NULL, primary_mon());
    UN(NtUserSetDisplayMapping)((HANDLE)(ULONG_PTR)0x1234, NULL);
    enum_user(NULL, NULL, &b);
    EXPECT_EQ(a.total, b.total);
    CHECK(!memcmp(a.rc, b.rc, sizeof a.rc));
}

/* ---- work area (read-only relations) and stub identity ---------------------------------------------------- */
XT(workarea_lies_inside_every_monitor) {
    struct xc_enum e;
    enum_user(NULL, NULL, &e);
    for (int i = 0; i < e.n; i++) {
        MONITORINFO mi; memset(&mi, 0, sizeof mi); mi.cbSize = sizeof mi;
        CHECK(U(GetMonitorInfoW)(e.m[i], &mi));
        RECT t; IntersectRect(&t, &mi.rcMonitor, &mi.rcWork);
        CHECKF(EqualRect(&t, &mi.rcWork), "monitor %d work area leaves the monitor", i);
        CHECKF(!IsRectEmpty(&mi.rcWork), "monitor %d has an empty work area", i);
    }
}
XT(workarea_of_the_primary_monitor_is_the_spi_work_area) {
    RECT spi; memset(&spi, 0, sizeof spi);
    CHECK(U(SystemParametersInfoW)(SPI_GETWORKAREA, 0, &spi, 0));
    MONITORINFO mi; memset(&mi, 0, sizeof mi); mi.cbSize = sizeof mi;
    CHECK(U(GetMonitorInfoW)(primary_mon(), &mi));
    CHECK(EqualRect(&mi.rcWork, &spi));
}
XT(workarea_query_does_not_change_it) {
    MONITORINFO a, b; memset(&a, 0, sizeof a); memset(&b, 0, sizeof b); a.cbSize = b.cbSize = sizeof a;
    U(GetMonitorInfoW)(primary_mon(), &a); U(GetMonitorInfoW)(primary_mon(), &b);
    CHECK(EqualRect(&a.rcWork, &b.rcWork));
}

#define STUBTEST(name, num) \
    XT(stub_##name) { \
        FARPROC p = t_proc_in("win32u.dll", #name); \
        if (!p) { if (t_is_wine()) { t_skip("not exported by this win32u"); return; } CHECK(p != NULL); return; } \
        if (t_is_wine()) { t_skip("emulator stub"); return; } \
        CHECK(k_stub_valid(p)); \
        EXPECT_EQ((long)k_stub_number(p), (long)(num)); \
    }
STUBTEST(NtUserGetCursor, 5118)
STUBTEST(NtUserSetCursor, 4122)
STUBTEST(NtUserGetRequiredCursorSizes, 5178)
STUBTEST(NtUserLinkDpiCursor, 5235)
STUBTEST(NtUserSetCursorContents, 5318)
STUBTEST(NtUserGetDisplayConfigBufferSizes, 5124)
STUBTEST(NtUserDisplayConfigSetDeviceInfo, 5073)
STUBTEST(NtUserSetDisplayConfig, 5324)
STUBTEST(NtUserSetDisplayMapping, 5325)
STUBTEST(NtUserPaintMonitor, 5259)
STUBTEST(NtUserEnumDisplayMonitors, 4170)
STUBTEST(NtUserSetMonitorWorkArea, 5345)
STUBTEST(NtUserFunctionalizeDisplayConfig, 5106)
