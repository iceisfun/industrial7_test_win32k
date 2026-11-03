/* x_menu_accel_hotkey_lifetime / life: what a process leaves behind when it ends.
 *
 * A child process creates one of everything this area covers: accelerator tables, menus (one with a submenu), a thread hotkey, a
 * window hotkey, a hotkey on a second thread, and an IME hotkey entry. While it runs the parent sees all of it (tables and menus
 * are valid, the keys are held and cannot be unregistered from here); after it ends - by returning, by ExitProcess with another
 * thread still running, by TerminateProcess from outside, or by killing itself - the tables, menus and hotkeys are gone and the
 * keys can be registered again, while the IME hotkey entry, which belongs to the session and not to the process, stays until deleted.
 *
 * Determinism: handles are compared and probed, never printed; teardown waits are bounded polls.
 */
#include "x.h"

struct keys { struct xm_key a, b, c; };
CHILD(xm_life_child) {
    HANDLE m; struct xm_shm *s = xm_shm_open(argv[1], &m); if (!s) return 1;
    t_module("user32.dll");
    const char *how = argv[2];
    struct keys k = { { (UINT)atoi(argv[3]), (UINT)atoi(argv[4]) }, { (UINT)atoi(argv[5]), (UINT)atoi(argv[6]) }, { (UINT)atoi(argv[7]), (UINT)atoi(argv[8]) } };
    HWND w = k_make_window("life");
    s->v[0] = (LONG64)(ULONG_PTR)xm_acc_make(3);
    s->v[1] = (LONG64)(ULONG_PTR)xm_acc_make(5);
    HMENU a = U(CreatePopupMenu)(), sub = U(CreatePopupMenu)(), bar = U(CreateMenu)();
    U(AppendMenuA)(a, MF_POPUP, (UINT_PTR)sub, "s");
    s->v[2] = (LONG64)(ULONG_PTR)a; s->v[3] = (LONG64)(ULONG_PTR)sub; s->v[4] = (LONG64)(ULONG_PTR)bar;
    s->v[5] = UN(NtUserRegisterHotKey)(NULL, 1, k.a.mods, k.a.vk);
    s->v[6] = UN(NtUserRegisterHotKey)(w, 2, k.b.mods, k.b.vk);
    s->v[7] = UN(NtUserSetImeHotKey)(0x7ff30001, 3, 0x7D, NULL, 2);
    if (!strcmp(how, "thread") || !strcmp(how, "threadkill")) {
        struct xm_ct *t = calloc(1, sizeof *t);
        xm_ct_start(t, 0);
        s->v[8] = xm_ct_do(t, CT_REG, 0, 1, k.c.mods, k.c.vk);
    }
    InterlockedExchange(&s->ready, 1);
    xm_wait(&s->go, 30000);
    if (!strcmp(how, "kill") || !strcmp(how, "threadkill")) TerminateProcess(GetCurrentProcess(), 6);
    if (!strcmp(how, "thread")) ExitProcess(0);   /* the second thread is still alive */
    return 0;
}
struct life { struct xm_shm *s; HANDLE sm, p; PROCESS_INFORMATION pi; struct keys k; };
static int life_start(struct life *l, const char *how) {
    memset(l, 0, sizeof *l);
    l->k.a = xm_key_next(); l->k.b = xm_key_next(); l->k.c = xm_key_next();
    const char *tag = xm_tag(); char ex[160];
    l->s = xm_shm_create(tag, &l->sm);
    if (!l->s) return 0;
    snprintf(ex, sizeof ex, "%s %u %u %u %u %u %u", how, l->k.a.mods, l->k.a.vk, l->k.b.mods, l->k.b.vk, l->k.c.mods, l->k.c.vk);
    l->p = xm_spawn("xm_life_child", tag, ex, &l->pi);
    if (!l->p) return 0;
    if (!xm_wait(&l->s->ready, 30000)) { TerminateProcess(l->p, 1); xm_reap(l->p, &l->pi, 5000); l->p = NULL; return 0; }
    return 1;
}
#define LH(l, i) ((HANDLE)(ULONG_PTR)(l).s->v[i])
static DWORD life_end(struct life *l, int terminate) {
    if (terminate) TerminateProcess(l->p, 8);
    else InterlockedExchange(&l->s->go, 1);
    DWORD ec = xm_reap(l->p, &l->pi, 30000);
    l->p = NULL;
    return ec;
}
#define BEGIN(how) NEED_ACC(); NEED_HK(); NEED(NtUserSetMenuFlagRtoL); NEED_IHK(); \
    struct life L; if (!life_start(&L, how)) { CHECK(0 && "child did not start"); return; }
static int gone_tables(struct life *l) { int alive = 0; for (int i = 0; i < 2; i++) if (UN(NtUserCopyAcceleratorTable)(LH(*l, i), NULL, 0)) alive++; return alive; }
static int gone_menus(struct life *l) { int alive = 0; for (int i = 2; i < 5; i++) if (UN(NtUserSetMenuFlagRtoL)((HMENU)LH(*l, i))) alive++; return alive; }
static void drop_ime(void) { UN(NtUserSetImeHotKey)(0x7ff30001, 0, 0, NULL, 1); }

XT(while_the_child_runs_everything_it_made_is_visible_here) {
    BEGIN("exit");
    EXPECT_EQ(L.s->v[5], 1); EXPECT_EQ(L.s->v[6], 1); EXPECT_EQ(L.s->v[7], 1);
    EXPECT_EQ(UN(NtUserCopyAcceleratorTable)(LH(L, 0), NULL, 0), 3);
    EXPECT_EQ(UN(NtUserCopyAcceleratorTable)(LH(L, 1), NULL, 0), 5);
    for (int i = 2; i < 5; i++) CHECK(U(IsMenu)((HMENU)LH(L, i)));
    UINT m = 0, v = 0; XOK(UN(NtUserGetImeHotKey)(0x7ff30001, &m, &v, NULL), 1);
    EXPECT_EQ(m, 3); EXPECT_EQ(v, 0x7D);
    life_end(&L, 0);
    drop_ime();
}
XT(while_the_child_runs_its_keys_are_held_and_not_ours_to_remove) {
    BEGIN("exit");
    XFAIL(UN(NtUserRegisterHotKey)(NULL, 1, L.k.a.mods, L.k.a.vk), XE_HOTKEY_ALREADY_REGISTERED);
    XFAIL(UN(NtUserRegisterHotKey)(NULL, 1, L.k.b.mods, L.k.b.vk), XE_HOTKEY_ALREADY_REGISTERED);
    XFAIL(UN(NtUserUnregisterHotKey)(NULL, 1), XE_HOTKEY_NOT_REGISTERED);
    XFAIL(UN(NtUserUnregisterHotKey)(NULL, 2), XE_HOTKEY_NOT_REGISTERED);
    life_end(&L, 0);
    drop_ime();
}
XT(returning_from_main_releases_tables_menus_and_keys) {
    BEGIN("exit");
    EXPECT_EQ(life_end(&L, 0), 0);
    EXPECT_EQ(gone_tables(&L), 0);
    EXPECT_EQ(gone_menus(&L), 0);
    CHECK(xm_key_free_within(L.k.a, XM_NIMBLE_MS)); CHECK(xm_key_free_within(L.k.b, XM_NIMBLE_MS));
    drop_ime();
}
XT(exit_process_with_a_live_second_thread_releases_the_second_threads_key_too) {
    BEGIN("thread");
    EXPECT_EQ(L.s->v[8], 1);
    XFAIL(UN(NtUserRegisterHotKey)(NULL, 1, L.k.c.mods, L.k.c.vk), XE_HOTKEY_ALREADY_REGISTERED);
    EXPECT_EQ(life_end(&L, 0), 0);
    EXPECT_EQ(gone_tables(&L), 0);
    EXPECT_EQ(gone_menus(&L), 0);
    CHECK(xm_key_free_within(L.k.a, XM_NIMBLE_MS)); CHECK(xm_key_free_within(L.k.b, XM_NIMBLE_MS)); CHECK(xm_key_free_within(L.k.c, XM_NIMBLE_MS));
    drop_ime();
}
XT(terminate_process_from_outside_releases_everything) {
    BEGIN("exit");
    EXPECT_EQ(life_end(&L, 1), 8);
    EXPECT_EQ(gone_tables(&L), 0);
    EXPECT_EQ(gone_menus(&L), 0);
    CHECK(xm_key_free_within(L.k.a, XM_NIMBLE_MS)); CHECK(xm_key_free_within(L.k.b, XM_NIMBLE_MS));
    drop_ime();
}
XT(the_child_killing_itself_releases_everything) {
    BEGIN("kill");
    EXPECT_EQ(life_end(&L, 0), 6);
    EXPECT_EQ(gone_tables(&L), 0);
    EXPECT_EQ(gone_menus(&L), 0);
    CHECK(xm_key_free_within(L.k.a, XM_NIMBLE_MS)); CHECK(xm_key_free_within(L.k.b, XM_NIMBLE_MS));
    drop_ime();
}
XT(killing_a_child_with_a_second_thread_holding_a_key_releases_that_key) {
    BEGIN("threadkill");
    EXPECT_EQ(L.s->v[8], 1);
    EXPECT_EQ(life_end(&L, 0), 6);
    CHECK(xm_key_free_within(L.k.a, XM_NIMBLE_MS)); CHECK(xm_key_free_within(L.k.b, XM_NIMBLE_MS)); CHECK(xm_key_free_within(L.k.c, XM_NIMBLE_MS));
    EXPECT_EQ(gone_tables(&L), 0);
    drop_ime();
}
XT(destroyed_tables_report_the_invalid_accel_handle_error_after_the_owner_ends) {
    BEGIN("exit");
    life_end(&L, 0);
    for (int i = 0; i < 2; i++) { XFAIL(UN(NtUserCopyAcceleratorTable)(LH(L, i), NULL, 0), XE_INVALID_ACCEL_HANDLE); XFAIL(UN(NtUserDestroyAcceleratorTable)(LH(L, i)), XE_INVALID_ACCEL_HANDLE); }
    drop_ime();
}
XT(destroyed_menus_report_the_invalid_menu_handle_error_after_the_owner_ends) {
    BEGIN("exit");
    life_end(&L, 0);
    for (int i = 2; i < 5; i++) { XFAIL(UN(NtUserSetMenuFlagRtoL)((HMENU)LH(L, i)), XE_INVALID_MENU_HANDLE); CHECK(!U(IsMenu)((HMENU)LH(L, i))); }
    drop_ime();
}
XT(the_ime_hotkey_entry_outlives_the_process_that_set_it) {
    BEGIN("exit");
    life_end(&L, 0);
    UINT m = 0, v = 0;
    XOK(UN(NtUserGetImeHotKey)(0x7ff30001, &m, &v, NULL), 1);
    EXPECT_EQ(m, 3); EXPECT_EQ(v, 0x7D);
    XOK(UN(NtUserSetImeHotKey)(0x7ff30001, 0, 0, NULL, 1), 1);
    XFAIL(UN(NtUserGetImeHotKey)(0x7ff30001, &m, &v, NULL), XE_HOTKEY_NOT_REGISTERED);
}
XT(the_ime_hotkey_entry_outlives_a_killed_process_too) {
    BEGIN("exit");
    life_end(&L, 1);
    UINT m = 0, v = 0;
    XOK(UN(NtUserGetImeHotKey)(0x7ff30001, &m, &v, NULL), 1);
    drop_ime();
}
XT(keys_of_one_child_can_be_taken_over_by_the_next_child) {
    NEED_HK();
    struct life L1; if (!life_start(&L1, "exit")) { CHECK(0 && "child did not start"); return; }
    struct xm_key a = L1.k.a, b = L1.k.b;
    life_end(&L1, 0);
    CHECK(xm_key_free_within(a, XM_NIMBLE_MS)); CHECK(xm_key_free_within(b, XM_NIMBLE_MS));
    /* a second child registering the very same keys succeeds */
    struct life L2; memset(&L2, 0, sizeof L2);
    const char *tag = xm_tag(); char ex[160];
    L2.s = xm_shm_create(tag, &L2.sm);
    snprintf(ex, sizeof ex, "exit %u %u %u %u %u %u", a.mods, a.vk, b.mods, b.vk, 1u, 0x7Cu);
    L2.p = xm_spawn("xm_life_child", tag, ex, &L2.pi);
    CHECK(L2.p && xm_wait(&L2.s->ready, 30000));
    EXPECT_EQ(L2.s->v[5], 1); EXPECT_EQ(L2.s->v[6], 1);
    life_end(&L2, 0);
    UN(NtUserSetImeHotKey)(0x7ff30001, 0, 0, NULL, 1);
    CHECK(xm_key_free_within(a, XM_NIMBLE_MS));
}
