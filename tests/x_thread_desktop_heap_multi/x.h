/* x_thread_desktop_heap_multi: shared declarations.
 *
 * What is per thread and what is per process in win32k. A thread has its own message queue state (change and wake bits,
 * posted messages), its own focus, active window and capture, and its own message extra info; a window carries its own
 * layout-synchronisation flag; a process carries the window-filtering switch and the process-session and integrity
 * checks. The tests start bounded numbers of threads (at most 3 at a time) and child processes, hand work from one
 * thread to another through events (no sleeps for ordering) and assert that state set on one side is invisible on the
 * other, is not inherited by threads and processes created afterwards, and that what a thread or process owned is gone
 * once it has ended. Syscalls come from win32u.dll; none has an SDK declaration, so the signatures are declared here from
 * the user32 callers and the kernel bodies. Return values are declared ULONG_PTR and masked by the test.
 */
#ifndef XT_H
#define XT_H
#include "t.h"
#include "k.h"

UDECL(DWORD,     NtUserGetQueueStatusReadonly, (UINT flags));
UDECL(DWORD,     NtUserGetQueueStatus, (UINT flags));
UDECL(LONG,      NtUserRealWaitMessageEx, (DWORD mask, DWORD timeout));
UDECL(LONG,      NtUserRealInternalGetMessage, (MSG *msg, HWND h, UINT lo, UINT hi, UINT flags, UINT extra));
UDECL(ULONG_PTR, NtUserCheckProcessSession, (DWORD pid));
UDECL(LONG,      NtUserCheckAccessForIntegrityLevel, (ULONG_PTR pid_a, ULONG_PTR pid_b, DWORD *out));
UDECL(ULONG_PTR, NtUserNotifyProcessCreate, (ULONG_PTR a, ULONG_PTR b, ULONG_PTR c, ULONG_PTR flags, ULONG_PTR e));
UDECL(LONG,      NtUserClearForeground, (void));
UDECL(ULONG_PTR, NtUserNavigateFocus, (HWND h));
UDECL(ULONG_PTR, NtUserDisableProcessWindowFiltering, (void));
UDECL(ULONG_PTR, NtUserBeginLayoutUpdate, (HWND h));
UDECL(ULONG_PTR, NtUserLayoutCompleted, (HWND h));
UDECL(ULONG_PTR, NtUserEnableResizeLayoutSynchronization, (HWND h, DWORD on));
UDECL(ULONG_PTR, NtUserIsResizeLayoutSynchronizationEnabled, (HWND h));
UDECL(ULONG_PTR, NtUserConfirmResizeCommit, (HWND h));

/* Every test runs with user32 loaded (a win32k service issued before that ends the process); syscall tests need the export
 * and are skipped under an emulator, which has no win32k service behind the names. */
void xt_init(void);
#define XT(n) \
    static void body_##n(void); \
    static void test_##n(void) { xt_init(); body_##n(); } \
    static void __attribute__((constructor)) reg_##n(void) { t_register("x_thread_desktop_heap_multi_" #n, test_##n); } \
    static void body_##n(void)
#define NEED(n) do { if (!UN(n)) { t_skip("missing export: " #n); return; } if (t_is_wine()) { t_skip("emulator: " #n " is not a win32k service"); return; } } while (0)
#define NEED_ALL do { if (t_is_wine()) { t_skip("emulator: no win32k service behind the syscall names"); return; } } while (0)

/* Failure counter for CHILD bodies (the child returns it as its exit code) and for worker jobs. */
extern int xt_cf;
#define CK(c) do { int ok_ = !!(c); if (!ok_) xt_cf++; t_check(ok_, __FILE__, __LINE__, #c, NULL); } while (0)
#define CKF(c, ...) do { int ok_ = !!(c); if (!ok_) xt_cf++; t_check(ok_, __FILE__, __LINE__, #c, __VA_ARGS__); } while (0)
#define CK_EQ(a, b) do { ULONG_PTR a__ = (ULONG_PTR)(a), b__ = (ULONG_PTR)(b); int ok_ = a__ == b__; if (!ok_) xt_cf++; \
    t_check(ok_, __FILE__, __LINE__, #a " == " #b, "got 0x%llx want 0x%llx", (unsigned long long)a__, (unsigned long long)b__); } while (0)

#define SENT 0x0BADF00Du                 /* last-error sentinel */
#define R_B(r) ((BYTE)(ULONG_PTR)(r))    /* bool returned in AL */
#define R_D(r) ((DWORD)(ULONG_PTR)(r))   /* BOOL / UINT / NTSTATUS returned in EAX */
#define E_ACCESS_DENIED 5u
#define E_INVALID_PARAM 87u
#define E_INVALID_FLAGS 1004u
#define E_INVALID_HWND 1400u
#define E_INVALID_THREAD_ID 1444u
#define E_TIMEOUT 1460u
#define ST_ACCESS_DENIED 0xC0000022u
#define ST_INVALID_PARAMETER 0xC000000Du

#define HI(x) ((unsigned)((x) >> 16) & 0xFFFF)
#define LO(x) ((unsigned)(x) & 0xFFFF)
#define RO(f) R_D(UN(NtUserGetQueueStatusReadonly)(f))

/* ---- windows ---- */
/* A tiny visible off-screen tool popup of this process (class registered on first use). NULL on failure. */
HWND xt_window(void);
HWND xt_child_window(HWND parent);
void xt_destroy(HWND h);
/* Removes everything pending in this thread's queue and consumes its change bits. */
void xt_drain(void);
/* Drops a bit mask of the wake bits this test cares about: pending posts for one message id. */
int  xt_peek_remove(UINT msg_lo, UINT msg_hi, MSG *out);

/* ---- worker threads ---- */
/* A GUI thread with `nwin` windows that runs one job at a time, on request, and waits (without pumping) in between. */
struct xt_wk;
typedef void (*xt_job)(struct xt_wk *w, void *arg);
struct xt_wk {
    HANDLE th, evcmd, evdone, evready, sig; DWORD tid;
    HWND w[4]; int nw;
    xt_job job; void *arg; volatile LONG quit;
};
int  xt_wk_start(struct xt_wk *w, int nwin);
/* Hands the job over without waiting. */
void xt_wk_go(struct xt_wk *w, xt_job job, void *arg);
/* Waits for the job (pumping the caller's queue meanwhile); 1 when it finished within ms. */
int  xt_wk_wait(struct xt_wk *w, DWORD ms);
/* go + wait. */
int  xt_wk_run(struct xt_wk *w, xt_job job, void *arg);
/* Waits for the job to raise w->sig (reset on return); 1 when it did. */
int  xt_wk_sig(struct xt_wk *w, DWORD ms);
/* Stops and joins the thread (its windows and queue go with it). */
void xt_wk_stop(struct xt_wk *w);
void job_drain(struct xt_wk *w, void *p);   /* job: xt_drain on the worker */
/* One-shot: a thread that runs job(NULL-worker semantics) on a fresh GUI thread and is joined before returning. */
int  xt_on_thread(void (*fn)(void *), void *arg);

/* ---- child processes ---- */
struct xt_shm { volatile LONG ready, go, done; volatile LONG64 v[16]; };
struct xt_shm *xt_shm_create(const char *tag, HANDLE *h);
struct xt_shm *xt_shm_open(const char *tag, HANDLE *h);
int  xt_wait(volatile LONG *flag, DWORD ms);              /* 1 once *flag is non-zero */
const char *xt_tag(void);                                /* unique per call */
/* Starts this exe as the child `mode` sharing page `tag`; NULL when it did not start. */
HANDLE xt_spawn(const char *mode, const char *tag, const char *extra, PROCESS_INFORMATION *pi);
/* Waits for exit and closes; the exit code (0xDEAD0001 after a kill on timeout). */
DWORD xt_reap(HANDLE p, PROCESS_INFORMATION *pi, DWORD ms);
/* Runs the child to completion and returns its exit code (the child's failure count). */
DWORD xt_child(const char *mode, const char *extra);
/* Child bodies: `mode` names one; the body gets the shared page (NULL when the tag was "-") and the extra words. */
typedef int (*xt_cmode)(struct xt_shm *s, int argc, char **argv);
void xt_register_mode(const char *name, xt_cmode fn);
#define XC(n) \
    static int cm_##n(struct xt_shm *s, int argc, char **argv); \
    static void __attribute__((constructor)) regc_##n(void) { xt_register_mode(#n, cm_##n); } \
    static int cm_##n(struct xt_shm *s, int argc, char **argv)
#endif
