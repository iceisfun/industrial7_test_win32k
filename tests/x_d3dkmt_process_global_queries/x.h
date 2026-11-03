/* x_d3dkmt_process_global_queries: shared declarations.
 *
 * Every front door is a graphics-kernel thunk reached through win32u.dll (NtGdiDdDDI<Name>, or NtDxgk<Name>
 * for the newer ones) or the same-named gdi32.dll export (D3DKMT<Name>). Each takes ONE argument, a pointer
 * to a D3DKMT_* argument block. The calls here are per-process or global queries and settings that need no
 * adapter handle; they never open an adapter, and every setting is restored.
 *
 * A thread must have user32 and gdi32 initialised before the first win32k system call, so x_prep() loads both.
 */
#ifndef X_H
#define X_H
#include "t.h"
#include "k.h"

#define XS_SUCCESS            ((LONG)0x00000000)
#define XS_TIMEOUT            ((LONG)0x00000102)
#define XS_INVALID_HANDLE     ((LONG)0xC0000008)
#define XS_INVALID_PARAMETER  ((LONG)0xC000000D)
#define XS_NO_MEMORY          ((LONG)0xC0000017)
#define XS_BUFFER_TOO_SMALL   ((LONG)0xC0000023)
#define XS_NOT_SUPPORTED      ((LONG)0xC00000BB)
#define XS_ACCESS_DENIED      ((LONG)0xC0000022)
#define XS_TYPE_MISMATCH      ((LONG)0xC0000024)
#define XS_ALREADY_SET        ((LONG)0xC0000510)   /* second call of the one-shot removal-support setter */

typedef LONG (WINAPI *x_fn)(void *);

const char *xs_name(LONG st);
void x_prep(void);
x_fn x_nt(const char *name);       /* win32u export by its full name (NtGdiDdDDIFoo, NtDxgkFoo); NULL under an emulator */
x_fn x_gdi(const char *name);      /* gdi32 D3DKMT<name> */
void *x_edge(size_t n);            /* n zeroed bytes ending exactly at the end of a committed page; the next page is PAGE_NOACCESS */
void *x_freed(void);               /* an address inside a region that was committed and then released */
void x_st(LONG got, LONG want, const char *file, int line, const char *fmt, ...);
#define X_ST(got, want, ...) x_st((got), (want), __FILE__, __LINE__, __VA_ARGS__)
#define XNEED(c) do { if (!(c)) { CHECKF(0, "setup failed: " #c); return; } } while (0)
#define XREQ(var, name) x_fn var = x_nt(name); if (!var) { t_skip("win32u!" name " not available (emulator or missing export)"); return; }
#define XREQG(var, name) x_fn var = x_gdi(name); if (!var) { t_skip("gdi32!D3DKMT" name " not available"); return; }
/* ---- argument blocks (x64 layouts of d3dkmthk.h) ---- */
typedef struct { DWORD hAdapter; LUID luid; DWORD NumOfSources; DWORD PresentMoveRegionsPreferred; } x_adapterinfo;   /* 20 */
typedef struct { DWORD NumAdapters; DWORD pad; x_adapterinfo *pAdapters; } x_enum2;                                  /* 16 */
typedef struct { ULONGLONG Filter; DWORD NumAdapters; DWORD pad; x_adapterinfo *pAdapters; } x_enum3;                /* 24 */
typedef struct { DWORD NumAdapters; x_adapterinfo Adapters[16]; } x_enum1;                                           /* 324 */

/* A handle of every kind the process-handle fields are tested with. */
typedef struct { HANDLE real, limited, sync, event, thread, closed, pseudo; } x_hset;
void x_hset_init(x_hset *h);
void x_hset_free(x_hset *h);
HANDLE x_open_self(DWORD access);

/* Opens every adapter EnumAdapters2 reports (up to 32); returns the count, -1 when the call fails. The caller closes them. */
int  x_adapters_open(x_adapterinfo *out32);
void x_adapters_close(x_adapterinfo *a, int n);
LONG x_close_adapter(DWORD h);

/* Child check: counted locally, the child returns the count as its exit code. */
void x_cst(int *fails, LONG got, LONG want, const char *file, int line, const char *what);
#define XC_ST(got, want, what) x_cst(&fails, (got), (want), __FILE__, __LINE__, (what))
#define XC(c) do { if (!(c)) { fails++; printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

#define XARR(a) (sizeof(a) / sizeof((a)[0]))
#endif
