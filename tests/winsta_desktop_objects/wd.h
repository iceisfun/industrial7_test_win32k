/* Shared helpers for the winsta_desktop_objects area.
 *
 * Kernel entry points covered (win32u stubs, service table win32k.sys -> win32kbase/win32kfull):
 *   NtUserCreateWindowStation / OpenWindowStation / CloseWindowStation
 *   NtUserGetProcessWindowStation / SetProcessWindowStation
 *   NtUserCreateDesktopEx / OpenDesktop / CloseDesktop / OpenInputDesktop
 *   NtUserGetThreadDesktop / SetThreadDesktop
 *   NtUserGetObjectInformation / SetObjectInformation / BuildNameList
 * Most are reached through the user32 wrappers (CommonCreateWindowStation, CommonCreateDesktop,
 * InternalEnumObjects) which build the OBJECT_ATTRIBUTES; the Get/Set/Close/BuildNameList stubs
 * take the same arguments as their documented API and are also called directly.
 */
#ifndef WD_H
#define WD_H
#include "t.h"
#include "k.h"
#include <winternl.h>

#define AREA_PREFIX "winsta_desktop_objects_"
#define WT(n) TEST(winsta_desktop_objects_##n)

/* failure counter for CHILD() bodies (children return it as their exit code) */
static int g_cf;
#define CK(c) do { int ok_ = !!(c); if (!ok_) g_cf++; t_check(ok_, __FILE__, __LINE__, #c, NULL); } while (0)
#define CKF(c, ...) do { int ok_ = !!(c); if (!ok_) g_cf++; t_check(ok_, __FILE__, __LINE__, #c, __VA_ARGS__); } while (0)

#ifndef DESKTOP_ALL_ACCESS_
#define DESKTOP_ALL_ACCESS_ (DESKTOP_READOBJECTS | DESKTOP_CREATEWINDOW | DESKTOP_CREATEMENU | DESKTOP_HOOKCONTROL | \
                             DESKTOP_JOURNALRECORD | DESKTOP_JOURNALPLAYBACK | DESKTOP_ENUMERATE | DESKTOP_WRITEOBJECTS | \
                             DESKTOP_SWITCHDESKTOP | 0x000F0000)
#endif
#define WSTA_FULL (WINSTA_ALL_ACCESS | 0x00020000 /* READ_CONTROL */)
#define DESK_FULL (DESKTOP_ALL_ACCESS_)
#define UOI_FLAGS_  1
#define UOI_NAME_   2
#define UOI_TYPE_   3
#define UOI_SID_    4
#define UOI_HEAP_   5
#define UOI_IO_     6

/* ---- direct win32u syscall stubs, signatures as in the user32 wrappers --------- */
typedef BOOL    (WINAPI *p_NtUserGetObjectInformation)(HANDLE, int, PVOID, DWORD, DWORD *);
typedef BOOL    (WINAPI *p_NtUserSetObjectInformation)(HANDLE, int, PVOID, DWORD);
typedef BOOL    (WINAPI *p_NtUserCloseDesktop)(HANDLE);
typedef BOOL    (WINAPI *p_NtUserCloseWindowStation)(HANDLE);
typedef HANDLE  (WINAPI *p_NtUserGetThreadDesktop)(DWORD);
typedef BOOL    (WINAPI *p_NtUserSetThreadDesktop)(HANDLE);
typedef HANDLE  (WINAPI *p_NtUserGetProcessWindowStation)(void);
typedef BOOL    (WINAPI *p_NtUserSetProcessWindowStation)(HANDLE);
typedef LONG    (WINAPI *p_NtUserBuildNameList)(HANDLE, DWORD, PVOID, DWORD *);
typedef LONG    (NTAPI  *p_NtQueryObject)(HANDLE, int, PVOID, ULONG, PULONG);

#define NTU(name) ((p_##name)t_proc_in("win32u.dll", #name))
#define NTQO()    ((p_NtQueryObject)t_proc_in("ntdll.dll", "NtQueryObject"))

/* Object creation failing is an expected skip only for a standard user who is refused (creating a window station needs
 * an administrator token); anywhere else the failure is reported. */
static void wd_skip_create_(const char *file, int line) {
    DWORD e = GetLastError();
    if (e == ERROR_ACCESS_DENIED && k_integrity_level() < SECURITY_MANDATORY_HIGH_RID) t_skip("object creation denied (error 5): needs a high-integrity administrator token");
    else t_check(0, file, line, "object creation", "error %lu", e);
}
#define wd_skip_create() wd_skip_create_(__FILE__, __LINE__)

/* ---- names --------------------------------------------------------------------------------- */
static void wd_name(WCHAR *out, const char *tag) {
    swprintf(out, 64, L"win32kt%lu_%hs", GetCurrentProcessId(), tag);
}

/* ---- creation ------------------------------------------------------------------------------ */
static HWINSTA wd_mk_winsta(const char *tag, DWORD access, BOOL inherit, WCHAR *nameout) {
    WCHAR n[64]; wd_name(n, tag);
    if (nameout) lstrcpyW(nameout, n);
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, inherit };
    return U(CreateWindowStationW)(n, 0, access, &sa);
}
static HDESK wd_mk_desk(const char *tag, DWORD access, BOOL inherit, WCHAR *nameout) {
    WCHAR n[64]; wd_name(n, tag);
    if (nameout) lstrcpyW(nameout, n);
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, inherit };
    return U(CreateDesktopW)(n, NULL, NULL, 0, access, &sa);
}

/* ---- queries ------------------------------------------------------------------------------- */
static BOOL wd_uoi_str(HANDLE h, int idx, WCHAR *out, DWORD cch) {
    DWORD need = 0; out[0] = 0;
    return U(GetUserObjectInformationW)(h, idx, out, cch * sizeof(WCHAR), &need);
}
static BOOL wd_type_name(HANDLE h, WCHAR *out, size_t cch) {
    p_NtQueryObject q = NTQO(); BYTE buf[512]; ULONG rl = 0;
    out[0] = 0;
    if (!q) return FALSE;
    LONG st = q(h, 2 /* ObjectTypeInformation */, buf, sizeof buf, &rl);
    if (st < 0) return FALSE;
    UNICODE_STRING *us = (UNICODE_STRING *)buf;   /* PUBLIC_OBJECT_TYPE_INFORMATION starts with TypeName */
    size_t n = us->Length / sizeof(WCHAR);
    if (n >= cch) n = cch - 1;
    memcpy(out, us->Buffer, n * sizeof(WCHAR)); out[n] = 0;
    return TRUE;
}
static BOOL wd_obj_name(HANDLE h, WCHAR *out, size_t cch) {
    p_NtQueryObject q = NTQO(); BYTE buf[1024]; ULONG rl = 0;
    out[0] = 0;
    if (!q) return FALSE;
    LONG st = q(h, 1 /* ObjectNameInformation */, buf, sizeof buf, &rl);
    if (st < 0) return FALSE;
    UNICODE_STRING *us = (UNICODE_STRING *)buf;
    size_t n = us->Length / sizeof(WCHAR);
    if (n >= cch) n = cch - 1;
    memcpy(out, us->Buffer, n * sizeof(WCHAR)); out[n] = 0;
    return TRUE;
}
/* PUBLIC_OBJECT_BASIC_INFORMATION: Attributes @0, GrantedAccess @4 */
static BOOL wd_granted(HANDLE h, ULONG *granted) {
    p_NtQueryObject q = NTQO(); ULONG b[14] = {0}; ULONG rl = 0;   /* ObjectBasicInformation wants exactly 0x38 bytes */
    if (!q) return FALSE;
    if (q(h, 0 /* ObjectBasicInformation */, b, sizeof b, &rl) < 0) return FALSE;
    *granted = b[1];
    return TRUE;
}
static BOOL wd_is_inherit(HANDLE h) {
    DWORD f = 0; return GetHandleInformation(h, &f) && (f & HANDLE_FLAG_INHERIT);
}
static BOOL wd_interactive(void) { return k_has_interactive_desktop(); }

/* ---- enumeration --------------------------------------------------------------------------- */
struct wd_enum { WCHAR want[64]; int found; int count; int stop_after; };
static BOOL CALLBACK wd_enum_cb(LPWSTR name, LPARAM lp) {
    struct wd_enum *e = (struct wd_enum *)lp;
    e->count++;
    if (!lstrcmpiW(name, e->want)) e->found++;
    if (e->stop_after && e->count >= e->stop_after) return FALSE;
    return TRUE;
}
static BOOL CALLBACK wd_enum_cb_a(LPSTR name, LPARAM lp) { (void)name; (*(int *)lp)++; return TRUE; }

/* ---- a worker GUI thread living on a given desktop -------------------------------------------- */
struct wd_worker {
    HDESK desk;               /* NULL = leave the thread on its inherited desktop */
    HANDLE ready, done;       /* ready: window created; done: parent finished, thread may exit */
    HWND hwnd; DWORD tid;
    BOOL set_ok; DWORD set_err; HANDLE got_desk;
    BOOL set_after_ok; DWORD set_after_err;  /* SetThreadDesktop(alt) attempted while owning a window */
    HDESK alt;                                /* alternative desktop for the busy test (NULL = skip) */
    int with_hook;            /* 1 = install a WH_CBT hook for this thread before the SetThreadDesktop attempt */
    WCHAR cls[64];
    HHOOK hook;
};
static LRESULT CALLBACK wd_cbt_proc(int c, WPARAM wp, LPARAM lp) { return U(CallNextHookEx)(NULL, c, wp, lp); }
static DWORD WINAPI wd_worker_main(void *p) {
    struct wd_worker *w = (struct wd_worker *)p;
    w->tid = GetCurrentThreadId();
    if (w->desk) { w->set_ok = U(SetThreadDesktop)(w->desk); w->set_err = GetLastError(); }
    w->got_desk = U(GetThreadDesktop)(w->tid);
    WNDCLASSW wc; memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = (WNDPROC)U(DefWindowProcW);
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = w->cls;
    U(RegisterClassW)(&wc);
    w->hwnd = U(CreateWindowExW)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, w->cls, L"wd", WS_POPUP,
                                 -32000, -32000, 8, 8, NULL, NULL, wc.hInstance, NULL);
    if (w->with_hook) w->hook = U(SetWindowsHookExW)(WH_CBT, wd_cbt_proc, NULL, w->tid);
    if (w->alt) {
        w->set_after_ok = U(SetThreadDesktop)(w->alt);
        w->set_after_err = GetLastError();
    }
    SetEvent(w->ready);
    DWORD t0 = GetTickCount();
    while (WaitForSingleObject(w->done, 20) == WAIT_TIMEOUT && GetTickCount() - t0 < 8000) {
        MSG m; while (U(PeekMessageW)(&m, NULL, 0, 0, PM_REMOVE)) { U(TranslateMessage)(&m); U(DispatchMessageW)(&m); }
    }
    if (w->hook) U(UnhookWindowsHookEx)(w->hook);
    if (w->hwnd) U(DestroyWindow)(w->hwnd);
    U(UnregisterClassW)(w->cls, wc.hInstance);
    return 0;
}
static HANDLE wd_worker_start(struct wd_worker *w, const char *tag) {
    w->ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    w->done = CreateEventW(NULL, TRUE, FALSE, NULL);
    swprintf(w->cls, 64, L"win32kt%lu_%hs", GetCurrentProcessId(), tag);
    HANDLE th = CreateThread(NULL, 0, wd_worker_main, w, 0, NULL);
    if (th && WaitForSingleObject(w->ready, 5000) != WAIT_OBJECT_0) { /* worker stuck: let caller see hwnd==NULL */ }
    return th;
}
static void wd_worker_stop(struct wd_worker *w, HANDLE th) {
    SetEvent(w->done);
    if (th) { WaitForSingleObject(th, 5000); CloseHandle(th); }
    CloseHandle(w->ready); CloseHandle(w->done);
}

/* A worker whose window could not be created: skip under an emulator without a window system, fail on Windows. */
#define WD_NEED_HWND(w, th) do { if (!(w).hwnd) { wd_worker_stop(&(w), th); \
    if (t_is_wine()) t_skip("worker window creation unsupported here"); else CHECK(0 && "worker window creation failed"); return; } } while (0)
struct wd_wnd_enum { HWND want; int found; int count; };
static BOOL CALLBACK wd_wnd_cb(HWND h, LPARAM lp) {
    struct wd_wnd_enum *e = (struct wd_wnd_enum *)lp;
    e->count++; if (h == e->want) e->found++;
    return TRUE;
}

/* ---- NtUserBuildNameList result parsing ----------------------------------------------------
 * Layout written by win32kfull!_BuildNameList: DWORD total_bytes; DWORD count; then count
 * NUL-terminated WCHAR strings, followed by an empty terminator string. */
static int wd_namelist_has(const BYTE *buf, size_t len, const WCHAR *name, int *count_out) {
    if (len < 12) return 0;
    DWORD count = ((const DWORD *)buf)[1];
    const WCHAR *p = (const WCHAR *)(buf + 8), *end = (const WCHAR *)(buf + len);
    int found = 0; DWORD i;
    for (i = 0; i < count && p < end; i++) {
        size_t l = 0; while (p + l < end && p[l]) l++;
        if (p + l >= end) break;
        if (!lstrcmpiW(p, name)) found++;
        p += l + 1;
    }
    if (count_out) *count_out = (int)i;
    return found;
}
#endif
