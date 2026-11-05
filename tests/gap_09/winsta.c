/* gap9_winsta*: NtUserOpenWindowStation (reached by user32!OpenWindowStationA/W).
 *
 * Analysis: NtUserOpenWindowStation(POBJECT_ATTRIBUTES oa, ACCESS_MASK access).  The special object name
 * "Service-0x00000000-00000000$" (Length 0x38, MaximumLength 0x3A, RootDirectory != 0) is rewritten IN THE
 * CALLER'S BUFFER to "Service-0x<luid hi>-<luid lo>$" before the open.  Everything else goes straight to
 * _OpenWindowStation (object manager open of a WindowStation object, so access mapping, inherit flag and
 * NTSTATUS->Win32 error follow the object manager rules). */
#include "g9.h"
#include <winternl.h>
#include <sddl.h>

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

/* Object names for the raw syscall.  user32 opens the station relative to the session's window station directory
 * ("\Sessions\<n>\Windows\WindowStations"); the name "\Windows\WindowStations" resolves in the system (session 0)
 * namespace instead, where the interactive station is not reachable for any token. */
typedef LONG (NTAPI *G9_OpenDir_t)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES);
#define G9_DIR_TRAVERSE 2u
#define G9_DIR_QUERY    1u
#define G9_STATUS_ACCESS_DENIED ((LONG)0xC0000022)
static void g9_session_dir(WCHAR *out) { DWORD sid = 0; ProcessIdToSessionId(GetCurrentProcessId(), &sid); wsprintfW(out, L"\\Sessions\\%lu\\Windows\\WindowStations", sid); }
static LONG g9_open_dir(const WCHAR *path, ACCESS_MASK acc, HANDLE *out) {
    G9_OpenDir_t f = (G9_OpenDir_t)t_proc_in("ntdll.dll", "NtOpenDirectoryObject");
    WCHAR buf[160]; lstrcpyW(buf, path);
    UNICODE_STRING us; us.Buffer = buf; us.Length = (USHORT)(lstrlenW(buf) * 2); us.MaximumLength = us.Length + 2;
    OBJECT_ATTRIBUTES oa; memset(&oa, 0, sizeof oa);
    oa.Length = sizeof oa; oa.ObjectName = &us; oa.Attributes = OBJ_CASE_INSENSITIVE;
    *out = NULL;
    return f ? f(out, acc, &oa) : (LONG)0xC0000002;
}
static HWINSTA g9_raw_open(const WCHAR *name, HANDLE root, ACCESS_MASK acc, DWORD *err) {
    WCHAR buf[200]; lstrcpyW(buf, name);
    UNICODE_STRING us; us.Buffer = buf; us.Length = (USHORT)(lstrlenW(buf) * 2); us.MaximumLength = us.Length + 2;
    OBJECT_ATTRIBUTES oa; memset(&oa, 0, sizeof oa);
    oa.Length = sizeof oa; oa.RootDirectory = root; oa.ObjectName = &us; oa.Attributes = OBJ_CASE_INSENSITIVE;
    SetLastError(0);
    HWINSTA h = UN(NtUserOpenWindowStation)(&oa, acc);
    *err = GetLastError();
    return h;
}

TEST(gap9_winsta_direct_absolute_path) {
    REQUIRE(UN(NtUserOpenWindowStation)); REQUIRE(U(CloseWindowStation));
    /* no root directory: the absolute name is resolved by the object manager; the session's own namespace path works */
    WCHAR path[200], dir[160];
    g9_session_dir(dir); lstrcpyW(path, dir); lstrcatW(path, L"\\WinSta0");
    DWORD e;
    HWINSTA h = g9_raw_open(path, NULL, WINSTA_READATTRIBUTES, &e);
    CHECKF(h != NULL, "raw absolute open failed, error %lu", e);
    if (!h) return;
    CHECK(type_is((HANDLE)h, L"WindowStation") != 0);
    CHECK(granted((HANDLE)h) & WINSTA_READATTRIBUTES);
    U(CloseWindowStation)(h);
}

TEST(gap9_winsta_direct_rooted_path) {
    REQUIRE(UN(NtUserOpenWindowStation)); REQUIRE(U(CloseWindowStation));
    WCHAR dir[160]; g9_session_dir(dir);
    HANDLE root = NULL;
    LONG st = g9_open_dir(dir, G9_DIR_TRAVERSE, &root);          /* traverse is enough for a rooted open, for both tokens */
    CHECKF(st >= 0, "session window station directory not openable, status %lu", (unsigned long)(ULONG)st);
    if (st < 0) return;
    DWORD e;
    HWINSTA h = g9_raw_open(L"WinSta0", root, WINSTA_READATTRIBUTES, &e);
    CloseHandle(root);
    CHECKF(h != NULL, "raw rooted open failed, error %lu", e);
    if (!h) return;
    CHECK(type_is((HANDLE)h, L"WindowStation") != 0);
    U(CloseWindowStation)(h);
}

TEST(gap9_winsta_direct_absolute_global_path_denied) {
    REQUIRE(UN(NtUserOpenWindowStation)); REQUIRE(U(CloseWindowStation));
    /* "\Windows\WindowStations\WinSta0" is the system namespace's station: refused with access denied whatever the
     * access mask and attributes, for the administrator and the restricted token alike */
    static const ACCESS_MASK acc[] = { WINSTA_READATTRIBUTES, 0x37F, MAXIMUM_ALLOWED, 0 };
    for (unsigned i = 0; i < sizeof acc / sizeof acc[0]; i++) {
        DWORD e;
        HWINSTA h = g9_raw_open(L"\\Windows\\WindowStations\\WinSta0", NULL, acc[i], &e);
        CHECKF(h == NULL, "access 0x%lx unexpectedly opened", (unsigned long)acc[i]);
        if (h) { U(CloseWindowStation)(h); continue; }
        CHECKF(e == ERROR_ACCESS_DENIED, "access 0x%lx error %lu", (unsigned long)acc[i], e);
    }
}

TEST(gap9_winsta_direct_rooted_global_directory_denied) {
    REQUIRE(UN(NtUserOpenWindowStation)); REQUIRE(U(CloseWindowStation));
    HANDLE root = NULL;
    LONG st = g9_open_dir(L"\\Windows\\WindowStations", G9_DIR_TRAVERSE, &root);
    CHECKF(st >= 0, "global directory not openable for traverse, status %lu", (unsigned long)(ULONG)st);
    if (st < 0) return;
    DWORD e;
    HWINSTA h = g9_raw_open(L"WinSta0", root, WINSTA_READATTRIBUTES, &e);   /* the directory is reachable, the station in it is not */
    CHECK(h == NULL);
    if (h) U(CloseWindowStation)(h); else EXPECT_ERR(ERROR_ACCESS_DENIED);
    CloseHandle(root);
}

TEST(gap9_winsta_direct_directory_query_access_depends_on_token) {
    REQUIRE(UN(NtUserOpenWindowStation));
    /* querying the window station directories needs administrator rights: the High integrity administrator token may,
     * the Medium restricted token is refused with STATUS_ACCESS_DENIED (traverse alone is granted to both) */
    HANDLE a = NULL, b = NULL;
    WCHAR dir[160]; g9_session_dir(dir);
    LONG s1 = g9_open_dir(dir, G9_DIR_QUERY | G9_DIR_TRAVERSE, &a);
    LONG s2 = g9_open_dir(L"\\Windows\\WindowStations", G9_DIR_QUERY | G9_DIR_TRAVERSE, &b);
    if (a) CloseHandle(a);
    if (b) CloseHandle(b);
    if (k_integrity_level() >= SECURITY_MANDATORY_HIGH_RID) {
        CHECKF(s1 >= 0, "session directory query status %lu", (unsigned long)(ULONG)s1);
        CHECKF(s2 >= 0, "global directory query status %lu", (unsigned long)(ULONG)s2);
    } else {
        CHECK(s1 == G9_STATUS_ACCESS_DENIED);
        CHECK(s2 == G9_STATUS_ACCESS_DENIED);
    }
}

/* An impersonation token restricted to a SID that no window station ACL grants: the station's own access check refuses. */
static HANDLE g9_restricted_impersonation(int unrelated_sid) {
    HANDLE pt = NULL, rt = NULL, it = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY, &pt)) return NULL;
    BYTE sid[SECURITY_MAX_SID_SIZE]; DWORD n = sizeof sid;
    SID_AND_ATTRIBUTES rs; memset(&rs, 0, sizeof rs); rs.Sid = sid;
    if (unrelated_sid) {
        SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
        PSID s = NULL;
        if (!AllocateAndInitializeSid(&nt, 5, 21, 1111111111, 2222222222u, 3333333333u, 4444, 0, 0, 0, &s)) { CloseHandle(pt); return NULL; }
        CopySid(sizeof sid, sid, s); FreeSid(s);
    } else if (!CreateWellKnownSid(WinWorldSid, NULL, sid, &n)) { CloseHandle(pt); return NULL; }
    if (CreateRestrictedToken(pt, 0, 0, NULL, 0, NULL, 1, &rs, &rt)) {
        if (!DuplicateTokenEx(rt, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, TokenImpersonation, &it)) it = NULL;
        CloseHandle(rt);
    }
    CloseHandle(pt);
    return it;
}

TEST(gap9_winsta_direct_open_denied_for_restricted_impersonation) {
    REQUIRE(UN(NtUserOpenWindowStation)); REQUIRE(U(CloseWindowStation)); REQUIRE(U(OpenWindowStationW));
    WCHAR dir[160], path[200];
    g9_session_dir(dir); lstrcpyW(path, dir); lstrcatW(path, L"\\WinSta0");
    HANDLE root = NULL;
    LONG st = g9_open_dir(dir, G9_DIR_TRAVERSE, &root);
    CHECKF(st >= 0, "session window station directory not openable, status %lu", (unsigned long)(ULONG)st);
    if (st < 0) return;
    for (int kind = 0; kind < 2; kind++) {
        HANDLE it = g9_restricted_impersonation(kind);
        CHECKF(it != NULL, "restricted impersonation token (%d) not created, error %lu", kind, GetLastError());
        if (!it) continue;
        CHECK(SetThreadToken(NULL, it));
        DWORD ea, er, eu;
        HWINSTA ha = g9_raw_open(path, NULL, WINSTA_READATTRIBUTES, &ea);
        HWINSTA hr = g9_raw_open(L"WinSta0", root, WINSTA_READATTRIBUTES, &er);
        SetLastError(0);
        HWINSTA hu = U(OpenWindowStationW)(L"WinSta0", FALSE, WINSTA_READATTRIBUTES);
        eu = GetLastError();
        SetThreadToken(NULL, NULL);
        CHECKF(ha == NULL, "absolute open with restricted token %d succeeded", kind);
        if (!ha) CHECKF(ea == ERROR_ACCESS_DENIED, "absolute error %lu", ea);
        CHECKF(hr == NULL, "rooted open with restricted token %d succeeded", kind);
        if (!hr) CHECKF(er == ERROR_ACCESS_DENIED, "rooted error %lu", er);
        CHECKF(hu == NULL, "user32 open with restricted token %d succeeded", kind);
        if (!hu) CHECKF(eu == ERROR_ACCESS_DENIED, "user32 error %lu", eu);
        if (ha) U(CloseWindowStation)(ha);
        if (hr) U(CloseWindowStation)(hr);
        if (hu) U(CloseWindowStation)(hu);
        /* the same calls succeed again once the thread has reverted */
        DWORD e;
        HWINSTA h = g9_raw_open(path, NULL, WINSTA_READATTRIBUTES, &e);
        CHECKF(h != NULL, "open after revert failed, error %lu", e);
        if (h) U(CloseWindowStation)(h);
        CloseHandle(it);
    }
    CloseHandle(root);
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
