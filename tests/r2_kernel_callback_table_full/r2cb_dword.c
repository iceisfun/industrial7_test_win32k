/* r2_kernel_callback_table_full: slot 2 (__fnDWORD, kernel DWORD thunk) and slot 3 (__fnNCDESTROY).
 *
 * the DWORD thunk (win32kfull builds a 0x30-byte buffer on the callback stack and retypes
 *   KeUserModeCallback(2, &buf, 0x30, &out, &outlen)
 *   buf+0x00  pwnd      client-side tagWND pointer (kernel pointer minus pti->ulClientDelta)
 *   buf+0x08  msg (u32), buf+0x0c zero
 *   buf+0x10  wParam, buf+0x18 lParam
 *   buf+0x20  xpfnProc  (window procedure as stored in the window)
 *   buf+0x28  user32 dispatcher the thunk calls (__guard_dispatch_icall with rax = [buf+0x28])
 * user32!__fnDWORD calls dispatcher(pwnd, msg, wParam, lParam, xpfnProc) and answers
 *   NtCallbackReturn(&{retval, 0, 0}, 0x18).
 * the DWORD thunk's tail: if (status < 0 || outlen != 0x18) result = 0 else result = *(qword*)out.
 * While the callback runs it also stores {hwnd, pwnd, pwnd->pActCtx} into pti->pClientInfo
 * (the TEB's _CLIENTINFO.CallbackWnd, +0x40/+0x48/+0x50) and restores the old values afterwards.
 */
#include "r2cb.h"

#define NEED_HOOK() do { R2_NEED(r2_gui_connect(), "no GUI connection / KernelCallbackTable"); \
                         R2_NEED(r2_install(), "cannot install the private callback table"); } while (0)

/* First slot-2 event at or after `from` whose message (buf+8) is msg. */
static struct r2_ev *find_dword(LONG from, UINT msg) {
    for (int i = 0; ; i++) {
        struct r2_ev *e = r2_find(2, from, i);
        if (!e) return NULL;
        if (r2_d(e, 8) == msg) return e;
    }
}
/* Sends one plain user message first: on Windows the receiving thread must run it through slot 2. */
#define PROBE_DWORD(w) do { LONG m_ = r2_mark(); LRESULT r_; r2_send((w).hwnd, WM_USER + 2, 0, 0, &r_); \
    if (!find_dword(m_, WM_USER + 2)) { KW(0 && "a sent WM_USER message must use slot 2"); return R2_SKIP; } (w).nuser = 0; } while (0)

/* Windows 10: the dispatcher entry the user32 message thunks call at buf+0x28 is an ntdll routine, not a user32 one. */
static int ptr_in_ntdll(ULONG_PTR p) {
    HMODULE mm = NULL;
    return GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)p, &mm) && mm &&
           mm == GetModuleHandleA("ntdll.dll");
}

R2_SCN(dword_buffer_layout_and_length) {
    NEED_HOOK();
    struct r2_wk w; memset(&w, 0, sizeof w);
    R2_NEED(r2_wk_start(&w, "dwl", 0), "no worker");
    LONG m = r2_mark();
    LRESULT r = 0;
    KW(r2_send(w.hwnd, WM_USER + 0x31, 0x1234, 0x5678, &r));
    struct r2_ev *e = find_dword(m, WM_USER + 0x31);
    R2_NEED(e, "no slot-2 callback seen for the sent message");
    KEQ(e->len, 0x30);                                  /* KeUserModeCallback(2, &buf, 0x30, ...) */
    KEQ(e->tid, w.tid);                                 /* runs on the receiving thread, not the sender */
    KEQ(r2_d(e, 8), WM_USER + 0x31);
    KEQ(r2_d(e, 0xc), 0);
    KEQ(r2_q(e, 0x10), 0x1234);
    KEQ(r2_q(e, 0x18), 0x5678);
    void *pwnd = k_wnd_ptr(w.hwnd);
    if (pwnd && r2_family()) KEQ(r2_q(e, 0), pwnd);     /* the thunk's first argument is the client tagWND pointer */
    else KNOTE("pwnd=%p recorded=%p (client window pointer not comparable here)", pwnd, (void *)r2_q(e, 0));
    K(r2_q(e, 0) != 0);
    if (r2_family()) K(ptr_in_ntdll(r2_q(e, 0x28)));         /* dispatcher the thunk calls lives in ntdll */
    KNOTE("xpfnProc=%p dispatcher=%p", (void *)r2_q(e, 0x20), (void *)r2_q(e, 0x28));
    K(r2_q(e, 0x20) != 0);
    K(!IsBadReadPtr((void *)r2_q(e, 0x20), 1));
    KEQ(r, R2_WK_RET(WM_USER + 0x31));
    r2_wk_stop(&w);
    return 0;
}

R2_SCN(dword_wparam_lparam_pass_through_full_64_bits) {
    NEED_HOOK();
    struct r2_wk w; memset(&w, 0, sizeof w);
    R2_NEED(r2_wk_start(&w, "dw64", 0), "no worker");
    static const struct { UINT msg; WPARAM wp; LPARAM lp; } cases[] = {
        { WM_USER + 0x100, 0, 0 },
        { WM_USER + 0x101, 1, (LPARAM)-1 },
        { WM_USER + 0x102, 0x1122334455667788ULL, 0x8877665544332211ULL },
        { WM_USER + 0x103, (WPARAM)-1, 1 },
        { WM_USER + 0x104, 0xFFFFFFFF00000000ULL, 0x00000000FFFFFFFFULL },
        { 0x7FFF, 0xCAFEBABEDEADBEEFULL, 0x0123456789ABCDEFULL },
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        LONG m = r2_mark();
        LRESULT r = 0;
        KW(r2_send(w.hwnd, cases[i].msg, cases[i].wp, cases[i].lp, &r));
        struct r2_ev *e = find_dword(m, cases[i].msg);
        if (!e) { KW(0 && "slot 2 callback recorded for the sent message"); continue; }
        KEQ(r2_q(e, 0x10), cases[i].wp);
        KEQ(r2_q(e, 0x18), cases[i].lp);
        KEQ(w.last_wp, cases[i].wp);
        KEQ(w.last_lp, cases[i].lp);
        KEQ(r, R2_WK_RET(cases[i].msg));
    }
    r2_wk_stop(&w);
    return 0;
}

R2_SCN(dword_runs_on_receiver_thread_in_send_order) {
    NEED_HOOK();
    struct r2_wk w; memset(&w, 0, sizeof w);
    R2_NEED(r2_wk_start(&w, "ord", 0), "no worker");
    LONG m = r2_mark();
    for (int i = 0; i < 12; i++) {
        LRESULT r;
        KW(r2_send(w.hwnd, WM_USER + 0x200 + i, i, i * 3, &r));
    }
    LONG prev = -1;
    for (int i = 0; i < 12; i++) {
        struct r2_ev *e = find_dword(m, WM_USER + 0x200 + i);
        if (!e) { KW(0 && "slot 2 callback recorded for the sent message"); continue; }
        K(e->seq > prev);                    /* SendMessage is synchronous: strictly the order they were sent in */
        prev = e->seq;
        KEQ(e->tid, w.tid);
        K(e->tid != GetCurrentThreadId());
        KEQ(r2_q(e, 0x10), i);
        KEQ(r2_q(e, 0x18), i * 3);
    }
    KEQ(w.nuser, 12);
    r2_wk_stop(&w);
    return 0;
}

R2_SCN(dword_callbackwnd_in_clientinfo_during_callback) {
    NEED_HOOK();
    struct r2_wk w; memset(&w, 0, sizeof w);
    R2_NEED(r2_wk_start(&w, "cbw", 0), "no worker");
    LONG m = r2_mark();
    LRESULT r;
    KW(r2_send(w.hwnd, WM_USER + 0x40, 0, 0, &r));
    struct r2_ev *e = find_dword(m, WM_USER + 0x40);
    R2_NEED(e, "no callback");
    /* the DWORD thunk stored the window being called into the receiving thread's _CLIENTINFO before the callout:
     * the window procedure (running inside the callback) sees it at TEB+0x800+0x40/0x48. */
    KEQ(w.ci_hwnd, (ULONG_PTR)w.hwnd);
    KEQ(w.ci_pwnd, r2_q(e, 0));
    KNOTE("CallbackWnd.pActCtx seen in callback: %p", (void *)w.ci_actctx);
    KNOTE("cInDDEMLCallback seen in callback: %lu", (unsigned long)w.ci_dde);
    r2_wk_stop(&w);
    return 0;
}

static ULONG_PTR g_ovr_ret;
static ULONG g_ovr_len;
static long g_ovr_status;
static UINT g_ovr_msg;
static void dword_override(void *buf, ULONG len, unsigned idx) {
    ULONG msg = 0; memcpy(&msg, (char *)buf + 8, 4);
    if (msg == g_ovr_msg) r2_cbreturn(g_ovr_ret, 0, NULL, g_ovr_len, g_ovr_status);
}

R2_SCN(dword_override_return_value_reaches_sender_without_wndproc) {
    NEED_HOOK();
    struct r2_wk w; memset(&w, 0, sizeof w);
    R2_NEED(r2_wk_start(&w, "ovr", 0), "no worker");
    PROBE_DWORD(w);
    g_ovr_msg = WM_USER + 0x50; g_ovr_ret = 0x1122334455667788ULL; g_ovr_len = 0x18; g_ovr_status = 0;
    r2_ovr[2] = dword_override;
    LRESULT r = 0;
    KW(r2_send(w.hwnd, WM_USER + 0x50, 1, 2, &r));
    /* the thunk never ran, so the window procedure was never called, yet the sender got our qword back */
    KEQ(r, 0x1122334455667788ULL);
    KEQ(w.nuser, 0);
    /* an untouched message still takes the normal route */
    KW(r2_send(w.hwnd, WM_USER + 0x51, 3, 4, &r));
    KEQ(r, R2_WK_RET(WM_USER + 0x51));
    KEQ(w.nuser, 1);
    r2_wk_stop(&w);
    return 0;
}

R2_SCN(dword_result_of_wrong_length_is_ignored) {
    NEED_HOOK();
    struct r2_wk w; memset(&w, 0, sizeof w);
    R2_NEED(r2_wk_start(&w, "len", 0), "no worker");
    PROBE_DWORD(w);
    static const ULONG lens[] = { 0x10, 0x17, 0x20, 0x8, 0 };
    for (unsigned i = 0; i < sizeof lens / sizeof lens[0]; i++) {
        g_ovr_msg = WM_USER + 0x60 + i; g_ovr_ret = 0xDEADBEEFCAFE0001ULL; g_ovr_len = lens[i]; g_ovr_status = 0;
        r2_ovr[2] = dword_override;
        LRESULT r = 1;
        /* the DWORD thunk: (status < 0 || outlen != 0x18) ? 0 : *(qword*)out */
        KW(r2_send(w.hwnd, WM_USER + 0x60 + i, 0, 0, &r));
        KF(r == 0, "result length 0x%lx returned 0x%llx instead of 0", (unsigned long)lens[i], (unsigned long long)r);
    }
    KEQ(w.nuser, 0);
    r2_wk_stop(&w);
    return 0;
}

R2_SCN(dword_failure_status_is_ignored_even_with_valid_result) {
    NEED_HOOK();
    struct r2_wk w; memset(&w, 0, sizeof w);
    R2_NEED(r2_wk_start(&w, "st", 0), "no worker");
    PROBE_DWORD(w);
    static const long sts[] = { (long)0xC0000001, (long)0xC0000005, (long)0x80000001, (long)0xC000000D };
    for (unsigned i = 0; i < sizeof sts / sizeof sts[0]; i++) {
        g_ovr_msg = WM_USER + 0x70 + i; g_ovr_ret = 0x55AA55AA55AA55AAULL; g_ovr_len = 0x18; g_ovr_status = sts[i];
        r2_ovr[2] = dword_override;
        LRESULT r = 1;
        KW(r2_send(w.hwnd, WM_USER + 0x70 + i, 0, 0, &r));
        KF(r == 0, "status 0x%lx: result 0x%llx, want 0 (status < 0 discards the output)", (unsigned long)sts[i], (unsigned long long)r);
    }
    /* an informational/success status (>= 0) with a 0x18-byte result is accepted */
    g_ovr_msg = WM_USER + 0x7F; g_ovr_ret = 0x1357; g_ovr_len = 0x18; g_ovr_status = 0x40000000;
    LRESULT r = 0;
    KW(r2_send(w.hwnd, WM_USER + 0x7F, 0, 0, &r));
    KEQ(r, 0x1357);
    r2_wk_stop(&w);
    return 0;
}

R2_SCN(dword_return_value_is_a_full_64_bit_lresult) {
    NEED_HOOK();
    struct r2_wk w; memset(&w, 0, sizeof w);
    R2_NEED(r2_wk_start(&w, "r64", 0), "no worker");
    PROBE_DWORD(w);
    static const ULONG_PTR vals[] = { 0, 1, 0x80000000ULL, 0xFFFFFFFFULL, 0x100000000ULL, 0x8000000000000000ULL, (ULONG_PTR)-1, 0x7FFFFFFFFFFFFFFFULL };
    for (unsigned i = 0; i < sizeof vals / sizeof vals[0]; i++) {
        g_ovr_msg = WM_USER + 0x80 + i; g_ovr_ret = vals[i]; g_ovr_len = 0x18; g_ovr_status = 0;
        r2_ovr[2] = dword_override;
        LRESULT r = 5;
        KW(r2_send(w.hwnd, WM_USER + 0x80 + i, 0, 0, &r));
        KEQ(r, vals[i]);
    }
    r2_wk_stop(&w);
    return 0;
}

R2_SCN(dword_hit_count_equals_send_count) {
    NEED_HOOK();
    struct r2_wk w; memset(&w, 0, sizeof w);
    R2_NEED(r2_wk_start(&w, "cnt", 0), "no worker");
    PROBE_DWORD(w);
    LONG m = r2_mark();
    for (int i = 0; i < 25; i++) { LRESULT r; KW(r2_send(w.hwnd, WM_USER + 0x300 + (i & 3), i, 0, &r)); }
    int seen = 0;
    for (int i = 0; ; i++) {
        struct r2_ev *e = r2_find(2, m, i);
        if (!e) break;
        ULONG msg = r2_d(e, 8);
        if (msg >= WM_USER + 0x300 && msg < WM_USER + 0x304) seen++;
    }
    KEQ(seen, 25);
    KEQ(w.nuser, 25);
    r2_wk_stop(&w);
    return 0;
}

R2_SCN(dword_own_thread_send_makes_no_callback) {
    NEED_HOOK();
    HWND h = k_make_window("r2cb-own");
    R2_NEED(h, "no window");
    k_pump(5);
    LONG m = r2_mark();
    /* user32!SendMessage on a window of the calling thread calls the window procedure itself: no kernel
     * round trip, therefore no table entry is used for this message. */
    SendMessageW(h, WM_USER + 0x33, 1, 2);
    KEQ(r2_since(2, m), 0);
    DestroyWindow(h);
    return 0;
}

static UINT g_seq_msgs[8]; static int g_seq_n;
static LRESULT CALLBACK seq_proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (g_seq_n < 8) g_seq_msgs[g_seq_n++] = m;
    return DefWindowProcW(h, m, wp, lp);
}

R2_SCN(ncdestroy_slot3_after_destroy_in_order) {
    NEED_HOOK();
    WNDCLASSW wc; memset(&wc, 0, sizeof wc);
    WCHAR cls[64];
    _snwprintf(cls, 63, L"win32ktR2_nc_%lu", GetCurrentProcessId());
    wc.lpfnWndProc = seq_proc; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = cls;
    R2_NEED(RegisterClassW(&wc), "no class");
    HWND h = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls, L"r2nc", WS_POPUP, -32000, -32000, 8, 8, NULL, NULL, wc.hInstance, NULL);
    R2_NEED(h, "no window");
    k_pump(5);
    void *pwnd = k_wnd_ptr(h);
    LONG m = r2_mark();
    g_seq_n = 0;
    K(DestroyWindow(h));
    /* kernel-originated messages always come through a callback, even on the owning thread */
    struct r2_ev *nc = r2_find(3, m, 0);
    R2_NEED(nc, "no slot-3 callback after DestroyWindow");
    KEQ(nc->len, 0x30);                          /* KeUserModeCallback(3, &buf, 0x30, ...) in SfnNCDESTROY */
    KEQ(r2_d(nc, 8), WM_NCDESTROY);
    KEQ(nc->tid, GetCurrentThreadId());
    if (pwnd && r2_family()) KEQ(r2_q(nc, 0), pwnd);
    if (r2_family()) K(ptr_in_ntdll(r2_q(nc, 0x28)));
    /* WM_DESTROY precedes WM_NCDESTROY in the procedure and in the callback log (slot 2 or 3) */
    int destroy_idx = -1, ncdestroy_idx = -1;
    for (int i = 0; i < g_seq_n; i++) { if (g_seq_msgs[i] == WM_DESTROY && destroy_idx < 0) destroy_idx = i; if (g_seq_msgs[i] == WM_NCDESTROY) ncdestroy_idx = i; }
    K(destroy_idx >= 0);
    K(ncdestroy_idx > destroy_idx);
    LONG d_seq = -1;
    for (unsigned s = 2; s <= 3 && d_seq < 0; s++)
        for (int i = 0; ; i++) { struct r2_ev *e = r2_find(s, m, i); if (!e) break; if (r2_d(e, 8) == WM_DESTROY) { d_seq = e->seq; break; } }
    K(d_seq >= 0);
    if (d_seq >= 0) K(d_seq < nc->seq);
    UnregisterClassW(cls, wc.hInstance);
    return 0;
}

/* ---- which messages reach the DWORD thunk ------------------------------------------------------------------ */
static LRESULT range_proc(struct r2_wk *w, HWND h, UINT m, WPARAM wp, LPARAM lp, int *handled) {
    if (m < 0x400) return 0;
    w->nuser++; w->last_msg = m; w->last_wp = wp; w->last_lp = lp;
    *handled = 1;
    return R2_WK_RET(m);
}
R2_SCN(dword_every_message_from_0x400_up_uses_slot_2) {
    NEED_HOOK();
    struct r2_wk w; memset(&w, 0, sizeof w);
    w.proc = range_proc;
    R2_NEED(r2_wk_start(&w, "rng", 0), "no worker");
    PROBE_DWORD(w);
    /* the kernel-to-client send path: if ((msg & 0x1ffff) > 0x3ff) the DWORD thunk(...) - WM_USER and everything above, including
     * the registered-message range 0xC000..0xFFFF, takes the generic path regardless of the window's class. */
    static const UINT msgs[] = { 0x400, 0x401, 0x7FF, 0x800, 0x1234, 0x7FFF, 0x8000, 0x8001, 0xBFFF, 0xC000, 0xC001, 0xFFFE, 0xFFFF };
    for (unsigned i = 0; i < sizeof msgs / sizeof msgs[0]; i++) {
        LONG m = r2_mark();
        LRESULT r = 0;
        KW(r2_send(w.hwnd, msgs[i], i, ~(LPARAM)i, &r));
        struct r2_ev *e = find_dword(m, msgs[i]);
        if (!e) { KW(0 && "message >= 0x400 must be delivered through slot 2"); continue; }
        KEQ(r, R2_WK_RET(msgs[i]));
        KEQ(r2_q(e, 0x10), i);
        KEQ(r2_q(e, 0x18), ~(LPARAM)i);
        KEQ(e->len, 0x30);
    }
    r2_wk_stop(&w);
    return 0;
}

R2_SCN(dword_xpfnproc_is_the_class_window_procedure) {
    NEED_HOOK();
    struct r2_wk w; memset(&w, 0, sizeof w);
    R2_NEED(r2_wk_start(&w, "xpf", 0), "no worker");
    PROBE_DWORD(w);
    LONG m = r2_mark();
    LRESULT r;
    KW(r2_send(w.hwnd, WM_USER + 0x20, 0, 0, &r));
    struct r2_ev *e = find_dword(m, WM_USER + 0x20);
    R2_NEED(e, "no callback");
    /* the DWORD thunk: the window's lpfnWndProc is handed to the dispatcher as its fifth argument */
    KNOTE("xpfnProc=%p, worker window procedure=%p", (void *)r2_q(e, 0x20), r2_wk_proc_addr());
    KW(r2_q(e, 0x20) == (ULONG_PTR)r2_wk_proc_addr());
    /* the dispatcher at +0x28 is the same function for every window and every message */
    LONG m2 = r2_mark();
    KW(r2_send(w.hwnd, WM_USER + 0x21, 0, 0, &r));
    struct r2_ev *e2 = find_dword(m2, WM_USER + 0x21);
    if (e2) KEQ(r2_q(e2, 0x28), r2_q(e, 0x28));
    r2_wk_stop(&w);
    return 0;
}
