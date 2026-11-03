/* r2_gdi_internal_eng_glue: helpers shared by the OBJ_ / Eng / UMPD guard tests.
 *
 * Every syscall here is called straight through its win32u.dll stub with up to 14 integer
 * arguments (the x64 convention puts four in registers and the rest on the caller's stack, so
 * passing more zeros than the real signature has is harmless). The real argument lists are
 * documented next to each test; they come from the win32kfull/win32kbase analysis. */
#ifndef R2E_H
#define R2E_H
#include "t.h"
#include "k.h"

typedef ULONG_PTR (WINAPI *r2e_fn)(ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR,
                                   ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR,
                                   ULONG_PTR, ULONG_PTR);

#define R2E_STATUS_INVALID_PARAMETER 0xC000000Du
#define R2E_STATUS_ACCESS_DENIED     0xC0000022u
#define R2E_STATUS_NOT_IMPLEMENTED   0xC0000002u
#define R2E_STATUS_BUFFER_TOO_SMALL  0xC0000023u

/* Wine's win32u exports most of these names as "unimplemented, aborting" stubs, so under an
 * emulator the stub is not resolved at all and the test skips through REQUIRE(). */
static inline r2e_fn r2e_stub(const char *name) {
    if (t_is_wine()) return NULL;
    t_module("user32.dll");     /* a process without user32 dies on its first win32k syscall (see r2e_stub_callback_table) */
    return (r2e_fn)t_proc_in("win32u.dll", name);
}
/* Checks the stub prologue and the service number from tools/win32u_syscalls.tsv. */
static inline void r2e_check_number(const char *name, long want) {
    FARPROC p = t_proc_in("win32u.dll", name);
    if (!p || !k_stub_valid(p)) return;
    long got = k_stub_number(p);
    t_check(got == want, __FILE__, __LINE__, name, "service number got %ld want %ld", got, want);
}
static inline ULONG_PTR r2e_call(r2e_fn f, ULONG_PTR a, ULONG_PTR b, ULONG_PTR c, ULONG_PTR d, ULONG_PTR e) {
    return f(a, b, c, d, e, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}
/* On Windows 10 the first win32k syscall of a thread calls back into user mode through PEB.KernelCallbackTable. A process
 * without user32 has no table (the exes do not link user32), so that first syscall kills it: access violation on the calling
 * thread, then STATUS_FATAL_USER_CALLBACK_EXCEPTION (0xC000041D). The pristine-process scenarios install a table of do-nothing
 * handlers, which lets the conversion complete with exactly the state a user32-less process would be left in. */
static LONG NTAPI r2e_cb_nothing(void *buf, ULONG len) { (void)buf; (void)len; return 0; }
static inline void r2e_stub_callback_table(void) {
    static void *tbl[0x100];
    if (GetModuleHandleA("user32.dll") != NULL) return;
    for (int i = 0; i < 0x100; i++) tbl[i] = (void *)r2e_cb_nothing;
    *(void *volatile *)((char *)k_peb() + PEB_KernelCallbackTable) = tbl;
}
#define R2E_LO(v) ((unsigned long)(ULONG_PTR)(v))

/* GDI object count of this process, the number GetGuiResources(GR_GDIOBJECTS) reports. */
static inline unsigned long r2e_gdi_count(void) {
    return U(GetGuiResources) ? (unsigned long)U(GetGuiResources)(GetCurrentProcess(), 0) : 0;
}
static inline unsigned long r2e_user_count(void) {
    return U(GetGuiResources) ? (unsigned long)U(GetGuiResources)(GetCurrentProcess(), 1) : 0;
}
/* DC_ATTR page of a DC (GDICELL.pUser); NULL when the cell cannot be decoded. */
static inline unsigned char *r2e_dcattr(HDC dc) {
    struct k_gdicell c;
    if (!k_gdi_cell((void *)dc, &c) || !c.field10) return NULL;
    return (unsigned char *)c.field10;
}
#define R2E_DCA_SNAP 0x168     /* the part of DC_ATTR every gdi32full accessor reads */
static inline int r2e_snap(HDC dc, unsigned char *out) {
    unsigned char *a = r2e_dcattr(dc);
    return a && k_try_read(a, out, R2E_DCA_SNAP);
}
#endif
