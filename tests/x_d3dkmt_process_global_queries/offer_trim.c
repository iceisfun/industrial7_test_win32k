/* x_d3dkmt_process_global_queries / offer_trim: QueryProcessOfferInfo and TrimProcessCommitment.
 *
 * Both take a block whose first 8 bytes are a "selector" qword, whose bytes 8..15 are a process handle and whose
 * remaining bytes are flags and output counters. Measures, for each thunk:
 *   - the minimum readable size (0x20 bytes for the offer query, 0x28 for the trim) below which the answer is
 *     INVALID_PARAMETER, and the answer at the boundary;
 *   - the selector (its low dword; the upper dword is ignored): zero and small values (1..0x10) are
 *     INVALID_PARAMETER before the handle is looked at; larger values (0x108, 0x1000, all ones) let the handle decide;
 *   - the handle: a full-access handle and the pseudo handle succeed, a query-limited handle succeeds for the offer
 *     query but is ACCESS_DENIED for the trim (which needs more rights), a synchronize-only handle is ACCESS_DENIED,
 *     an event or thread handle is OBJECT_TYPE_MISMATCH, the null and a closed handle are INVALID_HANDLE;
 *   - the flags dword of the trim at offset 0x10 accepts 0..3 and rejects 4 and above with INVALID_PARAMETER; the
 *     offer query ignores that dword;
 *   - the gdi32 route gives the same answers and wild pointers are INVALID_PARAMETER.
 *
 * Determinism: statuses only.
 */
#include "x.h"

typedef struct { const char *nt, *gdi; unsigned min; } oapi;
static const oapi OFFER = { "NtGdiDdDDIQueryProcessOfferInfo", "QueryProcessOfferInfo", 0x20 };
static const oapi TRIM  = { "NtGdiDdDDITrimProcessCommitment", "TrimProcessCommitment", 0x28 };

#define SEL_OK 0x1000

static unsigned char *blk(unsigned size, ULONGLONG sel, HANDLE h, DWORD flags) {
    unsigned char *p = x_edge(size);
    *(ULONGLONG *)p = sel; *(HANDLE *)(p + 8) = h; *(DWORD *)(p + 0x10) = flags;
    return p;
}

static void o_stub(const oapi *a) {
    x_fn f = x_nt(a->nt);
    if (!f) { t_skip("win32u export not available"); return; }
    CHECK(k_stub_valid((FARPROC)f));
    x_fn g = x_gdi(a->gdi);
    CHECK(g != NULL);
    if (g && k_stub_valid((FARPROC)g)) EXPECT_EQ(k_stub_number((FARPROC)g), k_stub_number((FARPROC)f));
}
static void o_short(const oapi *a) {
    x_fn f = x_nt(a->nt);
    if (!f) { t_skip("win32u export not available"); return; }
    X_ST(f(NULL), XS_INVALID_PARAMETER, "NULL");
    x_hset h; x_hset_init(&h);
    for (unsigned n = 1; n < a->min; n++) {
        unsigned char *p = x_edge(n);
        if (n >= 8) *(ULONGLONG *)p = SEL_OK;
        if (n >= 16) *(HANDLE *)(p + 8) = h.real;
        X_ST(f(p), XS_INVALID_PARAMETER, "%u readable bytes", n);
    }
    x_hset_free(&h);
}
static void o_boundary(const oapi *a) {
    x_fn f = x_nt(a->nt);
    if (!f) { t_skip("win32u export not available"); return; }
    x_hset h; x_hset_init(&h);
    /* with the null handle the block gets as far as the handle lookup */
    X_ST(f(blk(a->min, SEL_OK, NULL, 0)), XS_INVALID_HANDLE, "exact size, null handle");
    X_ST(f(blk(a->min - 4, SEL_OK, NULL, 0)), XS_INVALID_PARAMETER, "four bytes short, null handle");
    X_ST(f(blk(a->min, SEL_OK, h.real, 0)), XS_SUCCESS, "exact size, real handle");
    X_ST(f(blk(0x100, SEL_OK, h.real, 0)), XS_SUCCESS, "0x100 bytes, real handle");
    X_ST(f(blk(0x100, SEL_OK, NULL, 0)), XS_INVALID_HANDLE, "0x100 bytes, null handle");
    x_hset_free(&h);
}
static void o_wild(const oapi *a) {
    x_fn f = x_nt(a->nt);
    if (!f) { t_skip("win32u export not available"); return; }
    void *wild[] = { (void *)(ULONG_PTR)1, (void *)(ULONG_PTR)0xFFFF800000000000ull, x_freed(), (void *)~(ULONG_PTR)0 };
    for (size_t i = 0; i < XARR(wild); i++) if (wild[i]) X_ST(f(wild[i]), XS_INVALID_PARAMETER, "wild pointer %zu", i);
}
static void o_selector_small(const oapi *a) {
    x_fn f = x_nt(a->nt);
    if (!f) { t_skip("win32u export not available"); return; }
    x_hset h; x_hset_init(&h);
    static const ULONGLONG sel[] = { 0, 1, 2, 3, 4, 5, 6, 8, 0x10 };
    for (unsigned i = 0; i < XARR(sel); i++) {
        X_ST(f(blk(0x40, sel[i], h.real, 0)), XS_INVALID_PARAMETER, "selector 0x%llx, real handle", sel[i]);
        X_ST(f(blk(0x40, sel[i], NULL, 0)), XS_INVALID_PARAMETER, "selector 0x%llx, null handle", sel[i]);
    }
    x_hset_free(&h);
}
static void o_selector_upper(const oapi *a) {
    x_fn f = x_nt(a->nt);
    if (!f) { t_skip("win32u export not available"); return; }
    x_hset h; x_hset_init(&h);
    /* only the low dword is the selector: the upper dword is ignored, and a zero low dword is rejected whatever the upper one holds */
    static const ULONGLONG ok[] = { 0x100001000ull, 0x8000000000001000ull, 0x7FFFFFFF00000108ull, 0xFFFFFFFF00001000ull };
    static const ULONGLONG bad[] = { 0x100000000ull, 0xFFFFFFFF00000000ull, 0x8000000000000000ull, 0x700000001ull };
    for (unsigned i = 0; i < XARR(ok); i++) X_ST(f(blk(0x40, ok[i], h.real, 0)), XS_SUCCESS, "selector %u, upper half set", i);
    for (unsigned i = 0; i < XARR(bad); i++) X_ST(f(blk(0x40, bad[i], h.real, 0)), XS_INVALID_PARAMETER, "selector %u, low dword small", i);
    x_hset_free(&h);
}
static void o_selector_large(const oapi *a) {
    x_fn f = x_nt(a->nt);
    if (!f) { t_skip("win32u export not available"); return; }
    x_hset h; x_hset_init(&h);
    static const ULONGLONG sel[] = { 0x108, 0x1000, 0x10000, 0x7FFFFFFF, 0xFFFFFFFF, 0x12345678 };
    for (unsigned i = 0; i < XARR(sel); i++) {
        X_ST(f(blk(0x40, sel[i], h.real, 0)), XS_SUCCESS, "selector %u, real handle", i);
        X_ST(f(blk(0x40, sel[i], NULL, 0)), XS_INVALID_HANDLE, "selector %u, null handle", i);
    }
    x_hset_free(&h);
}
static void o_handles(const oapi *a, LONG want_limited) {
    x_fn f = x_nt(a->nt);
    if (!f) { t_skip("win32u export not available"); return; }
    x_hset h; x_hset_init(&h);
    X_ST(f(blk(0x40, SEL_OK, h.real, 0)), XS_SUCCESS, "full access handle");
    X_ST(f(blk(0x40, SEL_OK, h.pseudo, 0)), XS_SUCCESS, "pseudo handle");
    X_ST(f(blk(0x40, SEL_OK, h.limited, 0)), want_limited, "query-limited handle");
    X_ST(f(blk(0x40, SEL_OK, h.sync, 0)), XS_ACCESS_DENIED, "synchronize-only handle");
    X_ST(f(blk(0x40, SEL_OK, h.event, 0)), XS_TYPE_MISMATCH, "event handle");
    X_ST(f(blk(0x40, SEL_OK, h.thread, 0)), XS_TYPE_MISMATCH, "thread handle");
    X_ST(f(blk(0x40, SEL_OK, NULL, 0)), XS_INVALID_HANDLE, "null handle");
    X_ST(f(blk(0x40, SEL_OK, h.closed, 0)), XS_INVALID_HANDLE, "closed handle");
    x_hset_free(&h);
}
static void o_handle_upper(const oapi *a) {
    x_fn f = x_nt(a->nt);
    if (!f) { t_skip("win32u export not available"); return; }
    x_hset h; x_hset_init(&h);
    static const ULONGLONG hi[] = { 1ull << 32, 0xDEADBEEFull << 32 };
    for (unsigned i = 0; i < XARR(hi); i++)
        X_ST(f(blk(0x40, SEL_OK, (HANDLE)((ULONG_PTR)h.real | hi[i]), 0)), XS_INVALID_HANDLE, "handle with upper half %u", i);
    x_hset_free(&h);
}
static void o_small_handles(const oapi *a) {
    x_fn f = x_nt(a->nt);
    if (!f) { t_skip("win32u export not available"); return; }
    for (ULONG_PTR v = 1; v <= 3; v++) X_ST(f(blk(0x40, SEL_OK, (HANDLE)v, 0)), XS_INVALID_HANDLE, "handle value %lu", (unsigned long)v);
}
static void o_gdi(const oapi *a, LONG want_limited) {
    x_fn g = x_gdi(a->gdi);
    if (!g) { t_skip("gdi32 export not available"); return; }
    x_hset h; x_hset_init(&h);
    X_ST(g(NULL), XS_INVALID_PARAMETER, "gdi32 NULL");
    X_ST(g(blk(0x40, SEL_OK, h.real, 0)), XS_SUCCESS, "gdi32 real");
    X_ST(g(blk(0x40, SEL_OK, h.limited, 0)), want_limited, "gdi32 limited");
    X_ST(g(blk(0x40, SEL_OK, h.sync, 0)), XS_ACCESS_DENIED, "gdi32 sync");
    X_ST(g(blk(0x40, SEL_OK, h.event, 0)), XS_TYPE_MISMATCH, "gdi32 event");
    X_ST(g(blk(0x40, SEL_OK, NULL, 0)), XS_INVALID_HANDLE, "gdi32 null");
    X_ST(g(blk(0x40, 0, h.real, 0)), XS_INVALID_PARAMETER, "gdi32 zero selector");
    x_hset_free(&h);
}
static void o_repeat(const oapi *a) {
    x_fn f = x_nt(a->nt);
    if (!f) { t_skip("win32u export not available"); return; }
    x_hset h; x_hset_init(&h);
    for (int i = 0; i < 20; i++) X_ST(f(blk(0x40, SEL_OK, h.real, 0)), XS_SUCCESS, "call %d", i);
    x_hset_free(&h);
}
static void o_failure_untouched(const oapi *a) {
    x_fn f = x_nt(a->nt);
    if (!f) { t_skip("win32u export not available"); return; }
    x_hset h; x_hset_init(&h);
    HANDLE bad[] = { NULL, h.event, h.sync };
    for (unsigned i = 0; i < XARR(bad); i++) {
        unsigned char *p = blk(0x40, SEL_OK, bad[i], 0), q[0x40];
        memset(p + 0x18, 0x5A, 0x28); memcpy(q, p, sizeof q);
        LONG st = f(p);
        CHECKF(st < 0, "case %u unexpectedly succeeded", i);
        CHECKF(!memcmp(p, q, sizeof q), "case %u: a failing call wrote into the block", i);
    }
    x_hset_free(&h);
}

#define GEN(tag, A, LIM) \
TEST(x_##tag##_stubs_well_formed)               { o_stub(&A); } \
TEST(x_##tag##_short_blocks_invalid_parameter)  { o_short(&A); } \
TEST(x_##tag##_size_boundary)                   { o_boundary(&A); } \
TEST(x_##tag##_wild_pointers)                   { o_wild(&A); } \
TEST(x_##tag##_small_selector_invalid_parameter){ o_selector_small(&A); } \
TEST(x_##tag##_selector_uses_the_low_dword)    { o_selector_upper(&A); } \
TEST(x_##tag##_large_selector_lets_handle_decide){ o_selector_large(&A); } \
TEST(x_##tag##_handle_kinds)                    { o_handles(&A, LIM); } \
TEST(x_##tag##_handle_upper_half_invalid)       { o_handle_upper(&A); } \
TEST(x_##tag##_small_handle_values_invalid)     { o_small_handles(&A); } \
TEST(x_##tag##_gdi32_route_matches)             { o_gdi(&A, LIM); } \
TEST(x_##tag##_repeat_is_stable)                { o_repeat(&A); } \
TEST(x_##tag##_failure_leaves_block_untouched)  { o_failure_untouched(&A); }
GEN(offer, OFFER, XS_SUCCESS)
GEN(trim,  TRIM,  XS_ACCESS_DENIED)

/* trim only: the flags dword at offset 0x10 */
TEST(x_trim_flags_0_to_3_accepted) {
    XREQ(f, "NtGdiDdDDITrimProcessCommitment");
    x_hset h; x_hset_init(&h);
    for (DWORD fl = 0; fl <= 3; fl++) X_ST(f(blk(0x40, SEL_OK, h.real, fl)), XS_SUCCESS, "flags %lu", (unsigned long)fl);
    x_hset_free(&h);
}
TEST(x_trim_flags_4_and_above_invalid_parameter) {
    XREQ(f, "NtGdiDdDDITrimProcessCommitment");
    x_hset h; x_hset_init(&h);
    static const DWORD fl[] = { 4, 5, 6, 8, 0x10, 0x1000, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF };
    for (unsigned i = 0; i < XARR(fl); i++) X_ST(f(blk(0x40, SEL_OK, h.real, fl[i])), XS_INVALID_PARAMETER, "flags 0x%lx", (unsigned long)fl[i]);
    x_hset_free(&h);
}
TEST(x_trim_flags_checked_before_handle) {
    XREQ(f, "NtGdiDdDDITrimProcessCommitment");
    X_ST(f(blk(0x40, SEL_OK, NULL, 4)), XS_INVALID_PARAMETER, "null handle, flags 4");
    X_ST(f(blk(0x40, SEL_OK, NULL, 3)), XS_INVALID_HANDLE, "null handle, flags 3");
}
TEST(x_offer_ignores_dword_at_0x10) {
    XREQ(f, "NtGdiDdDDIQueryProcessOfferInfo");
    x_hset h; x_hset_init(&h);
    static const DWORD v[] = { 0, 1, 2, 3, 4, 0x1000, 0xFFFFFFFF };
    for (unsigned i = 0; i < XARR(v); i++) X_ST(f(blk(0x40, SEL_OK, h.real, v[i])), XS_SUCCESS, "dword %u", i);
    x_hset_free(&h);
}
TEST(x_trim_other_process_handle_denied_without_rights) {
    XREQ(f, "NtGdiDdDDITrimProcessCommitment");
    XREQ(o, "NtGdiDdDDIQueryProcessOfferInfo");
    x_hset h; x_hset_init(&h);
    /* a handle opened with query rights only: the offer query accepts it, the trim does not */
    HANDLE q = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, GetCurrentProcessId());
    if (!q) { x_hset_free(&h); t_skip("cannot open the process with query rights"); return; }
    X_ST(o(blk(0x40, SEL_OK, q, 0)), XS_SUCCESS, "offer, query-information handle");
    X_ST(f(blk(0x40, SEL_OK, q, 0)), XS_ACCESS_DENIED, "trim, query-information handle");
    CloseHandle(q);
    x_hset_free(&h);
}
