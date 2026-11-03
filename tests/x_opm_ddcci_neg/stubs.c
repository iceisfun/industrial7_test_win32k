/* The win32u stubs of the monitor-control syscalls: each exists, has the SYSCALL stub shape and a service number in the
 * win32k range, and no two of them share a number. */
#include "xo.h"

static const char *const names[] = {
    "NtGdiCreateOPMProtectedOutput", "NtGdiCreateOPMProtectedOutputs", "NtGdiDestroyOPMProtectedOutput", "NtGdiConfigureOPMProtectedOutput",
    "NtGdiGetOPMInformation", "NtGdiGetOPMRandomNumber", "NtGdiGetCOPPCompatibleOPMInformation", "NtGdiSetOPMSigningKeyAndSequenceNumbers",
    "NtGdiGetCertificate", "NtGdiGetCertificateSize", "NtGdiGetCertificateByHandle", "NtGdiGetCertificateSizeByHandle",
    "NtGdiGetSuggestedOPMProtectedOutputArraySize", "NtGdiDDCCIGetVCPFeature", "NtGdiDDCCISetVCPFeature", "NtGdiDDCCIGetTimingReport",
    "NtGdiDDCCIGetCapabilitiesStringLength", "NtGdiDDCCIGetCapabilitiesString", "NtGdiDDCCISaveCurrentSettings", "NtGdiGetGammaRampCapability",
    "NtGdiGetPhysicalMonitors", "NtGdiDestroyPhysicalMonitor", "NtGdiGetPhysicalMonitorDescription",
};
#define NNAMES (sizeof names / sizeof *names)
static void check_stub(unsigned i) {
    if (t_is_wine()) { t_skip("emulator"); return; }
    FARPROC p = t_proc_in("win32u.dll", names[i]);
    if (!p) { CHECKF(0, "win32u!%s missing", names[i]); return; }
    CHECKF(k_stub_valid(p), "%s: not a syscall stub", names[i]);
    long n = k_stub_number(p);
    CHECKF(n >= 0x1000, "%s: service number %ld below the win32k range", names[i], n);
    CHECKF(p == t_proc(names[i]), "%s: the search order does not resolve it to win32u", names[i]);
}
#define GEN(i) XT(stub_##i) { check_stub(i); }
GEN(0) GEN(1) GEN(2) GEN(3) GEN(4) GEN(5) GEN(6) GEN(7) GEN(8) GEN(9) GEN(10) GEN(11) GEN(12) GEN(13) GEN(14) GEN(15) GEN(16) GEN(17) GEN(18) GEN(19) GEN(20) GEN(21) GEN(22)

XT(stub_service_numbers_are_distinct) {
    if (t_is_wine()) { t_skip("emulator"); return; }
    long n[NNAMES];
    for (unsigned i = 0; i < NNAMES; i++) { FARPROC p = t_proc_in("win32u.dll", names[i]); n[i] = p ? k_stub_number(p) : -1; }
    for (unsigned i = 0; i < NNAMES; i++) for (unsigned j = i + 1; j < NNAMES; j++)
        CHECKF(n[i] < 0 || n[i] != n[j], "%s and %s share number %ld", names[i], names[j], n[i]);
}
