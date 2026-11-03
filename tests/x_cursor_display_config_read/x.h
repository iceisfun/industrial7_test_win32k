/* x_cursor_display_config_read: shared declarations.
 *
 * The thin cursor and display-configuration syscalls of win32k reached through win32u.dll: the per-queue current
 * cursor (NtUserGetCursor / NtUserSetCursor), the cursor size table, DPI cursor linking and cursor contents copying,
 * the connecting-and-configuring-displays (CCD) buffer size query and its two mutating entry points
 * (DisplayConfigSetDeviceInfo, SetDisplayConfig) in their rejection and validate-only forms, monitor enumeration,
 * monitor painting and display mapping, and the work-area / functionalize entry points whose service stubs are
 * only identified, never called.
 *
 * Safety: no test applies a display topology or a mode, moves a monitor work area or changes a system setting.
 * Cursor tests only set cursors of this thread (or of a helper thread's own queue) and restore the previous one.
 * Signatures come from the user32 callers and the kernel bodies; none is in an SDK header.
 */
#ifndef XC_H
#define XC_H
#include "t.h"
#include "k.h"
#include <setjmp.h>

UDECL(HANDLE, NtUserGetCursor, (void));
UDECL(HANDLE, NtUserSetCursor, (HANDLE));
UDECL(BOOL,   NtUserGetRequiredCursorSizes, (HCURSOR, int *));
UDECL(BOOL,   NtUserLinkDpiCursor, (HCURSOR, HCURSOR, UINT));
UDECL(BOOL,   NtUserSetCursorContents, (HCURSOR, HCURSOR));
UDECL(LONG,   NtUserGetDisplayConfigBufferSizes, (UINT32 flags, UINT32 *numPaths));
UDECL(LONG,   NtUserDisplayConfigSetDeviceInfo, (void *header));
UDECL(LONG,   NtUserSetDisplayConfig, (UINT numPaths, void *paths, UINT numModes, void *modes, UINT flags));
UDECL(BOOL,   NtUserSetDisplayMapping, (HANDLE, HMONITOR));
UDECL(BOOL,   NtUserPaintMonitor, (HMONITOR, HDC, RECT *));
UDECL(BOOL,   NtUserEnumDisplayMonitors, (HDC, LPCRECT, MONITORENUMPROC, LPARAM));
UDECL(BOOL,   SetCursorContents, (HCURSOR dst, HCURSOR src));

extern jmp_buf xc_jb;
extern volatile int xc_armed;
extern volatile unsigned long xc_exc;
void xc_run_guarded(void (*f)(void));
/* Exceptions out of a syscall are findings on Windows (the test ends); under an emulator they are a skip. */
#define XT(n) \
    static void body_##n(void); \
    static void test_##n(void) { xc_run_guarded(body_##n); } \
    static void __attribute__((constructor)) reg_##n(void) { t_register("x_cursor_display_config_read_" #n, test_##n); } \
    static void body_##n(void)
#define NEED(n) do { if (!UN(n)) { t_skip("missing export: " #n); return; } if (t_is_wine()) { t_skip("emulator: " #n " is not a win32k service"); return; } } while (0)

#define XE_INVALID_PARAMETER 87
#define XE_INVALID_HANDLE     6
#define XE_NOACCESS         998
#define XE_GEN_FAILURE       31
#define XE_INSUFFICIENT_BUFFER 122
#define XE_INVALID_CURSOR_HANDLE 1402
#define XSENT 0x4D2
#define XFILL 0xAA

#define ST_INVALID_PARAMETER ((LONG)0xC000000D)
#define ST_UNSUCCESSFUL      ((LONG)0xC0000001)

/* A call that must FAIL: result 0 and this last error. */
void xc_fail(long long ret, DWORD gle, DWORD want, const char *file, int line, const char *what);
#define XFAIL(ret_expr, want) do { SET_ERR(XSENT); long long r__ = (long long)(ULONG_PTR)(ret_expr); xc_fail(r__, GetLastError(), (want), __FILE__, __LINE__, #ret_expr); } while (0)

/* The calling thread's current cursor is saved on entry and put back on scope exit. */
struct xc_save { HANDLE prev; };
static inline struct xc_save xc_cur_save(void) { struct xc_save s; s.prev = UN(NtUserGetCursor)(); return s; }
static inline void xc_cur_restore(struct xc_save *s) { if (UN(NtUserSetCursor)) UN(NtUserSetCursor)(s->prev); }
#define XC_CUR_GUARD() struct xc_save xc_g_ __attribute__((cleanup(xc_cur_restore))) = xc_cur_save()

/* A monochrome cursor (32x32 unless given) with the given hotspot; NULL on failure. */
HCURSOR xc_cursor(int w, int h, int hx, int hy, int fill);
HCURSOR xc_stock(int idc);
HWND    xc_hidden_window(void);
/* The dimensions of the mask bitmap of a cursor (GetIconInfo); 0 when unreadable. */
int     xc_mask_dims(HCURSOR c, int *w, int *h, int *hx, int *hy);

/* Runs f(arg) on a new thread and waits for it. Returns 0 on timeout. */
int     xc_on_thread(DWORD (WINAPI *f)(LPVOID), void *arg);

/* Monitor enumeration through user32. */
#define XC_MAXMON 16
struct xc_enum { int n; int total; HMONITOR m[XC_MAXMON]; RECT rc[XC_MAXMON]; HDC dc[XC_MAXMON]; DWORD tid; int stop_after; int nested; LPARAM seen_lp; };
BOOL CALLBACK xc_enum_cb(HMONITOR m, HDC dc, LPRECT rc, LPARAM lp);
/* CCD is available (the path query succeeds); fills the active path count. */
int     xc_ccd(UINT32 *np_active);
#endif
