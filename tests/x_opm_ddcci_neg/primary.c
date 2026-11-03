/* The primary display device name against the OPM calls. Name resolution succeeds; what follows depends on the output.
 * Where the output has no certified protected-output support (a virtual display adapter) every certificate and
 * creation request is answered "not supported" after the checks that do not depend on the output, so those tests are
 * skipped on an OPM-capable output rather than asserted. Nothing here creates a protected output. */
#include "xo.h"

#define NEEDS() NEED(NtGdiGetCertificate); NEED(NtGdiGetCertificateSize); NEED(NtGdiGetSuggestedOPMProtectedOutputArraySize); NEED(NtGdiCreateOPMProtectedOutputs); \
    NEED(NtGdiDestroyOPMProtectedOutput)
#define PRIMARY(u) WCHAR pn_[32]; UNICODE_STRING u; if (!xo_primary_device(pn_)) { t_skip("no attached display device"); return; } xo_us(&u, pn_)
#define UNSUP() if (!xo_opm_unsupported()) { t_skip("the primary output is OPM-capable or answers differently; only the not-supported path is asserted"); return; }
#define NS(c, what) CHECKF(ST(c) == XO_ST_NOT_SUPPORTED, "%s: status 0x%lx", what, ST(c))

XT(primary_certificate_size_by_type) {
    NEEDS(); PRIMARY(u); UNSUP();
    static const ULONG types[] = { 0, 1, 2, 3, 4, 5, 6, 7, 0xFFFFFFFF };
    for (int i = 0; i < 9; i++) { ULONG sz = 0x5A5A5A5A; NS(UN(NtGdiGetCertificateSize)(&u, types[i], &sz), "size"); CHECKF(sz == 0x5A5A5A5A, "type %lx wrote the size", types[i]); }
}
XT(primary_certificate_by_buffer_size) {
    NEEDS(); PRIMARY(u); UNSUP();
    static const ULONG sizes[] = { 0, 1, 0x100, 0x1000, 0xFFFFFFFF };
    for (int i = 0; i < 5; i++) { memset(xo_a, 0xB7, 0x1000); NS(UN(NtGdiGetCertificate)(&u, 0, xo_a, sizes[i]), "certificate"); CHECK(xo_untouched(xo_a, 0x1000, 0xB7)); }
}
XT(primary_certificate_null_outputs_not_examined) {
    NEEDS(); PRIMARY(u); UNSUP();
    NS(UN(NtGdiGetCertificateSize)(&u, 0, NULL), "size, NULL pointer");
    NS(UN(NtGdiGetCertificate)(&u, 0, NULL, 0x100), "certificate, NULL buffer");
    NS(UN(NtGdiGetCertificateSize)(&u, 0, (ULONG *)KERNEL_PTR), "size, kernel pointer");
}
XT(primary_certificate_null_string_is_no_device) {
    NEEDS(); PRIMARY(u); (void)u;
    ULONG sz = 0;
    CHECK(ST(UN(NtGdiGetCertificate)(NULL, 0, xo_a, 1)) == XO_ST_NO_DEVICE);
    CHECK(ST(UN(NtGdiGetCertificateSize)(NULL, 0, &sz)) == XO_ST_NO_DEVICE);
}
XT(primary_suggested_array_size_is_at_least_one) {
    NEEDS(); PRIMARY(u);
    ULONG c = 0x5A5A5A5A; long s = UN(NtGdiGetSuggestedOPMProtectedOutputArraySize)(&u, &c);
    if (s < 0) { t_skip("the primary device reports no protected-output array size (status 0x%lx)", ST(s)); return; }
    CHECKF(c >= 1 && c < 64, "count %lu", c);
}
XT(primary_suggested_array_size_null_count_is_pointer_status) {
    NEEDS(); PRIMARY(u);
    ULONG c = 0; if (UN(NtGdiGetSuggestedOPMProtectedOutputArraySize)(&u, &c) < 0) { t_skip("the primary device reports no protected-output array size"); return; }
    CHECK(ST(UN(NtGdiGetSuggestedOPMProtectedOutputArraySize)(&u, NULL)) == XO_ST_PARAM_PTR);
    CHECK(ST(UN(NtGdiGetSuggestedOPMProtectedOutputArraySize)(&u, (ULONG *)KERNEL_PTR)) == XO_ST_PARAM_PTR);
}
XT(primary_suggested_array_size_is_stable) {
    NEEDS(); PRIMARY(u);
    ULONG a = 0, b = 0; long s1 = UN(NtGdiGetSuggestedOPMProtectedOutputArraySize)(&u, &a), s2 = UN(NtGdiGetSuggestedOPMProtectedOutputArraySize)(&u, &b);
    CHECK(s1 == s2); if (s1 >= 0) CHECK(a == b);
}
XT(primary_suggested_array_size_name_is_case_insensitive) {
    NEEDS(); PRIMARY(u);
    WCHAR lc[32]; wcscpy(lc, pn_); for (WCHAR *p = lc; *p; p++) if (*p >= L'A' && *p <= L'Z') *p += 32;
    UNICODE_STRING l; xo_us(&l, lc);
    ULONG a = 0, b = 0; long s1 = UN(NtGdiGetSuggestedOPMProtectedOutputArraySize)(&u, &a), s2 = UN(NtGdiGetSuggestedOPMProtectedOutputArraySize)(&l, &b);
    CHECKF(s1 == s2, "0x%lx vs 0x%lx", ST(s1), ST(s2)); if (s1 >= 0) CHECK(a == b);
}
XT(primary_create_outputs_zero_capacity_is_array_too_small) {
    NEEDS(); PRIMARY(u);
    ULONG c = 0; if (UN(NtGdiGetSuggestedOPMProtectedOutputArraySize)(&u, &c) < 0 || c == 0) { t_skip("the primary device reports no protected-output array size"); return; }
    static const ULONG sems[] = { 0, 1, 2, 0x10, 0xFFFFFFFF };
    for (int i = 0; i < 5; i++) {
        HANDLE h[2] = { (HANDLE)0x77, (HANDLE)0x77 }; ULONG got = 0x5A5A5A5A;
        long s = UN(NtGdiCreateOPMProtectedOutputs)(&u, sems[i], 0, &got, h);
        CHECKF(ST(s) == XO_ST_ARRAY_SMALL, "semantics %lx: 0x%lx", sems[i], ST(s));
        CHECK(got == 0x5A5A5A5A && h[0] == (HANDLE)0x77);
    }
}
XT(primary_create_outputs_not_supported) {
    NEEDS(); PRIMARY(u); UNSUP();
    static const ULONG sems[] = { 0, 1, 2, 0x10, 0xFFFFFFFF }; static const ULONG cnts[] = { 1, 2, 3, 0xFFFFFFFF };
    for (int i = 0; i < 5; i++) for (int k = 0; k < 4; k++) {
        HANDLE h[2] = { (HANDLE)0x77, (HANDLE)0x77 }; ULONG got = 0x5A5A5A5A;
        long s = UN(NtGdiCreateOPMProtectedOutputs)(&u, sems[i], cnts[k], &got, h);
        CHECKF(ST(s) == XO_ST_NOT_SUPPORTED, "semantics %lx count %lx: 0x%lx", sems[i], cnts[k], ST(s));
        CHECK(got == 0x5A5A5A5A && h[0] == (HANDLE)0x77 && h[1] == (HANDLE)0x77);
        if (s >= 0) { for (ULONG j = 0; j < got && j < 2; j++) UN(NtGdiDestroyOPMProtectedOutput)(h[j]); }
    }
}
XT(primary_create_outputs_null_outputs_not_examined) {
    NEEDS(); PRIMARY(u); UNSUP();
    HANDLE h[2]; ULONG got = 0;
    NS(UN(NtGdiCreateOPMProtectedOutputs)(&u, 0, 2, NULL, h), "NULL count");
    NS(UN(NtGdiCreateOPMProtectedOutputs)(&u, 0, 2, &got, NULL), "NULL array");
}
XT(primary_create_outputs_null_string_is_no_device) {
    NEEDS(); HANDLE h[2]; ULONG got = 0;
    CHECK(ST(UN(NtGdiCreateOPMProtectedOutputs)(NULL, 0, 2, &got, h)) == XO_ST_NO_DEVICE);
}
