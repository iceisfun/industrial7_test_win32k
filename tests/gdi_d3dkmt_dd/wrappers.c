/* User-mode D3DKMT wrappers that live in gdi32.dll itself (not win32u forwarders). Their pre-kernel validation and
 * argument rewriting is fully visible in the 19041 gdi32 analysis:
 *   D3DKMTCreateSynchronizationObject: NULL or Info.Type not in {1,2} -> 0xC000000D without a syscall; copies a result handle
 *       out (struct +0x48) only when the kernel returns success.
 *   D3DKMTWaitForSynchronizationObject / D3DKMTSignalSynchronizationObject: NULL -> 0xC000000D; ObjectCount clamped to 32 for the copy.
 *   D3DKMTOpenResource: NULL -> 0xC000000D; temporarily swaps the allocation-info pointer (+0x10), restores it on return.
 *   D3DKMTCreateAllocation: temporarily swaps pAllocationInfo (+0x30) for a 0x60-stride copy, restores it on return.
 *   D3DKMTSetVidPnSourceOwner/1, D3DKMTPin/UnpinDirectFlipResources: repackage into the V2 / NtDxgk* argument and call the syscall.
 *   D3DKMTSetDisplayPrivateDriverFormat: always STATUS_PROCEDURE_NOT_FOUND (0xC000007A).
 */
#include "d.h"
#include <stdlib.h>

typedef LONG (WINAPI *w1_fn)(void *);
typedef LONG (WINAPI *w3_fn)(ULONG_PTR, ULONG_PTR, ULONG_PTR);
static w1_fn gw(const char *n) { return t_is_wine() ? NULL : (w1_fn)t_proc_in("gdi32.dll", n); }
#define GREQ(var, name) w1_fn var = gw(name); if (!var) { t_skip("gdi32!" name " unavailable"); return; }

/* ---- CreateSynchronizationObject ---- */
TEST(gdi_d3dkmt_dd_wrap_createsync_null_invalid_parameter) {
    GREQ(f, "D3DKMTCreateSynchronizationObject");
    CHECK(f(NULL) == ST_INVALID_PARAMETER);
}
TEST(gdi_d3dkmt_dd_wrap_createsync_bad_type_invalid_parameter) {
    GREQ(f, "D3DKMTCreateSynchronizationObject");
    static const ULONG types[] = { 0, 3, 4, 5, 100, 0x7FFFFFFF, 0xFFFFFFFF };
    for (size_t i = 0; i < sizeof types / sizeof types[0]; i++) {
        ULONG a[32]; memset(a, 0, sizeof a);
        a[1] = types[i]; a[0x12] = 0xCAFEF00D;
        LONG st = f(a);
        CHECKF(st == ST_INVALID_PARAMETER, "Type %lu -> 0x%lx", (unsigned long)types[i], (unsigned long)st);
        CHECK(a[0x12] == 0xCAFEF00D);
    }
}
TEST(gdi_d3dkmt_dd_wrap_createsync_bad_type_never_reaches_kernel) {
    /* a user-mode rejection must not convert the calling thread to a GUI thread */
    GREQ(f, "D3DKMTCreateSynchronizationObject");
    ULONG a[32]; memset(a, 0, sizeof a); a[1] = 9;
    void *b = NULL, *c = NULL;
    k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &b);
    f(a);
    k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &c);
    CHECK(b == c);
}
TEST(gdi_d3dkmt_dd_wrap_createsync_valid_type_invalid_device) {
    GREQ(f, "D3DKMTCreateSynchronizationObject");
    for (ULONG type = 1; type <= 2; type++) {
        ULONG a[32]; memset(a, 0, sizeof a);
        a[0] = 0xDEADBEEF; a[1] = type; a[0x12] = 0xCAFEF00D;
        LONG st = f(a);
        CHECKF(!NTOK(st), "Type %lu with bogus hDevice accepted", (unsigned long)type);
        CHECKF(a[0x12] == 0xCAFEF00D, "hSyncObject written despite failure (0x%lx)", (unsigned long)a[0x12]);
        NOTE_ST("CreateSynchronizationObject(bogus device)", st);
    }
}
TEST(gdi_d3dkmt_dd_wrap_createsync_does_not_modify_other_fields) {
    GREQ(f, "D3DKMTCreateSynchronizationObject");
    ULONG a[32], b[32]; memset(a, 0, sizeof a);
    a[0] = 0xDEADBEEF; a[1] = 1; a[2] = 3; a[0x12] = 0x1234;
    memcpy(b, a, sizeof a);
    f(a);
    CHECK(memcmp(a, b, sizeof a) == 0);
}
/* ---- Wait/Signal ---- */
TEST(gdi_d3dkmt_dd_wrap_wait_null_invalid_parameter) {
    GREQ(f, "D3DKMTWaitForSynchronizationObject");
    CHECK(f(NULL) == ST_INVALID_PARAMETER);
}
TEST(gdi_d3dkmt_dd_wrap_signal_null_invalid_parameter) {
    GREQ(f, "D3DKMTSignalSynchronizationObject");
    CHECK(f(NULL) == ST_INVALID_PARAMETER);
}
TEST(gdi_d3dkmt_dd_wrap_wait_bogus_context_fails_fast) {
    GREQ(f, "D3DKMTWaitForSynchronizationObject");
    ULONG a[64]; memset(a, 0, sizeof a);
    a[0] = 0xDEADBEEF; a[1] = 1; a[2] = 0xDEADBEEF;
    DWORD t0 = GetTickCount();
    LONG st = f(a);
    CHECK(!NTOK(st));
    CHECK(GetTickCount() - t0 < 2000);
    NOTE_ST("WaitForSynchronizationObject(bogus context)", st);
}
TEST(gdi_d3dkmt_dd_wrap_signal_bogus_context_fails) {
    GREQ(f, "D3DKMTSignalSynchronizationObject");
    ULONG a[64]; memset(a, 0, sizeof a);
    a[0] = 0xDEADBEEF; a[1] = 1; a[2] = 0xDEADBEEF;
    LONG st = f(a);
    CHECK(!NTOK(st));
    NOTE_ST("SignalSynchronizationObject(bogus context)", st);
}
TEST(gdi_d3dkmt_dd_wrap_wait_count_above_32_is_clamped_in_copy) {
    /* the wrapper memcpy()s min(count,32) handles from +8 into a fixed 128-byte slot; a count of 0xFFFFFFFF must not overread the caller's buffer */
    GREQ(f, "D3DKMTWaitForSynchronizationObject");
    unsigned char *page = VirtualAlloc(NULL, 8192, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    REQUIRE(page);
    DWORD old;
    VirtualProtect(page + 4096, 4096, PAGE_NOACCESS, &old);
    ULONG *a = (ULONG *)(page + 4096 - 8 - 32 * 4);       /* exactly 8 + 32*4 readable bytes before the guard page */
    memset(a, 0, 8 + 32 * 4);
    a[0] = 0xDEADBEEF; a[1] = 0xFFFFFFFF;
    LONG st = f(a);
    CHECK(!NTOK(st));
    VirtualFree(page, 0, MEM_RELEASE);
}
TEST(gdi_d3dkmt_dd_wrap_signal_count_above_32_is_clamped_in_copy) {
    GREQ(f, "D3DKMTSignalSynchronizationObject");
    unsigned char *page = VirtualAlloc(NULL, 8192, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    REQUIRE(page);
    DWORD old;
    VirtualProtect(page + 4096, 4096, PAGE_NOACCESS, &old);
    /* reads +0 .. +0x88+4: allocate 0x8C readable bytes right below the guard page */
    ULONG *a = (ULONG *)(page + 4096 - 0x8C);
    memset(a, 0, 0x8C);
    a[0] = 0xDEADBEEF; a[1] = 0xFFFFFFFF;
    LONG st = f(a);
    CHECK(!NTOK(st));
    VirtualFree(page, 0, MEM_RELEASE);
}
/* ---- OpenResource ---- */
TEST(gdi_d3dkmt_dd_wrap_openresource_null_invalid_parameter) {
    GREQ(f, "D3DKMTOpenResource");
    CHECK(f(NULL) == ST_INVALID_PARAMETER);
}
TEST(gdi_d3dkmt_dd_wrap_openresource_restores_info_pointer) {
    GREQ(f, "D3DKMTOpenResource");
    unsigned char arr[0x18 * 4]; memset(arr, 0, sizeof arr);
    for (ULONG n = 0; n <= 3; n += 3) {
        unsigned char a[0x80]; memset(a, 0, sizeof a);
        *(ULONG *)a = 0xDEADBEEF;               /* hDevice */
        *(ULONG *)(a + 8) = n;                    /* NumAllocations */
        *(void **)(a + 0x10) = arr;              /* pOpenAllocationInfo */
        LONG st = f(a);
        CHECKF(!NTOK(st), "bogus device accepted (n=%lu)", (unsigned long)n);
        CHECKF(*(void **)(a + 0x10) == arr, "pOpenAllocationInfo not restored (n=%lu)", (unsigned long)n);
    }
}
/* ---- CreateAllocation ---- */
TEST(gdi_d3dkmt_dd_wrap_createalloc_restores_info_pointer_zero_allocs) {
    GREQ(f, "D3DKMTCreateAllocation");
    unsigned char a[0x80]; memset(a, 0, sizeof a);
    void *sentinel = (void *)(ULONG_PTR)0x123456780;
    *(ULONG *)a = 0xDEADBEEF;                    /* hDevice */
    *(ULONG *)(a + 0x2C) = 0;                    /* NumAllocations */
    *(void **)(a + 0x30) = sentinel;
    LONG st = f(a);
    CHECK(!NTOK(st));
    CHECKF(*(void **)(a + 0x30) == sentinel, "pAllocationInfo = %p", *(void **)(a + 0x30));
}
TEST(gdi_d3dkmt_dd_wrap_createalloc_restores_info_pointer_one_alloc) {
    GREQ(f, "D3DKMTCreateAllocation");
    unsigned char info[0x28]; memset(info, 0, sizeof info);
    unsigned char a[0x80]; memset(a, 0, sizeof a);
    *(ULONG *)a = 0xDEADBEEF;
    *(ULONG *)(a + 0x2C) = 1;
    *(void **)(a + 0x30) = info;
    CHECK(!NTOK(f(a)));
    CHECK(*(void **)(a + 0x30) == info);
}
TEST(gdi_d3dkmt_dd_wrap_createalloc_restores_info_pointer_many_allocs) {
    GREQ(f, "D3DKMTCreateAllocation");
    unsigned char info[0x28 * 5]; memset(info, 0, sizeof info);
    unsigned char a[0x80]; memset(a, 0, sizeof a);
    *(ULONG *)a = 0xDEADBEEF;
    *(ULONG *)(a + 0x2C) = 5;                    /* >= 2 takes the LocalAlloc(count * 0x60) path */
    *(void **)(a + 0x30) = info;
    CHECK(!NTOK(f(a)));
    CHECK(*(void **)(a + 0x30) == info);
}
TEST(gdi_d3dkmt_dd_wrap_createalloc_failure_leaves_infos_untouched) {
    GREQ(f, "D3DKMTCreateAllocation");
    unsigned char info[0x28 * 3]; unsigned char ref[sizeof info];
    for (size_t i = 0; i < sizeof info; i++) info[i] = (unsigned char)(0x40 + i);
    /* pSystemMem/pPrivateDriverData slots hold junk pointers; with a bogus device the kernel rejects before touching them */
    for (int k = 0; k < 3; k++) { *(void **)(info + k * 0x28 + 8) = NULL; *(void **)(info + k * 0x28 + 0x10) = NULL; *(ULONG *)(info + k * 0x28 + 0x18) = 0; }
    memcpy(ref, info, sizeof info);
    unsigned char a[0x80]; memset(a, 0, sizeof a);
    *(ULONG *)a = 0xDEADBEEF;
    *(ULONG *)(a + 0x2C) = 3;
    *(void **)(a + 0x30) = info;
    LONG st = f(a);
    CHECK(!NTOK(st));
 /* results are copied back only when the syscall succeeded */
    CHECK(memcmp(info, ref, sizeof info) == 0);
}
TEST(gdi_d3dkmt_dd_wrap_createalloc2_forwards_directly) {
    /* D3DKMTCreateAllocation2 is a plain forwarder to NtGdiDdDDICreateAllocation (no repack) */
    w1_fn w = gw("D3DKMTCreateAllocation2");
    dk_fn1 k = dk_nt("NtGdiDdDDICreateAllocation");
    if (!w || !k) { t_skip("exports unavailable"); return; }
    CHECK((void *)w == (void *)k);
}
/* ---- SetVidPnSourceOwner & friends: wrapper reads the caller's struct, never writes it ---- */
TEST(gdi_d3dkmt_dd_wrap_setvidpnsourceowner_bogus_device_rejected_input_unchanged) {
    GREQ(f, "D3DKMTSetVidPnSourceOwner");
    unsigned char a[0x20], ref[0x20]; memset(a, 0, sizeof a);
    *(ULONG *)a = 0xDEADBEEF;                   /* hDevice; pType/pVidPnSourceId NULL with VidPnSourceCount 0 */
    memcpy(ref, a, sizeof a);
    LONG st = f(a);
    CHECK(!NTOK(st));
    CHECK(memcmp(a, ref, sizeof a) == 0);
    NOTE_ST("SetVidPnSourceOwner(bogus device)", st);
}
TEST(gdi_d3dkmt_dd_wrap_setvidpnsourceowner_zero_device_rejected) {
    GREQ(f, "D3DKMTSetVidPnSourceOwner");
    unsigned char a[0x20]; memset(a, 0, sizeof a);
    CHECK(!NTOK(f(a)));
}
TEST(gdi_d3dkmt_dd_wrap_setvidpnsourceowner1_bogus_device_rejected) {
    GREQ(f, "D3DKMTSetVidPnSourceOwner1");
    unsigned char a[0x30], ref[0x30]; memset(a, 0, sizeof a);
    *(ULONG *)a = 0xDEADBEEF;
    memcpy(ref, a, sizeof a);
    CHECK(!NTOK(f(a)));
    CHECK(memcmp(a, ref, sizeof a) == 0);
}
TEST(gdi_d3dkmt_dd_wrap_setvidpnsourceowner2_direct_bogus_rejected) {
    w1_fn f = gw("D3DKMTSetVidPnSourceOwner2");
    if (!f) { t_skip("export unavailable"); return; }
    unsigned char a[0x40]; memset(a, 0, sizeof a);
    *(ULONG *)a = 0xDEADBEEF;
    CHECK(!NTOK(f(a)));
}
TEST(gdi_d3dkmt_dd_wrap_pin_direct_flip_bogus_device_rejected) {
    GREQ(f, "D3DKMTPinDirectFlipResources");
    unsigned char a[0x20], ref[0x20]; memset(a, 0, sizeof a);
    *(ULONG *)a = 0xDEADBEEF;
    memcpy(ref, a, sizeof a);
    LONG st = f(a);
    CHECK(!NTOK(st));
    CHECK(memcmp(a, ref, sizeof a) == 0);
}
TEST(gdi_d3dkmt_dd_wrap_unpin_direct_flip_bogus_device_rejected) {
    GREQ(f, "D3DKMTUnpinDirectFlipResources");
    unsigned char a[0x20], ref[0x20]; memset(a, 0, sizeof a);
    *(ULONG *)a = 0xDEADBEEF;
    memcpy(ref, a, sizeof a);
    CHECK(!NTOK(f(a)));
    CHECK(memcmp(a, ref, sizeof a) == 0);
}
TEST(gdi_d3dkmt_dd_wrap_pin_resources_direct_forward_rejects_zero) {
    w1_fn f = gw("D3DKMTPinResources");
    if (!f) { t_skip("export unavailable"); return; }
    unsigned char a[0x30]; memset(a, 0, sizeof a);
    CHECK(!NTOK(f(a)));
}
/* ---- SetDisplayPrivateDriverFormat ---- */
TEST(gdi_d3dkmt_dd_wrap_setdisplayprivatedriverformat_procedure_not_found) {
    w3_fn f = (w3_fn)gw("D3DKMTSetDisplayPrivateDriverFormat");
    if (!f) { t_skip("gdi32!D3DKMTSetDisplayPrivateDriverFormat unavailable"); return; }
    CHECK(f(0, 0, 0) == (LONG)ST_PROC_NOT_FOUND);
    CHECK(f(1, 2, 3) == (LONG)ST_PROC_NOT_FOUND);
}
TEST(gdi_d3dkmt_dd_wrap_setdisplayprivatedriverformat_not_a_syscall) {
    w3_fn f = (w3_fn)gw("D3DKMTSetDisplayPrivateDriverFormat");
    if (!f) { t_skip("export unavailable"); return; }
    void *b = NULL, *c = NULL;
    k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &b);
    f(0, 0, 0);
    k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &c);
    CHECK(b == c);
}
TEST(gdi_d3dkmt_dd_wrap_setdisplayprivatedriverformat_clears_last_error) {
    w3_fn f = (w3_fn)gw("D3DKMTSetDisplayPrivateDriverFormat");
    if (!f) { t_skip("export unavailable"); return; }
    /* The wrapper does not leave the caller's last-error value alone: it resets it to 0 (ERROR_SUCCESS) while it
     * returns STATUS_PROCEDURE_NOT_FOUND. */
    SetLastError(0x5151);
    f(0, 0, 0);
    CHECKF(GetLastError() == 0, "last error 0x%lx", (unsigned long)GetLastError());
}
/* ---- Wrapper status vs raw syscall status ---- */
TEST(gdi_d3dkmt_dd_wrap_openadapterfromhdc_bogus_matches_syscall) {
    GREQ(f, "D3DKMTOpenAdapterFromHdc");
    DKREQ(k, "NtGdiDdDDIOpenAdapterFromHdc");
    dk_openhdc a = { (HDC)(ULONG_PTR)0x1234 }, b = { (HDC)(ULONG_PTR)0x1234 };
    CHECK(f(&a) == k(&b));
}
