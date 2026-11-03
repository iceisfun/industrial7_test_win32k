/* kcb_kernel_callbacks: __fnINLPCREATESTRUCT (apfnDispatch slot 10 on the reference build) - the
 * WM_NCCREATE / WM_CREATE dispatch thunk used by window creation.
 *
 * user32!__fnINLPCREATESTRUCT (analysis takes a _CAPTUREBUF* (a kernel-marshaled
 * argument block, not a flat struct): hwnd at +0x28, message (DWORD) at +0x30, wParam at +0x38,
 * and a 4th dispatch argument built from pointer fixups rooted at +0x48/+0x80/+0x88 (the client-
 * side LPCREATESTRUCT, cross-session-heap-relocated by FixupCallbackPointers when needed) - this
 * area only hard-checks the three fixed-offset scalar fields; see the file header comment on
 * kcb_table.c for why message/argument *offsets* are trustworthy (read straight from the analysis)
 * while the pointer-fixup chain is not re-derived here.
 *
 * Unlike a generic SendMessage, window creation is entirely kernel-managed: NtUserCreateWindowEx
 * calls back into user mode for WM_NCCREATE and WM_CREATE through this same table slot regardless
 * of which thread calls CreateWindowEx - there is no "same-thread fast path" for creation, because
 * the window object itself does not exist in any user-mode table until the kernel creates it and
 * asks the (always-current) thread to initialize it via the callback. This is the one case in this
 * area where the callback is expected to fire even from the test's own thread.
 */
#include "t.h"
#include "k.h"
#include "kcb_api.h"

#define KCB_CREATESTRUCT_SLOT 10

static volatile UINT g_cs_seq[8];       /* creation messages the window procedure saw, in order */
static volatile HWND g_cs_hwnd[8];
static volatile LONG g_cs_n;
static LRESULT CALLBACK kcb_cs_wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if ((m == WM_NCCREATE || m == WM_CREATE) && g_cs_n < 8) { g_cs_seq[g_cs_n] = m; g_cs_hwnd[g_cs_n] = h; g_cs_n++; }
    return DefWindowProcA(h, m, w, l);
}

TEST(kcb_dispatch_createstruct_fires_on_window_creation) {
    REQUIRE(U(IsWindow));
    U(IsWindow)((HWND)0);
    if (k_cb_count() <= KCB_CREATESTRUCT_SLOT) { t_skip("table has no slot %u", KCB_CREATESTRUCT_SLOT); return; }

    char cls[64]; snprintf(cls, sizeof cls, "win32ktKcbCS_%lu", GetCurrentProcessId());
    WNDCLASSA wc; memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = kcb_cs_wndproc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = cls;
    if (!RegisterClassA(&wc)) { t_skip("RegisterClassA failed, err=%lu", GetLastError()); return; }

    kcb_warm_thread();
    g_cs_n = 0;
    CHECK(k_cb_hook(KCB_CREATESTRUCT_SLOT));
    HWND h = CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls, "kcb_cs",
                              WS_POPUP, -32000, -32000, 1, 1, NULL, NULL, GetModuleHandleA(NULL), NULL);
    unsigned long hits = k_cb_hit_count(KCB_CREATESTRUCT_SLOT);
    if (!h) {
        t_skip("CreateWindowExA failed, err=%lu (no display driver?)", GetLastError());
        k_cb_unhook_all();
        UnregisterClassA(cls, GetModuleHandleA(NULL));
        return;
    }
    if (hits == 0 && t_is_wine()) {
        t_note("slot %u never fired under Wine (own win32u dispatch path)", KCB_CREATESTRUCT_SLOT);
    } else {
        /* WM_NCCREATE then WM_CREATE: exactly two callback deliveries through this slot per
         * CreateWindowEx (both are "in lpCreateParams" messages per the analysis's shared thunk). */
        CHECKF(hits == 2, "hit_count=%lu, want exactly 2 (WM_NCCREATE + WM_CREATE)", hits);
        ULONG_PTR argv[4];
        if (k_cb_last_args(KCB_CREATESTRUCT_SLOT, argv)) {
            void *arg0 = (void *)argv[0];
            HWND got_hwnd = NULL; DWORD got_msg = 0;
            BOOL ok = k_try_read((char *)arg0 + 0x28, &got_hwnd, sizeof got_hwnd) &&
                      k_try_read((char *)arg0 + 0x30, &got_msg, sizeof got_msg);
            /* The window procedure is the authority on what the two deliveries carried: NCCREATE then CREATE, both for h. */
            CHECKF(g_cs_n == 2 && g_cs_seq[0] == WM_NCCREATE && g_cs_seq[1] == WM_CREATE, "window procedure saw %ld creation messages", (long)g_cs_n);
            CHECK(g_cs_n == 2 && g_cs_hwnd[0] == h && g_cs_hwnd[1] == h);
            if (ok && got_hwnd == NULL && got_msg == 0) {
                /* The capture buffer is released when the callback returns; by now the block can read back as zeroes. */
                t_note("capture buffer already released when read back");
            } else if (ok) {
                /* last_args holds the MOST RECENT of the two calls, i.e. WM_CREATE. */
                EXPECT_EQ(kcb_pwnd_hwnd((ULONG_PTR)got_hwnd), h);
                CHECKF(got_msg == WM_CREATE, "last message got %lu want WM_CREATE(%u)", (unsigned long)got_msg, WM_CREATE);
            } else {
                t_note("_CAPTUREBUF at %p not fully readable", arg0);
            }
        }
    }
    k_cb_unhook_all();
    DestroyWindow(h);
    UnregisterClassA(cls, GetModuleHandleA(NULL));
}

TEST(kcb_dispatch_createstruct_destroy_does_not_use_this_slot) {
    /* WM_NCDESTROY/WM_DESTROY route through a different fnid (__fnNCDESTROY) - this slot's
     * counter must not move during DestroyWindow. */
    REQUIRE(U(IsWindow));
    U(IsWindow)((HWND)0);
    if (k_cb_count() <= KCB_CREATESTRUCT_SLOT) { t_skip("table has no slot %u", KCB_CREATESTRUCT_SLOT); return; }
    char cls[64]; snprintf(cls, sizeof cls, "win32ktKcbCS2_%lu", GetCurrentProcessId());
    WNDCLASSA wc; memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = kcb_cs_wndproc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = cls;
    if (!RegisterClassA(&wc)) { t_skip("RegisterClassA failed"); return; }
    HWND h = CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls, "kcb_cs2",
                              WS_POPUP, -32000, -32000, 1, 1, NULL, NULL, GetModuleHandleA(NULL), NULL);
    if (!h) { t_skip("CreateWindowExA failed (no display driver?)"); UnregisterClassA(cls, GetModuleHandleA(NULL)); return; }
    CHECK(k_cb_hook(KCB_CREATESTRUCT_SLOT));
    DestroyWindow(h);
    CHECKF(k_cb_hit_count(KCB_CREATESTRUCT_SLOT) == 0, "destroy unexpectedly hit slot %u (hits=%lu)",
           KCB_CREATESTRUCT_SLOT, k_cb_hit_count(KCB_CREATESTRUCT_SLOT));
    k_cb_unhook_all();
    UnregisterClassA(cls, GetModuleHandleA(NULL));
}

TEST(kcb_dispatch_createstruct_multiple_windows_accumulate) {
    REQUIRE(U(IsWindow));
    U(IsWindow)((HWND)0);
    if (k_cb_count() <= KCB_CREATESTRUCT_SLOT) { t_skip("table has no slot %u", KCB_CREATESTRUCT_SLOT); return; }
    char cls[64]; snprintf(cls, sizeof cls, "win32ktKcbCS3_%lu", GetCurrentProcessId());
    WNDCLASSA wc; memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = kcb_cs_wndproc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = cls;
    if (!RegisterClassA(&wc)) { t_skip("RegisterClassA failed"); return; }
    kcb_warm_thread();
    CHECK(k_cb_hook(KCB_CREATESTRUCT_SLOT));
    HWND hs[3] = {0};
    int created = 0;
    for (int i = 0; i < 3; i++) {
        hs[i] = CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls, "kcb_cs3",
                                 WS_POPUP, -32000, -32000, 1, 1, NULL, NULL, GetModuleHandleA(NULL), NULL);
        if (hs[i]) created++;
    }
    if (!created) { t_skip("no windows created (no display driver?)"); }
    else if (!t_is_wine()) {
        CHECKF(k_cb_hit_count(KCB_CREATESTRUCT_SLOT) == (unsigned long)(2 * created),
               "hits=%lu want %d (2 per created window)", k_cb_hit_count(KCB_CREATESTRUCT_SLOT), 2 * created);
    }
    for (int i = 0; i < 3; i++) if (hs[i]) DestroyWindow(hs[i]);
    k_cb_unhook_all();
    UnregisterClassA(cls, GetModuleHandleA(NULL));
}
