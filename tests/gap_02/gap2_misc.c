/* gap_02 misc group: NtUserGetUniformSpaceMapping, NtUserEvent, NtUserModifyUserStartupInfoFlags (no-op masks),
 * dxgkrnl thunks (OpenAdapterFromLuid, Cache/GetCachedHybridQueryValue, GetMultiPlaneOverlayCaps).
 *
 * Analysis facts:
 *  - NtUserGetUniformSpaceMapping(hmon, out[4]): HMValidateSharedHandle(hmon) must yield a monitor; the 16-byte result is
 *    copied out only on the TRUE path (DrvIsUniformSpaceMapping and a matching display entry). Otherwise 0, out untouched.
 *  - NtUserEvent(p): p must be 4-byte aligned (8 is fine); header {DWORD mask; WORD type; WORD size; data[size]}; the body
 *    copies p, walks psiiList and sends WM 0x3c to matching instance windows, returning 0. A node matches when
 *    ((mask & filter) != 0) == (type == 0); with mask 0 and a non-zero type no node can match, so nothing is sent.
 *  - NtUserModifyUserStartupInfoFlags(mask, value): ppi->dwStartupFlags = (old & ~mask) | (mask & value), returns 1.
 *    A zero mask keeps the old value whatever `value` is.
 *  - dxgkrnl-routed calls: no win32k body to prove early exits, so only argument blocks of zeros (a NULL adapter handle,
 *    which no adapter can own) are used, expecting a failure status.
 */
#include "gap2.h"
#include <string.h>

TEST(gap2_uniform_invalid_handles) {
    NtUserGetUniformSpaceMapping_pfn f = GN(NtUserGetUniformSpaceMapping); REQUIRE(f);
    HMONITOR bad[] = { NULL, (HMONITOR)(ULONG_PTR)0x7ffe1234, (HMONITOR)(ULONG_PTR)0xffffffff, (HMONITOR)(ULONG_PTR)5 };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        UINT out[4] = { 0xA1A1A1A1, 0xA2A2A2A2, 0xA3A3A3A3, 0xA4A4A4A4 };
        EXPECT_EQ(f(bad[i], out), 0);
        EXPECT_EQ(out[0], 0xA1A1A1A1); EXPECT_EQ(out[1], 0xA2A2A2A2); EXPECT_EQ(out[2], 0xA3A3A3A3); EXPECT_EQ(out[3], 0xA4A4A4A4);
    }
}
TEST(gap2_uniform_wrong_object_type_handle) {
    NtUserGetUniformSpaceMapping_pfn f = GN(NtUserGetUniformSpaceMapping); REQUIRE(f);
    HWND w = gap2_top(); if (!w) { t_skip("window creation failed"); return; }
    UINT out[4] = { 1, 2, 3, 4 };
    /* an HWND is not a monitor: the shared-handle validation rejects the type */
    EXPECT_EQ(f((HMONITOR)w, out), 0);
    EXPECT_EQ(out[0], 1); EXPECT_EQ(out[3], 4);
    gap2_destroy(w);
}
TEST(gap2_uniform_primary_monitor_result_shape) {
    NtUserGetUniformSpaceMapping_pfn f = GN(NtUserGetUniformSpaceMapping); REQUIRE(f); REQUIRE(U(MonitorFromPoint));
    HMONITOR m = U(MonitorFromPoint)((POINT){ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
    if (!m) { t_skip("no monitor"); return; }
    UINT out[4] = { 0x11111111, 0x22222222, 0x33333333, 0x44444444 };
    DWORD r = f(m, out);
    CHECK(r == 0 || r == 1);
    if (!r) { EXPECT_EQ(out[0], 0x11111111); EXPECT_EQ(out[1], 0x22222222); EXPECT_EQ(out[2], 0x33333333); EXPECT_EQ(out[3], 0x44444444); }
    else t_note("uniform space mapping %08x %08x %08x %08x", out[0], out[1], out[2], out[3]);
    UINT out2[4] = { 9, 9, 9, 9 };
    EXPECT_EQ(f(m, out2), r);
    if (r) CHECK(!memcmp(out, out2, sizeof out));
}
struct acc { HMONITOR m[16]; int n; };
static BOOL CALLBACK mon_cb(HMONITOR hm, HDC dc, LPRECT rc, LPARAM lp) { struct acc *x = (struct acc *)lp; if (x->n < 16) x->m[x->n++] = hm; return TRUE; }
TEST(gap2_uniform_every_enumerated_monitor) {
    NtUserGetUniformSpaceMapping_pfn f = GN(NtUserGetUniformSpaceMapping); REQUIRE(f); REQUIRE(U(EnumDisplayMonitors));
    struct acc a; a.n = 0;
    U(EnumDisplayMonitors)(NULL, NULL, mon_cb, (LPARAM)&a);
    if (!a.n) { t_skip("no monitors"); return; }
    for (int i = 0; i < a.n; i++) {
        UINT out[4] = { 0xEE, 0xEE, 0xEE, 0xEE };
        DWORD r = f(a.m[i], out);
        CHECKF(r == 0 || r == 1, "monitor %d result %lu", i, (unsigned long)r);
        if (!r) CHECK(out[0] == 0xEE && out[3] == 0xEE);
    }
}

/* header + 8 bytes of payload, 8-byte aligned */
struct ev { DWORD mask; WORD type; WORD size; BYTE data[16]; } __attribute__((aligned(8)));
TEST(gap2_event_non_matching_filter_returns_zero) {
    NtUserEvent_pfn f = GN(NtUserEvent); REQUIRE(f);
    struct ev e; memset(&e, 0, sizeof e);
    e.mask = 0; e.type = 1; e.size = 0;
    EXPECT_EQ(f(&e), 0);
    e.size = 8; memset(e.data, 0x5c, 8);
    EXPECT_EQ(f(&e), 0);
    e.size = 16;
    EXPECT_EQ(f(&e), 0);
    EXPECT_EQ(e.data[0], 0x5c);   /* the kernel works on a pool copy */
    EXPECT_EQ(e.size, 16);
}
TEST(gap2_event_input_block_is_not_modified) {
    NtUserEvent_pfn f = GN(NtUserEvent); REQUIRE(f);
    struct ev e, ref; memset(&e, 0xAB, sizeof e);
    e.mask = 0; e.type = 7; e.size = 12;
    ref = e;
    EXPECT_EQ(f(&e), 0);
    CHECK(!memcmp(&e, &ref, sizeof e));
}
TEST(gap2_event_repeatable) {
    NtUserEvent_pfn f = GN(NtUserEvent); REQUIRE(f);
    struct ev e; memset(&e, 0, sizeof e);
    e.type = 3; e.size = 4;
    for (int i = 0; i < 64; i++) EXPECT_EQ(f(&e), 0);
}

TEST(gap2_startupinfo_zero_mask_is_noop_success) {
    NtUserModifyUserStartupInfoFlags_pfn f = GN(NtUserModifyUserStartupInfoFlags); REQUIRE(f);
    EXPECT_EQ(f(0, 0), 1);
    EXPECT_EQ(f(0, 0xffffffff), 1);   /* new = old & ~0 | 0 & value == old */
    EXPECT_EQ(f(0, 0x12345678), 1);
}
TEST(gap2_startupinfo_returns_one_always) {
    NtUserModifyUserStartupInfoFlags_pfn f = GN(NtUserModifyUserStartupInfoFlags); REQUIRE(f);
    for (int i = 0; i < 8; i++) EXPECT_EQ(f(0, 1u << (i * 4)), 1);
}

/* ---------- dxgkrnl thunks ---------- */
TEST(gap2_dxgk_open_adapter_bogus_luid) {
    NtGdiDdDDIOpenAdapterFromLuid_pfn f = GN(NtGdiDdDDIOpenAdapterFromLuid); NtGdiDdDDICloseAdapter_pfn cl = GN(NtGdiDdDDICloseAdapter);
    REQUIRE(f);
    /* D3DKMT_OPENADAPTERFROMLUID { LUID AdapterLuid; D3DKMT_HANDLE hAdapter; } */
    struct { LUID luid; UINT hAdapter; UINT pad; } a;
    memset(&a, 0, sizeof a);
    a.luid.LowPart = 0x7f7f7f7f; a.luid.HighPart = 0x3e3e;
    LONG st = f(&a);
    t_note("OpenAdapterFromLuid(bogus) = 0x%lx", (unsigned long)st);
    CHECKF(st < 0, "bogus LUID unexpectedly opened (0x%lx)", (unsigned long)st);
    if (st >= 0 && cl) { UINT h = a.hAdapter; cl(&h); }
}
TEST(gap2_dxgk_zero_blocks_rejected) {
    struct { const char *n; LONG (WINAPI *f)(void *); } t[] = {
        { "NtGdiDdDDIOpenAdapterFromLuid", (LONG (WINAPI *)(void *))GN(NtGdiDdDDIOpenAdapterFromLuid) },
        { "NtGdiDdDDIGetMultiPlaneOverlayCaps", (LONG (WINAPI *)(void *))GN(NtGdiDdDDIGetMultiPlaneOverlayCaps) },
    };
    int ran = 0;
    for (size_t i = 0; i < 2; i++) {
        if (!t[i].f) continue;
        unsigned char blk[256]; memset(blk, 0, sizeof blk);
        LONG st = t[i].f(blk);
        t_note("%s(zeros) = 0x%lx", t[i].n, (unsigned long)st);
        /* an all-zero block names the NULL adapter (and for OpenAdapterFromLuid, LUID 0:0) */
        if (i == 1) CHECKF(st < 0, "%s accepted a NULL adapter handle: 0x%lx", t[i].n, (unsigned long)st);
        ran++;
    }
    if (!ran) t_skip("no dxgk exports");
}
TEST(gap2_dxgk_hybrid_query_zero_blocks_observed) {
    LONG (WINAPI *a)(void *) = (LONG (WINAPI *)(void *))GN(NtGdiDdDDICacheHybridQueryValue);
    LONG (WINAPI *b)(void *) = (LONG (WINAPI *)(void *))GN(NtGdiDdDDIGetCachedHybridQueryValue);
    if (!a && !b) { t_skip("exports missing"); return; }
    unsigned char blk[256]; memset(blk, 0, sizeof blk);
    /* argument layouts are not in the SDK headers here: record the status only */
    if (a) t_note("CacheHybridQueryValue(zeros) = 0x%lx", (unsigned long)a(blk));
    memset(blk, 0, sizeof blk);
    if (b) t_note("GetCachedHybridQueryValue(zeros) = 0x%lx", (unsigned long)b(blk));
}
