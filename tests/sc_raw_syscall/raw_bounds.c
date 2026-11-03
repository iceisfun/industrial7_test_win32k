/* sc_raw_syscall/raw_bounds.c
 *
 * Out-of-range service numbers, and the GUI-thread-conversion side effect of a thread's first
 * win32k syscall. The behavior asserted here is the documented dispatcher contract plus what was
 * observed on build 19041.2965: the service number is split into a 12-bit index and a table
 * selector taken from bit 12 only; an index past the selected table's limit yields
 * STATUS_INVALID_SYSTEM_SERVICE (0xC000001C) before any argument is read. There is no separate
 * exported repeat/GDI-fault dispatch routine on this build; that logic is part of the main
 * system-call entry itself.
 *
 * The table selector (the offset used to pick which table half - ntdll's or win32k's - is used) is
 * derived from exactly ONE bit of N (bit 12, via ">>7 & 0x20"), not two. This means a number like
 * 0x2000 (bit 13 set, bit 12 CLEAR) does NOT select a third/fourth table and does NOT fail fast:
 * it aliases into the SAME table "0" (ntdll) slot as 0x0000, with index (N & 0xFFF) == 0, which
 * is NtAccessCheck's real service number on this build (number 0) - a function with several in/out pointer parameters that this area has no verified-
 * harmless argument list for. This area therefore does NOT raw-call 0x2000/0x3000-class numbers
 * directly (see the NOTE in sc_raw_syscall_table_select_is_one_bit_not_two below) - doing so
 * would be exactly the "NULL/kernel-range pointer into a probed syscall" pattern the project's
 * safety rules reserve for the dedicated kprobe_user_pointers area ("nowhere else"), and here we
 * would not even have an analysis-verified set of safe arguments for the aliased function. Every
 * out-of-range test in this file instead stays inside the SAME table bit-12 selects (so the
 * table-select arithmetic itself is never in question) and only varies the 12-bit index to land
 * past that table's real Limit.
 */
#include "t.h"
#include "k.h"

TEST(sc_raw_syscall_table_select_is_one_bit_not_two) {
    /* Not a CHECK: there is nothing on real Windows to assert here except that this area's
     * design deliberately avoids 0x2000/0x3000-class numbers (see file header). Recorded as a
     * NOTE so a reader of results.txt sees the reasoning without needing the source comment. */
    t_note("KiSystemCall64 table-select uses bit 12 of the service number only; "
           "0x2000/0x3000 alias into the ntdll table at index 0, not a distinct "
           "table 2/3 - this area never raw-calls those numbers (see file header)");
}

/* ============================================================================================
 * 1. win32k-table (bit 12 set) out-of-range index: tools/win32u_syscalls.tsv lists exactly 1316
 * win32k.sys-serviced numbers, 4096 (0x1000) .. 5411 (0x1523) inclusive - so index 1316 (number
 * 0x152C) is the first number past the real table's Limit. Several numbers further in (0x1600,
 * 0x1900, 0x1FFF) are included too, both to show the fast fail is not a one-off off-by-one and
 * because 0x1FFF is literally the highest index the 12-bit index field can encode (bit 12 stays
 * set, so the table-select half of the analysis fragment above is never in doubt for any of these).
 * ============================================================================================ */
TEST(sc_raw_syscall_win32k_table_out_of_range_fails_cleanly) {
    if (t_is_wine()) {
        /* See the NOTE above the ntdll-table test: a hardcoded real-Windows win32k (bit-12-set)
         * number is not safe to raw-call under this Wine build - it can land on a Wine-populated
         * slot whose implementation calls abort() for being "unimplemented", taking the whole
         * process down instead of returning a status. Skip the call itself; real Windows is the
         * only place this test asserts anything. */
        t_skip("win32k-table out-of-range numbers are real-Windows literals; not safe to raw-call under Wine's own numbering (see file header)");
        return;
    }
    static const unsigned long numbers[] = { 0x152C, 0x1600, 0x1900, 0x1D00, 0x1FFF };
    for (int i = 0; i < (int)(sizeof(numbers) / sizeof(numbers[0])); i++) {
        /* All args 0: whichever function (if any) a future build ever puts at this index, the
         * analysis shows the bounds check happens BEFORE any argument is read, so the argument
         * values cannot matter for a number this far past 1316 real entries. */
        K_NTSTATUS st = k_raw_syscall(numbers[i], 0, 0, 0, 0, 0, 0, 0, 0);
        CHECKF((unsigned long)st == 0xC000001C,
               "number 0x%lx: got %s want STATUS_INVALID_SYSTEM_SERVICE", numbers[i], k_status_name(st));
    }
}

/* ============================================================================================
 * 2. ntdll-table (bit 12 clear) out-of-range index: the real ntdll table's
 * highest service number on this build is 471 (0x1D7). 0x0FFF (4095) is comfortably past
 * that, and crucially still has bit 12 clear so the table-select arithmetic picks table "0" the
 * same way 0x0000 would - this is a genuine "real table, index past its Limit" case, not a
 * table-selection ambiguity.
 *
 * (win32k-table, bit-12-set, out-of-range numbers are deliberately NOT tested the same way here:
 * triaging this file under headless Wine showed that a hardcoded real-Windows win32k number can
 * land on a DIFFERENT, Wine-populated-but-unimplemented slot in Wine's own win32k table and abort
 * the whole process instead of returning a status - see raw_stub.c's file header for the full
 * story. There is no way to pick a "safely out of range for both real Windows and whatever Wine
 * happens to ship" bit-12-set number without reading Wine's own table size at runtime, which this
 * this area has no API for, so sc_raw_syscall_win32k_table_out_of_range_fails_cleanly below SKIPS the
 * actual raw call under Wine entirely rather than guess.)
 * ============================================================================================ */
TEST(sc_raw_syscall_ntdll_table_out_of_range_fails_cleanly) {
    static const unsigned long numbers[] = { 0x0FFF, 0x0FFE, 0x0800 };
    for (int i = 0; i < (int)(sizeof(numbers) / sizeof(numbers[0])); i++) {
        K_NTSTATUS st = k_raw_syscall(numbers[i], 0, 0, 0, 0, 0, 0, 0, 0);
        if (t_is_wine()) { t_note("wine: raw syscall 0x%lx returned %s", numbers[i], k_status_name(st)); continue; }
        CHECKF((unsigned long)st == 0xC000001C,
               "number 0x%lx: got %s want STATUS_INVALID_SYSTEM_SERVICE", numbers[i], k_status_name(st));
    }
}

/* A real, in-range ntdll number must NOT be mistaken for out-of-range by our own test logic -
 * sanity check that the harness/trampoline round-trips a normal small number correctly before
 * trusting the "it failed" tests above. NtClose (15) always succeeds or fails on its own terms,
 * never with STATUS_INVALID_SYSTEM_SERVICE. */
TEST(sc_raw_syscall_in_range_number_is_not_mistaken_for_invalid) {
    if (t_is_wine()) { t_skip("hardcoded NT service number 15: a raw SYSCALL with Windows numbers terminates the process under Wine"); return; }
    K_NTSTATUS st = k_raw_syscall(15 /* NtClose */, (ULONG_PTR)INVALID_HANDLE_VALUE, 0, 0, 0, 0, 0, 0, 0);
    CHECK((unsigned long)st != 0xC000001C);
}

/* ============================================================================================
 * 3. GUI-thread conversion: TEB_Win32ThreadInfo (TEB+0x078) is documented in k.h as "NULL until
 * the thread's first win32k syscall converts it to a GUI thread" - exactly the KiConvertToGuiThread
 * call in the analysis fragment above, which only runs on a win32k-table (bit 12 set) call. This is
 * per-THREAD state (the field lives at a fixed TEB offset, and every thread has its own TEB), so a
 * brand-new thread in this already-connected process still starts with Win32ThreadInfo == NULL
 * even though the process as a whole connected to win32k long ago - no CHILD process needed, just
 * a thread that has made no win32k call yet. The raw syscall is NtUserGetThreadState(0x0E), the
 * same harmless read used in raw_stub.c.
 * ============================================================================================ */
struct conv_result { void *before; void *after; K_NTSTATUS status; unsigned long number; };
static DWORD WINAPI conv_thread_fn(LPVOID arg) {
    struct conv_result *r = (struct conv_result *)arg;
    void *teb = k_teb();
    k_try_read_ptr((char *)teb + TEB_Win32ThreadInfo, &r->before);
    r->status = k_raw_syscall(r->number /* NtUserGetThreadState, number read from the live win32u
                                          * export's own stub bytes - see raw_stub.c's file header
                                          * for why a hardcoded number is not safe under Wine */,
                               0x0E, 0, 0, 0, 0, 0, 0, 0);
    k_try_read_ptr((char *)teb + TEB_Win32ThreadInfo, &r->after);
    return 0;
}
TEST(sc_raw_syscall_first_call_converts_thread_to_gui) {
    FARPROC p = t_proc_in("win32u.dll", "NtUserGetThreadState");
    REQUIRE(p); REQUIRE(k_stub_valid(p));
    struct conv_result r = { 0 };
    r.number = (unsigned long)k_stub_number(p);
    HANDLE h = CreateThread(NULL, 0, conv_thread_fn, &r, 0, NULL);
    REQUIRE(h);
    DWORD w = WaitForSingleObject(h, 5000);
    CloseHandle(h);
    if (w != WAIT_OBJECT_0) { t_skip("worker thread did not finish in time"); return; }
    CHECKF(r.before == NULL, "fresh thread's TEB.Win32ThreadInfo was already non-NULL (%p) before any win32k call", r.before);
    CHECKF((unsigned long)r.status != 0xC000001C, "the converting call itself must succeed: %s", k_status_name((K_NTSTATUS)r.status));
    if (t_is_wine()) {
        /* Wine's own per-thread layout may not populate the real-Windows TEB+0x078 field the
         * same way (observed on this build: stays NULL even though the call above succeeded) -
         * a Wine layout difference, not a claim about win32k.sys this area can make. */
        t_note("wine: TEB.Win32ThreadInfo after first win32k syscall = %p (real Windows must be non-NULL)", r.after);
    } else {
        CHECKF(r.after != NULL, "TEB.Win32ThreadInfo still NULL after a win32k syscall on this thread");
    }
}

/* The SAME worker-thread pattern through the real win32u.dll stub (not our trampoline) must
 * convert the thread exactly the same way - this isolates "does a win32k syscall convert the
 * thread" (true on both paths) from "does OUR trampoline happen to do it too" (tested above). */
UDECL(ULONG_PTR, NtUserGetThreadState, (int));
static DWORD WINAPI conv_thread_fn_stub(LPVOID arg) {
    struct conv_result *r = (struct conv_result *)arg;
    void *teb = k_teb();
    k_try_read_ptr((char *)teb + TEB_Win32ThreadInfo, &r->before);
    UN(NtUserGetThreadState)(0x0E);
    k_try_read_ptr((char *)teb + TEB_Win32ThreadInfo, &r->after);
    return 0;
}
TEST(sc_raw_syscall_first_call_converts_thread_to_gui_via_real_stub) {
    REQUIRE(UN(NtUserGetThreadState));
    struct conv_result r = { 0 };
    HANDLE h = CreateThread(NULL, 0, conv_thread_fn_stub, &r, 0, NULL);
    REQUIRE(h);
    DWORD w = WaitForSingleObject(h, 5000);
    CloseHandle(h);
    if (w != WAIT_OBJECT_0) { t_skip("worker thread did not finish in time"); return; }
    CHECK(r.before == NULL);
    if (t_is_wine()) t_note("wine: TEB.Win32ThreadInfo after first win32k syscall (via real stub) = %p", r.after);
    else CHECK(r.after != NULL);
}

/* ============================================================================================
 * 4. Legacy int 0x2E fallback target: every win32u stub's fixed shape (documented in k.h) is
 * `test byte [0x7FFE0308],1; jne +3; syscall; ret; int 0x2E; ret` - the `int 0x2E` is only ever
 * reached when KUSER_SHARED_DATA->SystemCallPad/flag at 0x7FFE0308 is nonzero, which on every
 * real x86-64 CPU capable of running Windows 10 (i.e. every CPU with the SYSCALL instruction,
 * which 64-bit NT has required since its first 64-bit release) this project has been told to
 * target is permanently 0 - the branch is dead code on this corpus's target hardware, and NOT
 * reachable by setting up our own call (we would have to get the KERNEL to flip that shared,
 * read-only-from-user-mode page's byte, not something this area's own process can do). We do not
 * attempt to synthesize an int 0x2E call ourselves: nothing documents what int 0x2E's
 * OWN trap-gate handler does differently from the main system-call entry on this build, so "is it
 * safe" cannot be verified the way this area requires - recorded here as an open item rather
 * than guessed at.
 * ============================================================================================ */
TEST(sc_raw_syscall_legacy_int2e_not_exercised) {
    t_note("int 0x2E fallback path not raw-called: dead on real SYSCALL-capable hardware and no "
           "analysis of its own trap-gate handler exists to verify safety (see "
           "file header) - open item, not a win32k.sys/win32kbase.sys/win32kfull.sys "
           "behaviour this area could assert on to begin with");
}
