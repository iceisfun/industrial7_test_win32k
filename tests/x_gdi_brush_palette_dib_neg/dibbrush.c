/* NtGdiCreateDIBBrush parameter matrix: the packed-DIB header (size, depth, compression, colour count, planes,
 * dimensions), the colour usage selector, the byte count handed to the kernel and a buffer that is shorter than
 * claimed (placed against an inaccessible page). Each row calls the Nt stub directly with a built DIB and records
 * whether a brush came back and the last error. The kernel validates the header against the byte count it is given,
 * so a mismatch is rejected rather than read past. */
#include "xb.h"

#define CJ0 ((int)0x80000000)      /* delta marker: pass a byte count of zero */
/* name, size, bpp, comp, clrused, w, h, planes, usage, cj delta, guard, b8x8, bpen, expected nz, expected error */
#define DIBROWS(X) \
 X(ok24,        40, 24, 0, 0, 8, 8, 1, 0, 0, 0, 0, 0, 1,0) \
 X(ok32,        40, 32, 0, 0, 8, 8, 1, 0, 0, 0, 0, 0, 1,0) \
 X(ok8,         40,  8, 0, 0, 8, 8, 1, 0, 0, 0, 0, 0, 1,0) \
 X(ok4,         40,  4, 0, 0, 8, 8, 1, 0, 0, 0, 0, 0, 1,0) \
 X(ok1,         40,  1, 0, 0, 8, 8, 1, 0, 0, 0, 0, 0, 1,0) \
 X(ok16,        40, 16, 0, 0, 8, 8, 1, 0, 0, 0, 0, 0, 1,0) \
 X(ok16_bitfields, 40, 16, 3, 0, 8, 8, 1, 0, 0, 0, 0, 0, 1,0) \
 X(ok32_bitfields, 40, 32, 3, 0, 8, 8, 1, 0, 0, 0, 0, 0, 1,0) \
 X(ok24_topdown, 40, 24, 0, 0, 8, -8, 1, 0, 0, 0, 0, 0, 1,0) \
 X(ok24_guard,  40, 24, 0, 0, 8, 8, 1, 0, 0, 1, 0, 0, 1,0) \
 X(ok8_8x8,     40,  8, 0, 0, 8, 8, 1, 0, 0, 0, 1, 0, 1,0) \
 X(ok24_pen,    40, 24, 0, 0, 8, 8, 1, 0, 0, 0, 0, 1, 1,0) \
 X(ok24_8x8_pen, 40, 24, 0, 0, 8, 8, 1, 0, 0, 0, 1, 1, 1,0) \
 X(ok24_16x16,  40, 24, 0, 0, 16, 16, 1, 0, 0, 0, 0, 0, 1,0) \
 X(ok24_16x16_8x8, 40, 24, 0, 0, 16, 16, 1, 0, 0, 0, 1, 0, 1,0) \
 X(ok24_1x1,    40, 24, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 1,0) \
 X(ok24_9x3,    40, 24, 0, 0, 9, 3, 1, 0, 0, 0, 0, 0, 1,0) \
 X(ok24_wide,   40, 24, 0, 0, 64, 2, 1, 0, 0, 0, 0, 0, 1,0) \
 X(use1_8,      40,  8, 0, 0, 8, 8, 1, 1, 0, 0, 0, 0, 1,0) \
 X(use1_4,      40,  4, 0, 0, 8, 8, 1, 1, 0, 0, 0, 0, 1,0) \
 X(use1_1,      40,  1, 0, 0, 8, 8, 1, 1, 0, 0, 0, 0, 1,0) \
 X(use1_24,     40, 24, 0, 0, 8, 8, 1, 1, 0, 0, 0, 0, 1,0) \
 X(use2_8,      40,  8, 0, 0, 8, 8, 1, 2, 0, 0, 0, 0, 1,0) \
 X(use3_8,      40,  8, 0, 0, 8, 8, 1, 3, 0, 0, 0, 0, 0,87) \
 X(use4_8,      40,  8, 0, 0, 8, 8, 1, 4, 0, 0, 0, 0, 0,87) \
 X(use256_8,    40,  8, 0, 0, 8, 8, 1, 256, 0, 0, 0, 0, 0,87) \
 X(usemax_8,    40,  8, 0, 0, 8, 8, 1, 0xffffffff, 0, 0, 0, 0, 0,87) \
 X(usemax_24,   40, 24, 0, 0, 8, 8, 1, 0xffffffff, 0, 0, 0, 0, 0,87) \
 X(size0,        0, 24, 0, 0, 8, 8, 1, 0, 0, 0, 0, 0, 0,0) \
 X(size4,        4, 24, 0, 0, 8, 8, 1, 0, 0, 0, 0, 0, 0,0) \
 X(size11,      11, 24, 0, 0, 8, 8, 1, 0, 0, 0, 0, 0, 0,0) \
 X(size12_core, 12, 24, 0, 0, 8, 8, 1, 0, 0, 0, 0, 0, 0,0) \
 X(size12_core8, 12, 8, 0, 0, 8, 8, 1, 0, 0, 0, 0, 0, 0,0) \
 X(size16,      16, 24, 0, 0, 8, 8, 1, 0, 0, 0, 0, 0, 0,0) \
 X(size39,      39, 24, 0, 0, 8, 8, 1, 0, 0, 0, 0, 0, 0,87) \
 X(size41,      41, 24, 0, 0, 8, 8, 1, 0, 0, 0, 0, 0, 1,0) \
 X(size52,      52, 24, 0, 0, 8, 8, 1, 0, 0, 0, 0, 0, 1,0) \
 X(size56,      56, 24, 0, 0, 8, 8, 1, 0, 0, 0, 0, 0, 1,0) \
 X(size64,      64, 24, 0, 0, 8, 8, 1, 0, 0, 0, 0, 0, 1,0) \
 X(size108_v4, 108, 24, 0, 0, 8, 8, 1, 0, 0, 0, 0, 0, 1,0) \
 X(size124_v5, 124, 24, 0, 0, 8, 8, 1, 0, 0, 0, 0, 0, 1,0) \
 X(size125,    125, 24, 0, 0, 8, 8, 1, 0, 0, 0, 0, 0, 1,0) \
 X(size140,    140, 24, 0, 0, 8, 8, 1, 0, 0, 0, 0, 0, 1,0) \
 X(size4096,  4096, 24, 0, 0, 8, 8, 1, 0, 0, 0, 0, 0, 0,87) \
 X(sizemax, 0xffffffff, 24, 0, 0, 8, 8, 1, 0, 0, 0, 0, 0, 0,0) \
 X(bpp0,        40,  0, 0, 0, 8, 8, 1, 0, 0, 0, 0, 0, 0,0) \
 X(bpp2,        40,  2, 0, 0, 8, 8, 1, 0, 0, 0, 0, 0, 0,0) \
 X(bpp3,        40,  3, 0, 0, 8, 8, 1, 0, 0, 0, 0, 0, 0,0) \
 X(bpp5,        40,  5, 0, 0, 8, 8, 1, 0, 0, 0, 0, 0, 0,0) \
 X(bpp15,       40, 15, 0, 0, 8, 8, 1, 0, 0, 0, 0, 0, 0,0) \
 X(bpp48,       40, 48, 0, 0, 8, 8, 1, 0, 0, 0, 0, 0, 0,0) \
 X(bpp64,       40, 64, 0, 0, 8, 8, 1, 0, 0, 0, 0, 0, 0,0) \
 X(bppmax,      40, 65535, 0, 0, 8, 8, 1, 0, 0, 0, 0, 0, 0,0) \
 X(rle8_8bpp,   40,  8, 1, 0, 8, 8, 1, 0, 0, 0, 0, 0, 0,87) \
 X(rle4_4bpp,   40,  4, 2, 0, 8, 8, 1, 0, 0, 0, 0, 0, 0,87) \
 X(rle8_24bpp,  40, 24, 1, 0, 8, 8, 1, 0, 0, 0, 0, 0, 0,0) \
 X(rle4_8bpp,   40,  8, 2, 0, 8, 8, 1, 0, 0, 0, 0, 0, 0,0) \
 X(bitfields_24, 40, 24, 3, 0, 8, 8, 1, 0, 0, 0, 0, 0, 0,0) \
 X(bitfields_8, 40,  8, 3, 0, 8, 8, 1, 0, 0, 0, 0, 0, 0,0) \
 X(jpeg_24,     40, 24, 4, 0, 8, 8, 1, 0, 0, 0, 0, 0, 0,0) \
 X(png_24,      40, 24, 5, 0, 8, 8, 1, 0, 0, 0, 0, 0, 0,0) \
 X(comp6,       40, 24, 6, 0, 8, 8, 1, 0, 0, 0, 0, 0, 0,0) \
 X(comp_max,    40, 24, 0xffffffff, 0, 8, 8, 1, 0, 0, 0, 0, 0, 0,0) \
 X(planes0,     40, 24, 0, 0, 8, 8, 0, 0, 0, 0, 0, 0, 1,0) \
 X(planes2,     40, 24, 0, 0, 8, 8, 2, 0, 0, 0, 0, 0, 1,0) \
 X(planesmax,   40, 24, 0, 0, 8, 8, 65535, 0, 0, 0, 0, 0, 1,0) \
 X(w0,          40, 24, 0, 0, 0, 8, 1, 0, 0, 0, 0, 0, 0,87) \
 X(h0,          40, 24, 0, 0, 8, 0, 1, 0, 0, 0, 0, 0, 0,87) \
 X(wneg,        40, 24, 0, 0, -8, 8, 1, 0, 0, 0, 0, 0, 0,87) \
 X(wmax,        40, 24, 0, 0, 0x7fffffff, 8, 1, 0, 0, 0, 0, 0, 0,0) \
 X(wmin,        40, 24, 0, 0, (LONG)0x80000000, 8, 1, 0, 0, 0, 0, 0, 0,87) \
 X(hmax,        40, 24, 0, 0, 8, 0x7fffffff, 1, 0, 0, 0, 0, 0, 0,0) \
 X(hmin,        40, 24, 0, 0, 8, (LONG)0x80000000, 1, 0, 0, 0, 0, 0, 0,0) \
 X(huge_wh,     40, 32, 0, 0, 65536, 65536, 1, 0, 0, 0, 0, 0, 0,0) \
 X(clr1_8,      40,  8, 0, 1, 8, 8, 1, 0, 0, 0, 0, 0, 1,0) \
 X(clr2_1,      40,  1, 0, 2, 8, 8, 1, 0, 0, 0, 0, 0, 1,0) \
 X(clr1_1,      40,  1, 0, 1, 8, 8, 1, 0, 0, 0, 0, 0, 1,0) \
 X(clr3_1,      40,  1, 0, 3, 8, 8, 1, 0, 0, 0, 0, 0, 1,0) \
 X(clr17_4,     40,  4, 0, 17, 8, 8, 1, 0, 0, 0, 0, 0, 1,0) \
 X(clr256_8,    40,  8, 0, 256, 8, 8, 1, 0, 0, 0, 0, 0, 1,0) \
 X(clr257_8,    40,  8, 0, 257, 8, 8, 1, 0, 0, 0, 0, 0, 1,0) \
 X(clrmax_8,    40,  8, 0, 0xffffffff, 8, 8, 1, 0, 0, 0, 0, 0, 1,0) \
 X(clr4_24,     40, 24, 0, 4, 8, 8, 1, 0, 0, 0, 0, 0, 1,0) \
 X(clr4_32,     40, 32, 0, 4, 8, 8, 1, 0, 0, 0, 0, 0, 1,0) \
 X(cj_minus1,   40, 24, 0, 0, 8, 8, 1, 0, -1, 0, 0, 0, 0,87) \
 X(cj_minus4,   40, 24, 0, 0, 8, 8, 1, 0, -4, 0, 0, 0, 0,87) \
 X(cj_plus1,    40, 24, 0, 0, 8, 8, 1, 0, 1, 0, 0, 0, 1,0) \
 X(cj_plus4,    40, 24, 0, 0, 8, 8, 1, 0, 4, 0, 0, 0, 1,0) \
 X(cj_plus100,  40, 24, 0, 0, 8, 8, 1, 0, 100, 0, 0, 0, 1,0) \
 X(cj_zero,     40, 24, 0, 0, 8, 8, 1, 0, CJ0, 0, 0, 0, 0,87) \
 X(cj_minus_bits, 40, 24, 0, 0, 8, 8, 1, 0, -192, 0, 0, 0, 0,87) \
 X(cj_hdr_only, 40, 24, 0, 0, 8, 8, 1, 0, -192, 0, 0, 0, 0,87) \
 X(cj_8_minus1, 40,  8, 0, 0, 8, 8, 1, 0, -1, 0, 0, 0, 0,87) \
 X(cj_8_plus1,  40,  8, 0, 0, 8, 8, 1, 0, 1, 0, 0, 0, 1,0) \
 X(cj_1_minus1, 40,  1, 0, 0, 8, 8, 1, 0, -1, 0, 0, 0, 0,87) \
 X(cj_1_plus4,  40,  1, 0, 0, 8, 8, 1, 0, 4, 0, 0, 0, 1,0) \
 X(guard_plus1, 40, 24, 0, 0, 8, 8, 1, 0, 1, 1, 0, 0, 0,0) \
 X(guard_plus4, 40, 24, 0, 0, 8, 8, 1, 0, 4, 1, 0, 0, 0,0) \
 X(guard_plus64, 40, 24, 0, 0, 8, 8, 1, 0, 64, 1, 0, 0, 0,0) \
 X(guard_minus4, 40, 24, 0, 0, 8, 8, 1, 0, -4, 1, 0, 0, 0,87) \
 X(guard_8bpp,  40,  8, 0, 0, 8, 8, 1, 0, 0, 1, 0, 0, 1,0) \
 X(guard_8bpp_plus8, 40, 8, 0, 0, 8, 8, 1, 0, 8, 1, 0, 0, 0,0) \
 X(guard_hdr_claims_more, 40, 24, 0, 0, 8, 64, 1, 0, 0, 1, 0, 0, 1,0)

struct dibrow { const char *nm; DWORD size; int bpp; DWORD comp, clrused; LONG w, h; int planes; DWORD usage; int delta, guard, b8, pen; int nz, err; };
#define ROWSTRUCT(nm, sz, bpp, comp, clr, w, h, pl, us, dl, gd, b8, pn, enz, eerr) { #nm, sz, bpp, comp, clr, w, h, pl, us, dl, gd, b8, pn, enz, eerr },
static const struct dibrow dibrows[] = { DIBROWS(ROWSTRUCT) };

static void dib_run(const struct dibrow *r) {
    REQUIRE(UN(NtGdiCreateDIBBrush));
    BYTE buf[4096];
    struct xb_spec s = { r->size, r->bpp, r->comp, r->clrused, r->w, r->h, r->planes, r->usage };
    size_t len = xb_build(buf, sizeof buf, &s);
    ULONG cj = r->delta == CJ0 ? 0 : (ULONG)((long)len + r->delta);
    struct xb_guard g = {0}; const void *p = buf;
    if (r->guard) {
        if (!xb_guard_open(&g, len)) { t_skip("guard page"); return; }
        memcpy(g.p, buf, len); p = g.p;
    }
    SetLastError(0);
    HANDLE h = UN(NtGdiCreateDIBBrush)(p, r->usage, cj, r->b8, r->pen, p);
    DWORD e = GetLastError();
    xb_row(r->nm, h != NULL, e, r->nz, r->err);
    if (h) {
        CHECKF(U(GetObjectType)(h) == OBJ_BRUSH, "%s: result is not a brush", r->nm);
        CHECKF(U(DeleteObject)(h), "%s: delete", r->nm);
    }
    if (r->guard) xb_guard_close(&g);
}
XB_GROUP(dibbrush_valid_headers, dibrows, dib_run, "ok")
XB_GROUP(dibbrush_usage, dibrows, dib_run, "use")
XB_GROUP(dibbrush_header_size, dibrows, dib_run, "size")
XB_GROUP(dibbrush_bit_count, dibrows, dib_run, "bpp")
XB_GROUP(dibbrush_compression, dibrows, dib_run, "rle|bitfields|jpeg|png|comp")
XB_GROUP(dibbrush_planes, dibrows, dib_run, "planes")
XB_GROUP(dibbrush_dimensions, dibrows, dib_run, "w|h")
XB_GROUP(dibbrush_colors_used, dibrows, dib_run, "clr")
XB_GROUP(dibbrush_byte_count, dibrows, dib_run, "cj")
XB_GROUP(dibbrush_truncated_buffer, dibrows, dib_run, "guard")
XT(dibbrush_null_bitmapinfo) {
    REQUIRE(UN(NtGdiCreateDIBBrush)); BYTE buf[256] = {0};
    SetLastError(0); CHECK(UN(NtGdiCreateDIBBrush)(NULL, DIB_RGB_COLORS, 0, FALSE, FALSE, buf) == NULL);
    SetLastError(0); CHECK(UN(NtGdiCreateDIBBrush)(NULL, DIB_RGB_COLORS, 100, FALSE, FALSE, buf) == NULL);
    SetLastError(0); CHECK(UN(NtGdiCreateDIBBrush)((void *)(ULONG_PTR)0x1000, DIB_RGB_COLORS, 100, FALSE, FALSE, buf) == NULL);
}
XT(dibbrush_gdi32_null_packed_dib) { REQUIRE(U(CreateDIBPatternBrushPt)); CHECK(U(CreateDIBPatternBrushPt)(NULL, DIB_RGB_COLORS) == NULL); }
