/* Shared helpers for the x_gdi_font_charset_embed area: charset resolution per font (GetTextCharset, GetTextCharsetInfo,
 * NtGdiGetCharSet, TEXTMETRIC, LOGFONT, FONTSIGNATURE), the embedded-font counter, EUDC and font-linking switches,
 * the remote/merge font attach calls, PolyTextOutW and direct font creation.
 *
 * Facts measured on Windows 10 22H2 and used here (only relations are asserted; face choice, signatures and
 * code pages that depend on installed fonts or the system locale are never compared with literals):
 *  - GetTextCharsetInfo(dc, fs, flags) returns the charset of the font selected in the DC (DEFAULT_CHARSET, 1, for
 *    anything that is not a usable DC, with ERROR_INVALID_HANDLE and the 24-byte FONTSIGNATURE zeroed). flags is ignored.
 *  - NtGdiGetCharSet(dc) returns (charset << 16) | codepage; for a bad DC it returns DEFAULT_CHARSET << 16 with code page 0.
 *  - NtGdiGetEmbedFonts() counts the private/embedded fonts of the calling process: memory resource fonts one per add,
 *    FR_PRIVATE file fonts one per distinct file (a second add of the same file is reference counted).
 *  - NtGdiEnableEudc returns TRUE for any argument; NtGdiAnyLinkedFonts mirrors gdi32!AnyLinkedFonts.
 *  - NtGdiEngComputeGlyphSet returns NULL for every argument from user mode; NtGdiAddRemoteFontToDC,
 *    NtGdiRemoveMergeFont and NtGdiAddEmbFontToDC return 0 for ordinary user fonts and leave their outputs alone.
 *  - NtGdiPolyTextOutW draws the same pixels as the matching ExtTextOutW calls; count 0 succeeds without looking at
 *    the array or the DC, any other failure to read the array or a negative/oversized string length returns 0.
 */
#ifndef XF_H
#define XF_H
#include "t.h"
#include "k.h"
#include <wingdi.h>

UDECL(int,   NtGdiGetTextCharsetInfo, (HDC, LPFONTSIGNATURE, DWORD));
UDECL(ULONG, NtGdiGetCharSet, (HDC));
UDECL(ULONG, NtGdiGetEmbedFonts, (void));
UDECL(BOOL,  NtGdiAnyLinkedFonts, (void));
UDECL(BOOL,  NtGdiEnableEudc, (BOOL));
UDECL(ULONG, NtGdiFontIsLinked, (HDC));
UDECL(ULONG, NtGdiAddRemoteFontToDC, (HDC, PVOID, ULONG, ULONG64 *));
UDECL(ULONG, NtGdiRemoveMergeFont, (HDC, ULONG64 *));
UDECL(ULONG, NtGdiAddEmbFontToDC, (HDC, ULONG64 *));
UDECL(PVOID, NtGdiEngComputeGlyphSet, (INT, INT, INT));
UDECL(PVOID, EngComputeGlyphSet, (INT, INT, INT));
UDECL(BOOL,  NtGdiPolyTextOutW, (HDC, POLYTEXTW *, UINT, DWORD));
UDECL(HFONT, NtGdiHfontCreate, (void *, ULONG, ULONG, ULONG, PVOID));
UDECL(BOOL,  AnyLinkedFonts, (void));
UDECL(BOOL,  EnableEUDC, (BOOL));

#define XT(n) TEST(x_gdi_font_charset_embed_##n)
#define XF_GUI() do { if (!U(GetDesktopWindow) || !U(CreateCompatibleDC) || !U(CreateFontW)) { t_skip("user32/gdi32 unavailable"); return; } U(GetDesktopWindow)(); } while (0)
#define XF_NEED(p) do { if (!(p)) { t_skip("missing export: " #p); return; } } while (0)
#define XF_NT(n) ((n##_pfn)t_proc_in("win32u.dll", #n))

struct xd { HDC dc; HFONT f; HGDIOBJ old; TEXTMETRICW tm; WCHAR face[64]; };
static inline int xd_finish(struct xd *d) {
    d->old = U(SelectObject)(d->dc, d->f);
    U(GetTextMetricsW)(d->dc, &d->tm); d->face[0] = 0; U(GetTextFaceW)(d->dc, 64, d->face);
    return 1;
}
/* Memory DC with a font made through CreateFontW (route 0) or through NtGdiHfontCreate (route 1). */
static inline int xd_open(struct xd *d, const WCHAR *face, int cs, int route) {
    memset(d, 0, sizeof *d);
    d->dc = U(CreateCompatibleDC)(NULL);
    if (!d->dc) return 0;
    if (route == 0) d->f = U(CreateFontW)(-16, 0, 0, 0, 400, 0, 0, 0, cs, 0, 0, 0, 0, face);
    else {
        struct { LOGFONTW lf; WCHAR full[64], style[32], script[32]; DWORD res, axes; LONG dv[16]; } e; memset(&e, 0, sizeof e);
        e.lf.lfHeight = -16; e.lf.lfWeight = 400; e.lf.lfCharSet = (BYTE)cs;
        for (int i = 0; face[i] && i < 31; i++) e.lf.lfFaceName[i] = face[i];
        d->f = XF_NT(NtGdiHfontCreate) ? XF_NT(NtGdiHfontCreate)(&e, 0x164, 0, 0, NULL) : NULL;
    }
    if (!d->f) { U(DeleteDC)(d->dc); d->dc = NULL; return 0; }
    return xd_finish(d);
}
static inline void xd_close(struct xd *d) {
    if (d->dc && d->old) U(SelectObject)(d->dc, d->old);
    if (d->f) U(DeleteObject)(d->f);
    if (d->dc) U(DeleteDC)(d->dc);
    memset(d, 0, sizeof *d);
}
static inline int xf_wcseq(const WCHAR *a, const WCHAR *b) {
    for (;; a++, b++) { WCHAR x = *a, y = *b; if (x >= 'A' && x <= 'Z') x += 32; if (y >= 'A' && y <= 'Z') y += 32; if (x != y) return 0; if (!x) return 1; }
}
#define XD(d, face, cs, route) struct xd d; if (!xd_open(&d, face, cs, route)) { t_skip("cannot create the font DC"); return; }

/* Handle set used by the wrong-handle matrices. */
struct xh { HDC wd, mdc; HBITMAP bm; HPEN pn; HBRUSH br; HRGN rg; HPALETTE pal; HFONT ft; HDC dead; HANDLE h[20]; };
enum { H_NULL, H_BITMAP, H_PEN, H_BRUSH, H_REGION, H_PALETTE, H_STOCKPEN, H_JUNK, H_MINUS1, H_HWND, H_FONT, H_STOCKFONT, H_HATCH, H_STOCKBRUSH, H_STOCKPAL, H_DEADDC, H_COUNT };
static const char *const xh_name[H_COUNT] = { "null", "bitmap", "pen", "brush", "region", "palette", "stockpen", "junk", "minus1", "hwnd", "font", "stockfont", "hatch", "stockbrush", "stockpal", "deleted-dc" };
static inline int xh_open(struct xh *x) {
    memset(x, 0, sizeof *x);
    if (!U(GetDC) || !U(CreateCompatibleDC) || !U(CreateCompatibleBitmap) || !U(CreatePen) || !U(CreateSolidBrush) || !U(CreateRectRgn) ||
        !U(CreatePalette) || !U(CreateFontW) || !U(GetStockObject) || !U(DeleteObject) || !U(GetDesktopWindow) || !U(CreateHatchBrush)) return 0;
    x->wd = U(GetDC)(NULL); x->mdc = U(CreateCompatibleDC)(NULL);
    x->bm = U(CreateCompatibleBitmap)(x->wd, 4, 4); x->pn = U(CreatePen)(PS_SOLID, 1, 0); x->br = U(CreateSolidBrush)(0x123456);
    x->rg = U(CreateRectRgn)(0, 0, 3, 3);
    LOGPALETTE lp = { 0x300, 1, { { 1, 2, 3, 0 } } }; x->pal = U(CreatePalette)(&lp);
    x->ft = U(CreateFontW)(10, 0, 0, 0, 400, 0, 0, 0, 0, 0, 0, 0, 0, L"Arial");
    x->dead = U(CreateCompatibleDC)(NULL); if (x->dead) U(DeleteDC)(x->dead);
    x->h[H_NULL] = NULL; x->h[H_BITMAP] = x->bm; x->h[H_PEN] = x->pn; x->h[H_BRUSH] = x->br; x->h[H_REGION] = x->rg; x->h[H_PALETTE] = x->pal;
    x->h[H_STOCKPEN] = U(GetStockObject)(BLACK_PEN); x->h[H_JUNK] = (HANDLE)(ULONG_PTR)0x12345678; x->h[H_MINUS1] = (HANDLE)(LONG_PTR)-1;
    x->h[H_HWND] = (HANDLE)U(GetDesktopWindow)(); x->h[H_FONT] = x->ft; x->h[H_STOCKFONT] = U(GetStockObject)(SYSTEM_FONT);
    x->h[H_HATCH] = U(CreateHatchBrush)(HS_CROSS, 0); x->h[H_STOCKBRUSH] = U(GetStockObject)(WHITE_BRUSH); x->h[H_STOCKPAL] = U(GetStockObject)(DEFAULT_PALETTE);
    x->h[H_DEADDC] = x->dead;
    return x->wd && x->mdc && x->bm && x->pn && x->br && x->rg && x->pal && x->ft && x->h[H_HATCH] && x->dead;
}
static inline void xh_close(struct xh *x) {
    if (x->h[H_HATCH]) U(DeleteObject)(x->h[H_HATCH]);
    HGDIOBJ o[] = { x->bm, x->pn, x->br, x->rg, x->pal, x->ft };
    for (unsigned i = 0; i < 6; i++) { if (o[i]) U(DeleteObject)(o[i]); }
    if (x->mdc) U(DeleteDC)(x->mdc);
    if (x->wd) U(ReleaseDC)(NULL, x->wd);
}
#define XH(x) struct xh x; if (!xh_open(&x)) { xh_close(&x); t_skip("cannot create the handle set"); return; }

/* The charsets exercised everywhere (name, value). */
#define XF_CHARSETS(X) \
    X(ansi, 0) X(default, 1) X(symbol, 2) X(mac, 77) X(shiftjis, 128) X(hangul, 129) X(johab, 130) X(gb2312, 134) X(big5, 136) \
    X(greek, 161) X(turkish, 162) X(vietnamese, 163) X(hebrew, 177) X(arabic, 178) X(baltic, 186) X(russian, 204) X(thai, 222) \
    X(easteurope, 238) X(oem, 255)
#endif
