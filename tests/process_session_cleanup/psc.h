/* process_session_cleanup helpers: child-process rendezvous, worker threads, GDI cell and USER
 * entry readers, and GetGuiResources sampling.
 *
 * Kernel facts the area relies on (win32kbase/win32kfull 19041 analysis):
 *   - NtUserGetGuiResources(hProcess, flags): flags > 4 -> ERROR_INVALID_PARAMETER; -2 = session
 *     totals; -1 = this process; any other handle needs PROCESS_QUERY_LIMITED_INFORMATION (0x1000)
 *     through ObReferenceObjectByHandle - EVERY failure (no access, wrong type, bad handle, other
 *     session, process without a Win32Process) is reported as ERROR_INVALID_PARAMETER (0x57), never
 *     as ERROR_ACCESS_DENIED. Counters: GDI count and peak, USER count and peak.
 *   - GDI: handle allocation -> process handle-count increment (peak = max) unless the stock bit (flag 8)
 *     is requested; the matching decrement on free. USER: the allocator bumps the per-process counters for
 *     every thread-owned or process-owned object type.
 *   - GDI cell (24 bytes): +0 einfo, +8 owner dword (pid | lock bit), +0xc UpperHandleBits,
 *     +0xe Objt, +0xf Flags, +0x10 pUser.
 */
#ifndef PSC_H
#define PSC_H
#include "t.h"
#include "k.h"
#include <wingdi.h>
#include <winuser.h>
#include <tlhelp32.h>

UDECL(ULONG, NtUserGetGuiResources, (HANDLE, ULONG));
UDECL(ULONG_PTR, NtUserQueryWindow, (HWND, ULONG));

#define PSC_SELF ((HANDLE)(LONG_PTR)-1)
#define PSC_SESSION ((HANDLE)(LONG_PTR)-2)
#define PSC_MAGIC 0x2468u
#define PSC_WAIT 12000

/* ---- shared block between parent and child ----------------------------------------------- */
struct psc_shm {
    volatile LONG fails;
    volatile LONG phase;
    char msg[240];
    volatile LONGLONG v[64];
    char s[4][96];
};

struct psc_ch {
    struct psc_shm *s;
    HANDLE hm, evC, evP, hp, ht;
    DWORD pid, tid;
    unsigned ctr;
};

unsigned psc_ctr_next(void);
static void psc_names(DWORD ppid, unsigned ctr, char *shm, char *c, char *p) {
    sprintf(shm, "Local\\win32kt_psc_%lu_%u_m", (unsigned long)ppid, ctr);
    sprintf(c, "Local\\win32kt_psc_%lu_%u_c", (unsigned long)ppid, ctr);
    sprintf(p, "Local\\win32kt_psc_%lu_%u_p", (unsigned long)ppid, ctr);
}

/* Spawn "--child psc <ppid> <ctr> <scenario> <extra>"; the child shares the block and two
 * auto-reset events (evC child->parent, evP parent->child). */
static int psc_spawn_ex(struct psc_ch *c, const char *scen, const char *extra, DWORD flags) {
    char n_m[96], n_c[96], n_p[96], args[256];
    memset(c, 0, sizeof *c);
    c->ctr = psc_ctr_next();
    psc_names(GetCurrentProcessId(), c->ctr, n_m, n_c, n_p);
    c->hm = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(struct psc_shm), n_m);
    if (!c->hm) return 0;
    c->s = (struct psc_shm *)MapViewOfFile(c->hm, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(struct psc_shm));
    c->evC = CreateEventA(NULL, FALSE, FALSE, n_c);
    c->evP = CreateEventA(NULL, FALSE, FALSE, n_p);
    if (!c->s || !c->evC || !c->evP) return 0;
    memset((void *)c->s, 0, sizeof *c->s);
    snprintf(args, sizeof args, "%lu %u %s \"%s\"", (unsigned long)GetCurrentProcessId(), c->ctr, scen, extra ? extra : "");
    PROCESS_INFORMATION pi;
    c->hp = t_spawn_child("psc", args, flags, NULL, NULL, NULL, FALSE, NULL, &pi);
    if (!c->hp) return 0;
    c->ht = pi.hThread; c->pid = pi.dwProcessId; c->tid = pi.dwThreadId;
    return 1;
}
static int psc_spawn(struct psc_ch *c, const char *scen, const char *extra) { return psc_spawn_ex(c, scen, extra, 0); }

/* Wait for the child's next sync point (or its exit). 1 = at a sync point. */
static int psc_wait(struct psc_ch *c, DWORD ms) {
    HANDLE h[2] = { c->evC, c->hp };
    DWORD r = WaitForMultipleObjects(2, h, FALSE, ms);
    return r == WAIT_OBJECT_0;
}
/* Wait while pumping this thread's messages (needed when the child sends us messages). */
static int psc_wait_pump(struct psc_ch *c, DWORD ms) {
    DWORD t0 = GetTickCount();
    while (GetTickCount() - t0 < ms) {
        if (WaitForSingleObject(c->evC, 0) == WAIT_OBJECT_0) return 1;
        if (WaitForSingleObject(c->hp, 0) == WAIT_OBJECT_0) return 0;
        k_pump(10);
    }
    return 0;
}
static void psc_go(struct psc_ch *c) { SetEvent(c->evP); }
/* Wait for the exit, kill on timeout; returns the exit code and reports the child's own failures. */
static DWORD psc_finish(struct psc_ch *c, DWORD ms) {
    DWORD code = 0xDEAD0001;
    if (WaitForSingleObject(c->hp, ms) != WAIT_OBJECT_0) { TerminateProcess(c->hp, 0xDEAD0001); WaitForSingleObject(c->hp, 5000); }
    else GetExitCodeProcess(c->hp, &code);
    CHECKF(c->s->fails == 0, "child reported %ld failure(s): %s", (long)c->s->fails, c->s->msg);
    return code;
}
static void psc_free(struct psc_ch *c) {
    if (c->hp) { if (WaitForSingleObject(c->hp, 0) != WAIT_OBJECT_0) { TerminateProcess(c->hp, 1); WaitForSingleObject(c->hp, 5000); } CloseHandle(c->hp); }
    if (c->ht) CloseHandle(c->ht);
    if (c->evC) CloseHandle(c->evC);
    if (c->evP) CloseHandle(c->evP);
    if (c->s) UnmapViewOfFile((void *)c->s);
    if (c->hm) CloseHandle(c->hm);
    memset(c, 0, sizeof *c);
}
/* Typical sequence: spawn, wait for the first sync point; on failure skip. */
#define PSC_START(c, scen, extra) do { if (!psc_spawn(&(c), scen, extra)) { t_skip("cannot spawn child %s", scen); psc_free(&(c)); return; } \
    if (!psc_wait(&(c), PSC_WAIT)) { CHECKF(0, "child %s never reached its first sync point (phase %ld: %s)", scen, (c).s ? (long)(c).s->phase : -1L, (c).s ? (c).s->msg : ""); psc_free(&(c)); return; } } while (0)
#define PSC_STEP(c) do { psc_go(&(c)); if (!psc_wait(&(c), PSC_WAIT)) { CHECKF(0, "child never reached its next sync point (%s)", (c).s->msg); psc_free(&(c)); return; } } while (0)
/* last step: let the child run to completion */
#define PSC_END(c) do { psc_go(&(c)); CHECKF(psc_finish(&(c), PSC_WAIT) == 0, "child exit code"); } while (0)

/* ---- child side ----------------------------------------------------------------------------- */
struct psc_cx {
    struct psc_shm *s;
    HANDLE evC, evP;
    DWORD ppid;
    unsigned ctr;
    const char *extra;
};
extern struct psc_cx *psc_x;
typedef void (*psc_scen_fn)(struct psc_cx *x);
void psc_scen_reg(const char *n, psc_scen_fn f);
#define SCEN(n) static void scen_##n(struct psc_cx *x); static void __attribute__((constructor)) regs_##n(void) { psc_scen_reg(#n, scen_##n); } static void scen_##n(struct psc_cx *x)

void psc_cfail(const char *file, int line, const char *expr);
#define CC(c) do { if (!(c)) psc_cfail(__FILE__, __LINE__, #c); } while (0)

/* Child sync point: wake the parent, wait for its go. */
static int psc_sync(struct psc_cx *x) {
    SetEvent(x->evC);
    if (WaitForSingleObject(x->evP, PSC_WAIT) != WAIT_OBJECT_0) { psc_cfail(__FILE__, __LINE__, "parent go timeout"); return 0; }
    return 1;
}
/* Sync while keeping the child's own message queue pumped (parent sends us messages). */
static int psc_sync_pump(struct psc_cx *x) {
    SetEvent(x->evC);
    DWORD t0 = GetTickCount();
    while (GetTickCount() - t0 < PSC_WAIT) {
        if (WaitForSingleObject(x->evP, 0) == WAIT_OBJECT_0) return 1;
        k_pump(10);
    }
    psc_cfail(__FILE__, __LINE__, "parent go timeout (pumping)");
    return 0;
}

/* ---- GetGuiResources sampling ---------------------------------------------------------------- */
static DWORD psc_gr(HANDLE h, DWORD f) { return U(GetGuiResources) ? U(GetGuiResources)(h, f) : 0xFFFFFFFEu; }
static DWORD psc_gr_nt(HANDLE h, DWORD f) { NtUserGetGuiResources_pfn p = UN(NtUserGetGuiResources); return p ? p(h, f) : 0xFFFFFFFEu; }
#define PSC_GDI 0
#define PSC_USER 1
#define PSC_GDI_PEAK 2
#define PSC_USER_PEAK 4

/* ---- GDI shared handle table ----------------------------------------------------------------- */
struct psc_cell { unsigned long long q0; unsigned int owner; unsigned short upper; unsigned char objt, flags; unsigned long long puser; };
static void *psc_gdi_base(void) { void *p = NULL; k_try_read_ptr((char *)k_peb() + PEB_GdiSharedHandleTable, &p); return p; }
static int psc_cell_idx(unsigned idx, struct psc_cell *c) {
    void *b = psc_gdi_base();
    if (!b || idx >= 0x10000) return 0;
    return k_try_read((char *)b + (ULONG_PTR)idx * GDI_CELL_SIZE, c, sizeof *c);
}
/* The cell a handle value designates, with the same low-16-bit indexing the gdi32 getters use. */
static int psc_cell_of(HGDIOBJ h, struct psc_cell *c) { return psc_cell_idx((unsigned)((ULONG_PTR)h & 0xffff), c); }
/* Does the cell currently describe exactly this handle value (reuse counter/upper bits match, live) and who owns it? */
static int psc_cell_live(HGDIOBJ h, unsigned *owner) {
    struct psc_cell c;
    if (!psc_cell_of(h, &c)) return 0;
    if (c.objt == 0 || c.upper != (unsigned short)((ULONG_PTR)h >> 16)) return 0;
    if (owner) *owner = c.owner & ~1u;
    return 1;
}
static int psc_count_owned(unsigned pid) {
    void *b = psc_gdi_base();
    static struct psc_cell buf[1024];
    int n = 0;
    if (!b) return -1;
    for (unsigned i = 0; i < 0x10000; i += 1024) {
        if (!k_try_read((char *)b + (ULONG_PTR)i * GDI_CELL_SIZE, buf, sizeof buf)) return -1;
        for (unsigned j = 0; j < 1024; j++) if (buf[j].objt != 0 && (buf[j].owner & ~1u) == pid) n++;
    }
    return n;
}
/* poll until a predicate holds (bounded) */
#define PSC_POLL(cond, ms) ({ int ok_ = 0; DWORD t0_ = GetTickCount(); do { if (cond) { ok_ = 1; break; } Sleep(10); } while (GetTickCount() - t0_ < (ms)); ok_; })

/* ---- USER side helpers ------------------------------------------------------------------------ */
/* The window is a live window object for this process's view of the shared USER table: user32!IsWindow's
 * own test (type byte 1, deleted flag clear, uniqueness word equals the handle's high word). */
static int psc_user_live(HWND h) {
    struct k_user_entry e;
    if (!k_user_entry(h, &e)) return 0;
    return e.type == K_TYPE_WINDOW && !(e.flags & 1) && (e.uniq == (unsigned short)((ULONG_PTR)h >> 16));
}
/* owner thread id stored at entry+8 (user32!GetWindowThreadProcessId compares it with GetCurrentThreadId) */
static int psc_user_owner_tid(HWND h, ULONG_PTR *tid) {
    struct k_user_shared_info si;
    if (!k_shared_info(&si) || !si.aheList) return 0;
    unsigned long entsz = si.heEntrySize ? si.heEntrySize : USER_HANDLE_SIZE;
    return k_try_read((char *)si.aheList + (ULONG_PTR)((ULONG_PTR)h & 0xffff) * entsz + 8, tid, sizeof *tid);
}

/* Messages understood by the shared window class (all in the WM_APP range, so no system meaning):
 *   +5 ExitThread(0)  +6 TerminateProcess(self, 77)  +7 reply = current thread id  +8 count it
 *   +9 send WM_APP+11 to the window in lParam, reply with its result  +11 terminate psc_kill_h, reply 0x77 */
#define PSC_M_EXITTHREAD (WM_APP + 5)
#define PSC_M_KILLSELF (WM_APP + 6)
#define PSC_M_TID (WM_APP + 7)
#define PSC_M_COUNT (WM_APP + 8)
#define PSC_M_RELAY (WM_APP + 9)
#define PSC_M_KILLPEER (WM_APP + 11)
extern volatile LONG psc_msg_count;
extern HANDLE psc_kill_h;
LRESULT CALLBACK psc_wndproc(HWND h, UINT m, WPARAM w, LPARAM l);
const char *psc_class(void);

/* hidden, off-screen, non-activating popup (or child/owned when parent given) */
/* Headless emulator runs (no DISPLAY) have no display driver, and window creation there can stall a child for
 * seconds; real Windows always has a desktop. */
static int psc_no_display(void) { return t_is_wine() && !getenv("DISPLAY"); }
static HWND psc_mk(HWND parent, const char *title) {
    if (psc_no_display()) return NULL;
    const char *cls = psc_class();
    if (!cls || !U(CreateWindowExA)) return NULL;
    DWORD style = parent ? (WS_CHILD) : WS_POPUP;
    return U(CreateWindowExA)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls, title ? title : "psc", style, -32000, -32000, 8, 8, parent, NULL, GetModuleHandleA(NULL), NULL);
}
static HWND psc_mk_owned(HWND owner, const char *title) {
    if (psc_no_display()) return NULL;
    const char *cls = psc_class();
    if (!cls || !U(CreateWindowExA)) return NULL;
    return U(CreateWindowExA)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls, title ? title : "psc", WS_POPUP, -32000, -32000, 8, 8, owner, NULL, GetModuleHandleA(NULL), NULL);
}

/* ---- worker threads ----------------------------------------------------------------------------
 * The thread runs fn(t); fn calls psc_park(t) when its objects exist: that signals `up` and then waits
 * (pumping messages) for `die`. Returning from fn ends the thread. */
struct psc_th {
    HANDLE h, up, die;
    DWORD tid;
    void (*fn)(struct psc_th *);
    void *user;
    volatile LONG ok;
    volatile ULONGLONG r[16];
    HWND hw[8];
};
static void psc_pump_now(void) {
    MSG m;
    while (U(PeekMessageA)(&m, NULL, 0, 0, PM_REMOVE)) { U(TranslateMessage)(&m); U(DispatchMessageA)(&m); }
}
static void psc_park(struct psc_th *t) {
    SetEvent(t->up);
    DWORD t0 = GetTickCount();
    while (GetTickCount() - t0 < PSC_WAIT) {
        DWORD r = MsgWaitForMultipleObjects(1, &t->die, FALSE, 50, QS_ALLINPUT);
        if (r == WAIT_OBJECT_0) return;
        psc_pump_now();
    }
}
static DWORD WINAPI psc_th_main(LPVOID p) {
    struct psc_th *t = (struct psc_th *)p;
    t->fn(t);
    SetEvent(t->up);     /* in case fn returned without parking */
    return 0;
}
static int psc_th_start(struct psc_th *t, void (*fn)(struct psc_th *), void *user) {
    memset(t, 0, sizeof *t);
    t->fn = fn; t->user = user;
    t->up = CreateEventA(NULL, TRUE, FALSE, NULL);
    t->die = CreateEventA(NULL, TRUE, FALSE, NULL);
    t->h = CreateThread(NULL, 0, psc_th_main, t, 0, &t->tid);
    if (!t->h || !t->up || !t->die) return 0;
    return WaitForSingleObject(t->up, PSC_WAIT) == WAIT_OBJECT_0;
}
/* Let the thread finish and wait for it to exit; returns 1 on a clean join. */
static int psc_th_end(struct psc_th *t) {
    int ok = 1;
    SetEvent(t->die);
    if (t->h) {
        if (WaitForSingleObject(t->h, PSC_WAIT) != WAIT_OBJECT_0) { ok = 0; TerminateThread(t->h, 0); }
        CloseHandle(t->h);
    }
    if (t->up) CloseHandle(t->up);
    if (t->die) CloseHandle(t->die);
    t->h = t->up = t->die = NULL;
    return ok;
}
/* Convert the calling thread's USER state and create a throw-away window so a later baseline is stable. */
static void psc_warm(void) {
    HWND h = psc_mk(NULL, "warm");
    if (h) U(DestroyWindow)(h);
    HBRUSH b = U(CreateSolidBrush)(0);
    if (b) U(DeleteObject)(b);
}
static void psc_warm_thread_fn(struct psc_th *t) { psc_warm(); }
/* A throw-away GUI thread, started and joined, so that process-wide lazy USER state exists. */
static void psc_warm_process(void) {
    struct psc_th t;
    if (psc_th_start(&t, psc_warm_thread_fn, NULL)) psc_th_end(&t);
}


/* ---- GDI object kinds ---------------------------------------------------------------------------
 * Brush/pen/region/bitmap/palette allocate exactly one handle (one handle each); the others are
 * checked for "at least one" and the exact delta is only recorded with t_note. */
enum { PK_BRUSH, PK_PEN, PK_RGN, PK_BITMAP, PK_PALETTE, PK_DC, PK_FONT, PK_DIB, PK_COUNT };
static const char *const psc_kind_name[PK_COUNT] = { "brush", "pen", "rgn", "bitmap", "palette", "dc", "font", "dib" };
static const int psc_kind_exact[PK_COUNT] = { 1, 1, 1, 1, 1, 0, 0, 0 };
static HGDIOBJ psc_mk_kind(int k) {
    switch (k) {
    case PK_BRUSH: return U(CreateSolidBrush)(RGB(1, 2, 3));
    case PK_PEN: return U(CreatePen)(PS_SOLID, 1, RGB(4, 5, 6));
    case PK_RGN: return U(CreateRectRgn)(0, 0, 4, 4);
    case PK_BITMAP: return U(CreateBitmap)(2, 2, 1, 1, NULL);
    case PK_PALETTE: {
        LOGPALETTE lp; memset(&lp, 0, sizeof lp);
        lp.palVersion = 0x300; lp.palNumEntries = 1; lp.palPalEntry[0].peRed = 9;
        return U(CreatePalette)(&lp);
    }
    case PK_DC: return U(CreateCompatibleDC)(NULL);
    case PK_FONT: { LOGFONTA lf; memset(&lf, 0, sizeof lf); lf.lfHeight = -12; strcpy(lf.lfFaceName, "Arial"); return U(CreateFontIndirectA)(&lf); }
    case PK_DIB: {
        BITMAPINFO bi; void *bits = NULL; memset(&bi, 0, sizeof bi);
        bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = 4; bi.bmiHeader.biHeight = 4;
        bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
        return U(CreateDIBSection)(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    }
    }
    return NULL;
}
static BOOL psc_del_kind(int k, HGDIOBJ h) { return k == PK_DC ? U(DeleteDC)((HDC)h) : U(DeleteObject)(h); }

/* Windows 10 recycles deleted solid brushes and rectangular regions through a small cache that keeps their handles
 * counted (9 brushes and 7 regions were observed): exact count deltas are only visible once both caches are empty.
 * psc_drain_take() makes every later create of those kinds a fresh handle; psc_drain_give() hands the objects back.
 * After deleting a batch the count can stay above its starting value by at most PSC_CACHE_MAX. */
#define PSC_CACHE_PROBE 24
#define PSC_CACHE_MAX 16
struct psc_drain { HGDIOBJ b[PSC_CACHE_PROBE], r[PSC_CACHE_PROBE]; int nb, nr; };
static void psc_drain_take(struct psc_drain *d) {
    memset(d, 0, sizeof *d);
    for (; d->nb < PSC_CACHE_PROBE; d->nb++) if (!(d->b[d->nb] = U(CreateSolidBrush)(RGB(200 + d->nb, 7, 9)))) break;
    for (; d->nr < PSC_CACHE_PROBE; d->nr++) if (!(d->r[d->nr] = U(CreateRectRgn)(0, 0, 3 + d->nr, 3))) break;
}
static void psc_drain_give(struct psc_drain *d) {
    for (int i = 0; i < d->nb; i++) U(DeleteObject)(d->b[i]);
    for (int i = 0; i < d->nr; i++) U(DeleteObject)(d->r[i]);
    d->nb = d->nr = 0;
}
static int psc_kind_cached(int k) { return k == PK_BRUSH || k == PK_RGN; }

static int psc_is_wine(void) { return t_is_wine(); }
#endif
