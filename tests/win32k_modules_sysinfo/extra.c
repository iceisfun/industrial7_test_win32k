/* win32k_modules_sysinfo: the user-mode routes onto the same kernel data (psapi device-driver
 * enumeration rides SystemModuleInformation) and per-object handle accounting for the window
 * station / desktop objects whose types the Win32 stack registers.
 *
 * Kernel facts: kernel32!K32EnumDeviceDrivers / K32GetDeviceDriverBaseNameA / ...FileNameA are thin
 * wrappers over NtQuerySystemInformation(11): the base is the LDR DllBase, the base name starts at
 * OffsetToFileName. NtQueryObject(ObjectBasicInformation) reports the live handle count of the object
 * (OBJECT_HEADER.HandleCount), so a duplicate in the same process adds exactly one handle to an object
 * that no other process can reach (a uniquely named, never-shared desktop or window station).
 */
#include "ms.h"

typedef BOOL  (WINAPI *ms_enumdrv_fn)(LPVOID *, DWORD, LPDWORD);
typedef DWORD (WINAPI *ms_drvname_fn)(LPVOID, LPSTR, DWORD);

static ms_enumdrv_fn ms_enumdrv(void) { return (ms_enumdrv_fn)t_proc_in("kernel32.dll", "K32EnumDeviceDrivers"); }

TEST(win32k_modules_sysinfo_psapi_enum_device_drivers_matches_module_list) {
    ms_enumdrv_fn f = ms_enumdrv(); REQUIRE(f);
    MS_MODS *m = ms_mods(); if (!m) { t_skip("no module list"); return; }
    if (t_is_wine()) { t_skip("emulated module list"); return; }
    LPVOID bases[2048]; DWORD need = 0;
    CHECK(f(bases, sizeof bases, &need));
    DWORD n = need / sizeof(LPVOID);
    CHECKF(n >= m->NumberOfModules && n <= m->NumberOfModules + 8, "psapi %lu modules, class 11 %lu", (unsigned long)n, (unsigned long)m->NumberOfModules);
    for (ULONG i = 0; i < m->NumberOfModules && i < n && i < 2048; i++)
        CHECKF(bases[i] == m->Modules[i].ImageBase, "entry %lu: psapi %p class 11 %p", (unsigned long)i, bases[i], m->Modules[i].ImageBase);
}

TEST(win32k_modules_sysinfo_psapi_enum_device_drivers_short_buffer_reports_need) {
    ms_enumdrv_fn f = ms_enumdrv(); REQUIRE(f);
    if (t_is_wine()) { t_skip("emulated module list"); return; }
    LPVOID one[1]; DWORD need = 0;
    f(one, sizeof one, &need);
    MS_MODS *m = ms_mods(); REQUIRE(m);
    /* psapi reports the bytes needed for every module, not the bytes copied. */
    CHECKF(need >= m->NumberOfModules * sizeof(LPVOID), "need %lu", (unsigned long)need);
}

static void ms_psapi_leaf(const char *leaf) {
    MS_NEED_MOD(i, leaf);
    ms_drvname_fn base = (ms_drvname_fn)t_proc_in("kernel32.dll", "K32GetDeviceDriverBaseNameA");
    ms_drvname_fn file = (ms_drvname_fn)t_proc_in("kernel32.dll", "K32GetDeviceDriverFileNameA");
    REQUIRE(base); REQUIRE(file);
    LPVOID b = ms_mods()->Modules[i].ImageBase;
    char nm[MAX_PATH] = {0}, fn[MAX_PATH] = {0};
    DWORD n = base(b, nm, sizeof nm);
    CHECK(n == strlen(leaf));
    CHECKF(!_stricmp(nm, leaf), "base name \"%s\"", nm);
    n = file(b, fn, sizeof fn);
    CHECK(n != 0);
    size_t fl = strlen(fn), ll = strlen(leaf);
    CHECKF(fl > ll && !_stricmp(fn + fl - ll, leaf) && fn[fl - ll - 1] == '\\', "file name \"%s\"", fn);
    /* an address inside the image is not a load address: psapi matches the exact DllBase only */
    SET_ERR(0);
    CHECK(base((char *)b + 0x1000, nm, sizeof nm) == 0);
}
TEST(win32k_modules_sysinfo_psapi_names_win32k_sys)     { ms_psapi_leaf("win32k.sys"); }
TEST(win32k_modules_sysinfo_psapi_names_win32kbase_sys) { ms_psapi_leaf("win32kbase.sys"); }
TEST(win32k_modules_sysinfo_psapi_names_win32kfull_sys) { ms_psapi_leaf("win32kfull.sys"); }

TEST(win32k_modules_sysinfo_psapi_basename_truncation) {
    MS_NEED_MOD(i, "win32kbase.sys");
    ms_drvname_fn base = (ms_drvname_fn)t_proc_in("kernel32.dll", "K32GetDeviceDriverBaseNameA"); REQUIRE(base);
    char nm[8]; memset(nm, 'x', sizeof nm);
    DWORD n = base(ms_mods()->Modules[i].ImageBase, nm, 4);
    CHECK(n <= 4);
    CHECK(nm[7] == 'x');                 /* nothing written past the stated size */
}

/* ---- handle accounting on private window station / desktop objects ---------------------------- */
static ULONG ms_handle_count(HANDLE h) {
    ULONG b[14] = {0}, ret = 0;
    return ms_qo(h, 0, b, 0x38, &ret) == ST_SUCCESS ? b[2] : 0xFFFFFFFF;
}

TEST(win32k_modules_sysinfo_objhandles_private_desktop_count_follows_duplicates) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(CloseDesktop));
    WCHAR name[64]; _snwprintf(name, 64, L"win32kt%lu_hc", (unsigned long)GetCurrentProcessId());
    HDESK d = U(CreateDesktopW)(name, NULL, NULL, 0, DESKTOP_CREATEWINDOW | DESKTOP_READOBJECTS, NULL);
    if (!d) { t_skip("CreateDesktop failed (%lu)", (unsigned long)GetLastError()); return; }
    ULONG c0 = ms_handle_count((HANDLE)d);
    if (c0 == 0xFFFFFFFF) { U(CloseDesktop)(d); t_skip("NtQueryObject refused the desktop handle"); return; }
    CHECKF(c0 >= 1, "initial handle count %lu", (unsigned long)c0);
    HANDLE dup = NULL;
    REQUIRE(DuplicateHandle(GetCurrentProcess(), (HANDLE)d, GetCurrentProcess(), &dup, 0, FALSE, DUPLICATE_SAME_ACCESS));
    ULONG c1 = ms_handle_count(dup);
    CHECKF(c1 == c0 + 1, "after duplicate %lu, before %lu", (unsigned long)c1, (unsigned long)c0);
    CloseHandle(dup);
    CHECKF(ms_handle_count((HANDLE)d) == c0, "after closing the duplicate %lu", (unsigned long)ms_handle_count((HANDLE)d));
    CHECK(U(CloseDesktop)(d));
}

TEST(win32k_modules_sysinfo_objhandles_private_window_station_count_follows_duplicates) {
    REQUIRE(U(CreateWindowStationW)); REQUIRE(U(CloseWindowStation));
    WCHAR name[64]; _snwprintf(name, 64, L"win32kt%lu_hs", (unsigned long)GetCurrentProcessId());
    HWINSTA ws = U(CreateWindowStationW)(name, 0, WINSTA_ALL_ACCESS, NULL);
    if (!ws) { t_skip("CreateWindowStation failed (%lu)", (unsigned long)GetLastError()); return; }
    ULONG c0 = ms_handle_count((HANDLE)ws);
    if (c0 == 0xFFFFFFFF) { U(CloseWindowStation)(ws); t_skip("NtQueryObject refused the station handle"); return; }
    HANDLE dup = NULL;
    REQUIRE(DuplicateHandle(GetCurrentProcess(), (HANDLE)ws, GetCurrentProcess(), &dup, 0, FALSE, DUPLICATE_SAME_ACCESS));
    CHECKF(ms_handle_count(dup) == c0 + 1, "after duplicate %lu, before %lu", (unsigned long)ms_handle_count(dup), (unsigned long)c0);
    CloseHandle(dup);
    CHECKF(ms_handle_count((HANDLE)ws) == c0, "after closing the duplicate %lu", (unsigned long)ms_handle_count((HANDLE)ws));
    CHECK(U(CloseWindowStation)(ws));
}

TEST(win32k_modules_sysinfo_objhandles_object_type_index_matches_types_list) {
    REQUIRE(U(CreateDesktopW)); REQUIRE(U(CloseDesktop));
    if (t_is_wine()) { t_skip("emulated object manager"); return; }
    WCHAR name[64]; _snwprintf(name, 64, L"win32kt%lu_ti", (unsigned long)GetCurrentProcessId());
    HDESK d = U(CreateDesktopW)(name, NULL, NULL, 0, DESKTOP_CREATEWINDOW, NULL);
    if (!d) { t_skip("CreateDesktop failed (%lu)", (unsigned long)GetLastError()); return; }
    unsigned char buf[0x200]; ULONG ret = 0;
    LONG st = ms_qo((HANDLE)d, 2, buf, sizeof buf, &ret);
    if (st == ST_SUCCESS) {
        /* ObjectTypeInformation of the handle is the very record ObjectTypesInformation lists for "Desktop". */
        ULONG n = 0, sz = 0; (void)n; (void)sz;
        ULONG big = 0x10000; unsigned char *all = (unsigned char *)calloc(1, big);
        if (all && ms_qo(NULL, 3, all, big, &ret) == ST_SUCCESS) {
            ULONG cnt = *(ULONG *)all, off = 8; int seen = 0;
            for (ULONG i = 0; i < cnt; i++) {
                const unsigned char *r = all + off; const unsigned short len = *(const unsigned short *)r;
                WCHAR w[64] = {0};
                if (len / 2 < 64 && k_try_read(*(WCHAR * const *)(r + 8), w, len) && !lstrcmpW(w, L"Desktop")) {
                    seen = 1;
                    CHECK(r[0x5A] == buf[0x5A]);
                    CHECK(*(const ULONG *)(r + 0x54) == *(const ULONG *)(buf + 0x54));
                    CHECK(!memcmp(r + 0x44, buf + 0x44, 16));
                }
                off += 0x68 + ((len + 9) & ~7u);
            }
            CHECK(seen);
        }
        free(all);
    }
    U(CloseDesktop)(d);
}

TEST(win32k_modules_sysinfo_objhandles_open_missing_window_station_fails) {
    REQUIRE(U(OpenWindowStationW));
    WCHAR name[64]; _snwprintf(name, 64, L"win32kt%lu_missing", (unsigned long)GetCurrentProcessId());
    SET_ERR(0);
    HWINSTA ws = U(OpenWindowStationW)(name, FALSE, WINSTA_READATTRIBUTES);
    CHECK(ws == NULL);
    t_note("OpenWindowStationW(missing) last error %lu", (unsigned long)GetLastError());
    if (ws) U(CloseWindowStation)(ws);
}

TEST(win32k_modules_sysinfo_objhandles_process_window_station_not_closable) {
    REQUIRE(U(GetProcessWindowStation)); REQUIRE(U(CloseWindowStation));
    HWINSTA ws = U(GetProcessWindowStation)(); REQUIRE(ws);
    /* The handle is the process's current station; the kernel keeps it, so it must still be queryable afterwards. */
    ULONG before = ms_handle_count((HANDLE)ws);
    if (before == 0xFFFFFFFF) { t_skip("NtQueryObject refused the station handle"); return; }
    HANDLE dup = NULL;
    REQUIRE(DuplicateHandle(GetCurrentProcess(), (HANDLE)ws, GetCurrentProcess(), &dup, 0, FALSE, DUPLICATE_SAME_ACCESS));
    CHECK(ms_handle_count(dup) >= before + 1);
    CloseHandle(dup);
    CHECK(ms_handle_count((HANDLE)ws) != 0xFFFFFFFF);
}
