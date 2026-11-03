/* Shared fixture for the x_opm_ddcci_neg area: rejection matrices for the Output Protection Manager, DDC/CI
 * and gamma-capability syscalls. Every expectation is what Windows 10 22H2 returns for handles, sizes and
 * names that do not denote a live protected output or physical monitor. Nothing here creates a protected
 * output or writes a monitor setting: with no certified output (a VM) the positive paths cannot be reached,
 * and a bogus handle never reaches the code that talks to a monitor. */
#ifndef XO_H
#define XO_H
#include "t.h"
#include "k.h"
#include <setjmp.h>
#include <wchar.h>

UDECL(LONG, NtGdiCreateOPMProtectedOutput,  (void *, HANDLE *));
UDECL(LONG, NtGdiCreateOPMProtectedOutputs, (UNICODE_STRING *, ULONG, ULONG, ULONG *, HANDLE *));
UDECL(LONG, NtGdiGetSuggestedOPMProtectedOutputArraySize, (UNICODE_STRING *, ULONG *));
UDECL(LONG, NtGdiDestroyOPMProtectedOutput, (HANDLE));
UDECL(LONG, NtGdiGetCertificate,            (UNICODE_STRING *, ULONG, void *, ULONG));
UDECL(LONG, NtGdiGetCertificateSize,        (UNICODE_STRING *, ULONG, ULONG *));
UDECL(LONG, NtGdiGetCertificateByHandle,    (HANDLE, ULONG, void *, ULONG));
UDECL(LONG, NtGdiGetCertificateSizeByHandle,(HANDLE, ULONG, ULONG *));
UDECL(LONG, NtGdiGetOPMRandomNumber,        (HANDLE, void *));
UDECL(LONG, NtGdiSetOPMSigningKeyAndSequenceNumbers,(HANDLE, void *));
UDECL(LONG, NtGdiConfigureOPMProtectedOutput,(HANDLE, void *, ULONG, void *));
UDECL(LONG, NtGdiGetOPMInformation,         (HANDLE, void *, void *));
UDECL(LONG, NtGdiGetCOPPCompatibleOPMInformation,(HANDLE, void *, void *));
UDECL(LONG, NtGdiDDCCIGetVCPFeature,        (HANDLE, ULONG, ULONG *, ULONG *, ULONG *));
UDECL(LONG, NtGdiDDCCISetVCPFeature,        (HANDLE, ULONG, ULONG));
UDECL(LONG, NtGdiDDCCIGetTimingReport,      (HANDLE, void *));
UDECL(LONG, NtGdiDDCCIGetCapabilitiesStringLength,(HANDLE, ULONG *));
UDECL(LONG, NtGdiDDCCIGetCapabilitiesString,(HANDLE, void *, ULONG));
UDECL(LONG, NtGdiDDCCISaveCurrentSettings,  (HANDLE));
UDECL(LONG, NtGdiGetPhysicalMonitors, (UNICODE_STRING *, ULONG, ULONG *, HANDLE *));
UDECL(LONG, NtGdiDestroyPhysicalMonitor, (HANDLE));
UDECL(LONG, NtGdiGetPhysicalMonitorDescription, (HANDLE, ULONG, WCHAR *));
UDECL(int,  NtGdiGetGammaRampCapability,    (HDC));

/* Exceptions out of a syscall are findings on Windows (recorded, test ends); under an emulator they are a skip. */
extern jmp_buf xo_jb;
extern volatile int xo_armed;
extern volatile unsigned long xo_exc;
LONG CALLBACK xo_veh(PEXCEPTION_POINTERS ep);
void xo_run_guarded(void (*f)(void));
#define XT(n) \
    static void body_##n(void); \
    static void test_##n(void) { xo_run_guarded(body_##n); } \
    static void __attribute__((constructor)) reg_##n(void) { t_register("x_opm_ddcci_neg_" #n, test_##n); } \
    static void body_##n(void)
#define NEED(n) do { if (!UN(n)) { t_skip("missing export: " #n); return; } if (t_is_wine()) { t_skip("emulator: " #n " is not a win32k service"); return; } } while (0)
#define ST(x) ((unsigned long)(LONG)(x))

static inline void xo_us(UNICODE_STRING *u, const WCHAR *s) { u->Length = (USHORT)(wcslen(s) * 2); u->MaximumLength = (USHORT)(u->Length + 2); u->Buffer = (PWSTR)s; }

/* NTSTATUS values observed on Windows 10 22H2. */
#define XO_ST_NOT_SUPPORTED 0xC00000BBUL   /* no OPM-capable output / no DDC-capable monitor behind the object */
#define XO_ST_INVALID_PARAM 0xC000000DUL
#define XO_ST_OPM_HANDLE    0xC01E050CUL   /* not a protected-output handle */
#define XO_ST_DDC_HANDLE    0xC01E058CUL   /* not a physical-monitor handle */
#define XO_ST_NO_DEVICE     0xC01E05E1UL   /* no graphics device carries this name */
#define XO_ST_PARAM_PTR     0xC01E05E4UL   /* a required pointer parameter is NULL or unreadable/unwritable */
#define XO_ST_ARRAY_SMALL   0xC01E05E6UL   /* caller array smaller than the monitor count */

/* Handle values that cannot name a live object. */
#define XO_NH 9
extern const ULONG_PTR xo_bad[XO_NH];
extern const char *const xo_bad_name[XO_NH];
/* Fixture helpers (xo.c). */
int  xo_primary_device(WCHAR out[32]);              /* name of the primary display device, 0 if none */
int  xo_opm_unsupported(void);                      /* 1 when OPM calls on the primary device answer "not supported" (no certified output) */
int  xo_get_monitor(HANDLE *h);                     /* one live physical-monitor handle of the primary device (caller destroys it), 0 on failure */
void xo_put_monitor(HANDLE h);
int  xo_ddc_unsupported(HANDLE live);               /* 1 when the monitor behind a live handle answers "not supported" to DDC/CI */
int  xo_untouched(const void *p, size_t n, unsigned char v);
extern unsigned char xo_a[0x2000], xo_b[0x2000];
#define KERNEL_PTR ((void *)0xFFFF800000001000ULL)
#endif
