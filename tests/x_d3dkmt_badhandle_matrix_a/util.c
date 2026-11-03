/* x_d3dkmt_badhandle_matrix_a: helpers shared by the test files. */
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
x_fn x_nt(const char *name)  { return lookup("win32u.dll", "NtGdiDdDDI", name); }
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

LONG x_km_create(ULONGLONG init, DWORD *hs, DWORD *hk) {
    x_fn f = x_nt("CreateKeyedMutex");
    x_ckm c; memset(&c, 0, sizeof c); c.InitialValue = init;
    if (!f) return (LONG)0xC0000BAD;
    LONG r = f(&c);
    if (hs) *hs = c.hSharedHandle;
    if (hk) *hk = c.hKeyedMutex;
    return r;
}
LONG x_km_destroy(DWORD h) {
    x_fn f = x_nt("DestroyKeyedMutex");
    x_dkm d = { h };
    return f ? f(&d) : (LONG)0xC0000BAD;
}
LONG x_km_acquire(DWORD h, ULONGLONG key, LONGLONG to, int use_to) {
    x_fn f = x_nt("AcquireKeyedMutex");
    LARGE_INTEGER t; t.QuadPart = to;
    x_akm a; memset(&a, 0, sizeof a);
    a.hKeyedMutex = h; a.Key = key; a.pTimeout = use_to ? &t : NULL;
    return f ? f(&a) : (LONG)0xC0000BAD;
}
LONG x_km_release(DWORD h, ULONGLONG key) {
    x_fn f = x_nt("ReleaseKeyedMutex");
    x_rkm r; memset(&r, 0, sizeof r);
    r.hKeyedMutex = h; r.Key = key;
    return f ? f(&r) : (LONG)0xC0000BAD;
}
