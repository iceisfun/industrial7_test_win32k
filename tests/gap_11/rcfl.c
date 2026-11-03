/* gap_11 / NtUserRemoveClipboardFormatListener (win32kfull 0xefb0):
 *   EnterCrit; hwnd = ValidateHwnd(arg)  -> NULL: return 0 (ValidateHwnd sets ERROR_INVALID_WINDOW_HANDLE)
 *   if (pwnd->pti->ppi != current process) return 5       (non-zero, no error set)
 *   if (CheckClipboardAccess() == NULL) return 0          (error from ReferenceWindowStation)
 *   if (!(listener flag bit 0)) { SetLastError(0x57); return 0; }
 *   else { __RemoveClipboardFormatListener(pwnd); return 1; }
 * Ownership is by process, not by thread. Only the window's own listener flag is touched here; the
 * tests that need the clipboard to change are in dx_gap_11. */
#include "g11.h"

#define RM_NEED() UDECL_RM_(); NtUserRemoveClipboardFormatListener_pfn rm = (NtUserRemoveClipboardFormatListener_pfn)g11_nt("NtUserRemoveClipboardFormatListener"); \
    NtUserAddClipboardFormatListener_pfn ad = (NtUserAddClipboardFormatListener_pfn)g11_nt("NtUserAddClipboardFormatListener"); \
    if (!rm || !ad) { t_skip("missing stub"); return; } \
    if (!U(CreateWindowExW)) { t_skip("no user32"); return; }
#define UDECL_RM_() (void)0

static ULONG_PTR rm_call(NtUserRemoveClipboardFormatListener_pfn rm, HWND h, DWORD *err) {
    SET_ERR(G11_SENT);
    ULONG_PTR r = rm(h);
    *err = GetLastError();
    return r;
}

TEST(gap11_rcfl_invalid_handle_table) {
    RM_NEED();
    G11_NEED_WND(w);
    HMENU m = U(CreatePopupMenu) ? U(CreatePopupMenu)() : NULL;
    HWND dead = g11_mk(WS_POPUP, 0, NULL); g11_kill(dead);
    HWND bad[] = { NULL, (HWND)(ULONG_PTR)-1, (HWND)(ULONG_PTR)0x7FFE7777, (HWND)(ULONG_PTR)0x0FFFFFF0,
                   (HWND)(ULONG_PTR)0xFFFF, (HWND)(ULONG_PTR)0x10000, dead, (HWND)m };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        if (i == 7 && !m) continue;
        DWORD e; ULONG_PTR r = rm_call(rm, bad[i], &e);
        /* ValidateHwnd fails (no window of that type/uniq) before ownership or listener state is looked at */
        CHECKF(r == 0, "handle %p returned %llu", (void *)bad[i], (unsigned long long)r);
        CHECKF(e == ERROR_INVALID_WINDOW_HANDLE, "handle %p error %lu", (void *)bad[i], e);
    }
    if (m) U(DestroyMenu)(m);
    g11_kill(w);
}

TEST(gap11_rcfl_not_listening_is_invalid_parameter) {
    RM_NEED(); G11_NEED_WND(w);
    DWORD e; ULONG_PTR r = rm_call(rm, w, &e);
    CHECK(r == 0); EXPECT_EQ(e, ERROR_INVALID_PARAMETER);   /* listener flag clear -> UserSetLastError(0x57) */
    g11_kill(w);
}

TEST(gap11_rcfl_remove_after_add_returns_exactly_one) {
    RM_NEED(); G11_NEED_WND(w);
    CHECK(ad(w) != 0);
    DWORD e; ULONG_PTR r = rm_call(rm, w, &e);
    EXPECT_EQ(r, 1);
    if (e != G11_SENT) t_note("RemoveClipboardFormatListener success changed last error to %lu", e);
    r = rm_call(rm, w, &e);                                  /* flag was cleared by __RemoveClipboardFormatListener */
    CHECK(r == 0); EXPECT_EQ(e, ERROR_INVALID_PARAMETER);
    g11_kill(w);
}

TEST(gap11_rcfl_add_remove_cycles) {
    RM_NEED(); G11_NEED_WND(w);
    for (int i = 0; i < 100; i++) {
        SET_ERR(0);
        CHECKF(ad(w) != 0, "add %d", i);
        DWORD e; ULONG_PTR r = rm_call(rm, w, &e);
        CHECKF(r == 1, "remove %d returned %llu", i, (unsigned long long)r);
        r = rm_call(rm, w, &e);
        CHECKF(r == 0 && e == ERROR_INVALID_PARAMETER, "second remove %d: %llu err %lu", i, (unsigned long long)r, e);
    }
    g11_kill(w);
}

TEST(gap11_rcfl_flags_are_per_window) {
    RM_NEED();
    HWND a = g11_mk(WS_POPUP, 0, NULL), b = g11_mk(WS_POPUP, 0, NULL), c = g11_mk(WS_POPUP, 0, NULL);
    if (!a || !b || !c) { g11_kill(a); g11_kill(b); g11_kill(c); t_skip("cannot create windows"); return; }
    CHECK(ad(a) != 0); CHECK(ad(b) != 0);
    DWORD e;
    G11_EQ(rm_call(rm, a, &e), 1);
    ULONG_PTR r = rm_call(rm, a, &e);                       /* a is no longer listening ... */
    CHECK(r == 0); EXPECT_EQ(e, ERROR_INVALID_PARAMETER);
    r = rm_call(rm, c, &e);                                  /* ... c never was ... */
    CHECK(r == 0); EXPECT_EQ(e, ERROR_INVALID_PARAMETER);
    G11_EQ(rm_call(rm, b, &e), 1);                        /* ... and b still is */
    g11_kill(a); g11_kill(b); g11_kill(c);
}

TEST(gap11_rcfl_message_only_window) {
    RM_NEED();
    HWND w = g11_mk(WS_POPUP, 0, HWND_MESSAGE);
    if (!w) { t_skip("no message-only window (err %lu)", GetLastError()); return; }
    CHECK(ad(w) != 0);
    DWORD e; G11_EQ(rm_call(rm, w, &e), 1);
    g11_kill(w);
}

TEST(gap11_rcfl_child_window) {
    RM_NEED(); G11_NEED_WND(p);
    HWND c = g11_mk(WS_CHILD, 0, p);
    if (!c) { g11_kill(p); t_skip("no child window"); return; }
    DWORD e;
    ULONG_PTR r = rm_call(rm, c, &e);                       /* a valid same-process window of any kind reaches the flag test */
    CHECK(r == 0); EXPECT_EQ(e, ERROR_INVALID_PARAMETER);
    if (ad(c)) { G11_EQ(rm_call(rm, c, &e), 1); }
    else t_note("AddClipboardFormatListener(child) failed, err %lu", GetLastError());
    g11_kill(p);
}

TEST(gap11_rcfl_destroyed_while_listening) {
    RM_NEED(); G11_NEED_WND(w);
    CHECK(ad(w) != 0);
    g11_kill(w);
    DWORD e; ULONG_PTR r = rm_call(rm, w, &e);
    CHECK(r == 0); EXPECT_EQ(e, ERROR_INVALID_WINDOW_HANDLE);  /* stale handle fails ValidateHwnd */
}

TEST(gap11_rcfl_other_thread_same_process_window) {
    RM_NEED();
    struct g11_peer p;
    if (!g11_peer_start(&p)) { g11_peer_stop(&p); t_skip("peer thread has no window"); return; }
    /* the owner test compares process objects (pti->ppi), so a window of another thread of this process passes */
    CHECK(ad(p.hwnd) != 0);
    DWORD e; G11_EQ(rm_call(rm, p.hwnd, &e), 1);
    ULONG_PTR r = rm_call(rm, p.hwnd, &e);
    CHECK(r == 0); EXPECT_EQ(e, ERROR_INVALID_PARAMETER);
    g11_peer_stop(&p);
}

TEST(gap11_rcfl_added_on_peer_removed_on_main) {
    RM_NEED();
    struct g11_peer p;
    if (!g11_peer_start(&p)) { g11_peer_stop(&p); t_skip("peer thread has no window"); return; }
    CHECK(ad(p.hwnd) != 0);
    /* the listener flag lives on the window, not on the calling thread */
    DWORD e; G11_EQ(rm_call(rm, p.hwnd, &e), 1);
    g11_peer_stop(&p);
    ULONG_PTR r = rm_call(rm, p.hwnd, &e);                  /* peer destroyed its window on exit */
    CHECK(r == 0); EXPECT_EQ(e, ERROR_INVALID_WINDOW_HANDLE);
}

TEST(gap11_rcfl_foreign_process_window) {
    RM_NEED();
    REQUIRE(U(GetShellWindow)); REQUIRE(U(GetWindowThreadProcessId));
    HWND sh = U(GetShellWindow)();
    DWORD pid = 0;
    if (!sh) { t_skip("no shell window"); return; }
    U(GetWindowThreadProcessId)(sh, &pid);
    if (!pid || pid == GetCurrentProcessId()) { t_skip("shell window is in this process"); return; }
    DWORD e; ULONG_PTR r = rm_call(rm, sh, &e);
    /* Two outcomes, depending on the caller's token: the foreign-process answer (the constant 5, error untouched) for the
     * elevated token, and a refusal (return 0 with ERROR_ACCESS_DENIED) for the unprivileged token, which may not use a window of
     * the shell's process. Nothing else is acceptable. */
    t_note("result %llu error %lu", (unsigned long long)r, e);
    CHECKF((r == 5 && e == G11_SENT) || (r == 0 && e == ERROR_ACCESS_DENIED), "result %llu error %lu", (unsigned long long)r, e);
}

TEST(gap11_rcfl_desktop_window) {
    RM_NEED(); REQUIRE(U(GetDesktopWindow));
    HWND d = U(GetDesktopWindow)();
    DWORD e; ULONG_PTR r = rm_call(rm, d, &e);
    /* the desktop window belongs to the system process: 5 (foreign); same-process would give 0/0x57 */
    CHECKF(r == 5 || (r == 0 && e == ERROR_INVALID_PARAMETER), "desktop window returned %llu err %lu", (unsigned long long)r, e);
    t_note("Remove(desktop window) -> %llu err %lu", (unsigned long long)r, e);
}

TEST(gap11_rcfl_user32_matches_stub) {
    RM_NEED();
    FARPROC u32 = t_proc_in("user32.dll", "RemoveClipboardFormatListener");
    if (!u32) { t_skip("no user32 export"); return; }
    BOOL (WINAPI *f)(HWND) = (BOOL (WINAPI *)(HWND))u32;
    G11_NEED_WND(w);
    SET_ERR(G11_SENT);
    BOOL r = f(w);
    CHECK(r == 0); EXPECT_ERR(ERROR_INVALID_PARAMETER);
    CHECK(ad(w) != 0);
    CHECK(f(w) != 0);
    SET_ERR(G11_SENT);
    r = f((HWND)(ULONG_PTR)0x7FFE7777);
    CHECK(r == 0); EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
    g11_kill(w);
}

TEST(gap11_rcfl_listener_does_not_pin_window) {
    RM_NEED(); REQUIRE(U(IsWindow));
    G11_NEED_WND(w);
    CHECK(ad(w) != 0);
    CHECK(U(DestroyWindow)(w));                              /* a pending listener registration does not keep the window alive */
    CHECK(!U(IsWindow)(w));
}
