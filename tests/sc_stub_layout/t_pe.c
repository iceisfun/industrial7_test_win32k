/* sc_stub_layout: the PE-level shape of win32u.dll as the loader sees it - export directory
 * sweep (names, ordinals, forwarders, ordering), the data export gDispatchTableValues, and how
 * dependants (user32/gdi32/gdi32full/imm32) bound their imports to these stubs. */
#include "sc_common.h"

static int open_win32u(struct sc_pe *pe) {
    if (!sc_pe_open("win32u.dll", pe)) { t_skip("win32u.dll headers unreadable"); return 0; }
    return 1;
}

TEST(sc_stub_layout_pe_machine_and_flags) {
    struct sc_pe pe; if (!open_win32u(&pe)) return;
    CHECK(pe.nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64);
    CHECK(pe.nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC);
    CHECK(pe.nt->FileHeader.Characteristics & IMAGE_FILE_DLL);
    CHECK(pe.nt->FileHeader.Characteristics & IMAGE_FILE_EXECUTABLE_IMAGE);
    CHECK((pe.nt->OptionalHeader.SectionAlignment & 0xFFF) == 0);
    CHECK(pe.size_of_image >= 0x10000);
}
TEST(sc_stub_layout_pe_no_entry_point_no_imports) {
    /* win32u is a pure stub library: AddressOfEntryPoint 0 and an empty import directory (the
     * reference image lists nimports_static 0, entry_rva 0), so loading it runs no code. */
    struct sc_pe pe; if (!open_win32u(&pe)) return;
    if (t_is_wine()) { t_note("Wine's win32u has code and imports"); return; }
    CHECK19(pe.nt->OptionalHeader.AddressOfEntryPoint == 0);
    const IMAGE_DATA_DIRECTORY *d = pe.nt->OptionalHeader.DataDirectory;
    CHECK19(d[IMAGE_DIRECTORY_ENTRY_IMPORT].Size == 0 || d[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress == 0);
    CHECK19(d[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT].Size == 0);
}
TEST(sc_stub_layout_pe_sections) {
    struct sc_pe pe; if (!open_win32u(&pe)) return;
    const IMAGE_SECTION_HEADER *s = IMAGE_FIRST_SECTION(pe.nt);
    int text = 0;
    for (unsigned i = 0; i < pe.nt->FileHeader.NumberOfSections; i++) {
        if (!memcmp(s[i].Name, ".text", 6)) {
            text++;
            CHECK(s[i].Characteristics & IMAGE_SCN_CNT_CODE);
            CHECK(s[i].Characteristics & IMAGE_SCN_MEM_EXECUTE);
            CHECK(!(s[i].Characteristics & IMAGE_SCN_MEM_WRITE));
            CHECK19(s[i].VirtualAddress == 0x1000);
        }
        /* W^X: no section is both writable and executable */
        CHECK(!((s[i].Characteristics & IMAGE_SCN_MEM_WRITE) && (s[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)));
    }
    EXPECT_EQ(text, 1);
    CHECK19(pe.nt->FileHeader.NumberOfSections == 6);
}
TEST(sc_stub_layout_pe_export_directory_counts) {
    struct sc_pe pe; if (!open_win32u(&pe)) return;
    if (sc_skip_wine("export counts")) return;
    EXPECT_EQ(pe.ed->NumberOfNames, pe.ed->NumberOfFunctions);   /* every export is named */
    CHECK19(pe.ed->NumberOfFunctions == SC_EXPORTS);
    CHECK19(pe.ed->Base == 1);
    CHECK(pe.ed->NumberOfFunctions >= SC_COUNT);
    char nm[32] = {0};
    if (k_try_read(pe.base + pe.ed->Name, nm, 12)) CHECK(!lstrcmpiA(nm, "win32u.dll"));
}
TEST(sc_stub_layout_pe_export_names_strictly_sorted) {
    /* the loader's GetProcAddress binary-searches AddressOfNames: must be strictly ascending bytes */
    struct sc_pe pe; if (!open_win32u(&pe)) return;
    const char *prev = NULL;
    for (DWORD i = 0; i < pe.ed->NumberOfNames; i++) {
        const char *n = sc_pe_name(&pe, i);
        if (!n) { CHECKF(0, "name %lu outside image", (unsigned long)i); continue; }
        if (prev) CHECKF(strcmp(prev, n) < 0, "%s !< %s", prev, n);
        prev = n;
    }
}
TEST(sc_stub_layout_pe_export_ordinal_table_is_identity) {
    /* names are sorted alphabetically and the linker numbered functions in that order too */
    struct sc_pe pe; if (!open_win32u(&pe)) return;
    if (sc_skip_wine("ordinal identity")) return;
    for (DWORD i = 0; i < pe.ed->NumberOfNames; i++)
        CHECK19F(pe.ords[i] == i, "name %lu -> ordinal index %u", (unsigned long)i, pe.ords[i]);
}
TEST(sc_stub_layout_pe_export_names_match_reference_by_ordinal) {
    struct sc_pe pe; if (!open_win32u(&pe)) return;
    if (sc_skip_wine("export names")) return;
    if (pe.ed->NumberOfNames != SC_EXPORTS) { t_note("export count %lu, reference has %d", (unsigned long)pe.ed->NumberOfNames, SC_EXPORTS); }
    DWORD n = pe.ed->NumberOfNames < SC_EXPORTS ? pe.ed->NumberOfNames : SC_EXPORTS;
    for (DWORD i = 0; i < n; i++) {
        const char *g = sc_pe_name(&pe, i);
        CHECK19F(g && !strcmp(g, sc_export_names[i]), "ordinal %lu got %s want %s", (unsigned long)i + 1, g ? g : "?", sc_export_names[i]);
    }
}
TEST(sc_stub_layout_pe_exports_have_no_forwarders) {
    struct sc_pe pe; if (!open_win32u(&pe)) return;
    if (sc_skip_wine("forwarders")) return;
    for (DWORD i = 0; i < pe.ed->NumberOfFunctions; i++) {
        DWORD r = pe.funcs[i];
        if (!r) continue;
        /* an RVA inside the export directory is a "DLL.Name" forwarder string */
        CHECKF(!(r >= pe.ed_rva && r < pe.ed_rva + pe.ed_size), "function %lu forwards", (unsigned long)i);
        const IMAGE_SECTION_HEADER *s = sc_pe_section(&pe, r);
        CHECKF(s && !memcmp(s->Name, ".text", 6), "function %lu RVA 0x%lx outside .text", (unsigned long)i, (unsigned long)r);
    }
    CHECK(1);
}
TEST(sc_stub_layout_pe_export_rva_agrees_with_getprocaddress) {
    struct sc_pe pe; if (!open_win32u(&pe)) return;
    for (DWORD i = 0; i < pe.ed->NumberOfNames; i++) {
        const char *n = sc_pe_name(&pe, i);
        if (!n) continue;
        FARPROC p = GetProcAddress((HMODULE)pe.base, n);
        DWORD want = pe.funcs[pe.ords[i]];
        CHECKF(p == (FARPROC)(pe.base + want), "%s: GetProcAddress %p vs RVA 0x%lx", n, p, (unsigned long)want);
        DWORD r; WORD o;
        CHECKF(sc_pe_find(&pe, n, &r, &o) && r == want, "%s binary search", n);
        CHECKF(GetProcAddress((HMODULE)pe.base, (LPCSTR)(ULONG_PTR)(pe.ords[i] + pe.ed->Base)) == p, "%s by ordinal", n);
    }
}
TEST(sc_stub_layout_pe_every_nt_export_is_a_stub) {
    struct sc_pe pe; if (!open_win32u(&pe)) return;
    if (sc_skip_wine("stub exports")) return;
    int stubs = 0, other = 0;
    for (DWORD i = 0; i < pe.ed->NumberOfNames; i++) {
        const char *n = sc_pe_name(&pe, i);
        if (!n) continue;
        FARPROC p = GetProcAddress((HMODULE)pe.base, n);
        if (k_stub_valid(p)) { stubs++; CHECK((unsigned long)k_stub_number(p) - SC_FIRST < 0x1000); }
        else { other++; t_note("export %s is not a syscall stub", n); CHECK19(!strcmp(n, "gDispatchTableValues")); }
    }
    CHECK19(stubs == SC_COUNT);
    CHECK19(other == 1);
}
TEST(sc_stub_layout_pe_reference_names_all_exported) {
    struct sc_pe pe; if (!open_win32u(&pe)) return;
    if (sc_skip_wine("reference names")) return;
    for (unsigned i = 0; i < SC_COUNT; i++) {
        DWORD r; WORD o;
        int f = sc_pe_find(&pe, sc_tab[i].name, &r, &o);
        CHECKF(f, "%s missing from export directory", sc_tab[i].name);
        if (f) CHECK19F(o == sc_tab[i].ord, "%s ordinal %u want %u", sc_tab[i].name, o, sc_tab[i].ord);
    }
}
TEST(sc_stub_layout_pe_gdispatchtablevalues_export) {
    struct sc_pe pe; if (!open_win32u(&pe)) return;
    if (sc_skip_wine("gDispatchTableValues")) return;
    DWORD r; WORD o;
    CHECK(sc_pe_find(&pe, "gDispatchTableValues", &r, &o));
    CHECK(GetProcAddress((HMODULE)pe.base, "gDispatchTableValues") == (FARPROC)(pe.base + r));
    CHECK19(o == SC_EXPORTS);                                       /* sorts last: lower-case 'g' > 'N' */
    const unsigned char *last = sc_stub(sc_tab[SC_COUNT - 1].name);
    /* the data object sits directly behind the last stub's 0x18 bytes (no 8-byte slot padding) */
    CHECK19F(pe.base + r == last + SC_STUB_LEN, "gDispatchTableValues at +0x%lx, last stub at +0x%lx", (unsigned long)r, (unsigned long)(last - pe.base));
    unsigned short v[5] = {0};
    if (k_try_read(pe.base + r, v, sizeof v)) {
        /* first word equals the number of services: 0x524 == 1316 == SC_COUNT on the reference image */
        CHECK19F(v[0] == SC_COUNT, "first word 0x%x", v[0]);
        t_note("gDispatchTableValues words: %04x %04x %04x %04x %04x", v[0], v[1], v[2], v[3], v[4]);
    } else CHECK(0);
}

/* ---- how dependants bound their imports from win32u --------------------- */
static void check_import_binding(const char *dll) {
    struct sc_pe w; if (!open_win32u(&w)) return;
    struct sc_pe m;
    HMODULE h = LoadLibraryA(dll);
    if (!h) { t_skip("%s not loadable", dll); return; }
    IMAGE_DOS_HEADER dos; const unsigned char *b = (const unsigned char *)h;
    if (!k_try_read(b, &dos, sizeof dos)) { t_skip("%s headers", dll); return; }
    const IMAGE_NT_HEADERS64 *nt = (const IMAGE_NT_HEADERS64 *)(b + dos.e_lfanew);
    IMAGE_DATA_DIRECTORY dd;
    if (!k_try_read(&nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT], &dd, sizeof dd) || !dd.VirtualAddress) { t_skip("%s has no imports", dll); return; }
    int bound = 0, stubs = 0, data = 0;
    for (const IMAGE_IMPORT_DESCRIPTOR *d = (const IMAGE_IMPORT_DESCRIPTOR *)(b + dd.VirtualAddress); ; d++) {
        IMAGE_IMPORT_DESCRIPTOR id;
        if (!k_try_read(d, &id, sizeof id) || !id.Name) break;
        char nm[64] = {0};
        if (!k_try_read(b + id.Name, nm, 63)) break;
        if (lstrcmpiA(nm, "win32u.dll")) continue;
        DWORD intr = id.OriginalFirstThunk ? id.OriginalFirstThunk : id.FirstThunk;
        for (unsigned i = 0; ; i++) {
            ULONGLONG ent, iat;
            if (!k_try_read(b + intr + i * 8, &ent, 8) || !ent) break;
            if (!k_try_read(b + id.FirstThunk + i * 8, &iat, 8)) { CHECK(0); break; }
            FARPROC want;
            char label[96];
            if (ent & IMAGE_ORDINAL_FLAG64) {
                want = t_proc_ord("win32u.dll", (unsigned)(ent & 0xFFFF));
                snprintf(label, sizeof label, "ordinal %u", (unsigned)(ent & 0xFFFF));
            } else {
                char fn[96] = {0};
                if (!k_try_read(b + (DWORD)ent + 2, fn, 95)) { CHECK(0); continue; }
                want = t_proc_in("win32u.dll", fn);
                snprintf(label, sizeof label, "%s", fn);
            }
            /* the loader writes the export's absolute address into the IAT slot */
            CHECKF(want != NULL && iat == (ULONGLONG)(ULONG_PTR)want, "%s!%s IAT 0x%llx want %p", dll, label, iat, want);
            bound++;
            if (want && k_stub_valid(want)) stubs++; else data++;
        }
    }
    (void)m; (void)w;
    t_note("%s binds %d win32u imports (%d syscall stubs, %d other)", dll, bound, stubs, data);
    if (!t_is_wine()) CHECK(bound > 0 || !strcmp(dll, "imm32.dll"));
}
TEST(sc_stub_layout_import_binding_user32) { check_import_binding("user32.dll"); }
TEST(sc_stub_layout_import_binding_gdi32) { check_import_binding("gdi32.dll"); }
TEST(sc_stub_layout_import_binding_gdi32full) { check_import_binding("gdi32full.dll"); }
TEST(sc_stub_layout_import_binding_imm32) { check_import_binding("imm32.dll"); }
TEST(sc_stub_layout_user32_imports_gdispatchtablevalues) {
    /* user32 imports the data symbol gDispatchTableValues (the reference corpus lists it as a
     * user32 import); its IAT slot must hold the win32u export address. */
    if (sc_skip_wine("user32 data import")) return;
    HMODULE h = LoadLibraryA("user32.dll"); REQUIRE(h);
    const unsigned char *b = (const unsigned char *)h;
    IMAGE_DOS_HEADER dos; if (!k_try_read(b, &dos, sizeof dos)) { t_skip("hdr"); return; }
    const IMAGE_NT_HEADERS64 *nt = (const IMAGE_NT_HEADERS64 *)(b + dos.e_lfanew);
    IMAGE_DATA_DIRECTORY dd; k_try_read(&nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT], &dd, sizeof dd);
    int found = 0;
    for (const IMAGE_IMPORT_DESCRIPTOR *d = (const IMAGE_IMPORT_DESCRIPTOR *)(b + dd.VirtualAddress); dd.VirtualAddress; d++) {
        IMAGE_IMPORT_DESCRIPTOR id; char nm[64] = {0};
        if (!k_try_read(d, &id, sizeof id) || !id.Name || !k_try_read(b + id.Name, nm, 63)) break;
        if (lstrcmpiA(nm, "win32u.dll")) continue;
        for (unsigned i = 0; id.OriginalFirstThunk; i++) {
            ULONGLONG ent, iat; char fn[64] = {0};
            if (!k_try_read(b + id.OriginalFirstThunk + i * 8, &ent, 8) || !ent) break;
            if ((ent & IMAGE_ORDINAL_FLAG64) || !k_try_read(b + (DWORD)ent + 2, fn, 63)) continue;
            if (strcmp(fn, "gDispatchTableValues")) continue;
            found++;
            k_try_read(b + id.FirstThunk + i * 8, &iat, 8);
            CHECK(iat == (ULONGLONG)(ULONG_PTR)GetProcAddress(GetModuleHandleA("win32u.dll"), "gDispatchTableValues"));
        }
    }
    CHECK19(found == 1);
}
