/* flags: per-process win32k flags set through NtUserSetProcessMousewheelRoutingMode (W32PROCESS+0x334 bits
 * 0x8000/0x10000) and NtUserSetProcessInteractionFlags (W32PROCESS+0x408..0x40a), and pristine-child
 * ordering. All run in a child because they change process-wide state.
 *
 * win32kbase!NtUserSetProcessMousewheelRoutingMode(int mode): needs a W32PROCESS; mode 0, 1 or 2 -> rewrite the bits,
 * return 1; any other mode -> UserSetLastError(0x57), return 0.
 * win32kfull!NtUserSetProcessInteractionFlags(b1,b2,b3) stores three bytes and returns 1 unconditionally.
 * user32!ForceFocusBasedMouseWheelRouting(BOOL f) = NtUserSetProcessMousewheelRoutingMode(f != 0).
 */
#include "r2.h"

#define ERR_SENT 0x7777

R2_SCN(flags_mousewheel_valid_modes_return_one) {
    R2_CHILD_NEED_WIN();
    static const int modes[] = { 0, 1, 2, 1, 0, 2, 2, 0 };
    for (unsigned i = 0; i < sizeof modes / sizeof modes[0]; i++) {
        SetLastError(ERR_SENT);
        ULONG_PTR r = SYS("NtUserSetProcessMousewheelRoutingMode", modes[i], 0,0,0,0,0,0,0,0,0,0,0);
        RCF(r == 1, "mode %d -> 0x%llx", modes[i], (unsigned long long)r);
        RC(GetLastError() == ERR_SENT);               /* success never touches the last error */
    }
}
R2_SCN(flags_mousewheel_invalid_modes_fail_87) {
    R2_CHILD_NEED_WIN();
    static const ULONG bad[] = { 3, 4, 7, 0x100, 0x8000, 0x10000, 0x80000000, 0xFFFFFFFF, 0xFFFFFFFE };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(ERR_SENT);
        ULONG_PTR r = SYS("NtUserSetProcessMousewheelRoutingMode", bad[i], 0,0,0,0,0,0,0,0,0,0,0);
        RCF(r == 0, "mode 0x%lx -> 0x%llx", (unsigned long)bad[i], (unsigned long long)r);
        RCF(GetLastError() == ERROR_INVALID_PARAMETER, "mode 0x%lx error %lu", (unsigned long)bad[i], GetLastError());
    }
}
R2_SCN(flags_mousewheel_invalid_then_valid_recovers) {
    R2_CHILD_NEED_WIN();
    SetLastError(0);
    RC(SYS("NtUserSetProcessMousewheelRoutingMode", 9, 0,0,0,0,0,0,0,0,0,0,0) == 0);
    RC(GetLastError() == 87);
    SetLastError(ERR_SENT);
    RC(SYS("NtUserSetProcessMousewheelRoutingMode", 0, 0,0,0,0,0,0,0,0,0,0,0) == 1);
    RC(GetLastError() == ERR_SENT);
}
R2_SCN(flags_mousewheel_argument_is_32_bit) {
    R2_CHILD_NEED_WIN();
    /* the parameter is declared int: the upper register half is ignored, so 0xDEADBEEF00000001 is mode 1 */
    ULONG_PTR r = SYS("NtUserSetProcessMousewheelRoutingMode", 0xDEADBEEF00000001ULL, 0,0,0,0,0,0,0,0,0,0,0);
    RC(r == 1);
    r = SYS("NtUserSetProcessMousewheelRoutingMode", 0xDEADBEEF00000003ULL, 0,0,0,0,0,0,0,0,0,0,0);
    RC(r == 0);
}
R2_SCN(flags_mousewheel_force_focus_wrapper) {
    R2_CHILD_NEED_WIN();
    typedef BOOL (WINAPI *ffm_t)(BOOL);
    ffm_t f = (ffm_t)t_proc_in("user32.dll", "ForceFocusBasedMouseWheelRouting");
    if (!f) { printf("  SKIP no ForceFocusBasedMouseWheelRouting\n"); return; }
    /* the wrapper normalises to 0/1 (setne) then tail-jumps to the syscall stub: result is the syscall's 1 */
    static const BOOL in[] = { TRUE, FALSE, 2, -1, 0x100, 0 };
    for (unsigned i = 0; i < sizeof in / sizeof in[0]; i++) {
        SetLastError(ERR_SENT);
        RCF(f(in[i]) == 1, "force(%d)", (int)in[i]);
        RC(GetLastError() == ERR_SENT);
    }
}
R2_SCN(flags_mousewheel_does_not_allocate_objects) {
    R2_CHILD_NEED_WIN();
    DWORD g = r2_gur(0), u = r2_gur(1);
    for (int m = 0; m < 3; m++) SYS("NtUserSetProcessMousewheelRoutingMode", m, 0,0,0,0,0,0,0,0,0,0,0);
    REQ(r2_gur(0), g); REQ(r2_gur(1), u);
}
static DWORD WINAPI mw_worker(LPVOID p) {
    /* a thread that has not touched win32k yet is converted by this very call */
    ULONG_PTR r = SYS("NtUserSetProcessMousewheelRoutingMode", 2, 0,0,0,0,0,0,0,0,0,0,0);
    return r == 1 ? 0 : 1;
}
R2_SCN(flags_mousewheel_from_unconverted_worker_thread) {
    R2_CHILD_NEED_WIN();
    HANDLE h = CreateThread(NULL, 0, mw_worker, NULL, 0, NULL);
    RC(h != NULL); if (!h) return;
    REQ(r2_wait_thread(h, 5000), 0); CloseHandle(h);
}
R2_SCN(flags_mousewheel_in_pristine_process) {
    if (t_is_wine()) { printf("  SKIP Wine\n"); return; }
    /* no user32/gdi32 loaded yet: the thread has no THREADINFO. On Windows 10 a win32u call without user32 loaded cannot
     * convert the thread (the kernel callback table does not exist yet and the call dies with a fatal callback exception),
     * so the conversion is the work of the user32 load. */
    void *pti = (void *)1; k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &pti);
    RC(pti == NULL);
    RC(LoadLibraryA("user32.dll") != NULL);
    ULONG_PTR r = SYS("NtUserSetProcessMousewheelRoutingMode", 1, 0,0,0,0,0,0,0,0,0,0,0);
    t_note("pristine mousewheel routing -> 0x%llx", (unsigned long long)r);
    RC(r == 1);
    pti = NULL; k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &pti);
    RC(pti != NULL);   /* Win10 22H2: a small non-pointer value (the THREADINFO is not addressable from user mode), set once the thread is a GUI thread */
}

R2_SCN(flags_interaction_returns_one) {
    R2_CHILD_NEED_WIN();
    static const unsigned char v[][3] = { {0,0,0}, {1,1,1}, {1,0,0}, {0,1,0}, {0,0,1}, {0xff,0xff,0xff}, {2,3,4}, {0x80,0x7f,0x01} };
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) {
        SetLastError(ERR_SENT);
        ULONG_PTR r = SYS("NtUserSetProcessInteractionFlags", v[i][0], v[i][1], v[i][2], 0,0,0,0,0,0,0,0,0);
        RCF(r == 1, "flags {%u,%u,%u} -> 0x%llx", v[i][0], v[i][1], v[i][2], (unsigned long long)r);
        RC(GetLastError() == ERR_SENT);
    }
}
R2_SCN(flags_interaction_garbage_upper_bits_still_succeed) {
    R2_CHILD_NEED_WIN();
    /* each argument is a byte; whatever sits above bit 7 of the registers is irrelevant */
    ULONG_PTR r = SYS("NtUserSetProcessInteractionFlags", 0xFFFFFFFFFFFFFF00ULL, 0xABCDEF0100ULL, 0x1234567800ULL, 0,0,0,0,0,0,0,0,0);
    RC(r == 1);
}
R2_SCN(flags_interaction_repeat_and_reset) {
    R2_CHILD_NEED_WIN();
    for (int i = 0; i < 50; i++) RC(SYS("NtUserSetProcessInteractionFlags", i & 1, (i >> 1) & 1, (i >> 2) & 1, 0,0,0,0,0,0,0,0,0) == 1);
    RC(SYS("NtUserSetProcessInteractionFlags", 0, 0, 0, 0,0,0,0,0,0,0,0,0) == 1);
}
R2_SCN(flags_interaction_no_object_allocation) {
    R2_CHILD_NEED_WIN();
    DWORD g = r2_gur(0), u = r2_gur(1);
    SYS("NtUserSetProcessInteractionFlags", 1, 1, 1, 0,0,0,0,0,0,0,0,0);
    REQ(r2_gur(0), g); REQ(r2_gur(1), u);
    SYS("NtUserSetProcessInteractionFlags", 0, 0, 0, 0,0,0,0,0,0,0,0,0);
}
static DWORD WINAPI ia_worker(LPVOID p) { return SYS("NtUserSetProcessInteractionFlags", 1, 0, 1, 0,0,0,0,0,0,0,0,0) == 1 ? 0 : 1; }
R2_SCN(flags_interaction_from_worker_thread) {
    R2_CHILD_NEED_WIN();
    HANDLE h = CreateThread(NULL, 0, ia_worker, NULL, 0, NULL);
    RC(h != NULL); if (!h) return;
    REQ(r2_wait_thread(h, 5000), 0); CloseHandle(h);
}
R2_SCN(flags_interaction_in_pristine_process) {
    if (t_is_wine()) { printf("  SKIP Wine\n"); return; }
    RC(LoadLibraryA("user32.dll") != NULL);              /* converts the thread; a bare win32u call would die (see above) */
    ULONG_PTR r = SYS("NtUserSetProcessInteractionFlags", 1, 1, 1, 0,0,0,0,0,0,0,0,0);
    RC(r == 1);
}
R2_SCN(flags_both_set_and_window_creation_still_works) {
    R2_CHILD_NEED_WIN();
    SYS("NtUserSetProcessMousewheelRoutingMode", 1, 0,0,0,0,0,0,0,0,0,0,0);
    SYS("NtUserSetProcessInteractionFlags", 1, 1, 1, 0,0,0,0,0,0,0,0,0);
    HWND w = r2_window();
    RC(w != NULL);
    if (w) { RC(U(IsWindow)(w)); RC(U(DestroyWindow)(w)); }
    SYS("NtUserSetProcessMousewheelRoutingMode", 0, 0,0,0,0,0,0,0,0,0,0,0);
    SYS("NtUserSetProcessInteractionFlags", 0, 0, 0, 0,0,0,0,0,0,0,0,0);
}
R2_SCN(flags_threadstate_in_child_invalid_and_valid) {
    R2_CHILD_NEED_WIN();
    SetLastError(0);
    RC((SYS("NtUserSetThreadState", 0x1, 1, 0,0,0,0,0,0,0,0,0,0) & 0xFF) == 0);
    RC(GetLastError() == 0xC000000D);
    SetLastError(0x1111);
    RC((SYS("NtUserSetThreadState", 0, 0, 0,0,0,0,0,0,0,0,0,0) & 0xFF) == 1);
    RC(GetLastError() == 0x1111);
}
