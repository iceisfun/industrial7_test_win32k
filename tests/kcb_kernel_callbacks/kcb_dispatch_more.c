/* kcb_kernel_callbacks: cross-thread sent messages as seen from BOTH sides of the kernel callback
 * channel - the value the receiving window procedure observes (thread, message, 64-bit wParam /
 * lParam, deep copies of pointer payloads) and the argument block the hooked apfnDispatch slot
 * received. See kcb_echo.h for the receiving window.
 *
 * Analysis facts used (user32 thunks, each ends with NtCallbackReturn(&result, 0x18) where result is the
 * window procedure's 64-bit return value):
 *   __fnDWORD (slot 2)       flat block: hwnd@0 msg@8 wParam@0x10 lParam@0x18 extra@0x20, no marshaling
 *   __fnGETTEXTLENGTHS (6/96) flat block: hwnd@0 msg@8 wParam@0x10, lParam forced to 0
 *   __fnINLPDRAWITEMSTRUCT (12) flat block, DRAWITEMSTRUCT inline at +0x18, skipped when hDC (+0x38)==0
 *   __fnINOUTLPRECT (20)     flat block, RECT inline at +0x18 and copied back (0x10 bytes)
 *   __fnCOPYDATA (0), __fnINSTRING (26/27), __fnPOWERBROADCAST (29): _CAPTUREBUF, hwnd@0x28 msg@0x30
 * Every scenario below is a message the kernel can only deliver across threads through the table;
 * the same-thread cases are covered in the other kcb_dispatch_* files.
 */
#include "t.h"
#include "k.h"
#include "kcb_api.h"
#include "kcb_echo.h"

#define NEED_WORKER(e, slot, tag, wide) \
    do { REQUIRE(U(IsWindow)); U(IsWindow)((HWND)0); \
         if (k_cb_count() == 0) { t_skip("callback table too small"); return; } \
         if (!kcb_echo_start(&(e), slot, tag, wide, FALSE)) { t_skip("could not create echo window (no display driver?)"); return; } } while (0)

/* ---- __fnDWORD family: message values >= WM_USER ---------------------------------------- */

static const UINT kcb_user_msgs[] = { WM_USER, WM_USER + 1, WM_USER + 0x3FF, WM_APP, WM_APP + 0x1234, 0xBFFF, 0xC000, 0xFFFF };

TEST(kcb_dword_user_message_table) {
    struct kcb_echo e;
    NEED_WORKER(e, 0, "dwt", TRUE);
    for (unsigned i = 0; i < sizeof kcb_user_msgs / sizeof kcb_user_msgs[0]; i++) {
        UINT msg = kcb_user_msgs[i];
        k_cb_hook(2);
        WPARAM wp = (WPARAM)0xFEDCBA9876543210ULL;
        LPARAM lp = (LPARAM)0x0123456789ABCDEFLL;
        LRESULT r = SendMessageW(e.hwnd, msg, wp, lp);
        /* The 64-bit result of the remote window procedure comes back through NtCallbackReturn intact. */
        CHECKF(r == KCB_SENTINEL_RET, "msg 0x%x: result 0x%llx want 0x%llx", msg, (unsigned long long)r, (unsigned long long)KCB_SENTINEL_RET);
        CHECKF(e.last_msg == msg, "proc saw msg 0x%x want 0x%x", e.last_msg, msg);
        CHECKF(e.last_wp == wp, "proc saw wParam 0x%llx", (unsigned long long)e.last_wp);
        CHECKF(e.last_lp == lp, "proc saw lParam 0x%llx", (unsigned long long)e.last_lp);
        CHECKF(e.last_tid == e.tid, "proc ran on thread %lu, window owner is %lu", (unsigned long)e.last_tid, (unsigned long)e.tid);
        if (!t_is_wine()) {
            CHECKF(k_cb_hit_count(2) >= 1, "msg 0x%x never went through slot 2", msg);
            ULONG_PTR argv[4];
            HWND h; UINT m; WPARAM w;
            if (k_cb_hit_count(2) >= 1 && k_cb_last_args(2, argv) && kcb_decode(2, argv[0], &h, &m, &w) && m == msg) {
                EXPECT_EQ(h, e.hwnd);
                CHECKF(w == wp, "block wParam 0x%llx", (unsigned long long)w);
            }
        }
        k_cb_unhook_all();
    }
    kcb_echo_stop(&e, 0);
}

TEST(kcb_dword_runs_on_receiver_thread_not_sender) {
    struct kcb_echo e;
    NEED_WORKER(e, 0, "thr", TRUE);
    CHECK(e.tid != GetCurrentThreadId());
    SendMessageW(e.hwnd, WM_USER + 5, 0, 0);
    EXPECT_EQ(e.last_tid, e.tid);
    CHECK(e.last_tid != GetCurrentThreadId());
    kcb_echo_stop(&e, 0);
}

TEST(kcb_dword_ansi_window_gets_same_slot) {
    /* A message >= WM_USER needs no ANSI/Unicode conversion, so an ANSI window (RegisterClassA) goes
     * through the same __fnDWORD thunk and sees identical values. */
    struct kcb_echo e;
    NEED_WORKER(e, 0, "ansi", FALSE);
    k_cb_hook(2);
    LRESULT r = SendMessageA(e.hwnd, WM_USER + 9, 0x55, 0x66);
    CHECK(r == KCB_SENTINEL_RET);
    EXPECT_EQ(e.last_wp, 0x55);
    EXPECT_EQ(e.last_lp, 0x66);
    if (!t_is_wine()) CHECK(k_cb_hit_count(2) >= 1);
    k_cb_unhook_all();
    kcb_echo_stop(&e, 0);
}

TEST(kcb_dword_sendmessagetimeout_cross_thread) {
    struct kcb_echo e;
    NEED_WORKER(e, 0, "smt", TRUE);
    k_cb_hook(2);
    DWORD_PTR res = 0;
    LRESULT ok = SendMessageTimeoutW(e.hwnd, WM_USER + 11, 7, 8, SMTO_NORMAL, 4000, &res);
    CHECK(ok != 0);
    CHECKF(res == (DWORD_PTR)KCB_SENTINEL_RET, "lpdwResult 0x%llx", (unsigned long long)res);
    EXPECT_EQ(e.last_wp, 7);
    EXPECT_EQ(e.last_lp, 8);
    if (!t_is_wine()) CHECK(k_cb_hit_count(2) >= 1);
    k_cb_unhook_all();
    kcb_echo_stop(&e, 0);
}

TEST(kcb_dword_sendnotifymessage_is_delivered_by_callback) {
    struct kcb_echo e;
    NEED_WORKER(e, 0, "snm", TRUE);
    k_cb_hook(2);
    BOOL ok = SendNotifyMessageW(e.hwnd, WM_USER + 13, 0x1234, 0x5678);
    CHECK(ok);
    /* Asynchronous for a cross-thread target: bounded wait for the receiver to run it. */
    for (int i = 0; i < 100 && e.calls == 0; i++) Sleep(20);
    CHECKF(e.calls == 1, "receiver ran %ld deliveries", (long)e.calls);
    EXPECT_EQ(e.last_msg, WM_USER + 13);
    EXPECT_EQ(e.last_wp, 0x1234);
    if (!t_is_wine()) CHECK(k_cb_hit_count(2) >= 1);
    k_cb_unhook_all();
    kcb_echo_stop(&e, 0);
}

TEST(kcb_dword_reverse_send_runs_on_sender_thread) {
    /* While the test thread waits inside SendMessage, the receiver's proc sends a message back to a
     * window owned by the test thread; the kernel delivers it to the waiting thread through the same
     * callback channel, so the proc runs on THIS thread nested inside its own SendMessage. */
    struct kcb_echo wk, mn;
    REQUIRE(U(IsWindow)); U(IsWindow)((HWND)0);
    if (k_cb_count() == 0) { t_skip("callback table too small"); return; }
    if (!kcb_echo_start(&mn, 1, "revm", TRUE, TRUE)) { t_skip("could not create main-thread window"); return; }
    if (!kcb_echo_start(&wk, 0, "revw", FALSE, FALSE)) { kcb_echo_stop(&mn, 1); t_skip("could not create worker window"); return; }
    wk.forward_to = mn.hwnd;
    k_cb_hook(2);
    LRESULT r = SendMessageA(wk.hwnd, WM_USER + 7, 3, 4);
    CHECK(r == KCB_SENTINEL_RET);
    EXPECT_EQ(mn.calls, 1);
    EXPECT_EQ(mn.last_msg, WM_USER + 8);
    EXPECT_EQ(mn.last_wp, 1);
    EXPECT_EQ(mn.last_lp, 2);
    CHECKF(mn.last_tid == GetCurrentThreadId(), "reverse message ran on thread %lu, expected the sender %lu", (unsigned long)mn.last_tid, (unsigned long)GetCurrentThreadId());
    CHECK(wk.last_tid == wk.tid);
    if (!t_is_wine()) CHECKF(k_cb_hit_count(2) >= 2, "slot 2 hits=%lu, want the forward and the reverse delivery", k_cb_hit_count(2));
    k_cb_unhook_all();
    kcb_echo_stop(&wk, 0);
    kcb_echo_stop(&mn, 1);
}

TEST(kcb_dword_hook_removed_stops_counting_but_delivery_continues) {
    struct kcb_echo e;
    NEED_WORKER(e, 0, "rm", TRUE);
    k_cb_hook(2);
    SendMessageW(e.hwnd, WM_USER + 1, 0, 0);
    unsigned long h1 = k_cb_hit_count(2);
    k_cb_unhook_all();
    LRESULT r = SendMessageW(e.hwnd, WM_USER + 1, 0, 0);
    CHECK(r == KCB_SENTINEL_RET);
    EXPECT_EQ(e.calls, 2);
    EXPECT_EQ(k_cb_hit_count(2), h1);
    kcb_echo_stop(&e, 0);
}

/* ---- WM_GETTEXTLENGTH: flat block, lParam zeroed by the thunk -------------------------- */

TEST(kcb_gettextlength_cross_thread) {
    static const unsigned slots[] = { 6, 96 };
    struct kcb_echo e;
    NEED_WORKER(e, 0, "gtl", TRUE);
    kcb_hook_set(slots, 2);
    LRESULT n = SendMessageW(e.hwnd, WM_GETTEXTLENGTH, 0x4141, (LPARAM)0xDEADBEEF);
    EXPECT_EQ(n, 1234);
    CHECK(e.last_msg == WM_GETTEXTLENGTH);
    /* __fnGETTEXTLENGTHS passes lParam = 0 to the procedure no matter what the sender supplied. */
    if (e.any && !t_is_wine()) EXPECT_EQ(e.last_lp, 0);
    if (!t_is_wine()) {
        CHECKF(kcb_sum_hits(slots, 2) >= 1, "GETTEXTLENGTHS family never fired");
        for (int i = 0; i < 2; i++) {
            ULONG_PTR argv[4]; HWND h; UINT m; WPARAM w;
            if (k_cb_hit_count(slots[i]) && k_cb_last_args(slots[i], argv) && kcb_decode(slots[i], argv[0], &h, &m, &w)) {
                EXPECT_EQ(h, e.hwnd); EXPECT_EQ(m, WM_GETTEXTLENGTH); EXPECT_EQ(w, 0x4141);
            }
        }
    }
    k_cb_unhook_all();
    kcb_echo_stop(&e, 0);
}

/* ---- text: ANSI/Unicode conversion of the marshaled string ------------------------------ */

TEST(kcb_settext_ansi_text_to_unicode_and_ansi_windows) {
    static const unsigned slots[] = { 26, 27 };
    for (int wide = 0; wide < 2; wide++) {
        struct kcb_echo e;
        NEED_WORKER(e, 0, wide ? "stw" : "sta", wide);
        kcb_hook_set(slots, 2);
        LRESULT r = SendMessageA(e.hwnd, WM_SETTEXT, 0, (LPARAM)"kcb ansi \xE9 text");
        CHECK(r == TRUE);
        if (wide) {
            /* Sent as ANSI to a Unicode window: converted using the sender's ANSI code page. */
            WCHAR want[64]; int n = MultiByteToWideChar(CP_ACP, 0, "kcb ansi \xE9 text", -1, want, 64);
            CHECK(n > 0);
            CHECK(lstrcmpW(e.wtext, want) == 0);
        } else {
            EXPECT_STR(e.text, "kcb ansi \xE9 text");
        }
        if (!t_is_wine()) CHECKF(kcb_sum_hits(slots, 2) >= 1, "INSTRING family silent for wide=%d", wide);
        k_cb_unhook_all();
        kcb_echo_stop(&e, 0);
    }
}

TEST(kcb_settext_unicode_text_to_ansi_window) {
    static const unsigned slots[] = { 26, 27 };
    struct kcb_echo e;
    NEED_WORKER(e, 0, "stu", FALSE);
    kcb_hook_set(slots, 2);
    LRESULT r = SendMessageW(e.hwnd, WM_SETTEXT, 0, (LPARAM)L"kcb wide text");
    CHECK(r == TRUE);
    EXPECT_STR(e.text, "kcb wide text");
    if (!t_is_wine()) CHECK(kcb_sum_hits(slots, 2) >= 1);
    k_cb_unhook_all();
    kcb_echo_stop(&e, 0);
}

TEST(kcb_gettext_result_roundtrip_ansi_and_wide) {
    static const unsigned slots[] = { 7, 8, 35, 37 };
    for (int wide = 0; wide < 2; wide++) {
        struct kcb_echo e;
        NEED_WORKER(e, 0, wide ? "gtw" : "gta", wide);
        kcb_hook_set(slots, 4);
        if (wide) {
            WCHAR b[32]; memset(b, 0xCC, sizeof b);
            LRESULT n = SendMessageW(e.hwnd, WM_GETTEXT, 32, (LPARAM)b);
            EXPECT_EQ(n, 13);
            CHECK(!lstrcmpW(b, L"kcb-echo-text"));
        } else {
            char b[32]; memset(b, 0xCC, sizeof b);
            LRESULT n = SendMessageA(e.hwnd, WM_GETTEXT, 32, (LPARAM)b);
            EXPECT_EQ(n, 13);
            EXPECT_STR(b, "kcb-echo-text");
        }
        if (!t_is_wine()) CHECKF(kcb_sum_hits(slots, 4) >= 1, "GETTEXT family silent for wide=%d", wide);
        k_cb_unhook_all();
        kcb_echo_stop(&e, 0);
    }
}

TEST(kcb_gettext_truncation_to_wparam_count) {
    /* The callee gets the count in wParam (INCNTOUTSTRING copies it into the capture block) and
     * must never write more than that many characters including the terminator. */
    struct kcb_echo e;
    NEED_WORKER(e, 0, "gtt", FALSE);
    char b[32]; memset(b, 0x7F, sizeof b);
    LRESULT n = SendMessageA(e.hwnd, WM_GETTEXT, 6, (LPARAM)b);
    EXPECT_EQ(n, 5);
    CHECK(!memcmp(b, "kcb-e", 6));
    CHECK((unsigned char)b[6] == 0x7F);
    kcb_echo_stop(&e, 0);
}

/* ---- in/out struct messages: payload crosses, modifications come back -------------------- */

TEST(kcb_sizing_rect_is_copied_in_and_out) {
    struct kcb_echo e;
    NEED_WORKER(e, 0, "siz", TRUE);
    e.armed = WM_SIZING;
    RECT r = { 100, 200, 300, 400 };
    LRESULT res = SendMessageW(e.hwnd, WM_SIZING, WMSZ_LEFT, (LPARAM)&r);
    EXPECT_EQ(res, TRUE);
    EXPECT_EQ(e.rect_in.left, 100); EXPECT_EQ(e.rect_in.top, 200);
    EXPECT_EQ(e.rect_in.right, 300); EXPECT_EQ(e.rect_in.bottom, 400);
    /* The receiver edited its (marshaled) copy; the sender's RECT shows the edit after the send. */
    EXPECT_EQ(r.left, 105); EXPECT_EQ(r.top, 211); EXPECT_EQ(r.right, 293); EXPECT_EQ(r.bottom, 387);
    EXPECT_EQ(e.last_wp, WMSZ_LEFT);
    kcb_echo_stop(&e, 0);
}

TEST(kcb_sizing_hits_inoutlprect_slot) {
    /* __fnINOUTLPRECT (slot 20) is the flat thunk that hands the proc a pointer to a RECT inline at +0x18. */
    struct kcb_echo e;
    NEED_WORKER(e, 0, "siz2", TRUE);
    e.armed = WM_SIZING;
    unsigned n = kcb_hook_all();
    CHECK(n > 0);
    RECT r = { 1, 2, 3, 4 };
    SendMessageW(e.hwnd, WM_SIZING, WMSZ_TOP, (LPARAM)&r);
    unsigned fired = 0;
    for (unsigned i = 0; i < n; i++) if (k_cb_hit_count(i)) fired++;
    if (!t_is_wine()) CHECKF(fired >= 1, "no slot at all fired for a cross-thread WM_SIZING");
    ULONG_PTR argv[4]; HWND h; UINT m; WPARAM w;
    for (unsigned i = 0; i < n && !t_is_wine(); i++) {
        if (!k_cb_hit_count(i) || !k_cb_last_args(i, argv)) continue;
        if (kcb_decode(i, argv[0], &h, &m, &w) && m == WM_SIZING) {
            EXPECT_EQ(h, e.hwnd); EXPECT_EQ(w, WMSZ_TOP);
            t_note("WM_SIZING went through slot %u", i);
            EXPECT_EQ(i, 20);
        }
    }
    k_cb_unhook_all();
    kcb_echo_stop(&e, 0);
}

TEST(kcb_getminmaxinfo_in_out) {
    struct kcb_echo e;
    NEED_WORKER(e, 0, "mmi", TRUE);
    e.armed = WM_GETMINMAXINFO;
    MINMAXINFO mm; memset(&mm, 0, sizeof mm);
    mm.ptMaxSize.x = 7; mm.ptMaxSize.y = 8; mm.ptMinTrackSize.x = 9; mm.ptMinTrackSize.y = 10;
    mm.ptMaxPosition.x = 11; mm.ptMaxPosition.y = 12; mm.ptMaxTrackSize.x = 13; mm.ptMaxTrackSize.y = 14;
    SendMessageW(e.hwnd, WM_GETMINMAXINFO, 0, (LPARAM)&mm);
    EXPECT_EQ(e.mmi_in.ptMaxSize.x, 7);
    EXPECT_EQ(e.mmi_in.ptMinTrackSize.y, 10);
    EXPECT_EQ(e.mmi_in.ptMaxTrackSize.x, 13);
    EXPECT_EQ(mm.ptMaxSize.x, 1111); EXPECT_EQ(mm.ptMaxSize.y, 2222);
    EXPECT_EQ(mm.ptMinTrackSize.x, 33); EXPECT_EQ(mm.ptMinTrackSize.y, 44);
    EXPECT_EQ(mm.ptMaxPosition.x, 11);   /* untouched fields survive the round trip */
    EXPECT_EQ(mm.ptMaxTrackSize.y, 14);
    kcb_echo_stop(&e, 0);
}

TEST(kcb_nccalcsize_rect_form_in_out) {
    struct kcb_echo e;
    NEED_WORKER(e, 0, "ncc", TRUE);
    e.armed = WM_NCCALCSIZE;
    RECT r = { 10, 20, 110, 220 };
    SendMessageW(e.hwnd, WM_NCCALCSIZE, FALSE, (LPARAM)&r);
    EXPECT_EQ(e.rect_in.left, 10); EXPECT_EQ(e.rect_in.bottom, 220);
    EXPECT_EQ(r.left, 12); EXPECT_EQ(r.top, 23); EXPECT_EQ(r.right, 106); EXPECT_EQ(r.bottom, 215);
    kcb_echo_stop(&e, 0);
}

TEST(kcb_help_notify_deleteitem_payloads) {
    struct kcb_echo e;
    NEED_WORKER(e, 0, "pay", TRUE);
    HELPINFO hi; memset(&hi, 0, sizeof hi);
    hi.cbSize = sizeof hi; hi.iContextType = HELPINFO_WINDOW; hi.iCtrlId = 0x77; hi.hItemHandle = (HANDLE)(ULONG_PTR)0x1234;
    hi.dwContextId = 0xABCD0001; hi.MousePos.x = 5; hi.MousePos.y = 6;
    e.armed = WM_HELP;
    LRESULT r = SendMessageW(e.hwnd, WM_HELP, 0, (LPARAM)&hi);
    EXPECT_EQ(r, TRUE);
    EXPECT_EQ(e.hi.cbSize, sizeof hi);
    EXPECT_EQ(e.hi.iCtrlId, 0x77);
    EXPECT_EQ(e.hi.dwContextId, 0xABCD0001);
    EXPECT_EQ(e.hi.MousePos.y, 6);
    EXPECT_EQ(e.hi.hItemHandle, (HANDLE)(ULONG_PTR)0x1234);

    NMHDR nm; memset(&nm, 0, sizeof nm);
    nm.hwndFrom = (HWND)(ULONG_PTR)0xAB00; nm.idFrom = 0x42; nm.code = (UINT)-12;
    e.armed = WM_NOTIFY;
    r = SendMessageW(e.hwnd, WM_NOTIFY, 0x42, (LPARAM)&nm);
    EXPECT_EQ(r, 0x77);
    EXPECT_EQ(e.nm.hwndFrom, (HWND)(ULONG_PTR)0xAB00);
    EXPECT_EQ(e.nm.idFrom, 0x42);
    EXPECT_EQ(e.nm.code, (UINT)-12);

    DELETEITEMSTRUCT di; memset(&di, 0, sizeof di);
    di.CtlType = ODT_LISTBOX; di.CtlID = 9; di.itemID = 3; di.hwndItem = (HWND)(ULONG_PTR)0xCD00; di.itemData = 0xFEEDFACECAFEBEEFULL;
    e.armed = WM_DELETEITEM;
    r = SendMessageW(e.hwnd, WM_DELETEITEM, 9, (LPARAM)&di);
    EXPECT_EQ(r, TRUE);
    EXPECT_EQ(e.del.CtlID, 9);
    EXPECT_EQ(e.del.itemID, 3);
    EXPECT_EQ(e.del.itemData, 0xFEEDFACECAFEBEEFULL);
    kcb_echo_stop(&e, 0);
}

/* ---- WM_COPYDATA payload shapes -------------------------------------------------------- */

TEST(kcb_copydata_zero_length_and_null_data) {
    struct kcb_echo e;
    NEED_WORKER(e, 0, "cd0", TRUE);
    e.armed = WM_COPYDATA;
    COPYDATASTRUCT c; c.dwData = 0xF00D; c.cbData = 0; c.lpData = NULL;
    LRESULT r = SendMessageW(e.hwnd, WM_COPYDATA, (WPARAM)0x1111, (LPARAM)&c);
    EXPECT_EQ(r, TRUE);
    EXPECT_EQ(e.cd_seen, 1);
    EXPECT_EQ(e.cds.dwData, 0xF00D);
    EXPECT_EQ(e.cds.cbData, 0);
    EXPECT_EQ(e.cd_wp, 0x1111);
    kcb_echo_stop(&e, 0);
}

TEST(kcb_copydata_dwdata_is_64_bit_and_binary_payload_exact) {
    struct kcb_echo e;
    NEED_WORKER(e, 0, "cd1", TRUE);
    e.armed = WM_COPYDATA;
    BYTE data[300];
    for (int i = 0; i < 300; i++) data[i] = (BYTE)(i * 7 + 3);
    COPYDATASTRUCT c; c.dwData = (ULONG_PTR)0x8877665544332211ULL; c.cbData = 300; c.lpData = data;
    k_cb_hook(0);
    LRESULT r = SendMessageW(e.hwnd, WM_COPYDATA, (WPARAM)e.hwnd, (LPARAM)&c);
    EXPECT_EQ(r, TRUE);
    EXPECT_EQ(e.cds.dwData, (ULONG_PTR)0x8877665544332211ULL);
    EXPECT_EQ(e.cds.cbData, 300);
    CHECK(!memcmp(e.cdata, data, 300));
    /* The receiver gets a private copy, never the sender's pointer. */
    CHECK(e.cds.lpData != (void *)data);
    if (!t_is_wine()) {
        CHECK(k_cb_hit_count(0) >= 1);
        ULONG_PTR argv[4]; HWND h; UINT m; WPARAM w;
        if (k_cb_hit_count(0) && k_cb_last_args(0, argv) && kcb_decode(0, argv[0], &h, &m, &w) && m == WM_COPYDATA) {
            EXPECT_EQ(h, e.hwnd); EXPECT_EQ(w, (WPARAM)e.hwnd);
        }
    }
    k_cb_unhook_all();
    kcb_echo_stop(&e, 0);
}

TEST(kcb_copydata_large_payload_exact) {
    struct kcb_echo e;
    NEED_WORKER(e, 0, "cd2", TRUE);
    e.armed = WM_COPYDATA;
    enum { N = 100000 };
    BYTE *data = (BYTE *)malloc(N);
    if (!data) { kcb_echo_stop(&e, 0); t_skip("malloc"); return; }
    for (int i = 0; i < N; i++) data[i] = (BYTE)(i ^ (i >> 8));
    COPYDATASTRUCT c; c.dwData = 5; c.cbData = N; c.lpData = data;
    LRESULT r = SendMessageW(e.hwnd, WM_COPYDATA, 0, (LPARAM)&c);
    EXPECT_EQ(r, TRUE);
    EXPECT_EQ(e.cds.cbData, N);
    /* only the first 512 bytes are recorded by the proc */
    CHECK(!memcmp(e.cdata, data, sizeof e.cdata));
    free(data);
    kcb_echo_stop(&e, 0);
}

/* ---- WM_POWERBROADCAST / WM_DRAWITEM thunks -------------------------------------------- */

TEST(kcb_powerbroadcast_cross_thread_slot_29) {
    struct kcb_echo e;
    NEED_WORKER(e, 0, "pbt", TRUE);
    e.armed = WM_POWERBROADCAST;
    k_cb_hook(29);
    BYTE scratch[64]; memset(scratch, 0, sizeof scratch);
    /* wParam without bit 15: __fnPOWERBROADCAST forwards lParam unchanged (the heap copy is only for
     * PBT_POWERSETTINGCHANGE-style 0x80xx values). 0xA = PBT_APMPOWERSTATUSCHANGE. */
    LRESULT r = SendMessageW(e.hwnd, WM_POWERBROADCAST, 0xA, (LPARAM)scratch);
    CHECK(r == TRUE);
    EXPECT_EQ(e.pbt_seen, 1);
    EXPECT_EQ(e.last_wp, 0xA);
    if (!t_is_wine()) {
        CHECKF(k_cb_hit_count(29) >= 1, "slot 29 (__fnPOWERBROADCAST) hits=%lu", k_cb_hit_count(29));
        ULONG_PTR argv[4]; HWND h; UINT m; WPARAM w;
        if (k_cb_hit_count(29) && k_cb_last_args(29, argv) && kcb_decode(29, argv[0], &h, &m, &w)) {
            EXPECT_EQ(h, e.hwnd); EXPECT_EQ(m, WM_POWERBROADCAST); EXPECT_EQ(w, 0xA);
        }
    }
    k_cb_unhook_all();
    kcb_echo_stop(&e, 0);
}

TEST(kcb_drawitem_direct_send_marshals_struct_and_requires_hdc) {
    struct kcb_echo e;
    NEED_WORKER(e, 0, "dis", TRUE);
    e.armed = WM_DRAWITEM;
    HDC dc = GetDC(e.hwnd);   /* any valid DC handle value: the proc never draws with it */
    if (!dc) { kcb_echo_stop(&e, 0); t_skip("GetDC failed (no display driver?)"); return; }
    DRAWITEMSTRUCT d; memset(&d, 0, sizeof d);
    d.CtlType = ODT_BUTTON; d.CtlID = 0x31; d.itemID = 5; d.itemAction = ODA_DRAWENTIRE; d.itemState = ODS_SELECTED;
    d.hwndItem = (HWND)(ULONG_PTR)0x4242; d.hDC = dc; d.itemData = 0xAABBCCDD11223344ULL;
    d.rcItem.left = 1; d.rcItem.top = 2; d.rcItem.right = 30; d.rcItem.bottom = 40;
    k_cb_hook(12);
    LRESULT r = SendMessageW(e.hwnd, WM_DRAWITEM, 0x31, (LPARAM)&d);
    EXPECT_EQ(r, TRUE);
    EXPECT_EQ(e.dis_seen, 1);
    EXPECT_EQ(e.dis.CtlID, 0x31); EXPECT_EQ(e.dis.itemID, 5); EXPECT_EQ(e.dis.itemState, ODS_SELECTED);
    EXPECT_EQ(e.dis.itemData, 0xAABBCCDD11223344ULL);
    EXPECT_EQ(e.dis.rcItem.right, 30); EXPECT_EQ(e.dis.rcItem.bottom, 40);
    EXPECT_EQ(e.dis.hDC, dc);
    unsigned long hits12 = k_cb_hit_count(12);
    if (!t_is_wine()) {
        CHECKF(hits12 >= 1, "slot 12 (__fnINLPDRAWITEMSTRUCT) hits=%lu", hits12);
        ULONG_PTR argv[4]; HWND h; UINT m; WPARAM w;
        if (hits12 && k_cb_last_args(12, argv) && kcb_decode(12, argv[0], &h, &m, &w)) {
            EXPECT_EQ(h, e.hwnd); EXPECT_EQ(m, WM_DRAWITEM); EXPECT_EQ(w, 0x31);
            ULONG_PTR hdc_in_block = 0;
            if (k_try_read((char *)argv[0] + 0x38, &hdc_in_block, 8)) EXPECT_EQ(hdc_in_block, (ULONG_PTR)dc);
        }
    }
 /* With hDC == NULL the thunk skips the dispatch call entirely and
     * returns 0, so the procedure never sees the message. Only meaningful when the slot fired. */
    d.hDC = NULL;
    e.dis_seen = 0;
    r = SendMessageW(e.hwnd, WM_DRAWITEM, 0x31, (LPARAM)&d);
    if (!t_is_wine() && hits12 >= 1) {
        EXPECT_EQ(e.dis_seen, 0);
        EXPECT_EQ(r, 0);
    } else t_note("NULL-hDC WM_DRAWITEM: proc saw it %d times, result %lld", e.dis_seen, (long long)r);
    k_cb_unhook_all();
    ReleaseDC(e.hwnd, dc);
    kcb_echo_stop(&e, 0);
}

/* ---- CreateWindowEx: the callback fires on the caller's own thread ---------------------- */

static LRESULT run_create(BOOL wide, DWORD style, HWND parent, void *param, struct kcb_echo *e, const char *tag) {
    memset(e, 0, sizeof *e);
    e->wide = wide; e->main_thread = TRUE;
    snprintf(e->cls, sizeof e->cls, "win32ktKcbN%s_%lu", tag, GetCurrentProcessId());
    for (int i = 0; i < 64; i++) e->clsw[i] = (WCHAR)(unsigned char)e->cls[i];
    HINSTANCE hi = GetModuleHandleA(NULL);
    g_kcb_creating = e;
    if (wide) {
        WNDCLASSW wc; memset(&wc, 0, sizeof wc);
        wc.lpfnWndProc = kcb_echo_procW; wc.hInstance = hi; wc.lpszClassName = e->clsw;
        if (!RegisterClassW(&wc)) { g_kcb_creating = NULL; return -1; }
        e->hwnd = CreateWindowExW(0, e->clsw, L"kcb-create", style, 3, 4, 50, 60, parent, NULL, hi, param);
    } else {
        WNDCLASSA wc; memset(&wc, 0, sizeof wc);
        wc.lpfnWndProc = kcb_echo_procA; wc.hInstance = hi; wc.lpszClassName = e->cls;
        if (!RegisterClassA(&wc)) { g_kcb_creating = NULL; return -1; }
        e->hwnd = CreateWindowExA(0, e->cls, "kcb-create", style, 3, 4, 50, 60, parent, NULL, hi, param);
    }
    g_kcb_creating = NULL;
    return e->hwnd ? 0 : -2;
}

TEST(kcb_create_ncreate_then_create_seen_by_proc) {
    REQUIRE(U(IsWindow)); U(IsWindow)((HWND)0);
    if (k_cb_count() == 0) { t_skip("callback table too small"); return; }
    for (int wide = 0; wide < 2; wide++) {
        struct kcb_echo e;
        int sentinel = 0;
        k_cb_hook(10);
        LRESULT rc = run_create(wide, WS_POPUP, HWND_MESSAGE, &sentinel, &e, wide ? "cw" : "ca");
        if (rc != 0) { k_cb_unhook_all(); t_skip("window creation failed (no display driver?)"); return; }
        if (t_is_wine() && k_cb_hit_count(10) == 0) t_note("slot 10 silent under Wine");
        else EXPECT_EQ(k_cb_hit_count(10), 2);
        /* The window procedure observed WM_NCCREATE first, then WM_CREATE (it can see no other
         * CREATESTRUCT-carrying message), both on this very thread. */
        CHECKF(e.create_n >= 2, "create_n=%d", e.create_n);
        if (e.create_n >= 2) {
            EXPECT_EQ(e.create_msg[0], WM_NCCREATE);
            EXPECT_EQ(e.create_msg[1], WM_CREATE);
            EXPECT_EQ(e.create_hwnd[0], e.hwnd);
            for (int i = 0; i < 2; i++) {
                CHECK(e.create_cs[i].lpCreateParams == &sentinel);
                CHECK((e.create_cs[i].style & WS_POPUP) != 0);
                EXPECT_EQ(e.create_cs[i].x, 3); EXPECT_EQ(e.create_cs[i].y, 4);
                EXPECT_EQ(e.create_cs[i].cx, 50); EXPECT_EQ(e.create_cs[i].cy, 60);
                if (e.create_cs[i].hwndParent != HWND_MESSAGE) t_note("CREATESTRUCT.hwndParent=%p for a message-only window", (void *)e.create_cs[i].hwndParent);
                EXPECT_EQ(e.create_cs[i].hInstance, GetModuleHandleA(NULL));
            }
        }
        CHECK(e.last_tid == GetCurrentThreadId());
        if (!t_is_wine()) {
            ULONG_PTR argv[4]; HWND h; UINT m; WPARAM w;
            if (k_cb_last_args(10, argv) && kcb_decode(10, argv[0], &h, &m, &w)) {
                /* the capture buffer is released once the callback returns; zeroes mean it was already recycled (the window
                 * procedure observations above are the authority on hwnd/message) */
                if (h == NULL && m == 0) t_note("capture buffer already released when read back");
                else { EXPECT_EQ(h, e.hwnd); EXPECT_EQ(m, WM_CREATE); }
            }
        }
        k_cb_unhook_all();
        kcb_echo_destroy_window(&e);
    }
}

TEST(kcb_create_child_window_also_two_deliveries) {
    REQUIRE(U(IsWindow)); U(IsWindow)((HWND)0);
    if (k_cb_count() == 0) { t_skip("callback table too small"); return; }
    struct kcb_echo par, kid;
    if (run_create(TRUE, WS_POPUP, HWND_MESSAGE, NULL, &par, "cp") != 0) { t_skip("window creation failed"); return; }
    k_cb_hook(10);
    LRESULT rc = run_create(FALSE, WS_CHILD, par.hwnd, NULL, &kid, "cc");
    if (rc != 0) { k_cb_unhook_all(); kcb_echo_destroy_window(&par); t_skip("child creation failed"); return; }
    if (!t_is_wine()) EXPECT_EQ(k_cb_hit_count(10), 2);
    CHECK((kid.create_cs[0].style & WS_CHILD) != 0);
    CHECK(kid.create_cs[0].hwndParent == par.hwnd);
    k_cb_unhook_all();
    kcb_echo_destroy_window(&kid);
    kcb_echo_destroy_window(&par);
}

static volatile LONG g_seen_create, g_seen_nccreate, g_seen_destroy;
static LRESULT CALLBACK kcb_fail_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_NCCREATE) { g_seen_nccreate++; return FALSE; }
    if (m == WM_CREATE) g_seen_create++;
    return DefWindowProcA(h, m, w, l);
}
static LRESULT CALLBACK kcb_veto_create_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_CREATE) { g_seen_create++; return -1; }
    if (m == WM_DESTROY) g_seen_destroy++;
    return DefWindowProcA(h, m, w, l);
}

TEST(kcb_create_nccreate_false_aborts_before_create) {
    /* A procedure returning FALSE for WM_NCCREATE (the value travels back through NtCallbackReturn
     * as the thunk's 64-bit result) makes CreateWindowEx fail without ever sending WM_CREATE. */
    REQUIRE(U(IsWindow)); U(IsWindow)((HWND)0);
    if (k_cb_count() == 0) { t_skip("callback table too small"); return; }
    g_seen_create = g_seen_nccreate = 0;
    char cls[64]; snprintf(cls, sizeof cls, "win32ktKcbFail_%lu", GetCurrentProcessId());
    WNDCLASSA wc; memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = kcb_fail_proc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = cls;
    if (!RegisterClassA(&wc)) { t_skip("RegisterClassA failed"); return; }
    k_cb_hook(10);
    SetLastError(0);
    HWND h = CreateWindowExA(0, cls, "x", WS_POPUP, 0, 0, 1, 1, HWND_MESSAGE, NULL, GetModuleHandleA(NULL), NULL);
    unsigned long hits = k_cb_hit_count(10);
    k_cb_unhook_all();
    CHECK(h == NULL);
    EXPECT_EQ(g_seen_nccreate, 1);
    EXPECT_EQ(g_seen_create, 0);
    if (!t_is_wine()) EXPECT_EQ(hits, 1);
    if (h) DestroyWindow(h);
    UnregisterClassA(cls, GetModuleHandleA(NULL));
}

TEST(kcb_create_wm_create_minus_one_fails_creation) {
    REQUIRE(U(IsWindow)); U(IsWindow)((HWND)0);
    if (k_cb_count() == 0) { t_skip("callback table too small"); return; }
    g_seen_create = g_seen_nccreate = g_seen_destroy = 0;
    char cls[64]; snprintf(cls, sizeof cls, "win32ktKcbVeto_%lu", GetCurrentProcessId());
    WNDCLASSA wc; memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = kcb_veto_create_proc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = cls;
    if (!RegisterClassA(&wc)) { t_skip("RegisterClassA failed"); return; }
    k_cb_hook(10);
    HWND h = CreateWindowExA(0, cls, "x", WS_POPUP, 0, 0, 1, 1, HWND_MESSAGE, NULL, GetModuleHandleA(NULL), NULL);
    unsigned long hits = k_cb_hit_count(10);
    k_cb_unhook_all();
    CHECK(h == NULL);
    EXPECT_EQ(g_seen_create, 1);
    if (!t_is_wine()) EXPECT_EQ(hits, 2);
    if (h) DestroyWindow(h);
    UnregisterClassA(cls, GetModuleHandleA(NULL));
}
