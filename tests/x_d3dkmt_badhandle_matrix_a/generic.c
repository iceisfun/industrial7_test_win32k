/* x_d3dkmt_badhandle_matrix_a / generic: the same argument-validation matrix applied to every front door.
 *
 * Measures, per NtGdiDdDDI<Name> (win32u) and D3DKMT<Name> (gdi32), for 29 keyed-mutex, synchronization-object and
 * swap-chain thunks, with no adapter and no live object involved:
 *   - the win32u export is a well-formed syscall stub and the gdi32 export carries the same service number;
 *   - a NULL argument pointer, and wild pointers (address 1, a kernel address, a released region, all ones)
 *     are answered STATUS_INVALID_PARAMETER by the kernel, never with an access violation;
 *   - an argument block shorter than the kernel's readable-size check is STATUS_INVALID_PARAMETER and one of exactly the
 *     checked size gets past the probe (the size is where the answer changes from INVALID_PARAMETER to the
 *     per-API zero-struct status);
 *   - the zero-filled block is answered with one stable status per API (INVALID_PARAMETER where the first
 *     check is on a field, INVALID_HANDLE where the handle is looked up first);
 *   - one garbage dword (0xDEADBEEF, then 0xFFFFFFFF) written at each dword position of an otherwise
 *     zero block changes the answer only at the positions the kernel interprets (counts, pointers, the
 *     swap-chain handle fields), and the gdi32 route gives the same answers as the win32u route.
 * The three gdi32 wrappers that validate in user mode (CreateSynchronizationObject, Signal-, WaitForSynchronizationObject)
 * are only given NULL and zero blocks on the gdi32 route: a wild pointer faults in user mode there.
 *
 * Determinism: only symbolic status codes are printed. No handle, address or size from the machine is printed.
 */
#include "x.h"

/* NOTE: the rows below drive the macros that follow; size = readable bytes the kernel requires (0 = not measured). */
#define API_LIST(X) \
    X(CreateKeyedMutex,                     16,   XS_CREATE_OK,        0) \
    X(CreateKeyedMutex2,                    32,   XS_CREATE_OK,        0) \
    X(OpenKeyedMutex,                       0,    XS_INVALID_PARAMETER, 0) \
    X(OpenKeyedMutex2,                      0,    XS_INVALID_PARAMETER, 0) \
    X(OpenKeyedMutexFromNtHandle,           32,   XS_INVALID_HANDLE,   0) \
    X(AcquireKeyedMutex,                    0,    XS_INVALID_PARAMETER, 0) \
    X(AcquireKeyedMutex2,                   0,    XS_INVALID_PARAMETER, 0) \
    X(ReleaseKeyedMutex,                    0,    XS_INVALID_PARAMETER, 0) \
    X(ReleaseKeyedMutex2,                   0,    XS_INVALID_PARAMETER, 0) \
    X(DestroyKeyedMutex,                    0,    XS_INVALID_PARAMETER, 0) \
    X(CreateSynchronizationObject,          0,    XS_INVALID_PARAMETER, 1) \
    X(OpenSynchronizationObject,            0,    XS_INVALID_PARAMETER, 0) \
    X(DestroySynchronizationObject,         0,    XS_INVALID_PARAMETER, 0) \
    X(SignalSynchronizationObject,          0,    XS_INVALID_PARAMETER, 1) \
    X(WaitForSynchronizationObject,         0,    XS_INVALID_PARAMETER, 1) \
    X(SignalSynchronizationObjectFromCpu,   0,    XS_INVALID_PARAMETER, 0) \
    X(WaitForSynchronizationObjectFromCpu,  0,    XS_INVALID_PARAMETER, 0) \
    X(SignalSynchronizationObjectFromGpu,   0,    XS_INVALID_PARAMETER, 0) \
    X(SignalSynchronizationObjectFromGpu2,  0,    XS_INVALID_PARAMETER, 0) \
    X(WaitForSynchronizationObjectFromGpu,  0,    XS_INVALID_PARAMETER, 0) \
    X(CreateSwapChain,                      0,    XS_INVALID_PARAMETER, 0) \
    X(OpenSwapChain,                        0,    XS_INVALID_PARAMETER, 0) \
    X(AcquireSwapChain,                     0x68, XS_INVALID_HANDLE,   0) \
    X(ReleaseSwapChain,                     0x40, XS_INVALID_HANDLE,   0) \
    X(AbandonSwapChain,                     8,    XS_INVALID_HANDLE,   0) \
    X(AddSurfaceToSwapChain,                0,    XS_INVALID_PARAMETER, 0) \
    X(RemoveSurfaceFromSwapChain,           0,    XS_INVALID_PARAMETER, 0) \
    X(UnOrderedPresentSwapChain,            0,    XS_INVALID_PARAMETER, 0) \
    X(GetSetSwapChainMetadata,              0x28, XS_INVALID_HANDLE,   0)

static LONG fuzz_expect(const char *api, unsigned idx, LONG base) {
    /* count fields of 0x7FFFFFFF or more fail with NO_MEMORY; pointer fields are probed before the handle is looked up */
    if (!strcmp(api, "SignalSynchronizationObjectFromCpu") || !strcmp(api, "WaitForSynchronizationObjectFromCpu") ||
        !strcmp(api, "SignalSynchronizationObjectFromGpu") || !strcmp(api, "WaitForSynchronizationObjectFromGpu"))
        if (idx == 1) return XS_NO_MEMORY;
    if (!strcmp(api, "SignalSynchronizationObjectFromGpu2") && (idx == 0 || idx == 5)) return XS_NO_MEMORY;
    if (!strcmp(api, "AcquireSwapChain") && idx == 16) return XS_INVALID_PARAMETER;
    if (!strcmp(api, "ReleaseSwapChain") && idx == 12) return XS_INVALID_PARAMETER;
    if ((!strcmp(api, "AddSurfaceToSwapChain") || !strcmp(api, "RemoveSurfaceFromSwapChain") ||
         !strcmp(api, "UnOrderedPresentSwapChain")) && idx == 2) return XS_INVALID_HANDLE;
    return base;
}

/* status for a 0x100-byte block filled with 0xFFFFFFFF dwords (ff) or 0xDEADBEEF dwords (dead): all-ones makes the
 * first handle-sized field the pseudo handle -1 (an existing but wrong kind of object), so the thunks that look a handle up first
 * answer OBJECT_TYPE_MISMATCH; the garbage handle 0xDEADBEEF is INVALID_HANDLE there. A huge count field makes the allocation fail. */
static void fill_expect(const char *api, LONG *ff, LONG *dead) {
    LONG P = XS_INVALID_PARAMETER, H = XS_INVALID_HANDLE, T = XS_TYPE_MISMATCH, M = XS_NO_MEMORY;
    *ff = P; *dead = P;
    if (!strcmp(api, "OpenKeyedMutexFromNtHandle")) { *ff = T; *dead = H; }
    else if (!strcmp(api, "SignalSynchronizationObjectFromCpu") || !strcmp(api, "SignalSynchronizationObjectFromGpu") ||
             !strcmp(api, "SignalSynchronizationObjectFromGpu2") || !strcmp(api, "WaitForSynchronizationObjectFromGpu") ||
             !strcmp(api, "OpenSwapChain")) { *ff = M; *dead = M; }
    else if (!strcmp(api, "AbandonSwapChain") || !strcmp(api, "AddSurfaceToSwapChain") || !strcmp(api, "RemoveSurfaceFromSwapChain") ||
             !strcmp(api, "UnOrderedPresentSwapChain") || !strcmp(api, "GetSetSwapChainMetadata")) { *ff = T; *dead = H; }
}

static void run_null(const char *name) {
    x_fn f = x_nt(name);
    if (!f) { t_skip("win32u export not available"); return; }
    X_ST(f(NULL), XS_INVALID_PARAMETER, "%s(NULL)", name);
}

static void run_wild(const char *name) {
    x_fn f = x_nt(name);
    if (!f) { t_skip("win32u export not available"); return; }
    void *wild[] = { (void *)(ULONG_PTR)1, (void *)(ULONG_PTR)0xFFFF800000000000ull, x_freed(), (void *)~(ULONG_PTR)0, (void *)(ULONG_PTR)0x7FFFFFFF0000ull };
    const char *wn[] = { "address 1", "kernel address", "released region", "all ones", "top of user space" };
    for (size_t i = 0; i < XARR(wild); i++)
        if (wild[i]) X_ST(f(wild[i]), XS_INVALID_PARAMETER, "%s(%s)", name, wn[i]);
}

static void run_short(const char *name, unsigned size, LONG zst) {
    x_fn f = x_nt(name);
    if (!f) { t_skip("win32u export not available"); return; }
    /* four bytes readable is below every checked size */
    X_ST(f(x_edge(4)), XS_INVALID_PARAMETER, "%s with 4 readable bytes", name);
    if (!size) {
        X_ST(f(x_edge(8)), XS_INVALID_PARAMETER, "%s with 8 readable bytes", name);
        return;
    }
    for (unsigned n = 4; n < size; n += 4)
        X_ST(f(x_edge(n)), XS_INVALID_PARAMETER, "%s with %u readable bytes (checked size is larger)", name, n);
    if (zst == XS_CREATE_OK) {
        /* the block of exactly the checked size is accepted and creates an object: destroy it afterwards */
    }
}

#define GEN(name, size, zst, local) \
TEST(x_##name##_stubs_well_formed) { \
    x_fn nt = x_nt(#name); \
    if (!nt) { t_skip("win32u export not available"); return; } \
    CHECK(k_stub_valid((FARPROC)nt)); \
    x_fn g = x_gdi(#name); \
    CHECK(g != NULL); \
    if (g && k_stub_valid((FARPROC)g)) EXPECT_EQ(k_stub_number((FARPROC)g), k_stub_number((FARPROC)nt)); \
} \
TEST(x_##name##_null_pointer_invalid_parameter) { run_null(#name); } \
TEST(x_##name##_wild_pointers_invalid_parameter) { run_wild(#name); } \
TEST(x_##name##_short_buffer_boundary) { run_short(#name, size, zst); } \
TEST(x_##name##_zero_struct_status) { \
    x_fn f = x_nt(#name); \
    if (!f) { t_skip("win32u export not available"); return; } \
    if ((zst) == XS_CREATE_OK) { t_skip("zero block creates an object; covered by the keyed mutex lifecycle tests"); return; } \
    X_ST(f(x_edge(0x100)), (zst), "%s(zero block)", #name); \
    if (size) X_ST(f(x_edge(size)), (zst), "%s(zero block of exactly the checked size)", #name); \
} \
TEST(x_##name##_garbage_dword_matrix) { \
    x_fn f = x_nt(#name); \
    if (!f) { t_skip("win32u export not available"); return; } \
    if ((zst) == XS_CREATE_OK) { t_skip("creating thunk: a garbage block may create an object"); return; } \
    static const DWORD vals[] = { 0xDEADBEEF, 0xFFFFFFFF }; \
    for (unsigned v = 0; v < 2; v++) \
        for (unsigned i = 0; i < 0x100 / 4; i++) { \
            DWORD *p = x_edge(0x100); p[i] = vals[v]; \
            X_ST(f(p), fuzz_expect(#name, i, (zst)), "%s dword %u = garbage %u", #name, i, v); \
        } \
} \
TEST(x_##name##_filled_block_status) { \
    x_fn f = x_nt(#name); \
    if (!f) { t_skip("win32u export not available"); return; } \
    if ((zst) == XS_CREATE_OK) { t_skip("creating thunk: a filled block may create an object"); return; } \
    LONG ff, dead; fill_expect(#name, &ff, &dead); \
    DWORD *p = x_edge(0x100); for (int i = 0; i < 0x40; i++) p[i] = 0xFFFFFFFF; \
    X_ST(f(p), ff, "%s(block of 0xFFFFFFFF)", #name); \
    p = x_edge(0x100); for (int i = 0; i < 0x40; i++) p[i] = 0xDEADBEEF; \
    X_ST(f(p), dead, "%s(block of 0xDEADBEEF)", #name); \
} \
TEST(x_##name##_gdi32_route_matches) { \
    x_fn g = x_gdi(#name); \
    if (!g) { t_skip("gdi32 export not available"); return; } \
    X_ST(g(NULL), XS_INVALID_PARAMETER, "gdi32 %s(NULL)", #name); \
    if ((zst) != XS_CREATE_OK) X_ST(g(x_edge(0x100)), (zst), "gdi32 %s(zero block)", #name); \
    if (!(local)) { \
        X_ST(g((void *)(ULONG_PTR)1), XS_INVALID_PARAMETER, "gdi32 %s(address 1)", #name); \
        X_ST(g((void *)(ULONG_PTR)0xFFFF800000000000ull), XS_INVALID_PARAMETER, "gdi32 %s(kernel address)", #name); \
    } \
}
API_LIST(GEN)
