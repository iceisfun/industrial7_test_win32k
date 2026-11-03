/* gap_07 user-side direct tests: NtUserGetCIMSSM, NtUserCopyAcceleratorTable, NtUserGetMenuItemRect,
 * NtUserMenuItemFromPoint, NtUserTransformPoint/Rect, NtUserDrawIconEx, NtUserUpdateLayeredWindow,
 * NtUserQueryDisplayConfig. */
#include "g7.h"

/* ---- NtUserGetCIMSSM(INPUT_MESSAGE_SOURCE *) -------------------------------------------------------------- */
/* win32kbase NtUserGetCIMSSM: NULL out pointer falls to UserSetLastError(ERROR_INVALID_PARAMETER) and returns 0. */
TEST(gap7_cimssm_null_out_fails_invalid_parameter) {
    NtUserGetCIMSSM_pfn f = G7N(NtUserGetCIMSSM); REQUIRE(f);
    SetLastError(0);
    BOOL r = f(NULL);
    EXPECT_EQ(r, FALSE);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}
/* The same NULL rule holds on every call (no state carried between calls). */
TEST(gap7_cimssm_null_out_repeatable) {
    NtUserGetCIMSSM_pfn f = G7N(NtUserGetCIMSSM); REQUIRE(f);
    for (int i = 0; i < 4; i++) {
        SetLastError(0);
        EXPECT_EQ(f(NULL), FALSE);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
}
/* A valid 8 byte buffer: on success the kernel stores INPUT_MESSAGE_SOURCE{deviceType, originId}.
 * With no input message being processed the result is environment dependent, so values are noted. */
TEST(gap7_cimssm_valid_buffer_layout) {
    NtUserGetCIMSSM_pfn f = G7N(NtUserGetCIMSSM); REQUIRE(f);
    k_pump(0);
    struct { int deviceType, originId; } src = { 0x5a5a5a5a, 0x5a5a5a5a };
    SetLastError(0);
    BOOL r = f(&src);
    if (r) {
        CHECKF(src.deviceType != 0x5a5a5a5a, "deviceType untouched on success");
        t_note("GetCIMSSM ok deviceType=%d originId=%d", src.deviceType, src.originId);
    } else {
        t_note("GetCIMSSM false, error %lu, dev=0x%x org=0x%x", GetLastError(), src.deviceType, src.originId);
    }
}

/* ---- NtUserCopyAcceleratorTable(HACCEL, ACCEL *, int) ------------------------------------------------------ */
static HACCEL make_accel(int n, WORD *keys)
{
    ACCEL a[8]; memset(a, 0, sizeof a);
    for (int i = 0; i < n; i++) {
        a[i].fVirt = FVIRTKEY | (i & 1 ? FCONTROL : FSHIFT);
        a[i].key = keys ? keys[i] : (WORD)('A' + i);
        a[i].cmd = (WORD)(100 + i);
    }
    return U(CreateAcceleratorTableW)(a, n);
}
TEST(gap7_accel_layout_is_six_bytes) {
    CHECK(sizeof(ACCEL) == 6);
    ACCEL a; EXPECT_EQ((char *)&a.key - (char *)&a, 2);
    EXPECT_EQ((char *)&a.cmd - (char *)&a, 4);
}
/* NULL buffer: the kernel answers the entry count and ignores argument 3. */
TEST(gap7_accel_null_buffer_returns_count) {
    NtUserCopyAcceleratorTable_pfn f = G7N(NtUserCopyAcceleratorTable); REQUIRE(f);
    for (int n = 1; n <= 8; n++) {
        HACCEL h = make_accel(n, NULL); REQUIRE(h);
        EXPECT_EQ(f(h, NULL, 0), n);
        EXPECT_EQ(f(h, NULL, 1000), n);
        U(DestroyAcceleratorTable)(h);
    }
}
TEST(gap7_accel_copy_roundtrip_entries) {
    NtUserCopyAcceleratorTable_pfn f = G7N(NtUserCopyAcceleratorTable); REQUIRE(f);
    WORD keys[3] = { 'Q', VK_F5, 'Z' };
    HACCEL h = make_accel(3, keys); REQUIRE(h);
    ACCEL out[4]; memset(out, 0xcc, sizeof out);
    EXPECT_EQ(f(h, out, 4), 3);
    for (int i = 0; i < 3; i++) {
        /* bit 7 (FLAST) is masked off in the copy, the pad byte is zeroed, key/cmd come back unchanged */
        CHECKF((out[i].fVirt & 0x80) == 0, "entry %d fVirt 0x%x", i, out[i].fVirt);
        EXPECT_EQ(out[i].fVirt & FVIRTKEY, FVIRTKEY);
        EXPECT_EQ(out[i].key, keys[i]);
        EXPECT_EQ(out[i].cmd, 100 + i);
        EXPECT_EQ(((unsigned char *)&out[i])[1], 0);
    }
    /* the 4th record is beyond the count: untouched */
    EXPECT_EQ(((unsigned char *)&out[3])[0], 0xcc);
    EXPECT_EQ(((unsigned char *)&out[3])[5], 0xcc);
    U(DestroyAcceleratorTable)(h);
}
TEST(gap7_accel_copy_clamps_to_table_count) {
    NtUserCopyAcceleratorTable_pfn f = G7N(NtUserCopyAcceleratorTable); REQUIRE(f);
    HACCEL h = make_accel(5, NULL); REQUIRE(h);
    ACCEL out[8];
    EXPECT_EQ(f(h, out, 8), 5);   /* min(requested, count) */
    EXPECT_EQ(f(h, out, 2), 2);
    EXPECT_EQ(f(h, out, 1), 1);
    EXPECT_EQ(f(h, out, 0), 0);
    U(DestroyAcceleratorTable)(h);
}
TEST(gap7_accel_copy_partial_does_not_touch_tail) {
    NtUserCopyAcceleratorTable_pfn f = G7N(NtUserCopyAcceleratorTable); REQUIRE(f);
    HACCEL h = make_accel(4, NULL); REQUIRE(h);
    unsigned char raw[6 * 4]; memset(raw, 0x77, sizeof raw);
    EXPECT_EQ(f(h, raw, 2), 2);
    for (int i = 12; i < 24; i++) EXPECT_EQ(raw[i], 0x77);
    U(DestroyAcceleratorTable)(h);
}
TEST(gap7_accel_invalid_handles_return_zero) {
    NtUserCopyAcceleratorTable_pfn f = G7N(NtUserCopyAcceleratorTable); REQUIRE(f);
    ACCEL out[2];
    /* HMValidateHandle(h, TYPE_ACCELTABLE=8) fails -> count forced to 0 for both query and copy */
    EXPECT_EQ(f(NULL, NULL, 0), 0);
    EXPECT_EQ(f((HACCEL)(ULONG_PTR)0x1234, NULL, 0), 0);
    EXPECT_EQ(f((HACCEL)(ULONG_PTR)0x1234, out, 2), 0);
    EXPECT_EQ(f((HACCEL)(ULONG_PTR)-1, out, 2), 0);
    EXPECT_EQ(f((HACCEL)(ULONG_PTR)0xfffffff0, NULL, 0), 0);
}
TEST(gap7_accel_wrong_object_type_rejected) {
    NtUserCopyAcceleratorTable_pfn f = G7N(NtUserCopyAcceleratorTable); REQUIRE(f);
    HMENU m = U(CreatePopupMenu)(); REQUIRE(m);
    HWND w = g7_window(WS_POPUP, 0);
    ACCEL out[2];
    EXPECT_EQ(f((HACCEL)m, NULL, 0), 0);
    EXPECT_EQ(f((HACCEL)m, out, 2), 0);
    if (w) { EXPECT_EQ(f((HACCEL)w, NULL, 0), 0); U(DestroyWindow)(w); }
    U(DestroyMenu)(m);
}
TEST(gap7_accel_destroyed_handle_rejected) {
    NtUserCopyAcceleratorTable_pfn f = G7N(NtUserCopyAcceleratorTable); REQUIRE(f);
    HACCEL h = make_accel(2, NULL); REQUIRE(h);
    EXPECT_EQ(f(h, NULL, 0), 2);
    CHECK(U(DestroyAcceleratorTable)(h));
    EXPECT_EQ(f(h, NULL, 0), 0);
    ACCEL out[2];
    EXPECT_EQ(f(h, out, 2), 0);
}
TEST(gap7_accel_user_handle_entry_type) {
    HACCEL h = make_accel(1, NULL); REQUIRE(h);
    struct k_user_entry e;
    if (t_is_wine() || !k_user_entry(h, &e)) { t_skip("no USER handle table"); U(DestroyAcceleratorTable)(h); return; }
    EXPECT_EQ(e.type, 8);     /* HMValidateHandle(h, 8) in NtUserCopyAcceleratorTable */
    EXPECT_EQ(e.uniq, ((ULONG_PTR)h >> 16) & 0xffff);
    U(DestroyAcceleratorTable)(h);
}
TEST(gap7_accel_route_matches_user32_copy) {
    NtUserCopyAcceleratorTable_pfn f = G7N(NtUserCopyAcceleratorTable); REQUIRE(f);
    HACCEL h = make_accel(3, NULL); REQUIRE(h);
    ACCEL a[3], b[3]; memset(a, 0, sizeof a); memset(b, 0, sizeof b);
    EXPECT_EQ(f(h, a, 3), 3);
    EXPECT_EQ(U(CopyAcceleratorTableW)(h, b, 3), 3);
    CHECK(memcmp(a, b, sizeof a) == 0);
    EXPECT_EQ(U(CopyAcceleratorTableW)(h, NULL, 0), 3);
    U(DestroyAcceleratorTable)(h);
}

/* ---- NtUserGetMenuItemRect / NtUserMenuItemFromPoint ------------------------------------------------------- */
static HMENU popup3(void)
{
    HMENU m = U(CreatePopupMenu)();
    if (!m) return NULL;
    U(AppendMenuW)(m, MF_STRING, 1, L"One");
    U(AppendMenuW)(m, MF_STRING, 2, L"Two");
    U(AppendMenuW)(m, MF_STRING, 3, L"Three");
    return m;
}
static ULONGLONG pk(POINT p) { return ((ULONGLONG)(DWORD)p.y << 32) | (DWORD)p.x; }
TEST(gap7_menurect_invalid_menu_returns_false) {
    NtUserGetMenuItemRect_pfn f = G7N(NtUserGetMenuItemRect); REQUIRE(f);
    RECT rc = { 9, 9, 9, 9 };
    /* ValidateHmenu fails -> FALSE, the rectangle is not written */
    EXPECT_EQ(f(NULL, NULL, 0, &rc), FALSE);
    EXPECT_EQ(f(NULL, (HMENU)(ULONG_PTR)0x1234, 0, &rc), FALSE);
    EXPECT_EQ(rc.left, 9); EXPECT_EQ(rc.bottom, 9);
}
TEST(gap7_menurect_invalid_hwnd_returns_false) {
    NtUserGetMenuItemRect_pfn f = G7N(NtUserGetMenuItemRect); REQUIRE(f);
    HMENU m = popup3(); REQUIRE(m);
    RECT rc = { 7, 7, 7, 7 };
    SetLastError(0);
    EXPECT_EQ(f((HWND)(ULONG_PTR)0x1234, m, 0, &rc), FALSE);   /* ValidateHwnd fails first */
    EXPECT_EQ(rc.left, 7);
    CHECK(GetLastError() == ERROR_INVALID_WINDOW_HANDLE);
    U(DestroyMenu)(m);
}
TEST(gap7_menurect_position_out_of_range_zeroes_rect) {
    NtUserGetMenuItemRect_pfn f = G7N(NtUserGetMenuItemRect); REQUIRE(f);
    HMENU m = popup3(); REQUIRE(m);
    static const UINT bad[] = { 3, 4, 100, 0x7fffffff, 0xffffffff };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        RECT rc; memset(&rc, 0x7f, sizeof rc);
        /* GetMenuItemRect clears the rect, then fails when pos >= cItems; the zeroed rect is still copied out */
        EXPECT_EQ(f(NULL, m, bad[i], &rc), FALSE);
        EXPECT_EQ(rc.left, 0); EXPECT_EQ(rc.top, 0); EXPECT_EQ(rc.right, 0); EXPECT_EQ(rc.bottom, 0);
    }
    U(DestroyMenu)(m);
}
TEST(gap7_menurect_unshown_popup_without_hwnd) {
    NtUserGetMenuItemRect_pfn f = G7N(NtUserGetMenuItemRect); REQUIRE(f);
    HMENU m = popup3(); REQUIRE(m);
    RECT rc = { 1, 1, 1, 1 };
    BOOL r = f(NULL, m, 1, &rc);
    t_note("unshown popup item rect: ret=%d (%ld,%ld,%ld,%ld)", r, rc.left, rc.top, rc.right, rc.bottom);
    U(DestroyMenu)(m);
}
static HWND menubar_window(HMENU *pm)
{
    HWND w = g7_window(WS_OVERLAPPED | WS_CAPTION, 0);
    /* the bar wraps into several rows when the window is narrow, so make it wide enough for one row */
    if (w) U(SetWindowPos)(w, NULL, 0, 0, 600, 200, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    HMENU m = U(CreateMenu)();
    if (!w || !m) { if (w) U(DestroyWindow)(w); if (m) U(DestroyMenu)(m); return NULL; }
    U(AppendMenuW)(m, MF_STRING, 1, L"File");
    U(AppendMenuW)(m, MF_STRING, 2, L"Edit");
    U(AppendMenuW)(m, MF_STRING, 3, L"View");
    U(SetMenu)(w, m);
    *pm = m;
    return w;
}
TEST(gap7_menurect_menubar_items_are_ordered_and_nonempty) {
    NtUserGetMenuItemRect_pfn f = G7N(NtUserGetMenuItemRect); REQUIRE(f);
    HMENU m; HWND w = menubar_window(&m); REQUIRE(w);
    RECT r[3];
    for (UINT i = 0; i < 3; i++) {
        memset(&r[i], 0, sizeof r[i]);
        EXPECT_EQ(f(w, m, i, &r[i]), TRUE);
        CHECKF(r[i].right > r[i].left && r[i].bottom > r[i].top, "item %u rect (%ld,%ld,%ld,%ld)", i, r[i].left, r[i].top, r[i].right, r[i].bottom);
    }
    CHECK(r[1].left >= r[0].right);   /* horizontal bar: items laid out left to right */
    CHECK(r[2].left >= r[1].right);
    EXPECT_EQ(r[0].top, r[1].top);
    EXPECT_EQ(r[1].top, r[2].top);
    U(DestroyWindow)(w);
}
TEST(gap7_menurect_matches_user32_route) {
    NtUserGetMenuItemRect_pfn f = G7N(NtUserGetMenuItemRect); REQUIRE(f);
    HMENU m; HWND w = menubar_window(&m); REQUIRE(w);
    RECT a, b; memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
    EXPECT_EQ(f(w, m, 1, &a), TRUE);
    EXPECT_EQ(U(GetMenuItemRect)(w, m, 1, &b), TRUE);
    CHECK(a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom);
    U(DestroyWindow)(w);
}
TEST(gap7_menurect_menubar_item_lies_inside_window_rect) {
    NtUserGetMenuItemRect_pfn f = G7N(NtUserGetMenuItemRect); REQUIRE(f);
    HMENU m; HWND w = menubar_window(&m); REQUIRE(w);
    RECT wr, ir;
    U(GetWindowRect)(w, &wr);
    EXPECT_EQ(f(w, m, 0, &ir), TRUE);
    CHECKF(ir.left >= wr.left && ir.right <= wr.right && ir.top >= wr.top && ir.bottom <= wr.bottom,
           "item (%ld,%ld,%ld,%ld) window (%ld,%ld,%ld,%ld)", ir.left, ir.top, ir.right, ir.bottom, wr.left, wr.top, wr.right, wr.bottom);
    U(DestroyWindow)(w);
}
TEST(gap7_menuitemfrompoint_invalid_arguments_return_minus_one) {
    NtUserMenuItemFromPoint_pfn f = G7N(NtUserMenuItemFromPoint); REQUIRE(f);
    POINT p = { 5, 5 };
    HMENU m = popup3(); REQUIRE(m);
    EXPECT_EQ(f(NULL, NULL, pk(p)), -1);                                /* ValidateHmenu fails */
    EXPECT_EQ(f(NULL, (HMENU)(ULONG_PTR)0x4321, pk(p)), -1);
    EXPECT_EQ(f((HWND)(ULONG_PTR)0x4321, m, pk(p)), -1);               /* ValidateHwnd fails first */
    U(DestroyMenu)(m);
}
TEST(gap7_menuitemfrompoint_hits_items_of_menubar) {
    NtUserMenuItemFromPoint_pfn f = G7N(NtUserMenuItemFromPoint); REQUIRE(f);
    NtUserGetMenuItemRect_pfn g = G7N(NtUserGetMenuItemRect); REQUIRE(g);
    HMENU m; HWND w = menubar_window(&m); REQUIRE(w);
    for (UINT i = 0; i < 3; i++) {
        RECT r;
        if (!g(w, m, i, &r)) { t_note("item %u rect unavailable", i); continue; }
        POINT c = { (r.left + r.right) / 2, (r.top + r.bottom) / 2 };
        EXPECT_EQ(f(w, m, pk(c)), (int)i);
        EXPECT_EQ(U(MenuItemFromPoint)(w, m, c), (int)i);
    }
    POINT far_ = { -30000, -30000 };
    EXPECT_EQ(f(w, m, pk(far_)), -1);
    U(DestroyWindow)(w);
}

/* ---- NtUserTransformPoint / NtUserTransformRect ------------------------------------------------------------- */
TEST(gap7_transformpoint_invalid_target_context_sets_57) {
    NtUserTransformPoint_pfn f = G7N(NtUserTransformPoint); REQUIRE(f);
    static const UINT bad[] = { 0, 1, 0x13, 0x77777, 0x12345, 0x55555555, 0xffffffff };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        POINT p = { 10, 20 };
        SetLastError(0);
        f(&p, bad[i], G7_DPI_PMV2, NULL);
        CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "to=0x%x err %lu", bad[i], GetLastError());
        EXPECT_EQ(p.x, 10); EXPECT_EQ(p.y, 20);
    }
}
TEST(gap7_transformpoint_invalid_source_context_sets_57) {
    NtUserTransformPoint_pfn f = G7N(NtUserTransformPoint); REQUIRE(f);
    static const UINT bad[] = { 0, 1, 0x77777, 0x12345, 0xffffffff };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        POINT p = { 33, 44 };
        SetLastError(0);
        f(&p, G7_DPI_PMV2, bad[i], NULL);
        CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "from=0x%x err %lu", bad[i], GetLastError());
        EXPECT_EQ(p.x, 33); EXPECT_EQ(p.y, 44);
    }
}
TEST(gap7_transformpoint_valid_contexts_leave_last_error) {
    NtUserTransformPoint_pfn f = G7N(NtUserTransformPoint); REQUIRE(f);
    static const UINT ok[] = { G7_DPI_SYSTEM, G7_DPI_PMV2, G7_DPI_UNAWARE, 0x40006010 };
    for (unsigned i = 0; i < 4; i++)
        for (unsigned j = 0; j < 4; j++) {
            POINT p = { 100, 100 };
            SetLastError(0x1234);
            f(&p, ok[i], ok[j], NULL);
            CHECKF(GetLastError() == 0x1234, "to=0x%x from=0x%x err %lu", ok[i], ok[j], GetLastError());
        }
}
TEST(gap7_transformpoint_same_context_is_identity) {
    NtUserTransformPoint_pfn f = G7N(NtUserTransformPoint); REQUIRE(f);
    POINT p = { 321, 123 };
    f(&p, G7_DPI_PMV2, G7_DPI_PMV2, NULL);        /* physical == logical for per-monitor-v2 */
    EXPECT_EQ(p.x, 321); EXPECT_EQ(p.y, 123);
    static const UINT ctx[] = { G7_DPI_SYSTEM, G7_DPI_UNAWARE };
    for (unsigned i = 0; i < 2; i++) {
        POINT q = { 321, 123 };
        f(&q, ctx[i], ctx[i], NULL);               /* logical->physical->logical: identity up to rounding */
        CHECKF(abs(q.x - 321) <= 1 && abs(q.y - 123) <= 1, "ctx 0x%x -> (%ld,%ld)", ctx[i], q.x, q.y);
    }
}
TEST(gap7_transformpoint_origin_is_fixed) {
    NtUserTransformPoint_pfn f = G7N(NtUserTransformPoint); REQUIRE(f);
    static const UINT ctx[] = { G7_DPI_SYSTEM, G7_DPI_PMV2, G7_DPI_UNAWARE };
    for (unsigned i = 0; i < 3; i++)
        for (unsigned j = 0; j < 3; j++) {
            POINT p = { 0, 0 };
            f(&p, ctx[i], ctx[j], NULL);
            CHECKF(abs(p.x) <= 1 && abs(p.y) <= 1, "to=0x%x from=0x%x -> (%ld,%ld)", ctx[i], ctx[j], p.x, p.y);
        }
}
TEST(gap7_transformpoint_cross_context_note_scale) {
    NtUserTransformPoint_pfn f = G7N(NtUserTransformPoint); REQUIRE(f);
    POINT p = { 960, 540 };
    f(&p, G7_DPI_PMV2, G7_DPI_UNAWARE, NULL);
    t_note("unaware (960,540) -> per-monitor-v2 physical (%ld,%ld)", p.x, p.y);
    CHECK(p.x >= 960 && p.y >= 540);   /* virtualisation only scales up */
}
TEST(gap7_transformpoint_primary_monitor_handle_accepted) {
    NtUserTransformPoint_pfn f = G7N(NtUserTransformPoint); REQUIRE(f);
    POINT o = { 0, 0 };
    HMONITOR mon = U(MonitorFromPoint)(o, MONITOR_DEFAULTTOPRIMARY);
    POINT p = { 50, 60 };
    SetLastError(0x1234);
    f(&p, G7_DPI_PMV2, G7_DPI_PMV2, mon);
    EXPECT_EQ(p.x, 50); EXPECT_EQ(p.y, 60);
    EXPECT_ERR(0x1234);
    p.x = 50; p.y = 60;
    f(&p, G7_DPI_PMV2, G7_DPI_PMV2, (HMONITOR)(ULONG_PTR)0x4444);   /* unknown monitor: falls back to the guessed one */
    EXPECT_EQ(p.x, 50); EXPECT_EQ(p.y, 60);
}
TEST(gap7_transformrect_invalid_source_context_sets_57) {
    NtUserTransformRect_pfn f = G7N(NtUserTransformRect); REQUIRE(f);
    /* valid first context, invalid second: UserSetLastError(0x57) before the rect is read */
    static const UINT bad[] = { 0, 1, 0x77777, 0x12345, 0xffffffff };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        RECT rc = { 1, 2, 30, 40 };
        SetLastError(0);
        f(&rc, G7_DPI_PMV2, bad[i], NULL);
        CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "from=0x%x err %lu", bad[i], GetLastError());
        EXPECT_EQ(rc.left, 1); EXPECT_EQ(rc.right, 30);
    }
}
TEST(gap7_transformrect_same_context_is_identity) {
    NtUserTransformRect_pfn f = G7N(NtUserTransformRect); REQUIRE(f);
    RECT rc = { 10, 20, 410, 320 };
    f(&rc, G7_DPI_PMV2, G7_DPI_PMV2, NULL);
    EXPECT_EQ(rc.left, 10); EXPECT_EQ(rc.top, 20); EXPECT_EQ(rc.right, 410); EXPECT_EQ(rc.bottom, 320);
    RECT r2 = { 10, 20, 410, 320 };
    f(&r2, G7_DPI_UNAWARE, G7_DPI_UNAWARE, NULL);
    CHECK(abs(r2.left - 10) <= 1 && abs(r2.top - 20) <= 1 && abs(r2.right - 410) <= 1 && abs(r2.bottom - 320) <= 1);
}
TEST(gap7_transformrect_keeps_orientation) {
    NtUserTransformRect_pfn f = G7N(NtUserTransformRect); REQUIRE(f);
    RECT rc = { 100, 100, 500, 400 };
    f(&rc, G7_DPI_PMV2, G7_DPI_UNAWARE, NULL);
    CHECK(rc.right > rc.left && rc.bottom > rc.top);
    t_note("unaware (100,100,500,400) -> (%ld,%ld,%ld,%ld)", rc.left, rc.top, rc.right, rc.bottom);
}
TEST(gap7_clienttoscreen_matches_window_origin) {
    HWND w = g7_window(WS_POPUP, 0); REQUIRE(w);
    RECT wr; U(GetWindowRect)(w, &wr);
    POINT p = { 0, 0 };
    EXPECT_EQ(U(ClientToScreen)(w, &p), TRUE);
    EXPECT_EQ(p.x, wr.left); EXPECT_EQ(p.y, wr.top);        /* WS_POPUP without border: client origin == window origin */
    POINT q = { 7, 9 };
    U(ClientToScreen)(w, &q);
    EXPECT_EQ(q.x, wr.left + 7); EXPECT_EQ(q.y, wr.top + 9);
    EXPECT_EQ(U(ScreenToClient)(w, &q), TRUE);
    EXPECT_EQ(q.x, 7); EXPECT_EQ(q.y, 9);
    U(DestroyWindow)(w);
}
TEST(gap7_clienttoscreen_invalid_hwnd_fails) {
    POINT p = { 1, 1 };
    SetLastError(0);
    EXPECT_EQ(U(ClientToScreen)((HWND)(ULONG_PTR)0x1234, &p), FALSE);
    EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
    EXPECT_EQ(p.x, 1);
}
TEST(gap7_getwindowinfo_client_rect_matches_getclientrect) {
    HWND w = g7_window(WS_OVERLAPPED | WS_CAPTION | WS_BORDER, 0); REQUIRE(w);
    WINDOWINFO wi; memset(&wi, 0, sizeof wi); wi.cbSize = sizeof wi;
    EXPECT_EQ(U(GetWindowInfo)(w, &wi), TRUE);
    RECT wr, cr; U(GetWindowRect)(w, &wr); U(GetClientRect)(w, &cr);
    EXPECT_EQ(wi.rcWindow.left, wr.left); EXPECT_EQ(wi.rcWindow.bottom, wr.bottom);
    EXPECT_EQ(wi.rcClient.right - wi.rcClient.left, cr.right);
    EXPECT_EQ(wi.rcClient.bottom - wi.rcClient.top, cr.bottom);
    EXPECT_EQ(wi.dwStyle, (DWORD)U(GetWindowLongW)(w, GWL_STYLE));
    EXPECT_EQ(wi.cxWindowBorders, (UINT)(wi.rcClient.left - wi.rcWindow.left));
    U(DestroyWindow)(w);
}
TEST(gap7_getwindowinfo_bad_arguments) {
    HWND w = g7_window(WS_POPUP, 0); REQUIRE(w);
    WINDOWINFO wi; memset(&wi, 0, sizeof wi);
    /* Windows 10 does not reject a wrong cbSize: it still fills the structure and returns TRUE, only leaving ERROR_INVALID_PARAMETER behind */
    SetLastError(0);
    wi.cbSize = 0;
    EXPECT_EQ(U(GetWindowInfo)(w, &wi), TRUE);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    wi.cbSize = sizeof wi + 1;
    SetLastError(0);
    EXPECT_EQ(U(GetWindowInfo)(w, &wi), TRUE);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    wi.cbSize = sizeof wi;
    SetLastError(0);
    EXPECT_EQ(U(GetWindowInfo)((HWND)(ULONG_PTR)0x1234, &wi), FALSE);
    EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
    U(DestroyWindow)(w);
}

/* ---- NtUserDrawIconEx --------------------------------------------------------------------------------------- */
static HICON make_icon(int cx, int cy)
{
    HDC sdc = U(GetDC)(NULL);
    HBITMAP color = U(CreateCompatibleBitmap)(sdc, cx, cy);
    HBITMAP mask = U(CreateBitmap)(cx, cy, 1, 1, NULL);
    U(ReleaseDC)(NULL, sdc);
    ICONINFO ii; memset(&ii, 0, sizeof ii);
    ii.fIcon = TRUE; ii.hbmColor = color; ii.hbmMask = mask;
    HICON h = U(CreateIconIndirect)(&ii);
    U(DeleteObject)(color); U(DeleteObject)(mask);
    return h;
}
TEST(gap7_drawiconex_invalid_icon_fails) {
    NtUserDrawIconEx_pfn f = G7N(NtUserDrawIconEx); REQUIRE(f);
    struct g7_dib d; REQUIRE(g7_dib_open(&d, 16, 16));
    /* HMValidateHandle(hIcon) fails -> returns 0 */
    EXPECT_EQ(f(d.dc, 0, 0, NULL, 16, 16, 0, NULL, DI_NORMAL, 0, NULL), FALSE);
    EXPECT_EQ(f(d.dc, 0, 0, (HICON)(ULONG_PTR)0x1234, 16, 16, 0, NULL, DI_NORMAL, 0, NULL), FALSE);
    g7_dib_close(&d);
}
TEST(gap7_drawiconex_metafile_mode_reports_icon_data) {
    NtUserDrawIconEx_pfn f = G7N(NtUserDrawIconEx); REQUIRE(f);
    HICON ic = make_icon(16, 16); REQUIRE(ic);
    unsigned char raw[0x28]; memset(raw, 0, sizeof raw);
    BOOL r = f(NULL, 0, 0, ic, 0, 0, 0, NULL, 0, TRUE, raw);
    EXPECT_EQ(r, TRUE);
    /* the kernel stores hbmMask/hbmColor at +0x00/+0x08 and the size at +0x18/+0x1c */
    HBITMAP mask = *(HBITMAP *)(raw + 0), color = *(HBITMAP *)(raw + 8);
    CHECK(mask != NULL);
    CHECK(color != NULL);
    if (mask)  EXPECT_EQ(U(GetObjectType)(mask), OBJ_BITMAP);
    if (color) EXPECT_EQ(U(GetObjectType)(color), OBJ_BITMAP);
    int cx = *(int *)(raw + 0x18), cy = *(int *)(raw + 0x1c);
    CHECKF(cx > 0 && cy > 0 && cx <= 256, "icon size %dx%d", cx, cy);
    t_note("icon data: %dx%d", cx, cy);
    U(DestroyIcon)(ic);
}
TEST(gap7_drawiconex_draws_into_memory_dc) {
    HICON ic = make_icon(16, 16); REQUIRE(ic);
    struct g7_dib d; REQUIRE(g7_dib_open(&d, 32, 32));
    for (int i = 0; i < 32 * 32; i++) d.bits[i] = 0xffffffff;
    HBRUSH blk = (HBRUSH)U(GetStockObject)(BLACK_BRUSH);
    /* A fresh color bitmap is undefined; paint it black through a DC so DI_IMAGE gives a deterministic result. */
    ICONINFO ii; U(GetIconInfo)(ic, &ii);
    HDC tmp = U(CreateCompatibleDC)(NULL);
    HGDIOBJ old = U(SelectObject)(tmp, ii.hbmColor);
    RECT all = { 0, 0, 16, 16 };
    U(FillRect)(tmp, &all, blk);
    U(SelectObject)(tmp, old); U(DeleteDC)(tmp);
    U(DeleteObject)(ii.hbmColor); U(DeleteObject)(ii.hbmMask);
    U(DestroyIcon)(ic);
    ic = make_icon(16, 16);
    EXPECT_EQ(U(DrawIconEx)(d.dc, 4, 4, ic, 16, 16, 0, NULL, DI_NORMAL), TRUE);
    U(GdiFlush)();
    /* the icon occupies (4..20); pixels well outside stay white */
    EXPECT_EQ(d.bits[0] & 0xffffff, 0xffffff);
    EXPECT_EQ(d.bits[31 * 32 + 31] & 0xffffff, 0xffffff);
    g7_dib_close(&d);
    U(DestroyIcon)(ic);
}
TEST(gap7_drawiconex_reserved_flag_bits_rejected_in_user32) {
    HICON ic = make_icon(8, 8); REQUIRE(ic);
    struct g7_dib d; REQUIRE(g7_dib_open(&d, 8, 8));
    static const UINT bad[] = { 0x20, 0x40, 0x100, 0x8000, 0x80000000 };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        SetLastError(0);
        EXPECT_EQ(U(DrawIconEx)(d.dc, 0, 0, ic, 8, 8, 0, NULL, bad[i]), FALSE);   /* (diFlags & ~0x1f) != 0 -> 0x57 */
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
    g7_dib_close(&d);
    U(DestroyIcon)(ic);
}
TEST(gap7_drawiconex_invalid_dc_fails) {
    HICON ic = make_icon(8, 8); REQUIRE(ic);
    EXPECT_EQ(U(DrawIconEx)(NULL, 0, 0, ic, 8, 8, 0, NULL, DI_NORMAL), FALSE);
    EXPECT_EQ(U(DrawIconEx)((HDC)(ULONG_PTR)0x1234, 0, 0, ic, 8, 8, 0, NULL, DI_NORMAL), FALSE);
    U(DestroyIcon)(ic);
}

/* ---- NtUserUpdateLayeredWindow ------------------------------------------------------------------------------ */
TEST(gap7_ulw_invalid_hwnd_fails) {
    NtUserUpdateLayeredWindow_pfn f = G7N(NtUserUpdateLayeredWindow); REQUIRE(f);
    EXPECT_EQ(f(NULL, NULL, NULL, NULL, NULL, NULL, 0, NULL, 0, NULL), FALSE);
    EXPECT_EQ(f((HWND)(ULONG_PTR)0x1234, NULL, NULL, NULL, NULL, NULL, 0, NULL, 0, NULL), FALSE);
}
TEST(gap7_ulw_reserved_flag_bits_set_57) {
    NtUserUpdateLayeredWindow_pfn f = G7N(NtUserUpdateLayeredWindow); REQUIRE(f);
    HWND w = g7_window(WS_POPUP, WS_EX_LAYERED); REQUIRE(w);
    static const DWORD bad[] = { 0x10, 0x40, 0x80, 0x100, 0x10000, 0x80000000, 0xffffffd0 };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        SetLastError(0);
        EXPECT_EQ(f(w, NULL, NULL, NULL, NULL, NULL, 0, NULL, bad[i], NULL), FALSE);   /* (flags & 0xffffffd0) != 0 */
        CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "flags 0x%lx err %lu", bad[i], GetLastError());
    }
    U(DestroyWindow)(w);
}
TEST(gap7_ulw_negative_size_set_57) {
    NtUserUpdateLayeredWindow_pfn f = G7N(NtUserUpdateLayeredWindow); REQUIRE(f);
    HWND w = g7_window(WS_POPUP, WS_EX_LAYERED); REQUIRE(w);
    SIZE neg[] = { { -1, 10 }, { 10, -1 }, { -5, -5 }, { (LONG)0x80000000, 4 } };
    for (unsigned i = 0; i < 4; i++) {
        SetLastError(0);
        EXPECT_EQ(f(w, NULL, NULL, &neg[i], NULL, NULL, 0, NULL, 0, NULL), FALSE);   /* cx/cy sign bit -> 0x57 */
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
    U(DestroyWindow)(w);
}
TEST(gap7_ulw_negative_dirty_origin_set_57) {
    NtUserUpdateLayeredWindow_pfn f = G7N(NtUserUpdateLayeredWindow); REQUIRE(f);
    HWND w = g7_window(WS_POPUP, WS_EX_LAYERED); REQUIRE(w);
    RECT bad[] = { { -1, 0, 4, 4 }, { 0, -1, 4, 4 }, { -9, -9, 1, 1 } };
    for (unsigned i = 0; i < 3; i++) {
        SetLastError(0);
        EXPECT_EQ(f(w, NULL, NULL, NULL, NULL, NULL, 0, NULL, 0, &bad[i]), FALSE);   /* dirty.left/top must be >= 0 */
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
    U(DestroyWindow)(w);
}
TEST(gap7_ulw_not_layered_window_fails) {
    NtUserUpdateLayeredWindow_pfn f = G7N(NtUserUpdateLayeredWindow); REQUIRE(f);
    HWND w = g7_window(WS_POPUP, 0); REQUIRE(w);
    struct g7_dib d; REQUIRE(g7_dib_open(&d, 8, 8));
    POINT src = { 0, 0 }; SIZE sz = { 8, 8 };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    EXPECT_EQ(f(w, NULL, NULL, &sz, d.dc, &src, 0, &bf, ULW_ALPHA, NULL), FALSE);
    g7_dib_close(&d);
    U(DestroyWindow)(w);
}
TEST(gap7_ulw_layered_window_alpha_resizes_window) {
    NtUserUpdateLayeredWindow_pfn f = G7N(NtUserUpdateLayeredWindow); REQUIRE(f);
    HWND w = g7_window(WS_POPUP, WS_EX_LAYERED); REQUIRE(w);
    struct g7_dib d; REQUIRE(g7_dib_open(&d, 8, 8));
    for (int i = 0; i < 64; i++) d.bits[i] = 0xff00ff00;
    POINT src = { 0, 0 }, dst = { -32000, -32000 }; SIZE sz = { 8, 8 };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    BOOL r = f(w, NULL, &dst, &sz, d.dc, &src, 0, &bf, ULW_ALPHA, NULL);
    if (!r) { t_note("ULW failed err %lu (no layered support?)", GetLastError()); }
    else {
        RECT wr; U(GetWindowRect)(w, &wr);
        EXPECT_EQ(wr.right - wr.left, 8);      /* psize becomes the window size */
        EXPECT_EQ(wr.bottom - wr.top, 8);
        EXPECT_EQ(wr.left, -32000);
        EXPECT_EQ(wr.top, -32000);
    }
    g7_dib_close(&d);
    U(DestroyWindow)(w);
}
TEST(gap7_ulw_user32_route_same_result) {
    HWND w = g7_window(WS_POPUP, WS_EX_LAYERED); REQUIRE(w);
    struct g7_dib d; REQUIRE(g7_dib_open(&d, 4, 4));
    POINT src = { 0, 0 }; SIZE sz = { 4, 4 };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 128, 0 };
    BOOL a = U(UpdateLayeredWindow)(w, NULL, NULL, &sz, d.dc, &src, 0, &bf, ULW_ALPHA);
    NtUserUpdateLayeredWindow_pfn f = G7N(NtUserUpdateLayeredWindow);
    if (f) { BOOL b = f(w, NULL, NULL, &sz, d.dc, &src, 0, &bf, ULW_ALPHA, NULL); EXPECT_EQ(a, b); }
    g7_dib_close(&d);
    U(DestroyWindow)(w);
}

/* ---- NtUserQueryDisplayConfig ------------------------------------------------------------------------------- */
/* win32kbase: flags & 0xffffffc8 != 0, or base (flags&7) outside {1,2,4}: the call fails before reading anything. */
TEST(gap7_qdc_invalid_flags_fail) {
    NtUserQueryDisplayConfig_pfn f = G7N(NtUserQueryDisplayConfig); REQUIRE(f);
    static const UINT bad[] = { 0, 3, 5, 6, 7, 8, 0x40, 0x80, 0x100, 0x80000000, 0xffffffff };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        UINT n = 4; static char buf[4 * 200];
        LONG st = f(bad[i], &n, buf, NULL, NULL);
        CHECKF(st < 0, "flags 0x%x status 0x%lx", bad[i], (unsigned long)st);
    }
}
TEST(gap7_qdc_zero_path_count_invalid_parameter) {
    NtUserQueryDisplayConfig_pfn f = G7N(NtUserQueryDisplayConfig); REQUIRE(f);
    UINT n = 0; static char buf[200];
    LONG st = f(QDC_ONLY_ACTIVE_PATHS, &n, buf, NULL, NULL);
    EXPECT_EQ((ULONG)st, 0xC000000Du);   /* STATUS_INVALID_PARAMETER when *pNumPaths == 0 */
}
TEST(gap7_qdc_topology_pointer_requires_database_flag) {
    NtUserQueryDisplayConfig_pfn f = G7N(NtUserQueryDisplayConfig); REQUIRE(f);
    UINT n = 4; static char buf[4 * 200]; DWORD topo = 0;
    LONG st = f(QDC_ONLY_ACTIVE_PATHS, &n, buf, &topo, NULL);   /* flag 4 clear + topology pointer -> invalid */
    EXPECT_EQ((ULONG)st, 0xC000000Du);
    n = 4;
    st = f(QDC_ALL_PATHS, &n, buf, &topo, NULL);
    EXPECT_EQ((ULONG)st, 0xC000000Du);
}
TEST(gap7_qdc_active_paths_success_in_interactive_session) {
    NtUserQueryDisplayConfig_pfn f = G7N(NtUserQueryDisplayConfig); REQUIRE(f);
    static char buf[16 * 200]; memset(buf, 0, sizeof buf);
    UINT n = 16;
    LONG st = f(QDC_ONLY_ACTIVE_PATHS, &n, buf, NULL, NULL);
    if (t_is_wine()) { t_note("wine status 0x%lx n=%u", (unsigned long)st, n); return; }
    CHECKF(st >= 0, "status 0x%lx", (unsigned long)st);
    CHECKF(n >= 1 && n <= 16, "active paths %u", n);
}
TEST(gap7_qdc_all_paths_not_fewer_than_active_paths) {
    NtUserQueryDisplayConfig_pfn f = G7N(NtUserQueryDisplayConfig); REQUIRE(f);
    static char b1[64 * 200], b2[64 * 200];
    UINT na = 64, nall = 64;
    LONG s1 = f(QDC_ONLY_ACTIVE_PATHS, &na, b1, NULL, NULL);
    LONG s2 = f(QDC_ALL_PATHS, &nall, b2, NULL, NULL);
    if (s1 < 0 || s2 < 0) { t_note("statuses 0x%lx 0x%lx", (unsigned long)s1, (unsigned long)s2); return; }
    CHECKF(nall >= na, "all %u active %u", nall, na);
}
TEST(gap7_qdc_database_current_returns_topology_id) {
    NtUserQueryDisplayConfig_pfn f = G7N(NtUserQueryDisplayConfig); REQUIRE(f);
    static char buf[64 * 200]; UINT n = 64; DWORD topo = 0xfefefefe;
    LONG st = f(QDC_DATABASE_CURRENT, &n, buf, &topo, NULL);
    if (st < 0) { t_note("database current status 0x%lx", (unsigned long)st); return; }
    /* DISPLAYCONFIG_TOPOLOGY_ID: INTERNAL 1, CLONE 2, EXTEND 4, EXTERNAL 8 */
    CHECKF(topo == 1 || topo == 2 || topo == 4 || topo == 8, "topology 0x%lx", topo);
}
TEST(gap7_qdc_user32_route_reports_success) {
    UINT np = 0, nm = 0;
    LONG e = U(GetDisplayConfigBufferSizes)(QDC_ONLY_ACTIVE_PATHS, &np, &nm);
    if (e != ERROR_SUCCESS) { t_note("GetDisplayConfigBufferSizes %ld", e); return; }
    CHECK(np >= 1);
    DISPLAYCONFIG_PATH_INFO *paths = (DISPLAYCONFIG_PATH_INFO *)calloc(np + 1, sizeof *paths);
    DISPLAYCONFIG_MODE_INFO *modes = (DISPLAYCONFIG_MODE_INFO *)calloc(nm + 1, sizeof *modes);
    UINT np2 = np, nm2 = nm;
    EXPECT_EQ(U(QueryDisplayConfig)(QDC_ONLY_ACTIVE_PATHS, &np2, paths, &nm2, modes, NULL), ERROR_SUCCESS);
    CHECK(np2 >= 1 && np2 <= np);
    CHECK(nm2 <= nm);
    free(paths); free(modes);
}
TEST(gap7_qdc_user32_invalid_flags_fail) {
    UINT np = 4, nm = 8;
    DISPLAYCONFIG_PATH_INFO paths[4]; DISPLAYCONFIG_MODE_INFO modes[8];
    LONG e = U(QueryDisplayConfig)(0x40, &np, paths, &nm, modes, NULL);
    CHECK(e != ERROR_SUCCESS);
    e = U(QueryDisplayConfig)(0, &np, paths, &nm, modes, NULL);
    CHECK(e != ERROR_SUCCESS);
}
