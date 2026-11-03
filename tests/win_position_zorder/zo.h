/* Shared fixture for the win_position_zorder area.
 *
 * Everything here is a thin layer over user32 + the win32u Nt* stubs. Windows are created hidden
 * or off-screen (x/y around -30000) with WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE so the interactive
 * desktop is never disturbed, and every SetWindowPos/ShowWindow call carries a no-activate flag.
 *
 * Desktop-heap reads: a window's client-side tagWND (the structure user32 itself reads in
 * GetWindow/GetWindowRect/IsIconic/IsZoomed/GetParent) is resolved with k_wnd_ptr() or, when the
 * heap mapping guard is stale, the NtUserCallOneParam(hwnd, 0x38) "GETWNDPTR" call that user32's
 * HMValidateHandle falls back to. Field offsets (as observed in user32 and the desktop heap):
 *   +0x00 HWND         head.h (GetWindow)
 *   +0x08 self         kernel-side address of this object; links below are kernel pointers and
 *                      are rebased with  client = pwnd + (link - self)  (GetWindowWorker)
 *   +0x18 ExStyle      (byte 0x18 bit 7 = WS_EX_TOOLWINDOW in GetWindowPlacement)
 *   +0x1c style        (byte 0x1f: bit0 WS_MAXIMIZE IsZoomed, bit4 WS_VISIBLE IsWindowVisible,
 *                       bit5 WS_MINIMIZE IsIconic, bits6/7 WS_CHILD/WS_POPUP GetParent)
 *   +0x2a fnid         (GetWindowRect: (fnid & 0x2fff) == 0x29d is the desktop window)
 *   +0x30 spwndParent  +0x38 spwndChild  +0x40 spwndOwner  +0x48 spwndNext  +0x50 spwndPrev
 *   +0x58 rcWindow     (GetWindowRect copies these four ints; screen coordinates)
 *   +0x68 rcClient     (GetClientRect subtracts left/top; screen coordinates)
 */
#ifndef ZO_H
#define ZO_H
#include "t.h"
#include "k.h"

#define ZO_WND_Hwnd    0x00
#define ZO_WND_Self    0x08
#define ZO_WND_ExStyle 0x18
#define ZO_WND_Style   0x1c
#define ZO_WND_Fnid    0x2a
#define ZO_WND_Parent  0x30
#define ZO_WND_Child   0x38
#define ZO_WND_Owner   0x40
#define ZO_WND_Next    0x48
#define ZO_WND_Prev    0x50
#define ZO_WND_RcWnd   0x58
#define ZO_WND_RcCli   0x68

#define ZO_OFFX (-30000)   /* all fixture windows live far off-screen */
#define ZO_OFFY (-30000)

#ifndef SWP_NOCLIENTSIZE
#define SWP_NOCLIENTSIZE 0x0800
#endif
#ifndef SWP_NOCLIENTMOVE
#define SWP_NOCLIENTMOVE 0x1000
#endif
#ifndef SWP_STATECHANGED
#define SWP_STATECHANGED 0x8000
#endif
#define ZO_SWP_QUIET (SWP_NOACTIVATE)
#define ZO_SWP_NOTHING (SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE)
#define ZO_SWP_ZONLY (SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE)

/* ---- win32u syscall declarations (signatures follow the user32 callers) ---- */
UDECL(BOOL,      NtUserSetWindowPos,            (HWND, HWND, int, int, int, int, UINT));
UDECL(BOOL,      NtUserMoveWindow,              (HWND, int, int, int, int, BOOL));
UDECL(BOOL,      NtUserShowWindow,              (HWND, int));
UDECL(BOOL,      NtUserShowWindowAsync,         (HWND, int));
UDECL(BOOL,      NtUserGetWindowPlacement,      (HWND, WINDOWPLACEMENT *));
UDECL(BOOL,      NtUserSetWindowPlacement,      (HWND, const WINDOWPLACEMENT *));
UDECL(HWND,      NtUserGetAncestor,             (HWND, UINT));
UDECL(LONG,      NtUserBuildHwndList,           (HANDLE, HWND, BOOL, BOOL, DWORD, UINT, HWND *, UINT *));
UDECL(HWND,      NtUserFindWindowEx,            (HWND, HWND, UNICODE_STRING *, UNICODE_STRING *, ULONG));
UDECL(HWND,      NtUserChildWindowFromPointEx,  (HWND, POINT, UINT));
UDECL(HWND,      NtUserWindowFromPoint,         (POINT));
UDECL(HWND,      NtUserRealChildWindowFromPoint,(HWND, POINT));
UDECL(HWND,      NtUserSetParent,               (HWND, HWND));
UDECL(HANDLE,    NtUserDeferWindowPosAndBand,   (HANDLE, HWND, HWND, int, int, int, int, UINT, UINT, UINT));
UDECL(BOOL,      NtUserEndDeferWindowPosEx,     (HANDLE, BOOL));
UDECL(ULONG_PTR, NtUserCallOneParam,            (ULONG_PTR, DWORD));
UDECL(ULONG_PTR, NtUserCallHwnd,                (HWND, DWORD));

#define STATUS_BUFFER_TOO_SMALL_ ((LONG)0xC0000023)
#define STATUS_INVALID_HANDLE_   ((LONG)0xC0000008)

/* ---- message log ------------------------------------------------------------------------- */
struct zo_msg {
    HWND h; UINT msg; WPARAM w; LPARAM l;
    WINDOWPOS wp;      /* valid for WM_WINDOWPOSCHANGING/CHANGED: the structure as received */
    int seq;
};
#define ZO_LOG_MAX 1024
extern struct zo_msg zo_log[ZO_LOG_MAX];
extern int zo_log_n;
extern int zo_log_on;

/* behaviour switches for the shared window procedure (all reset by zo_reset()) */
#define ZO_B_CLAMP_SIZE   0x01   /* WM_WINDOWPOSCHANGING: force cx/cy to zo_beh_cx/cy unless NOSIZE */
#define ZO_B_FORCE_NOMOVE 0x02   /* WM_WINDOWPOSCHANGING: OR in SWP_NOMOVE */
#define ZO_B_FORCE_NOZ    0x04   /* WM_WINDOWPOSCHANGING: OR in SWP_NOZORDER */
#define ZO_B_FORCE_NOSIZE 0x08   /* WM_WINDOWPOSCHANGING: OR in SWP_NOSIZE */
#define ZO_B_SHIFT_POS    0x10   /* WM_WINDOWPOSCHANGING: add (zo_beh_dx, zo_beh_dy) to x/y unless NOMOVE */
#define ZO_B_MINMAX_USER  0x20   /* WM_GETMINMAXINFO: apply zo_mm instead of the off-screen defaults */
#define ZO_B_HTTRANSPARENT 0x40  /* WM_NCHITTEST: return HTTRANSPARENT */
#define ZO_B_COUNT_ONLY   0x80   /* log nothing but still count (unused placeholder) */
extern unsigned zo_beh;
extern int zo_beh_cx, zo_beh_cy, zo_beh_dx, zo_beh_dy;
extern MINMAXINFO zo_mm;

void zo_log_clear(void);
int  zo_log_count(HWND h, UINT msg);               /* h == NULL: any window */
int  zo_log_find(HWND h, UINT msg, int from);       /* index of first matching entry at/after from, or -1 */
const struct zo_msg *zo_log_get(int i);

/* ---- environment -------------------------------------------------------------------------- */
int  zo_have_gui(void);                  /* user32 usable and a window can be created */
const char *zo_class(void);              /* the shared window class (registered on first use) */
const char *zo_class2(void);             /* a second class, so FindWindowEx can tell them apart */
ATOM zo_atom(void);                      /* atom of zo_class() */
HINSTANCE zo_hinst(void);

/* destroy every fixture window still alive on this thread; restore behaviour switches */
void zo_reset(void);
/* when non-zero (the default after zo_reset) every top-level fixture window is WS_EX_LAYERED with alpha 1/255, i.e. invisible
 * even if a maximize/minimize or a clamp ever lands it on a real monitor. Tests that need un-layered top-levels (SetParent
 * into a child window is refused for layered windows without a Windows 8 manifest) clear it right after NEED_GUI(). */
extern int zo_ghost;

/* ---- fixture windows ------------------------------------------------------------------- */
/* Top-level popup (visible iff show). Always TOOLWINDOW|NOACTIVATE. owner may be NULL. */
HWND zo_top_ex(const char *title, int x, int y, int w, int h, DWORD style, DWORD ex, HWND owner, int show, const char *cls);
HWND zo_top(const char *title, int x, int y, int w, int h, int show);
/* Child window of parent (visible iff WS_VISIBLE in style). Coordinates are parent-client. */
HWND zo_child(HWND parent, const char *title, int x, int y, int w, int h, DWORD style, DWORD ex);
/* make visible without activating / hide */
void zo_show(HWND h);
void zo_hide(HWND h);
/* Desired top->bottom ordering of the given top-level (or sibling) windows: issue HWND_TOP in reverse */
void zo_arrange(HWND *order, int n);
/* z-order of the listed windows among all their siblings, top first, via user32 GetWindow(). out[] is a permutation of in[]; returns n or -1 */
int  zo_rank(const HWND *in, int n, HWND *out);
/* same, via the desktop-heap sibling chain (spwndChild/spwndNext links); returns -1 when the heap is not readable */
int  zo_rank_heap(HWND parent, const HWND *in, int n, HWND *out);
int  zo_same(const HWND *a, const HWND *b, int n);
void zo_dump(const char *tag, const HWND *a, int n);

/* ---- desktop heap access ------------------------------------------------------------------- */
const unsigned char *zo_wnd(HWND h);                       /* client-side tagWND*, NULL if unavailable */
int   zo_heap_ok(void);                                    /* fixture window resolves & first qword == HWND */
int   zo_rd(const unsigned char *p, size_t off, void *out, size_t n);
unsigned long zo_style_heap(HWND h, int *ok);
unsigned long zo_exstyle_heap(HWND h, int *ok);
HWND  zo_link(HWND h, size_t off);                         /* HWND of the object linked at off (parent/child/owner/next/prev), NULL if none/unreadable */
int   zo_link_valid(HWND h, size_t off);                   /* 1 when the link field was readable (even if NULL) */
int   zo_rect_heap(HWND h, size_t off, RECT *r);

/* ---- GUI thread with its own message loop ----------------------------------------------- */
struct zo_thread;
#define ZO_TM_CALL (WM_APP + 41)   /* wParam = fn(lParam), runs on the thread, result in zo_thread->ret */
struct zo_thread { HANDLE th; DWORD tid; HANDLE ready; volatile LONG stop; ULONG_PTR ret; HWND hw[8]; int nhw; const char *cls; };
struct zo_thread *zo_thread_start(void);
/* run fn(arg) on that thread (it sits in GetMessage); returns fn's result, or (ULONG_PTR)-1 on timeout */
typedef ULONG_PTR (*zo_fn)(void *);
ULONG_PTR zo_thread_call(struct zo_thread *t, zo_fn fn, void *arg);
void zo_thread_stop(struct zo_thread *t);
HWND zo_thread_window(struct zo_thread *t, const char *title, int x, int y, int w, int h, int show);

/* ---- misc --------------------------------------------------------------------------------- */
int  zo_same_rect(const RECT *a, const RECT *b);
void zo_note_rect(const char *what, const RECT *r);
int  zo_is_child_of(HWND child, HWND parent);
HWND zo_desktop(void);
void zo_pump(unsigned ms);
/* index of h in list a[0..n), -1 when absent */
int  zo_idx(const HWND *a, int n, HWND h);
int  zo_at_bottom(HWND w);   /* nothing but the shell's permanently-bottom windows lies below w */
/* unique-per-process names */
void zo_unique_name(char *out, size_t cap, const char *stem);


/* per-test prologue: skip when windows cannot be created; start from a clean fixture */
#define NEED_GUI() do { if (!zo_have_gui()) { t_skip("cannot create windows (no display / user32)"); return; } zo_reset(); } while (0)
#define NEED_NT(n) do { if (!UN(n)) { t_skip("missing export: " #n); return; } } while (0)
static inline RECT zo_wr(HWND h) { RECT r = {0,0,0,0}; U(GetWindowRect)(h, &r); return r; }
static inline RECT zo_cr_screen(HWND h) {   /* client rect in screen coordinates via the public API */
    RECT c = {0,0,0,0}; POINT o = {0,0};
    U(GetClientRect)(h, &c); U(ClientToScreen)(h, &o);
    c.left += o.x; c.right += o.x; c.top += o.y; c.bottom += o.y; return c;
}
#define ZO_FLAGS_QUIET (SWP_NOACTIVATE)

#endif
