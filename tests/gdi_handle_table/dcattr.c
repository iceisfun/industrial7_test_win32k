/* DC_ATTR: the per-DC user-mode attribute block whose address is cell+0x10 of a DC's handle-table entry.
 * Offsets are the ones the gdi32full getters read (GetBkMode +0xe0, GetROP2 +0xd4 (byte), GetPolyFillMode +0xe4,
 * GetStretchBltMode +0xe8, GetMapMode +0x68, GetLayout +0x6c, GetTextColor +0xbc, GetBkColor +0xb4,
 * GetDCBrushColor +0xc4, GetDCPenColor +0xcc, GetGraphicsMode +0xd0, GetTextAlign +0x114,
 * GetTextCharacterExtra +0x118, GetBrushOrgEx +0x158, GetCurrentObject hbrush +0xa0 / hpen +0xa8 / hcs +0x10).
 * The block is a secured user-mode page holding 11 slots of 0x160 bytes. */
#include "gh.h"

static int dca_base(HDC dc, unsigned long long *p) {
    struct gh_cell c;
    if (!gh_cell_of(dc, &c) || !c.puser) return 0;
    *p = c.puser;
    return 1;
}
static int dca_read(HDC dc, unsigned off, void *out, size_t n) {
    unsigned long long p;
    return dca_base(dc, &p) && k_try_read((void *)(ULONG_PTR)(p + off), out, n);
}
static unsigned dca_u32(HDC dc, unsigned off, int *ok) {
    unsigned v = 0xdeadbeef;
    int r = dca_read(dc, off, &v, 4);
    if (ok) *ok = r;
    return v;
}

TEST(gdi_handle_table_dcattr_slot_shape) {
    GH_NEED();
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    unsigned long long p;
    CHECK(dca_base(dc, &p));
    unsigned off = (unsigned)(p & 0xfff);
    CHECKF(off <= 0xdc0 && off % 0x160 == 0, "DC_ATTR 0x%llx page offset 0x%x", p, off);
    CHECK(p < GH_KERNEL_FLOOR);
    U(DeleteDC)(dc);
}
TEST(gdi_handle_table_dcattr_page_is_private_rw) {
    GH_NEED();
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    unsigned long long p;
    MEMORY_BASIC_INFORMATION mbi;
    REQUIRE(dca_base(dc, &p));
    CHECK(VirtualQuery((void *)(ULONG_PTR)p, &mbi, sizeof mbi) == sizeof mbi);
    /* secured user memory: ZwAllocateVirtualMemory(MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE) + MmSecureVirtualMemory */
    CHECK(mbi.State == MEM_COMMIT);
    CHECK(mbi.Type == MEM_PRIVATE);
    CHECKF(mbi.Protect == PAGE_READWRITE, "protect 0x%lx", mbi.Protect);
    CHECK(((ULONG_PTR)mbi.AllocationBase & 0xfff) == 0);
    U(DeleteDC)(dc);
}
TEST(gdi_handle_table_dcattr_distinct_per_dc) {
    GH_NEED();
    enum { N = 24 };
    HDC dc[N]; unsigned long long p[N];
    for (int i = 0; i < N; i++) { dc[i] = U(CreateCompatibleDC)(NULL); REQUIRE(dc[i]); CHECK(dca_base(dc[i], &p[i])); }
    for (int i = 0; i < N; i++)
        for (int j = i + 1; j < N; j++) {
            unsigned long long d = p[i] > p[j] ? p[i] - p[j] : p[j] - p[i];
            CHECKF(d >= 0x160, "DCs %d/%d DC_ATTR 0x%llx 0x%llx overlap", i, j, p[i], p[j]);
        }
    for (int i = 0; i < N; i++) U(DeleteDC)(dc[i]);
}
TEST(gdi_handle_table_dcattr_slots_share_pages) {
    GH_NEED();
    enum { N = 22 };
    HDC dc[N]; unsigned long long p[N];
    int pages = 0;
    for (int i = 0; i < N; i++) { dc[i] = U(CreateCompatibleDC)(NULL); REQUIRE(dc[i]); dca_base(dc[i], &p[i]); }
    for (int i = 0; i < N; i++) {
        int seen = 0;
        for (int j = 0; j < i; j++) if ((p[j] >> 12) == (p[i] >> 12)) seen = 1;
        if (!seen) pages++;
    }
    /* 11 slots per page, so N DCs span at least ceil(N/11) pages (other DCs of the process may have consumed slots) */
    CHECKF(pages >= (N + 10) / 11, "%d DC_ATTR pages for %d DCs", pages, N);
    t_note("%d DCs use %d DC_ATTR pages", N, pages);
    for (int i = 0; i < N; i++) U(DeleteDC)(dc[i]);
}
TEST(gdi_handle_table_dcattr_display_dc_has_block) {
    GH_NEED();
    HDC dc = gh_disp();
    REQUIRE(dc);
    unsigned long long p;
    CHECK(dca_base(dc, &p));
    CHECK((p & 0xfff) % 0x160 == 0);
    U(DeleteDC)(dc);
}
TEST(gdi_handle_table_dcattr_getdc_has_block) {
    GH_NEED();
    REQUIRE(U(GetDC));
    HDC dc = U(GetDC)(NULL);
    REQUIRE(dc);
    unsigned long long p;
    CHECK(dca_base(dc, &p));
    CHECK((p & 0xfff) % 0x160 == 0);
    U(ReleaseDC)(NULL, dc);
}

/* ---- defaults of a fresh memory DC (documented API defaults, read from DC_ATTR) ---- */
#define DEFCHK(nm, off, getexpr, want) \
    TEST(gdi_handle_table_dcattr_default_##nm) { \
        GH_NEED(); HDC dc = U(CreateCompatibleDC)(NULL); REQUIRE(dc); int ok; \
        unsigned m = dca_u32(dc, off, &ok); CHECK(ok); \
        CHECKF((getexpr) == (want), "%s getter %lld want %lld", #nm, (long long)(getexpr), (long long)(want)); \
        if (off == 0xe8) t_note("%s DC_ATTR+0x%x = 0x%x", #nm, off, m); else \
    CHECKF(m == (unsigned)(want), "%s DC_ATTR+0x%x = 0x%x want 0x%x", #nm, off, m, (unsigned)(want)); \
        U(DeleteDC)(dc); }
DEFCHK(bkmode, 0xe0, U(GetBkMode)(dc), OPAQUE)
DEFCHK(polyfill, 0xe4, U(GetPolyFillMode)(dc), ALTERNATE)
DEFCHK(stretch, 0xe8, U(GetStretchBltMode)(dc), BLACKONWHITE)
DEFCHK(mapmode, 0x68, U(GetMapMode)(dc), MM_TEXT)
DEFCHK(graphicsmode, 0xd0, U(GetGraphicsMode)(dc), GM_COMPATIBLE)
DEFCHK(textalign, 0x114, U(GetTextAlign)(dc), (TA_LEFT | TA_TOP | TA_NOUPDATECP))
DEFCHK(textcolor, 0xbc, U(GetTextColor)(dc), RGB(0, 0, 0))
DEFCHK(bkcolor, 0xb4, U(GetBkColor)(dc), RGB(255, 255, 255))
DEFCHK(textextra, 0x118, U(GetTextCharacterExtra)(dc), 0)
DEFCHK(layout, 0x6c, U(GetLayout)(dc), 0)
DEFCHK(dcbrushcolor, 0xc4, U(GetDCBrushColor)(dc), RGB(255, 255, 255))
DEFCHK(dcpencolor, 0xcc, U(GetDCPenColor)(dc), RGB(0, 0, 0))

TEST(gdi_handle_table_dcattr_default_rop2) {
    GH_NEED();
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    unsigned char b = 0xee;
    CHECK(dca_read(dc, 0xd4, &b, 1));
    CHECK(b == R2_COPYPEN);
    CHECK(U(GetROP2)(dc) == R2_COPYPEN);
    U(DeleteDC)(dc);
}
TEST(gdi_handle_table_dcattr_default_brush_origin) {
    GH_NEED();
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    POINT p = { 77, 77 }, q = { 88, 88 };
    CHECK(dca_read(dc, 0x158, &p, 8));
    CHECK(U(GetBrushOrgEx)(dc, &q));
    CHECK(p.x == 0 && p.y == 0);
    CHECK(q.x == 0 && q.y == 0);
    U(DeleteDC)(dc);
}

/* ---- API setter -> DC_ATTR memory -> API getter ---- */
#define DCA_PAIR(nm, off, sz, SETF, GETF, v1, v2) \
    TEST(gdi_handle_table_dcattr_set_##nm) { \
        GH_NEED(); HDC dc = U(CreateCompatibleDC)(NULL); REQUIRE(dc); unsigned long long m = 0; \
        SETF(dc, v1); \
        CHECK(dca_read(dc, off, &m, sz)); \
        CHECKF((long long)GETF(dc) == (long long)(v1), "getter after set %s", #nm); \
        CHECKF((m & (sz == 1 ? 0xff : 0xffffffffull)) == ((unsigned)(v1) & (sz == 1 ? 0xff : 0xffffffffu)), "%s DC_ATTR+0x%x = 0x%llx want 0x%x", #nm, off, m, (unsigned)(v1)); \
        SETF(dc, v2); \
        CHECK(dca_read(dc, off, &m, sz)); \
        CHECKF((long long)GETF(dc) == (long long)(v2), "getter after second set %s", #nm); \
        CHECK((m & (sz == 1 ? 0xff : 0xffffffffull)) == ((unsigned)(v2) & (sz == 1 ? 0xff : 0xffffffffu))); \
        U(DeleteDC)(dc); }
#define S_BKMODE(dc, v) U(SetBkMode)(dc, v)
#define S_ROP2(dc, v) U(SetROP2)(dc, v)
#define S_POLY(dc, v) U(SetPolyFillMode)(dc, v)
#define S_STRETCH(dc, v) U(SetStretchBltMode)(dc, v)
#define S_TEXTCOLOR(dc, v) U(SetTextColor)(dc, v)
#define S_BKCOLOR(dc, v) U(SetBkColor)(dc, v)
#define S_EXTRA(dc, v) U(SetTextCharacterExtra)(dc, v)
#define S_DCBRUSH(dc, v) U(SetDCBrushColor)(dc, v)
#define S_DCPEN(dc, v) U(SetDCPenColor)(dc, v)
#define S_GMODE(dc, v) U(SetGraphicsMode)(dc, v)
#define S_MAPMODE(dc, v) U(SetMapMode)(dc, v)
#define S_TALIGN(dc, v) U(SetTextAlign)(dc, v)
#define S_LAYOUT(dc, v) U(SetLayout)(dc, v)
#define G_BKMODE(dc) U(GetBkMode)(dc)
#define G_ROP2(dc) U(GetROP2)(dc)
#define G_POLY(dc) U(GetPolyFillMode)(dc)
#define G_STRETCH(dc) U(GetStretchBltMode)(dc)
#define G_TEXTCOLOR(dc) U(GetTextColor)(dc)
#define G_BKCOLOR(dc) U(GetBkColor)(dc)
#define G_EXTRA(dc) U(GetTextCharacterExtra)(dc)
#define G_DCBRUSH(dc) U(GetDCBrushColor)(dc)
#define G_DCPEN(dc) U(GetDCPenColor)(dc)
#define G_GMODE(dc) U(GetGraphicsMode)(dc)
#define G_MAPMODE(dc) U(GetMapMode)(dc)
#define G_TALIGN(dc) U(GetTextAlign)(dc)
#define G_LAYOUT(dc) U(GetLayout)(dc)
DCA_PAIR(bkmode, 0xe0, 4, S_BKMODE, G_BKMODE, TRANSPARENT, OPAQUE)
DCA_PAIR(rop2, 0xd4, 1, S_ROP2, G_ROP2, R2_XORPEN, R2_MASKPEN)
DCA_PAIR(polyfill, 0xe4, 4, S_POLY, G_POLY, WINDING, ALTERNATE)
TEST(gdi_handle_table_dcattr_set_stretch) {
    GH_NEED();
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    /* gdi32full may store the mode in a transformed form, so the raw field is only noted; the getter must round-trip */
    unsigned long long m = 0;
    U(SetStretchBltMode)(dc, COLORONCOLOR);
    CHECK(U(GetStretchBltMode)(dc) == COLORONCOLOR);
    CHECK(dca_read(dc, 0xe8, &m, 4));
    t_note("DC_ATTR+0xe8 after COLORONCOLOR = 0x%llx", m & 0xffffffff);
    U(SetStretchBltMode)(dc, HALFTONE);
    CHECK(U(GetStretchBltMode)(dc) == HALFTONE);
    U(DeleteDC)(dc);
}
DCA_PAIR(textcolor, 0xbc, 4, S_TEXTCOLOR, G_TEXTCOLOR, RGB(1, 2, 3), RGB(250, 251, 252))
DCA_PAIR(bkcolor, 0xb4, 4, S_BKCOLOR, G_BKCOLOR, RGB(9, 8, 7), RGB(100, 101, 102))
DCA_PAIR(textextra, 0x118, 4, S_EXTRA, G_EXTRA, 5, -3)
DCA_PAIR(dcbrushcolor, 0xc4, 4, S_DCBRUSH, G_DCBRUSH, RGB(10, 20, 30), RGB(200, 100, 50))
DCA_PAIR(dcpencolor, 0xcc, 4, S_DCPEN, G_DCPEN, RGB(11, 22, 33), RGB(210, 110, 55))
DCA_PAIR(graphicsmode, 0xd0, 4, S_GMODE, G_GMODE, GM_ADVANCED, GM_COMPATIBLE)
DCA_PAIR(mapmode, 0x68, 4, S_MAPMODE, G_MAPMODE, MM_LOMETRIC, MM_TEXT)
DCA_PAIR(textalign, 0x114, 4, S_TALIGN, G_TALIGN, (TA_CENTER | TA_BASELINE), (TA_LEFT | TA_TOP))
DCA_PAIR(layout, 0x6c, 4, S_LAYOUT, G_LAYOUT, LAYOUT_RTL, 0)

TEST(gdi_handle_table_dcattr_set_brush_origin) {
    GH_NEED();
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    POINT old, p = { 0, 0 }, q;
    CHECK(U(SetBrushOrgEx)(dc, 5, 6, &old));
    CHECK(dca_read(dc, 0x158, &p, 8));
    CHECK(U(GetBrushOrgEx)(dc, &q));
    CHECK(p.x == 5 && p.y == 6);
    CHECK(q.x == 5 && q.y == 6);
    U(DeleteDC)(dc);
}
TEST(gdi_handle_table_dcattr_select_writes_hbrush_hpen) {
    GH_NEED();
    HDC dc = U(CreateCompatibleDC)(NULL);
    HBRUSH b = U(CreateSolidBrush)(RGB(1, 2, 3));
    HPEN p = U(CreatePen)(PS_SOLID, 3, RGB(4, 5, 6));
    REQUIRE(dc && b && p);
    unsigned long long hb = 0, hp = 0;
    HGDIOBJ ob = U(SelectObject)(dc, b), op = U(SelectObject)(dc, p);
    CHECK(dca_read(dc, 0xa0, &hb, 8));
    CHECK(dca_read(dc, 0xa8, &hp, 8));
    /* GetCurrentObject returns exactly these two fields for OBJ_BRUSH / OBJ_PEN without entering the kernel */
    CHECK((HGDIOBJ)(ULONG_PTR)hb == b);
    CHECK((HGDIOBJ)(ULONG_PTR)hp == p);
    CHECK(U(GetCurrentObject)(dc, OBJ_BRUSH) == b);
    CHECK(U(GetCurrentObject)(dc, OBJ_PEN) == p);
    U(SelectObject)(dc, ob); U(SelectObject)(dc, op);
    CHECK(dca_read(dc, 0xa0, &hb, 8) && (HGDIOBJ)(ULONG_PTR)hb == ob);
    CHECK(dca_read(dc, 0xa8, &hp, 8) && (HGDIOBJ)(ULONG_PTR)hp == op);
    U(DeleteDC)(dc); U(DeleteObject)(b); U(DeleteObject)(p);
}
TEST(gdi_handle_table_dcattr_extpen_in_hpen_field) {
    GH_NEED();
    HDC dc = U(CreateCompatibleDC)(NULL);
    HGDIOBJ ep = mk_extpen_geom();
    REQUIRE(dc && ep);
    unsigned long long hp = 0;
    HGDIOBJ op = U(SelectObject)(dc, ep);
    CHECK(dca_read(dc, 0xa8, &hp, 8));
    CHECK((HGDIOBJ)(ULONG_PTR)hp == ep);
    /* OBJ_EXTPEN is answered from the same +0xa8 field as OBJ_PEN (GetCurrentObject maps both to 0x300000) */
    CHECK(U(GetCurrentObject)(dc, OBJ_EXTPEN) == ep);
    CHECK(U(GetCurrentObject)(dc, OBJ_PEN) == ep);
    U(SelectObject)(dc, op);
    U(DeleteDC)(dc); U(DeleteObject)(ep);
}
TEST(gdi_handle_table_dcattr_colorspace_field) {
    GH_NEED();
    HDC dc = U(CreateCompatibleDC)(NULL);
    HGDIOBJ cs = mk_colorspace();
    REQUIRE(dc && cs);
    HCOLORSPACE old = U(SetColorSpace)(dc, (HCOLORSPACE)cs);
    t_note("SetColorSpace returned %p (default colour space of the DC)", old);
    unsigned long long f = 0;
    CHECK(dca_read(dc, 0x10, &f, 8));
    if (old) {
        CHECK((HGDIOBJ)(ULONG_PTR)f == cs);
        CHECK(U(GetCurrentObject)(dc, OBJ_COLORSPACE) == cs);
        U(SetColorSpace)(dc, old);
    }
    U(DeleteDC)(dc);
    rm_colorspace(cs);
}
TEST(gdi_handle_table_dcattr_independent_between_dcs) {
    GH_NEED();
    HDC a = U(CreateCompatibleDC)(NULL), b = U(CreateCompatibleDC)(NULL);
    REQUIRE(a && b);
    U(SetBkMode)(a, TRANSPARENT);
    U(SetTextColor)(a, RGB(1, 1, 1));
    U(SetROP2)(a, R2_NOT);
    int ok;
    CHECK(dca_u32(b, 0xe0, &ok) == OPAQUE && ok);
    CHECK(dca_u32(b, 0xbc, &ok) == 0 && ok);
    CHECK(U(GetROP2)(b) == R2_COPYPEN);
    CHECK(dca_u32(a, 0xe0, &ok) == TRANSPARENT && ok);
    U(DeleteDC)(a); U(DeleteDC)(b);
}
TEST(gdi_handle_table_dcattr_savedc_restore_syncs_memory) {
    GH_NEED();
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    U(SetBkMode)(dc, TRANSPARENT);
    U(SetTextColor)(dc, RGB(5, 6, 7));
    U(SetPolyFillMode)(dc, WINDING);
    int lvl = U(SaveDC)(dc);
    CHECK(lvl >= 1);
    U(SetBkMode)(dc, OPAQUE);
    U(SetTextColor)(dc, RGB(200, 200, 200));
    U(SetPolyFillMode)(dc, ALTERNATE);
    int ok;
    CHECK(dca_u32(dc, 0xe0, &ok) == OPAQUE);
    /* NtGdiRestoreDC -> DC::RestoreAttributes rewrites the user-mode block with the saved values */
    CHECK(U(RestoreDC)(dc, -1));
    CHECK(dca_u32(dc, 0xe0, &ok) == TRANSPARENT && ok);
    CHECK(dca_u32(dc, 0xbc, &ok) == RGB(5, 6, 7) && ok);
    CHECK(dca_u32(dc, 0xe4, &ok) == WINDING && ok);
    CHECK(U(GetBkMode)(dc) == TRANSPARENT);
    U(DeleteDC)(dc);
}
TEST(gdi_handle_table_dcattr_savedc_restores_selected_brush_field) {
    GH_NEED();
    HDC dc = U(CreateCompatibleDC)(NULL);
    HBRUSH b = U(CreateSolidBrush)(RGB(7, 7, 7));
    REQUIRE(dc && b);
    U(SaveDC)(dc);
    HGDIOBJ ob = U(SelectObject)(dc, b);
    unsigned long long hb = 0;
    CHECK(dca_read(dc, 0xa0, &hb, 8) && (HGDIOBJ)(ULONG_PTR)hb == b);
    CHECK(U(RestoreDC)(dc, -1));
    CHECK(dca_read(dc, 0xa0, &hb, 8) && (HGDIOBJ)(ULONG_PTR)hb == ob);
    CHECK(U(GetCurrentObject)(dc, OBJ_BRUSH) == ob);
    U(DeleteDC)(dc); U(DeleteObject)(b);
}
TEST(gdi_handle_table_dcattr_kernel_call_does_not_disturb_fields) {
    GH_NEED();
    REQUIRE(UN(NtGdiGetDCObject));
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    U(SetBkMode)(dc, TRANSPARENT);
    U(SetTextColor)(dc, RGB(11, 12, 13));
    /* NtGdiGetDCObject runs DC::SaveAttributes (user -> kernel) and RestoreAttributes: the block must read the same after */
    UN(NtGdiGetDCObject)(dc, 0x50000);
    int ok;
    CHECK(dca_u32(dc, 0xe0, &ok) == TRANSPARENT && ok);
    CHECK(dca_u32(dc, 0xbc, &ok) == RGB(11, 12, 13) && ok);
    U(DeleteDC)(dc);
}
TEST(gdi_handle_table_dcattr_deleted_dc_cell_has_no_block) {
    GH_NEED();
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    unsigned long long p;
    CHECK(dca_base(dc, &p));
    unsigned idx = GH_IDX(dc);
    U(DeleteDC)(dc);
    struct gh_cell c;
    CHECK(gh_read(idx, &c));
    CHECK(c.puser == 0);
    /* the user-mode getters now refuse the stale handle: they validate Objt == 1 and the upper word */
    CHECK(U(GetBkMode)(dc) == 0);
    CHECK(U(GetMapMode)(dc) == 0);
    t_note("DC_ATTR was at 0x%llx", p);
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery((void *)(ULONG_PTR)p, &mbi, sizeof mbi) == sizeof mbi) t_note("page state 0x%lx", mbi.State);
}
TEST(gdi_handle_table_dcattr_other_dc_getters_reject_non_dc_cells) {
    GH_NEED();
    HBRUSH b = U(CreateSolidBrush)(RGB(1, 1, 1));
    HRGN r = U(CreateRectRgn)(0, 0, 1, 1);
    REQUIRE(b && r);
    /* the DC getters require cell Objt == 1; a brush/region handle yields their error value and no DC_ATTR read */
    CHECK(U(GetBkMode)((HDC)b) == 0);
    CHECK(U(GetROP2)((HDC)r) == 0);
    CHECK(U(GetTextColor)((HDC)b) == CLR_INVALID);
    CHECK(U(GetBkColor)((HDC)r) == CLR_INVALID);
    CHECK(U(GetTextAlign)((HDC)b) == GDI_ERROR);
    SetLastError(0);
    CHECK(U(GetMapMode)((HDC)b) == 0);
    CHECK(GetLastError() == ERROR_INVALID_PARAMETER);
    U(DeleteObject)(b); U(DeleteObject)(r);
}
