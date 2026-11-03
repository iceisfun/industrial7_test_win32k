/* gdi_blit_raster: NtGdiStretchBlt and the stretch-mode attribute. */
#include "br.h"

typedef struct { BR s, d; } SD;
static int sd_make(SD *p, int sw, int sh, int dw, int dh) {
    memset(p, 0, sizeof *p);
    p->s = br_make(sw, sh); p->d = br_make(dw, dh);
    if (!p->s.dc || !p->d.dc) return 0;
    br_fill_pat(&p->s); br_fill(&p->d, 0x00DEAD);
    return 1;
}
static void sd_free(SD *p) { br_free(&p->s); br_free(&p->d); }
#define NEEDSD(p, sw, sh, dw, dh) SD p; if (!sd_make(&p, sw, sh, dw, dh)) { sd_free(&p); t_skip("no DIB/DC"); return; }
#define SENT 0x00DEAD

/* ---- integer upscale with COLORONCOLOR: each source pixel becomes a kx x ky block ---- */
static void upscale_case(int kx, int ky, int viaSyscall) {
    NT(NtGdiStretchBlt);
    NEEDSD(p, 5, 4, 5 * kx, 4 * ky);
    U(SetStretchBltMode)(p.d.dc, COLORONCOLOR);
    BOOL r = viaSyscall
        ? p_NtGdiStretchBlt(p.d.dc, 0, 0, 5 * kx, 4 * ky, p.s.dc, 0, 0, 5, 4, SRCCOPY, CLR_BAD)
        : U(StretchBlt)(p.d.dc, 0, 0, 5 * kx, 4 * ky, p.s.dc, 0, 0, 5, 4, SRCCOPY);
    br_flush();
    CHECKF(r, "%dx%d", kx, ky);
    int bad = 0;
    for (int y = 0; y < 4 * ky; y++) for (int x = 0; x < 5 * kx; x++) if (br_at(&p.d, x, y) != br_pat(x / kx, y / ky)) bad++;
    CHECKF(bad == 0, "%dx%d: %d wrong pixels", kx, ky, bad);
    sd_free(&p);
}
#define UPSCALE_TEST(kx, ky) \
    TEST(gdi_blit_raster_stretch_coloroncolor_up_##kx##x##ky##_syscall) { upscale_case(kx, ky, 1); } \
    TEST(gdi_blit_raster_stretch_coloroncolor_up_##kx##x##ky##_userland) { upscale_case(kx, ky, 0); }
UPSCALE_TEST(1, 1)
UPSCALE_TEST(2, 2)
UPSCALE_TEST(3, 3)
UPSCALE_TEST(4, 4)
UPSCALE_TEST(2, 1)
UPSCALE_TEST(1, 3)
UPSCALE_TEST(5, 2)

/* ---- COLORONCOLOR shrink: each destination pixel is one source pixel of its n x n group ---- */
static void downscale_case(int n) {
    NT(NtGdiStretchBlt);
    NEEDSD(p, 4 * n, 3 * n, 4, 3);
    U(SetStretchBltMode)(p.d.dc, COLORONCOLOR);
    BOOL r = p_NtGdiStretchBlt(p.d.dc, 0, 0, 4, 3, p.s.dc, 0, 0, 4 * n, 3 * n, SRCCOPY, CLR_BAD);
    br_flush();
    CHECK(r);
    /* every dest pixel must be a source pixel inside its own group; find the group-relative offset used */
    int found = -1;
    for (int oy = 0; oy < n && found < 0; oy++) for (int ox = 0; ox < n && found < 0; ox++) {
        int ok = 1;
        for (int y = 0; y < 3; y++) for (int x = 0; x < 4; x++) if (br_at(&p.d, x, y) != br_pat(x * n + ox, y * n + oy)) ok = 0;
        if (ok) found = oy * n + ox;
    }
    CHECKF(found >= 0, "shrink 1/%d does not select one pixel per group at a fixed offset", n);
    t_note("COLORONCOLOR 1/%d shrink samples group offset (%d,%d)", n, found < 0 ? -1 : found % n, found < 0 ? -1 : found / n);
    sd_free(&p);
}
TEST(gdi_blit_raster_stretch_coloroncolor_down_2) { downscale_case(2); }
TEST(gdi_blit_raster_stretch_coloroncolor_down_3) { downscale_case(3); }
TEST(gdi_blit_raster_stretch_coloroncolor_down_4) { downscale_case(4); }

/* ---- uniform sources give uniform results in every stretch mode and direction ---- */
static void uniform_case(int mode, int sw, int sh, int dw, int dh) {
    NT(NtGdiStretchBlt);
    NEEDSD(p, sw, sh, dw, dh);
    br_fill(&p.s, 0x4080C0);
    if (mode == HALFTONE) U(SetBrushOrgEx)(p.d.dc, 0, 0, NULL);
    U(SetStretchBltMode)(p.d.dc, mode);
    BOOL r = p_NtGdiStretchBlt(p.d.dc, 0, 0, dw, dh, p.s.dc, 0, 0, sw, sh, SRCCOPY, CLR_BAD);
    br_flush();
    CHECKF(r, "mode %d %dx%d->%dx%d", mode, sw, sh, dw, dh);
    int n = br_count(&p.d, 0, 0, dw, dh, 0x4080C0);
    CHECKF(n == dw * dh, "mode %d %dx%d->%dx%d uniform pixels %d/%d (edge pixel %06x)", mode, sw, sh, dw, dh, n, dw * dh, br_at(&p.d, dw - 1, dh - 1));
    sd_free(&p);
}
#define UNIFORM_TEST(nm, mode) \
    TEST(gdi_blit_raster_stretch_uniform_##nm##_up) { uniform_case(mode, 4, 4, 12, 8); } \
    TEST(gdi_blit_raster_stretch_uniform_##nm##_down) { uniform_case(mode, 12, 8, 4, 4); } \
    TEST(gdi_blit_raster_stretch_uniform_##nm##_one_to_one) { uniform_case(mode, 6, 6, 6, 6); }
UNIFORM_TEST(blackonwhite, BLACKONWHITE)
UNIFORM_TEST(whiteonblack, WHITEONBLACK)
UNIFORM_TEST(coloroncolor, COLORONCOLOR)
UNIFORM_TEST(halftone, HALFTONE)

/* Shrink in each mode, documented as AND (BLACKONWHITE/STRETCH_ANDSCANS), OR (WHITEONBLACK/STRETCH_ORSCANS)
 * of eliminated pixels, or deletion (COLORONCOLOR). Color-surface behaviour is engine-defined: only logged. */
TEST(gdi_blit_raster_stretch_shrink_mode_values_logged) {
    NT(NtGdiStretchBlt);
    static const struct { int mode; const char *nm; } m[] = { { BLACKONWHITE, "BLACKONWHITE" }, { WHITEONBLACK, "WHITEONBLACK" },
                                                             { COLORONCOLOR, "COLORONCOLOR" }, { HALFTONE, "HALFTONE" } };
    for (int i = 0; i < 4; i++) {
        NEEDSD(p, 2, 2, 1, 1);
        br_set(&p.s, 0, 0, 0xF00000); br_set(&p.s, 1, 0, 0x0F0000); br_set(&p.s, 0, 1, 0x00F000); br_set(&p.s, 1, 1, 0x0000F0);
        U(SetStretchBltMode)(p.d.dc, m[i].mode);
        BOOL r = p_NtGdiStretchBlt(p.d.dc, 0, 0, 1, 1, p.s.dc, 0, 0, 2, 2, SRCCOPY, CLR_BAD);
        br_flush();
        CHECKF(r, "%s", m[i].nm);
        t_note("2x2->1x1 %s result %06x", m[i].nm, br_at(&p.d, 0, 0));
        sd_free(&p);
    }
}

/* ---- identity and flips ---- */
TEST(gdi_blit_raster_stretch_one_to_one_equals_bitblt) {
    NT(NtGdiStretchBlt);
    NEEDSD(p, 7, 6, 7, 6);
    BR b = br_make(7, 6);
    CHECK(b.dc != NULL);
    BOOL r = p_NtGdiStretchBlt(p.d.dc, 0, 0, 7, 6, p.s.dc, 0, 0, 7, 6, SRCCOPY, CLR_BAD);
    BOOL r2 = U(BitBlt)(b.dc, 0, 0, 7, 6, p.s.dc, 0, 0, SRCCOPY);
    br_flush();
    CHECK(r && r2);
    for (int y = 0; y < 6; y++) for (int x = 0; x < 7; x++) { CHECK(br_at(&p.d, x, y) == br_pat(x, y)); CHECK(br_at(&b, x, y) == br_pat(x, y)); }
    br_free(&b); sd_free(&p);
}

/* Locate the modified column / row span of the destination relative to the sentinel fill. */
static void span(const BR *d, int *x0, int *x1, int *y0, int *y1) {
    *x0 = *y0 = 1 << 20; *x1 = *y1 = -1;
    for (int y = 0; y < d->h; y++) for (int x = 0; x < d->w; x++) if (br_at(d, x, y) != SENT) {
        if (x < *x0) *x0 = x;
        if (x > *x1) *x1 = x;
        if (y < *y0) *y0 = y;
        if (y > *y1) *y1 = y;
    }
}

/* Negative destination width: the same source is written horizontally mirrored. */
TEST(gdi_blit_raster_stretch_negative_dest_width_mirrors) {
    NT(NtGdiStretchBlt);
    NEEDSD(p, 6, 4, 12, 6);
    BOOL r = p_NtGdiStretchBlt(p.d.dc, 8, 1, -6, 4, p.s.dc, 0, 0, 6, 4, SRCCOPY, CLR_BAD); br_flush();
    CHECK(r);
    int x0, x1, y0, y1; span(&p.d, &x0, &x1, &y0, &y1);
    CHECKF(x1 - x0 + 1 == 6 && y1 - y0 + 1 == 4, "span %d..%d x %d..%d", x0, x1, y0, y1);
    t_note("negative-width dest 8,-6 covers columns %d..%d", x0, x1);
    CHECK(x0 == 2 || x0 == 3);
    for (int y = 0; y < 4; y++) for (int x = 0; x < 6; x++) CHECKF(br_at(&p.d, x0 + x, y0 + y) == br_pat(5 - x, y), "(%d,%d)", x, y);
    sd_free(&p);
}

TEST(gdi_blit_raster_stretch_negative_dest_height_flips) {
    NT(NtGdiStretchBlt);
    NEEDSD(p, 4, 6, 6, 12);
    BOOL r = p_NtGdiStretchBlt(p.d.dc, 1, 8, 4, -6, p.s.dc, 0, 0, 4, 6, SRCCOPY, CLR_BAD); br_flush();
    CHECK(r);
    int x0, x1, y0, y1; span(&p.d, &x0, &x1, &y0, &y1);
    CHECKF(x1 - x0 + 1 == 4 && y1 - y0 + 1 == 6, "span %d..%d x %d..%d", x0, x1, y0, y1);
    t_note("negative-height dest 8,-6 covers rows %d..%d", y0, y1);
    CHECK(y0 == 2 || y0 == 3);
    for (int y = 0; y < 6; y++) for (int x = 0; x < 4; x++) CHECKF(br_at(&p.d, x0 + x, y0 + y) == br_pat(x, 5 - y), "(%d,%d)", x, y);
    sd_free(&p);
}

TEST(gdi_blit_raster_stretch_both_negative_rotates_180) {
    NT(NtGdiStretchBlt);
    NEEDSD(p, 5, 5, 10, 10);
    BOOL r = p_NtGdiStretchBlt(p.d.dc, 7, 7, -5, -5, p.s.dc, 0, 0, 5, 5, SRCCOPY, CLR_BAD); br_flush();
    CHECK(r);
    int x0, x1, y0, y1; span(&p.d, &x0, &x1, &y0, &y1);
    CHECKF(x1 - x0 + 1 == 5 && y1 - y0 + 1 == 5, "span %d..%d x %d..%d", x0, x1, y0, y1);
    for (int y = 0; y < 5; y++) for (int x = 0; x < 5; x++) CHECKF(br_at(&p.d, x0 + x, y0 + y) == br_pat(4 - x, 4 - y), "(%d,%d)", x, y);
    sd_free(&p);
}

/* Negative source width flips the same way. The source rectangle spans [xs+cxs, xs] or (xs+cxs, xs]; find which. */
TEST(gdi_blit_raster_stretch_negative_src_width_mirrors) {
    NT(NtGdiStretchBlt);
    NEEDSD(p, 8, 4, 12, 4);
    BOOL r = p_NtGdiStretchBlt(p.d.dc, 2, 0, 6, 4, p.s.dc, 6, 0, -6, 4, SRCCOPY, CLR_BAD); br_flush();
    t_note("negative source width returned %d", r);
    if (!r) { sd_free(&p); return; }
    int x0, x1, y0, y1; span(&p.d, &x0, &x1, &y0, &y1);
    CHECKF(x1 - x0 + 1 == 6 && y1 - y0 + 1 == 4, "span %d..%d x %d..%d", x0, x1, y0, y1);
    int found = -1;
    for (int off = 0; off + 6 <= 8 && found < 0; off++) {
        int ok = 1;
        for (int y = 0; y < 4; y++) for (int x = 0; x < 6; x++) if (br_at(&p.d, x0 + x, y0 + y) != br_pat(off + 5 - x, y)) ok = 0;
        if (ok) found = off;
    }
    CHECKF(found >= 0, "dest rows are not a reversed contiguous source segment");
    t_note("negative source width 6,-6 reads source columns %d..%d reversed", found, found + 5);
    sd_free(&p);
}

/* ---- ROPs in the stretch syscall ---- */
TEST(gdi_blit_raster_stretch_srcinvert_one_to_one) {
    NT(NtGdiStretchBlt);
    NEEDSD(p, 5, 5, 5, 5);
    for (int y = 0; y < 5; y++) for (int x = 0; x < 5; x++) br_set(&p.d, x, y, br_pat(y, x));
    BOOL r = p_NtGdiStretchBlt(p.d.dc, 0, 0, 5, 5, p.s.dc, 0, 0, 5, 5, SRCINVERT, CLR_BAD); br_flush();
    CHECK(r);
    for (int y = 0; y < 5; y++) for (int x = 0; x < 5; x++) CHECKF(br_at(&p.d, x, y) == (br_pat(x, y) ^ br_pat(y, x)), "(%d,%d)", x, y);
    sd_free(&p);
}

TEST(gdi_blit_raster_stretch_notsrccopy_upscale) {
    NT(NtGdiStretchBlt);
    NEEDSD(p, 3, 3, 6, 6);
    U(SetStretchBltMode)(p.d.dc, COLORONCOLOR);
    BOOL r = p_NtGdiStretchBlt(p.d.dc, 0, 0, 6, 6, p.s.dc, 0, 0, 3, 3, NOTSRCCOPY, CLR_BAD); br_flush();
    CHECK(r);
    for (int y = 0; y < 6; y++) for (int x = 0; x < 6; x++) CHECKF(br_at(&p.d, x, y) == (br_pat(x / 2, y / 2) ^ M24), "(%d,%d)", x, y);
    sd_free(&p);
}

TEST(gdi_blit_raster_stretch_all_source_rops_one_to_one) {
    NT(NtGdiStretchBlt);
    NEEDSD(p, 3, 3, 3, 3);
    HBRUSH b = U(CreateSolidBrush)(PX2CR(0xF0F0F0));
    HGDIOBJ old = U(SelectObject)(p.d.dc, b);
    for (unsigned c = 0; c < 256; c += 3) {
        for (int y = 0; y < 3; y++) for (int x = 0; x < 3; x++) br_set(&p.d, x, y, br_pat(y + 2, x + 1));
        BOOL r = p_NtGdiStretchBlt(p.d.dc, 0, 0, 3, 3, p.s.dc, 0, 0, 3, 3, ROPCODE(c), CLR_BAD); br_flush();
        CHECKF(r, "rop %02x", c);
        for (int y = 0; y < 3; y++) for (int x = 0; x < 3; x++)
            CHECKF(br_at(&p.d, x, y) == rop3_apply(c, 0xF0F0F0, br_pat(x, y), br_pat(y + 2, x + 1)), "rop %02x (%d,%d)", c, x, y);
    }
    U(SelectObject)(p.d.dc, old); U(DeleteObject)(b);
    sd_free(&p);
}

/* ---- partial / clipped destination ---- */
TEST(gdi_blit_raster_stretch_dest_clipped_by_surface) {
    NT(NtGdiStretchBlt);
    NEEDSD(p, 3, 3, 6, 6);
    U(SetStretchBltMode)(p.d.dc, COLORONCOLOR);
    BOOL r = p_NtGdiStretchBlt(p.d.dc, -2, -2, 6, 6, p.s.dc, 0, 0, 3, 3, SRCCOPY, CLR_BAD); br_flush();
    CHECK(r);
    for (int y = 0; y < 6; y++) for (int x = 0; x < 6; x++) {
        DWORD want = (x < 4 && y < 4) ? br_pat((x + 2) / 2, (y + 2) / 2) : SENT;
        CHECKF(br_at(&p.d, x, y) == want, "(%d,%d) got %06x want %06x", x, y, br_at(&p.d, x, y), want);
    }
    sd_free(&p);
}

TEST(gdi_blit_raster_stretch_dest_clip_region) {
    NT(NtGdiStretchBlt);
    NEEDSD(p, 3, 3, 6, 6);
    U(SetStretchBltMode)(p.d.dc, COLORONCOLOR);
    HRGN rg = U(CreateRectRgn)(1, 1, 4, 3);
    U(SelectClipRgn)(p.d.dc, rg);
    BOOL r = p_NtGdiStretchBlt(p.d.dc, 0, 0, 6, 6, p.s.dc, 0, 0, 3, 3, SRCCOPY, CLR_BAD); br_flush();
    CHECK(r);
    for (int y = 0; y < 6; y++) for (int x = 0; x < 6; x++)
        CHECKF(br_at(&p.d, x, y) == ((x >= 1 && x < 4 && y >= 1 && y < 3) ? br_pat(x / 2, y / 2) : SENT), "(%d,%d)", x, y);
    U(SelectClipRgn)(p.d.dc, NULL); U(DeleteObject)(rg);
    sd_free(&p);
}

TEST(gdi_blit_raster_stretch_source_subrect) {
    NT(NtGdiStretchBlt);
    NEEDSD(p, 8, 8, 8, 8);
    U(SetStretchBltMode)(p.d.dc, COLORONCOLOR);
    BOOL r = p_NtGdiStretchBlt(p.d.dc, 0, 0, 6, 4, p.s.dc, 3, 2, 3, 2, SRCCOPY, CLR_BAD); br_flush();
    CHECK(r);
    for (int y = 0; y < 4; y++) for (int x = 0; x < 6; x++) CHECKF(br_at(&p.d, x, y) == br_pat(3 + x / 2, 2 + y / 2), "(%d,%d)", x, y);
    CHECK(br_at(&p.d, 6, 0) == SENT && br_at(&p.d, 0, 4) == SENT);
    sd_free(&p);
}

/* ---- failure cases ---- */
TEST(gdi_blit_raster_stretch_null_dc_fails) {
    NT(NtGdiStretchBlt);
    NEEDSD(p, 4, 4, 4, 4);
    /* The syscall reports TRUE for an invalid destination DC (it only sets ERROR_INVALID_HANDLE and draws nothing); an invalid
     * source DC returns FALSE. The user32/gdi32 wrapper returns FALSE for a NULL destination. */
    SetLastError(0);
    p_NtGdiStretchBlt(NULL, 0, 0, 4, 4, p.s.dc, 0, 0, 4, 4, SRCCOPY, CLR_BAD);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    CHECK(!U(StretchBlt)(NULL, 0, 0, 4, 4, p.s.dc, 0, 0, 4, 4, SRCCOPY));
    SetLastError(0);
    CHECK(!p_NtGdiStretchBlt(p.d.dc, 0, 0, 4, 4, NULL, 0, 0, 4, 4, SRCCOPY, CLR_BAD));
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    CHECK(br_all(&p.d, 0, 0, 4, 4, SENT));
    sd_free(&p);
}

TEST(gdi_blit_raster_stretch_deleted_dc_fails) {
    NT(NtGdiStretchBlt);
    NEEDSD(p, 4, 4, 4, 4);
    BR tmp = br_make(4, 4); HDC dead = tmp.dc; br_free(&tmp);
    SetLastError(0);
    p_NtGdiStretchBlt(dead, 0, 0, 4, 4, p.s.dc, 0, 0, 4, 4, SRCCOPY, CLR_BAD);   /* TRUE with ERROR_INVALID_HANDLE, nothing drawn */
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    SetLastError(0);
    CHECK(!p_NtGdiStretchBlt(p.d.dc, 0, 0, 4, 4, dead, 0, 0, 4, 4, SRCCOPY, CLR_BAD));
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    CHECK(br_all(&p.d, 0, 0, 4, 4, SENT));
    sd_free(&p);
}

TEST(gdi_blit_raster_stretch_zero_extents_logged) {
    NT(NtGdiStretchBlt);
    NEEDSD(p, 4, 4, 4, 4);
    static const int e[][4] = { { 0, 4, 4, 4 }, { 4, 0, 4, 4 }, { 4, 4, 0, 4 }, { 4, 4, 4, 0 }, { 0, 0, 0, 0 } };
    for (int i = 0; i < 5; i++) {
        br_fill(&p.d, SENT);
        BOOL r = p_NtGdiStretchBlt(p.d.dc, 0, 0, e[i][0], e[i][1], p.s.dc, 0, 0, e[i][2], e[i][3], SRCCOPY, CLR_BAD); br_flush();
        t_note("StretchBlt dst %dx%d src %dx%d returned %d", e[i][0], e[i][1], e[i][2], e[i][3], r);
        CHECK(br_all(&p.d, 0, 0, 4, 4, SENT));
    }
    sd_free(&p);
}

/* ---- stretch-mode attribute (gdi32full!SetStretchBltModeImpl / GetStretchBltMode on the DC_ATTR) ---- */
TEST(gdi_blit_raster_stretchmode_set_returns_previous) {
    BR d = br_make(2, 2);
    if (!d.dc) { t_skip("no DC"); return; }
    int prev = U(SetStretchBltMode)(d.dc, COLORONCOLOR);
    t_note("initial stretch mode %d", prev);
    CHECK(prev >= 0);
    CHECK(U(GetStretchBltMode)(d.dc) == COLORONCOLOR);
    static const int seq[] = { BLACKONWHITE, WHITEONBLACK, HALFTONE, STRETCH_DELETESCANS, STRETCH_ORSCANS, STRETCH_ANDSCANS };
    int last = COLORONCOLOR;
    for (unsigned i = 0; i < sizeof seq / sizeof seq[0]; i++) {
        CHECKF(U(SetStretchBltMode)(d.dc, seq[i]) == last, "mode %d", seq[i]);
        CHECKF(U(GetStretchBltMode)(d.dc) == seq[i], "mode %d", seq[i]);
        last = seq[i];
    }
    br_free(&d);
}

TEST(gdi_blit_raster_stretchmode_alias_values) {
    CHECK(BLACKONWHITE == STRETCH_ANDSCANS && WHITEONBLACK == STRETCH_ORSCANS && COLORONCOLOR == STRETCH_DELETESCANS);
    CHECK(BLACKONWHITE == 1 && WHITEONBLACK == 2 && COLORONCOLOR == 3 && HALFTONE == 4);
}

/* SetStretchBltModeImpl stores the value raw (DC_ATTR+0xe8) and only a normalised copy (+0xd7) for the kernel. */
TEST(gdi_blit_raster_stretchmode_raw_value_stored) {
    BR d = br_make(2, 2);
    if (!d.dc) { t_skip("no DC"); return; }
    static const int raw[] = { 0, 5, 77, 0x7fffffff, -1 };
    for (unsigned i = 0; i < sizeof raw / sizeof raw[0]; i++) {
        U(SetStretchBltMode)(d.dc, raw[i]);
        CHECKF(U(GetStretchBltMode)(d.dc) == raw[i], "stored %d", raw[i]);
    }
    br_free(&d);
}

TEST(gdi_blit_raster_stretchmode_dc_attr_fields) {
    BR d = br_make(2, 2);
    if (!d.dc) { t_skip("no DC"); return; }
    struct k_gdicell c;
    if (!k_gdi_cell(d.dc, &c) || !c.field10) { t_skip("no GDI cell / DC_ATTR"); br_free(&d); return; }
    unsigned char *a = (unsigned char *)c.field10;
    static const struct { int in, norm; } t[] = { { BLACKONWHITE, 1 }, { WHITEONBLACK, 2 }, { COLORONCOLOR, 3 }, { HALFTONE, 4 }, { 0, 2 }, { 5, 2 }, { 99, 2 } };
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
        U(SetStretchBltMode)(d.dc, t[i].in);
        unsigned long raw = 0xdead; unsigned char nrm = 0xee;
        if (!k_try_read_u32(a + 0xe8, &raw) || !k_try_read_u8(a + 0xd7, &nrm)) { t_note("DC_ATTR not readable (emulator?)"); break; }
        if (t_is_wine()) { t_note("wine layout differs"); break; }
        CHECKF(raw == (unsigned long)t[i].in, "raw %lu want %d", raw, t[i].in);
        CHECKF(nrm == t[i].norm, "norm %u want %d for mode %d", nrm, t[i].norm, t[i].in);
    }
    br_free(&d);
}

TEST(gdi_blit_raster_stretchmode_invalid_dc) {
    BR tmp = br_make(2, 2); HDC dead = tmp.dc; br_free(&tmp);
    SetLastError(0);
    int r = U(SetStretchBltMode)(dead, COLORONCOLOR);
    DWORD e = GetLastError();
    CHECK(r == 0);
    CHECKF(e == ERROR_INVALID_PARAMETER, "error %lu", e);
    SetLastError(0);
    r = U(GetStretchBltMode)(dead);
    CHECK(r == 0);
    r = U(SetStretchBltMode)(NULL, COLORONCOLOR);
    CHECK(r == 0);
    CHECK(U(GetStretchBltMode)(NULL) == 0);
}

TEST(gdi_blit_raster_stretchmode_is_per_dc) {
    BR a = br_make(2, 2), b = br_make(2, 2);
    if (!a.dc || !b.dc) { t_skip("no DC"); br_free(&a); br_free(&b); return; }
    U(SetStretchBltMode)(a.dc, HALFTONE);
    U(SetStretchBltMode)(b.dc, COLORONCOLOR);
    CHECK(U(GetStretchBltMode)(a.dc) == HALFTONE);
    CHECK(U(GetStretchBltMode)(b.dc) == COLORONCOLOR);
    br_free(&a); br_free(&b);
}

TEST(gdi_blit_raster_stretchmode_survives_save_restore) {
    BR d = br_make(2, 2);
    if (!d.dc) { t_skip("no DC"); return; }
    U(SetStretchBltMode)(d.dc, COLORONCOLOR);
    int id = U(SaveDC)(d.dc);
    CHECK(id > 0);
    U(SetStretchBltMode)(d.dc, HALFTONE);
    CHECK(U(RestoreDC)(d.dc, id));
    int m = U(GetStretchBltMode)(d.dc);
    CHECKF(m == COLORONCOLOR, "after RestoreDC mode %d", m);
    br_free(&d);
}

TEST(gdi_blit_raster_stretchmode_fresh_dc_default) {
    BR d = br_make(2, 2);
    if (!d.dc) { t_skip("no DC"); return; }
    int m = U(GetStretchBltMode)(d.dc);
    t_note("fresh memory DC stretch mode %d (documented default BLACKONWHITE=1)", m);
    CHECK(m == BLACKONWHITE);
    br_free(&d);
}
