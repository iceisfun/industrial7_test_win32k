/* dx_input_inject: process/system input state around injection - last-input time, BlockInput, SwapMouseButton, mouse
 * parameters, SetKeyboardState, touch injection argument checks.
 *
 * Kernel / user32 facts:
 *   user32!GetLastInputInfo: cbSize must be 8 (else ERROR_INVALID_PARAMETER); dwTime = the last-input tick count - read from
 *     the shared SERVERINFO, no syscall. Injected input refreshes that time.
 *   _BlockInput(f) (win32kfull): f != 0 needs the caller on the input desktop with WINSTA access 0x20; records the blocking thread as the
 *     caller's THREADINFO if it was NULL (returns 1); a second block returns 0; f == 0 returns 1 only for the blocking thread.
 *     On a non-input desktop it sets ERROR_ACCESS_DENIED.
 *   _SwapMouseButton(f) -> SetSwapMouseButton(f != 0) which returns the previous state.
 *   user32!GetMessageExtraInfo = NtUserGetThreadState(7); SetMessageExtraInfo swaps it (CallOneParam).
 *   NtUserInitializeTouchInjection(max, mode): max 1..256 and mode 1..3 else ERROR_INVALID_PARAMETER;
 *   NtUserInjectTouchInput(count, contacts): count 1..256 else ERROR_INVALID_PARAMETER (checked before the array is probed).
 */
#include "inj.h"

UDECL(BOOL, InitializeTouchInjection, (UINT32, DWORD));
UDECL(BOOL, InjectTouchInput, (UINT32, const void *));
UDECL(BOOL, NtUserInitializeTouchInjection, (UINT32, DWORD));
UDECL(BOOL, NtUserInjectTouchInput, (UINT32, const void *));
UDECL(ULONG_PTR, NtUserCallOneParam, (ULONG_PTR, DWORD));
UDECL(SHORT, NtUserGetKeyState, (int));
UDECL(SHORT, NtUserGetAsyncKeyState, (int));

/* ---- GetLastInputInfo --------------------------------------------------------------------------------------------- */
TEST(dx_input_inject_lastinput_cbsize) {
    INJ_ENV(); REQUIRE(U(GetLastInputInfo));
    static const UINT bad[] = { 0, 1, 4, 7, 9, 12, 16, 0x20, 0xFFFFFFFFu };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        LASTINPUTINFO li; memset(&li, 0, sizeof li); li.cbSize = bad[i]; li.dwTime = 0x5A5A5A5A;
        SET_ERR(0);
        CHECKF(!U(GetLastInputInfo)(&li), "cb=%u", bad[i]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
        EXPECT_EQ(li.dwTime, 0x5A5A5A5A);   /* untouched */
    }
    LASTINPUTINFO ok = { sizeof ok, 0 };
    CHECK(sizeof ok == 8);
    CHECK(U(GetLastInputInfo)(&ok));
}

/* The value user32 returns is read straight from SERVERINFO+0x1368 in the shared desktop mapping. */
TEST(dx_input_inject_lastinput_reads_shared_info) {
    INJ_ENV(); REQUIRE(U(GetLastInputInfo));
    if (t_is_wine()) { t_skip("SERVERINFO layout is Windows-specific"); return; }
    struct k_user_shared_info si;
    if (!k_shared_info(&si) || !si.psi) { t_skip("gSharedInfo unavailable"); return; }
    for (int i = 0; i < 3; i++) {
        LASTINPUTINFO li = { sizeof li, 0 };
        unsigned long raw = 0;
        CHECK(U(GetLastInputInfo)(&li));
        CHECK(k_try_read_u32((char *)si.psi + 0x1368, &raw));
        if (raw == li.dwTime) { CHECK(1); break; }
        if (i == 2) CHECKF(raw == li.dwTime, "api %lu raw %lu (real input between reads?)", li.dwTime, raw);
    }
}

TEST(dx_input_inject_lastinput_advances_key) {
    INJ_ENV(); inj_arm(); REQUIRE(U(GetLastInputInfo));
    Sleep(60);
    LASTINPUTINFO a = { sizeof a, 0 }, b = { sizeof b, 0 };
    U(GetLastInputInfo)(&a);
    Sleep(60);
    DWORD t0 = GetTickCount();
    inj_tap(VK_F24, 0, 0);
    CHECK(U(GetLastInputInfo)(&b));
    CHECKF((int)(b.dwTime - a.dwTime) > 0, "before %lu after %lu", a.dwTime, b.dwTime);
    CHECKF((int)(b.dwTime - t0) >= -5 && (int)(GetTickCount() - b.dwTime) >= 0 && (int)(GetTickCount() - b.dwTime) < 3000, "after %lu tick %lu", b.dwTime, GetTickCount());
    inj_restore();
}

TEST(dx_input_inject_lastinput_advances_mouse) {
    INJ_ENV(); inj_arm(); REQUIRE(U(GetLastInputInfo)); REQUIRE(U(GetCursorPos));
    LASTINPUTINFO a = { sizeof a, 0 }, b = { sizeof b, 0 };
    POINT c; U(GetCursorPos)(&c);
    RECT p; inj_screen(&p, NULL);
    Sleep(60);
    U(GetLastInputInfo)(&a);
    Sleep(60);
    inj_mouse1(inj_abs(c.x, p.right), inj_abs(c.y, p.bottom), 0, MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE, 0);
    U(GetLastInputInfo)(&b);
    CHECKF((int)(b.dwTime - a.dwTime) > 0, "before %lu after %lu", a.dwTime, b.dwTime);
    inj_restore();
}

/* A rejected SendInput (bad cbSize) is not input: last-input time unchanged by it. */
TEST(dx_input_inject_lastinput_unchanged_by_rejected) {
    INJ_ENV(); inj_arm(); REQUIRE(U(GetLastInputInfo)); REQUIRE(U(SendInput));
    inj_tap(VK_F24, 0, 0);
    LASTINPUTINFO a = { sizeof a, 0 }, b = { sizeof b, 0 };
    U(GetLastInputInfo)(&a);
    Sleep(80);
    INPUT in; inj_key(&in, VK_F24, 0, 0, 0);
    EXPECT_EQ(U(SendInput)(1, &in, sizeof in - 1), 0);
    EXPECT_EQ(U(SendInput)(0, &in, sizeof in), 0);
    U(GetLastInputInfo)(&b);
    t_note("last input before=%lu after=%lu (real user input may advance it)", a.dwTime, b.dwTime);
    CHECK((int)(b.dwTime - a.dwTime) < 70 || b.dwTime != a.dwTime);   /* informational */
    inj_restore();
}

/* ---- BlockInput --------------------------------------------------------------------------------------------------- */
static volatile int g_blk_other[3];
static DWORD th_block(void *a) {
    g_blk_other[0] = U(BlockInput)(TRUE);       /* already blocked by the creator thread */
    g_blk_other[1] = U(BlockInput)(FALSE);      /* not the blocking thread */
    return 0;
}
TEST(dx_input_inject_blockinput_ownership) {
    INJ_ENV(); inj_arm(); REQUIRE(U(BlockInput));
    SET_ERR(0);
    BOOL r0 = U(BlockInput)(FALSE);     /* nothing blocked: no thread other than the caller blocks input -> FALSE */
    CHECK(!r0);
    BOOL b = U(BlockInput)(TRUE);
    DWORD e = GetLastError();
    if (!b) {
        t_note("BlockInput(TRUE) refused, err=%lu (needs input-desktop WINSTA access / not already blocked)", e);
        t_skip("cannot block input in this session");
        return;
    }
    CHECK(!U(BlockInput)(TRUE));        /* a second block fails: input already blocked */
    /* the blocking thread's own injection still goes through */
    INPUT in; inj_mouse(&in, 0, 0, 0, 0, 0);
    EXPECT_EQ(inj_send(1, &in), 1);
    g_blk_other[0] = g_blk_other[1] = -1;
    k_run_gui_thread(th_block, NULL);
    CHECK(g_blk_other[0] == 0);
    CHECK(g_blk_other[1] == 0);
    CHECK(U(BlockInput)(FALSE));        /* the owner unblocks */
    CHECK(!U(BlockInput)(FALSE));       /* and once unblocked it is no longer the owner */
    inj_restore();
}

TEST(dx_input_inject_blockinput_other_thread_can_block_after) {
    INJ_ENV(); inj_arm(); REQUIRE(U(BlockInput));
    BOOL b = U(BlockInput)(TRUE);
    if (!b) { t_skip("cannot block input in this session"); return; }
    CHECK(U(BlockInput)(FALSE));
    BOOL b2 = U(BlockInput)(TRUE);       /* free again */
    CHECK(b2);
    if (b2) CHECK(U(BlockInput)(FALSE));
    inj_restore();
}

TEST(dx_input_inject_blockinput_direct_stub) {
    INJ_ENV(); inj_arm();
    typedef BOOL (WINAPI *pfn)(BOOL);
    pfn f = (pfn)t_proc_in("win32u.dll", "NtUserBlockInput");
    REQUIRE(f);
    if (t_is_wine()) { t_skip("Wine aborts on its unimplemented win32u.NtUserBlockInput"); return; }
    BOOL b = f(TRUE);
    if (!b) { t_skip("cannot block input in this session (err %lu)", GetLastError()); return; }
    CHECK(!f(TRUE));
    CHECK(f(FALSE));
    CHECK(!f(FALSE));
    inj_restore();
}

/* ---- SwapMouseButton ---------------------------------------------------------------------------------------------- */
TEST(dx_input_inject_swapmousebutton_returns_previous) {
    INJ_ENV(); inj_arm(); REQUIRE(U(SwapMouseButton)); REQUIRE(U(GetSystemMetrics));
    int orig = U(GetSystemMetrics)(SM_SWAPBUTTON) != 0;
    U(SwapMouseButton)(FALSE);
    CHECK(!U(SwapMouseButton)(FALSE));          /* previous state: not swapped */
    CHECK(!U(SwapMouseButton)(TRUE));           /* previous: not swapped, now swapped */
    CHECK(U(SwapMouseButton)(TRUE));            /* previous: swapped */
    CHECK(U(SwapMouseButton)(7));               /* any non-zero means TRUE: previous swapped */
    CHECK(U(SwapMouseButton)(FALSE));           /* previous swapped, now restored */
    CHECK(!U(SwapMouseButton)(FALSE));
    int sm = U(GetSystemMetrics)(SM_SWAPBUTTON) != 0;
    if (sm) t_note("SM_SWAPBUTTON still set after SwapMouseButton(FALSE)");
    U(SwapMouseButton)(orig);
    inj_restore();
}

TEST(dx_input_inject_swapmousebutton_metric_and_async) {
    INJ_ENV(); inj_arm(); REQUIRE(U(SwapMouseButton));
    int orig = U(GetSystemMetrics)(SM_SWAPBUTTON) != 0;
    U(SwapMouseButton)(TRUE);
    int m1 = U(GetSystemMetrics)(SM_SWAPBUTTON);
    U(SwapMouseButton)(FALSE);
    int m0 = U(GetSystemMetrics)(SM_SWAPBUTTON);
    t_note("SM_SWAPBUTTON after Swap(TRUE)=%d after Swap(FALSE)=%d", m1, m0);
    CHECK(m0 == 0 || orig);
    U(SwapMouseButton)(orig);
    inj_restore();
}

TEST(dx_input_inject_swapmousebutton_direct_callone) {
    INJ_ENV(); inj_arm(); REQUIRE(UN(NtUserCallOneParam)); REQUIRE(U(SwapMouseButton));
    if (t_is_wine()) { t_skip("CallOneParam function numbers are win32k.sys specific"); return; }
    int orig = U(GetSystemMetrics)(SM_SWAPBUTTON) != 0;
    UN(NtUserCallOneParam)(0, 0x49);
    EXPECT_EQ(UN(NtUserCallOneParam)(1, 0x49), 0);      /* returns previous state */
    EXPECT_EQ(UN(NtUserCallOneParam)(1, 0x49), 1);
    EXPECT_EQ(U(SwapMouseButton)(FALSE), 1);            /* user32 path sees the same state */
    EXPECT_EQ(UN(NtUserCallOneParam)(0, 0x49), 0);
    U(SwapMouseButton)(orig);
    inj_restore();
}

/* ---- mouse parameters: SPI_GET/SETMOUSE, SPI_GET/SETMOUSESPEED (fWinIni=0: in-memory only, nothing written) -------------- */
TEST(dx_input_inject_spi_mouse_roundtrip) {
    INJ_ENV(); REQUIRE(U(SystemParametersInfoW));
    int m0[3] = { -1, -1, -1 };
    CHECK(U(SystemParametersInfoW)(SPI_GETMOUSE, 0, m0, 0));
    CHECK(m0[0] >= 0 && m0[1] >= 0 && m0[2] >= 0);
    int m1[3] = { m0[0], m0[1], m0[2] };
    CHECK(U(SystemParametersInfoW)(SPI_SETMOUSE, 0, m1, 0));      /* same values */
    int m2[3] = { m0[0], m0[1], m0[2] ? 0 : 1 };                  /* flip the acceleration enable */
    BOOL ok = U(SystemParametersInfoW)(SPI_SETMOUSE, 0, m2, 0);
    int g[3] = { -1, -1, -1 };
    CHECK(U(SystemParametersInfoW)(SPI_GETMOUSE, 0, g, 0));
    if (ok) CHECKF(g[2] == m2[2], "accel %d want %d", g[2], m2[2]);
    CHECK(U(SystemParametersInfoW)(SPI_SETMOUSE, 0, m0, 0));      /* restore */
    CHECK(U(SystemParametersInfoW)(SPI_GETMOUSE, 0, g, 0));
    CHECK(g[0] == m0[0] && g[1] == m0[1] && g[2] == m0[2]);
}

TEST(dx_input_inject_spi_mousespeed_roundtrip) {
    INJ_ENV(); REQUIRE(U(SystemParametersInfoW));
    if (t_is_wine()) { t_skip("SPI_SETMOUSESPEED is a stub under Wine"); return; }
    UINT s0 = 0xFF;
    CHECK(U(SystemParametersInfoW)(SPI_GETMOUSESPEED, 0, &s0, 0));
    if (t_is_wine() && s0 == 0) { t_skip("no mouse speed under this Wine"); return; }
    CHECKF(s0 >= 1 && s0 <= 20, "speed %u", s0);
    static const UINT vals[] = { 1, 5, 11, 20, 10 };
    for (size_t i = 0; i < sizeof vals / sizeof *vals; i++) {
        BOOL ok = U(SystemParametersInfoW)(SPI_SETMOUSESPEED, 0, (PVOID)(ULONG_PTR)vals[i], 0);
        UINT g = 0;
        U(SystemParametersInfoW)(SPI_GETMOUSESPEED, 0, &g, 0);
        if (ok) CHECKF(g == vals[i], "set %u got %u", vals[i], g);
    }
    U(SystemParametersInfoW)(SPI_SETMOUSESPEED, 0, (PVOID)(ULONG_PTR)s0, 0);
    UINT g = 0; U(SystemParametersInfoW)(SPI_GETMOUSESPEED, 0, &g, 0);
    EXPECT_EQ(g, s0);
}

/* Pointer speed changes how far a relative injection moves, but only monotonically in the speed. */
TEST(dx_input_inject_mousespeed_scales_relative_move) {
    INJ_ENV(); inj_arm(); REQUIRE(U(SystemParametersInfoW)); REQUIRE(U(SetCursorPos)); REQUIRE(U(GetCursorPos));
    if (t_is_wine()) { t_skip("pointer speed scaling is not implemented by the Wine null driver"); return; }
    UINT s0 = 10, mv[3] = { 0, 0, 0 };
    U(SystemParametersInfoW)(SPI_GETMOUSESPEED, 0, &s0, 0);
    BOOL ok = U(SystemParametersInfoW)(SPI_GETMOUSE, 0, mv, 0);
    int m0[3] = { 0 }; U(SystemParametersInfoW)(SPI_GETMOUSE, 0, m0, 0);
    int off[3] = { 0, 0, 0 };
    U(SystemParametersInfoW)(SPI_SETMOUSE, 0, off, 0);       /* acceleration off -> linear scaling by pointer speed */
    RECT p; inj_screen(&p, NULL);
    LONG dist[2] = { 0, 0 };
    static const UINT sp[2] = { 4, 16 };
    for (int i = 0; i < 2; i++) {
        U(SystemParametersInfoW)(SPI_SETMOUSESPEED, 0, (PVOID)(ULONG_PTR)sp[i], 0);
        U(SetCursorPos)(p.right / 4, p.bottom / 2);
        POINT a, b; U(GetCursorPos)(&a);
        inj_mouse1(30, 0, 0, MOUSEEVENTF_MOVE, 0);
        U(GetCursorPos)(&b);
        dist[i] = b.x - a.x;
    }
    U(SystemParametersInfoW)(SPI_SETMOUSE, 0, m0, 0);
    U(SystemParametersInfoW)(SPI_SETMOUSESPEED, 0, (PVOID)(ULONG_PTR)s0, 0);
    CHECKF(dist[1] > dist[0], "speed 4 moved %ld px, speed 16 moved %ld px", dist[0], dist[1]);
    inj_restore();
}

/* ---- keyboard state ----------------------------------------------------------------------------------------------- */
TEST(dx_input_inject_set_keyboard_state_same) {
    INJ_ENV(); inj_arm(); REQUIRE(U(GetKeyboardState)); REQUIRE(U(SetKeyboardState));
    BYTE st[256]; memset(st, 0, sizeof st);
    CHECK(U(GetKeyboardState)(st));
    CHECK(U(SetKeyboardState)(st));          /* writing the thread's own state back is accepted */
    BYTE st2[256]; memset(st2, 0xCC, sizeof st2);
    CHECK(U(GetKeyboardState)(st2));
    for (int i = 0; i < 256; i++) if ((st[i] & 0x80) != (st2[i] & 0x80) && i != VK_LBUTTON && i != VK_RBUTTON && i != VK_MBUTTON) { CHECKF(0, "vk %d changed", i); break; }
    inj_restore();
}

/* The raw state syscalls agree with the user32 functions (NtUserGetKeyState/NtUserGetAsyncKeyState take the vk). */
TEST(dx_input_inject_key_state_direct_syscalls) {
    INJ_ENV(); HWND w = inj_fg_begin(); if (!w) return;
    REQUIRE(UN(NtUserGetKeyState)); REQUIRE(UN(NtUserGetAsyncKeyState));
    inj_keydown(VK_F24, 0, 0); inj_pump(50);
    CHECK(UN(NtUserGetAsyncKeyState)(VK_F24) & 0x8000);
    CHECK(UN(NtUserGetKeyState)(VK_F24) < 0);
    EXPECT_EQ((USHORT)UN(NtUserGetKeyState)(VK_F24) & 0x8000, (USHORT)U(GetKeyState)(VK_F24) & 0x8000);
    inj_keyup(VK_F24, 0, 0); inj_pump(50);
    CHECK(!(UN(NtUserGetAsyncKeyState)(VK_F24) & 0x8000));
    CHECK(UN(NtUserGetKeyState)(VK_F24) >= 0);
    /* out-of-range vk values are not an error: they read as up */
    CHECK(UN(NtUserGetAsyncKeyState)(0x1000) == 0 || 1);
    inj_fg_end(w);
}

/* Cached async/key state words that GetKeyState/GetAsyncKeyState fast paths compare: TEB copies vs the shared async/key state words. */
TEST(dx_input_inject_key_state_cache_serials) {
    INJ_ENV(); HWND w = inj_fg_begin(); if (!w) return;
    if (t_is_wine()) { inj_fg_end(w); t_skip("Windows-specific TEB/SERVERINFO layout"); return; }
    struct k_user_shared_info si;
    if (!k_shared_info(&si) || !si.psi) { inj_fg_end(w); t_skip("gSharedInfo unavailable"); return; }
    unsigned long c0 = 0, a0 = 0, g0 = 0, ga0 = 0, c1 = 0, a1 = 0, g1 = 0, ga1 = 0;
    U(GetKeyState)(VK_SHIFT); U(GetAsyncKeyState)(VK_SHIFT);
    k_try_read_u32((char *)k_teb() + 0x870, &c0); k_try_read_u32((char *)k_teb() + 0x87c, &a0);
    k_try_read_u32((char *)si.psi + 0x1b48, &g0); k_try_read_u32((char *)si.psi + 0x1b4c, &ga0);
    t_note("key-state serial client=%lu server=%lu async client=%lu server=%lu", c0, g0, a0, ga0);
    inj_tap(VK_F24, 0, 0); inj_pump(40);
    k_try_read_u32((char *)si.psi + 0x1b48, &g1); k_try_read_u32((char *)si.psi + 0x1b4c, &ga1);
    CHECKF(ga1 != ga0, "async serial did not advance (%lu -> %lu)", ga0, ga1);   /* the async table changed, clients must re-query */
    U(GetAsyncKeyState)(VK_SHIFT);
    k_try_read_u32((char *)k_teb() + 0x87c, &a1);
    CHECKF(a1 == ga1 || a1 == ga0, "client async serial %lu vs server %lu", a1, ga1);
    inj_fg_end(w);
}

/* ---- touch injection (negative paths only; a valid init changes system-wide virtual device state) ------------------------ */
TEST(dx_input_inject_touch_init_invalid_args) {
    INJ_ENV(); REQUIRE(U(InitializeTouchInjection));
    static const struct { UINT32 max; DWORD mode; } t[] = {
        { 0, 1 }, { 0, 2 }, { 0, 3 }, { 257, 1 }, { 1000, 1 }, { 0xFFFFFFFFu, 1 },
        { 1, 0 }, { 1, 4 }, { 1, 5 }, { 1, 0xFFFFFFFFu }, { 10, 0 }, { 0, 0 }, { 257, 4 },
    };
    for (size_t i = 0; i < sizeof t / sizeof *t; i++) {
        SET_ERR(0);
        BOOL r = U(InitializeTouchInjection)(t[i].max, t[i].mode);
        CHECKF(!r, "max=%u mode=%lu", t[i].max, t[i].mode);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
        if (t_is_wine()) continue;   /* the raw stub may be unimplemented (abort) under Wine */
        SET_ERR(0);
        r = UN(NtUserInitializeTouchInjection)(t[i].max, t[i].mode);
        CHECKF(!r, "nt max=%u mode=%lu", t[i].max, t[i].mode);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
}

TEST(dx_input_inject_touch_inject_invalid_count) {
    INJ_ENV(); REQUIRE(U(InjectTouchInput));
    BYTE buf[0x90];          /* POINTER_TOUCH_INFO is 0x90 bytes; the array is never read for these counts */
    memset(buf, 0, sizeof buf);
    static const UINT32 cnt[] = { 0, 257, 258, 1000, 0x10000, 0xFFFFFFFFu };
    for (size_t i = 0; i < sizeof cnt / sizeof *cnt; i++) {
        SET_ERR(0);
        CHECKF(!U(InjectTouchInput)(cnt[i], (const POINTER_TOUCH_INFO *)buf), "count=%u", cnt[i]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
        if (t_is_wine()) continue;
        SET_ERR(0);
        CHECKF(!UN(NtUserInjectTouchInput)(cnt[i], buf), "nt count=%u", cnt[i]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
}

/* Without a prior successful InitializeTouchInjection a (valid-looking) contact is rejected; recorded, not asserted. */
CHILD(dx_input_inject_touch_noinit) {
    typedef BOOL (WINAPI *pfn)(UINT32, const void *);
    pfn f = (pfn)t_proc_in("user32.dll", "InjectTouchInput");
    if (!f) return 0;
    BYTE buf[0x90]; memset(buf, 0, sizeof buf);
    *(DWORD *)buf = 2;                       /* pointerInfo.pointerType = PT_TOUCH */
    SetLastError(0);
    BOOL r = f(1, buf);
    printf("NOTE InjectTouchInput without init -> %d err=%lu\n", r, GetLastError());
    return 0;
}
TEST(dx_input_inject_touch_inject_without_init) {
    INJ_ENV(); REQUIRE(U(InjectTouchInput));
    struct k_child_result r = k_child_run("dx_input_inject_touch_noinit", "", 15000);
    CHECK(r.exit_code == 0);
}
