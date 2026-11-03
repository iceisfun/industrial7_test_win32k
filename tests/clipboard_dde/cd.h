/* clipboard_dde: fixture shared by the clipboard tests.
 *
 * Kernel side under test (Windows 10 19041, win32kfull.sys unless stated):
 *   NtUserOpenClipboard / CloseClipboard / EmptyClipboard / SetClipboardData / GetClipboardData
 *   NtUserGetClipboardFormatName / CountClipboardFormats / IsClipboardFormatAvailable
 *   NtUserGetPriorityClipboardFormat / GetClipboardSequenceNumber / GetUpdatedClipboardFormats
 *   NtUserAdd/RemoveClipboardFormatListener / SetClipboardViewer / ChangeClipboardChain
 *   NtUserGetClipboardOwner / GetClipboardViewer / GetOpenClipboardWindow
 *   NtUserConvertMemHandle / NtUserCreateLocalMemHandle (TYPE_CLIPDATA objects)
 *   NtUserCallOneParam(.., 0x31) = EnumClipboardFormats
 *
 * tagWINDOWSTATION clipboard fields as read from the analysis (offsets are comments only; the tests
 * never touch kernel memory):
 *   +0x40 flags (0x40 = changed, 0x80 = rendering), +0x50 ptiClipLock, +0x60 spwndClipOpen,
 *   +0x68 spwndClipViewer, +0x70 spwndClipOwner, +0x80 pClipBase (tagCLIP[0x20 bytes each]),
 *   +0x88 cNumClipFormats, +0x8c iClipSerialNumber, +0x90 iClipSequenceNumber, +0x98 listener list.
 * tagCLIP: +0 format, +8 hData (0 = delayed, 1 = synthesized text, 2 = synthesized bitmap/dib,
 *   3/4 = synthesized metafile), +0x10 fGlobalHandle.
 */
#ifndef CD_H
#define CD_H
#include "t.h"
#include "k.h"

#define CD_WM_RENDERFORMAT      0x0305
#define CD_WM_RENDERALLFORMATS  0x0306
#define CD_WM_DESTROYCLIPBOARD  0x0307
#define CD_WM_DRAWCLIPBOARD     0x0308
#define CD_WM_ASKCBFORMATNAME   0x030C
#define CD_WM_CHANGECBCHAIN     0x030D
#define CD_WM_CLIPBOARDUPDATE   0x031D

#define CD_ERR_ACCESS_DENIED     5
#define CD_ERR_INVALID_PARAMETER 87
#define CD_ERR_INSUFFICIENT      122
#define CD_ERR_INVALID_WINDOW    1400
#define CD_ERR_CLIPBOARD_NOT_OPEN 1418

#define CD_NT_BUFFER_TOO_SMALL  ((LONG)0xC0000023)
#define CD_NT_INVALID_HANDLE    ((LONG)0xC0000008)

/* user32 passes {fGlobalHandle, fIncSerialNumber} to NtUserSetClipboardData (SetClipboardData
 * stores local_res20[0]=fGlobal, local_res20[1]=1) and receives a 16-byte block from
 * NtUserGetClipboardData: {uFmtRet, fGlobalHandle, hLocale-or-hPalette}. */
typedef struct { DWORD fGlobalHandle; DWORD fIncSerial; } CD_SETCLIPBDATA;
typedef struct { DWORD uFmtRet; DWORD fGlobalHandle; HANDLE hExtra; } CD_GETCLIPBDATA;

UDECL(BOOL,      NtUserOpenClipboard, (HWND, DWORD *));
UDECL(BOOL,      NtUserCloseClipboard, (void));
UDECL(BOOL,      NtUserEmptyClipboard, (void));
UDECL(ULONG_PTR, NtUserSetClipboardData, (UINT, HANDLE, CD_SETCLIPBDATA *));
UDECL(HANDLE,    NtUserGetClipboardData, (UINT, CD_GETCLIPBDATA *));
UDECL(int,       NtUserGetClipboardFormatName, (UINT, LPWSTR, UINT));
UDECL(int,       NtUserCountClipboardFormats, (void));
UDECL(BOOL,      NtUserIsClipboardFormatAvailable, (UINT));
UDECL(int,       NtUserGetPriorityClipboardFormat, (UINT *, int));
UDECL(DWORD,     NtUserGetClipboardSequenceNumber, (void));
UDECL(BOOL,      NtUserAddClipboardFormatListener, (HWND));
UDECL(BOOL,      NtUserRemoveClipboardFormatListener, (HWND));
UDECL(BOOL,      NtUserGetUpdatedClipboardFormats, (UINT *, UINT, UINT *));
UDECL(HWND,      NtUserSetClipboardViewer, (HWND));
UDECL(BOOL,      NtUserChangeClipboardChain, (HWND, HWND));
UDECL(HWND,      NtUserGetClipboardOwner, (void));
UDECL(HWND,      NtUserGetClipboardViewer, (void));
UDECL(HWND,      NtUserGetOpenClipboardWindow, (void));
UDECL(HANDLE,    NtUserConvertMemHandle, (PVOID, DWORD));
UDECL(LONG,      NtUserCreateLocalMemHandle, (HANDLE, PVOID, DWORD, DWORD *));
UDECL(ULONG_PTR, NtUserCallOneParam, (ULONG_PTR, DWORD));
UDECL(BOOL,      NtUserCheckProcessForClipboardAccess, (DWORD, DWORD *, PVOID));

/* single-evaluation equality check (the template EXPECT_EQ evaluates its arguments twice, which
 * would repeat calls that have side effects) */
#define EQ(a, b) do { ULONG_PTR a_ = (ULONG_PTR)(a), b_ = (ULONG_PTR)(b); \
    t_check(a_ == b_, __FILE__, __LINE__, #a " == " #b, "got 0x%llx want 0x%llx", (unsigned long long)a_, (unsigned long long)b_); } while (0)

/* The direct-syscall tests only make sense against real win32k: Wine's win32u has other
 * signatures for these entry points. */
#define CD_NEED_WIN32K() do { if (t_is_wine()) { t_skip("direct win32k syscall layout is Windows-specific"); return; } } while (0)

/* ---- per-window behaviour / message log ------------------------------------------------ */
#define CD_MAXFMT 8
struct cd_ctx {
    HWND hwnd;
    int  id;
    int  listener;                 /* AddClipboardFormatListener succeeded and not yet removed */
    int  in_chain;                 /* SetClipboardViewer succeeded and not yet removed */
    HWND next_viewer;              /* value returned by SetClipboardViewer (what to forward to) */
    int  forward;                  /* forward WM_DRAWCLIPBOARD / WM_CHANGECBCHAIN down the chain */
    LRESULT chain_ret;             /* returned from WM_CHANGECBCHAIN */
    struct { UINT fmt; const void *data; size_t len; int refuse; } render[CD_MAXFMT];
    int  nrender;
    int  render_all;               /* WM_RENDERALLFORMATS: open the clipboard and render the table */
    unsigned cnt[32];              /* message counters, index = msg - 0x300 */
    WPARAM last_w[32];
    LPARAM last_l[32];
    int  seen_open_owner_ok;       /* captured inside WM_RENDERFORMAT: GetClipboardOwner()==hwnd */
    HWND seen_open_window;         /* captured inside WM_RENDERFORMAT: GetOpenClipboardWindow() */
    int  seen_fmt_avail;           /* captured inside WM_RENDERFORMAT: IsClipboardFormatAvailable(wParam) */
};
#define CD_CNT(c, m) ((c)->cnt[(m) - 0x300])

struct cd_msg { UINT msg; WPARAM w; LPARAM l; int ctx_id; };
#define CD_LOG_MAX 256
extern struct cd_msg cd_log[CD_LOG_MAX];
extern int cd_log_n;
void cd_log_reset(void);

struct cd_guard { int dummy; };
int  cd_begin(struct cd_guard *g);          /* 0 => test already skipped */
void cd_guard_end(struct cd_guard *g);
#define CD_SETUP() struct cd_guard cdg_ __attribute__((cleanup(cd_guard_end))) = {0}; if (!cd_begin(&cdg_)) return

int cd_have_window(void);                   /* can this session create windows at all */
struct cd_ctx *cd_new(void);                /* hidden off-screen popup with logging wndproc */
void cd_destroy(struct cd_ctx *c);          /* removes listener/viewer first, then DestroyWindow */

/* clipboard helpers (user32 path) */
BOOL  cd_open(HWND h);                      /* OpenClipboard with a short bounded retry */
BOOL  cd_open_empty(HWND h);                /* open + EmptyClipboard */
HGLOBAL cd_gmem(const void *p, size_t n);   /* GMEM_MOVEABLE copy */
HGLOBAL cd_gstr_w(const WCHAR *s);
HGLOBAL cd_gstr_a(const char *s);
BOOL  cd_put(HWND owner, UINT fmt, const void *p, size_t n);      /* open+empty+set+close  */
BOOL  cd_put_text(HWND owner, const WCHAR *s);                      /* CF_UNICODETEXT */
size_t cd_get_bytes(UINT fmt, void *out, size_t cap);                /* clipboard must be open */
int   cd_formats(UINT *out, int cap);                               /* enumerate; clipboard must be open */
int   cd_formats_closed(UINT *out, int cap, HWND opener);           /* open, enumerate, close */
int   cd_fmt_index(const UINT *f, int n, UINT fmt);                 /* -1 if absent */
UINT  cd_reg(const char *tag);                                      /* unique registered format */
int   cd_freed(HGLOBAL h);                                          /* handle no longer a live HGLOBAL */

/* thread helper: runs fn on a new thread, waits up to 5 s, returns its exit code (0xDEAD0001 on timeout) */
DWORD cd_thread(LPTHREAD_START_ROUTINE fn, void *arg);

/* TYPE_CLIPDATA object helpers */
HANDLE cd_convert(const void *p, DWORD n);                          /* NtUserConvertMemHandle */
void   cd_destroy_clipdata(HANDLE h);                               /* parks it in a private format and empties */
#define CD_TYPE_CLIPDATA 6   /* HMValidateHandle(h, 6) in NtUserCreateLocalMemHandle / UT_FreeCBFormat */

/* child plumbing (cd_children.c) */
extern const char *cd_child_payload;
#endif
