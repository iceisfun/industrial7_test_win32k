/* x_menu_accel_hotkey_lifetime: shared declarations.
 *
 * Syscalls reached through win32u.dll: accelerator-table create/copy/destroy, EndMenu, SetMenuFlagRtoL, RegisterHotKey /
 * UnregisterHotKey, SetImeHotKey / GetImeHotKey, MNDragOver / MNDragLeave. The area is about who owns what and for how
 * long: a table, menu or hotkey made on one thread or in one process, used or removed from another, and what is left
 * after the owning thread or process is gone. Signatures come from the user32 callers and the kernel bodies; none is
 * in an SDK header. Every key registered here is unregistered (or released by its owner's exit) before the test ends.
 */
#ifndef XM_H
#define XM_H
#include "t.h"
#include "k.h"
#include <setjmp.h>

UDECL(HANDLE,    NtUserCreateAcceleratorTable, (void *, int));
UDECL(int,       NtUserCopyAcceleratorTable, (HANDLE, void *, int));
UDECL(DWORD,     NtUserDestroyAcceleratorTable, (HANDLE));
UDECL(BOOL,      NtUserEndMenu, (void));
UDECL(BOOL,      NtUserSetMenuFlagRtoL, (HMENU));
UDECL(BOOL,      NtUserRegisterHotKey, (HWND, int, UINT, UINT));
UDECL(BOOL,      NtUserUnregisterHotKey, (HWND, int));
UDECL(BOOL,      NtUserSetImeHotKey, (DWORD, UINT, UINT, HKL, DWORD));
UDECL(BOOL,      NtUserGetImeHotKey, (DWORD, UINT *, UINT *, HKL *));
UDECL(BOOL,      NtUserMNDragOver, (POINT *, void *));
UDECL(BOOL,      NtUserMNDragLeave, (void));

/* Exceptions out of a syscall are findings on Windows (the test ends); under an emulator they are a skip. */
extern __thread jmp_buf xm_jb;
extern __thread volatile int xm_armed;
extern __thread volatile unsigned long xm_exc;
unsigned long xm_try(void (*f)(void *), void *arg);
void xm_run_guarded(void (*f)(void));
#define XT(n) \
    static void body_##n(void); \
    static void test_##n(void) { xm_run_guarded(body_##n); } \
    static void __attribute__((constructor)) reg_##n(void) { t_register("x_menu_accel_hotkey_lifetime_" #n, test_##n); } \
    static void body_##n(void)
#define NEED(n) do { if (!UN(n)) { t_skip("missing export: " #n); return; } if (t_is_wine()) { t_skip("emulator: " #n " is not a win32k service"); return; } } while (0)
#define NEED_ACC() do { NEED(NtUserCreateAcceleratorTable); NEED(NtUserCopyAcceleratorTable); NEED(NtUserDestroyAcceleratorTable); } while (0)
#define NEED_HK()  do { NEED(NtUserRegisterHotKey); NEED(NtUserUnregisterHotKey); } while (0)
#define NEED_IHK() do { NEED(NtUserSetImeHotKey); NEED(NtUserGetImeHotKey); } while (0)

#define XE_INVALID_PARAMETER 87
#define XE_ACCESS_DENIED      5
#define XE_NOACCESS         998
#define XE_INVALID_MENU_HANDLE 1401
#define XE_INVALID_ACCEL_HANDLE 1403
#define XE_WINDOW_OF_OTHER_THREAD 1408
#define XE_HOTKEY_ALREADY_REGISTERED 1409
#define XE_INVALID_HWND     1400
#define XE_HOTKEY_NOT_REGISTERED 1419
#define XE_SENT 0x4D2

/* The call must fail: low 32 bits of the return value are 0 and this last error. */
void xm_fail(LONGLONG ret, DWORD gle, DWORD want, const char *file, int line, const char *what);
#define XFAIL(call, want) do { SET_ERR(XE_SENT); LONGLONG r__ = (LONGLONG)(call); xm_fail(r__, GetLastError(), (want), __FILE__, __LINE__, #call); } while (0)
/* The call must succeed with this value (low 32 bits). */
#define XOK(call, val) do { SET_ERR(XE_SENT); LONGLONG r__ = (LONGLONG)(call); \
    t_check((r__ & 0xFFFFFFFF) == (LONGLONG)(unsigned)(val), __FILE__, __LINE__, #call, "got 0x%llx want 0x%llx", (unsigned long long)r__, (unsigned long long)(unsigned)(val)); } while (0)

/* ---- accelerator tables ---- */
typedef struct { BYTE fVirt, pad; WORD key, cmd; } xm_acc6;   /* the 6-byte object format */
#define AL4 __attribute__((aligned(4)))
void xm_acc_fill(xm_acc6 *a, int n);
/* Creates an n-entry table through the syscall (entries FVIRTKEY, key 'A'+i%26, cmd 1000+i). */
HANDLE xm_acc_make(int n);

/* ---- GUI helper thread ---- */
/* A thread that creates a hidden tool window, pumps its queue, runs `job` (if any) when told, and exits when released. */
struct xm_thr { HANDLE thread, ready, stop; HWND hwnd; DWORD tid; volatile LONG flag; };
int  xm_thr_start(struct xm_thr *t);
void xm_thr_stop(struct xm_thr *t);

/* ---- hotkey keys ---- */
struct xm_key { UINT mods, vk; };
/* The nth unique (mods, vk): VK_F13..VK_F24 with one to three of alt, ctrl, shift. */
struct xm_key xm_key_next(void);
#define KEY(k) struct xm_key k = xm_key_next()

/* ---- child processes ---- */
/* A shared page by name; both sides poll the fields (no event objects to leak). */
struct xm_shm { volatile LONG ready, go, done; volatile LONG64 v[16]; volatile LONG e[16]; };
struct xm_shm *xm_shm_create(const char *tag, HANDLE *h);
struct xm_shm *xm_shm_open(const char *tag, HANDLE *h);
int  xm_wait(volatile LONG *flag, DWORD ms);          /* 1 when *flag becomes non-zero */
/* Launches this exe as the named child with the shared page tag; returns the process handle or NULL. */
HANDLE xm_spawn(const char *child, const char *tag, const char *extra, PROCESS_INFORMATION *pi);
/* Waits for exit; returns the exit code (0xDEAD0001 after a kill on timeout). */
DWORD xm_reap(HANDLE p, PROCESS_INFORMATION *pi, DWORD ms);
const char *xm_tag(void);                              /* unique per call: pid and a counter */
/* The key is free again within ms (polls registration through the syscall, unregisters it on success). */
int  xm_key_free_within(struct xm_key k, DWORD ms);
#define XM_NIMBLE_MS 5000

/* ---- a thread that tracks a popup menu ---- */
/* The thread creates an off-screen owner window and a two-item popup menu (items 7 and 8, optionally with
 * MNS_DRAGDROP) and runs TrackPopupMenu at (200, 200) with TPM_RETURNCMD; a timer inside the modal loop calls
 * on_tick(m, n) at every tick (n counts from 1) and, once `rel` is signalled, calls EndMenu itself. */
struct xm_menu {
    HANDLE thread, rel; HWND w; HMENU menu; DWORD tid; DWORD mnstyle;
    volatile LONG state;      /* 0 starting, 1 in TrackPopupMenu, 2 returned */
    volatile LONG ret, ticks, endret, endcalled;
    void (*on_tick)(struct xm_menu *, int);
    void *arg; LONG64 scratch[8];
};
/* Starts the thread and waits until it is in menu mode (GetGUIThreadInfo); returns 0 when it never got there. */
int  xm_menu_start(struct xm_menu *m, DWORD mnstyle, void (*on_tick)(struct xm_menu *, int), void *arg);
/* Asks the thread to end its own menu, joins it and returns the TrackPopupMenu result (-1 on timeout). */
LONG xm_menu_end(struct xm_menu *m);
/* GetGUIThreadInfo flags of a thread (0 when the call fails). */
DWORD xm_gui_flags(DWORD tid);
/* The menu thread is still in menu mode and has not returned. */
int  xm_menu_alive(struct xm_menu *m);

/* ---- a thread that takes commands ---- */
/* One thread, optionally with a hidden window, that registers and unregisters hotkeys on request so a test can drive two
 * threads in lock step. xm_ct_do posts the command and waits for it to finish; the result is in ret / gle. */
enum { CT_REG = 1, CT_UNREG, CT_DESTROYWND, CT_QUIT };
struct xm_ct {
    HANDLE thread, ready; HWND w; DWORD tid;
    volatile LONG cmd, busy; int use_wnd, id; UINT mods, vk;
    volatile LONG64 ret; volatile DWORD gle;
};
int  xm_ct_start(struct xm_ct *t, int with_window);
/* use_wnd: register/unregister against the thread's own window instead of NULL. Returns ret. */
LONG64 xm_ct_do(struct xm_ct *t, int cmd, int use_wnd, int id, UINT mods, UINT vk);
void xm_ct_stop(struct xm_ct *t);          /* quit and join (thread exit releases what it held) */
#define CT_REGISTER(t, wnd, id, k)  xm_ct_do((t), CT_REG, (wnd), (id), (k).mods, (k).vk)
#define CT_UNREGISTER(t, wnd, id)   xm_ct_do((t), CT_UNREG, (wnd), (id), 0, 0)
#endif
