/* x_menu_accel_hotkey_lifetime / rtol: NtUserSetMenuFlagRtoL(HMENU).
 *
 * The service validates the handle as a menu of the caller's desktop and sets a right-to-left flag in it; TRUE for a valid menu,
 * otherwise FALSE with ERROR_INVALID_MENU_HANDLE (1401) and no other effect. Which thread or process made the menu does not matter
 * (there is no owner check); a menu is destroyed with its process, with the window it is the menu bar of, or with the menu it is
 * a submenu of, and a destroyed menu is invalid; a menu of another desktop is refused even though IsMenu still answers TRUE for
 * it. The handle follows the common USER rules: a 0xFFFF high word is a wildcard, a zero or wrong high word is invalid, the upper
 * half of the value is ignored. Handles of other types are refused.
 *
 * Determinism: results only, no handle values printed.
 */
#include "x.h"

#define RTL(m) UN(NtUserSetMenuFlagRtoL)(m)

XT(popup_menu_made_here_is_accepted_repeatedly) {
    NEED(NtUserSetMenuFlagRtoL);
    HMENU m = U(CreatePopupMenu)();
    for (int i = 0; i < 5; i++) XOK(RTL(m), 1);
    U(DestroyMenu)(m);
}
XT(accepting_the_flag_leaves_the_items_alone) {
    NEED(NtUserSetMenuFlagRtoL);
    HMENU m = U(CreatePopupMenu)();
    U(AppendMenuA)(m, MF_STRING, 5, "five"); U(AppendMenuA)(m, MF_SEPARATOR, 0, NULL); U(AppendMenuA)(m, MF_STRING | MF_CHECKED, 6, "six");
    XOK(RTL(m), 1);
    EXPECT_EQ(U(GetMenuItemCount)(m), 3);
    EXPECT_EQ(U(GetMenuItemID)(m, 0), 5);
    EXPECT_EQ(U(GetMenuState)(m, 6, MF_BYCOMMAND) & MF_CHECKED, MF_CHECKED);
    char b[16]; EXPECT_EQ(U(GetMenuStringA)(m, 5, b, sizeof b, MF_BYCOMMAND), 4);
    U(DestroyMenu)(m);
}
XT(menu_bar_is_accepted) {
    NEED(NtUserSetMenuFlagRtoL);
    HMENU m = U(CreateMenu)();
    XOK(RTL(m), 1);
    U(DestroyMenu)(m);
}
XT(empty_menu_is_accepted) {
    NEED(NtUserSetMenuFlagRtoL);
    HMENU m = U(CreatePopupMenu)();
    EXPECT_EQ(U(GetMenuItemCount)(m), 0);
    XOK(RTL(m), 1);
    U(DestroyMenu)(m);
}
XT(many_menus_all_accepted) {
    NEED(NtUserSetMenuFlagRtoL);
    HMENU m[200]; int bad = 0;
    for (int i = 0; i < 200; i++) m[i] = (i & 1) ? U(CreateMenu)() : U(CreatePopupMenu)();
    for (int i = 0; i < 200; i++) if (!m[i] || RTL(m[i]) != 1) bad++;
    EXPECT_EQ(bad, 0);
    for (int i = 0; i < 200; i++) if (m[i]) U(DestroyMenu)(m[i]);
}

/* ---- handle forms ---- */
XT(wildcard_high_word_is_accepted) {
    NEED(NtUserSetMenuFlagRtoL);
    HMENU m = U(CreatePopupMenu)();
    XOK(RTL((HMENU)(((ULONG_PTR)m & 0xFFFF) | 0xFFFF0000u)), 1);
    U(DestroyMenu)(m);
}
XT(zero_high_word_is_refused) {
    NEED(NtUserSetMenuFlagRtoL);
    HMENU m = U(CreatePopupMenu)();
    XFAIL(RTL((HMENU)((ULONG_PTR)m & 0xFFFF)), XE_INVALID_MENU_HANDLE);
    CHECK(U(IsMenu)(m));
    U(DestroyMenu)(m);
}
XT(wrong_high_word_is_refused) {
    NEED(NtUserSetMenuFlagRtoL);
    HMENU m = U(CreatePopupMenu)();
    for (ULONG_PTR x = 1; x <= 0x8000; x <<= 1) {
        ULONG_PTR f = (ULONG_PTR)m ^ (x << 16);
        if ((f >> 16 & 0xFFFF) == 0xFFFF || (f >> 16 & 0xFFFF) == 0) continue;
        SET_ERR(XE_SENT);
        BOOL r = RTL((HMENU)f);
        CHECKF(!r && GetLastError() == XE_INVALID_MENU_HANDLE, "flip 0x%llx: ret %d err %lu", (unsigned long long)x, r, GetLastError());
    }
    U(DestroyMenu)(m);
}
XT(upper_half_of_the_value_is_ignored) {
    NEED(NtUserSetMenuFlagRtoL);
    HMENU m = U(CreatePopupMenu)();
    XOK(RTL((HMENU)((ULONG_PTR)m | 0x100000000ULL)), 1);
    XOK(RTL((HMENU)((ULONG_PTR)m | 0xFFFFFFFF00000000ULL)), 1);
    U(DestroyMenu)(m);
}
XT(non_handles_are_refused) {
    NEED(NtUserSetMenuFlagRtoL);
    ULONG_PTR bad[] = { 0, 1, 2, 0x1234, 0xFFFF, 0x10000, 0x7FFE7777, 0xDEADBEEF, 0xFFFFFFFF, 0xFFFFFFFFFFFFFFFFULL };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SET_ERR(XE_SENT);
        BOOL r = RTL((HMENU)bad[i]);
        CHECKF(!r && GetLastError() == XE_INVALID_MENU_HANDLE, "value 0x%llx: ret %d err %lu", (unsigned long long)bad[i], r, GetLastError());
    }
}
XT(handles_of_other_types_are_refused) {
    NEED(NtUserSetMenuFlagRtoL); NEED_ACC();
    HWND w = k_make_window("xm");
    HANDLE acc = xm_acc_make(1);
    HICON ic = U(CreateIcon)(GetModuleHandleA(NULL), 1, 1, 1, 1, (BYTE[]){0xFF}, (BYTE[]){0x00});
    HANDLE objs[] = { w, acc, ic, GetCurrentProcess() };
    const char *names[] = { "window", "accelerator table", "icon", "pseudo process" };
    for (unsigned i = 0; i < 4; i++) {
        if (!objs[i]) continue;
        SET_ERR(XE_SENT);
        BOOL r = RTL((HMENU)objs[i]);
        CHECKF(!r && GetLastError() == XE_INVALID_MENU_HANDLE, "%s: ret %d err %lu", names[i], r, GetLastError());
    }
    U(DestroyWindow)(w); UN(NtUserDestroyAcceleratorTable)(acc); if (ic) U(DestroyIcon)(ic);
}

/* ---- destruction ---- */
XT(destroyed_menu_is_refused) {
    NEED(NtUserSetMenuFlagRtoL);
    HMENU m = U(CreatePopupMenu)();
    U(DestroyMenu)(m);
    XFAIL(RTL(m), XE_INVALID_MENU_HANDLE);
    CHECK(!U(IsMenu)(m));
}
XT(menu_bar_dies_with_its_window) {
    NEED(NtUserSetMenuFlagRtoL);
    HMENU m = U(CreateMenu)(); U(AppendMenuA)(m, MF_STRING, 1, "x");
    HWND w = k_make_window("xm");
    CHECK(U(SetMenu)(w, m));
    XOK(RTL(m), 1);
    U(DestroyWindow)(w);
    XFAIL(RTL(m), XE_INVALID_MENU_HANDLE);
    CHECK(!U(IsMenu)(m));
}
XT(menu_bar_removed_from_its_window_survives_it) {
    NEED(NtUserSetMenuFlagRtoL);
    HMENU m = U(CreateMenu)(); U(AppendMenuA)(m, MF_STRING, 1, "x");
    HWND w = k_make_window("xm");
    U(SetMenu)(w, m);
    U(SetMenu)(w, NULL);
    U(DestroyWindow)(w);
    XOK(RTL(m), 1);
    U(DestroyMenu)(m);
}
XT(submenu_dies_with_its_parent) {
    NEED(NtUserSetMenuFlagRtoL);
    HMENU p = U(CreatePopupMenu)(), s = U(CreatePopupMenu)();
    U(AppendMenuA)(p, MF_POPUP, (UINT_PTR)s, "sub");
    XOK(RTL(s), 1);
    U(DestroyMenu)(p);
    XFAIL(RTL(s), XE_INVALID_MENU_HANDLE);
}
XT(submenu_removed_from_its_parent_survives_it) {
    NEED(NtUserSetMenuFlagRtoL);
    HMENU p = U(CreatePopupMenu)(), s = U(CreatePopupMenu)();
    U(AppendMenuA)(p, MF_POPUP, (UINT_PTR)s, "sub");
    CHECK(U(RemoveMenu)(p, 0, MF_BYPOSITION));
    U(DestroyMenu)(p);
    XOK(RTL(s), 1);
    U(DestroyMenu)(s);
}
XT(submenu_deleted_from_its_parent_is_destroyed) {
    NEED(NtUserSetMenuFlagRtoL);
    HMENU p = U(CreatePopupMenu)(), s = U(CreatePopupMenu)();
    U(AppendMenuA)(p, MF_POPUP, (UINT_PTR)s, "sub");
    CHECK(U(DeleteMenu)(p, 0, MF_BYPOSITION));
    XFAIL(RTL(s), XE_INVALID_MENU_HANDLE);
    U(DestroyMenu)(p);
}
XT(whole_chain_of_nested_menus_is_accepted_then_dies_together) {
    NEED(NtUserSetMenuFlagRtoL);
    HMENU m[5]; for (int i = 0; i < 5; i++) m[i] = U(CreatePopupMenu)();
    for (int i = 0; i < 4; i++) U(AppendMenuA)(m[i], MF_POPUP, (UINT_PTR)m[i + 1], "n");
    for (int i = 0; i < 5; i++) XOK(RTL(m[i]), 1);
    U(DestroyMenu)(m[0]);
    int dead = 0; for (int i = 0; i < 5; i++) if (!RTL(m[i])) dead++;
    EXPECT_EQ(dead, 5);
}
XT(system_menu_copy_is_accepted_and_reverting_destroys_it) {
    NEED(NtUserSetMenuFlagRtoL);
    HWND w = U(CreateWindowExW)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"x", WS_POPUP | WS_CAPTION | WS_SYSMENU, -32000, -32000, 60, 60, NULL, NULL, GetModuleHandleA(NULL), NULL);
    if (!w) { t_skip("no window with a system menu"); return; }
    HMENU sm = U(GetSystemMenu)(w, FALSE);
    CHECK(sm != NULL);
    if (sm) XOK(RTL(sm), 1);
    U(GetSystemMenu)(w, TRUE);                 /* revert: the private copy is destroyed */
    if (sm) { XFAIL(RTL(sm), XE_INVALID_MENU_HANDLE); }
    U(DestroyWindow)(w);
}

/* ---- threads ---- */
static DWORD WINAPI rtl_thread(LPVOID p) { HMENU *a = p; t_module("user32.dll"); SET_ERR(XE_SENT); a[1] = (HMENU)(ULONG_PTR)RTL(a[0]); a[2] = (HMENU)(ULONG_PTR)GetLastError(); return 0; }
static DWORD WINAPI make_menu_thread(LPVOID p) { HMENU *a = p; a[0] = U(CreatePopupMenu)(); a[1] = U(CreateMenu)(); return 0; }
XT(menu_made_here_is_accepted_on_another_thread) {
    NEED(NtUserSetMenuFlagRtoL);
    HMENU a[3] = { U(CreatePopupMenu)(), 0, 0 };
    HANDLE t = CreateThread(NULL, 0, rtl_thread, a, 0, NULL); WaitForSingleObject(t, 10000); CloseHandle(t);
    EXPECT_EQ((ULONG_PTR)a[1], 1); EXPECT_EQ((ULONG_PTR)a[2], XE_SENT);
    U(DestroyMenu)(a[0]);
}
XT(menu_outlives_the_thread_that_made_it) {
    NEED(NtUserSetMenuFlagRtoL);
    HMENU a[2] = {0};
    HANDLE t = CreateThread(NULL, 0, make_menu_thread, a, 0, NULL); WaitForSingleObject(t, 10000); CloseHandle(t);
    CHECK(a[0] && a[1]); if (!a[0] || !a[1]) return;
    CHECK(U(IsMenu)(a[0])); CHECK(U(IsMenu)(a[1]));
    XOK(RTL(a[0]), 1); XOK(RTL(a[1]), 1);
    U(DestroyMenu)(a[0]); U(DestroyMenu)(a[1]);
}
XT(menu_made_on_a_thread_can_be_destroyed_here) {
    NEED(NtUserSetMenuFlagRtoL);
    HMENU a[2] = {0};
    HANDLE t = CreateThread(NULL, 0, make_menu_thread, a, 0, NULL); WaitForSingleObject(t, 10000); CloseHandle(t);
    CHECK(U(DestroyMenu)(a[0]));
    XFAIL(RTL(a[0]), XE_INVALID_MENU_HANDLE);
    U(DestroyMenu)(a[1]);
}

/* ---- processes ---- */
CHILD(xm_rtl_owner) {
    HANDLE m; struct xm_shm *s = xm_shm_open(argv[1], &m); if (!s) return 1;
    t_module("user32.dll");
    HMENU a = U(CreatePopupMenu)(), b = U(CreateMenu)();
    HMENU sub = U(CreatePopupMenu)(); U(AppendMenuA)(a, MF_POPUP, (UINT_PTR)sub, "s");
    s->v[0] = (LONG64)(ULONG_PTR)a; s->v[1] = (LONG64)(ULONG_PTR)b; s->v[2] = (LONG64)(ULONG_PTR)sub;
    InterlockedExchange(&s->ready, 1);
    xm_wait(&s->go, 20000);
    s->e[0] = U(IsMenu)(a); s->e[1] = U(IsMenu)(b);
    InterlockedExchange(&s->done, 1);
    xm_wait(&s->go, 1);
    return 0;
}
static HANDLE rtl_owner(struct xm_shm **s, HANDLE *sm, PROCESS_INFORMATION *pi) {
    const char *tag = xm_tag();
    *s = xm_shm_create(tag, sm);
    if (!*s) return NULL;
    HANDLE p = xm_spawn("xm_rtl_owner", tag, "", pi);
    if (!p) return NULL;
    if (!xm_wait(&(*s)->ready, 20000)) { TerminateProcess(p, 1); xm_reap(p, pi, 5000); return NULL; }
    return p;
}
XT(menu_of_another_process_is_accepted) {
    NEED(NtUserSetMenuFlagRtoL);
    struct xm_shm *s; HANDLE sm; PROCESS_INFORMATION pi;
    HANDLE p = rtl_owner(&s, &sm, &pi);
    if (!p) { CHECK(0 && "child did not start"); return; }
    XOK(RTL((HMENU)(ULONG_PTR)s->v[0]), 1);
    XOK(RTL((HMENU)(ULONG_PTR)s->v[1]), 1);
    XOK(RTL((HMENU)(ULONG_PTR)s->v[2]), 1);   /* the submenu too */
    CHECK(U(IsMenu)((HMENU)(ULONG_PTR)s->v[0]));
    InterlockedExchange(&s->go, 1); xm_wait(&s->done, 10000);
    EXPECT_EQ(s->e[0], 1); EXPECT_EQ(s->e[1], 1);   /* the owner still has its menus */
    EXPECT_EQ(xm_reap(p, &pi, 15000), 0);
}
XT(menus_die_with_their_process) {
    NEED(NtUserSetMenuFlagRtoL);
    struct xm_shm *s; HANDLE sm; PROCESS_INFORMATION pi;
    HANDLE p = rtl_owner(&s, &sm, &pi);
    if (!p) { CHECK(0 && "child did not start"); return; }
    HMENU h[3] = { (HMENU)(ULONG_PTR)s->v[0], (HMENU)(ULONG_PTR)s->v[1], (HMENU)(ULONG_PTR)s->v[2] };
    for (int i = 0; i < 3; i++) EXPECT_EQ(RTL(h[i]), 1);
    InterlockedExchange(&s->go, 1); xm_wait(&s->done, 10000);
    EXPECT_EQ(xm_reap(p, &pi, 15000), 0);
    for (int i = 0; i < 3; i++) { XFAIL(RTL(h[i]), XE_INVALID_MENU_HANDLE); CHECK(!U(IsMenu)(h[i])); }
}
XT(menus_die_with_their_process_when_it_is_terminated) {
    NEED(NtUserSetMenuFlagRtoL);
    struct xm_shm *s; HANDLE sm; PROCESS_INFORMATION pi;
    HANDLE p = rtl_owner(&s, &sm, &pi);
    if (!p) { CHECK(0 && "child did not start"); return; }
    HMENU h[3] = { (HMENU)(ULONG_PTR)s->v[0], (HMENU)(ULONG_PTR)s->v[1], (HMENU)(ULONG_PTR)s->v[2] };
    TerminateProcess(p, 4);
    EXPECT_EQ(xm_reap(p, &pi, 15000), 4);
    for (int i = 0; i < 3; i++) XFAIL(RTL(h[i]), XE_INVALID_MENU_HANDLE);
}

/* ---- another desktop ---- */
struct dctx { HDESK d; HMENU m; HWND w; };
static DWORD WINAPI desk_thread(LPVOID p) {
    struct dctx *c = p;
    t_module("user32.dll");
    if (!U(SetThreadDesktop)(c->d)) return 1;
    c->m = U(CreatePopupMenu)();
    return 0;
}
XT(menu_of_another_desktop_is_refused_though_it_is_a_menu) {
    NEED(NtUserSetMenuFlagRtoL);
    HDESK d = U(CreateDesktopW)(L"xm_rtl_desk", NULL, NULL, 0, GENERIC_ALL, NULL);
    if (!d) { t_skip("cannot create a desktop (error %lu)", GetLastError()); return; }
    struct dctx c = { d, NULL, NULL };
    HANDLE t = CreateThread(NULL, 0, desk_thread, &c, 0, NULL); WaitForSingleObject(t, 10000);
    DWORD ec = 1; GetExitCodeThread(t, &ec); CloseHandle(t);
    if (ec == 0 && c.m) {
        CHECK(U(IsMenu)(c.m));   /* IsMenu does not look at the desktop */
        XFAIL(RTL(c.m), XE_INVALID_MENU_HANDLE);
        HMENU own = U(CreatePopupMenu)();
        XOK(RTL(own), 1);       /* the caller's own desktop is unaffected */
        U(DestroyMenu)(own);
    } else t_skip("worker could not use the new desktop");
    U(CloseDesktop)(d);
}
