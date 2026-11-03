/* NtUserCallMsgFilter(MSG*, nCode): win32kfull copies the 0x30-byte MSG in, rejects message ids >= 0x20000
 * (error 0x57, return 0), skips WM_TOUCH-class ids (0x240, 0x119, 0x11b), then calls the WH_SYSMSGFILTER
 * chain and finally the thread's WH_MSGFILTER chain (the hook call with nCode and the message); the result is the
 * hook chain's return value. */
#include "mx.h"

static volatile LONG g_calls, g_calls_b;
static volatile int g_last_code, g_order[8], g_order_n;
static volatile WPARAM g_last_wparam;
static MSG g_last_msg;
static LRESULT g_ret_a, g_ret_b;
static int g_chain_a, g_modify;

static LRESULT CALLBACK hook_a(int code, WPARAM wp, LPARAM lp) {
    g_calls++; g_last_code = code; g_last_wparam = wp;
    if (lp) { g_last_msg = *(MSG *)lp; if (g_modify) ((MSG *)lp)->wParam = 0xAA55; }
    if (g_order_n < 8) g_order[g_order_n++] = 'A';
    if (g_chain_a) { LRESULT r = U(CallNextHookEx)(NULL, code, wp, lp); return r ? r : g_ret_a; }
    return g_ret_a;
}
static LRESULT CALLBACK hook_b(int code, WPARAM wp, LPARAM lp) {
    g_calls_b++;
    if (g_order_n < 8) g_order[g_order_n++] = 'B';
    return g_ret_b;
}
static void reset(void) { g_calls = g_calls_b = 0; g_order_n = 0; g_ret_a = g_ret_b = 0; g_chain_a = g_modify = 0; g_last_code = -999; g_last_wparam = 0; memset(&g_last_msg, 0, sizeof g_last_msg); }

static int call(MSG *m, int code) { return (int)(INT_PTR)UN(NtUserCallMsgFilter)(m, code); }

static MSG mk(UINT message) {
    MSG m; memset(&m, 0, sizeof m);
    m.hwnd = (HWND)(ULONG_PTR)0x1234; m.message = message; m.wParam = 0x1111; m.lParam = 0x2222; m.time = 0x3333; m.pt.x = 44; m.pt.y = 55;
    return m;
}

T(msgfilter_no_hooks_returns_zero_for_valid_messages) {
    NEED_RAW(NtUserCallMsgFilter);
    static const UINT msgs[] = { WM_NULL, WM_KEYDOWN, WM_USER, 0xC001, 0xFFFF, 0x1FFFF, 0x240, 0x119, 0x11b };
    for (size_t i = 0; i < sizeof msgs / sizeof msgs[0]; i++) {
        MSG m = mk(msgs[i]), orig = m;
        SetLastError(SENTINEL_ERR);
        CHECKF(call(&m, 0) == 0, "message 0x%x", msgs[i]);
        CHECKF(GetLastError() == SENTINEL_ERR, "message 0x%x changed last error to %lu", msgs[i], GetLastError());
        CHECKF(!memcmp(&m, &orig, sizeof m), "message 0x%x: MSG modified by an unhooked call", msgs[i]);
    }
}
T(msgfilter_message_id_limit_is_0x20000) {
    NEED_RAW(NtUserCallMsgFilter);
    static const UINT bad[] = { 0x20000, 0x20001, 0x40000, 0x80000000u, 0xFFFFFFFFu, 0x1000000 };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        MSG m = mk(bad[i]), orig = m;
        SetLastError(SENTINEL_ERR);
        CHECKF(call(&m, 0) == 0, "message 0x%x", bad[i]);
        CHECKF(GetLastError() == ERR_INVALID_PARAMETER, "message 0x%x err %lu want 87", bad[i], GetLastError());
        CHECKF(!memcmp(&m, &orig, sizeof m), "message 0x%x: MSG written on the error path", bad[i]);
    }
}
T(msgfilter_message_padding_dword_is_ignored_by_the_limit) {
    NEED_RAW(NtUserCallMsgFilter);
    /* the limit tests the 64-bit word at +8 against 0xfffe0000 (a 32-bit constant), so the four padding bytes
     * after MSG.message never make a message invalid and are preserved by the copy-back */
    MSG m = mk(WM_NULL);
    unsigned char *raw = (unsigned char *)&m;
    raw[12] = 0x11; raw[13] = 0x22; raw[14] = 0x33; raw[15] = 0x44;
    MSG orig = m;
    SetLastError(SENTINEL_ERR);
    call(&m, 0);
    CHECKF(GetLastError() == SENTINEL_ERR, "padding made the message invalid (err %lu)", GetLastError());
    CHECK(!memcmp(&m, &orig, sizeof m));
}

T(msgfilter_thread_hook_receives_code_and_message_copy) {
    NEED_RAW(NtUserCallMsgFilter);
    reset();
    HHOOK h = U(SetWindowsHookExW)(WH_MSGFILTER, hook_a, NULL, GetCurrentThreadId());
    if (!h) { t_skip("SetWindowsHookEx(WH_MSGFILTER) failed %lu", GetLastError()); return; }
    static const int codes[] = { MSGF_DIALOGBOX, MSGF_MENU, MSGF_SCROLLBAR, MSGF_NEXTWINDOW, 0x8001, 0x1234 };
    for (size_t i = 0; i < sizeof codes / sizeof codes[0]; i++) {
        reset();
        MSG m = mk(WM_KEYDOWN);
        CHECKF(call(&m, codes[i]) == 0, "hook returning 0 -> result 0 (code %d)", codes[i]);
        CHECKF(g_calls == 1, "code %d: hook called %ld times", codes[i], (long)g_calls);
        CHECKF(g_last_code == codes[i], "hook saw nCode %d want %d", g_last_code, codes[i]);
        CHECKF(g_last_wparam == 0, "hook wParam 0x%llx (the dispatcher passes 0)", (unsigned long long)g_last_wparam);
        CHECK(g_last_msg.hwnd == m.hwnd && g_last_msg.message == WM_KEYDOWN && g_last_msg.wParam == 0x1111 && g_last_msg.lParam == 0x2222);
        CHECK(g_last_msg.time == 0x3333 && g_last_msg.pt.x == 44 && g_last_msg.pt.y == 55);
    }
    CHECK(U(UnhookWindowsHookEx)(h));
}
T(msgfilter_hook_result_is_returned) {
    NEED_RAW(NtUserCallMsgFilter);
    reset();
    HHOOK h = U(SetWindowsHookExW)(WH_MSGFILTER, hook_a, NULL, GetCurrentThreadId());
    if (!h) { t_skip("SetWindowsHookEx failed %lu", GetLastError()); return; }
    static const LRESULT rets[] = { 0, 1, 7 };
    for (size_t i = 0; i < sizeof rets / sizeof rets[0]; i++) {
        reset(); g_ret_a = rets[i];
        MSG m = mk(WM_USER);
        int r = call(&m, MSGF_DIALOGBOX);
        CHECKF((r != 0) == (rets[i] != 0), "hook returned %lld, NtUserCallMsgFilter %d", (long long)rets[i], r);
        CHECK(g_calls == 1);
    }
    CHECK(U(UnhookWindowsHookEx)(h));
}
T(msgfilter_touch_gesture_messages_bypass_hooks) {
    NEED_RAW(NtUserCallMsgFilter);
    reset();
    HHOOK h = U(SetWindowsHookExW)(WH_MSGFILTER, hook_a, NULL, GetCurrentThreadId());
    if (!h) { t_skip("SetWindowsHookEx failed %lu", GetLastError()); return; }
    g_ret_a = 1;
    static const UINT msgs[] = { 0x240, 0x119, 0x11b };
    for (size_t i = 0; i < sizeof msgs / sizeof msgs[0]; i++) {
        MSG m = mk(msgs[i]);
        CHECKF(call(&m, 0) == 0, "message 0x%x", msgs[i]);
    }
    CHECKF(g_calls == 0, "hook called %ld times for touch/gesture ids", (long)g_calls);
    MSG m = mk(0x11a);                                     /* neighbour of 0x119 / 0x11b: filtered normally */
    CHECK(call(&m, 0) != 0);
    CHECK(g_calls == 1);
    CHECK(U(UnhookWindowsHookEx)(h));
}
T(msgfilter_hook_chain_newest_first_and_chaining) {
    NEED_RAW(NtUserCallMsgFilter);
    reset();
    HHOOK hb = U(SetWindowsHookExW)(WH_MSGFILTER, hook_b, NULL, GetCurrentThreadId());
    HHOOK ha = U(SetWindowsHookExW)(WH_MSGFILTER, hook_a, NULL, GetCurrentThreadId());
    if (!ha || !hb) { t_skip("SetWindowsHookEx failed %lu", GetLastError()); if (ha) U(UnhookWindowsHookEx)(ha); if (hb) U(UnhookWindowsHookEx)(hb); return; }
    g_ret_b = 3;
    g_chain_a = 0;
    MSG m = mk(WM_USER);
    int r = call(&m, 0);
    CHECK(r == 0);                                         /* A (installed last) runs first and does not chain */
    CHECK(g_calls == 1 && g_calls_b == 0);
    reset(); g_ret_b = 3; g_chain_a = 1;
    r = call(&m, 0);
    CHECKF(r != 0, "chained result %d", r);
    CHECKF(g_calls == 1 && g_calls_b == 1, "calls A=%ld B=%ld", (long)g_calls, (long)g_calls_b);
    CHECKF(g_order_n == 2 && g_order[0] == 'A' && g_order[1] == 'B', "order %c%c", g_order[0], g_order[1]);
    U(UnhookWindowsHookEx)(ha);
    reset(); g_ret_b = 5;
    r = call(&m, 0);
    CHECK(r != 0 && g_calls == 0 && g_calls_b == 1);       /* only B remains */
    U(UnhookWindowsHookEx)(hb);
    reset();
    CHECK(call(&m, 0) == 0 && g_calls == 0 && g_calls_b == 0);
}
T(msgfilter_hook_modification_is_recorded) {
    NEED_RAW(NtUserCallMsgFilter);
    reset();
    HHOOK h = U(SetWindowsHookExW)(WH_MSGFILTER, hook_a, NULL, GetCurrentThreadId());
    if (!h) { t_skip("SetWindowsHookEx failed %lu", GetLastError()); return; }
    g_modify = 1;
    MSG m = mk(WM_USER);
    call(&m, 0);
    t_note("hook wrote wParam=0xAA55 into the MSG; caller buffer afterwards has wParam=0x%llx (copy-back is %s)",
           (unsigned long long)m.wParam, m.wParam == 0xAA55 ? "visible" : "not visible");
    CHECK(m.hwnd == (HWND)(ULONG_PTR)0x1234 && m.message == WM_USER);   /* untouched fields survive either way */
    U(UnhookWindowsHookEx)(h);
}
T(msgfilter_public_wrapper_reaches_the_same_hook) {
    NEED_RAW(NtUserCallMsgFilter);
    reset();
    HHOOK h = U(SetWindowsHookExW)(WH_MSGFILTER, hook_a, NULL, GetCurrentThreadId());
    if (!h) { t_skip("SetWindowsHookEx failed %lu", GetLastError()); return; }
    g_ret_a = 1;
    MSG m = mk(WM_KEYUP);
    CHECK(U(CallMsgFilterW)(&m, MSGF_MENU) != 0);
    CHECK(g_calls == 1 && g_last_code == MSGF_MENU);
    CHECK(U(CallMsgFilterA)(&m, MSGF_DIALOGBOX) != 0);
    CHECK(g_calls == 2 && g_last_code == MSGF_DIALOGBOX);
    MSG t = mk(0x240);
    CHECK(U(CallMsgFilterW)(&t, 0) == 0);                  /* filtered out by user32 before the syscall */
    CHECK(g_calls == 2);
    U(UnhookWindowsHookEx)(h);
}
T(msgfilter_hook_installed_on_other_thread_is_not_called) {
    NEED_RAW(NtUserCallMsgFilter);
    struct mx_bg bg;
    if (!mx_bg_start(&bg, 0)) { t_skip("bg"); mx_bg_stop(&bg); return; }
    reset();
    HHOOK h = U(SetWindowsHookExW)(WH_MSGFILTER, hook_a, NULL, bg.tid);
    if (!h) { t_skip("SetWindowsHookEx failed %lu", GetLastError()); mx_bg_stop(&bg); return; }
    g_ret_a = 1;
    MSG m = mk(WM_USER);
    CHECK(call(&m, 0) == 0);
    CHECK(g_calls == 0);
    U(UnhookWindowsHookEx)(h);
    mx_bg_stop(&bg);
}
T(msgfilter_sysmsgfilter_is_global_only) {
    NEED_RAW(NtUserCallMsgFilter);
    SetLastError(SENTINEL_ERR);
    HHOOK h = U(SetWindowsHookExW)(WH_SYSMSGFILTER, hook_a, NULL, GetCurrentThreadId());
    CHECK(h == NULL);
    EXPECT_ERR(1429);                                       /* ERROR_GLOBAL_ONLY_HOOK */
    if (h) U(UnhookWindowsHookEx)(h);
}
