/* x_d3dkmt_badhandle_matrix_a / sync: synchronization-object thunks without a device.
 *
 * Measures NtGdiDdDDICreateSynchronizationObject (+ the gdi32-only ...2 variant), OpenSynchronizationObject,
 * DestroySynchronizationObject, SignalSynchronizationObject(2), WaitForSynchronizationObject(2),
 * Signal-/WaitForSynchronizationObjectFromCpu and the FromGpu variants with handles that cannot name a live
 * object. What Windows answers: a device or context handle that does not exist is STATUS_INVALID_PARAMETER on
 * these thunks (not STATUS_INVALID_HANDLE), for every object type and for any object count up to and past
 * the 32-entry legacy limit; a 32-bit count of 0x7FFFFFFF or more is
 * STATUS_NO_MEMORY, checked before the array pointers are read; a created object is never reported when the
 * call fails (the output handle field keeps its value); a keyed mutex handle or shared handle is not a
 * synchronization object for Destroy, and a keyed mutex shared handle is not one for Open.
 *
 * Determinism: only status names; handles never printed.
 */
#include "x.h"

static const DWORD bad_handles[] = { 0, 1, 4, 0x7FFFFFFF, 0x80000000, 0xDEADBEEF, 0xFFFFFFFF };

TEST(x_sync_create_every_type_without_device_invalid_parameter) {
    XREQ(f, "CreateSynchronizationObject");
    for (DWORD type = 0; type <= 8; type++) {
        DWORD *a = x_edge(0x100);
        a[0] = 0xDEADBEEF; a[1] = type; a[0x12] = 0xCAFEF00D;
        X_ST(f(a), XS_INVALID_PARAMETER, "type %lu with a non-existent device", (unsigned long)type);
        CHECKF(a[0x12] == 0xCAFEF00D, "output handle written for type %lu", (unsigned long)type);
    }
}
TEST(x_sync_create_garbage_device_handles_invalid_parameter) {
    XREQ(f, "CreateSynchronizationObject");
    for (size_t i = 0; i < XARR(bad_handles); i++)
        for (DWORD type = 1; type <= 2; type++) {
            DWORD *a = x_edge(0x100);
            a[0] = bad_handles[i]; a[1] = type; a[0x12] = 0xCAFEF00D;
            X_ST(f(a), XS_INVALID_PARAMETER, "device index %zu type %lu", i, (unsigned long)type);
            CHECK(a[0x12] == 0xCAFEF00D);
        }
}
TEST(x_sync_create_with_keyed_mutex_as_device_invalid_parameter) {
    XREQ(f, "CreateSynchronizationObject");
    DWORD hs = 0, hk = 0;
    if (x_km_create(0, &hs, &hk) != XS_SUCCESS) { t_skip("keyed mutex could not be created"); return; }
    DWORD *a = x_edge(0x100);
    a[0] = hk; a[1] = 1;
    X_ST(f(a), XS_INVALID_PARAMETER, "keyed mutex handle as the device");
    a = x_edge(0x100); a[0] = hs; a[1] = 2;
    X_ST(f(a), XS_INVALID_PARAMETER, "keyed mutex shared handle as the device");
    x_km_destroy(hk);
}
TEST(x_sync_create_gdi32_wrapper_rejects_types_in_user_mode) {
    XREQG(g, "CreateSynchronizationObject");
    static const DWORD bad[] = { 0, 3, 4, 5, 100, 0x7FFFFFFF, 0xFFFFFFFF };
    for (size_t i = 0; i < XARR(bad); i++) {
        DWORD *a = x_edge(0x100);
        a[0] = 0xDEADBEEF; a[1] = bad[i]; a[0x12] = 0xCAFEF00D;
        X_ST(g(a), XS_INVALID_PARAMETER, "type index %zu", i);
        CHECK(a[0x12] == 0xCAFEF00D);
    }
    for (DWORD type = 1; type <= 2; type++) {
        DWORD *a = x_edge(0x100);
        a[0] = 0xDEADBEEF; a[1] = type; a[0x12] = 0xCAFEF00D;
        X_ST(g(a), XS_INVALID_PARAMETER, "type %lu reaches the kernel and fails there", (unsigned long)type);
        CHECK(a[0x12] == 0xCAFEF00D);
    }
}
TEST(x_sync_create2_gdi32_only_variant_pointer_matrix) {
    XREQG(g, "CreateSynchronizationObject2");
    X_ST(g(NULL), XS_INVALID_PARAMETER, "NULL");
    X_ST(g((void *)1), XS_INVALID_PARAMETER, "address 1");
    X_ST(g((void *)(ULONG_PTR)0xFFFF800000000000ull), XS_INVALID_PARAMETER, "kernel address");
    if (x_freed()) X_ST(g(x_freed()), XS_INVALID_PARAMETER, "released region");
    X_ST(g(x_edge(4)), XS_INVALID_PARAMETER, "4 readable bytes");
    X_ST(g(x_edge(0x100)), XS_INVALID_PARAMETER, "zero block");
    DWORD *a = x_edge(0x100); a[0] = 0xDEADBEEF; a[1] = 1;
    X_ST(g(a), XS_INVALID_PARAMETER, "type 1, garbage device");
}
TEST(x_sync_signal2_wait2_gdi32_only_variants_pointer_matrix) {
    XREQG(s, "SignalSynchronizationObject2");
    XREQG(w, "WaitForSynchronizationObject2");
    void *wild[] = { NULL, (void *)1, (void *)(ULONG_PTR)0xFFFF800000000000ull, x_freed(), x_edge(4), x_edge(0x100) };
    for (size_t i = 0; i < XARR(wild); i++) {
        if (!wild[i] && i != 0) continue;
        X_ST(s(wild[i]), XS_INVALID_PARAMETER, "signal2 argument index %zu", i);
        X_ST(w(wild[i]), XS_INVALID_PARAMETER, "wait2 argument index %zu", i);
    }
}

TEST(x_sync_open_garbage_shared_handles_invalid_parameter) {
    XREQ(f, "OpenSynchronizationObject");
    for (size_t i = 0; i < XARR(bad_handles); i++) {
        DWORD *a = x_edge(0x100);
        a[0] = bad_handles[i]; a[1] = 0xCAFEF00D;
        X_ST(f(a), XS_INVALID_PARAMETER, "shared handle index %zu", i);
        CHECK(a[1] == 0xCAFEF00D);
    }
}
TEST(x_sync_open_keyed_mutex_shared_handle_invalid_parameter) {
    XREQ(f, "OpenSynchronizationObject");
    DWORD hs = 0, hk = 0;
    if (x_km_create(0, &hs, &hk) != XS_SUCCESS) { t_skip("keyed mutex could not be created"); return; }
    DWORD *a = x_edge(0x100); a[0] = hs;
    X_ST(f(a), XS_INVALID_PARAMETER, "keyed mutex shared handle");
    x_km_destroy(hk);
}
TEST(x_sync_open_destroy_garbage_and_foreign_handles) {
    XREQ(d, "DestroySynchronizationObject");
    for (size_t i = 0; i < XARR(bad_handles); i++) {
        DWORD *a = x_edge(0x100); a[0] = bad_handles[i];
        X_ST(d(a), XS_INVALID_PARAMETER, "destroy handle index %zu", i);
    }
    DWORD hs = 0, hk = 0;
    if (x_km_create(0, &hs, &hk) != XS_SUCCESS) return;
    DWORD *a = x_edge(0x100); a[0] = hk;
    X_ST(d(a), XS_INVALID_PARAMETER, "keyed mutex handle");
    a = x_edge(0x100); a[0] = hs;
    X_ST(d(a), XS_INVALID_PARAMETER, "keyed mutex shared handle");
    X_ST(x_km_acquire(hk, 0, 0, 1), XS_SUCCESS, "mutex untouched by the rejected destroys");
    x_km_release(hk, 0);
    x_km_destroy(hk);
}

static const DWORD counts[] = { 0, 1, 2, 3, 31, 32, 33, 34, 64, 1000 };

TEST(x_sync_signal_and_wait_garbage_context_any_count_invalid_parameter) {
    XREQ(sg, "SignalSynchronizationObject");
    XREQ(wt, "WaitForSynchronizationObject");
    for (size_t i = 0; i < XARR(bad_handles); i++)
        for (size_t c = 0; c < XARR(counts); c++) {
            DWORD *a = x_edge(0x100);
            a[0] = bad_handles[i]; a[1] = counts[c];
            for (int k = 0; k < 32; k++) a[2 + k] = 0xDEADBEE0 + k;
            X_ST(sg(a), XS_INVALID_PARAMETER, "signal context %zu count %zu", i, c);
            a = x_edge(0x100);
            a[0] = bad_handles[i]; a[1] = counts[c];
            for (int k = 0; k < 32; k++) a[2 + k] = 0xDEADBEE0 + k;
            X_ST(wt(a), XS_INVALID_PARAMETER, "wait context %zu count %zu", i, c);
        }
}
TEST(x_sync_signal_and_wait_keyed_mutex_handles_are_not_contexts) {
    XREQ(sg, "SignalSynchronizationObject");
    XREQ(wt, "WaitForSynchronizationObject");
    DWORD hs = 0, hk = 0;
    if (x_km_create(0, &hs, &hk) != XS_SUCCESS) { t_skip("keyed mutex could not be created"); return; }
    DWORD *a = x_edge(0x100); a[0] = 0; a[1] = 1; a[2] = hk;
    X_ST(sg(a), XS_INVALID_PARAMETER, "signal: mutex handle as object, no context");
    a = x_edge(0x100); a[0] = hk; a[1] = 1; a[2] = hk;
    X_ST(sg(a), XS_INVALID_PARAMETER, "signal: mutex handle as context and object");
    a = x_edge(0x100); a[0] = 0; a[1] = 1; a[2] = hk;
    X_ST(wt(a), XS_INVALID_PARAMETER, "wait: mutex handle as object, no context");
    a = x_edge(0x100); a[0] = hk; a[1] = 1; a[2] = hk;
    X_ST(wt(a), XS_INVALID_PARAMETER, "wait: mutex handle as context and object");
    x_km_destroy(hk);
}

typedef struct { DWORD hDevice, ObjectCount; DWORD *pObjectHandleArray; ULONGLONG *pFenceValueArray; DWORD Flags; } x_cpu;

static void cpu_family(const char *name) {
    x_fn f = x_nt(name);
    if (!f) { t_skip("win32u export not available"); return; }
    DWORD hh[64]; ULONGLONG fv[64];
    memset(hh, 0, sizeof hh); memset(fv, 0, sizeof fv);
    for (int k = 0; k < 40; k++) hh[k] = 0xDEADBEE0 + k;
    for (size_t c = 0; c < XARR(counts); c++)
        for (size_t i = 0; i < XARR(bad_handles); i++) {
            x_cpu *a = x_edge(sizeof(x_cpu) + 0x40);
            a->hDevice = bad_handles[i]; a->ObjectCount = counts[c]; a->pObjectHandleArray = hh; a->pFenceValueArray = fv;
            X_ST(f(a), XS_INVALID_PARAMETER, "%s device %zu count %zu with readable arrays", name, i, c);
        }
    x_cpu *a = x_edge(sizeof(x_cpu) + 0x40);
    a->hDevice = 0xDEADBEEF; a->ObjectCount = 1;
    X_ST(f(a), XS_INVALID_PARAMETER, "%s count 1 with NULL arrays", name);
    a = x_edge(sizeof(x_cpu) + 0x40);
    a->hDevice = 0xDEADBEEF; a->ObjectCount = 1; a->pObjectHandleArray = (DWORD *)1; a->pFenceValueArray = (ULONGLONG *)1;
    X_ST(f(a), XS_INVALID_PARAMETER, "%s count 1 with unreadable arrays", name);
    a = x_edge(sizeof(x_cpu) + 0x40);
    a->hDevice = 0xDEADBEEF; a->ObjectCount = 1; a->pObjectHandleArray = (DWORD *)(ULONG_PTR)0xFFFF800000000000ull; a->pFenceValueArray = fv;
    X_ST(f(a), XS_INVALID_PARAMETER, "%s count 1 with a kernel-address handle array", name);
    a = x_edge(sizeof(x_cpu) + 0x40);
    a->hDevice = 0; a->ObjectCount = 1; a->pObjectHandleArray = hh; a->pFenceValueArray = fv; a->Flags = 0xFFFFFFFF;
    X_ST(f(a), XS_INVALID_PARAMETER, "%s with every flag bit set", name);
}
TEST(x_sync_signal_from_cpu_garbage_device_invalid_parameter) { cpu_family("SignalSynchronizationObjectFromCpu"); }
TEST(x_sync_wait_from_cpu_garbage_device_invalid_parameter) { cpu_family("WaitForSynchronizationObjectFromCpu"); }

static void count_family(const char *name, unsigned count_dword) {
    x_fn f = x_nt(name);
    if (!f) { t_skip("win32u export not available"); return; }
    static const DWORD big[] = { 0xDEADBEEF, 0xFFFFFFFF, 0x7FFFFFFF, 0x80000000 };
    for (size_t i = 0; i < XARR(big); i++) {
        DWORD *a = x_edge(0x100); a[count_dword] = big[i];
        X_ST(f(a), XS_NO_MEMORY, "%s count index %zu of 0x7FFFFFFF or more", name, i);
    }
    /* counts of 0x10000000 to 0x40000000 are not allocated: the NULL arrays are rejected instead */
    static const DWORD mid[] = { 0x10000001, 0x20000000, 0x40000000 };
    DWORD *a;
    for (size_t i = 0; i < XARR(mid); i++) {
        a = x_edge(0x100); a[count_dword] = mid[i];
        if (mid[i] == 0x40000000) {
            /* whether an allocation of this size is attempted and fails depends on the commit available at that moment
             * (it differed between runs on the same build), so both outcomes are accepted here and nothing else */
            LONG st = f(a);
            CHECKF(st == XS_INVALID_PARAMETER || st == XS_NO_MEMORY, "%s count 0x40000000: status %08lx", name, (unsigned long)st);
            continue;
        }
        X_ST(f(a), XS_INVALID_PARAMETER, "%s count %zu in the middle range", name, i);
    }
    for (DWORD c = 0; c <= 3; c++) {
        a = x_edge(0x100); a[count_dword] = c;
        X_ST(f(a), XS_INVALID_PARAMETER, "%s small count %lu with NULL arrays", name, (unsigned long)c);
    }
}
TEST(x_sync_signal_from_cpu_huge_count_no_memory) { count_family("SignalSynchronizationObjectFromCpu", 1); }
TEST(x_sync_wait_from_cpu_huge_count_no_memory) { count_family("WaitForSynchronizationObjectFromCpu", 1); }
TEST(x_sync_signal_from_gpu_huge_count_no_memory) { count_family("SignalSynchronizationObjectFromGpu", 1); }
TEST(x_sync_wait_from_gpu_huge_count_no_memory) { count_family("WaitForSynchronizationObjectFromGpu", 1); }
TEST(x_sync_signal_from_gpu2_huge_count_no_memory) {
    count_family("SignalSynchronizationObjectFromGpu2", 0);
    count_family("SignalSynchronizationObjectFromGpu2", 5);
}
TEST(x_sync_gpu_variants_garbage_context_invalid_parameter) {
    XREQ(s, "SignalSynchronizationObjectFromGpu");
    XREQ(w, "WaitForSynchronizationObjectFromGpu");
    DWORD hh[8] = { 0xDEADBEEF }; ULONGLONG fv[8] = { 0 };
    for (size_t i = 0; i < XARR(bad_handles); i++)
        for (DWORD c = 0; c <= 3; c++) {
            struct { DWORD ctx, cnt; DWORD *h; ULONGLONG *fv; DWORD pad[8]; } *a = x_edge(sizeof *a);
            a->ctx = bad_handles[i]; a->cnt = c; a->h = hh; a->fv = fv;
            X_ST(s(a), XS_INVALID_PARAMETER, "signal-from-gpu context %zu count %lu", i, (unsigned long)c);
            a = x_edge(sizeof *a);
            a->ctx = bad_handles[i]; a->cnt = c; a->h = hh; a->fv = fv;
            X_ST(w(a), XS_INVALID_PARAMETER, "wait-from-gpu context %zu count %lu", i, (unsigned long)c);
        }
}
