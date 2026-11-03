/* sc_raw_syscall/raw_stub.c
 *
 * Area scope: issue syscalls with our OWN inline-asm trampoline (common/k.c's k_raw_syscall:
 * mov r10,rcx; mov eax,<number>; syscall; ret) instead of letting win32u.dll's compiled stub do
 * it, and show the result is identical to calling through the real win32u.dll export. k_raw_syscall
 * is documented (k.h) to use exactly the calling convention win32u's own stubs use (first arg in
 * rcx, moved to r10 before the syscall instruction, remaining args in rdx/r8/r9/stack - the win32u
 * stub prologue '4C 8B D1' is that same 'mov r10, rcx'), so the only variable between "call the
 * win32u export" and "call k_raw_syscall(number, same args)" is which piece of code issued the
 * `syscall` instruction. Every number used here is read from tools/win32u_syscalls.tsv (dumped
 * from win32u.dll's real export table on this corpus) or decoded live from the stub's own bytes
 * via k_stub_number(), never hand-guessed.
 *
 * Every raw call in this file uses an argument list independently verified against the
 * win32kbase.sys/win32kfull.sys analysis (cited per test) to be harmless - no raw call here
 * passes an unproven pointer. NULL/kernel-range probing is explicitly reserved for the dedicated
 * kprobe_user_pointers area (see docs/KERNEL_TESTING.md's safety rules); this area never does
 * that, including for the "wrong table" syscall numbers covered in raw_bounds.c.
 *
 * IMPORTANT discovered while triaging under headless Wine (and the reason every cross-check below
 * calls raw_number_of() instead of a literal tsv number): Wine's win32u.dll uses its OWN service
 * numbering, unrelated to real Windows's (confirmed in sc_raw_syscall_stub_numbers_match_tsv's
 * NOTE output - e.g. NtUserCallHwndOpt is 5031 on 19041 but 4917 under this Wine build). A
 * hardcoded real-Windows number issued as a raw syscall under Wine therefore lands on whatever
 * unrelated function Wine happens to have at that slot - on this Wine build, raw-calling the
 * literal number 4096 (real Windows's NtUserGetThreadState) actually invoked Wine's
 * NtBindCompositionSurface slot and Wine aborted the whole process with "unimplemented function
 * win32u.NtBindCompositionSurface" instead of returning a clean status. Deriving the number from
 * the resolved win32u export's OWN stub bytes (k_stub_number) side-steps this entirely: on real
 * Windows it reproduces the real number (same as the tsv), and under Wine it reproduces WHATEVER
 * number Wine itself uses for that same name - which Wine does implement, because the test's own
 * "via_stub" call through the resolved export just succeeded. This is also literally what the
 * task scope asks for: "N taken from the stub bytes ... instead of the win32u stub".
 */
#include "t.h"
#include "k.h"

/* Resolves `name` in win32u.dll and returns the service number encoded in ITS OWN stub bytes (not
 * a literal), or -1 if the export is missing or not a valid stub shape. See the file header for
 * why every raw call in this file goes through this instead of a hardcoded tsv number. */
static long raw_number_of(const char *name) {
    FARPROC p = t_proc_in("win32u.dll", name);
    if (!p || !k_stub_valid(p)) return -1;
    return k_stub_number(p);
}
/* Same idea, for an ntdll-table (table index "0") export - ntdll.dll's own stubs have the
 * identical fixed prologue (k_stub_valid doesn't care which module it reads). */
static long raw_number_of_in(const char *dll, const char *name) {
    FARPROC p = t_proc_in(dll, name);
    if (!p || !k_stub_valid(p)) return -1;
    return k_stub_number(p);
}

/* ---- Nt* prototypes not in any mingw header (verified by win32u.dll!<Name>/asm: all take their
 * arguments in the plain fastcall/syscall order the real caller is seen passing in analysis). ---- */
UDECL(ULONG_PTR, NtUserGetThreadState, (int));
UDECL(ULONG_PTR, NtUserCallNoParam, (ULONG));
UDECL(DWORD,     NtUserQueryWindow, (HWND, int));
UDECL(BOOL,      NtGdiFlush, (void));
UDECL(NTSTATUS,  NtClose, (HANDLE));

/* ============================================================================================
 * 1. Stub decode sanity: k_stub_number() on a spread of win32u exports across the whole service
 * range (4096..5411 per tools/win32u_syscalls.tsv, 1316 entries) must reproduce the tsv number
 * exactly. On real Windows this is the ground truth for which number our trampoline should use
 * for a given name; under Wine the table is Wine's own (see core_k_helpers's identical pattern),
 * so only the stub SHAPE (k_stub_valid) is checked there.
 * ============================================================================================ */
struct stub_case { const char *name; long number; };
static const struct stub_case g_stub_cases[] = {
    /* every ~55th line of tools/win32u_syscalls.tsv - spread across the full win32k range */
    { "NtUserGetThreadState",                       4096 },
    { "NtUserRegisterWindowMessage",                 4151 },
    { "NtUserUnhookWindowsHookEx",                   4206 },
    { "NtGdiSetBrushOrg",                            4261 },
    { "NtUserGetClipboardFormatName",                4316 },
    { "NtCloseCompositionInputSink",                 4371 },
    { "NtDCompositionTelemetryTouchInteractionBegin",4426 },
    { "NtGdiAddRemoteMMInstanceToDC",                4481 },
    { "NtGdiDdDDICloseAdapter",                      4536 },
    { "NtGdiDdDDIGetPresentHistory",                 4591 },
    { "NtGdiDdDDIQueryClockCalibration",             4646 },
    { "NtGdiDdDDIUpdateAllocationProperty",          4701 },
    { "NtGdiEnumFonts",                              4756 },
    { "NtGdiGetOPMRandomNumber",                     4811 },
    { "NtGdiScaleRgn",                               4866 },
    { "NtMITMinuserSetInputTransformOffset",         4921 },
    { "NtQueryCompositionSurfaceStatistics",         4976 },
    { "NtUserCallHwndOpt",                           5031 },
    { "NtUserDwmKernelShutdown",                     5086 },
    { "NtUserGetInternalWindowPos",                  5141 },
    { "NtUserGetWindowRgnEx",                        5196 },
    { "NtUserMinInitialize",                         5251 },
    { "NtUserSendInteractiveControlHapticsReport",   5306 },
    { "NtUserSetThreadLayoutHandles",                5361 },
};
#define N_STUB_CASES (int)(sizeof(g_stub_cases) / sizeof(g_stub_cases[0]))

TEST(sc_raw_syscall_stub_numbers_match_tsv) {
    int checked = 0;
    for (int i = 0; i < N_STUB_CASES; i++) {
        FARPROC p = t_proc_in("win32u.dll", g_stub_cases[i].name);
        if (!p) { t_note("win32u.dll missing export %s (build drift?)", g_stub_cases[i].name); continue; }
        CHECKF(k_stub_valid(p), "%s: prologue does not match the fixed syscall-stub shape", g_stub_cases[i].name);
        if (t_is_wine()) {
            t_note("%s: wine number=%ld (Wine's own win32u, not comparable to the 19041 table)",
                   g_stub_cases[i].name, k_stub_number(p));
            continue;
        }
        CHECKF(k_stub_number(p) == g_stub_cases[i].number,
               "%s got %ld want %ld", g_stub_cases[i].name, k_stub_number(p), g_stub_cases[i].number);
        /* every one of these is win32k.sys-serviced per tools/win32u_syscalls.tsv, so its number
         * must fall in the win32k table's number space (bit 12 set, bit 13 clear: [0x1000,0x1FFF])
         * exactly as KiSystemCall64's table-select arithmetic expects (see raw_bounds.c). */
        CHECK(g_stub_cases[i].number >= 0x1000 && g_stub_cases[i].number < 0x2000);
        checked++;
    }
    t_note("stub-number cross-check: %d/%d win32u exports present and verified", checked, N_STUB_CASES);
}

TEST(sc_raw_syscall_stub_rejects_non_syscall_export) {
    /* A plain kernel32 export is not a win32u syscall stub; the fixed-byte-pattern detector must
     * not be fooled by an arbitrary function's opening bytes (same check as core_k_helpers, kept
     * here too since this area's whole premise rests on k_stub_number being trustworthy). */
    FARPROC p = t_proc_in("kernel32.dll", "GetCurrentProcessId");
    REQUIRE(p);
    CHECK(!k_stub_valid(p));
    CHECK(k_stub_number(p) == -1);
}

/* ============================================================================================
 * 2. k_raw_syscall(number, ...) vs the real win32u.dll export: same number, same arguments, same
 * result. NtUserGetThreadState(0x0E): win32kfull.sys!NtUserGetThreadState with argument 1==0xE
 * simply returns 1 once the current thread has a PTHREADINFO (which the call guarantees by
 * converting the calling thread to a GUI thread as a side effect of the call itself, exactly the
 * side effect this whole corpus treats as expected and harmless) - no pointer args, no object
 * lookups, so it is safe to invoke both ways back to back with no window of state change between
 * them that could make the two calls disagree.
 * ============================================================================================ */
TEST(sc_raw_syscall_getthreadstate_0x0e_matches_stub) {
    REQUIRE(UN(NtUserGetThreadState));
    long num = raw_number_of("NtUserGetThreadState");
    REQUIRE(num >= 0);
    ULONG_PTR via_stub = UN(NtUserGetThreadState)(0x0E);
    ULONG_PTR via_raw  = (ULONG_PTR)k_raw_syscall((unsigned long)num, 0x0E, 0, 0, 0, 0, 0, 0, 0);
    if (t_is_wine()) {
        /* Wine's own NtUserGetThreadState implementation does not follow win32kfull.sys's real
         * 19041 branch for param 0x0E (observed: returns 0, not 1) - a Wine behaviour
         * difference, not a claim this area makes about win32kfull.sys itself. The raw-vs-stub
         * equivalence (the actual thing k_raw_syscall is being tested for) still holds either
         * way and is checked unconditionally below. */
        t_note("wine: NtUserGetThreadState(0x0E) = %llu (real Windows win32kfull.sys: always 1)", (unsigned long long)via_stub);
    } else {
        CHECKF(via_stub == 1, "stub call got %llu want 1 (win32kfull!NtUserGetThreadState param 0x0E branch)",
               (unsigned long long)via_stub);
    }
    EXPECT_EQ(via_raw, via_stub);
}

/* argument 1==9,10,11,15 are likewise plain reads of per-thread bitfields in the analysis (no probe,
 * no lock beyond EnterSharedCrit's own object lock) - table-driven since the expected VALUE is
 * version/state-dependent (foreground-queue membership, EN-/queue-status bits) but the invariant
 * "raw == stub" is not. */
TEST(sc_raw_syscall_getthreadstate_readonly_codes_match_stub) {
    REQUIRE(UN(NtUserGetThreadState));
    long num = raw_number_of("NtUserGetThreadState");
    REQUIRE(num >= 0);
    static const int codes[] = { 9, 10, 11, 12, 15 };
    for (int i = 0; i < (int)(sizeof(codes) / sizeof(codes[0])); i++) {
        ULONG_PTR via_stub = UN(NtUserGetThreadState)(codes[i]);
        ULONG_PTR via_raw  = (ULONG_PTR)k_raw_syscall((unsigned long)num, (ULONG_PTR)codes[i], 0, 0, 0, 0, 0, 0, 0);
        CHECKF(via_raw == via_stub, "code %d: raw=0x%llx stub=0x%llx", codes[i],
               (unsigned long long)via_raw, (unsigned long long)via_stub);
    }
}

/* ============================================================================================
 * 3. NtUserCallNoParam(0x0C): user32!GetMessagePos forwards to exactly this call, and code 0x0C only
 * ever reads state and returns it, so the call is side-effect-free - no args at all, so nothing to
 * get wrong.
 * ============================================================================================ */
TEST(sc_raw_syscall_callnoparam_0x0c_matches_stub_and_getmessagepos) {
    REQUIRE(UN(NtUserCallNoParam));
    REQUIRE(U(GetMessagePos));
    long num = raw_number_of("NtUserCallNoParam");
    REQUIRE(num >= 0);
    ULONG_PTR via_stub = UN(NtUserCallNoParam)(0x0C);
    ULONG_PTR via_raw  = (ULONG_PTR)k_raw_syscall((unsigned long)num, 0x0C, 0, 0, 0, 0, 0, 0, 0);
    EXPECT_EQ(via_raw, via_stub);
    /* Back-to-back with no intervening PeekMessage/GetMessage, GetMessagePos's own backing value
     * cannot have changed, so the public API must agree with our two direct calls too. */
    DWORD via_api = U(GetMessagePos)();
    EXPECT_EQ((ULONG_PTR)via_api, via_stub);
}

/* ============================================================================================
 * 4. NtUserQueryWindow(hwnd, 2) = QUERY_WINDOW_UNIQUE_THREAD_ID per win32kfull.sys!
 * NtUserQueryWindow's analysis (argument 2==2 branch calls PsGetThreadId on the window's owning
 * thread, or reads a cached copy of the same value - either way it is the window's owner thread
 * id and nothing else). k_make_window() creates a window this process just created and still
 * owns on the calling thread, so the only value analysis allows is GetCurrentThreadId().
 * ============================================================================================ */
TEST(sc_raw_syscall_querywindow_thread_id_matches_stub_and_owner) {
    HWND h = k_make_window("sc_raw_syscall_qw");
    if (!h) { t_skip("k_make_window failed (err %lu)", GetLastError()); return; }
    REQUIRE(UN(NtUserQueryWindow));
    long num = raw_number_of("NtUserQueryWindow");
    REQUIRE(num >= 0);
    DWORD via_stub = UN(NtUserQueryWindow)(h, 2);
    DWORD via_raw  = (DWORD)k_raw_syscall((unsigned long)num, (ULONG_PTR)h, 2, 0, 0, 0, 0, 0, 0);
    EXPECT_EQ(via_raw, via_stub);
    CHECKF(via_stub == GetCurrentThreadId(), "owner tid %lu, NtUserQueryWindow(.,2) got %lu",
           GetCurrentThreadId(), via_stub);
    DestroyWindow(h);
}
TEST(sc_raw_syscall_querywindow_process_id_matches_stub_and_owner) {
 /* argument 2==0: analysis's non-ghosting branch reads the cached owning PID the same way; for a
     * window we created ourselves this must be our own process id. */
    HWND h = k_make_window("sc_raw_syscall_qw2");
    if (!h) { t_skip("k_make_window failed (err %lu)", GetLastError()); return; }
    REQUIRE(UN(NtUserQueryWindow));
    long num = raw_number_of("NtUserQueryWindow");
    REQUIRE(num >= 0);
    DWORD via_stub = UN(NtUserQueryWindow)(h, 0);
    DWORD via_raw  = (DWORD)k_raw_syscall((unsigned long)num, (ULONG_PTR)h, 0, 0, 0, 0, 0, 0, 0);
    EXPECT_EQ(via_raw, via_stub);
    CHECKF(via_stub == GetCurrentProcessId(), "owner pid %lu, NtUserQueryWindow(.,0) got %lu",
           GetCurrentProcessId(), via_stub);
    DestroyWindow(h);
}
TEST(sc_raw_syscall_querywindow_invalid_hwnd_matches_stub) {
 /* win32kfull!NtUserQueryWindow's first action is ValidateHwnd; on failure it jumps
     * straight to the "return 0" exit (switchD_..._caseD_2), before touching any object - a bogus
     * but structurally-plausible HWND (never a real handle this process holds) is exactly the
     * documented early-reject case, not a kernel-range/NULL probe. */
    HWND bogus = (HWND)(ULONG_PTR)0x00110011;
    REQUIRE(UN(NtUserQueryWindow));
    long num = raw_number_of("NtUserQueryWindow");
    REQUIRE(num >= 0);
    DWORD via_stub = UN(NtUserQueryWindow)(bogus, 2);
    DWORD via_raw  = (DWORD)k_raw_syscall((unsigned long)num, (ULONG_PTR)bogus, 2, 0, 0, 0, 0, 0, 0);
    EXPECT_EQ(via_raw, via_stub);
    CHECKF(via_stub == 0, "ValidateHwnd should reject a handle we never received; got %lu", via_stub);
}

/* ============================================================================================
 * 5. NtGdiFlush(): zero arguments, flushes TEB.GdiTebBatch - idempotent on an empty batch (which
 * it is here: nothing queued a batched GDI command before this test runs in isolation), and the
 * real GdiFlush()/NtGdiFlush() pair is one of the two example win32u stubs the project's own
 * docs cite as "verified against win32u.dll!NtGdiGetEntry /asm and several others" for the fixed
 * stub shape, so it is a natural second win32k.sys-table member to cross-check (not just
 * NtUser-prefixed numbers).
 * ============================================================================================ */
TEST(sc_raw_syscall_gdiflush_matches_stub) {
    REQUIRE(UN(NtGdiFlush));
    long num = raw_number_of("NtGdiFlush");
    REQUIRE(num >= 0);
    BOOL via_stub = UN(NtGdiFlush)();
    BOOL via_raw  = (BOOL)k_raw_syscall((unsigned long)num, 0, 0, 0, 0, 0, 0, 0, 0);
    EXPECT_EQ(via_raw, via_stub);
}

/* ============================================================================================
 * 7. Two more zero-argument win32k.sys-table members, added to broaden "raw == stub" past
 * NtUser*GetThreadState/CallNoParam/QueryWindow and NtGdi*Flush:
 *
 * NtUserGetThreadDesktop (win32kbase.sys, 14 bytes total): analysis is a single tail
 * call to ApiSetEditionGetThreadDesktopEntryPoint() and nothing else - no args read, no object
 * touched directly by this function itself, so there is no argument to get wrong and no state
 * this call could corrupt; user32!GetThreadDesktop's own analysis is the same one-line forward
 * (), confirming the real caller passes zero arguments too.
 *
 * NtUserGetCaretBlinkTime (takes zero arguments, does
 * an access check, and returns either 0 or the blink time - whichever branch executes, it is a plain read with no pointer argument, so
 * raw-vs-stub identity holds regardless of which branch this process takes. user32!
 * GetCaretBlinkTime is the same one-line forward.
 * ============================================================================================ */
UDECL(ULONG_PTR, NtUserGetThreadDesktop, (void));
TEST(sc_raw_syscall_getthreaddesktop_matches_stub) {
    REQUIRE(UN(NtUserGetThreadDesktop));
    long num = raw_number_of("NtUserGetThreadDesktop");
    REQUIRE(num >= 0);
    ULONG_PTR via_stub = UN(NtUserGetThreadDesktop)();
    ULONG_PTR via_raw  = (ULONG_PTR)k_raw_syscall((unsigned long)num, 0, 0, 0, 0, 0, 0, 0, 0);
    EXPECT_EQ(via_raw, via_stub);
}

UDECL(ULONG_PTR, NtUserGetCaretBlinkTime, (void));
TEST(sc_raw_syscall_getcaretblinktime_matches_stub) {
    REQUIRE(UN(NtUserGetCaretBlinkTime));
    long num = raw_number_of("NtUserGetCaretBlinkTime");
    REQUIRE(num >= 0);
    ULONG_PTR via_stub = UN(NtUserGetCaretBlinkTime)();
    ULONG_PTR via_raw  = (ULONG_PTR)k_raw_syscall((unsigned long)num, 0, 0, 0, 0, 0, 0, 0, 0);
    EXPECT_EQ(via_raw, via_stub);
}

/* ============================================================================================
 * 6. The trampoline mechanics are table-agnostic: the same k_raw_syscall() also works for an
 * ordinary ntdll-table (table index "0") syscall, not just win32k ones. NtClose on a handle value
 * guaranteed never to have been returned by anything in this process (INVALID_HANDLE_VALUE itself
 * is a real sentinel user code never passes to NtClose) is the standard "handle lookup fails
 * early" case ntdll!NtClose's own callers rely on (e.g. CloseHandle(INVALID_HANDLE_VALUE) is a
 * documented-safe no-op pattern) - verified against the real ntdll export, not guessed.
 * ============================================================================================ */
TEST(sc_raw_syscall_ntclose_invalid_handle_matches_stub) {
    REQUIRE(UN(NtClose));
    long num = raw_number_of_in("ntdll.dll", "NtClose");
    REQUIRE(num >= 0);
    /* A multiple-of-4 value far above any handle this process holds. INVALID_HANDLE_VALUE (-1) is the
     * current-process pseudo handle and is NOT rejected by NtClose on Windows 10 22H2, so it cannot be
     * used as the "lookup fails" case. */
    HANDLE bogus = (HANDLE)(ULONG_PTR)0x0BAD0ADC;
    NTSTATUS via_stub = UN(NtClose)(bogus);
    NTSTATUS via_raw  = (NTSTATUS)k_raw_syscall((unsigned long)num, (ULONG_PTR)bogus,
                                                 0, 0, 0, 0, 0, 0, 0);
    EXPECT_EQ(via_raw, via_stub);
    if (t_is_wine()) {
        t_note("wine: NtClose(bogus) got %s (real Windows: STATUS_INVALID_HANDLE)", k_status_name(via_stub));
    } else {
        CHECKF((ULONG)via_stub == 0xC0000008 /* STATUS_INVALID_HANDLE */,
               "NtClose(bogus handle) got %s", k_status_name(via_stub));
    }
}
