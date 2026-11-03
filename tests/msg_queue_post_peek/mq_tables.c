/* Table-driven sweeps over the posted-message list: filters, PM_QS_* masks, status flag isolation,
 * bulk ordering.  Expectations follow observed wake-mask and message-retrieval behavior:
 *  - min == max == 0 selects everything; max == 0 alone means "no upper bound";
 *  - PM_QS_* bits containing POSTMESSAGE/HOTKEY/TIMER (0x98) enable all three posted-class sources,
 *    other bits enable only their own source.
 */
#include "mq.h"

#define B (WM_USER + 0x300)

QTEST(msg_queue_post_peek_table_range_filter_matrix) {
    static const struct { UINT lo, hi; } rg[] = {
        { 0, 0 }, { B, B }, { B + 1, B + 3 }, { B + 3, B + 5 }, { 0, B + 2 }, { B + 4, 0 }, { B + 6, B + 9 },
        { 0, B }, { B + 5, B + 5 }, { WM_USER, B + 5 }, { B + 2, 0xFFFF }, { 1, 0xFFFF }, { 0, 0xFFFF }, { B, 0 },
    };
    HWND h = mq_win(); QNEED(h);
    for (unsigned i = 0; i < sizeof rg / sizeof rg[0]; i++) {
        for (int k = 0; k < 6; k++) CHECK(mq_post(h, B + k, k, 0));
        int want = 0;
        for (int k = 0; k < 6; k++) {
            UINT m = B + k;
            int in = (rg[i].lo == 0 && rg[i].hi == 0) || (m >= rg[i].lo && (rg[i].hi == 0 || m <= rg[i].hi));
            want += in;
        }
        int got = 0; MSG m; int ordered = 1; UINT prev = 0;
        while (mq_peek(&m, h, rg[i].lo, rg[i].hi, PM_REMOVE)) {
            if (m.message < prev) ordered = 0;
            prev = m.message; got++;
            CHECKF(m.message >= B && m.message < B + 6, "unexpected id 0x%x", m.message);
        }
        CHECKF(got == want, "range [0x%x,0x%x]: %d retrieved, want %d", rg[i].lo, rg[i].hi, got, want);
        CHECK(ordered);
        EQ(mq_drain(), 6 - want);
    }
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_table_pm_qs_posted_class_matrix) {
    /* QS_TIMER or QS_HOTKEY in the mask pull in posted messages too; INPUT/PAINT/SEND alone do not. */
    static const UINT qs[] = { 0, QS_POSTMESSAGE, QS_TIMER, QS_HOTKEY, QS_POSTMESSAGE | QS_TIMER | QS_HOTKEY, QS_KEY, QS_MOUSEMOVE, QS_MOUSEBUTTON, QS_INPUT, QS_PAINT, QS_SENDMESSAGE, QS_PAINT | QS_SENDMESSAGE, QS_INPUT | QS_PAINT, QS_INPUT | QS_TIMER, QS_SENDMESSAGE | QS_HOTKEY };
    HWND h = mq_win(); QNEED(h);
    CHECK(mq_post(h, B, 0, 0));
    for (unsigned i = 0; i < sizeof qs / sizeof qs[0]; i++) {
        MSG m;
        int want = qs[i] == 0 || (qs[i] & (QS_POSTMESSAGE | QS_TIMER | QS_HOTKEY)) != 0;
        int got = mq_peek(&m, h, 0, 0, PM_NOREMOVE | (qs[i] << 16));
        CHECKF(!!got == want, "PM_QS 0x%x: found=%d want=%d", qs[i], got, want);
    }
    EQ(mq_drain(), 1);
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_table_status_flag_isolation) {
    static const UINT fl[] = { QS_KEY, QS_MOUSEMOVE, QS_MOUSEBUTTON, QS_POSTMESSAGE, QS_TIMER, QS_PAINT, QS_SENDMESSAGE, QS_HOTKEY, QS_ALLPOSTMESSAGE, QS_RAWINPUT, QS_POSTMESSAGE | QS_TIMER, QS_ALLINPUT, QS_INPUT, QS_ALLEVENTS };
    HWND h = mq_win(); QNEED(h);
    for (unsigned i = 0; i < sizeof fl / sizeof fl[0]; i++) {
        CHECK(mq_post(h, B, 0, 0));
        DWORD s = mq_status(fl[i]);
        CHECKF(((s & 0xFFFF) & ~fl[i]) == 0 && (((s >> 16) & 0xFFFF) & ~fl[i]) == 0, "flags 0x%x result 0x%lx has foreign bits", fl[i], s);
        int has = (fl[i] & QS_POSTMESSAGE) != 0;
        CHECKF((((s >> 16) & QS_POSTMESSAGE) != 0) == has, "flags 0x%x result 0x%lx", fl[i], s);
        CHECKF((((s & 0xFFFF) & QS_POSTMESSAGE) != 0) == has, "flags 0x%x result 0x%lx", fl[i], s);
        mq_drain();
        mq_status(0x5DFF);          /* reset the change bits for the next row */
    }
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_table_thread_message_values) {
    static const struct { UINT m; WPARAM w; LPARAM l; } v[] = {
        { WM_USER, 0, 0 }, { WM_USER + 1, 1, -1 }, { WM_APP, (WPARAM)-1, 0 }, { 0xC001, 0x8000000000000000ull, 0x7FFFFFFFFFFFFFFFll },
        { 0xBFFF, 0xFFFFFFFFull, 0xFFFFFFFFll }, { WM_NULL, 0, 0 }, { WM_USER + 100, 0xDEADBEEFCAFEF00Dull, 0 },
    };
    DWORD tid = GetCurrentThreadId();
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) {
        MSG m;
        CHECKF(U(PostThreadMessageW)(tid, v[i].m, v[i].w, v[i].l), "row %u", i);
        CHECKF(mq_peek(&m, NULL, 0, 0, PM_REMOVE), "row %u", i);
        CHECK(m.hwnd == NULL);
        EQ(m.message, v[i].m);
        EQ(m.wParam, v[i].w);
        EQ(m.lParam, v[i].l);
    }
}

QTEST(msg_queue_post_peek_table_direct_post_thread_message_values) {
    QNEED(UN(NtUserPostThreadMessage));
    static const WPARAM w[] = { 0, 1, 0xFFFFFFFF, 0x100000000ull, (WPARAM)-1 };
    for (unsigned i = 0; i < sizeof w / sizeof w[0]; i++) {
        MSG m;
        EQ(UN(NtUserPostThreadMessage)(GetCurrentThreadId(), B + i, w[i], ~w[i]), 1);
        CHECK(mq_peek(&m, NULL, B + i, B + i, PM_REMOVE));
        EQ(m.wParam, w[i]);
        EQ(m.lParam, (LPARAM)~w[i]);
    }
}

QTEST(msg_queue_post_peek_bulk_posts_keep_order_and_pending_bit) {
    HWND h = mq_win(); QNEED(h);
    enum { N = 600 };
    for (int i = 0; i < N; i++) CHECK(mq_post(h, B + (i % 7), i, i * 3));
    int bad = 0;
    for (int i = 0; i < N; i++) {
        MSG m;
        if (!mq_peek(&m, h, 0, 0, PM_REMOVE) || m.wParam != (WPARAM)i || m.lParam != (LPARAM)(i * 3) || m.message != (UINT)(B + (i % 7))) { bad++; break; }
        if (i == N - 2) CHECK(((mq_status(QS_POSTMESSAGE) >> 16) & QS_POSTMESSAGE) != 0);
    }
    EQ(bad, 0);
    EQ(mq_status(QS_POSTMESSAGE) >> 16 & QS_POSTMESSAGE, 0);
    U(DestroyWindow)(h);
}

QTEST(msg_queue_post_peek_bulk_interleaved_windows_filter_each) {
    HWND w[3] = { mq_win(), mq_win(), mq_win() };
    for (int i = 0; i < 3; i++) QNEED(w[i]);
    for (int i = 0; i < 90; i++) CHECK(mq_post(w[i % 3], B, i, 0));
    for (int k = 2; k >= 0; k--) {            /* drain the windows in a different order than they were fed */
        int n = 0, ordered = 1; MSG m; WPARAM prev = 0;
        while (mq_peek(&m, w[k], 0, 0, PM_REMOVE)) { if (n && m.wParam <= prev) ordered = 0; prev = m.wParam; CHECK(m.hwnd == w[k]); CHECK(m.wParam % 3 == (WPARAM)k); n++; }
        EQ(n, 30);
        CHECK(ordered);
    }
    for (int i = 0; i < 3; i++) U(DestroyWindow)(w[i]);
}

QTEST(msg_queue_post_peek_table_post_ids_with_exact_filter) {
    static const UINT ids[] = { 0, 1, 0x10, 0x7FF, WM_USER - 1, WM_USER, WM_USER + 1, 0x7FFF, 0x8000, 0xBFFF, 0xC000, 0xFFFF };
    HWND h = mq_win(); QNEED(h);
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        UINT id = ids[i];
        if (id == 0x10 || id == 1 || id == 0x7FF || id == WM_USER - 1) {
            /* ids below WM_USER are system messages: only attempt the harmless ones */
            if (id != 0x7FF && id != WM_USER - 1) continue;
        }
        MSG m;
        if (!mq_post(h, id, 0, 0)) { t_note("post of 0x%x rejected (error %lu)", id, GetLastError()); continue; }
        CHECKF(mq_peek(&m, h, id, id, PM_REMOVE), "id 0x%x", id);
        EQ(m.message, id);
        CHECK(!mq_peek(&m, h, 0, 0, PM_NOREMOVE));
    }
    U(DestroyWindow)(h);
}
