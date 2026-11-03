/* r2_ntusercall_codes_matrix - shared declarations.
 *
 * win32kfull.sys (build 19041.2965) exposes eight NtUserCall* dispatchers plus NtUserCallMsgFilter.
 * Each is "validate / lock, range-check the code, call table[code]". The code space is one
 * contiguous partition (code ranges as observed from the range checks):
 *   NtUserCallNoParam        0x00..0x2b   (code 8 is special-cased before the range check)
 *   NtUserCallOneParam       0x2c..0x53   (0x28 codes)
 *   NtUserCallHwnd           0x54..0x5b   (8 codes)
 *   NtUserCallHwndOpt        0x5c..0x5d   (2 codes)
 *   NtUserCallHwndParam      0x5e..0x68   (0xb codes)
 *   NtUserCallHwndLock       0x69..0x75   (0xd codes)
 *   NtUserCallHwndParamLock  0x76..0x7d   (8 codes)
 *   NtUserCallTwoParam       0x7e..0x8d   (0x10 codes)
 * and three "Safe"/"LockSafe" variants that accept only a sub-range (HwndSafe 0x5a,0x5b;
 * HwndLockSafe 0x6b; HwndParamLockSafe 0x7d).
 * Every Hwnd dispatcher calls ValidateHwnd(hwnd) BEFORE it looks at the code, so an invalid handle
 * yields 0 with ERROR_INVALID_WINDOW_HANDLE (1400) for every code, valid or not.
 */
#ifndef MX_H
#define MX_H
#include "t.h"
#include "k.h"

UDECL(void *, NtUserCallNoParam,            (DWORD code));
UDECL(void *, NtUserCallOneParam,           (ULONG_PTR p, DWORD code));
UDECL(void *, NtUserCallTwoParam,           (ULONG_PTR p1, ULONG_PTR p2, DWORD code));
UDECL(void *, NtUserCallHwnd,               (HWND h, DWORD code));
UDECL(void *, NtUserCallHwndOpt,            (HWND h, DWORD code));
UDECL(void *, NtUserCallHwndSafe,           (HWND h, DWORD code));
UDECL(void *, NtUserCallHwndLock,           (HWND h, DWORD code));
UDECL(void *, NtUserCallHwndLockSafe,       (HWND h, DWORD code));
UDECL(void *, NtUserCallHwndParam,          (HWND h, ULONG_PTR p, DWORD code));
UDECL(void *, NtUserCallHwndParamLock,      (HWND h, ULONG_PTR p, DWORD code));
UDECL(void *, NtUserCallHwndParamLockSafe,  (HWND h, ULONG_PTR p, DWORD code));
UDECL(void,   NtUserCallMsgFilterV,         (MSG *m, int nCode));
UDECL(int,    NtUserCallMsgFilter,          (MSG *m, int nCode));

#define NP(c)            ((ULONG_PTR)UN(NtUserCallNoParam)((DWORD)(c)))
#define OP(a, c)         ((ULONG_PTR)UN(NtUserCallOneParam)((ULONG_PTR)(a), (DWORD)(c)))
#define TP(a, b, c)      ((ULONG_PTR)UN(NtUserCallTwoParam)((ULONG_PTR)(a), (ULONG_PTR)(b), (DWORD)(c)))
#define HW(h, c)         ((ULONG_PTR)UN(NtUserCallHwnd)((HWND)(h), (DWORD)(c)))
#define HWO(h, c)        ((ULONG_PTR)UN(NtUserCallHwndOpt)((HWND)(h), (DWORD)(c)))
#define HWS(h, c)        ((ULONG_PTR)UN(NtUserCallHwndSafe)((HWND)(h), (DWORD)(c)))
#define HWL(h, c)        ((ULONG_PTR)UN(NtUserCallHwndLock)((HWND)(h), (DWORD)(c)))
#define HWLS(h, c)       ((ULONG_PTR)UN(NtUserCallHwndLockSafe)((HWND)(h), (DWORD)(c)))
#define HWP(h, p, c)     ((ULONG_PTR)UN(NtUserCallHwndParam)((HWND)(h), (ULONG_PTR)(p), (DWORD)(c)))
#define HWPL(h, p, c)    ((ULONG_PTR)UN(NtUserCallHwndParamLock)((HWND)(h), (ULONG_PTR)(p), (DWORD)(c)))
#define HWPLS(h, p, c)   ((ULONG_PTR)UN(NtUserCallHwndParamLockSafe)((HWND)(h), (ULONG_PTR)(p), (DWORD)(c)))
#define LO32(x)          ((DWORD)(ULONG_PTR)(x))
#define B8(x)            (LO32(x) & 0xff)   /* bool/BOOL results: only the low byte is defined for 'bool' returns */

#define ERR_INVALID_WINDOW_HANDLE 1400u  /* window handle validation failure */
#define SENTINEL_ERR              0x5A5Au
#define ERR_INVALID_PARAMETER     87u
#define ERR_ACCESS_DENIED         5u
#define ERR_CLIPBOARD_NOT_OPEN    1418u  /* 0x58a, _EnumClipboardFormats */

/* Wine's win32u has its own NtUserCall* numbering; invoking a Windows code there may abort. */
#define NEED_RAW(fn) do { \
    if (t_is_wine()) { t_skip("Wine has its own NtUserCall* code table; Windows is the authority"); return; } \
    REQUIRE(UN(fn)); } while (0)
#define NEED_ALL_HWND() do { \
    if (t_is_wine()) { t_skip("Wine has its own NtUserCall* code table; Windows is the authority"); return; } \
    REQUIRE(UN(NtUserCallHwnd)); REQUIRE(UN(NtUserCallHwndOpt)); REQUIRE(UN(NtUserCallHwndSafe)); \
    REQUIRE(UN(NtUserCallHwndLock)); REQUIRE(UN(NtUserCallHwndLockSafe)); REQUIRE(UN(NtUserCallHwndParam)); \
    REQUIRE(UN(NtUserCallHwndParamLock)); REQUIRE(UN(NtUserCallHwndParamLockSafe)); } while (0)

/* On Windows 10 a process without user32 has no PEB.KernelCallbackTable: its first win32k syscall converts the thread, the
 * callback into user mode cannot be dispatched and the process dies (access violation on the calling thread, then
 * STATUS_FATAL_USER_CALLBACK_EXCEPTION 0xC000041D). The exe does not link user32, and win32u is first in the module list, so
 * every test loads user32 first, as any real GUI process has. */
void mx_init(void);
#define T(n)    static void mxt_##n(void); \
                TEST(r2_ntusercall_codes_matrix_##n) { mx_init(); mxt_##n(); } \
                static void mxt_##n(void)
#define TCHILD(n) CHILD(r2_ntusercall_codes_matrix_##n)
#define TNAME(n) "r2_ntusercall_codes_matrix_" #n

/* A background GUI thread that owns one hidden window and pumps until told to stop. */
struct mx_bg {
    HANDLE th, ready, stop;
    DWORD  tid;
    HWND   hwnd;
    DWORD  helpid;   /* if nonzero, set as the window's context help id before signalling ready */
};
int  mx_bg_start(struct mx_bg *b, DWORD helpid);
void mx_bg_stop(struct mx_bg *b);

/* Snapshot of per-thread / per-session state that no out-of-range dispatch may touch. */
struct mx_snap {
    HWND fg, cap, focus, act; POINT cur; UINT blink, dbl; void *w32ti; LONG_PTR extra;
    DWORD layout; int swap; HMENU dummy;
};
void mx_snap_take(struct mx_snap *s);
int  mx_snap_equal(const struct mx_snap *a, const struct mx_snap *b);

/* A visible-but-off-screen, non-activating window (IsWindowVisible() true). */
HWND mx_visible_window(HWND owner, DWORD style_extra);
#endif
