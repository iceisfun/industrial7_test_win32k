/* Layered CHILD windows (WS_CHILD with WS_EX_LAYERED) and their Set/Get/UpdateLayeredWindow behaviour. Windows 8 and later allow
 * the layered style on child windows, but only for a process whose application manifest lists a supported OS of Windows 8 or newer;
 * any other process silently gets the old rule that the style cannot be put on a child window. Every scenario therefore runs in
 * a child process started from a private copy of this executable, once WITHOUT any manifest resource ("plain") and once WITH an
 * embedded manifest that lists Windows 7, 8, 8.1 and 10 ("manifested"). The scenario checks that the variant really took effect
 * (reported OS version 10.0 versus the compatibility-lie 6.2), then performs its steps and returns its failure count.
 * Measures: creation result, style bit through SetWindowLong, exact results of Set/Get/UpdateLayeredWindow on layered children,
 * nested children, children of layered or image-driven parents, destruction with the parent, style conversions and reparenting. */
#include "xl.h"

typedef void (*scn_fn)(int manif);
static struct { const char *name; scn_fn fn; } g_scn[64];
static int g_nscn;
static void scn_reg(const char *n, scn_fn f) { if (g_nscn < 64) { g_scn[g_nscn].name = n; g_scn[g_nscn].fn = f; g_nscn++; } }

#define SCN(nm) \
    static void scn_##nm(int manif); \
    static void __attribute__((constructor)) screg_##nm(void) { scn_reg(#nm, scn_##nm); } \
    TEST(child_manifested_##nm) { run_variant(#nm, 1); } \
    TEST(child_plain_##nm)      { run_variant(#nm, 0); } \
    static void scn_##nm(int manif)

static void run_variant(const char *name, int manif) {
    XL_NEED();
    const char *exe = xl_exe_copy(manif);
    if (!exe) { t_skip("cannot prepare the %s executable copy", manif ? "manifested" : "plain"); return; }
    char args[128], line[600]; struct xl_proc p;
    snprintf(args, sizeof args, "%s %d", name, manif);
    if (!xl_spawn(&p, exe, "cscn", args)) { t_skip("cannot start the copy (err %lu)", GetLastError()); return; }
    int bad = 0;
    while (xl_readline(&p, line, sizeof line, 40000)) { printf("  CHILD %s/%s: %s\n", manif ? "manifested" : "plain", name, line); if (!strncmp(line, "  FAIL", 6)) bad++; }
    DWORD code = xl_finish(&p);
    CHECKF(bad == 0, "scenario %s (%s): %d failed checks in the child", name, manif ? "manifested" : "plain", bad);
    CHECKF(code == 0, "scenario %s (%s) exit code 0x%lx", name, manif ? "manifested" : "plain", code);
}
CHILD(cscn) {
    const char *name = argc > 1 ? argv[1] : ""; int manif = argc > 2 && argv[2][0] == '1';
    if (!UN(NtUserSetLayeredWindowAttributes) || !U(CreateWindowExA) || !U(UpdateLayeredWindow)) { printf("FAIL missing exports\n"); return 90; }
    OSVERSIONINFOA vi; memset(&vi, 0, sizeof vi); vi.dwOSVersionInfoSize = sizeof vi; U(GetVersionExA)(&vi);
    EXPECT_EQ(vi.dwMajorVersion, manif ? 10 : 6);          /* the manifest took effect (or, without it, the legacy version is reported) */
    for (int i = 0; i < g_nscn; i++) if (!strcmp(g_scn[i].name, name)) { g_scn[i].fn(manif); return 0; }
    printf("FAIL unknown scenario %s\n", name); return 91;
}

#define IMG(im) struct xl_img im; if (!xl_img_make(&im, 8, 8)) { printf("FAIL cannot build image\n"); return; }
static void expect_get(HWND w, COLORREF k, BYTE a, DWORD f) {
    struct xl_attr o; xl_attr_clear(&o); SET_ERR(XL_ERR_SENT);
    BOOL r = xl_getall(w, &o);
    CHECK(r); if (r) { EXPECT_EQ(o.key, k); EXPECT_EQ(o.alpha, a); EXPECT_EQ(o.flags, f); }
}
static void expect_get_fails(HWND w, DWORD err) {
    struct xl_attr o; xl_attr_clear(&o); SET_ERR(XL_ERR_SENT);
    CHECK(!xl_getall(w, &o)); EXPECT_ERR(err); EXPECT_EQ(o.flags, 0xfeedf00d);
}
/* A layered child can only exist in the manifested variant; in the plain one the creation is refused (NULL) and nothing is left over. */
static HWND mk_lchild(HWND parent, int manif) {
    HWND c = xl_mk(WS_CHILD, XL_EXL, parent);
    if (manif) { CHECK(c != NULL); if (c) CHECK(xl_is_lay(c)); } else CHECK(c == NULL);
    return c;
}

SCN(create_layered_child_of_plain_top) { HWND top = xl_plain(); HWND c = mk_lchild(top, manif); if (c) xl_kill(c); xl_kill(top); }
SCN(create_layered_child_of_layered_top) { HWND top = xl_lay(); HWND c = mk_lchild(top, manif); if (c) xl_kill(c); xl_kill(top); }
SCN(create_layered_child_visible) { HWND top = xl_plain(); HWND c = xl_mk(WS_CHILD | WS_VISIBLE, XL_EXL, top); CHECK((c != NULL) == (manif != 0)); if (c) xl_kill(c); xl_kill(top); }
SCN(create_layered_child_transparent) { HWND top = xl_plain(); HWND c = xl_mk(WS_CHILD | WS_CLIPSIBLINGS, XL_EXL | WS_EX_TRANSPARENT, top); CHECK((c != NULL) == (manif != 0)); if (c) CHECK(xl_set(c, 1, 2, 3)); if (c) xl_kill(c); xl_kill(top); }
SCN(create_layered_child_without_parent_1406) {
    SET_ERR(XL_ERR_SENT); HWND c = xl_mk(WS_CHILD, XL_EXL, NULL); CHECK(c == NULL); EXPECT_ERR(1406);   /* a child window needs a parent */
    if (c) xl_kill(c);
}
SCN(create_layered_nested_child) {
    HWND top = xl_plain(); HWND c = mk_lchild(top, manif); HWND c2 = c ? xl_mk(WS_CHILD, XL_EXL, c) : NULL;
    if (manif) { CHECK(c2 != NULL); if (c2) CHECK(xl_set(c2, 5, 6, 3)); }
    if (c2) xl_kill(c2); if (c) xl_kill(c); xl_kill(top);
}
SCN(plain_child_set_get_87) {
    HWND top = xl_plain(); HWND c = xl_mk(WS_CHILD, 0, top); CHECK(c != NULL); if (!c) return;
    SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_set(c, 5, 100, 3), ERROR_INVALID_PARAMETER); expect_get_fails(c, ERROR_INVALID_PARAMETER);
    xl_kill(c); xl_kill(top);
}
SCN(plain_child_of_layered_top_set_87) {
    HWND top = xl_lay(); CHECK(xl_set(top, 0, 200, 2)); HWND c = xl_mk(WS_CHILD, 0, top); CHECK(c != NULL); if (!c) return;
    SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_set(c, 5, 100, 3), ERROR_INVALID_PARAMETER);
    expect_get(top, 0, 200, 2); xl_kill(c); xl_kill(top);
}
SCN(setwindowlong_add_layered_to_child) {
    HWND top = xl_plain(); HWND c = xl_mk(WS_CHILD, 0, top); CHECK(c != NULL); if (!c) return;
    U(SetWindowLongA)(c, GWL_EXSTYLE, xl_exstyle(c) | XL_EXL);
    CHECK(xl_is_lay(c) == (manif != 0));            /* ignored without the compatible manifest */
    SET_ERR(XL_ERR_SENT);
    if (manif) { CHECK(xl_set(c, 5, 100, 3)); expect_get(c, 5, 100, 3); } else { EXPECT_FAIL(!xl_set(c, 5, 100, 3), ERROR_INVALID_PARAMETER); }
    xl_kill(c); xl_kill(top);
}
SCN(setwindowlong_add_layered_to_child_of_layered_top) {
    HWND top = xl_lay(); xl_set(top, 0, 1, 2); HWND c = xl_mk(WS_CHILD, 0, top); CHECK(c != NULL); if (!c) return;
    U(SetWindowLongA)(c, GWL_EXSTYLE, xl_exstyle(c) | XL_EXL); CHECK(xl_is_lay(c) == (manif != 0));
    xl_kill(c); xl_kill(top);
}
SCN(setwindowlong_replace_exstyle_of_child) {
    HWND top = xl_plain(); HWND c = xl_mk(WS_CHILD, 0, top); CHECK(c != NULL); if (!c) return;
    U(SetWindowLongA)(c, GWL_EXSTYLE, XL_EXL); CHECK(xl_exstyle(c) == (manif ? XL_EXL : 0));
    xl_kill(c); xl_kill(top);
}
SCN(layered_child_get_fresh_fails_untouched) {
    HWND top = xl_plain(); HWND c = mk_lchild(top, manif); if (c) expect_get_fails(c, XL_ERR_SENT); if (c) xl_kill(c); xl_kill(top);
}
SCN(layered_child_set_get_roundtrip) {
    HWND top = xl_plain(); HWND c = mk_lchild(top, manif);
    if (c) { SET_ERR(XL_ERR_SENT); CHECK(xl_set(c, 0x050607, 100, 3)); EXPECT_EQ(GetLastError(), XL_ERR_SENT); expect_get(c, 0x050607, 100, 3);
             CHECK(xl_set(c, 0x050608, 7, 2)); expect_get(c, 0x050607, 7, 2); }   /* alpha only: the key is kept */
    if (c) xl_kill(c); xl_kill(top);
}
SCN(layered_child_set_bad_flags_87) {
    HWND top = xl_plain(); HWND c = mk_lchild(top, manif);
    if (c) { SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_set(c, 1, 2, 4), ERROR_INVALID_PARAMETER); }
    if (c) xl_kill(c); xl_kill(top);
}
SCN(layered_child_of_layered_top_independent_state) {
    HWND top = xl_lay(); CHECK(xl_set(top, 0x111111, 50, 2)); HWND c = mk_lchild(top, manif);
    if (c) { CHECK(xl_set(c, 0x222222, 60, 3)); expect_get(c, 0x222222, 60, 3); }
    expect_get(top, 0, 50, 2);                        /* the parent keeps its own attributes (flags 2 stores no key) */
    if (c) xl_kill(c); xl_kill(top);
}
SCN(layered_child_ulw_fresh_succeeds) {
    IMG(im); HWND top = xl_plain(); HWND c = mk_lchild(top, manif);
    if (c) { SET_ERR(XL_ERR_SENT); CHECK(xl_ulw(c, &im, ULW_ALPHA)); EXPECT_EQ(GetLastError(), XL_ERR_SENT); expect_get_fails(c, XL_ERR_SENT); }
    if (c) xl_kill(c); xl_kill(top); xl_img_free(&im);
}
SCN(layered_child_ulw_then_set_then_ulw) {
    IMG(im); HWND top = xl_plain(); HWND c = mk_lchild(top, manif);
    if (c) { CHECK(xl_ulw(c, &im, ULW_ALPHA)); CHECK(xl_set(c, 1, 2, 3)); SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_ulw(c, &im, ULW_ALPHA), ERROR_INVALID_PARAMETER); }
    if (c) xl_kill(c); xl_kill(top); xl_img_free(&im);
}
SCN(layered_child_set_then_ulw_87) {
    IMG(im); HWND top = xl_plain(); HWND c = mk_lchild(top, manif);
    if (c) { CHECK(xl_set(c, 1, 2, 3)); SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_ulw(c, &im, ULW_ALPHA), ERROR_INVALID_PARAMETER); expect_get(c, 1, 2, 3); }
    if (c) xl_kill(c); xl_kill(top); xl_img_free(&im);
}
SCN(layered_child_ulw_position_is_parent_relative) {
    IMG(im); HWND top = xl_plain(); HWND c = mk_lchild(top, manif);
    if (c) {
        POINT d = { 3, 4 }, s = { 0, 0 }; SIZE sz = { 10, 9 }; BLENDFUNCTION b = xl_blend(255); RECT rc;
        CHECK(U(UpdateLayeredWindow)(c, NULL, &d, &sz, im.dc, &s, 0, &b, ULW_ALPHA));
        U(GetWindowRect)(c, &rc); POINT o = { rc.left, rc.top }; U(ScreenToClient)(top, &o);
        EXPECT_EQ(rc.right - rc.left, 10); EXPECT_EQ(rc.bottom - rc.top, 9); EXPECT_EQ(o.x, 3); EXPECT_EQ(o.y, 4);
    }
    if (c) xl_kill(c); xl_kill(top); xl_img_free(&im);
}
SCN(nested_layered_child_ulw_fresh) {
    IMG(im); HWND top = xl_plain(); HWND c = mk_lchild(top, manif); HWND c2 = c ? xl_mk(WS_CHILD, XL_EXL, c) : NULL;
    if (c2) CHECK(xl_ulw(c2, &im, ULW_ALPHA));
    if (c2) xl_kill(c2); if (c) xl_kill(c); xl_kill(top); xl_img_free(&im);
}
SCN(plain_child_ulw_87) {
    IMG(im); HWND top = xl_plain(); HWND c = xl_mk(WS_CHILD, 0, top); CHECK(c != NULL);
    if (c) { SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_ulw(c, &im, ULW_ALPHA), ERROR_INVALID_PARAMETER); }
    if (c) xl_kill(c); xl_kill(top); xl_img_free(&im);
}
SCN(layered_child_of_attribute_layered_top) {
    IMG(im); HWND top = xl_lay(); CHECK(xl_set(top, 0, 1, 2)); HWND c = mk_lchild(top, manif);
    if (c) CHECK(xl_ulw(c, &im, ULW_ALPHA));           /* a child's image update does not care how its parent is driven */
    SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_ulw(top, &im, ULW_ALPHA), ERROR_INVALID_PARAMETER);
    expect_get(top, 0, 1, 2);
    if (c) xl_kill(c); xl_kill(top); xl_img_free(&im);
}
SCN(layered_child_of_image_layered_top) {
    IMG(im); HWND top = xl_lay(); CHECK(xl_ulw(top, &im, ULW_ALPHA)); HWND c = mk_lchild(top, manif);
    if (c) CHECK(xl_set(c, 1, 2, 3));
    HWND pc = xl_mk(WS_CHILD, 0, top); CHECK(pc != NULL);
    if (pc) { SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_set(pc, 1, 2, 3), ERROR_INVALID_PARAMETER); xl_kill(pc); }
    if (c) xl_kill(c); xl_kill(top); xl_img_free(&im);
}
SCN(child_destroyed_with_parent_1400) {
    HWND top = xl_plain(); HWND c = mk_lchild(top, manif); HWND pc = xl_mk(WS_CHILD, 0, top);
    if (c) CHECK(xl_set(c, 1, 2, 3));
    xl_kill(top);
    struct xl_attr a;
    if (c) { CHECK(!U(IsWindow)(c)); SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_set(c, 1, 2, 3), ERROR_INVALID_WINDOW_HANDLE); SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_getall(c, &a), ERROR_INVALID_WINDOW_HANDLE); }
    if (pc) { SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_set(pc, 1, 2, 3), ERROR_INVALID_WINDOW_HANDLE); }
}
SCN(layered_child_clear_and_readd_bit) {
    HWND top = xl_plain(); HWND c = mk_lchild(top, manif);
    if (c) {
        CHECK(xl_set(c, 9, 9, 3)); LONG ex = xl_exstyle(c);
        U(SetWindowLongA)(c, GWL_EXSTYLE, ex & ~XL_EXL); CHECK(!xl_is_lay(c)); expect_get_fails(c, ERROR_INVALID_PARAMETER);
        U(SetWindowLongA)(c, GWL_EXSTYLE, ex); CHECK(xl_is_lay(c)); expect_get_fails(c, XL_ERR_SENT);
        CHECK(xl_set(c, 1, 2, 3)); expect_get(c, 1, 2, 3);
    }
    if (c) xl_kill(c); xl_kill(top);
}
SCN(layered_popup_converted_to_child_style_keeps_state) {
    HWND w = xl_lay(); CHECK(xl_set(w, 5, 5, 3));
    U(SetWindowLongA)(w, GWL_STYLE, WS_CHILD);            /* no parent set: only the style bits change */
    CHECK(xl_is_lay(w)); expect_get(w, 5, 5, 3); CHECK(xl_set(w, 6, 6, 3)); expect_get(w, 6, 6, 3);
    xl_kill(w);
}
SCN(child_converted_to_popup_style_ignored_layered_state) {
    HWND top = xl_plain(); HWND c = xl_mk(WS_CHILD, 0, top); CHECK(c != NULL); if (!c) return;
    U(SetWindowLongA)(c, GWL_EXSTYLE, xl_exstyle(c) | XL_EXL);
    if (manif) { CHECK(xl_set(c, 9, 9, 3)); }
    U(SetWindowLongA)(c, GWL_STYLE, WS_POPUP);
    if (manif) { CHECK(xl_is_lay(c)); expect_get(c, 9, 9, 3); } else { CHECK(!xl_is_lay(c)); expect_get_fails(c, ERROR_INVALID_PARAMETER); }
    xl_kill(c); xl_kill(top);
}
SCN(setparent_layered_popup_into_top_keeps_state) {
    HWND top = xl_plain(); HWND w = xl_lay(); CHECK(xl_set(w, 0, 90, 2));
    HWND old = U(SetParent)(w, top); CHECK(old != NULL);
    CHECK(xl_is_lay(w)); expect_get(w, 0, 90, 2);
    xl_kill(w); xl_kill(top);
}
SCN(toplevel_layered_unaffected_by_manifest) {
    HWND w = xl_lay(); CHECK(w != NULL); if (!w) return;
    expect_get_fails(w, XL_ERR_SENT); CHECK(xl_set(w, 0x010203, 1, 3)); expect_get(w, 0x010203, 1, 3);
    xl_kill(w);
}
SCN(user32_front_doors_on_child) {
    HWND top = xl_plain(); HWND c = mk_lchild(top, manif);
    if (c) { COLORREF k; BYTE a; DWORD f;
        CHECK(U(SetLayeredWindowAttributes)(c, 0x0a0b0c, 33, 3)); CHECK(U(GetLayeredWindowAttributes)(c, &k, &a, &f));
        EXPECT_EQ(k, 0x0a0b0c); EXPECT_EQ(a, 33); EXPECT_EQ(f, 3); }
    if (c) xl_kill(c); xl_kill(top);
}
SCN(layered_child_survives_move_and_show) {
    HWND top = xl_plain(); HWND c = mk_lchild(top, manif);
    if (c) { CHECK(xl_set(c, 4, 5, 3)); U(SetWindowPos)(c, NULL, 1, 1, 5, 5, SWP_NOZORDER | SWP_NOACTIVATE); U(ShowWindow)(c, SW_SHOWNA); U(ShowWindow)(c, SW_HIDE); expect_get(c, 4, 5, 3); }
    if (c) xl_kill(c); xl_kill(top);
}
