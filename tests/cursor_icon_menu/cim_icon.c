/* Icons and cursors as TYPE_CURSOR (3) USER objects: creation, GetIconInfo/GetIconSize, destruction rules
 * (NtUserDestroyCursor modes), shared system icons, CopyImage/CopyIcon and DrawIconEx into a DIB section. */
#include "cim.h"
#include <wchar.h>

static void free_info(ICONINFO *ii) {
    if (ii->hbmMask) U(DeleteObject)(ii->hbmMask);
    if (ii->hbmColor) U(DeleteObject)(ii->hbmColor);
}
static int bm_dim(HBITMAP b, BITMAP *bm) { memset(bm, 0, sizeof *bm); return U(GetObjectW)(b, sizeof *bm, bm) == sizeof *bm; }

#define MKI(var, expr) HICON var = (expr); if (!var) { t_skip("cannot create icon (err %lu)", GetLastError()); return; }

/* ---------------- creation: the object in the USER handle table ---------------- */
static void entry_ok(HICON h) {
    cim_he e;
    CHECK(cim_he_read(h, &e));
    EXPECT_EQ(e.type, CT_CURSOR);
    EXPECT_EQ(e.uniq, CIM_UNIQ(h));
    CHECK((e.flags & 1) == 0);
    EXPECT_EQ(e.owner, GetCurrentProcessId()); /* cursors are process owned */
    EXPECT_EQ(e.delta, 0);                     /* pool object: no desktop-heap part */
    EXPECT_EQ(e.f10, 0);
}
TEST(cursor_icon_menu_icon_createicon_entry) { CIM_NEED(); MKI(h, cim_mk_icon(16, 16, 0xff, 0)); entry_ok(h); U(DestroyIcon)(h); }
TEST(cursor_icon_menu_icon_indirect_icon_entry) { CIM_NEED(); MKI(h, cim_mk_icon_ii(16, 16, 32)); entry_ok(h); U(DestroyIcon)(h); }
TEST(cursor_icon_menu_icon_indirect_cursor_entry) { CIM_NEED(); MKI(h, cim_mk_cursor_ii(16, 16, 3, 4)); entry_ok(h); U(DestroyIcon)(h); }
TEST(cursor_icon_menu_icon_createcursor_entry) {
    CIM_NEED();
    BYTE a[128], x[128]; memset(a, 0xff, sizeof a); memset(x, 0, sizeof x);
    HCURSOR h = U(CreateCursor)(GetModuleHandleW(NULL), 1, 1, 32, 32, a, x);
    if (!h) { t_skip("CreateCursor failed err %lu", GetLastError()); return; }
    entry_ok(h);
    CHECK(U(DestroyCursor)(h));
}
TEST(cursor_icon_menu_icon_handle_is_sign_extended_int) {
    CIM_GUI();
    MKI(h, cim_mk_icon(16, 16, 0xff, 0));
    long long v = (long long)(ULONG_PTR)h;
    EXPECT_EQ(v, (long long)(int)(unsigned)(ULONG_PTR)h);
    CHECK(CIM_UNIQ(h) != 0);
    U(DestroyIcon)(h);
}
TEST(cursor_icon_menu_icon_createcursor_hotspot_validation) {
    CIM_GUI();
    BYTE a[128], x[128]; memset(a, 0xff, sizeof a); memset(x, 0, sizeof x);
    static const int hs[][2] = { {-1, 0}, {0, -1}, {33, 0}, {0, 33}, {100, 100} };
    for (unsigned i = 0; i < sizeof hs / sizeof *hs; i++) {
        HCURSOR h = U(CreateCursor)(GetModuleHandleW(NULL), hs[i][0], hs[i][1], 32, 32, a, x);
        CHECKF(h == NULL, "hotspot %d,%d accepted", hs[i][0], hs[i][1]); /* user32!CreateCursor: 0 <= hot <= size */
        if (h) U(DestroyCursor)(h);
    }
}
TEST(cursor_icon_menu_icon_gui_user_objects_exact) {
    CIM_GUI();
    DWORD base = cim_gr(GR_USEROBJECTS);
    HICON a = cim_mk_icon(16, 16, 0xff, 0), b = cim_mk_icon_ii(16, 16, 32), c = cim_mk_cursor_ii(16, 16, 1, 1);
    if (!a || !b || !c) { t_skip("create failed"); return; }
    EXPECT_EQ(cim_gr(GR_USEROBJECTS), base + 3);
    CHECK(U(DestroyIcon)(a)); CHECK(U(DestroyIcon)(b)); CHECK(U(DestroyIcon)(c));
    EXPECT_EQ(cim_gr(GR_USEROBJECTS), base);
}
TEST(cursor_icon_menu_icon_createiconindirect_leaves_no_gdi_leak) {
    CIM_GUI();
    DWORD base = cim_gr(GR_GDIOBJECTS);
    for (int i = 0; i < 20; i++) { HICON h = cim_mk_icon_ii(16, 16, 32); if (!h) { t_skip("create failed"); return; } U(DestroyIcon)(h); }
    EXPECT_EQ(cim_gr(GR_GDIOBJECTS), base); /* the cursor keeps kernel-side copies, no caller-visible GDI handles remain */
}
TEST(cursor_icon_menu_icon_many_icons_distinct) {
    CIM_NEED();
    enum { N = 200 };
    static HICON hs[N];
    int n = 0;
    for (; n < N; n++) { hs[n] = cim_mk_icon(16, 16, 0xff, 0); if (!hs[n]) break; }
    EXPECT_EQ(n, N);
    int bad = 0, dup = 0;
    for (int i = 0; i < n; i++) { if (!cim_entry_is(hs[i], CT_CURSOR)) bad++; for (int j = i + 1; j < n; j++) if (hs[i] == hs[j]) dup++; }
    EXPECT_EQ(bad, 0); EXPECT_EQ(dup, 0);
    for (int i = 0; i < n; i++) U(DestroyIcon)(hs[i]);
}

/* ---------------- destruction ---------------- */
TEST(cursor_icon_menu_icon_destroy_frees_entry) {
    CIM_NEED();
    MKI(h, cim_mk_icon(16, 16, 0xff, 0));
    cim_he before, after;
    CHECK(cim_he_read(h, &before));
    EXPECT_EQ(U(DestroyIcon)(h), 1);
    CHECK(cim_he_read(h, &after));
    if (after.type == 0) EXPECT_EQ(after.uniq, cim_next_uniq(before.uniq));
    CHECK(!cim_entry_is(h, CT_CURSOR));
    ICONINFO ii; BOOL r; CIM_CALL_ERR(r, U(GetIconInfo)(h, &ii));
    EXPECT_FAIL(r == 0, E_INVALID_CURSOR_HANDLE);
}
TEST(cursor_icon_menu_icon_double_destroy_fails) {
    CIM_GUI();
    MKI(h, cim_mk_icon(16, 16, 0xff, 0));
    CHECK(U(DestroyIcon)(h));
    BOOL r; CIM_CALL_ERR(r, U(DestroyIcon)(h));
    EXPECT_FAIL(r == 0, E_INVALID_CURSOR_HANDLE);
    CIM_CALL_ERR(r, U(DestroyCursor)((HCURSOR)h));
    EXPECT_FAIL(r == 0, E_INVALID_CURSOR_HANDLE);
}
TEST(cursor_icon_menu_icon_destroy_bad_handles) {
    CIM_GUI();
    HMENU m = U(CreateMenu)(); HWND w = cim_make_top();
    HANDLE bad[] = { NULL, (HANDLE)(ULONG_PTR)1, (HANDLE)(ULONG_PTR)0x7fff0003, m, w };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        if (i >= 3 && !bad[i]) continue;
        BOOL r; CIM_CALL_ERR(r, U(DestroyIcon)((HICON)bad[i]));
        CHECKF(r == 0 && GetLastError() == E_INVALID_CURSOR_HANDLE, "bad[%u] %p err %lu", i, bad[i], GetLastError());
    }
    if (m) { CHECK(U(IsMenu)(m)); U(DestroyMenu)(m); }
    if (w) { CHECK(U(IsWindow)(w)); U(DestroyWindow)(w); }
}
TEST(cursor_icon_menu_icon_nt_destroycursor_modes) {
    CIM_GUI(); REQUIRE(UN(NtUserDestroyCursor));
    MKI(h, cim_mk_icon(16, 16, 0xff, 0));
    BOOL r;
    static const ULONG bad[] = { 2, 3, 4, 0x100, 0xffffffff };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        CIM_CALL_ERR(r, UN(NtUserDestroyCursor)(h, bad[i]));
        CHECKF(r == 0 && GetLastError() == E_INVALID_PARAMETER, "mode %lu err %lu", bad[i], GetLastError()); /* NtUserDestroyCursor: mode not 0/1 -> 0x57 */
        ICONINFO ii; CHECKF(U(GetIconInfo)(h, &ii), "mode %lu destroyed the icon", bad[i]);
        free_info(&ii);
    }
    EXPECT_EQ(UN(NtUserDestroyCursor)(h, 0), 1); /* owner == current process and the object is its own list head */
    ICONINFO ii; CIM_CALL_ERR(r, U(GetIconInfo)(h, &ii));
    EXPECT_FAIL(r == 0, E_INVALID_CURSOR_HANDLE);
}
TEST(cursor_icon_menu_icon_nt_destroycursor_mode1) {
    CIM_GUI(); REQUIRE(UN(NtUserDestroyCursor));
    MKI(h, cim_mk_cursor_ii(16, 16, 0, 0));
    EXPECT_EQ(UN(NtUserDestroyCursor)(h, 1), 1);
    BOOL r; ICONINFO ii; CIM_CALL_ERR(r, U(GetIconInfo)(h, &ii));
    EXPECT_FAIL(r == 0, E_INVALID_CURSOR_HANDLE);
}
TEST(cursor_icon_menu_icon_nt_destroycursor_wrong_type_handle) {
    CIM_GUI(); REQUIRE(UN(NtUserDestroyCursor));
    HMENU m = U(CreateMenu)(); if (!m) { t_skip("no menu"); return; }
    BOOL r; CIM_CALL_ERR(r, UN(NtUserDestroyCursor)(m, 1));
    EXPECT_FAIL(r == 0, E_INVALID_CURSOR_HANDLE);
    CIM_CALL_ERR(r, UN(NtUserDestroyCursor)(m, 0));
    EXPECT_FAIL(r == 0, E_INVALID_CURSOR_HANDLE);
    CHECK(U(IsMenu)(m));
    U(DestroyMenu)(m);
}
TEST(cursor_icon_menu_icon_destroy_cursor_in_use_by_thread) {
    CIM_NEED();
    MKI(h, cim_mk_cursor_ii(16, 16, 0, 0));
    HCURSOR prev = U(SetCursor)(h);
    EXPECT_EQ(U(GetCursor)(), (HCURSOR)h);
    /* _DestroyCursor -> FixupGlobalCursor / gpcurLogCurrent handling: destroying the current cursor must succeed */
    BOOL r; CIM_CALL_ERR(r, U(DestroyCursor)((HCURSOR)h));
    /* Windows 10: destroying the cursor the thread currently uses reports FALSE without setting an error,
     * yet the object is marked destroyed at once (checked below) */
    CHECK(!r);
    EXPECT_EQ(GetLastError(), CIM_SENTINEL);
    HCURSOR now = U(GetCursor)();
    t_note("GetCursor after destroying the current cursor: %p (destroyed %p, previous %p)", now, h, prev);
    U(SetCursor)(prev);
    /* destroying the object sets the destroyed bit at once even if the queue still holds a lock: the handle no longer validates */
    CHECK(!cim_entry_is(h, CT_CURSOR));
    ICONINFO ii; BOOL gi; CIM_CALL_ERR(gi, U(GetIconInfo)(h, &ii));
    EXPECT_FAIL(gi == 0, E_INVALID_CURSOR_HANDLE);
}
TEST(cursor_icon_menu_nt_create_empty_cursor_object) {
    CIM_NEED(); REQUIRE(UN(NtUserCreateEmptyCursorObject));
    DWORD base = cim_gr(GR_USEROBJECTS);
    HANDLE h = UN(NtUserCreateEmptyCursorObject)(0);
    if (!h) { t_skip("returned NULL err %lu", GetLastError()); return; }
    entry_ok(h);
    EXPECT_EQ(cim_gr(GR_USEROBJECTS), base + 1);
    REQUIRE(UN(NtUserDestroyCursor));
    EXPECT_EQ(UN(NtUserDestroyCursor)(h, 0), 1);
    EXPECT_EQ(cim_gr(GR_USEROBJECTS), base);
}
static void setdata_args(UNICODE_STRING *mod, UNICODE_STRING *res, WCHAR *mb, WCHAR *rb, BYTE *cd, DWORD flags) {
    memset(cd, 0, 0x88);
    memcpy(cd + 0x18, &flags, 4);
    mod->Length = 0; mod->MaximumLength = 16 * sizeof(WCHAR); mod->Buffer = mb;
    res->Length = 0; res->MaximumLength = 16 * sizeof(WCHAR); res->Buffer = rb;
}
TEST(cursor_icon_menu_nt_setcursoriconcdata_on_finished_icon_returns_zero) {
    CIM_GUI(); REQUIRE(UN(NtUserSetCursorIconData));
    MKI(h, cim_mk_icon(16, 16, 0xff, 0));
    UNICODE_STRING mod, res; WCHAR mb[16] = {0}, rb[16] = {0}; BYTE cd[0x88];
    setdata_args(&mod, &res, mb, rb, cd, 0);
    ULONG r; CIM_CALL_ERR(r, UN(NtUserSetCursorIconData)(h, &mod, &res, cd));
    /* only an object still carrying the "empty" flag (0x800) may be filled: otherwise UserSetLastError(0), return 0 */
    EXPECT_EQ(r, 0);
    EXPECT_EQ(GetLastError(), 0);
    ICONINFO ii; CHECK(U(GetIconInfo)(h, &ii)); free_info(&ii);
    U(DestroyIcon)(h);
}
TEST(cursor_icon_menu_nt_setcursoriconcdata_bad_handle) {
    CIM_GUI(); REQUIRE(UN(NtUserSetCursorIconData));
    UNICODE_STRING mod, res; WCHAR mb[16] = {0}, rb[16] = {0}; BYTE cd[0x88];
    setdata_args(&mod, &res, mb, rb, cd, 0);
    HICON dead = cim_mk_icon(16, 16, 0xff, 0); if (!dead) { t_skip("no icon"); return; }
    U(DestroyIcon)(dead);
    HANDLE hs[] = { NULL, dead, (HANDLE)(ULONG_PTR)0x1234 };
    for (unsigned i = 0; i < 3; i++) {
        ULONG r; CIM_CALL_ERR(r, UN(NtUserSetCursorIconData)(hs[i], &mod, &res, cd));
        CHECKF(r == 0 && GetLastError() == E_INVALID_CURSOR_HANDLE, "case %u err %lu", i, GetLastError());
    }
}
TEST(cursor_icon_menu_nt_setcursoriconcdata_invalid_flag_bits) {
    CIM_NEED(); REQUIRE(UN(NtUserSetCursorIconData)); REQUIRE(UN(NtUserCreateEmptyCursorObject)); REQUIRE(UN(NtUserDestroyCursor));
    HANDLE h = UN(NtUserCreateEmptyCursorObject)(0);
    if (!h) { t_skip("no empty cursor"); return; }
    UNICODE_STRING mod, res; WCHAR mb[16] = {0}, rb[16] = {0}; BYTE cd[0x88];
    static const DWORD flags[] = { 0x20, 0x10000, 0x80000000, 0x2000 };
    for (unsigned i = 0; i < sizeof flags / sizeof *flags; i++) {
        setdata_args(&mod, &res, mb, rb, cd, flags[i]);
        ULONG r; CIM_CALL_ERR(r, UN(NtUserSetCursorIconData)(h, &mod, &res, cd));
        CHECKF(r == 0 && GetLastError() == E_INVALID_FLAGS, "flags 0x%lx err %lu", flags[i], GetLastError()); /* fFlags & 0xffffefb0 -> 0x3ec */
    }
    CHECK(cim_entry_is(h, CT_CURSOR));
    EXPECT_EQ(UN(NtUserDestroyCursor)(h, 0), 1);
}

/* ---------------- GetIconInfo / GetIconSize ---------------- */
TEST(cursor_icon_menu_icon_geticoninfo_cursor_fields) {
    CIM_GUI();
    MKI(h, cim_mk_cursor_ii(16, 16, 3, 5));
    ICONINFO ii; memset(&ii, 0xcc, sizeof ii);
    CHECK(U(GetIconInfo)(h, &ii));
    EXPECT_EQ(ii.fIcon, 0);   /* rt == 1 (RT_CURSOR) -> fIcon false */
    int cx = 0, cy = 0;
    if (UN(NtUserGetIconSize) && UN(NtUserGetIconSize)(h, 0, &cx, &cy) && cx == 16) { EXPECT_EQ(ii.xHotspot, 3); EXPECT_EQ(ii.yHotspot, 5); }
    else t_note("icon is scaled (cx=%d), hotspot not compared", cx);
    CHECK(ii.hbmMask && ii.hbmColor);
    free_info(&ii);
    U(DestroyIcon)(h);
}
TEST(cursor_icon_menu_icon_geticoninfo_icon_fields) {
    CIM_GUI();
    MKI(h, cim_mk_icon_ii(16, 16, 32));
    ICONINFO ii;
    CHECK(U(GetIconInfo)(h, &ii));
    EXPECT_EQ(ii.fIcon, 1);
    int cx = 0, cy = 0;
    if (UN(NtUserGetIconSize) && UN(NtUserGetIconSize)(h, 0, &cx, &cy) && cx == 16) { EXPECT_EQ(ii.xHotspot, 8); EXPECT_EQ(ii.yHotspot, 8); } /* CreateIconIndirect: icon hotspot = centre */
    free_info(&ii);
    U(DestroyIcon)(h);
}
TEST(cursor_icon_menu_icon_geticoninfo_bitmaps_are_fresh_gdi_objects) {
    CIM_NEED();
    MKI(h, cim_mk_icon_ii(16, 16, 32));
    DWORD g0 = cim_gr(GR_GDIOBJECTS);
    ICONINFO a, b;
    CHECK(U(GetIconInfo)(h, &a));
    CHECK(U(GetIconInfo)(h, &b));
    CHECK(a.hbmMask != b.hbmMask); CHECK(a.hbmColor != b.hbmColor);
    CHECK(a.hbmMask != a.hbmColor);
    EXPECT_EQ(cim_gr(GR_GDIOBJECTS), g0 + 4); /* GetIconInfo creates two bitmaps per call */
    CHECK(cim_gdi_bitmap_ok(a.hbmMask)); CHECK(cim_gdi_bitmap_ok(a.hbmColor));
    CHECK(cim_gdi_bitmap_ok(b.hbmMask)); CHECK(cim_gdi_bitmap_ok(b.hbmColor));
    free_info(&a); free_info(&b);
    EXPECT_EQ(cim_gr(GR_GDIOBJECTS), g0);
    U(DestroyIcon)(h);
}
TEST(cursor_icon_menu_icon_geticoninfo_bitmap_geometry) {
    CIM_GUI();
    static const int dims[][2] = { {16, 16}, {32, 32}, {24, 24}, {48, 32}, {8, 8} };
    for (unsigned i = 0; i < sizeof dims / sizeof *dims; i++) {
        int w = dims[i][0], hh = dims[i][1];
        HICON h = cim_mk_icon_ii(w, hh, 32);
        if (!h) { t_note("cannot create %dx%d icon", w, hh); continue; }
        ICONINFO ii; BITMAP bmm, bmc;
        CHECK(U(GetIconInfo)(h, &ii));
        CHECK(bm_dim(ii.hbmMask, &bmm)); CHECK(bm_dim(ii.hbmColor, &bmc));
        EXPECT_EQ(bmm.bmBitsPixel, 1);                /* a 1-bpp bitmap of the icon size */
        EXPECT_EQ(bmc.bmBitsPixel, 32);               /* 32 bpp source -> DIB created with biBitCount 0x20 */
        EXPECT_EQ(bmm.bmWidth, bmc.bmWidth);
        EXPECT_EQ(bmm.bmHeight, bmc.bmHeight);        /* colour present: mask height is cy/2, colour height cy/2 */
        int cx = 0, cy = 0;
        if (UN(NtUserGetIconSize) && UN(NtUserGetIconSize)(h, 0, &cx, &cy)) {
            EXPECT_EQ(cx, bmc.bmWidth);
            EXPECT_EQ(cy, 2 * bmc.bmHeight);          /* cursor object keeps AND+XOR stacked: height is doubled */
            if (cx == w) EXPECT_EQ(bmc.bmHeight, hh);
        }
        free_info(&ii);
        U(DestroyIcon)(h);
    }
}
TEST(cursor_icon_menu_icon_geticoninfo_cell_in_gdi_table) {
    CIM_NEED();
    MKI(h, cim_mk_icon_ii(16, 16, 32));
    ICONINFO ii; CHECK(U(GetIconInfo)(h, &ii));
    void *base = NULL;
    if (!k_try_read_ptr((char *)k_peb() + PEB_GdiSharedHandleTable, &base) || !base) { free_info(&ii); U(DestroyIcon)(h); t_skip("no GDI table"); return; }
    HBITMAP bs[2] = { ii.hbmMask, ii.hbmColor };
    for (int i = 0; i < 2; i++) {
        unsigned char objt = 0;
        CHECK(k_try_read_u8((char *)base + (size_t)((ULONG_PTR)bs[i] & 0xffff) * GDI_CELL_SIZE + 0xe, &objt));
        EXPECT_EQ(objt, 5);                                     /* SURF_TYPE: the cell's Objt of a bitmap */
        EXPECT_EQ((unsigned)(((ULONG_PTR)bs[i] >> 16) & 0x7f), 5);
    }
    free_info(&ii);
    U(DestroyIcon)(h);
}
TEST(cursor_icon_menu_icon_geticoninfo_mono_icon_dims) {
    CIM_GUI();
    MKI(h, cim_mk_icon(32, 32, 0xff, 0));
    ICONINFO ii; BITMAP bm;
    CHECK(U(GetIconInfo)(h, &ii));
    CHECK(ii.hbmMask);
    if (ii.hbmMask && bm_dim(ii.hbmMask, &bm)) {
        EXPECT_EQ(bm.bmBitsPixel, 1);
        EXPECT_EQ(bm.bmWidth, 32);
        t_note("mono icon: mask %ldx%ld, colour bitmap %s", (long)bm.bmWidth, (long)bm.bmHeight, ii.hbmColor ? "present" : "absent");
    }
    free_info(&ii);
    U(DestroyIcon)(h);
}
TEST(cursor_icon_menu_icon_geticoninfo_invalid_handles) {
    CIM_GUI(); REQUIRE(UN(NtUserGetIconInfo));
    HMENU m = U(CreateMenu)(); HWND w = cim_make_top();
    HICON dead = cim_mk_icon(16, 16, 0xff, 0); if (dead) U(DestroyIcon)(dead);
    HANDLE bad[] = { NULL, dead, (HANDLE)(ULONG_PTR)0x1234, m, w };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        if (i >= 3 && !bad[i]) continue;
        ICONINFO ii; memset(&ii, 0, sizeof ii);
        BOOL r; CIM_CALL_ERR(r, UN(NtUserGetIconInfo)(bad[i], &ii, NULL, NULL, NULL, 0));
        CHECKF(r == 0 && GetLastError() == E_INVALID_CURSOR_HANDLE, "bad[%u] err %lu", i, GetLastError());
        CHECKF(ii.hbmMask == NULL && ii.hbmColor == NULL, "bad[%u] produced bitmaps", i);
    }
    if (m) U(DestroyMenu)(m);
    if (w) U(DestroyWindow)(w);
}
TEST(cursor_icon_menu_icon_nt_geticoninfo_direct) {
    CIM_GUI(); REQUIRE(UN(NtUserGetIconInfo));
    MKI(h, cim_mk_icon_ii(16, 16, 32));
    ICONINFO ii; memset(&ii, 0, sizeof ii);
    DWORD bpp = 0;
    EXPECT_EQ(UN(NtUserGetIconInfo)(h, &ii, NULL, NULL, &bpp, 0), 1);
    EXPECT_EQ(ii.fIcon, 1);
    CHECK(ii.hbmMask && ii.hbmColor);
    EXPECT_EQ(bpp, 32);
    free_info(&ii);
    U(DestroyIcon)(h);
}
TEST(cursor_icon_menu_icon_nt_geticoninfo_names_empty_for_created_icon) {
    CIM_GUI(); REQUIRE(UN(NtUserGetIconInfo));
    MKI(h, cim_mk_icon_ii(16, 16, 32));
    WCHAR mb[64], rb[64]; wmemset(mb, 0x7777, 64); wmemset(rb, 0x7777, 64);
    UNICODE_STRING mod = { 0, 63 * 2, mb }, res = { 0, 63 * 2, rb };
    ICONINFO ii;
    CHECK(UN(NtUserGetIconInfo)(h, &ii, &mod, &res, NULL, 0));
    EXPECT_EQ(mod.Length, 0);    /* no atom for the module name of a runtime-created icon */
    EXPECT_EQ((ULONG_PTR)res.Buffer, 0); /* resource name slot is an integer id (0) */
    free_info(&ii);
    U(DestroyIcon)(h);
}
TEST(cursor_icon_menu_icon_geticoninfoex_cbsize) {
    CIM_GUI();
    MKI(h, cim_mk_icon_ii(16, 16, 32));
    ICONINFOEXW ex;
    static const DWORD sizes[] = { 0, 4, sizeof(ICONINFO), 0x437, 0x439, 0x500 };
    for (unsigned i = 0; i < sizeof sizes / sizeof *sizes; i++) {
        memset(&ex, 0, sizeof ex); ex.cbSize = sizes[i];
        BOOL r; CIM_CALL_ERR(r, U(GetIconInfoExW)(h, &ex));
        CHECKF(r == 0 && GetLastError() == E_INVALID_PARAMETER, "cbSize 0x%lx err %lu", sizes[i], GetLastError());
    }
    EXPECT_EQ(sizeof(ICONINFOEXW), 0x438);
    U(DestroyIcon)(h);
}
TEST(cursor_icon_menu_icon_geticoninfoex_created_icon_names_empty) {
    CIM_GUI();
    MKI(h, cim_mk_icon_ii(16, 16, 32));
    ICONINFOEXW ex; memset(&ex, 0xee, sizeof ex); ex.cbSize = sizeof ex;
    CHECK(U(GetIconInfoExW)(h, &ex));
    EXPECT_EQ(ex.wResID, 0);
    EXPECT_EQ(ex.szModName[0], 0);
    EXPECT_EQ(ex.szResName[0], 0);
    EXPECT_EQ(ex.fIcon, 1);
    free_info((ICONINFO *)&ex.fIcon); /* first five members of ICONINFOEXW after cbSize mirror ICONINFO */
    U(DestroyIcon)(h);
}
TEST(cursor_icon_menu_icon_geticoninfoex_invalid_handle) {
    CIM_GUI();
    HICON dead = cim_mk_icon(16, 16, 0xff, 0); if (!dead) { t_skip("no icon"); return; }
    U(DestroyIcon)(dead);
    ICONINFOEXW ex; memset(&ex, 0, sizeof ex); ex.cbSize = sizeof ex;
    BOOL r; CIM_CALL_ERR(r, U(GetIconInfoExW)(dead, &ex));
    CHECK(r == 0);
}
TEST(cursor_icon_menu_icon_geticonsize_consistent_with_info) {
    CIM_GUI(); REQUIRE(UN(NtUserGetIconSize));
    MKI(h, cim_mk_icon_ii(24, 24, 32));
    int cx = -1, cy = -1;
    EXPECT_EQ(UN(NtUserGetIconSize)(h, 0, &cx, &cy), 1);
    CHECK(cx > 0 && cy > 0);
    EXPECT_EQ(cy % 2, 0);
    U(DestroyIcon)(h);
}
TEST(cursor_icon_menu_icon_geticonsize_invalid_handle_leaves_outputs) {
    CIM_GUI(); REQUIRE(UN(NtUserGetIconSize));
    HICON dead = cim_mk_icon(16, 16, 0xff, 0); if (!dead) { t_skip("no icon"); return; }
    U(DestroyIcon)(dead);
    int cx = 0x1111, cy = 0x2222;
    BOOL r; CIM_CALL_ERR(r, UN(NtUserGetIconSize)(dead, 0, &cx, &cy));
    EXPECT_EQ(r, 0);
    EXPECT_EQ(cx, 0x1111); EXPECT_EQ(cy, 0x2222); /* outputs are only written on success */
}

/* ---------------- CopyIcon / CopyImage ---------------- */
TEST(cursor_icon_menu_icon_copyicon_new_object) {
    CIM_NEED();
    MKI(h, cim_mk_icon_ii(16, 16, 32));
    HICON c = U(CopyIcon)(h);
    if (!c) { U(DestroyIcon)(h); t_skip("CopyIcon failed err %lu", GetLastError()); return; }
    CHECK(c != h);
    entry_ok(c);
    ICONINFO a, b; BITMAP ba, bb;
    CHECK(U(GetIconInfo)(h, &a)); CHECK(U(GetIconInfo)(c, &b));
    EXPECT_EQ(a.fIcon, b.fIcon);
    if (bm_dim(a.hbmColor, &ba) && bm_dim(b.hbmColor, &bb)) { EXPECT_EQ(ba.bmWidth, bb.bmWidth); EXPECT_EQ(ba.bmHeight, bb.bmHeight); }
    free_info(&a); free_info(&b);
    CHECK(U(DestroyIcon)(c));
    CHECK(cim_entry_is(h, CT_CURSOR)); /* destroying the copy leaves the original */
    U(DestroyIcon)(h);
}
TEST(cursor_icon_menu_icon_copyicon_cursor_keeps_hotspot) {
    CIM_GUI();
    MKI(h, cim_mk_cursor_ii(16, 16, 2, 7));
    HICON c = U(CopyIcon)(h);
    if (!c) { U(DestroyIcon)(h); t_skip("CopyIcon failed"); return; }
    ICONINFO a, b;
    CHECK(U(GetIconInfo)(h, &a)); CHECK(U(GetIconInfo)(c, &b));
    EXPECT_EQ(b.fIcon, 0);
    EXPECT_EQ(b.xHotspot, a.xHotspot); EXPECT_EQ(b.yHotspot, a.yHotspot);
    free_info(&a); free_info(&b);
    U(DestroyIcon)(c); U(DestroyIcon)(h);
}
TEST(cursor_icon_menu_icon_copyicon_invalid) {
    CIM_GUI();
    HICON dead = cim_mk_icon(16, 16, 0xff, 0); if (!dead) { t_skip("no icon"); return; }
    U(DestroyIcon)(dead);
    EXPECT_EQ(U(CopyIcon)(dead), NULL);
    EXPECT_EQ(U(CopyIcon)(NULL), NULL);
}
TEST(cursor_icon_menu_icon_copyimage_invalid_flags_table) {
    CIM_GUI();
    MKI(h, cim_mk_icon_ii(16, 16, 32));
    static const UINT bad[] = { 0x100, 0x200, 0x400, 0x20000, 0x40000, 0x1000000, 0x80000000, 0xfffe0000 };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        HANDLE c; CIM_CALL_ERR(c, U(CopyImage)(h, IMAGE_ICON, 0, 0, bad[i]));
        CHECKF(c == NULL && GetLastError() == E_INVALID_PARAMETER, "flags 0x%x err %lu", bad[i], GetLastError()); /* user32!CopyImage: flags & 0xfffe0700 -> 0x57 */
        if (c) U(DestroyIcon)(c);
    }
    U(DestroyIcon)(h);
}
TEST(cursor_icon_menu_icon_copyimage_new_handle) {
    CIM_NEED();
    MKI(h, cim_mk_icon_ii(16, 16, 32));
    HANDLE c = U(CopyImage)(h, IMAGE_ICON, 0, 0, 0);
    if (!c) { U(DestroyIcon)(h); t_skip("CopyImage failed err %lu", GetLastError()); return; }
    CHECK(c != (HANDLE)h);
    entry_ok(c);
    U(DestroyIcon)(c); U(DestroyIcon)(h);
}
TEST(cursor_icon_menu_icon_copyimage_copydeleteorg_destroys_original) {
    CIM_NEED();
    MKI(h, cim_mk_icon_ii(16, 16, 32));
    HANDLE c = U(CopyImage)(h, IMAGE_ICON, 0, 0, LR_COPYDELETEORG);
    if (!c) { U(DestroyIcon)(h); t_skip("CopyImage failed err %lu", GetLastError()); return; }
    CHECK(cim_entry_is(c, CT_CURSOR));
    if (c != (HANDLE)h) CHECK(!cim_entry_is(h, CT_CURSOR));
    else t_note("LR_COPYDELETEORG returned the original handle");
    U(DestroyIcon)(c);
}
TEST(cursor_icon_menu_icon_copyimage_returnorg_same_size) {
    CIM_GUI();
    MKI(h, cim_mk_icon_ii(16, 16, 32));
    HANDLE c = U(CopyImage)(h, IMAGE_ICON, 0, 0, LR_COPYRETURNORG);
    if (!c) { U(DestroyIcon)(h); t_skip("CopyImage failed"); return; }
    t_note("LR_COPYRETURNORG same size: %s", c == (HANDLE)h ? "original handle" : "new handle");
    if (c != (HANDLE)h) U(DestroyIcon)(c);
    U(DestroyIcon)(h);
}
TEST(cursor_icon_menu_icon_copyimage_resize) {
    CIM_GUI();
    MKI(h, cim_mk_icon_ii(16, 16, 32));
    HANDLE c = U(CopyImage)(h, IMAGE_ICON, 32, 32, 0);
    if (!c) { U(DestroyIcon)(h); t_skip("CopyImage failed err %lu", GetLastError()); return; }
    int cx = 0, cy = 0;
    if (UN(NtUserGetIconSize) && UN(NtUserGetIconSize)(c, 0, &cx, &cy)) t_note("resized copy: cx=%d cy=%d", cx, cy);
    CHECK(c != (HANDLE)h);
    U(DestroyIcon)(c); U(DestroyIcon)(h);
}
TEST(cursor_icon_menu_icon_copyimage_cursor_from_icon_type_mismatch) {
    CIM_GUI();
    MKI(h, cim_mk_icon_ii(16, 16, 32));
    HANDLE c = U(CopyImage)(h, IMAGE_CURSOR, 0, 0, 0);
    t_note("CopyImage(icon, IMAGE_CURSOR) -> %p err %lu", c, GetLastError());
    if (c && c != (HANDLE)h) U(DestroyIcon)(c);
    U(DestroyIcon)(h);
}

/* ---------------- system / shared icons and cursors ---------------- */
static const struct { const char *n; LPCWSTR id; } s_cur[] = {
    {"arrow", MAKEINTRESOURCEW(32512)}, {"ibeam", MAKEINTRESOURCEW(32513)}, {"wait", MAKEINTRESOURCEW(32514)}, {"cross", MAKEINTRESOURCEW(32515)}, {"uparrow", MAKEINTRESOURCEW(32516)},
    {"sizenwse", MAKEINTRESOURCEW(32642)}, {"sizenesw", MAKEINTRESOURCEW(32643)}, {"sizewe", MAKEINTRESOURCEW(32644)}, {"sizens", MAKEINTRESOURCEW(32645)},
    {"sizeall", MAKEINTRESOURCEW(32646)}, {"no", MAKEINTRESOURCEW(32648)}, {"hand", MAKEINTRESOURCEW(32649)}, {"appstarting", MAKEINTRESOURCEW(32650)}, {"help", MAKEINTRESOURCEW(32651)},
};
static const struct { const char *n; LPCWSTR id; } s_ico[] = {
    {"application", MAKEINTRESOURCEW(32512)}, {"hand", MAKEINTRESOURCEW(32513)}, {"question", MAKEINTRESOURCEW(32514)}, {"exclamation", MAKEINTRESOURCEW(32515)},
    {"asterisk", MAKEINTRESOURCEW(32516)}, {"winlogo", MAKEINTRESOURCEW(32517)}, {"shield", MAKEINTRESOURCEW(32518)},
};
TEST(cursor_icon_menu_shared_system_cursors_are_cursor_objects) {
    CIM_NEED();
    for (unsigned i = 0; i < sizeof s_cur / sizeof *s_cur; i++) {
        HCURSOR c = U(LoadCursorW)(NULL, s_cur[i].id);
        if (!c) { t_note("cursor %s not available err %lu", s_cur[i].n, GetLastError()); continue; }
        CHECKF(cim_entry_is(c, CT_CURSOR), "%s %p", s_cur[i].n, c);
        ICONINFO ii;
        if (U(GetIconInfo)(c, &ii)) { CHECKF(ii.fIcon == 0, "%s fIcon", s_cur[i].n); free_info(&ii); }
        else CHECKF(0, "%s GetIconInfo err %lu", s_cur[i].n, GetLastError());
    }
}
TEST(cursor_icon_menu_shared_system_icons_are_cursor_objects) {
    CIM_NEED();
    for (unsigned i = 0; i < sizeof s_ico / sizeof *s_ico; i++) {
        HICON c = U(LoadIconW)(NULL, s_ico[i].id);
        if (!c) { t_note("icon %s not available err %lu", s_ico[i].n, GetLastError()); continue; }
        CHECKF(cim_entry_is(c, CT_CURSOR), "%s %p", s_ico[i].n, c);
        ICONINFO ii;
        if (U(GetIconInfo)(c, &ii)) { CHECKF(ii.fIcon == 1, "%s fIcon", s_ico[i].n); free_info(&ii); }
        else CHECKF(0, "%s GetIconInfo err %lu", s_ico[i].n, GetLastError());
    }
}
TEST(cursor_icon_menu_shared_loadcursor_returns_same_handle) {
    CIM_GUI();
    for (unsigned i = 0; i < sizeof s_cur / sizeof *s_cur; i++) {
        HCURSOR a = U(LoadCursorW)(NULL, s_cur[i].id), b = U(LoadCursorW)(NULL, s_cur[i].id);
        CHECKF(a == b, "%s: %p vs %p", s_cur[i].n, a, b); /* LoadIcoCur passes LR_SHARED (0x8000) -> FindExistingCursorIcon */
    }
}
TEST(cursor_icon_menu_shared_loadicon_returns_same_handle) {
    CIM_GUI();
    for (unsigned i = 0; i < sizeof s_ico / sizeof *s_ico; i++) {
        HICON a = U(LoadIconW)(NULL, s_ico[i].id), b = U(LoadIconW)(NULL, s_ico[i].id);
        CHECKF(a == b, "%s: %p vs %p", s_ico[i].n, a, b);
    }
}
TEST(cursor_icon_menu_shared_distinct_cursors_distinct_handles) {
    CIM_GUI();
    HCURSOR a = U(LoadCursorW)(NULL, MAKEINTRESOURCEW(32512)), b = U(LoadCursorW)(NULL, MAKEINTRESOURCEW(32513)), c = U(LoadCursorW)(NULL, MAKEINTRESOURCEW(32514));
    if (!a || !b || !c) { t_skip("system cursor missing"); return; }
    CHECK(a != b && b != c && a != c);
}
TEST(cursor_icon_menu_shared_loadimage_shared_flag_same_handle) {
    CIM_GUI();
    HANDLE a = U(LoadImageW)(NULL, MAKEINTRESOURCEW(32512), IMAGE_ICON, 0, 0, LR_SHARED | LR_DEFAULTSIZE);
    HANDLE b = U(LoadImageW)(NULL, MAKEINTRESOURCEW(32512), IMAGE_ICON, 0, 0, LR_SHARED | LR_DEFAULTSIZE);
    if (!a || !b) { t_skip("LoadImage failed err %lu", GetLastError()); return; }
    EXPECT_EQ(a, b);
}
TEST(cursor_icon_menu_shared_destroy_does_not_free) {
    CIM_NEED();
    HICON c = U(LoadIconW)(NULL, MAKEINTRESOURCEW(32512));
    if (!c) { t_skip("no system icon"); return; }
    BOOL r = U(DestroyIcon)(c);
    t_note("DestroyIcon(shared system icon) -> %d", r);
    /* _DestroyCursor(mode 1) leaves objects with owner 0 or the LRSHARED/system flags (0x84) untouched */
    CHECK(cim_entry_is(c, CT_CURSOR));
    ICONINFO ii; CHECK(U(GetIconInfo)(c, &ii)); free_info(&ii);
    HICON again = U(LoadIconW)(NULL, MAKEINTRESOURCEW(32512));
    EXPECT_EQ(again, c);
}
TEST(cursor_icon_menu_shared_destroycursor_system_cursor_survives) {
    CIM_NEED();
    HCURSOR c = U(LoadCursorW)(NULL, MAKEINTRESOURCEW(32512));
    if (!c) { t_skip("no system cursor"); return; }
    BOOL r = U(DestroyCursor)(c);
    t_note("DestroyCursor(MAKEINTRESOURCEW(32512)) -> %d", r);
    CHECK(cim_entry_is(c, CT_CURSOR));
}
TEST(cursor_icon_menu_shared_system_cursor_not_counted_as_owned_leak) {
    CIM_GUI();
    HCURSOR c = U(LoadCursorW)(NULL, MAKEINTRESOURCEW(32515));
    if (!c) { t_skip("no cursor"); return; }
    DWORD base = cim_gr(GR_USEROBJECTS);
    for (int i = 0; i < 50; i++) U(LoadCursorW)(NULL, MAKEINTRESOURCEW(32515));
    EXPECT_EQ(cim_gr(GR_USEROBJECTS), base); /* repeated shared loads reuse the cached object */
}
TEST(cursor_icon_menu_shared_geticoninfoex_resource_ids) {
    CIM_GUI();
    HICON c = U(LoadIconW)(NULL, MAKEINTRESOURCEW(32512));
    if (!c) { t_skip("no system icon"); return; }
    ICONINFOEXW ex; memset(&ex, 0, sizeof ex); ex.cbSize = sizeof ex;
    if (U(GetIconInfoExW)(c, &ex)) {
        t_note("MAKEINTRESOURCEW(32512): wResID=%u szModName=%ls szResName=%ls", ex.wResID, ex.szModName, ex.szResName);
        CHECK(ex.fIcon == 1);
        free_info((ICONINFO *)&ex.fIcon);
    } else CHECKF(0, "GetIconInfoExW err %lu", GetLastError());
}

/* ---------------- DrawIconEx into a DIB section (kernel draws, we read the mapped section) ---------------- */
struct dib { HDC dc; HBITMAP bmp; HGDIOBJ old; DWORD *bits; int w, h; };
static int dib_make(struct dib *d, int w, int h, DWORD fill) {
    BITMAPINFO bi; memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    d->w = w; d->h = h; d->bits = NULL;
    d->dc = U(CreateCompatibleDC)(NULL);
    if (!d->dc) return 0;
    d->bmp = U(CreateDIBSection)(d->dc, &bi, DIB_RGB_COLORS, (void **)&d->bits, NULL, 0);
    if (!d->bmp || !d->bits) { U(DeleteDC)(d->dc); return 0; }
    d->old = U(SelectObject)(d->dc, d->bmp);
    for (int i = 0; i < w * h; i++) d->bits[i] = fill;
    U(GdiFlush)();
    return 1;
}
static void dib_free(struct dib *d) { U(SelectObject)(d->dc, d->old); U(DeleteObject)(d->bmp); U(DeleteDC)(d->dc); }

static void draw_case(BYTE and_fill, BYTE xor_fill, DWORD bg, DWORD want) {
    struct dib d;
    HICON ic = cim_mk_icon(16, 16, and_fill, xor_fill);
    if (!ic) { t_skip("no icon"); return; }
    if (!dib_make(&d, 32, 32, bg)) { U(DestroyIcon)(ic); t_skip("no DIB section"); return; }
    BOOL r = U(DrawIconEx)(d.dc, 4, 4, ic, 16, 16, 0, NULL, DI_NORMAL);
    U(GdiFlush)();
    EXPECT_EQ(r, 1);
    DWORD in = d.bits[12 * 32 + 12] & 0xffffff, out = d.bits[0] & 0xffffff, edge = d.bits[20 * 32 + 20] & 0xffffff;
    CHECKF(in == want, "pixel inside = 0x%06lx want 0x%06lx (and=%02x xor=%02x)", in, want, and_fill, xor_fill);
    EXPECT_EQ(out, bg & 0xffffff);   /* outside the icon rectangle nothing is touched */
    EXPECT_EQ(edge, bg & 0xffffff);
    dib_free(&d); U(DestroyIcon)(ic);
}
TEST(cursor_icon_menu_draw_mono_opaque_white) { CIM_GUI(); draw_case(0x00, 0xff, 0x808080, 0xffffff); }
TEST(cursor_icon_menu_draw_mono_opaque_black) { CIM_GUI(); draw_case(0x00, 0x00, 0x808080, 0x000000); }
TEST(cursor_icon_menu_draw_mono_transparent) { CIM_GUI(); draw_case(0xff, 0x00, 0x808080, 0x808080); }
TEST(cursor_icon_menu_draw_mono_invert) { CIM_GUI(); draw_case(0xff, 0xff, 0x808080, 0x7f7f7f); }
TEST(cursor_icon_menu_draw_mono_transparent_other_bg) { CIM_GUI(); draw_case(0xff, 0x00, 0x123456, 0x123456); }
TEST(cursor_icon_menu_draw_mono_invert_other_bg) { CIM_GUI(); draw_case(0xff, 0xff, 0x123456, 0xedcba9); }
TEST(cursor_icon_menu_draw_di_mask_only) {
    CIM_GUI();
    struct dib d; HICON ic = cim_mk_icon(16, 16, 0x00, 0xff);
    if (!ic) { t_skip("no icon"); return; }
    if (!dib_make(&d, 32, 32, 0x808080)) { U(DestroyIcon)(ic); t_skip("no DIB"); return; }
    BOOL r = U(DrawIconEx)(d.dc, 0, 0, ic, 16, 16, 0, NULL, DI_MASK);
    U(GdiFlush)();
    EXPECT_EQ(r, 1);
    EXPECT_EQ(d.bits[8 * 32 + 8] & 0xffffff, 0x000000); /* DI_MASK: dest AND mask(0) */
    dib_free(&d); U(DestroyIcon)(ic);
}
TEST(cursor_icon_menu_draw_di_image_only) {
    CIM_GUI();
    struct dib d; HICON ic = cim_mk_icon(16, 16, 0xff, 0xff);
    if (!ic) { t_skip("no icon"); return; }
    if (!dib_make(&d, 32, 32, 0x808080)) { U(DestroyIcon)(ic); t_skip("no DIB"); return; }
    BOOL r = U(DrawIconEx)(d.dc, 0, 0, ic, 16, 16, 0, NULL, DI_IMAGE);
    U(GdiFlush)();
    EXPECT_EQ(r, 1);
    /* DI_IMAGE: the image part alone, mask ignored; with an all-ones image the white pixel replaces the 0x808080 background */
    EXPECT_EQ(d.bits[8 * 32 + 8] & 0xffffff, 0xffffff);
    dib_free(&d); U(DestroyIcon)(ic);
    /* an all-zero image distinguishes a copy from an OR of the background */
    ic = cim_mk_icon(16, 16, 0xff, 0x00);
    if (!ic) { t_skip("no icon"); return; }
    if (!dib_make(&d, 32, 32, 0x808080)) { U(DestroyIcon)(ic); t_skip("no DIB"); return; }
    r = U(DrawIconEx)(d.dc, 0, 0, ic, 16, 16, 0, NULL, DI_IMAGE);
    U(GdiFlush)();
    EXPECT_EQ(r, 1);
    EXPECT_EQ(d.bits[8 * 32 + 8] & 0xffffff, 0x000000);
    dib_free(&d); U(DestroyIcon)(ic);
}
TEST(cursor_icon_menu_draw_invalid_flag_bits) {
    CIM_GUI();
    struct dib d; HICON ic = cim_mk_icon(16, 16, 0xff, 0);
    if (!ic) { t_skip("no icon"); return; }
    if (!dib_make(&d, 32, 32, 0x808080)) { U(DestroyIcon)(ic); t_skip("no DIB"); return; }
    static const UINT bad[] = { 0x20, 0x40, 0x100, 0x10000, 0x80000000 };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        BOOL r; CIM_CALL_ERR(r, U(DrawIconEx)(d.dc, 0, 0, ic, 16, 16, 0, NULL, bad[i]));
        CHECKF(r == 0 && GetLastError() == E_INVALID_PARAMETER, "flags 0x%x err %lu", bad[i], GetLastError()); /* diFlags & 0xffffffe0 */
    }
    U(GdiFlush)();
    EXPECT_EQ(d.bits[8 * 32 + 8] & 0xffffff, 0x808080);
    dib_free(&d); U(DestroyIcon)(ic);
}
TEST(cursor_icon_menu_draw_invalid_icon_handle) {
    CIM_GUI();
    struct dib d;
    HICON dead = cim_mk_icon(16, 16, 0xff, 0); if (!dead) { t_skip("no icon"); return; }
    U(DestroyIcon)(dead);
    if (!dib_make(&d, 32, 32, 0x808080)) { t_skip("no DIB"); return; }
    BOOL r; CIM_CALL_ERR(r, U(DrawIconEx)(d.dc, 0, 0, dead, 16, 16, 0, NULL, DI_NORMAL));
    CHECK(r == 0);
    U(GdiFlush)();
    EXPECT_EQ(d.bits[8 * 32 + 8] & 0xffffff, 0x808080);
    dib_free(&d);
}
TEST(cursor_icon_menu_draw_stretches_to_requested_size) {
    CIM_GUI();
    struct dib d; HICON ic = cim_mk_icon(16, 16, 0x00, 0xff);
    if (!ic) { t_skip("no icon"); return; }
    if (!dib_make(&d, 64, 64, 0x000000)) { U(DestroyIcon)(ic); t_skip("no DIB"); return; }
    BOOL r = U(DrawIconEx)(d.dc, 8, 8, ic, 32, 32, 0, NULL, DI_NORMAL);
    U(GdiFlush)();
    EXPECT_EQ(r, 1);
    EXPECT_EQ(d.bits[10 * 64 + 10] & 0xffffff, 0xffffff);
    EXPECT_EQ(d.bits[38 * 64 + 38] & 0xffffff, 0xffffff);   /* 8 + 32 - 2 */
    EXPECT_EQ(d.bits[41 * 64 + 41] & 0xffffff, 0x000000);   /* beyond 8 + 32 */
    EXPECT_EQ(d.bits[5 * 64 + 5] & 0xffffff, 0x000000);
    dib_free(&d); U(DestroyIcon)(ic);
}
TEST(cursor_icon_menu_draw_32bpp_icon_alpha_dib_untouched_outside) {
    CIM_GUI();
    struct dib d; HICON ic = cim_mk_icon_ii(16, 16, 32);
    if (!ic) { t_skip("no icon"); return; }
    if (!dib_make(&d, 32, 32, 0x445566)) { U(DestroyIcon)(ic); t_skip("no DIB"); return; }
    BOOL r = U(DrawIconEx)(d.dc, 8, 8, ic, 16, 16, 0, NULL, DI_NORMAL);
    U(GdiFlush)();
    t_note("DrawIconEx(32bpp zero icon) -> %d, inside pixel 0x%08lx", r, d.bits[16 * 32 + 16]);
    EXPECT_EQ(d.bits[2 * 32 + 2] & 0xffffff, 0x445566);
    EXPECT_EQ(d.bits[30 * 32 + 30] & 0xffffff, 0x445566);
    dib_free(&d); U(DestroyIcon)(ic);
}
TEST(cursor_icon_menu_draw_keeps_dc_state_modulo_colours) {
    CIM_GUI();
    struct dib d; HICON ic = cim_mk_icon(16, 16, 0xff, 0);
    if (!ic) { t_skip("no icon"); return; }
    if (!dib_make(&d, 32, 32, 0)) { U(DestroyIcon)(ic); t_skip("no DIB"); return; }
    U(SetROP2)(d.dc, R2_XORPEN);
    U(DrawIconEx)(d.dc, 0, 0, ic, 16, 16, 0, NULL, DI_NORMAL);
    if (U(GetROP2)(d.dc) != R2_XORPEN) t_note("DrawIconEx changed the DC ROP2 to %d", U(GetROP2)(d.dc));
    EXPECT_EQ((ULONG_PTR)U(GetCurrentObject)(d.dc, OBJ_BITMAP), (ULONG_PTR)d.bmp);
    dib_free(&d); U(DestroyIcon)(ic);
}
