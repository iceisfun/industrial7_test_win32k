/* gdi_blit_raster: NtGdiMaskBlt, NtGdiPlgBlt, NtGdiAlphaBlend, NtGdiTransparentBlt, NtGdiGradientFill. */
#include "br.h"

#define SENT 0x00DEAD
#define ROP4(fore, back) ((((DWORD)(back)) << 24) | (((DWORD)(fore)) << 16))
#define BLEND(op, flags, sca, fmt) ((DWORD)(op) | ((DWORD)(flags) << 8) | ((DWORD)(sca) << 16) | ((DWORD)(fmt) << 24))

typedef struct { BR s, d; } SD;
static int sd_make(SD *p, int sw, int sh, int dw, int dh) {
    memset(p, 0, sizeof *p);
    p->s = br_make(sw, sh); p->d = br_make(dw, dh);
    if (!p->s.dc || !p->d.dc) return 0;
    br_fill_pat(&p->s); br_fill(&p->d, SENT);
    return 1;
}
static void sd_free(SD *p) { br_free(&p->s); br_free(&p->d); }
#define NEEDSD(p, sw, sh, dw, dh) SD p; if (!sd_make(&p, sw, sh, dw, dh)) { sd_free(&p); t_skip("no DIB/DC"); return; }

/* 1bpp mask bitmap, one byte per row (MSB = leftmost column), width <= 8. */
static HBITMAP mk_mask(int w, int h, const unsigned char *rows) {
    WORD buf[16]; memset(buf, 0, sizeof buf);
    for (int y = 0; y < h && y < 16; y++) buf[y] = (WORD)(rows[y] << 0);   /* byte0 = leftmost 8 pixels; high byte is padding */
    return U(CreateBitmap)(w, h, 1, 1, buf);
}
static int mask_bit(const unsigned char *rows, int x, int y) { return (rows[y] >> (7 - x)) & 1; }
static const unsigned char CHECKER[8] = { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 };

/* ======================= MaskBlt ======================= */
/* ROP4: bits 16..23 apply where the mask bit is 1, bits 24..31 where it is 0 (MAKEROP4). */
TEST(gdi_blit_raster_maskblt_foreground_copy_background_keep) {
    NT(NtGdiMaskBlt); NEEDSD(p, 8, 8, 8, 8);
    HBITMAP m = mk_mask(8, 8, CHECKER); CHECK(m != NULL);
    BOOL r = p_NtGdiMaskBlt(p.d.dc, 0, 0, 8, 8, p.s.dc, 0, 0, m, 0, 0, ROP4(0xCC, 0xAA), CLR_BAD); br_flush();
    CHECK(r);
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++)
        CHECKF(br_at(&p.d, x, y) == (mask_bit(CHECKER, x, y) ? br_pat(x, y) : SENT), "(%d,%d)", x, y);
    U(DeleteObject)(m); sd_free(&p);
}

TEST(gdi_blit_raster_maskblt_background_rop_applies_where_mask_zero) {
    NT(NtGdiMaskBlt); NEEDSD(p, 8, 8, 8, 8);
    HBITMAP m = mk_mask(8, 8, CHECKER);
    BOOL r = p_NtGdiMaskBlt(p.d.dc, 0, 0, 8, 8, p.s.dc, 0, 0, m, 0, 0, ROP4(0xCC, 0x33), CLR_BAD); br_flush();
    CHECK(r);
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++)
        CHECKF(br_at(&p.d, x, y) == (mask_bit(CHECKER, x, y) ? br_pat(x, y) : (br_pat(x, y) ^ M24)), "(%d,%d)", x, y);
    U(DeleteObject)(m); sd_free(&p);
}

TEST(gdi_blit_raster_maskblt_blackness_and_whiteness_planes) {
    NT(NtGdiMaskBlt); NEEDSD(p, 8, 8, 8, 8);
    HBITMAP m = mk_mask(8, 8, CHECKER);
    BOOL r = p_NtGdiMaskBlt(p.d.dc, 0, 0, 8, 8, p.s.dc, 0, 0, m, 0, 0, ROP4(0xFF, 0x00), CLR_BAD); br_flush();
    CHECK(r);
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++)
        CHECKF(br_at(&p.d, x, y) == (mask_bit(CHECKER, x, y) ? 0xFFFFFF : 0), "(%d,%d)", x, y);
    U(DeleteObject)(m); sd_free(&p);
}

TEST(gdi_blit_raster_maskblt_all_zero_mask_uses_background_only) {
    NT(NtGdiMaskBlt); NEEDSD(p, 8, 8, 8, 8);
    static const unsigned char z[8] = { 0 };
    HBITMAP m = mk_mask(8, 8, z);
    BOOL r = p_NtGdiMaskBlt(p.d.dc, 0, 0, 8, 8, p.s.dc, 0, 0, m, 0, 0, ROP4(0xCC, 0xAA), CLR_BAD); br_flush();
    CHECK(r);
    CHECK(br_all(&p.d, 0, 0, 8, 8, SENT));
    U(DeleteObject)(m); sd_free(&p);
}

TEST(gdi_blit_raster_maskblt_all_one_mask_uses_foreground_only) {
    NT(NtGdiMaskBlt); NEEDSD(p, 8, 8, 8, 8);
    static const unsigned char o[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    HBITMAP m = mk_mask(8, 8, o);
    BOOL r = p_NtGdiMaskBlt(p.d.dc, 0, 0, 8, 8, p.s.dc, 0, 0, m, 0, 0, ROP4(0xCC, 0x00), CLR_BAD); br_flush();
    CHECK(r);
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) CHECK(br_at(&p.d, x, y) == br_pat(x, y));
    U(DeleteObject)(m); sd_free(&p);
}

TEST(gdi_blit_raster_maskblt_mask_origin_offsets) {
    NT(NtGdiMaskBlt); NEEDSD(p, 8, 8, 8, 8);
    HBITMAP m = mk_mask(8, 8, CHECKER);
    BOOL r = p_NtGdiMaskBlt(p.d.dc, 0, 0, 4, 4, p.s.dc, 0, 0, m, 1, 0, ROP4(0xCC, 0xAA), CLR_BAD); br_flush();
    CHECK(r);
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++)
        CHECKF(br_at(&p.d, x, y) == (mask_bit(CHECKER, x + 1, y) ? br_pat(x, y) : SENT), "(%d,%d)", x, y);
    CHECK(br_at(&p.d, 4, 0) == SENT);
    U(DeleteObject)(m); sd_free(&p);
}

TEST(gdi_blit_raster_maskblt_source_and_dest_offsets) {
    NT(NtGdiMaskBlt); NEEDSD(p, 8, 8, 8, 8);
    HBITMAP m = mk_mask(8, 8, CHECKER);
    BOOL r = p_NtGdiMaskBlt(p.d.dc, 2, 3, 4, 4, p.s.dc, 3, 1, m, 0, 0, ROP4(0xCC, 0xAA), CLR_BAD); br_flush();
    CHECK(r);
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {
        int inside = x >= 2 && x < 6 && y >= 3 && y < 7;
        DWORD want = (inside && mask_bit(CHECKER, x - 2, y - 3)) ? br_pat(x - 2 + 3, y - 3 + 1) : SENT;
        CHECKF(br_at(&p.d, x, y) == want, "(%d,%d)", x, y);
    }
    U(DeleteObject)(m); sd_free(&p);
}

TEST(gdi_blit_raster_maskblt_null_mask_is_bitblt) {
    NT(NtGdiMaskBlt); NEEDSD(p, 8, 8, 8, 8);
    BOOL r = p_NtGdiMaskBlt(p.d.dc, 0, 0, 8, 8, p.s.dc, 0, 0, NULL, 0, 0, ROP4(0xCC, 0x00), CLR_BAD); br_flush();
    CHECK(r);
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) CHECK(br_at(&p.d, x, y) == br_pat(x, y));
    sd_free(&p);
}

TEST(gdi_blit_raster_maskblt_userland_matches_syscall) {
    NEEDSD(p, 8, 8, 8, 8);
    HBITMAP m = mk_mask(8, 8, CHECKER);
    BOOL r = U(MaskBlt)(p.d.dc, 0, 0, 8, 8, p.s.dc, 0, 0, m, 0, 0, ROP4(0xCC, 0xAA)); br_flush();
    CHECK(r);
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++)
        CHECKF(br_at(&p.d, x, y) == (mask_bit(CHECKER, x, y) ? br_pat(x, y) : SENT), "(%d,%d)", x, y);
    U(DeleteObject)(m); sd_free(&p);
}

TEST(gdi_blit_raster_maskblt_mask_must_be_monochrome) {
    NEEDSD(p, 8, 8, 8, 8);
    HBITMAP color = U(CreateBitmap)(8, 8, 1, 32, NULL);
    CHECK(color != NULL);
    BOOL r = U(MaskBlt)(p.d.dc, 0, 0, 8, 8, p.s.dc, 0, 0, color, 0, 0, ROP4(0xCC, 0xAA)); br_flush();
    CHECK(!r);
    CHECK(br_all(&p.d, 0, 0, 8, 8, SENT));
    U(DeleteObject)(color); sd_free(&p);
}

TEST(gdi_blit_raster_maskblt_null_src_dc_userland_fails) {
    NEEDSD(p, 8, 8, 8, 8);
    HBITMAP m = mk_mask(8, 8, CHECKER);
    /* gdi32full!MaskBlt: hdcSrc == NULL returns 0 before any syscall */
    CHECK(!U(MaskBlt)(p.d.dc, 0, 0, 8, 8, NULL, 0, 0, m, 0, 0, ROP4(0xCC, 0xAA)));
    CHECK(!U(MaskBlt)(NULL, 0, 0, 8, 8, p.s.dc, 0, 0, m, 0, 0, ROP4(0xCC, 0xAA)));
    CHECK(br_all(&p.d, 0, 0, 8, 8, SENT));
    U(DeleteObject)(m); sd_free(&p);
}

TEST(gdi_blit_raster_maskblt_deleted_dest_dc_fails) {
    NT(NtGdiMaskBlt); NEEDSD(p, 8, 8, 8, 8);
    HBITMAP m = mk_mask(8, 8, CHECKER);
    BR t = br_make(8, 8); HDC dead = t.dc; br_free(&t);
    CHECK(!p_NtGdiMaskBlt(dead, 0, 0, 8, 8, p.s.dc, 0, 0, m, 0, 0, ROP4(0xCC, 0xAA), CLR_BAD));
    CHECK(!p_NtGdiMaskBlt(p.d.dc, 0, 0, 8, 8, dead, 0, 0, m, 0, 0, ROP4(0xCC, 0xAA), CLR_BAD));
    U(DeleteObject)(m); sd_free(&p);
}

/* ======================= PlgBlt ======================= */
TEST(gdi_blit_raster_plgblt_identity_mapping_is_copy) {
    NT(NtGdiPlgBlt); NEEDSD(p, 8, 8, 8, 8);
    POINT pt[3] = { { 0, 0 }, { 8, 0 }, { 0, 8 } };
    BOOL r = p_NtGdiPlgBlt(p.d.dc, pt, p.s.dc, 0, 0, 8, 8, NULL, 0, 0, CLR_BAD); br_flush();
    CHECK(r);
    int bad = 0;
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) if (br_at(&p.d, x, y) != br_pat(x, y)) bad++;
    CHECKF(bad == 0, "%d pixels differ from source", bad);
    sd_free(&p);
}

TEST(gdi_blit_raster_plgblt_translation) {
    NT(NtGdiPlgBlt); NEEDSD(p, 8, 8, 12, 12);
    POINT pt[3] = { { 3, 2 }, { 7, 2 }, { 3, 6 } };
    BOOL r = p_NtGdiPlgBlt(p.d.dc, pt, p.s.dc, 2, 2, 4, 4, NULL, 0, 0, CLR_BAD); br_flush();
    CHECK(r);
    int bad = 0;
    for (int y = 0; y < 12; y++) for (int x = 0; x < 12; x++) {
        DWORD want = (x >= 3 && x < 7 && y >= 2 && y < 6) ? br_pat(x - 3 + 2, y - 2 + 2) : SENT;
        if (br_at(&p.d, x, y) != want) bad++;
    }
    CHECKF(bad == 0, "%d wrong pixels", bad);
    sd_free(&p);
}

TEST(gdi_blit_raster_plgblt_userland_identity) {
    NEEDSD(p, 6, 6, 6, 6);
    POINT pt[3] = { { 0, 0 }, { 6, 0 }, { 0, 6 } };
    BOOL r = U(PlgBlt)(p.d.dc, pt, p.s.dc, 0, 0, 6, 6, NULL, 0, 0); br_flush();
    CHECK(r);
    for (int y = 0; y < 6; y++) for (int x = 0; x < 6; x++) CHECKF(br_at(&p.d, x, y) == br_pat(x, y), "(%d,%d)", x, y);
    sd_free(&p);
}

TEST(gdi_blit_raster_plgblt_horizontal_mirror) {
    NT(NtGdiPlgBlt); NEEDSD(p, 6, 4, 6, 4);
    POINT pt[3] = { { 6, 0 }, { 0, 0 }, { 6, 4 } };   /* UL<->UR swapped */
    BOOL r = p_NtGdiPlgBlt(p.d.dc, pt, p.s.dc, 0, 0, 6, 4, NULL, 0, 0, CLR_BAD); br_flush();
    CHECK(r);
    int bad = 0;
    for (int y = 0; y < 4; y++) for (int x = 0; x < 6; x++) if (br_at(&p.d, x, y) != br_pat(5 - x, y)) bad++;
    t_note("mirrored PlgBlt differs from the exact mirror in %d pixels", bad);
    CHECK(br_count(&p.d, 0, 0, 6, 4, SENT) < 6 * 4);
    sd_free(&p);
}

TEST(gdi_blit_raster_plgblt_with_mask) {
    NT(NtGdiPlgBlt); NEEDSD(p, 8, 8, 8, 8);
    HBITMAP m = mk_mask(8, 8, CHECKER);
    POINT pt[3] = { { 0, 0 }, { 8, 0 }, { 0, 8 } };
    BOOL r = p_NtGdiPlgBlt(p.d.dc, pt, p.s.dc, 0, 0, 8, 8, m, 0, 0, CLR_BAD); br_flush();
    CHECK(r);
    int bad = 0;
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) if (br_at(&p.d, x, y) != (mask_bit(CHECKER, x, y) ? br_pat(x, y) : SENT)) bad++;
    CHECKF(bad == 0, "%d wrong pixels", bad);
    U(DeleteObject)(m); sd_free(&p);
}

TEST(gdi_blit_raster_plgblt_null_dcs_fail) {
    NEEDSD(p, 6, 6, 6, 6);
    POINT pt[3] = { { 0, 0 }, { 6, 0 }, { 0, 6 } };
    CHECK(!U(PlgBlt)(NULL, pt, p.s.dc, 0, 0, 6, 6, NULL, 0, 0));
    CHECK(!U(PlgBlt)(p.d.dc, pt, NULL, 0, 0, 6, 6, NULL, 0, 0));
    CHECK(br_all(&p.d, 0, 0, 6, 6, SENT));
    sd_free(&p);
}

/* ======================= AlphaBlend ======================= */
static DWORD ch(DWORD px, int c) { return (px >> (c * 8)) & 0xff; }
static int within(DWORD a, DWORD b, int tol) { for (int c = 0; c < 3; c++) { int d = (int)ch(a, c) - (int)ch(b, c); if (d < -tol || d > tol) return 0; } return 1; }

TEST(gdi_blit_raster_alphablend_constant_255_copies_source) {
    NT(NtGdiAlphaBlend); NEEDSD(p, 6, 6, 6, 6);
    BOOL r = p_NtGdiAlphaBlend(p.d.dc, 0, 0, 6, 6, p.s.dc, 0, 0, 6, 6, BLEND(AC_SRC_OVER, 0, 255, 0), 0); br_flush();
    CHECK(r);
    for (int y = 0; y < 6; y++) for (int x = 0; x < 6; x++) CHECKF(br_at(&p.d, x, y) == br_pat(x, y), "(%d,%d) got %06x", x, y, br_at(&p.d, x, y));
    sd_free(&p);
}

TEST(gdi_blit_raster_alphablend_constant_0_leaves_dest) {
    NT(NtGdiAlphaBlend); NEEDSD(p, 6, 6, 6, 6);
    BOOL r = p_NtGdiAlphaBlend(p.d.dc, 0, 0, 6, 6, p.s.dc, 0, 0, 6, 6, BLEND(AC_SRC_OVER, 0, 0, 0), 0); br_flush();
    CHECK(r);
    CHECK(br_all(&p.d, 0, 0, 6, 6, SENT));
    sd_free(&p);
}

/* result = (S*a + D*(255-a)) / 255 : within 1 lsb per channel (rounding is implementation-defined). */
static void const_alpha_case(int a) {
    NT(NtGdiAlphaBlend); NEEDSD(p, 6, 6, 6, 6);
    BOOL r = p_NtGdiAlphaBlend(p.d.dc, 0, 0, 6, 6, p.s.dc, 0, 0, 6, 6, BLEND(AC_SRC_OVER, 0, a, 0), 0); br_flush();
    CHECKF(r, "a=%d", a);
    int exact = 0, tot = 0;
    for (int y = 0; y < 6; y++) for (int x = 0; x < 6; x++) {
        DWORD s = br_pat(x, y), d = SENT, want = 0;
        for (int c = 0; c < 3; c++) want |= ((ch(s, c) * a + ch(d, c) * (255 - a)) / 255) << (c * 8);
        CHECKF(within(br_at(&p.d, x, y), want, 1), "a=%d (%d,%d) got %06x want %06x", a, x, y, br_at(&p.d, x, y), want);
        tot++; if (br_at(&p.d, x, y) == want) exact++;
    }
    t_note("constant alpha %d: %d/%d pixels equal truncating division", a, exact, tot);
    sd_free(&p);
}
#define CONST_ALPHA(a) TEST(gdi_blit_raster_alphablend_constant_##a) { const_alpha_case(a); }
CONST_ALPHA(1) CONST_ALPHA(64) CONST_ALPHA(127) CONST_ALPHA(128) CONST_ALPHA(192) CONST_ALPHA(254)

/* AC_SRC_ALPHA: source is premultiplied ARGB, result = S + D*(255-Sa)/255 (SCA = 255). */
static void per_pixel_case(int viaUserland) {
    NEEDSD(p, 4, 4, 4, 4);
    NT(NtGdiAlphaBlend);
    static const int alphas[] = { 0, 17, 64, 128, 200, 255 };
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) {
        DWORD a = alphas[(x + y) % 6];
        DWORD r = (0x80 * a) / 255, g = (0x40 * a) / 255, b = (0xF0 * a) / 255;          /* premultiplied */
        br_set(&p.s, x, y, (a << 24) | (r << 16) | (g << 8) | b);
        br_set(&p.d, x, y, 0x00204060 + x * 0x010101 + y * 0x020202);
    }
    BOOL ok;
    if (viaUserland) {
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        if (!MS(AlphaBlend)) { t_skip("msimg32!AlphaBlend missing"); sd_free(&p); return; }
        ok = MS(AlphaBlend)(p.d.dc, 0, 0, 4, 4, p.s.dc, 0, 0, 4, 4, bf);
    } else
        ok = p_NtGdiAlphaBlend(p.d.dc, 0, 0, 4, 4, p.s.dc, 0, 0, 4, 4, BLEND(AC_SRC_OVER, 0, 255, AC_SRC_ALPHA), 0);
    br_flush();
    CHECK(ok);
    int exact = 0;
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) {
        DWORD s = p.s.px[y * 4 + x], a = s >> 24, d = 0x00204060 + x * 0x010101 + y * 0x020202, want = 0;
        for (int c = 0; c < 3; c++) want |= (ch(s, c) + ch(d, c) * (255 - a) / 255) << (c * 8);
        CHECKF(within(br_at(&p.d, x, y), want, 1), "a=%lu (%d,%d) got %06x want %06x", a, x, y, br_at(&p.d, x, y), want);
        if (br_at(&p.d, x, y) == want) exact++;
    }
    t_note("per-pixel alpha: %d/16 exact against truncating model", exact);
    sd_free(&p);
}
TEST(gdi_blit_raster_alphablend_per_pixel_premultiplied_syscall) { per_pixel_case(0); }
TEST(gdi_blit_raster_alphablend_per_pixel_premultiplied_userland) { per_pixel_case(1); }

TEST(gdi_blit_raster_alphablend_per_pixel_alpha_255_replaces_and_0_keeps) {
    NT(NtGdiAlphaBlend); NEEDSD(p, 2, 1, 2, 1);
    br_set(&p.s, 0, 0, 0xFF000000 | 0x123456);   /* opaque */
    br_set(&p.s, 1, 0, 0x00000000);              /* fully transparent, premultiplied colour is zero */
    br_set(&p.d, 0, 0, 0x00AABBCC); br_set(&p.d, 1, 0, 0x00AABBCC);
    BOOL r = p_NtGdiAlphaBlend(p.d.dc, 0, 0, 2, 1, p.s.dc, 0, 0, 2, 1, BLEND(AC_SRC_OVER, 0, 255, AC_SRC_ALPHA), 0); br_flush();
    CHECK(r);
    CHECKF(br_at(&p.d, 0, 0) == 0x123456, "opaque got %06x", br_at(&p.d, 0, 0));
    CHECKF(br_at(&p.d, 1, 0) == 0xAABBCC, "transparent got %06x", br_at(&p.d, 1, 0));
    sd_free(&p);
}

TEST(gdi_blit_raster_alphablend_per_pixel_with_constant_alpha) {
    NT(NtGdiAlphaBlend); NEEDSD(p, 2, 1, 2, 1);
    br_set(&p.s, 0, 0, 0xFF000000 | 0xC8C8C8);
    br_set(&p.s, 1, 0, 0x80000000 | 0x404040);
    br_set(&p.d, 0, 0, 0x00000000); br_set(&p.d, 1, 0, 0x00FFFFFF);
    BOOL r = p_NtGdiAlphaBlend(p.d.dc, 0, 0, 2, 1, p.s.dc, 0, 0, 2, 1, BLEND(AC_SRC_OVER, 0, 128, AC_SRC_ALPHA), 0); br_flush();
    CHECK(r);
    /* S' = S*SCA/255, Sa' = Sa*SCA/255, D' = S' + D*(255-Sa')/255 */
    int sa0 = 255 * 128 / 255, s0 = 0xC8 * 128 / 255;
    DWORD w0 = (s0 + 0) & 0xff; (void)sa0;
    CHECKF(within(br_at(&p.d, 0, 0), w0 * 0x010101, 1), "opaque src half constant: got %06x want ~%02lx", br_at(&p.d, 0, 0), w0);
    int sa1 = 0x80 * 128 / 255, s1 = 0x40 * 128 / 255;
    DWORD w1 = s1 + 0xFF * (255 - sa1) / 255;
    CHECKF(within(br_at(&p.d, 1, 0), w1 * 0x010101, 2), "got %06x want ~%02lx", br_at(&p.d, 1, 0), w1);
    sd_free(&p);
}

TEST(gdi_blit_raster_alphablend_userland_constant) {
    NEEDSD(p, 4, 4, 4, 4);
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, 0 };
    if (!MS(AlphaBlend)) { t_skip("msimg32!AlphaBlend missing"); sd_free(&p); return; }
    BOOL r = MS(AlphaBlend)(p.d.dc, 0, 0, 4, 4, p.s.dc, 0, 0, 4, 4, bf); br_flush();
    CHECK(r);
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) CHECK(br_at(&p.d, x, y) == br_pat(x, y));
    sd_free(&p);
}

/* NtGdiAlphaBlend validates the packed BLENDFUNCTION first: low byte (BlendOp) must be 0 and the top
 * byte (AlphaFormat) must be 0 or 1; otherwise ERROR_INVALID_PARAMETER and a FALSE return. */
TEST(gdi_blit_raster_alphablend_invalid_blendop) {
    NT(NtGdiAlphaBlend); NEEDSD(p, 4, 4, 4, 4);
    static const DWORD bad[] = { 1, 2, 0x7f, 0xff, 0x100ff };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(0);
        BOOL r = p_NtGdiAlphaBlend(p.d.dc, 0, 0, 4, 4, p.s.dc, 0, 0, 4, 4, bad[i] | (255u << 16), 0);
        CHECKF(!r, "BlendOp %lx accepted", bad[i]);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
    br_flush();
    CHECK(br_all(&p.d, 0, 0, 4, 4, SENT));
    sd_free(&p);
}

TEST(gdi_blit_raster_alphablend_invalid_alphaformat) {
    NT(NtGdiAlphaBlend); NEEDSD(p, 4, 4, 4, 4);
    for (unsigned f = 2; f < 256; f += (f < 8 ? 1 : 61)) {
        SetLastError(0);
        BOOL r = p_NtGdiAlphaBlend(p.d.dc, 0, 0, 4, 4, p.s.dc, 0, 0, 4, 4, BLEND(AC_SRC_OVER, 0, 255, f), 0);
        CHECKF(!r, "AlphaFormat %u accepted", f);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
    br_flush();
    CHECK(br_all(&p.d, 0, 0, 4, 4, SENT));
    sd_free(&p);
}

TEST(gdi_blit_raster_alphablend_invalid_fl_argument) {
    NT(NtGdiAlphaBlend); NEEDSD(p, 4, 4, 4, 4);
    t_note("blend-flags byte is not validated by the kernel; 12th argument is reserved (the callers pass 0)");
    SetLastError(0);
    BOOL r = p_NtGdiAlphaBlend(p.d.dc, 0, 0, 4, 4, p.s.dc, 0, 0, 4, 4, BLEND(AC_SRC_OVER, 0x55, 255, 0), 0);
    t_note("BlendFlags 0x55 returned %d", r);
    sd_free(&p);
}

/* A zero source width/height returns TRUE before any handle is even looked at. */
TEST(gdi_blit_raster_alphablend_zero_source_extent_succeeds_even_with_bad_dcs) {
    NT(NtGdiAlphaBlend);
    static const int e[][2] = { { 0, 4 }, { 4, 0 }, { 0, 0 } };
    for (int i = 0; i < 3; i++) {
        CHECK(p_NtGdiAlphaBlend(NULL, 0, 0, 4, 4, NULL, 0, 0, e[i][0], e[i][1], BLEND(AC_SRC_OVER, 0, 255, 0), 0) == TRUE);
        CHECK(p_NtGdiAlphaBlend((HDC)(ULONG_PTR)0x1234, 0, 0, 4, 4, (HDC)(ULONG_PTR)0x4321, 0, 0, e[i][0], e[i][1], BLEND(AC_SRC_OVER, 0, 255, 0), 0) == TRUE);
    }
    /* but the blend function is validated before the extent test */
    SetLastError(0);
    CHECK(!p_NtGdiAlphaBlend(NULL, 0, 0, 4, 4, NULL, 0, 0, 0, 0, BLEND(1, 0, 255, 0), 0));
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}

TEST(gdi_blit_raster_alphablend_null_dcs_fail) {
    NT(NtGdiAlphaBlend); NEEDSD(p, 4, 4, 4, 4);
    CHECK(!p_NtGdiAlphaBlend(NULL, 0, 0, 4, 4, p.s.dc, 0, 0, 4, 4, BLEND(AC_SRC_OVER, 0, 255, 0), 0));
    CHECK(!p_NtGdiAlphaBlend(p.d.dc, 0, 0, 4, 4, NULL, 0, 0, 4, 4, BLEND(AC_SRC_OVER, 0, 255, 0), 0));
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, 0 };
    CHECK(!MS(AlphaBlend) || !MS(AlphaBlend)(p.d.dc, 0, 0, 4, 4, NULL, 0, 0, 4, 4, bf));
    CHECK(br_all(&p.d, 0, 0, 4, 4, SENT));
    sd_free(&p);
}

TEST(gdi_blit_raster_alphablend_source_offsets_and_clip) {
    NT(NtGdiAlphaBlend); NEEDSD(p, 8, 8, 8, 8);
    HRGN rg = U(CreateRectRgn)(2, 2, 5, 5);
    U(SelectClipRgn)(p.d.dc, rg);
    BOOL r = p_NtGdiAlphaBlend(p.d.dc, 1, 1, 6, 6, p.s.dc, 2, 1, 6, 6, BLEND(AC_SRC_OVER, 0, 255, 0), 0); br_flush();
    CHECK(r);
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {
        DWORD want = (x >= 2 && x < 5 && y >= 2 && y < 5) ? br_pat(x - 1 + 2, y - 1 + 1) : SENT;
        CHECKF(br_at(&p.d, x, y) == want, "(%d,%d)", x, y);
    }
    U(SelectClipRgn)(p.d.dc, NULL); U(DeleteObject)(rg);
    sd_free(&p);
}

TEST(gdi_blit_raster_alphablend_stretch_constant_255_integer) {
    NT(NtGdiAlphaBlend); NEEDSD(p, 3, 3, 6, 6);
    BOOL r = p_NtGdiAlphaBlend(p.d.dc, 0, 0, 6, 6, p.s.dc, 0, 0, 3, 3, BLEND(AC_SRC_OVER, 0, 255, 0), 0); br_flush();
    CHECK(r);
    int exact = 0;
    for (int y = 0; y < 6; y++) for (int x = 0; x < 6; x++) if (br_at(&p.d, x, y) == br_pat(x / 2, y / 2)) exact++;
    t_note("stretched AlphaBlend 2x: %d/36 pixels equal replication", exact);
    CHECK(br_at(&p.d, 0, 0) == br_pat(0, 0));
    sd_free(&p);
}

/* ======================= TransparentBlt ======================= */
TEST(gdi_blit_raster_transparentblt_key_pixels_skipped) {
    NT(NtGdiTransparentBlt); NEEDSD(p, 6, 6, 6, 6);
    DWORD key = RGBPX(0xAA, 0x00, 0xAA);
    for (int y = 0; y < 6; y++) for (int x = 0; x < 6; x++) if ((x + y) % 3 == 0) br_set(&p.s, x, y, key);
    BOOL r = p_NtGdiTransparentBlt(p.d.dc, 0, 0, 6, 6, p.s.dc, 0, 0, 6, 6, PX2CR(key)); br_flush();
    CHECK(r);
    for (int y = 0; y < 6; y++) for (int x = 0; x < 6; x++)
        CHECKF(br_at(&p.d, x, y) == (((x + y) % 3 == 0) ? SENT : br_pat(x, y)), "(%d,%d) got %06x", x, y, br_at(&p.d, x, y));
    sd_free(&p);
}

TEST(gdi_blit_raster_transparentblt_key_is_exact_match) {
    NT(NtGdiTransparentBlt); NEEDSD(p, 4, 1, 4, 1);
    DWORD key = 0x808080;
    br_set(&p.s, 0, 0, key); br_set(&p.s, 1, 0, key + 1); br_set(&p.s, 2, 0, key - 0x010000); br_set(&p.s, 3, 0, key ^ 0x000100);
    BOOL r = p_NtGdiTransparentBlt(p.d.dc, 0, 0, 4, 1, p.s.dc, 0, 0, 4, 1, PX2CR(key)); br_flush();
    CHECK(r);
    CHECK(br_at(&p.d, 0, 0) == SENT);
    CHECK(br_at(&p.d, 1, 0) == key + 1);
    CHECK(br_at(&p.d, 2, 0) == key - 0x010000);
    CHECK(br_at(&p.d, 3, 0) == (key ^ 0x000100));
    sd_free(&p);
}

TEST(gdi_blit_raster_transparentblt_absent_key_copies_all) {
    NT(NtGdiTransparentBlt); NEEDSD(p, 6, 6, 6, 6);
    BOOL r = p_NtGdiTransparentBlt(p.d.dc, 0, 0, 6, 6, p.s.dc, 0, 0, 6, 6, RGB(1, 2, 3)); br_flush();
    CHECK(r);
    for (int y = 0; y < 6; y++) for (int x = 0; x < 6; x++) CHECK(br_at(&p.d, x, y) == br_pat(x, y));
    sd_free(&p);
}

TEST(gdi_blit_raster_transparentblt_all_key_changes_nothing) {
    NT(NtGdiTransparentBlt); NEEDSD(p, 4, 4, 4, 4);
    br_fill(&p.s, 0x102030);
    BOOL r = p_NtGdiTransparentBlt(p.d.dc, 0, 0, 4, 4, p.s.dc, 0, 0, 4, 4, PX2CR(0x102030)); br_flush();
    CHECK(r);
    CHECK(br_all(&p.d, 0, 0, 4, 4, SENT));
    sd_free(&p);
}

TEST(gdi_blit_raster_transparentblt_offsets) {
    NT(NtGdiTransparentBlt); NEEDSD(p, 8, 8, 8, 8);
    BOOL r = p_NtGdiTransparentBlt(p.d.dc, 2, 1, 3, 4, p.s.dc, 4, 2, 3, 4, RGB(0, 0, 0)); br_flush();
    CHECK(r);
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {
        DWORD want = (x >= 2 && x < 5 && y >= 1 && y < 5) ? br_pat(x - 2 + 4, y - 1 + 2) : SENT;
        CHECKF(br_at(&p.d, x, y) == want, "(%d,%d)", x, y);
    }
    sd_free(&p);
}

TEST(gdi_blit_raster_transparentblt_stretch_2x_key_blocks) {
    NT(NtGdiTransparentBlt); NEEDSD(p, 3, 3, 6, 6);
    DWORD key = 0x010203;
    br_set(&p.s, 1, 1, key);
    BOOL r = p_NtGdiTransparentBlt(p.d.dc, 0, 0, 6, 6, p.s.dc, 0, 0, 3, 3, PX2CR(key)); br_flush();
    CHECK(r);
    int skipped = br_count(&p.d, 2, 2, 4, 4, SENT);
    t_note("2x stretched TransparentBlt leaves %d/4 key-block pixels untouched", skipped);
    CHECK(br_at(&p.d, 0, 0) == br_pat(0, 0) && br_at(&p.d, 5, 5) == br_pat(2, 2));
    CHECK(skipped == 4);
    sd_free(&p);
}

/* NtGdiTransparentBlt rejects negative extents (any of dest cx/cy, src cx/cy) before touching the DCs. */
TEST(gdi_blit_raster_transparentblt_negative_extent_invalid_parameter) {
    NT(NtGdiTransparentBlt); NEEDSD(p, 4, 4, 4, 4);
    static const int e[][4] = { { -4, 4, 4, 4 }, { 4, -4, 4, 4 }, { 4, 4, -4, 4 }, { 4, 4, 4, -4 }, { -1, -1, -1, -1 } };
    for (int i = 0; i < 5; i++) {
        SetLastError(0);
        BOOL r = p_NtGdiTransparentBlt(p.d.dc, 0, 0, e[i][0], e[i][1], p.s.dc, 0, 0, e[i][2], e[i][3], 0);
        CHECKF(!r, "extent set %d accepted", i);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
    SetLastError(0);
    CHECK(!p_NtGdiTransparentBlt(NULL, 0, 0, -1, 4, NULL, 0, 0, 4, 4, 0));
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
    br_flush();
    CHECK(br_all(&p.d, 0, 0, 4, 4, SENT));
    sd_free(&p);
}

/* gdi32full!GdiTransparentBlt refuses non-positive extents and NULL/metafile source itself. */
TEST(gdi_blit_raster_transparentblt_userland_nonpositive_extent_returns_false) {
    GDIF(GdiTransparentBlt); NEEDSD(p, 4, 4, 4, 4);
    static const int e[][4] = { { 0, 4, 4, 4 }, { 4, 0, 4, 4 }, { 4, 4, 0, 4 }, { 4, 4, 4, 0 }, { -4, 4, 4, 4 } };
    for (int i = 0; i < 5; i++) CHECKF(!p_GdiTransparentBlt(p.d.dc, 0, 0, e[i][0], e[i][1], p.s.dc, 0, 0, e[i][2], e[i][3], 0), "set %d", i);
    CHECK(!p_GdiTransparentBlt(p.d.dc, 0, 0, 4, 4, NULL, 0, 0, 4, 4, 0));
    CHECK(!p_GdiTransparentBlt(NULL, 0, 0, 4, 4, p.s.dc, 0, 0, 4, 4, 0));
    CHECK(br_all(&p.d, 0, 0, 4, 4, SENT));
    sd_free(&p);
}

TEST(gdi_blit_raster_transparentblt_userland_matches_syscall) {
    GDIF(GdiTransparentBlt); NEEDSD(p, 6, 6, 6, 6);
    DWORD key = br_pat(2, 2);
    BOOL r = p_GdiTransparentBlt(p.d.dc, 0, 0, 6, 6, p.s.dc, 0, 0, 6, 6, PX2CR(key)); br_flush();
    CHECK(r);
    for (int y = 0; y < 6; y++) for (int x = 0; x < 6; x++) CHECK(br_at(&p.d, x, y) == ((x == 2 && y == 2) ? SENT : br_pat(x, y)));
    sd_free(&p);
}

/* ======================= GradientFill ======================= */
static TRIVERTEX tv(LONG x, LONG y, int r, int g, int b) { TRIVERTEX v; v.x = x; v.y = y; v.Red = (COLOR16)(r << 8); v.Green = (COLOR16)(g << 8); v.Blue = (COLOR16)(b << 8); v.Alpha = 0; return v; }

TEST(gdi_blit_raster_gradient_rect_h_endpoints_and_monotonic) {
    NT(NtGdiGradientFill); NEEDSD(p, 32, 8, 32, 8);
    br_fill(&p.d, SENT);
    TRIVERTEX v[2] = { tv(0, 0, 0x00, 0x20, 0xF0), tv(32, 8, 0xF0, 0xE0, 0x10) };
    GRADIENT_RECT gr = { 0, 1 };
    BOOL r = p_NtGdiGradientFill(p.d.dc, v, 2, &gr, 1, GRADIENT_FILL_RECT_H); br_flush();
    CHECK(r);
    CHECKF(br_at(&p.d, 0, 0) == RGBPX(0x00, 0x20, 0xF0), "left edge %06x", br_at(&p.d, 0, 0));
    DWORD last = br_at(&p.d, 31, 0);
    t_note("last column %06x (end color f0e010)", last);
    CHECK(within(last, RGBPX(0xF0, 0xE0, 0x10), 12));
    for (int x = 1; x < 32; x++) {
        DWORD a = br_at(&p.d, x - 1, 0), b = br_at(&p.d, x, 0);
        CHECKF(ch(b, 2) >= ch(a, 2) && ch(b, 1) >= ch(a, 1) && ch(b, 0) <= ch(a, 0), "column %d not monotonic %06x -> %06x", x, a, b);
    }
    for (int y = 1; y < 8; y++) for (int x = 0; x < 32; x++) CHECKF(br_at(&p.d, x, y) == br_at(&p.d, x, 0), "row %d col %d differs from row 0", y, x);
    sd_free(&p);
}

TEST(gdi_blit_raster_gradient_rect_v_endpoints_and_monotonic) {
    NT(NtGdiGradientFill); NEEDSD(p, 8, 32, 8, 32);
    TRIVERTEX v[2] = { tv(0, 0, 0xF0, 0x00, 0x00), tv(8, 32, 0x00, 0x00, 0xF0) };
    GRADIENT_RECT gr = { 0, 1 };
    BOOL r = p_NtGdiGradientFill(p.d.dc, v, 2, &gr, 1, GRADIENT_FILL_RECT_V); br_flush();
    CHECK(r);
    CHECKF(br_at(&p.d, 0, 0) == RGBPX(0xF0, 0, 0), "top edge %06x", br_at(&p.d, 0, 0));
    t_note("bottom row %06x (end color 0000f0)", br_at(&p.d, 0, 31));
    CHECK(within(br_at(&p.d, 0, 31), RGBPX(0, 0, 0xF0), 12));
    for (int y = 1; y < 32; y++) {
        DWORD a = br_at(&p.d, 0, y - 1), b = br_at(&p.d, 0, y);
        CHECKF(ch(b, 2) <= ch(a, 2) && ch(b, 0) >= ch(a, 0), "row %d not monotonic", y);
    }
    for (int y = 0; y < 32; y++) for (int x = 1; x < 8; x++) CHECKF(br_at(&p.d, x, y) == br_at(&p.d, 0, y), "(%d,%d)", x, y);
    sd_free(&p);
}

TEST(gdi_blit_raster_gradient_rect_confined_to_rectangle) {
    NT(NtGdiGradientFill); NEEDSD(p, 16, 16, 16, 16);
    TRIVERTEX v[2] = { tv(4, 3, 0x10, 0x10, 0x10), tv(12, 9, 0xE0, 0xE0, 0xE0) };
    GRADIENT_RECT gr = { 0, 1 };
    BOOL r = p_NtGdiGradientFill(p.d.dc, v, 2, &gr, 1, GRADIENT_FILL_RECT_H); br_flush();
    CHECK(r);
    for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) {
        int in = x >= 4 && x < 12 && y >= 3 && y < 9;
        if (!in) CHECKF(br_at(&p.d, x, y) == SENT, "outside (%d,%d) modified: %06x", x, y, br_at(&p.d, x, y));
        else CHECKF(br_at(&p.d, x, y) != SENT, "inside (%d,%d) untouched", x, y);
    }
    CHECKF(br_at(&p.d, 4, 3) == 0x101010, "ul pixel %06x", br_at(&p.d, 4, 3));
    sd_free(&p);
}

TEST(gdi_blit_raster_gradient_rect_equal_colors_is_solid) {
    NT(NtGdiGradientFill); NEEDSD(p, 16, 4, 16, 4);
    TRIVERTEX v[2] = { tv(0, 0, 0x55, 0x66, 0x77), tv(16, 4, 0x55, 0x66, 0x77) };
    GRADIENT_RECT gr = { 0, 1 };
    BOOL r = p_NtGdiGradientFill(p.d.dc, v, 2, &gr, 1, GRADIENT_FILL_RECT_H); br_flush();
    CHECK(r);
    CHECK(br_all(&p.d, 0, 0, 16, 4, RGBPX(0x55, 0x66, 0x77)));
    sd_free(&p);
}

TEST(gdi_blit_raster_gradient_rect_userland_matches_syscall) {
    GDIF(GdiGradientFill); NEEDSD(p, 16, 4, 16, 4);
    TRIVERTEX v[2] = { tv(0, 0, 0, 0, 0), tv(16, 4, 0xF0, 0xF0, 0xF0) };
    GRADIENT_RECT gr = { 0, 1 };
    BOOL r = p_GdiGradientFill(p.d.dc, v, 2, &gr, 1, GRADIENT_FILL_RECT_H); br_flush();
    CHECK(r);
    CHECK(br_at(&p.d, 0, 0) == 0);
    for (int x = 1; x < 16; x++) CHECK(ch(br_at(&p.d, x, 0), 1) >= ch(br_at(&p.d, x - 1, 0), 1));
    sd_free(&p);
}

TEST(gdi_blit_raster_gradient_multiple_rects) {
    NT(NtGdiGradientFill); NEEDSD(p, 16, 8, 16, 8);
    TRIVERTEX v[4] = { tv(0, 0, 0x20, 0, 0), tv(8, 8, 0x20, 0, 0), tv(8, 0, 0, 0x30, 0), tv(16, 8, 0, 0x30, 0) };
    GRADIENT_RECT gr[2] = { { 0, 1 }, { 2, 3 } };
    BOOL r = p_NtGdiGradientFill(p.d.dc, v, 4, gr, 2, GRADIENT_FILL_RECT_H); br_flush();
    CHECK(r);
    CHECK(br_all(&p.d, 0, 0, 8, 8, RGBPX(0x20, 0, 0)));
    CHECK(br_all(&p.d, 8, 0, 16, 8, RGBPX(0, 0x30, 0)));
    sd_free(&p);
}

TEST(gdi_blit_raster_gradient_triangle) {
    NT(NtGdiGradientFill); NEEDSD(p, 64, 64, 64, 64);
    TRIVERTEX v[3] = { tv(2, 2, 0xF0, 0, 0), tv(60, 2, 0, 0xF0, 0), tv(2, 60, 0, 0, 0xF0) };
    GRADIENT_TRIANGLE gt = { 0, 1, 2 };
    BOOL r = p_NtGdiGradientFill(p.d.dc, v, 3, &gt, 1, GRADIENT_FILL_TRIANGLE); br_flush();
    CHECK(r);
    /* outside the vertex bounding box nothing is written */
    for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) if (x < 2 || y < 2 || x > 60 || y > 60) CHECKF(br_at(&p.d, x, y) == SENT, "(%d,%d) %06x", x, y, br_at(&p.d, x, y));
    /* beyond the hypotenuse (x + y > 62) nothing is written */
    CHECK(br_at(&p.d, 50, 50) == SENT && br_at(&p.d, 60, 60) == SENT && br_at(&p.d, 40, 40) == SENT);
    DWORD ca = br_at(&p.d, 4, 4);
    t_note("near red vertex %06x, near green vertex %06x, near blue vertex %06x, centroid %06x", ca, br_at(&p.d, 56, 3), br_at(&p.d, 3, 56), br_at(&p.d, 21, 21));
    CHECK(ch(ca, 2) > ch(ca, 1) + 0x40 && ch(ca, 2) > ch(ca, 0) + 0x40);
    DWORD cg = br_at(&p.d, 56, 3);
    CHECK(ch(cg, 1) > ch(cg, 2) + 0x40 && ch(cg, 1) > ch(cg, 0) + 0x40);
    DWORD cb = br_at(&p.d, 3, 56);
    CHECK(ch(cb, 0) > ch(cb, 1) + 0x40 && ch(cb, 0) > ch(cb, 2) + 0x40);
    DWORD cc = br_at(&p.d, 21, 21);
    CHECK(within(cc, RGBPX(0x50, 0x50, 0x50), 0x20));
    sd_free(&p);
}

TEST(gdi_blit_raster_gradient_triangle_solid_when_colors_equal) {
    NT(NtGdiGradientFill); NEEDSD(p, 32, 32, 32, 32);
    TRIVERTEX v[3] = { tv(0, 0, 0x30, 0x60, 0x90), tv(31, 0, 0x30, 0x60, 0x90), tv(0, 31, 0x30, 0x60, 0x90) };
    GRADIENT_TRIANGLE gt = { 0, 1, 2 };
    BOOL r = p_NtGdiGradientFill(p.d.dc, v, 3, &gt, 1, GRADIENT_FILL_TRIANGLE); br_flush();
    CHECK(r);
    int n = br_count(&p.d, 0, 0, 32, 32, RGBPX(0x30, 0x60, 0x90));
    CHECKF(n > 400, "only %d solid pixels", n);
    CHECK(br_at(&p.d, 5, 5) == RGBPX(0x30, 0x60, 0x90) && br_at(&p.d, 20, 20) == SENT);
    int stray = 0;
    for (int i = 0; i < 32 * 32; i++) { DWORD px = p.d.px[i] & M24; if (px != SENT && px != RGBPX(0x30, 0x60, 0x90)) stray++; }
    CHECKF(stray == 0, "%d pixels of another colour", stray);
    sd_free(&p);
}

/* NtGdiGradientFill (win32kfull): mode must be 0..2 with no bits above the low byte, vertex/mesh pointers
 * and counts must be non-zero; every violation is ERROR_INVALID_PARAMETER and FALSE. */
TEST(gdi_blit_raster_gradient_invalid_parameters) {
    NT(NtGdiGradientFill); NEEDSD(p, 8, 8, 8, 8);
    TRIVERTEX v[3] = { tv(0, 0, 1, 2, 3), tv(8, 8, 4, 5, 6), tv(0, 8, 7, 8, 9) };
    GRADIENT_RECT gr = { 0, 1 };
    struct { TRIVERTEX *v; ULONG nv; void *mesh; ULONG nc; ULONG mode; const char *why; } c[] = {
        { v, 2, &gr, 1, 3, "mode 3" }, { v, 2, &gr, 1, 0x100, "mode bit 8" }, { v, 2, &gr, 1, 0x80000000, "mode high bit" },
        { v, 2, &gr, 1, 0xffffffff, "mode -1" }, { NULL, 2, &gr, 1, 0, "null vertices" }, { v, 2, NULL, 1, 0, "null mesh" },
        { v, 0, &gr, 1, 0, "zero vertices" }, { v, 2, &gr, 0, 0, "zero count" }, { v, 0x80000000, &gr, 1, 0, "huge nv" },
        { v, 2, &gr, 0x80000000, 0, "huge count" }, { v, 0x271001, &gr, 1, 0, "nv past pool limit" } };
    for (unsigned i = 0; i < sizeof c / sizeof c[0]; i++) {
        SetLastError(0);
        BOOL r = p_NtGdiGradientFill(p.d.dc, c[i].v, c[i].nv, c[i].mesh, c[i].nc, c[i].mode);
        CHECKF(!r, "%s accepted", c[i].why);
        DWORD e = GetLastError();
        CHECKF(e == ERROR_INVALID_PARAMETER, "%s: error %lu", c[i].why, e);
    }
    br_flush();
    CHECK(br_all(&p.d, 0, 0, 8, 8, SENT));
    sd_free(&p);
}

TEST(gdi_blit_raster_gradient_mode_high_bits_checked_before_dc) {
    NT(NtGdiGradientFill);
    TRIVERTEX v[2] = { tv(0, 0, 0, 0, 0), tv(1, 1, 0, 0, 0) };
    GRADIENT_RECT gr = { 0, 1 };
    SetLastError(0);
    CHECK(!p_NtGdiGradientFill(NULL, v, 2, &gr, 1, 7));
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}

TEST(gdi_blit_raster_gradient_null_dc_fails) {
    NT(NtGdiGradientFill);
    TRIVERTEX v[2] = { tv(0, 0, 0, 0, 0), tv(1, 1, 0, 0, 0) };
    GRADIENT_RECT gr = { 0, 1 };
    CHECK(!p_NtGdiGradientFill(NULL, v, 2, &gr, 1, GRADIENT_FILL_RECT_H));
    GDIF(GdiGradientFill);
    SetLastError(0);
    CHECK(!p_GdiGradientFill(NULL, v, 2, &gr, 1, GRADIENT_FILL_RECT_H));
    EXPECT_ERR(ERROR_INVALID_PARAMETER);   /* gdi32full!GdiGradientFill: handle fails validation -> GdiSetLastError(0x57) */
}

TEST(gdi_blit_raster_gradient_bad_mesh_index_fails) {
    NT(NtGdiGradientFill); NEEDSD(p, 8, 8, 8, 8);
    TRIVERTEX v[2] = { tv(0, 0, 0, 0, 0), tv(8, 8, 0xF0, 0xF0, 0xF0) };
    GRADIENT_RECT gr = { 0, 5 };   /* vertex index past nVertex */
    SetLastError(0);
    BOOL r = p_NtGdiGradientFill(p.d.dc, v, 2, &gr, 1, GRADIENT_FILL_RECT_H); br_flush();
    t_note("out-of-range mesh vertex index: returned %d, error %lu", r, GetLastError());
    CHECK(!r);
    CHECK(br_all(&p.d, 0, 0, 8, 8, SENT));
    sd_free(&p);
}
