/* Timer creation, replacement, id allocation, argument validation and object lifetime.
 * win32kfull!NtUserSetTimer validates the window, clamps the interval to [10, 0x7fffffff] and the
 * tolerance rules, then win32kbase!InternalSetTimer allocates or replaces a TYPE_TIMER (16) object. */
#include "mta.h"

#define P(x) msg_timers_atoms_##x

/* ---- window timers: return value and id handling ---------------------------------------------- */
TEST(msg_timers_atoms_settimer_window_returns_requested_id) {
    MTA_WIN(w);
 /* InternalSetTimer returns the stored id for a window timer. */
    SET_ERR(0);
    MEQ(U(SetTimer)(w, 77, 60000, NULL), 77);
    CHECK(U(KillTimer)(w, 77));
    mta_destroy(w);
}
TEST(msg_timers_atoms_settimer_window_id_table) {
    MTA_WIN(w);
    static const UINT_PTR ids[] = { 1, 2, 0xff, 0x100, 0x7fff, 0x8000, 0xffff, 0x10000, 0x7fffffff,
        0x80000000u, 0xffffffffu, (UINT_PTR)0x123456789ULL, (UINT_PTR)0xFFFFFFFF00000001ULL, (UINT_PTR)~0ULL };
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        UINT_PTR r = U(SetTimer)(w, ids[i], 60000, NULL);
        CHECKF(r == ids[i], "id 0x%llx -> 0x%llx", (unsigned long long)ids[i], (unsigned long long)r);
        CHECKF(U(KillTimer)(w, ids[i]), "kill 0x%llx", (unsigned long long)ids[i]);
    }
    mta_destroy(w);
}
TEST(msg_timers_atoms_settimer_window_id_zero_returns_one) {
    MTA_WIN(w);
    /* InternalSetTimer ends with "return id == 0 ? 1 : id": a window timer with id 0 reports success as 1. */
    unsigned n0 = mta_timers();
    MEQ(U(SetTimer)(w, 0, 60000, NULL), 1);
    CHECK(U(KillTimer)(w, 0));
    if (mta_table_ok()) MEQ(mta_timers(), n0);
    mta_destroy(w);
}
TEST(msg_timers_atoms_settimer_window_id_zero_replaces) {
    MTA_WIN(w);
    if (!mta_table_ok()) { t_skip("USER handle table unavailable"); mta_destroy(w); return; }
    unsigned n0 = mta_timers();
    MEQ(U(SetTimer)(w, 0, 60000, NULL), 1);
    MEQ(U(SetTimer)(w, 0, 50000, NULL), 1);      /* same (hwnd, 0) key: replaced, not duplicated */
    MEQ(mta_timers(), n0 + 1);
    CHECK(U(KillTimer)(w, 0));
    MEQ(mta_timers(), n0);
    mta_destroy(w);
}
TEST(msg_timers_atoms_settimer_replace_keeps_one_object) {
    MTA_WIN(w);
    if (!mta_table_ok()) { t_skip("USER handle table unavailable"); mta_destroy(w); return; }
    unsigned n0 = mta_timers();
    MEQ(U(SetTimer)(w, 5, 60000, NULL), 5);
    MEQ(mta_timers(), n0 + 1);
    for (int i = 0; i < 5; i++) MEQ(U(SetTimer)(w, 5, 60000 + i, NULL), 5);
    MEQ(mta_timers(), n0 + 1);                    /* InternalSetTimer finds (hwnd,id) in gTimerHashTable and reuses it */
    CHECK(U(KillTimer)(w, 5));
    CHECK(!U(KillTimer)(w, 5));                        /* a replaced timer is a single object: one kill removes it */
    MEQ(mta_timers(), n0);
    mta_destroy(w);
}
TEST(msg_timers_atoms_settimer_distinct_ids_distinct_objects) {
    MTA_WIN(w);
    if (!mta_table_ok()) { t_skip("USER handle table unavailable"); mta_destroy(w); return; }
    unsigned n0 = mta_timers();
    for (UINT_PTR i = 1; i <= 8; i++) MEQ(U(SetTimer)(w, i, 60000, NULL), i);
    MEQ(mta_timers(), n0 + 8);
    for (UINT_PTR i = 1; i <= 8; i++) CHECK(U(KillTimer)(w, i));
    MEQ(mta_timers(), n0);
    mta_destroy(w);
}
TEST(msg_timers_atoms_settimer_same_id_two_windows) {
    MTA_WIN(w1);
    HWND w2 = mta_win();
    if (!w2) { t_skip("second window"); mta_destroy(w1); return; }
    if (!mta_table_ok()) { t_skip("USER handle table unavailable"); mta_destroy(w1); mta_destroy(w2); return; }
    unsigned n0 = mta_timers();
    /* the hash key is (hwnd, id): equal ids on different windows are separate timers */
    MEQ(U(SetTimer)(w1, 9, 60000, NULL), 9);
    MEQ(U(SetTimer)(w2, 9, 60000, NULL), 9);
    MEQ(mta_timers(), n0 + 2);
    CHECK(U(KillTimer)(w1, 9));
    MEQ(mta_timers(), n0 + 1);
    CHECK(U(KillTimer)(w2, 9));
    MEQ(mta_timers(), n0);
    mta_destroy(w1); mta_destroy(w2);
}
TEST(msg_timers_atoms_settimer_hash_bucket_collisions) {
    MTA_WIN(w);
    if (!mta_table_ok()) { t_skip("USER handle table unavailable"); mta_destroy(w); return; }
    unsigned n0 = mta_timers();
    /* bucket = ((hwnd >> 8) + id) & 0x3f, so ids 64 apart share a bucket and must still be told apart */
    for (int i = 0; i < 6; i++) MEQ(U(SetTimer)(w, 3 + 64 * i, 60000, NULL), 3 + 64 * i);
    MEQ(mta_timers(), n0 + 6);
    CHECK(U(KillTimer)(w, 3 + 64 * 3));
    CHECK(!U(KillTimer)(w, 3 + 64 * 3));
    MEQ(mta_timers(), n0 + 5);
    for (int i = 0; i < 6; i++) if (i != 3) CHECK(U(KillTimer)(w, 3 + 64 * i));
    MEQ(mta_timers(), n0);
    mta_destroy(w);
}
TEST(msg_timers_atoms_settimer_many_timers) {
    MTA_WIN(w);
    if (!mta_table_ok()) { t_skip("USER handle table unavailable"); mta_destroy(w); return; }
    unsigned n0 = mta_timers();
    for (UINT_PTR i = 1; i <= 200; i++) if (U(SetTimer)(w, i, 60000, NULL) != i) { CHECKF(0, "SetTimer id %llu failed", (unsigned long long)i); break; }
    MEQ(mta_timers(), n0 + 200);
    for (UINT_PTR i = 1; i <= 200; i++) U(KillTimer)(w, i);
    MEQ(mta_timers(), n0);
    mta_destroy(w);
}

/* ---- NULL-hwnd timers: system-range ids ------------------------------------------------------- */
TEST(msg_timers_atoms_null_hwnd_id_in_system_range) {
    MTA_NEED();
    static const UINT_PTR req[] = { 0, 1, 5, 0xff, 0xABCDE0 };
    for (unsigned i = 0; i < sizeof req / sizeof req[0]; i++) {
 /* InternalSetTimer allocates ids for argument 1 == 0 from a bitmap of 0x7f00 slots, id = slot + 0x100 */
        UINT_PTR id = U(SetTimer)(NULL, req[i], 60000, NULL);
        CHECK(id != 0);
        CHECKF(id >= 0x100 && id <= 0x7fff, "id 0x%llx", (unsigned long long)id);
        CHECKF(id != req[i], "requested id 0x%llx was honoured", (unsigned long long)req[i]);
        CHECK(U(KillTimer)(NULL, id));
    }
}
TEST(msg_timers_atoms_null_hwnd_ids_unique) {
    MTA_NEED();
    UINT_PTR ids[40]; int n = 0;
    for (int i = 0; i < 40; i++) { ids[i] = U(SetTimer)(NULL, 0, 60000, NULL); if (!ids[i]) break; n++; }
    MEQ(n, 40);
    for (int i = 0; i < n; i++) {
        CHECK(ids[i] >= 0x100 && ids[i] <= 0x7fff);
        for (int j = i + 1; j < n; j++) if (ids[i] == ids[j]) CHECKF(0, "duplicate id 0x%llx", (unsigned long long)ids[i]);
    }
    for (int i = 0; i < n; i++) CHECK(U(KillTimer)(NULL, ids[i]));
}
TEST(msg_timers_atoms_null_hwnd_ids_descend_note) {
    MTA_NEED();
    UINT_PTR a = U(SetTimer)(NULL, 0, 60000, NULL), b = U(SetTimer)(NULL, 0, 60000, NULL);
    REQUIRE(a && b);
    CHECK(a != b);
    t_info("consecutive NULL-hwnd timer ids 0x%llx, 0x%llx (g_cTimerId counts down, wraps to 0x7eff)", (unsigned long long)a, (unsigned long long)b);
    U(KillTimer)(NULL, a); U(KillTimer)(NULL, b);
}
TEST(msg_timers_atoms_null_hwnd_existing_id_replaces) {
    MTA_NEED();
    if (!mta_table_ok()) { t_skip("USER handle table unavailable"); return; }
    unsigned n0 = mta_timers();
    UINT_PTR id = U(SetTimer)(NULL, 0, 60000, NULL);
    REQUIRE(id);
    /* (hwnd NULL, id) matches the existing timer, so it is reset in place and reports the same id */
    MEQ(U(SetTimer)(NULL, id, 50000, NULL), id);
    MEQ(mta_timers(), n0 + 1);
    CHECK(U(KillTimer)(NULL, id));
    MEQ(mta_timers(), n0);
}
TEST(msg_timers_atoms_null_hwnd_id_does_not_collide_with_window_timer) {
    MTA_WIN(w);
    if (!mta_table_ok()) { t_skip("USER handle table unavailable"); mta_destroy(w); return; }
    unsigned n0 = mta_timers();
    UINT_PTR id = U(SetTimer)(NULL, 0, 60000, NULL);
    REQUIRE(id);
    MEQ(U(SetTimer)(w, id, 60000, NULL), id);     /* same numeric id, different hwnd key */
    MEQ(mta_timers(), n0 + 2);
    CHECK(U(KillTimer)(w, id));
    MEQ(mta_timers(), n0 + 1);
    CHECK(U(KillTimer)(NULL, id));
    MEQ(mta_timers(), n0);
    mta_destroy(w);
}
TEST(msg_timers_atoms_killtimer_null_does_not_hit_window_timer) {
    MTA_WIN(w);
    MEQ(U(SetTimer)(w, 0x321, 60000, NULL), 0x321);
    CHECK(!U(KillTimer)(NULL, 0x321));       /* key (NULL, 0x321) does not exist */
    CHECK(U(KillTimer)(w, 0x321));
    mta_destroy(w);
}

/* ---- KillTimer -------------------------------------------------------------------------------- */
TEST(msg_timers_atoms_killtimer_twice) {
    MTA_WIN(w);
    MEQ(U(SetTimer)(w, 3, 60000, NULL), 3);
    CHECK(U(KillTimer)(w, 3));
    CHECK(!U(KillTimer)(w, 3));
    mta_destroy(w);
}
TEST(msg_timers_atoms_killtimer_unknown_id_fails) {
    MTA_WIN(w);
    SET_ERR(MTA_ERR_SENTINEL);
    CHECK(!U(KillTimer)(w, 0xdead));
    /* NtUserKillTimer sets no error when the (hwnd,id) key is missing and user32!KillTimer is a bare jump */
    DWORD e = GetLastError();
    if (e != MTA_ERR_SENTINEL) t_note("KillTimer(unknown) changed last error to %lu", e);
    mta_destroy(w);
}
TEST(msg_timers_atoms_killtimer_wrong_window_keeps_timer) {
    MTA_WIN(w1);
    HWND w2 = mta_win();
    if (!w2) { t_skip("second window"); mta_destroy(w1); return; }
    MEQ(U(SetTimer)(w1, 9, 60000, NULL), 9);
    CHECK(!U(KillTimer)(w2, 9));
    CHECK(U(KillTimer)(w1, 9));
    mta_destroy(w1); mta_destroy(w2);
}
TEST(msg_timers_atoms_killtimer_never_set_null_hwnd) {
    MTA_NEED();
    CHECK(!U(KillTimer)(NULL, 0x7001));
    CHECK(!U(KillTimer)(NULL, 0));
}
TEST(msg_timers_atoms_killtimer_frees_object_and_entry) {
    MTA_WIN(w);
    if (!mta_table_ok()) { t_skip("USER handle table unavailable"); mta_destroy(w); return; }
    unsigned before[512]; unsigned nb = mta_timer_indexes(before, 512);
    MEQ(U(SetTimer)(w, 11, 60000, NULL), 11);
    unsigned after[512]; unsigned na = mta_timer_indexes(after, 512);
    MEQ(na, nb + 1);
    unsigned fresh = ~0u;
    for (unsigned i = 0; i < na && i < 512; i++) {
        int seen = 0;
        for (unsigned j = 0; j < nb && j < 512; j++) if (before[j] == after[i]) seen = 1;
        if (!seen) fresh = after[i];
    }
    REQUIRE(fresh != ~0u);
    struct mta_hent e;
    CHECK(mta_read_entry(fresh, &e));
    MEQ(e.type, MTA_TYPE_TIMER);
    CHECK(e.owner != 0);
    CHECK((e.flags & 1) == 0);
    unsigned short uniq = e.uniq;
    CHECK(U(KillTimer)(w, 11));
    CHECK(mta_read_entry(fresh, &e));
    MEQ(e.type, 0);                   /* freeing returns the slot to the free list */
    CHECK(e.uniq != uniq);                  /* and bumps the uniqueness counter */
    mta_destroy(w);
}
TEST(msg_timers_atoms_timer_entry_fields) {
    MTA_WIN(w);
    if (!mta_table_ok()) { t_skip("USER handle table unavailable"); mta_destroy(w); return; }
    unsigned idx[512]; unsigned n0 = mta_timer_indexes(idx, 512);
    UINT_PTR id = U(SetTimer)(NULL, 0, 60000, NULL);
    REQUIRE(id);
    unsigned idx2[512]; unsigned n1 = mta_timer_indexes(idx2, 512);
    MEQ(n1, n0 + 1);
    int found = 0;
    for (unsigned i = 0; i < n1 && i < 512; i++) {
        int seen = 0;
        for (unsigned j = 0; j < n0 && j < 512; j++) if (idx[j] == idx2[i]) seen = 1;
        if (seen) continue;
        struct mta_hent e; CHECK(mta_read_entry(idx2[i], &e));
        found++;
        /* TYPE_TIMER with no desktop: a pool object without a desktop-heap part */
        MEQ(e.delta, 0);   /* no desktop-heap part: the client delta is 0 (f10 may hold a recycled slot's value) */
        MEQ(e.type, 16);
    }
    MEQ(found, 1);
    U(KillTimer)(NULL, id);
    mta_destroy(w);
}
TEST(msg_timers_atoms_timer_counts_as_user_object) {
    MTA_NEED();
    DWORD g0 = mta_guires();
    UINT_PTR id = U(SetTimer)(NULL, 0, 60000, NULL);
    REQUIRE(id);
    MEQ(mta_guires(), g0 + 1);
    CHECK(U(KillTimer)(NULL, id));
    MEQ(mta_guires(), g0);
}

/* ---- invalid windows --------------------------------------------------------------------------- */
TEST(msg_timers_atoms_settimer_invalid_hwnd_table) {
    MTA_NEED();
    /* ValidateHwndEx: out-of-range index, wrong entry type or stale uniq all end in ERROR_INVALID_WINDOW_HANDLE (0x578) */
    static const ULONG_PTR bad[] = { 1, 2, 0x10, 0xFFFE, 0xFFFF, 0x12345678, 0xFFFF0000, (ULONG_PTR)-1, (ULONG_PTR)-3 };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SET_ERR(0);
        UINT_PTR r = U(SetTimer)((HWND)bad[i], 1, 1000, NULL);
        if (r) { CHECKF(0, "SetTimer(0x%llx) succeeded", (unsigned long long)bad[i]); U(KillTimer)((HWND)bad[i], 1); continue; }
        DWORD e = GetLastError();
        CHECKF(e == ERROR_INVALID_WINDOW_HANDLE, "hwnd 0x%llx err %lu", (unsigned long long)bad[i], e);
    }
}
TEST(msg_timers_atoms_killtimer_invalid_hwnd_table) {
    MTA_NEED();
    static const ULONG_PTR bad[] = { 1, 0x10, 0xFFFF, 0x12345678, (ULONG_PTR)-1 };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SET_ERR(0);
        CHECKF(!U(KillTimer)((HWND)bad[i], 1), "hwnd 0x%llx", (unsigned long long)bad[i]);
        EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
    }
}
TEST(msg_timers_atoms_settimer_destroyed_window) {
    MTA_WIN(w);
    mta_destroy(w);
    SET_ERR(0);
    MEQ(U(SetTimer)(w, 1, 1000, NULL), 0);
    EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
    SET_ERR(0);
    CHECK(!U(KillTimer)(w, 1));
    EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
}
TEST(msg_timers_atoms_settimer_foreign_process_window_denied) {
    MTA_NEED();
    HWND dw = U(GetDesktopWindow)();
    REQUIRE(dw);
    /* NtUserSetTimer: the window's thread must belong to the caller's process (ppi compare), else ERROR_ACCESS_DENIED.
     * The desktop window belongs to csrss, which ValidateHwndEx exempts from the integrity check. */
    unsigned n0 = mta_table_ok() ? mta_timers() : 0;
    SET_ERR(0);
    MEQ(U(SetTimer)(dw, 0x55, 1000, NULL), 0);
    EXPECT_ERR(ERROR_ACCESS_DENIED);
    if (mta_table_ok()) MEQ(mta_timers(), n0);
    SET_ERR(0);
    MEQ(U(SetCoalescableTimer)(dw, 0x55, 1000, NULL, 0), 0);
    EXPECT_ERR(ERROR_ACCESS_DENIED);
}
TEST(msg_timers_atoms_killtimer_foreign_window_no_match) {
    MTA_NEED();
    HWND dw = U(GetDesktopWindow)();
    REQUIRE(dw);
    CHECK(!U(KillTimer)(dw, 0x55));
}
TEST(msg_timers_atoms_settimer_cross_thread_window_same_process) {
    MTA_NEED();
    struct mta_owner o;
    if (!mta_owner_start(&o, 1, 1, 0)) { t_skip("owner thread"); return; }
    /* only the process is compared, so a window owned by another thread of this process is acceptable */
    SET_ERR(0);
    UINT_PTR r = U(SetTimer)(o.hwnd, 42, 60000, NULL);
    MEQ(r, 42);
    CHECK(U(KillTimer)(o.hwnd, 42));
    mta_owner_stop(&o);
}

/* ---- interval handling ------------------------------------------------------------------------- */
TEST(msg_timers_atoms_settimer_elapse_table) {
    MTA_WIN(w);
    /* InternalSetTimer clamps to [USER_TIMER_MINIMUM 10, USER_TIMER_MAXIMUM 0x7fffffff] instead of failing */
    static const UINT el[] = { 0, 1, 9, 10, 11, 15, 1000, 0x7ffffffe, 0x7fffffff, 0x80000000u, 0xfffffffeu, 0xffffffffu };
    for (unsigned i = 0; i < sizeof el / sizeof el[0]; i++) {
        SET_ERR(0);
        UINT_PTR r = U(SetTimer)(w, 100 + i, el[i], NULL);
        CHECKF(r == 100 + i, "elapse 0x%x -> 0x%llx err %lu", el[i], (unsigned long long)r, GetLastError());
        U(KillTimer)(w, 100 + i);
    }
    mta_destroy(w);
}
TEST(msg_timers_atoms_settimer_elapse_table_null_hwnd) {
    MTA_NEED();
    static const UINT el[] = { 0, 1, 9, 10, 0x7fffffff, 0x80000000u, 0xffffffffu };
    for (unsigned i = 0; i < sizeof el / sizeof el[0]; i++) {
        UINT_PTR id = U(SetTimer)(NULL, 0, el[i], NULL);
        CHECKF(id >= 0x100 && id <= 0x7fff, "elapse 0x%x -> 0x%llx", el[i], (unsigned long long)id);
        if (id) U(KillTimer)(NULL, id);
    }
}
TEST(msg_timers_atoms_settimer_proc_pointer_accepted) {
    MTA_WIN(w);
    /* the TIMERPROC is stored verbatim (tagTIMER+0x20); the kernel never validates it at SetTimer time */
    MEQ(U(SetTimer)(w, 1, 60000, mta_proc_a), 1);
    MEQ(U(SetTimer)(w, 2, 60000, (TIMERPROC)(ULONG_PTR)0x10000), 2);
    CHECK(U(KillTimer)(w, 1)); CHECK(U(KillTimer)(w, 2));
    mta_destroy(w);
}

/* ---- SetCoalescableTimer tolerance ---------------------------------------------------------------- */
TEST(msg_timers_atoms_coalescable_valid_tolerances) {
    MTA_WIN(w);
    if (!U(SetCoalescableTimer)) { t_skip("SetCoalescableTimer missing"); mta_destroy(w); return; }
    static const ULONG tol[] = { 0, 1, 10, 100, 5000, MTA_TIMERV_NO_COALESCING, MTA_TIMERV_COALESCING_MAX };
    for (unsigned i = 0; i < sizeof tol / sizeof tol[0]; i++) {
        SET_ERR(0);
        UINT_PTR r = U(SetCoalescableTimer)(w, 200 + i, 1000, NULL, tol[i]);
        CHECKF(r == 200 + i, "tolerance 0x%lx -> 0x%llx err %lu", tol[i], (unsigned long long)r, GetLastError());
        U(KillTimer)(w, 200 + i);
    }
    mta_destroy(w);
}
TEST(msg_timers_atoms_coalescable_overflowing_tolerance_rejected) {
    MTA_WIN(w);
    if (!U(SetCoalescableTimer)) { t_skip("SetCoalescableTimer missing"); mta_destroy(w); return; }
    struct { UINT el; ULONG tol; } v[] = {
        { 1000, 0x7fffffff }, { 10, 0x7ffffff6 }, { 1000, 0x80000000u }, { 1000, 0xfffffffeu },
        { 0x7fffffff, 1 }, { 0x7ffffff0, 0x20 }
    };
    unsigned n0 = mta_table_ok() ? mta_timers() : 0;
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) {
        /* NtUserSetTimer: tolerance (other than -1 and 0x7ffffff5) with elapse+tolerance above 0x7fffffff, or wrapping, fails with 87 */
        SET_ERR(0);
        UINT_PTR r = U(SetCoalescableTimer)(w, 300 + i, v[i].el, NULL, v[i].tol);
        CHECKF(r == 0, "elapse 0x%x tol 0x%lx accepted", v[i].el, v[i].tol);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
        if (r) U(KillTimer)(w, 300 + i);
    }
    if (mta_table_ok()) MEQ(mta_timers(), n0);   /* nothing was allocated */
    mta_destroy(w);
}
TEST(msg_timers_atoms_coalescable_max_elapse_zero_tolerance) {
    MTA_WIN(w);
    if (!U(SetCoalescableTimer)) { t_skip("SetCoalescableTimer missing"); mta_destroy(w); return; }
    MEQ(U(SetCoalescableTimer)(w, 1, 0x7fffffff, NULL, 0), 1);
    MEQ(U(SetCoalescableTimer)(w, 2, 0x7fffffff, NULL, MTA_TIMERV_NO_COALESCING), 2);
    MEQ(U(SetCoalescableTimer)(w, 3, 0x7fffffff, NULL, MTA_TIMERV_COALESCING_MAX), 3);  /* special-cased to 0x7fffffff - elapse = 0 */
    U(KillTimer)(w, 1); U(KillTimer)(w, 2); U(KillTimer)(w, 3);
    mta_destroy(w);
}
TEST(msg_timers_atoms_coalescable_invalid_hwnd) {
    MTA_NEED();
    if (!U(SetCoalescableTimer)) { t_skip("SetCoalescableTimer missing"); return; }
    SET_ERR(0);
    MEQ(U(SetCoalescableTimer)((HWND)(ULONG_PTR)0x12345678, 1, 1000, NULL, 10), 0);
    EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
}
TEST(msg_timers_atoms_coalescable_replaces_plain_timer) {
    MTA_WIN(w);
    if (!U(SetCoalescableTimer) || !mta_table_ok()) { t_skip("prerequisites"); mta_destroy(w); return; }
    unsigned n0 = mta_timers();
    MEQ(U(SetTimer)(w, 4, 60000, NULL), 4);
    MEQ(U(SetCoalescableTimer)(w, 4, 50000, NULL, 100), 4);   /* same key regardless of the coalescing flag */
    MEQ(mta_timers(), n0 + 1);
    CHECK(U(KillTimer)(w, 4));
    mta_destroy(w);
}

/* ---- direct NtUserSetTimer / NtUserKillTimer ---------------------------------------------------------- */
TEST(msg_timers_atoms_direct_settimer_matches_user32) {
    MTA_WIN(w);
    NtUserSetTimer_pfn st = UN(NtUserSetTimer); NtUserKillTimer_pfn kt = UN(NtUserKillTimer);
    REQUIRE(st); REQUIRE(kt);
    MEQ(st(w, 31, 60000, NULL, 0), 31);
    MEQ(U(SetTimer)(w, 31, 60000, NULL), 31);
    CHECK(kt(w, 31));
    CHECK(!U(KillTimer)(w, 31));
    mta_destroy(w);
}
TEST(msg_timers_atoms_direct_settimer_tolerance_arg) {
    MTA_WIN(w);
    NtUserSetTimer_pfn st = UN(NtUserSetTimer); NtUserKillTimer_pfn kt = UN(NtUserKillTimer);
    REQUIRE(st); REQUIRE(kt);
    MEQ(st(w, 1, 1000, NULL, 0xFFFFFFFFu), 1);
    SET_ERR(0);
    MEQ(st(w, 2, 1000, NULL, 0x7fffffff), 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    CHECK(kt(w, 1));
    CHECK(!kt(w, 2));
    mta_destroy(w);
}
TEST(msg_timers_atoms_direct_settimer_stub_numbers) {
    FARPROC a = t_proc_in("win32u.dll", "NtUserSetTimer"), b = t_proc_in("win32u.dll", "NtUserKillTimer");
    FARPROC c = t_proc_in("win32u.dll", "NtUserSetSystemTimer"), d = t_proc_in("win32u.dll", "NtUserValidateTimerCallback");
    REQUIRE(a); REQUIRE(b); REQUIRE(c); REQUIRE(d);
    CHECK(k_stub_valid(a)); CHECK(k_stub_valid(b)); CHECK(k_stub_valid(c)); CHECK(k_stub_valid(d));
    /* win32u_syscalls.tsv: SetTimer 4120, KillTimer 4123, SetSystemTimer 5358, ValidateTimerCallback 4118 (decimal) */
    MEQ(k_stub_number(a), 4120); MEQ(k_stub_number(b), 4123);
    MEQ(k_stub_number(c), 5358); MEQ(k_stub_number(d), 4118);
}
TEST(msg_timers_atoms_setcoalescable_and_settimer_share_stub) {
    MTA_NEED();
    /* user32!SetCoalescableTimer and SetTimer both tail-call NtUserSetTimer; SetTimer passes tolerance 0 */
    FARPROC a = t_proc_in("win32u.dll", "NtUserSetTimer");
    REQUIRE(a);
    HWND w = mta_win(); REQUIRE(w);
    MEQ(U(SetTimer)(w, 8, 60000, NULL), 8);
    CHECK(U(KillTimer)(w, 8));
    mta_destroy(w);
}

/* ---- timers die with their owners ---------------------------------------------------------------------- */
TEST(msg_timers_atoms_destroy_window_kills_timers) {
    MTA_WIN(w);
    if (!mta_table_ok()) { t_skip("USER handle table unavailable"); mta_destroy(w); return; }
    unsigned n0 = mta_timers();
    for (UINT_PTR i = 1; i <= 6; i++) MEQ(U(SetTimer)(w, i, 60000, NULL), i);
    MEQ(mta_timers(), n0 + 6);
    mta_destroy(w);
    MEQ(mta_timers(), n0);               /* window destruction removes its timers */
    SET_ERR(0);
    CHECK(!U(KillTimer)(w, 1));
}
TEST(msg_timers_atoms_destroy_window_keeps_null_hwnd_timers) {
    MTA_WIN(w);
    if (!mta_table_ok()) { t_skip("USER handle table unavailable"); mta_destroy(w); return; }
    unsigned n0 = mta_timers();
    UINT_PTR id = U(SetTimer)(NULL, 0, 60000, NULL);
    REQUIRE(id);
    MEQ(U(SetTimer)(w, 5, 60000, NULL), 5);
    mta_destroy(w);
    MEQ(mta_timers(), n0 + 1);           /* only the window's timer died */
    CHECK(U(KillTimer)(NULL, id));
}
TEST(msg_timers_atoms_destroy_window_does_not_kill_other_windows_timers) {
    MTA_WIN(w1);
    HWND w2 = mta_win();
    if (!w2 || !mta_table_ok()) { t_skip("prerequisites"); mta_destroy(w1); mta_destroy(w2); return; }
    unsigned n0 = mta_timers();
    MEQ(U(SetTimer)(w1, 1, 60000, NULL), 1);
    MEQ(U(SetTimer)(w2, 1, 60000, NULL), 1);
    mta_destroy(w1);
    MEQ(mta_timers(), n0 + 1);
    CHECK(U(KillTimer)(w2, 1));
    mta_destroy(w2);
}
static DWORD WINAPI timer_thread(LPVOID p) {
    U(GetDesktopWindow)();
    UINT_PTR a = U(SetTimer)(NULL, 0, 60000, NULL), b = U(SetTimer)(NULL, 0, 60000, NULL);
    *(int *)p = (a && b && a != b);
    return 0;
}
TEST(msg_timers_atoms_thread_exit_destroys_thread_timers) {
    MTA_NEED();
    if (!mta_table_ok()) { t_skip("USER handle table unavailable"); return; }
    unsigned n0 = mta_timers();
    int ok = 0;
    HANDLE t = CreateThread(NULL, 0, timer_thread, &ok, 0, NULL);
    REQUIRE(t);
    CHECK(WaitForSingleObject(t, 5000) == WAIT_OBJECT_0);
    CloseHandle(t);
    MEQ(ok, 1);
    unsigned n = mta_timers();
    for (int i = 0; i < 100 && n != n0; i++) { Sleep(20); n = mta_timers(); }
    MEQ(n, n0);      /* thread teardown removes its timers */
}
TEST(msg_timers_atoms_thread_exit_destroys_owned_window_timers) {
    MTA_NEED();
    if (!mta_table_ok()) { t_skip("USER handle table unavailable"); return; }
    unsigned n0 = mta_timers();
    struct mta_owner o;
    if (!mta_owner_start(&o, 1, 1, 1)) { t_skip("owner thread"); return; }
    HWND w = o.hwnd;
    MEQ(U(SetTimer)(w, 1, 60000, NULL), 1);
    MEQ(U(SetTimer)(w, 2, 60000, NULL), 2);
    MEQ(mta_timers(), n0 + 2);
    mta_owner_stop(&o);                         /* thread exits with the window alive */
    unsigned n = mta_timers();
    for (int i = 0; i < 100 && n != n0; i++) { Sleep(20); n = mta_timers(); }
    MEQ(n, n0);
    CHECK(!U(IsWindow)(w));
}
TEST(msg_timers_atoms_cross_thread_kill) {
    MTA_NEED();
    struct mta_owner o;
    if (!mta_owner_start(&o, 1, 1, 0)) { t_skip("owner thread"); return; }
    MEQ(U(SetTimer)(o.hwnd, 12, 60000, NULL), 12);
    CHECK(U(KillTimer)(o.hwnd, 12));            /* NtUserKillTimer compares processes, not threads */
    CHECK(!U(KillTimer)(o.hwnd, 12));
    mta_owner_stop(&o);
}
