/* Families whose service bodies live outside win32kbase/win32kfull (dxgkrnl flip manager, composition surface manager,
 * dxgkrnl Dxgk* entry points, win32kmin Moderncore): win32k.sys only carries a thunk (an indirect jump through the
 * ext-ms-win-core-win32k-{flipmgr,surfmgr,dxgk} slot), so no body is available to prove a status. The tests therefore
 * assert only invariants every kernel service must keep, plus statuses that follow from a bogus *handle* being rejected:
 *   - the call returns (no exception, no hang) for the argument shapes below;
 * The NtDxgk* services are not exercised at all: their first argument is not known to be a handle and a blind call with
 * a fabricated pointer-like value would be an unproven garbage-pointer probe.
 *   - a syscall given an invalid first handle fails (NT_FAIL) and creates no handle;
 *   - the process handle count is unchanged by failed calls.
 * Real objects are obtained through the same exports the OS components use: ntdll NtFlipObjectCreate(&h) as called by
 * dcomp!CreateFlipObject (a single out-pointer, see its disassembly) and dcomp!DCompositionCreateSurfaceHandle.
 * Not called on purpose: NtModerncoreIdleTimerThread (a thread parking call), all NtDxgk*, NtDxgkVail*, NtDxgkDispMgrOperation,
 * NtDxgkDisplayPortOperation, NtDxgkSubmitPresent*, NtDxgkOutputDuplPresentToHwQueue, NtDxgkPin/UnpinResources: they
 * act on display hardware or container state. */
#include "rc.h"

typedef NTSTATUS (WINAPI *gen8_fn)(ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR);

struct prow { const char *name; int strict; };   /* strict: a bogus first handle must give a failure status */

static const struct prow flip_rows[] = {
    {"NtFlipObjectAddContent", 1}, {"NtFlipObjectAddPoolBuffer", 1}, {"NtFlipObjectConsumerAcquirePresent", 1},
    {"NtFlipObjectConsumerAdjustUsageReference", 1}, {"NtFlipObjectConsumerBeginProcessPresent", 1},
    {"NtFlipObjectConsumerEndProcessPresent", 1}, {"NtFlipObjectConsumerPostMessage", 1},
    {"NtFlipObjectConsumerQueryBufferInfo", 1}, {"NtFlipObjectDisconnectEndpoint", 1},
    {"NtFlipObjectEnablePresentStatisticsType", 1}, {"NtFlipObjectPresentCancel", 1},
    {"NtFlipObjectQueryBufferAvailableEvent", 1}, {"NtFlipObjectQueryEndpointConnected", 1},
    {"NtFlipObjectQueryNextMessageToProducer", 1}, {"NtFlipObjectReadNextMessageToProducer", 1},
    {"NtFlipObjectRemoveContent", 1}, {"NtFlipObjectRemovePoolBuffer", 1}, {"NtFlipObjectSetContent", 1},
    {"NtFlipObjectSetMaximumBackchannelQueueDepth", 1},
};
static const struct prow surface_rows[] = {
    {"NtValidateCompositionSurfaceHandle", 1}, {"NtQueryCompositionSurfaceBinding", 1},
    {"NtQueryCompositionSurfaceStatistics", 1}, {"NtSetCompositionSurfaceStatistics", 1},
    {"NtSetCompositionSurfaceBufferUsage", 1}, {"NtQueryCompositionSurfaceHDRMetaData", 1},
    {"NtQueryCompositionSurfaceRenderingRealization", 1}, {"NtUnBindCompositionSurface", 1},
    {"NtBindCompositionSurface", 1},
    {"NtOpenCompositionSurfaceDirtyRegion", 0}, {"NtOpenCompositionSurfaceSectionInfo", 0},
    {"NtOpenCompositionSurfaceSwapChainHandleInfo", 0}, {"NtSetCompositionSurfaceAnalogExclusive", 0},
    {"NtSetCompositionSurfaceDirectFlipState", 0}, {"NtSetCompositionSurfaceIndependentFlipInfo", 0},
    {"NtNotifyPresentToCompositionSurface", 0},
};

static void run_rows(const struct prow *rows, size_t n, ULONG_PTR first, const char *what) {
    size_t i;
    for (i = 0; i < n; i++) {
        FARPROC p = t_proc_in("win32u.dll", rows[i].name);
        if (!p) { t_note("%s: export missing", rows[i].name); continue; }
        unsigned long h0 = rc_handles();
        NTSTATUS st = ((gen8_fn)p)(first, 0, 0, 0, 0, 0, 0, 0);
        t_note("%s(%s) -> 0x%08lx (%s)", rows[i].name, what, (unsigned long)st, k_status_name((K_NTSTATUS)st));
        if (rows[i].strict) CHECKF(NT_FAIL(st), "%s(%s) = 0x%08lx, want a failure status", rows[i].name, what, (unsigned long)st);
        CHECKF(rc_handles() == h0, "%s(%s) leaked a handle (%lu -> %lu)", rows[i].name, what, h0, rc_handles());
    }
}

RT(flip_object_calls_reject_bogus_handle) {
    NEED_WIN();
    run_rows(flip_rows, sizeof flip_rows / sizeof flip_rows[0], 0x7FFC, "bogus handle");
}
RT(flip_object_calls_reject_null_handle) {
    NEED_WIN();
    run_rows(flip_rows, sizeof flip_rows / sizeof flip_rows[0], 0, "NULL handle");
}
RT(flip_object_calls_reject_non_flip_handle) {
    NEED_WIN();
    /* a valid handle of another object type (the current process) must not be accepted as a flip object */
    run_rows(flip_rows, sizeof flip_rows / sizeof flip_rows[0], (ULONG_PTR)GetCurrentProcess(), "process pseudo-handle");
}
RT(surface_calls_reject_bogus_handle) {
    NEED_WIN();
    run_rows(surface_rows, sizeof surface_rows / sizeof surface_rows[0], 0x7FFC, "bogus handle");
}
RT(surface_calls_reject_non_surface_handle) {
    NEED_WIN();
    HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
    REQUIRE(ev);
    run_rows(surface_rows, sizeof surface_rows / sizeof surface_rows[0], (ULONG_PTR)ev, "event handle");
    CloseHandle(ev);
}
/* ---- real flip object ---- */
UDECL(NTSTATUS, NtFlipObjectCreate, (HANDLE *phOut));
RT(flip_object_create_close) {
    NEED_WIN();
    NtFlipObjectCreate_pfn cr = UN(NtFlipObjectCreate); REQUIRE(cr);
    HANDLE a = NULL, b = NULL; char tn[64], tn2[64]; unsigned long h0 = rc_handles();
    NTSTATUS st = cr(&a);
    t_note("NtFlipObjectCreate -> 0x%08lx handle %p", (unsigned long)st, a);
    if (st < 0) { CHECK(a == NULL); CHECK(rc_handles() == h0); t_skip("flip objects not available here"); return; }
    CHECK(a != NULL);
    CHECK(rc_handles() == h0 + 1);
    rc_type_name(a, tn, sizeof tn);
    t_note("flip object type \"%s\"", tn);
    CHECK(tn[0] != 0);
    CHECK_ST(cr(&b), 0);
    CHECK(b != NULL && b != a);
    rc_type_name(b, tn2, sizeof tn2);
    CHECK(!strcmp(tn, tn2));
    {   HANDLE d = NULL;
        CHECK(DuplicateHandle(GetCurrentProcess(), a, GetCurrentProcess(), &d, 0, FALSE, DUPLICATE_SAME_ACCESS));
        if (d) { char t3[64]; rc_type_name(d, t3, sizeof t3); CHECK(!strcmp(tn, t3)); CloseHandle(d); }
    }
    CHECK(CloseHandle(a)); CHECK(CloseHandle(b));
    CHECK(rc_handles() == h0);
}

/* ---- real surface handle (dcomp.dll!DCompositionCreateSurfaceHandle) ---- */
typedef HRESULT (WINAPI *CreateSurfaceHandle_fn)(DWORD access, SECURITY_ATTRIBUTES *sa, HANDLE *ph);
static HANDLE make_surface(DWORD access, BOOL inherit, HRESULT *hr) {
    HANDLE h = NULL; SECURITY_ATTRIBUTES sa; HRESULT r;
    CreateSurfaceHandle_fn f = (CreateSurfaceHandle_fn)t_proc_in("dcomp.dll", "DCompositionCreateSurfaceHandle");
    if (!f) { if (hr) *hr = E_NOTIMPL; return NULL; }
    memset(&sa, 0, sizeof sa); sa.nLength = sizeof sa; sa.bInheritHandle = inherit;
    r = f(access, inherit ? &sa : NULL, &h);
    if (hr) *hr = r;
    return r >= 0 ? h : NULL;
}
RT(surface_handle_create_access_and_inherit) {
    NEED_WIN();
    HRESULT hr;
    /* Warm-up: the first call loads dcomp.dll and its dependencies, which opens handles of its own; the
     * handle accounting below must only see the surface handles. */
    {   HANDLE warm = make_surface(0x0003, FALSE, &hr); if (warm) CloseHandle(warm); }
    unsigned long h0 = rc_handles();
    HANDLE a = make_surface(0x0003 /* COMPOSITIONSURFACE_ALL_ACCESS */, FALSE, &hr);
    if (!a) { t_note("DCompositionCreateSurfaceHandle -> 0x%08lx", (unsigned long)hr); t_skip("no composition surface handle"); return; }
    char tn[64]; rc_type_name(a, tn, sizeof tn);
    t_note("composition surface handle type \"%s\"", tn);
    CHECK(tn[0] != 0);
    CHECK(rc_handles() == h0 + 1);
    DWORD fl = 0;
    CHECK(GetHandleInformation(a, &fl));
    CHECK((fl & HANDLE_FLAG_INHERIT) == 0);               /* no security attributes: not inheritable */
    {   NtQueryObject_pfn q = UN(NtQueryObject); unsigned char bi[0x40]; ULONG ret = 0;
        if (q && q(a, 0, bi, sizeof bi, &ret) >= 0) {
            ULONG granted = *(ULONG *)(bi + 4);            /* OBJECT_BASIC_INFORMATION.GrantedAccess */
            t_note("granted access 0x%lx", granted);
            CHECK((granted & 0x3) == 0x3);                 /* both composition-surface rights were requested */
        }
    }
    HANDLE b = make_surface(0x0001, TRUE, &hr);            /* bInheritHandle -> OBJ_INHERIT in the OBJECT_ATTRIBUTES */
    if (b) {
        CHECK(b != a);
        CHECK(GetHandleInformation(b, &fl));
        CHECK((fl & HANDLE_FLAG_INHERIT) != 0);
        char t2[64]; rc_type_name(b, t2, sizeof t2);
        CHECK(!strcmp(tn, t2));
        CloseHandle(b);
    }
    CloseHandle(a);
    CHECK(rc_handles() == h0);
}
/* ---- cross-process-visual and swapchain-to-DWM calls with bogus operands ---- */
UDECL(NTSTATUS, NtDCompositionDuplicateSwapchainHandleToDwm, (HANDLE hSrc, HANDLE *phOut));
UDECL(NTSTATUS, NtDCompositionRegisterVirtualDesktopVisual, (ULONG_PTR a, ULONGLONG *pId, HANDLE hVisual));
UDECL(NTSTATUS, NtDCompositionSetChildRootVisual, (ULONG_PTR hwnd, HANDLE hVisual));
UDECL(NTSTATUS, NtDCompositionAddCrossDeviceVisualChild, (UINT a, UINT b, UINT c, UINT d, int e, UINT f, UINT g));
UDECL(NTSTATUS, NtDCompositionRemoveCrossDeviceVisualChild, (UINT a, UINT b, UINT c, UINT d));
RT(swapchain_to_dwm_duplication_arguments) {
    NEED_WIN();
    NtDCompositionDuplicateSwapchainHandleToDwm_pfn f = UN(NtDCompositionDuplicateSwapchainHandleToDwm); REQUIRE(f);
    HANDLE out = (HANDLE)(ULONG_PTR)0x1234; unsigned long h0 = rc_handles();
    CHECK_ST(f((HANDLE)(ULONG_PTR)0x7FFC, NULL), S_INVPARAM);        /* out pointer is mandatory */
    CHECK_ST(f(NULL, NULL), S_INVPARAM);
    NTSTATUS st = f((HANDLE)(ULONG_PTR)0x7FFC, &out);               /* no DWM -> 0xC000000D, else ObDuplicateObject fails */
    t_note("duplicate bogus swapchain handle -> 0x%08lx", (unsigned long)st);
    CHECK(NT_FAIL(st));
    CHECK(out == (HANDLE)(ULONG_PTR)0x1234);
    CHECK(rc_handles() == h0);
}
RT(virtual_desktop_visual_rejects_bad_handles) {
    NEED_WIN();
    NtDCompositionRegisterVirtualDesktopVisual_pfn f = UN(NtDCompositionRegisterVirtualDesktopVisual); REQUIRE(f);
    NtDCompositionSetChildRootVisual_pfn g = UN(NtDCompositionSetChildRootVisual); REQUIRE(g);
    ULONGLONG id = 0x1234;
    NTSTATUS st = f(0, &id, (HANDLE)(ULONG_PTR)0x7FFC);
    t_note("RegisterVirtualDesktopVisual(bogus) -> 0x%08lx", (unsigned long)st);
    CHECK(NT_FAIL(st));                                              /* ResourceObject::ResolveHandle fails before the DWM port is touched */
    st = f(0, &id, GetCurrentProcess());
    CHECK(NT_FAIL(st));
    st = g(0, (HANDLE)(ULONG_PTR)0x7FFC);
    t_note("SetChildRootVisual(bogus visual) -> 0x%08lx", (unsigned long)st);
    CHECK(NT_FAIL(st));
    st = g(0, GetCurrentProcess());
    CHECK(NT_FAIL(st));
}
RT(virtual_desktop_visual_wrong_resource_type) {
    NEED_WIN();
    NtDCompositionRegisterVirtualDesktopVisual_pfn f = UN(NtDCompositionRegisterVirtualDesktopVisual); REQUIRE(f);
    NtDCompositionSetChildRootVisual_pfn g = UN(NtDCompositionSetChildRootVisual); REQUIRE(g);
    NtDCompositionCreateSharedResourceHandle_pfn mk = UN(NtDCompositionCreateSharedResourceHandle);
    if (!mk) { t_skip("no CreateSharedResourceHandle"); return; }
    HANDLE h = NULL; ULONGLONG id = 0x1234;
    if (mk(0x8E, &h) < 0 || !h) { t_skip("shared resource type 0x8e unavailable"); return; }
    /* Only resource type 0xc3 (a shared visual) is accepted; any other type is rejected with 0xC000000D. */
    NTSTATUS a = f(0, &id, h), b = g(0, h);
    t_note("type-0x8e object as visual: Register -> 0x%08lx, SetChildRoot -> 0x%08lx", (unsigned long)a, (unsigned long)b);
    CHECK(NT_FAIL(a));
    CHECK(NT_FAIL(b));
    CloseHandle(h);
}
RT(cross_device_visual_child_unknown_channels) {
    NEED_WIN();
    NtDCompositionAddCrossDeviceVisualChild_pfn a = UN(NtDCompositionAddCrossDeviceVisualChild); REQUIRE(a);
    NtDCompositionRemoveCrossDeviceVisualChild_pfn r = UN(NtDCompositionRemoveCrossDeviceVisualChild); REQUIRE(r);
    struct rc_chan c; REQUIRE_CHAN(c);
    UINT bogus = rc_bogus_id(c.id);
    NTSTATUS s1 = a(bogus, 1, bogus, 2, 0, 3, 4), s2 = r(bogus, 1, bogus, 2);
    t_note("AddCrossDeviceVisualChild(dead channels) -> 0x%08lx, Remove -> 0x%08lx", (unsigned long)s1, (unsigned long)s2);
    CHECK(NT_FAIL(s1));
    CHECK(NT_FAIL(s2));
    rc_chan_destroy(&c);
}

/* ---- Moderncore thunks (win32kmin / modern-app only): pristine child, nothing is asserted but survival ---- */
RT(moderncore_thunks_survive_zero_arguments) {
    NEED_WIN();
    struct k_child_result r = k_child_run("r2_moderncore_zero", "", 30000);
    CHECK(r.exit_code == 0);
}
CHILD(r2_moderncore_zero) {
    static const char *names[] = {
        "NtModerncoreProcessConnect", "NtModerncoreBeginLayoutUpdate", "NtModerncoreCreateDCompositionHwndTarget",
        "NtModerncoreCreateGDIHwndTarget", "NtModerncoreDestroyDCompositionHwndTarget", "NtModerncoreDestroyGDIHwndTarget",
        "NtModerncoreEnableResizeLayoutSynchronization", "NtModerncoreGetNavigationWindowVisual",
        "NtModerncoreGetResizeDCompositionSynchronizationObject", "NtModerncoreGetWindowContentVisual",
        "NtModerncoreIsResizeLayoutSynchronizationEnabled", "NtModerncoreRegisterEnhancedNavigationWindowHandle",
        "NtModerncoreRegisterNavigationWindowHandle", "NtModerncoreSetNavigationServiceSid",
        "NtModerncoreUnregisterNavigationWindowHandle",
    };
    size_t i; int fails = 0;
    for (i = 0; i < sizeof names / sizeof names[0]; i++) {
        FARPROC p = t_proc_in("win32u.dll", names[i]);
        if (!p) { printf("NOTE %s: export missing\n", names[i]); continue; }
        unsigned long h0 = rc_handles();
        NTSTATUS st = ((gen8_fn)p)(0, 0, 0, 0, 0, 0, 0, 0);
        printf("NOTE %s(0,...) -> 0x%08lx\n", names[i], (unsigned long)st);
        if (rc_handles() != h0) { printf("FAIL %s leaked a handle\n", names[i]); fails++; }
    }
    return fails;
}
