/* NtUserPrintWindow into a DIB-section memory DC, and NtUserLockWindowUpdate (system-wide single lock). */
#include "pd.h"

#define NEED_PW() REQUIRE(UN(NtUserPrintWindow)); REQUIRE(U(CreateCompatibleDC)); REQUIRE(U(CreateDIBSection)); REQUIRE(UN(NtUserInvalidateRect)); \
                  REQUIRE(UN(NtUserGetUpdateRect)); REQUIRE(U(GdiFlush))
#define NEED_LOCK() REQUIRE(UN(NtUserLockWindowUpdate)); REQUIRE(U(LockWindowUpdate)); REQUIRE(U(GetClipBox)); REQUIRE(UN(NtUserGetDCEx))

static int dib_differs_from_marker(struct pd_dib *d, int x0, int y0, int x1, int y1) {
    int n = 0;
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) if (pd_dib_px(d, x, y) != PD_MARKER) n++;
    return n;
}

/* ---- PrintWindow ---- */
PT(printwindow_undefined_flags_rejected) {
    PD_BEGIN(); NEED_PW();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    struct pd_dib d;
    if (!pd_dib_make(&d, 8, 8)) { t_skip("cannot create a DIB section"); return; }
    UINT bad[] = { 4, 8, 0x10, 0x100, 0x10000, 0x80000000, 0xFFFFFFFC, 5, 7 };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SET_ERR(0);
        BOOL r = UN(NtUserPrintWindow)(h, d.dc, bad[i]);
        DWORD e = GetLastError();
        /* NtUserPrintWindow: (flags & 3) != flags -> ERROR_INVALID_PARAMETER before PrintWindow */
        CHECKF(r == 0, "flags 0x%x returned %d", bad[i], r);
        CHECKF(e == ERROR_INVALID_PARAMETER, "flags 0x%x last error %lu", bad[i], e);
    }
    pd_dib_free(&d);
    PD_END();
}
PT(printwindow_flags_zero_and_clientonly_succeed) {
    PD_BEGIN(); NEED_PW();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    struct pd_dib d;
    if (!pd_dib_make(&d, 200, 160)) { t_skip("cannot create a DIB section"); return; }
    CHECK(UN(NtUserPrintWindow)(h, d.dc, 0) == 1);
    CHECK(UN(NtUserPrintWindow)(h, d.dc, PW_CLIENTONLY) == 1);
    pd_dib_free(&d);
    PD_END();
}
PT(printwindow_renderfullcontent_flag_accepted) {
    PD_BEGIN(); NEED_PW();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    struct pd_dib d;
    if (!pd_dib_make(&d, 200, 160)) { t_skip("cannot create a DIB section"); return; }
    SET_ERR(0);
    BOOL r2 = UN(NtUserPrintWindow)(h, d.dc, PW_RENDERFULLCONTENT);
    BOOL r3 = UN(NtUserPrintWindow)(h, d.dc, PW_RENDERFULLCONTENT | PW_CLIENTONLY);
    t_note("PW_RENDERFULLCONTENT=%d, |CLIENTONLY=%d (composition-dependent: DWM surface capture)", r2, r3);
    CHECK(GetLastError() != ERROR_INVALID_PARAMETER);
    pd_dib_free(&d);
    PD_END();
}
PT(printwindow_bad_hwnd) {
    PD_BEGIN(); NEED_PW();
    struct pd_dib d;
    if (!pd_dib_make(&d, 8, 8)) { t_skip("cannot create a DIB section"); return; }
    SET_ERR(0);
    CHECK(UN(NtUserPrintWindow)((HWND)(ULONG_PTR)0x12345678, d.dc, 0) == 0);
    EXPECT_ERR(PD_ERR_INVALID_WINDOW_HANDLE);
    pd_dib_free(&d);
    PD_END();
}
PT(printwindow_desktop_window_refused_but_message_parent_popup_prints) {
    PD_BEGIN(); NEED_PW();
    struct pd_dib d;
    if (!pd_dib_make(&d, 8, 8)) { t_skip("cannot create a DIB section"); return; }
    /* NtUserPrintWindow: the desktop window (fnid 0x29d) is refused */
    CHECK(UN(NtUserPrintWindow)(U(GetDesktopWindow)(), d.dc, 0) == 0);
    HWND m = U(CreateWindowExA)(0, pd_cls(PDC_PLAIN), "msg", WS_POPUP, 0, 0, 10, 10, HWND_MESSAGE, NULL, pd_hinst(), NULL);
    /* only the message-only root window itself is refused: a popup created below HWND_MESSAGE prints (returns 1) */
    if (m) { int r = UN(NtUserPrintWindow)(m, d.dc, 0); if (t_is_wine()) t_note("PrintWindow(window below HWND_MESSAGE)=%d", r); else CHECK(r == 1); U(DestroyWindow)(m); }
    pd_dib_free(&d);
    PD_END();
}
PT(printwindow_client_only_writes_exactly_client_extent) {
    PD_BEGIN(); NEED_PW();
    HWND h = pd_top_style(PDC_PLAIN, WS_CAPTION);
    REQUIRE(h);
    struct pd_dib d;
    if (!pd_dib_make(&d, 240, 200)) { t_skip("cannot create a DIB section"); return; }
    pd_dib_fill(&d, PD_MARKER);
    RECT c; U(GetClientRect)(h, &c);
    CHECK(UN(NtUserPrintWindow)(h, d.dc, PW_CLIENTONLY) == 1);
    /* PrintWindow blits (rcClient.right-left) x (rcClient.bottom-top) to (0,0): nothing outside that rectangle is touched */
    CHECK(dib_differs_from_marker(&d, c.right, 0, 240, 200) == 0);
    CHECK(dib_differs_from_marker(&d, 0, c.bottom, 240, 200) == 0);
    int inside = dib_differs_from_marker(&d, 0, 0, c.right, c.bottom);
    t_note("client-only: %d of %ld pixels changed inside the client extent", inside, (long)c.right * c.bottom);
    CHECK(inside > 0);
    pd_dib_free(&d);
    PD_END();
}
PT(printwindow_full_window_writes_exactly_window_extent) {
    PD_BEGIN(); NEED_PW();
    HWND h = pd_top_style(PDC_PLAIN, WS_CAPTION);
    REQUIRE(h);
    struct pd_dib d;
    if (!pd_dib_make(&d, 240, 200)) { t_skip("cannot create a DIB section"); return; }
    pd_dib_fill(&d, PD_MARKER);
    RECT w; U(GetWindowRect)(h, &w);
    int ww = w.right - w.left, wh = w.bottom - w.top;
    CHECK(ww > PD_CW && wh > PD_CH);     /* the caption gives a bigger window than client */
    CHECK(UN(NtUserPrintWindow)(h, d.dc, 0) == 1);
    CHECK(dib_differs_from_marker(&d, ww, 0, 240, 200) == 0);
    CHECK(dib_differs_from_marker(&d, 0, wh, 240, 200) == 0);
    CHECK(dib_differs_from_marker(&d, 0, 0, ww, wh) > 0);
    pd_dib_free(&d);
    PD_END();
}
PT(printwindow_captures_class_background) {
    PD_BEGIN(); NEED_PW();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    struct pd_dib d;
    if (!pd_dib_make(&d, 200, 160)) { t_skip("cannot create a DIB section"); return; }
    pd_dib_fill(&d, PD_MARKER);
    UN(NtUserInvalidateRect)(h, NULL, TRUE);
    CHECK(UN(NtUserPrintWindow)(h, d.dc, PW_CLIENTONLY) == 1);
    unsigned mid = pd_dib_px(&d, PD_CW / 2, PD_CH / 2), want = ((GetRValue(PD_FILL)) << 16) | (GetGValue(PD_FILL) << 8) | GetBValue(PD_FILL);
    CHECK(mid != PD_MARKER);
    t_note("centre pixel 0x%06x, class brush colour 0x%06x (%s)", mid, want, mid == want ? "match" : "differs: redirection/composition dependent");
    pd_dib_free(&d);
    PD_END();
}
PT(printwindow_runs_pending_paint_synchronously) {
    PD_BEGIN(); NEED_PW();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    struct pd_dib d;
    if (!pd_dib_make(&d, 200, 160)) { t_skip("cannot create a DIB section"); return; }
    pd_beh = PB_PAINT_BP;
    UN(NtUserInvalidateRect)(h, NULL, FALSE);
    CHECK(pd_count(h, WM_PAINT) == 0);
    CHECK(UN(NtUserPrintWindow)(h, d.dc, PW_CLIENTONLY) == 1);
    CHECK(pd_count(h, WM_PAINT) == 1);                  /* PrintWindow: an update of the window before the capture */
    CHECK(UN(NtUserGetUpdateRect)(h, NULL, FALSE) == 0);
    pd_beh = 0;
    pd_dib_free(&d);
    PD_END();
}
PT(printwindow_does_not_send_wm_print) {
    PD_BEGIN(); NEED_PW();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    struct pd_dib d;
    if (!pd_dib_make(&d, 200, 160)) { t_skip("cannot create a DIB section"); return; }
    CHECK(UN(NtUserPrintWindow)(h, d.dc, 0) == 1);
    CHECK(UN(NtUserPrintWindow)(h, d.dc, PW_CLIENTONLY) == 1);
    int n = pd_count(h, WM_PRINT) + pd_count(h, WM_PRINTCLIENT);
    if (t_is_wine()) t_note("WM_PRINT/WM_PRINTCLIENT sent %d times", n); else CHECK(n == 0);   /* the 19041 kernel path is a BitBlt from the window DC */
    pd_dib_free(&d);
    PD_END();
}
PT(printwindow_user32_is_the_syscall) {
    PD_BEGIN(); NEED_PW(); REQUIRE(U(PrintWindow));
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    struct pd_dib d;
    if (!pd_dib_make(&d, 200, 160)) { t_skip("cannot create a DIB section"); return; }
    CHECK(U(PrintWindow)(h, d.dc, PW_CLIENTONLY) == TRUE);
    SET_ERR(0);
    CHECK(U(PrintWindow)(h, d.dc, 0x20) == FALSE);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    pd_dib_free(&d);
    PD_END();
}
PT(printwindow_leaves_no_cache_dc_busy) {
    PD_BEGIN(); NEED_PW();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    struct pd_dib d;
    if (!pd_dib_make(&d, 200, 160)) { t_skip("cannot create a DIB section"); return; }
    HDC before = U(GetDC)(h);
    U(ReleaseDC)(h, before);
    for (int i = 0; i < 8; i++) UN(NtUserPrintWindow)(h, d.dc, PW_CLIENTONLY);
    HDC after = U(GetDC)(h);
    CHECK(after != NULL);
    t_note("cache DC before/after 8 PrintWindow calls: %p / %p", (void *)before, (void *)after);
    U(ReleaseDC)(h, after);
    pd_dib_free(&d);
    PD_END();
}

/* ---- LockWindowUpdate ---- */
PT(lockwindowupdate_single_lock_protocol) {
    PD_BEGIN(); NEED_LOCK();
    HWND a = pd_top(PDC_PLAIN), b = pd_top(PDC_PLAIN);
    REQUIRE(a); REQUIRE(b);
    SET_ERR(0);
    CHECK(UN(NtUserLockWindowUpdate)(NULL) == 0);        /* nothing locked: LockWindowUpdate -> ERROR_INVALID_PARAMETER */
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    CHECK(UN(NtUserLockWindowUpdate)(a) == 1);
    SET_ERR(0);
    CHECK(UN(NtUserLockWindowUpdate)(b) == 0);           /* already locked by this thread */
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    SET_ERR(0);
    CHECK(UN(NtUserLockWindowUpdate)(a) == 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    CHECK(UN(NtUserLockWindowUpdate)(NULL) == 1);
    SET_ERR(0);
    CHECK(UN(NtUserLockWindowUpdate)(NULL) == 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    CHECK(UN(NtUserLockWindowUpdate)(b) == 1);           /* free again, other window */
    CHECK(UN(NtUserLockWindowUpdate)(NULL) == 1);
    PD_END();
}
PT(lockwindowupdate_user32_wrapper) {
    PD_BEGIN(); NEED_LOCK();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    CHECK(U(LockWindowUpdate)(h) == TRUE);
    CHECK(U(LockWindowUpdate)(NULL) == TRUE);
    PD_END();
}
PT(lockwindowupdate_bad_hwnd_leaves_no_lock) {
    PD_BEGIN(); NEED_LOCK();
    SET_ERR(0);
    CHECK(UN(NtUserLockWindowUpdate)((HWND)(ULONG_PTR)0x12345678) == 0);
    EXPECT_ERR(PD_ERR_INVALID_WINDOW_HANDLE);
    SET_ERR(0);
    CHECK(UN(NtUserLockWindowUpdate)(NULL) == 0);        /* confirms nothing was locked */
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    PD_END();
}
PT(lockwindowupdate_child_window_can_be_locked) {
    PD_BEGIN(); NEED_LOCK();
    HWND p = pd_top(PDC_PLAIN);
    REQUIRE(p);
    HWND c = pd_child(p, PDC_PLAIN, 0, 5, 5, 20, 20);
    REQUIRE(c);
    CHECK(UN(NtUserLockWindowUpdate)(c) == 1);
    CHECK(UN(NtUserLockWindowUpdate)(NULL) == 1);
    PD_END();
}
PT(lockwindowupdate_destroying_locked_window_releases_lock) {
    PD_BEGIN(); NEED_LOCK();
    HWND a = pd_top(PDC_PLAIN), b = pd_top(PDC_PLAIN);
    REQUIRE(a); REQUIRE(b);
    CHECK(UN(NtUserLockWindowUpdate)(a) == 1);
    U(DestroyWindow)(a);
    int r = UN(NtUserLockWindowUpdate)(b);
    CHECK(r == 1);
    UN(NtUserLockWindowUpdate)(NULL);
    PD_END();
}
PT(lockwindowupdate_locked_window_dc_has_empty_clip) {
    PD_BEGIN(); NEED_LOCK();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    PD_NEED_EXPOSED(h);
    CHECK(UN(NtUserLockWindowUpdate)(h) == 1);
    HDC locked = U(GetDC)(h), bypass = UN(NtUserGetDCEx)(h, NULL, DCX_CACHE | DCX_LOCKWINDOWUPDATE);
    REQUIRE(locked); REQUIRE(bypass);
    RECT b, c;
    U(GetClientRect)(h, &c);
    CHECK(U(GetClipBox)(locked, &b) == NULLREGION);      /* drawing into a locked window is discarded: empty visible region */
    CHECK(U(GetClipBox)(bypass, &b) == SIMPLEREGION);    /* DCX_LOCKWINDOWUPDATE ignores the lock */
    CHECK(pd_eq(&b, &c));
    U(ReleaseDC)(h, locked); U(ReleaseDC)(h, bypass);
    CHECK(UN(NtUserLockWindowUpdate)(NULL) == 1);
    HDC after = U(GetDC)(h);
    CHECK(U(GetClipBox)(after, &b) == SIMPLEREGION);
    U(ReleaseDC)(h, after);
    PD_END();
}
PT(lockwindowupdate_unlock_invalidates_note) {
    PD_BEGIN(); NEED_LOCK(); REQUIRE(UN(NtUserGetUpdateRect));
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    CHECK(UN(NtUserLockWindowUpdate)(h) == 1);
    CHECK(UN(NtUserLockWindowUpdate)(NULL) == 1);
    RECT r = {0};
    BOOL g = UN(NtUserGetUpdateRect)(h, &r, FALSE);
    t_note("after unlock: GetUpdateRect=%d (%ld,%ld)-(%ld,%ld)", g, r.left, r.top, r.right, r.bottom);
    PD_END();
}
PT(lockwindowupdate_does_not_block_update_region_queries) {
    PD_BEGIN(); NEED_LOCK(); REQUIRE(UN(NtUserGetUpdateRect));
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    CHECK(UN(NtUserLockWindowUpdate)(h) == 1);
    RECT in = pd_rc(2, 2, 12, 12), r = {0};
    CHECK(UN(NtUserInvalidateRect)(h, &in, FALSE));
    CHECK(UN(NtUserGetUpdateRect)(h, &r, FALSE) == 1);
    CHECK(pd_eq(&r, &in));
    CHECK(UN(NtUserLockWindowUpdate)(NULL) == 1);
    PD_END();
}

struct lk { HWND h; int r_lock, r_unlock; DWORD e_lock, e_unlock; };
static DWORD lock_other_thread(void *p) {
    struct lk *l = (struct lk *)p;
    SetLastError(0);
    l->r_lock = UN(NtUserLockWindowUpdate)(l->h);
    l->e_lock = GetLastError();
    SetLastError(0);
    l->r_unlock = UN(NtUserLockWindowUpdate)(NULL);
    l->e_unlock = GetLastError();
    return 0;
}
PT(lockwindowupdate_other_thread_gets_screen_already_locked) {
    PD_BEGIN(); NEED_LOCK();
    HWND a = pd_top(PDC_PLAIN), b = pd_top(PDC_PLAIN);
    REQUIRE(a); REQUIRE(b);
    CHECK(UN(NtUserLockWindowUpdate)(a) == 1);
    struct lk l; memset(&l, 0, sizeof l);
    l.h = b;
    k_run_gui_thread(lock_other_thread, &l);
    /* LockWindowUpdate: the lock-update owner is to another thread and no override -> ERROR_SCREEN_ALREADY_LOCKED, for lock and unlock alike */
    CHECK(l.r_lock == 0);   CHECK(l.e_lock == PD_ERR_SCREEN_ALREADY_LOCKED);
    CHECK(l.r_unlock == 0); CHECK(l.e_unlock == PD_ERR_SCREEN_ALREADY_LOCKED);
    CHECK(UN(NtUserLockWindowUpdate)(NULL) == 1);   /* the owner can still unlock */
    PD_END();
}
PT(lockwindowupdate_other_thread_lock_when_free_then_main_blocked) {
    PD_BEGIN(); NEED_LOCK();
    HWND a = pd_top(PDC_PLAIN);
    REQUIRE(a);
    struct lk l; memset(&l, 0, sizeof l);
    l.h = a;
    k_run_gui_thread(lock_other_thread, &l);   /* locks then unlocks inside the same thread */
    CHECK(l.r_lock == 1);
    CHECK(l.r_unlock == 1);
    CHECK(UN(NtUserLockWindowUpdate)(a) == 1);
    CHECK(UN(NtUserLockWindowUpdate)(NULL) == 1);
    PD_END();
}
