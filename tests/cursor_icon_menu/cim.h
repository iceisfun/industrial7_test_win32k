/* cursor_icon_menu: shared declarations.
 *
 * Kernel facts this area relies on (win32kfull/win32kbase 19041 analysis):
 *  - TYPE_MENU (2) and TYPE_CURSOR (3) objects live in the USER handle table (handle allocator; entry
 *    stride 0x20, +0x08 owner = pid for process-owned types, +0x18 type, +0x19 flags, +0x1a uniq).
 *    Menus keep their shared part in the desktop heap, so the client can map it (the same pointer
 *    user32!HMValidateHandle returns); cursors are pool objects and have no client view.
 *  - Client view of a menu (user32!GetMenuInfo / GetMenuItemID / GetMenuItemCount read these):
 *      +0x00 handle  +0x08 heap base word  +0x10 dwMenuData  +0x18 hbrBack  +0x20 rgItems (pointer
 *      in the same address space as +0x08)  +0x28 style/flags  +0x2c cItems  +0x30 contextHelpId
 *      +0x34 cyMax.   Item i is at (rgItems - [+8]) + menu + i*0x70:
 *      +0x00 fType  +0x04 fState  +0x08 wID  +0x10 submenu object pointer (0 if none).
 *  - InternalCreateMenu(popup): a popup menu stores 1 at +0x28, a menu bar stores 0.
 */
#ifndef CIM_H
#define CIM_H
#include "t.h"
#include "k.h"
#include <wingdi.h>

#define CIM_ESZ 0x20
enum { CT_WINDOW = 1, CT_MENU = 2, CT_CURSOR = 3 };

typedef struct cim_he {
    unsigned long long delta, owner, f10;
    unsigned char type, flags;
    unsigned short uniq;
} cim_he;

#define CIM_IDX(h)  ((unsigned)((ULONG_PTR)(h) & 0xffff))
#define CIM_UNIQ(h) ((unsigned)(((ULONG_PTR)(h) >> 16) & 0xffff))

/* Error codes the kernel/user32 produce (HMValidateHandle: 1 window, 2 menu, 3 cursor). */
#define E_INVALID_WINDOW_HANDLE 0x578
#define E_INVALID_MENU_HANDLE   0x579
#define E_INVALID_CURSOR_HANDLE 0x57a
#define E_INVALID_PARAMETER     87
#define E_INVALID_FLAGS         0x3ec
#define E_MENU_ITEM_NOT_FOUND   0x5b0
#define E_CHILD_WINDOW_MENU     0x59c
#define E_ACCESS_DENIED         5
#define CIM_SENTINEL            0xDEAD0003UL

/* ---- Nt* syscalls declared from their kernel-side use (see cim_common.c for the derivation) ---- */
UDECL(ULONG_PTR, NtUserCallNoParam, (DWORD));
UDECL(BOOL, NtUserDestroyMenu, (HMENU));
UDECL(BOOL, NtUserDeleteMenu, (HMENU, UINT, UINT));
UDECL(BOOL, NtUserRemoveMenu, (HMENU, UINT, UINT));
UDECL(DWORD, NtUserCheckMenuItem, (HMENU, UINT, UINT));
UDECL(DWORD, NtUserEnableMenuItem, (HMENU, UINT, UINT));
UDECL(BOOL, NtUserSetMenuDefaultItem, (HMENU, UINT, UINT));
UDECL(BOOL, NtUserHiliteMenuItem, (HWND, HMENU, UINT, UINT));
UDECL(BOOL, NtUserThunkedMenuInfo, (HMENU, const MENUINFO *));
UDECL(BOOL, NtUserThunkedMenuItemInfo, (HMENU, UINT, BOOL, BOOL, MENUITEMINFOW *, void *));
UDECL(BOOL, NtUserSetMenu, (HWND, HMENU, BOOL));
UDECL(BOOL, NtUserGetMenuBarInfo, (HWND, LONG, LONG, MENUBARINFO *));
UDECL(BOOL, NtUserDestroyCursor, (HANDLE, ULONG));
UDECL(HANDLE, NtUserCreateEmptyCursorObject, (int));
UDECL(BOOL, NtUserGetIconInfo, (HANDLE, ICONINFO *, UNICODE_STRING *, UNICODE_STRING *, DWORD *, int));
UDECL(BOOL, NtUserGetIconSize, (HANDLE, UINT, int *, int *));
UDECL(ULONG, NtUserSetCursorIconData, (HANDLE, UNICODE_STRING *, UNICODE_STRING *, void *));
UDECL(HANDLE, NtUserSetCursor, (HANDLE));
UDECL(HANDLE, NtUserGetCursor, (void));
UDECL(BOOL, NtUserGetCursorInfo, (CURSORINFO *));
UDECL(int, NtUserShowCursor, (BOOL));
UDECL(BOOL, NtUserSetMenuContextHelpId, (HMENU, DWORD));
UDECL(UINT, NtUserGetMenuIndex, (HMENU, HMENU));
UDECL(HMENU, NtUserGetSystemMenu, (HWND, BOOL));
UDECL(BOOL, NtUserClipCursor, (const RECT *));
UDECL(BOOL, NtUserGetClipCursor, (RECT *));

/* ---- cim_common.c ---- */
int  cim_ready(void);                         /* user32 loaded, thread is a GUI thread, handle table readable */
unsigned long long cim_nentries(void);
bool cim_he_read(HANDLE h, cim_he *e);
bool cim_entry_is(HANDLE h, int type);        /* live entry of this type with matching uniq */
unsigned cim_count_owned(int type);           /* live entries of `type` whose owner == this pid */
DWORD cim_gr(DWORD flag);                     /* GetGuiResources(self, flag) */
HANDLE cim_forge(HANDLE h, unsigned hiword);
unsigned short cim_next_uniq(unsigned short u);
const WCHAR *cim_class(void);
HWND cim_make_top(void);
HWND cim_make_child(HWND parent);

/* menu shared part */
struct cim_menu { unsigned long long self_word, menudata, hbr, rgitems; unsigned flags, citems, helpid, cymax; unsigned long long handle; };
void *cim_menu_ptr(HMENU h);
bool cim_menu_read(HMENU h, struct cim_menu *m);
struct cim_item { unsigned type, state, id; unsigned long long sub_ptr; HMENU sub; };
bool cim_item_read(HMENU h, unsigned idx, struct cim_item *it);

/* tiny icons: 16x16, monochrome via CreateIcon (AND plane then XOR plane) */
HICON cim_mk_icon(int w, int h, BYTE and_fill, BYTE xor_fill);
HICON cim_mk_cursor_ii(int w, int h, int hx, int hy);   /* CreateIconIndirect(fIcon=FALSE), caller DestroyIcon */
HICON cim_mk_icon_ii(int w, int h, int bpp);            /* CreateIconIndirect(fIcon=TRUE), colour bitmap of `bpp` */
bool cim_gdi_bitmap_ok(HBITMAP b);              /* GDI handle of type OBJ_BITMAP with a table cell owned by us */

int  cim_gui(void);                           /* user32 loaded and the thread converted to a GUI thread (no table needed) */
#define CIM_GUI() do { if (!cim_gui()) { t_skip("user32 not usable"); return; } } while (0)
#define CIM_NEED() do { if (!cim_ready()) { t_skip("USER shared handle table not available"); return; } } while (0)
#define CIM_CALL_ERR(var, expr) do { SetLastError(CIM_SENTINEL); (var) = (expr); } while (0)
#endif
