/* Shared helpers for the gdi_font_metrics_text area (kernel font engine through the GDI font syscalls).
 *
 * Facts taken from the Windows 10 19041 analysis:
 *  - gdi32full!CreateFontIndirectExW calls NtGdiHfontCreate(elf, dvNumAxes*4+0x164, 0, 0, LOCALFONT*);
 *    win32kfull!NtGdiHfontCreate fails (returns 0) for a NULL pointer or a size outside 1..0x1a4, zero-extends
 *    the copied-in ENUMLOGFONTEXDVW to 0x1a4 bytes, and win32kfull!hfontCreate rejects dvNumAxes > 16 and stores the
 *    caller's pvCliData in the handle table cell's pUser slot (cell+0x10).
 *  - win32kfull!NtGdiGetTextFaceW: only copies when 0 < cwc and the buffer is non-NULL; NtGdiGetTextMetricsW needs a
 *    buffer of at least 0x44 bytes; NtGdiGetFontData with cb==0 is a pure size query and returns GDI_ERROR on failure;
 *    NtGdiGetGlyphOutline copies through an OUT GLYPHMETRICS (20 bytes) and needs a MAT2 (gdi32full!GetGlyphOutlineInternalW
 *    returns -1 when either gm or mat2 is NULL); NtGdiGetKerningPairs / NtGdiGetFontUnicodeRanges are size queries when
 *    the buffer is NULL.
 *  - NtGdiDeleteObjectApp returns 1 at once for a handle with the stock bit (bit 23) set.
 * Handle layout: bits 0..15 table index, 16..22 object type (font 0x0a), bit 23 stock, 24..31 reuse counter.
 */
#ifndef FM_H
#define FM_H
#include "t.h"
#include "k.h"
#include <wingdi.h>
#include <stdint.h>

#undef EXPECT_EQ
#define EXPECT_EQ(a, b) do { unsigned long long a_ = (unsigned long long)(ULONG_PTR)(a), b_ = (unsigned long long)(ULONG_PTR)(b); \
    t_check(a_ == b_, __FILE__, __LINE__, #a " == " #b, "got 0x%llx want 0x%llx", a_, b_); } while (0)
#undef EXPECT_NE
#define EXPECT_NE(a, b) do { unsigned long long a_ = (unsigned long long)(ULONG_PTR)(a), b_ = (unsigned long long)(ULONG_PTR)(b); \
    t_check(a_ != b_, __FILE__, __LINE__, #a " != " #b, "got 0x%llx", a_); } while (0)

#define FM_TYPE7(h)   ((unsigned)(((ULONG_PTR)(h) >> 16) & 0x7f))
#define FM_STOCK(h)   (((ULONG_PTR)(h) & 0x800000) != 0)
#define FM_INDEX(h)   ((unsigned)((ULONG_PTR)(h) & 0xffff))
#define FM_T7_FONT    0x0a
#ifndef GDI_ERROR
#define GDI_ERROR 0xFFFFFFFFu
#endif

/* ENUMLOGFONTEXDVW with a fixed layout (mingw's header may lack it). */
struct fm_elfdv {
    LOGFONTW lf;          /* 0x000 */
    WCHAR full[64];       /* 0x05c */
    WCHAR style[32];      /* 0x0dc */
    WCHAR script[32];     /* 0x11c */
    DWORD dvReserved;     /* 0x15c */
    DWORD dvNumAxes;      /* 0x160 */
    LONG  dvValues[16];   /* 0x164 */
};

#define FM_FUNCS(X) \
    X(CreateCompatibleDC) X(DeleteDC) X(SelectObject) X(DeleteObject) X(GetObjectW) X(GetObjectType) \
    X(CreateFontIndirectW) X(CreateFontW) X(GetTextFaceW) X(GetTextMetricsW) X(GetCharWidth32W) X(GetCharWidthW) \
    X(GetCharABCWidthsW) X(GetTextExtentPoint32W) X(GetTextExtentPointW) X(GetTextExtentExPointW) X(GetGlyphIndicesW) \
    X(GetOutlineTextMetricsW) X(GetFontData) X(GetFontUnicodeRanges) X(GetKerningPairsW) X(GetCharacterPlacementW) \
    X(GetTextCharset) X(GetTextCharsetInfo) X(GetGlyphOutlineW) X(SetTextJustification) X(SetTextCharacterExtra) \
    X(GetTextCharacterExtra) X(SetTextAlign) X(GetTextAlign) X(SetMapMode) X(GetMapMode) X(GetStockObject) \
    X(GetCurrentObject) X(GetFontLanguageInfo) X(SetViewportExtEx) X(SetWindowExtEx) X(GetDeviceCaps) \
    X(GetCurrentPositionEx) X(MoveToEx) X(TextOutW) X(CreateCompatibleBitmap) X(GetDC) X(ReleaseDC) X(SaveDC) X(RestoreDC) \
    X(GetCharWidthI) X(GetCharABCWidthsI) X(GetTextExtentPointI) X(GetCharWidthFloatW) X(GetCharABCWidthsFloatW) \
    X(SetBkMode) X(CreateDIBSection) X(GetGuiResources) X(GetTextColor) X(SetGraphicsMode) X(SetWorldTransform) \
    X(GetCharABCWidthsA) X(GetTextExtentPoint32A) X(CreateFontA) X(GetTextFaceA) X(GetTextMetricsA) X(ExtTextOutW)

struct fm_api {
#define X(n) __typeof__(&n) n;
    FM_FUNCS(X)
#undef X
};
extern struct fm_api fm;

/* Resolves everything once. Returns the name of the first missing export or NULL. */
const char *fm_init(void);
/* Begin a test: skip when something is missing. */
#define FM_BEGIN() do { const char *m_ = fm_init(); if (m_) { t_skip("missing export: %s", m_); return; } } while (0)

/* Wide-string helpers. */
int fm_wcsieq(const WCHAR *a, const WCHAR *b);
void fm_u8(const WCHAR *w, char *out, size_t cap);   /* for messages */

/* Fresh memory DC (caller frees with fm_freedc). */
HDC fm_dc(void);
void fm_freedc(HDC dc);
/* Creates a font; any field not given is zero. */
HFONT fm_font(const WCHAR *face, int height, int weight, int charset, int quality, int pitchfam);
/* Select font into DC; returns the previous font. */
HFONT fm_sel(HDC dc, HFONT f);
/* Reads the face name the font mapper actually selected for f. */
int fm_face(HDC dc, WCHAR *out, int cap);
/* True when the mapper picks the requested face for a TrueType request at 16 px. */
int fm_face_present(const WCHAR *face);

/* Process-wide handle table cell (24 bytes): einfo@0, owner@8, upper@0xc, objt@0xe, flags@0xf, pUser@0x10. */
struct fm_cell { unsigned long long q0; unsigned owner; unsigned short upper; unsigned char objt, flags; unsigned long long puser; };
int fm_cell_of(HGDIOBJ h, struct fm_cell *c);
/* Live font cell: objt==0x0a and upper matches the handle's high word. */
int fm_cell_live_font(HGDIOBJ h);

/* Direct win32u stubs (declared in fm.c). */
typedef HFONT (WINAPI *fm_NtGdiHfontCreate_pfn)(const void *elf, ULONG cb, ULONG type, ULONG flags, PVOID cli);
typedef int   (WINAPI *fm_NtGdiGetTextFaceW_pfn)(HDC, int cwc, WCHAR *buf, BOOL alias);
typedef int   (WINAPI *fm_NtGdiGetTextMetricsW_pfn)(HDC, void *tm, ULONG cb);
typedef ULONG (WINAPI *fm_NtGdiGetFontData_pfn)(HDC, ULONG tag, ULONG off, void *buf, ULONG cb);
typedef ULONG (WINAPI *fm_NtGdiExtGetObjectW_pfn)(HGDIOBJ, int cb, void *buf);
typedef int   (WINAPI *fm_NtGdiGetTextCharsetInfo_pfn)(HDC, void *fsig, ULONG flags);
typedef int   (WINAPI *fm_NtGdiGetCharSet_pfn)(HDC);
typedef ULONG (WINAPI *fm_NtGdiGetFontUnicodeRanges_pfn)(HDC, void *gs);
typedef ULONG (WINAPI *fm_NtGdiGetKerningPairs_pfn)(HDC, ULONG n, void *pairs);
typedef int   (WINAPI *fm_NtGdiGetGlyphOutline_pfn)(HDC, WCHAR ch, UINT fmt, void *gm, ULONG cb, void *buf, const void *mat2, BOOL ignoreRot);
typedef int   (WINAPI *fm_NtGdiGetOutlineTextMetricsInternalW_pfn)(HDC, ULONG cb, void *potm, void *tmdiff);
typedef HGDIOBJ (WINAPI *fm_NtGdiSelectFont_pfn)(HDC, HFONT);
extern fm_NtGdiHfontCreate_pfn NtGdiHfontCreate_;
extern fm_NtGdiGetTextFaceW_pfn NtGdiGetTextFaceW_;
extern fm_NtGdiGetTextMetricsW_pfn NtGdiGetTextMetricsW_;
extern fm_NtGdiGetFontData_pfn NtGdiGetFontData_;
extern fm_NtGdiExtGetObjectW_pfn NtGdiExtGetObjectW_;
extern fm_NtGdiGetTextCharsetInfo_pfn NtGdiGetTextCharsetInfo_;
extern fm_NtGdiGetCharSet_pfn NtGdiGetCharSet_;
extern fm_NtGdiGetFontUnicodeRanges_pfn NtGdiGetFontUnicodeRanges_;
extern fm_NtGdiGetKerningPairs_pfn NtGdiGetKerningPairs_;
extern fm_NtGdiGetGlyphOutline_pfn NtGdiGetGlyphOutline_;
extern fm_NtGdiGetOutlineTextMetricsInternalW_pfn NtGdiGetOutlineTextMetricsInternalW_;
extern fm_NtGdiSelectFont_pfn NtGdiSelectFont_;
/* Skip the test unless the named direct stub resolved. */
#define FM_NEED(p) do { if (!(p)) { t_skip("missing export: " #p); return; } } while (0)

/* TEXTMETRIC as the kernel returns it (68 bytes, tmCharSet widened to a DWORD at +56). */
struct fm_ktm {
    LONG tmHeight, tmAscent, tmDescent, tmInternalLeading, tmExternalLeading, tmAveCharWidth, tmMaxCharWidth, tmWeight,
         tmOverhang, tmDigitizedAspectX, tmDigitizedAspectY;
    WCHAR tmFirstChar, tmLastChar, tmDefaultChar, tmBreakChar;
    BYTE tmItalic, tmUnderlined, tmStruckOut, tmPitchAndFamily;
    DWORD tmCharSet;
    DWORD pad[2];
};

/* Common sample strings. */
#define FM_STR_A  L"The quick brown fox"
#endif
