#include "r2.h"
#include <wchar.h>

int r2_fails;
#define NSCN 512
static struct { const char *n; r2_fn f; } g_scn[NSCN];
static int g_nscn;
static const char *g_token = "";
void r2_register(const char *name, r2_fn fn) { if (g_nscn < NSCN) { g_scn[g_nscn].n = name; g_scn[g_nscn].f = fn; g_nscn++; } }
const char *r2_token(void) { return g_token; }

CHILD(r2) {
    if (argc < 2) return 90;
    if (argc > 2) g_token = argv[2];
    for (int i = 0; i < g_nscn; i++) {
        if (!strcmp(g_scn[i].n, argv[1])) {
            r2_fails = 0;
            g_scn[i].f();
            return r2_fails > 100 ? 100 : r2_fails;
        }
    }
    printf("  FAIL unknown scenario %s\n", argv[1]);
    return 91;
}

void r2_run_parent(const char *name) {
    struct k_child_result r = k_child_run("r2", name, 90000);
    CHECKF(r.exit_code == 0, "scenario %s exit code 0x%lx", name, (unsigned long)r.exit_code);
}

/* ---- win32k-locked children ---- */
#ifndef PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY
#define PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY 0x00020007
#endif
static ULONGLONG g_lock_policy = 1ULL << 28;           /* WIN32K_SYSTEM_CALL_DISABLE_ALWAYS_ON */
/* Builds an attribute list carrying the lock policy; the caller frees *mem. 0 when the system refuses it. */
static int lock_attr(STARTUPINFOEXA *si, void **mem) {
    SIZE_T sz = 0;
    memset(si, 0, sizeof *si); si->StartupInfo.cb = sizeof *si; *mem = NULL;
    InitializeProcThreadAttributeList(NULL, 1, 0, &sz);
    *mem = malloc(sz);
    if (!*mem) return 0;
    si->lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)*mem;
    if (!InitializeProcThreadAttributeList(si->lpAttributeList, 1, 0, &sz)) { free(*mem); *mem = NULL; return 0; }
    if (!UpdateProcThreadAttribute(si->lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY, &g_lock_policy, sizeof g_lock_policy, NULL, NULL)) {
        DeleteProcThreadAttributeList(si->lpAttributeList); free(*mem); *mem = NULL; return 0;
    }
    return 1;
}
void r2_run_parent_locked(const char *name) {
    STARTUPINFOEXA si; void *mem;
    if (!lock_attr(&si, &mem)) { t_skip("cannot build the mitigation attribute list (%lu)", GetLastError()); return; }
    struct k_child_result r = k_child_run_ex("r2", name, 90000, &si);
    DeleteProcThreadAttributeList(si.lpAttributeList); free(mem);
    CHECKF(r.exit_code == 0, "scenario %s exit code 0x%lx", name, (unsigned long)r.exit_code);
}

/* ---- parked children ---- */
static int g_seq;
static int start_x(struct r2_proc *p, const char *scn, int locked);
int r2_start(struct r2_proc *p, const char *scn) { return start_x(p, scn, 0); }
int r2_start_locked(struct r2_proc *p, const char *scn) { return start_x(p, scn, 1); }
static int start_x(struct r2_proc *p, const char *scn, int locked) {
    char nm[160], args[200];
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    HANDLE wr = NULL;
    memset(p, 0, sizeof *p);
    snprintf(p->name, sizeof p->name, "%s", scn);
    snprintf(p->tok, sizeof p->tok, "%lu_%d", GetCurrentProcessId(), ++g_seq);
    snprintf(nm, sizeof nm, "Local\\win32kt_r2_%s_rdy", p->tok);
    p->ev_rdy = CreateEventA(NULL, TRUE, FALSE, nm);
    snprintf(nm, sizeof nm, "Local\\win32kt_r2_%s_go", p->tok);
    p->ev_go = CreateEventA(NULL, TRUE, FALSE, nm);
    if (!p->ev_rdy || !p->ev_go) return 0;
    if (!CreatePipe(&p->rd, &wr, &sa, 1 << 18)) return 0;
    SetHandleInformation(p->rd, HANDLE_FLAG_INHERIT, 0);
    snprintf(args, sizeof args, "%s %s", scn, p->tok);
    PROCESS_INFORMATION pi;
    if (locked) {
        STARTUPINFOEXA si; void *mem; char cmd[4096];
        if (!lock_attr(&si, &mem)) { CloseHandle(wr); return 0; }
        si.StartupInfo.dwFlags |= STARTF_USESTDHANDLES; si.StartupInfo.hStdOutput = wr; si.StartupInfo.hStdError = wr;
        snprintf(cmd, sizeof cmd, "\"%s\" --child r2 %s", t_self_path(), args);
        memset(&pi, 0, sizeof pi);
        p->proc = CreateProcessA(NULL, cmd, NULL, NULL, TRUE, EXTENDED_STARTUPINFO_PRESENT, NULL, NULL, &si.StartupInfo, &pi) ? pi.hProcess : NULL;
        DeleteProcThreadAttributeList(si.lpAttributeList); free(mem);
    } else
        p->proc = t_spawn_child("r2", args, 0, NULL, wr, wr, TRUE, NULL, &pi);
    CloseHandle(wr);
    if (!p->proc) return 0;
    CloseHandle(pi.hThread);
    p->pid = pi.dwProcessId;
    return 1;
}
int r2_wait_ready(struct r2_proc *p, DWORD ms) {
    HANDLE h[2] = { p->ev_rdy, p->proc };
    int ok = WaitForMultipleObjects(2, h, FALSE, ms) == WAIT_OBJECT_0;
    if (ok) ResetEvent(p->ev_rdy);   /* manual-reset: re-armed so the child can park again */
    return ok;
}
void r2_go(struct r2_proc *p) { SetEvent(p->ev_go); }
DWORD r2_finish(struct r2_proc *p, DWORD ms) {
    DWORD code = 0xDEAD0001;
    if (WaitForSingleObject(p->proc, ms) == WAIT_OBJECT_0) GetExitCodeProcess(p->proc, &code);
    else { TerminateProcess(p->proc, 1); WaitForSingleObject(p->proc, 5000); }
    char buf[4096]; DWORD avail = 0, got = 0; size_t n = 0;
    while (p->rd && PeekNamedPipe(p->rd, NULL, 0, NULL, &avail, NULL) && avail && n < sizeof buf - 1) {
        if (!ReadFile(p->rd, buf + n, (DWORD)(sizeof buf - 1 - n), &got, NULL) || !got) break;
        n += got;
    }
    buf[n] = 0;
    for (char *s = buf, *nl; *s; s = nl ? nl + 1 : s + strlen(s)) {
        nl = strchr(s, '\n'); if (nl) *nl = 0;
        if (*s) printf("  CHILD %s: %s\n", p->name, s);
        if (!nl) break;
    }
    if (p->rd) CloseHandle(p->rd);
    CloseHandle(p->proc); CloseHandle(p->ev_rdy); CloseHandle(p->ev_go);
    memset(p, 0, sizeof *p);
    return code;
}
void r2_park(void) {
    char nm[160]; HANDLE rdy, go;
    snprintf(nm, sizeof nm, "Local\\win32kt_r2_%s_rdy", g_token);
    rdy = OpenEventA(EVENT_MODIFY_STATE, FALSE, nm);
    snprintf(nm, sizeof nm, "Local\\win32kt_r2_%s_go", g_token);
    go = OpenEventA(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, nm);
    if (rdy) { SetEvent(rdy); CloseHandle(rdy); }
    if (go) { WaitForSingleObject(go, 15000); ResetEvent(go); CloseHandle(go); }
}
/* values travel through small named file mappings: Local\win32kt_r2_<tok>_<key> */
void r2_publish(const char *key, ULONGLONG v) {
    char nm[200]; snprintf(nm, sizeof nm, "Local\\win32kt_r2_%s_%s", g_token, key);
    HANDLE m = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 16, nm);
    if (!m) return;
    ULONGLONG *p = MapViewOfFile(m, FILE_MAP_WRITE, 0, 0, 16);
    if (p) { p[0] = v; p[1] = 1; UnmapViewOfFile(p); }
    /* the mapping handle is deliberately leaked: it must outlive this call until the child exits */
}
int r2_fetch(struct r2_proc *p, const char *key, ULONGLONG *v) {
    char nm[200]; snprintf(nm, sizeof nm, "Local\\win32kt_r2_%s_%s", p->tok, key);
    HANDLE m = OpenFileMappingA(FILE_MAP_READ, FALSE, nm);
    if (!m) return 0;
    ULONGLONG *q = MapViewOfFile(m, FILE_MAP_READ, 0, 0, 16);
    int ok = 0;
    if (q) { if (q[1] == 1) { *v = q[0]; ok = 1; } UnmapViewOfFile(q); }
    CloseHandle(m);
    return ok;
}

/* ---- helpers ---- */
int r2_connect(void) {
    __typeof__(&GetGuiResources) g = U(GetGuiResources);
    __typeof__(&CreateSolidBrush) cb = U(CreateSolidBrush);
    __typeof__(&DeleteObject) del = U(DeleteObject);
    if (!g || !cb || !del) return 0;
    HBRUSH b = cb(RGB(1, 2, 3));
    if (b) del(b);
    return 1;
}
DWORD r2_gur(DWORD flag) {
    __typeof__(&GetGuiResources) f = U(GetGuiResources);
    return f ? f(GetCurrentProcess(), flag) : 0;
}
FARPROC r2_w32u(const char *name) { return t_proc_in("win32u.dll", name); }

const WCHAR *r2_class(void) {
    static WCHAR name[64];
    if (!name[0]) {
        WNDCLASSEXW wc; memset(&wc, 0, sizeof wc);
        _snwprintf(name, 64, L"win32kt_r2_%lu", GetCurrentProcessId());
        wc.cbSize = sizeof wc; wc.lpfnWndProc = (WNDPROC)U(DefWindowProcW);
        wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = name;
        if (!U(RegisterClassExW)(&wc)) { name[0] = 0; if (GetLastError() == ERROR_CLASS_ALREADY_EXISTS) _snwprintf(name, 64, L"win32kt_r2_%lu", GetCurrentProcessId()); }
    }
    return name[0] ? name : NULL;
}
HWND r2_window(void) {
    const WCHAR *c = r2_class();
    if (!c) return NULL;
    return U(CreateWindowExW)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, c, L"r2", WS_POPUP, -32000, -32000, 8, 8, NULL, NULL, GetModuleHandleW(NULL), NULL);
}
int r2_vq(const void *p, MEMORY_BASIC_INFORMATION *mbi) {
    memset(mbi, 0, sizeof *mbi);
    return VirtualQuery(p, mbi, sizeof *mbi) == sizeof *mbi;
}
unsigned long r2_wait_thread(HANDLE h, DWORD ms) {
    DWORD r = WaitForSingleObject(h, ms);
    DWORD code = 0xDEAD0001;
    if (r == WAIT_OBJECT_0) GetExitCodeThread(h, &code);
    return code;
}
