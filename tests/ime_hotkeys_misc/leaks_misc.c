/* Resource-balance checks over the area's create/destroy syscalls: USER object and GDI object
 * counts (GetGuiResources, kernel-maintained per-process counters) and hotkey bookkeeping must
 * return to their starting values. */
#include "imhm.h"

#define SENT 0x7e57u
UDECL(DWORD, GetGuiResources, (HANDLE, DWORD));

static DWORD user_objs(void) {
    return U(GetGuiResources) ? U(GetGuiResources)(GetCurrentProcess(), 1 /* GR_USEROBJECTS */) : 0;
}

TEST(ime_hotkeys_misc_bal_hotkey_many_ids_roundtrip) {
    NEED_HK();
    enum { N = 30 };
    struct hk k[N];
    for (int i = 0; i < N; i++) k[i] = hk_next();
    for (int i = 0; i < N; i++) CHECKF(RHK(NULL, 300 + i, k[i].mods, k[i].vk), "register %d err %lu", i, GetLastError());
    for (int i = 0; i < N; i++) {
        /* same key under another id clashes while the first registration lives (FindHotKey on mods&0xf, vk) */
        SET_ERR(SENT);
        EXPECT_FAIL(RHK(NULL, 400 + i, k[i].mods, k[i].vk) == 0, ERROR_HOTKEY_ALREADY_REGISTERED);
    }
    for (int i = 0; i < N; i++) CHECK(UHK(NULL, 300 + i));
    for (int i = 0; i < N; i++) {
        SET_ERR(SENT);
        EXPECT_FAIL(UHK(NULL, 300 + i) == 0, ERROR_HOTKEY_NOT_REGISTERED);  /* nothing left to remove */
    }
    for (int i = 0; i < N; i++) { CHECK(RHK(NULL, 500, k[i].mods, k[i].vk)); CHECK(UHK(NULL, 500)); }  /* every key is free again */
}
TEST(ime_hotkeys_misc_bal_hotkey_window_loop_keeps_user_objects) {
    NEED_HK();
    HWND probe = im_mkpopup();
    if (!probe) { t_skip("cannot create a window"); return; }
    DWORD before = user_objs();
    for (int i = 0; i < 20; i++) {
        HK_DECL(k);
        HWND w = im_mkpopup();
        if (!w) break;
        CHECK(RHK(w, 1, k.mods, k.vk));
        im_kill(w);                                /* window destruction removes the window's hotkeys */
        CHECK(RHK(NULL, 2, k.mods, k.vk));         /* ... so the key is free for the thread queue */
        UHK(NULL, 2);
    }
    EXPECT_EQ(user_objs(), before);
    im_kill(probe);
}
TEST(ime_hotkeys_misc_bal_imc_create_destroy_keeps_user_objects) {
    if (!IM(ImmCreateContext) || !IM(ImmDestroyContext)) { t_skip("missing imm32 export"); return; }
    NEED_IME();
    DWORD before = user_objs();
    HIMC h[16];
    int n = 0;
    for (; n < 16; n++) { h[n] = IM(ImmCreateContext)(); if (!h[n]) break; }
    if (!n) { t_skip("no input context"); return; }
    for (int i = 0; i < n; i++) CHECK(IM(ImmDestroyContext)(h[i]));
    EXPECT_EQ(user_objs(), before);   /* input contexts are USER handle-table objects (type 0x11) */
}
TEST(ime_hotkeys_misc_bal_shellhook_register_loop_keeps_user_objects) {
    if (!U(RegisterShellHookWindow) || !U(DeregisterShellHookWindow)) { t_skip("missing export"); return; }
    HWND w = im_mkpopup();
    if (!w) { t_skip("cannot create a window"); return; }
    DWORD before = user_objs();
    for (int i = 0; i < 30; i++) { U(RegisterShellHookWindow)(w); U(DeregisterShellHookWindow)(w); }
    EXPECT_EQ(user_objs(), before);
    im_kill(w);
}
TEST(ime_hotkeys_misc_bal_system_menu_revert_keeps_user_objects) {
    if (!UN(NtUserGetSystemMenu)) { t_skip("missing stub"); return; }
    HWND w = im_mkwnd(WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME, 0, NULL);
    if (!w) { t_skip("cannot create a window"); return; }
    HMENU first = UN(NtUserGetSystemMenu)(w, FALSE);
    if (!first) { im_kill(w); t_skip("no system menu"); return; }
    UN(NtUserGetSystemMenu)(w, TRUE);     /* revert: destroys the private copy */
    DWORD before = user_objs();
    for (int i = 0; i < 10; i++) { UN(NtUserGetSystemMenu)(w, FALSE); UN(NtUserGetSystemMenu)(w, TRUE); }
    EXPECT_EQ(user_objs(), before);
    im_kill(w);
}
TEST(ime_hotkeys_misc_bal_layered_set_loop_keeps_user_objects) {
    if (!UN(NtUserSetLayeredWindowAttributes)) { t_skip("missing stub"); return; }
    HWND w = im_mkwnd(WS_POPUP, WS_EX_LAYERED, NULL);
    if (!w) { t_skip("cannot create a window"); return; }
    DWORD before = user_objs();
    for (int i = 0; i < 50; i++) CHECK(UN(NtUserSetLayeredWindowAttributes)(w, 0, (BYTE)i, 2 /* LWA_ALPHA */));
    EXPECT_EQ(user_objs(), before);
    im_kill(w);
}
