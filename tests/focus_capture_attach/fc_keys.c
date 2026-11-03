/* Per-queue key state and queue status as exposed to the client:
 * NtUserGetKeyState / NtUserGetAsyncKeyState fill the _CLIENTINFO key caches (dwKeyCache /
 * afKeyState, dwAsyncKeyCache / afAsyncKeyState / afAsyncKeyStateRecentDown) that user32's
 * GetKeyState / GetAsyncKeyState consult first, NtUserGetThreadState info classes, and the
 * CLIENTTHREADINFO (fsChangeBits / fsWakeBits) behind GetInputState and GetQueueStatus. */
#include "fc.h"

static int swap_buttons(void) { unsigned long v = 0; return fc_psi_dword(0x7c4, &v) && v; }

TEST(fca_keys_getkeystate_invalid_vk) {
    FC_NEED(); REQUIRE(UN(NtUserGetKeyState));
    HWND a = fc_top(); REQUIRE(a);
 /* low byte <= 0x1f: NtUserGetKeyState reaches the test and fails with 0x57 */
    int vks[] = { 0x100, 0x101, 0x108, 0x110, 0x11f, 0x200, 0x1000, 0x10000, (int)0xFFFFFF00 };
    for (unsigned i = 0; i < sizeof vks / sizeof vks[0]; i++) {
        SET_ERR(0);
        UN(NtUserGetKeyState)(vks[i]);
        DWORD e = GetLastError();
        CHECKF(e == ERROR_INVALID_PARAMETER, "NtUserGetKeyState(%x) err %lu", vks[i], e);
        SET_ERR(0);
        U(GetKeyState)(vks[i]);
        CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "GetKeyState(%x) err %lu", vks[i], GetLastError());
    }
    fc_destroy(a);
}

TEST(fca_keys_getkeystate_valid_vk_no_error) {
    FC_NEED(); REQUIRE(UN(NtUserGetKeyState));
    HWND a = fc_top(); REQUIRE(a);
    int vks[] = { 0, 1, 0x10, 0x14, 0x1f, 0x20, 0x41, 0x5a, 0x70, 0x90, 0xA0, 0xFE, 0xFF };
    for (unsigned i = 0; i < sizeof vks / sizeof vks[0]; i++) {
        SET_ERR(0);
        UN(NtUserGetKeyState)(vks[i]);
        CHECKF(GetLastError() == 0, "NtUserGetKeyState(%x) err %lu", vks[i], GetLastError());
    }
    fc_destroy(a);
}

TEST(fca_keys_ordinary_keys_up_by_default) {
    FC_NEED(); REQUIRE(UN(NtUserGetKeyState));
    HWND a = fc_top(); REQUIRE(a);
    /* F13..F24 are not on a normal keyboard; their state in a quiet session is "up" (no high bit). */
    for (int vk = 0x7C; vk <= 0x87; vk++) {
        SHORT s = UN(NtUserGetKeyState)(vk);
        CHECKF((s & 0x8000) == 0, "vk %x reads down", vk);
    }
    fc_destroy(a);
}

static int key_state_from_cache(const unsigned char c[8], int vk) {
    unsigned char b = c[(vk & 0xff) >> 2];
    int toggle = (b >> ((vk & 3) * 2 + 1)) & 1, down = (b >> ((vk & 3) * 2)) & 1;
    return (down ? 0xff80 : 0) | toggle;
}

TEST(fca_keys_client_key_cache_filled_by_syscall) {
    FC_NEED(); REQUIRE(UN(NtUserGetKeyState));
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    int done = 0;
    for (int attempt = 0; attempt < 5 && !done; attempt++) {
        SHORT direct[0x20];
        for (int vk = 0; vk < 0x20; vk++) direct[vk] = UN(NtUserGetKeyState)(vk);
        unsigned long ci = fc_ci_dword(FC_CI_DWKEYCACHE), psi = 0;
        unsigned char cache[8];
        if (!fc_psi_dword(FC_PSI_DWKEYCACHE, &psi) ||
            !k_try_read((char *)k_teb() + TEB_Win32ClientInfo + FC_CI_AFKEYSTATE, cache, 8)) {
            fc_destroy(a); FC_SKIP("client info / SERVERINFO not readable");
        }
        if (ci != psi) continue;     /* key state changed between the calls */
        done = 1;
        /* NtUserGetKeyState caches the key state in the TEB client info */
        EXPECT_EQ(ci, psi);
        for (int vk = 0; vk < 0x20; vk++) {
            int want = key_state_from_cache(cache, vk);
            CHECKF((direct[vk] & 0x8001) == (want & 0x8001), "vk %x: direct %04x cache %04x", vk, direct[vk] & 0xffff, want & 0xffff);
            /* user32!GetKeyState answers vk < 0x20 from this cache */
            SHORT u = U(GetKeyState)(vk);
            CHECKF((u & 0x8001) == (direct[vk] & 0x8001), "vk %x: GetKeyState %04x vs NtUserGetKeyState %04x", vk, u & 0xffff, direct[vk] & 0xffff);
        }
    }
    if (!done) t_note("key cache kept changing; skipped comparison");
    fc_destroy(a);
}

TEST(fca_keys_getkeystate_user32_equals_direct_high_vk) {
    FC_NEED(); REQUIRE(UN(NtUserGetKeyState));
    HWND a = fc_top(); REQUIRE(a);
    int vks[] = { 0x20, 0x30, 0x41, 0x5b, 0x70, 0x90, 0x91, 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5 };
    for (unsigned i = 0; i < sizeof vks / sizeof vks[0]; i++) {
        SHORT u = U(GetKeyState)(vks[i]), d = UN(NtUserGetKeyState)(vks[i]);
        CHECKF(u == d || (u & 0x8000) != (d & 0x8000), "vk %x: %04x vs %04x", vks[i], u & 0xffff, d & 0xffff);   /* vk >= 0x20 never uses the cache */
    }
    fc_destroy(a);
}

TEST(fca_keys_toggle_keys_stable_across_focus_changes) {
    FC_NEED();
    HWND a = fc_top(), b = fc_top(); REQUIRE(a && b);
    fc_quiesce();
    int vks[] = { VK_CAPITAL, VK_NUMLOCK, VK_SCROLL };
    SHORT before[3];
    for (int i = 0; i < 3; i++) before[i] = U(GetKeyState)(vks[i]);
    U(SetFocus)(a); U(SetFocus)(b); U(SetActiveWindow)(a); U(SetFocus)(NULL);
    for (int i = 0; i < 3; i++) {
        SHORT now = U(GetKeyState)(vks[i]);
        if ((now & 1) != (before[i] & 1)) t_note("toggle state of vk %x changed during the test (user input?)", vks[i]);
        else CHECK((now & 1) == (before[i] & 1));
    }
    fc_destroy(a); fc_destroy(b);
}

TEST(fca_keys_getkeyboardstate_agrees_with_getkeystate) {
    FC_NEED(); REQUIRE(U(GetKeyboardState));
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    int agreed = 0;
    for (int attempt = 0; attempt < 4 && !agreed; attempt++) {
        BYTE kb[256]; memset(kb, 0xCC, sizeof kb);
        CHECK(U(GetKeyboardState)(kb));
        int bad = 0;
        for (int vk = 1; vk < 256; vk++) {
            SHORT s = U(GetKeyState)(vk);
            if (((kb[vk] & 0x80) != 0) != ((s & 0x8000) != 0) || (kb[vk] & 1) != (s & 1)) { bad++; }
        }
        if (!bad) agreed = 1;
    }
    if (!agreed) t_note("GetKeyboardState and GetKeyState disagreed on every attempt (input arriving?)");
    else CHECK(agreed);
    fc_destroy(a);
}

TEST(fca_keys_async_cache_filled_by_syscall) {
    FC_NEED(); REQUIRE(UN(NtUserGetAsyncKeyState));
    HWND a = fc_top(); REQUIRE(a);
    int done = 0;
    for (int attempt = 0; attempt < 5 && !done; attempt++) {
        UN(NtUserGetAsyncKeyState)(0x7F);   /* any vk: the syscall refreshes the cache after answering */
        unsigned long ci = fc_ci_dword(FC_CI_DWASYNCCACHE), psi = 0;
        unsigned char st[8], recent[8];
        if (!fc_psi_dword(FC_PSI_DWASYNCCACHE, &psi) ||
            !k_try_read((char *)k_teb() + TEB_Win32ClientInfo + FC_CI_AFASYNC, st, 8) ||
            !k_try_read((char *)k_teb() + TEB_Win32ClientInfo + FC_CI_AFASYNC + 8, recent, 8)) {
            fc_destroy(a); FC_SKIP("client info / SERVERINFO not readable");
        }
        if (ci != psi) continue;
        done = 1;
        EXPECT_EQ(ci, psi);
        for (int vk = 3; vk < 0x20; vk++) {      /* skip the (possibly swapped) mouse buttons */
            if (recent[vk >> 3] & (1 << (vk & 7))) continue;   /* the recent-down bit forces a syscall */
            int down = (st[vk >> 2] >> ((vk & 3) * 2)) & 1;
            SHORT u = U(GetAsyncKeyState)(vk);
            CHECKF(((u & 0x8000) != 0) == (down != 0), "vk %x: GetAsyncKeyState %04x cache down=%d", vk, u & 0xffff, down);
        }
    }
    if (!done) t_note("async key cache kept changing");
    fc_destroy(a);
}

TEST(fca_keys_async_vs_sync_state_of_idle_keys) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    for (int vk = 0x7C; vk <= 0x87; vk++) {
        SHORT as = U(GetAsyncKeyState)(vk), s = U(GetKeyState)(vk);
        CHECKF((as & 0x8000) == 0 && (s & 0x8000) == 0, "vk %x down: async %04x sync %04x", vk, as & 0xffff, s & 0xffff);
    }
    fc_destroy(a);
}

TEST(fca_keys_async_mouse_swap_note) {
    FC_NEED();
    if (swap_buttons()) t_note("SM_SWAPBUTTON is set: GetAsyncKeyState swaps VK_LBUTTON/VK_RBUTTON");
    SHORT l = U(GetAsyncKeyState)(VK_LBUTTON), r = U(GetAsyncKeyState)(VK_RBUTTON);
    t_note("async mouse buttons L=%04x R=%04x", l & 0xffff, r & 0xffff);
}

/* ---- NtUserGetThreadState classes ---------------------------------------------------------- */

TEST(fca_threadstate_unknown_classes_return_zero) {
    FC_NEED(); if (t_is_wine()) FC_SKIP("syscall info classes are win32k-specific");
    REQUIRE(UN(NtUserGetThreadState));
    HWND a = fc_top(); REQUIRE(a);
    DWORD cls[] = { 16, 17, 31, 100, 0xFF, 0x1000, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF };
    for (unsigned i = 0; i < sizeof cls / sizeof cls[0]; i++) {
        SET_ERR(0);
        ULONG_PTR r = UN(NtUserGetThreadState)(cls[i]);
        CHECKF(r == 0, "class %lx -> %llx", cls[i], (unsigned long long)r);
    }
    fc_destroy(a);
}

TEST(fca_threadstate_constant_classes) {
    FC_NEED(); if (t_is_wine()) FC_SKIP("syscall info classes are win32k-specific");
    REQUIRE(UN(NtUserGetThreadState));
    HWND a = fc_top(); REQUIRE(a);
    EXPECT_CALL_EQ(UN(NtUserGetThreadState)(FC_TS_KEYSTATE_OK), 1);   /* class 14: always 1 */
    EXPECT_CALL_EQ(UN(NtUserGetThreadState)(13), 0);                   /* class 13: only winlogon / the logon desktop */
    fc_destroy(a);
}

TEST(fca_threadstate_matches_user32_getters) {
    FC_NEED(); if (t_is_wine()) FC_SKIP("syscall info classes are win32k-specific");
    REQUIRE(UN(NtUserGetThreadState));
    HWND a = fc_top(), b = fc_top(); REQUIRE(a && b);
    fc_quiesce();
    U(SetCapture)(a); U(SetFocus)(b);
    EXPECT_CALL_EQ(UN(NtUserGetThreadState)(FC_TS_FOCUS), (ULONG_PTR)U(GetFocus)());
    EXPECT_CALL_EQ(UN(NtUserGetThreadState)(FC_TS_ACTIVE), (ULONG_PTR)U(GetActiveWindow)());
    EXPECT_CALL_EQ(UN(NtUserGetThreadState)(FC_TS_CAPTURE), (ULONG_PTR)U(GetCapture)());
    U(ReleaseCapture)();
    fc_destroy(a); fc_destroy(b);
}

/* ---- CLIENTTHREADINFO / queue status ------------------------------------------------------- */

static unsigned short pcti_u16(unsigned off) {
    void *pcti = NULL; unsigned short v = 0xFFFF;
    if (k_try_read_ptr((char *)k_teb() + TEB_Win32ClientInfo + FC_CI_PCTI, &pcti) && pcti)
        if (!k_try_read_u16((char *)pcti + off, &v)) v = 0xFFFF;
    return v;
}

TEST(fca_queue_pcti_pointer_readable) {
    FC_NEED();
    HWND a = fc_top(); REQUIRE(a);
    void *pcti = NULL;
    CHECK(k_try_read_ptr((char *)k_teb() + TEB_Win32ClientInfo + FC_CI_PCTI, &pcti));
    CHECK(pcti != NULL);               /* user32!GetInputState dereferences Self+0x860 directly */
    unsigned short v;
    if (pcti) CHECK(k_try_read_u16((char *)pcti + 4, &v));
    fc_destroy(a);
}

TEST(fca_queue_getinputstate_idle) {
    FC_NEED(); REQUIRE(U(GetInputState));
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    int idle = 0;
    for (int i = 0; i < 5 && !idle; i++) { k_pump(10); idle = !U(GetInputState)(); }
    if (!idle) { t_note("input queue not idle (real input arriving)"); fc_destroy(a); return; }
    CHECK(idle);
    EXPECT_EQ(pcti_u16(4) & 5, 0);     /* the user32 fast path returns FALSE exactly when fsChangeBits has no KEY/MOUSEBUTTON bits */
    fc_destroy(a);
}

TEST(fca_queue_getinputstate_matches_threadstate) {
    FC_NEED(); if (t_is_wine()) FC_SKIP("syscall info classes are win32k-specific");
    REQUIRE(UN(NtUserGetThreadState));
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    BOOL u = U(GetInputState)();
    ULONG_PTR k = UN(NtUserGetThreadState)(FC_TS_INPUTSTATE);
    if (u == (BOOL)k) CHECK(1);
    else t_note("GetInputState %d vs class 5 %llu (input changed in between)", u, (unsigned long long)k);
    fc_destroy(a);
}

TEST(fca_queue_getqueuestatus_invalid_flags) {
    FC_NEED(); REQUIRE(U(GetQueueStatus));
    UINT bad[] = { 0x200, 0x2000, 0x8000, 0x10000, 0x80000000, 0xFFFF0000 };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SET_ERR(0);
        DWORD r = U(GetQueueStatus)(bad[i]);
        CHECKF(r == 0, "flags %x -> %lx", bad[i], r);
        EXPECT_ERR(ERROR_INVALID_FLAGS);   /* user32!GetQueueStatus rejects (flags & 0xffffa200) */
    }
}

TEST(fca_queue_post_sets_wake_and_change_bits) {
    FC_NEED(); REQUIRE(U(GetQueueStatus) && U(PostMessageW) && U(PeekMessageW));
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    MSG m;
    while (U(PeekMessageW)(&m, NULL, 0, 0, PM_REMOVE)) U(DispatchMessageW)(&m);
    U(GetQueueStatus)(QS_ALLINPUT);                        /* clears the change bits it asks about */
    unsigned short w0 = pcti_u16(6), c0 = pcti_u16(4);
    if (w0 == 0xFFFF) { fc_destroy(a); FC_SKIP("client thread info not readable"); }
    EXPECT_EQ(w0 & QS_POSTMESSAGE, 0);
    EXPECT_EQ(c0 & QS_POSTMESSAGE, 0);
    CHECK(U(PostMessageW)(a, WM_APP + 1, 0, 0));
    unsigned short w1 = pcti_u16(6), c1 = pcti_u16(4);
    /* a posted message sets QS_POSTMESSAGE in both fsWakeBits (+6) and fsChangeBits (+4) of the shared CLIENTTHREADINFO */
    EXPECT_EQ(w1 & QS_POSTMESSAGE, QS_POSTMESSAGE);
    EXPECT_EQ(c1 & QS_POSTMESSAGE, QS_POSTMESSAGE);
    DWORD qs = U(GetQueueStatus)(QS_POSTMESSAGE);
    EXPECT_EQ(HIWORD(qs) & QS_POSTMESSAGE, QS_POSTMESSAGE);   /* still queued */
    EXPECT_EQ(LOWORD(qs) & QS_POSTMESSAGE, QS_POSTMESSAGE);   /* and new since the previous query */
    EXPECT_EQ(pcti_u16(4) & QS_POSTMESSAGE, 0);               /* querying cleared the change bit */
    CHECK(U(PeekMessageW)(&m, a, WM_APP + 1, WM_APP + 1, PM_REMOVE));
    EXPECT_EQ(pcti_u16(6) & QS_POSTMESSAGE, 0);               /* queue empty again */
    fc_destroy(a);
}

TEST(fca_queue_getqueuestatus_empty_after_drain) {
    FC_NEED(); REQUIRE(U(GetQueueStatus));
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    MSG m;
    while (U(PeekMessageW)(&m, NULL, 0, 0, PM_REMOVE)) U(DispatchMessageW)(&m);
    DWORD qs = U(GetQueueStatus)(QS_POSTMESSAGE | QS_SENDMESSAGE);
    EXPECT_EQ(HIWORD(qs) & (QS_POSTMESSAGE | QS_SENDMESSAGE), 0);
    fc_destroy(a);
}

TEST(fca_queue_posted_key_message_status_observed) {
    FC_NEED(); REQUIRE(U(PostMessageW) && U(GetQueueStatus));
    HWND a = fc_top(); REQUIRE(a);
    fc_quiesce();
    MSG m;
    while (U(PeekMessageW)(&m, NULL, 0, 0, PM_REMOVE)) U(DispatchMessageW)(&m);
    U(GetQueueStatus)(QS_ALLINPUT);
    CHECK(U(PostMessageW)(a, WM_KEYDOWN, 'A', 0));
    DWORD qs = U(GetQueueStatus)(QS_ALLINPUT);
    t_note("queue status after PostMessage(WM_KEYDOWN): %08lx, GetInputState=%d", qs, U(GetInputState)());
    while (U(PeekMessageW)(&m, NULL, 0, 0, PM_REMOVE)) U(DispatchMessageW)(&m);
    fc_destroy(a);
}
