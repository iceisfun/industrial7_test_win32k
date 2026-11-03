/* GDI handle quota. Each scenario runs in its own child process (see ql_common.c).
 * Boundary rule (per-process GDI handle quota): an allocation is refused when the process
 * GDI handle count is already >= gProcessHandleQuota, so a fill of single-handle objects stops with the
 * count equal to the quota exactly, and a failed allocation leaves the count unchanged. */
#include "ql.h"

struct gk {
    const char *name;
    DWORD objtype;      /* gdi32full!GetObjectType result */
    int exact;          /* one object == one handle, so the count tracks the fills one for one */
    HGDIOBJ (*mk)(int i);
    BOOL (*del)(HGDIOBJ h);
};

static HGDIOBJ mk_brush(int i) { return U(CreateSolidBrush)(RGB(i & 255, (i >> 8) & 255, 0)); }
static HGDIOBJ mk_pen(int i) { return U(CreatePen)(PS_SOLID, 1, RGB(i & 255, 0, 0)); }
static HGDIOBJ mk_rgn(int i) { return U(CreateRectRgn)(0, 0, 1 + i % 50, 1 + i % 7); }
static HGDIOBJ mk_bmp(int i) { return U(CreateBitmap)(1, 1, 1, 1, NULL); }
static HGDIOBJ mk_pal(int i) {
    LOGPALETTE lp; memset(&lp, 0, sizeof lp);
    lp.palVersion = 0x300; lp.palNumEntries = 1; lp.palPalEntry[0].peRed = (BYTE)i;
    return U(CreatePalette)(&lp);
}
static HGDIOBJ mk_dc(int i) { return U(CreateCompatibleDC)(NULL); }
static HGDIOBJ mk_font(int i) {
    LOGFONTW lf; memset(&lf, 0, sizeof lf);
    lf.lfHeight = -(8 + i % 200); lf.lfWeight = 400 + (i % 5) * 100; lstrcpyW(lf.lfFaceName, L"Arial");
    return U(CreateFontIndirectW)(&lf);
}
static BOOL del_obj(HGDIOBJ h) { return U(DeleteObject)(h); }
static BOOL del_dc(HGDIOBJ h) { return U(DeleteDC)((HDC)h); }

static const struct gk gk_brush = { "brush", OBJ_BRUSH, 1, mk_brush, del_obj };
static const struct gk gk_pen   = { "pen",   OBJ_PEN,   1, mk_pen,   del_obj };
static const struct gk gk_rgn   = { "region", OBJ_REGION, 1, mk_rgn, del_obj };
static const struct gk gk_bmp   = { "bitmap", OBJ_BITMAP, 1, mk_bmp, del_obj };
static const struct gk gk_pal   = { "palette", OBJ_PAL, 1, mk_pal, del_obj };
static const struct gk gk_dc    = { "memdc", OBJ_MEMDC, 0, mk_dc, del_dc };
static const struct gk gk_font  = { "font", OBJ_FONT, 0, mk_font, del_obj };

/* Brushes and rectangular regions stay counted for a while after their delete (see QL_CACHE_MAX in ql.h): after a batch of
 * deletes the count is back at its starting value within that allowance; every other kind returns exactly. */
static int gk_cached(const struct gk *k) { return k == &gk_brush || k == &gk_rgn; }
static void expect_back(const struct gk *k, DWORD base, const char *what) {
    DWORD cur = ql_gur(GR_GDI_CUR);
    if (gk_cached(k)) QCF(cur >= base && cur - base <= QL_CACHE_MAX, "%s: count %lu, base %lu (allowance %d)", what, cur, base, QL_CACHE_MAX);
    else QCF(cur == base, "%s: count %lu, base %lu", what, cur, base);
}

/* Returns the quota to test, or 0 after t_skip. */
static unsigned gdi_prep(const struct gk *k, struct ql_quota *qq) {
    if (!ql_connect()) { t_skip("win32k not reachable"); return 0; }
    *qq = ql_gdi_quota();
    if (!qq->std) t_note("GDIProcessHandleQuota configured as %u (registry %s)", qq->q, qq->in_reg ? "set" : "absent");
    if (qq->q < 256) { t_skip("GDI quota %u too small for this scenario", qq->q); return 0; }
    if (qq->q + QL_SLACK > QL_MAX_OBJ) { t_skip("GDI quota %u exceeds the %d object test budget", qq->q, QL_MAX_OBJ); return 0; }
    return qq->q;
}

/* Creates objects until the kernel refuses or the cap is hit. Records the count at the refusal. */
static void gdi_fill(const struct gk *k, unsigned q, DWORD base, int checkpoints) {
    DWORD prev = ql_gur(GR_GDI_CUR);
    int cap = (int)q + QL_SLACK;
    ql_f.n = 0; ql_f.failed = 0; ql_f.err = 0;
    for (int i = 0; i < cap; i++) {
        SetLastError(0);
        HGDIOBJ h = k->mk(i);
        if (!h) {
            ql_f.err = GetLastError();
            ql_f.failed = 1;
            ql_f.gdi_at_fail = ql_gur(GR_GDI_CUR);
            break;
        }
        ql_f.h[ql_f.n++] = h;
        if (checkpoints && (i % 61) == 0) {
            DWORD cur = ql_gur(GR_GDI_CUR);
            QCF(cur >= prev, "count went down while creating: %lu -> %lu", prev, cur);   /* only creations happened */
            if (k->exact) QCF(cur == base + (DWORD)ql_f.n, "count %lu after %d creations (base %lu)", cur, ql_f.n, base);
            prev = cur;
        }
    }
}

static void gdi_free_all(const struct gk *k, int order) {
    int n = ql_f.n;
    if (n == 0) return;
    if (order == 0) { for (int i = 0; i < n; i++) QC(k->del(ql_f.h[i])); }
    else if (order == 1) { for (int i = n - 1; i >= 0; i--) QC(k->del(ql_f.h[i])); }
    else {
        /* stride walk: visits every index exactly once, scattering the frees through the handle table */
        static unsigned char seen[QL_MAX_OBJ];
        memset(seen, 0, sizeof seen);
        int stride = 4093 % n ? 4093 % n : 1, pos = 0, done = 0;
        while (done < n) {
            while (seen[pos]) pos = (pos + 1) % n;
            seen[pos] = 1; QC(k->del(ql_f.h[pos])); done++;
            pos = (pos + stride) % n;
        }
    }
    ql_f.n = 0;
}

static void gdi_boundary(const struct gk *k) {
    struct ql_quota qq;
    unsigned q = gdi_prep(k, &qq);
    if (!q) return;
    DWORD base = ql_gur(GR_GDI_CUR), peak0 = ql_gur(GR_GDI_PEAK);
    gdi_fill(k, q, base, 1);
    if (!ql_f.failed) {
        QCF(t_is_wine(), "%s: no refusal after %d creations, quota %u", k->name, ql_f.n, q);
        gdi_free_all(k, 0);
        return;
    }
    DWORD F = ql_f.gdi_at_fail;
    QCF(F <= q, "count at refusal %lu exceeds quota %u", F, q);
    if (k->exact) {
        QCF(F == q, "count at refusal %lu quota %u (refuse when count >= quota)", F, q);
        QCF((DWORD)ql_f.n == q - base, "created %d, expected %lu", ql_f.n, (DWORD)q - base);
    } else {
        QCF(F + 2 >= q, "count at refusal %lu far below quota %u", F, q);
    }
    if (qq.q == 10000) QCF(F >= 9900 && F <= 10010, "count at refusal %lu outside [9900,10010]", F);
    t_note("%s: refused after %d creations, count %lu quota %u, GetLastError %lu", k->name, ql_f.n, F, q, ql_f.err);
    /* a refused allocation must not leak a handle, and refusing is stable */
    for (int r = 0; r < 3; r++) {
        HGDIOBJ h = k->mk(r);
        QC(h == NULL);
        QCF(ql_gur(GR_GDI_CUR) == F, "count changed by refused creation: %lu -> %lu", F, ql_gur(GR_GDI_CUR));
        if (h) k->del(h);
    }
    DWORD peak = ql_gur(GR_GDI_PEAK);
    QCF(peak >= F, "peak %lu < count %lu", peak, F);
    QCF(peak >= peak0, "peak went down %lu -> %lu", peak0, peak);
    gdi_free_all(k, 0);
    expect_back(k, base, "after freeing everything");
    QCF(ql_gur(GR_GDI_PEAK) == peak, "peak changed by frees: %lu -> %lu", peak, ql_gur(GR_GDI_PEAK));
}

static void gdi_recycle(const struct gk *k) {
    struct ql_quota qq;
    unsigned q = gdi_prep(k, &qq);
    if (!q) return;
    DWORD base = ql_gur(GR_GDI_CUR);
    gdi_fill(k, q, base, 0);
    if (!ql_f.failed) { QCF(t_is_wine(), "%s: no refusal", k->name); gdi_free_all(k, 0); return; }
    DWORD F = ql_f.gdi_at_fail;
    static const int steps[] = { 1, 2, 10, 50 };
    for (unsigned s = 0; s < sizeof steps / sizeof steps[0]; s++) {
        int st = steps[s];
        for (int j = 0; j < st; j++) { QC(k->del(ql_f.h[--ql_f.n])); }
        DWORD cur = ql_gur(GR_GDI_CUR);
        if (k->exact && gk_cached(k)) QCF(cur >= F - st && cur <= F - (st > QL_CACHE_MAX ? st - QL_CACHE_MAX : 0), "after freeing %d: %lu (was %lu)", st, cur, F);
        else if (k->exact) QCF(cur == F - st, "after freeing %d: %lu (was %lu)", st, cur, F);
        else QCF(cur < F, "count did not drop after freeing %d", st);
        /* the freed slots are immediately reusable */
        int again = 0;
        for (int j = 0; j < st + 5; j++) {
            HGDIOBJ h = k->mk(j);
            if (!h) break;
            ql_f.h[ql_f.n++] = h; again++;
        }
        if (k->exact) QCF(again == st, "re-created %d after freeing %d", again, st);
        else QCF(again >= 1 && again <= st + 2, "re-created %d after freeing %d", again, st);
        F = ql_gur(GR_GDI_CUR);
        if (k->exact) QC(F == q);
        { HGDIOBJ x = k->mk(0); QC(x == NULL); if (x) ql_f.h[ql_f.n++] = x; }   /* full again */
    }
    gdi_free_all(k, 1);
    expect_back(k, base, "after cleanup");
}

static void gdi_cleanup(const struct gk *k, int order) {
    struct ql_quota qq;
    unsigned q = gdi_prep(k, &qq);
    if (!q) return;
    DWORD base = ql_gur(GR_GDI_CUR);
    gdi_fill(k, q, base, 0);
    int n1 = ql_f.n;
    gdi_free_all(k, order);
    expect_back(k, base, "after freeing");
    /* identical second fill: the quota accounting is fully undone by the frees */
    gdi_fill(k, q, base, 0);
    QCF(ql_f.n == n1 || t_is_wine(), "second fill created %d, first %d", ql_f.n, n1);
    gdi_free_all(k, order == 0 ? 1 : 0);
    expect_back(k, base, "after second cleanup");
}

static void gdi_handles(const struct gk *k) {
    struct ql_quota qq;
    unsigned q = gdi_prep(k, &qq);
    if (!q) return;
    DWORD base = ql_gur(GR_GDI_CUR);
    gdi_fill(k, q, base, 0);
    static unsigned char used[65536];
    memset(used, 0, sizeof used);
    unsigned type7 = (unsigned)(((ULONG_PTR)ql_f.h[0] >> 16) & 0x7f), pid = GetCurrentProcessId();
    int dup = 0, badtype = 0, badot = 0, badrange = 0, cells = 0, badowner = 0, n = ql_f.n;
    for (int i = 0; i < n; i++) {
        ULONG_PTR h = (ULONG_PTR)ql_f.h[i];
        unsigned idx = (unsigned)(h & 0xffff);
        if (idx == 0) badrange++;                       /* index 0 is never handed out */
        if (used[idx]) dup++;
        used[idx] = 1;
        if (((h >> 16) & 0x7f) != type7) badtype++;     /* ENTRYOBJ::hSetup: bits 16..22 of the handle carry the type */
        if ((h >> 32) != 0 && (h >> 32) != 0xffffffff) badrange++;
        if (U(GetObjectType)((HGDIOBJ)h) != k->objtype) badot++;
        if ((i % 97) == 0) {
            struct k_gdicell c;
            if (k_gdi_cell((void *)h, &c)) {
                cells++;
                if (((unsigned)c.field8 & ~1u) != pid) badowner++;   /* cell+8: pid<<0 with bit0 = lock bit, as every gdi32full getter compares */
            }
        }
    }
    QCF(dup == 0, "%d handles share a table index", dup);
    QCF(badtype == 0, "%d handles with a different type field", badtype);
    QCF(badot == 0, "%d handles with a wrong GetObjectType", badot);
    QCF(badrange == 0, "%d handles out of the expected range", badrange);
    if (cells) QCF(badowner == 0, "%d of %d sampled cells not owned by pid %u", badowner, cells, pid);
    else t_note("GDI shared table not readable here; owner sampling skipped");
    /* with the table full, a handle that already exists still works for queries that do not allocate */
    QC(U(GetObjectType)((HGDIOBJ)ql_f.h[n / 2]) == k->objtype);
    QC(U(GetObjectType)(U(GetStockObject)(WHITE_BRUSH)) == OBJ_BRUSH);
    HGDIOBJ first = ql_f.h[0];
    gdi_free_all(k, 2);
    expect_back(k, base, "after cleanup");
    /* a deleted handle can no longer be deleted (GetObjectType keeps answering from the type bits of the handle value, so
     * it cannot be used to tell; brushes and regions are skipped because their freed cell may sit in the handle cache) */
    if (!gk_cached(k)) QC(!k->del(first));
}

#define GK_FACETS(K) \
    QL_SCENARIO(quota_limits_gdi_##K##_boundary, gdi_boundary(&gk_##K)) \
    QL_SCENARIO(quota_limits_gdi_##K##_recycle, gdi_recycle(&gk_##K)) \
    QL_SCENARIO(quota_limits_gdi_##K##_cleanup_fwd, gdi_cleanup(&gk_##K, 0)) \
    QL_SCENARIO(quota_limits_gdi_##K##_cleanup_rev, gdi_cleanup(&gk_##K, 1)) \
    QL_SCENARIO(quota_limits_gdi_##K##_cleanup_stride, gdi_cleanup(&gk_##K, 2)) \
    QL_SCENARIO(quota_limits_gdi_##K##_handles, gdi_handles(&gk_##K))
GK_FACETS(brush)
GK_FACETS(pen)
GK_FACETS(rgn)
GK_FACETS(bmp)
GK_FACETS(pal)
GK_FACETS(dc)
GK_FACETS(font)

/* ---- mixed kinds share one per-process count ---- */
static const struct gk *const mixed[] = { &gk_brush, &gk_pen, &gk_rgn, &gk_bmp, &gk_pal, &gk_dc, &gk_font };
#define NMIX (sizeof mixed / sizeof mixed[0])
static const struct gk *g_mix_of[QL_MAX_OBJ];

static void gdi_mixed(void) {
    struct ql_quota qq; unsigned q = gdi_prep(&gk_brush, &qq);
    if (!q) return;
    DWORD base = ql_gur(GR_GDI_CUR);
    int n = 0, failed = 0, cap = (int)q + QL_SLACK;
    for (int i = 0; i < cap; i++) {
        const struct gk *k = mixed[i % NMIX];
        HGDIOBJ h = k->mk(i);
        if (!h) { failed = 1; break; }
        ql_f.h[n] = h; g_mix_of[n] = k; n++;
    }
    if (!failed) { QCF(t_is_wine(), "mixed fill never refused"); }
    else {
        DWORD F = ql_gur(GR_GDI_CUR);
        QCF(F <= q && F + 2 >= q, "mixed count at refusal %lu quota %u", F, q);
        /* once full, every kind is refused: the quota is per process, not per type */
        for (unsigned j = 0; j < NMIX; j++) {
            HGDIOBJ h = mixed[j]->mk(j);
            QCF(h == NULL, "kind %s still allocates at the quota", mixed[j]->name);
            if (h) mixed[j]->del(h);
        }
        /* free one object of a different kind, then each kind can take it (one at a time); a brush or region would only
         * move into the handle cache and keep its slot, so the freed object is the last one of a kind outside it */
        for (unsigned j = 0; j < NMIX; j++) {
            int v = n - 1;
            while (v >= 0 && gk_cached(g_mix_of[v])) v--;
            if (v < 0) { QCF(0, "no non-cached object to free"); break; }
            QC(g_mix_of[v]->del(ql_f.h[v]));
            ql_f.h[v] = ql_f.h[n - 1]; g_mix_of[v] = g_mix_of[n - 1]; n--;
            HGDIOBJ h = mixed[j]->mk(j);
            QCF(h != NULL, "kind %s refused although one slot is free", mixed[j]->name);
            if (h) { ql_f.h[n] = h; g_mix_of[n] = mixed[j]; n++; }
        }
    }
    for (int i = 0; i < n; i++) QC(g_mix_of[i]->del(ql_f.h[i]));
    { DWORD cur = ql_gur(GR_GDI_CUR); QCF(cur >= base && cur - base <= 2 * QL_CACHE_MAX, "count %lu after cleanup, base %lu", cur, base); }
}
QL_SCENARIO(quota_limits_gdi_mixed_kinds, gdi_mixed())

/* ---- what keeps working with a full quota ---- */
static void gdi_full_ops(void) {
    struct ql_quota qq; unsigned q = gdi_prep(&gk_brush, &qq);
    if (!q) return;
    HDC dc = U(CreateCompatibleDC)(NULL);
    HBITMAP bmp = U(CreateBitmap)(4, 4, 1, 1, NULL);
    DWORD base = ql_gur(GR_GDI_CUR);
    QC(dc && bmp);
    gdi_fill(&gk_brush, q, base, 0);
    if (!ql_f.failed) { QCF(t_is_wine(), "no refusal"); gdi_free_all(&gk_brush, 0); return; }
    DWORD F = ql_f.gdi_at_fail;
    /* none of these needs a new handle: stock objects are public (allocated with the no-quota flag) and
     * selecting / querying / drawing only touches existing objects */
    HGDIOBJ old = U(SelectObject)(dc, bmp);
    QC(old != NULL);
    QC(U(SelectObject)(dc, old) == (HGDIOBJ)bmp);
    HGDIOBJ sb = U(GetStockObject)(BLACK_BRUSH);
    QC(sb != NULL);
    HGDIOBJ ob = U(SelectObject)(dc, sb);
    QC(ob != NULL);
    U(SelectObject)(dc, ob);
    BITMAP bm; QC(U(GetObjectW)(bmp, sizeof bm, &bm) == sizeof bm);
    QC(bm.bmWidth == 4 && bm.bmHeight == 4);
    QC(U(GetDeviceCaps)(dc, BITSPIXEL) > 0);
    QCF(ql_gur(GR_GDI_CUR) == F, "non-allocating calls changed the count: %lu -> %lu", F, ql_gur(GR_GDI_CUR));
    /* deleting is always possible and frees quota */
    QC(U(DeleteObject)(bmp));
    QCF(ql_gur(GR_GDI_CUR) == F - 1, "count after DeleteObject %lu, want %lu", ql_gur(GR_GDI_CUR), F - 1);
    QC(U(DeleteDC)(dc));
    QCF(ql_gur(GR_GDI_CUR) == F - 2, "count after DeleteDC %lu, want %lu", ql_gur(GR_GDI_CUR), F - 2);
    gdi_free_all(&gk_brush, 0);
}
QL_SCENARIO(quota_limits_gdi_full_nonallocating_ops, gdi_full_ops())

/* A DC creation at the limit must not leak the DC when a dependent allocation fails. */
static void gdi_dc_pairs(void) {
    struct ql_quota qq; unsigned q = gdi_prep(&gk_dc, &qq);
    if (!q) return;
    DWORD base = ql_gur(GR_GDI_CUR);
    gdi_fill(&gk_pen, q, base, 0);      /* pens: a deleted pen frees its slot at once (brushes would go to the handle cache) */
    if (!ql_f.failed) { QCF(t_is_wine(), "no refusal"); gdi_free_all(&gk_pen, 0); return; }
    DWORD F = ql_f.gdi_at_fail;
    /* free 1 slot: a DC (1 handle) fits, CreateCompatibleBitmap with it needs 1 more and is refused */
    QC(U(DeleteObject)(ql_f.h[--ql_f.n]));
    HDC dc = U(CreateCompatibleDC)(NULL);
    QCF(dc != NULL, "DC refused with one free slot");
    QCF(ql_gur(GR_GDI_CUR) == F, "count %lu want %lu", ql_gur(GR_GDI_CUR), F);
    HBITMAP b = U(CreateCompatibleBitmap)(dc, 2, 2);
    QCF(b == NULL, "bitmap created with no free slot");
    if (b) U(DeleteObject)(b);
    QCF(ql_gur(GR_GDI_CUR) == F, "refused bitmap changed the count to %lu", ql_gur(GR_GDI_CUR));
    if (dc) QC(U(DeleteDC)(dc));
    gdi_free_all(&gk_pen, 0);
    QCF(ql_gur(GR_GDI_CUR) == base, "count %lu want base %lu", ql_gur(GR_GDI_CUR), base);
}
QL_SCENARIO(quota_limits_gdi_dc_then_bitmap_at_limit, gdi_dc_pairs())

/* ---- objects whose creation has a second resource (section) are not filled to the quota; a few only ---- */
static void gdi_dib_few(void) {
    if (!ql_connect()) { t_skip("win32k not reachable"); return; }
    DWORD base = ql_gur(GR_GDI_CUR);
    BITMAPINFO bi; memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = 8; bi.bmiHeader.biHeight = 8;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    HBITMAP hb[64]; void *bits[64]; int n = 0;
    for (int i = 0; i < 64; i++) {
        hb[i] = U(CreateDIBSection)(NULL, &bi, DIB_RGB_COLORS, &bits[i], NULL, 0);
        if (!hb[i]) break;
        n++;
        QC(bits[i] != NULL);
        QCF(ql_gur(GR_GDI_CUR) == base + (DWORD)n, "count %lu after %d DIB sections", ql_gur(GR_GDI_CUR), n);
    }
    QC(n == 64);
    for (int i = 0; i < n; i++) QC(U(GetObjectType)(hb[i]) == OBJ_BITMAP);
    for (int i = 0; i < n; i++) QC(U(DeleteObject)(hb[i]));
    QCF(ql_gur(GR_GDI_CUR) == base, "count %lu after DIB cleanup, base %lu", ql_gur(GR_GDI_CUR), base);
}
QL_SCENARIO(quota_limits_gdi_dibsection_counted_once, gdi_dib_few())
