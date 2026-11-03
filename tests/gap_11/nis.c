/* gap_11 / NtUserNotifyIMEStatus :
 *   EnterCrit; pwnd = ValidateHwnd(hwnd) -> NULL: return FALSE (error 1400 from ValidateHwnd)
 *   if (IME not enabled) { SetLastError(0x78 ERROR_CALL_NOT_IMPLEMENTED); return FALSE; }
 *   else notify the IME status; return TRUE;
 * The notification only changes anything when pwnd is the focus window of the foreground queue
 * (pwnd has focus in the foreground queue) and the (himc, open, conversion) triple differs from the cached one,
 * so a hidden never-activated window is safe to pass; the foreground case lives in dx_gap_11. */
#include "g11.h"

#define NIS_NEED() NtUserNotifyIMEStatus_pfn nis = (NtUserNotifyIMEStatus_pfn)g11_nt("NtUserNotifyIMEStatus"); \
    if (!nis) { t_skip("missing stub"); return; }

/* SERVERINFO.dwSRVIFlags (first dword of gSharedInfo.psi), the bit the kernel tests */
static int nis_ime_flag(void) {
    struct k_user_shared_info si; unsigned long v = 0;
    if (!k_shared_info(&si) || !si.psi || !k_try_read_u32(si.psi, &v)) return -1;
    return (v & 4) != 0;
}

TEST(gap11_nis_server_flag_readable) {
    NIS_NEED(); REQUIRE(U(GetDesktopWindow));
    int f = nis_ime_flag();
    if (f < 0) { t_skip("cannot read SERVERINFO"); return; }
    t_note("SERVERINFO flag 4 (IME enabled) = %d", f);
}

TEST(gap11_nis_hidden_window_follows_server_flag) {
    NIS_NEED(); G11_NEED_WND(w);
    int f = nis_ime_flag();
    if (f < 0) { g11_kill(w); t_skip("cannot read SERVERINFO"); return; }
    SET_ERR(G11_SENT);
    BYTE r = nis(w, 0, 0);
    DWORD e = GetLastError();
    if (f) {
        EXPECT_EQ(r, 1);                                     /* flag 4 set: the notification runs, result TRUE */
    } else {
        EXPECT_EQ(r, 0);
        EXPECT_EQ(e, ERROR_CALL_NOT_IMPLEMENTED);            /* flag 4 clear: UserSetLastError(0x78) */
    }
    g11_kill(w);
}

TEST(gap11_nis_argument_table_on_hidden_window) {
    NIS_NEED(); G11_NEED_WND(w);
    int f = nis_ime_flag();
    if (f < 0) { g11_kill(w); t_skip("cannot read SERVERINFO"); return; }
    static const DWORD open[] = { 0, 1, 2, 0x80000000u, 0xFFFFFFFFu };
    static const DWORD conv[] = { 0, 1, 2, 8, 0x10, 0x20, 0x40, 0x3FF, 0xFFFFFFFFu };
    for (unsigned i = 0; i < sizeof open / sizeof open[0]; i++)
        for (unsigned j = 0; j < sizeof conv / sizeof conv[0]; j++) {
            SET_ERR(G11_SENT);
            BYTE r = nis(w, open[i], conv[j]);
            DWORD e = GetLastError();
            /* the arguments are never validated: the outcome depends on the window and the server flag only */
            CHECKF(r == (f ? 1 : 0), "open %lx conv %lx -> %u", open[i], conv[j], r);
            if (!f) CHECKF(e == ERROR_CALL_NOT_IMPLEMENTED, "open %lx conv %lx err %lu", open[i], conv[j], e);
        }
    g11_kill(w);
}

TEST(gap11_nis_invalid_handles_set_invalid_window_error) {
    NIS_NEED();
    HWND dead = g11_mk(WS_POPUP, 0, NULL); g11_kill(dead);
    HWND bad[] = { NULL, (HWND)(ULONG_PTR)-1, (HWND)(ULONG_PTR)0x7FFE7777, (HWND)(ULONG_PTR)0x0FFFFFF0, dead };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        if (i == 4 && !dead) continue;
        SET_ERR(G11_SENT);
        BYTE r = nis(bad[i], 1, 1);
        DWORD e = GetLastError();
        /* ValidateHwnd fails first, so the answer does not depend on the IME server flag */
        CHECKF(r == 0, "handle %p -> %u", (void *)bad[i], r);
        CHECKF(e == ERROR_INVALID_WINDOW_HANDLE, "handle %p err %lu", (void *)bad[i], e);
    }
}

TEST(gap11_nis_menu_handle_is_not_a_window) {
    NIS_NEED(); REQUIRE(U(CreatePopupMenu));
    HMENU m = U(CreatePopupMenu)();
    if (!m) { t_skip("no menu"); return; }
    SET_ERR(G11_SENT);
    BYTE r = nis((HWND)m, 0, 0);
    DWORD e = GetLastError();
    CHECK(r == 0); EXPECT_EQ(e, ERROR_INVALID_WINDOW_HANDLE);
    U(DestroyMenu)(m);
}

TEST(gap11_nis_child_window_accepted_like_top_level) {
    NIS_NEED(); G11_NEED_WND(p);
    HWND c = g11_mk(WS_CHILD, 0, p);
    int f = nis_ime_flag();
    if (!c || f < 0) { g11_kill(p); t_skip("no child window or SERVERINFO"); return; }
    BYTE r = nis(c, 0, 0);
    EXPECT_EQ(r, f ? 1 : 0);                                 /* no top-level requirement in this entry point */
    g11_kill(p);
}

TEST(gap11_nis_window_of_other_thread) {
    NIS_NEED();
    struct g11_peer p; int f = nis_ime_flag();
    if (f < 0 || !g11_peer_start(&p)) { if (f >= 0) g11_peer_stop(&p); t_skip("no peer window or SERVERINFO"); return; }
    BYTE r = nis(p.hwnd, 0, 0);                              /* the entry point takes no ownership/thread test */
    EXPECT_EQ(r, f ? 1 : 0);
    g11_peer_stop(&p);
}

TEST(gap11_nis_repeated_calls_idempotent_result) {
    NIS_NEED(); G11_NEED_WND(w);
    int f = nis_ime_flag();
    if (f < 0) { g11_kill(w); t_skip("cannot read SERVERINFO"); return; }
    for (int i = 0; i < 50; i++) CHECK(nis(w, i & 1, i) == (f ? 1 : 0));
    g11_kill(w);
}

TEST(gap11_nis_destroyed_window_error) {
    NIS_NEED(); G11_NEED_WND(w);
    g11_kill(w);
    SET_ERR(G11_SENT);
    CHECK(nis(w, 0, 0) == 0); EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
}
