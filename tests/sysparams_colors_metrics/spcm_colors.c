/* NtUserSetSysColors (win32kbase, service 5356) validation and the shared-page effects of a no-change call.
 *
 * Kernel flow (win32kbase!NtUserSetSysColors):
 *   - EnterCrit; access check on the window station (WINSTA_WRITEATTRIBUTES) else ERROR_ACCESS_DENIED;
 *   - cElements elements are probed/copied from both arrays only when cElements != 0 (4-byte alignment checked);
 *   - the color setter applies element i only when index < 0x1f, so out-of-range indices are silently skipped and the
 *     call still returns TRUE;
 *   - flags & 1 broadcasts WM_SYSCOLORCHANGE and repaints the desktop; flags == 0 only edits SERVERINFO and the brushes;
 *   - SetSysColor writes the requested color at the SERVERINFO unmatched-color array, the (device-matched) color at the system-color array and updates the
 *     existing solid brush at the system-brush array in place, so the HBRUSH value is stable.
 * user32!SetSysColors forwards to NtUserSetSysColors with flags 7.
 */
#include "spcm.h"

static int sys_nc(void) { return SPCM_NCOLORS; }

typedef struct { COLORREF unm[SPCM_NCOLORS], sys[SPCM_NCOLORS]; HBRUSH br[SPCM_NCOLORS]; } snap_t;
static int snap_take(const char *psi, snap_t *s) {
    for (int i = 0; i < SPCM_NCOLORS; i++) {
        s->unm[i] = (COLORREF)pg_u32(psi, PG_argbUnm + 4 * (size_t)i);
        s->sys[i] = (COLORREF)pg_u32(psi, PG_argbSys + 4 * (size_t)i);
        s->br[i]  = (HBRUSH)pg_ptr(psi, PG_hbrSys + 8 * (size_t)i);
    }
    return 1;
}

/* put the unmatched (requested) colors back after a test re-applied the effective ones; flags 0 so nothing is broadcast */
static void snap_restore(const snap_t *a) {
    for (int i = 0; i < SPCM_NCOLORS; i++) {
        INT idx = i; COLORREF c = a->unm[i];
        UN(NtUserSetSysColors)(1, &idx, &c, 0);
    }
}

TEST(syscolors_zero_elements_returns_true) {
    NEED_SYSCOL();
    NtUserSetSysColors_pfn fn = UN(NtUserSetSysColors);
    SetLastError(SPCM_ERR_SENTINEL);
    BOOL r = fn(0, NULL, NULL, 0);
    /* cElements == 0: no probe, the color setter loops zero times and returns 1 */
    CHECK(r == 1);
    EXPECT_EQ(GetLastError(), SPCM_ERR_SENTINEL);
}
TEST(syscolors_zero_elements_with_valid_pointers) {
    NEED_SYSCOL(); INT idx = COLOR_BTNFACE; COLORREF c = 0;
    CHECK(UN(NtUserSetSysColors)(0, &idx, &c, 0) == 1);
}
TEST(syscolors_zero_elements_leaves_page_unchanged) {
    NEED_SYSCOL(); char *psi = spcm_page(); if (!psi) return;
    snap_t a, b; snap_take(psi, &a);
    CHECK(UN(NtUserSetSysColors)(0, NULL, NULL, 0) == 1);
    snap_take(psi, &b);
    CHECK(!memcmp(&a, &b, sizeof a));
}
TEST(syscolors_out_of_range_index_skipped_one) {
    NEED_SYSCOL(); char *psi = spcm_page(); if (!psi) return;
    snap_t a, b; snap_take(psi, &a);
    INT idx = 0x1f; COLORREF c = 0x00A1B2C3;
    CHECK(UN(NtUserSetSysColors)(1, &idx, &c, 0) == 1);   /* index must be < 0x1f */
    snap_take(psi, &b);
    CHECK(!memcmp(&a, &b, sizeof a));
}
TEST(syscolors_out_of_range_index_skipped_table) {
    NEED_SYSCOL(); char *psi = spcm_page(); if (!psi) return;
    static const INT bad[] = { 0x1f, 0x20, 0x21, 0x40, 0x100, 0x7fffffff, -1, (INT)0x80000000 };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        snap_t a, b; snap_take(psi, &a);
        COLORREF c = 0x00112233 + i;
        NtUserSetSysColors_pfn fn = UN(NtUserSetSysColors);
        SetLastError(SPCM_ERR_SENTINEL);
        BOOL r = fn(1, &bad[i], &c, 0);
        CHECKF(r == 1, "index %d", bad[i]);
        snap_take(psi, &b);
        CHECKF(!memcmp(&a, &b, sizeof a), "index %d modified the page", bad[i]);
    }
}
TEST(syscolors_mixed_valid_invalid_batch_skips_only_invalid) {
    NEED_SYSCOL(); char *psi = spcm_page(); if (!psi) return;
    snap_t a, b; snap_take(psi, &a);
    /* COLOR_BTNFACE rewritten with its current value; the invalid neighbours are dropped one by one */
    INT idx[3] = { 0x40, COLOR_BTNFACE, 0x1f }; COLORREF c[3] = { 1, a.sys[COLOR_BTNFACE], 2 };
    CHECK(UN(NtUserSetSysColors)(3, idx, c, 0) == 1);
    snap_take(psi, &b);
    /* only the effective color is re-applied (verbatim into the unmatched slot), every other slot is untouched */
    for (int i = 0; i < SPCM_NCOLORS; i++) {
        CHECKF(a.br[i] == b.br[i], "brush %d moved", i);
        if (i != COLOR_BTNFACE) CHECKF(a.unm[i] == b.unm[i] && a.sys[i] == b.sys[i], "color %d changed", i);
    }
    EXPECT_EQ(b.unm[COLOR_BTNFACE], a.sys[COLOR_BTNFACE]);
    snap_restore(&a);
    CHECK((b.sys[COLOR_BTNFACE] & 0xffffff) == (a.sys[COLOR_BTNFACE] & 0xffffff) || !spcm_truecolor());
}
TEST(syscolors_same_value_keeps_brush_handle) {
    NEED_SYSCOL(); char *psi = spcm_page(); if (!psi) return;
    snap_t a, b; snap_take(psi, &a);
    for (int i = 0; i < sys_nc(); i++) {
        if (!a.br[i]) continue;
        INT idx = i; COLORREF c = a.sys[i];     /* re-apply the effective color: cannot change what is displayed */
        CHECKF(UN(NtUserSetSysColors)(1, &idx, &c, 0) == 1, "color %d", i);
    }
    snap_take(psi, &b);
    snap_restore(&a);
    for (int i = 0; i < sys_nc(); i++) {
        if (!a.br[i]) continue;
        /* SetSysColor finds hbrSystem[i] != NULL and updates the existing brush handle */
        CHECKF(a.br[i] == b.br[i], "color %d brush %p -> %p", i, a.br[i], b.br[i]);
        CHECKF(b.unm[i] == a.sys[i], "color %d unmatched slot 0x%lx, requested 0x%lx", i, (unsigned long)b.unm[i], (unsigned long)a.sys[i]);
    }
}
TEST(syscolors_same_value_system_color_stable) {
    NEED_SYSCOL(); char *psi = spcm_page(); if (!psi) return;
    snap_t a, b; snap_take(psi, &a);
    for (int i = 0; i < sys_nc(); i++) {
        INT idx = i; COLORREF c = a.sys[i];
        UN(NtUserSetSysColors)(1, &idx, &c, 0);
    }
    snap_take(psi, &b);
    snap_restore(&a);
    for (int i = 0; i < sys_nc(); i++) {
        if (spcm_truecolor()) CHECKF((a.sys[i] & 0xffffff) == (b.sys[i] & 0xffffff), "color %d system 0x%lx -> 0x%lx", i, (unsigned long)a.sys[i], (unsigned long)b.sys[i]);
        else if (a.sys[i] != b.sys[i]) t_note("palette display: color %d system 0x%lx -> 0x%lx", i, (unsigned long)a.sys[i], (unsigned long)b.sys[i]);
    }
}
TEST(syscolors_same_value_brush_color_unchanged) {
    NEED_SYSCOL(); char *psi = spcm_page(); if (!psi) return;
    REQUIRE(U(GetObjectW)); REQUIRE(U(GetSysColorBrush));
    snap_t a; snap_take(psi, &a);
    static const int idx[] = { COLOR_BTNFACE, COLOR_WINDOW, COLOR_WINDOWTEXT, COLOR_HIGHLIGHT, COLOR_INFOBK, COLOR_APPWORKSPACE, COLOR_DESKTOP };
    for (unsigned k = 0; k < sizeof idx / sizeof idx[0]; k++) {
        int i = idx[k]; INT ii = i; COLORREF c = a.sys[i];
        LOGBRUSH l0, l1; memset(&l0, 0, sizeof l0); memset(&l1, 0, sizeof l1);
        HBRUSH hb = U(GetSysColorBrush)(i);
        if (!hb) { t_note("no brush for color %d", i); continue; }
        CHECK(U(GetObjectW)(hb, sizeof l0, &l0) == sizeof l0);
        CHECK(UN(NtUserSetSysColors)(1, &ii, &c, 0) == 1);
        EXPECT_EQ(U(GetSysColorBrush)(i), hb);
        CHECK(U(GetObjectW)(hb, sizeof l1, &l1) == sizeof l1);
        CHECKF(l0.lbColor == l1.lbColor && l0.lbStyle == l1.lbStyle, "color %d brush changed 0x%lx -> 0x%lx", i, (unsigned long)l0.lbColor, (unsigned long)l1.lbColor);
    }
}
TEST(syscolors_flags_zero_sends_no_syscolorchange) {
    NEED_SYSCOL(); char *psi = spcm_page(); if (!psi) return;
    if (!spcm_obs_create()) { t_skip("cannot create observer window"); return; }
    spcm_obs_reset(); k_pump(30); spcm_obs_reset();
    snap_t a; snap_take(psi, &a);
    INT idx = COLOR_BTNFACE; COLORREF c = a.sys[COLOR_BTNFACE];
    CHECK(UN(NtUserSetSysColors)(1, &idx, &c, 0) == 1);
    k_pump(150);
    /* the color setter only broadcasts when (flags & 1) */
    EXPECT_EQ(g_obs.syscolor, 0);
    spcm_obs_destroy();
}
TEST(syscolors_get_syscolor_invalid_index) {
    REQUIRE(U(GetSysColor)); __typeof__(&GetSysColor) gsc = U(GetSysColor);
    static const int bad[] = { 0x1f, 0x20, 0x100, -1, 0x7fffffff };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(SPCM_ERR_SENTINEL);
        DWORD c = gsc(bad[i]);
        EXPECT_ERR(87); CHECKF(c == 0, "index %d", bad[i]);
    }
}
TEST(syscolors_get_syscolor_matches_page_for_all) {
    REQUIRE(U(GetSysColor)); char *psi = spcm_page(); if (!psi) return;
    for (int i = 0; i < SPCM_NCOLORS; i++) EXPECT_EQ(U(GetSysColor)(i), pg_u32(psi, PG_argbSys + 4 * (size_t)i));
}
TEST(syscolors_hbrush_slots_are_gdi_brushes) {
    char *psi = spcm_page(); if (!psi) return;
    snap_t a; snap_take(psi, &a);
    for (int i = 0; i < SPCM_NCOLORS; i++) {
        if (!a.br[i]) continue;
        EXPECT_EQ(U(GetObjectType)(a.br[i]), OBJ_BRUSH);
        struct k_gdicell cell;
        if (k_gdi_cell(a.br[i], &cell)) CHECKF(cell.type_check == (unsigned char)((ULONG_PTR)a.br[i] >> 24), "color %d cell type", i);
    }
}
TEST(syscolors_brush_handles_are_distinct_objects) {
    char *psi = spcm_page(); if (!psi) return;
    snap_t a; snap_take(psi, &a);
    /* SetSysColor creates one brush per index, so no two non-null slots share a handle */
    for (int i = 0; i < SPCM_NCOLORS; i++) for (int j = i + 1; j < SPCM_NCOLORS; j++)
        if (a.br[i] && a.br[j]) CHECKF(a.br[i] != a.br[j], "colors %d and %d share brush %p", i, j, a.br[i]);
}

/* user32!GetSysColorBrush is "if ((uint)nIndex < 0x1f) return the system brush for nIndex; return NULL" with no last error. */
TEST(syscolors_getsyscolorbrush_invalid_index_null) {
    REQUIRE(U(GetSysColorBrush)); __typeof__(&GetSysColorBrush) f = U(GetSysColorBrush);
    static const int bad[] = { 0x1f, 0x20, 0x100, -1, 0x7fffffff, (int)0x80000000 };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(SPCM_ERR_SENTINEL);
        HBRUSH h = f(bad[i]);
        CHECKF(h == NULL, "index %d -> %p", bad[i], h);
        EXPECT_EQ(GetLastError(), SPCM_ERR_SENTINEL);
    }
}
TEST(syscolors_getsyscolorbrush_equals_page_slot) {
    REQUIRE(U(GetSysColorBrush)); char *psi = spcm_page(); if (!psi) return;
    for (int i = 0; i < SPCM_NCOLORS; i++)
        CHECKF(U(GetSysColorBrush)(i) == (HBRUSH)pg_ptr(psi, PG_hbrSys + 8 * (size_t)i), "color %d", i);
}
TEST(syscolors_brush_handle_type_bits_are_brush) {
    char *psi = spcm_page(); if (!psi) return;
    snap_t a; snap_take(psi, &a);
    for (int i = 0; i < SPCM_NCOLORS; i++) {
        if (!a.br[i]) continue;
        /* brush handles are GDI_OBJ_BRUSH (0x10) typed: the type byte sits in bits 16..23 of the handle */
        CHECKF((((ULONG_PTR)a.br[i] >> 16) & 0x7f) == 0x10, "color %d handle %p type bits", i, a.br[i]);
    }
}
