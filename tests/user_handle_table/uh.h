/* user_handle_table: shared declarations.
 *
 * The client-visible USER handle table (user32!gSharedInfo.aheList) is a read-only view of the
 * session shared section that the kernel handle allocator
 * maintains. Layout of one client entry (stride gSharedInfo.HeEntrySize == 0x20, set by
 * the kernel):
 *   +0x00 QWORD delta   offset of the desktop-heap/shared-heap part of the object (0 for pool objects)
 *   +0x08 QWORD owner   PsGetThreadId() for thread-owned types, PsGetProcessId() for process-owned, 0 else
 *   +0x10 QWORD f10     *pDesktopInfo->pvDesktopBase for desktop-heap types (user32 fast-path guard), else 0
 *   +0x18 BYTE  type    TYPE_*
 *   +0x19 BYTE  flags   bit0 destroyed, bit1 destroy pending (object still locked), 0x20 granted, 0x40 no-desktop
 *   +0x1A WORD  uniq    must equal HIWORD(handle) (0xFFFF in the handle is a wildcard)
 */
#ifndef UH_H
#define UH_H
#include "t.h"
#include "k.h"
#include <imm.h>
#include <tlhelp32.h>

#define UH_ESZ 0x20

typedef struct uh_entry {
    unsigned long long delta, owner, f10;
    unsigned char type, flags;
    unsigned short uniq;
    unsigned int pad;
} uh_entry_t;

/* TYPE_* values: index into win32kbase's per-type table (allocation callers). */
enum {
    UT_FREE = 0, UT_WINDOW = 1, UT_MENU = 2, UT_CURSOR = 3, UT_SETWINDOWPOS = 4, UT_HOOK = 5,
    UT_CLIPDATA = 6, UT_CALLPROC = 7, UT_ACCEL = 8, UT_DDE = 9, UT_DDECONV = 10, UT_DDEXACT = 11,
    UT_MONITOR = 12, UT_KBDLAYOUT = 13, UT_KBDFILE = 14, UT_WINEVENT = 15, UT_TIMER = 16,
    UT_INPUTCONTEXT = 17, UT_HIDDATA = 18, UT_DEVICEINFO = 19, UT_TOUCH = 20, UT_GESTURE = 21,
    UT_SPC = 22, UT_BASEWND = 23, UT_MAX = 23
};
enum { UH_OWN_NONE, UH_OWN_THREAD, UH_OWN_PROCESS };

struct uh_desc {
    const char *name;
    int type;
    int owner;             /* UH_OWN_* */
    int desk;              /* object has a desktop-heap part visible to the client */
    int exact;             /* creating exactly one handle changes the USER object count by exactly 1 */
    int forge_ok;
    int destroy_ok;        /* destroy() of a live handle must return TRUE */
    DWORD err;             /* LastError for a rejected handle of this type (0 = unchecked) */
    HANDLE (*create)(void);
    BOOL (*destroy)(HANDLE);
    BOOL (*valid)(HANDLE); /* user-mode validity probe (IsWindow/IsMenu) or NULL */
    void (*setup)(void);
    void (*teardown)(void);
};
extern const struct uh_desc *const uh_descs[];
extern const struct uh_desc uh_d_wtop, uh_d_wchild, uh_d_menu, uh_d_popup, uh_d_icon, uh_d_hdwp,
    uh_d_hook, uh_d_accel, uh_d_winevent, uh_d_imc;

/* ---- common.c ---- */
int  uh_ready(void);                     /* user32 loaded, GUI thread, table sane; else 0 */
unsigned long long uh_n(void);           /* SERVERINFO.cHandleEntries */
bool uh_read(unsigned idx, uh_entry_t *e);
bool uh_read_h(HANDLE h, uh_entry_t *e); /* entry at LOWORD(h) */
uh_entry_t *uh_snap(unsigned *n);        /* malloc'ed copy of the whole table (free()), NULL on failure */
unsigned short uh_next_uniq(unsigned short u); /* uniqueness successor after free */
HANDLE uh_forge(HANDLE h, unsigned hiword);
#define UH_IDX(h)  ((unsigned)((ULONG_PTR)(h) & 0xffff))
#define UH_UNIQ(h) ((unsigned)(((ULONG_PTR)(h) >> 16) & 0xffff))
int  uh_counted_type(int t);
unsigned uh_scan_owned(int type);        /* live entries of `type` (-1 = every counted type) owned by this process/its threads */
DWORD uh_gr(DWORD flag);                 /* GetGuiResources(self, flag) */
const WCHAR *uh_class(void);
HWND uh_make_top(void);
HWND uh_make_child(HWND parent);
void *uh_client_ptr(HANDLE h);           /* k_wnd_ptr() for desktop-heap handles, NULL if unavailable */
int  uh_ime_enabled(void);               /* SERVERINFO.dwSRVIFlags & 4 */
extern HWND uh_parent;                   /* parent created by uh_setup_parent() */
void uh_setup_parent(void);
void uh_teardown_parent(void);
#define UH_NEED() do { if (!uh_ready()) { t_skip("USER shared handle table not available"); return; } } while (0)

#define UH_SENTINEL 0xDEAD0001UL
#define UH_CALL_ERR(var, expr) do { SetLastError(UH_SENTINEL); (var) = (expr); } while (0)
#endif
