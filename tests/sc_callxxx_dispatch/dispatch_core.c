/* sc_callxxx_dispatch - the generic, non-HWND dispatchers: NtUserCallNoParam, NtUserCallOneParam,
 * NtUserCallTwoParam. These three are plain "code selects a function pointer out of a win32kfull
 * table, call it with the given argument(s)" dispatchers: no handle validation, no object
 * locking. This file covers the dispatch envelope itself - valid-code range boundaries,
 * out-of-range behaviour, and direct-syscall-vs-public-API equality for the handful of codes
 * whose semantics are fully documented by a public user32 wrapper and are safe to invoke from a
 * conformance test (read-only query, or an idempotent/reversible toggle on state this process
 * already owns).
 *
 * Ground truth for every claim below is the behavior of win32kfull.sys!NtUserCallXxx observed on
 * build 19041.2965 plus the user32.dll callers that emit each code (cited per TEST). Signatures are not in any SDK header, so every argument shape here comes
 * from how the real caller builds it.
 *
 *   win32kfull!NtUserCallNoParam(uint code):
 *     code == 8            -> EnterSharedCrit(0); call _DrainThreadCoreMessagingCompletions() (not
 *                              in the generic table; excluded from the range tests below)
 *     code < 0x2c           -> EnterCrit(0,1); call gapfnNoParam[code](); codes 0,1 dereference
 *                              the returned pointer once (so a NULL result becomes NULL, not a
 *                              wild dereference), codes 2.. return the raw value.
 *     code >= 0x2c          -> no dispatch, returns 0. No UserSetLastError call on this path.
 *   win32kfull!NtUserCallOneParam(void* arg, uint code):
 *     code - 0x2c < 0x28 (i.e. 0x2c..0x53) -> dispatch; code==0x53 uses EnterSharedCrit instead of
 *                              EnterCrit (SetShellChangeNotifyWindow, confirmed below); code==0x4c
 *                              returns the raw pointer with no EnterCrit/dereference at all
 *                              (EnableSessionForMMCSS, confirmed below); every other in-range code
 *                              dereferences the result once iff code < 0x2e.
 *     else                   -> no dispatch, returns 0 immediately (no EnterCrit entered at all).
 *   win32kfull!NtUserCallTwoParam(void* a, void* b, uint code):
 *     code - 0x7e < 0x10 (0x7e..0x8d) -> EnterCrit(0,1); dispatch; return the raw value.
 *     else                   -> EnterCrit/LeaveCrit still happen, but the dispatch is skipped and
 *                              0 is returned.
 *
 * None of the three "out of range" paths call UserSetLastError, so GetLastError() is left exactly
 * as the caller set it - that is itself a checkable claim (not just "return value is 0").
 */
#include "t.h"
#include "k.h"

/* ---- direct syscall stubs (not in any mingw header) --------------------------------------- */
UDECL(void *, NtUserCallNoParam,  (DWORD code));
UDECL(void *, NtUserCallOneParam, (ULONG_PTR param, DWORD code));
UDECL(void *, NtUserCallTwoParam, (ULONG_PTR p1, ULONG_PTR p2, DWORD code));

/* =============================================================================================
 * 1. NtUserCallNoParam - valid range [0, 0x2c), out-of-range behaviour
 * ============================================================================================= */

/* code -> (name, public API, dereferences-return) for every NtUserCallNoParam code this corpus
 * found a real user32 caller for (see docs/KERNEL_TESTING.md "finding a syscall's signature";
 * each entry's function and callsite are the analysis citation). "deref" is true for codes 0 and 1
 * only (win32kfull!NtUserCallNoParam: ).
 */
struct noparam_row { DWORD code; const char *api; int deref; int safe_to_call; DWORD mask; };
static const struct noparam_row g_noparam[] = {
    { 0x00, "CreateMenu",                       1, 0, 0 }, /* creates a real HMENU - would leak a handle if called bare; test via CreateMenu() itself */
    { 0x01, "CreatePopupMenu",                  1, 0, 0 }, /* same */
    { 0x02, "AllowForegroundActivation",        0, 0, 0 }, /* _AllowForegroundActivation ORs 0x8000000 into the session-global gdwPUDFlags and is never cleared - not safe in the main exe */
    { 0x06, "DestroyCaret",                     0, 1, 0xff }, /* DestroyCaret returns bool (al only); no-op if this thread owns no caret */
    { 0x07, "DisableProcessWindowsGhosting",    0, 0, 0 }, /* irreversible for the process lifetime - CHILD only */
    { 0x0b, "GetInputDesktop",                  0, 0, 0 }, /* returns an HDESK; whether each call yields a fresh handle is not proven - note only */
    { 0x0c, "GetMessagePos",                    0, 1, 0xffffffffu }, /* _GetMessagePos: pure read of the per-thread last-message position */
    { 0x0e, "GetUnpredictedMessagePos",         0, 1, 0xffffffffu }, /* pure query */
    { 0x11, "IsQueueAttached",                  0, 1, 0xff }, /* _IsQueueAttached returns bool: 1 < queue->cThreads */
    { 0x16, "ReleaseCapture",                   0, 1, 0xffffffffu }, /* ReleaseCapture returns 1 (or 0 when queue flag 0x100000 is set) */
    { 0x29, "EnableMouseInPointerForThread",    0, 0, 0 }, /* per-thread opt-in that changes mouse message delivery for the calling thread - CHILD only */
    { 0x2b, "EnablePerMonitorMenuScaling",      0, 0, 0 }, /* per-process opt-in - CHILD only */
};
#define NOPARAM_N (sizeof(g_noparam)/sizeof(g_noparam[0]))

TEST(sc_callxxx_dispatch_noparam_table_present) {
    REQUIRE(UN(NtUserCallNoParam));
    t_note("NtUserCallNoParam codes exercised: %zu (table-driven)", (size_t)NOPARAM_N);
}

/* Codes >= 0x2c (the win32kfull table has 0x2c entries, is the only valid-range
 * check) must return NULL, and must not touch last error (no UserSetLastError on this path). */
TEST(sc_callxxx_dispatch_noparam_out_of_range_returns_null) {
    REQUIRE(UN(NtUserCallNoParam));
    DWORD codes[] = { 0x2c, 0x2d, 0x30, 0x40, 0xff, 0x1000, 0x7fffffff, 0xffffffff };
    for (size_t i = 0; i < sizeof(codes)/sizeof(codes[0]); i++) {
        SetLastError(0xCAFEu + (DWORD)i);
        void *r = UN(NtUserCallNoParam)(codes[i]);
        if (t_is_wine()) {
            t_note("code 0x%lx under Wine: returned %p, err=%lu (Wine's own last-error behaviour differs)",
                   (unsigned long)codes[i], r, GetLastError());
            continue;
        }
        CHECKF(r == NULL, "code 0x%lx returned %p, want NULL", (unsigned long)codes[i], r);
        CHECKF(GetLastError() == 0xCAFEu + (DWORD)i,
               "code 0x%lx last error got %lu, out-of-range NoParam must not set it", (unsigned long)codes[i], GetLastError());
    }
}

/* Code 8 is NOT in the generic gapfn table - it is special-cased before the range check
 * (win32kfull!NtUserCallNoParam: "if { EnterSharedCrit(0); return
 * _DrainThreadCoreMessagingCompletions(); }"). It is still < 0x2c, so a naive range test would
 * wrongly expect it to flow through the generic table; this test documents that it does not
 * crash and returns a plausible (small/zero) value instead of asserting a specific one, since
 * the function's own semantics are undocumented. */
TEST(sc_callxxx_dispatch_noparam_code_8_special_cased) {
    REQUIRE(UN(NtUserCallNoParam));
    void *r = UN(NtUserCallNoParam)(8);
    t_note("NtUserCallNoParam(8) [_DrainThreadCoreMessagingCompletions] returned %p", r);
}

/* Direct-dispatch-vs-public-API equality for every safe, side-effect-free/idempotent code. */
TEST(sc_callxxx_dispatch_noparam_matches_public_api) {
    REQUIRE(UN(NtUserCallNoParam));
    for (size_t i = 0; i < NOPARAM_N; i++) {
        const struct noparam_row *row = &g_noparam[i];
        if (!row->safe_to_call) continue;
        void *direct = UN(NtUserCallNoParam)(row->code);
        if (t_is_wine()) {
            /* Several of these user32 exports exist under Wine but are literally unimplemented
             * (FIXME + abort on invocation, e.g. GetInputDesktop) rather than merely behaving
             * differently - never call the public wrapper in this loop under Wine. */
            t_note("%s code 0x%lx under Wine: direct=%p (public API not called)", row->api, (unsigned long)row->code, direct);
            continue;
        }
        /* The public wrapper for each of these is itself nothing but this exact call (each
         * simply forwards to NtUserCallNoParam(<code>) with no extra logic), so calling both
         * back to back and comparing is a direct equality check. Only the bits the kernel function
         * actually defines are compared (bool functions define al only). */
        FARPROC papi = t_proc_in("user32.dll", row->api);
        if (!papi) { t_skip("user32.dll missing export %s", row->api); continue; }
        void *(WINAPI *fn)(void) = (void *(WINAPI *)(void))papi;
        void *viaapi = fn();
        CHECKF(((ULONG_PTR)direct & row->mask) == ((ULONG_PTR)viaapi & row->mask),
               "%s: direct=%p via-api=%p (code 0x%lx)", row->api, direct, viaapi, (unsigned long)row->code);
    }
}

/* DisableProcessWindowsGhosting is process-lifetime-irreversible (no re-enable call exists), so
 * it is only exercised in a CHILD, never the main test process. */
CHILD(sc_callxxx_dispatch_noparam_disable_ghosting_child) {
    (void)argc; (void)argv;
    FARPROC p = t_proc_in("win32u.dll", "NtUserCallNoParam");
    if (!p) { printf("NOTE sc_callxxx_dispatch_noparam_disable_ghosting_child: win32u.dll missing NtUserCallNoParam\n"); return 0; }
    void *(WINAPI *noparam)(DWORD) = (void *(WINAPI *)(DWORD))p;
    /* user32!DisableProcessWindowsGhosting is a void thunk, so the kernel function's return value
     * is not defined and is not asserted; the point is that the code dispatches without fault. */
    (void)noparam(0x07);
    return 0;
}
TEST(sc_callxxx_dispatch_noparam_disable_ghosting_in_child) {
    struct k_child_result r = k_child_run("sc_callxxx_dispatch_noparam_disable_ghosting_child", NULL, 10000);
    CHECK(r.exit_code == 0);
}

/* CreateMenu/CreatePopupMenu (codes 0,1) are the two codes that dereference the table result once
 * - tested through the real public API (which allocates a real HMENU we must destroy) rather than
 * bare NtUserCallNoParam, so the handle is not leaked. This also confirms the "codes 0/1
 * dereference" claim: a non-NULL HMENU is only possible if the raw table pointer was followed. */
TEST(sc_callxxx_dispatch_noparam_code0_code1_deref_produce_real_handles) {
    REQUIRE(U(CreateMenu));
    REQUIRE(U(CreatePopupMenu));
    REQUIRE(U(DestroyMenu));
    HMENU m0 = U(CreateMenu)();      /* code 0 */
    HMENU m1 = U(CreatePopupMenu)(); /* code 1 */
    CHECK(m0 != NULL);
    CHECK(m1 != NULL);
    CHECK(m0 != m1);
    if (m0) U(DestroyMenu)(m0);
    if (m1) U(DestroyMenu)(m1);
}

/* =============================================================================================
 * 2. NtUserCallOneParam - valid range [0x2c, 0x54)
 * ============================================================================================= */

struct oneparam_row { DWORD code; const char *api; int safe_to_call; };
/* safe_to_call here means "safe to run with the generic arg=0 loop"; rows with a dedicated test
 * below are marked 0 because they need a specific argument (a bare 0 would, e.g., set the system
 * double-click time to its default or the caret blink time to 0). */
static const struct oneparam_row g_oneparam[] = {
    { 0x2d, "GetSendMessageReceiver",     0 }, /* dedicated test: arg is a thread id */
    { 0x2e, "AllowSetForegroundWindow",   0 }, /* CHILD-only: mutates session foreground bookkeeping (gppiLockSFW, ptiLastWoken) */
    { 0x30, "DirectedYield",              0 }, /* yields this thread's quantum to another thread id - not a query, skip */
    { 0x31, "EnumClipboardFormats",       1 }, /* arg 0 with clipboard closed: returns 0 either way (see dedicated test for the error) */
    { 0x33, "GetKeyboardType",            0 }, /* dedicated test (session-dependent public path) */
    { 0x34, "GetProcessDefaultLayout",    0 }, /* dedicated test: arg is a DWORD* */
    { 0x36, "LockSetForegroundWindow",    0 }, /* dedicated tests: invalid codes in main, lock/unlock in a CHILD */
    { 0x39, "MessageBeep",                0 }, /* makes an audible sound - not a good guest in a test run */
    { 0x42, "ReplyMessage",               0 }, /* only meaningful inside a sent-message handler, not here */
    { 0x43, "SetCaretBlinkTime",          0 }, /* dedicated test restores the value */
    { 0x44, "SetDoubleClickTime",         0 }, /* dedicated test restores the value */
    { 0x49, "SwapMouseButton",            0 }, /* changes a system-wide accessibility setting - skip even with restore, too invasive */
    { 0x4e, "IsThreadMessageQueueAttached",0 }, /* dedicated tests: arg is a thread id */
};
#define ONEPARAM_N (sizeof(g_oneparam)/sizeof(g_oneparam[0]))

/* Below 0x2c and at/above 0x54, win32kfull!NtUserCallOneParam returns 0 WITHOUT entering its
 * critical section at all ( has no else-branch body other
 * than falling out of the function with the result still NULL from initialization) - so, like
 * NoParam, last error must be untouched. */
TEST(sc_callxxx_dispatch_oneparam_out_of_range_returns_null) {
    REQUIRE(UN(NtUserCallOneParam));
    DWORD codes[] = { 0, 1, 0x2b, 0x54, 0x55, 0x80, 0xffff, 0xffffffff };
    for (size_t i = 0; i < sizeof(codes)/sizeof(codes[0]); i++) {
        SetLastError(0xD00Du + (DWORD)i);
        void *r = UN(NtUserCallOneParam)(0, codes[i]);
        if (t_is_wine()) {
            t_note("code 0x%lx under Wine: returned %p, err=%lu (Wine's own range/last-error "
                   "behaviour differs)", (unsigned long)codes[i], r, GetLastError());
            continue;
        }
        CHECKF(r == NULL, "code 0x%lx returned %p, want NULL", (unsigned long)codes[i], r);
        CHECKF(GetLastError() == 0xD00Du + (DWORD)i,
               "code 0x%lx last error got %lu, out-of-range OneParam must not set it", (unsigned long)codes[i], GetLastError());
    }
}

/* Code 0x53 (SetShellChangeNotifyWindow) is the one OneParam code that takes the EnterSharedCrit
 * path instead of EnterCrit (win32kfull: ),
 * and code 0x4c (EnableSessionForMMCSS) is the one that skips EnterCrit AND the result-dereference
 * entirely (). Neither is safe to
 * invoke bare (0x53 touches the shell-notification window registration, 0x4c touches session-wide
 * MMCSS scheduling), so this test only confirms both codes are inside the generic valid-range
 * window and does not call them - the special-case branches are documented here so a reader of
 * the analysis comment above does not mistake them for being out of range.
 */
TEST(sc_callxxx_dispatch_oneparam_special_cased_codes_in_range) {
    CHECK(0x53 - 0x2c < 0x28);
    CHECK(0x4c - 0x2c < 0x28);
}

TEST(sc_callxxx_dispatch_oneparam_matches_public_api) {
    REQUIRE(UN(NtUserCallOneParam));
    for (size_t i = 0; i < ONEPARAM_N; i++) {
        const struct oneparam_row *row = &g_oneparam[i];
        if (!row->safe_to_call) continue;
        FARPROC papi = t_proc_in("user32.dll", row->api);
        if (!papi) { t_skip("user32.dll missing export %s", row->api); continue; }
        if (!strcmp(row->api, "GetSendMessageReceiver") || !strcmp(row->api, "GetProcessDefaultLayout")) {
            /* these pass a pointer, not a value - handled below with the real API only */
            continue;
        }
        ULONG_PTR arg = 0;
        if (!strcmp(row->api, "AllowSetForegroundWindow")) arg = GetCurrentProcessId();
        else if (!strcmp(row->api, "EnumClipboardFormats")) arg = 0;
        else if (!strcmp(row->api, "GetKeyboardType")) arg = 0;
        else if (!strcmp(row->api, "IsThreadMessageQueueAttached")) arg = GetCurrentThreadId();
        void *direct = UN(NtUserCallOneParam)(arg, row->code);
        if (t_is_wine()) {
            /* Observed: Wine's own user32 does not route several of these through win32u's
             * generic NtUserCallOneParam at all (its own reimplementation diverges), so
             * direct-dispatch-vs-public-API equality is not meaningful here, and at least one of
             * these exports (IsThreadMessageQueueAttached) is entirely unimplemented in Wine and
             * aborts the process if invoked - never call the public wrapper in this loop under
             * Wine. Windows is the authority for this equality claim. */
            t_note("%s code 0x%lx under Wine: direct=%p (public API not called)", row->api, (unsigned long)row->code, direct);
            continue;
        }
        UINT (WINAPI *fn)(ULONG_PTR) = (UINT (WINAPI *)(ULONG_PTR))papi;
        UINT viaapi = fn(arg);
        CHECKF((DWORD)(ULONG_PTR)direct == viaapi,
               "%s(0x%llx): direct=%p via-api=%u (code 0x%lx)", row->api, (unsigned long long)arg, direct, viaapi, (unsigned long)row->code);
    }
}

#define SKIP_UNDER_WINE(why) \
    do { if (t_is_wine()) { t_skip("Wine's win32u has its own NtUserCallXxx code numbering (%s) - " \
        "Windows is the authority", why); return; } } while (0)
#define LO8(p)  ((unsigned)((ULONG_PTR)(p) & 0xff))
#define LO32(p) ((DWORD)((ULONG_PTR)(p) & 0xffffffffu))
/* thread id that cannot belong to a thread: ids are multiples of 4 */
#define BOGUS_ID 0x3FFFFFFDu

/* SetCaretBlinkTime: the kernel stores the raw value in the shared server info with no
 * validation (only the winsta/desktop access checks can fail) and NtUserGetCaretBlinkTime returns
 * that same field, so any value read back must round-trip, including 0 and INFINITE. The original
 * value is restored even if a CHECK fails. */
TEST(sc_callxxx_dispatch_oneparam_caret_blink_time_roundtrip) {
    REQUIRE(U(GetCaretBlinkTime));
    REQUIRE(U(SetCaretBlinkTime));
    UINT old = U(GetCaretBlinkTime)();
    BOOL ok = U(SetCaretBlinkTime)(old);
    CHECK(ok);
    CHECKF(U(GetCaretBlinkTime)() == old, "got %u want %u", U(GetCaretBlinkTime)(), old);
    UINT probe = old == 345 ? 346 : 345;
    ok = U(SetCaretBlinkTime)(probe);
    UINT now = U(GetCaretBlinkTime)();
    U(SetCaretBlinkTime)(old);
    CHECK(ok);
    CHECKF(now == probe, "got %u want %u (stored raw, no clamping)", now, probe);
    CHECKF(U(GetCaretBlinkTime)() == old, "restore failed: %u vs %u", U(GetCaretBlinkTime)(), old);
}
/* The same through the raw dispatcher, code 0x43. */
TEST(sc_callxxx_dispatch_oneparam_caret_blink_time_direct) {
    SKIP_UNDER_WINE("SetCaretBlinkTime");
    REQUIRE(UN(NtUserCallOneParam));
    REQUIRE(U(GetCaretBlinkTime));
    UINT old = U(GetCaretBlinkTime)();
    UINT probe = old == 411 ? 412 : 411;
    void *r = UN(NtUserCallOneParam)(probe, 0x43);
    UINT now = U(GetCaretBlinkTime)();
    UN(NtUserCallOneParam)(old, 0x43);
    CHECKF(LO32(r) == 1, "NtUserCallOneParam(.,0x43) returned %p, _SetCaretBlinkTime returns 1", r);
    CHECKF(now == probe, "GetCaretBlinkTime=%u want %u", now, probe);
    CHECK(U(GetCaretBlinkTime)() == old);
}

/* _SetDoubleClickTime: 0 -> 500, > 5000 -> 5000, otherwise stored as is (win32kfull analysis), and
 * NtUserGetDoubleClickTime returns gdtDblClk. Original restored unconditionally. */
TEST(sc_callxxx_dispatch_oneparam_double_click_time_clamps) {
    REQUIRE(U(GetDoubleClickTime));
    REQUIRE(U(SetDoubleClickTime));
    UINT old = U(GetDoubleClickTime)();
    struct { UINT in, want; } rows[] = { { 0, 500 }, { 1, 1 }, { 777, 777 }, { 5000, 5000 }, { 5001, 5000 }, { 0x7fffffffu, 5000 } };
    for (size_t i = 0; i < sizeof(rows)/sizeof(rows[0]); i++) {
        BOOL ok = U(SetDoubleClickTime)(rows[i].in);
        UINT got = U(GetDoubleClickTime)();
        CHECKF(ok, "SetDoubleClickTime(%u) failed, err %lu", rows[i].in, GetLastError());
        if (t_is_wine()) { if (got != rows[i].want) t_note("Wine stores SetDoubleClickTime(%u) as %u (Windows clamps to %u)", rows[i].in, got, rows[i].want); }
        else CHECKF(got == rows[i].want, "SetDoubleClickTime(%u) -> %u want %u", rows[i].in, got, rows[i].want);
    }
    U(SetDoubleClickTime)(old);
    CHECK(U(GetDoubleClickTime)() == old);
}
TEST(sc_callxxx_dispatch_oneparam_double_click_time_direct_matches_api) {
    SKIP_UNDER_WINE("SetDoubleClickTime");
    REQUIRE(UN(NtUserCallOneParam));
    REQUIRE(U(GetDoubleClickTime));
    UINT old = U(GetDoubleClickTime)();
    void *r0 = UN(NtUserCallOneParam)(0, 0x44);
    UINT g0 = U(GetDoubleClickTime)();
    void *r1 = UN(NtUserCallOneParam)(9999, 0x44);
    UINT g1 = U(GetDoubleClickTime)();
    UN(NtUserCallOneParam)(old, 0x44);
    CHECK(LO32(r0) == 1 && LO32(r1) == 1);
    CHECKF(g0 == 500, "code 0x44 with 0 -> %u want 500", g0);
    CHECKF(g1 == 5000, "code 0x44 with 9999 -> %u want 5000", g1);
    CHECK(U(GetDoubleClickTime)() == old);
}

/* LockSetForegroundWindow: _LockSetForegroundWindow accepts only 1 (LOCK) and 2 (UNLOCK); every
 * other code takes the UserSetLastError(0x57) exit, and the immersive-restricted exit gives 5.
 * No state is touched on either path, so this is safe in the main exe. */
TEST(sc_callxxx_dispatch_oneparam_lsfw_invalid_code_fails) {
    SKIP_UNDER_WINE("LockSetForegroundWindow");
    REQUIRE(UN(NtUserCallOneParam));
    ULONG_PTR codes[] = { 0, 3, 4, 0x100, 0xffffffffu };
    for (size_t i = 0; i < sizeof(codes)/sizeof(codes[0]); i++) {
        SetLastError(0);
        void *r = UN(NtUserCallOneParam)(codes[i], 0x36);
        DWORD e = GetLastError();
        CHECKF(LO32(r) == 0, "LSFW code %llx returned %p", (unsigned long long)codes[i], r);
        CHECKF(e == 87 || e == 5, "LSFW code %llx err %lu want 87 (or 5 if immersive-restricted)", (unsigned long long)codes[i], e);
    }
}
/* UNLOCK by a process that does not hold the lock is rejected
 * without changing anything, unless this very process holds it - which only the CHILD below ever does. */
TEST(sc_callxxx_dispatch_oneparam_lsfw_unlock_without_lock_denied) {
    SKIP_UNDER_WINE("LockSetForegroundWindow");
    REQUIRE(UN(NtUserCallOneParam));
    SetLastError(0);
    void *r = UN(NtUserCallOneParam)(2, 0x36);
    DWORD e = GetLastError();
    CHECKF(LO32(r) == 0, "LSFW_UNLOCK without holding the lock returned %p", r);
    CHECKF(e == 5, "err %lu want ERROR_ACCESS_DENIED", e);
}

/* LSFW_LOCK is process-wide session UI state - only exercised in a CHILD. LOCK succeeds only if
 * CanForceForeground(process) and nobody else holds the lock; if it fails it must fail with 5,
 * and UNLOCK is then also denied (this process does not hold it). If it succeeds, a second LOCK
 * must fail (gppiLockSFW != 0) and UNLOCK must succeed. The lock dies with the process anyway. */
CHILD(sc_callxxx_dispatch_oneparam_lsfw_child) {
    (void)argc; (void)argv;
    if (t_is_wine()) { printf("NOTE lsfw child: Wine implements LockSetForegroundWindow differently - skipped\n"); return 0; }
    HMODULE u32 = LoadLibraryA("user32.dll");
    if (!u32) { printf("NOTE sc_callxxx_dispatch_oneparam_lsfw_child: user32.dll load failed\n"); return 0; }
    BOOL (WINAPI *Lsfw)(UINT) = (BOOL (WINAPI *)(UINT))GetProcAddress(u32, "LockSetForegroundWindow");
    if (!Lsfw) { printf("NOTE sc_callxxx_dispatch_oneparam_lsfw_child: missing export\n"); return 0; }
    int fails = 0;
    SetLastError(0);
    BOOL locked = Lsfw(1);
    DWORD e = GetLastError();
    if (!locked) {
        if (e != 5) { printf("FAIL lsfw child: LOCK failed with err %lu want 5\n", e); fails++; }
        SetLastError(0);
        if (Lsfw(2) || GetLastError() != 5) { printf("FAIL lsfw child: UNLOCK without holding the lock must fail with 5\n"); fails++; }
        return fails;
    }
    SetLastError(0);
    if (Lsfw(1) || GetLastError() != 5) { printf("FAIL lsfw child: second LOCK must fail with 5 (err %lu)\n", GetLastError()); fails++; }
    if (!Lsfw(2)) { printf("FAIL lsfw child: UNLOCK failed, err %lu\n", GetLastError()); fails++; }
    SetLastError(0);
    if (Lsfw(2) || GetLastError() != 5) { printf("FAIL lsfw child: UNLOCK after UNLOCK must fail with 5\n"); fails++; }
    return fails;
}
TEST(sc_callxxx_dispatch_oneparam_lock_set_foreground_window_in_child) {
    struct k_child_result r = k_child_run("sc_callxxx_dispatch_oneparam_lsfw_child", NULL, 10000);
    CHECK(r.exit_code == 0);
}

/* IsThreadMessageQueueAttached: _ThreadMessageQueueAttached(tid): tid==0 -> bool "queue shared by
 * more than one thread"; tid of a live thread -> bool "same queue as the caller"; unknown tid ->
 * UserSetLastError(0x57), FALSE. It is a bool function so only al is meaningful. */
TEST(sc_callxxx_dispatch_oneparam_thread_queue_attached_self_and_bogus) {
    SKIP_UNDER_WINE("IsThreadMessageQueueAttached aborts in Wine");
    REQUIRE(UN(NtUserCallOneParam));
    REQUIRE(U(PeekMessageA));
    MSG m; U(PeekMessageA)(&m, NULL, 0, 0, PM_NOREMOVE); /* make this a GUI thread with a queue */
    SetLastError(0);
    void *self = UN(NtUserCallOneParam)(GetCurrentThreadId(), 0x4e);
    CHECKF(LO8(self) == 1, "own thread id is trivially on the same queue, got %p", self);
    CHECKF(GetLastError() == 0, "own tid must not set an error, got %lu", GetLastError());
    SetLastError(0);
    void *zero = UN(NtUserCallOneParam)(0, 0x4e);
    CHECKF(LO8(zero) == 0, "tid 0 on an unattached thread, got %p", zero);
    SetLastError(0);
    void *bogus = UN(NtUserCallOneParam)(BOGUS_ID, 0x4e);
    CHECKF(LO8(bogus) == 0, "nonexistent tid, got %p", bogus);
    EXPECT_ERR(87);
    /* the public export is a bare tail jump to the same code */
    FARPROC p = t_proc_in("user32.dll", "IsThreadMessageQueueAttached");
    if (p) {
        BOOL (WINAPI *fn)(DWORD) = (BOOL (WINAPI *)(DWORD))p;
        CHECK((fn(GetCurrentThreadId()) & 0xff) == 1);
    }
}

struct att_ctx { HANDLE ready, done; DWORD tid; };
static DWORD WINAPI att_thread(LPVOID p) {
    struct att_ctx *a = (struct att_ctx *)p;
    MSG m;
    if (U(PeekMessageA)) U(PeekMessageA)(&m, NULL, 0, 0, PM_NOREMOVE);
    a->tid = GetCurrentThreadId();
    SetEvent(a->ready);
    WaitForSingleObject(a->done, 5000);
    return 0;
}
/* AttachThreadInput merges two queues: queue->cThreads (+0x188) becomes 2, so NoParam 0x11
 * (_IsQueueAttached: 1 < cThreads) and OneParam 0x4e with tid 0 flip to TRUE, and 0x4e with the
 * partner's tid (same queue) becomes TRUE. Detaching restores all three. Both threads are in this
 * process, so no foreign input state is touched; the helper exiting would detach it anyway. */
TEST(sc_callxxx_dispatch_attach_thread_input_flips_queue_attached) {
    SKIP_UNDER_WINE("IsQueueAttached / IsThreadMessageQueueAttached");
    REQUIRE(UN(NtUserCallNoParam));
    REQUIRE(UN(NtUserCallOneParam));
    REQUIRE(U(AttachThreadInput));
    REQUIRE(U(PeekMessageA));
    MSG m; U(PeekMessageA)(&m, NULL, 0, 0, PM_NOREMOVE);
    struct att_ctx a = { CreateEventA(NULL, TRUE, FALSE, NULL), CreateEventA(NULL, TRUE, FALSE, NULL), 0 };
    HANDLE th = CreateThread(NULL, 0, att_thread, &a, 0, NULL);
    if (!th || WaitForSingleObject(a.ready, 5000) != WAIT_OBJECT_0) { t_skip("helper thread did not start"); goto out; }
    CHECK(LO8(UN(NtUserCallNoParam)(0x11)) == 0);
    CHECK(LO8(UN(NtUserCallOneParam)(a.tid, 0x4e)) == 0);
    if (U(AttachThreadInput)(a.tid, GetCurrentThreadId(), TRUE)) {
        CHECK(LO8(UN(NtUserCallNoParam)(0x11)) == 1);
        CHECK(LO8(UN(NtUserCallOneParam)(0, 0x4e)) == 1);
        CHECK(LO8(UN(NtUserCallOneParam)(a.tid, 0x4e)) == 1);
        CHECK(U(AttachThreadInput)(a.tid, GetCurrentThreadId(), FALSE));
        CHECK(LO8(UN(NtUserCallNoParam)(0x11)) == 0);
        CHECK(LO8(UN(NtUserCallOneParam)(0, 0x4e)) == 0);
        CHECK(LO8(UN(NtUserCallOneParam)(a.tid, 0x4e)) == 0);
    } else {
        t_skip("AttachThreadInput failed, err %lu", GetLastError());
    }
out:
    SetEvent(a.done);
    if (th) { WaitForSingleObject(th, 5000); CloseHandle(th); }
    CloseHandle(a.ready); CloseHandle(a.done);
}

/* GetSendMessageReceiver: _GetSendMessageReceiver(tid) -> PtiFromThreadId; unknown thread or a
 * thread on another desktop -> UserSetLastError(0x57), 0. A live thread of this process that is not
 * blocked in SendMessage has no sender record (pti+0x1f8 == 0) -> 0 and no error. */
TEST(sc_callxxx_dispatch_oneparam_send_message_receiver_bogus_tid) {
    SKIP_UNDER_WINE("GetSendMessageReceiver");
    REQUIRE(UN(NtUserCallOneParam));
    SetLastError(0);
    void *r = UN(NtUserCallOneParam)(BOGUS_ID, 0x2d);
    CHECKF(r == NULL, "returned %p", r);
    EXPECT_ERR(87);
}
TEST(sc_callxxx_dispatch_oneparam_send_message_receiver_idle_self) {
    SKIP_UNDER_WINE("GetSendMessageReceiver");
    REQUIRE(UN(NtUserCallOneParam));
    REQUIRE(U(PeekMessageA));
    MSG m; U(PeekMessageA)(&m, NULL, 0, 0, PM_NOREMOVE);
    SetLastError(0x1357);
    void *r = UN(NtUserCallOneParam)(GetCurrentThreadId(), 0x2d);
    CHECKF(r == NULL, "no SendMessage in flight, got %p", r);
    EXPECT_EQ(GetLastError(), 0x1357u);
}

/* GetKeyboardType: _GetKeyboardType(flag) -> 0 type, 1 subtype, 2 function-key count; any other
 * flag except the two 0x7fff.. IME ones returns 0. In the active console / service session the
 * public wrapper is a tail call into this exact code, so values must match. */
TEST(sc_callxxx_dispatch_oneparam_get_keyboard_type_matches_api) {
    SKIP_UNDER_WINE("GetKeyboardType");
    REQUIRE(UN(NtUserCallOneParam));
    REQUIRE(U(GetKeyboardType));
    DWORD sid = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &sid);
    if (sid != WTSGetActiveConsoleSessionId()) { t_skip("not the console session: user32 uses a client-side keyboard type"); return; }
    for (int flag = 0; flag <= 2; flag++) {
        void *d = UN(NtUserCallOneParam)((ULONG_PTR)flag, 0x33);
        int v = U(GetKeyboardType)(flag);
        CHECKF(LO32(d) == (DWORD)v, "flag %d direct=%p api=%d", flag, d, v);
    }
    CHECKF(LO32(UN(NtUserCallOneParam)(0, 0x33)) != 0, "keyboard type 0 means no keyboard");
}
TEST(sc_callxxx_dispatch_oneparam_get_keyboard_type_unknown_flags_zero) {
    SKIP_UNDER_WINE("GetKeyboardType");
    REQUIRE(UN(NtUserCallOneParam));
    ULONG_PTR flags[] = { 3, 4, 100, 0x7ffffffd, 0x12345678 };
    for (size_t i = 0; i < sizeof(flags)/sizeof(flags[0]); i++) {
        void *d = UN(NtUserCallOneParam)(flags[i], 0x33);
        CHECKF(LO32(d) == 0, "flag 0x%llx returned %p, only 0,1,2,0x7ffffffe,0x7fffffff are defined", (unsigned long long)flags[i], d);
    }
}

/* EnumClipboardFormats: _EnumClipboardFormats fails with ERROR_CLIPBOARD_NOT_OPEN (0x58a) when the
 * calling thread did not open the clipboard (winsta clipboard owner thread != current). */
TEST(sc_callxxx_dispatch_oneparam_enum_clipboard_not_open) {
    SKIP_UNDER_WINE("EnumClipboardFormats");
    REQUIRE(UN(NtUserCallOneParam));
    SetLastError(0);
    void *r = UN(NtUserCallOneParam)(0, 0x31);
    DWORD e = GetLastError();
    CHECKF(LO32(r) == 0, "returned %p", r);
    CHECKF(e == 1418 || e == 5, "err %lu want ERROR_CLIPBOARD_NOT_OPEN (or 5 from CheckClipboardAccess)", e);
}
/* With the clipboard open, the direct sequence must equal the public one, format for format. */
TEST(sc_callxxx_dispatch_oneparam_enum_clipboard_open_matches_api) {
    SKIP_UNDER_WINE("EnumClipboardFormats");
    REQUIRE(UN(NtUserCallOneParam));
    REQUIRE(U(OpenClipboard));
    REQUIRE(U(CloseClipboard));
    REQUIRE(U(EnumClipboardFormats));
    if (!U(OpenClipboard)(NULL)) { t_skip("clipboard busy, err %lu", GetLastError()); return; }
    UINT api[64], dir[64]; int na = 0, nd = 0;
    UINT f = 0;
    while (na < 64 && (f = U(EnumClipboardFormats)(f)) != 0) api[na++] = f;
    f = 0;
    while (nd < 64) { f = LO32(UN(NtUserCallOneParam)(f, 0x31)); if (!f) break; dir[nd++] = f; }
    U(CloseClipboard)();
    CHECKF(na == nd, "public enumerated %d formats, direct %d", na, nd);
    for (int i = 0; i < na && i < nd; i++) CHECKF(api[i] == dir[i], "format %d: api=%u direct=%u", i, api[i], dir[i]);
}

/* GetProcessDefaultLayout: _GetProcessDefaultLayout probes the DWORD*, copies the process default
 * layout (win32 process +0x338) and returns 1 (error 12 for csrss). The sentinel proves the
 * kernel actually wrote through the pointer. */
TEST(sc_callxxx_dispatch_oneparam_get_process_default_layout) {
    SKIP_UNDER_WINE("GetProcessDefaultLayout");
    REQUIRE(UN(NtUserCallOneParam));
    REQUIRE(U(GetProcessDefaultLayout));
    DWORD d = 0xCCCCCCCCu, a = 0xDDDDDDDDu;
    void *r = UN(NtUserCallOneParam)((ULONG_PTR)&d, 0x34);
    BOOL ok = U(GetProcessDefaultLayout)(&a);
    CHECKF(LO32(r) == 1, "returned %p", r);
    CHECK(ok);
    CHECKF(d == a, "direct layout 0x%lx vs api 0x%lx", d, a);
    t_note("process default layout = 0x%lx", d);
}

/* Process-global opt-ins and foreground bookkeeping only ever run in a child. */
CHILD(sc_callxxx_dispatch_process_globals_child) {
    (void)argc; (void)argv;
    FARPROC pn = t_proc_in("win32u.dll", "NtUserCallNoParam");
    FARPROC po = t_proc_in("win32u.dll", "NtUserCallOneParam");
    if (!pn || !po) { printf("NOTE process_globals_child: win32u exports missing\n"); return 0; }
    if (t_is_wine()) { printf("NOTE process_globals_child: Wine's NtUserCallXxx code numbering differs - skipped\n"); return 0; }
    void *(WINAPI *np)(DWORD) = (void *(WINAPI *)(DWORD))pn;
    void *(WINAPI *op)(ULONG_PTR, DWORD) = (void *(WINAPI *)(ULONG_PTR, DWORD))po;
    int fails = 0;
    void *a = np(0x29), *b = np(0x29);
    printf("NOTE EnableMouseInPointerForThread direct: %p then %p\n", a, b);
    a = np(0x2b); b = np(0x2b);
    printf("NOTE EnablePerMonitorMenuScaling direct: %p then %p\n", a, b);
    /* AllowSetForegroundWindow: unknown pid -> 0 with 87 (or 5 when this process may not force
     * the foreground); our own pid -> TRUE, or 0 with 5 in the same restricted case. */
    SetLastError(0);
    void *r = op(BOGUS_ID, 0x2e);
    DWORD e = GetLastError();
    if ((DWORD)(ULONG_PTR)r != 0 || (e != 87 && e != 5)) { printf("FAIL ASFW bogus pid: ret %p err %lu\n", r, e); fails++; }
    SetLastError(0);
    r = op(GetCurrentProcessId(), 0x2e);
    e = GetLastError();
    if ((DWORD)(ULONG_PTR)r == 0 && e != 5) { printf("FAIL ASFW own pid failed with err %lu want 5\n", e); fails++; }
    return fails;
}
TEST(sc_callxxx_dispatch_process_globals_in_child) {
    struct k_child_result r = k_child_run("sc_callxxx_dispatch_process_globals_child", NULL, 10000);
    CHECK(r.exit_code == 0);
}

/* ---- NoParam queries/effects that need a window ------------------------------------------- */

/* ReleaseCapture returns 1 and clears the thread's capture (clearing capture). */
TEST(sc_callxxx_dispatch_noparam_release_capture_clears_capture) {
    SKIP_UNDER_WINE("ReleaseCapture");
    REQUIRE(UN(NtUserCallNoParam));
    REQUIRE(U(SetCapture)); REQUIRE(U(GetCapture)); REQUIRE(U(DestroyWindow));
    HWND h = k_make_window("sc_callxxx_capture");
    if (!h) { t_skip("k_make_window failed, err %lu", GetLastError()); return; }
    U(SetCapture)(h);
    CHECKF(U(GetCapture)() == h, "capture not set on our own window");
    void *r = UN(NtUserCallNoParam)(0x16);
    CHECKF(LO32(r) == 1, "ReleaseCapture returned %p", r);
    CHECKF(U(GetCapture)() == NULL, "capture still held by %p", (void *)U(GetCapture)());
    U(DestroyWindow)(h);
}

/* DestroyCaret: the call reports whether a caret existed. */
TEST(sc_callxxx_dispatch_noparam_destroy_caret_removes_caret) {
    SKIP_UNDER_WINE("DestroyCaret");
    REQUIRE(UN(NtUserCallNoParam));
    REQUIRE(U(CreateCaret)); REQUIRE(U(DestroyWindow)); REQUIRE(U(DestroyCaret));
    HWND h = k_make_window("sc_callxxx_caret");
    if (!h) { t_skip("k_make_window failed, err %lu", GetLastError()); return; }
    CHECK(U(CreateCaret)(h, NULL, 4, 4));
    void *r = UN(NtUserCallNoParam)(6);
    CHECKF(LO8(r) == 1, "DestroyCaret with a caret returned %p", r);
    /* the slot is free again: a second create works and the public destroy agrees */
    CHECK(U(CreateCaret)(h, NULL, 4, 4));
    CHECK(U(DestroyCaret)());
    U(DestroyWindow)(h);
}

/* GetMessagePos: pure read of the per-thread last-message point, repeatable without pumping. */
TEST(sc_callxxx_dispatch_noparam_message_pos_stable) {
    SKIP_UNDER_WINE("GetMessagePos");
    REQUIRE(UN(NtUserCallNoParam));
    DWORD a = LO32(UN(NtUserCallNoParam)(0x0c));
    DWORD b = LO32(UN(NtUserCallNoParam)(0x0c));
    CHECKF(a == b, "0x%lx vs 0x%lx without any message retrieved in between", a, b);
    CHECK(LO32(UN(NtUserCallNoParam)(0x0e)) == LO32(UN(NtUserCallNoParam)(0x0e)));
}

/* Sweep of every code just above NoParam's table: nothing dispatches, 0 comes back, last error
 * stays exactly as set (the analysis has no UserSetLastError on that path). */
TEST(sc_callxxx_dispatch_noparam_out_of_range_sweep) {
    SKIP_UNDER_WINE("NoParam table");
    REQUIRE(UN(NtUserCallNoParam));
    int bad = 0;
    for (DWORD c = 0x2c; c < 0x400; c++) {
        SetLastError(0x4D4D);
        void *r = UN(NtUserCallNoParam)(c);
        if (r != NULL || GetLastError() != 0x4D4D) { bad++; if (bad < 5) printf("  code 0x%lx: ret %p err %lu\n", c, r, GetLastError()); }
    }
    CHECKF(bad == 0, "%d codes in [0x2c,0x400) dispatched or touched last error", bad);
}
TEST(sc_callxxx_dispatch_oneparam_out_of_range_sweep) {
    SKIP_UNDER_WINE("OneParam table");
    REQUIRE(UN(NtUserCallOneParam));
    int bad = 0;
    for (DWORD c = 0x54; c < 0x400; c++) {
        SetLastError(0x4E4E);
        void *r = UN(NtUserCallOneParam)(0, c);
        if (r != NULL || GetLastError() != 0x4E4E) { bad++; if (bad < 5) printf("  code 0x%lx: ret %p err %lu\n", c, r, GetLastError()); }
    }
    for (DWORD c = 0; c < 0x2c; c++) {
        SetLastError(0x4E4E);
        void *r = UN(NtUserCallOneParam)(0, c);
        if (r != NULL || GetLastError() != 0x4E4E) { bad++; if (bad < 5) printf("  code 0x%lx: ret %p err %lu\n", c, r, GetLastError()); }
    }
    CHECKF(bad == 0, "%d out-of-range OneParam codes dispatched or touched last error", bad);
}

/* =============================================================================================
 * 3. NtUserCallTwoParam - valid range [0x7e, 0x8e)
 * ============================================================================================= */

/* The only code this corpus found a real, safe-to-invoke-twice caller for is RegisterLogonProcess
 * (0x83), and that one is NOT safe here: it flips a process-global "I am winlogon" flag
 * (gfLogonProcess) that nothing in this corpus can undo and that changes how the kernel treats
 * every subsequent syscall from this process (elevated window-station/desktop creation rights).
 * It is documented but deliberately never invoked, direct or via the public API, by this area. */
TEST(sc_callxxx_dispatch_twoparam_register_logon_process_documented_not_invoked) {
    CHECK(0x83 - 0x7e < 0x10); /* in range, per win32kfull!NtUserCallTwoParam */
    t_note("NtUserCallTwoParam(0x83) == user32!RegisterLogonProcess: sets process-global "
           "gfLogonProcess, irreversible for the process; not invoked by this corpus");
}

TEST(sc_callxxx_dispatch_twoparam_out_of_range_returns_null) {
    REQUIRE(UN(NtUserCallTwoParam));
    DWORD codes[] = { 0, 1, 0x7d, 0x8e, 0x8f, 0xff, 0xffffffff };
    for (size_t i = 0; i < sizeof(codes)/sizeof(codes[0]); i++) {
        void *r = UN(NtUserCallTwoParam)(0, 0, codes[i]);
        CHECKF(r == NULL, "code 0x%lx returned %p, want NULL", (unsigned long)codes[i], r);
    }
}
/* Unlike NoParam/OneParam, TwoParam's out-of-range path DOES enter and leave the critical section
 * (EnterCrit(0,1) runs unconditionally before the range check) - but it still never calls
 * UserSetLastError on this path, so last error is untouched regardless. Verified separately from
 * the NULL-return test above so a future analysis re-check of EnterCrit's side effects (if any) is
 * easy to isolate from the return-value claim. */
TEST(sc_callxxx_dispatch_twoparam_out_of_range_does_not_touch_last_error) {
    REQUIRE(UN(NtUserCallTwoParam));
    SetLastError(0x1337);
    UN(NtUserCallTwoParam)(0, 0, 0xdeadbeef);
    if (t_is_wine()) { t_note("out-of-range TwoParam under Wine: err=%lu (Wine's own EnterCrit path differs)", GetLastError()); return; }
    EXPECT_EQ(GetLastError(), 0x1337u);
}

/* TwoParam sweep: both sides of [0x7e,0x8e) must be dead (zero args, so even an accidental
 * dispatch would be harmless, but none may happen). */
TEST(sc_callxxx_dispatch_twoparam_out_of_range_sweep) {
    SKIP_UNDER_WINE("TwoParam table");
    REQUIRE(UN(NtUserCallTwoParam));
    int bad = 0;
    for (DWORD c = 0; c < 0x400; c++) {
        if (c >= 0x7e && c < 0x8e) continue;
        SetLastError(0x4F4F);
        void *r = UN(NtUserCallTwoParam)(0, 0, c);
        if (r != NULL || GetLastError() != 0x4F4F) { bad++; if (bad < 5) printf("  code 0x%lx: ret %p err %lu\n", c, r, GetLastError()); }
    }
    CHECKF(bad == 0, "%d out-of-range TwoParam codes dispatched or touched last error", bad);
}

/* user32!GetCursorPos == NtUserCallTwoParam(lpPoint, 1, 0x7f). The sentinel proves the kernel
 * wrote the POINT; the cursor may move between the two samples, so up to 5 retries. */
TEST(sc_callxxx_dispatch_twoparam_get_cursor_pos_matches_api) {
    SKIP_UNDER_WINE("GetCursorPos");
    REQUIRE(UN(NtUserCallTwoParam));
    REQUIRE(U(GetCursorPos));
    int equal = 0;
    for (int i = 0; i < 5 && !equal; i++) {
        POINT d = { 0x12345678, 0x12345678 }, a = { 0x12345678, 0x12345678 };
        void *r = UN(NtUserCallTwoParam)((ULONG_PTR)&d, 1, 0x7f);
        CHECKF(LO32(r) != 0, "GetCursorPos code returned %p", r);
        CHECK(U(GetCursorPos)(&a));
        CHECKF(d.x != 0x12345678 || d.y != 0x12345678, "kernel did not write the POINT");
        equal = d.x == a.x && d.y == a.y;
    }
    CHECKF(equal, "direct and public cursor positions never agreed in 5 tries");
}

/* user32!SetCaretPos == NtUserCallTwoParam(x, y, 0x88). Needs a caret owned by this thread; the
 * public GetCaretPos reads it back (values chosen so DPI virtualisation round-trips exactly). */
TEST(sc_callxxx_dispatch_twoparam_set_caret_pos_matches_api) {
    SKIP_UNDER_WINE("SetCaretPos");
    REQUIRE(UN(NtUserCallTwoParam));
    REQUIRE(U(CreateCaret)); REQUIRE(U(GetCaretPos)); REQUIRE(U(DestroyCaret)); REQUIRE(U(DestroyWindow));
    HWND h = k_make_window("sc_callxxx_caretpos");
    if (!h) { t_skip("k_make_window failed, err %lu", GetLastError()); return; }
    if (!U(CreateCaret)(h, NULL, 4, 4)) { t_skip("CreateCaret failed, err %lu", GetLastError()); U(DestroyWindow)(h); return; }
    void *r = UN(NtUserCallTwoParam)(40, 80, 0x88);
    POINT p = { -1, -1 };
    CHECK(U(GetCaretPos)(&p));
    CHECKF(LO32(r) != 0, "returned %p", r);
    CHECKF(p.x == 40 && p.y == 80, "caret at (%ld,%ld) want (40,80)", p.x, p.y);
    U(DestroyCaret)();
    U(DestroyWindow)(h);
}
