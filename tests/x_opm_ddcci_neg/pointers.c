/* Pointer-parameter ordering. For a bogus handle the kernel either rejects an unusable pointer first (the buffers it
 * copies in, and the output block of ConfigureOPMProtectedOutput) or reaches the handle lookup first (every other
 * output pointer). NULL, an odd address, a kernel address and the first address above user space all give the same
 * answer, and none of them faults. */
#include "xo.h"

static void *const kinds[4] = { NULL, (void *)1, KERNEL_PTR, (void *)0x00007FFFFFFF0000ULL };
static const char *const kind_name[4] = { "null", "odd", "kernel", "above-user" };
static const ULONG_PTR hs[] = { 0, 0x1234, (ULONG_PTR)-1 };

#define NEEDS() NEED(NtGdiSetOPMSigningKeyAndSequenceNumbers); NEED(NtGdiConfigureOPMProtectedOutput); NEED(NtGdiGetOPMInformation); \
    NEED(NtGdiGetCOPPCompatibleOPMInformation); NEED(NtGdiGetOPMRandomNumber); NEED(NtGdiGetCertificateSizeByHandle); NEED(NtGdiGetCertificateByHandle); \
    NEED(NtGdiDDCCIGetVCPFeature); NEED(NtGdiDDCCIGetTimingReport); NEED(NtGdiDDCCIGetCapabilitiesStringLength); NEED(NtGdiDDCCIGetCapabilitiesString)
#define W(c, h, want, what) CHECKF(ST(c) == (want), "%s, handle %p: status 0x%lx want 0x%lx", what, (void *)(h), ST(c), (unsigned long)(want))

static void group_first(void *q) {      /* pointer rejected before the handle is looked at */
    for (int i = 0; i < 3; i++) { HANDLE h = (HANDLE)hs[i];
        memset(xo_a, 0, sizeof xo_a); memset(xo_b, 0xB7, sizeof xo_b);
        W(UN(NtGdiSetOPMSigningKeyAndSequenceNumbers)(h, q), h, XO_ST_PARAM_PTR, "setkey input");
        W(UN(NtGdiConfigureOPMProtectedOutput)(h, q, 0x20, xo_b), h, XO_ST_PARAM_PTR, "configure input");
        W(UN(NtGdiConfigureOPMProtectedOutput)(h, xo_a, 0x20, q), h, XO_ST_PARAM_PTR, "configure output");
        W(UN(NtGdiGetOPMInformation)(h, q, xo_b), h, XO_ST_PARAM_PTR, "info input");
        W(UN(NtGdiGetCOPPCompatibleOPMInformation)(h, q, xo_b), h, XO_ST_PARAM_PTR, "copp input");
        CHECK(xo_untouched(xo_b, 0x1000, 0xB7));
    }
}
static void group_opm_late(void *q) {   /* handle looked up first */
    for (int i = 0; i < 3; i++) { HANDLE h = (HANDLE)hs[i];
        memset(xo_a, 0, sizeof xo_a);
        W(UN(NtGdiGetOPMRandomNumber)(h, q), h, XO_ST_OPM_HANDLE, "random number output");
        W(UN(NtGdiGetOPMInformation)(h, xo_a, q), h, XO_ST_OPM_HANDLE, "info output");
        W(UN(NtGdiGetCOPPCompatibleOPMInformation)(h, xo_a, q), h, XO_ST_OPM_HANDLE, "copp output");
        W(UN(NtGdiGetCertificateSizeByHandle)(h, 0, q), h, XO_ST_OPM_HANDLE, "certificate size output");
        W(UN(NtGdiGetCertificateByHandle)(h, 0, q, 0x100), h, XO_ST_OPM_HANDLE, "certificate buffer");
    }
}
static void group_ddc(void *q) {
    for (int i = 0; i < 3; i++) { HANDLE h = (HANDLE)hs[i];
        W(UN(NtGdiDDCCIGetVCPFeature)(h, 0x10, q, (ULONG *)xo_a, (ULONG *)xo_a + 1), h, XO_ST_DDC_HANDLE, "vcp type output");
        W(UN(NtGdiDDCCIGetVCPFeature)(h, 0x10, (ULONG *)xo_a, q, (ULONG *)xo_a + 1), h, XO_ST_DDC_HANDLE, "vcp current output");
        W(UN(NtGdiDDCCIGetVCPFeature)(h, 0x10, (ULONG *)xo_a, (ULONG *)xo_a + 1, q), h, XO_ST_DDC_HANDLE, "vcp maximum output");
        W(UN(NtGdiDDCCIGetTimingReport)(h, q), h, XO_ST_DDC_HANDLE, "timing report output");
        W(UN(NtGdiDDCCIGetCapabilitiesStringLength)(h, q), h, XO_ST_DDC_HANDLE, "capabilities length output");
        W(UN(NtGdiDDCCIGetCapabilitiesString)(h, q, 16), h, XO_ST_DDC_HANDLE, "capabilities string buffer");
    }
}
#define GEN(k, kn) \
    XT(ptr_##kn##_inputs_rejected_before_handle) { NEEDS(); group_first(kinds[k]); } \
    XT(ptr_##kn##_outputs_checked_after_opm_handle) { NEEDS(); group_opm_late(kinds[k]); } \
    XT(ptr_##kn##_outputs_checked_after_monitor_handle) { NEEDS(); group_ddc(kinds[k]); }
GEN(0, null) GEN(1, odd) GEN(2, kernel) GEN(3, above_user)

/* A usable pointer is not enough: the kernel never reaches a successful path with a bogus handle, so the output
 * blocks stay untouched for every handle class. */
XT(ptr_valid_buffers_stay_untouched_for_bogus_handles) {
    NEEDS();
    for (int i = 0; i < 3; i++) { HANDLE h = (HANDLE)hs[i];
        memset(xo_a, 0, 0x1000); memset(xo_b, 0xB7, 0x1000);
        UN(NtGdiGetOPMInformation)(h, xo_a, xo_b); UN(NtGdiGetCOPPCompatibleOPMInformation)(h, xo_a, xo_b);
        UN(NtGdiConfigureOPMProtectedOutput)(h, xo_a, 0x20, xo_b); UN(NtGdiGetOPMRandomNumber)(h, xo_b);
        UN(NtGdiGetCertificateByHandle)(h, 0, xo_b, 0x800); UN(NtGdiDDCCIGetTimingReport)(h, xo_b); UN(NtGdiDDCCIGetCapabilitiesString)(h, xo_b, 0x800);
        CHECKF(xo_untouched(xo_b, 0x1000, 0xB7), "handle %p", (void *)h);
    }
}

/* ---- buffer sizes: a buffer one byte short of what the kernel reads runs into a guard page and is answered with the
 * pointer status; a buffer of exactly the right size gets as far as the handle lookup. The sizes are fixed by the
 * structure the call copies in: 0x100 bytes of signing key and sequence numbers, a 0x1000-byte parameter block for
 * Configure and for the COPP information request, 0x1010 bytes for the OPM information request, and for Configure the
 * output block is as long as the size parameter. ---- */
static unsigned char *guard_base;
static unsigned char *guard_at(unsigned k) {          /* k readable and writable bytes immediately before an inaccessible page */
    if (!guard_base) {
        guard_base = VirtualAlloc(NULL, 0x20000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        DWORD o; if (guard_base) VirtualProtect(guard_base + 0x10000, 0x10000, PAGE_NOACCESS, &o);
        if (guard_base) memset(guard_base, 0, 0x10000);
    }
    return guard_base ? guard_base + 0x10000 - k : NULL;
}
#define GUARD(fn_expr, size, name) \
    XT(size_##name##_exact_reaches_handle_lookup) { NEEDS(); for (int i = 0; i < 3; i++) { HANDLE h = (HANDLE)hs[i]; unsigned char *p = guard_at(size); if (!p) { t_skip("no guard page"); return; } \
        memset(xo_b, 0xB7, 0x1000); long s = (fn_expr); CHECKF(ST(s) == XO_ST_OPM_HANDLE, "handle %p: 0x%lx", (void *)h, ST(s)); } } \
    XT(size_##name##_one_byte_short_is_pointer_status) { NEEDS(); for (int i = 0; i < 3; i++) { HANDLE h = (HANDLE)hs[i]; unsigned char *p = guard_at((size) - 1); if (!p) { t_skip("no guard page"); return; } \
        memset(xo_b, 0xB7, 0x1000); long s = (fn_expr); CHECKF(ST(s) == XO_ST_PARAM_PTR, "handle %p: 0x%lx", (void *)h, ST(s)); } }
GUARD(UN(NtGdiSetOPMSigningKeyAndSequenceNumbers)(h, p), 0x100, setkey_block)
GUARD(UN(NtGdiConfigureOPMProtectedOutput)(h, p, 0x20, xo_b), 0x1000, configure_params)
GUARD(UN(NtGdiGetOPMInformation)(h, p, xo_b), 0x1010, info_request)
GUARD(UN(NtGdiGetCOPPCompatibleOPMInformation)(h, p, xo_b), 0x1000, copp_request)
GUARD(UN(NtGdiConfigureOPMProtectedOutput)(h, xo_a, 0x20, p), 0x20, configure_output_20)
GUARD(UN(NtGdiConfigureOPMProtectedOutput)(h, xo_a, 0x100, p), 0x100, configure_output_100)
