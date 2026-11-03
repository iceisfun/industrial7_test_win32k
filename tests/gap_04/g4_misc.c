/* Remaining syscalls: mouse move history, auto-rotation, OpenDesktop, injection devices, display configuration
 * early exits and SetSystemCursor negatives. */
#include "gap4.h"

static HWND gui_window(void) { return g4_window(WS_OVERLAPPEDWINDOW, 0, 100, 100, 200, 100, NULL); }

/* ---- NtUserGetMouseMovePointsEx ---------------------------------------------------------------- */
/* cbSize must be sizeof(MOUSEMOVEPOINT) == 0x18 and the count at most 0x40; both fail with 0x57 and -1 before any desktop check. */
TEST(gap4_mousemove_bad_cbsize_fails_87) {
    G4REQ(NtUserGetMouseMovePointsEx);
    MOUSEMOVEPOINT in, out[4]; memset(&in, 0, sizeof in); memset(out, 0, sizeof out);
    static const UINT cb[] = { 0, 1, 8, 0x10, 0x17, 0x19, 0x20, 0x30, 0xffffffff };
    for (unsigned i = 0; i < sizeof cb / sizeof cb[0]; i++) {
        SET_ERR(0);
        int r = UN(NtUserGetMouseMovePointsEx)(cb[i], &in, out, 4, GMMP_USE_DISPLAY_POINTS);
        CHECKF(r == -1, "cb 0x%x ret %d", cb[i], r);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
}
TEST(gap4_mousemove_count_over_64_fails_87) {
    G4REQ(NtUserGetMouseMovePointsEx);
    MOUSEMOVEPOINT in, out[1]; memset(&in, 0, sizeof in);
    static const int n[] = { 0x41, 0x42, 0x100, 0x7fffffff, -1 };
    for (unsigned i = 0; i < sizeof n / sizeof n[0]; i++) {
        SET_ERR(0);
        int r = UN(NtUserGetMouseMovePointsEx)(sizeof(MOUSEMOVEPOINT), &in, out, n[i], GMMP_USE_DISPLAY_POINTS);
        CHECKF(r == -1, "count %d ret %d", n[i], r);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
}
TEST(gap4_mousemove_struct_is_0x18_bytes) {
    CHECK(sizeof(MOUSEMOVEPOINT) == 0x18);
    CHECK(offsetof(MOUSEMOVEPOINT, time) == 8);
    CHECK(offsetof(MOUSEMOVEPOINT, dwExtraInfo) == 16);
}
/* A point (time 1 ms after boot) that the 64-entry history cannot contain: no match gives -1 with ERROR_POINT_NOT_FOUND (0x493). */
TEST(gap4_mousemove_unknown_point_not_found_1171) {
    G4REQ(NtUserGetMouseMovePointsEx); REQUIRE(U(GetCursorPos));
    HWND h = gui_window();     /* converts the thread to a GUI thread on the input desktop */
    if (!h) { t_skip("no window"); return; }
    MOUSEMOVEPOINT in, out[8]; memset(&in, 0, sizeof in); memset(out, 0, sizeof out);
    in.x = 0x1234; in.y = 0x2345; in.time = 1; in.dwExtraInfo = 0;
    SET_ERR(0);
    int r = UN(NtUserGetMouseMovePointsEx)(sizeof in, &in, out, 8, GMMP_USE_DISPLAY_POINTS);
    DWORD e = GetLastError();
    if (r == -1 && e == ERROR_ACCESS_DENIED) { t_skip("thread is not on the input desktop"); g4_destroy(h); return; }
    CHECKF(r == -1, "ret %d", r);
    CHECKF(e == 1171, "gle %lu", e);
    g4_destroy(h);
}
TEST(gap4_mousemove_zero_buffer_count_unknown_point_not_found) {
    G4REQ(NtUserGetMouseMovePointsEx);
    HWND h = gui_window();
    if (!h) { t_skip("no window"); return; }
    MOUSEMOVEPOINT in, out[1]; memset(&in, 0, sizeof in);
    in.x = 0x1234; in.y = 0x2345; in.time = 1;
    SET_ERR(0);
    int r = UN(NtUserGetMouseMovePointsEx)(sizeof in, &in, out, 0, GMMP_USE_DISPLAY_POINTS);
    if (r == -1 && GetLastError() == ERROR_ACCESS_DENIED) { t_skip("not on the input desktop"); g4_destroy(h); return; }
    CHECKF(r == -1 && GetLastError() == 1171, "ret %d gle %lu", r, GetLastError());
    g4_destroy(h);
}
TEST(gap4_mousemove_user32_wrapper_agrees) {
    G4REQ(NtUserGetMouseMovePointsEx); REQUIRE(U(GetMouseMovePointsEx));
    HWND h = gui_window();
    if (!h) { t_skip("no window"); return; }
    MOUSEMOVEPOINT in, out[4]; memset(&in, 0, sizeof in); in.x = 0x1234; in.y = 0x2345; in.time = 1;
    SET_ERR(0);
    int a = U(GetMouseMovePointsEx)(sizeof in, &in, out, 4, GMMP_USE_DISPLAY_POINTS); DWORD ea = GetLastError();
    SET_ERR(0);
    int b = UN(NtUserGetMouseMovePointsEx)(sizeof in, &in, out, 4, GMMP_USE_DISPLAY_POINTS); DWORD eb = GetLastError();
    CHECK(a == b); CHECK(ea == eb);
    g4_destroy(h);
}
/* The current cursor position is pushed into the history by input; if it is found the results must be consistent. */
TEST(gap4_mousemove_found_points_are_ordered_newest_first) {
    G4REQ(NtUserGetMouseMovePointsEx); REQUIRE(U(GetCursorPos)); REQUIRE(U(GetMouseMovePointsEx));
    HWND h = gui_window();
    if (!h) { t_skip("no window"); return; }
    POINT pt; U(GetCursorPos)(&pt);
    MOUSEMOVEPOINT in, out[64]; memset(&in, 0, sizeof in); memset(out, 0, sizeof out);
    in.x = pt.x & 0xffff; in.y = pt.y & 0xffff;
    int r = UN(NtUserGetMouseMovePointsEx)(sizeof in, &in, out, 64, GMMP_USE_DISPLAY_POINTS);
    if (r < 0) { t_note("cursor position %ld,%ld not in history (gle %lu)", pt.x, pt.y, GetLastError()); g4_destroy(h); return; }
    CHECK(r >= 1 && r <= 64);
    for (int i = 1; i < r; i++) CHECKF((int)(out[i - 1].time - out[i].time) >= 0, "entry %d time %lu after %lu", i, out[i].time, out[i - 1].time);
    g4_destroy(h);
}

/* ---- Auto-rotation ----------------------------------------------------------------------------- */
TEST(gap4_autorotation_state_null_fails_87) {
    G4REQ(NtUserGetAutoRotationState);
    SET_ERR(0);
    EXPECT_EQ(UN(NtUserGetAutoRotationState)(NULL), 0);
    EXPECT_ERR(ERROR_INVALID_PARAMETER);
}
/* AR_STATE is a flag set: ENABLED 0, DISABLED 1, SUPPRESSED 2, REMOTESESSION 4, MULTIMON 8, NOSENSOR 0x10, NOT_SUPPORTED 0x20, DOCKED 0x40, LAPTOP 0x80. */
TEST(gap4_autorotation_state_valid_returns_flags) {
    G4REQ(NtUserGetAutoRotationState);
    DWORD st = 0xcccccccc;
    BOOL r = UN(NtUserGetAutoRotationState)(&st);
    CHECK(r == 1);
    CHECKF((st & ~0xffu) == 0, "state 0x%lx", st);
    t_note("AR_STATE = 0x%lx", st);
    DWORD st2 = 0xcccccccc;
    CHECK(UN(NtUserGetAutoRotationState)(&st2) == 1);
    CHECK(st2 == st);
}
TEST(gap4_autorotation_state_matches_user32) {
    G4REQ(NtUserGetAutoRotationState); REQUIRE(U(GetAutoRotationState));
    DWORD a = 0xcc, b = 0xdd;
    BOOL ra = U(GetAutoRotationState)((PAR_STATE)&a), rb = UN(NtUserGetAutoRotationState)(&b);
    CHECK(ra == rb); CHECK(a == b);
}
/* ppi->dwDisplayAutoRotationPreferences (+0x398) is zero-initialised and this process never sets it. */
TEST(gap4_autorotation_prefs_default_is_none) {
    G4REQ(NtUserGetDisplayAutoRotationPreferences);
    HWND h = gui_window();   /* the syscall dereferences the Win32 thread's process info */
    if (!h) { t_skip("no window"); return; }
    DWORD p = 0xcccccccc;
    CHECK(UN(NtUserGetDisplayAutoRotationPreferences)(&p) == 1);
    CHECKF(p == 0, "pref 0x%lx", p);
    g4_destroy(h);
}
TEST(gap4_autorotation_prefs_get_matches_user32) {
    G4REQ(NtUserGetDisplayAutoRotationPreferences); REQUIRE(U(GetDisplayAutoRotationPreferences));
    HWND h = gui_window();
    if (!h) { t_skip("no window"); return; }
    ORIENTATION_PREFERENCE a = (ORIENTATION_PREFERENCE)0x7777; DWORD b = 0x8888;
    BOOL ra = U(GetDisplayAutoRotationPreferences)(&a), rb = UN(NtUserGetDisplayAutoRotationPreferences)(&b);
    CHECK(ra == rb); CHECK((DWORD)a == b);
    g4_destroy(h);
}
/* NtUserSetDisplayAutoRotationPreferences rejects any bit in 0x3ffffff0 with 0x57 and stores nothing. */
TEST(gap4_autorotation_prefs_set_invalid_bits_fail_87) {
    G4REQ(NtUserSetDisplayAutoRotationPreferences); G4REQ(NtUserGetDisplayAutoRotationPreferences);
    HWND h = gui_window();
    if (!h) { t_skip("no window"); return; }
    DWORD before = 0xcc; UN(NtUserGetDisplayAutoRotationPreferences)(&before);
    static const DWORD bad[] = { 0x10, 0x20, 0x100, 0x1000, 0x10000, 0x20000000, 0x3ffffff0, 0x3fffffff };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SET_ERR(0);
        BOOL r = UN(NtUserSetDisplayAutoRotationPreferences)(bad[i]);
        CHECKF(r == 0, "pref 0x%lx ret %d", bad[i], r);
        EXPECT_ERR(ERROR_INVALID_PARAMETER);
    }
    DWORD after = 0xdd; UN(NtUserGetDisplayAutoRotationPreferences)(&after);
    CHECK(before == after);
    g4_destroy(h);
}

/* ---- NtUserOpenDesktop ------------------------------------------------------------------------- */
typedef struct { ULONG Length; HANDLE Root; PUNICODE_STRING Name; ULONG Attr; PVOID Sd; PVOID Sqos; } G4_OA;
static HDESK open_desk(const WCHAR *name, DWORD flags, BOOL inherit, ACCESS_MASK acc) {
    UNICODE_STRING us; us.Buffer = (PWSTR)name; us.Length = (USHORT)(lstrlenW(name) * 2); us.MaximumLength = us.Length + 2;
    G4_OA oa; memset(&oa, 0, sizeof oa);
    oa.Length = 0x30;
    oa.Root = U(GetProcessWindowStation) ? (HANDLE)U(GetProcessWindowStation)() : NULL;
    oa.Name = &us;
    oa.Attr = inherit ? 0x42 : 0x40;
    return UN(NtUserOpenDesktop)(&oa, flags, acc);
}
static int g4_granted(HANDLE h, ULONG *g) {
    typedef LONG (NTAPI *q_t)(HANDLE, ULONG, PVOID, ULONG, PULONG);
    q_t q = (q_t)t_proc_in("ntdll.dll", "NtQueryObject");
    struct { ULONG Attributes; ACCESS_MASK GrantedAccess; ULONG rest[20]; } bi;
    ULONG rl = 0;
    if (!q || q(h, 0, &bi, sizeof bi, &rl) < 0) return 0;
    *g = bi.GrantedAccess; return 1;
}
static int g4_uoi_name(HDESK d, WCHAR *out, DWORD cch) {
    DWORD need = 0;
    return U(GetUserObjectInformationW) && U(GetUserObjectInformationW)(d, UOI_NAME, out, cch * 2, &need);
}
TEST(gap4_opendesktop_default_opens_with_forced_access) {
    G4REQ(NtUserOpenDesktop); REQUIRE(U(CloseDesktop));
    HWND h = gui_window();   /* ensure the process is connected to a window station */
    HDESK d = open_desk(L"Default", 0, FALSE, DESKTOP_ENUMERATE);
    if (!d) { t_skip("OpenDesktop(Default) gle=%lu", GetLastError()); g4_destroy(h); return; }
    WCHAR n[64] = L"";
    CHECK(g4_uoi_name(d, n, 64)); EXPECT_WSTR(n, L"Default");
    ULONG g = 0;
    /* _OpenDesktop ORs 0x81 (DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS) into the access request. */
    if (!t_is_wine() && g4_granted(d, &g)) CHECKF((g & 0x81) == 0x81 && (g & DESKTOP_ENUMERATE), "granted 0x%lx", g);
    U(CloseDesktop)(d);
    g4_destroy(h);
}
TEST(gap4_opendesktop_inherit_attribute) {
    G4REQ(NtUserOpenDesktop); REQUIRE(U(CloseDesktop)); REQUIRE(U(GetHandleInformation));
    HWND h = gui_window();
    HDESK a = open_desk(L"Default", 0, TRUE, DESKTOP_READOBJECTS), b = open_desk(L"Default", 0, FALSE, DESKTOP_READOBJECTS);
    if (!a || !b) { t_skip("open failed gle=%lu", GetLastError()); if (a) U(CloseDesktop)(a); if (b) U(CloseDesktop)(b); g4_destroy(h); return; }
    DWORD fa = 0, fb = 0;
    CHECK(GetHandleInformation((HANDLE)a, &fa)); CHECK(GetHandleInformation((HANDLE)b, &fb));
    CHECK(fa & HANDLE_FLAG_INHERIT); CHECK(!(fb & HANDLE_FLAG_INHERIT));
    CHECK(a != b);
    U(CloseDesktop)(a); U(CloseDesktop)(b);
    g4_destroy(h);
}
/* Name matching is case-insensitive (attributes carry OBJ_CASE_INSENSITIVE 0x40). */
TEST(gap4_opendesktop_name_is_case_insensitive) {
    G4REQ(NtUserOpenDesktop); REQUIRE(U(CloseDesktop));
    HWND h = gui_window();
    HDESK d = open_desk(L"dEFAULT", 0, FALSE, DESKTOP_READOBJECTS);
    if (!d) { t_skip("open failed gle=%lu", GetLastError()); g4_destroy(h); return; }
    WCHAR n[64] = L""; CHECK(g4_uoi_name(d, n, 64)); EXPECT_WSTR(n, L"Default");
    U(CloseDesktop)(d);
    g4_destroy(h);
}
TEST(gap4_opendesktop_nonexistent_fails_file_not_found) {
    G4REQ(NtUserOpenDesktop);
    HWND h = gui_window();
    WCHAR nm[48]; wsprintfW(nm, L"win32kt_nodesk_%lu", GetCurrentProcessId());
    SET_ERR(0);
    HDESK d = open_desk(nm, 0, FALSE, DESKTOP_READOBJECTS);
    CHECK(d == NULL);
    EXPECT_ERR(ERROR_FILE_NOT_FOUND);   /* STATUS_OBJECT_NAME_NOT_FOUND via RtlNtStatusToDosError in NtUserOpenDesktop */
    if (d && U(CloseDesktop)) U(CloseDesktop)(d);
    g4_destroy(h);
}
TEST(gap4_opendesktop_matches_user32_opendesktopw) {
    G4REQ(NtUserOpenDesktop); REQUIRE(U(OpenDesktopW)); REQUIRE(U(CloseDesktop));
    HWND h = gui_window();
    HDESK a = open_desk(L"Default", 0, FALSE, DESKTOP_READOBJECTS);
    HDESK b = U(OpenDesktopW)(L"Default", 0, FALSE, DESKTOP_READOBJECTS);
    if (!a || !b) { t_skip("open failed gle=%lu", GetLastError()); if (a) U(CloseDesktop)(a); if (b) U(CloseDesktop)(b); g4_destroy(h); return; }
    WCHAR na[64] = L"", nb[64] = L""; ULONG ga = 0, gb = 0;
    CHECK(g4_uoi_name(a, na, 64)); CHECK(g4_uoi_name(b, nb, 64)); EXPECT_WSTR(na, nb);
    if (g4_granted(a, &ga) && g4_granted(b, &gb)) CHECK(ga == gb);
    U(CloseDesktop)(a); U(CloseDesktop)(b);
    g4_destroy(h);
}
TEST(gap4_opendesktop_returns_desktop_type_object) {
    G4REQ(NtUserOpenDesktop); REQUIRE(U(CloseDesktop));
    HWND h = gui_window();
    HDESK d = open_desk(L"Default", 0, FALSE, DESKTOP_READOBJECTS);
    if (!d) { t_skip("open failed"); g4_destroy(h); return; }
    typedef LONG (NTAPI *q_t)(HANDLE, ULONG, PVOID, ULONG, PULONG);
    q_t q = (q_t)t_proc_in("ntdll.dll", "NtQueryObject");
    if (q) {
        union { struct { UNICODE_STRING Name; WCHAR buf[64]; } t; char raw[512]; } u; ULONG rl = 0;
        if (q(d, 2, &u, sizeof u, &rl) >= 0 && u.t.Name.Buffer) {
            CHECK(u.t.Name.Length == 14);
            CHECK(!lstrcmpW(u.t.Name.Buffer, L"Desktop") || !wcsncmp(u.t.Name.Buffer, L"Desktop", 7));
        }
    }
    U(CloseDesktop)(d);
    g4_destroy(h);
}

/* ---- NtUserRemoveInjectionDevice --------------------------------------------------------------- */
/* RawInputManagerDeviceObjectResolveHandle(handle, type 3) fails for anything that is not a synthetic pointer device
 * handle: UserSetLastError(6) and FALSE. */
TEST(gap4_removeinjection_invalid_handles_fail_6) {
    G4REQ(NtUserRemoveInjectionDevice);
    HWND h = gui_window();
    static const ULONG_PTR bad[] = { 0, 1, 4, 0x1234, 0xffff, 0x10000, (ULONG_PTR)-1, (ULONG_PTR)-2 };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        SET_ERR(0);
        BOOL r = UN(NtUserRemoveInjectionDevice)((HANDLE)bad[i]);
        CHECKF(r == 0, "handle %p ret %d", (void *)bad[i], r);
        EXPECT_ERR(ERROR_INVALID_HANDLE);
    }
    g4_destroy(h);
}
TEST(gap4_removeinjection_other_object_type_handle_fails_6) {
    G4REQ(NtUserRemoveInjectionDevice); REQUIRE(U(CreateEventW));
    HWND h = gui_window();
    HANDLE ev = U(CreateEventW)(NULL, TRUE, FALSE, NULL);
    if (!ev) { t_skip("no event"); g4_destroy(h); return; }
    SET_ERR(0);
    CHECK(UN(NtUserRemoveInjectionDevice)(ev) == 0);
    EXPECT_ERR(ERROR_INVALID_HANDLE);
    CHECK(WaitForSingleObject(ev, 0) == WAIT_TIMEOUT);   /* the handle is still ours and usable */
    CloseHandle(ev);
    g4_destroy(h);
}
TEST(gap4_removeinjection_user32_alias_agrees) {
    typedef BOOL (WINAPI *rid_t)(HANDLE);
    rid_t rid = (rid_t)t_proc_in("user32.dll", "RemoveInjectionDevice");
    G4REQ(NtUserRemoveInjectionDevice); REQUIRE(rid);
    HWND h = gui_window();
    SET_ERR(0);
    BOOL r = rid((HANDLE)(ULONG_PTR)0x1234);
    CHECK(r == 0); EXPECT_ERR(ERROR_INVALID_HANDLE);
    g4_destroy(h);
}

/* ---- NtUserSetDisplayConfig / NtUserDisplayConfigSetDeviceInfo: early exits only ---------------- */
#define STATUS_INVALID_PARAMETER_ 0xC000000DL
#define STATUS_ACCESS_DENIED_     0xC0000022L
#define STATUS_UNSUCCESSFUL_      0xC0000001L
#define STATUS_BUFFER_TOO_SMALL_  0xC0000023L
/* Count above 0x400 paths is rejected before any array is read: 0xC000000D, or 0xC0000022 / 0xC0000001 when the caller lacks
 * the display-config access or video is not initialised (checked first). */
TEST(gap4_setdisplayconfig_count_over_1024_rejected) {
    G4REQ(NtUserSetDisplayConfig);
    HWND h = gui_window();
    static const UINT counts[] = { 0x401, 0x1000, 0xffff, 0x7fffffff, 0xffffffff };
    for (unsigned i = 0; i < sizeof counts / sizeof counts[0]; i++) {
        LONG st = UN(NtUserSetDisplayConfig)(counts[i], NULL, 0, 0, 0);
        t_note("count 0x%x -> %s", counts[i], k_status_name((K_NTSTATUS)st));
        CHECKF(st == (LONG)STATUS_INVALID_PARAMETER_ || st == (LONG)STATUS_ACCESS_DENIED_ || st == (LONG)STATUS_UNSUCCESSFUL_,
               "count 0x%x status 0x%lx", counts[i], (unsigned long)st);
    }
    g4_destroy(h);
}
TEST(gap4_setdisplayconfig_user32_rejects_null_arrays_with_flags_zero) {
    REQUIRE(U(SetDisplayConfig));
    HWND h = gui_window();
    /* user32!SetDisplayConfig runs SetDisplayConfigValidateParamsUserMode before any syscall; flags 0 is not a valid mode. */
    LONG r = U(SetDisplayConfig)(0, NULL, 0, NULL, 0);
    t_note("SetDisplayConfig(0,NULL,0,NULL,0) = %ld", r);
    if (!t_is_wine()) CHECK(r != ERROR_SUCCESS);   /* SetDisplayConfigValidateParamsUserMode: (flags & 0xc0) must be exactly VALIDATE or APPLY */
    g4_destroy(h);
}
/* Header size below 0x14 -> STATUS_BUFFER_TOO_SMALL after the access check; STATUS_ACCESS_DENIED / UNSUCCESSFUL are the
 * outcomes of the earlier checks. */
TEST(gap4_displayconfig_setdeviceinfo_small_header) {
    G4REQ(NtUserDisplayConfigSetDeviceInfo);
    HWND h = gui_window();
    DWORD hdr[8]; memset(hdr, 0, sizeof hdr);
    static const DWORD sizes[] = { 0, 4, 8, 0x10, 0x13 };
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        hdr[0] = DISPLAYCONFIG_DEVICE_INFO_SET_TARGET_PERSISTENCE; hdr[1] = sizes[i];
        LONG st = UN(NtUserDisplayConfigSetDeviceInfo)(hdr);
        t_note("size 0x%lx -> %s", sizes[i], k_status_name((K_NTSTATUS)st));
        CHECKF(st == (LONG)STATUS_BUFFER_TOO_SMALL_ || st == (LONG)STATUS_ACCESS_DENIED_ || st == (LONG)STATUS_UNSUCCESSFUL_,
               "size 0x%lx status 0x%lx", sizes[i], (unsigned long)st);
    }
    g4_destroy(h);
}
/* user32!DisplayConfigSetDeviceInfo maps the NTSTATUS through RtlNtStatusToDosError. */
TEST(gap4_displayconfig_setdeviceinfo_user32_maps_status) {
    REQUIRE(U(DisplayConfigSetDeviceInfo));
    HWND h = gui_window();
    DWORD hdr[8]; memset(hdr, 0, sizeof hdr);
    hdr[0] = DISPLAYCONFIG_DEVICE_INFO_SET_TARGET_PERSISTENCE; hdr[1] = 4;
    LONG r = U(DisplayConfigSetDeviceInfo)((DISPLAYCONFIG_DEVICE_INFO_HEADER *)hdr);
    t_note("DisplayConfigSetDeviceInfo(size 4) = %ld", r);
    CHECKF(r == ERROR_INSUFFICIENT_BUFFER || r == ERROR_ACCESS_DENIED || r == ERROR_GEN_FAILURE, "got %ld", r);
    g4_destroy(h);
}

/* ---- NtUserSetSystemCursor negatives ------------------------------------------------------------ */
/* zzzSetSystemCursor scans gasyscur for the id (19 entries) before touching anything; an unknown id returns FALSE. */
TEST(gap4_setsystemcursor_unknown_id_returns_false) {
    G4REQ(NtUserSetSystemCursor); REQUIRE(U(LoadCursorW));
    HWND h = gui_window();
    HCURSOR c = U(LoadCursorW)(NULL, G4_IDC_ARROW);
    if (!c) { t_skip("no cursor"); g4_destroy(h); return; }
    static const DWORD ids[] = { 0, 1, 0x1234, 32511, 32600, 32649 + 100, 0xffffffff };
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        BOOL r = UN(NtUserSetSystemCursor)(c, ids[i]);
        CHECKF(r == 0, "id %lu ret %d", ids[i], r);
    }
    CHECK(U(GetCursorInfo) != NULL);
    g4_destroy(h);
}
TEST(gap4_setsystemcursor_invalid_cursor_handle_returns_false) {
    G4REQ(NtUserSetSystemCursor);
    HWND h = gui_window();
    static const ULONG_PTR bad[] = { 0, 1, 0x1234, 0xffff, 0x1FFF0, (ULONG_PTR)-1 };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        BOOL r = UN(NtUserSetSystemCursor)((HCURSOR)bad[i], OCR_NORMAL);   /* HMValidateHandle(h, TYPE_CURSOR) fails */
        CHECKF(r == 0, "cursor %p ret %d", (void *)bad[i], r);
    }
    g4_destroy(h);
}
/* A window handle has the wrong USER object type for HMValidateHandle(.., 3). */
TEST(gap4_setsystemcursor_window_handle_returns_false) {
    G4REQ(NtUserSetSystemCursor);
    HWND h = gui_window();
    if (!h) { t_skip("no window"); return; }
    EXPECT_EQ(UN(NtUserSetSystemCursor)((HCURSOR)h, OCR_NORMAL), 0);
    CHECK(U(IsWindow)(h));
    g4_destroy(h);
}
TEST(gap4_setsystemcursor_menu_handle_returns_false) {
    G4REQ(NtUserSetSystemCursor); REQUIRE(U(CreatePopupMenu)); REQUIRE(U(DestroyMenu));
    HMENU m = U(CreatePopupMenu)();
    if (!m) { t_skip("no menu"); return; }
    EXPECT_EQ(UN(NtUserSetSystemCursor)((HCURSOR)m, OCR_NORMAL), 0);
    U(DestroyMenu)(m);
}
/* The failing call must not have destroyed the cursor passed in: LoadCursor handles are shared and stay valid. */
TEST(gap4_setsystemcursor_failure_keeps_cursor_valid) {
    G4REQ(NtUserSetSystemCursor); REQUIRE(U(LoadCursorW)); REQUIRE(U(GetIconInfo));
    HWND h = gui_window();
    HCURSOR c = U(LoadCursorW)(NULL, G4_IDC_CROSS);
    if (!c) { t_skip("no cursor"); g4_destroy(h); return; }
    UN(NtUserSetSystemCursor)(c, 0x1234);
    ICONINFO ii;
    CHECK(U(GetIconInfo)(c, &ii));
    if (ii.hbmMask) U(DeleteObject)(ii.hbmMask);
    if (ii.hbmColor) U(DeleteObject)(ii.hbmColor);
    g4_destroy(h);
}

/* ---- LockWorkStation: never invoked ------------------------------------------------------------- */
/* NtUserLockWorkStation posts the winlogon lock message when CheckWinstaAttributeAccess(WINSTA_WRITEATTRIBUTES) passes, which
 * every interactive process does, so no test calls it; only the stub is validated (gap4_stub_service_numbers). */
TEST(gap4_lockworkstation_stub_present_not_invoked) {
    FARPROC p = t_proc_in("win32u.dll", "NtUserLockWorkStation");
    REQUIRE(p);
    if (t_is_wine()) { t_note("Wine stub layout differs"); return; }
    CHECK(k_stub_valid(p));
    CHECK(k_stub_number(p) == 5239);
    CHECK(t_proc_in("user32.dll", "LockWorkStation") != NULL);
}
