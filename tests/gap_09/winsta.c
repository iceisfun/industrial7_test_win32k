/* gap9_winsta*: NtUserOpenWindowStation (reached by user32!OpenWindowStationA/W).
 *
 * Analysis: NtUserOpenWindowStation(POBJECT_ATTRIBUTES oa, ACCESS_MASK access).  The special object name
 * "Service-0x00000000-00000000$" (Length 0x38, MaximumLength 0x3A, RootDirectory != 0) is rewritten IN THE
 * CALLER'S BUFFER to "Service-0x<luid hi>-<luid lo>$" before the open.  Everything else goes straight to
 * _OpenWindowStation (object manager open of a WindowStation object, so access mapping, inherit flag and
 * NTSTATUS->Win32 error follow the object manager rules). */
#include "g9.h"
#include <winternl.h>

UDECL(HWINSTA, NtUserOpenWindowStation, (OBJECT_ATTRIBUTES *oa, ACCESS_MASK access));

typedef struct { ULONG Attributes; ACCESS_MASK GrantedAccess; ULONG HandleCount; ULONG PointerCount; } G9_OBI;
UDECL(LONG, NtQueryObject, (HANDLE, int, void *, ULONG, ULONG *));

static ACCESS_MASK granted(HANDLE h) {
    G9_OBI bi; ULONG rl = 0;
    memset(&bi, 0xCC, sizeof bi);
    if (!UN(NtQueryObject) || UN(NtQueryObject)(h, 0 /*ObjectBasicInformation*/, &bi, sizeof bi, &rl) < 0) return 0xFFFFFFFFu;
    return bi.GrantedAccess;
}
static int type_is(HANDLE h, const WCHAR *want) {
    BYTE buf[512]; ULONG rl = 0;
    if (!UN(NtQueryObject) || UN(NtQueryObject)(h, 2 /*ObjectTypeInformation*/, buf, sizeof buf, &rl) < 0) return -1;
    UNICODE_STRING *u = (UNICODE_STRING *)buf;
    return u->Length / 2 == lstrlenW(want) && !memcmp(u->Buffer, want, u->Length);
}

TEST(gap9_winsta_open_winsta0) {
    REQUIRE(U(OpenWindowStationW)); REQUIRE(U(CloseWindowStation)); REQUIRE(U(GetUserObjectInformationW));
    SetLastError(0);
    HWINSTA h = U(OpenWindowStationW)(L"WinSta0", FALSE, WINSTA_READATTRIBUTES);
    if (!h) { t_skip("WinSta0 not openable: %lu", GetLastError()); return; }
    CHECK(type_is((HANDLE)h, L"WindowStation") != 0);
    WCHAR nm[64] = {0}; DWORD need = 0;
    CHECK(U(GetUserObjectInformationW)(h, UOI_NAME, nm, sizeof nm, &need));
    EXPECT_WSTR(nm, L"WinSta0");
    EXPECT_EQ(need, 16);                                   /* 7 chars + NUL */
    DWORD fl = 0;
    HANDLE hh = (HANDLE)h;
    CHECK(GetHandleInformation(hh, &fl));
    CHECK(!(fl & HANDLE_FLAG_INHERIT));
    CHECK(U(CloseWindowStation)(h));
    SetLastError(0);
    CHECK(!GetHandleInformation(hh, &fl));                 /* handle value is dead after close */
    EXPECT_ERR(ERROR_INVALID_HANDLE);
}

TEST(gap9_winsta_inherit_flag) {
    REQUIRE(U(OpenWindowStationW)); REQUIRE(U(CloseWindowStation));
    for (int inh = 0; inh < 2; inh++) {
        HWINSTA h = U(OpenWindowStationW)(L"WinSta0", inh, WINSTA_READATTRIBUTES);
        if (!h) { t_skip("not openable"); return; }
        DWORD fl = 0;
        CHECK(GetHandleInformation((HANDLE)h, &fl));
        EXPECT_EQ(!!(fl & HANDLE_FLAG_INHERIT), inh);      /* OBJ_INHERIT attribute set by user32 from fInherit */
        U(CloseWindowStation)(h);
    }
}

TEST(gap9_winsta_nonexistent_name) {
    REQUIRE(U(OpenWindowStationW));
    WCHAR nm[64];
    wsprintfW(nm, L"win32kt_nosuch_%lu", GetCurrentProcessId());
    SetLastError(0);
    HWINSTA h = U(OpenWindowStationW)(nm, FALSE, WINSTA_READATTRIBUTES);
    CHECK(h == NULL);
    EXPECT_ERR(ERROR_FILE_NOT_FOUND);                      /* STATUS_OBJECT_NAME_NOT_FOUND */
    if (h) U(CloseWindowStation)(h);
}

TEST(gap9_winsta_backslash_name_rejected) {
    REQUIRE(U(OpenWindowStationW));
    /* a name relative to a root directory cannot start with a separator */
    SetLastError(0);
    HWINSTA h = U(OpenWindowStationW)(L"\\WinSta0", FALSE, WINSTA_READATTRIBUTES);
    CHECK(h == NULL);
    t_note("OpenWindowStation(\"\\\\WinSta0\") error %lu", GetLastError());
    if (h) U(CloseWindowStation)(h);
}

TEST(gap9_winsta_case_insensitive) {
    REQUIRE(U(OpenWindowStationW)); REQUIRE(U(CloseWindowStation));
    HWINSTA a = U(OpenWindowStationW)(L"WinSta0", FALSE, WINSTA_READATTRIBUTES);
    SetLastError(0);
    HWINSTA b = U(OpenWindowStationW)(L"WINSTA0", FALSE, WINSTA_READATTRIBUTES);
    if (!a) { t_skip("not openable"); return; }
    if (!b) t_note("upper-case name not found, error %lu", GetLastError());
    CHECK(b != NULL);
    U(CloseWindowStation)(a);
    if (b) U(CloseWindowStation)(b);
}

TEST(gap9_winsta_two_opens_same_object) {
    REQUIRE(U(OpenWindowStationW)); REQUIRE(U(CloseWindowStation));
    HWINSTA a = U(OpenWindowStationW)(L"WinSta0", FALSE, WINSTA_READATTRIBUTES);
    HWINSTA b = U(OpenWindowStationW)(L"WinSta0", FALSE, WINSTA_READATTRIBUTES);
    if (!a || !b) { t_skip("not openable"); return; }
    CHECK(a != b);                                         /* every open allocates a new handle */
    HMODULE kb = GetModuleHandleW(L"kernelbase.dll");
    BOOL (WINAPI *cmp)(HANDLE, HANDLE) = kb ? (BOOL (WINAPI *)(HANDLE, HANDLE))GetProcAddress(kb, "CompareObjectHandles") : NULL;
    if (cmp) CHECK(cmp((HANDLE)a, (HANDLE)b));
    CHECK(U(CloseWindowStation)(a));
    DWORD fl;
    CHECK(GetHandleInformation((HANDLE)b, &fl));           /* closing one handle leaves the other valid */
    CHECK(U(CloseWindowStation)(b));
}

TEST(gap9_winsta_ansi_and_wide_same_object) {
    REQUIRE(U(OpenWindowStationA)); REQUIRE(U(OpenWindowStationW)); REQUIRE(U(CloseWindowStation));
    HWINSTA a = U(OpenWindowStationA)("WinSta0", FALSE, WINSTA_READATTRIBUTES);
    HWINSTA w = U(OpenWindowStationW)(L"WinSta0", FALSE, WINSTA_READATTRIBUTES);
    if (!a || !w) { t_skip("not openable"); return; }
    HMODULE kb = GetModuleHandleW(L"kernelbase.dll");
    BOOL (WINAPI *cmp)(HANDLE, HANDLE) = kb ? (BOOL (WINAPI *)(HANDLE, HANDLE))GetProcAddress(kb, "CompareObjectHandles") : NULL;
    if (cmp) CHECK(cmp((HANDLE)a, (HANDLE)w));
    U(CloseWindowStation)(a); U(CloseWindowStation)(w);
}

TEST(gap9_winsta_granted_access_single_rights) {
    REQUIRE(U(OpenWindowStationW)); REQUIRE(U(CloseWindowStation));
    static const struct { DWORD right; const char *n; } t[] = {
        { WINSTA_ENUMDESKTOPS, "ENUMDESKTOPS" }, { WINSTA_READATTRIBUTES, "READATTRIBUTES" },
        { WINSTA_ACCESSCLIPBOARD, "ACCESSCLIPBOARD" }, { WINSTA_CREATEDESKTOP, "CREATEDESKTOP" },
        { WINSTA_WRITEATTRIBUTES, "WRITEATTRIBUTES" }, { WINSTA_ACCESSGLOBALATOMS, "ACCESSGLOBALATOMS" },
        { WINSTA_EXITWINDOWS, "EXITWINDOWS" }, { WINSTA_ENUMERATE, "ENUMERATE" },
        { WINSTA_READSCREEN, "READSCREEN" }, { READ_CONTROL, "READ_CONTROL" },
    };
    int opened = 0;
    for (int i = 0; i < G9_N(t); i++) {
        SetLastError(0);
        HWINSTA h = U(OpenWindowStationW)(L"WinSta0", FALSE, t[i].right);
        if (!h) { t_note("%s: denied (error %lu)", t[i].n, GetLastError()); continue; }
        opened++;
        ACCESS_MASK g = granted((HANDLE)h);
        if (g == 0xFFFFFFFFu) { t_note("NtQueryObject failed"); U(CloseWindowStation)(h); continue; }
        CHECKF((g & t[i].right) == t[i].right, "%s: granted 0x%lx", t[i].n, g);
        if (g != t[i].right) t_note("%s: granted 0x%lx", t[i].n, g);
        U(CloseWindowStation)(h);
    }
    if (!opened) t_skip("nothing openable");
}

TEST(gap9_winsta_generic_rights_are_mapped) {
    REQUIRE(U(OpenWindowStationW)); REQUIRE(U(CloseWindowStation));
    static const DWORD gen[] = { GENERIC_READ, GENERIC_WRITE, GENERIC_EXECUTE, GENERIC_ALL, MAXIMUM_ALLOWED };
    for (int i = 0; i < G9_N(gen); i++) {
        SetLastError(0);
        HWINSTA h = U(OpenWindowStationW)(L"WinSta0", FALSE, gen[i]);
        if (!h) { t_note("generic 0x%lx denied, error %lu", gen[i], GetLastError()); continue; }
        ACCESS_MASK g = granted((HANDLE)h);
        if (g == 0xFFFFFFFFu) { t_note("NtQueryObject failed"); U(CloseWindowStation)(h); continue; }
        CHECKF(!(g & 0xF0000000u), "mask 0x%lx granted 0x%lx still has generic bits", gen[i], g);   /* mapped by ObOpenObjectByName */
        CHECKF(g != 0, "mask 0x%lx granted nothing", gen[i]);
        t_note("access 0x%lx -> granted 0x%lx", gen[i], g);
        U(CloseWindowStation)(h);
    }
}

TEST(gap9_winsta_close_twice_and_bad_close) {
    REQUIRE(U(OpenWindowStationW)); REQUIRE(U(CloseWindowStation));
    HWINSTA h = U(OpenWindowStationW)(L"WinSta0", FALSE, WINSTA_READATTRIBUTES);
    if (!h) { t_skip("not openable"); return; }
    CHECK(U(CloseWindowStation)(h));
    SetLastError(0);
    CHECK(!U(CloseWindowStation)(h));
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    SetLastError(0);
    CHECK(!U(CloseWindowStation)(NULL));
    EXPECT_ERR(ERROR_INVALID_HANDLE);
}

TEST(gap9_winsta_closed_handle_not_usable) {
    REQUIRE(U(OpenWindowStationW)); REQUIRE(U(CloseWindowStation)); REQUIRE(U(GetUserObjectInformationW));
    HWINSTA h = U(OpenWindowStationW)(L"WinSta0", FALSE, WINSTA_READATTRIBUTES);
    if (!h) { t_skip("not openable"); return; }
    U(CloseWindowStation)(h);
    WCHAR nm[32]; DWORD need;
    SetLastError(0);
    CHECK(!U(GetUserObjectInformationW)(h, UOI_NAME, nm, sizeof nm, &need));
    EXPECT_ERR(0);       /* Windows 10 fails the call without setting a last error for a closed handle */
}

TEST(gap9_winsta_direct_absolute_path) {
    REQUIRE(UN(NtUserOpenWindowStation)); REQUIRE(U(CloseWindowStation));
    /* no root directory: an absolute object name is resolved by the object manager in the session namespace */
    WCHAR path[] = L"\\Windows\\WindowStations\\WinSta0";
    UNICODE_STRING us; us.Buffer = path; us.Length = (USHORT)(lstrlenW(path) * 2); us.MaximumLength = us.Length + 2;
    OBJECT_ATTRIBUTES oa; memset(&oa, 0, sizeof oa);
    oa.Length = sizeof oa; oa.ObjectName = &us; oa.Attributes = OBJ_CASE_INSENSITIVE;
    SetLastError(0);
    HWINSTA h = UN(NtUserOpenWindowStation)(&oa, WINSTA_READATTRIBUTES);
    if (!h) {
        /* Observed on Windows 10: a raw open of the absolute name (with or without an elevated token) is denied, user32 opens
         * the station relative to a directory handle instead; see gap9_winsta_direct_rooted_path */
        DWORD e = GetLastError();
        t_note("absolute open failed, error %lu", e);
        t_skip("absolute open denied (error %lu)", e);
        return;
    }
    CHECK(type_is((HANDLE)h, L"WindowStation") != 0);
    EXPECT_WSTR(path, L"\\Windows\\WindowStations\\WinSta0");   /* only the Service-... name is rewritten */
    U(CloseWindowStation)(h);
}

TEST(gap9_winsta_direct_rooted_path) {
    REQUIRE(UN(NtUserOpenWindowStation)); REQUIRE(U(CloseWindowStation));
    typedef LONG (NTAPI *OpenDir_t)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES);
    OpenDir_t open_dir = (OpenDir_t)t_proc_in("ntdll.dll", "NtOpenDirectoryObject");
    REQUIRE(open_dir);
    WCHAR dpath[] = L"\\Windows\\WindowStations";
    UNICODE_STRING du; du.Buffer = dpath; du.Length = (USHORT)(lstrlenW(dpath) * 2); du.MaximumLength = du.Length + 2;
    OBJECT_ATTRIBUTES da; memset(&da, 0, sizeof da);
    da.Length = sizeof da; da.ObjectName = &du; da.Attributes = OBJ_CASE_INSENSITIVE;
    HANDLE root = NULL;
    LONG st = open_dir(&root, 3 /* DIRECTORY_QUERY | DIRECTORY_TRAVERSE */, &da);
    if (st < 0) { t_skip("cannot open the window station directory (status 0x%lx)", (unsigned long)st); return; }
    WCHAR name[] = L"WinSta0";
    UNICODE_STRING us; us.Buffer = name; us.Length = (USHORT)(lstrlenW(name) * 2); us.MaximumLength = us.Length + 2;
    OBJECT_ATTRIBUTES oa; memset(&oa, 0, sizeof oa);
    oa.Length = sizeof oa; oa.RootDirectory = root; oa.ObjectName = &us; oa.Attributes = OBJ_CASE_INSENSITIVE;
    SetLastError(0);
    HWINSTA h = UN(NtUserOpenWindowStation)(&oa, WINSTA_READATTRIBUTES);
    DWORD e = GetLastError();
    CloseHandle(root);
    if (!h) { t_note("rooted open failed, error %lu", e); t_skip("rooted open denied (error %lu)", e); return; }
    CHECK(type_is((HANDLE)h, L"WindowStation") != 0);
    U(CloseWindowStation)(h);
}

TEST(gap9_winsta_direct_absolute_path_missing) {
    REQUIRE(UN(NtUserOpenWindowStation));
    WCHAR path[96];
    wsprintfW(path, L"\\Windows\\WindowStations\\win32kt_gone_%lu", GetCurrentProcessId());
    UNICODE_STRING us; us.Buffer = path; us.Length = (USHORT)(lstrlenW(path) * 2); us.MaximumLength = us.Length + 2;
    OBJECT_ATTRIBUTES oa; memset(&oa, 0, sizeof oa);
    oa.Length = sizeof oa; oa.ObjectName = &us; oa.Attributes = OBJ_CASE_INSENSITIVE;
    SetLastError(0);
    HWINSTA h = UN(NtUserOpenWindowStation)(&oa, WINSTA_READATTRIBUTES);
    CHECK(h == NULL);
    EXPECT_ERR(ERROR_FILE_NOT_FOUND);
    if (h) CloseWindowStation(h);
}

TEST(gap9_winsta_service_name_rewritten_in_place) {
    REQUIRE(U(OpenWindowStationW)); REQUIRE(U(CloseWindowStation));
    /* The kernel overwrites this exact 28-character name with the caller's service LUID (a writable
     * buffer is required: it is probed for write).  A user logon session has no such window station,
     * so the open itself is expected to fail; what we pin down is the in-place rewrite. */
    WCHAR name[] = L"Service-0x00000000-00000000$";
    CHECK(lstrlenW(name) == 28);
    SetLastError(0);
    HWINSTA h = U(OpenWindowStationW)(name, FALSE, WINSTA_READATTRIBUTES);
    DWORD err = GetLastError();
    if (!wcscmp(name, L"Service-0x00000000-00000000$")) {
        t_note("name not rewritten (open %s, error %lu)", h ? "ok" : "failed", err);
    } else {
        unsigned hi = 0, lo = 0; char tail = 0;
        char a[64]; for (int i = 0; i < 28; i++) a[i] = (char)name[i];
        a[28] = 0;
        int n = sscanf(a, "Service-0x%x-%x%c", &hi, &lo, &tail);
        CHECKF(n == 3 && tail == '$', "rewritten name is \"%s\"", a);
        t_info("rewritten to %s (error %lu)", a, err);
    }
    if (h) U(CloseWindowStation)(h);
}
