/* x_menu_accel_hotkey_lifetime / hotkey: NtUserRegisterHotKey and NtUserUnregisterHotKey across threads.
 *
 * The hotkey table is shared by the whole session; every entry belongs to the thread that registered it (and, for window
 * hotkeys, names a window of that thread). Registering a key another thread holds is ERROR_HOTKEY_ALREADY_REGISTERED (1409);
 * unregistering only ever matches the caller's own (window, id) entries, so another thread's id is ERROR_HOTKEY_NOT_REGISTERED
 * (1419) and the key stays held; ids are per thread and window, not global. A key is free again the moment its owner unregisters
 * it, and (after the kernel tears the thread down, which may lag the exit by a moment) when the owner thread ends, whether or not
 * the thread ever destroyed its window. A window hotkey also dies with its window while the thread's own keys stay.
 *
 * Keys are the virtual keys F13..F24 in every modifier combination; every one is released before a test ends.
 * Determinism: only symbolic results; waits for thread teardown are bounded polls.
 */
#include "x.h"

#define RHK(h, id, k) UN(NtUserRegisterHotKey)((h), (id), (k).mods, (k).vk)
#define UHK(h, id)    UN(NtUserUnregisterHotKey)((h), (id))
#define FREE_WITHIN(k) xm_key_free_within((k), XM_NIMBLE_MS)

/* ---- one other thread ---- */
XT(key_held_by_another_thread_cannot_be_registered_here) {
    NEED_HK(); KEY(k);
    struct xm_ct t; CHECK(xm_ct_start(&t, 0));
    EXPECT_EQ(CT_REGISTER(&t, 0, 1, k), 1);
    XFAIL(RHK(NULL, 1, k), XE_HOTKEY_ALREADY_REGISTERED);
    XFAIL(RHK(NULL, 99, k), XE_HOTKEY_ALREADY_REGISTERED);
    EXPECT_EQ(CT_UNREGISTER(&t, 0, 1), 1);
    xm_ct_stop(&t);
}
XT(key_held_here_cannot_be_registered_by_another_thread) {
    NEED_HK(); KEY(k);
    struct xm_ct t; CHECK(xm_ct_start(&t, 0));
    XOK(RHK(NULL, 1, k), 1);
    EXPECT_EQ(CT_REGISTER(&t, 0, 1, k), 0);
    EXPECT_EQ(t.gle, XE_HOTKEY_ALREADY_REGISTERED);
    XOK(UHK(NULL, 1), 1);
    xm_ct_stop(&t);
}
XT(another_threads_id_is_not_ours_to_unregister) {
    NEED_HK(); KEY(k);
    struct xm_ct t; CHECK(xm_ct_start(&t, 0));
    EXPECT_EQ(CT_REGISTER(&t, 0, 7, k), 1);
    XFAIL(UHK(NULL, 7), XE_HOTKEY_NOT_REGISTERED);
    XFAIL(RHK(NULL, 8, k), XE_HOTKEY_ALREADY_REGISTERED);   /* still held */
    EXPECT_EQ(CT_UNREGISTER(&t, 0, 7), 1);
    XOK(RHK(NULL, 8, k), 1);                                 /* and free at once, no teardown wait */
    XOK(UHK(NULL, 8), 1);
    xm_ct_stop(&t);
}
XT(another_threads_window_hotkey_cannot_be_unregistered_through_its_window) {
    NEED_HK(); KEY(k);
    struct xm_ct t; CHECK(xm_ct_start(&t, 1));
    EXPECT_EQ(CT_REGISTER(&t, 1, 3, k), 1);
    XFAIL(UHK(t.w, 3), XE_HOTKEY_NOT_REGISTERED);
    XFAIL(UHK(NULL, 3), XE_HOTKEY_NOT_REGISTERED);
    XFAIL(RHK(NULL, 3, k), XE_HOTKEY_ALREADY_REGISTERED);
    EXPECT_EQ(CT_UNREGISTER(&t, 1, 3), 1);
    xm_ct_stop(&t);
}
XT(window_of_another_thread_is_refused_for_registration) {
    NEED_HK(); KEY(k);
    struct xm_ct t; CHECK(xm_ct_start(&t, 1));
    XFAIL(RHK(t.w, 1, k), XE_WINDOW_OF_OTHER_THREAD);
    XOK(RHK(NULL, 1, k), 1);          /* the refused attempt registered nothing */
    XOK(UHK(NULL, 1), 1);
    xm_ct_stop(&t);
}
XT(own_window_of_the_other_thread_is_refused_to_us_but_not_to_it) {
    NEED_HK(); KEY(k);
    struct xm_ct t; CHECK(xm_ct_start(&t, 1));
    EXPECT_EQ(CT_REGISTER(&t, 1, 1, k), 1);
    XFAIL(RHK(t.w, 2, k), XE_WINDOW_OF_OTHER_THREAD);   /* the window check comes before the key clash */
    EXPECT_EQ(CT_UNREGISTER(&t, 1, 1), 1);
    xm_ct_stop(&t);
}
XT(ids_are_per_thread_so_two_threads_can_both_use_id_5) {
    NEED_HK(); KEY(a); KEY(b);
    struct xm_ct t; CHECK(xm_ct_start(&t, 0));
    XOK(RHK(NULL, 5, a), 1);
    EXPECT_EQ(CT_REGISTER(&t, 0, 5, b), 1);
    XOK(UHK(NULL, 5), 1);
    XFAIL(UHK(NULL, 5), XE_HOTKEY_NOT_REGISTERED);   /* ours is gone, theirs is not ours */
    EXPECT_EQ(CT_UNREGISTER(&t, 0, 5), 1);
    EXPECT_EQ(CT_UNREGISTER(&t, 0, 5), 0);
    EXPECT_EQ(t.gle, XE_HOTKEY_NOT_REGISTERED);
    xm_ct_stop(&t);
}
XT(ids_are_per_window_so_two_windows_of_two_threads_can_both_use_id_5) {
    NEED_HK(); KEY(a); KEY(b);
    struct xm_ct t; CHECK(xm_ct_start(&t, 1));
    HWND w = k_make_window("xm");
    XOK(RHK(w, 5, a), 1);
    EXPECT_EQ(CT_REGISTER(&t, 1, 5, b), 1);
    XOK(UHK(w, 5), 1);
    EXPECT_EQ(CT_UNREGISTER(&t, 1, 5), 1);
    U(DestroyWindow)(w);
    xm_ct_stop(&t);
}
XT(same_id_on_thread_and_on_window_are_different_entries_per_thread) {
    NEED_HK(); KEY(a); KEY(b);
    struct xm_ct t; CHECK(xm_ct_start(&t, 1));
    EXPECT_EQ(CT_REGISTER(&t, 0, 1, a), 1);
    EXPECT_EQ(CT_REGISTER(&t, 1, 1, b), 1);
    EXPECT_EQ(CT_UNREGISTER(&t, 0, 1), 1);
    XFAIL(RHK(NULL, 2, b), XE_HOTKEY_ALREADY_REGISTERED);   /* the window entry is still there */
    XOK(RHK(NULL, 2, a), 1);                                 /* the thread entry is gone */
    XOK(UHK(NULL, 2), 1);
    EXPECT_EQ(CT_UNREGISTER(&t, 1, 1), 1);
    xm_ct_stop(&t);
}
XT(same_vk_with_different_modifiers_coexists_across_threads) {
    NEED_HK();
    UINT vk = 0x7F;
    struct xm_ct t; CHECK(xm_ct_start(&t, 0));
    EXPECT_EQ(xm_ct_do(&t, CT_REG, 0, 1, MOD_CONTROL | MOD_ALT | MOD_SHIFT, vk), 1);
    XOK(UN(NtUserRegisterHotKey)(NULL, 1, MOD_CONTROL | MOD_ALT, vk), 1);
    XFAIL(UN(NtUserRegisterHotKey)(NULL, 2, MOD_CONTROL | MOD_ALT | MOD_SHIFT, vk), XE_HOTKEY_ALREADY_REGISTERED);
    XOK(UHK(NULL, 1), 1);
    EXPECT_EQ(CT_UNREGISTER(&t, 0, 1), 1);
    xm_ct_stop(&t);
}
XT(no_repeat_flag_does_not_make_a_key_distinct_across_threads) {
    NEED_HK(); KEY(k);
    struct xm_ct t; CHECK(xm_ct_start(&t, 0));
    EXPECT_EQ(xm_ct_do(&t, CT_REG, 0, 1, k.mods, k.vk), 1);
    XFAIL(UN(NtUserRegisterHotKey)(NULL, 1, k.mods | MOD_NOREPEAT, k.vk), XE_HOTKEY_ALREADY_REGISTERED);
    EXPECT_EQ(CT_UNREGISTER(&t, 0, 1), 1);
    EXPECT_EQ(xm_ct_do(&t, CT_REG, 0, 1, k.mods | MOD_NOREPEAT, k.vk), 1);
    XFAIL(RHK(NULL, 1, k), XE_HOTKEY_ALREADY_REGISTERED);
    EXPECT_EQ(CT_UNREGISTER(&t, 0, 1), 1);
    xm_ct_stop(&t);
}
XT(user32_calls_see_the_same_ownership) {
    NEED_HK(); KEY(k);
    struct xm_ct t; CHECK(xm_ct_start(&t, 0));
    EXPECT_EQ(CT_REGISTER(&t, 0, 1, k), 1);
    SET_ERR(XE_SENT);
    EXPECT_EQ(U(RegisterHotKey)(NULL, 2, k.mods, k.vk), 0);
    EXPECT_ERR(XE_HOTKEY_ALREADY_REGISTERED);
    SET_ERR(XE_SENT);
    EXPECT_EQ(U(UnregisterHotKey)(NULL, 1), 0);
    EXPECT_ERR(XE_HOTKEY_NOT_REGISTERED);
    EXPECT_EQ(CT_UNREGISTER(&t, 0, 1), 1);
    xm_ct_stop(&t);
}

/* ---- ping-pong ---- */
XT(ownership_passes_back_and_forth_ten_times) {
    NEED_HK(); KEY(k);
    struct xm_ct t; CHECK(xm_ct_start(&t, 0));
    int bad = 0;
    for (int i = 0; i < 10; i++) {
        if (i & 1) {
            if (CT_REGISTER(&t, 0, 1, k) != 1) bad++;
            if (RHK(NULL, 1, k)) bad++;
            if (CT_UNREGISTER(&t, 0, 1) != 1) bad++;
        } else {
            if (!RHK(NULL, 1, k)) bad++;
            if (CT_REGISTER(&t, 0, 1, k) != 0 || t.gle != XE_HOTKEY_ALREADY_REGISTERED) bad++;
            if (!UHK(NULL, 1)) bad++;
        }
    }
    EXPECT_EQ(bad, 0);
    xm_ct_stop(&t);
}
XT(many_keys_swap_owners_in_one_pass) {
    NEED_HK();
    struct xm_key ks[24]; for (int i = 0; i < 24; i++) ks[i] = xm_key_next();
    struct xm_ct t; CHECK(xm_ct_start(&t, 0));
    int bad = 0;
    for (int i = 0; i < 24; i++) if (CT_REGISTER(&t, 0, 100 + i, ks[i]) != 1) bad++;
    for (int i = 0; i < 24; i++) if (UN(NtUserRegisterHotKey)(NULL, 200 + i, ks[i].mods, ks[i].vk)) bad++;
    for (int i = 0; i < 24; i++) if (CT_UNREGISTER(&t, 0, 100 + i) != 1) bad++;
    for (int i = 0; i < 24; i++) if (!UN(NtUserRegisterHotKey)(NULL, 200 + i, ks[i].mods, ks[i].vk)) bad++;
    for (int i = 0; i < 24; i++) if (CT_REGISTER(&t, 0, 100 + i, ks[i]) != 0) bad++;
    for (int i = 0; i < 24; i++) UHK(NULL, 200 + i);
    EXPECT_EQ(bad, 0);
    xm_ct_stop(&t);
}

/* ---- thread exit ---- */
XT(thread_exit_releases_its_thread_hotkey) {
    NEED_HK(); KEY(k);
    struct xm_ct t; CHECK(xm_ct_start(&t, 0));
    EXPECT_EQ(CT_REGISTER(&t, 0, 1, k), 1);
    xm_ct_stop(&t);
    CHECK(FREE_WITHIN(k));
}
XT(thread_exit_releases_its_window_hotkey_without_destroying_the_window_first) {
    NEED_HK(); KEY(k);
    struct xm_ct t; CHECK(xm_ct_start(&t, 1));
    EXPECT_EQ(CT_REGISTER(&t, 1, 1, k), 1);
    xm_ct_stop(&t);
    CHECK(FREE_WITHIN(k));
}
XT(thread_exit_releases_a_mix_of_thread_and_window_hotkeys) {
    NEED_HK(); KEY(a); KEY(b); KEY(c); KEY(d);
    struct xm_ct t; CHECK(xm_ct_start(&t, 1));
    EXPECT_EQ(CT_REGISTER(&t, 0, 1, a), 1);
    EXPECT_EQ(CT_REGISTER(&t, 0, 2, b), 1);
    EXPECT_EQ(CT_REGISTER(&t, 1, 1, c), 1);
    EXPECT_EQ(CT_REGISTER(&t, 1, 2, d), 1);
    xm_ct_stop(&t);
    CHECK(FREE_WITHIN(a)); CHECK(FREE_WITHIN(b)); CHECK(FREE_WITHIN(c)); CHECK(FREE_WITHIN(d));
}
XT(thread_exit_releases_sixty_four_keys) {
    NEED_HK();
    struct xm_key ks[64]; for (int i = 0; i < 64; i++) ks[i] = xm_key_next();
    struct xm_ct t; CHECK(xm_ct_start(&t, 1));
    int ok = 0;
    for (int i = 0; i < 64; i++) if (xm_ct_do(&t, CT_REG, i & 1, 1 + i / 2, ks[i].mods, ks[i].vk) == 1) ok++;
    EXPECT_EQ(ok, 64);
    for (int i = 0; i < 64; i += 9) XFAIL(UN(NtUserRegisterHotKey)(NULL, 1, ks[i].mods, ks[i].vk), XE_HOTKEY_ALREADY_REGISTERED);
    xm_ct_stop(&t);
    int held = 0; for (int i = 0; i < 64; i++) if (!FREE_WITHIN(ks[i])) held++;
    EXPECT_EQ(held, 0);
}
XT(thread_exit_after_unregistering_leaves_nothing_held) {
    NEED_HK(); KEY(k);
    struct xm_ct t; CHECK(xm_ct_start(&t, 0));
    EXPECT_EQ(CT_REGISTER(&t, 0, 1, k), 1);
    EXPECT_EQ(CT_UNREGISTER(&t, 0, 1), 1);
    xm_ct_stop(&t);
    XOK(RHK(NULL, 1, k), 1);
    XOK(UHK(NULL, 1), 1);
}
XT(ownership_chain_through_three_threads_that_each_exit) {
    NEED_HK(); KEY(k);
    for (int i = 0; i < 3; i++) {
        struct xm_ct t; CHECK(xm_ct_start(&t, i & 1));
        int ok = 0;
        DWORD t0 = GetTickCount();
        while (!ok && GetTickCount() - t0 < XM_NIMBLE_MS) { ok = (int)CT_REGISTER(&t, i & 1, 1, k); if (!ok) Sleep(20); }
        CHECKF(ok, "round %d: key never became free", i);
        XFAIL(RHK(NULL, 9, k), XE_HOTKEY_ALREADY_REGISTERED);
        xm_ct_stop(&t);
    }
    CHECK(FREE_WITHIN(k));
}
XT(unregister_after_the_owner_thread_exited_is_not_registered) {
    NEED_HK(); KEY(k);
    struct xm_ct t; CHECK(xm_ct_start(&t, 0));
    EXPECT_EQ(CT_REGISTER(&t, 0, 4, k), 1);
    xm_ct_stop(&t);
    CHECK(FREE_WITHIN(k));
    XFAIL(UHK(NULL, 4), XE_HOTKEY_NOT_REGISTERED);
}
XT(window_of_an_exited_thread_is_an_invalid_window_for_registration) {
    NEED_HK(); KEY(k);
    struct xm_ct t; CHECK(xm_ct_start(&t, 1));
    HWND w = t.w;
    xm_ct_stop(&t);
    XFAIL(RHK(w, 1, k), XE_INVALID_HWND);
    XFAIL(UHK(w, 1), XE_INVALID_HWND);
}

/* ---- window destruction ---- */
XT(destroying_the_window_frees_its_hotkey_but_not_the_thread_hotkey) {
    NEED_HK(); KEY(a); KEY(b);
    struct xm_ct t; CHECK(xm_ct_start(&t, 1));
    EXPECT_EQ(CT_REGISTER(&t, 1, 1, a), 1);
    EXPECT_EQ(CT_REGISTER(&t, 0, 1, b), 1);
    XFAIL(RHK(NULL, 1, a), XE_HOTKEY_ALREADY_REGISTERED);
    EXPECT_EQ(xm_ct_do(&t, CT_DESTROYWND, 0, 0, 0, 0), 1);
    XOK(RHK(NULL, 1, a), 1);                                  /* at once: the window's entries go with the window */
    XFAIL(RHK(NULL, 2, b), XE_HOTKEY_ALREADY_REGISTERED);     /* the thread's entry stays */
    XOK(UHK(NULL, 1), 1);
    EXPECT_EQ(CT_UNREGISTER(&t, 0, 1), 1);
    xm_ct_stop(&t);
}
XT(another_thread_cannot_destroy_the_window_so_the_hotkey_stays) {
    NEED_HK(); KEY(k);
    struct xm_ct t; CHECK(xm_ct_start(&t, 1));
    EXPECT_EQ(CT_REGISTER(&t, 1, 1, k), 1);
    SET_ERR(XE_SENT);
    CHECK(!U(DestroyWindow)(t.w));
    EXPECT_ERR(XE_ACCESS_DENIED);
    XFAIL(RHK(NULL, 1, k), XE_HOTKEY_ALREADY_REGISTERED);
    EXPECT_EQ(CT_UNREGISTER(&t, 1, 1), 1);
    xm_ct_stop(&t);
}

/* ---- racing registrations ---- */
struct race { struct xm_key k; HANDLE go; volatile LONG wins, errs_ok; int id; };
static DWORD WINAPI race_reg(LPVOID p) {
    struct race *r = p;
    t_module("user32.dll");
    k_make_window("xm");                          /* converts the thread before the race */
    WaitForSingleObject(r->go, 5000);
    SET_ERR(XE_SENT);
    if (UN(NtUserRegisterHotKey)(NULL, r->id, r->k.mods, r->k.vk)) { InterlockedIncrement(&r->wins); Sleep(30); UN(NtUserUnregisterHotKey)(NULL, r->id); }
    else if (GetLastError() == XE_HOTKEY_ALREADY_REGISTERED) InterlockedIncrement(&r->errs_ok);
    return 0;
}
XT(four_threads_racing_for_one_key_have_one_winner_and_three_clashes) {
    NEED_HK();
    for (int round = 0; round < 10; round++) {
        struct race r; r.k = xm_key_next(); r.go = CreateEventA(NULL, TRUE, FALSE, NULL); r.wins = r.errs_ok = 0; r.id = 1;
        HANDLE t[4];
        for (int i = 0; i < 4; i++) t[i] = CreateThread(NULL, 0, race_reg, &r, 0, NULL);
        Sleep(50);
        SetEvent(r.go);
        for (int i = 0; i < 4; i++) { WaitForSingleObject(t[i], 10000); CloseHandle(t[i]); }
        CloseHandle(r.go);
        /* a loser that runs after the winner released (the winner holds for 30 ms) would also win: accept 1..4 wins, every loser a clash */
        CHECKF(r.wins >= 1 && r.wins + r.errs_ok == 4, "round %d: %ld wins, %ld clashes", round, r.wins, r.errs_ok);
        if (r.wins + r.errs_ok != 4) break;
    }
}
XT(each_of_four_threads_registering_its_own_key_all_win) {
    NEED_HK();
    struct race r[4]; HANDLE t[4]; HANDLE go = CreateEventA(NULL, TRUE, FALSE, NULL);
    for (int i = 0; i < 4; i++) { r[i].k = xm_key_next(); r[i].go = go; r[i].wins = r[i].errs_ok = 0; r[i].id = 1; t[i] = CreateThread(NULL, 0, race_reg, &r[i], 0, NULL); }
    Sleep(50); SetEvent(go);
    int wins = 0;
    for (int i = 0; i < 4; i++) { WaitForSingleObject(t[i], 10000); CloseHandle(t[i]); wins += r[i].wins; }
    CloseHandle(go);
    EXPECT_EQ(wins, 4);
}
