/* x_d3dkmt_badhandle_matrix_a: shared declarations.
 *
 * Every front door here is a graphics-kernel (dxgkrnl) thunk reached through win32u.dll
 * (NtGdiDdDDI<Name>) or the same-named gdi32.dll export (D3DKMT<Name>). Each takes ONE argument, a
 * pointer to a D3DKMT_* argument block. No adapter is ever opened: the tests drive the argument and
 * handle validation, and the keyed mutex objects, which Windows creates without any adapter.
 *
 * A thread must have user32 and gdi32 initialised before the first win32k system call (win32u alone is
 * not enough: the kernel calls back into user mode to set the thread up), so x_prep() loads both.
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
#define XS_TYPE_MISMATCH      ((LONG)0xC0000024)
#define XS_CREATE_OK          ((LONG)0x7FFF0001)   /* table marker: zero struct creates an object */

typedef LONG (WINAPI *x_fn)(void *);

const char *xs_name(LONG st);                 /* symbolic name; unknown codes print as XXXX_XXXX (never scrubbed) */
void x_prep(void);                            /* load user32 + gdi32 */
x_fn x_nt(const char *name);                  /* win32u NtGdiDdDDI<name>; NULL when missing or under an emulator */
x_fn x_gdi(const char *name);                 /* gdi32 D3DKMT<name> */
void *x_edge(size_t n);                       /* n zeroed bytes ending exactly at the end of a committed page; the next page is PAGE_NOACCESS */
void *x_freed(void);                          /* an address inside a region that was committed and then released */
void x_st(LONG got, LONG want, const char *file, int line, const char *fmt, ...);
#define X_ST(got, want, ...) x_st((got), (want), __FILE__, __LINE__, __VA_ARGS__)
#define XREQ(var, name) x_fn var = x_nt(name); if (!var) { t_skip("win32u!NtGdiDdDDI" name " not available (emulator or missing export)"); return; }
#define XREQG(var, name) x_fn var = x_gdi(name); if (!var) { t_skip("gdi32!D3DKMT" name " not available"); return; }

/* ---- argument blocks (x64 layouts of d3dkmthk.h; sizes measured against the kernel's readable-size check) ---- */
typedef struct { ULONGLONG InitialValue; DWORD hSharedHandle, hKeyedMutex; } x_ckm;                                   /* 16 */
typedef struct { ULONGLONG InitialValue; DWORD hSharedHandle, hKeyedMutex; void *pPrivateRuntimeData;
                 DWORD PrivateRuntimeDataSize; DWORD Flags; } x_ckm2;                                                /* 32 */
typedef struct { DWORD hSharedHandle, hKeyedMutex; } x_okm;                                                          /* 8 */
typedef struct { DWORD hSharedHandle, hKeyedMutex; void *pPrivateRuntimeData; DWORD PrivateRuntimeDataSize; } x_okm2; /* 24 */
typedef struct { HANDLE hNtHandle; DWORD hKeyedMutex; void *pPrivateRuntimeData; DWORD PrivateRuntimeDataSize; } x_okmnt; /* 32 */
typedef struct { DWORD hKeyedMutex; ULONGLONG Key; LARGE_INTEGER *pTimeout; ULONGLONG FenceValue; } x_akm;           /* 32 */
typedef struct { DWORD hKeyedMutex; ULONGLONG Key; LARGE_INTEGER *pTimeout; ULONGLONG FenceValue;
                 void *pPrivateRuntimeData; DWORD PrivateRuntimeDataSize; } x_akm2;                                  /* 48 */
typedef struct { DWORD hKeyedMutex; ULONGLONG Key; ULONGLONG FenceValue; } x_rkm;                                    /* 24 */
typedef struct { DWORD hKeyedMutex; ULONGLONG Key; ULONGLONG FenceValue; void *pPrivateRuntimeData;
                 DWORD PrivateRuntimeDataSize; } x_rkm2;                                                              /* 40 */
typedef struct { DWORD hKeyedMutex; } x_dkm;

/* keyed mutex fixture */
LONG x_km_create(ULONGLONG init, DWORD *hshared, DWORD *hmutex);   /* CreateKeyedMutex */
LONG x_km_destroy(DWORD hmutex);
LONG x_km_acquire(DWORD hmutex, ULONGLONG key, LONGLONG timeout_100ns, int use_timeout);  /* use_timeout 0 -> pTimeout NULL (infinite) */
LONG x_km_release(DWORD hmutex, ULONGLONG key);

#define XARR(a) (sizeof(a) / sizeof((a)[0]))
#endif
