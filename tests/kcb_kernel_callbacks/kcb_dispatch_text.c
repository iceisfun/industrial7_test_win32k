/* kcb_kernel_callbacks: __fnINSTRING (slot 26) and __fnOUTSTRING (slot 35) - the cross-thread
 * WM_SETTEXT / WM_GETTEXT dispatch thunks.
 *
 * user32!__fnINSTRING (analysis takes a _CAPTUREBUF*: hwnd@+0x28, message(DWORD)@+0x30,
 * wParam@+0x38, and dispatches with a 4th call-argument read from +0x50 (the inbound string
 * pointer the kernel copied into the capture buffer - this is the LPARAM a cross-thread
 * WM_SETTEXT carries) and a 5th from +0x40. __fnOUTSTRING is the WM_GETTEXT
 * counterpart: it only forwards hwnd/message/wParam (+0x28/+0x30/+0x38) to the real handler and
 * has the kernel pre-allocate the OUT buffer (stack buffer for <=0x800 chars, or the
 * caller's own buffer via +0x50 for longer ones) - it is the handler's *return value* that is
 * marshaled back via NtCallbackReturn, not a struct field this test can read directly, so the
 * OUTSTRING test below only checks that the slot fires and the end-to-end GetWindowText result
 * is correct, not an internal buffer offset.
 */
#include "t.h"
#include "k.h"
#include "kcb_api.h"
#include "kcb_local.h"
#include "kcb_echo.h"

#define KCB_INSTRING_SLOT  26
#define KCB_OUTSTRING_SLOT 35

static const unsigned kcb_instring_slots[] = { KCB_INSTRING_SLOT, 27 };
static const unsigned kcb_outstring_slots[] = { 7, 8, KCB_OUTSTRING_SLOT, 37 };

TEST(kcb_dispatch_instring_cross_thread_settext_fires_slot) {
    REQUIRE(U(IsWindow));
    U(IsWindow)((HWND)0);
    if (k_cb_count() <= 27) { t_skip("table has no slot 27"); return; }
    struct kcb_worker wk;
    if (!kcb_worker_start(&wk, "instr")) { t_skip("could not create worker window (no display driver?)"); return; }

    kcb_hook_set(kcb_instring_slots, 2);
    const char *text = "win32kt_instring_probe";
    LRESULT r = SendMessageA(wk.hwnd, WM_SETTEXT, 0, (LPARAM)text);
    CHECK(r != 0); /* DefWindowProc's WM_SETTEXT returns nonzero on success */

    unsigned long hits = kcb_sum_hits(kcb_instring_slots, 2);
    if (hits == 0 && t_is_wine()) {
        t_note("INSTRING slots never fired under Wine");
    } else {
        CHECKF(hits >= 1, "INSTRING family hit_count=%lu, want >=1 after cross-thread WM_SETTEXT", hits);
        for (int i = 0; i < 2; i++) {
            unsigned slot = kcb_instring_slots[i];
            ULONG_PTR argv[4];
            if (k_cb_hit_count(slot) == 0 || !k_cb_last_args(slot, argv)) continue;
            void *cbuf = (void *)argv[0];
            HWND got_hwnd = NULL; DWORD got_msg = 0; ULONG_PTR strv = 0;
            BOOL ok = k_try_read((char *)cbuf + 0x28, &got_hwnd, sizeof got_hwnd) &&
                      k_try_read((char *)cbuf + 0x30, &got_msg, sizeof got_msg) &&
                      k_try_read((char *)cbuf + 0x50, &strv, sizeof strv);
            if (!ok) { t_note("_CAPTUREBUF at %p not fully readable", cbuf); continue; }
            EXPECT_EQ(kcb_pwnd_hwnd((ULONG_PTR)got_hwnd), wk.hwnd);
            CHECKF(got_msg == WM_SETTEXT, "slot %u message got %lu want WM_SETTEXT(%u)", slot, (unsigned long)got_msg, WM_SETTEXT);
            /* strv is a client pointer after FixupCallbackPointers, or an offset from the capture
             * buffer before it; either way the marshaled ANSI text must sit there byte for byte. */
            char got[64]; BOOL found = FALSE;
            memset(got, 0, sizeof got);
            if (k_try_read((void *)strv, got, strlen(text) + 1) && !strcmp(got, text)) found = TRUE;
            memset(got, 0, sizeof got);
            if (!found && k_try_read((char *)cbuf + strv, got, strlen(text) + 1) && !strcmp(got, text)) found = TRUE;
            if (!found) t_note("slot %u: string at +0x50 (%p) not recognizable (layout variant)", slot, (void *)strv);
        }
    }
    k_cb_unhook_all();
    /* End-to-end: the title must actually have been set. */
    char buf[64] = {0};
    GetWindowTextA(wk.hwnd, buf, sizeof buf);
    EXPECT_STR(buf, text);
    kcb_worker_stop(&wk);
}

TEST(kcb_dispatch_outstring_cross_thread_gettext_fires_slot) {
    REQUIRE(U(IsWindow));
    U(IsWindow)((HWND)0);
    if (k_cb_count() <= 37) { t_skip("table has no slot 37"); return; }
    struct kcb_worker wk;
    if (!kcb_worker_start(&wk, "outstr")) { t_skip("could not create worker window (no display driver?)"); return; }

    const char *text = "win32kt_outstring_probe";
    SetWindowTextA(wk.hwnd, text);

    kcb_hook_set(kcb_outstring_slots, 4);
    char buf[64] = {0};
    LRESULT n = SendMessageA(wk.hwnd, WM_GETTEXT, (WPARAM)sizeof buf, (LPARAM)buf);
    unsigned long hits = kcb_sum_hits(kcb_outstring_slots, 4);
    if (hits == 0 && t_is_wine()) {
        t_note("OUTSTRING family never fired under Wine");
    } else {
        CHECKF(hits >= 1, "OUTSTRING family hit_count=%lu, want >=1 after cross-thread WM_GETTEXT", hits);
        for (int i = 0; i < 4; i++) {
            unsigned slot = kcb_outstring_slots[i];
            ULONG_PTR argv[4];
            if (k_cb_hit_count(slot) == 0 || !k_cb_last_args(slot, argv)) continue;
            HWND got_hwnd = NULL; UINT got_msg = 0; WPARAM got_wp = 0;
            if (!kcb_decode(slot, argv[0], &got_hwnd, &got_msg, &got_wp)) { t_note("slot %u layout not derived/readable", slot); continue; }
            EXPECT_EQ(got_hwnd, wk.hwnd);
            CHECKF(got_msg == WM_GETTEXT, "slot %u message got %lu want WM_GETTEXT(%u)", slot, (unsigned long)got_msg, WM_GETTEXT);
        }
    }
    CHECKF((size_t)n == strlen(text), "WM_GETTEXT returned %ld want %zu", (long)n, strlen(text));
    EXPECT_STR(buf, text);
    k_cb_unhook_all();
    kcb_worker_stop(&wk);
}

TEST(kcb_dispatch_instring_same_thread_does_not_use_callback) {
    REQUIRE(U(IsWindow));
    U(IsWindow)((HWND)0);
    if (k_cb_count() <= KCB_INSTRING_SLOT) { t_skip("table has no slot %u", KCB_INSTRING_SLOT); return; }
    HWND h = k_make_window("kcb_instring_same_thread");
    if (!h) { t_skip("k_make_window failed (no display driver?)"); return; }
    CHECK(k_cb_hook(KCB_INSTRING_SLOT));
    SendMessageA(h, WM_SETTEXT, 0, (LPARAM)"same thread");
    CHECKF(k_cb_hit_count(KCB_INSTRING_SLOT) == 0, "same-thread WM_SETTEXT unexpectedly used the kernel callback (hits=%lu)",
           k_cb_hit_count(KCB_INSTRING_SLOT));
    k_cb_unhook_all();
    DestroyWindow(h);
}
