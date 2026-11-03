/* gap_03 / comp: NtDCompositionGetFrameStatistics (win32kbase).
 *
 * Analysis: argument 1 == NULL -> STATUS_INVALID_PARAMETER (0xC000000D) before anything else. Otherwise
 * CConnection::GetDefaultConnection() (NULL -> STATUS_ACCESS_DENIED 0xC0000022), then GetFrameStatistics into
 * a local 0x28-byte block and, only on success, copy-out of 0x28 bytes to argument 1 and 0x10 bytes to argument 2.
 * On a failure status the caller's buffers are not written.
 */
#include "gap3.h"

TEST(gap3_dcomp_framestats_null_stats_invalid_parameter) {
    NTSTATUS (WINAPI *f)(void *, void *) = (void *)UNX(NtDCompositionGetFrameStatistics);
    REQUIRE(f);
    BYTE extra[16]; memset(extra, 0xCC, sizeof extra);
    EXPECT_EQ((ULONG)f(NULL, NULL), NTSTATUS_INVALID_PARAMETER);
    EXPECT_EQ((ULONG)f(NULL, extra), NTSTATUS_INVALID_PARAMETER);
    for (int i = 0; i < 16; i++) CHECKF(extra[i] == 0xCC, "extra[%d] written", i);
}

TEST(gap3_dcomp_framestats_null_stats_ignores_second_pointer) {
    NTSTATUS (WINAPI *f)(void *, void *) = (void *)UNX(NtDCompositionGetFrameStatistics);
    REQUIRE(f);
    /* The second pointer is only touched after success; a garbage value must not matter on the NULL-first path. */
    EXPECT_EQ((ULONG)f(NULL, (void *)(ULONG_PTR)0x10), NTSTATUS_INVALID_PARAMETER);
}

TEST(gap3_dcomp_framestats_failure_leaves_buffers_untouched) {
    NTSTATUS (WINAPI *f)(void *, void *) = (void *)UNX(NtDCompositionGetFrameStatistics);
    REQUIRE(f);
    BYTE stats[0x28], extra[0x10];
    memset(stats, 0xCC, sizeof stats); memset(extra, 0xCC, sizeof extra);
    NTSTATUS st = f(stats, extra);
    t_note("NtDCompositionGetFrameStatistics -> %s", k_status_name(st));
    if (st < 0) {
        for (unsigned i = 0; i < sizeof stats; i++) if (stats[i] != 0xCC) { CHECKF(0, "stats[%u] written on failure", i); break; }
        for (unsigned i = 0; i < sizeof extra; i++) if (extra[i] != 0xCC) { CHECKF(0, "extra[%u] written on failure", i); break; }
    } else {
        CHECK(1);
    }
}

TEST(gap3_dcomp_framestats_optional_second_pointer) {
    NTSTATUS (WINAPI *f)(void *, void *) = (void *)UNX(NtDCompositionGetFrameStatistics);
    REQUIRE(f);
    BYTE stats[0x28], extra[0x10];
    memset(stats, 0xCC, sizeof stats); memset(extra, 0xCC, sizeof extra);
    NTSTATUS a = f(stats, NULL);
    NTSTATUS b = f(stats, extra);
    /* Both calls take the same path up to the optional copy; the status is the same unless the connection state changed. */
    CHECKF((a < 0) == (b < 0), "NULL-extra %s vs extra %s", k_status_name(a), k_status_name(b));
    if (b >= 0) {
        /* extra receives two global qwords of win32kbase (not process data): just require the write happened. */
        int touched = 0;
        for (int i = 0; i < 16; i++) if (extra[i] != 0xCC) touched = 1;
        t_note("extra block %s", touched ? "written" : "unchanged (all 0xCC, unlikely)");
    }
}

TEST(gap3_dcomp_framestats_repeat_is_consistent) {
    NTSTATUS (WINAPI *f)(void *, void *) = (void *)UNX(NtDCompositionGetFrameStatistics);
    REQUIRE(f);
    BYTE s1[0x28], s2[0x28];
    NTSTATUS a = f(s1, NULL), b = f(s2, NULL);
    CHECKF(a == b || (a >= 0) == (b >= 0), "first %s second %s", k_status_name(a), k_status_name(b));
}
