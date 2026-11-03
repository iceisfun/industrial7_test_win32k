/* Handle matrices. Every call that takes an OPM protected-output handle or a physical-monitor handle is given handle
 * values that cannot name a live object: zero and small values, all-ones and values above 32 bits, and 32-bit patterns.
 * Windows answers an OPM handle with one status and a monitor handle with another, regardless of the other arguments
 * (code, size and type values are not examined before the handle), and writes none of the output buffers. The tests run
 * in a process that holds no live protected output or monitor handle. */
#include "xo.h"

static const ULONG_PTR cls_small[] = { 0, 1, 0x1234 };
static const ULONG_PTR cls_wide[]  = { (ULONG_PTR)-1, 0x100000000ULL, 0x7FFFFFFFFFFFFFFFULL, 0x8000000000000000ULL };
static const ULONG_PTR cls_pat[]   = { 0xDEADBEEF, 0x40000000, 0x80000000, 0xFFFFFFFF, 0xFFFF };

typedef long (*hcall)(HANDLE);
static void run(hcall fn, unsigned long want, const ULONG_PTR *v, int n) {
    for (int i = 0; i < n; i++) {
        long s = fn((HANDLE)v[i]);
        CHECKF(ST(s) == want, "handle %p: status 0x%lx want 0x%lx", (void *)v[i], ST(s), want);
    }
}

#define SENT 0xB7
static long c_opm_destroy(HANDLE h)  { return UN(NtGdiDestroyOPMProtectedOutput)(h); }
static long c_opm_random(HANDLE h)   { memset(xo_a, SENT, 64); long s = UN(NtGdiGetOPMRandomNumber)(h, xo_a); CHECK(xo_untouched(xo_a, 64, SENT)); return s; }
static long c_opm_setkey(HANDLE h)   { memset(xo_a, 0, 0x100); return UN(NtGdiSetOPMSigningKeyAndSequenceNumbers)(h, xo_a); }
static long conf(HANDLE h, ULONG cb) { memset(xo_a, 0, sizeof xo_a); memset(xo_b, SENT, sizeof xo_b); long s = UN(NtGdiConfigureOPMProtectedOutput)(h, xo_a, cb, xo_b); CHECK(xo_untouched(xo_b, 0x1000, SENT)); return s; }
static long c_opm_conf_0(HANDLE h)    { return conf(h, 0); }
static long c_opm_conf_20(HANDLE h)   { return conf(h, 0x20); }
static long c_opm_conf_1000(HANDLE h) { return conf(h, 0x1000); }
static long c_opm_conf_2000(HANDLE h)  { return conf(h, 0x2000); }
static long c_opm_info(HANDLE h)      { memset(xo_a, 0, sizeof xo_a); memset(xo_b, SENT, sizeof xo_b); long s = UN(NtGdiGetOPMInformation)(h, xo_a, xo_b); CHECK(xo_untouched(xo_b, 0x1000, SENT)); return s; }
static long c_opm_copp(HANDLE h)      { memset(xo_a, 0, sizeof xo_a); memset(xo_b, SENT, sizeof xo_b); long s = UN(NtGdiGetCOPPCompatibleOPMInformation)(h, xo_a, xo_b); CHECK(xo_untouched(xo_b, 0x1000, SENT)); return s; }
static long certsz(HANDLE h, ULONG t) { ULONG sz = 0x5A5A5A5A; long s = UN(NtGdiGetCertificateSizeByHandle)(h, t, &sz); CHECK(sz == 0x5A5A5A5A); return s; }
static long c_opm_certsz_0(HANDLE h)  { return certsz(h, 0); }
static long c_opm_certsz_2(HANDLE h)  { return certsz(h, 2); }
static long c_opm_certsz_7(HANDLE h)  { return certsz(h, 7); }
static long c_opm_certsz_big(HANDLE h){ return certsz(h, 0xFFFFFFFF); }
static long cert(HANDLE h, ULONG t, ULONG sz) { memset(xo_a, SENT, 0x400); long s = UN(NtGdiGetCertificateByHandle)(h, t, xo_a, sz); CHECK(xo_untouched(xo_a, 0x400, SENT)); return s; }
static long c_opm_cert_0_0(HANDLE h)       { return cert(h, 0, 0); }
static long c_opm_cert_1_1(HANDLE h)       { return cert(h, 1, 1); }
static long c_opm_cert_2_400(HANDLE h)     { return cert(h, 2, 0x400); }
static long c_opm_cert_7_big(HANDLE h)     { return cert(h, 7, 0xFFFFFFFF); }

static long getvcp(HANDLE h, ULONG code) { ULONG t = 0x11111111, c = 0x22222222, m = 0x33333333; long s = UN(NtGdiDDCCIGetVCPFeature)(h, code, &t, &c, &m); CHECK(t == 0x11111111 && c == 0x22222222 && m == 0x33333333); return s; }
static long c_ddc_getvcp_10(HANDLE h)    { return getvcp(h, 0x10); }
static long c_ddc_getvcp_0(HANDLE h)     { return getvcp(h, 0); }
static long c_ddc_getvcp_100(HANDLE h)   { return getvcp(h, 0x100); }
static long c_ddc_getvcp_big(HANDLE h)   { return getvcp(h, 0xFFFFFFFF); }
/* A handle that is not live never reaches the monitor, so these cannot change any setting. */
static long c_ddc_setvcp_10(HANDLE h)    { return UN(NtGdiDDCCISetVCPFeature)(h, 0x10, 0); }
static long c_ddc_setvcp_big(HANDLE h)   { return UN(NtGdiDDCCISetVCPFeature)(h, 0xFFFFFFFF, 0xFFFFFFFF); }
static long c_ddc_timing(HANDLE h)       { memset(xo_a, SENT, 64); long s = UN(NtGdiDDCCIGetTimingReport)(h, xo_a); CHECK(xo_untouched(xo_a, 64, SENT)); return s; }
static long c_ddc_capslen(HANDLE h)      { ULONG l = 0xA5A5A5A5; long s = UN(NtGdiDDCCIGetCapabilitiesStringLength)(h, &l); CHECK(l == 0xA5A5A5A5); return s; }
static long capsstr(HANDLE h, ULONG n)   { memset(xo_a, SENT, 64); long s = UN(NtGdiDDCCIGetCapabilitiesString)(h, xo_a, n); CHECK(xo_untouched(xo_a, 64, SENT)); return s; }
static long c_ddc_caps_1(HANDLE h)       { return capsstr(h, 1); }
static long c_ddc_caps_400(HANDLE h)     { return capsstr(h, 0x400); }
static long c_ddc_caps_big(HANDLE h)     { return capsstr(h, 0xFFFFFFFF); }
static long c_ddc_save(HANDLE h)         { return UN(NtGdiDDCCISaveCurrentSettings)(h); }
static long c_phys_destroy(HANDLE h)     { return UN(NtGdiDestroyPhysicalMonitor)(h); }
static long c_phys_desc(HANDLE h)        { static WCHAR d[0x80]; wmemset(d, 0x5A5A, 0x80); long s = UN(NtGdiGetPhysicalMonitorDescription)(h, 0x80, d); CHECK(d[0] == 0x5A5A && d[0x7F] == 0x5A5A); return s; }

#define NEEDS_OPM() NEED(NtGdiDestroyOPMProtectedOutput); NEED(NtGdiGetOPMRandomNumber); NEED(NtGdiSetOPMSigningKeyAndSequenceNumbers); NEED(NtGdiConfigureOPMProtectedOutput); \
    NEED(NtGdiGetOPMInformation); NEED(NtGdiGetCOPPCompatibleOPMInformation); NEED(NtGdiGetCertificateSizeByHandle); NEED(NtGdiGetCertificateByHandle)
#define NEEDS_DDC() NEED(NtGdiDDCCIGetVCPFeature); NEED(NtGdiDDCCISetVCPFeature); NEED(NtGdiDDCCIGetTimingReport); NEED(NtGdiDDCCIGetCapabilitiesStringLength); \
    NEED(NtGdiDDCCIGetCapabilitiesString); NEED(NtGdiDDCCISaveCurrentSettings); NEED(NtGdiDestroyPhysicalMonitor); NEED(NtGdiGetPhysicalMonitorDescription)

#define GEN(kind, name, st) \
    XT(h_##name##_small) { NEEDS_##kind(); run(c_##name, st, cls_small, 3); } \
    XT(h_##name##_wide)  { NEEDS_##kind(); run(c_##name, st, cls_wide, 4); } \
    XT(h_##name##_pattern) { NEEDS_##kind(); run(c_##name, st, cls_pat, 5); }
#define OPMH XO_ST_OPM_HANDLE
#define DDCH XO_ST_DDC_HANDLE
GEN(OPM, opm_destroy, OPMH)   GEN(OPM, opm_random, OPMH)    GEN(OPM, opm_setkey, OPMH)
GEN(OPM, opm_conf_0, OPMH)    GEN(OPM, opm_conf_20, OPMH)   GEN(OPM, opm_conf_1000, OPMH)  GEN(OPM, opm_conf_2000, OPMH)
GEN(OPM, opm_info, OPMH)      GEN(OPM, opm_copp, OPMH)
GEN(OPM, opm_certsz_0, OPMH)  GEN(OPM, opm_certsz_2, OPMH)  GEN(OPM, opm_certsz_7, OPMH)   GEN(OPM, opm_certsz_big, OPMH)
GEN(OPM, opm_cert_0_0, OPMH)  GEN(OPM, opm_cert_1_1, OPMH)  GEN(OPM, opm_cert_2_400, OPMH) GEN(OPM, opm_cert_7_big, OPMH)
GEN(DDC, ddc_getvcp_10, DDCH) GEN(DDC, ddc_getvcp_0, DDCH)  GEN(DDC, ddc_getvcp_100, DDCH) GEN(DDC, ddc_getvcp_big, DDCH)
GEN(DDC, ddc_setvcp_10, DDCH) GEN(DDC, ddc_setvcp_big, DDCH)
GEN(DDC, ddc_timing, DDCH)    GEN(DDC, ddc_capslen, DDCH)
GEN(DDC, ddc_caps_1, DDCH)    GEN(DDC, ddc_caps_400, DDCH)  GEN(DDC, ddc_caps_big, DDCH)
GEN(DDC, ddc_save, DDCH)      GEN(DDC, phys_destroy, DDCH)  GEN(DDC, phys_desc, DDCH)

/* The capabilities-string buffer length is checked before the handle: length 0 is an invalid parameter for any handle. */
static long c_ddc_caps_0(HANDLE h) { return capsstr(h, 0); }
XT(h_ddc_caps_zero_length_small)   { NEEDS_DDC(); run(c_ddc_caps_0, XO_ST_INVALID_PARAM, cls_small, 3); }
XT(h_ddc_caps_zero_length_wide)    { NEEDS_DDC(); run(c_ddc_caps_0, XO_ST_INVALID_PARAM, cls_wide, 4); }
XT(h_ddc_caps_zero_length_pattern) { NEEDS_DDC(); run(c_ddc_caps_0, XO_ST_INVALID_PARAM, cls_pat, 5); }
/* The monitor description length must be exactly 0x80 characters, checked before the handle. */
XT(h_phys_desc_wrong_length_is_invalid_parameter) {
    NEED(NtGdiGetPhysicalMonitorDescription);
    static const ULONG lens[] = { 0, 1, 0x7F, 0x81, 0x100, 0xFFFFFFFF };
    static WCHAR d[0x200];
    for (unsigned i = 0; i < 6; i++) for (unsigned k = 0; k < 3; k++) {
        wmemset(d, 0x5A5A, 0x200);
        long s = UN(NtGdiGetPhysicalMonitorDescription)((HANDLE)cls_small[k], lens[i], d);
        CHECKF(ST(s) == XO_ST_INVALID_PARAM, "len %lx handle %p: 0x%lx", lens[i], (void *)cls_small[k], ST(s));
        CHECK(d[0] == 0x5A5A);
    }
}
/* The same bogus handle gives the same answer every time (no state is created by a failed lookup). */
XT(h_failed_lookups_are_repeatable) {
    NEEDS_OPM(); NEEDS_DDC();
    for (int i = 0; i < 4; i++) {
        CHECK(ST(c_opm_destroy((HANDLE)0x1234)) == OPMH);
        CHECK(ST(c_ddc_save((HANDLE)0x1234)) == DDCH);
        CHECK(ST(c_phys_destroy((HANDLE)0)) == DDCH);
    }
}
/* The two namespaces are separate: a failed monitor destroy on a value does not affect protected-output lookups. */
XT(h_opm_and_monitor_handle_errors_differ) {
    NEEDS_OPM(); NEEDS_DDC();
    CHECK(ST(c_opm_destroy((HANDLE)7)) != ST(c_phys_destroy((HANDLE)7)));
}
