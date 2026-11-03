/* msg_send_cross_thread: argument validation, error codes and same-thread semantics of the send
 * family, plus direct NtUserMessageCall calls. */
#include "msc.h"

#define BIGW ((WPARAM)0x0123456789ABCDEFULL)
#define BIGL ((LPARAM)0xFEDCBA9876543210ULL)

static HWND mk(struct msc_beh **b, int ansi) {
    HWND h = msc_create(NULL, b, ansi, NULL);
    msc_log_reset();    /* the messages of window creation (WM_CREATE, WM_SIZE, ...) are logged too; count only what the test sends */
    return h;
}
static HWND mk_dead(void) {
    HWND h = msc_create(NULL, NULL, 0, NULL);
    if (h) DestroyWindow(h);
    return h;
}
static LRESULT nt_call(HWND h, UINT m, WPARAM w, LPARAM l, ULONG_PTR x, DWORD fnid, DWORD ansi) {
    NtUserMessageCall_pfn f = UN(NtUserMessageCall);
    return f ? f(h, m, w, l, x, fnid, ansi) : 0;
}
/* Wine's NtUserMessageCall uses its own "type" enumeration instead of the Windows fnid values, so the
 * direct-call tests (which pass the real user32 fnids) are Windows-only. */
#define NEED_NT() do { if (!UN(NtUserMessageCall)) { t_skip("win32u!NtUserMessageCall missing"); return; } \
                       if (t_is_wine()) { t_skip("Wine: NtUserMessageCall has a different fnid vocabulary"); return; } } while (0)

/* ---- stub identity ---------------------------------------------------------------------- */
TEST(msc_stub_ntusermessagecall_number) {
    FARPROC f = t_proc_in("win32u.dll", "NtUserMessageCall");
    REQUIRE(f);
    if (t_is_wine()) { t_note("Wine's win32u has its own syscall numbering"); return; }
    CHECK(k_stub_valid(f));
    /* tools/win32u_syscalls.tsv: NtUserMessageCall = 0x1007 (win32k.sys thunk). */
    EQ(k_stub_number(f), 0x1007);
}
TEST(msc_stub_related_exports_present) {
    static const char *names[] = { "NtUserMessageCall", "NtUserQuerySendMessage", "NtUserGetThreadState",
                                   "NtUserCallOneParam", "NtUserPostMessage", "NtUserPostThreadMessage" };
    for (unsigned i = 0; i < sizeof names / sizeof *names; i++) {
        FARPROC f = t_proc_in("win32u.dll", names[i]);
        CHECKF(f != NULL, "win32u export %s missing", names[i]);
        if (f && !t_is_wine()) CHECKF(k_stub_valid(f), "%s stub shape", names[i]);
    }
}
TEST(msc_user32_send_family_resolves) {
    CHECK(msc_api()->ok);
    CHECK(t_proc("SendMessageCallbackW") && t_proc("SendNotifyMessageW") && t_proc("SendMessageTimeoutW"));
    CHECK(t_proc("InSendMessageEx") && t_proc("ReplyMessage") && t_proc("IsHungAppWindow"));
}

/* ---- invalid window handles ------------------------------------------------------------- */
/* user32!SendMessageW: a handle that fails the handle-table check ends in RtlSetLastWin32Error
 * (ERROR_INVALID_WINDOW_HANDLE 1400) and returns 0; the A variant shares SendMessageWorker. */
TEST(msc_send_null_hwnd_1400) {
    MSC_NEED_GUI();
    SetLastError(0); EQ(SendMessageW(NULL, MSC_ECHO, 1, 2), 0); EXPECT_ERR(1400);
    SetLastError(0); EQ(SendMessageA(NULL, MSC_ECHO, 1, 2), 0); EXPECT_ERR(1400);
    SetLastError(0); EQ(SendMessageW(NULL, WM_NULL, 0, 0), 0); EXPECT_ERR(1400);
}
TEST(msc_send_destroyed_hwnd_1400) {
    MSC_NEED_GUI();
    HWND h = mk_dead(); REQUIRE(h);
    CHECK(!IsWindow(h));
    SetLastError(0); EQ(SendMessageW(h, MSC_ECHO, 1, 2), 0); EXPECT_ERR(1400);
    SetLastError(0); EQ(SendMessageA(h, WM_NULL, 1, 2), 0); EXPECT_ERR(1400);
    CHECK(msc_log_count(MSC_ECHO) == 0);
}
TEST(msc_send_wrong_uniq_hwnd_1400) {
    MSC_NEED_GUI();
    HWND h = mk(NULL, 0); REQUIRE(h);
    /* Same handle-table index, different uniqueness word (and not the 0xFFFF wildcard): the entry
     * check in SendMessageW compares HIWORD(hwnd) with the entry's uniq. */
    WORD hi = (WORD)((ULONG_PTR)h >> 16), nhi = hi ^ 3;
    if (nhi == 0xFFFF || nhi == hi) nhi = hi ^ 5;
    HWND bad = (HWND)(((ULONG_PTR)h & ~(ULONG_PTR)0xFFFF0000) | ((ULONG_PTR)nhi << 16));
    SetLastError(0); EQ(SendMessageW(bad, MSC_ECHO, 1, 2), 0); EXPECT_ERR(1400);
    CHECK(msc_log_count(MSC_ECHO) == 0);
    if (UN(NtUserMessageCall) && !t_is_wine()) {
        SetLastError(0); EQ(nt_call(bad, MSC_ECHO, 1, 2, 0, MSC_FNID_SENDMESSAGE, 0), 0);
        EXPECT_ERR(1400);
    }
    DestroyWindow(h);
}
TEST(msc_send_wildcard_uniq_resolves_to_window) {
    MSC_NEED_GUI();
    struct msc_beh *b; HWND h = mk(&b, 0); REQUIRE(h);
 /* user32!SendMessageW accepts uniq == 0xFFFF as "any generation". */
    HWND wild = (HWND)(((ULONG_PTR)h & 0xFFFF) | 0xFFFF0000ULL);
    LRESULT r = SendMessageW(wild, MSC_ECHO, BIGW, BIGL);
    EQ(r, msc_echo_ret(BIGW, BIGL));
    struct msc_ev e;
    CHECK(msc_log_find(MSC_ECHO, 0, &e));
    EQ(e.hwnd, h);               /* the procedure sees the real handle */
    DestroyWindow(h);
}
TEST(msc_send_garbage_handles_1400) {
    MSC_NEED_GUI();
    static const ULONG_PTR vals[] = { 1, 2, 0x10, 0x7FFFFFFF, 0xFFFFFFFE, 0x12345678, 0x00010000, 0x0000FFFE, 0x7FFE0000UL };
    for (unsigned i = 0; i < sizeof vals / sizeof *vals; i++) {
        HWND h = (HWND)vals[i];
        if (IsWindow(h)) continue;          /* a lucky live handle is not "garbage" */
        SetLastError(0);
        LRESULT r = SendMessageW(h, MSC_ECHO, 1, 2);
        CHECKF(r == 0 && GetLastError() == 1400, "hwnd %p: r=%lld err=%lu", h, (long long)r, GetLastError());
    }
}
TEST(msc_family_invalid_hwnd_all_report_1400) {
    MSC_NEED_GUI();
    HWND h = mk_dead(); REQUIRE(h);
    ULONG_PTR res = 0x77;
    SetLastError(0); CHECK(SendMessageTimeoutW(h, MSC_ECHO, 1, 2, SMTO_NORMAL, 100, &res) == 0); EXPECT_ERR(1400);
    SetLastError(0); CHECK(SendMessageTimeoutA(h, MSC_ECHO, 1, 2, SMTO_NORMAL, 100, &res) == 0); EXPECT_ERR(1400);
    SetLastError(0); CHECK(SendNotifyMessageW(h, MSC_ECHO, 1, 2) == FALSE); EXPECT_ERR(1400);
    SetLastError(0); CHECK(SendNotifyMessageA(h, MSC_ECHO, 1, 2) == FALSE); EXPECT_ERR(1400);
    SetLastError(0); CHECK(SendMessageCallbackW(h, MSC_ECHO, 1, 2, NULL, 0) == FALSE); EXPECT_ERR(1400);
    SetLastError(0); CHECK(SendMessageCallbackA(h, MSC_ECHO, 1, 2, NULL, 0) == FALSE); EXPECT_ERR(1400);
    /* low messages take the same route through the per-message handler table */
    SetLastError(0); CHECK(SendMessageTimeoutW(h, WM_NULL, 0, 0, SMTO_NORMAL, 100, &res) == 0); EXPECT_ERR(1400);
    SetLastError(0); CHECK(SendNotifyMessageW(h, WM_NULL, 0, 0) == FALSE); EXPECT_ERR(1400);
}
TEST(msc_smto_invalid_hwnd_zeroes_result) {
    MSC_NEED_GUI();
    HWND h = mk_dead(); REQUIRE(h);
    ULONG_PTR res = 0xAAAAAAAAAAAAAAAAULL;
    /* user32!SendMessageTimeoutW stores 0 into *lpdwResult before it touches the window. */
    CHECK(SendMessageTimeoutW(h, MSC_ECHO, 1, 2, SMTO_NORMAL, 100, &res) == 0);
    EQ(res, 0);
}

/* ---- reserved message bits and flag validation ------------------------------------------ */
/* Every entry point rejects msg & 0xFFFE0000 with ERROR_INVALID_PARAMETER (0x57) before looking at
 * the window: user32!SendMessageW/A, SendMessageTimeoutW (first branch), win32kfull!
 * NtUserMessageCall (after ValidateHwnd) and the notify and callback send paths. */
static const UINT g_badmsg[] = { 0x20000, 0x40000, 0x80000000u, 0xFFFE0000u, 0xFFFFFFFFu, 0x7FFE0000u, 0x00020400u, 0x00100000u };
TEST(msc_badmsg_sendmessage_w_a_0x57) {
    MSC_NEED_GUI();
    HWND h = mk(NULL, 0); REQUIRE(h);
    for (unsigned i = 0; i < sizeof g_badmsg / sizeof *g_badmsg; i++) {
        SetLastError(0); EQ(SendMessageW(h, g_badmsg[i], 1, 2), 0); EXPECT_ERR(0x57);
        SetLastError(0); EQ(SendMessageA(h, g_badmsg[i], 1, 2), 0); EXPECT_ERR(0x57);
    }
    CHECK(msc_log_total() == 0);
    DestroyWindow(h);
}
TEST(msc_badmsg_timeout_notify_callback_0x57) {
    MSC_NEED_GUI();
    HWND h = mk(NULL, 0); REQUIRE(h);
    for (unsigned i = 0; i < sizeof g_badmsg / sizeof *g_badmsg; i++) {
        ULONG_PTR res = 0x1111;
        SetLastError(0); CHECK(SendMessageTimeoutW(h, g_badmsg[i], 1, 2, SMTO_NORMAL, 50, &res) == 0); EXPECT_ERR(0x57);
        EQ(res, 0x1111);   /* rejected before *lpdwResult is initialised */
        SetLastError(0); CHECK(SendMessageTimeoutA(h, g_badmsg[i], 1, 2, SMTO_NORMAL, 50, &res) == 0); EXPECT_ERR(0x57);
        SetLastError(0); CHECK(SendNotifyMessageW(h, g_badmsg[i], 1, 2) == FALSE); EXPECT_ERR(0x57);
        SetLastError(0); CHECK(SendNotifyMessageA(h, g_badmsg[i], 1, 2) == FALSE); EXPECT_ERR(0x57);
        SetLastError(0); CHECK(SendMessageCallbackW(h, g_badmsg[i], 1, 2, NULL, 0) == FALSE); EXPECT_ERR(0x57);
        SetLastError(0); CHECK(SendMessageCallbackA(h, g_badmsg[i], 1, 2, NULL, 0) == FALSE); EXPECT_ERR(0x57);
    }
    CHECK(msc_log_total() == 0);
    DestroyWindow(h);
}
TEST(msc_badmsg_direct_ntusermessagecall_0x57) {
    MSC_NEED_GUI(); NEED_NT();
    HWND h = mk(NULL, 0); REQUIRE(h);
    static const DWORD fnids[] = { MSC_FNID_SENDMESSAGE, MSC_FNID_SENDMESSAGETIMEOUT, MSC_FNID_SENDNOTIFYMESSAGE, MSC_FNID_SENDMESSAGECALLBACK };
    for (unsigned i = 0; i < sizeof g_badmsg / sizeof *g_badmsg; i++)
        for (unsigned j = 0; j < 2; j++) {
            SetLastError(0);
            LRESULT r = nt_call(h, g_badmsg[i], 1, 2, 0, fnids[j], 0);
            CHECKF(r == 0 && GetLastError() == 0x57, "msg %08x fnid %x: r=%lld err=%lu", g_badmsg[i], fnids[j], (long long)r, GetLastError());
        }
    CHECK(msc_log_total() == 0);
    DestroyWindow(h);
}
TEST(msc_direct_hwnd_validated_before_msg) {
    MSC_NEED_GUI(); NEED_NT();
    HWND dead = mk_dead(); REQUIRE(dead);
    /* NtUserMessageCall runs ValidateHwnd first, so a dead window with a reserved-bit message
     * reports 1400, whereas a live window reports 0x57. */
    SetLastError(0); EQ(nt_call(dead, 0x20000, 0, 0, 0, MSC_FNID_SENDMESSAGE, 0), 0); EXPECT_ERR(1400);
    HWND h = mk(NULL, 0);
    SetLastError(0); EQ(nt_call(h, 0x20000, 0, 0, 0, MSC_FNID_SENDMESSAGE, 0), 0); EXPECT_ERR(0x57);
    DestroyWindow(h);
}
TEST(msc_msg_0x10000_to_0x1ffff_accepted) {
    MSC_NEED_GUI();
    struct msc_beh *b; HWND h = mk(&b, 0); REQUIRE(h);
    /* msg & 0xFFFE0000 == 0 for 0x10000..0x1FFFF, so these are delivered like any other message. */
    static const UINT ms[] = { 0x10000, 0x10400, 0x1FFFF, 0xC000, 0xFFFF, 0x8000 };
    for (unsigned i = 0; i < sizeof ms / sizeof *ms; i++) {
        SetLastError(0);
        SendMessageW(h, ms[i], BIGW, BIGL);
        CHECKF(GetLastError() != 0x57, "msg %x rejected", ms[i]);
        CHECKF(msc_log_count(ms[i]) == 1, "msg %x delivered %d times", ms[i], msc_log_count(ms[i]));
    }
    DestroyWindow(h);
}
TEST(msc_smto_reserved_flag_bits_0x57) {
    MSC_NEED_GUI();
    struct msc_beh *b; HWND h = mk(&b, 0); REQUIRE(h);
    /* fuFlags & 0xFFFFFF80 is rejected by user32!SendMessageTimeoutW before anything else. */
    static const UINT bad[] = { 0x80, 0x100, 0x400, 0x8000, 0x10000, 0x80000000u, 0xFFFFFF80u, 0xFFFFFFFFu };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        ULONG_PTR res = 0x4242;
        SetLastError(0);
        LRESULT r = SendMessageTimeoutW(h, MSC_ECHO, 1, 2, bad[i], 100, &res);
        CHECKF(r == 0 && GetLastError() == 0x57, "flags %x: r=%lld err=%lu", bad[i], (long long)r, GetLastError());
        EQ(res, 0x4242);
        SetLastError(0);
        r = SendMessageTimeoutA(h, MSC_ECHO, 1, 2, bad[i], 100, &res);
        CHECKF(r == 0 && GetLastError() == 0x57, "A flags %x: r=%lld err=%lu", bad[i], (long long)r, GetLastError());
    }
    CHECK(msc_log_count(MSC_ECHO) == 0);
    DestroyWindow(h);
}
TEST(msc_smto_all_defined_range_flags_accepted_same_thread) {
    MSC_NEED_GUI();
    struct msc_beh *b; HWND h = mk(&b, 0); REQUIRE(h);
    int bad = 0;
    for (UINT f = 0; f < 0x80; f++) {
        ULONG_PTR res = 0;
        SetLastError(0);
        LRESULT r = SendMessageTimeoutW(h, MSC_ECHO, BIGW, BIGL, f, 1000, &res);
        if (r == 0 || res != (ULONG_PTR)msc_echo_ret(BIGW, BIGL) || GetLastError() == 0x57) { bad++; t_note("flags 0x%02x r=%lld err=%lu", f, (long long)r, GetLastError()); }
    }
    CHECKF(bad == 0, "%d flag values were refused for a same-thread window", bad);
    CHECK(msc_log_count(MSC_ECHO) == 0x80);
    DestroyWindow(h);
}
TEST(msc_smto_null_result_pointer_ok) {
    MSC_NEED_GUI();
    HWND h = mk(NULL, 0); REQUIRE(h);
    CHECK(SendMessageTimeoutW(h, MSC_ECHO, 1, 2, SMTO_NORMAL, 100, NULL) != 0);
    CHECK(SendMessageTimeoutA(h, MSC_ECHO, 1, 2, SMTO_NORMAL, 100, NULL) != 0);
    CHECK(SendMessageTimeoutW(h, WM_NULL, 0, 0, SMTO_NORMAL, 100, NULL) != 0);
    CHECK(msc_log_count(MSC_ECHO) == 2);
    DestroyWindow(h);
}

/* ---- same-thread delivery ---------------------------------------------------------------- */
TEST(msc_samethread_roundtrips_64bit_values) {
    MSC_NEED_GUI();
    struct msc_beh *b; HWND h = mk(&b, 0); REQUIRE(h);
    static const struct { WPARAM w; LPARAM l; } v[] = {
        { 0, 0 }, { 1, -1 }, { BIGW, BIGL }, { (WPARAM)-1, (LPARAM)-1 }, { 0x80000000u, 0x7FFFFFFF }, { 0xFFFFFFFF00000000ULL, 0x00000000FFFFFFFFULL } };
    for (unsigned i = 0; i < sizeof v / sizeof *v; i++) {
        LRESULT r = SendMessageW(h, MSC_ECHO, v[i].w, v[i].l);
        EQ(r, msc_echo_ret(v[i].w, v[i].l));
        struct msc_ev e;
        if (msc_log_find(MSC_ECHO, (int)i, &e)) { EQ(e.w, v[i].w); EQ(e.l, v[i].l); EQ(e.hwnd, h); }
        else CHECK(0);
    }
    DestroyWindow(h);
}
TEST(msc_samethread_runs_on_calling_thread_not_in_send) {
    MSC_NEED_GUI();
    struct msc_beh *b; HWND h = mk(&b, 0); REQUIRE(h);
    SendMessageW(h, MSC_ECHO, 1, 2);
    struct msc_ev e; REQUIRE(msc_log_find(MSC_ECHO, 0, &e));
    EQ(e.tid, GetCurrentThreadId());
    /* user32!InSendMessageEx reads the CLIENTINFO flag word; an intra-thread call sets no
     * "sent by another thread" bit, and InSendMessage mirrors bit 1 of the same word. */
    CHECK(!e.insend);
    EQ(e.ismex, ISMEX_NOSEND);
    DestroyWindow(h);
}
TEST(msc_insendmessage_outside_any_send) {
    MSC_NEED_GUI();
    CHECK(!InSendMessage());
    EQ(InSendMessageEx(NULL), ISMEX_NOSEND);
    EQ(InSendMessageEx((LPVOID)0), 0);
}
TEST(msc_replymessage_outside_a_send_fails) {
    MSC_NEED_GUI();
    /* win32kfull!_ReplyMessage: no current tagSMS on the calling thread => returns 0. */
    CHECK(ReplyMessage(0) == FALSE);
    CHECK(ReplyMessage(0x1234) == FALSE);
}
TEST(msc_replymessage_in_samethread_send_fails) {
    MSC_NEED_GUI();
    struct msc_beh *b; HWND h = mk(&b, 0); REQUIRE(h);
    /* A same-thread SendMessage creates no tagSMS, so ReplyMessage from inside fails twice, and the
     * procedure's own return value is what SendMessage returns. */
    LRESULT r = SendMessageW(h, MSC_REPLY, 0, 0);
    EQ(r, 0x9999);
    CHECK(b->rep1 == FALSE);
    CHECK(b->rep2 == FALSE);
    EQ(b->ismex_before, ISMEX_NOSEND);
    EQ(b->ismex_after, ISMEX_NOSEND);
    DestroyWindow(h);
}
TEST(msc_samethread_notify_is_synchronous) {
    MSC_NEED_GUI();
    struct msc_beh *b; HWND h = mk(&b, 0); REQUIRE(h);
    /* SendNotifyMessage to a window of the calling thread behaves like SendMessage: the procedure has
     * run when the call returns. */
    CHECK(SendNotifyMessageW(h, MSC_ECHO, BIGW, BIGL) != FALSE);
    CHECK(msc_log_count(MSC_ECHO) == 1);
    CHECK(SendNotifyMessageA(h, MSC_ECHO, 1, 1) != FALSE);
    CHECK(msc_log_count(MSC_ECHO) == 2);
    DestroyWindow(h);
}

static volatile LONG g_cb_hits;
static HWND g_cb_hwnd; static UINT g_cb_msg; static ULONG_PTR g_cb_data; static LRESULT g_cb_res; static DWORD g_cb_tid;
static void CALLBACK cb_record(HWND h, UINT m, ULONG_PTR d, LRESULT r) {
    g_cb_hwnd = h; g_cb_msg = m; g_cb_data = d; g_cb_res = r; g_cb_tid = GetCurrentThreadId();
    InterlockedIncrement(&g_cb_hits);
}
TEST(msc_samethread_callback_invoked_before_return) {
    MSC_NEED_GUI();
    struct msc_beh *b; HWND h = mk(&b, 0); REQUIRE(h);
    g_cb_hits = 0;
    BOOL ok = SendMessageCallbackW(h, MSC_ECHO, BIGW, BIGL, cb_record, 0xC0FFEE);
    CHECK(ok);
    CHECK(msc_log_count(MSC_ECHO) == 1);
    /* docs: for a window of the calling thread the procedure runs immediately and the callback
     * is called right after, before SendMessageCallback returns. */
    CHECKF(g_cb_hits == 1, "callback hits %ld", g_cb_hits);
    if (g_cb_hits == 1) {
        EQ(g_cb_hwnd, h); EQ(g_cb_msg, MSC_ECHO); EQ(g_cb_data, 0xC0FFEE);
        EQ(g_cb_res, msc_echo_ret(BIGW, BIGL)); EQ(g_cb_tid, GetCurrentThreadId());
    }
    DestroyWindow(h);
}

/* ---- direct NtUserMessageCall on a same-thread window ------------------------------------ */
TEST(msc_direct_sendmessage_fnid_echo_w_and_a) {
    MSC_NEED_GUI(); NEED_NT();
    struct msc_beh *b; HWND h = mk(&b, 0); REQUIRE(h);
    LRESULT r = nt_call(h, MSC_ECHO, BIGW, BIGL, 0, MSC_FNID_SENDMESSAGE, 0);
    EQ(r, msc_echo_ret(BIGW, BIGL));
    r = nt_call(h, MSC_ECHO, 5, 6, 0, MSC_FNID_SENDMESSAGE, 1);
    EQ(r, msc_echo_ret(5, 6));
    CHECK(msc_log_count(MSC_ECHO) == 2);
    struct msc_ev e; REQUIRE(msc_log_find(MSC_ECHO, 0, &e));
    EQ(e.hwnd, h);
    EQ(e.tid, GetCurrentThreadId());
    DestroyWindow(h);
}
TEST(msc_direct_sendmessage_low_message_dispatches_handler) {
    MSC_NEED_GUI(); NEED_NT();
    struct msc_beh *b; HWND h = mk(&b, 0); REQUIRE(h);
    /* WM_NULL has the NtUserfnDWORD handler (MessageTable word 0x0400): it reaches the procedure. */
    EQ(nt_call(h, WM_NULL, 0, 0, 0, MSC_FNID_SENDMESSAGE, 0), 0);
    CHECK(msc_log_count(WM_NULL) == 1);
    /* WM_GETTEXTLENGTH goes through NtUserfnGETDBCSTEXTLENGTHS and DefWindowProc: "win32kt_msc" = 11. */
    EQ(nt_call(h, WM_GETTEXTLENGTH, 0, 0, 0, MSC_FNID_SENDMESSAGE, 0), 11);
    EQ(nt_call(h, WM_GETTEXTLENGTH, 0, 0, 0, MSC_FNID_SENDMESSAGE, 1), 11);
    DestroyWindow(h);
}
TEST(msc_direct_smto_block_layout) {
    MSC_NEED_GUI(); NEED_NT();
    struct msc_beh *b; HWND h = mk(&b, 0); REQUIRE(h);
    /* the send path reads {flags, timeout} from the first qword and writes back the status at
     * +8 (1 on success) and the LRESULT at +16 - exactly the block SendMessageTimeoutW builds. */
    struct msc_smto s = { SMTO_NORMAL, 1000, 0x1111, 0x2222 };
    nt_call(h, MSC_ECHO, BIGW, BIGL, (ULONG_PTR)&s, MSC_FNID_SENDMESSAGETIMEOUT, 0);
    EQ(s.ret, 1);
    EQ(s.result, msc_echo_ret(BIGW, BIGL));
    EQ(s.flags, SMTO_NORMAL);
    EQ(s.timeout, 1000);
    DestroyWindow(h);
}
TEST(msc_direct_smto_block_low_message) {
    MSC_NEED_GUI(); NEED_NT();
    struct msc_beh *b; HWND h = mk(&b, 0); REQUIRE(h);
    struct msc_smto s = { SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, 0, 0x3333 };
    nt_call(h, WM_GETTEXTLENGTH, 0, 0, (ULONG_PTR)&s, MSC_FNID_SENDMESSAGETIMEOUT, 0);
    EQ(s.ret, 1);
    EQ(s.result, 11);
    DestroyWindow(h);
}
TEST(msc_direct_broadcast_hwnd_needs_notify_or_callback_fnid) {
    MSC_NEED_GUI(); NEED_NT();
    /* win32kfull!NtUserMessageCall only lets HWND_BROADCAST (0xFFFF) / -1 through for fnid 0x2B7 and
     * 0x2B8 ((fnid - 0x2B7) < 2); every other fnid runs ValidateHwnd on it and fails with 1400. */
    static const DWORD fnids[] = { MSC_FNID_SENDMESSAGE, MSC_FNID_SENDMESSAGETIMEOUT };
    static const ULONG_PTR bc[] = { 0xFFFF, (ULONG_PTR)-1 };
    for (unsigned i = 0; i < 2; i++)
        for (unsigned j = 0; j < 2; j++) {
            SetLastError(0);
            LRESULT r = nt_call((HWND)bc[j], MSC_ECHO, 1, 2, 0, fnids[i], 0);
            CHECKF(r == 0 && GetLastError() == 1400, "fnid %x hwnd %p r=%lld err=%lu", fnids[i], (void *)bc[j], (long long)r, GetLastError());
        }
}
TEST(msc_direct_notify_fnid_broadcast_reaches_samethread_windows) {
    MSC_NEED_GUI(); NEED_NT();
    struct msc_beh *b; HWND h = mk(&b, 0); REQUIRE(h);
    WCHAR nm[64]; swprintf(nm, 64, L"win32ktMscBc_%lu", GetCurrentProcessId());
    UINT priv = RegisterWindowMessageW(nm);
    REQUIRE(priv >= 0xC000);
    /* A sent-notify broadcast delivers to the caller's own top-level windows synchronously. */
    nt_call((HWND)0xFFFF, priv, 0x11, 0x22, 0, MSC_FNID_SENDNOTIFYMESSAGE, 0);
    msc_pump(50);
    CHECKF(msc_log_count(priv) == 1, "private message delivered %d times", msc_log_count(priv));
    DestroyWindow(h);
}
TEST(msc_broadcast_private_message_sendmessagetimeout) {
    MSC_NEED_GUI();
    struct msc_worker wk; MSC_NEED_WORKER(wk, 0, 0);
    struct msc_beh *b; HWND h = mk(&b, 0); REQUIRE(h);
    WCHAR nm[64]; swprintf(nm, 64, L"win32ktMscBt_%lu", GetCurrentProcessId());
    UINT priv = RegisterWindowMessageW(nm);
    msc_log_reset();
    ULONG_PTR res = 0;
    /* HWND_BROADCAST: the broadcast path visits every top-level window; ours must each see it
     * exactly once (the message-only window is not a top-level desktop child). */
    SendMessageTimeoutW(HWND_BROADCAST, priv, 0x33, 0x44, SMTO_ABORTIFHUNG, 1000, &res);
    CHECKF(msc_log_count(priv) >= 3, "private broadcast reached %d of our 3 top-level windows", msc_log_count(priv));
    int per[3] = {0, 0, 0}; struct msc_ev e;
    HWND want[3] = { h, wk.w, wk.wa };
    for (int i = 0; msc_log_find(priv, i, &e); i++)
        for (int j = 0; j < 3; j++) if (e.hwnd == want[j]) per[j]++;
    for (int j = 0; j < 3; j++) CHECKF(per[j] == 1, "window %d got %d copies", j, per[j]);
    CHECK(msc_log_find(priv, 0, &e) && e.w == 0x33 && e.l == 0x44);
    DestroyWindow(h);
    msc_worker_stop(&wk);
}
