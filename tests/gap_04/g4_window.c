/* Window-validating syscalls with no other direct caller: NtUserDragDetect, DrawAnimatedRects, DrawCaption,
 * GetListBoxInfo, EnableNonClientDpiScaling, EndMenu, PaintDesktop, CalculatePopupWindowPosition and the
 * Logical/Physical point pair. */
#include "gap4.h"

/* Handles win32kbase!ValidateHwndEx rejects (index out of the entry table, free entry or uniq mismatch). */
static const ULONG_PTR k_bad[] = { 0, 0xFFFF, 0x1FFF0, 0x7FFFFFFF, 0xFFFFFFFF };
#define NBAD (sizeof k_bad / sizeof k_bad[0])
#define BAD(i) ((HWND)k_bad[i])

static HWND dead_window(void) {
    HWND h = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 200, 100, NULL);
    if (h) g4_destroy(h);
    return h;
}

/* ---- NtUserDragDetect (window validation, then the drag test) ------------------- */
TEST(gap4_dragdetect_invalid_hwnd_fails_1400) {
    G4REQ(NtUserDragDetect);
    POINT p = { 5, 5 };
    for (unsigned i = 0; i < NBAD; i++) {
        SET_ERR(0);
        BOOL r = UN(NtUserDragDetect)(BAD(i), p);
        CHECKF(r == 0, "hwnd %p ret %d", BAD(i), r);
        EXPECT_ERR(G4_ERR_INVALID_WINDOW);
    }
}
TEST(gap4_dragdetect_destroyed_hwnd_fails_1400) {
    G4REQ(NtUserDragDetect);
    HWND h = dead_window(); if (!h) { t_skip("no window"); return; }
    SET_ERR(0);
    EXPECT_EQ(UN(NtUserDragDetect)(h, (POINT){ 1, 1 }), 0);
    EXPECT_ERR(G4_ERR_INVALID_WINDOW);
}
/* the drag test returns 0 at once when _GetKeyState(VK_LBUTTON) has no 0x8000 bit; no capture is taken. */
TEST(gap4_dragdetect_button_up_returns_false_without_capture) {
    G4REQ(NtUserDragDetect); REQUIRE(U(GetKeyState)); REQUIRE(U(GetCapture));
    HWND h = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 200, 100, NULL);
    if (!h) { t_skip("no window"); return; }
    if (U(GetKeyState)(VK_LBUTTON) & 0x8000) { t_skip("left button is down"); g4_destroy(h); return; }
    HWND cap0 = U(GetCapture)();
    EXPECT_EQ(UN(NtUserDragDetect)(h, (POINT){ 150, 150 }), 0);
    EXPECT_EQ(UN(NtUserDragDetect)(h, (POINT){ -5, -5 }), 0);
    CHECK(U(GetCapture)() == cap0);
    g4_destroy(h);
}
TEST(gap4_dragdetect_matches_user32_thunk) {
    G4REQ(NtUserDragDetect); REQUIRE(U(DragDetect)); REQUIRE(U(GetKeyState));
    HWND h = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 200, 100, NULL);
    if (!h) { t_skip("no window"); return; }
    if (U(GetKeyState)(VK_LBUTTON) & 0x8000) { t_skip("left button is down"); g4_destroy(h); return; }
    POINT p = { 120, 120 };
    CHECK(U(DragDetect)(h, p) == UN(NtUserDragDetect)(h, p));
    g4_destroy(h);
}

/* ---- NtUserDrawAnimatedRects ------------------------------------------------------------------- */
/* DrawAnimatedRects returns 1 at once for every idAni other than IDANI_CAPTION (3): nothing is drawn. */
TEST(gap4_drawanimrects_non_caption_ids_return_true) {
    G4REQ(NtUserDrawAnimatedRects);
    HWND h = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 200, 100, NULL);
    if (!h) { t_skip("no window"); return; }
    RECT a = { 0, 0, 10, 10 }, b = { 20, 20, 40, 40 };
    static const int ids[] = { 0, 1, 2, 4, 5, -1, 0x7fffffff, 0x100 };
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        BOOL r = UN(NtUserDrawAnimatedRects)(h, ids[i], &a, &b);
        CHECKF(r == 1, "idAni %d ret %d", ids[i], r);
        r = UN(NtUserDrawAnimatedRects)(NULL, ids[i], &a, &b);   /* NULL hwnd skips validation */
        CHECKF(r == 1, "NULL hwnd idAni %d ret %d", ids[i], r);
    }
    g4_destroy(h);
}
/* idAni == 3 with a NULL window: the early return of DrawAnimatedRects gives FALSE. */
TEST(gap4_drawanimrects_caption_null_hwnd_returns_false) {
    G4REQ(NtUserDrawAnimatedRects);
    RECT a = { 0, 0, 10, 10 }, b = { 20, 20, 40, 40 };
    EXPECT_EQ(UN(NtUserDrawAnimatedRects)(NULL, 3, &a, &b), 0);
}
TEST(gap4_drawanimrects_invalid_hwnd_fails_1400) {
    G4REQ(NtUserDrawAnimatedRects);
    RECT a = { 0, 0, 10, 10 }, b = { 20, 20, 40, 40 };
    for (unsigned i = 1; i < NBAD; i++) {   /* index 0 is NULL, which is accepted */
        SET_ERR(0);
        BOOL r = UN(NtUserDrawAnimatedRects)(BAD(i), 3, &a, &b);
        CHECKF(r == 0, "hwnd %p ret %d", BAD(i), r);
        EXPECT_ERR(G4_ERR_INVALID_WINDOW);
    }
}
/* The desktop window short-circuits DrawAnimatedRects (argument 1 == _GetDesktopWindow()) with FALSE. */
TEST(gap4_drawanimrects_caption_desktop_window_returns_false) {
    G4REQ(NtUserDrawAnimatedRects); REQUIRE(U(GetDesktopWindow));
    RECT a = { 0, 0, 10, 10 }, b = { 20, 20, 40, 40 };
    EXPECT_EQ(UN(NtUserDrawAnimatedRects)(U(GetDesktopWindow)(), 3, &a, &b), 0);
}

/* ---- NtUserDrawCaption ------------------------------------------------------------------------- */
TEST(gap4_drawcaption_invalid_hwnd_fails_1400) {
    G4REQ(NtUserDrawCaption);
    HBITMAP b, o; HDC dc = g4_memdc(&b, &o, NULL);
    if (!dc) { t_skip("no memory dc"); return; }
    RECT rc = { 0, 0, 30, 20 };
    for (unsigned i = 0; i < NBAD; i++) {
        SET_ERR(0);
        BOOL r = UN(NtUserDrawCaption)(BAD(i), dc, &rc, DC_TEXT);
        CHECKF(r == 0, "hwnd %p ret %d", BAD(i), r);
        EXPECT_ERR(G4_ERR_INVALID_WINDOW);
    }
    g4_memdc_free(dc, b, o);
}
/* DrawCaptionTemp returns 0 before touching the DC when right <= left. */
TEST(gap4_drawcaption_empty_rect_returns_false_and_leaves_bits) {
    G4REQ(NtUserDrawCaption);
    HWND h = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 200, 100, NULL);
    HBITMAP b, o; void *bits = NULL; HDC dc = g4_memdc(&b, &o, &bits);
    if (!h || !dc) { t_skip("no fixtures"); g4_destroy(h); g4_memdc_free(dc, b, o); return; }
    memset(bits, 0x5a, 32 * 32 * 4);
    RECT same = { 4, 4, 4, 20 }, rev = { 20, 4, 10, 20 };
    EXPECT_EQ(UN(NtUserDrawCaption)(h, dc, &same, DC_TEXT | DC_ACTIVE), 0);
    EXPECT_EQ(UN(NtUserDrawCaption)(h, dc, &rev, DC_TEXT | DC_ACTIVE), 0);
    unsigned char *p = bits; int same_bytes = 1;
    for (int i = 0; i < 32 * 32 * 4; i++) if (p[i] != 0x5a) { same_bytes = 0; break; }
    CHECK(same_bytes);
    g4_destroy(h); g4_memdc_free(dc, b, o);
}
/* Valid window, valid memory DC: draws the caption bar into the DIB section. Result and pixel effect are
 * theme dependent, so they are recorded rather than asserted. */
TEST(gap4_drawcaption_valid_draws_into_memory_dc) {
    G4REQ(NtUserDrawCaption);
    HWND h = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 200, 100, NULL);
    HBITMAP b, o; void *bits = NULL; HDC dc = g4_memdc(&b, &o, &bits);
    if (!h || !dc) { t_skip("no fixtures"); g4_destroy(h); g4_memdc_free(dc, b, o); return; }
    memset(bits, 0, 32 * 32 * 4);
    RECT rc = { 0, 0, 32, 16 };
    SET_ERR(0);
    BOOL r = UN(NtUserDrawCaption)(h, dc, &rc, DC_TEXT | DC_ACTIVE | DC_GRADIENT);
    unsigned char *p = bits; int touched = 0;
    for (int i = 0; i < 32 * 16 * 4; i++) if (p[i]) { touched = 1; break; }
    t_note("DrawCaption ret=%d gle=%lu pixels_touched=%d", r, GetLastError(), touched);
    CHECK(r == 0 || r == 1);
    g4_destroy(h); g4_memdc_free(dc, b, o);
}

/* ---- NtUserGetListBoxInfo ---------------------------------------------------------------------- */
TEST(gap4_listboxinfo_invalid_hwnd_fails_1400) {
    G4REQ(NtUserGetListBoxInfo);
    for (unsigned i = 0; i < NBAD; i++) {
        SET_ERR(0);
        DWORD r = UN(NtUserGetListBoxInfo)(BAD(i));
        CHECKF(r == 0, "hwnd %p ret %lu", BAD(i), r);
        EXPECT_ERR(G4_ERR_INVALID_WINDOW);
    }
}
/* A window that is not a listbox: GetListBoxInfo sends LB_GETLISTBOXINFO (0x1b2) and DefWindowProc answers 0. */
TEST(gap4_listboxinfo_non_listbox_window_returns_zero) {
    G4REQ(NtUserGetListBoxInfo);
    HWND h = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 200, 100, NULL);
    if (!h) { t_skip("no window"); return; }
    EXPECT_EQ(UN(NtUserGetListBoxInfo)(h), 0);
    g4_destroy(h);
}
TEST(gap4_listboxinfo_real_listbox_matches_lb_getlistboxinfo) {
    G4REQ(NtUserGetListBoxInfo); REQUIRE(U(CreateWindowExW)); REQUIRE(U(SendMessageW));
    HWND par = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 300, 300, NULL);
    if (!par) { t_skip("no window"); return; }
    HWND lb = U(CreateWindowExW)(0, L"LISTBOX", L"", WS_CHILD | WS_VSCROLL, 0, 0, 120, 150, par, (HMENU)1,
                                 GetModuleHandleW(NULL), NULL);
    if (!lb) { t_skip("no listbox"); g4_destroy(par); return; }
    for (int i = 0; i < 20; i++) U(SendMessageW)(lb, LB_ADDSTRING, 0, (LPARAM)L"item");
    /* The kernel reads the listbox's items-per-column field directly; the message handler returns the same field. */
    DWORD k = UN(NtUserGetListBoxInfo)(lb);
    LRESULT m = U(SendMessageW)(lb, 0x1b2, 0, 0);
    CHECKF((LRESULT)k == m, "kernel %lu message %lld", k, (long long)m);
    CHECK(k > 0);
    g4_destroy(par);
}
TEST(gap4_listboxinfo_child_listbox_destroyed_fails) {
    G4REQ(NtUserGetListBoxInfo); REQUIRE(U(CreateWindowExW));
    HWND par = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 300, 300, NULL);
    if (!par) { t_skip("no window"); return; }
    HWND lb = U(CreateWindowExW)(0, L"LISTBOX", L"", WS_CHILD, 0, 0, 100, 100, par, (HMENU)1, GetModuleHandleW(NULL), NULL);
    if (!lb) { t_skip("no listbox"); g4_destroy(par); return; }
    g4_destroy(lb);
    SET_ERR(0);
    EXPECT_EQ(UN(NtUserGetListBoxInfo)(lb), 0);
    EXPECT_ERR(G4_ERR_INVALID_WINDOW);
    g4_destroy(par);
}

/* ---- NtUserEnableNonClientDpiScaling ----------------------------------------------------------- */
TEST(gap4_ncdpi_invalid_hwnd_fails_1400) {
    G4REQ(NtUserEnableNonClientDpiScaling);
    for (unsigned i = 0; i < NBAD; i++) {
        SET_ERR(0);
        BOOL r = UN(NtUserEnableNonClientDpiScaling)(BAD(i));
        CHECKF(r == 0, "hwnd %p ret %d", BAD(i), r);
        EXPECT_ERR(G4_ERR_INVALID_WINDOW);
    }
}
/* EnableNonClientDpiScaling needs the window's DPI context low nibble == 2 (per-monitor v1); this exe has no
 * manifest so its windows are DPI unaware. */
TEST(gap4_ncdpi_unaware_window_returns_false) {
    G4REQ(NtUserEnableNonClientDpiScaling);
    HWND h = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 200, 100, NULL);
    if (!h) { t_skip("no window"); return; }
    if (U(GetThreadDpiAwarenessContext) && U(GetAwarenessFromDpiAwarenessContext) &&
        U(GetAwarenessFromDpiAwarenessContext)(U(GetThreadDpiAwarenessContext)()) != DPI_AWARENESS_UNAWARE) {
        t_skip("process is not DPI unaware"); g4_destroy(h); return;
    }
    EXPECT_EQ(UN(NtUserEnableNonClientDpiScaling)(h), 0);
    g4_destroy(h);
}
TEST(gap4_ncdpi_unaware_child_window_returns_false) {
    G4REQ(NtUserEnableNonClientDpiScaling);
    HWND par = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 200, 100, NULL);
    HWND ch = par ? g4_window(WS_CHILD, 0, 0, 0, 50, 50, par) : NULL;
    if (!ch) { t_skip("no window"); g4_destroy(par); return; }
    if (U(GetThreadDpiAwarenessContext) && U(GetAwarenessFromDpiAwarenessContext) &&
        U(GetAwarenessFromDpiAwarenessContext)(U(GetThreadDpiAwarenessContext)()) != DPI_AWARENESS_UNAWARE) {
        t_skip("process is not DPI unaware"); g4_destroy(par); return;
    }
    EXPECT_EQ(UN(NtUserEnableNonClientDpiScaling)(ch), 0);
    g4_destroy(par);
}
/* Per-monitor-v1 process (child: process-wide, irreversible): a top-level window is accepted, the call is
 * idempotent, and a child window (top-level windows only) is refused. */
CHILD(gap4_ncdpi_permonitor) {
    (void)argc; (void)argv;
    int bad = 0;
#define CC(c) do { if (!(c)) { printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); bad++; } } while (0)
    typedef BOOL (WINAPI *spdac_t)(DPI_AWARENESS_CONTEXT);
    spdac_t spdac = (spdac_t)GetProcAddress(LoadLibraryA("user32.dll"), "SetProcessDpiAwarenessContext");
    if (!spdac || !UN(NtUserEnableNonClientDpiScaling)) { printf("NOTE prerequisites missing\n"); return 0; }
    if (!spdac(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE)) { printf("NOTE SetProcessDpiAwarenessContext gle=%lu\n", GetLastError()); return 0; }
    HWND top = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 200, 100, NULL);
    HWND ch = top ? g4_window(WS_CHILD, 0, 0, 0, 40, 40, top) : NULL;
    if (!top || !ch) { printf("NOTE no windows\n"); return 0; }
    BOOL r1 = UN(NtUserEnableNonClientDpiScaling)(top);
    BOOL r2 = UN(NtUserEnableNonClientDpiScaling)(top);
    printf("NOTE top-level ret=%d second ret=%d\n", r1, r2);
    CC(r1 == r2);
    CC(UN(NtUserEnableNonClientDpiScaling)(ch) == 0);
    g4_destroy(top);
    return bad;
#undef CC
}
TEST(gap4_ncdpi_per_monitor_process_child) {
    struct k_child_result r = k_child_run("gap4_ncdpi_permonitor", "", 30000);
    CHECK(r.exit_code == 0);
}

/* ---- NtUserEndMenu ----------------------------------------------------------------------------- */
/* win32kfull!NtUserEndMenu always returns 1; with no menu state on the thread (+0x260 == 0) it does nothing else. */
TEST(gap4_endmenu_without_menu_returns_true) {
    G4REQ(NtUserEndMenu);
    HWND h = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 200, 100, NULL);   /* makes this a GUI thread */
    if (!h) { t_skip("no window"); return; }
    for (int i = 0; i < 3; i++) EXPECT_EQ(UN(NtUserEndMenu)(), 1);
    g4_destroy(h);
}
TEST(gap4_endmenu_does_not_post_to_window_queue) {
    G4REQ(NtUserEndMenu); REQUIRE(U(PeekMessageW));
    HWND h = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 200, 100, NULL);
    if (!h) { t_skip("no window"); return; }
    MSG m; while (U(PeekMessageW)(&m, NULL, 0, 0, PM_REMOVE)) {}
    UN(NtUserEndMenu)();
    CHECK(!U(PeekMessageW)(&m, NULL, 499, 499, PM_NOREMOVE));   /* the kernel posts message 499 only when a menu window exists */
    g4_destroy(h);
}
TEST(gap4_endmenu_matches_user32_endmenu) {
    G4REQ(NtUserEndMenu); REQUIRE(U(EndMenu));
    HWND h = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 200, 100, NULL);
    if (!h) { t_skip("no window"); return; }
    EXPECT_EQ(U(EndMenu)(), UN(NtUserEndMenu)());
    g4_destroy(h);
}

/* ---- NtUserPaintDesktop ------------------------------------------------------------------------ */
/* the desktop paint returns 0 (no last error) when the DC origin query fails for an invalid HDC. */
TEST(gap4_paintdesktop_invalid_dc_returns_false) {
    G4REQ(NtUserPaintDesktop);
    HWND h = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 200, 100, NULL);   /* thread needs a desktop */
    if (!h) { t_skip("no window"); return; }
    static const ULONG_PTR bad[] = { 0, 1, 0x1234, 0xdeadbeef };
    for (unsigned i = 0; i < 4; i++) {
        BOOL r = UN(NtUserPaintDesktop)((HDC)bad[i]);
        CHECKF(r == 0, "hdc %p ret %d", (void *)bad[i], r);
    }
    g4_destroy(h);
}
/* The syscall moves the DC origin and clip while painting and restores both. */
TEST(gap4_paintdesktop_memory_dc_restores_origin_and_clip) {
    G4REQ(NtUserPaintDesktop); REQUIRE(U(GetDCOrgEx)); REQUIRE(U(GetClipBox));
    HWND h = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 200, 100, NULL);
    HBITMAP b, o; void *bits = NULL; HDC dc = g4_memdc(&b, &o, &bits);
    if (!h || !dc) { t_skip("no fixtures"); g4_destroy(h); g4_memdc_free(dc, b, o); return; }
    POINT o0, o1; RECT c0, c1;
    CHECK(U(GetDCOrgEx)(dc, &o0)); int t0 = U(GetClipBox)(dc, &c0);
    BOOL r = UN(NtUserPaintDesktop)(dc);
    CHECK(U(GetDCOrgEx)(dc, &o1)); int t1 = U(GetClipBox)(dc, &c1);
    t_note("PaintDesktop(memdc) ret=%d", r);
    CHECK(o0.x == o1.x && o0.y == o1.y);
    CHECK(c0.left == c1.left && c0.top == c1.top && c0.right == c1.right && c0.bottom == c1.bottom);
    CHECK(t0 == t1);
    g4_destroy(h); g4_memdc_free(dc, b, o);
}
TEST(gap4_paintdesktop_does_not_invalidate_dc_handle) {
    G4REQ(NtUserPaintDesktop); REQUIRE(U(GetObjectType));
    HWND h = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 200, 100, NULL);
    HBITMAP b, o; HDC dc = g4_memdc(&b, &o, NULL);
    if (!h || !dc) { t_skip("no fixtures"); g4_destroy(h); g4_memdc_free(dc, b, o); return; }
    UN(NtUserPaintDesktop)(dc);
    EXPECT_EQ(U(GetObjectType)(dc), OBJ_MEMDC);
    g4_destroy(h); g4_memdc_free(dc, b, o);
}

/* ---- NtUserCalculatePopupWindowPosition -------------------------------------------------------- */
static int popup(POINT a, SIZE s, UINT flags, const RECT *ex, RECT *out) {
    memset(out, 0x7e, sizeof *out);
    return UN(NtUserCalculatePopupWindowPosition)(&a, &s, flags, ex, out);
}
/* flags & 0xfffe0200 -> UserSetLastError(0x3ec) and FALSE, before any pointer is read. */
TEST(gap4_popuppos_invalid_flag_bits_fail_1004) {
    G4REQ(NtUserCalculatePopupWindowPosition);
    static const UINT bad[] = { 0x200, 0x20000, 0x40000, 0x80000000, 0x200 | TPM_CENTERALIGN, 0xfffe0000 };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        RECT out; SET_ERR(0);
        int r = popup((POINT){ 500, 500 }, (SIZE){ 100, 50 }, bad[i], NULL, &out);
        CHECKF(r == 0, "flags 0x%x ret %d", bad[i], r);
        EXPECT_ERR(ERROR_INVALID_FLAGS);
    }
}
/* NULL output rectangle -> 0x57 (checked before the probes of the other arguments). */
TEST(gap4_popuppos_null_out_rect_fails_87) {
    G4REQ(NtUserCalculatePopupWindowPosition);
    POINT a = { 500, 500 }; SIZE s = { 100, 50 };
    SET_ERR(0);
    EXPECT_EQ(UN(NtUserCalculatePopupWindowPosition)(&a, &s, 0, NULL, NULL), 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}
/* Invalid flags are tested before the NULL out rect. */
TEST(gap4_popuppos_flag_check_precedes_null_out_check) {
    G4REQ(NtUserCalculatePopupWindowPosition);
    POINT a = { 500, 500 }; SIZE s = { 100, 50 };
    SET_ERR(0);
    EXPECT_EQ(UN(NtUserCalculatePopupWindowPosition)(&a, &s, 0x200, NULL, NULL), 0);
    EXPECT_ERR(ERROR_INVALID_FLAGS);
}
/* A negative cx or cy in the SIZE fails with 0x57 and leaves the output rectangle untouched. */
TEST(gap4_popuppos_negative_size_fails_87_out_untouched) {
    G4REQ(NtUserCalculatePopupWindowPosition);
    static const SIZE sz[] = { { -1, 50 }, { 100, -1 }, { -100, -50 }, { 0x80000000, 10 }, { 10, 0x80000000 } };
    for (unsigned i = 0; i < sizeof sz / sizeof sz[0]; i++) {
        RECT out; SET_ERR(0);
        int r = popup((POINT){ 500, 500 }, sz[i], 0, NULL, &out);
        CHECKF(r == 0, "size %ld,%ld ret %d", sz[i].cx, sz[i].cy, r);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
        unsigned char *p = (unsigned char *)&out; int same = 1;
        for (unsigned j = 0; j < sizeof out; j++) if (p[j] != 0x7e) same = 0;
        CHECK(same);
    }
}
static int mon_center(POINT *c, RECT *mon) {
    if (g4_real_dpi() != 96) return 0;   /* the kernel works in physical pixels; a DPI-unaware process sees scaled ones */
    if (!U(MonitorFromPoint) || !U(GetMonitorInfoW)) return 0;
    HMONITOR m = U(MonitorFromPoint)((POINT){ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi; mi.cbSize = sizeof mi;
    if (!m || !U(GetMonitorInfoW)(m, &mi)) return 0;
    *mon = mi.rcMonitor;
    c->x = (mi.rcMonitor.left + mi.rcMonitor.right) / 2; c->y = (mi.rcMonitor.top + mi.rcMonitor.bottom) / 2;
    return 1;
}
/* The output rectangle is {pos, pos + size} (right = x + cx, bottom = y + cy in NtUserCalculatePopupWindowPosition). */
TEST(gap4_popuppos_output_rect_has_requested_size) {
    G4REQ(NtUserCalculatePopupWindowPosition);
    POINT c; RECT mon;
    if (!mon_center(&c, &mon)) { t_skip("no monitor"); return; }
    static const SIZE sz[] = { { 1, 1 }, { 100, 50 }, { 200, 300 }, { 0, 0 } };
    static const UINT fl[] = { 0, TPM_CENTERALIGN, TPM_RIGHTALIGN, TPM_VCENTERALIGN, TPM_BOTTOMALIGN, TPM_RIGHTALIGN | TPM_BOTTOMALIGN };
    for (unsigned i = 0; i < 4; i++) for (unsigned j = 0; j < 6; j++) {
        RECT out; int r = popup(c, sz[i], fl[j], NULL, &out);
        CHECKF(r == 1, "ret %d", r);
        CHECKF(out.right - out.left == sz[i].cx && out.bottom - out.top == sz[i].cy,
               "size %ld,%ld flags 0x%x -> %ld,%ld,%ld,%ld", sz[i].cx, sz[i].cy, fl[j], out.left, out.top, out.right, out.bottom);
    }
}
/* With room on every side the alignment flags select the origin: LEFT/TOP -> anchor; CENTER -> x - cx/2;
 * RIGHT -> x - cx; VCENTER -> y - cy/2; BOTTOM -> y - cy. */
TEST(gap4_popuppos_alignment_flags_place_origin) {
    G4REQ(NtUserCalculatePopupWindowPosition);
    POINT c; RECT mon;
    if (!mon_center(&c, &mon)) { t_skip("no monitor"); return; }
    SIZE s = { 100, 60 };
    static const struct { UINT f; int dx, dy; } v[] = {
        { 0, 0, 0 }, { TPM_CENTERALIGN, -50, 0 }, { TPM_RIGHTALIGN, -100, 0 },
        { TPM_VCENTERALIGN, 0, -30 }, { TPM_BOTTOMALIGN, 0, -60 },
        { TPM_CENTERALIGN | TPM_VCENTERALIGN, -50, -30 }, { TPM_RIGHTALIGN | TPM_BOTTOMALIGN, -100, -60 },
    };
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) {
        RECT out; int r = popup(c, s, v[i].f, NULL, &out);
        CHECKF(r == 1, "ret %d", r);
        CHECKF(out.left == c.x + v[i].dx && out.top == c.y + v[i].dy,
               "flags 0x%x got %ld,%ld want %ld,%ld", v[i].f, out.left, out.top, (long)c.x + v[i].dx, (long)c.y + v[i].dy);
    }
}
TEST(gap4_popuppos_result_stays_inside_monitor) {
    G4REQ(NtUserCalculatePopupWindowPosition);
    POINT c; RECT mon;
    if (!mon_center(&c, &mon)) { t_skip("no monitor"); return; }
    /* An anchor at the bottom-right corner of the monitor with a popup that cannot fit there is moved back inside. */
    POINT corner = { mon.right - 1, mon.bottom - 1 };
    RECT out; int r = popup(corner, (SIZE){ 150, 80 }, 0, NULL, &out);
    CHECK(r == 1);
    CHECKF(out.left >= mon.left && out.top >= mon.top && out.right <= mon.right && out.bottom <= mon.bottom,
           "out %ld,%ld,%ld,%ld mon %ld,%ld,%ld,%ld", out.left, out.top, out.right, out.bottom, mon.left, mon.top, mon.right, mon.bottom);
}
TEST(gap4_popuppos_exclude_rect_is_avoided) {
    G4REQ(NtUserCalculatePopupWindowPosition);
    POINT c; RECT mon;
    if (!mon_center(&c, &mon)) { t_skip("no monitor"); return; }
    RECT ex = { c.x - 20, c.y - 20, c.x + 20, c.y + 20 };
    RECT out; int r = popup(c, (SIZE){ 100, 60 }, 0, &ex, &out);
    CHECK(r == 1);
    /* FindBestPos keeps the popup off the exclusion rectangle when there is room beside it. */
    int overlap = out.left < ex.right && out.right > ex.left && out.top < ex.bottom && out.bottom > ex.top;
    CHECKF(!overlap, "out %ld,%ld,%ld,%ld ex %ld,%ld,%ld,%ld", out.left, out.top, out.right, out.bottom, ex.left, ex.top, ex.right, ex.bottom);
    CHECK(out.right - out.left == 100 && out.bottom - out.top == 60);
}
TEST(gap4_popuppos_matches_user32_thunk) {
    G4REQ(NtUserCalculatePopupWindowPosition); REQUIRE(U(CalculatePopupWindowPosition));
    POINT c; RECT mon;
    if (!mon_center(&c, &mon)) { t_skip("no monitor"); return; }
    SIZE s = { 120, 70 }; RECT a, b;
    SET_ERR(0);
    BOOL ra = U(CalculatePopupWindowPosition)(&c, &s, TPM_CENTERALIGN, NULL, &a);
    BOOL rb = UN(NtUserCalculatePopupWindowPosition)(&c, &s, TPM_CENTERALIGN, NULL, &b);
    CHECK(ra == rb);
    CHECK(a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom);
}
/* The flag mask accepts every TPM bit that is not 0x200 or above 0x1ffff. */
TEST(gap4_popuppos_valid_flag_bits_accepted) {
    G4REQ(NtUserCalculatePopupWindowPosition);
    POINT c; RECT mon;
    if (!mon_center(&c, &mon)) { t_skip("no monitor"); return; }
    for (int bit = 0; bit < 17; bit++) {
        if (bit == 9) continue;
        RECT out; int r = popup(c, (SIZE){ 40, 20 }, 1u << bit, NULL, &out);
        CHECKF(r == 1, "flag 0x%x ret %d", 1u << bit, r);
    }
}

/* ---- NtUserLogicalToPhysicalPoint / NtUserPhysicalToLogicalPoint ------------------------------- */
TEST(gap4_l2p_invalid_hwnd_fails_1400_point_untouched) {
    G4REQ(NtUserLogicalToPhysicalPoint);
    for (unsigned i = 0; i < NBAD; i++) {
        POINT p = { 11, 22 }; SET_ERR(0);
        BOOL r = UN(NtUserLogicalToPhysicalPoint)(BAD(i), &p);
        CHECKF(r == 0, "hwnd %p ret %d", BAD(i), r);
        EXPECT_ERR(G4_ERR_INVALID_WINDOW);
        CHECK(p.x == 11 && p.y == 22);
    }
}
TEST(gap4_p2l_invalid_hwnd_fails_1400_point_untouched) {
    G4REQ(NtUserPhysicalToLogicalPoint);
    for (unsigned i = 0; i < NBAD; i++) {
        POINT p = { 11, 22 }; SET_ERR(0);
        BOOL r = UN(NtUserPhysicalToLogicalPoint)(BAD(i), &p);
        CHECKF(r == 0, "hwnd %p ret %d", BAD(i), r);
        EXPECT_ERR(G4_ERR_INVALID_WINDOW);
        CHECK(p.x == 11 && p.y == 22);
    }
}
/* DCEPtInRect tests the point against the window rectangle at tagWND+0x28->+0x58; a point outside gives FALSE
 * and the output point is only written on success. */
static void l2p_p2l(const char *which, BOOL (WINAPI *fn)(HWND, POINT *)) {
    HWND h = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 120, 300, 200, NULL);
    if (!h || !U(GetWindowRect)) { t_skip("no window"); g4_destroy(h); return; }
    RECT wr; U(GetWindowRect)(h, &wr);
    POINT in = { (wr.left + wr.right) / 2, (wr.top + wr.bottom) / 2 };
    POINT p = in;
    BOOL r = fn(h, &p);
    CHECKF(r == 1, "%s inside ret %d", which, r);
    t_note("%s inside: %ld,%ld -> %ld,%ld", which, in.x, in.y, p.x, p.y);
    if (g4_real_dpi() == 96)
        CHECKF(p.x == in.x && p.y == in.y, "%s unaware process moved the point to %ld,%ld", which, p.x, p.y);
    static const POINT out[] = { { -30000, -30000 }, { 30000, 30000 }, { 0, 0 }, { 99999, 5 } };
    for (unsigned i = 0; i < 4; i++) {
        if (out[i].x >= wr.left && out[i].x < wr.right && out[i].y >= wr.top && out[i].y < wr.bottom) continue;
        POINT q = out[i];
        r = fn(h, &q);
        CHECKF(r == 0, "%s outside %ld,%ld ret %d", which, out[i].x, out[i].y, r);
        CHECK(q.x == out[i].x && q.y == out[i].y);
    }
    g4_destroy(h);
}
TEST(gap4_l2p_point_inside_and_outside_window_rect) { G4REQ(NtUserLogicalToPhysicalPoint); l2p_p2l("L2P", UN(NtUserLogicalToPhysicalPoint)); }
TEST(gap4_p2l_point_inside_and_outside_window_rect) { G4REQ(NtUserPhysicalToLogicalPoint); l2p_p2l("P2L", UN(NtUserPhysicalToLogicalPoint)); }
/* Edges: on Windows 10 the test is inclusive on all four sides: left/top and right/bottom themselves are inside, one pixel
 * further out is outside (checked on the diagonal corner points). */
TEST(gap4_l2p_window_rect_edges_inclusive) {
    G4REQ(NtUserLogicalToPhysicalPoint); REQUIRE(U(GetWindowRect));
    HWND h = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 120, 300, 200, NULL);
    if (!h) { t_skip("no window"); return; }
    if (g4_real_dpi() != 96) { t_skip("display scaling is not 100%%"); g4_destroy(h); return; }
    RECT wr; U(GetWindowRect)(h, &wr);
    POINT tl = { wr.left, wr.top }, br = { wr.right, wr.bottom }, lastp = { wr.right - 1, wr.bottom - 1 };
    POINT tl_out = { wr.left - 1, wr.top - 1 }, br_out = { wr.right + 1, wr.bottom + 1 };
    CHECK(UN(NtUserLogicalToPhysicalPoint)(h, &tl) == 1);
    CHECK(UN(NtUserLogicalToPhysicalPoint)(h, &lastp) == 1);
    CHECK(UN(NtUserLogicalToPhysicalPoint)(h, &br) == 1);
    CHECK(UN(NtUserLogicalToPhysicalPoint)(h, &tl_out) == 0);
    CHECK(UN(NtUserLogicalToPhysicalPoint)(h, &br_out) == 0);
    g4_destroy(h);
}
TEST(gap4_p2l_matches_user32_thunk) {
    G4REQ(NtUserPhysicalToLogicalPoint); REQUIRE(U(PhysicalToLogicalPoint)); REQUIRE(U(GetWindowRect));
    HWND h = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 120, 300, 200, NULL);
    if (!h) { t_skip("no window"); return; }
    RECT wr; U(GetWindowRect)(h, &wr);
    POINT a = { wr.left + 10, wr.top + 10 }, b = a;
    BOOL ra = U(PhysicalToLogicalPoint)(h, &a), rb = UN(NtUserPhysicalToLogicalPoint)(h, &b);
    CHECK(ra == rb); CHECK(a.x == b.x && a.y == b.y);
    g4_destroy(h);
}
TEST(gap4_l2p_matches_user32_thunk) {
    G4REQ(NtUserLogicalToPhysicalPoint); REQUIRE(U(LogicalToPhysicalPoint)); REQUIRE(U(GetWindowRect));
    HWND h = g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 120, 300, 200, NULL);
    if (!h) { t_skip("no window"); return; }
    RECT wr; U(GetWindowRect)(h, &wr);
    POINT a = { wr.left + 10, wr.top + 10 }, b = a;
    BOOL ra = U(LogicalToPhysicalPoint)(h, &a), rb = UN(NtUserLogicalToPhysicalPoint)(h, &b);
    CHECK(ra == rb); CHECK(a.x == b.x && a.y == b.y);
    g4_destroy(h);
}
