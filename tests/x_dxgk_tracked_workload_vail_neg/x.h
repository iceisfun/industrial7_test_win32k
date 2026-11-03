/* x_dxgk_tracked_workload_vail_neg: shared declarations.
 *
 * The eighteen NtDxgk* thunks of the tracked-workload, display-manager, hardware-queue present, pinning and
 * "Vail" families. All of them are reached through win32u.dll (NtDxgk<Name>) or the same-named gdi32.dll export
 * (D3DKMT<Name>). Their argument layout is not published; this area only feeds them argument shapes that a
 * caller without the right to use them cannot get past, and asserts the status each one returns.
 *
 * A thread must have user32 and gdi32 initialised before the first win32k system call, so x_prep() loads both.
 */
#ifndef X_H
#define X_H
#include "t.h"
#include "k.h"

#define XS_SUCCESS            ((LONG)0x00000000)
#define XS_NOT_IMPLEMENTED    ((LONG)0xC0000002)
#define XS_INVALID_HANDLE     ((LONG)0xC0000008)
#define XS_INVALID_PARAMETER  ((LONG)0xC000000D)
#define XS_ACCESS_DENIED      ((LONG)0xC0000022)
#define XS_ACCESS_VIOLATION   ((LONG)0xC0000005)
#define XS_TYPE_MISMATCH      ((LONG)0xC0000024)
#define XS_NOT_SUPPORTED      ((LONG)0xC00000BB)
#define XS_GFX_C01E0011       ((LONG)0xC01E0011)   /* what RegisterVailProcess answers to a readable block */

typedef LONG (WINAPI *x_fn)(void *);
typedef LONG (WINAPI *x_fn8)(ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR);

const char *xs_name(LONG st);
void x_prep(void);
x_fn8 x_nt8(const char *name);          /* win32u export by full name; NULL under an emulator */
x_fn8 x_gdi8(const char *name);         /* gdi32 export by full name (D3DKMT<Name>) */
void *x_edge(size_t n);                 /* n zeroed bytes ending exactly at the end of a committed page; the next page is PAGE_NOACCESS */
void *x_blk(int slot, size_t n, int fill);  /* n bytes of `fill` ending at the end of a committed page, the next page inaccessible; slot 0..15 each own pages */
void *x_freed(void);                    /* an address inside a region that was committed and then released, free right now */
void x_st(LONG got, LONG want, const char *file, int line, const char *fmt, ...);
#define X_ST(got, want, ...) x_st((got), (want), __FILE__, __LINE__, __VA_ARGS__)
#define XARR(a) (sizeof(a) / sizeof((a)[0]))
HANDLE x_open_self(DWORD access);

/* ---- the table of thunks ------------------------------------------------------------------------------ */
enum { X_CREATE, X_UPDATE, X_DESTROY, X_RESET, X_GETIDX, X_GETSTAT, X_DISPMGR, X_DPORT, X_DUP, X_ODUPL, X_BLT, X_PRESENT,
       X_PIN, X_UNPIN, X_REGVAIL, X_VCONN, X_VDISC, X_PROMOTE, X_COUNT };
struct xsc { const char *nt; const char *gdi; long num; };
extern const struct xsc xsc_tab[X_COUNT];

/* Shapes of argument vectors the model below predicts a status for. */
enum xshape {
    XS_ZERO,         /* every argument 0 */
    XS_BLOCK0,       /* arg0 = readable zero block (0x100 bytes), the rest 0 */
    XS_BLOCKN,       /* arg1..7 = readable zero blocks, arg0 = 0 (or: a single later argument = block) */
    XS_BLOCKS_ALL,   /* every argument = its own readable zero block */
    XS_WILD0,        /* arg0 = unreadable pointer, the rest 0 */
    XS_WILDN,        /* arg0 = 0, one later argument = unreadable pointer */
    XS_WILD_ALL,     /* every argument = unreadable value */
    XS_FILL_FF,      /* every argument = block filled with 0xFF */
    XS_FILL_41,      /* every argument = block filled with 0x41 */
    XS_FILL_01       /* every argument = block filled with 0x01 */
};
/* The status the thunk answers to the shape. Returns 0 and sets *skip_reason when the answer depends on the caller's
 * rights and the process is neither clearly privileged nor clearly unprivileged. */
int x_model(int sc, enum xshape sh, LONG *want, const char **skip_reason);
/* Builds an argument vector of a shape (fresh blocks each time). */
#define X_NWILD 8
void x_args(enum xshape sh, int pos, int variant, ULONG_PTR a[8]);   /* pos: the argument index for the BLOCKN/WILDN shapes; variant: which unreadable value (0..X_NWILD-1) */
/* 1 = administrator-class token at high integrity, 0 = medium-or-lower integrity token, -1 = mixed. */
int x_privileged(void);
/* Calls through win32u (via == 0) or gdi32 (via == 1). */
LONG x_call(int sc, int via, const ULONG_PTR a[8]);
x_fn8 x_resolve(int sc, int via);
#define XRESOLVE(var, sc, via) x_fn8 var = x_resolve((sc), (via)); \
    if (!var) { t_skip("%s not available (emulator or missing export)", (via) ? xsc_tab[sc].gdi : xsc_tab[sc].nt); return; }
#define XMODEL(sc, sh, want) LONG want; do { const char *r_; if (!x_model((sc), (sh), &want, &r_)) { t_skip("%s", r_); return; } } while (0)
#endif
