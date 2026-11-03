/* gap_06 shared helpers: direct win32u.dll syscall-stub invocation with all-integer arguments. */
#ifndef GAP6_H
#define GAP6_H
#include "t.h"
#include "k.h"

#define G6_STATUS_INVALID_PARAMETER   0xC000000Du
#define G6_STATUS_ACCESS_DENIED       0xC0000022u
#define G6_STATUS_INVALID_HANDLE      0xC0000008u
#define G6_STATUS_UNSUCCESSFUL        0xC0000001u
#define G6_STATUS_NOT_IMPLEMENTED     0xC0000002u

/* Every win32u Nt* stub is "mov r10,rcx; mov eax,N; syscall; ret" so a call is just the x64
 * calling convention: four register arguments then the stack. Passing more zero arguments than
 * the real signature has is harmless (the caller owns the stack), so one 14-argument prototype
 * can drive every row; the per-row real signature is documented next to the row. */
typedef ULONG_PTR (WINAPI *g6_fn14)(ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR,
                                    ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR,
                                    ULONG_PTR, ULONG_PTR);

/* Wine's win32u exports most of these names as "unimplemented function ... aborting" stubs that
 * terminate the process, so under Wine only an explicit g6_stub_any() resolves them. */
static inline g6_fn14 g6_stub_any(const char *name) { return (g6_fn14)t_proc_in("win32u.dll", name); }
static inline g6_fn14 g6_stub(const char *name) { return t_is_wine() ? NULL : g6_stub_any(name); }

/* Verifies the stub is a well formed 19041 syscall stub with the service number from
 * tools/win32u_syscalls.tsv. Quiet under Wine, whose win32u has no such stub layout. */
static inline void g6_check_stub_number(const char *name, long want) {
    FARPROC p = t_proc_in("win32u.dll", name);
    if (!p || !k_stub_valid(p)) return;
    long got = k_stub_number(p);
    t_check(got == want, __FILE__, __LINE__, name, "service number got %ld want %ld", got, want);
}

static inline ULONG_PTR g6_call0(g6_fn14 f) { return f(0,0,0,0,0,0,0,0,0,0,0,0,0,0); }
static inline ULONG_PTR g6_call(g6_fn14 f, ULONG_PTR a, ULONG_PTR b, ULONG_PTR c, ULONG_PTR d) {
    return f(a,b,c,d,0,0,0,0,0,0,0,0,0,0);
}
#endif
