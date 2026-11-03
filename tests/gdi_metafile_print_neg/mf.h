/* Shared helpers for the gdi_metafile_print_neg area.
 *
 * Facts used here come from the Windows 10 19041 analysis:
 *  - gdi32full!CreateEnhMetaFileW / CloseEnhMetaFile / GetEnhMetaFileHeader / GetEnhMetaFileBits /
 *    InternalGetEnhMetaFileDescription / GetEnhMetaFilePaletteEntries / SetEnhMetaFileBits / CopyEnhMetaFileW /
 *    DeleteEnhMetaFile / pvClientObjGet / hCreateClientObjLink / EnumObjects / GetObjectType / StartDocWImpl /
 *    StartPageImpl / EndDocImpl / AbortDocImpl / InternalEndPage / EscapeImpl / ExtEscapeImpl / DrawEscape /
 *    SetAbortProc / SetPixelFormat family (GetAPI -> opengl32 wgl*), gdi32!GetDeviceCaps;
 *  - win32kbase!NtGdiCreateMetafileDC, win32kfull!NtGdiEnumObjects / NtGdiStartDoc /
 *    NtGdiStartPage / NtGdiEndPage / NtGdiEndDoc / NtGdiAbortDoc / NtGdiExtEscape /
 *    NtGdiDrawEscape / NtGdiSetPixelFormat / NtGdiSwapBuffers / NtGdiDescribePixelFormat /
 *    NtGdiQueryFontAssocInfo / NtGdiConvertMetafileRect / NtGdiSetMetaRgn / NtGdiCreateServerMetaFile /
 *    NtGdiGetServerMetaFileBits / NtGdiInitSpool, win32kbase!NtGdiGetDeviceCaps / NtGdiGetDeviceCapsAll.
 * Handle type bits (HIWORD & 0x7f): DC 0x01, alt DC (metafile DC) 0x21, enhanced metafile (client object) 0x46,
 * 16-bit metafile DC 0x66.
 */
#ifndef MF_H
#define MF_H
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

#define MF_TYPE7(h)   ((unsigned)(((ULONG_PTR)(h) >> 16) & 0x7f))
#define MF_STOCKED(h) (((ULONG_PTR)(h) & 0x800000) != 0)
#define MF_IDX(h)     ((unsigned)((ULONG_PTR)(h) & 0xffff))
#define MF_UNIQ(h)    ((unsigned)(((ULONG_PTR)(h) >> 24) & 0xff))
#define MF_T7_DC      0x01u
#define MF_T7_ALTDC   0x21u
#define MF_T7_EMF     0x46u
#define MF_T7_METADC16 0x66u

/* cell layout of PEB->GdiSharedHandleTable (win32k.sys _ENTRY): einfo@0, owner@8 (pid&~1), upper@0xc, objt@0xe, flags@0xf */
struct mf_cell { unsigned long long q0; unsigned owner; unsigned short upper; unsigned char objt, flags; unsigned long long puser; };
static int mf_cell_of(HGDIOBJ h, struct mf_cell *c) {
    void *tab = NULL;
    if (!k_try_read_ptr((char *)k_peb() + PEB_GdiSharedHandleTable, &tab) || !tab) return 0;
    return k_try_read((char *)tab + (ULONG_PTR)MF_IDX(h) * GDI_CELL_SIZE, c, sizeof *c);
}

/* RAII-less fixtures */
static HDC mf_screen(void) { return U(GetDC) ? U(GetDC)(NULL) : NULL; }
static void mf_screen_free(HDC dc) { if (dc && U(ReleaseDC)) U(ReleaseDC)(NULL, dc); }

#define MF_NEED_EMF() do { if (!U(CreateEnhMetaFileW) || !U(CloseEnhMetaFile) || !U(DeleteEnhMetaFile) || !U(GetDC) || !U(ReleaseDC) || !U(GetEnhMetaFileHeader) || !U(GetObjectType)) { t_skip("enhanced metafile exports missing"); return; } } while (0)
#define MF_SCREEN(var) HDC var = mf_screen(); if (!var) { t_skip("no screen DC"); return; }

/* Dispose of a metafile DC that unexpectedly came back non-NULL (never under Wine, whose close path can fault on odd input). */
static void mf_discard(HDC m) {
    if (!m || t_is_wine()) return;
    HENHMETAFILE e = U(CloseEnhMetaFile)(m);
    if (e) U(DeleteEnhMetaFile)(e);
}

/* A memory EMF with a known content: header + select/rect/ellipse + EOF. Caller deletes. */
static HENHMETAFILE mf_make(HDC ref, const RECT *frame, const wchar_t *desc, int nrect) {
    HDC m = U(CreateEnhMetaFileW)(ref, NULL, frame, desc);
    if (!m) return NULL;
    for (int i = 0; i < nrect; i++) U(Rectangle)(m, 10 + i, 10 + i, 50 + i, 40 + i);
    return U(CloseEnhMetaFile)(m);
}

/* Record enumeration */
struct mf_enum {
    int total, first, last, nord, nobj, stop_at, calls_after_stop;
    int counts[128];
    int order[512];
    unsigned long dsize_sum;
    int bad_dsize;
};
static int CALLBACK mf_enum_cb(HDC hdc, HANDLETABLE *ht, const ENHMETARECORD *r, int nobj, LPARAM lp) {
    struct mf_enum *e = (struct mf_enum *)lp;
    if (e->stop_at && e->total >= e->stop_at) e->calls_after_stop++;
    if (e->total == 0) e->first = (int)r->iType;
    e->last = (int)r->iType;
    if (r->iType < 128) e->counts[r->iType]++;
    if (e->nord < 512) e->order[e->nord++] = (int)r->iType;
    e->nobj = nobj;
    e->dsize_sum += r->nSize;
    if (r->nSize < 8 || (r->nSize & 3)) e->bad_dsize++;
    e->total++;
    if (e->stop_at && e->total >= e->stop_at) return 0;
    return 1;
}
static int mf_enum(HENHMETAFILE h, struct mf_enum *e) {
    RECT rc = { 0, 0, 100, 100 };
    HDC dc = mf_screen();
    int r;
    memset(e, 0, sizeof *e);
    if (!dc || !U(EnumEnhMetaFile)) return -1;
    r = U(EnumEnhMetaFile)(dc, h, mf_enum_cb, e, &rc);
    mf_screen_free(dc);
    return r;
}

/* Walk the records of a metafile straight from its bits, filling the same summary as mf_enum. EnumEnhMetaFile does not call
   back at all for a metafile whose bounds are empty (nothing drawn), so such files are inspected this way. */
static int mf_walk(HENHMETAFILE h, struct mf_enum *e) {
    UINT n; unsigned char *b; UINT off = 0;
    memset(e, 0, sizeof *e);
    if (!U(GetEnhMetaFileBits)) return -1;
    n = U(GetEnhMetaFileBits)(h, 0, NULL);
    if (!n) return -1;
    b = (unsigned char *)malloc(n);
    if (!b) return -1;
    if (U(GetEnhMetaFileBits)(h, n, b) != n) { free(b); return -1; }
    while (off + 8 <= n) {
        const ENHMETARECORD *r = (const ENHMETARECORD *)(b + off);
        if (r->nSize < 8 || (r->nSize & 3) || off + r->nSize > n) { e->bad_dsize++; break; }
        if (e->total == 0) e->first = (int)r->iType;
        e->last = (int)r->iType;
        if (r->iType < 128) e->counts[r->iType]++;
        if (e->nord < 512) e->order[e->nord++] = (int)r->iType;
        e->dsize_sum += r->nSize;
        e->total++;
        off += r->nSize;
    }
    free(b);
    return e->total;
}

/* 32bpp top-down DIB section selected into a memory DC */
struct mf_dib { HDC dc; HBITMAP bmp, old; uint32_t *bits; int w, h; };
static int mf_dib_open(struct mf_dib *d, int w, int h, uint32_t fill) {
    BITMAPINFO bi; void *bits = NULL;
    memset(d, 0, sizeof *d);
    if (!U(CreateCompatibleDC) || !U(CreateDIBSection) || !U(SelectObject)) return 0;
    d->dc = U(CreateCompatibleDC)(NULL);
    if (!d->dc) return 0;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    d->bmp = U(CreateDIBSection)(d->dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!d->bmp || !bits) { U(DeleteDC)(d->dc); d->dc = NULL; return 0; }
    d->bits = (uint32_t *)bits; d->w = w; d->h = h;
    d->old = (HBITMAP)U(SelectObject)(d->dc, d->bmp);
    for (int i = 0; i < w * h; i++) d->bits[i] = fill;
    return 1;
}
static void mf_dib_close(struct mf_dib *d) {
    if (!d->dc) return;
    U(SelectObject)(d->dc, d->old);
    U(DeleteObject)(d->bmp);
    U(DeleteDC)(d->dc);
    memset(d, 0, sizeof *d);
}
static uint32_t mf_px(const struct mf_dib *d, int x, int y) { return d->bits[y * d->w + x] & 0xffffff; }
#define MF_NEED_DIB(d, w, h, fill) struct mf_dib d; if (!mf_dib_open(&d, w, h, fill)) { t_skip("cannot create DIB section"); return; }

static BYTE *mf_getbits(HENHMETAFILE e, UINT *n) {
    UINT s = U(GetEnhMetaFileBits)(e, 0, NULL);
    BYTE *b;
    *n = 0;
    if (!s || s > (1u << 24)) return NULL;
    b = (BYTE *)malloc(s);
    if (!b) return NULL;
    if (U(GetEnhMetaFileBits)(e, s, b) != s) { free(b); return NULL; }
    *n = s;
    return b;
}
static int mf_hdr(HENHMETAFILE e, ENHMETAHEADER *h) {
    memset(h, 0, sizeof *h);
    return U(GetEnhMetaFileHeader)(e, sizeof *h, h) != 0;
}


/* nth record of type `ty` in raw EMF bits, or NULL */
static const ENHMETARECORD *mf_find(const BYTE *b, UINT n, DWORD ty, int nth) {
    UINT off = 0;
    while (off + 8 <= n) {
        const ENHMETARECORD *r = (const ENHMETARECORD *)(b + off);
        if (r->nSize < 8 || (r->nSize & 3) || off + r->nSize > n) return NULL;
        if (r->iType == ty && nth-- == 0) return r;
        off += r->nSize;
    }
    return NULL;
}

#endif
