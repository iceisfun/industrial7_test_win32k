/* x_d3dkmt_badhandle_matrix_a / swapchain: swap-chain thunks with handles that cannot name a live swap chain.
 *
 * Measures NtGdiDdDDICreateSwapChain, OpenSwapChain, AcquireSwapChain, ReleaseSwapChain, AbandonSwapChain,
 * AddSurfaceToSwapChain, RemoveSurfaceFromSwapChain, UnOrderedPresentSwapChain and GetSetSwapChainMetadata. Swap
 * chains need a display adapter to create, so these tests only reach the argument and handle validation, which
 * runs before anything adapter-specific:
 *   - Acquire, Release, Abandon and GetSetMetadata look the swap-chain handle up first: any value, including zero
 *     and wild ones, is STATUS_INVALID_HANDLE; the exception is a pointer-sized field the kernel checks first
 *     (Acquire +0x40, Release +0x30), which is STATUS_INVALID_PARAMETER for a non-NULL user address that is not readable;
 *   - the argument block must be readable for the kernel's size: Acquire 0x68 bytes, Release 0x40, Abandon 8
 *     (although the handle is 4), GetSetMetadata 0x28; one dword fewer is STATUS_INVALID_PARAMETER;
 *   - Add/Remove/UnOrderedPresent keep the handle at +8: zero there is STATUS_INVALID_PARAMETER and anything else
 *     is STATUS_INVALID_HANDLE, whatever the other fields hold;
 *   - Create and Open fail with STATUS_INVALID_PARAMETER for a zero block (and OpenSwapChain with
 *     STATUS_NO_MEMORY for a block of garbage dwords, which makes a huge size field).
 *
 * Determinism: only status names; the garbage handle values are fixed constants (no live handle is ever
 * used for them).
 */
#include "x.h"

/* NOTE: the value 4 is not here: it names an existing object of another kind (OBJECT_TYPE_MISMATCH), not an invalid handle */
static const DWORD bad_handles[] = { 0, 1, 2, 3, 0x7FFFFFFF, 0x80000000, 0xC0000000, 0xDEADBEEF, 0xFFFFFFFF };

static void handle_first(const char *name, unsigned size, unsigned ptr_off) {
    x_fn f = x_nt(name);
    if (!f) { t_skip("win32u export not available"); return; }
    for (size_t i = 0; i < XARR(bad_handles); i++) {
        DWORD *a = x_edge(size);
        a[0] = bad_handles[i];
        X_ST(f(a), XS_INVALID_HANDLE, "%s handle index %zu", name, i);
    }
    if (ptr_off) {
        static const ULONGLONG ptrs[] = { 1, 0xDEADBEEFull, 0xFFFFFFFFFFFFFFFFull };
        for (size_t i = 0; i < XARR(ptrs); i++) {
            DWORD *a = x_edge(size);
            *(ULONGLONG *)((char *)a + ptr_off) = ptrs[i];
            X_ST(f(a), XS_INVALID_PARAMETER, "%s pointer field %zu", name, i);
        }
        /* a kernel address in the field is not probed: the handle lookup fails first */
        DWORD *a = x_edge(size);
        *(ULONGLONG *)((char *)a + ptr_off) = 0xFFFF800000000000ull;
        X_ST(f(a), XS_INVALID_HANDLE, "%s kernel address in the pointer field", name);
    }
}
TEST(x_swapchain_acquire_bad_handles_invalid_handle) { handle_first("AcquireSwapChain", 0x68, 0x40); }
TEST(x_swapchain_release_bad_handles_invalid_handle) { handle_first("ReleaseSwapChain", 0x40, 0x30); }
TEST(x_swapchain_abandon_bad_handles_invalid_handle) { handle_first("AbandonSwapChain", 8, 0); }
TEST(x_swapchain_getset_metadata_bad_handles_invalid_handle) { handle_first("GetSetSwapChainMetadata", 0x28, 0); }

TEST(x_swapchain_abandon_reads_eight_bytes) {
    XREQ(f, "AbandonSwapChain");
    X_ST(f(x_edge(4)), XS_INVALID_PARAMETER, "4 readable bytes");
    X_ST(f(x_edge(8)), XS_INVALID_HANDLE, "8 readable bytes");
    X_ST(f(x_edge(12)), XS_INVALID_HANDLE, "12 readable bytes");
}
TEST(x_swapchain_getset_metadata_reads_0x28_bytes) {
    XREQ(f, "GetSetSwapChainMetadata");
    for (unsigned n = 4; n < 0x28; n += 4) X_ST(f(x_edge(n)), XS_INVALID_PARAMETER, "%u readable bytes", n);
    X_ST(f(x_edge(0x28)), XS_INVALID_HANDLE, "0x28 readable bytes");
}
TEST(x_swapchain_release_reads_0x40_bytes) {
    XREQ(f, "ReleaseSwapChain");
    for (unsigned n = 4; n < 0x40; n += 4) X_ST(f(x_edge(n)), XS_INVALID_PARAMETER, "%u readable bytes", n);
    X_ST(f(x_edge(0x40)), XS_INVALID_HANDLE, "0x40 readable bytes");
}
TEST(x_swapchain_acquire_reads_0x68_bytes) {
    XREQ(f, "AcquireSwapChain");
    for (unsigned n = 4; n < 0x68; n += 4) X_ST(f(x_edge(n)), XS_INVALID_PARAMETER, "%u readable bytes", n);
    X_ST(f(x_edge(0x68)), XS_INVALID_HANDLE, "0x68 readable bytes");
}
TEST(x_swapchain_handle_lookup_ignores_other_fields) {
    static const char *const names[] = { "AcquireSwapChain", "ReleaseSwapChain", "AbandonSwapChain", "GetSetSwapChainMetadata" };
    static const unsigned sizes[] = { 0x68, 0x40, 8, 0x28 };
    static const unsigned skip[] = { 0x40, 0x30, 0, 0 };
    for (size_t k = 0; k < XARR(names); k++) {
        x_fn f = x_nt(names[k]);
        if (!f) continue;
        for (unsigned off = 4; off < sizes[k]; off += 4) {
            if (skip[k] && off >= skip[k] && off < skip[k] + 8) continue;
            DWORD *a = x_edge(sizes[k]);
            a[off / 4] = 0xFFFFFFFF;
            X_ST(f(a), XS_INVALID_HANDLE, "%s with only the dword at +0x%x set", names[k], off);
        }
    }
}

static void handle_at_8(const char *name) {
    x_fn f = x_nt(name);
    if (!f) { t_skip("win32u export not available"); return; }
    DWORD *a = x_edge(0x100);
    X_ST(f(a), XS_INVALID_PARAMETER, "%s with a zero handle field", name);
    for (size_t i = 1; i < XARR(bad_handles); i++) {
        a = x_edge(0x100);
        a[2] = bad_handles[i];
        X_ST(f(a), XS_INVALID_HANDLE, "%s handle index %zu", name, i);
    }
    /* the other dwords alone never get past the zero-handle check */
    for (unsigned d = 0; d < 0x40; d++) {
        if (d == 2) continue;
        a = x_edge(0x100);
        a[d] = 0xFFFFFFFF;
        X_ST(f(a), XS_INVALID_PARAMETER, "%s with only dword %u set", name, d);
    }
}
TEST(x_swapchain_add_surface_handle_at_offset_8) { handle_at_8("AddSurfaceToSwapChain"); }
TEST(x_swapchain_remove_surface_handle_at_offset_8) { handle_at_8("RemoveSurfaceFromSwapChain"); }
TEST(x_swapchain_unordered_present_handle_at_offset_8) { handle_at_8("UnOrderedPresentSwapChain"); }
TEST(x_swapchain_keyed_mutex_handles_are_not_swap_chains) {
    DWORD hs = 0, hk = 0;
    if (!x_nt("CreateKeyedMutex") || x_km_create(0, &hs, &hk) != XS_SUCCESS) { t_skip("keyed mutex could not be created"); return; }
    static const char *const first[] = { "AcquireSwapChain", "ReleaseSwapChain", "AbandonSwapChain", "GetSetSwapChainMetadata" };
    static const unsigned sizes[] = { 0x68, 0x40, 8, 0x28 };
    for (size_t k = 0; k < XARR(first); k++) {
        x_fn f = x_nt(first[k]);
        if (!f) continue;
        DWORD *a = x_edge(sizes[k]); a[0] = hk;
        X_ST(f(a), XS_INVALID_HANDLE, "%s with a keyed mutex handle", first[k]);
        a = x_edge(sizes[k]); a[0] = hs;
        X_ST(f(a), XS_INVALID_HANDLE, "%s with a keyed mutex shared handle", first[k]);
    }
    static const char *const at8[] = { "AddSurfaceToSwapChain", "RemoveSurfaceFromSwapChain", "UnOrderedPresentSwapChain" };
    for (size_t k = 0; k < XARR(at8); k++) {
        x_fn f = x_nt(at8[k]);
        if (!f) continue;
        DWORD *a = x_edge(0x100); a[2] = hk;
        X_ST(f(a), XS_INVALID_HANDLE, "%s with a keyed mutex handle", at8[k]);
    }
    x_km_destroy(hk);
}
TEST(x_swapchain_create_open_zero_blocks_invalid_parameter) {
    XREQ(c, "CreateSwapChain");
    XREQ(o, "OpenSwapChain");
    for (unsigned n = 0x20; n <= 0x400; n += 0x20) {
        X_ST(c(x_edge(n)), XS_INVALID_PARAMETER, "create, zero block of 0x%x bytes", n);
        X_ST(o(x_edge(n)), XS_INVALID_PARAMETER, "open, zero block of 0x%x bytes", n);
    }
}
TEST(x_swapchain_create_garbage_fields_never_create_anything) {
    XREQ(c, "CreateSwapChain");
    static const DWORD vals[] = { 1, 2, 0x7FFFFFFF, 0x80000000, 0xDEADBEEF, 0xFFFFFFFF };
    for (unsigned d = 0; d < 0x40; d++)
        for (size_t v = 0; v < XARR(vals); v++) {
            DWORD *a = x_edge(0x100); a[d] = vals[v];
            LONG r = c(a);
            CHECKF(r != XS_SUCCESS, "dword %u value index %zu created a swap chain", d, v);
            X_ST(r, XS_INVALID_PARAMETER, "create with dword %u value index %zu", d, v);
        }
}
TEST(x_swapchain_open_garbage_fields) {
    XREQ(o, "OpenSwapChain");
    static const DWORD vals[] = { 1, 0xDEADBEEF, 0xFFFFFFFF };
    for (unsigned d = 0; d < 0x40; d++)
        for (size_t v = 0; v < XARR(vals); v++) {
            DWORD *a = x_edge(0x100); a[d] = vals[v];
            LONG r = o(a);
            CHECKF(r != XS_SUCCESS, "dword %u value index %zu opened a swap chain", d, v);
        }
}
TEST(x_swapchain_open_garbage_block_no_memory) {
    XREQ(o, "OpenSwapChain");
    DWORD *a = x_edge(0x100);
    for (int i = 0; i < 0x40; i++) a[i] = 0xDEADBEEF;
    X_ST(o(a), XS_NO_MEMORY, "block of 0xDEADBEEF");
    a = x_edge(0x100);
    for (int i = 0; i < 0x40; i++) a[i] = 0xFFFFFFFF;
    X_ST(o(a), XS_NO_MEMORY, "block of 0xFFFFFFFF");
}
