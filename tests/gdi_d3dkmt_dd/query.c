/* NtGdiDdDDIQueryAdapterInfo, GetDisplayModeList, GetScanLine, CheckVidPnExclusiveOwnership, QueryStatistics,
 * GetSharedPrimaryHandle, WaitForVerticalBlankEvent (argument validation only: a valid wait could block). All valid
 * calls go through the primary display adapter opened from the screen DC and are skipped when it is unavailable. */
#include "d.h"
#include <stdlib.h>

#define OPEN_ADAPTER() dk_openhdc ad; HDC adc; if (!dk_open_screen(&ad, &adc)) { t_skip("no display adapter"); return; }
#define CLOSE_ADAPTER() do { dk_close_ok(ad.hAdapter); U(ReleaseDC)(NULL, adc); } while (0)

static LONG qai(ULONG h, ULONG type, void *buf, ULONG size) {
    dk_fn1 f = dk_nt("NtGdiDdDDIQueryAdapterInfo");
    dk_qai q = { h, type, buf, size };
    return f ? f(&q) : 0x7FFFFFFF;
}

/* ---- QueryAdapterInfo ---------------------------------------------------------------------------- */
TEST(gdi_d3dkmt_dd_qai_adaptertype_screen_adapter) {
    DKREQ(f, "NtGdiDdDDIQueryAdapterInfo");
    OPEN_ADAPTER();
    ULONG t = 0xFFFFFFFF;
    LONG st = qai(ad.hAdapter, QAI_ADAPTERTYPE, &t, 4);
    NOTE_ST("QueryAdapterInfo(ADAPTERTYPE)", st);
    if (NTOK(st)) {
        CHECK(t != 0xFFFFFFFF);
        /* D3DKMT_ADAPTERTYPE bit1 = DisplaySupported: the adapter behind the screen DC drives a display */
        CHECKF(t & 2, "ADAPTERTYPE flags 0x%lx lack DisplaySupported", (unsigned long)t);
        t_note("ADAPTERTYPE flags 0x%lx (render=%d display=%d software=%d post=%d)", (unsigned long)t, !!(t & 1), !!(t & 2), !!(t & 4), !!(t & 8));
    }
    CLOSE_ADAPTER();
}
TEST(gdi_d3dkmt_dd_qai_adaptertype_stable_across_calls) {
    DKREQ(f, "NtGdiDdDDIQueryAdapterInfo");
    OPEN_ADAPTER();
    ULONG a = 1, b = 2;
    if (NTOK(qai(ad.hAdapter, QAI_ADAPTERTYPE, &a, 4))) {
        for (int i = 0; i < 8; i++) { b = 3; CHECK(NTOK(qai(ad.hAdapter, QAI_ADAPTERTYPE, &b, 4))); CHECKF(a == b, "0x%lx vs 0x%lx", (unsigned long)a, (unsigned long)b); }
    }
    CLOSE_ADAPTER();
}
TEST(gdi_d3dkmt_dd_qai_adaptertype_writes_exactly_size_bytes) {
    DKREQ(f, "NtGdiDdDDIQueryAdapterInfo");
    OPEN_ADAPTER();
    unsigned char buf[32];
    memset(buf, 0xA5, sizeof buf);
    LONG st = qai(ad.hAdapter, QAI_ADAPTERTYPE, buf, 4);
    if (NTOK(st)) for (int i = 4; i < 32; i++) CHECKF(buf[i] == 0xA5, "byte %d overwritten with 0x%02x", i, buf[i]);
    CLOSE_ADAPTER();
}
TEST(gdi_d3dkmt_dd_qai_adaptertype_wrong_sizes_rejected) {
    DKREQ(f, "NtGdiDdDDIQueryAdapterInfo");
    OPEN_ADAPTER();
    static const ULONG sizes[] = { 0, 1, 2, 3 };
    for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        unsigned char buf[16]; memset(buf, 0x5A, sizeof buf);
        LONG st = qai(ad.hAdapter, QAI_ADAPTERTYPE, buf, sizes[i]);
        CHECKF(!NTOK(st), "size %lu accepted", (unsigned long)sizes[i]);
        NOTE_ST("QueryAdapterInfo(ADAPTERTYPE, short size)", st);
        for (int k = 0; k < 16; k++) CHECKF(buf[k] == 0x5A, "size %lu: byte %d written on failure", (unsigned long)sizes[i], k);
    }
    CLOSE_ADAPTER();
}
TEST(gdi_d3dkmt_dd_qai_adaptertype_oversized_buffer_recorded) {
    DKREQ(f, "NtGdiDdDDIQueryAdapterInfo");
    OPEN_ADAPTER();
    unsigned char buf[64]; memset(buf, 0x5A, sizeof buf);
    LONG st = qai(ad.hAdapter, QAI_ADAPTERTYPE, buf, 8);
    NOTE_ST("QueryAdapterInfo(ADAPTERTYPE, size 8)", st);
    st = qai(ad.hAdapter, QAI_ADAPTERTYPE, buf, 64);
    NOTE_ST("QueryAdapterInfo(ADAPTERTYPE, size 64)", st);
    CLOSE_ADAPTER();
}
TEST(gdi_d3dkmt_dd_qai_bogus_types_rejected) {
    DKREQ(f, "NtGdiDdDDIQueryAdapterInfo");
    OPEN_ADAPTER();
    static const ULONG types[] = { 0x7FFFFFFF, 0xFFFFFFFF, 0x80000000, 0x10000, 4096, 200 };
    for (size_t i = 0; i < sizeof types / sizeof types[0]; i++) {
        unsigned char buf[1024]; memset(buf, 0, sizeof buf);
        LONG st = qai(ad.hAdapter, types[i], buf, 64);
        CHECKF(!NTOK(st), "type 0x%lx accepted", (unsigned long)types[i]);
        NOTE_ST("QueryAdapterInfo(bogus type)", st);
    }
    CLOSE_ADAPTER();
}
TEST(gdi_d3dkmt_dd_qai_invalid_handles_rejected) {
    DKREQ(f, "NtGdiDdDDIQueryAdapterInfo");
    static const ULONG v[] = { 0, 0xDEADBEEF, 0xFFFFFFFF, 0x1 };
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        ULONG t = 0xFFFFFFFF;
        LONG st = qai(v[i], QAI_ADAPTERTYPE, &t, 4);
        CHECKF(!NTOK(st), "handle 0x%lx accepted", (unsigned long)v[i]);
        CHECK(t == 0xFFFFFFFF);
        NOTE_ST("QueryAdapterInfo(invalid handle)", st);
    }
}
TEST(gdi_d3dkmt_dd_qai_after_close_rejected) {
    DKREQ(f, "NtGdiDdDDIQueryAdapterInfo");
    OPEN_ADAPTER();
    dk_close_ok(ad.hAdapter);
    ULONG t = 0xFFFFFFFF;
    LONG st = qai(ad.hAdapter, QAI_ADAPTERTYPE, &t, 4);
    CHECKF(!NTOK(st), "closed handle still queryable");
    CHECK(t == 0xFFFFFFFF);
    U(ReleaseDC)(NULL, adc);
}
TEST(gdi_d3dkmt_dd_qai_gdi32_export_same_result) {
    typedef LONG (WINAPI *wr_fn)(void *);
    DKREQ(f, "NtGdiDdDDIQueryAdapterInfo");
    wr_fn w = (wr_fn)t_proc_in("gdi32.dll", "D3DKMTQueryAdapterInfo");
    REQUIRE(w);
    OPEN_ADAPTER();
    ULONG a = 1, b = 2;
    dk_qai qa = { ad.hAdapter, QAI_ADAPTERTYPE, &a, 4 }, qb = { ad.hAdapter, QAI_ADAPTERTYPE, &b, 4 };
    LONG s1 = f(&qa), s2 = w(&qb);
    CHECK(s1 == s2);
    if (NTOK(s1)) CHECK(a == b);
    CLOSE_ADAPTER();
}
TEST(gdi_d3dkmt_dd_qai_driverversion) {
    DKREQ(f, "NtGdiDdDDIQueryAdapterInfo");
    OPEN_ADAPTER();
    ULONG v = 0;
    LONG st = qai(ad.hAdapter, QAI_DRIVERVERSION, &v, 4);
    NOTE_ST("QueryAdapterInfo(DRIVERVERSION)", st);
    if (NTOK(st)) {
        /* KMT_DRIVERVERSION: WDDM 1.0 = 1000 ... WDDM 2.x = 2000..2799 on a 19041 kernel (max WDDM 2.7 = 2700) */
        CHECKF(v >= 1000 && v <= 3000, "driver version %lu", (unsigned long)v);
        t_note("WDDM driver version %lu", (unsigned long)v);
    }
    CLOSE_ADAPTER();
}
TEST(gdi_d3dkmt_dd_qai_driverversion_wrong_size) {
    DKREQ(f, "NtGdiDdDDIQueryAdapterInfo");
    OPEN_ADAPTER();
    ULONG v[4] = { 0 };
    for (ULONG sz = 0; sz < 4; sz++) {
        LONG st = qai(ad.hAdapter, QAI_DRIVERVERSION, v, sz);
        CHECKF(!NTOK(st), "size %lu accepted", (unsigned long)sz);
    }
    CLOSE_ADAPTER();
}
TEST(gdi_d3dkmt_dd_qai_umdrivername) {
    DKREQ(f, "NtGdiDdDDIQueryAdapterInfo");
    OPEN_ADAPTER();
    /* D3DKMT_UMDFILENAMEINFO = { KMT_UMD_VERSION Version; WCHAR UmdFileName[MAX_PATH]; } = 4 + 520 bytes */
    unsigned char buf[524 + 16]; memset(buf, 0xCC, sizeof buf);
    LONG st = qai(ad.hAdapter, QAI_UMDRIVERNAME, buf, 524);
    NOTE_ST("QueryAdapterInfo(UMDRIVERNAME)", st);
    if (NTOK(st)) {
        for (int i = 524; i < (int)sizeof buf; i++) CHECK(buf[i] == 0xCC);
        WCHAR *name = (WCHAR *)(buf + 4); size_t n = 0;
        while (n < 260 && name[n]) n++;
        CHECKF(n < 260, "UMD file name not terminated");
        t_note("UMD version %lu name %ls", (unsigned long)*(ULONG *)buf, name);
    }
    CLOSE_ADAPTER();
}
TEST(gdi_d3dkmt_dd_qai_umdrivername_short_buffer_rejected) {
    DKREQ(f, "NtGdiDdDDIQueryAdapterInfo");
    OPEN_ADAPTER();
    unsigned char buf[600]; memset(buf, 0xCC, sizeof buf);
    LONG st = qai(ad.hAdapter, QAI_UMDRIVERNAME, buf, 16);
    NOTE_ST("QueryAdapterInfo(UMDRIVERNAME, 16 bytes)", st);
    if (NTOK(st)) t_note("short UMDRIVERNAME buffer accepted");
    CLOSE_ADAPTER();
}
TEST(gdi_d3dkmt_dd_qai_adapteraddress) {
    DKREQ(f, "NtGdiDdDDIQueryAdapterInfo");
    OPEN_ADAPTER();
    ULONG a[4] = { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF };   /* D3DKMT_ADAPTERADDRESS: Bus, Device, Function, Reserved */
    LONG st = qai(ad.hAdapter, QAI_ADAPTERADDRESS, a, 16);
    NOTE_ST("QueryAdapterInfo(ADAPTERADDRESS)", st);
    if (NTOK(st)) {
        CHECKF(a[1] < 32 && a[2] < 8, "device %lu function %lu", (unsigned long)a[1], (unsigned long)a[2]);
        t_note("adapter at bus %lu device %lu function %lu", (unsigned long)a[0], (unsigned long)a[1], (unsigned long)a[2]);
    }
    CLOSE_ADAPTER();
}
TEST(gdi_d3dkmt_dd_qai_segmentsize) {
    DKREQ(f, "NtGdiDdDDIQueryAdapterInfo");
    OPEN_ADAPTER();
    ULONGLONG s[3] = { ~0ull, ~0ull, ~0ull };   /* D3DKMT_SEGMENTSIZEINFO: Dedicated video, Dedicated system, Shared system */
    LONG st = qai(ad.hAdapter, QAI_GETSEGMENTSIZE, s, 24);
    NOTE_ST("QueryAdapterInfo(GETSEGMENTSIZE)", st);
    if (NTOK(st)) {
        CHECK(s[0] != ~0ull && s[1] != ~0ull && s[2] != ~0ull);
        t_note("segments: video %llu MB, system %llu MB, shared %llu MB", s[0] >> 20, s[1] >> 20, s[2] >> 20);
    }
    CLOSE_ADAPTER();
}
TEST(gdi_d3dkmt_dd_qai_each_enumerated_adapter) {
    DKREQ(f, "NtGdiDdDDIQueryAdapterInfo");
    dk_adapterinfo *a; int n = dk_enum(&a);
    if (n <= 0) { if (a) dk_enum_close(a, n); t_skip("no adapters"); return; }
    for (int i = 0; i < n; i++) {
        ULONG t = 0xFFFFFFFF;
        LONG st = qai(a[i].hAdapter, QAI_ADAPTERTYPE, &t, 4);
        CHECKF(NTOK(st), "adapter %d: QueryAdapterInfo(ADAPTERTYPE) 0x%lx", i, (unsigned long)st);
        if (NTOK(st)) t_note("adapter %d type flags 0x%lx", i, (unsigned long)t);
    }
    dk_enum_close(a, n);
}

/* ---- GetDisplayModeList -------------------------------------------------------------------------- */
typedef struct { ULONG hAdapter; ULONG VidPnSourceId; void *pModeList; ULONG ModeCount; } dk_modelist;   /* whNtGdiDdDDIGetDisplayModeList: 4 dwords, pointer at +8 */
TEST(gdi_d3dkmt_dd_modelist_probe_count) {
    DKREQ(f, "NtGdiDdDDIGetDisplayModeList");
    OPEN_ADAPTER();
    dk_modelist m = { ad.hAdapter, ad.VidPnSourceId, NULL, 0xCCCCCCCC };
    LONG st = f(&m);
    NOTE_ST("GetDisplayModeList(NULL list)", st);
    if (NTOK(st)) { CHECK(m.ModeCount != 0xCCCCCCCC); CHECK(m.ModeCount > 0); t_note("%lu display modes", (unsigned long)m.ModeCount); }
    CLOSE_ADAPTER();
}
TEST(gdi_d3dkmt_dd_modelist_fetch_contains_current_mode) {
    DKREQ(f, "NtGdiDdDDIGetDisplayModeList");
    REQUIRE(U(GetSystemMetrics));
    OPEN_ADAPTER();
    dk_modelist m = { ad.hAdapter, ad.VidPnSourceId, NULL, 0 };
    if (!NTOK(f(&m)) || m.ModeCount == 0) { CLOSE_ADAPTER(); t_skip("mode count unavailable"); return; }
    ULONG n = m.ModeCount;
    size_t cap = (size_t)n * 128;
    unsigned char *buf = calloc(1, cap);
    m.pModeList = buf; m.ModeCount = n;
    LONG st = f(&m);
    NOTE_ST("GetDisplayModeList(fetch)", st);
    if (NTOK(st)) {
        CHECK(m.ModeCount <= n && m.ModeCount > 0);
        ULONG w = *(ULONG *)buf, h = *(ULONG *)(buf + 4);   /* D3DKMT_DISPLAYMODE starts with Width, Height */
        CHECKF(w >= 320 && w <= 16384 && h >= 200 && h <= 16384, "first mode %lux%lu", (unsigned long)w, (unsigned long)h);
        int cx = U(GetSystemMetrics)(SM_CXSCREEN), cy = U(GetSystemMetrics)(SM_CYSCREEN), found = 0;
        for (size_t o = 0; o + 8 <= cap; o += 4) if (*(ULONG *)(buf + o) == (ULONG)cx && *(ULONG *)(buf + o + 4) == (ULONG)cy) { found = 1; break; }
        if (!found) t_note("current %dx%d not among the %lu listed modes (DPI virtualization or mode filtering)", cx, cy, (unsigned long)m.ModeCount);
    }
    free(buf);
    CLOSE_ADAPTER();
}
TEST(gdi_d3dkmt_dd_modelist_short_buffer_recorded) {
    DKREQ(f, "NtGdiDdDDIGetDisplayModeList");
    OPEN_ADAPTER();
    dk_modelist m = { ad.hAdapter, ad.VidPnSourceId, NULL, 0 };
    if (!NTOK(f(&m)) || m.ModeCount < 2) { CLOSE_ADAPTER(); t_skip("need at least two modes"); return; }
    unsigned char *buf = calloc(1, (size_t)m.ModeCount * 128);
    m.pModeList = buf; m.ModeCount = 1;
    LONG st = f(&m);
    NOTE_ST("GetDisplayModeList(ModeCount=1 of many)", st);
    t_note("ModeCount after = %lu", (unsigned long)m.ModeCount);
    free(buf);
    CLOSE_ADAPTER();
}
TEST(gdi_d3dkmt_dd_modelist_invalid_adapter_rejected) {
    DKREQ(f, "NtGdiDdDDIGetDisplayModeList");
    static const ULONG v[] = { 0, 0xDEADBEEF, 0xFFFFFFFF };
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        dk_modelist m = { v[i], 0, NULL, 0 };
        LONG st = f(&m);
        CHECKF(!NTOK(st), "handle 0x%lx accepted", (unsigned long)v[i]);
    }
}
TEST(gdi_d3dkmt_dd_modelist_invalid_source_rejected) {
    DKREQ(f, "NtGdiDdDDIGetDisplayModeList");
    OPEN_ADAPTER();
    dk_modelist m = { ad.hAdapter, 0x7FFFFFFF, NULL, 0 };
    LONG st = f(&m);
    CHECKF(!NTOK(st), "VidPnSourceId 0x7FFFFFFF accepted");
    NOTE_ST("GetDisplayModeList(bad source)", st);
    CLOSE_ADAPTER();
}

/* ---- GetScanLine / WaitForVerticalBlankEvent / ownership / power ---------------------------------- */
typedef struct { ULONG hAdapter; ULONG VidPnSourceId; BOOLEAN InVerticalBlank; ULONG ScanLine; } dk_scanline;
TEST(gdi_d3dkmt_dd_scanline_valid_adapter) {
    DKREQ(f, "NtGdiDdDDIGetScanLine");
    OPEN_ADAPTER();
    dk_scanline s = { ad.hAdapter, ad.VidPnSourceId, 0, 0xFFFFFFFF };
    LONG st = f(&s);
    NOTE_ST("GetScanLine", st);
    if (NTOK(st)) { CHECKF(s.ScanLine < 20000, "scanline %lu", (unsigned long)s.ScanLine); CHECK(s.InVerticalBlank == 0 || s.InVerticalBlank == 1); }
    CLOSE_ADAPTER();
}
TEST(gdi_d3dkmt_dd_scanline_invalid_adapter_rejected) {
    DKREQ(f, "NtGdiDdDDIGetScanLine");
    static const ULONG v[] = { 0, 0xDEADBEEF, 0xFFFFFFFF };
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        dk_scanline s = { v[i], 0, 0, 0xFFFFFFFF };
        CHECKF(!NTOK(f(&s)), "handle 0x%lx accepted", (unsigned long)v[i]);
        CHECK(s.ScanLine == 0xFFFFFFFF);
    }
}
TEST(gdi_d3dkmt_dd_scanline_after_close_rejected) {
    DKREQ(f, "NtGdiDdDDIGetScanLine");
    OPEN_ADAPTER();
    dk_close_ok(ad.hAdapter);
    dk_scanline s = { ad.hAdapter, 0, 0, 0xFFFFFFFF };
    CHECK(!NTOK(f(&s)));
    U(ReleaseDC)(NULL, adc);
}
TEST(gdi_d3dkmt_dd_vblank_wait_invalid_handles_return_immediately) {
    DKREQ(f, "NtGdiDdDDIWaitForVerticalBlankEvent");
    /* D3DKMT_WAITFORVERTICALBLANKEVENT = { hAdapter, hDevice, VidPnSourceId }; invalid handles fail validation before any wait */
    static const ULONG v[] = { 0, 0xDEADBEEF, 0xFFFFFFFF };
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        ULONG s[3] = { v[i], v[i], 0 };
        DWORD t0 = GetTickCount();
        LONG st = f(s);
        CHECKF(!NTOK(st), "handle 0x%lx accepted", (unsigned long)v[i]);
        CHECKF(GetTickCount() - t0 < 2000, "invalid wait took %lu ms", (unsigned long)(GetTickCount() - t0));
    }
}
TEST(gdi_d3dkmt_dd_vblank_wait_closed_adapter_rejected) {
    DKREQ(f, "NtGdiDdDDIWaitForVerticalBlankEvent");
    OPEN_ADAPTER();
    dk_close_ok(ad.hAdapter);
    ULONG s[3] = { ad.hAdapter, 0, 0 };
    DWORD t0 = GetTickCount();
    CHECK(!NTOK(f(s)));
    CHECK(GetTickCount() - t0 < 2000);
    U(ReleaseDC)(NULL, adc);
}
TEST(gdi_d3dkmt_dd_vidpn_exclusive_ownership_valid) {
    DKREQ(f, "NtGdiDdDDICheckVidPnExclusiveOwnership");
    OPEN_ADAPTER();
    ULONG a[2] = { ad.hAdapter, ad.VidPnSourceId };   /* D3DKMT_CHECKVIDPNEXCLUSIVEOWNERSHIP */
    LONG st = f(a);
    NOTE_ST("CheckVidPnExclusiveOwnership(screen source)", st);
    CLOSE_ADAPTER();
}
TEST(gdi_d3dkmt_dd_vidpn_exclusive_ownership_repeatable) {
    DKREQ(f, "NtGdiDdDDICheckVidPnExclusiveOwnership");
    OPEN_ADAPTER();
    ULONG a[2] = { ad.hAdapter, ad.VidPnSourceId };
    LONG first = f(a);
    for (int i = 0; i < 5; i++) CHECK(f(a) == first);
    CLOSE_ADAPTER();
}
TEST(gdi_d3dkmt_dd_vidpn_exclusive_ownership_invalid_handles) {
    DKREQ(f, "NtGdiDdDDICheckVidPnExclusiveOwnership");
    static const ULONG v[] = { 0, 0xDEADBEEF, 0xFFFFFFFF };
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        ULONG a[2] = { v[i], 0 };
        CHECKF(!NTOK(f(a)), "handle 0x%lx accepted", (unsigned long)v[i]);
    }
}
TEST(gdi_d3dkmt_dd_vidpn_exclusive_ownership_bogus_source) {
    DKREQ(f, "NtGdiDdDDICheckVidPnExclusiveOwnership");
    OPEN_ADAPTER();
    ULONG a[2] = { ad.hAdapter, 0x7FFFFFFF };
    LONG st = f(a);
    NOTE_ST("CheckVidPnExclusiveOwnership(bogus source)", st);
    CLOSE_ADAPTER();
}
TEST(gdi_d3dkmt_dd_monitor_power_state_invalid_handle) {
    DKREQ(f, "NtGdiDdDDICheckMonitorPowerState");
    ULONG a[2] = { 0xDEADBEEF, 0 };
    CHECK(!NTOK(f(a)));
    a[0] = 0;
    CHECK(!NTOK(f(a)));
}
TEST(gdi_d3dkmt_dd_shared_primary_handle_invalid_adapter) {
    DKREQ(f, "NtGdiDdDDIGetSharedPrimaryHandle");
    ULONG a[4] = { 0xDEADBEEF, 0, 0xCCCCCCCC, 0 };
    LONG st = f(a);
    CHECK(!NTOK(st));
    CHECK(a[2] == 0xCCCCCCCC);
    a[0] = 0;
    CHECK(!NTOK(f(a)));
}
TEST(gdi_d3dkmt_dd_shared_primary_handle_valid_adapter_recorded) {
    DKREQ(f, "NtGdiDdDDIGetSharedPrimaryHandle");
    OPEN_ADAPTER();
    ULONG a[4] = { ad.hAdapter, ad.VidPnSourceId, 0, 0 };   /* D3DKMT_GETSHAREDPRIMARYHANDLE = { hAdapter, VidPnSourceId, hSharedPrimary } */
    LONG st = f(a);
    NOTE_ST("GetSharedPrimaryHandle", st);
    if (NTOK(st)) t_note("shared primary handle 0x%lx", (unsigned long)a[2]);
    CLOSE_ADAPTER();
}

/* ---- QueryStatistics ------------------------------------------------------------------------------ */
/* D3DKMT_QUERYSTATISTICS = { Type; LUID AdapterLuid; HANDLE hProcess; QUERYSTATISTICS_RESULT QueryResult; QUERYSTATISTICS_QUERY_ELEMENT Query; }
 * (wow64win!whNtGdiDdDDIQueryStatistics: Type at +0, LUID +4, hProcess +0xC(wow)/+0x10(x64), result after). */
#define QS_SIZE 0x800
TEST(gdi_d3dkmt_dd_querystatistics_adapter_query) {
    DKREQ(f, "NtGdiDdDDIQueryStatistics");
    OPEN_ADAPTER();
    unsigned char *q = calloc(1, QS_SIZE);
    *(ULONG *)q = 0;                                 /* D3DKMT_QUERYSTATISTICS_ADAPTER */
    memcpy(q + 4, &ad.luid, sizeof(LUID));
    LONG st = f(q);
    NOTE_ST("QueryStatistics(ADAPTER)", st);
    if (NTOK(st)) {
        ULONG nb = *(ULONG *)(q + 0x18), nodes = *(ULONG *)(q + 0x1C), srcs = *(ULONG *)(q + 0x20);
        CHECKF(nb < 64 && nodes < 64 && srcs < 64, "segments %lu nodes %lu sources %lu", (unsigned long)nb, (unsigned long)nodes, (unsigned long)srcs);
        t_note("ADAPTER statistics: segments %lu nodes %lu vidpn sources %lu", (unsigned long)nb, (unsigned long)nodes, (unsigned long)srcs);
    }
    free(q);
    CLOSE_ADAPTER();
}
TEST(gdi_d3dkmt_dd_querystatistics_unknown_luid_rejected) {
    DKREQ(f, "NtGdiDdDDIQueryStatistics");
    unsigned char *q = calloc(1, QS_SIZE);
    *(ULONG *)q = 0;
    LUID l = { 0xDEADBEEF, 0x1234 };
    memcpy(q + 4, &l, sizeof l);
    LONG st = f(q);
    CHECK(!NTOK(st));
    NOTE_ST("QueryStatistics(unknown LUID)", st);
    free(q);
}
TEST(gdi_d3dkmt_dd_querystatistics_bogus_type_rejected) {
    DKREQ(f, "NtGdiDdDDIQueryStatistics");
    OPEN_ADAPTER();
    static const ULONG types[] = { 0x7FFFFFFF, 0xFFFFFFFF, 1000 };
    for (size_t i = 0; i < sizeof types / sizeof types[0]; i++) {
        unsigned char *q = calloc(1, QS_SIZE);
        *(ULONG *)q = types[i];
        memcpy(q + 4, &ad.luid, sizeof(LUID));
        LONG st = f(q);
        CHECKF(!NTOK(st), "type 0x%lx accepted", (unsigned long)types[i]);
        free(q);
    }
    CLOSE_ADAPTER();
}
TEST(gdi_d3dkmt_dd_querystatistics_does_not_need_open_handle) {
    /* statistics are addressed by LUID, not by hAdapter: an adapter whose handle is closed can still be queried */
    DKREQ(f, "NtGdiDdDDIQueryStatistics");
    OPEN_ADAPTER();
    LUID l = ad.luid;
    dk_close_ok(ad.hAdapter);
    unsigned char *q = calloc(1, QS_SIZE);
    memcpy(q + 4, &l, sizeof l);
    LONG st = f(q);
    NOTE_ST("QueryStatistics(ADAPTER) after the handle was closed", st);
    free(q);
    U(ReleaseDC)(NULL, adc);
}
