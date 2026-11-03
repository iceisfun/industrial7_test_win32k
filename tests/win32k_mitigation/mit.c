/* win32k_mitigation: child dispatcher, spawn helpers and process-state accessors. */
#include "mit.h"
#include <setjmp.h>

int mit_fails;
static struct { const char *n; mit_fn f; } g_sc[1024];
static int g_nsc;
static char g_arg[256], g_flavor[32], g_token[128];
static jmp_buf g_jb;
static volatile int g_armed;
static DWORD g_main_tid;
static volatile int g_stub_abort;
__thread jmp_buf mit_soft_jb; __thread volatile int mit_soft_on; __thread volatile unsigned long mit_soft_code;

void mit_register(const char *name, mit_fn fn) { if (g_nsc < 1024) { g_sc[g_nsc].n = name; g_sc[g_nsc].f = fn; g_nsc++; } }
const char *mit_arg(void) { return g_arg; }
const char *mit_flavor(void) { return g_flavor; }
const char *mit_token(void) { return g_token; }

/* ---- attributes ---------------------------------------------------------------------------- */
const struct mit_attr MIT_ATTR_ON8   = { 1, { MIT_CREATE_ON, 0 }, 8, 0, { 0, 0 }, 0 };
const struct mit_attr MIT_ATTR_ON16  = { 1, { MIT_CREATE_ON, 0 }, 16, 0, { 0, 0 }, 0 };
const struct mit_attr MIT_ATTR_ON4   = { 1, { MIT_CREATE_ON, 0 }, 4, 0, { 0, 0 }, 0 };
const struct mit_attr MIT_ATTR_OFF   = { 1, { MIT_CREATE_OFF, 0 }, 8, 0, { 0, 0 }, 0 };
const struct mit_attr MIT_ATTR_DEFER = { 1, { MIT_CREATE_DEFER, 0 }, 8, 0, { 0, 0 }, 0 };
const struct mit_attr MIT_ATTR_AUDIT = { 0, { 0, 0 }, 0, 1, { MIT_CREATE_ON, 0 }, 8 };

/* ---- module helpers (never touch user32/gdi32) ---------------------------------------------- */
FARPROC mit_k32(const char *name) { return t_proc_in("kernel32.dll", name); }
FARPROC mit_nt(const char *name) { return t_proc_in("ntdll.dll", name); }
FARPROC mit_w32u(const char *name) { return t_proc_in("win32u.dll", name); }

typedef BOOL (WINAPI *setmit_fn)(int, PVOID, SIZE_T);
typedef BOOL (WINAPI *getmit_fn)(HANDLE, int, PVOID, SIZE_T);
typedef LONG (WINAPI *ntset_fn)(HANDLE, ULONG, PVOID, ULONG);
typedef LONG (WINAPI *ntqry_fn)(HANDLE, ULONG, PVOID, ULONG, PULONG);

BOOL mit_set_scd(DWORD flags) {
    setmit_fn f = (setmit_fn)mit_k32("SetProcessMitigationPolicy");
    if (!f) { SetLastError(ERROR_PROC_NOT_FOUND); return FALSE; }
    return f(MIT_POLICY_SCD, &flags, sizeof flags);
}
BOOL mit_get_scd(HANDLE h, DWORD *flags) {
    getmit_fn f = (getmit_fn)mit_k32("GetProcessMitigationPolicy");
    DWORD v = 0xAAAAAAAA;
    if (!f) { SetLastError(ERROR_PROC_NOT_FOUND); return FALSE; }
    BOOL ok = f(h, MIT_POLICY_SCD, &v, sizeof v);
    if (flags) *flags = v;
    return ok;
}
LONG mit_nt_set(HANDLE h, DWORD policy, DWORD flags) {
    ntset_fn f = (ntset_fn)mit_nt("NtSetInformationProcess");
    DWORD buf[2] = { policy, flags };
    return f ? f(h, 0x34 /* ProcessMitigationPolicy */, buf, 8) : (LONG)0xC0000002;
}
LONG mit_nt_get(HANDLE h, DWORD policy, DWORD *flags) {
    ntqry_fn f = (ntqry_fn)mit_nt("NtQueryInformationProcess");
    DWORD buf[2] = { policy, 0xAAAAAAAA };
    LONG st = f ? f(h, 0x34, buf, 8, NULL) : (LONG)0xC0000002;
    if (flags) *flags = buf[1];
    return st;
}
LONG mit_nt_get_len(HANDLE h, DWORD policy, DWORD len) {
    ntqry_fn f = (ntqry_fn)mit_nt("NtQueryInformationProcess");
    DWORD buf[8] = { policy, 0xAAAAAAAA };
    return f ? f(h, 0x34, buf, len, NULL) : (LONG)0xC0000002;
}
int mit_have_policy_api(void) { return mit_k32("SetProcessMitigationPolicy") && mit_k32("GetProcessMitigationPolicy"); }

unsigned long long mit_teb64(unsigned off) { unsigned long long v = 0; k_try_read((const char *)k_teb() + off, &v, 8); return v; }
unsigned long long mit_peb64(unsigned off) { unsigned long long v = 0; k_try_read((const char *)k_peb() + off, &v, 8); return v; }
int mit_unconverted(void) { return mit_teb64(TEB_Win32ThreadInfo) == 0; }

void mit_check_pristine_state(const char *where) {
    unsigned long v = 0; unsigned char ci[0x1F0];
    MCF(mit_teb64(TEB_Win32ThreadInfo) == 0, "%s: TEB.Win32ThreadInfo converted", where);
    MCF(mit_peb64(PEB_GdiSharedHandleTable) == 0, "%s: PEB.GdiSharedHandleTable set", where);
    MCF(mit_peb64(PEB_KernelCallbackTable) == 0, "%s: PEB.KernelCallbackTable set", where);
    k_try_read_u32((const char *)k_peb() + PEB_GdiDCAttributeList, &v);
    MCF(v == 0, "%s: PEB.GdiDCAttributeList = %lu", where, v);
    int nz = 0;
    for (int i = 0; i < 60; i++) { v = 0; k_try_read_u32((const char *)k_peb() + PEB_GdiHandleBuffer + i * 4, &v); nz += v != 0; }
    MCF(nz == 0, "%s: PEB.GdiHandleBuffer has %d non-zero dwords", where, nz);
    v = 0; k_try_read_u32((const char *)k_teb() + TEB_GdiBatchCount, &v);
    MCF(v == 0, "%s: TEB.GdiBatchCount = %lu", where, v);
    memset(ci, 0xCC, sizeof ci);
    k_try_read((const char *)k_teb() + TEB_Win32ClientInfo, ci, sizeof ci);
    nz = 0; for (unsigned i = 0; i < sizeof ci; i++) nz += ci[i] != 0;
    MCF(nz == 0, "%s: TEB.Win32ClientInfo has %d non-zero bytes", where, nz);
}

/* KiSystemServiceStart: after KiConvertToGuiThread fails, the win32k service index is looked up in
 * a signed-byte table: <= 0 returns that value sign-extended (0 = silent success, -1 etc.
 * for services that report a failure value), otherwise (or when the index is beyond the table) 0xC000001C.
 * The ACCESS_DENIED PsConvertToGuiThread produced is discarded (only its zero/non-zero is used). */
int mit_status_ok(unsigned long st) { return st == ST_INVALID_SYSTEM_SERVICE || mit_status_filtered(st); }
/* The tail returns the table byte sign-extended to 32 bits when it is <= 0: 0, or a small negative number
 * (0xFFFFFF80..0xFFFFFFFF) that a whitelisted service reports as its failure value (e.g. -1). */
int mit_status_filtered(unsigned long st) { long v = (long)(int)st; return v <= 0 && v >= -128; }
unsigned long mit_call_stub(FARPROC f) { return (unsigned long)((mit_stub8)f)(0, 0, 0, 0, 0, 0, 0, 0); }

struct mit_th { DWORD (WINAPI *fn)(LPVOID); void *arg; };
static DWORD WINAPI mit_th_main(LPVOID p) { struct mit_th *t = (struct mit_th *)p; return t->fn(t->arg); }
int mit_thread(DWORD (WINAPI *fn)(LPVOID), void *arg, DWORD ms) {
    struct mit_th t = { fn, arg };
    HANDLE h = CreateThread(NULL, 0, mit_th_main, &t, 0, NULL);
    if (!h) return 0;
    int ok = WaitForSingleObject(h, ms) == WAIT_OBJECT_0;
    if (ok) CloseHandle(h);
    return ok;
}
HANDLE mit_open_self(DWORD access) {
    HANDLE h = NULL;
    DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(), &h, access, FALSE, 0);
    return h;
}

/* ---- park (child waits for the parent to look at it) ---------------------------------------- */
void mit_park(void) {
    char n[160];
    if (!g_token[0]) return;
    snprintf(n, sizeof n, "%s_rdy", g_token);
    HANDLE rdy = OpenEventA(EVENT_MODIFY_STATE, FALSE, n);
    snprintf(n, sizeof n, "%s_go", g_token);
    HANDLE go = OpenEventA(SYNCHRONIZE, FALSE, n);
    MCF(rdy && go, "park events (%p %p) err %lu", rdy, go, GetLastError());
    if (rdy) SetEvent(rdy);
    if (go) WaitForSingleObject(go, 15000);
    if (rdy) CloseHandle(rdy);
    if (go) CloseHandle(go);
}

/* ---- parent side ------------------------------------------------------------------------------ */
static LONG g_seq;
struct drain_ctx { HANDLE rd; struct mit_proc *p; };
static DWORD WINAPI drain_main(LPVOID a) {
    struct drain_ctx *c = (struct drain_ctx *)a; struct mit_proc *p = c->p; HANDLE rd = c->rd;
    free(c);
    char tmp[1024]; DWORD got;
    while (ReadFile(rd, tmp, sizeof tmp, &got, NULL) && got) {
        size_t room = 0x20000 - 1 - p->len;
        size_t n = got < room ? got : room;
        memcpy(p->buf + p->len, tmp, n);
        p->len += n;
    }
    return 0;
}

int mit_start(struct mit_proc *p, const char *selector, const struct mit_attr *a, int use_pipe) {
    char cmd[2048], nm[160];
    HANDLE wr = NULL;
    memset(p, 0, sizeof *p);
    snprintf(p->evname, sizeof p->evname, "Local\\" T_TMP_PREFIX "_mit_%lu_%ld", GetCurrentProcessId(), InterlockedIncrement(&g_seq));
    snprintf(nm, sizeof nm, "%s_rdy", p->evname); p->rdy = CreateEventA(NULL, TRUE, FALSE, nm);
    snprintf(nm, sizeof nm, "%s_go", p->evname);  p->go = CreateEventA(NULL, TRUE, FALSE, nm);
    if (use_pipe) {
        SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
        if (!CreatePipe(&p->rd, &wr, &sa, 0)) { p->err = GetLastError(); return 0; }
        SetHandleInformation(p->rd, HANDLE_FLAG_INHERIT, 0);
        p->buf = (char *)calloc(1, 0x20000);
    }
    int nattr = (use_pipe ? 1 : 0) + (a && a->have_pol) + (a && a->have_aud);
    STARTUPINFOEXA si; memset(&si, 0, sizeof si);
    si.StartupInfo.cb = nattr ? sizeof si : sizeof si.StartupInfo;
    LPPROC_THREAD_ATTRIBUTE_LIST list = NULL;
    ULONGLONG pol[2] = { 0, 0 }, aud[2] = { 0, 0 };
    HANDLE hl[1] = { wr };
    if (a) { memcpy(pol, a->pol, sizeof pol); memcpy(aud, a->aud, sizeof aud); }
    if (nattr) {
        SIZE_T sz = 0;
        InitializeProcThreadAttributeList(NULL, nattr, 0, &sz);
        list = (LPPROC_THREAD_ATTRIBUTE_LIST)calloc(1, sz);
        if (!InitializeProcThreadAttributeList(list, nattr, 0, &sz)) { p->err = GetLastError(); goto fail; }
        if (use_pipe && !UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, hl, sizeof hl, NULL, NULL)) { p->err = GetLastError(); goto fail; }
        if (a && a->have_pol && !UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY, pol, a->pol_size, NULL, NULL)) { p->err = GetLastError(); goto fail; }
        if (a && a->have_aud && !UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_MITIGATION_AUDIT_POLICY, aud, a->aud_size, NULL, NULL)) { p->err = GetLastError(); goto fail; }
        si.lpAttributeList = list;
    }
    if (use_pipe) { si.StartupInfo.dwFlags |= STARTF_USESTDHANDLES; si.StartupInfo.hStdOutput = wr; si.StartupInfo.hStdError = wr; }
    snprintf(cmd, sizeof cmd, "\"%s\" --child mit %s %s", t_self_path(), selector, p->evname);
    PROCESS_INFORMATION pi; memset(&pi, 0, sizeof pi);
    if (!CreateProcessA(NULL, cmd, NULL, NULL, use_pipe ? TRUE : FALSE, nattr ? EXTENDED_STARTUPINFO_PRESENT : 0, NULL, NULL, &si.StartupInfo, &pi)) {
        p->err = GetLastError(); goto fail;
    }
    if (list) { DeleteProcThreadAttributeList(list); free(list); }
    if (wr) CloseHandle(wr);
    p->proc = pi.hProcess; p->thread = pi.hThread; p->pid = pi.dwProcessId;
    if (use_pipe) {
        struct drain_ctx *c = (struct drain_ctx *)malloc(sizeof *c);
        c->rd = p->rd; c->p = p;
        p->drain = CreateThread(NULL, 0, drain_main, c, 0, NULL);
    }
    return 1;
fail:
    if (list) { DeleteProcThreadAttributeList(list); free(list); }
    if (wr) CloseHandle(wr);
    if (p->rd) CloseHandle(p->rd);
    if (p->rdy) CloseHandle(p->rdy);
    if (p->go) CloseHandle(p->go);
    free(p->buf);
    p->rd = p->rdy = p->go = NULL; p->buf = NULL;
    return 0;
}

int mit_wait_ready(struct mit_proc *p, DWORD ms) {
    HANDLE h[2] = { p->rdy, p->proc };
    return WaitForMultipleObjects(2, h, FALSE, ms) == WAIT_OBJECT_0;   /* process exit first = not ready */
}
void mit_release(struct mit_proc *p) { if (p->go) SetEvent(p->go); }

DWORD mit_finish(struct mit_proc *p, DWORD ms) {
    DWORD code = 0;
    if (WaitForSingleObject(p->proc, ms) != WAIT_OBJECT_0) {
        TerminateProcess(p->proc, 1); WaitForSingleObject(p->proc, 5000); code = 0xDEAD0001;
    } else GetExitCodeProcess(p->proc, &code);
    if (p->drain) { WaitForSingleObject(p->drain, 5000); CloseHandle(p->drain); }
    if (p->buf) {
        p->buf[p->len] = 0;
        for (char *s = p->buf; *s; ) {
            char *nl = strchr(s, '\n');
            if (nl) *nl = 0;
            size_t l = strlen(s); if (l && s[l - 1] == '\r') s[l - 1] = 0;
            if (*s) printf("  CHILD mit: %s\n", s);
            if (!nl) break;
            s = nl + 1;
        }
    }
    CloseHandle(p->thread); CloseHandle(p->proc);
    if (p->rd) CloseHandle(p->rd);
    if (p->rdy) CloseHandle(p->rdy);
    if (p->go) CloseHandle(p->go);
    free(p->buf);
    memset(p, 0, sizeof *p);
    return code;
}

void mit_run_attr(const char *scn, const char *arg, const char *flavor, const struct mit_attr *a) {
    char sel[512]; struct mit_proc p;
    snprintf(sel, sizeof sel, "%s:%s:%s", scn, arg ? arg : "", flavor);
    if (!mit_start(&p, sel, a, 1)) {
        if (!strcmp(flavor, "audc") || (a && a->have_aud && p.err == ERROR_INVALID_PARAMETER)) { t_note("audit creation attribute not accepted by CreateProcess (error %lu)", p.err); t_skip("no audit attribute"); return; }
        if (t_is_wine()) { t_skip("CreateProcess with attribute list failed under Wine (%lu)", p.err); return; }
        CHECKF(0, "CreateProcess(%s) failed, error %lu", sel, p.err);
        return;
    }
    mit_release(&p);   /* a parked child that this test does not drive continues at once */
    DWORD code = mit_finish(&p, 30000);
    CHECKF(code == 0, "child scenario %s exit code 0x%lx (0 = clean, else its failure count / crash code)", sel, code);
}
void mit_run(const char *scn, const char *arg, const char *flavor) {
    const struct mit_attr *a = NULL;
    if (!strcmp(flavor, "cre")) a = &MIT_ATTR_ON8;
    else if (!strcmp(flavor, "audc")) a = &MIT_ATTR_AUDIT;
    mit_run_attr(scn, arg, flavor, a);
}

/* ---- child entry ------------------------------------------------------------------------------- */
/* Only two jobs: (1) a MIT_SOFT region turns an exception into a NOTE (on whichever thread entered it);
 * (2) the emulator's unimplemented-function stub aborts the scenario. Everything else is left to the
 * normal SEH search: a first-chance exception that some frame inside the loader or user32 handles itself
 * must not be mistaken for a failure (nor longjmp'd out of while a loader lock is held). A truly
 * unhandled exception ends in mit_unhandled(). */
static LONG CALLBACK mit_veh(PEXCEPTION_POINTERS ep) {
    DWORD c = ep->ExceptionRecord->ExceptionCode;
    if (mit_soft_on && (c & 0xF0000000) == 0xC0000000) { mit_soft_code = c; longjmp(mit_soft_jb, 1); }
    if (!g_armed || GetCurrentThreadId() != g_main_tid) return EXCEPTION_CONTINUE_SEARCH;
    if (c == 0x80000100) {       /* EXCEPTION_WINE_STUB: emulator lacks this function; not a conformance result */
        printf("  SKIP emulator stub reached (Wine unimplemented function)\n");
        g_stub_abort = 1; g_armed = 0; longjmp(g_jb, 1);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
static LONG WINAPI mit_unhandled(PEXCEPTION_POINTERS ep) {
    printf("  FAIL unhandled exception 0x%08lx at %p in scenario\n", ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
    fflush(stdout);
    TerminateProcess(GetCurrentProcess(), ep->ExceptionRecord->ExceptionCode);   /* no WER dialog on a desktop VM */
    return EXCEPTION_EXECUTE_HANDLER;
}

/* Returns 1 if the scenario body should run. */
static int prelude(void) {
    const char *f = g_flavor; DWORD fl = 0xFFFFFFFF; int wine = t_is_wine();
    if (!strcmp(f, "plain")) return 1;
    if (!mit_have_policy_api()) { printf("  SKIP no SetProcessMitigationPolicy export\n"); return 0; }
    if (!strcmp(f, "set")) {
        BOOL ok = mit_set_scd(MIT_SCD_DISALLOW);
        if (!ok && wine) { printf("  SKIP Wine: policy not implemented (error %lu)\n", GetLastError()); return 0; }
        MCF(ok, "SetProcessMitigationPolicy(SystemCallDisable, Disallow=1) failed, error %lu", GetLastError());
        if (!ok) return 0;
    }
    if (!strcmp(f, "aud")) {
        BOOL ok = mit_set_scd(MIT_SCD_AUDIT);
        if (!ok && wine) { printf("  SKIP Wine: policy not implemented (error %lu)\n", GetLastError()); return 0; }
        MCF(ok, "SetProcessMitigationPolicy(SystemCallDisable, Audit=1) failed, error %lu", GetLastError());
        if (!ok) return 0;
    }
    BOOL r = mit_get_scd(GetCurrentProcess(), &fl);
    DWORD want = (!strcmp(f, "aud") || !strcmp(f, "audc")) ? MIT_SCD_AUDIT : MIT_SCD_DISALLOW;
    if ((!r || fl != want) && wine) { printf("  SKIP Wine: policy readback %lu (r=%d)\n", fl, r); return 0; }
    MCF(r, "GetProcessMitigationPolicy(SystemCallDisable) failed, error %lu", GetLastError());
    MCF(fl == want, "prelude readback flags 0x%lx want 0x%lx", fl, want);
    return r && fl == want;
}

CHILD(mit) {
    const char *sel = argc > 1 ? argv[1] : "";
    char name[128];
    snprintf(g_token, sizeof g_token, "%s", argc > 2 ? argv[2] : "");
    const char *c1 = strchr(sel, ':'), *c2 = strrchr(sel, ':');
    size_t n = c1 ? (size_t)(c1 - sel) : strlen(sel);
    if (n >= sizeof name) n = sizeof name - 1;
    memcpy(name, sel, n); name[n] = 0;
    if (c1 && c2 && c2 > c1) {
        size_t al = (size_t)(c2 - c1 - 1); if (al >= sizeof g_arg) al = sizeof g_arg - 1;
        memcpy(g_arg, c1 + 1, al); g_arg[al] = 0;
        snprintf(g_flavor, sizeof g_flavor, "%s", c2 + 1);
    } else snprintf(g_flavor, sizeof g_flavor, "%s", "plain");
    setvbuf(stdout, NULL, _IONBF, 0);
    for (int i = 0; i < g_nsc; i++) {
        if (strcmp(g_sc[i].n, name)) continue;
        g_main_tid = GetCurrentThreadId();
        SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
        SetUnhandledExceptionFilter(mit_unhandled);
        AddVectoredExceptionHandler(1, mit_veh);
        if (!setjmp(g_jb)) {
            g_armed = 1;
            if (prelude()) g_sc[i].f();
            g_armed = 0;
        } else if (!g_stub_abort) mit_fails++;
        return mit_fails > 250 ? 250 : mit_fails;
    }
    printf("  FAIL unknown scenario '%s'\n", name);
    return 0xC0DE0002;
}
