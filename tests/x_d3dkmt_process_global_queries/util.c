/* x_d3dkmt_process_global_queries: helpers shared by the test files. */
#include "x.h"
#include <stdarg.h>

const char *xs_name(LONG st) {
    static char buf[4][24]; static int n;
    switch ((DWORD)st) {
    case 0x00000000: return "STATUS_SUCCESS";
    case 0x00000102: return "STATUS_TIMEOUT";
    case 0xC0000001: return "STATUS_UNSUCCESSFUL";
    case 0xC0000005: return "STATUS_ACCESS_VIOLATION";
    case 0xC0000008: return "STATUS_INVALID_HANDLE";
    case 0xC000000D: return "STATUS_INVALID_PARAMETER";
    case 0xC0000017: return "STATUS_NO_MEMORY";
    case 0xC0000022: return "STATUS_ACCESS_DENIED";
    case 0xC0000024: return "STATUS_OBJECT_TYPE_MISMATCH";
    case 0xC000007A: return "STATUS_PROCEDURE_NOT_FOUND";
    case 0xC0000023: return "STATUS_BUFFER_TOO_SMALL";
    case 0xC00000BB: return "STATUS_NOT_SUPPORTED";
    }
    char *b = buf[n++ & 3];
    snprintf(b, 24, "%04X_%04X", (unsigned)((DWORD)st >> 16), (unsigned)((DWORD)st & 0xFFFF));
    return b;
}

void x_prep(void) {
    t_module("user32.dll");
    t_module("gdi32.dll");
}

static x_fn lookup(const char *dll, const char *prefix, const char *name) {
    char b[128];
    if (t_is_wine()) return NULL;
    x_prep();
    snprintf(b, sizeof b, "%s%s", prefix, name);
    return (x_fn)t_proc_in(dll, b);
}
x_fn x_nt(const char *name)  { return lookup("win32u.dll", "", name); }
x_fn x_gdi(const char *name) { return lookup("gdi32.dll", "D3DKMT", name); }

static char *g_edge;
void *x_edge(size_t n) {
    if (!g_edge) {
        char *p = VirtualAlloc(NULL, 0x3000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        DWORD old;
        if (!p) return NULL;
        VirtualProtect(p + 0x2000, 0x1000, PAGE_NOACCESS, &old);
        g_edge = p;
    }
    if (n > 0x2000) return NULL;
    char *r = g_edge + 0x2000 - n;
    memset(r, 0, n);
    return r;
}

void *x_freed(void) {
    static void *p;
    if (!p) {
        p = VirtualAlloc(NULL, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (p) VirtualFree(p, 0, MEM_RELEASE);
    }
    return p;
}

void x_st(LONG got, LONG want, const char *file, int line, const char *fmt, ...) {
    char ctx[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ctx, sizeof ctx, fmt, ap);
    va_end(ap);
    t_check(got == want, file, line, "status", "%s: got %s want %s", ctx, xs_name(got), xs_name(want));
}


HANDLE x_open_self(DWORD access) {
    HANDLE h = NULL;
    if (!DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(), &h, access, FALSE, access ? 0 : DUPLICATE_SAME_ACCESS)) return NULL;
    return h;
}
void x_hset_init(x_hset *h) {
    memset(h, 0, sizeof *h);
    h->real = x_open_self(PROCESS_ALL_ACCESS);
    h->limited = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentProcessId());
    h->sync = OpenProcess(SYNCHRONIZE, FALSE, GetCurrentProcessId());
    h->event = CreateEventA(NULL, FALSE, FALSE, NULL);
    h->thread = OpenThread(THREAD_ALL_ACCESS, FALSE, GetCurrentThreadId());
    h->closed = x_open_self(PROCESS_ALL_ACCESS);
    if (h->closed) CloseHandle(h->closed);
    h->pseudo = GetCurrentProcess();
}
void x_hset_free(x_hset *h) {
    HANDLE *l[] = { &h->real, &h->limited, &h->sync, &h->event, &h->thread };
    for (int i = 0; i < 5; i++) if (*l[i]) { CloseHandle(*l[i]); *l[i] = NULL; }
}
LONG x_close_adapter(DWORD h) {
    x_fn f = x_nt("NtGdiDdDDICloseAdapter");
    DWORD a[2] = { h, 0 };
    return f ? f(a) : (LONG)0xC0000BAD;
}
int x_adapters_open(x_adapterinfo *out) {
    x_fn f = x_nt("NtGdiDdDDIEnumAdapters2");
    if (!f) return -1;
    x_enum2 e = { 32, 0, out };
    memset(out, 0, 32 * sizeof *out);
    if (f(&e) < 0) return -1;
    return (int)e.NumAdapters;
}
void x_adapters_close(x_adapterinfo *a, int n) {
    for (int i = 0; i < n; i++) if (a[i].hAdapter) x_close_adapter(a[i].hAdapter);
}
void x_cst(int *fails, LONG got, LONG want, const char *file, int line, const char *what) {
    if (got == want) return;
    (*fails)++;
    printf("  FAIL %s:%d: %s: got %s want %s\n", file, line, what, xs_name(got), xs_name(want));
}
