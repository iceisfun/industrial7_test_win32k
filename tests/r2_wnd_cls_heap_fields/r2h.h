/* r2_wnd_cls_heap_fields - shared helpers (decoder of the client view of the desktop heap).
 *
 * The kernel maps the desktop heap read-only into every GUI process; user32 reads windows, classes
 * and the DESKTOPINFO from it without a syscall. No PDB type for tagWND/tagCLS/DESKTOPINFO/
 * CLIENTINFO exists in the corpus, so every offset below is one the real 19041 user32 code uses
 * (the function that fixes it is named next to it). Entry layout:
 *   +0x00 phead (client = phead + ulClientDelta, TEB+0x828)   +0x08 owner PTHREADINFO
 *   +0x10 desktop guard (== first qword of DESKTOPINFO)       +0x18 type (1 = window)
 *   +0x19 flags (bit0 destroyed)                              +0x1a uniq
 */
#ifndef R2H_H
#define R2H_H
#include "k.h"
#ifndef R2H_TAG
#error define R2H_TAG (a short wide string unique per .c file) before including r2h.h
#endif
#include <stdint.h>
#include <stdlib.h>
#include <wchar.h>

/* ---- TEB.Win32ClientInfo (TEB+0x800) fields user32 uses ------------------------------------ */
#define TEBO_PTI         0x078   /* PtiCurrent() */
#define TEBO_DESKINFO    0x820   /* GetCurrentThreadDesktopWindow / HMValidateHandle: DESKTOPINFO* */
#define TEBO_DELTA       0x828   /* ulClientDelta */
#define TEBO_CBWND       0x840   /* CallbackWnd.hwnd   (fast path: hWnd == this) */
#define TEBO_CBPWND      0x848   /* CallbackWnd.pwnd   (client pointer for the above) */
/* DESKTOPINFO (GetCurrentThreadDesktopWindow, GetShellWindow) */
#define DI_BASE          0x00    /* first qword, compared with handle entry +0x10 */
#define DI_SPWND         0x08    /* kernel pointer of the desktop window; HWND of it is GetDesktopWindow() */
#define DI_SHELL         0x18    /* kernel pointer of the shell window or 0; GetShellWindow() */

/* ---- tagWND client view (all offsets from the user32 functions in the comment) -------------- */
#define W_HWND      0x00  /* GetWindow: *(HWND*)pwnd */
#define W_PSELF     0x08  /* GetParent/GetWindowWorker: kernel address of this object; pointer fixup base */
#define W_STATE     0x10  /* DWORD. byte 0x12 bit0 dialog (GetWindowWord, _GetWindowLongPtr), bit2 server proc
                             (GetWindowData -4), bit3 ANSI (IsWindowUnicode) */
#define W_EXSTYLE   0x18  /* GetWindowData -0x14 */
#define W_STYLE     0x1c  /* IsIconic/IsZoomed/IsWindowEnabled/GetParent read byte 0x1f of it */
#define W_HMODULE   0x20  /* GetWindowData -6 */
#define W_FNID      0x2a  /* USHORT, & 0x2fff compared with 0x29d (desktop) in GetWindowRect etc. */
#define W_PARENT    0x30  /* GetParent (child) / GetWindowWorker GW_HWNDFIRST */
#define W_CHILD     0x38  /* GetWindowWorker GW_CHILD */
#define W_OWNER     0x40  /* GetParent (popup) / GetWindowWorker GW_OWNER */
#define W_NEXT      0x48  /* GetWindowWorker GW_HWNDNEXT */
#define W_PREV      0x50  /* GetWindowWorker GW_HWNDPREV */
#define W_RCWINDOW  0x58  /* GetWindowRect */
#define W_RCCLIENT  0x68  /* GetClientRect */
#define W_WNDPROC   0x78  /* GetWindowData -4 */
#define W_PCLS      0x80  /* _GetClassLongPtr */
#define W_ID        0x98  /* GetWindowData -0xc */
#define W_HRGN      0xa8  /* GetWindowRgn */
#define W_LASTACT   0xb0  /* GetLastActivePopup */
#define W_CBEXTRA   0xc8  /* _GetWindowLongPtr: size of the extra bytes */
#define W_USERDATA  0xd8  /* GetWindowData -0x15 */
#define W_STATE2    0xe8  /* _GetWindowLong: bit 0x800 = remote extra bytes; GetWindowData -0x14: bit 0x20 */
#define W_BAND      0xec  /* GetWindowWorker compares with 1, 2 and 0x10 (z-order band) */
#define W_EXTRABASE 0xfc  /* _GetWindowLongPtr: first valid extra-byte index */
#define W_MONITOR   0x100 /* GetClientRect: validates the monitor handle */
#define W_DPICTX    0x120 /* GetWindowRect / GetClientRect */
#define W_EXTRAPTR  0x128 /* _GetWindowLongPtr: pointer to the extra bytes */
#define WV_SNAP     0x140

/* tagCLS (user32 _GetClassLongPtr / _GetClassData and the offset table at user32 rva 0x95362) */
#define C_ATOM      0x02  /* USHORT, GCW_ATOM */
#define C_FLAGS     0x06  /* USHORT, &1 server side proc, &2 ANSI class */
#define C_STYLE     0x08  /* DWORD, GCL_STYLE returns it & 0x37bff */
#define C_CBCLS     0x0c  /* int, GCL_CBCLSEXTRA */
#define C_WNDPROC   0x20  /* GCLP_WNDPROC */
#define C_HMODULE   0x40  /* GCLP_HMODULE */
#define C_HBR       0x48  /* GCLP_HBRBACKGROUND */
#define C_CBWND0    0x50  /* int; GCL_CBWNDEXTRA = [0x50] + [0x54] */
#define C_CBWND1    0x54
#define C_EXTRA     0x58  /* class extra bytes (_GetClassLongPtr: cls + index + 0x58) */
#define CLS_SNAP    0x80

#define FNID_DESKTOP 0x029d  /* compared in GetWindowRect/GetClientRect/IsWindowVisible/GetWindowData */

/* ---- handle entry --------------------------------------------------------------------------- */
struct he {
    uint64_t phead, owner, guard;
    unsigned char type, flags;
    unsigned short uniq;
    uint64_t count;       /* cHandleEntries: *(psi + 8) */
    unsigned long esize;  /* HeEntrySize */
};

static bool he_read(HWND h, struct he *e) {
    struct k_user_shared_info si;
    memset(e, 0, sizeof *e);
    if (!k_shared_info(&si) || !si.aheList || !si.psi) return false;
    e->esize = si.heEntrySize ? si.heEntrySize : 0x20;
    if (e->esize < 0x1c || e->esize > 0x100) return false;
    if (!k_try_read((char *)si.psi + 8, &e->count, 8)) return false;
    uint64_t idx = (uint64_t)((ULONG_PTR)h & 0xffff);
    if (idx >= e->count) return false;
    char *ent = (char *)si.aheList + idx * e->esize;
    if (!k_try_read(ent + 0x00, &e->phead, 8)) return false;
    if (!k_try_read(ent + 0x08, &e->owner, 8)) return false;
    if (!k_try_read(ent + 0x10, &e->guard, 8)) return false;
    if (!k_try_read_u8(ent + 0x18, &e->type)) return false;
    if (!k_try_read_u8(ent + 0x19, &e->flags)) return false;
    if (!k_try_read_u16(ent + 0x1a, &e->uniq)) return false;
    return true;
}

static uint64_t teb_q(unsigned off) {
    uint64_t v = 0;
    k_try_read((char *)k_teb() + off, &v, 8);
    return v;
}
static uint64_t pti_now(void) { return teb_q(TEBO_PTI); }
static uint64_t delta_now(void) { return teb_q(TEBO_DELTA); }
static uint64_t deskinfo_now(void) { return teb_q(TEBO_DESKINFO); }

/* ---- snapshot of one window ----------------------------------------------------------------- */
struct wv {
    HWND hwnd;
    uint64_t pc;            /* client address of the window body */
    struct he e;
    uint64_t delta;
    unsigned char b[WV_SNAP];
};
static uint64_t W64(const struct wv *w, unsigned o) { uint64_t v; memcpy(&v, w->b + o, 8); return v; }
static uint32_t W32(const struct wv *w, unsigned o) { uint32_t v; memcpy(&v, w->b + o, 4); return v; }
static uint16_t W16(const struct wv *w, unsigned o) { uint16_t v; memcpy(&v, w->b + o, 2); return v; }
static int32_t  WI(const struct wv *w, unsigned o) { return (int32_t)W32(w, o); }
static RECT WRC(const struct wv *w, unsigned o) {
    RECT r; r.left = WI(w, o); r.top = WI(w, o + 4); r.right = WI(w, o + 8); r.bottom = WI(w, o + 12); return r;
}
static uint32_t wv_style(const struct wv *w) { return W32(w, W_STYLE); }
static uint32_t wv_ex(const struct wv *w) { return W32(w, W_EXSTYLE); }
static unsigned wv_fnid(const struct wv *w) { return W16(w, W_FNID) & 0x2fff; }

static bool wv_get(HWND h, struct wv *w) {
    memset(w, 0, sizeof *w);
    w->hwnd = h;
    if (!h) return false;
    if (!he_read(h, &w->e)) return false;
    if (w->e.type != 1 || (w->e.flags & 1)) return false;
    if (w->e.uniq != (unsigned short)((ULONG_PTR)h >> 16) && w->e.uniq != 0xffff) return false;
    w->delta = delta_now();
    w->pc = w->e.phead + w->delta;
    if (!k_try_read((void *)(ULONG_PTR)w->pc, w->b, WV_SNAP)) return false;
    if ((uint32_t)W64(w, W_HWND) != (uint32_t)(ULONG_PTR)h) return false;
    return true;
}
/* Client address of a kernel pointer stored in this window (the fixup user32 performs). */
static uint64_t wv_fix(const struct wv *w, uint64_t k) { return k ? w->pc + (k - W64(w, W_PSELF)) : 0; }
/* HWND of the window the kernel pointer at offset `off` refers to; NULL if the link is empty,
 * (HWND)-1 if the pointer could not be followed. */
static HWND wv_link(const struct wv *w, unsigned off) {
    uint64_t k = W64(w, off);
    if (!k) return NULL;
    uint64_t a = wv_fix(w, k), v = 0;
    if (!k_try_read((void *)(ULONG_PTR)a, &v, 8)) return (HWND)(ULONG_PTR)-1;
    return (HWND)(ULONG_PTR)(uint32_t)v;
}
static bool wv_read_at(uint64_t a, void *out, size_t n) { return k_try_read((void *)(ULONG_PTR)a, out, n); }

/* Memory properties of an address in the mapped heap. */
static bool vq(uint64_t a, MEMORY_BASIC_INFORMATION *mbi) {
    memset(mbi, 0, sizeof *mbi);
    return VirtualQuery((void *)(ULONG_PTR)a, mbi, sizeof *mbi) == sizeof *mbi;
}
static bool same_view(uint64_t a, uint64_t b) {
    MEMORY_BASIC_INFORMATION x, y;
    if (!vq(a, &x) || !vq(b, &y)) return false;
    return x.State == MEM_COMMIT && y.State == MEM_COMMIT && x.AllocationBase && x.AllocationBase == y.AllocationBase;
}

/* class view */
struct cv { uint64_t pc; unsigned char b[CLS_SNAP]; };
static bool cv_get(const struct wv *w, struct cv *c) {
    memset(c, 0, sizeof *c);
    uint64_t k = W64(w, W_PCLS);
    if (!k) return false;
    c->pc = wv_fix(w, k);
    return k_try_read((void *)(ULONG_PTR)c->pc, c->b, CLS_SNAP);
}
static uint64_t C64(const struct cv *c, unsigned o) { uint64_t v; memcpy(&v, c->b + o, 8); return v; }
static uint32_t C32(const struct cv *c, unsigned o) { uint32_t v; memcpy(&v, c->b + o, 4); return v; }
static uint16_t C16(const struct cv *c, unsigned o) { uint16_t v; memcpy(&v, c->b + o, 2); return v; }

/* ---- fixtures ------------------------------------------------------------------------------- */
#define MAXTRK 256
static HWND g_trk[MAXTRK];
static int g_ntrk;
static wchar_t g_trkcls[32][64];
static int g_ntrkcls;
static HINSTANCE g_hinst(void) { return GetModuleHandleW(NULL); }

static void wk_reap(void) {
    for (int i = g_ntrk - 1; i >= 0; i--) {
        if (g_trk[i] && U(IsWindow) && U(IsWindow)(g_trk[i])) U(DestroyWindow)(g_trk[i]);
    }
    g_ntrk = 0;
    for (int i = 0; i < g_ntrkcls; i++) if (U(UnregisterClassW)) U(UnregisterClassW)(g_trkcls[i], g_hinst());
    g_ntrkcls = 0;
}
static HWND wk_add(HWND h) { if (h && g_ntrk < MAXTRK) g_trk[g_ntrk++] = h; return h; }

/* Message log for tests that look at what the kernel left in the TEB during a callback. */
struct cbrec {
    int on, n;
    UINT msg[24]; HWND wnd[24];
    uint64_t cache_h[24], cache_p[24], pwnd[24];
};
static struct cbrec g_rec;

static LRESULT CALLBACK r2h_wp(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (g_rec.on && g_rec.n < 24) {
        int i = g_rec.n++;
        struct he e;
        g_rec.msg[i] = m; g_rec.wnd[i] = h;
        g_rec.cache_h[i] = teb_q(TEBO_CBWND);
        g_rec.cache_p[i] = teb_q(TEBO_CBPWND);
        g_rec.pwnd[i] = he_read(h, &e) ? e.phead + delta_now() : 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}
static LRESULT CALLBACK r2h_wpA(HWND h, UINT m, WPARAM wp, LPARAM lp) { return DefWindowProcA(h, m, wp, lp); }
static LRESULT CALLBACK r2h_wp2(HWND h, UINT m, WPARAM wp, LPARAM lp) { return DefWindowProcW(h, m, wp, lp); }

static int g_clsctr;
/* Registers a private class with a unique name; the name is returned in out[] and tracked. */
static ATOM reg_class(wchar_t out[64], UINT style, int cbcls, int cbwnd, WNDPROC proc, BOOL ansi, HBRUSH hbr, HCURSOR cur) {
    if (!U(RegisterClassW)) return 0;
    _snwprintf(out, 64, L"win32kt_r2h_%ls_%lu_%d", R2H_TAG, GetCurrentProcessId(), ++g_clsctr);
    ATOM a;
    if (ansi) {
        WNDCLASSA wc; memset(&wc, 0, sizeof wc);
        char nm[64]; wcstombs(nm, out, 64);
        wc.style = style; wc.lpfnWndProc = (WNDPROC)proc; wc.cbClsExtra = cbcls; wc.cbWndExtra = cbwnd;
        wc.hInstance = g_hinst(); wc.hbrBackground = hbr; wc.hCursor = cur; wc.lpszClassName = nm;
        a = U(RegisterClassA)(&wc);
    } else {
        WNDCLASSW wc; memset(&wc, 0, sizeof wc);
        wc.style = style; wc.lpfnWndProc = proc; wc.cbClsExtra = cbcls; wc.cbWndExtra = cbwnd;
        wc.hInstance = g_hinst(); wc.hbrBackground = hbr; wc.hCursor = cur; wc.lpszClassName = out;
        a = U(RegisterClassW)(&wc);
    }
    if (a && g_ntrkcls < 32) wcscpy(g_trkcls[g_ntrkcls++], out);
    return a;
}
/* The default fixture class: 32 class extra bytes, 64 window extra bytes, Unicode. */
static const wchar_t *plain_class(void) {
    static wchar_t name[64];
    static int done;
    if (!done) {
        WNDCLASSW wc; memset(&wc, 0, sizeof wc);
        _snwprintf(name, 64, L"win32kt_r2h_plain_%ls_%lu", R2H_TAG, GetCurrentProcessId());
        wc.lpfnWndProc = r2h_wp; wc.cbClsExtra = 32; wc.cbWndExtra = 64; wc.hInstance = g_hinst();
        wc.hbrBackground = (HBRUSH)(ULONG_PTR)(COLOR_WINDOW + 1);
        wc.hCursor = U(LoadCursorW) ? U(LoadCursorW)(NULL, MAKEINTRESOURCEW(32512)) : NULL;
        wc.lpszClassName = name;
        if (!U(RegisterClassW)(&wc)) return NULL;
        done = 1;
    }
    return name;
}
#define PLAIN_CBCLS 32
#define PLAIN_CBWND 64

#define EX_GUEST (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)
/* Hidden, off-screen, non-activating top-level popup. */
static HWND mk_top(const wchar_t *cls, DWORD style, DWORD ex, int x, int y, int w, int h, HWND owner) {
    if (!U(CreateWindowExW) || !cls) return NULL;
    return wk_add(U(CreateWindowExW)(ex | EX_GUEST, cls, L"r2h", (style & ~WS_VISIBLE),
                                     x, y, w, h, owner, NULL, g_hinst(), NULL));
}
static HWND mk_plain(void) { return mk_top(plain_class(), WS_POPUP, 0, -30000, -30000, 100, 80, NULL); }
static HWND mk_child(HWND parent, const wchar_t *cls, DWORD style, int x, int y, int w, int h, UINT id) {
    if (!U(CreateWindowExW) || !cls || !parent) return NULL;
    return wk_add(U(CreateWindowExW)(0, cls, L"r2h", (style & ~WS_VISIBLE) | WS_CHILD, x, y, w, h, parent,
                                     (HMENU)(ULONG_PTR)id, g_hinst(), NULL));
}
/* Parent for child tests: hidden, off-screen, sized popup. */
static HWND mk_host(void) { return mk_top(plain_class(), WS_POPUP, 0, -30000, -30000, 300, 200, NULL); }

#define T_BEGIN() do { wk_reap(); if (!U(CreateWindowExW) || !U(DestroyWindow) || !U(IsWindow)) { t_skip("user32 window API missing"); return; } } while (0)
#define NEED_WND(h, w) \
    struct wv w; \
    do { if (!(h)) { DWORD e_ = GetLastError(); wk_reap(); t_skip("window creation failed (err %lu)", e_); return; } \
         if (!wv_get((h), &(w))) { wk_reap(); t_skip("client tagWND view not decodable here (not a Windows desktop heap)"); return; } } while (0)
#define END() wk_reap()

/* True when no DPI scaling can sit between the kernel rectangle and what user32 returns. */
static bool dpi96(HWND h) {
    typedef UINT (WINAPI *fn_t)(HWND);
    typedef UINT (WINAPI *fs_t)(void);
    HMODULE u = GetModuleHandleA("user32.dll");
    fn_t f = u ? (fn_t)GetProcAddress(u, "GetDpiForWindow") : NULL;
    fs_t s = u ? (fs_t)GetProcAddress(u, "GetDpiForSystem") : NULL;
    if (!f || !s) return true;
    return f(h) == 96 && s() == 96;
}

/* ---- helper thread that owns a window ------------------------------------------------------- */
struct xt {
    HANDLE thread, ready, stop;
    HWND hwnd;
    DWORD tid;
    const wchar_t *cls;
};
static DWORD WINAPI xt_main(void *arg) {
    struct xt *x = (struct xt *)arg;
    x->tid = GetCurrentThreadId();
    x->hwnd = U(CreateWindowExW)(EX_GUEST, x->cls, L"r2h-x", WS_POPUP, -30000, -30000, 50, 50, NULL, NULL, g_hinst(), NULL);
    SetEvent(x->ready);
    DWORD t0 = GetTickCount();
    while (GetTickCount() - t0 < 15000) {
        DWORD r = MsgWaitForMultipleObjects(1, &x->stop, FALSE, 100, QS_ALLINPUT);
        MSG m;
        while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
        if (r == WAIT_OBJECT_0) break;
    }
    if (x->hwnd) DestroyWindow(x->hwnd);
    return 0;
}
static bool xt_start(struct xt *x, const wchar_t *cls) {
    memset(x, 0, sizeof *x);
    x->cls = cls;
    x->ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    x->stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    x->thread = CreateThread(NULL, 0, xt_main, x, 0, NULL);
    if (!x->thread || WaitForSingleObject(x->ready, 5000) != WAIT_OBJECT_0 || !x->hwnd) { return false; }
    return true;
}
static void xt_stop(struct xt *x) {
    if (x->stop) SetEvent(x->stop);
    if (x->thread) { WaitForSingleObject(x->thread, 5000); CloseHandle(x->thread); }
    if (x->ready) CloseHandle(x->ready);
    if (x->stop) CloseHandle(x->stop);
    memset(x, 0, sizeof *x);
}
/* TEB.Win32ThreadInfo of another thread of this process (NtQueryInformationThread + safe read). */
static uint64_t thread_pti(DWORD tid) {
    typedef LONG (WINAPI *qit_t)(HANDLE, int, void *, ULONG, ULONG *);
    qit_t q = (qit_t)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationThread");
    HANDLE th = OpenThread(THREAD_QUERY_INFORMATION, FALSE, tid);
    uint64_t pti = 0;
    if (q && th) {
        struct { LONG st; PVOID teb; PVOID cid[2]; ULONG_PTR aff; LONG pri, bpri; } tbi;
        memset(&tbi, 0, sizeof tbi);
        if (q(th, 0, &tbi, sizeof tbi, NULL) >= 0 && tbi.teb) k_try_read((char *)tbi.teb + TEBO_PTI, &pti, 8);
    }
    if (th) CloseHandle(th);
    return pti;
}

/* Direct syscalls (signatures from win32kfull NtUserCallOneParam / NtUserQueryWindow and the
 * user32 callers that pass (hwnd, 0x38) and (hwnd, 0|2)). */
UDECL(ULONG_PTR, NtUserCallOneParam, (ULONG_PTR, DWORD));
UDECL(ULONG_PTR, NtUserQueryWindow, (HWND, DWORD));


/* ---- additional offsets (user32 19041 functions named) ---------------------------------------- */
#define W_STRLEN    0xb8  /* strName.Length in bytes (RealDefWindowProcWorker WM_GETTEXT: >>1 = characters) */
#define W_STRMAX    0xbc  /* strName.MaximumLength | bAnsi<<31 (LARGE_UNICODE_STRING) */
#define W_STRBUF    0xc0  /* strName.Buffer, kernel pointer fixed up like the other links */
#define ST_DIALOG   0x00010000u   /* state: byte 0x12 bit 0 (ValidateDialogPwnd) */
#define ST_SRVPROC  0x00040000u   /* byte 0x12 bit 2 (SendMessageWorker, _GetWindowLong) */
#define ST_ANSIPROC 0x00080000u   /* byte 0x12 bit 3 (IsWindowUnicode = !bit) */
#define ST_ANSICREATOR 0x20000000u /* byte 0x13 bit 5 (RealDefWindowProc WM_NOTIFYFORMAT NF_QUERY) */

/* TEB.Win32ClientInfo (TEB+0x800) fields */
#define CI_FLAGS     0x800  /* CallNextHookEx sets bit 0x10; GetKeyboardLayout tests bit 0x4000 */
#define CI_EXPWINVER 0x810  /* GetWindowInfo / GetAppCompatFlags2 compare with it */
#define CI_COMPAT1   0x814  /* GetAppCompatFlags */
#define CI_COMPAT2   0x818  /* GetAppCompatFlags2 */
#define CI_TIFLAGS   0x81c  /* thread info flags: RealDefWindowProc tests 0x2000000, GetWindowWorker tests 8 */
#define CI_PHKCUR    0x830  /* CallNextHookEx: NULL outside a hook proc */
#define CI_FSHOOKS   0x838  /* SendMessageWorker ORs it with DESKTOPINFO+0x10 and tests 0x2020 */
#define CI_HOOKCUR   0x858  /* CallNextHookEx: low word = proc flags, high word = hook id */
#define CI_PCTI      0x860  /* CLIENTTHREADINFO* (GetInputState / GetQueueStatus / PeekMessage / InSendMessage) */
#define CI_KEYCACHE  0x870  /* GetKeyState: compared with a shared-info key-state counter */
#define CI_KEYSTATE  0x874  /* GetKeyState: 2 bits per key for VK < 0x20 */
/* CLIENTTHREADINFO */
#define CTI_FLAGS    0x00   /* bit 1 = inside a SendMessage from another thread (InSendMessage) */
#define CTI_CHANGE   0x04   /* WORD fsChangeBits (GetInputState tests & 5) */
#define CTI_WAKE     0x06   /* WORD fsWakeBits (PeekMessage fast path) */
#define CTI_TIMEREAD 0x0c   /* tick of the last message read (PeekMessage fast path, < 0x3e9 ms) */
/* DESKTOPINFO additional */
#define DI_FSHOOKS   0x10
#define DI_TASKMAN   0x20   /* GetTaskmanWindow */
#define DI_PROGMAN   0x28   /* GetProgmanWindow */
#define GPSI_KEYCACHE 0x1b48
#define GPSI_FNIDEXTRA 0x148 /* WORD table indexed by fnid - 0x29a: 0x140 + cbWndExtra of the server class */

static uint32_t teb_d(unsigned off) { uint32_t v = 0; k_try_read((char *)k_teb() + off, &v, 4); return v; }
static uint64_t read_q(uint64_t a) { uint64_t v = 0; k_try_read((void *)(ULONG_PTR)a, &v, 8); return v; }
static uint32_t read_d(uint64_t a) { uint32_t v = 0; k_try_read((void *)(ULONG_PTR)a, &v, 4); return v; }
static uint16_t read_w(uint64_t a) { uint16_t v = 0; k_try_read((void *)(ULONG_PTR)a, &v, 2); return v; }
/* HWND stored in the object a kernel pointer from DESKTOPINFO refers to (user32 adds ulClientDelta). */
static HWND kptr_hwnd(uint64_t kptr) {
    if (!kptr) return NULL;
    uint64_t v = 0;
    if (!k_try_read((void *)(ULONG_PTR)(kptr + delta_now()), &v, 8)) return (HWND)(ULONG_PTR)-1;
    return (HWND)(ULONG_PTR)(uint32_t)v;
}
/* Skips the test when the client view is not a Windows desktop heap (Wine, no GUI connection). */
#define NEED_GUI() do { T_BEGIN(); if (U(GetDesktopWindow)) U(GetDesktopWindow)(); if (!deskinfo_now() || !delta_now()) { wk_reap(); t_skip("no win32k client info in the TEB"); return; } } while (0)

#endif
