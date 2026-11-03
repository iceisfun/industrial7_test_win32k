/* gdi_init_order: when a process whose only import is user32 gets its GDI client state, and how the
 * state relates across threads. The fixture w32k_gdiinit.exe imports kernel32 and user32 only, so gdi32 is
 * loaded purely through user32's dependency. It samples the state at fixed boundaries; this area runs it
 * and asserts the relations the fixture reports (never addresses). */
#include "t.h"
#include "k.h"
#include <stdio.h>
#include <string.h>

#define MAXL 24
struct fx { char line[MAXL][400]; int n; DWORD rc; int ran; };

static int fx_run(struct fx *f) {
    char self[MAX_PATH], path[MAX_PATH];
    snprintf(self, sizeof self, "%s", t_self_path());
    char *sl = strrchr(self, '\\'); if (!sl) return 0; sl[1] = 0;
    snprintf(path, sizeof path, "%sw32k_gdiinit.exe", self);
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) snprintf(path, sizeof path, "%stools\\w32k_gdiinit.exe", self);
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) return 0;
    HANDLE rd, wr; SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    if (!CreatePipe(&rd, &wr, &sa, 0)) return 0;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOA si; memset(&si, 0, sizeof si); si.cb = sizeof si; si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr; si.hStdError = wr;
    PROCESS_INFORMATION pi; char cmd[MAX_PATH + 4]; snprintf(cmd, sizeof cmd, "\"%s\"", path);
    BOOL ok = CreateProcessA(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
    CloseHandle(wr);
    if (!ok) { CloseHandle(rd); return 0; }
    char buf[8192]; DWORD tot = 0, got;
    while (tot < sizeof buf - 1 && ReadFile(rd, buf + tot, sizeof buf - 1 - tot, &got, NULL) && got) tot += got;
    buf[tot] = 0; CloseHandle(rd);
    if (WaitForSingleObject(pi.hProcess, 20000) != WAIT_OBJECT_0) TerminateProcess(pi.hProcess, 1);
    GetExitCodeProcess(pi.hProcess, &f->rc); CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    for (char *p = strtok(buf, "\r\n"); p && f->n < MAXL; p = strtok(NULL, "\r\n")) snprintf(f->line[f->n++], 400, "%s", p);
    f->ran = 1;
    return 1;
}
static const char *fx_stage(struct fx *f, const char *name) {
    char key[64]; snprintf(key, sizeof key, "STAGE %s ", name);
    for (int i = 0; i < f->n; i++) if (!strncmp(f->line[i], key, strlen(key))) return f->line[i] + strlen(key);
    return NULL;
}
static long fx_val(const char *s, const char *key) {
    char k[40]; snprintf(k, sizeof k, " %s=", key);
    char tmp[420]; snprintf(tmp, sizeof tmp, " %s", s);
    char *p = strstr(tmp, k); return p ? atol(p + strlen(k)) : -999;
}


/* The fixture is only meaningful if its import table really lacks gdi32: check the file itself. */
static int pe_imports(const char *path, const char *dll) {
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return -1;
    static unsigned char img[1 << 20]; DWORD got = 0; ReadFile(h, img, sizeof img, &got, NULL); CloseHandle(h);
    if (got < 0x400) return -1;
    IMAGE_DOS_HEADER *d = (IMAGE_DOS_HEADER *)img; IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(img + d->e_lfanew);
    DWORD rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    #define R2O(r) ({ DWORD o_ = 0; for (int i_ = 0; i_ < nt->FileHeader.NumberOfSections; i_++) if ((r) >= sec[i_].VirtualAddress && (r) < sec[i_].VirtualAddress + sec[i_].Misc.VirtualSize) o_ = (r) - sec[i_].VirtualAddress + sec[i_].PointerToRawData; o_; })
    DWORD off = R2O(rva); if (!off) return -1;
    int found = 0;
    for (IMAGE_IMPORT_DESCRIPTOR *id = (IMAGE_IMPORT_DESCRIPTOR *)(img + off); id->Name; id++) {
        DWORD no = R2O(id->Name); if (!no || no >= got) return -1;
        if (!_stricmp((char *)img + no, dll)) found = 1;
    }
    return found;
}
static const char *fx_path(char *path, size_t n) {
    char self[MAX_PATH]; snprintf(self, sizeof self, "%s", t_self_path());
    char *sl = strrchr(self, '\\'); if (!sl) return NULL; sl[1] = 0;
    snprintf(path, n, "%sw32k_gdiinit.exe", self);
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) snprintf(path, n, "%stools\\w32k_gdiinit.exe", self);
    return GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES ? NULL : path;
}

static struct fx G; static int G_state; /* 0 not run, 1 ran, -1 fixture unavailable */
static struct fx *fx_get(void) {
    if (!G_state) G_state = fx_run(&G) ? 1 : -1;
    return G_state > 0 ? &G : NULL;
}
#define NEED_FX() struct fx *f = fx_get(); if (!f) { t_skip("fixture executable not available next to the test"); return; } \
    if (f->rc != 0 || !fx_stage(f, "after_cleanup")) { CHECKF(0, "fixture did not finish (rc=%lu)", (unsigned long)f->rc); return; }
#define ST(var, name) const char *var = fx_stage(f, name); if (!var) { CHECKF(0, "missing stage %s", name); return; }

TEST(gdi_init_order_fixture_imports_user32_not_gdi32) {
    char path[MAX_PATH];
    if (!fx_path(path, sizeof path)) { t_skip("fixture executable not available next to the test"); return; }
    CHECK(pe_imports(path, "user32.dll") == 1);
    CHECK(pe_imports(path, "kernel32.dll") == 1);
    CHECK(pe_imports(path, "gdi32.dll") == 0);
    CHECK(pe_imports(path, "gdi32full.dll") == 0);
}

/* The process-wide GDI connection exists before the image's entry point: loading user32 (the only
 * explicit dependency) is what loads gdi32/gdi32full and connects the process; no GDI call is needed. */
TEST(gdi_init_order_connected_at_entry_by_user32_load) {
    NEED_FX();
    ST(e, "entry");
    CHECK(fx_val(e, "gdi32") == 1);
    CHECK(fx_val(e, "gdi32full") == 1);
    CHECK(fx_val(e, "tab") == 1);
    CHECK(fx_val(e, "kcb") == 1);
    CHECK(fx_val(e, "w32t") == 1);         /* the initial thread already has its win32 thread */
    CHECK(fx_val(e, "dca") == 0x14);
    CHECK(fx_val(e, "gdi") == 0);          /* connected, but nothing created yet */
    t_note("process connected before the entry point; initial thread already has win32 state; no GDI objects yet");
}

TEST(gdi_init_order_objects_on_initial_thread) {
    NEED_FX();
    ST(e, "entry"); ST(m, "main_objects");
    int dc = 0, bm = 0; for (int i = 0; i < f->n; i++) if (!strncmp(f->line[i], "OBJ ", 4)) sscanf(f->line[i], "OBJ dc=%d bm=%d", &dc, &bm);
    CHECK(dc == 1); CHECK(bm == 1);
    CHECK(fx_val(m, "gdi") > fx_val(e, "gdi"));
    CHECK(fx_val(m, "tab") == 1); CHECK(fx_val(m, "same_tab") == 1);
    CHECK(fx_val(m, "w32t") == 1);
    t_info("gdi objects entry=%ld main=%ld", fx_val(e, "gdi"), fx_val(m, "gdi"));
}

TEST(gdi_init_order_worker_thread_shares_table_but_starts_unconverted) {
    NEED_FX();
    ST(w0, "worker_start"); ST(w1, "worker_objects"); ST(m, "main_objects");
    /* process-wide state is the same on every thread ... */
    CHECK(fx_val(w0, "tab") == 1); CHECK(fx_val(w0, "same_tab") == 1);
    CHECK(fx_val(w0, "kcb") == 1); CHECK(fx_val(w0, "same_kcb") == 1);
    CHECK(fx_val(w0, "dca") == 0x14);
    /* ... the object count is process-wide and still includes the initial thread's objects ... */
    CHECK(fx_val(w0, "gdi") == fx_val(m, "gdi"));
    /* ... but a new thread has no win32 thread until its first win32k service, which converts it */
    CHECK(fx_val(w0, "w32t") == 0);
    CHECK(fx_val(w1, "w32t") == 1);
    CHECK(fx_val(w1, "same_tab") == 1);
    CHECK(fx_val(w1, "gdi") > fx_val(w0, "gdi"));
    t_info("gdi objects main=%ld worker_start=%ld worker_objects=%ld", fx_val(m, "gdi"), fx_val(w0, "gdi"), fx_val(w1, "gdi"));
}

TEST(gdi_init_order_after_join_state_unchanged_and_initial_objects_survive) {
    NEED_FX();
    ST(m, "main_objects"); ST(j, "after_join"); ST(c, "after_cleanup");
    int join = 0, wdc = 0, wbm = 0;
    for (int i = 0; i < f->n; i++) {
        if (!strncmp(f->line[i], "JOIN ", 5)) sscanf(f->line[i], "JOIN %d", &join);
        if (!strncmp(f->line[i], "WORKER ", 7)) sscanf(f->line[i], "WORKER dc=%d bm=%d", &wdc, &wbm);
    }
    CHECK(join == 1); CHECK(wdc == 1); CHECK(wbm == 1);
    CHECK(fx_val(j, "tab") == 1); CHECK(fx_val(j, "same_tab") == 1);
    CHECK(fx_val(j, "kcb") == 1); CHECK(fx_val(j, "same_kcb") == 1);
    CHECK(fx_val(j, "w32t") == 1);
    CHECK(fx_val(j, "dca") == 0x14);
    /* the objects the initial thread created are still counted after the worker is gone */
    CHECK(fx_val(j, "gdi") >= fx_val(m, "gdi"));
    CHECK(fx_val(c, "gdi") <= fx_val(j, "gdi"));
    CHECK(fx_val(c, "same_tab") == 1);
    t_info("gdi objects join=%ld cleanup=%ld", fx_val(j, "gdi"), fx_val(c, "gdi"));
}
