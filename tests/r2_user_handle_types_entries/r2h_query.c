/* NtUserQueryWindow and handle-value algebra: how win32kbase!ValidateHwndEx / user32!IsWindow
 * turn a 64-bit HWND into a table entry (index = bits 0..15, uniq = bits 16..31, bits 32..63
 * never looked at), and what the kernel-side window query reports for owner thread/process. */
#include "r2h.h"

UDECL(ULONG_PTR, NtUserQueryWindow, (HWND, DWORD));
#define QW(h, i) UN(NtUserQueryWindow)((HWND)(h), (DWORD)(i))

/* win32kfull!NtUserQueryWindow: 0 -> PsGetThreadProcessId (pid), 1 -> same, 2 -> PsGetThreadId (tid). */
R2(query_index0_is_process_id) {
    R2H_NEED(); REQUIRE(UN(NtUserQueryWindow));
    HWND w = r2h_make_top(); REQUIRE(w);
    EXPECT_EQ(QW(w, 0), GetCurrentProcessId());
    U(DestroyWindow)(w);
}
R2(query_index1_is_process_id) {
    R2H_NEED(); REQUIRE(UN(NtUserQueryWindow));
    HWND w = r2h_make_top(); REQUIRE(w);
    EXPECT_EQ(QW(w, 1), GetCurrentProcessId());
    U(DestroyWindow)(w);
}
R2(query_index2_is_thread_id) {
    R2H_NEED(); REQUIRE(UN(NtUserQueryWindow));
    HWND w = r2h_make_top(); REQUIRE(w);
    EXPECT_EQ(QW(w, 2), GetCurrentThreadId());
    U(DestroyWindow)(w);
}
/* the handle allocator stores PsGetThreadId of the creating thread in the client entry; NtUserQueryWindow(2)
 * returns PsGetThreadId(pwnd->head.pti->pEThread): the two must agree. */
R2(query_index2_equals_table_owner) {
    R2H_NEED(); REQUIRE(UN(NtUserQueryWindow));
    HWND w = r2h_make_top(); REQUIRE(w);
    r2h_entry_t e;
    CHECK(r2h_read_h(w, &e));
    EXPECT_EQ(e.owner, QW(w, 2));
    U(DestroyWindow)(w);
}
/* user32!GetWindowThreadProcessId reads the owner straight from the entry for own-thread windows
 * and asks NtUserQueryWindow(0)/(2) otherwise. */
R2(query_matches_getwindowthreadprocessid_own_thread) {
    R2H_NEED(); REQUIRE(UN(NtUserQueryWindow));
    HWND w = r2h_make_top(); REQUIRE(w);
    DWORD pid = 0;
    DWORD tid = U(GetWindowThreadProcessId)(w, &pid);
    EXPECT_EQ(tid, QW(w, 2));
    EXPECT_EQ(pid, QW(w, 0));
    U(DestroyWindow)(w);
}
R2(query_foreign_thread_window_owner) {
    R2H_NEED(); REQUIRE(UN(NtUserQueryWindow));
    struct r2h_gw g;
    if (!r2h_gw_start(&g, NULL)) { r2h_gw_stop(&g); t_skip("worker window not created"); return; }
    DWORD pid = 0;
    DWORD tid = U(GetWindowThreadProcessId)(g.w, &pid); /* owner != current thread: NtUserQueryWindow(0) and (2) */
    EXPECT_EQ(tid, g.tid);
    EXPECT_EQ(pid, GetCurrentProcessId());
    EXPECT_EQ(QW(g.w, 2), g.tid);
    EXPECT_EQ(QW(g.w, 0), GetCurrentProcessId());
    r2h_entry_t e;
    CHECK(r2h_read_h(g.w, &e));
    EXPECT_EQ(e.owner, g.tid);
    r2h_gw_stop(&g);
}
R2(query_foreign_thread_window_destroy_denied_keeps_entry) {
    R2H_NEED();
    struct r2h_gw g;
    if (!r2h_gw_start(&g, NULL)) { r2h_gw_stop(&g); t_skip("worker window not created"); return; }
    r2h_entry_t e0, e1;
    CHECK(r2h_read_h(g.w, &e0));
    BOOL r; R2H_CALL_ERR(r, U(DestroyWindow)(g.w)); /* DestroyWindow refuses a window of another thread */
    CHECK(!r);
    EXPECT_ERR(5);
    CHECK(r2h_read_h(g.w, &e1));
    EXPECT_EQ(e1.type, UT_WINDOW); EXPECT_EQ(e1.uniq, e0.uniq); CHECK((e1.flags & 1) == 0);
    CHECK(U(IsWindow)(g.w));
    r2h_gw_stop(&g);
}

/* ValidateHwnd succeeds for the live window, then switch default arm -> 0 (no error set). */
R2(query_invalid_index_returns_zero) {
    R2H_NEED(); REQUIRE(UN(NtUserQueryWindow));
    HWND w = r2h_make_top(); REQUIRE(w);
    static const DWORD idx[] = { 6, 10, 11, 12, 16, 0x20, 0x100, 0x7fffffff, 0x80000000u, 0xffffffffu };
    for (unsigned i = 0; i < sizeof idx / sizeof idx[0]; i++) {
        ULONG_PTR r = QW(w, idx[i]);
        CHECKF(r == 0, "index 0x%lx returned 0x%llx", idx[i], (unsigned long long)r);
    }
    U(DestroyWindow)(w);
}
/* ValidateHwnd -> ValidateHwndEx fails -> UserSetLastError(0x578) and the query returns 0 for every index. */
static void bad_handle_all_indices(HWND h, const char *what) {
    static const DWORD idx[] = { 0, 1, 2, 3, 4, 5, 7, 8, 9 };
    for (unsigned i = 0; i < sizeof idx / sizeof idx[0]; i++) {
        ULONG_PTR r; R2H_CALL_ERR(r, QW(h, idx[i]));
        CHECKF(r == 0, "%s: index %lu returned 0x%llx", what, idx[i], (unsigned long long)r);
        DWORD e = GetLastError();
        CHECKF(e == 0x578, "%s: index %lu last error %lu want 1400", what, idx[i], e);
    }
}
R2(query_null_window) { R2H_NEED(); REQUIRE(UN(NtUserQueryWindow)); bad_handle_all_indices(NULL, "NULL"); }
R2(query_forged_uniq_window) {
    R2H_NEED(); REQUIRE(UN(NtUserQueryWindow));
    HWND w = r2h_make_top(); REQUIRE(w);
    unsigned u = R2H_UNIQ(w), o = (u + 1) & 0xffff; if (o == 0 || o == 0xffff) o = 2;
    bad_handle_all_indices((HWND)r2h_forge(w, o), "forged uniq");
    U(DestroyWindow)(w);
}
/* ValidateHwndEx: native (non-WOW64) callers need uniq == entry uniq or 0xFFFF; uniq 0 is only accepted for WOW64. */
R2(query_zero_uniq_rejected_native) {
    R2H_NEED(); REQUIRE(UN(NtUserQueryWindow));
    if (sizeof(void *) != 8) { t_skip("native 64-bit only"); return; }
    HWND w = r2h_make_top(); REQUIRE(w);
    bad_handle_all_indices((HWND)r2h_forge(w, 0), "zero uniq");
    U(DestroyWindow)(w);
}
R2(query_wildcard_uniq_accepted) {
    R2H_NEED(); REQUIRE(UN(NtUserQueryWindow));
    HWND w = r2h_make_top(); REQUIRE(w);
    HWND wc = (HWND)r2h_forge(w, 0xffff);
    EXPECT_EQ(QW(wc, 2), GetCurrentThreadId());
    EXPECT_EQ(QW(wc, 0), GetCurrentProcessId());
    U(DestroyWindow)(w);
}
R2(query_destroyed_window_rejected) {
    R2H_NEED(); REQUIRE(UN(NtUserQueryWindow));
    HWND w = r2h_make_top(); REQUIRE(w);
    U(DestroyWindow)(w);
    bad_handle_all_indices(w, "destroyed");
}
R2(query_free_entry_rejected) {
    R2H_NEED(); REQUIRE(UN(NtUserQueryWindow));
    unsigned idx, uq;
    if (!r2h_free_slot(&idx, &uq, -1)) { t_skip("no free entry"); return; }
    bad_handle_all_indices((HWND)(ULONG_PTR)(idx | (uq << 16)), "free entry"); /* entry type 0 != TYPE_WINDOW */
}
R2(query_index_beyond_count_rejected) {
    R2H_NEED(); REQUIRE(UN(NtUserQueryWindow));
    bad_handle_all_indices((HWND)(ULONG_PTR)(0xffffu | (1u << 16)), "index 0xFFFF"); /* cHandleEntries <= 0xFFFE */
    unsigned long long n = r2h_n();
    if (n && n < 0xffff) bad_handle_all_indices((HWND)(ULONG_PTR)((unsigned)n | (1u << 16)), "index == cHandleEntries");
}
/* Non-window handle types: the entry type is compared with TYPE_WINDOW (1). */
R2(query_other_object_types_rejected) {
    R2H_NEED(); REQUIRE(UN(NtUserQueryWindow));
    const struct r2h_desc *const K[] = { &r2h_d_menu, &r2h_d_icon, &r2h_d_hook, &r2h_d_accel, &r2h_d_hdwp, &r2h_d_winevent };
    for (unsigned i = 0; i < sizeof K / sizeof K[0]; i++) {
        HANDLE h = K[i]->create();
        if (!h) { t_note("cannot create %s", K[i]->name); continue; }
        bad_handle_all_indices((HWND)h, K[i]->name);
        K[i]->destroy(h);
    }
}
/* Bits 32..63 of the handle are not part of the lookup: IsWindow / ValidateHwndEx slice (short)(h >> 16) and h & 0xffff. */
static const ULONG_PTR hi_bits[] = { 1ull << 32, 0xdeadbeefull << 32, 0xffffffffull << 32, 0x80000000ull << 32 };
R2(handle_high_bits_ignored_by_iswindow) {
    R2H_NEED();
    HWND w = r2h_make_top(); REQUIRE(w);
    for (unsigned i = 0; i < sizeof hi_bits / sizeof hi_bits[0]; i++) {
        HWND g = (HWND)(((ULONG_PTR)w & 0xffffffffull) | hi_bits[i]);
        CHECKF(U(IsWindow)(g), "high bits 0x%llx", (unsigned long long)hi_bits[i]);
    }
    U(DestroyWindow)(w);
}
R2(handle_high_bits_ignored_by_query) {
    R2H_NEED(); REQUIRE(UN(NtUserQueryWindow));
    HWND w = r2h_make_top(); REQUIRE(w);
    for (unsigned i = 0; i < sizeof hi_bits / sizeof hi_bits[0]; i++) {
        HWND g = (HWND)(((ULONG_PTR)w & 0xffffffffull) | hi_bits[i]);
        ULONG_PTR r = QW(g, 2);
        CHECKF(r == GetCurrentThreadId(), "high bits 0x%llx -> 0x%llx", (unsigned long long)hi_bits[i], (unsigned long long)r);
    }
    U(DestroyWindow)(w);
}
R2(handle_high_bits_ignored_by_getwindowthreadprocessid) {
    R2H_NEED();
    HWND w = r2h_make_top(); REQUIRE(w);
    HWND g = (HWND)(((ULONG_PTR)w & 0xffffffffull) | (0x1234ull << 32));
    DWORD pid = 0;
    EXPECT_EQ(U(GetWindowThreadProcessId)(g, &pid), GetCurrentThreadId());
    EXPECT_EQ(pid, GetCurrentProcessId());
    U(DestroyWindow)(w);
}
/* The same slice applies after the window is gone: a stale handle with garbage high bits stays stale. */
R2(handle_high_bits_do_not_resurrect_stale) {
    R2H_NEED();
    HWND w = r2h_make_top(); REQUIRE(w);
    U(DestroyWindow)(w);
    HWND g = (HWND)(((ULONG_PTR)w & 0xffffffffull) | (0x7fffull << 32));
    CHECK(!U(IsWindow)(g));
}
/* the handle allocator returns (int)(uniq << 16 | index): handles are sign-extended 32-bit values. */
R2(handle_value_fits_in_32_bits_sign_extended) {
    R2H_NEED();
    HWND w = r2h_make_top(); REQUIRE(w);
    unsigned long long v = (unsigned long long)(ULONG_PTR)w;
    EXPECT_EQ(v, (unsigned long long)(long long)(int)(unsigned)v);
    U(DestroyWindow)(w);
}

/* Queue-state indices read pointer fields of the owner thread's queue and return the tagWND's handle. */
R2(query_active_focus_return_window_or_zero) {
    R2H_NEED(); REQUIRE(UN(NtUserQueryWindow));
    HWND w = r2h_make_top(); REQUIRE(w);
    for (int i = 3; i <= 4; i++) {
        ULONG_PTR r = QW(w, i);
        CHECKF(r == 0 || U(IsWindow)((HWND)r), "index %d returned 0x%llx", i, (unsigned long long)r);
    }
    ULONG_PTR a = QW(w, 3), f = QW(w, 4);
    if (a != (ULONG_PTR)U(GetActiveWindow)()) t_note("QueryWindow(3)=0x%llx GetActiveWindow=%p", (unsigned long long)a, (void *)U(GetActiveWindow)());
    if (f != (ULONG_PTR)U(GetFocus)()) t_note("QueryWindow(4)=0x%llx GetFocus=%p", (unsigned long long)f, (void *)U(GetFocus)());
    U(DestroyWindow)(w);
}
/* Index 7 is (queue == gpqForeground): a hidden non-activating popup of a thread that never took the foreground. */
R2(query_foreground_flag_false_for_hidden_window) {
    R2H_NEED(); REQUIRE(UN(NtUserQueryWindow));
    HWND fg = U(GetForegroundWindow)();
    if (fg && U(GetWindowThreadProcessId)(fg, NULL) == GetCurrentThreadId()) { t_skip("test thread owns the foreground"); return; }
    HWND w = r2h_make_top(); REQUIRE(w);
    EXPECT_EQ(QW(w, 7), 0);
    U(DestroyWindow)(w);
}
R2(query_hung_flag_for_fresh_window) {
    R2H_NEED(); REQUIRE(UN(NtUserQueryWindow));
    HWND w = r2h_make_top(); REQUIRE(w);
    ULONG_PTR r = QW(w, 5);
    if (r) t_note("fresh window reported hung by QueryWindow(5): %llu", (unsigned long long)r);
    if ((r != 0) != !!U(IsHungAppWindow)(w)) t_note("IsHungAppWindow disagrees with QueryWindow(5)=%llu", (unsigned long long)r);
    U(DestroyWindow)(w);
}
/* The desktop window belongs to a system thread; the kernel query and the table agree on it. */
R2(query_desktop_window_owner_matches_table) {
    R2H_NEED(); REQUIRE(UN(NtUserQueryWindow));
    HWND d = U(GetDesktopWindow)(); REQUIRE(d);
    r2h_entry_t e;
    CHECK(r2h_read_h(d, &e));
    ULONG_PTR tid = QW(d, 2), pid = QW(d, 0);
    EXPECT_EQ(tid, e.owner);
    CHECK(pid != 0);
    CHECK(pid != GetCurrentProcessId());
    DWORD p2 = 0;
    EXPECT_EQ(U(GetWindowThreadProcessId)(d, &p2), tid);
    EXPECT_EQ(p2, pid);
}
/* Every top-level window of this session: table entry is a live window with an owner; the owning
 * process lives in this session (the handle table is per session). */
static BOOL CALLBACK en_cb(HWND h, LPARAM lp) {
    struct { unsigned n, bad_type, bad_owner, bad_sess, mism; } *s = (void *)lp;
    r2h_entry_t e;
    s->n++;
    if (!r2h_read_h(h, &e) || e.type != UT_WINDOW || (e.flags & 1) || e.uniq != R2H_UNIQ(h)) { s->bad_type++; return TRUE; }
    if (!e.owner) s->bad_owner++;
    DWORD pid = 0, sess = 0;
    if (U(GetWindowThreadProcessId)(h, &pid) && pid) {
        if (U(ProcessIdToSessionId)(pid, &sess) && sess != k_session_id()) s->bad_sess++;
    }
    return s->n < 400;
}
R2(enum_toplevel_entries_are_session_windows) {
    R2H_NEED();
    struct { unsigned n, bad_type, bad_owner, bad_sess, mism; } s = {0};
    U(EnumWindows)(en_cb, (LPARAM)&s);
    CHECK(s.n > 0);
    EXPECT_EQ(s.bad_type, 0);
    EXPECT_EQ(s.bad_owner, 0);
    EXPECT_EQ(s.bad_sess, 0);
}
/* SERVERINFO.cHandleEntries is the committed size in 0x20-byte entries (table growth adds 0x1000 bytes = 128
 * entries per step, capped at 0xFFFE), so it is a multiple of 128 unless it hit the cap. */
R2(table_count_grows_in_page_steps) {
    R2H_NEED();
    unsigned long long n = r2h_n();
    CHECKF(n % 128 == 0 || n == 0xfffe, "cHandleEntries=%llu", n);
    CHECK(n >= 128);
}
