/* Layered attribute calls against a window that belongs to ANOTHER PROCESS (a helper process of the same user, same integrity
 * and desktop; it creates the window, publishes its handle and keeps pumping messages until told to leave or killed).
 * Measures that SetLayeredWindowAttributes, GetLayeredWindowAttributes and UpdateLayeredWindow have no ownership test: they
 * succeed or fail purely on window state, with the same exact errors as for a local window, that the owner process sees the
 * stored values, that SetWindowLong on the style bits is allowed across processes while DestroyWindow is refused with
 * access denied (5), and that after the owner process is gone the handle is invalid (1400) for every entry point.
 * The helper is always killed and waited for at the end of each test. */
#include "xl.h"

CHILD(xwin) {
    const char *kind = argc > 1 ? argv[1] : "L";
    if (!UN(NtUserSetLayeredWindowAttributes) || !U(CreateWindowExA)) return 3;
    HWND top = NULL, w;
    if (kind[0] == 'C') { top = xl_mk(WS_POPUP, 0, NULL); w = top ? xl_mk(WS_CHILD, XL_EXL, top) : NULL; }   /* a layered child of a plain top-level window */
    else w = xl_mk(WS_POPUP, kind[0] == 'P' ? 0 : XL_EXL, NULL);
    if (!w) { printf("FAIL create\n"); return 4; }
    (void)top;
    if (kind[0] == 'S') xl_set(w, 0x112233, 100, 3);
    if (kind[0] == 'U') { struct xl_img im; if (xl_img_make(&im, 8, 8)) { if (!xl_ulw(w, &im, ULW_ALPHA)) printf("FAIL child ulw\n"); } }
    printf("READY %llx %lu\n", (unsigned long long)(ULONG_PTR)w, GetCurrentProcessId());
    char line[128]; int n = 0; MSG m;
    for (;;) {
        while (U(PeekMessageA)(&m, NULL, 0, 0, PM_REMOVE)) { U(TranslateMessage)(&m); U(DispatchMessageA)(&m); }
        DWORD avail = 0; HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
        if (!PeekNamedPipe(in, NULL, 0, NULL, &avail, NULL)) return 0;
        if (!avail) { Sleep(5); continue; }
        char c; DWORD got; ReadFile(in, &c, 1, &got, NULL);
        if (c != '\n') { if (n < 120) line[n++] = c; continue; }
        line[n] = 0; n = 0;
        if (!strcmp(line, "quit")) return 0;
        if (!strcmp(line, "get")) {
            struct xl_attr a; xl_attr_clear(&a); SET_ERR(XL_ERR_SENT); BOOL r = xl_getall(w, &a);
            printf("GET r=%d err=%lu key=%lx alpha=%u flags=%lx ex=%lx\n", r, GetLastError(), (unsigned long)a.key, a.alpha, a.flags, xl_exstyle(w));
        }
    }
}

struct xp { struct xl_proc p; HWND w; DWORD pid; int open; };
struct xview { int r; DWORD err; unsigned long key, alpha, flags, ex; };
static int xp_open_exe(struct xp *x, const char *kind, const char *exe) {
    char line[256]; unsigned long long h = 0; unsigned long pid = 0;
    memset(x, 0, sizeof *x);
    if (!exe || !xl_spawn(&x->p, exe, "xwin", kind)) return 0;
    if (!xl_readline(&x->p, line, sizeof line, 30000) || sscanf(line, "READY %llx %lu", &h, &pid) != 2) { xl_kill_proc(&x->p); return 0; }
    x->w = (HWND)(ULONG_PTR)h; x->pid = pid; x->open = 1;
    return 1;
}
static void xp_close(struct xp *x) { if (x->open) { xl_kill_proc(&x->p); x->open = 0; } }
static struct xview xp_view(struct xp *x) {      /* the owner's own Get result, evaluated inside the owner */
    struct xview v; char line[256]; memset(&v, 0, sizeof v); v.r = -1;
    xl_send(&x->p, "get");
    if (xl_readline(&x->p, line, sizeof line, 20000)) {
        unsigned long err = 0;
        sscanf(line, "GET r=%d err=%lu key=%lx alpha=%lu flags=%lx ex=%lx", &v.r, &err, &v.key, &v.alpha, &v.flags, &v.ex);
        v.err = err;
    }
    return v;
}
static int xp_open(struct xp *x, const char *kind) { return xp_open_exe(x, kind, t_self_path()); }
#define XP(kind, x) XL_NEED(); struct xp x; if (!xp_open(&x, kind)) { t_skip("cannot start the helper process"); return; }
#define XEND(x) xp_close(&x)

TEST(xproc_window_belongs_to_other_process) {
    XP("L", x); DWORD pid = 0; DWORD tid = U(GetWindowThreadProcessId)(x.w, &pid);
    CHECK(tid != 0); EXPECT_EQ(pid, x.pid); EXPECT_NE(pid, GetCurrentProcessId()); CHECK(U(IsWindow)(x.w)); CHECK(xl_is_lay(x.w)); XEND(x);
}
TEST(xproc_get_fresh_layered_fails_untouched) {
    XP("L", x); struct xl_attr a; xl_attr_clear(&a); SET_ERR(XL_ERR_SENT);
    CHECK(!xl_getall(x.w, &a)); EXPECT_ERR(XL_ERR_SENT); EXPECT_EQ(a.key, 0xdeadbeef); EXPECT_EQ(a.flags, 0xfeedf00d); XEND(x);
}
TEST(xproc_set_succeeds) {
    XP("L", x); SET_ERR(XL_ERR_SENT); CHECK(xl_set(x.w, 0x0a0b0c, 55, 3)); EXPECT_EQ(GetLastError(), XL_ERR_SENT); XEND(x);
}
TEST(xproc_set_then_get_roundtrip) {
    XP("L", x); CHECK(xl_set(x.w, 0x0a0b0c, 55, 3)); struct xl_attr a; CHECK(xl_getall(x.w, &a));
    EXPECT_EQ(a.key, 0x0a0b0c); EXPECT_EQ(a.alpha, 55); EXPECT_EQ(a.flags, 3); XEND(x);
}
TEST(xproc_owner_sees_values_set_by_us) {
    XP("L", x); CHECK(xl_set(x.w, 0x0a0b0c, 55, 3)); struct xview v = xp_view(&x);
    EXPECT_EQ(v.r, 1); EXPECT_EQ(v.key, 0x0a0b0c); EXPECT_EQ(v.alpha, 55); EXPECT_EQ(v.flags, 3); XEND(x);
}
TEST(xproc_owner_fresh_view_fails_before_set) {
    XP("L", x); struct xview v = xp_view(&x); EXPECT_EQ(v.r, 0); EXPECT_EQ(v.err, XL_ERR_SENT); XEND(x);
}
TEST(xproc_preset_values_readable_by_us) {
    XP("S", x); struct xl_attr a; CHECK(xl_getall(x.w, &a));
    EXPECT_EQ(a.key, 0x112233); EXPECT_EQ(a.alpha, 100); EXPECT_EQ(a.flags, 3); XEND(x);
}
TEST(xproc_set_overwrites_preset_values) {
    XP("S", x); CHECK(xl_set(x.w, 7, 8, 3)); struct xview v = xp_view(&x); EXPECT_EQ(v.key, 7); EXPECT_EQ(v.alpha, 8); XEND(x);
}
TEST(xproc_set_alpha_only_keeps_nothing_else_required) {
    XP("L", x); CHECK(xl_set(x.w, 0, 200, LWA_ALPHA)); struct xl_attr a; CHECK(xl_getall(x.w, &a)); EXPECT_EQ(a.alpha, 200); EXPECT_EQ(a.flags, LWA_ALPHA); XEND(x);
}
TEST(xproc_set_bad_flags_87) {
    XP("L", x); SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_set(x.w, 1, 2, 0x10), ERROR_INVALID_PARAMETER); XEND(x);
}
TEST(xproc_set_plain_window_87) {
    XP("P", x); SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_set(x.w, 1, 2, 3), ERROR_INVALID_PARAMETER); XEND(x);
}
TEST(xproc_get_plain_window_87) {
    XP("P", x); struct xl_attr a; SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_getall(x.w, &a), ERROR_INVALID_PARAMETER); XEND(x);
}
TEST(xproc_owner_view_unchanged_by_failed_set) {
    XP("S", x); CHECK(!xl_set(x.w, 1, 2, 0x10)); struct xview v = xp_view(&x); EXPECT_EQ(v.key, 0x112233); EXPECT_EQ(v.alpha, 100); XEND(x);
}

/* ---- UpdateLayeredWindow on a remote window ---------------------------------------------------------------------- */
#define XU(x, im) XP("L", x); struct xl_img im; if (!xl_img_make(&im, 8, 8)) { xp_close(&x); t_skip("image"); return; }
TEST(xproc_ulw_fresh_succeeds) {
    XU(x, im); SET_ERR(XL_ERR_SENT); CHECK(xl_ulw(x.w, &im, ULW_ALPHA)); EXPECT_EQ(GetLastError(), XL_ERR_SENT); xl_img_free(&im); XEND(x);
}
TEST(xproc_ulw_moves_and_sizes_remote_window) {
    XU(x, im); POINT d = { -29000, -29000 }, s = { 0, 0 }; SIZE sz = { 24, 20 }; BLENDFUNCTION b = xl_blend(255); RECT rc;
    CHECK(U(UpdateLayeredWindow)(x.w, NULL, &d, &sz, im.dc, &s, 0, &b, ULW_ALPHA)); U(GetWindowRect)(x.w, &rc);
    EXPECT_EQ(rc.left, (ULONG_PTR)-29000); EXPECT_EQ(rc.top, (ULONG_PTR)-29000); EXPECT_EQ(rc.right - rc.left, 24); EXPECT_EQ(rc.bottom - rc.top, 20);
    xl_img_free(&im); XEND(x);
}
TEST(xproc_ulw_makes_get_fail_for_us_and_owner) {
    XU(x, im); CHECK(xl_ulw(x.w, &im, ULW_ALPHA)); struct xl_attr a; SET_ERR(XL_ERR_SENT); CHECK(!xl_getall(x.w, &a)); EXPECT_ERR(XL_ERR_SENT);
    struct xview v = xp_view(&x); EXPECT_EQ(v.r, 0); EXPECT_EQ(v.err, XL_ERR_SENT); xl_img_free(&im); XEND(x);
}
TEST(xproc_ulw_then_set_succeeds_and_owner_sees_it) {
    XU(x, im); CHECK(xl_ulw(x.w, &im, ULW_ALPHA)); CHECK(xl_set(x.w, 7, 8, 3)); struct xview v = xp_view(&x);
    EXPECT_EQ(v.r, 1); EXPECT_EQ(v.key, 7); EXPECT_EQ(v.alpha, 8); xl_img_free(&im); XEND(x);
}
TEST(xproc_set_then_ulw_fails_87) {
    XU(x, im); CHECK(xl_set(x.w, 7, 8, 3)); SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_ulw(x.w, &im, ULW_ALPHA), ERROR_INVALID_PARAMETER); xl_img_free(&im); XEND(x);
}
TEST(xproc_ulw_preset_by_owner_then_ours_succeeds) {
    XP("U", x); struct xl_img im; if (!xl_img_make(&im, 8, 8)) { xp_close(&x); t_skip("image"); return; }
    CHECK(xl_ulw(x.w, &im, ULW_ALPHA)); struct xl_attr a; SET_ERR(XL_ERR_SENT); CHECK(!xl_getall(x.w, &a)); EXPECT_ERR(XL_ERR_SENT); xl_img_free(&im); XEND(x);
}
TEST(xproc_ulw_preset_by_owner_then_set_ok) {
    XP("U", x); CHECK(xl_set(x.w, 3, 4, 3)); struct xl_attr a; CHECK(xl_getall(x.w, &a)); EXPECT_EQ(a.alpha, 4); XEND(x);
}
TEST(xproc_ulw_bad_flags_87) {
    XU(x, im); SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_ulw(x.w, &im, 0x10), ERROR_INVALID_PARAMETER); xl_img_free(&im); XEND(x);
}
TEST(xproc_ulw_plain_window_87) {
    XP("P", x); struct xl_img im; if (!xl_img_make(&im, 8, 8)) { xp_close(&x); t_skip("image"); return; }
    SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_ulw(x.w, &im, ULW_ALPHA), ERROR_INVALID_PARAMETER); xl_img_free(&im); XEND(x);
}
TEST(xproc_ulw_negative_size_87) {
    XU(x, im); POINT s = { 0, 0 }; SIZE sz = { -1, 4 }; BLENDFUNCTION b = xl_blend(255); SET_ERR(XL_ERR_SENT);
    EXPECT_FAIL(!U(UpdateLayeredWindow)(x.w, NULL, NULL, &sz, im.dc, &s, 0, &b, ULW_ALPHA), ERROR_INVALID_PARAMETER); xl_img_free(&im); XEND(x);
}

/* ---- window operations across the process boundary -------------------------------------------------------------------- */
TEST(xproc_setwindowlong_clear_layered_bit) {
    XP("S", x); LONG ex = xl_exstyle(x.w); LONG r = U(SetWindowLongA)(x.w, GWL_EXSTYLE, ex & ~XL_EXL);
    EXPECT_EQ(r, ex); CHECK(!xl_is_lay(x.w)); struct xview v = xp_view(&x); EXPECT_EQ(v.r, 0); EXPECT_EQ(v.err, ERROR_INVALID_PARAMETER); EXPECT_EQ(v.ex & XL_EXL, 0); XEND(x);
}
TEST(xproc_setwindowlong_readd_bit_drops_values) {
    XP("S", x); LONG ex = xl_exstyle(x.w); U(SetWindowLongA)(x.w, GWL_EXSTYLE, ex & ~XL_EXL); U(SetWindowLongA)(x.w, GWL_EXSTYLE, ex);
    struct xl_attr a; SET_ERR(XL_ERR_SENT); CHECK(!xl_getall(x.w, &a)); EXPECT_ERR(XL_ERR_SENT); XEND(x);
}
TEST(xproc_setwindowlong_add_layered_to_plain_window) {
    XP("P", x); LONG ex = xl_exstyle(x.w); LONG r = U(SetWindowLongA)(x.w, GWL_EXSTYLE, ex | XL_EXL);
    EXPECT_EQ(r, ex); CHECK(xl_is_lay(x.w)); CHECK(xl_set(x.w, 1, 2, 3)); struct xl_attr a; CHECK(xl_getall(x.w, &a)); EXPECT_EQ(a.alpha, 2); XEND(x);
}
TEST(xproc_setwindowlong_unrelated_style_keeps_values) {
    XP("S", x); U(SetWindowLongA)(x.w, GWL_EXSTYLE, xl_exstyle(x.w) | WS_EX_TRANSPARENT); struct xl_attr a; CHECK(xl_getall(x.w, &a)); EXPECT_EQ(a.alpha, 100); XEND(x);
}
TEST(xproc_setwindowpos_keeps_values_and_resizes) {
    XP("S", x); RECT rc; CHECK(U(SetWindowPos)(x.w, NULL, -28000, -28000, 30, 30, SWP_NOZORDER | SWP_NOACTIVATE));
    U(GetWindowRect)(x.w, &rc); EXPECT_EQ(rc.left, (ULONG_PTR)-28000); EXPECT_EQ(rc.right - rc.left, 30);
    struct xview v = xp_view(&x); EXPECT_EQ(v.r, 1); EXPECT_EQ(v.alpha, 100); XEND(x);
}
TEST(xproc_destroywindow_denied_5) {
    XP("L", x); SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!U(DestroyWindow)(x.w), ERROR_ACCESS_DENIED); CHECK(U(IsWindow)(x.w)); XEND(x);
}
TEST(xproc_user32_front_doors) {
    XP("L", x); CHECK(U(SetLayeredWindowAttributes)(x.w, 0x10, 20, 3)); COLORREF k; BYTE a; DWORD f; CHECK(U(GetLayeredWindowAttributes)(x.w, &k, &a, &f));
    EXPECT_EQ(k, 0x10); EXPECT_EQ(a, 20); EXPECT_EQ(f, 3); XEND(x);
}
TEST(xproc_two_helpers_independent) {
    XP("L", x); struct xp y; if (!xp_open(&y, "L")) { xp_close(&x); t_skip("second helper"); return; }
    CHECK(xl_set(x.w, 1, 11, 3)); CHECK(xl_set(y.w, 2, 22, 3)); struct xview vx = xp_view(&x), vy = xp_view(&y);
    EXPECT_EQ(vx.alpha, 11); EXPECT_EQ(vy.alpha, 22); xp_close(&y); XEND(x);
}

/* ---- owner process gone ---------------------------------------------------------------------------------------------------- */
TEST(xproc_killed_owner_window_invalid) {
    XP("S", x); HWND w = x.w; xp_close(&x); CHECK(!U(IsWindow)(w));
}
TEST(xproc_killed_owner_set_1400) {
    XP("S", x); HWND w = x.w; xp_close(&x); SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_set(w, 1, 2, 3), ERROR_INVALID_WINDOW_HANDLE);
}
TEST(xproc_killed_owner_get_1400) {
    XP("S", x); HWND w = x.w; xp_close(&x); struct xl_attr a; SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_getall(w, &a), ERROR_INVALID_WINDOW_HANDLE);
}
TEST(xproc_killed_owner_ulw_1400) {
    XP("S", x); HWND w = x.w; xp_close(&x); struct xl_img im; xl_img_make(&im, 8, 8);
    SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_ulw(w, &im, ULW_ALPHA), ERROR_INVALID_WINDOW_HANDLE); xl_img_free(&im);
}
TEST(xproc_killed_owner_setwindowlong_1400) {
    XP("S", x); HWND w = x.w; xp_close(&x); SET_ERR(XL_ERR_SENT); LONG r = U(SetWindowLongA)(w, GWL_EXSTYLE, 0); EXPECT_EQ(r, 0); EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
}
TEST(xproc_orderly_quit_invalidates_window) {
    XP("L", x); DWORD code = xl_finish(&x.p); x.open = 0; EXPECT_EQ(code, 0); CHECK(!U(IsWindow)(x.w));
}

/* ---- owner process frozen (its only thread suspended, so it cannot pump messages) ---------------------------------------- */
/* Set, Get and UpdateLayeredWindow are carried out by the window system on the caller's thread, so a frozen owner changes nothing. */
#define FROZEN(kind, x) XP(kind, x); if (SuspendThread(x.p.hthread) == (DWORD)-1) { xp_close(&x); t_skip("cannot suspend the helper"); return; }
#define THAW(x) do { ResumeThread(x.p.hthread); xp_close(&x); } while (0)
TEST(xproc_frozen_owner_set_get) {
    FROZEN("L", x); SET_ERR(XL_ERR_SENT); CHECK(xl_set(x.w, 0x0a0b0c, 55, 3)); EXPECT_EQ(GetLastError(), XL_ERR_SENT);
    struct xl_attr a; CHECK(xl_getall(x.w, &a)); EXPECT_EQ(a.key, 0x0a0b0c); EXPECT_EQ(a.alpha, 55); EXPECT_EQ(a.flags, 3); THAW(x);
}
TEST(xproc_frozen_owner_fresh_get_untouched) {
    FROZEN("L", x); struct xl_attr a; xl_attr_clear(&a); SET_ERR(XL_ERR_SENT); CHECK(!xl_getall(x.w, &a)); EXPECT_ERR(XL_ERR_SENT); EXPECT_EQ(a.flags, 0xfeedf00d); THAW(x);
}
TEST(xproc_frozen_owner_ulw_fresh) {
    FROZEN("L", x); struct xl_img im; if (!xl_img_make(&im, 8, 8)) { THAW(x); t_skip("image"); return; }
    SET_ERR(XL_ERR_SENT); CHECK(xl_ulw(x.w, &im, ULW_ALPHA)); xl_img_free(&im); THAW(x);
}
TEST(xproc_frozen_owner_errors_unchanged) {
    FROZEN("P", x); struct xl_attr a; SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_set(x.w, 1, 2, 3), ERROR_INVALID_PARAMETER);
    SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_getall(x.w, &a), ERROR_INVALID_PARAMETER); THAW(x);
}

/* ---- a layered CHILD window of another process (helper started from the manifested copy of this exe) ----------------------- */
#define XC(x) XL_NEED(); struct xp x; if (!xp_open_exe(&x, "C", xl_exe_copy(1))) { t_skip("cannot start the manifested helper"); return; }
TEST(xproc_layered_child_is_child_of_other_process) {
    XC(x); CHECK(xl_is_lay(x.w)); CHECK((U(GetWindowLongA)(x.w, GWL_STYLE) & WS_CHILD) != 0); CHECK(U(GetParent)(x.w) != NULL); XEND(x);
}
TEST(xproc_layered_child_fresh_get_untouched) {
    XC(x); struct xl_attr a; xl_attr_clear(&a); SET_ERR(XL_ERR_SENT); CHECK(!xl_getall(x.w, &a)); EXPECT_ERR(XL_ERR_SENT); EXPECT_EQ(a.flags, 0xfeedf00d); XEND(x);
}
TEST(xproc_layered_child_set_get_owner_view) {
    XC(x); SET_ERR(XL_ERR_SENT); CHECK(xl_set(x.w, 0x010203, 77, 3)); EXPECT_EQ(GetLastError(), XL_ERR_SENT);
    struct xl_attr a; CHECK(xl_getall(x.w, &a)); EXPECT_EQ(a.key, 0x010203); EXPECT_EQ(a.alpha, 77); EXPECT_EQ(a.flags, 3);
    struct xview v = xp_view(&x); EXPECT_EQ(v.r, 1); EXPECT_EQ(v.alpha, 77); XEND(x);
}
TEST(xproc_layered_child_ulw_then_set_then_ulw) {
    XC(x); struct xl_img im; if (!xl_img_make(&im, 8, 8)) { xp_close(&x); t_skip("image"); return; }
    CHECK(xl_ulw(x.w, &im, ULW_ALPHA)); CHECK(xl_set(x.w, 1, 2, 3)); SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_ulw(x.w, &im, ULW_ALPHA), ERROR_INVALID_PARAMETER);
    xl_img_free(&im); XEND(x);
}
TEST(xproc_layered_child_destroy_denied_5) {
    XC(x); SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!U(DestroyWindow)(x.w), ERROR_ACCESS_DENIED); CHECK(U(IsWindow)(x.w)); XEND(x);
}
TEST(xproc_layered_child_killed_owner_1400) {
    XC(x); HWND w = x.w; xp_close(&x); SET_ERR(XL_ERR_SENT); EXPECT_FAIL(!xl_set(w, 1, 2, 3), ERROR_INVALID_WINDOW_HANDLE);
}
