/* x_ntqip_qip_image_session_peb: the string-valued process information classes of NtQueryInformationProcess on
 * Windows 10 22H2 x64.
 *   27  image file name as an NT device path       43  image file name as a DOS (drive letter) path
 *   60  command line                                50  window information (STARTUPINFO flags and title)
 * Measured: the UNICODE_STRING header followed by the characters in the caller's buffer, MaximumLength = Length + 2,
 * ReturnLength = header + MaximumLength (also written when the buffer is too small, with STATUS_INFO_LENGTH_MISMATCH),
 * the handle being looked at before the length, equality with the Win32 views (GetModuleFileName,
 * QueryFullProcessImageName, GetCommandLineW, GetStartupInfo), other processes, suspended, terminated and System. */
#include "ip.h"
#include <wchar.h>
/* query with a NULL return length pointer (the helper otherwise supplies one) */
static LONG g_qip_null_rl(ULONG cls, ULONG len) { return ip_q_nullrl((HANDLE)-1, cls, ip_buf(), len); }

const WCHAR *ip_check_us_layout(const unsigned char *out, ULONG rl, ULONG cls, USHORT *len_bytes) {
    const IPUS *u = (const IPUS *)out;
    CHECKF(u->Length % 2 == 0, "class %lu: odd Length %u", cls, u->Length);
    CHECKF(u->MaximumLength == u->Length + 2, "class %lu: Length %u Maximum %u", cls, u->Length, u->MaximumLength);
    CHECKF(rl == 16 + (ULONG)u->MaximumLength, "class %lu: return length %lu, Maximum %u", cls, rl, u->MaximumLength);
    CHECKF((const unsigned char *)u->Buffer == out + 16, "class %lu: Buffer not at header+16", cls);
    if ((const unsigned char *)u->Buffer == out + 16) CHECKF(u->Buffer[u->Length / 2] == 0, "class %lu: no terminator", cls);
    if (len_bytes) *len_bytes = u->Length;
    return u->Buffer;
}
BOOL ip_image_path_of(HANDLE h, DWORD flags, WCHAR *out, DWORD cap) {
    DWORD n = cap; return QueryFullProcessImageNameW(h, flags, out, &n);
}
static int ieq(const WCHAR *a, const WCHAR *b) { return !_wcsicmp(a, b); }

/* ---- running a child with pipes for its output ----------------------------------------------------------- */
int ip_run_child(const char *name, const char *args, DWORD cflags, DWORD sflags, const char *title, char *out, size_t cap, DWORD *code) {
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE }; HANDLE rd, wr; STARTUPINFOA si; PROCESS_INFORMATION pi; char cmd[2048];
    if (!CreatePipe(&rd, &wr, &sa, 0)) return 0;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    memset(&si, 0, sizeof si); si.cb = sizeof si; si.dwFlags = sflags | STARTF_USESTDHANDLES; si.hStdOutput = wr; si.hStdError = wr;
    if (sflags & STARTF_USESHOWWINDOW) si.wShowWindow = SW_HIDE;
    if (sflags & STARTF_USEPOSITION) { si.dwX = 10; si.dwY = 20; }
    if (title) si.lpTitle = (char *)title;
    snprintf(cmd, sizeof cmd, "\"%s\" --child %s %s", t_self_path(), name, args ? args : "");
    memset(&pi, 0, sizeof pi);
    BOOL ok = CreateProcessA(NULL, cmd, NULL, NULL, TRUE, cflags, NULL, NULL, &si, &pi);
    CloseHandle(wr);
    if (!ok) { CloseHandle(rd); return 0; }
    size_t n = 0; DWORD got;
    while (n + 1 < cap && ReadFile(rd, out + n, (DWORD)(cap - 1 - n), &got, NULL) && got) n += got;
    out[n] = 0;
    if (WaitForSingleObject(pi.hProcess, 30000) != WAIT_OBJECT_0) TerminateProcess(pi.hProcess, 1);
    GetExitCodeProcess(pi.hProcess, code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread); CloseHandle(rd);
    return 1;
}

/* The child looks at itself: window information against GetStartupInfo, command line against GetCommandLineW, both
 * image paths against the Win32 views, and prints the console host value. Failures are counted in the exit code. */
CHILD(selfq) {
    static unsigned char b[0x4000]; ULONG rl; int bad = 0;
    ip_init();
    STARTUPINFOW si; GetStartupInfoW(&si);
    LONG st = ip_q((HANDLE)-1, C_WININFO, b, sizeof b, &rl);
    if (st != 0) { printf("BAD wininfo status 0x%lx\n", (unsigned long)st); bad++; }
    else {
        const WCHAR *want = si.lpTitle; WCHAR img[MAX_PATH]; if (!want) { GetModuleFileNameW(NULL, img, MAX_PATH); want = img; }
        USHORT tl = *(USHORT *)(b + 4);
        if (*(ULONG *)b != si.dwFlags) { printf("BAD flags %lx vs %lx\n", *(ULONG *)b, si.dwFlags); bad++; }
        if (tl != wcslen(want) * 2 || memcmp(b + 6, want, tl)) { printf("BAD title\n"); bad++; }
        if (rl != 6u + tl) { printf("BAD wininfo rl %lu\n", rl); bad++; }
        printf("WININFO flags=%lx\n", *(ULONG *)b);
    }
    st = ip_q((HANDLE)-1, C_CMDLINE, b, sizeof b, &rl);
    if (st != 0 || wcscmp(((IPUS *)b)->Buffer, GetCommandLineW())) { printf("BAD cmdline\n"); bad++; }
    WCHAR mf[MAX_PATH], q[MAX_PATH];
    GetModuleFileNameW(NULL, mf, MAX_PATH);
    st = ip_q((HANDLE)-1, C_DOSPATH, b, sizeof b, &rl);
    if (st != 0 || _wcsicmp(((IPUS *)b)->Buffer, mf)) { printf("BAD dospath\n"); bad++; }
    st = ip_q((HANDLE)-1, C_NTPATH, b, sizeof b, &rl);
    DWORD n = MAX_PATH; if (!QueryFullProcessImageNameW(GetCurrentProcess(), PROCESS_NAME_NATIVE, q, &n) || st != 0 || _wcsicmp(((IPUS *)b)->Buffer, q)) { printf("BAD ntpath\n"); bad++; }
    unsigned long long v = 0xAAAAAAAAAAAAAAAAull; st = ip_q((HANDLE)-1, C_CONHOST, &v, 8, &rl);
    printf("CONHOST st=%lx value=%llx\n", (unsigned long)st, v);
    return bad;
}

/* ---- classes 27 / 43: image path -------------------------------------------------------------------------- */
static void path_layout_self(ULONG cls) {
    if (!ip_init()) return;
    unsigned char *b = ip_buf(); ULONG rl; ip_fill(b, 0x2000);
    LONG st = ip_q((HANDLE)-1, cls, b, 0x1000, &rl);
    EXPECT_EQ(st, ST_OK);
    if (st) return;
    USHORT n; const WCHAR *s = ip_check_us_layout(b, rl, cls, &n);
    CHECKF(n >= 8, "class %lu: empty path", cls);
    CHECKF(ip_all_filler(b + rl, 0x2000 - rl), "class %lu: wrote past the return length", cls);
    if (cls == C_NTPATH) CHECK(!_wcsnicmp(s, L"\\Device\\", 8));
    else CHECKF(s[1] == L':' && s[2] == L'\\' && ((s[0] | 0x20) >= 'a' && (s[0] | 0x20) <= 'z'), "class 43: not a drive path");
}
TEST(ip_ntpath_layout_self) { path_layout_self(C_NTPATH); }
TEST(ip_dospath_layout_self) { path_layout_self(C_DOSPATH); }

TEST(ip_dospath_equals_win32_views) {
    if (!ip_init()) return;
    unsigned char *b = ip_buf(); ULONG rl; WCHAR mf[MAX_PATH], q[MAX_PATH];
    EXPECT_EQ(ip_q((HANDLE)-1, C_DOSPATH, b, 0x1000, &rl), ST_OK);
    GetModuleFileNameW(NULL, mf, MAX_PATH);
    CHECK(ieq(((IPUS *)b)->Buffer, mf));
    CHECK(ip_image_path_of(GetCurrentProcess(), 0, q, MAX_PATH) && ieq(((IPUS *)b)->Buffer, q));
}
TEST(ip_ntpath_equals_native_image_name) {
    if (!ip_init()) return;
    unsigned char *b = ip_buf(); ULONG rl; WCHAR q[MAX_PATH];
    EXPECT_EQ(ip_q((HANDLE)-1, C_NTPATH, b, 0x1000, &rl), ST_OK);
    CHECK(ip_image_path_of(GetCurrentProcess(), PROCESS_NAME_NATIVE, q, MAX_PATH) && ieq(((IPUS *)b)->Buffer, q));
}
TEST(ip_nt_and_dos_path_name_the_same_file) {
    if (!ip_init()) return;
    static unsigned char b1[0x1000], b2[0x1000]; ULONG r1, r2;
    EXPECT_EQ(ip_q((HANDLE)-1, C_NTPATH, b1, sizeof b1, &r1), ST_OK);
    EXPECT_EQ(ip_q((HANDLE)-1, C_DOSPATH, b2, sizeof b2, &r2), ST_OK);
    const WCHAR *nt = ((IPUS *)b1)->Buffer, *dos = ((IPUS *)b2)->Buffer;
    size_t nl = wcslen(nt), dl = wcslen(dos);
    CHECKF(dl > 2 && nl >= dl - 2 && ieq(nt + nl - (dl - 2), dos + 2), "tails of the two forms differ");
    CHECKF(((IPUS *)b1)->Length == nl * 2 && ((IPUS *)b2)->Length == dl * 2, "Length does not cover the whole string");
}
static void path_too_small(ULONG cls) {
    if (!ip_init()) return;
    unsigned char *b = ip_buf(); ULONG need, rl;
    EXPECT_EQ(ip_q((HANDLE)-1, cls, b, 0x1000, &need), ST_OK);
    ULONG lens[] = { 0, 4, 8, 15, 16, 17, 32, need - 4, need - 2, need - 1 };
    for (int i = 0; i < 10; i++) {
        ip_fill(b, 0x1000);
        LONG st = ip_q((HANDLE)-1, cls, b, lens[i], &rl);
        CHECKF(st == ST_INFO_LEN, "class %lu len %lu: status 0x%lx", cls, lens[i], (unsigned long)st);
        CHECKF(rl == need, "class %lu len %lu: return length %lu want %lu", cls, lens[i], rl, need);
        CHECKF(ip_all_filler(b, 0x1000), "class %lu len %lu: buffer touched on failure", cls, lens[i]);
    }
    ip_fill(b, 0x1000);
    EXPECT_EQ(ip_q((HANDLE)-1, cls, b, need, &rl), ST_OK);          /* exactly enough */
    CHECK(rl == need);
    EXPECT_EQ(ip_q((HANDLE)-1, cls, b, need + 0x800, &rl), ST_OK);  /* more than enough: same return length */
    CHECK(rl == need);
    EXPECT_EQ(g_qip_null_rl(cls, 16), ST_INFO_LEN);                 /* NULL return length, too small: no fault */
}
TEST(ip_ntpath_too_small) { path_too_small(C_NTPATH); }
TEST(ip_dospath_too_small) { path_too_small(C_DOSPATH); }
TEST(ip_paths_null_buffer_len0_reports_needed_size) {
    if (!ip_init()) return;
    ULONG cls[] = { C_NTPATH, C_DOSPATH, C_CMDLINE }; ULONG rl, need;
    for (int i = 0; i < 3; i++) {
        EXPECT_EQ(ip_q((HANDLE)-1, cls[i], ip_buf(), 0x1000, &need), ST_OK);
        LONG st = ip_q((HANDLE)-1, cls[i], NULL, 0, &rl);
        CHECKF(st == ST_INFO_LEN && rl == need, "class %lu: 0x%lx rl %lu need %lu", cls[i], (unsigned long)st, rl, need);
    }
}

/* handle is looked at first (even with length 0) */
static void handle_first(ULONG cls) {
    if (!ip_init()) return;
    unsigned char *b = ip_buf(); ULONG rl; HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    struct kid k; if (!kid_start(&k, "", 0, NULL, 0, NULL)) { t_skip("no child"); return; }
    kid_wait_ready(&k);
    HANDLE sy = kid_open(&k, SYNCHRONIZE);
    struct { const char *n; HANDLE h; LONG want; } H[] = {
        { "null", NULL, ST_INVALID_HANDLE }, { "unused", (HANDLE)(ULONG_PTR)0x7ffc0000, ST_INVALID_HANDLE },
        { "thread pseudo", (HANDLE)(LONG_PTR)-2, ST_TYPE_MISMATCH }, { "event", ev, ST_TYPE_MISMATCH }, { "sync only", sy, ST_ACCESS_DENIED },
    };
    ULONG lens[] = { 0, 8, 16, 0x1000 };
    for (int i = 0; i < 5; i++) for (int j = 0; j < 4; j++) {
        ip_fill(b, 0x1000);
        LONG st = ip_q(H[i].h, cls, b, lens[j], &rl);
        CHECKF(st == H[i].want, "class %lu %s len %lu: 0x%lx want 0x%lx", cls, H[i].n, lens[j], (unsigned long)st, (unsigned long)H[i].want);
        CHECKF(rl == RL_SENT && ip_all_filler(b, 0x1000), "class %lu %s: output written", cls, H[i].n);
    }
    if (sy) CloseHandle(sy);
    CloseHandle(ev); kid_stop(&k);
}
TEST(ip_ntpath_handle_checked_before_length) { handle_first(C_NTPATH); }
TEST(ip_dospath_handle_checked_before_length) { handle_first(C_DOSPATH); }
TEST(ip_cmdline_handle_checked_before_length) { handle_first(C_CMDLINE); }
TEST(ip_wininfo_handle_checked_before_length) { handle_first(C_WININFO); }

TEST(ip_paths_of_another_process_with_limited_right) {
    if (!ip_init()) return;
    struct kid k; if (!kid_start(&k, "", 0, NULL, 0, NULL)) { t_skip("no child"); return; }
    kid_wait_ready(&k);
    static unsigned char b1[0x1000], b2[0x1000]; ULONG r1, r2; WCHAR q[MAX_PATH];
    HANDLE h[2] = { kid_open(&k, PROC_LIM), kid_open(&k, PROC_QRY) };
    for (int i = 0; i < 2; i++) {
        if (!h[i]) { t_skip("OpenProcess refused"); continue; }
        EXPECT_EQ(ip_q(h[i], C_NTPATH, b1, sizeof b1, &r1), ST_OK);
        EXPECT_EQ(ip_q(h[i], C_DOSPATH, b2, sizeof b2, &r2), ST_OK);
        CHECK(ip_image_path_of(h[i], PROCESS_NAME_NATIVE, q, MAX_PATH) && ieq(((IPUS *)b1)->Buffer, q));
        CHECK(ip_image_path_of(h[i], 0, q, MAX_PATH) && ieq(((IPUS *)b2)->Buffer, q));
        WCHAR mf[MAX_PATH]; GetModuleFileNameW(NULL, mf, MAX_PATH);
        CHECK(ieq(((IPUS *)b2)->Buffer, mf));       /* the child runs the same image */
        CloseHandle(h[i]);
    }
    kid_stop(&k);
}
TEST(ip_paths_of_a_copied_image_name_the_copy) {
    if (!ip_init()) return;
    char cp[MAX_PATH]; t_tmpfile(cp, sizeof cp, "ipcopy.exe");
    if (!CopyFileA(t_self_path(), cp, FALSE)) { t_skip("cannot copy the image"); return; }
    struct kid k; if (!kid_start(&k, "", 0, NULL, 0, cp)) { DeleteFileA(cp); t_skip("cannot start the copy"); return; }
    kid_wait_ready(&k);
    static unsigned char b1[0x1000], b2[0x1000], b3[0x1000]; ULONG r; WCHAR q[MAX_PATH];
    HANDLE h = kid_open(&k, PROC_LIM);
    if (h) {
        EXPECT_EQ(ip_q(h, C_DOSPATH, b1, sizeof b1, &r), ST_OK);
        EXPECT_EQ(ip_q(h, C_NTPATH, b2, sizeof b2, &r), ST_OK);
        EXPECT_EQ(ip_q(h, C_CMDLINE, b3, sizeof b3, &r), ST_OK);
        const WCHAR *d = ((IPUS *)b1)->Buffer; size_t dl = wcslen(d);
        CHECKF(dl > 11 && ieq(d + dl - 11, L"\\ipcopy.exe"), "dos path does not end in the copy's name");
        CHECK(ip_image_path_of(h, 0, q, MAX_PATH) && ieq(d, q));
        CHECK(ip_image_path_of(h, PROCESS_NAME_NATIVE, q, MAX_PATH) && ieq(((IPUS *)b2)->Buffer, q));
        WCHAR mf[MAX_PATH]; GetModuleFileNameW(NULL, mf, MAX_PATH);
        CHECKF(!ieq(d, mf), "the copy reports the original image path");
        const WCHAR *c = ((IPUS *)b3)->Buffer;
        CHECKF(c[0] == L'"' && wcsstr(c, L"ipcopy.exe\" --child park "), "command line of the copy is not its own");
        CloseHandle(h);
    } else t_skip("OpenProcess refused");
    kid_stop(&k); DeleteFileA(cp);
}

/* terminated / suspended / System */
TEST(ip_terminated_process_string_classes) {
    if (!ip_init()) return;
    struct kid k; if (!kid_start(&k, "", 0, NULL, 0, NULL)) { t_skip("no child"); return; }
    kid_wait_ready(&k);
    TerminateProcess(k.h, 3); WaitForSingleObject(k.h, 10000);
    unsigned char *b = ip_buf(); ULONG rl;
    EXPECT_EQ(ip_q(k.h, C_NTPATH, b, 0x1000, &rl), ST_OK);          /* the NT name survives the exit */
    EXPECT_EQ(ip_q(k.h, C_DOSPATH, b, 0x1000, &rl), ST_UNSUCCESSFUL);
    EXPECT_EQ(ip_q(k.h, C_CMDLINE, b, 0x1000, &rl), ST_TERMINATING);
    EXPECT_EQ(ip_q(k.h, C_WININFO, b, 0x1000, &rl), ST_TERMINATING);
    EXPECT_EQ(ip_q(k.h, C_IMAGEINFO, b, 0x40, &rl), ST_TERMINATING);
    ULONG v; EXPECT_EQ(ip_q(k.h, C_SESSION, &v, 4, &rl), ST_OK);
    EXPECT_EQ(ip_q(k.h, C_LUIDMAPS, &v, 4, &rl), ST_OK);
    kid_stop(&k);
}
TEST(ip_suspended_process_string_classes) {
    if (!ip_init()) return;
    struct kid k; if (!kid_start(&k, "", CREATE_SUSPENDED, "Suspended Title", 0, NULL)) { t_skip("no child"); return; }
    static unsigned char b[0x1000]; ULONG rl; WCHAR q[MAX_PATH];
    EXPECT_EQ(ip_q(k.h, C_DOSPATH, b, sizeof b, &rl), ST_OK);
    CHECK(ip_image_path_of(k.h, 0, q, MAX_PATH) && ieq(((IPUS *)b)->Buffer, q));
    EXPECT_EQ(ip_q(k.h, C_NTPATH, b, sizeof b, &rl), ST_OK);
    EXPECT_EQ(ip_q(k.h, C_CMDLINE, b, sizeof b, &rl), ST_OK);
    CHECK(wcsstr(((IPUS *)b)->Buffer, L" --child park "));
    EXPECT_EQ(ip_q(k.h, C_WININFO, b, sizeof b, &rl), ST_OK);
    CHECK(*(USHORT *)(b + 4) == 15 * 2 && !memcmp(b + 6, L"Suspended Title", 30));
    EXPECT_EQ(ip_q(k.h, C_IMAGEINFO, b, 0x40, &rl), ST_OK);
    ResumeThread(k.th); kid_stop(&k);
}
TEST(ip_system_process_string_classes) {
    if (!ip_init()) return;
    HANDLE h = OpenProcess(PROC_LIM, FALSE, 4);
    if (!h) { t_skip("cannot open the System process with limited query right (restricted token)"); return; }
    unsigned char *b = ip_buf(); ULONG rl; ip_fill(b, 0x100);
    EXPECT_EQ(ip_q(h, C_NTPATH, b, 0x100, &rl), ST_OK);             /* no image: an empty string */
    CHECKF(rl == 16 && ((IPUS *)b)->Length == 0, "rl %lu len %u", rl, ((IPUS *)b)->Length);
    EXPECT_EQ(ip_q(h, C_DOSPATH, b, 0x100, &rl), ST_UNSUCCESSFUL);
    EXPECT_EQ(ip_q(h, C_CMDLINE, b, 0x100, &rl), ST_NOT_FOUND);
    EXPECT_EQ(ip_q(h, C_IMAGEINFO, b, 0x40, &rl), ST_INVALID_PARAM);
    ULONG sid = 0xAAAA; EXPECT_EQ(ip_q(h, C_SESSION, &sid, 4, &rl), ST_OK); CHECK(sid == 0);
    CloseHandle(h);
}

/* ---- class 60: command line --------------------------------------------------------------------------------- */
TEST(ip_cmdline_layout_self) {
    if (!ip_init()) return;
    unsigned char *b = ip_buf(); ULONG rl; ip_fill(b, 0x2000);
    EXPECT_EQ(ip_q((HANDLE)-1, C_CMDLINE, b, 0x1000, &rl), ST_OK);
    USHORT n; const WCHAR *s = ip_check_us_layout(b, rl, C_CMDLINE, &n);
    CHECKF(!wcscmp(s, GetCommandLineW()), "differs from GetCommandLineW");
    CHECK(n == wcslen(GetCommandLineW()) * 2);
    CHECK(ip_all_filler(b + rl, 0x2000 - rl));
}
TEST(ip_cmdline_too_small) {
    if (!ip_init()) return;
    unsigned char *b = ip_buf(); ULONG need, rl;
    EXPECT_EQ(ip_q((HANDLE)-1, C_CMDLINE, b, 0x1000, &need), ST_OK);
    ULONG lens[] = { 0, 8, 15, 16, 17, need - 4, need - 2 };
    for (int i = 0; i < 7; i++) {
        ip_fill(b, 0x1000);
        LONG st = ip_q((HANDLE)-1, C_CMDLINE, b, lens[i], &rl);
        CHECKF(st == ST_INFO_LEN && rl == need && ip_all_filler(b, 0x1000), "len %lu: 0x%lx rl %lu need %lu", lens[i], (unsigned long)st, rl, need);
    }
    EXPECT_EQ(ip_q((HANDLE)-1, C_CMDLINE, b, need, &rl), ST_OK);
    EXPECT_EQ(g_qip_null_rl(C_CMDLINE, 16), ST_INFO_LEN);
}
static void cmdline_of_child_eq(const char *extra, DWORD cflags, int unquoted) {
    if (!ip_init()) return;
    struct kid k; char want[8192]; static unsigned char b[0x4000]; ULONG rl;
    if (!kid_start(&k, extra, cflags, NULL, 0, NULL)) { t_skip("no child"); return; }
    snprintf(want, sizeof want, "\"%s\" --child park %lu%s%s", t_self_path(), GetCurrentProcessId(), *extra ? " " : "", extra);
    WCHAR w[8192]; MultiByteToWideChar(CP_ACP, 0, want, -1, w, 8192);
    if (!(cflags & CREATE_SUSPENDED)) kid_wait_ready(&k);
    HANDLE h = kid_open(&k, PROC_LIM);
    if (h) {
        EXPECT_EQ(ip_q(h, C_CMDLINE, b, sizeof b, &rl), ST_OK);
        CHECKF(!wcscmp(((IPUS *)b)->Buffer, w), "child command line text differs");
        CHECK(rl == 16 + 2 * (wcslen(w) + 1));
        CloseHandle(h);
    }
    if (cflags & CREATE_SUSPENDED) ResumeThread(k.th);
    kid_stop(&k);
}
TEST(ip_cmdline_of_child_plain_arguments) { cmdline_of_child_eq("alpha beta", 0, 0); }
TEST(ip_cmdline_of_child_quoted_arguments) { cmdline_of_child_eq("\"two words\" 'single' tab\tx", 0, 0); }
TEST(ip_cmdline_of_suspended_child) { cmdline_of_child_eq("one \"two three\"", CREATE_SUSPENDED, 0); }
TEST(ip_cmdline_of_child_long) {
    char big[1801]; memset(big, 'x', 1800); big[1800] = 0;
    cmdline_of_child_eq(big, 0, 0);
}
TEST(ip_cmdline_without_quoting_is_kept_verbatim) {
    if (!ip_init()) return;
    STARTUPINFOA si; PROCESS_INFORMATION pi; char cmd[1024]; static unsigned char b[0x1000]; ULONG rl;
    memset(&si, 0, sizeof si); si.cb = sizeof si;
    snprintf(cmd, sizeof cmd, "%s --child park %lu   spaced   args", t_self_path(), GetCurrentProcessId());
    if (!CreateProcessA(t_self_path(), cmd, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &si, &pi)) { t_skip("cannot start"); return; }
    EXPECT_EQ(ip_q(pi.hProcess, C_CMDLINE, b, sizeof b, &rl), ST_OK);
    WCHAR w[1024]; MultiByteToWideChar(CP_ACP, 0, cmd, -1, w, 1024);
    CHECKF(!wcscmp(((IPUS *)b)->Buffer, w), "text differs");
    TerminateProcess(pi.hProcess, 0); WaitForSingleObject(pi.hProcess, 10000); CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
}

/* ---- class 50: window information --------------------------------------------------------------------------- */
static LONG wininfo(HANDLE h, ULONG len, ULONG *rl) { return ip_q(h, C_WININFO, ip_buf(), len, rl); }
TEST(ip_wininfo_self_matches_startupinfo) {
    if (!ip_init()) return;
    unsigned char *b = ip_buf(); ULONG rl; ip_fill(b, 0x2000);
    EXPECT_EQ(wininfo((HANDLE)-1, 0x1000, &rl), ST_OK);
    STARTUPINFOW si; GetStartupInfoW(&si);
    WCHAR img[MAX_PATH]; GetModuleFileNameW(NULL, img, MAX_PATH);
    const WCHAR *want = si.lpTitle ? si.lpTitle : img; USHORT tl = *(USHORT *)(b + 4);
    CHECKF(*(ULONG *)b == si.dwFlags, "flags 0x%lx vs 0x%lx", *(ULONG *)b, si.dwFlags);
    CHECKF(tl == wcslen(want) * 2 && !memcmp(b + 6, want, tl), "title differs");
    CHECKF(rl == 6u + tl, "return length %lu title bytes %u", rl, tl);
    CHECKF(ip_all_filler(b + rl, 0x2000 - rl), "wrote past the return length (terminator?)");
}
TEST(ip_wininfo_child_title_and_flags) {
    if (!ip_init()) return;
    struct kid k; if (!kid_start(&k, "", 0, "Window Info Title", STARTF_USESHOWWINDOW | STARTF_USEPOSITION | STARTF_USESIZE, NULL)) { t_skip("no child"); return; }
    kid_wait_ready(&k);
    unsigned char *b = ip_buf(); ULONG rl; ip_fill(b, 0x200);
    EXPECT_EQ(wininfo(k.h, 0x200, &rl), ST_OK);
    CHECKF(*(ULONG *)b == (STARTF_USESHOWWINDOW | STARTF_USEPOSITION | STARTF_USESIZE), "flags 0x%lx", *(ULONG *)b);
    CHECK(*(USHORT *)(b + 4) == 17 * 2 && !memcmp(b + 6, L"Window Info Title", 34));
    CHECK(rl == 6 + 34);
    kid_stop(&k);
}
TEST(ip_wininfo_default_title_is_the_image_path) {
    if (!ip_init()) return;
    struct kid k; if (!kid_start(&k, "", 0, NULL, 0, NULL)) { t_skip("no child"); return; }
    kid_wait_ready(&k);
    unsigned char *b = ip_buf(); ULONG rl; WCHAR q[MAX_PATH];
    EXPECT_EQ(wininfo(k.h, 0x1000, &rl), ST_OK);
    CHECK(*(ULONG *)b == 0);
    CHECK(ip_image_path_of(k.h, 0, q, MAX_PATH));
    WCHAR t[MAX_PATH]; USHORT tl = *(USHORT *)(b + 4); memcpy(t, b + 6, tl); t[tl / 2] = 0;
    CHECKF(ieq(t, q), "default title is not the image path");
    kid_stop(&k);
}
TEST(ip_wininfo_empty_title) {
    if (!ip_init()) return;
    struct kid k; if (!kid_start(&k, "", 0, "", 0, NULL)) { t_skip("no child"); return; }
    kid_wait_ready(&k);
    ULONG rl; ip_fill(ip_buf(), 0x40);
    EXPECT_EQ(wininfo(k.h, 0x40, &rl), ST_OK);
    CHECK(*(USHORT *)(ip_buf() + 4) == 0);
    CHECK(rl == 6);
    EXPECT_EQ(wininfo(k.h, 5, &rl), ST_INFO_LEN);
    CHECK(rl == 6);
    kid_stop(&k);
}
TEST(ip_wininfo_wide_title) {
    if (!ip_init()) return;
    static const WCHAR t[] = { 'T', 0x00e9, 's', 't', 0x4e2d, 0 };
    struct kid k; if (!kid_start_w(&k, t, 0)) { t_skip("no child"); return; }
    kid_wait_ready(&k);
    ULONG rl; ip_fill(ip_buf(), 0x100);
    EXPECT_EQ(wininfo(k.h, 0x100, &rl), ST_OK);
    CHECK(*(USHORT *)(ip_buf() + 4) == 10 && !memcmp(ip_buf() + 6, t, 10));
    kid_stop(&k);
}
TEST(ip_wininfo_long_title_and_sizes) {
    if (!ip_init()) return;
    char big[601]; memset(big, 'T', 600); big[600] = 0;
    struct kid k; if (!kid_start(&k, "", 0, big, 0, NULL)) { t_skip("no child"); return; }
    kid_wait_ready(&k);
    ULONG rl; unsigned char *b = ip_buf();
    ULONG lens[] = { 0, 5, 6, 7, 605, 1205 };
    for (int i = 0; i < 6; i++) {
        ip_fill(b, 0x2000);
        LONG st = wininfo(k.h, lens[i], &rl);
        CHECKF(st == ST_INFO_LEN && rl == 1206 && ip_all_filler(b, 0x2000), "len %lu: 0x%lx rl %lu", lens[i], (unsigned long)st, rl);
    }
    EXPECT_EQ(wininfo(k.h, 1206, &rl), ST_OK);
    CHECK(rl == 1206 && *(USHORT *)(b + 4) == 1200);
    EXPECT_EQ(g_qip_null_rl(C_WININFO, 16), ST_INFO_LEN);
    kid_stop(&k);
}
TEST(ip_wininfo_too_small_for_self) {
    if (!ip_init()) return;
    unsigned char *b = ip_buf(); ULONG need, rl;
    EXPECT_EQ(wininfo((HANDLE)-1, 0x1000, &need), ST_OK);
    for (ULONG len = 0; len < need; len += (len < 16 ? 1 : 7)) {
        ip_fill(b, 0x1000);
        LONG st = wininfo((HANDLE)-1, len, &rl);
        CHECKF(st == ST_INFO_LEN && rl == need && ip_all_filler(b, 0x1000), "len %lu: 0x%lx rl %lu", len, (unsigned long)st, rl);
    }
}
TEST(ip_wininfo_suspended_child_has_title) {
    if (!ip_init()) return;
    struct kid k; if (!kid_start(&k, "", CREATE_SUSPENDED, "Early", STARTF_USESHOWWINDOW, NULL)) { t_skip("no child"); return; }
    ULONG rl; EXPECT_EQ(wininfo(k.h, 0x100, &rl), ST_OK);
    CHECK(*(ULONG *)ip_buf() == STARTF_USESHOWWINDOW && *(USHORT *)(ip_buf() + 4) == 10);
    ResumeThread(k.th); kid_stop(&k);
}
/* the process looks at itself in a child that gets other startup parameters */
static void selfq_run(DWORD cflags, DWORD sflags, const char *title, const char *args, char *out, size_t cap) {
    DWORD code = 99;
    if (!ip_run_child("selfq", args, cflags, sflags, title, out, cap, &code)) { CHECK(0 && "cannot start child"); return; }
    CHECKF(code == 0, "child reported %lu mismatches: %s", code, out);
}
TEST(ip_wininfo_cmdline_paths_child_with_title_flags) {
    if (!ip_init()) return;
    char out[4096]; selfq_run(0, STARTF_USESHOWWINDOW | STARTF_USEPOSITION, "Self Query Title", "p \"q r\" s", out, sizeof out);
    CHECK(strstr(out, "WININFO flags=") != NULL);
}
TEST(ip_wininfo_cmdline_paths_child_default_startup) {
    if (!ip_init()) return;
    char out[4096]; selfq_run(0, 0, NULL, "", out, sizeof out);
}
TEST(ip_wininfo_cmdline_paths_child_detached) {
    if (!ip_init()) return;
    char out[4096]; selfq_run(DETACHED_PROCESS, 0, "TT", "x  y", out, sizeof out);
}
