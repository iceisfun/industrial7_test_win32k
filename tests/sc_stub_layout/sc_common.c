#include "sc_common.h"

static int g_build_known, g_is19041;
int sc_on_19041(void) {
    if (!g_build_known) {
        unsigned long b = 0, u = 0;
        k_win_build(&b, &u);
        g_is19041 = (b == 19041);
        g_build_known = 1;
    }
    return g_is19041;
}
int sc_skip_wine(const char *what) {
    if (!t_is_wine()) return 0;
    t_note("Wine: its win32u is not the Windows stub set, %s not evaluated", what);
    return 1;
}
void sc_note_once(const char *expr) {
    static int n;
    if (n++ < 8) t_note("non-19041 build, expectation differs: %s", expr);
}

static int in_img(const struct sc_pe *pe, DWORD rva, DWORD len) {
    return rva < pe->size_of_image && len <= pe->size_of_image - rva;
}
int sc_pe_open(const char *dll, struct sc_pe *pe) {
    memset(pe, 0, sizeof *pe);
    HMODULE m = t_module(dll);
    if (!m) return 0;
    const unsigned char *b = (const unsigned char *)m;
    IMAGE_DOS_HEADER dos;
    if (!k_try_read(b, &dos, sizeof dos) || dos.e_magic != IMAGE_DOS_SIGNATURE) return 0;
    if (dos.e_lfanew <= 0 || dos.e_lfanew > 0x1000) return 0;
    const IMAGE_NT_HEADERS64 *nt = (const IMAGE_NT_HEADERS64 *)(b + dos.e_lfanew);
    IMAGE_NT_HEADERS64 h;
    if (!k_try_read(nt, &h, sizeof h) || h.Signature != IMAGE_NT_SIGNATURE) return 0;
    if (h.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return 0;
    pe->base = b; pe->nt = nt; pe->size_of_image = h.OptionalHeader.SizeOfImage;
    IMAGE_DATA_DIRECTORY dd = h.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    pe->ed_rva = dd.VirtualAddress; pe->ed_size = dd.Size;
    if (!dd.VirtualAddress || !in_img(pe, dd.VirtualAddress, sizeof(IMAGE_EXPORT_DIRECTORY))) return 0;
    pe->ed = (const IMAGE_EXPORT_DIRECTORY *)(b + dd.VirtualAddress);
    IMAGE_EXPORT_DIRECTORY e;
    if (!k_try_read(pe->ed, &e, sizeof e)) return 0;
    if (!in_img(pe, e.AddressOfFunctions, e.NumberOfFunctions * 4) ||
        !in_img(pe, e.AddressOfNames, e.NumberOfNames * 4) ||
        !in_img(pe, e.AddressOfNameOrdinals, e.NumberOfNames * 2)) return 0;
    pe->funcs = (const DWORD *)(b + e.AddressOfFunctions);
    pe->names = (const DWORD *)(b + e.AddressOfNames);
    pe->ords  = (const WORD *)(b + e.AddressOfNameOrdinals);
    return 1;
}
const IMAGE_SECTION_HEADER *sc_pe_section(const struct sc_pe *pe, DWORD rva) {
    const IMAGE_SECTION_HEADER *s = IMAGE_FIRST_SECTION(pe->nt);
    for (unsigned i = 0; i < pe->nt->FileHeader.NumberOfSections; i++) {
        DWORD sz = s[i].Misc.VirtualSize > s[i].SizeOfRawData ? s[i].Misc.VirtualSize : s[i].SizeOfRawData;
        if (rva >= s[i].VirtualAddress && rva - s[i].VirtualAddress < sz) return &s[i];
    }
    return NULL;
}
const char *sc_pe_name(const struct sc_pe *pe, DWORD i) {
    if (i >= pe->ed->NumberOfNames) return NULL;
    DWORD r = pe->names[i];
    if (!in_img(pe, r, 1)) return NULL;
    return (const char *)(pe->base + r);
}
int sc_pe_find(const struct sc_pe *pe, const char *name, DWORD *rva_out, WORD *ord_out) {
    long lo = 0, hi = (long)pe->ed->NumberOfNames - 1;
    while (lo <= hi) {
        long mid = (lo + hi) / 2;
        const char *n = sc_pe_name(pe, (DWORD)mid);
        if (!n) return 0;
        int c = strcmp(name, n);
        if (c == 0) {
            WORD o = pe->ords[mid];
            if (o >= pe->ed->NumberOfFunctions) return 0;
            if (rva_out) *rva_out = pe->funcs[o];
            if (ord_out) *ord_out = (WORD)(o + pe->ed->Base);
            return 1;
        }
        if (c < 0) hi = mid - 1; else lo = mid + 1;
    }
    return 0;
}

void sc_expect_stub(unsigned num, unsigned char o[SC_STUB_LEN]) {
    static const unsigned char tmpl[SC_STUB_LEN] = {
        0x4C, 0x8B, 0xD1,                                     /* mov r10, rcx            */
        0xB8, 0, 0, 0, 0,                                     /* mov eax, imm32          */
        0xF6, 0x04, 0x25, 0x08, 0x03, 0xFE, 0x7F, 0x01,       /* test byte [7FFE0308h],1 */
        0x75, 0x03,                                           /* jne +3 -> int 2e        */
        0x0F, 0x05,                                           /* syscall                 */
        0xC3,                                                 /* ret                     */
        0xCD, 0x2E,                                           /* int 2eh                 */
        0xC3                                                  /* ret                     */
    };
    memcpy(o, tmpl, SC_STUB_LEN);
    memcpy(o + 4, &num, 4);
}
const unsigned char *sc_stub(const char *name) {
    return (const unsigned char *)t_proc_in("win32u.dll", name);
}
