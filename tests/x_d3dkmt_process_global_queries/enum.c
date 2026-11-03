/* x_d3dkmt_process_global_queries / enum: EnumAdapters, EnumAdapters2 and EnumAdapters3 (win32u NtDxgkEnumAdapters3).
 *
 * The three thunks list the display adapters of the session and (when asked to fill) open a handle for each.
 * Measures:
 *   - minimum block sizes (v1 0x144 bytes = count + 16 entries of 20 bytes; v2 0x10; v3 0x18), NULL and wild pointers
 *     answered INVALID_PARAMETER;
 *   - the sizing call (NULL array) succeeds for every incoming count and reports a capacity that is at least the
 *     number of adapters; a too-small non-NULL array is BUFFER_TOO_SMALL, writes no handle and leaves the count;
 *   - v1, v2 and v3 (filter 3) report the same number of adapters and the same set of LUIDs, the default v3 filter 0 a subset; handles are non-zero and
 *     distinct, LUIDs are non-zero and pairwise unique, two enumerations return the same LUID set with fresh handles,
 *     every handle closes once and a second close fails;
 *   - the v3 filter takes bits 0 and 1 only (values 0..3 succeed; 4 and above, and any upper-half bit, are
 *     INVALID_PARAMETER) and the set for filter 3 contains the sets for filters 1, 2 and 0 and is the set EnumAdapters2 returns;
 *   - the gdi32 routes agree.
 * A session without any adapter skips the tests that need one.
 *
 * Determinism: only relations over counts and LUIDs are checked; no count, handle or LUID is printed.
 */
#include "x.h"

static int luid_eq(LUID a, LUID b) { return a.LowPart == b.LowPart && a.HighPart == b.HighPart; }
static int luid_zero(LUID a) { return a.LowPart == 0 && a.HighPart == 0; }
static int has_luid(const x_adapterinfo *a, int n, LUID l) { for (int i = 0; i < n; i++) if (luid_eq(a[i].luid, l)) return 1; return 0; }
static int subset(const x_adapterinfo *a, int na, const x_adapterinfo *b, int nb) { for (int i = 0; i < na; i++) if (!has_luid(b, nb, a[i].luid)) return 0; return 1; }

/* sizing capacity reported by v2 */
static int v2_bound(void) {
    x_fn f = x_nt("NtGdiDdDDIEnumAdapters2");
    x_enum2 e = { 0, 0, NULL };
    if (!f || f(&e) < 0) return -1;
    return (int)e.NumAdapters;
}
static int v3_fill(ULONGLONG filter, x_adapterinfo *out32, LONG *st_out) {
    x_fn f = x_nt("NtDxgkEnumAdapters3");
    x_enum3 e = { filter, 32, 0, out32 };
    memset(out32, 0, 32 * sizeof *out32);
    LONG st = f ? f(&e) : (LONG)0xC0000BAD;
    if (st_out) *st_out = st;
    return st >= 0 ? (int)e.NumAdapters : -1;
}
static int v1_fill(x_adapterinfo *out16, LONG *st_out) {
    x_fn f = x_nt("NtGdiDdDDIEnumAdapters");
    x_enum1 *e = x_edge(sizeof(x_enum1));
    LONG st = f ? f(e) : (LONG)0xC0000BAD;
    if (st_out) *st_out = st;
    if (st < 0) return -1;
    int n = (int)e->NumAdapters;
    memcpy(out16, e->Adapters, 16 * sizeof(x_adapterinfo));
    return n;
}

/* ---------------- v1 ---------------- */
TEST(x_enum1_stubs_well_formed) {
    XREQ(f, "NtGdiDdDDIEnumAdapters"); XREQG(g, "EnumAdapters");
    CHECK(k_stub_valid((FARPROC)f));
    EXPECT_EQ(k_stub_number((FARPROC)g), k_stub_number((FARPROC)f));
}
TEST(x_enum1_short_blocks_invalid_parameter) {
    XREQ(f, "NtGdiDdDDIEnumAdapters");
    X_ST(f(NULL), XS_INVALID_PARAMETER, "NULL");
    for (unsigned n = 4; n < sizeof(x_enum1); n += 4) X_ST(f(x_edge(n)), XS_INVALID_PARAMETER, "%u readable bytes", n);
}
TEST(x_enum1_wild_pointers_invalid_parameter) {
    XREQ(f, "NtGdiDdDDIEnumAdapters");
    void *wild[] = { (void *)(ULONG_PTR)1, (void *)(ULONG_PTR)0xFFFF800000000000ull, x_freed(), (void *)~(ULONG_PTR)0 };
    for (size_t i = 0; i < XARR(wild); i++) if (wild[i]) X_ST(f(wild[i]), XS_INVALID_PARAMETER, "wild pointer %zu", i);
}
TEST(x_enum1_exact_size_succeeds_and_handles_close) {
    XREQ(f, "NtGdiDdDDIEnumAdapters");
    x_adapterinfo a[16]; LONG st;
    int n = v1_fill(a, &st);
    X_ST(st, XS_SUCCESS, "exact size block");
    if (n < 0) return;
    CHECKF(n >= 0 && n <= 16, "count outside 0..16");
    for (int i = 0; i < n; i++) {
        CHECKF(a[i].hAdapter != 0, "adapter %d has no handle", i);
        X_ST(x_close_adapter(a[i].hAdapter), XS_SUCCESS, "close adapter %d", i);
    }
}
TEST(x_enum1_count_matches_enum2) {
    XREQ(f, "NtGdiDdDDIEnumAdapters");
    x_adapterinfo a[16], b[32]; LONG st;
    int n1 = v1_fill(a, &st), n2 = x_adapters_open(b);
    XNEED(n1 >= 0 && n2 >= 0);
    if (n2 <= 16) EXPECT_EQ(n1, n2);
    x_adapters_close(a, n1 > 16 ? 16 : n1); x_adapters_close(b, n2);
}
TEST(x_enum1_luids_match_enum2) {
    XREQ(f, "NtGdiDdDDIEnumAdapters");
    x_adapterinfo a[16], b[32]; LONG st;
    int n1 = v1_fill(a, &st), n2 = x_adapters_open(b);
    XNEED(n1 >= 0 && n2 >= 0);
    if (n2 <= 16) { CHECK(subset(a, n1, b, n2)); CHECK(subset(b, n2, a, n1)); }
    x_adapters_close(a, n1 > 16 ? 16 : n1); x_adapters_close(b, n2);
}
TEST(x_enum1_handles_distinct_and_luids_unique) {
    XREQ(f, "NtGdiDdDDIEnumAdapters");
    x_adapterinfo a[16]; LONG st;
    int n = v1_fill(a, &st);
    XNEED(n >= 0);
    for (int i = 0; i < n; i++) {
        CHECK(!luid_zero(a[i].luid));
        for (int j = i + 1; j < n; j++) { CHECK(a[i].hAdapter != a[j].hAdapter); CHECK(!luid_eq(a[i].luid, a[j].luid)); }
    }
    x_adapters_close(a, n);
}
TEST(x_enum1_gdi32_route_matches) {
    XREQG(g, "EnumAdapters");
    X_ST(g(NULL), XS_INVALID_PARAMETER, "gdi32 NULL");
    X_ST(g(x_edge(0x100)), XS_INVALID_PARAMETER, "gdi32 short block");
    x_enum1 *e = x_edge(sizeof *e);
    LONG st = g(e);
    X_ST(st, XS_SUCCESS, "gdi32 full block");
    if (st >= 0) for (DWORD i = 0; i < e->NumAdapters && i < 16; i++) x_close_adapter(e->Adapters[i].hAdapter);
}

/* ---------------- v2 ---------------- */
TEST(x_enum2_stubs_well_formed) {
    XREQ(f, "NtGdiDdDDIEnumAdapters2"); XREQG(g, "EnumAdapters2");
    CHECK(k_stub_valid((FARPROC)f));
    EXPECT_EQ(k_stub_number((FARPROC)g), k_stub_number((FARPROC)f));
}
TEST(x_enum2_short_blocks_invalid_parameter) {
    XREQ(f, "NtGdiDdDDIEnumAdapters2");
    X_ST(f(NULL), XS_INVALID_PARAMETER, "NULL");
    for (unsigned n = 1; n < 0x10; n++) X_ST(f(x_edge(n)), XS_INVALID_PARAMETER, "%u readable bytes", n);
    X_ST(f(x_edge(0x10)), XS_SUCCESS, "exact size");
}
TEST(x_enum2_wild_pointers_invalid_parameter) {
    XREQ(f, "NtGdiDdDDIEnumAdapters2");
    void *wild[] = { (void *)(ULONG_PTR)1, (void *)(ULONG_PTR)0xFFFF800000000000ull, x_freed(), (void *)~(ULONG_PTR)0 };
    for (size_t i = 0; i < XARR(wild); i++) if (wild[i]) X_ST(f(wild[i]), XS_INVALID_PARAMETER, "wild pointer %zu", i);
}
TEST(x_enum2_sizing_succeeds_for_any_incoming_count) {
    XREQ(f, "NtGdiDdDDIEnumAdapters2");
    static const DWORD in[] = { 0, 1, 2, 16, 32, 33, 100, 1000, 0x7FFFFFFF, 0xFFFFFFFF };
    int base = v2_bound();
    XNEED(base >= 0); if (base == 0) { t_skip("no adapters in this session"); return; }
    for (unsigned i = 0; i < XARR(in); i++) {
        x_enum2 e = { in[i], 0, NULL };
        X_ST(f(&e), XS_SUCCESS, "sizing with incoming count %u", i);
        EXPECT_EQ(e.NumAdapters, (DWORD)base);
        CHECK(e.pAdapters == NULL);
    }
}
TEST(x_enum2_sizing_is_repeatable_and_upper_bound) {
    int b = v2_bound();
    XNEED(b >= 0); if (b == 0) { t_skip("no adapters in this session"); return; }
    for (int i = 0; i < 10; i++) EXPECT_EQ(v2_bound(), b);
    x_adapterinfo a[32]; int n = x_adapters_open(a);
    XNEED(n >= 0);
    CHECKF(n <= b, "adapter count exceeds the sizing capacity");
    x_adapters_close(a, n);
}
TEST(x_enum2_fill_basic_invariants) {
    x_adapterinfo a[32]; int n = x_adapters_open(a);
    XNEED(n >= 0);
    if (n == 0) { t_skip("no adapters in this session"); return; }
    for (int i = 0; i < n; i++) {
        CHECKF(a[i].hAdapter != 0, "adapter %d has no handle", i);
        CHECKF(!luid_zero(a[i].luid), "adapter %d has a zero LUID", i);
    }
    x_adapters_close(a, n);
}
TEST(x_enum2_fill_handles_distinct_luids_unique) {
    x_adapterinfo a[32]; int n = x_adapters_open(a);
    XNEED(n >= 0);
    if (n < 2) { x_adapters_close(a, n); t_skip("fewer than two adapters"); return; }
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) { CHECK(a[i].hAdapter != a[j].hAdapter); CHECK(!luid_eq(a[i].luid, a[j].luid)); }
    x_adapters_close(a, n);
}
TEST(x_enum2_fill_writes_no_more_entries_than_reported) {
    x_fn f = x_nt("NtGdiDdDDIEnumAdapters2");
    REQUIRE(f);
    x_adapterinfo a[34]; memset(a, 0x5A, sizeof a);
    x_enum2 e = { 32, 0, a };
    LONG st = f(&e);
    X_ST(st, XS_SUCCESS, "fill");
    if (st < 0) return;
    for (DWORD i = e.NumAdapters; i < 34; i++) { unsigned char *p = (unsigned char *)&a[i]; for (size_t k = 0; k < sizeof a[i]; k++) if (p[k] != 0x5A) { CHECKF(0, "entry %lu beyond the count was written", (unsigned long)i); break; } }
    x_adapters_close(a, e.NumAdapters);
}
TEST(x_enum2_too_small_array_buffer_too_small) {
    XREQ(f, "NtGdiDdDDIEnumAdapters2");
    x_adapterinfo a[34]; int n = x_adapters_open(a);
    XNEED(n >= 0); if (n == 0) { t_skip("no adapters in this session"); return; }
    x_adapters_close(a, n);
    memset(a, 0x5A, sizeof a);
    x_enum2 e = { (DWORD)(n - 1), 0, a };
    X_ST(f(&e), XS_BUFFER_TOO_SMALL, "capacity one short");
    unsigned char *p = (unsigned char *)a;
    int touched = 0; for (size_t k = 0; k < sizeof a; k++) if (p[k] != 0x5A) touched = 1;
    CHECKF(!touched, "a failing call wrote into the array");
    EXPECT_EQ(e.NumAdapters, (DWORD)(n - 1));
}
TEST(x_enum2_zero_capacity_with_array_buffer_too_small) {
    XREQ(f, "NtGdiDdDDIEnumAdapters2");
    x_adapterinfo a[2]; memset(a, 0, sizeof a);
    x_enum2 e = { 0, 0, a };
    X_ST(f(&e), XS_BUFFER_TOO_SMALL, "capacity zero with an array");
    EXPECT_EQ(e.NumAdapters, 0);
    e.pAdapters = (x_adapterinfo *)(ULONG_PTR)1;
    X_ST(f(&e), XS_BUFFER_TOO_SMALL, "capacity zero with array address 1");
}
TEST(x_enum2_oversized_capacity_reports_actual_count) {
    x_adapterinfo a[32]; int n = x_adapters_open(a);
    XNEED(n >= 0);
    x_adapters_close(a, n);
    x_fn f = x_nt("NtGdiDdDDIEnumAdapters2");
    x_adapterinfo b[34];
    x_enum2 e = { 34, 0, b };
    memset(b, 0, sizeof b);
    LONG st = f(&e);
    X_ST(st, XS_SUCCESS, "capacity larger than the sizing capacity");
    if (st >= 0) { EXPECT_EQ(e.NumAdapters, (DWORD)n); x_adapters_close(b, e.NumAdapters); }
}
TEST(x_enum2_two_fills_same_luids_fresh_handles) {
    x_adapterinfo a[32], b[32]; int n = x_adapters_open(a), m = x_adapters_open(b);
    XNEED(n >= 0 && m >= 0);
    EXPECT_EQ(n, m);
    if (n == m) { CHECK(subset(a, n, b, m)); CHECK(subset(b, m, a, n)); }
    for (int i = 0; i < n; i++) for (int j = 0; j < m; j++) CHECKF(a[i].hAdapter != b[j].hAdapter, "two live handles share a value");
    x_adapters_close(a, n); x_adapters_close(b, m);
}
TEST(x_enum2_handle_closes_once) {
    x_adapterinfo a[32]; int n = x_adapters_open(a);
    XNEED(n >= 0);
    if (n == 0) { t_skip("no adapters in this session"); return; }
    X_ST(x_close_adapter(a[0].hAdapter), XS_SUCCESS, "first close");
    CHECKF(x_close_adapter(a[0].hAdapter) < 0, "second close of the same handle succeeded");
    for (int i = 1; i < n; i++) x_close_adapter(a[i].hAdapter);
}
TEST(x_enum2_fill_does_not_create_gdi_objects) {
    REQUIRE(U(GetGuiResources));
    DWORD g0 = U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS), u0 = U(GetGuiResources)(GetCurrentProcess(), GR_USEROBJECTS);
    x_adapterinfo a[32]; int n = x_adapters_open(a);
    XNEED(n >= 0);
    EXPECT_EQ(U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS), g0);
    EXPECT_EQ(U(GetGuiResources)(GetCurrentProcess(), GR_USEROBJECTS), u0);
    x_adapters_close(a, n);
}
TEST(x_enum2_gdi32_route_matches) {
    XREQG(g, "EnumAdapters2");
    X_ST(g(NULL), XS_INVALID_PARAMETER, "gdi32 NULL");
    x_enum2 e = { 0, 0, NULL };
    X_ST(g(&e), XS_SUCCESS, "gdi32 sizing");
    EXPECT_EQ(e.NumAdapters, (DWORD)v2_bound());
    x_adapterinfo a[32]; memset(a, 0, sizeof a);
    x_enum2 f = { 32, 0, a };
    X_ST(g(&f), XS_SUCCESS, "gdi32 fill");
    x_adapters_close(a, f.NumAdapters);
}
TEST(x_enum2_padding_dword_ignored) {
    XREQ(f, "NtGdiDdDDIEnumAdapters2");
    x_enum2 *e = x_edge(0x10);
    e->NumAdapters = 0; e->pad = 0xFFFFFFFF; e->pAdapters = NULL;
    X_ST(f(e), XS_SUCCESS, "pad dword all ones");
}

/* ---------------- v3 ---------------- */
TEST(x_enum3_stubs_well_formed) {
    XREQ(f, "NtDxgkEnumAdapters3"); XREQG(g, "EnumAdapters3");
    CHECK(k_stub_valid((FARPROC)f));
    EXPECT_EQ(k_stub_number((FARPROC)g), k_stub_number((FARPROC)f));
}
TEST(x_enum3_short_blocks_invalid_parameter) {
    XREQ(f, "NtDxgkEnumAdapters3");
    X_ST(f(NULL), XS_INVALID_PARAMETER, "NULL");
    for (unsigned n = 1; n < 0x18; n++) X_ST(f(x_edge(n)), XS_INVALID_PARAMETER, "%u readable bytes", n);
    X_ST(f(x_edge(0x18)), XS_SUCCESS, "exact size");
}
TEST(x_enum3_wild_pointers_invalid_parameter) {
    XREQ(f, "NtDxgkEnumAdapters3");
    void *wild[] = { (void *)(ULONG_PTR)1, (void *)(ULONG_PTR)0xFFFF800000000000ull, x_freed(), (void *)~(ULONG_PTR)0 };
    for (size_t i = 0; i < XARR(wild); i++) if (wild[i]) X_ST(f(wild[i]), XS_INVALID_PARAMETER, "wild pointer %zu", i);
}
TEST(x_enum3_filter_values_0_to_3_accepted) {
    XREQ(f, "NtDxgkEnumAdapters3");
    for (ULONGLONG fl = 0; fl <= 3; fl++) { x_enum3 e = { fl, 0, 0, NULL }; X_ST(f(&e), XS_SUCCESS, "sizing, filter %llu", fl); }
}
TEST(x_enum3_filter_4_and_above_invalid_parameter) {
    XREQ(f, "NtDxgkEnumAdapters3");
    static const ULONGLONG fl[] = { 4, 5, 6, 7, 8, 0x10, 0x80000000ull, 0xFFFFFFFFull };
    for (unsigned i = 0; i < XARR(fl); i++) { x_enum3 e = { fl[i], 0, 0, NULL }; X_ST(f(&e), XS_INVALID_PARAMETER, "filter 0x%llx", fl[i]); }
}
TEST(x_enum3_filter_upper_half_invalid_parameter) {
    XREQ(f, "NtDxgkEnumAdapters3");
    static const ULONGLONG fl[] = { 1ull << 32, (1ull << 32) | 1, (1ull << 32) | 3, 0xFFFFFFFF00000000ull, 0x8000000000000000ull };
    for (unsigned i = 0; i < XARR(fl); i++) { x_enum3 e = { fl[i], 0, 0, NULL }; X_ST(f(&e), XS_INVALID_PARAMETER, "filter %u with upper half", i); }
}
TEST(x_enum3_sizing_matches_enum2_capacity) {
    XREQ(f, "NtDxgkEnumAdapters3");
    int b = v2_bound();
    XNEED(b >= 0); if (b == 0) { t_skip("no adapters in this session"); return; }
    for (ULONGLONG fl = 0; fl <= 3; fl++) {
        x_enum3 e = { fl, 0xFFFFFFFF, 0, NULL };
        X_ST(f(&e), XS_SUCCESS, "sizing, filter %llu", fl);
        EXPECT_EQ(e.NumAdapters, (DWORD)b);
    }
}
TEST(x_enum3_zero_capacity_with_array_buffer_too_small) {
    XREQ(f, "NtDxgkEnumAdapters3");
    x_adapterinfo a[2]; memset(a, 0, sizeof a);
    for (ULONGLONG fl = 0; fl <= 3; fl++) {
        x_enum3 e = { fl, 0, 0, a };
        X_ST(f(&e), XS_BUFFER_TOO_SMALL, "filter %llu, capacity zero with an array", fl);
        EXPECT_EQ(e.NumAdapters, 0);
    }
}
TEST(x_enum3_filter_checked_before_array) {
    XREQ(f, "NtDxgkEnumAdapters3");
    x_adapterinfo a[2];
    x_enum3 e = { 4, 0, 0, a };
    X_ST(f(&e), XS_INVALID_PARAMETER, "bad filter with an array");
}
TEST(x_enum3_filter0_is_inside_enum2) {
    XREQ(f3, "NtDxgkEnumAdapters3");
    x_adapterinfo a[32], b[32]; LONG st;
    int n3 = v3_fill(0, a, &st), n2 = x_adapters_open(b);
    X_ST(st, XS_SUCCESS, "filter 0 fill");
    XNEED(n3 >= 0 && n2 >= 0);
    CHECK(n3 <= n2);
    CHECK(subset(a, n3, b, n2));
    x_adapters_close(a, n3); x_adapters_close(b, n2);
}
TEST(x_enum3_filter3_equals_enum2) {
    XREQ(f3, "NtDxgkEnumAdapters3");
    x_adapterinfo a[32], b[32]; LONG st;
    int n3 = v3_fill(3, a, &st), n2 = x_adapters_open(b);
    X_ST(st, XS_SUCCESS, "filter 3 fill");
    XNEED(n3 >= 0 && n2 >= 0);
    EXPECT_EQ(n3, n2);
    if (n3 == n2) { CHECK(subset(a, n3, b, n2)); CHECK(subset(b, n2, a, n3)); }
    x_adapters_close(a, n3); x_adapters_close(b, n2);
}
TEST(x_enum3_filter0_is_inside_enum1) {
    XREQ(f3, "NtDxgkEnumAdapters3");
    x_adapterinfo a[32], b[16]; LONG st;
    int n3 = v3_fill(0, a, &st), n1 = v1_fill(b, &st);
    XNEED(n3 >= 0 && n1 >= 0);
    if (n1 <= 16) { CHECK(n3 <= n1); CHECK(subset(a, n3, b, n1)); }
    x_adapters_close(a, n3); x_adapters_close(b, n1 > 16 ? 16 : n1);
}
TEST(x_enum3_filter3_contains_every_other_filter) {
    XREQ(f3, "NtDxgkEnumAdapters3");
    x_adapterinfo s[4][32]; int n[4]; LONG st;
    for (int fl = 0; fl < 4; fl++) { n[fl] = v3_fill(fl, s[fl], &st); X_ST(st, XS_SUCCESS, "filter %d", fl); }
    for (int fl = 0; fl < 4; fl++) if (n[fl] < 0) { for (int k = 0; k < 4; k++) if (n[k] > 0) x_adapters_close(s[k], n[k]); t_skip("a fill failed"); return; }
    for (int fl = 0; fl < 3; fl++) CHECKF(subset(s[fl], n[fl], s[3], n[3]), "filter %d set not inside filter 3 set", fl);
    EXPECT_NE(n[3] < n[0], 1);
    for (int fl = 0; fl < 4; fl++) x_adapters_close(s[fl], n[fl]);
}
TEST(x_enum3_filter_sets_are_consistent_across_calls) {
    for (int fl = 0; fl < 4; fl++) {
        x_adapterinfo a[32], b[32]; LONG st;
        int n = v3_fill(fl, a, &st), m = v3_fill(fl, b, &st);
        XNEED(n >= 0 && m >= 0);
        EXPECT_EQ(n, m);
        if (n == m) { CHECK(subset(a, n, b, m)); }
        x_adapters_close(a, n); x_adapters_close(b, m);
    }
}
TEST(x_enum3_handles_distinct_luids_unique) {
    x_adapterinfo a[32]; LONG st;
    int n = v3_fill(3, a, &st);
    XNEED(n >= 0);
    for (int i = 0; i < n; i++) {
        CHECK(a[i].hAdapter != 0); CHECK(!luid_zero(a[i].luid));
        for (int j = i + 1; j < n; j++) { CHECK(a[i].hAdapter != a[j].hAdapter); CHECK(!luid_eq(a[i].luid, a[j].luid)); }
    }
    x_adapters_close(a, n);
}
TEST(x_enum3_too_small_array_buffer_too_small) {
    XREQ(f, "NtDxgkEnumAdapters3");
    x_adapterinfo a[34]; LONG st;
    int n = v3_fill(0, a, &st);
    XNEED(n >= 0); if (n == 0) { t_skip("no adapters in this session"); return; }
    x_adapters_close(a, n);
    memset(a, 0x5A, sizeof a);
    x_enum3 e = { 0, (DWORD)(n - 1), 0, a };
    X_ST(f(&e), XS_BUFFER_TOO_SMALL, "capacity one short");
    unsigned char *p = (unsigned char *)a; int touched = 0;
    for (size_t k = 0; k < sizeof a; k++) if (p[k] != 0x5A) touched = 1;
    CHECKF(!touched, "a failing call wrote into the array");
}
TEST(x_enum3_gdi32_route_matches) {
    XREQG(g, "EnumAdapters3");
    X_ST(g(NULL), XS_INVALID_PARAMETER, "gdi32 NULL");
    x_enum3 e = { 0, 0, 0, NULL };
    X_ST(g(&e), XS_SUCCESS, "gdi32 sizing");
    EXPECT_EQ(e.NumAdapters, (DWORD)v2_bound());
    x_enum3 bad = { 4, 0, 0, NULL };
    X_ST(g(&bad), XS_INVALID_PARAMETER, "gdi32 bad filter");
}
TEST(x_enum_versions_leave_gdi_counts_alone) {
    REQUIRE(U(GetGuiResources));
    DWORD g0 = U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS);
    x_adapterinfo a[32], b[16]; LONG st;
    int n3 = v3_fill(3, a, &st), n1 = v1_fill(b, &st);
    EXPECT_EQ(U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS), g0);
    x_adapters_close(a, n3); x_adapters_close(b, n1 > 16 ? 16 : n1);
}
