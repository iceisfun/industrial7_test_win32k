/* gdi_d3dkmt_dd: graphics kernel thunks reachable through win32u (NtGdiDdDDI*), the user-mode D3DKMT
 * wrappers living in gdi32, and the legacy DirectDraw entry points.
 *
 * Facts this area relies on (all from the 19041 analysis):
 *  - every NtGdiDdDDI* thunk takes ONE argument, a pointer to the D3DKMT_* argument struct
 *    (wow64win!whNtGdiDdDDI* marshal exactly one pointer); the implementation lives in dxgkrnl, win32k.sys
 *    only imports it (__win32kstub_NtGdiDdDDI* -> __imp_NtGdiDdDDI*), so the dxgkrnl-side status codes are
 *    only hard-checked where a user-mode gdi32 wrapper (analysed) produces them without the kernel.
 *  - gdi32 forwards 208 D3DKMT* exports to win32u; 16 are real gdi32 functions (dk_local_tab).
 *  - gdi32full exports 94 names (NtGdiDd*, NtGdiDvp*, NtGdiD3d*, GdiEntryN, ...) that all alias one
 *    "xor eax,eax; ret" body at; gdi32!DdEntry0..56 are "jmp [iat]" thunks to the first 57 of them.
 *  - win32kfull!NtGdiDdQueryVisRgnUniqueness returns the DWORD global giVisRgnUniqueness (no arguments).
 *  - win32kfull!SpDdCreate/Destroy/NotifyFullscreenSprite return STATUS_INVALID_PARAMETER (0xC000000D)
 *    when XDCOBJ::vLock(hdc) fails.
 */
#ifndef D_H
#define D_H
#include "t.h"
#include "k.h"
#include <wingdi.h>

#define ST_SUCCESS            ((LONG)0x00000000)
#define ST_UNSUCCESSFUL       ((LONG)0xC0000001)
#define ST_INVALID_PARAMETER  ((LONG)0xC000000D)
#define ST_BUFFER_TOO_SMALL   ((LONG)0xC0000023)
#define ST_PROC_NOT_FOUND     ((LONG)0xC000007A)
#define ST_NOT_SUPPORTED      ((LONG)0xC00000BB)
#define NTOK(s)               ((LONG)(s) >= 0)

struct dk_fwd { const char *name; const char *target; int num; };
struct dk_num { const char *name; int num; };
extern const struct dk_fwd dk_fwd_tab[]; extern const int dk_fwd_cnt;
extern const char *const dk_local_tab[]; extern const int dk_local_cnt;
extern const char *const dk_legacy_tab[]; extern const int dk_legacy_cnt;
extern const char *const dk_ddentry_tab[]; extern const int dk_ddentry_cnt;
extern const char *const dk_wine_ok_tab[]; extern const int dk_wine_ok_cnt;
extern const struct dk_num dk_num_tab[]; extern const int dk_num_cnt;

/* D3DKMT argument structs (x64 layouts of d3dkmthk.h; verified sizes via the wow64 thunks where noted). */
typedef struct { ULONG hAdapter; LUID luid; ULONG NumOfSources; BOOL bPresentMoveRegionsPreferred; } dk_adapterinfo; /* 20 */
typedef struct { ULONG NumAdapters; dk_adapterinfo *pAdapters; } dk_enum2;                /* 16, wow64 whNtGdiDdDDIEnumAdapters2 */
typedef struct { HDC hDc; ULONG hAdapter; LUID luid; ULONG VidPnSourceId; } dk_openhdc;   /* 0x18, whNtGdiDdDDIOpenAdapterFromHdc */
typedef struct { ULONG hAdapter; } dk_closeargs;
typedef struct { ULONG hAdapter; ULONG Type; void *pData; ULONG Size; } dk_qai;            /* 24, whNtGdiDdDDIQueryAdapterInfo */
typedef struct { LUID luid; ULONG hAdapter; } dk_openluid;
typedef struct { WCHAR Name[32]; ULONG hAdapter; LUID luid; ULONG VidPnSourceId; } dk_opengdiname; /* 0x50: out fields at 0x40/0x44/0x4c per gdi32!D3DKMTOpenAdapterFromGdiDisplayName */

enum { QAI_UMDRIVERPRIVATE = 0, QAI_UMDRIVERNAME = 1, QAI_UMOPENGLINFO = 2, QAI_GETSEGMENTSIZE = 3,
       QAI_ADAPTERADDRESS = 6, QAI_DRIVERVERSION = 13, QAI_ADAPTERTYPE = 15, QAI_BOGUS = 0x7fffffff };

typedef LONG (WINAPI *dk_fn1)(void *);

/* Resolve a win32u syscall stub (the module under test); NULL if missing. */
dk_fn1 dk_nt(const char *name);
#define DKREQ(var, name) dk_fn1 var = dk_nt(name); if (!var) { t_skip("missing export: " name); return; }

/* Opens the primary display adapter through the screen DC. Returns false (and notes) if unavailable. */
int dk_open_screen(dk_openhdc *o, HDC *hdc_out);
void dk_close_ok(ULONG h);   /* closes, CHECKs success */
LONG dk_close(ULONG h);      /* closes via the win32u stub; ST_UNSUCCESSFUL-ish sentinel 0x7FFFFFFF if no export */
/* Enumerate: fills *list (caller frees with free) and returns count; -1 on failure. */
int dk_enum(dk_adapterinfo **list);
void dk_enum_close(dk_adapterinfo *a, int n); /* closes every handle then frees */
int dk_luid_eq(LUID a, LUID b);
int dk_luid_zero(LUID a);
const char *dk_stn(LONG st);
#define NOTE_ST(what, st) t_note("%s = 0x%08lx (%s)", what, (unsigned long)(st), dk_stn(st))
unsigned dk_gui_count(unsigned which); /* GetGuiResources(self, which) */
#endif
