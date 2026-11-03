/* x_dxgk_tracked_workload_vail_neg: helpers shared by the test files. */
#include "x.h"
#include <stdarg.h>

const char *xs_name(LONG st) {
    static char buf[4][24]; static int n;
    switch ((DWORD)st) {
    case 0x00000000: return "STATUS_SUCCESS";
    case 0xC0000001: return "STATUS_UNSUCCESSFUL";
    case 0xC0000002: return "STATUS_NOT_IMPLEMENTED";
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

static x_fn8 lookup(const char *dll, const char *name) {
    if (t_is_wine()) return NULL;
    x_prep();
    return (x_fn8)t_proc_in(dll, name);
}
x_fn8 x_nt8(const char *name)  { return lookup("win32u.dll", name); }
x_fn8 x_gdi8(const char *name) { return lookup("gdi32.dll", name); }

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
    /* A fresh region every time, released at once, so the address is unmapped at the moment of the call. */
    void *p = VirtualAlloc(NULL, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (p) VirtualFree(p, 0, MEM_RELEASE);
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
