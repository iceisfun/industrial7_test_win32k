#ifndef DXGAP6_H
#define DXGAP6_H
#include "t.h"
#include "k.h"
typedef ULONG_PTR (WINAPI *dxg6_fn14)(ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR,
                                      ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR, ULONG_PTR,
                                      ULONG_PTR, ULONG_PTR);
/* Wine's win32u stubs for unimplemented syscalls abort the process, so nothing resolves there. */
static inline dxg6_fn14 dxg6_stub(const char *name) { return t_is_wine() ? NULL : (dxg6_fn14)t_proc_in("win32u.dll", name); }
#endif
