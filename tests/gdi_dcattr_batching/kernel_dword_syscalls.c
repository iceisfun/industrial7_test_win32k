/* Pinpoint calls to the DC dword syscalls, and the contrast between state that lives in the page
 * (served from the DC_ATTR mapping) and state that only the kernel keeps (arc direction): the second
 * kind is only reachable through a syscall. Signatures come from gdi32full!GetAndSetDCDWord and the
 * callers of NtGdiGetDCDword:
 *   int NtGdiGetAndSetDCDword(HDC, int index, ULONG value, ULONG *previous);
 *   int NtGdiGetDCDword(HDC, int index, ULONG *value);                                          */
#include "dcattr.h"

UDECL(int, NtGdiGetAndSetDCDword, (HDC, int, ULONG, ULONG *));
UDECL(int, NtGdiGetDCDword, (HDC, int, ULONG *));

TEST(gdi_dcattr_batching_getandset_mapmode_writes_the_page) {
    REQUIRE(UN(NtGdiGetAndSetDCDword));
    FXT(f, 8, 8);
    ULONG prev = 0xdeadbeef;
    /* Index 8 is DC::iSetMapMode: returns the previous mode in *previous. The page word GetMapMode reads
     * is the kernel's own attribute storage, so the new mode is visible locally right away. */
    CHECK(UN(NtGdiGetAndSetDCDword)(f.dc, 8, MM_HIMETRIC, &prev));
    EXPECT_EQ(prev, MM_TEXT);
    EXPECT_EQ(U(GetMapMode)(f.dc), MM_HIMETRIC);
    if (f.a) EXPECT_EQ(dca_i32(f.a, DCA_MAPMODE), MM_HIMETRIC);
    prev = 0xdeadbeef;
    CHECK(UN(NtGdiGetAndSetDCDword)(f.dc, 8, MM_HIMETRIC, &prev));      /* unchanged mode: previous == current */
    EXPECT_EQ(prev, MM_HIMETRIC);
    CHECK(UN(NtGdiGetAndSetDCDword)(f.dc, 8, MM_TEXT, &prev));
    EXPECT_EQ(prev, MM_HIMETRIC);
    EXPECT_EQ(U(GetMapMode)(f.dc), MM_TEXT);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_getandset_invalid_dc) {
    REQUIRE(UN(NtGdiGetAndSetDCDword));
    ULONG out = 0x55555555;
    /* A DC that cannot be locked returns 0; only index 5 stores a (0x80000000) failure marker. */
    static const int idx[] = { 1, 2, 4, 6, 7, 8, 9 };
    for (unsigned i = 0; i < sizeof idx / sizeof *idx; i++) {
        out = 0x55555555;
        CHECKF(!UN(NtGdiGetAndSetDCDword)(NULL, idx[i], 1, &out), "index %d on NULL dc succeeded", idx[i]);
        EXPECT_EQ(out, 0x55555555);
    }
    out = 0x55555555;
    CHECK(!UN(NtGdiGetAndSetDCDword)(NULL, 5, 1, &out));
    EXPECT_EQ(out, 0x80000000);
}

TEST(gdi_dcattr_batching_getandset_unknown_index_fails_without_output) {
    REQUIRE(UN(NtGdiGetAndSetDCDword));
    FXT(f, 8, 8);
    static const int idx[] = { 0, 3, 10, 11, 100, -1 };
    for (unsigned i = 0; i < sizeof idx / sizeof *idx; i++) {
        ULONG out = 0x55555555;
        CHECKF(!UN(NtGdiGetAndSetDCDword)(f.dc, idx[i], 1, &out), "index %d accepted", idx[i]);
        EXPECT_EQ(out, 0x55555555);
    }
    fx_close(&f);
}

TEST(gdi_dcattr_batching_relabs_is_kernel_state_outside_the_page) {
    REQUIRE(UN(NtGdiGetAndSetDCDword)); REQUIRE(UN(NtGdiGetDCDword));
    FXT(f, 8, 8);
    ULONG prev = 0xdeadbeef, now = 0xdeadbeef;
    int sevens_before = 0;
    if (f.a) for (unsigned off = 0; off < 0x160; off += 4) if (dca_u32(f.a, off) == 7) sevens_before++;
    /* SetRelAbs -> index 4 stores a kernel-side dword; NtGdiGetDCDword index 1 reads it back. The client page ends at
     * +0x160 (the next DC's page starts there), so the value is not stored in it. */
    CHECK(UN(NtGdiGetAndSetDCDword)(f.dc, 4, 7, &prev));
    CHECK(UN(NtGdiGetDCDword)(f.dc, 1, &now));
    EXPECT_EQ(now, 7);
    if (f.a) {
        int found = 0;
        for (unsigned off = 0; off < 0x160; off += 4) if (dca_u32(f.a, off) == 7) found++;
        EXPECT_EQ(found, sevens_before);
    }
    CHECK(UN(NtGdiGetAndSetDCDword)(f.dc, 4, 1, &now));
    EXPECT_EQ(now, 7);
    CHECK(UN(NtGdiGetAndSetDCDword)(f.dc, 4, prev, &now));
    fx_close(&f);
}

TEST(gdi_dcattr_batching_arc_direction_lives_only_in_the_kernel) {
    REQUIRE(UN(NtGdiGetAndSetDCDword)); REQUIRE(UN(NtGdiGetDCDword));
    FXT(f, 8, 8);
    unsigned char before[DCA_MIN_READ], after[DCA_MIN_READ];
    if (f.a) k_try_read(f.a, before, sizeof before);
    /* Default is AD_COUNTERCLOCKWISE (1); the direction is a flag in the kernel DC (not in the page). */
    EXPECT_EQ(U(GetArcDirection)(f.dc), AD_COUNTERCLOCKWISE);
    EXPECT_EQ(U(SetArcDirection)(f.dc, AD_CLOCKWISE), AD_COUNTERCLOCKWISE);
    EXPECT_EQ(U(GetArcDirection)(f.dc), AD_CLOCKWISE);
    ULONG v = 0;
    CHECK(UN(NtGdiGetDCDword)(f.dc, 4, &v));          /* the index GetArcDirection uses */
    EXPECT_EQ(v, AD_CLOCKWISE);
    EXPECT_EQ(U(SetArcDirection)(f.dc, AD_COUNTERCLOCKWISE), AD_CLOCKWISE);
    EXPECT_EQ(U(GetArcDirection)(f.dc), AD_COUNTERCLOCKWISE);
    if (f.a) {
        k_try_read(f.a, after, sizeof after);
        /* Nothing in the page changed colour/mode fields while the direction flipped twice. */
        CHECK(!memcmp(before + DCA_BKCLR_XL, after + DCA_BKCLR_XL, 0x20));
        CHECK(!memcmp(before + DCA_BKMODE, after + DCA_BKMODE, 0xc));
    }
    fx_close(&f);
}

TEST(gdi_dcattr_batching_arc_direction_stub_rejects_bad_values) {
    REQUIRE(UN(NtGdiGetAndSetDCDword));
    FXT(f, 8, 8);
    ULONG prev = 0x55555555;
    CHECK(UN(NtGdiGetAndSetDCDword)(f.dc, 9, AD_CLOCKWISE, &prev));
    EXPECT_EQ(prev, AD_COUNTERCLOCKWISE);
    static const ULONG bad[] = { 0, 3, 4, 0x80000000, 0xffffffff };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        prev = 0x55555555;
        SetLastError(0);
        /* index 9: anything but 1/2 raises 0x57 via EngSetLastError, reports 0 and leaves the flag alone. */
        CHECK(UN(NtGdiGetAndSetDCDword)(f.dc, 9, bad[i], &prev));
        EXPECT_EQ(prev, 0);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
        EXPECT_EQ(U(GetArcDirection)(f.dc), AD_CLOCKWISE);
    }
    UN(NtGdiGetAndSetDCDword)(f.dc, 9, AD_COUNTERCLOCKWISE, &prev);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_layout_rtl_sets_page_flag_and_mirrors_text_align) {
    FXT(f, 8, 8); NEEDA(f);
    EXPECT_EQ(U(SetLayout)(f.dc, LAYOUT_RTL), 0);
    EXPECT_EQ(U(GetLayout)(f.dc), LAYOUT_RTL);
    CHECK((dca_u8(f.a, DCA_LAYOUTFLAGS) & 1) == 1);
    /* SetTextAlignImpl: with the RTL flag set and (align & 6) != 6 the masked copy has bit 1 flipped. */
    static const struct { UINT ta, masked; } v[] = { {TA_LEFT, 2}, {TA_RIGHT, 0}, {TA_CENTER, 6}, {TA_LEFT | TA_BOTTOM, 0xa} };
    for (unsigned i = 0; i < sizeof v / sizeof *v; i++) {
        U(SetTextAlign)(f.dc, v[i].ta);
        EXPECT_EQ(dca_u32(f.a, DCA_TA), v[i].ta);
        EXPECT_EQ(dca_u32(f.a, DCA_TA_MASKED), v[i].masked);
        EXPECT_EQ(U(GetTextAlign)(f.dc), v[i].ta);
    }
    U(SetTextAlign)(f.dc, TA_LEFT);
    U(SetLayout)(f.dc, 0);
    fx_close(&f);
}

TEST(gdi_dcattr_batching_layout_rtl_negates_viewport_org_in_the_page) {
    FXT(f, 8, 8); NEEDA(f);
    U(SetLayout)(f.dc, LAYOUT_RTL);
    CHECK(U(SetViewportOrgEx)(f.dc, 5, 6, NULL));
    /* SetViewportOrgEx stores -x under RTL; GetViewportOrgEx negates it back. */
    POINT p = dca_pt(f.a, DCA_VIEWORG);
    EXPECT_EQ(p.x, -5); EXPECT_EQ(p.y, 6);
    POINT g = { 0, 0 };
    CHECK(U(GetViewportOrgEx)(f.dc, &g));
    EXPECT_EQ(g.x, 5); EXPECT_EQ(g.y, 6);
    U(SetViewportOrgEx)(f.dc, 0, 0, NULL);
    U(SetLayout)(f.dc, 0);
    fx_close(&f);
}
