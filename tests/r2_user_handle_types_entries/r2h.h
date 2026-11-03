/* user_handle_table: shared declarations.
 *
 * The client-visible USER handle table (user32!gSharedInfo.aheList) is a read-only view of the
 * session shared section that win32kbase object allocation / free / destroy
 * maintain. Layout of one client entry (stride gSharedInfo.HeEntrySize == 0x20, set by
 * HMInitHandleTable):
 *   +0x00 QWORD delta   offset of the desktop-heap/shared-heap part of the object (0 for pool objects)
 *   +0x08 QWORD owner   PsGetThreadId() for thread-owned types, PsGetProcessId() for process-owned, 0 else
 *   +0x10 QWORD f10     *pDesktopInfo->pvDesktopBase for desktop-heap types (user32 fast-path guard), else 0
 *   +0x18 BYTE  type    TYPE_*
 *   +0x19 BYTE  flags   bit0 destroyed, bit1 destroy pending (object still locked), 0x20 granted, 0x40 no-desktop
 *   +0x1A WORD  uniq    must equal HIWORD(handle) (0xFFFF in the handle is a wildcard)
 */
#ifndef R2H_H
#define R2H_H
#include "t.h"
#include "k.h"
#include <imm.h>
#include <tlhelp32.h>

#define R2H_ESZ 0x20

typedef struct r2h_entry {
    unsigned long long delta, owner, f10;
    unsigned char type, flags;
    unsigned short uniq;
    unsigned int pad;
} r2h_entry_t;

/* TYPE_* values: index into win32kbase's per-type table (object allocation callers). */
enum {
    UT_FREE = 0, UT_WINDOW = 1, UT_MENU = 2, UT_CURSOR = 3, UT_SETWINDOWPOS = 4, UT_HOOK = 5,
    UT_CLIPDATA = 6, UT_CALLPROC = 7, UT_ACCEL = 8, UT_DDE = 9, UT_DDECONV = 10, UT_DDEXACT = 11,
    UT_MONITOR = 12, UT_KBDLAYOUT = 13, UT_KBDFILE = 14, UT_WINEVENT = 15, UT_TIMER = 16,
    UT_INPUTCONTEXT = 17, UT_HIDDATA = 18, UT_DEVICEINFO = 19, UT_TOUCH = 20, UT_GESTURE = 21,
    UT_SPC = 22, UT_BASEWND = 23, UT_MAX = 23
};
enum { R2H_OWN_NONE, R2H_OWN_THREAD, R2H_OWN_PROCESS };

struct r2h_desc {
    const char *name;
    int type;
    int owner;             /* R2H_OWN_* */
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
extern const struct r2h_desc *const r2h_descs[];
extern const struct r2h_desc r2h_d_wtop, r2h_d_wchild, r2h_d_menu, r2h_d_popup, r2h_d_icon, r2h_d_hdwp,
    r2h_d_hook, r2h_d_accel, r2h_d_winevent, r2h_d_imc;

/* ---- common.c ---- */
int  r2h_ready(void);                     /* user32 loaded, GUI thread, table sane; else 0 */
unsigned long long r2h_n(void);           /* SERVERINFO.cHandleEntries */
bool r2h_read(unsigned idx, r2h_entry_t *e);
bool r2h_read_h(HANDLE h, r2h_entry_t *e); /* entry at LOWORD(h) */
r2h_entry_t *r2h_snap(unsigned *n);        /* malloc'ed copy of the whole table (free()), NULL on failure */
unsigned short r2h_next_uniq(unsigned short u); /* freed-object uniqueness successor */
HANDLE r2h_forge(HANDLE h, unsigned hiword);
#define R2H_IDX(h)  ((unsigned)((ULONG_PTR)(h) & 0xffff))
#define R2H_UNIQ(h) ((unsigned)(((ULONG_PTR)(h) >> 16) & 0xffff))
int  r2h_counted_type(int t);
unsigned r2h_scan_owned(int type);        /* live entries of `type` (-1 = every counted type) owned by this process/its threads */
DWORD r2h_gr(DWORD flag);                 /* GetGuiResources(self, flag) */
const WCHAR *r2h_class(void);
HWND r2h_make_top(void);
HWND r2h_make_child(HWND parent);
void *r2h_client_ptr(HANDLE h);           /* k_wnd_ptr() for desktop-heap handles, NULL if unavailable */
int  r2h_ime_enabled(void);               /* SERVERINFO.dwSRVIFlags & 4 */
extern HWND r2h_parent;                   /* parent created by r2h_setup_parent() */
void r2h_setup_parent(void);
void r2h_teardown_parent(void);
#define R2H_NEED() do { if (!r2h_ready()) { t_skip("USER shared handle table not available"); return; } } while (0)

#define R2H_SENTINEL 0xDEAD0001UL
#define R2H_CALL_ERR(var, expr) do { SetLastError(R2H_SENTINEL); (var) = (expr); } while (0)

/* ---- r2 additions ---- */
#define R2(n) TEST(r2_user_handle_types_entries_##n)
/* Type table of win32kbase, read from the
 * 19041.2965 image: ownership/heap flag word per TYPE_*. */
#define R2H_TF_THREAD   0x0001  /* pOwner = PsGetThreadId(owner thread) */
#define R2H_TF_PROCESS  0x0002  /* pOwner = PsGetProcessId(owner process) */
#define R2H_TF_DESKHEAP 0x0010  /* object lives in the desktop heap (delta/f10 set when a desktop exists) */
#define R2H_TF_SHARED   0x0040  /* object lives in the session shared heap (delta set, f10 clear) */
extern const unsigned short r2h_typeflags[24];
int r2h_is_thread_type(int t);   /* TF_THREAD set */
int r2h_is_process_type(int t);  /* TF_PROCESS set */
int r2h_is_unowned_type(int t);  /* neither: owner field stays 0 */

/* A GUI worker thread that owns one hidden top-level window (optionally on another desktop) and
 * pumps until told to quit. */
struct r2h_gw { HANDLE ready, quit, th; HWND w; DWORD tid; HDESK desk; int ok; };
int  r2h_gw_start(struct r2h_gw *g, HDESK desk);
void r2h_gw_stop(struct r2h_gw *g);
bool r2h_free_slot(unsigned *idx_out, unsigned *uniq_out, int want_even); /* a free (type 0) entry; 0 if none */
#endif
