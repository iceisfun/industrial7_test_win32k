/* scroll_info_structs - shared fixtures.
 *
 * Kernel entry points covered (win32k service numbers in tools/win32u_syscalls.tsv):
 *   NtUserSetScrollInfo(hwnd, bar, const SCROLLINFO*, redraw)   sets scroll parameters
 *   NtUserSBGetParms(hwnd, bar, tagSBDATA*, SCROLLINFO*)        user32!RealGetScrollInfo (GetScrollInfo)
 *   NtUserShowScrollBar(hwnd, bar, show)                        ShowScrollBar
 *   NtUserEnableScrollBar(hwnd, bar, arrows)                    EnableScrollBar
 *   NtUserGetScrollBarInfo(hwnd, idObject, SCROLLBARINFO*)      GetScrollBarInfo
 *   NtUserGetTitleBarInfo(hwnd, TITLEBARINFO*)                  GetTitleBarInfo
 *   NtUserGetComboBoxInfo(hwnd, COMBOBOXINFO*)                  GetComboBoxInfo
 *   NtUserGetMenuBarInfo(hwnd, idObject, idItem, MENUBARINFO*)  GetMenuBarInfo
 *   NtUserGetCursorInfo(CURSORINFO*)
 * GetWindowInfo, GetScrollPos/Range, GetLastInputInfo and AdjustWindowRectEx never enter the kernel:
 * they read the read-only desktop-heap tagWND / tagSBINFO / SERVERINFO that the kernel maintains.
 *
 * Client-side tagSBINFO (user32!GetScrollPos / GetScrollRange): pSBInfo is tagWND+0x90 (a kernel
 * address); the client address is wnd + (pSBInfo - tagWND.pSelf(+0x08)).  Layout (GetScrollPos,
 * GetScrollRange):  +0 WSBflags, +4 Horz tagSBDATA {posMin,posMax,page,pos},
 * +0x14 Vert tagSBDATA.  WSBflags bits 0-1 = horizontal arrow disable flags, bits 2-3 = vertical.
 */
#ifndef SIB_H
#define SIB_H
#include "k.h"
#include <stdlib.h>
#include <wchar.h>

#define E_NO_SCROLLBARS     0x5A7   /* ERROR_NO_SCROLLBARS */
#define E_BAD_SB_RANGE      0x5A8   /* ERROR_INVALID_SCROLLBAR_RANGE */
#define E_INVALID_HWND      0x578   /* ERROR_INVALID_WINDOW_HANDLE */
#define E_INVALID_PARAM     0x57    /* ERROR_INVALID_PARAMETER */

#define SIB_W_PSELF   0x08
#define SIB_W_SBINFO  0x90
#define SIB_W_RCWIN   0x58
#define SIB_W_RCCLI   0x68

struct sb_data { int min, max, page, pos; };
struct sb_info { unsigned flags; struct sb_data h, v; };

/* Unique per-process class used for scroll-hosting windows. */
const wchar_t *sib_class(void);
/* Hidden (unless vis), off-screen, non-activating top-level window with `style`/`ex`. */
HWND sib_mk(DWORD style, DWORD ex, int w, int h, int vis);
HWND sib_child(HWND parent, const wchar_t *cls, DWORD style, int x, int y, int w, int h);
/* Reads the client-side SBINFO of hwnd; false when the view cannot be resolved (Wine, no SBINFO). */
int  sib_read_sbinfo(HWND hwnd, struct sb_info *out, int *present);
/* Direct win32u.dll syscall stub; NULL under Wine (its win32u has different/unimplemented NtUser entry points and aborts on some). */
FARPROC sib_stub(const char *name);
#define SIB_NEED_WND(h) do { if (!(h)) { t_skip("window creation failed"); return; } } while (0)

typedef int (WINAPI *SetScrollInfo_pfn)(HWND, int, const SCROLLINFO *, BOOL);
UDECL(int, NtUserSetScrollInfo, (HWND, int, const SCROLLINFO *, BOOL));
UDECL(BOOL, NtUserGetScrollBarInfo, (HWND, LONG, SCROLLBARINFO *));
UDECL(BOOL, NtUserGetTitleBarInfo, (HWND, TITLEBARINFO *));
UDECL(BOOL, NtUserGetComboBoxInfo, (HWND, COMBOBOXINFO *));
UDECL(BOOL, NtUserGetMenuBarInfo, (HWND, LONG, LONG, MENUBARINFO *));
UDECL(BOOL, NtUserGetCursorInfo, (CURSORINFO *));
UDECL(BOOL, NtUserShowScrollBar, (HWND, int, BOOL));
UDECL(BOOL, NtUserEnableScrollBar, (HWND, UINT, UINT));

/* Evaluates `call` exactly once (the harness EXPECT_EQ evaluates its arguments twice). */
#define EQ(call, want) do { long long r_ = (long long)(call); \
    t_check(r_ == (long long)(want), __FILE__, __LINE__, #call " == " #want, "got %lld want %lld", r_, (long long)(want)); } while (0)
#endif
