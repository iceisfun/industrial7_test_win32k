/* Shared fixture for the r2_display_opm_gamma_dpi area: gamma ramp, physical monitor, OPM and per-window DPI syscalls.
 *
 * Signatures below are derived from the 19041 win32kfull/win32kbase analysis of the Nt* handlers and from the
 * dxva2 KMFW* wrappers that call them (dxva2!KMFWGetNumberOfPhysicalMonitors passes (UNICODE_STRING*, ULONG*), etc.).
 * NTSTATUS-returning handlers are declared LONG; the kernel returns the status in eax.
 */
#ifndef R2_H
#define R2_H
#include "t.h"
#include "k.h"
#include <setjmp.h>
#include <wchar.h>

/* ---- exception guard ----------------------------------------------------------------------------------------
 * Emulators abort on an unimplemented win32k syscall (EXCEPTION_WINE_STUB 0x80000100) or fault on layouts they do not
 * model. Under Wine that becomes a SKIP. On Windows a hardware/software exception out of a syscall is a real finding:
 * it is recorded as a failed check and the test ends, but the run continues. */
extern jmp_buf r2_jb;
extern volatile int r2_armed;
extern volatile unsigned long r2_exc;
extern int r2_veh_installed;
extern int r2_escapes;   /* longjmp-ed exceptions so far; Wine's stub-abort path faults after about eight of them in one process */
LONG CALLBACK r2_veh(PEXCEPTION_POINTERS ep);
#ifdef R2_DEFINE_GUARD
jmp_buf r2_jb; volatile int r2_armed; volatile unsigned long r2_exc; int r2_veh_installed; int r2_escapes;
LONG CALLBACK r2_veh(PEXCEPTION_POINTERS ep) {
    DWORD c = ep->ExceptionRecord->ExceptionCode;
    if (r2_armed && ((c & 0xC0000000) == 0xC0000000 || c == 0x80000002 || c == 0x80000100) && c != 0xC0000194 && c != 0xC000013A) {
        r2_armed = 0; r2_exc = c; longjmp(r2_jb, 1);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif
static inline void r2_run_guarded(void (*f)(void)) {
    if (!r2_veh_installed) { AddVectoredExceptionHandler(1, r2_veh); r2_veh_installed = 1; }
    if (!setjmp(r2_jb)) { r2_armed = 1; f(); r2_armed = 0; }
    else if (r2_escapes++, t_is_wine()) t_skip("emulator fault/stub 0x%lx (unimplemented or differently laid out win32k function)", r2_exc);
    else CHECKF(0, "unexpected exception 0x%lx escaped a syscall", r2_exc);
}
#define T2(n) \
    static void body_##n(void); \
    static void test_##n(void) { r2_run_guarded(body_##n); } \
    static void __attribute__((constructor)) reg_##n(void) { t_register("r2_display_opm_gamma_dpi_" #n, test_##n); } \
    static void body_##n(void)

/* ---- syscall signatures (win32u.dll) ---- */
UDECL(int,   NtGdiGetDeviceGammaRamp,        (HDC, void *));
UDECL(int,   NtGdiSetDeviceGammaRamp,        (HDC, void *));
UDECL(int,   NtGdiGetAppliedDeviceGammaRamp, (HDC, void *));
UDECL(int,   NtGdiGetGammaRampCapability,    (HDC));
UDECL(int,   NtGdiSetPrivateDeviceGammaRamp, (HDC, void *, int));
UDECL(LONG,  NtGdiGetNumberOfPhysicalMonitors,(UNICODE_STRING *, ULONG *));
UDECL(LONG,  NtGdiGetPhysicalMonitors,       (UNICODE_STRING *, ULONG, ULONG *, HANDLE *));
UDECL(LONG,  NtGdiGetPhysicalMonitorDescription,(HANDLE, ULONG, WCHAR *));
UDECL(LONG,  NtGdiDestroyPhysicalMonitor,    (HANDLE));
UDECL(LONG,  NtGdiDDCCISetVCPFeature,        (HANDLE, ULONG, ULONG));
UDECL(ULONG, NtGdiGetBitmapDpiScaleValue,    (HBITMAP));
UDECL(LONG,  NtGdiGetSuggestedOPMProtectedOutputArraySize,(UNICODE_STRING *, ULONG *));
UDECL(LONG,  NtGdiCreateOPMProtectedOutputs, (UNICODE_STRING *, ULONG, ULONG, ULONG *, HANDLE *));
UDECL(LONG,  NtGdiCreateOPMProtectedOutput,  (void *, HANDLE *));
UDECL(LONG,  NtGdiDestroyOPMProtectedOutput, (HANDLE));
UDECL(LONG,  NtGdiGetCertificate,            (UNICODE_STRING *, ULONG, void *, ULONG));
UDECL(LONG,  NtGdiGetCertificateSize,        (UNICODE_STRING *, ULONG, ULONG *));
UDECL(LONG,  NtGdiGetCertificateByHandle,    (HANDLE, ULONG, void *, ULONG));
UDECL(LONG,  NtGdiGetCertificateSizeByHandle,(HANDLE, ULONG, ULONG *));
UDECL(LONG,  NtGdiGetOPMRandomNumber,        (HANDLE, void *));
UDECL(LONG,  NtGdiSetOPMSigningKeyAndSequenceNumbers,(HANDLE, void *));
UDECL(LONG,  NtGdiConfigureOPMProtectedOutput,(HANDLE, void *, ULONG, void *));
UDECL(LONG,  NtGdiGetOPMInformation,         (HANDLE, void *, void *));
UDECL(LONG,  NtGdiGetCOPPCompatibleOPMInformation,(HANDLE, void *, void *));
UDECL(BOOL,  NtUserLogicalToPhysicalDpiPointForWindow,(HWND, POINT *));
UDECL(BOOL,  NtUserPhysicalToLogicalDpiPointForWindow,(HWND, POINT *));
UDECL(BOOL,  NtUserDeferWindowDpiChanges,    (HWND));
UDECL(BOOL,  NtUserRestoreWindowDpiChanges,  (HWND));
UDECL(BOOL,  NtUserEnableWindowGDIScaledDpiMessage,(HWND, BOOL));
UDECL(BOOL,  NtUserIsWindowGDIScaledDpiMessageEnabled,(HWND));
UDECL(BOOL,  NtUserSetDialogControlDpiChangeBehavior,(HWND, USHORT, USHORT));
UDECL(BOOL,  NtUserSetProcessDpiAwarenessContext,(ULONG, int));
UDECL(ULONG, NtUserGetProcessDpiAwarenessContext,(HANDLE));
UDECL(BOOL,  NtUserGetOwnerTransformedMonitorRect,(HWND, HMONITOR, int, RECT *));
UDECL(BOOL,  NtUserGetPhysicalDeviceRect,    (HANDLE, RECT *));
UDECL(BOOL,  NtUserGetDisplayAutoRotationPreferencesByProcessId,(DWORD, DWORD *, DWORD *));
UDECL(BOOL,  NtUserSetDisplayMapping,        (HANDLE, HMONITOR));
UDECL(BOOL,  NtUserPaintMonitor,             (HMONITOR, HDC, RECT *));

/* An unimplemented win32u export under Wine is a generated "sub rsp,28h; lea rcx,[rip+x]" stub that aborts the process when called
 * (raising and escaping from it repeatedly corrupts Wine's exception state), so it is recognised by its bytes and skipped unseen. */
static inline int r2_is_wine_stub(FARPROC p) {
    static const unsigned char sig[7] = { 0x48, 0x83, 0xEC, 0x28, 0x48, 0x8D, 0x0D };
    return p && t_is_wine() && !memcmp((const void *)p, sig, sizeof sig);
}
#define NEED_NT(n) do { if (!UN(n)) { t_skip("missing export: " #n); return; } if (r2_is_wine_stub((FARPROC)UN(n))) { t_skip("Wine: " #n " is an unimplemented stub"); return; } } while (0)
static inline int r2_gui(void) { return U(GetDC) != NULL && U(CreateWindowExA) != NULL; }
#define NEED_GUI() do { if (!r2_gui()) { t_skip("user32 unavailable"); return; } } while (0)

static inline void r2_us(UNICODE_STRING *u, const WCHAR *s) {
    u->Length = (USHORT)(wcslen(s) * 2); u->MaximumLength = (USHORT)(u->Length + 2); u->Buffer = (PWSTR)s;
}
#define R2_NTST(x) ((unsigned long)(x))
/* STATUS_GRAPHICS_* values produced by DrvGetDeviceFromNameAndValidateDevice / DrvGetNumberOfPhysicalMonitors /
 * DrvCreatePhysicalMonitorObjects in win32kbase (decimal immediates converted: -0x3fe1fa1f etc.). */
#define R2_ST_NO_DEVICE        0xC01E05E1UL  /* -0x3fe1fa1f: device name not found */
#define R2_ST_DEV_NOT_ATTACHED 0xC01E05E2UL  /* -0x3fe1fa1e */
#define R2_ST_DEV_MIRROR       0xC01E05E3UL  /* -0x3fe1fa1d */
#define R2_ST_NO_MONITORS      0xC01E05E5UL  /* -0x3fe1fa1b: DrvGetNumberOfPhysicalMonitors with zero active monitors */
#define R2_ST_ARRAY_TOO_SMALL  0xC01E05E6UL  /* -0x3fe1fa1a: DrvCreatePhysicalMonitorObjects, caller array smaller than count */
#define R2_ST_INVALID_PARAM    0xC000000DUL

/* Display device names currently known to user32 (attached or not). */
struct r2_dev { WCHAR name[32]; DWORD flags; };
static inline int r2_enum_devices(struct r2_dev *out, int cap) {
    typedef BOOL (WINAPI *ED)(LPCWSTR, DWORD, PDISPLAY_DEVICEW, DWORD);
    ED f = (ED)t_proc("EnumDisplayDevicesW"); int n = 0;
    if (!f) return 0;
    for (DWORD i = 0; n < cap && i < 32; i++) {
        DISPLAY_DEVICEW dd; memset(&dd, 0, sizeof dd); dd.cb = sizeof dd;
        if (!f(NULL, i, &dd, 0)) break;
        wcsncpy(out[n].name, dd.DeviceName, 31); out[n].name[31] = 0; out[n].flags = dd.StateFlags; n++;
    }
    return n;
}
static inline HDC r2_screen_dc(void) { return U(GetDC) ? U(GetDC)(NULL) : NULL; }
static inline void r2_release_dc(HDC dc) { if (dc && U(ReleaseDC)) U(ReleaseDC)(NULL, dc); }
#endif
