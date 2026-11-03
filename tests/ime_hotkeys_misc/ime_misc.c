/* IME hotkey list (NtUserGetImeHotKey / NtUserSetImeHotKey), IMEINFOEX lookup, per-window app IME
 * level, thread/process IME disabling and IME status notification. */
#include "imhm.h"

#define SENT 0x7e57u
#define PRIV_ID 0x7ff00001u   /* far above the 0x10..0x8f ids GetHotKeyLangID maps to a language */

static BOOL gih(DWORD id, UINT *m, UINT *v, HKL *k) { return UN(NtUserGetImeHotKey)(id, m, v, k); }
static BOOL sih(DWORD id, UINT m, UINT v, HKL k, DWORD act) { return UN(NtUserSetImeHotKey)(id, m, v, k, act); }
#define NEED_IHK() do { if (!UN(NtUserGetImeHotKey) || !UN(NtUserSetImeHotKey) || !U(GetDesktopWindow)) { t_skip("missing stub"); return; } } while (0)

/* ---- IME hotkey table -------------------------------------------------------------------- */
TEST(ime_hotkeys_misc_ihk_get_unknown_id) {
    NEED_IHK();
    UINT m = 0xabc, v = 0xdef; HKL k = (HKL)7;
    SET_ERR(SENT);
    BOOL r = gih(0x7fffff01, &m, &v, &k);
    EXPECT_FAIL(r == 0, ERROR_HOTKEY_NOT_REGISTERED);   /* FindImeHotKeyByID miss -> 0x58b */
    EXPECT_EQ(m, 0xabc);       /* outputs untouched on failure */
    EXPECT_EQ(v, 0xdef);
    EXPECT_EQ(k, (HKL)7);
}
TEST(ime_hotkeys_misc_ihk_get_unknown_id_without_hkl_pointer) {
    NEED_IHK();
    UINT m = 0, v = 0;
    SET_ERR(SENT);
    EXPECT_FAIL(gih(0x7fffff02, &m, &v, NULL) == 0, ERROR_HOTKEY_NOT_REGISTERED);
}
TEST(ime_hotkeys_misc_ihk_set_get_roundtrip) {
    NEED_IHK();
    CHECK(sih(PRIV_ID, 0x3, 0x88, NULL, 2));
    UINT m = 0, v = 0; HKL k = (HKL)5;
    SET_ERR(SENT);
    CHECK(gih(PRIV_ID, &m, &v, &k));
    EXPECT_EQ(m, 3);
    EXPECT_EQ(v, 0x88);
    EXPECT_EQ(k, NULL);
    sih(PRIV_ID, 0, 0, NULL, 1);
}
TEST(ime_hotkeys_misc_ihk_get_without_hkl_pointer_ok_when_hkl_zero) {
    NEED_IHK();
    CHECK(sih(PRIV_ID + 1, 0x2, 0x89, NULL, 2));
    UINT m = 0, v = 0;
    CHECK(gih(PRIV_ID + 1, &m, &v, NULL));   /* NULL hkl pointer is accepted when the stored hkl is 0 */
    EXPECT_EQ(m, 2);
    EXPECT_EQ(v, 0x89);
    sih(PRIV_ID + 1, 0, 0, NULL, 1);
}
TEST(ime_hotkeys_misc_ihk_get_without_hkl_pointer_rejects_nonzero_hkl) {
    NEED_IHK();
    HKL fake = (HKL)(ULONG_PTR)0xE0010411;
    CHECK(sih(PRIV_ID + 2, 0x2, 0x8a, fake, 2));
    UINT m = 0, v = 0; HKL k = NULL;
    SET_ERR(SENT);
    BOOL r = gih(PRIV_ID + 2, &m, &v, NULL);
    EXPECT_FAIL(r == 0, ERROR_INVALID_PARAMETER);   /* hkl would be lost: 0x57 */
    CHECK(gih(PRIV_ID + 2, &m, &v, &k));
    EXPECT_EQ(k, fake);
    sih(PRIV_ID + 2, 0, 0, NULL, 1);
}
TEST(ime_hotkeys_misc_ihk_modifier_word_stored_raw) {
    NEED_IHK();
    static const UINT mods[] = { 0, 1, 0xf, 0x400, 0x4003, 0x8002, 0xc00f, 0xffffffff };
    for (unsigned i = 0; i < sizeof mods / sizeof mods[0]; i++) {
        DWORD id = PRIV_ID + 0x10 + i;
        BOOL s = sih(id, mods[i], 0x90 + i, NULL, 2);
        CHECKF(s, "mods 0x%x err %lu", mods[i], GetLastError());
        UINT m = 0, v = 0;
        if (s && gih(id, &m, &v, NULL)) {
            CHECKF(m == mods[i], "mods 0x%x read back 0x%x", mods[i], m);
            CHECKF(v == 0x90 + i, "vk");
        }
        sih(id, 0, 0, NULL, 1);
    }
}
TEST(ime_hotkeys_misc_ihk_update_existing_id) {
    NEED_IHK();
    CHECK(sih(PRIV_ID + 3, 0x1, 0x8b, NULL, 2));
    CHECK(sih(PRIV_ID + 3, 0x2, 0x8c, NULL, 2));   /* FindImeHotKeyByID hit: fields rewritten, no new entry */
    UINT m = 0, v = 0;
    CHECK(gih(PRIV_ID + 3, &m, &v, NULL));
    EXPECT_EQ(m, 2);
    EXPECT_EQ(v, 0x8c);
    CHECK(sih(PRIV_ID + 3, 0, 0, NULL, 1));
    SET_ERR(SENT);
    EXPECT_FAIL(sih(PRIV_ID + 3, 0, 0, NULL, 1) == 0, ERROR_INVALID_PARAMETER);   /* a single entry only */
}
TEST(ime_hotkeys_misc_ihk_set_same_key_same_id_ok) {
    NEED_IHK();
    CHECK(sih(PRIV_ID + 4, 0x3, 0x8d, NULL, 2));
    CHECK(sih(PRIV_ID + 4, 0x3, 0x8d, NULL, 2));   /* the clash found is the same id: allowed */
    sih(PRIV_ID + 4, 0, 0, NULL, 1);
}
TEST(ime_hotkeys_misc_ihk_set_conflicting_key_other_id) {
    NEED_IHK();
    CHECK(sih(PRIV_ID + 5, 0x3, 0x8e, NULL, 2));
    SET_ERR(SENT);
    BOOL r = sih(PRIV_ID + 6, 0x3, 0x8e, NULL, 2);
    EXPECT_FAIL(r == 0, ERROR_HOTKEY_ALREADY_REGISTERED);   /* FindImeHotKeyByKeyWithLang hit on another id -> 0x581 */
    SET_ERR(SENT);
    UINT m, v;
    EXPECT_FAIL(gih(PRIV_ID + 6, &m, &v, NULL) == 0, ERROR_HOTKEY_NOT_REGISTERED);
    sih(PRIV_ID + 5, 0, 0, NULL, 1);
}
TEST(ime_hotkeys_misc_ihk_different_high_modifier_bits_do_not_conflict) {
    NEED_IHK();
    /* FindImeHotKeyByKeyWithLang matches only when (stored & 0xc000) == (new & 0xc000) or the two share a
     * bit of 0xc000; 0x0001 and 0x4001 have the same low nibble but disjoint 0xc000 parts */
    CHECK(sih(PRIV_ID + 7, 0x1, 0x8f, NULL, 2));
    SET_ERR(SENT);
    CHECK(sih(PRIV_ID + 8, 0x1 | 0x4000, 0x8f, NULL, 2));
    sih(PRIV_ID + 7, 0, 0, NULL, 1);
    sih(PRIV_ID + 8, 0, 0, NULL, 1);
}
TEST(ime_hotkeys_misc_ihk_equal_high_modifier_bits_conflict) {
    NEED_IHK();
    CHECK(sih(PRIV_ID + 70, 0x4001, 0x8f, NULL, 2));
    SET_ERR(SENT);
    EXPECT_FAIL(sih(PRIV_ID + 71, 0x4001, 0x8f, NULL, 2) == 0, ERROR_HOTKEY_ALREADY_REGISTERED);
    sih(PRIV_ID + 70, 0, 0, NULL, 1);
}
TEST(ime_hotkeys_misc_ihk_different_vk_no_conflict) {
    NEED_IHK();
    CHECK(sih(PRIV_ID + 9, 0x3, 0x98, NULL, 2));
    CHECK(sih(PRIV_ID + 10, 0x3, 0x99, NULL, 2));
    sih(PRIV_ID + 9, 0, 0, NULL, 1);
    sih(PRIV_ID + 10, 0, 0, NULL, 1);
}
TEST(ime_hotkeys_misc_ihk_delete_unknown_id) {
    NEED_IHK();
    SET_ERR(SENT);
    BOOL r = sih(0x7fffff03, 0, 0, NULL, 1);
    EXPECT_FAIL(r == 0, ERROR_INVALID_PARAMETER);   /* action 1: FindImeHotKeyByID miss -> 0x57 */
}
TEST(ime_hotkeys_misc_ihk_delete_makes_get_fail) {
    NEED_IHK();
    CHECK(sih(PRIV_ID + 11, 0x3, 0x9a, NULL, 2));
    CHECK(sih(PRIV_ID + 11, 0, 0, NULL, 1));
    UINT m, v;
    SET_ERR(SENT);
    EXPECT_FAIL(gih(PRIV_ID + 11, &m, &v, NULL) == 0, ERROR_HOTKEY_NOT_REGISTERED);
}
TEST(ime_hotkeys_misc_ihk_vk_packet_refused_silently) {
    NEED_IHK();
    SET_ERR(SENT);
    BOOL r = sih(PRIV_ID + 12, 0x3, 0xe7, NULL, 2);
    EXPECT_EQ(r, 0);
    EXPECT_ERR(SENT);
    UINT m, v;
    CHECK(!gih(PRIV_ID + 12, &m, &v, NULL));
}
TEST(ime_hotkeys_misc_ihk_unknown_action_returns_false) {
    NEED_IHK();
    static const DWORD acts[] = { 0, 4, 5, 0x10, 0xffffffff };
    for (unsigned i = 0; i < sizeof acts / sizeof acts[0]; i++) {
        SET_ERR(SENT);
        BOOL r = sih(PRIV_ID + 13, 0x3, 0x9b, NULL, acts[i]);
        CHECKF(r == 0, "action 0x%lx", acts[i]);
        CHECKF(GetLastError() == SENT, "action 0x%lx err %lu", acts[i], GetLastError());
    }
    UINT m, v;
    CHECK(!gih(PRIV_ID + 13, &m, &v, NULL));   /* nothing was stored */
}
TEST(ime_hotkeys_misc_ihk_set_returns_one) {
    NEED_IHK();
    BOOL r = sih(PRIV_ID + 14, 0x3, 0x9c, NULL, 2);
    EXPECT_EQ(r, 1);
    sih(PRIV_ID + 14, 0, 0, NULL, 1);
}
TEST(ime_hotkeys_misc_ihk_ids_independent) {
    NEED_IHK();
    for (int i = 0; i < 12; i++) CHECKF(sih(PRIV_ID + 0x40 + i, 0x3, 0xa0 + i, NULL, 2), "id %d", i);
    for (int i = 0; i < 12; i++) {
        UINT m = 0, v = 0;
        CHECKF(gih(PRIV_ID + 0x40 + i, &m, &v, NULL) && v == (UINT)(0xa0 + i), "id %d", i);
    }
    for (int i = 0; i < 12; i += 2) sih(PRIV_ID + 0x40 + i, 0, 0, NULL, 1);
    for (int i = 0; i < 12; i++) {
        UINT m = 0, v = 0;
        BOOL r = gih(PRIV_ID + 0x40 + i, &m, &v, NULL);
        CHECKF(r == (i & 1), "id %d presence %d", i, r);
    }
    for (int i = 1; i < 12; i += 2) sih(PRIV_ID + 0x40 + i, 0, 0, NULL, 1);
}
static int g_shared_ok; static UINT g_shared_v;
static DWORD WINAPI shared_probe(void *p) { UINT m = 0; g_shared_ok = gih(PRIV_ID + 20, &m, &g_shared_v, NULL); return 0; }
TEST(ime_hotkeys_misc_ihk_shared_with_other_threads) {
    NEED_IHK();
    CHECK(sih(PRIV_ID + 20, 0x3, 0x9d, NULL, 2));
    g_shared_ok = 0; g_shared_v = 0;
    im_on_thread(shared_probe, NULL);
    CHECK(g_shared_ok);            /* gpImeHotKeyListHeader is session-global, not per thread */
    EXPECT_EQ(g_shared_v, 0x9d);
    sih(PRIV_ID + 20, 0, 0, NULL, 1);
}
TEST(ime_hotkeys_misc_ihk_system_ids_report_consistently) {
    NEED_IHK();
    /* IME_CHOTKEY_*, IME_JHOTKEY_*, IME_KHOTKEY_*, IME_THOTKEY_* live at 0x10.. - presence depends on the
     * installed input methods, so only the internal consistency is asserted */
    int found = 0;
    for (DWORD id = 0x10; id < 0x90; id++) {
        UINT m = 0xffff, v = 0xffff; HKL k = (HKL)1;
        SET_ERR(SENT);
        if (gih(id, &m, &v, &k)) {
            found++;
            CHECKF(v != 0xffff || m != 0xffff, "id 0x%lx returned nothing", id);
            if (found <= 6) t_note("IME hotkey 0x%lx: mods 0x%x vk 0x%x hkl %p", id, m, v, k);
        } else {
            CHECKF(GetLastError() == ERROR_HOTKEY_NOT_REGISTERED, "id 0x%lx err %lu", id, GetLastError());
        }
    }
    t_note("%d of the system IME hotkey ids are defined", found);
}
TEST(ime_hotkeys_misc_ihk_get_does_not_require_ime_enabled) {
    NEED_IHK();
    /* unlike the input context calls there is no guard here: the unknown-id error is the same */
    UINT m, v;
    SET_ERR(SENT);
    CHECK(!gih(0x7fffff04, &m, &v, NULL));
    EXPECT_ERR(ERROR_HOTKEY_NOT_REGISTERED);
}

/* ---- IMEINFOEX --------------------------------------------------------------------------- */
#define IMEINFOEX_SIZE 0x160
TEST(ime_hotkeys_misc_iie_unknown_hkl_type0) {
    NEED_IME();
    if (!UN(NtUserGetImeInfoEx)) { t_skip("missing stub"); return; }
    unsigned char buf[IMEINFOEX_SIZE + 16];
    memset(buf, 0, sizeof buf);
    /* GetImeInfoEx walks the window station's list of loaded layouts: on Windows 10 that list holds every keyboard layout
     * (the US layout 0x04090409 answers TRUE), so the unknown handle has to be one that is not loaded at all. */
    *(HKL *)buf = (HKL)(ULONG_PTR)0x12341234;
    SET_ERR(SENT);
    BOOL r = UN(NtUserGetImeInfoEx)(buf, 0);
    EXPECT_EQ(r, 0);         /* no hit */
    for (unsigned i = IMEINFOEX_SIZE; i < sizeof buf; i++) if (buf[i]) { CHECKF(0, "wrote past the 0x160 byte struct at %u", i); break; }
}
TEST(ime_hotkeys_misc_iie_null_hkl_type0) {
    NEED_IME();
    if (!UN(NtUserGetImeInfoEx)) { t_skip("missing stub"); return; }
    unsigned char buf[IMEINFOEX_SIZE];
    memset(buf, 0, sizeof buf);
    EXPECT_EQ(UN(NtUserGetImeInfoEx)(buf, 0), 0);
}
TEST(ime_hotkeys_misc_iie_invalid_type) {
    NEED_IME();
    if (!UN(NtUserGetImeInfoEx)) { t_skip("missing stub"); return; }
    static const DWORD types[] = { 1, 2, 4, 5, 0x100, 0xffffffff };
    for (unsigned i = 0; i < sizeof types / sizeof types[0]; i++) {
        unsigned char buf[IMEINFOEX_SIZE];
        memset(buf, 0, sizeof buf);
        *(HKL *)buf = (HKL)(ULONG_PTR)0x04090409;
        CHECKF(UN(NtUserGetImeInfoEx)(buf, types[i]) == 0, "type 0x%lx", types[i]);  /* only types 0 and 3 are handled */
    }
}
TEST(ime_hotkeys_misc_iie_type3_unknown_file_name) {
    NEED_IME();
    if (!UN(NtUserGetImeInfoEx)) { t_skip("missing stub"); return; }
    unsigned char buf[IMEINFOEX_SIZE];
    memset(buf, 0, sizeof buf);
    wcscpy((WCHAR *)(buf + 0xbc), L"win32kt_no_such_ime.ime");   /* type 3 matches wszImeFile (+0xbc) case-insensitively */
    EXPECT_EQ(UN(NtUserGetImeInfoEx)(buf, 3), 0);
}
TEST(ime_hotkeys_misc_iie_input_buffer_unchanged_on_failure) {
    NEED_IME();
    if (!UN(NtUserGetImeInfoEx)) { t_skip("missing stub"); return; }
    unsigned char buf[IMEINFOEX_SIZE], ref[IMEINFOEX_SIZE];
    for (unsigned i = 0; i < sizeof buf; i++) buf[i] = (unsigned char)(i * 7 + 1);
    *(HKL *)buf = (HKL)(ULONG_PTR)0x12341234;   /* not a loaded layout (the US layout is: a hit rewrites the struct) */
    memcpy(ref, buf, sizeof buf);
    UN(NtUserGetImeInfoEx)(buf, 0);
    CHECK(!memcmp(buf, ref, sizeof buf));   /* the kernel copies the struct in and back verbatim when nothing matched */
}
TEST(ime_hotkeys_misc_iie_imm_isime_agrees) {
    NEED_IME();
    if (!UN(NtUserGetImeInfoEx) || !IM(ImmIsIME)) { t_skip("missing export"); return; }
    HKL k = U(GetKeyboardLayout)(0);
    unsigned char buf[IMEINFOEX_SIZE];
    memset(buf, 0, sizeof buf);
    *(HKL *)buf = k;
    BOOL kern = UN(NtUserGetImeInfoEx)(buf, 0);
    if ((((ULONG_PTR)k >> 16) & 0xf000) != 0xe000) EXPECT_EQ(IM(ImmIsIME)(k), kern);   /* ImmIsIME is exactly this call for non-0xE000 HKLs */
    else t_note("current layout %p is an IME layout", k);
}

/* ---- app IME level ----------------------------------------------------------------------- */
#define NEED_LEVEL() do { if (!UN(NtUserGetAppImeLevel) || !UN(NtUserSetAppImeLevel)) { t_skip("missing stub"); return; } NEED_IME(); } while (0)
TEST(ime_hotkeys_misc_lvl_default_zero) {
    NEED_LEVEL(); NEED_GUI(w);
    EXPECT_EQ(UN(NtUserGetAppImeLevel)(w), 0);
    im_kill(w);
}
TEST(ime_hotkeys_misc_lvl_set_get_roundtrip) {
    NEED_LEVEL(); NEED_GUI(w);
    static const int vals[] = { 1, 2, 3, 100, 0x7fffffff, -1, 0 };
    for (unsigned i = 0; i < sizeof vals / sizeof vals[0]; i++) {
        BOOL s = UN(NtUserSetAppImeLevel)(w, vals[i]);
        CHECKF(s == 1, "set %d", vals[i]);
        ULONG_PTR g = UN(NtUserGetAppImeLevel)(w);
        CHECKF((int)g == vals[i], "level %d read back 0x%llx", vals[i], (unsigned long long)g);   /* _GetProp(atomImeLevel) */
    }
    im_kill(w);
}
TEST(ime_hotkeys_misc_lvl_stored_as_window_property) {
    NEED_LEVEL(); NEED_GUI(w);
    CHECK(UN(NtUserSetAppImeLevel)(w, 5));
    /* InternalSetProp(wnd, atomImeLevel, level, 5) */
    t_note("GetPropW(L\"ImeLevel\") -> %p", U(GetPropW)(w, L"ImeLevel"));
    EXPECT_EQ(UN(NtUserGetAppImeLevel)(w), 5);
    im_kill(w);
}
TEST(ime_hotkeys_misc_lvl_per_window) {
    NEED_LEVEL(); NEED_GUI(w);
    HWND w2 = im_mkpopup();
    if (!w2) { im_kill(w); t_skip("second window"); return; }
    UN(NtUserSetAppImeLevel)(w, 3);
    EXPECT_EQ(UN(NtUserGetAppImeLevel)(w2), 0);
    UN(NtUserSetAppImeLevel)(w2, 4);
    EXPECT_EQ(UN(NtUserGetAppImeLevel)(w), 3);
    im_kill(w); im_kill(w2);
}
TEST(ime_hotkeys_misc_lvl_bogus_window) {
    NEED_LEVEL();
    SET_ERR(SENT);
    EXPECT_EQ(UN(NtUserSetAppImeLevel)(im_bogus_hwnd(), 1), 0);
    EXPECT_EQ(UN(NtUserGetAppImeLevel)(im_bogus_hwnd()), 0);
    EXPECT_EQ(UN(NtUserSetAppImeLevel)(NULL, 1), 0);
}
TEST(ime_hotkeys_misc_lvl_other_process_window) {
    NEED_LEVEL();
    HWND s = U(GetShellWindow)();
    if (!s) { t_skip("no shell window"); return; }
    DWORD pid = 0; U(GetWindowThreadProcessId)(s, &pid);
    if (pid == GetCurrentProcessId()) { t_skip("shell is ours"); return; }
    EXPECT_EQ(UN(NtUserSetAppImeLevel)(s, 1), 0);     /* window's ppi != current process */
    EXPECT_EQ(UN(NtUserGetAppImeLevel)(s), 0);        /* GetAppImeLevel applies the same process test */
}
TEST(ime_hotkeys_misc_lvl_other_thread_window_same_process) {
    NEED_LEVEL();
    struct imt x;
    if (!imt_start(&x, 1)) { imt_stop(&x); t_skip("helper"); return; }
    EXPECT_EQ(UN(NtUserSetAppImeLevel)(x.hwnd, 7), 1);   /* process, not thread, comparison */
    EXPECT_EQ(UN(NtUserGetAppImeLevel)(x.hwnd), 7);
    imt_stop(&x);
}
TEST(ime_hotkeys_misc_lvl_destroyed_window) {
    NEED_LEVEL(); NEED_GUI(w);
    UN(NtUserSetAppImeLevel)(w, 2);
    im_kill(w);
    EXPECT_EQ(UN(NtUserGetAppImeLevel)(w), 0);
    EXPECT_EQ(UN(NtUserSetAppImeLevel)(w, 2), 0);
}

/* ---- DisableThreadIme -------------------------------------------------------------------- */
struct dis { int step; HWND ime_before, ime_after; int alive_after; HIMC ctx_after; HIMC ctx_before; BOOL r; DWORD tid; };
static DWORD WINAPI dis_self(void *p) {
    struct dis *d = (struct dis *)p;
    HWND w = im_mkpopup();
    if (!w) return 1;
    d->ime_before = (HWND)UN(NtUserGetThreadState)(3);
    d->ctx_before = IM(ImmCreateContext)();
    if (d->ctx_before) IM(ImmDestroyContext)(d->ctx_before);
    d->r = UN(NtUserDisableThreadIme)(0);
    d->ime_after = (HWND)UN(NtUserGetThreadState)(3);
    d->alive_after = d->ime_before ? U(IsWindow)(d->ime_before) : 0;
    d->ctx_after = IM(ImmCreateContext)();
    if (d->ctx_after) IM(ImmDestroyContext)(d->ctx_after);
    U(DestroyWindow)(w);
    return 0;
}
#define NEED_DIS() do { if (!UN(NtUserDisableThreadIme) || !IM(ImmCreateContext) || !UN(NtUserGetThreadState)) { t_skip("missing stub"); return; } NEED_IME(); } while (0)
TEST(ime_hotkeys_misc_dis_current_thread_returns_true) {
    NEED_DIS();
    struct dis d; memset(&d, 0, sizeof d);
    im_on_thread(dis_self, &d);
    EXPECT_EQ(d.r, 1);
}
TEST(ime_hotkeys_misc_dis_destroys_default_ime_window) {
    NEED_DIS();
    struct dis d; memset(&d, 0, sizeof d);
    im_on_thread(dis_self, &d);
    if (!d.ime_before) { t_note("no default IME window to destroy"); return; }
    EXPECT_EQ(d.alive_after, 0);       /* NtUserDisableThreadIme: the default IME window is destroyed */
    EXPECT_EQ(d.ime_after, NULL);
}
TEST(ime_hotkeys_misc_dis_blocks_context_creation) {
    NEED_DIS();
    struct dis d; memset(&d, 0, sizeof d);
    im_on_thread(dis_self, &d);
    CHECK(d.ctx_before != NULL);       /* before: creation works */
    EXPECT_EQ(d.ctx_after, NULL);      /* CreateInputContext: `pti->flags & 0x2000000` (TIF_DISABLEIME) -> NULL */
}
static DWORD WINAPI dis_state(void *p) {
    struct dis *d = (struct dis *)p;
    HWND w = im_mkpopup();
    if (!w) return 1;
    d->r = UN(NtUserDisableThreadIme)(0);
    HWND w2 = im_mkpopup();      /* windows made afterwards get no default IME window either */
    d->ime_after = (HWND)UN(NtUserGetThreadState)(3);
    d->ctx_after = IM(ImmGetContext)(w2);
    if (w2) U(DestroyWindow)(w2);
    U(DestroyWindow)(w);
    return 0;
}
TEST(ime_hotkeys_misc_dis_later_windows_have_no_ime_window) {
    NEED_DIS();
    struct dis d; memset(&d, 0, sizeof d);
    im_on_thread(dis_state, &d);
    EXPECT_EQ(d.r, 1);
    t_note("default IME window of a window created after DisableThreadIme(0): %p", d.ime_after);
}
static DWORD WINAPI dis_twice(void *p) { BOOL a = UN(NtUserDisableThreadIme)(0), b = UN(NtUserDisableThreadIme)(0); return (a << 1) | b; }
TEST(ime_hotkeys_misc_dis_twice_is_idempotent) {
    NEED_DIS();
    EXPECT_EQ(im_on_thread(dis_twice, NULL), 3);
}
TEST(ime_hotkeys_misc_dis_other_thread_in_process) {
    NEED_DIS();
    struct imt x;
    if (!imt_start(&x, 1)) { imt_stop(&x); t_skip("helper"); return; }
    HWND before = (HWND)UN(NtUserQueryWindow)(x.hwnd, 8);
    SET_ERR(SENT);
    BOOL r = UN(NtUserDisableThreadIme)(x.tid);
    EXPECT_EQ(r, 1);    /* PtiFromThreadId(tid) in the same process: flag set, window destroyed by the owner */
    Sleep(100);
    if (before) CHECK(!U(IsWindow)(before) || U(GetWindowThreadProcessId)(before, NULL) == x.tid);
    imt_stop(&x);
}
TEST(ime_hotkeys_misc_dis_bogus_thread_id) {
    NEED_DIS();
    SET_ERR(SENT);
    EXPECT_EQ(UN(NtUserDisableThreadIme)(0xFFFFFF00), 0);
    EXPECT_ERR(SENT);     /* failure path is a silent `goto leave` */
}
TEST(ime_hotkeys_misc_dis_thread_of_other_process) {
    NEED_DIS();
    HWND s = U(GetShellWindow)();
    if (!s) { t_skip("no shell window"); return; }
    DWORD pid = 0, tid = U(GetWindowThreadProcessId)(s, &pid);
    if (pid == GetCurrentProcessId()) { t_skip("shell is ours"); return; }
    EXPECT_EQ(UN(NtUserDisableThreadIme)(tid), 0);   /* ppi mismatch: explorer is untouched */
}
static DWORD WINAPI dis_imm(void *p) { return IM(ImmDisableIME)(0); }
TEST(ime_hotkeys_misc_dis_imm_disable_ime_current) {
    NEED_DIS();
    if (!IM(ImmDisableIME)) { t_skip("missing export"); return; }
    EXPECT_EQ(im_on_thread(dis_imm, NULL), 1);
}

/* Process wide disable (-1): irreversible, child process only. */
#define CC(c) do { if (!(c)) { fails++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
CHILD(imhm_disable_process_ime) {
    int fails = 0;
    if (!IM(ImmDisableIME) || !IM(ImmCreateContext) || !UN(NtUserDisableThreadIme)) { printf("NOTE child: missing export\n"); return 0; }
    if (im_ime_on() == 0) { printf("NOTE child: IME not enabled, skipping\n"); return 0; }
    HWND w = im_mkpopup();
    CC(w != NULL);
    HIMC c1 = IM(ImmCreateContext)();
    CC(c1 != NULL);
    if (c1) IM(ImmDestroyContext)(c1);
    BOOL r = IM(ImmDisableIME)((DWORD)-1);
    CC(r == 1);
    HIMC c2 = IM(ImmCreateContext)();
    CC(c2 == NULL);                 /* flag set on every pti of the process */
    HWND ime = (HWND)UN(NtUserGetThreadState)(3);
    CC(ime == NULL);                /* the calling thread's default IME window was destroyed */
    HWND w2 = im_mkpopup();         /* later windows get no IME window */
    CC(w2 != NULL);
    printf("NOTE default IME window after a later window was created: %p\n", (void *)UN(NtUserGetThreadState)(3));
    return fails;
}
TEST(ime_hotkeys_misc_dis_process_wide_child) {
    if (t_is_wine()) { t_skip("child processes need a real desktop connection"); return; }
    DWORD r = t_run_child("imhm_disable_process_ime", "", 30000);
    CHECK(r == 0);
}
static HIMC g_probe_got = (HIMC)1;
static DWORD WINAPI probe_thread(void *p) { g_probe_got = IM(ImmCreateContext)(); return 0; }
CHILD(imhm_disable_process_ime_other_thread) {
    int fails = 0;
    if (!IM(ImmCreateContext) || !UN(NtUserDisableThreadIme)) return 0;
    if (im_ime_on() == 0) return 0;
    struct imt x;
    if (!imt_start(&x, 1)) { imt_stop(&x); printf("NOTE child: helper failed\n"); return 0; }
    BOOL r = UN(NtUserDisableThreadIme)((DWORD)-1);
    CC(r == 1);
    /* the already-running helper thread is flagged as well: it loses its default IME window */
    HWND hw = (HWND)UN(NtUserQueryWindow)(x.hwnd, 8);
    for (int i = 0; hw && i < 60; i++) { Sleep(50); hw = (HWND)UN(NtUserQueryWindow)(x.hwnd, 8); }  /* destroyed by the owning thread's own message loop */
    printf("NOTE helper thread default IME window after DisableThreadIme(-1): %p\n", (void *)hw);
    HANDLE t = CreateThread(NULL, 0, probe_thread, NULL, 0, NULL);   /* a thread created after the call */
    WaitForSingleObject(t, 5000); CloseHandle(t);
    printf("NOTE thread created after DisableThreadIme(-1) got context %p\n", (void *)g_probe_got);
    imt_stop(&x);
    return fails;
}
TEST(ime_hotkeys_misc_dis_process_wide_other_thread_child) {
    if (t_is_wine()) { t_skip("child processes need a real desktop connection"); return; }
    CHECK(t_run_child("imhm_disable_process_ime_other_thread", "", 30000) == 0);
}

/* ---- NotifyIMEStatus ---------------------------------------------------------------------- */
TEST(ime_hotkeys_misc_nis_bogus_window) {
    NEED_IME();
    if (!UN(NtUserNotifyIMEStatus)) { t_skip("missing stub"); return; }
    SET_ERR(SENT);
    EXPECT_FAIL(UN(NtUserNotifyIMEStatus)(im_bogus_hwnd(), 0, 0) == 0, ERROR_INVALID_WINDOW_HANDLE);
}
TEST(ime_hotkeys_misc_nis_null_window) {
    NEED_IME();
    if (!UN(NtUserNotifyIMEStatus)) { t_skip("missing stub"); return; }
    EXPECT_EQ(UN(NtUserNotifyIMEStatus)(NULL, 0, 0), 0);
}
TEST(ime_hotkeys_misc_nis_destroyed_window) {
    NEED_IME(); NEED_GUI(w);
    if (!UN(NtUserNotifyIMEStatus)) { im_kill(w); t_skip("missing stub"); return; }
    im_kill(w);
    EXPECT_EQ(UN(NtUserNotifyIMEStatus)(w, 0, 0), 0);
}
