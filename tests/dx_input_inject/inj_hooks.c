/* dx_input_inject: injected events as seen by WH_KEYBOARD_LL / WH_MOUSE_LL.
 *
 * Kernel facts: the kernel builds the KBDLLHOOKSTRUCT source from (vk, scan, flags, time, dwExtraInfo) and
 * hands it to key processing; mouse injection does the same for mouse events with dwExtraInfo
 * passed through verbatim. Events that did not come from the hardware path carry LLKHF_INJECTED (0x10) / LLMHF_INJECTED (1).
 * With KEYEVENTF_UNICODE and wVk == 0 (and no flag other than KEYUP) the key is VK_PACKET (0xE7); the kernel
 * takes the virtual key as a BYTE. A low-level hook that returns non-zero swallows the event.
 */
#include "inj.h"

#define NEEDHOOK(kb, ms) do { INJ_ENV(); inj_arm(); if (!inj_hook_begin(kb, ms)) { inj_hook_end(); t_skip("cannot install LL hook"); return; } } while (0)

TEST(dx_input_inject_ll_hook_install_remove) {
    INJ_ENV(); inj_arm(); REQUIRE(U(SetWindowsHookExW)); REQUIRE(U(UnhookWindowsHookEx));
    HHOOK a = inj_hook_install(WH_KEYBOARD_LL, (HOOKPROC)U(DefWindowProcW));
    HHOOK b = inj_hook_install(WH_MOUSE_LL, (HOOKPROC)U(DefWindowProcW));
    CHECK(a != NULL); CHECK(b != NULL);
    CHECK(a != b);
    if (a) { CHECK(U(UnhookWindowsHookEx)(a)); SET_ERR(0); CHECK(!U(UnhookWindowsHookEx)(a)); EXPECT_ERR(ERROR_INVALID_HOOK_HANDLE); }
    if (b) CHECK(U(UnhookWindowsHookEx)(b));
}

/* table: VK_F13..VK_F24 down+up each seen once, in order, with INJECTED, UP and the tag intact */
TEST(dx_input_inject_ll_kb_fkeys_table) {
    NEEDHOOK(1, 0);
    for (WORD vk = VK_F13; vk <= VK_F24; vk++) {
        ULONG_PTR t = inj_tag();
        inj_ev_clear();
        EXPECT_EQ(inj_keydown(vk, 0, t), 1);
        EXPECT_EQ(inj_keyup(vk, 0, t), 1);
        inj_pump(30);
        int d = inj_ev_find(0, t, 0), u = d >= 0 ? inj_ev_find(0, t, d + 1) : -1;
        CHECKF(d >= 0 && u > d, "vk 0x%x d=%d u=%d", vk, d, u);
        if (d >= 0) {
            CHECK(inj_ev(d)->wp == WM_KEYDOWN || inj_ev(d)->wp == WM_SYSKEYDOWN);
            EXPECT_EQ(inj_ev(d)->vk, vk);
            CHECK(inj_ev(d)->flags & LLKHF_INJECTED);
            CHECK(!(inj_ev(d)->flags & LLKHF_UP));
        }
        if (u > d && u >= 0) {
            CHECK(inj_ev(u)->wp == WM_KEYUP || inj_ev(u)->wp == WM_SYSKEYUP);
            EXPECT_EQ(inj_ev(u)->vk, vk);
            CHECK(inj_ev(u)->flags & LLKHF_INJECTED);
            CHECK(inj_ev(u)->flags & LLKHF_UP);
        }
    }
    inj_hook_end(); inj_restore();
}

/* dwExtraInfo passes through unchanged for keyboard events (full pointer width) */
TEST(dx_input_inject_ll_kb_extrainfo_passthrough) {
    NEEDHOOK(1, 0);
    static const ULONG_PTR vals[] = { 1, 0x7F, 0xFFFF, 0x80000000u, 0xFFFFFFFFu, 0x100000000ull, 0x123456789ABCDEFull, (ULONG_PTR)-1, (ULONG_PTR)1 << 63 };
    for (size_t i = 0; i < sizeof vals / sizeof *vals; i++) {
        inj_ev_clear();
        inj_keydown(VK_F24, 0, vals[i]); inj_keyup(VK_F24, 0, vals[i]);
        inj_pump(30);
        int d = inj_ev_find_vk(VK_F24, 0);
        CHECKF(d >= 0, "no event for extra %llx", (unsigned long long)vals[i]);
        if (d >= 0) CHECKF(inj_ev(d)->extra == vals[i], "got %llx want %llx", (unsigned long long)inj_ev(d)->extra, (unsigned long long)vals[i]);
    }
    inj_ev_clear();
    inj_keydown(VK_F24, 0, 0); inj_keyup(VK_F24, 0, 0); inj_pump(30);
    int d = inj_ev_find_vk(VK_F24, 0);
    CHECK(d >= 0);
    if (d >= 0) EXPECT_EQ(inj_ev(d)->extra, 0);
    inj_hook_end(); inj_restore();
}

TEST(dx_input_inject_ll_kb_time_is_current) {
    NEEDHOOK(1, 0);
    ULONG_PTR t = inj_tag();
    DWORD t0 = GetTickCount();
    inj_tap(VK_F24, 0, t); inj_pump(30);
    int d = inj_ev_find(0, t, 0);
    CHECK(d >= 0);
    if (d >= 0) {
        int dt = (int)(inj_ev(d)->time - t0);
        CHECKF(dt >= -50 && dt < 3000, "hook time %lu vs tick %lu", inj_ev(d)->time, t0);
    }
    inj_hook_end(); inj_restore();
}

TEST(dx_input_inject_ll_kb_extended_flag) {
    NEEDHOOK(1, 0);
    ULONG_PTR a = inj_tag(), b = inj_tag();
    inj_tap(VK_F24, KEYEVENTF_EXTENDEDKEY, a);
    inj_tap(VK_F24, 0, b);
    inj_pump(30);
    int i = inj_ev_find(0, a, 0), j = inj_ev_find(0, b, 0);
    CHECK(i >= 0 && j >= 0);
    if (i >= 0) CHECK(inj_ev(i)->flags & LLKHF_EXTENDED);
    if (j >= 0) CHECK(!(inj_ev(j)->flags & LLKHF_EXTENDED));
    inj_hook_end(); inj_restore();
}

TEST(dx_input_inject_ll_kb_scancode_flag) {
    NEEDHOOK(1, 0);
    UINT sc = U(MapVirtualKeyW) ? U(MapVirtualKeyW)(VK_F24, MAPVK_VK_TO_VSC) : 0;
    if (!sc) { t_skip("no scancode for F24 on this layout"); inj_hook_end(); return; }
    ULONG_PTR t = inj_tag();
    INPUT in[2];
    inj_key(&in[0], 0, (WORD)sc, KEYEVENTF_SCANCODE, t);
    inj_key(&in[1], 0, (WORD)sc, KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP, t);
    EXPECT_EQ(inj_send(2, in), 2);
    inj_pump(30);
    int d = inj_ev_find(0, t, 0);
    CHECK(d >= 0);
    if (d >= 0) {
        CHECK(inj_ev(d)->flags & LLKHF_INJECTED);
        EXPECT_EQ(inj_ev(d)->scan, sc);                         /* the hook reports the scan code the caller supplied */
        if (inj_ev(d)->vk != VK_F24) t_note("scancode 0x%x translated to vk 0x%lx (layout dependent)", sc, inj_ev(d)->vk);
    }
    inj_hook_end(); inj_restore();
}

/* A hook that returns non-zero swallows the event: it is delivered to the hook, never to the focus window. */
TEST(dx_input_inject_ll_kb_blocked_event) {
    INJ_ENV(); HWND w = inj_fg_begin(); if (!w) return;
    if (!inj_hook_begin(1, 0)) { inj_fg_end(w); t_skip("cannot install LL hook"); return; }
    inj_block_vk = VK_F24;
    ULONG_PTR t = inj_tag();
    inj_tap(VK_F24, 0, t); inj_pump(60);
    CHECK(inj_ev_find(0, t, 0) >= 0);                               /* the hook saw it */
    CHECK(inj_msg_find(WM_KEYDOWN, 0) < 0 && inj_msg_find(WM_SYSKEYDOWN, 0) < 0);   /* the window did not */
    CHECK(!(U(GetAsyncKeyState)(VK_F24) & 0x8000));
    inj_block_vk = 0;
    inj_msg_clear();
    inj_tap(VK_F24, 0, t); inj_pump(60);
    CHECK(inj_msg_find(WM_KEYDOWN, 0) >= 0);                        /* unblocked again: delivered */
    inj_fg_end(w);
}

/* After UnhookWindowsHookEx the hook is not called again. */
TEST(dx_input_inject_ll_unhook_stops_events) {
    NEEDHOOK(1, 1);
    ULONG_PTR t = inj_tag();
    inj_tap(VK_F24, 0, t); inj_pump(30);
    int n1 = inj_ev_n();
    CHECK(inj_ev_find(0, t, 0) >= 0);
    inj_hook_end();
    inj_tap(VK_F24, 0, t); inj_pump(30);
    EXPECT_EQ(inj_ev_n(), n1);
    inj_restore();
}

/* Two LL hooks on the same thread: the most recently installed one is called first. */
static int g_order[8], g_norder;
static LRESULT CALLBACK ordA(int c, WPARAM w, LPARAM l) { KBDLLHOOKSTRUCT *k = (KBDLLHOOKSTRUCT *)l; if (c == HC_ACTION && k->vkCode == VK_F24 && w == WM_KEYDOWN && g_norder < 8) g_order[g_norder++] = 'A'; return U(CallNextHookEx)(NULL, c, w, l); }
static LRESULT CALLBACK ordB(int c, WPARAM w, LPARAM l) { KBDLLHOOKSTRUCT *k = (KBDLLHOOKSTRUCT *)l; if (c == HC_ACTION && k->vkCode == VK_F24 && w == WM_KEYDOWN && g_norder < 8) g_order[g_norder++] = 'B'; return U(CallNextHookEx)(NULL, c, w, l); }
TEST(dx_input_inject_ll_hook_chain_order) {
    INJ_ENV(); inj_arm();
    HHOOK a = inj_hook_install(WH_KEYBOARD_LL, ordA), b = inj_hook_install(WH_KEYBOARD_LL, ordB);
    if (!a || !b) { inj_hook_remove(a); inj_hook_remove(b); t_skip("cannot install hooks"); return; }
    g_norder = 0;
    inj_tap(VK_F24, 0, inj_tag()); inj_pump(60);
    CHECKF(g_norder == 2, "calls %d", g_norder);
    if (g_norder == 2) { CHECK(g_order[0] == 'B'); CHECK(g_order[1] == 'A'); }
    inj_hook_remove(b);
    g_norder = 0;
    inj_tap(VK_F24, 0, inj_tag()); inj_pump(60);
    CHECK(g_norder == 1 && g_order[0] == 'A');
    inj_hook_remove(a);
    inj_restore();
}

/* The hook also sees the key state it was injected under: Alt held -> LLKHF_ALTDOWN and a SYS message. */
TEST(dx_input_inject_ll_kb_altdown_flag) {
    INJ_ENV(); HWND w = inj_fg_begin(); if (!w) return;
    if (!inj_hook_begin(1, 0)) { inj_fg_end(w); t_skip("cannot install LL hook"); return; }
    ULONG_PTR t = inj_tag(), m = inj_tag();
    inj_keydown(VK_MENU, 0, m);
    inj_keydown(VK_F24, 0, t);
    inj_keyup(VK_F24, 0, t);
    inj_keyup(VK_MENU, 0, m);
    inj_pump(60);
    int d = inj_ev_find(0, t, 0), a = inj_ev_find(0, m, 0);
    CHECK(d >= 0 && a >= 0);
    if (d >= 0) { CHECK(inj_ev(d)->flags & LLKHF_ALTDOWN); CHECK(inj_ev(d)->wp == WM_SYSKEYDOWN); }
    if (a >= 0) { CHECK(inj_ev(a)->wp == WM_SYSKEYDOWN); CHECK(inj_ev(a)->vk == VK_LMENU); }   /* VK_MENU is reported as the left variant */
    inj_fg_end(w);
}

/* modifier VKs are reported as the L/R variants the kernel picks (kernel: 0x10->0xA0/0xA1, 0x11->0xA2|ext, 0x12->0xA4|ext) */
TEST(dx_input_inject_ll_kb_modifier_variants) {
    INJ_ENV(); HWND w = inj_fg_begin(); if (!w) return;
    if (!inj_hook_begin(1, 0)) { inj_fg_end(w); t_skip("cannot install LL hook"); return; }
    static const struct { WORD vk; DWORD fl; DWORD want; } t[] = {
        { VK_SHIFT, 0, VK_LSHIFT }, { VK_CONTROL, 0, VK_LCONTROL }, { VK_CONTROL, KEYEVENTF_EXTENDEDKEY, VK_RCONTROL },
        { VK_MENU, 0, VK_LMENU }, { VK_MENU, KEYEVENTF_EXTENDEDKEY, VK_RMENU }, { VK_LSHIFT, 0, VK_LSHIFT }, { VK_RSHIFT, 0, VK_RSHIFT },
    };
    for (size_t i = 0; i < sizeof t / sizeof *t; i++) {
        ULONG_PTR tag = inj_tag(); inj_ev_clear();
        inj_keydown(t[i].vk, t[i].fl, tag); inj_keyup(t[i].vk, t[i].fl, tag); inj_pump(30);
        int d = inj_ev_find(0, tag, 0);
        CHECKF(d >= 0, "i=%zu", i);
        if (d >= 0) CHECKF(inj_ev(d)->vk == t[i].want, "i=%zu vk 0x%x -> 0x%lx want 0x%lx", i, t[i].vk, inj_ev(d)->vk, t[i].want);
    }
    inj_fg_end(w);
}

/* VK_PACKET: KEYEVENTF_UNICODE with wVk==0 -> hook sees vkCode 0xE7 and the UTF-16 unit in scanCode. */
TEST(dx_input_inject_ll_kb_unicode_packet) {
    INJ_ENV(); HWND w = inj_fg_begin(); if (!w) return;
    if (!inj_hook_begin(1, 0)) { inj_fg_end(w); t_skip("cannot install LL hook"); return; }
    static const WORD ch[] = { L'Z', 0x00E9, 0x20AC, 0x3042 };
    for (size_t i = 0; i < sizeof ch / sizeof *ch; i++) {
        ULONG_PTR tag = inj_tag(); inj_ev_clear();
        INPUT in[2]; inj_key(&in[0], 0, ch[i], KEYEVENTF_UNICODE, tag); inj_key(&in[1], 0, ch[i], KEYEVENTF_UNICODE | KEYEVENTF_KEYUP, tag);
        EXPECT_EQ(inj_send(2, in), 2);
        inj_pump(30);
        int d = inj_ev_find(0, tag, 0);
        CHECKF(d >= 0, "ch 0x%x", ch[i]);
        if (d >= 0) {
            EXPECT_EQ(inj_ev(d)->vk, 0xE7);
            CHECK(inj_ev(d)->flags & LLKHF_INJECTED);
            if (inj_ev(d)->scan != ch[i]) t_note("VK_PACKET scanCode 0x%lx for U+%04x", inj_ev(d)->scan, ch[i]);
        }
    }
    inj_fg_end(w);
}

/* ---- mouse ---- */
static int over_window(HWND w) { return inj_cursor_into(w); }

TEST(dx_input_inject_ll_mouse_move_injected) {
    NEEDHOOK(0, 1);
    RECT p; inj_screen(&p, NULL);
    ULONG_PTR t = inj_tag();
    inj_mouse1(inj_abs(p.right / 2, p.right), inj_abs(p.bottom / 2, p.bottom), 0, MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE, t);
    inj_pump(60);
    int i = inj_ev_find(1, t, 0);
    CHECK(i >= 0);
    if (i >= 0) {
        EXPECT_EQ(inj_ev(i)->wp, WM_MOUSEMOVE);
        CHECK(inj_ev(i)->flags & LLMHF_INJECTED);
        CHECK(inj_near(inj_ev(i)->x, p.right / 2, 1) && inj_near(inj_ev(i)->y, p.bottom / 2, 1));
        int dt = (int)(inj_ev(i)->time - GetTickCount());
        CHECK(dt > -3000 && dt < 50);
    }
    inj_hook_end(); inj_restore();
}

TEST(dx_input_inject_ll_mouse_extrainfo_passthrough) {
    NEEDHOOK(0, 1);
    RECT p; inj_screen(&p, NULL);
    static const ULONG_PTR vals[] = { 1, 0xFFFFFFFFu, 0x100000000ull, 0x123456789ABCDEFull, (ULONG_PTR)-1 };
    for (size_t i = 0; i < sizeof vals / sizeof *vals; i++) {
        inj_ev_clear();
        int x = p.right / 2 + (int)i * 3;
        inj_mouse1(inj_abs(x, p.right), inj_abs(p.bottom / 2, p.bottom), 0, MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE, vals[i]);
        inj_pump(30);
        int k = inj_ev_find(1, vals[i], 0);
        CHECKF(k >= 0, "extra %llx", (unsigned long long)vals[i]);
    }
    inj_hook_end(); inj_restore();
}

/* Buttons, wheel and X buttons on our own window: LL wParam message, mouseData and INJECTED flag. */
TEST(dx_input_inject_ll_mouse_buttons_table) {
    INJ_ENV(); HWND w = inj_fg_begin(); if (!w) return;
    if (!inj_hook_begin(0, 1)) { inj_fg_end(w); t_skip("cannot install LL hook"); return; }
    if (!over_window(w)) { inj_fg_end(w); t_skip("test window is not under the cursor"); return; }
    if (U(GetSystemMetrics)(SM_SWAPBUTTON)) { inj_fg_end(w); t_skip("mouse buttons are swapped"); return; }
    static const struct { DWORD down, up; UINT mdown, mup; } t[] = {
        { MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP, WM_LBUTTONDOWN, WM_LBUTTONUP },
        { MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP, WM_RBUTTONDOWN, WM_RBUTTONUP },
        { MOUSEEVENTF_MIDDLEDOWN, MOUSEEVENTF_MIDDLEUP, WM_MBUTTONDOWN, WM_MBUTTONUP },
    };
    for (size_t i = 0; i < sizeof t / sizeof *t; i++) {
        ULONG_PTR tag = inj_tag(); inj_ev_clear();
        EXPECT_EQ(inj_mouse1(0, 0, 0, t[i].down, tag), 1);
        EXPECT_EQ(inj_mouse1(0, 0, 0, t[i].up, tag), 1);
        inj_pump(30);
        int d = inj_ev_find(1, tag, 0), u = d >= 0 ? inj_ev_find(1, tag, d + 1) : -1;
        CHECKF(d >= 0 && u > d, "i=%zu", i);
        if (d >= 0) { EXPECT_EQ(inj_ev(d)->wp, t[i].mdown); CHECK(inj_ev(d)->flags & LLMHF_INJECTED); }
        if (u > 0) { EXPECT_EQ(inj_ev(u)->wp, t[i].mup); CHECK(inj_ev(u)->flags & LLMHF_INJECTED); }
    }
    inj_fg_end(w);
}

TEST(dx_input_inject_ll_mouse_wheel_data) {
    INJ_ENV(); HWND w = inj_fg_begin(); if (!w) return;
    if (!inj_hook_begin(0, 1)) { inj_fg_end(w); t_skip("cannot install LL hook"); return; }
    if (!over_window(w)) { inj_fg_end(w); t_skip("test window is not under the cursor"); return; }
    static const int deltas[] = { 120, -120, 240, 1, -1, 30000 };
    for (size_t i = 0; i < sizeof deltas / sizeof *deltas; i++) {
        ULONG_PTR tag = inj_tag(); inj_ev_clear();
        EXPECT_EQ(inj_mouse1(0, 0, (DWORD)deltas[i], MOUSEEVENTF_WHEEL, tag), 1);
        inj_pump(30);
        int k = inj_ev_find(1, tag, 0);
        CHECKF(k >= 0, "delta %d", deltas[i]);
        if (k >= 0) {
            EXPECT_EQ(inj_ev(k)->wp, WM_MOUSEWHEEL);
            CHECKF((short)HIWORD(inj_ev(k)->mdata) == (short)deltas[i], "mouseData 0x%lx want delta %d", inj_ev(k)->mdata, deltas[i]);
        }
    }
    ULONG_PTR tag = inj_tag(); inj_ev_clear();
    inj_mouse1(0, 0, 120, MOUSEEVENTF_HWHEEL, tag); inj_pump(30);
    int k = inj_ev_find(1, tag, 0);
    CHECK(k >= 0);
    if (k >= 0) { EXPECT_EQ(inj_ev(k)->wp, WM_MOUSEHWHEEL); CHECK((short)HIWORD(inj_ev(k)->mdata) == 120); }
    inj_fg_end(w);
}

TEST(dx_input_inject_ll_mouse_xbuttons) {
    INJ_ENV(); HWND w = inj_fg_begin(); if (!w) return;
    if (!inj_hook_begin(0, 1)) { inj_fg_end(w); t_skip("cannot install LL hook"); return; }
    if (!over_window(w)) { inj_fg_end(w); t_skip("test window is not under the cursor"); return; }
    for (DWORD x = XBUTTON1; x <= XBUTTON2; x++) {
        ULONG_PTR tag = inj_tag(); inj_ev_clear();
        inj_mouse1(0, 0, x, MOUSEEVENTF_XDOWN, tag); inj_mouse1(0, 0, x, MOUSEEVENTF_XUP, tag); inj_pump(30);
        int d = inj_ev_find(1, tag, 0), u = d >= 0 ? inj_ev_find(1, tag, d + 1) : -1;
        CHECK(d >= 0 && u > d);
        if (d >= 0) { EXPECT_EQ(inj_ev(d)->wp, WM_XBUTTONDOWN); EXPECT_EQ(HIWORD(inj_ev(d)->mdata), x); }
        if (u > 0) { EXPECT_EQ(inj_ev(u)->wp, WM_XBUTTONUP); EXPECT_EQ(HIWORD(inj_ev(u)->mdata), x); }
    }
    inj_fg_end(w);
}

/* Returning non-zero from the LL mouse hook swallows an absolute move: the cursor does not get there. */
TEST(dx_input_inject_ll_mouse_blocked_move) {
    NEEDHOOK(0, 1);
    RECT p; inj_screen(&p, NULL);
    U(SetCursorPos)(p.right / 4, p.bottom / 4);
    inj_pump(20); inj_ev_clear();
    POINT a, b; U(GetCursorPos)(&a);
    inj_block_mouse_msg = WM_MOUSEMOVE;
    ULONG_PTR t = inj_tag();
    inj_mouse1(inj_abs(p.right * 3 / 4, p.right), inj_abs(p.bottom * 3 / 4, p.bottom), 0, MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE, t);
    inj_pump(60);
    inj_block_mouse_msg = 0;
    U(GetCursorPos)(&b);
    CHECK(inj_ev_find(1, t, 0) >= 0);
    CHECKF(inj_near(a.x, b.x, 3) && inj_near(a.y, b.y, 3), "cursor moved %ld,%ld -> %ld,%ld despite blocking hook", a.x, a.y, b.x, b.y);
    inj_hook_end(); inj_restore();
}
