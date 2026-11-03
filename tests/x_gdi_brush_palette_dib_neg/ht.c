/* NtGdiHT_Get8BPPFormatPalette and NtGdiHT_Get8BPPMaskPalette: the halftone palette helpers. A NULL output asks for the
 * entry count; with a buffer the routine fills that many palette entries and returns the count. Checks: the count
 * query for each argument combination, how many entries a real buffer receives (against a buffer that ends at an
 * inaccessible page), which entries stay untouched, the structure of the produced palette (distinct colours, black and
 * white present) and that the result does not depend on caller-supplied garbage. */
#include "xb.h"

static xb_fn14 stub(const char *nm) { return t_is_wine() ? NULL : (xb_fn14)t_proc_in("win32u.dll", nm); }
#define F(nm) xb_fn14 f = stub(nm); REQUIRE(f)
#define A0 0,0,0,0,0,0,0,0,0,0
#define A8 0,0,0,0,0,0,0,0

/* Count queries (NULL output). */
/* One row per argument combination; the format palette is always the 216-entry cube. The mask palette has 216 entries
 * when its second argument is zero and 256 otherwise, whatever the colour-mask and gamma arguments are. */
XT(ht_format_count_query) {
    F("NtGdiHT_Get8BPPFormatPalette"); ULONG_PTR r;
    SetLastError(0); r = f (0,0,0,0,A0); CHECKF(r == 216 && GetLastError() == 0, "zero: %llu", (unsigned long long)r);
    SetLastError(0); r = f (0,10000,10000,10000,A0); CHECKF(r == 216 && GetLastError() == 0, "gamma_default: %llu", (unsigned long long)r);
    SetLastError(0); r = f (0,20000,20000,20000,A0); CHECKF(r == 216 && GetLastError() == 0, "gamma_2: %llu", (unsigned long long)r);
    SetLastError(0); r = f (0,65535,65535,65535,A0); CHECKF(r == 216 && GetLastError() == 0, "gamma_max: %llu", (unsigned long long)r);
    SetLastError(0); r = f (0,0xFFFF0001,0xFFFF0001,0xFFFF0001,A0); CHECKF(r == 216 && GetLastError() == 0, "gamma_junk_high: %llu", (unsigned long long)r);
    SetLastError(0); r = f (0,5000,0,0,A0); CHECKF(r == 216 && GetLastError() == 0, "r_only: %llu", (unsigned long long)r);
}
XT(ht_mask_count_query) {
    F("NtGdiHT_Get8BPPMaskPalette"); ULONG_PTR r;
    SetLastError(0); r = f (0,0,0,0,0,0,A8); CHECKF(r == 216 && GetLastError() == 0, "zero: %llu", (unsigned long long)r);
    SetLastError(0); r = f (0,1,0,0,0,0,A8); CHECKF(r == 256 && GetLastError() == 0, "use8: %llu", (unsigned long long)r);
    SetLastError(0); r = f (0,1,0xFF,0,0,0,A8); CHECKF(r == 256 && GetLastError() == 0, "use8_cmy: %llu", (unsigned long long)r);
    SetLastError(0); r = f (0,0,0xFF,0,0,0,A8); CHECKF(r == 216 && GetLastError() == 0, "use0_cmy: %llu", (unsigned long long)r);
    SetLastError(0); r = f (0,2,0,0,0,0,A8); CHECKF(r == 256 && GetLastError() == 0, "use2: %llu", (unsigned long long)r);
    SetLastError(0); r = f (0,0xFFFFFFFF,0,0,0,0,A8); CHECKF(r == 256 && GetLastError() == 0, "use_max: %llu", (unsigned long long)r);
    SetLastError(0); r = f (0,1,0,20000,20000,20000,A8); CHECKF(r == 256 && GetLastError() == 0, "use8_gamma: %llu", (unsigned long long)r);
    SetLastError(0); r = f (0,0,0,20000,20000,20000,A8); CHECKF(r == 216 && GetLastError() == 0, "use0_gamma: %llu", (unsigned long long)r);
}

/* A real buffer. The count comes back and exactly that many 4-byte entries are written. */
static void fill_probe(const char *nm, int mask, int use, const char *tag, unsigned long long want) {
    F(nm);
    BYTE sent = 0xCD; struct xb_guard g;
    if (!xb_guard_open(&g, 1024)) { t_skip("guard"); return; }
    memset(g.p, sent, 1024);
    SetLastError(0);
    ULONG_PTR r = mask ? f((ULONG_PTR)g.p, use, 0, 10000, 10000, 10000, 0,0,0,0,0,0,0,0) : f((ULONG_PTR)g.p, 10000, 10000, 10000, 0,0,0,0,0,0,0,0,0,0);
    DWORD e = GetLastError();
    int touched = 0; for (int i = 0; i < 1024; i += 4) { int t = 0; for (int j = 0; j < 4; j++) if (g.p[i + j] != sent) t = 1; if (t) touched = i / 4 + 1; }
    CHECKF(r == want, "%s: returned %llu want %llu", tag, (unsigned long long)r, want);
    CHECKF(e == 0, "%s: error %lu", tag, (unsigned long)e);
    CHECKF((unsigned long long)touched == want, "%s: last written entry %d want %llu", tag, touched, want);
    xb_guard_close(&g);
}
XT(ht_format_fill_probe) { fill_probe("NtGdiHT_Get8BPPFormatPalette", 0, 0, "fmt_fill", 216); }
XT(ht_mask_fill_probe_use0) { fill_probe("NtGdiHT_Get8BPPMaskPalette", 1, 0, "mask_fill0", 216); }
XT(ht_mask_fill_probe_use1) { fill_probe("NtGdiHT_Get8BPPMaskPalette", 1, 1, "mask_fill1", 256); }

/* Buffer exactly the size of the count query: writes stay inside. */
static void exact_buffer(const char *nm, int mask, int use) {
    F(nm);
    ULONG_PTR n = mask ? f(0, use, 0, 10000, 10000, 10000, 0,0,0,0,0,0,0,0) : f(0, 10000, 10000, 10000, 0,0,0,0,0,0,0,0,0,0);
    if (!n || n > 256) { CHECKF(0, "count query returned %llu", (unsigned long long)n); return; }
    struct xb_guard g; if (!xb_guard_open(&g, n * 4)) { t_skip("guard"); return; }
    SetLastError(0);
    ULONG_PTR r = mask ? f((ULONG_PTR)g.p, use, 0, 10000, 10000, 10000, 0,0,0,0,0,0,0,0) : f((ULONG_PTR)g.p, 10000, 10000, 10000, 0,0,0,0,0,0,0,0,0,0);
    CHECKF(r == n, "fill returned %llu, count query %llu", (unsigned long long)r, (unsigned long long)n);
    /* distinct, includes black and white */
    PALETTEENTRY *e = (PALETTEENTRY *)g.p; int black = 0, white = 0, dup = 0;
    for (unsigned i = 0; i < n; i++) {
        if (!e[i].peRed && !e[i].peGreen && !e[i].peBlue) black++;
        if (e[i].peRed == 255 && e[i].peGreen == 255 && e[i].peBlue == 255) white++;
        for (unsigned j = 0; j < i; j++) if (e[i].peRed == e[j].peRed && e[i].peGreen == e[j].peGreen && e[i].peBlue == e[j].peBlue) dup++;
    }
    CHECKF(black == 1 && white == 1 && dup == 0, "%s: black %d white %d duplicates %d", nm, black, white, dup);
    xb_guard_close(&g);
}
XT(ht_format_exact_buffer) { exact_buffer("NtGdiHT_Get8BPPFormatPalette", 0, 0); }
XT(ht_mask_exact_buffer_use0) { exact_buffer("NtGdiHT_Get8BPPMaskPalette", 1, 0); }
XT(ht_mask_exact_buffer_use1) { exact_buffer("NtGdiHT_Get8BPPMaskPalette", 1, 1); }

/* Buffer one entry short: the kernel's write probe fails instead of writing past the end. */
static void short_buffer(const char *nm, int mask, int use) {
    F(nm);
    ULONG_PTR n = mask ? f(0, use, 0, 10000, 10000, 10000, 0,0,0,0,0,0,0,0) : f(0, 10000, 10000, 10000, 0,0,0,0,0,0,0,0,0,0);
    struct xb_guard g; if (n < 2 || !xb_guard_open(&g, (n - 1) * 4)) { t_skip("guard"); return; }
    SetLastError(0);
    ULONG_PTR r = mask ? f((ULONG_PTR)g.p, use, 0, 10000, 10000, 10000, 0,0,0,0,0,0,0,0) : f((ULONG_PTR)g.p, 10000, 10000, 10000, 0,0,0,0,0,0,0,0,0,0);
    CHECKF(r == 0 && GetLastError() == 0, "%s: short buffer returned %llu error %lu", nm, (unsigned long long)r, (unsigned long)GetLastError());
    xb_guard_close(&g);
}
XT(ht_format_short_buffer) { short_buffer("NtGdiHT_Get8BPPFormatPalette", 0, 0); }
XT(ht_mask_short_buffer_use0) { short_buffer("NtGdiHT_Get8BPPMaskPalette", 1, 0); }
XT(ht_mask_short_buffer_use1) { short_buffer("NtGdiHT_Get8BPPMaskPalette", 1, 1); }

XT(ht_format_invalid_pointers) {
    F("NtGdiHT_Get8BPPFormatPalette");
    ULONG_PTR bad[] = { 1, 0x1000, 0x7FFFFFFF0000ull, 0xFFFF800000000000ull, (ULONG_PTR)-1 };
    for (unsigned i = 0; i < 5; i++) { SetLastError(0); ULONG_PTR r = f(bad[i], 10000, 10000, 10000, 0,0,0,0,0,0,0,0,0,0); CHECKF(r == 0 && GetLastError() == 0, "bad pointer %u: returned %llu error %lu", i, (unsigned long long)r, (unsigned long)GetLastError()); }
}
XT(ht_mask_invalid_pointers) {
    F("NtGdiHT_Get8BPPMaskPalette");
    ULONG_PTR bad[] = { 1, 0x1000, 0x7FFFFFFF0000ull, 0xFFFF800000000000ull, (ULONG_PTR)-1 };
    for (unsigned i = 0; i < 5; i++) { SetLastError(0); ULONG_PTR r = f(bad[i], 1, 0, 10000, 10000, 10000, 0,0,0,0,0,0,0,0); CHECKF(r == 0 && GetLastError() == 0, "bad pointer %u: returned %llu error %lu", i, (unsigned long long)r, (unsigned long)GetLastError()); }
}
/* Same arguments, same palette. */
XT(ht_format_is_deterministic) {
    F("NtGdiHT_Get8BPPFormatPalette");
    struct xb_guard a, b; if (!xb_guard_open(&a, 1024) || !xb_guard_open(&b, 1024)) { t_skip("guard"); return; }
    memset(a.p, 0x11, 1024); memset(b.p, 0xEE, 1024);
    ULONG_PTR ra = f((ULONG_PTR)a.p, 10000, 10000, 10000, 0,0,0,0,0,0,0,0,0,0), rb = f((ULONG_PTR)b.p, 10000, 10000, 10000, 0,0,0,0,0,0,0,0,0,0);
    CHECK(ra == rb);
    CHECKF(ra && ra <= 256 && !memcmp(a.p, b.p, ra * 4), "palettes differ for identical arguments (count %llu)", (unsigned long long)ra);
    xb_guard_close(&a); xb_guard_close(&b);
}
XT(ht_mask_is_deterministic) {
    F("NtGdiHT_Get8BPPMaskPalette");
    struct xb_guard a, b; if (!xb_guard_open(&a, 1024) || !xb_guard_open(&b, 1024)) { t_skip("guard"); return; }
    memset(a.p, 0x11, 1024); memset(b.p, 0xEE, 1024);
    ULONG_PTR ra = f((ULONG_PTR)a.p, 1, 0, 10000, 10000, 10000, 0,0,0,0,0,0,0,0), rb = f((ULONG_PTR)b.p, 1, 0, 10000, 10000, 10000, 0,0,0,0,0,0,0,0);
    CHECK(ra == rb);
    CHECKF(ra && ra <= 256 && !memcmp(a.p, b.p, ra * 4), "palettes differ for identical arguments (count %llu)", (unsigned long long)ra);
    xb_guard_close(&a); xb_guard_close(&b);
}
/* The format palette is the fixed 6x6x6 cube: the gamma arguments change neither the count nor the colours. */
XT(ht_format_gamma_does_not_change_palette) {
    F("NtGdiHT_Get8BPPFormatPalette");
    struct xb_guard a, b; if (!xb_guard_open(&a, 1024) || !xb_guard_open(&b, 1024)) { t_skip("guard"); return; }
    ULONG_PTR ra = f((ULONG_PTR)a.p, 10000, 10000, 10000, 0,0,0,0,0,0,0,0,0,0), rb = f((ULONG_PTR)b.p, 30000, 30000, 30000, 0,0,0,0,0,0,0,0,0,0);
    CHECK(ra == rb);
    CHECKF(ra && !memcmp(a.p, b.p, ra * 4), "the palette changed with the gamma arguments");
    xb_guard_close(&a); xb_guard_close(&b);
}
