/* Output Protection Manager syscalls (win32kbase NtGdiCreateOPMProtectedOutput(s), DestroyOPMProtectedOutput, GetCertificate*,
 * GetOPMRandomNumber, SetOPMSigningKeyAndSequenceNumbers, ConfigureOPMProtectedOutput, GetOPMInformation,
 * GetCOPPCompatibleOPMInformation, GetSuggestedOPMProtectedOutputArraySize).
 *
 * These are the calls dxva2 makes for IOPMVideoOutput. Handles are OPM protected-output objects created by
 * NtGdiCreateOPMProtectedOutput(s); no test here creates a live one (that needs a certified LDDM monitor), so every handle is
 * bogus and every call must fail with a negative NTSTATUS without writing its output buffer. Copy-out in every handler
 * (SafelyCopyKernelModeDataToUserModeData, memcpy after ProbeForWrite) runs only after the COPM method succeeded.
 * Struct sizes: configure/get-info parameter blocks are 0x1000 bytes (ExAllocatePoolWithTag(...,0x1000)), the encrypted
 * signing-key block is 0x100 bytes, a requested-information block is 0x1000 bytes. */
#include "r2.h"

static HANDLE bad_h[] = { NULL, (HANDLE)1, (HANDLE)0x1234, (HANDLE)(ULONG_PTR)-1, (HANDLE)(ULONG_PTR)0xDEADBEEF, (HANDLE)(ULONG_PTR)0x40000000 };
#define NBADH (sizeof bad_h / sizeof bad_h[0])
static const WCHAR *bad_dev[] = { L"\\\\.\\DISPLAY99999", L"r2_nonexistent_monitor" };
static unsigned char g_big[0x2000];

T2(opm_stubs_present_in_win32u) {
    static const char *names[] = { "NtGdiCreateOPMProtectedOutput", "NtGdiCreateOPMProtectedOutputs", "NtGdiDestroyOPMProtectedOutput",
        "NtGdiGetCertificate", "NtGdiGetCertificateSize", "NtGdiGetCertificateByHandle", "NtGdiGetCertificateSizeByHandle",
        "NtGdiGetOPMRandomNumber", "NtGdiSetOPMSigningKeyAndSequenceNumbers", "NtGdiConfigureOPMProtectedOutput",
        "NtGdiGetOPMInformation", "NtGdiGetCOPPCompatibleOPMInformation", "NtGdiGetSuggestedOPMProtectedOutputArraySize" };
    for (unsigned i = 0; i < sizeof names / sizeof *names; i++) {
        FARPROC p = t_proc_in("win32u.dll", names[i]);
        if (!p) { if (t_is_wine()) continue; CHECKF(0, "win32u!%s missing", names[i]); continue; }
        CHECKF(k_stub_valid(p) || t_is_wine(), "%s stub shape", names[i]);
    }
}
T2(opm_certificate_size_by_handle_bogus_handles_fail) {
    NEED_NT(NtGdiGetCertificateSizeByHandle);
    for (unsigned i = 0; i < NBADH; i++) for (ULONG type = 0; type < 4; type++) {
        ULONG sz = 0x5A5A5A5A;
        LONG st = UN(NtGdiGetCertificateSizeByHandle)(bad_h[i], type, &sz);
        CHECKF(st < 0, "handle %p type %lu: status 0x%lx", bad_h[i], type, R2_NTST(st));
        CHECKF(sz == 0x5A5A5A5A, "handle %p type %lu: size written on failure", bad_h[i], type);
    }
}
T2(opm_certificate_by_handle_bogus_handles_fail_buffer_untouched) {
    NEED_NT(NtGdiGetCertificateByHandle);
    for (unsigned i = 0; i < NBADH; i++) for (ULONG type = 0; type < 3; type++) for (ULONG sz = 0; sz <= 0x400; sz += 0x400) {
        memset(g_big, 0xB7, sizeof g_big);
        LONG st = UN(NtGdiGetCertificateByHandle)(bad_h[i], type, g_big, sz);
        CHECKF(st < 0, "handle %p type %lu size %lu: status 0x%lx", bad_h[i], type, sz, R2_NTST(st));
        CHECKF(g_big[0] == 0xB7 && g_big[0x3FF] == 0xB7, "handle %p: buffer written", bad_h[i]);
    }
}
T2(opm_certificate_size_unknown_device_fails) {
    NEED_NT(NtGdiGetCertificateSize);
    for (unsigned d = 0; d < 2; d++) for (ULONG type = 0; type < 3; type++) {
        UNICODE_STRING u; r2_us(&u, bad_dev[d]);
        ULONG sz = 0x5A5A5A5A;
        LONG st = UN(NtGdiGetCertificateSize)(&u, type, &sz);
        CHECKF(st < 0, "%ls type %lu: status 0x%lx", bad_dev[d], type, R2_NTST(st));
        CHECKF(sz == 0x5A5A5A5A, "%ls: size written on failure", bad_dev[d]);
    }
}
T2(opm_certificate_unknown_device_fails_buffer_untouched) {
    NEED_NT(NtGdiGetCertificate);
    for (unsigned d = 0; d < 2; d++) for (ULONG type = 0; type < 3; type++) {
        UNICODE_STRING u; r2_us(&u, bad_dev[d]);
        memset(g_big, 0xB7, sizeof g_big);
        LONG st = UN(NtGdiGetCertificate)(&u, type, g_big, 0x1000);
        CHECKF(st < 0, "%ls type %lu: status 0x%lx", bad_dev[d], type, R2_NTST(st));
        CHECKF(g_big[0] == 0xB7 && g_big[0xFFF] == 0xB7, "%ls: buffer written", bad_dev[d]);
    }
}
T2(opm_random_number_bogus_handles_fail) {
    NEED_NT(NtGdiGetOPMRandomNumber);
    for (unsigned i = 0; i < NBADH; i++) {
        memset(g_big, 0xB7, 64);
        LONG st = UN(NtGdiGetOPMRandomNumber)(bad_h[i], g_big);
        CHECKF(st < 0, "handle %p: status 0x%lx", bad_h[i], R2_NTST(st));
        CHECKF(g_big[0] == 0xB7 && g_big[15] == 0xB7, "handle %p: 16-byte random number written on failure", bad_h[i]);
    }
}
T2(opm_set_signing_key_bogus_handles_fail) {
    NEED_NT(NtGdiSetOPMSigningKeyAndSequenceNumbers);
    for (unsigned i = 0; i < NBADH; i++) {
        memset(g_big, 0, 0x100);                                                /* kernel copies 0x100 bytes from the caller */
        LONG st = UN(NtGdiSetOPMSigningKeyAndSequenceNumbers)(bad_h[i], g_big);
        CHECKF(st < 0, "handle %p: status 0x%lx", bad_h[i], R2_NTST(st));
    }
}
T2(opm_configure_bogus_handles_fail) {
    NEED_NT(NtGdiConfigureOPMProtectedOutput);
    for (unsigned i = 0; i < NBADH; i++) for (ULONG extra = 0; extra <= 0x20; extra += 0x20) {
        memset(g_big, 0, sizeof g_big);
        LONG st = UN(NtGdiConfigureOPMProtectedOutput)(bad_h[i], g_big, extra, g_big + 0x1000);
        CHECKF(st < 0, "handle %p extra %lu: status 0x%lx", bad_h[i], extra, R2_NTST(st));
    }
}
T2(opm_get_information_bogus_handles_fail_output_untouched) {
    NEED_NT(NtGdiGetOPMInformation);
    for (unsigned i = 0; i < NBADH; i++) {
        memset(g_big, 0, 0x1000);
        memset(g_big + 0x1000, 0xB7, 0x1000);
        LONG st = UN(NtGdiGetOPMInformation)(bad_h[i], g_big, g_big + 0x1000);
        CHECKF(st < 0, "handle %p: status 0x%lx", bad_h[i], R2_NTST(st));
        CHECKF(g_big[0x1000] == 0xB7 && g_big[0x1FFF] == 0xB7, "handle %p: requested-information block written on failure", bad_h[i]);
    }
}
T2(opm_get_copp_information_bogus_handles_fail_output_untouched) {
    NEED_NT(NtGdiGetCOPPCompatibleOPMInformation);
    for (unsigned i = 0; i < NBADH; i++) {
        memset(g_big, 0, 0x1000);
        memset(g_big + 0x1000, 0xB7, 0x1000);
        LONG st = UN(NtGdiGetCOPPCompatibleOPMInformation)(bad_h[i], g_big, g_big + 0x1000);
        CHECKF(st < 0, "handle %p: status 0x%lx", bad_h[i], R2_NTST(st));
        CHECKF(g_big[0x1000] == 0xB7 && g_big[0x1FFF] == 0xB7, "handle %p: output written on failure", bad_h[i]);
    }
}
T2(opm_destroy_bogus_handles_fail) {
    NEED_NT(NtGdiDestroyOPMProtectedOutput);
    for (unsigned i = 0; i < NBADH; i++) {
        LONG st = UN(NtGdiDestroyOPMProtectedOutput)(bad_h[i]);
        CHECKF(st < 0, "handle %p: status 0x%lx", bad_h[i], R2_NTST(st));
    }
}
T2(opm_create_protected_output_unknown_target_fails) {
    NEED_NT(NtGdiCreateOPMProtectedOutput);
    /* params: LUID adapter (8), ULONG VidPnTargetId, ULONG semantics (read as 16 bytes). No adapter has this LUID. */
    struct cp { LONG lo; LONG hi; ULONG target; ULONG sem; };
    static const struct cp p[] = {
        { 0x1234, 0x5678, 0, 0 }, { 0x7FFFFFF0, 0x7FFFFFF0, 1, 1 }, { 0, 0, 0xFFFF, 0 }, { 0x10101, 0, 0, 0xFFFFFFFF } };
    for (unsigned i = 0; i < sizeof p / sizeof *p; i++) {
        struct cp in = p[i];
        HANDLE out = (HANDLE)0x5A5A5A5A;
        LONG st = UN(NtGdiCreateOPMProtectedOutput)(&in, &out);
        CHECKF(st < 0, "case %u: status 0x%lx", i, R2_NTST(st));
        /* the handler stores its local handle (initialised to -1) unconditionally once the device list lock was taken */
        CHECKF(out == (HANDLE)0x5A5A5A5A || out == (HANDLE)(ULONG_PTR)-1, "case %u: out handle %p", i, out);
        if (st >= 0 && out != (HANDLE)(ULONG_PTR)-1) UN(NtGdiDestroyOPMProtectedOutput)(out);
    }
}
T2(opm_suggested_size_and_create_outputs_empty_name) {
    NEED_NT(NtGdiGetSuggestedOPMProtectedOutputArraySize); NEED_NT(NtGdiCreateOPMProtectedOutputs);
    UNICODE_STRING u = { 0, 2, (PWSTR)L"" };
    ULONG c = 0x5A5A5A5A; HANDLE h[2] = { (HANDLE)0x77, (HANDLE)0x77 }; ULONG got = 0x5A5A5A5A;
    LONG st = UN(NtGdiGetSuggestedOPMProtectedOutputArraySize)(&u, &c);
    CHECKF(st < 0 && c == 0x5A5A5A5A, "empty device name: status 0x%lx count %lu", R2_NTST(st), c);
    st = UN(NtGdiCreateOPMProtectedOutputs)(&u, 0, 2, &got, h);
    CHECKF(st < 0 && h[0] == (HANDLE)0x77, "empty device name: create status 0x%lx", R2_NTST(st));
}
T2(opm_dxva2_exports_exist_or_skip) {
    HMODULE dx = t_module("dxva2.dll");
    if (!dx) { t_skip("dxva2.dll not available"); return; }
    static const char *ex[] = { "GetNumberOfPhysicalMonitorsFromHMONITOR", "GetPhysicalMonitorsFromHMONITOR", "DestroyPhysicalMonitor",
        "DestroyPhysicalMonitors", "GetPhysicalMonitorsFromIDirect3DDevice9", "GetNumberOfPhysicalMonitorsFromIDirect3DDevice9",
        "GetCapabilitiesStringLength", "CapabilitiesRequestAndCapabilitiesReply", "GetVCPFeatureAndVCPFeatureReply", "SetVCPFeature",
        "GetMonitorBrightness", "SetMonitorBrightness", "GetMonitorContrast", "SaveCurrentMonitorSettings", "GetMonitorCapabilities" };
    for (unsigned i = 0; i < sizeof ex / sizeof *ex; i++) CHECKF(GetProcAddress(dx, ex[i]) != NULL, "dxva2!%s missing", ex[i]);
}
T2(opm_dxva2_physical_monitor_handle_rejected_by_destroy_and_brightness) {
    HMODULE dx = t_module("dxva2.dll");
    if (!dx) { t_skip("dxva2.dll not available"); return; }
    typedef BOOL (WINAPI *DP)(HANDLE);
    typedef BOOL (WINAPI *GB)(HANDLE, DWORD *, DWORD *, DWORD *);
    DP dp = (DP)GetProcAddress(dx, "DestroyPhysicalMonitor");
    GB gb = (GB)GetProcAddress(dx, "GetMonitorBrightness");
    if (!dp || !gb) { t_skip("dxva2 exports missing"); return; }
    for (unsigned i = 0; i < NBADH; i++) {
        SetLastError(0);
        CHECKF(dp(bad_h[i]) == FALSE, "DestroyPhysicalMonitor(%p) succeeded", bad_h[i]);
        CHECKF(GetLastError() != 0, "no last error from DestroyPhysicalMonitor(%p)", bad_h[i]);
    }
    (void)gb;   /* GetMonitorBrightness on a handle nobody created issues no DDC/CI traffic; not asserted: its failure path is monitor-API internal */
}
