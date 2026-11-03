/* State-changing or input-adjacent syscalls from the gap_01 list. Run only with /destructive. Everything is confined to windows,
 * DCs and the public font table lookups of this process; nothing is shown, activated or injected into other processes. */
#include "../gap_01/gap.h"

/* ---- NtGdiChangeGhostFont(UNIVERSAL_FONT_ID *, BOOL add) ------------------------------------------------
 * NtGdiChangeGhostFont probes 8 bytes at the pointer and passes (pointer, flag) on to the font code, which
 * returns 0 when the font table is absent and otherwise looks the font up; an id no
 * installed font carries cannot be found. */
TEST(gap1_dx_ghostfont_unknown_ufi) {
    GAP_BEGIN(); NtGdiChangeGhostFont_pfn f = UN(NtGdiChangeGhostFont); REQUIRE(f);
    gap_ufi ids[] = { { 0xdeadbeef, 0x7fff }, { 0x12345678, 0 }, { 1, 1 } };
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        gap_ufi u = ids[i];
        int ra = f(&u, TRUE), rb = f(&u, FALSE);
        t_note("ChangeGhostFont(%08lx,%lu): add=%d remove=%d", (unsigned long)u.CheckSum, (unsigned long)u.Index, ra, rb);
        CHECK(ra == 0 || ra == 1);
        CHECK(rb == 0 || rb == 1);
        EXPECT_EQ(u.CheckSum, ids[i].CheckSum);                 /* the identifier is read, never modified */
        EXPECT_EQ(u.Index, ids[i].Index);
    }
}

TEST(gap1_dx_ghostfont_installed_font_untouched) {
    GAP_BEGIN(); NtGdiChangeGhostFont_pfn f = UN(NtGdiChangeGhostFont); REQUIRE(f);
    /* Toggling the ghost state of a font nobody installed as removable must leave text metrics alone. */
    HDC dc = U(CreateCompatibleDC)(NULL);
    HFONT font = U(CreateFontW)(-16, 0, 0, 0, 400, 0, 0, 0, 0, 0, 0, 0, 0, L"Arial");
    if (!dc || !font) { t_skip("setup"); if (dc) U(DeleteDC)(dc); return; }
    HGDIOBJ old = U(SelectObject)(dc, font);
    TEXTMETRICW before, after;
    CHECK(U(GetTextMetricsW)(dc, &before));
    gap_ufi u = { 0xcafef00d, 3 };
    f(&u, TRUE); f(&u, FALSE);
    CHECK(U(GetTextMetricsW)(dc, &after));
    CHECK(!memcmp(&before, &after, sizeof before));
    U(SelectObject)(dc, old); U(DeleteObject)(font); U(DeleteDC)(dc);
}

/* ---- NtUserMinMaximize on a hidden window of our own ------------------------------------------------------
 * fKeepHidden (bit 0 of the third argument of the kernel min/max path) keeps the window invisible while the min/max state
 * is applied. The returned value is the window-placement result of the min/max path. */
static int vis(HWND w) { return U(IsWindowVisible)(w) != 0; }

TEST(gap1_dx_minmax_keep_hidden) {
    GAP_BEGIN(); NtUserMinMaximize_pfn mm = UN(NtUserMinMaximize); REQUIRE(mm);
    HWND w = gap_win(0, WS_OVERLAPPEDWINDOW);
    if (!w) { t_skip("no window"); return; }
    CHECK(!vis(w));
    ULONG_PTR r = mm(w, SW_SHOWMINNOACTIVE, TRUE);
    t_note("minimize(keep hidden) -> %p, iconic=%d style=%08lx", (void *)r, U(IsIconic)(w), (unsigned long)U(GetWindowLongW)(w, GWL_STYLE));
    CHECK(!vis(w));
    r = mm(w, SW_SHOWMAXIMIZED, TRUE);
    t_note("maximize(keep hidden) -> %p, zoomed=%d", (void *)r, U(IsZoomed)(w));
    CHECK(!vis(w));
    r = mm(w, SW_RESTORE, TRUE);
    t_note("restore(keep hidden) -> %p iconic=%d zoomed=%d", (void *)r, U(IsIconic)(w), U(IsZoomed)(w));
    CHECK(!vis(w));
    EXPECT_EQ(U(IsIconic)(w), 0);
    EXPECT_EQ(U(IsZoomed)(w), 0);
    U(DestroyWindow)(w);
}

TEST(gap1_dx_minmax_state_matches_showwindow) {
    GAP_BEGIN(); NtUserMinMaximize_pfn mm = UN(NtUserMinMaximize); REQUIRE(mm);
    HWND w = gap_win(0, WS_OVERLAPPEDWINDOW);
    if (!w) { t_skip("no window"); return; }
    mm(w, SW_SHOWMAXIMIZED, TRUE);
    int z1 = U(IsZoomed)(w);
    mm(w, SW_RESTORE, TRUE);
    int z2 = U(IsZoomed)(w);
    t_note("zoomed after maximize %d, after restore %d", z1, z2);
    CHECK(z2 == 0);
    CHECK(!vis(w));
    U(DestroyWindow)(w);
}

/* ---- NtUserInjectGesture with a structure that fails ValidateGestureInfo ---------------------------------
 * A zeroed GESTUREINFO-sized block passes the argument-shape test; the caller/gesture validation rejects it before any
 * message is allocated or posted. */
TEST(gap1_dx_injectgesture_zero_struct) {
    GAP_BEGIN(); NtUserInjectGesture_pfn f = UN(NtUserInjectGesture); REQUIRE(f);
    if (t_is_wine()) { t_skip("gesture injection unsupported"); return; }
    HWND w = gap_win(0, GAP_STYLE_SYS);
    if (!w) { t_skip("no window"); return; }
    BYTE buf[0x40] = {0};
    GAP_POISON();
    int r = f(w, 0, 0, buf, 0);
    t_note("zeroed gesture structure -> %d (err %lu)", r, (unsigned long)GetLastError());
    EXPECT_EQ(r, 0);
    MSG m;
    EXPECT_EQ(U(PeekMessageA)(&m, w, 0x119, 0x119, PM_REMOVE), 0);
    U(DestroyWindow)(w);
}
