/* Shared declarations for the r2_winsta_activation_group_session area.
 *
 * Every NtUser* entry point used here is reached through its win32u.dll stub (UDECL/UN, first module in
 * T_MODULE_LIST). Signatures are taken from the win32kfull/win32kbase bodies (parameter use, probes) since
 * none of them is in an SDK header. Return values are declared ULONG_PTR and masked by the test, because
 * several kernel functions return bool/BOOL in AL/EAX only. */
#ifndef R2_H
#define R2_H
#include "t.h"
#include "k.h"
#include <winternl.h>

#define RT(n) TEST(r2_winsta_activation_group_session_##n)
#define RC(n) CHILD(r2wag_##n)
#define RCN(n) "r2wag_" #n

static int g_cf;   /* failure counter for CHILD bodies (the child returns it as its exit code) */
#define CK(c) do { int ok_ = !!(c); if (!ok_) g_cf++; t_check(ok_, __FILE__, __LINE__, #c, NULL); } while (0)
#define CKF(c, ...) do { int ok_ = !!(c); if (!ok_) g_cf++; t_check(ok_, __FILE__, __LINE__, #c, __VA_ARGS__); } while (0)

#define SENT 0x0BADF00Du                 /* last-error sentinel: a path that must not touch last error leaves it */
#define R_B(r) ((BYTE)(ULONG_PTR)(r))    /* bool returned in AL */
#define R_D(r) ((DWORD)(ULONG_PTR)(r))   /* BOOL/UINT/NTSTATUS returned in EAX */

#define ERR_INVALID_WINDOW 1400u         /* ValidateHwnd failure */
#define ERR_INVALID_HANDLE 6u            /* STATUS_INVALID_HANDLE */
#define ERR_ACCESS_DENIED 5u
#define ERR_INVALID_PARAM 87u
#define ERR_NOT_FOUND 1168u              /* STATUS_NOT_FOUND (0xC0000225) */
#define ERR_INVALID_FLAGS 1004u          /* 0x3ec */

/* Under Wine, win32u exports every name but an unimplemented one is a stub that aborts the whole process
 * (prologue `sub rsp,28h; lea rcx,..`), while a syscall stub starts `mov r10,rcx; mov eax,imm`. Resolve such names to NULL. */
static FARPROC r2_wine_filter(FARPROC p) {
    unsigned char b[8];
    if (p && t_is_wine() && k_try_read((void *)p, b, sizeof b) && b[0] == 0x48 && b[1] == 0x83 && b[2] == 0xEC && b[3] == 0x28 && b[4] == 0x48 && b[5] == 0x8D)
        return NULL;
    return p;
}
static FARPROC r2_proc(const char *n) { return r2_wine_filter(t_proc(n)); }
static FARPROC r2_proc_in(const char *d, const char *n) { return r2_wine_filter(t_proc_in(d, n)); }
#undef UN
#define UN(name) ((name##_pfn)r2_proc(#name))

UDECL(ULONG_PTR, NtUserLockWindowStation, (HANDLE));
UDECL(ULONG_PTR, NtUserUnlockWindowStation, (HANDLE));
UDECL(ULONG_PTR, NtUserSetWindowStationUser, (HANDLE, void *luid, void *sid, DWORD len));
UDECL(ULONG_PTR, NtUserCheckProcessSession, (DWORD));
UDECL(ULONG_PTR, NtUserGetDesktopID, (DWORD, ULONGLONG *));
UDECL(LONG, NtUserCheckAccessForIntegrityLevel, (ULONG_PTR, ULONG_PTR, DWORD *));
UDECL(ULONG_PTR, NtUserDisableProcessWindowFiltering, (void));
UDECL(ULONG_PTR, NtUserSetProcessUIAccessZorder, (void));
UDECL(HANDLE, NtUserOpenThreadDesktop, (DWORD tid, DWORD flags, BOOL inherit, ACCESS_MASK access));
UDECL(ULONG_PTR, NtUserCheckWindowThreadDesktop, (HWND, DWORD));
UDECL(LONG, NtUserResolveDesktopForWOW, (UNICODE_STRING *));
UDECL(ULONG_PTR, NtUserSwitchDesktop, (HANDLE, DWORD fade, DWORD flags));
UDECL(ULONG_PTR, NtUserChangeWindowMessageFilterEx, (HWND, UINT, DWORD, void *));
UDECL(ULONG_PTR, NtUserGetWindowBand, (HWND, DWORD *));
UDECL(ULONG_PTR, NtUserSetWindowBand, (HWND, HWND, DWORD));
UDECL(ULONG_PTR, NtUserCreateActivationObject, (HANDLE, ULONGLONG *, ULONGLONG *, DWORD));
UDECL(ULONG_PTR, NtUserConfigureActivationObject, (ULONGLONG *luid, DWORD, DWORD, DWORD));
UDECL(ULONG_PTR, NtUserQueryActivationObject, (ULONGLONG *luid, DWORD out[4]));
UDECL(ULONG_PTR, NtUserDestroyActivationObject, (ULONGLONG *luid));
UDECL(ULONG_PTR, NtUserCreateActivationGroup, (ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR));
UDECL(ULONG_PTR, NtUserDestroyActivationGroup, (ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR));
UDECL(ULONG_PTR, NtUserSetActivationFilter, (HWND, DWORD));
UDECL(ULONG_PTR, NtUserCreateWindowGroup, (DWORD *));
UDECL(ULONG_PTR, NtUserDeleteWindowGroup, (DWORD));
UDECL(ULONG_PTR, NtUserEnableWindowGroupPolicy, (DWORD, DWORD, ULONG_PTR));
UDECL(ULONG_PTR, NtUserSetWindowGroup, (DWORD, HWND, DWORD, DWORD));
UDECL(ULONG_PTR, NtUserGetWindowGroupId, (HWND, DWORD *));
UDECL(ULONG_PTR, NtUserGetSharedWindowData, (ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR));
UDECL(ULONG_PTR, NtUserSetSharedWindowData, (ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR));
UDECL(ULONG_PTR, NtUserSetWindowShowState, (HWND, DWORD, int, RECT *));
UDECL(ULONG_PTR, NtUserSetWindowArrangement, (HWND, RECT *, RECT *, DWORD));
UDECL(HWND, NtUserGetTopLevelWindow, (HWND));
UDECL(ULONG_PTR, NtUserIsTopLevelWindow, (HWND));
UDECL(UINT, NtUserGetInternalWindowPos, (HWND, RECT *, POINT *));
UDECL(ULONG_PTR, NtUserSetInternalWindowPos, (HWND, DWORD, RECT *, POINT *));
UDECL(ULONG_PTR, NtUserGetWindowMinimizeRect, (HWND, RECT *));
UDECL(ULONG_PTR, NtUserLayoutCompleted, (HWND));
UDECL(ULONG_PTR, NtUserBeginLayoutUpdate, (HWND));
UDECL(ULONG_PTR, NtUserEnableResizeLayoutSynchronization, (HWND, DWORD));
UDECL(ULONG_PTR, NtUserIsResizeLayoutSynchronizationEnabled, (HWND));
UDECL(HWND, NtUserGhostWindowFromHungWindow, (HWND));
UDECL(HWND, NtUserHungWindowFromGhostWindow, (HWND));
UDECL(HWND, NtUserFrostCrashedWindow, (HWND, HWND));

typedef LONG (NTAPI *r2_NtQueryObject)(HANDLE, int, PVOID, ULONG, PULONG);
typedef LONG (NTAPI *r2_NtOpenDirectoryObject)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES);
typedef LONG (NTAPI *r2_NtQueryDirectoryObject)(HANDLE, PVOID, ULONG, BOOLEAN, BOOLEAN, PULONG, PULONG);
typedef LONG (NTAPI *r2_NtClose)(HANDLE);
#define R2_QO() ((r2_NtQueryObject)t_proc_in("ntdll.dll", "NtQueryObject"))

#ifndef DESKTOP_ALL_
#define DESKTOP_ALL_ (DESKTOP_READOBJECTS | DESKTOP_CREATEWINDOW | DESKTOP_CREATEMENU | DESKTOP_HOOKCONTROL | \
                      DESKTOP_JOURNALRECORD | DESKTOP_JOURNALPLAYBACK | DESKTOP_ENUMERATE | DESKTOP_WRITEOBJECTS | \
                      DESKTOP_SWITCHDESKTOP | 0x000F0000)
#endif
#define WSTA_ALL_ (WINSTA_ALL_ACCESS | 0x000F0000)

/* An unlikely-to-be-valid set of window handle values: NULL, a never-allocated index, junk in the high word. */
static const ULONG_PTR r2_bad_hwnds[] = { 0, 0x7777770, 0x12345670, 0xFFFFFFF0 };
#define N_BAD_HWNDS (sizeof r2_bad_hwnds / sizeof r2_bad_hwnds[0])
/* Handle values that are not open handles in this process. */
static const ULONG_PTR r2_bad_handles[] = { 0, 0x7777770, 0x12345670 };
#define N_BAD_HANDLES (sizeof r2_bad_handles / sizeof r2_bad_handles[0])

static void r2_name(WCHAR *out, const char *tag) {
    swprintf(out, 64, L"win32kt%lu_r2_%hs", GetCurrentProcessId(), tag);
}
static HWINSTA r2_mk_winsta(const char *tag, DWORD access, BOOL inherit, WCHAR *nameout) {
    WCHAR n[64]; r2_name(n, tag);
    if (nameout) lstrcpyW(nameout, n);
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, inherit };
    return U(CreateWindowStationW)(n, 0, access, &sa);
}
/* CreateDesktop fails (ERROR_NOT_ENOUGH_MEMORY) unless the requested access, after generic mapping, includes
 * DESKTOP_CREATEWINDOW: the desktop's own window is created through the handle that CreateDesktop returns. A handle with
 * a smaller mask is therefore made by creating the desktop with full access and opening it again by name with the
 * wanted mask; the creating handle is closed afterwards, the new handle keeps the desktop alive. */
static HDESK r2_mk_desk(const char *tag, DWORD access, BOOL inherit, WCHAR *nameout) {
    WCHAR n[64]; r2_name(n, tag);
    if (nameout) lstrcpyW(nameout, n);
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, inherit };
    if (access & (DESKTOP_CREATEWINDOW | GENERIC_WRITE | GENERIC_ALL | MAXIMUM_ALLOWED))
        return U(CreateDesktopW)(n, NULL, NULL, 0, access, &sa);
    HDESK full = U(CreateDesktopW)(n, NULL, NULL, 0, DESKTOP_ALL_, &sa);
    if (!full) return NULL;
    HDESK d = U(OpenDesktopW)(n, 0, inherit, access);
    DWORD e = GetLastError();
    U(CloseDesktop)(full);
    SetLastError(e);
    return d;
}
/* PUBLIC_OBJECT_BASIC_INFORMATION (0x38 bytes): Attributes @0, GrantedAccess @4, HandleCount @8, PointerCount @0xC.
 * NtQueryObject(ObjectBasicInformation) rejects any other length with STATUS_INFO_LENGTH_MISMATCH. */
static BOOL r2_basic(HANDLE h, ULONG out[4]) {
    r2_NtQueryObject q = R2_QO(); ULONG b[14] = {0}, rl = 0;
    if (!q || q(h, 0, b, 0x38, &rl) < 0) return FALSE;
    memcpy(out, b, 4 * sizeof(ULONG));
    return TRUE;
}
static BOOL r2_granted(HANDLE h, ULONG *g) { ULONG b[4]; if (!r2_basic(h, b)) return FALSE; *g = b[1]; return TRUE; }
static BOOL r2_str_info(HANDLE h, int cls, WCHAR *out, size_t cch) {
    r2_NtQueryObject q = R2_QO(); BYTE buf[1024]; ULONG rl = 0;
    out[0] = 0;
    if (!q || q(h, cls, buf, sizeof buf, &rl) < 0) return FALSE;
    UNICODE_STRING *us = (UNICODE_STRING *)buf;
    size_t n = us->Length / sizeof(WCHAR);
    if (n >= cch) n = cch - 1;
    memcpy(out, us->Buffer, n * sizeof(WCHAR)); out[n] = 0;
    return TRUE;
}
#define r2_type_name(h, o, c) r2_str_info(h, 2, o, c)
#define r2_obj_name(h, o, c)  r2_str_info(h, 1, o, c)
static BOOL r2_uoi_name(HANDLE h, WCHAR *out, DWORD cch) {
    DWORD need = 0; out[0] = 0;
    return U(GetUserObjectInformationW)(h, 2 /* UOI_NAME */, out, cch * sizeof(WCHAR), &need);
}
static HWND r2_win(void) { return k_make_window("r2wag"); }
#define NEED_WIN(h) HWND h = r2_win(); if (!h) { t_skip("window creation failed (no GUI)"); return; }
#define END_WIN(h) do { if (h) U(DestroyWindow)(h); } while (0)
#endif
