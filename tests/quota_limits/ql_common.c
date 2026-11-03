#include "ql.h"
#include <wchar.h>

int ql_fails;
struct ql_fill ql_f;

#define QL_NSCN 512
static struct { const char *n; ql_fn f; } g_scn[QL_NSCN];
static int g_nscn;
void ql_register(const char *name, ql_fn fn) { if (g_nscn < QL_NSCN) { g_scn[g_nscn].n = name; g_scn[g_nscn].f = fn; g_nscn++; } }

/* The child body: runs exactly one scenario and returns the number of failed checks. */
CHILD(ql_scn) {
    if (argc < 2) return 90;
    for (int i = 0; i < g_nscn; i++) {
        if (!strcmp(g_scn[i].n, argv[1])) {
            ql_fails = 0;
            g_scn[i].f();
            return ql_fails > 100 ? 100 : ql_fails;
        }
    }
    printf("  FAIL unknown scenario %s\n", argv[1]);
    return 91;
}

void ql_run_parent(const char *name) {
    /* Slow but bounded: the child creates at most QL_MAX_OBJ objects and every wait is capped. */
    struct k_child_result r = k_child_run("ql_scn", name, 120000);
    CHECKF(r.exit_code == 0, "scenario %s exit code 0x%lx", name, (unsigned long)r.exit_code);
}

unsigned ql_reg_dword(const char *name, int *found) {
    HKEY k; DWORD v = 0, sz = sizeof v, type = 0;
    if (found) *found = 0;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Windows", 0, KEY_READ, &k)) return 0;
    LONG r = RegQueryValueExA(k, name, NULL, &type, (BYTE *)&v, &sz);
    RegCloseKey(k);
    if (r || type != REG_DWORD) return 0;
    if (found) *found = 1;
    return v;
}

struct ql_quota ql_gdi_quota(void) {
    struct ql_quota q = { 10000, 1, 0, 0 };
    int f; unsigned v = ql_reg_dword("GDIProcessHandleQuota", &f);
    q.in_reg = f; q.reg = v;
    /* bLoadProcessHandleQuota: a value that is negative as int or above gMaxGdiHandleCount (65536) falls back to 10000 */
    if (f && (int)v >= 0 && v <= 65536) q.q = v;
    q.std = q.q == 10000;
    return q;
}
struct ql_quota ql_user_quota(void) {
    struct ql_quota q = { 10000, 1, 0, 0 };
    int f; unsigned v = ql_reg_dword("USERProcessHandleQuota", &f);
    q.in_reg = f; q.reg = v;
    if (f && v >= 200 && v <= 18000) q.q = v;
    q.std = (!f || v == 10000);
    return q;
}
struct ql_quota ql_post_limit(void) {
    struct ql_quota q = { 10000, 1, 0, 0 };
    int f; unsigned v = ql_reg_dword("USERPostMessageLimit", &f);
    q.in_reg = f; q.reg = v;
    if (f && v >= 200 && v <= 100000) q.q = v;
    q.std = (!f || v == 10000);
    return q;
}

DWORD ql_gur(DWORD flag) {
    __typeof__(&GetGuiResources) f = U(GetGuiResources);
    return f ? f(GetCurrentProcess(), flag) : 0;
}

void ql_drain_take(struct ql_drain *d) {
    memset(d, 0, sizeof *d);
    for (int i = 0; i < QL_DRAIN_N; i++) d->obj[d->n++] = U(CreateSolidBrush)(RGB(200 + i, 7, 9));
    for (int i = 0; i < QL_DRAIN_N; i++) d->obj[d->n++] = U(CreateRectRgn)(0, 0, 3 + i, 3);
}
void ql_drain_give(struct ql_drain *d) {
    for (int i = 0; i < d->n; i++) if (d->obj[i]) U(DeleteObject)(d->obj[i]);
    d->n = 0;
}
/* once per process: scenario children start from here, the objects are held until the process ends */
static void ql_drain_caches(void) {
    static int done;
    static struct ql_drain d;
    if (done) return;
    done = 1;
    ql_drain_take(&d);
}

int ql_connect(void) {
    __typeof__(&GetGuiResources) g = U(GetGuiResources);
    __typeof__(&CreateSolidBrush) cb = U(CreateSolidBrush);
    __typeof__(&DeleteObject) del = U(DeleteObject);
    if (!g || !cb || !del) return 0;
    HBRUSH b = cb(RGB(1, 2, 3));      /* first win32k syscall converts the thread, creates GDI table access */
    if (b) del(b);
    ql_drain_caches();
    if (t_is_wine() && !getenv("QL_FORCE")) {
        /* Wine's GetGuiResources is a stub that always returns 0: every count based expectation would be noise. */
        DWORD a = g(GetCurrentProcess(), 0);
        HBRUSH b2 = cb(RGB(4, 5, 6));
        DWORD c = g(GetCurrentProcess(), 0);
        if (b2) del(b2);
        if (c != a + 1) { t_note("Wine: GetGuiResources does not track GDI objects"); return 0; }
    }
    return 1;
}

const WCHAR *ql_class(void) {
    static WCHAR name[64];
    if (!name[0]) {
        WNDCLASSEXW wc; memset(&wc, 0, sizeof wc);
        _snwprintf(name, 64, L"win32kt_ql_%lu", GetCurrentProcessId());
        wc.cbSize = sizeof wc; wc.lpfnWndProc = (WNDPROC)U(DefWindowProcW);
        wc.hInstance = GetModuleHandleW(NULL); wc.lpszClassName = name;
        if (!U(RegisterClassExW)(&wc)) name[0] = 0;
    }
    return name[0] ? name : NULL;
}

HWND ql_msg_window(void) {
    const WCHAR *c = ql_class();
    if (!c) return NULL;
    return U(CreateWindowExW)(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, c, L"ql", WS_POPUP, 0, 0, 1, 1, HWND_MESSAGE, NULL, GetModuleHandleW(NULL), NULL);
}
