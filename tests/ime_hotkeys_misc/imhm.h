/* ime_hotkeys_misc - shared helpers.
 *
 * Syscall signatures (all read from the win32kfull analysis, argument order from the
 * user32 / imm32 callers):
 *   BOOL  NtUserRegisterHotKey(HWND, int id, UINT mods, UINT vk)
 *   BOOL  NtUserUnregisterHotKey(HWND, int id)
 *   HIMC  NtUserCreateInputContext(PCLIENTIMC)      (imm32!ImmCreateContext passes a 0x58 byte heap block)
 *   BOOL  NtUserDestroyInputContext(HIMC)
 *   DWORD NtUserAssociateInputContext(HWND, HIMC, DWORD iace)     0 ok, 1 ok+focus window, 2 failure
 *   DWORD NtUserQueryInputContext(HIMC, DWORD qic)
 *   BOOL  NtUserUpdateInputContext(HIMC, DWORD code, PVOID)
 *   void  NtUserBuildHimcList(DWORD tid, DWORD cap, HIMC *buf, DWORD *count)
 *   BOOL  NtUserGetImeInfoEx(IMEINFOEX*, DWORD type)  (0x160 byte struct)
 *   BOOL  NtUserGetImeHotKey(DWORD id, UINT *mods, UINT *vk, HKL *hkl_or_null)
 *   BOOL  NtUserSetImeHotKey(DWORD id, UINT mods, UINT vk, HKL hkl, DWORD action 1=del 2=set 3=reset)
 *   BOOL  NtUserDisableThreadIme(DWORD tid)
 *   BOOL  NtUserSetWindowDisplayAffinity(HWND, DWORD)   BOOL NtUserGetWindowDisplayAffinity(HWND, DWORD*)
 *   BOOL  NtUserSetLayeredWindowAttributes(HWND, COLORREF, BYTE, DWORD)
 *   BOOL  NtUserGetLayeredWindowAttributes(HWND, COLORREF*, BYTE*, DWORD*)
 *   BOOL  NtUserSetShellWindowEx(HWND shell, HWND listview)
 *   BOOL  NtUserUserHandleGrantAccess(HANDLE userobj, HANDLE job, BOOL grant)
 *   BOOL  NtUserSetProcessRestrictionExemption(BOOL)
 *   HMENU NtUserGetSystemMenu(HWND, BOOL revert)
 */
#ifndef IMHM_H
#define IMHM_H
#include "k.h"
#include <stdint.h>
#include <imm.h>

#ifndef MOD_NOREPEAT
#define MOD_NOREPEAT 0x4000
#endif
#ifndef ERROR_HOTKEY_ALREADY_REGISTERED
#define ERROR_HOTKEY_ALREADY_REGISTERED 1409
#endif
#ifndef ERROR_HOTKEY_NOT_REGISTERED
#define ERROR_HOTKEY_NOT_REGISTERED 1419
#endif
#ifndef ERROR_WINDOW_OF_OTHER_THREAD
#define ERROR_WINDOW_OF_OTHER_THREAD 1408
#endif
#ifndef ERROR_INVALID_FLAGS
#define ERROR_INVALID_FLAGS 1004
#endif
#ifndef ERROR_INVALID_WINDOW_HANDLE
#define ERROR_INVALID_WINDOW_HANDLE 1400
#endif
#ifndef ERROR_ALREADY_INITIALIZED
#define ERROR_ALREADY_INITIALIZED 1247
#endif
#ifndef WDA_NONE
#define WDA_NONE 0
#define WDA_MONITOR 1
#endif
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x11
#endif
#ifndef IACE_CHILDREN
#define IACE_CHILDREN 1
#define IACE_DEFAULT 0x10
#define IACE_IGNORENOCONTEXT 0x20
#endif

#define IM_TYPE_INPUTCONTEXT 0x11  /* handle type of an input context */
#define IM_TYPE_MENU 2

/* Wine's win32u exports some of these as abort-on-call stubs; resolving them as missing makes the
 * tests skip instead of taking the whole run down. Real Windows implements all of them. */
static FARPROC im_nt(const char *name) {
    static const char *const wine_stubs[] = { "NtUserGetImeInfoEx", "NtUserSetImeInfoEx", "NtUserGetImeHotKey", "NtUserSetImeHotKey",
        "NtUserGetAppImeLevel", "NtUserSetAppImeLevel", "NtUserSetWindowDisplayAffinity", "NtUserUserHandleGrantAccess",
        "NtUserSetProcessRestrictionExemption", "NtUserUpdateLayeredWindow" };
    if (t_is_wine())
        for (unsigned i = 0; i < sizeof wine_stubs / sizeof wine_stubs[0]; i++)
            if (!strcmp(name, wine_stubs[i])) return NULL;
    return t_proc(name);
}
#undef UN
#define UN(name) ((name##_pfn)im_nt(#name))

UDECL(BOOL,  NtUserRegisterHotKey, (HWND, int, UINT, UINT));
UDECL(BOOL,  NtUserUnregisterHotKey, (HWND, int));
UDECL(HANDLE, NtUserCreateInputContext, (PVOID));
UDECL(BOOL,  NtUserDestroyInputContext, (HANDLE));
UDECL(DWORD, NtUserAssociateInputContext, (HWND, HANDLE, DWORD));
UDECL(ULONG_PTR, NtUserQueryInputContext, (HANDLE, DWORD));
UDECL(BOOL,  NtUserUpdateInputContext, (HANDLE, DWORD, PVOID));
UDECL(void,  NtUserBuildHimcList, (DWORD, DWORD, HANDLE *, DWORD *));
UDECL(BOOL,  NtUserGetImeInfoEx, (PVOID, DWORD));
UDECL(BOOL,  NtUserSetImeInfoEx, (PVOID));
UDECL(BOOL,  NtUserGetImeHotKey, (DWORD, UINT *, UINT *, HKL *));
UDECL(BOOL,  NtUserSetImeHotKey, (DWORD, UINT, UINT, HKL, DWORD));
UDECL(BOOL,  NtUserDisableThreadIme, (DWORD));
UDECL(BOOL,  NtUserSetWindowDisplayAffinity, (HWND, DWORD));
UDECL(BOOL,  NtUserGetWindowDisplayAffinity, (HWND, DWORD *));
UDECL(BOOL,  NtUserSetLayeredWindowAttributes, (HWND, COLORREF, BYTE, DWORD));
UDECL(BOOL,  NtUserGetLayeredWindowAttributes, (HWND, COLORREF *, BYTE *, DWORD *));
UDECL(BOOL,  NtUserSetShellWindowEx, (HWND, HWND));
UDECL(BOOL,  NtUserUserHandleGrantAccess, (HANDLE, HANDLE, BOOL));
UDECL(BYTE,  NtUserSetProcessRestrictionExemption, (BOOL));  /* kernel returns bool */
UDECL(HMENU, NtUserGetSystemMenu, (HWND, BOOL));
UDECL(BYTE,  NtUserNotifyIMEStatus, (HWND, DWORD, DWORD));  /* kernel returns bool: only AL is defined */
UDECL(DWORD, NtUserGetAppImeLevel, (HWND));
UDECL(BOOL,  NtUserSetAppImeLevel, (HWND, DWORD));
UDECL(ULONG_PTR, NtUserQueryWindow, (HWND, DWORD));
UDECL(ULONG_PTR, NtUserGetThreadState, (DWORD));

#define IM(name) UIN("imm32.dll", name)

/* A child that has not loaded user32 has a NULL PEB.KernelCallbackTable; on Windows 10 its first win32k syscall then ends the
 * process (0xC000041D) because the thread conversion calls back into user mode. Children whose first win32k call is a bare
 * Nt* syscall install a table of do-nothing handlers first. */
static LONG NTAPI im_cb_nothing(void *b, ULONG n) { (void)b; (void)n; return 0; }
static void im_install_stub_callbacks(void) {
    static void *tab[0x100];
    for (int i = 0; i < 0x100; i++) tab[i] = (void *)im_cb_nothing;
    *(void *volatile *)((char *)k_peb() + PEB_KernelCallbackTable) = tab;
}

/* ---- window helper ----------------------------------------------------------------------- */
static LRESULT CALLBACK im_wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    return U(DefWindowProcW)(h, m, w, l);
}
static ATOM im_class(void) {
    static ATOM a;
    static int tried;
    if (!tried) {
        tried = 1;
        WNDCLASSEXW wc;
        memset(&wc, 0, sizeof wc);
        wc.cbSize = sizeof wc;
        wc.lpfnWndProc = im_wndproc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.lpszClassName = L"win32kt_imhm";
        if (U(RegisterClassExW)) {
            a = U(RegisterClassExW)(&wc);
            /* this header is included by several translation units, each with its own copy of this state; the class is
             * process wide, so a later unit finds it registered by an earlier one */
            if (!a && GetLastError() == ERROR_CLASS_ALREADY_EXISTS) a = 1;
        }
    }
    return a;
}
/* Hidden, off-screen, non-activating window of this thread. */
static HWND im_mkwnd(DWORD style, DWORD ex, HWND parent) {
    if (!im_class() || !U(CreateWindowExW)) return NULL;
    return U(CreateWindowExW)(ex | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"win32kt_imhm", L"imhm",
                              style, -32000, -32000, 40, 40, parent, NULL, GetModuleHandleW(NULL), NULL);
}
static HWND im_mkpopup(void) { return im_mkwnd(WS_POPUP, 0, NULL); }
static void im_kill(HWND h) { if (h && U(DestroyWindow)) U(DestroyWindow)(h); }
static HWND im_bogus_hwnd(void) { return (HWND)(ULONG_PTR)0x0FFFFFF0; }

#define NEED_GUI(w) HWND w = im_mkpopup(); if (!(w)) { t_skip("cannot create a window (err %lu)", GetLastError()); return; }

/* Integrity level of another process, 0 when it cannot be read. */
static unsigned long im_proc_integrity(DWORD pid) {
    unsigned long level = 0;
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid), tok = NULL;
    if (!p) return 0;
    if (OpenProcessToken(p, TOKEN_QUERY, &tok)) {
        DWORD need = 0;
        GetTokenInformation(tok, TokenIntegrityLevel, NULL, 0, &need);
        TOKEN_MANDATORY_LABEL *lbl = need ? (TOKEN_MANDATORY_LABEL *)malloc(need) : NULL;
        if (lbl && GetTokenInformation(tok, TokenIntegrityLevel, lbl, need, &need))
            level = *GetSidSubAuthority(lbl->Label.Sid, *GetSidSubAuthorityCount(lbl->Label.Sid) - 1);
        free(lbl);
        CloseHandle(tok);
    }
    CloseHandle(p);
    return level;
}
/* Error a window of the shell process yields: a caller of lower integrity than the shell is stopped by UIPI first
 * (ERROR_ACCESS_DENIED) before any of the checks the test is after. Returns that error when it applies, else `normal`.
 * When the shell's level cannot be read, a Medium-or-lower caller is treated as blocked only if the observed error says so
 * (the caller then compares against both values through im_err_ok). */
static DWORD im_shell_err(DWORD shell_pid, DWORD normal) {
    unsigned long ours = k_integrity_level(), theirs = im_proc_integrity(shell_pid);
    if (theirs && ours < theirs) return ERROR_ACCESS_DENIED;
    return normal;
}

/* ---- IME-enabled gate --------------------------------------------------------------------- */
/* NtUserCreateInputContext/Destroy/Associate/Query/Update/DisableThreadIme/BuildHimcList/
 * GetImeInfoEx/SetImeInfoEx/Get+SetAppImeLevel/NotifyIMEStatus all begin with
 * - SERVERINFO.dwSRVIFlags bit 2
 * (SRVIF_IME). imm32 tests the same bit through its cached shared info. Returns 1/0, or -1 when the
 * shared info is unreadable. */
static int im_ime_on(void) {
    struct k_user_shared_info si;
    unsigned long f = 0;
    if (!U(GetSystemMetrics)) return -1;  /* also loads user32 so that gSharedInfo resolves */
    if (!k_shared_info(&si) || !si.psi) return -1;
    if (!k_try_read_u32(si.psi, &f)) return -1;
    return (f & 4) ? 1 : 0;
}
#define NEED_IME() do { int on_ = im_ime_on(); \
    if (on_ == 0) { t_skip("SRVIF_IME clear (no IME-capable input method on this system)"); return; } \
    if (on_ < 0 && !t_is_wine()) { t_skip("SERVERINFO not readable"); return; } } while (0)

/* ---- desktop-heap views ------------------------------------------------------------------- */
#define IM_W_HIMC 0xd0   /* tagWND client view: imm32!ImmGetContext reads *(HIMC*)(pwnd + 0xd0) */
#define IM_W_EXSTYLE 0x18 /* tagWND client view: user32 GetWindowData(-0x14) */
#define IM_IMC_HANDLE 0x00 /* imm32!ImmLockClientImc: the client IMC object starts with its HIMC */
#define IM_IMC_CLIENT 0x18 /* ... and +0x18 holds the pClientImc stored by UpdateInputContext(code 0) */

static bool im_obj_qword(HANDLE h, unsigned off, uint64_t *v) {
    void *p = k_wnd_ptr((HWND)h);
    if (!p) return false;
    return k_try_read((char *)p + off, v, 8);
}
static bool im_wnd_himc(HWND w, HANDLE *h) {
    uint64_t v;
    if (!im_obj_qword((HANDLE)w, IM_W_HIMC, &v)) return false;
    *h = (HANDLE)(ULONG_PTR)v;
    return true;
}
static bool im_entry(HANDLE h, struct k_user_entry *e) { return k_user_entry(h, e); }

/* ---- hotkey key allocator ---------------------------------------------------------------- */
/* Unique (mods, vk) pairs from VK 0x88..0x8F (unassigned virtual keys) so a test that fails halfway
 * can never collide with a later one. FindHotKey keys on (mods & 0xf, vk) only. */
#define HK_ALL (MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN)
struct hk { UINT mods, vk; };
static struct hk hk_next(void) {
    static int n;
    struct hk k;
    int i = n++ % 120;
    k.vk = 0x88 + (i % 8);
    k.mods = 1 + (i / 8);
    return k;
}
#define HK_DECL(k) struct hk k = hk_next()
#define RHK(h, id, m, v) UN(NtUserRegisterHotKey)((h), (id), (m), (v))
#define UHK(h, id)       UN(NtUserUnregisterHotKey)((h), (id))
#define NEED_HK() \
    do { if (!UN(NtUserRegisterHotKey) || !UN(NtUserUnregisterHotKey)) { t_skip("missing NtUserRegisterHotKey"); return; } \
         if (!U(GetDesktopWindow)) { t_skip("no user32"); return; } } while (0)

/* ---- helper thread owning a hidden window --------------------------------------------------- */
struct imt {
    HANDLE thread, ready, stop;
    HWND hwnd;
    DWORD tid;
    int mkwnd;
};
static DWORD WINAPI imt_main(void *arg) {
    struct imt *x = (struct imt *)arg;
    x->tid = GetCurrentThreadId();
    if (x->mkwnd) x->hwnd = im_mkpopup();
    SetEvent(x->ready);
    DWORD t0 = GetTickCount();
    while (GetTickCount() - t0 < 20000) {
        DWORD r = U(MsgWaitForMultipleObjects)(1, &x->stop, FALSE, 50, QS_ALLINPUT);
        MSG m;
        while (U(PeekMessageW)(&m, NULL, 0, 0, PM_REMOVE)) { U(TranslateMessage)(&m); U(DispatchMessageW)(&m); }
        if (r == WAIT_OBJECT_0) break;
    }
    if (x->hwnd) U(DestroyWindow)(x->hwnd);
    return 0;
}
static bool imt_start(struct imt *x, int mkwnd) {
    memset(x, 0, sizeof *x);
    x->mkwnd = mkwnd;
    x->ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    x->stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    x->thread = CreateThread(NULL, 0, imt_main, x, 0, NULL);
    if (!x->thread || WaitForSingleObject(x->ready, 5000) != WAIT_OBJECT_0) return false;
    return !mkwnd || x->hwnd;
}
static void imt_stop(struct imt *x) {
    if (x->stop) SetEvent(x->stop);
    if (x->thread) { WaitForSingleObject(x->thread, 5000); CloseHandle(x->thread); }
    if (x->ready) CloseHandle(x->ready);
    if (x->stop) CloseHandle(x->stop);
    memset(x, 0, sizeof *x);
}
/* Runs fn on a fresh thread (its own pti, own hotkeys, own IME state) and waits (bounded). */
static DWORD im_on_thread(DWORD (WINAPI *fn)(void *), void *arg) {
    HANDLE t = CreateThread(NULL, 0, fn, arg, 0, NULL);
    DWORD r = 0xFFFFFFFF;
    if (!t) return r;
    if (WaitForSingleObject(t, 20000) == WAIT_OBJECT_0) GetExitCodeThread(t, &r);
    CloseHandle(t);
    return r;
}
#endif
