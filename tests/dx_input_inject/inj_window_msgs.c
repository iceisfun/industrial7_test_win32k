/* dx_input_inject: injected input as delivered to a focused private window, and the key state it leaves behind.
 *
 * Kernel facts: injected keyboard input goes to the foreground queue's focus window; the async key state table
 * (read by NtUserGetAsyncKeyState) is updated synchronously inside SendInput,
 * the per-queue state read by GetKeyState follows when the queue's message is processed. lParam of WM_KEYDOWN/UP carries
 * repeat count (0-15), scan code (16-23), extended (24), previous state (30) and transition (31).
 * User32 forms WM_CHAR from the key messages in TranslateMessage; VK_PACKET (0xE7) key messages become WM_CHAR with the
 * UTF-16 unit that was injected via KEYEVENTF_UNICODE.
 */
#include "inj.h"

#define PREP() HWND w = inj_fg_begin(); if (!w) return

static int scan_of(WORD vk) { return U(MapVirtualKeyW) ? (int)U(MapVirtualKeyW)(vk, MAPVK_VK_TO_VSC) : 0; }

TEST(dx_input_inject_win_keydown_keyup_lparam) {
    INJ_ENV(); PREP();
    ULONG_PTR t = inj_tag();
    EXPECT_EQ(inj_keydown(VK_F24, 0, t), 1);
    inj_pump(50);
    int d = inj_msg_find(WM_KEYDOWN, 0);
    CHECK(d >= 0);
    if (d >= 0) {
        struct inj_msg *m = inj_msg(d);
        EXPECT_EQ(m->wp, VK_F24);
        CHECK((m->lp & 0xFFFF) == 1);                    /* repeat count */
        CHECK(!(m->lp & (1 << 24)));                     /* not extended */
        CHECK(!(m->lp & (1 << 30)));                     /* previously up */
        CHECK(!(m->lp & (1u << 31)));                    /* transition: pressed */
        int sc = scan_of(VK_F24);
        if (sc) CHECK(((m->lp >> 16) & 0xFF) == (unsigned)sc);
        CHECK(m->extra == (LPARAM)t);                    /* GetMessageExtraInfo during the message */
    }
    inj_msg_clear();
    EXPECT_EQ(inj_keyup(VK_F24, 0, t), 1);
    inj_pump(50);
    int u = inj_msg_find(WM_KEYUP, 0);
    CHECK(u >= 0);
    if (u >= 0) {
        struct inj_msg *m = inj_msg(u);
        EXPECT_EQ(m->wp, VK_F24);
        CHECK(m->lp & (1 << 30));                        /* previous state down */
        CHECK(m->lp & (1u << 31));                       /* transition: released */
        CHECK(m->extra == (LPARAM)t);
    }
    inj_fg_end(w);
}

/* a second KEYDOWN without an intervening KEYUP is an auto-repeat: previous-state bit set */
TEST(dx_input_inject_win_key_autorepeat_bit) {
    INJ_ENV(); PREP();
    inj_keydown(VK_F24, 0, 0); inj_keydown(VK_F24, 0, 0); inj_keydown(VK_F24, 0, 0);
    inj_pump(60);
    int a = inj_msg_find(WM_KEYDOWN, 0), b = a >= 0 ? inj_msg_find(WM_KEYDOWN, a + 1) : -1;
    CHECK(a >= 0 && b >= 0);
    if (a >= 0) CHECK(!(inj_msg(a)->lp & (1 << 30)));
    if (b >= 0) CHECK(inj_msg(b)->lp & (1 << 30));
    inj_keyup(VK_F24, 0, 0); inj_pump(30);
    inj_fg_end(w);
}

TEST(dx_input_inject_win_extended_bit) {
    INJ_ENV(); PREP();
    inj_tap(VK_F24, KEYEVENTF_EXTENDEDKEY, 0);
    inj_pump(50);
    int d = inj_msg_find(WM_KEYDOWN, 0);
    CHECK(d >= 0);
    if (d >= 0) CHECK(inj_msg(d)->lp & (1 << 24));
    inj_fg_end(w);
}

/* WM_CHAR from virtual keys with a layout-independent character */
TEST(dx_input_inject_win_wm_char_table) {
    INJ_ENV(); PREP();
    static const struct { WORD vk; WPARAM ch; } t[] = { { VK_SPACE, ' ' }, { VK_RETURN, '\r' }, { VK_BACK, 8 }, { VK_TAB, '\t' }, { VK_ESCAPE, 27 } };
    for (size_t i = 0; i < sizeof t / sizeof *t; i++) {
        inj_msg_clear();
        inj_tap(t[i].vk, 0, 0);
        inj_pump(50);
        int d = inj_msg_find(WM_KEYDOWN, 0);
        CHECKF(d >= 0, "vk 0x%x keydown", t[i].vk);
        if (d >= 0) EXPECT_EQ(inj_msg(d)->wp, t[i].vk);
        int c = inj_msg_find(WM_CHAR, 0);
        CHECKF(c >= 0, "vk 0x%x produced no WM_CHAR", t[i].vk);
        if (c >= 0) EXPECT_EQ(inj_msg(c)->wp, t[i].ch);
        CHECK(inj_msg_find(WM_KEYUP, 0) >= 0);
        CHECK(inj_msg_count(WM_CHAR) == 1);
    }
    inj_fg_end(w);
}

/* F-keys produce no WM_CHAR */
TEST(dx_input_inject_win_fkey_no_char) {
    INJ_ENV(); PREP();
    inj_tap(VK_F24, 0, 0); inj_tap(VK_F13, 0, 0);
    inj_pump(60);
    CHECK(inj_msg_count(WM_KEYDOWN) == 2);
    CHECK(inj_msg_count(WM_CHAR) == 0);
    inj_fg_end(w);
}

/* KEYEVENTF_UNICODE -> VK_PACKET key messages and a WM_CHAR with the injected UTF-16 unit */
TEST(dx_input_inject_win_unicode_char_table) {
    INJ_ENV(); PREP();
    static const WORD ch[] = { L'Z', L'7', 0x00E9, 0x00DF, 0x03A9, 0x20AC, 0x3042, 0x4E2D, 0xFB01 };
    for (size_t i = 0; i < sizeof ch / sizeof *ch; i++) {
        inj_msg_clear();
        INPUT in[2]; inj_key(&in[0], 0, ch[i], KEYEVENTF_UNICODE, 0); inj_key(&in[1], 0, ch[i], KEYEVENTF_UNICODE | KEYEVENTF_KEYUP, 0);
        EXPECT_EQ(inj_send(2, in), 2);
        inj_pump(50);
        int d = inj_msg_find(WM_KEYDOWN, 0);
        CHECKF(d >= 0, "U+%04x no keydown", ch[i]);
        if (d >= 0) EXPECT_EQ(inj_msg(d)->wp, 0xE7);
        int c = inj_msg_find(WM_CHAR, 0);
        CHECKF(c >= 0, "U+%04x no WM_CHAR", ch[i]);
        if (c >= 0) EXPECT_EQ(inj_msg(c)->wp, ch[i]);
        CHECK(inj_msg_count(WM_CHAR) == 1);
    }
    inj_fg_end(w);
}

/* A surrogate pair injected as two packets arrives as two WM_CHARs in order. */
TEST(dx_input_inject_win_unicode_surrogates) {
    INJ_ENV(); PREP();
    INPUT in[4];
    inj_key(&in[0], 0, 0xD83D, KEYEVENTF_UNICODE, 0); inj_key(&in[1], 0, 0xD83D, KEYEVENTF_UNICODE | KEYEVENTF_KEYUP, 0);
    inj_key(&in[2], 0, 0xDE00, KEYEVENTF_UNICODE, 0); inj_key(&in[3], 0, 0xDE00, KEYEVENTF_UNICODE | KEYEVENTF_KEYUP, 0);
    EXPECT_EQ(inj_send(4, in), 4);
    inj_pump(60);
    int a = inj_msg_find(WM_CHAR, 0), b = a >= 0 ? inj_msg_find(WM_CHAR, a + 1) : -1;
    CHECK(a >= 0 && b > a);
    if (a >= 0) EXPECT_EQ(inj_msg(a)->wp, 0xD83D);
    if (b >= 0) EXPECT_EQ(inj_msg(b)->wp, 0xDE00);
    inj_fg_end(w);
}

/* KEYEVENTF_SCANCODE: the system maps the scan code back to a virtual key for the message. */
TEST(dx_input_inject_win_scancode_flag) {
    INJ_ENV(); PREP();
    int sc = scan_of(VK_F24);
    if (!sc) { inj_fg_end(w); t_skip("no scan code for F24"); return; }
    INPUT in[2]; inj_key(&in[0], 0, (WORD)sc, KEYEVENTF_SCANCODE, 0); inj_key(&in[1], 0, (WORD)sc, KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP, 0);
    EXPECT_EQ(inj_send(2, in), 2);
    inj_pump(60);
    int d = inj_msg_find(WM_KEYDOWN, 0);
    CHECK(d >= 0);
    if (d >= 0) {
        if (inj_msg(d)->wp != VK_F24) t_note("scan 0x%x delivered vk 0x%llx", sc, (unsigned long long)inj_msg(d)->wp);
        CHECK(((inj_msg(d)->lp >> 16) & 0xFF) == (unsigned)sc);
    }
    inj_fg_end(w);
}

/* Async key state tracks the injected transition immediately; GetKeyState follows once the queue consumed the message. */
TEST(dx_input_inject_key_state_down_up) {
    INJ_ENV(); PREP();
    CHECK(!(U(GetAsyncKeyState)(VK_F24) & 0x8000));
    inj_keydown(VK_F24, 0, 0);
    CHECK(U(GetAsyncKeyState)(VK_F24) & 0x8000);
    inj_pump(50);
    CHECK(U(GetKeyState)(VK_F24) < 0);
    inj_keyup(VK_F24, 0, 0);
    CHECK(!(U(GetAsyncKeyState)(VK_F24) & 0x8000));
    inj_pump(50);
    CHECK(U(GetKeyState)(VK_F24) >= 0);
    inj_fg_end(w);
}

/* Table of modifier / extended variants: GetAsyncKeyState of the aggregate and the specific side keys. */
TEST(dx_input_inject_key_state_modifiers) {
    INJ_ENV(); PREP();
    static const struct { WORD send; DWORD fl; WORD agg; WORD side; } t[] = {
        { VK_SHIFT, 0, VK_SHIFT, VK_LSHIFT }, { VK_LSHIFT, 0, VK_SHIFT, VK_LSHIFT }, { VK_RSHIFT, 0, VK_SHIFT, VK_RSHIFT },
        { VK_CONTROL, 0, VK_CONTROL, VK_LCONTROL }, { VK_CONTROL, KEYEVENTF_EXTENDEDKEY, VK_CONTROL, VK_RCONTROL },
        { VK_MENU, 0, VK_MENU, VK_LMENU }, { VK_MENU, KEYEVENTF_EXTENDEDKEY, VK_MENU, VK_RMENU },
    };
    for (size_t i = 0; i < sizeof t / sizeof *t; i++) {
        inj_keydown(t[i].send, t[i].fl, 0);
        CHECKF(U(GetAsyncKeyState)(t[i].side) & 0x8000, "i=%zu side down", i);
        CHECKF(U(GetAsyncKeyState)(t[i].agg) & 0x8000, "i=%zu aggregate down", i);
        inj_pump(30);
        CHECKF(U(GetKeyState)(t[i].side) < 0, "i=%zu GetKeyState side", i);
        CHECKF(U(GetKeyState)(t[i].agg) < 0, "i=%zu GetKeyState aggregate", i);
        inj_keyup(t[i].send, t[i].fl, 0);
        CHECKF(!(U(GetAsyncKeyState)(t[i].side) & 0x8000), "i=%zu side up", i);
        CHECKF(!(U(GetAsyncKeyState)(t[i].agg) & 0x8000), "i=%zu aggregate up", i);
        inj_pump(30);
    }
    inj_fg_end(w);
}

/* Toggle keys flip the low bit of GetKeyState; tapping twice restores. Scroll lock first (no side effects), then caps/num. */
TEST(dx_input_inject_key_state_toggles) {
    INJ_ENV(); PREP();
    static const WORD keys[] = { VK_SCROLL, VK_CAPITAL, VK_NUMLOCK };
    for (size_t i = 0; i < 3; i++) {
        int s0 = U(GetKeyState)(keys[i]) & 1;
        inj_tap(keys[i], keys[i] == VK_NUMLOCK ? KEYEVENTF_EXTENDEDKEY : 0, 0);
        inj_pump(60);
        int s1 = U(GetKeyState)(keys[i]) & 1;
        CHECKF(s1 != s0, "vk 0x%x did not toggle", keys[i]);
        inj_tap(keys[i], keys[i] == VK_NUMLOCK ? KEYEVENTF_EXTENDEDKEY : 0, 0);
        inj_pump(60);
        int s2 = U(GetKeyState)(keys[i]) & 1;
        CHECKF(s2 == s0, "vk 0x%x not restored", keys[i]);
    }
    inj_fg_end(w);
}

/* Held keys are not toggles: pressing twice without release (repeat) flips CapsLock only once per press edge. */
TEST(dx_input_inject_key_state_toggle_edge_only) {
    INJ_ENV(); PREP();
    int s0 = U(GetKeyState)(VK_SCROLL) & 1;
    inj_keydown(VK_SCROLL, 0, 0); inj_keydown(VK_SCROLL, 0, 0); inj_keydown(VK_SCROLL, 0, 0);
    inj_keyup(VK_SCROLL, 0, 0);
    inj_pump(60);
    int s1 = U(GetKeyState)(VK_SCROLL) & 1;
    CHECKF(s1 != s0, "toggle state after repeat run");
    inj_tap(VK_SCROLL, 0, 0); inj_pump(60);
    CHECK((U(GetKeyState)(VK_SCROLL) & 1) == s0);
    inj_fg_end(w);
}

/* GetKeyboardState reflects the same per-queue state as GetKeyState. */
TEST(dx_input_inject_keyboard_state_array) {
    INJ_ENV(); PREP(); REQUIRE(U(GetKeyboardState));
    BYTE st[256];
    memset(st, 0xCC, sizeof st);
    CHECK(U(GetKeyboardState)(st));
    CHECK(!(st[VK_F24] & 0x80));
    inj_keydown(VK_F24, 0, 0); inj_pump(50);
    memset(st, 0, sizeof st);
    CHECK(U(GetKeyboardState)(st));
    CHECK(st[VK_F24] & 0x80);
    inj_keyup(VK_F24, 0, 0); inj_pump(50);
    CHECK(U(GetKeyboardState)(st));
    CHECK(!(st[VK_F24] & 0x80));
    for (int i = 0; i < 256; i++) CHECKF((st[i] & 0x7E) == 0, "reserved bits of state[%d] = 0x%x", i, st[i]);   /* only bit 7 (down) and bit 0 (toggle) are used */
    inj_fg_end(w);
}

/* GetQueueStatus: injected input sets QS_KEY in the high word of the foreground queue until it is read. */
TEST(dx_input_inject_queue_status_key) {
    INJ_ENV(); PREP();
    inj_pump(30);
    inj_keydown(VK_F24, 0, 0);
    DWORD qs = U(GetQueueStatus)(QS_KEY);
    CHECKF(HIWORD(qs) & QS_KEY, "qs=0x%lx", qs);
    inj_pump(50);
    qs = U(GetQueueStatus)(QS_KEY);
    CHECKF(!(HIWORD(qs) & QS_KEY), "qs after drain=0x%lx", qs);
    inj_keyup(VK_F24, 0, 0); inj_pump(30);
    inj_fg_end(w);
}

/* ---- mouse into the window ---- */
static int mouse_prep(HWND w, POINT *c) {
    inj_win_center(w, c);
    if (!inj_cursor_into(w)) { t_skip("test window is not under the cursor"); return 0; }
    if (U(GetSystemMetrics)(SM_SWAPBUTTON)) { t_skip("mouse buttons are swapped"); return 0; }
    inj_pump(30); inj_msg_clear();
    return 1;
}

TEST(dx_input_inject_win_mousemove_client_coords) {
    INJ_ENV(); PREP();
    POINT c; inj_win_center(w, &c);
    RECT p; inj_screen(&p, NULL);
    if (!inj_cursor_into(w)) { inj_fg_end(w); t_skip("test window is not under the cursor"); return; }
    inj_pump(30); inj_msg_clear();
    POINT tgt = { c.x + 20, c.y - 10 };
    ULONG_PTR t = inj_tag();
    inj_mouse1(inj_abs(tgt.x, p.right), inj_abs(tgt.y, p.bottom), 0, MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE, t);
    inj_pump(60);
    int m = inj_msg_find(WM_MOUSEMOVE, 0);
    CHECK(m >= 0);
    if (m >= 0) {
        POINT cl = tgt; U(ScreenToClient)(w, &cl);
        CHECKF(inj_near((short)LOWORD(inj_msg(m)->lp), cl.x, 1) && inj_near((short)HIWORD(inj_msg(m)->lp), cl.y, 1),
               "lParam %d,%d want %ld,%ld", (short)LOWORD(inj_msg(m)->lp), (short)HIWORD(inj_msg(m)->lp), cl.x, cl.y);
        CHECK(inj_msg(m)->extra == (LPARAM)t);
        EXPECT_EQ(inj_msg(m)->wp & ~(WPARAM)(MK_SHIFT | MK_CONTROL), 0);
    }
    inj_fg_end(w);
}

/* Table: button down/up -> WM_xBUTTONDOWN/UP, wParam MK_ bit while pressed, coordinates are client coordinates. */
TEST(dx_input_inject_win_buttons_table) {
    INJ_ENV(); PREP();
    POINT c; if (!mouse_prep(w, &c)) { inj_fg_end(w); return; }
    static const struct { DWORD down, up; UINT mdown, mup; WPARAM mk; } t[] = {
        { MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP, WM_LBUTTONDOWN, WM_LBUTTONUP, MK_LBUTTON },
        { MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP, WM_RBUTTONDOWN, WM_RBUTTONUP, MK_RBUTTON },
        { MOUSEEVENTF_MIDDLEDOWN, MOUSEEVENTF_MIDDLEUP, WM_MBUTTONDOWN, WM_MBUTTONUP, MK_MBUTTON },
    };
    POINT cl = c; U(ScreenToClient)(w, &cl);
    for (size_t i = 0; i < sizeof t / sizeof *t; i++) {
        inj_msg_clear();
        ULONG_PTR tag = inj_tag();
        EXPECT_EQ(inj_mouse1(0, 0, 0, t[i].down, tag), 1);
        EXPECT_EQ(inj_mouse1(0, 0, 0, t[i].up, tag), 1);
        inj_pump(50);
        int d = inj_msg_find(t[i].mdown, 0), u = inj_msg_find(t[i].mup, 0);
        CHECKF(d >= 0 && u > d, "i=%zu", i);
        if (d >= 0) {
            CHECK((inj_msg(d)->wp & t[i].mk) == t[i].mk);
            CHECK(inj_near((short)LOWORD(inj_msg(d)->lp), cl.x, 2) && inj_near((short)HIWORD(inj_msg(d)->lp), cl.y, 2));
            CHECK(inj_msg(d)->extra == (LPARAM)tag);
        }
        if (u >= 0) CHECK((inj_msg(u)->wp & t[i].mk) == 0);
    }
    inj_fg_end(w);
}

/* The window class has CS_DBLCLKS: two quick clicks give DOWN, UP, DBLCLK, UP. */
TEST(dx_input_inject_win_double_click) {
    INJ_ENV(); PREP();
    POINT c; if (!mouse_prep(w, &c)) { inj_fg_end(w); return; }
    INPUT in[4];
    inj_mouse(&in[0], 0, 0, 0, MOUSEEVENTF_LEFTDOWN, 0); inj_mouse(&in[1], 0, 0, 0, MOUSEEVENTF_LEFTUP, 0);
    inj_mouse(&in[2], 0, 0, 0, MOUSEEVENTF_LEFTDOWN, 0); inj_mouse(&in[3], 0, 0, 0, MOUSEEVENTF_LEFTUP, 0);
    EXPECT_EQ(inj_send(4, in), 4);
    inj_pump(80);
    CHECK(inj_msg_count(WM_LBUTTONDOWN) == 1);
    CHECK(inj_msg_count(WM_LBUTTONDBLCLK) == 1);
    CHECK(inj_msg_count(WM_LBUTTONUP) == 2);
    inj_fg_end(w);
}

TEST(dx_input_inject_win_wheel) {
    INJ_ENV(); PREP();
    POINT c; if (!mouse_prep(w, &c)) { inj_fg_end(w); return; }
    static const int d[] = { 120, -120, 360, -240 };
    for (size_t i = 0; i < sizeof d / sizeof *d; i++) {
        inj_msg_clear();
        ULONG_PTR tag = inj_tag();
        EXPECT_EQ(inj_mouse1(0, 0, (DWORD)d[i], MOUSEEVENTF_WHEEL, tag), 1);
        inj_pump(50);
        int m = inj_msg_find(WM_MOUSEWHEEL, 0);
        CHECKF(m >= 0, "delta %d", d[i]);
        if (m >= 0) {
            CHECK((short)HIWORD(inj_msg(m)->wp) == d[i]);
            CHECK(inj_near((short)LOWORD(inj_msg(m)->lp), c.x, 2) && inj_near((short)HIWORD(inj_msg(m)->lp), c.y, 2));   /* screen coordinates */
            CHECK(inj_msg(m)->extra == (LPARAM)tag);
        }
    }
    inj_fg_end(w);
}

TEST(dx_input_inject_win_xbutton) {
    INJ_ENV(); PREP();
    POINT c; if (!mouse_prep(w, &c)) { inj_fg_end(w); return; }
    for (DWORD x = XBUTTON1; x <= XBUTTON2; x++) {
        inj_msg_clear();
        inj_mouse1(0, 0, x, MOUSEEVENTF_XDOWN, 0); inj_mouse1(0, 0, x, MOUSEEVENTF_XUP, 0);
        inj_pump(50);
        int d = inj_msg_find(WM_XBUTTONDOWN, 0), u = inj_msg_find(WM_XBUTTONUP, 0);
        CHECK(d >= 0 && u >= 0);
        if (d >= 0) { EXPECT_EQ(HIWORD(inj_msg(d)->wp), x); CHECK(LOWORD(inj_msg(d)->wp) & (x == XBUTTON1 ? MK_XBUTTON1 : MK_XBUTTON2)); }
        if (u >= 0) EXPECT_EQ(HIWORD(inj_msg(u)->wp), x);
    }
    inj_fg_end(w);
}

/* Held button state is visible through GetAsyncKeyState(VK_LBUTTON) immediately and after release. */
TEST(dx_input_inject_button_async_state) {
    INJ_ENV(); PREP();
    POINT c; if (!mouse_prep(w, &c)) { inj_fg_end(w); return; }
    CHECK(!(U(GetAsyncKeyState)(VK_LBUTTON) & 0x8000));
    inj_mouse1(0, 0, 0, MOUSEEVENTF_LEFTDOWN, 0);
    CHECK(U(GetAsyncKeyState)(VK_LBUTTON) & 0x8000);
    inj_pump(30);
    CHECK(U(GetKeyState)(VK_LBUTTON) < 0);
    inj_mouse1(0, 0, 0, MOUSEEVENTF_LEFTUP, 0);
    CHECK(!(U(GetAsyncKeyState)(VK_LBUTTON) & 0x8000));
    inj_pump(30);
    inj_fg_end(w);
}

/* With the buttons swapped, record what the injected LEFTDOWN produced (behaviour note) and always restore. */
TEST(dx_input_inject_swapped_buttons_injected_click) {
    INJ_ENV(); PREP(); REQUIRE(U(SwapMouseButton));
    POINT c; if (!mouse_prep(w, &c)) { inj_fg_end(w); return; }
    U(SwapMouseButton)(TRUE);
    inj_msg_clear();
    inj_mouse1(0, 0, 0, MOUSEEVENTF_LEFTDOWN, 0); inj_mouse1(0, 0, 0, MOUSEEVENTF_LEFTUP, 0);
    inj_pump(50);
    int l = inj_msg_count(WM_LBUTTONDOWN), r = inj_msg_count(WM_RBUTTONDOWN);
    U(SwapMouseButton)(FALSE);
    CHECK(l + r == 1);
    t_note("with swapped buttons an injected MOUSEEVENTF_LEFTDOWN produced %s", r ? "WM_RBUTTONDOWN" : "WM_LBUTTONDOWN");
    inj_fg_end(w);
}

/* GetMessageExtraInfo keeps the last message's extra info on the thread; SetMessageExtraInfo swaps it atomically. */
TEST(dx_input_inject_message_extra_info) {
    INJ_ENV(); PREP(); REQUIRE(U(SetMessageExtraInfo));
    LPARAM old = U(SetMessageExtraInfo)(0x1234);
    EXPECT_EQ(U(GetMessageExtraInfo)(), 0x1234);
    EXPECT_EQ(U(SetMessageExtraInfo)(0x5678), 0x1234);
    EXPECT_EQ(U(GetMessageExtraInfo)(), 0x5678);
    EXPECT_EQ(U(SetMessageExtraInfo)(old), 0x5678);
    ULONG_PTR t = inj_tag();
    inj_tap(VK_F24, 0, t); inj_pump(50);
    int d = inj_msg_find(WM_KEYDOWN, 0);
    CHECK(d >= 0);
    if (d >= 0) CHECK(inj_msg(d)->extra == (LPARAM)t);
    inj_msg_clear();
    inj_tap(VK_F24, 0, 0); inj_pump(50);
    d = inj_msg_find(WM_KEYDOWN, 0);
    if (d >= 0) CHECK(inj_msg(d)->extra == 0);
    U(SetMessageExtraInfo)(old);
    inj_fg_end(w);
}
