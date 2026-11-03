/* gui_thread_conversion / guires: GetGuiResources, i.e. win32kfull!NtUserGetGuiResources(hProcess, flags).
 *
 * Observed rules (19041):
 *   - EnterSharedCrit first: the call itself converts a pristine caller.
 *   - flags > 4 (unsigned)                 -> ERROR_INVALID_PARAMETER (0x57), result 0
 *   - hProcess == -1 (current process)     -> counters of PsGetCurrentProcessWin32Process():
 *         0 = GDI objects (+0x3c)  1 = USER objects (+0x44)  2 = GDI peak (+0x40)  4 = USER peak (+0x48)
 *         flag 3 matches no counter: result 0 and the last error is left alone
 *   - hProcess == -2                       -> session-wide: the session GDI handle count and its peak
 *   - other handles: ObReferenceObjectByHandle(h, 0x1000 = PROCESS_QUERY_LIMITED_INFORMATION, PsProcessType);
 *         failure or a process of another session -> 0x57, result 0; a process that never used win32k reads 0 and leaves the last error alone (Windows 10)
 * Success paths never touch the last error. */
#include "gtc.h"

#define BAD 0x57
#define MAGIC 0x13579

static ULONG gr(HANDLE h, ULONG flags) {
    DWORD keep = GetLastError();                   /* the lookup (module load) may change the last error; callers set a marker first */
    NtUserGetGuiResources_pfn f = W(NtUserGetGuiResources);
    SetLastError(keep);
    return f ? f(h, flags) : 0xFFFFFFFE;
}
#define SELF ((HANDLE)(LONG_PTR)-1)
#define SESSION ((HANDLE)(LONG_PTR)-2)

SCEN(guires_flags_self_valid) {
    static const ULONG flags[] = { 0, 1, 2, 4 };
    for (int i = 0; i < 4; i++) {
        SetLastError(MAGIC);
        ULONG v = gr(SELF, flags[i]);
        GCF(v < 0x100000, "flags %lu -> %lu", flags[i], v);
        GERR(MAGIC);                      /* success path leaves the error alone */
    }
}

SCEN(guires_flag3_returns_zero_without_error) {
    SetLastError(MAGIC);
    GEQ(gr(SELF, 3), 0);
    GERR(MAGIC);
    SetLastError(MAGIC);
    GEQ(gr(SESSION, 3), 0);
    GERR(MAGIC);
}

SCEN(guires_flags_above_four_invalid) {
    static const ULONG flags[] = { 5, 6, 7, 8, 9, 15, 16, 0xFF, 0x100, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFE, 0xFFFFFFFF };
    for (size_t i = 0; i < sizeof flags / sizeof flags[0]; i++) {
        SetLastError(MAGIC);
        GCF(gr(SELF, flags[i]) == 0, "flags 0x%lx", flags[i]);
        GERR(BAD);
        SetLastError(MAGIC);
        GCF(gr(SESSION, flags[i]) == 0, "session flags 0x%lx", flags[i]);   /* the flags check precedes the -2 branch */
        GERR(BAD);
        SetLastError(MAGIC);
        GCF(gr(NULL, flags[i]) == 0, "NULL handle flags 0x%lx", flags[i]);
        GERR(BAD);
    }
}

SCEN(guires_peak_not_below_current) {
    ULONG g = gr(SELF, 0), gp = gr(SELF, 2), u = gr(SELF, 1), up = gr(SELF, 4);
    GCF(gp >= g, "GDI peak %lu < current %lu", gp, g);
    GCF(up >= u, "USER peak %lu < current %lu", up, u);
    t_note("GDI=%lu peak=%lu USER=%lu peak=%lu", g, gp, u, up);
}

SCEN(guires_real_self_handle_matches_pseudo_handle) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentProcessId());
    if (!h) { t_skip("OpenProcess failed %lu", GetLastError()); return; }
    for (ULONG fl = 0; fl <= 4; fl++) {
        ULONG a = gr(SELF, fl);
        SetLastError(MAGIC);
        ULONG b = gr(h, fl);
        GCF(a == b, "flags %lu: pseudo %lu vs real %lu", fl, a, b);
        GERR(MAGIC);
    }
    CloseHandle(h);
}

SCEN(guires_current_process_pseudo_handle_is_minus_one) {
    GEQ((ULONG_PTR)GetCurrentProcess(), (ULONG_PTR)-1);
    GEQ(gr(GetCurrentProcess(), 0), gr(SELF, 0));
}

SCEN(guires_handle_without_query_limited_denied) {
    HANDLE h = OpenProcess(PROCESS_VM_READ, FALSE, GetCurrentProcessId());
    if (!h) { t_skip("OpenProcess(VM_READ) failed %lu", GetLastError()); return; }
    SetLastError(MAGIC);
    /* ObReferenceObjectByHandle demands 0x1000; a handle that only carries PROCESS_VM_READ fails the access check */
    GEQ(gr(h, 0), 0);
    GERR(BAD);
    CloseHandle(h);
}

SCEN(guires_handle_with_query_limited_only_accepted) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentProcessId());
    if (!h) { t_skip("OpenProcess failed %lu", GetLastError()); return; }
    SetLastError(MAGIC);
    ULONG v = gr(h, 1);
    GCF(v < 0x100000, "%lu", v);
    GERR(MAGIC);
    CloseHandle(h);
}

SCEN(guires_handle_query_information_only_note) {
    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, GetCurrentProcessId());
    if (!h) { t_skip("OpenProcess failed %lu", GetLastError()); return; }
    SetLastError(MAGIC);
    ULONG v = gr(h, 0);
    /* 0x400 does not contain the 0x1000 bit the kernel asks for, but access-mask mapping is version policy */
    t_note("PROCESS_QUERY_INFORMATION-only handle: result %lu error %lu", v, GetLastError());
    CloseHandle(h);
}

SCEN(guires_invalid_handles) {
    HANDLE hs[] = { NULL, (HANDLE)4, (HANDLE)0x7FFFFFF0, (HANDLE)0xFFFFFFFF, (HANDLE)(LONG_PTR)-3, (HANDLE)(LONG_PTR)-4, (HANDLE)(LONG_PTR)0x100000000LL };
    for (size_t i = 0; i < sizeof hs / sizeof hs[0]; i++) {
        SetLastError(MAGIC);
        GCF(gr(hs[i], 0) == 0, "handle %p", hs[i]);
        GCF(GetLastError() == BAD, "handle %p err %lu", hs[i], GetLastError());
    }
}

SCEN(guires_closed_handle) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentProcessId());
    if (!h) { t_skip("OpenProcess failed"); return; }
    CloseHandle(h);
    SetLastError(MAGIC);
    GEQ(gr(h, 0), 0);
    GERR(BAD);
}

SCEN(guires_wrong_object_type_handles) {
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    HANDLE th = NULL;
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &th, 0, FALSE, DUPLICATE_SAME_ACCESS);
    HANDLE mx = CreateMutexA(NULL, FALSE, NULL);
    /* PsProcessType mismatch -> STATUS_OBJECT_TYPE_MISMATCH -> 0x57 */
    HANDLE hs[3] = { ev, th, mx };
    for (int i = 0; i < 3; i++) {
        if (!hs[i]) continue;
        SetLastError(MAGIC);
        GCF(gr(hs[i], 0) == 0, "object %d", i);
        GERR(BAD);
        CloseHandle(hs[i]);
    }
}

SCEN(guires_session_wide_counts) {
    ULONG sg = gr(SESSION, 0), su = gr(SESSION, 1), sgp = gr(SESSION, 2), sup = gr(SESSION, 4);
    ULONG g = gr(SELF, 0), u = gr(SELF, 1);
    SetLastError(MAGIC);
    gr(SESSION, 0);
    GERR(MAGIC);
    /* a session counts at least what one of its processes owns */
    GCF(sg >= g, "session GDI %lu < own %lu", sg, g);
    GCF(su >= u, "session USER %lu < own %lu", su, u);
    GCF(sgp >= sg, "session GDI peak %lu < current %lu", sgp, sg);
    GCF(sup >= su, "session USER peak %lu < current %lu", sup, su);
    GC(su > 0);                           /* the interactive session always has desktop/window objects */
    t_info("session GDI=%lu peak=%lu USER=%lu peak=%lu", sg, sgp, su, sup);
}

/* ---- other processes -------------------------------------------------------------------- */

SCEN_BODY(idle_wait) {
    /* helper process: stay alive (bounded) without ever touching win32k */
    for (int i = 0; i < 160; i++) Sleep(50);
}
SCEN_BODY(idle_converted) {
    NtUserGetThreadState_pfn f = W(NtUserGetThreadState);
    if (f) f(0xe);
    HANDLE ev = OpenEventA(EVENT_MODIFY_STATE, FALSE, gtc_arg());
    if (ev) { SetEvent(ev); CloseHandle(ev); }
    for (int i = 0; i < 160; i++) Sleep(50);
}

SCEN(guires_other_process_without_win32k_calls_reports_zero) {
    HANDLE p = gtc_spawn("idle_wait");
    if (!p) { t_skip("cannot spawn helper"); return; }
    Sleep(300);
    /* The helper never made a win32k call. On Windows 10 every counter then reads zero and the call neither fails nor sets
     * the last error (an invalid-parameter error is reserved for bad flags, see guires_flags_above_four_invalid). */
    for (ULONG fl = 0; fl <= 4; fl++) {
        SetLastError(MAGIC);
        GCF(gr(p, fl) == 0, "flags %lu", fl);
        GERR(MAGIC);
    }
    TerminateProcess(p, 0);
    WaitForSingleObject(p, 5000);
    CloseHandle(p);
}

SCEN(guires_other_process_after_conversion_ok) {
    char name[96], sel[128];
    snprintf(name, sizeof name, "Local\\win32kt_gtc_%lu", GetCurrentProcessId());
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, name);
    snprintf(sel, sizeof sel, "idle_converted:%s", name);
    HANDLE p = gtc_spawn(sel);
    if (!p || !ev) { t_skip("cannot spawn helper"); return; }
    if (WaitForSingleObject(ev, 8000) != WAIT_OBJECT_0) { t_skip("helper did not convert in time"); TerminateProcess(p, 0); CloseHandle(p); CloseHandle(ev); return; }
    for (ULONG fl = 0; fl <= 4; fl++) {
        SetLastError(MAGIC);
        ULONG v = gr(p, fl);
        GCF(v < 0x100000, "flags %lu -> %lu", fl, v);
        GERR(MAGIC);
    }
    GC(gr(p, 2) >= gr(p, 0));
    GC(gr(p, 4) >= gr(p, 1));
    TerminateProcess(p, 0);
    WaitForSingleObject(p, 5000);
    CloseHandle(p); CloseHandle(ev);
}

SCEN(guires_other_process_exited_handle) {
    HANDLE p = gtc_spawn("idle_converted:none");
    if (!p) { t_skip("cannot spawn helper"); return; }
    Sleep(500);
    TerminateProcess(p, 0);
    WaitForSingleObject(p, 5000);
    /* the process object (and handle) outlive the process; once the win32k process teardown has run
     * PsGetProcessWin32Process is NULL -> 0x57. Teardown timing is not pinned: poll (bounded), note on miss. */
    int done = 0;
    for (int i = 0; i < 40 && !done; i++) {
        SetLastError(MAGIC);
        done = gr(p, 0) == 0 && GetLastError() == BAD;
        if (!done) Sleep(50);
    }
    if (!done) t_note("exited process still reports GUI resources after 2 s");
    CloseHandle(p);
}

/* A pristine caller's first syscall can be GetGuiResources(-1, ...) itself. */
SCEN(guires_first_call_pristine_self) {
    gtc_assert_pristine();
    SetLastError(MAGIC);
    ULONG g = gr(SELF, 0);
    /* EnterSharedCrit converted the caller before the Win32Process test, so this is a valid count, not 0x57 */
    GCF(g < 0x100000, "%lu", g);
    GERR(MAGIC);
    GEQ(gtc_teb_u64(TEB_Win32ThreadInfo), GetCurrentThreadId());
}
