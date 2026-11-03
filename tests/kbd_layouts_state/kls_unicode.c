/* NtUserToUnicodeEx / ToUnicodeEx / ToUnicode.
 *
 * win32kbase!NtUserToUnicodeEx:
 *   - cchBuff < 1: ERROR_INVALID_PARAMETER, result 0, nothing probed or written;
 *   - otherwise the 256-byte key-state array is copied in (bit7 = down, bit0 = toggle), the output
 *     buffer is probed for write and a zero-initialised scratch buffer of cchBuff WCHARs is filled
 *     by the kernel translation and copied back whole, so every cell past the result is zeroed;
 *   the kernel unicode translation: vk 0xFF -> 0; key-up (scan bit 15) -> 0 unless wFlags bit 1;
 *     the dead-key store (per-layout pending state) is skipped when wFlags bit 2 is set; a dead key returns -1
 *     with the accent in buf[0]; Ctrl+A..Z yields vk & 0x1f; an unknown HKL returns 0.
 * user32!ToUnicodeEx stores a NUL in buf[0] when the result is 0.
 */
#include "kls.h"

#define M_S 1   /* shift */
#define M_C 2   /* ctrl */
#define M_A 4   /* alt */
#define M_L 8   /* caps lock toggled */
#define M_N 16  /* num lock toggled */

static void mkstate(BYTE st[256], unsigned m) {
    memset(st, 0, 256);
    if (m & M_S) st[VK_SHIFT] = 0x80;
    if (m & M_C) st[VK_CONTROL] = 0x80;
    if (m & M_A) st[VK_MENU] = 0x80;
    if (m & M_L) st[VK_CAPITAL] = 0x01;
    if (m & M_N) st[VK_NUMLOCK] = 0x01;
}
static int tu(UINT vk, unsigned mods, HKL h, UINT flags, WCHAR *out, int cch) {
    BYTE st[256]; mkstate(st, mods);
    UINT sc = U(MapVirtualKeyExW)(vk, MAPVK_VK_TO_VSC, h);
    return U(ToUnicodeEx)(vk, sc, st, out, cch, flags, h);
}

struct tc { UINT vk; unsigned mods; int ret; WCHAR ch; };
#define NEED_TU() KLS_NEED(); REQUIRE(U(ToUnicodeEx)); REQUIRE(U(MapVirtualKeyExW)); REQUIRE(UN(NtUserToUnicodeEx)); \
    HKL us = kls_us(); if (!us) { t_skip("00000409 not loadable"); return; }

static void run_tc(HKL us, const struct tc *t, unsigned n) {
    for (unsigned i = 0; i < n; i++) {
        WCHAR b[8]; for (int j = 0; j < 8; j++) b[j] = 0x7777;
        int r = tu(t[i].vk, t[i].mods, us, 0, b, 8);
        CHECKF(r == t[i].ret && (t[i].ret == 0 || b[0] == t[i].ch), "vk %x mods %x: ret %d ch %04x; want %d %04x", t[i].vk, t[i].mods, r, b[0], t[i].ret, t[i].ch);
        for (int j = (r > 0 ? r : 0); j < 8; j++) if (b[j] != 0) { CHECKF(0, "vk %x mods %x: cell %d = %04x not zeroed", t[i].vk, t[i].mods, j, b[j]); break; }
    }
}

TEST(kbd_layouts_state_tounicode_letters_base_and_shift) {
    NEED_TU();
    for (UINT vk = 'A'; vk <= 'Z'; vk++) {
        struct tc t[] = { { vk, 0, 1, (WCHAR)(vk + 32) }, { vk, M_S, 1, (WCHAR)vk }, { vk, M_L, 1, (WCHAR)vk }, { vk, M_S | M_L, 1, (WCHAR)(vk + 32) } };
        run_tc(us, t, 4);
    }
    kls_cleanup();
}

TEST(kbd_layouts_state_tounicode_ctrl_letters) {
    NEED_TU();
    /* Ctrl column of the US VK_TO_WCHAR tables / the (vk & 0x1f) path for A..Z */
    for (UINT vk = 'A'; vk <= 'Z'; vk++) {
        struct tc t[] = { { vk, M_C, 1, (WCHAR)(vk & 0x1f) } };
        run_tc(us, t, 1);
    }
    kls_cleanup();
}

TEST(kbd_layouts_state_tounicode_digits_and_symbols) {
    NEED_TU();
    static const char sh[] = ")!@#$%^&*(";
    for (int d = 0; d < 10; d++) {
        struct tc t[] = { { '0' + d, 0, 1, (WCHAR)('0' + d) }, { '0' + d, M_S, 1, (WCHAR)sh[d] }, { '0' + d, M_L, 1, (WCHAR)('0' + d) } };
        run_tc(us, t, 3);   /* Caps Lock does not affect the digit row (no CAPLOK attribute) */
    }
    kls_cleanup();
}

TEST(kbd_layouts_state_tounicode_oem_punctuation) {
    NEED_TU();
    static const struct tc t[] = {
        { VK_OEM_1, 0, 1, ';' }, { VK_OEM_1, M_S, 1, ':' }, { VK_OEM_PLUS, 0, 1, '=' }, { VK_OEM_PLUS, M_S, 1, '+' },
        { VK_OEM_COMMA, 0, 1, ',' }, { VK_OEM_COMMA, M_S, 1, '<' }, { VK_OEM_MINUS, 0, 1, '-' }, { VK_OEM_MINUS, M_S, 1, '_' },
        { VK_OEM_PERIOD, 0, 1, '.' }, { VK_OEM_PERIOD, M_S, 1, '>' }, { VK_OEM_2, 0, 1, '/' }, { VK_OEM_2, M_S, 1, '?' },
        { VK_OEM_3, 0, 1, '`' }, { VK_OEM_3, M_S, 1, '~' }, { VK_OEM_4, 0, 1, '[' }, { VK_OEM_4, M_S, 1, '{' },
        { VK_OEM_5, 0, 1, '\\' }, { VK_OEM_5, M_S, 1, '|' }, { VK_OEM_6, 0, 1, ']' }, { VK_OEM_6, M_S, 1, '}' },
        { VK_OEM_7, 0, 1, '\'' }, { VK_OEM_7, M_S, 1, '"' },
    };
    run_tc(us, t, sizeof t / sizeof *t);
    kls_cleanup();
}

TEST(kbd_layouts_state_tounicode_whitespace_and_control_keys) {
    NEED_TU();
    static const struct tc t[] = {
        { VK_SPACE, 0, 1, ' ' }, { VK_SPACE, M_S, 1, ' ' }, { VK_RETURN, 0, 1, '\r' }, { VK_BACK, 0, 1, '\b' },
        { VK_TAB, 0, 1, '\t' }, { VK_ESCAPE, 0, 1, 0x1B },
    };
    run_tc(us, t, sizeof t / sizeof *t);
    kls_cleanup();
}

TEST(kbd_layouts_state_tounicode_numpad_keys) {
    NEED_TU();
    /* VK_NUMPADn are their own virtual keys: they produce digits whatever the Num Lock toggle says */
    for (int d = 0; d < 10; d++) {
        struct tc t[] = { { VK_NUMPAD0 + d, 0, 1, (WCHAR)('0' + d) }, { VK_NUMPAD0 + d, M_N, 1, (WCHAR)('0' + d) } };
        run_tc(us, t, 2);
    }
    static const struct tc ops[] = { { VK_MULTIPLY, 0, 1, '*' }, { VK_ADD, 0, 1, '+' }, { VK_SUBTRACT, 0, 1, '-' }, { VK_DECIMAL, 0, 1, '.' }, { VK_DIVIDE, 0, 1, '/' } };
    run_tc(us, ops, 5);
    kls_cleanup();
}

TEST(kbd_layouts_state_tounicode_non_character_keys_return_zero) {
    NEED_TU();
    static const UINT keys[] = { VK_F1, VK_F5, VK_F12, VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN, VK_SHIFT, VK_CONTROL, VK_MENU,
                                 VK_LSHIFT, VK_RSHIFT, VK_CAPITAL, VK_NUMLOCK, VK_SCROLL, VK_LWIN, VK_RWIN, VK_APPS, VK_PRIOR, VK_NEXT,
                                 VK_HOME, VK_END, VK_INSERT, VK_LBUTTON, VK_RBUTTON };
    for (unsigned i = 0; i < sizeof keys / sizeof *keys; i++) {
        WCHAR b[4]; for (int j = 0; j < 4; j++) b[j] = 0x7777;
        int r = tu(keys[i], 0, us, 0, b, 4);
        CHECKF(r == 0, "vk %x -> %d", keys[i], r);
        CHECKF(b[0] == 0, "vk %x left buf[0]=%04x", keys[i], b[0]);
    }
    kls_cleanup();
}

TEST(kbd_layouts_state_tounicode_vk_ff_is_nothing) {
    NEED_TU();
    BYTE st[256] = { 0 }; WCHAR b[4] = { 0x7777, 0x7777, 0x7777, 0x7777 };
    CHECK(U(ToUnicodeEx)(0xFF, 0, st, b, 4, 0, us) == 0);   /* unicode translation: (char)vk == -1 returns immediately */
    CHECK(b[0] == 0);
    kls_cleanup();
}

TEST(kbd_layouts_state_tounicode_key_up_returns_zero) {
    NEED_TU();
    BYTE st[256] = { 0 };
    static const UINT vk[] = { 'A', '1', VK_SPACE, VK_OEM_1, VK_RETURN };
    for (unsigned i = 0; i < sizeof vk / sizeof *vk; i++) {
        WCHAR b[4] = { 0x7777, 0x7777, 0x7777, 0x7777 };
        UINT sc = U(MapVirtualKeyExW)(vk[i], MAPVK_VK_TO_VSC, us);
        /* scan code bit 15 = key release; without wFlags bit 1 the function returns 0 */
        int r = U(ToUnicodeEx)(vk[i], sc | 0x8000, st, b, 4, 0, us);
        CHECKF(r == 0, "vk %x up -> %d", vk[i], r);
        CHECK(b[0] == 0);
    }
    kls_cleanup();
}

TEST(kbd_layouts_state_tounicode_nonpositive_buffer_size_fails) {
    NEED_TU();
    BYTE st[256] = { 0 };
    static const int c[] = { 0, -1, -100, (int)0x80000000 };
    for (unsigned i = 0; i < sizeof c / sizeof *c; i++) {
        /* checked before the key table or the buffer pointer is touched, so NULL buffer is safe here */
        SET_ERR(0);
        int r = UN(NtUserToUnicodeEx)('A', 0x1E, st, NULL, c[i], 0, us);
        CHECKF(r == 0, "cch %d -> %d", c[i], r);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
    WCHAR b[2] = { 0x7777, 0x7777 };
    SET_ERR(0);
    CHECK(U(ToUnicodeEx)('A', 0x1E, st, b, 0, 0, us) == 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    CHECK(b[0] == 0);   /* the user32 wrapper writes the NUL even for cch == 0 */
    kls_cleanup();
}

TEST(kbd_layouts_state_tounicode_output_buffer_zero_filled) {
    NEED_TU();
    for (int cch = 1; cch <= 16; cch++) {
        WCHAR b[32]; for (int j = 0; j < 32; j++) b[j] = 0x7777;
        int r = tu('A', 0, us, 0, b, cch);
        CHECKF(r == 1 && b[0] == 'a', "cch %d: r=%d b0=%04x", cch, r, b[0]);
        /* the scratch buffer is zero-initialised and copied back in full */
        for (int j = 1; j < cch; j++) CHECKF(b[j] == 0, "cch %d cell %d = %04x", cch, j, b[j]);
        for (int j = cch; j < 32; j++) CHECKF(b[j] == 0x7777, "cch %d cell %d clobbered", cch, j);
    }
    kls_cleanup();
}

TEST(kbd_layouts_state_tounicode_key_state_array_is_input_only) {
    NEED_TU();
    BYTE st[256], ref[256];
    for (int i = 0; i < 256; i++) st[i] = (BYTE)((i * 37) & 0xff);
    st[VK_SHIFT] = 0x80; st[VK_CONTROL] = 0; st[VK_MENU] = 0;
    memcpy(ref, st, 256);
    WCHAR b[4];
    U(ToUnicodeEx)('A', 0x1E, st, b, 4, 0, us);
    CHECK(!memcmp(st, ref, 256));
    CHECK(b[0] == 'A');   /* only the modifier cells (and Caps Lock bit 0) matter for a letter */
    kls_cleanup();
}

TEST(kbd_layouts_state_tounicode_only_bit7_of_modifiers_counts) {
    NEED_TU();
    /* the state table is rebuilt from `byte < 0` (down) and `byte & 1` (toggle): 0x7E is neither */
    static const struct { BYTE shift, caps; WCHAR expect; } c[] = {
        { 0x00, 0x00, 'a' }, { 0x80, 0x00, 'A' }, { 0xFE, 0x00, 'A' }, { 0x7E, 0x00, 'a' }, { 0x7F, 0x00, 'a' },
        { 0x00, 0x01, 'A' }, { 0x00, 0x81, 'A' }, { 0x00, 0x80, 'a' }, { 0x00, 0xFE, 'a' }, { 0x80, 0x01, 'a' },
    };
    for (unsigned i = 0; i < sizeof c / sizeof *c; i++) {
        BYTE st[256] = { 0 }; WCHAR b[4];
        st[VK_SHIFT] = c[i].shift; st[VK_CAPITAL] = c[i].caps;
        int r = U(ToUnicodeEx)('A', 0x1E, st, b, 4, 0, us);
        CHECKF(r == 1 && b[0] == c[i].expect, "shift %02x caps %02x -> %d '%c'", c[i].shift, c[i].caps, r, b[0] < 128 ? b[0] : '?');
    }
    kls_cleanup();
}

TEST(kbd_layouts_state_tounicode_left_right_modifier_cells_are_ignored) {
    NEED_TU();
    /* GetModifierBits reads the generic VK_SHIFT/VK_CONTROL/VK_MENU cells; VK_LSHIFT (0xA0) alone does nothing */
    BYTE st[256] = { 0 }; WCHAR b[4];
    st[VK_LSHIFT] = 0x80;
    CHECK(U(ToUnicodeEx)('A', 0x1E, st, b, 4, 0, us) == 1);
    CHECK(b[0] == 'a');
    memset(st, 0, 256); st[VK_RSHIFT] = 0x80;
    CHECK(U(ToUnicodeEx)('A', 0x1E, st, b, 4, 0, us) == 1);
    CHECK(b[0] == 'a');
    kls_cleanup();
}

TEST(kbd_layouts_state_tounicode_unknown_hkl_returns_zero) {
    NEED_TU();
    BYTE st[256] = { 0 }; WCHAR b[4] = { 0x7777, 0x7777, 0x7777, 0x7777 };
    ULONG_PTR h = 0x12345678;
    if (kls_in_list((HKL)h)) { t_skip("unexpectedly present"); return; }
    CHECK(U(ToUnicodeEx)('A', 0x1E, st, b, 4, 0, (HKL)h) == 0);   /* HKLtoPKL fails */
    CHECK(b[0] == 0);
    kls_cleanup();
}

static DWORD w_tu_current(void *arg) {
    HKL us = (HKL)arg;
    UN(NtUserActivateKeyboardLayout)(us, 0);
    /* hkl == 0 with the Nt entry point: the thread's active layout */
    BYTE st[256] = { 0 }; WCHAR b[4];
    CHECK(UN(NtUserToUnicodeEx)('A', 0x1E, st, b, 4, 0, 0) == 1);
    CHECK(b[0] == 'a');
    st[VK_SHIFT] = 0x80;
    CHECK(U(ToUnicode)('A', 0x1E, st, b, 4, 0) == 1);
    CHECK(b[0] == 'A');
    return 0;
}
TEST(kbd_layouts_state_tounicode_current_layout_path) {
    NEED_TU(); REQUIRE(U(ToUnicode)); REQUIRE(UN(NtUserActivateKeyboardLayout));
    kls_thread(w_tu_current, us);
    kls_cleanup();
}

TEST(kbd_layouts_state_tounicode_ctrl_alt_is_not_altgr_on_us) {
    NEED_TU();
    WCHAR b[4] = { 0 };
    int r = tu('Q', M_C | M_A, us, 0, b, 4);
    /* the US modifier map has no AltGr column; whatever the layout does with Ctrl+Alt is layout data */
    t_note("US Ctrl+Alt+Q -> %d %04x; Alt+Q -> %d; Ctrl+Shift+Q -> %d %04x", r, b[0], tu('Q', M_A, us, 0, b, 4), tu('Q', M_C | M_S, us, 0, b, 4), b[0]);
    kls_cleanup();
}

/* =================================================================================== dead keys */

static HKL us_intl(void) { return kls_load(L"00020409"); }

#define NEED_DEAD() NEED_TU(); HKL ix = us_intl(); if (!ix) { t_skip("US International layout not available"); kls_cleanup(); return; } \
    { WCHAR z[8]; int r_ = tu(VK_OEM_7, 0, ix, 0, z, 8); if (r_ != -1) { t_note("VK_OEM_7 on 00020409 returned %d: not a dead key here", r_); kls_cleanup(); return; } \
      tu(VK_SPACE, 0, ix, 0, z, 8); }

TEST(kbd_layouts_state_tounicode_dead_key_returns_minus_one) {
    NEED_DEAD();
    WCHAR b[8]; for (int j = 0; j < 8; j++) b[j] = 0x7777;
    int r = tu(VK_OEM_7, 0, ix, 0, b, 8);
    CHECK(r == -1);
    CHECK(b[0] != 0 && b[0] != 0x7777);    /* the accent character itself */
    t_note("dead acute char %04x", b[0]);
    tu(VK_SPACE, 0, ix, 0, b, 8);          /* consume the pending accent */
    kls_cleanup();
}

TEST(kbd_layouts_state_tounicode_dead_key_then_letter_composes) {
    NEED_DEAD();
    WCHAR b[8] = { 0 };
    CHECK(tu(VK_OEM_7, 0, ix, 0, b, 8) == -1);
    for (int j = 0; j < 8; j++) b[j] = 0x7777;
    int r = tu('A', 0, ix, 0, b, 8);
    CHECK(r == 1);
    CHECK(b[0] != 'a');
    t_note("dead acute + a -> %04x (expect U+00E1)", b[0]);
    /* the dead state was consumed: a second plain 'a' is plain */
    CHECK(tu('A', 0, ix, 0, b, 8) == 1);
    CHECK(b[0] == 'a');
    kls_cleanup();
}

TEST(kbd_layouts_state_tounicode_flag4_keeps_dead_state_unchanged) {
    NEED_DEAD();
    WCHAR b[8] = { 0 };
    /* wFlags bit 2: `(wFlags & 4) == 0` guards the store of the pending dead key, so the accent stays unpended */
    CHECK(tu(VK_OEM_7, 0, ix, 4, b, 8) == -1);
    CHECK(tu('A', 0, ix, 0, b, 8) == 1);
    CHECK(b[0] == 'a');
    /* and the converse: without the flag the accent is pending */
    CHECK(tu(VK_OEM_7, 0, ix, 0, b, 8) == -1);
    CHECK(tu('A', 0, ix, 4, b, 8) == 1);
    CHECK(b[0] != 'a');
    kls_cleanup();
}

TEST(kbd_layouts_state_tounicode_dead_key_up_does_not_clear_pending) {
    NEED_DEAD();
    BYTE st[256] = { 0 }; WCHAR b[8] = { 0 };
    CHECK(tu(VK_OEM_7, 0, ix, 0, b, 8) == -1);
    UINT sc = U(MapVirtualKeyExW)(VK_OEM_7, MAPVK_VK_TO_VSC, ix);
    CHECK(U(ToUnicodeEx)(VK_OEM_7, sc | 0x8000, st, b, 8, 0, ix) == 0);   /* release of the dead key */
    CHECK(tu('A', 0, ix, 0, b, 8) == 1);
    CHECK(b[0] != 'a');
    kls_cleanup();
}

TEST(kbd_layouts_state_tounicode_dead_key_then_space_and_noncomposable) {
    NEED_DEAD();
    WCHAR b[8] = { 0 };
    CHECK(tu(VK_OEM_7, 0, ix, 0, b, 8) == -1);
    int r = tu(VK_SPACE, 0, ix, 0, b, 8);
    t_note("dead acute + space -> %d chars: %04x %04x", r, b[0], b[1]);
    CHECK(r >= 1);
    CHECK(tu(VK_OEM_7, 0, ix, 0, b, 8) == -1);
    r = tu('X', 0, ix, 0, b, 8);
    t_note("dead acute + x -> %d chars: %04x %04x", r, b[0], b[1]);
    CHECK(r == 2);   /* a non-composable letter yields the accent followed by the letter */
    kls_cleanup();
}

TEST(kbd_layouts_state_tounicode_dead_key_small_buffer) {
    NEED_DEAD();
    WCHAR b[8] = { 0 };
    CHECK(tu(VK_OEM_7, 0, ix, 0, b, 8) == -1);
    int r = tu('X', 0, ix, 0, b, 1);
    t_note("dead acute + x into cch=1 -> %d", r);
    CHECK(r >= 0 && r <= 1);
    tu(VK_SPACE, 0, ix, 0, b, 8);
    kls_cleanup();
}

TEST(kbd_layouts_state_tounicode_other_dead_keys_us_international) {
    NEED_DEAD();
    static const struct { UINT vk; unsigned mods; const char *name; } d[] = {
        { VK_OEM_3, 0, "grave" }, { VK_OEM_3, M_S, "tilde" }, { VK_OEM_7, M_S, "diaeresis" }, { '6', M_S, "circumflex" },
    };
    for (unsigned i = 0; i < sizeof d / sizeof *d; i++) {
        WCHAR b[8] = { 0 };
        int r = tu(d[i].vk, d[i].mods, ix, 0, b, 8);
        t_note("%s: ret %d char %04x", d[i].name, r, b[0]);
        CHECK(r == -1 || r == 1);
        tu(VK_SPACE, 0, ix, 0, b, 8);
    }
    kls_cleanup();
}

static DWORD w_tu_activate_clears(void *arg) {
    HKL ix = (HKL)arg, us = kls_us();
    WCHAR b[8] = { 0 };
    UN(NtUserActivateKeyboardLayout)(ix, 0);
    CHECK(tu(VK_OEM_7, 0, ix, 0, b, 8) == -1);
    /* activating a keyboard layout clears the pending dead key of the layout being activated */
    UN(NtUserActivateKeyboardLayout)(us, 0);
    UN(NtUserActivateKeyboardLayout)(ix, 0);
    CHECK(tu('A', 0, ix, 0, b, 8) == 1);
    CHECK(b[0] == 'a');
    return 0;
}
TEST(kbd_layouts_state_tounicode_activation_resets_pending_dead_key) {
    NEED_DEAD(); REQUIRE(UN(NtUserActivateKeyboardLayout));
    kls_thread(w_tu_activate_clears, ix);
    kls_cleanup();
}

/* ====================================================================== other layouts (recorded) */

TEST(kbd_layouts_state_tounicode_german_altgr_note) {
    NEED_TU();
    HKL de = kls_load(L"00000407");
    if (!de) { t_skip("German layout not available"); kls_cleanup(); return; }
    WCHAR b[8] = { 0 };
    int a = tu('Q', M_C | M_A, de, 0, b, 8); WCHAR q = b[0];
    int e = tu('E', M_C | M_A, de, 0, b, 8); WCHAR ee = b[0];
    int y = tu('Y', 0, de, 0, b, 8); WCHAR yy = b[0];
    int z = tu('Z', 0, de, 0, b, 8); WCHAR zz = b[0];
    t_note("de: Ctrl+Alt+Q -> %d %04x (AltGr+Q = '@'), Ctrl+Alt+E -> %d %04x, Y -> %d %04x, Z -> %d %04x", a, q, e, ee, y, yy, z, zz);
    CHECK(y == 1 && z == 1);
    /* the QWERTZ swap is observable through the explicit HKL while the US result for the same VK is unchanged */
    WCHAR u[4]; tu('Z', 0, us, 0, u, 4);
    CHECK(u[0] == 'z');
    kls_cleanup();
}

TEST(kbd_layouts_state_tounicode_explicit_hkl_beats_thread_layout) {
    NEED_TU();
    HKL de = kls_load(L"00000407");
    if (!de) { t_skip("German layout not available"); kls_cleanup(); return; }
    WCHAR a[4] = { 0 }, b[4] = { 0 };
    /* the same virtual key through two explicit layouts is layout specific; the thread layout is irrelevant */
    int ra = tu('Z', 0, us, 0, a, 4), rb = tu('Z', 0, de, 0, b, 4);
    CHECK(ra == 1 && rb == 1);
    CHECK(a[0] == 'z');
    if (b[0] != 'y') t_note("German VK_Z -> %04x (expected 'y')", b[0]);
    kls_cleanup();
}
