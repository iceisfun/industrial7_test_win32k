/* Physical-monitor handles from NtGdiGetPhysicalMonitors, used only for lifetime and lookup rules. Handle lifetime is a
 * table lookup and does not need DDC/CI support from the monitor, so it is checked everywhere a monitor handle can be
 * obtained. The calls that talk to the monitor are only made with read-only requests, and only when the monitor answers
 * "not supported" (a virtual display), so no setting is read or written on real hardware. Nothing here calls
 * NtGdiDDCCISetVCPFeature or NtGdiDDCCISaveCurrentSettings with a live handle. Every handle taken is destroyed. */
#include "xo.h"

#define NEEDS() NEED(NtGdiGetPhysicalMonitors); NEED(NtGdiDestroyPhysicalMonitor); NEED(NtGdiGetPhysicalMonitorDescription); NEED(NtGdiDDCCIGetVCPFeature); \
    NEED(NtGdiDDCCISetVCPFeature); NEED(NtGdiDDCCIGetTimingReport); NEED(NtGdiDDCCIGetCapabilitiesStringLength); NEED(NtGdiDDCCIGetCapabilitiesString); \
    NEED(NtGdiDDCCISaveCurrentSettings)
#define LIVE(h) HANDLE h; if (!xo_get_monitor(&h)) { t_skip("no physical monitor handle available for the primary display device"); return; }
#define DESTROYED(c, what) CHECKF(ST(c) == XO_ST_DDC_HANDLE, "%s on a destroyed handle: status 0x%lx", what, ST(c))

XT(live_destroy_succeeds_once) {
    NEEDS(); LIVE(h);
    CHECK(UN(NtGdiDestroyPhysicalMonitor)(h) == 0);
    DESTROYED(UN(NtGdiDestroyPhysicalMonitor)(h), "second destroy");
}
XT(live_stale_getvcp) {
    NEEDS(); LIVE(h); xo_put_monitor(h);
    ULONG t = 1, c = 2, m = 3; DESTROYED(UN(NtGdiDDCCIGetVCPFeature)(h, 0x10, &t, &c, &m), "get VCP"); CHECK(t == 1 && c == 2 && m == 3);
}
XT(live_stale_setvcp) {          /* never reaches the monitor: the handle is gone */
    NEEDS(); LIVE(h); xo_put_monitor(h);
    DESTROYED(UN(NtGdiDDCCISetVCPFeature)(h, 0x10, 0), "set VCP");
}
XT(live_stale_timing_report) {
    NEEDS(); LIVE(h); xo_put_monitor(h);
    memset(xo_a, 0xB7, 64); DESTROYED(UN(NtGdiDDCCIGetTimingReport)(h, xo_a), "timing report"); CHECK(xo_untouched(xo_a, 64, 0xB7));
}
XT(live_stale_capabilities_length) {
    NEEDS(); LIVE(h); xo_put_monitor(h);
    ULONG l = 0xA5A5A5A5; DESTROYED(UN(NtGdiDDCCIGetCapabilitiesStringLength)(h, &l), "capabilities length"); CHECK(l == 0xA5A5A5A5);
}
XT(live_stale_capabilities_string) {
    NEEDS(); LIVE(h); xo_put_monitor(h);
    memset(xo_a, 0xB7, 64); DESTROYED(UN(NtGdiDDCCIGetCapabilitiesString)(h, xo_a, 16), "capabilities string"); CHECK(xo_untouched(xo_a, 64, 0xB7));
}
XT(live_stale_save_current_settings) {
    NEEDS(); LIVE(h); xo_put_monitor(h);
    DESTROYED(UN(NtGdiDDCCISaveCurrentSettings)(h), "save current settings");
}
XT(live_stale_description) {
    NEEDS(); LIVE(h); xo_put_monitor(h);
    static WCHAR d[0x80]; wmemset(d, 0x5A5A, 0x80); DESTROYED(UN(NtGdiGetPhysicalMonitorDescription)(h, 0x80, d), "description"); CHECK(d[0] == 0x5A5A);
}
XT(live_zero_length_checked_before_lookup_of_live_handle) {
    NEEDS(); LIVE(h);
    memset(xo_a, 0xB7, 64);
    CHECK(ST(UN(NtGdiDDCCIGetCapabilitiesString)(h, xo_a, 0)) == XO_ST_INVALID_PARAM);
    CHECK(xo_untouched(xo_a, 64, 0xB7));
    xo_put_monitor(h);
}
XT(live_description_wrong_length_is_invalid_parameter) {
    NEEDS(); LIVE(h);
    static WCHAR d[0x200];
    static const ULONG lens[] = { 0, 1, 0x7F, 0x81, 0x200 };
    for (int i = 0; i < 5; i++) { wmemset(d, 0x5A5A, 0x200); CHECKF(ST(UN(NtGdiGetPhysicalMonitorDescription)(h, lens[i], d)) == XO_ST_INVALID_PARAM, "length %lx", lens[i]); CHECK(d[0] == 0x5A5A); }
    xo_put_monitor(h);
}
XT(live_description_is_a_terminated_string_and_stable) {
    NEEDS(); LIVE(h);
    static WCHAR d1[0x80], d2[0x80];
    wmemset(d1, 0x5A5A, 0x80); wmemset(d2, 0x5A5A, 0x80);
    CHECK(UN(NtGdiGetPhysicalMonitorDescription)(h, 0x80, d1) == 0);
    CHECK(UN(NtGdiGetPhysicalMonitorDescription)(h, 0x80, d2) == 0);
    CHECK(wcsnlen(d1, 0x80) < 0x80);                        /* terminated inside the 0x80 characters */
    CHECK(!memcmp(d1, d2, sizeof d1));
    xo_put_monitor(h);
}
XT(live_aliases_of_a_live_handle_are_rejected) {
    NEEDS(); LIVE(h); ULONG l = 0;
    static const ULONG_PTR deltas[] = { 0x100000000ULL, 1000, 0x80000000ULL };
    for (int i = 0; i < 3; i++) {
        HANDLE a = (HANDLE)((ULONG_PTR)h + deltas[i]);
        CHECKF(ST(UN(NtGdiDDCCIGetCapabilitiesStringLength)(a, &l)) == XO_ST_DDC_HANDLE, "alias +%llx", (unsigned long long)deltas[i]);
    }
    CHECK(ST(UN(NtGdiDDCCIGetCapabilitiesStringLength)((HANDLE)(ULONG_PTR)-1, &l)) == XO_ST_DDC_HANDLE);
    CHECK(ST(UN(NtGdiDestroyPhysicalMonitor)((HANDLE)((ULONG_PTR)h + 0x100000000ULL))) == XO_ST_DDC_HANDLE);
    CHECK(UN(NtGdiDestroyPhysicalMonitor)(h) == 0);          /* the real handle was unaffected */
}
XT(live_two_calls_give_independent_handles) {
    NEEDS(); LIVE(a); LIVE(b);
    CHECK(a != b);
    CHECK(UN(NtGdiDestroyPhysicalMonitor)(a) == 0);
    ULONG l = 0;
    unsigned long st = ST(UN(NtGdiDDCCIGetCapabilitiesStringLength)(b, &l));
    CHECKF(st != XO_ST_DDC_HANDLE, "second handle died with the first: 0x%lx", st);
    DESTROYED(UN(NtGdiDDCCIGetCapabilitiesStringLength)(a, &l), "first handle");
    CHECK(UN(NtGdiDestroyPhysicalMonitor)(b) == 0);
    DESTROYED(UN(NtGdiDDCCIGetCapabilitiesStringLength)(b, &l), "second handle");
}
XT(live_handles_are_gone_after_destroy_of_both) {
    NEEDS(); LIVE(a);
    CHECK(UN(NtGdiDestroyPhysicalMonitor)(a) == 0);
    for (int i = 0; i < 3; i++) { ULONG l = 0; DESTROYED(UN(NtGdiDDCCIGetCapabilitiesStringLength)(a, &l), "lookup"); }
}

/* ---- read-only requests on a monitor that has no DDC/CI ("not supported") ---- */
#define UNSUP(h) if (!xo_ddc_unsupported(h)) { t_skip("the monitor answers DDC/CI requests; only the not-supported path is asserted"); xo_put_monitor(h); return; }
#define NS(c, what) CHECKF(ST(c) == XO_ST_NOT_SUPPORTED, "%s: status 0x%lx", what, ST(c))
XT(live_unsupported_capabilities_length) {
    NEEDS(); LIVE(h); UNSUP(h);
    ULONG l = 0xA5A5A5A5; NS(UN(NtGdiDDCCIGetCapabilitiesStringLength)(h, &l), "length"); CHECK(l == 0xA5A5A5A5); xo_put_monitor(h);
}
XT(live_unsupported_capabilities_string) {
    NEEDS(); LIVE(h); UNSUP(h);
    memset(xo_a, 0xB7, 64); NS(UN(NtGdiDDCCIGetCapabilitiesString)(h, xo_a, 16), "string 16"); NS(UN(NtGdiDDCCIGetCapabilitiesString)(h, xo_a, 1), "string 1");
    CHECK(xo_untouched(xo_a, 64, 0xB7)); xo_put_monitor(h);
}
XT(live_unsupported_get_vcp) {
    NEEDS(); LIVE(h); UNSUP(h);
    static const ULONG codes[] = { 0x10, 0, 0x100, 0xFFFFFFFF };
    for (int i = 0; i < 4; i++) { ULONG t = 1, c = 2, m = 3; NS(UN(NtGdiDDCCIGetVCPFeature)(h, codes[i], &t, &c, &m), "get VCP"); CHECK(t == 1 && c == 2 && m == 3); }
    xo_put_monitor(h);
}
XT(live_unsupported_timing_report) {
    NEEDS(); LIVE(h); UNSUP(h);
    memset(xo_a, 0xB7, 64); NS(UN(NtGdiDDCCIGetTimingReport)(h, xo_a), "timing"); CHECK(xo_untouched(xo_a, 64, 0xB7)); xo_put_monitor(h);
}
XT(live_unsupported_is_answered_before_output_pointers_are_checked) {
    NEEDS(); LIVE(h); UNSUP(h);
    NS(UN(NtGdiDDCCIGetCapabilitiesStringLength)(h, NULL), "length, NULL");
    NS(UN(NtGdiDDCCIGetCapabilitiesString)(h, NULL, 16), "string, NULL buffer");
    NS(UN(NtGdiDDCCIGetVCPFeature)(h, 0x10, NULL, NULL, NULL), "get VCP, NULL outputs");
    NS(UN(NtGdiDDCCIGetTimingReport)(h, NULL), "timing, NULL");
    xo_put_monitor(h);
}
XT(live_unsupported_is_stable) {
    NEEDS(); LIVE(h); UNSUP(h);
    for (int i = 0; i < 4; i++) { ULONG l = 0; NS(UN(NtGdiDDCCIGetCapabilitiesStringLength)(h, &l), "repeat"); }
    xo_put_monitor(h);
}
