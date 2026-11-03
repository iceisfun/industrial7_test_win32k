/* Touch windows, touch input handles and gesture configuration.
 *
 * win32kfull!NtUserModifyWindowTouchCapability(hwnd, flags, add): ValidateHwnd first (failure =
 * ERROR_INVALID_WINDOW_HANDLE, the HMValidateHandle(type 1) code), then flags & ~3 -> ERROR_INVALID_FLAGS
 * (1004), then the window must belong to the calling thread -> ERROR_ACCESS_DENIED, then the flags (plus the
 * internal 0x10000 "registered" marker) are stored as a window property (gatomTouchFlags) or removed.
 * user32!RegisterTouchWindow = Modify(hwnd, flags, 1); UnregisterTouchWindow = Modify(hwnd, 0, 0).
 * win32kfull!NtUserIsTouchWindow reads that property back (marker bit stripped).
 */
#include "rit.h"

UDECL(BOOL, UnregisterTouchWindow, (HWND));

static HWND mkwin(void) { return k_make_window("touch"); }

static void touch_ok(ULONG flags) {
    REQUIRE(U(RegisterTouchWindow));
    REQUIRE(U(IsTouchWindow));
    HWND h = mkwin(); if (!h) { t_skip("no window"); return; }
    ULONG got = 0x77;
    CHECK(!U(IsTouchWindow)(h, &got));              /* nothing registered yet */
    CHECK(got == 0x77);
    SET_ERR(0);
    CHECK(U(RegisterTouchWindow)(h, flags));
    got = 0x77;
    CHECK(U(IsTouchWindow)(h, &got));
    CHECK(got == flags);                             /* property = flags | 0x10000, marker removed on read */
    CHECK(U(IsTouchWindow)(h, NULL));                /* pulFlags is optional */
    U(DestroyWindow)(h);
}
RT(touch_register_flags_0) { touch_ok(0); }
RT(touch_register_flags_finetouch) { touch_ok(TWF_FINETOUCH); }
RT(touch_register_flags_wantpalm) { touch_ok(TWF_WANTPALM); }
RT(touch_register_flags_both) { touch_ok(TWF_FINETOUCH | TWF_WANTPALM); }

static void touch_badflags(ULONG flags) {
    REQUIRE(U(RegisterTouchWindow));
    HWND h = mkwin(); if (!h) { t_skip("no window"); return; }
    SET_ERR(0);
    CHECK(!U(RegisterTouchWindow)(h, flags));
    EXPECT_ERR(ERROR_INVALID_FLAGS);                /* (flags & 0xfffffffc) != 0 */
    CHECK(!U(IsTouchWindow)(h, NULL));              /* nothing got registered */
    U(DestroyWindow)(h);
}
RT(touch_flags_4)          { touch_badflags(4); }
RT(touch_flags_8)          { touch_badflags(8); }
RT(touch_flags_0x10)       { touch_badflags(0x10); }
RT(touch_flags_0x100)      { touch_badflags(0x100); }
RT(touch_flags_0x10000)    { touch_badflags(0x10000); }       /* the internal marker bit is not accepted from callers */
RT(touch_flags_0x80000000) { touch_badflags(0x80000000); }
RT(touch_flags_0xfffffffc) { touch_badflags(0xfffffffc); }
RT(touch_flags_0xffffffff) { touch_badflags(0xffffffff); }
RT(touch_flags_7)          { touch_badflags(7); }

RT(touch_reregister_changes_flags) {
    REQUIRE(U(RegisterTouchWindow));
    HWND h = mkwin(); if (!h) { t_skip("no window"); return; }
    ULONG g = 0;
    CHECK(U(RegisterTouchWindow)(h, TWF_FINETOUCH));
    CHECK(U(RegisterTouchWindow)(h, TWF_WANTPALM));
    CHECK(U(IsTouchWindow)(h, &g)); CHECK(g == TWF_WANTPALM);
    CHECK(U(RegisterTouchWindow)(h, 0));
    CHECK(U(IsTouchWindow)(h, &g)); CHECK(g == 0);
    U(DestroyWindow)(h);
}
RT(touch_failed_register_keeps_old_flags) {
    REQUIRE(U(RegisterTouchWindow));
    HWND h = mkwin(); if (!h) { t_skip("no window"); return; }
    ULONG g = 0;
    CHECK(U(RegisterTouchWindow)(h, TWF_FINETOUCH));
    CHECK(!U(RegisterTouchWindow)(h, 0x40));
    CHECK(U(IsTouchWindow)(h, &g)); CHECK(g == TWF_FINETOUCH);
    U(DestroyWindow)(h);
}
RT(touch_unregister_wrapper) {
    REQUIRE(U(RegisterTouchWindow));
    REQUIRE(UN(UnregisterTouchWindow));
    HWND h = mkwin(); if (!h) { t_skip("no window"); return; }
    CHECK(U(RegisterTouchWindow)(h, TWF_WANTPALM));
    CHECK(U(IsTouchWindow)(h, NULL));
    CHECK(UN(UnregisterTouchWindow)(h));
    ULONG g = 0x55;
    CHECK(!U(IsTouchWindow)(h, &g));
    CHECK(g == 0x55);
    CHECK(UN(UnregisterTouchWindow)(h));
    U(DestroyWindow)(h);
}
RT(touch_direct_modify_add_remove) {
    REQUIRE(UN(NtUserModifyWindowTouchCapability));
    REQUIRE(UN(NtUserIsTouchWindow));
    HWND h = mkwin(); if (!h) { t_skip("no window"); return; }
    ULONG g = 0;
    CHECK(UN(NtUserModifyWindowTouchCapability)(h, TWF_FINETOUCH, 1) != 0);
    CHECK(UN(NtUserIsTouchWindow)(h, &g)); CHECK(g == TWF_FINETOUCH);
    /* add == 0 ignores the flags and removes the property */
    CHECK(UN(NtUserModifyWindowTouchCapability)(h, TWF_FINETOUCH, 0) != 0);
    CHECK(!UN(NtUserIsTouchWindow)(h, &g));
    /* flags are validated even when removing */
    SET_ERR(0);
    CHECK(UN(NtUserModifyWindowTouchCapability)(h, 0x20, 0) == 0);
    EXPECT_ERR(ERROR_INVALID_FLAGS);
    U(DestroyWindow)(h);
}
RT(touch_isTouchWindow_null_out_unregistered) {
    REQUIRE(U(IsTouchWindow));
    HWND h = mkwin(); if (!h) { t_skip("no window"); return; }
    CHECK(!U(IsTouchWindow)(h, NULL));
    U(DestroyWindow)(h);
}

RT(touch_invalid_hwnd) {
    REQUIRE(U(RegisterTouchWindow));
    HWND bad[] = { NULL, (HWND)(ULONG_PTR)1, (HWND)(ULONG_PTR)0x7fff1234, (HWND)(ULONG_PTR)-1 };
    for (unsigned i = 0; i < 4; i++) {
        SET_ERR(0);
        CHECKF(!U(RegisterTouchWindow)(bad[i], 0), "hwnd %p", bad[i]);
        EXPECT_ERR(E_INVALID_HWND);
        CHECKF(!U(IsTouchWindow)(bad[i], NULL), "hwnd %p", bad[i]);
    }
}
RT(touch_invalid_hwnd_beats_bad_flags) {
    REQUIRE(U(RegisterTouchWindow));
    SET_ERR(0);
    CHECK(!U(RegisterTouchWindow)((HWND)(ULONG_PTR)0x7fff1234, 0x80));
    EXPECT_ERR(E_INVALID_HWND);                     /* ValidateHwnd precedes the flags test */
}
RT(touch_destroyed_window) {
    REQUIRE(U(RegisterTouchWindow));
    HWND h = mkwin(); if (!h) { t_skip("no window"); return; }
    CHECK(U(RegisterTouchWindow)(h, 0));
    U(DestroyWindow)(h);
    SET_ERR(0);
    CHECK(!U(RegisterTouchWindow)(h, 0));
    EXPECT_ERR(E_INVALID_HWND);
    CHECK(!U(IsTouchWindow)(h, NULL));
}
RT(touch_desktop_window_refused) {
    REQUIRE(U(RegisterTouchWindow));
    SET_ERR(0);
    HWND dw = U(GetDesktopWindow)();
    CHECK(!U(RegisterTouchWindow)(dw, 0));          /* belongs to another thread/process */
    DWORD e = GetLastError();
    CHECK(e == E_ACCESS_DENIED || e == E_INVALID_HWND);
    t_note("RegisterTouchWindow(desktop) err %lu", e);
}
RT(touch_other_thread_window) {
    REQUIRE(U(RegisterTouchWindow));
    rit_owner o;
    if (!rit_owner_start(&o)) { t_skip("owner thread"); return; }
    SET_ERR(0);
    /* the window is valid and in our process but owned by another thread: the pti equality test fails */
    CHECK(!U(RegisterTouchWindow)(o.hwnd, 0));
    EXPECT_ERR(E_ACCESS_DENIED);
    SET_ERR(0);
    CHECK(!U(RegisterTouchWindow)(o.hwnd, TWF_FINETOUCH));
    EXPECT_ERR(E_ACCESS_DENIED);
    if (UN(UnregisterTouchWindow)) {
        SET_ERR(0);
        CHECK(!UN(UnregisterTouchWindow)(o.hwnd));
        EXPECT_ERR(E_ACCESS_DENIED);
    }
    SET_ERR(0);
    /* flag validation (1004) happens before the ownership test (5) */
    CHECK(!U(RegisterTouchWindow)(o.hwnd, 0x10));
    EXPECT_ERR(ERROR_INVALID_FLAGS);
    CHECK(!U(IsTouchWindow)(o.hwnd, NULL));          /* IsTouchWindow has no thread restriction */
    rit_owner_stop(&o);
}

typedef struct { HWND h; volatile LONG registered; ULONG flags; HANDLE ready, quit; } selfreg;
static DWORD WINAPI selfreg_main(LPVOID p) {
    selfreg *s = p;
    s->h = k_make_window("touch-self");
    if (s->h) s->registered = U(RegisterTouchWindow)(s->h, s->flags);
    SetEvent(s->ready);
    while (WaitForSingleObject(s->quit, 20) == WAIT_TIMEOUT) k_pump(1);
    if (s->h) U(DestroyWindow)(s->h);
    return 0;
}
RT(touch_state_visible_across_threads) {
    REQUIRE(U(RegisterTouchWindow));
    selfreg s; memset(&s, 0, sizeof s);
    s.flags = TWF_WANTPALM;
    s.ready = CreateEventW(NULL, TRUE, FALSE, NULL); s.quit = CreateEventW(NULL, TRUE, FALSE, NULL);
    HANDLE t = CreateThread(NULL, 0, selfreg_main, &s, 0, NULL);
    WaitForSingleObject(s.ready, 5000);
    CHECK(s.registered);
    ULONG g = 0;
    CHECK(U(IsTouchWindow)(s.h, &g));
    CHECK(g == TWF_WANTPALM);
    SetEvent(s.quit); WaitForSingleObject(t, 5000); CloseHandle(t); CloseHandle(s.ready); CloseHandle(s.quit);
}
RT(touch_property_listed_on_window) {
    REQUIRE(U(RegisterTouchWindow));
    REQUIRE(U(EnumPropsExW));
    HWND h = mkwin(); if (!h) { t_skip("no window"); return; }
    struct cnt { int n; } c = {0};
    BOOL CALLBACK cb(HWND w, LPWSTR s, HANDLE d, ULONG_PTR p) { (*(int *)p)++; return TRUE; }
    U(EnumPropsExW)(h, (PROPENUMPROCEXW)cb, (LPARAM)&c.n);
    int before = c.n; c.n = 0;
    CHECK(U(RegisterTouchWindow)(h, 0));
    U(EnumPropsExW)(h, (PROPENUMPROCEXW)cb, (LPARAM)&c.n);
    t_note("window property count %d -> %d after RegisterTouchWindow", before, c.n);
    U(DestroyWindow)(h);
}

/* ---- touch hit testing window -------------------------------------------------------------- */
RT(touch_hittest_invalid_hwnd) {
    REQUIRE(U(RegisterTouchHitTestingWindow));
    SET_ERR(0);
    CHECK(!U(RegisterTouchHitTestingWindow)(NULL, 0));
    EXPECT_ERR(E_INVALID_HWND);
    SET_ERR(0);
    CHECK(!U(RegisterTouchHitTestingWindow)((HWND)(ULONG_PTR)0x7fff1234, 1));
    EXPECT_ERR(E_INVALID_HWND);
}
RT(touch_hittest_values) {
    REQUIRE(U(RegisterTouchHitTestingWindow));
    HWND h = mkwin(); if (!h) { t_skip("no window"); return; }
    /* value 0 removes the targeting property (always success); 1 and 2 set it through InternalSetProp */
    CHECK(U(RegisterTouchHitTestingWindow)(h, 0));
    CHECK(U(RegisterTouchHitTestingWindow)(h, 1));
    CHECK(U(RegisterTouchHitTestingWindow)(h, 2));
    CHECK(U(RegisterTouchHitTestingWindow)(h, 0));
    U(DestroyWindow)(h);
}
RT(touch_hittest_other_thread_5) {
    REQUIRE(U(RegisterTouchHitTestingWindow));
    rit_owner o;
    if (!rit_owner_start(&o)) { t_skip("owner thread"); return; }
    SET_ERR(0);
    CHECK(!U(RegisterTouchHitTestingWindow)(o.hwnd, 1));
    EXPECT_ERR(E_ACCESS_DENIED);
    SET_ERR(0);
    CHECK(!U(RegisterTouchHitTestingWindow)(o.hwnd, 0));
    EXPECT_ERR(E_ACCESS_DENIED);
    rit_owner_stop(&o);
}

/* ---- touch input handles ------------------------------------------------------------------- */
RT(touchinput_wrapper_cbsize) {
    REQUIRE(U(GetTouchInputInfo));
    TOUCHINPUT ti[2];
    UINT bad[] = { 0, 8, 0x2f, 0x31, 0x60 };
    for (unsigned i = 0; i < 5; i++) {
        SET_ERR(0);
        CHECK(!U(GetTouchInputInfo)((HTOUCHINPUT)(ULONG_PTR)0x1234, 1, ti, bad[i]));
        EXPECT_ERR(E_INVALID_PARAM);
    }
    CHECK(sizeof(TOUCHINPUT) == 0x30);
}
RT(touchinput_wrapper_args) {
    REQUIRE(U(GetTouchInputInfo));
    TOUCHINPUT ti[2];
    SET_ERR(0);
    CHECK(!U(GetTouchInputInfo)((HTOUCHINPUT)(ULONG_PTR)0x1234, 0, ti, sizeof ti[0]));
    EXPECT_ERR(E_INVALID_PARAM);
    SET_ERR(0);
    CHECK(!U(GetTouchInputInfo)((HTOUCHINPUT)(ULONG_PTR)0x1234, 1, NULL, sizeof ti[0]));
    EXPECT_ERR(E_INVALID_PARAM);
}
RT(touchinput_wrapper_bad_handle) {
    REQUIRE(U(GetTouchInputInfo));
    TOUCHINPUT ti[2]; memset(ti, 0x44, sizeof ti);
    ULONG_PTR hs[] = { 0, 1, 0x1234, 0xffff, (ULONG_PTR)-1 };
    for (unsigned i = 0; i < 5; i++) {
        SET_ERR(0);
        /* user32 keeps its own handle list (UMHandleActiveEntryFromHandle): miss -> ERROR_INVALID_HANDLE */
        CHECK(!U(GetTouchInputInfo)((HTOUCHINPUT)hs[i], 2, ti, sizeof ti[0]));
        EXPECT_ERR(E_INVALID_HANDLE);
    }
    CHECK(((unsigned char *)ti)[0] == 0x44);
}
RT(touchinput_close_bad_handle) {
    REQUIRE(U(CloseTouchInputHandle));
    CHECK(!U(CloseTouchInputHandle)(NULL));
    ULONG_PTR hs[] = { 1, 0x1234, 0xffff, (ULONG_PTR)-1 };
    for (unsigned i = 0; i < 4; i++) {
        SET_ERR(0);
        CHECK(!U(CloseTouchInputHandle)((HTOUCHINPUT)hs[i]));
        EXPECT_ERR(E_INVALID_HANDLE);
    }
}
RT(touchinput_direct_arg_order) {
    REQUIRE(UN(NtUserGetTouchInputInfo));
    rit_gui();
    char buf[0x60];
    SET_ERR(0);
    CHECK(!UN(NtUserGetTouchInputInfo)((HANDLE)0x1234, 1, NULL, 0x30));
    EXPECT_ERR(E_INVALID_PARAM);
    SET_ERR(0);
    CHECK(!UN(NtUserGetTouchInputInfo)((HANDLE)0x1234, 1, buf, 0x2f));
    EXPECT_ERR(E_INVALID_PARAM);
    SET_ERR(0);
    CHECK(!UN(NtUserGetTouchInputInfo)((HANDLE)0x1234, 1, buf, 0x60));
    EXPECT_ERR(E_INVALID_PARAM);
    SET_ERR(0);
    /* args fine: the kernel-side handle (type 0x14) fails HMValidateHandle -> ERROR_INVALID_HANDLE */
    CHECK(!UN(NtUserGetTouchInputInfo)((HANDLE)0x1234, 1, buf, 0x30));
    EXPECT_ERR(E_INVALID_HANDLE);
    SET_ERR(0);
    CHECK(!UN(NtUserGetTouchInputInfo)(NULL, 1, buf, 0x30));
    EXPECT_ERR(E_INVALID_HANDLE);
}
RT(touchinput_validation_status_quirk) {
    WINE_UNIMPL("NtUserGetTouchValidationStatus");
    REQUIRE(UN(NtUserGetTouchValidationStatus));
    rit_gui();
    SET_ERR(0x1234);
    BOOL r = UN(NtUserGetTouchValidationStatus)((HANDLE)0x1234);
    /* a bad non-NULL handle: SetLastError(6) is immediately overwritten with SetLastError(0) and 0 is returned */
    CHECK(r == 0);
    CHECK(GetLastError() == 0);
    r = UN(NtUserGetTouchValidationStatus)(NULL);
    t_note("touch validation status of NULL input -> %d", r);
}

/* ---- gesture info -------------------------------------------------------------------------- */
RT(gesture_info_args) {
    REQUIRE(U(GetGestureInfo));
    GESTUREINFO gi; memset(&gi, 0, sizeof gi);
    CHECK(sizeof gi == 0x38);
    SET_ERR(0);
    CHECK(!U(GetGestureInfo)((HGESTUREINFO)(ULONG_PTR)0x1234, NULL));
    EXPECT_ERR(E_INVALID_PARAM);
    UINT bad[] = { 0, 0x30, 0x37, 0x39, 0x40 };
    for (unsigned i = 0; i < 5; i++) {
        gi.cbSize = bad[i];
        SET_ERR(0);
        CHECK(!U(GetGestureInfo)((HGESTUREINFO)(ULONG_PTR)0x1234, &gi));
        EXPECT_ERR(E_INVALID_PARAM);
    }
}
RT(gesture_info_bad_handle) {
    REQUIRE(U(GetGestureInfo));
    GESTUREINFO gi; memset(&gi, 0, sizeof gi); gi.cbSize = sizeof gi;
    ULONG_PTR hs[] = { 0, 1, 0x1234, (ULONG_PTR)-1 };
    for (unsigned i = 0; i < 4; i++) {
        SET_ERR(0);
        CHECK(!U(GetGestureInfo)((HGESTUREINFO)hs[i], &gi));
        EXPECT_ERR(E_INVALID_HANDLE);
        CHECK(gi.cbSize == sizeof gi && gi.dwFlags == 0);
    }
}
RT(gesture_close_bad_handle) {
    REQUIRE(U(CloseGestureInfoHandle));
    CHECK(!U(CloseGestureInfoHandle)(NULL));
    SET_ERR(0);
    CHECK(!U(CloseGestureInfoHandle)((HGESTUREINFO)(ULONG_PTR)0x1234));
    EXPECT_ERR(E_INVALID_HANDLE);
}
RT(gesture_info_direct_stub) {
    WINE_UNIMPL("NtUserGetGestureInfo");
    REQUIRE(UN(NtUserGetGestureInfo));
    rit_gui();
    GESTUREINFO gi; memset(&gi, 0, sizeof gi);
    SET_ERR(0);
    CHECK(!UN(NtUserGetGestureInfo)((HANDLE)0x1234, NULL));
    EXPECT_ERR(E_INVALID_PARAM);
    gi.cbSize = 0x30;
    SET_ERR(0);
    CHECK(!UN(NtUserGetGestureInfo)((HANDLE)0x1234, &gi));
    EXPECT_ERR(E_INVALID_PARAM);
    gi.cbSize = 0x38;
    SET_ERR(0);
    CHECK(!UN(NtUserGetGestureInfo)((HANDLE)0x1234, &gi));
    EXPECT_ERR(E_INVALID_HANDLE);                  /* HMValidateHandle(h, 0x15) */
}
RT(gesture_extargs_direct_stub) {
    WINE_UNIMPL("NtUserGetGestureExtArgs");
    REQUIRE(UN(NtUserGetGestureExtArgs));
    rit_gui();
    char b[16];
    SET_ERR(0);
    CHECK(!UN(NtUserGetGestureExtArgs)((HANDLE)0x1234, 16, NULL));
    EXPECT_ERR(E_INVALID_PARAM);
    SET_ERR(0);
    CHECK(!UN(NtUserGetGestureExtArgs)((HANDLE)0x1234, 16, b));
    EXPECT_ERR(E_INVALID_HANDLE);
}

/* ---- gesture configuration ----------------------------------------------------------------- */
static GESTURECONFIG gc(DWORD id, DWORD want, DWORD block) { GESTURECONFIG g = { id, want, block }; return g; }

RT(gesture_config_sizeof) { CHECK(sizeof(GESTURECONFIG) == 12); }
RT(gesture_set_invalid_hwnd) {
    REQUIRE(U(SetGestureConfig));
    GESTURECONFIG g = gc(GID_ZOOM, GC_ZOOM, 0);
    HWND bad[] = { NULL, (HWND)(ULONG_PTR)0x7fff1234 };
    for (unsigned i = 0; i < 2; i++) {
        SET_ERR(0);
        CHECK(!U(SetGestureConfig)(bad[i], 0, 1, &g, sizeof g));
        EXPECT_ERR(E_INVALID_HWND);
        UINT n = 1;
        SET_ERR(0);
        CHECK(!U(GetGestureConfig)(bad[i], 0, 0, &n, &g, sizeof g));
        EXPECT_ERR(E_INVALID_HWND);
    }
    /* hwnd is validated before the argument checks */
    SET_ERR(0);
    CHECK(!U(SetGestureConfig)((HWND)(ULONG_PTR)0x7fff1234, 0, 0, NULL, 0));
    EXPECT_ERR(E_INVALID_HWND);
}
RT(gesture_set_bad_args) {
    REQUIRE(U(SetGestureConfig));
    HWND h = mkwin(); if (!h) { t_skip("no window"); return; }
    GESTURECONFIG g[2] = { gc(GID_ZOOM, GC_ZOOM, 0), gc(GID_PAN, GC_PAN, 0) };
    struct { UINT n; void *p; UINT cb; } c[] = {
        { 1, NULL, 12 }, { 1, g, 0 }, { 1, g, 8 }, { 1, g, 16 }, { 1, g, 24 }, { 0, g, 12 }, { 257, g, 12 }, { 0xffffffff, g, 12 },
    };
    for (unsigned i = 0; i < sizeof c / sizeof c[0]; i++) {
        SET_ERR(0);
 /* argument 4 == NULL, cbSize != 12 or cIDs outside [1,256] -> 87 */
        CHECKF(!U(SetGestureConfig)(h, 0, c[i].n, c[i].p, c[i].cb), "case %u", i);
        EXPECT_ERR(E_INVALID_PARAM);
    }
    U(DestroyWindow)(h);
}
RT(gesture_set_bad_entries) {
    REQUIRE(U(SetGestureConfig));
    HWND h = mkwin(); if (!h) { t_skip("no window"); return; }
    GESTURECONFIG ids[][2] = {
        { gc(8, 1, 0), gc(0, 0, 0) },                    /* id > 7 */
        { gc(0xffffffff, 1, 0), gc(0, 0, 0) },
        { gc(GID_PAN, 1, 1), gc(0, 0, 0) },               /* want & block overlap */
        { gc(0, 2, 0), gc(0, 0, 0) },                     /* global config must be exactly want=1 xor block=1 */
        { gc(0, 1, 1), gc(0, 0, 0) },
        { gc(0, 0, 2), gc(0, 0, 0) },
    };
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        SET_ERR(0);
        CHECKF(!U(SetGestureConfig)(h, 0, 1, ids[i], sizeof(GESTURECONFIG)), "case %u", i);
        EXPECT_ERR(E_INVALID_PARAM);
    }
    GESTURECONFIG dup[2] = { gc(GID_ZOOM, GC_ZOOM, 0), gc(GID_ZOOM, GC_ZOOM, 0) };
    SET_ERR(0);
    CHECK(!U(SetGestureConfig)(h, 0, 2, dup, sizeof dup[0]));     /* duplicate ids rejected by SortGestureConfigArray */
    EXPECT_ERR(E_INVALID_PARAM);
    U(DestroyWindow)(h);
}
RT(gesture_get_bad_args) {
    REQUIRE(U(GetGestureConfig));
    HWND h = mkwin(); if (!h) { t_skip("no window"); return; }
    GESTURECONFIG g[2] = { gc(GID_ZOOM, 0, 0), gc(GID_PAN, 0, 0) };
    UINT n = 1;
    SET_ERR(0);
    CHECK(!U(GetGestureConfig)(h, 0, 0, &n, g, 8));
    EXPECT_ERR(E_INVALID_PARAM);
    SET_ERR(0);
    CHECK(!U(GetGestureConfig)(h, 0, 0, &n, g, 16));
    EXPECT_ERR(E_INVALID_PARAM);
    SET_ERR(0);
    CHECK(!U(GetGestureConfig)(h, 0, 0, &n, NULL, 12));
    EXPECT_ERR(E_INVALID_PARAM);
    n = 0;
    SET_ERR(0);
    CHECK(!U(GetGestureConfig)(h, 0, 0, &n, g, 12));
    EXPECT_ERR(E_INVALID_PARAM);
    n = 257;
    SET_ERR(0);
    CHECK(!U(GetGestureConfig)(h, 0, 0, &n, g, 12));
    EXPECT_ERR(E_INVALID_PARAM);
    U(DestroyWindow)(h);
}
RT(gesture_get_bad_flags) {
    REQUIRE(U(GetGestureConfig));
    HWND h = mkwin(); if (!h) { t_skip("no window"); return; }
    GESTURECONFIG g = gc(GID_ZOOM, 0, 0);
    UINT n = 1;
    DWORD flags[] = { 2, 4, 0x80, 0x80000000, 0xfffffffe };
    for (unsigned i = 0; i < 5; i++) {
        SET_ERR(0);
        /* (flags & ~GCF_INCLUDE_ANCESTORS) != 0 */
        CHECKF(!U(GetGestureConfig)(h, 0, flags[i], &n, &g, 12), "flags 0x%lx", flags[i]);
        EXPECT_ERR(ERROR_INVALID_FLAGS);
    }
    U(DestroyWindow)(h);
}
RT(gesture_get_bad_ids) {
    REQUIRE(U(GetGestureConfig));
    HWND h = mkwin(); if (!h) { t_skip("no window"); return; }
    DWORD ids[] = { 0, 8, 100, 0xffffffff };
    for (unsigned i = 0; i < 4; i++) {
        GESTURECONFIG g = gc(ids[i], 0, 0);
        UINT n = 1;
        SET_ERR(0);
        /* each requested id must be in 1..7 (GetGestureConfigSettings) */
        CHECKF(!U(GetGestureConfig)(h, 0, 0, &n, &g, 12), "id %lu", ids[i]);
        EXPECT_ERR(E_INVALID_PARAM);
    }
    U(DestroyWindow)(h);
}
RT(gesture_get_default_zero) {
    REQUIRE(U(GetGestureConfig));
    HWND h = mkwin(); if (!h) { t_skip("no window"); return; }
    GESTURECONFIG g[3] = { gc(GID_ZOOM, 0x55, 0x66), gc(GID_PAN, 0x55, 0x66), gc(GID_ROTATE, 0x55, 0x66) };
    UINT n = 3;
    CHECK(U(GetGestureConfig)(h, 0, 0, &n, g, 12));
    for (int i = 0; i < 3; i++) { CHECK(g[i].dwWant == 0); CHECK(g[i].dwBlock == 0); }   /* GetGCData == NULL: zero-filled */
    CHECK(g[0].dwID == GID_ZOOM && g[1].dwID == GID_PAN && g[2].dwID == GID_ROTATE);
    U(DestroyWindow)(h);
}
RT(gesture_set_get_roundtrip_pan) {
    REQUIRE(U(SetGestureConfig));
    HWND h = mkwin(); if (!h) { t_skip("no window"); return; }
    GESTURECONFIG s = gc(GID_PAN, GC_PAN | GC_PAN_WITH_SINGLE_FINGER_VERTICALLY, GC_PAN_WITH_GUTTER);
    CHECK(U(SetGestureConfig)(h, 0, 1, &s, sizeof s));
    GESTURECONFIG g = gc(GID_PAN, 0, 0);
    UINT n = 1;
    CHECK(U(GetGestureConfig)(h, 0, 0, &n, &g, sizeof g));
    CHECK(g.dwWant == s.dwWant);                    /* _StoreGestureConfig keeps want/block verbatim per id */
    CHECK(g.dwBlock == s.dwBlock);
    GESTURECONFIG z = gc(GID_ZOOM, 0x55, 0x55);
    CHECK(U(GetGestureConfig)(h, 0, 0, &n, &z, sizeof z));
    CHECK(z.dwWant == 0 && z.dwBlock == 0);        /* other ids untouched */
    GESTURECONFIG clr = gc(GID_PAN, 0, 0);
    CHECK(U(SetGestureConfig)(h, 0, 1, &clr, sizeof clr));   /* want = block = 0 removes the entry */
    CHECK(U(GetGestureConfig)(h, 0, 0, &n, &g, sizeof g));
    CHECK(g.dwWant == 0 && g.dwBlock == 0);
    U(DestroyWindow)(h);
}
RT(gesture_set_get_multi_sorted) {
    REQUIRE(U(SetGestureConfig));
    HWND h = mkwin(); if (!h) { t_skip("no window"); return; }
    /* ids are sorted by SortGestureConfigArray before storing; input order is irrelevant */
    GESTURECONFIG s[3] = { gc(GID_ROTATE, GC_ROTATE, 0), gc(GID_ZOOM, 0, GC_ZOOM), gc(GID_TWOFINGERTAP, GC_TWOFINGERTAP, 0) };
    CHECK(U(SetGestureConfig)(h, 0, 3, s, sizeof s[0]));
    GESTURECONFIG g[3] = { gc(GID_TWOFINGERTAP, 0, 0), gc(GID_ROTATE, 0, 0), gc(GID_ZOOM, 0, 0) };
    UINT n = 3;
    CHECK(U(GetGestureConfig)(h, 0, 0, &n, g, sizeof g[0]));
    CHECK(g[0].dwWant == GC_TWOFINGERTAP && g[0].dwBlock == 0);
    CHECK(g[1].dwWant == GC_ROTATE && g[1].dwBlock == 0);
    CHECK(g[2].dwWant == 0 && g[2].dwBlock == GC_ZOOM);
    /* {0,0,0} is not a way to clear the window's configuration: the global form needs exactly one of want/block set */
    GESTURECONFIG off = gc(0, 0, 0);
    SET_ERR(0);
    CHECK(!U(SetGestureConfig)(h, 0, 1, &off, sizeof off));
    EXPECT_ERR(E_INVALID_PARAM);
    GESTURECONFIG clr[3] = { gc(GID_ZOOM, 0, 0), gc(GID_ROTATE, 0, 0), gc(GID_TWOFINGERTAP, 0, 0) };
    CHECK(U(SetGestureConfig)(h, 0, 3, clr, sizeof clr[0]));   /* want = block = 0 per id removes the entries */
    CHECK(U(GetGestureConfig)(h, 0, 0, &n, g, sizeof g[0]));
    CHECK(g[0].dwWant == 0 && g[1].dwWant == 0 && g[2].dwBlock == 0);
    U(DestroyWindow)(h);
}
RT(gesture_set_global_all_and_block) {
    REQUIRE(U(SetGestureConfig));
    HWND h = mkwin(); if (!h) { t_skip("no window"); return; }
    GESTURECONFIG all = gc(0, GC_ALLGESTURES, 0), none = gc(0, 0, GC_ALLGESTURES), g;
    UINT n = 1;
    CHECK(U(SetGestureConfig)(h, 0, 1, &all, sizeof all));
    g = gc(GID_ZOOM, 0, 0);
    CHECK(U(GetGestureConfig)(h, 0, 0, &n, &g, sizeof g));
    CHECK(g.dwWant != 0 && g.dwBlock == 0);        /* global "want all": want = per-id valid mask, block 0 */
    CHECK(U(SetGestureConfig)(h, 0, 1, &none, sizeof none));
    g = gc(GID_ZOOM, 0, 0);
    CHECK(U(GetGestureConfig)(h, 0, 0, &n, &g, sizeof g));
    CHECK(g.dwWant == 0 && g.dwBlock != 0);        /* global "block all": block = per-id valid mask */
    GESTURECONFIG off = gc(0, 0, 0);                  /* not a valid global form: rejected, the block-all state stays until the window dies */
    SET_ERR(0);
    CHECK(!U(SetGestureConfig)(h, 0, 1, &off, sizeof off));
    EXPECT_ERR(E_INVALID_PARAM);
    U(DestroyWindow)(h);
}
RT(gesture_set_other_thread_window) {
    REQUIRE(U(SetGestureConfig));
    rit_owner o;
    if (!rit_owner_start(&o)) { t_skip("owner thread"); return; }
    GESTURECONFIG s = gc(GID_ZOOM, GC_ZOOM, 0);
    /* same process, other thread: the check is on the owning *process* (ppi), so this succeeds */
    BOOL r = U(SetGestureConfig)(o.hwnd, 0, 1, &s, sizeof s);
    CHECK(r);
    GESTURECONFIG off = gc(0, 0, 0);
    if (r) U(SetGestureConfig)(o.hwnd, 0, 1, &off, sizeof off);
    rit_owner_stop(&o);
}
RT(gesture_set_desktop_window_refused) {
    REQUIRE(U(SetGestureConfig));
    GESTURECONFIG s = gc(GID_ZOOM, GC_ZOOM, 0);
    SET_ERR(0);
    CHECK(!U(SetGestureConfig)(U(GetDesktopWindow)(), 0, 1, &s, sizeof s));   /* window of another process */
    t_note("SetGestureConfig(desktop) err %lu", GetLastError());
}
RT(gesture_direct_stub_matches_user32) {
    REQUIRE(UN(NtUserSetGestureConfig));
    REQUIRE(UN(NtUserGetGestureConfig));
    HWND h = mkwin(); if (!h) { t_skip("no window"); return; }
    GESTURECONFIG s = gc(GID_ROTATE, GC_ROTATE, 0), g = gc(GID_ROTATE, 0, 0);
    UINT n = 1;
    CHECK(UN(NtUserSetGestureConfig)(h, 0, 1, &s, 12) != 0);
    CHECK(UN(NtUserGetGestureConfig)(h, 0, 0, &n, &g, 12) != 0);
    CHECK(g.dwWant == GC_ROTATE);
    SET_ERR(0);
    CHECK(UN(NtUserGetGestureConfig)(h, 0, 2, &n, &g, 12) == 0);
    EXPECT_ERR(ERROR_INVALID_FLAGS);
    GESTURECONFIG off = gc(0, 0, 0);
    UN(NtUserSetGestureConfig)(h, 0, 1, &off, 12);
    U(DestroyWindow)(h);
}
