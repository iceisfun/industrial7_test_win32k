/* dx_gap_08: state-changing probes of thin-coverage syscalls, run only with /destructive.
 *
 *  - NtUserEnableMouseInPointer / NtUserIsMouseInPointerEnabled (win32kbase): the argument must be exactly 0 or 1
 *    ((v != 0) == v), else FALSE + ERROR_INVALID_PARAMETER; a successful call stores the value in the process
 *    (ppi+0x334 bit 2) together with a "decided" bit (bit 3); a later call asking for the other value is refused.
 *    Every case runs in a CHILD process: the switch is per process and cannot be undone.
 *  - NtUserChangeDisplaySettings with CDS_TEST only asks the driver whether a mode would work; the current mode must be
 *    accepted and nothing may change.
 *  - NtUserIsClipboardFormatAvailable after the clipboard owner publishes data (the clipboard is saved and restored). */
#include "t.h"
#include "k.h"

UDECL(BOOL, NtUserEnableMouseInPointer, (UINT));
UDECL(BOOL, NtUserIsMouseInPointerEnabled, (void));

#define NOT_WINE() do { if (t_is_wine()) { t_skip("win32u stub unimplemented under Wine"); return; } } while (0)

/* ---- EnableMouseInPointer ------------------------------------------------------------------------ */

static BOOL mip_enable(UINT v) { return UN(NtUserEnableMouseInPointer)(v); }
static BOOL mip_is(void) { return UN(NtUserIsMouseInPointerEnabled)() != 0; }

static DWORD WINAPI mip_probe_thread(void *p) { *(BOOL *)p = mip_is(); return 0; }

CHILD(gap8dx_mip_true_first) {
    int fails = 0;
    #define CF(c) do { if (!(c)) { printf("  FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
    if (!UN(NtUserEnableMouseInPointer) || !UN(NtUserIsMouseInPointerEnabled)) return 0;
    CF(!mip_is());                                       /* default: off */
    static const UINT bad[] = { 2, 3, 0x100, 0x80000000u, 0xffffffffu };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SetLastError(0);
        BOOL r = mip_enable(bad[i]);
        CF(r == FALSE);
        CF(GetLastError() == ERROR_INVALID_PARAMETER);
        CF(!mip_is());                                    /* a rejected value changes nothing */
    }
    SetLastError(0);
    BOOL r = mip_enable(1);
    if (!r && GetLastError() == ERROR_ACCESS_DENIED) { printf("  NOTE mouse-in-pointer denied by policy\n"); return fails; }
    CF(r == TRUE);
    CF(mip_is());
    CF(mip_enable(1) == TRUE);                            /* same value again is accepted */
    CF(mip_is());
    SetLastError(0);
    r = mip_enable(0);                                    /* the decision is final once taken */
    printf("  NOTE enable(0) after enable(1) -> %d err %lu\n", r, GetLastError());
    CF(r == FALSE);
    CF(mip_is());
    BOOL other = FALSE; HANDLE t = CreateThread(NULL, 0, mip_probe_thread, &other, 0, NULL);
    if (t) { WaitForSingleObject(t, 3000); CloseHandle(t); }
    CF(other == TRUE);                                    /* per process, not per thread */
    return fails;
}

CHILD(gap8dx_mip_false_first) {
    int fails = 0;
    if (!UN(NtUserEnableMouseInPointer) || !UN(NtUserIsMouseInPointerEnabled)) return 0;
    CF(!mip_is());
    SetLastError(0);
    BOOL r = mip_enable(0);
    if (!r && GetLastError() == ERROR_ACCESS_DENIED) { printf("  NOTE denied by policy\n"); return fails; }
    CF(r == TRUE);
    CF(!mip_is());
    SetLastError(0);
    r = mip_enable(1);                                    /* locked to FALSE */
    printf("  NOTE enable(1) after enable(0) -> %d err %lu\n", r, GetLastError());
    CF(r == FALSE);
    CF(!mip_is());
    return fails;
}

CHILD(gap8dx_mip_untouched) {
    int fails = 0;
    if (!UN(NtUserIsMouseInPointerEnabled)) return 0;
    for (int i = 0; i < 3; i++) CF(!mip_is());           /* querying never flips the state */
    return fails;
}

TEST(gap8_dx_mouse_in_pointer_true_then_locked) {
    NOT_WINE(); REQUIRE(UN(NtUserEnableMouseInPointer)); REQUIRE(UN(NtUserIsMouseInPointerEnabled));
    struct k_child_result r = k_child_run("gap8dx_mip_true_first", "", 30000);
    CHECKF(r.exit_code == 0, "child exit %lu", r.exit_code);
}
TEST(gap8_dx_mouse_in_pointer_false_then_locked) {
    NOT_WINE(); REQUIRE(UN(NtUserEnableMouseInPointer)); REQUIRE(UN(NtUserIsMouseInPointerEnabled));
    struct k_child_result r = k_child_run("gap8dx_mip_false_first", "", 30000);
    CHECKF(r.exit_code == 0, "child exit %lu", r.exit_code);
}
TEST(gap8_dx_mouse_in_pointer_default_off) {
    NOT_WINE(); REQUIRE(UN(NtUserIsMouseInPointerEnabled));
    struct k_child_result r = k_child_run("gap8dx_mip_untouched", "", 30000);
    CHECKF(r.exit_code == 0, "child exit %lu", r.exit_code);
}

/* ---- ChangeDisplaySettings(CDS_TEST) ---------------------------------------------------------------- */

static LONG cds_ex(DEVMODEW *dm, DWORD fl) {
    FARPROC f = t_proc_in("user32.dll", "ChangeDisplaySettingsExW");
    return f ? ((LONG (WINAPI *)(LPCWSTR, DEVMODEW *, HWND, DWORD, LPVOID))f)(NULL, dm, NULL, fl, NULL) : -100;
}
static int cur_mode(DEVMODEW *dm) {
    memset(dm, 0, sizeof *dm); dm->dmSize = sizeof *dm;
    return U(EnumDisplaySettingsW) && U(EnumDisplaySettingsW)(NULL, ENUM_CURRENT_SETTINGS, dm);
}

TEST(gap8_dx_changedisplay_test_current_mode_succeeds) {
    NOT_WINE(); REQUIRE(U(EnumDisplaySettingsW)); REQUIRE(t_proc_in("user32.dll", "ChangeDisplaySettingsExW"));
    DEVMODEW dm, after;
    if (!cur_mode(&dm)) { t_skip("no current mode"); return; }
    int cx = U(GetSystemMetrics)(SM_CXSCREEN), cy = U(GetSystemMetrics)(SM_CYSCREEN);
    LONG r = cds_ex(&dm, CDS_TEST);
    CHECKF(r == DISP_CHANGE_SUCCESSFUL, "CDS_TEST of the current mode -> %ld", r);
    CHECK(cur_mode(&after));
    CHECK(after.dmPelsWidth == dm.dmPelsWidth && after.dmPelsHeight == dm.dmPelsHeight && after.dmBitsPerPel == dm.dmBitsPerPel && after.dmDisplayFrequency == dm.dmDisplayFrequency);
    CHECK(U(GetSystemMetrics)(SM_CXSCREEN) == cx && U(GetSystemMetrics)(SM_CYSCREEN) == cy);
}

TEST(gap8_dx_changedisplay_test_impossible_mode_fails_without_change) {
    NOT_WINE(); REQUIRE(U(EnumDisplaySettingsW)); REQUIRE(t_proc_in("user32.dll", "ChangeDisplaySettingsExW"));
    DEVMODEW dm, bad, after;
    if (!cur_mode(&dm)) { t_skip("no current mode"); return; }
    static const struct { DWORD w, h; } sizes[] = { {1, 1}, {7, 5}, {100000, 100000}, {0x7fffffff, 0x7fffffff} };
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        bad = dm; bad.dmPelsWidth = sizes[i].w; bad.dmPelsHeight = sizes[i].h; bad.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
        LONG r = cds_ex(&bad, CDS_TEST);
        t_note("CDS_TEST %lux%lu -> %ld", sizes[i].w, sizes[i].h, r);
        CHECKF(r != DISP_CHANGE_SUCCESSFUL, "%lux%lu accepted", sizes[i].w, sizes[i].h);
        CHECK(cur_mode(&after));
        CHECKF(after.dmPelsWidth == dm.dmPelsWidth && after.dmPelsHeight == dm.dmPelsHeight, "mode changed after a test-only call");
    }
}

TEST(gap8_dx_changedisplay_test_flag_does_not_persist) {
    NOT_WINE(); REQUIRE(U(EnumDisplaySettingsW)); REQUIRE(t_proc_in("user32.dll", "ChangeDisplaySettingsExW"));
    DEVMODEW dm;
    if (!cur_mode(&dm)) { t_skip("no current mode"); return; }
    for (int i = 0; i < 3; i++) {
        LONG r = cds_ex(&dm, CDS_TEST);
        CHECKF(r == DISP_CHANGE_SUCCESSFUL, "repeat %d -> %ld", i, r);
    }
    /* bit 27 stays refused whatever the mode argument says */
    SetLastError(0);
    LONG r = cds_ex(&dm, 0x08000000 | CDS_TEST);
    CHECKF(r == DISP_CHANGE_FAILED, "bit27|CDS_TEST -> %ld", r);
}

/* ---- IsClipboardFormatAvailable after SetClipboardData ----------------------------------------------- */

static HGLOBAL g_saved;
static HGLOBAL wtext(const wchar_t *s) {
    size_t n = (lstrlenW(s) + 1) * 2;
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, n);
    if (h) { void *p = GlobalLock(h); memcpy(p, s, n); GlobalUnlock(h); }
    return h;
}
static HWND clip_win(void) {
    return U(CreateWindowExW)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"Static", L"gap8", WS_POPUP, -30000, -30000, 10, 10, NULL, NULL, GetModuleHandleW(NULL), NULL);
}
static int clip_save(HWND w) {
    g_saved = NULL;
    if (!U(OpenClipboard)(w)) return 0;
    HANDLE h = U(GetClipboardData)(CF_UNICODETEXT);
    if (h) { void *p = GlobalLock(h); if (p) { g_saved = wtext((const wchar_t *)p); GlobalUnlock(h); } }
    U(CloseClipboard)();
    return 1;
}
static void clip_restore(HWND w) {
    if (!U(OpenClipboard)(w)) return;
    U(EmptyClipboard)();
    if (g_saved) { if (!U(SetClipboardData)(CF_UNICODETEXT, g_saved)) GlobalFree(g_saved); g_saved = NULL; }
    U(CloseClipboard)();
}
#define CLIP_REQ() do { if (!U(OpenClipboard) || !U(EmptyClipboard) || !U(SetClipboardData) || !U(IsClipboardFormatAvailable) || !U(RegisterClipboardFormatW) || !U(CreateWindowExW)) { t_skip("clipboard exports missing"); return; } } while (0)

TEST(gap8_dx_clipfmt_text_publishes_synthesized_formats) {
    CLIP_REQ();
    HWND w = clip_win(); CHECK(w != NULL); if (!w) return;
    if (!clip_save(w)) { t_skip("clipboard busy"); U(DestroyWindow)(w); return; }
    BOOL ok = U(OpenClipboard)(w);
    CHECK(ok);
    if (ok) {
        U(EmptyClipboard)();
        CHECK(U(SetClipboardData)(CF_UNICODETEXT, wtext(L"gap8 clipboard probe")) != NULL);
        U(CloseClipboard)();
        /* the query needs no open clipboard, and sees the owner's format plus the ones the system converts to */
        CHECK(U(IsClipboardFormatAvailable)(CF_UNICODETEXT));
        CHECK(U(IsClipboardFormatAvailable)(CF_TEXT));
        CHECK(U(IsClipboardFormatAvailable)(CF_OEMTEXT));
        CHECK(U(IsClipboardFormatAvailable)(CF_LOCALE));
        CHECK(!U(IsClipboardFormatAvailable)(CF_BITMAP));
        CHECK(!U(IsClipboardFormatAvailable)(CF_HDROP));
        CHECK(!U(IsClipboardFormatAvailable)(0));
    }
    clip_restore(w);
    U(DestroyWindow)(w);
}

TEST(gap8_dx_clipfmt_registered_format_and_empty) {
    CLIP_REQ();
    HWND w = clip_win(); CHECK(w != NULL); if (!w) return;
    if (!clip_save(w)) { t_skip("clipboard busy"); U(DestroyWindow)(w); return; }
    wchar_t nm[64]; wsprintfW(nm, L"win32kt_gap8_dx_%lu", GetCurrentProcessId());
    UINT f = U(RegisterClipboardFormatW)(nm);
    CHECK(f != 0);
    if (U(OpenClipboard)(w)) {
        U(EmptyClipboard)();
        CHECK(!U(IsClipboardFormatAvailable)(f));
        CHECK(!U(IsClipboardFormatAvailable)(CF_UNICODETEXT));
        CHECK(U(SetClipboardData)(f, wtext(L"private")) != NULL);
        U(CloseClipboard)();
        CHECK(U(IsClipboardFormatAvailable)(f));
        CHECK(!U(IsClipboardFormatAvailable)(CF_UNICODETEXT));
        if (U(OpenClipboard)(w)) { U(EmptyClipboard)(); U(CloseClipboard)(); }
        CHECK(!U(IsClipboardFormatAvailable)(f));
    }
    clip_restore(w);
    U(DestroyWindow)(w);
}

TEST(gap8_dx_clipfmt_delayed_render_counts_as_available) {
    CLIP_REQ();
    HWND w = clip_win(); CHECK(w != NULL); if (!w) return;
    if (!clip_save(w)) { t_skip("clipboard busy"); U(DestroyWindow)(w); return; }
    wchar_t nm[64]; wsprintfW(nm, L"win32kt_gap8_dxd_%lu", GetCurrentProcessId());
    UINT f = U(RegisterClipboardFormatW)(nm);
    if (U(OpenClipboard)(w)) {
        U(EmptyClipboard)();
        U(SetClipboardData)(f, NULL);                       /* delayed rendering: format advertised, data not yet produced */
        U(CloseClipboard)();
        CHECK(U(IsClipboardFormatAvailable)(f));
        if (U(OpenClipboard)(w)) { U(EmptyClipboard)(); U(CloseClipboard)(); }
    }
    clip_restore(w);
    U(DestroyWindow)(w);
}

CHILD(gap8dx_clip_reader) {
    int fails = 0;
    if (!U(IsClipboardFormatAvailable)) return 0;
    CF(U(IsClipboardFormatAvailable)(CF_UNICODETEXT));
    CF(U(IsClipboardFormatAvailable)(CF_TEXT));
    CF(!U(IsClipboardFormatAvailable)(CF_BITMAP));
    return fails;
}

TEST(gap8_dx_clipfmt_visible_from_another_process) {
    CLIP_REQ();
    HWND w = clip_win(); CHECK(w != NULL); if (!w) return;
    if (!clip_save(w)) { t_skip("clipboard busy"); U(DestroyWindow)(w); return; }
    if (U(OpenClipboard)(w)) {
        U(EmptyClipboard)();
        U(SetClipboardData)(CF_UNICODETEXT, wtext(L"gap8 cross-process"));
        U(CloseClipboard)();
        struct k_child_result r = k_child_run("gap8dx_clip_reader", "", 30000);
        CHECKF(r.exit_code == 0, "child exit %lu", r.exit_code);
    }
    clip_restore(w);
    U(DestroyWindow)(w);
}
