/* dx_gap_07: state-changing gap tests (run only with /destructive): NtUserRegisterPointerInputTarget,
 * NtUserRegisterTouchHitTestingWindow, NtGdiSetSystemPaletteUse. Each test restores what it touched. */
#include "g7.h"

static BOOL token_uiaccess(void)
{
    HANDLE tok = NULL; DWORD v = 0, len = 0;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return FALSE;
    BOOL ok = GetTokenInformation(tok, (TOKEN_INFORMATION_CLASS)26 /* TokenUIAccess */, &v, sizeof v, &len);
    CloseHandle(tok);
    return ok && v;
}

/* A window owned by another thread that stays alive until released. */
struct other { HWND hwnd; HANDLE ready, stop; };
static DWORD WINAPI other_thread(void *p)
{
    struct other *o = (struct other *)p;
    o->hwnd = g7_window(WS_POPUP, 0);
    SetEvent(o->ready);
    WaitForSingleObject(o->stop, 5000);
    if (o->hwnd) U(DestroyWindow)(o->hwnd);
    return 0;
}
static HANDLE other_start(struct other *o)
{
    memset(o, 0, sizeof *o);
    o->ready = CreateEventW(NULL, TRUE, FALSE, NULL); o->stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    HANDLE th = CreateThread(NULL, 0, other_thread, o, 0, NULL);
    if (th) WaitForSingleObject(o->ready, 5000);
    return th;
}
static void other_stop(struct other *o, HANDLE th)
{
    SetEvent(o->stop);
    if (th) { WaitForSingleObject(th, 5000); CloseHandle(th); }
    CloseHandle(o->ready); CloseHandle(o->stop);
}

/* ---- NtUserRegisterPointerInputTarget(hwnd, reserved, pointerType, flag) -------------------------------------- */
TEST(dx_gap7_regptrtarget_invalid_hwnd_fails) {
    NtUserRegisterPointerInputTarget_pfn f = G7N(NtUserRegisterPointerInputTarget); REQUIRE(f);
    EXPECT_EQ(f(NULL, 0, 2, 1), FALSE);                       /* ValidateHwnd fails before any other check */
    EXPECT_EQ(f((HWND)(ULONG_PTR)0x1234, 0, 2, 1), FALSE);
    EXPECT_EQ(f((HWND)(ULONG_PTR)0x1234, 0, 3, 1), FALSE);
}
TEST(dx_gap7_regptrtarget_invalid_pointer_type_fails) {
    NtUserRegisterPointerInputTarget_pfn f = G7N(NtUserRegisterPointerInputTarget); REQUIRE(f);
    HWND w = g7_window(WS_POPUP, 0); REQUIRE(w);
    /* only PT_TOUCH (2) and PT_PEN (3) are accepted; everything else fails (5 without UIAccess under UIPI, else 0x57) */
    static const DWORD bad[] = { 0, 1, 4, 5, 6, 99, 0xffffffff };
    BOOL uia = token_uiaccess();
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        SetLastError(0);
        EXPECT_EQ(f(w, 0, bad[i], 1), FALSE);
        DWORD e = GetLastError();
        CHECKF(e == ERROR_INVALID_PARAMETER || (!uia && e == ERROR_ACCESS_DENIED), "type %lu err %lu uiaccess %d", bad[i], e, uia);
        if (uia) EXPECT_EQ(e, ERROR_INVALID_PARAMETER);
    }
    U(DestroyWindow)(w);
}
TEST(dx_gap7_regptrtarget_cross_thread_window_denied) {
    NtUserRegisterPointerInputTarget_pfn f = G7N(NtUserRegisterPointerInputTarget); REQUIRE(f);
    struct other o; HANDLE th = other_start(&o);
    if (!th || !o.hwnd) { other_stop(&o, th); t_skip("no helper window"); return; }
    SetLastError(0);
    EXPECT_EQ(f(o.hwnd, 0, 2, 1), FALSE);      /* only the owning thread may register its window */
    EXPECT_ERR(ERROR_ACCESS_DENIED);
    SetLastError(0);
    EXPECT_EQ(f(o.hwnd, 0, 3, 0), FALSE);
    EXPECT_ERR(ERROR_ACCESS_DENIED);
    other_stop(&o, th);
}
TEST(dx_gap7_regptrtarget_without_uiaccess_is_denied) {
    NtUserRegisterPointerInputTarget_pfn f = G7N(NtUserRegisterPointerInputTarget); REQUIRE(f);
    if (token_uiaccess()) { t_skip("process has UIAccess"); return; }
    HWND w = g7_window(WS_POPUP, 0); REQUIRE(w);
    SetLastError(0);
    BOOL r = f(w, 0, 2, 1);
    t_note("RegisterPointerInputTarget touch without UIAccess: %d err %lu", r, GetLastError());
    if (!r) EXPECT_ERR(ERROR_ACCESS_DENIED);     /* UIPI enforcement: the caller lacks UIAccess */
    U(DestroyWindow)(w);                         /* destroying the window drops any registration */
}
TEST(dx_gap7_regptrtarget_user32_route_mouse_type_fails) {
    HWND w = g7_window(WS_POPUP, 0); REQUIRE(w);
    SetLastError(0);
    EXPECT_EQ(U(RegisterPointerInputTarget)(w, PT_MOUSE), FALSE);
    CHECK(GetLastError() == ERROR_INVALID_PARAMETER || GetLastError() == ERROR_ACCESS_DENIED);
    EXPECT_EQ(U(RegisterPointerInputTarget)((HWND)(ULONG_PTR)0x1234, PT_TOUCH), FALSE);
    U(DestroyWindow)(w);
}

/* ---- NtUserRegisterTouchHitTestingWindow(hwnd, value) ---------------------------------------------------------- */
TEST(dx_gap7_touchhittest_invalid_hwnd_fails) {
    NtUserRegisterTouchHitTestingWindow_pfn f = G7N(NtUserRegisterTouchHitTestingWindow); REQUIRE(f);
    EXPECT_EQ(f(NULL, 0), FALSE);
    EXPECT_EQ(f((HWND)(ULONG_PTR)0x1234, 1), FALSE);
}
TEST(dx_gap7_touchhittest_cross_thread_window_denied) {
    NtUserRegisterTouchHitTestingWindow_pfn f = G7N(NtUserRegisterTouchHitTestingWindow); REQUIRE(f);
    struct other o; HANDLE th = other_start(&o);
    if (!th || !o.hwnd) { other_stop(&o, th); t_skip("no helper window"); return; }
    SetLastError(0);
    EXPECT_EQ(f(o.hwnd, 0), FALSE);            /* caller thread != window's thread -> ERROR_ACCESS_DENIED */
    EXPECT_ERR(ERROR_ACCESS_DENIED);
    SetLastError(0);
    EXPECT_EQ(f(o.hwnd, 1), FALSE);
    EXPECT_ERR(ERROR_ACCESS_DENIED);
    other_stop(&o, th);
}
TEST(dx_gap7_touchhittest_own_window_register_unregister) {
    NtUserRegisterTouchHitTestingWindow_pfn f = G7N(NtUserRegisterTouchHitTestingWindow); REQUIRE(f);
    HWND w = g7_window(WS_POPUP, 0); REQUIRE(w);
    SetLastError(0);
    BOOL a = f(w, 1);
    t_note("RegisterTouchHitTestingWindow(own, 1) = %d err %lu", a, GetLastError());
    BOOL b = f(w, 0);                          /* value 0 removes the targeting window again */
    t_note("RegisterTouchHitTestingWindow(own, 0) = %d err %lu", b, GetLastError());
    EXPECT_EQ(U(IsWindow)(w), TRUE);
    U(DestroyWindow)(w);
}
TEST(dx_gap7_touchhittest_user32_route_matches_direct) {
    NtUserRegisterTouchHitTestingWindow_pfn f = G7N(NtUserRegisterTouchHitTestingWindow); REQUIRE(f);
    EXPECT_EQ(U(RegisterTouchHitTestingWindow)((HWND)(ULONG_PTR)0x1234, 0), f((HWND)(ULONG_PTR)0x1234, 0));
    struct other o; HANDLE th = other_start(&o);
    if (th && o.hwnd) {
        SetLastError(0);
        EXPECT_EQ(U(RegisterTouchHitTestingWindow)(o.hwnd, 0), FALSE);
        EXPECT_ERR(ERROR_ACCESS_DENIED);
    }
    other_stop(&o, th);
}

/* ---- NtGdiSetSystemPaletteUse ---------------------------------------------------------------------------------- */
TEST(dx_gap7_setsystempaletteuse_roundtrip_restores) {
    NtGdiSetSystemPaletteUse_pfn f = G7N(NtGdiSetSystemPaletteUse); REQUIRE(f);
    HDC dc = U(GetDC)(NULL); REQUIRE(dc);
    UINT before = U(GetSystemPaletteUse)(dc);
    int bpp = U(GetDeviceCaps)(dc, BITSPIXEL) * U(GetDeviceCaps)(dc, PLANES);
    t_note("system palette use %u, display bits per pixel %d", before, bpp);
    UINT prev = f(dc, before ? before : SYSPAL_STATIC);       /* re-apply the current value */
    t_note("SetSystemPaletteUse(same) returned %u", prev);
    UINT after = U(GetSystemPaletteUse)(dc);
    EXPECT_EQ(after, before);                                /* no net change */
    (void)bpp;
    U(ReleaseDC)(NULL, dc);
}
TEST(dx_gap7_setsystempaletteuse_invalid_dc_error) {
    NtGdiSetSystemPaletteUse_pfn f = G7N(NtGdiSetSystemPaletteUse); REQUIRE(f);
    if (t_is_wine()) { t_skip("wine hangs on an invalid DC here"); return; }
    EXPECT_EQ(f(NULL, SYSPAL_STATIC), SYSPAL_ERROR);
    EXPECT_EQ(f((HDC)(ULONG_PTR)0x00010123, SYSPAL_NOSTATIC), SYSPAL_ERROR);
    EXPECT_EQ(f((HDC)(ULONG_PTR)0x00010123, SYSPAL_STATIC), SYSPAL_ERROR);
}
TEST(dx_gap7_setsystempaletteuse_route_matches_direct) {
    NtGdiSetSystemPaletteUse_pfn f = G7N(NtGdiSetSystemPaletteUse); REQUIRE(f);
    HDC dc = U(GetDC)(NULL); REQUIRE(dc);
    UINT before = U(GetSystemPaletteUse)(dc);
    UINT a = U(SetSystemPaletteUse)(dc, before ? before : SYSPAL_STATIC);
    UINT b = f(dc, before ? before : SYSPAL_STATIC);
    EXPECT_EQ(a, b);
    EXPECT_EQ(U(GetSystemPaletteUse)(dc), before);
    U(ReleaseDC)(NULL, dc);
}
