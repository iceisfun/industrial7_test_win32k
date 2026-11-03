/* gap_07 process-wide DPI awareness syscalls. NtUserSetProcessDpiAwarenessContext and the user32
 * SetProcessDpiAwarenessContext route change irreversible per-process state, so every test runs in a CHILD process
 * that has not touched user32/gdi32 before the call.
 *
 * win32kbase NtUserSetProcessDpiAwarenessContext(ctx, allowUpgrade): ctx must pass IsValidKernelDpiAwarenessContext
 * (else FALSE + ERROR_INVALID_PARAMETER); the first call sets process flag 0x400000 and stores ctx in the process
 * info; a later call only succeeds when allowUpgrade != 0, ctx's low nibble != 0 and the stored ctx is 0x12.
 * NtUserGetProcessDpiAwarenessContext(hProcess): 0 or -1 selects the calling process, a handle that cannot be
 * referenced with PROCESS_QUERY_LIMITED_INFORMATION yields 0 + ERROR_INVALID_PARAMETER. */
#include "g7.h"

#define C(x) do { if (!(x)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #x); bad++; } } while (0)

CHILD(gap7_dpi_direct) {
    (void)argc; (void)argv;
    int bad = 0;
    NtUserSetProcessDpiAwarenessContext_pfn set = G7N(NtUserSetProcessDpiAwarenessContext);
    NtUserGetProcessDpiAwarenessContext_pfn get = G7N(NtUserGetProcessDpiAwarenessContext);
    if (!set || !get) { printf("SKIP missing export\n"); return 0; }

    UINT v0 = get(NULL);
    UINT vm1 = get((HANDLE)(LONG_PTR)-1);
    C(v0 == vm1);                                     /* 0 and -1 both mean "this process" */
    printf("NOTE initial process dpi ctx 0x%x\n", v0);
    SetLastError(0);
    C(get((HANDLE)(ULONG_PTR)0x7ff4) == 0);           /* ObReferenceObjectByHandle fails */
    C(GetLastError() == ERROR_INVALID_PARAMETER);
    HANDLE self = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentProcessId());
    if (self) { C(get(self) == v0); CloseHandle(self); }

    static const UINT bad_ctx[] = { 0, 1, 0x12345, 0x77777, 0x55555555, 0xffffffff };
    for (unsigned i = 0; i < sizeof bad_ctx / sizeof *bad_ctx; i++) {
        SetLastError(0);
        BOOL r = set(bad_ctx[i], 0);
        if (r || GetLastError() != ERROR_INVALID_PARAMETER) { printf("FAIL ctx 0x%x r=%d err=%lu\n", bad_ctx[i], r, GetLastError()); bad++; }
    }
    C(get(NULL) == v0);                               /* rejected values leave the process untouched */

    C(set(G7_DPI_PMV2, 0) != 0);
    UINT v1 = get(NULL);
    printf("NOTE after set 0x22: 0x%x\n", v1);
    C((v1 & 0xf) == 2);                               /* per-monitor family */
    C(set(G7_DPI_PMV2, 0) == 0);                      /* flag 0x400000 set and allowUpgrade == 0 -> refused */
    C(get(NULL) == v1);
    C(set(G7_DPI_SYSTEM, 1) == 0);                    /* upgrade path needs the stored ctx to be exactly 0x12 */
    C(get(NULL) == v1);
    C(set(G7_DPI_UNAWARE, 1) == 0);
    C(get(NULL) == v1);
    return bad ? 1 : 0;
}
TEST(gap7_dpi_direct_set_get_in_child) {
    struct k_child_result r = k_child_run("gap7_dpi_direct", NULL, 20000);
    EXPECT_EQ(r.exit_code, 0);
}

/* The upgrade transition: 0x12 first, then a second call with allowUpgrade != 0 and a non-zero low nibble. */
CHILD(gap7_dpi_upgrade) {
    (void)argc; (void)argv;
    int bad = 0;
    NtUserSetProcessDpiAwarenessContext_pfn set = G7N(NtUserSetProcessDpiAwarenessContext);
    NtUserGetProcessDpiAwarenessContext_pfn get = G7N(NtUserGetProcessDpiAwarenessContext);
    if (!set || !get) { printf("SKIP missing export\n"); return 0; }
    C(set(G7_DPI_SYSTEM, 0) != 0);
    UINT v1 = get(NULL);
    printf("NOTE after set 0x12: 0x%x\n", v1);
    C((v1 & 0xf) == 2);
    C(set(G7_DPI_PMV2, 1) != 0);                      /* stored 0x12 + allowUpgrade -> accepted */
    UINT v2 = get(NULL);
    printf("NOTE after upgrade to 0x22: 0x%x\n", v2);
    C((v2 & 0xf) == 2);
    C(set(G7_DPI_PMV2, 1) == 0);                      /* now stored != 0x12 */
    C(set(G7_DPI_SYSTEM, 1) == 0);
    C(get(NULL) == v2);
    return bad ? 1 : 0;
}
TEST(gap7_dpi_upgrade_transition_in_child) {
    struct k_child_result r = k_child_run("gap7_dpi_upgrade", NULL, 20000);
    EXPECT_EQ(r.exit_code, 0);
}

/* user32 route: SetProcessDpiAwarenessContext validates, calls the syscall once, and records success. */
CHILD(gap7_dpi_user32_pmv2) {
    (void)argc; (void)argv;
    int bad = 0;
    NtUserGetProcessDpiAwarenessContext_pfn get = G7N(NtUserGetProcessDpiAwarenessContext);
    SetLastError(0);
    C(SetProcessDpiAwarenessContext((DPI_AWARENESS_CONTEXT)(ULONG_PTR)0x1234) == FALSE);
    C(GetLastError() == ERROR_INVALID_PARAMETER);     /* ValidateDpiAwarenessContextEx fails */
    SetLastError(0);
    C(U(SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) == TRUE);
    if (get) C((get(NULL) & 0xf) == 2);
    C(U(IsProcessDPIAware)() == TRUE);
    SetLastError(0);
    C(U(SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) == FALSE);
    C(GetLastError() == ERROR_ACCESS_DENIED);         /* gfDpiAwarenessInitialized already set */
    SetLastError(0);
    C(U(SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE) == FALSE);
    C(GetLastError() == ERROR_ACCESS_DENIED);
    return bad ? 1 : 0;
}
TEST(gap7_dpi_user32_route_pmv2_in_child) {
    struct k_child_result r = k_child_run("gap7_dpi_user32_pmv2", NULL, 20000);
    EXPECT_EQ(r.exit_code, 0);
}

CHILD(gap7_dpi_user32_system) {
    (void)argc; (void)argv;
    int bad = 0;
    NtUserGetProcessDpiAwarenessContext_pfn get = G7N(NtUserGetProcessDpiAwarenessContext);
    C(U(SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE) == TRUE);
    if (get) {
        UINT v = get(NULL);
        printf("NOTE system aware ctx 0x%x\n", v);
        C((v & 0xf) == 1);                            /* system-aware family; bits 8..16 carry the system DPI */
    }
    C(U(IsProcessDPIAware)() == TRUE);
    SetLastError(0);
    C(U(SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) == FALSE);
    C(GetLastError() == ERROR_ACCESS_DENIED);
    return bad ? 1 : 0;
}
TEST(gap7_dpi_user32_route_system_aware_in_child) {
    struct k_child_result r = k_child_run("gap7_dpi_user32_system", NULL, 20000);
    EXPECT_EQ(r.exit_code, 0);
}

CHILD(gap7_dpi_user32_unaware) {
    (void)argc; (void)argv;
    int bad = 0;
    NtUserGetProcessDpiAwarenessContext_pfn get = G7N(NtUserGetProcessDpiAwarenessContext);
    C(U(SetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT_UNAWARE) == TRUE);
    if (get) {
        UINT v = get(NULL);
        printf("NOTE unaware ctx 0x%x\n", v);
        C((v & 0xf) == 0);
    }
    C(U(IsProcessDPIAware)() == FALSE);
    return bad ? 1 : 0;
}
TEST(gap7_dpi_user32_route_unaware_in_child) {
    struct k_child_result r = k_child_run("gap7_dpi_user32_unaware", NULL, 20000);
    EXPECT_EQ(r.exit_code, 0);
}

/* An un-modified child (no SetProcessDpiAwarenessContext call) reports the unaware default. */
CHILD(gap7_dpi_default) {
    (void)argc; (void)argv;
    int bad = 0;
    NtUserGetProcessDpiAwarenessContext_pfn get = G7N(NtUserGetProcessDpiAwarenessContext);
    if (!get) { printf("SKIP missing export\n"); return 0; }
    UINT v = get(NULL);
    printf("NOTE default process ctx 0x%x\n", v);
    /* a manifest-less exe is DPI unaware unless the machine forces awareness; only the family is asserted */
    C((v & 0xf) <= 2);
    C(v != 0);
    return bad ? 1 : 0;
}
TEST(gap7_dpi_default_context_in_child) {
    struct k_child_result r = k_child_run("gap7_dpi_default", NULL, 20000);
    EXPECT_EQ(r.exit_code, 0);
}
