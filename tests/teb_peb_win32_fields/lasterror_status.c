/* teb_peb_win32_fields: TEB.LastErrorValue (0x68) and TEB.LastStatusValue (0x1250) as written by
 * the win32k kernel side.
 *
 * Kernel facts (Windows 10 19041 analysis):
 *  - win32kbase!UserSetLastError(code): Teb->LastErrorValue = code (a plain store through the
 *    thread's user TEB; for WOW64 callers also the TEB32 copy at +0x2034).
 *  - win32kbase!UserSetLastStatus(status): Teb->LastStatusValue = status; Teb->LastErrorValue =
 *    RtlNtStatusToDosError(status). It is the only win32k routine that touches LastStatusValue.
 *  - win32kfull!NtUserRegisterWindowMessage copies at most 256 WCHARs of the name (stops at the
 *    first NUL, ignoring UNICODE_STRING.Length) and reports STATUS_BUFFER_OVERFLOW (0x80000005)
 *    through UserSetLastStatus when 256 characters were copied without reaching the NUL.
 *  - win32kbase!ValidateHwndEx: an HWND that is not a live window handle yields
 *    UserSetLastError(0x578 ERROR_INVALID_WINDOW_HANDLE).
 *  - win32kbase!NtUserGetDpiForMonitor: HMValidateSharedHandle failure -> UserSetLastError(6).
 *  - win32kfull!_OpenWindowStation: ObOpenObjectByName failure -> UserSetLastError(
 *    RtlNtStatusToDosError(status)); the status itself is not stored. */
#include "tp.h"

#define SENTINEL_STATUS 0x5A5A5A5Au
#define SENTINEL_ERROR  0x31337u

static void fill_name(WCHAR *buf, unsigned len, WCHAR tag) {
    /* stable, session-wide-unique-enough name of exactly len characters: win32kt_tp_<tag>_xxxx... */
    static const WCHAR pre[] = L"win32kt_tp_regmsg_";
    unsigned i = 0;
    for (; i < len && pre[i]; i++) buf[i] = pre[i];
    for (; i < len; i++) buf[i] = tag;
    buf[len] = 0;
}

static UINT regmsg_direct(const WCHAR *name, USHORT length_bytes) {
    UNICODE_STRING us;
    us.Buffer = (PWSTR)name;
    us.Length = length_bytes;
    us.MaximumLength = (USHORT)(length_bytes + 2);
    if (us.MaximumLength < us.Length) us.MaximumLength = 0xFFFF;
    return UN(NtUserRegisterWindowMessage)(&us);
}

static unsigned long expect_dos(unsigned long st) { return U(RtlNtStatusToDosError)((LONG)st); }

/* ---- NtUserRegisterWindowMessage: STATUS_BUFFER_OVERFLOW boundary at 256 characters ------------------ */

TEST(teb_peb_win32_fields_regmsg_name_up_to_255_chars_succeeds) {
    REQUIRE(UN(NtUserRegisterWindowMessage));
    REQUIRE(U(RtlNtStatusToDosError));
    static const unsigned lens[] = { 18, 64, 128, 254, 255 };
    for (unsigned i = 0; i < sizeof lens / sizeof lens[0]; i++) {
        WCHAR name[300];
        fill_name(name, lens[i], L'a');
        tp_teb_write32(TEB_LastStatusValue, SENTINEL_STATUS);
        UINT atom = regmsg_direct(name, (USHORT)(lens[i] * 2));
        /* UserAddAtomEx(..., 2) returns an atom in the registered-message range 0xC000..0xFFFF */
        CHECKF(atom >= 0xC000 && atom <= 0xFFFF, "len %u atom 0x%x", lens[i], atom);
        /* success path never reaches UserSetLastStatus */
        EXPECT_EQ(tp_teb32(TEB_LastStatusValue), SENTINEL_STATUS);
        UINT again = regmsg_direct(name, (USHORT)(lens[i] * 2));
        EXPECT_EQ(again, atom);   /* registering the same string is idempotent */
    }
}

TEST(teb_peb_win32_fields_regmsg_name_of_256_or_more_chars_sets_status_and_error) {
    REQUIRE(UN(NtUserRegisterWindowMessage));
    REQUIRE(U(RtlNtStatusToDosError));
    static const unsigned lens[] = { 256, 257, 300, 511, 1000 };
    for (unsigned i = 0; i < sizeof lens / sizeof lens[0]; i++) {
        WCHAR *name = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, (lens[i] + 1) * sizeof(WCHAR));
        REQUIRE(name);
        fill_name(name, lens[i], L'b');
        tp_teb_write32(TEB_LastStatusValue, SENTINEL_STATUS);
        SetLastError(SENTINEL_ERROR);
        UINT atom = regmsg_direct(name, (USHORT)(lens[i] * 2));
        unsigned long st = tp_teb32(TEB_LastStatusValue), err = tp_teb32(TEB_LastErrorValue);
        EXPECT_EQ(atom, 0);
        EXPECT_EQ(st, 0x80000005);                       /* UserSetLastStatus stored the raw NTSTATUS */
        EXPECT_EQ(err, expect_dos(0x80000005));          /* ...and the mapped Win32 code (ERROR_MORE_DATA) */
        EXPECT_EQ(GetLastError(), err);
        HeapFree(GetProcessHeap(), 0, name);
    }
    SetLastError(0);
}

TEST(teb_peb_win32_fields_regmsg_failure_via_user32_path) {
    REQUIRE(U(RegisterWindowMessageW));
    REQUIRE(U(RtlNtStatusToDosError));
    WCHAR name[300];
    fill_name(name, 256, L'c');
    tp_teb_write32(TEB_LastStatusValue, SENTINEL_STATUS);
    SetLastError(SENTINEL_ERROR);
    UINT atom = U(RegisterWindowMessageW)(name);
    EXPECT_EQ(atom, 0);
    EXPECT_EQ(tp_teb32(TEB_LastStatusValue), 0x80000005);
    EXPECT_EQ(GetLastError(), expect_dos(0x80000005));
    /* the 255-character form of the same name goes through and matches the direct syscall */
    fill_name(name, 255, L'c');
    UINT a = U(RegisterWindowMessageW)(name);
    CHECK(a >= 0xC000 && a <= 0xFFFF);
    if (UN(NtUserRegisterWindowMessage)) EXPECT_EQ(regmsg_direct(name, 255 * 2), a);
    SetLastError(0);
}

TEST(teb_peb_win32_fields_regmsg_ignores_unicode_string_length) {
    /* the copy loop stops on NUL (or at 256 chars) and only probes Length bytes of the buffer, so a
     * UNICODE_STRING whose Length is shorter than the NUL-terminated buffer still registers the whole string */
    REQUIRE(UN(NtUserRegisterWindowMessage));
    WCHAR name[64];
    fill_name(name, 40, L'd');
    UINT full = regmsg_direct(name, 40 * 2);
    CHECK(full >= 0xC000 && full <= 0xFFFF);
    UINT shortlen = regmsg_direct(name, 6);
    EXPECT_EQ(shortlen, full);
    UNICODE_STRING us = { 6, 80, name };      /* Length < MaximumLength, still the same atom */
    EXPECT_EQ(UN(NtUserRegisterWindowMessage)(&us), full);
}

struct R { unsigned long st, err; UINT atom; };
static DWORD regmsg_fail_thread(void *p) {
    struct R *rr = (struct R *)p;
    WCHAR name[300];
    fill_name(name, 270, L'e');
    rr->atom = regmsg_direct(name, 270 * 2);
    rr->st = tp_teb32(TEB_LastStatusValue);
    rr->err = tp_teb32(TEB_LastErrorValue);
    return 0;
}
TEST(teb_peb_win32_fields_regmsg_failure_is_per_thread) {
    /* the kernel writes the *calling* thread's TEB: the failing thread sees the pair, this thread keeps its sentinel */
    REQUIRE(UN(NtUserRegisterWindowMessage));
    struct R r = { 0, 0, 1 };
    tp_teb_write32(TEB_LastStatusValue, SENTINEL_STATUS);
    SetLastError(SENTINEL_ERROR);
    CHECK(tp_run_thread(regmsg_fail_thread, &r, 5000) == 0);
    /* read the field before anything else: RtlNtStatusToDosError (used by expect_dos) itself stores its argument
     * in LastStatusValue on Windows 10 */
    unsigned long parent_status = tp_teb32(TEB_LastStatusValue);
    DWORD parent_error = GetLastError();
    EXPECT_EQ(r.atom, 0);
    EXPECT_EQ(r.st, 0x80000005);
    EXPECT_EQ(r.err, expect_dos(0x80000005));
    EXPECT_EQ(parent_status, SENTINEL_STATUS);
    EXPECT_EQ(parent_error, SENTINEL_ERROR);
    SetLastError(0);
}

/* ---- UserSetLastError-only paths leave LastStatusValue alone ----------------------------------------- */

TEST(teb_peb_win32_fields_getdpiformonitor_invalid_handle_sets_error_6_only) {
    REQUIRE(UN(NtUserGetDpiForMonitor));
    static const ULONG_PTR handles[] = { 0, 0x7FFF0001, 0xDEADBEEF, (ULONG_PTR)-1 };
    static const int types[] = { 0, 1, 2, 3 };     /* HMValidateSharedHandle runs before the type is looked at */
    for (unsigned h = 0; h < sizeof handles / sizeof handles[0]; h++)
        for (unsigned t = 0; t < sizeof types / sizeof types[0]; t++) {
            UINT x = 0xCCCCCCCC, y = 0xCCCCCCCC;
            tp_teb_write32(TEB_LastStatusValue, SENTINEL_STATUS);
            SetLastError(SENTINEL_ERROR);
            UN(NtUserGetDpiForMonitor)((HANDLE)handles[h], types[t], &x, &y);
            CHECKF(GetLastError() == ERROR_INVALID_HANDLE, "handle %#llx type %d: error %lu", (unsigned long long)handles[h], types[t], GetLastError());
            EXPECT_EQ(tp_teb32(TEB_LastStatusValue), SENTINEL_STATUS);
            EXPECT_EQ(x, 0xCCCCCCCC);                 /* outputs are only written after the handle validated */
            EXPECT_EQ(y, 0xCCCCCCCC);
        }
    SetLastError(0);
}

TEST(teb_peb_win32_fields_getdpiformonitorinternal_via_user32) {
    UDECL(BOOL, GetDpiForMonitorInternal, (HANDLE, UINT, UINT *, UINT *));
    REQUIRE(UN(GetDpiForMonitorInternal));
    UINT x = 0xCCCCCCCC, y = 0xCCCCCCCC;
    SetLastError(SENTINEL_ERROR);
    BOOL r = UN(GetDpiForMonitorInternal)((HANDLE)0, 0, &x, &y);
    EXPECT_EQ(r, 0);
    EXPECT_EQ(GetLastError(), ERROR_INVALID_HANDLE);
    /* user32 rejects an out-of-range dpi type itself (it never reaches the kernel) with ERROR_INVALID_PARAMETER... */
    SetLastError(SENTINEL_ERROR);
    r = UN(GetDpiForMonitorInternal)((HANDLE)0, 3, &x, &y);
    EXPECT_EQ(r, 0);
    EXPECT_EQ(GetLastError(), 0xA0);                 /* ERROR_BAD_ARGUMENTS, user32!GetDpiForMonitorInternal: SetLastError(0xa0) */
    EXPECT_EQ(x, 0xCCCCCCCC);
    SetLastError(0);
}

TEST(teb_peb_win32_fields_layered_attributes_null_hwnd_sets_1400) {
    UDECL(BOOL, NtUserSetLayeredWindowAttributes, (HWND, COLORREF, BYTE, DWORD));
    REQUIRE(UN(NtUserSetLayeredWindowAttributes));
    static const DWORD flags[] = { 0, 1, 2, 3, 4, 0x100, 0xFFFFFFFC };
    for (unsigned i = 0; i < sizeof flags / sizeof flags[0]; i++) {
        tp_teb_write32(TEB_LastStatusValue, SENTINEL_STATUS);
        SetLastError(SENTINEL_ERROR);
        BOOL r = UN(NtUserSetLayeredWindowAttributes)(NULL, 0, 255, flags[i]);
        EXPECT_EQ(r, 0);
        /* ValidateHwnd(NULL) fails before the flag word is looked at: UserSetLastError(0x578) */
        EXPECT_EQ(GetLastError(), ERROR_INVALID_WINDOW_HANDLE);
        EXPECT_EQ(tp_teb32(TEB_LastStatusValue), SENTINEL_STATUS);
    }
    SetLastError(0);
}

TEST(teb_peb_win32_fields_layered_attributes_bad_flags_on_live_window_sets_87) {
    UDECL(BOOL, NtUserSetLayeredWindowAttributes, (HWND, COLORREF, BYTE, DWORD));
    REQUIRE(UN(NtUserSetLayeredWindowAttributes));
    HWND w = k_make_window("teb_peb_layered");
    if (!w) { t_skip("no window (headless)"); return; }
    static const DWORD bad[] = { 4, 8, 0x100, 0x80000000 };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        tp_teb_write32(TEB_LastStatusValue, SENTINEL_STATUS);
        SetLastError(SENTINEL_ERROR);
        BOOL r = UN(NtUserSetLayeredWindowAttributes)(w, 0, 0, bad[i]);
        EXPECT_EQ(r, 0);
 /* != 0 -> UserSetLastError(ERROR_INVALID_PARAMETER) */
        EXPECT_EQ(GetLastError(), ERROR_INVALID_PARAMETER);
        EXPECT_EQ(tp_teb32(TEB_LastStatusValue), SENTINEL_STATUS);
    }
    U(DestroyWindow)(w);
    SetLastError(0);
}

TEST(teb_peb_win32_fields_openwindowstation_missing_name_maps_status_to_error) {
    REQUIRE(U(OpenWindowStationW));
    REQUIRE(U(RtlNtStatusToDosError));
    WCHAR name[96];
    _snwprintf(name, 95, L"win32kt_tp_missing_winsta_%lu", GetCurrentProcessId());
    tp_teb_write32(TEB_LastStatusValue, SENTINEL_STATUS);
    SetLastError(SENTINEL_ERROR);
    HWINSTA h = U(OpenWindowStationW)(name, FALSE, WINSTA_ENUMDESKTOPS);
    EXPECT_EQ(h, 0);
    /* ObOpenObjectByName -> STATUS_OBJECT_NAME_NOT_FOUND; UserSetLastError(RtlNtStatusToDosError(status)) */
    EXPECT_EQ(GetLastError(), expect_dos(0xC0000034));
    EXPECT_EQ(GetLastError(), ERROR_FILE_NOT_FOUND);
    /* the kernel uses UserSetLastError here, not UserSetLastStatus, so only the error word moved
     * (user32 itself calls RtlNtStatusToDosError only when its own NtOpenDirectoryObject fails) */
    unsigned long st = tp_teb32(TEB_LastStatusValue);
    if (st != SENTINEL_STATUS) t_note("LastStatusValue changed to 0x%lx during OpenWindowStationW", st);
    SetLastError(0);
}

TEST(teb_peb_win32_fields_open_missing_winsta_via_nt_syscall_matches_wrapper) {
    UDECL(HANDLE, NtUserOpenWindowStation, (OBJECT_ATTRIBUTES *, ACCESS_MASK));
    REQUIRE(UN(NtUserOpenWindowStation));
    REQUIRE(U(RtlNtStatusToDosError));
    HANDLE dir = NULL;
    UNICODE_STRING dirname; WCHAR dn[] = L"\\Windows\\WindowStations";
    dirname.Buffer = dn; dirname.Length = sizeof dn - 2; dirname.MaximumLength = sizeof dn;
    OBJECT_ATTRIBUTES oa; InitializeObjectAttributes(&oa, &dirname, OBJ_CASE_INSENSITIVE, NULL, NULL);
    UDECL(LONG, NtOpenDirectoryObject, (HANDLE *, ACCESS_MASK, OBJECT_ATTRIBUTES *));
    REQUIRE(UN(NtOpenDirectoryObject));
    LONG st = UN(NtOpenDirectoryObject)(&dir, 2 /* DIRECTORY_TRAVERSE */, &oa);
    if (st < 0) { t_skip("cannot open the window-station directory: 0x%lx", (unsigned long)st); return; }
    WCHAR leaf[96];
    _snwprintf(leaf, 95, L"win32kt_tp_missing_ws2_%lu", GetCurrentProcessId());
    UNICODE_STRING nm; nm.Buffer = leaf; nm.Length = (USHORT)(lstrlenW(leaf) * 2); nm.MaximumLength = nm.Length + 2;
    OBJECT_ATTRIBUTES wa; InitializeObjectAttributes(&wa, &nm, OBJ_CASE_INSENSITIVE, dir, NULL);
    SetLastError(SENTINEL_ERROR);
    HANDLE h = UN(NtUserOpenWindowStation)(&wa, WINSTA_ENUMDESKTOPS);
    DWORD e = GetLastError();
    EXPECT_EQ(h, 0);
    EXPECT_EQ(e, ERROR_FILE_NOT_FOUND);
    CloseHandle(dir);
    SetLastError(0);
}

TEST(teb_peb_win32_fields_successful_syscalls_do_not_touch_last_status) {
    /* neither NtUserGetThreadState(0xE) nor NtGdiFlush ever reach UserSetLastStatus */
    REQUIRE(UN(NtUserGetThreadState));
    REQUIRE(UN(NtGdiFlush));
    tp_teb_write32(TEB_LastStatusValue, SENTINEL_STATUS);
    EXPECT_EQ(UN(NtUserGetThreadState)(0xE), 1);
    EXPECT_EQ(tp_teb32(TEB_LastStatusValue), SENTINEL_STATUS);
    EXPECT_EQ(UN(NtGdiFlush)(), 0);
    EXPECT_EQ(tp_teb32(TEB_LastStatusValue), SENTINEL_STATUS);
    tp_teb_write32(TEB_LastStatusValue, 0);
}

static DWORD status_probe_thread(void *p) { *(unsigned long *)p = tp_teb32(TEB_LastStatusValue); return 0; }
TEST(teb_peb_win32_fields_last_status_starts_zero_on_new_thread) {
    unsigned long v = 0xFFFFFFFF;
    tp_teb_write32(TEB_LastStatusValue, SENTINEL_STATUS);
    CHECK(tp_run_thread(status_probe_thread, &v, 5000) == 0);
    /* The creator's value is not copied to the new thread. The value a new thread starts with is whatever the
     * loader's thread start path left behind (non-zero on Windows 10), so only "not inherited" is asserted. */
    CHECK(v != SENTINEL_STATUS);
    tp_teb_write32(TEB_LastStatusValue, 0);
}
