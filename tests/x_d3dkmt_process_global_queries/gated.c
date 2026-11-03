/* x_d3dkmt_process_global_queries / gated: thunks whose answer does not depend on the argument block at all.
 *
 * Measures, per thunk, on an ordinary process (interactive session, no adapter handle involved):
 *   - the win32u export is a well-formed syscall stub and the gdi32 D3DKMT export carries the same service number;
 *   - one status is returned for the NULL pointer, for wild pointers (address 1, a kernel address, a released
 *     region, all ones), for argument blocks of every size from 4 to 0x100 bytes, for the zero block, for blocks
 *     carrying a real process handle and for blocks filled with garbage, and the gdi32 route answers the same;
 *   - repeating the call does not change the answer.
 * The scheduling-priority-band, yield-percentage and memory-budget getters/setters answer ACCESS_DENIED to this
 * caller before the block is looked at, the full-screen-exclusive block query answers NOT_SUPPORTED, the
 * scheduling-priority-class setter INVALID_HANDLE, and the class getter and the process-list query
 * INVALID_PARAMETER, in each case without writing to the block. Two exceptions are recorded as they are: the
 * process-list query answers NO_MEMORY for a block filled with garbage, and the class getter's answers for NULL and
 * wild pointers vary from call to call between INVALID_PARAMETER and INVALID_HANDLE, so they are only checked to
 * be one of the two.
 *
 * Determinism: only symbolic status codes are printed; no handle, address or size from the machine is printed.
 */
#include "x.h"

static x_fn nt_of(const char *n) { return x_nt(n); }
static x_fn gdi_of(const char *n) { return x_gdi(n); }

static void gated_stub(const char *nt, const char *gdi) {
    x_fn f = nt_of(nt);
    if (!f) { t_skip("win32u export not available"); return; }
    CHECK(k_stub_valid((FARPROC)f));
    x_fn g = gdi_of(gdi);
    CHECKF(g != NULL, "gdi32 export missing");
    if (g && k_stub_valid((FARPROC)g)) EXPECT_EQ(k_stub_number((FARPROC)g), k_stub_number((FARPROC)f));
}
static void gated_null(const char *nt, LONG want, int nullok) {
    x_fn f = nt_of(nt);
    if (!f) { t_skip("win32u export not available"); return; }
    X_ST(f(NULL), want, "%s(NULL)", nt);
}
static void gated_wild(const char *nt, LONG want) {
    x_fn f = nt_of(nt);
    if (!f) { t_skip("win32u export not available"); return; }
    void *wild[] = { (void *)(ULONG_PTR)1, (void *)(ULONG_PTR)0xFFFF800000000000ull, x_freed(), (void *)~(ULONG_PTR)0, (void *)(ULONG_PTR)0x7FFFFFFF0000ull };
    const char *wn[] = { "address 1", "kernel address", "released region", "all ones", "top of user space" };
    for (size_t i = 0; i < XARR(wild); i++)
        if (wild[i]) X_ST(f(wild[i]), want, "%s(%s)", nt, wn[i]);
}
static void gated_sizes(const char *nt, LONG want) {
    x_fn f = nt_of(nt);
    if (!f) { t_skip("win32u export not available"); return; }
    for (unsigned n = 4; n <= 0x100; n += 4) X_ST(f(x_edge(n)), want, "%s with %u readable bytes", nt, n);
    for (unsigned n = 1; n < 4; n++) X_ST(f(x_edge(n)), want, "%s with %u readable bytes", nt, n);
}
static void gated_zero(const char *nt, LONG want) {
    x_fn f = nt_of(nt);
    if (!f) { t_skip("win32u export not available"); return; }
    X_ST(f(x_edge(0x100)), want, "%s(zero block)", nt);
    X_ST(f(x_edge(0x1000)), want, "%s(zero page)", nt);
}
static void gated_handles(const char *nt, LONG want) {
    x_fn f = nt_of(nt);
    if (!f) { t_skip("win32u export not available"); return; }
    x_hset h; x_hset_init(&h);
    HANDLE hs[] = { h.real, h.limited, h.sync, h.event, h.thread, h.closed, h.pseudo, NULL };
    const char *hn[] = { "real process", "limited process", "synchronize-only process", "event", "thread", "closed", "pseudo process", "null" };
    for (unsigned i = 0; i < XARR(hs); i++) {
        for (unsigned v = 0; v < 4; v++) {
            DWORD *p = x_edge(0x40);
            *(HANDLE *)p = hs[i];
            p[2] = v;             /* the class / value / flag field right behind the handle */
            X_ST(f(p), want, "%s(%s handle, value %u)", nt, hn[i], v);
        }
    }
    x_hset_free(&h);
}
static void gated_garbage(const char *nt, LONG want, int fill) {
    x_fn f = nt_of(nt);
    if (!f) { t_skip("win32u export not available"); return; }
    static const DWORD vals[] = { 0xDEADBEEF, 0xFFFFFFFF, 0x7FFFFFFF, 1 };
    if (fill) {
        DWORD *p = x_edge(0x100); for (int i = 0; i < 0x40; i++) p[i] = 0xDEADBEEF;
        X_ST(f(p), want, "%s(block of 0xDEADBEEF)", nt);
        p = x_edge(0x100); for (int i = 0; i < 0x40; i++) p[i] = 0xFFFFFFFF;
        X_ST(f(p), want, "%s(block of 0xFFFFFFFF)", nt);
        return;
    }
    for (unsigned v = 0; v < XARR(vals); v++)
        for (unsigned i = 0; i < 0x40; i++) {
            DWORD *p = x_edge(0x100); p[i] = vals[v];
            X_ST(f(p), want, "%s dword %u = value %u", nt, i, v);
        }
}
static void gated_gdi(const char *gdi, LONG want, int nullok, int wild) {
    x_fn g = gdi_of(gdi);
    if (!g) { t_skip("gdi32 export not available"); return; }
    if (nullok) X_ST(g(NULL), want, "gdi32 %s(NULL)", gdi);
    X_ST(g(x_edge(0x100)), want, "gdi32 %s(zero block)", gdi);
    if (!wild) return;
    X_ST(g((void *)(ULONG_PTR)1), want, "gdi32 %s(address 1)", gdi);
    X_ST(g((void *)(ULONG_PTR)0xFFFF800000000000ull), want, "gdi32 %s(kernel address)", gdi);
}
static void gated_repeat(const char *nt, LONG want) {
    x_fn f = nt_of(nt);
    if (!f) { t_skip("win32u export not available"); return; }
    for (int i = 0; i < 8; i++) X_ST(f(x_edge(0x40)), want, "%s call %d", nt, i);
}
static void gated_untouched(const char *nt) {
    x_fn f = nt_of(nt);
    if (!f) { t_skip("win32u export not available"); return; }
    /* the block is not written: a block of a recognisable pattern comes back unchanged */
    unsigned char *p = x_edge(0x100), q[0x100];
    for (int i = 0; i < 0x100; i++) p[i] = (unsigned char)(0x5A ^ i);
    memcpy(q, p, sizeof q);
    LONG st = f(p);
    CHECKF(!memcmp(p, q, sizeof q), "%s wrote into the block (status %s)", nt, xs_name(st));
}

/* Test pieces: every thunk gets the BASE set; the others are added where the thunk answers the same way for them. */
#define T_BASE(tag, nt, gdi, want) \
TEST(x_##tag##_stubs_well_formed)          { gated_stub(nt, gdi); } \
TEST(x_##tag##_every_block_size)           { gated_sizes(nt, want); } \
TEST(x_##tag##_zero_block)                 { gated_zero(nt, want); } \
TEST(x_##tag##_handle_blocks)              { gated_handles(nt, want); } \
TEST(x_##tag##_repeat_is_stable)           { gated_repeat(nt, want); } \
TEST(x_##tag##_block_is_not_written)       { gated_untouched(nt); } \
TEST(x_##tag##_gdi32_route_matches)        { gated_gdi(gdi, want, 0, 0); }
#define T_NULL(tag, nt, want)              TEST(x_##tag##_null_pointer) { gated_null(nt, want, 1); }
#define T_WILD(tag, nt, want)              TEST(x_##tag##_wild_pointers) { gated_wild(nt, want); }
#define T_GARB(tag, nt, want)              TEST(x_##tag##_garbage_dwords) { gated_garbage(nt, want, 0); }
#define T_FILL(tag, nt, want)              TEST(x_##tag##_garbage_filled_block) { gated_garbage(nt, want, 1); }
#define T_GDIW(tag, gdi, want)             TEST(x_##tag##_gdi32_route_pointers) { gated_gdi(gdi, want, 1, 1); }

#define GEN_FULL(tag, nt, gdi, want) T_BASE(tag, nt, gdi, want) T_NULL(tag, nt, want) T_WILD(tag, nt, want) T_GARB(tag, nt, want) T_FILL(tag, nt, want) T_GDIW(tag, gdi, want)
GEN_FULL(band_get,   "NtGdiDdDDIGetProcessSchedulingPriorityBand",  "GetProcessSchedulingPriorityBand",  XS_ACCESS_DENIED)
GEN_FULL(band_set,   "NtGdiDdDDISetProcessSchedulingPriorityBand",  "SetProcessSchedulingPriorityBand",  XS_ACCESS_DENIED)
GEN_FULL(yield_get,  "NtGdiDdDDIGetYieldPercentage",                "GetYieldPercentage",                XS_ACCESS_DENIED)
GEN_FULL(yield_set,  "NtGdiDdDDISetYieldPercentage",                "SetYieldPercentage",                XS_ACCESS_DENIED)
GEN_FULL(budget_get, "NtGdiDdDDIGetMemoryBudgetTarget",             "GetMemoryBudgetTarget",             XS_ACCESS_DENIED)
GEN_FULL(budget_set, "NtGdiDdDDISetMemoryBudgetTarget",             "SetMemoryBudgetTarget",             XS_ACCESS_DENIED)
GEN_FULL(fse_query,  "NtGdiDdDDIQueryFSEBlock",                     "QueryFSEBlock",                     XS_NOT_SUPPORTED)
/* the process-list query answers a garbage-filled block with NO_MEMORY (a count field is taken at face value) */
T_BASE(process_list, "NtDxgkGetProcessList", "GetProcessList", XS_INVALID_PARAMETER)
T_NULL(process_list, "NtDxgkGetProcessList", XS_INVALID_PARAMETER)
T_WILD(process_list, "NtDxgkGetProcessList", XS_INVALID_PARAMETER)
T_GARB(process_list, "NtDxgkGetProcessList", XS_INVALID_PARAMETER)
T_GDIW(process_list, "GetProcessList", XS_INVALID_PARAMETER)
/* the class setter answers INVALID_HANDLE for every block; its pointer checks differ for the all-ones pointer */
T_BASE(class_set, "NtGdiDdDDISetProcessSchedulingPriorityClass", "SetProcessSchedulingPriorityClass", XS_INVALID_HANDLE)
T_NULL(class_set, "NtGdiDdDDISetProcessSchedulingPriorityClass", XS_INVALID_HANDLE)
T_GARB(class_set, "NtGdiDdDDISetProcessSchedulingPriorityClass", XS_INVALID_HANDLE)
T_FILL(class_set, "NtGdiDdDDISetProcessSchedulingPriorityClass", XS_INVALID_HANDLE)
/* the class getter: valid readable blocks are INVALID_PARAMETER; its NULL and wild pointer answers vary between calls */
T_BASE(class_get, "NtGdiDdDDIGetProcessSchedulingPriorityClass", "GetProcessSchedulingPriorityClass", XS_INVALID_PARAMETER)

TEST(x_class_set_wild_pointers) {
    XREQ(f, "NtGdiDdDDISetProcessSchedulingPriorityClass");
    void *wild[] = { (void *)(ULONG_PTR)1, (void *)(ULONG_PTR)0xFFFF800000000000ull, x_freed(), (void *)(ULONG_PTR)0x7FFFFFFF0000ull };
    for (size_t i = 0; i < XARR(wild); i++) if (wild[i]) X_ST(f(wild[i]), XS_INVALID_HANDLE, "wild pointer %zu", i);
    X_ST(f((void *)~(ULONG_PTR)0), XS_INVALID_PARAMETER, "all-ones pointer");
}
TEST(x_class_get_wild_pointers_never_fault) {
    XREQ(f, "NtGdiDdDDIGetProcessSchedulingPriorityClass");
    void *wild[] = { (void *)(ULONG_PTR)1, (void *)(ULONG_PTR)0xFFFF800000000000ull, x_freed(), (void *)~(ULONG_PTR)0, (void *)(ULONG_PTR)0x7FFFFFFF0000ull, NULL };
    for (size_t i = 0; i < XARR(wild); i++) {
        LONG st = f(wild[i]);
        CHECKF(st == XS_INVALID_PARAMETER || st == XS_INVALID_HANDLE, "wild pointer %zu answered %s", i, xs_name(st));
    }
}

/* The set/get pair of the class thunks never round-trips on an ordinary process: after every set attempt the getter
 * still answers INVALID_PARAMETER and writes nothing. */
TEST(x_class_set_then_get_does_not_round_trip) {
    XREQ(s, "NtGdiDdDDISetProcessSchedulingPriorityClass");
    XREQ(g, "NtGdiDdDDIGetProcessSchedulingPriorityClass");
    x_hset h; x_hset_init(&h);
    for (DWORD cls = 0; cls <= 8; cls++) {
        DWORD *p = x_edge(0x10); *(HANDLE *)p = h.real; p[2] = cls;
        X_ST(s(p), XS_INVALID_HANDLE, "set class %lu", (unsigned long)cls);
        DWORD *q = x_edge(0x10); *(HANDLE *)q = h.real; q[2] = 0x77;
        X_ST(g(q), XS_INVALID_PARAMETER, "get after set class %lu", (unsigned long)cls);
        EXPECT_EQ(q[2], 0x77);
    }
    x_hset_free(&h);
}
