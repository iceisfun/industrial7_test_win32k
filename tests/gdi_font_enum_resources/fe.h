/* Shared helpers for the gdi_font_enum_resources area: font enumeration (NtGdiEnumFonts) and font resources
 * (NtGdiAddFontResourceW, NtGdiRemoveFontResourceW, NtGdiAddFontMemResourceEx, NtGdiRemoveFontMemResourceEx,
 * NtGdiGetFontResourceInfoInternalW, NtGdiGetPublicFontTableChangeCookie, NtGdiGetFontUnicodeRanges).
 *
 * Signatures are taken from win32kfull (NtGdiEnumFonts / EnumEngineOnly, NtGdiAddFontResourceW /
 * path length capping, NtGdiRemoveFontResourceW, NtGdiAddFontMemResourceEx,
 * NtGdiGetFontResourceInfoInternalW / GetFontResourceInfoInternalW) and from the gdi32full callers
 * (bEnumFonts, EnumFontsInternalW, GdiAddFontResourceW, AddFontResourceExW, RemoveFontResourceExW,
 * AddFontMemResourceEx, GetFontResourceInfoW).
 *
 * NtGdiEnumFonts(hdc, iEnumType, flags, cwchFace, pwszFace, ulCharSet, ULONG *pcjBuf, void *pBuf) fills pBuf with
 * a chain of ENUMFONTDATA records: { ULONG ulSize (stride); ULONG cjEfdw (offset of the NTM minus 8); ULONG fontType;
 * ENUMLOGFONTEXDVW elf at +12 }, the NEWTEXTMETRICEXW living at record+8+cjEfdw (EnumFontsInternalW hands the
 * callback record+12, record+8+cjEfdw and the fontType word). pBuf == NULL asks for the size.
 */
#ifndef FE_H
#define FE_H
#include "t.h"
#include "k.h"
#include <wingdi.h>

/* Single-evaluation EXPECT_EQ/NE: many checks here call the function under test inside the macro argument. */
#undef EXPECT_EQ
#define EXPECT_EQ(a, b) do { unsigned long long a_ = (unsigned long long)(ULONG_PTR)(a), b_ = (unsigned long long)(ULONG_PTR)(b); \
    t_check(a_ == b_, __FILE__, __LINE__, #a " == " #b, "got 0x%llx want 0x%llx", a_, b_); } while (0)
#undef EXPECT_NE
#define EXPECT_NE(a, b) do { unsigned long long a_ = (unsigned long long)(ULONG_PTR)(a), b_ = (unsigned long long)(ULONG_PTR)(b); \
    t_check(a_ != b_, __FILE__, __LINE__, #a " != " #b, "got 0x%llx", a_); } while (0)

UDECL(ULONG, NtGdiEnumFonts, (HDC, ULONG, ULONG, ULONG, const WCHAR *, ULONG, ULONG *, void *));
UDECL(int,   NtGdiAddFontResourceW, (const WCHAR *, ULONG, ULONG, ULONG, ULONG, void *));
UDECL(ULONG, NtGdiRemoveFontResourceW, (const WCHAR *, ULONG, ULONG, ULONG, ULONG, void *));
UDECL(void*, NtGdiAddFontMemResourceEx, (void *, ULONG, void *, ULONG, ULONG *));
UDECL(ULONG, NtGdiRemoveFontMemResourceEx, (void *));
UDECL(int,   NtGdiGetFontResourceInfoInternalW, (const WCHAR *, ULONG, ULONG, ULONG, ULONG *, void *, ULONG));
UDECL(ULONG, NtGdiGetPublicFontTableChangeCookie, (void));
UDECL(ULONG, NtGdiGetFontUnicodeRanges, (HDC, void *));
UDECL(DWORD, GetFontResourceInfoW, (const WCHAR *, DWORD *, void *, DWORD));
UDECL(BOOL,  RemoveFontMemResourceEx_, (HANDLE));

#define FE_TRUETYPE 4
#define FE_RASTER   1
#define FE_DEVICE   2
#define FE_SCALEBIT 0x80000000u

/* One enumerated face, as seen by an EnumFontFamiliesEx callback. */
struct fe_item {
    WCHAR face[LF_FACESIZE], full[LF_FULLFACESIZE], style[LF_FACESIZE], script[LF_FACESIZE];
    LONG weight; BYTE italic, charset, pitch;
    DWORD ftype;
    int hasntm; DWORD ntmFlags; FONTSIGNATURE sig; DWORD sizeEM;
    WORD tmPitch;
};
struct fe_set {
    struct fe_item *it; int n, cap;
    int stop_after;      /* >0: callback returns 0 once n reaches it */
    int ret;             /* value the callback returns while enumerating (default 1) */
    void (*hook)(struct fe_set *, const struct fe_item *); /* optional, invoked per item */
    void *hookarg;
};
void fe_set_init(struct fe_set *s);
void fe_set_free(struct fe_set *s);
int  fe_find(const struct fe_set *s, const WCHAR *face);          /* number of items whose face matches (case-insensitive) */
int  fe_find_full(const struct fe_set *s, const WCHAR *full);
/* EnumFontFamiliesExW wrapper; face may be NULL/L"". Returns the API return value. */
int  fe_enum(HDC dc, const WCHAR *face, int charset, struct fe_set *s);
int  fe_count_family(HDC dc, const WCHAR *face, int charset);

/* Raw NtGdiEnumFonts: two-call protocol. Returns the final syscall result; *out is a malloc'd buffer (caller frees). */
struct fe_rec { ULONG size, cjEfdw, ftype; ENUMLOGFONTEXW elf; NEWTEXTMETRICEXW ntm; };
ULONG fe_raw_enum(HDC dc, ULONG type, const WCHAR *face, ULONG cs, BYTE **out, ULONG *size);
/* Walks a record chain; returns the record count or -1 when it is malformed (bounds, stride). */
int  fe_raw_walk(const BYTE *buf, ULONG size, struct fe_rec **recs);
/* The only faces that carry none of the RASTER/DEVICE/TRUETYPE font-type bits are the three stock vector fonts. */
int  fe_is_vector_face(const WCHAR *face);

/* Paths */
void fe_nt_path(const char *dos, WCHAR *out, size_t cap, ULONG *cwc); /* "\??\C:\x.ttf", cwc includes the NUL */
void fe_wide(const char *a, WCHAR *w, size_t cap);
const char *fe_windir_font(const char *leaf, char *out, size_t cap);  /* C:\Windows\Fonts\leaf */
int  fe_file_exists(const char *path);
BYTE *fe_read_file(const char *path, DWORD *len);

/* Minimal TrueType writer: writes a font with the given family into `out` (cap >= 2048). Glyphs: .notdef, U+0020, U+0041.
 * em 1024, advance 600 for every glyph. Returns byte length. */
size_t fe_make_ttf(BYTE *out, size_t cap, const char *family);
/* Unique per-process family name "Win32ktT<pid>x<n>" */
void fe_family(char *out, size_t cap, int n);
/* Writes the generated font to a temp file and returns its path (DOS). */
int  fe_write_ttf(const char *family, char *path, size_t cap);

HDC fe_screen_dc(void);          /* GetDC(NULL); caller ReleaseDC(NULL, dc) */
#endif
