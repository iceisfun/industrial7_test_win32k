/* dx_gap_02: state-changing calls from gap_02, run only with /destructive and only in child processes.
 *   NtUserModifyUserStartupInfoFlags(mask, value): rewrites the process W32 startup flags word
 *   ((old & ~mask) | (mask & value)); the word is not readable from user mode, so the checks are the return value and
 *   that the process keeps working. NtUserSetImeInfoEx: without IME support in the session (dwSRVIFlags & 4 == 0) fails
 *   with ERROR_CALL_NOT_IMPLEMENTED (0x78); otherwise an all-zero block names no keyboard layout and is refused.
 */
#include "../gap_02/gap2.h"
#include <string.h>

#define CC(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

CHILD(dx_gap2_startupinfo) {
    (void)argc; (void)argv;
    int fails = 0;
    NtUserModifyUserStartupInfoFlags_pfn f = GN(NtUserModifyUserStartupInfoFlags);
    if (!f) return 77;
    /* STARTF_FORCEONFEEDBACK 0x40 / STARTF_FORCEOFFFEEDBACK 0x80 */
    CC(f(0x40, 0x40) == 1);
    CC(f(0x40, 0x00) == 1);
    CC(f(0x80, 0x80) == 1);
    CC(f(0x80, 0x00) == 1);
    CC(f(0xC0, 0x00) == 1);
    HWND h = gap2_top();
    if (h) {
        CC(IsWindow(h));
        gap2_destroy(h);
    }
    printf("dx_gap2_startupinfo fails=%d\n", fails);
    return fails ? 1 : 0;
}
TEST(dx_gap2_startupinfo_modify_in_child) {
    REQUIRE(GN(NtUserModifyUserStartupInfoFlags));
    DWORD r = t_run_child("dx_gap2_startupinfo", NULL, 20000);
    if (r == 77) { t_skip("child prerequisite missing"); return; }
    EXPECT_EQ(r, 0);
}

CHILD(dx_gap2_imeinfo) {
    (void)argc; (void)argv;
    int fails = 0;
    NtUserSetImeInfoEx_pfn f = GN(NtUserSetImeInfoEx);
    if (!f) return 77;
    struct k_user_shared_info si;
    HWND w = gap2_top();   /* converts the thread to a GUI thread so gSharedInfo is valid */
    unsigned long srvi = 0; int have = 0;
    if (w && k_shared_info(&si) && si.psi) have = k_try_read_u32(si.psi, &srvi);
    unsigned char blk[0x200]; memset(blk, 0, sizeof blk);
    SetLastError(0);
    INT_PTR r = f(blk);
    DWORD e = GetLastError();
    CC(r == 0);
    printf("NOTE SetImeInfoEx(zeros) = %lld err %lu dwSRVIFlags %s%08lx\n", (long long)r, (unsigned long)e, have ? "" : "unreadable ", srvi);
    if (have && !(srvi & 4)) CC(e == 0x78);
    gap2_destroy(w);
    printf("dx_gap2_imeinfo fails=%d\n", fails);
    return fails ? 1 : 0;
}
TEST(dx_gap2_imeinfo_zero_block_in_child) {
    REQUIRE(GN(NtUserSetImeInfoEx));
    DWORD r = t_run_child("dx_gap2_imeinfo", NULL, 20000);
    if (r == 77) { t_skip("child prerequisite missing"); return; }
    EXPECT_EQ(r, 0);
}
