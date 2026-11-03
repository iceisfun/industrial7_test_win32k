/* Font objects as seen from other threads and processes, handle table bookkeeping, height-sign semantics. */
#include "fm.h"

static int g_child_fail;
#define CC(c) do { if (!(c)) { g_child_fail++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

/* ---- height sign: lfHeight > 0 is a cell height, < 0 an em height ---- */

TEST(gdi_font_metrics_text_state_height_sign_relation) {
    FM_BEGIN();
    static const WCHAR *faces[] = { L"Arial", L"Tahoma", L"Times New Roman", L"Courier New", L"Segoe UI" };
    static const int ns[] = { 16, 24, 40, 64 };
    for (unsigned f = 0; f < 5; f++) {
        if (!fm_face_present(faces[f])) continue;
        for (unsigned i = 0; i < 4; i++) {
            HDC dc = fm_dc(); if (!dc) continue;
            HFONT neg = fm_font(faces[f], -ns[i], 400, ANSI_CHARSET, 0, 0), pos = fm_font(faces[f], ns[i], 400, ANSI_CHARSET, 0, 0);
            HGDIOBJ old = fm_sel(dc, neg);
            TEXTMETRICW tn, tp;
            fm.GetTextMetricsW(dc, &tn);
            fm_sel(dc, pos); fm.GetTextMetricsW(dc, &tp);
            /* A cell height of N holds an em smaller than N (the cell includes the internal leading), so the
               cell-height font can never be taller than the em-height font of the same number. */
            CHECKF(tp.tmHeight <= tn.tmHeight, "%u/%d: cell(+N) %ld > em(-N) cell %ld", f, ns[i], tp.tmHeight, tn.tmHeight);
            CHECKF(tp.tmHeight - tp.tmInternalLeading <= ns[i], "%u/%d: em of the +N font %ld", f, ns[i], tp.tmHeight - tp.tmInternalLeading);
            CHECKF(tn.tmHeight >= ns[i], "%u/%d: -N font has cell %ld < N", f, ns[i], tn.tmHeight);
            fm_sel(dc, old); fm.DeleteObject(neg); fm.DeleteObject(pos); fm_freedc(dc);
        }
    }
}

TEST(gdi_font_metrics_text_state_same_request_same_metrics) {
    FM_BEGIN();
    HDC dc = fm_dc(); if (!dc) return;
    HFONT a = fm_font(L"Arial", -23, 400, ANSI_CHARSET, 0, 0), b = fm_font(L"Arial", -23, 400, ANSI_CHARSET, 0, 0);
    TEXTMETRICW ta, tb;
    HGDIOBJ old = fm_sel(dc, a); fm.GetTextMetricsW(dc, &ta);
    fm_sel(dc, b); fm.GetTextMetricsW(dc, &tb);
    CHECK(a != b);
    CHECK(!memcmp(&ta, &tb, sizeof ta));         /* two handles for one request realise to one font */
    fm_sel(dc, old); fm.DeleteObject(a); fm.DeleteObject(b); fm_freedc(dc);
}

TEST(gdi_font_metrics_text_state_zero_logfont_usable) {
    FM_BEGIN();
    HDC dc = fm_dc(); if (!dc) return;
    LOGFONTW lf; memset(&lf, 0, sizeof lf);
    HFONT f = fm.CreateFontIndirectW(&lf);
    CHECK(f != NULL);          /* an all-zero LOGFONT is valid: the mapper picks its default */
    if (f) {
        HGDIOBJ old = fm_sel(dc, f);
        TEXTMETRICW tm; CHECK(fm.GetTextMetricsW(dc, &tm)); CHECK(tm.tmHeight > 0);
        WCHAR face[40]; CHECK(fm_face(dc, face, 40) > 1);
        SIZE sz; CHECK(fm.GetTextExtentPoint32W(dc, L"abc", 3, &sz)); CHECK(sz.cx > 0);
        fm_sel(dc, old); fm.DeleteObject(f);
    }
    fm_freedc(dc);
}

TEST(gdi_font_metrics_text_state_extreme_heights_do_not_crash) {
    FM_BEGIN();
    HDC dc = fm_dc(); if (!dc) return;
    static const int hs[] = { 1, -1, 2, -2, 500, -500, 1000, -1000 };
    HGDIOBJ old = fm.GetCurrentObject(dc, OBJ_FONT);
    for (unsigned i = 0; i < sizeof hs / sizeof *hs; i++) {
        HFONT f = fm_font(L"Arial", hs[i], 400, ANSI_CHARSET, 0, 0);
        CHECKF(f != NULL, "height %d", hs[i]);
        if (!f) continue;
        fm_sel(dc, f);
        TEXTMETRICW tm; BOOL ok = fm.GetTextMetricsW(dc, &tm);
        CHECKF(ok, "height %d", hs[i]);
        if (ok) { CHECKF(tm.tmHeight > 0, "height %d -> tmHeight %ld", hs[i], tm.tmHeight); CHECK(tm.tmHeight == tm.tmAscent + tm.tmDescent); }
        SIZE sz; CHECK(fm.GetTextExtentPoint32W(dc, L"Wx", 2, &sz));
        fm_sel(dc, old); fm.DeleteObject(f);
    }
    fm_freedc(dc);
}

/* ---- handle table bookkeeping ---- */

TEST(gdi_font_metrics_text_state_cell_owner_is_process) {
    FM_BEGIN();
    HFONT f = fm_font(L"Arial", -12, 400, 0, 0, 0);
    if (!f) { t_skip("create"); return; }
    struct fm_cell c;
    CHECK(fm_cell_of(f, &c));
    /* GdiGetEntry callers compare (cell+8 & ~1) with the process id: the owner field of a live font is this process. */
    EXPECT_EQ(c.owner & 0xfffffffe, GetCurrentProcessId() & 0xfffffffe);
    EXPECT_EQ(c.objt, 0x0a);
    EXPECT_EQ(c.upper, (ULONG_PTR)f >> 16 & 0xffff);
    fm.DeleteObject(f);
}

TEST(gdi_font_metrics_text_state_stock_font_cells) {
    FM_BEGIN();
    static const int ids[] = { OEM_FIXED_FONT, ANSI_FIXED_FONT, ANSI_VAR_FONT, SYSTEM_FONT, DEVICE_DEFAULT_FONT, SYSTEM_FIXED_FONT, DEFAULT_GUI_FONT };
    for (unsigned i = 0; i < 7; i++) {
        HGDIOBJ s = fm.GetStockObject(ids[i]);
        struct fm_cell c;
        CHECK(fm_cell_of(s, &c));
        EXPECT_EQ(c.objt, 0x0a);
        EXPECT_EQ(c.upper, (ULONG_PTR)s >> 16 & 0xffff);
        CHECK(FM_INDEX(s) != 0);
    }
}

TEST(gdi_font_metrics_text_state_create_delete_cycle_no_leak) {
    FM_BEGIN();
    DWORD before = fm.GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    for (int round = 0; round < 20; round++) {
        HFONT h[25]; int n = 0;
        for (int i = 0; i < 25; i++) { h[i] = fm_font(L"Arial", -(8 + i), 400 + 300 * (i & 1), ANSI_CHARSET, 0, 0); if (h[i]) n++; }
        CHECK(n == 25);
        for (int i = 0; i < 25; i++) if (h[i]) fm.DeleteObject(h[i]);
    }
    DWORD after = fm.GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    EXPECT_EQ(after, before);
}

TEST(gdi_font_metrics_text_state_indices_recycled_below_table_limit) {
    FM_BEGIN();
    HFONT h[64]; unsigned maxi = 0;
    for (int i = 0; i < 64; i++) { h[i] = fm_font(L"Tahoma", -12, 400, 0, 0, 0); if (h[i] && FM_INDEX(h[i]) > maxi) maxi = FM_INDEX(h[i]); }
    for (int i = 0; i < 64; i++) if (h[i]) fm.DeleteObject(h[i]);
    unsigned maxj = 0;
    for (int i = 0; i < 64; i++) { h[i] = fm_font(L"Tahoma", -12, 400, 0, 0, 0); if (h[i] && FM_INDEX(h[i]) > maxj) maxj = FM_INDEX(h[i]); }
    for (int i = 0; i < 64; i++) if (h[i]) fm.DeleteObject(h[i]);
    t_info("highest font table index first batch %u, second batch %u", maxi, maxj);
    CHECK(maxj <= maxi + 64);      /* freed cells are reused instead of the table growing without bound */
}

TEST(gdi_font_metrics_text_state_font_selected_survives_other_dc_delete) {
    FM_BEGIN();
    HDC a = fm_dc(), b = fm_dc(); HFONT f = fm_font(L"Arial", -20, 400, 0, 0, 0);
    if (!a || !b || !f) { t_skip("setup"); fm_freedc(a); fm_freedc(b); if (f) fm.DeleteObject(f); return; }
    fm_sel(a, f); fm_sel(b, f);
    fm_freedc(a);
    TEXTMETRICW tm; CHECK(fm.GetTextMetricsW(b, &tm));
    CHECK(fm_cell_live_font(f));
    EXPECT_EQ(fm.GetCurrentObject(b, OBJ_FONT), f);
    fm_freedc(b);
    CHECK(fm.DeleteObject(f));
}

TEST(gdi_font_metrics_text_state_select_many_fonts_same_dc) {
    FM_BEGIN();
    HDC dc = fm_dc(); if (!dc) return;
    HGDIOBJ old = fm.GetCurrentObject(dc, OBJ_FONT);
    HFONT h[40]; LONG prev = 0;
    for (int i = 0; i < 40; i++) {
        h[i] = fm_font(L"Arial", -(10 + i * 2), 400, ANSI_CHARSET, 0, 0);
        fm_sel(dc, h[i]);
        TEXTMETRICW tm; fm.GetTextMetricsW(dc, &tm);
        CHECKF(tm.tmHeight >= prev, "font %d", i);
        prev = tm.tmHeight;
        EXPECT_EQ(fm.GetCurrentObject(dc, OBJ_FONT), h[i]);
    }
    fm_sel(dc, old);
    for (int i = 0; i < 40; i++) fm.DeleteObject(h[i]);
    fm_freedc(dc);
}

TEST(gdi_font_metrics_text_state_nt_hfontcreate_type_and_flags_params) {
    FM_BEGIN(); FM_NEED(NtGdiHfontCreate_);
    struct fm_elfdv e; memset(&e, 0, sizeof e);
    e.lf.lfHeight = -14; e.lf.lfWeight = 400; lstrcpyW(e.lf.lfFaceName, L"Arial");
    static const ULONG types[] = { 0, 1, 2, 3 };
    for (unsigned i = 0; i < 4; i++) {
        HFONT f = NtGdiHfontCreate_(&e, 0x164, types[i], 0, NULL);
        t_note("NtGdiHfontCreate lfType=%lu -> %p", types[i], f);
        CHECK(f != NULL);          /* hfontCreate only stores the type word */
        if (f) { CHECK(fm_cell_live_font(f)); fm.DeleteObject(f); }
    }
}

/* ---- other threads ---- */

struct thr { HFONT f; int ok; LONG height; WCHAR face[40]; };
static DWORD WINAPI use_font_thread(void *p) {
    struct thr *t = p;
    HDC dc = fm_dc();
    if (!dc) return 1;
    HGDIOBJ old = fm_sel(dc, t->f);
    TEXTMETRICW tm;
    t->ok = fm.GetTextMetricsW(dc, &tm) && fm.GetCurrentObject(dc, OBJ_FONT) == t->f;
    t->height = tm.tmHeight;
    fm_face(dc, t->face, 40);
    fm_sel(dc, old);
    fm_freedc(dc);
    return 0;
}

TEST(gdi_font_metrics_text_state_font_usable_from_other_thread) {
    FM_BEGIN();
    HFONT f = fm_font(L"Arial", -22, 400, ANSI_CHARSET, 0, 0);
    if (!f) { t_skip("create"); return; }
    HDC dc = fm_dc(); HGDIOBJ old = fm_sel(dc, f);
    TEXTMETRICW tm; fm.GetTextMetricsW(dc, &tm);
    WCHAR face[40]; fm_face(dc, face, 40);
    fm_sel(dc, old); fm_freedc(dc);
    struct thr t; memset(&t, 0, sizeof t); t.f = f;
    HANDLE h = CreateThread(NULL, 0, use_font_thread, &t, 0, NULL);
    if (!h) { fm.DeleteObject(f); t_skip("thread"); return; }
    CHECK(WaitForSingleObject(h, 5000) == WAIT_OBJECT_0);
    CloseHandle(h);
    /* GDI objects belong to the process, not the creating thread. */
    CHECK(t.ok);
    EXPECT_EQ(t.height, tm.tmHeight);
    CHECK(fm_wcsieq(t.face, face));
    fm.DeleteObject(f);
}

static DWORD WINAPI create_font_thread(void *p) {
    struct thr *t = p;
    t->f = fm_font(L"Tahoma", -18, 400, ANSI_CHARSET, 0, 0);
    return 0;
}

TEST(gdi_font_metrics_text_state_font_outlives_creating_thread) {
    FM_BEGIN();
    struct thr t; memset(&t, 0, sizeof t);
    HANDLE h = CreateThread(NULL, 0, create_font_thread, &t, 0, NULL);
    if (!h) { t_skip("thread"); return; }
    WaitForSingleObject(h, 5000); CloseHandle(h);
    if (!t.f) { t_skip("create failed"); return; }
    /* A font handle is not tied to the creator thread's lifetime. */
    CHECK(fm_cell_live_font(t.f));
    LOGFONTW lf; EXPECT_EQ(fm.GetObjectW(t.f, sizeof lf, &lf), sizeof lf);
    EXPECT_EQ(lf.lfHeight, -18);
    CHECK(fm.DeleteObject(t.f));
}

/* ---- other processes ---- */

CHILD(fm_foreign_font) {
    g_child_fail = 0;
    ULONG_PTR h = (ULONG_PTR)_strtoui64(argv[1], NULL, 10);
    const char *m = fm_init();
    if (m) { printf("  SKIP missing %s\n", m); return 0; }
    HGDIOBJ fh = (HGDIOBJ)h;
    LOGFONTW lf;
    /* A font handle of another process is not valid here: its table cell belongs to a different owner (type query, delete and select fail). */
    CC(fm.GetObjectType(fh) != OBJ_FONT);
    /* GetObjectW on the foreign handle is not asserted: the kernel answers it for a font owned by another process. */
    (void)lf;
    SetLastError(0);
    CC(!fm.DeleteObject(fh));
    HDC dc = fm_dc();
    if (dc) { CC(fm.SelectObject(dc, fh) == NULL); fm_freedc(dc); }
    return g_child_fail;
}

TEST(gdi_font_metrics_text_state_foreign_process_cannot_use_font) {
    FM_BEGIN();
    HFONT f = fm_font(L"Arial", -12, 400, 0, 0, 0);
    if (!f) { t_skip("create"); return; }
    char arg[32]; snprintf(arg, sizeof arg, "%llu", (unsigned long long)(ULONG_PTR)f);
    struct k_child_result r = k_child_run("fm_foreign_font", arg, 30000);
    EXPECT_EQ(r.exit_code, 0);
    CHECK(fm_cell_live_font(f));          /* the parent's font is untouched by the child's DeleteObject attempt */
    fm.DeleteObject(f);
}

CHILD(fm_fresh_process_fonts) {
    g_child_fail = 0;
    /* Before user32/gdi32 are touched there is no GDI table; the first gdi32 call connects. Stock fonts then resolve. */
    const char *m = fm_init();
    if (m) { printf("  SKIP missing %s\n", m); return 0; }
    HDC dc = fm_dc();
    CC(dc != NULL);
    if (!dc) return 1;
    HGDIOBJ cur = fm.GetCurrentObject(dc, OBJ_FONT);
    CC(cur != NULL && FM_STOCK(cur));
    TEXTMETRICW tm; CC(fm.GetTextMetricsW(dc, &tm)); CC(tm.tmHeight > 0);
    HFONT f = fm_font(L"Arial", -16, 400, 0, 0, 0);
    CC(f != NULL);
    CC(fm_cell_live_font(f));
    CC(fm.GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) >= 2);   /* at least the DC and the font */
    fm.DeleteObject(f);
    fm_freedc(dc);
    return g_child_fail;
}

TEST(gdi_font_metrics_text_state_fresh_process_first_font) {
    FM_BEGIN();
    struct k_child_result r = k_child_run("fm_fresh_process_fonts", NULL, 30000);
    EXPECT_EQ(r.exit_code, 0);
}
