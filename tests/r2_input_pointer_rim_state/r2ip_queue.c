/* Per-thread queue state: NtUserGetQueueStatusReadonly vs NtUserGetQueueStatus vs the CLIENTTHREADINFO
 * (_CLIENTINFO.pClientThreadInfo) change/wake bits, and the bounded sleepers NtUserRealWaitMessageEx /
 * NtUserWaitAvailableMessageEx / NtUserWaitForMsgAndEvent / NtUserRealInternalGetMessage.
 *
 * win32kfull facts:
 *  NtUserGetQueueStatusReadonly(f) = DWORD at pcti+4 ({fsChangeBits, fsWakeBits}) & ((f << 16) | (f & 0xffff));
 *      it does not clear anything and does not mask f with 0x5dff.
 *  NtUserGetQueueStatus(f): f &= 0x5dff (16 bit parameter); returns LOWORD = old fsChangeBits & f,
 *      HIWORD = (fsWakeBitsJournal | fsWakeBits) & f; then fsChangeBits &= ~f.
 *  the thread sleep(mask, ms, ...): returns 1 at once if (mask & fsChangeBits) - or, for the "available"
 *      variant, also (mask & fsWakeBits); a timeout (STATUS_TIMEOUT 0x102) sets ERROR_TIMEOUT (0x5b4 = 1460)
 *      and returns 0. A pending sent message is delivered while sleeping unless bit 0x8000 is in the mask.
 *  NtUserRealWaitMessageEx rejects masks with bits in 0xffffa200 with ERROR_INVALID_FLAGS (1004).
 *  NtUserWaitForMsgAndEvent is an empty body that returns 0.
 *  NtUserRealInternalGetMessage rejects flags with bits in 0xe300fffc with 1004, else fills a 0x30 byte MSG
 *      (explicit zero at +0xc and +0x2c).
 */
#include "r2ip.h"
#include <stdlib.h>

#define HI(x) ((unsigned)((x) >> 16) & 0xFFFF)
#define LO(x) ((unsigned)(x) & 0xFFFF)

/* Emulator fallbacks (Wine's win32u has placeholders that abort for these syscalls): the same questions asked
 * through documented user32 calls, so that the area still runs to completion headless. Windows never takes them. */
static DWORD ro_impl(UINT f) {
    if (UNW(NtUserGetQueueStatusReadonly)) return UNW(NtUserGetQueueStatusReadonly)(f);
    struct r2_cti c; r2_cti_read(&c);
    return c.ok ? (DWORD)((((DWORD)c.wake << 16) | c.change) & ((f << 16) | (f & 0xFFFF))) : 0;
}
static LONG wait_impl(UINT mask, UINT ms, BOOL avail) {
    DWORD r = U(MsgWaitForMultipleObjectsEx)(0, NULL, ms, mask & 0x5DFF, avail ? MWMO_INPUTAVAILABLE : 0);
    if (r == WAIT_OBJECT_0) return 1;
    SET_ERR(ERROR_TIMEOUT);
    return 0;
}
static LONG real_wait(UINT mask, UINT ms) {
    if (UNW(NtUserRealWaitMessageEx)) return UNW(NtUserRealWaitMessageEx)(mask, ms);
    return wait_impl(mask, ms, FALSE);
}
static LONG avail_wait(UINT mask, UINT ms) {
    if (UNW(NtUserWaitAvailableMessageEx)) return UNW(NtUserWaitAvailableMessageEx)(mask, ms);
    return wait_impl(mask, ms, TRUE);
}
static LONG rigm(MSG *m, HWND h, UINT lo, UINT hi, UINT fl, UINT extra) {
    if (UNW(NtUserRealInternalGetMessage)) return UNW(NtUserRealInternalGetMessage)(m, h, lo, hi, fl, extra);
    return U(PeekMessageW)(m, h, lo, hi, fl);
}
static ULONG_PTR waitformsg(ULONG_PTR a, ULONG_PTR b, ULONG_PTR c, ULONG_PTR d) {
    if (UNW(NtUserWaitForMsgAndEvent)) return UNW(NtUserWaitForMsgAndEvent)(a, b, c, d);
    return 0;
}

/* a test whose body runs on a fresh GUI thread */
#define QT(n) \
    static void qb_##n(void); \
    RT(n) { REQUIRE(U(GetQueueStatus) && U(PostThreadMessageW) && U(PeekMessageW) && UNW(NtUserGetQueueStatus)); r2_gui_thread(qb_##n); } \
    static void qb_##n(void)


static DWORD ro(UINT f) { return ro_impl(f); }
static BOOL post_self(UINT m, WPARAM w, LPARAM l) { return U(PostThreadMessageW)(GetCurrentThreadId(), m, w, l); }
static BOOL peek(MSG *m, HWND h, UINT lo, UINT hi, UINT f) { return U(PeekMessageW)(m, h, lo, hi, f); }

/* ---- readonly vs live state -------------------------------------------------------------- */

QT(queue_ro_fresh_thread_has_no_bits) {
    EXPECT_EQ(ro(R2_QS_ALL_VALID), 0);               /* the thread starter already consumed the change bits */
    t_note("readonly(0xFFFF) on a fresh thread = 0x%lx", (unsigned long)ro(0xFFFF));
    EXPECT_EQ(ro(0), 0);
}

QT(queue_ro_post_sets_wake_and_change_bits) {
    CHECK(post_self(WM_APP + 1, 1, 2));
    DWORD r = ro(QS_POSTMESSAGE);
    EXPECT_EQ(r, 0x00080008);                         /* wake and change bit of QS_POSTMESSAGE */
    struct r2_cti c; r2_cti_read(&c);
    if (c.ok) {
        EXPECT_EQ(c.wake & QS_POSTMESSAGE, QS_POSTMESSAGE);
        EXPECT_EQ(c.change & QS_POSTMESSAGE, QS_POSTMESSAGE);
    }
    MSG m; CHECK(peek(&m, NULL, WM_APP + 1, WM_APP + 1, PM_REMOVE));
}

QT(queue_ro_equals_cti_words) {
    CHECK(post_self(WM_APP + 1, 0, 0));
    struct r2_cti c; r2_cti_read(&c);
    if (!c.ok) { t_note("client thread info not readable"); return; }
    static const UINT masks[] = { 0xFFFF, R2_QS_ALL_VALID, QS_POSTMESSAGE, QS_ALLPOSTMESSAGE, QS_POSTMESSAGE | QS_TIMER, QS_PAINT, 0x0002 };
    for (size_t i = 0; i < sizeof masks / sizeof *masks; i++) {
        DWORD r = ro(masks[i]);
        EXPECT_EQ(LO(r), c.change & masks[i]);        /* LOWORD = fsChangeBits & flags */
        EXPECT_EQ(HI(r), c.wake & masks[i]);          /* HIWORD = fsWakeBits & flags */
    }
    MSG m; peek(&m, NULL, WM_APP + 1, WM_APP + 1, PM_REMOVE);
}

QT(queue_ro_does_not_consume) {
    CHECK(post_self(WM_APP + 1, 0, 0));
    DWORD a = ro(QS_POSTMESSAGE), b = ro(QS_POSTMESSAGE), c = ro(QS_POSTMESSAGE | QS_ALLPOSTMESSAGE);
    EXPECT_EQ(a, 0x00080008);
    EXPECT_EQ(a, b);
    EXPECT_EQ(c & 0x00080008, 0x00080008);
    struct r2_cti k; r2_cti_read(&k);
    if (k.ok) EXPECT_EQ(k.change & QS_POSTMESSAGE, QS_POSTMESSAGE);   /* still set after three readonly queries */
    MSG m; peek(&m, NULL, WM_APP + 1, WM_APP + 1, PM_REMOVE);
}

QT(queue_getqueuestatus_consumes_change_bits_readonly_sees_it) {
    CHECK(post_self(WM_APP + 1, 0, 0));
    DWORD before = ro(QS_POSTMESSAGE);
    DWORD g = UNW(NtUserGetQueueStatus)(QS_POSTMESSAGE);
    EXPECT_EQ(LO(g), LO(before));                      /* GetQueueStatus returns the old change bits ... */
    EXPECT_EQ(HI(g) & QS_POSTMESSAGE, QS_POSTMESSAGE);
    DWORD after = ro(QS_POSTMESSAGE);
    EXPECT_EQ(after, 0x00080000);                      /* ... then clears them; the message is still queued */
    DWORD g2 = UNW(NtUserGetQueueStatus)(QS_POSTMESSAGE);
    EXPECT_EQ(g2, 0x00080000);
    MSG m; CHECK(peek(&m, NULL, WM_APP + 1, WM_APP + 1, PM_REMOVE));
    EXPECT_EQ(ro(QS_POSTMESSAGE), 0);
}

QT(queue_getqueuestatus_high_word_is_wake_or_journal) {
    CHECK(post_self(WM_APP + 1, 0, 0));
    struct r2_cti c; r2_cti_read(&c);
    DWORD r = ro(R2_QS_ALL_VALID);
    DWORD g = UNW(NtUserGetQueueStatus)(R2_QS_ALL_VALID);
    EXPECT_EQ(LO(g), LO(r));
    EXPECT_EQ(HI(g), HI(r) | (c.ok ? (c.journal & R2_QS_ALL_VALID) : 0));
    MSG m; peek(&m, NULL, WM_APP + 1, WM_APP + 1, PM_REMOVE);
}

QT(queue_ro_ignores_high_word_of_flags) {
    CHECK(post_self(WM_APP + 1, 0, 0));
    EXPECT_EQ(ro(0x10000 | QS_POSTMESSAGE), ro(QS_POSTMESSAGE));   /* (f << 16) | (f & 0xffff): bits 16..31 of f fall out */
    EXPECT_EQ(ro(0xFFFF0000), 0);
    EXPECT_EQ(ro(0xABCD0000 | QS_POSTMESSAGE), ro(QS_POSTMESSAGE));
    MSG m; peek(&m, NULL, WM_APP + 1, WM_APP + 1, PM_REMOVE);
}

QT(queue_getqueuestatus_ignores_high_word_of_flags) {
    CHECK(post_self(WM_APP + 1, 0, 0));
    DWORD a = UNW(NtUserGetQueueStatus)(0x10000 | QS_POSTMESSAGE);   /* the parameter is a 16 bit value in win32kfull */
    EXPECT_EQ(a, 0x00080008);
    MSG m; peek(&m, NULL, WM_APP + 1, WM_APP + 1, PM_REMOVE);
}

QT(queue_ro_flags_select_bits) {
    CHECK(post_self(WM_APP + 1, 0, 0));
    EXPECT_EQ(ro(QS_PAINT), 0);
    EXPECT_EQ(ro(QS_TIMER | QS_SENDMESSAGE | QS_KEY | QS_MOUSEMOVE), 0);
    EXPECT_EQ(ro(QS_POSTMESSAGE | QS_PAINT), 0x00080008);
    EXPECT_EQ(ro(0), 0);
    MSG m; peek(&m, NULL, WM_APP + 1, WM_APP + 1, PM_REMOVE);
}

QT(queue_ro_noremove_peek_clears_change_keeps_wake) {
    CHECK(post_self(WM_APP + 1, 0, 0));
    MSG m;
    CHECK(peek(&m, NULL, WM_APP + 1, WM_APP + 1, PM_NOREMOVE));     /* PeekMessage consumes the change bits it looked at */
    EXPECT_EQ(ro(QS_POSTMESSAGE), 0x00080000);
    CHECK(peek(&m, NULL, WM_APP + 1, WM_APP + 1, PM_REMOVE));
    EXPECT_EQ(ro(QS_POSTMESSAGE), 0);
}

QT(queue_ro_two_messages_one_wake_bit_until_last_removed) {
    CHECK(post_self(WM_APP + 1, 0, 0));
    CHECK(post_self(WM_APP + 2, 0, 0));
    MSG m;
    CHECK(peek(&m, NULL, WM_APP + 1, WM_APP + 1, PM_REMOVE));
    EXPECT_EQ(HI(ro(QS_POSTMESSAGE)), QS_POSTMESSAGE);   /* one left */
    CHECK(peek(&m, NULL, WM_APP + 2, WM_APP + 2, PM_REMOVE));
    EXPECT_EQ(HI(ro(QS_POSTMESSAGE)), 0);
}

QT(queue_post_to_window_sets_postmessage_bit) {
    HWND w = r2_window(WS_POPUP); if (!w) { t_note("no window"); return; }
    CHECK(U(PostMessageW)(w, WM_APP + 1, 0, 0));
    EXPECT_EQ(ro(QS_POSTMESSAGE), 0x00080008);
    MSG m; CHECK(peek(&m, w, WM_APP + 1, WM_APP + 1, PM_REMOVE));
    EXPECT_EQ(m.hwnd, w);
    EXPECT_EQ(ro(QS_POSTMESSAGE), 0);
    U(DestroyWindow)(w);
}

QT(queue_post_to_missing_thread_leaves_bits_alone) {
    SET_ERR(0);
    CHECK(!U(PostThreadMessageW)(0x7FFFFFF0, WM_APP + 1, 0, 0));
    EXPECT_ERR(1444);                                   /* ERROR_INVALID_THREAD_ID */
    EXPECT_EQ(ro(R2_QS_ALL_VALID), 0);
    CHECK(post_self(WM_APP + 1, 0, 0));
    MSG m; peek(&m, NULL, WM_APP + 1, WM_APP + 1, PM_REMOVE);
    EXPECT_EQ(ro(R2_QS_ALL_VALID), 0);
}

/* ---- sent messages ------------------------------------------------------------------------ */

static DWORD WINAPI notify_thread(LPVOID p) {
    HWND w = (HWND)p;
    MSG m; U(PeekMessageW)(&m, NULL, 0, 0, PM_NOREMOVE);
    U(SendNotifyMessageW)(w, WM_APP + 3, 0, 0);          /* cross-thread: queued as a sent message, no wait */
    return 0;
}

QT(queue_sendnotify_sets_sendmessage_wake_bit) {
    HWND w = r2_window(WS_POPUP); if (!w) { t_note("no window"); return; }
    r2_hits_reset();
    CHECK(r2_run_thread(notify_thread, w));
    DWORD r = ro(QS_SENDMESSAGE);
    EXPECT_EQ(HI(r), QS_SENDMESSAGE);
    struct r2_cti c; r2_cti_read(&c);
    if (c.ok) EXPECT_EQ(c.wake & QS_SENDMESSAGE, QS_SENDMESSAGE);
    EXPECT_EQ(r2_hit[3], 0);                              /* not delivered until this thread calls into the queue */
    MSG m; peek(&m, NULL, 0, 0, PM_NOREMOVE);             /* delivers pending sent messages */
    EXPECT_EQ(r2_hit[3], 1);
    EXPECT_EQ(r2_hit_tid[3], GetCurrentThreadId());
    EXPECT_EQ(HI(ro(QS_SENDMESSAGE)), 0);
    U(DestroyWindow)(w);
}

QT(queue_sendnotify_readonly_is_not_delivery) {
    HWND w = r2_window(WS_POPUP); if (!w) { t_note("no window"); return; }
    r2_hits_reset();
    CHECK(r2_run_thread(notify_thread, w));
    ro(0xFFFF); ro(QS_SENDMESSAGE);                        /* NtUserGetQueueStatusReadonly does not receive messages */
    EXPECT_EQ(r2_hit[3], 0);
    EXPECT_EQ(HI(ro(QS_SENDMESSAGE)), QS_SENDMESSAGE);
    MSG m; peek(&m, NULL, 0, 0, PM_NOREMOVE);
    EXPECT_EQ(r2_hit[3], 1);
    U(DestroyWindow)(w);
}

/* ---- timers -------------------------------------------------------------------------------- */

static int wait_ro_bit(UINT bit, int ms_total) {
    DWORD t0 = GetTickCount();
    while ((int)(GetTickCount() - t0) < ms_total) {
        if (HI(ro(bit)) & bit) return 1;
        avail_wait(bit, 50);
    }
    return HI(ro(bit)) & bit;
}

QT(queue_timer_expiry_sets_qs_timer) {
    HWND w = r2_window(WS_POPUP); if (!w) { t_note("no window"); return; }
    CHECK(U(SetTimer)(w, 7, 10, NULL) == 7);
    CHECK(wait_ro_bit(QS_TIMER, 3000));
    DWORD r = ro(QS_TIMER);
    EXPECT_EQ(HI(r), QS_TIMER);
    struct r2_cti c; r2_cti_read(&c);
    if (c.ok) EXPECT_EQ(c.wake & QS_TIMER, QS_TIMER);
    DWORD g = UNW(NtUserGetQueueStatus)(QS_TIMER);
    EXPECT_EQ(HI(g), QS_TIMER);
    MSG m; CHECK(peek(&m, w, WM_TIMER, WM_TIMER, PM_REMOVE));
    EXPECT_EQ(m.wParam, 7);
    U(KillTimer)(w, 7);
    U(DestroyWindow)(w);
}

QT(queue_timer_bit_clear_before_expiry) {
    HWND w = r2_window(WS_POPUP); if (!w) { t_note("no window"); return; }
    CHECK(U(SetTimer)(w, 8, 60000, NULL) == 8);          /* a minute: never expires within the test */
    EXPECT_EQ(ro(QS_TIMER), 0);
    U(KillTimer)(w, 8);
    U(DestroyWindow)(w);
}

/* ---- paint ---------------------------------------------------------------------------------- */

QT(queue_paint_bit_follows_update_region) {
    HWND w = r2_window(WS_POPUP); if (!w) { t_note("no window"); return; }
    U(ShowWindow)(w, SW_SHOWNOACTIVATE);
    MSG m;
    while (peek(&m, w, WM_PAINT, WM_PAINT, PM_REMOVE)) U(ValidateRect)(w, NULL);
    U(UpdateWindow)(w);
    U(InvalidateRect)(w, NULL, FALSE);
    DWORD r = ro(QS_PAINT);
    if (!(HI(r) & QS_PAINT)) { t_note("no pending paint (off-screen clipping?): 0x%lx", r); U(DestroyWindow)(w); return; }
    struct r2_cti c; r2_cti_read(&c);
    if (c.ok) EXPECT_EQ(c.wake & QS_PAINT, QS_PAINT);
    U(ValidateRect)(w, NULL);
    EXPECT_EQ(HI(ro(QS_PAINT)), 0);                       /* no update region left -> QS_PAINT wake bit cleared */
    U(DestroyWindow)(w);
}

/* ---- RealWaitMessageEx ------------------------------------------------------------------------ */

QT(wait_real_timeout_returns_zero_with_error_timeout) {
    DWORD t0 = GetTickCount();
    SET_ERR(0);
    LONG r = real_wait(0, 60);
    DWORD e = GetLastError(), dt = GetTickCount() - t0;
    EXPECT_EQ(r, 0);
    EXPECT_EQ(e, ERROR_TIMEOUT);                          /* UserSetLastError(0x5b4) on STATUS_TIMEOUT */
    CHECKF(dt >= 40 && dt < 3000, "dt=%lu", dt);
}

QT(wait_real_timeout_with_valid_masks) {
    static const UINT masks[] = { QS_KEY, QS_MOUSEMOVE, QS_POSTMESSAGE, QS_TIMER, QS_PAINT, QS_SENDMESSAGE, QS_HOTKEY, QS_ALLPOSTMESSAGE, QS_RAWINPUT, 0x1000, 0x4000 };
    for (size_t i = 0; i < sizeof masks / sizeof *masks; i++) {
        SET_ERR(0);
        LONG r = real_wait(masks[i], 20);
        CHECKF(r == 0 && GetLastError() == ERROR_TIMEOUT, "mask 0x%x r=%ld e=%lu", masks[i], r, GetLastError());
    }
}

QT(wait_real_invalid_mask_bits_rejected_at_once) {
    static const UINT bad[] = { 0x200, 0x2000, 0x8000, 0x10000, 0x20000, 0x1000000, 0x80000000u, 0xFFFF0000u, 0xA200, 0xFFFFFFFFu };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        DWORD t0 = GetTickCount();
        SET_ERR(0);
        LONG r = real_wait(bad[i], 5000);
        DWORD dt = GetTickCount() - t0;
        CHECKF(r == 0, "mask 0x%x r=%ld", bad[i], r);
        EXPECT_ERR(E_INVALID_FLAGS);
        CHECKF(dt < 1000, "mask 0x%x waited %lu ms", bad[i], dt);
    }
}

QT(wait_real_returns_at_once_for_set_change_bit) {
    CHECK(post_self(WM_APP + 1, 0, 0));
    DWORD t0 = GetTickCount();
    SET_ERR(0);
    LONG r = real_wait(QS_POSTMESSAGE, 5000);
    DWORD dt = GetTickCount() - t0;
    EXPECT_EQ(r, 1);
    CHECKF(dt < 1000, "dt=%lu", dt);
    r = real_wait(QS_POSTMESSAGE, 5000);   /* waiting does not consume the change bit */
    EXPECT_EQ(r, 1);
    MSG m; peek(&m, NULL, WM_APP + 1, WM_APP + 1, PM_REMOVE);
}

QT(wait_real_ignores_wake_bits_without_change) {
    CHECK(post_self(WM_APP + 1, 0, 0));
    UNW(NtUserGetQueueStatus)(QS_POSTMESSAGE);                   /* change bit gone, message still queued */
    EXPECT_EQ(ro(QS_POSTMESSAGE), 0x00080000);
    DWORD t0 = GetTickCount();
    SET_ERR(0);
    LONG r = real_wait(QS_POSTMESSAGE, 80);
    DWORD dt = GetTickCount() - t0;
    EXPECT_EQ(r, 0);                                            /* fInputMask == 0: only the change bits count */
    EXPECT_ERR(ERROR_TIMEOUT);
    CHECKF(dt >= 50, "dt=%lu", dt);
    MSG m; peek(&m, NULL, WM_APP + 1, WM_APP + 1, PM_REMOVE);
}

/* ---- WaitAvailableMessageEx --------------------------------------------------------------------- */

QT(wait_available_sees_wake_bits_without_change) {
    CHECK(post_self(WM_APP + 1, 0, 0));
    UNW(NtUserGetQueueStatus)(QS_POSTMESSAGE);
    DWORD t0 = GetTickCount();
    SET_ERR(0);
    LONG r = avail_wait(QS_POSTMESSAGE, 5000);
    DWORD dt = GetTickCount() - t0;
    EXPECT_EQ(r, 1);                                            /* fInputMask == 1: wake bits count as available input */
    CHECKF(dt < 1000, "dt=%lu", dt);
    EXPECT_EQ(ro(QS_POSTMESSAGE), 0x00080000);                  /* and it consumed nothing */
    MSG m; peek(&m, NULL, WM_APP + 1, WM_APP + 1, PM_REMOVE);
}

QT(wait_available_timeout_returns_zero_with_error_timeout) {
    DWORD t0 = GetTickCount();
    SET_ERR(0);
    LONG r = avail_wait(0, 100);
    DWORD e = GetLastError(), dt = GetTickCount() - t0;
    EXPECT_EQ(r, 0);
    EXPECT_EQ(e, ERROR_TIMEOUT);
    CHECKF(dt >= 70 && dt < 3000, "dt=%lu", dt);
}

QT(wait_available_does_not_validate_mask) {
    static const UINT odd[] = { 0x200, 0x2000, 0x10000, 0x80000000u };
    for (size_t i = 0; i < sizeof odd / sizeof *odd; i++) {
        SET_ERR(0);
        LONG r = avail_wait(odd[i], 20);
        CHECKF(r == 0 && GetLastError() == ERROR_TIMEOUT, "mask 0x%x r=%ld e=%lu", odd[i], r, GetLastError());   /* no 1004 here */
    }
}

QT(wait_available_empty_queue_timeout_leaves_bits_clear) {
    avail_wait(R2_QS_ALL_VALID & ~QS_SENDMESSAGE, 30);
    EXPECT_EQ(ro(R2_QS_ALL_VALID), 0);
}

/* ---- wake-ups from another thread ------------------------------------------------------------------ */

struct poster { DWORD tid; UINT delay; UINT msg; };
static DWORD WINAPI post_later(LPVOID p) {
    struct poster *s = p;
    Sleep(s->delay);
    U(PostThreadMessageW)(s->tid, s->msg, 0, 0);
    return 0;
}

QT(wait_real_is_woken_by_foreign_post) {
    struct poster s = { GetCurrentThreadId(), 80, WM_APP + 1 };
    HANDLE th = CreateThread(NULL, 0, post_later, &s, 0, NULL);
    CHECK(th != NULL); if (!th) return;
    DWORD t0 = GetTickCount();
    SET_ERR(0);
    LONG r = real_wait(QS_POSTMESSAGE, 5000);
    DWORD dt = GetTickCount() - t0;
    EXPECT_EQ(r, 1);
    CHECKF(dt >= 50 && dt < 3000, "dt=%lu", dt);
    WaitForSingleObject(th, 5000); CloseHandle(th);
    MSG m; CHECK(peek(&m, NULL, WM_APP + 1, WM_APP + 1, PM_REMOVE));
}

QT(wait_real_wake_mask_is_restored_and_tick_updated) {
    struct r2_cti a; r2_cti_read(&a);
    struct poster s = { GetCurrentThreadId(), 40, WM_APP + 1 };
    HANDLE th = CreateThread(NULL, 0, post_later, &s, 0, NULL);
    CHECK(th != NULL); if (!th) return;
    LONG r = real_wait(QS_POSTMESSAGE, 5000);
    EXPECT_EQ(r, 1);
    struct r2_cti b; r2_cti_read(&b);
    if (a.ok && b.ok) {
        EXPECT_EQ(b.wakemask, a.wakemask);                   /* the sleep restores fsWakeMask on exit */
        DWORD now = GetTickCount();
        DWORD d = now - b.tick;
        CHECKF(b.tick != 0 && d < 2000, "timeLastRead %lu now %lu", b.tick, now);   /* stamped with the ms tick count on a successful wake */
    } else t_note("client thread info not readable");
    WaitForSingleObject(th, 5000); CloseHandle(th);
    MSG m; peek(&m, NULL, WM_APP + 1, WM_APP + 1, PM_REMOVE);
}

QT(wait_real_timeout_restores_wake_mask) {
    struct r2_cti a, b; r2_cti_read(&a);
    real_wait(QS_POSTMESSAGE | QS_PAINT, 30);
    r2_cti_read(&b);
    if (a.ok && b.ok) EXPECT_EQ(b.wakemask, a.wakemask);
    else t_note("client thread info not readable");
}

static DWORD WINAPI send_later(LPVOID p) {
    HWND w = (HWND)p;
    MSG m; U(PeekMessageW)(&m, NULL, 0, 0, PM_NOREMOVE);
    Sleep(60);
    DWORD_PTR res = 0;
    U(SendMessageTimeoutW)(w, WM_APP + 4, 0, 0, SMTO_ABORTIFHUNG, 4000, &res);
    return (DWORD)res;
}
QT(wait_real_delivers_sent_messages_while_sleeping) {

    HWND w = r2_window(WS_POPUP); if (!w) { t_note("no window"); return; }
    r2_hits_reset();
    HANDLE th = CreateThread(NULL, 0, send_later, w, 0, NULL);
    CHECK(th != NULL); if (!th) { U(DestroyWindow)(w); return; }
    LONG r = real_wait(0, 700);              /* mask lacks 0x8000: incoming sent-message processing runs inside the sleep */
    int hits_at_return = (int)r2_hit[4];
    t_note("RealWaitMessageEx(0,700) = %ld, sent messages delivered during the wait: %d", r, hits_at_return);
    EXPECT_EQ(hits_at_return, 1);
    EXPECT_EQ(r2_hit_tid[4], GetCurrentThreadId());
    MSG m; peek(&m, NULL, 0, 0, PM_NOREMOVE);
    WaitForSingleObject(th, 6000);
    DWORD code = 0; GetExitCodeThread(th, &code);
    EXPECT_EQ(code, 0x5151);                                   /* the sender got our window procedure's result */
    CloseHandle(th);
    U(DestroyWindow)(w);
}

/* ---- WaitForMsgAndEvent --------------------------------------------------------------------------- */

QT(wait_formsgandevent_is_an_immediate_noop) {
    for (int i = 0; i < 3; i++) {
        DWORD t0 = GetTickCount();
        SET_ERR(0x1234);
        ULONG_PTR r = waitformsg(0, 0, 0, 0);
        EXPECT_EQ(r, 0);
        CHECK(GetTickCount() - t0 < 500);
        r = waitformsg(1, (ULONG_PTR)GetCurrentProcess(), 0xFFFFFFFF, QS_ALLINPUT);   /* arguments are never read */
        EXPECT_EQ(r, 0);
        CHECK(GetTickCount() - t0 < 500);
    }
    EXPECT_EQ(ro(0xFFFF), 0);
}

/* ---- RealInternalGetMessage --------------------------------------------------------------------------- */

QT(rigm_invalid_flag_bits_rejected) {
    static const UINT bad[] = { 0x4, 0x8, 0x10, 0x100, 0x8000, 0x1000000, 0x2000000, 0x20000000, 0x40000000, 0x80000000u, 0xFFFFFFFFu };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        MSG m; memset(&m, 0xCC, sizeof m);
        SET_ERR(0);
        rigm(&m, NULL, 0, 0, bad[i], 0);
        EXPECT_ERR(E_INVALID_FLAGS);
        CHECKF(((unsigned char *)&m)[0] == 0xCC && ((unsigned char *)&m)[47] == 0xCC, "flags 0x%x wrote the MSG", bad[i]);
    }
}

QT(rigm_retrieves_posted_message) {
    CHECK(post_self(WM_APP + 5, 0x1111, 0x2222));
    MSG m; memset(&m, 0xCC, sizeof m);
    LONG r = rigm(&m, NULL, WM_APP + 5, WM_APP + 5, PM_REMOVE, 0);
    t_note("RealInternalGetMessage returned %ld", r);
    EXPECT_EQ(m.message, WM_APP + 5);
    EXPECT_EQ(m.wParam, 0x1111);
    EXPECT_EQ(m.lParam, 0x2222);
    EXPECT_EQ(m.hwnd, NULL);
    EXPECT_EQ(*(DWORD *)((unsigned char *)&m + 12), 0);        /* explicit zero padding at +0xc */
    EXPECT_EQ(*(DWORD *)((unsigned char *)&m + 44), 0);        /* and lPrivate at +0x2c */
    EXPECT_EQ(ro(QS_POSTMESSAGE) >> 16 & QS_POSTMESSAGE, 0);   /* PM_REMOVE took it out of the queue */
}

QT(rigm_noremove_leaves_message_queued) {
    CHECK(post_self(WM_APP + 5, 7, 8));
    MSG m; memset(&m, 0xCC, sizeof m);
    rigm(&m, NULL, WM_APP + 5, WM_APP + 5, PM_NOREMOVE, 0);
    EXPECT_EQ(m.message, WM_APP + 5);
    EXPECT_EQ(HI(ro(QS_POSTMESSAGE)), QS_POSTMESSAGE);
    CHECK(peek(&m, NULL, WM_APP + 5, WM_APP + 5, PM_REMOVE));
}

QT(rigm_range_filter_picks_matching_message) {
    CHECK(post_self(WM_APP + 1, 0, 0));
    CHECK(post_self(WM_APP + 2, 0, 0));
    MSG m; memset(&m, 0xCC, sizeof m);
    rigm(&m, NULL, WM_APP + 2, WM_APP + 2, PM_REMOVE, 0);
    EXPECT_EQ(m.message, WM_APP + 2);
    CHECK(peek(&m, NULL, 0, 0, PM_NOREMOVE));
    EXPECT_EQ(m.message, WM_APP + 1);                         /* the earlier message is still queued */
    peek(&m, NULL, 0, 0, PM_REMOVE);
}

QT(rigm_hwnd_filter) {
    HWND w = r2_window(WS_POPUP); if (!w) { t_note("no window"); return; }
    CHECK(post_self(WM_APP + 1, 0, 0));
    CHECK(U(PostMessageW)(w, WM_APP + 1, 1, 1));
    MSG m; memset(&m, 0xCC, sizeof m);
    rigm(&m, w, 0, 0, PM_REMOVE, 0);
    EXPECT_EQ(m.hwnd, w);
    EXPECT_EQ(m.wParam, 1);
    CHECK(peek(&m, NULL, WM_APP + 1, WM_APP + 1, PM_REMOVE));
    EXPECT_EQ(m.hwnd, NULL);
    U(DestroyWindow)(w);
}
