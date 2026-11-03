/* Shared helpers for the dx_desktop_switch area (DESTRUCTIVE: run only with /destructive).
 *
 * Kernel entry points covered (win32u stubs; service table win32k.sys -> win32kbase/win32kfull):
 *   NtUserSwitchDesktop(hdesk, fade, flags)          win32kfull!NtUserSwitchDesktop
 *   NtUserOpenInputDesktop(flags, inherit, access)   win32kbase thunk -> win32kfull!EditionOpenInputDesktopEntryPoint
 *   NtUserCreateDesktopEx / NtUserCreateWindowStation (via the user32 Common* wrappers)
 *   NtUserGetThreadDesktop / NtUserSetThreadDesktop  win32kfull!NtUserSetThreadDesktop
 *   NtUserCloseDesktop                               win32kfull!_CloseDesktop (EditionCloseDesktopEntryPoint)
 *   NtUserSetProcessWindowStation / GetProcessWindowStation
 *
 * Safety model: the input desktop of the interactive window station is switched only for a short
 * dwell and always restored. dxd_guard_begin() records the original input desktop and starts a
 * CHILD watchdog process that switches back if this process dies or leaves the original desktop
 * for more than a few seconds. No test in this area calls any logoff/shutdown/lock API.
 */
#ifndef DXD_H
#define DXD_H
#include "t.h"
#include "k.h"
#include <winternl.h>

#define DXT(n) TEST(dx_desktop_switch_##n)

extern int dxd_cf;   /* failure counter for CHILD() bodies; children return it as their exit code */
#define CK(c) do { int ok_ = !!(c); if (!ok_) dxd_cf++; t_check(ok_, __FILE__, __LINE__, #c, NULL); } while (0)
#define CKF(c, ...) do { int ok_ = !!(c); if (!ok_) dxd_cf++; t_check(ok_, __FILE__, __LINE__, #c, __VA_ARGS__); } while (0)

#ifndef DESKTOP_ALL_RIGHTS
#define DESKTOP_ALL_RIGHTS 0x1FF   /* READOBJECTS..SWITCHDESKTOP */
#endif
#define DXD_UOI_FLAGS 1
#define DXD_UOI_NAME  2
#define DXD_UOI_TYPE  3
#ifndef WSF_VISIBLE
#define WSF_VISIBLE 0x0001
#endif
#ifndef CWF_CREATE_ONLY
#define CWF_CREATE_ONLY 0x00000001
#endif
#define DXD_WINSTA_ALL 0x37F       /* WINSTA_ALL_ACCESS */

/* ---- direct win32u syscall stubs. Argument lists follow the user32 callers (analysis):
 *   user32!SwitchDesktop      -> NtUserSwitchDesktop(hdesk, 0, 0)
 *   user32!OpenInputDesktop   -> NtUserOpenInputDesktop(dwFlags, fInherit, dwDesiredAccess)
 *   user32!SetThreadDesktop   -> NtUserSetThreadDesktop(hdesk)
 *   user32!CloseDesktop       -> NtUserCloseDesktop(hdesk) */
typedef DWORD  (WINAPI *p_NtUserSwitchDesktop)(HANDLE, int, DWORD);
typedef HANDLE (WINAPI *p_NtUserOpenInputDesktop)(DWORD, BOOL, DWORD);
typedef BOOL   (WINAPI *p_NtUserCloseDesktop)(HANDLE);
typedef BOOL   (WINAPI *p_NtUserSetThreadDesktop)(HANDLE);
typedef HANDLE (WINAPI *p_NtUserGetThreadDesktop)(DWORD);
typedef LONG   (NTAPI  *p_NtQueryObject)(HANDLE, int, PVOID, ULONG, PULONG);
typedef BOOL   (WINAPI *p_CompareObjectHandles)(HANDLE, HANDLE);
#define NTU(name) ((p_##name)t_proc_in("win32u.dll", #name))

/* ---- guard (guard.c) -------------------------------------------------------------------------- */
int  dxd_guard_begin(void);                     /* 1 = original recorded + watchdog running; 0 = unavailable */
int  dxd_restore(void);                         /* switch back to the original input desktop, retrying; 1 = ok */
BOOL dxd_input_name(WCHAR *out, size_t cch);    /* UOI_NAME of the current input desktop */
const WCHAR *dxd_orig_name(void);
HDESK dxd_orig_handle(void);                    /* OpenInputDesktop(DESKTOP_SWITCHDESKTOP) handle taken at guard start */
/* Standard prologue of a test that switches the input desktop: returns 1 when the test may proceed. */
int  dxd_begin(void);
void dxd_expect_unchanged(void);                 /* CHECK input == original; restore if not */
#define DXD_BEGIN() do { if (!dxd_begin()) return; } while (0)

/* ---- small utilities -------------------------------------------------------------------------- */
void  dxd_name(WCHAR *out, const char *tag);                  /* win32kt<pid>_<tag> */
BOOL  dxd_uoi_str(HANDLE h, int idx, WCHAR *out, DWORD cch);
BOOL  dxd_obj_type(HANDLE h, WCHAR *out, size_t cch);         /* NtQueryObject ObjectTypeInformation */
BOOL  dxd_granted(HANDLE h, ULONG *granted);                  /* NtQueryObject ObjectBasicInformation */
HDESK dxd_mk_desk(const char *tag, DWORD access, WCHAR *name_out);
BOOL  dxd_same_object(HANDLE a, HANDLE b);                    /* CompareObjectHandles when present, else name+type equality */
BOOL  dxd_winsta_name(WCHAR *out, size_t cch);                /* name of the process window station */

struct dxd_sw { BOOL rc; DWORD err; WCHAR in_name[128]; BOOL in_ok; };
/* SwitchDesktop(d), optionally dwell, then sample the input desktop name. Never restores: caller must. */
void  dxd_do_switch(HDESK d, struct dxd_sw *s, DWORD dwell_ms);

/* ---- worker thread living on a given desktop, optionally with a counting window -------------- */
#define DXD_WM_COUNT (WM_APP + 5)
struct dxd_thr {
    HDESK desk;                 /* NULL = keep the inherited desktop */
    int   want_window, visible; /* create a (visible) hidden-style popup window */
    void  (*action)(struct dxd_thr *);   /* runs on the worker after the window exists */
    void  *user;
    /* results */
    HANDLE ready, done, th;
    HWND   hw; DWORD tid;
    BOOL   set_ok; DWORD set_err; HDESK got;
    volatile LONG hits;         /* DXD_WM_COUNT messages received by the window procedure */
    WCHAR  cls[96];
    LONG_PTR r[6];
};
int  dxd_thr_start(struct dxd_thr *t, const char *tag);   /* 1 = ready signalled within 5s */
void dxd_thr_stop(struct dxd_thr *t);
int  dxd_have_window(struct dxd_thr *t);   /* 1 if the worker has its window (or this is an emulator without windows: then skips) */

/* ---- process on a named desktop ---------------------------------------------------------------- */
HANDLE dxd_spawn_on_desktop(const char *child, const char *extra, const char *winsta_desk, PROCESS_INFORMATION *pi);

#endif
