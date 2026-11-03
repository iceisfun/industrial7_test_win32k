/* Syscalls that a normal process can only fail: DWM-only DirectComposition entry points, NtUserProcessConnect (CSRSS only),
 * argument-validation fronts of NtUserInjectTouchInput / NtUserChangeDisplaySettings, and pointer-info queries for pointers that do not exist.
 *
 * Analysis facts (19041):
 *  - NtUserProcessConnect: the first test is PsGetCurrentProcess() == gpepCSRSS, else STATUS_UNSUCCESSFUL (0xC0000001); no argument is touched.
 *    Only user32!ClientThreadSetup (gfServerProcess, i.e. inside csrss) and winsrvext!UserClientConnect call it.
 *  - NtQueryCompositionInputSink / NtQueryCompositionInputQueueAndTransform / NtDCompositionDuplicateHandleToProcess /
 *    NtDCompositionReferenceSharedResourceOnDwmChannel: UserIsCurrentProcessDwm() is the first test, else STATUS_ACCESS_DENIED
 *    (0xC0000022, written -0x3fffffde); no argument is read before it.
 *  - NtDCompositionConfirmFrame: argument 2 NULL or pointing at a zero qword -> STATUS_INVALID_PARAMETER; then the process's composition
 *    connection must exist and match argument 1.
 *  - NtUserInjectTouchInput: contact count outside 1..256 -> FALSE + ERROR_INVALID_PARAMETER before the contacts are probed.
 *  - NtUserChangeDisplaySettings: flag bit 27 (0x08000000) is rejected with DISP_CHANGE_FAILED (-1) + ERROR_INVALID_PARAMETER;
 *    user32!ChangeDisplaySettingsExW with a non-NULL hwnd returns DISP_CHANGE_BADPARAM (-5) without a syscall. */
#include "g8.h"

#define STATUS_UNSUCCESSFUL_ 0xC0000001u
#define STATUS_ACCESS_DENIED_ 0xC0000022u
#define STATUS_INVALID_PARAMETER_ 0xC000000Du

typedef ULONG_PTR UP;
UDECL(LONG, NtUserProcessConnect,    (UP, UP, UP));
UDECL(LONG, NtQueryCompositionInputSink, (UP, UP));
UDECL(LONG, NtQueryCompositionInputQueueAndTransform, (UP, UP, UP, UP));
UDECL(LONG, NtDCompositionDuplicateHandleToProcess, (UP, UP, UP));
UDECL(LONG, NtDCompositionReferenceSharedResourceOnDwmChannel, (UP, UP, UP, UP, UP));
UDECL(LONG, NtDCompositionConfirmFrame, (UP, UP, UP, UP));
UDECL(BOOL, NtUserInjectTouchInput,  (UINT, void *));
UDECL(LONG, NtUserChangeDisplaySettings, (UNICODE_STRING *, DEVMODEW *, DWORD, void *));

/* ================================================================= NtUserProcessConnect ============== */

TEST(gap8_processconnect_refused_outside_csrss) {
    G8_NOT_WINE();
    REQUIRE(UN(NtUserProcessConnect));
    /* CSRSS passes a real process handle; user32 inside csrss passes -1 with a 0x240-byte USERCONNECT block. */
    static BYTE blk[0x240];
    HANDLE hs[] = { GetCurrentProcess(), (HANDLE)(ULONG_PTR)-1, NULL, INVALID_HANDLE_VALUE, (HANDLE)(ULONG_PTR)0x4, (HANDLE)(ULONG_PTR)0xdead0 };
    for (unsigned i = 0; i < sizeof hs / sizeof hs[0]; i++) {
        memset(blk, 0x5a, sizeof blk);
        LONG r = UN(NtUserProcessConnect)((UP)hs[i], (UP)blk, sizeof blk);
        CHECKF((ULONG)r == STATUS_UNSUCCESSFUL_, "handle %p -> 0x%lx (%s)", hs[i], (ULONG)r, k_status_name(r));
        BYTE want[0x240]; memset(want, 0x5a, sizeof want);
        CHECKF(!memcmp(blk, want, sizeof blk), "handle %p: connect block modified", hs[i]);
    }
}

TEST(gap8_processconnect_refusal_precedes_any_argument_use) {
    G8_NOT_WINE();
    REQUIRE(UN(NtUserProcessConnect));
    /* The csrss test is first, so even NULL / kernel-range buffers are never dereferenced. */
    UP bufs[] = { 0, 1, 0x7fffffff0000ull, 0xffff800000000000ull };
    for (unsigned i = 0; i < sizeof bufs / sizeof bufs[0]; i++) {
        LONG r = UN(NtUserProcessConnect)((UP)GetCurrentProcess(), bufs[i], 0x240);
        CHECKF((ULONG)r == STATUS_UNSUCCESSFUL_, "buffer 0x%llx -> 0x%lx", (unsigned long long)bufs[i], (ULONG)r);
    }
}

TEST(gap8_processconnect_does_not_disturb_existing_connection) {
    G8_NOT_WINE();
    REQUIRE(UN(NtUserProcessConnect)); REQUIRE(U(IsWindow)); REQUIRE(U(GetDC));
    void *si_before = k_shared_info_ptr();
    static BYTE blk[0x240];
    (void)UN(NtUserProcessConnect)((UP)GetCurrentProcess(), (UP)blk, sizeof blk);
    void *si_after = k_shared_info_ptr();
    CHECK(si_before == si_after);
    HDC dc = U(GetDC)(NULL);
    CHECK(dc != NULL);
    if (dc) U(ReleaseDC)(NULL, dc);
    void *cb = k_cb_table();
    CHECK(cb != NULL);
}

TEST(gap8_processconnect_stub_shape) {
    G8_NOT_WINE();
    REQUIRE(UN(NtUserProcessConnect));
    FARPROC p = (FARPROC)UN(NtUserProcessConnect);
    CHECK(k_stub_valid(p));
    long n = k_stub_number(p);
    CHECKF(n >= 0x1000 && n < 0x1000 + 0x600, "service number 0x%lx", n);
}

/* ================================================================= DWM-only composition syscalls ===== */

TEST(gap8_dcomp_inputsink_dwm_only) {
    G8_NOT_WINE();
    REQUIRE(UN(NtQueryCompositionInputSink));
    static DWORD q[8];
    UP hs[] = { 0, 4, (UP)GetCurrentProcess(), 0xdead0, ~(UP)0 };
    for (unsigned h = 0; h < sizeof hs / sizeof hs[0]; h++)
        for (int sz = 0; sz < 4; sz++) {
            static const DWORD cb[] = { 0x14, 0x18, 0, 0x10 };
            memset(q, 0, sizeof q); q[0] = cb[sz];
            LONG r = UN(NtQueryCompositionInputSink)(hs[h], (UP)q);
            CHECKF((ULONG)r == STATUS_ACCESS_DENIED_, "handle %llx cb %lx -> 0x%lx", (unsigned long long)hs[h], cb[sz], (ULONG)r);
            CHECKF(q[1] == 0 && q[2] == 0 && q[4] == 0, "output written for a refused caller");
        }
}

TEST(gap8_dcomp_inputsink_refusal_precedes_pointer_use) {
    G8_NOT_WINE();
    REQUIRE(UN(NtQueryCompositionInputSink));
    UP ptrs[] = { 0, 8, 0xffff800000000000ull };
    for (unsigned i = 0; i < sizeof ptrs / sizeof ptrs[0]; i++) {
        LONG r = UN(NtQueryCompositionInputSink)(0, ptrs[i]);
        CHECKF((ULONG)r == STATUS_ACCESS_DENIED_, "ptr %llx -> 0x%lx", (unsigned long long)ptrs[i], (ULONG)r);
    }
}

TEST(gap8_dcomp_inputqueue_transform_dwm_only) {
    G8_NOT_WINE();
    REQUIRE(UN(NtQueryCompositionInputQueueAndTransform));
    static BYTE q[0x38], t[0x40];
    for (UP type = 0; type < 6; type++)
        for (UP h = 0; h < 3; h++) {
            memset(q, 0x5a, sizeof q); memset(t, 0x5a, sizeof t);
            LONG r = UN(NtQueryCompositionInputQueueAndTransform)(h * 4, type, (UP)q, (UP)t);
            CHECKF((ULONG)r == STATUS_ACCESS_DENIED_, "handle %llu type %llu -> 0x%lx", (unsigned long long)h * 4, (unsigned long long)type, (ULONG)r);
            CHECKF(q[0] == 0x5a && q[0x37] == 0x5a && t[0] == 0x5a && t[0x3f] == 0x5a, "outputs written for a refused caller");
        }
    CHECK((ULONG)UN(NtQueryCompositionInputQueueAndTransform)(0, 0, 0, 0) == STATUS_ACCESS_DENIED_);
}

TEST(gap8_dcomp_duplicate_handle_to_process_dwm_only) {
    G8_NOT_WINE();
    REQUIRE(UN(NtDCompositionDuplicateHandleToProcess));
    UP hs[] = { 0, 4, 0x1234, (UP)GetCurrentProcess() };
    UP pids[] = { 0, 4, GetCurrentProcessId(), (UP)(int)-0x54535255, 0xffffffffu };
    for (unsigned i = 0; i < 4; i++)
        for (unsigned j = 0; j < 5; j++) {
            LONG r = UN(NtDCompositionDuplicateHandleToProcess)(hs[i], pids[j], 0);
            /* the (pid == -0x54535255 && arg3 == 0) bugcheck arm is behind the DWM test, so a normal process just gets ACCESS_DENIED */
            CHECKF((ULONG)r == STATUS_ACCESS_DENIED_, "handle %llx pid %llx -> 0x%lx", (unsigned long long)hs[i], (unsigned long long)pids[j], (ULONG)r);
        }
}

TEST(gap8_dcomp_reference_shared_resource_dwm_only) {
    G8_NOT_WINE();
    REQUIRE(UN(NtDCompositionReferenceSharedResourceOnDwmChannel));
    static DWORD o1 = 0x5a5a5a5a, o2 = 0x5a5a5a5a;
    for (UP ch = 0; ch < 3; ch++)
        for (UP type = 0; type < 4; type++) {
            LONG r = UN(NtDCompositionReferenceSharedResourceOnDwmChannel)((UP)ch, 0x40, type, (UP)&o1, (UP)&o2);
            CHECKF((ULONG)r == STATUS_ACCESS_DENIED_, "channel %llu type %llu -> 0x%lx", (unsigned long long)ch, (unsigned long long)type, (ULONG)r);
        }
    CHECK(o1 == 0x5a5a5a5a && o2 == 0x5a5a5a5a);
    CHECK((ULONG)UN(NtDCompositionReferenceSharedResourceOnDwmChannel)(0, 0, 0, 0, 0) == STATUS_ACCESS_DENIED_);
}

TEST(gap8_dcomp_confirmframe_null_or_zero_frame_handle) {
    G8_NOT_WINE();
    REQUIRE(UN(NtDCompositionConfirmFrame));
    static ULONGLONG zero = 0;
    UP hs[] = { 0, 4, 0x1234, ~(UP)0 };
    for (unsigned i = 0; i < 4; i++) {
        LONG r = UN(NtDCompositionConfirmFrame)(hs[i], 0, 0, 0);
        CHECKF((ULONG)r == STATUS_INVALID_PARAMETER_, "NULL frame ptr, handle %llx -> 0x%lx", (unsigned long long)hs[i], (ULONG)r);
        zero = 0;
        r = UN(NtDCompositionConfirmFrame)(hs[i], (UP)&zero, 0, 0);
        CHECKF((ULONG)r == STATUS_INVALID_PARAMETER_, "zero frame handle, handle %llx -> 0x%lx", (unsigned long long)hs[i], (ULONG)r);
        CHECK(zero == 0);
    }
}

TEST(gap8_dcomp_confirmframe_without_connection_fails) {
    G8_NOT_WINE();
    REQUIRE(UN(NtDCompositionConfirmFrame));
    /* A non-zero frame handle gets past the argument test; the process has no composition connection (or the handle differs): a failure status. */
    static ULONGLONG frame = 0x1234;
    UP hs[] = { 0, 4, 0x1234 };
    for (unsigned i = 0; i < 3; i++) {
        LONG r = UN(NtDCompositionConfirmFrame)(hs[i], (UP)&frame, 0, 0);
        CHECKF((ULONG)r == STATUS_UNSUCCESSFUL_ || (ULONG)r == STATUS_ACCESS_DENIED_, "handle %llx -> 0x%lx (%s)", (unsigned long long)hs[i], (ULONG)r, k_status_name(r));
        CHECK(frame == 0x1234);
    }
}

/* ================================================================= NtUserInjectTouchInput ============ */

TEST(gap8_injecttouch_count_range) {
    G8_NOT_WINE();
    REQUIRE(UN(NtUserInjectTouchInput));
    /* Counts outside 1..256 are refused before the contact array is probed, so a NULL array is safe here and nothing can be injected. */
    static const UINT bad[] = { 0, 257, 258, 512, 0x1000, 0x10000, 0x7fffffff, 0x80000000u, 0xffffffffu };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(0);
        BOOL r = UN(NtUserInjectTouchInput)(bad[i], NULL);
        CHECKF(r == FALSE, "count %u", bad[i]);
        CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "count %u err %lu", bad[i], GetLastError());
    }
}

TEST(gap8_injecttouch_count_range_userland) {
    G8_NOT_WINE();
    FARPROC f = t_proc_in("user32.dll", "InjectTouchInput");
    if (!f) { t_skip("InjectTouchInput missing"); return; }
    BOOL (WINAPI *inject)(UINT32, const void *) = (BOOL (WINAPI *)(UINT32, const void *))f;
    static const UINT bad[] = { 0, 257, 0x1000, 0xffffffffu };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(0);
        BOOL r = inject(bad[i], NULL);
        CHECKF(r == FALSE, "count %u", bad[i]);
        CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "count %u err %lu", bad[i], GetLastError());
    }
}

/* ================================================================= NtUserChangeDisplaySettings ======= */

TEST(gap8_changedisplay_flag_bit27_refused) {
    REQUIRE(UN(NtUserChangeDisplaySettings));
    /* No mode change can happen: the flag test rejects before the display-settings change. */
    static const DWORD bad[] = { 0x08000000, 0x08000002, 0x08000001, 0x0a000000, 0x0fffffff, 0xffffffffu, 0x88000000u };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(0);
        LONG r = UN(NtUserChangeDisplaySettings)(NULL, NULL, bad[i], NULL);
        CHECKF(r == DISP_CHANGE_FAILED, "flags 0x%lx -> %ld", bad[i], r);
        DWORD e = GetLastError();
        if (e == ERROR_ACCESS_DENIED) t_note("flags 0x%lx: caller lacks desktop write access", bad[i]);
        else CHECKF(e == ERROR_INVALID_PARAMETER, "flags 0x%lx err %lu", bad[i], e);
    }
}

TEST(gap8_changedisplay_flag_bit27_refused_userland) {
    FARPROC f = t_proc_in("user32.dll", "ChangeDisplaySettingsExW");
    if (!f) { t_skip("ChangeDisplaySettingsExW missing"); return; }
    LONG (WINAPI *cds)(LPCWSTR, DEVMODEW *, HWND, DWORD, LPVOID) = (LONG (WINAPI *)(LPCWSTR, DEVMODEW *, HWND, DWORD, LPVOID))f;
    SetLastError(0);
    LONG r = cds(NULL, NULL, NULL, 0x08000000, NULL);
    CHECKF(r == DISP_CHANGE_FAILED, "-> %ld", r);
}

TEST(gap8_changedisplay_hwnd_must_be_null) {
    FARPROC f = t_proc_in("user32.dll", "ChangeDisplaySettingsExW");
    if (!f) { t_skip("ChangeDisplaySettingsExW missing"); return; }
    LONG (WINAPI *cds)(LPCWSTR, DEVMODEW *, HWND, DWORD, LPVOID) = (LONG (WINAPI *)(LPCWSTR, DEVMODEW *, HWND, DWORD, LPVOID))f;
    /* The wrapper returns DISP_CHANGE_BADPARAM itself when hwnd != NULL, before any syscall, whatever the other arguments. */
    HWND hs[] = { (HWND)(ULONG_PTR)1, (HWND)(ULONG_PTR)0x1234 };
    DWORD fls[] = { 0, CDS_TEST, CDS_UPDATEREGISTRY, 0x08000000 };
    for (unsigned i = 0; i < 2; i++)
        for (unsigned j = 0; j < 4; j++) {
            LONG r = cds(NULL, NULL, hs[i], fls[j], NULL);
            CHECKF(r == DISP_CHANGE_BADPARAM, "hwnd %p flags 0x%lx -> %ld", hs[i], fls[j], r);
        }
}

/* ================================================================= NtUserGetPointerInfoList fronts === */

TEST(gap8_pointerinfo_unknown_pointer_ids) {
    FARPROC gpi = t_proc_in("user32.dll", "GetPointerInfo");
    if (!gpi) { t_skip("GetPointerInfo missing"); return; }
    BOOL (WINAPI *get)(UINT32, void *) = (BOOL (WINAPI *)(UINT32, void *))gpi;
    static BYTE info[0x80];
    static const UINT32 ids[] = { 0, 0x7fff0000, 0xffffffffu, 0x12345, 0xfffe };
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        memset(info, 0x5a, sizeof info);
        SetLastError(0);
        BOOL r = get(ids[i], info);
        CHECKF(r == FALSE, "pointer id 0x%x", ids[i]);
        t_note("GetPointerInfo(0x%x) err %lu", ids[i], GetLastError());
    }
}

TEST(gap8_pointerinfo_frame_and_history_unknown_ids) {
    static const char *names[] = { "GetPointerFrameInfo", "GetPointerInfoHistory", "GetPointerFrameInfoHistory", "GetPointerTouchInfo", "GetPointerPenInfo", "GetPointerFrameTouchInfo", "GetPointerFramePenInfo", "GetPointerTouchInfoHistory", "GetPointerPenInfoHistory" };
    int any = 0;
    for (unsigned n = 0; n < sizeof names / sizeof names[0]; n++) {
        FARPROC f = t_proc_in("user32.dll", names[n]);
        if (!f) continue;
        any = 1;
        static BYTE buf[0x2000];
        UINT32 cnt = 1, cnt2 = 1;
        memset(buf, 0x5a, sizeof buf);
        SetLastError(0);
        BOOL r;
        if (n == 0 || n == 5 || n == 6) r = ((BOOL (WINAPI *)(UINT32, UINT32 *, void *))f)(0x7fff0000, &cnt, buf);
        else if (n == 1 || n == 2 || n == 7 || n == 8) r = ((BOOL (WINAPI *)(UINT32, UINT32 *, void *))f)(0x7fff0000, &cnt, buf);
        else r = ((BOOL (WINAPI *)(UINT32, void *))f)(0x7fff0000, buf);
        (void)cnt2;
        CHECKF(r == FALSE, "%s on a pointer that does not exist", names[n]);
        t_note("%s -> err %lu", names[n], GetLastError());
    }
    if (!any) t_skip("no pointer-info exports");
}
