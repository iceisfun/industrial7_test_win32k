/* r2_kernel_callback_table_full: argument structures of the message-carrying callbacks.
 *
 * Every Sfn* routine in win32kfull fills a buffer and calls KeUserModeCallback(slot, buf, len, ...):
 *   CAPTUREBUF family (slots 0, 10, 26, 76, ...): buf+0x00 u32 total length (== the `len` argument),
 *     buf+0x08 cCapturedPointers, buf+0x20 relocation base, then the payload starting at buf+0x28 (pwnd).
 *   plain family (slots 2, 3, 17, 18, 21, 22, ...): pwnd, msg, wParam, then lParam payload inline.
 * The user32 thunk passes (pwnd, msg, wParam, lParam, xpfnProc) to the dispatcher; for the pointer
 * messages lParam is a pointer INTO the callback buffer, and the kernel copies `cbOut` bytes back from there.
 */
#include "r2cb.h"

#define NEED_HOOK() do { R2_NEED(r2_gui_connect(), "no GUI connection / KernelCallbackTable"); \
                         R2_NEED(r2_install(), "cannot install the private callback table"); } while (0)

static struct r2_ev *find_msg(unsigned slot, LONG from, UINT msg, unsigned msg_off) {
    for (int i = 0; ; i++) {
        struct r2_ev *e = r2_find(slot, from, i);
        if (!e) return NULL;
        if (r2_d(e, msg_off) == msg) return e;
    }
}
static const unsigned char *find_bytes(const unsigned char *h, size_t hl, const void *n, size_t nl) {
    if (!nl || hl < nl) return NULL;
    for (size_t i = 0; i + nl <= hl; i++) if (!memcmp(h + i, n, nl)) return h + i;
    return NULL;
}

/* ---- capture proc: records what the window procedure sees for one message ---------------------- */
static struct {
    UINT msg; volatile LONG calls; WPARAM wp; LPARAM lp; DWORD tid;
    unsigned char mem[0x100]; size_t memsz;
    ULONG_PTR lppos;
    void (*mod)(void *lp, WPARAM wp);
    LRESULT ret;
} g_cap;
static LRESULT cap_proc(struct r2_wk *w, HWND h, UINT m, WPARAM wp, LPARAM lp, int *handled) {
    if (m != g_cap.msg) return 0;
    g_cap.calls++; g_cap.wp = wp; g_cap.lp = lp; g_cap.tid = GetCurrentThreadId();
    if (lp && g_cap.memsz) k_try_read((void *)lp, g_cap.mem, g_cap.memsz);
    g_cap.lppos = 0;
    if (m == WM_NCCALCSIZE && wp && lp) k_try_read_ptr((char *)lp + 0x30, (void **)&g_cap.lppos);
    if (g_cap.mod && lp) g_cap.mod((void *)lp, wp);
    *handled = 1;
    return g_cap.ret;
}
static void cap_reset(UINT msg, size_t memsz, LRESULT ret) {
    memset(&g_cap, 0, sizeof g_cap);
    g_cap.msg = msg; g_cap.memsz = memsz; g_cap.ret = ret;
}

/* ---- CREATESTRUCT (slot 10, SfnINLPCREATESTRUCT: KeUserModeCallback(10, buf, *(u32*)buf)) ---------- */
static UINT g_cs_msgs[4]; static LPARAM g_cs_lp[4]; static int g_cs_n;
static LRESULT CALLBACK cs_proc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if ((m == WM_NCCREATE || m == WM_CREATE) && g_cs_n < 4) { g_cs_msgs[g_cs_n] = m; g_cs_lp[g_cs_n] = lp; g_cs_n++; }
    return DefWindowProcW(h, m, wp, lp);
}
static BOOL reg_cs_class(WCHAR *cls, size_t cap, const char *tag) {
    WNDCLASSW wc; memset(&wc, 0, sizeof wc);
    _snwprintf(cls, cap - 1, L"win32ktR2_cs_%hs_%lu", tag, GetCurrentProcessId());
    wc.lpfnWndProc = cs_proc; wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = cls;
    return RegisterClassW(&wc) != 0;
}

R2_SCN(createstruct_buffer_layout_top_level) {
    NEED_HOOK();
    WCHAR cls[64];
    R2_NEED(reg_cs_class(cls, 64, "top"), "no class");
    HINSTANCE hi = GetModuleHandleW(NULL);
    g_cs_n = 0;
    LONG m = r2_mark();
    HWND h = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls, L"r2cs-title", WS_POPUP,
                             100, 200, 300, 400, NULL, NULL, hi, (LPVOID)0x1234);
    R2_NEED(h, "no window");
    void *pwnd = k_wnd_ptr(h);
    struct r2_ev *nc = find_msg(10, m, WM_NCCREATE, 0x30), *cr = find_msg(10, m, WM_CREATE, 0x30);
    R2_NEED(nc && cr, "no slot-10 callbacks for window creation");
    r2_dump("slot 10 WM_NCCREATE", nc, 0xd0);
    K(nc->seq < cr->seq);                               /* WM_NCCREATE is delivered before WM_CREATE */
    KEQ(r2_since(10, m), 2);
    KEQ(g_cs_n, 2);
    struct r2_ev *evs[2] = { nc, cr };
    for (int i = 0; i < 2; i++) {
        struct r2_ev *e = evs[i];
        KEQ(e->tid, GetCurrentThreadId());
        KEQ(r2_d(e, 0), e->len);                        /* CAPTUREBUF.cbCallback == the length handed to the callback */
        K(e->len >= 0xa8);                              /* AllocCallbackMessage(0xa8, ...) */
        KW(r2_q(e, 0x28) != 0);
        if (pwnd && r2_family()) KEQ(r2_q(e, 0x28), pwnd);
        KEQ(r2_q(e, 0x38), 0);                          /* wParam of WM_NCCREATE / WM_CREATE */
        /* CREATESTRUCT starts at buf+0x48: lpszName at +0x80 and lpszClass at +0x88 are what the thunk fixes up */
        KEQ(r2_q(e, 0x48), 0x1234);                     /* lpCreateParams */
        KEQ(r2_q(e, 0x50), (ULONG_PTR)hi);              /* hInstance */
        KEQ(r2_q(e, 0x58), 0);                          /* hMenu */
        KEQ(r2_q(e, 0x60), 0);                          /* hwndParent */
        KEQ((int)r2_d(e, 0x68), 400);                   /* cy */
        KEQ((int)r2_d(e, 0x6c), 300);                   /* cx */
        KEQ((int)r2_d(e, 0x70), 200);                   /* y */
        KEQ((int)r2_d(e, 0x74), 100);                   /* x */
        K((r2_d(e, 0x78) & WS_POPUP) != 0);
        K((r2_d(e, 0x78) & WS_CHILD) == 0);
        K((r2_d(e, 0x90) & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)) == (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE));
        K(r2_q(e, 0x98) != 0);                          /* xpfnProc */
        /* the window procedure's lParam is a pointer to that embedded CREATESTRUCT */
        KEQ(g_cs_lp[i], (ULONG_PTR)e->buf + 0x48);
    }
    KEQ(g_cs_msgs[0], WM_NCCREATE);
    KEQ(g_cs_msgs[1], WM_CREATE);
    DestroyWindow(h);
    UnregisterClassW(cls, hi);
    return 0;
}

R2_SCN(createstruct_child_window_carries_parent_and_id) {
    NEED_HOOK();
    WCHAR cls[64];
    R2_NEED(reg_cs_class(cls, 64, "child"), "no class");
    HINSTANCE hi = GetModuleHandleW(NULL);
    HWND parent = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls, L"parent", WS_POPUP, -32000, -32000, 100, 100, NULL, NULL, hi, NULL);
    R2_NEED(parent, "no parent");
    g_cs_n = 0;
    LONG m = r2_mark();
    HWND child = CreateWindowExW(0, cls, L"kid", WS_CHILD, 5, 6, 40, 30, parent, (HMENU)(ULONG_PTR)42, hi, NULL);
    R2_NEED(child, "no child");
    struct r2_ev *nc = find_msg(10, m, WM_NCCREATE, 0x30);
    R2_NEED(nc, "no callback");
    KEQ(r2_q(nc, 0x60), (ULONG_PTR)parent);             /* hwndParent */
    KEQ(r2_q(nc, 0x58), 42);                            /* hMenu carries the child id */
    K((r2_d(nc, 0x78) & WS_CHILD) != 0);
    KEQ((int)r2_d(nc, 0x74), 5);
    KEQ((int)r2_d(nc, 0x70), 6);
    KEQ((int)r2_d(nc, 0x6c), 40);
    KEQ((int)r2_d(nc, 0x68), 30);
    KEQ(nc->tid, GetCurrentThreadId());
    void *pchild = k_wnd_ptr(child);
    if (pchild && r2_family()) KEQ(r2_q(nc, 0x28), pchild);
    DestroyWindow(parent);
    UnregisterClassW(cls, hi);
    return 0;
}

R2_SCN(createstruct_name_and_class_strings_travel_in_the_buffer) {
    NEED_HOOK();
    WCHAR cls[64];
    R2_NEED(reg_cs_class(cls, 64, "str"), "no class");
    HINSTANCE hi = GetModuleHandleW(NULL);
    static const WCHAR title[] = L"r2cs-title-0123456789";
    LONG m = r2_mark();
    HWND h = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls, title, WS_POPUP, -32000, -32000, 8, 8, NULL, NULL, hi, NULL);
    R2_NEED(h, "no window");
    struct r2_ev *nc = find_msg(10, m, WM_NCCREATE, 0x30);
    R2_NEED(nc, "no callback");
    /* Windows 10: the class name is captured behind the fixed part (lpszClass at buf+0x88 is an offset into the buffer until the
     * thunk fixes it up), but for a window created by the calling process itself lpszName at buf+0x80 is the caller's own pointer,
     * so the title is not copied into the buffer. */
    K(nc->len > 0xa8);
    K(find_bytes(nc->data, nc->ncopy, cls, wcslen(cls) * sizeof(WCHAR)) != NULL);
    KEQ(r2_q(nc, 0x80), (ULONG_PTR)title);
    K(find_bytes(nc->data, nc->ncopy, title, 21 * sizeof(WCHAR)) == NULL);
    DestroyWindow(h);
    UnregisterClassW(cls, hi);
    return 0;
}

/* ---- WM_COPYDATA (slot 0, SfnCOPYDATA: KeUserModeCallback(0, buf, *(u32*)buf), min size 0x70) ------ */
R2_SCN(copydata_buffer_layout_and_payload) {
    NEED_HOOK();
    struct r2_wk w; memset(&w, 0, sizeof w);
    w.proc = cap_proc;
    R2_NEED(r2_wk_start(&w, "cd", 0), "no worker");
    cap_reset(WM_COPYDATA, sizeof(COPYDATASTRUCT), 0x77);
    static const unsigned char payload[24] = { 'R','2','C','D',0xDE,0xAD,0xBE,0xEF, 1,2,3,4,5,6,7,8, 0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88 };
    COPYDATASTRUCT cds; cds.dwData = 0x1122334455667788ULL; cds.cbData = sizeof payload; cds.lpData = (PVOID)payload;
    HWND sender = (HWND)(ULONG_PTR)0x4242;
    LONG m = r2_mark();
    LRESULT r = 0;
    KW(r2_send(w.hwnd, WM_COPYDATA, (WPARAM)sender, (LPARAM)&cds, &r));
    KEQ(r, 0x77);
    struct r2_ev *e = find_msg(0, m, WM_COPYDATA, 0x30);
    R2_NEED(e, "no slot-0 callback");
    KEQ(e->tid, w.tid);
    KEQ(r2_d(e, 0), e->len);
    K(e->len >= 0x70 + sizeof payload);
    KEQ(r2_q(e, 0x38), (ULONG_PTR)sender);            /* wParam passes through untouched */
    K(r2_d(e, 0x40) != 0);                            /* lParam was non-NULL: the thunk will pass buf+0x48 */
    KEQ(r2_q(e, 0x48), 0x1122334455667788ULL);       /* COPYDATASTRUCT.dwData */
    KEQ(r2_d(e, 0x50), sizeof payload);              /* .cbData */
    void *pwnd = k_wnd_ptr(w.hwnd);
    if (pwnd && r2_family()) KEQ(r2_q(e, 0x28), pwnd);
    K(find_bytes(e->data, e->ncopy, payload, sizeof payload) != NULL);   /* the bytes ride inside the buffer */
    /* the window procedure got a COPYDATASTRUCT that lives in the buffer, not the sender's memory */
    KEQ(g_cap.lp, (ULONG_PTR)e->buf + 0x48);
    K(g_cap.lp != (ULONG_PTR)&cds);
    COPYDATASTRUCT seen; memcpy(&seen, g_cap.mem, sizeof seen);
    KEQ(seen.dwData, 0x1122334455667788ULL);
    KEQ(seen.cbData, sizeof payload);
    K(seen.lpData != (PVOID)payload);
    r2_wk_stop(&w);
    return 0;
}

R2_SCN(copydata_null_lparam_has_no_struct) {
    NEED_HOOK();
    struct r2_wk w; memset(&w, 0, sizeof w);
    w.proc = cap_proc;
    R2_NEED(r2_wk_start(&w, "cd0", 0), "no worker");
    cap_reset(WM_COPYDATA, 0, 0);
    LONG m = r2_mark();
    LRESULT r = 5;
    SetLastError(0);
    int sent0 = r2_send(w.hwnd, WM_COPYDATA, 7, 0, &r);
    DWORD err = GetLastError();
    /* Windows 10: a WM_COPYDATA without a COPYDATASTRUCT is refused in the kernel. SendMessageTimeout returns 0 with the last error
     * still 0, nothing reaches the window procedure and no slot-0 callback is made. */
    KEQ(sent0, 0);
    KEQ(err, 0);
    K(find_msg(0, m, WM_COPYDATA, 0x30) == NULL);
    KEQ(g_cap.calls, 0);
    r2_wk_stop(&w);
    return 0;
}

/* The payload of a large WM_COPYDATA lives in memory that exists only for the duration of the call, so it is compared inside the
 * window procedure. */
static struct { const unsigned char *want; ULONG_PTR dwData; ULONG cb; int same; LONG calls; } g_big;
static LRESULT bigcd_proc(struct r2_wk *w, HWND h, UINT m, WPARAM wp, LPARAM lp, int *handled) {
    if (m != WM_COPYDATA) return 0;
    g_big.calls++;
    if (lp && !IsBadReadPtr((void *)lp, sizeof(COPYDATASTRUCT))) {
        COPYDATASTRUCT *c = (COPYDATASTRUCT *)lp;
        g_big.dwData = c->dwData; g_big.cb = c->cbData;
        g_big.same = c->lpData && !IsBadReadPtr(c->lpData, c->cbData) && !memcmp(c->lpData, g_big.want, c->cbData);
    }
    *handled = 1;
    return 1;
}
R2_SCN(copydata_payload_larger_than_the_inline_copy) {
    NEED_HOOK();
    struct r2_wk w; memset(&w, 0, sizeof w);
    w.proc = bigcd_proc;
    R2_NEED(r2_wk_start(&w, "cdbig", 0), "no worker");
    enum { N = 6000 };
    unsigned char *big = (unsigned char *)malloc(N);
    R2_NEED(big, "no memory");
    for (int i = 0; i < N; i++) big[i] = (unsigned char)(i * 7 + 3);
    memset(&g_big, 0, sizeof g_big);
    g_big.want = big;
    COPYDATASTRUCT cds; cds.dwData = 9; cds.cbData = N; cds.lpData = big;
    LONG m = r2_mark();
    LRESULT r = 0;
    KW(r2_send(w.hwnd, WM_COPYDATA, 1, (LPARAM)&cds, &r));
    struct r2_ev *e = find_msg(0, m, WM_COPYDATA, 0x30);
    R2_NEED(e, "no callback");
    /* Windows 10: a payload this large is not copied into the callback buffer. The buffer keeps the fixed 0x78 bytes (the
     * COPYDATASTRUCT with its length) and the data is handed over through separate memory: the procedure still sees the whole
     * payload, byte for byte. */
    KEQ(e->len, 0x78);
    KEQ(r2_d(e, 0), e->len);
    KEQ(r2_d(e, 0x50), N);
    KEQ(g_big.calls, 1);
    KEQ(g_big.dwData, 9);
    KEQ(g_big.cb, N);
    K(g_big.same);
    free(big);
    r2_wk_stop(&w);
    return 0;
}

/* ---- WM_NCCALCSIZE (slot 21 = 0x15, SfnINOUTNCCALCSIZE) ----------------------------------------- */
R2_SCN(nccalcsize_rect_form_lparam_points_into_buffer) {
    NEED_HOOK();
    struct r2_wk w; memset(&w, 0, sizeof w);
    w.proc = cap_proc;
    R2_NEED(r2_wk_start(&w, "ncr", 0), "no worker");
    cap_reset(WM_NCCALCSIZE, sizeof(RECT), 0);
    RECT rc = { 10, 20, 310, 220 };
    LONG m = r2_mark();
    LRESULT r = 99;
    KW(r2_send(w.hwnd, WM_NCCALCSIZE, FALSE, (LPARAM)&rc, &r));
    struct r2_ev *e = find_msg(21, m, WM_NCCALCSIZE, 8);
    R2_NEED(e, "no slot-21 callback");
    KEQ(e->tid, w.tid);
    KEQ(r2_q(e, 0x10), 0);                              /* wParam FALSE */
    RECT in; memcpy(&in, e->data + 0x28, sizeof in);
    KEQ(in.left, 10); KEQ(in.top, 20); KEQ(in.right, 310); KEQ(in.bottom, 220);
    /* __fnINOUTNCCALCSIZE: lParam = &buf[5 qwords] = buf+0x28 */
    KEQ(g_cap.lp, (ULONG_PTR)e->buf + 0x28);
    KEQ(g_cap.wp, 0);
    KEQ(r, 0);
    r2_wk_stop(&w);
    return 0;
}

R2_SCN(nccalcsize_params_form_sets_lppos_inside_buffer) {
    NEED_HOOK();
    struct r2_wk w; memset(&w, 0, sizeof w);
    w.proc = cap_proc;
    R2_NEED(r2_wk_start(&w, "ncp", 0), "no worker");
    cap_reset(WM_NCCALCSIZE, sizeof(NCCALCSIZE_PARAMS), 0x300);
    NCCALCSIZE_PARAMS p; memset(&p, 0, sizeof p);
    WINDOWPOS wpos; memset(&wpos, 0, sizeof wpos);
    wpos.hwnd = w.hwnd; wpos.x = 11; wpos.y = 22; wpos.cx = 33; wpos.cy = 44; wpos.flags = SWP_NOZORDER | SWP_NOACTIVATE;
    p.rgrc[0].left = 1; p.rgrc[0].top = 2; p.rgrc[0].right = 301; p.rgrc[0].bottom = 202;
    p.rgrc[1].left = 5; p.rgrc[1].top = 6; p.rgrc[1].right = 7; p.rgrc[1].bottom = 8;
    p.rgrc[2].left = 9; p.rgrc[2].top = 10; p.rgrc[2].right = 11; p.rgrc[2].bottom = 12;
    p.lppos = &wpos;
    LONG m = r2_mark();
    LRESULT r = 0;
    KW(r2_send(w.hwnd, WM_NCCALCSIZE, TRUE, (LPARAM)&p, &r));
    struct r2_ev *e = find_msg(21, m, WM_NCCALCSIZE, 8);
    R2_NEED(e, "no slot-21 callback");
    KEQ(r2_q(e, 0x10), 1);                              /* wParam TRUE */
    KEQ(g_cap.lp, (ULONG_PTR)e->buf + 0x28);
    /* with wParam != 0 the thunk stores buf+0x60 into the lppos member (lParam+0x30 == buf+0x58) */
    KEQ(g_cap.lppos, (ULONG_PTR)e->buf + 0x60);
    NCCALCSIZE_PARAMS seen; memcpy(&seen, g_cap.mem, sizeof seen);
    KEQ(seen.rgrc[0].left, 1); KEQ(seen.rgrc[0].bottom, 202);
    KEQ(seen.rgrc[1].top, 6); KEQ(seen.rgrc[2].right, 11);
    KEQ(r, 0x300);                                      /* WVR flags returned by the procedure */
    r2_wk_stop(&w);
    return 0;
}

/* ---- WINDOWPOS / MINMAXINFO (slots 22, 17, 18) ----------------------------------------------------- */
static void wp_changing_mod(void *lp, WPARAM wp) { WINDOWPOS *p = (WINDOWPOS *)lp; p->x += 100; p->y += 200; p->cx += 3; p->cy += 4; p->flags |= SWP_NOACTIVATE; }
R2_SCN(windowpos_changing_is_in_out_through_slot_22) {
    NEED_HOOK();
    struct r2_wk w; memset(&w, 0, sizeof w);
    w.proc = cap_proc;
    R2_NEED(r2_wk_start(&w, "wpc", 0), "no worker");
    cap_reset(WM_WINDOWPOSCHANGING, sizeof(WINDOWPOS), 0);
    g_cap.mod = wp_changing_mod;
    WINDOWPOS p; memset(&p, 0, sizeof p);
    p.hwnd = w.hwnd; p.hwndInsertAfter = HWND_TOP; p.x = 1; p.y = 2; p.cx = 30; p.cy = 40; p.flags = SWP_NOZORDER;
    LONG m = r2_mark();
    LRESULT r = 9;
    KW(r2_send(w.hwnd, WM_WINDOWPOSCHANGING, 0, (LPARAM)&p, &r));
    struct r2_ev *e = find_msg(22, m, WM_WINDOWPOSCHANGING, 8);
    R2_NEED(e, "no slot-22 callback");
    KEQ(e->len, 0x50);                                  /* KeUserModeCallback(0x16, &buf, 0x50, ...) */
    KEQ(g_cap.lp, (ULONG_PTR)e->buf + 0x18);            /* __fnINOUTLPWINDOWPOS: lParam = buffer + 3 qwords */
    WINDOWPOS in; memcpy(&in, e->data + 0x18, sizeof in);
    KEQ(in.hwnd, (ULONG_PTR)w.hwnd); KEQ(in.x, 1); KEQ(in.y, 2); KEQ(in.cx, 30); KEQ(in.cy, 40); KEQ(in.flags, SWP_NOZORDER);
    /* ... and the 0x28 bytes behind it (cbOut) are copied back into the sender's WINDOWPOS */
    KEQ(p.x, 101); KEQ(p.y, 202); KEQ(p.cx, 33); KEQ(p.cy, 44);
    K((p.flags & SWP_NOACTIVATE) != 0);
    r2_wk_stop(&w);
    return 0;
}

R2_SCN(windowpos_changed_is_in_only_through_slot_17) {
    NEED_HOOK();
    struct r2_wk w; memset(&w, 0, sizeof w);
    w.proc = cap_proc;
    R2_NEED(r2_wk_start(&w, "wpd", 0), "no worker");
    cap_reset(WM_WINDOWPOSCHANGED, sizeof(WINDOWPOS), 0);
    g_cap.mod = wp_changing_mod;                        /* modifies the callback's copy only */
    WINDOWPOS p; memset(&p, 0, sizeof p);
    p.hwnd = w.hwnd; p.x = 4; p.y = 5; p.cx = 6; p.cy = 7;
    LONG m = r2_mark();
    LRESULT r;
    KW(r2_send(w.hwnd, WM_WINDOWPOSCHANGED, 0, (LPARAM)&p, &r));
    struct r2_ev *e = find_msg(17, m, WM_WINDOWPOSCHANGED, 8);
    R2_NEED(e, "no slot-17 callback");
    KEQ(e->len, 0x50);
    KEQ(g_cap.lp, (ULONG_PTR)e->buf + 0x18);
    KEQ(p.x, 4); KEQ(p.y, 5); KEQ(p.cx, 6); KEQ(p.cy, 7);   /* INLPWINDOWPOS: nothing is copied back */
    r2_wk_stop(&w);
    return 0;
}

static void mmi_mod(void *lp, WPARAM wp) { MINMAXINFO *p = (MINMAXINFO *)lp; p->ptMaxSize.x = 1111; p->ptMinTrackSize.y = 44; }
R2_SCN(getminmaxinfo_is_in_out_through_slot_18) {
    NEED_HOOK();
    struct r2_wk w; memset(&w, 0, sizeof w);
    w.proc = cap_proc;
    R2_NEED(r2_wk_start(&w, "mmi", 0), "no worker");
    cap_reset(WM_GETMINMAXINFO, sizeof(MINMAXINFO), 0);
    g_cap.mod = mmi_mod;
    MINMAXINFO mi; memset(&mi, 0, sizeof mi);
    mi.ptMaxSize.x = 640; mi.ptMaxSize.y = 480; mi.ptMaxPosition.x = 3; mi.ptMinTrackSize.x = 20;
    LONG m = r2_mark();
    LRESULT r;
    KW(r2_send(w.hwnd, WM_GETMINMAXINFO, 0, (LPARAM)&mi, &r));
    struct r2_ev *e = find_msg(18, m, WM_GETMINMAXINFO, 8);
    R2_NEED(e, "no slot-18 callback");
    KEQ(e->len, 0x50);                                  /* SfnINOUTLPPOINT5: KeUserModeCallback(0x12, &buf, 0x50, ...) */
    KEQ(g_cap.lp, (ULONG_PTR)e->buf + 0x18);
    MINMAXINFO in; memcpy(&in, e->data + 0x18, sizeof in);
    KEQ(in.ptMaxSize.x, 640); KEQ(in.ptMaxSize.y, 480); KEQ(in.ptMaxPosition.x, 3); KEQ(in.ptMinTrackSize.x, 20);
    KEQ(mi.ptMaxSize.x, 1111); KEQ(mi.ptMinTrackSize.y, 44);       /* five POINTs (0x28 bytes) copied back */
    KEQ(mi.ptMaxSize.y, 480);
    r2_wk_stop(&w);
    return 0;
}

/* ---- strings (slots 26, 7/35, 6) ------------------------------------------------------------------ */
R2_SCN(settext_goes_through_instring_slot_26) {
    NEED_HOOK();
    struct r2_wk w; memset(&w, 0, sizeof w);
    w.proc = cap_proc;
    R2_NEED(r2_wk_start(&w, "stx", 0), "no worker");
    cap_reset(WM_SETTEXT, 0x40, TRUE);
    LONG m = r2_mark();
    LRESULT r = 0;
    static const WCHAR text[] = L"r2cb-set-text-\x00e9\x4e2d";
    KW(r2_send(w.hwnd, WM_SETTEXT, 0, (LPARAM)text, &r));
    KEQ(r, TRUE);
    /* Which callback carries a sent WM_SETTEXT is a property of the kernel's message table: find the one that did (any slot, the
     * message number at buf+0x30 in the capture-buffer layout or at buf+8 in the plain one) and record it. */
    struct r2_ev *e = NULL; unsigned moff = 0;
    for (LONG i = m; i < (r2_ev_n < R2_NEV ? r2_ev_n : R2_NEV) && !e; i++) {
        struct r2_ev *x = &r2_ev_log[i];
        if (x->tid != w.tid) continue;
        if (r2_d(x, 0x30) == WM_SETTEXT && r2_d(x, 0) == x->len) { e = x; moff = 0x30; }
        else if (r2_d(x, 8) == WM_SETTEXT && x->len <= 0x30) { e = x; moff = 8; }
    }
    R2_NEED(e, "no callback carried the message");
    KNOTE("WM_SETTEXT used slot %u (message at buf+0x%x, len 0x%lx)", e->idx, moff, (unsigned long)e->len);
    KEQ(e->tid, w.tid);
    K(find_bytes(e->data, e->ncopy, text, sizeof text - sizeof(WCHAR)) != NULL);
    /* the procedure's lParam is a string pointer into the buffer, not the sender's */
    K(g_cap.lp != (ULONG_PTR)text);
    K(g_cap.lp >= (ULONG_PTR)e->buf && g_cap.lp < (ULONG_PTR)e->buf + e->len);
    r2_wk_stop(&w);
    return 0;
}

static LRESULT gettext_proc(struct r2_wk *w, HWND h, UINT m, WPARAM wp, LPARAM lp, int *handled) {
    if (m != WM_GETTEXT) return 0;
    g_cap.calls++; g_cap.wp = wp; g_cap.lp = lp;
    WCHAR *d = (WCHAR *)lp; const WCHAR *s = L"r2cb-out-text"; size_t i;
    for (i = 0; s[i] && i + 1 < wp; i++) d[i] = s[i];
    d[i] = 0;
    *handled = 1;
    return (LRESULT)i;
}
R2_SCN(gettext_returns_text_through_a_counted_out_string_slot) {
    NEED_HOOK();
    struct r2_wk w; memset(&w, 0, sizeof w);
    w.proc = gettext_proc;
    R2_NEED(r2_wk_start(&w, "gtx", 0), "no worker");
    cap_reset(WM_GETTEXT, 0, 0);
    WCHAR out[64]; memset(out, 0xAA, sizeof out);
    LONG m = r2_mark();
    LRESULT r = 0;
    KW(r2_send(w.hwnd, WM_GETTEXT, 64, (LPARAM)out, &r));
    KEQ(r, 13);
    K(!wcscmp(out, L"r2cb-out-text"));
    /* SfnINCNTOUTSTRING (7) or SfnOUTSTRING (35) carries it; the count is wParam at buf+0x38 */
    struct r2_ev *e = find_msg(7, m, WM_GETTEXT, 0x30);
    if (!e) e = find_msg(35, m, WM_GETTEXT, 0x30);
    KW(e != NULL);
    if (e) { KEQ(r2_q(e, 0x38), 64); KEQ(e->tid, w.tid); KEQ(r2_d(e, 0), e->len); KNOTE("WM_GETTEXT used slot %u", e->idx); }
    r2_wk_stop(&w);
    return 0;
}

R2_SCN(gettextlength_uses_the_text_lengths_slot_6) {
    NEED_HOOK();
    struct r2_wk w; memset(&w, 0, sizeof w);
    w.proc = cap_proc;
    R2_NEED(r2_wk_start(&w, "gtl", 0), "no worker");
    cap_reset(WM_GETTEXTLENGTH, 0, 1234);
    LONG m = r2_mark();
    LRESULT r = 0;
    KW(r2_send(w.hwnd, WM_GETTEXTLENGTH, 0, 0, &r));
    KEQ(r, 1234);
    struct r2_ev *e = find_msg(6, m, WM_GETTEXTLENGTH, 8);
    KW(e != NULL);
    if (e) { KEQ(e->tid, w.tid); K(e->len >= 0x20); }
    r2_wk_stop(&w);
    return 0;
}
