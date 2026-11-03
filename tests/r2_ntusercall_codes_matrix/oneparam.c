/* NtUserCallOneParam per-code semantics. Dispatcher (win32kfull): codes 0x2c..0x53; 0x53 runs under
 * EnterSharedCrit, 0x4c without any critical section, codes < 0x2e would dereference the result
 * (none of the codes below do). Return values are pointer-sized; BOOL/int codes are masked to 32 bits. */
#include "mx.h"

/* ---- 0x2c BeginDeferWindowPos ----------------------------------------------------------------- */
/* _BeginDeferWindowPos: allocates a deferred-position object, capacity = n ? n : 8, flags |= 2 */
T(oneparam_0x2c_begin_defer_window_pos) {
    NEED_RAW(NtUserCallOneParam);
    static const ULONG_PTR counts[] = { 0, 1, 5, 8, 64 };
    for (size_t i = 0; i < sizeof counts / sizeof counts[0]; i++) {
        HDWP h = (HDWP)OP(counts[i], 0x2c);
        CHECKF(h != NULL, "BeginDeferWindowPos(%llu) returned NULL", (unsigned long long)counts[i]);
        if (!h) continue;
        struct k_user_entry e;
        if (k_shared_info_ptr() && k_user_entry((HANDLE)h, &e)) CHECKF(e.type == 4, "handle type %u want TYPE_SETWINDOWPOS (4)", e.type);
        CHECK(U(EndDeferWindowPos)(h));          /* frees the object */
    }
}
T(oneparam_0x2c_two_handles_are_distinct_and_freed) {
    NEED_RAW(NtUserCallOneParam);
    HDWP a = (HDWP)OP(2, 0x2c), b = (HDWP)OP(2, 0x2c);
    CHECK(a && b && a != b);
    if (a) CHECK(U(EndDeferWindowPos)(a));
    if (b) CHECK(U(EndDeferWindowPos)(b));
    SetLastError(0);
    if (a) CHECK(!U(EndDeferWindowPos)(a));   /* already freed */
}
T(oneparam_0x2c_defer_window_pos_moves_window) {
    NEED_RAW(NtUserCallOneParam);
    HWND w = k_make_window("r2ncm_dwp");
    if (!w) { t_skip("no window"); return; }
    HDWP h = (HDWP)OP(1, 0x2c);
    CHECK(h != NULL);
    if (h) {
        h = U(DeferWindowPos)(h, w, NULL, -31000, -31000, 3, 3, SWP_NOZORDER | SWP_NOACTIVATE);
        CHECK(h != NULL);
        if (h) CHECK(U(EndDeferWindowPos)(h));
        RECT r; CHECK(U(GetWindowRect)(w, &r));
        CHECK(r.left == -31000 && r.top == -31000 && r.right - r.left == 3);
    }
    U(DestroyWindow)(w);
}

/* ---- 0x2d GetSendMessageReceiver -------------------------------------------------------------- */
/* _GetSendMessageReceiver(tid): PtiFromThreadId == NULL or other desktop -> UserSetLastError(0x57) */
T(oneparam_0x2d_get_send_message_receiver_bogus_tid) {
    NEED_RAW(NtUserCallOneParam);
    static const DWORD tids[] = { 0, 0x7ffffff0u, 0xfffffffcu };
    for (size_t i = 0; i < sizeof tids / sizeof tids[0]; i++) {
        SetLastError(SENTINEL_ERR);
        CHECKF(OP(tids[i], 0x2d) == 0, "tid 0x%lx", (unsigned long)tids[i]);
        CHECKF(GetLastError() == ERR_INVALID_PARAMETER, "tid 0x%lx err %lu", (unsigned long)tids[i], GetLastError());
    }
}
T(oneparam_0x2d_get_send_message_receiver_idle_threads) {
    NEED_RAW(NtUserCallOneParam);
    struct mx_bg bg;
    if (!mx_bg_start(&bg, 0)) { t_skip("bg"); mx_bg_stop(&bg); return; }
    HWND w = k_make_window("r2ncm_rcv");
    /* neither this thread nor the helper thread is inside a send: *(pti+0x1f8) is NULL -> 0, no error */
    SetLastError(SENTINEL_ERR);
    CHECK(OP(GetCurrentThreadId(), 0x2d) == 0);
    CHECK(GetLastError() == SENTINEL_ERR);
    SetLastError(SENTINEL_ERR);
    CHECK(OP(bg.tid, 0x2d) == 0);
    CHECK(GetLastError() == SENTINEL_ERR);
    if (w) U(DestroyWindow)(w);
    mx_bg_stop(&bg);
}
T(oneparam_0x2d_finished_thread_is_invalid) {
    NEED_RAW(NtUserCallOneParam);
    struct mx_bg bg;
    if (!mx_bg_start(&bg, 0)) { t_skip("bg"); mx_bg_stop(&bg); return; }
    DWORD tid = bg.tid;
    mx_bg_stop(&bg);
    Sleep(50);
    SetLastError(SENTINEL_ERR);
    CHECK(OP(tid, 0x2d) == 0);
    CHECK(GetLastError() == ERR_INVALID_PARAMETER);
    SetLastError(SENTINEL_ERR);
    CHECK(B8(OP(tid, 0x4e)) == 0);                      /* _ThreadMessageQueueAttached: same PtiFromThreadId failure */
    CHECK(GetLastError() == ERR_INVALID_PARAMETER);
}

/* ---- 0x2e AllowSetForegroundWindow ------------------------------------------------------------ */
/* _AllowSetForegroundWindow: pid -1 clears the "last woken" slot (return 1); an unknown pid fails
 * LockProcessByClientId -> 0x57. Before that a caller that may not force the foreground gets 5. */
T(oneparam_0x2e_allow_set_foreground_window) {
    NEED_RAW(NtUserCallOneParam);
    SetLastError(SENTINEL_ERR);
    DWORD r = LO32(OP(0xffffffffu, 0x2e));            /* ASFW_ANY */
    if (r) { CHECK(r == 1); }
    else   { CHECK(GetLastError() == ERR_ACCESS_DENIED); t_note("caller cannot force foreground (error 5)"); }
    SetLastError(SENTINEL_ERR);
    r = LO32(OP(0x7ffffff0u, 0x2e));                  /* no such process */
    CHECK(r == 0);
    DWORD e = GetLastError();
    CHECKF(e == ERR_INVALID_PARAMETER || e == ERR_ACCESS_DENIED, "err %lu", e);
    SetLastError(SENTINEL_ERR);
    CHECK(U(AllowSetForegroundWindow)(0x7ffffff0u) == 0);   /* public wrapper == code 0x2e */
}

/* ---- 0x31 EnumClipboardFormats ------------------------------------------------------------------ */
T(oneparam_0x31_enum_clipboard_formats_requires_open_clipboard) {
    NEED_RAW(NtUserCallOneParam);
    SetLastError(SENTINEL_ERR);
    CHECK(LO32(OP(0, 0x31)) == 0);
    EXPECT_ERR(ERR_CLIPBOARD_NOT_OPEN);          /* the clipboard owner is not the calling thread -> UserSetLastError(0x58a) */
    SetLastError(SENTINEL_ERR);
    CHECK(U(EnumClipboardFormats)(0) == 0);
    EXPECT_ERR(ERR_CLIPBOARD_NOT_OPEN);
}
T(oneparam_0x31_enum_clipboard_formats_with_open_clipboard) {
    NEED_RAW(NtUserCallOneParam);
    HWND w = k_make_window("r2ncm_clip");
    if (!w) { t_skip("no window"); return; }
    if (!U(OpenClipboard)(w)) { t_skip("clipboard busy (%lu)", GetLastError()); U(DestroyWindow)(w); return; }
    UINT first = LO32(OP(0, 0x31));
    CHECK(first == U(EnumClipboardFormats)(0));
    UINT cur = first; int n = 0;
    while (cur && n < 64) { UINT nx = LO32(OP(cur, 0x31)); CHECK(nx == U(EnumClipboardFormats)(cur)); cur = nx; n++; }
    CHECK(n < 64);                                   /* the enumeration terminates */
    CHECK(n == U(CountClipboardFormats)());
    SetLastError(SENTINEL_ERR);
    CHECK(LO32(OP(0xdeadu, 0x31)) == 0);             /* FindClipFormat() == NULL */
    U(CloseClipboard)();
    SetLastError(SENTINEL_ERR);
    CHECK(LO32(OP(0, 0x31)) == 0);
    EXPECT_ERR(ERR_CLIPBOARD_NOT_OPEN);
    U(DestroyWindow)(w);
}

/* ---- 0x33 GetKeyboardType ----------------------------------------------------------------------- */
T(oneparam_0x33_get_keyboard_type_unknown_flags_return_zero) {
    NEED_RAW(NtUserCallOneParam);
    static const long long flags[] = { 3, 4, 5, 0x100, 0x7ffffffd, -1, -2, 0x80000000LL };
    for (size_t i = 0; i < sizeof flags / sizeof flags[0]; i++) {
        SetLastError(SENTINEL_ERR);
        CHECKF(LO32(OP((ULONG_PTR)flags[i], 0x33)) == 0, "flag %lld", flags[i]);
        CHECKF(GetLastError() == SENTINEL_ERR, "flag %lld changed last error", flags[i]);
    }
}
T(oneparam_0x33_get_keyboard_type_matches_public_on_console) {
    NEED_RAW(NtUserCallOneParam);
    /* user32!GetKeyboardType only forwards to the kernel in the console / service session */
    DWORD sid = k_session_id();
    typedef DWORD (WINAPI *wts_fn)(void);
    wts_fn wts = (wts_fn)t_proc_in("kernel32.dll", "WTSGetActiveConsoleSessionId");
    int console = wts && wts() == sid;
    for (int f = 0; f <= 2; f++) {
        DWORD d = LO32(OP(f, 0x33)), p = U(GetKeyboardType)(f);
        if (console) CHECKF(d == p, "flag %d direct %lu public %lu", f, (unsigned long)d, (unsigned long)p);
        else t_note("flag %d direct %lu public %lu (session %lu not the console)", f, (unsigned long)d, (unsigned long)p, (unsigned long)sid);
    }
    t_note("keyboard type %lu subtype %lu function keys %lu", (unsigned long)LO32(OP(0, 0x33)), (unsigned long)LO32(OP(1, 0x33)), (unsigned long)LO32(OP(2, 0x33)));
}
T(oneparam_0x33_get_keyboard_type_is_deterministic) {
    NEED_RAW(NtUserCallOneParam);
    for (int f = 0; f <= 2; f++) CHECK(LO32(OP(f, 0x33)) == LO32(OP(f, 0x33)));
    CHECK(LO32(OP(0x7ffffffe, 0x33)) == LO32(OP(0x7ffffffe, 0x33)));
    CHECK(LO32(OP(0x7fffffff, 0x33)) == LO32(OP(0x7fffffff, 0x33)));
}

/* ---- 0x34 GetProcessDefaultLayout ------------------------------------------------------------------ */
/* _GetProcessDefaultLayout: csrss -> error 12; otherwise *out = ppi+0x338, return 1 */
T(oneparam_0x34_get_process_default_layout) {
    NEED_RAW(NtUserCallOneParam);
    DWORD direct = 0xdeadbeef, pub = 0xcafe;
    SetLastError(SENTINEL_ERR);
    CHECK(LO32(OP(&direct, 0x34)) == 1);
    CHECK(U(GetProcessDefaultLayout)(&pub) != 0);
    CHECK(direct == pub);
    CHECK((direct & ~7u) == 0);                  /* only LAYOUT_RTL / BTT / VBH can ever be stored */
    CHECK(GetLastError() == SENTINEL_ERR);
}

/* ---- 0x46 SetProcessDefaultLayout (process-wide -> child only) ------------------------------------ */
TCHILD(oneparam_default_layout) {
    (void)argc; (void)argv;
    FARPROC pset = t_proc_in("win32u.dll", "NtUserCallOneParam");
    FARPROC pget = t_proc_in("user32.dll", "GetProcessDefaultLayout");
    FARPROC pubset = t_proc_in("user32.dll", "SetProcessDefaultLayout");
    if (!pset || !pget || !pubset) return 0;
    ULONG_PTR (WINAPI *op)(ULONG_PTR, DWORD) = (ULONG_PTR (WINAPI *)(ULONG_PTR, DWORD))pset;
    BOOL (WINAPI *get)(DWORD *) = (BOOL (WINAPI *)(DWORD *))pget;
    BOOL (WINAPI *pub)(DWORD) = (BOOL (WINAPI *)(DWORD))pubset;
    int fails = 0;
    for (DWORD v = 0; v <= 7; v++) {
        SetLastError(0);
        DWORD r = (DWORD)op(v, 0x46);
        DWORD got = 0xff; get(&got);
        if (r != 1 || got != v) { printf("  FAIL layout %lu: ret %lu readback %lu\n", (unsigned long)v, (unsigned long)r, (unsigned long)got); fails++; }
    }
    static const DWORD bad[] = { 8, 9, 0x10, 0x100, 0x80000000u, 0xfffffff8u, 0xffffffffu };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        DWORD before = 0; get(&before);
        SetLastError(0);
        DWORD r = (DWORD)op(bad[i], 0x46);
        DWORD e = GetLastError(), after = 0xff; get(&after);
        if (r != 0 || e != 87 || after != before) { printf("  FAIL layout 0x%lx: ret %lu err %lu after %lu before %lu\n", (unsigned long)bad[i], (unsigned long)r, (unsigned long)e, (unsigned long)after, (unsigned long)before); fails++; }
    }
    /* the public wrapper is the same call; a window created afterwards inherits the process layout bit */
    pub(1);
    DWORD got = 0; get(&got);
    if (got != 1) { printf("  FAIL public SetProcessDefaultLayout(1) readback %lu\n", (unsigned long)got); fails++; }
    pub(0);
    return fails;
}
T(oneparam_0x46_set_process_default_layout_in_child) {
    if (t_is_wine()) { t_skip("Wine"); return; }
    struct k_child_result r = k_child_run("r2_ntusercall_codes_matrix_oneparam_default_layout", "", 30000);
    CHECK(r.exit_code == 0);
}

/* ---- 0x36 LockSetForegroundWindow --------------------------------------------------------------------- */
/* _LockSetForegroundWindow: 1 = lock (needs CanForceForeground, only when unlocked), 2 = unlock (only the
 * locking process), anything else -> 0x57; refusals are error 5. */
T(oneparam_0x36_lock_set_foreground_window_bad_codes) {
    NEED_RAW(NtUserCallOneParam);
    static const DWORD bad[] = { 0, 3, 4, 0x100, 0xffffffffu };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(SENTINEL_ERR);
        CHECKF(LO32(OP(bad[i], 0x36)) == 0, "code %lu", (unsigned long)bad[i]);
        CHECKF(GetLastError() == ERR_INVALID_PARAMETER, "code %lu err %lu", (unsigned long)bad[i], GetLastError());
    }
}
T(oneparam_0x36_lock_unlock_roundtrip) {
    NEED_RAW(NtUserCallOneParam);
    SetLastError(SENTINEL_ERR);
    DWORD unl = LO32(OP(2, 0x36));                    /* we do not hold the lock */
    CHECK(unl == 0);
    EXPECT_ERR(ERR_ACCESS_DENIED);
    SetLastError(SENTINEL_ERR);
    DWORD l = LO32(OP(1, 0x36));
    if (l == 1) {
        SetLastError(SENTINEL_ERR);
        CHECK(LO32(OP(1, 0x36)) == 0);                /* already locked: *gppiLockSFW != 0 */
        EXPECT_ERR(ERR_ACCESS_DENIED);
        CHECK(LO32(OP(2, 0x36)) == 1);                /* release immediately */
        SetLastError(SENTINEL_ERR);
        CHECK(LO32(OP(2, 0x36)) == 0);
        EXPECT_ERR(ERR_ACCESS_DENIED);
    } else {
        CHECK(GetLastError() == ERR_ACCESS_DENIED);
        t_note("LockSetForegroundWindow(LSFW_LOCK) refused (not allowed to force foreground, or already locked)");
        OP(2, 0x36);
    }
    OP(2, 0x36);                                      /* never leave the session-wide lock behind */
}

/* ---- 0x38 HMValidateHandle slow path (user32!ValidateHwnd) ------------------------------------------- */
/* The returned pointer is the desktop-heap (client-mapped) object: the first qword of every USER
 * object is its handle (THRDESKHEAD.h). */
T(oneparam_0x38_validate_handle_returns_client_pointer) {
    NEED_RAW(NtUserCallOneParam);
    HWND w = k_make_window("r2ncm_vh");
    if (!w) { t_skip("no window"); return; }
    ULONG_PTR p = OP(w, 0x38);
    CHECK(p != 0);
    void *fast = k_wnd_ptr(w);
    if (fast) CHECKF((void *)p == fast, "slow path %p vs handle-table fast path %p", (void *)p, fast);
    else t_note("fast-path pointer unavailable");
    void *head = NULL;
    if (p && k_try_read_ptr((void *)p, &head)) CHECKF(LO32(head) == LO32(w), "object head handle %p vs %p", head, w);
    else t_note("object not readable at %p", (void *)p);
    U(DestroyWindow)(w);
}
T(oneparam_0x38_validate_handle_stable_pointer) {
    NEED_RAW(NtUserCallOneParam);
    HWND w = k_make_window("r2ncm_vh2");
    if (!w) { t_skip("no window"); return; }
    ULONG_PTR a = OP(w, 0x38), b = OP(w, 0x38);
    CHECK(a == b && a != 0);
    HWND w2 = k_make_window("r2ncm_vh3");
    if (w2) { ULONG_PTR c = OP(w2, 0x38); CHECK(c != 0 && c != a); U(DestroyWindow)(w2); }
    U(DestroyWindow)(w);
}
T(oneparam_0x38_validate_handle_destroyed_window) {
    NEED_RAW(NtUserCallOneParam);
    HWND w = k_make_window("r2ncm_vh4");
    if (!w) { t_skip("no window"); return; }
    ULONG_PTR live = OP(w, 0x38);
    CHECK(live != 0);
    U(DestroyWindow)(w);
    SetLastError(SENTINEL_ERR);
    ULONG_PTR dead = OP(w, 0x38);
    CHECK(dead == 0);
    t_note("destroyed handle: result %p, last error %lu", (void *)dead, GetLastError());
}
T(oneparam_0x38_validate_handle_null_and_junk) {
    NEED_RAW(NtUserCallOneParam);
    static const ULONG_PTR junk[] = { 0, 0xffff, 0x10000, 0x7fffffff };
    for (size_t i = 0; i < sizeof junk / sizeof junk[0]; i++) {
        SetLastError(SENTINEL_ERR);
        ULONG_PTR r = OP(junk[i], 0x38);
        CHECKF(r == 0, "handle 0x%llx returned %p", (unsigned long long)junk[i], (void *)r);
    }
}
T(oneparam_0x38_validate_handle_menu_object) {
    NEED_RAW(NtUserCallOneParam);
    HMENU m = U(CreateMenu)();
    if (!m) { t_skip("no menu"); return; }
    ULONG_PTR p = OP(m, 0x38);               /* not window-specific: imm32 uses it for HIMC objects */
    t_note("0x38 on an HMENU returned %p", (void *)p);
    void *head = NULL;
    if (p && k_try_read_ptr((void *)p, &head)) CHECK(LO32(head) == LO32(m));
    U(DestroyMenu)(m);
}

/* ---- 0x39 MessageBeep ------------------------------------------------------------------------------ */
T(oneparam_0x39_message_beep_returns_true) {
    NEED_RAW(NtUserCallOneParam);
    /* MessageBeep always returns 1 (sound itself is best-effort and depends on the session's sound scheme) */
    DWORD t0 = GetTickCount();
    CHECK(LO32(OP(0, 0x39)) == 1);                        /* MB_OK */
    t_info("MessageBeep(MB_OK) took %lu ms", (unsigned long)(GetTickCount() - t0));
    CHECK(U(MessageBeep)(MB_OK) != 0);
}

/* ---- 0x3b PostQuitMessage -------------------------------------------------------------------------------- */
struct pq { int code; UINT msg; WPARAM wp; int got; int again; };
static DWORD WINAPI pq_thread(LPVOID p) {
    struct pq *q = (struct pq *)p;
    HWND w = k_make_window("r2ncm_pq");
    OP((ULONG_PTR)(LONG_PTR)q->code, 0x3b);
    MSG m; memset(&m, 0, sizeof m);
    for (int i = 0; i < 100 && !q->got; i++) {
        if (U(PeekMessageA)(&m, NULL, WM_QUIT, WM_QUIT, PM_REMOVE)) { q->got = 1; q->msg = m.message; q->wp = m.wParam; }
        else Sleep(5);
    }
    q->again = U(PeekMessageA)(&m, NULL, WM_QUIT, WM_QUIT, PM_REMOVE);
    if (w) U(DestroyWindow)(w);
    return 0;
}
T(oneparam_0x3b_post_quit_message_exit_codes) {
    NEED_RAW(NtUserCallOneParam);
    static const int codes[] = { 0, 1, 77, 0x7fffffff, -5 };
    for (size_t i = 0; i < sizeof codes / sizeof codes[0]; i++) {
        struct pq q = { codes[i], 0, 0, 0, 0 };
        HANDLE th = CreateThread(NULL, 0, pq_thread, &q, 0, NULL);
        WaitForSingleObject(th, 8000); CloseHandle(th);
        CHECKF(q.got, "no WM_QUIT for exit code %d", codes[i]);
        CHECK(q.msg == WM_QUIT);
        CHECKF(LO32(q.wp) == (DWORD)codes[i], "wParam 0x%llx want 0x%lx", (unsigned long long)q.wp, (unsigned long)(DWORD)codes[i]);
        CHECK(!q.again);                       /* the quit state is consumed by retrieval */
    }
}
T(oneparam_0x3b_post_quit_message_does_not_touch_other_thread) {
    NEED_RAW(NtUserCallOneParam);
    HWND w = k_make_window("r2ncm_pq2");
    if (!w) { t_skip("no window"); return; }
    struct pq q = { 3, 0, 0, 0, 0 };
    HANDLE th = CreateThread(NULL, 0, pq_thread, &q, 0, NULL);
    WaitForSingleObject(th, 8000); CloseHandle(th);
    MSG m;
    CHECK(!U(PeekMessageA)(&m, NULL, WM_QUIT, WM_QUIT, PM_NOREMOVE));   /* quit is a per-queue flag */
    U(DestroyWindow)(w);
}

/* ---- 0x42 ReplyMessage -------------------------------------------------------------------------------------- */
static volatile LONG g_r1, g_r2, g_waited;
static volatile LRESULT g_sendres;
static HANDLE g_done;
static HWND g_replywnd;
static LRESULT CALLBACK reply_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_USER + 0x77) {
        g_r1 = (LONG)LO32(OP(0x1234, 0x42));
        g_r2 = (LONG)LO32(OP(0x4321, 0x42));          /* bit0 of qmsg flags set by the first reply -> 0 */
        g_waited = (LONG)WaitForSingleObject(g_done, 3000);
        return 0x5678;
    }
    return U(DefWindowProcA)(h, m, w, l);
}
static DWORD WINAPI sender_proc(LPVOID p) {
    g_sendres = U(SendMessageA)(g_replywnd, WM_USER + 0x77, 0, 0);
    SetEvent(g_done);
    return 0;
}
T(oneparam_0x42_reply_message_outside_send_fails) {
    NEED_RAW(NtUserCallOneParam);
    HWND w = k_make_window("r2ncm_rm");
    if (!w) { t_skip("no window"); return; }
    SetLastError(SENTINEL_ERR);
    CHECK(LO32(OP(0x99, 0x42)) == 0);                 /* pti+0x200 == NULL: no message being processed */
    CHECK(GetLastError() == SENTINEL_ERR);
    CHECK(U(ReplyMessage)(0x99) == 0);
    U(DestroyWindow)(w);
}
T(oneparam_0x42_reply_message_releases_cross_thread_sender) {
    NEED_RAW(NtUserCallOneParam);
    WNDCLASSA wc; memset(&wc, 0, sizeof wc);
    char cls[64]; snprintf(cls, sizeof cls, "r2ncm_reply_%lu", (unsigned long)GetCurrentProcessId());
    wc.lpfnWndProc = reply_proc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = cls;
    if (!U(RegisterClassA)(&wc)) { t_skip("RegisterClass failed"); return; }
    g_replywnd = U(CreateWindowExA)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls, "r2ncm_reply", WS_POPUP, -32000, -32000, 1, 1, NULL, NULL, wc.hInstance, NULL);
    if (!g_replywnd) { t_skip("no window"); U(UnregisterClassA)(cls, wc.hInstance); return; }
    g_done = CreateEventA(NULL, TRUE, FALSE, NULL);
    g_r1 = g_r2 = g_waited = -1; g_sendres = -1;
    HANDLE th = CreateThread(NULL, 0, sender_proc, NULL, 0, NULL);
    DWORD start = GetTickCount();
    while (WaitForSingleObject(g_done, 5) == WAIT_TIMEOUT && GetTickCount() - start < 6000) {
        MSG m; while (U(PeekMessageA)(&m, NULL, 0, 0, PM_REMOVE)) { U(TranslateMessage)(&m); U(DispatchMessageA)(&m); }
    }
    WaitForSingleObject(th, 3000); CloseHandle(th);
    CHECKF(g_r1 == 1, "first ReplyMessage = %ld", (long)g_r1);
    CHECKF(g_r2 == 0, "second ReplyMessage = %ld", (long)g_r2);
    CHECKF(g_waited == (LONG)WAIT_OBJECT_0, "sender was not released before the handler returned (wait=%ld)", (long)g_waited);
    CHECKF(g_sendres == 0x1234, "SendMessage returned 0x%llx, want the replied 0x1234", (unsigned long long)g_sendres);
    U(DestroyWindow)(g_replywnd);
    CloseHandle(g_done);
    U(UnregisterClassA)(cls, wc.hInstance);
}

/* ---- 0x43 SetCaretBlinkTime ------------------------------------------------------------------------------------ */
/* _SetCaretBlinkTime: needs WINSTA_WRITEATTRIBUTES (0x10) + desktop policy; stores the blink time, returns 1 */
T(oneparam_0x43_set_caret_blink_time_roundtrip) {
    NEED_RAW(NtUserCallOneParam);
    UINT orig = U(GetCaretBlinkTime)();
    SetLastError(SENTINEL_ERR);
    DWORD r = LO32(OP(777, 0x43));
    if (r == 0) { t_note("SetCaretBlinkTime refused (error %lu)", GetLastError()); return; }
    CHECK(r == 1);
    CHECK(U(GetCaretBlinkTime)() == 777);
    CHECK(LO32(OP(500, 0x43)) == 1);
    CHECK(U(GetCaretBlinkTime)() == 500);
    CHECK(U(SetCaretBlinkTime)(333) != 0);
    CHECK(U(GetCaretBlinkTime)() == 333);
    OP(orig, 0x43);
    CHECK(U(GetCaretBlinkTime)() == orig);
}
T(oneparam_0x43_blink_time_is_visible_in_serverinfo) {
    NEED_RAW(NtUserCallOneParam);
    struct k_user_shared_info si;
    if (!k_shared_info(&si) || !si.psi) { t_skip("no SERVERINFO"); return; }
    UINT orig = U(GetCaretBlinkTime)();
    unsigned long v = 0;
    if (!k_try_read_u32((char *)si.psi + 0x1378, &v)) { t_skip("SERVERINFO not readable"); return; }
    CHECKF(v == orig, "stored blink time = %lu, GetCaretBlinkTime = %u", v, orig);
    if (LO32(OP(654, 0x43)) == 1) {
        CHECK(k_try_read_u32((char *)si.psi + 0x1378, &v) && v == 654);
        OP(orig, 0x43);
    }
    CHECK(k_try_read_u32((char *)si.psi + 0x1378, &v) && v == orig);
}

/* ---- 0x44 SetDoubleClickTime ------------------------------------------------------------------------------------ */
/* _SetDoubleClickTime: 0 -> 500, > 5000 -> 5000, else as given; also derives 4*t and (4*t)/5 for the double-click size/time values */
T(oneparam_0x44_set_double_click_time_clamping) {
    NEED_RAW(NtUserCallOneParam);
    UINT orig = U(GetDoubleClickTime)();
    static const struct { ULONG_PTR in; UINT want; } cases[] = {
        { 0, 500 }, { 1, 1 }, { 200, 200 }, { 500, 500 }, { 4999, 4999 }, { 5000, 5000 }, { 5001, 5000 }, { 6000, 5000 }, { 100000, 5000 }, { 0xffffffffu, 5000 },
    };
    SetLastError(SENTINEL_ERR);
    if (LO32(OP(orig, 0x44)) == 0) { t_note("SetDoubleClickTime refused (error %lu)", GetLastError()); return; }
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        CHECKF(LO32(OP(cases[i].in, 0x44)) == 1, "set %llu", (unsigned long long)cases[i].in);
        UINT got = U(GetDoubleClickTime)();
        CHECKF(got == cases[i].want, "SetDoubleClickTime(%llu): GetDoubleClickTime = %u want %u", (unsigned long long)cases[i].in, got, cases[i].want);
    }
    OP(orig, 0x44);
    CHECK(U(GetDoubleClickTime)() == orig);
}
T(oneparam_0x44_double_click_derived_values_in_serverinfo) {
    NEED_RAW(NtUserCallOneParam);
    struct k_user_shared_info si;
    if (!k_shared_info(&si) || !si.psi) { t_skip("no SERVERINFO"); return; }
    UINT orig = U(GetDoubleClickTime)();
    if (LO32(OP(300, 0x44)) != 1) { t_note("refused"); return; }
    unsigned long a = 0, b = 0;
    if (k_try_read_u32((char *)si.psi + 0x1374, &a) && k_try_read_u32((char *)si.psi + 0x1370, &b)) {
        CHECKF(a == 1200, "derived value = %lu want 4*300", a);
        CHECKF(b == 240, "derived value = %lu want 1200/5", b);
    } else t_note("SERVERINFO not readable");
    OP(orig, 0x44);
}

/* ---- 0x45 SetMessageExtraInfo ------------------------------------------------------------------------------------- */
/* _SetMessageExtraInfo swaps the per-queue value (pq+0x198) and returns the previous one */
T(oneparam_0x45_set_message_extra_info_swaps) {
    NEED_RAW(NtUserCallOneParam);
    HWND w = k_make_window("r2ncm_extra");
    if (!w) { t_skip("no window"); return; }
    LPARAM orig = U(GetMessageExtraInfo)();
    static const LPARAM vals[] = { 0x1234, 0, (LPARAM)-1, 0x123456789abcLL };
    LPARAM prev = orig;
    for (size_t i = 0; i < sizeof vals / sizeof vals[0]; i++) {
        LPARAM r = (LPARAM)OP(vals[i], 0x45);
        CHECKF(r == prev, "step %zu returned 0x%llx want previous 0x%llx", i, (long long)r, (long long)prev);
        CHECKF(U(GetMessageExtraInfo)() == vals[i], "step %zu readback 0x%llx", i, (long long)U(GetMessageExtraInfo)());
        prev = vals[i];
    }
    CHECK(U(SetMessageExtraInfo)(0x4242) == prev);          /* public wrapper == code 0x45 */
    CHECK(U(GetMessageExtraInfo)() == 0x4242);
    OP(orig, 0x45);
    CHECK(U(GetMessageExtraInfo)() == orig);
    U(DestroyWindow)(w);
}
static DWORD WINAPI extra_probe(LPVOID p) { *(LPARAM *)p = U(GetMessageExtraInfo)(); return 0; }
T(oneparam_0x45_extra_info_is_per_queue) {
    NEED_RAW(NtUserCallOneParam);
    HWND w = k_make_window("r2ncm_extra2");
    struct mx_bg bg;
    if (!w || !mx_bg_start(&bg, 0)) { t_skip("fixture"); if (w) U(DestroyWindow)(w); mx_bg_stop(&bg); return; }
    LPARAM orig = U(GetMessageExtraInfo)();
    OP(0x5151, 0x45);
    /* the helper thread has its own queue: its value is not 0x5151 */
    struct { LPARAM v; } box = { 0 };
    HANDLE th = CreateThread(NULL, 0, extra_probe, &box, 0, NULL);
    WaitForSingleObject(th, 5000); CloseHandle(th);
    CHECKF(box.v != 0x5151, "other thread sees 0x%llx", (long long)box.v);
    OP(orig, 0x45);
    mx_bg_stop(&bg);
    U(DestroyWindow)(w);
}

/* ---- 0x49 SwapMouseButton ----------------------------------------------------------------------------------------------- */
/* _SwapMouseButton: needs a granted access (CheckGrantedAccess 0x12) and process flag 0x40000, else error 0x5b3;
 * returns the previous swap state. Re-applying the current state keeps the system unchanged. */
T(oneparam_0x49_swap_mouse_button_reapply_current) {
    NEED_RAW(NtUserCallOneParam);
    int cur = U(GetSystemMetrics)(SM_SWAPBUTTON) != 0;
    SetLastError(SENTINEL_ERR);
    DWORD r = LO32(OP(cur, 0x49)) & 0xff;
    if (r == 0 && GetLastError() == 0x5b3) { t_note("process may not change the button mapping (0x5b3)"); return; }
    CHECKF(r == (DWORD)cur, "previous state %lu, SM_SWAPBUTTON %d", (unsigned long)r, cur);
    CHECK((U(GetSystemMetrics)(SM_SWAPBUTTON) != 0) == (cur != 0));
    CHECK((U(SwapMouseButton)(cur) != 0) == (cur != 0));
}

/* ---- 0x4e IsThreadMessageQueueAttached ----------------------------------------------------------------------------------- */
T(oneparam_0x4e_is_thread_queue_attached) {
    NEED_RAW(NtUserCallOneParam);
    FARPROC pub = t_proc_in("user32.dll", "IsThreadMessageQueueAttached");
    HWND w = k_make_window("r2ncm_tq");
    struct mx_bg bg;
    if (!w || !mx_bg_start(&bg, 0)) { t_skip("fixture"); if (w) U(DestroyWindow)(w); mx_bg_stop(&bg); return; }
    CHECK(B8(OP(0, 0x4e)) == 0);                           /* tid 0: cThreads > 1 */
    CHECK(B8(OP(GetCurrentThreadId(), 0x4e)) == 1);        /* a thread shares its own queue */
    CHECK(B8(OP(bg.tid, 0x4e)) == 0);                      /* the helper has a private queue */
    if (pub) CHECK((((BOOL (WINAPI *)(DWORD))pub)(bg.tid) & 0xff) == 0);
    SetLastError(SENTINEL_ERR);
    CHECK(B8(OP(0x7ffffff0u, 0x4e)) == 0);
    EXPECT_ERR(ERR_INVALID_PARAMETER);
    mx_bg_stop(&bg);
    U(DestroyWindow)(w);
}

/* ---- documented-but-not-invoked OneParam codes ---------------------------------------------------------------------------- */
/* 0x2f DDE initialise/uninitialise, 0x30 DirectedYield (scheduling), 0x35 GetWinStationInfo, 0x37
 * LoadLocalFonts, 0x3a/0x3c..0x41 palette / client-thread / winstation connect+terminate / thinwire, 0x47
 * watermark strings, 0x48 ShowStartGlass, 0x4a WOWModuleUnload, 0x4b DwmLockScreenUpdates, 0x4c/0x4f/0x50..0x53
 * MMCSS, UI actions, DPI cache fill, numpad, TSF and shell-change-notify registration: each changes
 * session or shell wide state or is only valid with an object this test cannot make; they are reached
 * only through the inert-range tests in ranges.c. */
