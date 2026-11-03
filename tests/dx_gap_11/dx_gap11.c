/* dx_gap_11 (run only with /destructive): gap_11 checks that change global state - the clipboard contents,
 * the foreground/focus window, and window display affinity (composition state). Each test restores
 * what it can; the clipboard is emptied by design (EmptyClipboard is what raises WM_CLIPBOARDUPDATE
 * for a listener), so run this area only on a VM whose clipboard does not matter. */
#include "g11.h"

static UINT dx_fmt(void) { return U(RegisterClipboardFormatW)(L"win32kt_gap11_fmt"); }

/* Puts one private-format item on the clipboard: Open + Empty + Set + Close. */
static int dx_put(HWND owner, UINT fmt) {
    int ok = 0;
    for (int i = 0; i < 20 && !ok; i++) { ok = U(OpenClipboard)(owner); if (!ok) Sleep(20); }
    if (!ok) return 0;
    U(EmptyClipboard)();
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, 8);
    if (g) { memcpy(GlobalLock(g), "gap11", 6); GlobalUnlock(g); }
    if (g && !U(SetClipboardData)(fmt, g)) { GlobalFree(g); g = NULL; }
    U(CloseClipboard)();
    return g != NULL;
}
static void dx_settle(void) { for (int i = 0; i < 5; i++) { k_pump(20); } }

TEST(dx_gap11_listener_receives_update_until_removed) {
    NtUserRemoveClipboardFormatListener_pfn rm = (void *)g11_nt("NtUserRemoveClipboardFormatListener");
    NtUserAddClipboardFormatListener_pfn ad = (void *)g11_nt("NtUserAddClipboardFormatListener");
    if (!rm || !ad || !U(OpenClipboard) || !U(RegisterClipboardFormatW)) { t_skip("missing stub"); return; }
    G11_NEED_WND(w);
    UINT fmt = dx_fmt();
    CHECK(ad(w) != 0);
    InterlockedExchange(&g11_updates, 0);
    if (!dx_put(w, fmt)) { rm(w); g11_kill(w); t_skip("clipboard busy"); return; }
    dx_settle();
    EXPECT_EQ(g11_updates, 1);                               /* the kernel posts WM_CLIPBOARDUPDATE to each listener */
    CHECK(rm(w) == 1);
    InterlockedExchange(&g11_updates, 0);
    CHECK(dx_put(w, fmt));
    dx_settle();
    EXPECT_EQ(g11_updates, 0);                               /* removed: no further notifications */
    CHECK(ad(w) != 0);                                       /* and it can listen again */
    CHECK(dx_put(w, fmt));
    dx_settle();
    EXPECT_EQ(g11_updates, 1);
    CHECK(rm(w) == 1);
    g11_kill(w);
}

TEST(dx_gap11_removed_listener_does_not_disturb_other_listener) {
    NtUserRemoveClipboardFormatListener_pfn rm = (void *)g11_nt("NtUserRemoveClipboardFormatListener");
    NtUserAddClipboardFormatListener_pfn ad = (void *)g11_nt("NtUserAddClipboardFormatListener");
    if (!rm || !ad || !U(OpenClipboard) || !U(RegisterClipboardFormatW)) { t_skip("missing stub"); return; }
    HWND a = g11_mk(WS_POPUP, 0, NULL), b = g11_mk(WS_POPUP, 0, NULL);
    if (!a || !b) { g11_kill(a); g11_kill(b); t_skip("no windows"); return; }
    UINT fmt = dx_fmt();
    CHECK(ad(a) != 0); CHECK(ad(b) != 0);
    CHECK(rm(a) == 1);
    InterlockedExchange(&g11_updates, 0);
    if (dx_put(b, fmt)) { dx_settle(); EXPECT_EQ(g11_updates, 1); }   /* both share one proc; only b may be notified */
    CHECK(rm(b) == 1);
    g11_kill(a); g11_kill(b);
}

TEST(dx_gap11_destroyed_listener_window_ends_notifications_cleanly) {
    NtUserAddClipboardFormatListener_pfn ad = (void *)g11_nt("NtUserAddClipboardFormatListener");
    NtUserRemoveClipboardFormatListener_pfn rm = (void *)g11_nt("NtUserRemoveClipboardFormatListener");
    if (!rm || !ad || !U(OpenClipboard) || !U(RegisterClipboardFormatW)) { t_skip("missing stub"); return; }
    HWND a = g11_mk(WS_POPUP, 0, NULL), b = g11_mk(WS_POPUP, 0, NULL);
    if (!a || !b) { g11_kill(a); g11_kill(b); t_skip("no windows"); return; }
    UINT fmt = dx_fmt();
    CHECK(ad(a) != 0); CHECK(ad(b) != 0);
    g11_kill(a);                                             /* never removed: window destruction unlinks it from the listener list */
    InterlockedExchange(&g11_updates, 0);
    if (dx_put(b, fmt)) { dx_settle(); EXPECT_EQ(g11_updates, 1); }
    CHECK(rm(b) == 1);
    g11_kill(b);
}

TEST(dx_gap11_nis_on_foreground_focus_window) {
    NtUserNotifyIMEStatus_pfn nis = (void *)g11_nt("NtUserNotifyIMEStatus");
    if (!nis || !U(SetForegroundWindow) || !U(SetFocus)) { t_skip("missing stub"); return; }
    struct k_user_shared_info si; unsigned long v = 0;
    if (!k_shared_info(&si) || !si.psi || !k_try_read_u32(si.psi, &v)) { t_skip("cannot read SERVERINFO"); return; }
    HWND w = g11_mk(WS_POPUP | WS_VISIBLE, 0, NULL);
    if (!w) { t_skip("no window"); return; }
    U(SetForegroundWindow)(w); U(SetFocus)(w);
    k_pump(50);
    int fg = U(GetForegroundWindow)() == w;
    SET_ERR(G11_SENT);
    BYTE r = nis(w, 0, 0);                                   /* with the focus window the notification path actually runs */
    DWORD e = GetLastError();
    if (v & 4) EXPECT_EQ(r, 1);
    else { EXPECT_EQ(r, 0); EXPECT_EQ(e, ERROR_CALL_NOT_IMPLEMENTED); }
    t_note("window foreground = %d, ime flag = %lu", fg, (v & 4) != 0);
    g11_kill(w);
}

TEST(dx_gap11_wda_monitor_visible_from_other_thread_and_cleared) {
    typedef BOOL (WINAPI *set_t)(HWND, DWORD);
    set_t set = (set_t)g11_nt("NtUserSetWindowDisplayAffinity");
    NtUserGetWindowDisplayAffinity_pfn get = (void *)g11_nt("NtUserGetWindowDisplayAffinity");
    if (!set || !get) { t_skip("missing stub"); return; }
    struct g11_peer p;
    if (!g11_peer_start(&p)) { g11_peer_stop(&p); t_skip("no peer window"); return; }
    if (!set(p.hwnd, 1)) { t_note("Set(WDA_MONITOR) failed, err %lu", GetLastError()); g11_peer_stop(&p); t_skip("composition unavailable"); return; }
    DWORD v = 0xdead;
    CHECK(get(p.hwnd, &v) == 1);
    EXPECT_EQ(v, 1);                                         /* stored as a window property, readable from any thread of the process */
    CHECK(set(p.hwnd, 0) != 0);
    v = 0xdead;
    CHECK(get(p.hwnd, &v) == 1);
    EXPECT_EQ(v, 0);
    g11_peer_stop(&p);
}

TEST(dx_gap11_wda_follows_window_after_reparent_to_desktop) {
    typedef BOOL (WINAPI *set_t)(HWND, DWORD);
    set_t set = (set_t)g11_nt("NtUserSetWindowDisplayAffinity");
    NtUserGetWindowDisplayAffinity_pfn get = (void *)g11_nt("NtUserGetWindowDisplayAffinity");
    if (!set || !get || !U(SetParent)) { t_skip("missing stub"); return; }
    G11_NEED_WND(w);
    DWORD v = 0xdead;
    if (!set(w, 1)) { g11_kill(w); t_skip("composition unavailable (err %lu)", GetLastError()); return; }
    CHECK(get(w, &v) == 1); EXPECT_EQ(v, 1);
    HWND c = g11_mk(WS_CHILD, 0, w);
    if (c) {
        SET_ERR(G11_SENT);
        CHECK(get(c, &v) == 0); EXPECT_ERR(ERROR_INVALID_PARAMETER);   /* a child never reports its parent's affinity */
        g11_kill(c);
    }
    CHECK(set(w, 0) != 0);
    g11_kill(w);
}
