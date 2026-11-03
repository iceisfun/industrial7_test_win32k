/* Shared helpers for the win32k_modules_sysinfo area: thin ntdll wrappers, a cached
 * SystemModuleInformation snapshot, an on-disk PE reader and a few KUSER_SHARED_DATA accessors.
 * Everything is static so each translation unit carries its own copy (the Makefile compiles
 * every .c of the area into one exe). */
#ifndef MS_H
#define MS_H
#include "t.h"
#include "k.h"
#include <stdlib.h>

/* ---- NTSTATUS values used in this area (ntstatus.h names, kept private to avoid clashes) ---- */
#define ST_SUCCESS               ((LONG)0x00000000)
#define ST_MORE_ENTRIES          ((LONG)0x00000105)
#define ST_NO_MORE_ENTRIES       ((LONG)0x8000001A)
#define ST_BUFFER_OVERFLOW       ((LONG)0x80000005)
#define ST_INVALID_INFO_CLASS    ((LONG)0xC0000003)
#define ST_INFO_LENGTH_MISMATCH  ((LONG)0xC0000004)
#define ST_INVALID_HANDLE        ((LONG)0xC0000008)
#define ST_INVALID_PARAMETER     ((LONG)0xC000000D)
#define ST_ACCESS_DENIED         ((LONG)0xC0000022)
#define ST_BUFFER_TOO_SMALL      ((LONG)0xC0000023)
#define ST_OBJECT_TYPE_MISMATCH  ((LONG)0xC0000024)
#define ST_OBJECT_NAME_NOT_FOUND ((LONG)0xC0000034)
#define ST_OBJECT_PATH_NOT_FOUND ((LONG)0xC000003A)
#define ST_INVALID_SYSTEM_SERVICE ((LONG)0xC000001C)

#define MS_KERNEL_START 0xFFFF800000000000ULL

/* ---- ntdll wrappers --------------------------------------------------------------------- */
typedef LONG (NTAPI *ms_qsi_fn)(ULONG, PVOID, ULONG, PULONG);
typedef LONG (NTAPI *ms_qo_fn)(HANDLE, ULONG, PVOID, ULONG, PULONG);
typedef LONG (NTAPI *ms_open_dir_fn)(PHANDLE, ULONG, PVOID);
typedef LONG (NTAPI *ms_query_dir_fn)(HANDLE, PVOID, ULONG, BOOLEAN, BOOLEAN, PULONG, PULONG);
typedef LONG (NTAPI *ms_close_fn)(HANDLE);
typedef LONG (NTAPI *ms_qip_fn)(HANDLE, ULONG, PVOID, ULONG, PULONG);

static ms_qsi_fn ms_qsi_p(void)  { return (ms_qsi_fn)t_proc_in("ntdll.dll", "NtQuerySystemInformation"); }
static ms_qo_fn  ms_qo_p(void)   { return (ms_qo_fn)t_proc_in("ntdll.dll", "NtQueryObject"); }
static ms_open_dir_fn ms_od_p(void) { return (ms_open_dir_fn)t_proc_in("ntdll.dll", "NtOpenDirectoryObject"); }
static ms_query_dir_fn ms_qd_p(void) { return (ms_query_dir_fn)t_proc_in("ntdll.dll", "NtQueryDirectoryObject"); }
static ms_close_fn ms_close_p(void) { return (ms_close_fn)t_proc_in("ntdll.dll", "NtClose"); }
static ms_qip_fn ms_qip_p(void)  { return (ms_qip_fn)t_proc_in("ntdll.dll", "NtQueryInformationProcess"); }

static LONG ms_qsi(ULONG cls, void *buf, ULONG len, ULONG *ret) {
    ms_qsi_fn f = ms_qsi_p(); return f ? f(cls, buf, len, ret) : (LONG)0xC0000002;
}
static LONG ms_qo(HANDLE h, ULONG cls, void *buf, ULONG len, ULONG *ret) {
    ms_qo_fn f = ms_qo_p(); return f ? f(h, cls, buf, len, ret) : (LONG)0xC0000002;
}
static LONG ms_qip(HANDLE h, ULONG cls, void *buf, ULONG len, ULONG *ret) {
    ms_qip_fn f = ms_qip_p(); return f ? f(h, cls, buf, len, ret) : (LONG)0xC0000002;
}
static void ms_nt_close(HANDLE h) { ms_close_fn f = ms_close_p(); if (f && h) f(h); }

/* ---- SystemModuleInformation (class 11) ------------------------------------------------ */
#define MS_MOD_SIZE 0x128
typedef struct {
    PVOID Section, MappedBase, ImageBase;
    ULONG ImageSize, Flags;
    USHORT LoadOrderIndex, InitOrderIndex, LoadCount, OffsetToFileName;
    UCHAR FullPathName[256];
} MS_MOD;
typedef struct { ULONG NumberOfModules; ULONG pad; MS_MOD Modules[1]; } MS_MODS;

/* Cached snapshot (the module list of the booted system does not change while a test run lasts).
 * NULL when the query fails. */
static MS_MODS *ms_mods(void) {
    static MS_MODS *cache; static int tried;
    if (tried) return cache;
    tried = 1;
    ULONG size = 0x40000;
    for (int i = 0; i < 8; i++) {
        MS_MODS *m = (MS_MODS *)calloc(1, size);
        if (!m) return NULL;
        ULONG ret = 0;
        LONG st = ms_qsi(11, m, size, &ret);
        if (st == ST_SUCCESS) { cache = m; return m; }
        free(m);
        if (st != ST_INFO_LENGTH_MISMATCH) return NULL;
        size = ret + 0x4000;
    }
    return NULL;
}
static const char *ms_mod_name(const MS_MOD *m) { return (const char *)m->FullPathName + m->OffsetToFileName; }
static int ms_find_mod(const char *leaf) {
    MS_MODS *m = ms_mods(); if (!m) return -1;
    for (ULONG i = 0; i < m->NumberOfModules; i++)
        if (!_stricmp(ms_mod_name(&m->Modules[i]), leaf)) return (int)i;
    return -1;
}

/* Under Wine the kernel modules do not exist; on Windows their absence is a real failure. */
#define MS_NEED_MOD(var, leaf) \
    int var = ms_find_mod(leaf); \
    if (var < 0) { \
        if (t_is_wine() || !ms_mods()) { t_skip("module %s not listed (emulator or query unavailable)", leaf); return; } \
        CHECKF(0, "module %s missing from SystemModuleInformation", leaf); return; }

/* ---- on-disk PE reader --------------------------------------------------------------- */
typedef struct {
    unsigned char *base; size_t size; int ok;
    const IMAGE_NT_HEADERS64 *nt; const IMAGE_SECTION_HEADER *sec; int nsec;
    char path[MAX_PATH];
} MS_PE;

static int ms_read_file(const char *path, unsigned char **out, size_t *size) {
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    LARGE_INTEGER li; if (!GetFileSizeEx(h, &li) || li.QuadPart <= 0 || li.QuadPart > (64LL << 20)) { CloseHandle(h); return 0; }
    unsigned char *buf = (unsigned char *)malloc((size_t)li.QuadPart);
    size_t got = 0;
    while (buf && got < (size_t)li.QuadPart) {
        DWORD n = 0;
        if (!ReadFile(h, buf + got, (DWORD)((size_t)li.QuadPart - got > 0x100000 ? 0x100000 : (size_t)li.QuadPart - got), &n, NULL) || !n) break;
        got += n;
    }
    CloseHandle(h);
    if (!buf || got != (size_t)li.QuadPart) { free(buf); return 0; }
    *out = buf; *size = got; return 1;
}

/* Loads <System32>\leaf and validates the DOS/NT signatures. Cached per leaf. Returns NULL if the
 * file is absent or not a PE64 image. */
static MS_PE *ms_pe(const char *leaf) {
    static MS_PE cache[8]; static int n;
    for (int i = 0; i < n; i++) if (!_stricmp(cache[i].path + strlen(cache[i].path) - strlen(leaf), leaf) && cache[i].ok) return &cache[i];
    if (n >= 8) return NULL;
    MS_PE *pe = &cache[n];
    char dir[MAX_PATH]; UINT l = GetSystemDirectoryA(dir, sizeof dir);
    if (!l || l >= sizeof dir) return NULL;
    snprintf(pe->path, sizeof pe->path, "%s\\%s", dir, leaf);
    if (!ms_read_file(pe->path, &pe->base, &pe->size)) return NULL;
    if (pe->size < sizeof(IMAGE_DOS_HEADER)) return NULL;
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)pe->base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return NULL;
    if (dos->e_lfanew < 0 || (size_t)dos->e_lfanew + sizeof(IMAGE_NT_HEADERS64) > pe->size) return NULL;
    pe->nt = (const IMAGE_NT_HEADERS64 *)(pe->base + dos->e_lfanew);
    if (pe->nt->Signature != IMAGE_NT_SIGNATURE) return NULL;
    pe->nsec = pe->nt->FileHeader.NumberOfSections;
    pe->sec = (const IMAGE_SECTION_HEADER *)((const unsigned char *)&pe->nt->OptionalHeader + pe->nt->FileHeader.SizeOfOptionalHeader);
    if ((const unsigned char *)(pe->sec + pe->nsec) > pe->base + pe->size) return NULL;
    pe->ok = 1; n++;
    return pe;
}
/* RVA -> file offset, or (size_t)-1 */
static size_t ms_pe_off(const MS_PE *pe, DWORD rva) {
    if (rva < pe->nt->OptionalHeader.SizeOfHeaders) return rva;
    for (int i = 0; i < pe->nsec; i++) {
        DWORD va = pe->sec[i].VirtualAddress, vs = pe->sec[i].Misc.VirtualSize, rs = pe->sec[i].SizeOfRawData;
        if (!vs) vs = rs;
        if (rva >= va && rva < va + vs) {
            DWORD d = rva - va;
            if (d >= rs) return (size_t)-1;
            size_t off = pe->sec[i].PointerToRawData + d;
            return off < pe->size ? off : (size_t)-1;
        }
    }
    return (size_t)-1;
}
static const IMAGE_SECTION_HEADER *ms_pe_sec_of(const MS_PE *pe, DWORD rva) {
    for (int i = 0; i < pe->nsec; i++) {
        DWORD va = pe->sec[i].VirtualAddress, vs = pe->sec[i].Misc.VirtualSize;
        if (!vs) vs = pe->sec[i].SizeOfRawData;
        if (rva >= va && rva < va + vs) return &pe->sec[i];
    }
    return NULL;
}
static const char *ms_pe_str(const MS_PE *pe, DWORD rva) {
    size_t o = ms_pe_off(pe, rva); if (o == (size_t)-1) return NULL;
    size_t e = o; while (e < pe->size && pe->base[e]) e++;
    return e < pe->size ? (const char *)pe->base + o : NULL;
}
static const IMAGE_DATA_DIRECTORY *ms_pe_dir(const MS_PE *pe, int idx) {
    if ((DWORD)idx >= pe->nt->OptionalHeader.NumberOfRvaAndSizes) return NULL;
    const IMAGE_DATA_DIRECTORY *d = &pe->nt->OptionalHeader.DataDirectory[idx];
    return d->VirtualAddress ? d : NULL;
}
static const IMAGE_EXPORT_DIRECTORY *ms_pe_exports(const MS_PE *pe) {
    const IMAGE_DATA_DIRECTORY *d = ms_pe_dir(pe, IMAGE_DIRECTORY_ENTRY_EXPORT); if (!d) return NULL;
    size_t o = ms_pe_off(pe, d->VirtualAddress);
    if (o == (size_t)-1 || o + sizeof(IMAGE_EXPORT_DIRECTORY) > pe->size) return NULL;
    return (const IMAGE_EXPORT_DIRECTORY *)(pe->base + o);
}
static const DWORD *ms_pe_dwords(const MS_PE *pe, DWORD rva, DWORD count) {
    size_t o = ms_pe_off(pe, rva);
    if (o == (size_t)-1 || o + (size_t)count * 4 > pe->size) return NULL;
    return (const DWORD *)(pe->base + o);
}
/* RVA of the named export, 0 when absent. *forwarded is set when the RVA lies inside the export directory. */
static DWORD ms_pe_export(const MS_PE *pe, const char *name, int *forwarded) {
    const IMAGE_EXPORT_DIRECTORY *ex = ms_pe_exports(pe); if (!ex) return 0;
    const DWORD *names = ms_pe_dwords(pe, ex->AddressOfNames, ex->NumberOfNames);
    const DWORD *funcs = ms_pe_dwords(pe, ex->AddressOfFunctions, ex->NumberOfFunctions);
    size_t oo = ms_pe_off(pe, ex->AddressOfNameOrdinals);
    if (!names || !funcs || oo == (size_t)-1) return 0;
    const WORD *ords = (const WORD *)(pe->base + oo);
    for (DWORD i = 0; i < ex->NumberOfNames; i++) {
        const char *s = ms_pe_str(pe, names[i]);
        if (s && !strcmp(s, name)) {
            if (ords[i] >= ex->NumberOfFunctions) return 0;
            DWORD r = funcs[ords[i]];
            const IMAGE_DATA_DIRECTORY *d = ms_pe_dir(pe, IMAGE_DIRECTORY_ENTRY_EXPORT);
            if (forwarded) *forwarded = (d && r >= d->VirtualAddress && r < d->VirtualAddress + d->Size);
            return r;
        }
    }
    return 0;
}
/* Number of import descriptors naming `dll` (case-insensitive) and total thunks through it. */
static int ms_pe_imports_dll(const MS_PE *pe, const char *dll, unsigned *thunks) {
    const IMAGE_DATA_DIRECTORY *d = ms_pe_dir(pe, IMAGE_DIRECTORY_ENTRY_IMPORT); if (!d) return 0;
    size_t o = ms_pe_off(pe, d->VirtualAddress); if (o == (size_t)-1) return 0;
    int found = 0; if (thunks) *thunks = 0;
    for (const IMAGE_IMPORT_DESCRIPTOR *id = (const IMAGE_IMPORT_DESCRIPTOR *)(pe->base + o);
         (const unsigned char *)(id + 1) <= pe->base + pe->size && id->Name; id++) {
        const char *n = ms_pe_str(pe, id->Name);
        if (n && !_stricmp(n, dll)) {
            found++;
            DWORD t = id->OriginalFirstThunk ? id->OriginalFirstThunk : id->FirstThunk;
            size_t to = ms_pe_off(pe, t);
            if (to != (size_t)-1 && thunks)
                for (const ULONGLONG *q = (const ULONGLONG *)(pe->base + to); (const unsigned char *)(q + 1) <= pe->base + pe->size && *q; q++) (*thunks)++;
        }
    }
    return found;
}

/* ---- file version (version.dll is not in the module list; resolved explicitly) ---------- */
typedef DWORD (WINAPI *ms_gfvis_fn)(LPCWSTR, LPDWORD);
typedef BOOL  (WINAPI *ms_gfvi_fn)(LPCWSTR, DWORD, DWORD, LPVOID);
typedef BOOL  (WINAPI *ms_vqv_fn)(LPCVOID, LPCWSTR, LPVOID *, PUINT);
/* Fills the fixed file info of a System32 file. Returns 0 when unavailable. */
static int ms_file_version(const char *leaf, VS_FIXEDFILEINFO *out) {
    ms_gfvis_fn sz = (ms_gfvis_fn)t_proc_in("version.dll", "GetFileVersionInfoSizeW");
    ms_gfvi_fn gi = (ms_gfvi_fn)t_proc_in("version.dll", "GetFileVersionInfoW");
    ms_vqv_fn vq = (ms_vqv_fn)t_proc_in("version.dll", "VerQueryValueW");
    if (!sz || !gi || !vq) return 0;
    WCHAR dir[MAX_PATH], path[MAX_PATH], wl[64];
    if (!GetSystemDirectoryW(dir, MAX_PATH)) return 0;
    MultiByteToWideChar(CP_ACP, 0, leaf, -1, wl, 64);
    _snwprintf(path, MAX_PATH, L"%ls\\%ls", dir, wl); path[MAX_PATH - 1] = 0;
    DWORD h = 0, n = sz(path, &h); if (!n) return 0;
    void *buf = malloc(n); if (!buf) return 0;
    VS_FIXEDFILEINFO *fi = NULL; UINT fl = 0; int ok = 0;
    if (gi(path, 0, n, buf) && vq(buf, L"\\", (LPVOID *)&fi, &fl) && fi && fl >= sizeof *fi) { *out = *fi; ok = 1; }
    free(buf);
    return ok;
}

/* ---- KUSER_SHARED_DATA ---------------------------------------------------------------- */
#define MS_KUSER 0x7FFE0000ULL
static int ms_ku_u32(unsigned off, unsigned long *v) { return k_try_read_u32((void *)(ULONG_PTR)(MS_KUSER + off), v); }
static int ms_ku_u16(unsigned off, unsigned short *v) { return k_try_read_u16((void *)(ULONG_PTR)(MS_KUSER + off), v); }
static int ms_ku_u8(unsigned off, unsigned char *v) { return k_try_read_u8((void *)(ULONG_PTR)(MS_KUSER + off), v); }
static int ms_ku_u64(unsigned off, unsigned long long *v) { return k_try_read((void *)(ULONG_PTR)(MS_KUSER + off), v, 8); }
/* KSYSTEM_TIME stable read (High1 == High2 handshake, as the kernel publishes it) */
static unsigned long long ms_ku_ktime(unsigned off) {
    for (int i = 0; i < 1000; i++) {
        unsigned long lo = 0, h1 = 0, h2 = 0;
        if (!ms_ku_u32(off + 4, &h1) || !ms_ku_u32(off, &lo) || !ms_ku_u32(off + 8, &h2)) return 0;
        if (h1 == h2) return ((unsigned long long)h1 << 32) | lo;
    }
    return 0;
}

static int ms_ci_eq(const char *a, const char *b) { return !_stricmp(a, b); }
static void ms_wide_to_narrow(const WCHAR *w, char *out, size_t cap) {
    size_t i = 0; for (; w[i] && i + 1 < cap; i++) out[i] = (char)w[i]; out[i] = 0;
}
#endif
