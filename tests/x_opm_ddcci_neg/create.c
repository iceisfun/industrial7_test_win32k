/* NtGdiCreateOPMProtectedOutput(params, &handle). params is a 16-byte block (adapter LUID, video-present target, semantics).
 * No adapter carries the LUIDs used here, so the call fails with an invalid-parameter status and stores the invalid
 * handle value (all ones) into the output; a NULL params or output pointer is answered with the pointer status and
 * stores nothing. No protected output is ever created by these tests. */
#include "xo.h"

struct cp { LONG lo, hi; ULONG target, sem; };
static const struct cp cases[] = {
    { 0, 0, 0, 0 }, { 0, 0, 0, 1 }, { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0x1234, 0x5678, 0, 0 }, { 0, 0, 1, 0 },
    { 0x7FFFFFF0, 0x7FFFFFF0, 1, 1 }, { 0, 0, 0xFFFFFFFF, 0xFFFFFFFF }, { -1, -1, -1, -1 },
};
#define NC (sizeof cases / sizeof *cases)
#define NEEDS() NEED(NtGdiCreateOPMProtectedOutput); NEED(NtGdiDestroyOPMProtectedOutput)

static void one(unsigned i) {
    struct cp in = cases[i]; HANDLE out = (HANDLE)0x5A5A5A5A;
    long s = UN(NtGdiCreateOPMProtectedOutput)(&in, &out);
    CHECKF(ST(s) == XO_ST_INVALID_PARAM, "case %u: status 0x%lx", i, ST(s));
    CHECKF(out == (HANDLE)(ULONG_PTR)-1, "case %u: output handle %p", i, out);
    CHECKF(in.lo == cases[i].lo && in.hi == cases[i].hi && in.target == cases[i].target && in.sem == cases[i].sem, "case %u: input modified", i);
    if (s >= 0) UN(NtGdiDestroyOPMProtectedOutput)(out);
}
#define GEN(i) XT(create_unknown_adapter_case_##i) { NEEDS(); one(i); }
GEN(0) GEN(1) GEN(2) GEN(3) GEN(4) GEN(5) GEN(6) GEN(7) GEN(8)

XT(create_null_params_is_param_ptr_status) {
    NEEDS(); HANDLE out = (HANDLE)0x5A5A5A5A;
    CHECK(ST(UN(NtGdiCreateOPMProtectedOutput)(NULL, &out)) == XO_ST_PARAM_PTR);
    CHECK(out == (HANDLE)0x5A5A5A5A);
}
XT(create_null_output_is_param_ptr_status) {
    NEEDS(); struct cp in = cases[0];
    CHECK(ST(UN(NtGdiCreateOPMProtectedOutput)(&in, NULL)) == XO_ST_PARAM_PTR);
}
XT(create_both_null_is_param_ptr_status) { NEEDS(); CHECK(ST(UN(NtGdiCreateOPMProtectedOutput)(NULL, NULL)) == XO_ST_PARAM_PTR); }
XT(create_kernel_pointers) {
    NEEDS(); struct cp in = cases[0]; HANDLE out = (HANDLE)0x5A5A5A5A;
    unsigned long a = ST(UN(NtGdiCreateOPMProtectedOutput)(KERNEL_PTR, &out));
    unsigned long b = ST(UN(NtGdiCreateOPMProtectedOutput)(&in, (HANDLE *)KERNEL_PTR));
    CHECKF(a == XO_ST_PARAM_PTR, "kernel params: 0x%lx", a);
    CHECKF(b == XO_ST_PARAM_PTR, "kernel output: 0x%lx", b);
    CHECK(out == (HANDLE)0x5A5A5A5A);
}
XT(create_failure_leaves_no_destroyable_handle) {
    NEEDS(); struct cp in = cases[4]; HANDLE out = NULL;
    UN(NtGdiCreateOPMProtectedOutput)(&in, &out);
    CHECK(ST(UN(NtGdiDestroyOPMProtectedOutput)(out)) == XO_ST_OPM_HANDLE);      /* the stored all-ones value names nothing */
    CHECK(ST(UN(NtGdiDestroyOPMProtectedOutput)((HANDLE)0)) == XO_ST_OPM_HANDLE);
}
XT(create_status_is_repeatable) {
    NEEDS();
    for (int k = 0; k < 4; k++) one(4);
}
