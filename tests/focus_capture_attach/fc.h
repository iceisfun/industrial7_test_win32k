/* Shared fixtures for the focus_capture_attach area: per-queue input state (focus, active, capture,
 * caret), GetGUIThreadInfo, AttachThreadInput, EnableWindow. Windows are tiny, off-screen
 * WS_EX_TOOLWINDOW popups shown with SW_SHOWNOACTIVATE (or kept hidden); a logging window
 * procedure records the focus/activation/enable/capture notifications they receive. */
#ifndef FC_H
#define FC_H
#include "t.h"
#include "k.h"

/* ---- win32u syscall stubs that are not in any mingw header (signatures from the win32kfull
 * analysis: NtUserSetFocus(hwnd), NtUserGetGUIThreadInfo(tid, pgui), ...). ---------------- */
UDECL(HWND,      NtUserSetFocus, (HWND));
UDECL(HWND,      NtUserSetActiveWindow, (HWND));
UDECL(HWND,      NtUserSetCapture, (HWND));
UDECL(HWND,      NtUserGetForegroundWindow, (void));
UDECL(ULONG_PTR, NtUserGetThreadState, (DWORD));
UDECL(BOOL,      NtUserGetGUIThreadInfo, (DWORD, PGUITHREADINFO));
UDECL(BOOL,      NtUserAttachThreadInput, (DWORD, DWORD, BOOL));
UDECL(BOOL,      NtUserCreateCaret, (HWND, HBITMAP, int, int));
UDECL(BOOL,      NtUserShowCaret, (HWND));
UDECL(BOOL,      NtUserHideCaret, (HWND));
UDECL(BOOL,      NtUserGetCaretPos, (POINT *));
UDECL(UINT,      NtUserGetCaretBlinkTime, (void));
UDECL(SHORT,     NtUserGetKeyState, (int));
UDECL(SHORT,     NtUserGetAsyncKeyState, (int));
UDECL(ULONG_PTR, NtUserCallNoParam, (DWORD));
UDECL(ULONG_PTR, NtUserCallOneParam, (ULONG_PTR, DWORD));
UDECL(ULONG_PTR, NtUserCallHwndLock, (HWND, DWORD));
UDECL(ULONG_PTR, NtUserCallHwndParamLockSafe, (HWND, ULONG_PTR, DWORD));

/* NtUserCallNoParam / OneParam / HwndLock / HwndParamLockSafe function codes (the dispatch
 * indices user32 passes: DestroyCaret=6, ReleaseCapture=0x16, AllowSetForegroundWindow=0x2e,
 * LockSetForegroundWindow=0x36, SetCaretBlinkTime=0x43, SetForegroundWindow=0x70, EnableWindow=0x7d). */
#define FC_CALL_DESTROYCARET   6
#define FC_CALL_RELEASECAPTURE 0x16
#define FC_CALL_ALLOWSFW       0x2e
#define FC_CALL_LOCKSFW        0x36
#define FC_CALL_SETFOREGROUND  0x70
#define FC_CALL_ENABLEWINDOW   0x7d

/* NtUserGetThreadState information classes (win32kfull!NtUserGetThreadState). */
#define FC_TS_FOCUS      0
#define FC_TS_ACTIVE     1
#define FC_TS_CAPTURE    2
#define FC_TS_INPUTSTATE 5
#define FC_TS_ISFOREGROUND 10
#define FC_TS_KEYSTATE_OK 14

/* ---- message log ----------------------------------------------------------------------------- */
#define FC_LOG_MAX 1024
struct fc_msg {
    UINT msg; HWND hwnd; WPARAM wp; LPARAM lp; DWORD tid;
    LONG seq;
    int is_window;          /* IsWindow(hwnd) when delivered */
    LONG style_now;         /* GWL_STYLE when delivered */
    HWND focus_now, active_now, capture_now; /* this thread's queue state when delivered */
};
extern struct fc_msg fc_log[FC_LOG_MAX];
extern volatile LONG fc_nlog;
void fc_reset(void);
int  fc_own_foreground(void);                          /* the foreground window belongs to this process */
int  fc_find(UINT msg, HWND h, int from);             /* first index >= from, or -1 */
int  fc_find_wp(UINT msg, HWND h, WPARAM wp, int from);
int  fc_find_lo(UINT msg, HWND h, WORD lo, int from); /* LOWORD(wp)==lo (WM_ACTIVATE) */
int  fc_count(UINT msg, HWND h);                      /* h==NULL: any window */
int  fc_total(void);

/* ---- windows --------------------------------------------------------------------------------- */
int   fc_user(void);                                  /* the user32 exports every test relies on */
/* t_skip() returns, so a skip must be followed by an explicit return. */
#define FC_SKIP(...) do { t_skip(__VA_ARGS__); return; } while (0)
#define FC_NEED() do { if (!fc_user()) { t_skip("missing user32 export"); return; } } while (0)
const WCHAR *fc_class(void);
HWND  fc_make(DWORD style, DWORD ex, HWND parent_or_owner, int visible);
HWND  fc_top(void);                                   /* visible (SW_SHOWNOACTIVATE) off-screen popup */
HWND  fc_hidden(void);                                /* never shown */
HWND  fc_owned(HWND owner);                           /* off-screen owned popup, shown no-activate */
HWND  fc_child(HWND parent);                          /* visible 20x20 child */
void  fc_destroy(HWND h);
void  fc_fg_restore(void);
#define FC_WM_CALL (WM_APP + 0x70)  /* wParam = DWORD_PTR (*)(void*), lParam = arg; runs on the window's thread */

/* Put this thread's queue in a known quiet state: no capture, no focus, empty log. */
void  fc_quiesce(void);

/* ---- worker GUI threads ---------------------------------------------------------------------- */
struct fc_thr {
    HANDLE th; DWORD tid; HWND w[2];
    HANDLE ready; int ok;
};
int   fc_thr_start(struct fc_thr *t);                 /* creates two windows, pumps messages */
int   fc_thr_call(struct fc_thr *t, DWORD_PTR (*fn)(void *), void *arg, DWORD_PTR *res);
void  fc_thr_stop(struct fc_thr *t);                  /* joins (bounded), terminates as last resort */

/* attach/detach helpers that always restore: detach until the kernel refuses. */
void  fc_detach_all(DWORD a, DWORD b);

/* local USER shared state */
bool  fc_psi_dword(unsigned off, unsigned long *out); /* SERVERINFO field via gSharedInfo.psi */
#define FC_PSI_DWKEYCACHE   0x1b48
#define FC_PSI_DWASYNCCACHE 0x1b4c
#define FC_PSI_CCAPTURES    0x1b50
#define FC_PSI_CARETBLINK   0x1378
#define FC_CI_PCTI          0x060 /* _CLIENTINFO.pClientThreadInfo (user32 GetInputState: Self+0x860) */
#define FC_CI_DWKEYCACHE    0x070 /* user32 GetKeyState: Self+0x870 */
#define FC_CI_AFKEYSTATE    0x074 /* 8 bytes, 2 bits per vk < 0x20: bit0 down, bit1 toggle */
#define FC_CI_DWASYNCCACHE  0x07c
#define FC_CI_AFASYNC       0x080
unsigned long fc_ci_dword(unsigned off);              /* TEB.Win32ClientInfo + off, 0 if unreadable */

/* GUITHREADINFO helper: fills with 0xCC first. returns the call result. */
BOOL  fc_gti(DWORD tid, GUITHREADINFO *g);

/* EXPECT_EQ evaluates its arguments twice: use this for calls with side effects. */
#define EXPECT_CALL_EQ(expr, want) do { ULONG_PTR v_ = (ULONG_PTR)(expr); EXPECT_EQ(v_, want); } while (0)
#endif
