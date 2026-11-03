/* kcb_kernel_callbacks: __fnDWORD (apfnDispatch slot 2 on the reference build) - the generic
 * cross-thread "sent message" dispatch thunk.
 *
 * user32!__fnDWORD forwards the block to the window procedure dispatch and then returns the
 * result with NtCallbackReturn, i.e. the kernel hands user mode a flat 5-qword argument block:
 *   [0] HWND target, [1](low DWORD) message, [2] WPARAM, [3] LPARAM, [4] extra (window-proc-ish
 *   value the dispatch call also receives but this corpus has not identified further).
 * This fires when SendMessage() targets a window owned by a different thread of the SAME process
 * for a message with no pointer payload (no structure to marshal) - see kcb_local.h for why the
 * receiving thread must be pumping messages for the kernel to deliver it.
 */
#include "t.h"
#include "k.h"
#include "kcb_api.h"
#include "kcb_local.h"

#define KCB_DWORD_SLOT 2

TEST(kcb_dispatch_dword_cross_thread_sendmessage_fires_slot) {
    REQUIRE(U(IsWindow));
    U(IsWindow)((HWND)0);
    void *tbl = k_cb_table();
    REQUIRE(tbl);
    if (k_cb_count() <= KCB_DWORD_SLOT) { t_skip("table has no slot %u", KCB_DWORD_SLOT); return; }

    struct kcb_worker wk;
    if (!kcb_worker_start(&wk, "dword")) { t_skip("could not create worker window (no display driver?)"); return; }

    CHECK(k_cb_hook(KCB_DWORD_SLOT));
    EXPECT_EQ(k_cb_hit_count(KCB_DWORD_SLOT), 0);

    const UINT msg = WM_USER + 0x123;
    const WPARAM wp = (WPARAM)0xAABBCCDD;
    const LPARAM lp = (LPARAM)0x11223344;
    LRESULT r = SendMessageA(wk.hwnd, msg, wp, lp);
    (void)r; /* DefWindowProc's return for an unhandled WM_USER message is not asserted here */

    unsigned long hits = k_cb_hit_count(KCB_DWORD_SLOT);
    if (hits == 0 && t_is_wine()) {
        t_note("slot %u never fired under Wine (different cross-thread SendMessage architecture)", KCB_DWORD_SLOT);
    } else {
        CHECKF(hits >= 1, "slot %u hit_count=%lu, want >=1 after cross-thread SendMessage", KCB_DWORD_SLOT, hits);
        ULONG_PTR argv[4];
        if (hits >= 1 && k_cb_last_args(KCB_DWORD_SLOT, argv)) {
            /* argv[0] is rcx at the trampoline, i.e. the Arguments pointer the dispatcher passed -
 * which is exactly __fnDWORD's argument 1 above. */
            void *arg0 = (void *)argv[0];
            ULONG_PTR qw[5];
            BOOL readable = TRUE;
            for (int i = 0; i < 5; i++) readable &= k_try_read(((char *)arg0) + i * 8, &qw[i], 8);
            if (!readable) { t_note("argument block at %p not fully readable", arg0); }
            else {
                EXPECT_EQ(kcb_pwnd_hwnd(qw[0]), wk.hwnd);   /* the block holds the PWND */
                CHECKF((DWORD)qw[1] == msg, "message got %lu want %u", (unsigned long)qw[1], msg);
                EXPECT_EQ(qw[2], wp);
                EXPECT_EQ(qw[3], lp);
            }
        }
    }

    k_cb_unhook_all();
    kcb_worker_stop(&wk);
}

TEST(kcb_dispatch_dword_same_thread_sendmessage_does_not_use_callback) {
    /* The same window, same message, sent from the SAME thread that owns it: user32's SendMessage
     * fast path calls the window procedure directly without any kernel round trip (no sent message
     * is queued at all when sender==target thread), so the hooked slot must not fire. This is the
     * architectural reason cross-thread delivery exists above, verified by contrast. */
    REQUIRE(U(IsWindow));
    U(IsWindow)((HWND)0);
    if (k_cb_count() <= KCB_DWORD_SLOT) { t_skip("table has no slot %u", KCB_DWORD_SLOT); return; }
    HWND h = k_make_window("kcb_dword_same_thread");
    if (!h) { t_skip("k_make_window failed (no display driver?)"); return; }
    CHECK(k_cb_hook(KCB_DWORD_SLOT));
    SendMessageA(h, WM_USER + 1, 1, 2);
    CHECKF(k_cb_hit_count(KCB_DWORD_SLOT) == 0, "same-thread SendMessage unexpectedly went through the kernel callback (hits=%lu)",
           k_cb_hit_count(KCB_DWORD_SLOT));
    k_cb_unhook_all();
    DestroyWindow(h);
}

TEST(kcb_dispatch_dword_multiple_sends_increment_hit_count) {
    REQUIRE(U(IsWindow));
    U(IsWindow)((HWND)0);
    if (k_cb_count() <= KCB_DWORD_SLOT) { t_skip("table has no slot %u", KCB_DWORD_SLOT); return; }
    struct kcb_worker wk;
    if (!kcb_worker_start(&wk, "dwordN")) { t_skip("could not create worker window"); return; }
    CHECK(k_cb_hook(KCB_DWORD_SLOT));
    const int N = 5;
    for (int i = 0; i < N; i++) SendMessageA(wk.hwnd, WM_USER + 2, (WPARAM)i, 0);
    unsigned long hits = k_cb_hit_count(KCB_DWORD_SLOT);
    if (hits == 0 && t_is_wine()) t_note("slot never fired under Wine");
    else CHECKF(hits >= (unsigned long)N, "hits=%lu want >=%d", hits, N);
    /* last_args must reflect the LAST call's wParam (i == N-1), not an earlier one, since the
     * trampoline overwrites last_args on every hit (common/k.c). */
    ULONG_PTR argv[4];
    if (hits >= (unsigned long)N && k_cb_last_args(KCB_DWORD_SLOT, argv)) {
        ULONG_PTR wp = 0;
        if (k_try_read((char *)argv[0] + 16, &wp, sizeof wp))
            CHECKF(wp == (ULONG_PTR)(N - 1), "last wParam=%lu want %d", (unsigned long)wp, N - 1);
    }
    k_cb_unhook_all();
    kcb_worker_stop(&wk);
}

TEST(kcb_dispatch_dword_unhook_mid_sequence_stops_counting) {
    REQUIRE(U(IsWindow));
    U(IsWindow)((HWND)0);
    if (k_cb_count() <= KCB_DWORD_SLOT) { t_skip("table has no slot %u", KCB_DWORD_SLOT); return; }
    struct kcb_worker wk;
    if (!kcb_worker_start(&wk, "dwordU")) { t_skip("could not create worker window"); return; }
    CHECK(k_cb_hook(KCB_DWORD_SLOT));
    SendMessageA(wk.hwnd, WM_USER + 3, 0, 0);
    unsigned long before = k_cb_hit_count(KCB_DWORD_SLOT);
    if (!t_is_wine()) CHECKF(before >= 1, "hooked slot %u never fired before the unhook (hits=%lu)", KCB_DWORD_SLOT, before);
    k_cb_unhook_all();
    { void *tbl = k_cb_table(), *cur = NULL;
      k_try_read_ptr((char *)tbl + KCB_DWORD_SLOT * sizeof(void *), &cur);
      EXPECT_EQ(cur, k_cb_original(KCB_DWORD_SLOT)); }
    SendMessageA(wk.hwnd, WM_USER + 3, 0, 0); /* now goes to the real, restored handler */
    unsigned long after = k_cb_hit_count(KCB_DWORD_SLOT); /* unhooked: counter is frozen, not reset */
    EXPECT_EQ(before, after);
    kcb_worker_stop(&wk);
}
