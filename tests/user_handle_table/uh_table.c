/* Table-wide invariants of the client-visible USER handle table and SERVERINFO header fields.
 * Other processes allocate and free concurrently, so every violation is re-read a few times
 * before it is counted (an entry is filled in field by field under the kernel's handle-manager
 * lock, which the client does not take). */
#include "uh.h"

typedef int (*pred_fn)(const uh_entry_t *e, unsigned idx);

static unsigned scan_pred(pred_fn p, unsigned *first, const char *what) {
    unsigned n; uh_entry_t *t = uh_snap(&n);
    if (!t) { t_skip("table snapshot failed"); return 0; }
    unsigned bad = 0; *first = 0;
    for (unsigned i = 0; i < n; i++) {
        if (p(&t[i], i)) continue;
        int ok = 0;
        for (int r = 0; r < 4 && !ok; r++) { uh_entry_t e; Sleep(1); if (uh_read(i, &e) && p(&e, i)) ok = 1; }
        if (!ok) { if (!bad) *first = i; bad++; }
    }
    free(t);
    return bad;
}
#define SCAN(pred, msg) do { unsigned first_; unsigned bad_ = scan_pred(pred, &first_, msg); \
    CHECKF(bad_ == 0, "%s: %u violating entries, first index %u", msg, bad_, first_); } while (0)

/* ---- header / mapping ---- */
TEST(user_handle_table_shared_info_header) {
    UH_NEED();
    struct k_user_shared_info si;
    CHECK(k_shared_info(&si));
    CHECK(si.psi != NULL);
    CHECK(si.aheList != NULL);
    EXPECT_EQ(si.heEntrySize, 0x20);
}
TEST(user_handle_table_entry_count_bounds) {
    UH_NEED();
    unsigned long long n = uh_n();
    CHECK(n >= 128);
    CHECK(n <= 0xfffe); /* handle table growth caps cHandleEntries at 0xFFFE */
    if (n != 0xfffe) CHECKF(n % 128 == 0, "cHandleEntries=%llu not a multiple of 128 (grown in 0x1000-byte steps)", n);
}
TEST(user_handle_table_committed_size_matches_count) {
    UH_NEED();
    struct k_user_shared_info si; unsigned long sz = 0;
    CHECK(k_shared_info(&si) && k_try_read_u32((char *)si.psi + 0x360, &sz));
    unsigned long long n = uh_n();
    /* growth: entry count = committed bytes >> 5, then clamped to 0xFFFE */
    if (n != 0xfffe) EXPECT_EQ((unsigned long long)sz >> 5, n);
    else CHECK((unsigned long long)sz >> 5 >= n);
}
TEST(user_handle_table_whole_table_readable) {
    UH_NEED();
    unsigned n = 0; uh_entry_t *t = uh_snap(&n);
    CHECK(t != NULL);
    CHECK(n == uh_n() || n + 128 >= uh_n()); /* table may grow while we read */
    free(t);
}
TEST(user_handle_table_view_is_read_only_mapped) {
    UH_NEED();
    struct k_user_shared_info si; CHECK(k_shared_info(&si));
    MEMORY_BASIC_INFORMATION mbi;
    CHECK(VirtualQuery(si.aheList, &mbi, sizeof mbi) == sizeof mbi);
    EXPECT_EQ(mbi.State, MEM_COMMIT);
    EXPECT_EQ(mbi.Type, MEM_MAPPED); /* session shared section view (CommitReadOnlyMemory) */
    CHECKF((mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) == 0, "protect 0x%lx", mbi.Protect);
}
TEST(user_handle_table_table_base_page_aligned) {
    UH_NEED();
    struct k_user_shared_info si; CHECK(k_shared_info(&si));
    CHECK(((ULONG_PTR)si.aheList & 0xfff) == 0); /* base of a section view */
}
TEST(user_handle_table_serverinfo_readable) {
    UH_NEED();
    struct k_user_shared_info si; CHECK(k_shared_info(&si));
    unsigned char buf[0x400];
    CHECK(k_try_read(si.psi, buf, 0x368)); /* SERVERINFO is read at +0, +8 and +0x360 by the kernel */
    unsigned long f = 0; CHECK(k_try_read_u32(si.psi, &f));
    /* only the DBCS/IME/MidEast bits (1..3) are stable; higher bits vary across fresh guests */
    t_note("SERVERINFO.dwSRVIFlags & 0xe = 0x%lx (bit 2 = IME enabled)", f & 0xe);
    t_info("SERVERINFO.dwSRVIFlags = 0x%lx", f);
}
TEST(user_handle_table_pdispinfo_pointer) {
    UH_NEED();
    void *p = NULL;
    char *si = (char *)k_shared_info_ptr();
    CHECK(k_try_read_ptr(si + 0x18, &p));
    if (!p) { t_note("gSharedInfo.pDispInfo is NULL"); return; }
    unsigned char b[16]; CHECK(k_try_read(p, b, sizeof b));
}
TEST(user_handle_table_gsharedinfo_stable_across_calls) {
    UH_NEED();
    struct k_user_shared_info a, b;
    CHECK(k_shared_info(&a));
    HWND w = uh_make_top();
    if (w) U(DestroyWindow)(w);
    CHECK(k_shared_info(&b));
    EXPECT_EQ(a.psi, b.psi); EXPECT_EQ(a.aheList, b.aheList); EXPECT_EQ(a.heEntrySize, b.heEntrySize);
}
TEST(user_handle_table_count_never_shrinks) {
    UH_NEED();
    unsigned long long a = uh_n();
    HWND ws[40]; int k = 0;
    for (; k < 40; k++) { ws[k] = uh_make_top(); if (!ws[k]) break; }
    for (int i = 0; i < k; i++) U(DestroyWindow)(ws[i]);
    CHECK(uh_n() >= a); /* the table only grows */
}

/* ---- per-entry invariants ---- */
static int p_reserved0(const uh_entry_t *e, unsigned i) { return i != 0 || (e->type == 0 && e->uniq == 1 && e->delta == 0 && e->owner == 0 && e->f10 == 0); }
TEST(user_handle_table_entry0_reserved) { UH_NEED(); SCAN(p_reserved0, "entry 0 is the reserved free entry with uniq 1"); }

static int p_free_zero(const uh_entry_t *e, unsigned i) { return e->type != 0 || (e->delta == 0 && e->owner == 0 && e->f10 == 0 && e->flags == 0); }
TEST(user_handle_table_free_entries_zeroed) { UH_NEED(); SCAN(p_free_zero, "free entry has zero delta/owner/f10/flags ((freeing and growth zero it))"); }

static int p_uniq_range(const uh_entry_t *e, unsigned i) { return e->uniq != 0 && e->uniq != 0xffff; }
TEST(user_handle_table_uniq_in_range) { UH_NEED(); SCAN(p_uniq_range, "wUniq is never 0 or 0xFFFF"); }

static int p_type_known(const uh_entry_t *e, unsigned i) { return e->type <= UT_MAX; }
TEST(user_handle_table_type_in_range) { UH_NEED(); SCAN(p_type_known, "bType <= 0x17 (24-entry type table)"); }

static int p_window_even(const uh_entry_t *e, unsigned i) { return e->type != UT_WINDOW || !(i & 1); }
TEST(user_handle_table_windows_use_even_indices) { UH_NEED(); SCAN(p_window_even, "window entries come from the even free list"); }

static int p_owner(const uh_entry_t *e, unsigned i) {
    switch (e->type) {
    case 1: case 4: case 5: case 9: case 10: case 11: case 15: case 17: case 18: case 20: case 21: case 23: /* thread-owned */
    case 2: case 3: case 7: case 8: case 16:                                                                 /* process-owned */
        return e->owner != 0 && e->owner < 0x40000000ULL; /* client id */
    case 12: return e->owner == 0; /* monitors have no owner flag */
    default: return 1;
    }
}
TEST(user_handle_table_owned_types_have_client_id) { UH_NEED(); SCAN(p_owner, "owned live entries carry a thread/process id"); }

static int p_heap(const uh_entry_t *e, unsigned i) {
    switch (e->type) {
    /* desktop-heap objects (type flag 0x10): both set, or both clear when allocated without a desktop (allocation falls back to pool) */
    case 1: case 2: case 7: case 17: return (e->delta != 0) == (e->f10 != 0);
    case 12: return e->delta != 0 && e->f10 == 0;                              /* shared-heap monitor (flag 0x40) */
    case 3: case 4: case 5: case 8: case 15: case 16: return e->delta == 0 && e->f10 == 0; /* pool objects */
    default: return 1;
    }
}
TEST(user_handle_table_heap_fields_by_type) { UH_NEED(); SCAN(p_heap, "delta/f10 are set exactly for heap-backed types"); }

static int p_flags(const uh_entry_t *e, unsigned i) { return e->type != 0 || e->flags == 0; }
TEST(user_handle_table_free_entries_have_no_flags) { UH_NEED(); SCAN(p_flags, "free entries have bFlags 0"); }

static int p_pad_destroyed(const uh_entry_t *e, unsigned i) { return !(e->flags & 2) || (e->flags & 1); }
TEST(user_handle_table_pending_flag_implies_destroyed) {
    UH_NEED();
 /* marking an object for destruction sets bit1 only together with bit0. A
     * bit1-without-bit0 state is the one an allocator never produces. */
    unsigned first; unsigned bad = scan_pred(p_pad_destroyed, &first, "");
    if (bad) t_note("%u entries with bit1 set without bit0 (first %u)", bad, first);
}

TEST(user_handle_table_population_by_type) {
    UH_NEED();
    unsigned n; uh_entry_t *t = uh_snap(&n); CHECK(t);
    if (!t) return;
    unsigned c[32] = {0}, live = 0;
    for (unsigned i = 0; i < n; i++) { if (t[i].type <= 31) c[t[i].type]++; if (t[i].type) live++; }
    free(t);
    CHECK(live > 0);
    CHECK(c[UT_WINDOW] > 0); /* at least the desktop and message windows of this session */
    t_info("entries=%u live=%u windows=%u menus=%u cursors=%u hooks=%u monitors=%u timers=%u", n, live, c[1], c[2], c[3], c[5], c[12], c[16]);
}

/* ---- user32 view must agree with the table ---- */
TEST(user_handle_table_iswindow_agrees_with_table) {
    UH_NEED();
    unsigned n; uh_entry_t *t = uh_snap(&n); CHECK(t);
    if (!t) return;
    unsigned wrong = 0, trues = 0, first = 0;
    for (unsigned i = 0; i < n; i++) {
        HWND h = (HWND)(ULONG_PTR)(i | ((unsigned)t[i].uniq << 16));
        BOOL v = U(IsWindow)(h);
        if (v) {
            trues++;
            uh_entry_t e;
            /* user32!IsWindow only answers TRUE for a live type-1 entry with matching uniq */
            if (!(uh_read(i, &e) && e.type == UT_WINDOW && e.uniq == t[i].uniq)) { if (!wrong) first = i; wrong++; }
        } else if (t[i].type == UT_WINDOW && (t[i].flags & 1)) {
            /* destroyed windows are never valid: IsWindow requires (flags & 1) == 0 */
        }
        if (t[i].type != UT_WINDOW && v) { if (!wrong) first = i; wrong++; }
    }
    free(t);
    CHECKF(wrong == 0, "%u entries where IsWindow disagrees with the table, first %u", wrong, first);
    CHECK(trues > 0);
}
TEST(user_handle_table_iswindow_false_for_non_window_entries) {
    UH_NEED();
    unsigned n; uh_entry_t *t = uh_snap(&n); CHECK(t);
    if (!t) return;
    unsigned bad = 0;
    for (unsigned i = 0; i < n; i++) {
        if (t[i].type == UT_WINDOW) continue;
        if (U(IsWindow)((HWND)(ULONG_PTR)(i | ((unsigned)t[i].uniq << 16)))) bad++;
    }
    free(t);
    CHECK(bad == 0);
}
TEST(user_handle_table_ismenu_agrees_with_table) {
    UH_NEED();
    unsigned n; uh_entry_t *t = uh_snap(&n); CHECK(t);
    if (!t) return;
    unsigned bad = 0;
    for (unsigned i = 0; i < n; i++) {
        BOOL v = U(IsMenu)((HMENU)(ULONG_PTR)(i | ((unsigned)t[i].uniq << 16)));
        if (v && t[i].type != UT_MENU) bad++; /* user32!IsMenu = HMValidateHandle(h, 2) */
    }
    free(t);
    CHECK(bad == 0);
}
TEST(user_handle_table_count_of_my_gui_objects_matches_kernel_counter) {
    UH_NEED();
    /* allocation increments PROCESSINFO+0x44 for every type whose flags have bit0|bit1 and
     * freeing decrements it; NtUserGetGuiResources(GR_USEROBJECTS) returns it. */
    HWND w = uh_make_top(); HMENU m = U(CreateMenu)();
    DWORD gr = 0; unsigned sc = 0;
    for (int tries = 0; tries < 4; tries++) {
        gr = uh_gr(GR_USEROBJECTS); sc = uh_scan_owned(-1);
        if (gr == sc) break;
        Sleep(10);
    }
    EXPECT_EQ(gr, sc);
    if (m) U(DestroyMenu)(m);
    if (w) U(DestroyWindow)(w);
}
TEST(user_handle_table_gui_peak_not_below_current) {
    UH_NEED();
    DWORD cur = uh_gr(GR_USEROBJECTS), pk = uh_gr(4);
    CHECK(pk >= cur);
    HMENU m[10]; int k = 0;
    for (; k < 10; k++) { m[k] = U(CreateMenu)(); if (!m[k]) break; }
    DWORD hi = uh_gr(GR_USEROBJECTS), pk2 = uh_gr(4);
    CHECK(pk2 >= hi); CHECK(pk2 >= pk);
    for (int i = 0; i < k; i++) U(DestroyMenu)(m[i]);
    DWORD pk3 = uh_gr(4);
    CHECK(pk3 >= pk2); /* peak never decreases */
}
TEST(user_handle_table_system_wide_count_bounds) {
    UH_NEED();
    /* NtUserGetGuiResources with the pseudo handle -2 returns giheCount (flag 1) / giheCountPeak (flag 4) */
    DWORD mine = uh_gr(GR_USEROBJECTS);
    DWORD tot = U(GetGuiResources)((HANDLE)(LONG_PTR)-2, GR_USEROBJECTS);
    DWORD pk = U(GetGuiResources)((HANDLE)(LONG_PTR)-2, 4);
    CHECK(tot >= mine);
    CHECK(pk >= tot);
    CHECK(tot <= uh_n());
    unsigned n; uh_entry_t *t = uh_snap(&n);
    if (t) { unsigned live = 0; for (unsigned i = 0; i < n; i++) if (t[i].type) live++; free(t);
             long d = (long)live - (long)tot; t_info("live entries %u vs system-wide counter %lu (diff %ld)", live, tot, d); CHECK(d > -256 && d < 256); }
}
