/* r2_teb_peb_field_matrix: shared helpers.
 *
 * Offsets inside _CLIENTINFO (TEB+0x800), every one pinned by an analysis (19041):
 *   +0x00 CI_flags            user32!ClientThreadSetup (|= 8), user32!GetKeyboardLayout (bit 14),
 *                             user32!CallNextHookEx (bit 4)
 *   +0x10 dwExpWinVer         win32kbase!InitClientInfo (= pti+0x278)
 *   +0x1c dwTIFlags           win32kbase!InitClientInfo (= pti->TIF_flags at conversion time)
 *   +0x20 pDeskInfo          user32!ClientThreadSetup / DefWindowProcW / SendMessageW
 *   +0x28 ulClientDelta       user32!DefWindowProcW: tagWND* = entry.pHead + CI[0x28]
 *   +0x30 phkCurrent          user32!CallNextHookEx (zero => returns 0 immediately)
 *   +0x40 CallbackWnd.hwnd    user32!SendMessageW/DefWindowProcW/CallWindowProcW (cache compare)
 *   +0x48 CallbackWnd.pwnd    same, the tagWND* used when hwnd matches the cache
 *   +0x58 dwHookCurrent       user32!CallNextHookEx: lo16 -> NtUserCallNextHookEx, hi16 = hook id
 *   +0x60 pClientThreadInfo   user32!InSendMessage / GetInputState / PeekMessageW
 *   +0x68 dwHookData          user32!CallNextHookEx (saved/restored)
 *   +0x70 dwKeyCache          user32!GetKeyState
 *   +0x74 afKeyState[]        user32!GetKeyState
 *   +0x90 hKL / +0x98 CodePage  win32kbase!InitClientInfo (spklActive->hkl / ->CodePage)
 *   +0xa0 msgDbcsCB (MSG)     user32!GetMessageA (hwnd +0xa0, message +0xa8, wParam +0xb0)
 *   +0xd0 lpdwRegisteredClasses  InitClientInfo zeroes it; ClientThreadSetup sets &gdwRegisteredClasses
 *   +0xe0 flags, +0xe8/+0xf0  InitClientInfo (embedded CLIENTTHREADINFO storage)
 */
#ifndef RTP_H
#define RTP_H
#include "t.h"
#include "k.h"

#define CI_SIZE 0x1F0
#define CIO_flags        0x00
#define CIO_cSpins       0x08
#define CIO_dwExpWinVer  0x10
#define CIO_dwCompat     0x14
#define CIO_dwCompat2    0x18
#define CIO_dwTIFlags    0x1c
#define CIO_pDeskInfo    0x20
#define CIO_ulClientDelta 0x28
#define CIO_phkCurrent   0x30
#define CIO_fsHooks      0x38
#define CIO_cbHwnd       0x40
#define CIO_cbPwnd       0x48
#define CIO_cbActCtx     0x50
#define CIO_dwHookCurrent 0x58
#define CIO_cInDDEML     0x5c
#define CIO_pcti         0x60
#define CIO_dwHookData   0x68
#define CIO_dwKeyCache   0x70
#define CIO_afKeyState   0x74
#define CIO_hKL          0x90
#define CIO_CodePage     0x98
#define CIO_msgDbcsCB    0xa0
#define CIO_msgDbcsEnd   0xd0
#define CIO_regClasses   0xd0
#define CIO_flags2       0xe0
#define CIO_ctiStore     0xe8

#define CIF_INITIALIZED  0x8   /* user32!ClientThreadSetup sets bit 3 once the thread is set up */

#define RT_USER_MAX 0x00007FFFFFFE0000ULL
static inline int rt_user_va(unsigned long long p) { return p >= 0x10000ULL && p < RT_USER_MAX; }

/* ---- a complete value snapshot of the win32 members of one TEB ----------------------------------- */
struct rsnap {
    ULONG_PTR teb;
    int       ok;                      /* TEB readable */
    unsigned long long ti;             /* +0x78 Win32ThreadInfo */
    unsigned long pid, tid;            /* ClientId */
    unsigned char ci[CI_SIZE];         /* +0x800 */
    unsigned long gdi_off;             /* GdiTebBatch.Offset dword (bit31 = HasRenderingCommand) */
    unsigned long long gdi_hdc;        /* GdiTebBatch.HDC */
    unsigned long gdi_count;           /* +0x1740 */
    unsigned long long gdi_cached;     /* +0x7e8 */
    unsigned long gdi_pid, gdi_tid;    /* +0x7f0/+0x7f4 */
    unsigned long long gdi_tli;        /* +0x7f8 */
    unsigned long long gl_dispatch0;   /* +0x9f0 */
    unsigned long long gl_sectioninfo, gl_section, gl_table, gl_currentrc, gl_context;   /* +0x1228.. */
    unsigned long last_error, last_status;
};

static inline unsigned long long rs_ci64(const struct rsnap *s, unsigned off) { unsigned long long v; memcpy(&v, s->ci + off, 8); return v; }
static inline unsigned long      rs_ci32(const struct rsnap *s, unsigned off) { unsigned long v; memcpy(&v, s->ci + off, 4); return v; }
static inline unsigned short     rs_ci16(const struct rsnap *s, unsigned off) { unsigned short v; memcpy(&v, s->ci + off, 2); return v; }

static inline unsigned long long rt_rd64(ULONG_PTR base, unsigned off) {
    unsigned long long v = 0; k_try_read((void *)(base + off), &v, 8); return v; }
static inline unsigned long rt_rd32(ULONG_PTR base, unsigned off) {
    unsigned long v = 0; k_try_read_u32((void *)(base + off), &v); return v; }

/* Reads the snapshot out of the TEB at `teb` (the calling thread's own, or another thread's TEB of this
 * process - same address space, so k_try_read is enough). */
static void rs_take_teb(struct rsnap *s, ULONG_PTR teb) {
    memset(s, 0, sizeof *s);
    s->teb = teb;
    unsigned char probe;
    s->ok = teb && k_try_read((void *)teb, &probe, 1);
    if (!s->ok) return;
    s->ti = rt_rd64(teb, TEB_Win32ThreadInfo);
    s->pid = (unsigned long)rt_rd64(teb, TEB_ClientId);
    s->tid = (unsigned long)rt_rd64(teb, TEB_ClientId + 8);
    k_try_read((void *)(teb + TEB_Win32ClientInfo), s->ci, CI_SIZE);
    s->gdi_off = rt_rd32(teb, TEB_GdiTebBatch);
    s->gdi_hdc = rt_rd64(teb, TEB_GdiTebBatch + GDI_TEB_BATCH_HDC);
    s->gdi_count = rt_rd32(teb, TEB_GdiBatchCount);
    s->gdi_cached = rt_rd64(teb, TEB_GdiCachedProcessHandle);
    s->gdi_pid = rt_rd32(teb, TEB_GdiClientPID);
    s->gdi_tid = rt_rd32(teb, TEB_GdiClientTID);
    s->gdi_tli = rt_rd64(teb, TEB_GdiThreadLocalInfo);
    s->gl_dispatch0 = rt_rd64(teb, TEB_glDispatchTable);
    s->gl_sectioninfo = rt_rd64(teb, TEB_glSectionInfo);
    s->gl_section = rt_rd64(teb, TEB_glSection);
    s->gl_table = rt_rd64(teb, TEB_glTable);
    s->gl_currentrc = rt_rd64(teb, TEB_glCurrentRC);
    s->gl_context = rt_rd64(teb, TEB_glContext);
    s->last_error = rt_rd32(teb, TEB_LastErrorValue);
    s->last_status = rt_rd32(teb, TEB_LastStatusValue);
}
static inline void rs_take(struct rsnap *s) { rs_take_teb(s, (ULONG_PTR)k_teb()); }

static inline int rs_ci_all_zero(const struct rsnap *s, unsigned from, unsigned to) {
    for (unsigned i = from; i < to && i < CI_SIZE; i++) if (s->ci[i]) return 0;
    return 1;
}

/* ---- ntdll without touching user32/gdi32 (t_proc would load them on a miss in win32u) ------------- */
static inline FARPROC rt_ntdll(const char *name) { return GetProcAddress(GetModuleHandleA("ntdll.dll"), name); }

typedef struct { LONG ExitStatus; PVOID TebBaseAddress; ULONG_PTR Pid, Tid; ULONG_PTR Affinity; LONG Priority, BasePriority; } RT_TBI;
typedef LONG (WINAPI *rt_NtQIT_t)(HANDLE, ULONG, PVOID, ULONG, PULONG);
/* TEB address of a thread of this process (0 on failure) */
static inline ULONG_PTR rt_thread_teb(HANDLE th) {
    rt_NtQIT_t f = (rt_NtQIT_t)rt_ntdll("NtQueryInformationThread");
    RT_TBI tbi; memset(&tbi, 0, sizeof tbi);
    if (!f || f(th, 0, &tbi, sizeof tbi, NULL) < 0) return 0;
    return (ULONG_PTR)tbi.TebBaseAddress;
}

/* ---- win32u / user32 entry points ------------------------------------------------------------------- */
UDECL(ULONG_PTR, NtUserGetThreadState, (int));
UDECL(LONG, NtGdiFlush, (void));
UDECL(HKL, NtUserGetKeyboardLayout, (DWORD));
UDECL(HANDLE, NtUserGetThreadDesktop, (DWORD));
static inline ULONG_PTR rt_ts(int idx) {   /* NtUserGetThreadState via win32u only */
    /* Wine's win32u aborts the process on this unimplemented service; the answer is meaningless there anyway */
    if (t_is_wine()) return (ULONG_PTR)-1;
    NtUserGetThreadState_pfn f = (NtUserGetThreadState_pfn)t_proc_in("win32u.dll", "NtUserGetThreadState");
    return f ? f(idx) : (ULONG_PTR)-1;
}
/* Brings user32 up and converts the calling thread through the ordinary path */
static inline HWND rt_gui(void) {
    HWND (WINAPI *f)(void) = (HWND (WINAPI *)(void))t_proc("GetDesktopWindow");
    return f ? f() : NULL;
}
static inline int rt_in_user32(unsigned long long p, unsigned long long n) {
    HMODULE m = GetModuleHandleA("user32.dll");
    if (!m) return 0;
    IMAGE_DOS_HEADER dos; IMAGE_NT_HEADERS64 nt;
    if (!k_try_read(m, &dos, sizeof dos) || dos.e_magic != IMAGE_DOS_SIGNATURE) return 0;
    if (!k_try_read((char *)m + dos.e_lfanew, &nt, sizeof nt) || nt.Signature != IMAGE_NT_SIGNATURE) return 0;
    unsigned long long base = (unsigned long long)(ULONG_PTR)m;
    return p >= base && p + n <= base + nt.OptionalHeader.SizeOfImage;
}

#define NOT_WINE() do { if (t_is_wine()) { t_skip("Wine's win32u aborts on the unimplemented service this test reaches"); return; } } while (0)

/* ---- worker threads ---------------------------------------------------------------------------------
 * A worker takes `before` (fresh thread), optionally runs op(w), takes `after`, signals ready, then parks
 * until released so the parent can read its TEB remotely. */
struct wk;
typedef void (*wk_op)(struct wk *);
struct wk {
    HANDLE th, ev_ready, ev_go;
    wk_op op; int gui; void *arg;
    struct rsnap before, after;
    ULONG_PTR teb;
    DWORD tid;
    unsigned long long x[8];      /* op scratch results */
    volatile LONG done;
};
static DWORD WINAPI wk_main(void *p) {
    struct wk *w = (struct wk *)p;
    w->tid = GetCurrentThreadId();
    w->teb = (ULONG_PTR)k_teb();
    rs_take(&w->before);
    if (w->gui) rt_gui();
    if (w->op) w->op(w);
    rs_take(&w->after);
    SetEvent(w->ev_ready);
    WaitForSingleObject(w->ev_go, 8000);
    w->done = 1;
    return 0;
}
static inline int wk_start(struct wk *w, int gui, wk_op op, void *arg) {
    memset(w, 0, sizeof *w);
    w->gui = gui; w->op = op; w->arg = arg;
    w->ev_ready = CreateEventA(NULL, TRUE, FALSE, NULL);
    w->ev_go = CreateEventA(NULL, TRUE, FALSE, NULL);
    w->th = CreateThread(NULL, 0, wk_main, w, 0, NULL);
    if (!w->th || !w->ev_ready || !w->ev_go) return 0;
    return WaitForSingleObject(w->ev_ready, 8000) == WAIT_OBJECT_0;
}
static inline void wk_finish(struct wk *w) {
    if (w->ev_go) SetEvent(w->ev_go);
    if (w->th) { WaitForSingleObject(w->th, 8000); CloseHandle(w->th); }
    if (w->ev_ready) CloseHandle(w->ev_ready);
    if (w->ev_go) CloseHandle(w->ev_go);
    w->th = w->ev_ready = w->ev_go = NULL;
}

/* run on a plain thread, no parking: returns thread exit code */
typedef DWORD (*rt_fn)(void *);
struct rt_run { rt_fn fn; void *arg; };
static DWORD WINAPI rt_run_main(void *p) { struct rt_run *r = (struct rt_run *)p; return r->fn(r->arg); }
static inline DWORD rt_run_thread(rt_fn fn, void *arg, DWORD ms) {
    struct rt_run *r = (struct rt_run *)HeapAlloc(GetProcessHeap(), 0, sizeof *r);
    if (!r) return 0xDEAD0000;
    r->fn = fn; r->arg = arg;
    HANDLE h = CreateThread(NULL, 0, rt_run_main, r, 0, NULL);
    if (!h) { HeapFree(GetProcessHeap(), 0, r); return 0xDEAD0000; }
    DWORD code = 0xDEAD0001;
    if (WaitForSingleObject(h, ms) == WAIT_OBJECT_0) { GetExitCodeThread(h, &code); HeapFree(GetProcessHeap(), 0, r); }
    CloseHandle(h);
    return code;
}

/* Starts a GUI worker and fails the test through t_skip when the thread cannot be created */
#define WK_GUI(w) struct wk w; if (!wk_start(&w, 1, NULL, NULL)) { wk_finish(&w); t_skip("worker did not start"); return; }
/* like WK_GUI but skips when the new thread was already converted before our op ran (so "zero before" is meaningless) */
#define WK_GUI_PRE(w) WK_GUI(w); if (w.before.ti) { wk_finish(&w); t_skip("thread already converted before its first op"); return; }
#define WK_PLAIN(w) struct wk w; if (!wk_start(&w, 0, NULL, NULL)) { wk_finish(&w); t_skip("worker did not start"); return; }

/* main-thread snapshot after making sure this thread is a fully set-up GUI thread */
static inline void rt_main_gui(struct rsnap *s) { rt_gui(); rs_take(s); }
#endif
