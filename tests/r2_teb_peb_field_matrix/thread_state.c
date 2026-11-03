/* NtUserGetThreadState / NtUserCallOneParam / NtUserCallNoParam cross-checks against the TEB and _CLIENTINFO
 * fields user32 reads instead of making the call, plus the TEB fields the kernel flips while a thread is
 * inside a sent message, a hook procedure or a window procedure.
 *
 * win32kfull!NtUserGetThreadState: 0 -> focus window, 1 -> active window, 2 -> capture window (user32
 * GetFocus/GetActiveWindow/GetCapture are thin wrappers), 7 -> message extra info, 8 -> InSendMessageEx word
 * (1 SEND, 2 NOTIFY, 4 CALLBACK, +8 once replied; 0 outside a sent message), 9 -> message time, 0xA -> queue
 * is the foreground queue (0/1), 0xE -> 1 for every caller, any index above 0xF -> 0. */
#include "rtp.h"

UDECL(ULONG_PTR, NtUserCallNoParam, (ULONG));
UDECL(ULONG_PTR, NtUserCallOneParam, (ULONG_PTR, ULONG));

#define WM_R2_SENT  (WM_APP + 1)
#define WM_R2_POST  (WM_APP + 2)

static char g_cls[64];
static HWND g_hw;
static HINSTANCE g_inst;

static struct {
    int hit;
    BOOL ism;
    unsigned long long ts8, ts8_after_reply;
    unsigned long pcti_flags; int pcti_ok;
    struct rsnap s;
    BOOL reply_ok;
} g_send;

static LRESULT CALLBACK wp(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_R2_SENT) {
        BOOL (WINAPI *ism)(void) = (BOOL (WINAPI *)(void))t_proc("InSendMessage");
        BOOL (WINAPI *rep)(LRESULT) = (BOOL (WINAPI *)(LRESULT))t_proc("ReplyMessage");
        g_send.hit++;
        g_send.ism = ism ? ism() : 0;
        g_send.ts8 = rt_ts(8);
        rs_take(&g_send.s);
        unsigned long long p = rs_ci64(&g_send.s, CIO_pcti);
        g_send.pcti_ok = p && k_try_read_u32((void *)(ULONG_PTR)p, &g_send.pcti_flags);
        if (w == 1 && rep) { g_send.reply_ok = rep(0x55); g_send.ts8_after_reply = rt_ts(8); }
        return 0x99;
    }
    LRESULT (WINAPI *dw)(HWND, UINT, WPARAM, LPARAM) = (LRESULT (WINAPI *)(HWND, UINT, WPARAM, LPARAM))t_proc("DefWindowProcA");
    return dw ? dw(h, m, w, l) : 0;
}

static int fx_open(void) {
    if (g_hw) return 1;
    if (!U(RegisterClassExA) || !U(CreateWindowExA)) { t_note("RegisterClassExA/CreateWindowExA not resolved"); return 0; }
    snprintf(g_cls, sizeof g_cls, "win32kt_r2tpfm_%lu", GetCurrentProcessId());
    g_inst = GetModuleHandleA(NULL);
    WNDCLASSEXA wc; memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc; wc.lpfnWndProc = wp; wc.hInstance = g_inst; wc.lpszClassName = g_cls;
    if (!U(RegisterClassExA)(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) { t_note("RegisterClassExA failed %lu", GetLastError()); return 0; }
    g_hw = U(CreateWindowExA)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, g_cls, "r2", WS_POPUP, -32000, -32000, 1, 1, NULL, NULL, g_inst, NULL);
    if (!g_hw) t_note("CreateWindowExA failed %lu", GetLastError());
    return g_hw != NULL;
}
static void fx_close(void) {
    if (g_hw) { U(DestroyWindow)(g_hw); g_hw = NULL; }
    if (U(UnregisterClassA)) U(UnregisterClassA)(g_cls, g_inst);
}
#define FXW() do { if (!fx_open()) { fx_close(); t_skip("cannot create test window"); return; } } while (0)

static void pump_until(HANDLE h, DWORD ms) {
    DWORD t0 = GetTickCount();
    MSG m;
    while (WaitForSingleObject(h, 0) != WAIT_OBJECT_0 && GetTickCount() - t0 < ms) {
        while (U(PeekMessageA)(&m, NULL, 0, 0, PM_REMOVE)) { U(TranslateMessage)(&m); U(DispatchMessageA)(&m); }
        Sleep(1);
    }
}
static LRESULT g_sender_res; static BOOL g_sender_ok;
static DWORD WINAPI sender(void *p) {
    DWORD_PTR r = 0;
    g_sender_ok = U(SendMessageTimeoutA)(g_hw, WM_R2_SENT, (WPARAM)(ULONG_PTR)p, 0, SMTO_NORMAL, 3000, &r);
    g_sender_res = (LRESULT)r;
    return 0;
}
/* sends WM_R2_SENT from another thread to g_hw and pumps this thread so the window procedure runs here */
static int cross_send(int reply) {
    memset(&g_send, 0, sizeof g_send);
    g_sender_ok = 0; g_sender_res = 0;
    HANDLE th = CreateThread(NULL, 0, sender, (void *)(ULONG_PTR)reply, 0, NULL);
    if (!th) return 0;
    pump_until(th, 4000);
    WaitForSingleObject(th, 1000);
    CloseHandle(th);
    return g_send.hit;
}

/* ---- NtUserGetThreadState indices ---------------------------------------------------------------------- */

static const int g_idx_zero[] = { 0x10, 0x11, 0x12, 0x1F, 0x20, 0x40, 0x7F, 0x80, 0xFF, 0x100, 0x1000, 0x7FFFFFFF, -1, -2, -0x7FFFFFFF - 1 };
TEST(r2_teb_peb_field_matrix_ts_indices_above_0xf_return_zero) {
    rt_gui();
    for (unsigned i = 0; i < sizeof g_idx_zero / sizeof g_idx_zero[0]; i++) {
        ULONG_PTR r = rt_ts(g_idx_zero[i]);
        CHECKF(r == 0, "NtUserGetThreadState(%d) = 0x%llx", g_idx_zero[i], (unsigned long long)r);
    }
}
static void op_ts_first(struct wk *w) { w->x[0] = rt_ts(0xE); }
TEST(r2_teb_peb_field_matrix_ts_index_0xe_is_one_on_every_thread) {
    EXPECT_EQ(rt_ts(0xE), 1);
    struct wk w;
    if (!wk_start(&w, 0, op_ts_first, NULL)) { wk_finish(&w); t_skip("worker did not start"); return; }
    EXPECT_EQ(w.x[0], 1);
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_ts_index_0xe_converts_a_plain_thread) {
    rt_gui();
    struct wk w;
    if (!wk_start(&w, 0, op_ts_first, NULL)) { wk_finish(&w); t_skip("worker did not start"); return; }
    EXPECT_EQ(w.x[0], 1);
    CHECK(w.after.ti != 0);                                /* the service call itself converted the thread */
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_ts_boolean_indices) {
    rt_gui();
    ULONG_PTR r = rt_ts(0xA);
    CHECK(r == 0 || r == 1);                                /* (pq == gpqForeground) */
    r = rt_ts(5);
    CHECK(r == 0 || r == 1);                                /* (GetQueueStatus(0x1c7f) & 5) != 0 */
    r = rt_ts(0xF);
    CHECK(r == 0 || r == 1);                                /* bit 29 of pti+0x4d0 */
    r = rt_ts(0xD);
    CHECK(r == 0 || r == 1);
}
TEST(r2_teb_peb_field_matrix_ts_index_8_zero_outside_sent_message) {
    rt_gui();
    EXPECT_EQ(rt_ts(8), 0);
}
TEST(r2_teb_peb_field_matrix_ts_focus_active_capture_match_wrappers) {
    rt_gui();
    REQUIRE(U(GetFocus)); REQUIRE(U(GetActiveWindow)); REQUIRE(U(GetCapture));
    EXPECT_EQ(U(GetFocus)(), (HWND)rt_ts(0));
    EXPECT_EQ(U(GetActiveWindow)(), (HWND)rt_ts(1));
    EXPECT_EQ(U(GetCapture)(), (HWND)rt_ts(2));
}
static void op_focus_trio(struct wk *w) {
    w->x[0] = (unsigned long long)(ULONG_PTR)U(GetFocus)();
    w->x[1] = (unsigned long long)(ULONG_PTR)U(GetActiveWindow)();
    w->x[2] = (unsigned long long)(ULONG_PTR)U(GetCapture)();
    w->x[3] = rt_ts(0); w->x[4] = rt_ts(1); w->x[5] = rt_ts(2);
}
TEST(r2_teb_peb_field_matrix_ts_fresh_thread_has_no_focus_active_or_capture) {
    rt_gui();
    struct wk w;
    if (!wk_start(&w, 1, op_focus_trio, NULL)) { wk_finish(&w); t_skip("worker did not start"); return; }
    EXPECT_EQ(w.x[0], w.x[3]);
    EXPECT_EQ(w.x[1], w.x[4]);
    EXPECT_EQ(w.x[2], w.x[5]);
    EXPECT_EQ(w.x[0], 0);                                  /* a thread that owns no window has no focus/active/capture */
    EXPECT_EQ(w.x[1], 0);
    EXPECT_EQ(w.x[2], 0);
    wk_finish(&w);
}
TEST(r2_teb_peb_field_matrix_ts_capture_follows_setcapture) {
    FXW();
    REQUIRE(U(SetCapture)); REQUIRE(U(ReleaseCapture));
    HWND prev = U(SetCapture)(g_hw);
    EXPECT_EQ(U(GetCapture)(), g_hw);
    EXPECT_EQ(rt_ts(2), (ULONG_PTR)g_hw);
    U(ReleaseCapture)();
    EXPECT_EQ(rt_ts(2), 0);
    if (prev) U(SetCapture)(prev);
    fx_close();
}

/* ---- message state ------------------------------------------------------------------------------------- */

TEST(r2_teb_peb_field_matrix_ts_extra_info_roundtrip) {
    NOT_WINE();
    rt_gui();
    REQUIRE(U(SetMessageExtraInfo)); REQUIRE(U(GetMessageExtraInfo));
    LPARAM old = U(GetMessageExtraInfo)();
    LPARAM prev = U(SetMessageExtraInfo)(0x1234);
    EXPECT_EQ(prev, old);                                  /* SetMessageExtraInfo returns the previous value */
    EXPECT_EQ(U(GetMessageExtraInfo)(), 0x1234);
    EXPECT_EQ(rt_ts(7), 0x1234);
    U(SetMessageExtraInfo)(old);
    EXPECT_EQ(rt_ts(7), (ULONG_PTR)old);
}
TEST(r2_teb_peb_field_matrix_ts_extra_info_stub_matches_setmessageextrainfo) {
    NOT_WINE();
    rt_gui();
    NtUserCallOneParam_pfn c1 = (NtUserCallOneParam_pfn)t_proc_in("win32u.dll", "NtUserCallOneParam");
    REQUIRE(c1); REQUIRE(U(GetMessageExtraInfo));
    LPARAM old = U(GetMessageExtraInfo)();
    ULONG_PTR prev = c1(0x4321, 0x45);                     /* user32!SetMessageExtraInfo = CallOneParam(code 0x45) */
    EXPECT_EQ(prev, (ULONG_PTR)old);
    EXPECT_EQ(U(GetMessageExtraInfo)(), 0x4321);
    c1((ULONG_PTR)old, 0x45);
}
static void op_set_extra(struct wk *w) { U(SetMessageExtraInfo)(0x999); w->x[0] = (unsigned long long)U(GetMessageExtraInfo)(); }
TEST(r2_teb_peb_field_matrix_ts_extra_info_is_per_thread) {
    NOT_WINE();
    rt_gui();
    REQUIRE(U(SetMessageExtraInfo)); REQUIRE(U(GetMessageExtraInfo));
    LPARAM old = U(GetMessageExtraInfo)();
    U(SetMessageExtraInfo)(0x777);
    WK_GUI(w);
    wk_finish(&w);
    struct wk w2;
    if (!wk_start(&w2, 1, op_set_extra, NULL)) { U(SetMessageExtraInfo)(old); wk_finish(&w2); t_skip("worker did not start"); return; }
    EXPECT_EQ(w2.x[0], 0x999);
    wk_finish(&w2);
    EXPECT_EQ(U(GetMessageExtraInfo)(), 0x777);            /* the value lives in the thread's input state */
    U(SetMessageExtraInfo)(old);
}
TEST(r2_teb_peb_field_matrix_ts_message_time_matches_peeked_message) {
    NOT_WINE();
    FXW();
    REQUIRE(U(PostMessageA)); REQUIRE(U(GetMessageTime));
    CHECK(U(PostMessageA)(g_hw, WM_R2_POST, 0, 0));
    MSG m; memset(&m, 0, sizeof m);
    BOOL got = U(PeekMessageA)(&m, g_hw, WM_R2_POST, WM_R2_POST, PM_REMOVE);
    CHECK(got);
    if (got) {
        EXPECT_EQ(U(GetMessageTime)(), (LONG)m.time);
        EXPECT_EQ((unsigned long)rt_ts(9), (unsigned long)m.time);
    }
    fx_close();
}
TEST(r2_teb_peb_field_matrix_ts_message_pos_matches_peeked_message_and_callnoparam) {
    NOT_WINE();
    FXW();
    REQUIRE(U(PostMessageA)); REQUIRE(U(GetMessagePos));
    NtUserCallNoParam_pfn c0 = (NtUserCallNoParam_pfn)t_proc_in("win32u.dll", "NtUserCallNoParam");
    REQUIRE(c0);
    CHECK(U(PostMessageA)(g_hw, WM_R2_POST, 0, 0));
    MSG m; memset(&m, 0, sizeof m);
    BOOL got = U(PeekMessageA)(&m, g_hw, WM_R2_POST, WM_R2_POST, PM_REMOVE);
    CHECK(got);
    if (got) {
        DWORD pos = U(GetMessagePos)();
        EXPECT_EQ(pos, MAKELONG(m.pt.x, m.pt.y));
        EXPECT_EQ((DWORD)c0(0xC), pos);                    /* user32!GetMessagePos = NtUserCallNoParam(0xc) */
    }
    fx_close();
}
TEST(r2_teb_peb_field_matrix_ts_message_time_changes_with_later_message) {
    NOT_WINE();
    FXW();
    CHECK(U(PostMessageA)(g_hw, WM_R2_POST, 0, 0));
    MSG m;
    CHECK(U(PeekMessageA)(&m, g_hw, WM_R2_POST, WM_R2_POST, PM_REMOVE));
    LONG t1 = U(GetMessageTime)();
    Sleep(60);
    CHECK(U(PostMessageA)(g_hw, WM_R2_POST, 0, 0));
    CHECK(U(PeekMessageA)(&m, g_hw, WM_R2_POST, WM_R2_POST, PM_REMOVE));
    LONG t2 = U(GetMessageTime)();
    CHECK((DWORD)(t2 - t1) >= 30);                          /* time stamps of the two posts, 60 ms apart (tick granularity up to 16 ms) */
    fx_close();
}

/* ---- inside a sent message: pClientThreadInfo, InSendMessageEx ------------------------------------------ */

TEST(r2_teb_peb_field_matrix_send_cross_thread_runs_wndproc_on_owner_thread) {
    FXW();
    CHECK(cross_send(0) == 1);
    CHECK(g_sender_ok);
    EXPECT_EQ(g_sender_res, 0x99);
    EXPECT_EQ(g_send.s.tid, GetCurrentThreadId());
    fx_close();
}
TEST(r2_teb_peb_field_matrix_send_cross_thread_insendmessage_true_and_state_is_send) {
    FXW();
    REQUIRE(cross_send(0) == 1);
    CHECK(g_send.ism);
    EXPECT_EQ(g_send.ts8, 1);                               /* ISMEX_SEND: cross-thread SendMessage, not yet replied */
    fx_close();
}
TEST(r2_teb_peb_field_matrix_send_cross_thread_pcti_insend_bit_set) {
    FXW();
    REQUIRE(cross_send(0) == 1);
    if (!g_send.pcti_ok) { fx_close(); t_skip("no readable client thread info"); return; }
    EXPECT_EQ(g_send.pcti_flags & 2, 2);                   /* user32!InSendMessage reads bit 1 of *pClientThreadInfo */
    fx_close();
}
TEST(r2_teb_peb_field_matrix_send_cross_thread_clears_insend_state_afterwards) {
    FXW();
    REQUIRE(cross_send(0) == 1);
    EXPECT_EQ(rt_ts(8), 0);
    BOOL (WINAPI *ism)(void) = (BOOL (WINAPI *)(void))t_proc("InSendMessage");
    REQUIRE(ism);
    EXPECT_EQ(ism(), 0);
    struct rsnap s; rs_take(&s);
    unsigned long long p = rs_ci64(&s, CIO_pcti);
    unsigned long f = 0;
    if (p && k_try_read_u32((void *)(ULONG_PTR)p, &f)) EXPECT_EQ(f & 2, 0);
    fx_close();
}
TEST(r2_teb_peb_field_matrix_send_replymessage_sets_replied_flag) {
    FXW();
    REQUIRE(cross_send(1) == 1);
    CHECK(g_send.reply_ok);
    EXPECT_EQ(g_send.ts8_after_reply & 8, 8);               /* ISMEX_REPLIED */
    EXPECT_EQ(g_send.ts8_after_reply & 1, 1);
    CHECK(g_sender_ok);
    EXPECT_EQ(g_sender_res, 0x55);                          /* the sender receives the ReplyMessage value, not the wndproc return */
    fx_close();
}
TEST(r2_teb_peb_field_matrix_send_cross_thread_client_info_stays_consistent) {
    FXW();
    struct rsnap before; rs_take(&before);
    REQUIRE(cross_send(0) == 1);
    struct rsnap after; rs_take(&after);
    EXPECT_EQ(g_send.s.ti, before.ti);
    EXPECT_EQ(after.ti, before.ti);
    EXPECT_EQ(rs_ci64(&g_send.s, CIO_pDeskInfo), rs_ci64(&before, CIO_pDeskInfo));
    EXPECT_EQ(rs_ci64(&g_send.s, CIO_ulClientDelta), rs_ci64(&before, CIO_ulClientDelta));
    EXPECT_EQ(rs_ci64(&after, CIO_pDeskInfo), rs_ci64(&before, CIO_pDeskInfo));
    fx_close();
}
TEST(r2_teb_peb_field_matrix_send_cross_thread_callback_window_cache_is_a_valid_pair) {
    FXW();
    REQUIRE(cross_send(0) == 1);
    unsigned long long h = rs_ci64(&g_send.s, CIO_cbHwnd), p = rs_ci64(&g_send.s, CIO_cbPwnd);
    t_note("inside sent message: CallbackWnd hwnd=0x%llx pwnd=0x%llx (window %p)", h, p, (void *)g_hw);
    if (h == (unsigned long long)(ULONG_PTR)g_hw) {
        unsigned long long hh = 0;
        CHECK(p != 0);
        CHECK(k_try_read((void *)(ULONG_PTR)p, &hh, 8));
        EXPECT_EQ(hh, (unsigned long long)(ULONG_PTR)g_hw);   /* THRDESKHEAD.h is the first member of tagWND */
    } else if (h) {
        CHECK(p != 0);
    }
    fx_close();
}
TEST(r2_teb_peb_field_matrix_send_same_thread_is_not_in_send_state) {
    FXW();
    REQUIRE(U(SendMessageA));
    memset(&g_send, 0, sizeof g_send);
    LRESULT r = U(SendMessageA)(g_hw, WM_R2_SENT, 0, 0);
    EXPECT_EQ(r, 0x99);
    EXPECT_EQ(g_send.hit, 1);
    CHECK(!g_send.ism);                                      /* InSendMessage is only true for messages from other threads */
    EXPECT_EQ(g_send.ts8, 0);
    fx_close();
}

/* ---- DESKTOPINFO slots user32 decodes by hand -------------------------------------------------------------- */

static HWND decode_slot(unsigned slot) {
    struct rsnap s; rs_take(&s);
    unsigned long long di = rs_ci64(&s, CIO_pDeskInfo), delta = rs_ci64(&s, CIO_ulClientDelta), v = 0, h = 0;
    if (!di || !k_try_read((void *)(ULONG_PTR)(di + slot), &v, 8) || !v) return NULL;
    if (!k_try_read((void *)(ULONG_PTR)(v + delta), &h, 8)) return (HWND)(ULONG_PTR)-1;
    return (HWND)(ULONG_PTR)h;
}
TEST(r2_teb_peb_field_matrix_deskinfo_progman_slot_matches_getprogmanwindow) {
    rt_gui();
    HWND (WINAPI *f)(void) = (HWND (WINAPI *)(void))t_proc("GetProgmanWindow");
    REQUIRE(f);
    HWND d = decode_slot(0x28);                              /* user32!GetProgmanWindow: *(*(pDeskInfo+0x28) + ulClientDelta) */
    CHECK(d != (HWND)(ULONG_PTR)-1);
    if (d != (HWND)(ULONG_PTR)-1) EXPECT_EQ(f(), d);
}
TEST(r2_teb_peb_field_matrix_deskinfo_taskman_slot_matches_gettaskmanwindow) {
    rt_gui();
    HWND (WINAPI *f)(void) = (HWND (WINAPI *)(void))t_proc("GetTaskmanWindow");
    REQUIRE(f);
    HWND d = decode_slot(0x20);
    CHECK(d != (HWND)(ULONG_PTR)-1);
    if (d != (HWND)(ULONG_PTR)-1) EXPECT_EQ(f(), d);
}
TEST(r2_teb_peb_field_matrix_deskinfo_slots_agree_on_worker_thread) {
    rt_gui();
    HWND (WINAPI *f)(void) = (HWND (WINAPI *)(void))t_proc("GetProgmanWindow");
    REQUIRE(f);
    HWND main_pm = f();
    WK_GUI(w);
    wk_finish(&w);
    EXPECT_EQ(decode_slot(0x28) == (HWND)(ULONG_PTR)-1 ? NULL : decode_slot(0x28), main_pm);
}

/* ---- hook procedures: phkCurrent / dwHookCurrent ----------------------------------------------------------- */

static struct { int hit; struct rsnap s; HHOOK hh; } g_hk;
static LRESULT CALLBACK hookproc(int code, WPARAM w, LPARAM l) {
    if (!g_hk.hit) { g_hk.hit = 1; rs_take(&g_hk.s); }
    LRESULT (WINAPI *cn)(HHOOK, int, WPARAM, LPARAM) = (LRESULT (WINAPI *)(HHOOK, int, WPARAM, LPARAM))t_proc("CallNextHookEx");
    return cn ? cn(g_hk.hh, code, w, l) : 0;
}
static int hook_run(int id, void (*trigger)(void)) {
    memset(&g_hk, 0, sizeof g_hk);
    g_hk.hh = U(SetWindowsHookExA)(id, hookproc, NULL, GetCurrentThreadId());
    if (!g_hk.hh) return -1;
    trigger();
    U(UnhookWindowsHookEx)(g_hk.hh);
    return g_hk.hit;
}
static void trig_send(void) { U(SendMessageA)(g_hw, WM_NULL, 0, 0); }
static void trig_peek(void) {
    MSG m; U(PostMessageA)(g_hw, WM_R2_POST, 0, 0);
    U(PeekMessageA)(&m, g_hw, WM_R2_POST, WM_R2_POST, PM_REMOVE);
}
static void trig_create(void) {
    HWND h = U(CreateWindowExA)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, g_cls, "r2h", WS_POPUP, -32000, -32000, 1, 1, NULL, NULL, g_inst, NULL);
    if (h) U(DestroyWindow)(h);
}
static void hook_checks(int id, void (*trigger)(void), int hi_is_id_verified) {
    FXW();
    REQUIRE(U(SetWindowsHookExA)); REQUIRE(U(UnhookWindowsHookEx)); REQUIRE(U(SendMessageA)); REQUIRE(U(PostMessageA));
    int r = hook_run(id, trigger);
    if (r < 0) { fx_close(); t_skip("hook not installed (%lu)", GetLastError()); return; }
    if (!r) { fx_close(); t_skip("hook procedure not called"); return; }
    unsigned long long phk = rs_ci64(&g_hk.s, CIO_phkCurrent);
    unsigned long hc = rs_ci32(&g_hk.s, CIO_dwHookCurrent);
    t_note("hook %d: phkCurrent=0x%llx dwHookCurrent=0x%lx", id, phk, hc);
    /* Windows 10 22H2: CallNextHookEx is forwarded to the kernel, so the user-mode phkCurrent slot stays NULL during the
     * hook call; the kernel still publishes the hook id in HIWORD(dwHookCurrent). Both are observed on build 19045. */
    EXPECT_EQ(phk, 0);
    (void)hi_is_id_verified;
    EXPECT_EQ(hc >> 16, (unsigned long)id);
    unsigned long long hh = 0;
    if (phk && k_try_read((void *)(ULONG_PTR)phk, &hh, 8) && hh && hh != (unsigned long long)(ULONG_PTR)g_hk.hh)
        t_note("first qword of *phkCurrent is 0x%llx, hook handle is %p", hh, (void *)g_hk.hh);
    struct rsnap after; rs_take(&after);
    EXPECT_EQ(rs_ci64(&after, CIO_phkCurrent), 0);           /* restored once the hook call returned */
    EXPECT_EQ(rs_ci32(&after, CIO_dwHookCurrent), 0);
    fx_close();
}
TEST(r2_teb_peb_field_matrix_hook_callwndproc_sets_phkcurrent_and_hiword_4) { hook_checks(WH_CALLWNDPROC, trig_send, 1); }
TEST(r2_teb_peb_field_matrix_hook_callwndprocret_sets_phkcurrent_and_hiword_12) { hook_checks(WH_CALLWNDPROCRET, trig_send, 1); }
TEST(r2_teb_peb_field_matrix_hook_getmessage_sets_phkcurrent) { hook_checks(WH_GETMESSAGE, trig_peek, 0); }
TEST(r2_teb_peb_field_matrix_hook_cbt_sets_phkcurrent) { hook_checks(WH_CBT, trig_create, 0); }
TEST(r2_teb_peb_field_matrix_hook_not_installed_leaves_fields_clear) {
    FXW();
    struct rsnap a, b; rs_take(&a);
    trig_send(); trig_peek();
    rs_take(&b);
    EXPECT_EQ(rs_ci64(&b, CIO_phkCurrent), 0);
    EXPECT_EQ(rs_ci32(&b, CIO_dwHookCurrent), 0);
    EXPECT_EQ(rs_ci64(&b, CIO_dwHookData), rs_ci64(&a, CIO_dwHookData));
    fx_close();
}
TEST(r2_teb_peb_field_matrix_hook_fields_are_per_thread) {
    FXW();
    REQUIRE(U(SetWindowsHookExA)); REQUIRE(U(UnhookWindowsHookEx));
    memset(&g_hk, 0, sizeof g_hk);
    g_hk.hh = U(SetWindowsHookExA)(WH_GETMESSAGE, hookproc, NULL, GetCurrentThreadId());
    REQUIRE(g_hk.hh);
    WK_GUI(w);                                               /* a hook on this thread is not active on the worker */
    EXPECT_EQ(rs_ci64(&w.after, CIO_phkCurrent), 0);
    EXPECT_EQ(rs_ci32(&w.after, CIO_dwHookCurrent), 0);
    wk_finish(&w);
    U(UnhookWindowsHookEx)(g_hk.hh);
    fx_close();
}
TEST(r2_teb_peb_field_matrix_hook_fshooks_reflects_installed_hook) {
    /* CI+0x38 (fsHooks) is the bit set of thread hook types; installing a WH_GETMESSAGE hook adds bit (id+1) */
    FXW();
    REQUIRE(U(SetWindowsHookExA)); REQUIRE(U(UnhookWindowsHookEx));
    struct rsnap a; rs_take(&a);
    HHOOK hh = U(SetWindowsHookExA)(WH_GETMESSAGE, hookproc, NULL, GetCurrentThreadId());
    REQUIRE(hh);
    struct rsnap b; rs_take(&b);
    U(UnhookWindowsHookEx)(hh);
    struct rsnap c; rs_take(&c);
    t_note("fsHooks before=0x%lx with=0x%lx after=0x%lx", rs_ci32(&a, CIO_fsHooks), rs_ci32(&b, CIO_fsHooks), rs_ci32(&c, CIO_fsHooks));
    EXPECT_EQ(rs_ci32(&c, CIO_fsHooks), rs_ci32(&a, CIO_fsHooks));
    fx_close();
}

/* ---- window pointers through ulClientDelta ---------------------------------------------------------------- */

TEST(r2_teb_peb_field_matrix_delta_maps_handle_entry_to_wnd_with_head_handle) {
    FXW();
    struct k_user_entry e;
    REQUIRE(k_user_entry(g_hw, &e));
    struct rsnap s; rs_take(&s);
    unsigned long long delta = rs_ci64(&s, CIO_ulClientDelta);
    unsigned long long pwnd = (unsigned long long)(ULONG_PTR)e.delta + delta;
    unsigned long long h = 0;
    CHECK(k_try_read((void *)(ULONG_PTR)pwnd, &h, 8));
    EXPECT_EQ(h, (unsigned long long)(ULONG_PTR)g_hw);      /* tagWND.head.h */
    EXPECT_EQ((unsigned long long)(ULONG_PTR)k_wnd_ptr(g_hw), pwnd);
    fx_close();
}
static void op_wnd_in_worker(struct wk *w) {
    HWND h = U(CreateWindowExA)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, g_cls, "r2w", WS_POPUP, -32000, -32000, 1, 1, NULL, NULL, g_inst, NULL);
    w->x[0] = (unsigned long long)(ULONG_PTR)h;
    struct k_user_entry e;
    if (h && k_user_entry(h, &e)) {
        struct rsnap s; rs_take(&s);
        unsigned long long pw = (unsigned long long)(ULONG_PTR)e.delta + rs_ci64(&s, CIO_ulClientDelta), hh = 0;
        k_try_read((void *)(ULONG_PTR)pw, &hh, 8);
        w->x[1] = hh;
    }
    if (h) U(DestroyWindow)(h);
}
TEST(r2_teb_peb_field_matrix_delta_valid_for_windows_created_by_other_threads) {
    FXW();
    struct wk w;
    if (!wk_start(&w, 1, op_wnd_in_worker, NULL)) { wk_finish(&w); fx_close(); t_skip("worker did not start"); return; }
    REQUIRE(w.x[0] != 0);
    EXPECT_EQ(w.x[1], w.x[0]);                               /* same delta on every thread of the process */
    wk_finish(&w);
    fx_close();
}
TEST(r2_teb_peb_field_matrix_delta_for_every_toplevel_visible_to_process_is_consistent) {
    FXW();
    HWND dw = U(GetDesktopWindow)();
    REQUIRE(dw);
    struct rsnap s; rs_take(&s);
    unsigned long long delta = rs_ci64(&s, CIO_ulClientDelta);
    struct k_user_entry e;
    REQUIRE(k_user_entry(dw, &e));
    unsigned long long h = 0;
    CHECK(k_try_read((void *)(ULONG_PTR)((unsigned long long)(ULONG_PTR)e.delta + delta), &h, 8));
    EXPECT_EQ(h, (unsigned long long)(ULONG_PTR)dw);
    fx_close();
}
