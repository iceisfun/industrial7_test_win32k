/* x_d3dkmt_process_global_queries / clock: QueryClockCalibration, DDisplayEnum.
 *
 * QueryClockCalibration { DWORD hAdapter; DWORD NodeOrdinal; DWORD EngineOrdinal; { ULONGLONG GpuFrequency;
 * ULONGLONG GpuClockCounter; ULONGLONG CpuClockCounter; } ClockData } reads the GPU and CPU clocks of one engine
 * of one adapter. It needs an adapter handle (taken from EnumAdapters2 here) and nothing else. Measures:
 *   - blocks shorter than 0x28 bytes, a zero or closed adapter handle and an engine ordinal other than 0 are
 *     INVALID_PARAMETER; node ordinal 0 of engine 0 succeeds, a node ordinal of 0xFFFFFFFF or above the node count is
 *     INVALID_PARAMETER;
 *   - on success the frequency is non-zero, the counters are non-zero and do not go backwards between calls, and the
 *     input fields are left alone; on failure the clock data is not written;
 *   - DDisplayEnum: blocks shorter than 0x20 bytes are INVALID_PARAMETER; a zero 0x20-byte block is BUFFER_TOO_SMALL and
 *     the call writes the same non-zero value into the qwords at offsets 0 and 0x10; non-zero upper halves of those
 *     two qwords are INVALID_PARAMETER; the pointer qwords at offsets 8 and 0x18 are not looked at while the
 *     capacity is zero.
 * A session without an adapter skips the clock tests.
 *
 * Determinism: statuses and relations only.
 */
#include "x.h"

typedef struct { DWORD hAdapter, NodeOrdinal, EngineOrdinal; ULONGLONG GpuFrequency, GpuClockCounter, CpuClockCounter; } x_clock;   /* 0x24, packed on 4 */
#pragma pack(push, 4)
typedef struct { DWORD hAdapter, NodeOrdinal, EngineOrdinal; ULONGLONG GpuFrequency, GpuClockCounter, CpuClockCounter; } x_clock4;
#pragma pack(pop)

static x_clock4 *cblk(unsigned size, DWORD h, DWORD node, DWORD engine) {
    x_clock4 *c = x_edge(size);
    c->hAdapter = h; c->NodeOrdinal = node; c->EngineOrdinal = engine;
    return c;
}

TEST(x_clock_stubs_well_formed) {
    XREQ(f, "NtGdiDdDDIQueryClockCalibration"); XREQG(g, "QueryClockCalibration");
    CHECK(k_stub_valid((FARPROC)f));
    EXPECT_EQ(k_stub_number((FARPROC)g), k_stub_number((FARPROC)f));
}
TEST(x_clock_null_and_short_blocks_invalid_parameter) {
    XREQ(f, "NtGdiDdDDIQueryClockCalibration");
    X_ST(f(NULL), XS_INVALID_PARAMETER, "NULL");
    x_adapterinfo a[32]; int n = x_adapters_open(a);
    DWORD h = n > 0 ? a[0].hAdapter : 0;
    for (unsigned s = 1; s < 0x28; s++) {
        X_ST(f(x_edge(s)), XS_INVALID_PARAMETER, "%u readable bytes", s);
    }
    x_adapters_close(a, n);
}
TEST(x_clock_wild_pointers_invalid_parameter) {
    XREQ(f, "NtGdiDdDDIQueryClockCalibration");
    void *wild[] = { (void *)(ULONG_PTR)1, (void *)(ULONG_PTR)0xFFFF800000000000ull, x_freed(), (void *)~(ULONG_PTR)0 };
    for (size_t i = 0; i < XARR(wild); i++) if (wild[i]) X_ST(f(wild[i]), XS_INVALID_PARAMETER, "wild pointer %zu", i);
}
TEST(x_clock_zero_block_invalid_parameter) {
    XREQ(f, "NtGdiDdDDIQueryClockCalibration");
    X_ST(f(x_edge(0x28)), XS_INVALID_PARAMETER, "zero block");
    X_ST(f(x_edge(0x100)), XS_INVALID_PARAMETER, "zero page tail");
}
TEST(x_clock_bogus_adapter_handles_invalid_parameter) {
    XREQ(f, "NtGdiDdDDIQueryClockCalibration");
    static const DWORD h[] = { 1, 2, 3, 4, 5, 6, 8, 0x10, 0x7FFFFFFF, 0xFFFFFFFF, 0xDEADBEEF };
    for (unsigned i = 0; i < XARR(h); i++) X_ST(f(cblk(0x28, h[i], 0, 0)), XS_INVALID_PARAMETER, "adapter handle 0x%lx", (unsigned long)h[i]);
}
TEST(x_clock_engine0_node0_succeeds) {
    XREQ(f, "NtGdiDdDDIQueryClockCalibration");
    x_adapterinfo a[32]; int n = x_adapters_open(a);
    XNEED(n >= 0);
    if (n == 0) { t_skip("no adapters in this session"); return; }
    for (int i = 0; i < n; i++) X_ST(f(cblk(0x28, a[i].hAdapter, 0, 0)), XS_SUCCESS, "adapter %d node 0 engine 0", i);
    x_adapters_close(a, n);
}
TEST(x_clock_exact_size_boundary) {
    XREQ(f, "NtGdiDdDDIQueryClockCalibration");
    x_adapterinfo a[32]; int n = x_adapters_open(a);
    XNEED(n >= 0);
    if (n == 0) { t_skip("no adapters in this session"); return; }
    X_ST(f(cblk(0x28, a[0].hAdapter, 0, 0)), XS_SUCCESS, "0x28 bytes");
    X_ST(f(cblk(0x24, a[0].hAdapter, 0, 0)), XS_INVALID_PARAMETER, "0x24 bytes");
    X_ST(f(cblk(0x100, a[0].hAdapter, 0, 0)), XS_SUCCESS, "0x100 bytes");
    x_adapters_close(a, n);
}
TEST(x_clock_engine_other_than_zero_invalid_parameter) {
    XREQ(f, "NtGdiDdDDIQueryClockCalibration");
    x_adapterinfo a[32]; int n = x_adapters_open(a);
    XNEED(n >= 0);
    if (n == 0) { t_skip("no adapters in this session"); return; }
    static const DWORD e[] = { 1, 2, 3, 4, 5, 6, 8, 0x10, 0x7FFFFFFF, 0xFFFFFFFF };
    for (int i = 0; i < n; i++) for (unsigned k = 0; k < XARR(e); k++)
        X_ST(f(cblk(0x28, a[i].hAdapter, 0, e[k])), XS_INVALID_PARAMETER, "adapter %d engine 0x%lx", i, (unsigned long)e[k]);
    x_adapters_close(a, n);
}
TEST(x_clock_huge_node_invalid_parameter) {
    XREQ(f, "NtGdiDdDDIQueryClockCalibration");
    x_adapterinfo a[32]; int n = x_adapters_open(a);
    XNEED(n >= 0);
    if (n == 0) { t_skip("no adapters in this session"); return; }
    static const DWORD nd[] = { 0x100, 0x1000, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF };
    for (int i = 0; i < n; i++) for (unsigned k = 0; k < XARR(nd); k++)
        X_ST(f(cblk(0x28, a[i].hAdapter, nd[k], 0)), XS_INVALID_PARAMETER, "adapter %d node 0x%lx", i, (unsigned long)nd[k]);
    x_adapters_close(a, n);
}
TEST(x_clock_node_accepts_a_prefix_of_ordinals) {
    XREQ(f, "NtGdiDdDDIQueryClockCalibration");
    x_adapterinfo a[32]; int n = x_adapters_open(a);
    XNEED(n >= 0);
    if (n == 0) { t_skip("no adapters in this session"); return; }
    for (int i = 0; i < n; i++) {
        int seen_fail = 0, ok = 0;
        for (DWORD nd = 0; nd < 64; nd++) {
            LONG st = f(cblk(0x28, a[i].hAdapter, nd, 0));
            CHECKF(st == XS_SUCCESS || st == XS_INVALID_PARAMETER, "node %lu answered %s", (unsigned long)nd, xs_name(st));
            if (st == XS_SUCCESS) { ok++; CHECKF(!seen_fail, "node %lu valid after an invalid node", (unsigned long)nd); }
            else seen_fail = 1;
        }
        CHECKF(ok >= 1, "node 0 not valid");
    }
    x_adapters_close(a, n);
}
TEST(x_clock_data_sane_and_counters_monotonic) {
    XREQ(f, "NtGdiDdDDIQueryClockCalibration");
    x_adapterinfo a[32]; int n = x_adapters_open(a);
    XNEED(n >= 0);
    if (n == 0) { t_skip("no adapters in this session"); return; }
    for (int i = 0; i < n; i++) {
        x_clock4 *c = cblk(0x28, a[i].hAdapter, 0, 0);
        X_ST(f(c), XS_SUCCESS, "adapter %d", i);
        CHECK(c->GpuFrequency != 0); CHECK(c->GpuClockCounter != 0); CHECK(c->CpuClockCounter != 0);
        ULONGLONG g0 = c->GpuClockCounter, p0 = c->CpuClockCounter;
        Sleep(20);
        c = cblk(0x28, a[i].hAdapter, 0, 0);
        X_ST(f(c), XS_SUCCESS, "adapter %d second call", i);
        CHECK(c->GpuClockCounter > g0); CHECK(c->CpuClockCounter > p0);
        EXPECT_EQ(c->hAdapter, a[i].hAdapter); EXPECT_EQ(c->NodeOrdinal, 0); EXPECT_EQ(c->EngineOrdinal, 0);
    }
    x_adapters_close(a, n);
}
TEST(x_clock_cpu_counter_advances_with_wall_time) {
    XREQ(f, "NtGdiDdDDIQueryClockCalibration");
    x_adapterinfo a[32]; int n = x_adapters_open(a);
    XNEED(n >= 0);
    if (n == 0) { t_skip("no adapters in this session"); return; }
    /* The counter's unit is not assumed: two intervals of different length must advance it in proportion to their
     * length (the second sleep is twice the first). */
    x_clock4 *c = cblk(0x28, a[0].hAdapter, 0, 0);
    X_ST(f(c), XS_SUCCESS, "first");
    ULONGLONG p0 = c->CpuClockCounter;
    Sleep(100);
    c = cblk(0x28, a[0].hAdapter, 0, 0);
    X_ST(f(c), XS_SUCCESS, "second");
    ULONGLONG p1 = c->CpuClockCounter;
    Sleep(300);
    c = cblk(0x28, a[0].hAdapter, 0, 0);
    X_ST(f(c), XS_SUCCESS, "third");
    ULONGLONG p2 = c->CpuClockCounter;
    ULONGLONG d1 = p1 - p0, d2 = p2 - p1;
    CHECKF(p1 > p0 && p2 > p1, "cpu counter did not advance");
    /* nominal ratio 3; a generous window keeps scheduling noise out */
    CHECKF(d1 != 0 && d2 / d1 >= 2 && d2 / d1 <= 4, "cpu counter advance is not proportional to elapsed time");
    x_adapters_close(a, n);
}
TEST(x_clock_failure_does_not_write_clock_data) {
    XREQ(f, "NtGdiDdDDIQueryClockCalibration");
    x_adapterinfo a[32]; int n = x_adapters_open(a);
    XNEED(n >= 0);
    if (n == 0) { t_skip("no adapters in this session"); return; }
    x_clock4 *c = cblk(0x28, a[0].hAdapter, 0, 1);
    c->GpuFrequency = c->GpuClockCounter = c->CpuClockCounter = 0x5A5A5A5A5A5A5A5Aull;
    X_ST(f(c), XS_INVALID_PARAMETER, "engine 1");
    EXPECT_EQ(c->GpuFrequency, 0x5A5A5A5A5A5A5A5Aull); EXPECT_EQ(c->CpuClockCounter, 0x5A5A5A5A5A5A5A5Aull);
    x_adapters_close(a, n);
}
TEST(x_clock_closed_adapter_handle_invalid_parameter) {
    XREQ(f, "NtGdiDdDDIQueryClockCalibration");
    x_adapterinfo a[32]; int n = x_adapters_open(a);
    XNEED(n >= 0);
    if (n == 0) { t_skip("no adapters in this session"); return; }
    DWORD h = a[0].hAdapter;
    X_ST(f(cblk(0x28, h, 0, 0)), XS_SUCCESS, "open handle");
    x_adapters_close(a, n);
    X_ST(f(cblk(0x28, h, 0, 0)), XS_INVALID_PARAMETER, "closed handle");
}
TEST(x_clock_gdi32_route_matches) {
    XREQG(g, "QueryClockCalibration");
    X_ST(g(NULL), XS_INVALID_PARAMETER, "gdi32 NULL");
    X_ST(g(x_edge(0x28)), XS_INVALID_PARAMETER, "gdi32 zero block");
    x_adapterinfo a[32]; int n = x_adapters_open(a);
    if (n <= 0) { t_skip("no adapters in this session"); return; }
    x_clock4 *c = cblk(0x28, a[0].hAdapter, 0, 0);
    X_ST(g(c), XS_SUCCESS, "gdi32 adapter 0");
    CHECK(c->GpuFrequency != 0);
    x_adapters_close(a, n);
}

/* ---------------- DDisplayEnum ---------------- */
TEST(x_ddisplay_stubs_well_formed) {
    XREQ(f, "NtGdiDdDDIDDisplayEnum"); XREQG(g, "DDisplayEnum");
    CHECK(k_stub_valid((FARPROC)f));
    EXPECT_EQ(k_stub_number((FARPROC)g), k_stub_number((FARPROC)f));
}
TEST(x_ddisplay_short_blocks_invalid_parameter) {
    XREQ(f, "NtGdiDdDDIDDisplayEnum");
    X_ST(f(NULL), XS_INVALID_PARAMETER, "NULL");
    for (unsigned n = 1; n < 0x20; n++) X_ST(f(x_edge(n)), XS_INVALID_PARAMETER, "%u readable bytes", n);
}
TEST(x_ddisplay_wild_pointers_invalid_parameter) {
    XREQ(f, "NtGdiDdDDIDDisplayEnum");
    void *wild[] = { (void *)(ULONG_PTR)1, (void *)(ULONG_PTR)0xFFFF800000000000ull, x_freed(), (void *)~(ULONG_PTR)0 };
    for (size_t i = 0; i < XARR(wild); i++) if (wild[i]) X_ST(f(wild[i]), XS_INVALID_PARAMETER, "wild pointer %zu", i);
}
TEST(x_ddisplay_zero_block_buffer_too_small) {
    XREQ(f, "NtGdiDdDDIDDisplayEnum");
    X_ST(f(x_edge(0x20)), XS_BUFFER_TOO_SMALL, "zero block of 0x20 bytes");
    X_ST(f(x_edge(0x100)), XS_BUFFER_TOO_SMALL, "zero block of 0x100 bytes");
}
TEST(x_ddisplay_reports_a_required_count_in_two_qwords) {
    XREQ(f, "NtGdiDdDDIDDisplayEnum");
    ULONGLONG *q = x_edge(0x20);
    X_ST(f(q), XS_BUFFER_TOO_SMALL, "zero block");
    CHECK(q[0] != 0);
    EXPECT_EQ(q[0], q[2]);
    CHECK(q[0] < 0x100000000ull);
    EXPECT_EQ(q[1], 0); EXPECT_EQ(q[3], 0);
}
TEST(x_ddisplay_upper_halves_must_be_zero) {
    XREQ(f, "NtGdiDdDDIDDisplayEnum");
    for (int which = 0; which < 2; which++) {
        DWORD *p = x_edge(0x20); p[which ? 5 : 1] = 1;
        X_ST(f(p), XS_INVALID_PARAMETER, which ? "upper half of qword 2" : "upper half of qword 0");
        p = x_edge(0x20); p[which ? 5 : 1] = 0xFFFFFFFF;
        X_ST(f(p), XS_INVALID_PARAMETER, which ? "upper half of qword 2, all ones" : "upper half of qword 0, all ones");
    }
}
TEST(x_ddisplay_incoming_counts_do_not_matter_while_pointers_are_null) {
    XREQ(f, "NtGdiDdDDIDDisplayEnum");
    static const DWORD v[] = { 1, 2, 4, 16, 100 };
    for (unsigned i = 0; i < XARR(v); i++) {
        DWORD *p = x_edge(0x20); p[0] = v[i];
        X_ST(f(p), XS_BUFFER_TOO_SMALL, "qword 0 = incoming count %u", i);
        p = x_edge(0x20); p[4] = v[i];
        X_ST(f(p), XS_BUFFER_TOO_SMALL, "qword 2 = incoming count %u", i);
    }
}
TEST(x_ddisplay_pointer_qwords_not_read_with_zero_capacity) {
    XREQ(f, "NtGdiDdDDIDDisplayEnum");
    static char buf[0x1000];
    ULONGLONG *q = x_edge(0x20); q[1] = (ULONGLONG)(ULONG_PTR)buf;
    X_ST(f(q), XS_BUFFER_TOO_SMALL, "valid pointer at offset 8");
    q = x_edge(0x20); q[3] = (ULONGLONG)(ULONG_PTR)buf;
    X_ST(f(q), XS_BUFFER_TOO_SMALL, "valid pointer at offset 0x18");
    q = x_edge(0x20); q[1] = 1; q[3] = 1;
    X_ST(f(q), XS_BUFFER_TOO_SMALL, "address 1 at both offsets");
    for (size_t i = 0; i < sizeof buf; i++) if (buf[i]) { CHECKF(0, "the buffer was written"); break; }
}
TEST(x_ddisplay_repeat_is_stable) {
    XREQ(f, "NtGdiDdDDIDDisplayEnum");
    ULONGLONG *q = x_edge(0x20);
    X_ST(f(q), XS_BUFFER_TOO_SMALL, "first");
    ULONGLONG first = q[0];
    for (int i = 0; i < 10; i++) { q = x_edge(0x20); X_ST(f(q), XS_BUFFER_TOO_SMALL, "call %d", i); EXPECT_EQ(q[0], first); }
}
TEST(x_ddisplay_gdi32_route_matches) {
    XREQG(g, "DDisplayEnum");
    X_ST(g(NULL), XS_INVALID_PARAMETER, "gdi32 NULL");
    X_ST(g(x_edge(0x1F)), XS_INVALID_PARAMETER, "gdi32 short block");
    X_ST(g(x_edge(0x20)), XS_BUFFER_TOO_SMALL, "gdi32 zero block");
}
