/* Objects destroyed while something still holds a lock on them. win32kbase!the deferred-destroy mark sets
 * bFlags bit0 in the table entry; the object is only released (the handle free path: type 0, uniq+1) once the
 * lock count drops to zero, so a locked object stays in the table as a "destroyed" entry for a while.
 * The tests assert the two legal states and that the entry always ends up free. */
#include "r2h.h"

/* Entry for `idx` is either already free (type 0 with the successor uniq) or a destroyed zombie. */
static int zombie_or_free(unsigned idx, unsigned uniq_before, unsigned type, const char **state) {
    r2h_entry_t e;
    if (!r2h_read(idx, &e)) { *state = "unreadable"; return 0; }
    if (e.type == 0) { *state = "freed"; return e.uniq == r2h_next_uniq((unsigned short)uniq_before); }
    if (e.type == type && e.uniq == uniq_before) { *state = "zombie"; return (e.flags & 1) != 0; }
    *state = "reused"; /* another process took the slot already: only legal after a free */
    return e.uniq != uniq_before;
}
static int wait_gone(unsigned idx, unsigned uniq_before, unsigned type, DWORD ms) {
    DWORD t0 = GetTickCount();
    for (;;) {
        r2h_entry_t e;
        if (r2h_read(idx, &e) && !(e.type == type && e.uniq == uniq_before)) return 1;
        if (GetTickCount() - t0 > ms) return 0;
        Sleep(10);
    }
}

/* ---- hook unhooked from inside its own callback ---- */
static struct {
    HHOOK h; unsigned idx, uniq; int calls, unhook_rc, done;
    r2h_entry_t before, during; int read_before, read_during;
} hk;
static LRESULT CALLBACK hook_cb(int code, WPARAM wp, LPARAM lp) {
    hk.calls++;
    if (!hk.done) {
        hk.done = 1;
        hk.read_before = r2h_read(hk.idx, &hk.before);
        hk.unhook_rc = U(UnhookWindowsHookEx)(hk.h);
        hk.read_during = r2h_read(hk.idx, &hk.during);
    }
    return U(CallNextHookEx)(NULL, code, wp, lp);
}
/* the hook-call path holds a lock on the hook while its procedure runs, so UnhookWindowsHookEx from inside
 * the callback marks the entry destroyed but cannot free it until the callback returns. */
R2(hook_unhook_inside_callback_zombie_then_freed) {
    R2H_NEED();
    HWND w = r2h_make_top(); REQUIRE(w);
    memset(&hk, 0, sizeof hk);
    hk.h = U(SetWindowsHookExW)(WH_GETMESSAGE, hook_cb, NULL, GetCurrentThreadId());
    if (!hk.h) { U(DestroyWindow)(w); t_skip("cannot install WH_GETMESSAGE hook (err %lu)", GetLastError()); return; }
    hk.idx = R2H_IDX(hk.h); hk.uniq = R2H_UNIQ(hk.h);
    U(PostMessageW)(w, WM_NULL, 0, 0);
    MSG m;
    for (int i = 0; i < 50 && !hk.done; i++) { U(PeekMessageW)(&m, NULL, 0, 0, PM_REMOVE); if (!hk.done) Sleep(10); }
    if (!hk.done) { U(UnhookWindowsHookEx)(hk.h); U(DestroyWindow)(w); t_skip("hook procedure was not invoked"); return; }
    CHECK(hk.read_before);
    EXPECT_EQ(hk.before.type, UT_HOOK);
    CHECK((hk.before.flags & 1) == 0);
    EXPECT_EQ(hk.before.owner, GetCurrentThreadId());
    CHECK(hk.unhook_rc);
    CHECK(hk.read_during);
    if (hk.during.type == UT_HOOK && hk.during.uniq == hk.uniq) {
        CHECK(hk.during.flags & 1);   /* locked while running: destroyed flag, entry kept */
        t_note("hook unhooked inside its callback stays as a destroyed entry (flags 0x%02x)", hk.during.flags);
    } else {
        t_note("hook entry already released inside the callback (type %u uniq %u)", hk.during.type, hk.during.uniq);
        CHECK(hk.during.type == 0 || hk.during.uniq != hk.uniq);
    }
    /* the callback has returned: nothing holds the hook any more */
    CHECK(wait_gone(hk.idx, hk.uniq, UT_HOOK, 2000));
    int calls = hk.calls;
    U(PostMessageW)(w, WM_NULL, 0, 0);
    for (int i = 0; i < 5; i++) { U(PeekMessageW)(&m, NULL, 0, 0, PM_REMOVE); Sleep(5); }
    EXPECT_EQ(hk.calls, calls);   /* a destroyed hook is not called again */
    U(DestroyWindow)(w);
}
R2(hook_unhook_after_callback_rejects_stale_handle) {
    R2H_NEED();
    HWND w = r2h_make_top(); REQUIRE(w);
    memset(&hk, 0, sizeof hk);
    hk.h = U(SetWindowsHookExW)(WH_GETMESSAGE, hook_cb, NULL, GetCurrentThreadId());
    if (!hk.h) { U(DestroyWindow)(w); t_skip("cannot install hook"); return; }
    hk.idx = R2H_IDX(hk.h); hk.uniq = R2H_UNIQ(hk.h);
    U(PostMessageW)(w, WM_NULL, 0, 0);
    MSG m;
    for (int i = 0; i < 50 && !hk.done; i++) { U(PeekMessageW)(&m, NULL, 0, 0, PM_REMOVE); if (!hk.done) Sleep(10); }
    if (!hk.done) { U(UnhookWindowsHookEx)(hk.h); U(DestroyWindow)(w); t_skip("hook procedure was not invoked"); return; }
    wait_gone(hk.idx, hk.uniq, UT_HOOK, 2000);
    BOOL r; R2H_CALL_ERR(r, U(UnhookWindowsHookEx)(hk.h)); /* HMValidateHandle(TYPE_HOOK) fails: 0x57c */
    CHECK(!r);
    EXPECT_ERR(0x57c);
    U(DestroyWindow)(w);
}

/* ---- window destroyed while another thread's SendMessage is in flight ---- */
static struct { HWND w; unsigned idx, uniq; int hit, vis_in_destroy; r2h_entry_t after; int read_after; BOOL iswin_after; } zs;
static LRESULT CALLBACK zs_proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_APP + 7) {
        zs.hit = 1;
        U(DestroyWindow)(h);
        zs.read_after = r2h_read(zs.idx, &zs.after);
        zs.iswin_after = U(IsWindow)(h);
        return 0x1234;
    }
    return U(DefWindowProcW)(h, m, wp, lp);
}
static DWORD WINAPI sender_thread(void *p) {
    DWORD_PTR res = 0;
    LRESULT r = U(SendMessageTimeoutW)(zs.w, WM_APP + 7, 0, 0, SMTO_ABORTIFHUNG, 4000, &res);
    return (DWORD)(r ? 1 : 0) | ((DWORD)(res & 0xffff) << 8);
}
/* The sending queue references the receiver window for the duration of the send, so destroying it
 * inside the handler cannot free the object before the sender is released. */
R2(window_destroyed_inside_cross_thread_send) {
    R2H_NEED();
    WNDCLASSEXW wc; memset(&wc, 0, sizeof wc);
    WCHAR cn[64]; wsprintfW(cn, L"win32kt_r2h_zs_%lu", GetCurrentProcessId());
    wc.cbSize = sizeof wc; wc.lpfnWndProc = zs_proc; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = cn;
    REQUIRE(U(RegisterClassExW)(&wc));
    memset(&zs, 0, sizeof zs);
    zs.w = U(CreateWindowExW)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cn, L"zs", WS_POPUP, -32000, -32000, 1, 1, NULL, NULL, wc.hInstance, NULL);
    if (!zs.w) { U(UnregisterClassW)(cn, wc.hInstance); t_skip("window not created"); return; }
    zs.idx = R2H_IDX(zs.w); zs.uniq = R2H_UNIQ(zs.w);
    HANDLE th = CreateThread(NULL, 0, sender_thread, NULL, 0, NULL);
    REQUIRE(th);
    MSG m;
    DWORD t0 = GetTickCount();
    while (!zs.hit && GetTickCount() - t0 < 4000) { U(PeekMessageW)(&m, NULL, 0, 0, PM_REMOVE); Sleep(1); }
    CHECK(zs.hit);
    CHECK(WaitForSingleObject(th, 6000) == WAIT_OBJECT_0);
    DWORD rc = 0; GetExitCodeThread(th, &rc); CloseHandle(th);
    t_note("sender result 0x%lx", rc);
    if (zs.hit) {
        CHECK(!zs.iswin_after);  /* user32 IsWindow rejects the entry whether it is freed or flagged destroyed */
        CHECK(zs.read_after);
        const char *st = "?";
        CHECKF(zombie_or_free(zs.idx, zs.uniq, UT_WINDOW, &st), "entry after in-handler DestroyWindow is %s (type %u flags 0x%02x uniq %u)", st, zs.after.type, zs.after.flags, zs.after.uniq);
        t_info("entry state right after DestroyWindow inside a cross-thread send: %s", st);
    }
    CHECK(wait_gone(zs.idx, zs.uniq, UT_WINDOW, 3000));
    CHECK(!U(IsWindow)(zs.w));
    U(UnregisterClassW)(cn, wc.hInstance);
}

/* ---- WM_DESTROY / WM_NCDESTROY observation ---- */
static struct { unsigned idx, uniq; int dest, ncdest; BOOL win_in_destroy, win_in_nc; r2h_entry_t e_dest, e_nc; int r1, r2; HWND child_valid_in_destroy; HWND child; } ds;
static LRESULT CALLBACK ds_proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_DESTROY) {
        ds.dest++;
        ds.win_in_destroy = U(IsWindow)(h);
        ds.r1 = r2h_read(ds.idx, &ds.e_dest);
        ds.child_valid_in_destroy = (HWND)(ULONG_PTR)U(IsWindow)(ds.child);
    } else if (m == WM_NCDESTROY) {
        ds.ncdest++;
        ds.win_in_nc = U(IsWindow)(h);
        ds.r2 = r2h_read(ds.idx, &ds.e_nc);
    }
    return U(DefWindowProcW)(h, m, wp, lp);
}
/* WM_DESTROY is sent before the window is torn down: still a live, unflagged window entry and its
 * children are still valid. */
R2(window_entry_live_during_wm_destroy) {
    R2H_NEED();
    WNDCLASSEXW wc; memset(&wc, 0, sizeof wc);
    WCHAR cn[64]; wsprintfW(cn, L"win32kt_r2h_ds_%lu", GetCurrentProcessId());
    wc.cbSize = sizeof wc; wc.lpfnWndProc = ds_proc; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = cn;
    REQUIRE(U(RegisterClassExW)(&wc));
    memset(&ds, 0, sizeof ds);
    HWND w = U(CreateWindowExW)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cn, L"ds", WS_POPUP, -32000, -32000, 1, 1, NULL, NULL, wc.hInstance, NULL);
    if (!w) { U(UnregisterClassW)(cn, wc.hInstance); t_skip("window not created"); return; }
    ds.child = U(CreateWindowExW)(0, cn, L"dc", WS_CHILD, 0, 0, 1, 1, w, NULL, wc.hInstance, NULL);
    ds.idx = R2H_IDX(w); ds.uniq = R2H_UNIQ(w);
    U(DestroyWindow)(w);
    EXPECT_EQ(ds.dest, 2);   /* parent + child both receive WM_DESTROY */
    EXPECT_EQ(ds.ncdest, 2);
    CHECK(!U(IsWindow)(w));
    CHECK(!U(IsWindow)(ds.child));
    r2h_entry_t e; CHECK(r2h_read(ds.idx, &e));
    CHECK(e.type == 0 || e.uniq != ds.uniq || (e.flags & 1));
    U(UnregisterClassW)(cn, wc.hInstance);
}
R2(window_destroy_message_state_is_recorded) {
    /* separate window so the counters above are not mixed */
    R2H_NEED();
    WNDCLASSEXW wc; memset(&wc, 0, sizeof wc);
    WCHAR cn[64]; wsprintfW(cn, L"win32kt_r2h_dt_%lu", GetCurrentProcessId());
    wc.cbSize = sizeof wc; wc.lpfnWndProc = ds_proc; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = cn;
    REQUIRE(U(RegisterClassExW)(&wc));
    memset(&ds, 0, sizeof ds);
    HWND w = U(CreateWindowExW)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cn, L"dt", WS_POPUP, -32000, -32000, 1, 1, NULL, NULL, wc.hInstance, NULL);
    if (!w) { U(UnregisterClassW)(cn, wc.hInstance); t_skip("window not created"); return; }
    ds.idx = R2H_IDX(w); ds.uniq = R2H_UNIQ(w);
    U(DestroyWindow)(w);
    EXPECT_EQ(ds.dest, 1);
    EXPECT_EQ(ds.ncdest, 1);
    CHECK(ds.r1);
    CHECK(ds.win_in_destroy);              /* MSDN: the handle is still valid while WM_DESTROY runs */
    EXPECT_EQ(ds.e_dest.type, UT_WINDOW);
    EXPECT_EQ(ds.e_dest.uniq, ds.uniq);
    CHECK((ds.e_dest.flags & 1) == 0);     /* the deferred-destroy mark has not run yet */
    EXPECT_EQ(ds.e_dest.owner, GetCurrentThreadId());
    CHECK(ds.r2);
    t_note("inside WM_NCDESTROY: IsWindow=%d entry type %u flags 0x%02x", ds.win_in_nc, ds.e_nc.type, ds.e_nc.flags);
    CHECK(!U(IsWindow)(w));
    U(UnregisterClassW)(cn, wc.hInstance);
}

/* ---- menu owner: destroying the owner window of a tracked/attached menu ---- */
R2(attached_menu_freed_with_window_but_other_menu_survives) {
    R2H_NEED();
    HWND w = r2h_make_top(); REQUIRE(w);
    HMENU a = U(CreateMenu)(), b = U(CreateMenu)();
    REQUIRE(a); REQUIRE(b);
    CHECK(U(SetMenu)(w, a));
    unsigned ia = R2H_IDX(a), ib = R2H_IDX(b), ua = R2H_UNIQ(a), ub = R2H_UNIQ(b);
    U(DestroyWindow)(w);
    r2h_entry_t ea, eb;
    CHECK(r2h_read(ia, &ea)); CHECK(r2h_read(ib, &eb));
    CHECK(!(ea.type == UT_MENU && ea.uniq == ua && !(ea.flags & 1)));  /* attached menu goes away with the window */
    EXPECT_EQ(eb.type, UT_MENU); EXPECT_EQ(eb.uniq, ub); CHECK((eb.flags & 1) == 0); /* unattached menu is independent */
    CHECK(U(IsMenu)(b));
    CHECK(!U(IsMenu)(a));
    U(DestroyMenu)(b);
}
