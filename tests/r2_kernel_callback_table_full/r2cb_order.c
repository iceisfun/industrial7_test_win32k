/* r2_kernel_callback_table_full: order of the callbacks the kernel makes for one user-mode operation.
 * The window procedure's view (message order) and the table's view (slot per message) must agree. */
#include "r2cb.h"

#define NEED_HOOK() do { R2_NEED(r2_gui_connect(), "no GUI connection / KernelCallbackTable"); \
                         R2_NEED(r2_install(), "cannot install the private callback table"); } while (0)

static UINT g_msgs[64]; static int g_n;
static LRESULT CALLBACK order_proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (g_n < 64 && m != WM_NCHITTEST && m != WM_SETCURSOR && m != WM_GETICON && m != WM_NCPAINT && m != WM_ERASEBKGND) g_msgs[g_n++] = m;
    return DefWindowProcW(h, m, wp, lp);
}
static int pos_of(UINT msg, int from) { for (int i = from; i < g_n; i++) if (g_msgs[i] == msg) return i; return -1; }

/* sequence number of the first event at or after `from` in `slot` carrying msg at offset moff */
static LONG ev_seq(unsigned slot, LONG from, UINT msg, unsigned moff) {
    for (int i = 0; ; i++) {
        struct r2_ev *e = r2_find(slot, from, i);
        if (!e) return -1;
        if (r2_d(e, moff) == msg) return e->seq;
    }
}

R2_SCN(createwindow_message_order_matches_the_callback_log) {
    NEED_HOOK();
    WNDCLASSW wc; memset(&wc, 0, sizeof wc);
    WCHAR cls[64];
    _snwprintf(cls, 63, L"win32ktR2_ord_%lu", GetCurrentProcessId());
    wc.lpfnWndProc = order_proc; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = cls;
    R2_NEED(RegisterClassW(&wc), "no class");
    g_n = 0;
    LONG m = r2_mark();
    HWND h = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls, L"order", WS_OVERLAPPEDWINDOW, -32000, -32000, 200, 150, NULL, NULL, wc.hInstance, NULL);
    R2_NEED(h, "no window");
    /* documented creation order for a top-level window */
    int gmm = pos_of(WM_GETMINMAXINFO, 0), ncc = pos_of(WM_NCCREATE, 0), ncs = pos_of(WM_NCCALCSIZE, 0), cr = pos_of(WM_CREATE, 0);
    K(ncc >= 0 && ncs > ncc && cr > ncs);
    if (gmm >= 0) K(gmm < ncc);
    /* ... and the same order in the table: SfnINLPCREATESTRUCT(10) x2 around SfnINOUTNCCALCSIZE(21) */
    LONG s_nc = ev_seq(10, m, WM_NCCREATE, 0x30), s_cr = ev_seq(10, m, WM_CREATE, 0x30), s_cs = ev_seq(21, m, WM_NCCALCSIZE, 8);
    KW(s_nc >= 0 && s_cs >= 0 && s_cr >= 0);
    KW(s_nc < s_cs && s_cs < s_cr);
    LONG s_gm = ev_seq(18, m, WM_GETMINMAXINFO, 8);
    if (s_gm >= 0) KW(s_gm < s_nc);
    KNOTE("callback seq: GETMINMAXINFO=%ld NCCREATE=%ld NCCALCSIZE=%ld CREATE=%ld", (long)s_gm, (long)s_nc, (long)s_cs, (long)s_cr);
    DestroyWindow(h);
    UnregisterClassW(cls, wc.hInstance);
    return 0;
}

R2_SCN(setwindowpos_frame_change_callback_order) {
    NEED_HOOK();
    WNDCLASSW wc; memset(&wc, 0, sizeof wc);
    WCHAR cls[64];
    _snwprintf(cls, 63, L"win32ktR2_ordp_%lu", GetCurrentProcessId());
    wc.lpfnWndProc = order_proc; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = cls;
    R2_NEED(RegisterClassW(&wc), "no class");
    HWND h = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls, L"orderp", WS_OVERLAPPEDWINDOW, -32000, -32000, 200, 150, NULL, NULL, wc.hInstance, NULL);
    R2_NEED(h, "no window");
    k_pump(5);
    g_n = 0;
    LONG m = r2_mark();
    K(SetWindowPos(h, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE));
    int pc = pos_of(WM_WINDOWPOSCHANGING, 0), nc = pos_of(WM_NCCALCSIZE, 0), pd = pos_of(WM_WINDOWPOSCHANGED, 0);
    K(pc >= 0 && nc > pc && pd > nc);
    LONG s_pc = ev_seq(22, m, WM_WINDOWPOSCHANGING, 8), s_nc = ev_seq(21, m, WM_NCCALCSIZE, 8), s_pd = ev_seq(17, m, WM_WINDOWPOSCHANGED, 8);
    KW(s_pc >= 0 && s_nc >= 0 && s_pd >= 0);
    KW(s_pc < s_nc && s_nc < s_pd);
    /* every one of them runs on this thread (kernel-originated, same-thread): no cross-thread hop */
    for (unsigned sl = 17; sl <= 22; sl++)
        for (int i = 0; ; i++) { struct r2_ev *e = r2_find(sl, m, i); if (!e) break; KEQ(e->tid, GetCurrentThreadId()); }
    DestroyWindow(h);
    UnregisterClassW(cls, wc.hInstance);
    return 0;
}

R2_SCN(destroywindow_callback_log_has_no_creation_slots) {
    NEED_HOOK();
    HWND h = k_make_window("r2cb-dnc");
    R2_NEED(h, "no window");
    k_pump(5);
    LONG m = r2_mark();
    K(DestroyWindow(h));
    /* tearing a window down never creates anything: no CREATESTRUCT, no LoadMenu, no CopyImage */
    KEQ(r2_since(10, m), 0);
    KEQ(r2_since(76, m), 0);
    KEQ(r2_since(64, m), 0);
    KW(r2_since(3, m) == 1);                    /* exactly one WM_NCDESTROY */
    return 0;
}
