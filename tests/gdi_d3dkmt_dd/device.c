/* Device/context lifecycle on the primary adapter plus "all-zero / bogus handle" validation of the other thunks. */
#include "d.h"
#include <stdlib.h>

#define OPEN_ADAPTER() dk_openhdc ad; HDC adc; if (!dk_open_screen(&ad, &adc)) { t_skip("no display adapter"); return; }
#define CLOSE_ADAPTER() do { dk_close_ok(ad.hAdapter); U(ReleaseDC)(NULL, adc); } while (0)

/* D3DKMT_CREATEDEVICE (x64): union { hAdapter; pAdapter } @0, Flags @8, hDevice @0xC */
static int create_device(ULONG hAdapter, ULONG *hDevice, LONG *stOut) {
    dk_fn1 f = dk_nt("NtGdiDdDDICreateDevice");
    unsigned char a[0x80]; memset(a, 0, sizeof a);
    *(ULONG *)a = hAdapter;
    LONG st = f ? f(a) : 0x7FFFFFFF;
    if (stOut) *stOut = st;
    if (!NTOK(st)) return 0;
    *hDevice = *(ULONG *)(a + 0xC);
    return 1;
}
static LONG destroy_device(ULONG h) { dk_fn1 f = dk_nt("NtGdiDdDDIDestroyDevice"); ULONG a[2] = { h, 0 }; return f ? f(a) : 0x7FFFFFFF; }
static LONG destroy_context(ULONG h) { dk_fn1 f = dk_nt("NtGdiDdDDIDestroyContext"); ULONG a[2] = { h, 0 }; return f ? f(a) : 0x7FFFFFFF; }

TEST(gdi_d3dkmt_dd_device_create_destroy) {
    DKREQ(f, "NtGdiDdDDICreateDevice");
    OPEN_ADAPTER();
    ULONG dev = 0; LONG st;
    if (!create_device(ad.hAdapter, &dev, &st)) { NOTE_ST("CreateDevice", st); CLOSE_ADAPTER(); t_skip("device creation unavailable on this adapter"); return; }
    CHECK(dev != 0);
    CHECK(dev != ad.hAdapter);
    t_note("hAdapter 0x%lx hDevice 0x%lx", (unsigned long)ad.hAdapter, (unsigned long)dev);
    CHECK(destroy_device(dev) == ST_SUCCESS);
    CLOSE_ADAPTER();
}
TEST(gdi_d3dkmt_dd_device_double_destroy_fails) {
    DKREQ(f, "NtGdiDdDDICreateDevice");
    OPEN_ADAPTER();
    ULONG dev = 0; LONG st;
    if (!create_device(ad.hAdapter, &dev, &st)) { CLOSE_ADAPTER(); t_skip("device creation unavailable"); return; }
    CHECK(destroy_device(dev) == ST_SUCCESS);
    LONG again = destroy_device(dev);
    CHECK(!NTOK(again));
    NOTE_ST("second DestroyDevice", again);
    CLOSE_ADAPTER();
}
TEST(gdi_d3dkmt_dd_device_handle_is_not_adapter_handle) {
    DKREQ(f, "NtGdiDdDDICreateDevice");
    OPEN_ADAPTER();
    ULONG dev = 0; LONG st;
    if (!create_device(ad.hAdapter, &dev, &st)) { CLOSE_ADAPTER(); t_skip("device creation unavailable"); return; }
    /* handle kinds are checked: an adapter handle is not a device handle and vice versa */
    CHECK(!NTOK(destroy_device(ad.hAdapter)));
    dk_closeargs c = { dev };
    CHECK(!NTOK(((dk_fn1)dk_nt("NtGdiDdDDICloseAdapter"))(&c)));
    CHECK(destroy_device(dev) == ST_SUCCESS);
    CLOSE_ADAPTER();
}
TEST(gdi_d3dkmt_dd_device_two_devices_distinct) {
    DKREQ(f, "NtGdiDdDDICreateDevice");
    OPEN_ADAPTER();
    ULONG d1 = 0, d2 = 0; LONG st;
    if (!create_device(ad.hAdapter, &d1, &st)) { CLOSE_ADAPTER(); t_skip("device creation unavailable"); return; }
    if (create_device(ad.hAdapter, &d2, &st)) {
        CHECK(d1 != d2);
        CHECK(destroy_device(d2) == ST_SUCCESS);
    } else NOTE_ST("second CreateDevice", st);
    CHECK(destroy_device(d1) == ST_SUCCESS);
    CLOSE_ADAPTER();
}
TEST(gdi_d3dkmt_dd_device_create_on_closed_adapter_fails) {
    DKREQ(f, "NtGdiDdDDICreateDevice");
    OPEN_ADAPTER();
    dk_close_ok(ad.hAdapter);
    ULONG dev = 0; LONG st;
    int ok = create_device(ad.hAdapter, &dev, &st);
    CHECK(!ok);
    if (ok) destroy_device(dev);
    NOTE_ST("CreateDevice(closed adapter)", st);
    U(ReleaseDC)(NULL, adc);
}
TEST(gdi_d3dkmt_dd_device_create_invalid_adapter_fails) {
    DKREQ(f, "NtGdiDdDDICreateDevice");
    static const ULONG v[] = { 0, 0xDEADBEEF, 0xFFFFFFFF };
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        ULONG dev = 0; LONG st;
        int ok = create_device(v[i], &dev, &st);
        CHECKF(!ok, "adapter 0x%lx accepted", (unsigned long)v[i]);
        if (ok) destroy_device(dev);
    }
}
TEST(gdi_d3dkmt_dd_device_create_leaves_hdevice_zero_on_failure) {
    DKREQ(f, "NtGdiDdDDICreateDevice");
    unsigned char a[0x80]; memset(a, 0, sizeof a);
    *(ULONG *)a = 0xDEADBEEF;
    *(ULONG *)(a + 0xC) = 0xCCCCCCCC;
    LONG st = f(a);
    CHECK(!NTOK(st));
    CHECKF(*(ULONG *)(a + 0xC) == 0xCCCCCCCC || *(ULONG *)(a + 0xC) == 0, "hDevice written on failure: 0x%lx", (unsigned long)*(ULONG *)(a + 0xC));
}
TEST(gdi_d3dkmt_dd_device_create_via_gdi32_export) {
    typedef LONG (WINAPI *wr_fn)(void *);
    wr_fn w = (wr_fn)t_proc_in("gdi32.dll", "D3DKMTCreateDevice");
    wr_fn d = (wr_fn)t_proc_in("gdi32.dll", "D3DKMTDestroyDevice");
    REQUIRE(w); REQUIRE(d);
    OPEN_ADAPTER();
    unsigned char a[0x80]; memset(a, 0, sizeof a);
    *(ULONG *)a = ad.hAdapter;
    LONG st = w(a);
    if (NTOK(st)) { ULONG h[2] = { *(ULONG *)(a + 0xC), 0 }; CHECK(d(h) == ST_SUCCESS); }
    else NOTE_ST("D3DKMTCreateDevice", st);
    CLOSE_ADAPTER();
}
TEST(gdi_d3dkmt_dd_device_create_leaves_gdi_counts_alone) {
    DKREQ(f, "NtGdiDdDDICreateDevice");
    REQUIRE(U(GetGuiResources));
    OPEN_ADAPTER();
    unsigned g = dk_gui_count(GR_GDIOBJECTS), u = dk_gui_count(GR_USEROBJECTS);
    ULONG dev = 0; LONG st;
    if (create_device(ad.hAdapter, &dev, &st)) {
        CHECK(dk_gui_count(GR_GDIOBJECTS) == g);
        CHECK(dk_gui_count(GR_USEROBJECTS) == u);
        destroy_device(dev);
    }
    CLOSE_ADAPTER();
}
TEST(gdi_d3dkmt_dd_context_create_destroy) {
    DKREQ(f, "NtGdiDdDDICreateContext");
    OPEN_ADAPTER();
    ULONG dev = 0; LONG st;
    if (!create_device(ad.hAdapter, &dev, &st)) { CLOSE_ADAPTER(); t_skip("device creation unavailable"); return; }
    /* D3DKMT_CREATECONTEXT: hDevice @0, NodeOrdinal @4, EngineAffinity @8, Flags @0xC, pPrivateDriverData @0x10, PrivateDriverDataSize @0x18, ClientHint @0x1C, hContext @0x20 */
    unsigned char a[0x80]; memset(a, 0, sizeof a);
    *(ULONG *)a = dev;
    st = f(a);
    NOTE_ST("CreateContext(node 0, no private data)", st);
    if (NTOK(st)) {
        ULONG ctx = *(ULONG *)(a + 0x20);
        CHECK(ctx != 0);
        CHECK(destroy_context(ctx) == ST_SUCCESS);
        CHECK(!NTOK(destroy_context(ctx)));
    }
    CHECK(destroy_device(dev) == ST_SUCCESS);
    CLOSE_ADAPTER();
}
TEST(gdi_d3dkmt_dd_context_invalid_device_fails) {
    DKREQ(f, "NtGdiDdDDICreateContext");
    unsigned char a[0x80]; memset(a, 0, sizeof a);
    *(ULONG *)a = 0xDEADBEEF;
    *(ULONG *)(a + 0x20) = 0xCCCCCCCC;
    CHECK(!NTOK(f(a)));
    CHECK(*(ULONG *)(a + 0x20) == 0xCCCCCCCC || *(ULONG *)(a + 0x20) == 0);
}
TEST(gdi_d3dkmt_dd_context_destroy_invalid) {
    static const ULONG v[] = { 0, 0xDEADBEEF, 0xFFFFFFFF };
    DKREQ(f, "NtGdiDdDDIDestroyContext");
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) CHECKF(!NTOK(destroy_context(v[i])), "context 0x%lx", (unsigned long)v[i]);
}
TEST(gdi_d3dkmt_dd_device_destroy_invalid) {
    static const ULONG v[] = { 0, 0xDEADBEEF, 0xFFFFFFFF };
    DKREQ(f, "NtGdiDdDDIDestroyDevice");
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) CHECKF(!NTOK(destroy_device(v[i])), "device 0x%lx", (unsigned long)v[i]);
}
TEST(gdi_d3dkmt_dd_createalloc_direct_zero_allocations_rejected) {
    DKREQ(f, "NtGdiDdDDICreateAllocation");
    OPEN_ADAPTER();
    ULONG dev = 0; LONG st;
    if (!create_device(ad.hAdapter, &dev, &st)) { CLOSE_ADAPTER(); t_skip("device creation unavailable"); return; }
    unsigned char a[0x100]; memset(a, 0, sizeof a);
    *(ULONG *)a = dev;                 /* NumAllocations (+0x2C) == 0 and no resource: nothing to create */
    st = f(a);
    CHECKF(!NTOK(st), "CreateAllocation with no allocations accepted");
    NOTE_ST("CreateAllocation(NumAllocations=0)", st);
    destroy_device(dev);
    CLOSE_ADAPTER();
}
TEST(gdi_d3dkmt_dd_createalloc_direct_invalid_device) {
    DKREQ(f, "NtGdiDdDDICreateAllocation");
    unsigned char a[0x100]; memset(a, 0, sizeof a);
    *(ULONG *)a = 0xDEADBEEF;
    CHECK(!NTOK(f(a)));
}

/* ---- all-zero or bogus-handle argument blocks must be rejected by handle validation (no hangs, no state change) ---- */
#define BOGUS_CASE(tname, nt) \
    TEST(gdi_d3dkmt_dd_bogus_##tname) { \
        DKREQ(f, nt); \
        unsigned char a[0x200]; \
        memset(a, 0, sizeof a); \
        LONG st0 = f(a); \
        CHECKF(!NTOK(st0), nt " accepted an all-zero argument block"); \
        NOTE_ST(nt "(zeroed)", st0); \
        memset(a, 0, sizeof a); *(ULONG *)a = 0xDEADBEEF; \
        LONG st1 = f(a); \
        CHECKF(!NTOK(st1), nt " accepted handle 0xDEADBEEF"); \
    }
BOGUS_CASE(destroyalloc, "NtGdiDdDDIDestroyAllocation")
BOGUS_CASE(destroyalloc2, "NtGdiDdDDIDestroyAllocation2")
BOGUS_CASE(lock, "NtGdiDdDDILock")
BOGUS_CASE(unlock, "NtGdiDdDDIUnlock")
BOGUS_CASE(render, "NtGdiDdDDIRender")
BOGUS_CASE(getdevicestate, "NtGdiDdDDIGetDeviceState")
BOGUS_CASE(makeresident, "NtGdiDdDDIMakeResident")
BOGUS_CASE(evict, "NtGdiDdDDIEvict")
BOGUS_CASE(escape, "NtGdiDdDDIEscape")
BOGUS_CASE(setdisplaymode, "NtGdiDdDDISetDisplayMode")
BOGUS_CASE(getcontextprio, "NtGdiDdDDIGetContextSchedulingPriority")
BOGUS_CASE(setcontextprio, "NtGdiDdDDISetContextSchedulingPriority")
BOGUS_CASE(queryallocresidency, "NtGdiDdDDIQueryAllocationResidency")
BOGUS_CASE(getruntimedata, "NtGdiDdDDIGetRuntimeData")
BOGUS_CASE(multisamplelist, "NtGdiDdDDIGetMultisampleMethodList")
BOGUS_CASE(setqueuedlimit, "NtGdiDdDDISetQueuedLimit")
BOGUS_CASE(queryvideomem, "NtGdiDdDDIQueryVideoMemoryInfo")
BOGUS_CASE(opensyncobj, "NtGdiDdDDIOpenSynchronizationObject")
BOGUS_CASE(destroysyncobj, "NtGdiDdDDIDestroySynchronizationObject")
BOGUS_CASE(getpresentqueueevent, "NtGdiDdDDIGetPresentQueueEvent")
BOGUS_CASE(getallocpriority, "NtGdiDdDDIGetAllocationPriority")
BOGUS_CASE(setallocpriority, "NtGdiDdDDISetAllocationPriority")
BOGUS_CASE(getpostcompcaps, "NtGdiDdDDIGetPostCompositionCaps")
BOGUS_CASE(queryresourceinfo, "NtGdiDdDDIQueryResourceInfo")
BOGUS_CASE(cacheoccl, "NtGdiDdDDICheckOcclusion")
BOGUS_CASE(markdeviceaserror, "NtGdiDdDDIMarkDeviceAsError")
BOGUS_CASE(createpagingqueue, "NtGdiDdDDICreatePagingQueue")
BOGUS_CASE(destroypagingqueue, "NtGdiDdDDIDestroyPagingQueue")
BOGUS_CASE(destroykeyedmutex, "NtGdiDdDDIDestroyKeyedMutex")
BOGUS_CASE(waitforidle, "NtGdiDdDDIWaitForIdle")
BOGUS_CASE(getdwmvblank, "NtGdiDdDDIGetDWMVerticalBlankEvent")
BOGUS_CASE(getoverlaystate, "NtGdiDdDDIGetOverlayState")
BOGUS_CASE(createoverlay, "NtGdiDdDDICreateOverlay")
BOGUS_CASE(destroyoverlay, "NtGdiDdDDIDestroyOverlay")

/* ---- cross-cutting local-state checks ---- */
TEST(gdi_d3dkmt_dd_handles_are_not_nt_handles) {
    OPEN_ADAPTER();
    ULONG h = ad.hAdapter;
    DWORD flags = 0;
    BOOL isnt = GetHandleInformation((HANDLE)(ULONG_PTR)h, &flags);
    if (isnt) t_note("adapter handle 0x%lx collides with an NT handle value", (unsigned long)h);
    else CHECK(GetLastError() == ERROR_INVALID_HANDLE);
    CLOSE_ADAPTER();
}
TEST(gdi_d3dkmt_dd_handle_values_recorded) {
    OPEN_ADAPTER();
    ULONG dev = 0; LONG st;
    t_note("adapter handle 0x%08lx", (unsigned long)ad.hAdapter);
    if (create_device(ad.hAdapter, &dev, &st)) { t_note("device handle 0x%08lx", (unsigned long)dev); destroy_device(dev); }
    CLOSE_ADAPTER();
}
TEST(gdi_d3dkmt_dd_syscalls_set_no_gdi_last_error_on_success_path) {
    /* D3DKMT status codes are returned as NTSTATUS only; win32u stubs do not touch the Win32 last error */
    DKREQ(f, "NtGdiDdDDICloseAdapter");
    dk_closeargs c = { 0xDEADBEEF };
    SetLastError(0x7777);
    f(&c);
    CHECK(GetLastError() == 0x7777);
}
TEST(gdi_d3dkmt_dd_syscalls_preserve_teb_gdi_batch) {
    /* a D3DKMT syscall is not a batched GDI call: the TEB GDI batch counter is untouched by it */
    DKREQ(f, "NtGdiDdDDICloseAdapter");
    ULONG b0 = 0, b1 = 0;
    CHECK(k_try_read(((char *)k_teb()) + TEB_GdiBatchCount, &b0, 4));
    dk_closeargs c = { 0xDEADBEEF };
    for (int i = 0; i < 20; i++) f(&c);
    CHECK(k_try_read(((char *)k_teb()) + TEB_GdiBatchCount, &b1, 4));
    CHECKF(b0 == b1, "GdiBatchCount %lu -> %lu", (unsigned long)b0, (unsigned long)b1);
}
TEST(gdi_d3dkmt_dd_gui_thread_after_first_ddi_call) {
    DKREQ(f, "NtGdiDdDDICloseAdapter");
    dk_closeargs c = { 0xDEADBEEF };
    f(&c);
    void *ti = NULL;
    CHECK(k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &ti));
    if (!t_is_wine()) CHECK(ti != NULL);
}
TEST(gdi_d3dkmt_dd_calls_do_not_perturb_clientinfo) {
    /* Win32ClientInfo (TEB+0x800) is maintained by user32/win32k callbacks only; a D3DKMT call leaves its first words alone */
    DKREQ(f, "NtGdiDdDDICloseAdapter");
    unsigned char before[0x40] = { 0 }, after[0x40] = { 0 };
    f(&(dk_closeargs){ 0xDEADBEEF });
    CHECK(k_try_read((char *)k_teb() + TEB_Win32ClientInfo, before, sizeof before));
    for (int i = 0; i < 10; i++) f(&(dk_closeargs){ 0xDEADBEEF });
    CHECK(k_try_read((char *)k_teb() + TEB_Win32ClientInfo, after, sizeof after));
    CHECK(memcmp(before, after, sizeof before) == 0);
}
