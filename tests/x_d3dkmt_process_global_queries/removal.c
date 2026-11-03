/* x_d3dkmt_process_global_queries / removal: Get/SetProcessDeviceRemovalSupport.
 *
 * The getter takes a 0x18-byte block { HANDLE hProcess; LUID AdapterLuid; BOOL Support } and needs no adapter.
 * Measures:
 *   - blocks shorter than 0x18 bytes are INVALID_PARAMETER, a block of exactly 0x18 bytes is accepted;
 *   - the process handle decides the answer: a full-access handle, a query-limited handle and the pseudo handle
 *     succeed, a synchronize-only handle is ACCESS_DENIED, an event or thread handle is OBJECT_TYPE_MISMATCH, the
 *     null handle, a closed handle and a handle with garbage in its upper half are INVALID_HANDLE;
 *   - on success Support (the byte at offset 0x10) is written with a boolean and is the same for every accepted
 *     handle; on failure the block is not written; the LUID field and the incoming Support value do not change the
 *     answer;
 *   - only the caller's own process is accepted: the handle of another live process is INVALID_PARAMETER, that of
 *     a terminated one 0xC000010A;
 *   - the setter may succeed ONCE per process: the first call with a non-NULL block answers SUCCESS and every
 *     later one answers 0xC0000510, whatever the block size and contents were (the block is not read); the NULL
 *     block is INVALID_PARAMETER and does not use the one-shot up; the gdi32 route shares the latch;
 *     the setter never changes what the getter reports; two fresh processes each get their own one-shot.
 * The setter is only ever called in child processes, so the test process itself stays untouched.
 *
 * Determinism: statuses only. The Support value is checked as a boolean and for equality, never printed.
 */
#include "x.h"
#include <stdlib.h>

typedef struct { HANDLE hProcess; LUID AdapterLuid; BYTE Support; BYTE pad[7]; } x_rmget;   /* 0x18; Support is a one-byte BOOLEAN */

static LONG rm_get(x_fn f, HANDLE h, x_rmget *b) {
    memset(b, 0, sizeof *b); b->hProcess = h;
    b->Support = 0xAA;
    return f(b);
}

TEST(x_removal_get_stubs_well_formed) {
    XREQ(f, "NtGdiDdDDIGetProcessDeviceRemovalSupport");
    XREQ(s, "NtGdiDdDDISetProcessDeviceRemovalSupport");
    CHECK(k_stub_valid((FARPROC)f)); CHECK(k_stub_valid((FARPROC)s));
    XREQG(g, "GetProcessDeviceRemovalSupport"); XREQG(gs, "SetProcessDeviceRemovalSupport");
    EXPECT_EQ(k_stub_number((FARPROC)g), k_stub_number((FARPROC)f));
    EXPECT_EQ(k_stub_number((FARPROC)gs), k_stub_number((FARPROC)s));
    EXPECT_NE(k_stub_number((FARPROC)f), k_stub_number((FARPROC)s));
}
TEST(x_removal_get_short_blocks_invalid_parameter) {
    XREQ(f, "NtGdiDdDDIGetProcessDeviceRemovalSupport");
    X_ST(f(NULL), XS_INVALID_PARAMETER, "NULL");
    x_hset h; x_hset_init(&h);
    for (unsigned n = 1; n < 0x18; n++) {
        unsigned char *p = x_edge(n); if (n >= 8) *(HANDLE *)p = h.real;
        X_ST(f(p), XS_INVALID_PARAMETER, "%u readable bytes", n);
    }
    x_hset_free(&h);
}
TEST(x_removal_get_exact_size_succeeds) {
    XREQ(f, "NtGdiDdDDIGetProcessDeviceRemovalSupport");
    x_hset h; x_hset_init(&h);
    x_rmget *b = x_edge(0x18); b->hProcess = h.real;
    X_ST(f(b), XS_SUCCESS, "block of exactly 0x18 bytes");
    b = x_edge(0x100); b->hProcess = h.real;
    X_ST(f(b), XS_SUCCESS, "block of 0x100 bytes");
    x_hset_free(&h);
}
TEST(x_removal_get_wild_pointers_invalid_parameter) {
    XREQ(f, "NtGdiDdDDIGetProcessDeviceRemovalSupport");
    void *wild[] = { (void *)(ULONG_PTR)1, (void *)(ULONG_PTR)0xFFFF800000000000ull, x_freed(), (void *)~(ULONG_PTR)0 };
    for (size_t i = 0; i < XARR(wild); i++) if (wild[i]) X_ST(f(wild[i]), XS_INVALID_PARAMETER, "wild pointer %zu", i);
}
TEST(x_removal_get_full_access_handle) {
    XREQ(f, "NtGdiDdDDIGetProcessDeviceRemovalSupport");
    x_hset h; x_hset_init(&h); x_rmget b;
    X_ST(rm_get(f, h.real, &b), XS_SUCCESS, "full access handle");
    CHECKF(b.Support == 0 || b.Support == 1, "Support is not a boolean");
    x_hset_free(&h);
}
TEST(x_removal_get_query_limited_handle) {
    XREQ(f, "NtGdiDdDDIGetProcessDeviceRemovalSupport");
    x_hset h; x_hset_init(&h); x_rmget b, c;
    X_ST(rm_get(f, h.limited, &b), XS_SUCCESS, "query-limited handle");
    rm_get(f, h.real, &c);
    EXPECT_EQ(b.Support, c.Support);
    x_hset_free(&h);
}
TEST(x_removal_get_pseudo_handle) {
    XREQ(f, "NtGdiDdDDIGetProcessDeviceRemovalSupport");
    x_hset h; x_hset_init(&h); x_rmget b, c;
    X_ST(rm_get(f, GetCurrentProcess(), &b), XS_SUCCESS, "pseudo handle");
    rm_get(f, h.real, &c);
    EXPECT_EQ(b.Support, c.Support);
    x_hset_free(&h);
}
TEST(x_removal_get_synchronize_only_handle_denied) {
    XREQ(f, "NtGdiDdDDIGetProcessDeviceRemovalSupport");
    x_hset h; x_hset_init(&h); x_rmget b;
    X_ST(rm_get(f, h.sync, &b), XS_ACCESS_DENIED, "synchronize-only handle");
    x_hset_free(&h);
}
TEST(x_removal_get_event_handle_type_mismatch) {
    XREQ(f, "NtGdiDdDDIGetProcessDeviceRemovalSupport");
    x_hset h; x_hset_init(&h); x_rmget b;
    X_ST(rm_get(f, h.event, &b), XS_TYPE_MISMATCH, "event handle");
    x_hset_free(&h);
}
TEST(x_removal_get_thread_handle_type_mismatch) {
    XREQ(f, "NtGdiDdDDIGetProcessDeviceRemovalSupport");
    x_hset h; x_hset_init(&h); x_rmget b;
    X_ST(rm_get(f, h.thread, &b), XS_TYPE_MISMATCH, "thread handle");
    x_hset_free(&h);
}
TEST(x_removal_get_null_handle_invalid_handle) {
    XREQ(f, "NtGdiDdDDIGetProcessDeviceRemovalSupport");
    x_rmget b;
    X_ST(rm_get(f, NULL, &b), XS_INVALID_HANDLE, "null handle");
}
TEST(x_removal_get_closed_handle_invalid_handle) {
    XREQ(f, "NtGdiDdDDIGetProcessDeviceRemovalSupport");
    x_hset h; x_hset_init(&h); x_rmget b;
    X_ST(rm_get(f, h.closed, &b), XS_INVALID_HANDLE, "closed handle");
    x_hset_free(&h);
}
TEST(x_removal_get_garbage_upper_half_invalid_handle) {
    XREQ(f, "NtGdiDdDDIGetProcessDeviceRemovalSupport");
    x_hset h; x_hset_init(&h); x_rmget b;
    static const ULONGLONG hi[] = { 1ull << 32, 0xDEADBEEFull << 32, 0x7FFFFFFFull << 32 };
    for (unsigned i = 0; i < XARR(hi); i++)
        X_ST(rm_get(f, (HANDLE)((ULONG_PTR)h.real | hi[i]), &b), XS_INVALID_HANDLE, "handle with upper half %u", i);
    x_hset_free(&h);
}
TEST(x_removal_get_small_handle_values) {
    XREQ(f, "NtGdiDdDDIGetProcessDeviceRemovalSupport");
    x_rmget b;
    for (ULONG_PTR v = 1; v <= 3; v++) X_ST(rm_get(f, (HANDLE)v, &b), XS_INVALID_HANDLE, "handle value %lu", (unsigned long)v);
    X_ST(rm_get(f, (HANDLE)(ULONG_PTR)0x7FFFFFFF, &b), XS_INVALID_HANDLE, "handle 0x7FFFFFFF");
}
TEST(x_removal_get_failure_does_not_write) {
    XREQ(f, "NtGdiDdDDIGetProcessDeviceRemovalSupport");
    x_hset h; x_hset_init(&h); x_rmget b;
    HANDLE bad[] = { h.sync, h.event, h.thread, NULL, h.closed };
    for (unsigned i = 0; i < XARR(bad); i++) {
        LONG st = rm_get(f, bad[i], &b);
        CHECKF(st < 0, "case %u unexpectedly succeeded", i);
        EXPECT_EQ(b.Support, 0xAA);
        EXPECT_EQ(b.AdapterLuid.LowPart, 0); EXPECT_EQ(b.AdapterLuid.HighPart, 0);
    }
    x_hset_free(&h);
}
TEST(x_removal_get_success_leaves_handle_and_luid) {
    XREQ(f, "NtGdiDdDDIGetProcessDeviceRemovalSupport");
    x_hset h; x_hset_init(&h); x_rmget b;
    memset(&b, 0, sizeof b); b.hProcess = h.real; b.AdapterLuid.LowPart = 0x12345; b.AdapterLuid.HighPart = 0x67;
    X_ST(f(&b), XS_SUCCESS, "get");
    EXPECT_EQ((ULONG_PTR)b.hProcess, (ULONG_PTR)h.real);
    EXPECT_EQ(b.AdapterLuid.LowPart, 0x12345); EXPECT_EQ(b.AdapterLuid.HighPart, 0x67);
    x_hset_free(&h);
}
TEST(x_removal_get_luid_and_support_input_do_not_matter) {
    XREQ(f, "NtGdiDdDDIGetProcessDeviceRemovalSupport");
    x_hset h; x_hset_init(&h); x_rmget b, base;
    rm_get(f, h.real, &base);
    static const DWORD vals[] = { 1, 2, 5, 0x10, 0x7FFFFFFF, 0xFFFFFFFF };
    for (unsigned i = 0; i < XARR(vals); i++) {
        memset(&b, 0, sizeof b); b.hProcess = h.real; b.AdapterLuid.LowPart = vals[i]; b.AdapterLuid.HighPart = (LONG)vals[i]; b.Support = (BYTE)vals[i];
        X_ST(f(&b), XS_SUCCESS, "luid/support input %u", i);
        EXPECT_EQ(b.Support, base.Support);
    }
    x_hset_free(&h);
}
TEST(x_removal_get_padding_dword_ignored) {
    XREQ(f, "NtGdiDdDDIGetProcessDeviceRemovalSupport");
    x_hset h; x_hset_init(&h);
    unsigned char *p = x_edge(0x20); memset(p, 0xFF, 0x20); *(HANDLE *)p = h.real;
    X_ST(f(p), XS_SUCCESS, "block with ones everywhere but the handle");
    x_hset_free(&h);
}
TEST(x_removal_get_repeat_is_stable) {
    XREQ(f, "NtGdiDdDDIGetProcessDeviceRemovalSupport");
    x_hset h; x_hset_init(&h); x_rmget b, c;
    rm_get(f, h.real, &b);
    for (int i = 0; i < 20; i++) { X_ST(rm_get(f, h.real, &c), XS_SUCCESS, "call %d", i); EXPECT_EQ(c.Support, b.Support); }
    x_hset_free(&h);
}
TEST(x_removal_get_gdi32_route_matches) {
    XREQG(g, "GetProcessDeviceRemovalSupport");
    XREQ(f, "NtGdiDdDDIGetProcessDeviceRemovalSupport");
    x_hset h; x_hset_init(&h); x_rmget a, b;
    X_ST(rm_get(g, h.real, &a), XS_SUCCESS, "gdi32 real");
    rm_get(f, h.real, &b); EXPECT_EQ(a.Support, b.Support);
    X_ST(rm_get(g, h.sync, &a), XS_ACCESS_DENIED, "gdi32 sync");
    X_ST(rm_get(g, h.event, &a), XS_TYPE_MISMATCH, "gdi32 event");
    X_ST(rm_get(g, NULL, &a), XS_INVALID_HANDLE, "gdi32 null handle");
    X_ST(g(NULL), XS_INVALID_PARAMETER, "gdi32 NULL pointer");
    x_hset_free(&h);
}

/* ---- other process ---- */
CHILD(x_removal_sleeper) { Sleep(15000); return 0; }
TEST(x_removal_get_other_process_handle_invalid_parameter) {
    XREQ(f, "NtGdiDdDDIGetProcessDeviceRemovalSupport");
    PROCESS_INFORMATION pi;
    HANDLE ph = t_spawn_child("x_removal_sleeper", "", 0, NULL, NULL, NULL, FALSE, NULL, &pi);
    if (!ph) { t_skip("could not start the helper process"); return; }
    x_rmget a;
    /* only the caller's own process is accepted: the handle of a live other process is INVALID_PARAMETER, and the
     * block is not written */
    X_ST(rm_get(f, ph, &a), XS_INVALID_PARAMETER, "handle of another process");
    EXPECT_EQ(a.Support, 0xAA);
    HANDLE lim = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pi.dwProcessId);
    if (lim) { X_ST(rm_get(f, lim, &a), XS_INVALID_PARAMETER, "query-limited handle of another process"); CloseHandle(lim); }
    TerminateProcess(ph, 0);
    WaitForSingleObject(ph, 5000);
    /* once the process is gone the lookup reports the dead process instead (STATUS_PROCESS_IS_TERMINATING) */
    X_ST(rm_get(f, ph, &a), (LONG)0xC000010A, "handle of a terminated process");
    CloseHandle(ph); CloseHandle(pi.hThread);
}

/* ---- the one-shot setter, always in fresh processes ---- */
/* args: "<size> <v0> <v1>" */
CHILD(x_removal_set_once) {
    int fails = 0;
    x_fn s = x_nt("NtGdiDdDDISetProcessDeviceRemovalSupport"), g = x_nt("NtGdiDdDDIGetProcessDeviceRemovalSupport");
    if (!s || !g) { printf("  SKIP export missing\n"); return 0; }
    int size = atoi(argv[1]); DWORD v0 = strtoul(argv[2], 0, 0), v1 = strtoul(argv[3], 0, 0);
    HANDLE me = x_open_self(PROCESS_ALL_ACCESS);
    x_rmget before, after;
    XC_ST(rm_get(g, me, &before), XS_SUCCESS, "get before");
    unsigned char *p = x_edge(size); ((DWORD *)p)[0] = v0; if (size >= 8) ((DWORD *)p)[1] = v1;
    XC_ST(s(p), XS_SUCCESS, "first set");
    XC_ST(rm_get(g, me, &after), XS_SUCCESS, "get after first set");
    XC(after.Support == before.Support);
    p = x_edge(size); ((DWORD *)p)[0] = v0; if (size >= 8) ((DWORD *)p)[1] = v1;
    XC_ST(s(p), XS_ALREADY_SET, "second set");
    p = x_edge(0x20);
    XC_ST(s(p), XS_ALREADY_SET, "third set with a larger block");
    XC_ST(rm_get(g, me, &after), XS_SUCCESS, "get after second set");
    XC(after.Support == before.Support);
    return fails;
}
static void run_once(const char *a) {
    if (!x_nt("NtGdiDdDDISetProcessDeviceRemovalSupport")) { t_skip("export not available"); return; }
    struct k_child_result r = k_child_run("x_removal_set_once", a, 60000);
    EXPECT_EQ(r.exit_code, 0);
}
TEST(x_removal_set_once_size4_value0)        { run_once("4 0 0"); }
TEST(x_removal_set_once_size4_value1)        { run_once("4 1 0"); }
TEST(x_removal_set_once_size4_value2)        { run_once("4 2 0"); }
TEST(x_removal_set_once_size4_all_ones)      { run_once("4 0xFFFFFFFF 0"); }
TEST(x_removal_set_once_size8_zero)          { run_once("8 0 0"); }
TEST(x_removal_set_once_size8_first_one)     { run_once("8 1 0"); }
TEST(x_removal_set_once_size8_both_one)      { run_once("8 1 1"); }
TEST(x_removal_set_once_size8_second_one)    { run_once("8 0 1"); }
TEST(x_removal_set_once_size8_second_ones)   { run_once("8 0 0xFFFFFFFF"); }
TEST(x_removal_set_once_size16)              { run_once("16 1 0"); }
TEST(x_removal_set_once_size24)              { run_once("24 0 0"); }
TEST(x_removal_set_once_size32)              { run_once("32 1 0"); }

CHILD(x_removal_set_null) {
    int fails = 0;
    x_fn s = x_nt("NtGdiDdDDISetProcessDeviceRemovalSupport");
    if (!s) { printf("  SKIP export missing\n"); return 0; }
    /* only a NULL block is rejected: the kernel does not read the block, any other user pointer is accepted, even
     * one with a single readable byte */
    XC_ST(s(NULL), XS_INVALID_PARAMETER, "NULL");
    XC_ST(s(NULL), XS_INVALID_PARAMETER, "NULL again");
    XC_ST(s(x_edge(1)), XS_SUCCESS, "first non-NULL call, one readable byte");
    XC_ST(s(x_edge(4)), XS_ALREADY_SET, "next call");
    XC_ST(s(x_edge(1)), XS_ALREADY_SET, "next call, one readable byte");
    return fails;
}
TEST(x_removal_set_null_rejected_and_does_not_use_the_one_shot) {
    if (!x_nt("NtGdiDdDDISetProcessDeviceRemovalSupport")) { t_skip("export not available"); return; }
    struct k_child_result r = k_child_run("x_removal_set_null", "", 60000);
    EXPECT_EQ(r.exit_code, 0);
}
CHILD(x_removal_set_gdi) {
    int fails = 0;
    x_fn gs = x_gdi("SetProcessDeviceRemovalSupport"), s = x_nt("NtGdiDdDDISetProcessDeviceRemovalSupport");
    if (!gs || !s) { printf("  SKIP export missing\n"); return 0; }
    XC_ST(gs(NULL), XS_INVALID_PARAMETER, "gdi32 NULL");
    XC_ST(gs(x_edge(4)), XS_SUCCESS, "gdi32 first");
    XC_ST(s(x_edge(4)), XS_ALREADY_SET, "win32u second");
    XC_ST(gs(x_edge(4)), XS_ALREADY_SET, "gdi32 third");
    return fails;
}
TEST(x_removal_set_gdi32_route_shares_the_latch) {
    if (!x_gdi("SetProcessDeviceRemovalSupport")) { t_skip("export not available"); return; }
    struct k_child_result r = k_child_run("x_removal_set_gdi", "", 60000);
    EXPECT_EQ(r.exit_code, 0);
}
CHILD(x_removal_set_nt_then_gdi) {
    int fails = 0;
    x_fn gs = x_gdi("SetProcessDeviceRemovalSupport"), s = x_nt("NtGdiDdDDISetProcessDeviceRemovalSupport");
    if (!gs || !s) { printf("  SKIP export missing\n"); return 0; }
    XC_ST(s(x_edge(8)), XS_SUCCESS, "win32u first");
    XC_ST(gs(x_edge(8)), XS_ALREADY_SET, "gdi32 second");
    return fails;
}
TEST(x_removal_set_latch_is_per_process) {
    if (!x_nt("NtGdiDdDDISetProcessDeviceRemovalSupport")) { t_skip("export not available"); return; }
    for (int i = 0; i < 3; i++) {
        struct k_child_result r = k_child_run("x_removal_set_nt_then_gdi", "", 60000);
        EXPECT_EQ(r.exit_code, 0);
    }
}
CHILD(x_removal_set_parent_unaffected) {
    int fails = 0;
    x_fn s = x_nt("NtGdiDdDDISetProcessDeviceRemovalSupport");
    if (!s) { printf("  SKIP export missing\n"); return 0; }
    XC_ST(s(x_edge(4)), XS_SUCCESS, "first");
    return fails;
}
TEST(x_removal_set_child_use_does_not_consume_the_parent_latch) {
    XREQ(s, "NtGdiDdDDISetProcessDeviceRemovalSupport");
    struct k_child_result r = k_child_run("x_removal_set_parent_unaffected", "", 60000);
    EXPECT_EQ(r.exit_code, 0);
    /* this test process has never called the setter: its own first call is still accepted */
    X_ST(s(x_edge(4)), XS_SUCCESS, "parent's first set");
    X_ST(s(x_edge(4)), XS_ALREADY_SET, "parent's second set");
}
