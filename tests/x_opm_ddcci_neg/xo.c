#include "xo.h"
jmp_buf xo_jb; volatile int xo_armed; volatile unsigned long xo_exc; static int veh_on;
LONG CALLBACK xo_veh(PEXCEPTION_POINTERS ep) {
    DWORD c = ep->ExceptionRecord->ExceptionCode;
    if (xo_armed && ((c & 0xC0000000) == 0xC0000000 || c == 0x80000002 || c == 0x80000100) && c != 0xC0000194 && c != 0xC000013A) {
        xo_armed = 0; xo_exc = c; longjmp(xo_jb, 1);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
void xo_run_guarded(void (*f)(void)) {
    /* a win32k service issued before user32 is loaded ends the process (the thread cannot be converted to a GUI thread without the callback table) */
    t_module("user32.dll"); t_module("gdi32.dll");
    if (!veh_on) { AddVectoredExceptionHandler(1, xo_veh); veh_on = 1; }
    if (!setjmp(xo_jb)) { xo_armed = 1; f(); xo_armed = 0; }
    else if (t_is_wine()) t_skip("emulator fault 0x%lx", xo_exc);
    else t_check(0, __FILE__, __LINE__, "no exception", "exception 0x%lx escaped a syscall", xo_exc);
}
const ULONG_PTR xo_bad[XO_NH] = { 0, 1, 0x1234, (ULONG_PTR)-1, 0xDEADBEEF, 0x40000000, 0x100000000ULL, 0x7FFFFFFFFFFFFFFFULL, 0x80000000 };
const char *const xo_bad_name[XO_NH] = { "0", "1", "1234", "minus1", "deadbeef", "40000000", "100000000", "7fff...", "80000000" };

unsigned char xo_a[0x2000], xo_b[0x2000];
int xo_untouched(const void *p, size_t n, unsigned char v) { const unsigned char *c = p; for (size_t i = 0; i < n; i++) if (c[i] != v) return 0; return 1; }

int xo_primary_device(WCHAR out[32]) {
    typedef BOOL (WINAPI *EDD)(LPCWSTR, DWORD, PDISPLAY_DEVICEW, DWORD);
    EDD f = (EDD)t_proc("EnumDisplayDevicesW"); WCHAR first[32] = { 0 };
    if (!f) return 0;
    for (DWORD i = 0; i < 16; i++) {
        DISPLAY_DEVICEW dd; memset(&dd, 0, sizeof dd); dd.cb = sizeof dd;
        if (!f(NULL, i, &dd, 0)) break;
        if (!(dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)) continue;
        if (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) { wcsncpy(out, dd.DeviceName, 31); out[31] = 0; return 1; }
        if (!first[0]) { wcsncpy(first, dd.DeviceName, 31); first[31] = 0; }
    }
    if (!first[0]) return 0;
    wcscpy(out, first); return 1;
}
int xo_opm_unsupported(void) {
    static int cached = -1;
    if (cached >= 0) return cached;
    cached = 0;
    WCHAR n[32]; UNICODE_STRING u; ULONG sz = 0;
    if (!UN(NtGdiGetCertificateSize) || !xo_primary_device(n)) return 0;
    xo_us(&u, n);
    cached = ST(UN(NtGdiGetCertificateSize)(&u, 0, &sz)) == XO_ST_NOT_SUPPORTED;
    return cached;
}
int xo_get_monitor(HANDLE *h) {
    WCHAR n[32]; UNICODE_STRING u; HANDLE arr[8] = { 0 }; ULONG got = 0;
    if (!UN(NtGdiGetPhysicalMonitors) || !UN(NtGdiDestroyPhysicalMonitor) || !xo_primary_device(n)) return 0;
    xo_us(&u, n);
    if (UN(NtGdiGetPhysicalMonitors)(&u, 8, &got, arr) < 0 || got == 0 || got > 8) return 0;
    for (ULONG i = 1; i < got; i++) UN(NtGdiDestroyPhysicalMonitor)(arr[i]);
    *h = arr[0]; return 1;
}
void xo_put_monitor(HANDLE h) { UN(NtGdiDestroyPhysicalMonitor)(h); }
int xo_ddc_unsupported(HANDLE live) {
    ULONG len = 0;
    return ST(UN(NtGdiDDCCIGetCapabilitiesStringLength)(live, &len)) == XO_ST_NOT_SUPPORTED;
}
