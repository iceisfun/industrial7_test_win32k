/* GetDC / GetDCEx / GetWindowDC / ReleaseDC and the DC cache (DCE list) behaviour: cache DCs, CS_OWNDC / CS_CLASSDC /
 * CS_PARENTDC classes, DCX_* flag handling, clip boxes, and the GDI handle-table cell of the returned DC. */
#include "pd.h"

#define NEED_DC() REQUIRE(UN(NtUserGetDC)); REQUIRE(UN(NtUserGetDCEx)); REQUIRE(UN(NtUserGetWindowDC)); REQUIRE(UN(NtUserReleaseDC)); \
                  REQUIRE(UN(NtUserWindowFromDC)); REQUIRE(U(GetClipBox)); REQUIRE(U(GetObjectType)); REQUIRE(U(CreateRectRgn)); \
                  REQUIRE(U(RectVisible)); REQUIRE(U(GetDCOrgEx)); REQUIRE(UN(NtUserInvalidateRect))

static RECT client(HWND h) { RECT c; U(GetClientRect)(h, &c); return c; }
static RECT wnd_rect_local(HWND h) { RECT w; U(GetWindowRect)(h, &w); U(OffsetRect)(&w, -w.left, -w.top); return w; }

/* ---- plain GetDC / ReleaseDC ---- */
PT(getdc_two_held_dcs_are_distinct) {
    PD_BEGIN(); NEED_DC();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    HDC a = U(GetDC)(h), b = U(GetDC)(h);
    CHECK(a != NULL); CHECK(b != NULL);
    CHECK(a != b);                              /* a busy cache DCE is never handed out twice */
    CHECK(U(ReleaseDC)(h, a) == 1); CHECK(U(ReleaseDC)(h, b) == 1);
    PD_END();
}
PT(getdc_cell_is_dc_with_attr_page) {
    PD_BEGIN(); NEED_DC();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    HDC dc = U(GetDC)(h);
    REQUIRE(dc);
    struct pd_cell c;
    CHECK(PD_TYPE7(dc) == 1);                   /* DC type bits in the handle */
    CHECK(U(GetObjectType)(dc) == OBJ_DC);
    if (pd_cell(dc, &c)) {
        CHECK(c.objt == 1);
        CHECK(c.puser != 0);                    /* DC_ATTR */
        CHECK(c.puser < PD_KERNEL_FLOOR);
        CHECK(c.q0 >= PD_KERNEL_FLOOR);
        CHECK((c.owner & ~1u) == GetCurrentProcessId());
        CHECK((c.upper >> 8) == (unsigned)(((ULONG_PTR)dc >> 24) & 0xff));
        t_info("GetDC cell: owner=0x%x upper=0x%04x flags=0x%02x pUser=0x%llx", c.owner, c.upper, c.flags, c.puser);
    } else t_note("GDI cell unreadable");
    U(ReleaseDC)(h, dc);
    PD_END();
}
PT(getdc_nt_direct_roundtrip) {
    PD_BEGIN(); NEED_DC();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    HDC dc = UN(NtUserGetDC)(h);
    CHECK(dc != NULL);
    CHECK(U(GetObjectType)(dc) == OBJ_DC);
    CHECK(UN(NtUserWindowFromDC)(dc) == h);
    CHECK(pd_nt_release(dc) == 1);
    PD_END();
}
PT(getdc_null_is_screen_dc) {
    PD_BEGIN(); NEED_DC(); REQUIRE(U(GetDeviceCaps));
    HDC dc = U(GetDC)(NULL);
    CHECK(dc != NULL);
    if (dc) {
        CHECK(U(GetObjectType)(dc) == OBJ_DC);
        CHECK(U(GetDeviceCaps)(dc, TECHNOLOGY) == DT_RASDISPLAY);
        CHECK(U(GetDeviceCaps)(dc, HORZRES) > 0);
        t_note("WindowFromDC(GetDC(NULL)) = %p, desktop window = %p", (void *)UN(NtUserWindowFromDC)(dc), (void *)U(GetDesktopWindow)());
        CHECK(U(ReleaseDC)(NULL, dc) == 1);
    }
    PD_END();
}
PT(getdc_bad_hwnd_returns_null_with_1400) {
    PD_BEGIN(); NEED_DC();
    HWND bogus[] = { (HWND)(ULONG_PTR)0x12345678, (HWND)(ULONG_PTR)0xFFFF };
    for (int i = 0; i < 2; i++) {
        SET_ERR(0);
        CHECKF(UN(NtUserGetDC)(bogus[i]) == NULL, "GetDC(%p)", bogus[i]);
        EXPECT_ERR(PD_ERR_INVALID_WINDOW_HANDLE);
        SET_ERR(0);
        CHECKF(UN(NtUserGetWindowDC)(bogus[i]) == NULL, "GetWindowDC(%p)", bogus[i]);
        EXPECT_ERR(PD_ERR_INVALID_WINDOW_HANDLE);
        SET_ERR(0);
        CHECKF(UN(NtUserGetDCEx)(bogus[i], NULL, DCX_CACHE) == NULL, "GetDCEx(%p)", bogus[i]);
        EXPECT_ERR(PD_ERR_INVALID_WINDOW_HANDLE);
    }
    PD_END();
}
PT(getdc_destroyed_hwnd_returns_null) {
    PD_BEGIN(); NEED_DC();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    U(DestroyWindow)(h);
    SET_ERR(0);
    CHECK(U(GetDC)(h) == NULL);
    EXPECT_ERR(PD_ERR_INVALID_WINDOW_HANDLE);
    PD_END();
}
PT(getdc_message_only_window) {
    PD_BEGIN(); NEED_DC();
    HWND m = U(CreateWindowExA)(0, pd_cls(PDC_PLAIN), "msg", WS_POPUP, 0, 0, 10, 10, HWND_MESSAGE, NULL, pd_hinst(), NULL);
    if (!m) { t_skip("message-only window creation failed"); return; }
    HDC dc = U(GetDC)(m);
    t_note("GetDC(message-only window) = %p", (void *)dc);
    if (dc) CHECK(U(ReleaseDC)(m, dc) == 1);
    U(DestroyWindow)(m);
    PD_END();
}

/* ---- ReleaseDC ---- */
PT(releasedc_ignores_hwnd_argument) {
    PD_BEGIN(); NEED_DC();
    HWND a = pd_top(PDC_PLAIN), b = pd_top(PDC_PLAIN);
    REQUIRE(a); REQUIRE(b);
    HDC dc = U(GetDC)(a);
    REQUIRE(dc);
    /* NtUserReleaseDC takes only the HDC: the window handle user32 is given is never sent to the kernel */
    CHECK(U(ReleaseDC)(b, dc) == 1);
    PD_END();
}
PT(releasedc_cache_dc_twice_second_is_zero) {
    PD_BEGIN(); NEED_DC();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    HDC dc = U(GetDC)(h);
    REQUIRE(dc);
    CHECK(U(ReleaseDC)(h, dc) == 1);
    /* ReleaseCacheDC: found in the DCE list but no longer busy (flags & 0x1000 clear) -> 2 -> FALSE */
    CHECK(U(ReleaseDC)(h, dc) == 0);
    PD_END();
}
PT(releasedc_nt_direct_twice) {
    PD_BEGIN(); NEED_DC();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    HDC dc = UN(NtUserGetDC)(h);
    REQUIRE(dc);
    CHECK(pd_nt_release(dc) == 1);
    CHECK(pd_nt_release(dc) == 0);
    PD_END();
}
PT(releasedc_null_dc_is_zero) {
    PD_BEGIN(); NEED_DC();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    CHECK(U(ReleaseDC)(h, NULL) == 0);          /* user32!ReleaseDC: hDC == NULL -> 0 before any syscall */
    SET_ERR(0);
    CHECK(pd_nt_release(NULL) == 0);
    EXPECT_ERR(PD_ERR_DC_NOT_FOUND);            /* ReleaseCacheDC: not in the DCE list -> ERROR_DC_NOT_FOUND */
    PD_END();
}
PT(releasedc_unknown_handle_sets_dc_not_found) {
    PD_BEGIN(); NEED_DC();
    SET_ERR(0);
    CHECK(pd_nt_release((HDC)(ULONG_PTR)0x00010456) == 0);
    EXPECT_ERR(PD_ERR_DC_NOT_FOUND);
    PD_END();
}
PT(releasedc_memory_dc_is_zero_and_dc_survives) {
    PD_BEGIN(); NEED_DC(); REQUIRE(U(CreateCompatibleDC)); REQUIRE(U(DeleteDC));
    HDC m = U(CreateCompatibleDC)(NULL);
    REQUIRE(m);
    CHECK(U(ReleaseDC)(NULL, m) == 0);          /* not a cache DC */
    CHECK(U(GetObjectType)(m) == OBJ_MEMDC);    /* and ReleaseDC did not destroy it */
    CHECK(U(DeleteDC)(m));
    PD_END();
}
PT(releasedc_createdc_display_dc_is_released_and_gone) {
    PD_BEGIN(); NEED_DC(); REQUIRE(U(CreateDCW));
    HDC d = U(CreateDCW)(L"DISPLAY", NULL, NULL, NULL);
    if (!d) { t_skip("CreateDC(DISPLAY) failed"); return; }
    /* unlike a memory DC, the DISPLAY DC is on the DC cache list: ReleaseDC(NULL, dc) finds and releases it (1), and a
     * DeleteDC afterwards fails because the DC is already gone */
    CHECK(U(ReleaseDC)(NULL, d) == 1);
    CHECK(!U(DeleteDC)(d));
    PD_END();
}
PT(releasedc_after_window_destroyed_cache_dc) {
    PD_BEGIN(); NEED_DC();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    HDC dc = U(GetDC)(h);
    REQUIRE(dc);
    U(DestroyWindow)(h);
    int r = U(ReleaseDC)(h, dc);
    t_note("ReleaseDC of a cache DC after its window was destroyed = %d", r);
    PD_END();
}
PT(cache_dc_forty_held_distinct_then_released) {
    PD_BEGIN(); NEED_DC();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    enum { N = 40 };
    HDC dc[N];
    unsigned before = U(GetGuiResources) ? U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS) : 0;
    for (int i = 0; i < N; i++) { dc[i] = U(GetDC)(h); CHECKF(dc[i] != NULL, "GetDC #%d", i); }
    int dup = 0;
    for (int i = 0; i < N; i++) for (int j = i + 1; j < N; j++) if (dc[i] && dc[i] == dc[j]) dup++;
    CHECK(dup == 0);
    unsigned during = U(GetGuiResources) ? U(GetGuiResources)(GetCurrentProcess(), GR_GDIOBJECTS) : 0;
    t_note("GDI objects: %u before, %u while 40 cache DCs are held", before, during);
    int rel = 0, again = 0;
    for (int i = 0; i < N; i++) if (dc[i]) rel += U(ReleaseDC)(h, dc[i]) == 1;
    CHECK(rel == N);
    /* DCEs beyond the 0x20 free ones are destroyed (ReleaseCacheDC: gnDCECount > 0x20 -> DestroyCacheDC): either way a second release is 0 */
    for (int i = 0; i < N; i++) if (dc[i]) again += U(ReleaseDC)(h, dc[i]) != 0;
    CHECK(again == 0);
    PD_END();
}
PT(cache_dc_reuse_after_release_note) {
    PD_BEGIN(); NEED_DC();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    HDC a = U(GetDC)(h);
    U(ReleaseDC)(h, a);
    HDC b = U(GetDC)(h);
    t_note("GetDC/ReleaseDC/GetDC: %p then %p (%s)", (void *)a, (void *)b, a == b ? "same cache DCE reused" : "different");
    CHECK(b != NULL);
    U(ReleaseDC)(h, b);
    PD_END();
}

/* ---- DCX flag validation ---- */
PT(getdcex_undefined_flag_bits_rejected) {
    PD_BEGIN(); NEED_DC();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    /* NtUserGetDCEx: (flags & 0xffdaf800) != 0 -> ERROR_INVALID_PARAMETER and NULL */
    DWORD bad[] = { 0x800, 0x1000, 0x2000, 0x4000, 0x8000, 0x20000, 0x80000, 0x100000, 0x400000, 0x800000, 0x1000000, 0x2000000,
                    0x4000000, 0x8000000, 0x10000000, 0x20000000, 0x40000000, 0x80000000, DCX_CACHE | 0x800, DCX_CACHE | 0x80000000 };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SET_ERR(0);
        HDC dc = UN(NtUserGetDCEx)(h, NULL, bad[i]);
        DWORD e = GetLastError();
        CHECKF(dc == NULL, "flags 0x%lx returned a DC", (unsigned long)bad[i]);
        CHECKF(e == ERROR_INVALID_PARAMETER, "flags 0x%lx last error %lu", (unsigned long)bad[i], e);
        if (dc) pd_nt_release(dc);
    }
    PD_END();
}
PT(getdcex_defined_flag_bits_accepted) {
    PD_BEGIN(); NEED_DC();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    DWORD ok[] = { DCX_CACHE, DCX_WINDOW | DCX_CACHE, DCX_CACHE | DCX_CLIPCHILDREN, DCX_CACHE | DCX_CLIPSIBLINGS, DCX_CACHE | DCX_PARENTCLIP,
                   DCX_CACHE | DCX_NORESETATTRS, DCX_CACHE | DCX_LOCKWINDOWUPDATE, DCX_CACHE | DCX_USESTYLE, DCX_CACHE | DCX_VALIDATE,
                   DCX_CACHE | DCX_EXCLUDEUPDATE, DCX_CACHE | DCX_INTERSECTUPDATE, 0x40000 | DCX_CACHE, DCX_USESTYLE };
    for (unsigned i = 0; i < sizeof ok / sizeof ok[0]; i++) {
        SET_ERR(0);
        HDC dc = UN(NtUserGetDCEx)(h, NULL, ok[i]);
        CHECKF(dc != NULL, "flags 0x%lx failed (err %lu)", (unsigned long)ok[i], GetLastError());
        if (dc) CHECKF(pd_nt_release(dc) == 1, "flags 0x%lx: release", (unsigned long)ok[i]);
    }
    /* DCX_WINDOW alone (no DCX_CACHE) asks for the window's own DC: a window of a class without CS_OWNDC/CS_CLASSDC has none */
    {   SET_ERR(0);
        HDC dc = UN(NtUserGetDCEx)(h, NULL, DCX_WINDOW);
        CHECK(dc == NULL);
        if (dc) pd_nt_release(dc); }
    PD_END();
}
PT(getdcex_non_region_handle_rejected) {
    PD_BEGIN(); NEED_DC(); REQUIRE(U(GetStockObject));
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    HGDIOBJ br = U(GetStockObject)(WHITE_BRUSH);
    HRGN gone = pd_rgn(0, 0, 5, 5);
    pd_rgn_free(gone);
    HRGN bad[] = { (HRGN)br, (HRGN)(ULONG_PTR)0x1234, gone };
    DWORD fl[] = { DCX_CACHE | DCX_INTERSECTRGN, DCX_CACHE | DCX_EXCLUDERGN, DCX_CACHE };
    for (int i = 0; i < 3; i++)
        for (int f = 0; f < 3; f++) {
            SET_ERR(0);
            HDC dc = UN(NtUserGetDCEx)(h, bad[i], fl[f]);
            DWORD e = GetLastError();
            /* a non-NULL hrgn must be accepted as a valid region regardless of which DCX bits are set */
            CHECKF(dc == NULL, "bad hrgn #%d flags #%d returned a DC", i, f);
            CHECKF(e == ERROR_INVALID_PARAMETER, "bad hrgn #%d flags #%d err %lu", i, f, e);
            if (dc) pd_nt_release(dc);
        }
    PD_END();
}
PT(getdcex_null_hwnd_window_cache_dc) {
    PD_BEGIN(); NEED_DC();
    HDC dc = UN(NtUserGetDCEx)(NULL, NULL, DCX_WINDOW | DCX_CACHE);
    CHECK(dc != NULL);
    if (dc) { CHECK(U(GetObjectType)(dc) == OBJ_DC); CHECK(pd_nt_release(dc) == 1); }
    PD_END();
}

/* ---- clip boxes ---- */
PT(client_dc_clip_box_is_client_rect) {
    PD_BEGIN(); NEED_DC();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    PD_NEED_EXPOSED(h);
    HDC dc = U(GetDC)(h);
    RECT b, c = client(h);
    CHECK(U(GetClipBox)(dc, &b) == SIMPLEREGION);
    CHECK(pd_eq(&b, &c));
    U(ReleaseDC)(h, dc);
    PD_END();
}
PT(client_dc_clip_box_always_within_client) {
    PD_BEGIN(); NEED_DC();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    HDC dc = U(GetDC)(h);
    RECT b = {0}, c = client(h);
    int t = U(GetClipBox)(dc, &b);
    CHECK(t == SIMPLEREGION || t == NULLREGION);
    if (t == SIMPLEREGION) CHECK(b.left >= c.left && b.top >= c.top && b.right <= c.right && b.bottom <= c.bottom);
    U(ReleaseDC)(h, dc);
    PD_END();
}
PT(window_dc_covers_frame_and_origin_is_window_origin) {
    PD_BEGIN(); NEED_DC();
    HWND h = pd_top_style(PDC_PLAIN, WS_CAPTION);
    REQUIRE(h);
    PD_NEED_EXPOSED(h);
    HDC cdc = U(GetDC)(h), wdc = U(GetWindowDC)(h);
    REQUIRE(cdc); REQUIRE(wdc);
    RECT wb, wl = wnd_rect_local(h), wr, cr;
    int t = U(GetClipBox)(wdc, &wb);
    t_note("window DC clip box (%ld,%ld)-(%ld,%ld) window rect %ldx%ld", wb.left, wb.top, wb.right, wb.bottom, wl.right, wl.bottom);
    CHECK(t == SIMPLEREGION);
    CHECK(wb.left <= 0 && wb.top <= 0);          /* includes the non-client area */
    U(GetWindowRect)(h, &wr); U(GetClientRect)(h, &cr);
    POINT co = {0, 0}, wo = {0, 0};
    CHECK(U(GetDCOrgEx)(cdc, &co) && U(GetDCOrgEx)(wdc, &wo));
    POINT cs = {0, 0}; U(ClientToScreen)(h, &cs);
    /* a window DC's origin is the window's top-left, a client DC's the client's: they differ by the frame, whatever the DPI/redirection */
    CHECK(co.x - wo.x == cs.x - wr.left);
    CHECK(co.y - wo.y == cs.y - wr.top);
    CHECK(co.y - wo.y > 0);                      /* the caption pushes the client down */
    U(ReleaseDC)(h, cdc); U(ReleaseDC)(h, wdc);
    PD_END();
}
PT(window_dc_clip_box_is_whole_window) {
    PD_BEGIN(); NEED_DC();
    HWND h = pd_top_style(PDC_PLAIN, WS_CAPTION);
    REQUIRE(h);
    PD_NEED_EXPOSED(h);
    HDC wdc = U(GetWindowDC)(h);
    RECT wb, wl = wnd_rect_local(h);
    CHECK(U(GetClipBox)(wdc, &wb) == SIMPLEREGION);
    CHECK(pd_eq(&wb, &wl));
    U(ReleaseDC)(h, wdc);
    PD_END();
}
PT(getwindowdc_equals_getdcex_window_clip) {
    PD_BEGIN(); NEED_DC();
    HWND h = pd_top_style(PDC_PLAIN, WS_CAPTION);
    REQUIRE(h);
    PD_NEED_EXPOSED(h);
    HDC a = UN(NtUserGetWindowDC)(h), b = UN(NtUserGetDCEx)(h, NULL, DCX_WINDOW | DCX_CACHE);
    REQUIRE(a); REQUIRE(b);
    RECT ra, rb;
    CHECK(a != b);
    CHECK(U(GetClipBox)(a, &ra) == U(GetClipBox)(b, &rb));
    CHECK(pd_eq(&ra, &rb));
    pd_nt_release(a); pd_nt_release(b);
    PD_END();
}
PT(getwindowdc_null_hwnd_screen_dc) {
    PD_BEGIN(); NEED_DC();
    HDC dc = UN(NtUserGetWindowDC)(NULL);
    CHECK(dc != NULL);
    if (dc) { CHECK(U(GetObjectType)(dc) == OBJ_DC); CHECK(pd_nt_release(dc) == 1); }
    PD_END();
}
PT(child_dc_clipped_by_parent) {
    PD_BEGIN(); NEED_DC();
    HWND p = pd_top(PDC_PLAIN);
    REQUIRE(p);
    PD_NEED_EXPOSED(p);
    HWND c = pd_child(p, PDC_PLAIN, 0, PD_CW - 30, 20, 60, 30);
    REQUIRE(c);
    HDC dc = U(GetDC)(c);
    RECT b, e = pd_rc(0, 0, 30, 30);
    CHECK(U(GetClipBox)(dc, &b) == SIMPLEREGION);
    CHECK(pd_eq(&b, &e));                        /* visrgn of a child is intersected with every ancestor's client area */
    U(ReleaseDC)(c, dc);
    PD_END();
}
PT(child_dc_origin_is_child_client_origin) {
    PD_BEGIN(); NEED_DC();
    HWND p = pd_top(PDC_PLAIN);
    REQUIRE(p);
    HWND c = pd_child(p, PDC_PLAIN, 0, 11, 13, 30, 30);
    REQUIRE(c);
    HDC pd = U(GetDC)(p), cd = U(GetDC)(c);
    POINT po = {0}, co = {0};
    CHECK(U(GetDCOrgEx)(pd, &po) && U(GetDCOrgEx)(cd, &co));
    CHECK(co.x - po.x == 11); CHECK(co.y - po.y == 13);
    U(ReleaseDC)(p, pd); U(ReleaseDC)(c, cd);
    PD_END();
}

/* ---- DCX_USESTYLE and explicit clipping flags ---- */
PT(getdc_usestyle_clipchildren_excludes_child_area) {
    PD_BEGIN(); NEED_DC();
    HWND p = pd_top_style(PDC_PLAIN, WS_CLIPCHILDREN);
    REQUIRE(p);
    HWND c = pd_child(p, PDC_PLAIN, 0, 10, 10, 40, 30);
    REQUIRE(c);
    RECT inside = pd_rc(20, 20, 30, 30), outside = pd_rc(60, 45, 80, 70);
    HDC dc = U(GetDC)(p);                        /* NtUserGetDC -> _GetDCEx(.., DCX_USESTYLE): WS_CLIPCHILDREN -> DCX_CLIPCHILDREN */
    REQUIRE(dc);
    if (!pd_exposed(p) && !U(RectVisible)(dc, &outside)) { U(ReleaseDC)(p, dc); t_skip("parent not exposed"); return; }
    CHECK(!U(RectVisible)(dc, &inside));
    CHECK(U(RectVisible)(dc, &outside));
    U(ReleaseDC)(p, dc);
    PD_END();
}
PT(getdc_no_clipchildren_style_includes_child_area) {
    PD_BEGIN(); NEED_DC();
    HWND p = pd_top(PDC_PLAIN);
    REQUIRE(p);
    HWND c = pd_child(p, PDC_PLAIN, 0, 10, 10, 40, 30);
    REQUIRE(c);
    RECT inside = pd_rc(20, 20, 30, 30);
    HDC dc = U(GetDC)(p);
    REQUIRE(dc);
    if (!U(RectVisible)(dc, &inside)) { U(ReleaseDC)(p, dc); t_skip("parent not exposed"); return; }
    CHECK(U(RectVisible)(dc, &inside));
    U(ReleaseDC)(p, dc);
    PD_END();
}
PT(getdcex_explicit_clipchildren_without_usestyle) {
    PD_BEGIN(); NEED_DC();
    HWND p = pd_top(PDC_PLAIN);
    REQUIRE(p);
    HWND c = pd_child(p, PDC_PLAIN, 0, 10, 10, 40, 30);
    REQUIRE(c);
    RECT inside = pd_rc(20, 20, 30, 30);
    HDC plain = UN(NtUserGetDCEx)(p, NULL, DCX_CACHE);
    HDC cc = UN(NtUserGetDCEx)(p, NULL, DCX_CACHE | DCX_CLIPCHILDREN);
    REQUIRE(plain); REQUIRE(cc);
    if (!U(RectVisible)(plain, &inside)) { pd_nt_release(plain); pd_nt_release(cc); t_skip("parent not exposed"); return; }
    CHECK(U(RectVisible)(plain, &inside));
    CHECK(!U(RectVisible)(cc, &inside));
    pd_nt_release(plain); pd_nt_release(cc);
    PD_END();
}
PT(getdcex_explicit_flags_ignore_window_style) {
    PD_BEGIN(); NEED_DC();
    HWND p = pd_top_style(PDC_PLAIN, WS_CLIPCHILDREN);
    REQUIRE(p);
    HWND c = pd_child(p, PDC_PLAIN, 0, 10, 10, 40, 30);
    REQUIRE(c);
    RECT inside = pd_rc(20, 20, 30, 30), outside = pd_rc(60, 45, 80, 70);
    HDC dc = UN(NtUserGetDCEx)(p, NULL, DCX_CACHE);   /* no DCX_USESTYLE: WS_CLIPCHILDREN is not consulted */
    REQUIRE(dc);
    if (!U(RectVisible)(dc, &outside)) { pd_nt_release(dc); t_skip("parent not exposed"); return; }
    CHECK(U(RectVisible)(dc, &inside));
    pd_nt_release(dc);
    PD_END();
}
PT(getdcex_clipsiblings_excludes_overlapping_sibling) {
    PD_BEGIN(); NEED_DC();
    HWND p = pd_top(PDC_PLAIN);
    REQUIRE(p);
    HWND lower = pd_child(p, PDC_PLAIN, 0, 0, 0, 50, 50);
    HWND upper = pd_child(p, PDC_PLAIN, 0, 25, 25, 50, 50);
    REQUIRE(lower); REQUIRE(upper);
    U(SetWindowPos)(upper, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);   /* explicit z-order: a new child is linked at the bottom */
    pd_settle(p);
    RECT overlap = pd_rc(30, 30, 40, 40), free = pd_rc(2, 2, 10, 10);
    HDC a = UN(NtUserGetDCEx)(lower, NULL, DCX_CACHE), b = UN(NtUserGetDCEx)(lower, NULL, DCX_CACHE | DCX_CLIPSIBLINGS);
    REQUIRE(a); REQUIRE(b);
    if (!U(RectVisible)(a, &free)) { pd_nt_release(a); pd_nt_release(b); t_skip("window not exposed"); return; }
    t_note("overlap visible: plain=%d clipsiblings=%d; z: next of lower=%p upper=%p", U(RectVisible)(a, &overlap), U(RectVisible)(b, &overlap), (void *)U(GetWindow)(lower, GW_HWNDPREV), (void *)upper);
    CHECK(U(RectVisible)(a, &overlap));
    CHECK(!U(RectVisible)(b, &overlap));
    CHECK(U(RectVisible)(b, &free));
    pd_nt_release(a); pd_nt_release(b);
    PD_END();
}
PT(getdc_usestyle_clipsiblings_child_style) {
    PD_BEGIN(); NEED_DC();
    HWND p = pd_top(PDC_PLAIN);
    REQUIRE(p);
    HWND lower = pd_child(p, PDC_PLAIN, WS_CLIPSIBLINGS, 0, 0, 50, 50);
    HWND upper = pd_child(p, PDC_PLAIN, 0, 25, 25, 50, 50);
    REQUIRE(lower); REQUIRE(upper);
    U(SetWindowPos)(upper, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    pd_settle(p);
    RECT overlap = pd_rc(30, 30, 40, 40), free = pd_rc(2, 2, 10, 10);
    HDC dc = U(GetDC)(lower);
    REQUIRE(dc);
    if (!U(RectVisible)(dc, &free)) { U(ReleaseDC)(lower, dc); t_skip("window not exposed"); return; }
    CHECK(!U(RectVisible)(dc, &overlap));       /* WS_CLIPSIBLINGS -> DCX_CLIPSIBLINGS via DCX_USESTYLE */
    U(ReleaseDC)(lower, dc);
    PD_END();
}
PT(getdcex_intersectrgn_clip_box) {
    PD_BEGIN(); NEED_DC();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    PD_NEED_EXPOSED(h);
    HRGN rg = pd_rgn_screen(h, 10, 10, 30, 30);   /* GetDCEx clip regions are in screen (device) coordinates: BeginPaint hands it the update region, which is in screen space */
    HDC dc = UN(NtUserGetDCEx)(h, rg, DCX_CACHE | DCX_INTERSECTRGN);
    REQUIRE(dc);
    RECT b, e = pd_rc(10, 10, 30, 30);
    CHECK(U(GetClipBox)(dc, &b) == SIMPLEREGION);
    CHECK(pd_eq(&b, &e));
    t_note("INTERSECTRGN clip box (%ld,%ld)-(%ld,%ld)", b.left, b.top, b.right, b.bottom);
    pd_nt_release(dc);
    t_note("hrgn still a region after INTERSECTRGN GetDCEx + release: %d", U(GetObjectType)(rg) == OBJ_REGION);
    pd_rgn_free(rg);
    PD_END();
}
PT(getdcex_excludergn_clip) {
    PD_BEGIN(); NEED_DC();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    PD_NEED_EXPOSED(h);
    HRGN rg = pd_rgn_screen(h, 10, 10, 30, 30);
    HDC dc = UN(NtUserGetDCEx)(h, rg, DCX_CACHE | DCX_EXCLUDERGN);
    REQUIRE(dc);
    RECT hole = pd_rc(12, 12, 28, 28), corner = pd_rc(0, 0, 8, 8);
    CHECK(!U(RectVisible)(dc, &hole));
    CHECK(U(RectVisible)(dc, &corner));
    RECT b, c = client(h);
    CHECK(U(GetClipBox)(dc, &b) == COMPLEXREGION);
    CHECK(pd_eq(&b, &c));
    pd_nt_release(dc);
    pd_rgn_free(rg);
    PD_END();
}
PT(getdcex_intersectrgn_disjoint_is_empty) {
    PD_BEGIN(); NEED_DC();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    PD_NEED_EXPOSED(h);
    HRGN rg = pd_rgn_screen(h, PD_CW + 20, PD_CH + 20, PD_CW + 40, PD_CH + 40);
    HDC dc = UN(NtUserGetDCEx)(h, rg, DCX_CACHE | DCX_INTERSECTRGN);
    REQUIRE(dc);
    RECT b;
    CHECK(U(GetClipBox)(dc, &b) == NULLREGION);
    pd_nt_release(dc);
    pd_rgn_free(rg);
    PD_END();
}
PT(getdcex_intersectupdate_flag_does_not_clip) {
    PD_BEGIN(); NEED_DC();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    PD_NEED_EXPOSED(h);
    RECT in = pd_rc(4, 5, 20, 30), b;
    UN(NtUserInvalidateRect)(h, &in, FALSE);
    HDC dc = UN(NtUserGetDCEx)(h, NULL, DCX_CACHE | DCX_INTERSECTUPDATE);
    REQUIRE(dc);
    /* DCX_INTERSECTUPDATE is an internal flag: the syscall accepts it and the DC still covers the whole client area */
    RECT cl; U(GetClientRect)(h, &cl);
    CHECK(U(GetClipBox)(dc, &b) == SIMPLEREGION);
    CHECK(pd_eq(&b, &cl));
    pd_nt_release(dc);
    PD_END();
}
PT(getdcex_excludeupdate_flag_does_not_hide_invalid_area) {
    PD_BEGIN(); NEED_DC();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    PD_NEED_EXPOSED(h);
    RECT in = pd_rc(10, 10, 30, 30), ok = pd_rc(40, 40, 60, 60);
    UN(NtUserInvalidateRect)(h, &in, FALSE);
    HDC dc = UN(NtUserGetDCEx)(h, NULL, DCX_CACHE | DCX_EXCLUDEUPDATE);
    REQUIRE(dc);
    /* DCX_EXCLUDEUPDATE is an internal flag: the syscall accepts it, the invalid area stays visible in the DC */
    CHECK(U(RectVisible)(dc, &in));
    CHECK(U(RectVisible)(dc, &ok));
    pd_nt_release(dc);
    PD_END();
}

/* ---- CS_PARENTDC ---- */
PT(parentdc_child_dc_uses_parent_visible_region) {
    PD_BEGIN(); NEED_DC();
    HWND p = pd_top(PDC_PLAIN);
    REQUIRE(p);
    PD_NEED_EXPOSED(p);
    HWND c = pd_child(p, PDC_PARENT, 0, 10, 20, 30, 30);
    REQUIRE(c);
    HDC dc = U(GetDC)(c);
    REQUIRE(dc);
    RECT b = {0}, e = pd_rc(-10, -20, PD_CW - 10, PD_CH - 20);
    /* CS_PARENTDC -> DCX_PARENTCLIP: the DC clips to the parent's visible region while its origin stays at the child's client origin */
    CHECK(U(GetClipBox)(dc, &b) == SIMPLEREGION);
    CHECK(pd_eq(&b, &e));
    POINT po = {0}, co = {0};
    HDC pdc = U(GetDC)(p);
    CHECK(U(GetDCOrgEx)(dc, &co) && U(GetDCOrgEx)(pdc, &po));
    CHECK(co.x - po.x == 10); CHECK(co.y - po.y == 20);
    U(ReleaseDC)(p, pdc);
    U(ReleaseDC)(c, dc);
    PD_END();
}
PT(parentdc_with_clipsiblings_child_is_clipped_to_itself) {
    PD_BEGIN(); NEED_DC();
    HWND p = pd_top(PDC_PLAIN);
    REQUIRE(p);
    PD_NEED_EXPOSED(p);
    HWND c = pd_child(p, PDC_PARENT, WS_CLIPSIBLINGS, 10, 20, 30, 30);
    REQUIRE(c);
    HDC dc = U(GetDC)(c);
    REQUIRE(dc);
    RECT b = {0}, e = pd_rc(0, 0, 30, 30);
    /* _GetDCEx: DCX_PARENTCLIP is dropped when CLIPSIBLINGS/CLIPCHILDREN are requested (flags & 0x18) */
    CHECK(U(GetClipBox)(dc, &b) == SIMPLEREGION);
    CHECK(pd_eq(&b, &e));
    U(ReleaseDC)(c, dc);
    PD_END();
}
PT(parentdc_flag_explicit_matches_class_style) {
    PD_BEGIN(); NEED_DC();
    HWND p = pd_top(PDC_PLAIN);
    REQUIRE(p);
    PD_NEED_EXPOSED(p);
    HWND c = pd_child(p, PDC_PLAIN, 0, 10, 20, 30, 30);
    REQUIRE(c);
    HDC dc = UN(NtUserGetDCEx)(c, NULL, DCX_CACHE | DCX_PARENTCLIP);
    REQUIRE(dc);
    RECT b = {0}, e = pd_rc(-10, -20, PD_CW - 10, PD_CH - 20);
    CHECK(U(GetClipBox)(dc, &b) == SIMPLEREGION);
    CHECK(pd_eq(&b, &e));
    pd_nt_release(dc);
    PD_END();
}

/* ---- attributes: cache DCs reset, CS_OWNDC / CS_CLASSDC keep ---- */
PT(cache_dc_attributes_reset_between_uses) {
    PD_BEGIN(); NEED_DC(); REQUIRE(U(SetBkColor)); REQUIRE(U(GetBkColor)); REQUIRE(U(SetTextColor)); REQUIRE(U(GetTextColor));
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    HDC dc = U(GetDC)(h);
    REQUIRE(dc);
    U(SetBkColor)(dc, RGB(11, 22, 33)); U(SetTextColor)(dc, RGB(44, 55, 66)); U(SetROP2)(dc, R2_XORPEN); U(SetMapMode)(dc, MM_HIMETRIC);
    U(SetViewportOrgEx)(dc, 7, 9, NULL);
    CHECK(U(ReleaseDC)(h, dc) == 1);
    HDC d2 = U(GetDC)(h);
    REQUIRE(d2);
    POINT o = {1, 1};
    U(GetViewportOrgEx)(d2, &o);
    CHECK(U(GetBkColor)(d2) == RGB(255, 255, 255));    /* bCleanDC on release restores defaults */
    CHECK(U(GetTextColor)(d2) == RGB(0, 0, 0));
    CHECK(U(GetROP2)(d2) == R2_COPYPEN);
    CHECK(U(GetMapMode)(d2) == MM_TEXT);
    CHECK(o.x == 0 && o.y == 0);
    t_note("second GetDC returned %s DC", d2 == dc ? "the same" : "a different");
    U(ReleaseDC)(h, d2);
    PD_END();
}
PT(cache_dc_norestattrs_flag_attributes_still_reset) {
    PD_BEGIN(); NEED_DC(); REQUIRE(U(SetBkColor)); REQUIRE(U(GetBkColor));
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    HDC dc = UN(NtUserGetDCEx)(h, NULL, DCX_CACHE | DCX_NORESETATTRS);
    REQUIRE(dc);
    U(SetBkColor)(dc, RGB(11, 22, 33));
    CHECK(pd_nt_release(dc) == 1);
    HDC d2 = UN(NtUserGetDCEx)(h, NULL, DCX_CACHE | DCX_NORESETATTRS);
    REQUIRE(d2);
    /* Windows 10 hands the same cache DC out again and resets its attributes whatever DCX_NORESETATTRS says */
    CHECK(d2 == dc);
    CHECK(U(GetBkColor)(d2) == RGB(255, 255, 255));
    pd_nt_release(d2);
    PD_END();
}
PT(owndc_same_handle_every_time) {
    PD_BEGIN(); NEED_DC();
    HWND h = pd_top(PDC_OWN);
    REQUIRE(h);
    HDC a = U(GetDC)(h), b = U(GetDC)(h);
    CHECK(a != NULL); CHECK(a == b);
    CHECK(U(ReleaseDC)(h, a) == 1);
    HDC c = U(GetDC)(h);
    CHECK(c == a);
    CHECK(U(ReleaseDC)(h, c) == 1);
    t_note("second ReleaseDC of the OWNDC handle = %d", U(ReleaseDC)(h, c));
    PD_END();
}
PT(owndc_attributes_persist_across_release) {
    PD_BEGIN(); NEED_DC(); REQUIRE(U(SetBkColor)); REQUIRE(U(GetBkColor));
    HWND h = pd_top(PDC_OWN);
    REQUIRE(h);
    HDC dc = U(GetDC)(h);
    REQUIRE(dc);
    U(SetBkColor)(dc, RGB(11, 22, 33)); U(SetTextColor)(dc, RGB(44, 55, 66)); U(SetROP2)(dc, R2_XORPEN);
    U(SetMapMode)(dc, MM_ANISOTROPIC); U(SetViewportOrgEx)(dc, 7, 9, NULL); U(SetBkMode)(dc, TRANSPARENT);
    U(ReleaseDC)(h, dc);
    HDC d2 = U(GetDC)(h);
    CHECK(d2 == dc);
    POINT o = {0, 0};
    U(GetViewportOrgEx)(d2, &o);
    CHECK(U(GetBkColor)(d2) == RGB(11, 22, 33));
    CHECK(U(GetTextColor)(d2) == RGB(44, 55, 66));
    CHECK(U(GetROP2)(d2) == R2_XORPEN);
    CHECK(U(GetMapMode)(d2) == MM_ANISOTROPIC);
    CHECK(U(GetBkMode)(d2) == TRANSPARENT);
    CHECK(o.x == 7 && o.y == 9);
    U(ReleaseDC)(h, d2);
    PD_END();
}
PT(owndc_cell_and_window_association) {
    PD_BEGIN(); NEED_DC();
    HWND h = pd_top(PDC_OWN);
    REQUIRE(h);
    HDC dc = U(GetDC)(h);
    REQUIRE(dc);
    struct pd_cell c;
    CHECK(PD_TYPE7(dc) == 1);
    if (pd_cell(dc, &c)) { CHECK(c.objt == 1); CHECK(c.puser != 0); }
    CHECK(UN(NtUserWindowFromDC)(dc) == h);
    U(ReleaseDC)(h, dc);
    PD_END();
}
PT(owndc_getdcex_cache_gives_a_different_dc) {
    PD_BEGIN(); NEED_DC();
    HWND h = pd_top(PDC_OWN);
    REQUIRE(h);
    HDC own = U(GetDC)(h);
    HDC cache = UN(NtUserGetDCEx)(h, NULL, DCX_CACHE);
    REQUIRE(own); REQUIRE(cache);
    CHECK(own != cache);                        /* DCX_CACHE forces the cache list; the 0x400002 filter skips the window's own DCE */
    CHECK(pd_nt_release(cache) == 1);
    CHECK(U(ReleaseDC)(h, own) == 1);
    PD_END();
}
PT(owndc_two_windows_have_different_dcs) {
    PD_BEGIN(); NEED_DC();
    HWND a = pd_top(PDC_OWN), b = pd_top(PDC_OWN);
    REQUIRE(a); REQUIRE(b);
    HDC da = U(GetDC)(a), db = U(GetDC)(b);
    CHECK(da != NULL && db != NULL); CHECK(da != db);
    CHECK(UN(NtUserWindowFromDC)(da) == a); CHECK(UN(NtUserWindowFromDC)(db) == b);
    U(ReleaseDC)(a, da); U(ReleaseDC)(b, db);
    PD_END();
}
PT(owndc_window_dc_is_not_the_client_dc) {
    PD_BEGIN(); NEED_DC();
    HWND h = pd_top_style(PDC_OWN, WS_CAPTION);
    REQUIRE(h);
    HDC c = U(GetDC)(h), w = U(GetWindowDC)(h);
    REQUIRE(c); REQUIRE(w);
    CHECK(c != w);
    POINT co = {0}, wo = {0};
    CHECK(U(GetDCOrgEx)(c, &co) && U(GetDCOrgEx)(w, &wo));
    CHECK(co.y - wo.y > 0);
    U(ReleaseDC)(h, c); U(ReleaseDC)(h, w);
    PD_END();
}
PT(classdc_shared_between_windows_of_class) {
    PD_BEGIN(); NEED_DC();
    HWND a = pd_top(PDC_CLASS), b = pd_top(PDC_CLASS);
    REQUIRE(a); REQUIRE(b);
    HDC da = U(GetDC)(a);
    REQUIRE(da);
    CHECK(UN(NtUserWindowFromDC)(da) == a);
    U(ReleaseDC)(a, da);
    HDC db = U(GetDC)(b);
    CHECK(db == da);                            /* one DCE per class: re-targeted at whichever window asks */
    CHECK(UN(NtUserWindowFromDC)(db) == b);
    U(ReleaseDC)(b, db);
    PD_END();
}
PT(classdc_attributes_follow_the_class) {
    PD_BEGIN(); NEED_DC(); REQUIRE(U(SetBkColor)); REQUIRE(U(GetBkColor));
    HWND a = pd_top(PDC_CLASS), b = pd_top(PDC_CLASS);
    REQUIRE(a); REQUIRE(b);
    HDC da = U(GetDC)(a);
    REQUIRE(da);
    U(SetBkColor)(da, RGB(9, 8, 7));
    U(ReleaseDC)(a, da);
    HDC db = U(GetDC)(b);
    REQUIRE(db);
    if (db == da) CHECK(U(GetBkColor)(db) == RGB(9, 8, 7));
    else t_note("class DC handle differs between windows");
    U(ReleaseDC)(b, db);
    PD_END();
}
PT(classdc_release_returns_one) {
    PD_BEGIN(); NEED_DC();
    HWND a = pd_top(PDC_CLASS);
    REQUIRE(a);
    HDC da = U(GetDC)(a);
    REQUIRE(da);
    CHECK(U(ReleaseDC)(a, da) == 1);
    PD_END();
}
PT(plain_class_dcs_are_cache_dcs_not_shared) {
    PD_BEGIN(); NEED_DC();
    HWND a = pd_top(PDC_PLAIN), b = pd_top(PDC_PLAIN);
    REQUIRE(a); REQUIRE(b);
    HDC da = U(GetDC)(a), db = U(GetDC)(b);
    CHECK(da != db);
    CHECK(UN(NtUserWindowFromDC)(da) == a); CHECK(UN(NtUserWindowFromDC)(db) == b);
    U(ReleaseDC)(a, da); U(ReleaseDC)(b, db);
    PD_END();
}

/* ---- WindowFromDC ---- */
PT(windowfromdc_window_and_client_and_cache) {
    PD_BEGIN(); NEED_DC();
    HWND h = pd_top(PDC_PLAIN);
    REQUIRE(h);
    HDC c = U(GetDC)(h), w = U(GetWindowDC)(h), x = UN(NtUserGetDCEx)(h, NULL, DCX_CACHE | DCX_WINDOW);
    REQUIRE(c); REQUIRE(w); REQUIRE(x);
    CHECK(UN(NtUserWindowFromDC)(c) == h); CHECK(UN(NtUserWindowFromDC)(w) == h); CHECK(UN(NtUserWindowFromDC)(x) == h);
    CHECK(U(WindowFromDC)(c) == h);
    U(ReleaseDC)(h, c); U(ReleaseDC)(h, w); U(ReleaseDC)(h, x);
    PD_END();
}
PT(windowfromdc_memory_dc_and_garbage_are_null) {
    PD_BEGIN(); NEED_DC(); REQUIRE(U(CreateCompatibleDC));
    HDC m = U(CreateCompatibleDC)(NULL);
    REQUIRE(m);
    CHECK(UN(NtUserWindowFromDC)(m) == NULL);
    CHECK(UN(NtUserWindowFromDC)(NULL) == NULL);
    CHECK(UN(NtUserWindowFromDC)((HDC)(ULONG_PTR)0x00010789) == NULL);
    U(DeleteDC)(m);
    PD_END();
}
PT(windowfromdc_child_window) {
    PD_BEGIN(); NEED_DC();
    HWND p = pd_top(PDC_PLAIN);
    REQUIRE(p);
    HWND c = pd_child(p, PDC_PLAIN, 0, 5, 5, 20, 20);
    REQUIRE(c);
    HDC dc = U(GetDC)(c);
    REQUIRE(dc);
    CHECK(UN(NtUserWindowFromDC)(dc) == c);
    U(ReleaseDC)(c, dc);
    PD_END();
}

/* ---- another thread asking for an OWNDC window's DC gets a cache DC ---- */
struct xthr { HWND h; HDC own; HDC got; int rel1, rel2; HWND wfd; };
static DWORD xthr_fn(void *p) {
    struct xthr *x = (struct xthr *)p;
    x->got = U(GetDC)(x->h);
    if (x->got) { x->wfd = UN(NtUserWindowFromDC)(x->got); x->rel1 = U(ReleaseDC)(x->h, x->got); x->rel2 = U(ReleaseDC)(x->h, x->got); }
    return 0;
}
PT(owndc_from_other_thread_is_cache_dc) {
    PD_BEGIN(); NEED_DC();
    HWND h = pd_top(PDC_OWN);
    REQUIRE(h);
    struct xthr x; memset(&x, 0, sizeof x);
    x.h = h;
    x.own = U(GetDC)(h);
    REQUIRE(x.own);
    k_run_gui_thread(xthr_fn, &x);
    /* _GetDCEx: a window owned by another thread always takes the DCX_CACHE path */
    CHECK(x.got != NULL);
    CHECK(x.got != x.own);
    CHECK(x.wfd == h);
    CHECK(x.rel1 == 1);
    CHECK(x.rel2 == 0);
    CHECK(U(ReleaseDC)(h, x.own) == 1);
    PD_END();
}
PT(owndc_destroyed_with_window_note) {
    PD_BEGIN(); NEED_DC();
    HWND h = pd_top(PDC_OWN);
    REQUIRE(h);
    HDC dc = U(GetDC)(h);
    REQUIRE(dc);
    U(ReleaseDC)(h, dc);
    U(DestroyWindow)(h);
    DWORD t = U(GetObjectType)(dc);
    t_note("OWNDC handle %p GetObjectType after DestroyWindow = %lu", (void *)dc, t);
    PD_END();
}

/* ---- a pristine process: the first GUI call is a DC request ---- */
CHILD(pd_fresh_getdc) {
    int bad = 0;
#define CC(c) do { int ok_ = !!(c); if (!ok_) bad++; t_check(ok_, __FILE__, __LINE__, #c, NULL); } while (0)   /* evaluates c once: the operands have side effects */
    /* nothing of user32/gdi32 has been loaded yet: the first U() below loads them (NtUserProcessConnect) and the GetDC
     * call is the first GUI call of the process; only afterwards are the win32u stubs used directly */
    HDC dc = U(GetDC) ? U(GetDC)(NULL) : NULL;
    t_note("fresh process GetDC(NULL) = %p, last error %lu", (void *)dc, GetLastError());
    void *w32ti = NULL;
    k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &w32ti);
    CC(w32ti != NULL);                          /* the thread was converted by its first win32k syscall */
    CC(dc != NULL);
    if (dc) {
        CC(U(GetObjectType)(dc) == OBJ_DC);
        struct pd_cell c;
        if (pd_cell(dc, &c)) { CC(c.objt == 1); CC(c.puser != 0); CC((c.owner & ~1u) == GetCurrentProcessId()); }
        CC(U(ReleaseDC)(NULL, dc) == 1);
        CC(U(ReleaseDC)(NULL, dc) == 0);
        if (UN(NtUserGetDC)) {
            HDC d2 = UN(NtUserGetDC)(NULL);
            CC(d2 != NULL);
            if (d2) CC(pd_nt_release(d2) == 1);
        }
    }
#undef CC
    return bad;
}
PT(fresh_process_first_getdc_child) {
    PD_BEGIN();
    REQUIRE(UN(NtUserGetDC));
    struct k_child_result r = k_child_run("pd_fresh_getdc", "", 20000);
    CHECK(r.exit_code == 0);
    PD_END();
}
