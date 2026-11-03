/* r2_win32k_process_lifecycle_quotas: shared scaffolding.
 *
 * Process/thread lifecycle of win32k state. Anything that changes process-wide win32k state
 * (interaction flags, mitigation policy, object quotas, first-call conversion) runs in a CHILD:
 * a scenario is a function registered with R2_SCN(name); the parent TEST re-launches this exe as
 *   exe --child r2 <scenario> [token]
 * and requires exit code 0 (= number of failed RC() checks inside the child).
 *
 * Facts the expectations rest on (Windows 10 build 19041, win32kbase/win32kfull analysis):
 *  - NtUserProcessConnect (win32kfull 0x3fab0): first test is PsGetCurrentProcess()==gpepCSRSS, else
 *    STATUS_UNSUCCESSFUL. Ordinary processes obtain gSharedInfo from csrss via CsrClientConnectToServer
 *    (user32!_UserClientDllInitialize copies the USERCONNECT reply at +8 into gSharedInfo, 0x238 bytes).
 *  - NtUserQueryInformationThread / NtUserNotifyProcessCreate(flags!=0) / NtUserRemoteConnect: csrss-only,
 *    other callers get STATUS_ACCESS_DENIED (0xC0000022).
 *  - NtUserInitTask is bound to the stub that returns STATUS_UNSUCCESSFUL (0xC0000001).
 *  - NtUserSetProcessMousewheelRoutingMode: modes 0/1/2 store bits in W32PROCESS+0x334 and return 1,
 *    anything else UserSetLastError(0x57) and returns 0.
 *  - NtUserSetProcessInteractionFlags(a,b,c): stores bytes at W32PROCESS+0x409/+0x408/+0x40a, returns 1.
 *  - NtUserSetThreadState(flags,mask): flags & ~0x4000 != 0 -> UserSetLastError(0xC000000D), FALSE.
 */
#ifndef R2_H
#define R2_H
#include "t.h"
#include "k.h"

#define R2P "r2_win32k_process_lifecycle_quotas_"

typedef void (*r2_fn)(void);
void r2_register(const char *name, r2_fn fn);
extern int r2_fails;
const char *r2_token(void);          /* argv[2] of the child, "" when absent */

#define RC(c)        do { int ok_ = !!(c); if (!ok_) r2_fails++; t_check(ok_, __FILE__, __LINE__, #c, NULL); } while (0)
#define RCF(c, ...)  do { int ok_ = !!(c); if (!ok_) r2_fails++; t_check(ok_, __FILE__, __LINE__, #c, __VA_ARGS__); } while (0)
#define REQ(a, b)    do { unsigned long long a_ = (unsigned long long)(ULONG_PTR)(a), b_ = (unsigned long long)(ULONG_PTR)(b); \
    { if (a_ != b_) r2_fails++; } t_check(a_ == b_, __FILE__, __LINE__, #a " == " #b, "got 0x%llx want 0x%llx", a_, b_); } while (0)
#define RNE(a, b)    do { unsigned long long a_ = (unsigned long long)(ULONG_PTR)(a), b_ = (unsigned long long)(ULONG_PTR)(b); \
    { if (a_ == b_) r2_fails++; } t_check(a_ != b_, __FILE__, __LINE__, #a " != " #b, "got 0x%llx", a_); } while (0)
/* NTSTATUS compare on the low 32 bits (stubs for non-NTSTATUS services return rax zero-extended). */
#define RST(a, b)    do { unsigned long a_ = (unsigned long)(ULONG_PTR)(a), b_ = (unsigned long)(b); \
    { if (a_ != b_) r2_fails++; } t_check(a_ == b_, __FILE__, __LINE__, #a " == " #b, "got %s want %s", k_status_name((K_NTSTATUS)a_), k_status_name((K_NTSTATUS)b_)); } while (0)

/* A TEST that runs scenario NAME in a fresh child process and a scenario body that follows. */
#define R2_SCN(NAME) \
    static void scn_##NAME(void); \
    static void __attribute__((constructor)) rr_##NAME(void) { r2_register(#NAME, scn_##NAME); } \
    TEST(r2_win32k_process_lifecycle_quotas_##NAME) { r2_run_parent(#NAME); } \
    static void scn_##NAME(void)
void r2_run_parent(const char *name);
/* Same, with the child created under PROCESS_CREATION_MITIGATION_POLICY_WIN32K_SYSTEM_CALL_DISABLE_ALWAYS_ON: on Windows 10
 * the system-call-disable policy can only be given at creation, SetProcessMitigationPolicy refuses it at run time. */
#define R2_SCN_LOCKED(NAME) \
    static void scn_##NAME(void); \
    static void __attribute__((constructor)) rr_##NAME(void) { r2_register(#NAME, scn_##NAME); } \
    TEST(r2_win32k_process_lifecycle_quotas_##NAME) { r2_run_parent_locked(#NAME); } \
    static void scn_##NAME(void)
void r2_run_parent_locked(const char *name);

/* ---- parked children (parent inspects the child while it is alive) ---- */
struct r2_proc {
    HANDLE proc, rd, ev_rdy, ev_go;
    DWORD pid;
    char tok[64];
    char name[64];
};
int   r2_start_locked(struct r2_proc *p, const char *scn);  /* like r2_start, created with the win32k lock policy */
int   r2_start(struct r2_proc *p, const char *scn);      /* 1 on success, else t_skip'able failure */
int   r2_wait_ready(struct r2_proc *p, DWORD ms);
void  r2_go(struct r2_proc *p);
DWORD r2_finish(struct r2_proc *p, DWORD ms);            /* waits, relays output, closes; 0xDEAD0001 on timeout */
void  r2_park(void);                                     /* child side: signal ready, wait up to 15 s for go */
void  r2_publish(const char *key, ULONGLONG v);          /* child side: write a value the parent reads back */
int   r2_fetch(struct r2_proc *p, const char *key, ULONGLONG *v);

/* ---- helpers ---- */
int    r2_connect(void);                                 /* load user32/gdi32, convert the thread; 0 = unavailable */
#define R2_NEED() do { if (!r2_connect()) { t_skip("win32k not reachable"); return; } } while (0)
#define R2_NEED_WIN() do { if (t_is_wine()) { t_skip("Wine has no win32k.sys"); return; } if (!r2_connect()) { t_skip("win32k not reachable"); return; } } while (0)
#define R2_CHILD_NEED_WIN() do { if (t_is_wine()) { printf("  SKIP Wine has no win32k.sys\n"); return; } if (!r2_connect()) { printf("  SKIP win32k not reachable\n"); return; } } while (0)
/* A GDI object that is freed at once. Deleted brushes and regions stay in a per-process cache of win32k and keep being
 * counted by GetGuiResources, so exact count relations are taken with 1x1 bitmaps. */
static inline void *r2_mkgdi(void) { return U(CreateBitmap)(1, 1, 1, 1, NULL); }
DWORD  r2_gur(DWORD flag);                               /* GetGuiResources(GetCurrentProcess(), flag) */
FARPROC r2_w32u(const char *name);                       /* win32u export, loads only win32u.dll */
const WCHAR *r2_class(void);
HWND   r2_window(void);                                  /* hidden tool popup of the private class on this thread */
int    r2_vq(const void *p, MEMORY_BASIC_INFORMATION *mbi);
unsigned long r2_wait_thread(HANDLE h, DWORD ms);

#define GR_GDIOBJECTS_ 0
#define GR_USEROBJECTS_ 1
#define GR_GDIOBJECTS_PEAK_ 2
#define GR_USEROBJECTS_PEAK_ 4

/* generic win32u syscall shape: up to 12 ULONG_PTR arguments, result in rax */
typedef ULONG_PTR (WINAPI *r2_sys12)(ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR,
                                      ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR);
#define SYS(name, ...) ({ r2_sys12 f_ = (r2_sys12)r2_w32u(name); f_ ? f_(__VA_ARGS__) : (ULONG_PTR)0xDEADBEEFDEADBEEFULL; })
#define SYS0(name) SYS(name, 0,0,0,0,0,0,0,0,0,0,0,0)
#define SYS_MISSING(r) ((r) == (ULONG_PTR)0xDEADBEEFDEADBEEFULL)
#endif
