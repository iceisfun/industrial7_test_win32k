/* x_d3dkmt_badhandle_matrix_b: shared declarations.
 *
 * Second batch of graphics-kernel (dxgkrnl) thunks reached through win32u.dll (NtGdiDdDDI<Name>) or the
 * same-named gdi32.dll export (D3DKMT<Name>): contexts, hardware queues, GPU virtual addresses, offer and
 * reclaim, Lock2/Unlock2, presents, shared resources and protected sessions. Each takes ONE argument, a
 * pointer to a D3DKMT_* argument block. No adapter, device, context or allocation is ever opened or created:
 * every handle the tests can supply is bogus, and the tests drive the argument and handle validation.
 *
 * A thread must have user32 and gdi32 initialised before the first win32k system call (win32u alone is
 * not enough: the kernel calls back into user mode to set the thread up), so x_prep() loads both.
 */
#ifndef X_H
#define X_H
#include "t.h"
#include "k.h"

#define XS_SUCCESS            ((LONG)0x00000000)
#define XS_INVALID_HANDLE     ((LONG)0xC0000008)
#define XS_INVALID_PARAMETER  ((LONG)0xC000000D)
#define XS_NO_MEMORY          ((LONG)0xC0000017)
#define XS_ACCESS_DENIED      ((LONG)0xC0000022)
#define XS_TYPE_MISMATCH      ((LONG)0xC0000024)
#define XS_NOT_IMPLEMENTED    ((LONG)0xC0000002)

typedef LONG (WINAPI *x_fn)(void *);

const char *xs_name(LONG st);                 /* symbolic name; unknown codes print as XXXX_XXXX */
void x_prep(void);                            /* load user32 + gdi32 */
x_fn x_nt(const char *name);                  /* win32u NtGdiDdDDI<name>; NULL when missing or under an emulator */
x_fn x_gdi(const char *name);                 /* gdi32 D3DKMT<name> */
void *x_edge(size_t n);                       /* n zeroed bytes ending exactly at the end of a committed page; the next page is PAGE_NOACCESS */
void *x_freed(void);                          /* an address inside a region that was committed and then released */
void x_st(LONG got, LONG want, const char *file, int line, const char *fmt, ...);
#define X_ST(got, want, ...) x_st((got), (want), __FILE__, __LINE__, __VA_ARGS__)
#define XARR(a) (sizeof(a) / sizeof((a)[0]))
#endif
