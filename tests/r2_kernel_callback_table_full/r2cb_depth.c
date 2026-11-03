/* r2_kernel_callback_table_full: re-entrancy, depth, stack placement and channel hygiene.
 *
 * KeUserModeCallback copies the input buffer onto the CALLING thread's user stack, switches to user mode
 * (ntdll!KiUserCallbackDispatcher) and returns when the thunk calls NtCallbackReturn. A callback that sends a
 * message back to the original sender makes the sender (blocked in its own SendMessage kernel wait) run a nested
 * callback, so depth grows on BOTH threads' user and kernel stacks. SfnDWORD saves the _CLIENTINFO.CallbackWnd
 * triple before each callout and restores it after, which is what lets the nesting unwind cleanly.
 */
#include "r2cb.h"

#define NEED_HOOK() do { R2_NEED(r2_gui_connect(), "no GUI connection / KernelCallbackTable"); \
                         R2_NEED(r2_install(), "cannot install the private callback table"); } while (0)

#define NEST_MSG (WM_USER + 0x500)
#define NEST_MAX 256

static struct {
    HWND h[2]; DWORD tid[2];
    volatile LONG cur[2]; int maxdepth[2];
    int entry_ok[2], entry_bad[2], restore_ok[2], restore_bad[2];
    ULONG_PTR sp[2][NEST_MAX];
    ULONG_PTR stack_lo[2], stack_hi[2];
    volatile LONG calls;
    int failed_sends;
} g_n;

static LRESULT nest_logic(int me, HWND self, WPARAM wp) {
    int d = ++g_n.cur[me];
    g_n.calls++;
    if (d > g_n.maxdepth[me]) g_n.maxdepth[me] = d;
    volatile char local = 0;
    if (d < NEST_MAX) g_n.sp[me][d] = (ULONG_PTR)&local;
    g_n.stack_lo[me] = r2_teb_q(TEB_DeallocationStack);
    g_n.stack_hi[me] = r2_teb_q(TEB_StackBase);
    if (r2_teb_q(R2_CI_CallbackWnd_hwnd) == (ULONG_PTR)self) g_n.entry_ok[me]++; else g_n.entry_bad[me]++;
    LRESULT r = 1;
    if (wp > 0) {
        DWORD_PTR res = 0;
        LRESULT ok = SendMessageTimeoutW(g_n.h[1 - me], NEST_MSG, wp - 1, 0, SMTO_NORMAL, 8000, &res);
        if (ok) r = (LRESULT)res + 1; else { g_n.failed_sends++; r = -1000000; }
        /* the inner callout restored this thread's CallbackWnd to the value it had when we were called */
        if (r2_teb_q(R2_CI_CallbackWnd_hwnd) == (ULONG_PTR)self) g_n.restore_ok[me]++; else g_n.restore_bad[me]++;
    }
    g_n.cur[me]--;
    return r;
}
static LRESULT CALLBACK nest_main_proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (m == NEST_MSG) return nest_logic(0, h, wp);
    return DefWindowProcW(h, m, wp, lp);
}
static LRESULT nest_wk_proc(struct r2_wk *w, HWND h, UINT m, WPARAM wp, LPARAM lp, int *handled) {
    if (m != NEST_MSG) return 0;
    *handled = 1;
    return nest_logic(1, h, wp);
}

/* Sets up the two windows: [0] on this thread, [1] on a worker. */
static int nest_setup(struct r2_wk *w, WCHAR *cls, size_t cap) {
    memset(&g_n, 0, sizeof g_n);
    memset(w, 0, sizeof *w);
    w->proc = nest_wk_proc;
    if (!r2_wk_start(w, "nest", 0)) return 0;
    WNDCLASSW wc; memset(&wc, 0, sizeof wc);
    _snwprintf(cls, cap - 1, L"win32ktR2_nm_%lu", GetCurrentProcessId());
    wc.lpfnWndProc = nest_main_proc; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = cls;
    if (!RegisterClassW(&wc)) { r2_wk_stop(w); return 0; }
    g_n.h[0] = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls, L"nest-main", WS_POPUP, -32000, -32000, 1, 1, HWND_MESSAGE, NULL, wc.hInstance, NULL);
    g_n.h[1] = w->hwnd;
    g_n.tid[0] = GetCurrentThreadId(); g_n.tid[1] = w->tid;
    if (!g_n.h[0]) { UnregisterClassW(cls, wc.hInstance); r2_wk_stop(w); return 0; }
    return 1;
}
static void nest_teardown(struct r2_wk *w, WCHAR *cls) {
    if (g_n.h[0]) DestroyWindow(g_n.h[0]);
    UnregisterClassW(cls, GetModuleHandleW(NULL));
    r2_wk_stop(w);
}

static int run_nest(unsigned depth, LRESULT *res) {
    return r2_send(g_n.h[1], NEST_MSG, depth, 0, res);
}

#define NEST_DEPTH_TEST(D) \
R2_SCN(nested_send_ping_pong_depth_##D) { \
    NEED_HOOK(); \
    struct r2_wk w; WCHAR cls[64]; \
    R2_NEED(nest_setup(&w, cls, 64), "cannot create the two windows"); \
    LONG m = r2_mark(); \
    LRESULT r = 0; \
    KW(run_nest(D, &r)); \
    /* D+1 procedure invocations, each adds 1 to what its inner call returned */ \
    KW(r == (LRESULT)(D + 1)); \
    KW(g_n.calls == (D + 1)); \
    KEQ(g_n.failed_sends, 0); \
    KEQ(g_n.cur[0], 0); KEQ(g_n.cur[1], 0); \
    /* the worker takes the even levels, this thread the odd ones */ \
    KW(g_n.maxdepth[1] == (D / 2) + 1); \
    KW(g_n.maxdepth[0] == (D + 1) / 2); \
    KW(r2_since(2, m) >= (D + 1)); \
    KWEQ(g_n.entry_bad[0] + g_n.entry_bad[1], 0); \
    KWEQ(g_n.restore_bad[0] + g_n.restore_bad[1], 0); \
    nest_teardown(&w, cls); \
    return 0; \
}
NEST_DEPTH_TEST(1)
NEST_DEPTH_TEST(2)
NEST_DEPTH_TEST(3)
NEST_DEPTH_TEST(10)
NEST_DEPTH_TEST(30)

R2_SCN(nested_callbackwnd_is_saved_and_restored_at_every_level) {
    NEED_HOOK();
    struct r2_wk w; WCHAR cls[64];
    R2_NEED(nest_setup(&w, cls, 64), "cannot create the two windows");
    LRESULT r = 0;
    KW(run_nest(12, &r));
    /* on entry of each procedure CallbackWnd.hwnd names the window being called ... */
    KW(g_n.entry_ok[0] + g_n.entry_ok[1] == 13);
    /* ... and after every inner send came back it names it again (the kernel restored the saved triple) */
    KW(g_n.restore_ok[0] + g_n.restore_ok[1] == 12);
    KWEQ(g_n.entry_bad[0] + g_n.entry_bad[1], 0);
    KWEQ(g_n.restore_bad[0] + g_n.restore_bad[1], 0);
    nest_teardown(&w, cls);
    return 0;
}

R2_SCN(nested_callbacks_descend_the_user_stack_and_buffers_live_on_it) {
    NEED_HOOK();
    struct r2_wk w; WCHAR cls[64];
    R2_NEED(nest_setup(&w, cls, 64), "cannot create the two windows");
    LONG m = r2_mark();
    LRESULT r = 0;
    KW(run_nest(14, &r));
    R2_NEED(g_n.calls >= 15, "nested chain did not run (not Windows)");
    for (int me = 0; me < 2; me++) {
        for (int d = 1; d < g_n.maxdepth[me]; d++)
            KF(g_n.sp[me][d + 1] < g_n.sp[me][d], "thread %d: depth %d frame %p not below depth %d frame %p", me, d + 1, (void *)g_n.sp[me][d + 1], d, (void *)g_n.sp[me][d]);
        for (int d = 1; d <= g_n.maxdepth[me]; d++)
            K(g_n.sp[me][d] >= g_n.stack_lo[me] && g_n.sp[me][d] < g_n.stack_hi[me]);
    }
    /* every slot-2 input buffer was copied onto the receiving thread's own user stack */
    int checked = 0;
    for (int i = 0; ; i++) {
        struct r2_ev *e = r2_find(2, m, i);
        if (!e) break;
        if (r2_d(e, 8) != NEST_MSG) continue;
        int me = e->tid == g_n.tid[0] ? 0 : (e->tid == g_n.tid[1] ? 1 : -1);
        KF(me >= 0, "callback on unexpected thread %lu", (unsigned long)e->tid);
        if (me < 0) continue;
        KF((ULONG_PTR)e->buf >= g_n.stack_lo[me] && (ULONG_PTR)e->buf < g_n.stack_hi[me], "buf %p outside thread %d stack [%p,%p)", e->buf, me, (void *)g_n.stack_lo[me], (void *)g_n.stack_hi[me]);
        checked++;
    }
    KW(checked == 15);
    nest_teardown(&w, cls);
    return 0;
}

R2_SCN(nested_events_alternate_threads_in_call_order) {
    NEED_HOOK();
    struct r2_wk w; WCHAR cls[64];
    R2_NEED(nest_setup(&w, cls, 64), "cannot create the two windows");
    LONG m = r2_mark();
    LRESULT r = 0;
    KW(run_nest(9, &r));
    int n = 0; DWORD prev = 0; LONG prev_seq = -1; ULONG_PTR prev_wp = 100;
    for (int i = 0; ; i++) {
        struct r2_ev *e = r2_find(2, m, i);
        if (!e) break;
        if (r2_d(e, 8) != NEST_MSG) continue;
        K(e->tid != prev);                          /* every hop changes thread */
        K(e->seq > prev_seq);
        K(r2_q(e, 0x10) < prev_wp);                 /* wParam counts down 9, 8, ..., 0 */
        K(e->len == 0x30);
        prev = e->tid; prev_seq = e->seq; prev_wp = r2_q(e, 0x10); n++;
    }
    KW(n == 10);
    KEQ(prev_wp, n ? 0 : 100);
    nest_teardown(&w, cls);
    return 0;
}

R2_SCN(nested_depth_ladder_degrades_gracefully_and_recovers) {
    NEED_HOOK();
    struct r2_wk w; WCHAR cls[64];
    R2_NEED(nest_setup(&w, cls, 64), "cannot create the two windows");
    static const unsigned ladder[] = { 50, 100, 150, 200 };
    unsigned deepest_ok = 0;
    for (unsigned i = 0; i < sizeof ladder / sizeof ladder[0]; i++) {
        memset(g_n.sp, 0, sizeof g_n.sp);
        g_n.calls = 0; g_n.failed_sends = 0;
        LRESULT r = 0;
        int ok = run_nest(ladder[i], &r);
        KNOTE("depth %u: sent=%d result=%lld calls=%ld failed_inner=%d", ladder[i], ok, (long long)r, (long)g_n.calls, g_n.failed_sends);
        if (ok && r == (LRESULT)(ladder[i] + 1)) deepest_ok = ladder[i];
        else break;                                  /* the kernel refused to go deeper: fine, but nothing may crash or hang */
        if (i + 1 < sizeof ladder / sizeof ladder[0] && g_n.calls == 0) break;
    }
    KNOTE("deepest fully completed ping-pong: %u levels", deepest_ok);
    /* whatever happened above, the channel works afterwards and both thread nesting counters are back to zero */
    KEQ(g_n.cur[0], 0); KEQ(g_n.cur[1], 0);
    LRESULT r = 0;
    KW(run_nest(3, &r));
    KW(r == 4);
    KW(r2_send(w.hwnd, WM_USER + 0x3, 1, 2, &r));
    KW(r == R2_WK_RET(WM_USER + 0x3));
    nest_teardown(&w, cls);
    return 0;
}

/* ---- channel hygiene ------------------------------------------------------------------------------- */
R2T(ntcallbackreturn_outside_a_callback_reports_no_callback_active) {
    typedef long (NTAPI *pfn_t)(PVOID, ULONG, long);
    pfn_t f = (pfn_t)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtCallbackReturn");
    REQUIRE(f);
    if (t_is_wine()) { t_skip("Wine reports this differently"); return; }
    struct { ULONG_PTR ret; ULONG cb; ULONG pad; void *p; } res = { 0x1234, 0, 0, NULL };
    /* ntoskrnl!NtCallbackReturn checks the thread's callback stack first: a plain thread has none */
    long st = f(&res, 0x18, 0);
    EXPECT_EQ((unsigned long)st, 0xC0000258UL);        /* STATUS_NO_CALLBACK_ACTIVE */
    st = f(NULL, 0, 0);
    EXPECT_EQ((unsigned long)st, 0xC0000258UL);
    st = f(&res, 0x18, (long)0xC0000001);
    EXPECT_EQ((unsigned long)st, 0xC0000258UL);        /* the status argument does not matter */
    st = f(&res, 0, 0);
    EXPECT_EQ((unsigned long)st, 0xC0000258UL);
}

R2_SCN(padding_slots_stay_silent_after_a_wide_workload) {
    NEED_HOOK();
    struct r2_wk w; memset(&w, 0, sizeof w);
    R2_NEED(r2_wk_start(&w, "wide", 0), "no worker");
    /* windows with class menu/icon, hooks, win events, cross-thread sends, destroys: many different ApiNumbers */
    for (int i = 0; i < 3; i++) {
        HWND h = k_make_window("r2cb-wide");
        if (h) { SetWindowTextW(h, L"w"); SetWindowPos(h, NULL, -32000, -32000, 20, 20, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED); k_pump(5); DestroyWindow(h); }
    }
    LRESULT r;
    for (int i = 0; i < 20; i++) r2_send(w.hwnd, WM_USER + i, i, i, &r);
    static const WCHAR t[] = L"wide-text";
    r2_send(w.hwnd, WM_SETTEXT, 0, (LPARAM)t, &r);
    WCHAR buf[32];
    r2_send(w.hwnd, WM_GETTEXT, 32, (LPARAM)buf, &r);
    r2_send(w.hwnd, WM_GETTEXTLENGTH, 0, 0, &r);
    COPYDATASTRUCT cds = { 1, 4, (PVOID)"abc" };
    r2_send(w.hwnd, WM_COPYDATA, 0, (LPARAM)&cds, &r);
    RECT rc = { 0, 0, 10, 10 };
    r2_send(w.hwnd, WM_NCCALCSIZE, FALSE, (LPARAM)&rc, &r);
    k_pump(20);
    r2_wk_stop(&w);
    R2_NEED(r2_ev_n > 10, "no callbacks");
    unsigned distinct = 0;
    for (unsigned i = 0; i < R2_NSLOT; i++) {
        if (r2_hit[i]) distinct++;
        if (i >= r2_nlive) KF(r2_hit[i] == 0, "padding slot %u called", i);
    }
    KNOTE("%u distinct ApiNumbers used by the workload", distinct);
    KW(distinct >= 6);
    return 0;
}

/* ---- DDE ---------------------------------------------------------------------------------------------- */
static struct { volatile LONG calls; WPARAM wp; LPARAM lp; DWORD tid; } g_dde;
static LRESULT dde_proc(struct r2_wk *w, HWND h, UINT m, WPARAM wp, LPARAM lp, int *handled) {
    if (m != 0x3E0) return 0;
    g_dde.calls++; g_dde.wp = wp; g_dde.lp = lp; g_dde.tid = GetCurrentThreadId();
    *handled = 1;
    return 0x3E0;
}
R2_SCN(dde_initiate_sent_across_threads_keeps_wparam_lparam) {
    NEED_HOOK();
    struct r2_wk w; memset(&w, 0, sizeof w);
    w.proc = dde_proc;
    R2_NEED(r2_wk_start(&w, "dde", 0), "no worker");
    /* The kernel validates the window in wParam of WM_DDE_INITIATE (a made-up handle fails with ERROR_INVALID_WINDOW_HANDLE and the
     * message is not delivered), so the "client" is a real window of this thread. */
    HWND client = k_make_window("r2cb-ddecl");
    R2_NEED(client, "no client window");
    LONG m = r2_mark();
    LRESULT r = 0;
    /* WM_DDE_INITIATE (0x3E0): wParam = client window, lParam = MAKELONG(atomApp, atomTopic); 0/0 = wildcard */
    KW(r2_send(w.hwnd, 0x3E0, (WPARAM)client, 0, &r));
    KEQ(r, 0x3E0);
    KEQ(g_dde.calls, 1);
    KEQ(g_dde.wp, (WPARAM)client);
    KEQ(g_dde.lp, 0);
    KEQ(g_dde.tid, w.tid);
    /* the message is below WM_USER, so which Sfn routine carries it is a property of the kernel's message table:
     * record which slot saw it */
    int found = -1;
    for (LONG i = m; i < (r2_ev_n < R2_NEV ? r2_ev_n : R2_NEV) && found < 0; i++) {
        struct r2_ev *x = &r2_ev_log[i];
        if (x->tid != w.tid) continue;
        for (unsigned o = 0; o + 4 <= 0x40 && o + 4 <= x->ncopy; o += 4)
            if (r2_d(x, o) == 0x3E0) { found = (int)x->idx; KNOTE("WM_DDE_INITIATE at slot %u offset 0x%x len 0x%lx", x->idx, o, (unsigned long)x->len); break; }
    }
    KNOTE("WM_DDE_INITIATE delivered through slot %d (-1: not seen)", found);
    K(found >= 0);
    if (found >= 0) KW(found == 2 || found == 38);   /* SfnDWORD or SfnSENTDDEMSG */
    KEQ(w.last_tid, w.tid);
    DestroyWindow(client);
    r2_wk_stop(&w);
    return 0;
}

R2_SCN(ddeml_callback_counter_is_idle_outside_ddeml) {
    NEED_HOOK();
    struct r2_wk w; memset(&w, 0, sizeof w);
    R2_NEED(r2_wk_start(&w, "ddec", 0), "no worker");
    LRESULT r;
    KW(r2_send(w.hwnd, WM_USER + 0x41, 0, 0, &r));
    /* _CLIENTINFO.cInDDEMLCallback is only touched by DDEML (DdeInitialize callbacks); an ordinary SfnDWORD callback
     * never raises it, on either thread. The offset (+0x5c) follows the structure layout, not an analysed access. */
    KNOTE("cInDDEMLCallback during an ordinary callback: %lu; idle on this thread: %lu", (unsigned long)w.ci_dde, (unsigned long)r2_teb_d(R2_CI_cInDDEMLCallback));
    KEQ(w.ci_dde, 0);
    KEQ(r2_teb_d(R2_CI_cInDDEMLCallback), 0);
    r2_wk_stop(&w);
    return 0;
}
