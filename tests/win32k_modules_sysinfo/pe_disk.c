/* win32k_modules_sysinfo: the on-disk images of the Win32 driver stack
 * (<System32>\win32k.sys, win32kbase.sys, win32kfull.sys) parsed as plain files, and cross-checked
 * against the loaded kernel module list and against the win32u.dll syscall stubs.
 *
 * Facts used (observed on 19041): all three are native-subsystem PE32+ x64 drivers; win32k.sys
 * is the service-thunk host: it exports one __win32kstub_<Nt name> per service plus the data
 * exports W32pServiceTable / W32pServiceLimit / W32pArgumentTable (and *Filter twins) that live in
 * the read-only TABLERO section. The table holds one image-relative function RVA per service
 * number - 0x1000 (win32u stub eax) indexes entry 0 - and W32pArgumentTable holds one byte per
 * service giving the stack bytes of arguments beyond the fourth ((nargs-4)*4). win32kfull.sys
 * imports win32kbase.sys; win32k.sys reaches the others through ext-ms-win-core-win32k-* api sets.
 */
#include "ms.h"

static const char *const MS_LEAVES[3] = { "win32k.sys", "win32kbase.sys", "win32kfull.sys" };

/* Generates three TESTs (one per image) from fn(const char *leaf). */
#define MS_PE_TESTS(name, fn) \
    TEST(win32k_modules_sysinfo_pe_win32k_##name)     { fn("win32k.sys"); } \
    TEST(win32k_modules_sysinfo_pe_win32kbase_##name) { fn("win32kbase.sys"); } \
    TEST(win32k_modules_sysinfo_pe_win32kfull_##name) { fn("win32kfull.sys"); }

#define MS_NEED_PE(var, leaf) \
    if (t_is_wine()) { t_skip("%s is a placeholder module under the emulator", leaf); return; } \
    MS_PE *var = ms_pe(leaf); \
    if (!var) { \
        CHECKF(0, "cannot load/parse %s from the system directory", leaf); return; }

static void pe_readable(const char *leaf) {
    MS_NEED_PE(pe, leaf);
    CHECK(pe->size > 0x10000);
    t_note("%s file size %llu", leaf, (unsigned long long)pe->size);
}
MS_PE_TESTS(file_parses, pe_readable)

static void pe_machine(const char *leaf) {
    MS_NEED_PE(pe, leaf);
    CHECK(pe->nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64);
    CHECK(pe->nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC);
    CHECK(pe->nt->FileHeader.SizeOfOptionalHeader >= 0xF0);
}
MS_PE_TESTS(machine_is_amd64_pe32plus, pe_machine)

static void pe_subsystem(const char *leaf) {
    MS_NEED_PE(pe, leaf);
    CHECKF(pe->nt->OptionalHeader.Subsystem == IMAGE_SUBSYSTEM_NATIVE, "subsystem %u", pe->nt->OptionalHeader.Subsystem);
}
MS_PE_TESTS(subsystem_is_native, pe_subsystem)

static void pe_characteristics(const char *leaf) {
    MS_NEED_PE(pe, leaf);
    WORD c = pe->nt->FileHeader.Characteristics;
    CHECK(c & IMAGE_FILE_EXECUTABLE_IMAGE);
    CHECK(c & IMAGE_FILE_LARGE_ADDRESS_AWARE);
    CHECK(!(c & IMAGE_FILE_RELOCS_STRIPPED));   /* session images are relocated, so the .reloc data must be kept */
    CHECK(!(c & IMAGE_FILE_32BIT_MACHINE));
    t_note("%s characteristics=0x%04x dllcharacteristics=0x%04x", leaf, c, pe->nt->OptionalHeader.DllCharacteristics);
}
MS_PE_TESTS(characteristics, pe_characteristics)

static void pe_has_reloc_dir(const char *leaf) {
    MS_NEED_PE(pe, leaf);
    const IMAGE_DATA_DIRECTORY *d = ms_pe_dir(pe, IMAGE_DIRECTORY_ENTRY_BASERELOC);
    CHECK(d != NULL);
    if (d) CHECK(d->Size >= 8);
}
MS_PE_TESTS(has_base_relocations, pe_has_reloc_dir)

static void pe_alignments(const char *leaf) {
    MS_NEED_PE(pe, leaf);
    const IMAGE_OPTIONAL_HEADER64 *o = &pe->nt->OptionalHeader;
    CHECK(o->SectionAlignment >= 0x20 && (o->SectionAlignment & (o->SectionAlignment - 1)) == 0);
    CHECK(o->FileAlignment >= 0x200 && (o->FileAlignment & (o->FileAlignment - 1)) == 0);
    CHECK(o->SectionAlignment >= o->FileAlignment);
    CHECK(o->SizeOfImage % o->SectionAlignment == 0);
    CHECK(o->SizeOfHeaders % o->FileAlignment == 0);
    CHECK(o->SizeOfHeaders <= o->SizeOfImage);
}
MS_PE_TESTS(alignment_fields_consistent, pe_alignments)

static void pe_sections_layout(const char *leaf) {
    MS_NEED_PE(pe, leaf);
    const IMAGE_OPTIONAL_HEADER64 *o = &pe->nt->OptionalHeader;
    DWORD prev_end = o->SizeOfHeaders;
    for (int i = 0; i < pe->nsec; i++) {
        const IMAGE_SECTION_HEADER *s = &pe->sec[i];
        char nm[9] = {0}; memcpy(nm, s->Name, 8);
        CHECKF(s->VirtualAddress >= prev_end, "%s section %s at 0x%lx overlaps previous (end 0x%lx)", leaf, nm, (unsigned long)s->VirtualAddress, (unsigned long)prev_end);
        CHECKF(s->VirtualAddress % o->SectionAlignment == 0, "%s misaligned", nm);
        DWORD vs = s->Misc.VirtualSize ? s->Misc.VirtualSize : s->SizeOfRawData;
        prev_end = s->VirtualAddress + vs;
        CHECKF(prev_end <= o->SizeOfImage, "%s section %s ends 0x%lx beyond SizeOfImage 0x%lx", leaf, nm, (unsigned long)prev_end, (unsigned long)o->SizeOfImage);
        CHECK(s->PointerToRawData + s->SizeOfRawData <= pe->size);
        CHECK(nm[0] != 0);
    }
    /* SizeOfImage is the aligned end of the last section. */
    DWORD al = o->SectionAlignment;
    CHECK(((prev_end + al - 1) & ~(al - 1)) == o->SizeOfImage);
}
MS_PE_TESTS(section_table_consistent, pe_sections_layout)

static void pe_no_rwx(const char *leaf) {
    MS_NEED_PE(pe, leaf);
    for (int i = 0; i < pe->nsec; i++) {
        DWORD f = pe->sec[i].Characteristics;
        char nm[9] = {0}; memcpy(nm, pe->sec[i].Name, 8);
        CHECKF(!((f & IMAGE_SCN_MEM_EXECUTE) && (f & IMAGE_SCN_MEM_WRITE)), "%s: section %s is writable and executable", leaf, nm);
    }
}
MS_PE_TESTS(no_writable_executable_section, pe_no_rwx)

static const IMAGE_SECTION_HEADER *pe_find_sec(const MS_PE *pe, const char *name) {
    for (int i = 0; i < pe->nsec; i++) if (!strncmp((const char *)pe->sec[i].Name, name, 8)) return &pe->sec[i];
    return NULL;
}

static void pe_text_exec(const char *leaf) {
    MS_NEED_PE(pe, leaf);
    const IMAGE_SECTION_HEADER *t = pe_find_sec(pe, ".text");
    CHECK(t != NULL);
    if (!t) return;
    CHECK(t->Characteristics & IMAGE_SCN_CNT_CODE);
    CHECK(t->Characteristics & IMAGE_SCN_MEM_EXECUTE);
    CHECK(t->Characteristics & IMAGE_SCN_MEM_READ);
    CHECK(!(t->Characteristics & IMAGE_SCN_MEM_WRITE));
    CHECK(t->VirtualAddress == 0x1000);
}
MS_PE_TESTS(text_section_is_rx_code, pe_text_exec)

static void pe_init_discardable(const char *leaf) {
    MS_NEED_PE(pe, leaf);
    const IMAGE_SECTION_HEADER *s = pe_find_sec(pe, "INIT");
    CHECK(s != NULL);
    if (s) CHECK(s->Characteristics & IMAGE_SCN_MEM_DISCARDABLE);   /* DriverEntry lives here and is freed after load */
}
MS_PE_TESTS(init_section_is_discardable, pe_init_discardable)

static void pe_entry_point(const char *leaf) {
    MS_NEED_PE(pe, leaf);
    DWORD ep = pe->nt->OptionalHeader.AddressOfEntryPoint;
    CHECK(ep != 0);
    const IMAGE_SECTION_HEADER *s = ms_pe_sec_of(pe, ep);
    CHECK(s != NULL);
    if (s) CHECK(s->Characteristics & IMAGE_SCN_MEM_EXECUTE);
    if (s) { char nm[9] = {0}; memcpy(nm, s->Name, 8); t_note("%s entry 0x%lx in %s", leaf, (unsigned long)ep, nm); }
}
MS_PE_TESTS(entry_point_in_executable_section, pe_entry_point)

static void pe_size_matches_loaded(const char *leaf) {
    MS_NEED_PE(pe, leaf);
    MS_NEED_MOD(i, leaf);
    const MS_MOD *m = &ms_mods()->Modules[i];
    /* The loaded entry's ImageSize is the LDR SizeOfImage, copied from the optional header. */
    CHECKF(m->ImageSize == pe->nt->OptionalHeader.SizeOfImage, "%s loaded 0x%lx disk 0x%lx", leaf, (unsigned long)m->ImageSize, (unsigned long)pe->nt->OptionalHeader.SizeOfImage);
}
MS_PE_TESTS(size_of_image_matches_loaded_size, pe_size_matches_loaded)

static void pe_dll_name_leaf_note(const char *leaf) {
    MS_NEED_PE(pe, leaf);
    const IMAGE_EXPORT_DIRECTORY *ex = ms_pe_exports(pe);
    CHECK(ex != NULL);
    if (!ex) return;
    const char *n = ms_pe_str(pe, ex->Name);
    t_note("%s export directory names the image \"%s\", %lu functions, %lu names", leaf, n ? n : "?", (unsigned long)ex->NumberOfFunctions, (unsigned long)ex->NumberOfNames);
    CHECK(ex->NumberOfNames <= ex->NumberOfFunctions);
    CHECK(ex->NumberOfNames > 100);
}
MS_PE_TESTS(export_directory_present, pe_dll_name_leaf_note)

static void pe_exports_sorted(const char *leaf) {
    MS_NEED_PE(pe, leaf);
    const IMAGE_EXPORT_DIRECTORY *ex = ms_pe_exports(pe); REQUIRE(ex);
    const DWORD *names = ms_pe_dwords(pe, ex->AddressOfNames, ex->NumberOfNames); REQUIRE(names);
    const char *prev = "";
    int bad = 0;
    for (DWORD i = 0; i < ex->NumberOfNames; i++) {
        const char *s = ms_pe_str(pe, names[i]);
        if (!s || strcmp(prev, s) >= 0) bad++;
        prev = s ? s : "";
    }
    /* The PE loader binary-searches the name table: it must be sorted by byte order, no duplicates. */
    CHECKF(bad == 0, "%s: %d unsorted/duplicate export names", leaf, bad);
}
MS_PE_TESTS(export_names_sorted_unique, pe_exports_sorted)

static void pe_export_rvas_in_image(const char *leaf) {
    MS_NEED_PE(pe, leaf);
    const IMAGE_EXPORT_DIRECTORY *ex = ms_pe_exports(pe); REQUIRE(ex);
    const DWORD *funcs = ms_pe_dwords(pe, ex->AddressOfFunctions, ex->NumberOfFunctions); REQUIRE(funcs);
    int bad = 0;
    for (DWORD i = 0; i < ex->NumberOfFunctions; i++)
        if (funcs[i] && funcs[i] >= pe->nt->OptionalHeader.SizeOfImage) bad++;
    CHECKF(bad == 0, "%s: %d export RVAs beyond SizeOfImage", leaf, bad);
    const IMAGE_DATA_DIRECTORY *d = ms_pe_dir(pe, IMAGE_DIRECTORY_ENTRY_EXPORT);
    CHECK(d && d->Size >= sizeof(IMAGE_EXPORT_DIRECTORY));
}
MS_PE_TESTS(export_rvas_inside_image, pe_export_rvas_in_image)

static void pe_name_ordinals_valid(const char *leaf) {
    MS_NEED_PE(pe, leaf);
    const IMAGE_EXPORT_DIRECTORY *ex = ms_pe_exports(pe); REQUIRE(ex);
    size_t oo = ms_pe_off(pe, ex->AddressOfNameOrdinals); REQUIRE(oo != (size_t)-1);
    const WORD *ords = (const WORD *)(pe->base + oo);
    int bad = 0;
    for (DWORD i = 0; i < ex->NumberOfNames; i++) if (ords[i] >= ex->NumberOfFunctions) bad++;
    CHECKF(bad == 0, "%d name ordinals out of range", bad);
}
MS_PE_TESTS(export_name_ordinals_in_range, pe_name_ordinals_valid)

static void pe_imports_ntoskrnl(const char *leaf) {
    MS_NEED_PE(pe, leaf);
    unsigned n = 0;
    CHECK(ms_pe_imports_dll(pe, "ntoskrnl.exe", &n) == 1);
    CHECK(n >= 20);
}
MS_PE_TESTS(imports_ntoskrnl, pe_imports_ntoskrnl)

static void pe_no_user_mode_imports(const char *leaf) {
    MS_NEED_PE(pe, leaf);
    static const char *const bad[] = { "kernel32.dll", "ntdll.dll", "user32.dll", "gdi32.dll", "win32u.dll", "advapi32.dll" };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) CHECKF(ms_pe_imports_dll(pe, bad[i], NULL) == 0, "%s imports user-mode %s", leaf, bad[i]);
}
MS_PE_TESTS(imports_only_kernel_side, pe_no_user_mode_imports)

/* Every import that is not an api set must name a kernel module that is resident right now. */
static void pe_imports_resolve_to_loaded(const char *leaf) {
    MS_NEED_PE(pe, leaf);
    if (!ms_mods() || t_is_wine()) { t_skip("no kernel module list"); return; }
    const IMAGE_DATA_DIRECTORY *d = ms_pe_dir(pe, IMAGE_DIRECTORY_ENTRY_IMPORT); REQUIRE(d);
    size_t o = ms_pe_off(pe, d->VirtualAddress); REQUIRE(o != (size_t)-1);
    int checked = 0;
    for (const IMAGE_IMPORT_DESCRIPTOR *id = (const IMAGE_IMPORT_DESCRIPTOR *)(pe->base + o); id->Name; id++) {
        const char *n = ms_pe_str(pe, id->Name); if (!n) { CHECK(0); continue; }
        if (!_strnicmp(n, "ext-ms-win-", 11) || !_strnicmp(n, "api-ms-win-", 11)) continue;
        CHECKF(ms_find_mod(n) >= 0, "%s imports %s which is not in the kernel module list", leaf, n);
        checked++;
    }
    CHECK(checked >= 2);
}
MS_PE_TESTS(imports_resolve_to_resident_modules, pe_imports_resolve_to_loaded)

/* ---- stack-specific import/export structure --------------------------------------------- */
TEST(win32k_modules_sysinfo_pe_win32kfull_imports_win32kbase) {
    MS_NEED_PE(pe, "win32kfull.sys");
    unsigned n = 0;
    CHECK(ms_pe_imports_dll(pe, "win32kbase.sys", &n) == 1);
    CHECKF(n >= 100, "only %u thunks", n);
}
TEST(win32k_modules_sysinfo_pe_win32kbase_does_not_import_win32kfull) {
    MS_NEED_PE(pe, "win32kbase.sys");
    CHECK(ms_pe_imports_dll(pe, "win32kfull.sys", NULL) == 0);
    CHECK(ms_pe_imports_dll(pe, "win32k.sys", NULL) == 0);
}
TEST(win32k_modules_sysinfo_pe_win32kfull_does_not_import_win32k) {
    MS_NEED_PE(pe, "win32kfull.sys");
    CHECK(ms_pe_imports_dll(pe, "win32k.sys", NULL) == 0);
}
TEST(win32k_modules_sysinfo_pe_win32k_imports_win32k_apisets) {
    MS_NEED_PE(pe, "win32k.sys");
    /* win32k.sys binds the dxgk / flip / surface / token managers through ext-ms-win-core-win32k-*
     * api set contracts instead of naming win32kbase.sys or dxgkrnl.sys directly. */
    static const char *const sets[] = { "ext-ms-win-core-win32k-dxgk-l1-1-0.dll", "ext-ms-win-core-win32k-flipmgr-l1-1-0.dll",
                                        "ext-ms-win-core-win32k-surfmgr-l1-1-0.dll", "ext-ms-win-core-win32k-tokenmgr-l1-1-0.dll" };
    int found = 0;
    for (size_t i = 0; i < 4; i++) {
        unsigned n = 0; int c = ms_pe_imports_dll(pe, sets[i], &n);
        t_note("%s: %d descriptor(s), %u thunks", sets[i], c, n);
        found += c;
    }
    CHECK(found >= 1);
    CHECK(ms_pe_imports_dll(pe, "watchdog.sys", NULL) == 1);
}
TEST(win32k_modules_sysinfo_pe_win32k_preferred_base_is_session_space) {
    MS_NEED_PE(pe, "win32k.sys");
    /* The thunk host is linked at a session-space image base, unlike the two payload drivers. */
    CHECKF((ULONGLONG)pe->nt->OptionalHeader.ImageBase >= MS_KERNEL_START, "ImageBase 0x%llx", (unsigned long long)pe->nt->OptionalHeader.ImageBase);
    MS_NEED_MOD(i, "win32k.sys");
    t_note("preferred 0x%llx loaded %p", (unsigned long long)pe->nt->OptionalHeader.ImageBase, ms_mods()->Modules[i].ImageBase);
}
TEST(win32k_modules_sysinfo_pe_win32k_has_tablero_section) {
    MS_NEED_PE(pe, "win32k.sys");
    const IMAGE_SECTION_HEADER *s = pe_find_sec(pe, "TABLERO");
    CHECK(s != NULL);
    if (!s) return;
    CHECK(!(s->Characteristics & IMAGE_SCN_MEM_WRITE));   /* the service tables are read-only data */
    CHECK(s->Characteristics & IMAGE_SCN_MEM_READ);
    CHECK(!(s->Characteristics & IMAGE_SCN_MEM_EXECUTE));
}
TEST(win32k_modules_sysinfo_pe_win32kbase_full_have_nonpage_and_page_sections) {
    const char *leaves[2] = { "win32kbase.sys", "win32kfull.sys" };
    for (int i = 0; i < 2; i++) {
        MS_NEED_PE(pe, leaves[i]);
        CHECK(pe_find_sec(pe, "PAGE") != NULL);
        CHECK(pe_find_sec(pe, "NONPAGE") != NULL);
    }
}

/* ---- win32k.sys data exports ----------------------------------------------------------- */
static const char *const W32P_NAMES[] = {
    "W32pServiceTable", "W32pServiceLimit", "W32pArgumentTable",
    "W32pServiceTableFilter", "W32pServiceLimitFilter", "W32pArgumentTableFilter",
};
TEST(win32k_modules_sysinfo_pe_win32k_exports_w32p_tables_in_tablero) {
    MS_NEED_PE(pe, "win32k.sys");
    const IMAGE_SECTION_HEADER *tb = pe_find_sec(pe, "TABLERO"); REQUIRE(tb);
    for (size_t i = 0; i < sizeof W32P_NAMES / sizeof *W32P_NAMES; i++) {
        int fwd = 0; DWORD rva = ms_pe_export(pe, W32P_NAMES[i], &fwd);
        CHECKF(rva != 0, "missing export %s", W32P_NAMES[i]);
        CHECK(!fwd);
        if (!rva) continue;
        CHECKF(rva >= tb->VirtualAddress && rva < tb->VirtualAddress + tb->Misc.VirtualSize, "%s RVA 0x%lx outside TABLERO", W32P_NAMES[i], (unsigned long)rva);
    }
}
TEST(win32k_modules_sysinfo_pe_win32k_exports_sysentry_getters) {
    MS_NEED_PE(pe, "win32k.sys");
    static const char *const g[] = {
        "SysEntryGetW32pServiceTable", "SysEntryGetW32pServiceLimit", "SysEntryGetW32pArgumentTable",
        "SysEntryGetW32pServiceTableFilter", "SysEntryGetW32pServiceLimitFilter", "SysEntryGetW32pArgumentTableFilter",
        "SysEntryGetDispatchTableValues", "SysEntryGetDispatchTableValuesFilter",
        "SysEntryPsEstablishWin32Callouts", "SysEntryRegisterWin32kDriverUnload", "SysEntrySMgrNotifySessionChange",
    };
    const IMAGE_SECTION_HEADER *tx = pe_find_sec(pe, ".text"); REQUIRE(tx);
    for (size_t i = 0; i < sizeof g / sizeof *g; i++) {
        int fwd = 0; DWORD rva = ms_pe_export(pe, g[i], &fwd);
        CHECKF(rva != 0, "missing export %s", g[i]);
        CHECK(!fwd);
        if (rva) CHECKF(rva >= tx->VirtualAddress && rva < tx->VirtualAddress + tx->Misc.VirtualSize, "%s not in .text", g[i]);
    }
}
TEST(win32k_modules_sysinfo_pe_win32k_ntoskrnl_forwarders) {
    MS_NEED_PE(pe, "win32k.sys");
    /* Some Eng* helpers are plain forwards to the kernel (rva inside the export directory). */
    static const char *const f[][2] = { { "EngBugCheckEx", "NTOSKRNL.KeBugCheckEx" }, { "EngGetCurrentProcessId", "NTOSKRNL.PsGetCurrentProcessId" } };
    for (int i = 0; i < 2; i++) {
        int fwd = 0; DWORD rva = ms_pe_export(pe, f[i][0], &fwd);
        CHECKF(rva && fwd, "%s should be a forwarder", f[i][0]);
        if (rva && fwd) { const char *s = ms_pe_str(pe, rva); CHECK(s && !strcmp(s, f[i][1])); }
    }
}

static int pe_read_u32_at_export(const MS_PE *pe, const char *name, DWORD *v) {
    DWORD rva = ms_pe_export(pe, name, NULL); if (!rva) return 0;
    size_t o = ms_pe_off(pe, rva); if (o == (size_t)-1 || o + 4 > pe->size) return 0;
    memcpy(v, pe->base + o, 4); return 1;
}

TEST(win32k_modules_sysinfo_pe_win32k_service_limit_value) {
    MS_NEED_PE(pe, "win32k.sys");
    DWORD lim = 0; CHECK(pe_read_u32_at_export(pe, "W32pServiceLimit", &lim));
    DWORD flim = 0; CHECK(pe_read_u32_at_export(pe, "W32pServiceLimitFilter", &flim));
    t_note("W32pServiceLimit=%lu W32pServiceLimitFilter=%lu", (unsigned long)lim, (unsigned long)flim);
    CHECK(lim >= 1000 && lim < 4096);          /* service index is 12 bits wide (eax & 0xfff in KiSystemServiceStart) */
    CHECK(flim <= lim);
}

/* count of __win32kstub_ exports == W32pServiceLimit */
static unsigned pe_count_stub_exports(const MS_PE *pe) {
    const IMAGE_EXPORT_DIRECTORY *ex = ms_pe_exports(pe); if (!ex) return 0;
    const DWORD *names = ms_pe_dwords(pe, ex->AddressOfNames, ex->NumberOfNames); if (!names) return 0;
    unsigned n = 0;
    for (DWORD i = 0; i < ex->NumberOfNames; i++) { const char *s = ms_pe_str(pe, names[i]); if (s && !strncmp(s, "__win32kstub_", 13)) n++; }
    return n;
}
TEST(win32k_modules_sysinfo_pe_win32k_stub_export_count_equals_limit) {
    MS_NEED_PE(pe, "win32k.sys");
    DWORD lim = 0; REQUIRE(pe_read_u32_at_export(pe, "W32pServiceLimit", &lim));
    unsigned n = pe_count_stub_exports(pe);
    CHECKF(n == lim, "%u __win32kstub_ exports, limit %lu", n, (unsigned long)lim);
}

TEST(win32k_modules_sysinfo_pe_win32k_service_table_entries_in_text) {
    MS_NEED_PE(pe, "win32k.sys");
    DWORD lim = 0; REQUIRE(pe_read_u32_at_export(pe, "W32pServiceLimit", &lim));
    DWORD trva = ms_pe_export(pe, "W32pServiceTable", NULL); REQUIRE(trva);
    const DWORD *tab = ms_pe_dwords(pe, trva, lim); REQUIRE(tab);
    const IMAGE_SECTION_HEADER *tx = pe_find_sec(pe, ".text"); REQUIRE(tx);
    int bad = 0;
    for (DWORD i = 0; i < lim; i++)
        if (tab[i] < tx->VirtualAddress || tab[i] >= tx->VirtualAddress + tx->Misc.VirtualSize) bad++;
    /* Each entry is the image-relative RVA of the service routine (a win32k.sys thunk). */
    CHECKF(bad == 0, "%d service table entries outside .text", bad);
}

TEST(win32k_modules_sysinfo_pe_win32k_argument_table_shape) {
    MS_NEED_PE(pe, "win32k.sys");
    DWORD lim = 0; REQUIRE(pe_read_u32_at_export(pe, "W32pServiceLimit", &lim));
    DWORD arva = ms_pe_export(pe, "W32pArgumentTable", NULL); REQUIRE(arva);
    size_t o = ms_pe_off(pe, arva); REQUIRE(o != (size_t)-1 && o + lim <= pe->size);
    const unsigned char *a = pe->base + o;
    int bad4 = 0, big = 0; unsigned mx = 0;
    for (DWORD i = 0; i < lim; i++) { if (a[i] & 3) bad4++; if (a[i] > 0x80) big++; if (a[i] > mx) mx = a[i]; }
    /* Stack argument bytes are (nargs-4)*4; no service takes more than ~32 parameters. */
    CHECKF(bad4 == 0, "%d entries not a multiple of 4", bad4);
    CHECKF(big == 0, "%d entries above 0x80", big);
    t_note("largest stack-argument size 0x%x", mx);
}

/* ---- cross-check with the win32u.dll stubs ---------------------------------------------- */
struct ms_stub { char name[96]; long number; };
static struct ms_stub *ms_stubs(unsigned *count) {
    static struct ms_stub *arr; static unsigned n; static int done;
    if (done) { *count = n; return arr; }
    done = 1;
    HMODULE m = GetModuleHandleA("win32u.dll");
    if (!m) m = LoadLibraryA("win32u.dll");
    if (!m) { *count = 0; return NULL; }
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)m;
    const IMAGE_NT_HEADERS64 *nt = (const IMAGE_NT_HEADERS64 *)((const char *)m + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY *d = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    const IMAGE_EXPORT_DIRECTORY *ex = (const IMAGE_EXPORT_DIRECTORY *)((const char *)m + d->VirtualAddress);
    const DWORD *names = (const DWORD *)((const char *)m + ex->AddressOfNames);
    const DWORD *funcs = (const DWORD *)((const char *)m + ex->AddressOfFunctions);
    const WORD *ords = (const WORD *)((const char *)m + ex->AddressOfNameOrdinals);
    arr = (struct ms_stub *)calloc(ex->NumberOfNames, sizeof *arr);
    for (DWORD i = 0; arr && i < ex->NumberOfNames; i++) {
        FARPROC p = (FARPROC)((const char *)m + funcs[ords[i]]);
        if (funcs[ords[i]] >= d->VirtualAddress && funcs[ords[i]] < d->VirtualAddress + d->Size) continue;   /* forwarder */
        if (!k_stub_valid(p)) continue;
        long num = k_stub_number(p);
        if (num < 0x1000) continue;
        snprintf(arr[n].name, sizeof arr[n].name, "%s", (const char *)m + names[i]);
        arr[n].number = num; n++;
    }
    *count = n;
    return arr;
}
static int ms_stub_cmp(const void *a, const void *b) { return ((const struct ms_stub *)a)->number - ((const struct ms_stub *)b)->number; }

TEST(win32k_modules_sysinfo_pe_win32u_stub_numbers_contiguous) {
    unsigned n; struct ms_stub *s = ms_stubs(&n);
    if (t_is_wine() || !s || n < 100) { t_skip("no genuine win32u stubs (%u)", n); return; }
    struct ms_stub *c = (struct ms_stub *)malloc(n * sizeof *c); REQUIRE(c);
    memcpy(c, s, n * sizeof *c);
    qsort(c, n, sizeof *c, ms_stub_cmp);
    CHECK(c[0].number == 0x1000);
    int gaps = 0;
    for (unsigned i = 1; i < n; i++) if (c[i].number != c[i - 1].number + 1) gaps++;
    CHECKF(gaps == 0, "%d gaps/duplicates in %u stub numbers", gaps, n);
    MS_PE *pe = ms_pe("win32k.sys");
    DWORD lim = 0;
    if (pe && pe_read_u32_at_export(pe, "W32pServiceLimit", &lim)) CHECKF(n == lim, "%u stubs, W32pServiceLimit %lu", n, (unsigned long)lim);
    free(c);
}

TEST(win32k_modules_sysinfo_pe_win32u_every_stub_has_win32k_thunk_export) {
    unsigned n; struct ms_stub *s = ms_stubs(&n);
    if (t_is_wine() || !s || n < 100) { t_skip("no genuine win32u stubs"); return; }
    MS_NEED_PE(pe, "win32k.sys");
    int missing = 0;
    for (unsigned i = 0; i < n; i++) {
        char nm[128]; snprintf(nm, sizeof nm, "__win32kstub_%s", s[i].name);
        if (!ms_pe_export(pe, nm, NULL)) { missing++; if (missing < 6) t_note("no win32k.sys export %s", nm); }
    }
    CHECKF(missing == 0, "%d stubs lack a __win32kstub_ export", missing);
}

TEST(win32k_modules_sysinfo_pe_win32u_stub_services_implemented_by_base_or_full_or_win32k) {
    unsigned n; struct ms_stub *s = ms_stubs(&n);
    if (t_is_wine() || !s || n < 100) { t_skip("no genuine win32u stubs"); return; }
    MS_NEED_PE(pb, "win32kbase.sys"); MS_NEED_PE(pf, "win32kfull.sys");
    unsigned in_base = 0, in_full = 0, neither = 0;
    for (unsigned i = 0; i < n; i++) {
        int b = ms_pe_export(pb, s[i].name, NULL) != 0, f = ms_pe_export(pf, s[i].name, NULL) != 0;
        in_base += b; in_full += f; neither += !(b || f);
    }
    t_note("%u services: %u exported by win32kbase, %u by win32kfull, %u only reachable through win32k.sys thunks", n, in_base, in_full, neither);
    CHECK(in_base + in_full > n / 2);
    CHECK(in_base > 50 && in_full > 300);
}

static const struct { const char *name; unsigned char bytes; } MS_ARGB[] = {
    { "NtUserGetThreadState", 0 }, { "NtUserCallOneParam", 0 }, { "NtUserCallTwoParam", 0 }, { "NtUserGetKeyState", 0 },
    { "NtGdiFlush", 0 }, { "NtUserGetForegroundWindow", 0 }, { "NtUserDestroyWindow", 0 }, { "NtUserShowWindow", 0 },
    { "NtGdiCreateCompatibleDC", 0 }, { "NtUserSetParent", 0 }, { "NtUserGetMessage", 0 },
    { "NtUserMoveWindow", 8 }, { "NtUserSetWindowPos", 12 }, { "NtUserMessageCall", 12 },
    { "NtGdiExtTextOutW", 20 }, { "NtGdiBitBlt", 28 }, { "NtGdiStretchBlt", 32 }, { "NtUserCreateWindowEx", 52 },
    { "NtUserPeekMessage", 8 },
};
TEST(win32k_modules_sysinfo_pe_win32k_argument_bytes_match_known_signatures) {
    MS_NEED_PE(pe, "win32k.sys");
    if (t_is_wine()) { t_skip("stub numbers are Wine's own"); return; }
    DWORD arva = ms_pe_export(pe, "W32pArgumentTable", NULL); REQUIRE(arva);
    size_t o = ms_pe_off(pe, arva); REQUIRE(o != (size_t)-1);
    for (size_t i = 0; i < sizeof MS_ARGB / sizeof *MS_ARGB; i++) {
        FARPROC p = t_proc_in("win32u.dll", MS_ARGB[i].name);
        if (!p || !k_stub_valid(p)) { t_note("%s: no stub", MS_ARGB[i].name); continue; }
        long idx = k_stub_number(p) - 0x1000;
        CHECKF(pe->base[o + idx] == MS_ARGB[i].bytes, "%s service %ld: table says %u want %u", MS_ARGB[i].name, idx + 0x1000, pe->base[o + idx], MS_ARGB[i].bytes);
    }
}

/* ---- version resources ----------------------------------------------------------------- */
static void pe_version(const char *leaf) {
    VS_FIXEDFILEINFO v, nt;
    MS_NEED_PE(pe, leaf);
    if (!ms_file_version(leaf, &v) || !ms_file_version("ntdll.dll", &nt)) { t_skip("version resources unreadable"); return; }
    /* The three win32k drivers carry file version 6.2.<build>.<revision> on Windows 10 (not 10.0 like ntdll). */
    CHECKF(HIWORD(v.dwFileVersionMS) == 6 && LOWORD(v.dwFileVersionMS) == 2, "%s version %u.%u", leaf, HIWORD(v.dwFileVersionMS), LOWORD(v.dwFileVersionMS));
    /* Revision (UBR) differs between the three drivers within one servicing level; the build is shared with ntdll. */
    CHECKF(HIWORD(v.dwFileVersionLS) == HIWORD(nt.dwFileVersionLS), "%s build %u ntdll build %u", leaf, HIWORD(v.dwFileVersionLS), HIWORD(nt.dwFileVersionLS));
    CHECK(v.dwFileOS == VOS_NT_WINDOWS32);
    t_note("%s FileType=%lu FileSubtype=%lu", leaf, (unsigned long)v.dwFileType, (unsigned long)v.dwFileSubtype);
    t_note("%s %u.%u.%u.%u", leaf, HIWORD(v.dwFileVersionMS), LOWORD(v.dwFileVersionMS), HIWORD(v.dwFileVersionLS), LOWORD(v.dwFileVersionLS));
}
MS_PE_TESTS(file_version_matches_ntdll_build, pe_version)

TEST(win32k_modules_sysinfo_pe_ntdll_build_equals_rtlgetversion) {
    VS_FIXEDFILEINFO nt;
    if (t_is_wine() || !ms_file_version("ntdll.dll", &nt)) { t_skip("no version resource"); return; }
    unsigned long build = 0, ubr = 0; k_win_build(&build, &ubr);
    /* the 19041..19045 releases share binaries whose file version stays 19041 while RtlGetVersion reports the release build */
    if (build >= 19041 && build <= 19045) CHECKF(HIWORD(nt.dwFileVersionLS) == 19041, "ntdll file build %u RtlGetVersion %lu", HIWORD(nt.dwFileVersionLS), build);
    else CHECKF(HIWORD(nt.dwFileVersionLS) == build, "ntdll file build %u RtlGetVersion %lu", HIWORD(nt.dwFileVersionLS), build);
    if (!k_is_19041()) t_note("build %lu: the corpus expectations are for 19041", build);
}

TEST(win32k_modules_sysinfo_pe_ntoskrnl_version_matches_ntdll) {
    VS_FIXEDFILEINFO k, nt;
    if (!ms_file_version("ntoskrnl.exe", &k) || !ms_file_version("ntdll.dll", &nt)) { t_skip("version resources unreadable"); return; }
    CHECK(k.dwFileVersionMS == nt.dwFileVersionMS);
    CHECK(HIWORD(k.dwFileVersionLS) == HIWORD(nt.dwFileVersionLS));
}

TEST(win32k_modules_sysinfo_pe_three_images_share_build) {
    VS_FIXEDFILEINFO a, b, c;
    if (!ms_file_version("win32k.sys", &a) || !ms_file_version("win32kbase.sys", &b) || !ms_file_version("win32kfull.sys", &c)) { t_skip("version resources unreadable"); return; }
    CHECK(a.dwFileVersionMS == b.dwFileVersionMS && b.dwFileVersionMS == c.dwFileVersionMS);
    CHECK(HIWORD(a.dwFileVersionLS) == HIWORD(b.dwFileVersionLS) && HIWORD(b.dwFileVersionLS) == HIWORD(c.dwFileVersionLS));
}

TEST(win32k_modules_sysinfo_pe_files_live_in_system32_not_drivers) {
    char dir[MAX_PATH], p[MAX_PATH];
    if (!GetSystemDirectoryA(dir, sizeof dir)) { t_skip("no system dir"); return; }
    for (int i = 0; i < 3; i++) {
        snprintf(p, sizeof p, "%s\\%s", dir, MS_LEAVES[i]);
        DWORD a = GetFileAttributesA(p);
        if (a == INVALID_FILE_ATTRIBUTES && t_is_wine()) { t_skip("not present under the emulator"); return; }
        CHECKF(a != INVALID_FILE_ATTRIBUTES, "%s", p);
        if (a != INVALID_FILE_ATTRIBUTES) CHECK(!(a & FILE_ATTRIBUTE_DIRECTORY));
    }
}

TEST(win32k_modules_sysinfo_pe_win32kfull_larger_than_base_larger_than_thunk_host) {
    MS_NEED_PE(a, "win32k.sys"); MS_NEED_PE(b, "win32kbase.sys"); MS_NEED_PE(c, "win32kfull.sys");
    CHECK(a->nt->OptionalHeader.SizeOfImage < b->nt->OptionalHeader.SizeOfImage);
    CHECK(b->nt->OptionalHeader.SizeOfImage < c->nt->OptionalHeader.SizeOfImage);
}
