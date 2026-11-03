/* The dxva2 user-mode monitor-control functions on handles that name no monitor. dxva2 forwards to the syscalls above
 * and reports the syscall's NTSTATUS as the thread's last error, so a rejected handle leaves the monitor-handle status
 * value in GetLastError; an invalid HMONITOR is ERROR_INVALID_MONITOR_HANDLE (1461). No setting is written: the handle
 * is never live. */
#include "xo.h"

static HMODULE dx;
typedef BOOL (WINAPI *f_h)(HANDLE);
typedef BOOL (WINAPI *f_h1)(HANDLE, DWORD *);
typedef BOOL (WINAPI *f_h3)(HANDLE, DWORD *, DWORD *, DWORD *);
typedef BOOL (WINAPI *f_hbuf)(HANDLE, void *, DWORD);
typedef BOOL (WINAPI *f_hdd)(HANDLE, DWORD, DWORD);
static void *dxp(const char *n) { if (!dx) dx = LoadLibraryA("dxva2.dll"); return dx ? (void *)GetProcAddress(dx, n) : NULL; }
#define NEEDDX(var, type, name) type var = (type)dxp(name); if (t_is_wine()) { t_skip("emulator"); return; } if (!var) { t_skip("dxva2!" name " not available"); return; }
static const ULONG_PTR hs[] = { 0x1234, 0, 1, (ULONG_PTR)-1, 0xDEADBEEF };
#define NH 5
#define ERRCHK(c, want, what) do { SetLastError(0x77); BOOL r_ = (c); DWORD e_ = GetLastError(); \
    CHECKF(!r_, "%s handle %p: returned TRUE", what, (void *)hs[i]); CHECKF(e_ == (DWORD)(want), "%s handle %p: error %lu want %lu", what, (void *)hs[i], e_, (DWORD)(want)); } while (0)
/* the last error dxva2 leaves for a rejected monitor handle: the monitor-handle status mapped into the Win32 error space */
#define DDC_ERR 0xC026258CUL

XT(dxva_get_monitor_brightness) { NEEDDX(f, f_h3, "GetMonitorBrightness"); DWORD a = 1, b = 2, c = 3; for (int i = 0; i < NH; i++) { ERRCHK(f((HANDLE)hs[i], &a, &b, &c), DDC_ERR, "brightness"); CHECK(a == 1 && b == 2 && c == 3); } }
XT(dxva_get_monitor_contrast)   { NEEDDX(f, f_h3, "GetMonitorContrast");   DWORD a = 1, b = 2, c = 3; for (int i = 0; i < NH; i++) { ERRCHK(f((HANDLE)hs[i], &a, &b, &c), DDC_ERR, "contrast"); CHECK(a == 1 && b == 2 && c == 3); } }
XT(dxva_save_current_settings)  { NEEDDX(f, f_h, "SaveCurrentMonitorSettings"); for (int i = 0; i < NH; i++) ERRCHK(f((HANDLE)hs[i]), DDC_ERR, "save"); }
XT(dxva_capabilities_string_length) { NEEDDX(f, f_h1, "GetCapabilitiesStringLength"); DWORD l = 0xA5A5A5A5; for (int i = 0; i < NH; i++) { ERRCHK(f((HANDLE)hs[i], &l), DDC_ERR, "length"); CHECK(l == 0xA5A5A5A5); } }
XT(dxva_get_monitor_capabilities)   { NEEDDX(f, f_h1, "GetMonitorCapabilities"); DWORD l = 0xA5A5A5A5; for (int i = 0; i < NH; i++) { ERRCHK(f((HANDLE)hs[i], &l), DDC_ERR, "capabilities"); } }
XT(dxva_capabilities_request)   { NEEDDX(f, f_hbuf, "CapabilitiesRequestAndCapabilitiesReply"); memset(xo_a, 0xB7, 64); for (int i = 0; i < NH; i++) { ERRCHK(f((HANDLE)hs[i], xo_a, 16), DDC_ERR, "request"); } CHECK(xo_untouched(xo_a, 64, 0xB7)); }
XT(dxva_capabilities_request_zero_length_is_invalid_parameter) { NEEDDX(f, f_hbuf, "CapabilitiesRequestAndCapabilitiesReply"); for (int i = 0; i < NH; i++) ERRCHK(f((HANDLE)hs[i], xo_a, 0), ERROR_INVALID_PARAMETER, "request, length 0"); }
XT(dxva_set_vcp_feature)        { NEEDDX(f, f_hdd, "SetVCPFeature"); for (int i = 0; i < NH; i++) ERRCHK(f((HANDLE)hs[i], 0x10, 50), DDC_ERR, "set VCP"); }
XT(dxva_destroy_physical_monitor) { NEEDDX(f, f_h, "DestroyPhysicalMonitor"); for (int i = 0; i < NH; i++) ERRCHK(f((HANDLE)hs[i]), DDC_ERR, "destroy"); }
XT(dxva_invalid_hmonitor) {
    NEEDDX(n, f_h1, "GetNumberOfPhysicalMonitorsFromHMONITOR"); NEEDDX(g, f_hbuf, "GetPhysicalMonitorsFromHMONITOR");
    DWORD c = 0xA5A5A5A5;
    for (int i = 0; i < NH; i++) {
        SetLastError(0x77); CHECKF(!n((HANDLE)hs[i], &c), "count, hmonitor %p", (void *)hs[i]); EXPECT_ERR(ERROR_INVALID_MONITOR_HANDLE);
        SetLastError(0x77); CHECKF(!g((HANDLE)hs[i], xo_a, 0), "array, hmonitor %p", (void *)hs[i]); EXPECT_ERR(ERROR_INVALID_MONITOR_HANDLE);
    }
    CHECK(c == 0xA5A5A5A5);
}
XT(dxva_invalid_hmonitor_wins_over_null_count) {
    NEEDDX(n, f_h1, "GetNumberOfPhysicalMonitorsFromHMONITOR");
    SetLastError(0x77); CHECK(!n((HANDLE)0x1234, NULL)); EXPECT_ERR(ERROR_INVALID_MONITOR_HANDLE);
}
XT(dxva_destroy_zero_monitors_succeeds) {
    typedef BOOL (WINAPI *f_dm)(DWORD, void *);
    NEEDDX(f, f_dm, "DestroyPhysicalMonitors");
    SetLastError(0x77); CHECK(f(0, xo_a)); EXPECT_ERR(0x77);           /* nothing to destroy, no error stored */
}
XT(dxva_failures_are_repeatable) {
    NEEDDX(f, f_h, "SaveCurrentMonitorSettings");
    for (int k = 0; k < 4; k++) { SetLastError(0); CHECK(!f((HANDLE)0x1234)); EXPECT_ERR(DDC_ERR); }
}
