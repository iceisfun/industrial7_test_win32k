/* si.h - shared helpers for the user_serverinfo_shared area.
 *
 * gSharedInfo.psi  is the SERVERINFO page that win32kbase maps read-only into
 * every GUI process. No PDB type exists for it, so every offset below was read from the
 * analysis of the function named next to it (Windows 10 build 19041.2965):
 *   user32!GetSystemMetrics / RealGetSystemMetrics / RealGetSystemMetricsForDpi / GetDpiCacheSlot
 *   user32!GetSysColor / GetSysColorBrush / GetLastInputInfo / GetDPIServerInfo / GetCapture
 *   the system-color setter, the SystemParametersInfo path
 */
#ifndef SI_H
#define SI_H
#include "t.h"
#include "k.h"

#define SI_dwSRVIFlags     0x000  /* ULONG; bit1 DBCS, bit2 IME, bit3 MidEast (RealGetSystemMetrics 0x2a/0x52/0x4a) */
#define SI_cHandleEntries  0x008  /* ULONG64; handle index bound used by IsWindow / GetMonitorInfoW */
#define SI_WinEventMask    0x764  /* ULONG; installed WinEvent categories (IsWinEventHookInstalled / NotifyWinEvent) */
#define SI_aiSysMet        0x768  /* int[]; RealGetSystemMetrics reads the SERVERINFO metrics array */
#define SI_SystemRemarks   0x8bc  /* ULONG; RealGetSystemMetrics 0x2000..0x2006 read bit n of the SERVERINFO remarks word */
#define SI_DpiCache        0x8ec  /* int[18][30]; slot*0x78 + metric*4 (RealGetSystemMetricsForDpi) */
#define SI_DpiCacheSlots   18
#define SI_DpiCacheMetrics 30
#define SI_argbUnmatched   0x115c /* COLORREF[31]; SetSysColor writes SERVERINFO+0x115c+4*i with the requested color */
#define SI_argbSystem      0x11d8 /* COLORREF[31]; GetSysColor reads SERVERINFO+0x11d8+4*i */
#define SI_hbrSystem       0x1258 /* HBRUSH[31]; GetSysColorBrush reads SERVERINFO+0x1258+8*i */
#define SI_hbrGray         0x1350 /* HBRUSH; DrawFrame uses it as brush slot 31 (0xf8 >> 3) */
#define SI_LastRITTick     0x1368 /* ULONG; GetLastInputInfo copies it to dwTime */
#define SI_DpiServerInfo   0x13e8 /* DPISERVERINFO[], stride 0x68, indexed slot+1 (GetDPIServerInfo: (slot+0x31)*0x68) */
#define SI_DpiServerInfo96 0x1450 /* DPISERVERINFO used by DPI-unaware callers (GetDPIServerInfo) */
#define SI_DSI_cxChar      0x20   /* GetDialogBaseUnits: LOWORD = word at +0x20 ... */
#define SI_DSI_cyChar      0x24   /* ... HIWORD = word at +0x24 */
#define SI_CaptureCount    0x1b50 /* ULONG; GetCapture returns NULL when this is 0 */
#define SI_SystemDpi       0x1b56 /* USHORT; GetDpiCacheSlot compares against it (system DPI, cache slot 0) */

#define SI_COLOR_COUNT 31
#define SI_SM_LAST     0x60   /* RealGetSystemMetrics rejects nIndex > 0x60 with ERROR_INVALID_PARAMETER */

/* ---- raw page access: everything goes through the safe reader ---- */
static inline bool si_rd(const char *psi, size_t off, void *out, size_t n) { return k_try_read(psi + off, out, n); }
static inline unsigned long si_u32(const char *psi, size_t off) { unsigned long v = 0; k_try_read_u32(psi + off, &v); return v; }
static inline long si_i32(const char *psi, size_t off) { return (long)(int)si_u32(psi, off); }
static inline unsigned si_u16(const char *psi, size_t off) { unsigned short v = 0; k_try_read_u16(psi + off, &v); return v; }
static inline void *si_ptr(const char *psi, size_t off) { void *v = NULL; k_try_read_ptr(psi + off, &v); return v; }

UDECL(UINT, GetDpiForSystem, (void));
UDECL(int,  GetSystemMetricsForDpi, (int, UINT));
UDECL(BOOL, IsProcessDPIAware, (void));

/* Connects the process to win32k (loads user32 -> NtUserProcessConnect), then returns the SERVERINFO
 * pointer, or NULL after t_skip(). The caller must on NULL. Skipped under Wine, whose
 * win32u/user32 have no SERVERINFO page of this layout. */
static char *si_get(void) {
    if (!U(GetSystemMetrics)) { t_skip("missing export: GetSystemMetrics"); return NULL; }
    (void)U(GetSystemMetrics)(SM_CXSCREEN);
    if (t_is_wine()) { t_skip("Wine has no win32k SERVERINFO page (layout is Windows-only)"); return NULL; }
    struct k_user_shared_info s;
    if (!k_shared_info(&s) || !s.psi) { t_skip("user32!gSharedInfo.psi unavailable"); return NULL; }
    unsigned long probe;
    if (!k_try_read_u32(s.psi, &probe)) { t_skip("SERVERINFO page not readable"); return NULL; }
    return (char *)s.psi;
}

static inline UINT si_sysdpi_user(void) { return UN(GetDpiForSystem) ? UN(GetDpiForSystem)() : 96; }

/* GetDpiCacheSlot (user32): -1 when the DPI has no cache slot. */
static inline int si_cache_slot(unsigned dpi, unsigned page_dpi) {
    if (dpi == (page_dpi & 0xffff)) return 0;
    if (dpi > 0x5f && dpi == (dpi / 0x18) * 0x18) {
        int s = (int)(dpi - 0x48) / 0x18;
        return s > 0x11 ? -1 : s;
    }
    return -1;
}
/* GetDpiDepSysMetCacheMetricSlot (user32): index of the DPI-dependent metric in a cache slot, or -1.
 * 0x5c (CXPADDEDBORDER) is computed through GetWindowFrameMetric... on the GetSystemMetrics path, so it is not listed. */
static inline int si_cache_idx(int m) {
    switch (m) {
    case 2: return 0;   case 3: return 1;   case 4: return 2;   case 9: return 3;   case 10: return 4;
    case 11: return 5;  case 12: return 6;  case 13: return 7;  case 14: return 8;  case 15: return 9;
    case 0x14: return 10; case 0x15: return 11; case 0x1c: return 0x10; case 0x1d: return 0x11;
    case 0x1e: return 0xc; case 0x1f: return 0xd; case 0x20: return 0xe; case 0x21: return 0xf;
    case 0x22: return 0x10; case 0x23: return 0x11; case 0x26: return 0x12; case 0x27: return 0x13;
    case 0x31: return 0x14; case 0x32: return 0x15; case 0x33: return 0x16; case 0x34: return 0x17;
    case 0x35: return 0x18; case 0x36: return 0x19; case 0x37: return 0x1a; case 0x47: return 0x1b;
    case 0x48: return 0x1c;
    }
    return -1;
}
/* RealGetSystemMetricsForDpi cache index (differs from si_cache_idx only for 0x5c). */
static inline int si_cache_idx_fordpi(int m) { return m == 0x5c ? 0x1d : si_cache_idx(m); }

/* Metrics RealGetSystemMetrics computes itself (explicit switch cases / registry) instead of
 * taking them from aiSysMet or the DPI cache. */
static inline bool si_metric_special(int m) {
    switch (m) {
    case 0: case 1: case 0x10: case 0x11: case 0x20: case 0x21: case 0x2a: case 0x3b: case 0x3c:
    case 0x3d: case 0x3e: case 0x4a: case 0x4c: case 0x4d: case 0x4e: case 0x4f: case 0x52:
    case 0x56: case 0x57: case 0x5c: case 0x5e: case 0x5f:
        return true;
    }
    return false;
}

/* The CLIENTINFO dwExpWinVer word (TEB+0x810). RealGetSystemMetrics applies legacy adjustments when it is < 0x400. */
static inline bool si_expver_modern(void) {
    unsigned short v = 0xffff;
    k_try_read_u16((char *)k_teb() + TEB_Win32ClientInfo + 0x10, &v);
    return v >= 0x400;
}

/* ---- USER handle table bits (user32!GetMonitorInfoW, IsWindow) ---- */
struct si_gsi { void *psi, *ahe; unsigned long entsz; void *pDispInfo; ULONG_PTR sharedDelta; };
static inline bool si_gsi(struct si_gsi *g) {
    char *p = (char *)k_shared_info_ptr();
    if (!p) return false;
    memset(g, 0, sizeof *g);
    if (!k_try_read_ptr(p + 0x00, &g->psi) || !k_try_read_ptr(p + 0x08, &g->ahe)) return false;
    k_try_read_u32(p + 0x10, &g->entsz);
    k_try_read_ptr(p + 0x18, &g->pDispInfo);
    k_try_read_ptr(p + 0x20, (void **)&g->sharedDelta);
    return true;
}
#define SI_TYPE_MONITOR 0x0c /* GetMonitorInfoW: handle-entry type byte must be '\f' */
#define SI_MON_rcMonitor 0x1c
#define SI_MON_rcWork    0x2c

/* Client-side address of the MONITOR object behind `h`, validated like GetMonitorInfoW does. */
static inline char *si_monitor_obj(HMONITOR h) {
    struct si_gsi g;
    if (!si_gsi(&g) || !g.ahe) return NULL;
    char *psi = (char *)g.psi;
    unsigned long long n = 0;
    if (!k_try_read(psi + SI_cHandleEntries, &n, 8)) return NULL;
    unsigned idx = (unsigned)((ULONG_PTR)h & 0xffff);
    if (idx >= n) return NULL;
    char *ent = (char *)g.ahe + (ULONG_PTR)idx * (g.entsz ? g.entsz : 0x18);
    void *obj = NULL; unsigned char type = 0, flags = 0; unsigned short uniq = 0;
    if (!k_try_read_ptr(ent, &obj) || !k_try_read_u8(ent + 0x18, &type) || !k_try_read_u8(ent + 0x19, &flags) ||
        !k_try_read_u16(ent + 0x1a, &uniq)) return NULL;
    if (type != SI_TYPE_MONITOR || (flags & 1)) return NULL;
    if (uniq != (unsigned short)((ULONG_PTR)h >> 16) && (unsigned short)((ULONG_PTR)h >> 16) != 0xffff) return NULL;
    return (char *)obj + g.sharedDelta;
}
static inline char *si_primary_monitor_obj(void) {
    struct si_gsi g;
    if (!si_gsi(&g) || !g.pDispInfo) return NULL;
    void *p = NULL;
    if (!k_try_read_ptr((char *)g.pDispInfo + 8, &p) || !p) return NULL;
    return (char *)p + g.sharedDelta;
}

struct si_mon_count { int n; RECT u; };
static BOOL CALLBACK si_enum_cb(HMONITOR m, HDC dc, LPRECT r, LPARAM lp) {
    struct si_mon_count *c = (struct si_mon_count *)lp;
    if (c->n == 0) c->u = *r; else { /* manual union: a static user32 import would connect the process before main() */
        if (r->left < c->u.left) c->u.left = r->left; if (r->top < c->u.top) c->u.top = r->top;
        if (r->right > c->u.right) c->u.right = r->right; if (r->bottom > c->u.bottom) c->u.bottom = r->bottom; }
    c->n++;
    return TRUE;
}
#endif
