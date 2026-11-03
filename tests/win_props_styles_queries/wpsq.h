/* Shared fixtures for the win_props_styles_queries area: window long/word/property syscalls and
 * the window query / text syscalls of win32kfull (NtUserSetWindowLong[Ptr], NtUserSetWindowWord,
 * NtUserSetProp/GetProp/RemoveProp/BuildPropList, NtUserQueryWindow, NtUserInternalGetWindowText,
 * NtUserDefSetText). Signatures below follow the user32
 * call sites (user32!_SetWindowLong, SetPropW, GetWindowThreadProcessId, InternalEnumProps...). */
#ifndef WPSQ_H
#define WPSQ_H
#include "t.h"
#include "k.h"
#include <limits.h>
#include <wchar.h>

#define WT(n)  TEST(win_props_styles_queries_##n)
#define WC(n)  CHILD(win_props_styles_queries_##n)
#define WCN(n) "win_props_styles_queries_" #n

#ifndef GWL_USERDATA
#define GWL_WNDPROC (-4)
#define GWL_HINSTANCE (-6)
#define GWL_HWNDPARENT (-8)
#define GWL_USERDATA (-21)
#endif
/* Single-evaluation equality (t.h's EXPECT_EQ evaluates its arguments twice, which breaks calls with side effects). */
#define WEQ(a, b) do { unsigned long long a_ = (unsigned long long)(ULONG_PTR)(a), b_ = (unsigned long long)(ULONG_PTR)(b); \
    t_check(a_ == b_, __FILE__, __LINE__, #a " == " #b, "got 0x%llx want 0x%llx", a_, b_); } while (0)
#define E_INVALID_HWND   1400  /* ERROR_INVALID_WINDOW_HANDLE: user32 ValidateHwnd / win32k ValidateHwnd failure */
#define E_INVALID_INDEX  1413  /* ERROR_INVALID_INDEX: 0x585, the default arm of the window-data accessors */
#define E_DLG_INDEX      1415  /* 0x587: dialog window extra index in the reserved range (0,0x1e) other than 0/0x10 */
#define E_ACCESS_DENIED  5
#define E_INVALID_PARAM  87    /* 0x57: RealInternalSetProp with atom 0 */
#define NTST_INVALID_HANDLE        ((LONG)0xC0000008)
#define NTST_BUFFER_TOO_SMALL      ((LONG)0xC0000023)

/* ---- syscalls resolved from win32u.dll (no SDK declaration) ------------------------------ */
UDECL(LONG,      NtUserSetWindowLong,    (HWND, int, LONG, BOOL));          /* (hwnd, index, value, bAnsi) */
UDECL(LONG_PTR,  NtUserSetWindowLongPtr, (HWND, int, LONG_PTR, BOOL));      /* (hwnd, index, value, bAnsi) */
UDECL(WORD,      NtUserSetWindowWord,    (HWND, int, WORD));
UDECL(LONG_PTR,  NtUserSetProp,          (HWND, ULONG, HANDLE));            /* ULONG: atom | 0x10000 when the atom is owned by the prop */
UDECL(HANDLE,    NtUserGetProp,          (HWND, USHORT));
UDECL(HANDLE,    NtUserRemoveProp,       (HWND, ULONG));
UDECL(LONG,      NtUserBuildPropList,    (HWND, ULONG, PVOID, ULONG *));    /* NTSTATUS */
UDECL(ULONG_PTR, NtUserQueryWindow,      (HWND, int));
UDECL(int,       NtUserInternalGetWindowText, (HWND, WCHAR *, int));
UDECL(int,       InternalGetWindowText,  (HWND, WCHAR *, int));
UDECL(BOOL,      NtUserDefSetText,       (HWND, void *));                   /* PLARGE_STRING */

/* tagLARGE_STRING as NtUserDefSetText reads it: bytes 0..3 Length (bytes), 4..7 MaximumLength with
 * bit 31 = bAnsi, 8..15 Buffer. */
struct wpsq_lstr { ULONG Length; ULONG MaxAnsi; void *Buffer; };
/* PROPLISTITEM as written by the kernel property enumeration: 16-byte stride, data at +0, atom at +8. */
struct wpsq_prop { ULONG_PTR data; USHORT atom; USHORT pad[3]; };

/* ---- window classes + logging wndproc ---------------------------------------------------- */
#define WPSQ_EXTRA 64
#define WPSQ_LOGMAX 256
struct wpsq_log { UINT m; WPARAM w; LPARAM l; DWORD tid; unsigned char head[16]; };
/* Shared state lives in one translation unit (wpsq_base.c defines WPSQ_DEFINE); every other .c sees externs,
 * so a window class registered by one file logs into the same buffers as seen by all of them. */
#ifdef WPSQ_DEFINE
#define WD(decl, init) decl = init
#else
#define WD(decl, init) extern decl
#endif
WD(struct wpsq_log g_wlog[WPSQ_LOGMAX], {{0}});
WD(volatile LONG g_wlog_n, 0);
WD(volatile LONG g_wlog_on, 0);
/* behaviour switches for the logging procs */
WD(volatile LONG g_gettext_override, 0);          /* WM_GETTEXT answers g_override_text instead of calling DefWindowProc */
WD(volatile LONG g_gettextlen_override, -1);      /* WM_GETTEXTLENGTH answers this when >= 0 */
WD(volatile LONG g_settext_swallow, 0);           /* WM_SETTEXT returns TRUE without storing */
WD(volatile LONG g_style_force_on, 0);            /* WM_STYLECHANGING rewrites styleNew when wParam == g_style_force_idx */
WD(volatile LONG g_style_force_idx, 0);
WD(volatile LONG g_style_force_val, 0);
WD(const char *g_override_text, "OVERRIDE");

static void wpsq_log_reset(void) { g_wlog_n = 0; }
static int  wpsq_log_count(UINT m) {
    int c = 0, n = g_wlog_n; if (n > WPSQ_LOGMAX) n = WPSQ_LOGMAX;
    for (int i = 0; i < n; i++) if (g_wlog[i].m == m) c++;
    return c;
}
static struct wpsq_log *wpsq_log_find(UINT m, int nth) {
    int n = g_wlog_n; if (n > WPSQ_LOGMAX) n = WPSQ_LOGMAX;
    for (int i = 0; i < n; i++) if (g_wlog[i].m == m && nth-- == 0) return &g_wlog[i];
    return NULL;
}

LRESULT CALLBACK wpsq_procW(HWND h, UINT m, WPARAM w, LPARAM l);
LRESULT CALLBACK wpsq_procA(HWND h, UINT m, WPARAM w, LPARAM l);
LRESULT wpsq_core(HWND h, UINT m, WPARAM w, LPARAM l, int ansi);
#ifdef WPSQ_DEFINE
LRESULT wpsq_core(HWND h, UINT m, WPARAM w, LPARAM l, int ansi) {
    if (g_wlog_on && (m == WM_GETTEXT || m == WM_GETTEXTLENGTH || m == WM_SETTEXT ||
                      m == WM_STYLECHANGING || m == WM_STYLECHANGED)) {
        LONG i = InterlockedIncrement(&g_wlog_n) - 1;
        if (i < WPSQ_LOGMAX) {
            struct wpsq_log *e = &g_wlog[i];
            e->m = m; e->w = w; e->l = l; e->tid = GetCurrentThreadId();
            memset(e->head, 0, sizeof e->head);
            if (m == WM_SETTEXT && l) k_try_read((void *)l, e->head, 8);
        }
    }
    switch (m) {
    case WM_GETTEXT:
        if (g_gettext_override) {
            const char *s = g_override_text; size_t n = strlen(s);
            if (!w) return 0;
            if (n > w - 1) n = w - 1;
            if (ansi) { memcpy((char *)l, s, n); ((char *)l)[n] = 0; }
            else { for (size_t i = 0; i < n; i++) ((WCHAR *)l)[i] = (unsigned char)s[i]; ((WCHAR *)l)[n] = 0; }
            return (LRESULT)n;
        }
        break;
    case WM_GETTEXTLENGTH:
        if (g_gettextlen_override >= 0) return g_gettextlen_override;
        break;
    case WM_SETTEXT:
        if (g_settext_swallow) return TRUE;
        break;
    case WM_STYLECHANGING:
        if (g_style_force_on && (LONG)w == g_style_force_idx) ((STYLESTRUCT *)l)->styleNew = (DWORD)g_style_force_val;
        break;
    }
    return ansi ? U(DefWindowProcA)(h, m, w, l) : U(DefWindowProcW)(h, m, w, l);
}
LRESULT CALLBACK wpsq_procW(HWND h, UINT m, WPARAM w, LPARAM l) { return wpsq_core(h, m, w, l, 0); }
LRESULT CALLBACK wpsq_procA(HWND h, UINT m, WPARAM w, LPARAM l) { return wpsq_core(h, m, w, l, 1); }

#endif

WD(char g_clsA_name[64], {0});
WD(char g_clsW_name[64], {0});
WD(WCHAR g_clsW_wname[64], {0});
WD(int g_cls_ok, 0);
static int wpsq_reg(void) {
    if (g_cls_ok) return 1;
    if (!U(RegisterClassW) || !U(RegisterClassA)) return 0;
    snprintf(g_clsA_name, sizeof g_clsA_name, "win32ktWpsqA%lu", GetCurrentProcessId());
    snprintf(g_clsW_name, sizeof g_clsW_name, "win32ktWpsqW%lu", GetCurrentProcessId());
    for (int i = 0; i < 64; i++) g_clsW_wname[i] = (unsigned char)g_clsW_name[i];
    WNDCLASSA a; memset(&a, 0, sizeof a);
    a.lpfnWndProc = wpsq_procA; a.cbWndExtra = WPSQ_EXTRA; a.hInstance = GetModuleHandleA(NULL); a.lpszClassName = g_clsA_name;
    WNDCLASSW w; memset(&w, 0, sizeof w);
    w.lpfnWndProc = wpsq_procW; w.cbWndExtra = WPSQ_EXTRA; w.hInstance = GetModuleHandleA(NULL); w.lpszClassName = g_clsW_wname;
    g_cls_ok = U(RegisterClassA)(&a) && U(RegisterClassW)(&w);
    return g_cls_ok;
}
/* Hidden, off-screen, non-activating popup (a "unicode" window made with CreateWindowExW). */
static HWND wpsq_mkex(int ansi, DWORD style, DWORD ex, HWND parent, const char *title, UINT_PTR id) {
    if (!wpsq_reg()) return NULL;
    if (ansi)
        return U(CreateWindowExA)(ex | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, g_clsA_name, title, style,
                                  -32000, -32000, 1, 1, parent, (HMENU)id, GetModuleHandleA(NULL), NULL);
    WCHAR wt[128]; int i = 0;
    if (title) for (; title[i] && i < 127; i++) wt[i] = (unsigned char)title[i];
    wt[i] = 0;
    return U(CreateWindowExW)(ex | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, g_clsW_wname, title ? wt : NULL, style,
                              -32000, -32000, 1, 1, parent, (HMENU)id, GetModuleHandleA(NULL), NULL);
}
static HWND wpsq_mk(int ansi, const char *title) { return wpsq_mkex(ansi, WS_POPUP, 0, NULL, title, 0); }
static HWND wpsq_mkchild(HWND parent, UINT_PTR id) { return wpsq_mkex(0, WS_CHILD, 0, parent, "child", id); }

#define NEED_WND(h, ansi) \
    HWND h = wpsq_mk(ansi, "wpsq"); \
    if (!h) { t_skip("cannot create a window (err %lu)", GetLastError()); return; }

#define GARBAGE_HWND ((HWND)(ULONG_PTR)0xDEADBEEF)

/* ---- a window owned by a second thread of this process that pumps ----------------------- */
struct wpsq_owner { HANDLE th, ready, stop; HWND hwnd; DWORD tid; int ansi; const char *title; };
static DWORD WINAPI wpsq_owner_main(LPVOID p) {
    struct wpsq_owner *o = (struct wpsq_owner *)p;
    o->hwnd = wpsq_mk(o->ansi, o->title ? o->title : "owner");
    o->tid = GetCurrentThreadId();
    SetEvent(o->ready);
    if (!o->hwnd) return 1;
    DWORD t0 = GetTickCount();
    for (;;) {
        DWORD r = MsgWaitForMultipleObjects(1, &o->stop, FALSE, 100, QS_ALLINPUT);
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
        if (r == WAIT_OBJECT_0) break;
        if (GetTickCount() - t0 > 60000) break;   /* hard bound so a lost test can never hang the run */
    }
    U(DestroyWindow)(o->hwnd);
    return 0;
}
static int wpsq_owner_start(struct wpsq_owner *o, int ansi, const char *title) {
    memset(o, 0, sizeof *o);
    o->ansi = ansi; o->title = title;
    o->ready = CreateEventA(NULL, TRUE, FALSE, NULL);
    o->stop  = CreateEventA(NULL, TRUE, FALSE, NULL);
    o->th = CreateThread(NULL, 0, wpsq_owner_main, o, 0, NULL);
    if (!o->th || WaitForSingleObject(o->ready, 5000) != WAIT_OBJECT_0 || !o->hwnd) {
        SetEvent(o->stop);
        if (o->th) { WaitForSingleObject(o->th, 3000); CloseHandle(o->th); }
        CloseHandle(o->ready); CloseHandle(o->stop);
        return 0;
    }
    return 1;
}
static void wpsq_owner_stop(struct wpsq_owner *o) {
    SetEvent(o->stop);
    WaitForSingleObject(o->th, 5000);
    CloseHandle(o->th); CloseHandle(o->ready); CloseHandle(o->stop);
}

/* ---- tagWND client-side field access (user32 _GetWindowLong / GetWindowData / IsWindowUnicode) */
#define WND_hwnd       0x00   /* head.h */
#define WND_state2b    0x12   /* byte: bit0 server-side proc, bit3 ANSI window (IsWindowUnicode = !bit3) */
#define WND_exstyle    0x18   /* GWL_EXSTYLE reads (field & mask) */
#define WND_style      0x1c   /* GWL_STYLE */
#define WND_hmodule    0x20   /* GWLP_HINSTANCE */
#define WND_fnid       0x2a   /* low 14 bits: function id (0 = plain window) */
#define WND_id         0x98   /* GWLP_ID for child windows */
#define WND_userdata   0xd8   /* GWLP_USERDATA */
#define WND_cbextra    0xc8   /* cbwndExtra (u32) */
#define WND_ibextra    0xfc   /* ibWndExtra: size of the server-owned prefix (0 for plain windows) */
#define WND_extraptr   0x128  /* pointer to extra bytes */

static unsigned char *wpsq_wnd(HWND h) { return (unsigned char *)k_wnd_ptr(h); }
static int wpsq_rd(HWND h, size_t off, void *out, size_t n) {
    unsigned char *p = wpsq_wnd(h);
    return p && k_try_read(p + off, out, n);
}

/* ---- child process helpers --------------------------------------------------------------- */
WD(int g_cfail, 0);
#define CC(c) do { if (!(c)) { g_cfail++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CCF(c, ...) do { if (!(c)) { g_cfail++; printf("  FAIL %s:%d: %s (", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf(")\n"); } } while (0)
static HWND wpsq_arg_hwnd(char **argv, int i) { return (HWND)(ULONG_PTR)strtoull(argv[i], NULL, 16); }

#endif
