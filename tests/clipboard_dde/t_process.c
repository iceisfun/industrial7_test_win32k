/* clipboard_dde: the window-station-wide nature of the clipboard, seen from other processes.
 *
 * Everything here lives in tagWINDOWSTATION, so a second process of the same window station sees
 * the same entries, the same sequence number, the same owner/open windows. Data crosses the
 * process boundary through the TYPE_CLIPDATA objects: the reader's user32 builds a private HGLOBAL
 * with NtUserCreateLocalMemHandle. The per-window-station isolation test is a child because
 * SetProcessWindowStation is process-wide. */
#include "cd.h"

static int cf;
#define CH(c) do { if (!(c)) { cf++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static void pattern(unsigned char *p, size_t n, unsigned seed) {
    for (size_t i = 0; i < n; i++) p[i] = (unsigned char)((i * 37u + seed) ^ (i >> 3));
}
static UINT xfmt(DWORD ppid, const char *suffix) {
    WCHAR nm[96];
    wsprintfW(nm, L"win32kt_cd_%lu_%S", ppid, suffix);
    return U(RegisterClipboardFormatW)(nm);
}
static HANDLE spawn_async(const char *name, const char *args, PROCESS_INFORMATION *pi) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out == INVALID_HANDLE_VALUE || out == NULL) return t_spawn_child(name, args, 0, NULL, NULL, NULL, FALSE, NULL, pi);
    SetHandleInformation(out, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    return t_spawn_child(name, args, 0, NULL, out, out, TRUE, NULL, pi);
}
/* wait for a child while keeping this thread's windows serviced */
static DWORD wait_pump(HANDLE h, DWORD ms) {
    DWORD t0 = GetTickCount(), code = 0;
    while (GetTickCount() - t0 < ms) {
        if (WaitForSingleObject(h, 0) == WAIT_OBJECT_0) { GetExitCodeProcess(h, &code); return code; }
        k_pump(20);
    }
    TerminateProcess(h, 1);
    WaitForSingleObject(h, 3000);
    return 0xDEAD0001;
}
static BOOL open_retry(HWND h) {
    for (int i = 0; i < 40; i++) { if (U(OpenClipboard)(h)) return TRUE; Sleep(50); }
    return FALSE;
}
static const WCHAR kParentText[] = L"parent wrote this";
static const WCHAR kChildText[] = L"child wrote this";

/* ---- child: read what the parent put ---------------------------------------------------------- */
CHILD(cdde_reader) {
    (void)argc;
    DWORD ppid = strtoul(argv[1], NULL, 10), seq = strtoul(argv[2], NULL, 10);
    HWND owner = (HWND)(ULONG_PTR)strtoull(argv[3], NULL, 16);
    UINT fx = xfmt(ppid, "xproc");
    CH(U(IsClipboardFormatAvailable)(CF_UNICODETEXT));
    CH(U(IsClipboardFormatAvailable)(CF_TEXT));
    CH(U(IsClipboardFormatAvailable)(CF_OEMTEXT));
    CH(U(IsClipboardFormatAvailable)(fx));
    CH(U(GetClipboardSequenceNumber)() == seq);       /* window-station wide counter */
    CH(U(GetClipboardOwner)() == owner);
    DWORD pid = 0;
    U(GetWindowThreadProcessId)(owner, &pid);
    CH(pid == ppid);
    CH(U(OpenClipboard)(NULL));
    HANDLE h = U(GetClipboardData)(CF_UNICODETEXT);
    CH(h != NULL);
    if (h) {
        CH(GlobalSize(h) >= sizeof kParentText);
        WCHAR *p = GlobalLock(h);
        CH(p && !lstrcmpW(p, kParentText));
        if (p) GlobalUnlock(h);
    }
    h = U(GetClipboardData)(CF_TEXT);                 /* converted in this process from the kernel copy */
    CH(h != NULL);
    if (h) { char *a = GlobalLock(h); CH(a && !strcmp(a, "parent wrote this")); if (a) GlobalUnlock(h); }
    h = U(GetClipboardData)(fx);
    CH(h != NULL);
    if (h) {
        unsigned char want[100], *p = GlobalLock(h);
        pattern(want, sizeof want, 77);
        CH(GlobalSize(h) >= sizeof want);
        CH(p && !memcmp(p, want, sizeof want));
        if (p) GlobalUnlock(h);
    }
    UINT f[16];
    int n = cd_formats(f, 16);
    CH(n >= 5);
    CH(n == U(CountClipboardFormats)());
    CH(U(CloseClipboard)());
    return cf;
}

TEST(clipboard_dde_process_child_reads_parent_clipboard) {
    CD_SETUP();
    if (!cd_have_window()) { t_skip("no window station/display"); return; }
    struct cd_ctx *c = cd_new();
    if (!c) { t_skip("no window"); return; }
    UINT fx = xfmt(GetCurrentProcessId(), "xproc");
    unsigned char pat[100];
    pattern(pat, sizeof pat, 77);
    CHECK(cd_open_empty(c->hwnd));
    CHECK(U(SetClipboardData)(CF_UNICODETEXT, cd_gmem(kParentText, sizeof kParentText)) != NULL);
    CHECK(U(SetClipboardData)(fx, cd_gmem(pat, sizeof pat)) != NULL);
    CHECK(U(CloseClipboard)());
    char args[128];
    snprintf(args, sizeof args, "%lu %lu %llx", GetCurrentProcessId(), U(GetClipboardSequenceNumber)(), (unsigned long long)(ULONG_PTR)c->hwnd);
    struct k_child_result r = k_child_run("cdde_reader", args, 30000);
    EQ(r.exit_code, 0);
}

/* ---- child: bitmap crosses the process boundary as a public GDI object -------------------------------- */
CHILD(cdde_bmp_reader) {
    (void)argc;
    HBITMAP want = (HBITMAP)(ULONG_PTR)strtoull(argv[1], NULL, 16);
    CH(U(IsClipboardFormatAvailable)(CF_BITMAP));
    CH(U(IsClipboardFormatAvailable)(CF_DIB));
    CH(U(OpenClipboard)(NULL));
    HBITMAP bm = U(GetClipboardData)(CF_BITMAP);
    CH(bm != NULL);
    if (bm) {
        CH(bm == want);                                /* user32 returns the kernel's GDI handle unchanged */
        CH(U(GetObjectType)(bm) == OBJ_BITMAP);
        BITMAP info; memset(&info, 0, sizeof info);
        CH(U(GetObjectW)(bm, sizeof info, &info) == sizeof info);
        CH(info.bmWidth == 8 && info.bmHeight == 8);
    }
    HANDLE d = U(GetClipboardData)(CF_DIB);
    CH(d != NULL);
    if (d) {
        BITMAPINFOHEADER *b = GlobalLock(d);
        CH(b && b->biWidth == 8 && (b->biHeight == 8 || b->biHeight == -8));
        if (b) GlobalUnlock(d);
    }
    CH(U(CloseClipboard)());
    return cf;
}

TEST(clipboard_dde_process_child_reads_bitmap) {
    CD_SETUP();
    if (!cd_have_window()) { t_skip("no window station/display"); return; }
    REQUIRE(U(CreateBitmap));
    static BYTE bits[8 * 8 * 4];
    memset(bits, 0x44, sizeof bits);
    HBITMAP bm = U(CreateBitmap)(8, 8, 1, 32, bits);
    CHECK(bm != NULL);
    CHECK(cd_open_empty(NULL));
    CHECK(U(SetClipboardData)(CF_BITMAP, bm) == (HANDLE)bm);
    CHECK(U(CloseClipboard)());
    char args[48];
    snprintf(args, sizeof args, "%llx", (unsigned long long)(ULONG_PTR)bm);
    struct k_child_result r = k_child_run("cdde_bmp_reader", args, 30000);
    EQ(r.exit_code, 0);
}

/* ---- child: write, parent reads ---------------------------------------------------------------------- */
CHILD(cdde_writer) {
    (void)argc;
    DWORD ppid = strtoul(argv[1], NULL, 10);
    UINT fx = xfmt(ppid, "xwrite");
    unsigned char pat[64];
    pattern(pat, sizeof pat, 5);
    CH(open_retry(NULL));
    CH(U(EmptyClipboard)());
    CH(U(SetClipboardData)(fx, cd_gmem(pat, sizeof pat)) != NULL);
    CH(U(SetClipboardData)(CF_UNICODETEXT, cd_gmem(kChildText, sizeof kChildText)) != NULL);
    CH(U(CloseClipboard)());
    return cf;
}

TEST(clipboard_dde_process_parent_reads_child_clipboard) {
    CD_SETUP();
    if (!cd_have_window()) { t_skip("no window station/display"); return; }
    UINT fx = xfmt(GetCurrentProcessId(), "xwrite");
    CHECK(cd_put(NULL, 0x2A0, "before", 7));
    DWORD s0 = U(GetClipboardSequenceNumber)();
    char args[32];
    snprintf(args, sizeof args, "%lu", GetCurrentProcessId());
    struct k_child_result r = k_child_run("cdde_writer", args, 30000);
    EQ(r.exit_code, 0);
    CHECK(U(GetClipboardSequenceNumber)() > s0);
    CHECK(!U(IsClipboardFormatAvailable)(0x2A0));         /* the child emptied the clipboard */
    CHECK(U(IsClipboardFormatAvailable)(fx));
    EQ(U(GetClipboardOwner)(), NULL);              /* the child opened with a NULL window */
    CHECK(cd_open(NULL));
    unsigned char want[64], got[64] = {0};
    pattern(want, sizeof want, 5);
    CHECK(cd_get_bytes(fx, got, sizeof got) >= sizeof got);
    CHECK(!memcmp(got, want, sizeof want));
    WCHAR t[32] = {0};
    CHECK(cd_get_bytes(CF_UNICODETEXT, t, sizeof t) >= sizeof kChildText);
    EXPECT_WSTR(t, kChildText);
    char a[32] = {0};
    CHECK(cd_get_bytes(CF_TEXT, a, sizeof a) >= 17);
    EXPECT_STR(a, "child wrote this");
    U(CloseClipboard)();
}

/* ---- child: another process holds the clipboard open ---------------------------------------------------- */
static HANDLE named_event(DWORD ppid, const char *suffix, BOOL create) {
    WCHAR nm[96];
    wsprintfW(nm, L"Local\\win32kt_cd_%lu_%S", ppid, suffix);
    return create ? CreateEventW(NULL, TRUE, FALSE, nm) : OpenEventW(EVENT_ALL_ACCESS, FALSE, nm);
}

CHILD(cdde_holder) {
    (void)argc;
    DWORD ppid = strtoul(argv[1], NULL, 10);
    HANDLE held = named_event(ppid, "held", FALSE), rel = named_event(ppid, "release", FALSE);
    struct cd_ctx *c = cd_new();
    CH(held && rel && c);
    if (!held || !rel || !c) return cf ? cf : 1;
    CH(open_retry(c->hwnd));
    SetEvent(held);
    CH(WaitForSingleObject(rel, 10000) == WAIT_OBJECT_0);
    CH(U(CloseClipboard)());
    return cf;
}

static void wait_for(HANDLE ev, DWORD ms) { WaitForSingleObject(ev, ms); }

TEST(clipboard_dde_process_open_in_other_process_denied) {
    CD_SETUP();
    if (!cd_have_window()) { t_skip("no window station/display"); return; }
    HANDLE held = named_event(GetCurrentProcessId(), "held", TRUE), rel = named_event(GetCurrentProcessId(), "release", TRUE);
    if (!held || !rel) { t_skip("cannot create events"); return; }
    CHECK(cd_put(NULL, 0x2A1, "present", 8));
    PROCESS_INFORMATION pi;
    char args[32];
    snprintf(args, sizeof args, "%lu", GetCurrentProcessId());
    HANDLE ch = spawn_async("cdde_holder", args, &pi);
    if (!ch) { t_skip("cannot spawn child"); return; }
    wait_for(held, 10000);
    CHECK(WaitForSingleObject(held, 0) == WAIT_OBJECT_0);
    SetLastError(0xDEAD);
    CHECK(!U(OpenClipboard)(NULL));
    EXPECT_ERR(CD_ERR_ACCESS_DENIED);                     /* the clipboard lock belongs to the other process' thread */
    HWND ow = U(GetOpenClipboardWindow)();
    DWORD pid = 0;
    CHECK(ow != NULL);
    if (ow) { U(GetWindowThreadProcessId)(ow, &pid); EQ(pid, pi.dwProcessId); }
    CHECK(U(IsClipboardFormatAvailable)(0x2A1));          /* queries need no open */
    EQ(U(CountClipboardFormats)(), 1);
    SetLastError(0xDEAD);
    CHECK(U(GetClipboardData)(0x2A1) == NULL);
    EXPECT_ERR(CD_ERR_CLIPBOARD_NOT_OPEN);
    SetLastError(0xDEAD);
    CHECK(!U(EmptyClipboard)());
    EXPECT_ERR(CD_ERR_CLIPBOARD_NOT_OPEN);
    SetEvent(rel);
    EQ(wait_pump(ch, 15000), 0);
    CloseHandle(pi.hThread); CloseHandle(ch);
    CHECK(cd_open(NULL));                                 /* released when the holder closed */
    U(CloseClipboard)();
    CloseHandle(held); CloseHandle(rel);
}

/* ---- child: foreign process cannot close/empty/enumerate what the parent opened ------------------------ */
CHILD(cdde_closer) {
    (void)argc;
    HWND parent_wnd = (HWND)(ULONG_PTR)strtoull(argv[1], NULL, 16);
    SetLastError(0xDEAD);
    CH(!U(CloseClipboard)());
    CH(GetLastError() == CD_ERR_CLIPBOARD_NOT_OPEN);
    SetLastError(0xDEAD);
    CH(!U(EmptyClipboard)());
    CH(GetLastError() == CD_ERR_CLIPBOARD_NOT_OPEN);
    SetLastError(0xDEAD);
    CH(U(EnumClipboardFormats)(0) == 0);
    CH(GetLastError() == CD_ERR_CLIPBOARD_NOT_OPEN);
    SetLastError(0xDEAD);
    CH(!U(OpenClipboard)(NULL));
    CH(GetLastError() == CD_ERR_ACCESS_DENIED);
    CH(U(GetOpenClipboardWindow)() == parent_wnd);
    CH(U(IsClipboardFormatAvailable)(0x2A2));
    return cf;
}

TEST(clipboard_dde_process_foreign_close_empty_enum_fail) {
    CD_SETUP();
    if (!cd_have_window()) { t_skip("no window station/display"); return; }
    struct cd_ctx *c = cd_new();
    if (!c) { t_skip("no window"); return; }
    CHECK(cd_put(NULL, 0x2A2, "present", 8));
    CHECK(cd_open(c->hwnd));
    char args[32];
    snprintf(args, sizeof args, "%llx", (unsigned long long)(ULONG_PTR)c->hwnd);
    struct k_child_result r = k_child_run("cdde_closer", args, 30000);
    EQ(r.exit_code, 0);
    EQ(U(GetOpenClipboardWindow)(), c->hwnd);      /* still ours */
    CHECK(U(CloseClipboard)());
}

/* ---- child: delayed rendering requested from another process ------------------------------------------------ */
CHILD(cdde_renderer) {
    (void)argc;
    UINT fmt = (UINT)strtoul(argv[1], NULL, 16);
    CH(U(IsClipboardFormatAvailable)(fmt));
    CH(open_retry(NULL));
    HANDLE h = U(GetClipboardData)(fmt);                 /* kernel sends WM_RENDERFORMAT to the parent's window */
    CH(h != NULL);
    if (h) {
        char *p = GlobalLock(h);
        CH(p && !strcmp(p, "REMOTE-RENDERED"));
        if (p) GlobalUnlock(h);
    }
    CH(U(CloseClipboard)());
    return cf;
}

TEST(clipboard_dde_process_remote_delayed_render) {
    CD_SETUP();
    if (!cd_have_window()) { t_skip("no window station/display"); return; }
    struct cd_ctx *c = cd_new();
    if (!c) { t_skip("no window"); return; }
    c->render[0].fmt = 0x2A3; c->render[0].data = "REMOTE-RENDERED"; c->render[0].len = 16; c->nrender = 1;
    CHECK(cd_put(c->hwnd, 0x2A3, NULL, 0));
    PROCESS_INFORMATION pi;
    char args[16];
    snprintf(args, sizeof args, "%x", 0x2A3);
    HANDLE ch = spawn_async("cdde_renderer", args, &pi);
    if (!ch) { t_skip("cannot spawn child"); return; }
    EQ(wait_pump(ch, 20000), 0);                   /* this thread must service the cross-process SendMessage */
    CloseHandle(pi.hThread); CloseHandle(ch);
    EQ(CD_CNT(c, CD_WM_RENDERFORMAT), 1);
    EQ(c->last_w[CD_WM_RENDERFORMAT - 0x300], 0x2A3);
}

/* ---- child: owner process exits with real and delayed data on the clipboard ----------------------------------- */
CHILD(cdde_exiter) {
    (void)argc;
    DWORD ppid = strtoul(argv[1], NULL, 10);
    UINT fr = xfmt(ppid, "exit_real"), fd = xfmt(ppid, "exit_delayed");
    unsigned char pat[40];
    pattern(pat, sizeof pat, 9);
    struct cd_ctx *c = cd_new();
    CH(c != NULL);
    if (!c) return cf;
    CH(open_retry(c->hwnd));
    CH(U(EmptyClipboard)());
    CH(U(SetClipboardData)(fr, cd_gmem(pat, sizeof pat)) != NULL);
    U(SetClipboardData)(fd, NULL);
    CH(U(CloseClipboard)());
    return cf;                                            /* the window dies with the process */
}

TEST(clipboard_dde_process_exit_keeps_real_drops_delayed) {
    CD_SETUP();
    if (!cd_have_window()) { t_skip("no window station/display"); return; }
    UINT fr = xfmt(GetCurrentProcessId(), "exit_real"), fd = xfmt(GetCurrentProcessId(), "exit_delayed");
    char args[32];
    snprintf(args, sizeof args, "%lu", GetCurrentProcessId());
    struct k_child_result r = k_child_run("cdde_exiter", args, 30000);
    EQ(r.exit_code, 0);
    CHECK(U(IsClipboardFormatAvailable)(fr));              /* data in the kernel outlives its owner process */
    int gone = 0;
    for (int i = 0; i < 40 && !(gone = !U(IsClipboardFormatAvailable)(fd)); i++) Sleep(100);
    CHECK(gone);                                           /* the kernel dropped the never-rendered entry */
    EQ(U(GetClipboardOwner)(), NULL);
    CHECK(cd_open(NULL));
    unsigned char want[40], got[40] = {0};
    pattern(want, sizeof want, 9);
    CHECK(cd_get_bytes(fr, got, sizeof got) >= sizeof got);
    CHECK(!memcmp(got, want, sizeof want));
    U(CloseClipboard)();
}

/* ---- child: first win32k call of a fresh process ---------------------------------------------------------------- */
CHILD(cdde_fresh) {
    (void)argc;
    DWORD seq = strtoul(argv[1], NULL, 10);
    void *before = NULL, *after = NULL;
    k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &before);
    printf("  NOTE Win32ThreadInfo before first user32 call: %p\n", before);
    DWORD s = U(GetClipboardSequenceNumber)();            /* user32 loads, connects, the thread becomes a GUI thread */
    CH(s == seq);
    k_try_read_ptr((char *)k_teb() + TEB_Win32ThreadInfo, &after);
    CH(after != NULL);
    CH(!U(IsClipboardFormatAvailable)(0));
    CH(U(CountClipboardFormats)() >= 1);
    return cf;
}

TEST(clipboard_dde_process_fresh_child_sees_same_sequence) {
    CD_SETUP();
    if (!cd_have_window()) { t_skip("no window station/display"); return; }
    CHECK(cd_put(NULL, 0x2A4, "abcdefgh", 8));
    char args[32];
    snprintf(args, sizeof args, "%lu", (unsigned long)U(GetClipboardSequenceNumber)());
    struct k_child_result r = k_child_run("cdde_fresh", args, 30000);
    EQ(r.exit_code, 0);
}

/* ---- child: per-window-station isolation --------------------------------------------------------------------------- */
CHILD(cdde_winsta) {
    (void)argc; (void)argv;
    HINSTANCE hi = GetModuleHandleW(NULL);
    HWINSTA orig = U(GetProcessWindowStation)();
    HWND w = U(CreateWindowExW)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"cdws", WS_POPUP, -32000, -32000, 1, 1, NULL, NULL, hi, NULL);
    CH(w != NULL);
    if (!orig || !w) return cf ? cf : 1;
    CH(open_retry(w));
    CH(U(EmptyClipboard)());
    CH(U(SetClipboardData)(CF_UNICODETEXT, cd_gmem(L"station A", sizeof L"station A")) != NULL);
    CH(U(CloseClipboard)());
    U(DestroyWindow)(w);                                  /* the owner must not live across the station switch */
    DWORD seqA = U(GetClipboardSequenceNumber)();
    int cntA = U(CountClipboardFormats)();
    CH(seqA != 0 && cntA >= 1);

    WCHAR nm[64];
    wsprintfW(nm, L"win32kt_cd_ws_%lu", GetCurrentProcessId());
    HWINSTA ws = U(CreateWindowStationW)(nm, 0, GENERIC_ALL, NULL);
    if (!ws) { printf("  NOTE CreateWindowStationW failed (%lu): isolation not exercised\n", GetLastError()); return cf; }
    if (!U(SetProcessWindowStation)(ws)) {
        printf("  NOTE SetProcessWindowStation failed (%lu): isolation not exercised\n", GetLastError());
        U(CloseWindowStation)(ws);
        return cf;
    }
    CH(U(GetProcessWindowStation)() == ws);
    CH(!U(IsClipboardFormatAvailable)(CF_UNICODETEXT));   /* the new station has its own, empty clipboard */
    CH(U(CountClipboardFormats)() == 0);
    DWORD seqB0 = U(GetClipboardSequenceNumber)();
    printf("  NOTE new station sequence number %lu (original %lu)\n", seqB0, seqA);
    CH(open_retry(NULL));
    CH(U(EmptyClipboard)());
    CH(U(SetClipboardData)(CF_UNICODETEXT, cd_gmem(L"station B", sizeof L"station B")) != NULL);
    CH(U(CloseClipboard)());
    CH(U(IsClipboardFormatAvailable)(CF_UNICODETEXT));
    CH(U(GetClipboardSequenceNumber)() != seqB0);
    CH(open_retry(NULL));
    HANDLE h = U(GetClipboardData)(CF_UNICODETEXT);
    CH(h != NULL);
    if (h) { WCHAR *p = GlobalLock(h); CH(p && !lstrcmpW(p, L"station B")); if (p) GlobalUnlock(h); }
    U(CloseClipboard)();

    CH(U(SetProcessWindowStation)(orig));
    CH(U(IsClipboardFormatAvailable)(CF_UNICODETEXT));
    CH(U(CountClipboardFormats)() == cntA);               /* untouched by what happened in station B */
    CH(open_retry(NULL));
    h = U(GetClipboardData)(CF_UNICODETEXT);
    CH(h != NULL);
    if (h) { WCHAR *p = GlobalLock(h); CH(p && !lstrcmpW(p, L"station A")); if (p) GlobalUnlock(h); }
    U(CloseClipboard)();
    CH(U(GetClipboardSequenceNumber)() == seqA);
    U(CloseWindowStation)(ws);
    return cf;
}

TEST(clipboard_dde_winsta_isolation_child) {
    CD_SETUP();
    if (!cd_have_window()) { t_skip("no window station/display"); return; }
    REQUIRE(U(CreateWindowStationW) && U(SetProcessWindowStation) && U(GetProcessWindowStation));
    struct k_child_result r = k_child_run("cdde_winsta", "", 40000);
    EQ(r.exit_code, 0);
}
