/* WM_TIMER generation and delivery, QS_TIMER, TIMERPROC validation and system timers.
 * Waits are bounded (3 s) and no assertion depends on exact timing. */
#include "mta.h"

#define WAIT_MS 3000

static BOOL peek_timer(HWND w, MSG *m) { return U(PeekMessageW)(m, w, WM_TIMER, WM_TIMER, PM_REMOVE); }

/* ---- plain WM_TIMER ------------------------------------------------------------------------------- */
TEST(msg_timers_atoms_deliver_wm_timer_to_window) {
    MTA_WIN(w);
    mta_reset();
    MEQ(U(SetTimer)(w, 5, 20, NULL), 5);
    int n = mta_pump_until(0, WM_TIMER, 1, WAIT_MS);
    CHECK(n >= 1);
    if (n) {
        MEQ(mta_log[0].msg, WM_TIMER);
        MEQ(mta_log[0].hwnd, w);
        MEQ(mta_log[0].wp, 5);
        MEQ(mta_log[0].lp, 0);       /* no TIMERPROC: the timer message carries a NULL callback in lParam */
        MEQ(mta_log[0].tid, GetCurrentThreadId());
    }
    U(KillTimer)(w, 5);
    mta_destroy(w);
}
TEST(msg_timers_atoms_deliver_periodic) {
    MTA_WIN(w);
    mta_reset();
    MEQ(U(SetTimer)(w, 6, 20, NULL), 6);
    int n = mta_pump_until(0, WM_TIMER, 4, WAIT_MS);
    CHECKF(n >= 4, "only %d WM_TIMER in %d ms", n, WAIT_MS);
    if (n >= 4) {
        DWORD mn = 0xffffffff;
        for (int i = 1; i < n && i < 8; i++) { DWORD d = mta_log[i].tick - mta_log[i - 1].tick; if (d < mn) mn = d; }
        t_info("minimum observed spacing of a 20 ms WM_TIMER: %lu ms", mn);
    }
    U(KillTimer)(w, 6);
    mta_destroy(w);
}
TEST(msg_timers_atoms_deliver_zero_elapse_fires) {
    MTA_WIN(w);
    mta_reset();
    MEQ(U(SetTimer)(w, 7, 0, NULL), 7);     /* clamped to USER_TIMER_MINIMUM, so it still fires */
    CHECK(mta_pump_until(0, WM_TIMER, 1, WAIT_MS) >= 1);
    U(KillTimer)(w, 7);
    mta_destroy(w);
}
TEST(msg_timers_atoms_deliver_killed_timer_is_silent) {
    MTA_WIN(w);
    mta_reset();
    MEQ(U(SetTimer)(w, 8, 20, NULL), 8);
    CHECK(mta_pump_until(0, WM_TIMER, 1, WAIT_MS) >= 1);
    CHECK(U(KillTimer)(w, 8));
    mta_drain_timers();
    mta_reset();
    mta_pump_ms(150);
    MEQ(mta_count(WM_TIMER, -1), 0);
    mta_destroy(w);
}
TEST(msg_timers_atoms_deliver_destroyed_window_is_silent) {
    MTA_WIN(w);
    mta_reset();
    MEQ(U(SetTimer)(w, 9, 20, NULL), 9);
    CHECK(mta_pump_until(0, WM_TIMER, 1, WAIT_MS) >= 1);
    mta_destroy(w);
    mta_drain_timers();
    mta_reset();
    mta_pump_ms(150);
    MEQ(mta_count(WM_TIMER, -1), 0);
}
TEST(msg_timers_atoms_deliver_replace_shortens_interval) {
    MTA_WIN(w);
    mta_reset();
    MEQ(U(SetTimer)(w, 10, 3600000, NULL), 10);
    mta_pump_ms(120);
    MEQ(mta_count(WM_TIMER, -1), 0);
    MEQ(U(SetTimer)(w, 10, 20, NULL), 10);    /* the existing timer is re-armed with the new interval */
    CHECK(mta_pump_until(0, WM_TIMER, 1, WAIT_MS) >= 1);
    U(KillTimer)(w, 10);
    mta_destroy(w);
}
TEST(msg_timers_atoms_deliver_two_timers_both_arrive) {
    MTA_WIN(w);
    mta_reset();
    MEQ(U(SetTimer)(w, 21, 20, NULL), 21);
    MEQ(U(SetTimer)(w, 22, 20, NULL), 22);
    mta_pump_until(0, WM_TIMER, 6, WAIT_MS);
    CHECK(mta_count(WM_TIMER, 21) >= 1);
    CHECK(mta_count(WM_TIMER, 22) >= 1);
    U(KillTimer)(w, 21); U(KillTimer)(w, 22);
    mta_destroy(w);
}
TEST(msg_timers_atoms_deliver_wparam_is_full_id) {
    MTA_WIN(w);
    mta_reset();
    UINT_PTR id = (UINT_PTR)0x1122334455ULL;
    MEQ(U(SetTimer)(w, id, 20, NULL), id);
    CHECK(mta_pump_until(0, WM_TIMER, 1, WAIT_MS) >= 1);
    if (mta_nlog) MEQ(mta_log[0].wp, id);      /* wParam carries tagTIMER.nID, a 64-bit value */
    U(KillTimer)(w, id);
    mta_destroy(w);
}
TEST(msg_timers_atoms_deliver_wm_timer_posted_with_zero_lparam) {
    MTA_WIN(w);
    mta_reset();
    /* a WM_TIMER with lParam == 0 takes the normal dispatch path, nothing is validated */
    CHECK(U(PostMessageW)(w, WM_TIMER, 999, 0));
    CHECK(mta_pump_until(0, WM_TIMER, 1, WAIT_MS) == 1);
    MEQ(mta_log[0].wp, 999);
    mta_destroy(w);
}

/* ---- TIMERPROC ---------------------------------------------------------------------------------- */
TEST(msg_timers_atoms_deliver_timerproc_replaces_wndproc) {
    MTA_WIN(w);
    mta_reset();
    MEQ(U(SetTimer)(w, 31, 20, mta_proc_a), 31);
    CHECK(mta_pump_until(1, 0, 1, WAIT_MS) >= 1);
    MEQ(mta_count(WM_TIMER, -1), 0);          /* DispatchMessage calls the TIMERPROC, not the window procedure */
    if (mta_nplog) {
        MEQ(mta_plog[0].hwnd, w);
        MEQ(mta_plog[0].msg, WM_TIMER);
        MEQ(mta_plog[0].id, 31);
        MEQ(mta_plog[0].tid, GetCurrentThreadId());
    }
    U(KillTimer)(w, 31);
    mta_destroy(w);
}
TEST(msg_timers_atoms_deliver_timerproc_tick_argument) {
    MTA_WIN(w);
    mta_reset();
    MEQ(U(SetTimer)(w, 32, 20, mta_proc_a), 32);
    CHECK(mta_pump_until(1, 0, 1, WAIT_MS) >= 1);
    if (mta_nplog) {
        /* DispatchMessageWorker passes the KUSER_SHARED_DATA tick count as the 4th argument */
        LONG d = (LONG)(GetTickCount() - mta_plog[0].tick);
        CHECKF(d >= 0 && d < 5000, "tick argument %lu vs now %lu", mta_plog[0].tick, GetTickCount());
    }
    U(KillTimer)(w, 32);
    mta_destroy(w);
}
TEST(msg_timers_atoms_deliver_timerproc_message_lparam_is_proc) {
    MTA_WIN(w);
    mta_reset();
    MEQ(U(SetTimer)(w, 33, 20, mta_proc_b), 33);
    MSG m; int got = 0;
    DWORD end = GetTickCount() + WAIT_MS;
    while (!got && (LONG)(end - GetTickCount()) > 0) { got = peek_timer(w, &m); if (!got) Sleep(2); }
    CHECK(got);
    if (got) {
        MEQ(m.message, WM_TIMER); MEQ(m.hwnd, w); MEQ(m.wParam, 33);
        MEQ(m.lParam, (LPARAM)mta_proc_b);     /* the retrieved message exposes tagTIMER.pfn */
        U(DispatchMessageW)(&m);
        MEQ(mta_pcount(2), 1);
        MEQ(mta_pcount(1), 0);
    }
    U(KillTimer)(w, 33);
    mta_destroy(w);
}
TEST(msg_timers_atoms_deliver_two_procs_routed_separately) {
    MTA_WIN(w);
    mta_reset();
    MEQ(U(SetTimer)(w, 34, 20, mta_proc_a), 34);
    MEQ(U(SetTimer)(w, 35, 20, mta_proc_b), 35);
    mta_pump_until(1, 0, 2, WAIT_MS);
    mta_pump_until(2, 0, 2, WAIT_MS);
    for (int i = 0; i < mta_nplog && i < MTA_LOG_MAX; i++) {
        if (mta_plog[i].which == 1) MEQ(mta_plog[i].id, 34);
        if (mta_plog[i].which == 2) MEQ(mta_plog[i].id, 35);
    }
    CHECK(mta_pcount(1) >= 1); CHECK(mta_pcount(2) >= 1);
    U(KillTimer)(w, 34); U(KillTimer)(w, 35);
    mta_destroy(w);
}
TEST(msg_timers_atoms_deliver_null_hwnd_timerproc) {
    MTA_NEED();
    mta_reset();
    UINT_PTR id = U(SetTimer)(NULL, 0, 20, mta_proc_a);
    REQUIRE(id);
    MSG m; int got = 0;
    DWORD end = GetTickCount() + WAIT_MS;
    while (!got && (LONG)(end - GetTickCount()) > 0) { got = U(PeekMessageW)(&m, NULL, WM_TIMER, WM_TIMER, PM_REMOVE); if (!got) Sleep(2); }
    CHECK(got);
    if (got) {
        MEQ(m.hwnd, NULL); MEQ(m.wParam, id); MEQ(m.lParam, (LPARAM)mta_proc_a);
        U(DispatchMessageW)(&m);       /* hwnd NULL: still routed to the TIMERPROC */
        MEQ(mta_pcount(1), 1);
        if (mta_nplog) { MEQ(mta_plog[0].hwnd, NULL); MEQ(mta_plog[0].id, id); }
    }
    U(KillTimer)(NULL, id);
    mta_drain_timers();
}
TEST(msg_timers_atoms_deliver_null_hwnd_without_proc_has_no_target) {
    MTA_NEED();
    UINT_PTR id = U(SetTimer)(NULL, 0, 20, NULL);
    REQUIRE(id);
    MSG m; int got = 0;
    DWORD end = GetTickCount() + WAIT_MS;
    while (!got && (LONG)(end - GetTickCount()) > 0) { got = U(PeekMessageW)(&m, NULL, WM_TIMER, WM_TIMER, PM_REMOVE); if (!got) Sleep(2); }
    CHECK(got);
    if (got) {
        MEQ(m.hwnd, NULL); MEQ(m.wParam, id); MEQ(m.lParam, 0);
        MEQ(U(DispatchMessageW)(&m), 0);     /* nothing to dispatch to */
    }
    U(KillTimer)(NULL, id);
    mta_drain_timers();
}
TEST(msg_timers_atoms_forged_wm_timer_with_unregistered_proc_not_called) {
    MTA_WIN(w);
    mta_reset();
    MEQ(U(SetTimer)(w, 1, 3600000, mta_proc_a), 1);
    MSG m; memset(&m, 0, sizeof m);
    m.hwnd = w; m.message = WM_TIMER; m.wParam = 66; m.lParam = (LPARAM)mta_proc_b;
    /* DispatchMessageWorker asks NtUserValidateTimerCallback first; proc_b belongs to no timer, so nothing runs */
    LRESULT r = U(DispatchMessageW)(&m);
    MEQ(r, 0);
    MEQ(mta_pcount(2), 0);
    MEQ(mta_count(WM_TIMER, -1), 0);
    U(KillTimer)(w, 1);
    mta_destroy(w);
}
TEST(msg_timers_atoms_forged_wm_timer_with_registered_proc_called) {
    MTA_WIN(w);
    mta_reset();
    MEQ(U(SetTimer)(w, 1, 3600000, mta_proc_a), 1);
    MSG m; memset(&m, 0, sizeof m);
    m.hwnd = w; m.message = WM_TIMER; m.wParam = 77; m.lParam = (LPARAM)mta_proc_a;
    U(DispatchMessageW)(&m);       /* validation only compares the procedure address against the process's timers */
    MEQ(mta_pcount(1), 1);
    if (mta_nplog) { MEQ(mta_plog[0].id, 77); MEQ(mta_plog[0].hwnd, w); MEQ(mta_plog[0].msg, WM_TIMER); }
    U(KillTimer)(w, 1);
    mta_destroy(w);
}
TEST(msg_timers_atoms_posted_wm_timer_registered_proc_called) {
    MTA_WIN(w);
    mta_reset();
    MEQ(U(SetTimer)(w, 1, 3600000, mta_proc_b), 1);
    CHECK(U(PostMessageW)(w, WM_TIMER, 88, (LPARAM)mta_proc_b));
    mta_pump_until(2, 0, 1, WAIT_MS);
    MEQ(mta_pcount(2), 1);
    if (mta_nplog) MEQ(mta_plog[0].id, 88);
    U(KillTimer)(w, 1);
    mta_destroy(w);
}
TEST(msg_timers_atoms_forged_proc_after_kill_not_called) {
    MTA_WIN(w);
    mta_reset();
    MEQ(U(SetTimer)(w, 1, 3600000, mta_proc_a), 1);
    CHECK(U(KillTimer)(w, 1));
    MSG m; memset(&m, 0, sizeof m);
    m.hwnd = w; m.message = WM_TIMER; m.wParam = 1; m.lParam = (LPARAM)mta_proc_a;
    MEQ(U(DispatchMessageW)(&m), 0);        /* the timer object, and with it the registration, is gone */
    MEQ(mta_pcount(1), 0);
    mta_destroy(w);
}

/* ---- NtUserValidateTimerCallback --------------------------------------------------------------- */
TEST(msg_timers_atoms_validate_callback_registered_proc) {
    MTA_WIN(w);
    NtUserValidateTimerCallback_pfn v = UN(NtUserValidateTimerCallback);
    REQUIRE(v);
    if (t_is_wine()) { t_skip("Wine's win32u aborts on the unimplemented NtUserValidateTimerCallback"); mta_destroy(w); return; }
    MEQ(U(SetTimer)(w, 1, 3600000, mta_proc_a), 1);
    MEQ(v((PVOID)mta_proc_a), 1);
    U(KillTimer)(w, 1);
    mta_destroy(w);
}
TEST(msg_timers_atoms_validate_callback_unregistered_proc) {
    MTA_WIN(w);
    NtUserValidateTimerCallback_pfn v = UN(NtUserValidateTimerCallback);
    REQUIRE(v);
    if (t_is_wine()) { t_skip("Wine's win32u aborts on the unimplemented NtUserValidateTimerCallback"); mta_destroy(w); return; }
    MEQ(U(SetTimer)(w, 1, 3600000, mta_proc_a), 1);
    MEQ(v((PVOID)mta_proc_b), 0);
    MEQ(v((PVOID)(ULONG_PTR)0x12340000), 0);
    U(KillTimer)(w, 1);
    mta_destroy(w);
}
TEST(msg_timers_atoms_validate_callback_follows_lifetime) {
    MTA_WIN(w);
    NtUserValidateTimerCallback_pfn v = UN(NtUserValidateTimerCallback);
    REQUIRE(v);
    if (t_is_wine()) { t_skip("Wine's win32u aborts on the unimplemented NtUserValidateTimerCallback"); mta_destroy(w); return; }
    MEQ(v((PVOID)mta_proc_a), 0);
    MEQ(U(SetTimer)(w, 1, 3600000, mta_proc_a), 1);
    MEQ(v((PVOID)mta_proc_a), 1);
    CHECK(U(KillTimer)(w, 1));
    MEQ(v((PVOID)mta_proc_a), 0);
    MEQ(U(SetTimer)(w, 2, 3600000, mta_proc_a), 2);
    mta_destroy(w);                                 /* window death removes the timer */
    MEQ(v((PVOID)mta_proc_a), 0);
}
TEST(msg_timers_atoms_validate_callback_null_hwnd_timer) {
    MTA_NEED();
    NtUserValidateTimerCallback_pfn v = UN(NtUserValidateTimerCallback);
    REQUIRE(v);
    if (t_is_wine()) { t_skip("Wine's win32u aborts on the unimplemented NtUserValidateTimerCallback"); return; }
    UINT_PTR id = U(SetTimer)(NULL, 0, 3600000, mta_proc_b);
    REQUIRE(id);
    MEQ(v((PVOID)mta_proc_b), 1);
    U(KillTimer)(NULL, id);
    MEQ(v((PVOID)mta_proc_b), 0);
}
TEST(msg_timers_atoms_validate_callback_two_timers_share_proc) {
    MTA_WIN(w);
    NtUserValidateTimerCallback_pfn v = UN(NtUserValidateTimerCallback);
    REQUIRE(v);
    if (t_is_wine()) { t_skip("Wine's win32u aborts on the unimplemented NtUserValidateTimerCallback"); mta_destroy(w); return; }
    MEQ(U(SetTimer)(w, 1, 3600000, mta_proc_a), 1);
    MEQ(U(SetTimer)(w, 2, 3600000, mta_proc_a), 2);
    U(KillTimer)(w, 1);
    MEQ(v((PVOID)mta_proc_a), 1);              /* one registration remains */
    U(KillTimer)(w, 2);
    MEQ(v((PVOID)mta_proc_a), 0);
    mta_destroy(w);
}

/* ---- QS_TIMER, priority and coalescing ----------------------------------------------------------- */
static int qs_timer_set(void) { return (int)((U(GetQueueStatus)(QS_TIMER) >> 16) & QS_TIMER); }
TEST(msg_timers_atoms_qs_timer_flag) {
    MTA_WIN(w);
    mta_drain_timers();
    CHECK(!qs_timer_set());
    MEQ(U(SetTimer)(w, 1, 150, NULL), 1);
    int hit = 0;
    DWORD end = GetTickCount() + WAIT_MS;
    while (!hit && (LONG)(end - GetTickCount()) > 0) { hit = qs_timer_set(); if (!hit) Sleep(5); }
    CHECK(hit);                                  /* RIT sets the QS_TIMER wake bit when the timer comes due */
    MSG m;
    DWORD t0 = GetTickCount();
    CHECK(peek_timer(w, &m));
    int still = qs_timer_set();
    if (GetTickCount() - t0 < 80) CHECK(!still);   /* retrieving the message clears the bit until the next expiry */
    else t_note("slow run (%lu ms), QS_TIMER recheck skipped", GetTickCount() - t0);
    U(KillTimer)(w, 1);
    mta_drain_timers();
    mta_destroy(w);
}
TEST(msg_timers_atoms_qs_timer_clear_without_timers) {
    MTA_NEED();
    mta_drain_timers();
    CHECK(!qs_timer_set());
}
TEST(msg_timers_atoms_qs_timer_clears_after_kill) {
    MTA_WIN(w);
    MEQ(U(SetTimer)(w, 1, 20, NULL), 1);
    int hit = 0;
    DWORD end = GetTickCount() + WAIT_MS;
    while (!hit && (LONG)(end - GetTickCount()) > 0) { hit = qs_timer_set(); if (!hit) Sleep(5); }
    CHECK(hit);
    U(KillTimer)(w, 1);
    mta_drain_timers();
    CHECK(!qs_timer_set());
    mta_destroy(w);
}
TEST(msg_timers_atoms_posted_message_beats_wm_timer) {
    MTA_WIN(w);
    mta_drain_timers();
    MEQ(U(SetTimer)(w, 1, 20, NULL), 1);
    int hit = 0;
    DWORD end = GetTickCount() + WAIT_MS;
    while (!hit && (LONG)(end - GetTickCount()) > 0) { hit = qs_timer_set(); if (!hit) Sleep(5); }
    REQUIRE(hit);
    CHECK(U(PostMessageW)(w, WM_APP + 1, 1, 2));
    MSG m; UINT first = 0;
    for (int i = 0; i < 10 && !first; i++)
        if (U(PeekMessageW)(&m, w, 0, 0, PM_REMOVE) && (m.message == WM_APP + 1 || m.message == WM_TIMER)) first = m.message;
    MEQ(first, WM_APP + 1);     /* WM_TIMER is generated only when the queue holds nothing else */
    U(KillTimer)(w, 1);
    mta_drain_timers();
    mta_destroy(w);
}
TEST(msg_timers_atoms_sent_message_does_not_consume_timer) {
    MTA_WIN(w);
    mta_reset();
    MEQ(U(SetTimer)(w, 1, 20, NULL), 1);
    mta_pump_ms(60);
    U(SendMessageW)(w, WM_APP + 2, 0, 0);
    mta_pump_until(0, WM_TIMER, 1, WAIT_MS);
    CHECK(mta_count(WM_TIMER, -1) >= 1);
    U(KillTimer)(w, 1);
    mta_destroy(w);
}
TEST(msg_timers_atoms_missed_expirations_coalesce) {
    MTA_WIN(w);
    mta_drain_timers();
    MEQ(U(SetTimer)(w, 1, 100, NULL), 1);
    Sleep(450);                       /* several periods pass with the queue not being read */
    MSG m; int c = 0;
    DWORD t0 = GetTickCount();
    while (c < 8 && peek_timer(w, &m)) c++;
    DWORD dt = GetTickCount() - t0;
    CHECK(c >= 1);
    if (dt < 60) CHECKF(c == 1, "%d WM_TIMER messages for one timer after missed expirations", c);   /* one pending WM_TIMER per timer */
    else t_note("burst took %lu ms, coalescing count %d not asserted", dt, c);
    U(KillTimer)(w, 1);
    mta_drain_timers();
    mta_destroy(w);
}
TEST(msg_timers_atoms_one_message_per_timer_after_burst) {
    MTA_WIN(w);
    mta_drain_timers();
    MEQ(U(SetTimer)(w, 1, 100, NULL), 1);
    MEQ(U(SetTimer)(w, 2, 100, NULL), 2);
    Sleep(450);
    MSG m; int c1 = 0, c2 = 0, n = 0;
    DWORD t0 = GetTickCount();
    while (n++ < 12 && peek_timer(w, &m)) { if (m.wParam == 1) c1++; else if (m.wParam == 2) c2++; }
    DWORD dt = GetTickCount() - t0;
    CHECK(c1 >= 1); CHECK(c2 >= 1);
    if (dt < 60) { MEQ(c1, 1); MEQ(c2, 1); }
    else t_note("burst took %lu ms, counts %d/%d not asserted", dt, c1, c2);
    U(KillTimer)(w, 1); U(KillTimer)(w, 2);
    mta_drain_timers();
    mta_destroy(w);
}
TEST(msg_timers_atoms_peek_without_remove_leaves_timer) {
    MTA_WIN(w);
    mta_drain_timers();
    MEQ(U(SetTimer)(w, 1, 20, NULL), 1);
    int hit = 0;
    DWORD end = GetTickCount() + WAIT_MS;
    while (!hit && (LONG)(end - GetTickCount()) > 0) { hit = qs_timer_set(); if (!hit) Sleep(5); }
    REQUIRE(hit);
    MSG m;
    CHECK(U(PeekMessageW)(&m, w, WM_TIMER, WM_TIMER, PM_NOREMOVE));
    MEQ(m.wParam, 1);
    MEQ(m.message, WM_TIMER);
    /* a second look may legitimately see nothing until the next expiry, so it is informational only */
    if (!U(PeekMessageW)(&m, w, WM_TIMER, WM_TIMER, PM_NOREMOVE)) t_note("PM_NOREMOVE consumed the pending WM_TIMER state");
    int got = 0;
    DWORD end2 = GetTickCount() + WAIT_MS;
    while (!got && (LONG)(end2 - GetTickCount()) > 0) { got = peek_timer(w, &m); if (!got) Sleep(2); }
    CHECK(got);
    U(KillTimer)(w, 1);
    mta_drain_timers();
    mta_destroy(w);
}
TEST(msg_timers_atoms_filtered_peek_ignores_timers) {
    MTA_WIN(w);
    mta_drain_timers();
    MEQ(U(SetTimer)(w, 1, 20, NULL), 1);
    Sleep(80);
    MSG m;
    CHECK(!U(PeekMessageW)(&m, w, WM_APP, WM_APP + 0x10, PM_REMOVE));   /* the WM_TIMER is outside the filter */
    CHECK(peek_timer(w, &m));
    U(KillTimer)(w, 1);
    mta_drain_timers();
    mta_destroy(w);
}

/* ---- queue ownership across threads ------------------------------------------------------------------- */
TEST(msg_timers_atoms_cross_thread_window_timer_goes_to_owner_thread) {
    MTA_NEED();
    struct mta_owner o;
    if (!mta_owner_start(&o, 1, 1, 0)) { t_skip("owner thread"); return; }
    mta_reset();
    MEQ(U(SetTimer)(o.hwnd, 5, 20, NULL), 5);
    DWORD end = GetTickCount() + WAIT_MS; int n = 0;
    while ((LONG)(end - GetTickCount()) > 0 && !(n = mta_count(WM_TIMER, 5))) Sleep(5);
    CHECK(n >= 1);
    if (n) { MEQ(mta_log[0].tid, o.tid); MEQ(mta_log[0].hwnd, o.hwnd); }
    CHECK(U(KillTimer)(o.hwnd, 5));
    mta_owner_stop(&o);
}
struct tq { volatile LONG ready; volatile LONG got; DWORD tid; UINT_PTR id; };
static DWORD WINAPI tq_thread(LPVOID p) {
    struct tq *q = p; MSG m;
    U(GetDesktopWindow)();
    q->id = U(SetTimer)(NULL, 0, 20, NULL);
    q->ready = 1;
    DWORD end = GetTickCount() + WAIT_MS;
    while ((LONG)(end - GetTickCount()) > 0) {
        if (U(PeekMessageW)(&m, NULL, WM_TIMER, WM_TIMER, PM_REMOVE) && m.hwnd == NULL && m.wParam == q->id) { q->got = 1; break; }
        Sleep(2);
    }
    U(KillTimer)(NULL, q->id);
    return 0;
}
TEST(msg_timers_atoms_null_hwnd_timer_belongs_to_creating_thread_queue) {
    MTA_NEED();
    mta_drain_timers();
    struct tq q; memset(&q, 0, sizeof q);
    HANDLE t = CreateThread(NULL, 0, tq_thread, &q, 0, NULL);
    REQUIRE(t);
    DWORD end = GetTickCount() + 200; MSG m; int seen_here = 0;
    while ((LONG)(end - GetTickCount()) > 0) { if (U(PeekMessageW)(&m, NULL, WM_TIMER, WM_TIMER, PM_REMOVE)) seen_here++; Sleep(5); }
    CHECK(WaitForSingleObject(t, 6000) == WAIT_OBJECT_0);
    CloseHandle(t);
    MEQ(q.got, 1);
    MEQ(seen_here, 0);          /* the thread-queue timer never shows up in this thread's queue */
}

/* ---- system timers (NtUserSetSystemTimer, no user32 export) ---------------------------------------------- */
TEST(msg_timers_atoms_systimer_returns_id) {
    MTA_WIN(w);
    NtUserSetSystemTimer_pfn st = UN(NtUserSetSystemTimer);
    REQUIRE(st);
    UINT_PTR r = st(w, 0x40, 3600000);
    MEQ(r, 0x40);
    mta_destroy(w);                  /* KillTimer cannot remove it; the window dies with it */
}
TEST(msg_timers_atoms_systimer_id_zero_returns_one) {
    MTA_WIN(w);
    NtUserSetSystemTimer_pfn st = UN(NtUserSetSystemTimer);
    REQUIRE(st);
    MEQ(st(w, 0, 3600000), 1);
    mta_destroy(w);
}
TEST(msg_timers_atoms_systimer_not_killable_by_killtimer) {
    MTA_WIN(w);
    NtUserSetSystemTimer_pfn st = UN(NtUserSetSystemTimer);
    REQUIRE(st);
    if (!mta_table_ok()) { t_skip("USER handle table unavailable"); mta_destroy(w); return; }
    unsigned n0 = mta_timers();
    MEQ(st(w, 0x41, 3600000), 0x41);
    MEQ(mta_timers(), n0 + 1);
    /* NtUserKillTimer only matches timers whose flags & (TMRF_SYSTEM|TMRF_RIT) are clear */
    CHECK(!U(KillTimer)(w, 0x41));
    MEQ(mta_timers(), n0 + 1);
    mta_destroy(w);
    MEQ(mta_timers(), n0);
}
TEST(msg_timers_atoms_systimer_and_normal_timer_coexist) {
    MTA_WIN(w);
    NtUserSetSystemTimer_pfn st = UN(NtUserSetSystemTimer);
    REQUIRE(st);
    if (!mta_table_ok()) { t_skip("USER handle table unavailable"); mta_destroy(w); return; }
    unsigned n0 = mta_timers();
    MEQ(st(w, 0x42, 3600000), 0x42);
    MEQ(U(SetTimer)(w, 0x42, 3600000, NULL), 0x42);   /* InternalSetTimer's match requires equal TMRF_SYSTEM bits */
    MEQ(mta_timers(), n0 + 2);
    CHECK(U(KillTimer)(w, 0x42));                           /* removes the normal one only */
    MEQ(mta_timers(), n0 + 1);
    CHECK(!U(KillTimer)(w, 0x42));
    mta_destroy(w);
    MEQ(mta_timers(), n0);
}
TEST(msg_timers_atoms_systimer_replace_keeps_one_object) {
    MTA_WIN(w);
    NtUserSetSystemTimer_pfn st = UN(NtUserSetSystemTimer);
    REQUIRE(st);
    if (!mta_table_ok()) { t_skip("USER handle table unavailable"); mta_destroy(w); return; }
    unsigned n0 = mta_timers();
    MEQ(st(w, 7, 3600000), 7);
    MEQ(st(w, 7, 3500000), 7);
    MEQ(mta_timers(), n0 + 1);
    mta_destroy(w);
}
TEST(msg_timers_atoms_systimer_elapse_table) {
    MTA_WIN(w);
    NtUserSetSystemTimer_pfn st = UN(NtUserSetSystemTimer);
    REQUIRE(st);
    static const UINT el[] = { 0, 1, 9, 10, 0x7fffffff, 0x80000000u, 0xffffffffu };
    for (unsigned i = 0; i < sizeof el / sizeof el[0]; i++)
        CHECKF(st(w, 0x100 + i, el[i]) == 0x100 + i, "elapse 0x%x", el[i]);
    mta_destroy(w);
}
TEST(msg_timers_atoms_systimer_counts_as_user_object) {
    MTA_NEED();
    NtUserSetSystemTimer_pfn st = UN(NtUserSetSystemTimer);
    REQUIRE(st);
    DWORD b0 = mta_guires();             /* before the window: creating a window may add a hidden helper window of the thread */
    HWND w = mta_win();
    if (!w) { t_skip("cannot create a test window"); return; }
    DWORD g0 = mta_guires();
    CHECK(g0 > b0);
    MEQ(st(w, 3, 3600000), 3);
    MEQ(mta_guires(), g0 + 1);          /* the system timer is one USER object */
    mta_destroy(w);
    MEQ(mta_guires(), b0);              /* window, its helper windows and the timer are all gone */
}
TEST(msg_timers_atoms_systimer_cross_thread_window_denied) {
    MTA_NEED();
    NtUserSetSystemTimer_pfn st = UN(NtUserSetSystemTimer);
    REQUIRE(st);
    struct mta_owner o;
    if (!mta_owner_start(&o, 1, 1, 0)) { t_skip("owner thread"); return; }
    /* NtUserSetSystemTimer requires the window belongs to the calling thread, stricter than SetTimer's process test */
    SET_ERR(0);
    MEQ(st(o.hwnd, 1, 3600000), 0);
    EXPECT_ERR(ERROR_ACCESS_DENIED);
    mta_owner_stop(&o);
}
TEST(msg_timers_atoms_systimer_invalid_hwnd) {
    MTA_NEED();
    NtUserSetSystemTimer_pfn st = UN(NtUserSetSystemTimer);
    REQUIRE(st);
    static const ULONG_PTR bad[] = { 0, 1, 0xFFFF, 0x12345678, (ULONG_PTR)-1 };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SET_ERR(0);
        CHECKF(st((HWND)bad[i], 1, 1000) == 0, "hwnd 0x%llx", (unsigned long long)bad[i]);
        EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
    }
}
TEST(msg_timers_atoms_systimer_destroyed_window) {
    MTA_WIN(w);
    NtUserSetSystemTimer_pfn st = UN(NtUserSetSystemTimer);
    REQUIRE(st);
    mta_destroy(w);
    SET_ERR(0);
    MEQ(st(w, 1, 1000), 0);
    EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
}
TEST(msg_timers_atoms_systimer_delivers_wm_systimer) {
    MTA_WIN(w);
    NtUserSetSystemTimer_pfn st = UN(NtUserSetSystemTimer);
    REQUIRE(st);
    mta_reset();
    MEQ(st(w, 9, 20), 9);
    int n = mta_pump_until(0, MTA_WM_SYSTIMER, 1, WAIT_MS);
    if (!n) t_note("no WM_SYSTIMER observed within %d ms", WAIT_MS);
    else {
        MEQ(mta_log[0].wp, 9);
        MEQ(mta_count(WM_TIMER, -1), 0);   /* a TMRF_SYSTEM timer produces WM_SYSTIMER, not WM_TIMER */
    }
    mta_destroy(w);
}
