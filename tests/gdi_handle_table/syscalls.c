/* Direct win32u syscalls that touch the GDI handle manager, then local-state inspection.
 * Signatures come from the win32kbase/win32kfull analysis:
 *   NtGdiGetEntry(uint index, void *out24) -> NTSTATUS            win32kbase 0xb4d40
 *   NtGdiDeleteObjectApp(HGDIOBJ)          -> BOOL (low byte)     win32kbase 0x3a900
 *   NtGdiGetDCObject(HDC, int typeBits)    -> HGDIOBJ             win32kfull 0x99060
 *   NtGdiGetDCDword(HDC, int idx, ulong*)  -> BOOL                win32kfull 0xeb280
 *   NtGdiCreateClientObj(uint typeBits)    -> HANDLE              win32kfull 0x110a10
 *   NtGdiDeleteClientObj(HANDLE)           -> BOOL
 *   NtGdiCreateRectRgn(l, t, r, b)         -> HRGN                win32kbase 0x1cc30 */
#include "gh.h"

#define OKLOW(r) (((ULONG_PTR)(r) & 0xff) != 0)
#define STATUS_UNSUCCESSFUL_ ((LONG)0xC0000001)

static unsigned long gh_session_peak(void) {
    return GHN(NtUserGetGuiResources) ? GHN(NtUserGetGuiResources)((HANDLE)(LONG_PTR)-2, 2) : 0xffffffff;
}

/* ---- stub numbers (tools/win32u_syscalls.tsv) ----------------------------------------- */
TEST(gdi_handle_table_sys_stub_numbers) {
    static const struct { const char *n; long num; } v[] = {
        { "NtGdiDeleteObjectApp", 4131 }, { "NtGdiGetDCObject", 4149 }, { "NtGdiGetDCDword", 4159 },
        { "NtGdiGetEntry", 4795 }, { "NtGdiCreateClientObj", 4274 }, { "NtGdiDeleteClientObj", 4228 },
        { "NtGdiCreateRectRgn", 4225 }, { "NtUserGetGuiResources", 5131 }, { "NtGdiFlush", 4114 },
    };
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        FARPROC p = t_proc_in("win32u.dll", v[i].n);
        CHECKF(p != NULL, "win32u export %s", v[i].n);
        if (!p) continue;
        CHECKF(k_stub_valid(p), "%s stub shape", v[i].n);
        if (t_is_wine()) continue;
        CHECKF(k_stub_number(p) == v[i].num, "%s number %ld want %ld", v[i].n, k_stub_number(p), v[i].num);
    }
}

/* ---- NtGdiGetEntry ---------------------------------------------------------------------- */
TEST(gdi_handle_table_sys_getentry_free_slot_unsuccessful) {
    GH_NEED();
    REQUIRE(GHN(NtGdiGetEntry));
    HRGN r = U(CreateEllipticRgn)(0, 0, 3, 3);       /* not a rect region: those stay in the region cache after a delete */
    REQUIRE(r);
    unsigned idx = GH_IDX(r);
    struct gh_cell out;
    CHECK(GHN(NtGdiGetEntry)(idx, &out) == 0);
    U(DeleteObject)(r);
    /* GdiHandleEntryDirectory::GetEntry(idx, false) needs a non-NULL object-lookup slot; vUnlockAndRelease clears it,
     * so a freed index is STATUS_UNSUCCESSFUL. Another process may have taken the slot in between: then it is not ours. */
    memset(&out, 0, sizeof out);
    LONG st = GHN(NtGdiGetEntry)(idx, &out);
    CHECKF(st == STATUS_UNSUCCESSFUL_ || (st == 0 && (out.owner & ~1u) != GH_PID()), "status 0x%lx owner 0x%x", st, out.owner);
}
TEST(gdi_handle_table_sys_getentry_live_matches_peb_cell) {
    GH_NEED();
    REQUIRE(GHN(NtGdiGetEntry));
    HBRUSH b = U(CreateSolidBrush)(RGB(1, 2, 3));
    REQUIRE(b);
    struct gh_cell a, k;
    CHECK(gh_cell_of(b, &a));
    memset(&k, 0, sizeof k);
    CHECK(GHN(NtGdiGetEntry)(GH_IDX(b), &k) == 0);
    CHECK(k.q0 == a.q0 && k.upper == a.upper && k.objt == a.objt && k.puser == a.puser);
    CHECK((k.owner & ~1u) == (a.owner & ~1u));
    U(DeleteObject)(b);
}
TEST(gdi_handle_table_sys_getentry_high_bits_masked) {
    GH_NEED();
    REQUIRE(GHN(NtGdiGetEntry));
    unsigned long peak = gh_session_peak();
    if (peak > 0x10000) { t_skip("session peak %lu > 0x10000, extended indices in use", peak); return; }
    HRGN r = U(CreateRectRgn)(0, 0, 3, 3);
    REQUIRE(r);
    struct gh_cell a, b, c;
    memset(&b, 0, sizeof b); memset(&c, 0, sizeof c);
    CHECK(gh_cell_of(r, &a));
    /* GdiHandleManager::DecodeIndex: while the session peak is <= 0x10000 the upper index bits are dropped */
    CHECK(GHN(NtGdiGetEntry)(GH_IDX(r), &b) == 0);
    CHECK(GHN(NtGdiGetEntry)(GH_IDX(r) | 0x00ff0000, &c) == 0);
    CHECK(b.q0 == c.q0 && b.upper == c.upper && b.puser == c.puser);
    CHECK(c.q0 == a.q0);
    U(DeleteObject)(r);
}
TEST(gdi_handle_table_sys_getentry_stock_object) {
    GH_NEED();
    REQUIRE(GHN(NtGdiGetEntry));
    HGDIOBJ h = U(GetStockObject)(BLACK_BRUSH);
    struct gh_cell k, a;
    memset(&k, 0, sizeof k);
    CHECK(GHN(NtGdiGetEntry)(GH_IDX(h), &k) == 0);
    CHECK(gh_cell_of(h, &a));
    CHECK(k.upper == a.upper && k.upper == GH_HIWORD(h));
    CHECK(k.objt == 0x10);
}
TEST(gdi_handle_table_sys_getentry_all_live_cells_match) {
    GH_NEED();
    REQUIRE(GHN(NtGdiGetEntry));
    HGDIOBJ hs[40]; const struct gh_kind *ks[40]; int n = 0, bad = 0;
    for (size_t i = 0; i < GH_NKINDS && n < 38; i++)
        for (int rep = 0; rep < 2; rep++) {
            HGDIOBJ h = gh_all_kinds[i]->mk();
            if (h) { hs[n] = h; ks[n] = gh_all_kinds[i]; n++; }
        }
    for (int i = 0; i < n; i++) {
        struct gh_cell a, k;
        if (!gh_cell_of(hs[i], &a)) { bad++; continue; }
        memset(&k, 0, sizeof k);
        if (GHN(NtGdiGetEntry)(GH_IDX(hs[i]), &k) != 0 || k.q0 != a.q0 || k.upper != a.upper || k.objt != a.objt) bad++;
    }
    CHECKF(bad == 0, "%d of %d cells differ between PEB table and NtGdiGetEntry", bad, n);
    for (int i = n - 1; i >= 0; i--) ks[i]->rm(hs[i]);
}

/* ---- NtGdiDeleteObjectApp ----------------------------------------------------------------- */
TEST(gdi_handle_table_sys_delete_stock_bit_returns_true) {
    GH_NEED();
    REQUIRE(UN(NtGdiDeleteObjectApp));
    /* first statement: if (handle & 0x800000) return 1; no validation of the rest of the value at all */
    ULONG vals[] = { 0x00800000, 0x00ff0000, 0x00800001, 0x01800000, 0x1d9f0000, 0xffffffff, 0x80800000 };
    for (size_t i = 0; i < sizeof vals / sizeof vals[0]; i++)
        CHECKF(UN(NtGdiDeleteObjectApp)((HANDLE)(ULONG_PTR)vals[i]) == 1, "value 0x%lx", vals[i]);
}
TEST(gdi_handle_table_sys_delete_null_and_garbage_return_false) {
    GH_NEED();
    REQUIRE(UN(NtGdiDeleteObjectApp));
    /* index 0xffff is the last cell of the table; a non-zero type that cannot equal a free cell's Objt keeps clear of the
     * early-out that returns TRUE for matching public entries (value 1 would hit a free cell) */
    /* a palette-typed handle is answered TRUE even when no such object exists */
    CHECK(OKLOW(UN(NtGdiDeleteObjectApp)((HANDLE)(ULONG_PTR)0x0008ffff)));
    ULONG vals[] = { 0, 0x0001ffff, 0x0004ffff, 0x0005ffff, 0x000affff, 0x0010ffff, 0x0030ffff, 0x0009ffff, 0x0002ffff };
    for (size_t i = 0; i < sizeof vals / sizeof vals[0]; i++) {
        ULONG_PTR r = UN(NtGdiDeleteObjectApp)((HANDLE)(ULONG_PTR)vals[i]);
        CHECKF(!OKLOW(r), "value 0x%lx -> 0x%llx", vals[i], (unsigned long long)r);
    }
}
static void del_direct(const struct gh_kind *k) {
    GH_NEED();
    REQUIRE(UN(NtGdiDeleteObjectApp));
    GH_MAKE(h, k);
    if (gh_cached_kind(k)) gh_cache_fill(k);       /* with a full cache the delete frees the cell instead of recycling it */
    unsigned idx = GH_IDX(h);
    ULONG_PTR r = UN(NtGdiDeleteObjectApp)((HANDLE)h);
    CHECKF(OKLOW(r), "%s: NtGdiDeleteObjectApp = 0x%llx", k->name, (unsigned long long)r);
    struct gh_cell c;
    if (gh_read(idx, &c)) {
        CHECKF(c.objt == 0, "%s cell still live (objt 0x%x)", k->name, c.objt);
        CHECKF(c.puser == 0, "%s pUser 0x%llx survived the delete", k->name, c.puser);
    }
    /* a second delete of the same handle fails, except for palettes: a palette handle whose object is gone answers TRUE */
    ULONG_PTR r2 = UN(NtGdiDeleteObjectApp)((HANDLE)h);
    CHECKF(OKLOW(r2) == (k->type7 == 0x08), "%s: second NtGdiDeleteObjectApp = 0x%llx", k->name, (unsigned long long)r2);
}
#define GEND(id) TEST(gdi_handle_table_sys_delete_direct_##id) { del_direct(&gk_##id); }
GEND(dc_memory) GEND(dc_display) GEND(rgn_rect) GEND(rgn_ellip) GEND(bmp_mono) GEND(bmp_dib24)
GEND(pal_logical) GEND(font_indirect) GEND(brush_solid) GEND(brush_hatch) GEND(brush_pattern)
GEND(pen_solid) GEND(extpen_geom)

TEST(gdi_handle_table_sys_delete_colorspace_not_handled) {
    GH_NEED();
    REQUIRE(UN(NtGdiDeleteObjectApp));
    HGDIOBJ h = mk_colorspace();
    REQUIRE(h);
    struct gh_cell before, after;
    CHECK(gh_cell_of(h, &before));
    /* the type switch in NtGdiDeleteObjectApp has no ICM colour-space case (NtGdiDeleteColorSpace does that): result 0 */
    CHECK(!OKLOW(UN(NtGdiDeleteObjectApp)((HANDLE)h)));
    CHECK(gh_cell_of(h, &after));
    CHECK(after.objt == before.objt && after.objt != 0);
    rm_colorspace(h);
}
TEST(gdi_handle_table_sys_delete_wrong_type_bits_does_not_free) {
    GH_NEED();
    REQUIRE(UN(NtGdiDeleteObjectApp));
    /* a live bitmap handle with the type bits rewritten to other deletable types: the lookup uses the claimed type, finds
     * no such object under that handle (cell upper bits differ) and returns 0; the bitmap must survive. */
    HGDIOBJ b = mk_bmp_color();
    REQUIRE(b);
    unsigned long long base = (unsigned long long)(ULONG_PTR)b & ~0x7f0000ull;
    unsigned types[] = { 0x01, 0x04, 0x08, 0x10, 0x30, 0x50 };
    for (size_t i = 0; i < sizeof types / sizeof types[0]; i++) {
        HANDLE fake = (HANDLE)(ULONG_PTR)(unsigned)(base | ((unsigned long long)types[i] << 16));
        ULONG_PTR r = UN(NtGdiDeleteObjectApp)(fake);
        /* the palette type answers TRUE for a handle it does not find (the bitmap is still checked to survive below) */
        CHECKF(OKLOW(r) == (types[i] == 0x08), "type 0x%x -> 0x%llx", types[i], (unsigned long long)r);
        CHECKF(U(GetObjectType)(b) == OBJ_BITMAP, "bitmap vanished after type 0x%x attempt", types[i]);
    }
    U(DeleteObject)(b);
}
TEST(gdi_handle_table_sys_delete_wrong_reuse_byte_does_not_free) {
    GH_NEED();
    REQUIRE(UN(NtGdiDeleteObjectApp));
    HGDIOBJ b = mk_brush_solid();
    REQUIRE(b);
    /* a handle with the same index and type but another reuse byte must not match the cell (upper bits compared as a word) */
    for (unsigned flip = 1; flip < 0x100; flip <<= 1) {
        HANDLE fake = (HANDLE)(ULONG_PTR)(unsigned)(((unsigned)(ULONG_PTR)b) ^ (flip << 24));
        if ((((ULONG_PTR)fake) & GH_STOCK)) continue;
        CHECKF(!OKLOW(UN(NtGdiDeleteObjectApp)(fake)), "reuse flip 0x%x deleted something", flip);
        CHECK(U(GetObjectType)(b) == OBJ_BRUSH);
    }
    CHECK(U(DeleteObject)(b));
}
TEST(gdi_handle_table_sys_delete_client_object_not_handled) {
    GH_NEED();
    REQUIRE(UN(NtGdiDeleteObjectApp));
    REQUIRE(UN(NtGdiCreateClientObj));
    REQUIRE(UN(NtGdiDeleteClientObj));
    HANDLE h = UN(NtGdiCreateClientObj)(0x660000);
    REQUIRE(h);
    struct gh_cell c;
    /* type & 0x1f == 6 (CLIENTOBJ) is not in NtGdiDeleteObjectApp's switch: 0, object stays until NtGdiDeleteClientObj */
    CHECK(!OKLOW(UN(NtGdiDeleteObjectApp)(h)));
    CHECK(gh_cell_of(h, &c) && c.objt == 6);
    CHECK(OKLOW(UN(NtGdiDeleteClientObj)(h)));
    CHECK(gh_cell_of(h, &c) && c.objt == 0);
}
TEST(gdi_handle_table_sys_delete_selected_bitmap_lifetime) {
    GH_NEED();
    REQUIRE(UN(NtGdiDeleteObjectApp));
    HDC dc = U(CreateCompatibleDC)(NULL);
    HBITMAP bm = U(CreateBitmap)(8, 8, 1, 32, NULL);
    REQUIRE(dc && bm);
    HGDIOBJ old = U(SelectObject)(dc, bm);
    ULONG_PTR r = UN(NtGdiDeleteObjectApp)((HANDLE)bm);
    struct gh_cell c;
    CHECK(gh_cell_of(bm, &c));
    t_note("delete of a bitmap selected into a DC: result 0x%llx, cell objt 0x%x", (unsigned long long)r, c.objt);
    /* whatever the result, the cell and the result must agree */
    if (!OKLOW(r)) CHECK(c.objt == 5);
    U(SelectObject)(dc, old);
    CHECK(UN(NtGdiGetDCObject)(dc, 0x50000) == old);
    U(DeleteDC)(dc);
    U(DeleteObject)(bm);
}

/* ---- NtGdiCreateClientObj / NtGdiDeleteClientObj -------------------------------------- */
static void client_obj(ULONG bits, DWORD objtype) {
    GH_NEED();
    REQUIRE(UN(NtGdiCreateClientObj));
    REQUIRE(UN(NtGdiDeleteClientObj));
    DWORD g0 = U(GetGuiResources)(GetCurrentProcess(), 0);
    HANDLE h = UN(NtGdiCreateClientObj)(bits);
    CHECK(h != NULL);
    if (!h) return;
    struct gh_cell c;
    /* handle = Insert(type 6) | bits, then the handle-type modification: the cell keeps Objt 6 and gets the full type in its upper word */
    CHECK(GH_TYPE7(h) == (bits >> 16));
    CHECK(gh_cell_of(h, &c));
    CHECKF(c.objt == 6, "objt 0x%x", c.objt);
    CHECKF(c.upper == GH_HIWORD(h), "upper 0x%04x HIWORD 0x%04x", c.upper, GH_HIWORD(h));
    CHECK((c.owner & ~1u) == GH_PID());
    CHECK(c.q0 >= GH_KERNEL_FLOOR);
    CHECKF(U(GetObjectType)((HGDIOBJ)h) == objtype, "GetObjectType %lu want %lu", U(GetObjectType)((HGDIOBJ)h), objtype);
    t_note("bits 0x%lx: GR_GDIOBJECTS %lu -> %lu", bits, g0, U(GetGuiResources)(GetCurrentProcess(), 0));
    ULONG_PTR r = UN(NtGdiDeleteClientObj)(h);
    t_note("NtGdiDeleteClientObj = 0x%llx", (unsigned long long)r);
    /* the cell is released; another allocator may already have taken the slot, which shows as a different upper word */
    CHECK(gh_cell_of(h, &c) && (c.objt == 0 || c.upper != GH_HIWORD(h)));
    CHECK(U(GetGuiResources)(GetCurrentProcess(), 0) == g0);
}
TEST(gdi_handle_table_sys_client_obj_metafile)    { client_obj(0x260000, OBJ_METAFILE); }
TEST(gdi_handle_table_sys_client_obj_enhmetafile) { client_obj(0x460000, OBJ_ENHMETAFILE); }
TEST(gdi_handle_table_sys_client_obj_metadc)      { client_obj(0x660000, OBJ_METADC); }
TEST(gdi_handle_table_sys_client_obj_rejects_bad_bits) {
    GH_NEED();
    REQUIRE(UN(NtGdiCreateClientObj));
    if (t_is_wine()) { t_skip("Wine's NtGdiCreateClientObj asserts on a zero object type"); return; }
    /* accept iff (b & 0x600000) != 0 && (b & 0xff00ffff) == 0 && (b & 0x1f0000) == 0x60000 */
    ULONG bad[] = { 0, 0x060000, 0x160000, 0x860000, 0x270000, 0x1260000, 0x262000, 0x000001, 0x200000, 0x400000,
                    0x610000, 0x7f0000, 0xffffffff, 0x2060001 };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        HANDLE h = UN(NtGdiCreateClientObj)(bad[i]);
        CHECKF(h == NULL, "bits 0x%lx produced %p", bad[i], h);
        if (h) UN(NtGdiDeleteClientObj)(h);
    }
}
TEST(gdi_handle_table_sys_client_obj_distinct_slots) {
    GH_NEED();
    REQUIRE(UN(NtGdiCreateClientObj));
    HANDLE h[16];
    for (int i = 0; i < 16; i++) { h[i] = UN(NtGdiCreateClientObj)(0x660000); REQUIRE(h[i]); }
    for (int i = 0; i < 16; i++) for (int j = i + 1; j < 16; j++) CHECK(GH_IDX(h[i]) != GH_IDX(h[j]));
    for (int i = 0; i < 16; i++) UN(NtGdiDeleteClientObj)(h[i]);
}

/* ---- NtGdiGetDCObject --------------------------------------------------------------------- */
TEST(gdi_handle_table_sys_getdcobject_brush_pen_defaults) {
    GH_NEED();
    REQUIRE(UN(NtGdiGetDCObject));
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    CHECK(UN(NtGdiGetDCObject)(dc, 0x100000) == U(GetStockObject)(WHITE_BRUSH));
    CHECK(UN(NtGdiGetDCObject)(dc, 0x300000) == U(GetStockObject)(BLACK_PEN));
    CHECK(UN(NtGdiGetDCObject)(dc, 0x500000) == U(GetStockObject)(BLACK_PEN));
    U(DeleteDC)(dc);
}
TEST(gdi_handle_table_sys_getdcobject_follows_select) {
    GH_NEED();
    REQUIRE(UN(NtGdiGetDCObject));
    HDC dc = U(CreateCompatibleDC)(NULL);
    HBRUSH b = U(CreateSolidBrush)(RGB(9, 8, 7));
    HPEN p = U(CreatePen)(PS_SOLID, 2, RGB(1, 2, 3));
    HBITMAP bm = U(CreateBitmap)(8, 8, 1, 32, NULL);
    REQUIRE(dc && b && p && bm);
    HGDIOBJ ob = U(SelectObject)(dc, b), op = U(SelectObject)(dc, p), obm = U(SelectObject)(dc, bm);
    CHECK(UN(NtGdiGetDCObject)(dc, 0x100000) == b);
    CHECK(UN(NtGdiGetDCObject)(dc, 0x300000) == p);
    CHECK(UN(NtGdiGetDCObject)(dc, 0x50000) == bm);
    CHECK(U(GetCurrentObject)(dc, OBJ_BRUSH) == b);
    CHECK(U(GetCurrentObject)(dc, OBJ_PEN) == p);
    CHECK(U(GetCurrentObject)(dc, OBJ_BITMAP) == bm);
    U(SelectObject)(dc, ob); U(SelectObject)(dc, op); U(SelectObject)(dc, obm);
    CHECK(UN(NtGdiGetDCObject)(dc, 0x50000) == obm);
    U(DeleteDC)(dc); U(DeleteObject)(b); U(DeleteObject)(p); U(DeleteObject)(bm);
}
TEST(gdi_handle_table_sys_getdcobject_font) {
    GH_NEED();
    REQUIRE(UN(NtGdiGetDCObject));
    HDC dc = U(CreateCompatibleDC)(NULL);
    HGDIOBJ f = mk_font_indirect();
    REQUIRE(dc && f);
    HGDIOBJ of = U(SelectObject)(dc, f);
    CHECK(UN(NtGdiGetDCObject)(dc, 0xa0000) == f);
    CHECK(U(GetCurrentObject)(dc, OBJ_FONT) == f);
    U(SelectObject)(dc, of);
    U(DeleteDC)(dc); U(DeleteObject)(f);
}
TEST(gdi_handle_table_sys_getdcobject_unknown_selectors_return_null) {
    GH_NEED();
    REQUIRE(UN(NtGdiGetDCObject));
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    int sel[] = { 0, 1, 0x10000, 0x20000, 0x40000, 0x60000, 0x70000, 0x90000, 0xb0000, 0x200000, 0x400000, 0x7f0000, 0x1000000, -1 };
    for (size_t i = 0; i < sizeof sel / sizeof sel[0]; i++) {
        HANDLE r = UN(NtGdiGetDCObject)(dc, sel[i]);
        CHECKF(r == NULL, "selector 0x%x -> %p", sel[i], r);
    }
    U(DeleteDC)(dc);
}
TEST(gdi_handle_table_sys_getdcobject_rejects_non_dc) {
    GH_NEED();
    REQUIRE(UN(NtGdiGetDCObject));
    HBITMAP bm = U(CreateBitmap)(2, 2, 1, 32, NULL);
    HRGN rg = U(CreateRectRgn)(0, 0, 2, 2);
    REQUIRE(bm && rg);
    HANDLE bad[] = { NULL, (HANDLE)1, bm, rg, U(GetStockObject)(WHITE_BRUSH), (HANDLE)0x00010000, (HANDLE)(ULONG_PTR)0xffffffffu };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
        CHECKF(UN(NtGdiGetDCObject)(bad[i], 0x100000) == NULL, "handle %p", bad[i]);
    U(DeleteObject)(bm); U(DeleteObject)(rg);
}
TEST(gdi_handle_table_sys_getdcobject_deleted_dc) {
    GH_NEED();
    REQUIRE(UN(NtGdiGetDCObject));
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    U(DeleteDC)(dc);
    CHECK(UN(NtGdiGetDCObject)(dc, 0x100000) == NULL);
    CHECK(UN(NtGdiGetDCObject)(dc, 0x50000) == NULL);
}
TEST(gdi_handle_table_sys_getdcobject_dc_attr_agrees) {
    GH_NEED();
    REQUIRE(UN(NtGdiGetDCObject));
    HDC dc = U(CreateCompatibleDC)(NULL);
    HBRUSH b = U(CreateHatchBrush)(HS_VERTICAL, RGB(4, 5, 6));
    REQUIRE(dc && b);
    struct gh_cell c; unsigned long long hb = 0;
    HGDIOBJ ob = U(SelectObject)(dc, b);
    CHECK(gh_cell_of(dc, &c) && c.puser);
    /* GetCurrentObject(OBJ_BRUSH) reads DC_ATTR+0xa0 with no syscall; NtGdiGetDCObject asks the kernel DC: same handle */
    CHECK(k_try_read((void *)(ULONG_PTR)(c.puser + 0xa0), &hb, 8));
    CHECK((HANDLE)(ULONG_PTR)hb == b);
    CHECK(UN(NtGdiGetDCObject)(dc, 0x100000) == (HANDLE)(ULONG_PTR)hb);
    U(SelectObject)(dc, ob);
    U(DeleteDC)(dc); U(DeleteObject)(b);
}

/* ---- NtGdiGetDCDword ---------------------------------------------------------------------- */
TEST(gdi_handle_table_sys_getdcdword_memdc_flag) {
    GH_NEED();
    REQUIRE(UN(NtGdiGetDCDword));
    ULONG out = 0xAAAA5555;
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    /* index 7 returns (DC.dctype == 1); GetObjectType maps non-zero to OBJ_MEMDC */
    CHECK(UN(NtGdiGetDCDword)(dc, 7, &out) != 0);
    CHECK(out == 1);
    CHECK(U(GetObjectType)(dc) == OBJ_MEMDC);
    U(DeleteDC)(dc);
}
TEST(gdi_handle_table_sys_getdcdword_display_dc_flag) {
    GH_NEED();
    REQUIRE(UN(NtGdiGetDCDword));
    HDC dc = gh_disp();
    REQUIRE(dc);
    ULONG out = 0xAAAA5555;
    CHECK(UN(NtGdiGetDCDword)(dc, 7, &out) != 0);
    CHECK(out == 0);
    CHECK(U(GetObjectType)(dc) == OBJ_DC);
    U(DeleteDC)(dc);
}
TEST(gdi_handle_table_sys_getdcdword_bad_index_leaves_output) {
    GH_NEED();
    REQUIRE(UN(NtGdiGetDCDword));
    HDC dc = U(CreateCompatibleDC)(NULL);
    REQUIRE(dc);
    int bad[] = { 9, 10, 100, -1, 0x7fffffff };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        ULONG out = 0xAAAA5555;
        CHECKF(UN(NtGdiGetDCDword)(dc, bad[i], &out) == 0, "index %d", bad[i]);
        CHECKF(out == 0xAAAA5555, "index %d wrote 0x%lx", bad[i], out);
    }
    U(DeleteDC)(dc);
}
TEST(gdi_handle_table_sys_getdcdword_invalid_dc) {
    GH_NEED();
    REQUIRE(UN(NtGdiGetDCDword));
    HBITMAP bm = U(CreateBitmap)(2, 2, 1, 32, NULL);
    REQUIRE(bm);
    HANDLE bad[] = { NULL, (HANDLE)1, bm, (HANDLE)0x00010001, U(GetStockObject)(BLACK_PEN) };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        ULONG out = 0xAAAA5555;
        CHECKF(UN(NtGdiGetDCDword)(bad[i], 7, &out) == 0, "handle %p", bad[i]);
        CHECKF(out == 0xAAAA5555, "handle %p wrote output", bad[i]);
    }
    U(DeleteObject)(bm);
}
TEST(gdi_handle_table_sys_getdcdword_info_dc) {
    GH_NEED();
    REQUIRE(UN(NtGdiGetDCDword));
    HDC ic = U(CreateICW)(L"DISPLAY", NULL, NULL, NULL);
    REQUIRE(ic);
    ULONG out = 0xAAAA5555;
    CHECK(UN(NtGdiGetDCDword)(ic, 7, &out) != 0);
    CHECK(out == 0);
    CHECK(U(GetObjectType)(ic) == OBJ_DC);
    U(DeleteDC)(ic);
}

/* ---- NtGdiCreateRectRgn ------------------------------------------------------------------- */
TEST(gdi_handle_table_sys_createrectrgn_accepts_range) {
    GH_NEED();
    REQUIRE(UN(NtGdiCreateRectRgn));
    static const int ok[][4] = {
        { 0, 0, 1, 1 }, { 1, 1, 0, 0 }, { -5, -5, 5, 5 }, { 0, 0, 0, 0 },
        { -0x8000000, -0x8000000, 0x7ffffff, 0x7ffffff }, { 0x7ffffff, 0x7ffffff, -0x8000000, -0x8000000 },
    };
    for (size_t i = 0; i < sizeof ok / sizeof ok[0]; i++) {
        HRGN r = UN(NtGdiCreateRectRgn)(ok[i][0], ok[i][1], ok[i][2], ok[i][3]);
        CHECKF(r != NULL, "case %zu", i);
        if (!r) continue;
        CHECK(GH_TYPE7(r) == 0x04);
        struct gh_cell c;
        if (gh_cell_of(r, &c)) {
            CHECK(c.objt == 4);
            CHECK(c.puser != 0);
            CHECK((c.owner & ~1u) == GH_PID());
        }
        U(DeleteObject)(r);
    }
}
TEST(gdi_handle_table_sys_createrectrgn_rejects_out_of_range) {
    GH_NEED();
    REQUIRE(UN(NtGdiCreateRectRgn));
    /* each coordinate must satisfy ((c & 0xf8000000) + 0x8000000) & 0xf7ffffff == 0, i.e. -2^27 <= c < 2^27 */
    for (int pos = 0; pos < 4; pos++) {
        int vals[] = { 0x8000000, -0x8000001, 0x7fffffff, (int)0x80000000 };
        for (size_t v = 0; v < 4; v++) {
            int c[4] = { 1, 2, 3, 4 };
            c[pos] = vals[v];
            SetLastError(0);
            HRGN r = UN(NtGdiCreateRectRgn)(c[0], c[1], c[2], c[3]);
            CHECKF(r == NULL, "coord %d = 0x%x created %p", pos, vals[v], r);
            CHECKF(GetLastError() == ERROR_INVALID_PARAMETER, "coord %d = 0x%x error %lu", pos, vals[v], GetLastError());
            if (r) U(DeleteObject)(r);
        }
    }
}
TEST(gdi_handle_table_sys_createrectrgn_object_attr_slot) {
    GH_NEED();
    REQUIRE(UN(NtGdiCreateRectRgn));
    HRGN r = UN(NtGdiCreateRectRgn)(0, 0, 4, 4);
    REQUIRE(r);
    struct gh_cell c;
    CHECK(gh_cell_of(r, &c));
    /* the attribute allocator hands out 0x18-byte slots of a 4 KiB user page (0xaa of them, last at +0xfd8) */
    CHECK(c.puser != 0);
    unsigned off = (unsigned)(c.puser & 0xfff);
    CHECKF(off <= 0xfd8 && off % 0x18 == 0, "pUser 0x%llx page offset 0x%x", c.puser, off);
    MEMORY_BASIC_INFORMATION mbi;
    if (c.puser && VirtualQuery((void *)(ULONG_PTR)c.puser, &mbi, sizeof mbi)) {
        CHECK(mbi.State == MEM_COMMIT);
        CHECK(mbi.Type == MEM_PRIVATE);
        CHECKF(mbi.Protect == PAGE_READWRITE, "protect 0x%lx", mbi.Protect);
    }
    U(DeleteObject)(r);
}
TEST(gdi_handle_table_sys_createrectrgn_box_orders_corners) {
    GH_NEED();
    REQUIRE(UN(NtGdiCreateRectRgn));
    /* the raw syscall hands back a region whose client block is not filled in yet; gdi32's CreateRectRgn completes it */
    HRGN r = U(CreateRectRgn)(30, 40, 10, 20);
    REQUIRE(r);
    RECT rc;
    CHECK(U(GetRgnBox)(r, &rc) == SIMPLEREGION);
    /* the corners are swapped into order */
    CHECK(rc.left == 10 && rc.top == 20 && rc.right == 30 && rc.bottom == 40);
    U(DeleteObject)(r);
}
