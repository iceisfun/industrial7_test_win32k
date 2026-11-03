/* win32k_mitigation: shared scaffolding.
 *
 * Win32k system-call lockdown (EPROCESS flags at +0x9d0: 0x1000 = DisallowWin32kSystemCalls,
 * 0x2000 = AuditDisallowWin32kSystemCalls) is process-wide and irreversible, so every scenario
 * runs in a CHILD process ("mit"). The child entry path of the harness loads no module, so the
 * scenario starts pristine (no user32/gdi32/win32u) and decides itself what to load.
 *
 * A scenario selector is  name[:arg]:flavor  and the flavor says how the child gets (or does not
 * get) the policy before the scenario body runs:
 *   set   child calls SetProcessMitigationPolicy(ProcessSystemCallDisablePolicy, {Disallow=1})
 *   cre   parent created the child with PROCESS_CREATION_MITIGATION_POLICY_WIN32K_SYSTEM_CALL_DISABLE_ALWAYS_ON
 *   aud   child sets {Audit=1} itself
 *   audc  parent created the child with the audit creation attribute
 *   plain nothing; the scenario body does whatever it wants (control and late-lockdown scenarios)
 * The prelude verifies the policy by readback; on Wine (no such policy) the child reports a SKIP
 * note and exits 0 so a headless run still exercises the spawn path.
 *
 * Inside a scenario use MC()/MCF()/MEQ()/MST() (counted CHECKs); the child's exit code is its
 * failure count.
 */
#ifndef MIT_H
#define MIT_H
#include "t.h"
#include "k.h"

#define MIT_AREA "win32k_mitigation"
#include "modules.h"
#ifndef PROC_THREAD_ATTRIBUTE_MITIGATION_AUDIT_POLICY
#define PROC_THREAD_ATTRIBUTE_MITIGATION_AUDIT_POLICY 0x00020018   /* ProcThreadAttributeValue(24, FALSE, TRUE, FALSE) */
#endif

/* ---- statuses used by the kernel paths under test ------------------------------------------ */
#define ST_SUCCESS                0x00000000UL
#define ST_INFO_LENGTH_MISMATCH   0xC0000004UL  /* NtSet/QueryInformationProcess class 0x34: length != 8 */
#define ST_INVALID_HANDLE         0xC0000008UL
#define ST_INVALID_PARAMETER      0xC000000DUL
#define ST_INVALID_SYSTEM_SERVICE 0xC000001CUL  /* KiSystemServiceStart failure tail for an unconverted thread */
#define ST_ACCESS_DENIED          0xC0000022UL
#define ST_NOT_SUPPORTED          0xC00000BBUL  /* NtSetInformationProcess 0x34 default: policy not handled */
#define ST_ALREADY_WIN32          0xC0000189UL  /* policy 4 set while the calling thread is already a GUI thread */

/* ---- ProcessSystemCallDisablePolicy ---------------------------------------------------------
 * The SDK struct only names bit 0; NtQueryInformationProcess(0x34) case 4 reports bit 0 when
 * EPROCESS flag 0x1000 is set, else bit 1 when 0x2000 is set (audit). */
#define MIT_POLICY_SCD 4
#define MIT_SCD_DISALLOW 0x1UL
#define MIT_SCD_AUDIT    0x2UL
typedef struct mit_scd { union { DWORD Flags; struct { DWORD Disallow : 1; DWORD Audit : 1; DWORD Reserved : 30; }; }; } mit_scd;

/* Creation attribute values (winbase.h: PROCESS_CREATION_MITIGATION_POLICY_WIN32K_SYSTEM_CALL_DISABLE_*,
 * nibble 7 of the first mitigation qword; PspApplyMitigationOptions tests (opts >> 0x1c) & 3 == 1). */
#define MIT_CREATE_ON       0x0000000010000000ULL
#define MIT_CREATE_OFF      0x0000000020000000ULL
#define MIT_CREATE_RESERVED 0x0000000030000000ULL
#define MIT_CREATE_DEFER    0x0ULL

/* ---- scenarios ----------------------------------------------------------------------------- */
typedef void (*mit_fn)(void);
void mit_register(const char *name, mit_fn fn);
extern int mit_fails;
const char *mit_arg(void);     /* middle selector component, "" when absent */
const char *mit_flavor(void);  /* last selector component */
const char *mit_token(void);   /* argv[2]: names the park events, "" when the parent does not park */

#define MC(c)        do { int ok_ = !!(c); { if (!ok_) mit_fails++; } t_check(ok_, __FILE__, __LINE__, #c, NULL); } while (0)
#define MCF(c, ...)  do { int ok_ = !!(c); { if (!ok_) mit_fails++; } t_check(ok_, __FILE__, __LINE__, #c, __VA_ARGS__); } while (0)
#define MEQ(a, b)    do { unsigned long long a_ = (unsigned long long)(ULONG_PTR)(a), b_ = (unsigned long long)(ULONG_PTR)(b); \
    { if (a_ != b_) mit_fails++; } t_check(a_ == b_, __FILE__, __LINE__, #a " == " #b, "got 0x%llx want 0x%llx", a_, b_); } while (0)
#define MNE(a, b)    do { unsigned long long a_ = (unsigned long long)(ULONG_PTR)(a), b_ = (unsigned long long)(ULONG_PTR)(b); \
    { if (a_ == b_) mit_fails++; } t_check(a_ != b_, __FILE__, __LINE__, #a " != " #b, "got 0x%llx", a_); } while (0)
/* NTSTATUS equality on the low 32 bits (stubs for non-NTSTATUS services return rax zero-extended). */
#define MST(a, b)    do { unsigned long a_ = (unsigned long)(ULONG_PTR)(a), b_ = (unsigned long)(b); \
    { if (a_ != b_) mit_fails++; } t_check(a_ == b_, __FILE__, __LINE__, #a " == " #b, "got %s want %s", k_status_name((K_NTSTATUS)a_), k_status_name((K_NTSTATUS)b_)); } while (0)
#define MERR(err)    do { DWORD e_ = GetLastError(); { if (e_ != (DWORD)(err)) mit_fails++; } \
    t_check(e_ == (DWORD)(err), __FILE__, __LINE__, #err, "GetLastError got %lu want %lu", e_, (DWORD)(err)); } while (0)

#define SCN(n) \
    static void sc_##n(void); \
    static void __attribute__((constructor)) rsc_##n(void) { mit_register(#n, sc_##n); } \
    static void sc_##n(void)

/* Wine has no win32k.sys: scenarios that need a real GUI conversion bail out there. */
#define MIT_WINE_SKIP() do { if (t_is_wine()) { t_skip("Wine does not implement ProcessMitigationPolicy / win32k"); return; } } while (0)
#define MIT_NEED_WIN32K() do { if (t_is_wine()) { printf("  SKIP Wine has no win32k.sys\n"); return; } } while (0)
/* Run a statement whose crash would be an observation, not a failure (a user32/gdi32 wrapper called in a
 * process whose win32k connection never happened): an exception becomes a NOTE and execution continues. */
#include <setjmp.h>
extern __thread jmp_buf mit_soft_jb; extern __thread volatile int mit_soft_on; extern __thread volatile unsigned long mit_soft_code;
#define MIT_SOFT(stmt) do { mit_soft_on = 1; if (!setjmp(mit_soft_jb)) { stmt; } else t_note("exception 0x%08lx in: %s", mit_soft_code, #stmt); mit_soft_on = 0; } while (0)

/* ---- parent side: spawn / observe / finish -------------------------------------------------- */
struct mit_attr {
    int       have_pol;  ULONGLONG pol[2];  SIZE_T pol_size;    /* PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY */
    int       have_aud;  ULONGLONG aud[2];  SIZE_T aud_size;    /* PROC_THREAD_ATTRIBUTE_MITIGATION_AUDIT_POLICY */
};
struct mit_proc {
    HANDLE proc, thread, rd, drain, rdy, go;
    DWORD  pid, err;            /* err = GetLastError() when mit_start failed */
    char   evname[96];
    char  *buf; volatile size_t len;
};
/* Launch `exe --child mit <selector> <token>`; stdout/stderr go to a private pipe (pipe == 0: no
 * std handles and bInheritHandles FALSE). Returns 1 on success. */
int   mit_start(struct mit_proc *p, const char *selector, const struct mit_attr *a, int pipe);
int   mit_wait_ready(struct mit_proc *p, DWORD ms);   /* child reached mit_park() */
void  mit_release(struct mit_proc *p);                /* let the parked child continue */
DWORD mit_finish(struct mit_proc *p, DWORD ms);       /* wait, relay output, close; 0xDEAD0001 = timeout */
/* Convenience: spawn, relay, CHECK child exit code == 0. Skips under attribute failure. */
void  mit_run(const char *scn, const char *arg, const char *flavor);
void  mit_run_attr(const char *scn, const char *arg, const char *flavor, const struct mit_attr *a);
extern const struct mit_attr MIT_ATTR_ON8, MIT_ATTR_ON16, MIT_ATTR_ON4, MIT_ATTR_OFF, MIT_ATTR_DEFER, MIT_ATTR_AUDIT;

/* ---- child side helpers ------------------------------------------------------------------ */
void  mit_park(void);                          /* signal "rdy", wait up to 15 s for "go" */
FARPROC mit_k32(const char *name);             /* kernel32 export, never loads user32/gdi32 */
FARPROC mit_nt(const char *name);              /* ntdll export */
FARPROC mit_w32u(const char *name);            /* win32u export (loads win32u.dll only) */
BOOL  mit_set_scd(DWORD flags);                /* kernelbase SetProcessMitigationPolicy(4,...) */
BOOL  mit_get_scd(HANDLE h, DWORD *flags);     /* kernelbase GetProcessMitigationPolicy(h,4,...) */
LONG  mit_nt_set(HANDLE h, DWORD policy, DWORD flags);   /* NtSetInformationProcess(h, 0x34, {policy,flags}) */
LONG  mit_nt_get(HANDLE h, DWORD policy, DWORD *flags);  /* NtQueryInformationProcess(h, 0x34, ...) */
LONG  mit_nt_get_len(HANDLE h, DWORD policy, DWORD len);
int   mit_have_policy_api(void);               /* readback works at all (false on Wine) */
unsigned long long mit_teb64(unsigned off);
unsigned long long mit_peb64(unsigned off);
int   mit_unconverted(void);                   /* TEB.Win32ThreadInfo == 0 */
void  mit_check_pristine_state(const char *where);
int   mit_status_ok(unsigned long st);         /* 0xC000001C or a filtered value: the only outcomes KiSystemServiceStart's tail can give */
int   mit_status_filtered(unsigned long st);   /* sign-extended signed byte <= 0 from the whitelist table */
typedef ULONG_PTR (WINAPI *mit_stub8)(ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR);
unsigned long mit_call_stub(FARPROC f);        /* call a win32u stub with eight zero arguments; low 32 bits of rax */
int   mit_thread(DWORD (WINAPI *fn)(LPVOID), void *arg, DWORD ms);   /* run fn on a new thread, join */
HANDLE mit_open_self(DWORD access);            /* real (non-pseudo) handle of this process */

/* Parent-side test generators. NAME is the scenario, FLAVORS are fixed. */
#define MIT_TEST(tname, scn, arg, flavor) \
    TEST(win32k_mitigation_##tname) { mit_run(scn, arg, flavor); }
#define MIT_LOCKED2(base, scn, arg) \
    MIT_TEST(base##_set, scn, arg, "set") \
    MIT_TEST(base##_cre, scn, arg, "cre")
#endif
