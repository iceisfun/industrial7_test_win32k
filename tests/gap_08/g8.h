/* Shared helpers for the gap_08 area (thin-coverage syscalls).
 *
 * GDI handle layout used here (verified in gdi32full RoundRect/ResizePalette: "hdc & 0x7f0000 == 0x10000" is a
 * display DC, "hpal & 0x7f0000 == 0x80000" is a palette): bits 16..22 of an HGDIOBJ carry the object type
 * (1 DC, 4 region, 8 palette), bits 24..31 are the uniqueness byte mirrored in GDICELL.+0x0d. */
#ifndef G8_H
#define G8_H
#include "t.h"
#include "k.h"

#define GH_TYPE(h) ((unsigned)(((ULONG_PTR)(h) >> 16) & 0x7f))
#define GT_DC 1
#define GT_RGN 4
#define GT_PAL 8

/* The cell's wType (cell+0x0e) read through the shared handle table; 0 when unreadable. */
static inline unsigned g8_cell_type(void *h) {
    void *tab = NULL;
    if (!k_try_read_ptr((char *)k_peb() + PEB_GdiSharedHandleTable, &tab) || !tab) return 0;
    unsigned short t = 0;
    if (!k_try_read_u16((char *)tab + (ULONG_PTR)((ULONG_PTR)h & 0xffff) * GDI_CELL_SIZE + 0x0e, &t)) return 0;
    return t;
}

/* Out-of-range / boundary coordinates of the GDI "(v & 0xf8000000) + 0x8000000 & 0xf7ffffff" test. */
#define G8_LO  (-0x08000000)
#define G8_HI  (0x07ffffff)

/* Single-evaluation equality check: the harness EXPECT_EQ evaluates both operands twice (once to compare, once to print),
 * which re-runs a syscall wrapper passed as the operand. */
#define G8EQ(a, b) do { ULONG_PTR a_ = (ULONG_PTR)(a); ULONG_PTR b_ = (ULONG_PTR)(b); \
    t_check(a_ == b_, __FILE__, __LINE__, #a " == " #b, "got 0x%llx want 0x%llx", (unsigned long long)a_, (unsigned long long)b_); } while (0)

/* Tests that call win32u stubs Wine leaves unimplemented (Wine aborts the process on them). */
#define G8_NOT_WINE() do { if (t_is_wine()) { t_skip("win32u stub unimplemented under Wine"); return; } } while (0)
/* A window that cannot be created is a failure on Windows but only an environment limit on a display-less emulator. */
#define WCHK(w) do { if (!(w)) { if (t_is_wine()) t_skip("no window support in this environment"); else CHECK((w) != NULL); return; } } while (0)
#endif
