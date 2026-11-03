/* State-changing gap_04 syscalls (run with /destructive): NtUserSetSystemCursor with a valid cursor,
 * NtUserSetDisplayAutoRotationPreferences with valid bits and an on-screen caption animation request.
 * Every test restores what it changed before the end, whether or not its checks pass. */
#include "t.h"
#include "k.h"

UDECL(BOOL, NtUserSetSystemCursor, (HCURSOR c, DWORD id));
UDECL(BOOL, NtUserGetDisplayAutoRotationPreferences, (DWORD *pref));
UDECL(BOOL, NtUserSetDisplayAutoRotationPreferences, (DWORD pref));
UDECL(BOOL, NtUserDrawAnimatedRects, (HWND h, int idAni, const RECT *from, const RECT *to));

#define OCR_NORMAL_ 32512
#define SPI_SETCURSORS_ 0x57

static HWND dx_window(void) {
    if (!U(RegisterClassExW) || !U(CreateWindowExW) || !U(DefWindowProcW)) return NULL;
    static ATOM a;
    if (!a) {
        WNDCLASSEXW wc; memset(&wc, 0, sizeof wc);
        wc.cbSize = sizeof wc; wc.lpfnWndProc = (WNDPROC)U(DefWindowProcW);
        wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = L"gap4dx_wnd";
        a = U(RegisterClassExW)(&wc);
    }
    return U(CreateWindowExW)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"gap4dx_wnd", L"gap4dx", WS_OVERLAPPEDWINDOW,
                              100, 100, 200, 100, NULL, NULL, GetModuleHandleW(NULL), NULL);
}

/* zzzSetSystemCursor with a valid cursor and a known id (OCR_NORMAL) replaces the system cursor, then SPI_SETCURSORS
 * reloads the defaults from the registry/scheme. */
TEST(gap4_dx_setsystemcursor_valid_replaces_then_restore) {
    REQUIRE(UN(NtUserSetSystemCursor)); REQUIRE(U(LoadCursorW)); REQUIRE(U(CopyIcon)); REQUIRE(U(SystemParametersInfoW));
    HWND h = dx_window();
    HCURSOR cross = U(LoadCursorW)(NULL, MAKEINTRESOURCEW(32515));
    HCURSOR copy = cross ? (HCURSOR)U(CopyIcon)((HICON)cross) : NULL;
    if (!h || !copy) { t_skip("fixtures missing"); return; }
    SET_ERR(0);
    BOOL r = UN(NtUserSetSystemCursor)(copy, OCR_NORMAL_);
    DWORD e = GetLastError();
    t_note("SetSystemCursor(copy, OCR_NORMAL) = %d gle=%lu", r, e);
    CHECK(r == 1 || e == ERROR_ACCESS_DENIED || e == ERROR_REQUIRES_INTERACTIVE_WINDOWSTATION);
    if (r != 1 && copy) U(DestroyCursor)(copy);
    /* restore the scheme cursors no matter what */
    CHECK(U(SystemParametersInfoW)(SPI_SETCURSORS_, 0, NULL, 0));
    if (h) U(DestroyWindow)(h);
}
/* Several ids in one go, each a copy of a different cursor, then one restore. */
TEST(gap4_dx_setsystemcursor_each_standard_id_then_restore) {
    REQUIRE(UN(NtUserSetSystemCursor)); REQUIRE(U(LoadCursorW)); REQUIRE(U(CopyIcon)); REQUIRE(U(SystemParametersInfoW));
    HWND h = dx_window();
    if (!h) { t_skip("no window"); return; }
    static const DWORD ids[] = { 32512, 32513, 32514, 32515, 32516, 32642, 32643, 32644, 32645, 32646, 32648, 32649, 32650 };
    int ok = 0, tried = 0;
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        HCURSOR src = U(LoadCursorW)(NULL, MAKEINTRESOURCEW(ids[i]));
        HCURSOR c = src ? (HCURSOR)U(CopyIcon)((HICON)src) : NULL;
        if (!c) continue;
        tried++;
        if (UN(NtUserSetSystemCursor)(c, ids[i])) ok++; else U(DestroyCursor)(c);
    }
    t_note("%d of %d ids accepted", ok, tried);
    CHECK(ok == tried || ok == 0);   /* either the window station grants WINSTA_WRITEATTRIBUTES or nothing is replaced */
    CHECK(U(SystemParametersInfoW)(SPI_SETCURSORS_, 0, NULL, 0));
    U(DestroyWindow)(h);
}

/* Valid orientation preferences are stored in the process info and read back; the original value is restored. */
TEST(gap4_dx_autorotation_prefs_set_get_roundtrip) {
    REQUIRE(UN(NtUserGetDisplayAutoRotationPreferences)); REQUIRE(UN(NtUserSetDisplayAutoRotationPreferences));
    HWND h = dx_window();
    if (!h) { t_skip("no window"); return; }
    DWORD orig = 0xcc;
    CHECK(UN(NtUserGetDisplayAutoRotationPreferences)(&orig) == 1);
    static const DWORD vals[] = { 1, 2, 4, 8, 3, 5, 0xa, 0xf, 0 };   /* ORIENTATION_PREFERENCE flag combinations */
    for (unsigned i = 0; i < sizeof vals / sizeof vals[0]; i++) {
        BOOL r = UN(NtUserSetDisplayAutoRotationPreferences)(vals[i]);
        DWORD got = 0xcc; BOOL g = UN(NtUserGetDisplayAutoRotationPreferences)(&got);
        CHECKF(r == 1 && g == 1 && got == vals[i], "set 0x%lx ret %d get ret %d got 0x%lx", vals[i], r, g, got);
    }
    UN(NtUserSetDisplayAutoRotationPreferences)(orig);
    DWORD fin = 0xcc; UN(NtUserGetDisplayAutoRotationPreferences)(&fin);
    CHECK(fin == orig);
    U(DestroyWindow)(h);
}
/* ppi-wide, not process-global: a second process starts at 0 whatever this one stored. */
CHILD(gap4dx_rot_child) {
    (void)argc; (void)argv;
    int bad = 0;
    HWND h = dx_window();
    if (!h || !UN(NtUserGetDisplayAutoRotationPreferences)) { printf("NOTE prerequisites missing\n"); return 0; }
    DWORD p = 0xcc;
    if (UN(NtUserGetDisplayAutoRotationPreferences)(&p) != 1 || p != 0) { printf("  FAIL child pref=0x%lx\n", p); bad++; }
    return bad;
}
TEST(gap4_dx_autorotation_prefs_are_per_process) {
    REQUIRE(UN(NtUserGetDisplayAutoRotationPreferences)); REQUIRE(UN(NtUserSetDisplayAutoRotationPreferences));
    HWND h = dx_window();
    if (!h) { t_skip("no window"); return; }
    DWORD orig = 0; UN(NtUserGetDisplayAutoRotationPreferences)(&orig);
    CHECK(UN(NtUserSetDisplayAutoRotationPreferences)(5) == 1);
    struct k_child_result r = k_child_run("gap4dx_rot_child", "", 30000);
    CHECK(r.exit_code == 0);
    UN(NtUserSetDisplayAutoRotationPreferences)(orig);
    U(DestroyWindow)(h);
}

/* idAni == IDANI_CAPTION with a real top-level window: DrawAnimatedRects returns 1 whether or not it has to draw
 * (the composed-desktop path returns 1 without drawing). */
TEST(gap4_dx_drawanimatedrects_caption_valid_window) {
    REQUIRE(UN(NtUserDrawAnimatedRects));
    HWND h = dx_window();
    if (!h) { t_skip("no window"); return; }
    RECT a = { 100, 100, 110, 110 }, b = { 100, 100, 300, 200 };
    EXPECT_EQ(UN(NtUserDrawAnimatedRects)(h, 3, &a, &b), 1);
    U(DestroyWindow)(h);
}
