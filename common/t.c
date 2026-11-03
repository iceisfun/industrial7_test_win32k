#include "t.h"
#include <setjmp.h>
#include "modules.h"

static const char *g_mods[] = { T_MODULE_LIST, NULL };
static int g_pass, g_fail, g_cur_fail, g_cur_skip;
static const char *g_cur;
static jmp_buf g_jb;
static volatile int g_in_test;

HMODULE t_module(const char *dll) { return LoadLibraryA(dll); }   /* refcounted; modules stay loaded */
/* Wine only: exports that are "stub" spec entries abort the whole process when called (win32k.sys does not exist there).
 * Their thunk is  sub rsp,28; lea rcx,[rip+x]; lea rdx,[rip+y]; call __wine_spec_unimplemented_stub.  Report such exports as absent
 * so tests REQUIRE()/skip instead of killing the run. Real Windows never matches this pattern at a syscall stub. */
static int wine_stub_thunk(FARPROC f) {
    const unsigned char *b = (const unsigned char *)f;
    return b && b[0] == 0x48 && b[1] == 0x83 && b[2] == 0xec && b[3] == 0x28 && b[4] == 0x48 && b[5] == 0x8d && b[6] == 0x0d &&
           b[11] == 0x48 && b[12] == 0x8d && b[13] == 0x15 && b[18] == 0xe8;
}
/* LastStatusValue (TEB+0x1250, x64) is the NTSTATUS of the last failing ntdll call on this thread. Lookups
 * (LoadLibrary, GetProcAddress) overwrite it; tests that plant a sentinel in it and then call UN()/U() expect it
 * to survive the lookup, so the helpers save and restore it next to the last-error value. */
static inline volatile DWORD *t_last_status_ptr(void) { return (volatile DWORD *)((char *)NtCurrentTeb() + 0x1250); }
/* The lookup helpers never disturb the caller's last-error value: a miss in an earlier module of the search list
 * (GetProcAddress sets ERROR_PROC_NOT_FOUND) must not leak into a test that checks GetLastError() after the call. */
FARPROC t_proc_in(const char *dll, const char *name) {
    DWORD le = GetLastError(), ls = *t_last_status_ptr();
    HMODULE m = t_module(dll); FARPROC p;
    if (!m) { SetLastError(le); *t_last_status_ptr() = ls; return NULL; }
    p = GetProcAddress(m, name);
    if (p && t_is_wine() && wine_stub_thunk(p)) p = NULL;
    SetLastError(le); *t_last_status_ptr() = ls;
    return p;
}
FARPROC t_proc_ord(const char *dll, unsigned ord) {
    DWORD le = GetLastError(), ls = *t_last_status_ptr();
    HMODULE m = t_module(dll); FARPROC p = m ? GetProcAddress(m, (LPCSTR)(ULONG_PTR)ord) : NULL;
    SetLastError(le); *t_last_status_ptr() = ls;
    return p;
}
FARPROC t_proc(const char *name) {
    for (int i = 0; g_mods[i]; i++) { FARPROC p = t_proc_in(g_mods[i], name); if (p) return p; }
    return NULL;
}
int t_is_wine(void) {
    DWORD le = GetLastError(), ls = *t_last_status_ptr();   /* the failed lookup sets ERROR_PROC_NOT_FOUND; callers check GetLastError() afterwards */
    HMODULE n = GetModuleHandleA("ntdll.dll");
    int r = n && GetProcAddress(n, "wine_get_version") != NULL;
    SetLastError(le); *t_last_status_ptr() = ls;
    return r;
}

#define MAXT 8192
static struct { const char *n; t_fn f; } g_t[MAXT];
static int g_nt;
void t_register(const char *n, t_fn f) { if (g_nt < MAXT) { g_t[g_nt].n = n; g_t[g_nt].f = f; g_nt++; } }
static struct { const char *n; t_child_fn f; } g_c[512];
static int g_nc;
void t_register_child(const char *n, t_child_fn f) { if (g_nc < 512) { g_c[g_nc].n = n; g_c[g_nc].f = f; g_nc++; } }

/* Run-to-run determinism: addresses, handles and other machine-varying hexadecimal values are never printed. Every
 * line a test emits through t_check/t_note/t_skip is scrubbed: a hex number of 6 or more digits (0x-prefixed) or a
 * bare hex run of 12 or more digits, or of 8 or more digits that contains a letter, becomes "<hex>". NTSTATUS-looking
 * (0xc0..., 0x8000000...) and 0xdead... sentinel values stay. Set W32K_RAW=1 to disable the scrub when debugging. */
static int hexd(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
static int alnum_(char c) { return hexd(c) || (c >= 'g' && c <= 'z') || (c >= 'G' && c <= 'Z') || c == '_'; }
void t_scrub(char *s) {
    static int raw = -1;
    if (raw < 0) {   /* the lookup must not disturb the caller's last-error value or LastStatusValue */
        DWORD le = GetLastError(), ls = *t_last_status_ptr(); char e[8];
        raw = GetEnvironmentVariableA("W32K_RAW", e, sizeof e) ? 1 : 0;
        SetLastError(le); *t_last_status_ptr() = ls;
    }
    if (raw) return;
    char *o = s; const char *i = s;
    while (*i) {
        if ((i == s || !alnum_(i[-1])) && hexd(*i)) {
            const char *b = i; int pfx = 0;
            if (i[0] == '0' && (i[1] == 'x' || i[1] == 'X') && hexd(i[2])) { pfx = 1; b = i + 2; }
            const char *e = b; int letter = 0, digit = 0;
            while (hexd(*e)) { if ((*e >= '0' && *e <= '9')) digit = 1; else letter = 1; e++; }
            size_t n = (size_t)(e - b);
            int hit = 0;
            if (!alnum_(*e) || !*e) {
                if (pfx) {
                    hit = n >= 6;
                    if (n == 8 && ((b[0] == 'c' || b[0] == 'C') && b[1] == '0')) hit = 0;
                    if (n == 8 && !strncmp(b, "8000000", 7)) hit = 0;
                    if (n == 8 && (b[0] == 'd' || b[0] == 'D') && (b[1] == 'e' || b[1] == 'E') && (b[2] == 'a' || b[2] == 'A') && (b[3] == 'd' || b[3] == 'D')) hit = 0;
                } else hit = digit && (n >= 12 || (n >= 8 && letter));
            }
            if (hit) { memcpy(o, "<hex>", 5); o += 5; i = e; continue; }
            while (i < e) *o++ = *i++;
            continue;
        }
        *o++ = *i++;
    }
    *o = 0;
}
void t_check(int ok, const char *file, int line, const char *expr, const char *fmt, ...) {
    if (ok) { g_pass++; return; }
    g_fail++; g_cur_fail++;
    char buf[8192]; int n = snprintf(buf, sizeof buf, "  FAIL %s:%d: %s", file, line, expr);
    if (fmt && n < (int)sizeof buf - 8) { va_list a; va_start(a, fmt); n += snprintf(buf + n, sizeof buf - n, " ("); n += vsnprintf(buf + n, sizeof buf - n, fmt, a); va_end(a); snprintf(buf + n, sizeof buf - n, ")"); }
    t_scrub(buf); printf("%s\n", buf);
}
int t_info_on(void) {
    static int on = -1;
    if (on < 0) {
        DWORD le = GetLastError(), ls = *t_last_status_ptr(); char e[8];
        on = GetEnvironmentVariableA("W32K_INFO", e, sizeof e) ? 1 : 0;
        SetLastError(le); *t_last_status_ptr() = ls;
    }
    return on;
}
void t_info(const char *fmt, ...) {
    if (!t_info_on()) return;
    char buf[8192]; int n = snprintf(buf, sizeof buf, "  INFO "); va_list a; va_start(a, fmt); vsnprintf(buf + n, sizeof buf - n, fmt, a); va_end(a); printf("%s\n", buf);
}
void t_note(const char *fmt, ...) { char buf[8192]; int n = snprintf(buf, sizeof buf, "  NOTE "); va_list a; va_start(a, fmt); vsnprintf(buf + n, sizeof buf - n, fmt, a); va_end(a); t_scrub(buf); printf("%s\n", buf); }
void t_skip(const char *fmt, ...) { char buf[8192]; int n = snprintf(buf, sizeof buf, "  SKIP "); va_list a; va_start(a, fmt); vsnprintf(buf + n, sizeof buf - n, fmt, a); va_end(a); t_scrub(buf); printf("%s\n", buf); g_cur_skip = 1; }

/* A crash inside a test is reported as a failure of that test and the run continues. */
static LONG CALLBACK veh(PEXCEPTION_POINTERS ep) {
    DWORD c = ep->ExceptionRecord->ExceptionCode;
    if (!g_in_test) return EXCEPTION_CONTINUE_SEARCH;
    if (c == 0x40010006 || c == 0x406D1388 || c == DBG_PRINTEXCEPTION_C || c == 0x4001000A) return EXCEPTION_CONTINUE_SEARCH;
    /* 0x80000100 = Wine's "call to unimplemented stub" (Wine has no win32k.sys): report it as a test failure instead of letting Wine abort the run */
    if ((c & 0xF0000000) != 0xC0000000 && c != 0x80000100) return EXCEPTION_CONTINUE_SEARCH;
    printf("  FAIL exception 0x%08lx at %p in %s\n", c, ep->ExceptionRecord->ExceptionAddress, g_cur);
    g_fail++; g_cur_fail++;
    g_in_test = 0;
    longjmp(g_jb, 1);
    return EXCEPTION_CONTINUE_SEARCH;
}

const char *t_self_path(void) { static char p[MAX_PATH * 2]; if (!p[0]) GetModuleFileNameA(NULL, p, sizeof p); return p; }
HANDLE t_spawn_child(const char *name, const char *extra, DWORD flags, HANDLE hin, HANDLE hout, HANDLE herr, BOOL inherit, const char *env, PROCESS_INFORMATION *pi_out) {
    char cmd[4096]; STARTUPINFOA si; PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si); si.cb = sizeof si;
    if (hin || hout || herr) { si.dwFlags |= STARTF_USESTDHANDLES; si.hStdInput = hin; si.hStdOutput = hout; si.hStdError = herr; }
    snprintf(cmd, sizeof cmd, "\"%s\" --child %s %s", t_self_path(), name, extra ? extra : "");
    memset(&pi, 0, sizeof pi);
    if (!CreateProcessA(NULL, cmd, NULL, NULL, inherit, flags, (LPVOID)env, NULL, &si, &pi)) return NULL;
    if (pi_out) *pi_out = pi; else CloseHandle(pi.hThread);
    return pi.hProcess;
}
DWORD t_run_child(const char *name, const char *extra, DWORD timeout_ms) {
    PROCESS_INFORMATION pi; DWORD code = 0xDEAD0000;
    HANDLE h = t_spawn_child(name, extra, 0, NULL, NULL, NULL, FALSE, NULL, &pi);
    if (!h) return 0xDEAD0000;
    if (WaitForSingleObject(h, timeout_ms) != WAIT_OBJECT_0) { TerminateProcess(h, 1); WaitForSingleObject(h, 5000); code = 0xDEAD0001; }
    else GetExitCodeProcess(h, &code);
    CloseHandle(pi.hThread); CloseHandle(h);
    return code;
}

static char g_tmp[MAX_PATH];
static void rmtree(const char *dir) {
    char pat[MAX_PATH + 4], sub[MAX_PATH + 300]; WIN32_FIND_DATAA fd;
    snprintf(pat, sizeof pat, "%s\\*", dir);
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
            snprintf(sub, sizeof sub, "%s\\%s", dir, fd.cFileName);
            SetFileAttributesA(sub, FILE_ATTRIBUTE_NORMAL);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) rmtree(sub); else DeleteFileA(sub);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryA(dir);
}
static void cleanup_tmp(void) { if (g_tmp[0]) { char d[MAX_PATH]; strcpy(d, g_tmp); d[strlen(d) - 1] = 0; rmtree(d); g_tmp[0] = 0; } }
const char *t_tmpdir(void) {
    if (!g_tmp[0]) {
        char base[MAX_PATH]; if (!GetTempPathA(sizeof base, base)) strcpy(base, ".\\");
        snprintf(g_tmp, sizeof g_tmp, "%s" T_TMP_PREFIX "_%lu\\", base, GetCurrentProcessId());
        CreateDirectoryA(g_tmp, NULL);
        atexit(cleanup_tmp);
    }
    return g_tmp;
}
void t_tmpfile(char *out, size_t cap, const char *leaf) { snprintf(out, cap, "%s%s", t_tmpdir(), leaf); }

int main(int argc, char **argv) {
    const char *filter = argc > 1 ? argv[1] : NULL;
    int ran = 0, tskip = 0, tfail = 0;
    setvbuf(stdout, NULL, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc > 2 && !strcmp(argv[1], "--child")) {
        for (int i = 0; i < g_nc; i++) if (!strcmp(g_c[i].n, argv[2])) return g_c[i].f(argc - 2, argv + 2);
        return 0xC0DE0001;
    }
    if (argc == 2 && !strcmp(argv[1], "--list")) {   /* test names, one per line; nothing is run */
        for (int i = 0; i < g_nt; i++) printf("%s\n", g_t[i].n);
        return 0;
    }
    AddVectoredExceptionHandler(1, veh);
    if (!t_module(g_mods[0])) { printf("FATAL cannot load %s\n", g_mods[0]); return 2; }
    for (int i = 0; i < g_nt; i++) {
        if (filter && !strstr(g_t[i].n, filter)) continue;
        g_cur = g_t[i].n; g_cur_fail = g_cur_skip = 0;
        printf("RUN  %s\n", g_cur);
        SetLastError(0);
        if (!setjmp(g_jb)) { g_in_test = 1; g_t[i].f(); g_in_test = 0; }
        ran++;
        if (g_cur_fail) tfail++; else if (g_cur_skip) tskip++;
        printf("%s %s\n", g_cur_fail ? "FAIL" : g_cur_skip ? "SKIP" : "OK  ", g_cur);
    }
    printf("SUMMARY tests=%d failed=%d skipped=%d checks_pass=%d checks_fail=%d\n", ran, tfail, tskip, g_pass, g_fail);
    return tfail ? 1 : 0;
}
