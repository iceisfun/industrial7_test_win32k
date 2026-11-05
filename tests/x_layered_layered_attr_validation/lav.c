/* x_layered_layered_attr_validation: argument validation and exact return/last-error values of the layered
 * window attribute services, called both straight through the win32u stubs (NtUserSetLayeredWindowAttributes,
 * NtUserGetLayeredWindowAttributes) and through the user32 front doors (SetLayeredWindowAttributes,
 * GetLayeredWindowAttributes).
 *
 * Measures:
 *  - the handle matrix: NULL, small and large garbage values, handles with the upper half set, handles of other
 *    object types (menu, monitor, window station, pseudo process/thread handles, GDI brush, cursor), destroyed
 *    windows (plain, layered with attributes, owned by an exited thread, slot reused by a newer window) all
 *    fail with ERROR_INVALID_WINDOW_HANDLE (1400); the desktop window and message-only windows are valid
 *    handles that are not layered and fail with ERROR_INVALID_PARAMETER (87);
 *  - the flag word: 0..3 are accepted on a layered window, every other bit pattern fails with 87 whether or not
 *    the window is layered, and never reaches the stored state; a bad handle wins over bad flags;
 *  - the WS_EX_LAYERED requirement on popup, overlapped, child, message-only and desktop windows, after the
 *    style is removed, re-added or cannot be added;
 *  - what is stored: crKey is kept as a full 32-bit value without validation, bAlpha as one byte, each only when
 *    its flag bit is present, flags 0 stores nothing; Get masks the flag word to two bits;
 *  - the Get out pointers: each of the three is optional, nothing is written on failure;
 *  - last-error behaviour: successful calls and Get on a layered window that was never given attributes leave
 *    the caller's last-error value untouched;
 *  - cross-thread use inside the process.
 *
 * Determinism: only exact status values and relations over values the test itself stored are asserted. Window
 * handles never appear in the output. All windows are private, hidden, owned by this process, destroyed at the
 * end of the test; nothing outside the process is modified. Behaviour that depends on the application manifest
 * or on other processes (layered child windows, SetParent of a layered window, the shell window) is only noted
 * or checked through a relation to the window's own style bits.
 */
#include "t.h"
#include "k.h"

UDECL(BOOL, NtUserSetLayeredWindowAttributes, (HWND, COLORREF, BYTE, DWORD));
UDECL(BOOL, NtUserGetLayeredWindowAttributes, (HWND, COLORREF *, BYTE *, DWORD *));

#define SENT 0x5A5Au
#ifndef ERROR_INVALID_WINDOW_HANDLE
#define ERROR_INVALID_WINDOW_HANDLE 1400
#endif

/* ---- helpers ------------------------------------------------------------------------------------ */
static LRESULT CALLBACK lav_wp(HWND h, UINT m, WPARAM w, LPARAM l) { return U(DefWindowProcA)(h, m, w, l); }
static const char *lav_class(void) {
    static char n[64];
    if (!n[0]) {
        WNDCLASSA wc; memset(&wc, 0, sizeof wc);
        wsprintfA(n, "win32kt_lav_%lu", GetCurrentProcessId());
        wc.lpfnWndProc = lav_wp; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = n;
        if (!U(RegisterClassA)(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) { n[0] = 0; return NULL; }
    }
    return n;
}
static const char *lav_shadow_class(void) {
    static char n[64];
    if (!n[0]) {
        WNDCLASSA wc; memset(&wc, 0, sizeof wc);
        wsprintfA(n, "win32kt_lavsh_%lu", GetCurrentProcessId());
        wc.style = CS_DROPSHADOW; wc.lpfnWndProc = lav_wp; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = n;
        if (!U(RegisterClassA)(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) { n[0] = 0; return NULL; }
    }
    return n;
}
static HWND mk(DWORD style, DWORD ex, HWND parent) {
    const char *c = lav_class();
    if (!c) return NULL;
    return U(CreateWindowExA)(ex, c, "lav", style, 10, 10, 40, 40, parent, NULL, GetModuleHandleA(NULL), NULL);
}
static HWND mk_msg(DWORD ex) {
    const char *c = lav_class();
    if (!c) return NULL;
    return U(CreateWindowExA)(ex, c, "lavm", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, GetModuleHandleA(NULL), NULL);
}
static void kill(HWND h) { if (h) U(DestroyWindow)(h); }

#define NEED() do { REQUIRE(UN(NtUserSetLayeredWindowAttributes)); REQUIRE(UN(NtUserGetLayeredWindowAttributes)); \
    REQUIRE(U(CreateWindowExA)); REQUIRE(U(SetLayeredWindowAttributes)); REQUIRE(U(GetLayeredWindowAttributes)); \
    REQUIRE(U(RegisterClassA)); REQUIRE(U(DestroyWindow)); REQUIRE(U(GetWindowLongPtrA)); REQUIRE(U(SetWindowLongPtrA)); \
    if (!lav_class()) { t_skip("class registration failed"); return; } } while (0)

/* route 0 = win32u stub, route 1 = user32 front door */
static BOOL do_set(int route, HWND h, COLORREF k, BYTE a, DWORD f) {
    return route ? U(SetLayeredWindowAttributes)(h, k, a, f) : UN(NtUserSetLayeredWindowAttributes)(h, k, a, f);
}
static BOOL do_get(int route, HWND h, COLORREF *k, BYTE *a, DWORD *f) {
    return route ? U(GetLayeredWindowAttributes)(h, k, a, f) : UN(NtUserGetLayeredWindowAttributes)(h, k, a, f);
}
static const char *rn(int route) { return route ? "u32" : "nt"; }

/* A Set that must fail with err; the last-error value is primed with a sentinel so a missing store shows up. */
static void set_fails(int route, HWND h, COLORREF k, BYTE a, DWORD f, DWORD err, const char *what) {
    SetLastError(SENT);
    BOOL r = do_set(route, h, k, a, f);
    DWORD e = GetLastError();
    CHECKF(!r, "%s %s: Set(flags=0x%lx) unexpectedly succeeded", what, rn(route), f);
    CHECKF(e == err, "%s %s: Set(flags=0x%lx) error %lu want %lu", what, rn(route), f, e, err);
}
/* A Get with all three pointers that must fail with err and leave the outputs alone. */
static void get_fails(int route, HWND h, DWORD err, const char *what) {
    COLORREF k = 0x11223344; BYTE a = 0x99; DWORD f = 0xFEEDBEEF;
    SetLastError(SENT);
    BOOL r = do_get(route, h, &k, &a, &f);
    DWORD e = GetLastError();
    CHECKF(!r, "%s %s: Get unexpectedly succeeded", what, rn(route));
    CHECKF(e == err, "%s %s: Get error %lu want %lu", what, rn(route), e, err);
    CHECKF(k == 0x11223344 && a == 0x99 && f == 0xFEEDBEEF, "%s %s: Get wrote outputs on failure", what, rn(route));
    SetLastError(SENT);
    r = do_get(route, h, NULL, NULL, NULL);
    e = GetLastError();
    CHECKF(!r && e == err, "%s %s: Get(NULL,NULL,NULL) r=%d error %lu want %lu", what, rn(route), r, e, err);
}
/* Full failure matrix for a handle that cannot be used at all (err for every flag value). */
static void handle_all_fail(HWND h, DWORD err, const char *what) {
    if (err == ERROR_INVALID_WINDOW_HANDLE && U(IsWindow) && U(IsWindow)(h)) { t_note("%s: value coincides with a live window, skipped", what); return; }
    static const DWORD fl[] = { 0, 1, 2, 3, 4, 0x80, 0xFFFFFFFC, 0xFFFFFFFF };
    for (int route = 0; route < 2; route++) {
        for (int i = 0; i < (int)(sizeof fl / sizeof fl[0]); i++) set_fails(route, h, 0x00112233, 0x44, fl[i], err, what);
        get_fails(route, h, err, what);
    }
}
/* Get must succeed and report exactly (k,a,f). */
static void get_is(int route, HWND h, COLORREF k, BYTE a, DWORD f, const char *what) {
    COLORREF gk = 0xAAAAAAAA; BYTE ga = 0xAB; DWORD gf = 0xFFFFFFFF;
    SetLastError(SENT);
    BOOL r = do_get(route, h, &gk, &ga, &gf);
    CHECKF(r, "%s %s: Get failed, error %lu", what, rn(route), GetLastError());
    if (!r) return;
    CHECKF(gk == k, "%s %s: key 0x%lx want 0x%lx", what, rn(route), gk, k);
    CHECKF(ga == a, "%s %s: alpha 0x%x want 0x%x", what, rn(route), ga, a);
    CHECKF(gf == f, "%s %s: flags 0x%lx want 0x%lx", what, rn(route), gf, f);
    EXPECT_ERR(SENT);
}
static void set_ok(int route, HWND h, COLORREF k, BYTE a, DWORD f, const char *what) {
    SetLastError(SENT);
    BOOL r = do_set(route, h, k, a, f);
    DWORD e = GetLastError();
    CHECKF(r, "%s %s: Set(flags=0x%lx) failed, error %lu", what, rn(route), f, e);
    CHECKF(e == SENT, "%s %s: Set success changed last error to %lu", what, rn(route), e);
}
static BOOL has_layered(HWND h) { return (U(GetWindowLongPtrA)(h, GWL_EXSTYLE) & WS_EX_LAYERED) != 0; }

/* ---- handle matrix ------------------------------------------------------------------------------ */
#define HTEST(nm, hexpr) TEST(lav_handle_##nm) { NEED(); HWND h = (HWND)(hexpr); handle_all_fail(h, ERROR_INVALID_WINDOW_HANDLE, #nm); }
HTEST(null, (ULONG_PTR)0)
HTEST(garbage_777777, (ULONG_PTR)0x777777)
HTEST(minus1, (ULONG_PTR)-1)
HTEST(minus2, (ULONG_PTR)-2)
HTEST(message_parent_value, (ULONG_PTR)-3)
HTEST(one, (ULONG_PTR)1)
HTEST(two, (ULONG_PTR)2)
HTEST(ffff, (ULONG_PTR)0xFFFF)
HTEST(x10000, (ULONG_PTR)0x10000)
HTEST(high_word_only, (ULONG_PTR)0xFFFF0000)
HTEST(bit31, (ULONG_PTR)0x80000000ull)
HTEST(all32, (ULONG_PTR)0xFFFFFFFFull)
HTEST(bit63, (ULONG_PTR)0x8000000000000000ull)
HTEST(max_ptr, ~(ULONG_PTR)0 - 0x10000)

TEST(lav_handle_menu) {
    NEED(); REQUIRE(U(CreatePopupMenu)); REQUIRE(U(DestroyMenu));
    HMENU m = U(CreatePopupMenu)();
    if (!m) { t_skip("no menu"); return; }
    handle_all_fail((HWND)m, ERROR_INVALID_WINDOW_HANDLE, "menu");
    U(DestroyMenu)(m);
}
TEST(lav_handle_monitor) {
    NEED(); REQUIRE(U(MonitorFromPoint));
    POINT pt = { 0, 0 };
    HMONITOR mon = U(MonitorFromPoint)(pt, MONITOR_DEFAULTTOPRIMARY);
    if (!mon) { t_skip("no monitor"); return; }
    handle_all_fail((HWND)mon, ERROR_INVALID_WINDOW_HANDLE, "monitor");
}
TEST(lav_handle_window_station) {
    NEED(); REQUIRE(U(GetProcessWindowStation));
    HWINSTA ws = U(GetProcessWindowStation)();
    if (!ws) { t_skip("no window station"); return; }
    handle_all_fail((HWND)ws, ERROR_INVALID_WINDOW_HANDLE, "winsta");
}
TEST(lav_handle_pseudo_process) { NEED(); handle_all_fail((HWND)GetCurrentProcess(), ERROR_INVALID_WINDOW_HANDLE, "curproc"); }
TEST(lav_handle_pseudo_thread) { NEED(); handle_all_fail((HWND)GetCurrentThread(), ERROR_INVALID_WINDOW_HANDLE, "curthread"); }
TEST(lav_handle_real_kernel_handle) {
    NEED();
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    handle_all_fail((HWND)ev, ERROR_INVALID_WINDOW_HANDLE, "event");
    CloseHandle(ev);
}
TEST(lav_handle_gdi_brush) {
    NEED(); REQUIRE(U(GetStockObject));
    handle_all_fail((HWND)U(GetStockObject)(WHITE_BRUSH), ERROR_INVALID_WINDOW_HANDLE, "brush");
}
TEST(lav_handle_cursor) {
    NEED(); REQUIRE(U(LoadCursorA));
    HCURSOR c = U(LoadCursorA)(NULL, IDC_ARROW);
    if (!c) { t_skip("no cursor"); return; }
    handle_all_fail((HWND)c, ERROR_INVALID_WINDOW_HANDLE, "cursor");
}

/* handles derived from a live window: only the exact value is a window */
static void derived_handle(int which) {
    NEED();
    HWND w = mk(WS_POPUP, WS_EX_LAYERED, NULL);
    if (!w) { t_skip("no window"); return; }
    ULONG_PTR v = (ULONG_PTR)w;
    if (!(v >> 16)) { t_skip("handle has no unique half"); kill(w); return; }
    set_ok(0, w, 0, 0x10, LWA_ALPHA, "base");
    ULONG_PTR d = 0;
    switch (which) {
    case 0: d = v & 0xFFFF; break;
    case 1: d = v & 0xFFFF0000; break;
    case 2: d = v + 1; break;
    case 3: d = v - 1; break;
    case 4: d = v + 0x10000; break;
    case 5: d = v ^ 0x80000000ull; break;
    }
    handle_all_fail((HWND)d, ERROR_INVALID_WINDOW_HANDLE, "derived");
    get_is(0, w, 0, 0x10, LWA_ALPHA, "base survives");
    kill(w);
}
TEST(lav_handle_derived_low_word) { derived_handle(0); }
TEST(lav_handle_derived_high_word) { derived_handle(1); }
TEST(lav_handle_derived_plus_one) { derived_handle(2); }
TEST(lav_handle_derived_minus_one) { derived_handle(3); }
TEST(lav_handle_derived_plus_unique) { derived_handle(4); }
TEST(lav_handle_derived_bit31_flipped) { derived_handle(5); }

/* the upper half of the 64-bit handle value is ignored (handles are 32-bit quantities) */
TEST(lav_handle_upper_half_ignored) {
    NEED();
    HWND w = mk(WS_POPUP, WS_EX_LAYERED, NULL);
    if (!w) { t_skip("no window"); return; }
    ULONG_PTR v = (ULONG_PTR)w;
    for (int route = 0; route < 2; route++) {
        HWND a = (HWND)(v | (1ull << 32)), b = (HWND)(v | 0xFFFFFFFF00000000ull);
        set_ok(route, a, 0x00010203, 0x31, LWA_ALPHA | LWA_COLORKEY, "bit32");
        get_is(route, w, 0x00010203, 0x31, 3, "read via plain");
        get_is(route, a, 0x00010203, 0x31, 3, "read via bit32");
        set_ok(route, b, 0x00040506, 0x32, LWA_ALPHA | LWA_COLORKEY, "signext");
        get_is(route, b, 0x00040506, 0x32, 3, "read via signext");
        get_is(route, w, 0x00040506, 0x32, 3, "read via plain 2");
    }
    kill(w);
}

/* ---- destroyed windows -------------------------------------------------------------------------- */
TEST(lav_dead_plain_popup) {
    NEED();
    HWND w = mk(WS_POPUP, 0, NULL);
    if (!w) { t_skip("no window"); return; }
    kill(w);
    handle_all_fail(w, ERROR_INVALID_WINDOW_HANDLE, "dead plain");
}
TEST(lav_dead_layered_without_attributes) {
    NEED();
    HWND w = mk(WS_POPUP, WS_EX_LAYERED, NULL);
    if (!w) { t_skip("no window"); return; }
    kill(w);
    handle_all_fail(w, ERROR_INVALID_WINDOW_HANDLE, "dead layered");
}
TEST(lav_dead_layered_with_attributes) {
    NEED();
    HWND w = mk(WS_POPUP, WS_EX_LAYERED, NULL);
    if (!w) { t_skip("no window"); return; }
    set_ok(0, w, 0x00AABBCC, 0x80, 3, "pre");
    kill(w);
    handle_all_fail(w, ERROR_INVALID_WINDOW_HANDLE, "dead with attrs");
}
TEST(lav_dead_overlapped_layered) {
    NEED();
    HWND w = mk(WS_OVERLAPPEDWINDOW, WS_EX_LAYERED, NULL);
    if (!w) { t_skip("no window"); return; }
    set_ok(1, w, 1, 2, 3, "pre");
    kill(w);
    handle_all_fail(w, ERROR_INVALID_WINDOW_HANDLE, "dead overlapped");
}
TEST(lav_dead_handle_stays_dead_after_new_window) {
    NEED();
    HWND d = mk(WS_POPUP, WS_EX_LAYERED, NULL);
    if (!d) { t_skip("no window"); return; }
    set_ok(0, d, 0, 1, 2, "pre");
    kill(d);
    HWND n = mk(WS_POPUP, WS_EX_LAYERED, NULL);
    if (!n) { t_skip("no window"); return; }
    set_ok(0, n, 0x00123456, 0x21, 3, "new");
    handle_all_fail(d, ERROR_INVALID_WINDOW_HANDLE, "old handle");
    get_is(0, n, 0x00123456, 0x21, 3, "new unaffected");
    kill(n);
}
TEST(lav_dead_parent_destroys_child_handle) {
    NEED();
    HWND p = mk(WS_POPUP, 0, NULL);
    if (!p) { t_skip("no window"); return; }
    HWND c = mk(WS_CHILD, 0, p);
    if (!c) { kill(p); t_skip("no child"); return; }
    set_fails(0, c, 0, 0, 2, ERROR_INVALID_PARAMETER, "live child");
    kill(p);
    handle_all_fail(c, ERROR_INVALID_WINDOW_HANDLE, "child of destroyed parent");
}
struct dctx { HWND w; HANDLE ev; };
static DWORD WINAPI dthr(void *p) {
    struct dctx *c = p;
    c->w = mk(WS_POPUP, WS_EX_LAYERED, NULL);
    if (c->w) U(SetLayeredWindowAttributes)(c->w, 0x00010203, 0x66, 3);
    SetEvent(c->ev);
    return 0;
}
TEST(lav_dead_window_of_exited_thread) {
    NEED();
    struct dctx cx; cx.w = NULL; cx.ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    HANDLE t = CreateThread(NULL, 0, dthr, &cx, 0, NULL);
    if (!t) { CloseHandle(cx.ev); t_skip("no thread"); return; }
    WaitForSingleObject(t, 10000);
    CloseHandle(t); CloseHandle(cx.ev);
    if (!cx.w) { t_skip("no window"); return; }
    handle_all_fail(cx.w, ERROR_INVALID_WINDOW_HANDLE, "window of exited thread");
}

/* ---- valid handles that are not layered windows ------------------------------------------------- */
static void nonlayered_all_fail(HWND h, const char *what) {
    static const DWORD fl[] = { 0, 1, 2, 3, 4, 8, 0x10, 0x80, 0x10000, 0x80000000, 0xFFFFFFFC, 0xFFFFFFFF };
    for (int route = 0; route < 2; route++) {
        for (int i = 0; i < (int)(sizeof fl / sizeof fl[0]); i++) set_fails(route, h, 0x00ABCDEF, 0x40, fl[i], ERROR_INVALID_PARAMETER, what);
        get_fails(route, h, ERROR_INVALID_PARAMETER, what);
    }
}
TEST(lav_nonlayered_desktop_window) { NEED(); REQUIRE(U(GetDesktopWindow)); nonlayered_all_fail(U(GetDesktopWindow)(), "desktop"); }
TEST(lav_nonlayered_message_only) {
    NEED();
    HWND w = mk_msg(0);
    if (!w) { t_skip("no message-only window"); return; }
    nonlayered_all_fail(w, "message-only");
    kill(w);
}
TEST(lav_nonlayered_popup) { NEED(); HWND w = mk(WS_POPUP, 0, NULL); if (!w) { t_skip("no window"); return; } nonlayered_all_fail(w, "popup"); kill(w); }
TEST(lav_nonlayered_overlapped) { NEED(); HWND w = mk(WS_OVERLAPPEDWINDOW, 0, NULL); if (!w) { t_skip("no window"); return; } nonlayered_all_fail(w, "overlapped"); kill(w); }
TEST(lav_nonlayered_caption_popup) { NEED(); HWND w = mk(WS_POPUP | WS_CAPTION, 0, NULL); if (!w) { t_skip("no window"); return; } nonlayered_all_fail(w, "caption popup"); kill(w); }
TEST(lav_nonlayered_child) {
    NEED();
    HWND p = mk(WS_POPUP, 0, NULL);
    HWND c = p ? mk(WS_CHILD, 0, p) : NULL;
    if (!c) { kill(p); t_skip("no child"); return; }
    nonlayered_all_fail(c, "child");
    kill(p);
}
TEST(lav_nonlayered_child_of_layered_parent) {
    NEED();
    HWND p = mk(WS_POPUP, WS_EX_LAYERED, NULL);
    HWND c = p ? mk(WS_CHILD, 0, p) : NULL;
    if (!c) { kill(p); t_skip("no child"); return; }
    set_ok(0, p, 0, 0x50, LWA_ALPHA, "parent");
    nonlayered_all_fail(c, "child of layered parent");
    get_is(0, p, 0, 0x50, LWA_ALPHA, "parent unchanged");
    kill(p);
}
TEST(lav_nonlayered_owned_popup_of_layered_owner) {
    NEED();
    HWND o = mk(WS_POPUP, WS_EX_LAYERED, NULL);
    HWND p = o ? mk(WS_POPUP, 0, o) : NULL;
    if (!p) { kill(o); t_skip("no owned popup"); return; }
    nonlayered_all_fail(p, "owned popup");
    kill(o);
}
TEST(lav_nonlayered_other_exstyles) {
    NEED();
    static const DWORD ex[] = { WS_EX_TOOLWINDOW, WS_EX_TOPMOST, WS_EX_TRANSPARENT, WS_EX_NOACTIVATE, WS_EX_APPWINDOW, WS_EX_COMPOSITED, WS_EX_NOREDIRECTIONBITMAP };
    for (int i = 0; i < (int)(sizeof ex / sizeof ex[0]); i++) {
        HWND w = mk(WS_POPUP, ex[i], NULL);
        if (!w) { t_note("ex 0x%lx not creatable", ex[i]); continue; }
        CHECKF(!has_layered(w), "ex 0x%lx unexpectedly layered", ex[i]);
        nonlayered_all_fail(w, "other exstyle");
        kill(w);
    }
}
TEST(lav_nonlayered_dialog_frame_class_shadow) {
    NEED();
    const char *c = lav_shadow_class();
    if (!c) { t_skip("no class"); return; }
    HWND w = U(CreateWindowExA)(0, c, "lav", WS_POPUP, 0, 0, 20, 20, NULL, NULL, GetModuleHandleA(NULL), NULL);
    if (!w) { t_skip("no window"); return; }
    nonlayered_all_fail(w, "shadow class");
    kill(w);
}
TEST(lav_layered_shadow_class_accepts_alpha) {
    NEED();
    const char *c = lav_shadow_class();
    if (!c) { t_skip("no class"); return; }
    HWND w = U(CreateWindowExA)(WS_EX_LAYERED, c, "lav", WS_POPUP, 0, 0, 20, 20, NULL, NULL, GetModuleHandleA(NULL), NULL);
    if (!w) { t_skip("no window"); return; }
    set_ok(0, w, 0, 0x77, LWA_ALPHA, "shadow class");
    get_is(0, w, 0, 0x77, LWA_ALPHA, "shadow class");
    kill(w);
}

/* ---- layered style toggling --------------------------------------------------------------------- */
TEST(lav_style_added_after_creation) {
    NEED();
    for (int route = 0; route < 2; route++) {
        HWND w = mk(WS_POPUP, 0, NULL);
        if (!w) { t_skip("no window"); return; }
        set_fails(route, w, 0, 0x10, 2, ERROR_INVALID_PARAMETER, "before add");
        LONG_PTR ex = U(GetWindowLongPtrA)(w, GWL_EXSTYLE);
        U(SetWindowLongPtrA)(w, GWL_EXSTYLE, ex | WS_EX_LAYERED);
        CHECK(has_layered(w));
        /* layered, but never given attributes: Get fails and leaves the last error alone */
        COLORREF k = 1; BYTE a = 2; DWORD f = 3;
        SetLastError(SENT);
        CHECK(!do_get(route, w, &k, &a, &f));
        EXPECT_ERR(SENT);
        CHECK(k == 1 && a == 2 && f == 3);
        set_ok(route, w, 0x00010101, 0x33, 3, "after add");
        get_is(route, w, 0x00010101, 0x33, 3, "after add");
        kill(w);
    }
}
TEST(lav_style_removed_after_attributes) {
    NEED();
    for (int route = 0; route < 2; route++) {
        HWND w = mk(WS_POPUP, WS_EX_LAYERED, NULL);
        if (!w) { t_skip("no window"); return; }
        set_ok(route, w, 0x00010203, 0x44, 3, "pre");
        LONG_PTR ex = U(GetWindowLongPtrA)(w, GWL_EXSTYLE);
        U(SetWindowLongPtrA)(w, GWL_EXSTYLE, ex & ~(LONG_PTR)WS_EX_LAYERED);
        CHECK(!has_layered(w));
        get_fails(route, w, ERROR_INVALID_PARAMETER, "removed");
        set_fails(route, w, 0, 0, 3, ERROR_INVALID_PARAMETER, "removed");
        kill(w);
    }
}
TEST(lav_style_readded_loses_attributes) {
    NEED();
    for (int route = 0; route < 2; route++) {
        HWND w = mk(WS_POPUP, WS_EX_LAYERED, NULL);
        if (!w) { t_skip("no window"); return; }
        set_ok(route, w, 0x00010203, 0x44, 3, "pre");
        LONG_PTR ex = U(GetWindowLongPtrA)(w, GWL_EXSTYLE);
        U(SetWindowLongPtrA)(w, GWL_EXSTYLE, ex & ~(LONG_PTR)WS_EX_LAYERED);
        U(SetWindowLongPtrA)(w, GWL_EXSTYLE, ex);
        CHECK(has_layered(w));
        COLORREF k = 1; BYTE a = 2; DWORD f = 3;
        SetLastError(SENT);
        CHECK(!do_get(route, w, &k, &a, &f));
        EXPECT_ERR(SENT);
        CHECK(k == 1 && a == 2 && f == 3);
        /* a new Set starts from zeroed state: the old key and alpha are gone */
        set_ok(route, w, 0x00AAAAAA, 0x99, LWA_COLORKEY, "readded");
        get_is(route, w, 0x00AAAAAA, 0, LWA_COLORKEY, "readded key only");
        kill(w);
    }
}
TEST(lav_style_readded_alpha_only_has_zero_key) {
    NEED();
    HWND w = mk(WS_POPUP, WS_EX_LAYERED, NULL);
    if (!w) { t_skip("no window"); return; }
    set_ok(0, w, 0x00010203, 0x44, 3, "pre");
    LONG_PTR ex = U(GetWindowLongPtrA)(w, GWL_EXSTYLE);
    U(SetWindowLongPtrA)(w, GWL_EXSTYLE, ex & ~(LONG_PTR)WS_EX_LAYERED);
    U(SetWindowLongPtrA)(w, GWL_EXSTYLE, ex);
    set_ok(0, w, 0x00AAAAAA, 0x99, LWA_ALPHA, "readded");
    get_is(0, w, 0, 0x99, LWA_ALPHA, "readded alpha only");
    kill(w);
}
TEST(lav_style_extra_bits_keep_attributes) {
    NEED();
    HWND w = mk(WS_POPUP, WS_EX_LAYERED, NULL);
    if (!w) { t_skip("no window"); return; }
    set_ok(0, w, 0x00010203, 0x44, 3, "pre");
    LONG_PTR ex = U(GetWindowLongPtrA)(w, GWL_EXSTYLE);
    U(SetWindowLongPtrA)(w, GWL_EXSTYLE, ex | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW);
    CHECK(has_layered(w));
    get_is(0, w, 0x00010203, 0x44, 3, "after extra bits");
    U(SetWindowLongPtrA)(w, GWL_EXSTYLE, ex);
    get_is(0, w, 0x00010203, 0x44, 3, "after dropping extra bits");
    kill(w);
}
TEST(lav_style_set_does_not_change_other_exstyle_bits) {
    NEED();
    HWND w = mk(WS_POPUP, WS_EX_TOOLWINDOW | WS_EX_LAYERED, NULL);
    if (!w) { t_skip("no window"); return; }
    LONG_PTR before = U(GetWindowLongPtrA)(w, GWL_EXSTYLE);
    set_ok(0, w, 0x00010203, 0x44, 3, "set");
    set_fails(0, w, 0, 0, 4, ERROR_INVALID_PARAMETER, "bad flags");
    EXPECT_EQ(U(GetWindowLongPtrA)(w, GWL_EXSTYLE), before);
    kill(w);
}
TEST(lav_style_message_only_cannot_gain_layered) {
    NEED();
    HWND w = mk_msg(0);
    if (!w) { t_skip("no message-only window"); return; }
    LONG_PTR ex = U(GetWindowLongPtrA)(w, GWL_EXSTYLE);
    U(SetWindowLongPtrA)(w, GWL_EXSTYLE, ex | WS_EX_LAYERED);
    BOOL lay = has_layered(w);
    t_note("message-only window accepts WS_EX_LAYERED via SetWindowLong: %d", lay);
    /* relation: Set agrees with the style bit that is actually present */
    SetLastError(SENT);
    BOOL r = UN(NtUserSetLayeredWindowAttributes)(w, 0, 0x10, LWA_ALPHA);
    CHECKF(!!r == !!lay, "Set result %d vs layered bit %d", r, lay);
    if (!r) EXPECT_ERR(ERROR_INVALID_PARAMETER);
    kill(w);
}
TEST(lav_style_layered_child_creation_relation) {
    NEED();
    HWND p = mk(WS_POPUP, 0, NULL);
    if (!p) { t_skip("no parent"); return; }
    HWND c = mk(WS_CHILD, WS_EX_LAYERED, p);
    t_note("layered child created at creation time: %d", !!c);
    if (c) {
        set_ok(0, c, 0x00010203, 0x44, 3, "layered child");
        get_is(0, c, 0x00010203, 0x44, 3, "layered child");
    }
    kill(p);
}
TEST(lav_style_child_gain_layered_relation) {
    NEED();
    HWND p = mk(WS_POPUP, 0, NULL);
    HWND c = p ? mk(WS_CHILD, 0, p) : NULL;
    if (!c) { kill(p); t_skip("no child"); return; }
    LONG_PTR ex = U(GetWindowLongPtrA)(c, GWL_EXSTYLE);
    U(SetWindowLongPtrA)(c, GWL_EXSTYLE, ex | WS_EX_LAYERED);
    BOOL lay = has_layered(c);
    t_note("child accepts WS_EX_LAYERED via SetWindowLong: %d", lay);
    SetLastError(SENT);
    BOOL r = UN(NtUserSetLayeredWindowAttributes)(c, 0, 0x10, LWA_ALPHA);
    CHECKF(!!r == !!lay, "Set result %d vs layered bit %d", r, lay);
    if (!r) EXPECT_ERR(ERROR_INVALID_PARAMETER);
    kill(p);
}
TEST(lav_style_reparent_layered_popup_relation) {
    NEED(); REQUIRE(U(SetParent));
    HWND p = mk(WS_POPUP, 0, NULL);
    HWND w = p ? mk(WS_POPUP, WS_EX_LAYERED, NULL) : NULL;
    if (!w) { kill(p); t_skip("no window"); return; }
    set_ok(0, w, 0x00010203, 0x44, 3, "pre");
    U(SetWindowLongPtrA)(w, GWL_STYLE, (U(GetWindowLongPtrA)(w, GWL_STYLE) & ~(LONG_PTR)WS_POPUP) | WS_CHILD);
    HWND old = U(SetParent)(w, p);
    t_note("SetParent of layered window accepted: %d, layered bit kept: %d", !!old, has_layered(w));
    /* relation: a window that still carries the layered bit accepts a new Set and reads it back */
    if (has_layered(w)) {
        set_ok(0, w, 0x00050607, 0x55, 3, "after reparent");
        get_is(0, w, 0x00050607, 0x55, 3, "after reparent");
    } else {
        nonlayered_all_fail(w, "reparented, no longer layered");
    }
    kill(p);
}

/* ---- the flag word ------------------------------------------------------------------------------ */
/* valid flags on a fresh layered window: key stored only with LWA_COLORKEY, alpha only with LWA_ALPHA */
static void good_flags(DWORD f, DWORD style) {
    NEED();
    for (int route = 0; route < 2; route++) {
        HWND w = mk(style, WS_EX_LAYERED, NULL);
        if (!w) { t_skip("no window"); return; }
        set_ok(route, w, 0x00123456, 0x77, f, "fresh");
        get_is(route, w, (f & LWA_COLORKEY) ? 0x00123456 : 0, (f & LWA_ALPHA) ? 0x77 : 0, f, "fresh");
        kill(w);
    }
}
TEST(lav_flags_0_popup) { good_flags(0, WS_POPUP); }
TEST(lav_flags_1_popup) { good_flags(1, WS_POPUP); }
TEST(lav_flags_2_popup) { good_flags(2, WS_POPUP); }
TEST(lav_flags_3_popup) { good_flags(3, WS_POPUP); }
TEST(lav_flags_0_overlapped) { good_flags(0, WS_OVERLAPPEDWINDOW); }
TEST(lav_flags_1_overlapped) { good_flags(1, WS_OVERLAPPEDWINDOW); }
TEST(lav_flags_2_overlapped) { good_flags(2, WS_OVERLAPPEDWINDOW); }
TEST(lav_flags_3_overlapped) { good_flags(3, WS_OVERLAPPEDWINDOW); }

/* invalid flags: 87 on layered windows (state untouched), on non-layered windows, and a bad handle wins */
static void bad_flags(DWORD f) {
    NEED();
    HWND lay = mk(WS_POPUP, WS_EX_LAYERED, NULL);
    HWND ovl = mk(WS_OVERLAPPEDWINDOW, WS_EX_LAYERED, NULL);
    HWND non = mk(WS_POPUP, 0, NULL);
    HWND dead = mk(WS_POPUP, WS_EX_LAYERED, NULL);
    if (!lay || !ovl || !non || !dead) { kill(lay); kill(ovl); kill(non); kill(dead); t_skip("no window"); return; }
    kill(dead);
    set_ok(0, lay, 0x00010203, 0x44, 3, "pre");
    set_ok(0, ovl, 0x00040506, 0x55, 3, "pre");
    for (int route = 0; route < 2; route++) {
        set_fails(route, lay, 0x00AAAAAA, 0x11, f, ERROR_INVALID_PARAMETER, "layered popup");
        set_fails(route, ovl, 0x00AAAAAA, 0x11, f, ERROR_INVALID_PARAMETER, "layered overlapped");
        set_fails(route, non, 0x00AAAAAA, 0x11, f, ERROR_INVALID_PARAMETER, "non-layered");
        set_fails(route, dead, 0x00AAAAAA, 0x11, f, ERROR_INVALID_WINDOW_HANDLE, "dead handle");
        set_fails(route, NULL, 0x00AAAAAA, 0x11, f, ERROR_INVALID_WINDOW_HANDLE, "null handle");
        get_is(route, lay, 0x00010203, 0x44, 3, "state untouched");
        get_is(route, ovl, 0x00040506, 0x55, 3, "state untouched");
    }
    kill(lay); kill(ovl); kill(non);
}
#define BTEST(nm, v) TEST(lav_badflags_##nm) { bad_flags(v); }
BTEST(x04, 4)
BTEST(x05, 5)
BTEST(x06, 6)
BTEST(x07, 7)
BTEST(x08, 8)
BTEST(x10, 0x10)
BTEST(x20, 0x20)
BTEST(x40, 0x40)
BTEST(x80, 0x80)
BTEST(x100, 0x100)
BTEST(x10000, 0x10000)
BTEST(x80000000, 0x80000000)
BTEST(xfffffffc, 0xFFFFFFFC)
BTEST(xfffffffd, 0xFFFFFFFD)
BTEST(xfffffffe, 0xFFFFFFFE)
BTEST(xffffffff, 0xFFFFFFFF)

TEST(lav_badflags_single_bits_all) {
    NEED();
    HWND w = mk(WS_POPUP, WS_EX_LAYERED, NULL);
    if (!w) { t_skip("no window"); return; }
    set_ok(0, w, 0x00010203, 0x44, 3, "pre");
    for (int b = 2; b < 32; b++) {
        set_fails(0, w, 0, 0, 1u << b, ERROR_INVALID_PARAMETER, "single bit");
        set_fails(1, w, 0, 0, (1u << b) | 3, ERROR_INVALID_PARAMETER, "single bit plus valid");
    }
    get_is(0, w, 0x00010203, 0x44, 3, "state untouched");
    kill(w);
}
TEST(lav_badflags_precedence_over_bad_handle_is_handle_first) {
    NEED();
    static const DWORD fl[] = { 4, 0xFFFFFFFF, 0x80000000 };
    for (int i = 0; i < 3; i++) for (int r = 0; r < 2; r++) {
        set_fails(r, NULL, 0, 0, fl[i], ERROR_INVALID_WINDOW_HANDLE, "null");
        set_fails(r, (HWND)(ULONG_PTR)0x777777, 0, 0, fl[i], ERROR_INVALID_WINDOW_HANDLE, "garbage");
        set_fails(r, (HWND)(ULONG_PTR)-1, 0, 0, fl[i], ERROR_INVALID_WINDOW_HANDLE, "minus1");
    }
}
TEST(lav_badflags_on_desktop_is_87) {
    NEED(); REQUIRE(U(GetDesktopWindow));
    for (int r = 0; r < 2; r++) { set_fails(r, U(GetDesktopWindow)(), 0, 0, 4, ERROR_INVALID_PARAMETER, "desktop"); set_fails(r, U(GetDesktopWindow)(), 0, 0, 0xFFFFFFFF, ERROR_INVALID_PARAMETER, "desktop"); }
}

/* ---- stored values ------------------------------------------------------------------------------ */
static void key_value(COLORREF key) {
    NEED();
    for (int route = 0; route < 2; route++) {
        HWND w = mk(WS_POPUP, WS_EX_LAYERED, NULL);
        if (!w) { t_skip("no window"); return; }
        set_ok(route, w, key, 0x5A, LWA_ALPHA | LWA_COLORKEY, "both");
        get_is(route, w, key, 0x5A, 3, "both");
        set_ok(route, w, ~key, 0xA5, LWA_COLORKEY, "key only");
        get_is(route, w, ~key, 0x5A, LWA_COLORKEY, "key only");
        set_ok(1 - route, w, key, 0xA5, LWA_COLORKEY, "key only again");
        get_is(route, w, key, 0x5A, LWA_COLORKEY, "key only again");
        kill(w);
    }
}
#define KTEST(nm, v) TEST(lav_key_##nm) { key_value(v); }
KTEST(zero, 0x00000000)
KTEST(one, 0x00000001)
KTEST(white, 0x00FFFFFF)
KTEST(red_blue_mix, 0x00FF00FF)
KTEST(high_byte_01, 0x01000000)
KTEST(palette_index_style, 0x01000005)
KTEST(palette_relative_style, 0x02000005)
KTEST(high_byte_03, 0x03123456)
KTEST(high_byte_80, 0x80000000)
KTEST(high_byte_ff, 0xFF000000)
KTEST(all_ones, 0xFFFFFFFF)
KTEST(pattern_a, 0xA5A5A5A5)

static void alpha_value(BYTE al) {
    NEED();
    for (int route = 0; route < 2; route++) {
        HWND w = mk(WS_POPUP, WS_EX_LAYERED, NULL);
        if (!w) { t_skip("no window"); return; }
        set_ok(route, w, 0x00112233, al, LWA_ALPHA, "alpha only");
        get_is(route, w, 0, al, LWA_ALPHA, "alpha only");
        set_ok(route, w, 0x00445566, (BYTE)~al, LWA_COLORKEY, "key only");
        get_is(route, w, 0x00445566, al, LWA_COLORKEY, "alpha kept");
        set_ok(1 - route, w, 0x00445566, (BYTE)~al, LWA_ALPHA | LWA_COLORKEY, "both");
        get_is(route, w, 0x00445566, (BYTE)~al, 3, "both");
        kill(w);
    }
}
#define ATEST(nm, v) TEST(lav_alpha_##nm) { alpha_value(v); }
ATEST(zero, 0)
ATEST(one, 1)
ATEST(x7f, 0x7F)
ATEST(x80, 0x80)
ATEST(x81, 0x81)
ATEST(xfe, 0xFE)
ATEST(xff, 0xFF)
ATEST(x55, 0x55)

TEST(lav_store_flags_zero_on_fresh_window_reads_zero_state) {
    NEED();
    HWND w = mk(WS_POPUP, WS_EX_LAYERED, NULL);
    if (!w) { t_skip("no window"); return; }
    set_ok(0, w, 0x00123456, 0x77, 0, "flags 0");
    get_is(0, w, 0, 0, 0, "flags 0 stores nothing");
    kill(w);
}
TEST(lav_store_flags_zero_keeps_old_values_clears_flags) {
    NEED();
    HWND w = mk(WS_POPUP, WS_EX_LAYERED, NULL);
    if (!w) { t_skip("no window"); return; }
    set_ok(0, w, 0x00010203, 0x44, 3, "pre");
    set_ok(0, w, 0x00AABBCC, 0x99, 0, "flags 0");
    get_is(0, w, 0x00010203, 0x44, 0, "flags 0 after full set");
    set_ok(0, w, 0x00AABBCC, 0x99, 2, "alpha");
    get_is(0, w, 0x00010203, 0x99, 2, "alpha only: key kept");
    kill(w);
}
TEST(lav_store_fields_independent) {
    NEED();
    HWND w = mk(WS_POPUP, WS_EX_LAYERED, NULL);
    if (!w) { t_skip("no window"); return; }
    set_ok(0, w, 0x00010203, 0x44, 3, "pre");
    set_ok(0, w, 0x00AABBCC, 0x99, LWA_ALPHA, "alpha");
    get_is(0, w, 0x00010203, 0x99, LWA_ALPHA, "after alpha");
    set_ok(0, w, 0x00AABBCC, 0x11, LWA_COLORKEY, "key");
    get_is(0, w, 0x00AABBCC, 0x99, LWA_COLORKEY, "after key");
    set_ok(0, w, 0x00000001, 0x12, 3, "both");
    get_is(0, w, 1, 0x12, 3, "after both");
    kill(w);
}
TEST(lav_store_flags_reported_are_the_last_set) {
    NEED();
    HWND w = mk(WS_POPUP, WS_EX_LAYERED, NULL);
    if (!w) { t_skip("no window"); return; }
    static const DWORD seq[] = { 3, 1, 2, 0, 3, 2, 1, 0 };
    for (int i = 0; i < 8; i++) {
        set_ok(i & 1, w, 0x00101010 + i, 0x20 + i, seq[i], "seq");
        COLORREF k; BYTE a; DWORD f = 0xFFFFFFFF;
        CHECK(do_get(i & 1, w, NULL, NULL, &f));
        EXPECT_EQ(f, seq[i]);
        (void)k; (void)a;
    }
    kill(w);
}
TEST(lav_store_flags_masked_to_two_bits) {
    NEED();
    HWND w = mk(WS_POPUP, WS_EX_LAYERED, NULL);
    if (!w) { t_skip("no window"); return; }
    for (DWORD f = 0; f < 4; f++) {
        set_ok(0, w, 0x00010203, 0x44, f, "valid flags");
        DWORD got = 0xFFFFFFFF;
        CHECK(UN(NtUserGetLayeredWindowAttributes)(w, NULL, NULL, &got));
        EXPECT_EQ(got & ~3u, 0);
        EXPECT_EQ(got, f);
    }
    kill(w);
}
TEST(lav_store_idempotent) {
    NEED();
    HWND w = mk(WS_POPUP, WS_EX_LAYERED, NULL);
    if (!w) { t_skip("no window"); return; }
    for (int i = 0; i < 5; i++) {
        set_ok(i & 1, w, 0x00FEDCBA, 0xC3, 3, "repeat");
        get_is(1 - (i & 1), w, 0x00FEDCBA, 0xC3, 3, "repeat");
    }
    kill(w);
}
TEST(lav_store_windows_are_independent) {
    NEED();
    HWND a = mk(WS_POPUP, WS_EX_LAYERED, NULL), b = mk(WS_OVERLAPPEDWINDOW, WS_EX_LAYERED, NULL);
    if (!a || !b) { kill(a); kill(b); t_skip("no window"); return; }
    set_ok(0, a, 0x00010101, 0x11, 3, "a");
    COLORREF k = 5; BYTE al = 6; DWORD f = 7;
    SetLastError(SENT);
    CHECK(!UN(NtUserGetLayeredWindowAttributes)(b, &k, &al, &f));   /* b never given attributes */
    EXPECT_ERR(SENT);
    set_ok(0, b, 0x00020202, 0x22, 2, "b");
    get_is(0, a, 0x00010101, 0x11, 3, "a untouched");
    get_is(0, b, 0, 0x22, 2, "b");
    kill(a); kill(b);
}
TEST(lav_store_failed_set_changes_nothing) {
    NEED();
    HWND w = mk(WS_POPUP, WS_EX_LAYERED, NULL);
    if (!w) { t_skip("no window"); return; }
    set_ok(0, w, 0x00010203, 0x44, 3, "pre");
    set_fails(0, w, 0x00FFFFFF, 0xFF, 0x100 | 3, ERROR_INVALID_PARAMETER, "bad");
    set_fails(1, w, 0x00FFFFFF, 0xFF, 0xFFFFFFFF, ERROR_INVALID_PARAMETER, "bad");
    get_is(0, w, 0x00010203, 0x44, 3, "unchanged");
    kill(w);
}
TEST(lav_store_survives_show_move_frame_change) {
    NEED(); REQUIRE(U(ShowWindow)); REQUIRE(U(SetWindowPos)); REQUIRE(U(MoveWindow));
    HWND w = mk(WS_POPUP, WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, NULL);
    if (!w) { t_skip("no window"); return; }
    set_ok(0, w, 0x00010203, 0x44, 3, "pre");
    U(SetWindowPos)(w, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    get_is(0, w, 0x00010203, 0x44, 3, "after frame change");
    U(MoveWindow)(w, 50, 50, 60, 60, TRUE);
    get_is(0, w, 0x00010203, 0x44, 3, "after move");
    U(ShowWindow)(w, SW_SHOWNA);
    get_is(0, w, 0x00010203, 0x44, 3, "after show");
    set_ok(0, w, 0x00050607, 0x45, 3, "set while shown");
    U(ShowWindow)(w, SW_HIDE);
    get_is(0, w, 0x00050607, 0x45, 3, "after hide");
    set_ok(0, w, 0x00080910, 0x46, 3, "set while hidden");
    get_is(0, w, 0x00080910, 0x46, 3, "hidden");
    kill(w);
}

/* ---- Get: output pointers ----------------------------------------------------------------------- */
static HWND layered_with(COLORREF k, BYTE a, DWORD f) {
    HWND w = mk(WS_POPUP, WS_EX_LAYERED, NULL);
    if (w && !UN(NtUserSetLayeredWindowAttributes)(w, k, a, f)) { kill(w); return NULL; }
    return w;
}
TEST(lav_get_all_pointers_null) {
    NEED();
    HWND w = layered_with(0x00778899, 0x42, 3);
    if (!w) { t_skip("no window"); return; }
    for (int r = 0; r < 2; r++) {
        SetLastError(SENT);
        CHECK(do_get(r, w, NULL, NULL, NULL));
        EXPECT_ERR(SENT);
    }
    kill(w);
}
TEST(lav_get_key_pointer_only) {
    NEED();
    HWND w = layered_with(0x00778899, 0x42, 3);
    if (!w) { t_skip("no window"); return; }
    for (int r = 0; r < 2; r++) {
        COLORREF k = 0x5A5A5A5A;
        SetLastError(SENT);
        CHECK(do_get(r, w, &k, NULL, NULL));
        EXPECT_ERR(SENT);
        EXPECT_EQ(k, 0x00778899);
    }
    kill(w);
}
TEST(lav_get_alpha_pointer_only) {
    NEED();
    HWND w = layered_with(0x00778899, 0x42, 3);
    if (!w) { t_skip("no window"); return; }
    for (int r = 0; r < 2; r++) {
        BYTE b[4]; memset(b, 0xEE, sizeof b);
        CHECK(do_get(r, w, NULL, b + 1, NULL));
        EXPECT_EQ(b[1], 0x42);
        CHECK(b[0] == 0xEE && b[2] == 0xEE && b[3] == 0xEE);      /* a single byte is written */
    }
    kill(w);
}
TEST(lav_get_flags_pointer_only) {
    NEED();
    HWND w = layered_with(0x00778899, 0x42, 3);
    if (!w) { t_skip("no window"); return; }
    for (int r = 0; r < 2; r++) {
        DWORD f = 0x5A5A5A5A;
        CHECK(do_get(r, w, NULL, NULL, &f));
        EXPECT_EQ(f, 3);
    }
    kill(w);
}
TEST(lav_get_pairs_of_pointers) {
    NEED();
    HWND w = layered_with(0x00778899, 0x42, 3);
    if (!w) { t_skip("no window"); return; }
    for (int r = 0; r < 2; r++) {
        COLORREF k = 1; BYTE a = 2; DWORD f = 3;
        CHECK(do_get(r, w, &k, &a, NULL)); EXPECT_EQ(k, 0x00778899); EXPECT_EQ(a, 0x42); EXPECT_EQ(f, 3);
        k = 1; a = 2; f = 3;
        CHECK(do_get(r, w, &k, NULL, &f)); EXPECT_EQ(k, 0x00778899); EXPECT_EQ(a, 2); EXPECT_EQ(f, 3);
        k = 1; a = 2; f = 5;
        CHECK(do_get(r, w, NULL, &a, &f)); EXPECT_EQ(k, 1); EXPECT_EQ(a, 0x42); EXPECT_EQ(f, 3);
    }
    kill(w);
}
TEST(lav_get_null_pointers_with_bad_handle_still_fail) {
    NEED();
    for (int r = 0; r < 2; r++) {
        SetLastError(SENT);
        CHECK(!do_get(r, NULL, NULL, NULL, NULL));
        EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
        SetLastError(SENT);
        CHECK(!do_get(r, (HWND)(ULONG_PTR)0x777777, NULL, NULL, NULL));
        EXPECT_ERR(ERROR_INVALID_WINDOW_HANDLE);
    }
}
TEST(lav_get_null_pointers_with_nonlayered_window_still_fail) {
    NEED();
    HWND w = mk(WS_POPUP, 0, NULL);
    if (!w) { t_skip("no window"); return; }
    for (int r = 0; r < 2; r++) {
        SetLastError(SENT);
        CHECK(!do_get(r, w, NULL, NULL, NULL));
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
    kill(w);
}
TEST(lav_get_fresh_layered_window_fails_without_error) {
    NEED();
    for (int r = 0; r < 2; r++) {
        HWND w = mk(WS_POPUP, WS_EX_LAYERED, NULL);
        if (!w) { t_skip("no window"); return; }
        COLORREF k = 0x11223344; BYTE a = 0x99; DWORD f = 0xFEEDBEEF;
        SetLastError(SENT);
        CHECK(!do_get(r, w, &k, &a, &f));
        EXPECT_ERR(SENT);
        CHECK(k == 0x11223344 && a == 0x99 && f == 0xFEEDBEEF);
        SetLastError(SENT);
        CHECK(!do_get(r, w, NULL, NULL, NULL));
        EXPECT_ERR(SENT);
        SetLastError(0);
        CHECK(!do_get(r, w, &k, &a, &f));
        EXPECT_ERR(0);
        kill(w);
    }
}
TEST(lav_get_fresh_overlapped_layered_fails_without_error) {
    NEED();
    HWND w = mk(WS_OVERLAPPEDWINDOW, WS_EX_LAYERED, NULL);
    if (!w) { t_skip("no window"); return; }
    SetLastError(SENT);
    CHECK(!UN(NtUserGetLayeredWindowAttributes)(w, NULL, NULL, NULL));
    EXPECT_ERR(SENT);
    set_ok(0, w, 0, 0, 0, "flags 0");
    SetLastError(SENT);
    CHECK(UN(NtUserGetLayeredWindowAttributes)(w, NULL, NULL, NULL));       /* flags 0 is enough to create the state */
    EXPECT_ERR(SENT);
    kill(w);
}
TEST(lav_get_after_failed_first_set_stays_failing) {
    NEED();
    HWND w = mk(WS_POPUP, WS_EX_LAYERED, NULL);
    if (!w) { t_skip("no window"); return; }
    set_fails(0, w, 0, 0, 4, ERROR_INVALID_PARAMETER, "bad first set");
    SetLastError(SENT);
    CHECK(!UN(NtUserGetLayeredWindowAttributes)(w, NULL, NULL, NULL));
    EXPECT_ERR(SENT);
    kill(w);
}
TEST(lav_get_nt_and_user32_agree) {
    NEED();
    HWND w = layered_with(0x01234567, 0x89, 3);
    if (!w) { t_skip("no window"); return; }
    COLORREF k0 = 0, k1 = 1; BYTE a0 = 0, a1 = 1; DWORD f0 = 0, f1 = 1;
    CHECK(do_get(0, w, &k0, &a0, &f0));
    CHECK(do_get(1, w, &k1, &a1, &f1));
    CHECK(k0 == k1 && a0 == a1 && f0 == f1);
    EXPECT_EQ(k0, 0x01234567); EXPECT_EQ(a0, 0x89); EXPECT_EQ(f0, 3);
    kill(w);
}
TEST(lav_get_outputs_untouched_for_every_failure_kind) {
    NEED(); REQUIRE(U(GetDesktopWindow));
    HWND non = mk(WS_POPUP, 0, NULL), fresh = mk(WS_POPUP, WS_EX_LAYERED, NULL);
    if (!non || !fresh) { kill(non); kill(fresh); t_skip("no window"); return; }
    HWND bad[] = { NULL, (HWND)(ULONG_PTR)0x777777, U(GetDesktopWindow)(), non, fresh };
    for (int i = 0; i < 5; i++) for (int r = 0; r < 2; r++) {
        COLORREF k = 0xDEADBEEF; BYTE a = 0xCD; DWORD f = 0xCAFEBABE;
        CHECK(!do_get(r, bad[i], &k, &a, &f));
        CHECKF(k == 0xDEADBEEF && a == 0xCD && f == 0xCAFEBABE, "row %d route %s wrote outputs", i, rn(r));
    }
    kill(non); kill(fresh);
}

/* ---- return values and last-error preservation -------------------------------------------------- */
TEST(lav_return_values_are_boolean) {
    NEED();
    HWND w = mk(WS_POPUP, WS_EX_LAYERED, NULL);
    if (!w) { t_skip("no window"); return; }
    for (int r = 0; r < 2; r++) {
        BOOL ok = do_set(r, w, 0, 0x10, 3);
        EXPECT_EQ(ok, 1);
        BOOL bad = do_set(r, w, 0, 0x10, 4);
        EXPECT_EQ(bad, 0);
        BOOL g = do_get(r, w, NULL, NULL, NULL);
        EXPECT_EQ(g, 1);
        BOOL gb = do_get(r, NULL, NULL, NULL, NULL);
        EXPECT_EQ(gb, 0);
    }
    kill(w);
}
TEST(lav_lasterror_preserved_on_success) {
    NEED();
    HWND w = mk(WS_POPUP, WS_EX_LAYERED, NULL);
    if (!w) { t_skip("no window"); return; }
    static const DWORD sent[] = { 0, 1234, 0x5A5A, 0xDEADBEEF, 0xFFFFFFFF };
    for (int i = 0; i < 5; i++) for (int r = 0; r < 2; r++) {
        SetLastError(sent[i]); CHECK(do_set(r, w, 1, 2, 3)); EXPECT_ERR(sent[i]);
        SetLastError(sent[i]); CHECK(do_get(r, w, NULL, NULL, NULL)); EXPECT_ERR(sent[i]);
    }
    kill(w);
}
TEST(lav_lasterror_is_overwritten_on_failure) {
    NEED();
    static const DWORD sent[] = { 0, 1234, 0xDEADBEEF };
    for (int i = 0; i < 3; i++) for (int r = 0; r < 2; r++) {
        SetLastError(sent[i]); CHECK(!do_set(r, NULL, 0, 0, 0)); EXPECT_ERR(1400);
        SetLastError(sent[i]); CHECK(!do_get(r, NULL, NULL, NULL, NULL)); EXPECT_ERR(1400);
    }
}
/* ---- cross-thread, same process ----------------------------------------------------------------- */
struct xt { HWND w; HANDLE ready, go; };
static DWORD WINAPI xt_main(void *p) {
    struct xt *x = p;
    x->w = mk(WS_POPUP, WS_EX_LAYERED, NULL);
    if (x->w) U(SetLayeredWindowAttributes)(x->w, 0x00112233, 0x55, 3);
    SetEvent(x->ready);
    WaitForSingleObject(x->go, 10000);
    kill(x->w);
    return 0;
}
TEST(lav_cross_thread_set_and_get) {
    NEED();
    struct xt x = { NULL, CreateEventA(NULL, TRUE, FALSE, NULL), CreateEventA(NULL, TRUE, FALSE, NULL) };
    HANDLE t = CreateThread(NULL, 0, xt_main, &x, 0, NULL);
    if (!t) { t_skip("no thread"); return; }
    WaitForSingleObject(x.ready, 10000);
    if (!x.w) { SetEvent(x.go); WaitForSingleObject(t, 10000); CloseHandle(t); t_skip("no window"); return; }
    for (int r = 0; r < 2; r++) {
        if (r == 0) get_is(r, x.w, 0x00112233, 0x55, 3, "foreign thread window");
        set_ok(r, x.w, 0x00445566 + r, 0x66 + r, 3, "foreign thread window");
        get_is(1 - r, x.w, 0x00445566 + r, 0x66 + r, 3, "after set");
        set_fails(r, x.w, 0, 0, 4, ERROR_INVALID_PARAMETER, "foreign thread, bad flags");
    }
    HWND dead = x.w;
    SetEvent(x.go);
    WaitForSingleObject(t, 10000);
    CloseHandle(t); CloseHandle(x.ready); CloseHandle(x.go);
    handle_all_fail(dead, ERROR_INVALID_WINDOW_HANDLE, "after owner thread exit");
}
struct xo { HWND w; int phase; };
static DWORD WINAPI xo_main(void *p) {
    struct xo *x = p;
    COLORREF k = 0; BYTE a = 0; DWORD f = 0;
    if (!UN(NtUserGetLayeredWindowAttributes)(x->w, &k, &a, &f)) return 0;
    return (k == 0x00BEEF01 && a == 0xA5 && f == 3) ? 1 : 2;
}
TEST(lav_cross_thread_reader_sees_attributes) {
    NEED();
    HWND w = layered_with(0x00BEEF01, 0xA5, 3);
    if (!w) { t_skip("no window"); return; }
    struct xo x = { w, 0 };
    HANDLE t = CreateThread(NULL, 0, xo_main, &x, 0, NULL);
    DWORD code = 0xFF;
    if (t) { WaitForSingleObject(t, 10000); GetExitCodeThread(t, &code); CloseHandle(t); }
    EXPECT_EQ(code, 1);
    kill(w);
}

/* ---- the shell window (other process): relations only ------------------------------------------- */
TEST(lav_shell_tray_get_is_refused_or_not_layered) {
    NEED(); REQUIRE(U(FindWindowA));
    HWND tray = U(FindWindowA)("Shell_TrayWnd", NULL);
    if (!tray) { t_skip("no taskbar window"); return; }
    /* read-only probe of a window that belongs to another process */
    COLORREF k = 1; BYTE a = 2; DWORD f = 3;
    SetLastError(SENT);
    BOOL r = UN(NtUserGetLayeredWindowAttributes)(tray, &k, &a, &f);
    DWORD e = GetLastError();
    t_note("foreign shell window Get: result %d (error value is note-only)", r);
    if (!r) {
        CHECK(e == ERROR_INVALID_PARAMETER || e == ERROR_ACCESS_DENIED || e == SENT);
        CHECK(k == 1 && a == 2 && f == 3);
    }
}

/* ---- calls made from inside the window's own procedure ------------------------------------------ */
/* The attribute calls work from the first creation message to the last destruction message of a layered
 * window, and fail with 87 for a window that is not layered, at every one of those points. */
static struct { int expect_layered; int ncreate, create, destroy, ncdestroy, bad; HWND seen; } cb;
static void cb_probe(HWND h, int *slot) {
    COLORREF k = 0x77; BYTE a = 0x77; DWORD f = 0x77;
    SetLastError(SENT);
    BOOL s = U(SetLayeredWindowAttributes)(h, 0x00030201, 0x42, LWA_ALPHA | LWA_COLORKEY);
    DWORD se = GetLastError();
    SetLastError(SENT);
    BOOL g = U(GetLayeredWindowAttributes)(h, &k, &a, &f);
    DWORD ge = GetLastError();
    if (cb.expect_layered) {
        if (s && g && se == SENT && ge == SENT && k == 0x00030201 && a == 0x42 && f == 3) (*slot)++; else cb.bad++;
    } else {
        if (!s && !g && se == ERROR_INVALID_PARAMETER && ge == ERROR_INVALID_PARAMETER && k == 0x77 && a == 0x77 && f == 0x77) (*slot)++; else cb.bad++;
    }
}
static LRESULT CALLBACK cb_wp(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_NCCREATE) cb_probe(h, &cb.ncreate);
    else if (m == WM_CREATE) cb_probe(h, &cb.create);
    else if (m == WM_DESTROY) cb_probe(h, &cb.destroy);
    else if (m == WM_NCDESTROY) cb_probe(h, &cb.ncdestroy);
    return U(DefWindowProcA)(h, m, w, l);
}
static const char *cb_class(void) {
    static char n[64];
    if (!n[0]) {
        WNDCLASSA wc; memset(&wc, 0, sizeof wc);
        wsprintfA(n, "win32kt_lavcb_%lu", GetCurrentProcessId());
        wc.lpfnWndProc = cb_wp; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = n;
        if (!U(RegisterClassA)(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) { n[0] = 0; return NULL; }
    }
    return n;
}
static void callback_case(int layered, DWORD style) {
    NEED();
    const char *c = cb_class();
    if (!c) { t_skip("class registration failed"); return; }
    memset(&cb, 0, sizeof cb);
    cb.expect_layered = layered;
    HWND w = U(CreateWindowExA)(layered ? WS_EX_LAYERED : 0, c, "lavcb", style, 10, 10, 40, 40, NULL, NULL, GetModuleHandleA(NULL), NULL);
    if (!w) { t_skip("no window"); return; }
    EXPECT_EQ(cb.ncreate, 1);
    EXPECT_EQ(cb.create, 1);
    EXPECT_EQ(cb.destroy, 0);
    kill(w);
    EXPECT_EQ(cb.destroy, 1);
    EXPECT_EQ(cb.ncdestroy, 1);
    CHECKF(cb.bad == 0, "%d callback probes saw an unexpected result", cb.bad);
}
TEST(lav_callback_layered_popup_all_messages) { callback_case(1, WS_POPUP); }
TEST(lav_callback_layered_overlapped_all_messages) { callback_case(1, WS_OVERLAPPEDWINDOW); }
TEST(lav_callback_plain_popup_all_messages_87) { callback_case(0, WS_POPUP); }
TEST(lav_callback_plain_overlapped_all_messages_87) { callback_case(0, WS_OVERLAPPEDWINDOW); }

/* ---- Set sends nothing to the window; toggling the style sends the style-change pair ------------- */
static UINT g_msgs[128]; static int g_nmsg, g_rec;
static LRESULT CALLBACK rec_wp(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (g_rec && g_nmsg < 128) g_msgs[g_nmsg++] = m;
    return U(DefWindowProcA)(h, m, w, l);
}
static const char *rec_class(void) {
    static char n[64];
    if (!n[0]) {
        WNDCLASSA wc; memset(&wc, 0, sizeof wc);
        wsprintfA(n, "win32kt_lavrec_%lu", GetCurrentProcessId());
        wc.lpfnWndProc = rec_wp; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = n;
        if (!U(RegisterClassA)(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) { n[0] = 0; return NULL; }
    }
    return n;
}
static int rec_count(UINT m) { int n = 0; for (int i = 0; i < g_nmsg; i++) n += g_msgs[i] == m; return n; }
static int rec_index(UINT m) { for (int i = 0; i < g_nmsg; i++) if (g_msgs[i] == m) return i; return -1; }
static void set_sends_no_messages(int visible) {
    NEED(); REQUIRE(U(ShowWindow));
    const char *c = rec_class();
    if (!c) { t_skip("class registration failed"); return; }
    HWND w = U(CreateWindowExA)(WS_EX_LAYERED | WS_EX_NOACTIVATE, c, "lavrec", WS_POPUP, 10, 10, 40, 40, NULL, NULL, GetModuleHandleA(NULL), NULL);
    if (!w) { t_skip("no window"); return; }
    if (visible) U(ShowWindow)(w, SW_SHOWNA);
    k_pump(100);
    g_nmsg = 0; g_rec = 1;
    /* none of these may produce a style, position, size or frame message */
    set_ok(0, w, 0x00010203, 0x44, 3, "first");
    set_ok(1, w, 0x00040506, 0x45, 2, "alpha");
    set_ok(0, w, 0x00040506, 0x45, 1, "key");
    set_ok(0, w, 0x00040506, 0x45, 0, "none");
    set_fails(0, w, 0, 0, 4, ERROR_INVALID_PARAMETER, "bad flags");
    COLORREF k; BYTE a; DWORD f;
    CHECK(do_get(0, w, &k, &a, &f));
    k_pump(100);
    g_rec = 0;
    static const UINT never[] = { WM_STYLECHANGING, WM_STYLECHANGED, WM_WINDOWPOSCHANGING, WM_WINDOWPOSCHANGED, WM_MOVE, WM_SIZE, WM_NCCALCSIZE, WM_SHOWWINDOW, WM_ENABLE };
    for (int i = 0; i < (int)(sizeof never / sizeof never[0]); i++) CHECKF(rec_count(never[i]) == 0, "message 0x%x was sent by the attribute calls", never[i]);
    /* removing and adding the style each send the style-change pair, changing before changed */
    LONG_PTR ex = U(GetWindowLongPtrA)(w, GWL_EXSTYLE);
    g_nmsg = 0; g_rec = 1;
    U(SetWindowLongPtrA)(w, GWL_EXSTYLE, ex & ~(LONG_PTR)WS_EX_LAYERED);
    k_pump(100);
    g_rec = 0;
    CHECK(rec_count(WM_STYLECHANGING) == 1 && rec_count(WM_STYLECHANGED) == 1);
    CHECK(rec_index(WM_STYLECHANGING) >= 0 && rec_index(WM_STYLECHANGING) < rec_index(WM_STYLECHANGED));
    g_nmsg = 0; g_rec = 1;
    U(SetWindowLongPtrA)(w, GWL_EXSTYLE, ex);
    k_pump(100);
    g_rec = 0;
    CHECK(rec_count(WM_STYLECHANGING) == 1 && rec_count(WM_STYLECHANGED) == 1);
    CHECK(rec_index(WM_STYLECHANGING) >= 0 && rec_index(WM_STYLECHANGING) < rec_index(WM_STYLECHANGED));
    kill(w);
}
TEST(lav_messages_set_get_send_none_hidden) { set_sends_no_messages(0); }
TEST(lav_messages_set_get_send_none_visible) { set_sends_no_messages(1); }

/* ---- the owner thread does not have to pump ----------------------------------------------------- */
struct np { HWND w; HANDLE ready, go; BOOL r; DWORD e; };
static DWORD WINAPI np_owner(void *p) {
    struct np *x = p;
    x->w = mk(WS_POPUP, WS_EX_LAYERED, NULL);
    SetEvent(x->ready);
    WaitForSingleObject(x->go, 30000);   /* blocked, no message loop */
    kill(x->w);
    return 0;
}
static DWORD WINAPI np_user(void *p) {
    struct np *x = p;
    SetLastError(SENT);
    x->r = U(SetLayeredWindowAttributes)(x->w, 0x00112233, 0x66, 3);
    x->e = GetLastError();
    return 0;
}
TEST(lav_cross_thread_owner_not_pumping) {
    NEED();
    struct np x; memset(&x, 0, sizeof x);
    x.ready = CreateEventA(NULL, TRUE, FALSE, NULL); x.go = CreateEventA(NULL, TRUE, FALSE, NULL);
    HANDLE t = CreateThread(NULL, 0, np_owner, &x, 0, NULL);
    if (!t) { CloseHandle(x.ready); CloseHandle(x.go); t_skip("no thread"); return; }
    WaitForSingleObject(x.ready, 10000);
    if (!x.w) { SetEvent(x.go); WaitForSingleObject(t, 10000); CloseHandle(t); CloseHandle(x.ready); CloseHandle(x.go); t_skip("no window"); return; }
    HANDLE u = CreateThread(NULL, 0, np_user, &x, 0, NULL);
    DWORD w = u ? WaitForSingleObject(u, 20000) : WAIT_FAILED;
    CHECKF(w == WAIT_OBJECT_0, "setter did not finish while the owner thread was blocked (wait result %lu)", w);
    if (w == WAIT_OBJECT_0) {
        CHECK(x.r);
        CHECK(x.e == SENT);
        get_is(0, x.w, 0x00112233, 0x66, 3, "read from main thread");
        get_is(1, x.w, 0x00112233, 0x66, 3, "read from main thread");
    }
    if (u) CloseHandle(u);
    SetEvent(x.go);
    WaitForSingleObject(t, 10000);
    CloseHandle(t); CloseHandle(x.ready); CloseHandle(x.go);
}

/* ---- window relationships and states ------------------------------------------------------------ */
TEST(lav_owned_layered_popup_dies_with_owner) {
    NEED();
    HWND o = mk(WS_POPUP, WS_EX_LAYERED, NULL);
    HWND p = o ? mk(WS_POPUP, WS_EX_LAYERED, o) : NULL;
    if (!p) { kill(o); t_skip("no owned popup"); return; }
    set_ok(0, p, 0x00010203, 0x44, 3, "owned");
    set_ok(0, o, 0x00040506, 0x55, 3, "owner");
    kill(o);
    CHECK(!U(IsWindow)(p));
    handle_all_fail(p, ERROR_INVALID_WINDOW_HANDLE, "owned popup after owner destroyed");
}
TEST(lav_state_minimized_and_maximized_keep_attributes) {
    NEED(); REQUIRE(U(ShowWindow));
    for (int route = 0; route < 2; route++) {
        HWND w = mk(WS_OVERLAPPEDWINDOW, WS_EX_LAYERED | WS_EX_NOACTIVATE, NULL);
        if (!w) { t_skip("no window"); return; }
        set_ok(route, w, 0x00010203, 0x44, 3, "pre");
        U(ShowWindow)(w, SW_SHOWMINNOACTIVE);
        get_is(route, w, 0x00010203, 0x44, 3, "minimized");
        set_ok(route, w, 0x00040506, 0x55, 2, "while minimized");
        get_is(route, w, 0x00010203, 0x55, 2, "after set while minimized");
        U(ShowWindow)(w, SW_SHOWMAXIMIZED);
        get_is(route, w, 0x00010203, 0x55, 2, "maximized");
        set_fails(route, w, 0, 0, 8, ERROR_INVALID_PARAMETER, "maximized, bad flags");
        U(ShowWindow)(w, SW_RESTORE);
        get_is(route, w, 0x00010203, 0x55, 2, "restored");
        kill(w);
    }
}
TEST(lav_exstyle_combinations_roundtrip) {
    NEED();
    static const DWORD ex[] = { WS_EX_TRANSPARENT, WS_EX_NOACTIVATE, WS_EX_TOOLWINDOW, WS_EX_TOPMOST, WS_EX_COMPOSITED,
                                WS_EX_NOREDIRECTIONBITMAP, WS_EX_DLGMODALFRAME, WS_EX_LAYOUTRTL | WS_EX_RIGHT, WS_EX_APPWINDOW, WS_EX_CLIENTEDGE };
    for (int i = 0; i < (int)(sizeof ex / sizeof ex[0]); i++) {
        HWND w = mk(WS_POPUP, WS_EX_LAYERED | ex[i], NULL);
        if (!w) { t_note("ex 0x%lx not creatable", ex[i]); continue; }
        CHECKF(has_layered(w), "ex 0x%lx lost the layered bit", ex[i]);
        for (int route = 0; route < 2; route++) {
            set_ok(route, w, 0x00A1B2C3 + i, 0x30 + i, 3, "combo");
            get_is(1 - route, w, 0x00A1B2C3 + i, 0x30 + i, 3, "combo");
            set_fails(route, w, 0, 0, 0x10, ERROR_INVALID_PARAMETER, "combo bad flags");
        }
        kill(w);
    }
}
TEST(lav_child_of_desktop_relation) {
    NEED(); REQUIRE(U(GetDesktopWindow));
    HWND c = U(CreateWindowExA)(WS_EX_LAYERED, lav_class(), "lav", WS_CHILD, 10, 10, 40, 40, U(GetDesktopWindow)(), NULL, GetModuleHandleA(NULL), NULL);
    t_note("layered WS_CHILD window under the desktop window created: %d", !!c);
    if (!c) return;
    CHECKF(has_layered(c), "created without the layered bit");
    set_ok(0, c, 0x00010203, 0x44, 3, "desktop child");
    get_is(1, c, 0x00010203, 0x44, 3, "desktop child");
    set_fails(0, c, 0, 0, 4, ERROR_INVALID_PARAMETER, "desktop child, bad flags");
    kill(c);
}
