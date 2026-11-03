#include "fm.h"
struct fm_api fm;
static int g_init;
static const char *g_miss;
fm_NtGdiHfontCreate_pfn NtGdiHfontCreate_;
fm_NtGdiGetTextFaceW_pfn NtGdiGetTextFaceW_;
fm_NtGdiGetTextMetricsW_pfn NtGdiGetTextMetricsW_;
fm_NtGdiGetFontData_pfn NtGdiGetFontData_;
fm_NtGdiExtGetObjectW_pfn NtGdiExtGetObjectW_;
fm_NtGdiGetTextCharsetInfo_pfn NtGdiGetTextCharsetInfo_;
fm_NtGdiGetCharSet_pfn NtGdiGetCharSet_;
fm_NtGdiGetFontUnicodeRanges_pfn NtGdiGetFontUnicodeRanges_;
fm_NtGdiGetKerningPairs_pfn NtGdiGetKerningPairs_;
fm_NtGdiGetGlyphOutline_pfn NtGdiGetGlyphOutline_;
fm_NtGdiGetOutlineTextMetricsInternalW_pfn NtGdiGetOutlineTextMetricsInternalW_;
fm_NtGdiSelectFont_pfn NtGdiSelectFont_;

const char *fm_init(void) {
    if (g_init) return g_miss;
    g_init = 1;
#define X(n) fm.n = U(n); if (!fm.n && !g_miss) g_miss = #n;
    FM_FUNCS(X)
#undef X
#define S(n) n##_ = (fm_##n##_pfn)t_proc_in("win32u.dll", #n);
    S(NtGdiHfontCreate) S(NtGdiGetTextFaceW) S(NtGdiGetTextMetricsW) S(NtGdiGetFontData) S(NtGdiExtGetObjectW)
    S(NtGdiGetTextCharsetInfo) S(NtGdiGetCharSet) S(NtGdiGetFontUnicodeRanges) S(NtGdiGetKerningPairs)
    S(NtGdiGetGlyphOutline) S(NtGdiGetOutlineTextMetricsInternalW) S(NtGdiSelectFont)
#undef S
    return g_miss;
}

int fm_wcsieq(const WCHAR *a, const WCHAR *b) {
    for (;; a++, b++) {
        WCHAR x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
        if (!x) return 1;
    }
}
void fm_u8(const WCHAR *w, char *out, size_t cap) {
    size_t i = 0;
    for (; w[i] && i + 1 < cap; i++) out[i] = w[i] < 0x80 ? (char)w[i] : '?';
    out[i] = 0;
}
HDC fm_dc(void) { return fm.CreateCompatibleDC(NULL); }
void fm_freedc(HDC dc) { if (dc) fm.DeleteDC(dc); }
HFONT fm_font(const WCHAR *face, int height, int weight, int charset, int quality, int pitchfam) {
    LOGFONTW lf; memset(&lf, 0, sizeof lf);
    lf.lfHeight = height; lf.lfWeight = weight; lf.lfCharSet = (BYTE)charset; lf.lfQuality = (BYTE)quality;
    lf.lfPitchAndFamily = (BYTE)pitchfam;
    lf.lfOutPrecision = OUT_DEFAULT_PRECIS; lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    if (face) { int i; for (i = 0; i < 31 && face[i]; i++) lf.lfFaceName[i] = face[i]; }
    return fm.CreateFontIndirectW(&lf);
}
HFONT fm_sel(HDC dc, HFONT f) { return (HFONT)fm.SelectObject(dc, f); }
int fm_face(HDC dc, WCHAR *out, int cap) { out[0] = 0; return fm.GetTextFaceW(dc, cap, out); }
int fm_face_present(const WCHAR *face) {
    HDC dc = fm_dc(); if (!dc) return 0;
    HFONT f = fm_font(face, -16, 400, ANSI_CHARSET, 0, 0), old;
    WCHAR got[LF_FACESIZE + 8]; int ok = 0;
    if (f) { old = fm_sel(dc, f); fm_face(dc, got, 40); ok = fm_wcsieq(got, face); fm_sel(dc, old); fm.DeleteObject(f); }
    fm_freedc(dc);
    return ok;
}
int fm_cell_of(HGDIOBJ h, struct fm_cell *c) {
    void *base = NULL;
    if (!k_try_read_ptr((char *)k_peb() + PEB_GdiSharedHandleTable, &base) || !base) return 0;
    return k_try_read((char *)base + (ULONG_PTR)FM_INDEX(h) * GDI_CELL_SIZE, c, sizeof *c);
}
int fm_cell_live_font(HGDIOBJ h) {
    struct fm_cell c;
    if (!fm_cell_of(h, &c)) return 0;
    return c.objt == FM_T7_FONT && c.upper == (unsigned short)(((ULONG_PTR)h >> 16) & 0xffff);
}
