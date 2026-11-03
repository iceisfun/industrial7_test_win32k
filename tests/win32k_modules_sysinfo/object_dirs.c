/* win32k_modules_sysinfo: the object-manager namespace the Win32 stack lives in, read through
 * NtOpenDirectoryObject / NtQueryDirectoryObject: \Windows\WindowStations, \Sessions\<n>\...,
 * \ObjectTypes and the per-session \BaseNamedObjects.
 *
 * Kernel facts (ntoskrnl NtQueryDirectoryObject, 19041 analysis):
 *   - the handle is resolved with DIRECTORY_QUERY (1) against the Directory type: invalid handle ->
 *     STATUS_INVALID_HANDLE, other type -> STATUS_OBJECT_TYPE_MISMATCH, missing right -> STATUS_ACCESS_DENIED;
 *   - the result is an array of 0x20-byte records {UNICODE_STRING Name; UNICODE_STRING TypeName} ended by a
 *     zeroed record, the strings follow in the same buffer; ReturnLength covers records plus strings;
 *   - Context is the index of the next entry; with RestartScan FALSE it is read and written back;
 *   - a buffer too small for the first record gives STATUS_BUFFER_TOO_SMALL when ReturnSingleEntry is TRUE
 *     and STATUS_MORE_ENTRIES (0x105) otherwise; nothing left gives STATUS_NO_MORE_ENTRIES.
 */
#include "ms.h"

#define DIR_QUERY    0x0001
#define DIR_TRAVERSE 0x0002

typedef struct { char name[128]; char type[64]; } MS_DE;

static LONG ms_open_dir_w(const WCHAR *path, ULONG access, HANDLE *h, ULONG attr) {
    ms_open_dir_fn od = ms_od_p(); if (!od) return (LONG)0xC0000002;
    UNICODE_STRING us; us.Buffer = (PWSTR)path; us.Length = (USHORT)(lstrlenW(path) * 2); us.MaximumLength = us.Length + 2;
    OBJECT_ATTRIBUTES oa; memset(&oa, 0, sizeof oa);
    oa.Length = sizeof oa; oa.ObjectName = &us; oa.Attributes = attr;
    *h = NULL;
    return od(h, access, &oa);
}
static LONG ms_open_dir(const char *path, ULONG access, HANDLE *h) {
    WCHAR w[260]; MultiByteToWideChar(CP_ACP, 0, path, -1, w, 260);
    return ms_open_dir_w(w, access, h, 0x40);   /* OBJ_CASE_INSENSITIVE */
}
static LONG ms_qd(HANDLE h, void *buf, ULONG len, int single, int restart, ULONG *ctx, ULONG *ret) {
    ms_query_dir_fn f = ms_qd_p(); return f ? f(h, buf, len, (BOOLEAN)single, (BOOLEAN)restart, ctx, ret) : (LONG)0xC0000002;
}

static void ms_parse_dir_buf(const unsigned char *buf, MS_DE *out, int cap, int *n) {
    for (int i = 0;; i++) {
        const unsigned char *e = buf + 0x20 * i;
        USHORT nl = *(const USHORT *)e, tl = *(const USHORT *)(e + 0x10);
        const WCHAR *nb = *(WCHAR * const *)(e + 8), *tb = *(WCHAR * const *)(e + 0x18);
        if (!nl && !tl && !nb && !tb) break;
        if (*n < cap) {
            WCHAR w[128] = {0};
            if (nl / 2 < 128 && k_try_read(nb, w, nl)) ms_wide_to_narrow(w, out[*n].name, sizeof out[*n].name); else out[*n].name[0] = 0;
            memset(w, 0, sizeof w);
            if (tl / 2 < 128 && k_try_read(tb, w, tl)) ms_wide_to_narrow(w, out[*n].type, sizeof out[*n].type); else out[*n].type[0] = 0;
            (*n)++;
        }
        if (i > 5000) break;
    }
}
/* Whole listing via repeated multi-entry queries. Returns the open status; *n entries stored. */
static LONG ms_dir_list(const char *path, MS_DE *out, int cap, int *n) {
    HANDLE h; *n = 0;
    LONG st = ms_open_dir(path, DIR_QUERY | DIR_TRAVERSE, &h);
    if (st != ST_SUCCESS) return st;
    unsigned char *buf = (unsigned char *)calloc(1, 0x10000);
    ULONG ctx = 0, ret = 0; int first = 1;
    for (int it = 0; it < 64 && buf; it++) {
        memset(buf, 0, 0x10000);
        LONG q = ms_qd(h, buf, 0x10000, 0, first, &ctx, &ret);
        first = 0;
        if (q != ST_SUCCESS && q != ST_MORE_ENTRIES) break;
        int before = *n;
        ms_parse_dir_buf(buf, out, cap, n);
        if (q == ST_SUCCESS || *n == before) break;
    }
    free(buf); ms_nt_close(h);
    return ST_SUCCESS;
}
static MS_DE *ms_de_find(MS_DE *v, int n, const char *name) {
    for (int i = 0; i < n; i++) if (!_stricmp(v[i].name, name)) return &v[i];
    return NULL;
}

#define MS_LIST(var, n, path, cap) \
    MS_DE *var = (MS_DE *)calloc(cap, sizeof(MS_DE)); int n = 0; \
    LONG var##_st = var ? ms_dir_list(path, var, cap, &n) : (LONG)0xC0000017; \
    if (var##_st != ST_SUCCESS) { \
        free(var); \
        if (t_is_wine()) { t_skip("namespace unavailable under the emulator"); return; } \
        if (var##_st == ST_ACCESS_DENIED) { t_skip("%s: access denied for this token", path); return; } \
        CHECKF(0, "open %s failed 0x%lx", path, (unsigned long)var##_st); return; }

/* \Windows\WindowStations of the caller's session: session 0 owns the root \Windows, every other
 * session its \Sessions\<n>\Windows (the explicit path is valid for any session >= 1). */
static void ms_ws_dir(char *out, size_t cap) {
    if (k_session_id() == 0) snprintf(out, cap, "\\Windows\\WindowStations");
    else snprintf(out, cap, "\\Sessions\\%lu\\Windows\\WindowStations", k_session_id());
}
static void ms_bno_path(char *out, size_t cap) {
    if (k_session_id() == 0) snprintf(out, cap, "\\BaseNamedObjects");
    else snprintf(out, cap, "\\Sessions\\%lu\\BaseNamedObjects", k_session_id());
}

/* ---- root namespace -------------------------------------------------------------------- */
TEST(win32k_modules_sysinfo_objdir_root_lists_core_directories) {
    MS_LIST(v, n, "\\", 1024);
    static const char *const want[] = { "Device", "Driver", "ObjectTypes", "KernelObjects", "Sessions", "Windows", "BaseNamedObjects", "KnownDlls" };
    for (size_t i = 0; i < sizeof want / sizeof *want; i++) {
        MS_DE *e = ms_de_find(v, n, want[i]);
        CHECKF(e != NULL, "\\%s missing", want[i]);
        if (e) CHECKF(!strcmp(e->type, "Directory"), "\\%s is a %s", want[i], e->type);
    }
    CHECK(n >= 10);
    free(v);
}
TEST(win32k_modules_sysinfo_objdir_root_entries_have_names_and_types) {
    MS_LIST(v, n, "\\", 1024);
    for (int i = 0; i < n; i++) { CHECK(v[i].name[0]); CHECK(v[i].type[0]); }
    free(v);
}
TEST(win32k_modules_sysinfo_objdir_root_entry_names_unique) {
    MS_LIST(v, n, "\\", 1024);
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) CHECKF(strcmp(v[i].name, v[j].name), "duplicate %s", v[i].name);
    free(v);
}

TEST(win32k_modules_sysinfo_objdir_objecttypes_matches_query_object_types) {
    MS_LIST(v, n, "\\ObjectTypes", 1024);
    ULONG size = 0x10000, ret = 0; unsigned char *b = (unsigned char *)calloc(1, size); REQUIRE(b);
    LONG st = ms_qo(NULL, 3, b, size, &ret);
    if (st != ST_SUCCESS) { free(b); free(v); t_skip("types unavailable"); return; }
    ULONG cnt = *(ULONG *)b, off = 8;
    for (ULONG i = 0; i < cnt; i++) {
        const unsigned char *r = b + off; USHORT len = *(const USHORT *)r;
        WCHAR w[64] = {0}; char nm[64];
        if (len / 2 < 64 && k_try_read(*(WCHAR * const *)(r + 8), w, len)) {
            ms_wide_to_narrow(w, nm, sizeof nm);
            MS_DE *e = ms_de_find(v, n, nm);
            CHECKF(e != NULL, "type %s not listed in \\ObjectTypes", nm);
            if (e) CHECK(!strcmp(e->type, "Type"));
        }
        off += 0x68 + ((len + 9) & ~7u);
    }
    for (int i = 0; i < n; i++) CHECKF(!strcmp(v[i].type, "Type"), "%s has type %s", v[i].name, v[i].type);
    free(b); free(v);
}
TEST(win32k_modules_sysinfo_objdir_objecttypes_lists_win32_types) {
    MS_LIST(v, n, "\\ObjectTypes", 1024);
    static const char *const want[] = { "WindowStation", "Desktop", "Composition", "RawInputManager", "CoreMessaging", "ActivationObject" };
    for (size_t i = 0; i < 6; i++) CHECKF(ms_de_find(v, n, want[i]) != NULL, "%s", want[i]);
    free(v);
}

/* ---- window stations --------------------------------------------------------------------- */
TEST(win32k_modules_sysinfo_objdir_windowstations_contains_winsta0) {
    char wd[96]; ms_ws_dir(wd, sizeof wd);
    MS_LIST(v, n, wd, 256);
    MS_DE *e = ms_de_find(v, n, "WinSta0");
    if (k_session_id() == 0 && !e) t_note("session 0 lists no WinSta0 (service stations only)"); else CHECK(e != NULL);
    if (e) CHECK(!strcmp(e->type, "WindowStation"));
    free(v);
}
TEST(win32k_modules_sysinfo_objdir_windowstations_only_windowstation_objects) {
    char wd[96]; ms_ws_dir(wd, sizeof wd);
    MS_LIST(v, n, wd, 256);
    for (int i = 0; i < n; i++) CHECKF(!strcmp(v[i].type, "WindowStation"), "%s is %s", v[i].name, v[i].type);
    t_note("%d window station(s) visible:", n);
    for (int i = 0; i < n && i < 8; i++) t_note("  %s", v[i].name);
    free(v);
}
TEST(win32k_modules_sysinfo_objdir_session_windowstations_by_explicit_path) {
    if (k_session_id() == 0) { t_skip("session 0 uses the root \\Windows"); return; }
    char p[96]; snprintf(p, sizeof p, "\\Sessions\\%lu\\Windows\\WindowStations", k_session_id());
    MS_LIST(v, n, p, 256);
    MS_DE *e = ms_de_find(v, n, "WinSta0");
    CHECK(e != NULL);
    if (e) CHECK(!strcmp(e->type, "WindowStation"));
    free(v);
}
TEST(win32k_modules_sysinfo_objdir_root_windows_redirects_to_session_note) {
    if (k_session_id() == 0) { t_skip("session 0"); return; }
    char p[96]; snprintf(p, sizeof p, "\\Sessions\\%lu\\Windows\\WindowStations", k_session_id());
    MS_LIST(a, na, "\\Windows\\WindowStations", 256);
    MS_LIST(b, nb, p, 256);
    CHECK(ms_de_find(b, nb, "WinSta0") != NULL);
    int same = (na == nb);
    for (int i = 0; same && i < na; i++) same = ms_de_find(b, nb, a[i].name) != NULL;
    t_note("\\Windows\\WindowStations %s \\Sessions\\%lu\\Windows\\WindowStations (%d vs %d entries)", same ? "mirrors" : "differs from", k_session_id(), na, nb);
    /* whether the root name is redirected per session is not pinned by an analysis: note only */
    free(a); free(b);
}
TEST(win32k_modules_sysinfo_objdir_windows_directory_has_windowstations_child) {
    MS_LIST(v, n, "\\Windows", 256);
    MS_DE *e = ms_de_find(v, n, "WindowStations");
    CHECK(e != NULL);
    if (e) CHECK(!strcmp(e->type, "Directory"));
    free(v);
}

TEST(win32k_modules_sysinfo_objdir_sessions_lists_own_session) {
    if (k_session_id() == 0) { t_skip("session 0 has no \\Sessions\\<n> entry guarantee"); return; }
    MS_LIST(v, n, "\\Sessions", 256);
    char nm[16]; snprintf(nm, sizeof nm, "%lu", k_session_id());
    MS_DE *e = ms_de_find(v, n, nm);
    CHECK(e != NULL);
    if (e) CHECK(!strcmp(e->type, "Directory"));
    for (int i = 0; i < n; i++) {
        /* every entry is a directory: numbered sessions plus the BNOLINKS helper directory */
        int numeric = v[i].name[0] != 0;
        for (const char *c = v[i].name; *c; c++) if (*c < '0' || *c > '9') numeric = 0;
        if (!numeric) t_note("non-numeric \\Sessions entry %s (%s)", v[i].name, v[i].type);
        else CHECK(!strcmp(v[i].type, "Directory"));
    }
    free(v);
}
TEST(win32k_modules_sysinfo_objdir_own_session_directory_children) {
    if (k_session_id() == 0) { t_skip("session 0"); return; }
    char p[64]; snprintf(p, sizeof p, "\\Sessions\\%lu", k_session_id());
    MS_LIST(v, n, p, 256);
    static const char *const want[] = { "BaseNamedObjects", "Windows" };
    for (int i = 0; i < 2; i++) {
        MS_DE *e = ms_de_find(v, n, want[i]);
        CHECKF(e != NULL, "%s\\%s missing", p, want[i]);
        if (e) CHECK(!strcmp(e->type, "Directory"));
    }
    free(v);
}

/* ---- BaseNamedObjects: user-mode created objects land in the session namespace ------------- */
static void bno_object_visible(const char *kind, HANDLE h, const char *suffix, const char *type) {
    char dir[96]; ms_bno_path(dir, sizeof dir);
    MS_LIST(v, n, dir, 4096);
    char nm[64]; snprintf(nm, sizeof nm, "win32kt%lu_%s", (unsigned long)GetCurrentProcessId(), suffix);
    MS_DE *e = ms_de_find(v, n, nm);
    CHECKF(e != NULL, "%s \"%s\" not found in %s", kind, nm, dir);
    if (e) CHECKF(!strcmp(e->type, type), "%s listed as %s", kind, e->type);
    free(v);
}
static void bno_make_name(WCHAR *w, size_t cap, const char *suffix) { _snwprintf(w, cap, L"Local\\win32kt%lu_%hs", (unsigned long)GetCurrentProcessId(), suffix); w[cap - 1] = 0; }

TEST(win32k_modules_sysinfo_objdir_bno_event_visible) {
    WCHAR w[96]; bno_make_name(w, 96, "ev");
    HANDLE h = CreateEventW(NULL, TRUE, FALSE, w); REQUIRE(h);
    bno_object_visible("event", h, "ev", "Event");
    CloseHandle(h);
}
TEST(win32k_modules_sysinfo_objdir_bno_mutex_visible) {
    WCHAR w[96]; bno_make_name(w, 96, "mx");
    HANDLE h = CreateMutexW(NULL, FALSE, w); REQUIRE(h);
    bno_object_visible("mutex", h, "mx", "Mutant");
    CloseHandle(h);
}
TEST(win32k_modules_sysinfo_objdir_bno_semaphore_visible) {
    WCHAR w[96]; bno_make_name(w, 96, "sm");
    HANDLE h = CreateSemaphoreW(NULL, 0, 4, w); REQUIRE(h);
    bno_object_visible("semaphore", h, "sm", "Semaphore");
    CloseHandle(h);
}
TEST(win32k_modules_sysinfo_objdir_bno_timer_visible) {
    WCHAR w[96]; bno_make_name(w, 96, "tm");
    HANDLE h = CreateWaitableTimerW(NULL, TRUE, w); REQUIRE(h);
    bno_object_visible("timer", h, "tm", "Timer");
    CloseHandle(h);
}
TEST(win32k_modules_sysinfo_objdir_bno_section_visible) {
    WCHAR w[96]; bno_make_name(w, 96, "sc");
    HANDLE h = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 0x1000, w); REQUIRE(h);
    bno_object_visible("section", h, "sc", "Section");
    CloseHandle(h);
}
TEST(win32k_modules_sysinfo_objdir_bno_job_visible) {
    WCHAR w[96]; bno_make_name(w, 96, "jb");
    HANDLE h = CreateJobObjectW(NULL, w); REQUIRE(h);
    bno_object_visible("job", h, "jb", "Job");
    CloseHandle(h);
}
TEST(win32k_modules_sysinfo_objdir_bno_object_disappears_after_close) {
    WCHAR w[96]; bno_make_name(w, 96, "gone");
    HANDLE h = CreateEventW(NULL, TRUE, FALSE, w); REQUIRE(h);
    char dir[96]; ms_bno_path(dir, sizeof dir);
    char nm[64]; snprintf(nm, sizeof nm, "win32kt%lu_gone", (unsigned long)GetCurrentProcessId());
    { MS_LIST(v, n, dir, 4096); CHECK(ms_de_find(v, n, nm) != NULL); free(v); }
    CloseHandle(h);
    /* the last handle is gone: a non-permanent named object is removed from its directory */
    { MS_LIST(v, n, dir, 4096); CHECK(ms_de_find(v, n, nm) == NULL); free(v); }
}
TEST(win32k_modules_sysinfo_objdir_bno_object_name_query_returns_full_path) {
    WCHAR w[96]; bno_make_name(w, 96, "nm");
    HANDLE h = CreateEventW(NULL, TRUE, FALSE, w); REQUIRE(h);
    unsigned char buf[0x300]; ULONG ret = 0;
    LONG st = ms_qo(h, 1, buf, sizeof buf, &ret);
    CHECK(st == ST_SUCCESS);
    if (st == ST_SUCCESS) {
        USHORT len = *(USHORT *)buf; WCHAR nw[256] = {0};
        if (len / 2 < 256 && k_try_read(*(WCHAR **)(buf + 8), nw, len)) {
            char nm[256]; ms_wide_to_narrow(nw, nm, sizeof nm);
            char want[160]; char dir[96]; ms_bno_path(dir, sizeof dir);
            snprintf(want, sizeof want, "%s\\win32kt%lu_nm", dir, (unsigned long)GetCurrentProcessId());
            CHECKF(!_stricmp(nm, want), "name \"%s\" want \"%s\"", nm, want);
            CHECK(ret >= 16u + len + 2u);
        }
    }
    CloseHandle(h);
}
TEST(win32k_modules_sysinfo_objdir_bno_name_query_short_buffer) {
    WCHAR w[96]; bno_make_name(w, 96, "nm2");
    HANDLE h = CreateEventW(NULL, TRUE, FALSE, w); REQUIRE(h);
    unsigned char buf[0x300]; ULONG ret = 0;
    LONG st = ms_qo(h, 1, buf, 8, &ret);
    CHECK(st == ST_INFO_LENGTH_MISMATCH);
    CHECK(ret > 16);
    CHECK(ms_qo(h, 1, buf, sizeof buf, &ret) == ST_SUCCESS);
    CloseHandle(h);
}
TEST(win32k_modules_sysinfo_objdir_bno_directory_is_a_directory_object) {
    char dir[96]; ms_bno_path(dir, sizeof dir);
    HANDLE h; LONG st = ms_open_dir(dir, DIR_QUERY, &h);
    if (st != ST_SUCCESS) { t_skip("open 0x%lx", (unsigned long)st); return; }
    char nm[64] = {0}; unsigned char buf[0x200]; ULONG ret;
    REQUIRE(ms_qo(h, 2, buf, sizeof buf, &ret) == ST_SUCCESS);
    WCHAR w[64] = {0}; USHORT len = *(USHORT *)buf;
    if (len / 2 < 64 && k_try_read(*(WCHAR **)(buf + 8), w, len)) { ms_wide_to_narrow(w, nm, sizeof nm); CHECK(!strcmp(nm, "Directory")); }
    ULONG b[14];
    REQUIRE(ms_qo(h, 0, b, 0x38, &ret) == ST_SUCCESS);
    CHECK((b[1] & DIR_QUERY) == DIR_QUERY);
    ms_nt_close(h);
}

/* ---- NtOpenDirectoryObject / NtQueryDirectoryObject error paths --------------------------- */
TEST(win32k_modules_sysinfo_objdir_open_nonexistent_leaf) {
    HANDLE h; LONG st = ms_open_dir("\\win32kt_no_such_dir", DIR_QUERY, &h);
    CHECKF(st == ST_OBJECT_NAME_NOT_FOUND, "status 0x%lx", (unsigned long)st);
    CHECK(h == NULL);
}
TEST(win32k_modules_sysinfo_objdir_open_nonexistent_parent) {
    HANDLE h; LONG st = ms_open_dir("\\win32kt_no_such_dir\\child", DIR_QUERY, &h);
    CHECKF(st == ST_OBJECT_PATH_NOT_FOUND, "status 0x%lx", (unsigned long)st);
}
TEST(win32k_modules_sysinfo_objdir_open_relative_name_without_root) {
    HANDLE h; LONG st = ms_open_dir_w(L"Windows", DIR_QUERY, &h, 0x40);
    CHECKF(st == (LONG)0xC000003B, "status 0x%lx", (unsigned long)st);   /* STATUS_OBJECT_PATH_SYNTAX_BAD */
}
TEST(win32k_modules_sysinfo_objdir_open_case_sensitivity) {
    HANDLE h = NULL; LONG st = ms_open_dir_w(L"\\windows", DIR_QUERY, &h, 0);
    /* Windows keeps the object manager case-insensitive even without OBJ_CASE_INSENSITIVE (ObpCaseInsensitive), so a
     * differently cased name is found either way */
    CHECKF(st == ST_SUCCESS, "plain lookup of a differently cased name: status 0x%lx", (unsigned long)st);
    if (st == ST_SUCCESS) ms_nt_close(h);
    st = ms_open_dir_w(L"\\windows", DIR_QUERY, &h, 0x40);
    if (st == ST_SUCCESS) ms_nt_close(h);
    else CHECKF(st == ST_ACCESS_DENIED, "case-insensitive lookup status 0x%lx", (unsigned long)st);
}
TEST(win32k_modules_sysinfo_objdir_open_odd_length_name) {
    ms_open_dir_fn od = ms_od_p(); REQUIRE(od);
    UNICODE_STRING us; us.Buffer = L"\\Windows"; us.Length = 15; us.MaximumLength = 18;
    OBJECT_ATTRIBUTES oa; memset(&oa, 0, sizeof oa); oa.Length = sizeof oa; oa.ObjectName = &us; oa.Attributes = 0x40;
    HANDLE h = NULL;
    LONG st = od(&h, DIR_QUERY, &oa);
    CHECKF(st == (LONG)0xC0000033, "status 0x%lx", (unsigned long)st);   /* STATUS_OBJECT_NAME_INVALID */
    CHECK(h == NULL);
}
TEST(win32k_modules_sysinfo_objdir_open_type_mismatch_on_windowstation) {
    char wp[128]; ms_ws_dir(wp, sizeof wp); strcat(wp, "\\WinSta0");
    HANDLE h = NULL; LONG st = ms_open_dir(wp, DIR_QUERY, &h);
    if (st == ST_OBJECT_NAME_NOT_FOUND || st == ST_OBJECT_PATH_NOT_FOUND) { t_skip("no WinSta0 in this session (0x%lx)", (unsigned long)st); return; }
    t_note("opening WinSta0 as a directory: 0x%lx", (unsigned long)st);
    CHECKF(st == ST_OBJECT_TYPE_MISMATCH || st == ST_ACCESS_DENIED, "status 0x%lx", (unsigned long)st);
    if (st == ST_SUCCESS) ms_nt_close(h);
}
TEST(win32k_modules_sysinfo_objdir_open_returns_directory_handle_with_requested_access) {
    HANDLE h; LONG st = ms_open_dir("\\", DIR_QUERY | DIR_TRAVERSE, &h);
    if (st != ST_SUCCESS) { t_skip("status 0x%lx", (unsigned long)st); return; }
    ULONG b[14], ret;
    REQUIRE(ms_qo(h, 0, b, 0x38, &ret) == ST_SUCCESS);
    CHECK((b[1] & 3) == 3);
    CHECK(b[2] == 1);
    ms_nt_close(h);
}

TEST(win32k_modules_sysinfo_objdir_query_requires_query_right) {
    HANDLE h; LONG st = ms_open_dir("\\", DIR_TRAVERSE, &h);
    if (st != ST_SUCCESS) { t_skip("status 0x%lx", (unsigned long)st); return; }
    unsigned char buf[0x1000]; ULONG ctx = 0, ret = 0;
    st = ms_qd(h, buf, sizeof buf, 0, 1, &ctx, &ret);
    CHECKF(st == ST_ACCESS_DENIED, "status 0x%lx", (unsigned long)st);
    ms_nt_close(h);
}
TEST(win32k_modules_sysinfo_objdir_query_invalid_and_wrong_type_handles) {
    unsigned char buf[0x100]; ULONG ctx = 0, ret = 0;
    CHECK(ms_qd(NULL, buf, sizeof buf, 0, 1, &ctx, &ret) == ST_INVALID_HANDLE);
    HANDLE ev = CreateEventW(NULL, TRUE, FALSE, NULL); REQUIRE(ev);
    LONG st = ms_qd(ev, buf, sizeof buf, 0, 1, &ctx, &ret);
    CHECKF(st == ST_OBJECT_TYPE_MISMATCH, "event handle status 0x%lx", (unsigned long)st);
    CloseHandle(ev);
}
TEST(win32k_modules_sysinfo_objdir_query_small_buffer_single_entry) {
    HANDLE h; LONG st = ms_open_dir("\\", DIR_QUERY, &h);
    if (st != ST_SUCCESS) { t_skip("status 0x%lx", (unsigned long)st); return; }
    unsigned char buf[0x20]; ULONG ctx = 0, ret = 0;
    st = ms_qd(h, buf, 0x20, 1, 1, &ctx, &ret);
    CHECKF(st == ST_BUFFER_TOO_SMALL, "status 0x%lx", (unsigned long)st);
    CHECK(ret > 0x40);               /* record + terminator + strings */
    ms_nt_close(h);
}
TEST(win32k_modules_sysinfo_objdir_query_small_buffer_multi_entry_reports_more) {
    HANDLE h; LONG st = ms_open_dir("\\", DIR_QUERY, &h);
    if (st != ST_SUCCESS) { t_skip("status 0x%lx", (unsigned long)st); return; }
    unsigned char buf[0x20]; ULONG ctx = 0, ret = 0;
    st = ms_qd(h, buf, 0x20, 0, 1, &ctx, &ret);
    CHECKF(st == ST_MORE_ENTRIES, "status 0x%lx", (unsigned long)st);
    CHECK(ret == 0x20);
    ms_nt_close(h);
}
TEST(win32k_modules_sysinfo_objdir_query_context_past_end_is_no_more_entries) {
    HANDLE h; LONG st = ms_open_dir("\\", DIR_QUERY, &h);
    if (st != ST_SUCCESS) { t_skip("status 0x%lx", (unsigned long)st); return; }
    unsigned char buf[0x1000]; ULONG ctx = 100000, ret = 0;
    st = ms_qd(h, buf, sizeof buf, 0, 0, &ctx, &ret);
    CHECKF(st == ST_NO_MORE_ENTRIES, "status 0x%lx", (unsigned long)st);
    CHECK(ctx == 100000);
    ms_nt_close(h);
}
TEST(win32k_modules_sysinfo_objdir_query_full_listing_context_equals_count) {
    HANDLE h; LONG st = ms_open_dir("\\", DIR_QUERY, &h);
    if (st != ST_SUCCESS) { t_skip("status 0x%lx", (unsigned long)st); return; }
    unsigned char *buf = (unsigned char *)calloc(1, 0x20000); REQUIRE(buf);
    ULONG ctx = 0, ret = 0;
    st = ms_qd(h, buf, 0x20000, 0, 1, &ctx, &ret);
    CHECKF(st == ST_SUCCESS, "status 0x%lx", (unsigned long)st);
    MS_DE *v = (MS_DE *)calloc(1024, sizeof *v); int n = 0;
    ms_parse_dir_buf(buf, v, 1024, &n);
    CHECKF((int)ctx == n, "context %lu entries %d", (unsigned long)ctx, n);
    CHECK(ret >= (ULONG)(n + 1) * 0x20);
    CHECK(ret <= 0x20000);
    free(v); free(buf); ms_nt_close(h);
}
TEST(win32k_modules_sysinfo_objdir_query_single_entry_iteration_matches_bulk) {
    HANDLE h; LONG st = ms_open_dir("\\", DIR_QUERY, &h);
    if (st != ST_SUCCESS) { t_skip("status 0x%lx", (unsigned long)st); return; }
    MS_DE *bulk = (MS_DE *)calloc(1024, sizeof *bulk); int nb = 0;
    unsigned char *buf = (unsigned char *)calloc(1, 0x20000); REQUIRE(buf);
    ULONG ctx = 0, ret = 0;
    REQUIRE(ms_qd(h, buf, 0x20000, 0, 1, &ctx, &ret) == ST_SUCCESS);
    ms_parse_dir_buf(buf, bulk, 1024, &nb);
    MS_DE one[2]; int count = 0, order_ok = 1;
    ctx = 0;
    for (int i = 0; i < 1100; i++) {
        memset(buf, 0, 0x1000);
        st = ms_qd(h, buf, 0x1000, 1, i == 0, &ctx, &ret);
        if (st == ST_NO_MORE_ENTRIES) break;
        CHECKF(st == ST_SUCCESS, "iteration %d status 0x%lx", i, (unsigned long)st);
        if (st != ST_SUCCESS) break;
        int c = 0; ms_parse_dir_buf(buf, one, 2, &c);
        CHECK(c == 1);
        if (c == 1 && count < nb && (strcmp(one[0].name, bulk[count].name) || strcmp(one[0].type, bulk[count].type))) order_ok = 0;
        count++;
        CHECKF((int)ctx == count, "context %lu after %d entries", (unsigned long)ctx, count);
    }
    CHECKF(count == nb, "single-entry walk saw %d, bulk %d", count, nb);
    CHECK(order_ok);
    free(bulk); free(buf); ms_nt_close(h);
}
TEST(win32k_modules_sysinfo_objdir_query_null_returnlength_ok) {
    HANDLE h; LONG st = ms_open_dir("\\", DIR_QUERY, &h);
    if (st != ST_SUCCESS) { t_skip("status 0x%lx", (unsigned long)st); return; }
    unsigned char *buf = (unsigned char *)calloc(1, 0x20000); REQUIRE(buf);
    ULONG ctx = 0;
    CHECK(ms_qd(h, buf, 0x20000, 0, 1, &ctx, NULL) == ST_SUCCESS);
    free(buf); ms_nt_close(h);
}
TEST(win32k_modules_sysinfo_objdir_query_strings_point_into_caller_buffer) {
    HANDLE h; LONG st = ms_open_dir("\\", DIR_QUERY, &h);
    if (st != ST_SUCCESS) { t_skip("status 0x%lx", (unsigned long)st); return; }
    unsigned char *buf = (unsigned char *)calloc(1, 0x20000); REQUIRE(buf);
    ULONG ctx = 0, ret = 0;
    REQUIRE(ms_qd(h, buf, 0x20000, 0, 1, &ctx, &ret) == ST_SUCCESS);
    for (int i = 0; i < (int)ctx; i++) {
        const unsigned char *e = buf + 0x20 * i;
        const unsigned char *nb = *(unsigned char * const *)(e + 8), *tb = *(unsigned char * const *)(e + 0x18);
        CHECK(nb >= buf + 0x20 * (ctx + 1) && nb < buf + ret);
        CHECK(tb >= buf + 0x20 * (ctx + 1) && tb < buf + ret);
        CHECK(*(const USHORT *)(e + 2) == *(const USHORT *)e + 2);          /* MaximumLength = Length + terminator */
        CHECK(*(const WCHAR *)(nb + *(const USHORT *)e) == 0);
    }
    free(buf); ms_nt_close(h);
}

/* ---- window station / desktop objects created through user32 ------------------------------ */
static int ms_type_of(HANDLE h, char *out, size_t cap) {
    unsigned char buf[0x200]; ULONG ret = 0;
    if (ms_qo(h, 2, buf, sizeof buf, &ret) != ST_SUCCESS) return 0;
    USHORT len = *(USHORT *)buf; WCHAR w[64] = {0};
    if (len / 2 >= 64 || !k_try_read(*(WCHAR **)(buf + 8), w, len)) return 0;
    ms_wide_to_narrow(w, out, cap); return 1;
}
TEST(win32k_modules_sysinfo_objdir_created_window_station_appears_in_namespace) {
    REQUIRE(U(CreateWindowStationW)); REQUIRE(U(CloseWindowStation));
    WCHAR name[64]; _snwprintf(name, 64, L"win32kt%lu_ws", (unsigned long)GetCurrentProcessId());
    HWINSTA ws = U(CreateWindowStationW)(name, 0, WINSTA_ALL_ACCESS, NULL);
    if (!ws) { t_skip("CreateWindowStation failed (%lu)", (unsigned long)GetLastError()); return; }
    char ty[64] = {0};
    CHECK(ms_type_of((HANDLE)ws, ty, sizeof ty));
    CHECK(!strcmp(ty, "WindowStation"));
    unsigned char buf[0x300]; ULONG ret = 0;
    CHECK(ms_qo((HANDLE)ws, 1, buf, sizeof buf, &ret) == ST_SUCCESS);
    {
        USHORT len = *(USHORT *)buf; WCHAR nw[256] = {0};
        if (len / 2 < 256 && k_try_read(*(WCHAR **)(buf + 8), nw, len)) {
            char nm[256]; ms_wide_to_narrow(nw, nm, sizeof nm);
            char tail[96]; snprintf(tail, sizeof tail, "\\WindowStations\\win32kt%lu_ws", (unsigned long)GetCurrentProcessId());
            size_t a = strlen(nm), b = strlen(tail);
            CHECKF(a >= b && !_stricmp(nm + a - b, tail), "object name \"%s\"", nm);
        }
    }
    ULONG b[14];
    if (ms_qo((HANDLE)ws, 0, b, 0x38, &ret) == ST_SUCCESS) CHECK((b[1] & WINSTA_ALL_ACCESS) == WINSTA_ALL_ACCESS || (b[1] & 0xF0000) != 0);
    {
        char wd[96]; ms_ws_dir(wd, sizeof wd);
        MS_LIST(v, n, wd, 256);
        char nm[64]; snprintf(nm, sizeof nm, "win32kt%lu_ws", (unsigned long)GetCurrentProcessId());
        MS_DE *e = ms_de_find(v, n, nm);
        CHECK(e != NULL);
        if (e) CHECK(!strcmp(e->type, "WindowStation"));
        free(v);
    }
    CHECK(U(CloseWindowStation)(ws));
    {
        MS_DE *v = (MS_DE *)calloc(256, sizeof *v); int n = 0;
        char wd2[96]; ms_ws_dir(wd2, sizeof wd2);
        if (v && ms_dir_list(wd2, v, 256, &n) == ST_SUCCESS) {
            char nm[64]; snprintf(nm, sizeof nm, "win32kt%lu_ws", (unsigned long)GetCurrentProcessId());
            t_note("station after CloseWindowStation: %s", ms_de_find(v, n, nm) ? "still listed" : "removed");
        }
        free(v);
    }
}
TEST(win32k_modules_sysinfo_objdir_created_desktop_object_type) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(CloseDesktop)); REQUIRE(U(GetUserObjectInformationW));
    WCHAR name[64]; _snwprintf(name, 64, L"win32kt%lu_dk", (unsigned long)GetCurrentProcessId());
    HDESK d = U(CreateDesktopW)(name, NULL, NULL, 0, DESKTOP_CREATEWINDOW | DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS | READ_CONTROL, NULL);
    if (!d) { t_skip("CreateDesktop failed (%lu)", (unsigned long)GetLastError()); return; }
    char ty[64] = {0};
    CHECK(ms_type_of((HANDLE)d, ty, sizeof ty));
    CHECK(!strcmp(ty, "Desktop"));
    WCHAR nm[64] = {0}; DWORD need = 0;
    CHECK(U(GetUserObjectInformationW)(d, UOI_NAME, nm, sizeof nm, &need));
    CHECK(!lstrcmpW(nm, name));
    ULONG b[14], ret;
    if (ms_qo((HANDLE)d, 0, b, 0x38, &ret) == ST_SUCCESS) CHECK((b[1] & (DESKTOP_CREATEWINDOW | DESKTOP_READOBJECTS)) == (DESKTOP_CREATEWINDOW | DESKTOP_READOBJECTS));
    CHECK(U(CloseDesktop)(d));
    SET_ERR(0);
    CHECKF(ms_type_of((HANDLE)d, ty, sizeof ty) == 0, "handle still valid after CloseDesktop");
}
