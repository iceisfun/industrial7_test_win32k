/* hooks_winevents: shared declarations.
 *
 * Kernel side under test: NtUserSetWindowsHookEx/hook installation, NtUserUnhookWindowsHookEx,
 * NtUserCallNextHookEx, NtUserCallMsgFilter, the hook-call paths (the hook callbacks into user32), NtUserSetWinEventHook, NtUserUnhookWinEvent,
 * NtUserNotifyWinEvent/the WinEvent dispatch path.
 *
 * Syscall signatures (from the user32 callers and measured behaviour):
 *   ULONG_PTR NtUserSetWindowsHookEx(HMODULE hmod, PUNICODE_STRING pstrLib, int tid, int idHook, void *proc, DWORD flags)
 *       hmod == PEB->ImageBaseAddress is zeroed (hmod=0, pstrLib ignored); pstrLib is only read when hmod != 0.
 *       flags: user32 passes 0 (W) or 2 (A); the kernel keeps flags & 0x42.
 *   int       NtUserUnhookWindowsHookEx(HHOOK)
 *   LRESULT   NtUserCallNextHookEx(int nCode, WPARAM wParam, LPARAM lParam, BOOL bAnsi)
 *   ULONG_PTR NtUserSetWinEventHook(DWORD min, DWORD max, HMODULE hmod, PUNICODE_STRING pstrLib, void *pfn,
 *                                   DWORD idProcess, DWORD idThread, DWORD flags)
 *   int       NtUserUnhookWinEvent(HWINEVENTHOOK)
 *   BYTE      NtUserNotifyWinEvent(DWORD event, HWND, LONG idObject, LONG idChild)   (kernel returns bool)
 *
 * Per-thread hook state the kernel keeps in the TEB (client info in the TEB):
 *   CI+0x20 pDeskInfo, CI+0x30 phkCurrent (handle of the NEXT hook in the chain while inside a hook proc, else 0),
 *   CI+0x38 fsHooks (bit (1 << (idHook + 1)) per installed thread hook), CI+0x58 dwHookCurrent (id << 16 | ansi).
 */
#ifndef HK_H
#define HK_H
#include "t.h"
#include "k.h"

#define HKE_INVALID_FLAGS        1004
#define HKE_DLL_NOT_FOUND        1157
#define HKE_INVALID_HOOK_HANDLE  1404
#define HKE_INVALID_HOOK_FILTER  1426
#define HKE_INVALID_FILTER_PROC  1427
#define HKE_HOOK_NEEDS_HMOD      1428
#define HKE_GLOBAL_ONLY_HOOK     1429
#define HKE_INVALID_THREAD_ID    1444

UDECL(ULONG_PTR, NtUserSetWindowsHookEx, (HMODULE, PUNICODE_STRING, int, int, void *, DWORD));
UDECL(int, NtUserUnhookWindowsHookEx, (HHOOK));
UDECL(LRESULT, NtUserCallNextHookEx, (int, WPARAM, LPARAM, int));
UDECL(ULONG_PTR, NtUserSetWinEventHook, (DWORD, DWORD, HMODULE, PUNICODE_STRING, void *, DWORD, DWORD, DWORD));
UDECL(int, NtUserUnhookWinEvent, (HWINEVENTHOOK));
UDECL(unsigned char, NtUserNotifyWinEvent, (DWORD, HWND, LONG, LONG));
UDECL(BOOL, IsWinEventHookInstalled, (DWORD));

/* kernel callback table slots (PEB->KernelCallbackTable index == KeUserModeCallback ApiNumber) */
#define HK_CB_HKINDWORD      40  /* 0x28 DWORD hook callback: KEYBOARD, SHELL, FOREGROUNDIDLE, CBT codes other than 0/3/5/6 */
#define HK_CB_CBTACTIVATE    41  /* 0x29 */
#define HK_CB_CBTCREATE      42  /* 0x2a */
#define HK_CB_DEBUG          43  /* 0x2b */
#define HK_CB_MOUSEEX        44  /* 0x2c */
#define HK_CB_HKINLPMSG      47  /* 0x2f MSGFILTER, GETMESSAGE, SYSMSGFILTER */
#define HK_CB_HKINLPRECT     48  /* 0x30 */
#define HK_CB_WINEVENT       88  /* 0x58 WinEvent callback */

/* thread-capable and global-only hook ids per the kernel's per-hook attribute table  */
extern const int hk_thread_ids[11];
extern const int hk_global_only_ids[5];

/* ---- TEB / _CLIENTINFO readers ---- */
#define HK_CI(off) ((char *)k_teb() + TEB_Win32ClientInfo + (off))
ULONG_PTR hk_ci_phkcurrent(void);
unsigned long hk_ci_fshooks(void);
unsigned long hk_ci_hookcur(void);
unsigned long hk_teb_fshooks(void *teb);           /* another thread's TEB (same process) */
unsigned long hk_deskinfo_fshooks(unsigned long *ok); /* pDeskInfo->fsHooks (shared desktop heap view) */
#define HK_BIT(id) (1UL << ((id) + 1))

/* ---- the recorder ---- */
enum { HK_K_RAW, HK_K_MSG, HK_K_CWP, HK_K_CWPRET };
struct hk_slot;
typedef void (*hk_extra_fn)(struct hk_slot *, int code, WPARAM wp, LPARAM lp);
typedef struct hk_slot {
    HHOOK hh; int id; int kind;
    UINT only_msg;          /* 0: record every call; else only calls whose message equals it (others pass through) */
    int only_code;          /* -1000: any */
    LRESULT ret;            /* returned when !callnext */
    int callnext; LRESULT add;
    hk_extra_fn extra;
    /* recorded */
    volatile LONG calls;    /* matching calls */
    volatile LONG total;    /* every call, matching or not */
    int first_order, last_order;
    int code; WPARAM wp; LPARAM lp;
    MSG msg; CWPSTRUCT cwp; CWPRETSTRUCT cwpr;
    ULONG_PTR ci_cur; unsigned long ci_hookcur, ci_fshooks;
    LRESULT next_result; DWORD tid;
    char raw[64];           /* first bytes of the string lParam of a CWP WM_SETTEXT */
    ULONG_PTR user[4];
} hk_slot;
#define HK_NSLOT 4
extern hk_slot hk_s[HK_NSLOT];
extern volatile LONG hk_seq;
void hk_reset(void);                                  /* zero slots + logs */
HHOOK hk_install(int slot, int id, int kind, BOOL ansi); /* thread hook on the current thread */
HHOOK hk_install_on(int slot, int id, int kind, BOOL ansi, DWORD tid);
void hk_remove(int slot);
void hk_remove_all(void);
LRESULT CALLBACK hk_nop_proc(int code, WPARAM wp, LPARAM lp);

/* sequence log shared by hook procs and window procs */
void hk_log_add(int tag, UINT msg, LRESULT v);
int  hk_log_n(void);
int  hk_log_tag(int i);
UINT hk_log_msg(int i);
int  hk_log_find(int tag, UINT msg);   /* index of first matching entry or -1 */
int  hk_msg_seen(UINT msg);            /* count of window-procedure deliveries of msg since hk_reset() */

/* ---- windows ---- */
const WCHAR *hk_class(void);
HWND hk_window(const char *tag);       /* hidden, off-screen, non-activating popup of the recording class */
HWND hk_window_ex(const char *tag, DWORD style, int x, int y, int cx, int cy);
int  hk_user_obj_valid(HANDLE h, int type);  /* USER handle table entry says live object of `type` */
int  hk_user_obj_dead(HANDLE h);             /* entry type 0 or destroyed bit */
unsigned long hk_gui_objects(void);    /* GetGuiResources(self, GR_USEROBJECTS) */

/* ---- worker thread with its own window and message loop ---- */
typedef struct hk_worker {
    HANDLE th, ready, stopev; DWORD tid; HWND hwnd;
    void (*init)(struct hk_worker *);   /* runs on the worker before it starts pumping */
    void *arg; ULONG_PTR out[4];
} hk_worker;
int  hk_worker_start(hk_worker *w, void (*init)(hk_worker *), void *arg);
void hk_worker_stop(hk_worker *w);

/* ---- a second process with a GUI thread (child "hooks_winevents_peer") ---- */
typedef struct hk_peer { HANDLE proc, rd, ev; DWORD pid, tid; HWND hwnd; } hk_peer;
int  hk_peer_start(hk_peer *p);
void hk_peer_stop(hk_peer *p);

/* ---- WinEvent recorder ---- */
struct we_rec { HWINEVENTHOOK h; DWORD ev; HWND hwnd; LONG obj, child; DWORD tid, time, cbtid; int order; };
#define WE_MAX 128
extern struct we_rec we_r[WE_MAX];
extern volatile LONG we_n;
void CALLBACK we_proc(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD);
void we_reset(void);
int  we_count_event(DWORD ev);
struct we_rec *we_find(DWORD ev, int nth);
/* The corpus was derived from build 19041; the 2004/20H2/21H1/21H2/22H2 servicing builds (19041..19045) share the callback ABI. */
static inline int hk_win10_2004_family(void) {
    unsigned long b = 0; k_win_build(&b, NULL);
    return !t_is_wine() && b >= 19041 && b <= 19045;
}
#endif
