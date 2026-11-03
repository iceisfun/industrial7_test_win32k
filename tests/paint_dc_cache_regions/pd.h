/* Shared fixture for the paint_dc_cache_regions area.
 *
 * Syscall signatures below are derived from the win32kfull/win32kbase analysis and the
 * user32 thunks (user32!InvalidateRect, BeginPaint, GetDCEx ... are bare jumps into the win32u
 * stub, so the win32u argument list is the documented Win32 one):
 *   NtUserBeginPaint(HWND, PAINTSTRUCT*)            -> HDC     (win32kfull!NtUserBeginPaint)
 *   NtUserEndPaint(HWND, const PAINTSTRUCT*)        -> BOOL    (win32kfull!NtUserEndPaint)
 *   NtUserGetDC(HWND) / NtUserGetWindowDC(HWND)     -> HDC     (win32kbase / win32kfull)
 *   NtUserGetDCEx(HWND, HRGN, DWORD)                -> HDC
 *   NtUserReleaseDC(HDC)                            -> int     (win32kbase!NtUserReleaseDC: ReleaseCacheDC(hdc,0) != 2)
 *   NtUserRedrawWindow(HWND, const RECT*, HRGN, UINT)
 *   NtUserInvalidateRect(HWND, const RECT*, BOOL) / NtUserInvalidateRgn(HWND, HRGN, BOOL)
 *   NtUserValidateRect(HWND, const RECT*)
 *   NtUserGetUpdateRect(HWND, RECT*, BOOL) / NtUserGetUpdateRgn(HWND, HRGN, BOOL)
 *   NtUserExcludeUpdateRgn(HDC, HWND)
 *   NtUserScrollDC(HDC, dx, dy, const RECT*, const RECT*, HRGN, RECT*)
 *   NtUserScrollWindowEx(HWND, dx, dy, const RECT*, const RECT*, HRGN, RECT*, UINT)
 *   NtUserSetWindowRgn(HWND, HRGN, BOOL) / NtUserSetWindowRgnEx(HWND, HRGN, UINT)
 *   NtUserGetWindowRgnEx(HWND, HRGN, UINT)
 *   NtUserPrintWindow(HWND, HDC, UINT)
 *   NtUserLockWindowUpdate(HWND) / NtUserWindowFromDC(HDC)
 *   NtUserCallHwndLock(HWND, code)  (0x73 = UpdateWindow, user32!UpdateWindow)
 *   NtUserCallHwndParamLock(HWND, param, code) (0x7c = ValidateRgn, user32!ValidateRgn)
 *
 * Client-side window structure (the one k_wnd_ptr() returns, i.e. what user32!GetUpdateRect and
 * GetWindowRgn read; the kernel reaches the same block through *(pwnd+0x28)):
 *   +0x58 rcWindow  +0x68 rcClient (screen coordinates)
 *   +0x88 hrgnUpdate: 0 = clean, 1 = whole client invalid, else an HRGN in screen coordinates
 *   +0xa8 hrgnClip: the window region (win32kfull!NtUserSetWindowRgn -> SelectWindowRgn; user32!GetWindowRgn)
 *
 * Fixture windows: private classes, WS_POPUP, WS_EX_TOOLWINDOW|NOACTIVATE|LAYERED|TOPMOST with
 * layered alpha 0 so they are fully transparent, shown with SW_SHOWNOACTIVATE in a small corner of
 * the primary monitor. Tests that depend on the visible region being the whole client area first
 * check pd_exposed() and t_skip when something else covers the window.
 */
#ifndef PD_H
#define PD_H
#include "t.h"
#include "k.h"
#include <wingdi.h>

#define PT(n) TEST(paint_dc_cache_regions_##n)

/* ---- syscall declarations ---- */
UDECL(HDC,  NtUserBeginPaint,       (HWND, PAINTSTRUCT *));
UDECL(BOOL, NtUserEndPaint,         (HWND, const PAINTSTRUCT *));
UDECL(HDC,  NtUserGetDC,            (HWND));
UDECL(HDC,  NtUserGetWindowDC,      (HWND));
UDECL(HDC,  NtUserGetDCEx,          (HWND, HRGN, DWORD));
UDECL(int,  NtUserReleaseDC,        (HDC));
UDECL(BOOL, NtUserRedrawWindow,     (HWND, const RECT *, HRGN, UINT));
UDECL(BOOL, NtUserInvalidateRect,   (HWND, const RECT *, BOOL));
UDECL(BOOL, NtUserInvalidateRgn,    (HWND, HRGN, BOOL));
UDECL(BOOL, NtUserValidateRect,     (HWND, const RECT *));
UDECL(BOOL, NtUserGetUpdateRect,    (HWND, RECT *, BOOL));
UDECL(int,  NtUserGetUpdateRgn,     (HWND, HRGN, BOOL));
UDECL(int,  NtUserExcludeUpdateRgn, (HDC, HWND));
UDECL(BOOL, NtUserScrollDC,         (HDC, int, int, const RECT *, const RECT *, HRGN, RECT *));
UDECL(int,  NtUserScrollWindowEx,   (HWND, int, int, const RECT *, const RECT *, HRGN, RECT *, UINT));
UDECL(BOOL, NtUserSetWindowRgn,     (HWND, HRGN, BOOL));
UDECL(ULONG_PTR, NtUserSetWindowRgnEx, (HWND, HRGN, UINT));
UDECL(int,  NtUserGetWindowRgnEx,   (HWND, HRGN, UINT));
UDECL(BOOL, NtUserPrintWindow,      (HWND, HDC, UINT));
UDECL(BOOL, NtUserLockWindowUpdate, (HWND));
UDECL(HWND, NtUserWindowFromDC,     (HDC));
UDECL(ULONG_PTR, NtUserCallHwndLock,      (HWND, DWORD));
UDECL(ULONG_PTR, NtUserCallHwndParamLock, (HWND, ULONG_PTR, DWORD));
UDECL(HWND, NtUserWindowFromPoint,  (POINT));
UDECL(ULONG_PTR, NtUserCallOneParam, (ULONG_PTR, DWORD));

#ifndef DCX_USESTYLE
#define DCX_USESTYLE 0x10000
#endif
#ifndef DCX_NORECOMPUTE
#define DCX_NORECOMPUTE 0x100000
#endif
#ifndef DCX_VALIDATE
#define DCX_VALIDATE 0x200000
#endif
#ifndef PW_CLIENTONLY
#define PW_CLIENTONLY 1
#endif
#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 2
#endif
#define PD_ERR_INVALID_WINDOW_HANDLE 1400  /* win32kbase!ValidateHwndEx(.., 1) */
#define PD_ERR_INVALID_FLAGS         1004  /* NtUserRedrawWindow / NtUserGetWindowRgnEx flag rejection */
#define PD_ERR_DC_NOT_FOUND          1425  /* win32kbase!ReleaseCacheDC: hdc not in the DCE list */
#define PD_ERR_SCREEN_ALREADY_LOCKED 1440  /* win32kfull!zzzLockWindowUpdate2: locked by another thread */

/* ---- window classes / procedure behaviour ---- */
enum pd_class { PDC_PLAIN, PDC_NOBG, PDC_OWN, PDC_CLASS, PDC_PARENT, PDC_REDRAW, PDC_COUNT };

#define PD_FILL    RGB(0x21, 0x84, 0xC3)   /* colour WM_ERASEBKGND paints with PB_ERASE_FILL and the PDC_PLAIN class brush */
#define PD_MARKER  0x00A5A5A5u             /* DIB marker pixel */

#define PB_PAINT_BP       0x01  /* WM_PAINT: BeginPaint/EndPaint (recorded) */
#define PB_PAINT_NONE     0x02  /* WM_PAINT: return 0 without validating */
#define PB_ERASE_FILL     0x04  /* WM_ERASEBKGND: fill with PD_FILL, return 1 */
#define PB_ERASE_ZERO     0x08  /* WM_ERASEBKGND: do nothing, return 0 */
#define PB_PAINT_VALIDATE 0x10  /* WM_PAINT: ValidateRect(NULL) only */
#define PB_NCPAINT_NONE   0x20  /* WM_NCPAINT: swallow it */
extern unsigned pd_beh;

struct pd_ev { HWND h; UINT msg; WPARAM w; LPARAM l; int seq; };
#define PD_LOG_MAX 256
extern struct pd_ev pd_log[PD_LOG_MAX];
extern int pd_log_n;
void pd_log_clear(void);
int  pd_count(HWND h, UINT msg);          /* h == NULL: any window */
int  pd_find(HWND h, UINT msg, int from); /* index of the first match at/after from, or -1 */

/* GDI cell */
struct pd_cell { unsigned long long q0; unsigned owner; unsigned short upper; unsigned char objt, flags; unsigned long long puser; };
int  pd_cell(void *h, struct pd_cell *c);

/* what the WM_PAINT handler (PB_PAINT_BP) observed */
struct pd_paint_obs {
    int      valid;
    PAINTSTRUCT ps;
    HDC      hdc_ret;
    BOOL     upd_before;      /* GetUpdateRect(h) just before BeginPaint */
    RECT     upd_rect_before;
    BOOL     upd_after;       /* GetUpdateRect(h) just after BeginPaint */
    int      cliptype;        /* GetClipBox(hdc) */
    RECT     clipbox;
    HWND     wfd;             /* WindowFromDC(hdc) */
    DWORD    objtype;         /* GetObjectType(hdc) */
    BOOL     end_ret;
    int      cell_ok;         /* GDI cell of the paint DC, read while the DC is held (between BeginPaint and EndPaint) */
    struct pd_cell cell;
};
extern struct pd_paint_obs pd_obs;
/* what the WM_ERASEBKGND handler observed (wParam is the paint DC) */
struct pd_erase_obs { int valid; HDC hdc; HWND wfd; DWORD objtype; int cliptype; RECT clip; };
extern struct pd_erase_obs pd_eobs;
extern WPARAM pd_ncpaint_wparam; extern int pd_ncpaint_seen;
extern int pd_erase_result;

/* ---- fixture ---- */
HINSTANCE pd_hinst(void);
const char *pd_cls(enum pd_class c);
/* 1 if user32 usable and a window can be created; otherwise 0 (callers t_skip). */
int  pd_have_gui(void);
/* destroy every fixture window, drop locks, reset behaviour switches and the log */
void pd_reset(void);
#define PD_BEGIN() do { if (!pd_have_gui()) { t_skip("no usable GUI (user32 / window creation)"); return; } pd_reset(); } while (0)
#define PD_END()   pd_reset()

#define PD_X 24
#define PD_Y 24
#define PD_CW 96
#define PD_CH 80
/* generic creator: parent NULL => top-level popup shown no-activate; child windows get WS_CHILD|WS_VISIBLE */
HWND pd_make(enum pd_class c, HWND parent, DWORD style, DWORD ex, int x, int y, int w, int h);
HWND pd_top(enum pd_class c);                       /* PD_CW x PD_CH client popup, settled (no pending paint) */
HWND pd_top_style(enum pd_class c, DWORD style);    /* same with extra style bits (e.g. WS_CAPTION) */
HWND pd_child(HWND parent, enum pd_class c, DWORD style, int x, int y, int w, int h); /* settled */
void pd_kill(HWND h);
/* remove every pending paint/erase/ncpaint state in h and its children, then drain the queue */
void pd_settle(HWND h);
/* bounded message pump: at most ~maxmsg dispatches and ms milliseconds (never loops on a window that refuses to validate) */
int  pd_pump(unsigned ms, int maxmsg);
/* 1 when GetDC(h)'s clip box equals the full client rect (nothing covers the window, it is on screen) */
int  pd_exposed(HWND h);
#define PD_NEED_EXPOSED(h) do { if (!pd_exposed(h)) { t_skip("window is not fully exposed on screen (covered, off-screen or headless)"); pd_reset(); return; } } while (0)

/* ---- small helpers ---- */
RECT pd_rc(int l, int t, int r, int b);
int  pd_eq(const RECT *a, const RECT *b);
int  pd_rgn_box(HRGN r, RECT *box);                 /* GetRgnBox() result */
HRGN pd_rgn(int l, int t, int r, int b);
HRGN pd_rgn_screen(HWND h, int l, int t, int r, int b);   /* rect given in client coordinates of h, region in screen coordinates */
void pd_rgn_free(HRGN r);
const unsigned char *pd_wnd(HWND h);                /* client-side window block or NULL */
int  pd_wnd_q(HWND h, size_t off, ULONG_PTR *out);  /* read a pointer-size field */
#define PD_WND_RC_WINDOW 0x58
#define PD_WND_RC_CLIENT 0x68
#define PD_WND_HRGN_UPDATE 0x88
#define PD_WND_HRGN_CLIP   0xA8
/* hrgnUpdate: returns 1 if readable */
int  pd_hrgn_update(HWND h, ULONG_PTR *out);
int  pd_hrgn_clip(HWND h, ULONG_PTR *out);

#define PD_TYPE7(h) ((unsigned)(((ULONG_PTR)(h) >> 16) & 0x7f))
#define PD_KERNEL_FLOOR 0xFFFF800000000000ull

/* GUI state */
unsigned pd_qs_paint(void);       /* (GetQueueStatus(QS_PAINT) >> 16) & QS_PAINT */
int  pd_peek_paint(HWND h, MSG *m, int remove);

/* 32bpp top-down DIB selected into a memory DC */
struct pd_dib { HDC dc; HBITMAP bmp, old; unsigned *bits; int w, h; };
int  pd_dib_make(struct pd_dib *d, int w, int h);
void pd_dib_free(struct pd_dib *d);
void pd_dib_fill(struct pd_dib *d, unsigned v);
unsigned pd_dib_px(struct pd_dib *d, int x, int y);   /* after GdiFlush */

/* NtUserReleaseDC(hdc) on Windows (one argument, win32kbase!NtUserReleaseDC); Wine's win32u takes (hwnd, hdc), which this wrapper supplies there */
int pd_nt_release(HDC dc);
#endif
