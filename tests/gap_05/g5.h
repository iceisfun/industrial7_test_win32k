/* gap_05: direct win32u syscall tests for entry points no other area reaches.
 * Signatures are derived from the win32kfull/win32kbase analysis (parameter use, probes) and from the
 * real callers in user32/gdi32/gdi32full; each test cites the branch it relies on. Return types are
 * the width the kernel function really returns in rax (int/uint -> ULONG) so garbage in the upper
 * half of rax can never leak into a CHECK. */
#ifndef G5_H
#define G5_H
#include "t.h"
#include "k.h"

#define G5_LONG(x) ((LONG)(x))

UDECL(ULONG,     NtUserSoundSentry, (void));
UDECL(HWND,      NtUserWindowFromPhysicalPoint, (LONG64 pt));
UDECL(HWND,      NtUserWindowFromPoint, (LONG64 pt));
UDECL(ULONG,     NtUserGetActiveProcessesDpis, (void));
UDECL(ULONG,     NtUserHwndQueryRedirectionInfo, (HWND, int type, void *out, ULONG *inoutSize));
UDECL(ULONG,     NtUserMagGetContextInformation, (HWND, int type, void *out, ULONG *inoutSize));
UDECL(ULONG,     NtUserMagSetContextInformation, (HWND, int type, void *data, ULONG size));

UDECL(ULONG,     NtGdiAddEmbFontToDC, (HDC, ULONG64 *pFontId));
UDECL(ULONG,     NtGdiAddRemoteFontToDC, (HDC, PVOID buf, ULONG cb, ULONG64 *pUfi));
UDECL(ULONG,     NtGdiRemoveMergeFont, (HDC, ULONG64 *pUfi));
UDECL(ULONG_PTR, NtGdiSetBitmapAttributes, (HBITMAP, DWORD));
UDECL(ULONG_PTR, NtGdiClearBitmapAttributes, (HBITMAP, DWORD));
UDECL(ULONG_PTR, NtGdiSetBrushAttributes, (HBRUSH, DWORD));
UDECL(ULONG_PTR, NtGdiClearBrushAttributes, (HBRUSH, DWORD));
UDECL(ULONG,     NtGdiEudcLoadUnloadLink, (const WCHAR *base, ULONG cwcBase, const WCHAR *path, ULONG cwcPath, ULONG prio, ULONG type, BOOL load));
UDECL(ULONG,     NtGdiGetEudcTimeStampEx, (WCHAR *name, ULONG cwc, BOOL system));
UDECL(ULONG,     NtGdiEnableEudc, (BOOL));
UDECL(ULONG,     NtGdiFontIsLinked, (HDC));
UDECL(ULONG,     NtGdiGetCharWidthInfo, (HDC, void *out12));
UDECL(LONG,      NtGdiGetCurrentDpiInfo, (HMONITOR, void *out0x60));
UDECL(ULONG,     NtGdiGetStringBitmapW, (HDC, const WCHAR *pwc, ULONG cwc, ULONG cb, void *buf));
UDECL(ULONG,     NtGdiGetFontFileData, (ULONG id, ULONG idx, ULONG64 *pOff, void *buf, SIZE_T cb));
UDECL(ULONG,     NtGdiGetFontFileInfo, (ULONG id, ULONG idx, void *info, SIZE_T cb, SIZE_T *pNeeded));
UDECL(LONG,      NtDCompositionDestroyChannel, (ULONG id));
UDECL(ULONG,     RtlNtStatusToDosError, (LONG));

/* user-mode wrappers */
UDECL(ULONG,     SoundSentry, (void));
UDECL(ULONG_PTR, SetBrushAttributes, (HBRUSH, DWORD));
UDECL(ULONG_PTR, ClearBrushAttributes, (HBRUSH, DWORD));
UDECL(ULONG_PTR, SetBitmapAttributes, (HBITMAP, DWORD));
UDECL(ULONG_PTR, ClearBitmapAttributes, (HBITMAP, DWORD));
UDECL(ULONG,     GetEUDCTimeStamp, (void));
UDECL(ULONG,     GetEUDCTimeStampExW, (const WCHAR *));
UDECL(ULONG,     FontIsLinked, (HDC));
UDECL(ULONG,     GetCharWidthInfo, (HDC, void *));
UDECL(ULONG,     GetCurrentDpiInfo, (HMONITOR, void *));
UDECL(ULONG,     GetFontRealizationInfo, (HDC, void *));
UDECL(ULONG,     GetFontFileData, (ULONG, ULONG, ULONG64, void *, SIZE_T));
UDECL(ULONG,     GetStringBitmapA, (HDC, const char *, ULONG cb, ULONG cbBuf, void *));

/* Wine's win32u exports every one of these names but most are stubs that abort the process when called
 * ("unimplemented function win32u.dll.X"). Only the two it implements with the Windows convention resolve under Wine (its
 * GetFontFileInfo/Data return BOOL instead of an error code); the others come back NULL so the test skips cleanly. */
static inline FARPROC g5_nt(const char *name) {
    if (t_is_wine() &&
        strcmp(name, "NtGdiFontIsLinked") && strcmp(name, "NtGdiGetCharWidthInfo"))
        return NULL;
    return t_proc_in("win32u.dll", name);
}
#define G5_NT(n) n##_pfn n##_ = (n##_pfn)g5_nt(#n)
#define G5_NEED(fn) do { if (!(fn)) { t_skip("missing export: " #fn); return; } } while (0)
/* Wine's win32u implements its own, differently shaped Nt* entry points for a few of these. */
#define G5_NOWINE(why) do { if (t_is_wine()) { t_skip("Wine: " why); return; } } while (0)

/* Load user32/gdi32 (connects the process to win32k) - tests that only use win32u stubs still want a
 * connected GUI process so the kernel side behaves like it does for real applications. */
static inline int g5_gui(void) {
    if (!U(GetDesktopWindow)) return 0;
    U(GetDesktopWindow)();
    return 1;
}
#define G5_GUI() do { if (!g5_gui()) { t_skip("user32 unavailable"); return; } } while (0)

static inline ULONG g5_dos(LONG st) {
    RtlNtStatusToDosError_pfn f = (RtlNtStatusToDosError_pfn)t_proc_in("ntdll.dll", "RtlNtStatusToDosError");
    return f ? f(st) : 0xFFFFFFFFu;
}
/* TEB.LastStatusValue (x64 +0x1250) is written by UserSetLastStatus. */
static inline ULONG g5_laststatus(void) {
    unsigned long v = 0;
    if (!k_try_read_u32((char *)k_teb() + 0x1250, &v)) return 0xFFFFFFFFu;
    return (ULONG)v;
}
static inline void g5_clear_status(void) {
    SetLastError(0x5A5A5A5A);
    *(ULONG *)((char *)k_teb() + 0x1250) = 0x5A5A5A5Au;
}

/* A font-selected memory DC. */
typedef struct { HDC dc; HFONT font, old; } g5_fdc;
static inline int g5_fdc_open(g5_fdc *d, const WCHAR *face) {
    memset(d, 0, sizeof *d);
    if (!U(CreateCompatibleDC) || !U(CreateFontW) || !U(SelectObject)) return 0;
    d->dc = U(CreateCompatibleDC)(NULL);
    if (!d->dc) return 0;
    d->font = U(CreateFontW)(-24, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_ONLY_PRECIS,
                             CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, face);
    if (!d->font) return 0;
    d->old = (HFONT)U(SelectObject)(d->dc, d->font);
    return 1;
}
static inline void g5_fdc_close(g5_fdc *d) {
    if (d->dc && d->old) U(SelectObject)(d->dc, d->old);
    if (d->font) U(DeleteObject)(d->font);
    if (d->dc) U(DeleteDC)(d->dc);
    memset(d, 0, sizeof *d);
}

/* Handles that are not a live HDC/HBITMAP/HBRUSH of the right type. */
typedef struct {
    HGDIOBJ h[16];
    const char *what[16];
    int n;
    HPEN pen; HBRUSH brush; HBITMAP bmp; HDC dead;
} g5_bad;
static inline void g5_bad_open(g5_bad *b) {
    memset(b, 0, sizeof *b);
    b->pen = U(CreatePen) ? U(CreatePen)(PS_SOLID, 1, 0) : NULL;
    b->brush = U(CreateSolidBrush) ? U(CreateSolidBrush)(RGB(1, 2, 3)) : NULL;
    b->bmp = U(CreateBitmap) ? U(CreateBitmap)(2, 2, 1, 1, NULL) : NULL;
    b->dead = U(CreateCompatibleDC) ? U(CreateCompatibleDC)(NULL) : NULL;
    if (b->dead) { U(DeleteDC)(b->dead); }
#define ADD(v, w) do { b->h[b->n] = (HGDIOBJ)(ULONG_PTR)(v); b->what[b->n++] = w; } while (0)
    ADD(0, "NULL");
    ADD(1, "1");
    ADD(0xFFFFFFFFu, "0xFFFFFFFF");
    ADD(0x7FFFFFFFu, "0x7FFFFFFF");
    ADD(0x00050123u, "unallocated index");
    if (b->pen) { ADD(b->pen, "pen (wrong type)"); ADD((ULONG_PTR)b->pen ^ 0x10000, "pen, stale uniq"); }
    if (b->brush) ADD(b->brush, "brush (wrong type)");
    if (b->bmp) ADD(b->bmp, "bitmap (wrong type)");
    if (b->dead) ADD(b->dead, "deleted DC");
#undef ADD
}
static inline void g5_bad_close(g5_bad *b) {
    if (b->pen) U(DeleteObject)(b->pen);
    if (b->brush) U(DeleteObject)(b->brush);
    if (b->bmp) U(DeleteObject)(b->bmp);
}
#endif
