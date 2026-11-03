/* x_d3dkmt_badhandle_matrix_b / generic: the same argument-validation matrix applied to 36 more front doors.
 *
 * Measures, per NtGdiDdDDI<Name> (win32u) and D3DKMT<Name> (gdi32), for the context, hardware queue, GPU virtual
 * address, offer/reclaim, Lock2/Unlock2, present, shared-resource, bundle and protected-session thunks, with no
 * adapter, device, context or allocation involved (every handle is bogus):
 *   - the win32u export is a well-formed syscall stub (service number >= 0x1000) and the gdi32 export carries the
 *     same service number (D3DKMTOpenResource is the exception: gdi32 implements it in user mode);
 *   - a NULL argument pointer and wild pointers (address 1, a kernel address, a released region, all ones, the top of
 *     user space) get one uniform status, never an access violation: STATUS_INVALID_PARAMETER for almost all, but
 *     STATUS_NOT_IMPLEMENTED for the two hardware-context thunks and STATUS_ACCESS_DENIED for GetPresentHistory
 *     (a caller check that runs before any argument is looked at);
 *   - an argument block shorter than the kernel's readable-size check is STATUS_INVALID_PARAMETER, and one of exactly
 *     the checked size gets past the probe (only the three NT-handle thunks show a size, because only they answer
 *     differently once the probe passes);
 *   - zero, all-ones, 0xDEADBEEF, small-value, large-value and valid-pointer filled blocks give the per-API status;
 *     one garbage dword written at each dword position of an otherwise zero block changes the answer only at the
 *     positions the kernel interprets (the two Submit*SyncObjectsToHwQueue counts, the NT-handle thunk size and
 *     pointer fields);
 *   - the gdi32 route gives the same answers as the win32u route, and 64 repeated calls give the same status every
 *     time without changing the process handle count or its GDI object count (no state leaks).
 *
 * Determinism: only symbolic status codes are printed. No handle, address or size from the machine is printed.
 */
#include "x.h"

#define P   XS_INVALID_PARAMETER
#define H   XS_INVALID_HANDLE
#define T   XS_TYPE_MISMATCH
#define M   XS_NO_MEMORY

/* pst: NULL / wild / short block.  zst: zero block (and the small-value fills).  ffst / deadst: block of 0xFFFFFFFF /
 * 0xDEADBEEF dwords.  size: readable bytes the kernel needs (0 = not measurable, the answer never changes with size).
 * dev: dword indices (of a 0x400 block) where a garbage dword changes the answer, with the new status devst.
 * count: dword fills of 0x7FFFFFFF or more are answered NO_MEMORY (a huge element count).  gdistub: gdi32 export is a
 * syscall stub with the same number. */
#define API_LIST(X) \
    X(CreateContext,                    P, P, P, P, 0,    NULL,        0, 0, 1) \
    X(CreateContextVirtual,             P, P, P, P, 0,    NULL,        0, 0, 1) \
    X(CreateHwContext,                  XS_NOT_IMPLEMENTED, XS_NOT_IMPLEMENTED, XS_NOT_IMPLEMENTED, XS_NOT_IMPLEMENTED, 0, NULL, 0, 0, 1) \
    X(CreateHwQueue,                    P, P, P, P, 0,    NULL,        0, 0, 1) \
    X(DestroyHwContext,                 XS_NOT_IMPLEMENTED, XS_NOT_IMPLEMENTED, XS_NOT_IMPLEMENTED, XS_NOT_IMPLEMENTED, 0, NULL, 0, 0, 1) \
    X(DestroyHwQueue,                   P, P, P, P, 0,    NULL,        0, 0, 1) \
    X(SubmitCommand,                    P, P, P, P, 0,    NULL,        0, 0, 1) \
    X(SubmitCommandToHwQueue,           P, P, P, P, 0,    NULL,        0, 0, 1) \
    X(SubmitSignalSyncObjectsToHwQueue, P, P, M, M, 0,    dev_sig,     1, 1, 1) \
    X(SubmitWaitForSyncObjectsToHwQueue,P, P, M, M, 0,    dev_wait,    1, 1, 1) \
    X(Present,                          P, P, P, P, 0,    NULL,        0, 0, 1) \
    X(PresentRedirected,                P, P, P, P, 0,    NULL,        0, 0, 1) \
    X(PresentMultiPlaneOverlay,         P, P, P, P, 0,    NULL,        0, 0, 1) \
    X(PresentMultiPlaneOverlay2,        P, P, P, P, 0,    NULL,        0, 0, 1) \
    X(PresentMultiPlaneOverlay3,        P, P, P, P, 0,    NULL,        0, 0, 1) \
    X(ReserveGpuVirtualAddress,         P, P, P, P, 0,    NULL,        0, 0, 1) \
    X(MapGpuVirtualAddress,             P, P, P, P, 0,    NULL,        0, 0, 1) \
    X(UpdateGpuVirtualAddress,          P, P, P, P, 0,    NULL,        0, 0, 1) \
    X(FreeGpuVirtualAddress,            P, P, P, P, 0,    NULL,        0, 0, 1) \
    X(OfferAllocations,                 P, P, P, P, 0,    NULL,        0, 0, 1) \
    X(ReclaimAllocations,               P, P, P, P, 0,    NULL,        0, 0, 1) \
    X(ReclaimAllocations2,              P, P, P, P, 0,    NULL,        0, 0, 1) \
    X(Lock2,                            P, P, P, P, 0,    NULL,        0, 0, 1) \
    X(Unlock2,                          P, P, P, P, 0,    NULL,        0, 0, 1) \
    X(UpdateAllocationProperty,         P, P, P, P, 0,    NULL,        0, 0, 1) \
    X(FlushHeapTransitions,             P, P, P, P, 0,    NULL,        0, 0, 1) \
    X(GetPresentHistory,                XS_ACCESS_DENIED, XS_ACCESS_DENIED, XS_ACCESS_DENIED, XS_ACCESS_DENIED, 0, NULL, 0, 0, 1) \
    X(ShareObjects,                     P, P, P, P, 0,    NULL,        0, 0, 1) \
    X(OpenResource,                     P, P, P, P, 0,    NULL,        0, 0, 0) \
    X(OpenResourceFromNtHandle,         P, H, T, H, 104,  NULL,        0, 0, 1) \
    X(CreateBundleObject,               P, P, P, P, 0,    NULL,        0, 0, 1) \
    X(CreateProtectedSession,           P, P, P, P, 0,    NULL,        0, 0, 1) \
    X(DestroyProtectedSession,          P, P, P, P, 0,    NULL,        0, 0, 1) \
    X(QueryProtectedSessionStatus,      P, P, P, P, 0,    NULL,        0, 0, 1) \
    X(OpenProtectedSessionFromNtHandle, P, H, T, H, 16,   NULL,        0, 0, 1) \
    X(QueryProtectedSessionInfoFromNtHandle, P, H, T, H, 40, dev_qpsi, 0, 0, 1)

/* dword positions where a garbage dword changes the answer (terminated by 0xFFFF) */
static const unsigned dev_sig[]  = { 4, 0xFFFF };
static const unsigned dev_wait[] = { 1, 0xFFFF };
static const unsigned dev_qpsi[] = { 2, 3, 4, 6, 7, 8, 0xFFFF };

static LONG dev_status(const unsigned *dev, unsigned idx, LONG zst, LONG count_st) {
    if (dev) for (; *dev != 0xFFFF; dev++) if (*dev == idx) return count_st;
    return zst;
}

static void do_null_wild(const char *name, LONG pst) {
    x_fn f = x_nt(name);
    if (!f) { t_skip("win32u export not available (emulator or missing export)"); return; }
    X_ST(f(NULL), pst, "%s(NULL)", name);
    void *wild[] = { (void *)(ULONG_PTR)1, (void *)(ULONG_PTR)0xFFFF800000000000ull, x_freed(), (void *)~(ULONG_PTR)0, (void *)(ULONG_PTR)0x7FFFFFFF0000ull };
    const char *wn[] = { "address 1", "kernel address", "released region", "all ones", "top of user space" };
    for (size_t i = 0; i < XARR(wild); i++)
        if (wild[i]) X_ST(f(wild[i]), pst, "%s(%s)", name, wn[i]);
}

static void do_short(const char *name, LONG pst, LONG zst, unsigned size) {
    x_fn f = x_nt(name);
    if (!f) { t_skip("win32u export not available (emulator or missing export)"); return; }
    X_ST(f(x_edge(4)), pst, "%s with 4 readable bytes", name);
    X_ST(f(x_edge(8)), pst, "%s with 8 readable bytes", name);
    if (!size) {
        /* no size shows: the zero answer is the same for every length, so the boundary cannot be seen */
        X_ST(f(x_edge(0x10)), zst, "%s with 16 readable bytes", name);
        X_ST(f(x_edge(0x400)), zst, "%s with 1024 readable bytes", name);
        return;
    }
    for (unsigned n = 4; n < size; n += 4)
        X_ST(f(x_edge(n)), pst, "%s with %u readable bytes (checked size is larger)", name, n);
    X_ST(f(x_edge(size)), zst, "%s with exactly the checked size", name);
    X_ST(f(x_edge(size + 8)), zst, "%s with 8 bytes more than the checked size", name);
    X_ST(f(x_edge(0x400)), zst, "%s with 1024 readable bytes", name);
}

/* CreateHwQueue works for seconds on a block of huge sizes (see cross.c), so it only gets the small fills here */
static int slow_fill(const char *name) { return !strcmp(name, "CreateHwQueue"); }

static void do_filled(const char *name, LONG zst, LONG ffst, LONG deadst, int count) {
    x_fn f = x_nt(name);
    if (!f) { t_skip("win32u export not available (emulator or missing export)"); return; }
    X_ST(f(x_edge(0x400)), zst, "%s(zero block)", name);
    DWORD *p;
    if (!slow_fill(name)) {
        p = x_edge(0x400); for (int i = 0; i < 0x100; i++) p[i] = 0xFFFFFFFF;
        X_ST(f(p), ffst, "%s(block of 0xFFFFFFFF)", name);
        p = x_edge(0x400); for (int i = 0; i < 0x100; i++) p[i] = 0xDEADBEEF;
        X_ST(f(p), deadst, "%s(block of 0xDEADBEEF)", name);
    }
    static const DWORD small[] = { 1, 2, 0x100, 0xFFFF, 0x10000 };
    for (unsigned v = 0; v < XARR(small); v++) {
        p = x_edge(0x400); for (int i = 0; i < 0x100; i++) p[i] = small[v];
        X_ST(f(p), zst, "%s(block of 0x%lx)", name, (unsigned long)small[v]);
    }
    static const DWORD big[] = { 0x7FFFFFFF, 0x80000000, 0xC0000001 };
    for (unsigned v = 0; v < XARR(big) && !slow_fill(name); v++) {
        p = x_edge(0x400); for (int i = 0; i < 0x100; i++) p[i] = big[v];
        X_ST(f(p), count ? M : zst, "%s(block of 0x%lx)", name, (unsigned long)big[v]);
    }
    /* a valid, readable pointer in every pointer-sized slot and a small handle-like value in each first dword */
    static DWORD target[0x400];
    for (int mode = 0; mode < 3 && !count && !slow_fill(name); mode++) {   /* a count thunk reads part of the pointer as an element count */
        p = x_edge(0x400);
        ULONGLONG *q = (ULONGLONG *)p; for (int i = 0; i < 0x80; i++) q[i] = (ULONGLONG)(ULONG_PTR)target;
        if (mode == 1) p[0] = 1;
        if (mode == 2) p[0] = 0xDEADBEEF;
        X_ST(f(p), zst, "%s(valid pointer in every slot, variant %d)", name, mode);
    }
}

static void do_dwords(const char *name, LONG zst, const unsigned *dev, LONG devst) {
    x_fn f = x_nt(name);
    if (!f) { t_skip("win32u export not available (emulator or missing export)"); return; }
    static const DWORD vals[] = { 0xDEADBEEF, 0xFFFFFFFF };
    for (unsigned v = 0; v < 2; v++)
        for (unsigned i = 0; i < 0x400 / 4; i++) {
            DWORD *p = x_edge(0x400); p[i] = vals[v];
            X_ST(f(p), dev_status(dev, i, zst, devst), "%s dword %u = garbage %u", name, i, v);
        }
}

static void do_gdi_repeat(const char *name, LONG pst, LONG zst, LONG ffst, int gdistub) {
    x_fn g = x_gdi(name), f = x_nt(name);
    if (!g || !f) { t_skip("gdi32 or win32u export not available"); return; }
    X_ST(g(NULL), pst, "gdi32 %s(NULL)", name);
    X_ST(g(x_edge(0x400)), zst, "gdi32 %s(zero block)", name);
    DWORD *p;
    if (gdistub && !slow_fill(name)) {   /* OpenResource's user-mode wrapper sizes an allocation from the block and answers differently */
        p = x_edge(0x400); for (int i = 0; i < 0x100; i++) p[i] = 0xFFFFFFFF;
        X_ST(g(p), ffst, "gdi32 %s(block of 0xFFFFFFFF)", name);
    }
    /* repeat: same status each time, no handle or GDI object appears */
    DWORD h0 = 0, h1 = 0;
    GetProcessHandleCount(GetCurrentProcess(), &h0);
    DWORD gdi0 = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS), usr0 = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    int bad = 0;
    for (int i = 0; i < 64; i++) {
        if (f(NULL) != pst) bad++;
        if (f(x_edge(0x400)) != zst) bad++;
        p = x_edge(0x400); p[0] = 0xDEADBEEF;
        if (f(p) != zst) bad++;
    }
    CHECKF(bad == 0, "%s: %d of 192 repeated calls changed status", name, bad);
    GetProcessHandleCount(GetCurrentProcess(), &h1);
    CHECKF(h1 == h0, "%s: process handle count changed over 192 calls", name);
    EXPECT_EQ(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS), gdi0);
    EXPECT_EQ(GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS), usr0);
}

/* status a garbage dword at a deviating position gives: NO_MEMORY for the count fields, INVALID_PARAMETER for the
 * size and pointer fields of the NT-handle thunk */
static LONG XS_NO_MEMORY_OR(const char *name, LONG zst) {
    return !strncmp(name, "Submit", 6) ? XS_NO_MEMORY : XS_INVALID_PARAMETER;
}
#define GEN(name, pst, zst, ffst, deadst, size, dev, devst_is_m, count, gdistub) \
TEST(x_##name##_stub_well_formed) { \
    x_fn nt = x_nt(#name); \
    if (!nt) { t_skip("win32u export not available (emulator or missing export)"); return; } \
    CHECK(k_stub_valid((FARPROC)nt)); \
    CHECK(k_stub_number((FARPROC)nt) >= 0x1000); \
    x_fn g = x_gdi(#name); \
    CHECK(g != NULL); \
    if (gdistub && g) { CHECK(k_stub_valid((FARPROC)g)); EXPECT_EQ(k_stub_number((FARPROC)g), k_stub_number((FARPROC)nt)); } \
} \
TEST(x_##name##_null_and_wild_pointers) { do_null_wild(#name, pst); } \
TEST(x_##name##_short_buffer_boundary) { do_short(#name, pst, zst, size); } \
TEST(x_##name##_filled_block_status) { do_filled(#name, zst, ffst, deadst, count); } \
TEST(x_##name##_garbage_dword_matrix) { do_dwords(#name, zst, dev, XS_NO_MEMORY_OR(#name, zst)); } \
TEST(x_##name##_gdi32_route_and_repeat) { do_gdi_repeat(#name, pst, zst, ffst, gdistub); }

API_LIST(GEN)
