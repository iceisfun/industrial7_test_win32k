/* Shared fixture for the display_monitors_dpi area.
 *
 * Kernel-side facts this area relies on (all read from the 19041 analysis):
 *  - HMONITOR values are TYPE_MONITOR (12) objects of the USER shared handle table. user32!GetMonitorInfoW
 *    validates the handle itself (index < the shared handle-table entry count, entry+0x18 == 12, uniq == HIWORD or 0xffff) and reads the
 *    tagMONITOR from the shared heap: handle table entry +0 holds the *kernel* address of the object and
 *    gSharedInfo.ulSharedDelta (gSharedInfo+0x20) rebases it into this process.
 *  - gSharedInfo+0x18 is pDispInfo (client view of the DISPLAYINFO): +0 int "single monitor" flag, +8 primary
 *    tagMONITOR (shared-heap delta pointer), +0x10 first tagMONITOR. tagMONITOR: +0 HMONITOR, +0x10 next,
 *    +0x18 flags (bit0 = active), +0x1c rcMonitor, +0x2c rcWork, +0x40 monitor DPI (ushort).
 *  - The thread DPI context lives in the TEB (Win32ClientInfo at TEB+0x800): +0xe8 explicit context (0 = process
 *    default), +0xec "changed" flag, +0xf0 hosting behavior, +0xf4 hosting "changed" flag, +0x10c last returned token.
 *  - tagWND (desktop heap) +0x120 holds the window's DPI awareness context; (ctx>>8)&0x1ff is the DPI for
 *    unaware / system aware windows, 0 for per-monitor windows (their DPI is the ushort at +0x11e).
 */
#ifndef DM_H
#define DM_H
#include "t.h"
#include "k.h"
#include <setjmp.h>

/* Emulators abort the process when an unimplemented win32k syscall stub is reached (EXCEPTION_WINE_STUB). Every TEST in
 * this area runs through a guard that turns that into a SKIP; on Windows the guard never fires. */
static jmp_buf dm_jb;
static volatile int dm_armed;
static int dm_veh_installed;
static LONG CALLBACK dm_veh(PEXCEPTION_POINTERS ep) {
    if (dm_armed && ep->ExceptionRecord->ExceptionCode == 0x80000100) { dm_armed = 0; longjmp(dm_jb, 1); }
    return EXCEPTION_CONTINUE_SEARCH;
}
static inline void dm_run_guarded(void (*f)(void)) {
    if (!dm_veh_installed) { AddVectoredExceptionHandler(1, dm_veh); dm_veh_installed = 1; }
    if (!setjmp(dm_jb)) { dm_armed = 1; f(); dm_armed = 0; }
    else t_skip("emulator stub reached (unimplemented win32k function)");
}
#undef TEST
#define TEST(n) \
    static void body_##n(void); \
    static void test_##n(void) { dm_run_guarded(body_##n); } \
    static void __attribute__((constructor)) reg_##n(void) { t_register(#n, test_##n); } \
    static void body_##n(void)

/* ---- win32u / user32 internal syscalls (signatures derived from win32kbase/win32kfull analysis + user32 callers) ---- */
UDECL(BOOL,   NtUserEnumDisplayMonitors,       (HDC, LPCRECT, MONITORENUMPROC, LPARAM));
UDECL(BOOL,   NtUserGetHDevName,               (HMONITOR, WCHAR *));
UDECL(LONG,   NtUserEnumDisplayDevices,        (UNICODE_STRING *, DWORD, DISPLAY_DEVICEW *, DWORD));
UDECL(LONG,   NtUserEnumDisplaySettings,       (UNICODE_STRING *, DWORD, DEVMODEW *, DWORD));
UDECL(LONG,   NtUserChangeDisplaySettings,     (UNICODE_STRING *, DEVMODEW *, DWORD, PVOID));
UDECL(LONG,   NtUserGetDisplayConfigBufferSizes,(UINT32, UINT32 *));
UDECL(LONG,   NtUserDisplayConfigGetDeviceInfo,(void *));
UDECL(BOOL,   NtUserGetDpiForMonitor,          (HMONITOR, int, UINT *, UINT *));
UDECL(DWORD,  NtUserGetProcessDpiAwarenessContext,(HANDLE));
UDECL(DWORD,  NtUserGetSystemDpiForProcess,    (HANDLE));
UDECL(DWORD,  NtUserGetDpiForCurrentProcess,   (void));
UDECL(BOOL,   NtUserLogicalToPerMonitorDPIPhysicalPoint,(HWND, POINT *));
UDECL(BOOL,   NtUserPerMonitorDPIPhysicalToLogicalPoint,(HWND, POINT *));
UDECL(BOOL,   NtUserGetCurrentDpiInfoForWindow,(HWND, void *));
UDECL(int,    NtGdiGetDeviceCaps,              (HDC, int));
UDECL(ULONG_PTR, NtUserGetThreadState,         (DWORD));
UDECL(BOOL,   GetDpiForMonitorInternal,        (HMONITOR, UINT, UINT *, UINT *));
UDECL(LONG,   shcore_SetProcessDpiAwareness,   (int));
UDECL(LONG,   shcore_GetProcessDpiAwareness,   (HANDLE, int *));
UDECL(LONG,   shcore_GetDpiForMonitor,         (HMONITOR, int, UINT *, UINT *));
UDECL(BOOL,   SetProcessDpiAwarenessInternal,  (int));
UDECL(BOOL,   GetProcessDpiAwarenessInternal,  (HANDLE, int *));
UDECL(DPI_AWARENESS_CONTEXT, GetDpiAwarenessContextForProcess, (HANDLE));
#define DM_SHCORE(n) ((n##_pfn)t_proc_in("shcore.dll", #n + 7))

#define DM_NTSTATUS(x) ((LONG)(x))
#define STATUS_UNSUCCESSFUL_       ((LONG)0xC0000001)
#define STATUS_BUFFER_TOO_SMALL_   ((LONG)0xC0000023)
#define STATUS_INVALID_PARAM_1     ((LONG)0xC00000EF)
#define STATUS_INVALID_PARAM_2     ((LONG)0xC00000F0)

/* DPI awareness context raw values (user32!ValidateDpiAwarenessContextEx) */
#define DM_RAW_UNAWARE   0x6010u
#define DM_RAW_GDISCALED 0x40006010u
#define DM_RAW_PMONITOR  0x12u
#define DM_RAW_PMONITOR2 0x22u
#define DM_RAW_SYSTEM(dpi) ((((unsigned)(dpi) & 0x1ffu) << 8) | 0x11u)
#define DM_CTX(v) ((DPI_AWARENESS_CONTEXT)(ULONG_PTR)(v))
#define DM_LO(c)  ((unsigned)(ULONG_PTR)(c))

#ifndef MONITOR_DEFAULTTONULL
#define MONITOR_DEFAULTTONULL 0
#endif

/* ---- environment ------------------------------------------------------------------------------- */
static inline int dm_have_gui(void) {
    if (!U(GetSystemMetrics) || !U(GetMonitorInfoW) || !U(MonitorFromPoint)) return 0;
    return U(GetSystemMetrics)(SM_CMONITORS) > 0;
}
#define NEED_GUI() do { if (!dm_have_gui()) { t_skip("no display monitors / user32 unavailable"); return; } } while (0)
#define NEED_NT(n) do { if (!UN(n)) { t_skip("missing export: " #n); return; } } while (0)
static inline int dm_wine_no_child(void) {
    if (t_is_wine() && GetModuleHandleA("user32.dll")) { t_skip("Wine: child may stall once the parent connected to user32; Windows-only assertions"); return 1; }
    return 0;
}

/* ---- thread DPI context guard: restores the thread's explicit context (or "no override") on scope exit ---- */
struct dm_ctx_guard { DPI_AWARENESS_CONTEXT token; int active; };
static inline struct dm_ctx_guard dm_ctx_save(void) {
    struct dm_ctx_guard g = { NULL, 0 };
    if (U(SetThreadDpiAwarenessContext) && U(GetThreadDpiAwarenessContext)) {
        /* Set(Get()) returns the previous token; a token with bit 31 set clears the explicit override again. */
        g.token = U(SetThreadDpiAwarenessContext)(DM_CTX(DM_LO(U(GetThreadDpiAwarenessContext)())));
        g.active = g.token != NULL;
    }
    return g;
}
static inline void dm_ctx_restore(struct dm_ctx_guard *g) {
    if (g->active && U(SetThreadDpiAwarenessContext)) U(SetThreadDpiAwarenessContext)(DM_CTX(DM_LO(g->token)));
}
#define DM_CTX_GUARD() struct dm_ctx_guard dm_g_ __attribute__((cleanup(dm_ctx_restore))) = dm_ctx_save()

static inline unsigned dm_set_ctx(DPI_AWARENESS_CONTEXT c) { return DM_LO(U(SetThreadDpiAwarenessContext)(c)); }
static inline unsigned dm_cur_ctx(void) { return DM_LO(U(GetThreadDpiAwarenessContext)()); }
/* raw TEB client-info words */
static inline int dm_ci(unsigned off, unsigned long *out) { return k_try_read_u32((char *)k_teb() + TEB_Win32ClientInfo + off, out); }
#define DM_CI_CTX      0x0e8
#define DM_CI_CTXCHG   0x0ec
#define DM_CI_HOST     0x0f0
#define DM_CI_HOSTCHG  0x0f4
#define DM_CI_TOKEN    0x10c

/* user32!IsThreadDesktopComposited: *(DESKTOPINFO*)(TEB+0x820) +0x40 & 1. Returns -1 when the pointer chase fails. */
static inline int dm_composited(void) {
    void *pdi = NULL; unsigned long v = 0;
    if (!k_try_read_ptr((char *)k_teb() + TEB_Win32ClientInfo + 0x20, &pdi) || !pdi) return -1;
    if (!k_try_read_u32((char *)pdi + 0x40, &v)) return -1;
    return (int)(v & 1);
}

/* ---- monitors ---------------------------------------------------------------------------------- */
#define DM_MAXMON 16
struct dm_mon { HMONITOR h; RECT rc; HDC dc; };
struct dm_enum { struct dm_mon m[DM_MAXMON]; int n; int total; int stop_at; LPARAM want_lp; int lp_ok; int dc_obj_ok; int nested_ok; };
static BOOL CALLBACK dm_enum_cb(HMONITOR h, HDC dc, LPRECT rc, LPARAM lp) {
    struct dm_enum *e = (struct dm_enum *)lp;
    e->total++;
    if (e->n < DM_MAXMON) { e->m[e->n].h = h; e->m[e->n].dc = dc; if (rc) e->m[e->n].rc = *rc; else memset(&e->m[e->n].rc, 0, sizeof(RECT)); e->n++; }
    if (e->stop_at && e->total >= e->stop_at) return FALSE;
    return TRUE;
}
static inline int dm_enum_all(struct dm_enum *e, HDC dc, const RECT *clip) {
    memset(e, 0, sizeof *e);
    return U(EnumDisplayMonitors)(dc, clip, dm_enum_cb, (LPARAM)e);
}
static inline HMONITOR dm_primary(void) { POINT p = { 0, 0 }; return U(MonitorFromPoint)(p, MONITOR_DEFAULTTOPRIMARY); }
static inline int dm_info(HMONITOR h, MONITORINFOEXW *mi) {
    memset(mi, 0, sizeof *mi); mi->cbSize = sizeof *mi;
    return U(GetMonitorInfoW)(h, (MONITORINFO *)mi);
}
static inline int dm_rect_eq(const RECT *a, const RECT *b) { return a->left == b->left && a->top == b->top && a->right == b->right && a->bottom == b->bottom; }
static inline int dm_rect_in(const RECT *in, const RECT *out) { return in->left >= out->left && in->top >= out->top && in->right <= out->right && in->bottom <= out->bottom; }

/* ---- shared heap view of the monitor list ------------------------------------------------------ */
struct dm_hmon { ULONG_PTR kptr; unsigned char *cptr; HMONITOR h; unsigned long flags; RECT mon, work; unsigned short dpi, w42, w44; };
struct dm_heap { int ok; long single; int n; struct dm_hmon m[DM_MAXMON]; int primary; ULONG_PTR delta; };
static inline int dm_heap_read(struct dm_heap *o) {
    memset(o, 0, sizeof *o); o->primary = -1;
    void *si = k_shared_info_ptr();
    if (!si) return 0;
    void *pdisp = NULL; ULONG_PTR delta = 0;
    if (!k_try_read_ptr((char *)si + 0x18, &pdisp) || !pdisp) return 0;
    if (!k_try_read((char *)si + 0x20, &delta, sizeof delta)) return 0;
    unsigned long single = 0; ULONG_PTR kprim = 0, kfirst = 0;
    if (!k_try_read_u32(pdisp, &single)) return 0;
    if (!k_try_read((char *)pdisp + 8, &kprim, sizeof kprim)) return 0;
    if (!k_try_read((char *)pdisp + 0x10, &kfirst, sizeof kfirst)) return 0;
    o->single = (long)single; o->delta = delta;
    ULONG_PTR cur = kfirst;
    while (cur && o->n < DM_MAXMON) {
        unsigned char *c = (unsigned char *)(cur + delta);
        struct dm_hmon *m = &o->m[o->n];
        ULONG_PTR next = 0, hv = 0;
        if (!k_try_read(c, &hv, sizeof hv)) return 0;
        if (!k_try_read(c + 0x10, &next, sizeof next)) return 0;
        if (!k_try_read_u32(c + 0x18, &m->flags)) return 0;
        if (!k_try_read(c + 0x1c, &m->mon, sizeof(RECT))) return 0;
        if (!k_try_read(c + 0x2c, &m->work, sizeof(RECT))) return 0;
        if (!k_try_read_u16(c + 0x40, &m->dpi)) return 0;
        k_try_read_u16(c + 0x42, &m->w42); k_try_read_u16(c + 0x44, &m->w44);
        m->kptr = cur; m->cptr = c; m->h = (HMONITOR)hv;
        if (cur == kprim) o->primary = o->n;
        o->n++;
        cur = next;
    }
    o->ok = 1;
    return 1;
}
static inline int dm_heap_is_active(const struct dm_hmon *m) { return (m->flags & 1) != 0; }

/* ---- display device / mode helpers --------------------------------------------------------------- */
static inline void dm_devmode_init(DEVMODEW *dm) { memset(dm, 0, sizeof *dm); dm->dmSize = sizeof *dm; }
static inline int dm_cur_mode(DEVMODEW *dm) { dm_devmode_init(dm); return U(EnumDisplaySettingsW)(NULL, ENUM_CURRENT_SETTINGS, dm); }

/* thread entry helper */
static inline DWORD dm_wait_ms(HANDLE h, DWORD ms) { return WaitForSingleObject(h, ms); }
#endif
