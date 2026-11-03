/* x_window_chrome_info_queries: shared declarations.
 *
 * The thin window-information syscalls of win32k reached through win32u.dll: scroll bar, title bar and combo box
 * info, caret position, internal window placement, minimize rectangle, top-level window, thread/desktop check,
 * desktop id, shared window data, window fill, control brush and colour, window style alteration, caption and
 * menu bar drawing. Every test works on hidden windows of this process (or of a helper thread), draws only into
 * memory device contexts and asserts relations (against GetWindowRect, GetClientRect, ClientToScreen, system
 * metrics and styles) instead of pixel or metric values. Signatures come from the user32 callers; none is in an
 * SDK header.
 */
#ifndef XW_H
#define XW_H
#include "t.h"
#include "k.h"
#include <setjmp.h>

UDECL(ULONG_PTR, NtUserGetScrollBarInfo, (HWND, LONG, void *));
UDECL(ULONG_PTR, NtUserGetTitleBarInfo, (HWND, void *));
UDECL(ULONG_PTR, NtUserGetComboBoxInfo, (HWND, void *));
UDECL(ULONG_PTR, NtUserGetCaretPos, (POINT *));
UDECL(ULONG_PTR, NtUserGetInternalWindowPos, (HWND, RECT *, POINT *));
UDECL(ULONG_PTR, NtUserGetWindowMinimizeRect, (HWND, RECT *));
UDECL(ULONG_PTR, NtUserGetTopLevelWindow, (HWND));
UDECL(ULONG_PTR, NtUserCheckWindowThreadDesktop, (HWND, DWORD));
UDECL(ULONG_PTR, NtUserGetDesktopID, (DWORD, ULONGLONG *));
UDECL(ULONG_PTR, NtUserGetSharedWindowData, (ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR));
UDECL(ULONG_PTR, NtUserFillWindow, (HWND hwndBrush, HWND hwndPaint, HDC, HBRUSH));
UDECL(ULONG_PTR, NtUserGetControlBrush, (HWND, HDC, UINT msg));
UDECL(ULONG_PTR, NtUserGetControlColor, (HWND hwndParent, HWND hwnd, HDC, UINT msg));
UDECL(ULONG_PTR, NtUserAlterWindowStyle, (HWND, ULONG_PTR mask, ULONG_PTR flags));
UDECL(ULONG_PTR, NtUserDrawCaptionTemp, (HWND, HDC, const RECT *, HFONT, HICON, UNICODE_STRING *, UINT));
UDECL(ULONG_PTR, NtUserDrawMenuBarTemp, (HWND, HDC, RECT *, HMENU, HFONT));
UDECL(ULONG_PTR, NtUserPaintMenuBar, (HWND, HDC, int left, int right, int top, DWORD flags));

/* Exceptions out of a syscall are findings on Windows (the test ends); under an emulator they are a skip. */
extern jmp_buf xw_jb;
extern volatile int xw_armed;
extern volatile unsigned long xw_exc;
void xw_run_guarded(void (*f)(void));
#define XT(n) \
    static void body_##n(void); \
    static void test_##n(void) { xw_run_guarded(body_##n); } \
    static void __attribute__((constructor)) reg_##n(void) { t_register("x_window_chrome_info_queries_" XPFX #n, test_##n); } \
    static void body_##n(void)
#define NEED(n) do { if (!UN(n)) { t_skip("missing export: " #n); return; } if (t_is_wine()) { t_skip("emulator: " #n " is not a win32k service"); return; } } while (0)

#define XE_INVALID_PARAMETER 87
#define XE_ACCESS_DENIED      5
#define XE_INVALID_HANDLE     6
#define XE_NOACCESS         998
#define XE_INVALID_HWND    1400
#define XP_BADPTR ((void *)1)
#define XP_KERNELPTR ((void *)0xFFFF800000000000ULL)
#define XSENT 0x4D2

/* Low 32 bits of a syscall result (BOOL/UINT services leave the upper half unspecified). */
#define R32(x) ((DWORD)(ULONG_PTR)(x))

/* A call that must FAIL: result 0 (low 32 bits) and this last error. */
void xw_fail(ULONG_PTR ret, DWORD gle, DWORD want, const char *file, int line, const char *what);
#define XFAIL(ret_expr, want) do { SET_ERR(XSENT); ULONG_PTR r__ = (ULONG_PTR)(ret_expr); xw_fail(r__, GetLastError(), (want), __FILE__, __LINE__, #ret_expr); } while (0)
/* A call that must succeed: non-zero low 32 bits. */
#define XOK(ret_expr) do { SET_ERR(XSENT); ULONG_PTR r__ = (ULONG_PTR)(ret_expr); t_check(R32(r__) != 0, __FILE__, __LINE__, #ret_expr, "call failed, last error %lu", GetLastError()); } while (0)

/* Hidden test windows: never shown, never activated. */
HWND xw_win(DWORD style, DWORD ex, const WCHAR *cls);              /* 200x150 at (10,10), no parent */
HWND xw_child(HWND parent, DWORD style, const WCHAR *cls);         /* 50x50 child */
HWND xw_dead(void);                                               /* a handle that was a window and is destroyed */
void xw_kill(HWND w);

/* The sentinel fill and its check: a buffer must come back exactly as filled. */
#define XFILL 0xCC
int xw_untouched(const void *p, size_t n);

/* A window of another thread (hidden, pumping until released). */
struct xw_other { HANDLE thread; HWND hwnd; HANDLE ready, stop; DWORD style, ex, tid; const WCHAR *cls; };
int  xw_other_start(struct xw_other *o, DWORD style, DWORD ex, const WCHAR *cls);
void xw_other_stop(struct xw_other *o);

/* A memory DC with a 64x64 bitmap selected. */
struct xw_mem { HDC dc; HBITMAP bm, old; };
int  xw_mem_open(struct xw_mem *m, int w, int h);
void xw_mem_close(struct xw_mem *m);
COLORREF xw_px(HDC dc, int x, int y);
void xw_clear(HDC dc, int w, int h, COLORREF c);

#define STATE_UNAVAIL 0x1
#define STATE_INVIS   0x8000
#define STATE_FOCUS   0x100000
#define STATE_PRESSED 0x8
#define STATE_OFFSCREEN 0x10000
#endif
