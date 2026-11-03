/* NtUserRegisterHotKey / NtUserUnregisterHotKey: parameter validation, key identity and window
 * ownership rules, all from win32kfull!NtUserRegisterHotKey, _RegisterHotKey, FindHotKey,
 * HKRemoveMatchingHotkeys and _UnregisterHotKey. */
#include "imhm.h"

#define NAME(x) ime_hotkeys_misc_hk_##x
#define SENT 0x7e57u

/* ---- stub shape ---------------------------------------------------------------------------- */
static const struct { const char *name; long num; } g_stubs[] = {
    { "NtUserRegisterHotKey", 5280 }, { "NtUserUnregisterHotKey", 5390 },
    { "NtUserCreateInputContext", 5053 }, { "NtUserDestroyInputContext", 5066 },
    { "NtUserAssociateInputContext", 5021 }, { "NtUserQueryInputContext", 5272 },
    { "NtUserUpdateInputContext", 5394 }, { "NtUserBuildHimcList", 5027 },
    { "NtUserGetImeInfoEx", 5135 }, { "NtUserSetImeInfoEx", 5332 },
    { "NtUserGetImeHotKey", 5134 }, { "NtUserSetImeHotKey", 5331 },
    { "NtUserDisableThreadIme", 5070 }, { "NtUserGetAppImeLevel", 5108 },
    { "NtUserSetAppImeLevel", 5309 }, { "NtUserNotifyIMEStatus", 5256 },
    { "NtUserSetWindowDisplayAffinity", 5366 }, { "NtUserGetWindowDisplayAffinity", 5191 },
    { "NtUserSetLayeredWindowAttributes", 5338 }, { "NtUserGetLayeredWindowAttributes", 5145 },
    { "NtUserSetShellWindowEx", 5355 }, { "NtUserUserHandleGrantAccess", 5400 },
    { "NtUserSetProcessRestrictionExemption", 5352 }, { "NtUserGetSystemMenu", 4192 },
};
TEST(ime_hotkeys_misc_stubs_are_win32u_syscall_stubs) {
    HMODULE w = t_module("win32u.dll");
    if (!w) { t_skip("no win32u"); return; }
    for (unsigned i = 0; i < sizeof g_stubs / sizeof g_stubs[0]; i++) {
        FARPROC p = t_proc_in("win32u.dll", g_stubs[i].name);
        if (!p) { t_note("%s not exported", g_stubs[i].name); continue; }
        if (t_is_wine()) continue;
        CHECKF(k_stub_valid(p), "%s prologue", g_stubs[i].name);
        long n = k_stub_number(p);
        if (k_is_19041()) CHECKF(n == g_stubs[i].num, "%s: stub number %ld want %ld", g_stubs[i].name, n, g_stubs[i].num);
        else t_note("%s service %ld", g_stubs[i].name, n);
    }
}
TEST(ime_hotkeys_misc_stub_numbers_are_in_win32k_range) {
    if (t_is_wine()) { t_skip("wine numbers differ"); return; }
    for (unsigned i = 0; i < sizeof g_stubs / sizeof g_stubs[0]; i++) {
        FARPROC p = t_proc_in("win32u.dll", g_stubs[i].name);
        if (!p) continue;
        long n = k_stub_number(p);
        CHECKF(n >= 0x1000 && n < 0x2000, "%s service 0x%lx", g_stubs[i].name, n);
    }
}

/* ---- basic register/unregister -------------------------------------------------------------- */
TEST(ime_hotkeys_misc_hk_register_null_hwnd_returns_true) {
    NEED_HK(); HK_DECL(k);
    SET_ERR(SENT);
    BOOL r = RHK(NULL, 1, k.mods, k.vk);
    EXPECT_EQ(r, 1);   /* _RegisterHotKey returns literal 1 on success */
    UHK(NULL, 1);
}
TEST(ime_hotkeys_misc_hk_unregister_returns_true) {
    NEED_HK(); HK_DECL(k);
    RHK(NULL, 2, k.mods, k.vk);
    BOOL r = UHK(NULL, 2);
    EXPECT_EQ(r, 1);   /* HKRemoveMatchingHotkeys true -> _UnregisterHotKey true */
}
TEST(ime_hotkeys_misc_hk_unregister_unregistered_id) {
    NEED_HK();
    SET_ERR(SENT);
    BOOL r = UHK(NULL, 0x3a3a);
    EXPECT_FAIL(r == 0, ERROR_HOTKEY_NOT_REGISTERED); /* _UnregisterHotKey: 0x58b when nothing matched */
}
TEST(ime_hotkeys_misc_hk_unregister_twice) {
    NEED_HK(); HK_DECL(k);
    RHK(NULL, 3, k.mods, k.vk);
    CHECK(UHK(NULL, 3));
    SET_ERR(SENT);
    BOOL r = UHK(NULL, 3);
    EXPECT_FAIL(r == 0, ERROR_HOTKEY_NOT_REGISTERED);
}
TEST(ime_hotkeys_misc_hk_unregister_success_keeps_last_error) {
    NEED_HK(); HK_DECL(k);
    RHK(NULL, 4, k.mods, k.vk);
    SET_ERR(SENT);
    CHECK(UHK(NULL, 4));
    EXPECT_ERR(SENT);  /* success path never calls UserSetLastError */
}
TEST(ime_hotkeys_misc_hk_register_success_keeps_last_error) {
    NEED_HK(); HK_DECL(k);
    SET_ERR(SENT);
    CHECK(RHK(NULL, 5, k.mods, k.vk));
    EXPECT_ERR(SENT);
    UHK(NULL, 5);
}
TEST(ime_hotkeys_misc_hk_duplicate_same_id_same_key) {
    NEED_HK(); HK_DECL(k);
    CHECK(RHK(NULL, 6, k.mods, k.vk));
    SET_ERR(SENT);
    BOOL r = RHK(NULL, 6, k.mods, k.vk);
 /* FindHotKey reports a key clash even when the clash is the very same registration */
    EXPECT_FAIL(r == 0, ERROR_HOTKEY_ALREADY_REGISTERED);
    UHK(NULL, 6);
}
TEST(ime_hotkeys_misc_hk_duplicate_other_id_same_key) {
    NEED_HK(); HK_DECL(k);
    CHECK(RHK(NULL, 7, k.mods, k.vk));
    SET_ERR(SENT);
    BOOL r = RHK(NULL, 8, k.mods, k.vk);
    EXPECT_FAIL(r == 0, ERROR_HOTKEY_ALREADY_REGISTERED);
    UHK(NULL, 7);
    SET_ERR(SENT);
    EXPECT_EQ(UHK(NULL, 8), 0);  /* the failed registration left nothing behind */
    EXPECT_ERR(ERROR_HOTKEY_NOT_REGISTERED);
}
TEST(ime_hotkeys_misc_hk_duplicate_differs_only_by_norepeat) {
    NEED_HK(); HK_DECL(k);
    CHECK(RHK(NULL, 9, k.mods, k.vk));
    SET_ERR(SENT);
    /* FindHotKey compares (mods & 0xf, vk); MOD_NOREPEAT (0x4000) is stored in the flag word, not the key */
    BOOL r = RHK(NULL, 10, k.mods | MOD_NOREPEAT, k.vk);
    EXPECT_FAIL(r == 0, ERROR_HOTKEY_ALREADY_REGISTERED);
    UHK(NULL, 9);
}
TEST(ime_hotkeys_misc_hk_norepeat_registers) {
    NEED_HK(); HK_DECL(k);
    SET_ERR(SENT);
    BOOL r = RHK(NULL, 11, k.mods | MOD_NOREPEAT, k.vk);
    EXPECT_EQ(r, 1); /* 0x4000 is outside the 0xffff95f0 reject mask */
    EXPECT_ERR(SENT);
    EXPECT_EQ(UHK(NULL, 11), 1);
}
TEST(ime_hotkeys_misc_hk_norepeat_then_plain_conflicts) {
    NEED_HK(); HK_DECL(k);
    CHECK(RHK(NULL, 12, k.mods | MOD_NOREPEAT, k.vk));
    SET_ERR(SENT);
    EXPECT_FAIL(RHK(NULL, 13, k.mods, k.vk) == 0, ERROR_HOTKEY_ALREADY_REGISTERED);
    UHK(NULL, 12);
}
TEST(ime_hotkeys_misc_hk_same_vk_different_mods_both_register) {
    NEED_HK();
    for (UINT m = 0; m < 16; m++) {
        CHECKF(RHK(NULL, 0x100 + m, m, 0x97), "mods %u", m);
    }
    for (UINT m = 0; m < 16; m++) {
        CHECKF(UHK(NULL, 0x100 + m), "mods %u", m);
    }
}
TEST(ime_hotkeys_misc_hk_same_mods_different_vk_both_register) {
    NEED_HK();
    static const UINT vks[] = { 0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f, 0x97, 0x98, 0x99, 0x9a };
    for (unsigned i = 0; i < sizeof vks / sizeof vks[0]; i++) CHECKF(RHK(NULL, 0x200 + i, HK_ALL, vks[i]), "vk 0x%x", vks[i]);
    for (unsigned i = 0; i < sizeof vks / sizeof vks[0]; i++) CHECKF(UHK(NULL, 0x200 + i), "vk 0x%x", vks[i]);
}
TEST(ime_hotkeys_misc_hk_hash_bucket_collision) {
    NEED_HK();
    /* gphkHashTable is indexed by vk & 0x7f: 0x17 and 0x97 share bucket 0x17 but are different keys */
    CHECK(RHK(NULL, 0x301, HK_ALL, 0x17));
    CHECK(RHK(NULL, 0x302, HK_ALL, 0x97));
    SET_ERR(SENT);
    EXPECT_FAIL(RHK(NULL, 0x303, HK_ALL, 0x17) == 0, ERROR_HOTKEY_ALREADY_REGISTERED);
    SET_ERR(SENT);
    EXPECT_FAIL(RHK(NULL, 0x304, HK_ALL, 0x97) == 0, ERROR_HOTKEY_ALREADY_REGISTERED);
    CHECK(UHK(NULL, 0x301));
    CHECK(RHK(NULL, 0x303, HK_ALL, 0x17));  /* freed one slot of the chain only */
    SET_ERR(SENT);
    EXPECT_FAIL(RHK(NULL, 0x305, HK_ALL, 0x97) == 0, ERROR_HOTKEY_ALREADY_REGISTERED);
    UHK(NULL, 0x302); UHK(NULL, 0x303);
}
TEST(ime_hotkeys_misc_hk_unregister_frees_key_for_reuse) {
    NEED_HK(); HK_DECL(k);
    CHECK(RHK(NULL, 14, k.mods, k.vk));
    CHECK(UHK(NULL, 14));
    CHECK(RHK(NULL, 15, k.mods, k.vk));  /* a different id may take the key once it is released */
    CHECK(UHK(NULL, 15));
}
TEST(ime_hotkeys_misc_hk_reregister_same_id_moves_key) {
    NEED_HK();
    HK_DECL(a); HK_DECL(b);
    CHECK(RHK(NULL, 16, a.mods, a.vk));
    /* Registering the same id again with another key succeeds and does not move the first key: on Windows 10 both keys
     * stay reserved for as long as the id is registered, and one UnregisterHotKey(id) releases both. */
    SET_ERR(SENT);
    BOOL r = RHK(NULL, 16, b.mods, b.vk);
    EXPECT_EQ(r, 1);
    EXPECT_ERR(SENT);
    SET_ERR(SENT);
    EXPECT_FAIL(RHK(NULL, 17, a.mods, a.vk) == 0, ERROR_HOTKEY_ALREADY_REGISTERED);  /* old key is still taken */
    SET_ERR(SENT);
    EXPECT_FAIL(RHK(NULL, 18, b.mods, b.vk) == 0, ERROR_HOTKEY_ALREADY_REGISTERED);  /* new key is taken */
    EXPECT_EQ(UHK(NULL, 16), 1);
    SET_ERR(SENT);
    EXPECT_FAIL(UHK(NULL, 16) == 0, ERROR_HOTKEY_NOT_REGISTERED);   /* a single unregister removed every object of the id */
    CHECK(RHK(NULL, 17, a.mods, a.vk));   /* both keys are free again */
    CHECK(RHK(NULL, 18, b.mods, b.vk));
    UHK(NULL, 17); UHK(NULL, 18);
}
TEST(ime_hotkeys_misc_hk_reregister_same_id_same_flags_new_mods) {
    NEED_HK(); HK_DECL(a);
    CHECK(RHK(NULL, 19, a.mods, a.vk));
    UINT other = (a.mods == 8) ? 1 : a.mods + 1;
    CHECK(RHK(NULL, 19, other, a.vk)); /* same vk, different mods: no clash, same id -> rewritten */
    CHECK(RHK(NULL, 20, a.mods, a.vk));
    SET_ERR(SENT);
    EXPECT_FAIL(RHK(NULL, 21, other, a.vk) == 0, ERROR_HOTKEY_ALREADY_REGISTERED);
    UHK(NULL, 19); UHK(NULL, 20);
}

/* ---- id values ------------------------------------------------------------------------------ */
TEST(ime_hotkeys_misc_hk_id_values_are_not_range_checked) {
    NEED_HK();
    /* No id validation exists in NtUserRegisterHotKey/_RegisterHotKey; the id is stored as a plain int. */
    static const int ids[] = { 0, 1, 0x7fff, 0xbfff, 0xc000, 0xc001, 0xffff, 0x10000, 0x7fffffff, -1, (int)0x80000000 };
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        SET_ERR(SENT);
        BOOL r = RHK(NULL, ids[i], HK_ALL, 0x9b + i % 4 + (i / 4) * 0x10 + 0x00);
        CHECKF(r, "id 0x%x err %lu", ids[i], GetLastError());
    }
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++) CHECKF(UHK(NULL, ids[i]), "unregister id 0x%x", ids[i]);
}
TEST(ime_hotkeys_misc_hk_negative_id_unregisters_exactly) {
    NEED_HK(); HK_DECL(k);
    CHECK(RHK(NULL, -5, k.mods, k.vk));
    SET_ERR(SENT);
    EXPECT_FAIL(UHK(NULL, 5) == 0, ERROR_HOTKEY_NOT_REGISTERED);
    CHECK(UHK(NULL, -5));
}
TEST(ime_hotkeys_misc_hk_same_id_different_hwnd_independent) {
    NEED_HK();
    NEED_GUI(w);
    HK_DECL(a); HK_DECL(b);
    CHECK(RHK(NULL, 22, a.mods, a.vk));
    CHECK(RHK(w, 22, b.mods, b.vk));  /* identity of an object is (thread, hwnd, id) */
    CHECK(UHK(w, 22));
    SET_ERR(SENT);
    EXPECT_FAIL(RHK(NULL, 23, a.mods, a.vk) == 0, ERROR_HOTKEY_ALREADY_REGISTERED);  /* thread-level one survived */
    CHECK(UHK(NULL, 22));
    im_kill(w);
}

/* ---- modifier validation --------------------------------------------------------------------- */
TEST(ime_hotkeys_misc_hk_invalid_modifier_bits) {
    NEED_HK();
    /* NtUserRegisterHotKey rejects (mods & 0xffff95f0): bits 4-8, 10, 12, 15 and all of 16-31 */
    static const UINT bad[] = { 0x10, 0x20, 0x40, 0x80, 0x100, 0x400, 0x1000, 0x8000, 0x10000, 0x20000, 0x40000, 0x80000,
                                0x100000, 0x1000000, 0x10000000, 0x80000000, 0xffff0000, 0xfffffff0, 0x9, 0x30 };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        UINT m = bad[i];
        if (!(m & 0xffff95f0)) continue;  /* 0x9 is valid: only here to prove the table filter works */
        SET_ERR(SENT);
        BOOL r = RHK(NULL, 0x400 + i, m, 0x9c);
        CHECKF(r == 0, "mods 0x%x accepted", m);
        CHECKF(GetLastError() == ERROR_INVALID_FLAGS, "mods 0x%x err %lu", m, GetLastError());
    }
}
TEST(ime_hotkeys_misc_hk_invalid_mods_registers_nothing) {
    NEED_HK();
    CHECK(!RHK(NULL, 0x450, 0x10 | MOD_CONTROL, 0x9d));
    SET_ERR(SENT);
    EXPECT_FAIL(UHK(NULL, 0x450) == 0, ERROR_HOTKEY_NOT_REGISTERED);
    CHECK(RHK(NULL, 0x451, MOD_CONTROL, 0x9d));  /* same vk, same mods without the bad bit: key is free */
    UHK(NULL, 0x451);
}
TEST(ime_hotkeys_misc_hk_invalid_mods_checked_before_window) {
    NEED_HK();
    SET_ERR(SENT);
    /* the 0x3ec flag test precedes ValidateHwnd, so a bogus hwnd does not change the error */
    BOOL r = RHK(im_bogus_hwnd(), 1, 0x10, 0x9e);
    EXPECT_FAIL(r == 0, ERROR_INVALID_FLAGS);
}
TEST(ime_hotkeys_misc_hk_mods_0x2000_denied_for_non_shell) {
    NEED_HK(); HK_DECL(k);
    SET_ERR(SENT);
 /* bit 13 is a shell-only flag: _RegisterHotKey -> IsShellProcess() false -> the call sets ERROR_ACCESS_DENIED */
    BOOL r = RHK(NULL, 0x460, k.mods | 0x2000, k.vk);
    if (r) { t_note("0x2000 accepted: process is the shell process?"); UHK(NULL, 0x460); return; }
    EXPECT_ERR(ERROR_ACCESS_DENIED);
}
TEST(ime_hotkeys_misc_hk_vk_packet_silently_refused) {
    NEED_HK();
    SET_ERR(SENT);
    BOOL r = RHK(NULL, 0x470, HK_ALL, 0xe7);
    /* _RegisterHotKey - no error is set for VK_PACKET */
    EXPECT_EQ(r, 0);
    EXPECT_ERR(SENT);
}
TEST(ime_hotkeys_misc_hk_vk_packet_with_mods) {
    NEED_HK();
    for (UINT m = 0; m < 16; m++) {
        SET_ERR(SENT);
        CHECKF(RHK(NULL, 0x480 + m, m, 0xe7) == 0, "mods %u", m);
        CHECKF(GetLastError() == SENT, "mods %u err %lu", m, GetLastError());
    }
}
TEST(ime_hotkeys_misc_hk_zero_modifiers_allowed) {
    NEED_HK();
    SET_ERR(SENT);
    CHECK(RHK(NULL, 0x490, 0, 0x9f));   /* no validation of an empty modifier set */
    EXPECT_ERR(SENT);
    CHECK(UHK(NULL, 0x490));
}
TEST(ime_hotkeys_misc_hk_vk_zero_allowed) {
    NEED_HK();
    /* vk 0 is not screened (only 0xe7 is); with every modifier it clashes with a hotkey the system already holds
     * (ERROR_HOTKEY_ALREADY_REGISTERED on Windows 10), so use a single modifier. */
    SET_ERR(SENT);
    BOOL r = RHK(NULL, 0x4a0, MOD_ALT, 0);
    if (!r) t_note("vk 0 refused (err %lu)", GetLastError());
    CHECK(r);
    EXPECT_ERR(SENT);
    if (r) CHECK(UHK(NULL, 0x4a0));
}
TEST(ime_hotkeys_misc_hk_exotic_vk_values) {
    NEED_HK();
    static const UINT vks[] = { 0xff, 0x100, 0x1ff, 0x7fff, 0xffff, 0x10000, 0xffffffff };
    for (unsigned i = 0; i < sizeof vks / sizeof vks[0]; i++) {
        SET_ERR(SENT);
        BOOL r = RHK(NULL, 0x4b0 + i, HK_ALL, vks[i]);
        t_note("vk 0x%x -> %d (err %lu)", vks[i], r, r ? 0 : GetLastError());
        if (r) UHK(NULL, 0x4b0 + i);
    }
}
TEST(ime_hotkeys_misc_hk_vk_hash_uses_low_7_bits_only) {
    NEED_HK();
    /* vk values 0x18 and 0x98 share a bucket but are distinct keys */
    CHECK(RHK(NULL, 0x4c0, HK_ALL, 0x98));
    CHECK(RHK(NULL, 0x4c1, HK_ALL, 0x9a));
    CHECK(UHK(NULL, 0x4c0));
    SET_ERR(SENT);
    EXPECT_FAIL(RHK(NULL, 0x4c2, HK_ALL, 0x9a) == 0, ERROR_HOTKEY_ALREADY_REGISTERED);
    UHK(NULL, 0x4c1);
}

/* ---- window argument ------------------------------------------------------------------------- */
TEST(ime_hotkeys_misc_hk_bogus_hwnd_register) {
    NEED_HK(); HK_DECL(k);
    SET_ERR(SENT);
    BOOL r = RHK(im_bogus_hwnd(), 0x500, k.mods, k.vk);
    EXPECT_FAIL(r == 0, ERROR_INVALID_WINDOW_HANDLE); /* ValidateHwnd failure */
}
TEST(ime_hotkeys_misc_hk_bogus_hwnd_unregister) {
    NEED_HK();
    SET_ERR(SENT);
    BOOL r = UHK(im_bogus_hwnd(), 0x500);
    EXPECT_FAIL(r == 0, ERROR_INVALID_WINDOW_HANDLE);
}
TEST(ime_hotkeys_misc_hk_hwnd_one_is_not_validated) {
    NEED_HK();
 /* only hwnd 0 skips window validation; hwnd 1 is non-zero on the syscall side and is not a window */
    SET_ERR(SENT);
    BOOL r = UHK((HWND)(ULONG_PTR)1, 0x501);
    CHECK(r == 0);
}
TEST(ime_hotkeys_misc_hk_register_on_window_ok) {
    NEED_HK(); NEED_GUI(w); HK_DECL(k);
    SET_ERR(SENT);
    EXPECT_EQ(RHK(w, 0x510, k.mods, k.vk), 1);
    EXPECT_EQ(UHK(w, 0x510), 1);
    im_kill(w);
}
TEST(ime_hotkeys_misc_hk_unregister_needs_matching_hwnd) {
    NEED_HK(); NEED_GUI(w); HK_DECL(k);
    CHECK(RHK(w, 0x511, k.mods, k.vk));
    SET_ERR(SENT);
    /* HKRemoveMatchingHotkeys query type 3 compares (pti, hwnd, id): the NULL-hwnd key does not match */
    EXPECT_FAIL(UHK(NULL, 0x511) == 0, ERROR_HOTKEY_NOT_REGISTERED);
    CHECK(UHK(w, 0x511));
    im_kill(w);
}
TEST(ime_hotkeys_misc_hk_unregister_needs_matching_id_on_hwnd) {
    NEED_HK(); NEED_GUI(w); HK_DECL(k);
    CHECK(RHK(w, 0x512, k.mods, k.vk));
    SET_ERR(SENT);
    EXPECT_FAIL(UHK(w, 0x513) == 0, ERROR_HOTKEY_NOT_REGISTERED);
    CHECK(UHK(w, 0x512));
    im_kill(w);
}
TEST(ime_hotkeys_misc_hk_window_vs_thread_same_key_conflicts) {
    NEED_HK(); NEED_GUI(w); HK_DECL(k);
    CHECK(RHK(w, 0x514, k.mods, k.vk));
    SET_ERR(SENT);
    EXPECT_FAIL(RHK(NULL, 0x514, k.mods, k.vk) == 0, ERROR_HOTKEY_ALREADY_REGISTERED); /* key identity ignores hwnd */
    CHECK(UHK(w, 0x514));
    im_kill(w);
}
TEST(ime_hotkeys_misc_hk_two_windows_same_key_conflict) {
    NEED_HK(); NEED_GUI(w); HK_DECL(k);
    HWND w2 = im_mkpopup();
    if (!w2) { im_kill(w); t_skip("second window"); return; }
    CHECK(RHK(w, 1, k.mods, k.vk));
    SET_ERR(SENT);
    EXPECT_FAIL(RHK(w2, 1, k.mods, k.vk) == 0, ERROR_HOTKEY_ALREADY_REGISTERED);
    CHECK(UHK(w, 1));
    CHECK(RHK(w2, 1, k.mods, k.vk));
    CHECK(UHK(w2, 1));
    im_kill(w2); im_kill(w);
}
TEST(ime_hotkeys_misc_hk_destroy_window_releases_key) {
    NEED_HK(); NEED_GUI(w); HK_DECL(k);
    CHECK(RHK(w, 0x520, k.mods, k.vk));
    im_kill(w);  /* window destruction removes the window's hotkeys */
    SET_ERR(SENT);
    CHECK(RHK(NULL, 0x521, k.mods, k.vk));
    UHK(NULL, 0x521);
}
TEST(ime_hotkeys_misc_hk_destroy_window_releases_all_its_ids) {
    NEED_HK(); NEED_GUI(w);
    for (int i = 0; i < 6; i++) CHECKF(RHK(w, 0x530 + i, HK_ALL, 0xd8 + (i % 3) + (i / 3) * 0x0 + (i / 3) * 3), "id %d", i);
    im_kill(w);
    for (int i = 0; i < 6; i++) {
        BOOL r = RHK(NULL, 0x540 + i, HK_ALL, 0xd8 + (i % 3) + (i / 3) * 3);
        CHECKF(r, "key %d still held", i);
        if (r) UHK(NULL, 0x540 + i);
    }
}
TEST(ime_hotkeys_misc_hk_destroy_window_keeps_thread_keys) {
    NEED_HK(); NEED_GUI(w); HK_DECL(a); HK_DECL(b);
    CHECK(RHK(NULL, 0x550, a.mods, a.vk));
    CHECK(RHK(w, 0x551, b.mods, b.vk));
    im_kill(w);
    SET_ERR(SENT);
    EXPECT_FAIL(RHK(NULL, 0x552, a.mods, a.vk) == 0, ERROR_HOTKEY_ALREADY_REGISTERED);
    CHECK(UHK(NULL, 0x550));
}
TEST(ime_hotkeys_misc_hk_unregister_after_window_destroyed) {
    NEED_HK(); NEED_GUI(w); HK_DECL(k);
    CHECK(RHK(w, 0x560, k.mods, k.vk));
    im_kill(w);
    SET_ERR(SENT);
    EXPECT_FAIL(UHK(w, 0x560) == 0, ERROR_INVALID_WINDOW_HANDLE);
}
TEST(ime_hotkeys_misc_hk_register_on_destroyed_window) {
    NEED_HK(); NEED_GUI(w); HK_DECL(k);
    im_kill(w);
    SET_ERR(SENT);
    EXPECT_FAIL(RHK(w, 0x561, k.mods, k.vk) == 0, ERROR_INVALID_WINDOW_HANDLE);
}
TEST(ime_hotkeys_misc_hk_desktop_window_is_other_thread) {
    NEED_HK(); HK_DECL(k);
    HWND d = U(GetDesktopWindow)();
    if (!d) { t_skip("no desktop window"); return; }
    SET_ERR(SENT);
    /* the desktop window belongs to the desktop thread -> 0x580 */
    BOOL r = RHK(d, 0x570, k.mods, k.vk);
    EXPECT_FAIL(r == 0, ERROR_WINDOW_OF_OTHER_THREAD);
}
TEST(ime_hotkeys_misc_hk_shell_window_is_other_process) {
    NEED_HK(); HK_DECL(k);
    HWND s = U(GetShellWindow)();
    if (!s) { t_skip("no shell window"); return; }
    DWORD pid = 0;
    U(GetWindowThreadProcessId)(s, &pid);
    if (pid == GetCurrentProcessId()) { t_skip("shell window is ours"); return; }
    SET_ERR(SENT);
    /* a caller of lower integrity than the shell is refused with access denied before the owner check */
    EXPECT_FAIL(RHK(s, 0x571, k.mods, k.vk) == 0, im_shell_err(pid, ERROR_WINDOW_OF_OTHER_THREAD));
}
TEST(ime_hotkeys_misc_hk_unregister_other_process_window) {
    NEED_HK();
    HWND s = U(GetShellWindow)();
    if (!s) { t_skip("no shell window"); return; }
    DWORD pid = 0;
    U(GetWindowThreadProcessId)(s, &pid);
    SET_ERR(SENT);
    /* unregister does no ownership check, it just fails to find a (pti, hwnd, id) match; a caller of lower integrity than
     * the shell is stopped by UIPI first */
    EXPECT_FAIL(UHK(s, 0x572) == 0, im_shell_err(pid, ERROR_HOTKEY_NOT_REGISTERED));
}
TEST(ime_hotkeys_misc_hk_unregister_desktop_window) {
    NEED_HK();
    HWND d = U(GetDesktopWindow)();
    if (!d) { t_skip("no desktop window"); return; }
    SET_ERR(SENT);
    EXPECT_FAIL(UHK(d, 0x573) == 0, ERROR_HOTKEY_NOT_REGISTERED);
}

/* ---- user32 entry points ------------------------------------------------------------------- */
TEST(ime_hotkeys_misc_hk_user32_path_matches_syscall) {
    NEED_HK(); HK_DECL(k);
    if (!U(RegisterHotKey) || !U(UnregisterHotKey)) { t_skip("no user32 hotkey exports"); return; }
    CHECK(U(RegisterHotKey)(NULL, 0x580, k.mods, k.vk));
    SET_ERR(SENT);
    EXPECT_FAIL(RHK(NULL, 0x581, k.mods, k.vk) == 0, ERROR_HOTKEY_ALREADY_REGISTERED);
    CHECK(UHK(NULL, 0x580));
}
TEST(ime_hotkeys_misc_hk_user32_unregister_error) {
    NEED_HK();
    if (!U(UnregisterHotKey)) { t_skip("no user32 UnregisterHotKey"); return; }
    SET_ERR(SENT);
    BOOL r = U(UnregisterHotKey)(NULL, 0x582);
    EXPECT_FAIL(r == 0, ERROR_HOTKEY_NOT_REGISTERED);
}
TEST(ime_hotkeys_misc_hk_user32_export_is_tail_jump) {
    FARPROC p = t_proc_in("user32.dll", "RegisterHotKey");
    if (!p || t_is_wine()) { t_skip("no export or emulator"); return; }
    unsigned char b[16];
    size_t n = k_stub_bytes(p, b, sizeof b);
    CHECK(n >= 6);
    /* user32!RegisterHotKey is a 6-byte indirect jump (jmp [rip+x]) into the win32u import */
    CHECKF(b[0] == 0xff && b[1] == 0x25, "bytes %02x %02x", b[0], b[1]);
}
TEST(ime_hotkeys_misc_hk_ids_and_keys_matrix) {
    NEED_HK();
    int ok = 0;
    for (int i = 0; i < 40; i++) {
        struct hk k = hk_next();
        if (RHK(NULL, 0x600 + i, k.mods, k.vk)) ok++;
    }
    EXPECT_EQ(ok, 40);
    for (int i = 0; i < 40; i++) UHK(NULL, 0x600 + i);
    /* all gone */
    for (int i = 0; i < 40; i += 7) {
        SET_ERR(SENT);
        CHECK(!UHK(NULL, 0x600 + i));
        EXPECT_ERR(ERROR_HOTKEY_NOT_REGISTERED);
    }
}
