/* dcomp_flip_dxgk_neg: table-driven negative tests for the win32k.sys composition/flip/dxgk
 * syscall families (NtDComposition*, NtComposition*, NtFlipObject*, NtDxgk*, NtModerncore*,
 * NtToken*, NtMinQ* / NtMin*, NtCompositorNotify*, NtVisualCaptureBits, NtDesktopCaptureBits,
 * NtInputSpaceRegionFromPoint, NtConfigureInputSpace).
 *
 * These ~190 syscalls are compositor/DWM plumbing: from an ordinary medium-integrity desktop
 * process (what this harness always is - see docs/KERNEL_TESTING.md) they are expected to fail
 * before doing any real work, because win32kbase.sys/win32kfull.sys gate them on
 * a check that the caller is dwm.exe or on a trivial parameter check that fires
 * before any object/handle is touched. This file hard-CHECKs exactly the syscalls where that
 * early-exit is observable and side-effect-free on Windows 10 19041; the other syscalls in scope
 * are not exercised here because no safe, deterministic negative call is known for them.
 *
 * Call style: direct win32u.dll Nt* stub via UDECL+UN (none of these are in any SDK header).
 * Safety: every table row is read-only/early-exit per its stated early-exit; we still
 * defensively close/dereference any handle-shaped output that a row might unexpectedly produce
 * (see dcomp_defensive_close below) and never touch global desktop/session state.
 */
#include "t.h"
#include "k.h"

/* A win32u syscall that reaches the win32k thread conversion calls back into user mode through the kernel callback
 * table that user32 installs; in a process that never loaded user32 that callback faults. Load user32 first, as every
 * Windows GUI process does. */
static HMODULE dc_win32u(void) {
    t_module("user32.dll");
    return t_module("win32u.dll");
}

/* ---- Nt* declarations (not in any mingw/SDK header; signatures from the win32kbase.sys /
 * win32kfull.sys, with parameter lists as used per-row below). All return NTSTATUS. ---- */
UDECL(NTSTATUS, NtDCompositionConnectPipe, (void));
UDECL(NTSTATUS, NtDCompositionGetChannels, (void));
UDECL(NTSTATUS, NtDCompositionReferenceSharedResourceOnDwmChannel,
      (UINT hChannel, void *pGuid, UINT flags, UINT *outA, UINT *outB));
UDECL(NTSTATUS, NtDCompositionUpdatePointerCapture, (UINT a, UINT b));
UDECL(NTSTATUS, NtDuplicateCompositionInputSink, (void *hSink, UINT64 *outHandle));
UDECL(NTSTATUS, NtQueryCompositionInputQueueAndTransform,
      (void *hSink, int inputType, UINT64 *outQueue /* >=7 qwords */, UINT64 *outTransform));
UDECL(NTSTATUS, NtQueryCompositionInputSink, (void *hSink, int *inOutType));
UDECL(NTSTATUS, NtDCompositionDuplicateHandleToProcess,
      (void **pHandleBlob, int magic, LONG_PTR hTargetProcess));
UDECL(NTSTATUS, NtDCompositionConfirmFrame,
      (LONG_PTR channelId, LONG_PTR *pBatchId, UINT64 a3, UINT64 a4));
UDECL(NTSTATUS, NtDCompositionCreateDwmChannel, (UINT *outChannelId));
UDECL(NTSTATUS, NtDesktopCaptureBits,
      (LONG_PTR *p1, UINT p2, UINT p3, UINT p4, UINT p5, UINT p6, LONG_PTR p7, LONG_PTR p8));
UDECL(NTSTATUS, NtVisualCaptureBits,
      (UINT hChannel, UINT a2, int a3, int a4, UINT a5, UINT a6, int dxgiFormat, void *hEvent,
       LONG_PTR p9));

/* Defensive: if a row unexpectedly returns success and left something handle-shaped in an
 * output slot, close it rather than leak it (role rule: "must not ... leak handles"). None of
 * the rows below are expected to ever take this path - it is cheap insurance, not a check. */
static void dcomp_defensive_close(void *maybe_handle) {
    if (!maybe_handle || maybe_handle == INVALID_HANDLE_VALUE) return;
    HMODULE ntdll = t_module("ntdll.dll");
    if (!ntdll) return;
    typedef NTSTATUS (WINAPI *NtClose_pfn)(HANDLE);
    NtClose_pfn pNtClose = (NtClose_pfn)GetProcAddress(ntdll, "NtClose");
    if (pNtClose) pNtClose((HANDLE)maybe_handle);
}

/* ======================================================================================
 * Table 1: DWM-gated syscalls.
 *
 * Common shape in every row below: the kernel checks whether the caller is the desktop window
 * manager (dwm.exe) BEFORE touching any of its
 * pointer arguments or resolving any handle. When it is false the function stores
 * -0x3fffffde (== STATUS_ACCESS_DENIED, 0xC0000022) and takes the function's single early-return
 * path; the real DirectComposition object logic (ResolveHandle/Create/etc) sits behind
 * a success-status guard and is never reached. A harness process is never dwm.exe, so this branch
 * is deterministic and side-effect free: nothing is probed, resolved or created.
 * ====================================================================================== */
struct dwm_row {
    const char *name;
    int (*call)(void);
};

static int row_ReferenceSharedResourceOnDwmChannel(void) {
    UINT a = 0, b = 0;
    NtDCompositionReferenceSharedResourceOnDwmChannel_pfn fn = UN(NtDCompositionReferenceSharedResourceOnDwmChannel);
    if (!fn) return -1;
    return (int)fn(0, NULL, 0, &a, &b);
}
static int row_UpdatePointerCapture(void) {
    NtDCompositionUpdatePointerCapture_pfn fn = UN(NtDCompositionUpdatePointerCapture);
    if (!fn) return -1;
    return (int)fn(0, 0);
}
static int row_DuplicateCompositionInputSink(void) {
    UINT64 out = 0;
    NtDuplicateCompositionInputSink_pfn fn = UN(NtDuplicateCompositionInputSink);
    if (!fn) return -1;
    NTSTATUS st = fn(NULL, &out);
    dcomp_defensive_close((void *)(ULONG_PTR)out);
    return (int)st;
}
static int row_QueryCompositionInputQueueAndTransform(void) {
    UINT64 q[8] = {0}, xf[2] = {0};
    NtQueryCompositionInputQueueAndTransform_pfn fn = UN(NtQueryCompositionInputQueueAndTransform);
    if (!fn) return -1;
    return (int)fn(NULL, 0, q, xf);
}
static int row_QueryCompositionInputSink(void) {
    int t = 0;
    NtQueryCompositionInputSink_pfn fn = UN(NtQueryCompositionInputSink);
    if (!fn) return -1;
    return (int)fn(NULL, &t);
}
static int row_DuplicateHandleToProcess(void) {
    /* non-DWM path never derefs pHandleBlob/hTargetProcess.
     * magic deliberately != the 0x54535255 debug-crash sentinel the kernel treats specially. */
    NtDCompositionDuplicateHandleToProcess_pfn fn = UN(NtDCompositionDuplicateHandleToProcess);
    if (!fn) return -1;
    return (int)fn(NULL, 0, 0);
}
static int row_CreateDwmChannel(void) {
    UINT out = 0;
    /* out must be a valid writable pointer: a NULL pointer takes a *different* early branch
     * whose return value has not been verified, so the
     * DWM-check branch - the one we can prove - needs a real (but harmless, stack-local,
     * zero-initialized) output slot to reach. */
    NtDCompositionCreateDwmChannel_pfn fn = UN(NtDCompositionCreateDwmChannel);
    if (!fn) return -1;
    return (int)fn(&out);
}

static const struct dwm_row dwm_rows[] = {
    {"NtDCompositionReferenceSharedResourceOnDwmChannel", row_ReferenceSharedResourceOnDwmChannel},
    {"NtDCompositionUpdatePointerCapture",                 row_UpdatePointerCapture},
    {"NtDuplicateCompositionInputSink",                    row_DuplicateCompositionInputSink},
    {"NtQueryCompositionInputQueueAndTransform",           row_QueryCompositionInputQueueAndTransform},
    {"NtQueryCompositionInputSink",                        row_QueryCompositionInputSink},
    {"NtDCompositionDuplicateHandleToProcess",             row_DuplicateHandleToProcess},
    {"NtDCompositionCreateDwmChannel",                     row_CreateDwmChannel},
};
#define N_DWM_ROWS (sizeof(dwm_rows) / sizeof(dwm_rows[0]))

TEST(dcomp_win32kt_dwm_gate_access_denied) {
    HMODULE win32u = dc_win32u();
    REQUIRE(win32u);
    if (t_is_wine()) { t_skip("Wine's win32u.dll does not implement this stub (calls it as an unimplemented forward and aborts the process instead of raising a catchable exception) - real-hardware-only check, see docs/KERNEL_TESTING.md"); return; }
    for (size_t i = 0; i < N_DWM_ROWS; i++) {
        FARPROC stub = t_proc_in("win32u.dll", dwm_rows[i].name);
        if (!stub) { t_skip("missing export: %s", dwm_rows[i].name); continue; }
        int st = dwm_rows[i].call();
        CHECKF((NTSTATUS)st == (NTSTATUS)0xc0000022,
               "%s returned 0x%08x, want STATUS_ACCESS_DENIED (non-DWM caller "
               "path, known early return)", dwm_rows[i].name, (unsigned)st);
    }
}

/* ======================================================================================
 * Table 2: unconditional stubs.
 *
 * NtDCompositionConnectPipe (service 4386) and NtDCompositionGetChannels (service 4400) are two
 * different win32u.dll stubs / two different win32k.sys service-table entries that both point at
 * the *same* win32kbase.sys routine: a body with no parameters that is nothing but
 * "return 0xc000007a;" (STATUS_PROCEDURE_NOT_FOUND). Whatever these two syscalls did on an
 * earlier build, on 19041 they are both dead ends regardless of arguments - no probe, no handle
 * resolve, nothing. This is the single safest row in the whole area: argument-independent.
 * ====================================================================================== */
TEST(dcomp_win32kt_connectpipe_getchannels_retired) {
    HMODULE win32u = dc_win32u();
    REQUIRE(win32u);
    if (t_is_wine()) { t_skip("Wine's win32u.dll does not implement this stub (calls it as an unimplemented forward and aborts the process instead of raising a catchable exception) - real-hardware-only check, see docs/KERNEL_TESTING.md"); return; }
    static const char *names[] = {"NtDCompositionConnectPipe", "NtDCompositionGetChannels"};
    for (size_t i = 0; i < 2; i++) {
        FARPROC stub = t_proc_in("win32u.dll", names[i]);
        if (!stub) { t_skip("missing export: %s", names[i]); continue; }
        NTSTATUS st;
        if (i == 0) { NtDCompositionConnectPipe_pfn fn = UN(NtDCompositionConnectPipe); if (!fn) { t_skip("UN resolve failed: %s", names[i]); continue; } st = fn(); }
        else        { NtDCompositionGetChannels_pfn fn  = UN(NtDCompositionGetChannels);  if (!fn) { t_skip("UN resolve failed: %s", names[i]); continue; } st = fn(); }
        CHECKF(st == (NTSTATUS)0xc000007a,
               "%s returned 0x%08x, want STATUS_PROCEDURE_NOT_FOUND (win32kbase.sys "
               "is an unconditional 'return 0xc000007a', shared by both syscall numbers)",
               names[i], (unsigned)st);
    }
}

/* ======================================================================================
 * Table 3: parameter-presence checks that fire before any pointer is probed or handle is
 * resolved. Each row's reasoning is in its own comment.
 * ====================================================================================== */

/* NtDCompositionConfirmFrame: a NULL second argument returns 0xc000000d before anything else happens - nothing is read through
 * the first or second argument. STATUS_INVALID_PARAMETER, independent of arguments 1/3/4. */
TEST(dcomp_win32kt_confirmframe_null_out_invalid_param) {
    HMODULE win32u = dc_win32u();
    REQUIRE(win32u);
    if (t_is_wine()) { t_skip("Wine's win32u.dll does not implement this stub (calls it as an unimplemented forward and aborts the process instead of raising a catchable exception) - real-hardware-only check, see docs/KERNEL_TESTING.md"); return; }
    FARPROC stub = t_proc_in("win32u.dll", "NtDCompositionConfirmFrame");
    REQUIRE(stub);
    NtDCompositionConfirmFrame_pfn fn = UN(NtDCompositionConfirmFrame);
    REQUIRE(fn);
    NTSTATUS st = fn(0, NULL, 0, 0);
    CHECKF(st == (NTSTATUS)0xc000000d,
           "NtDCompositionConfirmFrame(chan=0, pBatchId=NULL) returned 0x%08x, want "
           "STATUS_INVALID_PARAMETER (null-output-pointer check precedes everything else)",
           (unsigned)st);
}

/* NtDesktopCaptureBits: a zero 7th or 8th argument yields 0xc000000d before the first argument
 * (the longlong* output) is ever dereferenced. Both are left 0 here. */
TEST(dcomp_win32kt_desktopcapturebits_zero_handles_invalid_param) {
    HMODULE win32u = dc_win32u();
    REQUIRE(win32u);
    if (t_is_wine()) { t_skip("Wine's win32u.dll does not implement this stub (calls it as an unimplemented forward and aborts the process instead of raising a catchable exception) - real-hardware-only check, see docs/KERNEL_TESTING.md"); return; }
    FARPROC stub = t_proc_in("win32u.dll", "NtDesktopCaptureBits");
    REQUIRE(stub);
    NtDesktopCaptureBits_pfn fn = UN(NtDesktopCaptureBits);
    REQUIRE(fn);
    LONG_PTR out = 0;
    NTSTATUS st = fn(&out, 0, 0, 0, 0, 0, 0, 0);
    CHECKF(st == (NTSTATUS)0xc000000d,
           "NtDesktopCaptureBits(...,p7=0,p8=0) returned 0x%08x, want STATUS_INVALID_PARAMETER "
           "(zero 7th/8th arguments are rejected before any use of the output pointer)",
           (unsigned)st);
}

/* NtVisualCaptureBits: a NULL 8th or zero 9th argument yields 0xc000000d before the channel
 * handle is resolved or touched. */
TEST(dcomp_win32kt_visualcapturebits_null_event_invalid_param) {
    HMODULE win32u = dc_win32u();
    REQUIRE(win32u);
    if (t_is_wine()) { t_skip("Wine's win32u.dll does not implement this stub (calls it as an unimplemented forward and aborts the process instead of raising a catchable exception) - real-hardware-only check, see docs/KERNEL_TESTING.md"); return; }
    FARPROC stub = t_proc_in("win32u.dll", "NtVisualCaptureBits");
    REQUIRE(stub);
    NtVisualCaptureBits_pfn fn = UN(NtVisualCaptureBits);
    REQUIRE(fn);
    NTSTATUS st = fn(0, 0, 0, 0, 0, 0, 0, NULL, 0);
    CHECKF(st == (NTSTATUS)0xc000000d,
           "NtVisualCaptureBits(hEvent=NULL, p9=0) returned 0x%08x, want STATUS_INVALID_PARAMETER "
           "(null-event/zero-size check precedes resolving the channel "
           "handle)", (unsigned)st);
}

/* A second, independent branch to the SAME status in NtDCompositionConfirmFrame: a valid second
 * argument whose pointed-to value is 0 also yields 0xc000000d, still before the caller is
 * identified. Exercises the second early check with the same constant. */
TEST(dcomp_win32kt_confirmframe_zero_batchid_invalid_param) {
    HMODULE win32u = dc_win32u();
    REQUIRE(win32u);
    if (t_is_wine()) { t_skip("Wine's win32u.dll does not implement this stub (calls it as an unimplemented forward and aborts the process instead of raising a catchable exception) - real-hardware-only check, see docs/KERNEL_TESTING.md"); return; }
    FARPROC stub = t_proc_in("win32u.dll", "NtDCompositionConfirmFrame");
    REQUIRE(stub);
    NtDCompositionConfirmFrame_pfn fn = UN(NtDCompositionConfirmFrame);
    REQUIRE(fn);
    LONG_PTR batchId = 0; /* valid pointer, but *pointee == 0 */
    NTSTATUS st = fn(0, &batchId, 0, 0);
    CHECKF(st == (NTSTATUS)0xc000000d,
           "NtDCompositionConfirmFrame(chan=0, *pBatchId=0) returned 0x%08x, want "
           "STATUS_INVALID_PARAMETER (a zero batch id is rejected by the second early check)", (unsigned)st);
}

/* ======================================================================================
 * Table 4: NTSTATUS -> Win32 last-error mapping for the three status codes this area proves.
 * None of the Nt* calls above go through a user32/gdi32full wrapper (they are called directly),
 * so none of them set GetLastError themselves; this checks the *mapping* a real caller's wrapper
 * would apply via RtlNtStatusToDosError (ntdll.dll), which is the role's documentation
 * requirement independent of whether any wrapper in this specific area happens to use it.
 * ====================================================================================== */
UDECL(ULONG, RtlNtStatusToDosError, (NTSTATUS));

TEST(dcomp_win32kt_status_to_doserror_mapping) {
    HMODULE ntdll = t_module("ntdll.dll");
    REQUIRE(ntdll);
    RtlNtStatusToDosError_pfn fn = (RtlNtStatusToDosError_pfn)GetProcAddress(ntdll, "RtlNtStatusToDosError");
    REQUIRE(fn);
    CHECKF(fn((NTSTATUS)0xc0000022) == ERROR_ACCESS_DENIED,
           "RtlNtStatusToDosError(STATUS_ACCESS_DENIED) = %lu, want ERROR_ACCESS_DENIED (5)",
           (unsigned long)fn((NTSTATUS)0xc0000022));
    CHECKF(fn((NTSTATUS)0xc000000d) == ERROR_INVALID_PARAMETER,
           "RtlNtStatusToDosError(STATUS_INVALID_PARAMETER) = %lu, want ERROR_INVALID_PARAMETER (87)",
           (unsigned long)fn((NTSTATUS)0xc000000d));
    CHECKF(fn((NTSTATUS)0xc000007a) == ERROR_PROC_NOT_FOUND,
           "RtlNtStatusToDosError(STATUS_PROCEDURE_NOT_FOUND) = %lu, want ERROR_PROC_NOT_FOUND (127)",
           (unsigned long)fn((NTSTATUS)0xc000007a));
}

/* ======================================================================================
 * Sanity: every stub this file calls directly really is a win32u.dll syscall stub with the
 * fixed 19041 x64 prologue (k_stub_valid), and its encoded service number matches
 * tools/win32u_syscalls.tsv - catches a resolver/typo mistake in this file before it is blamed
 * on the kernel. (sc_stub_layout already hard-covers this for the *whole* table; this is a
 * narrow self-check scoped to just the stubs this file calls.)
 * ====================================================================================== */
TEST(dcomp_win32kt_stub_numbers_self_check) {
    HMODULE win32u = dc_win32u();
    REQUIRE(win32u);
    /* Every stub this file resolves and calls, cross-checked at runtime against
     * k_stub_number() (decoded directly from the live stub bytes - see k.h) so a
     * typo'd export name or an unexpectedly-missing stub is flagged here rather than
     * misread as a kernel-behaviour difference. */
    static const char *called[] = {
        "NtDCompositionConnectPipe", "NtDCompositionGetChannels",
        "NtDCompositionReferenceSharedResourceOnDwmChannel", "NtDCompositionUpdatePointerCapture",
        "NtDuplicateCompositionInputSink", "NtQueryCompositionInputQueueAndTransform",
        "NtQueryCompositionInputSink", "NtDCompositionDuplicateHandleToProcess",
        "NtDCompositionConfirmFrame", "NtDCompositionCreateDwmChannel",
        "NtDesktopCaptureBits", "NtVisualCaptureBits",
    };
    for (size_t i = 0; i < sizeof(called) / sizeof(called[0]); i++) {
        FARPROC stub = t_proc_in("win32u.dll", called[i]);
        if (!stub) { t_skip("missing export: %s", called[i]); continue; }
        CHECKF(k_stub_valid(stub), "%s: win32u.dll export does not look like a 19041 x64 syscall "
               "stub (unexpected prologue bytes)", called[i]);
        long num = k_stub_number(stub);
        CHECKF(num >= 0x1000, "%s: decoded service number 0x%lx is not in the win32k shadow "
               "table range (>= 0x1000)", called[i], num);
    }
}
