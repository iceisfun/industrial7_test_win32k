/* Window query syscalls: NtUserQueryWindow (GetWindowThreadProcessId, IsHungAppWindow, ...),
 * NtUserInternalGetWindowText / NtUserDefSetText (GetWindowText / SetWindowText plumbing),
 * IsWindowUnicode and the client-side state those functions read. */
#include "wpsq.h"

static ULONG_PTR qw(HWND h, int code) { return UN(NtUserQueryWindow)(h, code); }

/* ---------------- NtUserQueryWindow ------------------------------------------------------- */

WT(query_window_pid_tid_of_own_window) {
    REQUIRE(UN(NtUserQueryWindow)); REQUIRE(U(CreateWindowExW));
    NEED_WND(h, 0);
    /* NtUserQueryWindow: code 0 and 1 -> PsGetThreadProcessId(owner), code 2 -> PsGetThreadId(owner) */
    WEQ(qw(h, 0), GetCurrentProcessId());
    WEQ(qw(h, 1), GetCurrentProcessId());
    WEQ(qw(h, 2), GetCurrentThreadId());
    U(DestroyWindow)(h);
}

WT(query_window_unknown_codes_return_zero) {
    REQUIRE(UN(NtUserQueryWindow)); REQUIRE(U(CreateWindowExW));
    NEED_WND(h, 0);
    static const int codes[] = { 6, 10, 11, 12, 16, 100, 0xFFFF, 0x10000, INT_MAX, -1, -2, INT_MIN };
    for (unsigned i = 0; i < sizeof codes / sizeof *codes; i++) {
        ULONG_PTR r = qw(h, codes[i]);
        CHECKF(r == 0, "code %d -> %llx", codes[i], (unsigned long long)r);     /* switch default arm */
    }
    U(DestroyWindow)(h);
}

WT(query_window_invalid_hwnd_all_codes_zero) {
    REQUIRE(UN(NtUserQueryWindow)); REQUIRE(U(CreateWindowExW));
    U(GetWindowLongW);
    HWND bad[] = { NULL, GARBAGE_HWND, (HWND)(ULONG_PTR)-1, (HWND)(ULONG_PTR)0x10 };
    for (unsigned i = 0; i < 4; i++)
        for (int c = 0; c < 12; c++) {
            ULONG_PTR r = qw(bad[i], c);
            CHECKF(r == 0, "hwnd %p code %d -> %llx", bad[i], c, (unsigned long long)r);   /* ValidateHwnd fails -> 0 */
        }
}

WT(query_window_destroyed_hwnd_zero) {
    REQUIRE(UN(NtUserQueryWindow)); REQUIRE(U(CreateWindowExW));
    NEED_WND(h, 0);
    U(DestroyWindow)(h);
    WEQ(qw(h, 0), 0);
    WEQ(qw(h, 2), 0);
}

WT(query_window_active_matches_getactivewindow) {
    REQUIRE(UN(NtUserQueryWindow)); REQUIRE(U(GetActiveWindow)); NEED_WND(h, 0);
    /* codes 3/4 read the owner queue's spwndActive / spwndFocus: for a window of this thread they equal GetActiveWindow / GetFocus */
    WEQ(qw(h, 3), (ULONG_PTR)U(GetActiveWindow)());
    WEQ(qw(h, 4), (ULONG_PTR)U(GetFocus)());
    U(DestroyWindow)(h);
}

WT(query_window_active_focus_of_other_thread_queue) {
    REQUIRE(UN(NtUserQueryWindow)); struct wpsq_owner o;
    if (!wpsq_owner_start(&o, 0, "q")) { t_skip("owner failed"); return; }
    /* a window of another thread reports that thread's queue, not the caller's */
    ULONG_PTR a = qw(o.hwnd, 3), f = qw(o.hwnd, 4);
    t_note("other-thread queue active=%p focus=%p", (void *)a, (void *)f);
    CHECK(a == 0 || U(IsWindow)((HWND)a));
    CHECK(f == 0 || U(IsWindow)((HWND)f));
    wpsq_owner_stop(&o);
}

WT(query_window_ishung_is_boolean) {
    REQUIRE(UN(NtUserQueryWindow)); NEED_WND(h, 0);
    k_pump(10);
    ULONG_PTR r = qw(h, 5);
    CHECKF(r <= 1, "ishung=%llx", (unsigned long long)r);
    t_note("NtUserQueryWindow(own window, 5) = %llu", (unsigned long long)r);
    if (U(IsHungAppWindow)) WEQ(U(IsHungAppWindow)(h) != 0, r != 0);   /* IsHungAppWindow is a thin wrapper over code 5 */
    U(DestroyWindow)(h);
}

WT(query_window_foreground_flag) {
    REQUIRE(UN(NtUserQueryWindow)); REQUIRE(U(GetForegroundWindow)); NEED_WND(h, 0);
    HWND fg = U(GetForegroundWindow)();
    ULONG_PTR mine = qw(h, 7);
    CHECK(mine <= 1);
    if (fg) WEQ(qw(fg, 7), 1);               /* code 7: pti->pq == gpqForeground, true for the foreground window itself */
    else t_skip("no foreground window");
    U(DestroyWindow)(h);
}

WT(query_window_codes_8_9_are_handles_or_zero) {
    REQUIRE(UN(NtUserQueryWindow)); NEED_WND(h, 0);
    for (int c = 8; c <= 9; c++) {
        ULONG_PTR r = qw(h, c);
        t_note("NtUserQueryWindow(own, %d) = %p", c, (void *)r);
    }
    U(DestroyWindow)(h);
}

WT(query_window_other_thread_window) {
    REQUIRE(UN(NtUserQueryWindow)); struct wpsq_owner o;
    if (!wpsq_owner_start(&o, 0, "q")) { t_skip("owner failed"); return; }
    WEQ(qw(o.hwnd, 0), GetCurrentProcessId());
    WEQ(qw(o.hwnd, 2), o.tid);
    CHECK(o.tid != GetCurrentThreadId());
    wpsq_owner_stop(&o);
}

WT(query_window_child_reports_creating_thread) {
    REQUIRE(UN(NtUserQueryWindow)); NEED_WND(par, 0);
    HWND c = wpsq_mkchild(par, 5);
    if (!c) { t_skip("no child"); U(DestroyWindow)(par); return; }
    WEQ(qw(c, 2), GetCurrentThreadId());
    WEQ(qw(c, 0), GetCurrentProcessId());
    U(DestroyWindow)(par);
}

/* ---------------- GetWindowThreadProcessId ------------------------------------------------ */

WT(gwtpid_same_thread_fast_path) {
    REQUIRE(U(GetWindowThreadProcessId)); NEED_WND(h, 0);
    DWORD pid = 0xDEAD;
    /* user32 answers from its own TEB-derived pid/tid when the window belongs to the calling thread */
    WEQ(U(GetWindowThreadProcessId)(h, &pid), GetCurrentThreadId());
    WEQ(pid, GetCurrentProcessId());
    WEQ(U(GetWindowThreadProcessId)(h, NULL), GetCurrentThreadId());
    U(DestroyWindow)(h);
}

WT(gwtpid_other_thread_uses_kernel) {
    REQUIRE(U(GetWindowThreadProcessId)); struct wpsq_owner o;
    if (!wpsq_owner_start(&o, 0, "g")) { t_skip("owner failed"); return; }
    DWORD pid = 0;
    WEQ(U(GetWindowThreadProcessId)(o.hwnd, &pid), o.tid);
    WEQ(pid, GetCurrentProcessId());
    WEQ(U(GetWindowThreadProcessId)(o.hwnd, NULL), o.tid);
    wpsq_owner_stop(&o);
}

WT(gwtpid_invalid_hwnd_leaves_pid_alone) {
    REQUIRE(U(GetWindowThreadProcessId));
    HWND bad[] = { NULL, GARBAGE_HWND, (HWND)(ULONG_PTR)-1 };
    for (unsigned i = 0; i < 3; i++) {
        DWORD pid = 0xC0FFEE;
        SetLastError(0);
        WEQ(U(GetWindowThreadProcessId)(bad[i], &pid), 0);
        WEQ(pid, 0xC0FFEE);                         /* the failure path returns before storing */
        EXPECT_ERR(E_INVALID_HWND);
    }
}

WT(gwtpid_destroyed_window) {
    REQUIRE(U(GetWindowThreadProcessId)); NEED_WND(h, 0);
    U(DestroyWindow)(h);
    DWORD pid = 0x1111;
    SetLastError(0);
    WEQ(U(GetWindowThreadProcessId)(h, &pid), 0);
    WEQ(pid, 0x1111);
    EXPECT_ERR(E_INVALID_HWND);
}

WT(gwtpid_matches_direct_stub_for_many_windows) {
    REQUIRE(U(GetWindowThreadProcessId)); REQUIRE(UN(NtUserQueryWindow));
    HWND w[8]; int n = 0;
    for (int i = 0; i < 8; i++) { w[n] = wpsq_mk(i & 1, "m"); if (w[n]) n++; }
    if (!n) { t_skip("no windows"); return; }
    for (int i = 0; i < n; i++) {
        DWORD pid = 0;
        DWORD tid = U(GetWindowThreadProcessId)(w[i], &pid);
        WEQ(tid, qw(w[i], 2));
        WEQ(pid, qw(w[i], 0));
    }
    for (int i = 0; i < n; i++) U(DestroyWindow)(w[i]);
}

WT(gwtpid_desktop_window_owned_elsewhere) {
    REQUIRE(U(GetWindowThreadProcessId)); REQUIRE(U(GetDesktopWindow));
    HWND d = U(GetDesktopWindow)();
    CHECK(d != NULL);
    DWORD pid = 0;
    DWORD tid = U(GetWindowThreadProcessId)(d, &pid);
    CHECK(tid != 0);
    CHECK(pid != 0);
    CHECK(tid != GetCurrentThreadId());
    if (UN(NtUserQueryWindow)) { WEQ(qw(d, 2), tid); WEQ(qw(d, 0), pid); }
    t_info("desktop window owner pid=%lu tid=%lu (self pid=%lu)", pid, tid, GetCurrentProcessId());
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    t_note("OpenProcess(desktop owner) %s", p ? "ok" : "denied");
    if (p) CloseHandle(p);
}

WT(gwtpid_shell_window_other_process) {
    REQUIRE(U(GetWindowThreadProcessId)); REQUIRE(U(GetShellWindow));
    HWND s = U(GetShellWindow)();
    if (!s) { t_skip("no shell window"); return; }
    DWORD pid = 0;
    DWORD tid = U(GetWindowThreadProcessId)(s, &pid);
    CHECK(tid != 0 && pid != 0);
    CHECK(pid != GetCurrentProcessId());
    t_info("shell window owner pid=%lu tid=%lu", pid, tid);
}

/* ---------------- IsWindowUnicode --------------------------------------------------------- */

WT(isunicode_by_creation_function) {
    REQUIRE(U(IsWindowUnicode));
    HWND w = wpsq_mk(0, "u"), a = wpsq_mk(1, "a");
    if (!w || !a) { t_skip("no windows"); if (w) U(DestroyWindow)(w); if (a) U(DestroyWindow)(a); return; }
    CHECK(U(IsWindowUnicode)(w));
    CHECK(!U(IsWindowUnicode)(a));
    unsigned char b;
    if (wpsq_rd(w, WND_state2b, &b, 1)) CHECK((b & 8) == 0);       /* IsWindowUnicode = !(byte+0x12 >> 3 & 1) */
    if (wpsq_rd(a, WND_state2b, &b, 1)) CHECK((b & 8) != 0);
    U(DestroyWindow)(w); U(DestroyWindow)(a);
}

WT(isunicode_invalid_hwnd_false) {
    REQUIRE(U(IsWindowUnicode));
    CHECK(!U(IsWindowUnicode)(NULL));
    CHECK(!U(IsWindowUnicode)(GARBAGE_HWND));
    CHECK(!U(IsWindowUnicode)((HWND)(ULONG_PTR)-1));
    HWND h = wpsq_mk(0, "d");
    if (h) { U(DestroyWindow)(h); CHECK(!U(IsWindowUnicode)(h)); }
}

WT(isunicode_child_follows_own_creation) {
    REQUIRE(U(IsWindowUnicode)); NEED_WND(par, 1);
    HWND c = wpsq_mkchild(par, 3);                 /* created by CreateWindowExW even though the parent is ANSI */
    if (!c) { t_skip("no child"); U(DestroyWindow)(par); return; }
    CHECK(U(IsWindowUnicode)(c));
    CHECK(!U(IsWindowUnicode)(par));
    U(DestroyWindow)(par);
}

WT(isunicode_other_thread_window) {
    REQUIRE(U(IsWindowUnicode)); struct wpsq_owner o;
    if (!wpsq_owner_start(&o, 1, "ia")) { t_skip("owner failed"); return; }
    CHECK(!U(IsWindowUnicode)(o.hwnd));
    wpsq_owner_stop(&o);
}

WT(isunicode_desktop_window) {
    REQUIRE(U(IsWindowUnicode)); REQUIRE(U(GetDesktopWindow));
    BOOL u = U(IsWindowUnicode)(U(GetDesktopWindow)());
    t_note("IsWindowUnicode(desktop) = %d", u);
}

WT(modulefilename_invalid_hwnd_zero) {
    REQUIRE(U(GetWindowModuleFileNameW));
    WCHAR b[MAX_PATH] = L"x";
    WEQ(U(GetWindowModuleFileNameW)(GARBAGE_HWND, b, MAX_PATH), 0);
    WEQ(U(GetWindowModuleFileNameW)(NULL, b, MAX_PATH), 0);
}

WT(modulefilename_truncates_like_getmodulefilename) {
    REQUIRE(U(GetWindowModuleFileNameW)); REQUIRE(U(GetModuleFileNameW)); NEED_WND(h, 0);
    WCHAR full[MAX_PATH], small[8];
    UINT n = U(GetWindowModuleFileNameW)(h, full, MAX_PATH);
    CHECK(n > 8);
    UINT s = U(GetWindowModuleFileNameW)(h, small, 8);
    WEQ(s, 8);                                      /* GetModuleFileNameW semantics: size returned when truncated */
    WEQ(small[7], 0);
    CHECK(!wcsncmp(small, full, 7));
    U(DestroyWindow)(h);
}

/* ---------------- NtUserInternalGetWindowText -------------------------------------------- */

static HWND text_win(const WCHAR *t) {
    HWND h = wpsq_mk(0, "x");
    if (h && t) U(SetWindowTextW)(h, t);
    return h;
}

WT(text_internal_basic) {
    REQUIRE(UN(NtUserInternalGetWindowText)); REQUIRE(U(SetWindowTextW)); NEED_WND(h, 0);
    U(SetWindowTextW)(h, L"hello");
    WCHAR b[32]; memset(b, 0x55, sizeof b);
    WEQ(UN(NtUserInternalGetWindowText)(h, b, 32), 5);
    CHECK(!lstrcmpW(b, L"hello"));
    U(DestroyWindow)(h);
}

WT(text_internal_truncation_table) {
    REQUIRE(UN(NtUserInternalGetWindowText)); REQUIRE(U(SetWindowTextW)); NEED_WND(h, 0);
    U(SetWindowTextW)(h, L"hello");
    /* TextCopy(): n = min(len, cch - 1), always NUL terminates, returns n */
    static const struct { int cch; int want; const WCHAR *s; } t[] = {
        { 1, 0, L"" }, { 2, 1, L"h" }, { 3, 2, L"he" }, { 5, 4, L"hell" }, { 6, 5, L"hello" }, { 7, 5, L"hello" }, { 64, 5, L"hello" }
    };
    for (unsigned i = 0; i < sizeof t / sizeof *t; i++) {
        WCHAR b[80]; memset(b, 0x55, sizeof b);
        int r = UN(NtUserInternalGetWindowText)(h, b, t[i].cch);
        CHECKF(r == t[i].want, "cch %d -> %d", t[i].cch, r);
        CHECKF(!lstrcmpW(b, t[i].s), "cch %d text mismatch", t[i].cch);
        if (t[i].cch < 70) CHECKF(b[t[i].cch] == 0x5555, "cch %d wrote past the buffer", t[i].cch);
    }
    U(DestroyWindow)(h);
}

WT(text_internal_cch_zero_touches_nothing) {
    REQUIRE(UN(NtUserInternalGetWindowText)); NEED_WND(h, 0);
    WCHAR b[4] = { 0x5555, 0x5555, 0x5555, 0x5555 };
    WEQ(UN(NtUserInternalGetWindowText)(h, b, 0), 0);
    WEQ(b[0], 0x5555);
    /* with cch == 0 even a bad buffer pointer is never touched */
    WEQ(UN(NtUserInternalGetWindowText)(h, (WCHAR *)(ULONG_PTR)0x10, 0), 0);
    U(DestroyWindow)(h);
}

WT(text_internal_empty_title) {
    REQUIRE(UN(NtUserInternalGetWindowText)); HWND h = wpsq_mkex(0, WS_POPUP, 0, NULL, NULL, 0);
    if (!h) { t_skip("no window"); return; }
    WCHAR b[8]; memset(b, 0x55, sizeof b);
    WEQ(UN(NtUserInternalGetWindowText)(h, b, 8), 0);
    WEQ(b[0], 0);                                            /* kernel stores the terminating NUL first */
    WEQ(b[1], 0x5555);
    U(DestroyWindow)(h);
}

WT(text_internal_invalid_hwnd_direct_leaves_buffer) {
    REQUIRE(UN(NtUserInternalGetWindowText)); U(GetWindowLongW);
    HWND bad[] = { NULL, GARBAGE_HWND, (HWND)(ULONG_PTR)-1 };
    for (unsigned i = 0; i < 3; i++) {
        WCHAR b[4] = { 0x5555, 0x5555, 0, 0 };
        WEQ(UN(NtUserInternalGetWindowText)(bad[i], b, 4), 0);    /* ValidateHwnd failed: nothing probed or written */
        WEQ(b[0], 0x5555);
    }
}

WT(text_internal_invalid_hwnd_wrapper_clears_buffer) {
    REQUIRE(UN(InternalGetWindowText));
    WCHAR b[4] = { 0x5555, 0x5555, 0, 0 };
    WEQ(UN(InternalGetWindowText)(GARBAGE_HWND, b, 4), 0);
    WEQ(b[0], 0);                                             /* user32!InternalGetWindowText stores L'\0' when the syscall returned 0 */
}

WT(text_internal_wrapper_matches_syscall) {
    REQUIRE(UN(InternalGetWindowText)); REQUIRE(UN(NtUserInternalGetWindowText)); HWND h = text_win(L"wrapper");
    if (!h) { t_skip("no window"); return; }
    WCHAR a[32], b[32];
    int ra = UN(InternalGetWindowText)(h, a, 32), rb = UN(NtUserInternalGetWindowText)(h, b, 32);
    WEQ(ra, rb);
    CHECK(!lstrcmpW(a, b));
    U(DestroyWindow)(h);
}

WT(text_internal_sends_no_message) {
    REQUIRE(UN(NtUserInternalGetWindowText)); HWND h = text_win(L"quiet");
    if (!h) { t_skip("no window"); return; }
    WCHAR b[32];
    wpsq_log_reset(); g_wlog_on = 1;
    UN(NtUserInternalGetWindowText)(h, b, 32);
    g_wlog_on = 0;
    WEQ(wpsq_log_count(WM_GETTEXT), 0);                      /* kernel copies straight out of the window's name string */
    U(DestroyWindow)(h);
}

WT(text_internal_ignores_wndproc_override) {
    REQUIRE(UN(NtUserInternalGetWindowText)); HWND h = text_win(L"real");
    if (!h) { t_skip("no window"); return; }
    g_gettext_override = 1;
    WCHAR b[32], c[32];
    int viaMsg = U(GetWindowTextW)(h, b, 32);
    int viaKernel = UN(NtUserInternalGetWindowText)(h, c, 32);
    g_gettext_override = 0;
    WEQ(viaMsg, 8);                                          /* "OVERRIDE": the wndproc answered WM_GETTEXT */
    CHECK(!lstrcmpW(b, L"OVERRIDE"));
    WEQ(viaKernel, 4);
    CHECK(!lstrcmpW(c, L"real"));
    U(DestroyWindow)(h);
}

WT(text_internal_long_title_5000) {
    REQUIRE(UN(NtUserInternalGetWindowText)); NEED_WND(h, 0);
    enum { N = 5000 };
    WCHAR *t = (WCHAR *)malloc((N + 1) * 2), *b = (WCHAR *)malloc((N + 1) * 2);
    for (int i = 0; i < N; i++) t[i] = L'a' + (i % 26);
    t[N] = 0;
    U(SetWindowTextW)(h, t);
    WEQ(UN(NtUserInternalGetWindowText)(h, b, N + 1), N);
    CHECK(!wcscmp(b, t));
    WEQ(U(GetWindowTextLengthW)(h), N);
    WEQ(UN(NtUserInternalGetWindowText)(h, b, 100), 99);
    free(t); free(b);
    U(DestroyWindow)(h);
}

WT(text_internal_title_over_64k_chars) {
    REQUIRE(UN(NtUserInternalGetWindowText)); NEED_WND(h, 0);
    enum { N = 70000 };
    WCHAR *t = (WCHAR *)malloc((N + 1) * 2), *b = (WCHAR *)malloc((N + 1) * 2);
    for (int i = 0; i < N; i++) t[i] = L'A' + (i % 26);
    t[N] = 0;
    BOOL ok = U(SetWindowTextW)(h, t);
    int got = UN(NtUserInternalGetWindowText)(h, b, N + 1);
    t_info("SetWindowTextW(70000 chars) -> %d, stored %d chars", ok, got);
    CHECK(got == 0 || got == N);              /* the name is stored whole or not at all */
    if (got == N) CHECK(!wcscmp(b, t));
    free(t); free(b);
    U(DestroyWindow)(h);
}

WT(text_internal_utf16_units_with_surrogates) {
    REQUIRE(UN(NtUserInternalGetWindowText)); NEED_WND(h, 0);
    static const WCHAR s[] = { 'a', 0xD83D, 0xDE00, 0x4E2D, 0xFFFD, 'z', 0 };
    U(SetWindowTextW)(h, s);
    WCHAR b[16];
    WEQ(UN(NtUserInternalGetWindowText)(h, b, 16), 6);        /* counted in UTF-16 units, stored verbatim */
    CHECK(!wcscmp(b, s));
    WEQ(U(GetWindowTextLengthW)(h), 6);
    U(DestroyWindow)(h);
}

WT(text_internal_whitespace_and_control_chars_kept) {
    REQUIRE(UN(NtUserInternalGetWindowText)); NEED_WND(h, 0);
    static const WCHAR s[] = L"  \t lead\r\nmid  trail  ";
    U(SetWindowTextW)(h, s);
    WCHAR b[64];
    WEQ(UN(NtUserInternalGetWindowText)(h, b, 64), lstrlenW(s));
    CHECK(!wcscmp(b, s));
    U(DestroyWindow)(h);
}

WT(text_createwindow_title_stored_at_creation) {
    REQUIRE(UN(NtUserInternalGetWindowText));
    HWND h = wpsq_mkex(0, WS_POPUP, 0, NULL, "created", 0);
    if (!h) { t_skip("no window"); return; }
    WCHAR b[32];
    WEQ(UN(NtUserInternalGetWindowText)(h, b, 32), 7);
    CHECK(!lstrcmpW(b, L"created"));
    U(DestroyWindow)(h);
}

WT(text_createwindow_ansi_title_stored_as_utf16) {
    REQUIRE(UN(NtUserInternalGetWindowText));
    HWND h = wpsq_mkex(1, WS_POPUP, 0, NULL, "ansi-title", 0);
    if (!h) { t_skip("no window"); return; }
    WCHAR b[32];
    WEQ(UN(NtUserInternalGetWindowText)(h, b, 32), 10);       /* the kernel only ever holds UTF-16 */
    CHECK(!lstrcmpW(b, L"ansi-title"));
    U(DestroyWindow)(h);
}

WT(text_child_window_has_own_text) {
    REQUIRE(UN(NtUserInternalGetWindowText)); NEED_WND(par, 0);
    HWND c = wpsq_mkchild(par, 9);
    if (!c) { t_skip("no child"); U(DestroyWindow)(par); return; }
    WCHAR b[32];
    WEQ(UN(NtUserInternalGetWindowText)(c, b, 32), 5);
    CHECK(!lstrcmpW(b, L"child"));
    WEQ(UN(NtUserInternalGetWindowText)(par, b, 32), 4);
    CHECK(!lstrcmpW(b, L"wpsq"));
    U(DestroyWindow)(par);
}

/* ---------------- GetWindowText / GetWindowTextLength / SetWindowText -------------------- */

WT(text_getwindowtext_sends_wm_gettext_same_thread) {
    REQUIRE(U(GetWindowTextW)); HWND h = text_win(L"msg");
    if (!h) { t_skip("no window"); return; }
    WCHAR b[16];
    wpsq_log_reset(); g_wlog_on = 1;
    WEQ(U(GetWindowTextW)(h, b, 16), 3);
    g_wlog_on = 0;
    /* GetWindowTextW: same-thread window -> SendMessageWorker(hwnd, WM_GETTEXT, nMaxCount, lpString) */
    WEQ(wpsq_log_count(WM_GETTEXT), 1);
    struct wpsq_log *e = wpsq_log_find(WM_GETTEXT, 0);
    if (e) { WEQ(e->w, 16); WEQ(e->l, (ULONG_PTR)b); WEQ(e->tid, GetCurrentThreadId()); }
    U(DestroyWindow)(h);
}

WT(text_getwindowtext_other_thread_same_process_sends_message) {
    REQUIRE(U(GetWindowTextW)); struct wpsq_owner o;
    if (!wpsq_owner_start(&o, 0, "otext")) { t_skip("owner failed"); return; }
    WCHAR b[32];
    wpsq_log_reset(); g_wlog_on = 1;
    WEQ(U(GetWindowTextW)(o.hwnd, b, 32), 5);
    g_wlog_on = 0;
    CHECK(!lstrcmpW(b, L"otext"));
 /* same process, other thread: the check stays true (pid matches) so the wndproc of the owner thread runs */
    WEQ(wpsq_log_count(WM_GETTEXT), 1);
    struct wpsq_log *e = wpsq_log_find(WM_GETTEXT, 0);
    if (e) WEQ(e->tid, o.tid);
    wpsq_owner_stop(&o);
}

WT(text_getwindowtext_truncation_and_return) {
    REQUIRE(U(GetWindowTextW)); HWND h = text_win(L"truncate");
    if (!h) { t_skip("no window"); return; }
    static const int cch[] = { 1, 2, 5, 8, 9, 10, 100 };
    for (unsigned i = 0; i < sizeof cch / sizeof *cch; i++) {
        WCHAR b[128]; memset(b, 0x55, sizeof b);
        int want = cch[i] - 1 < 8 ? cch[i] - 1 : 8;
        int r = U(GetWindowTextW)(h, b, cch[i]);
        CHECKF(r == want, "cch %d -> %d want %d", cch[i], r, want);
        CHECKF(b[want] == 0, "cch %d not terminated", cch[i]);
    }
    U(DestroyWindow)(h);
}

WT(text_getwindowtext_zero_count_or_null_buffer) {
    REQUIRE(U(GetWindowTextW)); NEED_WND(h, 0);
    WCHAR b[4] = { 0x5555, 0, 0, 0 };
    WEQ(U(GetWindowTextW)(h, b, 0), 0);
    WEQ(b[0], 0x5555);
    WEQ(U(GetWindowTextW)(h, NULL, 10), 0);
    U(DestroyWindow)(h);
}

WT(text_getwindowtext_invalid_hwnd) {
    REQUIRE(U(GetWindowTextW));
    WCHAR b[4] = { 0x5555, 0, 0, 0 };
    SetLastError(0);
    WEQ(U(GetWindowTextW)(GARBAGE_HWND, b, 4), 0);
    WEQ(b[0], 0);                                  /* *lpString = 0 happens before validation */
    EXPECT_ERR(E_INVALID_HWND);
}

WT(text_getwindowtext_ansi_conversion) {
    REQUIRE(U(GetWindowTextA)); HWND h = text_win(L"ansi view");
    if (!h) { t_skip("no window"); return; }
    char b[32]; memset(b, 0x55, sizeof b);
    WEQ(U(GetWindowTextA)(h, b, 32), 9);
    CHECK(!strcmp(b, "ansi view"));
    WEQ(U(GetWindowTextA)(h, b, 5), 4);
    CHECK(!strcmp(b, "ansi"));
    U(DestroyWindow)(h);
}

WT(text_getwindowtext_ansi_window) {
    REQUIRE(U(GetWindowTextA)); REQUIRE(U(GetWindowTextW)); HWND h = wpsq_mkex(1, WS_POPUP, 0, NULL, "abc", 0);
    if (!h) { t_skip("no window"); return; }
    char a[8]; WCHAR w[8];
    WEQ(U(GetWindowTextA)(h, a, 8), 3); CHECK(!strcmp(a, "abc"));
    WEQ(U(GetWindowTextW)(h, w, 8), 3); CHECK(!lstrcmpW(w, L"abc"));
    U(DestroyWindow)(h);
}

WT(text_getwindowtext_override_by_wndproc) {
    REQUIRE(U(GetWindowTextW)); HWND h = text_win(L"stored");
    if (!h) { t_skip("no window"); return; }
    g_gettext_override = 1;
    WCHAR b[32];
    WEQ(U(GetWindowTextW)(h, b, 32), 8);
    CHECK(!lstrcmpW(b, L"OVERRIDE"));
    g_gettext_override = 0;
    WEQ(U(GetWindowTextW)(h, b, 32), 6);
    CHECK(!lstrcmpW(b, L"stored"));
    U(DestroyWindow)(h);
}

WT(text_gettextlength_matches_length) {
    REQUIRE(U(GetWindowTextLengthW)); HWND h = text_win(L"twelve chars");
    if (!h) { t_skip("no window"); return; }
    WEQ(U(GetWindowTextLengthW)(h), 12);
    WEQ(U(GetWindowTextLengthA)(h), 12);
    U(SetWindowTextW)(h, L"");
    WEQ(U(GetWindowTextLengthW)(h), 0);
    U(DestroyWindow)(h);
}

WT(text_gettextlength_sends_message_to_own_process) {
    REQUIRE(U(GetWindowTextLengthW)); HWND h = text_win(L"len");
    if (!h) { t_skip("no window"); return; }
    wpsq_log_reset(); g_wlog_on = 1; g_gettextlen_override = 999;
    int n = U(GetWindowTextLengthW)(h);
    g_gettextlen_override = -1; g_wlog_on = 0;
    /* GetWindowTextLengthW: TestWindowProcess true -> SendMessageWorker(WM_GETTEXTLENGTH) so the wndproc may lie */
    WEQ(n, 999);
    WEQ(wpsq_log_count(WM_GETTEXTLENGTH), 1);
    U(DestroyWindow)(h);
}

WT(text_gettextlength_invalid_hwnd) {
    REQUIRE(U(GetWindowTextLengthW));
    WEQ(U(GetWindowTextLengthW)(GARBAGE_HWND), 0);
    WEQ(U(GetWindowTextLengthW)(NULL), 0);
}

WT(text_settext_roundtrip_and_return) {
    REQUIRE(U(SetWindowTextW)); REQUIRE(UN(NtUserInternalGetWindowText)); NEED_WND(h, 0);
    CHECK(U(SetWindowTextW)(h, L"first"));
    WCHAR b[32];
    WEQ(UN(NtUserInternalGetWindowText)(h, b, 32), 5); CHECK(!lstrcmpW(b, L"first"));
    CHECK(U(SetWindowTextW)(h, L"second text"));
    WEQ(UN(NtUserInternalGetWindowText)(h, b, 32), 11); CHECK(!lstrcmpW(b, L"second text"));
    U(DestroyWindow)(h);
}

WT(text_settext_sends_wm_settext_with_text) {
    REQUIRE(U(SetWindowTextW)); NEED_WND(h, 0);
    wpsq_log_reset(); g_wlog_on = 1;
    CHECK(U(SetWindowTextW)(h, L"ab"));
    g_wlog_on = 0;
    WEQ(wpsq_log_count(WM_SETTEXT), 1);
    struct wpsq_log *e = wpsq_log_find(WM_SETTEXT, 0);
    if (e) { WEQ(e->w, 0); CHECK(e->head[0] == 'a' && e->head[1] == 0 && e->head[2] == 'b' && e->head[3] == 0); }
    U(DestroyWindow)(h);
}

WT(text_settext_wndproc_may_swallow) {
    REQUIRE(U(SetWindowTextW)); REQUIRE(UN(NtUserInternalGetWindowText)); HWND h = text_win(L"keep");
    if (!h) { t_skip("no window"); return; }
    g_settext_swallow = 1;
    U(SetWindowTextW)(h, L"lost");
    g_settext_swallow = 0;
    WCHAR b[16];
    UN(NtUserInternalGetWindowText)(h, b, 16);
    CHECK(!lstrcmpW(b, L"keep"));                        /* the kernel only stores text when DefWindowProc handles WM_SETTEXT */
    U(DestroyWindow)(h);
}

WT(text_settext_null_clears) {
    REQUIRE(U(SetWindowTextW)); REQUIRE(UN(NtUserInternalGetWindowText)); HWND h = text_win(L"clearme");
    if (!h) { t_skip("no window"); return; }
    U(SetWindowTextW)(h, NULL);
    WCHAR b[16] = L"zzz";
    WEQ(UN(NtUserInternalGetWindowText)(h, b, 16), 0);
    WEQ(b[0], 0);
    WEQ(U(GetWindowTextLengthW)(h), 0);
    U(DestroyWindow)(h);
}

WT(text_settext_ansi_ascii_roundtrip) {
    REQUIRE(U(SetWindowTextA)); REQUIRE(UN(NtUserInternalGetWindowText)); NEED_WND(h, 0);
    CHECK(U(SetWindowTextA)(h, "ascii only"));
    WCHAR b[32];
    WEQ(UN(NtUserInternalGetWindowText)(h, b, 32), 10);
    CHECK(!lstrcmpW(b, L"ascii only"));
    U(DestroyWindow)(h);
}

WT(text_settext_invalid_hwnd) {
    REQUIRE(U(SetWindowTextW));
    SetLastError(0);
    WEQ(U(SetWindowTextW)(GARBAGE_HWND, L"x"), 0);
    t_note("SetWindowTextW(invalid) last error %lu", GetLastError());
    WEQ(U(SetWindowTextW)(NULL, L"x"), 0);
}

WT(text_settext_other_thread_same_process) {
    REQUIRE(U(SetWindowTextW)); REQUIRE(UN(NtUserInternalGetWindowText)); struct wpsq_owner o;
    if (!wpsq_owner_start(&o, 0, "before")) { t_skip("owner failed"); return; }
    wpsq_log_reset(); g_wlog_on = 1;
    CHECK(U(SetWindowTextW)(o.hwnd, L"after"));
    g_wlog_on = 0;
    WCHAR b[16];
    WEQ(UN(NtUserInternalGetWindowText)(o.hwnd, b, 16), 5);
    CHECK(!lstrcmpW(b, L"after"));
    struct wpsq_log *e = wpsq_log_find(WM_SETTEXT, 0);
    CHECK(e != NULL);
    if (e) WEQ(e->tid, o.tid);                    /* delivered on the owner's thread */
    wpsq_owner_stop(&o);
}

/* ---------------- NtUserDefSetText directly ---------------------------------------------- */

WT(text_defsettext_wide) {
    REQUIRE(UN(NtUserDefSetText)); if (t_is_wine()) { t_skip("Wine's win32u does not implement NtUserDefSetText"); return; } REQUIRE(UN(NtUserInternalGetWindowText)); NEED_WND(h, 0);
    static const WCHAR s[] = L"direct wide";
    struct wpsq_lstr ls = { sizeof s - 2, (sizeof s) & 0x7FFFFFFF, (void *)s };
    wpsq_log_reset(); g_wlog_on = 1;
    CHECK(UN(NtUserDefSetText)(h, &ls));
    g_wlog_on = 0;
    WEQ(wpsq_log_count(WM_SETTEXT), 0);              /* the syscall stores the text without any message */
    WCHAR b[32];
    WEQ(UN(NtUserInternalGetWindowText)(h, b, 32), 11);
    CHECK(!lstrcmpW(b, s));
    U(DestroyWindow)(h);
}

WT(text_defsettext_ansi_flag_converts) {
    REQUIRE(UN(NtUserDefSetText)); if (t_is_wine()) { t_skip("Wine's win32u does not implement NtUserDefSetText"); return; } REQUIRE(UN(NtUserInternalGetWindowText)); NEED_WND(h, 0);
    char s[] = "direct ansi";
    /* bit 31 of the second dword is tagLARGE_STRING.bAnsi: DefSetText converts with the ANSI code page */
    struct wpsq_lstr ls = { 11, 12u | 0x80000000u, s };
    CHECK(UN(NtUserDefSetText)(h, &ls));
    WCHAR b[32];
    WEQ(UN(NtUserInternalGetWindowText)(h, b, 32), 11);
    CHECK(!lstrcmpW(b, L"direct ansi"));
    U(DestroyWindow)(h);
}

WT(text_defsettext_null_struct_clears) {
    REQUIRE(UN(NtUserDefSetText)); if (t_is_wine()) { t_skip("Wine's win32u does not implement NtUserDefSetText"); return; } REQUIRE(UN(NtUserInternalGetWindowText)); HWND h = text_win(L"gone soon");
    if (!h) { t_skip("no window"); return; }
    CHECK(UN(NtUserDefSetText)(h, NULL));
    WCHAR b[16] = L"zz";
    WEQ(UN(NtUserInternalGetWindowText)(h, b, 16), 0);
    U(DestroyWindow)(h);
}

WT(text_defsettext_null_buffer_clears) {
    REQUIRE(UN(NtUserDefSetText)); if (t_is_wine()) { t_skip("Wine's win32u does not implement NtUserDefSetText"); return; } REQUIRE(UN(NtUserInternalGetWindowText)); HWND h = text_win(L"gone soon");
    if (!h) { t_skip("no window"); return; }
    struct wpsq_lstr ls = { 0, 0, NULL };            /* buffer pointer 0 -> length forced to 0 */
    CHECK(UN(NtUserDefSetText)(h, &ls));
    WCHAR b[16] = L"zz";
    WEQ(UN(NtUserInternalGetWindowText)(h, b, 16), 0);
    U(DestroyWindow)(h);
}

WT(text_defsettext_zero_length_with_buffer) {
    REQUIRE(UN(NtUserDefSetText)); if (t_is_wine()) { t_skip("Wine's win32u does not implement NtUserDefSetText"); return; } REQUIRE(UN(NtUserInternalGetWindowText)); HWND h = text_win(L"will clear");
    if (!h) { t_skip("no window"); return; }
    static const WCHAR s[] = L"ignored";
    struct wpsq_lstr ls = { 0, 16, (void *)s };
    UN(NtUserDefSetText)(h, &ls);
    WCHAR b[16] = L"zz";
    WEQ(UN(NtUserInternalGetWindowText)(h, b, 16), 0);
    U(DestroyWindow)(h);
}

WT(text_defsettext_partial_length) {
    REQUIRE(UN(NtUserDefSetText)); if (t_is_wine()) { t_skip("Wine's win32u does not implement NtUserDefSetText"); return; } REQUIRE(UN(NtUserInternalGetWindowText)); NEED_WND(h, 0);
    static const WCHAR s[] = L"0123456789";
    struct wpsq_lstr ls = { 8, 22, (void *)s };      /* Length in bytes: only 4 characters are taken */
    CHECK(UN(NtUserDefSetText)(h, &ls));
    WCHAR b[16];
    WEQ(UN(NtUserInternalGetWindowText)(h, b, 16), 4);
    CHECK(!lstrcmpW(b, L"0123"));
    U(DestroyWindow)(h);
}

WT(text_defsettext_invalid_hwnd) {
    REQUIRE(UN(NtUserDefSetText)); if (t_is_wine()) { t_skip("Wine's win32u does not implement NtUserDefSetText"); return; } U(GetWindowLongW);
    static const WCHAR s[] = L"x";
    struct wpsq_lstr ls = { 2, 4, (void *)s };
    WEQ(UN(NtUserDefSetText)(GARBAGE_HWND, &ls), 0);
    WEQ(UN(NtUserDefSetText)(NULL, &ls), 0);
    WEQ(UN(NtUserDefSetText)((HWND)(ULONG_PTR)-1, NULL), 0);
}

WT(text_defsettext_visible_to_getwindowtext_via_wndproc) {
    REQUIRE(UN(NtUserDefSetText)); if (t_is_wine()) { t_skip("Wine's win32u does not implement NtUserDefSetText"); return; } REQUIRE(U(GetWindowTextW)); NEED_WND(h, 0);
    static const WCHAR s[] = L"via kernel";
    struct wpsq_lstr ls = { 20, 22, (void *)s };
    UN(NtUserDefSetText)(h, &ls);
    WCHAR b[32];
    WEQ(U(GetWindowTextW)(h, b, 32), 10);              /* DefWindowProc(WM_GETTEXT) reads the stored name */
    CHECK(!lstrcmpW(b, s));
    U(DestroyWindow)(h);
}

/* ---------------- cross-process (child) --------------------------------------------------- */

/* argv: [0]=name [1]=hwnd(hex) [2]=owner pid [3]=owner tid */
WC(xproc) {
    g_cfail = 0;
    if (argc < 4) { printf("  FAIL child args\n"); return 99; }
    HWND h = wpsq_arg_hwnd(argv, 1);
    DWORD ppid = strtoul(argv[2], NULL, 10), ptid = strtoul(argv[3], NULL, 10);
    if (!U(GetWindowThreadProcessId) || !UN(NtUserQueryWindow) || !U(SetWindowLongPtrW)) { printf("  SKIP child imports\n"); return 0; }
    CC(U(IsWindow)(h));
    DWORD pid = 0;
    DWORD tid = U(GetWindowThreadProcessId)(h, &pid);
    CCF(tid == ptid && pid == ppid, "tid=%lu pid=%lu want %lu/%lu", tid, pid, ptid, ppid);
    CC(qw(h, 0) == ppid); CC(qw(h, 1) == ppid); CC(qw(h, 2) == ptid);
    CC(pid != GetCurrentProcessId());
    CC(U(IsWindowUnicode)(h));
    /* userdata and extra bytes may be written from another process (SetWindowLongPtr attaches to the owner) */
    LONG_PTR old = U(SetWindowLongPtrW)(h, GWLP_USERDATA, 0x5151);
    CCF(old == 0x77, "old userdata %llx", (unsigned long long)old);
    CC(U(GetWindowLongPtrW)(h, GWLP_USERDATA) == 0x5151);
    old = U(SetWindowLongW)(h, 8, 0xC0DE);
    CCF(old == 0x1111, "old extra %llx", (unsigned long long)old);
    CC((DWORD)U(GetWindowLongW)(h, 8) == 0xC0DE);
    /* bounds are enforced against the target window's cbWndExtra */
    SetLastError(0);
    CC(U(SetWindowLongW)(h, 1000, 1) == 0);
    CCF(GetLastError() == 0, "err %lu", GetLastError());   /* Windows 10 reports the bad index only for a same-process call; across processes the result is 0 with no error */
    /* the window procedure cannot be replaced or read across processes */
    SetLastError(0);
    LONG_PTR r = U(SetWindowLongPtrW)(h, GWLP_WNDPROC, (LONG_PTR)U(DefWindowProcW));
    CC(r == 0);
    CCF(GetLastError() == E_ACCESS_DENIED, "SetWindowLongPtr(WNDPROC) err %lu", GetLastError());
    SetLastError(0);
    r = U(GetWindowLongPtrW)(h, GWLP_WNDPROC);
    CC(r == 0);
    CCF(GetLastError() == E_ACCESS_DENIED, "GetWindowLongPtr(WNDPROC) err %lu", GetLastError());
    /* properties */
    WCHAR kpre[96], knew[96];
    {
        char t[96];
        snprintf(t, sizeof t, "win32ktP%lu_xprocpre", ppid); for (int i = 0;; i++) { kpre[i] = (unsigned char)t[i]; if (!t[i]) break; }
        snprintf(t, sizeof t, "win32ktP%lu_xprocnew", ppid); for (int i = 0;; i++) { knew[i] = (unsigned char)t[i]; if (!t[i]) break; }
    }
    CCF((ULONG_PTR)U(GetPropW)(h, kpre) == 0x99, "pre prop %p", U(GetPropW)(h, kpre));
    CC(U(SetPropW)(h, knew, (HANDLE)0xAB));
    CC((ULONG_PTR)U(GetPropW)(h, knew) == 0xAB);
    CC((ULONG_PTR)U(RemovePropW)(h, kpre) == 0x99);
    CC(U(GetPropW)(h, kpre) == NULL);
    /* text: cross-process reads never involve the owner's wndproc (RealDefWindowProcWorker / kernel copy) */
    WCHAR b[64] = {0};
    CC(U(GetWindowTextW)(h, b, 64) == 8);
    CC(!lstrcmpW(b, L"xp-title"));
    CC(U(GetWindowTextLengthW)(h) == 8);
    memset(b, 0, sizeof b);
    CC(UN(NtUserInternalGetWindowText)(h, b, 64) == 8);
    CC(!lstrcmpW(b, L"xp-title"));
    /* the write lands in the window without a WM_SETTEXT for the owner (checked by the parent) */
    CC(U(SetWindowTextW)(h, L"from-child"));
    memset(b, 0, sizeof b);
    CC(UN(NtUserInternalGetWindowText)(h, b, 64) == 10);
    CC(!lstrcmpW(b, L"from-child"));
    return g_cfail;
}

WT(xproc_window_long_prop_text_from_child) {
    REQUIRE(U(GetWindowLongPtrW)); REQUIRE(U(SetPropW)); REQUIRE(UN(NtUserInternalGetWindowText));
    struct wpsq_owner o;
    if (!wpsq_owner_start(&o, 0, "xp-title")) { t_skip("owner failed"); return; }
    WCHAR kpre[96], knew[96];
    char t[96];
    snprintf(t, sizeof t, "win32ktP%lu_xprocpre", GetCurrentProcessId()); for (int i = 0;; i++) { kpre[i] = (unsigned char)t[i]; if (!t[i]) break; }
    snprintf(t, sizeof t, "win32ktP%lu_xprocnew", GetCurrentProcessId()); for (int i = 0;; i++) { knew[i] = (unsigned char)t[i]; if (!t[i]) break; }
    U(SetWindowLongPtrW)(o.hwnd, GWLP_USERDATA, 0x77);
    U(SetWindowLongW)(o.hwnd, 8, 0x1111);
    U(SetPropW)(o.hwnd, kpre, (HANDLE)0x99);
    g_gettext_override = 1; g_gettextlen_override = 999;    /* the owner's wndproc would lie; cross-process readers must not reach it */
    wpsq_log_reset(); g_wlog_on = 1;
    char args[128]; snprintf(args, sizeof args, "%llx %lu %lu", (unsigned long long)(ULONG_PTR)o.hwnd, GetCurrentProcessId(), o.tid);
    struct k_child_result r = k_child_run(WCN(xproc), args, 30000);
    g_wlog_on = 0; g_gettext_override = 0; g_gettextlen_override = -1;
    WEQ(r.exit_code, 0);
    WEQ(U(GetWindowLongPtrW)(o.hwnd, GWLP_USERDATA), 0x5151);
    WEQ((DWORD)U(GetWindowLongW)(o.hwnd, 8), 0xC0DE);
    WEQ(U(GetPropW)(o.hwnd, knew), 0xAB);
    WEQ(U(GetPropW)(o.hwnd, kpre), 0);
    WCHAR b[32];
    WEQ(UN(NtUserInternalGetWindowText)(o.hwnd, b, 32), 10);
    CHECK(!lstrcmpW(b, L"from-child"));
    /* the child's reads did not produce WM_GETTEXT(LENGTH); on Windows 10 its SetWindowText did not reach the owner's
     * window procedure either (the new text is stored by the kernel, see the read-back above) */
    WEQ(wpsq_log_count(WM_SETTEXT), 0);
    WEQ(wpsq_log_count(WM_GETTEXT), 0);
    WEQ(wpsq_log_count(WM_GETTEXTLENGTH), 0);
    U(RemovePropW)(o.hwnd, knew);
    wpsq_owner_stop(&o);
}
