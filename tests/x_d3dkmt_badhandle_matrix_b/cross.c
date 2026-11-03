/* x_d3dkmt_badhandle_matrix_b / cross: properties that span several of the thunks.
 *
 * Measures, with no adapter, device, context or allocation involved:
 *   - the 36 thunks have 36 distinct service numbers, all in the win32k range, and each gdi32 export is a stub
 *     with its win32u twin's number;
 *   - a thread that has never called into graphics before gets the same answers as the main thread;
 *   - the two hardware-context thunks answer STATUS_NOT_IMPLEMENTED before looking at any argument, while the
 *     hardware-queue thunks that sit next to them validate arguments (STATUS_INVALID_PARAMETER);
 *   - GetPresentHistory answers STATUS_ACCESS_DENIED to a caller that is not the compositor, whatever block it gets;
 *   - interleaving every thunk with every other in a loop does not change any answer (no cross-call state).
 *
 * Determinism: only symbolic status codes are printed. No handle, address or size from the machine is printed.
 */
#include "x.h"

static const char *const ALL[] = {
    "CreateContext", "CreateContextVirtual", "CreateHwContext", "CreateHwQueue", "DestroyHwContext", "DestroyHwQueue",
    "SubmitCommand", "SubmitCommandToHwQueue", "SubmitSignalSyncObjectsToHwQueue", "SubmitWaitForSyncObjectsToHwQueue",
    "Present", "PresentRedirected", "PresentMultiPlaneOverlay", "PresentMultiPlaneOverlay2", "PresentMultiPlaneOverlay3",
    "ReserveGpuVirtualAddress", "MapGpuVirtualAddress", "UpdateGpuVirtualAddress", "FreeGpuVirtualAddress",
    "OfferAllocations", "ReclaimAllocations", "ReclaimAllocations2", "Lock2", "Unlock2", "UpdateAllocationProperty",
    "FlushHeapTransitions", "GetPresentHistory", "ShareObjects", "OpenResource", "OpenResourceFromNtHandle",
    "CreateBundleObject", "CreateProtectedSession", "DestroyProtectedSession", "QueryProtectedSessionStatus",
    "OpenProtectedSessionFromNtHandle", "QueryProtectedSessionInfoFromNtHandle" };

/* status of a zero block, per thunk (index matches ALL) */
static LONG zero_status(unsigned i) {
    const char *n = ALL[i];
    if (!strcmp(n, "CreateHwContext") || !strcmp(n, "DestroyHwContext")) return XS_NOT_IMPLEMENTED;
    if (!strcmp(n, "GetPresentHistory")) return XS_ACCESS_DENIED;
    if (strstr(n, "NtHandle")) return XS_INVALID_HANDLE;
    return XS_INVALID_PARAMETER;
}

TEST(x_cross_service_numbers_distinct) {
    long num[XARR(ALL)];
    int have = 0;
    for (unsigned i = 0; i < XARR(ALL); i++) {
        x_fn f = x_nt(ALL[i]);
        if (!f) { t_skip("win32u export not available (emulator or missing export)"); return; }
        num[i] = k_stub_number((FARPROC)f);
        CHECKF(num[i] >= 0x1000, "%s service number below the win32k range", ALL[i]);
        have++;
    }
    int dup = 0;
    for (unsigned i = 0; i < XARR(ALL); i++)
        for (unsigned j = i + 1; j < XARR(ALL); j++)
            if (num[i] == num[j]) dup++;
    CHECKF(dup == 0, "%d duplicated service numbers", dup);
    EXPECT_EQ(have, XARR(ALL));
}

TEST(x_cross_gdi32_twins_share_numbers) {
    x_fn probe = x_nt("Present");
    if (!probe) { t_skip("win32u export not available (emulator or missing export)"); return; }
    int same = 0, stubs = 0;
    for (unsigned i = 0; i < XARR(ALL); i++) {
        x_fn n = x_nt(ALL[i]), g = x_gdi(ALL[i]);
        if (!n || !g) continue;
        if (!k_stub_valid((FARPROC)g)) continue;      /* user-mode implementation (OpenResource) */
        stubs++;
        if (k_stub_number((FARPROC)g) == k_stub_number((FARPROC)n)) same++;
    }
    EXPECT_EQ(same, stubs);
    CHECK(stubs == XARR(ALL) - 1);
}

static DWORD WINAPI worker(LPVOID arg) {
    LONG *out = arg;
    for (unsigned i = 0; i < XARR(ALL); i++) {
        x_fn f = x_nt(ALL[i]);
        out[i] = f ? f(x_edge(0x400)) : (LONG)0xC0000BAD;
    }
    return 0;
}

TEST(x_cross_fresh_thread_same_answers) {
    if (!x_nt("Present")) { t_skip("win32u export not available (emulator or missing export)"); return; }
    static LONG got[XARR(ALL)];
    HANDLE h = CreateThread(NULL, 0, worker, got, 0, NULL);
    if (!h) { t_skip("cannot create thread"); return; }
    CHECK(WaitForSingleObject(h, 20000) == WAIT_OBJECT_0);
    CloseHandle(h);
    for (unsigned i = 0; i < XARR(ALL); i++)
        X_ST(got[i], zero_status(i), "%s(zero block) on a fresh thread", ALL[i]);
}

TEST(x_cross_hw_context_not_implemented_queue_validates) {
    x_fn cc = x_nt("CreateHwContext"), dc = x_nt("DestroyHwContext"), cq = x_nt("CreateHwQueue"), dq = x_nt("DestroyHwQueue");
    if (!cc || !dc || !cq || !dq) { t_skip("win32u export not available (emulator or missing export)"); return; }
    X_ST(cc(NULL), XS_NOT_IMPLEMENTED, "CreateHwContext(NULL)");
    X_ST(dc(NULL), XS_NOT_IMPLEMENTED, "DestroyHwContext(NULL)");
    X_ST(cc((void *)(ULONG_PTR)1), XS_NOT_IMPLEMENTED, "CreateHwContext(address 1)");
    X_ST(dc((void *)(ULONG_PTR)0xFFFF800000000000ull), XS_NOT_IMPLEMENTED, "DestroyHwContext(kernel address)");
    X_ST(cq(NULL), XS_INVALID_PARAMETER, "CreateHwQueue(NULL)");
    X_ST(dq(NULL), XS_INVALID_PARAMETER, "DestroyHwQueue(NULL)");
    X_ST(cq((void *)(ULONG_PTR)1), XS_INVALID_PARAMETER, "CreateHwQueue(address 1)");
    X_ST(dq((void *)(ULONG_PTR)1), XS_INVALID_PARAMETER, "DestroyHwQueue(address 1)");
}

TEST(x_cross_present_history_denied_before_arguments) {
    x_fn f = x_nt("GetPresentHistory");
    if (!f) { t_skip("win32u export not available (emulator or missing export)"); return; }
    X_ST(f(NULL), XS_ACCESS_DENIED, "GetPresentHistory(NULL)");
    X_ST(f((void *)(ULONG_PTR)1), XS_ACCESS_DENIED, "GetPresentHistory(address 1)");
    X_ST(f(x_edge(4)), XS_ACCESS_DENIED, "GetPresentHistory(4 bytes)");
    X_ST(f(x_edge(0x400)), XS_ACCESS_DENIED, "GetPresentHistory(zero block)");
    DWORD *p = x_edge(0x400); for (int i = 0; i < 0x100; i++) p[i] = 0xFFFFFFFF;
    X_ST(f(p), XS_ACCESS_DENIED, "GetPresentHistory(filled block)");
}

TEST(x_cross_interleaved_calls_no_cross_state) {
    if (!x_nt("Present")) { t_skip("win32u export not available (emulator or missing export)"); return; }
    int bad = 0;
    for (int round = 0; round < 8; round++)
        for (unsigned i = 0; i < XARR(ALL); i++) {
            unsigned k = (i * 7 + round * 5) % XARR(ALL);   /* a different order each round */
            x_fn f = x_nt(ALL[k]);
            if (f(x_edge(0x400)) != zero_status(k)) bad++;
        }
    CHECKF(bad == 0, "%d of 288 interleaved calls changed status", bad);
}

TEST(x_cross_all_null_same_class) {
    /* every thunk but the two special ones rejects NULL with INVALID_PARAMETER: count them */
    if (!x_nt("Present")) { t_skip("win32u export not available (emulator or missing export)"); return; }
    int inval = 0, notimpl = 0, denied = 0, other = 0;
    for (unsigned i = 0; i < XARR(ALL); i++) {
        LONG r = x_nt(ALL[i])(NULL);
        if (r == XS_INVALID_PARAMETER) inval++;
        else if (r == XS_NOT_IMPLEMENTED) notimpl++;
        else if (r == XS_ACCESS_DENIED) denied++;
        else other++;
    }
    EXPECT_EQ(inval, XARR(ALL) - 3);
    EXPECT_EQ(notimpl, 2);
    EXPECT_EQ(denied, 1);
    EXPECT_EQ(other, 0);
}

/* A block whose size fields are all huge makes dxgkrnl work for seconds before it rejects the call (it sizes and
 * probes the private-data buffers first): the answer is the same, so the generic matrix leaves these fills out and
 * this test runs each one once. */
TEST(x_cross_huge_size_fields_rejected_after_probe) {
    x_fn cq = x_nt("CreateHwQueue"), sig = x_nt("SubmitSignalSyncObjectsToHwQueue"), wt = x_nt("SubmitWaitForSyncObjectsToHwQueue");
    if (!cq || !sig || !wt) { t_skip("win32u export not available (emulator or missing export)"); return; }
    static const DWORD v[] = { 0x40000000, 0xDEADBEEF, 0xFFFFFFFF };
    for (unsigned k = 0; k < XARR(v); k++) {
        DWORD *p = x_edge(0x400); for (int i = 0; i < 0x100; i++) p[i] = v[k];
        X_ST(cq(p), XS_INVALID_PARAMETER, "CreateHwQueue(block of 0x%lx)", (unsigned long)v[k]);
    }
    DWORD *p = x_edge(0x400); for (int i = 0; i < 0x100; i++) p[i] = 0x40000000;
    X_ST(sig(p), XS_INVALID_PARAMETER, "SubmitSignalSyncObjectsToHwQueue(block of 0x40000000)");
    p = x_edge(0x400); for (int i = 0; i < 0x100; i++) p[i] = 0x40000000;
    X_ST(wt(p), XS_INVALID_PARAMETER, "SubmitWaitForSyncObjectsToHwQueue(block of 0x40000000)");
}
