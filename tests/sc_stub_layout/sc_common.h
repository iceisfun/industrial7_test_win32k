/* sc_stub_layout: shared helpers - win32u.dll PE walking, expected stub synthesis, build gating. */
#ifndef SC_COMMON_H
#define SC_COMMON_H
#include "t.h"
#include "k.h"

#define SC_COUNT    1316   /* win32u syscall stubs on 19041 (tools/win32u_syscalls.tsv) */
#define SC_EXPORTS  1317   /* + the data export gDispatchTableValues */
#define SC_FIRST    4096   /* 0x1000: table selector 1 (win32k shadow table), index 0 */
#define SC_STRIDE   0x20   /* distance between consecutive stubs in .text */
#define SC_STUB_LEN 0x18   /* bytes of one stub: 3+5+8+2+2+1+2+1 */

struct sc_entry { const char *name; unsigned num, rva, ord; };
extern const struct sc_entry sc_tab[SC_COUNT];            /* number order */
extern const char *const sc_export_names[SC_EXPORTS];     /* ordinal order (ordinal = index+1) */
extern const unsigned sc_export_rva[SC_EXPORTS];

struct sc_pe {
    const unsigned char *base;
    const IMAGE_NT_HEADERS64 *nt;
    DWORD size_of_image;
    const IMAGE_EXPORT_DIRECTORY *ed;
    DWORD ed_rva, ed_size;
    const DWORD *funcs, *names;
    const WORD *ords;
};
/* Maps module `dll` headers + export directory; returns 0 on any structural problem. */
int sc_pe_open(const char *dll, struct sc_pe *pe);
const IMAGE_SECTION_HEADER *sc_pe_section(const struct sc_pe *pe, DWORD rva);
/* Export lookup by exact name through the export directory itself (binary search like the loader). */
int sc_pe_find(const struct sc_pe *pe, const char *name, DWORD *rva_out, WORD *ord_out);
const char *sc_pe_name(const struct sc_pe *pe, DWORD i);

/* The 24 bytes a 19041 x64 win32u stub with service number `num` must contain. */
void sc_expect_stub(unsigned num, unsigned char out[SC_STUB_LEN]);
/* Address of win32u's export `name` (NULL if absent). */
const unsigned char *sc_stub(const char *name);

int sc_on_19041(void);            /* build 19041: table/number/layout expectations are hard checks */
int sc_skip_wine(const char *what); /* true (after a note) under Wine: callers return */
void sc_note_once(const char *expr);

/* Hard CHECK on 19041, t_note elsewhere (the 1316-entry table is specific to that build). */
#define CHECK19(c) do { if (sc_on_19041()) CHECK(c); else if (!(c)) sc_note_once(#c); } while (0)
#define CHECK19F(c, ...) do { if (sc_on_19041()) CHECKF(c, __VA_ARGS__); else if (!(c)) sc_note_once(#c); } while (0)
#endif
