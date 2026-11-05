/* x_ntqip_qip_sched_security / t_setters: refusals of the public setters whose effect the scheduling classes
 * report (SetPriorityClass, background mode, SetProcessInformation memory priority), and that a refused call
 * leaves the reported class values unchanged. Observed on Windows 10 22H2 x64, identical in both token modes. */
#include "qs.h"

static int child_fails;
#define CC_CHILD(c) do { if (!(c)) { printf("  FAIL child: %s\n", #c); child_fails++; } } while (0)
typedef BOOL (WINAPI *spi_t)(HANDLE, int, LPVOID, DWORD);
static ULONG g32(HANDLE h, ULONG c, LONG *s) {
    ULONG v = 0xCCCCCCCC, rl = 0; *s = qs_q(h, c, &v, 4, &rl); return v;
}
static unsigned cls18(HANDLE h, LONG *s) {
    _Alignas(4) unsigned char b[2] = { 0xCC, 0xCC }; ULONG rl = 0; *s = qs_q(h, 18, b, 2, &rl); return b[1];
}

/* Priority class values that are neither a single class flag nor the background-mode flags are refused with
 * ERROR_INVALID_PARAMETER and change nothing. */
TEST(qip_ss_setpriorityclass_invalid_values) {
    if (!qs_have()) { t_skip("export missing"); return; }
    HANDLE h = qs_dup(qs_child(), R_LIM | 0x200);
    if (!h) { t_skip("no child"); return; }
    DWORD bad[] = { 0, 3, 4, 0x10000, 0x200000, 0x400000 };
    CHECK(SetPriorityClass(h, NORMAL_PRIORITY_CLASS));
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        SetLastError(0);
        BOOL r = SetPriorityClass(h, bad[i]);
        DWORD e = GetLastError();
        CHECKF(!r && e == ERROR_INVALID_PARAMETER, "value 0x%lx: ret %d err %lu", bad[i], r, e);
        LONG s; unsigned c = cls18(h, &s);
        CHECKF(s == QS_OK && c == 2 && GetPriorityClass(h) == NORMAL_PRIORITY_CLASS, "value 0x%lx changed class to %u", bad[i], c);
    }
    CloseHandle(h);
}

/* Background mode can only be begun or ended by a process for itself. */
TEST(qip_ss_background_mode_foreign_refused) {
    if (!qs_have()) { t_skip("export missing"); return; }
    HANDLE h = qs_dup(qs_child(), R_LIM | 0x200);
    if (!h) { t_skip("no child"); return; }
    DWORD modes[] = { PROCESS_MODE_BACKGROUND_BEGIN, PROCESS_MODE_BACKGROUND_END };
    for (unsigned i = 0; i < 2; i++) {
        SetLastError(0);
        BOOL r = SetPriorityClass(h, modes[i]);
        DWORD e = GetLastError();
        CHECKF(!r && e == ERROR_INVALID_PARAMETER, "mode 0x%lx: ret %d err %lu", modes[i], r, e);
    }
    LONG s;
    CHECK(g32(h, 33, &s) == 2 && s == QS_OK);
    CHECK(g32(h, 39, &s) == 5 && s == QS_OK);
    CloseHandle(h);
}
CHILD(qbgend) {
    HANDLE me = GetCurrentProcess();
    SetLastError(0);
    BOOL r = SetPriorityClass(me, PROCESS_MODE_BACKGROUND_END);
    CC_CHILD(!r && GetLastError() == 403);   /* ERROR_PROCESS_MODE_NOT_BACKGROUND */
    r = SetPriorityClass(me, PROCESS_MODE_BACKGROUND_BEGIN);
    CC_CHILD(r);
    SetLastError(0);
    r = SetPriorityClass(me, PROCESS_MODE_BACKGROUND_BEGIN);
    CC_CHILD(!r && GetLastError() == 402);   /* ERROR_PROCESS_MODE_ALREADY_BACKGROUND */
    CC_CHILD(SetPriorityClass(me, PROCESS_MODE_BACKGROUND_END));
    SetLastError(0);
    r = SetPriorityClass(me, PROCESS_MODE_BACKGROUND_END);
    CC_CHILD(!r && GetLastError() == 403);
    return child_fails;
}
TEST(qip_ss_background_mode_begin_end_errors) {
    if (!qs_have()) { t_skip("export missing"); return; }
    struct k_child_result r = k_child_run("qbgend", "", 30000);
    EXPECT_EQ(r.exit_code, 0);
}

/* ProcessMemoryPriority accepts 1..5 only; refusals keep the previous page priority. */
TEST(qip_ss_setprocessinformation_mempri_invalid) {
    if (!qs_have()) { t_skip("export missing"); return; }
    spi_t sp = (spi_t)t_proc_in("kernel32.dll", "SetProcessInformation");
    if (!sp) { t_skip("SetProcessInformation missing"); return; }
    HANDLE h = qs_dup(qs_child(), R_LIM | 0x200);
    if (!h) { t_skip("no child"); return; }
    ULONG bad[] = { 0, 6, 7, 256, 0xffffffffu };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        ULONG in = bad[i]; LONG s;
        SetLastError(0);
        BOOL r = sp(h, 0, &in, 4);
        DWORD e = GetLastError();
        CHECKF(!r && e == ERROR_INVALID_PARAMETER, "value %lu: ret %d err %lu", bad[i], r, e);
        CHECKF(g32(h, 39, &s) == 5 && s == QS_OK, "value %lu changed the page priority", bad[i]);
    }
    ULONG in = 3; LONG s;
    CHECK(sp(h, 0, &in, 4) && g32(h, 39, &s) == 3);
    in = 5; sp(h, 0, &in, 4);
    CloseHandle(h);
}
