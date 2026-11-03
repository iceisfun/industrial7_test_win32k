/* Shared fixtures for the win_create_destroy area: NtUserCreateWindowEx / NtUserDestroyWindow
 * argument layout, a logging window procedure and USER shared-state accessors. */
#ifndef WCD_H
#define WCD_H
#include "t.h"
#include "k.h"

/* _LARGE_STRING as win32kfull!NtUserCreateWindowEx captures it: the kernel reads one qword header
 * (Length, then MaximumLength with the ANSI flag in bit 31 of the second dword) followed by the
 * buffer pointer, fails with an access violation when MaximumLength < Length, and probes
 * Length+2 (unicode) or Length+1 (ansi) bytes at Buffer. */
typedef struct {
    ULONG Length;                /* bytes, without terminator */
    ULONG MaximumLength : 31;
    ULONG Ansi : 1;
    PVOID Buffer;
} WCD_LSTR;

/* 17 arguments: win32kfull!NtUserCreateWindowEx(exstyle, plstrWindowClass (becomes
 * CREATESTRUCT.lpszClass), plstrClassLookup (the name/atom actually looked up), plstrWindowName,
 * style, x, y, cx, cy, hwndParent, hMenu(or child id), hInstance, lpParam, zbid, flags|expWinVer,
 * dwFlags2, pActCtx). user32!VerNtUserCreateWindowEx pushes exactly this list. */
UDECL(HWND, NtUserCreateWindowEx, (DWORD ex, WCD_LSTR *cls, WCD_LSTR *clsver, WCD_LSTR *name,
      DWORD style, int x, int y, int cx, int cy, HWND parent, HMENU menu, HINSTANCE inst,
      LPVOID param, DWORD zbid, DWORD flagsver, DWORD flags2, PVOID actctx));
UDECL(DWORD, NtUserDestroyWindow, (HWND hwnd));

#define WCD_WINVER 0x0A00   /* expWinVer value user32 passes for a Win10-manifested module */
#define WCD_HWND_MESSAGE ((HWND)(LONG_PTR)-3)

/* ---- logging window procedure ------------------------------------------------------------ */
#define WCD_LOG_MAX 1024
struct wcd_msg {
    UINT msg; HWND hwnd; WPARAM wp; LPARAM lp; DWORD tid;
    int is_window;            /* IsWindow(hwnd) while the message was delivered */
    int has_cs; CREATESTRUCTW cs; WCHAR cs_name[96]; WCHAR cs_cls[96]; int cs_cls_is_atom;
    LONG style_now, exstyle_now;
    RECT rc;                  /* first RECT of NCCALCSIZE, or WINDOWPOS rect */
    int has_rc;
    MINMAXINFO mmi; int has_mmi;
    WINDOWPOS wpos; int has_wpos;
    int seq;
};
extern struct wcd_msg wcd_log[WCD_LOG_MAX];
extern volatile LONG wcd_nlog;
extern LRESULT wcd_ncreate_ret;       /* returned for WM_NCCREATE (default TRUE) */
extern LRESULT wcd_create_ret;        /* returned for WM_CREATE (default 0) */
extern void (*wcd_msg_cb)(HWND, UINT, WPARAM, LPARAM);  /* optional, called before logging return */
extern UINT wcd_log_skip_mask;        /* unused */
void wcd_reset(void);                 /* clear log, restore defaults, clear callback */
int  wcd_find(UINT msg, int from);    /* first log index >= from with that message, or -1 */
int  wcd_find_hwnd(UINT msg, HWND h, int from);
int  wcd_count(UINT msg);
LRESULT CALLBACK wcd_wndproc(HWND, UINT, WPARAM, LPARAM);

/* ---- classes ------------------------------------------------------------------------------ */
const WCHAR *wcd_cls(void);           /* the shared, lazily registered per-process class name */
ATOM wcd_cls_atom(void);
HINSTANCE wcd_hinst(void);
/* unique class names, registered on request */
ATOM wcd_register(const WCHAR *name, WNDPROC proc, UINT style, int cbCls, int cbWnd);
void wcd_unregister(const WCHAR *name);
void wcd_uniq_name(WCHAR *out, size_t cch, const char *tag);

/* ---- creation helpers --------------------------------------------------------------------- */
/* CreateWindowExW with the shared class and hidden, off-screen, non-activating defaults. */
HWND wcd_make(DWORD ex, DWORD style, HWND parent, int x, int y, int w, int h);
HWND wcd_make_top(void);              /* plain hidden popup */
/* direct syscall; cls is a wide string or an integer atom */
HWND wcd_nt_create(DWORD ex, const void *cls, const WCHAR *name, DWORD style, int x, int y,
                   int w, int h, HWND parent, HMENU menu, HINSTANCE inst, LPVOID param);
HWND wcd_nt_create_full(DWORD ex, const void *cls, const void *clsver, const WCHAR *name, DWORD style,
                   int x, int y, int w, int h, HWND parent, HMENU menu, HINSTANCE inst,
                   LPVOID param, DWORD zbid, DWORD flagsver, DWORD flags2, PVOID act);
void wcd_destroy(HWND h);             /* DestroyWindow if still a window; silent */
int  wcd_user_objects(void);          /* GetGuiResources(GR_USEROBJECTS) */

/* ---- shared state ------------------------------------------------------------------------- */
/* Pointer to the client mapping of the window's shared structure (what user32!ValidateHwnd
 * returns), or NULL when the fast path is not usable. Offsets below were observed on Windows 10 22H2:
 * +0x18 exstyle, +0x1c style, +0x20 hInstance, +0x2a
 * FNID, +0x58 rcWindow, +0x68 rcClient, +0x98 id/menu, +0xc8 cbWndExtra. */
/* A loaded module that is not the exe: a bogus hInstance (an unmapped value) crashes user32 on Windows 10. */
static inline HINSTANCE wcd_other_module(void) { return (HINSTANCE)GetModuleHandleW(L"kernel32.dll"); }
#define SW_STYLE   0x1c   /* Windows 10 22H2: the extended style sits first (+0x18), the style after it */
#define SW_EXSTYLE 0x18
#define SW_HINST   0x20
#define SW_FNID    0x2a
#define SW_RCWND   0x58
#define SW_RCCLI   0x68
#define SW_ID      0x98
#define SW_CBWND   0xc8
void *wcd_sw(HWND h);
/* handle table facts (k_user_entry): returns true if entry is a live window entry for h */
bool wcd_entry_live(HWND h);
bool wcd_dpi_plain(void);             /* true when screen is not DPI-virtualised for this process */
unsigned long long wcd_handle_count(void); /* SERVERINFO.cHandleEntries or 0 */
BOOL wcd_user(void);                  /* user32 services loaded (false -> skip) */

/* ---- helper GUI thread: optionally creates a hidden popup, then pumps messages until stopped ---- */
struct wcd_gt {
    HANDLE th; DWORD tid; HANDLE ev_ready, ev_stop;
    HWND top; int make_top;
    volatile LONG pumped;
    void (*extra)(struct wcd_gt *);   /* optional: runs on the thread after window creation */
    void *arg;
};
bool wcd_gt_start(struct wcd_gt *g, int make_top);
void wcd_gt_stop(struct wcd_gt *g);   /* signals stop, waits (bounded), closes handles */
#endif
