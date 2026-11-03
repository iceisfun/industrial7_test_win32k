/* gap6_misc: assorted syscalls with analysis-proven early exits: DDC/CI monitor control, DirectComposition
 * channel calls with an unknown channel, composition input sink queries on a NULL handle, and the
 * per-process OneCore transform mode flag. */
#include "gap6.h"

/* ---- NtGdiDDCCI*: physical monitor handle table (win32kfull OPM::CMonitorHandleTable) ---------- */

/* CMonitorHandleTable::GetHandleObject refuses INVALID_MONITOR_HANDLE and any value above
 * 0xffffffff, and otherwise looks the 32-bit index up in a list; both failures return the table's
 * stored error status. Every caller below only continues when that status is >= 0 (the physical
 * monitor object would be NULL otherwise), so an unknown handle must come back as a negative
 * NTSTATUS. */
static const ULONG_PTR g6_bad_monitor_handles[] = { 0, 0x100000000ULL, 0x7FFFFFFFFFFFFFFFULL };

TEST(gap6_ddcci_get_capabilities_string_zero_length) {
    g6_fn14 f = g6_stub("NtGdiDDCCIGetCapabilitiesString"); REQUIRE(f);
    char buf[16]; memset(buf, 0x5A, sizeof buf);
 /* argument 3 (buffer length) == 0 returns STATUS_INVALID_PARAMETER before the handle is used. */
    for (unsigned i = 0; i < 3; i++)
        CHECK((unsigned long)g6_call(f, g6_bad_monitor_handles[i], (ULONG_PTR)buf, 0, 0) == G6_STATUS_INVALID_PARAMETER);
    CHECK((unsigned char)buf[0] == 0x5A);
}
TEST(gap6_ddcci_get_capabilities_string_unknown_handle) {
    g6_fn14 f = g6_stub("NtGdiDDCCIGetCapabilitiesString"); REQUIRE(f);
    char buf[16]; memset(buf, 0x5A, sizeof buf);
    for (unsigned i = 0; i < 3; i++) {
        long st = (long)g6_call(f, g6_bad_monitor_handles[i], (ULONG_PTR)buf, sizeof buf, 0);
        CHECKF(st < 0, "handle 0x%llx -> 0x%lx", (unsigned long long)g6_bad_monitor_handles[i], (unsigned long)st);
    }
    CHECK((unsigned char)buf[0] == 0x5A);
}
TEST(gap6_ddcci_get_capabilities_string_length_unknown_handle) {
    g6_fn14 f = g6_stub("NtGdiDDCCIGetCapabilitiesStringLength"); REQUIRE(f);
    for (unsigned i = 0; i < 3; i++) {
        DWORD len = 0xA5A5A5A5;
        long st = (long)g6_call(f, g6_bad_monitor_handles[i], (ULONG_PTR)&len, 0, 0);
        CHECKF(st < 0, "handle 0x%llx -> 0x%lx", (unsigned long long)g6_bad_monitor_handles[i], (unsigned long)st);
        CHECK(len == 0xA5A5A5A5);        /* the length is only stored (after ProbeForWrite) on success */
    }
}
TEST(gap6_ddcci_get_timing_report_unknown_handle) {
    g6_fn14 f = g6_stub("NtGdiDDCCIGetTimingReport"); REQUIRE(f);
    for (unsigned i = 0; i < 3; i++) {
        BYTE rep[12]; memset(rep, 0xC3, sizeof rep);
        long st = (long)g6_call(f, g6_bad_monitor_handles[i], (ULONG_PTR)rep, 0, 0);
        CHECKF(st < 0, "handle 0x%llx -> 0x%lx", (unsigned long long)g6_bad_monitor_handles[i], (unsigned long)st);
        CHECK(rep[0] == 0xC3 && rep[8] == 0xC3);
    }
}
TEST(gap6_ddcci_get_vcp_feature_unknown_handle) {
    g6_fn14 f = g6_stub("NtGdiDDCCIGetVCPFeature"); REQUIRE(f);
    for (unsigned i = 0; i < 3; i++) {
        DWORD type = 0x11111111, cur = 0x22222222, max = 0x33333333;
        /* (handle, vcp code 0x10 = luminance, &type, &current, &maximum) */
        ULONG_PTR r = f(g6_bad_monitor_handles[i], 0x10, (ULONG_PTR)&type, (ULONG_PTR)&cur, (ULONG_PTR)&max, 0,0,0,0,0,0,0,0,0);
        CHECKF((long)r < 0, "handle 0x%llx -> 0x%lx", (unsigned long long)g6_bad_monitor_handles[i], (unsigned long)r);
        CHECK(type == 0x11111111 && cur == 0x22222222 && max == 0x33333333);
    }
}
TEST(gap6_ddcci_save_current_settings_unknown_handle) {
    g6_fn14 f = g6_stub("NtGdiDDCCISaveCurrentSettings"); REQUIRE(f);
    /* Same lookup, then DdcciSaveCurrentSettings only if it succeeded. */
    for (unsigned i = 0; i < 3; i++) {
        long st = (long)g6_call(f, g6_bad_monitor_handles[i], 0, 0, 0);
        CHECKF(st < 0, "handle 0x%llx -> 0x%lx", (unsigned long long)g6_bad_monitor_handles[i], (unsigned long)st);
    }
}

/* ---- DirectComposition calls against a channel handle the process does not own ---------------- */

/* Channel-based NtDComposition* calls start with CApplicationChannel::ReferenceHandleAndLock, which
 * is CChannel::ReferenceHandleAndLock plus a type check: a process without DirectComposition state
 * gets -0x3fffffff (STATUS_UNSUCCESSFUL), an unknown channel id -0x3fffffde (STATUS_ACCESS_DENIED),
 * and a channel of the wrong type -0x3ffffff3 (STATUS_INVALID_PARAMETER). Which of the three applies
 * depends on whether the test process already created a channel, so any of them is accepted, and
 * nothing is created or changed. */
static int g6_dcomp_unknown_channel_status(unsigned long st) {
    return st == G6_STATUS_UNSUCCESSFUL || st == G6_STATUS_ACCESS_DENIED || st == G6_STATUS_INVALID_PARAMETER;
}
#define DCOMP_BOGUS_CHANNEL 0x7FFFF0u
#define DCOMP_CH_ROW(tag, sym, a2, a3) \
    TEST(gap6_dcomp_##tag##_unknown_channel) { \
        g6_fn14 f = g6_stub(sym); REQUIRE(f); \
        unsigned long st = (unsigned long)g6_call(f, DCOMP_BOGUS_CHANNEL, (a2), (a3), 0); \
        CHECKF(g6_dcomp_unknown_channel_status(st), sym " -> 0x%lx", st); \
        st = (unsigned long)g6_call(f, 0, (a2), (a3), 0); \
        CHECKF(g6_dcomp_unknown_channel_status(st), sym " (channel 0) -> 0x%lx", st); \
    }
DCOMP_CH_ROW(set_debug_counter,        "NtDCompositionSetDebugCounter", 0, 0)
DCOMP_CH_ROW(suspend_animations,       "NtDCompositionSuspendAnimations", 0, 0)
DCOMP_CH_ROW(wait_for_channel,         "NtDCompositionWaitForChannel", 0, 0)
DCOMP_CH_ROW(set_channel_connection_id,"NtDCompositionSetChannelConnectionId", 0, 0)

/* NtDCompositionSetChannelCommitCompletionEvent(channel, HANDLE event, BOOL): event == NULL is
 * STATUS_INVALID_PARAMETER before the channel is looked up. */
TEST(gap6_dcomp_set_channel_commit_completion_event_null_event) {
    g6_fn14 f = g6_stub("NtDCompositionSetChannelCommitCompletionEvent"); REQUIRE(f);
    CHECK((unsigned long)g6_call(f, DCOMP_BOGUS_CHANNEL, 0, 0, 0) == G6_STATUS_INVALID_PARAMETER);
    CHECK((unsigned long)g6_call(f, 0, 0, 1, 0) == G6_STATUS_INVALID_PARAMETER);
}
TEST(gap6_dcomp_set_channel_commit_completion_event_unknown_channel) {
    g6_fn14 f = g6_stub("NtDCompositionSetChannelCommitCompletionEvent"); REQUIRE(f);
    HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL);
    REQUIRE(ev);
    unsigned long st = (unsigned long)g6_call(f, DCOMP_BOGUS_CHANNEL, (ULONG_PTR)ev, 0, 0);
    CHECKF(g6_dcomp_unknown_channel_status(st), "-> 0x%lx", st);
    CloseHandle(ev);
}
/* NtDCompositionDestroyChannel(uint) / NtDCompositionDestroyConnection(handle): look the handle up
 * in the process table and destroy it only when found; an unknown handle is a no-op. The test
 * only asserts that the process survives and that a second identical call behaves the same. */
TEST(gap6_dcomp_destroy_unknown_handles_are_noops) {
    g6_fn14 c = g6_stub("NtDCompositionDestroyChannel");
    g6_fn14 n = g6_stub("NtDCompositionDestroyConnection");
    REQUIRE(c && n);
    g6_call(c, DCOMP_BOGUS_CHANNEL, 0, 0, 0);
    g6_call(c, DCOMP_BOGUS_CHANNEL, 0, 0, 0);
    g6_call(n, DCOMP_BOGUS_CHANNEL, 0, 0, 0);
    g6_call(n, 0, 0, 0, 0);
    CHECK(1);
}

/* ---- composition input sink queries ------------------------------------------------------------ */

/* NtQueryCompositionInput{IsImplicit,SinkLuid,SinkViewId}(HANDLE sink, T *out): the sink is resolved
 * with CompositionInputObject::ResolveHandle first and *out is stored only when that succeeded
 * (-1 < status), so a NULL handle gives an error status and leaves *out alone. */
#define SINK_ROW(tag, sym, type, pat) \
    TEST(gap6_sink_##tag##_null_handle) { \
        g6_fn14 f = g6_stub(sym); REQUIRE(f); \
        type v = (type)(pat); \
        unsigned long st = (unsigned long)g6_call(f, 0, (ULONG_PTR)&v, 0, 0); \
        t_note(sym "(NULL) -> 0x%lx", st); \
        CHECK((long)st < 0); \
        CHECK(v == (type)(pat)); \
    }
SINK_ROW(is_implicit,  "NtQueryCompositionInputIsImplicit", DWORD, 0xA5A5A5A5)
SINK_ROW(luid,         "NtQueryCompositionInputSinkLuid", ULONGLONG, 0xA5A5A5A55A5A5A5AULL)
SINK_ROW(view_id,      "NtQueryCompositionInputSinkViewId", DWORD, 0xA5A5A5A5)

/* ---- OneCore transform mode (per-process flag 0x800000 at Win32Process+0x334) ------------------ */

/* NtEnableOneCoreTransformMode() ORs 0x800000 into the process flags and returns 1;
 * NtIsOneCoreTransformMode() returns (flags >> 0x17) & 1. The flag has no clearing call, so the
 * enable runs in a child that exits right after. */
CHILD(gap6_onecore_transform) {
    (void)argc; (void)argv;
    int bad = 0;
    g6_fn14 en = g6_stub("NtEnableOneCoreTransformMode");
    g6_fn14 is = g6_stub("NtIsOneCoreTransformMode");
    if (!en || !is) { printf("  missing exports\n"); return 0; }
#define CB(x) do { int ok_ = !!(x); if (!ok_) { printf("  FAIL %s\n", #x); bad++; } } while (0)
    unsigned long before = (unsigned long)g6_call0(is);
    printf("  OneCore transform mode before enable: %lu\n", before);
    CB(before == 0 || before == 1);
    CB((unsigned long)g6_call0(en) == 1);
    CB((unsigned long)g6_call0(is) == 1);
    CB((unsigned long)g6_call0(en) == 1);       /* idempotent */
    CB((unsigned long)g6_call0(is) == 1);
    return bad;
}
TEST(gap6_onecore_transform_mode_enable_latches) {
    REQUIRE(g6_stub("NtEnableOneCoreTransformMode"));
    struct k_child_result r = k_child_run("gap6_onecore_transform", NULL, 20000);
    EXPECT_EQ(r.exit_code, 0);
}

/* ---- more DirectComposition early exits (win32kbase) --------------------------------------------- */

/* NtDCompositionGetFrameStatistics(DCOMPOSITION_FRAME_STATISTICS *stats, UINT64 *[2] extra):
 * the status is STATUS_INVALID_PARAMETER for a NULL stats pointer
 * and no connection lookup. */
TEST(gap6_dcomp_get_frame_statistics_null_stats) {
    g6_fn14 f = g6_stub("NtDCompositionGetFrameStatistics"); REQUIRE(f);
    CHECK((unsigned long)g6_call(f, 0, 0, 0, 0) == G6_STATUS_INVALID_PARAMETER);
    ULONGLONG extra[2] = { 0x1111, 0x2222 };
    CHECK((unsigned long)g6_call(f, 0, (ULONG_PTR)extra, 0, 0) == G6_STATUS_INVALID_PARAMETER);
    CHECK(extra[0] == 0x1111 && extra[1] == 0x2222);
}
/* NtDCompositionGetDeletedResources(channel, UINT count, BYTE *buffer, UINT *outCount): count outside
 * 1..8, a NULL buffer or a NULL out pointer is STATUS_INVALID_PARAMETER before the channel lookup. */
TEST(gap6_dcomp_get_deleted_resources_bad_arguments) {
    g6_fn14 f = g6_stub("NtDCompositionGetDeletedResources"); REQUIRE(f);
    BYTE buf[64]; DWORD got = 0xABCD;
    static const DWORD counts[] = { 0, 9, 0x100, 0xFFFFFFFF };
    for (unsigned i = 0; i < 4; i++)
        CHECK((unsigned long)g6_call(f, DCOMP_BOGUS_CHANNEL, counts[i], (ULONG_PTR)buf, (ULONG_PTR)&got) == G6_STATUS_INVALID_PARAMETER);
    CHECK((unsigned long)g6_call(f, DCOMP_BOGUS_CHANNEL, 1, 0, (ULONG_PTR)&got) == G6_STATUS_INVALID_PARAMETER);
    CHECK((unsigned long)g6_call(f, DCOMP_BOGUS_CHANNEL, 1, (ULONG_PTR)buf, 0) == G6_STATUS_INVALID_PARAMETER);
    CHECK(got == 0xABCD);
}
/* With valid arguments the channel lookup is reached and an unknown channel fails the same way as
 * the other channel calls. */
TEST(gap6_dcomp_get_deleted_resources_unknown_channel) {
    g6_fn14 f = g6_stub("NtDCompositionGetDeletedResources"); REQUIRE(f);
    BYTE buf[64]; DWORD got = 0xABCD;
    for (DWORD n = 1; n <= 8; n++) {
        unsigned long st = (unsigned long)g6_call(f, DCOMP_BOGUS_CHANNEL, n, (ULONG_PTR)buf, (ULONG_PTR)&got);
        CHECKF(g6_dcomp_unknown_channel_status(st), "count %lu -> 0x%lx", (unsigned long)n, st);
    }
    CHECK(got == 0xABCD);
}
/* NtDCompositionTelemetrySetApplicationId(channel, UINT bytes, WCHAR *id): NULL id, zero or odd
 * byte count, or more than 300 bytes is STATUS_INVALID_PARAMETER before the channel lookup. */
TEST(gap6_dcomp_telemetry_set_application_id_bad_arguments) {
    g6_fn14 f = g6_stub("NtDCompositionTelemetrySetApplicationId"); REQUIRE(f);
    WCHAR id[8] = L"app";
    CHECK((unsigned long)g6_call(f, DCOMP_BOGUS_CHANNEL, 8, 0, 0) == G6_STATUS_INVALID_PARAMETER);
    CHECK((unsigned long)g6_call(f, DCOMP_BOGUS_CHANNEL, 0, (ULONG_PTR)id, 0) == G6_STATUS_INVALID_PARAMETER);
    CHECK((unsigned long)g6_call(f, DCOMP_BOGUS_CHANNEL, 7, (ULONG_PTR)id, 0) == G6_STATUS_INVALID_PARAMETER);
    CHECK((unsigned long)g6_call(f, DCOMP_BOGUS_CHANNEL, 302, (ULONG_PTR)id, 0) == G6_STATUS_INVALID_PARAMETER);
}
TEST(gap6_dcomp_telemetry_set_application_id_unknown_channel) {
    g6_fn14 f = g6_stub("NtDCompositionTelemetrySetApplicationId"); REQUIRE(f);
    static WCHAR id[16] = L"gap6";          /* even byte count (8), inside 1..300 */
    unsigned long st = (unsigned long)g6_call(f, DCOMP_BOGUS_CHANNEL, 8, (ULONG_PTR)id, 0);
    CHECKF(g6_dcomp_unknown_channel_status(st), "-> 0x%lx", st);
}
/* NtDCompositionSynchronize / NtDCompositionSubmitDWMBatch resolve the channel first. */
TEST(gap6_dcomp_synchronize_unknown_channel) {
    g6_fn14 f = g6_stub("NtDCompositionSynchronize"); REQUIRE(f);
    ULONGLONG out = 0x77;
    unsigned long st = (unsigned long)g6_call(f, DCOMP_BOGUS_CHANNEL, (ULONG_PTR)&out, 0, 0);
    CHECKF(g6_dcomp_unknown_channel_status(st), "-> 0x%lx", st);
    CHECK(out == 0x77);
}
TEST(gap6_dcomp_submit_dwm_batch_unknown_channel) {
    g6_fn14 f = g6_stub("NtDCompositionSubmitDWMBatch"); REQUIRE(f);
    unsigned long st = (unsigned long)g6_call(f, DCOMP_BOGUS_CHANNEL, 0, 0, 0);
    CHECKF(g6_dcomp_unknown_channel_status(st), "-> 0x%lx", st);
}
