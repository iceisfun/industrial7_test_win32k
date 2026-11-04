/* clipboard_dde: windows owned by ANOTHER process (a real second process, not the desktop or shell window).
 *
 * A helper child creates a hidden top-level window of a class named after the parent's process id and, depending on its
 * mode, takes the clipboard before it starts pumping messages:
 *   plain   nothing; a bare foreign window
 *   own     owns a text item (opened, emptied, set, closed)
 *   hold    owns a text item and keeps the clipboard OPEN
 *   delayed owns a delayed-rendering text item and renders it on WM_RENDERFORMAT
 * The child's exit code reports what its window procedure saw: 100 + 1 if WM_DESTROYCLIPBOARD arrived + 2 if
 * WM_RENDERFORMAT arrived (an exit code below 100 is a setup failure). The parent finds the window by class name. */
#include "cd.h"

static volatile LONG fw_destroy, fw_render;
static LRESULT CALLBACK fw_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROYCLIPBOARD) fw_destroy++;
    if (m == WM_RENDERFORMAT) {
        fw_render++;
        HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, 32);
        WCHAR *p = g ? GlobalLock(g) : NULL;
        if (p) { lstrcpyW(p, L"rendered"); GlobalUnlock(g); SetClipboardData((UINT)w, g); }
        return 0;
    }
    if (m == WM_CLOSE) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(h, m, w, l);
}

CHILD(cdfw_win) {
    (void)argc;
    DWORD ppid = strtoul(argv[1], NULL, 10);
    const char *mode = argv[2];
    char cls[64];
    wsprintfA(cls, "win32kt_cdfw_%lu", ppid);
    WNDCLASSA wc; memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = fw_proc; wc.hInstance = GetModuleHandleA(NULL); wc.lpszClassName = cls;
    RegisterClassA(&wc);
    HWND h = CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls, "cdfw", WS_POPUP, 10, 10, 40, 40, NULL, NULL, wc.hInstance, NULL);
    if (!h) return 90;
    if (strcmp(mode, "plain")) {
        if (!OpenClipboard(h)) return 91;
        EmptyClipboard();
        if (!strcmp(mode, "delayed")) SetClipboardData(CF_UNICODETEXT, NULL);
        else {
            HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, 16);
            WCHAR *p = g ? GlobalLock(g) : NULL;
            if (!p) return 92;
            lstrcpyW(p, L"child"); GlobalUnlock(g);
            SetClipboardData(CF_UNICODETEXT, g);
        }
        if (strcmp(mode, "hold")) CloseClipboard();
    }
    MSG m; DWORD t0 = GetTickCount();
    while (GetTickCount() - t0 < 25000) {
        while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
            if (m.message == WM_QUIT) return 100 + (fw_destroy ? 1 : 0) + (fw_render ? 2 : 0);
            TranslateMessage(&m); DispatchMessageA(&m);
        }
        Sleep(5);
    }
    return 93;
}

struct fw { PROCESS_INFORMATION pi; HWND h; };
/* starts the helper and waits until its window exists and, for the clipboard modes, until it has taken the clipboard */
static int fw_start(struct fw *f, const char *mode) {
    char a[64], cls[64];
    memset(f, 0, sizeof *f);
    wsprintfA(a, "%lu %s", GetCurrentProcessId(), mode);
    wsprintfA(cls, "win32kt_cdfw_%lu", GetCurrentProcessId());
    if (!t_spawn_child("cdfw_win", a, 0, NULL, NULL, NULL, FALSE, NULL, &f->pi)) return 0;
    for (int i = 0; i < 500 && !f->h; i++) { f->h = U(FindWindowA)(cls, NULL); if (!f->h) Sleep(20); }
    if (!f->h) return 0;
    if (!strcmp(mode, "own") || !strcmp(mode, "delayed"))
        /* the owner is set by EmptyClipboard while the helper still has the clipboard open: wait for the close too */
        for (int i = 0; i < 500 && (U(GetClipboardOwner)() != f->h || U(GetOpenClipboardWindow)() != NULL); i++) Sleep(20);
    if (!strcmp(mode, "hold"))
        for (int i = 0; i < 500 && U(GetOpenClipboardWindow)() != f->h; i++) Sleep(20);
    return 1;
}
/* closes the helper and returns its exit code */
static DWORD fw_stop(struct fw *f) {
    DWORD c = 0xFFFF;
    if (f->h) U(PostMessageA)(f->h, WM_CLOSE, 0, 0);
    if (WaitForSingleObject(f->pi.hProcess, 8000) != WAIT_OBJECT_0) { TerminateProcess(f->pi.hProcess, 1); WaitForSingleObject(f->pi.hProcess, 3000); }
    GetExitCodeProcess(f->pi.hProcess, &c);
    CloseHandle(f->pi.hProcess); CloseHandle(f->pi.hThread);
    return c;
}
#define FW_NEED(f, mode) struct fw f; \
    if (!cd_have_window()) { t_skip("no window station/display"); return; } \
    REQUIRE(U(FindWindowA) && U(PostMessageA) && U(GetWindowThreadProcessId)); \
    if (!fw_start(&f, mode)) { fw_stop(&f); CHECKF(0, "helper window did not appear (mode %s)", mode); return; }

TEST(clipboard_dde_foreign_window_listener_add_remove_return_5_without_error) {
    CD_SETUP();
    REQUIRE(U(AddClipboardFormatListener) && U(RemoveClipboardFormatListener) && UN(NtUserAddClipboardFormatListener) && UN(NtUserRemoveClipboardFormatListener));
    CD_NEED_WIN32K();
    FW_NEED(f, "plain");
    DWORD pid = 0;
    U(GetWindowThreadProcessId)(f.h, &pid);
    CHECK(pid != 0 && pid != GetCurrentProcessId());
    /* the window belongs to another process: the call stops at the process comparison, stores nothing and returns the
     * constant 5 (truthy) without setting a last error, for the user32 wrapper and the raw syscall alike */
    SetLastError(0); EQ(U(AddClipboardFormatListener)(f.h), 5); EQ(GetLastError(), 0);
    SetLastError(0); EQ(U(RemoveClipboardFormatListener)(f.h), 5); EQ(GetLastError(), 0);
    SetLastError(0); EQ(UN(NtUserAddClipboardFormatListener)(f.h), 5); EQ(GetLastError(), 0);
    SetLastError(0); EQ(UN(NtUserRemoveClipboardFormatListener)(f.h), 5); EQ(GetLastError(), 0);
    EQ(fw_stop(&f), 100);
}

TEST(clipboard_dde_foreign_window_can_open_and_own_clipboard) {
    CD_SETUP();
    FW_NEED(f, "plain");
    /* OpenClipboard takes any valid window handle of the window station, the opener's process does not matter */
    CHECK(U(OpenClipboard)(f.h));
    EQ(U(GetOpenClipboardWindow)(), f.h);
    CHECK(U(EmptyClipboard)());
    EQ(U(GetClipboardOwner)(), f.h);
    CHECK(U(CloseClipboard)());
    EQ(U(GetClipboardOwner)(), f.h);                 /* ownership outlives the open */
    EQ(U(GetOpenClipboardWindow)(), NULL);
    CHECK(cd_open(NULL));
    CHECK(U(EmptyClipboard)());                      /* takes it back: the previous (foreign) owner is told */
    EQ(U(GetClipboardOwner)(), NULL);
    CHECK(U(CloseClipboard)());
    EQ(fw_stop(&f), 101);                            /* WM_DESTROYCLIPBOARD reached the helper */
}

TEST(clipboard_dde_foreign_owner_data_readable_and_empty_notifies_owner) {
    CD_SETUP();
    FW_NEED(f, "own");
    EQ(U(GetClipboardOwner)(), f.h);
    EQ(U(GetOpenClipboardWindow)(), NULL);
    CHECK(U(IsClipboardFormatAvailable)(CF_UNICODETEXT));
    DWORD s0 = U(GetClipboardSequenceNumber)();
    CHECK(cd_open(NULL));
    HANDLE h = U(GetClipboardData)(CF_UNICODETEXT);
    CHECK(h != NULL);
    if (h) {
        WCHAR *p = GlobalLock(h);
        CHECK(p && !lstrcmpW(p, L"child"));
        if (p) GlobalUnlock(h);
    }
    CHECK(U(EmptyClipboard)());
    EQ(U(GetClipboardOwner)(), NULL);
    CHECK(!U(IsClipboardFormatAvailable)(CF_UNICODETEXT));
    CHECK(U(CloseClipboard)());
    CHECK(U(GetClipboardSequenceNumber)() != s0);
    EQ(fw_stop(&f), 101);
}

TEST(clipboard_dde_foreign_open_window_reported_and_blocks_other_openers) {
    CD_SETUP();
    FW_NEED(f, "hold");
    EQ(U(GetOpenClipboardWindow)(), f.h);
    EQ(U(GetClipboardOwner)(), f.h);
    SetLastError(0xDEAD); CHECK(!U(OpenClipboard)(NULL)); EXPECT_ERR(CD_ERR_ACCESS_DENIED);
    CHECK(U(IsClipboardFormatAvailable)(CF_UNICODETEXT));          /* queries need no open */
    /* without an open of its own this thread is not the holder */
    SetLastError(0xDEAD); CHECK(U(GetClipboardData)(CF_UNICODETEXT) == NULL); EXPECT_ERR(CD_ERR_CLIPBOARD_NOT_OPEN);
    SetLastError(0xDEAD); CHECK(!U(EmptyClipboard)()); EXPECT_ERR(CD_ERR_CLIPBOARD_NOT_OPEN);
    SetLastError(0xDEAD); CHECK(!U(CloseClipboard)()); EXPECT_ERR(CD_ERR_CLIPBOARD_NOT_OPEN);
    SetLastError(0xDEAD); EQ(U(EnumClipboardFormats)(0), 0); EXPECT_ERR(CD_ERR_CLIPBOARD_NOT_OPEN);
    EQ(fw_stop(&f), 100);
}

TEST(clipboard_dde_foreign_delayed_render_asks_the_foreign_owner) {
    CD_SETUP();
    FW_NEED(f, "delayed");
    EQ(U(GetClipboardOwner)(), f.h);
    CHECK(U(IsClipboardFormatAvailable)(CF_UNICODETEXT));
    CHECK(cd_open(NULL));
    HANDLE h = U(GetClipboardData)(CF_UNICODETEXT);                /* WM_RENDERFORMAT is sent to the other process */
    CHECK(h != NULL);
    if (h) {
        WCHAR *p = GlobalLock(h);
        CHECK(p && !lstrcmpW(p, L"rendered"));
        if (p) GlobalUnlock(h);
    }
    CHECK(U(CloseClipboard)());
    EQ(fw_stop(&f), 102);                                          /* rendered, owner not destroyed */
}

TEST(clipboard_dde_foreign_viewer_stays_until_its_process_ends) {
    CD_SETUP();
    REQUIRE(U(SetClipboardViewer) && U(ChangeClipboardChain) && U(GetClipboardViewer));
    FW_NEED(f, "plain");
    EQ(U(GetClipboardViewer)(), NULL);
    SetLastError(0); EQ(U(SetClipboardViewer)(f.h), NULL); EQ(GetLastError(), 0);   /* empty chain: no previous viewer */
    EQ(U(GetClipboardViewer)(), f.h);
    /* another process's window is not removed from the chain by this process */
    SetLastError(0); CHECK(!U(ChangeClipboardChain)(f.h, NULL)); EQ(GetLastError(), 0);
    EQ(U(GetClipboardViewer)(), f.h);
    EQ(fw_stop(&f), 100);
    EQ(U(GetClipboardViewer)(), NULL);                             /* ending the window's process unlinks it */
}
