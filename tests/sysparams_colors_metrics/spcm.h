/* spcm.h - shared helpers for the sysparams_colors_metrics area.
 *
 * Kernel entry points exercised (Windows 10 19041, win32u.dll stubs, numbers from tools/win32u_syscalls.tsv):
 *   NtUserSystemParametersInfo(action, uiParam, pvParam, fWinIni)               NtUserSystemParametersInfo
 *      
 *   NtUserSystemParametersInfoForDpi(action, uiParam, pvParam, fWinIni, dpi)    NtUserSystemParametersInfoForDpi
 *   NtUserSetSysColors(cElements, aElements, aRgb, flags)                       NtUserSetSysColors
 *       user32!SetSysColors passes flags 7: flags 1 = broadcast WM_SYSCOLORCHANGE + repaint,
 *       2 and 4 = palette/magic-color handling. flags 0 changes SERVERINFO silently, which the dx_ area uses.
 *   NtUserGetDoubleClickTime() / NtUserGetCaretBlinkTime() (no arguments)
 *   NtUserCallOneParam(param, 0x43 = SetCaretBlinkTime / 0x44 = SetDoubleClickTime)
 *
 * SERVERINFO (user32!gSharedInfo.psi) offsets used here, each read off the analysis named next to it:
 *   aiSysMet[i]        psi+0x768+4*i   (user32!RealGetSystemMetrics)
 *   DblClk*4           psi+0x1374      (stores param<<2; +0x1370 = that / 5)
 *   CaretBlinkTime     psi+0x1378      (the kernel handler store, NtUserGetCaretBlinkTime load)
 *   WheelScrollLines   psi+0x137c      (SPI 0x68 get / 0x69 set)
 *   argbUnmatched[31]  psi+0x115c, argbSystem[31] psi+0x11d8, hbrSystem[31] psi+0x1258 
 *   SystemDpi (USHORT) psi+0x1b56
 */
#ifndef SPCM_H
#define SPCM_H
#include "t.h"
#include "k.h"

UDECL(BOOL,  NtUserSystemParametersInfo,       (UINT action, UINT uiParam, PVOID pv, UINT fWinIni));
UDECL(BOOL,  NtUserSystemParametersInfoForDpi, (UINT action, UINT uiParam, PVOID pv, UINT fWinIni, UINT dpi));
UDECL(BOOL,  NtUserSetSysColors,               (UINT c, const INT *idx, const COLORREF *rgb, UINT flags));
UDECL(ULONG_PTR, NtUserCallOneParam,           (ULONG_PTR param, ULONG code));
UDECL(UINT,  NtUserGetDoubleClickTime,         (void));
UDECL(UINT,  NtUserGetCaretBlinkTime,          (void));
UDECL(UINT,  GetDpiForSystem,                  (void));
UDECL(int,   GetSystemMetricsForDpi,           (int, UINT));

#define PG_aiSysMet   0x768
#define PG_Dbl4       0x1374
#define PG_Caret      0x1378
#define PG_Wheel      0x137c
#define PG_argbUnm    0x115c
#define PG_argbSys    0x11d8
#define PG_hbrSys     0x1258
#define PG_SysDpi     0x1b56
#define SPCM_NCOLORS  31

#define SPCM_ERR_SENTINEL 0xDEAD0057u

/* The BOOL result of a direct stub call; sets a sentinel last error first so "error untouched" is observable. */
static inline int spi(UINT a, UINT u, void *pv, UINT f) {
    NtUserSystemParametersInfo_pfn fn = UN(NtUserSystemParametersInfo);   /* resolve first: the lookup itself may set a last error */
    SetLastError(SPCM_ERR_SENTINEL);
    return (int)fn(a, u, pv, f);
}
static inline int spi_dpi(UINT a, UINT u, void *pv, UINT dpi) {
    NtUserSystemParametersInfoForDpi_pfn fn = UN(NtUserSystemParametersInfoForDpi);
    SetLastError(SPCM_ERR_SENTINEL);
    return (int)fn(a, u, pv, 0, dpi);
}
#define NEED_SPI()    REQUIRE(UN(NtUserSystemParametersInfo))
#define NEED_SPIDPI() REQUIRE(UN(NtUserSystemParametersInfoForDpi))
#define NEED_SYSCOL() REQUIRE(UN(NtUserSetSysColors))

/* 1 KiB guarded scratch buffer: 64 canary bytes on both sides, body prefilled with 0xA5. */
typedef struct __attribute__((aligned(16))) { unsigned char pre[64]; unsigned char b[1024]; unsigned char post[64]; } spcm_buf;
static inline void gb_init(spcm_buf *g) { memset(g, 0xA5, sizeof *g); }
/* true when bytes [from, from+n) of the body are still the 0xA5 fill */
static inline int gb_pristine(const spcm_buf *g, size_t from, size_t n) {
    for (size_t i = 0; i < n; i++) if (g->b[from + i] != 0xA5) return 0;
    return 1;
}
static inline int gb_guards_ok(const spcm_buf *g) {
    for (int i = 0; i < 64; i++) if (g->pre[i] != 0xA5 || g->post[i] != 0xA5) return 0;
    return gb_pristine(g, 0, 1024) ? 1 : 1;
}
static inline int gb_edges_ok(const spcm_buf *g) { /* the 128 guard bytes only */
    for (int i = 0; i < 64; i++) if (g->pre[i] != 0xA5 || g->post[i] != 0xA5) return 0;
    return 1;
}
static inline unsigned gb_u32(const spcm_buf *g, size_t off) { unsigned v; memcpy(&v, g->b + off, 4); return v; }

/* SERVERINFO page (Windows only: Wine's win32u has no such page). Loads user32 on first use. NULL after t_skip(). */
static inline char *spcm_page(void) {
    if (!U(GetSystemMetrics)) { t_skip("missing export: GetSystemMetrics"); return NULL; }
    (void)U(GetSystemMetrics)(SM_CXSCREEN);
    if (t_is_wine()) { t_skip("Wine has no win32k SERVERINFO page"); return NULL; }
    struct k_user_shared_info s;
    if (!k_shared_info(&s) || !s.psi) { t_skip("user32!gSharedInfo.psi unavailable"); return NULL; }
    unsigned long probe;
    if (!k_try_read_u32(s.psi, &probe)) { t_skip("SERVERINFO page not readable"); return NULL; }
    return (char *)s.psi;
}
static inline unsigned long pg_u32(const char *psi, size_t off) { unsigned long v = 0; k_try_read_u32(psi + off, &v); return v; }
static inline unsigned pg_u16(const char *psi, size_t off) { unsigned short v = 0; k_try_read_u16(psi + off, &v); return v; }
static inline void *pg_ptr(const char *psi, size_t off) { void *v = NULL; k_try_read_ptr(psi + off, &v); return v; }
static inline int pg_sm(const char *psi, int idx) { return (int)pg_u32(psi, PG_aiSysMet + 4 * (size_t)idx); }

/* True-color display: SetSysColor's nearest-color step is then the identity (flag 8 is set whenever
 * GetDeviceCaps(NUMCOLORS) < 0x11, which includes the -1 reported by deep-color devices). */
static inline int spcm_truecolor(void) {
    HDC dc = U(GetDC)(NULL);
    if (!dc) return 0;
    int bpp = U(GetDeviceCaps)(dc, BITSPIXEL) * U(GetDeviceCaps)(dc, PLANES);
    U(ReleaseDC)(NULL, dc);
    return bpp >= 24;
}
/* The system DPI as the kernel sees it: the SERVERINFO USHORT when readable, else GetDpiForSystem. */
static inline unsigned spcm_sysdpi(const char *psi) {
    unsigned d = psi ? pg_u16(psi, PG_SysDpi) : 0;
    if (!d && UN(GetDpiForSystem)) d = UN(GetDpiForSystem)();
    return d ? d : 96;
}

/* ---- message observer window: counts the broadcasts the kernel makes ------------------------------ */
struct spcm_obs { HWND hwnd; volatile LONG syscolor, setting; volatile WPARAM last_setting_wparam; volatile LPARAM last_setting_lparam; };
extern struct spcm_obs g_obs;
int  spcm_obs_create(void);     /* 0 on failure (caller t_skip()s) */
void spcm_obs_destroy(void);
void spcm_obs_reset(void);

/* ---- layout of the structures the kernel fills (x64) ---------------------------------------------- */
#define NCM_cbSize        0x00
#define NCM_iBorderWidth  0x04
#define NCM_iScrollWidth  0x08
#define NCM_iScrollHeight 0x0c
#define NCM_iCaptionWidth 0x10
#define NCM_iCaptionHeight 0x14
#define NCM_lfCaptionFont 0x18
#define NCM_iSmCaptionWidth 0x74
#define NCM_iSmCaptionHeight 0x78
#define NCM_lfSmCaptionFont 0x7c
#define NCM_iMenuWidth    0xd8
#define NCM_iMenuHeight   0xdc
#define NCM_lfMenuFont    0xe0
#define NCM_lfStatusFont  0x13c
#define NCM_lfMessageFont 0x198
#define NCM_iPaddedBorderWidth 0x1f4
#define NCM_SIZE          0x1f8   /* 504; the pre-Vista layout is 500 (no iPaddedBorderWidth) */
#define NCM_SIZE_OLD      0x1f4
#define ICM_SIZE          0x6c
#define LOGFONTW_SIZE     0x5c
#endif
