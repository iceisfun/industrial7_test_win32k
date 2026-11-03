/* NtGdiStretchDIBitsInternal parameter matrix and pixel consistency. Baseline: a 2x2 24bpp bottom-up source
 * (top-down red, green / blue, yellow) copied 1:1 with SRCCOPY to a 6x6 32bpp memory surface at (1,1). Each row mutates
 * one thing (DC handle, destination / source rectangle, header fields, byte counts, colour usage, raster operation) and
 * records the return value (the number of scan lines copied, 0 when rejected), the last error and how many of the 36
 * destination pixels changed from white. */
#include "xb.h"

#define RED 0xFF0000u
#define GRN 0x00FF00u
#define BLU 0x0000FFu
#define YEL 0xFFFF00u
struct sdp {
    int dk; int xD, yD, cxD, cyD, xS, yS, cxS, cyS;
    const void *bits; BITMAPINFO *bmi; DWORD usage, rop; UINT cjInfo, cjBits; HANDLE hcm;
    BITMAPINFOHEADER *h; BYTE *pbits; BYTE hdr[4096]; BYTE bitbuf[4096];
};
static void sd_base(struct sdp *p) {
    memset(p, 0, sizeof *p);
    p->dk = -1; p->xD = 1; p->yD = 1; p->cxD = 2; p->cyD = 2; p->xS = 0; p->yS = 0; p->cxS = 2; p->cyS = 2;
    p->h = (BITMAPINFOHEADER *)p->hdr; p->h->biSize = 40; p->h->biWidth = 2; p->h->biHeight = 2; p->h->biPlanes = 1; p->h->biBitCount = 24;
    p->pbits = p->bitbuf;
    static const BYTE px[16] = { 255,0,0, 0,255,255, 0,0, 0,0,255, 0,255,0, 0,0 };   /* bottom row: blue, yellow; top row: red, green */
    memcpy(p->pbits, px, 16);
    p->bits = p->pbits; p->bmi = (BITMAPINFO *)p->hdr; p->usage = DIB_RGB_COLORS; p->rop = SRCCOPY; p->cjInfo = 40; p->cjBits = 16;
}
static int sd_changed(const struct xb_surf *s, DWORD bg) { int n = 0; for (int i = 0; i < s->w * s->h; i++) if ((s->px[i] & 0xFFFFFF) != bg) n++; return n; }

/* name, expected return, error, changed pixels, mutation */
#define SDROWS(X) \
 X(base,          2u,0,4, ;) \
 X(dest_origin,   2u,0,4, p->xD = 0; p->yD = 0;) \
 X(dest_edge,     2u,0,4, p->xD = 4; p->yD = 4;) \
 X(dest_partly_outside, 2u,0,1, p->xD = 5; p->yD = 5;) \
 X(dest_fully_outside,  2u,0,0, p->xD = 6; p->yD = 6;) \
 X(dest_negative, 2u,0,1, p->xD = -1; p->yD = -1;) \
 X(dest_far,      2u,0,0, p->xD = 0x7fffffff; p->yD = 0x7fffffff;) \
 X(dest_far_neg,  2u,0,0, p->xD = (int)0x80000000; p->yD = (int)0x80000000;) \
 X(scale_up_2x,   2u,0,16, p->xD = 0; p->yD = 0; p->cxD = 4; p->cyD = 4;) \
 X(scale_up_odd,  2u,0,15, p->xD = 0; p->yD = 0; p->cxD = 3; p->cyD = 5;) \
 X(scale_down,    2u,0,1, p->cxD = 1; p->cyD = 1;) \
 X(scale_wide,    2u,0,6, p->xD = 0; p->cxD = 6; p->cyD = 1;) \
 X(zero_dest_w,   2u,0,0, p->cxD = 0;) \
 X(zero_dest_h,   2u,0,0, p->cyD = 0;) \
 X(zero_dest_both, 2u,0,0, p->cxD = 0; p->cyD = 0;) \
 X(neg_dest_w,    2u,0,4, p->xD = 2; p->cxD = -2;) \
 X(neg_dest_h,    2u,0,4, p->yD = 2; p->cyD = -2;) \
 X(neg_dest_both, 2u,0,4, p->xD = 2; p->yD = 2; p->cxD = -2; p->cyD = -2;) \
 X(huge_dest_w,   0u,0,0, p->cxD = 0x7fffffff;) \
 X(huge_dest_h,   0u,0,0, p->cyD = 0x7fffffff;) \
 X(min_dest_w,    0u,0,0, p->cxD = (int)0x80000000;) \
 X(zero_src_w,    0u,0,0, p->cxS = 0;) \
 X(zero_src_h,    0u,0,0, p->cyS = 0;) \
 X(neg_src_w,     2u,0,4, p->xS = 1; p->cxS = -2;) \
 X(neg_src_h,     0u,0,0, p->yS = 1; p->cyS = -2;) \
 X(src_x1,        2u,0,2, p->xS = 1; p->cxS = 1; p->cxD = 1;) \
 X(src_y1,        2u,0,2, p->yS = 1; p->cyS = 1; p->cyD = 1;) \
 X(src_x_outside, 0u,0,0, p->xS = 2;) \
 X(src_y_outside, 0u,0,0, p->yS = 2;) \
 X(src_x_neg,     2u,0,2, p->xS = -1;) \
 X(src_y_neg,     2u,0,2, p->yS = -1;) \
 X(src_w_over,    2u,0,2, p->cxS = 3;) \
 X(src_h_over,    2u,0,2, p->cyS = 3;) \
 X(src_w_huge,    0u,0,0, p->cxS = 0x7fffffff;) \
 X(src_x_huge,    0u,0,0, p->xS = 0x7fffffff;) \
 X(src_y_min,     0u,0,0, p->yS = (int)0x80000000;) \
 X(null_bits,     0u,0,0, p->bits = NULL;) \
 X(null_bmi,      0u,0,0, p->bmi = NULL;) \
 X(junk_bits,     0u,0,0, p->bits = (void *)(ULONG_PTR)0x1000;) \
 X(junk_bmi,      0u,0,0, p->bmi = (BITMAPINFO *)(ULONG_PTR)0x1000;) \
 X(cjinfo_0,      0u,0,0, p->cjInfo = 0;) \
 X(cjinfo_39,     0u,0,0, p->cjInfo = 39;) \
 X(cjinfo_41,     0u,0,0, p->cjInfo = 41;) \
 X(cjinfo_44,     0u,0,0, p->cjInfo = 44;) \
 X(cjinfo_1000,   0u,0,0, p->cjInfo = 1000;) \
 X(cjinfo_max,    0u,0,0, p->cjInfo = 0xFFFFFFFF;) \
 X(cjbits_0,      0u,0,0, p->cjBits = 0;) \
 X(cjbits_15,     0u,87,0, p->cjBits = 15;) \
 X(cjbits_8,      0u,87,0, p->cjBits = 8;) \
 X(cjbits_17,     2u,0,4, p->cjBits = 17;) \
 X(cjbits_1000,   2u,0,4, p->cjBits = 1000;) \
 X(cjbits_max,    0u,0,0, p->cjBits = 0xFFFFFFFF;) \
 X(hdr_size_0,    0u,0,0, p->h->biSize = 0;) \
 X(hdr_size_12,   0u,0,0, p->h->biSize = 12; p->cjInfo = 12;) \
 X(hdr_size_20,   0u,0,0, p->h->biSize = 20; p->cjInfo = 20;) \
 X(hdr_size_39,   0u,0,0, p->h->biSize = 39; p->cjInfo = 39;) \
 X(hdr_size_41,   0u,0,0, p->h->biSize = 41; p->cjInfo = 41;) \
 X(hdr_size_52,   2u,0,4, p->h->biSize = 52; p->cjInfo = 52;) \
 X(hdr_size_56,   2u,0,4, p->h->biSize = 56; p->cjInfo = 56;) \
 X(hdr_size_108,  2u,0,4, p->h->biSize = 108; p->cjInfo = 108;) \
 X(hdr_size_124,  2u,0,4, p->h->biSize = 124; p->cjInfo = 124;) \
 X(hdr_size_125,  0u,0,0, p->h->biSize = 125; p->cjInfo = 125;) \
 X(hdr_size_max,  0u,0,0, p->h->biSize = 0xFFFFFFFF;) \
 X(hdr_planes_0,  2u,0,4, p->h->biPlanes = 0;) \
 X(hdr_planes_2,  2u,0,4, p->h->biPlanes = 2;) \
 X(hdr_bpp_0,     0u,0,0, p->h->biBitCount = 0;) \
 X(hdr_bpp_3,     0u,0,0, p->h->biBitCount = 3;) \
 X(hdr_bpp_15,    0u,0,0, p->h->biBitCount = 15;) \
 X(hdr_bpp_64,    0u,0,0, p->h->biBitCount = 64;) \
 X(hdr_comp_rle8, 0u,0,0, p->h->biCompression = BI_RLE8;) \
 X(hdr_comp_rle4, 0u,0,0, p->h->biCompression = BI_RLE4;) \
 X(hdr_comp_bitfields, 0u,0,0, p->h->biCompression = BI_BITFIELDS;) \
 X(hdr_comp_jpeg, 0u,0,0, p->h->biCompression = BI_JPEG;) \
 X(hdr_comp_png,  0u,0,0, p->h->biCompression = BI_PNG;) \
 X(hdr_comp_6,    0u,0,0, p->h->biCompression = 6;) \
 X(hdr_comp_max,  0u,0,0, p->h->biCompression = 0xFFFFFFFF;) \
 X(hdr_width_0,   0u,0,0, p->h->biWidth = 0;) \
 X(hdr_width_neg, 0u,0,0, p->h->biWidth = -2;) \
 X(hdr_width_huge, 0u,534,0, p->h->biWidth = 0x7fffffff;) \
 X(hdr_height_0,  0u,0,0, p->h->biHeight = 0;) \
 X(hdr_height_neg, 2u,0,4, p->h->biHeight = -2;) \
 X(hdr_height_huge, 2u,0,4, p->h->biHeight = 0x7fffffff;) \
 X(hdr_height_min, 0u,534,0, p->h->biHeight = (LONG)0x80000000;) \
 X(hdr_clrused_1, 2u,0,4, p->h->biClrUsed = 1;) \
 X(hdr_clrused_big, 2u,0,4, p->h->biClrUsed = 0xFFFFFFFF;) \
 X(hdr_sizeimage_small, 2u,0,4, p->h->biSizeImage = 4;) \
 X(hdr_sizeimage_big, 2u,0,4, p->h->biSizeImage = 0xFFFFFF;) \
 X(usage_1,       2u,0,4, p->usage = 1;) \
 X(usage_2,       0u,87,0, p->usage = 2;) \
 X(usage_3,       0u,0,0, p->usage = 3;) \
 X(usage_0x100,   0u,0,0, p->usage = 0x100;) \
 X(usage_max,     0u,0,0, p->usage = 0xFFFFFFFF;) \
 X(rop_blackness, 1u,0,4, p->rop = BLACKNESS;) \
 X(rop_whiteness, 1u,0,0, p->rop = WHITENESS;) \
 X(rop_dstinvert, 1u,0,4, p->rop = DSTINVERT;) \
 X(rop_notsrccopy, 2u,0,4, p->rop = NOTSRCCOPY;) \
 X(rop_srcinvert, 2u,0,4, p->rop = SRCINVERT;) \
 X(rop_srcand,    2u,0,4, p->rop = SRCAND;) \
 X(rop_srcpaint,  2u,0,0, p->rop = SRCPAINT;) \
 X(rop_patcopy,   1u,0,0, p->rop = PATCOPY;) \
 X(rop_nomirror,  2u,0,4, p->rop = SRCCOPY | NOMIRRORBITMAP;) \
 X(rop_captureblt, 2u,0,4, p->rop = SRCCOPY | CAPTUREBLT;) \
 X(rop_zero,      1u,0,4, p->rop = 0;) \
 X(rop_junk,      0u,87,0, p->rop = 0x12345678;) \
 X(rop_max,       1u,0,0, p->rop = 0xFFFFFFFF;) \
 X(hcm_junk,      2u,0,4, p->hcm = (HANDLE)(ULONG_PTR)0x1234;) \
 X(hcm_minus1,    2u,0,4, p->hcm = (HANDLE)(LONG_PTR)-1;) \
 X(dc_null,       0u,0,0, p->dk = XH_NULL;) \
 X(dc_junk,       0u,0,0, p->dk = XH_JUNK;) \
 X(dc_minus1,     0u,0,0, p->dk = XH_MINUS1;) \
 X(dc_bitmap,     0u,0,0, p->dk = XH_BITMAP;) \
 X(dc_pen,        0u,0,0, p->dk = XH_PEN;) \
 X(dc_brush,      0u,0,0, p->dk = XH_BRUSH;) \
 X(dc_region,     0u,0,0, p->dk = XH_REGION;) \
 X(dc_palette,    0u,0,0, p->dk = XH_PALETTE;) \
 X(dc_stockpen,   0u,0,0, p->dk = XH_STOCKPEN;) \
 X(dc_hwnd,       0u,0,0, p->dk = XH_HWND;) \
 X(dc_font,       0u,0,0, p->dk = XH_FONT;)

static void sd_run(const char *nm, void (*mut)(struct sdp *), unsigned long long expv, int experr, int expchg) {
    REQUIRE(UN(NtGdiStretchDIBitsInternal));
    XHS(x); struct xb_surf s;
    if (!xb_surf_open(&s, 6, 6)) { xhs_close(&x); xb_surf_close(&s); t_skip("surface"); return; }
    struct sdp *p = (struct sdp *)calloc(1, sizeof *p); sd_base(p); mut(p);
    HDC dc = p->dk < 0 ? s.dc : (HDC)x.h[p->dk];
    SetLastError(0);
    int r = UN(NtGdiStretchDIBitsInternal)(dc, p->xD, p->yD, p->cxD, p->cyD, p->xS, p->yS, p->cxS, p->cyS, p->bits, p->bmi, p->usage, p->rop, p->cjInfo, p->cjBits, p->hcm);
    DWORD e = GetLastError(); U(GdiFlush)();
    int chg = sd_changed(&s, 0xFFFFFF);
    CHECKF((unsigned)r == (unsigned)expv, "%s: returned %d want %llu", nm, r, expv);
    CHECKF(e == (DWORD)experr, "%s: error %lu want %d", nm, (unsigned long)e, experr);
    CHECKF(chg == expchg, "%s: %d pixels changed want %d", nm, chg, expchg);
    free(p); xb_surf_close(&s); xhs_close(&x);
}
#define SM(nm, a, b, c, ...) static void sm_##nm(struct sdp *p) { __VA_ARGS__ }
SDROWS(SM)
struct srow { const char *nm; void (*mut)(struct sdp *); unsigned long long ev; int ee, ec; };
#define SS(nm, a, b, c, ...) { #nm, sm_##nm, a, b, c },
static const struct srow srows[] = { SDROWS(SS) };
static void sd_row(const struct srow *r) { sd_run(r->nm, r->mut, r->ev, r->ee, r->ec); }
XB_GROUP(stretch_dest_rectangle, srows, sd_row, "base|dest|scale|zero_dest|neg_dest|huge_dest|min_dest")
XB_GROUP(stretch_source_rectangle, srows, sd_row, "zero_src|neg_src|src_")
XB_GROUP(stretch_null_and_invalid_pointers, srows, sd_row, "null_|junk_")
XB_GROUP(stretch_byte_counts, srows, sd_row, "cjinfo|cjbits")
XB_GROUP(stretch_header_size, srows, sd_row, "hdr_size_")
XB_GROUP(stretch_header_format_fields, srows, sd_row, "hdr_planes|hdr_bpp|hdr_comp")
XB_GROUP(stretch_header_dimensions, srows, sd_row, "hdr_width|hdr_height|hdr_clr|hdr_sizeimage")
XB_GROUP(stretch_colour_usage, srows, sd_row, "usage")
XB_GROUP(stretch_raster_operations, srows, sd_row, "rop")
XB_GROUP(stretch_colour_transform_handle, srows, sd_row, "hcm")
XB_GROUP(stretch_dc_handle_types, srows, sd_row, "dc_")

/* ---- pixel consistency ------------------------------------------------------------------------------------------ */
static void sd_draw(struct xb_surf *s, struct sdp *p) {
    p->bits = p->pbits; U(GdiFlush)();
    UN(NtGdiStretchDIBitsInternal)(s->dc, p->xD, p->yD, p->cxD, p->cyD, p->xS, p->yS, p->cxS, p->cyS, p->bits, p->bmi, p->usage, p->rop, p->cjInfo, p->cjBits, p->hcm);
    U(GdiFlush)();
}
#define SDX(s, p) REQUIRE(UN(NtGdiStretchDIBitsInternal)); struct xb_surf s; struct sdp *p = (struct sdp *)calloc(1, sizeof *p); sd_base(p); \
    if (!xb_surf_open(&s, 6, 6)) { xb_surf_close(&s); free(p); t_skip("surface"); return; }
XT(stretch_pixels_one_to_one) {
    SDX(s, p); sd_draw(&s, p);
    CHECK(xb_px(&s, 1, 1) == RED && xb_px(&s, 2, 1) == GRN && xb_px(&s, 1, 2) == BLU && xb_px(&s, 2, 2) == YEL);
    CHECKF(sd_changed(&s, 0xFFFFFF) == 4, "%d pixels changed", sd_changed(&s, 0xFFFFFF));
    xb_surf_close(&s); free(p);
}
XT(stretch_return_value_is_scan_line_count) {
    SDX(s, p);
    int r = UN(NtGdiStretchDIBitsInternal)(s.dc, 1, 1, 2, 2, 0, 0, 2, 2, p->bits, p->bmi, 0, SRCCOPY, 40, 16, NULL);
    CHECKF(r == 2, "returned %d", r);
    r = UN(NtGdiStretchDIBitsInternal)(s.dc, 1, 1, 1, 1, 0, 1, 1, 1, p->bits, p->bmi, 0, SRCCOPY, 40, 16, NULL);
    CHECKF(r == 2, "copying the second scan line alone returned %d", r);   /* the count includes the skipped first line */
    xb_surf_close(&s); free(p);
}
XT(stretch_pixels_scale_up_blocks) {
    SDX(s, p); p->xD = 0; p->yD = 0; p->cxD = 4; p->cyD = 4; sd_draw(&s, p);
    int bad = 0;
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) { DWORD want = (y < 2) ? (x < 2 ? RED : GRN) : (x < 2 ? BLU : YEL); if (xb_px(&s, x, y) != want) bad++; }
    CHECKF(bad == 0, "%d of 16 pixels differ from 2x2 blocks", bad);
    CHECK(xb_px(&s, 4, 0) == 0xFFFFFF && xb_px(&s, 0, 4) == 0xFFFFFF);
    xb_surf_close(&s); free(p);
}
XT(stretch_pixels_source_subrect) {
    SDX(s, p); p->xS = 1; p->yS = 0; p->cxS = 1; p->cyS = 2; p->cxD = 1; sd_draw(&s, p);
    CHECK(xb_px(&s, 1, 1) == GRN && xb_px(&s, 1, 2) == YEL && xb_px(&s, 2, 1) == 0xFFFFFF);
    xb_surf_close(&s); free(p);
}
XT(stretch_pixels_top_down_source_same_picture) {
    SDX(s, p);
    p->h->biHeight = -2;
    static const BYTE px[16] = { 0,0,255, 0,255,0, 0,0, 255,0,0, 0,255,255, 0,0 };    /* top-down: red green / blue yellow */
    memcpy(p->pbits, px, 16); sd_draw(&s, p);
    CHECK(xb_px(&s, 1, 1) == RED && xb_px(&s, 2, 1) == GRN && xb_px(&s, 1, 2) == BLU && xb_px(&s, 2, 2) == YEL);
    xb_surf_close(&s); free(p);
}
XT(stretch_pixels_not_srccopy) {
    SDX(s, p); p->rop = NOTSRCCOPY; sd_draw(&s, p);
    CHECK(xb_px(&s, 1, 1) == (~RED & 0xFFFFFF) && xb_px(&s, 2, 2) == (~YEL & 0xFFFFFF));
    xb_surf_close(&s); free(p);
}
XT(stretch_pixels_blackness_ignores_bits) {
    SDX(s, p); p->rop = BLACKNESS; sd_draw(&s, p);
    CHECK(xb_px(&s, 1, 1) == 0 && xb_px(&s, 2, 2) == 0 && xb_px(&s, 0, 0) == 0xFFFFFF);
    xb_surf_close(&s); free(p);
}
XT(stretch_pixels_whiteness_ignores_bits) {
    SDX(s, p); for (int i = 0; i < 36; i++) s.px[i] = 0; p->rop = WHITENESS; sd_draw(&s, p);
    CHECK(xb_px(&s, 1, 1) == 0xFFFFFF && xb_px(&s, 2, 2) == 0xFFFFFF && xb_px(&s, 0, 0) == 0);
    xb_surf_close(&s); free(p);
}
XT(stretch_pixels_dstinvert) {
    SDX(s, p); for (int i = 0; i < 36; i++) s.px[i] = 0x123456; p->rop = DSTINVERT; sd_draw(&s, p);
    CHECK(xb_px(&s, 1, 1) == (~0x123456u & 0xFFFFFF) && xb_px(&s, 0, 0) == 0x123456);
    xb_surf_close(&s); free(p);
}
XT(stretch_pixels_srcinvert_twice_restores) {
    SDX(s, p); for (int i = 0; i < 36; i++) s.px[i] = 0x123456; p->rop = SRCINVERT; sd_draw(&s, p);
    CHECK(xb_px(&s, 1, 1) == (0x123456u ^ RED));
    sd_draw(&s, p);
    int bad = 0; for (int i = 0; i < 36; i++) if ((s.px[i] & 0xFFFFFF) != 0x123456) bad++;
    CHECKF(bad == 0, "%d pixels not restored", bad);
    xb_surf_close(&s); free(p);
}
XT(stretch_rejected_call_leaves_surface_untouched) {
    SDX(s, p); p->cjInfo = 0; sd_draw(&s, p);
    CHECKF(sd_changed(&s, 0xFFFFFF) == 0, "%d pixels changed after a rejected call", sd_changed(&s, 0xFFFFFF));
    xb_surf_close(&s); free(p);
}
/* Source depths: the same picture through palette-based and 16/32-bit sources. */
static void depth_case(int bpp) {
    SDX(s, p);
    BYTE *b = p->pbits; memset(b, 0, 64); size_t hl = 40; BITMAPINFOHEADER *h = p->h; h->biBitCount = (WORD)bpp;
    unsigned nc = bpp <= 8 ? (1u << bpp) : 0; if (bpp == 8) nc = 4; if (bpp <= 8) h->biClrUsed = nc;
    RGBQUAD *pal = (RGBQUAD *)(p->hdr + hl);
    DWORD cols[4] = { RED, GRN, BLU, YEL };
    for (unsigned i = 0; i < nc && i < 4; i++) { pal[i].rgbRed = (BYTE)(cols[i] >> 16); pal[i].rgbGreen = (BYTE)(cols[i] >> 8); pal[i].rgbBlue = (BYTE)cols[i]; }
    unsigned st = xb_stride(2, bpp);
    /* bottom-up: memory row 0 is the bottom line: blue yellow (palette 2,3), row 1: red green (0,1) */
    for (int row = 0; row < 2; row++) for (int x = 0; x < 2; x++) {
        int idx = (row == 0 ? 2 : 0) + x; DWORD c = cols[idx]; BYTE *q = b + row * st;
        if (bpp == 8) q[x] = (BYTE)idx; else if (bpp == 4) q[0] |= (BYTE)(x ? idx : idx << 4); else if (bpp == 1) q[0] |= 0;
        else if (bpp == 24) { q[x*3] = (BYTE)c; q[x*3+1] = (BYTE)(c >> 8); q[x*3+2] = (BYTE)(c >> 16); }
        else if (bpp == 32) { ((DWORD *)q)[x] = c; }
        else if (bpp == 16) { ((WORD *)q)[x] = (WORD)(((c >> 19) & 31) << 10 | ((c >> 11) & 31) << 5 | ((c >> 3) & 31)); }
    }
    p->cjInfo = (UINT)(hl + nc * 4); p->cjBits = st * 2; p->bmi = (BITMAPINFO *)p->hdr;
    if (bpp == 1) { t_skip("1bpp is covered by the monochrome rows"); xb_surf_close(&s); free(p); return; }
    sd_draw(&s, p);
    CHECKF(xb_px(&s, 1, 1) == RED && xb_px(&s, 2, 1) == GRN && xb_px(&s, 1, 2) == BLU && xb_px(&s, 2, 2) == YEL,
           "%d bpp source: pixels %06lx %06lx %06lx %06lx", bpp, (unsigned long)xb_px(&s, 1, 1), (unsigned long)xb_px(&s, 2, 1), (unsigned long)xb_px(&s, 1, 2), (unsigned long)xb_px(&s, 2, 2));
    xb_surf_close(&s); free(p);
}
XT(stretch_pixels_source_4bpp) { depth_case(4); }
XT(stretch_pixels_source_8bpp) { depth_case(8); }
XT(stretch_pixels_source_16bpp) { depth_case(16); }
XT(stretch_pixels_source_24bpp) { depth_case(24); }
XT(stretch_pixels_source_32bpp) { depth_case(32); }

/* The gdi32 export computes the byte counts itself and must paint the same picture as the Nt stub. */
XT(stretch_gdi32_export_matches_nt) {
    REQUIRE(U(StretchDIBits)); SDX(s, p);
    struct xb_surf t; if (!xb_surf_open(&t, 6, 6)) { t_skip("surface"); return; }
    sd_draw(&s, p);
    int r = U(StretchDIBits)(t.dc, 1, 1, 2, 2, 0, 0, 2, 2, p->pbits, p->bmi, DIB_RGB_COLORS, SRCCOPY);
    CHECKF(r == 2, "StretchDIBits returned %d", r);
    int bad = 0; for (int y = 0; y < 6; y++) for (int x = 0; x < 6; x++) if (xb_px(&s, x, y) != xb_px(&t, x, y)) bad++;
    CHECKF(bad == 0, "%d pixels differ between the routes", bad);
    xb_surf_close(&t); xb_surf_close(&s); free(p);
}
XT(stretch_gdi32_export_bad_dc) {
    REQUIRE(U(StretchDIBits)); XHS(x); SDX(s, p);
    for (int k = XH_NULL; k <= XH_MINUS1; k++) {
        SetLastError(0);
        int r = U(StretchDIBits)((HDC)x.h[k], 0, 0, 2, 2, 0, 0, 2, 2, p->pbits, p->bmi, DIB_RGB_COLORS, SRCCOPY);
        CHECKF(r == 0, "%s: StretchDIBits returned %d", xh_nm[k], r);
    }
    xb_surf_close(&s); free(p); xhs_close(&x);
}
