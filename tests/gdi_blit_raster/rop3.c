/* gdi_blit_raster: ternary raster operations through the NtGdiBitBlt / NtGdiPatBlt syscalls.
 *
 * Trick used throughout: with P = 0xF0F0F0 (solid brush), S = 0xCCCCCC and D = 0xAAAAAA the three
 * bit-planes of every colour byte enumerate all 8 (P,S,D) combinations, so the resulting byte of
 * each channel equals the ROP3 code byte itself. Results are read from the DIB bits after GdiFlush. */
#include "br.h"

#define PV 0xF0F0F0u
#define SV 0xCCCCCCu
#define DV 0xAAAAAAu

/* A 8x8 dest/src pair plus a solid brush selected into dest. */
typedef struct { BR d, s; HBRUSH br, oldbr; } Trio;
static int trio_make(Trio *t, int w, int h) {
    memset(t, 0, sizeof *t);
    t->d = br_make(w, h); t->s = br_make(w, h);
    if (!t->d.dc || !t->s.dc) return 0;
    t->br = U(CreateSolidBrush)(PX2CR(PV));
    if (!t->br) return 0;
    t->oldbr = (HBRUSH)U(SelectObject)(t->d.dc, t->br);
    br_fill(&t->s, SV);
    br_fill(&t->d, DV);
    return 1;
}
static void trio_free(Trio *t) {
    if (t->d.dc && t->oldbr) U(SelectObject)(t->d.dc, t->oldbr);
    br_free(&t->d); br_free(&t->s);
    if (t->br) U(DeleteObject)(t->br);
}
#define NEEDTRIO(t, w, h) Trio t; if (!trio_make(&t, w, h)) { trio_free(&t); t_skip("no DIB/DC"); return; }

/* ---- every one of the 256 ternary codes through the BitBlt syscall ---- */
TEST(gdi_blit_raster_rop3_all256_ntgdibitblt) {
    NT(NtGdiBitBlt); NEEDTRIO(t, 8, 8);
    int bad = 0;
    for (unsigned c = 0; c < 256; c++) {
        br_fill(&t.d, DV);
        BOOL r = p_NtGdiBitBlt(t.d.dc, 0, 0, 8, 8, t.s.dc, 0, 0, ROPCODE(c), CLR_BAD, 0);
        br_flush();
        DWORD want = rop3_apply(c, PV, SV, DV);
        CHECKF(want == c * 0x010101u, "model check code %02x", c);
        CHECKF(r, "NtGdiBitBlt rop %02x returned %d", c, r);
        int ok = br_all(&t.d, 0, 0, 8, 8, want);
        if (!ok) bad++;
        CHECKF(ok, "rop %02x: pixel(0,0)=%06x want %06x", c, br_at(&t.d, 0, 0), want);
    }
    CHECK(bad == 0);
    trio_free(&t);
}

/* Same sweep through user32-side BitBlt (gdi32full routes S-independent codes to PatBlt, which may be
 * batched; the result after GdiFlush must be identical). */
TEST(gdi_blit_raster_rop3_all256_userland_bitblt) {
    GDIF(NtGdiBitBlt); (void)p_NtGdiBitBlt;
    NEEDTRIO(t, 8, 8);
    int bad = 0;
    for (unsigned c = 0; c < 256; c++) {
        br_fill(&t.d, DV);
        BOOL r = U(BitBlt)(t.d.dc, 0, 0, 8, 8, t.s.dc, 0, 0, ROPCODE(c));
        br_flush();
        DWORD want = rop3_apply(c, PV, SV, DV);
        CHECKF(r, "BitBlt rop %02x returned %d", c, r);
        int ok = br_all(&t.d, 0, 0, 8, 8, want);
        if (!ok) bad++;
        CHECKF(ok, "rop %02x: pixel(0,0)=%06x want %06x", c, br_at(&t.d, 0, 0), want);
    }
    CHECK(bad == 0);
    trio_free(&t);
}

/* Per-pixel variety: a non-uniform source and dest so that each pixel exercises different bit planes. */
TEST(gdi_blit_raster_rop3_sampled_varied_pixels) {
    NT(NtGdiBitBlt); NEEDTRIO(t, 6, 6);
    static const unsigned codes[] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,
                                      0x0A, 0x0F, 0x1A, 0x2A, 0x3C, 0x5A, 0x69, 0x96, 0xA5, 0xB8, 0xC0, 0xCA, 0xE2, 0xF0, 0xFA };
    for (unsigned k = 0; k < sizeof codes / sizeof codes[0]; k++) {
        for (int y = 0; y < 6; y++) for (int x = 0; x < 6; x++) { br_set(&t.s, x, y, br_pat(x, y)); br_set(&t.d, x, y, br_pat(y + 3, x + 2)); }
        BOOL r = p_NtGdiBitBlt(t.d.dc, 0, 0, 6, 6, t.s.dc, 0, 0, ROPCODE(codes[k]), CLR_BAD, 0);
        br_flush();
        CHECKF(r, "rop %02x", codes[k]);
        for (int y = 0; y < 6; y++) for (int x = 0; x < 6; x++) {
            DWORD want = rop3_apply(codes[k], PV, br_pat(x, y), br_pat(y + 3, x + 2));
            CHECKF(br_at(&t.d, x, y) == want, "rop %02x (%d,%d) got %06x want %06x", codes[k], x, y, br_at(&t.d, x, y), want);
        }
    }
    trio_free(&t);
}

/* Named ROPs, one TEST each: the documented 32-bit codes (low word included) through userland BitBlt. */
#define NAMED_ROP(nm, full, byte) \
TEST(gdi_blit_raster_rop3_named_##nm) { \
    NEEDTRIO(t, 5, 4); \
    BOOL r = U(BitBlt)(t.d.dc, 1, 1, 3, 2, t.s.dc, 0, 0, (full)); br_flush(); \
    CHECK(r); \
    DWORD want = rop3_apply(byte, PV, SV, DV); \
    CHECK(br_all(&t.d, 1, 1, 4, 3, want)); \
    /* nothing outside the 3x2 destination rectangle is touched */ \
    CHECK(br_count(&t.d, 0, 0, 5, 4, DV) == 20 - 6 || want == DV); \
    CHECK(br_at(&t.d, 0, 0) == DV && br_at(&t.d, 4, 3) == DV && br_at(&t.d, 4, 0) == DV && br_at(&t.d, 0, 3) == DV); \
    trio_free(&t); \
}
NAMED_ROP(srccopy,    SRCCOPY,    0xCC)
NAMED_ROP(srcpaint,   SRCPAINT,   0xEE)
NAMED_ROP(srcand,     SRCAND,     0x88)
NAMED_ROP(srcinvert,  SRCINVERT,  0x66)
NAMED_ROP(srcerase,   SRCERASE,   0x44)
NAMED_ROP(notsrccopy, NOTSRCCOPY, 0x33)
NAMED_ROP(notsrcerase,NOTSRCERASE,0x11)
NAMED_ROP(mergecopy,  MERGECOPY,  0xC0)
NAMED_ROP(mergepaint, MERGEPAINT, 0xBB)
NAMED_ROP(patcopy,    PATCOPY,    0xF0)
NAMED_ROP(patpaint,   PATPAINT,   0xFB)
NAMED_ROP(patinvert,  PATINVERT,  0x5A)
NAMED_ROP(dstinvert,  DSTINVERT,  0x55)
NAMED_ROP(blackness,  BLACKNESS,  0x00)
NAMED_ROP(whiteness,  WHITENESS,  0xFF)

/* The documented full codes carry the byte in bits 16..23; check the table itself. */
TEST(gdi_blit_raster_rop3_named_code_bytes) {
    static const struct { DWORD full; unsigned byte; } tab[] = {
        { SRCCOPY, 0xCC }, { SRCPAINT, 0xEE }, { SRCAND, 0x88 }, { SRCINVERT, 0x66 }, { SRCERASE, 0x44 },
        { NOTSRCCOPY, 0x33 }, { NOTSRCERASE, 0x11 }, { MERGECOPY, 0xC0 }, { MERGEPAINT, 0xBB }, { PATCOPY, 0xF0 },
        { PATPAINT, 0xFB }, { PATINVERT, 0x5A }, { DSTINVERT, 0x55 }, { BLACKNESS, 0x00 }, { WHITENESS, 0xFF } };
    for (unsigned i = 0; i < sizeof tab / sizeof tab[0]; i++) CHECKF(((tab[i].full >> 16) & 0xff) == tab[i].byte, "entry %u", i);
}

/* Only bits 16..23 of the rop select the operation: the low word is ignored by the kernel
 * (NtGdiPatBlt derives its operation from (rop >> 16) & 0xff). */
TEST(gdi_blit_raster_rop3_low_word_ignored) {
    NT(NtGdiPatBlt); NEEDTRIO(t, 4, 4);
    static const DWORD lows[] = { 0x0000, 0x0021, 0x0001, 0xFFFF, 0x1234 };
    for (unsigned i = 0; i < sizeof lows / sizeof lows[0]; i++) {
        br_fill(&t.d, DV);
        BOOL r = p_NtGdiPatBlt(t.d.dc, 0, 0, 4, 4, ROPCODE(0xF0) | lows[i]);
        br_flush();
        CHECKF(r, "low %04lx", lows[i]);
        CHECKF(br_all(&t.d, 0, 0, 4, 4, PV), "low %04lx pixel %06x", lows[i], br_at(&t.d, 0, 0));
    }
    trio_free(&t);
}

/* 0x00AA0029 is the documented "leave destination unchanged" ROP (D). */
TEST(gdi_blit_raster_rop3_noop_00AA0029) {
    NT(NtGdiBitBlt); NEEDTRIO(t, 4, 4);
    for (int i = 0; i < 16; i++) t.d.px[i] = 0x01000000 | (DV + i);
    BOOL r = p_NtGdiBitBlt(t.d.dc, 0, 0, 4, 4, t.s.dc, 0, 0, 0x00AA0029, CLR_BAD, 0);
    br_flush();
    CHECK(r);
    for (int i = 0; i < 16; i++) CHECKF((t.d.px[i] & M24) == ((DV + i) & M24), "pixel %d", i);
    trio_free(&t);
}

/* ---- PatBlt syscall: only source-independent codes are accepted ---- */
TEST(gdi_blit_raster_patblt_rop_acceptance_all256) {
    NT(NtGdiPatBlt); NEEDTRIO(t, 4, 4);
    int accepted = 0;
    for (unsigned c = 0; c < 256; c++) {
        /* NtGdiPatBlt: if ((x*4 ^ x) & 0xcc) == 0 with x = c|c<<8 it draws, else it returns 0 untouched. */
        int sindep = ((c ^ (c >> 2)) & 0x33) == 0 ? 1 : 0;   /* c bit k == c bit k+2 for k in {0,1,4,5} */
        int idx_ok = 1;
        for (int i = 0; i < 8; i++) if (((c >> i) & 1) != ((c >> (i ^ 2)) & 1)) idx_ok = 0;
        CHECKF(sindep == idx_ok, "model %02x", c);
        br_fill(&t.d, DV);
        BOOL r = p_NtGdiPatBlt(t.d.dc, 0, 0, 4, 4, ROPCODE(c));
        br_flush();
        if (idx_ok) {
            accepted++;
            CHECKF(r, "S-independent rop %02x rejected", c);
            CHECKF(br_all(&t.d, 0, 0, 4, 4, rop3_apply(c, PV, 0, DV)), "rop %02x got %06x", c, br_at(&t.d, 0, 0));
        } else {
            CHECKF(!r, "S-dependent rop %02x accepted", c);
            CHECKF(br_all(&t.d, 0, 0, 4, 4, DV), "rop %02x modified dest", c);
        }
    }
    CHECK(accepted == 16);   /* 2^4 functions of (P, D) */
    trio_free(&t);
}

TEST(gdi_blit_raster_patblt_documented_patterns) {
    NT(NtGdiPatBlt); NEEDTRIO(t, 4, 4);
    static const struct { DWORD rop; DWORD want; } tab[] = {
        { PATCOPY, PV }, { PATINVERT, PV ^ DV }, { DSTINVERT, DV ^ M24 }, { BLACKNESS, 0 }, { WHITENESS, M24 } };
    for (int i = 0; i < 5; i++) {
        br_fill(&t.d, DV);
        BOOL r = p_NtGdiPatBlt(t.d.dc, 0, 0, 4, 4, tab[i].rop); br_flush();
        CHECKF(r, "entry %d", i);
        CHECKF(br_all(&t.d, 0, 0, 4, 4, tab[i].want), "entry %d got %06x want %06x", i, br_at(&t.d, 0, 0), tab[i].want);
    }
    trio_free(&t);
}

/* PatBlt with an empty extent: the kernel reduces the rect, sees it empty and returns 1 without drawing. */
TEST(gdi_blit_raster_patblt_empty_extents_success_noop) {
    NT(NtGdiPatBlt); NEEDTRIO(t, 4, 4);
    static const int ext[][2] = { { 0, 3 }, { 3, 0 }, { 0, 0 } };
    for (int i = 0; i < 3; i++) {
        br_fill(&t.d, DV);
        BOOL r = p_NtGdiPatBlt(t.d.dc, 1, 1, ext[i][0], ext[i][1], ROPCODE(0xF0)); br_flush();
        CHECKF(r == TRUE, "extent %d,%d", ext[i][0], ext[i][1]);
        CHECKF(br_all(&t.d, 0, 0, 4, 4, DV), "extent %d,%d touched pixels", ext[i][0], ext[i][1]);
    }
    trio_free(&t);
}

/* A negative extent normalises to the half-open rectangle [x+w, x): NtGdiPatBlt swaps the corners. */
TEST(gdi_blit_raster_patblt_negative_extent_normalises) {
    NT(NtGdiPatBlt); NEEDTRIO(t, 8, 8);
    BOOL r = p_NtGdiPatBlt(t.d.dc, 5, 6, -3, -2, ROPCODE(0xF0)); br_flush();
    CHECK(r);
    CHECK(br_all(&t.d, 2, 4, 5, 6, PV));
    CHECK(br_count(&t.d, 0, 0, 8, 8, PV) == 6);
    br_fill(&t.d, DV);
    r = p_NtGdiPatBlt(t.d.dc, 5, 2, -3, 2, ROPCODE(0xF0)); br_flush();
    CHECK(r);
    CHECK(br_all(&t.d, 2, 2, 5, 4, PV));
    CHECK(br_count(&t.d, 0, 0, 8, 8, PV) == 6);
    trio_free(&t);
}

/* Clipping of the PatBlt rectangle against the surface: pixels outside are simply not drawn. */
TEST(gdi_blit_raster_patblt_clipped_to_surface) {
    NT(NtGdiPatBlt); NEEDTRIO(t, 8, 8);
    BOOL r = p_NtGdiPatBlt(t.d.dc, -4, -4, 8, 8, ROPCODE(0xF0)); br_flush();
    CHECK(r);
    CHECK(br_all(&t.d, 0, 0, 4, 4, PV));
    CHECK(br_count(&t.d, 0, 0, 8, 8, PV) == 16);
    br_fill(&t.d, DV);
    r = p_NtGdiPatBlt(t.d.dc, 6, 6, 100, 100, ROPCODE(0xF0)); br_flush();
    CHECK(r);
    CHECK(br_all(&t.d, 6, 6, 8, 8, PV));
    CHECK(br_count(&t.d, 0, 0, 8, 8, PV) == 4);
    br_fill(&t.d, DV);
    r = p_NtGdiPatBlt(t.d.dc, 100, 100, 5, 5, ROPCODE(0xF0)); br_flush();
    t_note("fully off-surface PatBlt returned %d", r);
    CHECK(br_all(&t.d, 0, 0, 8, 8, DV));
    trio_free(&t);
}

/* ---- flags OR-ed into the BitBlt rop ---- */
TEST(gdi_blit_raster_bitblt_captureblt_flag) {
    GDIF(NtGdiBitBlt); (void)p_NtGdiBitBlt; NEEDTRIO(t, 4, 4);
    BOOL r = U(BitBlt)(t.d.dc, 0, 0, 4, 4, t.s.dc, 0, 0, SRCCOPY | CAPTUREBLT); br_flush();
    CHECK(r);
    CHECK(br_all(&t.d, 0, 0, 4, 4, SV));
    br_fill(&t.d, DV);
    BOOL r2 = U(BitBlt)(t.d.dc, 0, 0, 4, 4, t.s.dc, 0, 0, NOTSRCCOPY | CAPTUREBLT); br_flush();
    CHECK(r2);
    CHECK(br_all(&t.d, 0, 0, 4, 4, SV ^ M24));
    trio_free(&t);
}

TEST(gdi_blit_raster_bitblt_nomirrorbitmap_flag) {
    GDIF(NtGdiBitBlt); (void)p_NtGdiBitBlt; NEEDTRIO(t, 4, 4);
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) br_set(&t.s, x, y, br_pat(x, y));
    BOOL r = U(BitBlt)(t.d.dc, 0, 0, 4, 4, t.s.dc, 0, 0, SRCCOPY | NOMIRRORBITMAP); br_flush();
    CHECK(r);
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) CHECKF(br_at(&t.d, x, y) == br_pat(x, y), "(%d,%d)", x, y);
    trio_free(&t);
}

TEST(gdi_blit_raster_bitblt_flags_through_syscall_fl_arg) {
    NT(NtGdiBitBlt); NEEDTRIO(t, 4, 4);
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) br_set(&t.s, x, y, br_pat(x, y));
    /* the userland wrappers pass crBackColor = 0xFFFFFFFF and fl = 0 ; bit 2 of fl is a "dynamic" flag only */
    BOOL r = p_NtGdiBitBlt(t.d.dc, 0, 0, 4, 4, t.s.dc, 0, 0, SRCCOPY, CLR_BAD, 0); br_flush();
    CHECK(r);
    for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) CHECK(br_at(&t.d, x, y) == br_pat(x, y));
    trio_free(&t);
}

/* ---- offsets, sub-rectangles and overlap ---- */
TEST(gdi_blit_raster_bitblt_source_and_dest_offsets) {
    NT(NtGdiBitBlt); NEEDTRIO(t, 10, 10);
    for (int y = 0; y < 10; y++) for (int x = 0; x < 10; x++) br_set(&t.s, x, y, br_pat(x, y));
    br_fill(&t.d, DV);
    BOOL r = p_NtGdiBitBlt(t.d.dc, 2, 3, 4, 5, t.s.dc, 5, 1, ROPCODE(0xCC), CLR_BAD, 0); br_flush();
    CHECK(r);
    for (int y = 0; y < 10; y++) for (int x = 0; x < 10; x++) {
        DWORD want = (x >= 2 && x < 6 && y >= 3 && y < 8) ? br_pat(x - 2 + 5, y - 3 + 1) : DV;
        CHECKF(br_at(&t.d, x, y) == want, "(%d,%d)", x, y);
    }
    trio_free(&t);
}

TEST(gdi_blit_raster_bitblt_overlap_same_dib_down_right) {
    NT(NtGdiBitBlt); NEEDTRIO(t, 8, 8);
    BR *d = &t.d; DWORD snap[64];
    br_fill_pat(d); memcpy(snap, d->px, sizeof snap);
    BOOL r = p_NtGdiBitBlt(d->dc, 2, 2, 5, 5, d->dc, 0, 0, ROPCODE(0xCC), CLR_BAD, 0); br_flush();
    CHECK(r);
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {
        DWORD want = (x >= 2 && x < 7 && y >= 2 && y < 7) ? snap[(y - 2) * 8 + (x - 2)] & M24 : snap[y * 8 + x] & M24;
        CHECKF(br_at(d, x, y) == want, "(%d,%d) got %06x want %06x", x, y, br_at(d, x, y), want);
    }
    trio_free(&t);
}

TEST(gdi_blit_raster_bitblt_overlap_same_dib_up_left) {
    NT(NtGdiBitBlt); NEEDTRIO(t, 8, 8);
    BR *d = &t.d; DWORD snap[64];
    br_fill_pat(d); memcpy(snap, d->px, sizeof snap);
    BOOL r = p_NtGdiBitBlt(d->dc, 0, 0, 5, 5, d->dc, 2, 2, ROPCODE(0xCC), CLR_BAD, 0); br_flush();
    CHECK(r);
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {
        DWORD want = (x < 5 && y < 5) ? snap[(y + 2) * 8 + (x + 2)] & M24 : snap[y * 8 + x] & M24;
        CHECKF(br_at(d, x, y) == want, "(%d,%d)", x, y);
    }
    trio_free(&t);
}

TEST(gdi_blit_raster_bitblt_overlap_horizontal_shift) {
    NT(NtGdiBitBlt); NEEDTRIO(t, 12, 3);
    BR *d = &t.d; DWORD snap[36];
    br_fill_pat(d); memcpy(snap, d->px, sizeof snap);
    BOOL r = p_NtGdiBitBlt(d->dc, 1, 0, 10, 3, d->dc, 0, 0, ROPCODE(0xCC), CLR_BAD, 0); br_flush();
    CHECK(r);
    for (int y = 0; y < 3; y++) for (int x = 1; x < 11; x++) CHECKF(br_at(d, x, y) == (snap[y * 12 + x - 1] & M24), "(%d,%d)", x, y);
    trio_free(&t);
}

TEST(gdi_blit_raster_bitblt_overlap_srcinvert_self_clears) {
    NT(NtGdiBitBlt); NEEDTRIO(t, 6, 6);
    br_fill_pat(&t.d);
    BOOL r = p_NtGdiBitBlt(t.d.dc, 0, 0, 6, 6, t.d.dc, 0, 0, ROPCODE(0x66), CLR_BAD, 0); br_flush();
    CHECK(r);
    CHECK(br_all(&t.d, 0, 0, 6, 6, 0));
    trio_free(&t);
}

/* ---- clip region on the destination ---- */
TEST(gdi_blit_raster_bitblt_clip_rect_region) {
    NT(NtGdiBitBlt); NEEDTRIO(t, 8, 8);
    HRGN rg = U(CreateRectRgn)(2, 2, 6, 5);
    CHECK(rg != NULL);
    int sc = U(SelectClipRgn)(t.d.dc, rg);
    CHECK(sc == SIMPLEREGION);
    BOOL r = p_NtGdiBitBlt(t.d.dc, 0, 0, 8, 8, t.s.dc, 0, 0, ROPCODE(0xCC), CLR_BAD, 0); br_flush();
    CHECK(r);
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++)
        CHECKF(br_at(&t.d, x, y) == ((x >= 2 && x < 6 && y >= 2 && y < 5) ? SV : DV), "(%d,%d)", x, y);
    U(SelectClipRgn)(t.d.dc, NULL);
    U(DeleteObject)(rg);
    trio_free(&t);
}

TEST(gdi_blit_raster_bitblt_clip_complex_region) {
    NT(NtGdiPatBlt); NEEDTRIO(t, 8, 8);
    HRGN a = U(CreateRectRgn)(0, 0, 3, 3), b = U(CreateRectRgn)(5, 5, 8, 8);
    CHECK(U(CombineRgn)(a, a, b, RGN_OR) == COMPLEXREGION);
    U(SelectClipRgn)(t.d.dc, a);
    BOOL r = p_NtGdiPatBlt(t.d.dc, 0, 0, 8, 8, ROPCODE(0xF0)); br_flush();
    CHECK(r);
    CHECK(br_count(&t.d, 0, 0, 8, 8, PV) == 18);
    CHECK(br_all(&t.d, 0, 0, 3, 3, PV) && br_all(&t.d, 5, 5, 8, 8, PV));
    CHECK(br_at(&t.d, 4, 4) == DV);
    U(SelectClipRgn)(t.d.dc, NULL);
    U(DeleteObject)(a); U(DeleteObject)(b);
    trio_free(&t);
}

TEST(gdi_blit_raster_bitblt_clip_empty_region_blocks_drawing) {
    NT(NtGdiPatBlt); NEEDTRIO(t, 4, 4);
    HRGN e = U(CreateRectRgn)(0, 0, 0, 0);
    U(SelectClipRgn)(t.d.dc, e);
    BOOL r = p_NtGdiPatBlt(t.d.dc, 0, 0, 4, 4, ROPCODE(0xF0)); br_flush();
    t_note("PatBlt with empty clip returned %d", r);
    CHECK(br_all(&t.d, 0, 0, 4, 4, DV));
    U(SelectClipRgn)(t.d.dc, NULL);
    U(DeleteObject)(e);
    trio_free(&t);
}

TEST(gdi_blit_raster_bitblt_exclude_clip_rect) {
    NT(NtGdiPatBlt); NEEDTRIO(t, 6, 6);
    U(ExcludeClipRect)(t.d.dc, 2, 2, 4, 4);
    BOOL r = p_NtGdiPatBlt(t.d.dc, 0, 0, 6, 6, ROPCODE(0xF0)); br_flush();
    CHECK(r);
    CHECK(br_count(&t.d, 0, 0, 6, 6, PV) == 32);
    CHECK(br_all(&t.d, 2, 2, 4, 4, DV));
    U(SelectClipRgn)(t.d.dc, NULL);
    trio_free(&t);
}

/* ---- failure cases ---- */
TEST(gdi_blit_raster_bitblt_null_dest_dc_fails) {
    NT(NtGdiBitBlt); NEEDTRIO(t, 4, 4);
    SetLastError(0);
    BOOL r = p_NtGdiBitBlt(NULL, 0, 0, 4, 4, t.s.dc, 0, 0, ROPCODE(0xCC), CLR_BAD, 0);
    CHECK(!r);
    r = U(BitBlt)(NULL, 0, 0, 4, 4, t.s.dc, 0, 0, SRCCOPY);
    CHECK(!r);
    CHECK(br_all(&t.d, 0, 0, 4, 4, DV));
    trio_free(&t);
}

TEST(gdi_blit_raster_bitblt_null_src_dc_with_source_rop_fails) {
    NT(NtGdiBitBlt); NEEDTRIO(t, 4, 4);
    BOOL r = p_NtGdiBitBlt(t.d.dc, 0, 0, 4, 4, NULL, 0, 0, ROPCODE(0xCC), CLR_BAD, 0); br_flush();
    CHECK(!r);
    CHECK(br_all(&t.d, 0, 0, 4, 4, DV));
    trio_free(&t);
}

TEST(gdi_blit_raster_bitblt_null_src_dc_pattern_only_rop_succeeds) {
    NT(NtGdiBitBlt); NEEDTRIO(t, 4, 4);
    /* A source-independent rop never touches the source DC: the userland BitBlt hands it to PatBlt. */
    BOOL r = U(BitBlt)(t.d.dc, 0, 0, 4, 4, NULL, 0, 0, PATCOPY); br_flush();
    CHECK(r);
    CHECK(br_all(&t.d, 0, 0, 4, 4, PV));
    r = U(BitBlt)(t.d.dc, 0, 0, 4, 4, NULL, 0, 0, BLACKNESS); br_flush();
    CHECK(r);
    CHECK(br_all(&t.d, 0, 0, 4, 4, 0));
    trio_free(&t);
}

TEST(gdi_blit_raster_bitblt_deleted_dest_dc_fails) {
    NT(NtGdiBitBlt); NEEDTRIO(t, 4, 4);
    BR tmp = br_make(4, 4);
    CHECK(tmp.dc != NULL);
    HDC dead = tmp.dc;
    br_free(&tmp);
    BOOL r = p_NtGdiBitBlt(dead, 0, 0, 4, 4, t.s.dc, 0, 0, ROPCODE(0xCC), CLR_BAD, 0);
    CHECK(!r);
    r = p_NtGdiBitBlt(t.d.dc, 0, 0, 4, 4, dead, 0, 0, ROPCODE(0xCC), CLR_BAD, 0);
    CHECK(!r);
    CHECK(!U(DeleteDC)(dead));
    trio_free(&t);
}

TEST(gdi_blit_raster_bitblt_wrong_object_type_as_dc_fails) {
    NT(NtGdiBitBlt); NT(NtGdiPatBlt); NEEDTRIO(t, 4, 4);
    /* A brush / bitmap handle is not a DC: locking the handle as a DC fails -> 0. */
    CHECK(!p_NtGdiBitBlt((HDC)t.br, 0, 0, 4, 4, t.s.dc, 0, 0, ROPCODE(0xCC), CLR_BAD, 0));
    CHECK(!p_NtGdiBitBlt((HDC)t.d.bmp, 0, 0, 4, 4, t.s.dc, 0, 0, ROPCODE(0xCC), CLR_BAD, 0));
    CHECK(!p_NtGdiBitBlt(t.d.dc, 0, 0, 4, 4, (HDC)t.br, 0, 0, ROPCODE(0xCC), CLR_BAD, 0));
    CHECK(!p_NtGdiPatBlt((HDC)t.br, 0, 0, 4, 4, ROPCODE(0xF0)));
    CHECK(!p_NtGdiPatBlt((HDC)(ULONG_PTR)0x7fff0001, 0, 0, 4, 4, ROPCODE(0xF0)));
    CHECK(!p_NtGdiPatBlt(NULL, 0, 0, 4, 4, ROPCODE(0xF0)));
    CHECK(br_all(&t.d, 0, 0, 4, 4, DV));
    trio_free(&t);
}

TEST(gdi_blit_raster_bitblt_zero_extent_noop) {
    NT(NtGdiBitBlt); NEEDTRIO(t, 4, 4);
    static const int ext[][2] = { { 0, 4 }, { 4, 0 }, { 0, 0 } };
    for (int i = 0; i < 3; i++) {
        br_fill(&t.d, DV);
        BOOL r = p_NtGdiBitBlt(t.d.dc, 0, 0, ext[i][0], ext[i][1], t.s.dc, 0, 0, ROPCODE(0xCC), CLR_BAD, 0); br_flush();
        t_note("BitBlt %dx%d returned %d", ext[i][0], ext[i][1], r);
        CHECK(br_all(&t.d, 0, 0, 4, 4, DV));
    }
    trio_free(&t);
}

/* ---- the ROP of a PatBlt-class call tracks the DC's brush and brush origin ---- */
TEST(gdi_blit_raster_rop3_pattern_follows_selected_brush) {
    NT(NtGdiBitBlt); NEEDTRIO(t, 4, 4);
    HBRUSH b2 = U(CreateSolidBrush)(RGB(0x12, 0x34, 0x56));
    HGDIOBJ old = U(SelectObject)(t.d.dc, b2);
    CHECK(old == (HGDIOBJ)t.br);
    BOOL r = p_NtGdiBitBlt(t.d.dc, 0, 0, 4, 4, t.s.dc, 0, 0, ROPCODE(0xB8), CLR_BAD, 0); br_flush();   /* P ? S : D... code B8: (P & (S ^ D)) ^ D */
    CHECK(r);
    CHECK(br_all(&t.d, 0, 0, 4, 4, rop3_apply(0xB8, RGBPX(0x12, 0x34, 0x56), SV, DV)));
    U(SelectObject)(t.d.dc, old);
    U(DeleteObject)(b2);
    trio_free(&t);
}

TEST(gdi_blit_raster_rop3_stock_brushes) {
    NT(NtGdiPatBlt); NEEDTRIO(t, 4, 4);
    static const struct { int stock; DWORD px; } tab[] = {
        { WHITE_BRUSH, 0xFFFFFF }, { BLACK_BRUSH, 0x000000 }, { NULL_BRUSH, 0 } };
    for (int i = 0; i < 2; i++) {
        HGDIOBJ sb = U(GetStockObject)(tab[i].stock);
        HGDIOBJ old = U(SelectObject)(t.d.dc, sb);
        br_fill(&t.d, DV);
        BOOL r = p_NtGdiPatBlt(t.d.dc, 0, 0, 4, 4, ROPCODE(0xF0)); br_flush();
        CHECK(r);
        CHECKF(br_all(&t.d, 0, 0, 4, 4, tab[i].px), "stock %d got %06x", tab[i].stock, br_at(&t.d, 0, 0));
        U(SelectObject)(t.d.dc, old);
    }
    /* NULL_BRUSH: PATCOPY draws nothing (the null brush is a no-op for the pattern ROPs) */
    HGDIOBJ old = U(SelectObject)(t.d.dc, U(GetStockObject)(NULL_BRUSH));
    br_fill(&t.d, DV);
    BOOL r = p_NtGdiPatBlt(t.d.dc, 0, 0, 4, 4, ROPCODE(0xF0)); br_flush();
    t_note("NULL_BRUSH PATCOPY returned %d, pixel %06x", r, br_at(&t.d, 0, 0));
    U(SelectObject)(t.d.dc, old);
    trio_free(&t);
}
