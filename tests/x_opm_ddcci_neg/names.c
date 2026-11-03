/* Device-name parameters. NtGdiGetCertificate, GetCertificateSize, GetSuggestedOPMProtectedOutputArraySize and
 * CreateOPMProtectedOutputs resolve a display device name first. A name that no graphics device carries (empty, wrong
 * form, truncated, long, NULL pointer, NULL or unreadable buffer, a length that is odd or exceeds the maximum length)
 * is answered with one status before any other parameter is examined, and nothing is written. */
#include "xo.h"

static const WCHAR *const names[] = { L"", L"\\\\.\\DISPLAY99999", L"DISPLAY", L"\\\\.\\DISPLAYV1", L"x", L"\\\\.\\DISPLAY-1", L"\\\\.\\DISPLAY0" };
#define NN (sizeof names / sizeof *names)
static WCHAR longname[400];
#define NEEDS() NEED(NtGdiGetCertificate); NEED(NtGdiGetCertificateSize); NEED(NtGdiGetSuggestedOPMProtectedOutputArraySize); NEED(NtGdiCreateOPMProtectedOutputs)
#define W(c, what) CHECKF(ST(c) == XO_ST_NO_DEVICE, "%s: status 0x%lx", what, ST(c))

/* Each of these builds a malformed UNICODE_STRING over a bogus device name. */
enum { M_ODD_LEN, M_SMALL_MAX, M_NULL_BUF, M_BIG_LEN, M_COUNT };
static void malformed(UNICODE_STRING *u, int m) {
    xo_us(u, L"\\\\.\\DISPLAY99999");
    switch (m) {
    case M_ODD_LEN: u->Length = 1; break;
    case M_SMALL_MAX: u->MaximumLength = 2; break;
    case M_NULL_BUF: u->Length = 4; u->MaximumLength = 4; u->Buffer = NULL; break;
    case M_BIG_LEN: u->Length = 0xFFFE; u->MaximumLength = 0xFFFE; break;
    }
}

static void op_certsize(UNICODE_STRING *u, const char *w) {
    static const ULONG types[] = { 0, 1, 2, 3, 4, 7, 0xFFFFFFFF };
    for (unsigned t = 0; t < 7; t++) { ULONG sz = 0x5A5A5A5A; W(UN(NtGdiGetCertificateSize)(u, types[t], &sz), w); CHECK(sz == 0x5A5A5A5A); }
}
static void op_cert(UNICODE_STRING *u, const char *w) {
    static const ULONG sizes[] = { 0, 1, 0x1000, 0xFFFFFFFF };
    for (unsigned s = 0; s < 4; s++) { memset(xo_a, 0xB7, 0x1000); W(UN(NtGdiGetCertificate)(u, 0, xo_a, sizes[s]), w); CHECK(xo_untouched(xo_a, 0x1000, 0xB7)); }
}
static void op_suggest(UNICODE_STRING *u, const char *w) { ULONG c = 0x5A5A5A5A; W(UN(NtGdiGetSuggestedOPMProtectedOutputArraySize)(u, &c), w); CHECK(c == 0x5A5A5A5A); }
static void op_multi(UNICODE_STRING *u, const char *w) {
    static const ULONG cnt[] = { 0, 1, 2, 0xFFFFFFFF };
    for (unsigned c = 0; c < 4; c++) for (ULONG sem = 0; sem < 2; sem++) {
        HANDLE h[2] = { (HANDLE)0x77, (HANDLE)0x77 }; ULONG got = 0x5A5A5A5A;
        W(UN(NtGdiCreateOPMProtectedOutputs)(u, sem, cnt[c], &got, h), w);
        CHECK(got == 0x5A5A5A5A && h[0] == (HANDLE)0x77 && h[1] == (HANDLE)0x77);
    }
}
typedef void (*op_fn)(UNICODE_STRING *, const char *);
static void run_names(op_fn op) { for (unsigned i = 0; i < NN; i++) { UNICODE_STRING u; xo_us(&u, names[i]); op(&u, "name"); } }
static void run_malformed(op_fn op) { for (int m = 0; m < M_COUNT; m++) { UNICODE_STRING u; malformed(&u, m); op(&u, "malformed string"); } }
static void run_long(op_fn op) { wmemset(longname, L'a', 399); longname[399] = 0; UNICODE_STRING u; xo_us(&u, longname); op(&u, "long name"); }
static void run_null(op_fn op) { op(NULL, "NULL string"); }
#define GEN(op) \
    XT(name_##op##_unknown_names) { NEEDS(); run_names(op_##op); } \
    XT(name_##op##_malformed_strings) { NEEDS(); run_malformed(op_##op); } \
    XT(name_##op##_long_name) { NEEDS(); run_long(op_##op); } \
    XT(name_##op##_null_string_pointer) { NEEDS(); run_null(op_##op); }
GEN(certsize) GEN(cert) GEN(suggest) GEN(multi)

/* The name is resolved before the output pointers are looked at. */
XT(name_unknown_device_wins_over_null_outputs) {
    NEEDS();
    UNICODE_STRING u; xo_us(&u, L"\\\\.\\DISPLAY99999");
    HANDLE h[2];
    W(UN(NtGdiGetCertificateSize)(&u, 0, NULL), "certificate size, NULL size");
    W(UN(NtGdiGetCertificate)(&u, 0, NULL, 0x100), "certificate, NULL buffer");
    W(UN(NtGdiGetSuggestedOPMProtectedOutputArraySize)(&u, NULL), "suggested size, NULL count");
    W(UN(NtGdiCreateOPMProtectedOutputs)(&u, 0, 2, NULL, h), "create, NULL count");
    W(UN(NtGdiCreateOPMProtectedOutputs)(&u, 0, 2, (ULONG *)xo_a, NULL), "create, NULL array");
    W(UN(NtGdiGetCertificateSize)(&u, 0, (ULONG *)KERNEL_PTR), "certificate size, kernel size pointer");
    W(UN(NtGdiGetCertificate)(&u, 0, KERNEL_PTR, 0x100), "certificate, kernel buffer");
}
XT(name_unknown_device_status_is_repeatable) {
    NEEDS();
    UNICODE_STRING u; xo_us(&u, L"\\\\.\\DISPLAY99999");
    for (int i = 0; i < 5; i++) { ULONG sz = 1; W(UN(NtGdiGetCertificateSize)(&u, 0, &sz), "repeat"); }
}
