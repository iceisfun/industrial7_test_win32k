/* Whole-table invariants of the client handle table, each derivable from win32kbase:
 *  - growing the handle table zeroes every new entry and stores uniq = 1; initialisation then drops index 0
 *    from the even free list, so entry 0 is never allocated nor freed;
 *  - freeing an object zeroes the first 0x20 bytes (delta, owner, f10, type/flags/uniq) and stores only the
 *    successor uniq, 1..0xFFFE (0xFFFF is skipped), so a free entry is otherwise all zero;
 *  - allocation takes TYPE_WINDOW (1) only from the even free list, hence window indices are even. */
#include "r2h.h"

/* Entries move while other processes run: re-read a suspicious entry before calling it bad. */
static int still_bad(unsigned i, int (*bad)(const r2h_entry_t *)) {
    r2h_entry_t e;
    return r2h_read(i, &e) && bad(&e);
}
static int bad_uniq(const r2h_entry_t *e) { return e->uniq == 0 || e->uniq == 0xffff; }
static int bad_free(const r2h_entry_t *e) {
    return e->type == 0 && (e->owner || e->delta || e->f10 || (e->flags & ~0x20));
}
static int bad_type(const r2h_entry_t *e) { return e->type > UT_MAX; }

R2(table_uniq_is_never_zero_or_ffff) {
    R2H_NEED();
    unsigned n; r2h_entry_t *t = r2h_snap(&n); REQUIRE(t);
    unsigned bad = 0;
    for (unsigned i = 0; i < n; i++)
        if (bad_uniq(&t[i]) && still_bad(i, bad_uniq)) { bad++; t_note("entry %u uniq 0x%x", i, t[i].uniq); }
    free(t);
    EXPECT_EQ(bad, 0);
}
R2(table_free_entries_carry_no_owner_or_heap_part) {
    R2H_NEED();
    unsigned n; r2h_entry_t *t = r2h_snap(&n); REQUIRE(t);
    unsigned bad = 0, nfree = 0;
    for (unsigned i = 0; i < n; i++) {
        if (t[i].type) continue;
        nfree++;
        if (bad_free(&t[i]) && still_bad(i, bad_free)) { bad++; t_note("free entry %u: owner %llx delta %llx f10 %llx flags %x", i, t[i].owner, t[i].delta, t[i].f10, t[i].flags); }
    }
    free(t);
    EXPECT_EQ(bad, 0);
    CHECK(nfree > 0);
}
R2(table_every_type_is_a_known_type) {
    R2H_NEED();
    unsigned n; r2h_entry_t *t = r2h_snap(&n); REQUIRE(t);
    unsigned bad = 0;
    for (unsigned i = 0; i < n; i++)
        if (bad_type(&t[i]) && still_bad(i, bad_type)) { bad++; t_note("entry %u type %u", i, t[i].type); }
    free(t);
    EXPECT_EQ(bad, 0);
}
R2(table_entry_zero_is_never_allocated) {
    R2H_NEED();
    r2h_entry_t e; REQUIRE(r2h_read(0, &e));
    EXPECT_EQ(e.type, 0); EXPECT_EQ(e.uniq, 1);
    EXPECT_EQ(e.owner, 0); EXPECT_EQ(e.delta, 0); EXPECT_EQ(e.f10, 0); EXPECT_EQ(e.flags, 0);
    /* no handle we can create has index 0 */
    HWND w = r2h_make_top(); REQUIRE(w);
    CHECK(R2H_IDX(w) != 0);
    U(DestroyWindow)(w);
}
R2(table_count_is_within_cap) {
    R2H_NEED();
    unsigned long long n = r2h_n();
    CHECK(n >= 2);
    CHECK(n <= 0xfffe);   /* growth clamps cHandleEntries to 0xFFFE: index 0xFFFF is never valid */
}
/* Windows are allocated from the even free list only, so a window handle always has an even index. */
R2(window_handles_have_even_index) {
    R2H_NEED();
    HWND p = r2h_make_top(); REQUIRE(p);
    for (int i = 0; i < 24; i++) {
        HWND w = (i & 1) ? r2h_make_child(p) : r2h_make_top();
        if (!w) { t_note("window %d not created", i); continue; }
        CHECKF(!(R2H_IDX(w) & 1), "window %d handle %p has an odd index", i, w);
        U(DestroyWindow)(w);
    }
    U(DestroyWindow)(p);
}
R2(window_entries_in_table_have_even_index) {
    R2H_NEED();
    HWND p = r2h_make_top(); REQUIRE(p);
    unsigned n; r2h_entry_t *t = r2h_snap(&n);
    if (!t) { U(DestroyWindow)(p); t_skip("snapshot failed"); return; }
    unsigned bad = 0, seen = 0;
    for (unsigned i = 0; i < n; i++) {
        if (t[i].type != UT_WINDOW) continue;
        seen++;
        if (i & 1) { r2h_entry_t e; if (r2h_read(i, &e) && e.type == UT_WINDOW) { bad++; t_note("window entry at odd index %u", i); } }
    }
    free(t);
    CHECK(seen > 0);
    EXPECT_EQ(bad, 0);
    U(DestroyWindow)(p);
}
/* freeing clears owner, delta, f10 and the type/flags word: nothing of the old object stays. */
R2(destroyed_window_entry_is_fully_cleared) {
    R2H_NEED();
    HWND w = r2h_make_top(); REQUIRE(w);
    unsigned idx = R2H_IDX(w), uq = R2H_UNIQ(w);
    U(DestroyWindow)(w);
    r2h_entry_t e = {0};
    DWORD t0 = GetTickCount();
    do { CHECK(r2h_read(idx, &e)); if (e.type == 0 && e.uniq != uq) break; Sleep(10); } while (GetTickCount() - t0 < 1000);
    if (e.type != 0) { t_skip("slot reused by another allocation"); return; }
    EXPECT_EQ(e.uniq, r2h_next_uniq((unsigned short)uq));
    EXPECT_EQ(e.owner, 0); EXPECT_EQ(e.delta, 0); EXPECT_EQ(e.f10, 0); EXPECT_EQ(e.flags, 0);
}
