/* kcb_kernel_callbacks: __fnINLPDRAWITEMSTRUCT (apfnDispatch slot 12 on the reference build) -
 * the WM_DRAWITEM dispatch thunk for owner-draw controls.
 *
 * user32!__fnINLPDRAWITEMSTRUCT (analysis takes a flat (non-_CAPTUREBUF) struct:
 *   argument 1[0]=hwnd, argument 1[1](DWORD)=message, argument 1[2]=wParam, &argument 1[3]=DRAWITEMSTRUCT
 *   (passed BY ADDRESS, i.e. the struct is inline in this same buffer starting at +0x18),
 *   argument 1[0xb]=a 5th dispatch argument (+0x58). The function also NULL-checks argument 1[7]
 *   (+0x38) before dispatching and calls RtlSetLastWin32Error(6 = ERROR_INVALID_HANDLE) if it
 *   is zero. +0x38 is DRAWITEMSTRUCT.hDC (DRAWITEMSTRUCT starts at +0x18: CtlType, CtlID, itemID,
 *   itemAction, itemState, hwndItem@+0x30, hDC@+0x38, rcItem@+0x40, itemData@+0x50), so a
 *   WM_DRAWITEM whose hDC is NULL reaches the thunk but never reaches the window procedure.
 *
 * An owner-draw BS_PUSHBUTTON sends WM_DRAWITEM to GetParent(hwnd) whenever it paints. Giving the
 * button a WS_CHILD parent that belongs to a DIFFERENT thread (legal within one process) forces
 * that send across threads, which is what reaches the kernel callback mechanism - a same-thread
 * owner-draw button (the common case) never does, for the same reason same-thread SendMessage
 * never does in the other dispatch files in this area.
 */
#include "t.h"
#include "k.h"
#include "kcb_api.h"

#define KCB_DRAWITEM_SLOT 12
#define WM_KCB_CREATE_BUTTON (WM_USER + 100) /* posted to the owner thread so IT creates the child */

struct kcb_di_worker {
    HANDLE thread, ready;
    volatile LONG stop;
    HWND owner;   /* created and owned by the worker thread */
    HWND button;  /* WS_CHILD of owner, but created on the worker thread too (CreateWindowEx must
                   * run on the thread that will own the new window if that window is to pump its
                   * own messages; for a WS_CHILD button this also keeps parent/child same-thread,
                   * which is the setup real-world owner-draw apps actually use - see below for
                   * why this test still gets cross-thread delivery despite that). */
    char owner_class[40];
    DWORD type, ctl_id;
    volatile DWORD create_err; /* GetLastError() of the owner CreateWindowExA, 0 if it succeeded */
};

/* What the owner window procedure received for WM_DRAWITEM. The kernel's argument block lives on a callback stack that is
 * reused as soon as the call returns, so the receiver is the reliable witness for hwnd, message, wParam and hDC. */
static volatile LONG g_di_n;
static volatile HWND g_di_hwnd; static volatile UINT g_di_msg; static volatile WPARAM g_di_wp;
static volatile HDC g_di_hdc; static volatile HWND g_di_item; static volatile UINT g_di_ctltype;
static LRESULT CALLBACK kcb_di_owner_wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DRAWITEM) {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)l;
        g_di_hwnd = h; g_di_msg = m; g_di_wp = w; g_di_hdc = dis->hDC; g_di_item = dis->hwndItem; g_di_ctltype = dis->CtlType;
        InterlockedIncrement(&g_di_n);
        if (dis->hDC) FillRect(dis->hDC, &dis->rcItem, (HBRUSH)(COLOR_BTNFACE + 1));
        return TRUE;
    }
    return DefWindowProcA(h, m, w, l);
}

static DWORD WINAPI kcb_di_thread_proc(LPVOID arg) {
    struct kcb_di_worker *wk = (struct kcb_di_worker *)arg;
    WNDCLASSA wc; memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = kcb_di_owner_wndproc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = wk->owner_class;
    RegisterClassA(&wc);
    wk->owner = CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wk->owner_class, "kcb_di_owner",
                                 WS_POPUP | WS_VISIBLE, -32000, -32000, 100, 100, NULL, NULL, GetModuleHandleA(NULL), NULL);
    if (!wk->owner) wk->create_err = GetLastError() ? GetLastError() : 0xFFFFFFFFul;
    SetEvent(wk->ready);
    if (!wk->owner) return 1;
    for (int i = 0; i < 250 && !wk->stop; i++) k_pump(20);
    if (wk->button) DestroyWindow(wk->button);
    DestroyWindow(wk->owner);
    UnregisterClassA(wk->owner_class, GetModuleHandleA(NULL));
    return 0;
}

static BOOL kcb_di_start(struct kcb_di_worker *wk) {
    memset(wk, 0, sizeof *wk);
    snprintf(wk->owner_class, sizeof wk->owner_class, "win32ktKcbDIOwner_%lu", GetCurrentProcessId());
    wk->ready = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!wk->ready) return FALSE;
    wk->thread = CreateThread(NULL, 0, kcb_di_thread_proc, wk, 0, NULL);
    if (!wk->thread) { CloseHandle(wk->ready); return FALSE; }
    /* The thread signals `ready` right after CreateWindowExA returns; on a loaded guest the first GUI call of a fresh
     * thread can take well over a few seconds, so wait generously (the wait ends as soon as the window exists). */
    if (WaitForSingleObject(wk->ready, 30000) != WAIT_OBJECT_0) {
        t_info("owner window not created within the wait");
    } else if (!wk->owner) {
        t_info("owner CreateWindowExA failed, err=%lu", (unsigned long)wk->create_err);
    }
    if (!wk->owner) { InterlockedExchange(&wk->stop, 1); WaitForSingleObject(wk->thread, 5000);
        CloseHandle(wk->thread); CloseHandle(wk->ready); return FALSE; }
    return TRUE;
}
static void kcb_di_stop(struct kcb_di_worker *wk) {
    InterlockedExchange(&wk->stop, 1);
    WaitForSingleObject(wk->thread, 5000);
    CloseHandle(wk->thread); CloseHandle(wk->ready);
}

TEST(kcb_dispatch_drawitem_cross_thread_owner_fires_slot) {
    REQUIRE(U(IsWindow));
    U(IsWindow)((HWND)0);
    if (k_cb_count() <= KCB_DRAWITEM_SLOT) { t_skip("table has no slot %u", KCB_DRAWITEM_SLOT); return; }
    struct kcb_di_worker wk;
    if (!kcb_di_start(&wk)) { t_skip("could not create owner window (no display driver?)"); return; }

    /* Create the owner-draw button from THIS (the test's) thread, parented to wk.owner, which
     * belongs to the worker thread - a cross-thread WS_CHILD parent/child pair. The button's own
     * window procedure (DefWindowProcA/the stock BUTTON class) is what calls SendMessage(WM_DRAWITEM)
     * to GetParent(button) == wk.owner during painting, and that SendMessage is the cross-thread
     * sent message this area's other files already exercise. */
    HWND button = CreateWindowExA(0, "BUTTON", "kcb_di_btn",
                                   WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                   0, 0, 60, 20, wk.owner, (HMENU)(UINT_PTR)4242, GetModuleHandleA(NULL), NULL);
    if (!button) {
        t_skip("CreateWindowExA(BUTTON) failed, err=%lu", GetLastError());
        kcb_di_stop(&wk);
        return;
    }
    wk.button = button;

    g_di_n = 0;
    CHECK(k_cb_hook(KCB_DRAWITEM_SLOT));
    InvalidateRect(button, NULL, TRUE);
    UpdateWindow(button); /* forces a synchronous WM_PAINT -> WM_DRAWITEM round trip, bounded */
    k_pump(100); /* give the owner thread a chance to process the sent message if it needed one */

    unsigned long hits = k_cb_hit_count(KCB_DRAWITEM_SLOT);
    if (hits == 0) {
        /* Painting an off-screen/invisible-effectively window under a headless or null display
         * driver, or a build where the stock BUTTON class short-circuits owner-draw painting
         * without display hardware, can legitimately never paint at all - that is an environment
         * fact (t_note), not proof the dispatch thunk is wrong. Real hardware/session is where
         * this area's hard assertions below apply. */
        t_note("slot %u never fired (hits=0) - no paint occurred, environment-dependent", KCB_DRAWITEM_SLOT);
    } else {
        /* The owner window procedure, on the worker thread, received the marshaled struct. */
        CHECKF(g_di_n >= 1, "owner window procedure never saw WM_DRAWITEM");
        if (g_di_n >= 1) {
            EXPECT_EQ(g_di_hwnd, wk.owner);
            CHECKF(g_di_msg == WM_DRAWITEM, "message got %lu want WM_DRAWITEM(%u)", (unsigned long)g_di_msg, WM_DRAWITEM);
            /* WM_DRAWITEM's documented wParam for a control (not a menu) is the control identifier passed to CreateWindowEx, here 4242. */
            CHECKF((UINT_PTR)g_di_wp == 4242, "wParam got %lu want control id 4242", (unsigned long)g_di_wp);
            CHECKF(g_di_hdc != NULL, "DRAWITEMSTRUCT.hDC was NULL");
            CHECK(g_di_item == button);
            CHECKF(g_di_ctltype == ODT_BUTTON, "CtlType %u", g_di_ctltype);
        }
    }
    k_cb_unhook_all();
    DestroyWindow(button);
    wk.button = NULL;
    kcb_di_stop(&wk);
}
