/* kcb_kernel_callbacks: __fnCOPYDATA (apfnDispatch slot 0 on the reference build) - the
 * WM_COPYDATA dispatch thunk.
 *
 * user32!__fnCOPYDATA (analysis takes a _CAPTUREBUF*: hwnd@+0x28, message(DWORD)@+0x30,
 * wParam@+0x38 (the SENDER's HWND, per WM_COPYDATA's documented wParam), and a conditionally
 * relocated 4th dispatch argument built from +0x40/+0x48 (the COPYDATASTRUCT pointer, fixed up by
 * FixupCallbackPointers when the capture buffer needed cross-session relocation) - as with
 * __fnINLPCREATESTRUCT, only the three fixed-offset scalar fields are hard-checked directly; the
 * COPYDATASTRUCT payload itself is instead verified end-to-end through the receiving window
 * procedure, which is simpler and no less analysis-grounded (WM_COPYDATA's own documented contract:
 * lParam points at a COPYDATASTRUCT the receiver must treat as read-only and valid only for the
 * call's duration).
 *
 * This area's own worker (not kcb_local.h's generic one, which always DefWindowProc's) needs a
 * window procedure that actually handles WM_COPYDATA to prove the payload crossed intact.
 */
#include "t.h"
#include "k.h"
#include "kcb_api.h"

#define KCB_COPYDATA_SLOT 0

struct kcb_cd_worker {
    HANDLE thread, ready;
    volatile LONG stop;
    HWND hwnd;
    char class_name[40];
    /* last COPYDATASTRUCT contents the wndproc observed, written only by the worker thread and
     * read only after SendMessage (which blocks the sender until the receiver's wndproc returns),
     * so there is no real race despite crossing threads. */
    DWORD last_dwData;
    DWORD last_cbData;
    char last_data[128];
    HWND last_sender;
};

static struct kcb_cd_worker *g_cd_active; /* one at a time per test; simplest safe plumbing */

static LRESULT CALLBACK kcb_cd_wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_COPYDATA && g_cd_active) {
        COPYDATASTRUCT *cds = (COPYDATASTRUCT *)l;
        g_cd_active->last_sender = (HWND)w;
        g_cd_active->last_dwData = (DWORD)cds->dwData;
        g_cd_active->last_cbData = (DWORD)cds->cbData;
        size_t n = cds->cbData < sizeof g_cd_active->last_data - 1 ? cds->cbData : sizeof g_cd_active->last_data - 1;
        memcpy(g_cd_active->last_data, cds->lpData, n);
        g_cd_active->last_data[n] = 0;
        return TRUE;
    }
    return DefWindowProcA(h, m, w, l);
}

static DWORD WINAPI kcb_cd_thread_proc(LPVOID arg) {
    struct kcb_cd_worker *wk = (struct kcb_cd_worker *)arg;
    WNDCLASSA wc; memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = kcb_cd_wndproc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = wk->class_name;
    RegisterClassA(&wc);
    wk->hwnd = CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wk->class_name, "kcb_cd",
                                WS_POPUP, 0, 0, 1, 1, HWND_MESSAGE, NULL, GetModuleHandleA(NULL), NULL);
    SetEvent(wk->ready);
    if (!wk->hwnd) return 1;
    for (int i = 0; i < 250 && !wk->stop; i++) k_pump(20);
    DestroyWindow(wk->hwnd);
    UnregisterClassA(wk->class_name, GetModuleHandleA(NULL));
    return 0;
}

static BOOL kcb_cd_start(struct kcb_cd_worker *wk) {
    memset(wk, 0, sizeof *wk);
    snprintf(wk->class_name, sizeof wk->class_name, "win32ktKcbCD_%lu", GetCurrentProcessId());
    wk->ready = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!wk->ready) return FALSE;
    wk->thread = CreateThread(NULL, 0, kcb_cd_thread_proc, wk, 0, NULL);
    if (!wk->thread) { CloseHandle(wk->ready); return FALSE; }
    WaitForSingleObject(wk->ready, 5000);
    if (!wk->hwnd) {
        InterlockedExchange(&wk->stop, 1);
        WaitForSingleObject(wk->thread, 5000);
        CloseHandle(wk->thread); CloseHandle(wk->ready);
        return FALSE;
    }
    return TRUE;
}
static void kcb_cd_stop(struct kcb_cd_worker *wk) {
    InterlockedExchange(&wk->stop, 1);
    WaitForSingleObject(wk->thread, 5000);
    CloseHandle(wk->thread); CloseHandle(wk->ready);
}

TEST(kcb_dispatch_copydata_cross_thread_fires_slot_and_preserves_payload) {
    REQUIRE(U(IsWindow));
    U(IsWindow)((HWND)0);
    if (k_cb_count() <= KCB_COPYDATA_SLOT) { t_skip("table has no slot %u", KCB_COPYDATA_SLOT); return; }
    HWND sender = k_make_window("kcb_cd_sender");
    if (!sender) { t_skip("k_make_window failed (no display driver?)"); return; }
    struct kcb_cd_worker wk;
    if (!kcb_cd_start(&wk)) { t_skip("could not create worker window"); DestroyWindow(sender); return; }
    g_cd_active = &wk;

    CHECK(k_cb_hook(KCB_COPYDATA_SLOT));
    const char payload[] = "win32kt_copydata_probe";
    COPYDATASTRUCT cds; cds.dwData = 0xC0DE1234; cds.cbData = (DWORD)sizeof payload; cds.lpData = (void *)payload;
    LRESULT r = SendMessageA(wk.hwnd, WM_COPYDATA, (WPARAM)sender, (LPARAM)&cds);
    CHECK(r == TRUE);

    unsigned long hits = k_cb_hit_count(KCB_COPYDATA_SLOT);
    if (hits == 0 && t_is_wine()) {
        t_note("slot %u never fired under Wine", KCB_COPYDATA_SLOT);
    } else {
        CHECKF(hits >= 1, "hit_count=%lu, want >=1 after cross-thread WM_COPYDATA", hits);
        ULONG_PTR argv[4];
        if (hits >= 1 && k_cb_last_args(KCB_COPYDATA_SLOT, argv)) {
            void *cbuf = (void *)argv[0];
            HWND got_hwnd = NULL; DWORD got_msg = 0;
            if (k_try_read((char *)cbuf + 0x28, &got_hwnd, sizeof got_hwnd) &&
                k_try_read((char *)cbuf + 0x30, &got_msg, sizeof got_msg)) {
                EXPECT_EQ(kcb_pwnd_hwnd((ULONG_PTR)got_hwnd), wk.hwnd);
                CHECKF(got_msg == WM_COPYDATA, "message got %lu want WM_COPYDATA(%u)", (unsigned long)got_msg, WM_COPYDATA);
            } else t_note("_CAPTUREBUF at %p not fully readable", cbuf);
        }
    }
    /* End-to-end: the receiver's wndproc, running on the OTHER thread, must have seen the exact
     * sender HWND and payload bytes the kernel marshaled through the capture buffer. */
    EXPECT_EQ(wk.last_sender, sender);
    EXPECT_EQ(wk.last_dwData, cds.dwData);
    EXPECT_EQ(wk.last_cbData, cds.cbData);
    EXPECT_STR(wk.last_data, payload);

    k_cb_unhook_all();
    g_cd_active = NULL;
    kcb_cd_stop(&wk);
    DestroyWindow(sender);
}

TEST(kcb_dispatch_copydata_same_thread_does_not_use_callback) {
    REQUIRE(U(IsWindow));
    U(IsWindow)((HWND)0);
    if (k_cb_count() <= KCB_COPYDATA_SLOT) { t_skip("table has no slot %u", KCB_COPYDATA_SLOT); return; }
    struct kcb_cd_worker wk; memset(&wk, 0, sizeof wk);
    snprintf(wk.class_name, sizeof wk.class_name, "win32ktKcbCDs_%lu", GetCurrentProcessId());
    WNDCLASSA wc; memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = kcb_cd_wndproc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = wk.class_name;
    if (!RegisterClassA(&wc)) { t_skip("RegisterClassA failed"); return; }
    HWND h = CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wk.class_name, "kcb_cds",
                              WS_POPUP, -32000, -32000, 1, 1, NULL, NULL, GetModuleHandleA(NULL), NULL);
    if (!h) { t_skip("CreateWindowExA failed"); UnregisterClassA(wk.class_name, GetModuleHandleA(NULL)); return; }
    g_cd_active = &wk;
    CHECK(k_cb_hook(KCB_COPYDATA_SLOT));
    char payload[] = "same thread";
    COPYDATASTRUCT cds; cds.dwData = 1; cds.cbData = sizeof payload; cds.lpData = payload;
    SendMessageA(h, WM_COPYDATA, (WPARAM)h, (LPARAM)&cds);
    CHECKF(k_cb_hit_count(KCB_COPYDATA_SLOT) == 0, "same-thread WM_COPYDATA unexpectedly used the kernel callback (hits=%lu)",
           k_cb_hit_count(KCB_COPYDATA_SLOT));
    EXPECT_STR(wk.last_data, payload); /* the wndproc still ran - just without a kernel round trip */
    k_cb_unhook_all();
    g_cd_active = NULL;
    DestroyWindow(h);
    UnregisterClassA(wk.class_name, GetModuleHandleA(NULL));
}
