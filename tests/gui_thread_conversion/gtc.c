/* gui_thread_conversion: child dispatcher and TEB/PEB accessors. */
#include "gtc.h"
#include <setjmp.h>

int gtc_fails;
static struct { const char *n; gtc_fn f; } g_sc[1024];
static int g_nsc;
static char g_arg[256];
static jmp_buf g_jb;
static volatile int g_armed;
static DWORD g_main_tid;
static volatile int g_stub_abort;

void gtc_register(const char *name, gtc_fn fn) { if (g_nsc < 1024) { g_sc[g_nsc].n = name; g_sc[g_nsc].f = fn; g_nsc++; } }
const char *gtc_arg(void) { return g_arg; }

void gtc_parent_run(const char *selector, DWORD timeout_ms) {
    struct k_child_result r = k_child_run("gtc", selector, timeout_ms);
    CHECKF(r.exit_code == 0, "child scenario %s exit code 0x%lx (0 = clean, else its failure count / crash code)", selector, (unsigned long)r.exit_code);
}

static LONG CALLBACK gtc_veh(PEXCEPTION_POINTERS ep) {
    DWORD c = ep->ExceptionRecord->ExceptionCode;
    if (!g_armed || GetCurrentThreadId() != g_main_tid) return EXCEPTION_CONTINUE_SEARCH;
    if (c == 0x80000100) {       /* EXCEPTION_WINE_STUB: emulator lacks this syscall; not a conformance result */
        printf("  SKIP emulator stub reached (Wine unimplemented function)\n");
        g_stub_abort = 1; g_armed = 0; longjmp(g_jb, 1);
    }
    if ((c & 0xF0000000) != 0xC0000000) return EXCEPTION_CONTINUE_SEARCH;
    printf("  FAIL exception 0x%08lx at %p in scenario\n", c, ep->ExceptionRecord->ExceptionAddress);
    g_armed = 0;
    longjmp(g_jb, 1);
    return EXCEPTION_CONTINUE_SEARCH;
}

CHILD(gtc) {
    (void)argc;
    const char *sel = argc > 1 ? argv[1] : "";
    char name[128];
    const char *colon = strchr(sel, ':');
    size_t n = colon ? (size_t)(colon - sel) : strlen(sel);
    if (n >= sizeof name) n = sizeof name - 1;
    memcpy(name, sel, n); name[n] = 0;
    snprintf(g_arg, sizeof g_arg, "%s", colon ? colon + 1 : "");
    setvbuf(stdout, NULL, _IONBF, 0);
    for (int i = 0; i < g_nsc; i++) {
        if (strcmp(g_sc[i].n, name)) continue;
        g_main_tid = GetCurrentThreadId();
        AddVectoredExceptionHandler(1, gtc_veh);
        if (!setjmp(g_jb)) { g_armed = 1; g_sc[i].f(); g_armed = 0; }
        else if (!g_stub_abort) gtc_fails++;
        return gtc_fails > 250 ? 250 : gtc_fails;
    }
    printf("  FAIL unknown scenario '%s'\n", name);
    return 0xC0DE0002;
}

/* ---- user32-less callback table ------------------------------------------------------------- */
static void *g_stub_cb[0x100];
static LONG NTAPI gtc_cb_nothing(void *buf, ULONG len) { (void)buf; (void)len; return 0; }   /* KiUserCallbackDispatcher returns this status to the kernel */
FARPROC gtc_win32u_proc(const char *name) {
    FARPROC p = t_proc_in("win32u.dll", name);
    if (GetModuleHandleA("user32.dll") == NULL && gtc_peb_u64(PEB_KernelCallbackTable) == 0) {
        for (int i = 0; i < 0x100; i++) g_stub_cb[i] = (void *)gtc_cb_nothing;
        *(void *volatile *)((char *)k_peb() + PEB_KernelCallbackTable) = g_stub_cb;
    }
    return p;
}
unsigned long long gtc_stub_table(void) {
    return gtc_peb_u64(PEB_KernelCallbackTable) == (unsigned long long)(ULONG_PTR)g_stub_cb ? (unsigned long long)(ULONG_PTR)g_stub_cb : 0;
}

/* ---- accessors ---------------------------------------------------------------------------- */
unsigned long long gtc_teb_u64(unsigned off) {
    unsigned long long v = 0;
    k_try_read((const char *)k_teb() + off, &v, 8);
    return v;
}
unsigned long long gtc_peb_u64(unsigned off) {
    unsigned long long v = 0;
    k_try_read((const char *)k_peb() + off, &v, 8);
    return v;
}
unsigned long gtc_peb_u32(unsigned off) {
    unsigned long v = 0;
    k_try_read((const char *)k_peb() + off, &v, 4);
    return v;
}
unsigned long long gtc_ci_u64(unsigned off) { return gtc_teb_u64(TEB_Win32ClientInfo + off); }
unsigned long gtc_ci_u32(unsigned off) {
    unsigned long v = 0;
    k_try_read((const char *)k_teb() + TEB_Win32ClientInfo + off, &v, 4);
    return v;
}
unsigned short gtc_ci_u16(unsigned off) {
    unsigned short v = 0;
    k_try_read((const char *)k_teb() + TEB_Win32ClientInfo + off, &v, 2);
    return v;
}
int gtc_ci_all_zero(void) {
    for (int i = 0; i < CI_SIZE_QWORDS; i++) if (gtc_ci_u64(i * 8)) return 0;
    return 1;
}
int gtc_teb_converted(void) { return gtc_teb_u64(TEB_Win32ThreadInfo) != 0; }

int gtc_in_module(const char *mod, unsigned long long p) {
    HMODULE m = GetModuleHandleA(mod);
    if (!m) return 0;
    unsigned long lfanew = 0, size = 0;
    if (!k_try_read((char *)m + 0x3c, &lfanew, 4)) return 0;
    if (!k_try_read((char *)m + lfanew + 24 + 56, &size, 4)) return 0;   /* OptionalHeader.SizeOfImage */
    return p >= (unsigned long long)(ULONG_PTR)m && p < (unsigned long long)(ULONG_PTR)m + size;
}
int gtc_is_kernel_range(unsigned long long p) { return p >= 0xFFFF800000000000ULL; }

DWORD gtc_query(const void *p, DWORD *type, SIZE_T *region) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof mbi)) return 0;
    if (type) *type = mbi.Type;
    if (region) *region = mbi.RegionSize;
    return mbi.Protect;
}

struct gtc_th { gtc_thread_fn fn; void *arg; };
static DWORD WINAPI gtc_th_main(LPVOID p) { struct gtc_th *t = (struct gtc_th *)p; t->fn(t->arg); return 0; }
int gtc_thread(gtc_thread_fn fn, void *arg) {
    struct gtc_th t = { fn, arg };
    HANDLE h = CreateThread(NULL, 0, gtc_th_main, &t, 0, NULL);
    if (!h) return 0;
    int ok = WaitForSingleObject(h, 10000) == WAIT_OBJECT_0;
    CloseHandle(h);
    return ok;
}

HANDLE gtc_spawn(const char *selector) {
    char cmd[2048]; STARTUPINFOA si; PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si); si.cb = sizeof si;
    memset(&pi, 0, sizeof pi);
    snprintf(cmd, sizeof cmd, "\"%s\" --child gtc %s", t_self_path(), selector);
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) return NULL;
    CloseHandle(pi.hThread);
    return pi.hProcess;
}

void gtc_parent_run_gui(const char *selector, DWORD timeout_ms) {
    char copy[MAX_PATH * 2], cmd[4096];
    t_tmpfile(copy, sizeof copy, "gtc_guisub.exe");
    SetFileAttributesA(copy, FILE_ATTRIBUTE_NORMAL);   /* a copy left by an earlier scenario may be read-only */
    if (!CopyFileA(t_self_path(), copy, FALSE)) { t_skip("cannot copy exe (err %lu)", GetLastError()); return; }
    SetFileAttributesA(copy, FILE_ATTRIBUTE_NORMAL);   /* CopyFile keeps the source's read-only attribute; the subsystem patch needs write access */
    /* flip IMAGE_OPTIONAL_HEADER.Subsystem from CONSOLE(3) to WINDOWS_GUI(2) */
    FILE *f = fopen(copy, "r+b");
    if (!f) { t_skip("cannot reopen exe copy"); return; }
    unsigned long lfanew = 0; unsigned short sub = 0, want = 2;
    fseek(f, 0x3c, SEEK_SET); fread(&lfanew, 4, 1, f);
    fseek(f, (long)(lfanew + 24 + 68), SEEK_SET); fread(&sub, 2, 1, f);
    fseek(f, (long)(lfanew + 24 + 68), SEEK_SET); fwrite(&want, 2, 1, f);
    fclose(f);
    CHECKF(sub == 3, "source exe subsystem is %u, expected console (3)", sub);

    HANDLE rd, wr; SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    if (!CreatePipe(&rd, &wr, &sa, 1 << 20)) { t_skip("CreatePipe failed"); return; }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOA si; PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si); si.cb = sizeof si; si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr; si.hStdError = wr;
    memset(&pi, 0, sizeof pi);
    snprintf(cmd, sizeof cmd, "\"%s\" --child gtc %s", copy, selector);
    BOOL ok = CreateProcessA(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
    CloseHandle(wr);
    if (!ok) { CloseHandle(rd); t_skip("cannot start GUI-subsystem copy (err %lu)", GetLastError()); return; }
    DWORD code = 0xDEAD0001;
    if (WaitForSingleObject(pi.hProcess, timeout_ms) == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &code);
    else { TerminateProcess(pi.hProcess, 1); WaitForSingleObject(pi.hProcess, 5000); }
    char buf[8192]; DWORD got = 0, avail = 0;
    if (PeekNamedPipe(rd, NULL, 0, NULL, &avail, NULL) && avail) {
        if (!ReadFile(rd, buf, sizeof buf - 1, &got, NULL)) got = 0;
        buf[got] = 0;
        for (char *p = strtok(buf, "\n"); p; p = strtok(NULL, "\n")) printf("  CHILD(gui) %s: %s\n", selector, p);
    }
    CloseHandle(rd); CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    CHECKF(code == 0, "GUI-subsystem child %s exit code 0x%lx", selector, (unsigned long)code);
}

void gtc_assert_pristine(void) {
    GCF(GetModuleHandleA("user32.dll") == NULL, "user32.dll already loaded: not a pristine process");
    GCF(GetModuleHandleA("gdi32.dll") == NULL, "gdi32.dll already loaded");
    GCF(GetModuleHandleA("gdi32full.dll") == NULL, "gdi32full.dll already loaded");
    GEQ(gtc_teb_u64(TEB_Win32ThreadInfo), 0);
}
