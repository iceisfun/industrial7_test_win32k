/* win32k_mitigation / threads: lockdown is per-process but conversion is per-thread.
 *
 * PsConvertToGuiThread runs once per thread (the 0x80 GUI bit in ETHREAD+0x78 short-circuits it), so:
 *  - a thread that converted before the process was locked keeps working;
 *  - NtSetInformationProcess(0x34, policy 4) refuses to enable disallow/audit with STATUS_ALREADY_WIN32 only
 *    when *the calling thread* is a GUI thread (PsIsGuiThread on the current thread), so a non-GUI helper thread
 *    can lock a process whose other thread already is a GUI thread;
 *  - every thread that has not converted yet is refused at its first win32k call;
 *  - audit mode converts normally; the one-shot audit log (EtwTimLogProhibitWin32kSystemCalls) clears the
 *    EPROCESS 0x2000 bit, which NtQueryInformationProcess then no longer reports. */
#include "mit.h"

static const char *k_status(unsigned long st) { return k_status_name((K_NTSTATUS)st); }

/* user32 path: the first GetDC converts this thread to a GUI thread (PsConvertToGuiThread). */
static HDC become_gui(void) {
    HDC (WINAPI *gdc)(HWND) = (HDC (WINAPI *)(HWND))t_proc_in("user32.dll", "GetDC");
    return gdc ? gdc(NULL) : NULL;
}
static void release_dc(HDC dc) {
    int (WINAPI *rdc)(HWND, HDC) = (int (WINAPI *)(HWND, HDC))t_proc_in("user32.dll", "ReleaseDC");
    if (rdc && dc) rdc(NULL, dc);
}

struct tinfo { unsigned long st; unsigned long long ti; DWORD ok; BOOL set_ok; DWORD set_err; LONG set_nt; HDC dc; };
static DWORD WINAPI th_stub_call(LPVOID p) {
    struct tinfo *t = (struct tinfo *)p;
    FARPROC f = mit_w32u("NtUserGetThreadState");
    if (!f) return 1;
    t->st = mit_call_stub(f);
    t->ti = mit_teb64(TEB_Win32ThreadInfo);
    t->ok = 1;
    return 0;
}
static DWORD WINAPI th_lock(LPVOID p) {
    struct tinfo *t = (struct tinfo *)p;
    t->ti = mit_teb64(TEB_Win32ThreadInfo);          /* the helper itself is not a GUI thread */
    t->set_nt = mit_nt_set((HANDLE)-1, MIT_POLICY_SCD, MIT_SCD_DISALLOW);
    t->ok = 1;
    return 0;
}
static DWORD WINAPI th_lock_kernelbase(LPVOID p) {
    struct tinfo *t = (struct tinfo *)p;
    t->set_ok = mit_set_scd(MIT_SCD_DISALLOW);
    t->set_err = GetLastError();
    t->ok = 1;
    return 0;
}
static DWORD WINAPI th_audit(LPVOID p) {
    struct tinfo *t = (struct tinfo *)p;
    t->set_nt = mit_nt_set((HANDLE)-1, MIT_POLICY_SCD, MIT_SCD_AUDIT);
    t->ok = 1;
    return 0;
}
static DWORD WINAPI th_getdc(LPVOID p) {
    struct tinfo *t = (struct tinfo *)p;
    t->dc = become_gui();
    t->ti = mit_teb64(TEB_Win32ThreadInfo);
    release_dc(t->dc);
    t->ok = 1;
    return 0;
}

/* ---- locked from the start: workers never convert --------------------------------------------- */
SCN(workers_refused) {
    for (int i = 0; i < 6; i++) {
        struct tinfo t; memset(&t, 0, sizeof t); t.st = 0xFFFFFFFF; t.ti = 0xFFFFFFFF;
        MC(mit_thread(th_stub_call, &t, 10000));
        MCF(mit_status_ok(t.st), "worker %d -> %s", i, k_status(t.st));
        MEQ(t.ti, 0);
    }
    MC(mit_unconverted());
}
SCN(workers_concurrent) {
    HANDLE h[8]; struct tinfo t[8];
    for (int i = 0; i < 8; i++) {
        memset(&t[i], 0, sizeof t[i]); t[i].st = 0xFFFFFFFF; t[i].ti = 0xFFFFFFFF;
        h[i] = CreateThread(NULL, 0, th_stub_call, &t[i], 0, NULL);
        MC(h[i] != NULL);
    }
    for (int i = 0; i < 8; i++) {
        if (!h[i]) continue;
        MC(WaitForSingleObject(h[i], 10000) == WAIT_OBJECT_0);
        CloseHandle(h[i]);
        MCF(mit_status_ok(t[i].st), "worker %d -> %s", i, k_status(t[i].st));
        MEQ(t[i].ti, 0);
    }
}
MIT_LOCKED2(thread_workers_refused, "workers_refused", "")
MIT_LOCKED2(thread_workers_concurrent, "workers_concurrent", "")

/* ---- locking a process that is already a GUI process ------------------------------------------ */
SCN(late_lock_same_thread_refused) {
    MIT_NEED_WIN32K();
    HDC dc = become_gui();
    MC(dc != NULL);
    MC(!mit_unconverted());
    DWORD fl = ~0UL;
    MST(mit_nt_set((HANDLE)-1, MIT_POLICY_SCD, MIT_SCD_DISALLOW), ST_ALREADY_WIN32);   /* PsIsGuiThread(current) */
    MST(mit_nt_set((HANDLE)-1, MIT_POLICY_SCD, MIT_SCD_AUDIT), ST_ALREADY_WIN32);
    MST(mit_nt_set((HANDLE)-1, MIT_POLICY_SCD, MIT_SCD_DISALLOW | MIT_SCD_AUDIT), ST_ALREADY_WIN32);
    SetLastError(0);
    MC(!mit_set_scd(MIT_SCD_DISALLOW));
    {
        LONG (WINAPI *conv)(LONG) = (LONG (WINAPI *)(LONG))mit_nt("RtlNtStatusToDosError");
        if (conv) MERR(conv((LONG)ST_ALREADY_WIN32));      /* BaseSetLastNTError */
        t_note("STATUS_ALREADY_WIN32 maps to Win32 error %lu", GetLastError());
    }
    MC(mit_get_scd(GetCurrentProcess(), &fl)); MEQ(fl, 0);
    release_dc(dc);
}
SCN(late_lock_noop_clear_allowed) {
    MIT_NEED_WIN32K();
    HDC dc = become_gui();
    MC(dc != NULL);
    MST(mit_nt_set((HANDLE)-1, MIT_POLICY_SCD, 0), ST_SUCCESS);    /* the no-op arm never asks PsIsGuiThread */
    release_dc(dc);
}
SCN(late_lock_helper_thread) {
    MIT_NEED_WIN32K();
    HDC dc = become_gui();
    MC(dc != NULL);
    struct tinfo t; memset(&t, 0, sizeof t); t.set_nt = -1;
    MC(mit_thread(th_lock, &t, 10000));
    MEQ(t.ti, 0);
    MST(t.set_nt, ST_SUCCESS);                       /* helper is not a GUI thread, so PsIsGuiThread is false */
    DWORD fl = ~0UL;
    MC(mit_get_scd(GetCurrentProcess(), &fl)); MEQ(fl, 1);
    MC(!mit_unconverted());                          /* the GUI thread keeps its conversion */
    release_dc(dc);
}
SCN(late_lock_gui_thread_keeps_working) {
    MIT_NEED_WIN32K();
    HDC dc = become_gui();
    MC(dc != NULL);
    struct tinfo t; memset(&t, 0, sizeof t); t.set_nt = -1;
    MC(mit_thread(th_lock, &t, 10000));
    MST(t.set_nt, ST_SUCCESS);
    release_dc(dc);
    /* The converted thread does not re-enter PsConvertToGuiThread: the same syscalls still reach win32kbase. */
    HDC dc2 = become_gui();
    MC(dc2 != NULL);
    MC(!mit_unconverted());
    FARPROC f = mit_w32u("NtUserGetThreadState");
    MC(f != NULL);
    if (f) { unsigned long st = mit_call_stub(f); MCF(st != ST_INVALID_SYSTEM_SERVICE, "converted thread got %s", k_status(st)); }
    int (WINAPI *gsm)(int) = (int (WINAPI *)(int))t_proc_in("user32.dll", "GetSystemMetrics");
    if (gsm) MC(gsm(SM_CXSCREEN) > 0);
    release_dc(dc2);
}
SCN(late_lock_new_threads_refused) {
    MIT_NEED_WIN32K();
    HDC dc = become_gui();
    MC(dc != NULL);
    struct tinfo t; memset(&t, 0, sizeof t); t.set_nt = -1;
    MC(mit_thread(th_lock, &t, 10000));
    MST(t.set_nt, ST_SUCCESS);
    for (int i = 0; i < 3; i++) {
        struct tinfo w; memset(&w, 0, sizeof w); w.st = 0xFFFFFFFF; w.ti = 0xFFFFFFFF;
        MC(mit_thread(th_stub_call, &w, 10000));
        MCF(mit_status_ok(w.st), "new worker %d -> %s", i, k_status(w.st));
        MEQ(w.ti, 0);                                 /* PsConvertToGuiThread refused it before any callout */
    }
    release_dc(dc);
}
SCN(late_lock_user32_new_thread) {
    MIT_NEED_WIN32K();
    HDC dc = become_gui();
    MC(dc != NULL);
    struct tinfo t; memset(&t, 0, sizeof t); t.set_nt = -1;
    MC(mit_thread(th_lock, &t, 10000));
    MST(t.set_nt, ST_SUCCESS);
    struct tinfo w; memset(&w, 0, sizeof w); w.ti = 0xFFFFFFFF;
    MC(mit_thread(th_getdc, &w, 10000));              /* user32 GetDC on a never-converted thread of a locked process */
    MEQ(w.ti, 0);
    t_note("user32!GetDC on a refused thread returned %p", (void *)w.dc);
    release_dc(dc);
}
SCN(late_lock_kernelbase_error_codes) {
    MIT_NEED_WIN32K();
    HDC dc = become_gui();
    MC(dc != NULL);
    struct tinfo t; memset(&t, 0, sizeof t);
    MC(mit_thread(th_lock_kernelbase, &t, 10000));
    MC(t.set_ok);                                     /* from a non-GUI thread the kernelbase wrapper succeeds */
    DWORD fl = ~0UL;
    MC(mit_get_scd(GetCurrentProcess(), &fl)); MEQ(fl, 1);
    SetLastError(0); MC(!mit_set_scd(0)); MERR(ERROR_ACCESS_DENIED);   /* cannot be reverted afterwards either */
    release_dc(dc);
}
SCN(late_lock_audit_from_gui_thread) {
    MIT_NEED_WIN32K();
    HDC dc = become_gui();
    MC(dc != NULL);
    struct tinfo t; memset(&t, 0, sizeof t); t.set_nt = -1;
    MC(mit_thread(th_audit, &t, 10000));
    MST(t.set_nt, ST_SUCCESS);                        /* audit from a non-GUI thread of a GUI process is accepted */
    DWORD fl = ~0UL;
    MC(mit_get_scd(GetCurrentProcess(), &fl)); MEQ(fl, 2);
    release_dc(dc);
}
MIT_TEST(late_lock_same_thread_refused, "late_lock_same_thread_refused", "", "plain")
MIT_TEST(late_lock_noop_clear_allowed, "late_lock_noop_clear_allowed", "", "plain")
MIT_TEST(late_lock_helper_thread, "late_lock_helper_thread", "", "plain")
MIT_TEST(late_lock_gui_thread_keeps_working, "late_lock_gui_thread_keeps_working", "", "plain")
MIT_TEST(late_lock_new_threads_refused, "late_lock_new_threads_refused", "", "plain")
MIT_TEST(late_lock_user32_new_thread, "late_lock_user32_new_thread", "", "plain")
MIT_TEST(late_lock_kernelbase_error_codes, "late_lock_kernelbase_error_codes", "", "plain")
MIT_TEST(late_lock_audit_from_gui_thread, "late_lock_audit_from_gui_thread", "", "plain")

/* ---- audit-only variant ------------------------------------------------------------------------- */
SCN(audit_readback_stable) {
    DWORD fl = ~0UL;
    MC(mit_get_scd(GetCurrentProcess(), &fl)); MEQ(fl, 2);
    fl = ~0UL; MST(mit_nt_get((HANDLE)-1, MIT_POLICY_SCD, &fl), ST_SUCCESS); MEQ(fl, 2);
    mit_scd s; s.Flags = fl; MC(s.Audit == 1); MC(s.Disallow == 0);
    MC(mit_unconverted());                            /* audit alone converts nothing until the first win32k call */
    mit_check_pristine_state("audit before first call");
}
SCN(audit_idempotent_and_upgrade_rules) {
    DWORD fl = ~0UL;
    MC(mit_set_scd(MIT_SCD_AUDIT));
    MC(mit_get_scd(GetCurrentProcess(), &fl)); MEQ(fl, 2);
    SetLastError(0); MC(!mit_set_scd(0)); MERR(ERROR_ACCESS_DENIED);          /* 0x2000 set, new audit bit 0 */
    MST(mit_nt_set((HANDLE)-1, MIT_POLICY_SCD, 0), ST_ACCESS_DENIED);
    MC(mit_get_scd(GetCurrentProcess(), &fl)); MEQ(fl, 2);
}
SCN(audit_call_is_not_blocked) {
    MIT_NEED_WIN32K();
    /* a bare win32u call before user32 is loaded dies on Windows 10; the user32 load makes the first win32k call */
    MC(LoadLibraryA("user32.dll") != NULL);
    MC(!mit_unconverted());
    FARPROC f = mit_w32u("NtUserGetThreadState");
    MC(f != NULL); if (!f) return;
    unsigned long st = mit_call_stub(f);
    MCF(st != ST_INVALID_SYSTEM_SERVICE, "audit must not block: %s", k_status(st));
    MC(!mit_unconverted());                           /* PsConvertToGuiThread returned success */
    MC(mit_peb64(PEB_GdiSharedHandleTable) != 0);
}
SCN(audit_user32_path_works) {
    MIT_NEED_WIN32K();
    HDC dc = become_gui();
    MC(dc != NULL);
    MC(!mit_unconverted());
    release_dc(dc);
}
SCN(audit_flag_cleared_after_first_conversion) {
    MIT_NEED_WIN32K();
    DWORD fl = ~0UL;
    MC(mit_get_scd(GetCurrentProcess(), &fl)); MEQ(fl, 2);
    HDC dc = become_gui();
    MC(dc != NULL);
    /* EtwTimLogProhibitWin32kSystemCalls clears EPROCESS+0x9d0 bit 0x2000 after logging once. */
    fl = ~0UL; MC(mit_get_scd(GetCurrentProcess(), &fl)); MEQ(fl, 0);
    release_dc(dc);
}
SCN(audit_set_after_conversion_refused) {
    MIT_NEED_WIN32K();
    HDC dc = become_gui();
    MC(dc != NULL);
    MST(mit_nt_set((HANDLE)-1, MIT_POLICY_SCD, MIT_SCD_AUDIT), ST_ALREADY_WIN32);
    MST(mit_nt_set((HANDLE)-1, MIT_POLICY_SCD, MIT_SCD_DISALLOW), ST_ALREADY_WIN32);
    release_dc(dc);
}
SCN(audit_second_thread_converts) {
    MIT_NEED_WIN32K();
    HDC dc = become_gui();
    MC(dc != NULL);
    struct tinfo w; memset(&w, 0, sizeof w);
    MC(mit_thread(th_getdc, &w, 10000));
    MC(w.dc != NULL);                                 /* nothing is locked: the audit bit was already consumed */
    MNE(w.ti, 0);
    release_dc(dc);
}
SCN(audit_upgrade_to_disallow) {
    DWORD fl = ~0UL;
    MC(mit_set_scd(MIT_SCD_DISALLOW));                /* 0x3000 set; not a GUI thread yet */
    MC(mit_get_scd(GetCurrentProcess(), &fl)); MEQ(fl, 1);
    FARPROC f = mit_w32u("NtUserGetThreadState");
    MC(f != NULL); if (!f) return;
    unsigned long st = mit_call_stub(f);
    MCF(mit_status_ok(st), "-> %s", k_status(st));
    MC(mit_unconverted());
    mit_check_pristine_state("audit upgraded to disallow");
}
SCN(audit_then_both_bits) {
    DWORD fl = ~0UL;
    MC(mit_set_scd(MIT_SCD_DISALLOW | MIT_SCD_AUDIT));
    MC(mit_get_scd(GetCurrentProcess(), &fl)); MEQ(fl, 1);
}
SCN(audit_reserved_bits) {
    MST(mit_nt_set((HANDLE)-1, MIT_POLICY_SCD, 6), ST_INVALID_PARAMETER);
    DWORD fl = ~0UL; MC(mit_get_scd(GetCurrentProcess(), &fl)); MEQ(fl, 2);
}
SCN(audit_spawn_child_note) {
    /* Whether a plain child of an audit-only process inherits the audit bit depends on the audit inherit map
     * (PspInheritMitigationAuditOptions); report it, do not assert it. */
    struct mit_proc p;
    if (!mit_start(&p, "report_flags::plain", NULL, 1)) { t_note("spawn failed %lu", p.err); return; }
    mit_release(&p);
    DWORD code = mit_finish(&p, 20000);
    MEQ(code, 0);
}
SCN(report_flags) {
    DWORD fl = ~0UL;
    if (!mit_get_scd(GetCurrentProcess(), &fl)) { t_note("GetProcessMitigationPolicy failed %lu", GetLastError()); return; }
    t_note("child SystemCallDisable flags = 0x%lx", fl);
}
MIT_TEST(audit_readback_stable_set, "audit_readback_stable", "", "aud")
MIT_TEST(audit_readback_stable_create, "audit_readback_stable", "", "audc")
MIT_TEST(audit_idempotent_and_upgrade_rules_set, "audit_idempotent_and_upgrade_rules", "", "aud")
MIT_TEST(audit_idempotent_and_upgrade_rules_create, "audit_idempotent_and_upgrade_rules", "", "audc")
MIT_TEST(audit_call_is_not_blocked_set, "audit_call_is_not_blocked", "", "aud")
MIT_TEST(audit_call_is_not_blocked_create, "audit_call_is_not_blocked", "", "audc")
MIT_TEST(audit_user32_path_works_set, "audit_user32_path_works", "", "aud")
MIT_TEST(audit_user32_path_works_create, "audit_user32_path_works", "", "audc")
MIT_TEST(audit_flag_cleared_after_first_conversion_set, "audit_flag_cleared_after_first_conversion", "", "aud")
MIT_TEST(audit_flag_cleared_after_first_conversion_create, "audit_flag_cleared_after_first_conversion", "", "audc")
MIT_TEST(audit_set_after_conversion_refused_set, "audit_set_after_conversion_refused", "", "aud")
MIT_TEST(audit_set_after_conversion_refused_create, "audit_set_after_conversion_refused", "", "audc")
MIT_TEST(audit_second_thread_converts_set, "audit_second_thread_converts", "", "aud")
MIT_TEST(audit_second_thread_converts_create, "audit_second_thread_converts", "", "audc")
MIT_TEST(audit_upgrade_to_disallow_set, "audit_upgrade_to_disallow", "", "aud")
MIT_TEST(audit_upgrade_to_disallow_create, "audit_upgrade_to_disallow", "", "audc")
MIT_TEST(audit_then_both_bits_set, "audit_then_both_bits", "", "aud")
MIT_TEST(audit_reserved_bits_set, "audit_reserved_bits", "", "aud")
MIT_TEST(audit_spawn_child_note_set, "audit_spawn_child_note", "", "aud")
MIT_TEST(audit_spawn_child_note_create, "audit_spawn_child_note", "", "audc")
