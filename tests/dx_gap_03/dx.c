/* dx_gap_03: state-changing / injection-adjacent syscalls from the gap_03 set (run only with /destructive).
 *
 * NtUserCreateWindowStation / NtUserCreateDesktopEx are win32kbase edition entry points whose argument layout
 * (OBJECT_ATTRIBUTES, edition callback) is only exercised through user32!CreateWindowStationW / CreateDesktopExW here.
 * Each test creates a uniquely named object, validates it from the outside (user object name/type, EnumDesktops, handle
 * lifetime) and closes it again, restoring the process window station / desktop assignment (never changed).
 *
 * NtUserInitializePointerDeviceInjection(Ex)(type, contacts, hTemplate, mode, flags, HANDLE*):
 *   RIMIDECheckCurrentProcessForInjectionPermissions fails -> ERROR_ACCESS_DENIED (5), else type must be 2 (pen, contacts 1..0x100)
 *   or 3 (touch, contacts == 1), mode 1..3, otherwise ERROR_INVALID_PARAMETER (0x57). Only combinations that cannot create a
 *   device are used, so either outcome is a failure with *out untouched.
 * NtUserInjectPointerInput(hDevice, POINTER_TYPE_INFO[], count):
 *   no permission -> 5; count == 0 -> 0x57; unresolvable device handle -> ERROR_INVALID_HANDLE (6). Never injects.
 */
#include "t.h"
#include "k.h"

UDECL(BOOL, NtUserInitializePointerDeviceInjection, (int, int, HANDLE, int, DWORD, HANDLE *));
UDECL(BOOL, NtUserInjectPointerInput, (HANDLE, const void *, UINT));

#undef EXPECT_EQ
#define EXPECT_EQ(a, b) do { unsigned long long a_ = (unsigned long long)(ULONG_PTR)(a), b_ = (unsigned long long)(ULONG_PTR)(b); \
    t_check(a_ == b_, __FILE__, __LINE__, #a " == " #b, "got 0x%llx want 0x%llx", a_, b_); } while (0)
#define SENTINEL_ERR 0x31337

static FARPROC dx_nt(const char *name)
{
    if (t_is_wine()) return NULL;     /* Wine leaves these win32u exports unimplemented (the process aborts on call) */
    return t_proc(name);
}
#define DXN(name) ((name##_pfn)dx_nt(#name))

static void uname(WCHAR *out, size_t cap, const char *tag)
{
    wsprintfW(out, L"win32kt_gap3_%S_%lu_%lu", tag, GetCurrentProcessId(), GetTickCount());
    (void)cap;
}

/* ---- window station ---------------------------------------------------- */

TEST(gap3dx_createwinsta_unique_name_roundtrip) {
    HWINSTA (WINAPI *cws)(LPCWSTR, DWORD, ACCESS_MASK, LPSECURITY_ATTRIBUTES) = (void *)t_proc("CreateWindowStationW");
    REQUIRE(cws);
    BOOL (WINAPI *cl)(HWINSTA) = (void *)t_proc("CloseWindowStation");
    BOOL (WINAPI *gui)(HANDLE, int, PVOID, DWORD, LPDWORD) = (void *)t_proc("GetUserObjectInformationW");
    HWINSTA (WINAPI *gp)(void) = (void *)t_proc("GetProcessWindowStation");
    REQUIRE(cl); REQUIRE(gui); REQUIRE(gp);
    WCHAR name[96]; uname(name, 96, "ws");
    HWINSTA before = gp();
    SetLastError(SENTINEL_ERR);
    HWINSTA ws = cws(name, 0, WINSTA_ALL_ACCESS, NULL);
    if (!ws) { t_note("CreateWindowStationW failed: %lu (not permitted for this session?)", GetLastError()); t_skip("cannot create a window station here"); return; }
    CHECK(ws != INVALID_HANDLE_VALUE);
    CHECK(ws != before);
    WCHAR got[128] = L""; DWORD need = 0;
    CHECK(gui(ws, UOI_NAME, got, sizeof got, &need));
    EXPECT_WSTR(got, name);
    EXPECT_EQ(need, (lstrlenW(name) + 1) * 2);
    WCHAR type[32] = L"";
    CHECK(gui(ws, UOI_TYPE, type, sizeof type, &need));
    EXPECT_WSTR(type, L"WindowStation");
    EXPECT_EQ(gp(), before);                 /* creating a station never changes the process assignment */
    CHECK(cl(ws));
    SetLastError(SENTINEL_ERR);
    EXPECT_EQ(gui(ws, UOI_NAME, got, sizeof got, &need), 0);   /* closed handle is dead */
}

TEST(gap3dx_createwinsta_two_stations_distinct) {
    HWINSTA (WINAPI *cws)(LPCWSTR, DWORD, ACCESS_MASK, LPSECURITY_ATTRIBUTES) = (void *)t_proc("CreateWindowStationW");
    BOOL (WINAPI *cl)(HWINSTA) = (void *)t_proc("CloseWindowStation");
    REQUIRE(cws); REQUIRE(cl);
    WCHAR a[96], b[96];
    uname(a, 96, "wa"); uname(b, 96, "wb");
    HWINSTA wa = cws(a, 0, WINSTA_ALL_ACCESS, NULL);
    HWINSTA wb = cws(b, 0, WINSTA_ALL_ACCESS, NULL);
    if (!wa || !wb) { if (wa) cl(wa); if (wb) cl(wb); t_skip("cannot create window stations"); return; }
    CHECK(wa != wb);
    cl(wa); cl(wb);
}

/* ---- desktop ------------------------------------------------------------ */

static WCHAR g_enum_target[96];
static int g_enum_found;
static BOOL CALLBACK enum_cb(LPWSTR n, LPARAM l) { if (!lstrcmpiW(n, g_enum_target)) g_enum_found = 1; return TRUE; }

TEST(gap3dx_createdesktop_unique_name_roundtrip) {
    HDESK (WINAPI *cd)(LPCWSTR, LPCWSTR, DEVMODEW *, DWORD, ACCESS_MASK, LPSECURITY_ATTRIBUTES) = (void *)t_proc("CreateDesktopW");
    BOOL (WINAPI *cl)(HDESK) = (void *)t_proc("CloseDesktop");
    BOOL (WINAPI *gui)(HANDLE, int, PVOID, DWORD, LPDWORD) = (void *)t_proc("GetUserObjectInformationW");
    BOOL (WINAPI *ed)(HWINSTA, DESKTOPENUMPROCW, LPARAM) = (void *)t_proc("EnumDesktopsW");
    HWINSTA (WINAPI *gp)(void) = (void *)t_proc("GetProcessWindowStation");
    HDESK (WINAPI *gtd)(DWORD) = (void *)t_proc("GetThreadDesktop");
    REQUIRE(cd); REQUIRE(cl); REQUIRE(gui); REQUIRE(ed); REQUIRE(gp); REQUIRE(gtd);
    WCHAR name[96]; uname(name, 96, "dk");
    HDESK before = gtd(GetCurrentThreadId());
    SetLastError(SENTINEL_ERR);
    HDESK d = cd(name, NULL, NULL, 0, GENERIC_ALL, NULL);
    if (!d) { t_note("CreateDesktopW failed: %lu", GetLastError()); t_skip("cannot create a desktop here"); return; }
    WCHAR got[128] = L""; DWORD need = 0;
    CHECK(gui(d, UOI_NAME, got, sizeof got, &need));
    EXPECT_WSTR(got, name);
    WCHAR type[32] = L"";
    CHECK(gui(d, UOI_TYPE, type, sizeof type, &need));
    EXPECT_WSTR(type, L"Desktop");
    lstrcpyW(g_enum_target, name); g_enum_found = 0;
    CHECK(ed(gp(), enum_cb, 0));
    EXPECT_EQ(g_enum_found, 1);              /* the new desktop is listed in the current window station */
    EXPECT_EQ(gtd(GetCurrentThreadId()), before);   /* the calling thread keeps its desktop */
    CHECK(cl(d));
    g_enum_found = 0;
    ed(gp(), enum_cb, 0);
    if (g_enum_found) t_note("desktop still listed after its last handle was closed (destruction is deferred)");
}

TEST(gap3dx_createdesktopex_heap_size_accepted) {
    HDESK (WINAPI *cd)(LPCWSTR, LPCWSTR, DEVMODEW *, DWORD, ACCESS_MASK, LPSECURITY_ATTRIBUTES, ULONG, PVOID) = (void *)t_proc("CreateDesktopExW");
    BOOL (WINAPI *cl)(HDESK) = (void *)t_proc("CloseDesktop");
    REQUIRE(cd); REQUIRE(cl);
    static const ULONG heaps[] = { 0, 128, 512, 4096 };   /* KB; 0 selects the per-desktop-class default */
    for (unsigned i = 0; i < 4; i++) {
        WCHAR name[96]; uname(name, 96, "dh");
        wsprintfW(name + lstrlenW(name), L"_%u", i);
        HDESK d = cd(name, NULL, NULL, 0, GENERIC_ALL, NULL, heaps[i], NULL);
        CHECKF(d != NULL, "heap %lu KB: error %lu", (unsigned long)heaps[i], GetLastError());
        if (d) cl(d);
    }
}

TEST(gap3dx_createdesktop_empty_name_fails) {
    HDESK (WINAPI *cd)(LPCWSTR, LPCWSTR, DEVMODEW *, DWORD, ACCESS_MASK, LPSECURITY_ATTRIBUTES) = (void *)t_proc("CreateDesktopW");
    BOOL (WINAPI *cl)(HDESK) = (void *)t_proc("CloseDesktop");
    REQUIRE(cd); REQUIRE(cl);
    HDESK d = cd(L"", NULL, NULL, 0, GENERIC_ALL, NULL);
    t_note("CreateDesktopW(\"\") -> %p err %lu", (void *)d, GetLastError());
    if (d) cl(d);
}

/* ---- pointer injection (rejections only) ------------------------------- */

TEST(gap3dx_initpointerinjection_invalid_combinations_fail) {
    BOOL (WINAPI *ip)(int, int, HANDLE, int, DWORD, HANDLE *) = (void *)DXN(NtUserInitializePointerDeviceInjection);
    REQUIRE(ip);
    static const struct { int type, contacts, mode; } v[] = {
        {0, 1, 1}, {1, 1, 1}, {4, 1, 1}, {-1, 1, 1}, {0x100, 1, 1},      /* type not pen/touch */
        {3, 0, 1}, {3, 2, 1}, {3, 0x100, 1},                              /* touch requires exactly 1 contact */
        {2, 0, 1}, {2, 0x101, 1}, {2, 0x7fffffff, 1}, {2, -1, 1},         /* pen: 1..0x100 contacts */
        {2, 1, 0}, {2, 1, 4}, {2, 1, -1}, {2, 0x100, 0x100}, {3, 1, 0}, {3, 1, 4}, /* mode must be 1..3 */
    };
    for (unsigned i = 0; i < sizeof v / sizeof *v; i++) {
        HANDLE out = (HANDLE)(ULONG_PTR)0x5a5a5a5a;
        SetLastError(SENTINEL_ERR);
        BOOL r = ip(v[i].type, v[i].contacts, NULL, v[i].mode, 0, &out);
        CHECKF(r == 0, "type %d contacts %d mode %d succeeded", v[i].type, v[i].contacts, v[i].mode);
        DWORD e = GetLastError();
        CHECKF(e == ERROR_ACCESS_DENIED || e == ERROR_INVALID_PARAMETER, "error %lu", e);   /* 5 without injection permission, else 0x57 */
        EXPECT_EQ(out, 0x5a5a5a5a);
        if (r) { CloseHandle(out); break; }
    }
}

TEST(gap3dx_initpointerinjection_error_is_stable_per_process) {
    BOOL (WINAPI *ip)(int, int, HANDLE, int, DWORD, HANDLE *) = (void *)DXN(NtUserInitializePointerDeviceInjection);
    REQUIRE(ip);
    HANDLE o = NULL;
    SetLastError(SENTINEL_ERR); ip(1, 1, NULL, 1, 0, &o); DWORD e1 = GetLastError();
    SetLastError(SENTINEL_ERR); ip(1, 1, NULL, 1, 0, &o); DWORD e2 = GetLastError();
    EXPECT_EQ(e1, e2);
    t_note("injection permission outcome: error %lu (%s)", e1, e1 == ERROR_ACCESS_DENIED ? "process lacks injection permission" : "permitted; parameter validation reached");
}

TEST(gap3dx_injectpointerinput_rejections) {
    BOOL (WINAPI *ij)(HANDLE, const void *, UINT) = (void *)DXN(NtUserInjectPointerInput);
    REQUIRE(ij);
    CHECK(sizeof(POINTER_TYPE_INFO) == 0x98);   /* the kernel strides 0x98 bytes per element */
    BYTE info[0x98 * 2]; memset(info, 0, sizeof info);
    HANDLE ev = CreateEventA(NULL, TRUE, FALSE, NULL);
    /* count == 0 -> 0x57 once permitted, 5 otherwise. */
    SetLastError(SENTINEL_ERR);
    EXPECT_EQ(ij(NULL, info, 0), 0);
    DWORD e = GetLastError();
    CHECKF(e == ERROR_ACCESS_DENIED || e == ERROR_INVALID_PARAMETER, "count 0: error %lu", e);
    /* unresolvable device handle -> 6 once permitted, 5 otherwise. The buffer is not read before the handle resolves. */
    HANDLE bad[] = { NULL, (HANDLE)1, (HANDLE)0x1234, (HANDLE)(ULONG_PTR)-1, ev };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
        SetLastError(SENTINEL_ERR);
        EXPECT_EQ(ij(bad[i], info, 1), 0);
        e = GetLastError();
        CHECKF(e == ERROR_ACCESS_DENIED || e == ERROR_INVALID_HANDLE, "handle %p: error %lu", bad[i], e);
    }
    CloseHandle(ev);
}

TEST(gap3dx_injectpointerinput_user32_wrapper_rejects_bad_device) {
    BOOL (WINAPI *ij)(HANDLE, const void *, UINT) = (void *)t_proc("InjectSyntheticPointerInput");
    REQUIRE(ij);
    BYTE info[0x98]; memset(info, 0, sizeof info);
    SetLastError(SENTINEL_ERR);
    EXPECT_EQ(ij((HANDLE)0x1234, info, 1), 0);
    DWORD e = GetLastError();
    CHECKF(e == ERROR_ACCESS_DENIED || e == ERROR_INVALID_HANDLE || e == ERROR_INVALID_PARAMETER, "error %lu", e);
}

TEST(gap3dx_deviceremovalsupport_stub_present_not_called) {
    /* NtGdiDdDDISetProcessDeviceRemovalSupport (win32k thunk -> dxgkrnl) has no win32kfull/win32kbase body and its argument
     * block is not provable from the corpus; it also latches per-process DXGI state, so only the stub is checked. */
    FARPROC p = t_proc_in("win32u.dll", "NtGdiDdDDISetProcessDeviceRemovalSupport");
    REQUIRE(p);
    if (t_is_wine()) { t_skip("Wine stub"); return; }
    CHECK(k_stub_valid(p));
    EXPECT_EQ(k_stub_number(p), 4676);
}
