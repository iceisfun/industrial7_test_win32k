/* msg_timers_atoms: shared fixtures for the timer and user-atom-table tests.
 *
 * Syscalls reached directly (win32u stubs, signatures taken from the win32kfull analysis):
 *   NtUserSetTimer(hwnd, id, elapse, TIMERPROC, tolerance)          user32!SetTimer/SetCoalescableTimer
 *   NtUserKillTimer(hwnd, id)                                       user32!KillTimer
 *   NtUserSetSystemTimer(hwnd, id, elapse)                          no user32 export
 *   NtUserValidateTimerCallback(proc)                               user32!DispatchMessageWorker
 *   NtUserRegisterWindowMessage(PUNICODE_STRING)                    user32!RegisterWindowMessageW/RegisterClipboardFormatW
 *   NtUserGetAtomName(atom, PUNICODE_STRING)                        no user32 export
 *   NtUserGetClipboardFormatName(fmt, buf, cch)                     user32!GetClipboardFormatNameW (jump thunk)
 *   NtUserGetClassName(hwnd, bReal, PUNICODE_STRING)                user32!GetClassNameW
 */
#ifndef MTA_H
#define MTA_H
#include "t.h"
#include "k.h"

UDECL(UINT_PTR, NtUserSetTimer, (HWND, UINT_PTR, UINT, TIMERPROC, ULONG));
UDECL(BOOL, NtUserKillTimer, (HWND, UINT_PTR));
UDECL(UINT_PTR, NtUserSetSystemTimer, (HWND, UINT_PTR, UINT));
UDECL(BOOL, NtUserValidateTimerCallback, (PVOID));
UDECL(UINT, NtUserRegisterWindowMessage, (UNICODE_STRING *));
UDECL(UINT, NtUserGetAtomName, (ATOM, UNICODE_STRING *));
UDECL(UINT, NtUserGetClipboardFormatName, (UINT, PWSTR, UINT));
UDECL(UINT, NtUserGetClassName, (HWND, BOOL, UNICODE_STRING *));

/* EXPECT_EQ evaluates its arguments twice (once for the compare, once for the message), which re-runs
 * calls with side effects such as RemovePropW or DispatchMessageW; MEQ evaluates each side once. */
#define MEQ(a, b) do { ULONG_PTR a_ = (ULONG_PTR)(a), b_ = (ULONG_PTR)(b); \
    t_check(a_ == b_, __FILE__, __LINE__, #a " == " #b, "got 0x%llx want 0x%llx", (unsigned long long)a_, (unsigned long long)b_); } while (0)

#define MTA_WM_SYSTIMER 0x118
#define MTA_TIMERV_NO_COALESCING 0xFFFFFFFFu
#define MTA_TIMERV_COALESCING_MAX 0x7FFFFFF5u
#define MTA_ERR_SENTINEL 0x5A5A5A5Au

int  mta_ready(void);                 /* user32 resolved, this thread converted to a GUI thread */
#define MTA_NEED() do { if (!mta_ready()) { t_skip("user32 not available"); return; } } while (0)
#define MTA_WIN(w) MTA_NEED(); HWND w = mta_win(); if (!(w)) { t_skip("cannot create a test window"); return; }

/* ---- logging window ------------------------------------------------------------------------ */
#define MTA_LOG_MAX 4096
struct mta_ev { UINT msg; HWND hwnd; WPARAM wp; LPARAM lp; DWORD tid; DWORD tick; };
extern struct mta_ev mta_log[MTA_LOG_MAX];
extern volatile LONG mta_nlog;
struct mta_pev { HWND hwnd; UINT msg; UINT_PTR id; DWORD tick; DWORD tid; int which; };
extern struct mta_pev mta_plog[MTA_LOG_MAX];
extern volatile LONG mta_nplog;
void mta_reset(void);                 /* clear both logs */
int  mta_count(UINT msg, WPARAM wp);  /* logged window-procedure messages with msg (wp == (WPARAM)-1: any) */
int  mta_pcount(int which);           /* timer-procedure calls of proc `which` (-1: any) */
const WCHAR *mta_cls(void);
HWND mta_win(void);                   /* hidden off-screen WS_POPUP on the calling thread */
void mta_destroy(HWND w);
VOID CALLBACK mta_proc_a(HWND, UINT, UINT_PTR, DWORD);
VOID CALLBACK mta_proc_b(HWND, UINT, UINT_PTR, DWORD);

/* Dispatches this thread's queue until `pred` is satisfied or ms elapsed. kind: 0 = window procedure
 * log has >= n entries of msg; 1 = proc A called >= n times; 2 = proc B called >= n times. */
int  mta_pump_until(int kind, UINT msg, int n, unsigned ms);
void mta_pump_ms(unsigned ms);
void mta_drain_timers(void);          /* removes every queued WM_TIMER without dispatching */
void mta_name(WCHAR *out, size_t cch, const char *tag);   /* win32kt_mta_<pid>_<tag> */

/* ---- USER handle table (type TIMER == 16) --------------------------------------------------- */
#define MTA_TYPE_TIMER 16
struct mta_hent { unsigned long long delta, owner, f10; unsigned char type, flags; unsigned short uniq; };
int      mta_table_ok(void);                       /* gSharedInfo readable and stride is the verified 0x20 */
unsigned mta_timers(void);                         /* live timer entries owned by this process */
unsigned mta_entry_count(void);
bool     mta_read_entry(unsigned idx, struct mta_hent *e);
/* indexes of this process's live timer entries (up to max); returns the count */
unsigned mta_timer_indexes(unsigned *idx, unsigned max);
DWORD    mta_guires(void);                         /* GetGuiResources(self, GR_USEROBJECTS) */

/* ---- helper owner thread --------------------------------------------------------------------- */
struct mta_owner {
    HANDLE th, ready, stop;
    HWND hwnd; DWORD tid;
    int keep_window;            /* 1: leave the window alive at thread exit (thread teardown destroys it) */
    int make_window;            /* 0: GUI thread without a window */
    int pump;                   /* 1: dispatch messages until stop */
};
bool mta_owner_start(struct mta_owner *o, int make_window, int pump, int keep_window);
void mta_owner_stop(struct mta_owner *o);
/* runs f on the owner thread's GUI context in lock step is not provided: tests use cross-thread API calls */

/* ---- child helpers ---------------------------------------------------------------------------- */
UINT mta_register_utf16(const WCHAR *s);           /* through user32!RegisterWindowMessageW */
#endif
